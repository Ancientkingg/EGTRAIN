// Tests of the Capacity window with the worked example of docs/guides/capacity-analysis.md: the line F1, F2 of
// test_areacapacitywindow.cpp, eight blocks, F1 then F2, profile references 59 s and 199 s, authored departures 120 s and
// 180 s, analysed over 3600 s with F2 closing the cycle. The guide works out a cycle of 471.5 s, 13.1 % of the hour, with
// the critical block @3-B0@. Times are in seconds. The test shows no message box and opens no file chooser.
//
// The last tests run the Blocking time and Capacity scope dialogs on that line with stations (stationRun) and choose
// their stretch by stations, by blocks and their interval by clock times. They drive the dialogs by the labels of their
// rows and find the signals of the stretch by name.

#include "diagrams/BlockingTimeData.h"
#include "diagrams/BlockingTimeScope.h"
#include "diagrams/CapacityScope.h"
#include "diagrams/CapacityWindow.h"
#include "diagrams/ResultFrame.h"
#include "util/TimeFormat.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFont>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace {

BlockingTimeDiagramInput occupation(const char* id, double start, double end, double runStart = -1.0) {
	BlockingTimeDiagramInput value;
	value.blockId = id;
	value.startOccTime = start;
	value.endOccTime = end;
	value.posStart = 0.0;
	value.posEnd = 100.0;
	value.isComplete = true;
	value.endClearTime = end;
	value.startRunTime = runStart;
	return value;
}

// A train of route 0 with a planned departure at A, at the start of the first block.
CompletedTrain train(const char* id, std::vector<BlockingTimeDiagramInput> blockTimes, double authoredDeparture) {
	CompletedTrain value;
	value.trainId = id;
	value.operatingCode = id;
	value.routeIndex = 0;
	value.blockTimes = std::move(blockTimes);
	CompletedStop stop;
	stop.station = "A";
	stop.routePositionMeters = 0.0;
	stop.scheduledDeparture = authoredDeparture;
	stop.scheduledArrival = -1.0;
	value.stops.push_back(stop);
	return value;
}

CompletedRunData lineRun() {
	CompletedRunData data;
	CompletedRoute route;
	route.id = "R";
	route.corridor = "main";
	double km = 0.0;
	for (const char* blockId : {"@0-B0@", "@1-B0@", "@2-B0@", "@3-B0@", "@4-B0@", "@5-B0@", "@6-B0@", "@7-B0@"}) {
		route.blocks.push_back({blockId, km, km + 1.0});
		km += 1.0;
	}
	data.routes = {route};
	data.trains = {
		train("F1",
			{occupation("@0-B0@", 59.0, 146.0, 59.0), occupation("@1-B0@", 53.5, 201.0), occupation("@2-B0@", 135.5, 257.0),
				occupation("@3-B0@", 190.5, 674.0), occupation("@4-B0@", 246.5, 748.0), occupation("@5-B0@", 654.5, 803.0),
				occupation("@6-B0@", 737.5, 859.0), occupation("@7-B0@", 792.5, 983.0)},
			120.0),
		train("F2",
			{occupation("@0-B0@", 199.0, 286.0, 199.0), occupation("@1-B0@", 193.5, 357.0), occupation("@2-B0@", 275.5, 686.0),
				occupation("@3-B0@", 342.5, 892.0), occupation("@4-B0@", 666.5, 966.0), occupation("@5-B0@", 872.5, 1021.0),
				occupation("@6-B0@", 955.5, 1077.0), occupation("@7-B0@", 1010.5, 1201.0)},
			180.0)};
	data.sectionLabels.byReference["@3-B0@"] = "B0 0.690 to 0.920 km";
	return data;
}

CapacityAnalysisResult lineResult(const CompletedRunData& data) {
	CapacityAnalysisScope scope;
	scope.routeIndex = 0;
	scope.blockIds = {"@0-B0@", "@1-B0@", "@2-B0@", "@3-B0@", "@4-B0@", "@5-B0@", "@6-B0@", "@7-B0@"};
	scope.sectionIndices = {0, 1, 2, 3, 4, 5, 6, 7};
	scope.occurrenceIds = {"F1", "F2"};
	scope.cycleEndOccurrenceId = "F2";
	return analyzeCapacity(capacityInputsFrom(data, scope), scope.periodSeconds, scope.cycleEndOccurrenceId);
}

const QString kContext = QStringLiteral("Case A / Scenario B · R (main), blocks 1 to 8");

const ResultFigure* figure(const QWidget& window, const char* name) {
	return window.findChild<ResultFigure*>(name);
}

QString figureText(const QWidget& window, const char* name) {
	const ResultFigure* shown = figure(window, name);
	return shown ? shown->value() + " | " + shown->caption() : QStringLiteral("missing");
}

void pressKey(QWidget* widget, Qt::Key key) {
	QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
	QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
	QApplication::sendEvent(widget, &press);
	QApplication::sendEvent(widget, &release);
}

QStringList headerTexts(const QTableWidget* table) {
	QStringList headers;
	for (int column = 0; table && column < table->columnCount(); ++column)
		headers << table->horizontalHeaderItem(column)->text();
	return headers;
}

RouteDiagramNode node(const char* id, const char* stationId, const char* stationName, double km) {
	RouteDiagramNode value;
	value.nodeId = id;
	value.stationId = stationId;
	value.stationName = stationName;
	value.positionKm = km;
	return value;
}

