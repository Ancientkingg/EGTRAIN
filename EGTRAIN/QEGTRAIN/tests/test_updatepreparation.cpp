#include "update/UpdatePreparation.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <functional>

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
		// The stager takes a while, in which the event loop of the application thread keeps running.
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
		qWarning("the preparation thread did not stop");
	delete thread;
	return outcome;
}

static bool setModificationTime(const QString& path, const QDateTime& time) {
	QFile file(path);
	return file.open(QIODevice::ReadWrite | QIODevice::ExistingOnly) && file.setFileTime(time, QFileDevice::FileModificationTime);
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
	const QString script = QDir::cleanPath(QStringLiteral(EGTRAIN_TEST_SOURCE_DIR "/../../../tools/release/make_dmg.py"));
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
#endif

class TestUpdatePreparation : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void stagesAppImage();
	void failedHashNeverStages();
	void preparesOffApplicationThread();
	void sweepsStaleStaging();
	void explainsUnpackFailure();
	void detectsReadOnlyLocation();
	void probesWritableFolder();
	void stagesInstallerAsIs();
	void stagesMacDiskImage();

private:
	UpdatePreparationInput baseInput(const QString& sha256) const;

	QTemporaryDir m_temp;
	QByteArray m_packageContents;
	QString m_packagePath;
};

void TestUpdatePreparation::initTestCase() {
	qRegisterMetaType<UpdatePreparationInput>("UpdatePreparationInput");
	qRegisterMetaType<UpdatePreparationResult>("UpdatePreparationResult");
	QVERIFY(m_temp.isValid());
	m_packageContents = QByteArray(3 * 1024 * 1024, 'p');
	for (int offset = 0; offset < m_packageContents.size(); offset += 4096)
		m_packageContents[offset] = static_cast<char>(offset % 251);
	m_packagePath = QDir(m_temp.path()).filePath(QStringLiteral("package.zip"));
	QVERIFY2(writeFile(m_packagePath, m_packageContents), "package fixture is writable");
}

UpdatePreparationInput TestUpdatePreparation::baseInput(const QString& sha256) const {
	UpdatePreparationInput input;
	input.packagePath = m_packagePath;
	input.stagingRoot = QDir(m_temp.path()).filePath(QStringLiteral("staging"));
	input.currentPath = QDir(m_temp.path()).filePath(QStringLiteral("current"));
	input.manifest.version = QStringLiteral("1.2.3");
	input.manifest.assetName = QStringLiteral("package.zip");
	input.manifest.sha256 = sha256;
	input.manifest.assetSize = m_packageContents.size();
	return input;
}

void TestUpdatePreparation::stagesAppImage() {
#if defined(Q_OS_LINUX)
	UpdatePreparationInput linuxInput;
	linuxInput.stagingRoot = m_temp.path();
	linuxInput.packagePath = QDir(m_temp.path()).filePath("QEGTRAIN-linux-x86_64.AppImage");
	linuxInput.currentPath = QDir(m_temp.path()).filePath("current.AppImage");
	const QByteArray elfContents = QByteArray::fromHex("7f454c46") + "test AppImage";
	// AppImage fixtures are writable.
	QVERIFY(writeFile(linuxInput.packagePath, elfContents));
	QVERIFY(writeFile(linuxInput.currentPath, elfContents));
	QString stageError;
	const QString linuxStage = stageUpdatePackage(linuxInput, &stageError);
	// Real Linux preparation accepts the production download path.
	QVERIFY(!linuxStage.isEmpty());
	QVERIFY(stageError.isEmpty());
	// AppImage preparation keeps download and staged installation separate.
	QVERIFY(linuxStage != linuxInput.packagePath);
	QVERIFY(QFile::exists(linuxInput.packagePath));
	QVERIFY2(QFileInfo(linuxStage).isExecutable(), "staged AppImage is executable");
#else
	QSKIP("An AppImage is staged on Linux only.");
#endif
}

