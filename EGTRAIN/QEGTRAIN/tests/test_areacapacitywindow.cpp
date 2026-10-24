// Tests of the area capacity window, its options dialog and its export. Every value is synthetic. The
// numbers of the line F1, F2 were copied once from the made-up line of the committed test fixture, as in
// test_areacapacity.cpp: eight blocks, F1 then F2, profile references 59 s and 199 s, authored departures
// 120 s and 180 s. Times are in seconds and percentages in percent. The test shows no message box, and
// opens the file chooser only to check that the window stays in front after it.

#include "diagrams/AreaCapacityWindow.h"
#include "diagrams/ResultFrame.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

BlockingTimeDiagramInput occupation(const char* id, double start, double end, double runStart = -1.0, bool complete = true) {
	BlockingTimeDiagramInput value;
	value.blockId = id;
	value.startOccTime = start;
	value.endOccTime = end;
	value.posStart = 0.0;
	value.posEnd = 100.0;
	value.isComplete = complete;
	value.endClearTime = end;
	value.startRunTime = runStart;
	return value;
}

CompletedTrain train(const char* id, int route, std::vector<BlockingTimeDiagramInput> blockTimes, double authoredDeparture = -1.0) {
	CompletedTrain value;
	value.trainId = id;
	value.operatingCode = id;
	value.routeIndex = route;
	value.blockTimes = std::move(blockTimes);
	if (authoredDeparture >= 0.0) {
		// A stop at the start of the first block gives the authored reference time.
		CompletedStop stop;
		stop.station = "A";
		stop.routePositionMeters = 0.0;
		stop.scheduledDeparture = authoredDeparture;
		stop.scheduledArrival = -1.0;
		value.stops.push_back(stop);
	}
	return value;
}

CompletedRoute route(const char* id, bool reversed, std::initializer_list<const char*> blockIds) {
	CompletedRoute value;
	value.id = id;
	value.reversedDirection = reversed;
	double km = 0.0;
	for (const char* blockId : blockIds) {
		value.blocks.push_back({blockId, km, km + 1.0});
		km += 1.0;
	}
	return value;
}

CompletedTrain firstTrain() {
	return train("F1", 0,
		{occupation("@0-B0@", 59.0, 146.0, 59.0), occupation("@1-B0@", 53.5, 201.0), occupation("@2-B0@", 135.5, 257.0),
			occupation("@3-B0@", 190.5, 674.0), occupation("@4-B0@", 246.5, 748.0), occupation("@5-B0@", 654.5, 803.0),
			occupation("@6-B0@", 737.5, 859.0), occupation("@7-B0@", 792.5, 983.0)},
		120.0);
}

CompletedTrain secondTrain() {
	return train("F2", 0,
		{occupation("@0-B0@", 199.0, 286.0, 199.0), occupation("@1-B0@", 193.5, 357.0), occupation("@2-B0@", 275.5, 686.0),
			occupation("@3-B0@", 342.5, 892.0), occupation("@4-B0@", 666.5, 966.0), occupation("@5-B0@", 872.5, 1021.0),
			occupation("@6-B0@", 955.5, 1077.0), occupation("@7-B0@", 1010.5, 1201.0)},
		180.0);
}

// Route R holds the eight blocks. Route S runs the other way over the last two. The run lists F2 before F1.
// G runs on S and has no occupation of the first block of R. Z uses no block of R or S.
CompletedRunData lineRun() {
	CompletedRunData data;
	data.routes = {route("R", false, {"@0-B0@", "@1-B0@", "@2-B0@", "@3-B0@", "@4-B0@", "@5-B0@", "@6-B0@", "@7-B0@"}),
		route("S", true, {"@7-B0@", "@6-B0@"})};
	data.routes[0].corridor = "main";
	data.trains = {secondTrain(), firstTrain(),
		train("G", 1, {occupation("@7-B0@", 100.0, 160.0), occupation("@6-B0@", 160.0, 220.0)}),
		train("Z", 0, {occupation("@99-B0@", 10.0, 20.0)})};
	data.provenance.caseName = "Case A";
	data.provenance.appliedScenario = "Scenario B";
	return data;
}

AreaCapacityScope lineScope(std::vector<std::string> occurrences) {
	AreaCapacityScope scope;
	scope.routeIndex = 0;
	scope.blockIds = {"@0-B0@", "@1-B0@", "@2-B0@", "@3-B0@", "@4-B0@", "@5-B0@", "@6-B0@", "@7-B0@"};
	scope.sectionIndices = {0, 1, 2, 3, 4, 5, 6, 7};
	scope.occurrenceIds = std::move(occurrences);
	scope.cycleEndOccurrenceId = "F2";
	return scope;
}

AreaCapacityResult analyze(const CompletedRunData& data, const AreaCapacityScope& scope) {
	return analyzeAreaCapacity(areaCapacityInputFrom(data, scope));
}

AreaCapacityWindow makeWindow(const AreaCapacityResult& result) {
	return AreaCapacityWindow(result, "R / main / @0-B0@ to @7-B0@", lineRun().provenance);
}

// The text of a key figure of the panel: its value and its caption.
QString figure(const AreaCapacityWindow& window, const char* name) {
	const auto* shown = window.findChild<ResultFigure*>(name);
	return shown && window.windowTitle() == "Capacity by area" ? shown->value() + " | " + shown->caption() : QStringLiteral("missing");
}

// The figures of the cycle of the chain and of its share of the period, which say why they are not available.
QString cycleFigures(const AreaCapacityWindow& window) {
	return figure(window, "areaCapacityCycle") + ", " + figure(window, "areaCapacityShare");
}

// The context of the header.
QString context(const AreaCapacityWindow& window) {
	const auto* label = window.findChild<QLabel*>("areaCapacityContext");
	return label ? label->text() : QString();
}

QTableWidget* table(const AreaCapacityWindow& window, const char* name) {
	return window.findChild<QTableWidget*>(name);
}

// The text of the cell, or an empty text when the cell is not there.
QString cell(const QTableWidget* tableWidget, int row, int column) {
	return tableWidget && tableWidget->item(row, column) ? tableWidget->item(row, column)->text() : QString();
}