// The line of lineRun with stations: Alpha at its start, Bravo where blocks 3 and 4 meet, Charlie with two nodes inside
// block 6 and Delta at its end. Route S of the corridor side has no station and route T only the station Echo. Route U
// of the corridor loop visits Foxtrot inside its block 1, Golf where its blocks 2 and 3 meet and Foxtrot again inside its
// block 4. The run lasts an hour from 07:00:00. The trains enter every block 5 s after its occupation starts, so that any
// stretch of route R has the two trains for Capacity.
CompletedRunData stationRun() {
	CompletedRunData data = lineRun();
	for (CompletedTrain& lineTrain : data.trains)
		for (BlockingTimeDiagramInput& occupation : lineTrain.blockTimes)
			if (occupation.startRunTime < 0.0)
				occupation.startRunTime = occupation.startOccTime + 5.0;
	data.settings.timestepSeconds = 1.0;
	data.settings.horizonSteps = 3600.0;
	data.settings.startOffsetSeconds = 7 * 3600;
	data.routes.front().path.nodes = {node("n0", "a", "Alpha", 0.0), node("n1", "", "", 1.0), node("n3", "b", "Bravo", 3.0),
		node("n55", "c", "Charlie", 5.5), node("n58", "c", "Charlie", 5.8), node("n8", "d", "Delta", 8.0)};
	CompletedRoute side;
	side.id = "S";
	side.corridor = "side";
	side.blocks = {{"@0-S@", 0.0, 1.0}, {"@1-S@", 1.0, 2.0}};
	side.path.nodes = {node("s0", "", "", 0.0), node("s2", "", "", 2.0)};
	CompletedRoute spur;
	spur.id = "T";
	spur.blocks = {{"@0-T@", 0.0, 1.0}, {"@1-T@", 1.0, 2.0}};
	spur.path.nodes = {node("t1", "e", "Echo", 1.0)};
	CompletedRoute loop;
	loop.id = "U";
	loop.corridor = "loop";
	loop.blocks = {{"@0-U@", 0.0, 1.0}, {"@1-U@", 1.0, 2.0}, {"@2-U@", 2.0, 3.0}, {"@3-U@", 3.0, 4.0}};
	loop.path.nodes = {node("u05", "f", "Foxtrot", 0.5), node("u2", "g", "Golf", 2.0), node("u35", "f", "Foxtrot", 3.5)};
	data.routes.push_back(side);
	data.routes.push_back(spur);
	data.routes.push_back(loop);
	return data;
}

// The runtime section ids of the blocks `first` to `last` of route R, positions from 0.
QStringList lineIds(int first, int last) {
	QStringList ids;
	for (int index = first; index <= last; ++index)
		ids << QString("@%1-B0@").arg(index);
	return ids;
}

// Lets `answer` act on the open dialog titled `title`, and cancels the dialog when the answer leaves it open.
void answerDialog(const QString& title, const std::function<void(QDialog&)>& answer) {
	auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
	if (dialog == nullptr)
		QFAIL(qPrintable("the " + title + " dialog opens"));
	if (dialog->windowTitle() != title) {
		dialog->reject();
		QFAIL(qPrintable("the " + title + " dialog opens"));
	}
	answer(*dialog);
	if (dialog->isVisible())
		dialog->reject();
}

bool askBlockingTime(const CompletedRunData& run, BlockingTimeScope& scope, const std::function<void(QDialog&)>& answer) {
	QTimer::singleShot(0, [&answer]() { answerDialog("Blocking time", answer); });
	return chooseBlockingTimeScope(nullptr, blockingTimeScopeChoicesFrom(run), scope);
}

bool askCapacity(const CompletedRunData& run, CapacityAnalysisScope& scope, const std::function<void(QDialog&)>& answer) {
	QTimer::singleShot(0, [&answer]() { answerDialog("Capacity", answer); });
	return chooseCapacityAnalysisScope(nullptr, run, scope);
}

void press(QDialog& dialog, QDialogButtonBox::StandardButton button) {
	auto* buttons = dialog.findChild<QDialogButtonBox*>();
	if (buttons == nullptr || buttons->button(button) == nullptr)
		QFAIL("the dialog has its buttons");
	buttons->button(button)->click();
}

// The field of the row of the dialog whose label reads `label`, nullptr when there is none.
template <typename Field>
Field* fieldOf(const QDialog& dialog, const QString& label) {
	for (const QFormLayout* form : dialog.findChildren<QFormLayout*>())
		for (int row = 0; row < form->rowCount(); ++row) {
			const QLayoutItem* name = form->itemAt(row, QFormLayout::LabelRole);
			const QLayoutItem* field = form->itemAt(row, QFormLayout::FieldRole);
			const auto* text = name ? qobject_cast<const QLabel*>(name->widget()) : nullptr;
			if (text && field && text->text() == label)
				return qobject_cast<Field*>(field->widget());
		}
	return nullptr;
}

QRadioButton* kindButton(const QDialog& dialog, const QString& text) {
	for (QRadioButton* button : dialog.findChildren<QRadioButton*>())
		if (button->text() == text)
			return button;
	return nullptr;
}

QStringList entriesOf(const QComboBox* combo) {
	QStringList entries;
	for (int index = 0; combo && index < combo->count(); ++index)
		entries << combo->itemText(index);
	return entries;
}

QStringList tooltipsOf(const QComboBox* combo) {
	QStringList tooltips;
	for (int index = 0; combo && index < combo->count(); ++index)
		tooltips << combo->itemData(index, Qt::ToolTipRole).toString();
	return tooltips;
}

// The rows of a stretch: the route, the kinds and the From and To lists, by stations or by blocks.
struct StretchRows {
	QComboBox* route = nullptr;
	QRadioButton* stations = nullptr;
	QRadioButton* blocks = nullptr;
	QComboBox* from = nullptr;
	QComboBox* to = nullptr;
};

StretchRows stretchRows(const QDialog& dialog) {
	StretchRows rows;
	rows.route = fieldOf<QComboBox>(dialog, "Route / corridor:");
	rows.stations = kindButton(dialog, "Stations");
	rows.blocks = kindButton(dialog, "Blocks");
	const bool byStations = rows.stations && rows.stations->isChecked();
	rows.from = fieldOf<QComboBox>(dialog, byStations ? "From station:" : "From block:");
	rows.to = fieldOf<QComboBox>(dialog, byStations ? "To station:" : "To block:");
	return rows;
}

bool complete(const StretchRows& rows) {
	return rows.route && rows.stations && rows.blocks && rows.from && rows.to;
}

// Opens the list, puts the pointer on its entry `index` and closes the list again. Gives what `hovered`, a spy on
// entryHovered, holds last while the pointer is on the entry.
QVariantList hoverEntry(QComboBox* list, int index, const QSignalSpy& hovered) {
	list->showPopup();
	emit list->highlighted(index);
	const QVariantList sent = hovered.isEmpty() ? QVariantList() : hovered.last();
	list->hidePopup();
	return sent;
}

// The object of the dialog that sends the signals of the stretch.
QObject* stretchSignals(const QDialog& dialog) {
	for (QObject* child : dialog.findChildren<QObject*>())
		if (child->metaObject()->indexOfSignal("stretchChanged(QString,QStringList)") >= 0)
			return child;
	return nullptr;
}

} // namespace

class TestCapacityWindow : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void showsWorkedExampleInPanel();
	void namesCriticalBlocksOnce();
	void listsChainInTabs();
	void opensWithPanelOf200Px();
	void scrollsBodyWithLargeFont();
	void namesRoutesByEndStations();
	void choosesBlocksBetweenStations();
	void choosesStationVisitedTwice();
	void switchesBetweenStationsAndBlocks();
	void opensScopeOfBlocks();
	void entersClockTimes();
	void signalsStretchAndHover();
};

