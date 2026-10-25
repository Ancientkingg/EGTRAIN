#include "diagrams/DiagramKey.h"
#include "diagrams/DiagramWindow.h"
#include "diagrams/PositionScale.h"
#include "diagrams/RouteDiagramCoordinates.h"
#include "diagrams/TrainColors.h"
#include "diagrams/TrainFilterButton.h"
#include "diagrams/TrainKeyList.h"
#include "scene/SectionLabels.h"
#include "widgets/DisplayPath.h"
#include "widgets/PlainToolTip.h"

#include <QApplication>
#include <QClipboard>
#include <QGraphicsLayout>
#include <QGraphicsObject>
#include <QFontMetricsF>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMainWindow>
#include <QMessageBox>
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
#include <QPainter>
#include <QNativeGestureEvent>
#include <QPushButton>
#include <QRegularExpression>
#include <QRubberBand>
#include <QPointer>
#include <QScrollBar>
#include <QSplitter>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QtMath>
#include <QValueAxis>
#include <QWheelEvent>
#include <algorithm>
#include <array>
#include <functional>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <tuple>

static bool expect(bool condition, const char* message) {
	if (!condition) std::cerr << "failed: " << message << "\n";
	return condition;
}

// Runs the event loop until `done` is true, for at most about a second.
static bool waitFor(const std::function<bool()>& done) {
	for (int round = 0; round < 200 && !done(); ++round) {
		QApplication::processEvents(QEventLoop::AllEvents, 5);
		QThread::msleep(5);
	}
	return done();
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

// The Trains list of a diagram window.
static TrainKeyList* trainListOf(const QWidget& window) {
	return window.findChild<TrainKeyList*>("trainKeyList");
}

// The row of a train, or of a service group, in a Trains list; null when the list has none.
static QTreeWidgetItem* rowNamed(const TrainKeyList* list, const QString& text) {
	const auto found = list->tree()->findItems(text, Qt::MatchExactly | Qt::MatchRecursive);
	return found.isEmpty() ? nullptr : found.first();
}

// The colour in the middle of the line sample of a row.
static QColor lineSampleColour(const QTreeWidgetItem* row) {
	const QIcon icon = row->icon(0);
	return icon.isNull() ? QColor() : icon.pixmap(QSize(22, 14), 2.0).toImage().pixelColor(22, 14);
}

// The texts of the rows at the top of a Trains list, from the top, and of the open groups below their group.
static QStringList shownRowTexts(const TrainKeyList* list) {
	QStringList texts;
	for (int i = 0; i < list->tree()->topLevelItemCount(); ++i) {
		const QTreeWidgetItem* row = list->tree()->topLevelItem(i);
		if (row->isHidden()) continue;
		texts << row->text(0);
		if (row->isExpanded())
			for (int child = 0; child < row->childCount(); ++child)
				if (!row->child(child)->isHidden()) texts << row->child(child)->text(0);
	}
	return texts;
}

// The trains of a Trains list, each of its own service when `services` is empty.
static QVector<TrainKeyList::Train> trainsOf(const QStringList& ids, const QStringList& services = {}) {
	QVector<TrainKeyList::Train> trains;
	for (int i = 0; i < ids.size(); ++i) trains.append({ids[i], QColor(30, 90, 200), services.value(i)});
	return trains;
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

// Inside a diagram window the chart shortcuts leave the keys of the Trains list alone.
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
	TrainKeyList* trains = trainListOf(window);
	QTreeWidget* list = trains ? trains->tree() : nullptr;
	auto* x = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
	if (!expect(view && list && x && first && list->topLevelItemCount() == 3, "a diagram window lists its three trains"))
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
	ok &= expect(zoomedSpan < fullSpan, "plus zooms the chart before the list takes the focus");

	list->setFocus(Qt::TabFocusReason);
	list->setCurrentItem(list->topLevelItem(0), 0, QItemSelectionModel::NoUpdate);
	QApplication::processEvents();
	pressKey(list, Qt::Key_Down);
	pressKey(list, Qt::Key_End);
	ok &= expect(list->currentItem() == list->topLevelItem(2), "Down and End move to the last train of the list");
	pressKey(list, Qt::Key_Home);
	ok &= expect(list->currentItem() == list->topLevelItem(0) && x->max() - x->min() == zoomedSpan,
		"Home moves to the first train of the list and leaves the chart zoom alone");
	pressKey(list, Qt::Key_0, "0");
	pressKey(list, Qt::Key_Minus, "-");
	ok &= expect(x->max() - x->min() == zoomedSpan, "typing in the list does not zoom the chart");
	pressKey(list, Qt::Key_Home);
	pressKey(list, Qt::Key_Space, " ");
	ok &= expect(list->topLevelItem(0)->checkState(0) == Qt::Unchecked && trains->countLabel()->text() == "2 of 3" && !first->isVisible()
			&& trains->visibleTrainIds() == QStringList({"B", "C"}),
		"Space in the list hides the first train of the diagram");
	ok &= expect(list->topLevelItem(0)->text(0) == "A", "the hidden train keeps its row while the list has the focus");

	view->setFocus();
	QApplication::processEvents();
	ok &= expect(shownRowTexts(trains) == QStringList({"B", "C", "A"}), "the hidden train moves below the shown ones once the focus leaves");
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
		dashed->setProperty("planned", true);
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

// A pen of a muted train: opaque, in a colour between the colour of the train and the background, nearer the
// background. A transparent pen adds up where a chart draws the pieces of a dense line over each other.
static bool isMuted(const QPen& pen, const QColor& color, const QColor& background) {
	const QColor muted = pen.color();
	const auto channel = [](int value, int line, int back) {
		return (value - back) * (line - back) >= 0 && 2 * std::abs(value - back) <= std::abs(line - back) + 1;
	};
	return muted.alpha() == 255 && muted != color && channel(muted.red(), color.red(), background.red())
		&& channel(muted.green(), color.green(), background.green()) && channel(muted.blue(), color.blue(), background.blue());
}

static QColor swatchAt(const TrainKeyList* list, int row) {
	const QTreeWidgetItem* item = rowNamed(list, list->trainIds().value(row));
	return item ? lineSampleColour(item) : QColor();
}

// A blocking time of train `id` from `left` to `right` km and from `start` to `end` s, as the blocking-time windows draw
// one of `kind`: an area series for its fill and a closed outline.
static void addBlockingBox(QChart* chart, const QString& id, BlockingTimeKind kind, double left, double right, double start, double end) {
	auto* lower = new QLineSeries;
	auto* upper = new QLineSeries;
	lower->append(left, start);
	lower->append(right, start);
	upper->append(left, end);
	upper->append(right, end);
	auto* fill = new QAreaSeries(upper, lower);
	fill->setProperty("trainId", id);
	auto* outline = new QLineSeries;
	outline->setName(id);
	outline->setProperty("trainId", id);
	outline->setProperty("inspectionFilled", true);
	styleBlockingTime(fill, outline, kind);
	outline->setProperty("inspectionInterval",
		QString("Block: %1\nBlocked %2 to %3 s").arg(outline->property("keyKind").toString()).arg(start).arg(end));
	for (const QPointF& point : {QPointF(left, start), QPointF(right, start), QPointF(right, end), QPointF(left, end), QPointF(left, start)})
		outline->append(point);
	chart->addSeries(fill);
	chart->addSeries(outline);
}

// Clicks the chart of a window at `value` of `series`.
static void clickChart(QChartView* view, const QPointF& value, QAbstractSeries* series) {
	const QPoint point = view->mapFromScene(view->chart()->mapToScene(view->chart()->mapToPosition(value, series)));
	QMouseEvent press(QEvent::MouseButtonPress, point, view->viewport()->mapToGlobal(point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent release(QEvent::MouseButtonRelease, point, view->viewport()->mapToGlobal(point), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(view->viewport(), &press);
	QApplication::sendEvent(view->viewport(), &release);
}

// The colour rule paints the trains of a window: two trains take the first two colours of the palette in the order of
// the chart, and the colour that the case gives a service wins over the palette. A series keeps the width and dash
// pattern of its builder and an area series the alpha of its fill; a series whose "ownColour" property is set keeps its
// colours, and so does a chart of rolling stock input. The line samples of the Trains list follow, light grey for a
// hidden train.
static bool exerciseTrainColors() {
	const QColor blue(0x2a, 0x78, 0xd6), pink(0xc2, 0x44, 0x8a), green(60, 180, 75), hiddenGrey(0xc3, 0xcb, 0xd1);
	bool ok = true;
	for (const bool beforeChart : {false, true}) {
		const char* when = beforeChart ? " (map set before the chart)" : " (map set after the chart)";
		const auto note = [&](const char* text) { return (std::string(text) + when); };
		DiagramWindow window("Train colours");
		const ColoredChart built = makeColoredChart();
		const QHash<QString, QColor> colors{{"B", green}, {"Z", QColor(1, 2, 3)}, {"A", QColor()}};
		if (beforeChart) window.setTrainColors(colors);
		window.setChart(built.chart);
		if (!beforeChart) window.setTrainColors(colors);
		window.show();
		QApplication::processEvents();
		TrainKeyList* list = trainListOf(window);
		auto* view = window.findChild<QChartView*>();
		if (!expect(list && view && list->trainIds().size() == 2, "the coloured chart lists two trains"))
			return false;
		ok &= expect(isRecoloured(built.solid[0]->pen(), built.solidPen[0], blue)
				&& isRecoloured(built.dashed[0]->pen(), built.dashedPen[0], blue)
				&& built.solid[0]->pen().widthF() == 3.0 && built.dashed[0]->pen().widthF() == 2.0
				&& built.solid[0]->pen().style() == Qt::SolidLine && built.dashed[0]->pen().style() == Qt::DashLine,
			note("the line series of the first train take the first colour of the palette and keep width and dash pattern").c_str());
		ok &= expect(built.fill->brush().color() == QColor(blue.red(), blue.green(), blue.blue(), 65)
				&& built.fill->pen().color() == blue && built.fill->pen().widthF() == 1.5,
			note("the area series of a train takes its colour and keeps the fill alpha").c_str());
		ok &= expect(isRecoloured(built.solid[1]->pen(), built.solidPen[1], green) && isRecoloured(built.dashed[1]->pen(), built.dashedPen[1], green),
			note("the colour of the service of a train in the case wins over the palette").c_str());
		ok &= expect(swatchAt(list, 0) == blue && swatchAt(list, 1) == green,
			note("each line sample of the Trains list has the colour of its train").c_str());

		// Select train A by clicking its solid line.
		clickChart(view, QPointF(10, 10), built.solid[0]);
		ok &= expect(built.solid[0]->pen().widthF() > 3.0 && built.solid[0]->pen().color() == blue
				&& isMuted(built.solid[1]->pen(), green, view->chart()->backgroundBrush().color()),
			note("selecting a train emphasises it in its colour and mutes the other train in an opaque faded colour").c_str());
		list->setTrainVisible("B", false);
		ok &= expect(swatchAt(list, 1) == hiddenGrey && built.solid[0]->pen().color() == blue,
			note("a hidden train has a light grey line sample, and a train of two keeps its colour").c_str());
		// New colours keep which trains are shown and which is selected.
		window.setTrainColors(colors);
		ok &= expect(list->visibleTrainIds() == QStringList({"A"}) && !built.solid[1]->isVisible() && built.solid[0]->pen().widthF() > 3.0
				&& window.findChild<QLabel*>("diagramSelectedTitle")->text() == "Train A (simulated)",
			note("setting the colours again keeps a hidden train hidden and the selected train selected").c_str());
		list->setTrainVisible("B", true);
		QMetaObject::invokeMethod(&window, "clearPin");
		ok &= expect(isRecoloured(built.solid[0]->pen(), built.solidPen[0], blue)
				&& isRecoloured(built.dashed[0]->pen(), built.dashedPen[0], blue)
				&& isRecoloured(built.solid[1]->pen(), built.solidPen[1], green) && swatchAt(list, 1) == green
				&& built.fill->brush().color().alpha() == 65,
			note("clearing the selection and showing the train again leave the colours of the trains").c_str());
	}

	// Without colours of the case, and with an unknown train or an invalid colour, the palette colours the trains. A series
	// whose ownColour property is set keeps its colours.
	DiagramWindow window("Own colours");
	const ColoredChart built = makeColoredChart();
	built.fill->setProperty("ownColour", true);
	built.dashed[1]->setProperty("ownColour", true);
	window.setChart(built.chart);
	window.setTrainColors({{"Z", green}, {"A", QColor()}});
	TrainKeyList* list = trainListOf(window);
	ok &= expect(isRecoloured(built.solid[0]->pen(), built.solidPen[0], blue) && isRecoloured(built.solid[1]->pen(), built.solidPen[1], pink)
			&& list && swatchAt(list, 0) == blue && swatchAt(list, 1) == pink,
		"an unknown train id and an invalid colour leave the colours of the palette");
	ok &= expect(built.fill->pen() == built.fillPen && built.fill->brush() == built.fillBrush && built.dashed[1]->pen() == built.dashedPen[1],
		"a series whose ownColour property is set keeps the colours of its builder");

	// A chart of rolling stock input keeps the colours of its curves.
	DiagramWindow input("Rolling stock colours");
	input.setRollingStockSubject(QStringLiteral("Unit A"));
	const ColoredChart curves = makeColoredChart();
	input.setChart(curves.chart);
	ok &= expect(curves.solid[0]->pen() == curves.solidPen[0] && curves.solid[1]->pen() == curves.solidPen[1],
		"a chart of rolling stock input keeps the colours of its curves");
	return ok;
}

// The colour rule follows the trains that are shown and changes no check state. 47 trains in as many services are grey,
// and the selected train is blue; with 41 of them hidden each of the six shown trains takes a colour of the palette and
// the 41 stay hidden, also when the colours and the services are set again; a seventh shown train makes them grey again.
static bool exerciseColourRule() {
	const QColor grey(0x8a, 0x97, 0xa1), hiddenGrey(0xc3, 0xcb, 0xd1);
	const QColor palette[] = {QColor(0x2a, 0x78, 0xd6), QColor(0xc2, 0x44, 0x8a), QColor(0x4c, 0x8a, 0x12), QColor(0x61, 0x27, 0xb1),
		QColor(0xe3, 0x5b, 0x6b), QColor(0x84, 0x2a, 0x65)};
	const auto rgb = [](const QColor& colour) { return colour.rgb(); };
	DiagramWindow window("Colour rule");
	auto* chart = new QChart;
	QVector<QLineSeries*> lines;
	for (int i = 0; i < 47; ++i) {
		auto* line = new QLineSeries;
		line->setName(QString("T%1").arg(i));
		line->append(0, i * 10);
		line->append(50, i * 10 + 2);
		line->append(100, i * 10 + 4);
		chart->addSeries(line);
		lines.append(line);
	}
	chart->createDefaultAxes();
	window.setChart(chart);
	window.resize(1200, 750);
	window.show();
	QApplication::processEvents();
	TrainKeyList* list = trainListOf(window);
	auto* view = window.findChild<QChartView*>();
	if (!expect(list && view && list->trainIds().size() == 47, "the window lists 47 trains")) return false;
	bool ok = true;
	ok &= expect(std::all_of(lines.begin(), lines.end(), [&](const QLineSeries* line) { return line->pen().color() == grey; })
			&& swatchAt(list, 10) == grey,
		"47 trains in 47 services are grey");
	clickChart(view, QPointF(50, 102), lines[10]);
	ok &= expect(window.findChild<QLabel*>("diagramSelectedTitle")->text() == "T10" && rgb(lines[10]->pen().color()) == rgb(palette[0])
			&& isMuted(lines[11]->pen(), grey, view->chart()->backgroundBrush().color()) && swatchAt(list, 10) == palette[0],
		"the selected train of 47 is blue and the others stay grey, faded");
	QMetaObject::invokeMethod(&window, "clearPin");
	ok &= expect(lines[10]->pen().color() == grey, "a train that is no longer selected is grey again");

	for (int i = 6; i < 47; ++i) list->setTrainVisible(QString("T%1").arg(i), false);
	QStringList six;
	bool coloured = true;
	for (int i = 0; i < 6; ++i) {
		six << QString("T%1").arg(i);
		coloured &= lines[i]->pen().color() == palette[i] && swatchAt(list, i) == palette[i];
	}
	const auto hiddenStay = [&] {
		bool hidden = list->visibleTrainIds() == six;
		for (int i = 6; i < 47; ++i) hidden &= !lines[i]->isVisible() && swatchAt(list, i) == hiddenGrey;
		return hidden;
	};
	ok &= expect(coloured && hiddenStay(), "hiding 41 of 47 trains gives each of the six shown a colour of the palette, and the 41 stay hidden");
	window.setTrainColors({});
	window.setTrainServices({});
	ok &= expect(hiddenStay() && lines[5]->pen().color() == palette[5], "setting the colours and the services again keeps the 41 hidden");
	list->setTrainVisible("T6", true);
	ok &= expect(lines[0]->pen().color() == grey && lines[6]->pen().color() == grey && list->visibleTrainIds().size() == 7,
		"a seventh shown train in a seventh service makes the trains grey");
	return ok;
}

// Blocking times have the look of their kind. An ordinary one has a neutral grey fill and an outline 1.4 px wide in the
// colour of its train, and its sample in the Key is the grey box with a neutral outline. A critical block is amber with a
// hatch and an amber-brown outline, and a conflict is dark red with a cross hatch; both keep their colours whatever the
// train, and their samples in the Key have the hatch. A figure at twice the size of the window draws the hatch sharp.
static bool exerciseBlockingTimeLook() {
	const QColor blue(0x2a, 0x78, 0xd6), pink(0xc2, 0x44, 0x8a), neutral(0x5b, 0x68, 0x71);
	DiagramWindow window("Blocking time look");
	auto* chart = new QChart;
	addBlockingBox(chart, "A", BlockingTimeKind::Ordinary, 0, 2, 0, 40);
	addBlockingBox(chart, "B", BlockingTimeKind::Ordinary, 0, 2, 50, 90);
	addBlockingBox(chart, "B", BlockingTimeKind::CriticalBlock, 2, 6, 10, 90);
	addBlockingBox(chart, "A", BlockingTimeKind::Conflict, 6, 8, 10, 50);
	chart->createDefaultAxes();
	chart->axes(Qt::Horizontal).first()->setRange(0, 10);
	chart->axes(Qt::Vertical).first()->setRange(0, 100);
	window.setChart(chart);
	window.setMaximumSize(1200, 750);
	window.resize(1200, 750);
	window.show();
	QApplication::processEvents();
	const auto series = chart->series();
	const auto* fillA = qobject_cast<QAreaSeries*>(series[0]);
	const auto* outlineA = qobject_cast<QXYSeries*>(series[1]);
	const auto* outlineB = qobject_cast<QXYSeries*>(series[3]);
	const auto* criticalFill = qobject_cast<QAreaSeries*>(series[4]);
	auto* critical = qobject_cast<QXYSeries*>(series[5]);
	const auto* conflictFill = qobject_cast<QAreaSeries*>(series[6]);
	const auto* conflict = qobject_cast<QXYSeries*>(series[7]);
	auto* key = window.findChild<DiagramKey*>("diagramKey");
	if (!expect(fillA && outlineA && outlineB && criticalFill && critical && conflictFill && conflict && key, "the chart has four blocking times"))
		return false;
	bool ok = true;
	ok &= expect(outlineA->pen() == QPen(blue, 1.4) && outlineB->pen() == QPen(pink, 1.4) && fillA->brush().style() == Qt::SolidPattern
			&& fillA->brush().color().rgba() == qRgba(neutral.red(), neutral.green(), neutral.blue(), 26),
		qPrintable("an ordinary blocking time has a neutral fill and an outline 1.4 px wide in the colour of its train: "
			+ outlineA->pen().color().name() + QString(" %1 px").arg(outlineA->pen().widthF())));
	ok &= expect(critical->pen() == QPen(QColor(0x8f, 0x62, 0x00), 1.3) && conflict->pen() == QPen(QColor(0x9e, 0x1b, 0x1b), 1.4),
		qPrintable("the outlines of a critical block and a conflict keep their colours: " + critical->pen().color().name() + ", "
			+ conflict->pen().color().name()));
	ok &= expect(criticalFill->brush().style() == Qt::TexturePattern && conflictFill->brush().style() == Qt::TexturePattern
			&& criticalFill->brush().textureImage().devicePixelRatio() == 2.0,
		"critical blocks and conflicts are hatched with textures at two pixels per pixel");
	const QVector<DiagramKeyEntry> entries = key->entries();
	const auto entryNamed = [&entries](const QString& name) {
		const auto found = std::find_if(entries.begin(), entries.end(), [&name](const DiagramKeyEntry& entry) { return entry.name == name; });
		return found == entries.end() ? DiagramKeyEntry() : *found;
	};
	const DiagramKeyEntry ordinary = entryNamed("Blocking time");
	ok &= expect(ordinary.shape == DiagramKeyEntry::Shape::Box && ordinary.pen.color().rgb() == neutral.rgb() && ordinary.pen.widthF() == 1.4
			&& ordinary.brush == fillA->brush(),
		qPrintable("the Key shows a blocking time as the grey box with a neutral outline: " + ordinary.pen.color().name()));
	ok &= expect(entryNamed("Critical block").brush.style() == Qt::TexturePattern && entryNamed("Critical block").pen == critical->pen()
			&& entryNamed("Conflict").brush.style() == Qt::TexturePattern && entryNamed("Conflict").pen == conflict->pen(),
		"the Key shows a critical block and a conflict with their hatch and outline");
	// The hatch of a critical block runs up to the right, that of a conflict both ways: in the tile of 14 pixels, the pixel
	// (2, 11) lies on the line up to the right, (2, 2) on the line down to the right and (7, 2) on neither.
	const auto inked = [](const QImage& tile, int x, int y) { return qAlpha(tile.pixel(x, y)) > qAlpha(tile.pixel(7, 2)) + 40; };
	const QImage criticalTile = criticalFill->brush().textureImage();
	const QImage conflictTile = conflictFill->brush().textureImage();
	ok &= expect(inked(criticalTile, 2, 11) && !inked(criticalTile, 2, 2) && inked(conflictTile, 2, 11) && inked(conflictTile, 2, 2),
		"a critical block is hatched up to the right and a conflict both ways");

	// Along a row through the middle of the critical block the hatch repeats every 7 px, 14 pixels of the figure, and each
	// line has a sharp edge: a step of the grey value of 45 or more between two pixels, where a texture at one pixel per
	// pixel, scaled up, steps by 12 at most.
	auto* view = window.findChild<QChartView*>();
	const QImage figure = window.figureImage();
	const double top = figure.height() / figure.devicePixelRatio() - view->viewport()->height();
	const auto pixelOf = [&](const QPointF& value) {
		const QPoint at = view->mapFromScene(chart->mapToScene(chart->mapToPosition(value, critical)));
		return QPoint(at.x() * 2, qRound((at.y() + top) * 2));
	};
	const int row = pixelOf(QPointF(4, 50)).y();
	const int from = pixelOf(QPointF(2.5, 50)).x();
	const int to = pixelOf(QPointF(5.5, 50)).x();
	QVector<int> darkest;
	int sharpest = 0;
	for (int x = from + 1; x < to - 1; ++x) {
		const int grey = qGray(figure.pixel(x, row));
		sharpest = std::max(sharpest, std::abs(grey - qGray(figure.pixel(x + 1, row))));
		if (grey < qGray(figure.pixel(x - 1, row)) && grey <= qGray(figure.pixel(x + 1, row)) && grey < 170) darkest << x;
	}
	bool period = darkest.size() >= 4;
	for (int i = 1; i < darkest.size(); ++i) period &= std::abs(darkest[i] - darkest[i - 1] - 14) <= 1;
	ok &= expect(period && sharpest >= 45,
		qPrintable(QString("the figure at twice the size draws the hatch sharp, every 14 px: %1 lines, steps up to %2")
				.arg(darkest.size())
				.arg(sharpest)));

	// The window draws the hatch at one pixel per pixel, scaled down smoothly from the texture, so that each of the lines
	// in 80 px of a row, one every 7 px, keeps a grey of 170 or darker; scaled without smoothing they fade to 179.
	const QImage screen = view->grab().toImage();
	const QPoint middle = view->mapFromScene(chart->mapToScene(chart->mapToPosition(QPointF(4, 50), critical)));
	int screenLines = 0;
	for (int x = middle.x() - 40; x < middle.x() + 40; ++x) {
		const int grey = qGray(screen.pixel(x, middle.y()));
		if (grey < 170 && grey < qGray(screen.pixel(x - 1, middle.y())) && grey <= qGray(screen.pixel(x + 1, middle.y()))) ++screenLines;
	}
	ok &= expect(screenLines >= 10 && screenLines <= 12,
		qPrintable(QString("the window draws the hatch smoothly at one pixel per pixel: %1 lines in 80 px").arg(screenLines)));
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

// The chart of makeNamedChart with the series at plannedIndex marked planned.
static QChart* makePlannedChart(const QStringList& names, int plannedIndex) {
	QChart* chart = makeNamedChart(names);
	chart->series()[plannedIndex]->setProperty("planned", true);
	return chart;
}

// The names of the rows of the Key of the window; empty while the Key is hidden.
static QStringList legendKinds(const DiagramWindow& window) {
	const auto* key = window.findChild<DiagramKey*>("diagramKey");
	const QStringList kinds = key ? key->property("kinds").toStringList() : QStringList();
	return key && !key->parentWidget()->isHidden() ? kinds : QStringList();
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
	auto* pin = window.findChild<QLabel*>("diagramSelectedTitle");
	ok &= expect(pin && pin->isVisible() && pin->text() == "Train A", "click selects train A in the button test");
	if (!pin) return false;
	ok &= expect(clear->isVisible(), "Clear shows together with the selection");
	view->setFocus();
	pressKey(view, Qt::Key_Return);
	pressKey(view, Qt::Key_Enter);
	ok &= expect(pin->text() == "Train A", "Enter in the chart keeps the selection");
	clickTrain();
	ok &= expect(pin->text() == "Train A", "second click selects train A in the button test");
	pressKey(&window, Qt::Key_Return);
	ok &= expect(pin->text() == "Train A", "Enter in the window keeps the selection");
	ok &= expect(calls == 0, "Enter starts no CSV export");
	clear->click();
	ok &= expect(!clear->isVisible() && !pin->isVisible(), "Clear hides itself and the selection");
	DiagramWindow input("Input traction");
	input.setRollingStockSubject("unit-a");
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
	ok &= expect(legendKinds(window).isEmpty(), "sections without geometry draw no boundaries and get no Key row");
	const std::string authored = "@repeat<&>" + std::string(80, 'X') + "@";
	auto extents = buildBlockSectionExtents({{authored, 1, 3}, {authored, 1, 3}, {"@reverse@", 8, 5}, {"@missing<&>@", 4, 4}});
	window.setBlockSections(extents, "reference<&>");
	ok &= expect(legendKinds(window) == QStringList({"Block boundary"}), "the Key names the drawn section boundaries");
	DiagramWindow sectionsOnly("Physical sections only");
	auto* empty = new QChart;
	auto* km = new QValueAxis;
	km->setRange(0, 10);
	empty->addAxis(km, Qt::AlignBottom);
	sectionsOnly.setChart(empty);
	sectionsOnly.setBlockSections(extents, "reference<&>");
	ok &= expect(legendKinds(sectionsOnly) == QStringList({"Block boundary"}), "a chart with only section boundaries names them in the Key");
	// Wide enough beside the panel that the label of the long section at 1 to 3 km ends before the section at 5 to 8 km.
	window.setMaximumSize(1200, 640);
	window.resize(1200, 640);
	window.show();
	QApplication::processEvents();
	auto* view = window.findChild<QChartView*>();
	auto* tooltip = window.findChild<QLabel*>("diagramTooltip");
	auto* overlay = chart->findChild<QGraphicsObject*>("blockSectionOverlay");
	TrainKeyList* list = trainListOf(window);
	if (!expect(overlay && list && list->trainIds().size() == 2 && chart->series().size() == 3, "physical layer adds no train/filter/series identities")) return false;
	ok &= expect(overlay->parentItem() == chart && overlay->property("sectionCount").toInt() == 4,
		"chart owns all repeated/coincident and missing physical occurrences");
	const auto chartPoint = [&](double km, double offsetY) {
		const QRectF plot = chart->plotArea();
		return QPointF(plot.left() + (km - x->min()) / (x->max() - x->min()) * plot.width(), plot.top() + offsetY);
	};
	// The strip and its notice stand above the plot, as high as the overlay says.
	const auto headerPoint = [&](double km, double offsetY) {
		const auto* layer = chart->findChild<QGraphicsObject*>("blockSectionOverlay");
		return chartPoint(km, offsetY - layer->property("headerHeight").toDouble());
	};
	const auto hover = [&](const QPointF& point) {
		const QPoint position = view->mapFromScene(chart->mapToScene(point));
		QMouseEvent move(QEvent::MouseMove, position, view->viewport()->mapToGlobal(position), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(view->viewport(), &move);
		return position;
	};
	const QPoint header = hover(headerPoint(2, 5));
	const QString shortAuthored = QString::fromStdString("repeat<&>" + std::string(80, 'X'));
	ok &= expect(tooltip->isVisible() && tooltip->textFormat() == Qt::PlainText
			&& tooltip->text().startsWith(shortAuthored + "\nBlock section\nSection 1 of 4\nPosition 1.000 to 3.000 km\n")
			&& tooltip->text().contains("Section 2 of 4") && tooltip->text().contains("1.000 to 3.000 km along reference<&>"),
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
	ok &= expect(selected.isEmpty() && tooltip->isVisible() && tooltip->text().startsWith("reverse\nBlock section\nSection 3 of 4"),
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
	const double rowHeight = overlay->property("stripHeight").toDouble();
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
	for (const QString& id : list->trainIds()) list->setTrainVisible(id, false);
	hover(headerPoint(2, 5));
	ok &= expect(!first->isVisible() && !second->isVisible() && tooltip->isVisible() && overlay->isVisible(),
		"hiding every train retains section labels and inspection");
	std::vector<BlockSectionInput> narrowSections;
	for (int index = 0; index < 40; ++index)
		narrowSections.push_back({"@section-n" + std::to_string(index) + "@", index * 0.25, (index + 1) * 0.25});
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
	const double labelWidth = QFontMetricsF(window.font()).horizontalAdvance("section-n0");
	const QPointF expandedLabel(plot.left() + labelWidth - 2, headerPoint(0, 0).y() + 5);
	ok &= expect(expandedLabel.x() > chartPoint(0.25, 0).x() + 3,
		"crowded label test reaches visible text beyond its physical interval");
	hover(expandedLabel);
	ok &= expect(tooltip->isVisible() && tooltip->text().startsWith("section-n0\n") && !tooltip->text().contains("section-n1"),
		"expanded visible label reports its full ID rather than the neighbouring physical interval");
	// With labels the strip still shows the id, and the hover gives the label on the line after the id.
	SceneSectionLabels crowdedLabels;
	crowdedLabels.byReference = {{"@section-n0@", "T 0.000 to 0.250 km"}};
	window.setBlockSections(buildBlockSectionExtents(narrowSections), "crowded", &crowdedLabels);
	QApplication::processEvents();
	view->grab(); // paints the strip, which records its labels
	hover(QPointF(plot.left() + labelWidth - 2, headerPoint(0, 0).y() + 5));
	const auto* crowdedStrip = chart->findChild<QGraphicsObject*>("blockSectionOverlay");
	ok &= expect(crowdedStrip->property("stripLabels").toStringList().contains("section-n0") && tooltip->isVisible()
			&& tooltip->text().startsWith("section-n0\nT 0.000 to 0.250 km\nBlock section\nSection 1 of 40\n"
										  "Position 0.000 to 0.250 km\n0.000 to 0.250 km along crowded"),
		"the strip names a section by its id, and the hover by its id and label");
	window.setBlockSections(extents, "reference<&>");
	overlay = chart->findChild<QGraphicsObject*>("blockSectionOverlay");
	// The boundaries lie in a layer of the chart under the series: QChart stacks its series at 4.
	auto* boundaries = chart->findChild<QGraphicsObject*>("blockBoundaryLines");
	ok &= expect(boundaries && boundaries->parentItem() == chart && boundaries->zValue() > 3 && boundaries->zValue() < 4,
		"the block boundaries are drawn under the train lines and markers");
	const auto boundaryPainted = [&] {
		QApplication::processEvents();
		auto* layer = chart->findChild<QGraphicsObject*>("blockBoundaryLines");
		const QImage with = view->grab().toImage();
		layer->hide();
		const QImage without = view->grab().toImage();
		layer->show();
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
	// The boundary at the start of the section from 8 to 5 km.
	hover(chartPoint(8, 60));
	ok &= expect(tooltip->isVisible() && tooltip->text().startsWith("reverse\n") && tooltip->text().contains("Position 8.000 to 5.000 km")
			&& tooltip->text().contains("8.000 to 5.000 km along reference<&>"),
		"a section towards decreasing X reports its directed ends in hover");
	hover(headerPoint(2, overlay->property("stripHeight").toDouble() + 5));
	ok &= expect(tooltip->text().contains("missing<&>: zero length") && !tooltip->text().contains("@missing")
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
	ok &= expect(only->series().isEmpty() && trainListOf(window)->visibleTrainIds().isEmpty()
			&& tooltip->isVisible(),
		"explicit numeric axes allow section-only inspection without fake trains");
	QStringList exportedIds;
	window.setCsvProvider([&exportedIds](const QStringList& ids) {
		exportedIds = ids;
		return std::string("retained data");
	},
		"unmapped.csv", {"unrelated"});
	window.csvContent();
	list = trainListOf(window);
	ok &= expect(only->series().isEmpty() && list->trainIds() == QStringList{"unrelated"} && exportedIds == QStringList{"unrelated"},
		"real CSV-only train reaches the window provider without a dummy series");
	list->setTrainVisible("unrelated", false);
	window.csvContent();
	ok &= expect(exportedIds.isEmpty() && only->series().isEmpty(), "CSV-only train can be hidden with the actual filter");
	list->setTrainVisible("unrelated", true);
	ok &= expect(list->visibleTrainIds() == QStringList{"unrelated"}, "the CSV-only identity is a checked entry before the provider goes");
	window.setCsvProvider({}, {});
	ok &= expect(list->visibleTrainIds().isEmpty() && list->trainIds().isEmpty(),
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
			ok &= expect(texts.contains("08:00") && texts.contains("09:00"), qPrintable(what + ": the clock labels are whole"));
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
	// The chart hides a label that starts left of its axes, inside its own margin and the margin of its layout. The name
	// of the first station is as wide as a label centred on the left edge of the plot that starts 4 px left of the axes,
	// so it gets no label.
	const auto showStations = [](DiagramWindow& window, const QStringList& names, QCategoryAxis** stations) {
		std::vector<StationTick> ticks;
		window.setChart(routeChartWithStations(names, stations, &ticks));
		window.setStationAxis(*stations, ticks);
		window.setTimeAxisY(true, 8 * 3600);
		window.setMaximumSize(1152, 640);
		window.resize(1152, 640);
		window.show();
		return drawnChartTexts(window);
	};
	double plotLeft = 0, axesLeft = 0;
	QFont labelFont;
	{
		DiagramWindow probe("Timetable graph");
		QCategoryAxis* stations = nullptr;
		showStations(probe, {"A", "B"}, &stations);
		QChart* chart = probe.findChild<QChartView*>()->chart();
		qreal left = 0, top = 0, right = 0, bottom = 0;
		chart->layout()->getContentsMargins(&left, &top, &right, &bottom);
		plotLeft = chart->plotArea().left();
		axesLeft = left + chart->margins().left();
		labelFont = stations->labelsFont();
	}
	QString wide;
	while (QFontMetricsF(labelFont).horizontalAdvance(wide) < 2 * (plotLeft - axesLeft + 4)) wide += 'x';
	DiagramWindow window("Timetable graph");
	QCategoryAxis* stations = nullptr;
	const QStringList texts = showStations(window, {wide, "B"}, &stations);
	const QStringList labels = stations->categoriesLabels();
	const QString sizes = QString("%1 px wide, plot at %2 px, axes at %3 px").arg(QFontMetricsF(labelFont).horizontalAdvance(wide)).arg(plotLeft).arg(axesLeft);
	ok &= expect(labels.size() == 1 && !labels.first().startsWith(wide) && texts.contains(labels.first()),
		qPrintable("a station label that would start left of the chart margin is left out: " + sizes));
	return ok;
}

// When not all station names fit, a station with calls is named before one without, with as many names as before.
// When they all fit, every station is named.
static bool exerciseBusyStationLabels() {
	const QStringList names = {"Amsterdam Centraal", "Utrecht Centraal", "Den Bosch", "Eindhoven Centraal", "Venlo", "Roermond",
		"Maastricht", "Sittard", "Heerlen", "G&d<g"};
	// The stations named on a chart of `stations` at the given size, with `calls` at the station of index `busy`.
	const auto named = [](const QStringList& stations, int width, int height, int busy, int calls) {
		DiagramWindow window("Timetable graph");
		QCategoryAxis* axis = nullptr;
		std::vector<StationTick> ticks;
		window.setChart(routeChartWithStations(stations, &axis, &ticks));
		if (busy >= 0) ticks[static_cast<std::size_t>(busy)].calls = calls;
		window.setStationAxis(axis, ticks);
		window.setTimeAxisY(true, 8 * 3600);
		window.setMaximumSize(width, height);
		window.resize(width, height);
		window.show();
		QApplication::processEvents();
		QStringList result;
		for (const QString& label : axis->categoriesLabels()) {
			QTextDocument plain;
			plain.setHtml(label);
			const QString text = plain.toPlainText();
			const auto name = std::find_if(stations.begin(), stations.end(), [&](const QString& one) { return text.startsWith(one); });
			if (name != stations.end()) result << *name;
		}
		return result;
	};
	bool ok = true;
	const QStringList quiet = named(names, 768, 426, -1, 0);
	int busy = -1;
	for (int i = 1; i + 1 < names.size() && busy < 0; ++i)
		if (!quiet.contains(names[i])) busy = i;
	ok &= expect(busy > 0, "at 768 by 426 px some station between the ends is not named");
	if (busy > 0) {
		const QStringList withCalls = named(names, 768, 426, busy, 5);
		ok &= expect(withCalls.contains(names[busy]) && withCalls.size() == quiet.size(),
			qPrintable(QString("the busy station %1 is named, with %2 names as before: %3").arg(names[busy]).arg(quiet.size()).arg(withCalls.join(", "))));
	}
	const QStringList few = {"Gouda", "Woerden", "Utrecht"};
	ok &= expect(named(few, 1152, 640, -1, 0) == few && named(few, 1152, 640, 1, 5) == few, "when all names fit, every station is named");
	return ok;
}

// The Key lists the kinds of mark of the shown series, each with the pen and brush of its series, in a fixed order: the
// kind of a series is its "keyKind" property, else its layer, else planned or simulated. Simulated is left out while it
// is the only kind of line, and the Key hides while it has no row.
// A dashed line with a dot keeps a gap on each side of the dot, so that its sample does not read as a solid line.
static bool exerciseKeySamples() {
	bool ok = true;
	const auto gapsBesideDot = [](Qt::PenStyle style) {
		QImage image(static_cast<int>(kKeySampleWidth), static_cast<int>(kKeySampleHeight), QImage::Format_ARGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		DiagramKeyEntry entry{QStringLiteral("Planned"), DiagramKeyEntry::Shape::Line, QPen(Qt::black, 2, style), QBrush()};
		entry.marker = true;
		paintKeySample(painter, QRectF(QPointF(0, 0), image.size()), entry);
		painter.end();
		const int y = image.height() / 2, middle = image.width() / 2;
		const auto light = [&](int x) { return qGray(image.pixel(x, y)) > 200; };
		bool left = false, right = false;
		for (int x = 3; x < middle - 4; ++x) left = left || light(x);
		for (int x = middle + 5; x < image.width() - 3; ++x) right = right || light(x);
		return left && right;
	};
	ok &= expect(gapsBesideDot(Qt::DashLine), "a dashed line with a dot shows a gap on each side of the dot");
	ok &= expect(!gapsBesideDot(Qt::SolidLine), "a solid line with a dot has no gap");
	// The dot is filled with the brush of the row, as the points of a simulated line, or white without one, as the open
	// circles of a planned line.
	const auto dotMiddle = [](const QBrush& brush) {
		QImage image(static_cast<int>(kKeySampleWidth), static_cast<int>(kKeySampleHeight), QImage::Format_ARGB32);
		image.fill(Qt::white);
		QPainter painter(&image);
		DiagramKeyEntry entry{QStringLiteral("Line"), DiagramKeyEntry::Shape::Line, QPen(Qt::black, 2), brush};
		entry.marker = true;
		paintKeySample(painter, QRectF(QPointF(0, 0), image.size()), entry);
		painter.end();
		return qGray(image.pixel(image.width() / 2, image.height() / 2));
	};
	ok &= expect(dotMiddle(QBrush()) > 240 && dotMiddle(QBrush(Qt::black)) < 60, "the dot of a line sample has the fill of its row, white without one");
	return ok;
}

static bool exerciseLegend() {
	bool ok = true;

	DiagramWindow window("Key");
	window.show();
	window.setChart(makePlannedChart({"Train A (planned arrival)", "Train A (simulated arrival)"}, 0));
	ok &= expect(legendKinds(window) == QStringList({"Planned", "Simulated"}), "planned and simulated lines are named in the Key");
	window.setChart(makeNamedChart({"Train A", "Train B", "Train A"}));
	auto* key = window.findChild<DiagramKey*>("diagramKey");
	ok &= expect(key && key->property("kinds").toStringList().isEmpty() && key->parentWidget()->isHidden(),
		"a chart with only simulated lines has an empty Key, and the Key section hides");
	// The name of a series is the id of a train, which the user chose, so a word in it makes no line planned.
	QChart* namedPlanned = makeNamedChart({"planned-1", "Train B (PLANNED)"});
	window.setChart(namedPlanned);
	ok &= expect(legendKinds(window).isEmpty() && qobject_cast<QXYSeries*>(namedPlanned->series()[0])->pen().style() == Qt::SolidLine
			&& qobject_cast<QXYSeries*>(namedPlanned->series()[1])->pen().style() == Qt::SolidLine,
		"a train named planned-1 is drawn as a simulated train, solid and without a Planned row in the Key");
	QChart* plannedByProperty = makeNamedChart({"Train A", "Train A"});
	plannedByProperty->series()[0]->setProperty("planned", true);
	window.setChart(plannedByProperty);
	const auto* plannedLine = qobject_cast<QXYSeries*>(plannedByProperty->series()[0]);
	ok &= expect(legendKinds(window) == QStringList({"Planned", "Simulated"}) && plannedLine->pen().style() == Qt::DashLine,
		"a series whose planned property is set is planned without the word in its name, and drawn dashed");
	const QVector<DiagramKeyEntry> planned = key->entries();
	ok &= expect(planned.size() == 2 && planned[0].shape == DiagramKeyEntry::Shape::Line && planned[0].pen.style() == Qt::DashLine
			&& planned[0].pen.color() == window.palette().color(QPalette::WindowText) && planned[1].pen.style() == Qt::SolidLine,
		"a line sample takes the dash pattern of its series in the text colour");
	QChart* layered = makePlannedChart({"A", "A (planned reference)", "A again"}, 1);
	layered->series()[0]->setProperty("layer", "blocking time");
	layered->series()[2]->setProperty("layer", "simulated");
	window.setChart(layered);
	ok &= expect(legendKinds(window) == QStringList({"Blocking time", "Planned", "Simulated"}),
		"the layer of a series names its kind");
	ok &= expect(layered->margins().bottom() == 8 && !layered->findChild<QGraphicsObject*>("lineKindLegend"),
		"the chart keeps no legend and no room for one below the plot");

	// Blocking times of three kinds, a planned line with its points, a simulated line and a series the Key leaves out.
	auto* blocking = new QChart;
	addBlockingBox(blocking, "A", BlockingTimeKind::Ordinary, 1, 2, 10, 40);
	addBlockingBox(blocking, "B", BlockingTimeKind::Conflict, 2, 3, 20, 50);
	addBlockingBox(blocking, "B", BlockingTimeKind::CriticalBlock, 3, 4, 30, 60);
	auto* plannedRun = new QLineSeries;
	plannedRun->setName("A");
	plannedRun->setProperty("trainId", "A");
	plannedRun->setProperty("planned", true);
	plannedRun->setPointsVisible(true);
	plannedRun->append(1, 10);
	plannedRun->append(4, 70);
	blocking->addSeries(plannedRun);
	auto* run = new QLineSeries;
	run->setName("A");
	run->setProperty("trainId", "A");
	run->setProperty("layer", "simulated");
	run->append(1, 15);
	run->append(4, 75);
	blocking->addSeries(run);
	auto* hidden = new QLineSeries;
	hidden->setName("helper");
	hidden->setProperty("trainId", "A");
	hidden->setProperty("keyKind", QString());
	hidden->append(1, 1);
	hidden->append(2, 2);
	blocking->addSeries(hidden);
	blocking->createDefaultAxes();
	window.setChart(blocking);
	window.setTrainServices({});
	ok &= expect(legendKinds(window) == QStringList({"Blocking time", "Critical block", "Conflict", "Planned", "Simulated"}),
		qPrintable("the Key lists the kinds of the shown series in a fixed order, without a series whose keyKind is empty: "
			+ legendKinds(window).join(", ")));
	const QVector<DiagramKeyEntry> rows = key->entries();
	const auto rowNamedKind = [&rows](const QString& name) {
		const auto found = std::find_if(rows.begin(), rows.end(), [&name](const DiagramKeyEntry& entry) { return entry.name == name; });
		return found == rows.end() ? DiagramKeyEntry() : *found;
	};
	const DiagramKeyEntry conflict = rowNamedKind("Conflict");
	ok &= expect(conflict.shape == DiagramKeyEntry::Shape::Box && conflict.brush.style() == Qt::TexturePattern
			&& conflict.pen == QPen(QColor(0x9e, 0x1b, 0x1b), 1.4),
		"a blocking time is a box with the brush of its fill and the pen of its outline");
	ok &= expect(rowNamedKind("Planned").marker && !rowNamedKind("Simulated").marker,
		"a line that shows its points has a dot in its sample");
	trainListOf(window)->setTrainVisible("B", false);
	ok &= expect(legendKinds(window) == QStringList({"Blocking time", "Planned", "Simulated"}),
		"hiding a train takes the kinds that only it draws out of the Key");
	trainListOf(window)->setTrainVisible("A", false);
	ok &= expect(legendKinds(window).isEmpty(), "with every train hidden the Key is empty and hides");
	trainListOf(window)->setTrainVisible("A", true);
	trainListOf(window)->setTrainVisible("B", true);

	// The stops of an event line are a kind of their own, and a dwell is upright.
	auto* paths = new QChart;
	auto* events = new QLineSeries;
	events->setName("A (simulated arrival)");
	events->setProperty("trainId", "A");
	events->setProperty("keyMarker", "Stop");
	events->setPointsVisible(true);
	events->append(1, 10);
	events->append(3, 30);
	paths->addSeries(events);
	auto* dwell = new QLineSeries;
	dwell->setName("A (simulated dwell)");
	dwell->setProperty("trainId", "A");
	dwell->setProperty("keyKind", "Dwell");
	dwell->setProperty("keyUpright", true);
	dwell->append(3, 30);
	dwell->append(3, 40);
	paths->addSeries(dwell);
	auto* plannedEvents = new QLineSeries;
	plannedEvents->setName("A (planned arrival)");
	plannedEvents->setProperty("trainId", "A");
	plannedEvents->setProperty("planned", true);
	plannedEvents->setProperty("keyMarker", "Stop");
	plannedEvents->setPointsVisible(true);
	plannedEvents->append(1, 12);
	plannedEvents->append(3, 32);
	paths->addSeries(plannedEvents);
	paths->createDefaultAxes();
	window.setChart(paths);
	const QVector<DiagramKeyEntry> pathRows = key->entries();
	ok &= expect(legendKinds(window) == QStringList({"Planned", "Simulated", "Stop", "Dwell"}) && pathRows[2].shape == DiagramKeyEntry::Shape::Marker
			&& pathRows[3].shape == DiagramKeyEntry::Shape::Tick,
		qPrintable("the stops of an event line and a dwell are kinds of their own: " + legendKinds(window).join(", ")));
	// The chart draws planned points as open circles and other points as dots, and the Key draws them alike.
	const QImage open = plannedEvents->lightMarker();
	const QColor ring = open.isNull() ? QColor() : open.pixelColor(open.width() / 2, 1);
	const QColor middle = open.isNull() ? QColor() : open.pixelColor(open.width() / 2, open.height() / 2);
	const QColor penColour = plannedEvents->pen().color();
	const int ringOff = std::abs(ring.red() - penColour.red()) + std::abs(ring.green() - penColour.green()) + std::abs(ring.blue() - penColour.blue());
	ok &= expect(!open.isNull() && events->lightMarker().isNull() && middle == QColor(Qt::white) && ring.alpha() > 0 && ringOff < 40
			&& plannedEvents->markerSize() == 7,
		qPrintable("a planned line draws its points as open circles in its colour, a simulated line as dots: " + ring.name() + " "
			+ penColour.name() + " " + middle.name()));
	ok &= expect(pathRows[0].marker && pathRows[0].brush.style() == Qt::NoBrush && pathRows[2].brush.style() == Qt::SolidPattern
			&& pathRows[2].brush.color() == pathRows[2].pen.color(),
		"the Key draws Planned with an open circle and the stops of the simulated lines as dots");

	// A chart of rolling stock input lists its curves by name, in their own pens.
	DiagramWindow input("Key of rolling stock");
	input.setRollingStockSubject(QStringLiteral("Unit A"));
	input.show();
	QChart* curves = makeNamedChart({"Draisy, input", "Draisy, limit"});
	const QPen curvePen(QColor(20, 120, 60), 2.5);
	qobject_cast<QXYSeries*>(curves->series()[0])->setPen(curvePen);
	input.setChart(curves);
	const auto* inputKey = input.findChild<DiagramKey*>("diagramKey");
	ok &= expect(legendKinds(input) == QStringList({"Draisy, input", "Draisy, limit"}) && inputKey->entries()[0].pen == curvePen,
		"a chart of rolling stock lists each curve by its name and in its pen");

	// At the size of the window on a 1280 by 800 screen the Key lies in the panel, below the Trains.
	window.setMaximumSize(1152, 640);
	window.resize(1152, 640);
	window.setChart(makePlannedChart({"A", "A (planned reference)"}, 1));
	QApplication::processEvents();
	const QWidget* panel = window.findChild<QWidget*>("resultPanel");
	const QWidget* trains = trainListOf(window);
	ok &= expect(panel && key->isVisible() && panel->isAncestorOf(key) && key->height() == key->sizeHint().height() && key->height() > 0
			&& key->mapTo(&window, QPoint()).y() > trains->mapTo(&window, QPoint()).y() + trains->height(),
		"the Key lies in the panel below the Trains, with room for its rows");
	return ok;
}

// The trains of Assignment: 22 trains in the services IC1723 and S19825 of 6 trains and IC2025 and S9827 of 5.
static QVector<TrainKeyList::Train> assignmentTrains(const QColor& colour = QColor(30, 90, 200)) {
	QVector<TrainKeyList::Train> trains;
	const std::array<std::pair<const char*, int>, 4> services{{{"IC1723", 6}, {"S19825", 6}, {"IC2025", 5}, {"S9827", 5}}};
	for (const auto& [service, count] : services)
		for (int i = 1; i <= count; ++i) trains.append({QString("%1-%2").arg(service).arg(i), colour, service});
	return trains;
}

// The texts of column `column` of the rows at the top of a Trains list.
static QStringList topLevelTexts(const TrainKeyList& list, int column = 0) {
	QStringList texts;
	for (int i = 0; i < list.tree()->topLevelItemCount(); ++i) texts << list.tree()->topLevelItem(i)->text(column);
	return texts;
}

// More than six trains in six services or fewer are grouped by service, with the number of trains of each group; other
// lists are flat. The count names the shown trains, and setting the trains again keeps their check states.
static bool exerciseTrainKeyGroups() {
	bool ok = true;
	TrainKeyList list;
	int changes = 0;
	QObject::connect(&list, &TrainKeyList::visibilityChanged, [&changes] { ++changes; });
	list.setTrains(assignmentTrains());
	ok &= expect(list.isGrouped() && topLevelTexts(list) == QStringList({"IC1723", "S19825", "IC2025", "S9827"})
			&& topLevelTexts(list, 1) == QStringList({"6", "6", "5", "5"}) && changes == 0,
		qPrintable("the trains of Assignment form four service groups of 6, 6, 5 and 5 trains: " + topLevelTexts(list).join(", ")));
	ok &= expect(rowNamed(&list, "IC2025-3") && rowNamed(&list, "IC2025-3")->parent() == rowNamed(&list, "IC2025")
			&& rowNamed(&list, "IC2025")->checkState(0) == Qt::Checked,
		"each train lies under its service, and a group of shown trains is checked");
	ok &= expect(list.serviceOf("IC2025-3") == "IC2025" && list.serviceOf("T") == QString(), "the list knows the service of each train");
	ok &= expect(list.countLabel()->text() == "22 of 22", qPrintable("the count names the shown trains: " + list.countLabel()->text()));
	list.setTrainVisible("S9827-1", false);
	list.setTrainVisible("S9827-2", false);
	ok &= expect(list.countLabel()->text() == "20 of 22" && changes == 2, qPrintable("the count follows hidden trains: " + list.countLabel()->text()));
	list.setTrains(assignmentTrains(QColor(200, 40, 40)));
	ok &= expect(list.visibleTrainIds().size() == 20 && !list.isTrainVisible("S9827-1") && changes == 2,
		"setting the trains again keeps their check states and reports no change");
	const auto rows = list.shownRows();
	ok &= expect(rows.size() == 4 && rows[0].first == "IC1723" && rows[0].second == QColor(200, 40, 40),
		"an exported figure lists the services with a shown train, in the colour all their trains share");
	// A group has the colour that its shown trains share. While they differ, the figure lists them instead.
	QHash<QString, QColor> colours;
	for (const TrainKeyList::Train& train : assignmentTrains()) colours.insert(train.id, QColor(200, 40, 40));
	colours.insert("IC1723-1", QColor(20, 90, 200));
	list.setColours(colours);
	const auto mixed = list.shownRows();
	ok &= expect(mixed.size() == 9 && mixed[0] == qMakePair(QString("IC1723-1"), QColor(20, 90, 200))
			&& mixed[1] == qMakePair(QString("IC1723-2"), QColor(200, 40, 40)) && mixed[6].first == "S19825" && changes == 2,
		"an exported figure lists the shown trains of a group of two colours, each in its colour");
	list.setTrainVisible("IC1723-1", false);
	ok &= expect(list.shownRows().first().first == "IC1723" && list.shownRows().first().second == QColor(200, 40, 40),
		"a group has the colour that its shown trains share");
	list.setSelectedTrain("IC2025-3");
	list.setSelectedTrain(QString());
	ok &= expect(changes == 3, qPrintable(QString("selecting a train reports no change of the shown trains: %1 changes").arg(changes)));

	TrainKeyList six;
	QStringList ids;
	for (int i = 1; i <= 6; ++i) ids << QString("S-%1").arg(i);
	six.setTrains(trainsOf(ids, QStringList(6, "S")));
	ok &= expect(!six.isGrouped() && topLevelTexts(six) == ids && six.countLabel()->text() == "6 of 6",
		"six trains of one service are listed flat");
	TrainKeyList seven;
	seven.setTrains(trainsOf(ids + QStringList{"S-7"}, QStringList(7, "S")));
	ok &= expect(seven.isGrouped() && topLevelTexts(seven) == QStringList({"S"}), "seven trains of one service form a group");
	TrainKeyList scattered;
	QStringList many, services;
	for (int i = 1; i <= 8; ++i) {
		many << QString("T-%1").arg(i);
		services << QString("Service %1").arg(std::min(i, 7));
	}
	scattered.setTrains(trainsOf(many, services));
	ok &= expect(!scattered.isGrouped() && topLevelTexts(scattered) == many, "eight trains in seven services are listed flat");
	TrainKeyList unknown;
	unknown.setTrains(trainsOf({"A"}));
	ok &= expect(unknown.serviceOf("A") == "A", "a train without a service is a service of its own");
	return ok;
}

// The check box rectangle of a row of a Trains list, in viewport coordinates.
static QRect treeCheckBoxRect(const QTreeWidget* tree, const QTreeWidgetItem* item) {
	QStyleOptionViewItem option;
	option.initFrom(tree->viewport());
	option.widget = tree;
	option.rect = tree->visualItemRect(item);
	option.features = QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDisplay;
	option.checkState = item->checkState(0);
	option.text = item->text(0);
	if (!item->icon(0).isNull()) {
		option.features |= QStyleOptionViewItem::HasDecoration;
		option.icon = item->icon(0);
		option.decorationSize = tree->iconSize();
	}
	return tree->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, tree);
}

// A group row checks and unchecks all of its trains and is partly checked while some are shown, which the style sheet
// draws filled. Above 12 trains a search hides rows and leaves the check states alone, and All and None act on the rows
// it shows; a group then counts and acts on its trains that the search shows, and Escape clears the search. All and None
// show above 6 trains, the search above 12.
static bool exerciseTrainKeyChecks() {
	bool ok = true;
	TrainKeyList list;
	int changes = 0;
	QObject::connect(&list, &TrainKeyList::visibilityChanged, [&changes] { ++changes; });
	list.setTrains(assignmentTrains(QColor()));
	list.resize(260, 400);
	list.show();
	QApplication::processEvents();
	QTreeWidgetItem* group = rowNamed(&list, "S19825");
	rowNamed(&list, "S19825-2")->setCheckState(0, Qt::Unchecked);
	ok &= expect(group->checkState(0) == Qt::PartiallyChecked && list.countLabel()->text() == "21 of 22" && changes == 1,
		"hiding one train of a group makes the group partly checked");
	group->setCheckState(0, Qt::Unchecked);
	ok &= expect(list.countLabel()->text() == "16 of 22" && changes == 2 && rowNamed(&list, "S19825-1")->checkState(0) == Qt::Unchecked,
		"unchecking a group hides its six trains at once");
	rowNamed(&list, "S19825-4")->setCheckState(0, Qt::Checked);
	// A click on a partly checked box checks it.
	QTreeWidget* tree = list.tree();
	QTreeWidgetItem* partial = rowNamed(&list, "S19825");
	const QPoint box = treeCheckBoxRect(tree, partial).center();
	QMouseEvent press(QEvent::MouseButtonPress, box, tree->viewport()->mapToGlobal(box), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent release(QEvent::MouseButtonRelease, box, tree->viewport()->mapToGlobal(box), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(tree->viewport(), &press);
	QApplication::sendEvent(tree->viewport(), &release);
	ok &= expect(partial->checkState(0) == Qt::Checked && list.countLabel()->text() == "22 of 22",
		qPrintable("a click on a partly checked group shows all of its trains: " + list.countLabel()->text()));

	// The style sheet draws a partly checked box filled, unlike an empty one.
	rowNamed(&list, "S19825-3")->setCheckState(0, Qt::Unchecked);
	rowNamed(&list, "S9827")->setCheckState(0, Qt::Unchecked);
	QEvent leave(QEvent::Leave);
	QApplication::sendEvent(tree->viewport(), &leave);
	QApplication::processEvents();
	const QImage drawn = tree->viewport()->grab().toImage();
	const auto filled = [&](const QString& name) {
		const QRect rect = treeCheckBoxRect(tree, rowNamed(&list, name)).adjusted(3, 3, -3, -3);
		int dark = 0;
		for (int y = rect.top(); y <= rect.bottom(); ++y)
			for (int x = rect.left(); x <= rect.right(); ++x) dark += qGray(drawn.pixel(x, y)) < 110 ? 1 : 0;
		return dark;
	};
	ok &= expect(filled("S19825") > 20 && filled("IC1723") > 20 && filled("S9827") == 0,
		qPrintable(QString("a partly checked group draws a filled box: %1, %2 and %3 dark pixels")
				.arg(filled("S19825"))
				.arg(filled("IC1723"))
				.arg(filled("S9827"))));
	rowNamed(&list, "S19825-3")->setCheckState(0, Qt::Checked);
	rowNamed(&list, "S9827")->setCheckState(0, Qt::Checked);

	// The search hides rows only, and the groups it opens close when it is cleared.
	rowNamed(&list, "S9827")->setExpanded(true);
	changes = 0;
	ok &= expect(list.searchField()->isVisible(), "a list of more than 12 trains has a search field");
	list.searchField()->setText("IC2025");
	ok &= expect(shownRowTexts(&list) == QStringList({"IC2025", "IC2025-1", "IC2025-2", "IC2025-3", "IC2025-4", "IC2025-5"})
			&& list.visibleTrainIds().size() == 22 && changes == 0,
		qPrintable("a search shows the rows it finds and keeps the check states: " + shownRowTexts(&list).join(", ")));
	list.findChild<QPushButton*>("trainKeyNone")->click();
	ok &= expect(list.countLabel()->text() == "17 of 22" && changes == 1 && list.isTrainVisible("IC1723-1"),
		"None hides the trains of the rows the search shows, and only those");
	list.setTrainVisible("IC1723-1", false);
	list.searchField()->setText("IC1723-3");
	ok &= expect(rowNamed(&list, "IC1723")->checkState(0) == Qt::Checked, "a partly shown group counts its trains that the search shows");
	rowNamed(&list, "IC1723")->setCheckState(0, Qt::Unchecked);
	ok &= expect(!list.isTrainVisible("IC1723-3") && list.isTrainVisible("IC1723-2")
			&& rowNamed(&list, "IC1723")->checkState(0) == Qt::Unchecked && list.shownRows().first().first == "IC1723",
		"a group acts on its trains that the search shows, and its service keeps its place in an exported figure");
	pressKey(list.searchField(), Qt::Key_Escape);
	ok &= expect(list.searchField()->text().isEmpty() && shownRowTexts(&list).size() >= 4 && list.countLabel()->text() == "15 of 22"
			&& rowNamed(&list, "IC1723")->checkState(0) == Qt::PartiallyChecked,
		"Escape clears the search, which shows every row and keeps the check states");
	ok &= expect(!rowNamed(&list, "IC2025")->isExpanded() && !rowNamed(&list, "IC1723")->isExpanded() && rowNamed(&list, "S9827")->isExpanded(),
		"clearing the search closes the groups it opened and leaves a group opened by hand open");
	list.findChild<QPushButton*>("trainKeyAll")->click();
	ok &= expect(list.countLabel()->text() == "22 of 22", "All shows every train");

	// All and None show above 6 trains, the search above 12.
	for (const int count : {6, 7, 12, 13}) {
		QStringList ids;
		for (int i = 1; i <= count; ++i) ids << QString("T-%1").arg(i);
		TrainKeyList sized;
		sized.setTrains(trainsOf(ids));
		sized.show();
		QApplication::processEvents();
		const bool allNone = sized.findChild<QPushButton*>("trainKeyAll")->isVisible() && sized.findChild<QPushButton*>("trainKeyNone")->isVisible();
		ok &= expect(allNone == (count > 6) && sized.searchField()->isVisible() == (count > 12),
			qPrintable(QString("a list of %1 trains has All and None %2 and a search field %3")
					.arg(count)
					.arg(allNone ? "shown" : "hidden")
					.arg(sized.searchField()->isVisible() ? "shown" : "hidden")));
		if (count == 7) {
			sized.findChild<QPushButton*>("trainKeyNone")->click();
			ok &= expect(sized.visibleTrainIds().isEmpty(), "None hides every train of a list without a search");
		}
	}
	return ok;
}

// The selected train comes first and is highlighted, then the shown trains, then the hidden ones, each in the order the
// window gave them. While the pointer is over the rows they keep their places, and they move once it leaves.
static bool exerciseTrainKeyOrder() {
	bool ok = true;
	TrainKeyList list;
	list.setTrains(trainsOf({"A", "B", "C", "D"}));
	list.show();
	QApplication::processEvents();
	list.setTrainVisible("A", false);
	ok &= expect(shownRowTexts(&list) == QStringList({"B", "C", "D", "A"}), qPrintable("a hidden train moves below the shown ones: " + shownRowTexts(&list).join(", ")));
	QWidget* rows = list.tree()->viewport();
	QEvent enter(QEvent::Enter);
	QApplication::sendEvent(rows, &enter);
	list.setTrainVisible("B", false);
	ok &= expect(shownRowTexts(&list) == QStringList({"B", "C", "D", "A"}), "the rows keep their places while the pointer is over them");
	QEvent leave(QEvent::Leave);
	QApplication::sendEvent(rows, &leave);
	ok &= expect(shownRowTexts(&list) == QStringList({"C", "D", "A", "B"}), qPrintable("the rows move once the pointer leaves: " + shownRowTexts(&list).join(", ")));
	list.setSelectedTrain("D");
	ok &= expect(shownRowTexts(&list) == QStringList({"D", "C", "A", "B"}) && rowNamed(&list, "D")->background(0).color() == QColor(0xd8, 0xe1, 0xe5)
			&& rowNamed(&list, "C")->background(0).style() == Qt::NoBrush,
		"the selected train comes first and is highlighted");
	// The style sheet draws the rows, and the band still shows across the row.
	list.resize(260, list.sizeHint().height());
	QApplication::processEvents();
	// Without the focus or the pointer, which the window can get once it shows.
	list.tree()->clearFocus();
	QApplication::sendEvent(rows, &leave);
	const QImage band = list.tree()->viewport()->grab().toImage();
	const QRect selectedRow = list.tree()->visualItemRect(rowNamed(&list, "D"));
	const QColor bandColour = band.pixelColor(qRound((list.tree()->viewport()->width() - 4) * band.devicePixelRatio()),
		qRound(selectedRow.center().y() * band.devicePixelRatio()));
	ok &= expect(bandColour == QColor(0xd8, 0xe1, 0xe5), qPrintable("the band of the selected train is drawn: " + bandColour.name()));
	list.setSelectedTrain(QString());
	ok &= expect(shownRowTexts(&list) == QStringList({"C", "D", "A", "B"}) && rowNamed(&list, "D")->background(0).style() == Qt::NoBrush,
		"without a selection the highlight goes and the order is that of the shown trains");
	// A click on a row gives the list the focus, which it keeps once the pointer has left; the rows move all the same.
	list.activateWindow();
	list.tree()->setFocus();
	waitFor([&list] { return list.tree()->hasFocus(); });
	QApplication::sendEvent(rows, &enter);
	list.setTrainVisible("C", false);
	QApplication::sendEvent(rows, &leave);
	ok &= expect(list.tree()->hasFocus() && shownRowTexts(&list) == QStringList({"D", "A", "B", "C"}),
		qPrintable("the rows move once the pointer leaves, also when the list keeps the focus: " + shownRowTexts(&list).join(", ")));

	// Seven trains show All and None without the search field, at their own width.
	TrainKeyList seven;
	seven.setTrains(trainsOf({"A", "B", "C", "D", "E", "F", "G"}));
	seven.resize(260, 400);
	seven.show();
	QApplication::processEvents();
	const auto* all = seven.findChild<QPushButton*>("trainKeyAll");
	const auto* none = seven.findChild<QPushButton*>("trainKeyNone");
	ok &= expect(all && none && all->isVisible() && !seven.searchField()->isVisible() && all->width() == all->sizeHint().width()
			&& none->width() == none->sizeHint().width() && all->x() < none->x(),
		qPrintable(QString("All and None keep their own width without the search field: %1 and %2 px")
				.arg(all ? all->width() : 0)
				.arg(none ? none->width() : 0)));

	TrainKeyList grouped;
	grouped.setTrains(assignmentTrains());
	grouped.show();
	QApplication::processEvents();
	rowNamed(&grouped, "IC1723")->setCheckState(0, Qt::Unchecked);
	ok &= expect(topLevelTexts(grouped) == QStringList({"S19825", "IC2025", "S9827", "IC1723"}),
		qPrintable("a group of hidden trains moves below the others: " + topLevelTexts(grouped).join(", ")));
	grouped.setSelectedTrain("S9827-4");
	ok &= expect(topLevelTexts(grouped).first() == "S9827" && rowNamed(&grouped, "S9827")->isExpanded()
			&& rowNamed(&grouped, "S9827")->child(0)->text(0) == "S9827-4",
		"the group of the selected train comes first and opens, with the train first in it");
	return ok;
}

// A click on a line selects its train: Selected shows the lines of its tooltip, the line is emphasised and its row
// highlighted. A click inside a blocking time selects that box: an outline marks it, Selected shows its lines and the
// chart keeps its look. Clear, Escape, hiding the train and a new chart end the selection, and the exported figure leaves
// the outline out.
static bool exerciseSelection() {
	bool ok = true;
	DiagramWindow window("Selection");
	auto* chart = new QChart;
	const QString longId = QString(70, QLatin1Char('X'));
	addBlockingBox(chart, "A", BlockingTimeKind::Ordinary, 2, 4, 20, 60);
	addBlockingBox(chart, longId, BlockingTimeKind::Conflict, 5, 7, 30, 70);
	auto* run = new QLineSeries;
	run->setName("B run");
	run->setProperty("trainId", "B");
	run->append(9, 5);
	run->append(9, 95);
	chart->addSeries(run);
	chart->createDefaultAxes();
	for (auto* axis : chart->axes()) qobject_cast<QValueAxis*>(axis)->setRange(0, 100);
	chart->axes(Qt::Horizontal).first()->setRange(0, 10);
	window.setChart(chart);
	window.setMaximumSize(1200, 750);
	window.resize(1200, 750);
	window.show();
	QApplication::processEvents();
	auto* view = window.findChild<QChartView*>();
	auto* title = window.findChild<QLabel*>("diagramSelectedTitle");
	auto* lines = window.findChild<QLabel*>("diagramPinLabel");
	auto* clear = window.findChild<QPushButton*>("diagramClearSelection");
	TrainKeyList* list = trainListOf(window);
	if (!expect(view && title && lines && clear && list, "the window has its Selected labels and Clear")) return false;
	QString selected;
	QObject::connect(&window, &DiagramWindow::trainSelected, [&selected](const QString& id) { selected = id; });
	const auto click = [&](const QPointF& value, QAbstractSeries* series) {
		const QPoint point = view->mapFromScene(chart->mapToScene(chart->mapToPosition(value, series)));
		QMouseEvent press(QEvent::MouseButtonPress, point, view->viewport()->mapToGlobal(point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QMouseEvent release(QEvent::MouseButtonRelease, point, view->viewport()->mapToGlobal(point), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(view->viewport(), &press);
		QApplication::sendEvent(view->viewport(), &release);
		QApplication::processEvents();
	};
	auto* outlineA = qobject_cast<QXYSeries*>(chart->series()[1]);
	const QPen outlinePen = outlineA->pen();
	const QImage before = window.exportedFigure();

	click(QPointF(3, 40), outlineA);
	auto* outline = chart->findChild<QGraphicsObject*>("diagramSelectionOutline");
	view->grab();
	const QRectF box(chart->mapToPosition(QPointF(2, 60), outlineA), chart->mapToPosition(QPointF(4, 20), outlineA));
	const QRectF drawn = outline ? outline->property("outline").toRectF() : QRectF();
	ok &= expect(outline && outline->isVisible() && drawn.contains(box.normalized()) && std::abs(drawn.width() - box.normalized().width() - 3) < 0.5,
		"a click inside a blocking time draws an outline just around its box");
	ok &= expect(title->text() == "A" && lines->text().split('\n').contains("Block: Blocking time") && lines->text().split('\n').contains("Blocked 20 to 60 s")
			&& selected == "A" && rowNamed(list, "A")->background(0).style() != Qt::NoBrush,
		qPrintable("Selected shows the lines of the blocking time, one fact per line, and its train is selected: " + lines->text()));
	ok &= expect(outlineA->pen() == outlinePen && run->pen().color().alpha() == 255, "a selected blocking time leaves the look of the chart as it is");
	ok &= expect(window.exportedFigure() == before, "the exported figure leaves the outline out");

	pressKey(view, Qt::Key_Escape);
	ok &= expect(title->isHidden() || !title->isVisible(), "Escape ends the selection");
	ok &= expect(window.isVisible() && !chart->findChild<QGraphicsObject*>("diagramSelectionOutline"), "Escape with a selection keeps the window open and removes the outline");
	pressKey(view, Qt::Key_Escape);
	ok &= expect(!window.isVisible(), "Escape without a selection closes the window");
	window.show();
	QApplication::processEvents();

	click(QPointF(9, 50), run);
	ok &= expect(title->isVisible() && title->text() == "B run" && run->pen().widthF() > outlinePen.widthF() && isMuted(outlineA->pen(), outlinePen.color(), chart->backgroundBrush().color())
			&& !chart->findChild<QGraphicsObject*>("diagramSelectionOutline") && rowNamed(list, "B")->background(0).style() != Qt::NoBrush,
		"a click on a line selects its train, emphasises it and highlights its row");
	clear->click();
	ok &= expect(!title->isVisible() && outlineA->pen() == outlinePen && rowNamed(list, "B")->background(0).style() == Qt::NoBrush, "Clear ends the selection");

	click(QPointF(9, 50), run);
	list->setTrainVisible("B", false);
	ok &= expect(!title->isVisible() && outlineA->pen() == outlinePen, "hiding the selected train ends the selection");
	list->setTrainVisible("B", true);

	// A long id wraps in Selected and does not widen the panel.
	auto* outlineLong = qobject_cast<QXYSeries*>(chart->series()[3]);
	click(QPointF(6, 50), outlineLong);
	for (int round = 0; round < 4; ++round) QApplication::processEvents();
	ok &= expect(title->text().remove(QChar(0x200B)) == longId && title->height() >= 2 * title->fontMetrics().height()
			&& window.findChild<QWidget*>("resultPanel")->width() == 260,
		qPrintable(QString("a long id wraps in Selected: %1 px high").arg(title->height())));
	window.setChart(makeNamedChart({"A", "B"}));
	ok &= expect(!title->isVisible(), "a new chart ends the selection");
	return ok;
}

// The exported figure has a column at the right of the chart with the shown trains, each with its line sample and
// without a check box, and the Key.
static bool exerciseExportColumn() {
	bool ok = true;
	DiagramWindow window("Export column");
	auto* chart = makePlannedChart({"A", "B", "C", "A (planned)"}, 3);
	for (int i = 0; i < 4; ++i) chart->series()[i]->setProperty("trainId", QString(QChar('A' + (i % 3))));
	window.setChart(chart);
	window.setTrainColors({{"A", QColor(200, 0, 0)}, {"B", QColor(0, 160, 0)}, {"C", QColor(0, 0, 220)}});
	window.setMaximumSize(1200, 750);
	window.resize(1200, 750);
	window.show();
	QApplication::processEvents();
	const QImage plain = window.figureImage();
	const QImage figure = window.exportedFigure();
	const int column = (figure.width() - plain.width()) / 2;
	ok &= expect(figure.height() == plain.height() && column >= 140 && column <= 280,
		qPrintable(QString("the exported figure is wider than the chart by its column: %1 px").arg(column)));
	const auto colourInColumn = [](const QImage& image, int from, const QColor& colour) {
		for (int y = 0; y < image.height(); ++y)
			for (int x = from; x < image.width(); ++x) {
				const QColor pixel = image.pixelColor(x, y);
				if (std::abs(pixel.red() - colour.red()) + std::abs(pixel.green() - colour.green()) + std::abs(pixel.blue() - colour.blue()) < 30) return true;
			}
		return false;
	};
	const int from = plain.width();
	ok &= expect(colourInColumn(figure, from, QColor(200, 0, 0)) && colourInColumn(figure, from, QColor(0, 160, 0))
			&& !colourInColumn(figure, from, QColor(0x31, 0x5a, 0x70)),
		"the column draws the line samples of the trains and no check box");
	trainListOf(window)->setTrainVisible("B", false);
	const QImage shown = window.exportedFigure();
	ok &= expect(!colourInColumn(shown, from, QColor(0, 160, 0)) && colourInColumn(shown, from, QColor(200, 0, 0)), "the column lists the shown trains only");
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
	bool ok = expect(clockLabels(7 * 3600 + 10 * 60 + 40).mid(0, 3) == QStringList({"07:10:40", "07:30", "08:00"}),
		"a round tick whose label has room stays next to the run start");
	ok &= expect(clockLabels(7 * 3600 + 29 * 60 + 50).mid(0, 2) == QStringList({"07:29:50", "08:00"}),
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
	ok &= expect(verticalTitles() == QStringList({"Time (hh:mm)"}), "the clock axis is the only vertical axis that the chart draws");
	window.setTimeAxisY(false);
	ok &= expect(verticalTitles() == QStringList({"Elapsed simulation time (s)"}), "numeric labels again show the numeric axis alone");
	window.setTimeAxisY(true, 8 * 3600);
	ok &= expect(verticalTitles() == QStringList({"Time (hh:mm)"}), "a second clock axis is again the only vertical axis");
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
	// A window whose figure has a name of its own, as the delays have, suggests that name.
	window->setFigureFileName("delays.png");
	suggested.clear();
	QTimer::singleShot(0, window, [&suggested] {
		for (QWidget* widget : QApplication::topLevelWidgets())
			if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
				suggested = QFileInfo(dialog->selectedFiles().value(0)).fileName();
				dialog->reject();
			}
	});
	buttonNamed(*window, "PNG...")->click();
	ok &= expect(suggested == "delays.png", qPrintable("PNG... suggests the name of the figure: " + suggested));
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
	const QImage figure = window->exportedFigure();
	ok &= expect(!saved.isNull() && saved.size() == figure.size() && saved.dotsPerMeterX() == figure.dotsPerMeterX(),
		"PNG... saves the figure of the window");
	return ok;
}

// What the save dialog of an export showed, and the texts of the warnings after it.
struct ExportDialog {
	QString title;
	QStringList filters;
	QString suggested;
	QStringList warnings;
};

// Clicks `button`, answers the save dialog with `path`, or cancels it for an empty path, and closes the messages that
// follow. A dialog that stays open after the answer is cancelled.
static ExportDialog answerExport(QPushButton* button, const QString& path) {
	ExportDialog shown;
	bool answered = false;
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, [&shown, &answered, &path] {
		if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
			if (box->icon() == QMessageBox::Warning)
				shown.warnings << box->text();
			box->accept();
			return;
		}
		for (QWidget* widget : QApplication::topLevelWidgets())
			if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
				if (answered) {
					dialog->reject();
					continue;
				}
				answered = true;
				shown.title = dialog->windowTitle();
				shown.filters = dialog->nameFilters();
				shown.suggested = QFileInfo(dialog->selectedFiles().value(0)).fileName();
				auto* name = dialog->findChild<QLineEdit*>("fileNameEdit");
				if (path.isEmpty() || !name) {
					dialog->reject();
					continue;
				}
				// The whole path in the name field, which also takes a folder that does not exist.
				name->setText(path);
				// Through QDialog, as QFileDialog::accept is protected.
				static_cast<QDialog*>(dialog)->accept();
			}
	});
	timer.start(0);
	if (button) button->click();
	return shown;
}

// CSV... and PNG... ask for a file of their kind, export.csv and diagram.png for a window without names of its own. A
// write that fails says so with the path as the person reads it, here in the Windows form.
static bool exerciseExportDialogs() {
	QTemporaryDir directory;
	DiagramWindow window("Export dialogs");
	window.setChart(buttonTestChart());
	window.setCsvProvider([](const QStringList&) { return std::string("Train\nA\n"); }, QString());
	window.show();
	QPushButton* csvButton = buttonNamed(window, "CSV...");
	QPushButton* pngButton = buttonNamed(window, "PNG...");
	bool ok = expect(directory.isValid() && csvButton && pngButton, "the window has both exports");
	if (!ok) return false;
	const QChar separator = display_path::separator();
	display_path::separator() = QLatin1Char('\\');
	const QString csvPath = QDir(directory.path()).filePath("missing/diagram.csv");
	const ExportDialog csv = answerExport(csvButton, csvPath);
	ok &= expect(csv.title == "Export Data" && csv.filters == QStringList("CSV File (*.csv)") && csv.suggested == "export.csv",
		qPrintable("CSV... asks for a CSV file: " + csv.title + ", " + csv.filters.join(", ") + ", " + csv.suggested));
	ok &= expect(csv.warnings == QStringList("Could not save the data to:\n" + display_path::format(csvPath)),
		qPrintable("a CSV file that cannot be written is named as the person reads it: " + csv.warnings.join(" | ")));
	const QString pngPath = QDir(directory.path()).filePath("missing/diagram.png");
	const ExportDialog png = answerExport(pngButton, pngPath);
	ok &= expect(png.title == "Export Diagram" && png.filters == QStringList("PNG Image (*.png)") && png.suggested == "diagram.png",
		qPrintable("PNG... asks for a PNG image: " + png.title + ", " + png.filters.join(", ") + ", " + png.suggested));
	ok &= expect(png.warnings == QStringList("Could not save the image to:\n" + display_path::format(pngPath)),
		qPrintable("an image that cannot be written is named as the person reads it: " + png.warnings.join(" | ")));
	display_path::separator() = separator;
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
	// The names of the widgets that Tab gives the focus to, from the first header button on.
	const auto tabOrder = [](QWidget* first, int steps) {
		first->setFocus(Qt::TabFocusReason);
		QStringList order;
		for (int step = 0; step < steps && QApplication::focusWidget(); ++step) {
			QWidget* focused = QApplication::focusWidget();
			order << (qobject_cast<QChartView*>(focused) ? QStringLiteral("chart") : focused->objectName());
			pressKey(focused, Qt::Key_Tab);
		}
		return order;
	};
	const QStringList order = tabOrder(zoomOut, 8);
	const QStringList expectedOrder{"diagramZoomOut", "diagramZoomIn", "diagramFit", "diagramPanelToggle", "chart", "trainKeyTree",
		"diagramExportPng", "diagramExportCsv"};
	ok &= expect(order == expectedOrder, qPrintable("Tab goes through the header, the chart, the Trains list and the exports: " + order.join(", ")));

	// With the search of a long list and a selection, Tab goes through the search, All, None and the rows, then Clear.
	DiagramWindow many("Many trains");
	auto* manyChart = new QChart;
	for (int i = 0; i < 13; ++i) {
		auto* line = new QLineSeries;
		line->setName(QString("T%1").arg(i));
		line->append(i, 0);
		line->append(i + 1, 10);
		manyChart->addSeries(line);
	}
	manyChart->createDefaultAxes();
	many.setChart(manyChart);
	many.setMaximumSize(1200, 750);
	many.resize(1200, 750);
	many.show();
	QApplication::processEvents();
	auto* manyView = many.findChild<QChartView*>();
	const QPoint sample = manyView->mapFromScene(manyChart->mapToScene(manyChart->mapToPosition(QPointF(0.5, 5), manyChart->series().first())));
	QMouseEvent selectPress(QEvent::MouseButtonPress, sample, manyView->viewport()->mapToGlobal(sample), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent selectRelease(QEvent::MouseButtonRelease, sample, manyView->viewport()->mapToGlobal(sample), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(manyView->viewport(), &selectPress);
	QApplication::sendEvent(manyView->viewport(), &selectRelease);
	QApplication::processEvents();
	const QStringList longOrder = tabOrder(many.findChild<QToolButton*>("diagramZoomOut"), 11);
	const QStringList expectedLongOrder{"diagramZoomOut", "diagramZoomIn", "diagramFit", "diagramPanelToggle", "chart", "trainKeySearch",
		"trainKeyAll", "trainKeyNone", "trainKeyTree", "diagramClearSelection", "diagramExportPng"};
	ok &= expect(longOrder == expectedLongOrder,
		qPrintable("Tab goes through the search, All, None, the rows and Clear of a long list with a selection: " + longOrder.join(", ")));

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
			&& window.findChild<QLabel*>("diagramSelectedTitle")->text() == "Train B",
		"a selected train adds the section Selected with its name between Trains and Export");
	QMetaObject::invokeMethod(&window, "clearPin");
	QApplication::processEvents();
	ok &= expect(shownHeadings(window) == QStringList({"Trains", "Export"}) && shownRules(window) == 1, "Clear removes the section Selected");
	DiagramWindow input("Rolling stock panel");
	input.setRollingStockSubject(QStringLiteral("Unit A"));
	input.setChart(buttonTestChart());
	input.show();
	QApplication::processEvents();
	ok &= expect(shownHeadings(input) == QStringList({"Key", "Export"}) && shownRules(input) == 1,
		"a rolling stock window shows only its Key and Export");

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
	ok &= expect(handle && panel && panel->isVisible() && panel->width() == 200 && splitter->sizes().value(1) == 200,
		qPrintable(QString("a drag of the splitter to the edge stops at the narrow width of the panel: %1 px").arg(panel ? panel->width() : 0)));
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
	const QRegularExpression numeric(QStringLiteral("^(\\d+\\.\\d) km/h at (\\d+\\.\\d{2}) km$"));
	const auto near = [&numeric](const QString& text, double km, double speed) {
		const QRegularExpressionMatch match = numeric.match(text);
		return match.hasMatch() && std::abs(match.captured(2).toDouble() - km) < 0.02 && std::abs(match.captured(1).toDouble() - speed) < 0.3;
	};
	// The pointer that comes into the plot fills the readout before it moves there.
	const QPoint entry = view->mapFromScene(chart->mapToScene(chart->mapToPosition(QPointF(3, 40), line)));
	QEnterEvent enter(entry, entry, view->viewport()->mapToGlobal(entry));
	QApplication::sendEvent(view->viewport(), &enter);
	const QString entered = readout->text();
	const QString value = pointAt(QPointF(3, 40));
	bool ok = expect(entered == value, qPrintable("the readout fills when the pointer comes into the plot: " + entered));
	ok &= expect(near(value, 3, 40) && x->max() - x->min() < chart->plotArea().width() / 10
			&& y->max() - y->min() > chart->plotArea().height() / 10,
		qPrintable("the readout gives the speed at the distance to the precision of a pixel, with the units of the axes: " + value));
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
	// A dragged rectangle zooms into it once the button is released, and the readout then reads the new axes at the
	// pointer.
	QMetaObject::invokeMethod(&window, "resetZoom");
	QApplication::processEvents();
	const QPoint dragFrom = view->mapFromScene(chart->mapToScene(chart->mapToPosition(QPointF(2.2, 21), line)));
	const QPoint dragTo = view->mapFromScene(chart->mapToScene(chart->mapToPosition(QPointF(6.7, 73), line)));
	QMouseEvent dragPress(QEvent::MouseButtonPress, dragFrom, view->viewport()->mapToGlobal(dragFrom), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent dragMove(QEvent::MouseMove, dragTo, view->viewport()->mapToGlobal(dragTo), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent dragRelease(QEvent::MouseButtonRelease, dragTo, view->viewport()->mapToGlobal(dragTo), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(view->viewport(), &dragPress);
	QApplication::sendEvent(view->viewport(), &dragMove);
	const QString beforeRelease = readout->text();
	QApplication::sendEvent(view->viewport(), &dragRelease);
	QApplication::processEvents();
	const QString afterRelease = readout->text();
	QMouseEvent stillMove(QEvent::MouseMove, dragTo, view->viewport()->mapToGlobal(dragTo), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(view->viewport(), &stillMove);
	ok &= expect(x->max() - x->min() < 9 && !beforeRelease.isEmpty() && afterRelease == readout->text() && afterRelease != beforeRelease,
		qPrintable("the readout follows the zoom of a dragged rectangle: " + beforeRelease + " before, " + afterRelease + " after, "
			+ readout->text() + " at the pointer"));
	// A new size of the chart, as when the panel hides, moves the plot from under the pointer.
	QMetaObject::invokeMethod(&window, "resetZoom");
	moveTo(view, QPointF(3, 30), line);
	auto* tooltip = window.findChild<QLabel*>("diagramTooltip");
	ok &= expect(!readout->text().isEmpty() && tooltip->isVisible(), "the pointer on the line shows the tooltip and the readout");
	window.findChild<QToolButton*>("diagramPanelToggle")->click();
	QApplication::processEvents();
	ok &= expect(readout->text().isEmpty() && !tooltip->isVisible(), "hiding the panel clears the tooltip and the readout");
	window.findChild<QToolButton*>("diagramPanelToggle")->click();
	QApplication::processEvents();
	moveTo(view, QPointF(3, 30), line);
	window.resize(1100, 700);
	QApplication::processEvents();
	ok &= expect(readout->text().isEmpty() && !tooltip->isVisible(), "a resize of the window clears the tooltip and the readout");

	// With the clock along the bottom the readout gives the value at the clock time.
	window.setTimeAxisY(false);
	window.setTimeAxisX(true, 8 * 3600);
	QApplication::processEvents();
	const QString speedAtTime = pointAt(QPointF(3, 40));
	ok &= expect(QRegularExpression(QStringLiteral("^\\d+\\.\\d+ km/h at 08:00:0[234]$")).match(speedAtTime).hasMatch(),
		qPrintable("with the clock along the bottom the readout gives the speed at the clock time: " + speedAtTime));

	// A distance axis with "readoutStation" gives the station at the nearest position that a shown series has a sample at,
	// as the Delays chart has it: Charlie at 6 km is named at 5.1 km, not Foxtrot at 5 km, where no train has a delay.
	DiagramWindow delays("Delays");
	auto* delayChart = new QChart;
	auto* delay = new QLineSeries;
	delay->setName("Train A");
	delay->setPointsVisible(true);
	const QStringList names = {"Alpha", "Bravo", "Charlie", "Delta", "Echo"};
	std::vector<StationTick> stationTicks;
	for (int stop = 1; stop <= 5; ++stop) {
		delay->append(stop * 2.0, stop * 0.5);
		stationTicks.push_back({stop * 2.0, {QString("%1 (%2 km)").arg(names[stop - 1]).arg(stop * 2.0, 0, 'f', 2), names[stop - 1]}});
	}
	stationTicks.push_back({5.0, {"Foxtrot (5.00 km)", "Foxtrot"}});
	delayChart->addSeries(delay);
	delayChart->createDefaultAxes();
	auto* distance = qobject_cast<QValueAxis*>(delayChart->axes(Qt::Horizontal).first());
	distance->setTitleText("Distance (km)");
	distance->setProperty("readoutStation", true);
	delayChart->axes(Qt::Vertical).first()->setTitleText("Arrival delay (min)");
	auto* stationAxis = new QCategoryAxis;
	stationAxis->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
	stationAxis->setRange(distance->min(), distance->max());
	delayChart->addAxis(stationAxis, Qt::AlignTop);
	delay->attachAxis(stationAxis);
	delays.setChart(delayChart);
	delays.setStationAxis(stationAxis, stationTicks);
	delays.setMaximumSize(1200, 750);
	delays.resize(1200, 750);
	delays.show();
	QApplication::processEvents();
	auto* delayView = delays.findChild<QChartView*>();
	auto* delayReadout = delays.findChild<QLabel*>("resultReadout");
	moveTo(delayView, QPointF(5.1, 1.5), delay);
	const QString atStation = delayReadout->text();
	ok &= expect(QRegularExpression(QStringLiteral("^1\\.[45]\\d* min at Charlie$")).match(atStation).hasMatch(),
		qPrintable("the readout of the delays names the station of the sample nearest the pointer: " + atStation));
	clickChart(delayView, QPointF(6, 1.5), delay);
	QApplication::processEvents();
	const QStringList stationLines = delays.findChild<QLabel*>("diagramPinLabel")->text().split('\n');
	ok &= expect(stationLines.contains("Distance (km): 6.00"), qPrintable("Selected gives the distance of the call: " + stationLines.join(" / ")));
	trainListOf(delays)->setTrainVisible(delay->name(), false);
	moveTo(delayView, QPointF(5.1, 1.5), delay);
	ok &= expect(delayReadout->text().endsWith(" min"), qPrintable("without a shown call the readout gives the delay alone: " + delayReadout->text()));
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
	// The warning has the look of a warning of the table windows, in the window and in the figure.
	int warningFill = 0;
	for (int row = top * 2; row < warnedTop * 2; ++row)
		for (int x = 0; x < warned.width(); ++x)
			if (warned.pixelColor(x, row) == QColor(0xf5, 0xea, 0xdc)) ++warningFill;
	ok &= expect(window.findChild<QLabel*>("diagramWarning")->property("dialogStatus").toString() == "warning" && warningFill > 1000,
		qPrintable(QString("the warning has the look of a warning, also in the figure: %1 px of its fill").arg(warningFill)));
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
	bool ok = expect(!copied.isNull() && copied.size() == window.exportedFigure().size() && copied.width() > window.figureImage().width(),
		"Copy puts the figure with the column of the trains on the clipboard");
	// Without a CSV export, PNG... keeps its half of the row.
	auto* png = window.findChild<QPushButton*>("diagramExportPng");
	ok &= expect(png->width() <= png->parentWidget()->width() / 2 + 2,
		qPrintable(QString("PNG... keeps half of the row: %1 of %2 px").arg(png->width()).arg(png->parentWidget()->width())));
	// Only the chart in focus takes Copy, not the rest of the window.
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

// Lets queued relayouts and deferred deletes run.
static void settle() {
	for (int round = 0; round < 4; ++round) {
		QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QApplication::processEvents();
	}
}

// Moves the pointer to a point of the chart, in chart coordinates.
static void hoverChart(QChartView* view, const QPointF& point) {
	const QPoint position = view->mapFromScene(view->chart()->mapToScene(point));
	QMouseEvent move(QEvent::MouseMove, position, view->viewport()->mapToGlobal(position), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(view->viewport(), &move);
}

// One section of `ends` each: the route distance of the start and the end, then the geo X of both, in km.
static std::vector<BlockSectionExtent> geoSections(const std::vector<std::array<double, 4>>& ends) {
	std::vector<BlockSectionInput> inputs;
	for (std::size_t i = 0; i < ends.size(); ++i)
		inputs.push_back({"@s" + std::to_string(i) + "-B0@", ends[i][0], ends[i][1], ends[i][2], ends[i][3]});
	return buildBlockSectionExtents(inputs);
}

// Paimpol route0 from section `first` to section 72: blocks of 300 m from 10.21 km for section 36, ending in the switch
// section 71-B0/0-B1 from 21.01 to 21.45 km. The geo X is the route distance.
static std::vector<BlockSectionExtent> paimpolSections(int first) {
	std::vector<BlockSectionInput> inputs;
	for (int order = first; order < 72; ++order) {
		const double start = 10.21 + 0.3 * (order - 36);
		inputs.push_back({"@" + std::to_string(order - 1) + "-B0@", start, start + 0.3, start, start + 0.3});
	}
	inputs.push_back({"@71-B0@-21.235000/@0-B1@-21.252000", 21.01, 21.45, 21.01, 21.45, true});
	return buildBlockSectionExtents(inputs);
}

// A window built as the blocking-time windows are: one envelope across the sections, the distance axis exactly over the
// sections, the strip, the position axis and the clock downward from 07:00:00.
static QChart* showBlockingWindow(DiagramWindow& window, const std::vector<BlockSectionExtent>& extents, int width, int height,
	std::vector<BlockSectionMarks> marks = {}, int routeSectionCount = 0, const SceneSectionLabels* labels = nullptr) {
	const auto bounds = blockSectionBounds(extents).value_or(std::make_pair(0.0, 1.0));
	auto* chart = new QChart;
	auto* box = new QLineSeries;
	box->setName("T1 envelope");
	box->setProperty("trainId", "T1");
	box->append(bounds.first, 600);
	box->append(bounds.second, 1800);
	chart->addSeries(box);
	chart->createDefaultAxes();
	auto* x = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
	auto* y = qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first());
	x->setTitleText("Distance (km)");
	x->setRange(bounds.first, bounds.second);
	x->setProperty("exactRange", true);
	y->setTitleText("Time (s)");
	y->setRange(0, 3600);
	y->setReverse(true);
	window.setChart(chart);
	window.setBlockSections(extents, "route0", labels, std::move(marks), routeSectionCount);
	window.setPositionAxis(PositionScale(extents));
	window.setTimeAxisY(true, 7 * 3600);
	window.setMaximumSize(width, height);
	window.resize(width, height);
	window.show();
	settle();
	return chart;
}

// A label of the position axis: where it stands on the route, the geo X that it reads and its text as drawn.
struct PositionLabel {
	double routeKm;
	double geoKm;
	QString text;
};

static QCategoryAxis* positionAxisOf(const QChart* chart) {
	const auto axes = chart->axes(Qt::Horizontal);
	return axes.isEmpty() ? nullptr : qobject_cast<QCategoryAxis*>(axes.first());
}

static std::vector<PositionLabel> positionLabels(const QChart* chart) {
	std::vector<PositionLabel> labels;
	if (QCategoryAxis* axis = positionAxisOf(chart))
		for (const QString& label : axis->categoriesLabels()) {
			QString text = label;
			text.remove(QChar(0x200B));
			labels.push_back({axis->endValue(label), text.toDouble(), text});
		}
	return labels;
}

// The labels lie in the range in view, further along the route each, and at least 60 px apart.
static bool labelsApart(const QChart* chart, const std::vector<PositionLabel>& labels) {
	const QCategoryAxis* axis = positionAxisOf(chart);
	if (!axis || labels.size() < 2) return false;
	const double perKm = chart->plotArea().width() / (axis->max() - axis->min());
	for (std::size_t i = 0; i < labels.size(); ++i) {
		if (labels[i].routeKm < axis->min() - 1e-9 || labels[i].routeKm > axis->max() + 1e-9) return false;
		if (i > 0 && (labels[i].routeKm - labels[i - 1].routeKm) * perKm < 60 - 1e-6) return false;
	}
	return true;
}

// Each label reads the geo X of the scale where it stands.
static bool labelsReadScale(const std::vector<PositionLabel>& labels, const PositionScale& scale) {
	return !labels.empty() && std::all_of(labels.begin(), labels.end(), [&scale](const PositionLabel& label) {
		return std::abs(scale.geoAt(label.routeKm) - label.geoKm) < 1e-6;
	});
}

// The bottom axis of a blocking-time window reads the geo X at round values: up along a route that runs with the scene
// coordinate, down along one that runs against it. It replaces the distance axis with the series on it, and its labels
// follow every zoom, pan and resize, at least 60 px apart. Fit gives the range of the sections back. The readout gives the
// geo X under the pointer.
static bool exercisePositionAxis() {
	bool ok = true;
	// 20 km of route whose geo X starts at 10.5 km.
	std::vector<std::array<double, 4>> forwardEnds;
	for (int i = 0; i < 20; ++i) forwardEnds.push_back({double(i), double(i + 1), i + 10.5, i + 11.5});
	const auto forward = geoSections(forwardEnds);
	const PositionScale forwardScale(forward);
	DiagramWindow window("Blocking time");
	QChart* chart = showBlockingWindow(window, forward, 1200, 750);
	QCategoryAxis* axis = positionAxisOf(chart);
	auto labels = positionLabels(chart);
	ok &= expect(axis && axis->titleText() == "Position (km)" && chart->axes(Qt::Horizontal).size() == 1
			&& chart->series().first()->attachedAxes().contains(axis),
		"the position axis takes the place of the distance axis, with the series on it");
	ok &= expect(axis && axis->min() == 0 && axis->max() == 20 && labels.size() >= 4 && labelsApart(chart, labels)
			&& labelsReadScale(labels, forwardScale) && labels.front().geoKm < labels.back().geoKm
			&& std::all_of(labels.begin(), labels.end(), [](const PositionLabel& label) { return label.geoKm == std::round(label.geoKm); }),
		"the labels read round geo X values that count up along the route, 60 px apart or more");
	auto* view = window.findChild<QChartView*>();
	const QString before = labels.front().text;
	for (int step = 0; step < 6; ++step) wheel(view, {}, {0, 120}, Qt::ControlModifier);
	settle();
	labels = positionLabels(chart);
	ok &= expect(axis->max() - axis->min() < 8 && labelsApart(chart, labels) && labelsReadScale(labels, forwardScale) && labels.front().text != before,
		"zoomed in, the labels follow the range in view with a finer step");
	wheel(view, QPoint(-qRound(chart->plotArea().width() / 3), 0), {});
	settle();
	ok &= expect(labelsApart(chart, positionLabels(chart)) && labelsReadScale(positionLabels(chart), forwardScale),
		"panned, the labels follow the range in view");
	window.resize(700, 600);
	settle();
	ok &= expect(labelsApart(chart, positionLabels(chart)), "in a narrower window the labels stay 60 px apart");
	QMetaObject::invokeMethod(&window, "resetZoom");
	settle();
	ok &= expect(axis->min() == 0 && axis->max() == 20 && labelsApart(chart, positionLabels(chart)), "Fit shows the sections exactly again");
	// The readout gives the geo X, not the route distance.
	auto* readout = window.findChild<QLabel*>("resultReadout");
	moveTo(view, QPointF(5, 1200), chart->series().first());
	const QRegularExpressionMatch readoutMatch = QRegularExpression("^07:\\d\\d:\\d\\d at (\\d+\\.\\d+) km$").match(readout->text());
	ok &= expect(readoutMatch.hasMatch() && std::abs(readoutMatch.captured(1).toDouble() - 15.5) < 0.05,
		qPrintable("the readout gives the clock time at the geo X: " + readout->text()));
	// So does the hover of a sample: the envelope starts at route distance 0, geo X 10.5 km.
	moveTo(view, QPointF(0, 600), chart->series().first());
	const auto* sampleTip = window.findChild<QLabel*>("diagramTooltip");
	ok &= expect(sampleTip && sampleTip->text().contains("Position (km): 10.500"),
		qPrintable("the hover of a sample gives its geo X: " + (sampleTip ? sampleTip->text() : QString())));

	// Paimpol route1 runs against the scene coordinate: 10 km of route from geo X 21.45 km down to 11.45 km.
	std::vector<std::array<double, 4>> reversedEnds;
	for (int i = 0; i < 10; ++i) reversedEnds.push_back({double(i), double(i + 1), 21.45 - i, 20.45 - i});
	const auto reversed = geoSections(reversedEnds);
	DiagramWindow reversedWindow("Blocking time");
	QChart* reversedChart = showBlockingWindow(reversedWindow, reversed, 1200, 750);
	const auto down = positionLabels(reversedChart);
	bool countsDown = down.size() >= 3;
	for (std::size_t i = 1; i < down.size(); ++i) countsDown = countsDown && down[i].geoKm < down[i - 1].geoKm;
	ok &= expect(countsDown && labelsApart(reversedChart, down) && labelsReadScale(down, PositionScale(reversed)),
		"along a route against the scene coordinate the labels count down");

	// A route that turns back reads the same geo X twice; every label stays, and draws its text as it is.
	const auto zigzag = geoSections({{0, 1, 0, 1}, {1, 2, 1, 2}, {2, 3, 2, 1}, {3, 4, 1, 0}});
	DiagramWindow zigzagWindow("Blocking time");
	QChart* zigzagChart = showBlockingWindow(zigzagWindow, zigzag, 1200, 750);
	const auto twice = positionLabels(zigzagChart);
	const auto expected = PositionScale(zigzag).roundLabels(0, 4, zigzagChart->plotArea().width());
	QStringList texts;
	for (const PositionLabel& label : twice) texts << label.text;
	const QStringList drawn = drawnChartTexts(zigzagWindow);
	int repeats = 0;
	bool drawnPlain = true;
	for (const QString& text : drawn)
		if (text.contains(QChar(0x200B))) {
			++repeats;
			drawnPlain = drawnPlain && texts.contains(QString(text).remove(QChar(0x200B)));
		}
	ok &= expect(twice.size() == expected.size() && texts.removeDuplicates() > 0 && repeats > 0 && drawnPlain,
		qPrintable(QString("every label of a route that turns back is kept: %1 of %2").arg(twice.size()).arg(expected.size())));
	return ok;
}

// Where the geo X jumps, a break mark interrupts the bottom edge of the plot and its hover says where the position goes on.
// Only one of the two values at the jump gets a label.
static bool exerciseBreakMark() {
	// Netherlands route0: 18.298 km continues at 30.569 km.
	const auto extents = geoSections({{17.0, 18.298, 17.0, 18.298}, {18.298, 19.0, 30.569, 31.271}, {19.0, 20.0, 31.2715, 32.2715}});
	DiagramWindow window("Blocking time");
	QChart* chart = showBlockingWindow(window, extents, 1200, 750);
	auto* view = window.findChild<QChartView*>();
	auto* tooltip = window.findChild<QLabel*>("diagramTooltip");
	const QRectF plot = chart->plotArea();
	const double jumpX = plot.left() + (18.298 - 17.0) / 3.0 * plot.width();
	hoverChart(view, QPointF(jumpX + 2, plot.bottom() - 2));
	bool ok = expect(tooltip->isVisible() && tooltip->text() == "Position jumps\n18.298 km continues at 30.569 km",
		qPrintable("the break mark tells where the position goes on: " + tooltip->text()));
	hoverChart(view, QPointF(jumpX + 40, plot.bottom() - 2));
	ok &= expect(!tooltip->isVisible() || !tooltip->text().contains("Position jumps"), "away from the break mark there is no jump hover");
	auto* overlay = chart->findChild<QGraphicsObject*>("blockSectionOverlay");
	const QImage with = view->grab().toImage();
	overlay->hide();
	const QImage without = view->grab().toImage();
	overlay->show();
	const qreal ratio = with.devicePixelRatio();
	int changed = 0;
	for (int dy = -5; dy <= 5; ++dy)
		for (int dx = -5; dx <= 5; ++dx) {
			const QPoint at = view->mapFromScene(chart->mapToScene(QPointF(jumpX + dx, plot.bottom() + dy)));
			const QPoint pixel(qRound(at.x() * ratio), qRound(at.y() * ratio));
			if (with.rect().contains(pixel) && with.pixel(pixel) != without.pixel(pixel)) ++changed;
		}
	ok &= expect(changed > 10, "the break mark paints on the bottom edge of the plot at the jump");
	const auto labels = positionLabels(chart);
	ok &= expect(labelsApart(chart, labels) && labelsReadScale(labels, PositionScale(extents)),
		"the labels stay 60 px apart across the jump");
	return ok;
}

// The distance axis of a blocking-time window spans the sections exactly, as Assignment route0 from 0 to 12 km, with round
// ticks inside; without that request a range widens to round ticks past its ends, here to 14 km.
static bool exerciseExactRange() {
	const auto rangeAfterSetChart = [](bool exact) {
		DiagramWindow window("Range");
		QChart* chart = makeNamedChart({"A"});
		auto* x = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
		x->setRange(0, 12);
		if (exact) x->setProperty("exactRange", true);
		window.setChart(chart);
		return std::make_tuple(x->min(), x->max(), x->tickType() == QValueAxis::TicksDynamic && x->tickInterval() > 1.99 && x->tickInterval() <= 2);
	};
	const auto [exactLow, exactHigh, exactTicks] = rangeAfterSetChart(true);
	const auto [wideLow, wideHigh, wideTicks] = rangeAfterSetChart(false);
	bool ok = expect(exactLow == 0 && exactHigh == 12 && exactTicks, "an exact range keeps its ends and gets round ticks");
	ok &= expect(wideLow == 0 && wideHigh == 14 && wideTicks, "another range widens to round ticks past its ends");
	std::vector<BlockSectionInput> inputs;
	for (int i = 0; i < 6; ++i) inputs.push_back({"@" + std::to_string(i) + "-B0@", 2.0 * i, 2.0 * i + 2});
	const auto extents = buildBlockSectionExtents(inputs);
	const auto range = blockSectionAxisRange(extents, RouteDiagramPath{});
	DiagramWindow window("Blocking time");
	QChart* chart = showBlockingWindow(window, extents, 1200, 750);
	auto* view = window.findChild<QChartView*>();
	for (int step = 0; step < 3; ++step) wheel(view, {}, {0, 120}, Qt::ControlModifier);
	QMetaObject::invokeMethod(&window, "resetZoom");
	settle();
	const QCategoryAxis* axis = positionAxisOf(chart);
	ok &= expect(range == std::make_pair(0.0, 12.0) && axis && axis->min() == 0 && axis->max() == 12,
		"the position axis spans the sections exactly, also after zooming and Fit");
	return ok;
}

// The strip names each section by its short id, in one row when every id fits at the full view and in two otherwise, so
// zooming keeps the rows. It marks station, switch, critical and conflict sections, and keeps the ids of critical, first
// and last sections where two rows cannot name every section. The hover has one fact per line.
static bool exerciseSectionStrip() {
	bool ok = true;
	// The rows and the ids of the strip in a window of this size, the width of a cell of 300 m and that of the id 70-B0 with
	// the gap between two ids; with `zoomedRows`, also the rows once zoomed in.
	const auto rowsAt = [](const std::vector<BlockSectionExtent>& extents, int width, int height, double* cellPx, double* idPx, QStringList* shown, int* zoomedRows = nullptr) {
		DiagramWindow window("Blocking time");
		QChart* chart = showBlockingWindow(window, extents, width, height);
		*idPx = QFontMetricsF(window.font()).horizontalAdvance("70-B0") + 6;
		auto* view = window.findChild<QChartView*>();
		(void)view->grab(); // paints the strip, which records its ids
		const auto* overlay = chart->findChild<QGraphicsObject*>("blockSectionOverlay");
		const auto bounds = blockSectionBounds(extents).value();
		*cellPx = 0.3 * chart->plotArea().width() / (bounds.second - bounds.first);
		*shown = overlay->property("stripLabels").toStringList();
		const int rows = overlay->property("stripRows").toInt();
		if (zoomedRows) {
			for (int step = 0; step < 4; ++step) wheel(view, {}, {0, 120}, Qt::ControlModifier);
			settle();
			*zoomedRows = overlay->property("stripRows").toInt();
		}
		return rows;
	};
	// Paimpol route0, sections 50 to 72: 22 blocks of 300 m and the switch section 71-B0/0-B1.
	const auto partOfPaimpol = paimpolSections(50);
	double wideCell = 0, narrowCell = 0, idWidth = 0;
	QStringList wideIds, narrowIds;
	int wideZoomed = 0, narrowZoomed = 0;
	const int wideRows = rowsAt(partOfPaimpol, 1600, 900, &wideCell, &idWidth, &wideIds, &wideZoomed);
	const int narrowRows = rowsAt(partOfPaimpol, 900, 700, &narrowCell, &idWidth, &narrowIds, &narrowZoomed);
	if (!expect(wideCell > idWidth && narrowCell < idWidth, "every id fits in its cell at 1600 px and not at 900 px")) return false;
	ok &= expect(wideRows == 1 && wideZoomed == 1 && wideIds.size() == 23 && wideIds.contains("71-B0/0-B1") && wideIds.contains("49-B0"),
		qPrintable(QString("at 1600 px one row names every section: %1 rows, %2 ids").arg(wideRows).arg(wideIds.size())));
	ok &= expect(narrowRows == 2 && narrowZoomed == 2 && narrowIds.size() > 12 && narrowIds.contains("71-B0/0-B1"),
		qPrintable(QString("at 900 px the ids take two rows, also zoomed in: %1 rows, %2 ids").arg(narrowRows).arg(narrowIds.size())));
	// The strip is set before the window has its size, and chooses its rows again once it has: the room above the plot
	// follows the rows.
	for (const auto& [width, height, expectedRows] : {std::make_tuple(1600, 900, 1), std::make_tuple(900, 700, 2)}) {
		DiagramWindow window("Blocking time");
		QChart* chart = showBlockingWindow(window, partOfPaimpol, width, height);
		const auto* overlay = chart->findChild<QGraphicsObject*>("blockSectionOverlay");
		const double header = overlay->property("headerHeight").toDouble();
		const QLegend* legend = chart->legend();
		ok &= expect(overlay->property("stripRows").toInt() == expectedRows && legend->minimumHeight() == std::ceil(header)
				&& chart->plotArea().top() - header >= legend->geometry().top() - 0.5,
			qPrintable(QString("the room above the plot follows the rows at %1 px: strip %2 px, room %3 px").arg(width).arg(header).arg(legend->minimumHeight())));
	}
	// Paimpol route0, sections 36 to 72, in windows 1200 px and 1728 px wide: two rows that name most of the 37 sections,
	// with the first and the last.
	const auto paimpol = paimpolSections(36);
	for (const auto& [width, height, least] : {std::make_tuple(1200, 750, 20), std::make_tuple(1728, 1000, 30)}) {
		double cell = 0, id = 0;
		QStringList ids;
		const int rows = rowsAt(paimpol, width, height, &cell, &id, &ids);
		std::cout << "strip of Paimpol sections 36 to 72 at " << width << " px: " << rows << " rows, " << ids.size() << " of 37 ids\n";
		ok &= expect(rows == 2 && ids.size() >= least && ids.size() <= 37 && ids.contains("35-B0") && ids.contains("71-B0/0-B1"),
			qPrintable(QString("the strip of 37 sections at %1 px names %2, with the first and the last").arg(width).arg(ids.size())));
	}
	// Where two rows cannot name every section, the id of a critical section goes first: of three sections at one spot the
	// first and the critical one are named, the second is not.
	auto crowded = buildBlockSectionExtents({{"@first-B0@", 0, 0.01}, {"@second-B0@", 0.01, 0.02}, {"@third-B0@", 0.02, 0.03}, {"@last-B0@", 0.03, 10}});
	std::vector<BlockSectionMarks> crowdedMarks(4);
	crowdedMarks[2].critical = true;
	{
		DiagramWindow window("Blocking time");
		QChart* chart = showBlockingWindow(window, crowded, 1200, 750, crowdedMarks);
		(void)window.findChild<QChartView*>()->grab();
		const QStringList ids = chart->findChild<QGraphicsObject*>("blockSectionOverlay")->property("stripLabels").toStringList();
		ok &= expect(ids.contains("third-B0") && ids.contains("first-B0") && ids.contains("last-B0") && !ids.contains("second-B0"),
			qPrintable("the strip keeps the critical, first and last ids: " + ids.join(", ")));
	}
	// Where two rows name every section in route order, the strip keeps that order. The ids of the first four sections
	// overlap their neighbours only, so they alternate between the rows. Placing the first section and the switch section
	// 14-B0 first would put both in the top row and leave no room for 13-B0.
	{
		const auto chainOf = [](double cellKm) {
			return buildBlockSectionExtents({{"@11-B0@", 0, cellKm}, {"@12-B0@", cellKm, 2 * cellKm}, {"@13-B0@", 2 * cellKm, 3 * cellKm},
				{"@14-B0@", 3 * cellKm, 4 * cellKm, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), true},
				{"@15-B0@", 4 * cellKm, 10}});
		};
		double plotWidth = 0, chainIdWidth = 0;
		{
			DiagramWindow window("Blocking time");
			plotWidth = showBlockingWindow(window, chainOf(0.3), 1200, 750)->plotArea().width();
			chainIdWidth = QFontMetricsF(window.font()).horizontalAdvance("13-B0");
		}
		DiagramWindow window("Blocking time");
		QChart* chart = showBlockingWindow(window, chainOf(0.75 * (chainIdWidth + 6) * 10 / plotWidth), 1200, 750);
		(void)window.findChild<QChartView*>()->grab();
		const auto* overlay = chart->findChild<QGraphicsObject*>("blockSectionOverlay");
		const QStringList ids = overlay->property("stripLabels").toStringList();
		ok &= expect(overlay->property("stripRows").toInt() == 2 && ids.size() == 5,
			qPrintable("two rows name the five sections in route order: " + ids.join(", ")));
	}

	// The marks: a station, a switch, a critical block, a conflict and a plain block, 1 km each.
	auto marked = buildBlockSectionExtents({{"@10-B0@", 0, 1, 0, 1}, {"@11-B0@-1.5/@0-B1@-1.6", 1, 2, 1, 2, true}, {"@12-B0@", 2, 3, 2, 3},
		{"@13-B0@", 3, 4, 3, 4}, {"@14-B0@", 4, 5, 4, 5}});
	for (BlockSectionExtent& extent : marked) extent.order += 40;
	std::vector<BlockSectionMarks> marks(5);
	marks[0].station = true;
	marks[2].critical = true;
	marks[3].conflict = true;
	SceneSectionLabels labels;
	labels.byReference = {{"@12-B0@", "B0 2.000 to 3.000 km"}};
	DiagramWindow window("Blocking time");
	QChart* chart = showBlockingWindow(window, marked, 1200, 750, marks, 125, &labels);
	auto* view = window.findChild<QChartView*>();
	const auto* overlay = chart->findChild<QGraphicsObject*>("blockSectionOverlay");
	const QImage image = view->grab().toImage();
	const QRectF plot = chart->plotArea();
	const double top = plot.top() - overlay->property("headerHeight").toDouble();
	const double bottom = top + overlay->property("stripHeight").toDouble() - 4;
	const auto colourAt = [&](double km, double y) {
		const QPoint at = view->mapFromScene(chart->mapToScene(QPointF(plot.left() + km / 5 * plot.width(), y)));
		return image.pixelColor(qRound(at.x() * image.devicePixelRatio()), qRound(at.y() * image.devicePixelRatio()));
	};
	const auto near = [](const QColor& a, const QColor& b) {
		return std::abs(a.red() - b.red()) < 16 && std::abs(a.green() - b.green()) < 16 && std::abs(a.blue() - b.blue()) < 16;
	};
	const QColor station(91, 104, 113), amber(227, 162, 26), red(158, 27, 27), hatch(160, 172, 180);
	ok &= expect(near(colourAt(0.1, top + 1.5), station) && !near(colourAt(4.1, top + 1.5), station), "a station section has a dark bar on top");
	ok &= expect(near(colourAt(2.1, bottom - 1.5), amber) && near(colourAt(3.1, bottom - 1.5), red) && !near(colourAt(4.1, bottom - 1.5), amber)
			&& !near(colourAt(4.1, bottom - 1.5), red),
		"a critical block has an amber bar and a conflict a red bar below");
	int hatched = 0, plainHatched = 0;
	for (int dx = 0; dx < 40; ++dx)
		for (int dy = 5; top + dy < bottom - 5; ++dy) {
			hatched += near(colourAt(1.05 + dx * 0.005, top + dy), hatch);
			plainHatched += near(colourAt(4.05 + dx * 0.005, top + dy), hatch);
		}
	ok &= expect(hatched > 20 && plainHatched == 0, "a switch section is hatched");
	hoverChart(view, QPointF(plot.left() + 2.5 / 5 * plot.width(), top + 6));
	auto* tooltip = window.findChild<QLabel*>("diagramTooltip");
	ok &= expect(tooltip->text() == "12-B0\nB0 2.000 to 3.000 km\nBlock section\nCritical block\nSection 43 of 125\nPosition 2.000 to 3.000 km\n"
									"2.000 to 3.000 km along route0",
		qPrintable("the hover of a section has its id, label, kind, marks, place and extents: " + tooltip->text()));
	hoverChart(view, QPointF(plot.left() + 1.5 / 5 * plot.width(), top + 6));
	ok &= expect(tooltip->text().startsWith("11-B0/0-B1\nSwitch section\nSection 42 of 125\n"), qPrintable("a switch section: " + tooltip->text()));
	hoverChart(view, QPointF(plot.left() + 0.5 / 5 * plot.width(), top + 6));
	ok &= expect(tooltip->text().startsWith("10-B0\nStation section\n"), qPrintable("a station section: " + tooltip->text()));
	return ok;
}

// The clock axis is titled "Time (hh:mm)" and reads HH:MM on whole minutes. Ticks less than a minute apart keep their
// seconds, and a range of more than a day keeps every tick although the clock reads the same again.
static bool exerciseClockMinutes() {
	const auto clockLabels = [](double seconds, long long startOffset) {
		DiagramWindow window("Clock");
		QChart* chart = makeNamedChart({"A"});
		qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first())->setRange(0, seconds);
		window.setChart(chart);
		window.setTimeAxisY(true, startOffset);
		window.resize(1152, 640);
		window.show();
		settle();
		auto* clock = qobject_cast<QCategoryAxis*>(chart->axes(Qt::Vertical).first());
		QStringList labels;
		if (clock && clock->titleText() == "Time (hh:mm)")
			for (const QString& label : clock->categoriesLabels()) labels << QString(label).remove(QChar(0x200B));
		return labels;
	};
	const QStringList hour = clockLabels(3600, 8 * 3600);
	bool ok = expect(hour == QStringList({"08:00", "08:10", "08:20", "08:30", "08:40", "08:50", "09:00"}),
		qPrintable("an hour reads whole minutes: " + hour.join(", ")));
	const QStringList seconds = clockLabels(120, 8 * 3600);
	ok &= expect(seconds.size() >= 3 && seconds.first() == "08:00:00" && seconds.at(1) == "08:00:30",
		qPrintable("ticks less than a minute apart keep their seconds: " + seconds.join(", ")));
	const QStringList days = clockLabels(2 * 86400, 0);
	ok &= expect(days == QStringList({"00:00", "06:00", "12:00", "18:00", "00:00", "06:00", "12:00", "18:00", "00:00"}),
		qPrintable("two days keep every tick: " + days.join(", ")));
	return ok;
}

// After the save dialog of an export closes, saved or cancelled, the window is raised and active again, also when the
// main window took the activation while the dialog was open, as AppKit gives it on macOS.
static bool exerciseExportKeepsWindowInFront() {
	QMainWindow mainWindow;
	mainWindow.resize(500, 400);
	mainWindow.show();
	auto* window = new DiagramWindow("In front", &mainWindow);
	window->setChart(buttonTestChart());
	window->setCsvProvider([](const QStringList&) { return std::string("Train\nA\n"); }, "front.csv");
	window->show();
	window->activateWindow();
	bool ok = expect(waitFor([window] { return QApplication::activeWindow() == window; }), "the window is active before the export");
	for (const QString& text : {QStringLiteral("CSV..."), QStringLiteral("PNG...")}) {
		bool mainActive = false;
		QTimer::singleShot(0, window, [&mainWindow, &mainActive] {
			for (QWidget* widget : QApplication::topLevelWidgets())
				if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
					mainWindow.activateWindow();
					mainActive = waitFor([&mainWindow] { return QApplication::activeWindow() == &mainWindow; });
					dialog->reject();
				}
		});
		buttonNamed(*window, text)->click();
		const bool active = waitFor([window] { return QApplication::activeWindow() == window; });
		ok &= expect(mainActive && active,
			qPrintable(text + " leaves the window active after its dialog closes, though the main window took the activation"));
	}
	return ok;
}

// A chart without series says so in the middle of its plot with the text of setEmptyText, and the panel leaves out
// Trains, Key and Selected, which have nothing to show; Export stays. A chart with series has no such text.
// On a dark background, the open circles of a planned line of a train that is not selected fade with the line: their
// white is mixed with the background too, and comes back when the selection ends.
static bool exerciseMutedPlannedMarkers() {
	DiagramWindow window("Muted circles");
	QPalette dark = window.palette();
	dark.setColor(QPalette::Base, QColor(30, 30, 30));
	window.setPalette(dark);
	auto* chart = new QChart;
	auto* planned = new QLineSeries;
	planned->setName("A (planned)");
	planned->setProperty("trainId", "A");
	planned->setProperty("planned", true);
	planned->setPointsVisible(true);
	planned->append(0, 10);
	planned->append(100, 20);
	chart->addSeries(planned);
	auto* other = new QLineSeries;
	other->setName("B");
	other->setProperty("trainId", "B");
	other->append(0, 50);
	other->append(100, 50);
	chart->addSeries(other);
	chart->createDefaultAxes();
	window.setChart(chart);
	window.resize(900, 600);
	window.show();
	QApplication::processEvents();
	auto* view = window.findChild<QChartView*>();
	if (!expect(view && !planned->lightMarker().isNull(), "the planned line draws open circles")) return false;
	const auto middle = [planned]() {
		const QImage marker = planned->lightMarker();
		return marker.pixelColor(marker.width() / 2, marker.height() / 2);
	};
	const QColor background = view->chart()->backgroundBrush().color();
	bool ok = expect(middle() == QColor(Qt::white), "an open circle is white inside");
	clickChart(view, QPointF(50, 50), other);
	const QColor faded = middle();
	ok &= expect(faded.alpha() == 255 && std::abs(faded.red() - background.red()) < std::abs(255 - background.red()) / 2,
		qPrintable("a circle of a train that is not selected fades towards the background: " + faded.name()));
	QMetaObject::invokeMethod(&window, "clearPin");
	ok &= expect(middle() == QColor(Qt::white), "a circle is white again when the selection ends");
	return ok;
}

static bool exerciseEmptyChart() {
	const auto shownHeadings = [](const DiagramWindow& window) {
		QStringList texts;
		for (const QLabel* label : window.findChildren<QLabel*>())
			if (label->property("resultSectionHeading").toBool() && label->isVisible()) texts << label->text();
		return texts;
	};
	DiagramWindow window("Delays");
	window.setEmptyText("No arrival delays at stations of route8");
	auto* chart = new QChart;
	auto* x = new QValueAxis;
	auto* y = new QValueAxis;
	x->setTitleText("Distance (km)");
	y->setTitleText("Arrival delay (min)");
	chart->addAxis(x, Qt::AlignBottom);
	chart->addAxis(y, Qt::AlignLeft);
	window.setChart(chart);
	window.setCsvProvider([](const QStringList&) { return std::string(); }, "timetable.csv");
	window.setMaximumSize(1200, 750);
	window.resize(1200, 750);
	window.show();
	settle();
	const auto* note = chart->findChild<QGraphicsObject*>("diagramEmptyNote");
	bool ok = expect(note && note->isVisible() && note->property("text").toString() == "No arrival delays at stations of route8"
			&& note->boundingRect() == chart->plotArea(),
		"a chart without series shows its empty text over the plot");
	ok &= expect(shownHeadings(window) == QStringList({"Export"}), qPrintable("the panel of an empty chart has only Export: " + shownHeadings(window).join(", ")));
	// The text is drawn in the middle of the plot, where an empty chart has only its light grid.
	const QImage drawn = window.findChild<QChartView*>()->grab().toImage();
	const QRectF plot = chart->plotArea();
	const qreal ratio = drawn.devicePixelRatio();
	int dark = 0;
	for (int row = qRound((plot.center().y() - 10) * ratio); row < qRound((plot.center().y() + 10) * ratio); ++row)
		for (int column = qRound((plot.center().x() - 60) * ratio); column < qRound((plot.center().x() + 60) * ratio); ++column)
			if (qGray(drawn.pixel(column, row)) < 160) ++dark;
	ok &= expect(dark > 20, qPrintable(QString("the empty text is drawn in the middle of the plot: %1 dark pixels").arg(dark)));
	auto* full = buttonTestChart();
	window.setChart(full);
	settle();
	ok &= expect(!full->findChild<QGraphicsObject*>("diagramEmptyNote") && shownHeadings(window).contains("Trains"),
		"a chart with series has no empty text and shows its trains");
	return ok;
}

// Inside a blocking box a click selects a line only within 4 px of it: a click on or very near the line selects its
// train, one further away in the box outlines the box, which names itself by its "inspectionTitle". The selected train
// is drawn over the others: its series go last in the chart and keep their axes. A click just outside the box, near its
// outline, selects the box by its title too, with its own lines only and without an outline.
static bool exerciseBoxPrecedence() {
	DiagramWindow window("Box precedence");
	auto* chart = new QChart;
	addBlockingBox(chart, "A", BlockingTimeKind::Ordinary, 0, 10, 20, 80);
	auto* outline = qobject_cast<QXYSeries*>(chart->series().last());
	outline->setProperty("inspectionTitle", "A on 3-B0");
	auto* through = new QLineSeries;
	through->setName("B");
	through->setProperty("trainId", "B");
	through->append(0, 50);
	through->append(10, 50);
	chart->addSeries(through);
	auto* other = new QLineSeries;
	other->setName("C");
	other->setProperty("trainId", "C");
	other->append(0, 90);
	other->append(10, 95);
	chart->addSeries(other);
	chart->createDefaultAxes();
	for (auto* axis : chart->axes()) qobject_cast<QValueAxis*>(axis)->setRange(0, 100);
	chart->axes(Qt::Horizontal).first()->setRange(0, 10);
	window.setChart(chart);
	window.setMaximumSize(1200, 750);
	window.resize(1200, 750);
	window.show();
	settle();
	auto* view = window.findChild<QChartView*>();
	auto* title = window.findChild<QLabel*>("diagramSelectedTitle");
	if (!expect(view && title, "the window has its chart and Selected")) return false;
	const auto clickPixel = [&](const QPointF& position) {
		const QPoint point = view->mapFromScene(chart->mapToScene(position));
		QMouseEvent press(QEvent::MouseButtonPress, point, view->viewport()->mapToGlobal(point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QMouseEvent release(QEvent::MouseButtonRelease, point, view->viewport()->mapToGlobal(point), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(view->viewport(), &press);
		QApplication::sendEvent(view->viewport(), &release);
		QApplication::processEvents();
	};
	const QPointF onLine = chart->mapToPosition(QPointF(5, 50), through);
	clickPixel(onLine + QPointF(0, 8));
	bool ok = expect(chart->findChild<QGraphicsObject*>("diagramSelectionOutline") && title->text() == "A on 3-B0",
		qPrintable("a click in a box 8 px from a line outlines the box: " + title->text()));
	clickPixel(onLine + QPointF(0, 2));
	ok &= expect(!chart->findChild<QGraphicsObject*>("diagramSelectionOutline") && title->text() == "B",
		qPrintable("a click in a box 2 px from a line selects the line: " + title->text()));
	const QList<QAbstractSeries*> order = chart->series();
	ok &= expect(order.last() == through && order.size() == 4 && through->attachedAxes().size() == 2
			&& chart->mapToPosition(QPointF(5, 50), through) == onLine,
		"the selected train is drawn over the others, with its axes");
	clickPixel(chart->mapToPosition(QPointF(5, 93), other));
	ok &= expect(chart->series().last() == other && title->text() == "C", "a train selected later is drawn over the others in turn");
	// 5 px outside the box, beyond its edge at 20 s inside the plot, the box still names itself and gives only its own
	// lines, without the values of its nearest corner, and is not outlined.
	const QPointF edge = chart->mapToPosition(QPointF(5, 20), outline);
	const QPointF inward = chart->mapToPosition(QPointF(5, 80), outline) - edge;
	clickPixel(edge - inward * (5 / QLineF(QPointF(), inward).length()));
	const auto* tooltip = window.findChild<QLabel*>("diagramTooltip");
	const auto* body = window.findChild<QLabel*>("diagramPinLabel");
	const QStringList lines = (tooltip ? tooltip->text() : QString()).split('\n') + (body ? body->text() : QString()).split('\n');
	const bool sample = std::any_of(lines.cbegin(), lines.cend(), [](const QString& line) { return line.startsWith("x:") || line.startsWith("y:"); });
	ok &= expect(!chart->findChild<QGraphicsObject*>("diagramSelectionOutline") && title->text() == "A on 3-B0",
		qPrintable("a click near the outline outside a box selects the box by its title without outline: " + title->text()));
	ok &= expect(tooltip && body && tooltip->text().startsWith("A on 3-B0\n") && body->text().contains("Blocked 20 to 80 s") && !sample,
		qPrintable("the box near its outline gives its own lines and no sample: " + lines.join(" | ")));
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
	series->setProperty("planned", true);
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
	QWidget* filter = trainListOf(window) ? trainListOf(window)->tree() : nullptr;
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
	TrainKeyList* list = trainListOf(window);
	ok &= expect(list && list->trainIds() == QStringList({"A", "B"}), "train grouping");
	if (list) list->setTrainVisible("B", false);
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
	ok &= expect(tooltip->isVisible() && tooltip->text().contains("Time: 08:00:30") && tooltip->text().contains("20.00"),
		"time Y formats sample time with its seconds and leaves X numeric");
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
	ok &= expect(emptyTooltip->isVisible() && emptyTooltip->text() == "Block occupation\nResource: B12 | Type: block | Start: 20 s | End: 60 s",
		qPrintable("block interior reports its name and context, not the values of a corner: " + emptyTooltip->text()));
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
	input.setRollingStockSubject(QStringLiteral("Unit A"));
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
		!trainListOf(input)->isVisible()
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
	ok &= exerciseColourRule();
	ok &= exerciseBlockingTimeLook();
	ok &= exerciseKeySamples();
	ok &= exerciseLegend();
	ok &= exerciseTrainKeyGroups();
	ok &= exerciseTrainKeyChecks();
	ok &= exerciseTrainKeyOrder();
	ok &= exerciseSelection();
	ok &= exerciseExportColumn();
	ok &= exerciseButtonDefaults();
	ok &= exerciseRunContext();
	ok &= exerciseBlockSections();
	ok &= exercisePlainToolTips();
	ok &= exerciseLongToolTips();
	ok &= exerciseStationAxis();
	ok &= exerciseBusyStationLabels();
	ok &= exerciseLongAxisTitle();
	ok &= exerciseRoundTicks();
	ok &= exerciseClockTicks();
	ok &= exerciseSingleVerticalAxis();
	ok &= exerciseCsvSavedMessage();
	ok &= exerciseExportDialogs();
	ok &= exerciseHeader();
	ok &= exercisePanelSections();
	ok &= exerciseReadout();
	ok &= exerciseFigure();
	ok &= exerciseCopy();
	ok &= exercisePositionAxis();
	ok &= exerciseBreakMark();
	ok &= exerciseExactRange();
	ok &= exerciseSectionStrip();
	ok &= exerciseClockMinutes();
	ok &= exerciseExportKeepsWindowInFront();
	ok &= exerciseMutedPlannedMarkers();
	ok &= exerciseEmptyChart();
	ok &= exerciseBoxPrecedence();
	if (!ok) return 1;
	std::cout << "all DiagramWindow tests passed\n";
	return 0;
}
