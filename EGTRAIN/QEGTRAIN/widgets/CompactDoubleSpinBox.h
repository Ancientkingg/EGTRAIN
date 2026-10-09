#pragma once

#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QLineEdit>
#include <limits>

// Compact when browsing, full precision when editing. Merely visiting a field
// must not commit its rounded display representation to the canonical model.
class CompactDoubleSpinBox : public QDoubleSpinBox {
public:
	using QDoubleSpinBox::QDoubleSpinBox;

	QString textFromValue(double value) const override {
		return QString::number(value, 'g', hasFocus() ? std::numeric_limits<double>::max_digits10 : 6);
	}

	double valueFromText(const QString& text) const override {
		if (!lineEdit()->isModified()
			&& (isCurrentText(text, 6)
				|| isCurrentText(text, std::numeric_limits<double>::max_digits10)))
			return value();
		return QDoubleSpinBox::valueFromText(text);
	}

protected:
	void focusInEvent(QFocusEvent* event) override {
		if (!lineEdit()->isModified() && isCurrentText(lineEdit()->text(), 6))
			lineEdit()->setText(prefix()
				+ QString::number(value(), 'g', std::numeric_limits<double>::max_digits10) + suffix());
		QDoubleSpinBox::focusInEvent(event);
	}

	void focusOutEvent(QFocusEvent* event) override {
		QDoubleSpinBox::focusOutEvent(event);
		lineEdit()->setText(prefix() + textFromValue(value()) + suffix());
	}

private:
	bool isCurrentText(const QString& text, int precision) const {
		const QString number = QString::number(value(), 'g', precision);
		return text == number || text == prefix() + number + suffix();
	}
};
