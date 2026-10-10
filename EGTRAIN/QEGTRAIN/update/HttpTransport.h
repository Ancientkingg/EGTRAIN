#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

// One GET request. An empty userAgent or accept sets no such header.
struct HttpRequest {
	QUrl url;
	QByteArray userAgent;
	QByteArray accept;
};

// The response of one GET request.
//   readAll()        returns the bytes received since the last call.
//   failed()         is true for a transport error, a cancelled request and an HTTP error status (4xx and 5xx).
//   statusCode()     is 0 when no response arrived.
//   contentLength()  is 0 when the response states none.
//   finished()       is emitted once, also after abort().
// A redirect is followed, but never from https to http.
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

signals:
	void readyRead();
	void downloadProgress(qint64 received, qint64 total);
	void finished();
};

// Sends GET requests. A reply is a child of its transport; the caller releases it with deleteLater().
class HttpTransport : public QObject {
public:
	explicit HttpTransport(QObject* parent = nullptr) : QObject(parent) {}

	virtual HttpReply* get(const HttpRequest& request) = 0;
};

// Returns the transport the application uses.
HttpTransport* createHttpTransport(QObject* parent);
