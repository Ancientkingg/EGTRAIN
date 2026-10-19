#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkReply;
class QNetworkRequest;

/// One request. An empty userAgent or accept sets no such header. body, contentType and extraRootPem
/// belong to post() and are ignored by get().
struct HttpRequest {
	QUrl url;				///< The address to fetch or to post to.
	QByteArray userAgent;	///< The value of the User-Agent header.
	QByteArray accept;		///< The value of the Accept header.
	QByteArray body;		///< The bytes that post() sends.
	QByteArray contentType; ///< The value of the Content-Type header of a post. No Content-Type header when empty.
	/// Only a test sets this: a PEM root certificate that the certificate of a server on a loopback
	/// address is checked against, for a server whose certificate the system does not trust.
	QByteArray extraRootPem;
};

/// The response of one request.
///   readAll()        returns the bytes received since the last call.
///   failed()         is true for a transport error, a cancelled request and an HTTP error status (4xx and 5xx).
///   statusCode()     is 0 when no response arrived.
///   contentLength()  is 0 when the response states none.
///   header(name)     returns the value of a response header, or an empty array when the response has none.
///   finished()       is emitted once, also after abort().
/// A redirect of a get() is followed, but never from https to http.
class HttpReply : public QObject {
	Q_OBJECT

public:
	/// Makes a reply owned by parent, normally the transport that sent the request.
	explicit HttpReply(QObject* parent = nullptr) : QObject(parent) {}

	/// Returns the bytes received since the last call and removes them from the reply.
	virtual QByteArray readAll() = 0;
	/// Cancels the request. finished() is emitted if it was not emitted yet.
	virtual void abort() = 0;
	/// True for a transport error, a cancelled request and an HTTP error status (4xx and 5xx).
	virtual bool failed() const = 0;
	/// A description of the failure for the user.
	virtual QString errorString() const = 0;
	/// The HTTP status code of the response, or 0 when no response arrived.
	virtual int statusCode() const = 0;
	/// The length of the body in bytes that the response states, or 0 when it states none.
	virtual qint64 contentLength() const = 0;
	/// The value of the response header with this name, or an empty array when the response has none.
	/// The default reply has no headers.
	virtual QByteArray header(const QByteArray&) const { return QByteArray(); }

signals:
	/// New bytes can be taken with readAll().
	void readyRead();
	/// The bytes of the body received so far and the total in bytes, or -1 when the total is not known.
	void downloadProgress(qint64 received, qint64 total);
	/// The request ended, whether it succeeded, failed or was cancelled. Emitted once.
	void finished();
};

/// Sends requests. A reply is a child of its transport; the caller releases it with deleteLater().
class HttpTransport : public QObject {
public:
	/// Makes a transport owned by parent.
	explicit HttpTransport(QObject* parent = nullptr) : QObject(parent) {}

	/// Starts a GET of request.url and returns its reply, which the transport owns.
	virtual HttpReply* get(const HttpRequest& request) = 0;
	/// Sends request.body with a POST. A redirect is not followed: its reply is the reply of the post,
	/// without an error. The request carries no cookie and no credentials. The default transport cannot
	/// post and returns nullptr.
	virtual HttpReply* post(const HttpRequest&) { return nullptr; }
};

/// Returns the transport the application uses: WinHTTP on Windows, where Qt has no TLS library, and
/// Qt's network manager elsewhere. The Windows transport fetches https addresses only, unless
/// allowLoopbackHttp is true, which only a test sets so that it can use a server on 127.0.0.1.
/// The transport is a child of parent.
HttpTransport* createHttpTransport(QObject* parent, bool allowLoopbackHttp = false);

/// For a test that scripts replies of Qt's network manager in place of a server: the request that the
/// Qt transport hands to the network manager for a post().
QNetworkRequest qtPostRequest(const HttpRequest& request);
/// For a test that scripts replies of Qt's network manager: a reply that wraps a reply of a network
/// manager. The wrapper is a child of parent and deletes the wrapped reply with deleteLater().
HttpReply* wrapQtReply(QNetworkReply* reply, QObject* parent);