// The row whose first column is the text, or -1.
int rowOf(const QTableWidget* tableWidget, const QString& text) {
	for (int row = 0; tableWidget && row < tableWidget->rowCount(); ++row)
		if (cell(tableWidget, row, 0) == text)
			return row;
	return -1;
}

QString tabTitle(const AreaCapacityWindow& window, int index) {
	const auto* tabs = window.findChild<QTabWidget*>("areaCapacityTabs");
	return tabs ? tabs->tabText(index) : QString();
}

// Every text that the window shows: headings, labels, tab titles and cells.
QString allText(const AreaCapacityWindow& window) {
	QString text;
	for (const QLabel* label : window.findChildren<QLabel*>())
		text += label->text() + "\n";
	for (const QTableWidget* tableWidget : window.findChildren<QTableWidget*>()) {
		for (int row = 0; row < tableWidget->rowCount(); ++row)
			for (int column = 0; column < tableWidget->columnCount(); ++column)
				text += cell(tableWidget, row, column) + "\n";
	}
	const auto* tabs = window.findChild<QTabWidget*>("areaCapacityTabs");
	for (int index = 0; tabs && index < tabs->count(); ++index)
		text += tabs->tabText(index) + "\n";
	return text;
}

bool exerciseInput() {
	const CompletedRunData data = lineRun();
	AreaCapacityScope scope = lineScope({"F2", "F1"});
	scope.secondRouteIndex = 1;
	scope.secondBlockIds = {"@7-B0@", "@6-B0@"};
	const AreaCapacityInput input = areaCapacityInputFrom(data, scope);
	bool ok = expect(input.resourceIds.size() == 10 && input.resourceIds[7] == "@7-B0@" && input.resourceIds[8] == "@7-B0@",
		"the resources are the range of the first route followed by the range of the second");
	ok &= expect(input.selected.size() == 2 && input.selected[0].train.runtimeId == "F2" && input.selected[1].train.runtimeId == "F1",
		"the selected trains keep the order of the scope");
	ok &= expect(input.otherTraffic.size() == 2 && input.otherTraffic[0].train.runtimeId == "G" && input.otherTraffic[1].train.runtimeId == "Z",
		"every train that is not selected is other traffic");
	ok &= expect(input.periodSeconds == 3600.0 && input.cycleEndIdentity == "F2", "the period and the cycle-closing train are passed on");
	const AreaCapacityTrain& first = input.selected[1];
	ok &= expect(first.train.profileReferenceTime == 59.0 && first.train.scheduledReferenceTime == 120.0 && first.train.occupations.size() == 8,
		"the reference times come from the first block of the first route and all occupations are passed");
	ok &= expect(!first.reversedDirection && input.otherTraffic[0].reversedDirection, "the direction is the one of the route of the train");
	ok &= expect(input.otherTraffic[0].train.profileReferenceTime < 0.0, "a train without the first block has no reference");
	ok &= expect(areaCapacityLabel(data, scope).toStdString() == "R / main / @0-B0@ to @7-B0@ and S / @7-B0@ to @6-B0@",
		"the label names both route ranges");
	ok &= expect(areaCapacityLabel(data, lineScope({})).toStdString() == "R / main / @0-B0@ to @7-B0@", "the label of one route range");

	// An occupation that is not complete is passed on and reported, not dropped.
	CompletedRunData incomplete = data;
	incomplete.trains[0].blockTimes[2] = occupation("@2-B0@", 275.5, 686.0, -1.0, false);
	const AreaCapacityResult result = analyze(incomplete, lineScope({"F1", "F2"}));
	ok &= expect(!result.complete
			&& std::any_of(result.notes.begin(), result.notes.end(),
				[](const AreaCapacityNote& note) {
					return note.kind == AreaCapacityNoteKind::IncompleteOccupation && note.trainId == "F2" && note.resourceId == "@2-B0@";
				}),
		"an incomplete occupation of the run is reported");
	return ok;
}

