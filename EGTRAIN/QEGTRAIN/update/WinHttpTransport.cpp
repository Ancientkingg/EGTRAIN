#include "update/WinHttpTransport.h"

#include <QByteArray>
#include <QHostAddress>
#include <QMetaObject>
#include <QString>
#include <QUrl>

#include <atomic>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>
#include <winhttp.h>

// Older SDKs do not define the flag.
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

namespace {

constexpr int kMaxRedirects = 10;
constexpr DWORD kReadChunkBytes = 64 * 1024;
// WinHTTP ends a name lookup, a connection or a transfer that stalls after these times, so that the
// thread of a reply that nobody cancels ends too.
constexpr int kResolveTimeoutMs = 15000;
constexpr int kConnectTimeoutMs = 15000;
constexpr int kSendTimeoutMs = 30000;
constexpr int kReceiveTimeoutMs = 30000;

// The text WinHTTP has for an error code, followed by the code.
QString describeError(DWORD code) {
	wchar_t text[256] = {};
	const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		GetModuleHandleW(L"winhttp.dll"), code, 0, text, static_cast<DWORD>(std::size(text)), nullptr);
	const QString message = QString::fromWCharArray(text, static_cast<int>(length)).trimmed();
	if (message.isEmpty())
		return QStringLiteral("Network error %1.").arg(code);
	return QStringLiteral("%1 (error %2)").arg(message, QString::number(code));
}

bool isRedirect(DWORD status) {
	return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// The text of a response header, or an empty string when the response has none.
QString headerText(HINTERNET request, DWORD info) {
	DWORD bytes = 0;
	WinHttpQueryHeaders(request, info, nullptr, nullptr, &bytes, nullptr);
	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0)
		return QString();
	std::wstring text(bytes / sizeof(wchar_t), L'\0');
	if (!WinHttpQueryHeaders(request, info, nullptr, text.data(), &bytes, nullptr))
		return QString();
	return QString::fromWCharArray(text.c_str());
}

// The reply of one request. A worker thread makes the blocking WinHTTP calls and hands what it gets
// to the thread of the reply, which emits the signals. abort() and the destructor cancel the calls by
// closing the handles. The members from m_mutex on are shared with the worker and guarded by it.
class WinHttpReply : public HttpReply {
public:
	WinHttpReply(const HttpRequest& request, bool allowLoopbackHttp, QObject* parent) : HttpReply(parent), m_allowLoopbackHttp(allowLoopbackHttp) {
		m_thread = std::thread([this, request]() { run(request); });
	}

	~WinHttpReply() override {
		cancelWork();
		if (m_thread.joinable())
			m_thread.join();
	}

	QByteArray readAll() override {
		std::lock_guard<std::mutex> lock(m_mutex);
		QByteArray data;
		data.swap(m_buffer);
		return data;
	}

	void abort() override {
		if (m_finishedEmitted)
			return;
		cancelWork();
		m_finishedEmitted = true;
		emit finished();
	}

	bool failed() const override {
		std::lock_guard<std::mutex> lock(m_mutex);
		return !m_error.isEmpty();
	}

	QString errorString() const override {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_error;
	}

	int statusCode() const override {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_status;
	}