void TestUpdatePreparation::failedHashNeverStages() {
	PreparationOutcome failedHash = runPreparation(
		baseInput(QString(64, '0')), [](const UpdatePreparationInput&, QString*) {
			return QStringLiteral("should-not-stage");
		});
	// A failed hash never proceeds to staging.
	QVERIFY(!failedHash.result.success);
	QVERIFY(!failedHash.stagerRan);
	// A hash failure reports the SHA-256 mismatch.
	QCOMPARE(failedHash.result.error, QStringLiteral("The downloaded update failed its SHA-256 check."));
	QVERIFY2(failedHash.result.stagedPath.isEmpty(),
		"a failed hash publishes no staged path");
}

void TestUpdatePreparation::preparesOffApplicationThread() {
	const QString expectedSha = QString::fromLatin1(
		QCryptographicHash::hash(m_packageContents, QCryptographicHash::Sha256).toHex());
	const QThread* mainThread = QCoreApplication::instance()->thread();
	const QString stagedPath = QDir(m_temp.path()).filePath(QStringLiteral("staged"));
	PreparationOutcome prepared = runPreparation(baseInput(expectedSha),
		[stagedPath](const UpdatePreparationInput&, QString*) { return stagedPath; });
	// A successful preparation completes without an error.
	QVERIFY(prepared.result.success);
	QVERIFY(prepared.result.error.isEmpty());
	// A successful preparation publishes only the resulting staged path.
	QCOMPARE(prepared.result.stagedPath, stagedPath);
	// The preparation work runs off the application thread.
	QVERIFY(prepared.stagerRan);
	QVERIFY(prepared.stagerThread != nullptr);
	QVERIFY(prepared.stagerThread != mainThread);
	QVERIFY2(prepared.deliveryThread == mainThread,
		"preparation completion is delivered on the application thread");
	// The event loop keeps processing events while the preparation runs.
	QCOMPARE_GE(prepared.heartbeatTicks, 4);
}

void TestUpdatePreparation::sweepsStaleStaging() {
	const QDir parent(QDir(m_temp.path()).filePath("sweep"));
	const QString stale = parent.filePath(".qegtrain-update-stale");
	const QString empty = parent.filePath(".qegtrain-update-empty");
	const QString other = parent.filePath("other");
	const QString upper = parent.filePath(".QEGTRAIN-UPDATE-upper");
	const QString file = parent.filePath(".qegtrain-update-file");
	const QString target = parent.filePath("target");
	const QString link = parent.filePath(".qegtrain-update-link");
	// The sweep fixtures are writable.
	QVERIFY(QDir().mkpath(stale + "/extract"));
	QVERIFY(QDir().mkpath(empty));
	QVERIFY(QDir().mkpath(other));
	QVERIFY(QDir().mkpath(target));
	QVERIFY(writeFile(stale + "/package.zip", "p"));
	QVERIFY(writeFile(stale + "/extract/file", "f"));
	QVERIFY(writeFile(other + "/file", "o"));
	QVERIFY(QDir().mkpath(upper));
	QVERIFY(writeFile(upper + "/file", "u"));
	QVERIFY(writeFile(file, "f"));
	QVERIFY(writeFile(target + "/keep", "k"));
	QVERIFY(QFile::link(target, link));

	removeStaleUpdateStaging(parent.path(), std::chrono::minutes(60));
	// A staging folder that changed within the minimum age is kept.
	QVERIFY(QFileInfo::exists(stale));
	QVERIFY(QFileInfo::exists(empty));

	// A minimum age of zero removes a folder whose newest entry changed before now.
	QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(stale + "/extract/file").lastModified() < QDateTime::currentDateTime()
			&& QFileInfo(empty).lastModified() < QDateTime::currentDateTime(),
		1000);
	removeStaleUpdateStaging(parent.path(), std::chrono::minutes(0));
	QVERIFY2(!QFileInfo::exists(stale), "an old staging folder is removed with its content");
	QVERIFY2(!QFileInfo::exists(empty), "an old empty staging folder is removed");
	QVERIFY2(QFileInfo::exists(other + "/file"), "a folder with another name is kept");
	QVERIFY2(QFileInfo::exists(upper + "/file"), "a folder whose name differs in case is kept");
	QVERIFY2(QFileInfo::exists(file), "a file with the name of a staging folder is kept");
	QVERIFY2(QFileInfo::exists(target + "/keep"), "the target of a symbolic link is kept");
