#include "widgets/TimeProgressBar.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QImage>
#include <QMouseEvent>
#include <QScopeGuard>
#include <QTest>
#include <QToolTip>
#include <QWheelEvent>
#include <QWidget>

// The start of the timetable, 06:28:20, in seconds.
constexpr long long kStartOffset = 6 * 3600 + 28 * 60 + 20;

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

// The progress bar of a run shows the clock time and the step of the run, and paints a new step with the rest of the
// window.
class TestTimeProgressBar : public QObject {
	Q_OBJECT

private slots:
	void showsStep_data();
	void showsStep();
	void rangeIsZeroBased();
	void fixedHeightAndFont();
	void shownTooltipFollowsRun();
	void inputDoesNotScrub();
	void textOnFillIsLight();
	void textEndsBeforeBorder();
	void newStepPaintsFromEventLoop();
};

void TestTimeProgressBar::showsStep_data() {
	QTest::addColumn<int>("step");
	QTest::addColumn<QString>("text");
	QTest::addColumn<QString>("toolTip");
	QTest::newRow("first") << 0 << QStringLiteral("06:28:20 of 08:41:40") << QStringLiteral("Step 1 of 8,000");
	QTest::newRow("middle") << 3999 << QStringLiteral("07:34:59 of 08:41:40") << QStringLiteral("Step 4,000 of 8,000");
	QTest::newRow("last") << 7999 << QStringLiteral("08:41:39 of 08:41:40") << QStringLiteral("Step 8,000 of 8,000");
}

// The value of a step is zero-based, and its text and tooltip are exact.
void TestTimeProgressBar::showsStep() {
	QFETCH(int, step);
	QFETCH(QString, text);
	QFETCH(QString, toolTip);
	TimeProgressBar bar;
	bar.show();
	bar.setProgress(step, 8000, kStartOffset);
	QCOMPARE(bar.value(), step);
	QCOMPARE(bar.format(), text);
	QCOMPARE(bar.toolTip(), toolTip);
}

void TestTimeProgressBar::rangeIsZeroBased() {
	TimeProgressBar bar;
	bar.show();
	bar.setProgress(0, 8000, kStartOffset);
	// The timeline range is zero-based.
	QCOMPARE(bar.minimum(), 0);
	QCOMPARE(bar.maximum(), 7999);
}

