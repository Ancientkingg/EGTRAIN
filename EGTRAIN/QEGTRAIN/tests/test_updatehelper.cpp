#include "update/UpdateRecord.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <functional>
#include <optional>

static bool writeFile(const QString& path, const QByteArray& contents) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

static QByteArray readFile(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return file.readAll();
}

static const QString kProbeSuffix =
#if defined(Q_OS_WIN)
	QStringLiteral(".exe");
#else
	QString();
#endif

// Copies the probe program into directory under the given name and gives it a behavior file.
static bool writeInstallation(const QString& directory, const QString& program, const QString& label,
	const QString& log, const QString& behavior, const QString& name = QStringLiteral("app")) {
	if (!QDir().mkpath(directory))
		return false;
	const QString app = QDir(directory).filePath(name + kProbeSuffix);
	return QFile::copy(program, app)
		&& writeFile(QDir(directory).filePath("behavior.txt"),
			QStringLiteral("label=%1\nlog=%2\n%3\n").arg(label, log, behavior).toUtf8());
}

static QStringList logLines(const QString& log) {
	QStringList lines = QString::fromUtf8(readFile(log)).split('\n');
	lines.removeAll(QString());
	return lines;
}

// How long a line of a probe may take to reach the log.
constexpr int kLogWaitMs = 10000;

// Window for the scenarios in which the new version fails at once. The helper returns as soon
// as the process has ended, so a long window only gives a slow machine room.
constexpr int kFailureObserveMs = 10000;

// The staging folder of a scenario. Like the one of SelfUpdater it holds the staged installation
// and a copy of the helper, which is started from there.
static QString stagingFolder(const QString& root) {
	return QDir(root).filePath(".qegtrain-update-test");
}

static QString stagePath(const QString& root) {
	return QDir(stagingFolder(root)).filePath("stage");
}

// The helper can delete its folder only where a running program can be deleted.
static bool stagingIsCleared(const QString& root, const QString& helper) {
	const QDir staging(stagingFolder(root));
#if defined(Q_OS_WIN)
	return staging.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)
		== QStringList{QFileInfo(helper).fileName()};
#else
	(void)helper;
	return !staging.exists();
#endif
}

// The directory that a probe logged as its working directory, or an empty string.
static QString loggedDirectory(const QString& log, const QString& label) {
	const QString prefix = label + " cwd ";
	for (const QString& line : logLines(log))
		if (line.startsWith(prefix))
			return QFileInfo(line.mid(prefix.size())).canonicalFilePath();
	return {};
}

static bool ranIn(const QString& log, const QString& label, const QString& directory) {
	const QString logged = loggedDirectory(log, label);
	return !logged.isEmpty() && logged == QFileInfo(directory).canonicalFilePath();
}

// The version that the scenarios install, and the file in which the helper is asked to leave the
// record of the update. The file is in the root of the scenario, outside the installation, the
// staging folder and the backup.
static const SemanticVersion kUpdatedVersion{9, 8, 7};

static QString recordPath(const QString& root) {
	return QDir(root).filePath("record.txt");
}

// The options that SelfUpdater passes to the helper, for the record of a scenario.
static QStringList recordArguments(const QString& root) {
	return updateRecordArguments(kUpdatedVersion, recordPath(root));
}

// The record that the helper left, read with the reader of the application. The bytes are checked as
// well, because the reader also accepts CRLF and a missing last line feed.
static void recordIsWritten(const QString& path, UpdateOutcome outcome = UpdateOutcome::RolledBack) {
	QVERIFY2(QFileInfo::exists(path), "the helper leaves a record");
	QFile file(path);
	const QByteArray bytes = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
	const std::optional<UpdateRecord> record = parseUpdateRecord(bytes);
	// The application reads the version of the record.
	QVERIFY(record.has_value());
	QVERIFY(record->version == kUpdatedVersion);
	// The application reads the outcome of the record.
	QCOMPARE(record->outcome, outcome);
	// The record holds the current UTC time.
	QCOMPARE_LE(qAbs(record->time.secsTo(QDateTime::currentDateTimeUtc())), 300);
	QString name = QStringLiteral("rolled-back");
	if (outcome == UpdateOutcome::NotApplied)
		name = QStringLiteral("not-applied");
	else if (outcome == UpdateOutcome::Applied)
		name = QStringLiteral("applied");
	const QRegularExpression format(QRegularExpression::anchoredPattern(
		QStringLiteral("outcome=%1\nversion=9\\.8\\.7\ntime=\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z\n").arg(name)));
	QVERIFY2(format.match(QString::fromLatin1(bytes)).hasMatch(), "the record is three lines, each ended by one line feed");
}

struct HelperRun {
	int exitCode = -1;
	qint64 milliseconds = 0;
};

// Runs a copy of the helper that is placed in the staging folder of root, as the application does.
// A non-empty whileRunning is called once, delayMs after the helper has started.
static HelperRun runHelperProcess(const QString& helper, const QString& root, const QStringList& arguments,
	int delayMs = 0, const std::function<void()>& whileRunning = {}) {
	const QString helperCopy = QDir(stagingFolder(root)).filePath(QFileInfo(helper).fileName());
	HelperRun result;
	if (!QFile::copy(helper, helperCopy))
		return result;
	QProcess process;
	process.setProgram(helperCopy);
	process.setArguments(arguments);
	QElapsedTimer timer;
	timer.start();
	process.start();
	if (whileRunning) {
		// Not a wait for the helper: until whileRunning is called, the renames of the helper are meant to fail.
		QThread::msleep(static_cast<unsigned long>(delayMs));
		whileRunning();
	}
	if (!process.waitForFinished(60000) || process.exitStatus() != QProcess::NormalExit)
		return result;
	result.milliseconds = timer.elapsed();
	result.exitCode = process.exitCode();
	return result;
}

