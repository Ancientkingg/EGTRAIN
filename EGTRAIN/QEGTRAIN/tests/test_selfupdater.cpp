#include "update/SelfUpdater.h"
#include "update/UpdateRecord.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

// Drives SelfUpdater for an installed Windows copy against a server on the loopback address. The test
// hook of SelfUpdater replaces the capability of the running copy, so every run happens in a folder
// of this test. The installer is never run: the helper that restart() starts is the update probe.

static constexpr int kGuardMs = 20000;

static bool writeFile(const QString& path, const QByteArray& contents) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

static QByteArray readFile(const QString& path) {
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

static const QString kVersion = QStringLiteral("1.2.3");
static const QString kTag = QStringLiteral("v1.2.3");
static const QString kInstallerKey = QStringLiteral("windows-x64-installer");
static const QString kInstallerName = QStringLiteral("EGTRAIN-Setup-1.2.3-x64.exe");
static const QString kPortableName = QStringLiteral("EGTRAIN-Portable-1.2.3-x64.zip");
static const QByteArray kManifestPath = "/download/v1.2.3/update-manifest.json";
static const QByteArray kPackagePath = "/download/v1.2.3/EGTRAIN-Setup-1.2.3-x64.exe";

static QByteArray packageBytes() {
	QByteArray bytes(200 * 1000, '\0');
	for (int i = 0; i < bytes.size(); ++i)
		bytes[i] = static_cast<char>((i * 31 + i / 256) % 251);
	return bytes;
}

static QString sha256Of(const QByteArray& bytes) {
	return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

static QByteArray manifestJson(const QString& version, const QString& key, const QString& name,
	const QString& sha256, qint64 size) {
	QJsonObject entry;
	entry.insert(QStringLiteral("name"), name);
	entry.insert(QStringLiteral("sha256"), sha256);
	entry.insert(QStringLiteral("size"), static_cast<double>(size));
	QJsonObject assets;
	assets.insert(key, entry);
	QJsonObject root;
	root.insert(QStringLiteral("version"), version);
	root.insert(QStringLiteral("assets"), assets);
	return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// The manifest of a good release of the installer.
static QByteArray goodManifest(const QByteArray& package) {
	return manifestJson(kVersion, kInstallerKey, kInstallerName, sha256Of(package), package.size());
}

// What the server sends for one path.
struct Route {
	QByteArray status = "200 OK";
	QByteArray body;
	// The Content-Length of the response. A negative value announces the size of the body.
	qint64 announced = -1;
	// Keeps the connection open after the body, so that the client waits for the rest.
	bool keepOpen = false;
};

// A small HTTP server on the loopback address that answers by path and counts the requests.
class LoopbackServer {
public:
	LoopbackServer() {
		listening = m_server.listen(QHostAddress::LocalHost, 0);
		QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this]() { accept(); });
	}

	bool listening = false;

	// The folder that the paths of a release start with.
	QString base() const {
		return QStringLiteral("http://127.0.0.1:%1/download").arg(m_server.serverPort());
	}

	void serve(const QByteArray& path, const Route& route) {
		m_routes.insert(path, route);
	}

	int requests(const QByteArray& path) const {
		return m_counts.value(path, 0);
	}

	int totalRequests() const {
		int total = 0;
		for (int count : m_counts)
			total += count;
		return total;
	}

private:
	void accept() {
		while (QTcpSocket* socket = m_server.nextPendingConnection()) {
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
		if (!buffer.contains("\r\n\r\n"))
			return;
		const QList<QByteArray> requestLine = buffer.left(buffer.indexOf('\n')).trimmed().split(' ');
		const QByteArray path = requestLine.size() >= 2 ? requestLine.at(1) : QByteArray();
		m_pending.remove(socket);
		++m_counts[path];
		respond(socket, m_routes.value(path, Route{"404 Not Found", "no such file", -1, false}));
	}

	static void respond(QTcpSocket* socket, const Route& route) {
		const qint64 announced = route.announced < 0 ? route.body.size() : route.announced;
		socket->write("HTTP/1.1 " + route.status + "\r\nContent-Length: " + QByteArray::number(announced)
			+ "\r\nConnection: close\r\n\r\n" + route.body);
		if (!route.keepOpen)
			socket->disconnectFromHost();
	}

	// Declared before the server, so that they outlive the sockets whose handlers use them.
	QHash<QTcpSocket*, QByteArray> m_pending;
	QHash<QByteArray, Route> m_routes;
	QHash<QByteArray, int> m_counts;
	QTcpServer m_server;
};

// What the signals of one update reported.
struct Outcome {
	int finishedCount = 0;
	bool success = false;
	QString error;
	QStringList preparing;
	qint64 lastReceived = -1;
	qint64 lastTotal = -1;
};

// Starts the update and runs the event loop until it has finished or the guard time has passed.
static Outcome runUpdate(SelfUpdater& updater, const StableRelease& release) {
	Outcome outcome;
	QObject context;
	QEventLoop loop;
	QObject::connect(&updater, &SelfUpdater::finished, &context, [&outcome, &loop](bool success, const QString& error) {
		++outcome.finishedCount;
		outcome.success = success;
		outcome.error = error;
		loop.quit();
	});
	QObject::connect(&updater, &SelfUpdater::preparing, &context,
		[&outcome](const QString& version) { outcome.preparing << version; });
	QObject::connect(&updater, &SelfUpdater::progress, &context, [&outcome](qint64 received, qint64 total) {
		outcome.lastReceived = received;
		outcome.lastTotal = total;
	});
	updater.start(release);
	if (outcome.finishedCount == 0) {
		QTimer guard;
		guard.setSingleShot(true);
		QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
		guard.start(kGuardMs);
		loop.exec();
	}
	return outcome;
}

// Runs the event loop for the given time.
static void spin(int milliseconds) {
	QEventLoop loop;
	QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
	loop.exec();
}

static QStringList stagingFolders(const QString& parent) {
	return QDir(parent).entryList({QStringLiteral(".qegtrain-update-*")},
		QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
}

// The names and sizes of everything in a folder, to see that nothing changed.
static QStringList listing(const QString& folder) {
	QStringList entries;
	QDirIterator iterator(folder, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		iterator.next();
		entries << QStringLiteral("%1 %2").arg(iterator.filePath()).arg(iterator.fileInfo().size());
	}
	entries.sort();
	return entries;
}

// The lines of a log that the update probe writes, or an empty list.
static QStringList logLines(const QString& log) {
	return QString::fromUtf8(readFile(log)).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

// Waits until the log holds a line that starts with the prefix. Returns that line, or an empty string.
static QString waitForLine(const QString& log, const QString& prefix) {
	for (int waited = 0; waited < kGuardMs; waited += 25) {
		for (const QString& line : logLines(log))
			if (line.startsWith(prefix))
				return line;
		spin(25);
	}
	return {};
}

// One update in a folder of its own: an installation, the server and the hook. The staging folders
// of the updater appear next to the installation.
class Scenario {
public:
	Scenario(const QString& parent, const QString& label, const QString& helper)
		: root(QDir(parent).filePath(label)), install(root + QStringLiteral("/EGTRAIN")) {
		prepared = QDir().mkpath(install) && writeFile(install + QStringLiteral("/QEGTRAIN.exe"), "program")
			&& writeFile(install + QStringLiteral("/installed-by-setup.ini"), "marker");
		capability.supported = true;
		capability.usesInstaller = true;
		capability.manifestKey = kInstallerKey;
		capability.currentPath = install;
		capability.launchPath = install + QStringLiteral("/QEGTRAIN.exe");
		capability.helperPath = helper;
		SelfUpdater::setTestHook(capability, server.base());
		before = listing(install);
	}

	~Scenario() {
		SelfUpdater::setTestHook(SelfUpdateCapability(), QString());
	}

	Scenario(const Scenario&) = delete;
	Scenario& operator=(const Scenario&) = delete;

	// Replaces the capability that the updater sees.
	void useCapability(const SelfUpdateCapability& replacement) {
		capability = replacement;
		SelfUpdater::setTestHook(capability, server.base());
	}

	// A release that lists the manifest and the package of the given name, at the URLs of the server
	// unless the package is meant to be somewhere else.
	StableRelease release(const QString& packageName = kInstallerName, const QString& packageBase = QString()) const {
		const QString packageFolder = (packageBase.isEmpty() ? server.base() : packageBase) + QStringLiteral("/") + kTag;
		StableRelease result;
		result.version = SemanticVersion{1, 2, 3};
		result.tag = kTag;
		result.releasePage = QUrl(QStringLiteral("https://github.com/Ancientkingg/EGTRAIN/releases/tag/v1.2.3"));
		result.assets.append({QStringLiteral("update-manifest.json"),
			QUrl(server.base() + QStringLiteral("/") + kTag + QStringLiteral("/update-manifest.json"))});
		result.assets.append({packageName, QUrl(packageFolder + QStringLiteral("/") + packageName)});
		return result;
	}

	// Serves a good release of the package, with a manifest that fits it.
	void serveGoodRelease() {
		serveRelease(goodManifest(package), package);
	}

	void serveRelease(const QByteArray& manifest, const QByteArray& packageBody) {
		Route manifestRoute;
		manifestRoute.body = manifest;
		server.serve(kManifestPath, manifestRoute);
		Route packageRoute;
		packageRoute.body = packageBody;
		server.serve(kPackagePath, packageRoute);
	}

	bool installationUnchanged() const {
		return listing(install) == before;
	}

	// The failed update left no staging folder, left the installation as it was and is not busy.
	bool endedCleanly(const SelfUpdater& updater) const {
		return stagingFolders(root).isEmpty() && installationUnchanged() && !updater.isBusy();
	}

	QString root;
	QString install;
	LoopbackServer server;
	QByteArray package = packageBytes();
	SelfUpdateCapability capability;
	QStringList before;
	bool prepared = false;
};

static bool setModificationTime(const QString& path, const QDateTime& time) {
	QFile file(path);
	return file.open(QIODevice::ReadWrite | QIODevice::ExistingOnly) && file.setFileTime(time, QFileDevice::FileModificationTime);
}

class TestSelfUpdater : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void downloadsInstaller();
	void removesEarlierAttempts();
	void restartStartsInstallerMode();
	void restartWithoutHelper();
	void rejectsManifest_data();
	void rejectsManifest();
	void rejectsMissingManifest();
	void rejectsPackageWithOtherHash();
	void rejectsLargerPackage();
	void rejectsSmallerPackage();
	void failsTruncatedDownload();
	void failsMissingPackage();
	void refusesUntrustedPackageUrl();
	void cancelsDownload();
	void cancelsWhileManifestIsAwaited();
	void reportsUnsupportedCopy();
	void refusesReleaseWithoutInstaller();
	void keyChoosesPackage();
	void removesBackup();
	void choosesPackageForDistribution();

private:
	QTemporaryDir m_temp;
	// The update probe, which stands in for the helper.
	QString m_probe;
};

void TestSelfUpdater::initTestCase() {
	QStandardPaths::setTestModeEnabled(true);
	QVERIFY(m_temp.isValid());
	m_probe = QFileInfo(QStringLiteral(EGTRAIN_UPDATE_PROBE)).absoluteFilePath();
	QVERIFY2(QFileInfo(m_probe).isFile(), "the update probe exists");
}

// A good update downloads the manifest and the installer, checks the installer against the manifest,
// stages only that file and extracts nothing.
void TestSelfUpdater::downloadsInstaller() {
	Scenario scenario(m_temp.path(), QStringLiteral("download"), m_probe);
	scenario.serveGoodRelease();
	SelfUpdater updater;
	// The scenario is prepared.
	QVERIFY(scenario.prepared);
	QVERIFY(scenario.server.listening);
	QVERIFY2(updater.canSelfUpdate(scenario.release()), "a release with the installer can be installed");

	const Outcome outcome = runUpdate(updater, scenario.release());
	// The update finishes with success.
	QCOMPARE(outcome.finishedCount, 1);
	QVERIFY(outcome.success);
	QVERIFY(outcome.error.isEmpty());
	// The preparation names the version.
	QCOMPARE(outcome.preparing, QStringList({kVersion}));
	// The progress ends at the size of the installer.
	QCOMPARE(outcome.lastReceived, scenario.package.size());
	QCOMPARE(outcome.lastTotal, scenario.package.size());
	// The manifest and the installer are requested once.
	QCOMPARE(scenario.server.requests(kManifestPath), 1);
	QCOMPARE(scenario.server.requests(kPackagePath), 1);
	QCOMPARE(scenario.server.totalRequests(), 2);
	QVERIFY2(!updater.isBusy(), "the updater is not busy after the download");

	const QStringList folders = stagingFolders(scenario.root);
	// One staging folder is next to the installation.
	QCOMPARE(folders.size(), 1);
	const QString staging = QDir(scenario.root).filePath(folders.first());
	// The staging folder holds the installer as it was served.
	QCOMPARE(readFile(staging + QStringLiteral("/") + kInstallerName), scenario.package);
	// Nothing is extracted and nothing else is staged.
	QCOMPARE(QDir(staging).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden), QStringList({kInstallerName}));
	QVERIFY2(scenario.installationUnchanged(), "the installation is not touched by the download");
}

// The folders that earlier attempts left are removed when a new update starts, unless something in
// them changed lately. The folder of the new update is not touched.
void TestSelfUpdater::removesEarlierAttempts() {
	Scenario scenario(m_temp.path(), QStringLiteral("earlier"), m_probe);
	scenario.serveGoodRelease();
	const QString stale = QDir(scenario.root).filePath(QStringLiteral(".qegtrain-update-stale"));
	const QString recent = QDir(scenario.root).filePath(QStringLiteral(".qegtrain-update-recent"));
	const QString zip = stale + QStringLiteral("/") + kInstallerName;
	// The folders of earlier attempts are written.
	QVERIFY(scenario.prepared);
	QVERIFY(QDir().mkpath(stale));
	QVERIFY(writeFile(zip, "zip"));
	QVERIFY(QDir().mkpath(recent));
	QVERIFY(writeFile(recent + QStringLiteral("/") + kInstallerName, "zip"));
	const QDateTime twoHoursAgo = QDateTime::currentDateTime().addSecs(-2 * 3600);
	QVERIFY2(setModificationTime(zip, twoHoursAgo),
		"the folder of the earlier attempt is dated");

	SelfUpdater updater;
	const Outcome outcome = runUpdate(updater, scenario.release());
	QVERIFY2(outcome.success, "the update is ready");
	QVERIFY2(!QFileInfo::exists(stale), "the folder of an earlier attempt is removed when an update starts");
	QVERIFY2(QFileInfo::exists(recent), "a folder that changed lately is kept");
	const QStringList folders = stagingFolders(scenario.root);
	// The folder of the new update is next to the one that was kept.
	QCOMPARE(folders.size(), 2);
	QVERIFY(folders.contains(QStringLiteral(".qegtrain-update-recent")));
	const QString current = QDir(scenario.root).filePath(folders.first() == QStringLiteral(".qegtrain-update-recent") ? folders.last() : folders.first());
	// The folder of the new update holds its download.
	QCOMPARE(readFile(current + QStringLiteral("/") + kInstallerName), scenario.package);
}

// restart() starts the helper in installer mode with the installer, the silent options, the
// application to start and the record. The helper here is the update probe.
void TestSelfUpdater::restartStartsInstallerMode() {
	Scenario scenario(m_temp.path(), QStringLiteral("restart"), m_probe);
	scenario.serveGoodRelease();
	SelfUpdater updater;
	const Outcome outcome = runUpdate(updater, scenario.release());
	QVERIFY2(outcome.success, "the update is ready");
	const QStringList folders = stagingFolders(scenario.root);
	// One staging folder is next to the installation.
	QCOMPARE(folders.size(), 1);
	const QString staging = QDir(scenario.root).filePath(folders.first());
	const QString log = QDir(scenario.root).filePath(QStringLiteral("helper.log"));
	QVERIFY2(writeFile(staging + QStringLiteral("/behavior.txt"),
				 QStringLiteral("label=helper\nlog=%1\nmode=exit\ncode=0\n").arg(log).toUtf8()),
		"the behavior of the probe is written");

	QVERIFY2(updater.restart(), "the helper starts");
	const QString arguments = waitForLine(log, QStringLiteral("helper args "));
	QStringList expected = {
		QStringLiteral("--parent-pid"), QString::number(QCoreApplication::applicationPid()),
		QStringLiteral("--installer"), staging + QStringLiteral("/") + kInstallerName,
		QStringLiteral("--installer-args"), QStringLiteral("/VERYSILENT /SUPPRESSMSGBOXES /NORESTART"),
		QStringLiteral("--launch"), scenario.install + QStringLiteral("/QEGTRAIN.exe")};
	expected += updateRecordArguments(SemanticVersion{1, 2, 3}, updateRecordPath());
	// The helper gets the installer mode options and none of the options of a file replacement.
	QCOMPARE(arguments, QStringLiteral("helper args ") + expected.join(QStringLiteral("|")));
	QVERIFY2(!waitForLine(log, QStringLiteral("helper started")).isEmpty(), "the helper runs");
	QVERIFY2(scenario.installationUnchanged(), "the updater replaces no file of the installation");
}

// A helper that cannot be started leaves the installation as it is and removes the download.
void TestSelfUpdater::restartWithoutHelper() {
	Scenario scenario(m_temp.path(), QStringLiteral("restart-failure"), m_probe);
	scenario.capability.helperPath = scenario.root + QStringLiteral("/no-such-helper");
	scenario.useCapability(scenario.capability);
	scenario.serveGoodRelease();
	SelfUpdater updater;
	const Outcome outcome = runUpdate(updater, scenario.release());
	// The update is ready and staged.
	QVERIFY(outcome.success);
	QCOMPARE(stagingFolders(scenario.root).size(), 1);
	QVERIFY2(!updater.restart(), "restart reports that the helper did not start");
	QVERIFY2(scenario.endedCleanly(updater), "the staging folder is removed and the installation is as it was");
	QVERIFY2(!updater.restart(), "nothing is left to restart");
}

// A manifest that cannot be used ends the update before the installer is requested.
void TestSelfUpdater::rejectsManifest_data() {
	const QByteArray package = packageBytes();
	const QString hash = sha256Of(package);
	const QString noPackage = QStringLiteral("Update manifest has no valid package for this platform.");
	const auto manifestFor = [&](const QString& key, const QString& name) {
		return manifestJson(kVersion, key, name, hash, package.size());
	};
	// The folder of the scenario, the manifest that the server sends and the text of the error, or an empty string
	// when any error will do.
	QTest::addColumn<QString>("folder");
	QTest::addColumn<QByteArray>("manifest");
	QTest::addColumn<QString>("error");
	int index = 0;
	const auto add = [&index](const char* label, const QByteArray& manifest, const QString& error) {
		QTest::newRow(label) << QStringLiteral("manifest-%1").arg(index++) << manifest << error;
	};
	add("a manifest that is no JSON", "this is not a manifest", QStringLiteral("Invalid update manifest JSON."));
	add("a manifest without the installer entry", manifestFor(QStringLiteral("windows-x64"), kPortableName), noPackage);
	add("an installer entry that names the portable archive", manifestFor(kInstallerKey, kPortableName), noPackage);
	add("an installer entry with the name of another version", manifestFor(kInstallerKey, QStringLiteral("EGTRAIN-Setup-1.2.4-x64.exe")), noPackage);
	add("a manifest of another version", manifestJson(QStringLiteral("1.2.4"), kInstallerKey, kInstallerName, hash, package.size()),
		QStringLiteral("Update manifest version does not match the release tag."));
	add("a hash that is no SHA-256", manifestJson(kVersion, kInstallerKey, kInstallerName, QStringLiteral("not-a-hash"), package.size()),
		QStringLiteral("Update manifest has an invalid SHA-256."));
	add("a size of zero", manifestJson(kVersion, kInstallerKey, kInstallerName, hash, 0),
		QStringLiteral("Update manifest has an invalid package size."));
	add("a manifest of more than 128 KiB", QByteArray(129 * 1024, ' '), QStringLiteral("The update manifest is too large."));
}

void TestSelfUpdater::rejectsManifest() {
	QFETCH(QString, folder);
	QFETCH(QByteArray, manifest);
	QFETCH(QString, error);
	Scenario scenario(m_temp.path(), folder, m_probe);
	scenario.serveRelease(manifest, scenario.package);
	SelfUpdater updater;
	const Outcome outcome = runUpdate(updater, scenario.release());
	// The update fails; the name of the row says which manifest.
	QCOMPARE(outcome.finishedCount, 1);
	QVERIFY(!outcome.success);
	if (error.isEmpty())
		QVERIFY(!outcome.error.isEmpty());
	else
		QCOMPARE(outcome.error, error);
	// No installer is requested after an unusable manifest.
	QCOMPARE(scenario.server.requests(kPackagePath), 0);
	QVERIFY2(scenario.endedCleanly(updater), "an unusable manifest leaves nothing behind");
}

void TestSelfUpdater::rejectsMissingManifest() {
	Scenario missing(m_temp.path(), QStringLiteral("manifest-missing"), m_probe);
	missing.server.serve(kManifestPath, Route{"404 Not Found", "gone", -1, false});
	SelfUpdater updater;
	const Outcome outcome = runUpdate(updater, missing.release());
	// A manifest that the server does not have.
	QCOMPARE(outcome.finishedCount, 1);
	QVERIFY(!outcome.success);
	QVERIFY(!outcome.error.isEmpty());
	QVERIFY2(missing.endedCleanly(updater), "a missing manifest leaves nothing behind");
}

// A download that does not match the manifest is removed and never staged.
void TestSelfUpdater::rejectsPackageWithOtherHash() {
	Scenario scenario(m_temp.path(), QStringLiteral("hash"), m_probe);
	QByteArray other = scenario.package;
	other[1000] = static_cast<char>(other.at(1000) + 1);
	scenario.serveRelease(goodManifest(other), scenario.package);
	SelfUpdater updater;
	const Outcome outcome = runUpdate(updater, scenario.release());
	// An installer that differs from the manifest is rejected.
	QVERIFY(!outcome.success);
	QCOMPARE(outcome.error, QStringLiteral("The downloaded update failed its SHA-256 check."));
	// The installer was downloaded before it was rejected.
	QCOMPARE(scenario.server.requests(kPackagePath), 1);
	QVERIFY2(scenario.endedCleanly(updater), "a failed hash leaves nothing behind");
}

void TestSelfUpdater::rejectsLargerPackage() {
	Scenario scenario(m_temp.path(), QStringLiteral("larger"), m_probe);
	scenario.serveRelease(manifestJson(kVersion, kInstallerKey, kInstallerName, sha256Of(scenario.package), scenario.package.size() - 10),
		scenario.package);
	SelfUpdater updater;
	const Outcome outcome = runUpdate(updater, scenario.release());
	// An installer larger than the manifest says is rejected.
	QVERIFY(!outcome.success);
	QCOMPARE(outcome.error, QStringLiteral("The update package is larger than the release manifest."));
	QVERIFY2(scenario.endedCleanly(updater), "an installer that is too large leaves nothing behind");
}

void TestSelfUpdater::rejectsSmallerPackage() {
	Scenario scenario(m_temp.path(), QStringLiteral("smaller"), m_probe);
	scenario.serveRelease(manifestJson(kVersion, kInstallerKey, kInstallerName, sha256Of(scenario.package), scenario.package.size() + 10),
		scenario.package);
	SelfUpdater updater;
	const Outcome outcome = runUpdate(updater, scenario.release());
	// An installer smaller than the manifest says is rejected.
	QVERIFY(!outcome.success);
	QCOMPARE(outcome.error, QStringLiteral("The update package size does not match the release manifest."));
	QVERIFY2(scenario.endedCleanly(updater), "an installer that is too small leaves nothing behind");
}

void TestSelfUpdater::failsTruncatedDownload() {
	Scenario scenario(m_temp.path(), QStringLiteral("truncated"), m_probe);
	scenario.serveGoodRelease();
	Route cut;
	cut.body = scenario.package.left(scenario.package.size() / 2);
	cut.announced = scenario.package.size();
	scenario.server.serve(kPackagePath, cut);
	SelfUpdater updater;
	const Outcome outcome = runUpdate(updater, scenario.release());
	// A download that the server cuts short fails.
	QCOMPARE(outcome.finishedCount, 1);
	QVERIFY(!outcome.success);
	QVERIFY(!outcome.error.isEmpty());
	QVERIFY2(scenario.endedCleanly(updater), "a truncated download leaves nothing behind");
}

void TestSelfUpdater::failsMissingPackage() {
	Scenario scenario(m_temp.path(), QStringLiteral("package-missing"), m_probe);
	scenario.serveGoodRelease();
	scenario.server.serve(kPackagePath, Route{"404 Not Found", "gone", -1, false});
	SelfUpdater updater;
	const Outcome outcome = runUpdate(updater, scenario.release());
	// An installer that the server does not have fails.
	QCOMPARE(outcome.finishedCount, 1);
	QVERIFY(!outcome.success);
	QVERIFY(!outcome.error.isEmpty());
	QVERIFY2(scenario.endedCleanly(updater), "a missing installer leaves nothing behind");
}

void TestSelfUpdater::refusesUntrustedPackageUrl() {
	Scenario scenario(m_temp.path(), QStringLiteral("untrusted"), m_probe);
	scenario.serveGoodRelease();
	SelfUpdater updater;
	const Outcome outcome = runUpdate(updater, scenario.release(kInstallerName, scenario.server.base() + QStringLiteral("/elsewhere")));
	// An installer outside the folder of the release is not downloaded.
	QVERIFY(!outcome.success);
	QCOMPARE(outcome.error, QStringLiteral("The release package URL is not trusted."));
	// No request and no staging folder for an untrusted URL.
	QCOMPARE(scenario.server.requests(kPackagePath), 0);
	QVERIFY(stagingFolders(scenario.root).isEmpty());
}

// Cancel ends the update at any time before the preparation and removes what was downloaded.
void TestSelfUpdater::cancelsDownload() {
	Scenario scenario(m_temp.path(), QStringLiteral("cancel-package"), m_probe);
	scenario.serveGoodRelease();
	Route partial;
	partial.body = scenario.package.left(4096);
	partial.announced = scenario.package.size();
	partial.keepOpen = true;
	scenario.server.serve(kPackagePath, partial);
	SelfUpdater updater;
	// The cancel comes a little after the server has seen the request for the installer.
	QStringList stagedAtCancel;
	bool requested = false;
	QTimer watcher;
	watcher.setInterval(20);
	QObject::connect(&watcher, &QTimer::timeout, &updater, [&]() {
		if (requested || scenario.server.requests(kPackagePath) == 0)
			return;
		requested = true;
		QTimer::singleShot(300, &updater, [&]() {
			stagedAtCancel = stagingFolders(scenario.root);
			updater.cancel();
		});
	});
	watcher.start();
	const Outcome outcome = runUpdate(updater, scenario.release());
	watcher.stop();
	// The download was under way and staged when it was cancelled.
	QVERIFY(requested);
	QCOMPARE(stagedAtCancel.size(), 1);
	// A cancel during the download ends the update.
	QCOMPARE(outcome.finishedCount, 1);
	QVERIFY(!outcome.success);
	QCOMPARE(outcome.error, QStringLiteral("Update cancelled."));
	QVERIFY2(scenario.endedCleanly(updater), "a cancel during the download removes the partial file");
}

void TestSelfUpdater::cancelsWhileManifestIsAwaited() {
	Scenario scenario(m_temp.path(), QStringLiteral("cancel-manifest"), m_probe);
	scenario.serveGoodRelease();
	Route silent;
	silent.announced = 100;
	silent.keepOpen = true;
	scenario.server.serve(kManifestPath, silent);
	SelfUpdater updater;
	QTimer::singleShot(150, &updater, [&updater]() { updater.cancel(); });
	const Outcome outcome = runUpdate(updater, scenario.release());
	// A cancel while the manifest is awaited ends the update.
	QCOMPARE(outcome.finishedCount, 1);
	QVERIFY(!outcome.success);
	QCOMPARE(outcome.error, QStringLiteral("Update cancelled."));
	// No installer is requested after the cancel.
	QCOMPARE(scenario.server.requests(kPackagePath), 0);
	QVERIFY2(scenario.endedCleanly(updater), "a cancel before the download leaves nothing behind");
}

// An update that cannot go on says why before it asks the server anything.
void TestSelfUpdater::reportsUnsupportedCopy() {
	Scenario scenario(m_temp.path(), QStringLiteral("unsupported"), m_probe);
	scenario.serveGoodRelease();
	SelfUpdateCapability unsupported = scenario.capability;
	unsupported.supported = false;
	unsupported.reason = QStringLiteral("The packaged installation is not writable.");
	scenario.useCapability(unsupported);
	SelfUpdater updater;
	QVERIFY2(!updater.canSelfUpdate(scenario.release()), "a copy that cannot update is not offered the update");
	const Outcome outcome = runUpdate(updater, scenario.release());
	// The reason of the capability is reported.
	QCOMPARE(outcome.finishedCount, 1);
	QVERIFY(!outcome.success);
	QCOMPARE(outcome.error, unsupported.reason);
	// Nothing is requested and nothing is staged.
	QCOMPARE(scenario.server.totalRequests(), 0);
	QVERIFY(scenario.endedCleanly(updater));
}

void TestSelfUpdater::refusesReleaseWithoutInstaller() {
	Scenario scenario(m_temp.path(), QStringLiteral("no-package"), m_probe);
	scenario.serveGoodRelease();
	StableRelease manifestOnly = scenario.release();
	manifestOnly.assets.removeLast();
	SelfUpdater updater;
	QVERIFY2(!updater.canSelfUpdate(manifestOnly), "a release without the installer is not offered");
	const Outcome outcome = runUpdate(updater, manifestOnly);
	// A release without the installer cannot be installed.
	QCOMPARE(outcome.finishedCount, 1);
	QVERIFY(!outcome.success);
	QCOMPARE(outcome.error, QStringLiteral("This release does not provide a verified update package."));
	// Nothing is requested for a release without the installer.
	QCOMPARE(scenario.server.totalRequests(), 0);
}

// The capability decides which package of a release is the one of this copy.
void TestSelfUpdater::keyChoosesPackage() {
	Scenario scenario(m_temp.path(), QStringLiteral("key"), m_probe);
	SelfUpdater updater;
	const StableRelease withInstaller = scenario.release(kInstallerName);
	const StableRelease withArchive = scenario.release(kPortableName);
	// An installed copy takes the installer and not the portable archive.
	QVERIFY(updater.canSelfUpdate(withInstaller));
	QVERIFY(!updater.canSelfUpdate(withArchive));

	SelfUpdateCapability portable = scenario.capability;
	portable.usesInstaller = false;
	portable.manifestKey = QStringLiteral("windows-x64");
	scenario.useCapability(portable);
	// A portable copy takes the archive and not the installer.
	QVERIFY(updater.canSelfUpdate(withArchive));
	QVERIFY(!updater.canSelfUpdate(withInstaller));
}

// The second sweep after the start removes the backup that an update in place kept: the folder or the file
// named like the installation plus .egtrain-old. The sweep at the start leaves it, because the update helper
// may still need it. A symbolic link of that name stays, and so does the backup while a staging folder is next
// to the installation. A copy that updates with the installer has no backup, so nothing is removed for it.
void TestSelfUpdater::removesBackup() {
	Scenario scenario(m_temp.path(), QStringLiteral("backup"), m_probe);
	const SelfUpdateCapability installed = scenario.capability;
	SelfUpdateCapability portable = scenario.capability;
	portable.usesInstaller = false;
	portable.manifestKey = QStringLiteral("windows-x64");
	scenario.useCapability(portable);
	const QString backup = scenario.install + QStringLiteral(".egtrain-old");
	// The backup folder is written.
	QVERIFY(scenario.prepared);
	QVERIFY(QDir().mkpath(backup));
	QVERIFY(writeFile(backup + QStringLiteral("/QEGTRAIN.exe"), "old"));
	SelfUpdater::cleanupStaleStaging();
	QVERIFY2(QFileInfo::exists(backup + QStringLiteral("/QEGTRAIN.exe")), "the sweep of stale staging folders keeps the backup");
	SelfUpdater::removeUpdateBackup();
	QVERIFY2(!QFileInfo::exists(backup), "a backup folder is removed");
	QVERIFY2(scenario.installationUnchanged(), "the installation is not touched");

	QVERIFY2(writeFile(backup, "old"), "the backup file is written");
	SelfUpdater::removeUpdateBackup();
	QVERIFY2(!QFileInfo::exists(backup), "a backup file is removed");

	const QString staging = QDir(scenario.root).filePath(QStringLiteral(".qegtrain-update-running"));
	// A staging folder and the backup are written.
	QVERIFY(QDir().mkpath(staging));
	QVERIFY(writeFile(backup, "old"));
	SelfUpdater::removeUpdateBackup();
	QVERIFY2(QFileInfo::exists(backup), "the backup stays while a staging folder is next to the installation");
	QVERIFY2(QDir(staging).removeRecursively(), "the staging folder is removed");

	scenario.useCapability(installed);
	SelfUpdater::removeUpdateBackup();
	QVERIFY2(QFileInfo::exists(backup), "nothing is removed for a copy that updates with the installer");
	QVERIFY2(QFile::remove(backup), "the backup file of the installed copy is removed");
	scenario.useCapability(portable);

// QFile::link makes a shortcut file on Windows, so a symbolic link is only covered elsewhere.
#if !defined(Q_OS_WIN)
	const QString target = QDir(scenario.root).filePath(QStringLiteral("elsewhere"));
	// A symbolic link named like the backup is made.
	QVERIFY(QDir().mkpath(target));
	QVERIFY(writeFile(target + QStringLiteral("/kept"), "kept"));
	QVERIFY(QFile::link(target, backup));
	SelfUpdater::removeUpdateBackup();
	// A symbolic link named like the backup and its target stay.
	QVERIFY(QFileInfo(backup).isSymLink());
	QVERIFY(QFileInfo::exists(target + QStringLiteral("/kept")));
#endif
}

// The kind of copy decides whether the installer or the archive of the platform is its package.
void TestSelfUpdater::choosesPackageForDistribution() {
	const QString platform = QStringLiteral("windows-x64");
	const UpdatePackage installed = updatePackageFor(Distribution::WindowsInstalled, platform);
	// An installed Windows copy takes the installer.
	QVERIFY(installed.usesInstaller);
	QCOMPARE(installed.manifestKey, kInstallerKey);
	const UpdatePackage portable = updatePackageFor(Distribution::WindowsPortable, platform);
	// A portable Windows copy keeps the archive.
	QVERIFY(!portable.usesInstaller);
	QCOMPARE(portable.manifestKey, platform);
	const UpdatePackage macApp = updatePackageFor(Distribution::MacApp, QStringLiteral("macos-arm64"));
	// A macOS app keeps its package.
	QVERIFY(!macApp.usesInstaller);
	QCOMPARE(macApp.manifestKey, QStringLiteral("macos-arm64"));
	const UpdatePackage appImage = updatePackageFor(Distribution::LinuxAppImage, QStringLiteral("linux-x86_64"));
	// A Linux AppImage keeps its package.
	QVERIFY(!appImage.usesInstaller);
	QCOMPARE(appImage.manifestKey, QStringLiteral("linux-x86_64"));
	const UpdatePackage unsupported = updatePackageFor(Distribution::Unsupported, QString());
	// An unsupported platform has no package.
	QVERIFY(!unsupported.usesInstaller);
	QVERIFY(unsupported.manifestKey.isEmpty());
	QVERIFY2(updatePackageFor(Distribution::WindowsInstalled, QString()).manifestKey.isEmpty(),
		"an installed copy of a platform without a manifest entry has no package");
}

QTEST_GUILESS_MAIN(TestSelfUpdater)
#include "test_selfupdater.moc"
