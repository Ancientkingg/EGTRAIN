#include "diagrams/DiagramWindow.h"
#include "diagrams/TrainFilterButton.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QScreen>
#include <QCategoryAxis>
#include <QLabel>
#include <QKeyEvent>
#include <QImage>
#include <QLineEdit>
#include <QMenu>
#include <QStyle>
#include <QStyleOptionViewItem>
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

// Check box rectangle of one train filter row, in viewport coordinates.
static QRect checkBoxRect(const QListWidget* list, const QListWidgetItem* item) {
	QStyleOptionViewItem option;
	option.initFrom(list->viewport());
	option.widget = list;
	option.rect = list->visualItemRect(item);
	option.features = QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDisplay;
	option.checkState = item->checkState();
	option.text = item->text();
	if (!item->icon().isNull()) {
		option.features |= QStyleOptionViewItem::HasDecoration;
		option.icon = item->icon();
		option.decorationSize = item->icon().actualSize(list->iconSize().isValid() ? list->iconSize() : QSize(16, 16));
	}
	return list->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, list);
}

static void clickAt(QListWidget* list, const QPoint& point) {
	QMouseEvent press(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent release(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(list->viewport(), &press);
	QApplication::sendEvent(list->viewport(), &release);
}

static void pressKey(QWidget* widget, Qt::Key key, const QString& text = QString()) {
	QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
	QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
	QApplication::sendEvent(widget, &press);
	QApplication::sendEvent(widget, &release);
}

// Pixels inside one check box that differ clearly from the row background, at the pixel density of the image.
static int checkBoxPixels(const QImage& image, const QRect& box, const QRect& row) {
	const qreal ratio = image.devicePixelRatio();
	const int background = qGray(image.pixel(qRound((row.right() - 4) * ratio), qRound(row.center().y() * ratio)));
	int strong = 0;
	for (int y = qRound(box.top() * ratio); y < qRound((box.bottom() + 1) * ratio); ++y)
		for (int x = qRound(box.left() * ratio); x < qRound((box.right() + 1) * ratio); ++x)
			strong += std::abs(qGray(image.pixel(x, y)) - background) >= 90 ? 1 : 0;
	return strong;
}

// The train filter popup: rows and their check boxes, toggling by mouse and keyboard, search,
// All and None, reopening, and the train ids it reports.
static bool exerciseTrainFilter() {
	TrainFilterButton filter;
	int changes = 0;
	QObject::connect(&filter, &TrainFilterButton::selectionChanged, [&changes]() { ++changes; });
	QMenu* menu = filter.menu();
	auto* list = menu ? menu->findChild<QListWidget*>() : nullptr;
	auto* search = menu ? menu->findChild<QLineEdit*>() : nullptr;
	QPushButton* all = nullptr;
	QPushButton* none = nullptr;
	if (menu) {
		for (QPushButton* button : menu->findChildren<QPushButton*>()) {
			if (button->text() == "All") all = button;
			if (button->text() == "None") none = button;
		}
	}
	if (!expect(list && search && all && none, "train filter has a list, a search field, All and None"))
		return false;
	bool ok = true;

	ok &= expect(filter.text() == "Trains" && filter.visibleTrainIds().isEmpty() && filter.isTrainVisible("IC 1000"),
		"an empty train filter shows no count and hides no train");
	menu->popup(QPoint(40, 40));
	QApplication::processEvents();
	ok &= expect(menu->isVisible() && list->count() == 0, "an empty train filter opens");
	menu->hide();

	filter.setTrains({{"IC 1000", QColor(Qt::red)}});
	ok &= expect(filter.text() == "Trains (1/1)" && filter.visibleTrainIds() == QStringList{"IC 1000"} && changes == 0,
		"one train is listed checked and setting trains is not a selection change");

	QVector<QPair<QString, QColor>> trains;
	QStringList ids;
	for (int i = 0; i < 120; ++i) {
		const QString id = QString("%1 %2").arg(i % 3 == 0 ? "IC" : "SPR").arg(1000 + i);
		ids.append(id);
		trains.append({id, i % 2 ? QColor(Qt::blue) : QColor()});
	}
	filter.setTrains(trains);
	ok &= expect(filter.text() == "Trains (120/120)" && filter.visibleTrainIds() == ids,
		"all trains start visible and are reported in list order");

	menu->popup(QPoint(40, 40));
	QApplication::processEvents();
	const QRect viewport = list->viewport()->rect();
	bool boxesInsideRows = true;
	int rowsOnScreen = 0;
	for (int row = 0; row < list->count(); ++row) {
		const QRect rowRect = list->visualItemRect(list->item(row));
		if (!viewport.contains(rowRect))
			continue;
		++rowsOnScreen;
		const QRect box = checkBoxRect(list, list->item(row));
		boxesInsideRows &= box.width() >= 12 && box.height() >= 12 && rowRect.contains(box) && viewport.contains(box.center());
	}
	ok &= expect(rowsOnScreen >= 8 && boxesInsideRows, "every row on screen has a check box inside the row");

	// Row 0 is checked, row 1 gets unchecked by a click; both are drawn at one and two device pixels per pixel.
	clickAt(list, checkBoxRect(list, list->item(1)).center());
	ok &= expect(
		list->item(1)->checkState() == Qt::Unchecked && changes == 1 && !filter.isTrainVisible(ids.at(1))
			&& filter.isTrainVisible(ids.at(0)) && filter.text() == "Trains (119/120)",
		"a click on a check box hides that train only");
	QApplication::processEvents();
	for (const qreal ratio : {1.0, 2.0}) {
		QImage image(list->viewport()->size() * ratio, QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(ratio);
		image.fill(Qt::white);
		list->viewport()->render(&image);
		const int checked = checkBoxPixels(image, checkBoxRect(list, list->item(0)), list->visualItemRect(list->item(0)));
		const int unchecked = checkBoxPixels(image, checkBoxRect(list, list->item(1)), list->visualItemRect(list->item(1)));
		ok &= expect(unchecked >= 24 * ratio && checked >= 2 * unchecked,
			"unchecked and checked boxes are both drawn in the train filter");
	}

	const QRect secondRow = list->visualItemRect(list->item(2));
	clickAt(list, QPoint(secondRow.right() - 6, secondRow.center().y()));
	ok &= expect(list->item(2)->checkState() == Qt::Checked && changes == 1, "a click beside the check box changes nothing");

	list->scrollToBottom();
	QApplication::processEvents();
	clickAt(list, checkBoxRect(list, list->item(119)).center());
	ok &= expect(
		list->item(119)->checkState() == Qt::Unchecked && list->item(1)->checkState() == Qt::Unchecked && changes == 2
			&& filter.text() == "Trains (118/120)",
		"a row reached by scrolling toggles and earlier choices stay");

	list->setFocus(Qt::MouseFocusReason);
	QApplication::processEvents();
	pressKey(list, Qt::Key_Home);
	pressKey(list, Qt::Key_Space, " ");
	ok &= expect(list->currentRow() == 0 && list->item(0)->checkState() == Qt::Unchecked && changes == 3,
		"Home and Space toggle the first row");
	pressKey(list, Qt::Key_Down);
	pressKey(list, Qt::Key_Space, " ");
	ok &= expect(list->item(1)->checkState() == Qt::Checked && list->item(0)->checkState() == Qt::Unchecked && changes == 4,
		"Down and Space toggle the next row");

	// Unchecked now: rows 0 (IC) and 119 (SPR).
	search->setText("ic");
	int shown = 0;
	bool onlyMatchesShown = true;
	for (int row = 0; row < list->count(); ++row) {
		const bool match = ids.at(row).startsWith("IC");
		onlyMatchesShown &= list->item(row)->isHidden() == !match;
		shown += list->item(row)->isHidden() ? 0 : 1;
	}
	ok &= expect(shown == 40 && onlyMatchesShown && changes == 4 && filter.visibleTrainIds().size() == 118,
		"search narrows the rows without changing which trains are visible");
	none->click();
	ok &= expect(
		changes == 5 && filter.visibleTrainIds().size() == 79 && filter.text() == "Trains (79/120)"
			&& filter.isTrainVisible(ids.at(1)) && !filter.isTrainVisible(ids.at(3))
			&& !filter.isTrainVisible(ids.at(119)),
		"None unchecks the rows that the search shows and reports one change");
	all->click();
	ok &= expect(
		changes == 6 && filter.visibleTrainIds().size() == 119 && filter.isTrainVisible(ids.at(0))
			&& !filter.isTrainVisible(ids.at(119)),
		"All checks the rows that the search shows and leaves the others");
	search->clear();
	bool noneHidden = true;
	for (int row = 0; row < list->count(); ++row)
		noneHidden &= !list->item(row)->isHidden();
	ok &= expect(noneHidden && changes == 6 && filter.visibleTrainIds().size() == 119,
		"clearing the search shows every row with its state");

	menu->hide();
	QApplication::processEvents();
	menu->popup(QPoint(40, 40));
	QApplication::processEvents();
	QStringList expected = ids;
	expected.removeAt(119);
	ok &= expect(
		menu->isVisible() && filter.visibleTrainIds() == expected && list->item(119)->checkState() == Qt::Unchecked
			&& filter.text() == "Trains (119/120)" && changes == 6,
		"reopening keeps the selection and the reported train ids");

	filter.setTrains({{"A", QColor()}, {"B", QColor()}});
	ok &= expect(
		filter.text() == "Trains (2/2)" && filter.visibleTrainIds() == QStringList({"A", "B"})
			&& filter.isTrainVisible("B") && changes == 6,
		"a new train list starts with every train visible");
	menu->hide();
	return ok;
}

// Inside a diagram window the chart shortcuts leave the keys of the open train filter alone.
static bool exerciseTrainFilterKeysInWindow() {
	DiagramWindow window("Train filter keys");
	auto* chart = new QChart;
	QLineSeries* first = nullptr;
	for (const char* id : {"A", "B", "C"}) {
		auto* series = new QLineSeries;
		series->setName(QString("Train %1").arg(id));
		series->setProperty("trainId", id);
		series->append(10, 10);
		series->append(60, 60);
		chart->addSeries(series);
		if (!first) first = series;
	}
	chart->createDefaultAxes();
	window.setChart(chart);
	window.show();
	QApplication::processEvents();
	auto* view = window.findChild<QChartView*>();
	auto* filter = window.findChild<TrainFilterButton*>();
	QMenu* menu = filter ? filter->menu() : nullptr;
	auto* list = menu ? menu->findChild<QListWidget*>() : nullptr;
	auto* x = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
	if (!expect(view && list && x && list->count() == 3, "a diagram window lists its three trains"))
		return false;
	bool ok = true;
	const double fullSpan = x->max() - x->min();
	view->setFocus();
	pressKey(view, Qt::Key_Plus, "+");
	const double zoomedSpan = x->max() - x->min();
	ok &= expect(zoomedSpan < fullSpan, "plus zooms the chart before the filter opens");

	menu->popup(window.mapToGlobal(QPoint(20, 60)));
	QApplication::processEvents();
	list->setFocus(Qt::MouseFocusReason);
	QApplication::processEvents();
	pressKey(list, Qt::Key_Down);
	pressKey(list, Qt::Key_End);
	ok &= expect(list->currentRow() == 2, "Down and End move to the last train of the open filter");
	pressKey(list, Qt::Key_Home);
	ok &= expect(list->currentRow() == 0 && x->max() - x->min() == zoomedSpan,
		"Home moves to the first train of the open filter and leaves the chart zoom alone");
	pressKey(list, Qt::Key_0, "0");
	pressKey(list, Qt::Key_Minus, "-");
	ok &= expect(x->max() - x->min() == zoomedSpan, "typing in the open filter list does not zoom the chart");
	pressKey(list, Qt::Key_Home);
	pressKey(list, Qt::Key_Space, " ");
	ok &= expect(
		list->item(0)->checkState() == Qt::Unchecked && filter->text() == "Trains (2/3)" && !first->isVisible()
			&& filter->visibleTrainIds() == QStringList({"B", "C"}),
		"Space in the open filter hides the first train of the diagram");
	menu->hide();
	QApplication::processEvents();

	view->setFocus();
	pressKey(view, Qt::Key_Home);
	ok &= expect(x->max() - x->min() == fullSpan && !first->isVisible(), "Home still resets the zoom with chart focus and keeps the filter");
	return ok;
}

int main(int argc, char* argv[]) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QApplication app(argc, argv);
	QFile stylesheet(QStringLiteral(EGTRAIN_DIALOG_QSS));
	if (!stylesheet.open(QIODevice::ReadOnly)) return 1;
	// The test binary has no Qt resources, so point the stylesheet at the icon files.
	QString styleSheet = QString::fromUtf8(stylesheet.readAll());
	styleSheet.replace(QStringLiteral(":/icons/"),
		QFileInfo(stylesheet).absolutePath() + QStringLiteral("/../icons/"));
	app.setStyleSheet(styleSheet);
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
	ok &= expect(
		tooltip->text().contains("Train A") && tooltip->text().contains("20.00") && tooltip->text().contains("30.00")
			&& !tooltip->text().contains("21.00"),
		"nearest plotted sample, not cursor values");
	ok &= expect(
		tooltip->text().contains("Delft") && tooltip->text().contains("Call: 1")
			&& !tooltip->text().contains("Leiden") && tooltip->text().contains("Speed (m/s)")
			&& tooltip->text().contains("Tractive effort (N)"),
		"identity, context and units");
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
	ok &= expect(
		tooltip->text().contains("Speed (m/s): 20.00")
			&& qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first())->labelFormat() == originalXFormat,
		"disabling time restores numeric title and format");
	auto* restoredX = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
	ok &= expect(
		restoredX == x && restoredX->tickType() == QValueAxis::TicksDynamic && restoredX->tickInterval() == 7
			&& restoredX->tickAnchor() == 2 && restoredX->tickCount() == 9 && restoredX->minorTickCount() == 2
			&& !restoredX->isGridLineVisible() && !restoredX->isMinorGridLineVisible(),
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
	ok &= expect(
		emptyTooltip->isVisible() && emptyTooltip->text().contains("B12") && !emptyTooltip->text().contains("150.00"),
		"block interior reports plotted corner and context");
	auto* events = new QScatterSeries;
	events->setName("Arrival");
	events->append(100, 300); events->append(140, 400);
	events->setProperty("inspectionPoints", QStringList{"Station: Delft | Arrival", "Station: Leiden | Departure"});
	emptyChart->addSeries(events);
	events->attachAxis(emptyX); events->attachAxis(emptyY);
	moveTo(emptyView, QPointF(120, 350), events);
	ok &= expect(!emptyTooltip->isVisible(), "scatter does not invent connecting line samples");
	moveTo(emptyView, QPointF(100, 300), events);
	ok &= expect(
		emptyTooltip->isVisible() && emptyTooltip->text().contains("Delft")
			&& !emptyTooltip->text().contains("Leiden"),
		"scatter event inspection uses matching context");
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
	ok &= expect(
		!input.findChild<QWidget*>("diagramDetailsPanel") && !input.findChild<QWidget*>("diagramDetailsButton")
			&& !input.findChild<QWidget*>("diagramDetailsText"),
		"technical-detail widgets are absent");
	for (const auto* button : input.findChildren<QPushButton*>())
		ok &= expect(button->text() != "Technical details", "no technical-details action");
	const QRect screenArea = input.screen()->availableGeometry();
	ok &= expect(input.width() <= screenArea.width() * 9 / 10 &&
		input.height() <= screenArea.height() * 4 / 5,
		"scaled diagram fits available screen with application QSS");
	ok &= expect(
		input.windowTitle() == "Input traction characteristic"
			&& input.findChild<QLabel*>("diagramContext")->text().contains("Case A / Scenario B")
			&& inputChart->title().contains(scientificWarning)
			&& input.findChild<QLabel*>("diagramContext")->isVisible()
			&& input.findChild<QLabel*>("diagramWarning")->text() == scientificWarning,
		"explicit presentation before setChart preserves identity and scientific warning with | in ID");
	ok &= expect(
		!input.findChild<TrainFilterButton*>()->isVisible()
			&& input.findChild<QLabel*>("diagramContext")->text().contains(authoredId)
			&& !input.findChild<QLabel*>("diagramNavigationHelp")->text().contains("Planned:")
			&& input.findChild<QLabel*>("diagramNavigationHelp")->text().contains("Input tractive effort"),
		"input traction uses rolling-stock subject and no visible train filter");
	input.setPresentation("Updated heading", "Case C / Scenario D", scientificWarning);
	app.processEvents();
	ok &= expect(
		input.windowTitle() == "Updated heading"
			&& input.findChild<QLabel*>("diagramContext")->text() == "Case C / Scenario D"
			&& input.findChild<QLabel*>("diagramWarning")->text() == scientificWarning
			&& inputChart->title().contains(scientificWarning),
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
		ok &= expect(
			label->text() == warning && label->isVisible() && label->height() >= label->heightForWidth(label->width())
				&& content.height() >= textBounds.height() && input.rect().contains(label->geometry())
				&& label->geometry().bottom() < context->geometry().top()
				&& context->height() <= context->fontMetrics().height() * 3 && (context->alignment() & Qt::AlignTop)
				&& input.width() <= 640 && input.height() <= input.maximumHeight(),
			"complete warning fits above capped long provenance at enlarged font in bounded narrow window");
	}
	input.setPresentation("No warning", longContext);
	ok &= expect(input.findChild<QLabel*>("diagramWarning")->isHidden(), "absent warning uses no strip");
	window.setPresentation("No context", QString());
	ok &= expect(window.findChild<QLabel*>("diagramContext")->isHidden(), "empty context stays hidden");
	ok &= exerciseTrainFilter();
	ok &= exerciseTrainFilterKeysInWindow();
	if (!ok) return 1;
	std::cout << "all DiagramWindow tests passed\n";
	return 0;
}