// One update of root/install by the stage in the staging folder, with root/install.egtrain-old as
// the backup. The helper runs from a copy in the staging folder. extraArguments follow --observe-ms.
static HelperRun runHelper(const QString& helper, const QString& root, int observeMs,
	const QStringList& extraArguments, int delayMs = 0, const std::function<void()>& whileRunning = {}) {
	const QDir directory(root);
	QStringList arguments = {"--parent-pid", "0", "--current", directory.filePath("install"),
		"--staged", stagePath(root),
		"--backup", directory.filePath("install.egtrain-old"),
		"--launch", directory.filePath("install/app" + kProbeSuffix),
		"--observe-ms", QString::number(observeMs)};
	arguments += extraArguments;
	return runHelperProcess(helper, root, arguments, delayMs, whileRunning);
}

// The installer of an installer-managed update: a probe called setup in the staging folder.
static QString installerPath(const QString& root) {
	return QDir(stagingFolder(root)).filePath("setup" + kProbeSuffix);
}

// One update in installer mode: the installer runs and then root/install/app starts. The helper runs
// from a copy in the staging folder. extraArguments follow --observe-ms.
static HelperRun runInstallerHelper(const QString& helper, const QString& root, int observeMs,
	const QStringList& extraArguments) {
	QStringList arguments = {"--parent-pid", "0", "--installer", installerPath(root),
		"--launch", QDir(root).filePath("install/app" + kProbeSuffix),
		"--observe-ms", QString::number(observeMs)};
	arguments += extraArguments;
	return runHelperProcess(helper, root, arguments);
}

static bool installationIs(const QString& root, const QString& directory, const char* label) {
	return readFile(QDir(root).filePath(directory + "/behavior.txt")).contains(label);
}

#if defined(Q_OS_WIN)
// The helper does not watch the version that it starts after a rollback or an installer, so that version can
// still run when the helper has ended. Windows keeps the program file of a running process from being opened
// for writing, so this is true once that version has ended and its files are free.
static bool startedVersionHasEnded(const QString& root) {
	QFile program(QDir(root).filePath("install/app.exe"));
	return program.open(QIODevice::ReadWrite);
}
#endif

// A backup in a folder that does not exist: the first rename of the installation fails until the folder is
// made, as it does on Windows while a file of the installation is locked.
static QString unavailableBackup(const QString& root) {
	return QDir(root).filePath("unavailable/install.egtrain-old");
}

// The fixtures of an installer-mode update in root: the installed application and, in the staging
// folder, the installer. Both log whether the record exists when they start.
static bool writeInstallerFixtures(const QString& root, const QString& program, const QString& log,
	const QString& installerBehavior) {
	const QString seesRecord = QStringLiteral("record=") + recordPath(root);
	return writeInstallation(QDir(root).filePath("install"), program, "app", log, "mode=exit\ncode=0\n" + seesRecord)
		&& writeInstallation(stagingFolder(root), program, "setup", log, installerBehavior + "\n" + seesRecord,
			QStringLiteral("setup"));
}

// The installation is what writeInstallerFixtures made of it, and no backup was taken.
static bool installationIsUntouched(const QString& root) {
	const QStringList files = QDir(QDir(root).filePath("install"))
								  .entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
	return files == QStringList({"app" + kProbeSuffix, "behavior.txt"})
		&& !QFileInfo::exists(QDir(root).filePath("install.egtrain-old"));
}

// The application was started once, and the record was there when it started.
static bool applicationStartedOnceWithRecord(const QString& log) {
	const QStringList lines = logLines(log);
	return lines.count("app started") == 1 && lines.contains("app sees record") && !lines.contains("app sees no record");
}

// The record of an installer-mode update: the outcome, the version, a current UTC time and the details.
// The reader of a rolled-back update must not take it.
static void installerRecordIs(const QString& path, const QString& outcome, const QString& details) {
	const QByteArray bytes = readFile(path);
	const QRegularExpression format(QRegularExpression::anchoredPattern(
		QStringLiteral("outcome=%1\nversion=9\\.8\\.7\ntime=(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z)\n%2")
			.arg(outcome, details)));
	const QRegularExpressionMatch match = format.match(QString::fromLatin1(bytes));
	QVERIFY2(match.hasMatch(), "the installer record holds the outcome, the version, the time and the details");
	const qint64 age = qAbs(QDateTime::fromString(match.captured(1), Qt::ISODate).secsTo(QDateTime::currentDateTimeUtc()));
	// The installer record holds the current UTC time.
	QCOMPARE_LE(age, 300);
	QVERIFY2(!parseUpdateRecord(bytes), "the reader of a rolled-back update does not take an installer record");
}

// The options of the update of root/install by the stage in the staging folder.
static QStringList transactionArguments(const QString& root) {
	const QDir directory(root);
	return {"--current", directory.filePath("install"), "--staged", stagePath(root),
		"--backup", directory.filePath("install.egtrain-old")};
}

