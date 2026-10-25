// Tests of the Capacity window with the worked example of docs/guides/capacity-analysis.md: the line F1, F2 of
// test_areacapacitywindow.cpp, eight blocks, F1 then F2, profile references 59 s and 199 s, authored departures 120 s and
// 180 s, analysed over 3600 s with F2 closing the cycle. The guide works out a cycle of 471.5 s, 13.1 % of the hour, with
// the critical block @3-B0@. Times are in seconds. The test shows no message box and opens no file chooser.

#include "diagrams/CapacityScope.h"
#include "diagrams/CapacityWindow.h"
#include "diagrams/ResultFrame.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFont>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>
#include <QToolButton>

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
};

void TestCapacityWindow::initTestCase() {
	QFile stylesheet(QStringLiteral(EGTRAIN_DIALOG_QSS));
	QVERIFY2(stylesheet.open(QIODevice::ReadOnly), qPrintable("cannot read " + stylesheet.fileName()));
	qApp->setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
}

// The panel holds the figures of the worked example, Open Diagram and the export, and no paragraph explains the cycle.
void TestCapacityWindow::showsWorkedExampleInPanel() {
	const CompletedRunData data = lineRun();
	CapacityWindow window(lineResult(data), kContext, "First block 1: @0-B0@\nLast block 8: @7-B0@", data.sectionLabels);
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
	QVERIFY(context->toolTip().contains("Last block 8: @7-B0@"));
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

QTEST_MAIN(TestCapacityWindow)
#include "test_capacitywindow.moc"
