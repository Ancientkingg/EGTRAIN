#pragma once

#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QScreen>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QToolTip>
#include <QtGlobal>

#include "widgets/FocusWheel.h"

/// Combo box for choice fields in dialogs whose entries can be long imported names.
/// The field can shrink below its text so that a form never needs a horizontal scroll
/// bar, while its size hint still lets the dialog grow to show the whole text. A text
/// wider than the field is drawn with its middle elided. The tooltip is the current
/// text, followed by the tooltip of the field when it has one, and the popup list is as wide as its widest entry
/// within the screen. The wheel changes the choice only while the field has keyboard focus.
class ChoiceComboBox : public QComboBox {
public:
	/// A combo box that expands across its row and fits its size hint to its entries.
	explicit ChoiceComboBox(QWidget* parent = nullptr)
		: QComboBox(parent) {
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		setSizeAdjustPolicy(AdjustToContents);
		view()->setTextElideMode(Qt::ElideMiddle);
		FocusWheel::guard(this);
	}

	/// The current text as the field draws it.
	QString displayText() const {
		QStyleOptionComboBox option;
		initStyleOption(&option);
		const int width = style()->subControlRect(QStyle::CC_ComboBox, &option,
									 QStyle::SC_ComboBoxEditField, this)
							  .width();
		// The size hint reserves the bounding width of a text, which can be less than its advance.
		if (option.fontMetrics.boundingRect(option.currentText).width() <= width)
			return option.currentText;
		return option.fontMetrics.elidedText(option.currentText, Qt::ElideMiddle, width);
	}

	/// Room for about 12 characters, or the size hint when that is smaller.
	QSize minimumSizeHint() const override {
		const QSize hint = QComboBox::sizeHint();
		QStyleOptionComboBox option;
		initStyleOption(&option);
		const QFontMetrics metrics(font());
		const QSize contents(metrics.horizontalAdvance(QLatin1Char('x')) * 12, metrics.height());
		const int width = style()->sizeFromContents(QStyle::CT_ComboBox, &option, contents, this).width();
		return QSize(qMin(width, hint.width()), hint.height());
	}

	/// Opens the list as wide as its widest entry, at most nine tenths of the screen width, and keeps it on the screen.
	void showPopup() override {
		QScreen* target = QGuiApplication::screenAt(mapToGlobal(rect().center()));
		if (!target)
			target = QGuiApplication::primaryScreen();
		const QRect available = target ? target->availableGeometry() : QRect();
		const int budget = available.isValid() ? available.width() * 9 / 10 : QWIDGETSIZE_MAX;
		QAbstractItemView* list = view();
		// Qt sizes the popup from its items and the top padding that the field has now, but keeps above and below the list the
		// room that it took from the padding when the list was made. The style sheet gives a field with focus 1 px less
		// padding, so the list is brought up to date first; else a field with focus opens a list that scrolls.
		QEvent styleChange(QEvent::StyleChange);
		QCoreApplication::sendEvent(list->window(), &styleChange);
		list->setMinimumWidth(qMin(budget, qMax(width(), list->sizeHintForColumn(0) + 2 * list->frameWidth() + list->verticalScrollBar()->sizeHint().width())));
		QComboBox::showPopup();

		// The popup is as wide as the combo by default; keep it within the budget and the screen.
		QWidget* popup = list->window();
		QRect geometry = popup->geometry();
		geometry.setWidth(qMin(geometry.width(), budget + popup->width() - list->width()));
		if (available.isValid()) {
			if (geometry.right() > available.right())
				geometry.moveRight(available.right());
			if (geometry.left() < available.left())
				geometry.moveLeft(available.left());
		}
		popup->setGeometry(geometry);
	}

protected:
	/// Shows the current text as the tooltip, with the tooltip of the field below it when it has one.
	bool event(QEvent* event) override {
		// The tip reads the current text when it is shown, so a selection made while
		// signals are blocked cannot leave an old text behind.
		if (event->type() == QEvent::ToolTip) {
			const QString tip = toolTip().isEmpty() ? currentText() : currentText() + QLatin1Char('\n') + toolTip();
			QToolTip::showText(static_cast<QHelpEvent*>(event)->globalPos(), tip, this);
			return true;
		}
		return QComboBox::event(event);
	}

	/// Draws the field with displayText().
	void paintEvent(QPaintEvent*) override {
		QStylePainter painter(this);
		QStyleOptionComboBox option;
		initStyleOption(&option);
		painter.drawComplexControl(QStyle::CC_ComboBox, option);
		option.currentText = displayText();
		painter.drawControl(QStyle::CE_ComboBoxLabel, option);
	}
};