// QFile::link makes a shortcut file on Windows, so a symbolic link is only covered elsewhere.
#if !defined(Q_OS_WIN)
	QVERIFY2(QFileInfo(link).isSymLink(), "a symbolic link with the name of a staging folder is kept");
#endif

	// The folder time is now. Only the age of the entries counts.
	const QString oldEntries = parent.filePath(".qegtrain-update-old-entries");
	const QString recentInside = parent.filePath(".qegtrain-update-recent-inside");
	const QDateTime twoHoursAgo = QDateTime::currentDateTime().addSecs(-2 * 3600);
	// The age fixtures are writable.
	QVERIFY(QDir().mkpath(oldEntries));
	QVERIFY(writeFile(oldEntries + "/package.zip", "p"));
	QVERIFY(QDir().mkpath(recentInside + "/extract"));
	QVERIFY(writeFile(recentInside + "/package.zip", "p"));
	QVERIFY(writeFile(recentInside + "/extract/file", "f"));
	// The age fixtures can be dated.
	QVERIFY(setModificationTime(oldEntries + "/package.zip", twoHoursAgo));
	QVERIFY(setModificationTime(recentInside + "/package.zip", twoHoursAgo));
	removeStaleUpdateStaging(parent.path(), std::chrono::minutes(60));
	QVERIFY2(!QFileInfo::exists(oldEntries), "a folder whose entries are old is removed although the folder itself changed");
	QVERIFY2(QFileInfo::exists(recentInside), "a recent entry in a subfolder keeps the folder");

	removeStaleUpdateStaging(QDir(m_temp.path()).filePath("missing"), std::chrono::minutes(0));
	QVERIFY2(!QFileInfo::exists(QDir(m_temp.path()).filePath("missing")), "a missing parent is left alone");
}

// The failure text of the unpacking step says what failed, that nothing changed and how to try again,
// and keeps the technical detail apart from that.
void TestUpdatePreparation::explainsUnpackFailure() {
	const QString timedOut = unpackFailureMessage(QStringLiteral("powershell.exe timed out."));
	QVERIFY2(timedOut.startsWith(QStringLiteral("EGTRAIN could not unpack the downloaded update.")),
		"the unpack failure says what failed");
	QVERIFY2(timedOut.contains(QStringLiteral("Nothing was changed.")), "the unpack failure says that nothing changed");
	QVERIFY2(timedOut.contains(QStringLiteral("Check for Updates")), "the unpack failure says how to try again");
	QVERIFY2(timedOut.endsWith(QStringLiteral("\n\nDetails: powershell.exe timed out.")),
		"the technical detail follows the sentence");
	QVERIFY2(!unpackFailureMessage(QString()).contains(QStringLiteral("Details")), "no detail adds no details line");
}

void TestUpdatePreparation::detectsReadOnlyLocation() {
	QVERIFY2(!isReadOnlyLocation(m_temp.path()), "a writable folder is not a read-only location");
	QVERIFY2(isReadOnlyLocation("/private/var/folders/ab/cd/T/AppTranslocation/0123/d/QEGTRAIN.app"),
		"an App Translocation location is a read-only location");
}