void TestCapacityWindow::initTestCase() {
	QFile stylesheet(QStringLiteral(EGTRAIN_DIALOG_QSS));
	QVERIFY2(stylesheet.open(QIODevice::ReadOnly), qPrintable("cannot read " + stylesheet.fileName()));
	qApp->setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
}

// The panel holds the figures of the worked example, Open Diagram and the export, and no paragraph explains the cycle.
void TestCapacityWindow::showsWorkedExampleInPanel() {
	const CompletedRunData data = lineRun();
	CapacityWindow window(lineResult(data), kContext, "From block 1: @0-B0@\nTo block 8: @7-B0@", data.sectionLabels);
	window.show();
	QApplication::processEvents();
	// the window is titled Capacity and is not modal
	QCOMPARE(window.windowTitle(), "Capacity");
	QVERIFY(!window.isModal());
	// the cycle figure
	QCOMPARE(figureText(window, "capacityCycle"), "471.5 s | Cycle");
	// the share figure
	QCOMPARE(figureText(window, "capacityShare"), "13.1% | of 1 h");
	// The header of the compressed blocking time carries the same figures in one line.
	CapacityAnalysisResult result = lineResult(data);
	// the figures for a header
	QCOMPARE(capacityCycleText(result), "cycle 471.5 s, 13.1% of 1 h");
	result.periodSeconds = 0.0;
	// without a period a header gives the cycle
	QCOMPARE(capacityCycleText(result), "cycle 471.5 s");
	// the critical block figure
	QCOMPARE(figureText(window, "capacityCriticalBlock"), "3-B0 | Critical block");
	const ResultFigure* cycle = figure(window, "capacityCycle");
	// the tooltip of the cycle names the trains that close it and their reference times
	if (cycle == nullptr)
		QFAIL("the tooltip of the cycle names the trains that close it and their reference times");
	QVERIFY(cycle->toolTip().contains("471.5 s Cycle"));
	QVERIFY(cycle->toolTip().contains("From F1 to F2"));
	QVERIFY(cycle->toolTip().contains("F1: planned departure at A"));
	QVERIFY(cycle->toolTip().contains("F2: planned departure at A"));
	const ResultFigure* critical = figure(window, "capacityCriticalBlock");
	// the tooltip of the critical block gives its label without an internal id
	if (critical == nullptr)
		QFAIL("the tooltip of the critical block gives its label without an internal id");
	QVERIFY(critical->toolTip().contains("3-B0: B0 0.690 to 0.920 km"));
	QVERIFY(!critical->toolTip().contains("@"));
	// The tables name blocks by their short ids, and their tooltips by labels without internal ids.
	QStringList rawIds;
	int cells = 0;
	for (const QTableWidget* table : window.findChildren<QTableWidget*>())
		for (int row = 0; row < table->rowCount(); ++row)
			for (int column = 0; column < table->columnCount(); ++column)
				if (const QTableWidgetItem* item = table->item(row, column)) {
					++cells;
					if (item->text().contains('@') || item->toolTip().contains('@')) rawIds << item->text() + " / " + item->toolTip();
				}
	// no cell names an internal id
	QCOMPARE_GT(cells, 0);
	QVERIFY2(rawIds.isEmpty(), qPrintable("no cell names an internal id: " + rawIds.join(", ")));

	auto* panel = window.findChild<QWidget*>("resultPanel");
	auto* open = window.findChild<QPushButton*>("capacityOpenDiagram");
	auto* exportButton = window.findChild<QPushButton*>("capacityExport");
	// the window has its panel, Open Diagram and the export
	if (panel == nullptr)
		QFAIL("the window has its panel, Open Diagram and the export");
	if (open == nullptr)
		QFAIL("the window has its panel, Open Diagram and the export");
	if (exportButton == nullptr)
		QFAIL("the window has its panel, Open Diagram and the export");
	QCOMPARE(window.exportButton(), exportButton);
	QCOMPARE(window.diagramButton(), open);
	bool inPanel = panel->isAncestorOf(open) && panel->isAncestorOf(exportButton);
	for (const char* name : {"capacityCycle", "capacityShare", "capacityCriticalBlock"})
		inPanel = inPanel && figure(window, name) && panel->isAncestorOf(figure(window, name));
	QVERIFY2(inPanel, "the figures, Open Diagram and the export lie in the panel");
	// Open Diagram has the view in its tooltip and the export reads CSV...
	QCOMPARE(open->text(), "Open Diagram");
	QCOMPARE(open->toolTip(), "Compressed blocking time");
	QCOMPARE(exportButton->text(), "CSV...");
	QCOMPARE(exportButton->accessibleName(), "Export CSV");
	QStringList headings;
	for (const QLabel* label : panel->findChildren<QLabel*>())
		if (label->property("resultSectionHeading").toBool() && label->isVisible())
			headings << label->text();
	// the panel has only the heading Export
	QCOMPARE(headings, QStringList({"Export"}));
	bool explained = window.findChild<QToolButton*>("capacityDetailsToggle") != nullptr;
	for (const QLabel* label : window.findChildren<QLabel*>())
		explained = explained || label->text().contains("Cycle time") || label->text().contains(QChar(0x00d7));
	QVERIFY2(!explained, "no Calculation details toggle and no formula in the window");

	const auto* context = window.findChild<QLabel*>("capacityContext");
	// the header names the run and the section, with the blocks in its tooltip
	if (context == nullptr)
		QFAIL("the header names the run and the section, with the blocks in its tooltip");
	QVERIFY(context->isVisible());
	QCOMPARE(context->text(), kContext);
	QVERIFY(context->toolTip().contains("To block 8: @7-B0@"));
	QVERIFY(!window.findChild<QLabel*>("dialogContext"));
	bool autoDefault = false;
	for (const QPushButton* button : window.findChildren<QPushButton*>())
		autoDefault = autoDefault || button->autoDefault() || button->isDefault();
	QVERIFY2(!autoDefault, "no button of the window is a default button");
	bool rowNumbers = false;
	for (const QTableWidget* table : window.findChildren<QTableWidget*>())
		rowNumbers = rowNumbers || !table->verticalHeader()->isHidden();
	QVERIFY2(!rowNumbers, "the tables have no row numbers");
	const auto* buttons = window.findChild<QDialogButtonBox*>();
	// the window keeps its Close button
	if (buttons == nullptr)
		QFAIL("the window keeps its Close button");
	const QPushButton* close = buttons->button(QDialogButtonBox::Close);
	if (close == nullptr)
		QFAIL("the window keeps its Close button");
	QVERIFY(close->isVisible());
	// The body reaches the edges of the window, and Close keeps the margins of a dialog.
	const auto* body = window.findChild<QScrollArea*>("dialogBodyScroll");
	const QPoint closeCorner = close->mapTo(&window, QPoint(close->width(), close->height()));
	// the body fills the width and Close lies inside the margins
	if (body == nullptr)
		QFAIL("the body fills the width and Close lies inside the margins");
	QCOMPARE(body->geometry().topLeft(), QPoint(0, 0));
	QCOMPARE(body->width(), window.width());
	QCOMPARE_GE(window.width() - closeCorner.x(), 8);
	QCOMPARE_GE(window.height() - closeCorner.y(), 8);

	// Tab goes through the header, the tabs and the table, then the panel.
	window.findChild<QToolButton*>("capacityPanelToggle")->setFocus(Qt::TabFocusReason);
	QStringList order;
	for (int step = 0; step < 5 && QApplication::focusWidget(); ++step) {
		QWidget* focused = QApplication::focusWidget();
		if (qobject_cast<QTableWidget*>(focused))
			order << QStringLiteral("table");
		else if (qobject_cast<QTabBar*>(focused))
			order << QStringLiteral("tabs");
		else
			order << focused->objectName();
		pressKey(focused, Qt::Key_Tab);
	}
	// Tab goes through the header, the tabs, the table and the panel
	QCOMPARE(order, QStringList({"capacityPanelToggle", "tabs", "table", "capacityOpenDiagram", "capacityExport"}));
}

