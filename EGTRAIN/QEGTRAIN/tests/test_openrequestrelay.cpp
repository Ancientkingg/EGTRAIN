#include "app/OpenRequestRelay.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileOpenEvent>
#include <QIODevice>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <cstdlib>
#include <functional>
#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

// The names carry the process number, so that two runs of this test do not meet.
static QString serverName(const char* tag) {
	return QStringLiteral("egtrain-open-test-%1-%2").arg(QCoreApplication::applicationPid()).arg(QLatin1String(tag));
}

// Processes events until done() holds or the time is over.
static bool waitFor(const std::function<bool()>& done, int timeoutMs) {
	QElapsedTimer timer;
	timer.start();
	while (!done() && timer.elapsed() < timeoutMs) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QThread::msleep(5);
	}
	return done();
}

// Lets the relay run for a while, for a check that nothing arrives.
static void settle(int milliseconds) {
	waitFor([] { return false; }, milliseconds);
}

// The path of a request goes to the child as hexadecimal text of its UTF-8 bytes, so that the
// locale of the test machine cannot change it on the way.
static QString encodeArgument(const QString& text) {
	return QString::fromLatin1(text.toUtf8().toHex());
}

static QString decodeArgument(const QString& text) {
	return QString::fromUtf8(QByteArray::fromHex(text.toLatin1()));
}

// The test starts copies of itself as the second process. The result is the exit code of the
// child, or -1 when it did not end normally in time. The relay of the caller keeps running
// meanwhile.
static int runChild(const QStringList& arguments) {
	QProcess child;
	child.setProcessChannelMode(QProcess::ForwardedErrorChannel);
	child.start(QCoreApplication::applicationFilePath(), arguments);
	if (!child.waitForStarted(60000))
		return -1;
	waitFor([&child] { return child.state() == QProcess::NotRunning; }, 60000);
	if (child.state() != QProcess::NotRunning) {
		child.kill();
		child.waitForFinished(10000);
		return -1;
	}
	return child.exitStatus() == QProcess::NormalExit ? child.exitCode() : -1;
}

static int forwardFromChild(const QString& name, const QString& applicationDir, const QString& path) {
	return runChild({QStringLiteral("--child-forward"), name, applicationDir, encodeArgument(path)});
}

// Collects what a relay reports.
class Received {
public:
	explicit Received(OpenRequestRelay& relay) {
		QObject::connect(&relay, &OpenRequestRelay::openRequested, &relay, [this](const QString& path) { paths.append(path); });
	}
	QStringList paths;
};

static const QString kDir = QStringLiteral("application-folder-a");

// The child end of a request: exit code 0 when the running instance took it, 3 when not.
static int runChildForward(const QStringList& arguments) {
	if (arguments.size() != 5)
		return 2;
	return OpenRequestRelay::forward(arguments.at(2), arguments.at(3), decodeArgument(arguments.at(4))) ? 0 : 3;
}

// Takes the name and ends without closing the server, as a killed instance does. A socket file
// stays behind on macOS and Linux.
static int runChildStale(const QStringList& arguments) {
	if (arguments.size() != 4)
		return 2;
	OpenRequestRelay relay;
	if (!relay.listen(arguments.at(2), arguments.at(3)))
		return 3;
	std::_Exit(0);
}

