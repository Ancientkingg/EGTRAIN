#include "update/HttpTransport.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <iostream>
#include <vector>

static constexpr int kGuardMs = 10000;

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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
		const int end = buffer.indexOf("\r\n\r\n");
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
			const int colon = line.indexOf(':');
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

static bool testSmallBody(HttpTransport& transport, const TestServer& server) {
	Fetch fetch(transport, makeRequest(server.url("/small")));
	bool ok = expect(fetch.waitFinished(), "small body: finished() arrives");
	spin(50);
	ok &= expect(fetch.finishedCount == 1, "small body: finished() is emitted once");
	ok &= expect(!fetch.failed, "small body: the reply has not failed");
	ok &= expect(fetch.status == 200, "small body: the status code is 200");
	ok &= expect(fetch.contentLength == smallBody().size(), "small body: contentLength() is the size of the body");
	ok &= expect(fetch.data == smallBody(), "small body: readAll() returns the body");
	return ok;
}

static bool testRequestHeaders(HttpTransport& transport, const TestServer& server) {
	const QByteArray accept = QByteArrayLiteral("application/vnd.github+json");
	Fetch first(transport, makeRequest(server.url("/headers-first"), "EGTRAIN/9.9.9", accept));
	bool ok = expect(first.waitFinished(), "headers: the first request finishes");
	const RecordedRequest* recorded = server.find("/headers-first");
	ok &= expect(recorded != nullptr, "headers: the server saw the first request");
	if (recorded) {
		ok &= expect(recorded->userAgent == "EGTRAIN/9.9.9", "headers: the User-Agent is the one of the request");
		ok &= expect(recorded->accept == accept, "headers: the Accept header is the one of the request");
	}

	Fetch second(transport, makeRequest(server.url("/headers-second"), "EGTRAIN/9.9.9"));
	ok &= expect(second.waitFinished(), "headers: the second request finishes");
	recorded = server.find("/headers-second");
	ok &= expect(recorded != nullptr, "headers: the server saw the second request");
	if (recorded)
		ok &= expect(recorded->accept != accept, "headers: an empty accept does not send the Accept value of an earlier request");
	return ok;
}

static bool testPost(HttpTransport& transport, const TestServer& server) {
	const QByteArray body = bigBody();
	Fetch fetch(transport, makePost(server.url("/post-accepted"), body), true);
	bool ok = expect(fetch.reply != nullptr, "post: the transport posts");
	if (!ok)
		return false;
	ok &= expect(fetch.waitFinished(), "post: finished() arrives");
	ok &= expect(!fetch.failed && fetch.status == 202, "post: the status code is 202");
	ok &= expect(fetch.retryAfter == "120", "post: header() returns a header of the response, whatever its case");
	const RecordedRequest* recorded = server.find("/post-accepted");
	ok &= expect(recorded != nullptr, "post: the server saw the request");
	if (recorded) {
		ok &= expect(recorded->method == "POST", "post: the method is POST");
		ok &= expect(recorded->contentType == "application/json; charset=utf-8", "post: the Content-Type is the one of the request");
		ok &= expect(recorded->userAgent == "EGTRAIN/9.9.9", "post: the User-Agent is the one of the request");
		ok &= expect(recorded->body == body, "post: the body arrives whole");
	}
	return ok;
}

// A redirect of a post is a reply like any other: it is not followed.
static bool testPostRedirectNotFollowed(HttpTransport& transport, const TestServer& server) {
	Fetch fetch(transport, makePost(server.url("/post-redirect"), smallBody()), true);
	bool ok = expect(fetch.waitFinished(), "post redirect: finished() arrives");
	ok &= expect(!fetch.failed && fetch.status == 302, "post redirect: the reply is the redirect, without an error");
	ok &= expect(fetch.location == server.url("/post-target").toEncoded(), "post redirect: header() returns the Location");
	spin(50);
	ok &= expect(server.find("/post-target") == nullptr, "post redirect: the redirect is not followed");
	return ok;
}

static bool testPostErrorStatus(HttpTransport& transport, const TestServer& server) {
	Fetch fetch(transport, makePost(server.url("/missing"), smallBody()), true);
	bool ok = expect(fetch.waitFinished(), "post 404: finished() arrives");
	ok &= expect(fetch.failed && fetch.status == 404, "post 404: an HTTP error status is a failure with its status code");
	return ok;
}

// A cookie that an earlier response set is not sent with a post.
static bool testPostSendsNoCookie(HttpTransport& transport, const TestServer& server) {
	Fetch first(transport, makeRequest(server.url("/set-cookie")));
	bool ok = expect(first.waitFinished(), "cookie: the request that sets a cookie finishes");
	Fetch second(transport, makePost(server.url("/cookie-check"), smallBody()), true);
	ok &= expect(second.waitFinished(), "cookie: the post finishes");
	const RecordedRequest* recorded = server.find("/cookie-check");
	ok &= expect(recorded != nullptr, "cookie: the server saw the post");
	if (recorded)
		ok &= expect(recorded->cookie.isEmpty(), "cookie: the post sends no cookie");
	return ok;
}

