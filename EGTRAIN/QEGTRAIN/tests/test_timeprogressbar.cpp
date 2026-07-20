#include "widgets/TimeProgressBar.h"

#include <QApplication>
#include <QFontDatabase>
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
	ok &= expect(bar.format() == "06:28:20 | Step 1 of 8,000 | Ends 08:41:40",
		"first timestep text is exact");

	bar.setProgress(3999, 8000, startOffset);
	ok &= expect(bar.value() == 3999, "middle timestep value is zero-based");
	ok &= expect(bar.format() == "07:34:59 | Step 4,000 of 8,000 | Ends 08:41:40",
		"middle timestep text is exact");

	bar.setProgress(7999, 8000, startOffset);
	ok &= expect(bar.value() == 7999, "last timestep value is zero-based");
	ok &= expect(bar.format() == "08:41:39 | Step 8,000 of 8,000 | Ends 08:41:40",
		"last timestep text is exact");
	ok &= expect(bar.height() == 28 && bar.focusPolicy() == Qt::NoFocus,
		"timeline has fixed height and no focus");
	ok &= expect(bar.font().family() == QFontDatabase::systemFont(QFontDatabase::FixedFont).family(),
		"timeline uses the system fixed-width font for clock and step columns");
	ok &= expect(bar.font().fixedPitch(),
		"timeline font has fixed character widths");

	const int beforeInput = bar.value();
	QMouseEvent mouse(QEvent::MouseButtonPress, QPointF(10.0, 10.0), Qt::LeftButton,
		Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(&bar, &mouse);
	QWheelEvent wheel(QPointF(10.0, 10.0), QPointF(10.0, 10.0), QPoint(0, 0), QPoint(0, 120),
		Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QApplication::sendEvent(&bar, &wheel);
	ok &= expect(bar.value() == beforeInput, "mouse and wheel input do not scrub the timeline");

	return ok ? 0 : 1;
}
