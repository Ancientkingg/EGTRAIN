#ifndef CANNOTOPENMESSAGE_H
#define CANNOTOPENMESSAGE_H

#include <QFileInfo>
#include <QString>

// The message for a case study that is damaged or is not a case study. The window and the start of the program show
// the same title and text; the internal error follows the plain sentence.
namespace cannot_open {

inline QString title() {
	return QStringLiteral("Cannot Open Case Study");
}

inline QString text(const QString& path, const QString& error) {
	const QString name = QFileInfo(path).fileName();
	QString message = QStringLiteral("%1 could not be opened.\nThe file is damaged or is not an EGTRAIN case study.")
						  .arg(name.isEmpty() ? path : name);
	if (!error.isEmpty())
		message += QStringLiteral("\n\n") + error;
	return message;
}

}

#endif
