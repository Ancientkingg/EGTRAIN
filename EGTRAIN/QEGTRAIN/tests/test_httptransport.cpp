#include "update/HttpTransport.h"

#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

#include <vector>

static constexpr int kGuardMs = 10000;

// Runs the event loop for the given time.
static void spin(int milliseconds) {
	QEventLoop loop;
	QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
	loop.exec();
}

static QByteArray smallBody() {
	return QByteArrayLiteral("{\"update\":\"transport\"}");
}

static QByteArray targetBody() {
	return QByteArrayLiteral("the redirect target");
}

static QByteArray bigBody() {
	QByteArray body(300 * 1000, '\0');
	char* out = body.data();
	for (int i = 0; i < body.size(); ++i)
		out[i] = static_cast<char>((i * 31 + i / 256) % 251);
	return body;
}

struct RecordedRequest {
	QByteArray method;
	QByteArray path;
	QByteArray userAgent;
	QByteArray accept;
	QByteArray contentType;
	QByteArray cookie;
	QByteArray body;
};

// A small HTTP server on the loopback address that answers by path. It closes the connection after
// each response, except on /stall, which sends part of a body and then waits, and on /drop, which
// closes it without a response.
struct TestServer {
private:
	// Declared before the server, so it outlives the sockets whose disconnected handlers use it.
	QHash<QTcpSocket*, QByteArray> m_pending;

public:
	TestServer() {
		listening = server.listen(QHostAddress::LocalHost, 0);
		QObject::connect(&server, &QTcpServer::newConnection, &server, [this]() { accept(); });
	}

	QUrl url(const QByteArray& path) const {
		return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(QString::fromLatin1(path)));
	}

	// The last request for the path, or null.
	const RecordedRequest* find(const QByteArray& path) const {
		for (auto it = requests.rbegin(); it != requests.rend(); ++it)
			if (it->path == path)
				return &*it;
		return nullptr;
	}

	QTcpServer server;
	bool listening = false;
	std::vector<RecordedRequest> requests;

private:
	void accept() {
		while (QTcpSocket* socket = server.nextPendingConnection()) {
			QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() { readRequest(socket); });
			QObject::connect(socket, &QTcpSocket::disconnected, socket, [this, socket]() {
				m_pending.remove(socket);
				socket->deleteLater();
			});
		}
	}

	void readRequest(QTcpSocket* socket) {
		QByteArray& buffer = m_pending[socket];
		buffer += socket->readAll();
		const auto end = buffer.indexOf("\r\n\r\n");
		if (end < 0)
			return;
		RecordedRequest request;
		const QList<QByteArray> lines = buffer.left(end).split('\n');
		const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
		request.method = requestLine.first();
		if (requestLine.size() >= 2)
			request.path = requestLine.at(1);
		int bodyBytes = 0;
		for (const QByteArray& line : lines) {
			const auto colon = line.indexOf(':');
			if (colon < 0)
				continue;
			const QByteArray name = line.left(colon).trimmed().toLower();
			if (name == "user-agent")
				request.userAgent = line.mid(colon + 1).trimmed();
			else if (name == "accept")
				request.accept = line.mid(colon + 1).trimmed();
			else if (name == "content-type")
				request.contentType = line.mid(colon + 1).trimmed();
			else if (name == "cookie")
				request.cookie = line.mid(colon + 1).trimmed();
			else if (name == "content-length")
				bodyBytes = line.mid(colon + 1).trimmed().toInt();
		}
		if (buffer.size() < end + 4 + bodyBytes)
			return;
		request.body = buffer.mid(end + 4, bodyBytes);
		m_pending.remove(socket);
		requests.push_back(request);
		respond(socket, request.path);
	}

	void respond(QTcpSocket* socket, const QByteArray& path) {
		if (path == "/redirect")
			send(socket, "302 Found", QByteArray(), "Location: " + url("/target").toEncoded() + "\r\n");
		else if (path == "/redirect-relative")
			send(socket, "302 Found", QByteArray(), "Location: /target\r\n");
		else if (path == "/redirect-loop")
			send(socket, "302 Found", QByteArray(), "Location: " + url("/redirect-loop").toEncoded() + "\r\n");
		else if (path == "/redirect-away")
			send(socket, "302 Found", QByteArray(), "Location: http://localhost:" + QByteArray::number(server.serverPort()) + "/redirect-away-target\r\n");
		else if (path == "/post-redirect")
			send(socket, "302 Found", QByteArray(), "Location: " + url("/post-target").toEncoded() + "\r\n");
		else if (path == "/post-accepted")
			send(socket, "202 Accepted", QByteArray(), "Retry-After: 120\r\n");
		else if (path == "/set-cookie")
			send(socket, "200 OK", smallBody(), "Set-Cookie: session=1; Path=/\r\n");
		else if (path == "/target")
			send(socket, "200 OK", targetBody());
		else if (path == "/missing")
			send(socket, "404 Not Found", QByteArrayLiteral("no such file"));
		else if (path == "/big")
			send(socket, "200 OK", bigBody());
		else if (path == "/drop")
			socket->disconnectFromHost();
		else if (path == "/stall")
			socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: 100000\r\nConnection: close\r\n\r\n")
				+ QByteArray(4096, 'x'));
		else
			send(socket, "200 OK", smallBody());
	}

	static void send(QTcpSocket* socket, const QByteArray& status, const QByteArray& body, const QByteArray& extraHeaders = QByteArray()) {
		socket->write("HTTP/1.1 " + status + "\r\nContent-Length: " + QByteArray::number(body.size())
			+ "\r\nConnection: close\r\n" + extraHeaders + "\r\n" + body);
		socket->disconnectFromHost();
	}
};