class TestUpdateHelper : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void installsStagedFile();
	void rollsBackWhenRelaunchFails();
	void installsStagedDirectory();
	void earlyFailureRollsBack();
	void runningVersionIsInstalled();
	void cleanQuickExitIsInstalled();
	void renameIsRetried();
	void renameKeepsFailing();
	void unwritableRecordStillRollsBack();
	void noRecordWithoutValidOptions_data();
	void noRecordWithoutValidOptions();
	void installerRunsThenApplicationStarts();
	void failedInstallerStartsApplication_data();
	void failedInstallerStartsApplication();
	void missingInstaller();
	void installerSucceedsButApplicationDoesNotStart();
	void installerOptionsExcludeTheTransaction_data();
	void installerOptionsExcludeTheTransaction();
	void crashRollsBack();
	void missingDllRollsBack();

private:
	// A folder of the scenario in the temporary folder.
	QString scenarioRoot(const char* name) const;

	QTemporaryDir m_temp;
	QString m_helper;
	// The probe that stands in for an installed application.
	QString m_probe;
	// A copy of the probe that exits with 0 at once, for the updates that start the application.
	QString m_application;
#if defined(Q_OS_WIN)
	// A probe that needs the DLL next to it.
	QString m_dllProbe;
	QString m_dll;
#endif
};

void TestUpdateHelper::initTestCase() {
	QVERIFY(m_temp.isValid());
	m_helper = QStringLiteral(EGTRAIN_UPDATE_HELPER);
	m_probe = QStringLiteral(EGTRAIN_UPDATE_PROBE);
	QVERIFY2(QFileInfo(m_helper).isFile(), "the update helper exists");
	QVERIFY2(QFileInfo(m_probe).isFile(), "the update probe exists");
#if defined(Q_OS_WIN)
	m_dllProbe = QStringLiteral(EGTRAIN_UPDATE_DLLPROBE);
	m_dll = QStringLiteral(EGTRAIN_UPDATE_PROBE_DLL);
	QVERIFY2(QFileInfo(m_dllProbe).isFile(), "the DLL probe exists");
	QVERIFY2(QFileInfo(m_dll).isFile(), "the DLL of the probe exists");
#endif
	const QString application = QDir(m_temp.path()).filePath("application");
	QVERIFY2(writeInstallation(application, m_probe, "application", QString(), "mode=exit\ncode=0"),
		"the application to start is written");
	m_application = QDir(application).filePath("app" + kProbeSuffix);
}

QString TestUpdateHelper::scenarioRoot(const char* name) const {
	const QString root = QDir(m_temp.path()).filePath(name);
	QDir().mkpath(root);
	return root;
}

// A staged file replaces the current one and the current one is kept as the backup. A stage that is
// missing leaves the current file as it is.
void TestUpdateHelper::installsStagedFile() {
	const QString current = QDir(m_temp.path()).filePath("current.bin");
	const QString staged = QDir(m_temp.path()).filePath("staged.bin");
	const QString backup = QDir(m_temp.path()).filePath("backup.bin");
	QVERIFY2(writeFile(current, "old"), "fixture current file is writable");
	QVERIFY2(writeFile(staged, "new"), "fixture staged file is writable");
	const QStringList successArguments = {
		"--parent-pid", "0", "--current", current, "--staged", staged,
		"--backup", backup, "--launch", m_application};
	// The helper installs a staged file.
	QCOMPARE(QProcess::execute(m_helper, successArguments), 0);
	// The new file is active after helper success.
	QCOMPARE(readFile(current), QByteArray("new"));
	// A successful helper retains a recoverable backup.
	QCOMPARE(readFile(backup), QByteArray("old"));

	const QString missingStage = QDir(m_temp.path()).filePath("missing.bin");
	const QString rollbackBackup = QDir(m_temp.path()).filePath("rollback.bin");
	const int missingStageCode = QProcess::execute(m_helper,
		{"--parent-pid", "0", "--current", current, "--staged", missingStage,
			"--backup", rollbackBackup, "--launch", m_application});
	// The helper rejects a missing staged file.
	QCOMPARE_NE(missingStageCode, 0);
	// A failed helper leaves the active file untouched.
	QCOMPARE(readFile(current), QByteArray("new"));
}

void TestUpdateHelper::rollsBackWhenRelaunchFails() {
	const QString current = QDir(m_temp.path()).filePath("current.bin");
	const QString rollbackStage = QDir(m_temp.path()).filePath("rollback-stage.bin");
	const QString launchFailureBackup = QDir(m_temp.path()).filePath("launch-failure.bin");
	// The rollback fixture is writable.
	QVERIFY(writeFile(current, "old-again"));
	QVERIFY(writeFile(rollbackStage, "new-again"));
	const QString relaunchRecord = QDir(m_temp.path()).filePath("relaunch-record.txt");
	QStringList relaunchArguments = {"--parent-pid", "0", "--current", current, "--staged", rollbackStage,
		"--backup", launchFailureBackup, "--launch", QDir(m_temp.path()).filePath("missing-launch")};
	relaunchArguments += updateRecordArguments(kUpdatedVersion, relaunchRecord);
	// The helper rolls back when relaunch fails.
	QCOMPARE_NE(QProcess::execute(m_helper, relaunchArguments), 0);
	// A launch failure restores the previous file.
	QCOMPARE(readFile(current), QByteArray("old-again"));
	recordIsWritten(relaunchRecord);
}

