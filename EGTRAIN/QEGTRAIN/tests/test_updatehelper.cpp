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
#include <QThread>

#include <functional>
#include <iostream>
#include <optional>
#include <string>

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

static bool waitForLogLine(const QString& log, const QString& line, int timeoutMs = 10000) {
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < timeoutMs) {
		if (logLines(log).contains(line))
			return true;
		QThread::msleep(50);
	}
	return false;
}

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

// The version that the scenarios install, and the file that the helper is asked to leave if the
// update is rolled back. The file is in the root of the scenario, outside the installation, the
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
static bool recordIsWritten(const QString& path, UpdateOutcome outcome = UpdateOutcome::RolledBack) {
	bool ok = expect(QFileInfo::exists(path), "the helper leaves a record");
	QFile file(path);
	const QByteArray bytes = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
	const std::optional<UpdateRecord> record = parseUpdateRecord(bytes);
	ok &= expect(record && record->version == kUpdatedVersion, "the application reads the version of the record");
	ok &= expect(record && record->outcome == outcome, "the application reads the outcome of the record");
	ok &= expect(record && qAbs(record->time.secsTo(QDateTime::currentDateTimeUtc())) <= 300,
		"the record holds the current UTC time");
	const QString name = outcome == UpdateOutcome::NotApplied ? "not-applied" : "rolled-back";
	const QRegularExpression format(QRegularExpression::anchoredPattern(
		QStringLiteral("outcome=%1\nversion=9\\.8\\.7\ntime=\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z\n").arg(name)));
	ok &= expect(format.match(QString::fromLatin1(bytes)).hasMatch(),
		"the record is three lines, each ended by one line feed");
	return ok;
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

// A new version that exits with a failure code is replaced by the previous installation.
static bool testEarlyFailureRollsBack(const QString& helper, const QString& program, const QString& root) {
	const QString log = QDir(root).filePath("log.txt");
	bool ok = expect(writeInstallation(QDir(root).filePath("install"), program, "old", log,
						 QStringLiteral("mode=exit\ncode=0\nrecord=") + recordPath(root))
			&& writeInstallation(stagePath(root), program, "new", log, "mode=exit\ncode=3"),
		"early failure fixtures are writable");
	const HelperRun run = runHelper(helper, root, kFailureObserveMs, recordArguments(root));
	ok &= expect(run.exitCode != 0 && run.exitCode != -1, "helper fails when the new version exits with an error");
	ok &= expect(installationIs(root, "install", "label=old"), "early failure restores the previous installation");
	ok &= expect(!QFileInfo::exists(QDir(root).filePath("install.egtrain-old")), "early failure consumes the backup");
#if !defined(Q_OS_WIN)
	// On Windows a scanner can hold the crashed files, so the best-effort removal may lag.
	ok &= expect(!QFileInfo::exists(QDir(root).filePath("install.egtrain-old.failed")),
		"early failure removes the failed installation");
#endif
	ok &= expect(waitForLogLine(log, "old started"), "early failure starts the previous version again");
	ok &= recordIsWritten(recordPath(root));
	ok &= expect(logLines(log).contains("old sees record"), "the record exists before the previous version starts");
	const QString install = QDir(root).filePath("install");
	ok &= expect(ranIn(log, "new", install), "the new version starts in the installation directory");
	ok &= expect(ranIn(log, "old", install), "the restored version starts in the installation directory");
	const QStringList lines = logLines(log);
	ok &= expect(lines.indexOf("new started") >= 0 && lines.indexOf("new started") < lines.indexOf("old started"),
		"the new version ran before the previous one");
#if defined(Q_OS_WIN)
	QThread::msleep(300);
#endif
	return ok;
}

// A new version that keeps running is installed, and the helper ends after the observation window.
static bool testRunningVersionIsInstalled(const QString& helper, const QString& program, const QString& root) {
	const QString log = QDir(root).filePath("log.txt");
	const int observeMs = 1500;
	bool ok = expect(writeInstallation(QDir(root).filePath("install"), program, "old", log, "mode=exit\ncode=0")
			&& writeInstallation(stagePath(root), program, "new", log, "mode=sleep\nms=6000"),
		"running version fixtures are writable");
	const HelperRun run = runHelper(helper, root, observeMs, recordArguments(root));
	ok &= expect(run.exitCode == 0, "helper succeeds while the new version keeps running");
	ok &= expect(run.milliseconds >= observeMs - 100, "helper watches the new version for the whole window");
	ok &= expect(run.milliseconds < observeMs + 4000, "helper does not wait for the new version to end");
	if (!ok)
		std::cerr << "helper ran for " << run.milliseconds << " ms with a window of " << observeMs << " ms\n";
	ok &= expect(installationIs(root, "install", "label=new"), "running version stays installed");
	ok &= expect(installationIs(root, "install.egtrain-old", "label=old"), "running version keeps the backup");
	ok &= expect(waitForLogLine(log, "new done"), "running version finishes");
	ok &= expect(ranIn(log, "new", QDir(root).filePath("install")), "running version starts in the installation directory");
	ok &= expect(stagingIsCleared(root, helper), "the staging folder is removed after the observation window");
	ok &= expect(!logLines(log).contains("old started"), "previous version is not started again");
	ok &= expect(!QFileInfo::exists(recordPath(root)), "an update that keeps running leaves no record");
#if defined(Q_OS_WIN)
	QThread::msleep(300);
#endif
	return ok;
}

// A new version that exits with code 0 right away counts as started and ends the helper early.
static bool testCleanQuickExitIsInstalled(const QString& helper, const QString& program, const QString& root) {
	const QString log = QDir(root).filePath("log.txt");
	const int observeMs = 6000;
	bool ok = expect(writeInstallation(QDir(root).filePath("install"), program, "old", log, "mode=exit\ncode=0")
			&& writeInstallation(stagePath(root), program, "new", log, "mode=exit\ncode=0"),
		"quick exit fixtures are writable");
	const HelperRun run = runHelper(helper, root, observeMs, recordArguments(root));
	ok &= expect(run.exitCode == 0, "helper succeeds when the new version exits cleanly");
	ok &= expect(run.milliseconds < observeMs - 500, "helper does not sit out the window after a clean exit");
	if (!ok)
		std::cerr << "helper ran for " << run.milliseconds << " ms with a window of " << observeMs << " ms\n";
	ok &= expect(installationIs(root, "install", "label=new"), "clean exit stays installed");
	ok &= expect(installationIs(root, "install.egtrain-old", "label=old"), "clean exit keeps the backup");
	ok &= expect(!logLines(log).contains("old started"), "previous version is not started after a clean exit");
	ok &= expect(!QFileInfo::exists(recordPath(root)), "a clean exit leaves no record");
	ok &= expect(ranIn(log, "new", QDir(root).filePath("install")), "clean exit starts in the installation directory");
	ok &= expect(stagingIsCleared(root, helper), "the staging folder is removed after a clean exit");
	return ok;
}

// A backup in a folder that does not exist: the first rename of the installation fails until the folder is
// made, as it does on Windows while a file of the installation is locked.
static QString unavailableBackup(const QString& root) {
	return QDir(root).filePath("unavailable/install.egtrain-old");
}

// A first rename that fails for a while is tried again, and the update ends installed.
static bool testRenameIsRetried(const QString& helper, const QString& program, const QString& root) {
	const QString log = QDir(root).filePath("log.txt");
	const int observeMs = 6000;
	const int delayMs = 600;
	bool ok = expect(writeInstallation(QDir(root).filePath("install"), program, "old", log, "mode=exit\ncode=0")
			&& writeInstallation(stagePath(root), program, "new", log, "mode=exit\ncode=0"),
		"retry fixtures are writable");
	const HelperRun run = runHelper(helper, root, observeMs,
		QStringList{"--backup", unavailableBackup(root)} + recordArguments(root), delayMs,
		[&root]() { QDir().mkpath(QDir(root).filePath("unavailable")); });
	ok &= expect(run.exitCode == 0, "helper succeeds when the first rename works after a few attempts");
	ok &= expect(run.milliseconds >= delayMs - 100, "helper waited for the rename instead of giving up");
	ok &= expect(installationIs(root, "install", "label=new"), "the new version is installed after the retries");
	ok &= expect(readFile(unavailableBackup(root) + "/behavior.txt").contains("label=old"),
		"the previous installation is kept as the backup");
	ok &= expect(!logLines(log).contains("old started"), "the previous version is not started");
	ok &= expect(!QFileInfo::exists(recordPath(root)), "an update that is applied leaves no record");
	ok &= expect(stagingIsCleared(root, helper), "the staging folder is removed after the retries");
	return ok;
}

// A first rename that keeps failing leaves the previous installation as it was and starts it.
static bool testRenameKeepsFailing(const QString& helper, const QString& program, const QString& root) {
	const QString log = QDir(root).filePath("log.txt");
	bool ok = expect(writeInstallation(QDir(root).filePath("install"), program, "old", log,
						 QStringLiteral("mode=exit\ncode=0\nrecord=") + recordPath(root))
			&& writeInstallation(stagePath(root), program, "new", log, "mode=exit\ncode=0"),
		"failing rename fixtures are writable");
	const HelperRun run = runHelper(helper, root, kFailureObserveMs,
		QStringList{"--backup", unavailableBackup(root)} + recordArguments(root));
	ok &= expect(run.exitCode == 1, "helper ends with code 1 when the installation cannot be renamed");
	ok &= expect(installationIs(root, "install", "label=old"), "the previous installation stays in place");
	ok &= expect(!QFileInfo::exists(QDir(root).filePath("unavailable")), "no backup is made");
	ok &= expect(waitForLogLine(log, "old started"), "the previous version is started");
	ok &= expect(!logLines(log).contains("new started"), "the new version is not started");
	ok &= recordIsWritten(recordPath(root), UpdateOutcome::NotApplied);
	ok &= expect(logLines(log).contains("old sees record"), "the record exists before the previous version starts");
	ok &= expect(stagingIsCleared(root, helper), "the staging folder of the failed attempt is removed");
	ok &= expect(ranIn(log, "old", QDir(root).filePath("install")),
		"the previous version starts in the installation directory");
#if defined(Q_OS_WIN)
	QThread::msleep(300);
#endif
	return ok;
}

#if defined(Q_OS_WIN)
// A crash at startup is detected and does not leave an error dialog open.
static bool testCrashRollsBack(const QString& helper, const QString& program, const QString& root) {
	const QString log = QDir(root).filePath("log.txt");
	bool ok = expect(writeInstallation(QDir(root).filePath("install"), program, "old", log, "mode=exit\ncode=0")
			&& writeInstallation(stagePath(root), program, "new", log, "mode=crash"),
		"crash fixtures are writable");
	const HelperRun run = runHelper(helper, root, kFailureObserveMs, recordArguments(root));
	ok &= expect(run.exitCode != 0 && run.exitCode != -1, "helper fails when the new version crashes");
	ok &= expect(installationIs(root, "install", "label=old"), "crash restores the previous installation");
	ok &= expect(waitForLogLine(log, "old started"), "crash starts the previous version again");
	ok &= recordIsWritten(recordPath(root));
	QThread::msleep(300);
	return ok;
}

// A package without an imported DLL fails in the loader and is replaced by the previous installation.
static bool testMissingDllRollsBack(const QString& helper, const QString& dllProbe, const QString& dll,
	const QString& root) {
	const QDir directory(root);
	const QString dllName = QFileInfo(dll).fileName();
	bool ok = expect(QDir().mkpath(directory.filePath("install")) && QDir().mkpath(stagePath(root))
			&& QFile::copy(dllProbe, directory.filePath("install/app.exe"))
			&& QFile::copy(dll, directory.filePath("install/" + dllName))
			&& QFile::copy(dllProbe, QDir(stagePath(root)).filePath("app.exe"))
			&& writeFile(QDir(stagePath(root)).filePath("new-marker"), "new"),
		"missing DLL fixtures are writable");
	const HelperRun run = runHelper(helper, root, kFailureObserveMs, recordArguments(root));
	ok &= expect(run.exitCode != 0 && run.exitCode != -1, "helper fails when the new version cannot load a DLL");
	ok &= expect(QFileInfo::exists(directory.filePath("install/" + dllName)),
		"loader failure restores the previous installation");
	ok &= expect(!QFileInfo::exists(directory.filePath("install/new-marker")),
		"loader failure removes the new installation");
	ok &= expect(!QFileInfo::exists(directory.filePath("install.egtrain-old")), "loader failure consumes the backup");
	ok &= recordIsWritten(recordPath(root));
	QThread::msleep(500);
	return ok;
}
#endif

// A record that cannot be written changes nothing else. The path lies below a regular file, so the
// helper cannot create its folder; it must still restore the previous version and end normally.
static bool testUnwritableRecordStillRollsBack(const QString& helper, const QString& program, const QString& root) {
	const QString log = QDir(root).filePath("log.txt");
	const QString blocker = QDir(root).filePath("blocker");
	bool ok = expect(writeInstallation(QDir(root).filePath("install"), program, "old", log, "mode=exit\ncode=0")
			&& writeInstallation(stagePath(root), program, "new", log, "mode=exit\ncode=3")
			&& writeFile(blocker, "file"),
		"unwritable record fixtures are writable");
	const HelperRun run = runHelper(helper, root, kFailureObserveMs,
		updateRecordArguments(kUpdatedVersion, QDir(blocker).filePath("record.txt")));
	ok &= expect(run.exitCode == 1, "helper ends normally with code 1 when the record cannot be written");
	ok &= expect(installationIs(root, "install", "label=old"), "the previous installation is restored without a record");
	ok &= expect(waitForLogLine(log, "old started"), "the previous version is started without a record");
#if defined(Q_OS_WIN)
	QThread::msleep(300);
#endif
	return ok;
}

// Options that are missing or not accepted write no record, and the helper rolls back as before.
// Every case runs the early failure fixtures in a root of its own.
static bool testNoRecordWithoutValidOptions(const QString& helper, const QString& program, const QString& base) {
	struct Case {
		const char* label;
		QStringList (*arguments)(const QString& record);
	};
	const Case cases[] = {
		{"no options", [](const QString&) { return QStringList(); }},
		{"only --record", [](const QString& record) { return QStringList{"--record", record}; }},
		{"a version with a letter",
			[](const QString& record) { return QStringList{"--version", "1.2.x", "--record", record}; }},
		{"an empty version", [](const QString& record) { return QStringList{"--version", "", "--record", record}; }},
		{"a version of 33 digits",
			[](const QString& record) {
				return QStringList{"--version", QString(33, QLatin1Char('9')), "--record", record};
			}},
		{"a relative record path",
			[](const QString&) { return QStringList{"--version", "9.8.7", "--record", "rel.txt"}; }},
		{"a relative record path in a folder",
			[](const QString&) { return QStringList{"--version", "9.8.7", "--record", "rel/record.txt"}; }}};
	bool ok = true;
	int number = 0;
	for (const Case& entry : cases) {
		const QString root = QDir(base).filePath(QStringLiteral("no-record-%1").arg(++number));
		const QString log = QDir(root).filePath("log.txt");
		const std::string label = entry.label;
		ok &= expect(QDir().mkpath(root)
				&& writeInstallation(QDir(root).filePath("install"), program, "old", log, "mode=exit\ncode=0")
				&& writeInstallation(stagePath(root), program, "new", log, "mode=exit\ncode=3"),
			(label + ": fixtures are writable").c_str());
		const HelperRun run = runHelper(helper, root, kFailureObserveMs, entry.arguments(recordPath(root)));
		ok &= expect(run.exitCode == 1, (label + ": helper ends with code 1").c_str());
		ok &= expect(installationIs(root, "install", "label=old"), (label + ": the previous installation is restored").c_str());
		ok &= expect(waitForLogLine(log, "old started"), (label + ": the previous version is started").c_str());
		// The helper works in the parent of the installation, which is root, so a relative path would end up here.
		ok &= expect(!QFileInfo::exists(recordPath(root)) && !QFileInfo::exists(QDir(root).filePath("rel.txt"))
				&& !QFileInfo::exists(QDir(root).filePath("rel")),
			(label + ": no record is written").c_str());
#if defined(Q_OS_WIN)
		QThread::msleep(300);
#endif
	}
	return ok;
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
static bool installerRecordIs(const QString& path, const QString& outcome, const QString& details) {
	const QByteArray bytes = readFile(path);
	const QRegularExpression format(QRegularExpression::anchoredPattern(
		QStringLiteral("outcome=%1\nversion=9\\.8\\.7\ntime=(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}Z)\n%2")
			.arg(outcome, details)));
	const QRegularExpressionMatch match = format.match(QString::fromLatin1(bytes));
	bool ok = expect(match.hasMatch(), "the installer record holds the outcome, the version, the time and the details");
	ok &= expect(match.hasMatch()
			&& qAbs(QDateTime::fromString(match.captured(1), Qt::ISODate).secsTo(QDateTime::currentDateTimeUtc())) <= 300,
		"the installer record holds the current UTC time");
	ok &= expect(!parseUpdateRecord(bytes), "the reader of a rolled-back update does not take an installer record");
	return ok;
}

// An installer that exits with 0 runs with its arguments and before the application, which finds the
// record. The helper replaces no file and clears the staging folder.
static bool testInstallerRunsThenApplicationStarts(const QString& helper, const QString& program, const QString& root) {
	const QString log = QDir(root).filePath("log.txt");
	bool ok = expect(writeInstallerFixtures(root, program, log, "mode=exit\ncode=0"), "installer fixtures are writable");
	QStringList extraArguments = {"--installer-args", "/VERYSILENT /NORESTART"};
	extraArguments += recordArguments(root);
	const HelperRun run = runInstallerHelper(helper, root, kFailureObserveMs, extraArguments);
	ok &= expect(run.exitCode == 0, "helper succeeds when the installer exits with 0");
	ok &= expect(waitForLogLine(log, "app started"), "the application starts after the installer");
	const QStringList lines = logLines(log);
	ok &= expect(lines.contains("setup args /VERYSILENT|/NORESTART"), "the installer gets its arguments");
	ok &= expect(lines.contains("setup sees no record"), "the record is written after the installer has ended");
	ok &= expect(applicationStartedOnceWithRecord(log), "the application starts once, after the record is written");
	ok &= expect(lines.indexOf("setup started") >= 0 && lines.indexOf("setup started") < lines.indexOf("app started"),
		"the installer ran before the application");
	ok &= installerRecordIs(recordPath(root), "installer-succeeded", QString());
	ok &= expect(ranIn(log, "app", QDir(root).filePath("install")), "the application starts in the installation directory");
	ok &= expect(ranIn(log, "setup", root), "the installer runs in the parent of the installation");
	ok &= expect(installationIsUntouched(root), "the helper replaces no file of the installation");
	ok &= expect(stagingIsCleared(root, helper), "the staging folder is removed after the application has started");
	return ok;
}

// Any installer exit code other than 0 is a failure. The helper starts the application anyway and
// records the failure with the exit code. Every code runs in a root of its own.
static bool testFailedInstallerStartsApplication(const QString& helper, const QString& program, const QString& base) {
	bool ok = true;
	for (const int code : {1, 8}) {
		const QString root = QDir(base).filePath(QStringLiteral("installer-failed-%1").arg(code));
		const QString log = QDir(root).filePath("log.txt");
		const std::string label = "installer exit code " + std::to_string(code) + ": ";
		ok &= expect(QDir().mkpath(root)
				&& writeInstallerFixtures(root, program, log, QStringLiteral("mode=exit\ncode=%1").arg(code)),
			(label + "fixtures are writable").c_str());
		const HelperRun run = runInstallerHelper(helper, root, kFailureObserveMs, recordArguments(root));
		ok &= expect(run.exitCode == 1, (label + "helper ends with code 1").c_str());
		ok &= expect(waitForLogLine(log, "app started"), (label + "the application is started").c_str());
		ok &= expect(applicationStartedOnceWithRecord(log),
			(label + "the application starts once, after the record is written").c_str());
		ok &= installerRecordIs(recordPath(root), "installer-failed", QStringLiteral("exit-code=%1\n").arg(code));
		ok &= expect(installationIsUntouched(root), (label + "the helper replaces no file of the installation").c_str());
#if defined(Q_OS_WIN)
		QThread::msleep(300);
#endif
	}
	return ok;
}

// An installer that is not there cannot fail with a code: the record has none.
static bool testMissingInstaller(const QString& helper, const QString& program, const QString& root) {
	const QString log = QDir(root).filePath("log.txt");
	bool ok = expect(writeInstallerFixtures(root, program, log, "mode=exit\ncode=0")
			&& QFile::remove(installerPath(root)),
		"missing installer fixtures are writable");
	const HelperRun run = runInstallerHelper(helper, root, kFailureObserveMs, recordArguments(root));
	ok &= expect(run.exitCode == 1, "helper ends with code 1 when the installer is missing");
	ok &= expect(waitForLogLine(log, "app started"), "the application is started when the installer is missing");
	ok &= expect(applicationStartedOnceWithRecord(log), "the application starts once, after the record is written");
	ok &= installerRecordIs(recordPath(root), "installer-failed", QString());
	ok &= expect(!logLines(log).contains("setup started"), "no installer ran");
	ok &= expect(installationIsUntouched(root), "the helper replaces no file of the installation");
#if defined(Q_OS_WIN)
	QThread::msleep(300);
#endif
	return ok;
}

// The installer has done its work but the application cannot be started: the helper reports that by
// its exit code, and the record still says that the installer succeeded.
static bool testInstallerSucceedsButApplicationDoesNotStart(const QString& helper, const QString& program,
	const QString& root) {
	const QString log = QDir(root).filePath("log.txt");
	bool ok = expect(writeInstallerFixtures(root, program, log, "mode=exit\ncode=0")
			&& QFile::remove(QDir(root).filePath("install/app" + kProbeSuffix)),
		"missing application fixtures are writable");
	const HelperRun run = runInstallerHelper(helper, root, kFailureObserveMs, recordArguments(root));
	ok &= expect(run.exitCode == 1, "helper ends with code 1 when the application cannot be started");
	ok &= expect(logLines(log).contains("setup started"), "the installer ran");
	ok &= installerRecordIs(recordPath(root), "installer-succeeded", QString());
	return ok;
}

// The options of the update of root/install by the stage in the staging folder.
static QStringList transactionArguments(const QString& root) {
	const QDir directory(root);
	return {"--current", directory.filePath("install"), "--staged", stagePath(root),
		"--backup", directory.filePath("install.egtrain-old")};
}

// Installer mode never replaces files, so the options of the transaction are not accepted with it, and
// the options of the installer are not accepted without it. The helper then does nothing.
static bool testInstallerOptionsExcludeTheTransaction(const QString& helper, const QString& program,
	const QString& base) {
	struct Case {
		const char* label;
		QStringList (*arguments)(const QString& root);
	};
	const Case cases[] = {
		{"an installer with the options of the transaction",
			[](const QString& root) {
				QStringList arguments = {"--installer", installerPath(root)};
				arguments += transactionArguments(root);
				return arguments;
			}},
		{"installer arguments without an installer", [](const QString& root) {
			 QStringList arguments = {"--installer-args", "/VERYSILENT"};
			 arguments += transactionArguments(root);
			 return arguments;
		 }}};
	bool ok = true;
	int number = 0;
	for (const Case& entry : cases) {
		const QString root = QDir(base).filePath(QStringLiteral("installer-exclusive-%1").arg(++number));
		const QString log = QDir(root).filePath("log.txt");
		const std::string label = entry.label;
		ok &= expect(QDir().mkpath(root) && writeInstallerFixtures(root, program, log, "mode=exit\ncode=0")
				&& writeInstallation(stagePath(root), program, "new", log, "mode=exit\ncode=3"),
			(label + ": fixtures are writable").c_str());
		QStringList arguments = {"--parent-pid", "0", "--launch", QDir(root).filePath("install/app" + kProbeSuffix)};
		arguments += entry.arguments(root);
		const HelperRun run = runHelperProcess(helper, root, arguments);
		ok &= expect(run.exitCode == 2, (label + ": helper ends with the usage code").c_str());
		ok &= expect(logLines(log).isEmpty(), (label + ": nothing is started").c_str());
		ok &= expect(QFileInfo::exists(stagePath(root)) && !QFileInfo::exists(QDir(root).filePath("install.egtrain-old"))
				&& readFile(QDir(root).filePath("install/behavior.txt")).contains("label=app"),
			(label + ": no file is replaced").c_str());
	}
	return ok;
}

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	if (argc == 1)
		return 0;
#if defined(Q_OS_WIN)
	if (argc != 5)
		return 2;
#else
	if (argc != 3)
		return 2;
#endif
	const QString helper = QString::fromLocal8Bit(argv[1]);
	const QString probe = QString::fromLocal8Bit(argv[2]);
	QTemporaryDir temp;
	if (!temp.isValid())
		return 1;

	bool ok = true;
	const QString current = QDir(temp.path()).filePath("current.bin");
	const QString staged = QDir(temp.path()).filePath("staged.bin");
	const QString backup = QDir(temp.path()).filePath("backup.bin");
	ok &= expect(writeFile(current, "old"), "fixture current file is writable");
	ok &= expect(writeFile(staged, "new"), "fixture staged file is writable");
	const QStringList successArguments = {
		"--parent-pid", "0", "--current", current, "--staged", staged,
		"--backup", backup, "--launch", QCoreApplication::applicationFilePath()};
	ok &= expect(QProcess::execute(helper, successArguments) == 0,
		"helper installs a staged file");
	ok &= expect(readFile(current) == QByteArray("new"), "new file is active after helper success");
	ok &= expect(readFile(backup) == QByteArray("old"),
		"successful helper retains a recoverable backup");

	const QString missingStage = QDir(temp.path()).filePath("missing.bin");
	const QString rollbackBackup = QDir(temp.path()).filePath("rollback.bin");
	ok &= expect(
		QProcess::execute(helper,
			{"--parent-pid", "0", "--current", current, "--staged", missingStage,
				"--backup", rollbackBackup, "--launch", QCoreApplication::applicationFilePath()})
			!= 0,
		"helper rejects a missing staged file");
	ok &= expect(readFile(current) == QByteArray("new"),
		"failed helper leaves the active file untouched");
	const QString rollbackStage = QDir(temp.path()).filePath("rollback-stage.bin");
	const QString launchFailureBackup = QDir(temp.path()).filePath("launch-failure.bin");
	ok &= expect(writeFile(current, "old-again") && writeFile(rollbackStage, "new-again"),
		"rollback fixture is writable");
	const QString relaunchRecord = QDir(temp.path()).filePath("relaunch-record.txt");
	QStringList relaunchArguments = {"--parent-pid", "0", "--current", current, "--staged", rollbackStage,
		"--backup", launchFailureBackup, "--launch", QDir(temp.path()).filePath("missing-launch")};
	relaunchArguments += updateRecordArguments(kUpdatedVersion, relaunchRecord);
	ok &= expect(QProcess::execute(helper, relaunchArguments) != 0, "helper rolls back when relaunch fails");
	ok &= expect(readFile(current) == QByteArray("old-again"),
		"launch failure restores the previous file");
	ok &= recordIsWritten(relaunchRecord);

	const QString currentDir = QDir(temp.path()).filePath("current-dir");
	const QString stagedDir = QDir(temp.path()).filePath("staged-dir");
	const QString backupDir = QDir(temp.path()).filePath("backup-dir");
	QDir().mkpath(currentDir);
	QDir().mkpath(stagedDir);
	ok &= expect(writeFile(QDir(currentDir).filePath("marker"), "old"),
		"directory fixture current is writable");
	ok &= expect(writeFile(QDir(stagedDir).filePath("marker"), "new"),
		"directory fixture staged is writable");
	QProcess directoryUpdate;
	directoryUpdate.setProgram(helper);
	directoryUpdate.setArguments({"--parent-pid", "0", "--current", currentDir, "--staged", stagedDir,
		"--backup", backupDir, "--launch", QCoreApplication::applicationFilePath()});
	directoryUpdate.setWorkingDirectory(currentDir);
	directoryUpdate.start();
	directoryUpdate.waitForFinished();
	ok &= expect(directoryUpdate.exitStatus() == QProcess::NormalExit
			&& directoryUpdate.exitCode() == 0,
		"helper installs a staged directory");
	ok &= expect(readFile(QDir(currentDir).filePath("marker")) == QByteArray("new"),
		"new directory is active after helper success");

	const auto scenarioRoot = [&temp](const char* name) {
		const QString root = QDir(temp.path()).filePath(name);
		QDir().mkpath(root);
		return root;
	};
	ok &= testEarlyFailureRollsBack(helper, probe, scenarioRoot("early-failure"));
	ok &= testRunningVersionIsInstalled(helper, probe, scenarioRoot("running"));
	ok &= testCleanQuickExitIsInstalled(helper, probe, scenarioRoot("quick-exit"));
	ok &= testRenameIsRetried(helper, probe, scenarioRoot("rename-retried"));
	ok &= testRenameKeepsFailing(helper, probe, scenarioRoot("rename-fails"));
	ok &= testUnwritableRecordStillRollsBack(helper, probe, scenarioRoot("unwritable-record"));
	ok &= testNoRecordWithoutValidOptions(helper, probe, temp.path());
	ok &= testInstallerRunsThenApplicationStarts(helper, probe, scenarioRoot("installer"));
	ok &= testFailedInstallerStartsApplication(helper, probe, temp.path());
	ok &= testMissingInstaller(helper, probe, scenarioRoot("installer-missing"));
	ok &= testInstallerSucceedsButApplicationDoesNotStart(helper, probe, scenarioRoot("installer-without-application"));
	ok &= testInstallerOptionsExcludeTheTransaction(helper, probe, temp.path());
#if defined(Q_OS_WIN)
	const QString dllProbe = QString::fromLocal8Bit(argv[3]);
	const QString dll = QString::fromLocal8Bit(argv[4]);
	ok &= testCrashRollsBack(helper, probe, scenarioRoot("crash"));
	ok &= testMissingDllRollsBack(helper, dllProbe, dll, scenarioRoot("missing-dll"));
#endif
	return ok ? 0 : 1;
}
