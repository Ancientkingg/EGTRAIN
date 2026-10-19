#ifndef CANNOTOPENMESSAGE_H
#define CANNOTOPENMESSAGE_H

#include <QDir>
#include <QFileInfo>
#include <QString>

#include "widgets/DisplayPath.h"

/// The message for a case study that is damaged or is not a case study. The window and the start of the program show
/// the same title and text; the internal error follows the plain sentence. A folder gets its own sentence, which names
/// the scene.json that a scene folder holds.
namespace cannot_open {

/// The title of the message box.
inline QString title() {
	return QStringLiteral("Cannot Open Case Study");
}

/// The text for the case at `path`: its name, the sentence for a file or a folder, then `error` after a blank line
/// when it is not empty.
inline QString text(const QString& path, const QString& error) {
	const QFileInfo info(path);
	const QString name = info.fileName();
	QString sentence = QStringLiteral("The file is damaged or is not an EGTRAIN case study.");
	if (info.isDir()) {
		sentence = QDir(path).exists(QStringLiteral("scene.json"))
			? QStringLiteral("The folder holds a damaged or incomplete EGTRAIN scene.")
			: QStringLiteral("The folder holds no EGTRAIN scene. A scene folder holds a scene.json file.");
	}
	QString message = QStringLiteral("%1 could not be opened.\n%2").arg(name.isEmpty() ? display_path::format(path) : name, sentence);
	if (!error.isEmpty())
		message += QStringLiteral("\n\n") + error;
	return message;
}

}

#endif