void TestUpdateHelper::installsStagedDirectory() {
	const QString currentDir = QDir(m_temp.path()).filePath("current-dir");
	const QString stagedDir = QDir(m_temp.path()).filePath("staged-dir");
	const QString backupDir = QDir(m_temp.path()).filePath("backup-dir");
	QDir().mkpath(currentDir);
	QDir().mkpath(stagedDir);
	QVERIFY2(writeFile(QDir(currentDir).filePath("marker"), "old"), "directory fixture current is writable");
	QVERIFY2(writeFile(QDir(stagedDir).filePath("marker"), "new"), "directory fixture staged is writable");
	QProcess directoryUpdate;
	directoryUpdate.setProgram(m_helper);
	directoryUpdate.setArguments({"--parent-pid", "0", "--current", currentDir, "--staged", stagedDir,
		"--backup", backupDir, "--launch", m_application});
	directoryUpdate.setWorkingDirectory(currentDir);
	directoryUpdate.start();
	directoryUpdate.waitForFinished();
	// The helper installs a staged directory.
	QCOMPARE(directoryUpdate.exitStatus(), QProcess::NormalExit);
	QCOMPARE(directoryUpdate.exitCode(), 0);
	// The new directory is active after helper success.
	QCOMPARE(readFile(QDir(currentDir).filePath("marker")), QByteArray("new"));
}

// A new version that exits with a failure code is replaced by the previous installation, and the record
// that the update was applied, which the new version found at its start, by the record of the rollback.
void TestUpdateHelper::earlyFailureRollsBack() {
	const QString root = scenarioRoot("early-failure");
	const QString log = QDir(root).filePath("log.txt");
	// The early failure fixtures are writable.
	QVERIFY(writeInstallation(QDir(root).filePath("install"), m_probe, "old", log,
		QStringLiteral("mode=exit\ncode=0\nrecord=") + recordPath(root)));
	QVERIFY(writeInstallation(stagePath(root), m_probe, "new", log,
		QStringLiteral("mode=exit\ncode=3\nrecord=") + recordPath(root)));
	const HelperRun run = runHelper(m_helper, root, kFailureObserveMs, recordArguments(root));
	// The helper fails when the new version exits with an error.
	QCOMPARE_NE(run.exitCode, 0);
	QCOMPARE_NE(run.exitCode, -1);
	QVERIFY2(installationIs(root, "install", "label=old"), "early failure restores the previous installation");
	QVERIFY2(!QFileInfo::exists(QDir(root).filePath("install.egtrain-old")), "early failure consumes the backup");
#if !defined(Q_OS_WIN)
	// On Windows a scanner can hold the crashed files, so the best-effort removal may lag.
	QVERIFY2(!QFileInfo::exists(QDir(root).filePath("install.egtrain-old.failed")),
		"early failure removes the failed installation");
#endif
	QTRY_VERIFY2_WITH_TIMEOUT(logLines(log).contains("old started"), "early failure starts the previous version again",
		kLogWaitMs);
	QVERIFY2(logLines(log).contains("new sees record"), "the new version finds a record at its start");
	recordIsWritten(recordPath(root));
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(logLines(log).contains("old sees record"), "the record exists before the previous version starts");
	QVERIFY2(stagingIsCleared(root, m_helper), "the staging folder is removed after a rollback");
	const QString install = QDir(root).filePath("install");
	QVERIFY2(ranIn(log, "new", install), "the new version starts in the installation directory");
	QVERIFY2(ranIn(log, "old", install), "the restored version starts in the installation directory");
	const QStringList lines = logLines(log);
	// The new version ran before the previous one.
	QCOMPARE_GE(lines.indexOf("new started"), 0);
	QCOMPARE_LT(lines.indexOf("new started"), lines.indexOf("old started"));
#if defined(Q_OS_WIN)
	QTRY_VERIFY2_WITH_TIMEOUT(startedVersionHasEnded(root), "the restored version has ended", kLogWaitMs);
#endif
}

// A new version that keeps running is installed, finds the record that the update was applied at its
// start, and the helper ends after the observation window.
void TestUpdateHelper::runningVersionIsInstalled() {
	const QString root = scenarioRoot("running");
	const QString log = QDir(root).filePath("log.txt");
	const int observeMs = 1500;
	// The running version fixtures are writable.
	QVERIFY(writeInstallation(QDir(root).filePath("install"), m_probe, "old", log, "mode=exit\ncode=0"));
	QVERIFY(writeInstallation(stagePath(root), m_probe, "new", log,
		QStringLiteral("mode=sleep\nms=6000\nrecord=") + recordPath(root)));
	const HelperRun run = runHelper(m_helper, root, observeMs, recordArguments(root));
	// The helper succeeds while the new version keeps running.
	QCOMPARE(run.exitCode, 0);
	// The helper watches the new version for the whole window.
	QCOMPARE_GE(run.milliseconds, observeMs - 100);
	// The helper does not wait for the new version to end.
	QCOMPARE_LT(run.milliseconds, observeMs + 4000);
	QVERIFY2(installationIs(root, "install", "label=new"), "running version stays installed");
	QVERIFY2(installationIs(root, "install.egtrain-old", "label=old"), "running version keeps the backup");
	QTRY_VERIFY2_WITH_TIMEOUT(logLines(log).contains("new done"), "running version finishes", kLogWaitMs);
	QVERIFY2(ranIn(log, "new", QDir(root).filePath("install")), "running version starts in the installation directory");
	QVERIFY2(stagingIsCleared(root, m_helper), "the staging folder is removed after the observation window");
	QVERIFY2(!logLines(log).contains("old started"), "previous version is not started again");
	QVERIFY2(logLines(log).contains("new sees record"), "the record exists before the new version starts");
	recordIsWritten(recordPath(root), UpdateOutcome::Applied);
#if defined(Q_OS_WIN)
	if (QTest::currentTestFailed())
		return;
	QTRY_VERIFY2_WITH_TIMEOUT(startedVersionHasEnded(root), "the new version has ended", kLogWaitMs);
#endif
}

