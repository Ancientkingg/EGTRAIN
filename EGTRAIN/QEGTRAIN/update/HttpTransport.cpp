#include "update/HttpTransport.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

#ifdef Q_OS_WIN
#include "update/WinHttpTransport.h"
#endif

namespace {

// The QNetworkReply stays a child of the network manager, so a manager that is destroyed deletes
// its replies first. The pointer is null after that.
class QtHttpReply : public HttpReply {
public:
	QtHttpReply(QNetworkReply* reply, QObject* parent) : HttpReply(parent), m_reply(reply) {
		connect(reply, &QNetworkReply::readyRead, this, &HttpReply::readyRead);
		connect(reply, &QNetworkReply::downloadProgress, this, &HttpReply::downloadProgress);
		connect(reply, &QNetworkReply::finished, this, &HttpReply::finished);
	}

	~QtHttpReply() override {
		if (m_reply)
			m_reply->deleteLater();
	}

	QByteArray readAll() override { return m_reply->readAll(); }
	void abort() override { m_reply->abort(); }
	bool failed() const override { return m_reply->error() != QNetworkReply::NoError; }
	QString errorString() const override { return m_reply->errorString(); }
	int statusCode() const override { return m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); }
	qint64 contentLength() const override { return m_reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(); }

private:
	QPointer<QNetworkReply> m_reply;
};

class QtHttpTransport : public HttpTransport {
public:
	explicit QtHttpTransport(QObject* parent) : HttpTransport(parent), m_network(this) {}

	HttpReply* get(const HttpRequest& request) override {
		QNetworkRequest networkRequest(request.url);
		if (!request.userAgent.isEmpty())
			networkRequest.setHeader(QNetworkRequest::UserAgentHeader, request.userAgent);
		if (!request.accept.isEmpty())
			networkRequest.setRawHeader("Accept", request.accept);
		networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
			QNetworkRequest::NoLessSafeRedirectPolicy);
		return new QtHttpReply(m_network.get(networkRequest), this);
	}

private:
	QNetworkAccessManager m_network;
};

} // namespace

HttpTransport* createHttpTransport(QObject* parent, [[maybe_unused]] bool allowLoopbackHttp) {
#ifdef Q_OS_WIN
	return createWinHttpTransport(parent, allowLoopbackHttp);
#else
	return new QtHttpTransport(parent);
#endif
}
