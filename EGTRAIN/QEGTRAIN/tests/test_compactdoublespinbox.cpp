#include "widgets/CompactDoubleSpinBox.h"

#include <QApplication>
#include <QPushButton>
#include <QVBoxLayout>
#include <iostream>

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	QWidget window;
	QVBoxLayout layout(&window);
	CompactDoubleSpinBox spin;
	QPushButton other("Other field");
	layout.addWidget(&spin);
	layout.addWidget(&other);
	spin.setDecimals(std::numeric_limits<double>::max_digits10);
	spin.setRange(-1e15, 1e15);
	spin.setKeyboardTracking(false);
	window.show();
	window.activateWindow();
	bool ok = true;
	for (double input : {0.12345678901234567, -573.29999999999995, 1234567890.1234567}) {
		other.setFocus();
		QApplication::processEvents();
		spin.setValue(input);
		const double original = spin.value();
		ok &= spin.text() == QString::number(original, 'g', 6);
		spin.interpretText();
		ok &= spin.value() == original;
		spin.setFocus(Qt::TabFocusReason);
		QApplication::processEvents();
		ok &= spin.text() == QString::number(original, 'g', std::numeric_limits<double>::max_digits10);
		other.setFocus();
		QApplication::processEvents();
		ok &= spin.value() == original;
		ok &= spin.text() == QString::number(original, 'g', 6);
	}
	spin.setFocus();
	QApplication::processEvents();
	auto* edit = spin.findChild<QLineEdit*>();
	edit->setText("12.3456789012345");
	edit->setModified(true);
	other.setFocus();
	QApplication::processEvents();
	ok &= spin.value() == 12.3456789012345;
	edit->setText("111.25");
	edit->setFocus();
	QApplication::processEvents();
	spin.interpretText();
	ok &= spin.value() == 111.25;
	other.setFocus();
	QApplication::processEvents();
	spin.setPrefix("Speed: ");
	spin.setSuffix(" km/h");
	spin.setValue(12.3456789012345);
	const double decoratedValue = spin.value();
	spin.interpretText();
	ok &= spin.value() == decoratedValue;
	spin.setValue(decoratedValue);
	spin.setFocus(Qt::TabFocusReason);
	QApplication::processEvents();
	ok &= spin.text() == "Speed: " + QString::number(decoratedValue, 'g', std::numeric_limits<double>::max_digits10) + " km/h";
	other.setFocus();
	QApplication::processEvents();
	ok &= spin.value() == decoratedValue;
	ok &= spin.text() == "Speed: " + QString::number(decoratedValue, 'g', 6) + " km/h";
	if (!ok) std::cerr << "compact display, full-precision editing or unchanged focus roundtrip failed\n";
	return ok ? 0 : 1;
}