// A new version that exits with code 0 right away counts as started and ends the helper early.
void TestUpdateHelper::cleanQuickExitIsInstalled() {
	const QString root = scenarioRoot("quick-exit");
	const QString log = QDir(root).filePath("log.txt");
	const int observeMs = 6000;
	// The quick exit fixtures are writable.
	QVERIFY(writeInstallation(QDir(root).filePath("install"), m_probe, "old", log, "mode=exit\ncode=0"));
	QVERIFY(writeInstallation(stagePath(root), m_probe, "new", log, "mode=exit\ncode=0"));
	const HelperRun run = runHelper(m_helper, root, observeMs, recordArguments(root));
	// The helper succeeds when the new version exits cleanly.
	QCOMPARE(run.exitCode, 0);
	// The helper does not sit out the window after a clean exit.
	QCOMPARE_LT(run.milliseconds, observeMs - 500);
	QVERIFY2(installationIs(root, "install", "label=new"), "clean exit stays installed");
	QVERIFY2(installationIs(root, "install.egtrain-old", "label=old"), "clean exit keeps the backup");
	QVERIFY2(!logLines(log).contains("old started"), "previous version is not started after a clean exit");
	recordIsWritten(recordPath(root), UpdateOutcome::Applied);
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(ranIn(log, "new", QDir(root).filePath("install")), "clean exit starts in the installation directory");
	QVERIFY2(stagingIsCleared(root, m_helper), "the staging folder is removed after a clean exit");
}

// A first rename that fails for a while is tried again, and the update ends installed.
void TestUpdateHelper::renameIsRetried() {
	const QString root = scenarioRoot("rename-retried");
	const QString log = QDir(root).filePath("log.txt");
	const int observeMs = 6000;
	const int delayMs = 600;
	// The retry fixtures are writable.
	QVERIFY(writeInstallation(QDir(root).filePath("install"), m_probe, "old", log, "mode=exit\ncode=0"));
	QVERIFY(writeInstallation(stagePath(root), m_probe, "new", log, "mode=exit\ncode=0"));
	const HelperRun run = runHelper(m_helper, root, observeMs,
		QStringList{"--backup", unavailableBackup(root)} + recordArguments(root), delayMs,
		[&root]() { QDir().mkpath(QDir(root).filePath("unavailable")); });
	// The helper succeeds when the first rename works after a few attempts.
	QCOMPARE(run.exitCode, 0);
	// The helper waited for the rename instead of giving up.
	QCOMPARE_GE(run.milliseconds, delayMs - 100);
	QVERIFY2(installationIs(root, "install", "label=new"), "the new version is installed after the retries");
	QVERIFY2(readFile(unavailableBackup(root) + "/behavior.txt").contains("label=old"),
		"the previous installation is kept as the backup");
	QVERIFY2(!logLines(log).contains("old started"), "the previous version is not started");
	recordIsWritten(recordPath(root), UpdateOutcome::Applied);
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(stagingIsCleared(root, m_helper), "the staging folder is removed after the retries");
}

// A first rename that keeps failing leaves the previous installation as it was and starts it.
void TestUpdateHelper::renameKeepsFailing() {
	const QString root = scenarioRoot("rename-fails");
	const QString log = QDir(root).filePath("log.txt");
	// The failing rename fixtures are writable.
	QVERIFY(writeInstallation(QDir(root).filePath("install"), m_probe, "old", log,
		QStringLiteral("mode=exit\ncode=0\nrecord=") + recordPath(root)));
	QVERIFY(writeInstallation(stagePath(root), m_probe, "new", log, "mode=exit\ncode=0"));
	const HelperRun run = runHelper(m_helper, root, kFailureObserveMs,
		QStringList{"--backup", unavailableBackup(root)} + recordArguments(root));
	// The helper ends with code 1 when the installation cannot be renamed.
	QCOMPARE(run.exitCode, 1);
	QVERIFY2(installationIs(root, "install", "label=old"), "the previous installation stays in place");
	QVERIFY2(!QFileInfo::exists(QDir(root).filePath("unavailable")), "no backup is made");
	QTRY_VERIFY2_WITH_TIMEOUT(logLines(log).contains("old started"), "the previous version is started", kLogWaitMs);
	QVERIFY2(!logLines(log).contains("new started"), "the new version is not started");
	recordIsWritten(recordPath(root), UpdateOutcome::NotApplied);
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(logLines(log).contains("old sees record"), "the record exists before the previous version starts");
	QVERIFY2(stagingIsCleared(root, m_helper), "the staging folder of the failed attempt is removed");
	QVERIFY2(ranIn(log, "old", QDir(root).filePath("install")), "the previous version starts in the installation directory");
#if defined(Q_OS_WIN)
	QTRY_VERIFY2_WITH_TIMEOUT(startedVersionHasEnded(root), "the previous version has ended", kLogWaitMs);
#endif
}