static bool testLargeBody(HttpTransport& transport, const TestServer& server) {
	Fetch fetch(transport, makeRequest(server.url("/big")));
	const QByteArray body = bigBody();
	bool ok = expect(fetch.waitFinished(), "large body: finished() arrives");
	ok &= expect(!fetch.failed && fetch.status == 200, "large body: the request succeeds");
	ok &= expect(fetch.readyReads >= 1, "large body: readyRead is emitted");
	ok &= expect(fetch.lastReceived == body.size() && fetch.lastTotal == body.size(), "large body: downloadProgress reaches the size of the body");
	ok &= expect(fetch.data == body, "large body: all bytes arrive");
	return ok;
}

static bool testRedirect(HttpTransport& transport, const TestServer& server) {
	Fetch fetch(transport, makeRequest(server.url("/redirect")));
	bool ok = expect(fetch.waitFinished(), "redirect: finished() arrives");
	ok &= expect(!fetch.failed && fetch.status == 200, "redirect: the reply is the one of the target");
	ok &= expect(fetch.data == targetBody(), "redirect: the body is the one of the target");
	return ok;
}

static bool testRelativeRedirect(HttpTransport& transport, const TestServer& server) {
	Fetch fetch(transport, makeRequest(server.url("/redirect-relative")));
	bool ok = expect(fetch.waitFinished(), "relative redirect: finished() arrives");
	ok &= expect(!fetch.failed && fetch.status == 200, "relative redirect: the reply is the one of the target");
	ok &= expect(fetch.data == targetBody(), "relative redirect: the body is the one of the target");
	return ok;
}

// A server that redirects to its own address: the transport gives up after a limited number of redirects.
static bool testRedirectLoop(HttpTransport& transport, const TestServer& server) {
	Fetch fetch(transport, makeRequest(server.url("/redirect-loop")));
	bool ok = expect(fetch.waitFinished(), "redirect loop: finished() arrives");
	spin(50);
	ok &= expect(fetch.finishedCount == 1, "redirect loop: finished() is emitted once");
	ok &= expect(fetch.failed, "redirect loop: the reply has failed");
	ok &= expect(!fetch.error.isEmpty(), "redirect loop: errorString() is not empty");
	return ok;
}

static bool testErrorStatus(HttpTransport& transport, const TestServer& server) {
	Fetch fetch(transport, makeRequest(server.url("/missing")));
	bool ok = expect(fetch.waitFinished(), "404: finished() arrives");
	ok &= expect(fetch.failed, "404: an HTTP error status is a failure");
	ok &= expect(fetch.status == 404, "404: the status code is 404");
	ok &= expect(!fetch.error.isEmpty(), "404: errorString() is not empty");
	return ok;
}

static bool testAbortDuringResponse(HttpTransport& transport, const TestServer& server) {
	Fetch fetch(transport, makeRequest(server.url("/stall")));
	fetch.abortOnData = true;
	bool ok = expect(fetch.waitFinished(), "abort: finished() arrives");
	ok &= expect(fetch.abortCalled, "abort: the response had started when abort() was called");
	ok &= expect(fetch.failed, "abort: a cancelled request has failed");
	ok &= expect(!fetch.error.isEmpty(), "abort: errorString() is not empty");
	spin(200);
	ok &= expect(fetch.finishedCount == 1, "abort: finished() is emitted once");
	return ok;
}

static bool testConnectionDropped(HttpTransport& transport, const TestServer& server) {
	Fetch fetch(transport, makeRequest(server.url("/drop")));
	bool ok = expect(fetch.waitFinished(), "dropped: finished() arrives");
	ok &= expect(fetch.failed, "dropped: the reply has failed");
	ok &= expect(fetch.status == 0, "dropped: no status code arrives");
	ok &= expect(!fetch.error.isEmpty(), "dropped: errorString() is not empty");
	return ok;
}

// Nothing listens on the port: the server that had it is closed. Some Qt builds report the refused
// connection and some leave the request pending; a request that is still pending after a short wait is
// aborted, as the timeouts of UpdateChecker and SelfUpdater do. Either way the reply must end as failed.
static bool testConnectionRefused(HttpTransport& transport) {
	QUrl url;
	{
		QTcpServer closed;
		if (!expect(closed.listen(QHostAddress::LocalHost, 0), "refused: a free port is found"))
			return false;
		url = QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(closed.serverPort()));
	}
	Fetch fetch(transport, makeRequest(url));
	bool ok = true;
	if (!fetch.waitFinished(1000)) {
		ok &= expect(fetch.reply != nullptr, "refused: the reply exists while it is pending");
		if (fetch.reply)
			fetch.reply->abort();
		ok &= expect(fetch.waitFinished(), "refused: finished() arrives once a pending request is aborted");
	}
	spin(50);
	ok &= expect(fetch.finishedCount == 1, "refused: finished() is emitted once");
	ok &= expect(fetch.failed, "refused: the reply has failed");
	ok &= expect(fetch.status == 0, "refused: no status code arrives");
	ok &= expect(!fetch.error.isEmpty(), "refused: errorString() is not empty");
	return ok;
}