// One request and everything its reply reports.
struct Fetch {
	Fetch(HttpTransport& transport, const HttpRequest& request, bool post = false) : reply(post ? transport.post(request) : transport.get(request)) {
		QObject::connect(reply, &HttpReply::readyRead, &context, [this]() {
			++readyReads;
			data += reply->readAll();
			if (abortOnData && !abortCalled) {
				abortCalled = true;
				reply->abort();
			}
		});
		QObject::connect(reply, &HttpReply::downloadProgress, &context, [this](qint64 received, qint64 total) {
			lastReceived = received;
			lastTotal = total;
		});
		QObject::connect(reply, &HttpReply::finished, &context, [this]() {
			++finishedCount;
			failed = reply->failed();
			status = reply->statusCode();
			error = reply->errorString();
			contentLength = reply->contentLength();
			retryAfter = reply->header("retry-after");
			location = reply->header("Location");
			if (!failed)
				data += reply->readAll();
			if (loop)
				loop->quit();
		});
	}

	~Fetch() {
		if (reply)
			reply->deleteLater();
	}

	// Runs the event loop until finished() has arrived or the guard time has passed.
	bool waitFinished(int guardMs = kGuardMs) {
		if (finishedCount > 0)
			return true;
		QEventLoop eventLoop;
		QTimer guard;
		guard.setSingleShot(true);
		QObject::connect(&guard, &QTimer::timeout, &eventLoop, &QEventLoop::quit);
		loop = &eventLoop;
		guard.start(guardMs);
		eventLoop.exec();
		loop = nullptr;
		return finishedCount > 0;
	}

	QObject context;
	QPointer<HttpReply> reply;
	QEventLoop* loop = nullptr;
	QByteArray data;
	int readyReads = 0;
	int finishedCount = 0;
	qint64 lastReceived = -1;
	qint64 lastTotal = -1;
	bool failed = false;
	int status = -1;
	QString error;
	qint64 contentLength = -1;
	QByteArray retryAfter;
	QByteArray location;
	bool abortOnData = false;
	bool abortCalled = false;
};

static HttpRequest makeRequest(const QUrl& url, const QByteArray& userAgent = QByteArray(), const QByteArray& accept = QByteArray()) {
	HttpRequest request;
	request.url = url;
	request.userAgent = userAgent;
	request.accept = accept;
	return request;
}

static HttpRequest makePost(const QUrl& url, const QByteArray& body) {
	HttpRequest request = makeRequest(url, "EGTRAIN/9.9.9");
	request.body = body;
	request.contentType = QByteArrayLiteral("application/json; charset=utf-8");
	return request;
}

class TestHttpTransport : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void readsSmallBody();
	void requestHeaders();
	void postsBody();
	void postRedirectNotFollowed();
	void postErrorStatus();
	void postSendsNoCookie();
	void readsLargeBody();
	void redirect();
	void relativeRedirect();
	void redirectLoop();
	void errorStatus();
	void abortDuringResponse();
	void connectionDropped();
	void connectionRefused();
	void releaseAfterFinished();
	void teardownDuringRequest();
	void plainHttpRefused();
	void redirectToPlainHttpRefused();

private:
	TestServer m_server;
	// The server speaks plain http, which the transport of the application refuses on Windows unless a test allows it.
	HttpTransport* m_transport = nullptr;
};