// A block that several pairs touch is named once, and two blocks make the caption plural.
void TestCapacityWindow::namesCriticalBlocksOnce() {
	const CompletedRunData data = lineRun();
	CapacityAnalysisResult result = lineResult(data);
	// the worked example has one critical block
	QCOMPARE(result.criticalBlocks.size(), std::size_t(1));
	result.criticalBlocks.push_back(result.criticalBlocks.front());
	{
		const CapacityWindow window(result, kContext, QString(), data.sectionLabels);
		// a block of two rows is named once
		QCOMPARE(figureText(window, "capacityCriticalBlock"), "3-B0 | Critical block");
	}
	CapacityCriticalBlock other = result.criticalBlocks.front();
	other.leaderBlockId = "@4-B0@";
	result.criticalBlocks.push_back(other);
	const CapacityWindow window(result, kContext, QString(), data.sectionLabels);
	// two blocks are named in the order of the tab
	QCOMPARE(figureText(window, "capacityCriticalBlock"), "3-B0, 4-B0 | Critical blocks");
}

// The tabs of the chain with their counts, the columns of their tables and the names of the tables. Times lie at the right.
void TestCapacityWindow::listsChainInTabs() {
	const CompletedRunData data = lineRun();
	CapacityWindow window(lineResult(data), kContext, QString(), data.sectionLabels);
	const QTabWidget* tabs = window.findChild<QTabWidget*>();
	QStringList titles;
	for (int index = 0; tabs && index < tabs->count(); ++index)
		titles << tabs->tabText(index);
	// the tabs
	QCOMPARE(titles, QStringList({"Pairs (1)", "Compression (2)", "Critical blocks (1)"}));
	const auto* pairs = qobject_cast<QTableWidget*>(tabs ? tabs->widget(0) : nullptr);
	const auto* compression = qobject_cast<QTableWidget*>(tabs ? tabs->widget(1) : nullptr);
	const auto* critical = qobject_cast<QTableWidget*>(tabs ? tabs->widget(2) : nullptr);
	// the tabs hold the tables of the pairs, the compression and the critical blocks
	if (pairs == nullptr)
		QFAIL("the tabs hold the tables of the pairs, the compression and the critical blocks");
	if (compression == nullptr)
		QFAIL("the tabs hold the tables of the pairs, the compression and the critical blocks");
	if (critical == nullptr)
		QFAIL("the tabs hold the tables of the pairs, the compression and the critical blocks");
	// the tables are named
	QCOMPARE(pairs->objectName(), "capacityPairs");
	QCOMPARE(compression->objectName(), "capacityCompression");
	QCOMPARE(critical->objectName(), "capacityCritical");
	// the columns of the pairs
	QCOMPARE(headerTexts(pairs),
		QStringList({"Leader", "Follower", "Leader code", "Follower code", "Scheduled headway (s)", "Minimum headway (s)", "Buffer (s)",
			"Governing block"}));
	// the columns of the compression
	QCOMPARE(headerTexts(compression),
		QStringList({"Train", "Operating code", "Original reference (s)", "Scheduled reference (s)", "Compressed reference (s)", "Shift (s)",
			"Governing predecessor"}));
	// the columns of the critical blocks
	QCOMPARE(headerTexts(critical), QStringList({"Leader", "Follower", "Leader block", "Follower block", "Gap (s)"}));
	// the times lie at the right and the blocks at the left
	QVERIFY(pairs->item(0, 6)->textAlignment() & Qt::AlignRight);
	QVERIFY(!(pairs->item(0, 7)->textAlignment() & Qt::AlignRight));
	QVERIFY(compression->item(0, 5)->textAlignment() & Qt::AlignRight);
	QVERIFY(!(compression->item(0, 6)->textAlignment() & Qt::AlignRight));
	QVERIFY(critical->item(0, 4)->textAlignment() & Qt::AlignRight);
	QVERIFY(!(critical->item(0, 3)->textAlignment() & Qt::AlignRight));
	// a train has its reference time as tooltip
	QCOMPARE(pairs->item(0, 0)->toolTip(), "Reference time: planned departure at A");
	QVERIFY(pairs->item(0, 2)->toolTip().isEmpty());
}

