#include "update/SelfUpdater.h"
#include "update/UpdateRecord.h"

#include <QCoreApplication>
#include <QCryptographicHash>
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
#include <QTimer>

#include <iostream>

// Drives SelfUpdater for an installed Windows copy against a server on the loopback address. The test
// hook of SelfUpdater replaces the capability of the running copy, so every run happens in a folder
// of this test. The installer is never run: the helper that restart() starts is the update probe.

static constexpr int kGuardMs = 20000;

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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

// A good update downloads the manifest and the installer, checks the installer against the manifest,
// stages only that file and extracts nothing.
static bool testDownload(const QString& base, const QString& helper) {
	Scenario scenario(base, QStringLiteral("download"), helper);
	scenario.serveGoodRelease();
	SelfUpdater updater;
	bool ok = expect(scenario.prepared && scenario.server.listening, "the scenario is prepared");
	ok &= expect(updater.canSelfUpdate(scenario.release()), "a release with the installer can be installed");

	const Outcome outcome = runUpdate(updater, scenario.release());
	ok &= expect(outcome.finishedCount == 1 && outcome.success && outcome.error.isEmpty(), "the update finishes with success");
	ok &= expect(outcome.preparing == QStringList({kVersion}), "the preparation names the version");
	ok &= expect(outcome.lastReceived == scenario.package.size() && outcome.lastTotal == scenario.package.size(),
		"the progress ends at the size of the installer");
	ok &= expect(scenario.server.requests(kManifestPath) == 1 && scenario.server.requests(kPackagePath) == 1
			&& scenario.server.totalRequests() == 2,
		"the manifest and the installer are requested once");
	ok &= expect(!updater.isBusy(), "the updater is not busy after the download");

	const QStringList folders = stagingFolders(scenario.root);
	ok &= expect(folders.size() == 1, "one staging folder is next to the installation");
	const QString staging = folders.isEmpty() ? QString() : QDir(scenario.root).filePath(folders.first());
	ok &= expect(readFile(staging + QStringLiteral("/") + kInstallerName) == scenario.package,
		"the staging folder holds the installer as it was served");
	ok &= expect(QDir(staging).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden) == QStringList({kInstallerName}),
		"nothing is extracted and nothing else is staged");
	ok &= expect(scenario.installationUnchanged(), "the installation is not touched by the download");
	return ok;
}

// restart() starts the helper in installer mode with the installer, the silent options, the
// application to start and the record. The helper here is the update probe.
static bool testRestartStartsInstallerMode(const QString& base, const QString& helper) {
	Scenario scenario(base, QStringLiteral("restart"), helper);
	scenario.serveGoodRelease();
	SelfUpdater updater;
	const Outcome outcome = runUpdate(updater, scenario.release());
	bool ok = expect(outcome.success, "the update is ready");
	const QStringList folders = stagingFolders(scenario.root);
	ok &= expect(folders.size() == 1, "one staging folder is next to the installation");
	if (!ok)
		return false;
	const QString staging = QDir(scenario.root).filePath(folders.first());
	const QString log = QDir(scenario.root).filePath(QStringLiteral("helper.log"));
	ok &= expect(writeFile(staging + QStringLiteral("/behavior.txt"),
					 QStringLiteral("label=helper\nlog=%1\nmode=exit\ncode=0\n").arg(log).toUtf8()),
		"the behavior of the probe is written");

	ok &= expect(updater.restart(), "the helper starts");
	const QString arguments = waitForLine(log, QStringLiteral("helper args "));
	QStringList expected = {
		QStringLiteral("--parent-pid"), QString::number(QCoreApplication::applicationPid()),
		QStringLiteral("--installer"), staging + QStringLiteral("/") + kInstallerName,
		QStringLiteral("--installer-args"), QStringLiteral("/VERYSILENT /SUPPRESSMSGBOXES /NORESTART"),
		QStringLiteral("--launch"), scenario.install + QStringLiteral("/QEGTRAIN.exe")};
	expected += updateRecordArguments(SemanticVersion{1, 2, 3}, updateRecordPath());
	ok &= expect(arguments == QStringLiteral("helper args ") + expected.join(QStringLiteral("|")),
		"the helper gets the installer mode options and none of the options of a file replacement");
	ok &= expect(!waitForLine(log, QStringLiteral("helper started")).isEmpty(), "the helper runs");
	ok &= expect(scenario.installationUnchanged(), "the updater replaces no file of the installation");
	return ok;
}