bool exerciseResult() {
	const CompletedRunData data = lineRun();
	// F1 and F2 selected: G uses @6 and @7 and is not selected, so the traffic is partial.
	AreaCapacityWindow partial = makeWindow(analyze(data, lineScope({"F1", "F2"})));
	partial.show();
	QApplication::processEvents();
	bool ok = expect(cycleFigures(partial) == "471.5 s | Cycle, 13.1% | of 1 h" && figure(partial, "areaCapacityGoverning") == "28.7% | Governing: 3-B0",
		qPrintable("the panel gives the governing rate, the cycle and its share: " + figure(partial, "areaCapacityGoverning") + ", "
			+ cycleFigures(partial)));
	const QTableWidget* resources = table(partial, "areaCapacityResources");
	ok &= expect(resources && resources->rowCount() == 8, "one row for each resource of the area");
	const int governing = rowOf(resources, "3-B0");
	// F1 on @3 is 190.5 to 674 (483.5 s), F2 342.5 to 892 (549.5 s): sum 1033 s, union 190.5 to 892 (701.5 s), 1033 / 3600 = 28.7 percent.
	ok &= expect(governing >= 0 && cell(resources, governing, 1) == "28.7" && cell(resources, governing, 2) == "governing resource"
			&& cell(resources, governing, 3) == "2" && cell(resources, governing, 4) == "2" && cell(resources, governing, 5) == "1033"
			&& cell(resources, governing, 6) == "701.5",
		"the resource with the largest occupation is marked as governing");
	int governingRows = 0;
	for (int row = 0; resources && row < resources->rowCount(); ++row)
		governingRows += cell(resources, row, 2) == "governing resource" ? 1 : 0;
	ok &= expect(governingRows == 1 && resources->item(governing, 0)->font().bold(), "exactly one row is governing and it is bold");
	// The envelope is the latest release 1201 minus the earliest occupation 53.5: 1147.5 s, 31.9 percent.
	ok &= expect(!partial.findChild<QLabel*>("areaCapacityEnvelope") && !allText(partial).contains("envelope", Qt::CaseInsensitive),
		"the legacy envelope stays out of the window");
	const QTableWidget* pairs = table(partial, "areaCapacityPairs");
	ok &= expect(pairs && pairs->rowCount() == 1 && cell(pairs, 0, 0) == "F1" && cell(pairs, 0, 1) == "F2" && cell(pairs, 0, 5) == "471.5",
		"the chain tab holds the pair with the minimum headway");
	ok &= expect(pairs && pairs->columnCount() == 8 && pairs->item(0, 0)->toolTip() == "Reference time: planned departure at A"
			&& pairs->horizontalHeaderItem(5)->text() == "Minimum headway (s)",
		"the reference times of a pair are tooltips of its trains, not a column");
	const QTableWidget* excluded = table(partial, "areaCapacityExcluded");
	ok &= expect(excluded && excluded->rowCount() == 1 && cell(excluded, 0, 0) == "Unselected traffic" && cell(excluded, 0, 1) == "G",
		"the excluded traffic tab lists the unselected train");
	ok &= expect(tabTitle(partial, 0) == "Resources" && tabTitle(partial, 1) == "Chain" && tabTitle(partial, 2) == "Excluded traffic (1)",
		"the tabs");
	const auto* statuses = partial.findChild<QLabel*>("areaCapacityStatuses");
	ok &= expect(statuses && statuses->text() == "Statuses: Unselected traffic (1)", "the statuses line");
	const auto* partialContext = partial.findChild<QLabel*>("areaCapacityContext");
	ok &= expect(context(partial) == "Case A / Scenario B \u00b7 R / main / @0-B0@ to @7-B0@ \u00b7 partial traffic" && partialContext
			&& partialContext->toolTip().contains("Rates count only the checked trains") && !partial.findChild<QLabel*>("dialogContext"),
		qPrintable("the header names the run, the area and the partial traffic: " + context(partial)));

	// All three trains selected: G runs the other way, so it counts in the rows and not in the chain.
	AreaCapacityWindow complete = makeWindow(analyze(data, lineScope({"F1", "F2", "G"})));
	complete.show();
	QApplication::processEvents();
	ok &= expect(cycleFigures(complete) == "471.5 s | Cycle, 13.1% | of 1 h" && context(complete) == "Case A / Scenario B \u00b7 R / main / @0-B0@ to @7-B0@",
		"the header of complete traffic, which says nothing about the traffic");
	const QTableWidget* completeResources = table(complete, "areaCapacityResources");
	const int sixth = rowOf(completeResources, "6-B0");
	ok &= expect(sixth >= 0 && cell(completeResources, sixth, 3) == "3" && cell(completeResources, sixth, 5) == "303",
		"an opposing train counts in the rows of the resources");
	const QTableWidget* completeExcluded = table(complete, "areaCapacityExcluded");
	ok &= expect(completeExcluded && completeExcluded->rowCount() == 1 && cell(completeExcluded, 0, 0) == "Opposing train",
		"an opposing train is listed as excluded from the chain");

	// A resource that no selected train uses stays in the table and says so.
	AreaCapacityScope idle = lineScope({"F1", "F2"});
	idle.blockIds.push_back("@8-B0@");
	AreaCapacityWindow unused = makeWindow(analyze(data, idle));
	const QTableWidget* unusedResources = table(unused, "areaCapacityResources");
	const int last = rowOf(unusedResources, "8-B0");
	ok &= expect(last >= 0 && cell(unusedResources, last, 2) == "no traffic" && cell(unusedResources, last, 3) == "0"
			&& cell(unusedResources, last, 5) == "0",
		"a resource without traffic is shown as 0 with no traffic");

	const QString text = allText(partial) + allText(complete);
	ok &= expect(!text.contains("per hour", Qt::CaseInsensitive) && !text.contains("tph", Qt::CaseInsensitive)
			&& !text.contains("trains/h", Qt::CaseInsensitive),
		"no trains per hour figure is shown");
	return ok;
}

bool exerciseStatuses() {
	const AreaCapacityNoteKind kinds[] = {AreaCapacityNoteKind::EmptyScope, AreaCapacityNoteKind::InvalidPeriod,
		AreaCapacityNoteKind::DuplicateIdentity, AreaCapacityNoteKind::NoSelectedTraffic, AreaCapacityNoteKind::IncompleteOccupation,
		AreaCapacityNoteKind::RepeatedResource, AreaCapacityNoteKind::OpposingTrain, AreaCapacityNoteKind::InvalidReference,
		AreaCapacityNoteKind::FewerThanTwoTrains, AreaCapacityNoteKind::NoSharedResource, AreaCapacityNoteKind::ChainConflict,
		AreaCapacityNoteKind::UnselectedTraffic};
	AreaCapacityResult result;
	for (const AreaCapacityNoteKind kind : kinds)
		result.notes.push_back({kind, "T1", "T2", "X"});
	AreaCapacityWindow window = makeWindow(result);
	window.show();
	QApplication::processEvents();
	bool ok = expect(cycleFigures(window) == "- | Chain cannot be analysed, - | No valid period"
			&& figure(window, "areaCapacityGoverning") == "- | No governing resource",
		qPrintable("the figures without a chain: " + cycleFigures(window)));
	const QTableWidget* notes = table(window, "areaCapacityNotes");
	ok &= expect(notes && notes->rowCount() == 12 && tabTitle(window, 3) == "Notes (12)", "a row for every note");
	const auto* statuses = window.findChild<QLabel*>("areaCapacityStatuses");
	for (int index = 0; index < 12; ++index) {
		const QString name = areaCapacityStatusName(kinds[index]);
		ok &= expect(!name.isEmpty() && cell(notes, index, 0) == name, "the notes table shows the status name");
		ok &= expect(statuses && statuses->text().contains(name + " (1)"), "the statuses line names every status");
		ok &= expect(!areaCapacityStatusText({kinds[index], "T1", "T2", "X"}).isEmpty(), "every status has a meaning");
	}
	const QTableWidget* excluded = table(window, "areaCapacityExcluded");
	ok &= expect(excluded && excluded->rowCount() == 4, "four statuses concern traffic that the figures leave out");
	const auto* exportButton = window.findChild<QPushButton*>("areaCapacityExport");
	ok &= expect(exportButton && !exportButton->isEnabled() && !exportButton->autoDefault(), "nothing to export without a resource row");
	return ok;
}

