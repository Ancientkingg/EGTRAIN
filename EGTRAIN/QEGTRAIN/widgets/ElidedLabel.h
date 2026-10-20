#pragma once

#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QStyle>

// Label for one sentence in a status bar. It can shrink below its text, so that a long
// sentence never widens the window, while its size hint still shows the whole text. A text
// wider than the label is drawn with its end elided; text() stays the whole sentence.
class ElidedLabel : public QLabel {
public:
	explicit ElidedLabel(QWidget* parent = nullptr)
		: QLabel(parent) {
		setTextFormat(Qt::PlainText);
	}

	// The text as the label draws it.
	QString displayText() const { return fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()); }

	QSize minimumSizeHint() const override { return QSize(0, QLabel::minimumSizeHint().height()); }

protected:
	void paintEvent(QPaintEvent*) override {
		QPainter painter(this);
		style()->drawItemText(&painter, contentsRect(), QStyle::visualAlignment(layoutDirection(), alignment()), palette(),
			isEnabled(), displayText(), foregroundRole());
	}
};
