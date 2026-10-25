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
#include <QScopeGuard>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <cstdlib>
#include <functional>
#include <iostream>

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

// A second process hands the path that it was asked to open to the running instance, which reports it once. Malformed
// requests, other application folders and left-behind servers do not stop it. The test starts copies of itself as the
// second process.
class TestOpenRequestRelay : public QObject {
	Q_OBJECT

private slots:
	void forwardFromSecondProcess();
	void receiverWithDialog();
	void noRunningInstance();
	void otherApplicationDir();
	void secondServerLeavesRunningOne();
	void leftBehindServerIsTakenOver();
	void partialAndMalformedRequests();
	void postponedPathComesBack();
	void requestReplacesPostponedPath();
	void fileOpenEvent();
	void fileOpenEventWithoutReceiver();
	void environmentGuard();
	void defaultNames();
};

void TestOpenRequestRelay::forwardFromSecondProcess() {
	const QString name = serverName("forward");
	OpenRequestRelay relay;
	Received received(relay);
	QVERIFY2(relay.listen(name, kDir), "the first instance listens");

	// A folder name with spaces and a file name with characters outside the Western Windows code page.
	const QString path = QStringLiteral("/data/folder with spaces/Paimpol ") + QString::fromUtf16(u"é東京")
		+ QStringLiteral(".egscene");
	// The second process ends with 0 once the path is accepted.
	QCOMPARE(forwardFromChild(name, kDir, path), 0);
	QVERIFY2(waitFor([&received] { return !received.paths.isEmpty(); }, 5000), "the first instance receives the path");
	settle(300);
	// The path arrives once and unchanged.
	QCOMPARE(received.paths, QStringList{path});

	// A second request is accepted too.
	QCOMPARE(forwardFromChild(name, kDir, QStringLiteral("/data/second.egscene")), 0);
	QVERIFY2(waitFor([&received] { return received.paths.size() == 2; }, 5000), "both requests arrive");
	// In the order sent.
	QCOMPARE(received.paths.size(), 2);
	QCOMPARE(received.paths.at(1), QStringLiteral("/data/second.egscene"));

	// Each sender must read its answer, however soon after the previous request it comes.
	for (int i = 0; i < 8; ++i) {
		// Every further request is accepted.
		QCOMPARE(forwardFromChild(name, kDir, QStringLiteral("/data/next-%1.egscene").arg(i)), 0);
	}
	QVERIFY2(waitFor([&received] { return received.paths.size() == 10; }, 5000), "and arrives");
}

// The receiver may open a dialog, whose event loop runs while the sender reads the answer and disconnects, which
// deletes the connection. The relay must not use the connection when the dialog closes.
void TestOpenRequestRelay::receiverWithDialog() {
	const QString name = serverName("dialog");
	OpenRequestRelay relay;
	QVERIFY2(relay.listen(name, kDir), "the instance listens");
	QStringList paths;
	QObject::connect(&relay, &OpenRequestRelay::openRequested, &relay, [&paths](const QString& path) {
		QEventLoop dialog;
		QTimer::singleShot(1500, &dialog, &QEventLoop::quit);
		dialog.exec();
		paths.append(path);
	});
	// The sender gets its answer.
	QCOMPARE(forwardFromChild(name, kDir, QStringLiteral("/data/dialog.egscene")), 0);
	QVERIFY2(waitFor([&paths] { return !paths.isEmpty(); }, 5000), "the receiver gets the path after its dialog");
	settle(300);
	// Once.
	QCOMPARE(paths, QStringList{QStringLiteral("/data/dialog.egscene")});
}

void TestOpenRequestRelay::noRunningInstance() {
	const QString name = serverName("none");
	// The second process finds no instance and does not end with 0.
	QCOMPARE(forwardFromChild(name, kDir, QStringLiteral("/data/case.egscene")), 3);
	QVERIFY2(!OpenRequestRelay::forward(name, kDir, QStringLiteral("/data/case.egscene")), "forward answers false without an instance");
}

