#pragma once

#include "widgets/DialogLayout.h"

#include <QChar>
#include <QDockWidget>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QMainWindow>
#include <QObject>
#include <QScrollArea>
#include <QScrollBar>
#include <QSize>
#include <QSizePolicy>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <limits>

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

// Scroll area for the panel of an editor dock. QScrollArea keeps the size hint of the panel from the first time it was
// asked, which can be before the panel shows its tabs and tables, so the hint of this area is at least the width that the
// panel needs now, with the vertical scroll bar. As in QScrollArea the hint is at most 36 line heights wide.
// While the dock is docked in a main window, the hint is also at most five eighths of the width that the docks on the left
// of the window leave, but not under 12 line heights. A dock with large text then opens narrower than its panel, which
// scrolls inside it, and the network canvas keeps the rest.
class DockScrollArea : public QScrollArea {
public:
	explicit DockScrollArea(QWidget* parent = nullptr)
		: QScrollArea(parent) {}

	QSize sizeHint() const override {
		QSize hint = QScrollArea::sizeHint();
		const QWidget* panel = widget();
		if (!panel)
			return hint;
		const int lineHeight = fontMetrics().height();
		const int needed = panel->minimumSizeHint().width() + verticalScrollBar()->sizeHint().width() + 2 * frameWidth();
		hint.setWidth(std::min(std::max(hint.width(), needed), 36 * lineHeight));
		hint.setWidth(std::min(hint.width(), std::max(widthLimitInWindow(), 12 * lineHeight)));
		return hint;
	}

	// A change of the layout of the panel also changes the size hint of this area, which a dock keeps until told.
	bool eventFilter(QObject* watched, QEvent* event) override {
		if (watched == widget() && event->type() == QEvent::LayoutRequest)
			updateGeometry();
		return QScrollArea::eventFilter(watched, event);
	}

private:
	// The widest that the dock opens in its main window; unlimited while the dock floats.
	int widthLimitInWindow() const {
		const auto* dock = qobject_cast<const QDockWidget*>(parentWidget());
		const auto* window = dock && !dock->isFloating() ? qobject_cast<const QMainWindow*>(dock->parentWidget()) : nullptr;
		if (!window)
			return std::numeric_limits<int>::max();
		int beside = window->width();
		for (QDockWidget* other : window->findChildren<QDockWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
			if (other != dock && other->isVisible() && !other->isFloating() && window->dockWidgetArea(other) == Qt::LeftDockWidgetArea)
				beside -= other->width();
		}
		return beside * 5 / 8;
	}
};

} // namespace EditorLayout