void pressTab(QWidget* widget) {
	QKeyEvent press(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
	QKeyEvent release(QEvent::KeyRelease, Qt::Key_Tab, Qt::NoModifier);
	QApplication::sendEvent(widget, &press);
	QApplication::sendEvent(widget, &release);
}

// The panel holds the figures and the export. The statuses lie above the tabs with the look of a warning, the tables
// scroll sideways beside a panel of 200 px, and Tab goes through the header, the tabs and the panel.
bool exercisePanel() {
	const CompletedRunData data = lineRun();
	AreaCapacityWindow window = makeWindow(analyze(data, lineScope({"F1", "F2"})));
	window.show();
	QApplication::processEvents();
	auto* panel = window.findChild<QWidget*>("resultPanel");
	auto* statuses = window.findChild<QLabel*>("areaCapacityStatuses");
	auto* tabs = window.findChild<QTabWidget*>("areaCapacityTabs");
	auto* exportButton = window.findChild<QPushButton*>("areaCapacityExport");
	if (!expect(panel && statuses && tabs && exportButton, "the window has a panel, statuses, tabs and an export"))
		return false;
	bool inPanel = panel->isAncestorOf(exportButton);
	for (const char* name : {"areaCapacityGoverning", "areaCapacityCycle", "areaCapacityShare"})
		inPanel = inPanel && window.findChild<ResultFigure*>(name) && panel->isAncestorOf(window.findChild<ResultFigure*>(name));
	bool ok = expect(inPanel && !panel->isAncestorOf(tabs), "the figures and the export lie in the panel, beside the tabs");
	ok &= expect(exportButton->text() == "CSV..." && exportButton->accessibleName() == "Export CSV", "the export reads CSV...");
	QStringList headings;
	for (const QLabel* label : panel->findChildren<QLabel*>())
		if (label->property("resultSectionHeading").toBool() && label->isVisible())
			headings << label->text();
	ok &= expect(headings == QStringList({"Export"}), qPrintable("only the export has a heading: " + headings.join(", ")));
	ok &= expect(statuses->isVisible() && statuses->property("dialogStatus").toString() == "warning" && !panel->isAncestorOf(statuses)
			&& statuses->mapTo(&window, QPoint(0, statuses->height())).y() <= tabs->mapTo(&window, QPoint()).y(),
		"the statuses keep the look of a warning above the tabs");

	const QTableWidget* resources = table(window, "areaCapacityResources");
	int columns = 0;
	for (int column = 0; column < resources->columnCount(); ++column)
		columns += resources->columnWidth(column);
	ok &= expect(window.width() <= 960 && panel->width() == 200 && columns > resources->viewport()->width()
			&& resources->horizontalScrollBar()->maximum() > 0,
		qPrintable(QString("a panel of 200 px and a resources table that scrolls sideways: window %1, panel %2, columns %3 in %4")
				.arg(window.width())
				.arg(panel->width())
				.arg(columns)
				.arg(resources->viewport()->width())));
	window.findChild<QToolButton*>("areaCapacityPanelToggle")->setFocus(Qt::TabFocusReason);
	QStringList order;
	for (int step = 0; step < 3 && QApplication::focusWidget(); ++step) {
		QWidget* focused = QApplication::focusWidget();
		order << (qobject_cast<QTabBar*>(focused) ? QStringLiteral("tabs") : focused->objectName());
		pressTab(focused);
	}
	ok &= expect(order == QStringList({"areaCapacityPanelToggle", "tabs", "areaCapacityResources"}),
		qPrintable("Tab goes from the header to the tabs and their table: " + order.join(", ")));
	exportButton->setFocus(Qt::TabFocusReason);
	pressTab(exportButton);
	ok &= expect(QApplication::focusWidget() != exportButton && !panel->isAncestorOf(QApplication::focusWidget()),
		"Tab leaves the export, the last control of the panel");
	return ok;
}

// Under the style sheet of the application the wrapped statuses and the tall Chain tab do not squeeze the header to its
// smallest height.
bool exerciseHeader() {
	QFile stylesheet(QStringLiteral(EGTRAIN_DIALOG_QSS));
	if (!expect(stylesheet.open(QIODevice::ReadOnly), "the style sheet opens"))
		return false;
	qApp->setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
	bool ok = true;
	{
		const CompletedRunData data = lineRun();
		AreaCapacityWindow window = makeWindow(analyze(data, lineScope({"F1", "F2"})));
		window.show();
		QApplication::processEvents();
		const auto* header = window.findChild<QWidget*>("resultHeader");
		ok = expect(header && header->height() == header->sizeHint().height(),
			qPrintable(QString("the header keeps its height above the statuses: %1 of %2")
					.arg(header ? header->height() : -1)
					.arg(header ? header->sizeHint().height() : -1)));
	}
	qApp->setStyleSheet(QString());
	return ok;
}

// At 1.5 times the font in a short window the body of the dialog scrolls to the panel, which keeps its figures whole, and
// Close stays below the body.
bool exerciseLargeFont() {
	const QFont original = QApplication::font();
	QFont font = original;
	font.setPointSizeF(font.pointSizeF() * 1.5);
	QApplication::setFont(font);
	bool ok = true;
	{
		AreaCapacityWindow window = makeWindow(analyze(lineRun(), lineScope({"F1", "F2"})));
		window.show();
		window.resize(window.width(), 220);
		QApplication::processEvents();
		auto* scroll = window.findChild<QScrollArea*>("dialogBodyScroll");
		auto* panel = window.findChild<QWidget*>("resultPanel");
		auto* close = window.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Close);
		ok = expect(scroll && panel && close && scroll->verticalScrollBar()->maximum() > 0
				&& panel->height() >= panel->minimumSizeHint().height() && close->isVisible()
				&& close->mapTo(&window, QPoint(0, close->height())).y() <= window.height(),
			qPrintable(QString("the body scrolls to the whole panel and Close stays in the window: range %1, panel %2 of %3")
					.arg(scroll ? scroll->verticalScrollBar()->maximum() : -1)
					.arg(panel ? panel->height() : -1)
					.arg(panel ? panel->minimumSizeHint().height() : -1)));
	}
	QApplication::setFont(original);
	return ok;
}

QStringList lines(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return QString::fromUtf8(file.readAll()).split("\r\n", Qt::SkipEmptyParts);
}

