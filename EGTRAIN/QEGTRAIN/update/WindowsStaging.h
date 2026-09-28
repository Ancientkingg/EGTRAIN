#ifndef EGTRAIN_WINDOWS_STAGING_H
#define EGTRAIN_WINDOWS_STAGING_H

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

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

inline bool completeWindowsRuntime(const QString& directory) {
	const QDir root(directory);
	return QFileInfo(root.filePath(QStringLiteral("QEGTRAIN.exe"))).isFile()
		&& QFileInfo(root.filePath(QStringLiteral("egtrain_update_helper.exe"))).isFile()
		&& QFileInfo(root.filePath(QStringLiteral("Qt5Network.dll"))).isFile()
		&& QFileInfo(root.filePath(QStringLiteral("platforms/qwindows.dll"))).isFile()
		&& QFileInfo(root.filePath(QStringLiteral("Scenes"))).isDir();
}

// Builds the staged installation at stagePath as an exact copy of the
// extracted release package at extractPath. The stage path must not exist so
// staging can never merge new content into old installation leftovers.
inline bool buildStage(const QString& extractPath, const QString& stagePath,
	QString* error = nullptr) {
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
	if (!completeWindowsRuntime(stagePath)) {
		if (error)
			*error = QStringLiteral("The Windows update package is missing required runtime files.");
		return false;
	}
	return true;
}

} // namespace WindowsStaging

#endif