static bool testForwardFromSecondProcess() {
	bool ok = true;
	const QString name = serverName("forward");
	OpenRequestRelay relay;
	Received received(relay);
	ok &= expect(relay.listen(name, kDir), "the first instance listens");

	// A folder name with spaces and a file name with characters outside the Western Windows code page.
	const QString path = QStringLiteral("/data/folder with spaces/Paimpol ") + QString::fromUtf16(u"é東京")
		+ QStringLiteral(".egscene");
	ok &= expect(forwardFromChild(name, kDir, path) == 0, "the second process ends with 0 once the path is accepted");
	ok &= expect(waitFor([&received] { return !received.paths.isEmpty(); }, 5000), "the first instance receives the path");
	settle(300);
	ok &= expect(received.paths == QStringList{path}, "the path arrives once and unchanged");

	ok &= expect(forwardFromChild(name, kDir, QStringLiteral("/data/second.egscene")) == 0, "a second request is accepted too");
	ok &= expect(waitFor([&received] { return received.paths.size() == 2; }, 5000), "both requests arrive");
	ok &= expect(received.paths.size() == 2 && received.paths.at(1) == QStringLiteral("/data/second.egscene"), "in the order sent");

	// Each sender must read its answer, however soon after the previous request it comes.
	for (int i = 0; i < 8; ++i)
		ok &= expect(forwardFromChild(name, kDir, QStringLiteral("/data/next-%1.egscene").arg(i)) == 0, "every further request is accepted");
	ok &= expect(waitFor([&received] { return received.paths.size() == 10; }, 5000), "and arrives");
	return ok;
}

// The receiver may open a dialog, whose event loop runs while the sender reads the answer and disconnects, which
// deletes the connection. The relay must not use the connection when the dialog closes.
static bool testReceiverWithDialog() {
	const QString name = serverName("dialog");
	OpenRequestRelay relay;
	bool ok = expect(relay.listen(name, kDir), "the instance listens");
	QStringList paths;
	QObject::connect(&relay, &OpenRequestRelay::openRequested, &relay, [&paths](const QString& path) {
		QEventLoop dialog;
		QTimer::singleShot(1500, &dialog, &QEventLoop::quit);
		dialog.exec();
		paths.append(path);
	});
	ok &= expect(forwardFromChild(name, kDir, QStringLiteral("/data/dialog.egscene")) == 0, "the sender gets its answer");
	ok &= expect(waitFor([&paths] { return !paths.isEmpty(); }, 5000), "the receiver gets the path after its dialog");
	settle(300);
	ok &= expect(paths == QStringList{QStringLiteral("/data/dialog.egscene")}, "once");
	return ok;
}

static bool testNoRunningInstance() {
	bool ok = true;
	const QString name = serverName("none");
	ok &= expect(forwardFromChild(name, kDir, QStringLiteral("/data/case.egscene")) == 3,
		"the second process finds no instance and does not end with 0");
	ok &= expect(!OpenRequestRelay::forward(name, kDir, QStringLiteral("/data/case.egscene")), "forward answers false without an instance");
	return ok;
}

static bool testOtherApplicationDir() {
	bool ok = true;
	const QString name = serverName("other");
	OpenRequestRelay relay;
	Received received(relay);
	ok &= expect(relay.listen(name, kDir), "the first instance listens");
	ok &= expect(forwardFromChild(name, QStringLiteral("application-folder-b"), QStringLiteral("/data/case.egscene")) == 3,
		"an instance of another application folder refuses the request");
	settle(300);
	ok &= expect(received.paths.isEmpty(), "and does not report it");
	ok &= expect(forwardFromChild(name, kDir, QStringLiteral("/data/case.egscene")) == 0 && waitFor([&received] { return !received.paths.isEmpty(); }, 5000),
		"a request of its own folder still arrives afterwards");
	return ok;
}

static bool testSecondServerLeavesRunningOne() {
	bool ok = true;
	const QString name = serverName("twice");
	OpenRequestRelay first;
	Received received(first);
	ok &= expect(first.listen(name, kDir), "the first instance listens");
	OpenRequestRelay second;
	ok &= expect(!second.listen(name, kDir), "a second instance does not take a name that a running one holds");
	ok &= expect(forwardFromChild(name, kDir, QStringLiteral("/data/case.egscene")) == 0 && waitFor([&received] { return !received.paths.isEmpty(); }, 5000),
		"the running instance still receives requests");
	return ok;
}

