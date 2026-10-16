#include "update/UpdatePreparation.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <functional>
#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static bool writeFile(const QString& path, const QByteArray& contents) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

struct PreparationOutcome {
	UpdatePreparationResult result;
	QThread* stagerThread = nullptr;
	QThread* deliveryThread = nullptr;
	bool stagerRan = false;
	int heartbeatTicks = 0;
};

static PreparationOutcome runPreparation(const UpdatePreparationInput& input,
	const std::function<QString(const UpdatePreparationInput&, QString*)>& stager) {
	PreparationOutcome outcome;
	QEventLoop loop;
	auto* thread = new QThread;
	auto* worker = new UpdatePreparationWorker;
	worker->setPlatformStager([&outcome, stager](const UpdatePreparationInput& stagerInput,
								  QString* error) {
		outcome.stagerRan = true;
		outcome.stagerThread = QThread::currentThread();
		QThread::msleep(150);
		return stager ? stager(stagerInput, error) : QString();
	});
	worker->moveToThread(thread);
	QObject::connect(thread, &QThread::started, worker,
		[worker, input]() { worker->prepare(input); });
	QObject::connect(worker, &UpdatePreparationWorker::finished, &loop,
		[&outcome, &loop](const UpdatePreparationResult& result) {
			outcome.result = result;
			outcome.deliveryThread = QThread::currentThread();
			loop.quit();
		});
	QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);

	QTimer heartbeat;
	heartbeat.setInterval(10);
	QObject::connect(&heartbeat, &QTimer::timeout, &heartbeat,
		[&outcome]() { ++outcome.heartbeatTicks; });
	QTimer watchdog;
	watchdog.setSingleShot(true);
	QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);

	heartbeat.start();
	watchdog.start(15000);
	thread->start();
	loop.exec();
	heartbeat.stop();
	thread->quit();
	if (!thread->wait(5000))
		std::cerr << "failed: preparation thread stopped\n";
	delete thread;
	return outcome;
}

static bool setModificationTime(const QString& path, const QDateTime& time) {
	QFile file(path);
	return file.open(QIODevice::ReadWrite | QIODevice::ExistingOnly) && file.setFileTime(time, QFileDevice::FileModificationTime);
}

static bool testStaleStagingSweep(const QString& base) {
	const QDir parent(QDir(base).filePath("sweep"));
	const QString stale = parent.filePath(".qegtrain-update-stale");
	const QString empty = parent.filePath(".qegtrain-update-empty");
	const QString other = parent.filePath("other");
	const QString upper = parent.filePath(".QEGTRAIN-UPDATE-upper");
	const QString file = parent.filePath(".qegtrain-update-file");
	const QString target = parent.filePath("target");
	const QString link = parent.filePath(".qegtrain-update-link");
	bool ok = expect(QDir().mkpath(stale + "/extract") && QDir().mkpath(empty) && QDir().mkpath(other)
			&& QDir().mkpath(target) && writeFile(stale + "/package.zip", "p") && writeFile(stale + "/extract/file", "f")
			&& writeFile(other + "/file", "o") && QDir().mkpath(upper) && writeFile(upper + "/file", "u") && writeFile(file, "f")
			&& writeFile(target + "/keep", "k")
			&& QFile::link(target, link),
		"sweep fixtures are writable");

	removeStaleUpdateStaging(parent.path(), std::chrono::minutes(60));
	ok &= expect(QFileInfo::exists(stale) && QFileInfo::exists(empty),
		"a staging folder that changed within the minimum age is kept");

	QThread::msleep(20);
	removeStaleUpdateStaging(parent.path(), std::chrono::minutes(0));
	ok &= expect(!QFileInfo::exists(stale), "an old staging folder is removed with its content");
	ok &= expect(!QFileInfo::exists(empty), "an old empty staging folder is removed");
	ok &= expect(QFileInfo::exists(other + "/file"), "a folder with another name is kept");
	ok &= expect(QFileInfo::exists(upper + "/file"), "a folder whose name differs in case is kept");
	ok &= expect(QFileInfo::exists(file), "a file with the name of a staging folder is kept");
	ok &= expect(QFileInfo::exists(target + "/keep"), "the target of a symbolic link is kept");
// QFile::link makes a shortcut file on Windows, so a symbolic link is only covered elsewhere.
#if !defined(Q_OS_WIN)
	ok &= expect(QFileInfo(link).isSymLink(), "a symbolic link with the name of a staging folder is kept");
#endif

	// The folder time is now. Only the age of the entries counts.
	const QString oldEntries = parent.filePath(".qegtrain-update-old-entries");
	const QString recentInside = parent.filePath(".qegtrain-update-recent-inside");
	const QDateTime twoHoursAgo = QDateTime::currentDateTime().addSecs(-2 * 3600);
	ok &= expect(QDir().mkpath(oldEntries) && writeFile(oldEntries + "/package.zip", "p")
			&& QDir().mkpath(recentInside + "/extract") && writeFile(recentInside + "/package.zip", "p")
			&& writeFile(recentInside + "/extract/file", "f"),
		"age fixtures are writable");
	ok &= expect(setModificationTime(oldEntries + "/package.zip", twoHoursAgo) && setModificationTime(recentInside + "/package.zip", twoHoursAgo),
		"age fixtures can be dated");
	removeStaleUpdateStaging(parent.path(), std::chrono::minutes(60));
	ok &= expect(!QFileInfo::exists(oldEntries), "a folder whose entries are old is removed although the folder itself changed");
	ok &= expect(QFileInfo::exists(recentInside), "a recent entry in a subfolder keeps the folder");

	removeStaleUpdateStaging(QDir(base).filePath("missing"), std::chrono::minutes(0));
	ok &= expect(!QFileInfo::exists(QDir(base).filePath("missing")), "a missing parent is left alone");
	return ok;
}