// A helper that cannot be started leaves the installation as it is and removes the download.
static bool testRestartWithoutHelper(const QString& base, const QString& helper) {
	Scenario scenario(base, QStringLiteral("restart-failure"), helper);
	scenario.capability.helperPath = scenario.root + QStringLiteral("/no-such-helper");
	scenario.useCapability(scenario.capability);
	scenario.serveGoodRelease();
	SelfUpdater updater;
	const Outcome outcome = runUpdate(updater, scenario.release());
	bool ok = expect(outcome.success && stagingFolders(scenario.root).size() == 1, "the update is ready and staged");
	ok &= expect(!updater.restart(), "restart reports that the helper did not start");
	ok &= expect(scenario.endedCleanly(updater), "the staging folder is removed and the installation is as it was");
	ok &= expect(!updater.restart(), "nothing is left to restart");
	return ok;
}

struct ManifestCase {
	const char* label;
	QByteArray manifest;
	// The text of the error, or an empty string when any error will do.
	QString error;
};

// A manifest that cannot be used ends the update before the installer is requested.
static bool testManifestRejected(const QString& base, const QString& helper) {
	const QByteArray package = packageBytes();
	const QString hash = sha256Of(package);
	const QString noPackage = QStringLiteral("Update manifest has no valid package for this platform.");
	const auto manifestFor = [&](const QString& key, const QString& name) {
		return manifestJson(kVersion, key, name, hash, package.size());
	};
	const ManifestCase cases[] = {
		{"a manifest that is no JSON", "this is not a manifest", QStringLiteral("Invalid update manifest JSON.")},
		{"a manifest without the installer entry", manifestFor(QStringLiteral("windows-x64"), kPortableName), noPackage},
		{"an installer entry that names the portable archive", manifestFor(kInstallerKey, kPortableName), noPackage},
		{"an installer entry with the name of another version", manifestFor(kInstallerKey, QStringLiteral("EGTRAIN-Setup-1.2.4-x64.exe")), noPackage},
		{"a manifest of another version", manifestJson(QStringLiteral("1.2.4"), kInstallerKey, kInstallerName, hash, package.size()),
			QStringLiteral("Update manifest version does not match the release tag.")},
		{"a hash that is no SHA-256", manifestJson(kVersion, kInstallerKey, kInstallerName, QStringLiteral("not-a-hash"), package.size()),
			QStringLiteral("Update manifest has an invalid SHA-256.")},
		{"a size of zero", manifestJson(kVersion, kInstallerKey, kInstallerName, hash, 0),
			QStringLiteral("Update manifest has an invalid package size.")},
		{"a manifest of more than 128 KiB", QByteArray(129 * 1024, ' '), QStringLiteral("The update manifest is too large.")}};
	bool ok = true;
	int index = 0;
	for (const ManifestCase& entry : cases) {
		Scenario scenario(base, QStringLiteral("manifest-%1").arg(index++), helper);
		scenario.serveRelease(entry.manifest, package);
		SelfUpdater updater;
		const Outcome outcome = runUpdate(updater, scenario.release());
		ok &= expect(outcome.finishedCount == 1 && !outcome.success, entry.label);
		ok &= expect(entry.error.isEmpty() ? !outcome.error.isEmpty() : outcome.error == entry.error, entry.label);
		ok &= expect(scenario.server.requests(kPackagePath) == 0, "no installer is requested after an unusable manifest");
		ok &= expect(scenario.endedCleanly(updater), entry.label);
	}

	Scenario missing(base, QStringLiteral("manifest-missing"), helper);
	missing.server.serve(kManifestPath, Route{"404 Not Found", "gone", -1, false});
	SelfUpdater updater;
	const Outcome outcome = runUpdate(updater, missing.release());
	ok &= expect(outcome.finishedCount == 1 && !outcome.success && !outcome.error.isEmpty(), "a manifest that the server does not have");
	ok &= expect(missing.endedCleanly(updater), "a missing manifest leaves nothing behind");
	return ok;
}

