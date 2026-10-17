#pragma once

#include "widgets/DialogLayout.h"

#include <QChar>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QSize>
#include <QSizePolicy>
#include <QString>
#include <QWidget>

namespace EditorLayout {

// Label for data and sentences in an editor panel, such as a path, an id or an explanation. The
// text is plain and wraps at spaces. Layouts ignore the width hints of the label, so a word wider
// than the width its layout gives it is clipped at the right edge instead of widening the panel.
// The label has no role and stays visible when its text is empty. The caller adds it to a layout.
inline QLabel* wrappedLabel(const QString& text, QWidget* parent) {
	auto* label = new QLabel(parent);
	label->setTextFormat(Qt::PlainText);
	label->setWordWrap(true);
	QSizePolicy policy = label->sizePolicy();
	policy.setHorizontalPolicy(QSizePolicy::Ignored);
	label->setSizePolicy(policy);
	label->setText(text);
	return label;
}

// Label for the message of an editor panel that has nothing to show yet, for example when no
// entity is selected. The label is hidden and has the help role. Show a message with
// DialogLayout::setHelp(label, text), also after the panel is shown, and hide it again with an
// empty text. The caller adds it to a layout.
inline QLabel* emptyStateLabel(QWidget* parent) {
	auto* label = new QLabel(parent);
	DialogLayout::setHelp(label, QString());
	return label;
}

// Heading for a group of fields: bold and two points larger than the font of the parent when the
// heading is made, or three pixels larger when that font is given in pixels. The text is plain and
// wraps as in wrappedLabel. The caller adds it to a layout and sets any object name.
inline QLabel* sectionHeading(const QString& title, QWidget* parent) {
	QLabel* heading = wrappedLabel(title, parent);
	QFont font = heading->font();
	font.setBold(true);
	if (font.pointSizeF() > 0)
		font.setPointSizeF(font.pointSizeF() + 2.0);
	else if (font.pixelSize() > 0)
		font.setPixelSize(font.pixelSize() + 3);
	heading->setFont(font);
	return heading;
}

// The name with a zero-width space after each underscore. A wrapped label breaks a line at a space only, so
// a name such as Amsterdam_Hilversum_Student would be clipped at the edge of its panel. With the added characters
// it wraps between its parts. The characters are not drawn.
inline QString breakableName(QString name) {
	return name.replace(QLatin1Char('_'), QString(QLatin1Char('_')) + QChar(0x200B));
}

// Scroll area for the panel of a narrow rail dock. It scrolls up and down and never shows a horizontal scroll
// bar. Its minimum width is the minimum width of the panel plus the vertical scroll bar, so the dock cannot be
// made narrower than the panel needs and the scroll bar does not take room from the fields of the panel. The
// panel is resized with the area and has no frame.
class VerticalScrollArea : public QScrollArea {
public:
	explicit VerticalScrollArea(QWidget* parent = nullptr)
		: QScrollArea(parent) {
		setWidgetResizable(true);
		setFrameShape(QFrame::NoFrame);
		setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	}

	QSize minimumSizeHint() const override {
		QSize hint = QScrollArea::minimumSizeHint();
		if (const QWidget* panel = widget())
			hint.setWidth(panel->minimumSizeHint().width() + verticalScrollBar()->sizeHint().width() + 2 * frameWidth());
		return hint;
	}
};

} // namespace EditorLayout
