#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

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

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	if (argc == 1)
		return 0;
	if (argc != 2)
		return 2;
	const QString helper = QString::fromLocal8Bit(argv[1]);
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
	ok &= expect(QProcess::execute(helper, {
		"--parent-pid", "0", "--current", current, "--staged", missingStage,
		"--backup", rollbackBackup, "--launch", QCoreApplication::applicationFilePath()}) != 0,
		"helper rejects a missing staged file");
	ok &= expect(readFile(current) == QByteArray("new"),
		"failed helper leaves the active file untouched");
	const QString rollbackStage = QDir(temp.path()).filePath("rollback-stage.bin");
	const QString launchFailureBackup = QDir(temp.path()).filePath("launch-failure.bin");
	ok &= expect(writeFile(current, "old-again") && writeFile(rollbackStage, "new-again"),
		"rollback fixture is writable");
	ok &= expect(QProcess::execute(helper, {
		"--parent-pid", "0", "--current", current, "--staged", rollbackStage,
		"--backup", launchFailureBackup, "--launch", QDir(temp.path()).filePath("missing-launch")}) != 0,
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
	directoryUpdate.setArguments({
		"--parent-pid", "0", "--current", currentDir, "--staged", stagedDir,
		"--backup", backupDir, "--launch", QCoreApplication::applicationFilePath()});
	directoryUpdate.setWorkingDirectory(currentDir);
	directoryUpdate.start();
	directoryUpdate.waitForFinished();
	ok &= expect(directoryUpdate.exitStatus() == QProcess::NormalExit
		&& directoryUpdate.exitCode() == 0,
		"helper installs a staged directory");
	ok &= expect(readFile(QDir(currentDir).filePath("marker")) == QByteArray("new"),
		"new directory is active after helper success");
	return ok ? 0 : 1;
}