// A download that does not match the manifest is removed and never staged.
static bool testPackageRejected(const QString& base, const QString& helper) {
	bool ok = true;

	{
		Scenario scenario(base, QStringLiteral("hash"), helper);
		QByteArray other = scenario.package;
		other[1000] = static_cast<char>(other.at(1000) + 1);
		scenario.serveRelease(goodManifest(other), scenario.package);
		SelfUpdater updater;
		const Outcome outcome = runUpdate(updater, scenario.release());
		ok &= expect(!outcome.success && outcome.error == QStringLiteral("The downloaded update failed its SHA-256 check."),
			"an installer that differs from the manifest is rejected");
		ok &= expect(scenario.server.requests(kPackagePath) == 1, "the installer was downloaded before it was rejected");
		ok &= expect(scenario.endedCleanly(updater), "a failed hash leaves nothing behind");
	}
	{
		Scenario scenario(base, QStringLiteral("larger"), helper);
		scenario.serveRelease(manifestJson(kVersion, kInstallerKey, kInstallerName, sha256Of(scenario.package), scenario.package.size() - 10),
			scenario.package);
		SelfUpdater updater;
		const Outcome outcome = runUpdate(updater, scenario.release());
		ok &= expect(!outcome.success && outcome.error == QStringLiteral("The update package is larger than the release manifest."),
			"an installer larger than the manifest says is rejected");
		ok &= expect(scenario.endedCleanly(updater), "an installer that is too large leaves nothing behind");
	}
	{
		Scenario scenario(base, QStringLiteral("smaller"), helper);
		scenario.serveRelease(manifestJson(kVersion, kInstallerKey, kInstallerName, sha256Of(scenario.package), scenario.package.size() + 10),
			scenario.package);
		SelfUpdater updater;
		const Outcome outcome = runUpdate(updater, scenario.release());
		ok &= expect(!outcome.success && outcome.error == QStringLiteral("The update package size does not match the release manifest."),
			"an installer smaller than the manifest says is rejected");
		ok &= expect(scenario.endedCleanly(updater), "an installer that is too small leaves nothing behind");
	}
	{
		Scenario scenario(base, QStringLiteral("truncated"), helper);
		scenario.serveGoodRelease();
		Route cut;
		cut.body = scenario.package.left(scenario.package.size() / 2);
		cut.announced = scenario.package.size();
		scenario.server.serve(kPackagePath, cut);
		SelfUpdater updater;
		const Outcome outcome = runUpdate(updater, scenario.release());
		ok &= expect(outcome.finishedCount == 1 && !outcome.success && !outcome.error.isEmpty(),
			"a download that the server cuts short fails");
		ok &= expect(scenario.endedCleanly(updater), "a truncated download leaves nothing behind");
	}
	{
		Scenario scenario(base, QStringLiteral("package-missing"), helper);
		scenario.serveGoodRelease();
		scenario.server.serve(kPackagePath, Route{"404 Not Found", "gone", -1, false});
		SelfUpdater updater;
		const Outcome outcome = runUpdate(updater, scenario.release());
		ok &= expect(outcome.finishedCount == 1 && !outcome.success && !outcome.error.isEmpty(),
			"an installer that the server does not have fails");
		ok &= expect(scenario.endedCleanly(updater), "a missing installer leaves nothing behind");
	}
	{
		Scenario scenario(base, QStringLiteral("untrusted"), helper);
		scenario.serveGoodRelease();
		SelfUpdater updater;
		const Outcome outcome = runUpdate(updater, scenario.release(kInstallerName, scenario.server.base() + QStringLiteral("/elsewhere")));
		ok &= expect(!outcome.success && outcome.error == QStringLiteral("The release package URL is not trusted."),
			"an installer outside the folder of the release is not downloaded");
		ok &= expect(scenario.server.requests(kPackagePath) == 0 && stagingFolders(scenario.root).isEmpty(),
			"no request and no staging folder for an untrusted URL");
	}
	return ok;
}