static bool testReleaseAfterFinished(HttpTransport& transport, const TestServer& server) {
	QPointer<HttpReply> released;
	{
		Fetch fetch(transport, makeRequest(server.url("/small")));
		if (!expect(fetch.waitFinished(), "release: the first request finishes"))
			return false;
		released = fetch.reply;
		fetch.reply->deleteLater();
		spin(50);
		if (!expect(released.isNull(), "release: the reply is deleted after deleteLater()"))
			return false;
	}
	Fetch next(transport, makeRequest(server.url("/small")));
	bool ok = expect(next.waitFinished(), "release: a further request finishes");
	ok &= expect(!next.failed && next.data == smallBody(), "release: a further request succeeds");
	return ok;
}

// Deleting the transport with a request in flight deletes the reply, and no signal arrives afterwards.
static bool testTeardownDuringRequest(const TestServer& server) {
	HttpTransport* transport = createHttpTransport(nullptr, true);
	QPointer<HttpReply> reply = transport->get(makeRequest(server.url("/stall"), "EGTRAIN/test"));
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

	bool ok = expect(dataArrived, "teardown: data arrives before the transport is deleted");
	const int signalsBefore = signalCount;
	delete transport;
	ok &= expect(reply.isNull(), "teardown: the reply is deleted with the transport");
	spin(200);
	ok &= expect(signalCount == signalsBefore, "teardown: no signal arrives after the transport is deleted");
	return ok;
}

#ifdef Q_OS_WIN
// The transport of the application fetches https addresses only, so a request for plain http fails
// without a connection to the server.
static bool testPlainHttpRefused(HttpTransport& strict, const TestServer& server) {
	bool ok = true;
	{
		Fetch fetch(strict, makeRequest(server.url("/plain-http")));
		ok &= expect(fetch.waitFinished(), "plain http: finished() arrives");
		ok &= expect(fetch.failed, "plain http: the reply has failed");
		ok &= expect(fetch.status == 0, "plain http: no status code arrives");
		ok &= expect(!fetch.error.isEmpty(), "plain http: errorString() is not empty");
	}
	spin(50);
	ok &= expect(server.find("/plain-http") == nullptr, "plain http: no request reaches the server");
	return ok;
}

// With plain http to the loopback address allowed, a redirect to another plain http address is still
// refused. The redirect names this server by host name, which is not the loopback address the test
// allows, so a transport that followed it would send the request to this server.
static bool testRedirectToPlainHttpRefused(HttpTransport& transport, const TestServer& server) {
	Fetch fetch(transport, makeRequest(server.url("/redirect-away")));
	bool ok = expect(fetch.waitFinished(), "redirect to http: finished() arrives");
	ok &= expect(fetch.failed, "redirect to http: the reply has failed");
	ok &= expect(!fetch.error.isEmpty(), "redirect to http: errorString() is not empty");
	spin(50);
	ok &= expect(server.find("/redirect-away-target") == nullptr, "redirect to http: the redirect is not followed");
	return ok;
}
#endif

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	TestServer server;
	if (!expect(server.listening, "the test server listens on the loopback address"))
		return 1;
	// The server speaks plain http, which the transport of the application refuses on Windows unless a test allows it.
	HttpTransport* transport = createHttpTransport(&application, true);

	bool ok = true;
	ok &= testSmallBody(*transport, server);
	ok &= testRequestHeaders(*transport, server);
	ok &= testPost(*transport, server);
	ok &= testPostRedirectNotFollowed(*transport, server);
	ok &= testPostErrorStatus(*transport, server);
	ok &= testPostSendsNoCookie(*transport, server);
	ok &= testLargeBody(*transport, server);
	ok &= testRedirect(*transport, server);
	ok &= testRelativeRedirect(*transport, server);
	ok &= testRedirectLoop(*transport, server);
	ok &= testErrorStatus(*transport, server);
	ok &= testAbortDuringResponse(*transport, server);
	ok &= testConnectionDropped(*transport, server);
	ok &= testConnectionRefused(*transport);
	ok &= testReleaseAfterFinished(*transport, server);
	ok &= testTeardownDuringRequest(server);
#ifdef Q_OS_WIN
	HttpTransport* strict = createHttpTransport(&application);
	ok &= testPlainHttpRefused(*strict, server);
	ok &= testRedirectToPlainHttpRefused(*transport, server);
#endif
	return ok ? 0 : 1;
}
