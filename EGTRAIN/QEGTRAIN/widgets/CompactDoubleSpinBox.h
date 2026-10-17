#pragma once

#include <QDoubleSpinBox>
#include <QEvent>
#include <QFocusEvent>
#include <QHelpEvent>
#include <QLineEdit>
#include <QLocale>
#include <QToolTip>
#include <cmath>
#include <limits>

#include "widgets/FocusWheel.h"

// Compact when browsing, full precision when editing. Merely visiting a field
// must not commit its rounded display representation to the canonical model. The wheel changes the value
// only while the field has keyboard focus. The compact text has six significant digits, except that a value from
// 1e6 up to 1e15 shows all its integer digits without exponent. While the field is not focused, the tooltip shows
// the value with its fewest exact digits, after the tooltip of the field, when the compact text is not it.
class CompactDoubleSpinBox : public QDoubleSpinBox {
public:
	explicit CompactDoubleSpinBox(QWidget* parent = nullptr)
		: QDoubleSpinBox(parent) {
		FocusWheel::guard(this);
	}

	QString textFromValue(double value) const override {
		return hasFocus() ? fullText(value) : compactText(value);
	}

	double valueFromText(const QString& text) const override {
		if (!lineEdit()->isModified()
			&& (isCurrentText(text, compactText(value())) || isCurrentText(text, fullText(value()))))
			return value();
		return QDoubleSpinBox::valueFromText(text);
	}

protected:
	bool event(QEvent* event) override {
		if (event->type() == QEvent::ToolTip && !hasFocus()) {
			const QString shown = shortestValueText();
			if (shown != text()) {
				const QString base = toolTip();
				QToolTip::showText(static_cast<QHelpEvent*>(event)->globalPos(),
					base.isEmpty() ? shown : base + QLatin1Char('\n') + shown, this);
				return true;
			}
		}
		return QDoubleSpinBox::event(event);
	}

	void focusInEvent(QFocusEvent* focusEvent) override {
		if (!lineEdit()->isModified() && isCurrentText(lineEdit()->text(), compactText(value())))
			lineEdit()->setText(prefix() + fullText(value()) + suffix());
		QDoubleSpinBox::focusInEvent(focusEvent);
	}

	void focusOutEvent(QFocusEvent* focusEvent) override {
		QDoubleSpinBox::focusOutEvent(focusEvent);
		lineEdit()->setText(prefix() + textFromValue(value()) + suffix());
	}

private:
	// The value with the fewest digits that read back as the same number, with the prefix and the suffix of the field.
	// From 1e-4 up to 1e15 it has no exponent.
	QString shortestValueText() const {
		const double number = value();
		const double magnitude = std::fabs(number);
		const bool plain = magnitude >= 1e-4 && magnitude < 1e15;
		return prefix() + QString::number(number, plain ? 'f' : 'g', QLocale::FloatingPointShortest) + suffix();
	}

	static QString fullText(double value) {
		return QString::number(value, 'g', std::numeric_limits<double>::max_digits10);
	}

	static QString compactText(double value) {
		const double magnitude = std::fabs(value);
		if (magnitude >= 1e6 && magnitude < 1e15)
			return QString::number(value, 'f', 0);
		return QString::number(value, 'g', 6);
	}

	bool isCurrentText(const QString& text, const QString& number) const {
		return text == number || text == prefix() + number + suffix();
	}
};
