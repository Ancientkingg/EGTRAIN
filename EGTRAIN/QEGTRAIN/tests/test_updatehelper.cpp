#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

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

// Copies the probe program into directory as "app" and gives it a behavior file.
static bool writeInstallation(const QString& directory, const QString& program, const QString& label,
	const QString& log, const QString& behavior) {
	if (!QDir().mkpath(directory))
		return false;
	const QString app = QDir(directory).filePath("app" + kProbeSuffix);
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

struct HelperRun {
	int exitCode = -1;
	qint64 milliseconds = 0;
};

// One update of root/install by the stage in the staging folder, with root/install.egtrain-old as
// the backup. The helper runs from a copy in the staging folder.
static HelperRun runHelper(const QString& helper, const QString& root, int observeMs) {
	const QDir directory(root);
	const QString helperCopy = QDir(stagingFolder(root)).filePath(QFileInfo(helper).fileName());
	HelperRun result;
	if (!QFile::copy(helper, helperCopy))
		return result;
	QProcess process;
	process.setProgram(helperCopy);
	process.setArguments({"--parent-pid", "0", "--current", directory.filePath("install"),
		"--staged", stagePath(root),
		"--backup", directory.filePath("install.egtrain-old"),
		"--launch", directory.filePath("install/app" + kProbeSuffix),
		"--observe-ms", QString::number(observeMs)});
	QElapsedTimer timer;
	timer.start();
	process.start();
	if (!process.waitForFinished(60000) || process.exitStatus() != QProcess::NormalExit)
		return result;
	result.milliseconds = timer.elapsed();
	result.exitCode = process.exitCode();
	return result;
}

static bool installationIs(const QString& root, const QString& directory, const char* label) {
	return readFile(QDir(root).filePath(directory + "/behavior.txt")).contains(label);
}

// A new version that exits with a failure code is replaced by the previous installation.
static bool testEarlyFailureRollsBack(const QString& helper, const QString& program, const QString& root) {
	const QString log = QDir(root).filePath("log.txt");
	bool ok = expect(writeInstallation(QDir(root).filePath("install"), program, "old", log, "mode=exit\ncode=0")
			&& writeInstallation(stagePath(root), program, "new", log, "mode=exit\ncode=3"),
		"early failure fixtures are writable");
	const HelperRun run = runHelper(helper, root, kFailureObserveMs);
	ok &= expect(run.exitCode != 0 && run.exitCode != -1, "helper fails when the new version exits with an error");
	ok &= expect(installationIs(root, "install", "label=old"), "early failure restores the previous installation");
	ok &= expect(!QFileInfo::exists(QDir(root).filePath("install.egtrain-old")), "early failure consumes the backup");
#if !defined(Q_OS_WIN)
	// On Windows a scanner can hold the crashed files, so the best-effort removal may lag.
	ok &= expect(!QFileInfo::exists(QDir(root).filePath("install.egtrain-old.failed")),
		"early failure removes the failed installation");
#endif
	ok &= expect(waitForLogLine(log, "old started"), "early failure starts the previous version again");
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
	const HelperRun run = runHelper(helper, root, observeMs);
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
	const HelperRun run = runHelper(helper, root, observeMs);
	ok &= expect(run.exitCode == 0, "helper succeeds when the new version exits cleanly");
	ok &= expect(run.milliseconds < observeMs - 500, "helper does not sit out the window after a clean exit");
	if (!ok)
		std::cerr << "helper ran for " << run.milliseconds << " ms with a window of " << observeMs << " ms\n";
	ok &= expect(installationIs(root, "install", "label=new"), "clean exit stays installed");
	ok &= expect(installationIs(root, "install.egtrain-old", "label=old"), "clean exit keeps the backup");
	ok &= expect(!logLines(log).contains("old started"), "previous version is not started after a clean exit");
	ok &= expect(ranIn(log, "new", QDir(root).filePath("install")), "clean exit starts in the installation directory");
	ok &= expect(stagingIsCleared(root, helper), "the staging folder is removed after a clean exit");
	return ok;
}

#if defined(Q_OS_WIN)
// A crash at startup is detected and does not leave an error dialog open.
static bool testCrashRollsBack(const QString& helper, const QString& program, const QString& root) {
	const QString log = QDir(root).filePath("log.txt");
	bool ok = expect(writeInstallation(QDir(root).filePath("install"), program, "old", log, "mode=exit\ncode=0")
			&& writeInstallation(stagePath(root), program, "new", log, "mode=crash"),
		"crash fixtures are writable");
	const HelperRun run = runHelper(helper, root, kFailureObserveMs);
	ok &= expect(run.exitCode != 0 && run.exitCode != -1, "helper fails when the new version crashes");
	ok &= expect(installationIs(root, "install", "label=old"), "crash restores the previous installation");
	ok &= expect(waitForLogLine(log, "old started"), "crash starts the previous version again");
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
	const HelperRun run = runHelper(helper, root, kFailureObserveMs);
	ok &= expect(run.exitCode != 0 && run.exitCode != -1, "helper fails when the new version cannot load a DLL");
	ok &= expect(QFileInfo::exists(directory.filePath("install/" + dllName)),
		"loader failure restores the previous installation");
	ok &= expect(!QFileInfo::exists(directory.filePath("install/new-marker")),
		"loader failure removes the new installation");
	ok &= expect(!QFileInfo::exists(directory.filePath("install.egtrain-old")), "loader failure consumes the backup");
	QThread::msleep(500);
	return ok;
}
#endif

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
	ok &= expect(
		QProcess::execute(helper,
			{"--parent-pid", "0", "--current", current, "--staged", rollbackStage,
				"--backup", launchFailureBackup, "--launch", QDir(temp.path()).filePath("missing-launch")})
			!= 0,
		"helper rolls back when relaunch fails");
	ok &= expect(readFile(current) == QByteArray("old-again"),
		"launch failure restores the previous file");

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
#if defined(Q_OS_WIN)
	const QString dllProbe = QString::fromLocal8Bit(argv[3]);
	const QString dll = QString::fromLocal8Bit(argv[4]);
	ok &= testCrashRollsBack(helper, probe, scenarioRoot("crash"));
	ok &= testMissingDllRollsBack(helper, dllProbe, dll, scenarioRoot("missing-dll"));
#endif
	return ok ? 0 : 1;
}
