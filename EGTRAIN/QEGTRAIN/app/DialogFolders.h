#ifndef DIALOGFOLDERS_H
#define DIALOGFOLDERS_H

#include "app/SceneSavePolicy.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>

// The folder in which a file dialog starts. A dialog that is given only a file name starts in the working directory,
// which is the installation folder when EGTRAIN is started from the Start Menu.
namespace dialog_folders {

// The path with the slashes of Windows turned into forward slashes and the dots resolved, so a path that arrives
// with backslashes compares in the same way as one that Qt spells with slashes.
inline QString normalized(QString path) {
	path.replace('\\', '/');
	if (path.startsWith(QLatin1String("//?/")))
		path.remove(0, 4);
	return path.isEmpty() ? path : QDir::cleanPath(path);
}

inline QString absolute(const QString& normalizedPath) {
	const bool rooted = normalizedPath.startsWith('/')
		|| (normalizedPath.size() >= 3 && normalizedPath[1] == ':' && normalizedPath[2] == '/');
	return rooted ? normalizedPath : QDir::cleanPath(QFileInfo(normalizedPath).absoluteFilePath());
}

// Whether the path is the installation folder, which holds the program and the example scenes, or lies inside it or
// inside a folder of installed scenes. Letter case and the kind of slash do not matter.
inline bool inInstallation(const QString& path, const QString& applicationDir) {
	const QString file = normalized(path);
	const QString program = normalized(applicationDir);
	if (file.isEmpty() || program.isEmpty())
		return false;
	return scene_save_policy::containsPath(absolute(program), absolute(file)) || isInstalledScenePath(file, program);
}

// The folder that holds the case at `casePath`, a scene folder or a bundle, or the empty string.
inline QString folderOfCase(const QString& casePath) {
	const QString file = absolute(normalized(casePath));
	const int slash = file.lastIndexOf('/');
	if (slash < 0)
		return {};
	if (slash == 0 || (slash == 2 && file[1] == ':'))
		return file.left(slash + 1);
	return file.left(slash);
}

// The folder that holds the open case when that lies outside the installation, otherwise Documents, otherwise the
// home folder. `casePath` is empty when no case is open.
inline QString startFolder(const QString& casePath, const QString& applicationDir, const QString& documentsDir, const QString& homeDir) {
	if (!casePath.isEmpty() && !inInstallation(casePath, applicationDir)) {
		const QString folder = folderOfCase(casePath);
		if (!folder.isEmpty())
			return folder;
	}
	return documentsDir.isEmpty() ? homeDir : documentsDir;
}

inline QString startFolder(const QString& casePath = QString()) {
	const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
	return startFolder(casePath, QCoreApplication::applicationDirPath(), QDir(documents).exists() ? documents : QString(), QDir::homePath());
}

// The folder with the installed example cases, or the empty string when there is none.
inline QString installedScenesFolder(const QString& applicationDir) {
	const QDir app(applicationDir);
	for (const QString& relative : {QStringLiteral("Scenes"), QStringLiteral("../Resources/Scenes"), QStringLiteral("../share/EGTRAIN/Scenes")}) {
		const QString folder = QDir::cleanPath(app.absoluteFilePath(relative));
		if (QDir(folder).exists())
			return folder;
	}
	return {};
}

// The folder in which the Open Case Study and Open Scene Folder dialogs start: the folder above the open case, or
// with no case open the example cases when `installedScenes` is given, otherwise the home folder. On macOS a native
// open dialog that started in the home folder made macOS ask for access to Desktop, Documents and Downloads before
// it could be used; one that starts inside the application bundle asks nothing.
inline QString openStartFolder(const QString& sceneDir, const QString& installedScenes, const QString& homeDir) {
	if (!sceneDir.isEmpty())
		return QFileInfo(sceneDir).absolutePath();
	return installedScenes.isEmpty() ? homeDir : installedScenes;
}

inline QString openStartFolder(const QString& sceneDir = QString()) {
#ifdef Q_OS_MACOS
	const QString installed = installedScenesFolder(QCoreApplication::applicationDirPath());
#else
	const QString installed;
#endif
	return openStartFolder(sceneDir, installed, QDir::homePath());
}

// The start path for a save dialog: the start folder with the suggested file name in it.
inline QString suggestedPath(const QString& fileName, const QString& casePath = QString()) {
	const QString folder = startFolder(casePath);
	return fileName.isEmpty() ? folder : QDir(folder).filePath(fileName);
}

}

#endif