static bool testCanCreateFilesIn(const QString& base) {
	const QString writable = QDir(base).filePath("probe-writable");
	bool ok = expect(QDir().mkpath(writable), "probe fixture is writable");
	ok &= expect(canCreateFilesIn(writable), "a folder that accepts files is writable");
	ok &= expect(QDir(writable).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty(),
		"the probe leaves no file behind");
	ok &= expect(!canCreateFilesIn(QDir(base).filePath("probe-missing")), "a folder that does not exist is not writable");
	ok &= expect(writeFile(base + "/probe-file", "f") && !canCreateFilesIn(base + "/probe-file"), "a file is not a writable folder");

// Windows ignores the read-only attribute of a folder when a file is created in it, so only POSIX
// permissions are covered here.
#if !defined(Q_OS_WIN)
	// A process that may write anywhere, such as root, creates the file in a folder without permission too.
	const QString locked = QDir(base).filePath("probe-locked");
	ok &= expect(QDir().mkpath(locked) && QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::ExeOwner),
		"the locked fixture is made");
	if (!QFileInfo(locked).isWritable())
		ok &= expect(!canCreateFilesIn(locked), "a folder without write permission is not writable");
	QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
#endif
	return ok;
}

// The package of an installed Windows copy is the installer. It is checked against the manifest and
// then staged as it is: nothing is extracted, on any platform.
static bool testInstallerStaging(const QString& base) {
	const QString staging = QDir(base).filePath("installer-staging");
	const QByteArray installerBytes = QByteArray("MZ") + QByteArray(5000, 'i');
	UpdatePreparationInput input;
	input.packagePath = QDir(staging).filePath("EGTRAIN-Setup-1.2.3-x64.exe");
	input.stagingRoot = staging;
	input.installer = true;
	input.manifest.version = QStringLiteral("1.2.3");
	input.manifest.assetName = QStringLiteral("EGTRAIN-Setup-1.2.3-x64.exe");
	input.manifest.sha256 = QString::fromLatin1(QCryptographicHash::hash(installerBytes, QCryptographicHash::Sha256).toHex());
	input.manifest.assetSize = installerBytes.size();
	bool ok = expect(QDir().mkpath(staging) && writeFile(input.packagePath, installerBytes), "installer fixture is writable");

	QString error;
	ok &= expect(stageUpdatePackage(input, &error) == input.packagePath && error.isEmpty(), "the staged path of an installer is the installer");

	const auto prepare = [](const UpdatePreparationInput& preparation) {
		UpdatePreparationWorker worker;
		UpdatePreparationResult result;
		QObject::connect(&worker, &UpdatePreparationWorker::finished, &worker,
			[&result](const UpdatePreparationResult& outcome) { result = outcome; });
		worker.prepare(preparation);
		return result;
	};
	const UpdatePreparationResult prepared = prepare(input);
	ok &= expect(prepared.success && prepared.stagedPath == input.packagePath && prepared.error.isEmpty(),
		"an installer that fits the manifest is prepared");
	ok &= expect(QDir(staging).entryList(QDir::AllEntries | QDir::NoDotAndDotDot) == QStringList({"EGTRAIN-Setup-1.2.3-x64.exe"}),
		"preparing an installer extracts nothing");

	input.manifest.sha256 = QString(64, '0');
	const UpdatePreparationResult mismatch = prepare(input);
	ok &= expect(!mismatch.success && mismatch.stagedPath.isEmpty()
			&& mismatch.error == QStringLiteral("The downloaded update failed its SHA-256 check."),
		"an installer that differs from the manifest is not staged");
	return ok;
}