bool exerciseExport() {
	QTemporaryDir directory;
	if (!expect(directory.isValid(), "temporary directory"))
		return false;
	const CompletedRunData data = lineRun();
	AreaCapacityWindow window = makeWindow(analyze(data, lineScope({"F1", "F2"})));
	const QString path = QDir(directory.path()).filePath("area_capacity.csv");
	bool ok = expect(window.exportCsvTo(path), "the export is written");
	const QStringList rows = lines(path);
	ok &= expect(rows.size() == 1 + 1 + 8 + 1 + 1 && rows[0].startsWith("Record type,Area,Traffic,Period[s],Resource,"), "header and records");
	// Cycle 471.5 s is 13.097... percent of 3600 s. The envelope 1147.5 s is 31.87... percent. The first and the
	// last train of the cycle are in the leader and follower columns.
	ok &= expect(rows.size() > 1 && rows[1].startsWith("summary,R / main / @0-B0@ to @7-B0@,partial traffic,3600,,,,,,,,F1,F2,,,,,471.5,13.09722")
			&& rows[1].contains(",1147.5,31.87") && rows[1].endsWith(",,,,"),
		"the summary record has the cycle and the legacy envelope");
	const auto record = [&rows](const QString& prefix) {
		for (const QString& row : rows)
			if (row.startsWith(prefix))
				return row;
		return QString();
	};
	// 174 s is 87 s of F1 and 87 s of F2 on @0. The rate 174 / 3600 is 4.83 percent.
	ok &= expect(record("resource,R / main / @0-B0@ to @7-B0@,partial traffic,3600,@0-B0@,2,2,174,174,4.83").size() > 0, "a resource record");
	ok &= expect(record("resource,R / main / @0-B0@ to @7-B0@,partial traffic,3600,@3-B0@,2,2,1033,701.5,28.69").endsWith(",yes" + QString(14, ',')),
		"the governing resource record");
	ok &= expect(record("pair,").contains(",F1,F2,60,471.5,-411.5,"), "the pair record");
	ok &= expect(record("note,").contains(",Unselected traffic,G,,"), "the note record");

	QFile sidecar(path + ".provenance.json");
	ok &= expect(sidecar.open(QIODevice::ReadOnly), "the provenance sidecar is written");
	const QJsonObject run = QJsonDocument::fromJson(sidecar.readAll()).object().value("run").toObject();
	ok &= expect(run.value("case_name").toString() == "Case A" && run.value("applied_scenario").toString() == "Scenario B",
		"the sidecar names the run");

	AreaCapacityWindow empty = makeWindow(AreaCapacityResult());
	const QString emptyPath = QDir(directory.path()).filePath("empty.csv");
	ok &= expect(!empty.exportCsvTo(emptyPath) && !QFile::exists(emptyPath) && !QFile::exists(emptyPath + ".provenance.json"),
		"a result without a resource row writes nothing");
	ok &= expect(areaCapacityCsv(AreaCapacityResult(), "area").empty(), "no export text without a resource row");
	return ok;
}

QComboBox* combo(const AreaCapacityScopeDialog& dialog, const char* name) {
	return dialog.findChild<QComboBox*>(name);
}

QStringList listed(const AreaCapacityScopeDialog& dialog) {
	QStringList ids;
	const auto* list = dialog.findChild<QListWidget*>("areaCapacityOccurrences");
	for (int row = 0; list && row < list->count(); ++row)
		ids << list->item(row)->data(Qt::UserRole).toString();
	return ids;
}

bool exerciseDialog() {
	const CompletedRunData data = lineRun();
	AreaCapacityScopeDialog dialog(data);
	dialog.show();
	QApplication::processEvents();
	auto* list = dialog.findChild<QListWidget*>("areaCapacityOccurrences");
	auto* buttons = dialog.findChild<QDialogButtonBox*>();
	QPushButton* accept = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
	bool ok = expect(list && accept && combo(dialog, "areaCapacityRoute") && combo(dialog, "areaCapacityLastBlock"), "the dialog has its widgets");
	if (!ok)
		return false;

	// The run lists F2 first. The dialog sorts by reference time, and G has none.
	ok &= expect(listed(dialog) == QStringList({"F1", "F2", "G"}), "the trains that use the area, in the order of their reference time");
	AreaCapacityScope scope = dialog.scope();
	ok &= expect(scope.routeIndex == 0 && scope.blockIds.size() == 8 && scope.blockIds.front() == "@0-B0@" && scope.secondRouteIndex == -1
			&& scope.secondBlockIds.empty() && scope.periodSeconds == 3600.0
			&& scope.occurrenceIds == std::vector<std::string>({"F1", "F2", "G"}) && scope.cycleEndOccurrenceId.empty(),
		"the default scope is the whole first route with every listed train checked");
	ok &= expect(list->item(0)->checkState() == Qt::Checked && list->item(0)->text() == "F1"
			&& list->item(0)->toolTip() == "Reference time: planned departure at A" && list->item(2)->text() == "G, no reference time",
		"every train is checked, the reference time is in the tooltip and a missing one is said");
	ok &= expect(scope.sectionIndices == std::vector<int>({0, 1, 2, 3, 4, 5, 6, 7}), "the scope numbers its blocks in the route");

	// Cycle-closing train: F2 is after the first checked train. With F1 unchecked it is the first one.
	combo(dialog, "areaCapacityCycleEnd")->setCurrentIndex(2);
	ok &= expect(dialog.scope().cycleEndOccurrenceId == "F2" && accept->isEnabled(), "a cycle-closing train after the first one is accepted");
	list->item(0)->setCheckState(Qt::Unchecked);
	const auto* status = dialog.findChild<QLabel*>("areaCapacityScopeStatus");
	ok &= expect(!accept->isEnabled() && status && status->isVisible() && status->text().contains("cycle-closing"),
		"a cycle-closing train that is first is refused inline");
	list->item(0)->setCheckState(Qt::Checked);
	ok &= expect(accept->isEnabled() && status && status->isHidden(), "the refusal goes away when the train is checked again");

	// A cycle-closing train that runs the other way is accepted with a warning, because the chain leaves it out.
	combo(dialog, "areaCapacityCycleEnd")->setCurrentIndex(3);
	ok &= expect(dialog.scope().cycleEndOccurrenceId == "G" && accept->isEnabled() && status && status->isVisible()
			&& status->text().contains("other direction"),
		"a cycle-closing train that runs the other way is warned about");
	combo(dialog, "areaCapacityCycleEnd")->setCurrentIndex(2);
	ok &= expect(accept->isEnabled() && status && status->isHidden(), "the warning goes away with another cycle-closing train");

	// Moving a row, as a drop does, changes the order of the chain and checks the cycle-closing train again: F2 is first now.
	ok &= expect(list->model()->moveRow(QModelIndex(), 0, QModelIndex(), 2), "a row moves");
	ok &= expect(dialog.scope().occurrenceIds == std::vector<std::string>({"F2", "F1", "G"}), "the displayed order is the order of the chain");
	ok &= expect(!accept->isEnabled() && status && status->isVisible() && status->text().contains("cycle-closing"),
		"moving a row checks the cycle-closing train against the new order");

	// A narrower range of the first route leaves G out; the second route brings it back.
	combo(dialog, "areaCapacityLastBlock")->setCurrentIndex(3);
	ok &= expect(listed(dialog) == QStringList({"F1", "F2"}) && dialog.scope().blockIds.size() == 4, "a narrower range lists the trains that use it");
	ok &= expect(dialog.scope().cycleEndOccurrenceId.empty() && accept->isEnabled(), "a new list has no cycle-closing train yet");
	combo(dialog, "areaCapacitySecondRoute")->setCurrentIndex(2);
	scope = dialog.scope();
	ok &= expect(scope.secondRouteIndex == 1 && scope.secondBlockIds == std::vector<std::string>({"@7-B0@", "@6-B0@"})
			&& listed(dialog) == QStringList({"F1", "F2", "G"}),
		"a second route adds its range to the area");
	combo(dialog, "areaCapacitySecondRoute")->setCurrentIndex(0);
	ok &= expect(dialog.scope().secondBlockIds.empty() && !combo(dialog, "areaCapacitySecondFirstBlock")->isEnabled(), "no second route again");

	list->item(0)->setCheckState(Qt::Unchecked);
	list->item(1)->setCheckState(Qt::Unchecked);
	ok &= expect(accept->isEnabled() && dialog.scope().occurrenceIds.empty() && status && status->isVisible(),
		"with no train checked the dialog warns and still accepts");
	accept->click();
	ok &= expect(dialog.result() == QDialog::Accepted, "OK accepts");

	CompletedRunData noRoutes;
	AreaCapacityScopeDialog empty(noRoutes);
	empty.show();
	QApplication::processEvents();
	auto* emptyButtons = empty.findChild<QDialogButtonBox*>();
	ok &= expect(emptyButtons && !emptyButtons->button(QDialogButtonBox::Ok)->isEnabled() && empty.scope().routeIndex == -1
			&& empty.scope().blockIds.empty(),
		"a run without a route cannot be accepted");
	return ok;
}

