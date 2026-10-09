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
	QDir().mkpath(QDir(directory).filePath(QStringLiteral("imageformats")));
	QDir().mkpath(QDir(directory).filePath(QStringLiteral("Scenes")));
	for (const QString& name : WindowsStaging::requiredRuntimeFiles())
		writeFile(QDir(directory).filePath(name), executableContents);
	writeFile(QDir(directory).filePath(QStringLiteral("libzmq-mt-4_3_5.dll")), executableContents);
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
	ok &= expect(WindowsStaging::buildStage(extract, staged, {}, &error),
		"staging builds from the extracted release package");
	ok &= expect(readFile(QDir(staged).filePath(QStringLiteral("Scenes/marker.txt")))
		== QByteArray("new"), "the new package owns the shipped Scenes");
	ok &= expect(!QFileInfo(QDir(staged).filePath(
		QStringLiteral("obsolete-runtime-file.dll"))).exists(),
		"stale old installation files are dropped");
	ok &= expect(readFile(QDir(staged).filePath(QStringLiteral("QEGTRAIN.exe")))
		== QByteArray("new"), "staged runtime files come from the new package");

	error.clear();
	ok &= expect(!WindowsStaging::buildStage(extract, staged, {}, &error)
		&& error == QStringLiteral("The Windows update staging location is not empty."),
		"staging refuses to merge into an existing installation");

	// Stages a fresh copy of the fixture package after the given changes and
	// returns the staging error, or a null string when staging succeeded.
	int packageCount = 0;
	const auto stage = [&](const QStringList& removed, const QStringList& manifestFiles,
		const QStringList& added = {}) {
		const QString name = QStringLiteral("package-%1").arg(++packageCount);
		const QString package = QDir(temp.path()).filePath(name);
		populateInstallation(package, "new", "new");
		for (const QString& path : removed) {
			const QString full = QDir(package).filePath(path);
			if (QFileInfo(full).isDir())
				QDir(full).removeRecursively();
			else
				QFile::remove(full);
		}
		for (const QString& path : added) {
			QDir().mkpath(QFileInfo(QDir(package).filePath(path)).absolutePath());
			writeFile(QDir(package).filePath(path), "new");
		}
		QString stageError;
		const bool built = WindowsStaging::buildStage(package,
			QDir(temp.path()).filePath(name + QStringLiteral("-staged")), manifestFiles, &stageError);
		return built ? QString() : stageError;
	};
	const auto missingMessage = [](const QString& name) {
		return QStringLiteral("The Windows update package is missing required runtime files: %1.")
			.arg(name);
	};
	const QString libzmq = QStringLiteral("libzmq-mt-4_3_5.dll");

	ok &= expect(stage({}, {}).isNull(), "a complete package passes the runtime floor");

	struct MissingCase {
		QString removed;
		QString reported;
	};
	QList<MissingCase> missingCases = {
		{libzmq, QStringLiteral("libzmq*.dll")},
		{QStringLiteral("Scenes"), QStringLiteral("Scenes")}};
	for (const QString& name : WindowsStaging::requiredRuntimeFiles()) {
		if (name != QStringLiteral("QEGTRAIN.exe"))
			missingCases.append({name, name});
	}
	for (const MissingCase& missingCase : missingCases) {
		ok &= expect(stage({missingCase.removed}, {}) == missingMessage(missingCase.reported),
			qPrintable(QStringLiteral("staging names the missing %1").arg(missingCase.reported)));
	}
	ok &= expect(stage({QStringLiteral("Qt5Gui.dll"), QStringLiteral("Qt5Core.dll")}, {})
		== missingMessage(QStringLiteral("Qt5Core.dll")),
		"staging names the first missing file in floor order");

	ok &= expect(stage({libzmq}, {}, {QStringLiteral("libzmq-mt-5_0_0.dll")}).isNull(),
		"the libzmq pattern accepts another version name");
	ok &= expect(stage({libzmq}, {}, {QStringLiteral("zmq.dll")})
		== missingMessage(QStringLiteral("libzmq*.dll")),
		"the libzmq pattern needs the libzmq prefix");

	const QStringList listed = {QStringLiteral("QEGTRAIN.exe"), QStringLiteral("d3dcompiler_47.dll"),
		QStringLiteral("styles/qwindowsvistastyle.dll")};
	ok &= expect(stage({}, listed, {QStringLiteral("d3dcompiler_47.dll"),
		QStringLiteral("styles/qwindowsvistastyle.dll")}).isNull(),
		"a manifest list passes when every file is staged");
	ok &= expect(stage({}, listed, {QStringLiteral("d3dcompiler_47.dll")})
		== missingMessage(QStringLiteral("styles/qwindowsvistastyle.dll")),
		"staging names the first manifest file that is missing");
	ok &= expect(stage({}, {QStringLiteral("Scenes")}) == missingMessage(QStringLiteral("Scenes")),
		"a manifest entry must be a file");

	const QString noExecutable = QDir(temp.path()).filePath(QStringLiteral("no-executable"));
	QDir().mkpath(noExecutable);
	error.clear();
	ok &= expect(!WindowsStaging::buildStage(noExecutable,
		QDir(temp.path()).filePath(QStringLiteral("staged-no-executable")), {}, &error)
		&& error == QStringLiteral("The Windows update package does not contain QEGTRAIN.exe."),
		"staging rejects a package without QEGTRAIN.exe");

	return ok ? 0 : 1;
}