// The window opens with a panel of 200 px, and a table whose columns do not fit scrolls sideways.
void TestCapacityWindow::opensWithPanelOf200Px() {
	const CompletedRunData data = lineRun();
	CapacityWindow window(lineResult(data), kContext, QString(), data.sectionLabels);
	window.show();
	QApplication::processEvents();
	auto* panel = window.findChild<QWidget*>("resultPanel");
	auto* pairs = qobject_cast<QTableWidget*>(window.findChild<QTabWidget*>()->widget(0));
	// the window opens at most 960 px wide with a panel of 200 px
	QCOMPARE_LE(window.width(), 960);
	if (panel == nullptr)
		QFAIL("the window opens at most 960 px wide with a panel of 200 px");
	QCOMPARE(panel->width(), 200);
	int columns = 0;
	for (int column = 0; pairs && column < pairs->columnCount(); ++column)
		columns += pairs->columnWidth(column);
	// the pairs table scrolls sideways at the opening width
	if (pairs == nullptr)
		QFAIL("the pairs table scrolls sideways at the opening width");
	QCOMPARE_GT(columns, pairs->viewport()->width());
	QCOMPARE_GT(pairs->horizontalScrollBar()->maximum(), 0);
	QCOMPARE_NE(pairs->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
}

// At 1.5 times the font in a short window the body of the dialog scrolls to the panel, which keeps its sections whole,
// and Close stays in view below the body.
void TestCapacityWindow::scrollsBodyWithLargeFont() {
	const QFont original = QApplication::font();
	const auto restoreFont = qScopeGuard([&original] { QApplication::setFont(original); });
	QFont font = original;
	font.setPointSizeF(font.pointSizeF() * 1.5);
	QApplication::setFont(font);
	const CompletedRunData data = lineRun();
	CapacityWindow window(lineResult(data), kContext, QString(), data.sectionLabels);
	window.show();
	window.resize(window.width(), 260);
	QApplication::processEvents();
	auto* scroll = window.findChild<QScrollArea*>("dialogBodyScroll");
	auto* panel = window.findChild<QWidget*>("resultPanel");
	auto* exportButton = window.findChild<QPushButton*>("capacityExport");
	auto* close = window.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Close);
	// the window has its body, panel, export and Close
	if (scroll == nullptr)
		QFAIL("the window has its body, panel, export and Close");
	if (panel == nullptr)
		QFAIL("the window has its body, panel, export and Close");
	if (exportButton == nullptr)
		QFAIL("the window has its body, panel, export and Close");
	if (close == nullptr)
		QFAIL("the window has its body, panel, export and Close");
	// the body scrolls and the panel keeps its height
	QCOMPARE_GT(scroll->verticalScrollBar()->maximum(), 0);
	QCOMPARE_GE(panel->height(), panel->minimumSizeHint().height());
	scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
	QApplication::processEvents();
	const int bottom = exportButton->mapTo(scroll->viewport(), QPoint(0, exportButton->height())).y();
	// the export comes into view at the end of the body and Close stays in the window
	QCOMPARE_LE(bottom, scroll->viewport()->height());
	QVERIFY(close->isVisible());
	QCOMPARE_LE(close->mapTo(&window, QPoint(0, close->height())).y(), window.height());
	const ResultFigure* cycle = figure(window, "capacityCycle");
	const QLabel* value = cycle ? cycle->findChild<QLabel*>("resultFigureValue") : nullptr;
	// the value of a figure is larger than the text
	if (value == nullptr)
		QFAIL("the value of a figure is larger than the text");
	QCOMPARE_GT(value->font().pointSizeF(), font.pointSizeF() * 1.3);
}

// Both dialogs name a route by its first and last station, keep its id and corridor, and name a route without a station
// by its id and corridor. The stations of the route are listed in route order with their blocks as tooltips.
void TestCapacityWindow::namesRoutesByEndStations() {
	const CompletedRunData run = stationRun();
	BlockingTimeScope scope = defaultBlockingTimeScopeFrom(run);
	askBlockingTime(run, scope, [](QDialog& dialog) {
		StretchRows rows = stretchRows(dialog);
		// Blocking time: the dialog has the rows of the stretch
		if (rows.route == nullptr)
			QFAIL("Blocking time: the dialog has the rows of the stretch");
		// Blocking time: the routes by their end stations after All routes
		QCOMPARE(entriesOf(rows.route),
			QStringList({"All routes", "R: Alpha to Delta  (main)", "S  (side)", "T: Echo", "U: Foxtrot to Foxtrot  (loop)"}));
		rows.route->setCurrentIndex(1);
		rows = stretchRows(dialog);
		if (!complete(rows))
			QFAIL("Blocking time: route R lists its stations");
		// Blocking time: route R lists its stations in route order, from the first to the last
		QVERIFY(rows.stations->isChecked());
		QCOMPARE(entriesOf(rows.from), QStringList({"Alpha", "Bravo", "Charlie", "Delta"}));
		QCOMPARE(entriesOf(rows.to), entriesOf(rows.from));
		QCOMPARE(tooltipsOf(rows.from), QStringList({"Block 1", "Blocks 3 to 4", "Block 6", "Block 8"}));
		QCOMPARE(rows.from->currentText(), "Alpha");
		QCOMPARE(rows.to->currentText(), "Delta");
		rows.route->setCurrentIndex(2);
		rows = stretchRows(dialog);
		if (!complete(rows))
			QFAIL("Blocking time: route S lists its blocks");
		// Blocking time: a route without a station lists its blocks and offers no stations
		QVERIFY(rows.blocks->isChecked());
		QVERIFY(!rows.stations->isEnabled());
		QCOMPARE(entriesOf(rows.from).size(), 2);
		rows.route->setCurrentIndex(3);
		rows = stretchRows(dialog);
		// Blocking time: a route of one station lists its blocks too
		QVERIFY(rows.blocks && rows.blocks->isChecked());
		QVERIFY(rows.stations && !rows.stations->isEnabled());
	});
	if (QTest::currentTestFailed())
		return;
	CapacityAnalysisScope capacity;
	askCapacity(run, capacity, [](QDialog& dialog) {
		const StretchRows rows = stretchRows(dialog);
		if (!complete(rows))
			QFAIL("Capacity: the dialog has the rows of the stretch");
		// Capacity: the routes by their end stations, with no All routes, and route R by its stations from the first to
		// the last
		QCOMPARE(entriesOf(rows.route), QStringList({"R: Alpha to Delta  (main)", "S  (side)", "T: Echo", "U: Foxtrot to Foxtrot  (loop)"}));
		QVERIFY(rows.stations->isChecked());
		QCOMPARE(entriesOf(rows.from), QStringList({"Alpha", "Bravo", "Charlie", "Delta"}));
		QCOMPARE(rows.from->currentText(), "Alpha");
		QCOMPARE(rows.to->currentText(), "Delta");
	});
}