void TestHttpTransport::initTestCase() {
	QVERIFY2(m_server.listening, "the test server listens on the loopback address");
	m_transport = createHttpTransport(this, true);
}

void TestHttpTransport::readsSmallBody() {
	Fetch fetch(*m_transport, makeRequest(m_server.url("/small")));
	QVERIFY2(fetch.waitFinished(), "small body: finished() arrives");
	spin(50);
	// finished() is emitted once.
	QCOMPARE(fetch.finishedCount, 1);
	QVERIFY2(!fetch.failed, "small body: the reply has not failed");
	// The status code is 200.
	QCOMPARE(fetch.status, 200);
	// contentLength() is the size of the body.
	QCOMPARE(fetch.contentLength, smallBody().size());
	// readAll() returns the body.
	QCOMPARE(fetch.data, smallBody());
}

void TestHttpTransport::requestHeaders() {
	const QByteArray accept = QByteArrayLiteral("application/vnd.github+json");
	Fetch first(*m_transport, makeRequest(m_server.url("/headers-first"), "EGTRAIN/9.9.9", accept));
	QVERIFY2(first.waitFinished(), "headers: the first request finishes");
	const RecordedRequest* recorded = m_server.find("/headers-first");
	if (recorded == nullptr)
		QFAIL("headers: the server saw the first request");
	// The User-Agent is the one of the request.
	QCOMPARE(recorded->userAgent, QByteArray("EGTRAIN/9.9.9"));
	// The Accept header is the one of the request.
	QCOMPARE(recorded->accept, accept);

	Fetch second(*m_transport, makeRequest(m_server.url("/headers-second"), "EGTRAIN/9.9.9"));
	QVERIFY2(second.waitFinished(), "headers: the second request finishes");
	recorded = m_server.find("/headers-second");
	if (recorded == nullptr)
		QFAIL("headers: the server saw the second request");
	QVERIFY2(recorded->accept != accept, "headers: an empty accept does not send the Accept value of an earlier request");
}

void TestHttpTransport::postsBody() {
	const QByteArray body = bigBody();
	Fetch fetch(*m_transport, makePost(m_server.url("/post-accepted"), body), true);
	QVERIFY2(fetch.reply != nullptr, "post: the transport posts");
	QVERIFY2(fetch.waitFinished(), "post: finished() arrives");
	// The status code is 202.
	QVERIFY(!fetch.failed);
	QCOMPARE(fetch.status, 202);
	// header() returns a header of the response, whatever its case.
	QCOMPARE(fetch.retryAfter, QByteArray("120"));
	const RecordedRequest* recorded = m_server.find("/post-accepted");
	if (recorded == nullptr)
		QFAIL("post: the server saw the request");
	// The method is POST.
	QCOMPARE(recorded->method, QByteArray("POST"));
	// The Content-Type is the one of the request.
	QCOMPARE(recorded->contentType, QByteArray("application/json; charset=utf-8"));
	// The User-Agent is the one of the request.
	QCOMPARE(recorded->userAgent, QByteArray("EGTRAIN/9.9.9"));
	// The body arrives whole.
	QCOMPARE(recorded->body, body);
}

// A redirect of a post is a reply like any other: it is not followed.
void TestHttpTransport::postRedirectNotFollowed() {
	Fetch fetch(*m_transport, makePost(m_server.url("/post-redirect"), smallBody()), true);
	QVERIFY2(fetch.waitFinished(), "post redirect: finished() arrives");
	// The reply is the redirect, without an error.
	QVERIFY(!fetch.failed);
	QCOMPARE(fetch.status, 302);
	// header() returns the Location.
	QCOMPARE(fetch.location, m_server.url("/post-target").toEncoded());
	spin(50);
	QVERIFY2(m_server.find("/post-target") == nullptr, "post redirect: the redirect is not followed");
}

void TestHttpTransport::postErrorStatus() {
	Fetch fetch(*m_transport, makePost(m_server.url("/missing"), smallBody()), true);
	QVERIFY2(fetch.waitFinished(), "post 404: finished() arrives");
	// An HTTP error status is a failure with its status code.
	QVERIFY(fetch.failed);
	QCOMPARE(fetch.status, 404);
}

// A cookie that an earlier response set is not sent with a post.
void TestHttpTransport::postSendsNoCookie() {
	Fetch first(*m_transport, makeRequest(m_server.url("/set-cookie")));
	QVERIFY2(first.waitFinished(), "cookie: the request that sets a cookie finishes");
	Fetch second(*m_transport, makePost(m_server.url("/cookie-check"), smallBody()), true);
	QVERIFY2(second.waitFinished(), "cookie: the post finishes");
	const RecordedRequest* recorded = m_server.find("/cookie-check");
	if (recorded == nullptr)
		QFAIL("cookie: the server saw the post");
	QVERIFY2(recorded->cookie.isEmpty(), "cookie: the post sends no cookie");
}