// A crash at startup is detected and does not leave an error dialog open.
void TestUpdateHelper::crashRollsBack() {
#if defined(Q_OS_WIN)
	const QString root = scenarioRoot("crash");
	const QString log = QDir(root).filePath("log.txt");
	// The crash fixtures are writable.
	QVERIFY(writeInstallation(QDir(root).filePath("install"), m_probe, "old", log, "mode=exit\ncode=0"));
	QVERIFY(writeInstallation(stagePath(root), m_probe, "new", log, "mode=crash"));
	const HelperRun run = runHelper(m_helper, root, kFailureObserveMs, recordArguments(root));
	// The helper fails when the new version crashes.
	QCOMPARE_NE(run.exitCode, 0);
	QCOMPARE_NE(run.exitCode, -1);
	QVERIFY2(installationIs(root, "install", "label=old"), "crash restores the previous installation");
	QTRY_VERIFY2_WITH_TIMEOUT(logLines(log).contains("old started"), "crash starts the previous version again", kLogWaitMs);
	recordIsWritten(recordPath(root));
	if (QTest::currentTestFailed())
		return;
	QTRY_VERIFY2_WITH_TIMEOUT(startedVersionHasEnded(root), "the previous version has ended", kLogWaitMs);
#else
	QSKIP("The probe crashes only on Windows.");
#endif
}

// A package without an imported DLL fails in the loader and is replaced by the previous installation.
void TestUpdateHelper::missingDllRollsBack() {
#if defined(Q_OS_WIN)
	const QString root = scenarioRoot("missing-dll");
	const QDir directory(root);
	const QString dllName = QFileInfo(m_dll).fileName();
	// The missing DLL fixtures are writable.
	QVERIFY(QDir().mkpath(directory.filePath("install")));
	QVERIFY(QDir().mkpath(stagePath(root)));
	QVERIFY(QFile::copy(m_dllProbe, directory.filePath("install/app.exe")));
	QVERIFY(QFile::copy(m_dll, directory.filePath("install/" + dllName)));
	QVERIFY(QFile::copy(m_dllProbe, QDir(stagePath(root)).filePath("app.exe")));
	QVERIFY(writeFile(QDir(stagePath(root)).filePath("new-marker"), "new"));
	const HelperRun run = runHelper(m_helper, root, kFailureObserveMs, recordArguments(root));
	// The helper fails when the new version cannot load a DLL.
	QCOMPARE_NE(run.exitCode, 0);
	QCOMPARE_NE(run.exitCode, -1);
	QVERIFY2(QFileInfo::exists(directory.filePath("install/" + dllName)), "loader failure restores the previous installation");
	QVERIFY2(!QFileInfo::exists(directory.filePath("install/new-marker")), "loader failure removes the new installation");
	QVERIFY2(!QFileInfo::exists(directory.filePath("install.egtrain-old")), "loader failure consumes the backup");
	recordIsWritten(recordPath(root));
	if (QTest::currentTestFailed())
		return;
	QTRY_VERIFY2_WITH_TIMEOUT(startedVersionHasEnded(root), "the previous version has ended", kLogWaitMs);
#else
	QSKIP("Only the loader of Windows is covered.");
#endif
}

// A record that cannot be written changes nothing else. The path lies below a regular file, so the
// helper cannot create its folder; it must still restore the previous version and end normally.
void TestUpdateHelper::unwritableRecordStillRollsBack() {
	const QString root = scenarioRoot("unwritable-record");
	const QString log = QDir(root).filePath("log.txt");
	const QString blocker = QDir(root).filePath("blocker");
	// The unwritable record fixtures are writable.
	QVERIFY(writeInstallation(QDir(root).filePath("install"), m_probe, "old", log, "mode=exit\ncode=0"));
	QVERIFY(writeInstallation(stagePath(root), m_probe, "new", log, "mode=exit\ncode=3"));
	QVERIFY(writeFile(blocker, "file"));
	const HelperRun run = runHelper(m_helper, root, kFailureObserveMs,
		updateRecordArguments(kUpdatedVersion, QDir(blocker).filePath("record.txt")));
	// The helper ends normally with code 1 when the record cannot be written.
	QCOMPARE(run.exitCode, 1);
	QVERIFY2(installationIs(root, "install", "label=old"), "the previous installation is restored without a record");
	QTRY_VERIFY2_WITH_TIMEOUT(logLines(log).contains("old started"), "the previous version is started without a record",
		kLogWaitMs);
#if defined(Q_OS_WIN)
	QTRY_VERIFY2_WITH_TIMEOUT(startedVersionHasEnded(root), "the previous version has ended", kLogWaitMs);
#endif
}