// A stretch from one station to another takes every block that lies at least partly between them, the first and the
// last station standing for the ends of the route, and the diagram shows those blocks. A From station after the To
// station moves the To station to it, and a To station before the From station moves the From station.
void TestCapacityWindow::choosesBlocksBetweenStations() {
	const CompletedRunData run = stationRun();
	BlockingTimeScope scope = defaultBlockingTimeScopeFrom(run);
	bool accepted = askBlockingTime(run, scope, [](QDialog& dialog) {
		StretchRows rows = stretchRows(dialog);
		if (rows.route == nullptr)
			QFAIL("Blocking time: the dialog has the rows of the stretch");
		rows.route->setCurrentIndex(1);
		rows = stretchRows(dialog);
		if (!complete(rows))
			QFAIL("Blocking time: route R lists its stations");
		rows.to->setCurrentIndex(2);
		rows.from->setCurrentIndex(3);
		// Blocking time: a From station after the To station moves the To station to it
		QCOMPARE(rows.to->currentText(), "Delta");
		rows.to->setCurrentIndex(0);
		// Blocking time: a To station before the From station moves the From station to it
		QCOMPARE(rows.from->currentText(), "Alpha");
		rows.from->setCurrentIndex(1);
		rows.to->setCurrentIndex(2);
		// Blocking time: the stretch from Bravo to Charlie
		QCOMPARE(rows.from->currentText(), "Bravo");
		QCOMPARE(rows.to->currentText(), "Charlie");
		press(dialog, QDialogButtonBox::Ok);
	});
	// Blocking time: from Bravo, where blocks 3 and 4 meet, to Charlie inside block 6 the blocks 4 to 6
	QVERIFY(accepted);
	QCOMPARE(scope.routeIndex, 0);
	QCOMPARE(scope.sectionIndices, std::vector<int>({3, 4, 5}));
	QCOMPARE(scope.blockIds, std::vector<std::string>({"@3-B0@", "@4-B0@", "@5-B0@"}));
	const std::optional<BlockingTimeDiagramData> diagram = blockingTimeDiagramData(run, scope);
	QStringList shown;
	for (const BlockSectionExtent& extent : diagram ? diagram->extents : std::vector<BlockSectionExtent>())
		shown << QString::fromStdString(extent.id);
	// Blocking time: the diagram shows those blocks
	QCOMPARE(shown, lineIds(3, 5));

	// The pairs of stations and their blocks, with a station to itself giving the blocks that hold it.
	const std::vector<std::pair<std::pair<int, int>, std::vector<int>>> stretches = {{{0, 1}, {0, 1, 2}}, {{1, 1}, {2, 3}},
		{{2, 3}, {5, 6, 7}}, {{0, 3}, {0, 1, 2, 3, 4, 5, 6, 7}}, {{3, 3}, {7}}};
	for (const auto& [stations, blocks] : stretches) {
		CapacityAnalysisScope capacity;
		const int from = stations.first;
		const int to = stations.second;
		accepted = askCapacity(run, capacity, [from, to](QDialog& dialog) {
			const StretchRows rows = stretchRows(dialog);
			if (!complete(rows) || !rows.stations->isChecked())
				QFAIL("Capacity: route R lists its stations");
			rows.to->setCurrentIndex(to);
			rows.from->setCurrentIndex(from);
			press(dialog, QDialogButtonBox::Ok);
		});
		const QString stretch = QString("Capacity: the stretch from station %1 to station %2").arg(from + 1).arg(to + 1);
		QVERIFY2(accepted, qPrintable(stretch));
		QVERIFY2(capacity.sectionIndices == blocks, qPrintable(stretch));
	}
}

// A route that visits a station twice lists both visits, and each visit gives its own blocks.
void TestCapacityWindow::choosesStationVisitedTwice() {
	const CompletedRunData run = stationRun();
	// From Golf to the second Foxtrot, from the second Foxtrot to itself and from the first Foxtrot to itself.
	const std::vector<std::pair<std::pair<int, int>, std::vector<int>>> stretches = {
		{{1, 2}, {2, 3}}, {{2, 2}, {3}}, {{0, 0}, {0}}};
	for (const auto& [stations, blocks] : stretches) {
		BlockingTimeScope scope = defaultBlockingTimeScopeFrom(run);
		const int from = stations.first;
		const int to = stations.second;
		const bool accepted = askBlockingTime(run, scope, [from, to](QDialog& dialog) {
			StretchRows rows = stretchRows(dialog);
			if (rows.route == nullptr)
				QFAIL("Blocking time: the dialog has the rows of the stretch");
			rows.route->setCurrentIndex(4);
			rows = stretchRows(dialog);
			if (!complete(rows) || !rows.stations->isChecked())
				QFAIL("Blocking time: route U lists its stations");
			// Blocking time: route U lists Foxtrot at both visits, each with the block that holds it
			QCOMPARE(entriesOf(rows.from), QStringList({"Foxtrot", "Golf", "Foxtrot"}));
			QCOMPARE(entriesOf(rows.to), entriesOf(rows.from));
			QCOMPARE(tooltipsOf(rows.from), QStringList({"Block 1", "Blocks 2 to 3", "Block 4"}));
			rows.to->setCurrentIndex(to);
			rows.from->setCurrentIndex(from);
			QCOMPARE(rows.from->currentIndex(), from);
			QCOMPARE(rows.to->currentIndex(), to);
			press(dialog, QDialogButtonBox::Ok);
		});
		const QString stretch = QString("Blocking time: the stretch from station %1 to station %2 of route U").arg(from + 1).arg(to + 1);
		QVERIFY2(accepted, qPrintable(stretch));
		QVERIFY2(scope.routeIndex == 3, qPrintable(stretch));
		QVERIFY2(scope.sectionIndices == blocks, qPrintable(stretch));
	}
}

// Blocks shows the blocks of the stations, and Stations shows the smallest stretch between stations that holds the blocks.
void TestCapacityWindow::switchesBetweenStationsAndBlocks() {
	const CompletedRunData run = stationRun();
	CapacityAnalysisScope capacity;
	const bool accepted = askCapacity(run, capacity, [](QDialog& dialog) {
		StretchRows rows = stretchRows(dialog);
		if (!complete(rows))
			QFAIL("Capacity: route R lists its stations");
		rows.from->setCurrentIndex(1);
		rows.to->setCurrentIndex(2);
		rows.blocks->click();
		rows = stretchRows(dialog);
		// Capacity: Blocks lists the blocks with the blocks of Bravo to Charlie chosen
		QVERIFY(rows.blocks->isChecked());
		QCOMPARE(rows.from->count(), 8);
		QCOMPARE(rows.from->currentIndex(), 3);
		QCOMPARE(rows.to->currentIndex(), 5);
		rows.from->setCurrentIndex(4);
		rows.to->setCurrentIndex(4);
		rows.stations->click();
		rows = stretchRows(dialog);
		// Capacity: Stations shows Bravo to Charlie for block 5
		QVERIFY(rows.stations->isChecked());
		QCOMPARE(rows.from->currentText(), "Bravo");
		QCOMPARE(rows.to->currentText(), "Charlie");
		rows.blocks->click();
		rows = stretchRows(dialog);
		// Capacity: Blocks shows the blocks of that stretch again
		QCOMPARE(rows.from->currentIndex(), 3);
		QCOMPARE(rows.to->currentIndex(), 5);
		rows.to->setCurrentIndex(6);
		press(dialog, QDialogButtonBox::Ok);
	});
	// Capacity: the blocks chosen last
	QVERIFY(accepted);
	QCOMPARE(capacity.sectionIndices, std::vector<int>({3, 4, 5, 6}));
}

