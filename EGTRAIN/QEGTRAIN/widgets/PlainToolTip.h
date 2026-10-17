#pragma once

#include <QString>
#include <QTextDocument>

// The tooltip that shows `text` as typed. Qt reads a tooltip as rich text when it looks like markup, so a
// path or an identifier with a tag name or "&lt;" in it would lose characters. Converting it to rich text
// by hand removes the guess. Spaces stay breakable, so a long text wraps. An empty text gives no tooltip.
inline QString plainToolTip(const QString& text) {
	return text.isEmpty() ? QString() : Qt::convertFromPlainText(text, Qt::WhiteSpaceNormal);
}
