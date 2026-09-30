#include "diagrams/DiagramWindow.h"
#include "diagrams/TrainFilterButton.h"

#include <QApplication>
#include <QFile>
#include <QScreen>
#include <QCategoryAxis>
#include <QLabel>
#include <QKeyEvent>
#include <QGestureEvent>
#include <QPinchGesture>
#include <QShortcut>
#include <QScatterSeries>
#include <QLineSeries>
#include <QAreaSeries>
#include <QListWidget>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPushButton>
#include <QPointer>
#include <QValueAxis>
#include <QWheelEvent>
#include <cmath>
#include <iostream>

QT_CHARTS_USE_NAMESPACE

static bool expect(bool condition, const char* message) {
	if (!condition) std::cerr << "failed: " << message << "\n";
	return condition;
}

static void moveTo(QChartView* view, const QPointF& value, QAbstractSeries* series) {
	const QPoint position = view->mapFromScene(view->chart()->mapToScene(view->chart()->mapToPosition(value, series)));
	QMouseEvent event(QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(view->viewport(), &event);
}

static void wheel(QChartView* view, QPoint pixels, QPoint angles, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
	QPointF chartPoint = {}) {
	if (chartPoint.isNull()) chartPoint = view->chart()->plotArea().center();
	const QPoint point = view->mapFromScene(view->chart()->mapToScene(chartPoint));
	QWheelEvent event(point, view->viewport()->mapToGlobal(point), pixels, angles,
		Qt::NoButton, modifiers, Qt::ScrollUpdate, false);
	QApplication::sendEvent(view->viewport(), &event);
}

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	QFile stylesheet(QStringLiteral(EGTRAIN_DIALOG_QSS));
	if (!stylesheet.open(QIODevice::ReadOnly)) return 1;
	app.setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
	bool ok = true;
	DiagramWindow window("Diagram navigation");
	auto* chart = new QChart;
	auto* series = new QLineSeries;
	series->setName("Train A (planned arrival)");
	series->setProperty("trainId", "A");
	series->setProperty("inspectionPoints", QStringList{"Station: Delft | Call: 1 | Simulated arrival", "Station: Leiden | Call: 2 | Simulated arrival"});
	series->append(20, 30);
	series->append(40, 50);
	chart->addSeries(series);
	auto* other = new QLineSeries;
	other->setName("Train B");
	other->setProperty("trainId", "B");
	other->append(70, 20);
	other->append(80, 30);
	chart->addSeries(other);
	chart->createDefaultAxes();
	auto* x = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
	auto* y = qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first());
	x->setTitleText("Speed (m/s)");
	y->setTitleText("Tractive effort (N)");
	x->setRange(0, 100);
	y->setRange(0, 100);
	x->setTickType(QValueAxis::TicksDynamic);
	x->setTickAnchor(2);
	x->setTickInterval(7);
	x->setTickCount(9);
	x->setMinorTickCount(2);
	x->setGridLineVisible(false);
	x->setMinorGridLineVisible(false);
	const QString originalXFormat = x->labelFormat();
	window.setChart(chart);
	window.show();
	app.processEvents();
	auto* view = window.findChild<QChartView*>();
	auto* tooltip = window.findChild<QLabel*>("diagramTooltip");
	moveTo(view, QPointF(21, 31), series);
	ok &= expect(tooltip->isVisible(), "near line shows tooltip through real mouse event");
	ok &= expect(tooltip->text().contains("Train A") && tooltip->text().contains("20.00") &&
		tooltip->text().contains("30.00") && !tooltip->text().contains("21.00"), "nearest plotted sample, not cursor values");
	ok &= expect(tooltip->text().contains("Delft") && tooltip->text().contains("Call: 1") &&
		!tooltip->text().contains("Leiden") && tooltip->text().contains("Speed (m/s)") &&
		tooltip->text().contains("Tractive effort (N)"), "identity, context and units");
	ok &= expect(window.rect().contains(tooltip->geometry()), "tooltip contained in window");
	moveTo(view, QPointF(40, 50), series);
	ok &= expect(tooltip->text().contains("Leiden") && !tooltip->text().contains("Delft"),
		"context follows the nearest appended point");
	moveTo(view, QPointF(80, 80), series);
	ok &= expect(!tooltip->isVisible(), "empty plot clears tooltip");
	moveTo(view, QPointF(20, 30), series);
	QEvent leave(QEvent::Leave);
	QApplication::sendEvent(view->viewport(), &leave);
	ok &= expect(!tooltip->isVisible(), "leave clears tooltip");
	ok &= expect(series->pen().style() == Qt::DashLine, "planned style preserved");

	const QPointF anchor = chart->plotArea().topLeft() + QPointF(chart->plotArea().width() * 0.25, chart->plotArea().height() * 0.35);
	wheel(view, {}, QPoint(0, 120), Qt::ControlModifier, anchor);
	ok &= expect(x->max() - x->min() < 100, "modifier wheel zoom");
	ok &= expect(std::abs(x->min() + 0.25 * (x->max() - x->min()) - 25) < 0.2,
		"off-center pointer remains anchored during zoom");
	const double before = x->min();
	const double span = x->max() - x->min();
	const double plotWidth = chart->plotArea().width();
	wheel(view, QPoint(7, 0), {});
	ok &= expect(std::abs(x->min() - (before - 7 * span / plotWidth)) < 1e-8, "pixel-only wheel pans smoothly");
	const double stationary = x->min();
	wheel(view, {}, {});
	ok &= expect(x->min() == stationary, "zero wheel delta is inert");
	wheel(view, {}, QPoint(120, 0), Qt::ControlModifier);
	ok &= expect(x->max() - x->min() < span, "horizontal-only modifier angle zooms");
	const double horizontalAngleSpan = x->max() - x->min();
	wheel(view, QPoint(0, 8), {}, Qt::ControlModifier);
	ok &= expect(x->max() - x->min() < horizontalAngleSpan, "pixel-only modifier zooms");
	const double beforeMeta = x->max() - x->min();
	wheel(view, {}, QPoint(0, 120), Qt::MetaModifier);
	ok &= expect(x->max() - x->min() < beforeMeta, "Meta wheel zooms");
	const double beforeShift = x->min();
	wheel(view, {}, QPoint(0, 120), Qt::ShiftModifier);
	ok &= expect(x->min() < beforeShift, "Shift wheel pans horizontally");
	const QPoint center = view->mapFromScene(chart->mapToScene(chart->plotArea().center()));
	QNativeGestureEvent gesture(Qt::ZoomNativeGesture, nullptr, center, center,
		view->viewport()->mapToGlobal(center), 0.2, 0, 0);
	const double beforeNative = x->max() - x->min();
	QApplication::sendEvent(view->viewport(), &gesture);
	ok &= expect(std::abs((x->max() - x->min()) - beforeNative / 1.2) < 1e-8,
		"native pinch applies one relative scale factor");

	QPinchGesture pinch;
	pinch.setChangeFlags(QPinchGesture::ScaleFactorChanged);
	pinch.setScaleFactor(1.1);
	pinch.setCenterPoint(center);
	QGestureEvent pinchEvent({&pinch});
	const double nativeSpan = x->max() - x->min();
	QApplication::sendEvent(view->viewport(), &pinchEvent);
	ok &= expect(std::abs(x->max() - x->min() - nativeSpan / 1.1) < 1e-8, "Qt pinch scale event");
	QMetaObject::invokeMethod(&window, "resetZoom");
	wheel(view, QPoint(100000, 100000), {});
	ok &= expect(x->min() >= 0 && y->min() >= 0 && x->max() <= 100 && y->max() <= 100,
		"pan stays inside original bounds");
	QMetaObject::invokeMethod(&window, "resetZoom");
	view->setFocus();
	QKeyEvent plus(QEvent::KeyPress, Qt::Key_Plus, Qt::NoModifier, "+");
	QApplication::sendEvent(view, &plus);
	ok &= expect(x->max() - x->min() < 100, "keyboard plus zooms with chart focus");
	const double beforeArrow = x->min();
	QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
	QApplication::sendEvent(view, &right);
	ok &= expect(x->min() > beforeArrow, "keyboard arrow pans with chart focus");
	auto* filter = window.findChild<TrainFilterButton*>();
	if (filter) filter->setFocus();
	const double beforeFilterKey = x->max() - x->min();
	QKeyEvent filterPlus(QEvent::KeyPress, Qt::Key_Plus, Qt::NoModifier, "+");
	if (filter) QApplication::sendEvent(filter, &filterPlus);
	ok &= expect(filter && x->max() - x->min() == beforeFilterKey,
		"chart shortcut does not take filter focus");
	view->setFocus();
	QKeyEvent home(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
	QApplication::sendEvent(view, &home);
	ok &= expect(x->min() == 0 && x->max() == 100, "Home restores full bounds");

	QString selected;
	QObject::connect(&window, &DiagramWindow::trainSelected, [&selected](const QString& id) { selected = id; });
	const QPoint sample = view->mapFromScene(chart->mapToScene(chart->mapToPosition(QPointF(20, 30), series)));
	QMouseEvent press(QEvent::MouseButtonPress, sample, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent release(QEvent::MouseButtonRelease, sample, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(view->viewport(), &press);
	QApplication::sendEvent(view->viewport(), &release);
	ok &= expect(selected == "A", "actual click retains train linkage");
	auto* list = window.findChild<QListWidget*>();
	ok &= expect(list && list->count() == 2, "train grouping");
	if (list) list->item(1)->setCheckState(Qt::Unchecked);
	ok &= expect(!other->isVisible() && !tooltip->isVisible(), "filter hides series and tooltip");
	QMetaObject::invokeMethod(&window, "resetZoom");
	ok &= expect(x->min() == 0 && x->max() == 100 && y->min() == 0 && y->max() == 100,
		"reset restores caller bounds rather than data bounds");
	ok &= expect(!other->isVisible() && selected == "A" && series->pen().widthF() > 2,
		"reset preserves filter and pin");

	y->setReverse(true);
	window.setTimeAxisY(true, 8 * 3600);
	app.processEvents();
	y = qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first());
	ok &= expect(y->isReverse(), "time conversion preserves reversed axis");
	auto* clockY = qobject_cast<QCategoryAxis*>(chart->axes(Qt::Vertical).first());
	ok &= expect(clockY && clockY->categoriesLabels().contains("08:00:00"),
		"elapsed zero has a real tick with a nonzero clock offset");
	moveTo(view, QPointF(20, 30), series);
	ok &= expect(tooltip->isVisible() && tooltip->text().contains("08:00:30") && tooltip->text().contains("20.00"),
		"time Y formats sample time and leaves X numeric");
	wheel(view, {}, QPoint(0, 120), Qt::ControlModifier);
	const double reversedBefore = y->min();
	wheel(view, QPoint(0, 5), {});
	ok &= expect(y->min() < reversedBefore, "pixel pan respects reversed time axis");
	QMetaObject::invokeMethod(&window, "resetZoom");
	ok &= expect(y->min() == 0 && y->max() == 100 && y->isReverse(), "time Y reset retains full bounds and reversal");
	moveTo(view, QPointF(20, 30), series);
	window.setTimeAxisX(true, 8 * 3600);
	app.processEvents();
	auto* clockX = qobject_cast<QCategoryAxis*>(chart->axes(Qt::Horizontal).first());
	ok &= expect(clockX && !clockX->categoriesLabels().isEmpty(), "time X has clock labels");
	if (clockX) {
		const QStringList labelsBefore = clockX->categoriesLabels();
		wheel(view, {}, QPoint(0, 120), Qt::ControlModifier);
		ok &= expect(!clockX->categoriesLabels().isEmpty() && clockX->categoriesLabels() != labelsBefore,
			"time ticks rebuild on zoom");
		QMetaObject::invokeMethod(&window, "resetZoom");
		window.setChart(chart);
		ok &= expect(chart->axes(Qt::Horizontal).first() == clockX,
			"setting same timed chart leaves axis and ownership intact");
	}
	moveTo(view, QPointF(20, 30), series);
	ok &= expect(tooltip->text().contains("08:00:20") && tooltip->text().contains("Tractive effort (N): 30.00"),
		"switch from time Y to time X restores numeric Y");
	window.setTimeAxisX(false);
	app.processEvents();
	moveTo(view, QPointF(20, 30), series);
	ok &= expect(tooltip->text().contains("Speed (m/s): 20.00") &&
		qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first())->labelFormat() == originalXFormat,
		"disabling time restores numeric title and format");
	auto* restoredX = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
	ok &= expect(restoredX == x && restoredX->tickType() == QValueAxis::TicksDynamic &&
		restoredX->tickInterval() == 7 && restoredX->tickAnchor() == 2 &&
		restoredX->tickCount() == 9 && restoredX->minorTickCount() == 2 &&
		!restoredX->isGridLineVisible() && !restoredX->isMinorGridLineVisible(),
		"time roundtrip retains original numeric axis and nondefault tick/grid settings");
	window.close();
	ok &= expect(!tooltip->isVisible(), "close clears tooltip");
	DiagramWindow empty("Empty numeric chart");
	auto* emptyChart = new QChart;
	auto* emptyX = new QValueAxis;
	auto* emptyY = new QValueAxis;
	emptyX->setRange(0, 160);
	emptyY->setRange(0, 500);
	emptyChart->addAxis(emptyX, Qt::AlignBottom);
	emptyChart->addAxis(emptyY, Qt::AlignLeft);
	empty.setChart(emptyChart);
	empty.show();
	app.processEvents();
	auto* emptyView = empty.findChild<QChartView*>();
	wheel(emptyView, {}, QPoint(0, 120), Qt::ControlModifier);
	for (auto* button : empty.findChildren<QPushButton*>())
		if (button->text() == "Reset zoom") button->click();
	ok &= expect(emptyX->min() == 0 && emptyX->max() == 160 && emptyY->max() == 500,
		"no-data non-time reset retains caller ranges");

	auto* rectangle = new QLineSeries;
	rectangle->setName("Block occupation");
	rectangle->setProperty("inspectionInterval", "Resource: B12 | Type: block | Start: 20 s | End: 60 s");
	rectangle->setProperty("inspectionFilled", true);
	rectangle->append(20, 100); rectangle->append(60, 100);
	rectangle->append(60, 200); rectangle->append(20, 200); rectangle->append(20, 100);
	emptyChart->addSeries(rectangle);
	rectangle->attachAxis(emptyX); rectangle->attachAxis(emptyY);
	moveTo(emptyView, QPointF(40, 150), rectangle);
	auto* emptyTooltip = empty.findChild<QLabel*>("diagramTooltip");
	ok &= expect(emptyTooltip->isVisible() && emptyTooltip->text().contains("B12") &&
		!emptyTooltip->text().contains("150.00"), "block interior reports plotted corner and context");
	auto* events = new QScatterSeries;
	events->setName("Arrival");
	events->append(100, 300); events->append(140, 400);
	events->setProperty("inspectionPoints", QStringList{"Station: Delft | Arrival", "Station: Leiden | Departure"});
	emptyChart->addSeries(events);
	events->attachAxis(emptyX); events->attachAxis(emptyY);
	moveTo(emptyView, QPointF(120, 350), events);
	ok &= expect(!emptyTooltip->isVisible(), "scatter does not invent connecting line samples");
	moveTo(emptyView, QPointF(100, 300), events);
	ok &= expect(emptyTooltip->isVisible() && emptyTooltip->text().contains("Delft") &&
		!emptyTooltip->text().contains("Leiden"), "scatter event inspection uses matching context");
	rectangle->setProperty("inspectionFilled", false);
	moveTo(emptyView, QPointF(40, 150), rectangle);
	ok &= expect(!emptyTooltip->isVisible(), "closed outline without fill does not claim interior");
	// Samples just below the plot must not count as visible strokes.
	auto* clipped = new QLineSeries;
	clipped->setName("Clipped line");
	clipped->append(20, -4); clipped->append(60, -4);
	emptyChart->addSeries(clipped);
	clipped->attachAxis(emptyX); clipped->attachAxis(emptyY);
	moveTo(emptyView, QPointF(40, 2), clipped);
	ok &= expect(!emptyTooltip->isVisible(), "fully clipped line cannot be inspected inside plot");
	const QPoint clippedClick = emptyView->mapFromScene(emptyChart->mapToScene(
		emptyChart->mapToPosition(QPointF(40, 2), clipped)));
	QMouseEvent clippedPress(QEvent::MouseButtonPress, clippedClick, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent clippedRelease(QEvent::MouseButtonRelease, clippedClick, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QString clippedSelection;
	QObject::connect(&empty, &DiagramWindow::trainSelected, [&clippedSelection](const QString& id) { clippedSelection = id; });
	QApplication::sendEvent(emptyView->viewport(), &clippedPress);
	QApplication::sendEvent(emptyView->viewport(), &clippedRelease);
	ok &= expect(clippedSelection.isEmpty(), "fully clipped line cannot be selected");
	clipped->replace(0, QPointF(20, -4));
	clipped->replace(1, QPointF(60, 40));
	moveTo(emptyView, QPointF(30, 7), clipped);
	ok &= expect(emptyTooltip->isVisible() && emptyTooltip->text().contains("Clipped line"),
		"line crossing plot edge remains inspectable");
	QPointer<QChart> oldChart = emptyChart;
	empty.setChart(emptyChart);
	ok &= expect(emptyView->chart() == emptyChart, "setting same chart retains ownership");
	auto* replacement = new QChart;
	empty.setChart(replacement);
	ok &= expect(oldChart.isNull() && !emptyTooltip->isVisible(), "chart replacement deletes old chart and clears tooltip");
	DiagramWindow dual("Linked vertical axes");
	auto* dualChart = new QChart;
	auto* dualSeries = new QLineSeries;
	dualSeries->append(0, 0); dualSeries->append(100, 100);
	dualChart->addSeries(dualSeries);
	auto* dualX = new QValueAxis;
	auto* dualY = new QValueAxis;
	auto* dualCategory = new QCategoryAxis;
	dualChart->addAxis(dualX, Qt::AlignBottom);
	dualChart->addAxis(dualY, Qt::AlignLeft);
	dualChart->addAxis(dualCategory, Qt::AlignRight);
	dualSeries->attachAxis(dualX);
	dualSeries->attachAxis(dualY);
	dualSeries->attachAxis(dualCategory);
	dualX->setRange(0, 100);
	dualY->setRange(0, 100);
	dualCategory->setRange(0, 100);
	dual.setChart(dualChart);
	dual.show();
	app.processEvents();
	auto* dualView = dual.findChild<QChartView*>();
	wheel(dualView, {}, QPoint(0, 120), Qt::ControlModifier);
	const double expectedSpan = 100 / std::exp(0.2);
	ok &= expect(std::abs((dualY->max() - dualY->min()) - expectedSpan) < 0.01 &&
		std::abs((dualCategory->max() - dualCategory->min()) - expectedSpan) < 0.01,
		"numeric and category Y axes zoom exactly once from original ranges");
	// Route charts keep singleton events visible and allow zero/negative station X.
	DiagramWindow route("Route chart");
	auto* routeChart = new QChart;
	auto* singleEvent = new QLineSeries;
	singleEvent->setName("A (planned arrival)");
	singleEvent->setProperty("trainId", "A");
	singleEvent->setPointsVisible(true);
	singleEvent->append(-2, 30);
	routeChart->addSeries(singleEvent);
	auto* distance = new QValueAxis;
	auto* elapsed = new QValueAxis;
	elapsed->setReverse(true);
	distance->setRange(-3, 1);
	elapsed->setRange(0, 60);
	routeChart->addAxis(distance, Qt::AlignBottom);
	routeChart->addAxis(elapsed, Qt::AlignLeft);
	singleEvent->attachAxis(distance);
	singleEvent->attachAxis(elapsed);
	auto* stationAxis = new QCategoryAxis;
	stationAxis->setStartValue(-4); // Must precede negative and zero append values.
	stationAxis->append("Central [one] / Central [two]", -2);
	stationAxis->append("Zero [zero]", 0);
	stationAxis->setRange(-3, 1);
	routeChart->addAxis(stationAxis, Qt::AlignTop);
	singleEvent->attachAxis(stationAxis);
	route.setChart(routeChart);
	route.setTimeAxisY(true, 8 * 3600);
	route.show();
	app.processEvents();
	ok &= expect(singleEvent->pointsVisible() && singleEvent->count() == 1
		&& stationAxis->categoriesLabels().size() == 2
		&& stationAxis->categoriesLabels().first().contains("[two]")
		&& stationAxis->categoriesLabels().last().contains("Zero"),
		"isolated event and distinct colocated station IDs remain visible");
	auto* routeClock = qobject_cast<QCategoryAxis*>(routeChart->axes(Qt::Vertical).first());
	ok &= expect(routeClock && routeClock->isReverse()
		&& routeClock->categoriesLabels().contains("08:00:00"),
		"route elapsed zero tick and downward time orientation");
	DiagramWindow staircase("Calculated envelope");
	auto* stairChart = new QChart;
	auto* lower = new QLineSeries;
	auto* upper = new QLineSeries;
	lower->append(1, 10); lower->append(2, 10);
	upper->append(1, 20); upper->append(2, 20);
	auto* area = new QAreaSeries(upper, lower);
	area->setName("A envelope"); area->setProperty("trainId", "A");
	area->setBrush(QColor(60, 100, 200, 65));
	stairChart->addSeries(area);
	auto* outline = new QLineSeries;
	outline->setName("A envelope"); outline->setProperty("trainId", "A");
	outline->setProperty("inspectionFilled", true);
	outline->setProperty("inspectionInterval", "Calculated blocking envelope");
	for (const QPointF& p : {QPointF(1, 10), QPointF(2, 10), QPointF(2, 20), QPointF(1, 20), QPointF(1, 10)})
		outline->append(p);
	stairChart->addSeries(outline);
	auto* actual = new QLineSeries;
	actual->setName("A recorded trajectory"); actual->setProperty("trainId", "A");
	actual->append(1.2, 14); actual->append(1.8, 16);
	stairChart->addSeries(actual);
	auto* overlap = new QLineSeries;
	overlap->setName("B envelope"); overlap->setProperty("trainId", "B");
	overlap->setProperty("inspectionFilled", true);
	for (const QPointF& p : {QPointF(1, 10), QPointF(2, 10), QPointF(2, 20), QPointF(1, 20), QPointF(1, 10)})
		overlap->append(p);
	stairChart->addSeries(overlap);
	auto* otherActual = new QScatterSeries;
	otherActual->setName("B recorded trajectory"); otherActual->setProperty("trainId", "B");
	otherActual->append(1.7, 18);
	stairChart->addSeries(otherActual);
	stairChart->createDefaultAxes();
	auto* stairY = qobject_cast<QValueAxis*>(stairChart->axes(Qt::Vertical).first());
	stairY->setReverse(true); stairY->setRange(0, 30);
	staircase.setChart(stairChart); staircase.setTimeAxisY(true);
	staircase.show(); app.processEvents();
	moveTo(staircase.findChild<QChartView*>(), QPointF(1.5, 12), outline);
	auto* stairTip = staircase.findChild<QLabel*>("diagramTooltip");
	ok &= expect(stairTip->isVisible() && stairTip->text().contains("Calculated blocking envelope"),
		"filled chart-coordinate rectangle can be inspected in its interior");
	moveTo(staircase.findChild<QChartView*>(), QPointF(1.2, 14), actual);
	ok &= expect(stairTip->text().contains("A recorded trajectory"),
		"exact recorded sample takes precedence over overlapping filled interiors");
	moveTo(staircase.findChild<QChartView*>(), QPointF(1.7, 18), otherActual);
	ok &= expect(stairTip->text().contains("B recorded trajectory"),
		"overlapping trains select the nearby visible recorded sample");
	ok &= expect(stairChart->series().indexOf(actual) > stairChart->series().indexOf(area)
		&& stairY->isReverse(), "recorded layer draws over the downward-time area fill");
	const QString authoredId = QStringLiteral("Unit | ") + QString(120, QLatin1Char('Q'));
	const QString scientificWarning = QStringLiteral("Curve contains negative effort below the default 0 kN view");
	DiagramWindow input("Input traction characteristic: " + authoredId);
	QFont scaled = input.font();
	scaled.setPointSizeF(18);
	input.setFont(scaled);
	input.setProperty("inputTrainUnitId", QStringLiteral("Unit A"));
	auto* inputChart = new QChart;
	auto* inputLine = new QLineSeries;
	inputLine->setName("Input effort");
	inputLine->append(10, 20); inputLine->append(20, 30);
	inputChart->addSeries(inputLine);
	inputChart->createDefaultAxes();
	inputChart->setTitle(QStringLiteral("Input traction characteristic: ") + authoredId
		+ QStringLiteral("<br>") + scientificWarning);
	input.setPresentation("Input traction characteristic", "Case A / Scenario B | " + authoredId,
		scientificWarning);
	input.setChart(inputChart);
	input.show(); app.processEvents();
	ok &= expect(!input.findChild<QWidget*>("diagramDetailsPanel") &&
		!input.findChild<QWidget*>("diagramDetailsButton") &&
		!input.findChild<QWidget*>("diagramDetailsText"), "technical-detail widgets are absent");
	for (const auto* button : input.findChildren<QPushButton*>())
		ok &= expect(button->text() != "Technical details", "no technical-details action");
	const QRect screenArea = input.screen()->availableGeometry();
	ok &= expect(input.width() <= screenArea.width() * 9 / 10 &&
		input.height() <= screenArea.height() * 4 / 5,
		"scaled diagram fits available screen with application QSS");
	ok &= expect(input.windowTitle() == "Input traction characteristic" &&
		input.findChild<QLabel*>("diagramContext")->text().contains("Case A / Scenario B") &&
		inputChart->title().contains(scientificWarning) &&
		input.findChild<QLabel*>("diagramContext")->isVisible() &&
		input.findChild<QLabel*>("diagramWarning")->text() == scientificWarning,
		"explicit presentation before setChart preserves identity and scientific warning with | in ID");
	ok &= expect(!input.findChild<TrainFilterButton*>()->isVisible() &&
		input.findChild<QLabel*>("diagramContext")->text().contains(authoredId) &&
		!input.findChild<QLabel*>("diagramNavigationHelp")->text().contains("Planned:") &&
		input.findChild<QLabel*>("diagramNavigationHelp")->text().contains("Input tractive effort"),
		"input traction uses rolling-stock subject and no visible train filter");
	input.setPresentation("Updated heading", "Case C / Scenario D", scientificWarning);
	app.processEvents();
	ok &= expect(input.windowTitle() == "Updated heading" &&
		input.findChild<QLabel*>("diagramContext")->text() == "Case C / Scenario D" &&
		input.findChild<QLabel*>("diagramWarning")->text() == scientificWarning &&
		inputChart->title().contains(scientificWarning),
		"explicit presentation after setChart preserves distinct chart qualification");
	// Long provenance must never consume the space needed by scientific caveats.
	input.setMaximumWidth(qMin(640, input.maximumWidth()));
	input.resize(input.maximumWidth(), input.maximumHeight());
	const QString longContext = "Reference: " + QString(1500, QLatin1Char('R'))
		+ " | Case: " + QString(1500, QLatin1Char('C'));
	for (const QString& warning : {scientificWarning,
		QStringLiteral("Stop arrivals/departures and dwell, not continuous movement. Ambiguous/unmapped portions omitted; no extrapolation."),
		QStringLiteral("Calculated envelopes, not observed occupation; movement only within scope. Unmapped endpoints/events omitted: 2; incomplete/missing-clearance blocks omitted. No extrapolation."),
		QStringLiteral("Shifted calculated envelopes, not recorded movement. Unmapped/incomplete blocks omitted; no extrapolation.")}) {
		input.setPresentation("Scientific interpretation", longContext, warning);
		app.processEvents();
		const auto* label = input.findChild<QLabel*>("diagramWarning");
		const auto* context = input.findChild<QLabel*>("diagramContext");
		const QRect content = label->contentsRect();
		const QRect textBounds = label->fontMetrics().boundingRect(
			QRect(0, 0, content.width(), 10000), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, warning);
		ok &= expect(label->text() == warning && label->isVisible() &&
			label->height() >= label->heightForWidth(label->width()) &&
			content.height() >= textBounds.height() &&
			input.rect().contains(label->geometry()) &&
			label->geometry().bottom() < context->geometry().top() &&
			context->height() <= context->fontMetrics().height() * 3 &&
			(context->alignment() & Qt::AlignTop) && input.width() <= 640 &&
			input.height() <= input.maximumHeight(),
			"complete warning fits above capped long provenance at enlarged font in bounded narrow window");
	}
	input.setPresentation("No warning", longContext);
	ok &= expect(input.findChild<QLabel*>("diagramWarning")->isHidden(), "absent warning uses no strip");
	window.setPresentation("No context", QString());
	ok &= expect(window.findChild<QLabel*>("diagramContext")->isHidden(), "empty context stays hidden");
	if (!ok) return 1;
	std::cout << "all DiagramWindow tests passed\n";
	return 0;
}
