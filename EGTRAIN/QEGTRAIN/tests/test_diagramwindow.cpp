#include "diagrams/DiagramWindow.h"
#include "diagrams/TrainFilterButton.h"
#include "scene/SectionLabels.h"
#include "widgets/PlainToolTip.h"

#include <QApplication>
#include <QClipboard>
#include <QGraphicsObject>
#include <QFontMetricsF>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMainWindow>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QScreen>
#include <QCategoryAxis>
#include <QLabel>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QKeyEvent>
#include <QImage>
#include <QLineEdit>
#include <QMenu>
#include <QStyle>
#include <QProxyStyle>
#include <QStyleOptionViewItem>
#include <QGestureEvent>
#include <QHelpEvent>
#include <QPinchGesture>
#include <QShortcut>
#include <QTextDocument>
#include <QToolTip>
#include <QScatterSeries>
#include <QLineSeries>
#include <QAreaSeries>
#include <QListWidget>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QRubberBand>
#include <QPointer>
#include <QScrollBar>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QtMath>
#include <QValueAxis>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <iostream>

static bool expect(bool condition, const char* message) {
	if (!condition) std::cerr << "failed: " << message << "\n";
	return condition;
}

static void moveTo(QChartView* view, const QPointF& value, QAbstractSeries* series) {
	const QPoint position = view->mapFromScene(view->chart()->mapToScene(view->chart()->mapToPosition(value, series)));
	QMouseEvent event(QEvent::MouseMove, position, view->viewport()->mapToGlobal(position), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
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
	QMouseEvent press(QEvent::MouseButtonPress, point, list->viewport()->mapToGlobal(point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent release(QEvent::MouseButtonRelease, point, list->viewport()->mapToGlobal(point), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
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

// On a screen without room for the whole list below or above the button, the list takes the larger room and scrolls, and the
// menu does not cover the button. Beside a tall button in the middle of the screen the list is lower than the default height of
// a list view.
static bool exerciseTrainFilterRoom() {
	QWidget window;
	auto* filter = new TrainFilterButton(&window);
	QVector<QPair<QString, QColor>> trains;
	for (int i = 0; i < 40; ++i)
		trains.append({QString("IC %1").arg(1000 + i), QColor()});
	filter->setTrains(trains);
	const QRect screen = window.screen()->availableGeometry();
	// A tall button in the middle of the screen leaves less room above and below it than the list needs.
	filter->setFixedHeight(160);
	window.setGeometry(screen.left(), screen.top() + screen.height() / 2 - 80, 200, 160);
	window.show();
	QApplication::processEvents();
	QRect menuArea;
	bool scrolls = false;
	QTimer::singleShot(0, filter, [filter, &menuArea, &scrolls]() {
		menuArea = filter->menu()->geometry();
		scrolls = filter->menu()->findChild<QListWidget*>()->verticalScrollBar()->maximum() > 0;
		filter->menu()->hide();
	});
	filter->showMenu();
	const QRect button(filter->mapToGlobal(QPoint(0, 0)), filter->size());
	return expect(screen.contains(menuArea) && !menuArea.intersects(button) && scrolls,
		"the train list fits the room beside its button and scrolls");
}

// A menu separator is a line in the border colour of the menus, also at fractional scales.
static bool exerciseMenuSeparator() {
	QMenu menu;
	menu.addAction("Undo");
	QAction* separator = menu.addSeparator();
	menu.addAction("Find");
	menu.ensurePolished();
	menu.resize(menu.sizeHint());
	const QRect line = menu.actionGeometry(separator);
	const QColor border(0xc9, 0xd3, 0xdb);
	bool ok = true;
	for (const qreal ratio : {1.0, 1.25, 1.5}) {
		QImage image(menu.size() * ratio, QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(ratio);
		image.fill(Qt::white);
		menu.render(&image);
		const int x = qRound(line.center().x() * ratio);
		bool drawn = false;
		for (int y = qFloor(line.top() * ratio); y <= qCeil(line.bottom() * ratio) && y < image.height(); ++y) {
			const QColor pixel = image.pixelColor(x, y);
			drawn |= std::abs(pixel.red() - border.red()) <= 6 && std::abs(pixel.green() - border.green()) <= 6
				&& std::abs(pixel.blue() - border.blue()) <= 6;
		}
		ok &= expect(drawn, qPrintable(QString("the menu separator draws its line at %1 times").arg(ratio)));
	}
	return ok;
}

class SelectionRequiredStyle : public QProxyStyle {
public:
	SelectionRequiredStyle() : QProxyStyle(QStringLiteral("Fusion")) {}

	int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
		const QWidget* widget = nullptr, QStyleHintReturn* data = nullptr) const override {
		if (hint == SH_ItemView_MovementWithoutUpdatingSelection) return false;
		return QProxyStyle::styleHint(hint, option, widget, data);
	}
};

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
	if (!expect(view && list && x && first && list->count() == 3, "a diagram window lists its three trains"))
		return false;
	// Require a selection for keyboard navigation, as the macOS style does.
	auto* navigationStyle = new SelectionRequiredStyle;
	navigationStyle->setParent(list);
	list->setStyle(navigationStyle);
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
	// The offscreen platform of Qt 6 leaves no window active when the popup closes.
	window.activateWindow();
	QApplication::processEvents();

	view->setFocus();
	pressKey(view, Qt::Key_Home);
	ok &= expect(x->max() - x->min() == fullSpan && !first->isVisible(), "Home still resets the zoom with chart focus and keeps the filter");
	return ok;
}

// Two trains with a solid and a dashed line each, and a fill for the first train.
struct ColoredChart {
	QChart* chart = new QChart;
	QLineSeries* solid[2] = {};
	QLineSeries* dashed[2] = {};
	QAreaSeries* fill = nullptr;
	QPen solidPen[2];
	QPen dashedPen[2];
	QPen fillPen;
	QBrush fillBrush;
};

static ColoredChart makeColoredChart() {
	ColoredChart built;
	const QColor base[2] = {QColor(200, 40, 40), QColor(30, 90, 200)};
	for (int train = 0; train < 2; ++train) {
		const QString id = train == 0 ? "A" : "B";
		const double offset = train * 60;
		built.solidPen[train] = QPen(base[train], 3.0);
		built.dashedPen[train] = QPen(base[train], 2.0, Qt::DashLine);
		auto* solid = new QLineSeries;
		solid->setName("Train " + id + " (simulated)");
		solid->setProperty("trainId", id);
		solid->setPen(built.solidPen[train]);
		solid->append(10, 10 + offset);
		solid->append(60, 20 + offset);
		auto* dashed = new QLineSeries;
		dashed->setName("Train " + id + " (planned)");
		dashed->setProperty("trainId", id);
		dashed->setPen(built.dashedPen[train]);
		dashed->append(10, 25 + offset);
		dashed->append(60, 30 + offset);
		built.chart->addSeries(solid);
		built.chart->addSeries(dashed);
		built.solid[train] = solid;
		built.dashed[train] = dashed;
	}
	auto* upper = new QLineSeries;
	auto* lower = new QLineSeries;
	upper->append(70, 50);
	upper->append(90, 50);
	lower->append(70, 40);
	lower->append(90, 40);
	built.fill = new QAreaSeries(upper, lower);
	built.fill->setProperty("trainId", "A");
	built.fillBrush = QBrush(QColor(200, 40, 40, 65));
	built.fillPen = QPen(QColor(200, 40, 40), 1.5);
	built.fill->setBrush(built.fillBrush);
	built.fill->setPen(built.fillPen);
	built.chart->addSeries(built.fill);
	built.chart->createDefaultAxes();
	built.chart->axes(Qt::Horizontal).first()->setRange(0, 100);
	built.chart->axes(Qt::Vertical).first()->setRange(0, 140);
	return built;
}

// A pen with the colour of a train: same width, dash pattern and cap as before.
static bool isRecoloured(const QPen& pen, const QPen& before, const QColor& color) {
	QPen expected = before;
	expected.setColor(color);
	return pen == expected;
}

static QColor swatchAt(const QListWidget* list, int row) {
	const QIcon icon = list->item(row)->icon();
	return icon.isNull() ? QColor() : icon.pixmap(12, 12).toImage().pixelColor(6, 6);
}

static bool exerciseTrainColors() {
	const QColor green(60, 180, 75);
	bool ok = true;
	for (const bool beforeChart : {false, true}) {
		const char* when = beforeChart ? " (map set before the chart)" : " (map set after the chart)";
		const auto note = [&](const char* text) { return (std::string(text) + when); };
		DiagramWindow window("Train colours");
		const ColoredChart built = makeColoredChart();
		const QHash<QString, QColor> colors{{"A", green}, {"Z", QColor(1, 2, 3)}, {"B", QColor()}};
		if (beforeChart) window.setTrainColors(colors);
		window.setChart(built.chart);
		if (!beforeChart) window.setTrainColors(colors);
		window.show();
		QApplication::processEvents();
		auto* list = window.findChild<TrainFilterButton*>()->menu()->findChild<QListWidget*>();
		auto* view = window.findChild<QChartView*>();
		if (!expect(list && view && list->count() == 2, "the coloured chart lists two trains"))
			return false;
		ok &= expect(isRecoloured(built.solid[0]->pen(), built.solidPen[0], green)
				&& isRecoloured(built.dashed[0]->pen(), built.dashedPen[0], green)
				&& built.solid[0]->pen().widthF() == 3.0 && built.dashed[0]->pen().widthF() == 2.0
				&& built.solid[0]->pen().style() == Qt::SolidLine && built.dashed[0]->pen().style() == Qt::DashLine,
			note("the line series of a coloured train take its colour and keep width and dash pattern").c_str());
		ok &= expect(built.fill->brush().color() == QColor(green.red(), green.green(), green.blue(), 65)
				&& built.fill->pen().color() == green && built.fill->pen().widthF() == 1.5,
			note("the area series of a coloured train takes its colour and keeps the fill alpha").c_str());
		ok &= expect(built.solid[1]->pen() == built.solidPen[1] && built.dashed[1]->pen() == built.dashedPen[1],
			note("the series of a train with an invalid colour keep their pens").c_str());
		ok &= expect(swatchAt(list, 0) == green && swatchAt(list, 1) == built.solidPen[1].color(),
			note("each filter swatch is the colour of the first series of its train").c_str());

		// Pin train A by clicking its solid line, then clear the pin.
		const QPoint sample = view->mapFromScene(built.chart->mapToScene(
			built.chart->mapToPosition(QPointF(10, 10), built.solid[0])));
		QMouseEvent press(QEvent::MouseButtonPress, sample, view->viewport()->mapToGlobal(sample), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QMouseEvent release(QEvent::MouseButtonRelease, sample, view->viewport()->mapToGlobal(sample), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(view->viewport(), &press);
		QApplication::sendEvent(view->viewport(), &release);
		ok &= expect(built.solid[0]->pen().widthF() > 3.0 && built.solid[0]->pen().color() == green
				&& built.solid[1]->pen().color().alpha() < 255,
			note("pinning a coloured train emphasises it in its colour and mutes the other train").c_str());
		QMetaObject::invokeMethod(&window, "clearPin");
		ok &= expect(isRecoloured(built.solid[0]->pen(), built.solidPen[0], green)
				&& isRecoloured(built.dashed[0]->pen(), built.dashedPen[0], green)
				&& built.solid[1]->pen() == built.solidPen[1] && built.dashed[1]->pen() == built.dashedPen[1]
				&& built.fill->brush().color().alpha() == 65,
			note("clearing the pin leaves the colour of the coloured train").c_str());
	}

	// A map that names no train of the chart, or only invalid colours, changes nothing.
	DiagramWindow window("Unchanged colours");
	const ColoredChart built = makeColoredChart();
	window.setChart(built.chart);
	window.setTrainColors({{"Z", green}, {"A", QColor()}, {"B", QColor()}});
	auto* list = window.findChild<TrainFilterButton*>()->menu()->findChild<QListWidget*>();
	ok &= expect(built.solid[0]->pen() == built.solidPen[0] && built.dashed[0]->pen() == built.dashedPen[0]
			&& built.solid[1]->pen() == built.solidPen[1] && built.dashed[1]->pen() == built.dashedPen[1]
			&& built.fill->pen() == built.fillPen && built.fill->brush() == built.fillBrush
			&& list && swatchAt(list, 0) == built.solidPen[0].color() && swatchAt(list, 1) == built.solidPen[1].color(),
		"an unknown train id and an invalid colour change nothing");

	// Colours for the train list only: the swatch changes, every series keeps its look.
	DiagramWindow listOnly("List colours");
	const ColoredChart kept = makeColoredChart();
	listOnly.setChart(kept.chart);
	listOnly.setTrainListColors({{"A", green}});
	auto* keptList = listOnly.findChild<TrainFilterButton*>()->menu()->findChild<QListWidget*>();
	ok &= expect(kept.solid[0]->pen() == kept.solidPen[0] && kept.dashed[0]->pen() == kept.dashedPen[0]
			&& kept.solid[1]->pen() == kept.solidPen[1] && kept.dashed[1]->pen() == kept.dashedPen[1]
			&& kept.fill->pen() == kept.fillPen && kept.fill->brush() == kept.fillBrush,
		"list colours leave the series of a coloured train as they were");
	ok &= expect(keptList && swatchAt(keptList, 0) == green && swatchAt(keptList, 1) == kept.solidPen[1].color(),
		"list colours give the coloured train its swatch and leave the other swatch");
	return ok;
}

// A chart with one line series per name and two points each.
static QChart* makeNamedChart(const QStringList& names) {
	auto* chart = new QChart;
	for (const QString& name : names) {
		auto* line = new QLineSeries;
		line->setName(name);
		line->append(10, 10);
		line->append(20, 20);
		chart->addSeries(line);
	}
	chart->createDefaultAxes();
	return chart;
}

static QStringList legendKinds(const DiagramWindow& window) {
	const auto* legend = window.findChild<QChartView*>()->chart()->findChild<QGraphicsObject*>("lineKindLegend");
	return legend ? legend->property("kinds").toStringList() : QStringList();
}

// The push button of the window with `text`, in the window itself or in its panel.
static QPushButton* buttonNamed(QDialog& window, const QString& text) {
	for (auto* button : window.findChildren<QPushButton*>())
		if (button->text() == text) return button;
	return nullptr;
}

static bool buttonFlags(QDialog& window, const QString& prefix) {
	bool ok = true;
	for (auto* button : window.findChildren<QPushButton*>()) {
		ok &= expect(!button->autoDefault(), qPrintable(prefix + " is not auto default: " + button->text()));
		ok &= expect(!button->isDefault(), qPrintable(prefix + " is not the default: " + button->text()));
	}
	return ok;
}

static QChart* buttonTestChart() {
	auto* chart = new QChart;
	auto* a = new QLineSeries;
	a->setName("Train A");
	a->setProperty("trainId", "A");
	a->append(20, 30);
	a->append(40, 50);
	chart->addSeries(a);
	auto* b = new QLineSeries;
	b->setName("Train B");
	b->setProperty("trainId", "B");
	b->append(70, 20);
	b->append(80, 30);
	chart->addSeries(b);
	chart->createDefaultAxes();
	for (auto* axis : chart->axes()) qobject_cast<QValueAxis*>(axis)->setRange(0, 100);
	return chart;
}

static bool exerciseButtonDefaults() {
	int calls = 0;
	const auto provider = [&calls](const QStringList&) { ++calls; return std::string(); };
	DiagramWindow window("Button defaults");
	auto* chart = buttonTestChart();
	window.setChart(chart);
	window.setCsvProvider(provider, "buttons.csv");
	window.show();
	QApplication::processEvents();
	auto* csv = buttonNamed(window, "CSV...");
	auto* fit = window.findChild<QToolButton*>("diagramFit");
	auto* png = buttonNamed(window, "PNG...");
	auto* clear = window.findChild<QPushButton*>("diagramClearSelection");
	auto* view = window.findChild<QChartView*>();
	bool ok = expect(csv && fit && png && clear && clear->text() == "Clear" && view, "diagram window has its fit, clear and export buttons");
	if (!ok) return false;
	ok &= buttonFlags(window, "diagram window button");
	ok &= expect(csv->isVisible() && csv->isEnabled(), "diagram window shows CSV... with a provider");
	ok &= expect(!clear->isVisible(), "Clear is hidden while no train is selected");
	const auto clickTrain = [&] {
		const QPoint sample = view->mapFromScene(chart->mapToScene(chart->mapToPosition(QPointF(20, 30), chart->series().first())));
		QMouseEvent press(QEvent::MouseButtonPress, sample, view->viewport()->mapToGlobal(sample), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QMouseEvent release(QEvent::MouseButtonRelease, sample, view->viewport()->mapToGlobal(sample), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(view->viewport(), &press);
		QApplication::sendEvent(view->viewport(), &release);
	};
	clickTrain();
	auto* pin = window.findChild<QLabel*>("diagramPinLabel");
	ok &= expect(pin && pin->isVisible() && pin->text() == "A", "click selects train A in the button test");
	if (!pin) return false;
	ok &= expect(clear->isVisible(), "Clear shows together with the selection");
	view->setFocus();
	pressKey(view, Qt::Key_Return);
	pressKey(view, Qt::Key_Enter);
	ok &= expect(pin->text() == "A", "Enter in the chart keeps the selection");
	clickTrain();
	ok &= expect(pin->text() == "A", "second click selects train A in the button test");
	pressKey(&window, Qt::Key_Return);
	ok &= expect(pin->text() == "A", "Enter in the window keeps the selection");
	ok &= expect(calls == 0, "Enter starts no CSV export");
	clear->click();
	ok &= expect(!clear->isVisible() && !pin->isVisible(), "Clear hides itself and the selection");
	DiagramWindow input("Input traction");
	input.setProperty("inputTrainUnitId", "unit-a");
	input.setChart(buttonTestChart());
	input.show();
	QApplication::processEvents();
	auto* inputCsv = buttonNamed(input, "CSV...");
	auto* inputFit = input.findChild<QToolButton*>("diagramFit");
	auto* inputPng = buttonNamed(input, "PNG...");
	if (!expect(inputCsv && inputFit && inputPng, "rolling stock window has its fit and export buttons")) return false;
	ok &= expect(inputCsv->isHidden(), "a window without a provider has no CSV... button");
	ok &= expect(inputFit->isVisible() && inputPng->isVisible(), "a window without a provider keeps Fit and PNG...");
	ok &= buttonFlags(input, "rolling stock window button");
	DiagramWindow late("Late provider");
	late.setChart(buttonTestChart());
	late.show();
	QApplication::processEvents();
	auto* lateCsv = buttonNamed(late, "CSV...");
	if (!expect(lateCsv, "late provider window has its CSV button")) return false;
	late.setCsvProvider(provider, "late.csv");
	QApplication::processEvents();
	ok &= expect(lateCsv->isVisible() && lateCsv->isEnabled(), "setCsvProvider shows CSV...");
	late.setCsvProvider({}, QString());
	QApplication::processEvents();
	ok &= expect(lateCsv->isHidden(), "an empty provider hides CSV... again");
	return ok;
}

// A chart of the whole run opens with its name as the window title and the run in the context line. A run identity of
// 300 characters stays in the capped context line and its tooltip, and the window stays within the screen.
static bool exerciseRunContext() {
	const QString chartName = QStringLiteral("Speed vs Distance");
	const QString run = QString(300, QLatin1Char('c')) + QStringLiteral(" / ") + QString(300, QLatin1Char('s'));
	DiagramWindow window(chartName);
	QFont large = window.font();
	large.setPointSizeF(18);
	window.setFont(large);
	auto* chart = buttonTestChart();
	window.setChart(chart);
	window.setPresentation(chartName, QStringLiteral("Run: ") + run, QString());
	window.show();
	QApplication::processEvents();
	const auto* context = window.findChild<QLabel*>("diagramContext");
	const auto* warning = window.findChild<QLabel*>("diagramWarning");
	const QRect screenArea = window.screen()->availableGeometry();
	bool ok = expect(window.windowTitle() == chartName, "the window title is the name of the chart");
	ok &= expect(context->isVisible() && context->text() == QStringLiteral("Run: ") + run
			&& QString(context->toolTip()).remove(QChar(0x200B)).contains(run)
			&& context->height() <= context->fontMetrics().height() * 3 && warning->isHidden(),
		"the context line names the run in at most three lines, its tooltip holds the whole run, and no warning strip opens");
	ok &= expect(window.width() <= screenArea.width() * 9 / 10 && window.height() <= screenArea.height() * 4 / 5,
		"a long run identity keeps the window within the screen");
	return ok;
}

static bool exerciseBlockSections() {
	bool ok = true;
	DiagramWindow window("Physical sections");
	auto* chart = new QChart;
	chart->setTitle("Physical sections title");
	auto* first = new QLineSeries;
	first->setName("A envelope");
	first->setProperty("trainId", "A");
	first->append(2, 0);
	first->append(2, 100);
	chart->addSeries(first);
	auto* second = new QLineSeries;
	second->setName("B envelope");
	second->setProperty("trainId", "B");
	second->append(7, 0);
	second->append(7, 100);
	chart->addSeries(second);
	auto* early = new QScatterSeries;
	early->setName("A early recorded sample");
	early->setProperty("trainId", "A");
	early->setColor(Qt::red);
	early->setBorderColor(Qt::red);
	early->append(2.5, 1);
	early->append(2.5, 31);
	chart->addSeries(early);
	chart->createDefaultAxes();
	auto* x = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
	auto* y = qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first());
	x->setRange(0, 10);
	y->setRange(0, 100);
	y->setReverse(true);
	window.setChart(chart);
	window.setBlockSections(buildBlockSectionExtents({{"@missing<&>@", 4, 4}}), "reference<&>");
	ok &= expect(legendKinds(window).isEmpty(), "sections without geometry draw no boundaries and get no legend entry");
	const std::string authored = "@repeat<&>" + std::string(80, 'X') + "@";
	auto extents = buildBlockSectionExtents({{authored, 1, 3}, {authored, 1, 3}, {"@reverse@", 8, 5}, {"@missing<&>@", 4, 4}});
	window.setBlockSections(extents, "reference<&>");
	ok &= expect(legendKinds(window) == QStringList({"Simulated", "Section boundary"}), "the legend names the drawn section boundaries");
	DiagramWindow sectionsOnly("Physical sections only");
	auto* empty = new QChart;
	auto* km = new QValueAxis;
	km->setRange(0, 10);
	empty->addAxis(km, Qt::AlignBottom);
	sectionsOnly.setChart(empty);
	sectionsOnly.setBlockSections(extents, "reference<&>");
	ok &= expect(legendKinds(sectionsOnly) == QStringList({"Section boundary"}), "a chart with only section boundaries names them in the legend");
	// Wide enough beside the panel that the label of the long section at 1 to 3 km ends before the section at 5 to 8 km.
	window.setMaximumSize(1200, 640);
	window.resize(1200, 640);
	window.show();
	QApplication::processEvents();
	auto* view = window.findChild<QChartView*>();
	auto* tooltip = window.findChild<QLabel*>("diagramTooltip");
	auto* overlay = chart->findChild<QGraphicsObject*>("blockSectionOverlay");
	auto* list = window.findChild<TrainFilterButton*>()->menu()->findChild<QListWidget*>();
	if (!expect(overlay && list && list->count() == 2 && chart->series().size() == 3, "physical layer adds no train/filter/series identities")) return false;
	ok &= expect(overlay->parentItem() == chart && overlay->property("sectionCount").toInt() == 4,
		"chart owns all repeated/coincident and missing physical occurrences");
	const auto chartPoint = [&](double km, double offsetY) {
		const QRectF plot = chart->plotArea();
		return QPointF(plot.left() + (km - x->min()) / (x->max() - x->min()) * plot.width(), plot.top() + offsetY);
	};
	const auto headerPoint = [&](double km, double offsetY) {
		const auto* layer = chart->findChild<QGraphicsObject*>("blockSectionOverlay");
		const double height = (QFontMetricsF(window.font()).height() + 10)
			* (layer->property("geometryNotice").toString().isEmpty() || layer->property("sectionCount").toInt() == 0 ? 1 : 2);
		return chartPoint(km, offsetY - height);
	};
	const auto hover = [&](const QPointF& point) {
		const QPoint position = view->mapFromScene(chart->mapToScene(point));
		QMouseEvent move(QEvent::MouseMove, position, view->viewport()->mapToGlobal(position), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(view->viewport(), &move);
		return position;
	};
	const QPoint header = hover(headerPoint(2, 5));
	ok &= expect(tooltip->isVisible() && tooltip->textFormat() == Qt::PlainText
			&& tooltip->text().contains(QString::fromStdString(authored))
			&& tooltip->text().contains("1:") && tooltip->text().contains("2:") && tooltip->text().contains("reference<&>"),
		"section hover retains full escaped/plain text and both coincident occurrence identities");
	QString selected;
	QObject::connect(&window, &DiagramWindow::trainSelected, [&selected](const QString& id) { selected = id; });
	const auto click = [&](const QPoint& position) {
		QMouseEvent press(QEvent::MouseButtonPress, position, view->viewport()->mapToGlobal(position), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QMouseEvent release(QEvent::MouseButtonRelease, position, view->viewport()->mapToGlobal(position), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(view->viewport(), &press);
		QApplication::sendEvent(view->viewport(), &release);
	};
	// The strip lies above the plot, where no click can select a series. A click that reaches the section code shows its details.
	click(header);
	ok &= expect(selected.isEmpty() && tooltip->isVisible() && tooltip->text().contains("km along reference<&>"),
		"a click on the section strip inspects the section and selects no train");
	// A boundary inside the plot, away from every sample: the dotted line is inspected but belongs to no train.
	click(view->mapFromScene(chart->mapToScene(chartPoint(8, chart->plotArea().height() / 2))));
	ok &= expect(selected.isEmpty() && tooltip->isVisible() && tooltip->text().contains("@reverse@"),
		"a click on a physical boundary inside the plot inspects the section and selects no train");
	// The same click path selects a train when it lands on its series, so the checks above can fail.
	click(view->mapFromScene(chart->mapToScene(chart->mapToPosition(QPointF(2, 50), first))));
	ok &= expect(selected == "A", "a click on a train series selects it while the section layer is present");
	QMetaObject::invokeMethod(&window, "clearPin");
	const auto earlyVisible = [&](const QPointF& value) {
		moveTo(view, value, early);
		const bool inspected = tooltip->isVisible() && tooltip->text().contains("early recorded sample");
		QEvent leave(QEvent::Leave);
		QApplication::sendEvent(view->viewport(), &leave);
		const QImage image = view->grab().toImage();
		const QPoint sample = view->mapFromScene(chart->mapToScene(chart->mapToPosition(value, early)));
		const qreal ratio = image.devicePixelRatio();
		int redPixels = 0;
		for (int dy = -2; dy <= 2; ++dy)
			for (int dx = -2; dx <= 2; ++dx) {
				const QPoint pixel(qRound((sample.x() + dx) * ratio), qRound((sample.y() + dy) * ratio));
				if (!image.rect().contains(pixel)) continue;
				const QColor color = image.pixelColor(pixel);
				if (color.red() > 200 && color.green() < 80 && color.blue() < 80) ++redPixels;
			}
		return inspected && redPixels > 10;
	};
	// The strip and the geometry notice paint only in the band that ends at the plot area. Compare a grab with and without the
	// overlay there, and look for the dark notice text in the lower row of the band.
	const QImage withOverlay = view->grab().toImage();
	overlay->hide();
	const QImage withoutOverlay = view->grab().toImage();
	overlay->show();
	const auto bandPixels = [&](double fromY, double toY, bool darkText) {
		const qreal scale = withOverlay.devicePixelRatio();
		const QRectF plotRect = chart->plotArea();
		const QPoint from = view->mapFromScene(chart->mapToScene(QPointF(plotRect.left() + 2, fromY)));
		const QPoint to = view->mapFromScene(chart->mapToScene(QPointF(plotRect.right() - 2, toY)));
		int count = 0;
		for (int row = qRound(from.y() * scale); row < qRound(to.y() * scale); ++row)
			for (int column = qRound(from.x() * scale); column < qRound(to.x() * scale); ++column) {
				if (!withOverlay.rect().contains(column, row)) continue;
				const QColor color = withOverlay.pixelColor(column, row);
				if (darkText ? color.red() + color.green() + color.blue() < 400 : withOverlay.pixel(column, row) != withoutOverlay.pixel(column, row)) ++count;
			}
		return count;
	};
	const double bandTop = headerPoint(0, 0).y();
	const double rowHeight = QFontMetricsF(window.font()).height() + 10;
	ok &= expect(bandPixels(0, bandTop - 2, false) == 0 && bandPixels(bandTop, bandTop + rowHeight, false) > 0
			&& bandPixels(bandTop + rowHeight, chart->plotArea().top(), true) > 10,
		"the strip and the geometry notice text paint in the band above the plot and not over the title");
	// The window header names the chart, so the chart draws no title of its own: with the overlay hidden nothing dark paints
	// above the middle of the plot, although the chart came with a title.
	const auto titleExtent = [&](double limitY) {
		overlay->hide();
		const QImage image = view->grab().toImage();
		overlay->show();
		const qreal scale = image.devicePixelRatio();
		const QPoint centre = view->mapFromScene(chart->mapToScene(QPointF(chart->size().width() / 2, 0)));
		const int limit = view->mapFromScene(chart->mapToScene(QPointF(0, limitY))).y();
		int count = 0, bottom = -1;
		for (int row = 0; row < limit; ++row)
			for (int dx = -80; dx <= 80; ++dx) {
				const QPoint pixel(qRound((centre.x() + dx) * scale), qRound(row * scale));
				if (!image.rect().contains(pixel)) continue;
				const QColor color = image.pixelColor(pixel);
				if (color.red() + color.green() + color.blue() < 400) {
					++count;
					bottom = row;
				}
			}
		return std::make_pair(count, bottom);
	};
	const double stripTop = headerPoint(0, 0).y();
	const auto titleAll = titleExtent(chart->plotArea().top());
	const auto titleAboveStrip = titleExtent(stripTop);
	const int stripTopView = view->mapFromScene(chart->mapToScene(QPointF(0, stripTop))).y();
	ok &= expect(chart->title().isEmpty() && titleAll.first == 0 && titleAboveStrip == titleAll && stripTopView > 0,
		"the chart draws no title above the plot");
	ok &= expect(earlyVisible(QPointF(2.5, 1)), "near-minimum time sample is visible in capture and inspectable with physical header");
	y->setRange(20, 70);
	wheel(view, QPoint(0, -qRound(chart->plotArea().height() / 5)), {});
	QApplication::processEvents();
	ok &= expect(y->min() > 29 && y->min() < 31 && earlyVisible(QPointF(2.5, 31)),
		"after time pan a sample at the new upper edge remains visible and inspectable");
	QMetaObject::invokeMethod(&window, "resetZoom");
	QApplication::processEvents();
	for (int row = 0; row < list->count(); ++row) list->item(row)->setCheckState(Qt::Unchecked);
	hover(headerPoint(2, 5));
	ok &= expect(!first->isVisible() && !second->isVisible() && tooltip->isVisible() && overlay->isVisible(),
		"hiding every train retains section labels and inspection");
	std::vector<BlockSectionInput> narrowSections;
	for (int index = 0; index < 40; ++index)
		narrowSections.push_back({"@n" + std::to_string(index) + "@", index * 0.25, (index + 1) * 0.25});
	window.setBlockSections(buildBlockSectionExtents(narrowSections), "crowded");
	QApplication::processEvents();
	const QImage crowded = view->grab().toImage();
	const QRectF plot = chart->plotArea();
	const QPoint topLeft = view->mapFromScene(chart->mapToScene(headerPoint(0, 0)));
	const qreal ratio = crowded.devicePixelRatio();
	int textPixels = 0;
	for (int dy = 1; dy < QFontMetricsF(window.font()).height() + 3; ++dy)
		for (int dx = 2; dx < plot.width() - 2; ++dx) {
			const QPoint pixel(qRound((topLeft.x() + dx) * ratio), qRound((topLeft.y() + dy) * ratio));
			if (!crowded.rect().contains(pixel)) continue;
			const QColor color = crowded.pixelColor(pixel);
			if (color.red() + color.green() + color.blue() < 400) ++textPixels;
		}
	ok &= expect(textPixels > 10, "crowded extents thin labels rather than eliding every ID at fit");
	const double labelWidth = QFontMetricsF(window.font()).horizontalAdvance("1:@n0@");
	const QPointF expandedLabel(plot.left() + labelWidth - 2, headerPoint(0, 0).y() + 5);
	ok &= expect(expandedLabel.x() > chartPoint(0.25, 0).x() + 3,
		"crowded label test reaches visible text beyond its physical interval");
	hover(expandedLabel);
	ok &= expect(tooltip->isVisible() && tooltip->text().contains("1:@n0@") && !tooltip->text().contains("2:@n1@"),
		"expanded visible label reports its full ID rather than the neighbouring physical interval");
	// With labels the strip shows the label, which is wider than the id, and the hover names the label and the id.
	SceneSectionLabels crowdedLabels;
	crowdedLabels.byReference = {{"@n0@", "T 0.000 to 0.250 km"}};
	window.setBlockSections(buildBlockSectionExtents(narrowSections), "crowded", &crowdedLabels);
	QApplication::processEvents();
	const double labelledWidth = QFontMetricsF(window.font()).horizontalAdvance("1: T 0.000 to 0.250 km");
	hover(QPointF(plot.left() + labelledWidth - 2, headerPoint(0, 0).y() + 5));
	ok &= expect(tooltip->isVisible()
			&& tooltip->text().startsWith("Section 1: T 0.000 to 0.250 km (@n0@)\n0 to 0.25 km along crowded"),
		"the strip names a section by its label, and the hover by its label and id");
	window.setBlockSections(extents, "reference<&>");
	overlay = chart->findChild<QGraphicsObject*>("blockSectionOverlay");
	const auto boundaryPainted = [&] {
		QApplication::processEvents();
		const QImage with = view->grab().toImage();
		overlay->hide();
		const QImage without = view->grab().toImage();
		overlay->show();
		const QPoint boundary = view->mapFromScene(chart->mapToScene(chartPoint(3, 60)));
		const qreal pixelRatio = with.devicePixelRatio();
		int changed = 0;
		for (int dy = 0; dy < 20; ++dy)
			for (int dx = -2; dx <= 2; ++dx) {
				const QPoint pixel(qRound((boundary.x() + dx) * pixelRatio), qRound((boundary.y() + dy) * pixelRatio));
				if (with.rect().contains(pixel) && with.pixel(pixel) != without.pixel(pixel)) ++changed;
			}
		return changed > 0;
	};
	ok &= expect(boundaryPainted(), "painted physical boundary aligns with km axis, including PNG capture surface");
	wheel(view, {}, QPoint(0, 120), Qt::ControlModifier, chartPoint(3, 100));
	QApplication::processEvents();
	hover(headerPoint(2, 5));
	ok &= expect(tooltip->isVisible() && tooltip->text().contains("km along reference<&>"), "header inspection follows zoom");
	wheel(view, QPoint(-10, -8), {});
	window.resize(window.width() - 80, window.height() - 50);
	QApplication::processEvents();
	window.setTimeAxisY(true, 8 * 3600);
	QApplication::processEvents();
	hover(headerPoint(2, 5));
	ok &= expect(tooltip->isVisible() && boundaryPainted(), "boundary and header follow pan, resize and clock-axis replacement");
	QMetaObject::invokeMethod(&window, "resetZoom");
	QApplication::processEvents();
	hover(headerPoint(2, 5));
	ok &= expect(x->min() == 0 && x->max() == 12 && tooltip->isVisible() && boundaryPainted(), "reset retains original section alignment");
	hover(headerPoint(6.5, 5));
	ok &= expect(tooltip->isVisible() && tooltip->text().contains("@reverse@") && tooltip->text().contains("8 to 5 km along reference<&>"),
		"a section towards decreasing X reports its directed ends in hover");
	hover(headerPoint(2, rowHeight + 5));
	ok &= expect(tooltip->text().contains("@missing<&>@") && tooltip->text().contains("zero length")
			&& !overlay->property("geometryNotice").toString().isEmpty(),
		"missing geometry has an in-chart notice with full hover reason");
	QPointer<QGraphicsObject> old = overlay;
	auto* only = new QChart;
	auto* onlyX = new QValueAxis;
	auto* onlyY = new QValueAxis;
	onlyX->setRange(0, 10);
	onlyY->setRange(0, 100);
	only->addAxis(onlyX, Qt::AlignBottom);
	only->addAxis(onlyY, Qt::AlignLeft);
	window.setChart(only);
	ok &= expect(old.isNull() && !tooltip->isVisible(), "replacement chart clears old overlay and tooltip");
	x = onlyX;
	chart = only;
	window.setBlockSections(extents, "section-only");
	window.setTimeAxisY(true);
	QApplication::processEvents();
	hover(headerPoint(2, 5));
	ok &= expect(only->series().isEmpty() && window.findChild<TrainFilterButton*>()->visibleTrainIds().isEmpty()
			&& tooltip->isVisible(),
		"explicit numeric axes allow section-only inspection without fake trains");
	QStringList exportedIds;
	window.setCsvProvider([&exportedIds](const QStringList& ids) {
		exportedIds = ids;
		return std::string("retained data");
	},
		"unmapped.csv", {"unrelated"});
	window.csvContent();
	list = window.findChild<TrainFilterButton*>()->menu()->findChild<QListWidget*>();
	ok &= expect(only->series().isEmpty() && list->count() == 1 && exportedIds == QStringList{"unrelated"},
		"real CSV-only train reaches the window provider without a dummy series");
	list->item(0)->setCheckState(Qt::Unchecked);
	window.csvContent();
	ok &= expect(exportedIds.isEmpty() && only->series().isEmpty(), "CSV-only train can be hidden with the actual filter");
	list->item(0)->setCheckState(Qt::Checked);
	const auto* filter = window.findChild<TrainFilterButton*>();
	ok &= expect(filter->visibleTrainIds() == QStringList{"unrelated"}, "the CSV-only identity is a checked entry before the provider goes");
	window.setCsvProvider({}, {});
	list = filter->menu()->findChild<QListWidget*>();
	ok &= expect(filter->visibleTrainIds().isEmpty() && list->count() == 0,
		"removing the provider removes its checked CSV-only identity from the filter");
	window.setBlockSections({}, "no-geometry");
	QApplication::processEvents();
	hover(headerPoint(2, 5));
	ok &= expect(tooltip->isVisible() && tooltip->text().contains("No section boundaries for this route"), "empty section geometry is explicitly reported inside chart");
	return ok;
}

// The text that the tooltip of `widget` shows once the pointer rests on it.
static QString shownTip(QWidget* widget) {
	const QPoint point(4, 4);
	QHelpEvent help(QEvent::ToolTip, point, widget->mapToGlobal(point));
	QApplication::sendEvent(widget, &help);
	const QString text = QToolTip::text();
	QToolTip::hideText();
	if (!Qt::mightBeRichText(text))
		return text;
	QTextDocument document;
	document.setHtml(text);
	return document.toPlainText();
}

// The context label and the read-only inspector fields show their text as typed in the tooltip.
static bool exercisePlainToolTips() {
	bool ok = true;
	DiagramWindow window("Tooltips");
	window.show();
	QApplication::processEvents();
	auto* context = window.findChild<QLabel*>("diagramContext");
	for (const QString& text : {QStringLiteral("Case A & B | Scenario C"), QStringLiteral("Train <b>1</b> & <i>2</i>"),
			 QStringLiteral("Id &lt;x&gt; &amp; <not a tag>")}) {
		window.setPresentation("Tooltips", text);
		QApplication::processEvents();
		ok &= expect(context->isVisible() && shownTip(context) == text, "the context tooltip shows the context as typed");
		QLineEdit field;
		field.setToolTip(plainToolTip(text));
		field.show();
		QApplication::processEvents();
		ok &= expect(shownTip(&field) == text, "a read-only field tooltip shows its text as typed");
	}
	ok &= expect(plainToolTip(QString()).isEmpty(), "an empty text gives no tooltip");
	return ok;
}

// The widest tooltip that Qt shows at the moment.
static int shownTipWidth(QWidget* widget) {
	const QPoint point(4, 4);
	QHelpEvent help(QEvent::ToolTip, point, widget->mapToGlobal(point));
	QApplication::sendEvent(widget, &help);
	QApplication::processEvents();
	int widest = 0;
	for (QWidget* top : QApplication::topLevelWidgets())
		if (top->isVisible() && top->inherits("QTipLabel")) widest = std::max(widest, top->width());
	QToolTip::hideText();
	return widest;
}

// A 300-character identifier has no space to wrap at; the tooltip must still fit the screen.
static bool exerciseLongToolTips() {
	const QString id = QString(300, QLatin1Char('Q'));
	DiagramWindow window("Long tooltip");
	window.show();
	QApplication::processEvents();
	window.setPresentation("Long tooltip", QStringLiteral("Reference: ") + id, QString());
	QApplication::processEvents();
	auto* context = window.findChild<QLabel*>("diagramContext");
	const int screenWidth = window.screen()->availableGeometry().width();
	bool ok = true;
	const int contextWidth = shownTipWidth(context);
	ok &= expect(contextWidth > 0 && contextWidth <= screenWidth,
		"the context tooltip of a 300-character id is no wider than the screen");
	ok &= expect(shownTip(context).remove(QChar(0x200B)) == context->text(),
		"the break opportunities are the only change to the context tooltip");
	QLineEdit field;
	field.setToolTip(plainToolTip(id));
	field.show();
	QApplication::processEvents();
	const int fieldWidth = shownTipWidth(&field);
	ok &= expect(fieldWidth > 0 && fieldWidth <= screenWidth,
		"the tooltip of a read-only field with a 300-character id is no wider than the screen");
	const QString path = QStringLiteral("/Users/someone/a folder/without-any-space-in-its-long-name/case file");
	const QString shortWords = QStringLiteral("Case A & B | Scenario C");
	ok &= expect(plainToolTip(shortWords) == Qt::convertFromPlainText(shortWords, Qt::WhiteSpaceNormal),
		"a text with short words gets no break opportunity");
	ok &= expect(plainToolTip(path).contains(QChar(0x200B))
			&& QString(plainToolTip(path)).remove(QChar(0x200B)) == Qt::convertFromPlainText(path, Qt::WhiteSpaceNormal),
		"a long word in a text with spaces gets a break opportunity and nothing else changes");
	const QString pairs = QString(31, QLatin1Char('a')) + QString::fromUcs4(U"\U0001F682") + QString(40, QLatin1Char('b'));
	const QString broken = withBreakOpportunities(pairs);
	bool splitsPair = false;
	for (int i = 1; i < broken.size(); ++i)
		splitsPair = splitsPair || (broken.at(i - 1).isHighSurrogate() && broken.at(i) == QChar(0x200B));
	ok &= expect(!splitsPair && QString(broken).remove(QChar(0x200B)) == pairs && broken.contains(QChar(0x200B)),
		"a break opportunity never separates the halves of a character");
	return ok;
}

// The texts that the chart of `window` draws once its layout has settled and the items of removed axes are gone.
static QStringList drawnChartTexts(DiagramWindow& window) {
	for (int round = 0; round < 4; ++round) {
		QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QApplication::processEvents();
	}
	QStringList texts;
	for (QGraphicsItem* item : window.findChild<QChartView*>()->scene()->items())
		if (const auto* text = qgraphicsitem_cast<QGraphicsTextItem*>(item); text && text->isVisible()) texts << text->toPlainText();
	return texts;
}

// The scene rectangles of the visible texts of the chart that are not empty, less the 4-pixel margin of each text.
static QList<QRectF> drawnTextRects(DiagramWindow& window) {
	QList<QRectF> rects;
	for (QGraphicsItem* item : window.findChild<QChartView*>()->scene()->items())
		if (const auto* text = qgraphicsitem_cast<QGraphicsTextItem*>(item); text && text->isVisible() && !text->toPlainText().isEmpty())
			rects << text->sceneBoundingRect().adjusted(4, 4, -4, -4);
	return rects;
}

static QStringList elidedTexts(const QStringList& texts) {
	QStringList elided;
	for (const QString& text : texts)
		if (text.endsWith(QLatin1String("..."))) elided << text;
	return elided;
}

// A chart built like the timetable graph: distance along the bottom, the clock downward, the stations on top. A station
// can be labelled with its name and position or with its name alone.
static QChart* routeChartWithStations(const QStringList& names, QCategoryAxis** stationAxis, std::vector<StationTick>* ticks) {
	auto* chart = new QChart;
	auto* series = new QLineSeries;
	series->setProperty("trainId", "A");
	series->setName("A (arrival)");
	series->append(0.0, 10);
	series->append(9.5 * names.size(), 3000);
	chart->addSeries(series);
	chart->createDefaultAxes();
	auto* x = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
	auto* y = qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first());
	x->setRange(-0.01, 9.5 * (names.size() - 1) + 0.01);
	x->setTitleText("Route X (km); origin 0.000, travel right");
	y->setReverse(true);
	y->setRange(0, 3600);
	y->setTitleText("Elapsed simulation time (s), downward");
	*stationAxis = new QCategoryAxis;
	(*stationAxis)->setTitleText("Reference stations");
	(*stationAxis)->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
	(*stationAxis)->setStartValue(x->min() - 1);
	(*stationAxis)->setRange(x->min(), x->max());
	chart->addAxis(*stationAxis, Qt::AlignTop);
	series->attachAxis(*stationAxis);
	for (int i = 0; i < names.size(); ++i)
		ticks->push_back({9.5 * i, {QStringLiteral("%1 (%2 km)").arg(names[i]).arg(9.5 * i, 0, 'f', 2), names[i]}});
	return chart;
}

// The tick labels of the timetable graph stay whole at the normal font and at one and a half times its size.
static bool exerciseStationAxis() {
	const QFont original = QApplication::font();
	const QStringList names = {"Amsterdam Centraal", "Utrecht Centraal", "Den Bosch", "Eindhoven Centraal", "Venlo", "Roermond",
		"Maastricht", "Sittard", "Heerlen", "G&d<g"};
	struct Case {
		double fontScale;
		int width;
		int height;
		const char* name;
	};
	bool ok = true;
	// A window on a 1280 by 800 screen is at most 1152 by 640; at a scale factor of 1.5 that screen is 853 by 533.
	for (const Case& scene : {Case{1.0, 1152, 640, "normal font"}, Case{1.5, 1152, 640, "font 1.5 times larger"},
			 Case{1.0, 768, 426, "scale factor 1.5"}}) {
		QFont font = original;
		font.setPointSizeF(original.pointSizeF() * scene.fontScale);
		QApplication::setFont(font);
		{
			DiagramWindow window("Timetable graph");
			QCategoryAxis* stations = nullptr;
			std::vector<StationTick> ticks;
			window.setChart(routeChartWithStations(names, &stations, &ticks));
			window.setStationAxis(stations, ticks);
			window.setTimeAxisY(true, 8 * 3600);
			window.setMaximumSize(scene.width, scene.height);
			window.resize(scene.width, scene.height);
			window.show();
			QStringList texts = drawnChartTexts(window);
			const QString what = QString::fromLatin1(scene.name);
			ok &= expect(elidedTexts(texts).isEmpty(), qPrintable(what + ": no tick label is shortened to dots"));
			ok &= expect(texts.contains("08:00:00") && texts.contains("09:00:00"), qPrintable(what + ": the clock labels are whole"));
			const QStringList labelled = stations->categoriesLabels();
			int drawn = 0;
			for (const QString& label : labelled) {
				QTextDocument plain;
				plain.setHtml(label);
				drawn += texts.contains(plain.toPlainText());
			}
			ok &= expect(!labelled.isEmpty() && drawn == labelled.size(), qPrintable(what + ": the station labels are drawn whole"));
			// Zoomed in, the stations of the new range are named and the labels are still whole.
			auto* view = window.findChild<QChartView*>();
			for (int step = 0; step < 7; ++step)
				wheel(view, {}, {0, 120}, Qt::ControlModifier, view->chart()->plotArea().topLeft() + QPointF(20, 20));
			texts = drawnChartTexts(window);
			const auto denBosch = std::find_if(texts.begin(), texts.end(), [](const QString& text) { return text.startsWith("Den Bosch"); });
			ok &= expect(stations->categoriesLabels().size() >= 2 && elidedTexts(texts).isEmpty() && denBosch != texts.end(),
				qPrintable(what + ": zoomed in, the station labels stay whole"));
		}
	}
	QApplication::setFont(original);
	return ok;
}

// A chart with more than one kind of line names the kinds in a legend below the plot: the layer of a series, else planned
// or simulated by its "planned" property or its name.
static bool exerciseLegend() {
	bool ok = true;

	DiagramWindow window("Legend");
	window.setChart(makeNamedChart({"Train A (planned arrival)", "Train A (simulated arrival)"}));
	ok &= expect(legendKinds(window) == QStringList({"Planned", "Simulated"}), "planned and simulated lines are named in the legend");
	window.setChart(makeNamedChart({"Train A", "Train B", "Train A"}));
	ok &= expect(legendKinds(window).isEmpty(), "a chart with one kind of line has no legend");
	window.setChart(makeNamedChart({"TRAIN A (PLANNED REFERENCE)", "Train B"}));
	ok &= expect(legendKinds(window) == QStringList({"Planned", "Simulated"}), "the planned name test ignores letter case");
	QChart* plannedByProperty = makeNamedChart({"Train A", "Train A"});
	plannedByProperty->series()[0]->setProperty("planned", true);
	window.setChart(plannedByProperty);
	const auto* plannedLine = qobject_cast<QXYSeries*>(plannedByProperty->series()[0]);
	ok &= expect(legendKinds(window) == QStringList({"Planned", "Simulated"}) && plannedLine->pen().style() == Qt::DashLine,
		"a series whose planned property is set is planned without the word in its name, and drawn dashed");
	// The colour key takes the place of its layer, the first time the layer comes; a new chart clears it.
	const auto keyedChart = [] {
		QChart* chart = makeNamedChart({"A", "A", "B", "A"});
		chart->series()[0]->setProperty("layer", "blocking time");
		chart->series()[1]->setProperty("layer", "simulated");
		chart->series()[2]->setProperty("layer", "blocking time");
		chart->series()[3]->setProperty("planned", true);
		return chart;
	};
	window.setChart(keyedChart());
	ok &= expect(legendKinds(window) == QStringList({"Blocking time", "Simulated", "Planned"}), "without a colour key the layer names its kind");
	window.setColourKey("blocking time", {{"Block or station", QColor(100, 160, 240)}, {"Switch", QColor(240, 210, 40)}});
	ok &= expect(legendKinds(window) == QStringList({"Block or station", "Switch", "Simulated", "Planned"}),
		"the colour key replaces the entry of its layer with a swatch for each colour");
	window.setColourKey("no such layer", {{"Conflict", QColor(220, 50, 50)}});
	ok &= expect(legendKinds(window) == QStringList({"Blocking time", "Simulated", "Planned"}), "a key for a layer the chart lacks shows nothing");
	window.setColourKey("blocking time", {{"Conflict", QColor(220, 50, 50)}});
	window.setChart(keyedChart());
	ok &= expect(legendKinds(window) == QStringList({"Blocking time", "Simulated", "Planned"}), "a new chart clears the colour key");
	QChart* layered = makeNamedChart({"A", "A (planned reference)", "A again"});
	layered->series()[0]->setProperty("layer", "blocking time");
	layered->series()[2]->setProperty("layer", "simulated");
	window.setChart(layered);
	ok &= expect(legendKinds(window) == QStringList({"Blocking time", "Planned", "Simulated"}),
		"the layer of a series names its kind");

	// At the size of the window on a 1280 by 800 screen the legend is inside the chart, below the plot and its axis texts.
	window.setMaximumSize(1152, 640);
	window.resize(1152, 640);
	window.show();
	drawnChartTexts(window);
	const auto* legend = layered->findChild<QGraphicsObject*>("lineKindLegend");
	const QRectF area = legend ? legend->mapRectToScene(legend->boundingRect()) : QRectF();
	bool clear = !area.isEmpty() && layered->sceneBoundingRect().contains(area)
		&& area.top() > layered->mapRectToScene(layered->plotArea()).bottom();
	for (const QRectF& rect : drawnTextRects(window)) clear = clear && !rect.intersects(area);
	ok &= expect(clear, "the legend lies inside the chart, below the plot and clear of the axis texts");
	return ok;
}

// Numeric axes that keep the ticks Qt gives them get round ticks: at most eight steps of 1, 2 or 5 times a power of ten,
// labelled with the decimals of the step, zero as "0.0" and not "-0.0", none overlapping at the size of the window on a
// 1280 by 800 screen. The range widens to the first tick past each end, so both ends are labelled and a line at an end
// keeps clear of the plot edge; a range from zero keeps zero as its start. A zoom picks a smaller step.
static bool exerciseRoundTicks() {
	// Arrival delays in minutes by journey order, like those of Paimpol / baseline.
	DiagramWindow window("Round ticks");
	auto* chart = new QChart;
	for (const auto& points : {QList<QPointF>{{2, -0.35}, {3, 0.7}, {4, 0.48}, {5, 0.55}, {6, 0.45}, {7, -0.4}, {9, -0.2}, {10, -0.07}},
			 QList<QPointF>{{2, 0.22}, {3, -0.38}}}) {
		auto* series = new QLineSeries;
		series->setName(chart->series().isEmpty() ? "A" : "B");
		series->append(points);
		chart->addSeries(series);
	}
	chart->createDefaultAxes();
	auto* x = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
	auto* y = qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first());
	window.setChart(chart);
	window.setMaximumSize(1152, 640);
	window.resize(1152, 640);
	window.show();
	const QStringList texts = drawnChartTexts(window);
	bool ok = expect(x->min() == 0 && x->max() == 12 && std::abs(y->min() + 0.6) < 1e-9 && std::abs(y->max() - 0.8) < 1e-9,
		"the range widens to the first round tick past the data at each end");
	bool labelled = !texts.contains("-0.0");
	for (const char* label : {"0", "2", "4", "6", "8", "10", "12", "-0.6", "-0.4", "-0.2", "0.0", "0.2", "0.4", "0.6", "0.8"})
		labelled = labelled && texts.contains(label);
	ok &= expect(labelled, "the ticks are round, with the decimals of their step, and both ends are labelled");
	const QList<QRectF> labels = drawnTextRects(window);
	bool apart = true;
	for (int a = 0; a < labels.size(); ++a)
		for (int b = a + 1; b < labels.size(); ++b) apart = apart && !labels[a].intersects(labels[b]);
	ok &= expect(apart, "no two texts of the chart overlap");
	auto* view = window.findChild<QChartView*>();
	for (int step = 0; step < 6; ++step) wheel(view, {}, {0, 120}, Qt::ControlModifier);
	ok &= expect(x->tickType() == QValueAxis::TicksDynamic && x->tickInterval() > 0 && x->tickInterval() < 1 && x->max() - x->min() < 4,
		"zooming in picks a smaller round step");
	// Speeds from a stop up to a line speed of 70 km/h.
	DiagramWindow speeds("Round ticks from zero");
	auto* speedChart = new QChart;
	auto* speed = new QLineSeries;
	speed->append({{0, 0}, {1, 70}, {2, 70}, {3, 0}});
	speedChart->addSeries(speed);
	speedChart->createDefaultAxes();
	speeds.setChart(speedChart);
	auto* speedAxis = qobject_cast<QValueAxis*>(speedChart->axes(Qt::Vertical).first());
	ok &= expect(speedAxis->min() == 0 && speedAxis->max() == 80, "a range from zero keeps its start and a line at 70 gets room below 80");

	// A route of 5 km with the 0.01 km margin of the route charts on each side.
	DiagramWindow route("Round ticks of a short route");
	auto* routeChart = new QChart;
	auto* path = new QLineSeries;
	path->append({{0, 0}, {5, 1}});
	routeChart->addSeries(path);
	routeChart->createDefaultAxes();
	auto* routeAxis = qobject_cast<QValueAxis*>(routeChart->axes(Qt::Horizontal).first());
	routeAxis->setRange(-0.01, 5.01);
	route.setChart(routeChart);
	ok &= expect(routeAxis->min() == 0 && routeAxis->max() == 6, "a short route keeps zero as its start behind the margin of the route charts");
	return ok;
}

// The clock axis puts its ticks on round clock times and keeps one at elapsed zero, the run start. A round tick less than
// a step after it stays where their labels have room in the plot, as 07:30:00 after a start at 07:10:40, and is left out
// where the labels would overlap.
static bool exerciseClockTicks() {
	const auto clockLabels = [](long long startOffset) {
		DiagramWindow window("Clock ticks");
		QChart* chart = makeNamedChart({"A"});
		qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first())->setRange(0, 9000);
		window.setChart(chart);
		window.setTimeAxisY(true, startOffset);
		window.setMaximumSize(1152, 640);
		window.resize(1152, 640);
		window.show();
		drawnChartTexts(window);
		auto* clock = qobject_cast<QCategoryAxis*>(chart->axes(Qt::Vertical).first());
		return clock ? clock->categoriesLabels() : QStringList();
	};
	bool ok = expect(clockLabels(7 * 3600 + 10 * 60 + 40).mid(0, 3) == QStringList({"07:10:40", "07:30:00", "08:00:00"}),
		"a round tick whose label has room stays next to the run start");
	ok &= expect(clockLabels(7 * 3600 + 29 * 60 + 50).mid(0, 2) == QStringList({"07:29:50", "08:00:00"}),
		"a round tick whose label would overlap the run start is left out");
	return ok;
}

// The clock axis of setTimeAxisY takes the place of the numeric time axis, so the chart draws one vertical axis with one
// title, also in a picture taken before the event loop turns, as an export or an offscreen capture takes it. No test here
// runs the event loop, so the items of a removed axis that Qt deletes later stay in the scene as they would there.
static bool exerciseSingleVerticalAxis() {
	DiagramWindow window("Single vertical axis");
	QChart* chart = makeNamedChart({"A"});
	chart->axes(Qt::Horizontal).first()->setTitleText("Distance (km)");
	chart->axes(Qt::Vertical).first()->setTitleText("Elapsed simulation time (s)");
	window.setChart(chart);
	window.resize(800, 500);
	window.show();
	const auto verticalTitles = [&window] {
		for (int turn = 0; turn < 4; ++turn) QApplication::processEvents();
		window.grab();
		QStringList titles;
		for (QGraphicsItem* item : window.findChild<QChartView*>()->scene()->items())
			if (const auto* text = qgraphicsitem_cast<QGraphicsTextItem*>(item); text && text->isVisible() && text->rotation() != 0)
				titles << text->toPlainText();
		return titles;
	};
	bool ok = expect(verticalTitles() == QStringList({"Elapsed simulation time (s)"}), "a numeric time axis has one vertical title");
	window.setTimeAxisY(true, 8 * 3600);
	ok &= expect(verticalTitles() == QStringList({"Time"}), "the clock axis is the only vertical axis that the chart draws");
	window.setTimeAxisY(false);
	ok &= expect(verticalTitles() == QStringList({"Elapsed simulation time (s)"}), "numeric labels again show the numeric axis alone");
	window.setTimeAxisY(true, 8 * 3600);
	ok &= expect(verticalTitles() == QStringList({"Time"}), "a second clock axis is again the only vertical axis");
	return ok;
}

// Once CSV... has saved the file, the status bar of the main window names it. PNG... suggests the name of the CSV with
// .png and saves the figure of the window.
static bool exerciseCsvSavedMessage() {
	QMainWindow mainWindow;
	auto* window = new DiagramWindow("Saved message", &mainWindow);
	window->setChart(buttonTestChart());
	window->setCsvProvider([](const QStringList&) { return std::string("Train\nA\n"); }, "saved.csv");
	window->show();
	QTemporaryDir directory;
	const QString path = QDir(directory.path()).filePath("saved.csv");
	QTimer::singleShot(0, window, [path] {
		for (QWidget* widget : QApplication::topLevelWidgets())
			if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
				dialog->setDirectory(QFileInfo(path).absolutePath());
				dialog->selectFile(QFileInfo(path).fileName());
				static_cast<QDialog*>(dialog)->accept(); // QFileDialog::accept is protected
			}
	});
	buttonNamed(*window, "CSV...")->click();
	bool ok = expect(QFile::exists(path) && mainWindow.statusBar()->currentMessage().contains(QDir::toNativeSeparators(path)),
		"the status bar of the main window names the saved CSV file");
	QString suggested;
	QTimer::singleShot(0, window, [&suggested] {
		for (QWidget* widget : QApplication::topLevelWidgets())
			if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
				suggested = QFileInfo(dialog->selectedFiles().value(0)).fileName();
				dialog->reject();
			}
	});
	buttonNamed(*window, "PNG...")->click();
	ok &= expect(suggested == "saved.png", "PNG... suggests the name of the CSV with .png");
	const QString picture = QDir(directory.path()).filePath("saved.png");
	QTimer::singleShot(0, window, [picture] {
		for (QWidget* widget : QApplication::topLevelWidgets())
			if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
				dialog->setDirectory(QFileInfo(picture).absolutePath());
				dialog->selectFile(QFileInfo(picture).fileName());
				static_cast<QDialog*>(dialog)->accept();
			}
	});
	buttonNamed(*window, "PNG...")->click();
	const QImage saved(picture);
	const QImage figure = window->figureImage();
	ok &= expect(!saved.isNull() && saved.size() == figure.size() && saved.dotsPerMeterX() == figure.dotsPerMeterX(),
		"PNG... saves the figure of the window");
	return ok;
}

// A route id of 300 characters is cut short in the title of an axis; the pointer on the title shows all of it.
static bool exerciseLongAxisTitle() {
	DiagramWindow window("Blocking time");
	const QString id = QStringLiteral("r&0<x>") + QString(294, QLatin1Char('Q'));
	auto* chart = new QChart;
	auto* series = new QLineSeries;
	series->setName("A");
	series->append(0, 0);
	series->append(10, 100);
	chart->addSeries(series);
	chart->createDefaultAxes();
	auto* x = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
	x->setTitleText(QStringLiteral("Reference %1 X (km); origin 0.000, travel right").arg(id.toHtmlEscaped()));
	window.setChart(chart);
	window.show();
	const QStringList texts = drawnChartTexts(window);
	QString shown;
	for (const QString& text : texts)
		if (text.startsWith("Reference r&0<x>")) shown = text;
	bool ok = expect(shown.endsWith("...") && shown.size() > 20, "a title wider than the chart is shortened, with the id as typed");
	auto* view = window.findChild<QChartView*>();
	QPointF centre;
	for (QGraphicsItem* item : view->scene()->items())
		if (const auto* text = qgraphicsitem_cast<QGraphicsTextItem*>(item); text && text->toPlainText() == shown)
			centre = text->mapToScene(text->boundingRect().center());
	const QPoint position = view->mapFromScene(centre);
	QMouseEvent move(QEvent::MouseMove, position, view->viewport()->mapToGlobal(position), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(view->viewport(), &move);
	const auto* tooltip = window.findChild<QLabel*>("diagramTooltip");
	ok &= expect(tooltip->isVisible() && tooltip->text().contains(id), "the tooltip of a shortened title holds the whole id");
	ok &= expect(tooltip->geometry().right() <= window.rect().right() && tooltip->height() > tooltip->fontMetrics().height() * 3,
		"the tooltip of a title wraps the id within the window");
	return ok;
}

// The header of a diagram window: the context in one line at the left, then Zoom Out, Zoom In, Fit and the panel button,
// which show their names while their icons are not loaded. No help line is left; the tooltips of the buttons name the keys.
// The window opens with the chart in focus, and Tab goes through the header, the chart and the panel in that order. The
// panel is 260 px wide in a window of 1200 px and 200 px in a window narrower than 1000 px, until the user drags the
// splitter.
static bool exerciseHeader() {
	DiagramWindow window("Header");
	auto* chart = buttonTestChart();
	window.setChart(chart);
	window.setCsvProvider([](const QStringList&) { return std::string(); }, "header.csv");
	const QString longContext = QStringLiteral("Case / Scenario · ") + QString(400, QLatin1Char('R'));
	window.setPresentation("Header", longContext);
	window.setMaximumSize(1200, 750);
	window.resize(1200, 750);
	window.show();
	QApplication::processEvents();
	auto* header = window.findChild<QFrame*>("resultHeader");
	auto* view = window.findChild<QChartView*>();
	auto* panel = window.findChild<QWidget*>("resultPanel");
	if (!expect(header && view && panel, "the window has a header, a chart and a panel")) return false;
	QList<QToolButton*> buttons = header->findChildren<QToolButton*>();
	std::sort(buttons.begin(), buttons.end(), [](const QToolButton* a, const QToolButton* b) { return a->x() < b->x(); });
	QStringList names, accessible;
	for (const QToolButton* button : buttons) {
		names << button->objectName();
		accessible << button->accessibleName();
	}
	bool ok = expect(names == QStringList({"diagramZoomOut", "diagramZoomIn", "diagramFit", "diagramPanelToggle"})
			&& accessible == QStringList({"Zoom Out", "Zoom In", "Fit", "Panel"}),
		"the header has Zoom Out, Zoom In, Fit and the panel button, from left to right");
	if (!ok) return false;
	QToolButton* zoomOut = buttons[0];
	QToolButton* zoomIn = buttons[1];
	QToolButton* fit = buttons[2];
	QToolButton* toggle = buttons[3];
	ok &= expect(zoomOut->toolTip() == "Zoom out (-)" && zoomIn->toolTip().startsWith("Zoom in (+)\nDrag a rectangle to zoom into it\n")
			&& fit->toolTip() == "Show the whole chart (Home or 0)\nScroll or the arrow keys to pan"
			&& toggle->toolTip() == "Show or hide the panel",
		"the tooltips of the header buttons name the keys, one fact a line");
	ok &= expect(!window.findChild<QLabel*>("diagramNavigationHelp") && !buttonNamed(window, "Reset zoom"),
		"the window has no help line and no Reset zoom button");
	const auto* context = window.findChild<QLabel*>("diagramContext");
	const QRect contextArea(context->mapTo(&window, QPoint()), context->size());
	const QRect zoomOutArea(zoomOut->mapTo(&window, QPoint()), zoomOut->size());
	ok &= expect(context->isVisible() && context->text() == longContext && context->height() < context->fontMetrics().height() * 2
			&& contextArea.right() < zoomOutArea.left() && QString(context->toolTip()).remove(QChar(0x200B)).contains(longContext)
			&& window.width() == 1200 && window.findChild<QSplitter*>("resultSplitter")->sizeHint().width() < context->fontMetrics().horizontalAdvance(longContext),
		"a long context is one line left of the buttons, whole in its tooltip, and widens neither the window nor its size hint");
	const QRect headerArea(header->mapTo(&window, QPoint()), header->size());
	const QRect viewArea(view->mapTo(&window, QPoint()), view->size());
	const QRect panelArea(panel->mapTo(&window, QPoint()), panel->size());
	ok &= expect(headerArea.bottom() < viewArea.top() && headerArea.right() < panelArea.left() && panelArea.top() == 0
			&& panelArea.bottom() == window.height() - 1,
		"the header lies over the chart only and the panel takes the full height at the right");
	ok &= expect(panel->width() == 260, "the panel opens 260 px wide in a window of 1200 px");

	auto* x = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
	const double low = x->min();
	const double high = x->max();
	zoomIn->click();
	ok &= expect(std::abs((x->max() - x->min()) - (high - low) / 1.2) < 1e-6 && std::abs((x->min() + x->max() - low - high) / 2) < 1e-6,
		"Zoom in narrows the chart around its centre");
	zoomOut->click();
	zoomOut->click();
	ok &= expect(x->min() == low && x->max() == high, "Zoom out widens the chart up to its full range");
	zoomIn->click();
	zoomIn->click();
	fit->click();
	ok &= expect(x->min() == low && x->max() == high && low == 0 && high > 0, "Fit shows the whole chart");

	const int chartWidth = view->width();
	toggle->click();
	QApplication::processEvents();
	ok &= expect(!toggle->isChecked() && !panel->isVisible() && view->width() > chartWidth + 200,
		"the panel button hides the panel and the chart takes its room");
	toggle->click();
	QApplication::processEvents();
	ok &= expect(toggle->isChecked() && panel->isVisible() && view->width() == chartWidth, "the panel button shows the panel again");

	window.activateWindow();
	QApplication::processEvents();
	ok &= expect(window.focusWidget() == view, "the window opens with the chart in focus");
	zoomOut->setFocus(Qt::TabFocusReason);
	QStringList order;
	for (int step = 0; step < 8 && QApplication::focusWidget(); ++step) {
		QWidget* focused = QApplication::focusWidget();
		if (qobject_cast<QChartView*>(focused))
			order << QStringLiteral("chart");
		else if (qobject_cast<TrainFilterButton*>(focused))
			order << QStringLiteral("trains");
		else
			order << focused->objectName();
		pressKey(focused, Qt::Key_Tab);
	}
	const QStringList expectedOrder{"diagramZoomOut", "diagramZoomIn", "diagramFit", "diagramPanelToggle", "chart", "trains",
		"diagramExportPng", "diagramExportCsv"};
	ok &= expect(order.mid(0, 8) == expectedOrder,
		qPrintable("Tab goes through the header, the chart and the panel: " + order.join(", ")));

	DiagramWindow narrow("Narrow");
	narrow.setChart(buttonTestChart());
	// The window opens at most 90 % as wide as the screen, which is 800 px offscreen.
	narrow.setMaximumSize(1200, 750);
	narrow.resize(900, 600);
	narrow.show();
	QApplication::processEvents();
	auto* narrowPanel = narrow.findChild<QWidget*>("resultPanel");
	ok &= expect(narrow.width() == 900 && narrowPanel->width() == 200, "the panel opens 200 px wide in a window of 900 px");
	narrow.resize(1200, 600);
	QApplication::processEvents();
	ok &= expect(narrow.width() == 1200 && narrowPanel->width() == 260, "the panel widens to 260 px when the window widens to 1200 px");
	auto* splitter = narrow.findChild<QSplitter*>("resultSplitter");
	QSplitterHandle* handle = splitter->handle(1);
	const QPoint grip = handle->rect().center();
	const QPoint dragged = grip - QPoint(40, 0);
	QMouseEvent press(QEvent::MouseButtonPress, grip, handle->mapToGlobal(grip), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent move(QEvent::MouseMove, dragged, handle->mapToGlobal(dragged), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent release(QEvent::MouseButtonRelease, dragged, handle->mapToGlobal(dragged), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(handle, &press);
	QApplication::sendEvent(handle, &move);
	QApplication::sendEvent(handle, &release);
	QApplication::processEvents();
	const int draggedWidth = narrowPanel->width();
	narrow.resize(900, 600);
	QApplication::processEvents();
	ok &= expect(draggedWidth == 300 && narrowPanel->width() == 300,
		qPrintable(QStringLiteral("the panel keeps the width of a drag of the splitter: %1, then %2").arg(draggedWidth).arg(narrowPanel->width())));
	return ok;
}

// The sections of the panel: Trains, Selected while a train is selected, and Export, with a rule between two shown sections.
// A rolling stock window has no train sections.
static bool exercisePanelSections() {
	const auto shownRules = [](const DiagramWindow& window) {
		int count = 0;
		for (const QFrame* rule : window.findChildren<QFrame*>())
			if (rule->property("resultRule").toBool() && rule->isVisible()) ++count;
		return count;
	};
	const auto shownHeadings = [](const DiagramWindow& window) {
		QList<const QLabel*> labels;
		for (const QLabel* label : window.findChildren<QLabel*>())
			if (label->property("resultSectionHeading").toBool() && label->isVisible()) labels << label;
		std::sort(labels.begin(), labels.end(),
			[&window](const QLabel* a, const QLabel* b) { return a->mapTo(&window, QPoint()).y() < b->mapTo(&window, QPoint()).y(); });
		QStringList texts;
		for (const QLabel* label : labels) texts << label->text();
		return texts;
	};
	DiagramWindow window("Panel sections");
	auto* chart = buttonTestChart();
	window.setChart(chart);
	window.show();
	QApplication::processEvents();
	bool ok = expect(shownHeadings(window) == QStringList({"Trains", "Export"}) && shownRules(window) == 1,
		"the panel shows Trains and Export with one rule between them");
	auto* view = window.findChild<QChartView*>();
	const QPoint sample = view->mapFromScene(chart->mapToScene(chart->mapToPosition(QPointF(70, 20), chart->series().last())));
	QMouseEvent press(QEvent::MouseButtonPress, sample, view->viewport()->mapToGlobal(sample), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent release(QEvent::MouseButtonRelease, sample, view->viewport()->mapToGlobal(sample), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(view->viewport(), &press);
	QApplication::sendEvent(view->viewport(), &release);
	QApplication::processEvents();
	ok &= expect(shownHeadings(window) == QStringList({"Trains", "Selected", "Export"}) && shownRules(window) == 2
			&& window.findChild<QLabel*>("diagramPinLabel")->text() == "B",
		"a selected train adds the section Selected with its id between Trains and Export");
	QMetaObject::invokeMethod(&window, "clearPin");
	QApplication::processEvents();
	ok &= expect(shownHeadings(window) == QStringList({"Trains", "Export"}) && shownRules(window) == 1, "Clear removes the section Selected");
	DiagramWindow input("Rolling stock panel");
	input.setProperty("inputTrainUnitId", QStringLiteral("Unit A"));
	input.setChart(buttonTestChart());
	input.show();
	QApplication::processEvents();
	ok &= expect(shownHeadings(input) == QStringList({"Export"}) && shownRules(input) == 0,
		"a rolling stock window shows only Export, without a rule");

	// A drag of the splitter to the right edge of the window stops at the panel's minimum width.
	auto* splitter = window.findChild<QSplitter*>("resultSplitter");
	auto* panel = window.findChild<QWidget*>("resultPanel");
	QSplitterHandle* handle = splitter ? splitter->handle(1) : nullptr;
	if (handle && panel) {
		const QPoint grab = handle->rect().center();
		const QPoint edge = handle->mapFromGlobal(window.mapToGlobal(QPoint(window.width() - 1, grab.y())));
		QMouseEvent down(QEvent::MouseButtonPress, grab, handle->mapToGlobal(grab), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QMouseEvent move(QEvent::MouseMove, edge, handle->mapToGlobal(edge), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QMouseEvent up(QEvent::MouseButtonRelease, edge, handle->mapToGlobal(edge), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(handle, &down);
		QApplication::sendEvent(handle, &move);
		QApplication::sendEvent(handle, &up);
		QApplication::processEvents();
	}
	ok &= expect(handle && panel && panel->isVisible() && panel->width() > 0 && splitter->sizes().value(1) > 0,
		"a drag of the splitter to the edge does not collapse the panel");
	return ok;
}

// The readout at the bottom of the panel gives the value under the pointer in the units of the axes, to the precision of
// one pixel of the plot, and a clock time in the place of a time axis. It is empty outside the plot and once the pointer
// leaves the chart, and follows the chart under a pointer that stays where it is while the wheel pans.
static bool exerciseReadout() {
	DiagramWindow window("Readout");
	auto* chart = new QChart;
	auto* line = new QLineSeries;
	line->append(0, 0);
	line->append(10, 100);
	chart->addSeries(line);
	chart->createDefaultAxes();
	auto* x = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
	auto* y = qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first());
	x->setRange(0, 10);
	x->setTitleText("Distance (km)");
	y->setRange(0, 100);
	y->setTitleText("Speed (km/h)");
	window.setChart(chart);
	window.setMaximumSize(1200, 750);
	window.resize(1200, 750);
	window.show();
	QApplication::processEvents();
	auto* view = window.findChild<QChartView*>();
	auto* readout = window.findChild<QLabel*>("resultReadout");
	if (!expect(view && readout && readout->text().isEmpty(), "the readout is empty before the pointer comes")) return false;
	const auto pointAt = [&](const QPointF& value) {
		moveTo(view, value, line);
		return readout->text();
	};
	const QRegularExpression numeric(QStringLiteral("^(\\d+\\.\\d{2}) km, (\\d+\\.\\d) km/h$"));
	const auto near = [&numeric](const QString& text, double km, double speed) {
		const QRegularExpressionMatch match = numeric.match(text);
		return match.hasMatch() && std::abs(match.captured(1).toDouble() - km) < 0.02 && std::abs(match.captured(2).toDouble() - speed) < 0.3;
	};
	const QString value = pointAt(QPointF(3, 40));
	bool ok = expect(near(value, 3, 40) && x->max() - x->min() < chart->plotArea().width() / 10
			&& y->max() - y->min() > chart->plotArea().height() / 10,
		qPrintable("the readout gives distance and speed to the precision of a pixel, with the units of the axes: " + value));
	ok &= expect(pointAt(QPointF(-1, 40)).isEmpty(), "the readout is empty left of the plot");
	pointAt(QPointF(3, 40));
	QEvent leave(QEvent::Leave);
	QApplication::sendEvent(view->viewport(), &leave);
	ok &= expect(readout->text().isEmpty(), "the readout is empty once the pointer leaves the chart");
	y->setReverse(true);
	ok &= expect(near(pointAt(QPointF(3, 40)), 3, 40), qPrintable("a reversed axis reads from its top: " + readout->text()));
	window.setTimeAxisY(true, 8 * 3600);
	QApplication::processEvents();
	const QString clock = pointAt(QPointF(3, 40));
	const QRegularExpression timed(QStringLiteral("^08:00:(39|40|41) at (\\d+\\.\\d{2}) km$"));
	const QRegularExpressionMatch match = timed.match(clock);
	ok &= expect(match.hasMatch() && std::abs(match.captured(2).toDouble() - 3) < 0.02,
		qPrintable("with a time axis the readout gives the clock time at the distance: " + clock));
	const QPointF still = chart->mapToPosition(QPointF(3, 40), line);
	wheel(view, {}, QPoint(0, 360), Qt::ControlModifier, still);
	pointAt(QPointF(3, 40));
	const QString zoomed = readout->text();
	wheel(view, QPoint(0, qRound(chart->plotArea().height() / 4)), {}, Qt::NoModifier, still);
	ok &= expect(!readout->text().isEmpty() && readout->text() != zoomed,
		qPrintable("the readout follows a wheel pan under a still pointer: " + readout->text()));
	return ok;
}

// The figure of Copy and PNG... has twice the pixels of the window and 144 dots per inch: the window title as heading, the
// context below it and the chart under both. It leaves out the tooltip and the rubber band that lie over the chart, and a
// later export can add a column at the right.
static bool exerciseFigure() {
	DiagramWindow window("Figure heading");
	auto* chart = buttonTestChart();
	window.setChart(chart);
	window.setPresentation("Figure heading", "Case / Scenario");
	window.setMaximumSize(1200, 750);
	window.resize(1200, 750);
	window.show();
	QApplication::processEvents();
	auto* view = window.findChild<QChartView*>();
	const QSize chartSize = view->viewport()->size();
	const QImage figure = window.figureImage();
	const int top = figure.height() / 2 - chartSize.height();
	bool ok = expect(figure.devicePixelRatio() == 2 && figure.width() == 2 * chartSize.width() && figure.height() % 2 == 0 && top > 0
			&& figure.dotsPerMeterX() == qRound(2 * window.logicalDpiX() / 0.0254)
			&& figure.dotsPerMeterY() == qRound(2 * window.logicalDpiY() / 0.0254),
		"the figure has twice the pixels of the chart and room above it, at twice the resolution of the screen");
	QFont headingFont = window.font();
	headingFont.setPointSizeF(headingFont.pointSizeF() * 1.25);
	headingFont.setBold(true);
	const int headingBottom = 16 + QFontMetrics(headingFont).height();
	// The heading is in the text colour, #24313a; the context is in the secondary text colour, #5b6871, and nothing in its
	// band is darker than that.
	const auto pixels = [&figure](int fromY, int toY, int below) {
		int count = 0;
		for (int row = fromY * 2; row < toY * 2; ++row)
			for (int column = 0; column < figure.width(); ++column) {
				const QColor color = figure.pixelColor(column, row);
				if (color.red() + color.green() + color.blue() < below) ++count;
			}
		return count;
	};
	ok &= expect(pixels(0, headingBottom, 200) > 20, "the heading paints in the text colour at the top");
	// The heading is as wide as the text in its font, so it is not drawn larger than the room made for it.
	int inkLeft = figure.width();
	int inkRight = -1;
	for (int row = 0; row < headingBottom * 2; ++row)
		for (int column = 0; column < figure.width(); ++column) {
			const QColor color = figure.pixelColor(column, row);
			if (color.red() + color.green() + color.blue() < 200) {
				inkLeft = std::min(inkLeft, column);
				inkRight = std::max(inkRight, column);
			}
		}
	const double inkWidth = (inkRight - inkLeft + 1) / 2.0;
	const int textWidth = QFontMetrics(headingFont).boundingRect("Figure heading").width();
	ok &= expect(std::abs(inkWidth - textWidth) <= 2 + textWidth * 0.1,
		qPrintable(QStringLiteral("the heading is drawn at the size of its font: %1 px of ink for %2 px of text").arg(inkWidth).arg(textWidth)));
	ok &= expect(pixels(headingBottom, top, 600) > 20 && pixels(headingBottom, top, 280) == 0,
		"the context paints in the secondary colour below the heading");
	ok &= expect(figure.copy(0, top * 2, figure.width(), figure.height() - top * 2) != QImage(figure.width(), figure.height() - top * 2, figure.format()),
		"the chart fills the figure below the heading");

	moveTo(view, QPointF(20, 30), chart->series().first());
	ok &= expect(window.findChild<QLabel*>("diagramTooltip")->isVisible() && window.figureImage() == figure,
		"the figure leaves out the tooltip");
	const QPoint from = view->mapFromScene(chart->mapToScene(chart->plotArea().center()));
	const QPoint to = from + QPoint(80, 60);
	QMouseEvent press(QEvent::MouseButtonPress, from, view->viewport()->mapToGlobal(from), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent drag(QEvent::MouseMove, to, view->viewport()->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(view->viewport(), &press);
	QApplication::sendEvent(view->viewport(), &drag);
	QApplication::processEvents();
	bool band = false;
	for (const QRubberBand* rubberBand : view->findChildren<QRubberBand*>()) band = band || rubberBand->isVisible();
	ok &= expect(band && window.figureImage() == figure, "the figure leaves out the rubber band of a drag");
	QMouseEvent release(QEvent::MouseButtonRelease, from, view->viewport()->mapToGlobal(from), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(view->viewport(), &release);

	FigureColumn column;
	column.width = 120;
	column.paint = [](QPainter& painter, const QRectF& area) { painter.fillRect(area, Qt::red); };
	window.findChild<QToolButton*>("diagramFit")->click();
	const QImage keyed = window.figureImage(column);
	const int middle = keyed.height() - chartSize.height();
	ok &= expect(keyed.width() == 2 * (chartSize.width() + 120) && keyed.pixelColor(keyed.width() - 4, middle) == QColor(Qt::red)
			&& keyed.pixelColor(2 * chartSize.width() - 4, middle) != QColor(Qt::red) && keyed.pixelColor(keyed.width() - 4, 4) != QColor(Qt::red),
		"a column adds its width at the right of the chart, beside the chart only");

	// The warning of the window goes into the figure, under the context and above the chart.
	window.setPresentation("Figure heading", "Case / Scenario", "Negative effort below the displayed 0 kN range.");
	QApplication::processEvents();
	const QImage warned = window.figureImage();
	const int warnedTop = warned.height() / 2 - view->viewport()->size().height();
	int warningInk = 0;
	for (int row = top * 2; row < warnedTop * 2; ++row)
		for (int x = 0; x < warned.width(); ++x) {
			const QColor color = warned.pixelColor(x, row);
			if (color.red() + color.green() + color.blue() < 600) ++warningInk;
		}
	ok &= expect(warnedTop > top && warningInk > 20, "the figure shows the warning of the window under the context");
	return ok;
}

// Copy with the chart in focus puts the figure on the clipboard; with a button or a text field in focus it does not.
static bool exerciseCopy() {
	QClipboard* clipboard = QGuiApplication::clipboard();
	if (!expect(clipboard, "the application has a clipboard")) return false;
	DiagramWindow window("Copy");
	window.setChart(buttonTestChart());
	window.setPresentation("Copy", "Case / Scenario");
	window.show();
	QApplication::processEvents();
	clipboard->clear();
	auto* view = window.findChild<QChartView*>();
	view->setFocus();
	const QKeyCombination copy = QKeySequence(QKeySequence::Copy)[0];
	QKeyEvent press(QEvent::KeyPress, copy.key(), copy.keyboardModifiers(), "c");
	QApplication::sendEvent(view, &press);
	const QImage copied = clipboard->image();
	bool ok = expect(!copied.isNull() && copied.size() == window.figureImage().size(), "Copy puts the figure on the clipboard");
	// Only the chart in focus takes Copy, not the rest of the window.
	auto* png = window.findChild<QPushButton*>("diagramExportPng");
	png->setFocus();
	clipboard->clear();
	QKeyEvent buttonPress(QEvent::KeyPress, copy.key(), copy.keyboardModifiers(), "c");
	QApplication::sendEvent(png, &buttonPress);
	ok &= expect(clipboard->image().isNull(), "Copy with a button in focus leaves the clipboard empty");
	// A text field in the window keeps Copy for its own text.
	auto* field = new QLineEdit("Field text", &window);
	field->show();
	field->setFocus();
	field->selectAll();
	clipboard->clear();
	QKeyEvent fieldPress(QEvent::KeyPress, copy.key(), copy.keyboardModifiers(), "c");
	QApplication::sendEvent(field, &fieldPress);
	ok &= expect(clipboard->text() == "Field text" && clipboard->image().isNull(), "Copy in a text field copies its text");
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
	QNativeGestureEvent gesture(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(), 2, center, center,
		view->viewport()->mapToGlobal(center), 0.2, QPointF(), 0);
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
	ok &= expect(x->min() >= 0 && y->min() >= 0 && x->max() <= 100 && y->max() <= 120,
		"pan stays inside the full bounds");
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
	QMouseEvent press(QEvent::MouseButtonPress, sample, view->viewport()->mapToGlobal(sample), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent release(QEvent::MouseButtonRelease, sample, view->viewport()->mapToGlobal(sample), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(view->viewport(), &press);
	QApplication::sendEvent(view->viewport(), &release);
	ok &= expect(selected == "A", "actual click retains train linkage");
	auto* list = window.findChild<QListWidget*>();
	ok &= expect(list && list->count() == 2, "train grouping");
	if (list) list->item(1)->setCheckState(Qt::Unchecked);
	ok &= expect(!other->isVisible() && !tooltip->isVisible(), "filter hides series and tooltip");
	QMetaObject::invokeMethod(&window, "resetZoom");
	ok &= expect(x->min() == 0 && x->max() == 100 && y->min() == 0 && y->max() == 120,
		"reset restores the caller bounds, widened to round ticks where Qt sets the ticks, rather than data bounds");
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
	empty.findChild<QToolButton*>("diagramFit")->click();
	ok &= expect(emptyX->min() == 0 && emptyX->max() == 200 && emptyY->max() == 600,
		"no-data non-time reset retains the round caller ranges");

	auto* rectangle = new QLineSeries;
	rectangle->setName("Block occupation");
	rectangle->setProperty("inspectionInterval", "Resource: B12 | Type: block | Start: 20 s | End: 60 s");
	rectangle->setProperty("inspectionFilled", true);
	rectangle->append(20, 100);
	rectangle->append(60, 100);
	rectangle->append(60, 200);
	rectangle->append(20, 200);
	rectangle->append(20, 100);
	emptyChart->addSeries(rectangle);
	rectangle->attachAxis(emptyX);
	rectangle->attachAxis(emptyY);
	moveTo(emptyView, QPointF(40, 150), rectangle);
	auto* emptyTooltip = empty.findChild<QLabel*>("diagramTooltip");
	ok &= expect(
		emptyTooltip->isVisible() && emptyTooltip->text().contains("B12") && !emptyTooltip->text().contains("150.00"),
		"block interior reports plotted corner and context");
	auto* events = new QScatterSeries;
	events->setName("Arrival");
	events->append(100, 300);
	events->append(140, 400);
	events->setProperty("inspectionPoints", QStringList{"Station: Delft | Arrival", "Station: Leiden | Departure"});
	emptyChart->addSeries(events);
	events->attachAxis(emptyX);
	events->attachAxis(emptyY);
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
	clipped->append(20, -4);
	clipped->append(60, -4);
	emptyChart->addSeries(clipped);
	clipped->attachAxis(emptyX);
	clipped->attachAxis(emptyY);
	moveTo(emptyView, QPointF(40, 2), clipped);
	ok &= expect(!emptyTooltip->isVisible(), "fully clipped line cannot be inspected inside plot");
	const QPoint clippedClick = emptyView->mapFromScene(emptyChart->mapToScene(
		emptyChart->mapToPosition(QPointF(40, 2), clipped)));
	QMouseEvent clippedPress(QEvent::MouseButtonPress, clippedClick, emptyView->viewport()->mapToGlobal(clippedClick),
		Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent clippedRelease(QEvent::MouseButtonRelease, clippedClick, emptyView->viewport()->mapToGlobal(clippedClick),
		Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
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
	dualSeries->append(0, 0);
	dualSeries->append(100, 100);
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
	const double expectedSpan = (dualY->max() - dualY->min()) / std::exp(0.2);
	wheel(dualView, {}, QPoint(0, 120), Qt::ControlModifier);
	ok &= expect(std::abs((dualY->max() - dualY->min()) - expectedSpan) < 0.01 && std::abs((dualCategory->max() - dualCategory->min()) - expectedSpan) < 0.01,
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
	lower->append(1, 10);
	lower->append(2, 10);
	upper->append(1, 20);
	upper->append(2, 20);
	auto* area = new QAreaSeries(upper, lower);
	area->setName("A envelope");
	area->setProperty("trainId", "A");
	area->setBrush(QColor(60, 100, 200, 65));
	stairChart->addSeries(area);
	auto* outline = new QLineSeries;
	outline->setName("A envelope");
	outline->setProperty("trainId", "A");
	outline->setProperty("inspectionFilled", true);
	outline->setProperty("inspectionInterval", "Calculated blocking envelope");
	for (const QPointF& p : {QPointF(1, 10), QPointF(2, 10), QPointF(2, 20), QPointF(1, 20), QPointF(1, 10)})
		outline->append(p);
	stairChart->addSeries(outline);
	auto* actual = new QLineSeries;
	actual->setName("A recorded trajectory");
	actual->setProperty("trainId", "A");
	actual->append(1.2, 14);
	actual->append(1.8, 16);
	stairChart->addSeries(actual);
	auto* overlap = new QLineSeries;
	overlap->setName("B envelope");
	overlap->setProperty("trainId", "B");
	overlap->setProperty("inspectionFilled", true);
	for (const QPointF& p : {QPointF(1, 10), QPointF(2, 10), QPointF(2, 20), QPointF(1, 20), QPointF(1, 10)})
		overlap->append(p);
	stairChart->addSeries(overlap);
	auto* otherActual = new QScatterSeries;
	otherActual->setName("B recorded trajectory");
	otherActual->setProperty("trainId", "B");
	otherActual->append(1.7, 18);
	stairChart->addSeries(otherActual);
	stairChart->createDefaultAxes();
	auto* stairY = qobject_cast<QValueAxis*>(stairChart->axes(Qt::Vertical).first());
	stairY->setReverse(true);
	stairY->setRange(0, 30);
	staircase.setChart(stairChart);
	staircase.setTimeAxisY(true);
	staircase.show();
	app.processEvents();
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
			&& stairY->isReverse(),
		"recorded layer draws over the downward-time area fill");
	const QString authoredId = QStringLiteral("Unit | ") + QString(120, QLatin1Char('Q'));
	const QString scientificWarning = QStringLiteral("Part of the curve is below 0 kN and is not shown.");
	DiagramWindow input("Traction Curve");
	QFont scaled = input.font();
	scaled.setPointSizeF(18);
	input.setFont(scaled);
	input.setProperty("inputTrainUnitId", QStringLiteral("Unit A"));
	auto* inputChart = new QChart;
	auto* inputLine = new QLineSeries;
	inputLine->setName("Input effort");
	inputLine->append(10, 20);
	inputLine->append(20, 30);
	inputChart->addSeries(inputLine);
	inputChart->createDefaultAxes();
	input.setPresentation("Traction Curve", authoredId + ", from traction.txt", scientificWarning);
	input.setChart(inputChart);
	input.show();
	app.processEvents();
	ok &= expect(
		!input.findChild<QWidget*>("diagramDetailsPanel") && !input.findChild<QWidget*>("diagramDetailsButton")
			&& !input.findChild<QWidget*>("diagramDetailsText"),
		"technical-detail widgets are absent");
	for (const auto* button : input.findChildren<QPushButton*>())
		ok &= expect(button->text() != "Technical details", "no technical-details action");
	const QRect screenArea = input.screen()->availableGeometry();
	ok &= expect(input.width() <= screenArea.width() * 9 / 10 && input.height() <= screenArea.height() * 4 / 5,
		"scaled diagram fits available screen with application QSS");
	ok &= expect(
		input.windowTitle() == "Traction Curve"
			&& input.findChild<QLabel*>("diagramContext")->text().contains(", from traction.txt")
			&& inputChart->title().isEmpty()
			&& input.findChild<QLabel*>("diagramContext")->isVisible()
			&& input.findChild<QLabel*>("diagramWarning")->text() == scientificWarning,
		"explicit presentation before setChart preserves identity and scientific warning with | in ID");
	ok &= expect(
		!input.findChild<TrainFilterButton*>()->isVisible()
			&& input.findChild<QLabel*>("diagramContext")->text().contains(authoredId)
			&& !input.findChild<QLabel*>("diagramNavigationHelp"),
		"input traction uses rolling-stock subject and no visible train filter");
	input.setPresentation("Updated heading", "Case C / Scenario D", scientificWarning);
	app.processEvents();
	ok &= expect(
		input.windowTitle() == "Updated heading"
			&& input.findChild<QLabel*>("diagramContext")->text() == "Case C / Scenario D"
			&& input.findChild<QLabel*>("diagramWarning")->text() == scientificWarning
			&& inputChart->title().isEmpty(),
		"explicit presentation after setChart keeps the warning and leaves the chart without a title");
	// Long provenance must never consume the space needed by scientific caveats: the context stays one line in the header,
	// and the warning lies below the header and above the chart.
	input.setMaximumWidth(qMin(640, input.maximumWidth()));
	input.resize(input.maximumWidth(), input.maximumHeight());
	const QString longContext = "Reference: " + QString(1500, QLatin1Char('R'))
		+ " | Case: " + QString(1500, QLatin1Char('C'));
	for (const QString& warning : {scientificWarning,
			 QStringLiteral("2 blocks or stops could not be placed on route0 and are not shown."),
			 QStringLiteral("2 blocks or stops could not be placed on route0 and are not shown. No blocking times could be placed on route0.")}) {
		input.setPresentation("Scientific interpretation", longContext, warning);
		app.processEvents();
		const auto* label = input.findChild<QLabel*>("diagramWarning");
		const auto* context = input.findChild<QLabel*>("diagramContext");
		const auto* inputView = input.findChild<QChartView*>();
		const QRect content = label->contentsRect();
		const QRect textBounds = label->fontMetrics().boundingRect(
			QRect(0, 0, content.width(), 10000), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, warning);
		const QRect warningArea(label->mapTo(&input, QPoint()), label->size());
		const QRect contextArea(context->mapTo(&input, QPoint()), context->size());
		const QRect viewArea(inputView->mapTo(&input, QPoint()), inputView->size());
		ok &= expect(
			label->text() == warning && label->isVisible() && label->height() >= label->heightForWidth(label->width())
				&& content.height() >= textBounds.height() && input.rect().contains(warningArea)
				&& contextArea.bottom() < warningArea.top() && warningArea.bottom() < viewArea.top()
				&& context->text() == longContext && context->height() < context->fontMetrics().height() * 2
				&& input.width() <= 640 && input.height() <= input.maximumHeight(),
			"complete warning fits between the one-line long provenance and the chart at enlarged font in bounded narrow window");
	}
	input.setPresentation("No warning", longContext);
	ok &= expect(input.findChild<QLabel*>("diagramWarning")->isHidden(), "absent warning uses no strip");
	window.setPresentation("No context", QString());
	ok &= expect(window.findChild<QLabel*>("diagramContext")->isHidden(), "empty context stays hidden");
	ok &= exerciseTrainFilter();
	ok &= exerciseTrainFilterRoom();
	ok &= exerciseMenuSeparator();
	ok &= exerciseTrainFilterKeysInWindow();
	ok &= exerciseTrainColors();
	ok &= exerciseLegend();
	ok &= exerciseButtonDefaults();
	ok &= exerciseRunContext();
	ok &= exerciseBlockSections();
	ok &= exercisePlainToolTips();
	ok &= exerciseLongToolTips();
	ok &= exerciseStationAxis();
	ok &= exerciseLongAxisTitle();
	ok &= exerciseRoundTicks();
	ok &= exerciseClockTicks();
	ok &= exerciseSingleVerticalAxis();
	ok &= exerciseCsvSavedMessage();
	ok &= exerciseHeader();
	ok &= exercisePanelSections();
	ok &= exerciseReadout();
	ok &= exerciseFigure();
	ok &= exerciseCopy();
	if (!ok) return 1;
	std::cout << "all DiagramWindow tests passed\n";
	return 0;
}