void TestHttpTransport::readsLargeBody() {
	Fetch fetch(*m_transport, makeRequest(m_server.url("/big")));
	const QByteArray body = bigBody();
	QVERIFY2(fetch.waitFinished(), "large body: finished() arrives");
	// The request succeeds.
	QVERIFY(!fetch.failed);
	QCOMPARE(fetch.status, 200);
	// readyRead is emitted.
	QCOMPARE_GE(fetch.readyReads, 1);
	// downloadProgress reaches the size of the body.
	QCOMPARE(fetch.lastReceived, body.size());
	QCOMPARE(fetch.lastTotal, body.size());
	// All bytes arrive.
	QCOMPARE(fetch.data, body);
}

void TestHttpTransport::redirect() {
	Fetch fetch(*m_transport, makeRequest(m_server.url("/redirect")));
	QVERIFY2(fetch.waitFinished(), "redirect: finished() arrives");
	// The reply is the one of the target.
	QVERIFY(!fetch.failed);
	QCOMPARE(fetch.status, 200);
	// The body is the one of the target.
	QCOMPARE(fetch.data, targetBody());
}

void TestHttpTransport::relativeRedirect() {
	Fetch fetch(*m_transport, makeRequest(m_server.url("/redirect-relative")));
	QVERIFY2(fetch.waitFinished(), "relative redirect: finished() arrives");
	// The reply is the one of the target.
	QVERIFY(!fetch.failed);
	QCOMPARE(fetch.status, 200);
	// The body is the one of the target.
	QCOMPARE(fetch.data, targetBody());
}

// A server that redirects to its own address: the transport gives up after a limited number of redirects.
void TestHttpTransport::redirectLoop() {
	Fetch fetch(*m_transport, makeRequest(m_server.url("/redirect-loop")));
	QVERIFY2(fetch.waitFinished(), "redirect loop: finished() arrives");
	spin(50);
	// finished() is emitted once.
	QCOMPARE(fetch.finishedCount, 1);
	QVERIFY2(fetch.failed, "redirect loop: the reply has failed");
	QVERIFY2(!fetch.error.isEmpty(), "redirect loop: errorString() is not empty");
}

void TestHttpTransport::errorStatus() {
	Fetch fetch(*m_transport, makeRequest(m_server.url("/missing")));
	QVERIFY2(fetch.waitFinished(), "404: finished() arrives");
	QVERIFY2(fetch.failed, "404: an HTTP error status is a failure");
	// The status code is 404.
	QCOMPARE(fetch.status, 404);
	QVERIFY2(!fetch.error.isEmpty(), "404: errorString() is not empty");
}

void TestHttpTransport::abortDuringResponse() {
	Fetch fetch(*m_transport, makeRequest(m_server.url("/stall")));
	fetch.abortOnData = true;
	QVERIFY2(fetch.waitFinished(), "abort: finished() arrives");
	QVERIFY2(fetch.abortCalled, "abort: the response had started when abort() was called");
	QVERIFY2(fetch.failed, "abort: a cancelled request has failed");
	QVERIFY2(!fetch.error.isEmpty(), "abort: errorString() is not empty");
	spin(200);
	// finished() is emitted once.
	QCOMPARE(fetch.finishedCount, 1);
}

void TestHttpTransport::connectionDropped() {
	Fetch fetch(*m_transport, makeRequest(m_server.url("/drop")));
	QVERIFY2(fetch.waitFinished(), "dropped: finished() arrives");
	QVERIFY2(fetch.failed, "dropped: the reply has failed");
	// No status code arrives.
	QCOMPARE(fetch.status, 0);
	QVERIFY2(!fetch.error.isEmpty(), "dropped: errorString() is not empty");
}