static bool testLeftBehindServerIsTakenOver() {
	bool ok = true;
	const QString name = serverName("stale");
	ok &= expect(runChild({QStringLiteral("--child-stale"), name, kDir}) == 0, "the child takes the name and ends without closing it");
	OpenRequestRelay relay;
	Received received(relay);
	ok &= expect(relay.listen(name, kDir), "an instance takes over a name that a killed one left behind");
	ok &= expect(forwardFromChild(name, kDir, QStringLiteral("/data/case.egscene")) == 0 && waitFor([&received] { return !received.paths.isEmpty(); }, 5000),
		"and receives requests");
	return ok;
}

static QByteArray requestBytes(const QString& applicationDir, const QString& path) {
	QByteArray request;
	QDataStream out(&request, QIODevice::WriteOnly);
	out.setVersion(QDataStream::Qt_5_9);
	out << applicationDir << path;
	return request;
}

// Connects without blocking, so that the relay of this process can answer.
static bool connectRaw(QLocalSocket& socket, const QString& name) {
	socket.connectToServer(name);
	return waitFor([&socket] { return socket.state() == QLocalSocket::ConnectedState; }, 5000);
}

// True when the relay has ended the connection of a client that wrote something. A named pipe may
// report the end differently, so Windows does not wait for it.
#ifdef Q_OS_WIN
static bool waitForDrop(QLocalSocket&) {
	settle(200);
	return true;
}
#else
static bool waitForDrop(QLocalSocket& socket) {
	return waitFor([&socket] { return socket.state() != QLocalSocket::ConnectedState; }, 5000);
}
#endif

static bool testPartialAndMalformedRequests() {
	bool ok = true;
	const QString name = serverName("raw");
	OpenRequestRelay relay;
	Received received(relay);
	ok &= expect(relay.listen(name, kDir), "the instance listens");

	{
		// A request that arrives in two parts is accepted when the second part is there.
		QLocalSocket socket;
		ok &= expect(connectRaw(socket, name), "a client connects");
		const QByteArray request = requestBytes(kDir, QStringLiteral("/data/split.egscene"));
		socket.write(request.left(5));
		socket.flush();
		settle(200);
		ok &= expect(received.paths.isEmpty(), "half a request is not reported");
		socket.write(request.mid(5));
		socket.flush();
		ok &= expect(waitFor([&received] { return !received.paths.isEmpty(); }, 5000), "the whole request is reported");
		ok &= expect(waitFor([&socket] { return socket.bytesAvailable() > 0; }, 5000) && socket.readAll() == "1", "and answered with the sign of acceptance");
	}
	{
		// Bytes that are no request are dropped; the instance goes on.
		QLocalSocket socket;
		ok &= expect(connectRaw(socket, name), "another client connects");
		socket.write("junk");
		socket.flush();
		ok &= expect(waitForDrop(socket), "bytes that are no request end the connection");
	}
	{
		// So are more bytes than a request can hold, even when they start like a request.
		QLocalSocket socket;
		ok &= expect(connectRaw(socket, name), "a client that sends too much connects");
		QByteArray tooMuch;
		{
			QDataStream out(&tooMuch, QIODevice::WriteOnly);
			out.setVersion(QDataStream::Qt_5_9);
			out << quint32(1 << 20);
		}
		tooMuch.append(QByteArray(70000, 'a'));
		socket.write(tooMuch);
		ok &= expect(waitForDrop(socket), "more bytes than a request holds end the connection");
	}
	{
		// A client that connects and leaves, like the check of a second instance for a live one.
		QLocalSocket socket;
		ok &= expect(connectRaw(socket, name), "a client connects and leaves");
		socket.disconnectFromServer();
	}
	settle(200);
	ok &= expect(received.paths.size() == 1, "none of these reports a request");
	ok &= expect(forwardFromChild(name, kDir, QStringLiteral("/data/after.egscene")) == 0 && waitFor([&received] { return received.paths.size() == 2; }, 5000),
		"the instance still receives requests afterwards");
	return ok;
}