static bool testReadOnlyLocation(const QString& base) {
	bool ok = expect(!isReadOnlyLocation(base), "a writable folder is not a read-only location");
	ok &= expect(isReadOnlyLocation("/private/var/folders/ab/cd/T/AppTranslocation/0123/d/QEGTRAIN.app"),
		"an App Translocation location is a read-only location");
	return ok;
}

#if defined(Q_OS_MACOS)
// Runs a tool without a shell. The tool must exit with 0.
static bool runTool(const QString& program, const QStringList& arguments, QByteArray* output = nullptr) {
	QProcess process;
	process.start(program, arguments);
	if (!process.waitForFinished(120000)) {
		process.kill();
		process.waitForFinished(1000);
		return false;
	}
	if (output)
		*output = process.readAllStandardOutput();
	return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

// A bundle in the layout of a release. Its program is a copy of this test, signed ad hoc, unless
// withProgram is false.
static bool buildFixtureApp(const QString& app, const QString& version, bool withProgram) {
	const QString plistTemplate = QStringLiteral(R"(<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>QEGTRAIN</string>
<key>CFBundleIdentifier</key><string>com.egtrain.simulator</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleVersion</key><string>1</string>
<key>CFBundleShortVersionString</key><string>%1</string>
</dict></plist>
)");
	const QString plist = plistTemplate.arg(version);
	if (!QDir().mkpath(app + "/Contents/MacOS") || !writeFile(app + "/Contents/Info.plist", plist.toUtf8()))
		return false;
	return !withProgram
		|| (QFile::copy(QCoreApplication::applicationFilePath(), app + "/Contents/MacOS/QEGTRAIN")
			&& runTool("/usr/bin/codesign", {"--force", "--sign", "-", app}));
}

// The disk image that tools/release/make_dmg.py builds from the bundle, or an empty string.
static QString buildDiskImage(const QString& app, const QString& outputDirectory) {
	const QString python = QStandardPaths::findExecutable("python3");
	const QString script = QDir::cleanPath(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath() + "/../../../tools/release/make_dmg.py");
	if (python.isEmpty() || !runTool(python, {script, "--version", "1.2.3", "--app", app, "--output", outputDirectory}))
		return {};
	return QDir(outputDirectory).filePath("EGTRAIN-1.2.3-macOS-arm64.dmg");
}

struct MacStageOutcome {
	QString stagedPath;
	QString error;
	bool mounted = true;
};

// Stages the package and reports whether a volume is still mounted below its staging folder. Only
// this test detaches a volume that the staging left behind.
static MacStageOutcome stageMacPackageAt(const QString& stagingRoot, const QString& package, const QString& version) {
	UpdatePreparationInput input;
	input.packagePath = package;
	input.stagingRoot = stagingRoot;
	input.manifest.version = version;
	MacStageOutcome outcome;
	if (!QDir().mkpath(stagingRoot))
		return outcome;
	outcome.stagedPath = stageUpdatePackage(input, &outcome.error);
	QByteArray mounts;
	outcome.mounted = !runTool("/usr/bin/hdiutil", {"info"}, &mounts)
		|| mounts.contains(QFileInfo(stagingRoot).canonicalFilePath().toUtf8());
	runTool("/usr/bin/hdiutil", {"detach", "-force", stagingRoot + "/mnt"});
	return outcome;
}

static bool testMacDiskImage(const QString& base) {
	const QString folder = QDir(base).filePath("macos");
	const QString fixture = folder + "/fixture/QEGTRAIN.app";
	const QString broken = folder + "/broken/QEGTRAIN.app";
	const QString notImage = folder + "/not-an-image.dmg";
	const QString legacyZip = folder + "/legacy.zip";
	bool ok = expect(buildFixtureApp(fixture, "1.2.3", true) && buildFixtureApp(broken, "1.2.3", false)
			&& writeFile(notImage, "not a disk image"),
		"disk image fixtures are writable");
	const QString image = buildDiskImage(fixture, folder + "/image");
	const QString brokenImage = buildDiskImage(broken, folder + "/broken-image");
	ok &= expect(!image.isEmpty() && !brokenImage.isEmpty(), "the release script builds the fixture disk images");
	// The zip package of earlier releases holds QEGTRAIN-Lebanon/QEGTRAIN.app.
	ok &= expect(runTool("/usr/bin/ditto", {fixture, folder + "/legacy/QEGTRAIN-Lebanon/QEGTRAIN.app"})
			&& runTool("/usr/bin/ditto", {"-c", "-k", "--keepParent", folder + "/legacy/QEGTRAIN-Lebanon", legacyZip}),
		"the zip fixture is writable");
	if (!ok)
		return false;

	const MacStageOutcome staged = stageMacPackageAt(folder + "/staged", image, "1.2.3");
	QFile program(fixture + "/Contents/MacOS/QEGTRAIN");
	QFile stagedProgram(staged.stagedPath + "/Contents/MacOS/QEGTRAIN");
	ok &= expect(staged.stagedPath == folder + "/staged/QEGTRAIN.app" && staged.error.isEmpty(),
		"the app is staged from a disk image");
	ok &= expect(program.open(QIODevice::ReadOnly) && stagedProgram.open(QIODevice::ReadOnly)
			&& program.readAll() == stagedProgram.readAll(),
		"the staged app is the app of the disk image");
	ok &= expect(!staged.mounted, "the disk image is detached after staging");

	const QString readOnlyMount = folder + "/read-only";
	ok &= expect(QDir().mkpath(readOnlyMount)
			&& runTool("/usr/bin/hdiutil", {"attach", "-readonly", "-nobrowse", "-noautoopen", "-mountpoint", readOnlyMount, image}),
		"the disk image mounts");
	ok &= expect(isReadOnlyLocation(readOnlyMount + "/QEGTRAIN.app"), "an app on a mounted disk image is in a read-only location");
	ok &= expect(runTool("/usr/bin/hdiutil", {"detach", readOnlyMount}) || runTool("/usr/bin/hdiutil", {"detach", "-force", readOnlyMount}),
		"the disk image detaches");

	const MacStageOutcome mismatch = stageMacPackageAt(folder + "/mismatch", image, "9.9.9");
	ok &= expect(mismatch.stagedPath.isEmpty()
			&& mismatch.error == QStringLiteral("The macOS update app version does not match the release manifest."),
		"a disk image with another app version is rejected");
	ok &= expect(!mismatch.mounted, "the disk image is detached after a version mismatch");

	const MacStageOutcome invalid = stageMacPackageAt(folder + "/invalid", brokenImage, "1.2.3");
	ok &= expect(invalid.stagedPath.isEmpty()
			&& invalid.error == QStringLiteral("The macOS update package has an invalid app bundle."),
		"a disk image whose app has no program is rejected");
	ok &= expect(!invalid.mounted, "the disk image is detached after an invalid app bundle");

	const MacStageOutcome unreadable = stageMacPackageAt(folder + "/unreadable", notImage, "1.2.3");
	ok &= expect(unreadable.stagedPath.isEmpty() && !unreadable.error.isEmpty(), "a file that is no disk image is rejected");
	ok &= expect(!unreadable.mounted, "nothing is mounted after a file that is no disk image");

	const MacStageOutcome legacy = stageMacPackageAt(folder + "/legacy-staged", legacyZip, "1.2.3");
	ok &= expect(legacy.stagedPath == folder + "/legacy-staged/QEGTRAIN.app" && legacy.error.isEmpty(),
		"the app is still staged from a zip package");
	return ok;
}
#endif

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	qRegisterMetaType<UpdatePreparationInput>("UpdatePreparationInput");
	qRegisterMetaType<UpdatePreparationResult>("UpdatePreparationResult");
	QTemporaryDir temp;
	if (!temp.isValid())
		return 1;

	bool ok = true;
#if defined(Q_OS_LINUX)
	UpdatePreparationInput linuxInput;
	linuxInput.stagingRoot = temp.path();
	linuxInput.packagePath = QDir(temp.path()).filePath("QEGTRAIN-linux-x86_64.AppImage");
	linuxInput.currentPath = QDir(temp.path()).filePath("current.AppImage");
	const QByteArray elfContents = QByteArray::fromHex("7f454c46") + "test AppImage";
	ok &= expect(writeFile(linuxInput.packagePath, elfContents)
			&& writeFile(linuxInput.currentPath, elfContents),
		"AppImage fixtures are writable");
	QString stageError;
	const QString linuxStage = stageUpdatePackage(linuxInput, &stageError);
	ok &= expect(!linuxStage.isEmpty() && stageError.isEmpty(),
		"real Linux preparation accepts the production download path");
	ok &= expect(linuxStage != linuxInput.packagePath && QFile::exists(linuxInput.packagePath),
		"AppImage preparation keeps download and staged installation separate");
	ok &= expect(QFileInfo(linuxStage).isExecutable(), "staged AppImage is executable");
#endif
	QByteArray packageContents(3 * 1024 * 1024, 'p');
	for (int offset = 0; offset < packageContents.size(); offset += 4096)
		packageContents[offset] = static_cast<char>(offset % 251);
	const QString packagePath = QDir(temp.path()).filePath(QStringLiteral("package.zip"));
	ok &= expect(writeFile(packagePath, packageContents), "package fixture is writable");
	const QString expectedSha = QString::fromLatin1(
		QCryptographicHash::hash(packageContents, QCryptographicHash::Sha256).toHex());
	const QThread* mainThread = QCoreApplication::instance()->thread();

	const auto baseInput = [&](const QString& sha256) {
		UpdatePreparationInput input;
		input.packagePath = packagePath;
		input.stagingRoot = QDir(temp.path()).filePath(QStringLiteral("staging"));
		input.currentPath = QDir(temp.path()).filePath(QStringLiteral("current"));
		input.manifest.version = QStringLiteral("1.2.3");
		input.manifest.assetName = QStringLiteral("package.zip");
		input.manifest.sha256 = sha256;
		input.manifest.assetSize = packageContents.size();
		return input;
	};

	PreparationOutcome failedHash = runPreparation(
		baseInput(QString(64, '0')), [](const UpdatePreparationInput&, QString*) {
			return QStringLiteral("should-not-stage");
		});
	ok &= expect(!failedHash.result.success && !failedHash.stagerRan,
		"a failed hash never proceeds to staging");
	ok &= expect(failedHash.result.error
			== QStringLiteral("The downloaded update failed its SHA-256 check."),
		"hash failure reports the SHA-256 mismatch");
	ok &= expect(failedHash.result.stagedPath.isEmpty(),
		"a failed hash publishes no staged path");

	const QString stagedPath = QDir(temp.path()).filePath(QStringLiteral("staged"));
	PreparationOutcome prepared = runPreparation(baseInput(expectedSha),
		[stagedPath](const UpdatePreparationInput&, QString*) { return stagedPath; });
	ok &= expect(prepared.result.success && prepared.result.error.isEmpty(),
		"successful preparation completes without an error");
	ok &= expect(prepared.result.stagedPath == stagedPath,
		"successful preparation publishes only the resulting staged path");
	ok &= expect(prepared.stagerRan && prepared.stagerThread
			&& prepared.stagerThread != mainThread,
		"preparation work runs off the application thread");
	ok &= expect(prepared.deliveryThread == mainThread,
		"preparation completion is delivered on the application thread");
	ok &= expect(prepared.heartbeatTicks >= 4,
		"the event loop keeps processing events while preparation runs");

	ok &= testStaleStagingSweep(temp.path());
	ok &= testReadOnlyLocation(temp.path());
	ok &= testCanCreateFilesIn(temp.path());
	ok &= testInstallerStaging(temp.path());
#if defined(Q_OS_MACOS)
	ok &= testMacDiskImage(temp.path());
#endif

	return ok ? 0 : 1;
}