void TestOpenRequestRelay::otherApplicationDir() {
	const QString name = serverName("other");
	OpenRequestRelay relay;
	Received received(relay);
	QVERIFY2(relay.listen(name, kDir), "the first instance listens");
	// An instance of another application folder refuses the request.
	QCOMPARE(forwardFromChild(name, QStringLiteral("application-folder-b"), QStringLiteral("/data/case.egscene")), 3);
	settle(300);
	QVERIFY2(received.paths.isEmpty(), "and does not report it");
	// A request of its own folder still arrives afterwards.
	QCOMPARE(forwardFromChild(name, kDir, QStringLiteral("/data/case.egscene")), 0);
	QVERIFY(waitFor([&received] { return !received.paths.isEmpty(); }, 5000));
}

void TestOpenRequestRelay::secondServerLeavesRunningOne() {
	const QString name = serverName("twice");
	OpenRequestRelay first;
	Received received(first);
	QVERIFY2(first.listen(name, kDir), "the first instance listens");
	OpenRequestRelay second;
	QVERIFY2(!second.listen(name, kDir), "a second instance does not take a name that a running one holds");
	// The running instance still receives requests.
	QCOMPARE(forwardFromChild(name, kDir, QStringLiteral("/data/case.egscene")), 0);
	QVERIFY(waitFor([&received] { return !received.paths.isEmpty(); }, 5000));
}

