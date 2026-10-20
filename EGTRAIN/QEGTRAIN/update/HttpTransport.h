#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkReply;
class QNetworkRequest;

// One request. An empty userAgent or accept sets no such header. body, contentType and extraRootPem
// belong to post() and are ignored by get().
struct HttpRequest {
	QUrl url;
	QByteArray userAgent;
	QByteArray accept;
	QByteArray body;
	QByteArray contentType; // No Content-Type header when empty.
	// Only a test sets this: a PEM root certificate that the certificate of a server on a loopback
	// address is checked against, for a server whose certificate the system does not trust.
	QByteArray extraRootPem;
};

// The response of one request.
//   readAll()        returns the bytes received since the last call.
//   failed()         is true for a transport error, a cancelled request and an HTTP error status (4xx and 5xx).
//   statusCode()     is 0 when no response arrived.
//   contentLength()  is 0 when the response states none.
//   header(name)     returns the value of a response header, or an empty array when the response has none.
//   finished()       is emitted once, also after abort().
// A redirect of a get() is followed, but never from https to http.
class HttpReply : public QObject {
	Q_OBJECT

public:
	explicit HttpReply(QObject* parent = nullptr) : QObject(parent) {}

	virtual QByteArray readAll() = 0;
	virtual void abort() = 0;
	virtual bool failed() const = 0;
	virtual QString errorString() const = 0;
	virtual int statusCode() const = 0;
	virtual qint64 contentLength() const = 0;
	// The default reply has no headers.
	virtual QByteArray header(const QByteArray&) const { return QByteArray(); }

signals:
	void readyRead();
	void downloadProgress(qint64 received, qint64 total);
	void finished();
};

// Sends requests. A reply is a child of its transport; the caller releases it with deleteLater().
class HttpTransport : public QObject {
public:
	explicit HttpTransport(QObject* parent = nullptr) : QObject(parent) {}

	virtual HttpReply* get(const HttpRequest& request) = 0;
	// Sends request.body with a POST. A redirect is not followed: its reply is the reply of the post,
	// without an error. The request carries no cookie and no credentials. The default transport cannot
	// post and returns nullptr.
	virtual HttpReply* post(const HttpRequest&) { return nullptr; }
};

// Returns the transport the application uses: WinHTTP on Windows, where Qt has no TLS library, and
// Qt's network manager elsewhere. The Windows transport fetches https addresses only, unless
// allowLoopbackHttp is true, which only a test sets so that it can use a server on 127.0.0.1.
HttpTransport* createHttpTransport(QObject* parent, bool allowLoopbackHttp = false);

// For a test that scripts replies of Qt's network manager in place of a server: the request that the
// Qt transport hands to the network manager for a post(), and a reply that wraps a reply of a
// network manager. The wrapper is a child of parent and deletes the wrapped reply with deleteLater().
QNetworkRequest qtPostRequest(const HttpRequest& request);
HttpReply* wrapQtReply(QNetworkReply* reply, QObject* parent);