// Scopes that give no figures or leave out traffic, built as the dialog builds them.
bool exerciseEdgeCases() {
	const CompletedRunData data = lineRun();

	// An area without a resource is reported and nothing is computed or exported.
	AreaCapacityScope noArea = lineScope({"F1", "F2"});
	noArea.routeIndex = -1;
	noArea.blockIds.clear();
	AreaCapacityWindow empty = makeWindow(analyze(data, noArea));
	const auto* emptyStatuses = empty.findChild<QLabel*>("areaCapacityStatuses");
	const auto* emptyExport = empty.findChild<QPushButton*>("areaCapacityExport");
	bool ok = expect(figure(empty, "areaCapacityCycle") == "- | Chain cannot be analysed" && table(empty, "areaCapacityResources")->rowCount() == 0
			&& emptyStatuses && emptyStatuses->text() == "Statuses: Empty scope (1)" && emptyExport && !emptyExport->isEnabled(),
		"an area without a resource is reported");

	// A period of zero gives no percentage anywhere and an empty field in the export.
	AreaCapacityScope zero = lineScope({"F1", "F2", "G"});
	zero.periodSeconds = 0.0;
	const AreaCapacityResult noPeriod = analyze(data, zero);
	AreaCapacityWindow window = makeWindow(noPeriod);
	window.show();
	QApplication::processEvents();
	ok &= expect(cycleFigures(window) == "471.5 s | Cycle, - | No valid period", qPrintable("the figures of a period of zero: " + cycleFigures(window)));
	const auto* summary = window.findChild<QLabel*>("areaCapacityChainSummary");
	ok &= expect(summary && summary->text().startsWith("Cycle 471.5 s, no valid period, from F1 to ")
			&& summary->toolTip().startsWith("F1: planned departure at A"),
		"the chain tab of a period of zero, with the reference times in the tooltip");
	const QTableWidget* resources = table(window, "areaCapacityResources");
	const int governing = rowOf(resources, "3-B0");
	ok &= expect(governing >= 0 && cell(resources, governing, 1) == "-" && cell(resources, governing, 2) == "governing resource",
		"the rate of a period of zero is not available");
	const auto* governingFigure = window.findChild<ResultFigure*>("areaCapacityGoverning");
	const auto* statuses = window.findChild<QLabel*>("areaCapacityStatuses");
	ok &= expect(figure(window, "areaCapacityGoverning") == "- | Governing: 3-B0" && governingFigure->toolTip().contains("No valid period") && statuses
			&& statuses->text() == "Statuses: Invalid period (1), Opposing train (1)",
		"the governing resource and the statuses of a period of zero");
	const std::string csvText = areaCapacityCsv(noPeriod, "area");
	ok &= expect(csvText.find("summary,area,complete traffic,,") != std::string::npos
			&& csvText.find("resource,area,complete traffic,,@3-B0@,2,2,1033,701.5,,yes,") != std::string::npos,
		"the export leaves the period and the rate empty");

	// A cycle-closing train that runs the other way is not in the chain.
	AreaCapacityScope opposing = lineScope({"F1", "F2", "G"});
	opposing.cycleEndOccurrenceId = "G";
	AreaCapacityWindow other = makeWindow(analyze(data, opposing));
	const auto* otherSummary = other.findChild<QLabel*>("areaCapacityChainSummary");
	ok &= expect(figure(other, "areaCapacityCycle") == "- | No cycle-closing train" && otherSummary && otherSummary->text() == "No cycle-closing train was chosen."
			&& rowOf(table(other, "areaCapacityExcluded"), "Opposing train") == 0,
		"an opposing cycle-closing train is left out of the chain and listed as opposing");

	// A train that occupies a resource twice counts both occupations and is noted.
	CompletedRunData repeated = data;
	repeated.trains[1].blockTimes.push_back(occupation("@0-B0@", 400.0, 450.0));
	AreaCapacityWindow twice = makeWindow(analyze(repeated, lineScope({"F1", "F2"})));
	const QTableWidget* twiceResources = table(twice, "areaCapacityResources");
	const int first = rowOf(twiceResources, "0-B0");
	const QTableWidget* twiceNotes = table(twice, "areaCapacityNotes");
	const int note = rowOf(twiceNotes, "Repeated resource");
	ok &= expect(first >= 0 && cell(twiceResources, first, 3) == "2" && cell(twiceResources, first, 4) == "3" && cell(twiceResources, first, 5) == "224"
			&& note >= 0 && cell(twiceNotes, note, 1) == "F1" && cell(twiceNotes, note, 3) == "0-B0",
		"a repeated resource is counted twice and noted");
	return ok;
}