static bool testPostpone() {
	bool ok = true;
	{
		OpenRequestRelay relay;
		Received received(relay);
		ok &= expect(!relay.hasPendingRequest(), "nothing waits at first");
		relay.postpone(QStringLiteral("/data/old.egscene"));
		relay.postpone(QStringLiteral("/data/new.egscene"));
		ok &= expect(received.paths.isEmpty() && relay.hasPendingRequest(), "a postponed path does not come back at once and is pending");
		ok &= expect(waitFor([&received] { return !received.paths.isEmpty(); }, 5000), "a postponed path comes back");
		settle(500);
		ok &= expect(received.paths == QStringList{QStringLiteral("/data/new.egscene")}, "only the newest postponed path comes back, once");
		ok &= expect(!relay.hasPendingRequest(), "and is no longer pending");
	}
	{
		const QString name = serverName("postpone");
		OpenRequestRelay relay;
		Received received(relay);
		ok &= expect(relay.listen(name, kDir), "the instance listens");
		// The client is connected before the path waits, and writes at once, so that the request arrives
		// well before the waiting path is due.
		QLocalSocket socket;
		ok &= expect(connectRaw(socket, name), "a client connects");
		relay.postpone(QStringLiteral("/data/old.egscene"));
		socket.write(requestBytes(kDir, QStringLiteral("/data/new.egscene")));
		socket.flush();
		ok &= expect(waitFor([&received] { return !received.paths.isEmpty(); }, 5000), "a request arrives while one waits");
		settle(800);
		ok &= expect(received.paths == QStringList{QStringLiteral("/data/new.egscene")}, "the new request replaces the one that waited");
	}
	return ok;
}

// The system opens a file with the process itself, as Finder does: the application gets a
// QFileOpenEvent. The result is true when a relay took the event.
static bool sendFileOpen(const QUrl& url) {
	QFileOpenEvent event(url);
	return QCoreApplication::sendEvent(QCoreApplication::instance(), &event);
}

// The path that the receiver gets for a file that the event names.
static QString pathOfEvent(const QString& path) {
	return QUrl::fromLocalFile(path).toLocalFile();
}

static bool testFileOpenEvent() {
	bool ok = true;
	OpenRequestRelay relay;
	Received received(relay);

	// A folder name with spaces and a file name with characters outside the Western Windows code page.
	const QString path = QStringLiteral("/data/folder with spaces/Paimpol ") + QString::fromUtf16(u"é東京")
		+ QStringLiteral(".egscene");
	ok &= expect(sendFileOpen(QUrl::fromLocalFile(path)), "the relay takes an open event");
	ok &= expect(received.paths.isEmpty() && relay.hasPendingRequest(), "the request waits for the event loop, not the delivery of the event");
	ok &= expect(waitFor([&received] { return !received.paths.isEmpty(); }, 5000), "the request is reported");
	settle(300);
	ok &= expect(received.paths == QStringList{pathOfEvent(path)}, "once and unchanged");
	ok &= expect(!relay.hasPendingRequest(), "and nothing waits afterwards");

	// An event for a URL that names no local file is not for the relay.
	ok &= expect(!sendFileOpen(QUrl(QStringLiteral("https://example.com/case.egscene"))), "an open event for a remote URL is left alone");
	settle(300);
	ok &= expect(received.paths.size() == 1 && !relay.hasPendingRequest(), "and is not reported");

	// Events of one turn of the event loop leave the newest.
	ok &= expect(sendFileOpen(QUrl::fromLocalFile(QStringLiteral("/data/old.egscene"))) && sendFileOpen(QUrl::fromLocalFile(QStringLiteral("/data/new.egscene"))),
		"two open events are taken");
	ok &= expect(waitFor([&received] { return received.paths.size() == 2; }, 5000), "a request is reported");
	settle(300);
	ok &= expect(received.paths.size() == 2 && received.paths.at(1) == pathOfEvent(QStringLiteral("/data/new.egscene")), "the newest of them");
	return ok;
}

