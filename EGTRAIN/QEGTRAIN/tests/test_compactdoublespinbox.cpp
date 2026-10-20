#include "widgets/CompactDoubleSpinBox.h"

#include <QApplication>
#include <QHelpEvent>
#include <QPushButton>
#include <QToolTip>
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
	const auto check = [](bool condition, int line) {
		if (!condition)
			std::cerr << "failed at line " << line << "\n";
		return condition;
	};
	for (double input : {0.12345678901234567, -573.29999999999995, 1234567890.1234567}) {
		other.setFocus();
		QApplication::processEvents();
		spin.setValue(input);
		const double original = spin.value();
		ok &= spin.text() == (original >= 1e6 ? QString::number(original, 'f', 0) : QString::number(original, 'g', 6));
		spin.interpretText();
		ok &= spin.value() == original;
		spin.setFocus(Qt::TabFocusReason);
		QApplication::processEvents();
		ok &= spin.text() == QString::number(original, 'g', std::numeric_limits<double>::max_digits10);
		other.setFocus();
		QApplication::processEvents();
		ok &= spin.value() == original;
		ok &= spin.text() == (original >= 1e6 ? QString::number(original, 'f', 0) : QString::number(original, 'g', 6));
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

	// a value of twelve digits shows without exponent, and the tooltip has the value in full
	other.setFocus();
	QApplication::processEvents();
	spin.setPrefix(QString());
	spin.setSuffix(QString());
	spin.setValue(123456789012.0);
	ok &= check(spin.text() == "123456789012", __LINE__);
	spin.setValue(-987654321098.0);
	ok &= check(spin.text() == "-987654321098", __LINE__);
	spin.setValue(999999999999999.0);
	ok &= check(spin.text() == "999999999999999", __LINE__);
	spin.setValue(1e15);
	ok &= check(spin.text() == "1e+15", __LINE__);
	spin.setValue(1234567.5);
	ok &= check(spin.text() == "1234568", __LINE__);
	spin.setValue(999999.6);
	ok &= check(spin.text() == "1000000", __LINE__);
	spin.setValue(123456789012.0);
	spin.interpretText();
	ok &= check(spin.value() == 123456789012.0, __LINE__);
	spin.setValue(0.5);
	ok &= check(spin.text() == "0.5", __LINE__);

	// the field shows the full value in its tooltip only when the text is not it, after the tooltip it has
	// A visible tip stays up for a moment after it is hidden, so a marker shows whether the field showed a tip of its own.
	const auto shownToolTip = [&spin]() {
		const QString marker = QStringLiteral("marker");
		QToolTip::showText(spin.mapToGlobal(QPoint(2, 2)), marker, &spin);
		QHelpEvent help(QEvent::ToolTip, QPoint(2, 2), spin.mapToGlobal(QPoint(2, 2)));
		QApplication::sendEvent(&spin, &help);
		return QToolTip::text() == marker ? QString() : QToolTip::text();
	};
	spin.setValue(123456789012.5);
	ok &= check(shownToolTip() == "123456789012.5", __LINE__);
	spin.setValue(1234567.5);
	ok &= check(shownToolTip() == "1234567.5", __LINE__);
	spin.setPrefix("L: ");
	spin.setSuffix(" m");
	spin.setValue(1234567.5);
	ok &= check(shownToolTip() == "L: 1234567.5 m", __LINE__);
	spin.setToolTip("Length");
	ok &= check(shownToolTip() == "Length\nL: 1234567.5 m", __LINE__);
	spin.setPrefix(QString());
	spin.setSuffix(QString());
	spin.setToolTip(QString());
	for (double plainValue : {0.1, 22.2, -573.3, 0.0001, 1e-7, 1e15}) {
		spin.setValue(plainValue);
		ok &= check(shownToolTip().isEmpty(), __LINE__);
	}
	spin.setValue(1234.5678);
	ok &= check(shownToolTip() == "1234.5678", __LINE__);
	spin.setValue(0.123456789);
	ok &= check(shownToolTip() == "0.123456789", __LINE__);
	spin.setPrefix("L: ");
	spin.setSuffix(" m");
	spin.setToolTip("Length");
	spin.setValue(0.5);
	ok &= check(spin.text() == "L: 0.5 m" && shownToolTip() == "Length", __LINE__);
	QToolTip::hideText();
	if (!ok) std::cerr << "compact display, full-precision editing, large values or the tooltip failed\n";
	return ok ? 0 : 1;
}