// Cancel ends the update at any time before the preparation and removes what was downloaded.
static bool testCancel(const QString& base, const QString& helper) {
	bool ok = true;
	{
		Scenario scenario(base, QStringLiteral("cancel-package"), helper);
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
		ok &= expect(requested && stagedAtCancel.size() == 1, "the download was under way and staged when it was cancelled");
		ok &= expect(outcome.finishedCount == 1 && !outcome.success && outcome.error == QStringLiteral("Update cancelled."),
			"a cancel during the download ends the update");
		ok &= expect(scenario.endedCleanly(updater), "a cancel during the download removes the partial file");
	}
	{
		Scenario scenario(base, QStringLiteral("cancel-manifest"), helper);
		scenario.serveGoodRelease();
		Route silent;
		silent.announced = 100;
		silent.keepOpen = true;
		scenario.server.serve(kManifestPath, silent);
		SelfUpdater updater;
		QTimer::singleShot(150, &updater, [&updater]() { updater.cancel(); });
		const Outcome outcome = runUpdate(updater, scenario.release());
		ok &= expect(outcome.finishedCount == 1 && !outcome.success && outcome.error == QStringLiteral("Update cancelled."),
			"a cancel while the manifest is awaited ends the update");
		ok &= expect(scenario.server.requests(kPackagePath) == 0, "no installer is requested after the cancel");
		ok &= expect(scenario.endedCleanly(updater), "a cancel before the download leaves nothing behind");
	}
	return ok;
}

// An update that cannot go on says why before it asks the server anything.
static bool testNotPossible(const QString& base, const QString& helper) {
	bool ok = true;
	{
		Scenario scenario(base, QStringLiteral("unsupported"), helper);
		scenario.serveGoodRelease();
		SelfUpdateCapability unsupported = scenario.capability;
		unsupported.supported = false;
		unsupported.reason = QStringLiteral("The packaged installation is not writable.");
		scenario.useCapability(unsupported);
		SelfUpdater updater;
		ok &= expect(!updater.canSelfUpdate(scenario.release()), "a copy that cannot update is not offered the update");
		const Outcome outcome = runUpdate(updater, scenario.release());
		ok &= expect(outcome.finishedCount == 1 && !outcome.success && outcome.error == unsupported.reason,
			"the reason of the capability is reported");
		ok &= expect(scenario.server.totalRequests() == 0 && scenario.endedCleanly(updater), "nothing is requested and nothing is staged");
	}
	{
		Scenario scenario(base, QStringLiteral("no-package"), helper);
		scenario.serveGoodRelease();
		StableRelease manifestOnly = scenario.release();
		manifestOnly.assets.removeLast();
		SelfUpdater updater;
		ok &= expect(!updater.canSelfUpdate(manifestOnly), "a release without the installer is not offered");
		const Outcome outcome = runUpdate(updater, manifestOnly);
		ok &= expect(outcome.finishedCount == 1 && !outcome.success
				&& outcome.error == QStringLiteral("This release does not provide a verified update package."),
			"a release without the installer cannot be installed");
		ok &= expect(scenario.server.totalRequests() == 0, "nothing is requested for a release without the installer");
	}
	return ok;
}

// The capability decides which package of a release is the one of this copy.
static bool testKeyChoosesPackage(const QString& base, const QString& helper) {
	Scenario scenario(base, QStringLiteral("key"), helper);
	SelfUpdater updater;
	const StableRelease withInstaller = scenario.release(kInstallerName);
	const StableRelease withArchive = scenario.release(kPortableName);
	bool ok = expect(updater.canSelfUpdate(withInstaller) && !updater.canSelfUpdate(withArchive),
		"an installed copy takes the installer and not the portable archive");

	SelfUpdateCapability portable = scenario.capability;
	portable.usesInstaller = false;
	portable.manifestKey = QStringLiteral("windows-x64");
	scenario.useCapability(portable);
	ok &= expect(updater.canSelfUpdate(withArchive) && !updater.canSelfUpdate(withInstaller),
		"a portable copy takes the archive and not the installer");
	return ok;
}

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	QStandardPaths::setTestModeEnabled(true);
	if (argc < 2) {
		std::cerr << "usage: test_selfupdater UPDATE_PROBE\n";
		return 2;
	}
	QTemporaryDir temp;
	if (!temp.isValid())
		return 1;
	const QString probe = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();

	bool ok = expect(QFileInfo(probe).isFile(), "the update probe exists");
	ok &= testDownload(temp.path(), probe);
	ok &= testRestartStartsInstallerMode(temp.path(), probe);
	ok &= testRestartWithoutHelper(temp.path(), probe);
	ok &= testManifestRejected(temp.path(), probe);
	ok &= testPackageRejected(temp.path(), probe);
	ok &= testCancel(temp.path(), probe);
	ok &= testNotPossible(temp.path(), probe);
	ok &= testKeyChoosesPackage(temp.path(), probe);
	return ok ? 0 : 1;
}