	qint64 contentLength() const override {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_total > 0 ? m_total : 0;
	}

private:
	// Marks the reply as cancelled and closes the handles, which ends the call that the worker is in.
	void cancelWork() {
		HINTERNET handles[3];
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_aborted = true;
			if (m_error.isEmpty())
				m_error = QStringLiteral("The request was cancelled.");
			handles[0] = std::exchange(m_request, nullptr);
			handles[1] = std::exchange(m_connection, nullptr);
			handles[2] = std::exchange(m_session, nullptr);
		}
		for (HINTERNET handle : handles) {
			if (handle)
				WinHttpCloseHandle(handle);
		}
	}

	// Worker thread.

	void run(const HttpRequest& request) {
		QString error;
		transfer(request, &error);
		release(m_request);
		release(m_connection);
		release(m_session);
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_aborted)
				return;
			m_error = error;
			m_done = true;
		}
		notify();
	}

	// Stores a handle that the worker has opened, so that cancelWork() can close it. A handle that
	// arrives after cancelWork() is closed here.
	bool adopt(HINTERNET& slot, HINTERNET handle) {
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_aborted) {
				slot = handle;
				return true;
			}
		}
		WinHttpCloseHandle(handle);
		return false;
	}

	void release(HINTERNET& slot) {
		HINTERNET handle = nullptr;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			handle = std::exchange(slot, nullptr);
		}
		if (handle)
			WinHttpCloseHandle(handle);
	}

	bool isAllowed(const QUrl& url) const {
		if (!url.isValid() || url.host().isEmpty())
			return false;
		if (url.scheme() == QLatin1String("https"))
			return true;
		QHostAddress address;
		return m_allowLoopbackHttp && url.scheme() == QLatin1String("http") && address.setAddress(url.host()) && address.isLoopback();
	}

	// Follows the redirects, then reads the body. Returns false with the error text when the request
	// fails, and false without one when cancelWork() has closed a handle.
	bool transfer(const HttpRequest& request, QString* error) {
		const std::wstring userAgent = QString::fromLatin1(request.userAgent).toStdWString();
		HINTERNET session = WinHttpOpen(userAgent.empty() ? nullptr : userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0);
		if (!session) {
			*error = describeError(GetLastError());
			return false;
		}
		if (!adopt(m_session, session))
			return false;
		WinHttpSetTimeouts(session, kResolveTimeoutMs, kConnectTimeoutMs, kSendTimeoutMs, kReceiveTimeoutMs);
		DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
		if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, static_cast<DWORD>(sizeof(protocols)))) {
			// A system that does not know TLS 1.3 refuses the flag.
			protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
			WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, static_cast<DWORD>(sizeof(protocols)));
		}

		QUrl url = request.url;
		HINTERNET handle = nullptr;
		DWORD status = 0;
		for (int redirects = 0;; ++redirects) {
			if (!isAllowed(url)) {
				*error = QStringLiteral("The address is not an https address: %1").arg(url.toString());
				return false;
			}
			if (!sendRequest(session, url, request, &handle, &status, error))
				return false;
			if (!isRedirect(status))
				break;
			if (redirects == kMaxRedirects) {
				*error = QStringLiteral("The server redirected the request too many times.");
				return false;
			}
			const QString location = headerText(handle, WINHTTP_QUERY_LOCATION);
			if (location.isEmpty()) {
				*error = QStringLiteral("The server redirected the request without an address.");
				return false;
			}
			url = url.resolved(QUrl(location));
			release(m_request);
			release(m_connection);
		}

		DWORD length = 0;
		DWORD lengthBytes = static_cast<DWORD>(sizeof(length));
		const bool hasLength = WinHttpQueryHeaders(handle, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &length, &lengthBytes, nullptr) != FALSE;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_status = static_cast<int>(status);
			if (hasLength)
				m_total = length;
		}
		if (status >= 400) {
			*error = QStringLiteral("The server replied with HTTP %1.").arg(status);
			return false;
		}

		std::vector<char> chunk(kReadChunkBytes);
		qint64 received = 0;
		for (;;) {
			DWORD read = 0;
			if (!WinHttpReadData(handle, chunk.data(), kReadChunkBytes, &read)) {
				*error = describeError(GetLastError());
				return false;
			}
			if (read == 0)
				break;
			append(chunk.data(), static_cast<int>(read));
			received += read;
		}
		if (hasLength && received != static_cast<qint64>(length)) {
			*error = QStringLiteral("The connection closed before the whole response arrived.");
			return false;
		}
		return true;
	}

	// Sends the request for url and waits for the response headers. The handle of the request stays
	// open, for the redirect address and the body.
	bool sendRequest(HINTERNET session, const QUrl& url, const HttpRequest& request, HINTERNET* handle, DWORD* status, QString* error) {
		const bool secure = url.scheme() == QLatin1String("https");
		const std::wstring host = url.host(QUrl::FullyEncoded).toStdWString();
		const INTERNET_PORT port = static_cast<INTERNET_PORT>(url.port(secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT));
		HINTERNET connection = WinHttpConnect(session, host.c_str(), port, 0);
		if (!connection) {
			*error = describeError(GetLastError());
			return false;
		}
		if (!adopt(m_connection, connection))
			return false;

		QByteArray target = url.toEncoded(QUrl::RemoveScheme | QUrl::RemoveAuthority | QUrl::RemoveFragment);
		if (!target.startsWith('/'))
			target.prepend('/');
		const std::wstring path = QString::fromLatin1(target).toStdWString();
		const std::wstring acceptType = QString::fromLatin1(request.accept).toStdWString();
		const wchar_t* acceptTypes[] = {acceptType.c_str(), nullptr};
		HINTERNET opened = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, nullptr, acceptType.empty() ? nullptr : acceptTypes,
			secure ? static_cast<DWORD>(WINHTTP_FLAG_SECURE) : 0);
		if (!opened) {
			*error = describeError(GetLastError());
			return false;
		}
		if (!adopt(m_request, opened))
			return false;
		*handle = opened;

		// The redirects are followed here, where each address is checked.
		DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
		WinHttpSetOption(opened, WINHTTP_OPTION_REDIRECT_POLICY, &policy, static_cast<DWORD>(sizeof(policy)));

		if (!WinHttpSendRequest(opened, nullptr, 0, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(opened, nullptr)) {
			*error = describeError(GetLastError());
			return false;
		}
		DWORD statusBytes = static_cast<DWORD>(sizeof(*status));
		if (!WinHttpQueryHeaders(opened, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, status, &statusBytes, nullptr)) {
			*error = describeError(GetLastError());
			return false;
		}
		return true;
	}

	void append(const char* data, int size) {
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_buffer.append(data, size);
			m_received += size;
		}
		notify();
	}

	// Asks the thread of the reply to call deliver(). One call serves all the data that arrives
	// before it runs.
	void notify() {
		if (!m_notifyQueued.exchange(true))
			QMetaObject::invokeMethod(this, [this]() { deliver(); }, Qt::QueuedConnection);
	}

	// Thread of the reply.

	void deliver() {
		m_notifyQueued = false;
		if (m_finishedEmitted)
			return;
		bool hasData = false;
		bool done = false;
		qint64 received = 0;
		qint64 total = -1;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			hasData = !m_buffer.isEmpty();
			done = m_done;
			received = m_received;
			total = m_total;
		}
		// A slot may abort the reply, which emits finished().
		if (hasData) {
			emit readyRead();
			if (m_finishedEmitted)
				return;
		}
		if (received != m_reportedReceived) {
			m_reportedReceived = received;
			emit downloadProgress(received, total);
			if (m_finishedEmitted)
				return;
		}
		if (done) {
			m_finishedEmitted = true;
			emit finished();
		}
	}

	const bool m_allowLoopbackHttp;
	std::atomic<bool> m_notifyQueued{false};
	// Only the thread of the reply uses these two.
	bool m_finishedEmitted = false;
	qint64 m_reportedReceived = 0;

	mutable std::mutex m_mutex;
	QByteArray m_buffer;
	qint64 m_received = 0;
	qint64 m_total = -1;
	int m_status = 0;
	QString m_error;
	bool m_done = false;
	bool m_aborted = false;
	HINTERNET m_session = nullptr;
	HINTERNET m_connection = nullptr;
	HINTERNET m_request = nullptr;

	// Started last, once the members it uses exist.
	std::thread m_thread;
};

class WinHttpTransport : public HttpTransport {
public:
	WinHttpTransport(QObject* parent, bool allowLoopbackHttp) : HttpTransport(parent), m_allowLoopbackHttp(allowLoopbackHttp) {}

	HttpReply* get(const HttpRequest& request) override { return new WinHttpReply(request, m_allowLoopbackHttp, this); }

private:
	const bool m_allowLoopbackHttp;
};

} // namespace

HttpTransport* createWinHttpTransport(QObject* parent, bool allowLoopbackHttp) {
	return new WinHttpTransport(parent, allowLoopbackHttp);
}