// A scope of blocks opens on its route and blocks: by stations when two stations give exactly its blocks, else by blocks.
void TestCapacityWindow::opensScopeOfBlocks() {
	const CompletedRunData run = stationRun();
	BlockingTimeScope scope = defaultBlockingTimeScopeFrom(run);
	scope.routeIndex = 0;
	scope.sectionIndices = {3, 4, 5};
	scope.blockIds = {"@3-B0@", "@4-B0@", "@5-B0@"};
	bool accepted = askBlockingTime(run, scope, [](QDialog& dialog) {
		const StretchRows rows = stretchRows(dialog);
		if (!complete(rows))
			QFAIL("Blocking time: the dialog has the rows of the stretch");
		// Blocking time: blocks 4 to 6 open as the stretch from Bravo to Charlie of route R
		QCOMPARE(rows.route->currentText(), "R: Alpha to Delta  (main)");
		QVERIFY(rows.stations->isChecked());
		QCOMPARE(rows.from->currentText(), "Bravo");
		QCOMPARE(rows.to->currentText(), "Charlie");
		press(dialog, QDialogButtonBox::Ok);
	});
	// Blocking time: the scope keeps its blocks
	QVERIFY(accepted);
	QCOMPARE(scope.sectionIndices, std::vector<int>({3, 4, 5}));
	scope.sectionIndices = {4};
	scope.blockIds = {"@4-B0@"};
	accepted = askBlockingTime(run, scope, [](QDialog& dialog) {
		const StretchRows rows = stretchRows(dialog);
		if (!complete(rows))
			QFAIL("Blocking time: the dialog has the rows of the stretch");
		// Blocking time: block 5 alone opens as that block
		QVERIFY(rows.blocks->isChecked());
		QCOMPARE(rows.from->currentIndex(), 4);
		QCOMPARE(rows.to->currentIndex(), 4);
		press(dialog, QDialogButtonBox::Ok);
	});
	QVERIFY(accepted);
	QCOMPARE(scope.sectionIndices, std::vector<int>({4}));

	CapacityAnalysisScope capacity = capacityRangeScopeFrom(run, 0, 5, 7);
	accepted = askCapacity(run, capacity, [](QDialog& dialog) {
		const StretchRows rows = stretchRows(dialog);
		if (!complete(rows))
			QFAIL("Capacity: the dialog has the rows of the stretch");
		// Capacity: blocks 6 to 8 open as the stretch from Charlie to Delta
		QVERIFY(rows.stations->isChecked());
		QCOMPARE(rows.from->currentText(), "Charlie");
		QCOMPARE(rows.to->currentText(), "Delta");
		press(dialog, QDialogButtonBox::Ok);
	});
	QVERIFY(accepted);
	QCOMPARE(capacity.sectionIndices, std::vector<int>({5, 6, 7}));
	capacity = capacityRangeScopeFrom(run, 0, 4, 6);
	accepted = askCapacity(run, capacity, [](QDialog& dialog) {
		const StretchRows rows = stretchRows(dialog);
		if (!complete(rows))
			QFAIL("Capacity: the dialog has the rows of the stretch");
		// Capacity: blocks 5 to 7 open as blocks
		QVERIFY(rows.blocks->isChecked());
		QCOMPARE(rows.from->currentIndex(), 4);
		QCOMPARE(rows.to->currentIndex(), 6);
		press(dialog, QDialogButtonBox::Ok);
	});
	QVERIFY(accepted);
	QCOMPARE(capacity.sectionIndices, std::vector<int>({4, 5, 6}));
}

// The interval reads and is typed as the clock times of the time axis, and gives the seconds of the run at those times,
// after midnight too.
void TestCapacityWindow::entersClockTimes() {
	CompletedRunData run = stationRun();
	const auto type = [](QDoubleSpinBox* field, const QString& text) {
		auto* edit = field->findChild<QLineEdit*>();
		if (edit == nullptr)
			return;
		edit->selectAll();
		QTest::keyClicks(edit, text);
		field->interpretText();
	};
	BlockingTimeScope scope = defaultBlockingTimeScopeFrom(run);
	bool accepted = askBlockingTime(run, scope, [&type](QDialog& dialog) {
		auto* from = fieldOf<QDoubleSpinBox>(dialog, "From time:");
		auto* to = fieldOf<QDoubleSpinBox>(dialog, "To time:");
		if (!from || !to)
			QFAIL("Blocking time: the dialog asks for the interval as clock times");
		// Blocking time: the interval opens on the run, from 07:00:00 to 08:00:00
		QCOMPARE(from->text(), "07:00:00");
		QCOMPARE(to->text(), "08:00:00");
		type(from, "07:01:40");
		type(to, "07:30");
		// Blocking time: typed clock times give the seconds of the run
		QCOMPARE(from->value(), 100.0);
		QCOMPARE(to->value(), 1800.0);
		QCOMPARE(to->text(), "07:30:00");
		type(to, "25:00");
		// Blocking time: a time that is no clock time keeps the time before
		QCOMPARE(to->value(), 1800.0);
		type(to, "06:00");
		// Blocking time: a clock time outside the run keeps the time before
		QCOMPARE(to->value(), 1800.0);
		press(dialog, QDialogButtonBox::Ok);
	});
	QVERIFY(accepted);
	QCOMPARE(scope.startTime, 100.0);
	QCOMPARE(scope.endTime, 1800.0);

	run.settings.startOffsetSeconds = 23 * 3600 + 50 * 60;
	run.settings.horizonSteps = 1800.0;
	scope = defaultBlockingTimeScopeFrom(run);
	accepted = askBlockingTime(run, scope, [&type](QDialog& dialog) {
		auto* from = fieldOf<QDoubleSpinBox>(dialog, "From time:");
		auto* to = fieldOf<QDoubleSpinBox>(dialog, "To time:");
		if (!from || !to)
			QFAIL("Blocking time: the dialog asks for the interval as clock times");
		// Blocking time: a run from 23:50:00 ends at 00:20:00, as the time axis shows it
		QCOMPARE(from->text(), QString::fromStdString(formatSimTime(0, 23 * 3600 + 50 * 60)));
		QCOMPARE(to->text(), "00:20:00");
		type(from, "23:55");
		type(to, "00:05:00");
		// Blocking time: 00:05:00 is the time after midnight
		QCOMPARE(from->value(), 300.0);
		QCOMPARE(to->value(), 900.0);
		press(dialog, QDialogButtonBox::Ok);
	});
	QVERIFY(accepted);
	QCOMPARE(scope.startTime, 300.0);
	QCOMPARE(scope.endTime, 900.0);
}