// Handles events until `done` holds, for at most about a second.
bool waitFor(const std::function<bool()>& done) {
	for (int round = 0; round < 200 && !done(); ++round) {
		QApplication::processEvents(QEventLoop::AllEvents, 5);
		QThread::msleep(5);
	}
	return done();
}

// After the save dialog of the export closes, cancelled or saved, the window is raised and active again, also when the
// main window took the activation while the dialog was open, as AppKit gives it on macOS.
bool exerciseExportKeepsWindowInFront() {
	QTemporaryDir directory;
	QMainWindow mainWindow;
	mainWindow.resize(500, 400);
	mainWindow.show();
	const CompletedRunData data = lineRun();
	auto* window = new AreaCapacityWindow(analyze(data, lineScope({"F1", "F2"})), "R / main / @0-B0@ to @7-B0@", data.provenance, &mainWindow);
	window->show();
	window->activateWindow();
	auto* exportButton = window->findChild<QPushButton*>("areaCapacityExport");
	bool ok = expect(directory.isValid() && exportButton && exportButton->isEnabled()
			&& waitFor([window] { return QApplication::activeWindow() == window; }),
		"the window is active before the export");
	if (!ok)
		return false;
	const QString saved = QDir(directory.path()).filePath("front.csv");
	for (const QString& path : {QString(), saved}) {
		bool mainActive = false;
		QTimer::singleShot(0, window, [&mainWindow, &mainActive, &path] {
			for (QWidget* widget : QApplication::topLevelWidgets())
				if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
					mainWindow.activateWindow();
					mainActive = waitFor([&mainWindow] { return QApplication::activeWindow() == &mainWindow; });
					if (path.isEmpty()) {
						dialog->reject();
					} else {
						dialog->setDirectory(QFileInfo(path).absolutePath());
						dialog->selectFile(QFileInfo(path).fileName());
						// Through QDialog, as QFileDialog::accept is protected.
						static_cast<QDialog*>(dialog)->accept();
					}
				}
		});
		exportButton->click();
		const bool active = waitFor([window] { return QApplication::activeWindow() == window; });
		ok &= expect(mainActive && active,
			path.isEmpty() ? "a cancelled export leaves the window active, though the main window took the activation"
						   : "a saved export leaves the window active, though the main window took the activation");
	}
	ok &= expect(QFile::exists(saved), "the saved export is written");
	return ok;
}

// Presses a button of the options dialog once it is open.
void answerOptions(QDialogButtonBox::StandardButton button) {
	QTimer::singleShot(0, [button]() {
		auto* dialog = dynamic_cast<AreaCapacityScopeDialog*>(QApplication::activeModalWidget());
		if (dialog && dialog->findChild<QDialogButtonBox*>())
			dialog->findChild<QDialogButtonBox*>()->button(button)->click();
	});
}

bool exerciseOpen() {
	const CompletedRunData data = lineRun();
	answerOptions(QDialogButtonBox::Cancel);
	bool ok = expect(openAreaCapacityWindow(nullptr, data) == nullptr, "cancelling the options opens no window");
	// Every listed train is checked and no cycle-closing train is chosen. G runs the other way, so the traffic is complete.
	answerOptions(QDialogButtonBox::Ok);
	QPointer<AreaCapacityWindow> window = openAreaCapacityWindow(nullptr, data);
	ok &= expect(window && window->isVisible() && !window->isModal() && window->testAttribute(Qt::WA_DeleteOnClose),
		"the options open a window that is not modal and deletes itself");
	ok &= expect(window && figure(*window, "areaCapacityCycle") == "- | No cycle-closing train", "the window shows the chosen scope");
	if (window)
		window->close();
	QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	ok &= expect(window.isNull(), "closing the window deletes it");
	return ok;
}

// The run of lineRun with a label for each block of R.
CompletedRunData labelledRun() {
	CompletedRunData data = lineRun();
	const char* const labels[] = {"B0 0.000 to 0.230 km", "B0 0.230 to 0.460 km", "B0 0.460 to 0.690 km", "B0 0.690 to 0.920 km",
		"B0 0.920 to 1.150 km", "B0 1.150 to 1.380 km", "B0 1.380 to 1.610 km", "B0 1.610 to 1.840 km"};
	for (int index = 0; index < 8; ++index)
		data.sectionLabels.byReference["@" + std::to_string(index) + "-B0@"] = labels[index];
	return data;
}