static bool testFileOpenEventWithoutReceiver() {
	bool ok = true;
	OpenRequestRelay relay;
	const QString path = QStringLiteral("/data/early.egscene");
	ok &= expect(sendFileOpen(QUrl::fromLocalFile(path)), "the relay takes an open event that nothing waits for");
	// Longer than the time between two attempts to hand the request over.
	settle(700);
	ok &= expect(relay.hasPendingRequest(), "the request stays while nothing is connected");
	Received received(relay);
	ok &= expect(waitFor([&received] { return !received.paths.isEmpty(); }, 5000), "and is reported once something is connected");
	settle(300);
	ok &= expect(received.paths == QStringList{pathOfEvent(path)} && !relay.hasPendingRequest(), "once");
	return ok;
}

static bool testEnvironmentGuard() {
	bool ok = true;
	// The guard reads the whole environment, so the test starts from none of the test variables.
	const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	QStringList names;
	for (const QString& key : environment.keys())
		if (key.startsWith(QStringLiteral("QEGTRAIN_E2E_")))
			names.append(key);
	for (const QString& key : names)
		qunsetenv(key.toLocal8Bit().constData());

	ok &= expect(OpenRequestRelay::allowedByEnvironment(), "a launch without a test variable may use the relay");
	qputenv("QEGTRAIN_E2E_SETTINGS_DIR", "settings");
	ok &= expect(!OpenRequestRelay::allowedByEnvironment(), "a launch with a test variable may not");
	qputenv("QEGTRAIN_E2E_OPEN_RELAY", "1");
	ok &= expect(OpenRequestRelay::allowedByEnvironment(), "QEGTRAIN_E2E_OPEN_RELAY turns the relay on again");
	qunsetenv("QEGTRAIN_E2E_SETTINGS_DIR");
	ok &= expect(OpenRequestRelay::allowedByEnvironment(), "also when it is the only test variable");
	qunsetenv("QEGTRAIN_E2E_OPEN_RELAY");

	for (const QString& key : names)
		qputenv(key.toLocal8Bit().constData(), environment.value(key).toLocal8Bit());
	return ok;
}

static bool testDefaultNames() {
	bool ok = true;
	const QString name = OpenRequestRelay::defaultServerName();
	ok &= expect(name.startsWith(QStringLiteral("egtrain-open-")) && name.size() <= 45, "the default server name is short");
	ok &= expect(!name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\')), "and has no path separator");
	ok &= expect(OpenRequestRelay::currentApplicationDir() == QDir(QCoreApplication::applicationDirPath()).canonicalPath(),
		"the application folder is the canonical folder of the program");
	return ok;
}

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	const QStringList arguments = QCoreApplication::arguments();
	if (arguments.size() > 1 && arguments.at(1) == QLatin1String("--child-forward"))
		return runChildForward(arguments);
	if (arguments.size() > 1 && arguments.at(1) == QLatin1String("--child-stale"))
		return runChildStale(arguments);
	if (arguments.size() != 1) {
		std::cerr << "usage: test_openrequestrelay\n";
		return 2;
	}

#ifdef Q_OS_UNIX
	// The socket file lies in the temp folder, and a socket path is limited to about 100 characters.
	// A folder directly below /tmp keeps it within the limit, however long the temp folder of the
	// caller is. The children inherit the setting.
	QTemporaryDir socketDir(QStringLiteral("/tmp/egtrelay-XXXXXX"));
	if (!socketDir.isValid()) {
		std::cerr << "failed: no folder for the socket files\n";
		return 1;
	}
	qputenv("TMPDIR", socketDir.path().toLocal8Bit());
#endif

	bool ok = true;
	ok &= testForwardFromSecondProcess();
	ok &= testReceiverWithDialog();
	ok &= testNoRunningInstance();
	ok &= testOtherApplicationDir();
	ok &= testSecondServerLeavesRunningOne();
	ok &= testLeftBehindServerIsTakenOver();
	ok &= testPartialAndMalformedRequests();
	ok &= testPostpone();
	ok &= testFileOpenEvent();
	ok &= testFileOpenEventWithoutReceiver();
	ok &= testEnvironmentGuard();
	ok &= testDefaultNames();
	return ok ? 0 : 1;
}