// Nothing listens on the port: the server that had it is closed. Some Qt builds report the refused
// connection and some leave the request pending; a request that is still pending after a short wait is
// aborted, as the timeouts of UpdateChecker and SelfUpdater do. Either way the reply must end as failed.
void TestHttpTransport::connectionRefused() {
	QUrl url;
	{
		QTcpServer closed;
		QVERIFY2(closed.listen(QHostAddress::LocalHost, 0), "refused: a free port is found");
		url = QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(closed.serverPort()));
	}
	Fetch fetch(*m_transport, makeRequest(url));
	if (!fetch.waitFinished(1000)) {
		if (fetch.reply == nullptr)
			QFAIL("refused: the reply exists while it is pending");
		fetch.reply->abort();
		QVERIFY2(fetch.waitFinished(), "refused: finished() arrives once a pending request is aborted");
	}
	spin(50);
	// finished() is emitted once.
	QCOMPARE(fetch.finishedCount, 1);
	QVERIFY2(fetch.failed, "refused: the reply has failed");
	// No status code arrives.
	QCOMPARE(fetch.status, 0);
	QVERIFY2(!fetch.error.isEmpty(), "refused: errorString() is not empty");
}

void TestHttpTransport::releaseAfterFinished() {
	QPointer<HttpReply> released;
	{
		Fetch fetch(*m_transport, makeRequest(m_server.url("/small")));
		QVERIFY2(fetch.waitFinished(), "release: the first request finishes");
		released = fetch.reply;
		fetch.reply->deleteLater();
		spin(50);
		QVERIFY2(released.isNull(), "release: the reply is deleted after deleteLater()");
	}
	Fetch next(*m_transport, makeRequest(m_server.url("/small")));
	QVERIFY2(next.waitFinished(), "release: a further request finishes");
	// A further request succeeds.
	QVERIFY(!next.failed);
	QCOMPARE(next.data, smallBody());
}

// Deleting the transport with a request in flight deletes the reply, and no signal arrives afterwards.
void TestHttpTransport::teardownDuringRequest() {
	HttpTransport* transport = createHttpTransport(nullptr, true);
	QPointer<HttpReply> reply = transport->get(makeRequest(m_server.url("/stall"), "EGTRAIN/test"));
	QObject context;
	int signalCount = 0;
	bool dataArrived = false;
	QEventLoop loop;
	QTimer guard;
	guard.setSingleShot(true);
	QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
	QObject::connect(reply, &HttpReply::readyRead, &context, [&]() {
		++signalCount;
		dataArrived = true;
		loop.quit();
	});
	QObject::connect(reply, &HttpReply::downloadProgress, &context, [&]() { ++signalCount; });
	QObject::connect(reply, &HttpReply::finished, &context, [&]() { ++signalCount; });
	guard.start(kGuardMs);
	loop.exec();
	guard.stop();

	const bool arrivedBeforeDelete = dataArrived;
	const int signalsBefore = signalCount;
	delete transport;
	QVERIFY2(arrivedBeforeDelete, "teardown: data arrives before the transport is deleted");
	QVERIFY2(reply.isNull(), "teardown: the reply is deleted with the transport");
	spin(200);
	// No signal arrives after the transport is deleted.
	QCOMPARE(signalCount, signalsBefore);
}

// The transport of the application fetches https addresses only, so a request for plain http fails
// without a connection to the server.
void TestHttpTransport::plainHttpRefused() {
#ifdef Q_OS_WIN
	HttpTransport* strict = createHttpTransport(this);
	{
		Fetch fetch(*strict, makeRequest(m_server.url("/plain-http")));
		QVERIFY2(fetch.waitFinished(), "plain http: finished() arrives");
		QVERIFY2(fetch.failed, "plain http: the reply has failed");
		// No status code arrives.
		QCOMPARE(fetch.status, 0);
		QVERIFY2(!fetch.error.isEmpty(), "plain http: errorString() is not empty");
	}
	spin(50);
	QVERIFY2(m_server.find("/plain-http") == nullptr, "plain http: no request reaches the server");
#else
	QSKIP("Only the transport on Windows refuses plain http.");
#endif
}

// With plain http to the loopback address allowed, a redirect to another plain http address is still
// refused. The redirect names this server by host name, which is not the loopback address the test
// allows, so a transport that followed it would send the request to this server.
void TestHttpTransport::redirectToPlainHttpRefused() {
#ifdef Q_OS_WIN
	Fetch fetch(*m_transport, makeRequest(m_server.url("/redirect-away")));
	QVERIFY2(fetch.waitFinished(), "redirect to http: finished() arrives");
	QVERIFY2(fetch.failed, "redirect to http: the reply has failed");
	QVERIFY2(!fetch.error.isEmpty(), "redirect to http: errorString() is not empty");
	spin(50);
	QVERIFY2(m_server.find("/redirect-away-target") == nullptr, "redirect to http: the redirect is not followed");
#else
	QSKIP("Only the transport on Windows refuses plain http.");
#endif
}

QTEST_GUILESS_MAIN(TestHttpTransport)
#include "test_httptransport.moc"
