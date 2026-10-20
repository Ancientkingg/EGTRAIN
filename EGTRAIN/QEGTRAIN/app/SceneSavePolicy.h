#ifndef SCENESAVEPOLICY_H
#define SCENESAVEPOLICY_H

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace scene_save_policy {
// Resolve the deepest existing ancestor, retaining any not-yet-created suffix.
// A dangling or unreadable existing entry has no resolved spelling.
inline QString resolvedPath(QString path) {
	QStringList suffix;
	for (;;) {
		const QFileInfo info(path);
		const QString canonical = info.canonicalFilePath();
		// Qt on Windows can return the target spelling of a dangling link.
		if (!canonical.isEmpty() && QFileInfo::exists(canonical))
			return QDir::cleanPath(canonical + (suffix.isEmpty() ? QString() : "/" + suffix.join('/')));
		if (info.exists() || info.isSymLink())
			return {};
		const QString parent = info.absolutePath();
		if (parent == path)
			return {};
		suffix.prepend(info.fileName());
		path = parent;
	}
}

inline bool containsPath(const QString& root, const QString& path) {
	return !root.isEmpty() && !path.isEmpty()
		&& (path.compare(root, Qt::CaseInsensitive) == 0 || path.startsWith(root.endsWith('/') ? root : root + '/', Qt::CaseInsensitive));
}
}

// Only application-relative installed namespaces are protected, not CWD/Scenes.
// Case-only siblings are deliberately protected even on case-sensitive volumes.
// An installed-root symlink also protects its target, including an aliased checkout.
inline bool isInstalledScenePath(const QString& path, const QString& applicationDir) {
	if (path.isEmpty() || applicationDir.isEmpty())
		return false;
	const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
	const QString resolved = scene_save_policy::resolvedPath(absolute);
	const QDir app(applicationDir);
	for (const QString& relative : {QStringLiteral("Scenes"), QStringLiteral("../Resources/Scenes"), QStringLiteral("../share/EGTRAIN/Scenes")}) {
		const QString root = QDir::cleanPath(app.absoluteFilePath(relative));
		const QString resolvedRoot = scene_save_policy::resolvedPath(root);
		if (scene_save_policy::containsPath(root, absolute) || scene_save_policy::containsPath(root, resolved)
			|| scene_save_policy::containsPath(resolvedRoot, absolute) || scene_save_policy::containsPath(resolvedRoot, resolved))
			return true;
	}
	return false;
}

#endif
