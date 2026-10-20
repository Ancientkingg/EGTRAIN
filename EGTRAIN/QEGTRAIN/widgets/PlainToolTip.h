#pragma once

#include <QChar>
#include <QString>
#include <QTextDocument>

// `text` with a zero-width space after every 32 characters of a run without a space or a line break. A tooltip
// wraps at a space only, so an identifier of 300 characters would be one line wider than the screen. The added
// characters are not drawn.
inline QString withBreakOpportunities(const QString& text) {
	constexpr int runLength = 32;
	QString result;
	result.reserve(text.size() + text.size() / runLength);
	int run = 0;
	for (int i = 0; i < text.size(); ++i) {
		const QChar c = text.at(i);
		result += c;
		run = c.isSpace() ? 0 : run + 1;
		const bool splitsPair = c.isHighSurrogate() && i + 1 < text.size();
		if (run >= runLength && !splitsPair) {
			result += QChar(0x200B);
			run = 0;
		}
	}
	return result;
}

// The tooltip that shows `text` as typed. Qt reads a tooltip as rich text when it looks like markup, so a
// path or an identifier with a tag name or "&lt;" in it would lose characters. Converting it to rich text
// by hand removes the guess. Spaces stay breakable, and so does a long run without one. An empty text gives
// no tooltip.
inline QString plainToolTip(const QString& text) {
	return text.isEmpty() ? QString() : Qt::convertFromPlainText(withBreakOpportunities(text), Qt::WhiteSpaceNormal);
}
