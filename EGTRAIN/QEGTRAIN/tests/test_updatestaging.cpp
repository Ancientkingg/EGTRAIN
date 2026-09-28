#include "update/WindowsStaging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

static void populateInstallation(const QString& directory, const char* marker,
	const char* executableContents) {
	QDir().mkpath(QDir(directory).filePath(QStringLiteral("platforms")));
	QDir().mkpath(QDir(directory).filePath(QStringLiteral("Scenes")));
	writeFile(QDir(directory).filePath(QStringLiteral("QEGTRAIN.exe")), executableContents);
	writeFile(QDir(directory).filePath(QStringLiteral("egtrain_update_helper.exe")),
		executableContents);
	writeFile(QDir(directory).filePath(QStringLiteral("Qt5Network.dll")), executableContents);
	writeFile(QDir(directory).filePath(QStringLiteral("platforms/qwindows.dll")),
		executableContents);
	writeFile(QDir(directory).filePath(QStringLiteral("Scenes/marker.txt")), marker);
}

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	QTemporaryDir temp;
	if (!temp.isValid())
		return 1;

	bool ok = true;
	const QString current = QDir(temp.path()).filePath(QStringLiteral("current"));
	populateInstallation(current, "old", "old");
	ok &= expect(writeFile(QDir(current).filePath(QStringLiteral("obsolete-runtime-file.dll")),
		"old-dll"), "old installation fixture is writable");

	const QString extract = QDir(temp.path()).filePath(QStringLiteral("extract"));
	populateInstallation(extract, "new", "new");

	QString error;
	const QString staged = QDir(temp.path()).filePath(QStringLiteral("QEGTRAIN"));
	ok &= expect(!QFileInfo(staged).exists(), "staging starts without an installation");
	ok &= expect(WindowsStaging::buildStage(extract, staged, &error),
		"staging builds from the extracted release package");
	ok &= expect(readFile(QDir(staged).filePath(QStringLiteral("Scenes/marker.txt")))
		== QByteArray("new"), "the new package owns the shipped Scenes");
	ok &= expect(!QFileInfo(QDir(staged).filePath(
		QStringLiteral("obsolete-runtime-file.dll"))).exists(),
		"stale old installation files are dropped");
	ok &= expect(readFile(QDir(staged).filePath(QStringLiteral("QEGTRAIN.exe")))
		== QByteArray("new"), "staged runtime files come from the new package");

	error.clear();
	ok &= expect(!WindowsStaging::buildStage(extract, staged, &error)
		&& error == QStringLiteral("The Windows update staging location is not empty."),
		"staging refuses to merge into an existing installation");

	const QString incomplete = QDir(temp.path()).filePath(QStringLiteral("incomplete"));
	populateInstallation(incomplete, "new", "new");
	QFile::remove(QDir(incomplete).filePath(QStringLiteral("Qt5Network.dll")));
	error.clear();
	ok &= expect(!WindowsStaging::buildStage(incomplete,
		QDir(temp.path()).filePath(QStringLiteral("staged-incomplete")), &error)
		&& error == QStringLiteral("The Windows update package is missing required runtime files."),
		"staging rejects a package with missing runtime files");

	const QString noExecutable = QDir(temp.path()).filePath(QStringLiteral("no-executable"));
	QDir().mkpath(noExecutable);
	error.clear();
	ok &= expect(!WindowsStaging::buildStage(noExecutable,
		QDir(temp.path()).filePath(QStringLiteral("staged-no-executable")), &error)
		&& error == QStringLiteral("The Windows update package does not contain QEGTRAIN.exe."),
		"staging rejects a package without QEGTRAIN.exe");

	return ok ? 0 : 1;
}