// Options that are missing or not accepted write no record, and the helper rolls back as before.
// Every case runs the early failure fixtures in a root of its own.
void TestUpdateHelper::noRecordWithoutValidOptions_data() {
	// The root of the case and the options that follow --observe-ms.
	QTest::addColumn<QString>("root");
	QTest::addColumn<QStringList>("arguments");
	int number = 0;
	const auto add = [this, &number](const char* label, QStringList (*arguments)(const QString& record)) {
		const QString root = QDir(m_temp.path()).filePath(QStringLiteral("no-record-%1").arg(++number));
		QTest::newRow(label) << root << arguments(recordPath(root));
	};
	add("no options", [](const QString&) { return QStringList(); });
	add("only --record", [](const QString& record) { return QStringList{"--record", record}; });
	add("a version with a letter",
		[](const QString& record) { return QStringList{"--version", "1.2.x", "--record", record}; });
	add("an empty version", [](const QString& record) { return QStringList{"--version", "", "--record", record}; });
	add("a version of 33 digits",
		[](const QString& record) { return QStringList{"--version", QString(33, QLatin1Char('9')), "--record", record}; });
	add("a relative record path",
		[](const QString&) { return QStringList{"--version", "9.8.7", "--record", "rel.txt"}; });
	add("a relative record path in a folder",
		[](const QString&) { return QStringList{"--version", "9.8.7", "--record", "rel/record.txt"}; });
}

void TestUpdateHelper::noRecordWithoutValidOptions() {
	QFETCH(QString, root);
	QFETCH(QStringList, arguments);
	const QString log = QDir(root).filePath("log.txt");
	// The fixtures are writable.
	QVERIFY(QDir().mkpath(root));
	QVERIFY(writeInstallation(QDir(root).filePath("install"), m_probe, "old", log, "mode=exit\ncode=0"));
	QVERIFY(writeInstallation(stagePath(root), m_probe, "new", log, "mode=exit\ncode=3"));
	const HelperRun run = runHelper(m_helper, root, kFailureObserveMs, arguments);
	// The helper ends with code 1.
	QCOMPARE(run.exitCode, 1);
	QVERIFY2(installationIs(root, "install", "label=old"), "the previous installation is restored");
	QTRY_VERIFY2_WITH_TIMEOUT(logLines(log).contains("old started"), "the previous version is started", kLogWaitMs);
	// No record is written. The helper works in the parent of the installation, which is root, so a relative
	// path would end up here.
	QVERIFY(!QFileInfo::exists(recordPath(root)));
	QVERIFY(!QFileInfo::exists(QDir(root).filePath("rel.txt")));
	QVERIFY(!QFileInfo::exists(QDir(root).filePath("rel")));
#if defined(Q_OS_WIN)
	QTRY_VERIFY2_WITH_TIMEOUT(startedVersionHasEnded(root), "the previous version has ended", kLogWaitMs);
#endif
}

// An installer that exits with 0 runs with its arguments and before the application, which finds the
// record. The helper replaces no file and clears the staging folder.
void TestUpdateHelper::installerRunsThenApplicationStarts() {
	const QString root = scenarioRoot("installer");
	const QString log = QDir(root).filePath("log.txt");
	QVERIFY2(writeInstallerFixtures(root, m_probe, log, "mode=exit\ncode=0"), "installer fixtures are writable");
	QStringList extraArguments = {"--installer-args", "/VERYSILENT /NORESTART"};
	extraArguments += recordArguments(root);
	const HelperRun run = runInstallerHelper(m_helper, root, kFailureObserveMs, extraArguments);
	// The helper succeeds when the installer exits with 0.
	QCOMPARE(run.exitCode, 0);
	QTRY_VERIFY2_WITH_TIMEOUT(logLines(log).contains("app started"), "the application starts after the installer",
		kLogWaitMs);
	const QStringList lines = logLines(log);
	QVERIFY2(lines.contains("setup args /VERYSILENT|/NORESTART"), "the installer gets its arguments");
	QVERIFY2(lines.contains("setup sees no record"), "the record is written after the installer has ended");
	QVERIFY2(applicationStartedOnceWithRecord(log), "the application starts once, after the record is written");
	// The installer ran before the application.
	QCOMPARE_GE(lines.indexOf("setup started"), 0);
	QCOMPARE_LT(lines.indexOf("setup started"), lines.indexOf("app started"));
	installerRecordIs(recordPath(root), "installer-succeeded", QString());
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(ranIn(log, "app", QDir(root).filePath("install")), "the application starts in the installation directory");
	QVERIFY2(ranIn(log, "setup", root), "the installer runs in the parent of the installation");
	QVERIFY2(installationIsUntouched(root), "the helper replaces no file of the installation");
	QVERIFY2(stagingIsCleared(root, m_helper), "the staging folder is removed after the application has started");
}

// Any installer exit code other than 0 is a failure. The helper starts the application anyway and
// records the failure with the exit code. Every code runs in a root of its own.
void TestUpdateHelper::failedInstallerStartsApplication_data() {
	QTest::addColumn<int>("code");
	QTest::newRow("installer exit code 1") << 1;
	QTest::newRow("installer exit code 8") << 8;
}

