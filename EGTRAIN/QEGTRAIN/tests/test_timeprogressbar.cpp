#include "widgets/TimeProgressBar.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QImage>
#include <QMouseEvent>
#include <QToolTip>
#include <QWheelEvent>
#include <QWidget>
#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

// Counts the paint events of the widgets it watches.
class PaintCounter : public QObject {
public:
	int paints = 0;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override {
		if (event->type() == QEvent::Paint)
			++paints;
		return QObject::eventFilter(watched, event);
	}
};

// Runs the event loop until both counters have counted a paint, for at most two seconds.
static void waitForPaints(const PaintCounter& first, const PaintCounter& second) {
	QElapsedTimer waited;
	waited.start();
	while ((first.paints == 0 || second.paints == 0) && waited.elapsed() < 2000)
		QApplication::processEvents();
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

	// The shown tooltip follows the run.
	QHelpEvent help(QEvent::ToolTip, QPoint(10, 10), bar.mapToGlobal(QPoint(10, 10)));
	QApplication::sendEvent(&bar, &help);
	const bool tipShown = QToolTip::isVisible() && QToolTip::text() == "Step 8,000 of 8,000";
	bar.setProgress(3999, 8000, startOffset);
	ok &= expect(tipShown && QToolTip::isVisible() && QToolTip::text() == "Step 4,000 of 8,000",
		"the shown tooltip follows the step");
	QToolTip::hideText();

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

	// The fixed-pitch font gives every text the width of the widest. The text ends a gap before the right border.
	TimeProgressBar empty;
	empty.setStyleSheet("QProgressBar { border: 1px solid #c0c0c0; background: #ffffff; color: #000000; text-align: right; }"
						"QProgressBar::chunk { background: #000000; }");
	empty.resize(400, 28);
	empty.setProgress(0, 8000, startOffset);
	empty.show();
	QApplication::processEvents();
	const QImage emptyImage = empty.grab().toImage();
	int textRight = -1;
	for (int y = 0; y < emptyImage.height(); ++y)
		for (int x = 0; x < emptyImage.width(); ++x)
			if (qGray(emptyImage.pixel(x, y)) < 128)
				textRight = qMax(textRight, x);
	ok &= expect(textRight > 0 && emptyImage.width() - 2 - textRight >= 3,
		"at least 3 pixels lie between the text and the right border");

	// A new step paints nothing at once: a paint then would also draw the rest of the window that waits for one, as the
	// network view. The bar and the rest paint together from the event loop.
	QWidget window;
	window.resize(400, 80);
	auto* canvas = new QWidget(&window);
	canvas->setGeometry(0, 0, 400, 40);
	auto* timeline = new TimeProgressBar(&window);
	timeline->setGeometry(0, 40, 400, 28);
	timeline->setProgress(0, 8000, startOffset);
	PaintCounter canvasPaints;
	PaintCounter timelinePaints;
	canvas->installEventFilter(&canvasPaints);
	timeline->installEventFilter(&timelinePaints);
	window.show();
	waitForPaints(canvasPaints, timelinePaints);
	ok &= expect(canvasPaints.paints > 0 && timelinePaints.paints > 0, "the window paints when shown");
	canvasPaints.paints = 0;
	timelinePaints.paints = 0;
	canvas->update();
	timeline->setProgress(100, 8000, startOffset);
	ok &= expect(canvasPaints.paints == 0 && timelinePaints.paints == 0,
		"a new step paints neither the bar nor the rest of the window at once");
	waitForPaints(canvasPaints, timelinePaints);
	ok &= expect(canvasPaints.paints > 0 && timelinePaints.paints > 0,
		"the bar and the rest of the window paint the new step from the event loop");

	return ok ? 0 : 1;
}
