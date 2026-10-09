#ifndef EGTRAIN_WINDOWS_STAGING_H
#define EGTRAIN_WINDOWS_STAGING_H

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

// The staged Windows installation is assembled exclusively from the extracted
// release package: the release owns its runtime files and shipped canonical
// Scenes. Nothing is carried over from the installation being replaced, so a
// released package always produces the same result as a fresh installation.
namespace WindowsStaging {

inline bool copyTree(const QString& sourcePath, const QString& destinationPath) {
	const QFileInfo source(sourcePath);
	if (!source.isDir() || source.isSymLink())
		return false;
	if (!QDir().mkpath(destinationPath))
		return false;
	const QDir sourceDir(sourcePath);
	const QFileInfoList entries = sourceDir.entryInfoList(
		QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System,
		QDir::DirsFirst | QDir::Name);
	for (const QFileInfo& entry : entries) {
		if (entry.isSymLink())
			return false;
		const QString destination = QDir(destinationPath).filePath(entry.fileName());
		if (entry.isDir()) {
			if (!copyTree(entry.absoluteFilePath(), destination))
				return false;
			continue;
		}
		if (!entry.isFile())
			return false;
		QFile::remove(destination);
		if (!QFile::copy(entry.absoluteFilePath(), destination))
			return false;
	}
	return true;
}

// Files every Windows release package must contain, kept in step with the
// package verification of the Windows job in the release workflow. The
// libzmq DLL carries its version in the name and is matched separately.
inline QStringList requiredRuntimeFiles() {
	return {
		QStringLiteral("QEGTRAIN.exe"),
		QStringLiteral("egtrain_update_helper.exe"),
		QStringLiteral("Qt5Core.dll"),
		QStringLiteral("Qt5Gui.dll"),
		QStringLiteral("Qt5Widgets.dll"),
		QStringLiteral("Qt5Charts.dll"),
		QStringLiteral("Qt5Svg.dll"),
		QStringLiteral("Qt5Network.dll"),
		QStringLiteral("platforms/qwindows.dll"),
		QStringLiteral("imageformats/qsvg.dll")};
}

// True when the directory holds the required runtime files, the Scenes
// directory and every file named in manifestFiles. When it does not, missing
// receives the first absent entry.
inline bool completeWindowsRuntime(const QString& directory, const QStringList& manifestFiles,
	QString* missing = nullptr) {
	const QDir root(directory);
	const auto fail = [missing](const QString& name) {
		if (missing)
			*missing = name;
		return false;
	};
	for (const QString& name : requiredRuntimeFiles()) {
		if (!QFileInfo(root.filePath(name)).isFile())
			return fail(name);
	}
	if (root.entryList({QStringLiteral("libzmq*.dll")}, QDir::Files).isEmpty())
		return fail(QStringLiteral("libzmq*.dll"));
	if (!QFileInfo(root.filePath(QStringLiteral("Scenes"))).isDir())
		return fail(QStringLiteral("Scenes"));
	for (const QString& name : manifestFiles) {
		if (!QFileInfo(root.filePath(name)).isFile())
			return fail(name);
	}
	return true;
}

// Builds the staged installation at stagePath as an exact copy of the
// extracted release package at extractPath. The stage path must not exist so
// staging can never merge new content into old installation leftovers.
// manifestFiles lists the package files the release manifest promises.
inline bool buildStage(const QString& extractPath, const QString& stagePath,
	const QStringList& manifestFiles, QString* error = nullptr) {
	if (!QFileInfo(QDir(extractPath).filePath(QStringLiteral("QEGTRAIN.exe"))).isFile()) {
		if (error)
			*error = QStringLiteral("The Windows update package does not contain QEGTRAIN.exe.");
		return false;
	}
	if (QFileInfo(stagePath).exists()) {
		if (error)
			*error = QStringLiteral("The Windows update staging location is not empty.");
		return false;
	}
	if (!copyTree(extractPath, stagePath)) {
		if (error)
			*error = QStringLiteral("Could not finish staging the Windows update.");
		return false;
	}
	QString missing;
	if (!completeWindowsRuntime(stagePath, manifestFiles, &missing)) {
		if (error)
			*error = QStringLiteral("The Windows update package is missing a required file: %1.")
				.arg(missing);
		return false;
	}
	return true;
}

} // namespace WindowsStaging

#endif