void TestUpdatePreparation::probesWritableFolder() {
	const QString base = m_temp.path();
	const QString writable = QDir(base).filePath("probe-writable");
	QVERIFY2(QDir().mkpath(writable), "probe fixture is writable");
	QVERIFY2(canCreateFilesIn(writable), "a folder that accepts files is writable");
	QVERIFY2(QDir(writable).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty(),
		"the probe leaves no file behind");
	QVERIFY2(!canCreateFilesIn(QDir(base).filePath("probe-missing")), "a folder that does not exist is not writable");
	// A file is not a writable folder.
	QVERIFY(writeFile(base + "/probe-file", "f"));
	QVERIFY(!canCreateFilesIn(base + "/probe-file"));

// Windows ignores the read-only attribute of a folder when a file is created in it, so only POSIX
// permissions are covered here.
#if !defined(Q_OS_WIN)
	// A process that may write anywhere, such as root, creates the file in a folder without permission too.
	const QString locked = QDir(base).filePath("probe-locked");
	const auto unlock = qScopeGuard([&locked]() {
		QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
	});
	// The locked fixture is made.
	QVERIFY(QDir().mkpath(locked));
	QVERIFY(QFile::setPermissions(locked, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
	if (!QFileInfo(locked).isWritable())
		QVERIFY2(!canCreateFilesIn(locked), "a folder without write permission is not writable");
#endif
}

// The package of an installed Windows copy is the installer. It is checked against the manifest and
// then staged as it is: nothing is extracted, on any platform.
void TestUpdatePreparation::stagesInstallerAsIs() {
	const QString staging = QDir(m_temp.path()).filePath("installer-staging");
	const QByteArray installerBytes = QByteArray("MZ") + QByteArray(5000, 'i');
	UpdatePreparationInput input;
	input.packagePath = QDir(staging).filePath("EGTRAIN-Setup-1.2.3-x64.exe");
	input.stagingRoot = staging;
	input.installer = true;
	input.manifest.version = QStringLiteral("1.2.3");
	input.manifest.assetName = QStringLiteral("EGTRAIN-Setup-1.2.3-x64.exe");
	input.manifest.sha256 = QString::fromLatin1(QCryptographicHash::hash(installerBytes, QCryptographicHash::Sha256).toHex());
	input.manifest.assetSize = installerBytes.size();
	// The installer fixture is writable.
	QVERIFY(QDir().mkpath(staging));
	QVERIFY(writeFile(input.packagePath, installerBytes));

	QString error;
	// The staged path of an installer is the installer.
	QCOMPARE(stageUpdatePackage(input, &error), input.packagePath);
	QVERIFY(error.isEmpty());

	const auto prepare = [](const UpdatePreparationInput& preparation) {
		UpdatePreparationWorker worker;
		UpdatePreparationResult result;
		QObject::connect(&worker, &UpdatePreparationWorker::finished, &worker,
			[&result](const UpdatePreparationResult& outcome) { result = outcome; });
		worker.prepare(preparation);
		return result;
	};
	const UpdatePreparationResult prepared = prepare(input);
	// An installer that fits the manifest is prepared.
	QVERIFY(prepared.success);
	QCOMPARE(prepared.stagedPath, input.packagePath);
	QVERIFY(prepared.error.isEmpty());
	// Preparing an installer extracts nothing.
	QCOMPARE(QDir(staging).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), QStringList({"EGTRAIN-Setup-1.2.3-x64.exe"}));

	input.manifest.sha256 = QString(64, '0');
	const UpdatePreparationResult mismatch = prepare(input);
	// An installer that differs from the manifest is not staged.
	QVERIFY(!mismatch.success);
	QVERIFY(mismatch.stagedPath.isEmpty());
	QCOMPARE(mismatch.error, QStringLiteral("The downloaded update failed its SHA-256 check."));
}

void TestUpdatePreparation::stagesMacDiskImage() {
#if defined(Q_OS_MACOS)
	const QString folder = QDir(m_temp.path()).filePath("macos");
	const QString fixture = folder + "/fixture/QEGTRAIN.app";
	const QString broken = folder + "/broken/QEGTRAIN.app";
	const QString notImage = folder + "/not-an-image.dmg";
	const QString legacyZip = folder + "/legacy.zip";
	// The disk image fixtures are writable.
	QVERIFY(buildFixtureApp(fixture, "1.2.3", true));
	QVERIFY(buildFixtureApp(broken, "1.2.3", false));
	QVERIFY(writeFile(notImage, "not a disk image"));
	const QString image = buildDiskImage(fixture, folder + "/image");
	const QString brokenImage = buildDiskImage(broken, folder + "/broken-image");
	// The release script builds the fixture disk images.
	QVERIFY(!image.isEmpty());
	QVERIFY(!brokenImage.isEmpty());
	// The zip package of earlier releases holds QEGTRAIN-Lebanon/QEGTRAIN.app.
	// The zip fixture is writable.
	QVERIFY(runTool("/usr/bin/ditto", {fixture, folder + "/legacy/QEGTRAIN-Lebanon/QEGTRAIN.app"}));
	QVERIFY(runTool("/usr/bin/ditto", {"-c", "-k", "--keepParent", folder + "/legacy/QEGTRAIN-Lebanon", legacyZip}));

	const MacStageOutcome staged = stageMacPackageAt(folder + "/staged", image, "1.2.3");
	QFile program(fixture + "/Contents/MacOS/QEGTRAIN");
	QFile stagedProgram(staged.stagedPath + "/Contents/MacOS/QEGTRAIN");
	// The app is staged from a disk image.
	QCOMPARE(staged.stagedPath, folder + "/staged/QEGTRAIN.app");
	QVERIFY(staged.error.isEmpty());
	// The staged app is the app of the disk image.
	QVERIFY(program.open(QIODevice::ReadOnly));
	QVERIFY(stagedProgram.open(QIODevice::ReadOnly));
	QVERIFY(program.readAll() == stagedProgram.readAll());
	QVERIFY2(!staged.mounted, "the disk image is detached after staging");

	const QString readOnlyMount = folder + "/read-only";
	// The disk image mounts.
	QVERIFY(QDir().mkpath(readOnlyMount));
	QVERIFY(runTool("/usr/bin/hdiutil", {"attach", "-readonly", "-nobrowse", "-noautoopen", "-mountpoint", readOnlyMount, image}));
	// A failed check still detaches the image.
	auto detach = qScopeGuard([&readOnlyMount]() { runTool("/usr/bin/hdiutil", {"detach", "-force", readOnlyMount}); });
	QVERIFY2(isReadOnlyLocation(readOnlyMount + "/QEGTRAIN.app"), "an app on a mounted disk image is in a read-only location");
	detach.dismiss();
	QVERIFY2(runTool("/usr/bin/hdiutil", {"detach", readOnlyMount}) || runTool("/usr/bin/hdiutil", {"detach", "-force", readOnlyMount}),
		"the disk image detaches");

	const MacStageOutcome mismatch = stageMacPackageAt(folder + "/mismatch", image, "9.9.9");
	// A disk image with another app version is rejected.
	QVERIFY(mismatch.stagedPath.isEmpty());
	QCOMPARE(mismatch.error, QStringLiteral("The macOS update app version does not match the release manifest."));
	QVERIFY2(!mismatch.mounted, "the disk image is detached after a version mismatch");

	const MacStageOutcome invalid = stageMacPackageAt(folder + "/invalid", brokenImage, "1.2.3");
	// A disk image whose app has no program is rejected.
	QVERIFY(invalid.stagedPath.isEmpty());
	QCOMPARE(invalid.error, QStringLiteral("The macOS update package has an invalid app bundle."));
	QVERIFY2(!invalid.mounted, "the disk image is detached after an invalid app bundle");

	const MacStageOutcome unreadable = stageMacPackageAt(folder + "/unreadable", notImage, "1.2.3");
	// A file that is no disk image is rejected.
	QVERIFY(unreadable.stagedPath.isEmpty());
	QVERIFY(!unreadable.error.isEmpty());
	QVERIFY2(!unreadable.mounted, "nothing is mounted after a file that is no disk image");

	const MacStageOutcome legacy = stageMacPackageAt(folder + "/legacy-staged", legacyZip, "1.2.3");
	// The app is still staged from a zip package.
	QCOMPARE(legacy.stagedPath, folder + "/legacy-staged/QEGTRAIN.app");
	QVERIFY(legacy.error.isEmpty());
#else
	QSKIP("A disk image is staged on macOS only.");
#endif
}

QTEST_GUILESS_MAIN(TestUpdatePreparation)
#include "test_updatepreparation.moc"