// The stretch signals another route or stretch, and the entry under the pointer, with the route id and the section ids.
void TestCapacityWindow::signalsStretchAndHover() {
	const CompletedRunData run = stationRun();
	BlockingTimeScope scope = defaultBlockingTimeScopeFrom(run);
	askBlockingTime(run, scope, [](QDialog& dialog) {
		QObject* stretch = stretchSignals(dialog);
		StretchRows rows = stretchRows(dialog);
		if (stretch == nullptr || rows.route == nullptr)
			QFAIL("Blocking time: the stretch has its signals");
		QSignalSpy changed(stretch, SIGNAL(stretchChanged(QString, QStringList)));
		QSignalSpy hovered(stretch, SIGNAL(entryHovered(QString, QStringList, QStringList)));
		rows.route->setCurrentIndex(1);
		// Blocking time: another route sends its id and all its blocks
		QCOMPARE(changed.count(), 1);
		QCOMPARE(changed.last(), QVariantList({"R", lineIds(0, 7)}));
		rows = stretchRows(dialog);
		if (!complete(rows))
			QFAIL("Blocking time: route R lists its stations");
		rows.to->setCurrentIndex(2);
		// Blocking time: another To station sends the blocks of the stretch
		QCOMPARE(changed.count(), 2);
		QCOMPARE(changed.last(), QVariantList({"R", lineIds(0, 5)}));
		// Blocking time: choosing an entry is no hover
		QCOMPARE(hovered.count(), 0);
		const QVariantList station = hoverEntry(rows.from, 1, hovered);
		// Blocking time: a station under the pointer sends the blocks that hold it
		QCOMPARE_GT(hovered.count(), 0);
		QCOMPARE(station, QVariantList({"R", lineIds(0, 5), lineIds(2, 3)}));
		// Blocking time: the list that closes leaves the stretch without an entry
		QCOMPARE(hovered.last(), QVariantList({"R", lineIds(0, 5), QStringList()}));
		// Blocking time: a route under the pointer sends its blocks and the chosen stretch
		QCOMPARE(hoverEntry(rows.route, 2, hovered), QVariantList({"R", lineIds(0, 5), QStringList({"@0-S@", "@1-S@"})}));
		// Blocking time: All routes under the pointer sends no block
		QCOMPARE(hoverEntry(rows.route, 0, hovered), QVariantList({"R", lineIds(0, 5), QStringList()}));
		rows.blocks->click();
		rows = stretchRows(dialog);
		// Blocking time: Blocks keeps the stretch and sends nothing, since the blocks are the same
		QVERIFY(rows.blocks->isChecked());
		QCOMPARE(rows.from->currentIndex(), 0);
		QCOMPARE(rows.to->currentIndex(), 5);
		QCOMPARE(changed.count(), 2);
		// Blocking time: a block under the pointer sends that block
		QCOMPARE(hoverEntry(rows.to, 4, hovered), QVariantList({"R", lineIds(0, 5), lineIds(4, 4)}));
		QCOMPARE(hovered.last(), QVariantList({"R", lineIds(0, 5), QStringList()}));
		rows.route->setCurrentIndex(0);
		// Blocking time: All routes sends no route and no block
		QCOMPARE(changed.count(), 3);
		QCOMPARE(changed.last(), QVariantList({QString(), QStringList()}));
	});
	if (QTest::currentTestFailed())
		return;
	CapacityAnalysisScope capacity;
	const bool accepted = askCapacity(run, capacity, [](QDialog& dialog) {
		QObject* stretch = stretchSignals(dialog);
		StretchRows rows = stretchRows(dialog);
		auto* trains = dialog.findChild<QListWidget*>();
		if (stretch == nullptr || !complete(rows) || trains == nullptr)
			QFAIL("Capacity: the stretch has its signals");
		QSignalSpy changed(stretch, SIGNAL(stretchChanged(QString, QStringList)));
		QSignalSpy hovered(stretch, SIGNAL(entryHovered(QString, QStringList, QStringList)));
		rows.from->setCurrentIndex(2);
		// Capacity: another From station sends the blocks of the stretch
		QCOMPARE(changed.count(), 1);
		QCOMPARE(changed.last(), QVariantList({"R", lineIds(5, 7)}));
		// Capacity: a station under the pointer sends the blocks that hold it, and the list that closes no entry
		QCOMPARE(hoverEntry(rows.to, 3, hovered), QVariantList({"R", lineIds(5, 7), lineIds(7, 7)}));
		QCOMPARE(hovered.last(), QVariantList({"R", lineIds(5, 7), QStringList()}));
		const auto order = [trains]() {
			QStringList ids;
			for (int row = 0; row < trains->count(); ++row)
				ids << trains->item(row)->data(Qt::UserRole).toString();
			return ids;
		};
		QCOMPARE(order(), QStringList({"F1", "F2"}));
		QVERIFY2(trains->model()->moveRow(QModelIndex(), 1, QModelIndex(), 0), "F2 moves above F1");
		rows.blocks->click();
		rows = stretchRows(dialog);
		// Capacity: Blocks with the blocks of Charlie to Delta sends nothing and keeps the order of the trains
		QVERIFY(rows.blocks->isChecked());
		QCOMPARE(rows.from->currentIndex(), 5);
		QCOMPARE(rows.to->currentIndex(), 7);
		QCOMPARE(changed.count(), 1);
		QCOMPARE(order(), QStringList({"F2", "F1"}));
		rows.stations->click();
		rows = stretchRows(dialog);
		// Capacity: Stations with the same blocks sends nothing and keeps the order of the trains
		QVERIFY(rows.stations->isChecked());
		QCOMPARE(rows.from->currentText(), "Charlie");
		QCOMPARE(rows.to->currentText(), "Delta");
		QCOMPARE(changed.count(), 1);
		QCOMPARE(order(), QStringList({"F2", "F1"}));
		press(dialog, QDialogButtonBox::Ok);
	});
	// Capacity: the analysis takes the trains in the order that the user set
	QVERIFY(accepted);
	QCOMPARE(capacity.occurrenceIds, std::vector<std::string>({"F2", "F1"}));
}

QTEST_MAIN(TestCapacityWindow)
#include "test_capacitywindow.moc"