void TestOpenRequestRelay::leftBehindServerIsTakenOver() {
	const QString name = serverName("stale");
	// The child takes the name and ends without closing it.
	QCOMPARE(runChild({QStringLiteral("--child-stale"), name, kDir}), 0);
	OpenRequestRelay relay;
	Received received(relay);
	QVERIFY2(relay.listen(name, kDir), "an instance takes over a name that a killed one left behind");
	// And receives requests.
	QCOMPARE(forwardFromChild(name, kDir, QStringLiteral("/data/case.egscene")), 0);
	QVERIFY(waitFor([&received] { return !received.paths.isEmpty(); }, 5000));
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

void TestOpenRequestRelay::partialAndMalformedRequests() {
	const QString name = serverName("raw");
	OpenRequestRelay relay;
	Received received(relay);
	QVERIFY2(relay.listen(name, kDir), "the instance listens");

	{
		// A request that arrives in two parts is accepted when the second part is there.
		QLocalSocket socket;
		QVERIFY2(connectRaw(socket, name), "a client connects");
		const QByteArray request = requestBytes(kDir, QStringLiteral("/data/split.egscene"));
		socket.write(request.left(5));
		socket.flush();
		settle(200);
		QVERIFY2(received.paths.isEmpty(), "half a request is not reported");
		socket.write(request.mid(5));
		socket.flush();
		QVERIFY2(waitFor([&received] { return !received.paths.isEmpty(); }, 5000), "the whole request is reported");
		// And answered with the sign of acceptance.
		QVERIFY(waitFor([&socket] { return socket.bytesAvailable() > 0; }, 5000));
		QCOMPARE(socket.readAll(), QByteArray("1"));
	}
	{
		// Bytes that are no request are dropped; the instance goes on.
		QLocalSocket socket;
		QVERIFY2(connectRaw(socket, name), "another client connects");
		socket.write("junk");
		socket.flush();
		QVERIFY2(waitForDrop(socket), "bytes that are no request end the connection");
	}
	{
		// So are more bytes than a request can hold, even when they start like a request.
		QLocalSocket socket;
		QVERIFY2(connectRaw(socket, name), "a client that sends too much connects");
		QByteArray tooMuch;
		{
			QDataStream out(&tooMuch, QIODevice::WriteOnly);
			out.setVersion(QDataStream::Qt_5_9);
			out << quint32(1 << 20);
		}
		tooMuch.append(QByteArray(70000, 'a'));
		socket.write(tooMuch);
		QVERIFY2(waitForDrop(socket), "more bytes than a request holds end the connection");
	}
	{
		// A client that connects and leaves, like the check of a second instance for a live one.
		QLocalSocket socket;
		QVERIFY2(connectRaw(socket, name), "a client connects and leaves");
		socket.disconnectFromServer();
	}
	settle(200);
	// None of these reports a request.
	QCOMPARE(received.paths.size(), 1);
	// The instance still receives requests afterwards.
	QCOMPARE(forwardFromChild(name, kDir, QStringLiteral("/data/after.egscene")), 0);
	QVERIFY(waitFor([&received] { return received.paths.size() == 2; }, 5000));
}

void TestOpenRequestRelay::postponedPathComesBack() {
	OpenRequestRelay relay;
	Received received(relay);
	QVERIFY2(!relay.hasPendingRequest(), "nothing waits at first");
	relay.postpone(QStringLiteral("/data/old.egscene"));
	relay.postpone(QStringLiteral("/data/new.egscene"));
	// A postponed path does not come back at once and is pending.
	QVERIFY(received.paths.isEmpty());
	QVERIFY(relay.hasPendingRequest());
	QVERIFY2(waitFor([&received] { return !received.paths.isEmpty(); }, 5000), "a postponed path comes back");
	settle(500);
	// Only the newest postponed path comes back, once.
	QCOMPARE(received.paths, QStringList{QStringLiteral("/data/new.egscene")});
	QVERIFY2(!relay.hasPendingRequest(), "and is no longer pending");
}

void TestOpenRequestRelay::requestReplacesPostponedPath() {
	const QString name = serverName("postpone");
	OpenRequestRelay relay;
	Received received(relay);
	QVERIFY2(relay.listen(name, kDir), "the instance listens");
	// The client is connected before the path waits, and writes at once, so that the request arrives
	// well before the waiting path is due.
	QLocalSocket socket;
	QVERIFY2(connectRaw(socket, name), "a client connects");
	relay.postpone(QStringLiteral("/data/old.egscene"));
	socket.write(requestBytes(kDir, QStringLiteral("/data/new.egscene")));
	socket.flush();
	QVERIFY2(waitFor([&received] { return !received.paths.isEmpty(); }, 5000), "a request arrives while one waits");
	settle(800);
	// The new request replaces the one that waited.
	QCOMPARE(received.paths, QStringList{QStringLiteral("/data/new.egscene")});
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

void TestOpenRequestRelay::fileOpenEvent() {
	OpenRequestRelay relay;
	Received received(relay);

	// A folder name with spaces and a file name with characters outside the Western Windows code page.
	const QString path = QStringLiteral("/data/folder with spaces/Paimpol ") + QString::fromUtf16(u"é東京")
		+ QStringLiteral(".egscene");
	QVERIFY2(sendFileOpen(QUrl::fromLocalFile(path)), "the relay takes an open event");
	// The request waits for the event loop, not the delivery of the event.
	QVERIFY(received.paths.isEmpty());
	QVERIFY(relay.hasPendingRequest());
	QVERIFY2(waitFor([&received] { return !received.paths.isEmpty(); }, 5000), "the request is reported");
	settle(300);
	// Once and unchanged.
	QCOMPARE(received.paths, QStringList{pathOfEvent(path)});
	QVERIFY2(!relay.hasPendingRequest(), "and nothing waits afterwards");

	// An event for a URL that names no local file is not for the relay.
	QVERIFY2(!sendFileOpen(QUrl(QStringLiteral("https://example.com/case.egscene"))), "an open event for a remote URL is left alone");
	settle(300);
	// And is not reported.
	QCOMPARE(received.paths.size(), 1);
	QVERIFY(!relay.hasPendingRequest());

	// Events of one turn of the event loop leave the newest.
	// Two open events are taken.
	QVERIFY(sendFileOpen(QUrl::fromLocalFile(QStringLiteral("/data/old.egscene"))));
	QVERIFY(sendFileOpen(QUrl::fromLocalFile(QStringLiteral("/data/new.egscene"))));
	QVERIFY2(waitFor([&received] { return received.paths.size() == 2; }, 5000), "a request is reported");
	settle(300);
	// The newest of them.
	QCOMPARE(received.paths.size(), 2);
	QCOMPARE(received.paths.at(1), pathOfEvent(QStringLiteral("/data/new.egscene")));
}

void TestOpenRequestRelay::fileOpenEventWithoutReceiver() {
	OpenRequestRelay relay;
	const QString path = QStringLiteral("/data/early.egscene");
	QVERIFY2(sendFileOpen(QUrl::fromLocalFile(path)), "the relay takes an open event that nothing waits for");
	// Longer than the time between two attempts to hand the request over.
	settle(700);
	QVERIFY2(relay.hasPendingRequest(), "the request stays while nothing is connected");
	Received received(relay);
	QVERIFY2(waitFor([&received] { return !received.paths.isEmpty(); }, 5000), "and is reported once something is connected");
	settle(300);
	// Once.
	QCOMPARE(received.paths, QStringList{pathOfEvent(path)});
	QVERIFY(!relay.hasPendingRequest());
}

void TestOpenRequestRelay::environmentGuard() {
	// The guard reads the whole environment, so the test starts from none of the test variables.
	const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	QStringList names;
	for (const QString& key : environment.keys())
		if (key.startsWith(QStringLiteral("QEGTRAIN_E2E_")))
			names.append(key);
	for (const QString& key : names)
		qunsetenv(key.toLocal8Bit().constData());
	const auto restore = qScopeGuard([&environment, &names] {
		qunsetenv("QEGTRAIN_E2E_SETTINGS_DIR");
		qunsetenv("QEGTRAIN_E2E_OPEN_RELAY");
		for (const QString& key : names)
			qputenv(key.toLocal8Bit().constData(), environment.value(key).toLocal8Bit());
	});

	QVERIFY2(OpenRequestRelay::allowedByEnvironment(), "a launch without a test variable may use the relay");
	qputenv("QEGTRAIN_E2E_SETTINGS_DIR", "settings");
	QVERIFY2(!OpenRequestRelay::allowedByEnvironment(), "a launch with a test variable may not");
	qputenv("QEGTRAIN_E2E_OPEN_RELAY", "1");
	QVERIFY2(OpenRequestRelay::allowedByEnvironment(), "QEGTRAIN_E2E_OPEN_RELAY turns the relay on again");
	qunsetenv("QEGTRAIN_E2E_SETTINGS_DIR");
	QVERIFY2(OpenRequestRelay::allowedByEnvironment(), "also when it is the only test variable");
}

void TestOpenRequestRelay::defaultNames() {
	const QString name = OpenRequestRelay::defaultServerName();
	// The default server name is short.
	QVERIFY(name.startsWith(QStringLiteral("egtrain-open-")));
	QCOMPARE_LE(name.size(), 45);
	// And has no path separator.
	QVERIFY(!name.contains(QLatin1Char('/')));
	QVERIFY(!name.contains(QLatin1Char('\\')));
	// The application folder is the canonical folder of the program.
	QCOMPARE(OpenRequestRelay::currentApplicationDir(), QDir(QCoreApplication::applicationDirPath()).canonicalPath());
}

// The child modes run without Qt Test, which rejects arguments that it does not know.
int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	const QStringList arguments = QCoreApplication::arguments();
	if (arguments.size() > 1 && arguments.at(1) == QLatin1String("--child-forward"))
		return runChildForward(arguments);
	if (arguments.size() > 1 && arguments.at(1) == QLatin1String("--child-stale"))
		return runChildStale(arguments);

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

	TestOpenRequestRelay test;
	return QTest::qExec(&test, argc, argv);
}

#include "test_openrequestrelay.moc"
