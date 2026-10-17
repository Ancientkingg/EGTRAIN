#ifndef CANNOTOPENMESSAGE_H
#define CANNOTOPENMESSAGE_H

#include <QDir>
#include <QFileInfo>
#include <QString>

// The message for a case study that is damaged or is not a case study. The window and the start of the program show
// the same title and text; the internal error follows the plain sentence. A folder gets its own sentence, which names
// the scene.json that a scene folder holds.
namespace cannot_open {

inline QString title() {
	return QStringLiteral("Cannot Open Case Study");
}

inline QString text(const QString& path, const QString& error) {
	const QFileInfo info(path);
	const QString name = info.fileName();
	QString sentence = QStringLiteral("The file is damaged or is not an EGTRAIN case study.");
	if (info.isDir()) {
		sentence = QDir(path).exists(QStringLiteral("scene.json"))
			? QStringLiteral("The scene.json in the folder is damaged or is not an EGTRAIN scene.")
			: QStringLiteral("The folder holds no EGTRAIN scene. A scene folder holds a scene.json file.");
	}
	QString message = QStringLiteral("%1 could not be opened.\n%2").arg(name.isEmpty() ? path : name, sentence);
	if (!error.isEmpty())
		message += QStringLiteral("\n\n") + error;
	return message;
}

}

#endif