// The options and the window name the resources by their labels. The export and the tooltips keep the ids.
bool exerciseLabels() {
	const CompletedRunData data = labelledRun();
	AreaCapacityScope scope = lineScope({"F1", "F2"});
	scope.secondRouteIndex = 1;
	scope.secondBlockIds = {"@7-B0@", "@6-B0@"};
	scope.secondSectionIndices = {0, 1};
	bool ok = expect(areaCapacityText(data, scope) == "R (main), blocks 1 to 8 and S, blocks 1 to 2"
			&& areaCapacityLabel(data, scope) == "R / main / @0-B0@ to @7-B0@ and S / @7-B0@ to @6-B0@",
		"the window names the area by the numbers of its blocks, the export by their ids");

	AreaCapacityScopeDialog dialog(data);
	const QComboBox* first = combo(dialog, "areaCapacityFirstBlock");
	ok &= expect(first && first->itemText(0) == "1: B0 0.000 to 0.230 km" && first->itemData(0, Qt::ToolTipRole).toString() == "0-B0"
			&& combo(dialog, "areaCapacityLastBlock")->currentText() == "8: B0 1.610 to 1.840 km",
		"the options name the blocks by their labels, with the short id as tooltip");

	const AreaCapacityScope one = lineScope({"F1", "F2"});
	AreaCapacityWindow window(analyze(data, one), areaCapacityLabel(data, one), data.provenance, nullptr, areaCapacityText(data, one),
		data.sectionLabels);
	ok &= expect(context(window) == "Case A / Scenario B \u00b7 R (main), blocks 1 to 8 \u00b7 partial traffic"
			&& figure(window, "areaCapacityGoverning") == "28.7% | Governing: B0 0.690 to 0.920 km",
		"the header names the area by its block numbers and the panel the governing resource by its label");
	// Tied governing resources are all named, with semicolons between their labels, since a label can hold a comma.
	AreaCapacityResult tied = analyze(data, one);
	for (AreaCapacityResource& resource : tied.resources)
		resource.governing = resource.resourceId == "@3-B0@" || resource.resourceId == "@4-B0@";
	const AreaCapacityWindow tiedWindow(tied, areaCapacityLabel(data, one), data.provenance, nullptr, areaCapacityText(data, one),
		data.sectionLabels);
	const QString tiedCaption = "Governing: " + QString::fromStdString(data.sectionLabels.label("@3-B0@") + "; " + data.sectionLabels.label("@4-B0@"));
	ok &= expect(tiedWindow.findChild<ResultFigure*>("areaCapacityGoverning")->caption() == tiedCaption,
		qPrintable("two governing resources: " + figure(tiedWindow, "areaCapacityGoverning")));
	const QTableWidget* resources = table(window, "areaCapacityResources");
	const int governing = rowOf(resources, "3-B0");
	ok &= expect(governing >= 0 && resources->item(governing, 0)->toolTip() == "B0 0.690 to 0.920 km",
		"a resource has its short id in the table and its label as tooltip");
	const QTableWidget* pairs = table(window, "areaCapacityPairs");
	ok &= expect(pairs && pairs->rowCount() == 1 && cell(pairs, 0, 7).contains("-B0 / ") && !cell(pairs, 0, 7).contains("@")
			&& !pairs->item(0, 7)->toolTip().isEmpty() && !pairs->item(0, 7)->toolTip().contains("@") && pairs->item(0, 7)->toolTip().contains("B0 "),
		qPrintable("the evidence names the blocks by their short ids and its tooltip by their labels: " + cell(pairs, 0, 7)));
	const QTableWidget* compression = table(window, "areaCapacityCompression");
	int predecessorRow = -1;
	for (int row = 0; compression && row < compression->rowCount(); ++row)
		if (predecessorRow < 0 && cell(compression, row, 6).contains(" / "))
			predecessorRow = row;
	ok &= expect(predecessorRow >= 0 && !cell(compression, predecessorRow, 6).contains("@")
			&& compression->item(predecessorRow, 6)->toolTip().contains("B0 ") && !compression->item(predecessorRow, 6)->toolTip().contains("@"),
		"the governing predecessor names its blocks by their short ids and its tooltip by their labels");
	const QTableWidget* critical = table(window, "areaCapacityCritical");
	ok &= expect(critical && critical->rowCount() > 0 && cell(critical, 0, 2).endsWith("-B0") && !cell(critical, 0, 2).contains("@")
			&& critical->item(0, 2)->toolTip() == QString::fromStdString(data.sectionLabels.label("@" + cell(critical, 0, 2).toStdString() + "@"))
			&& critical->item(0, 3)->toolTip() == QString::fromStdString(data.sectionLabels.label("@" + cell(critical, 0, 3).toStdString() + "@"))
			&& critical->item(0, 2)->toolTip().startsWith("B0 "),
		"a critical resource has its short id and its label as tooltip");
	AreaCapacityResult noted;
	noted.notes.push_back({AreaCapacityNoteKind::RepeatedResource, "F1", "", "@7-B0@"});
	AreaCapacityWindow notedWindow(noted, areaCapacityLabel(data, one), data.provenance, nullptr, QString(), data.sectionLabels);
	const QTableWidget* notes = table(notedWindow, "areaCapacityNotes");
	ok &= expect(notes && notes->rowCount() == 1 && cell(notes, 0, 3) == "7-B0" && notes->item(0, 3)->toolTip() == "B0 1.610 to 1.840 km"
			&& cell(notes, 0, 4).contains("resource 7-B0 ") && !cell(notes, 0, 4).contains("@") && notes->item(0, 4)->toolTip() == cell(notes, 0, 4),
		qPrintable("a note names its resource by its short id with its label as tooltip, and its details are their own tooltip: " + cell(notes, 0, 4)));
	ok &= expect(tabTitle(notedWindow, 2) == "Excluded traffic" && tabTitle(notedWindow, 3) == "Notes (1)", "a tab title gives its count only when it is not 0");

	QTemporaryDir directory;
	const QString path = QDir(directory.path()).filePath("area_capacity.csv");
	ok &= expect(directory.isValid() && window.exportCsvTo(path) && lines(path).size() > 1
			&& lines(path)[1].startsWith("summary,R / main / @0-B0@ to @7-B0@,") && !lines(path).join("\n").contains("B0 0."),
		"the export names the area and the resources by their ids");
	return ok;
}

} // namespace

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = exerciseInput();
	ok &= exerciseResult();
	ok &= exerciseStatuses();
	ok &= exercisePanel();
	ok &= exerciseHeader();
	ok &= exerciseLargeFont();
	ok &= exerciseExport();
	ok &= exerciseExportKeepsWindowInFront();
	ok &= exerciseDialog();
	ok &= exerciseEdgeCases();
	ok &= exerciseOpen();
	ok &= exerciseLabels();
	return ok ? 0 : 1;
}