void TestUpdateHelper::failedInstallerStartsApplication() {
	QFETCH(int, code);
	const QString root = QDir(m_temp.path()).filePath(QStringLiteral("installer-failed-%1").arg(code));
	const QString log = QDir(root).filePath("log.txt");
	// The fixtures are writable.
	QVERIFY(QDir().mkpath(root));
	QVERIFY(writeInstallerFixtures(root, m_probe, log, QStringLiteral("mode=exit\ncode=%1").arg(code)));
	const HelperRun run = runInstallerHelper(m_helper, root, kFailureObserveMs, recordArguments(root));
	// The helper ends with code 1.
	QCOMPARE(run.exitCode, 1);
	QTRY_VERIFY2_WITH_TIMEOUT(logLines(log).contains("app started"), "the application is started", kLogWaitMs);
	QVERIFY2(applicationStartedOnceWithRecord(log), "the application starts once, after the record is written");
	installerRecordIs(recordPath(root), "installer-failed", QStringLiteral("exit-code=%1\n").arg(code));
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(installationIsUntouched(root), "the helper replaces no file of the installation");
#if defined(Q_OS_WIN)
	QTRY_VERIFY2_WITH_TIMEOUT(startedVersionHasEnded(root), "the application has ended", kLogWaitMs);
#endif
}

// An installer that is not there cannot fail with a code: the record has none.
void TestUpdateHelper::missingInstaller() {
	const QString root = scenarioRoot("installer-missing");
	const QString log = QDir(root).filePath("log.txt");
	// The missing installer fixtures are writable.
	QVERIFY(writeInstallerFixtures(root, m_probe, log, "mode=exit\ncode=0"));
	QVERIFY(QFile::remove(installerPath(root)));
	const HelperRun run = runInstallerHelper(m_helper, root, kFailureObserveMs, recordArguments(root));
	// The helper ends with code 1 when the installer is missing.
	QCOMPARE(run.exitCode, 1);
	QTRY_VERIFY2_WITH_TIMEOUT(logLines(log).contains("app started"), "the application is started when the installer is missing",
		kLogWaitMs);
	QVERIFY2(applicationStartedOnceWithRecord(log), "the application starts once, after the record is written");
	installerRecordIs(recordPath(root), "installer-failed", QString());
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(!logLines(log).contains("setup started"), "no installer ran");
	QVERIFY2(installationIsUntouched(root), "the helper replaces no file of the installation");
#if defined(Q_OS_WIN)
	QTRY_VERIFY2_WITH_TIMEOUT(startedVersionHasEnded(root), "the application has ended", kLogWaitMs);
#endif
}

// The installer has done its work but the application cannot be started: the helper reports that by
// its exit code, and the record still says that the installer succeeded.
void TestUpdateHelper::installerSucceedsButApplicationDoesNotStart() {
	const QString root = scenarioRoot("installer-without-application");
	const QString log = QDir(root).filePath("log.txt");
	// The missing application fixtures are writable.
	QVERIFY(writeInstallerFixtures(root, m_probe, log, "mode=exit\ncode=0"));
	QVERIFY(QFile::remove(QDir(root).filePath("install/app" + kProbeSuffix)));
	const HelperRun run = runInstallerHelper(m_helper, root, kFailureObserveMs, recordArguments(root));
	// The helper ends with code 1 when the application cannot be started.
	QCOMPARE(run.exitCode, 1);
	QVERIFY2(logLines(log).contains("setup started"), "the installer ran");
	installerRecordIs(recordPath(root), "installer-succeeded", QString());
}

// Installer mode never replaces files, so the options of the transaction are not accepted with it, and
// the options of the installer are not accepted without it. The helper then does nothing.
void TestUpdateHelper::installerOptionsExcludeTheTransaction_data() {
	// The root of the case and the options that follow --launch.
	QTest::addColumn<QString>("root");
	QTest::addColumn<QStringList>("arguments");
	int number = 0;
	const auto add = [this, &number](const char* label, QStringList (*arguments)(const QString& root)) {
		const QString root = QDir(m_temp.path()).filePath(QStringLiteral("installer-exclusive-%1").arg(++number));
		QTest::newRow(label) << root << arguments(root);
	};
	add("an installer with the options of the transaction", [](const QString& root) {
		QStringList arguments = {"--installer", installerPath(root)};
		arguments += transactionArguments(root);
		return arguments;
	});
	add("installer arguments without an installer", [](const QString& root) {
		QStringList arguments = {"--installer-args", "/VERYSILENT"};
		arguments += transactionArguments(root);
		return arguments;
	});
}

void TestUpdateHelper::installerOptionsExcludeTheTransaction() {
	QFETCH(QString, root);
	QFETCH(QStringList, arguments);
	const QString log = QDir(root).filePath("log.txt");
	// The fixtures are writable.
	QVERIFY(QDir().mkpath(root));
	QVERIFY(writeInstallerFixtures(root, m_probe, log, "mode=exit\ncode=0"));
	QVERIFY(writeInstallation(stagePath(root), m_probe, "new", log, "mode=exit\ncode=3"));
	QStringList helperArguments = {"--parent-pid", "0", "--launch", QDir(root).filePath("install/app" + kProbeSuffix)};
	helperArguments += arguments;
	const HelperRun run = runHelperProcess(m_helper, root, helperArguments);
	// The helper ends with the usage code.
	QCOMPARE(run.exitCode, 2);
	QVERIFY2(logLines(log).isEmpty(), "nothing is started");
	// No file is replaced.
	QVERIFY(QFileInfo::exists(stagePath(root)));
	QVERIFY(!QFileInfo::exists(QDir(root).filePath("install.egtrain-old")));
	QVERIFY(readFile(QDir(root).filePath("install/behavior.txt")).contains("label=app"));
}

QTEST_GUILESS_MAIN(TestUpdateHelper)
#include "test_updatehelper.moc"