void TestTimeProgressBar::fixedHeightAndFont() {
	TimeProgressBar bar;
	bar.show();
	bar.setProgress(7999, 8000, kStartOffset);
	// The timeline has a fixed height and no focus.
	QCOMPARE(bar.height(), 28);
	QCOMPARE(bar.focusPolicy(), Qt::NoFocus);
	// The timeline uses the system fixed-width font for the clock times.
	QCOMPARE(bar.font().family(), QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
	QVERIFY2(bar.font().fixedPitch(), "timeline font has fixed character widths");
}

// The shown tooltip follows the run.
void TestTimeProgressBar::shownTooltipFollowsRun() {
	TimeProgressBar bar;
	bar.show();
	bar.setProgress(7999, 8000, kStartOffset);
	QHelpEvent help(QEvent::ToolTip, QPoint(10, 10), bar.mapToGlobal(QPoint(10, 10)));
	QApplication::sendEvent(&bar, &help);
	const auto hideTip = qScopeGuard([] { QToolTip::hideText(); });
	// The shown tooltip follows the step.
	QVERIFY(QToolTip::isVisible());
	QCOMPARE(QToolTip::text(), QStringLiteral("Step 8,000 of 8,000"));
	bar.setProgress(3999, 8000, kStartOffset);
	QVERIFY(QToolTip::isVisible());
	QCOMPARE(QToolTip::text(), QStringLiteral("Step 4,000 of 8,000"));
}

void TestTimeProgressBar::inputDoesNotScrub() {
	TimeProgressBar bar;
	bar.show();
	bar.setProgress(3999, 8000, kStartOffset);
	const int beforeInput = bar.value();
	QMouseEvent mouse(QEvent::MouseButtonPress, QPointF(10.0, 10.0), bar.mapToGlobal(QPoint(10, 10)), Qt::LeftButton,
		Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(&bar, &mouse);
	QWheelEvent wheel(QPointF(10.0, 10.0), QPointF(10.0, 10.0), QPoint(0, 0), QPoint(0, 120),
		Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QApplication::sendEvent(&bar, &wheel);
	// Mouse and wheel input do not scrub the timeline.
	QCOMPARE(bar.value(), beforeInput);
}

// On a full bar the whole text lies on the fill, which is as dark as the text colour: the text is drawn light.
void TestTimeProgressBar::textOnFillIsLight() {
	TimeProgressBar full;
	full.setStyleSheet("QProgressBar { border: 0; color: #000000; selection-color: #ffffff; }"
					   "QProgressBar::chunk { background: #000000; }");
	full.resize(400, 28);
	full.setProgress(7999, 8000, kStartOffset);
	full.show();
	QApplication::processEvents();
	const QImage image = full.grab().toImage();
	int lightPixels = 0;
	for (int y = 0; y < image.height(); ++y)
		for (int x = 0; x < image.width(); ++x)
			lightPixels += qGray(image.pixel(x, y)) > 200 ? 1 : 0;
	// The text on the fill is drawn in the light text colour.
	QCOMPARE_GT(lightPixels, 50);
}

// The fixed-pitch font gives every text the width of the widest. The text ends a gap before the right border.
void TestTimeProgressBar::textEndsBeforeBorder() {
	TimeProgressBar empty;
	empty.setStyleSheet("QProgressBar { border: 1px solid #c0c0c0; background: #ffffff; color: #000000; text-align: right; }"
						"QProgressBar::chunk { background: #000000; }");
	empty.resize(400, 28);
	empty.setProgress(0, 8000, kStartOffset);
	empty.show();
	QApplication::processEvents();
	const QImage emptyImage = empty.grab().toImage();
	int textRight = -1;
	for (int y = 0; y < emptyImage.height(); ++y)
		for (int x = 0; x < emptyImage.width(); ++x)
			if (qGray(emptyImage.pixel(x, y)) < 128)
				textRight = qMax(textRight, x);
	// At least 3 pixels lie between the text and the right border.
	QCOMPARE_GT(textRight, 0);
	QCOMPARE_GE(emptyImage.width() - 2 - textRight, 3);
}

// A new step paints nothing at once: a paint then would also draw the rest of the window that waits for one, as the
// network view. The bar and the rest paint together from the event loop.
void TestTimeProgressBar::newStepPaintsFromEventLoop() {
	QWidget window;
	window.resize(400, 80);
	auto* canvas = new QWidget(&window);
	canvas->setGeometry(0, 0, 400, 40);
	auto* timeline = new TimeProgressBar(&window);
	timeline->setGeometry(0, 40, 400, 28);
	timeline->setProgress(0, 8000, kStartOffset);
	PaintCounter canvasPaints;
	PaintCounter timelinePaints;
	canvas->installEventFilter(&canvasPaints);
	timeline->installEventFilter(&timelinePaints);
	window.show();
	waitForPaints(canvasPaints, timelinePaints);
	// The window paints when shown.
	QCOMPARE_GT(canvasPaints.paints, 0);
	QCOMPARE_GT(timelinePaints.paints, 0);
	canvasPaints.paints = 0;
	timelinePaints.paints = 0;
	canvas->update();
	timeline->setProgress(100, 8000, kStartOffset);
	// A new step paints neither the bar nor the rest of the window at once.
	QCOMPARE(canvasPaints.paints, 0);
	QCOMPARE(timelinePaints.paints, 0);
	waitForPaints(canvasPaints, timelinePaints);
	// The bar and the rest of the window paint the new step from the event loop.
	QCOMPARE_GT(canvasPaints.paints, 0);
	QCOMPARE_GT(timelinePaints.paints, 0);
}

QTEST_MAIN(TestTimeProgressBar)
#include "test_timeprogressbar.moc"
