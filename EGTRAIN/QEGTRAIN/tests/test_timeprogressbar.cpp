#include "widgets/TimeProgressBar.h"

#include <QApplication>
#include <QFontDatabase>
#include <QImage>
#include <QMouseEvent>
#include <QWheelEvent>
#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	TimeProgressBar bar;
	const long long startOffset = 6 * 3600 + 28 * 60 + 20;
	bar.show();

	bool ok = true;
	bar.setProgress(0, 8000, startOffset);
	ok &= expect(bar.minimum() == 0 && bar.maximum() == 7999, "timeline range is zero-based");
	ok &= expect(bar.value() == 0, "first timestep is zero");
	ok &= expect(bar.format() == "06:28:20 of 08:41:40" && bar.toolTip() == "Step 1 of 8,000",
		"first timestep text and tooltip are exact");

	bar.setProgress(3999, 8000, startOffset);
	ok &= expect(bar.value() == 3999, "middle timestep value is zero-based");
	ok &= expect(bar.format() == "07:34:59 of 08:41:40" && bar.toolTip() == "Step 4,000 of 8,000",
		"middle timestep text and tooltip are exact");

	bar.setProgress(7999, 8000, startOffset);
	ok &= expect(bar.value() == 7999, "last timestep value is zero-based");
	ok &= expect(bar.format() == "08:41:39 of 08:41:40" && bar.toolTip() == "Step 8,000 of 8,000",
		"last timestep text and tooltip are exact");
	ok &= expect(bar.height() == 28 && bar.focusPolicy() == Qt::NoFocus,
		"timeline has fixed height and no focus");
	ok &= expect(bar.font().family() == QFontDatabase::systemFont(QFontDatabase::FixedFont).family(),
		"timeline uses the system fixed-width font for the clock times");
	ok &= expect(bar.font().fixedPitch(),
		"timeline font has fixed character widths");

	const int beforeInput = bar.value();
	QMouseEvent mouse(QEvent::MouseButtonPress, QPointF(10.0, 10.0), bar.mapToGlobal(QPoint(10, 10)), Qt::LeftButton,
		Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(&bar, &mouse);
	QWheelEvent wheel(QPointF(10.0, 10.0), QPointF(10.0, 10.0), QPoint(0, 0), QPoint(0, 120),
		Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QApplication::sendEvent(&bar, &wheel);
	ok &= expect(bar.value() == beforeInput, "mouse and wheel input do not scrub the timeline");

	// On a full bar the whole text lies on the fill, which is as dark as the text colour: the text is drawn light.
	TimeProgressBar full;
	full.setStyleSheet("QProgressBar { border: 0; color: #000000; selection-color: #ffffff; }"
					   "QProgressBar::chunk { background: #000000; }");
	full.resize(400, 28);
	full.setProgress(7999, 8000, startOffset);
	full.show();
	QApplication::processEvents();
	const QImage image = full.grab().toImage();
	int lightPixels = 0;
	for (int y = 0; y < image.height(); ++y)
		for (int x = 0; x < image.width(); ++x)
			lightPixels += qGray(image.pixel(x, y)) > 200 ? 1 : 0;
	ok &= expect(lightPixels > 50, "the text on the fill is drawn in the light text colour");

	return ok ? 0 : 1;
}
