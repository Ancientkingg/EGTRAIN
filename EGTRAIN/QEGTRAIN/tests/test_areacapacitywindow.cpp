// Tests of the area capacity window, its options dialog and its export. Every value is synthetic. The
// numbers of the line F1, F2 were copied once from the made-up line of the committed test fixture, as in
// test_areacapacity.cpp: eight blocks, F1 then F2, profile references 59 s and 199 s, authored departures
// 120 s and 180 s. Times are in seconds and percentages in percent. The test opens the file chooser to check
// that the window stays in front after it and what the export asks for, and closes the warning of an export that
// fails itself.

#include "diagrams/AreaCapacityWindow.h"
#include "diagrams/ResultFrame.h"
#include "widgets/DisplayPath.h"

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
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace {

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

void pressTab(QWidget* widget) {
	QKeyEvent press(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
	QKeyEvent release(QEvent::KeyRelease, Qt::Key_Tab, Qt::NoModifier);
	QApplication::sendEvent(widget, &press);
	QApplication::sendEvent(widget, &release);
}

QStringList lines(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return QString::fromUtf8(file.readAll()).split("\r\n", Qt::SkipEmptyParts);
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

// Handles events until `done` holds, for at most about a second.
bool waitFor(const std::function<bool()>& done) {
	for (int round = 0; round < 200 && !done(); ++round) {
		QApplication::processEvents(QEventLoop::AllEvents, 5);
		QThread::msleep(5);
	}
	return done();
}

// Presses a button of the options dialog once it is open.
void answerOptions(QDialogButtonBox::StandardButton button) {
	QTimer::singleShot(0, [button]() {
		auto* dialog = dynamic_cast<AreaCapacityScopeDialog*>(QApplication::activeModalWidget());
		if (dialog && dialog->findChild<QDialogButtonBox*>())
			dialog->findChild<QDialogButtonBox*>()->button(button)->click();
	});
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

// What the save dialog of an export showed, and the texts of the warnings after it.
struct ExportDialog {
	QString title;
	QStringList filters;
	QString suggested;
	QStringList warnings;
};

// Clicks `button`, answers the save dialog with `path`, or cancels it for an empty path, and closes the messages that
// follow. A dialog that stays open after the answer is cancelled.
ExportDialog answerExport(QPushButton* button, const QString& path) {
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

} // namespace

class TestAreaCapacityWindow : public QObject {
	Q_OBJECT

private slots:
	void buildsInputFromScope();
	void showsResultOfScope();
	void showsEveryStatus();
	void laysOutPanelBesideTabs();
	void keepsHeaderHeightUnderStyleSheet();
	void scrollsBodyWithLargeFont();
	void exportsCsvWithProvenance();
	void keepsWindowInFrontAfterExport();
	void asksForCsvFileOnExport();
	void choosesScopeInDialog();
	void reportsScopesWithoutFigures();
	void opensWindowFromOptions();
	void namesResourcesByLabels();
};

void TestAreaCapacityWindow::buildsInputFromScope() {
	const CompletedRunData data = lineRun();
	AreaCapacityScope scope = lineScope({"F2", "F1"});
	scope.secondRouteIndex = 1;
	scope.secondBlockIds = {"@7-B0@", "@6-B0@"};
	const AreaCapacityInput input = areaCapacityInputFrom(data, scope);
	// the resources are the range of the first route followed by the range of the second
	QCOMPARE(input.resourceIds.size(), std::size_t(10));
	QCOMPARE(input.resourceIds[7], "@7-B0@");
	QCOMPARE(input.resourceIds[8], "@7-B0@");
	// the selected trains keep the order of the scope
	QCOMPARE(input.selected.size(), std::size_t(2));
	QCOMPARE(input.selected[0].train.runtimeId, "F2");
	QCOMPARE(input.selected[1].train.runtimeId, "F1");
	// every train that is not selected is other traffic
	QCOMPARE(input.otherTraffic.size(), std::size_t(2));
	QCOMPARE(input.otherTraffic[0].train.runtimeId, "G");
	QCOMPARE(input.otherTraffic[1].train.runtimeId, "Z");
	// the period and the cycle-closing train are passed on
	QCOMPARE_EQ(input.periodSeconds, 3600.0);
	QCOMPARE(input.cycleEndIdentity, "F2");
	const AreaCapacityTrain& first = input.selected[1];
	// the reference times come from the first block of the first route and all occupations are passed
	QCOMPARE_EQ(first.train.profileReferenceTime, 59.0);
	QCOMPARE_EQ(first.train.scheduledReferenceTime, 120.0);
	QCOMPARE(first.train.occupations.size(), std::size_t(8));
	// the direction is the one of the route of the train
	QVERIFY(!first.reversedDirection);
	QVERIFY(input.otherTraffic[0].reversedDirection);
	// a train without the first block has no reference
	QCOMPARE_LT(input.otherTraffic[0].train.profileReferenceTime, 0.0);
	CompletedRunData later = data;
	later.trains.push_back(train("L", 0, {occupation("@3-B0@", 400.0, 460.0, 400.0)}));
	const AreaCapacityInput laterInput = areaCapacityInputFrom(later, lineScope({"F1", "L"}));
	// a train that enters the area after the first block has no reference, though its record there has a StartRunTime
	QCOMPARE(laterInput.selected.size(), std::size_t(2));
	QCOMPARE_LT(laterInput.selected[1].train.profileReferenceTime, 0.0);
	// the label names both route ranges
	QCOMPARE(areaCapacityLabel(data, scope).toStdString(), "R / main / @0-B0@ to @7-B0@ and S / @7-B0@ to @6-B0@");
	// the label of one route range
	QCOMPARE(areaCapacityLabel(data, lineScope({})).toStdString(), "R / main / @0-B0@ to @7-B0@");

	// An occupation that is not complete is passed on and reported, not dropped.
	CompletedRunData incomplete = data;
	incomplete.trains[0].blockTimes[2] = occupation("@2-B0@", 275.5, 686.0, -1.0, false);
	const AreaCapacityResult result = analyze(incomplete, lineScope({"F1", "F2"}));
	// an incomplete occupation of the run is reported
	QVERIFY(!result.complete);
	QVERIFY(std::any_of(result.notes.begin(), result.notes.end(), [](const AreaCapacityNote& note) {
		return note.kind == AreaCapacityNoteKind::IncompleteOccupation && note.trainId == "F2" && note.resourceId == "@2-B0@";
	}));
}

void TestAreaCapacityWindow::showsResultOfScope() {
	const CompletedRunData data = lineRun();
	// F1 and F2 selected: G uses @6 and @7 and is not selected, so the traffic is partial.
	AreaCapacityWindow partial = makeWindow(analyze(data, lineScope({"F1", "F2"})));
	partial.show();
	QApplication::processEvents();
	// the panel gives the governing rate, the cycle and its share
	QCOMPARE(cycleFigures(partial), "471.5 s | Cycle, 13.1% | of 1 h");
	QCOMPARE(figure(partial, "areaCapacityGoverning"), "28.7% | Governing: 3-B0");
	const QTableWidget* resources = table(partial, "areaCapacityResources");
	// one row for each resource of the area
	if (resources == nullptr)
		QFAIL("one row for each resource of the area");
	QCOMPARE(resources->rowCount(), 8);
	const int governing = rowOf(resources, "3-B0");
	// F1 on @3 is 190.5 to 674 (483.5 s), F2 342.5 to 892 (549.5 s): sum 1033 s, union 190.5 to 892 (701.5 s), 1033 / 3600 = 28.7 percent.
	// the resource with the largest occupation is marked as governing
	QCOMPARE_GE(governing, 0);
	QCOMPARE(cell(resources, governing, 1), "28.7");
	QCOMPARE(cell(resources, governing, 2), "governing resource");
	QCOMPARE(cell(resources, governing, 3), "2");
	QCOMPARE(cell(resources, governing, 4), "2");
	QCOMPARE(cell(resources, governing, 5), "1033");
	QCOMPARE(cell(resources, governing, 6), "701.5");
	int governingRows = 0;
	for (int row = 0; resources && row < resources->rowCount(); ++row)
		governingRows += cell(resources, row, 2) == "governing resource" ? 1 : 0;
	// exactly one row is governing and it is bold
	QCOMPARE(governingRows, 1);
	QVERIFY(resources->item(governing, 0)->font().bold());
	// The envelope is the latest release 1201 minus the earliest occupation 53.5: 1147.5 s, 31.9 percent.
	// the legacy envelope stays out of the window
	QVERIFY(!partial.findChild<QLabel*>("areaCapacityEnvelope"));
	QVERIFY(!allText(partial).contains("envelope", Qt::CaseInsensitive));
	const QTableWidget* pairs = table(partial, "areaCapacityPairs");
	// the chain tab holds the pair with the minimum headway
	if (pairs == nullptr)
		QFAIL("the chain tab holds the pair with the minimum headway");
	QCOMPARE(pairs->rowCount(), 1);
	QCOMPARE(cell(pairs, 0, 0), "F1");
	QCOMPARE(cell(pairs, 0, 1), "F2");
	QCOMPARE(cell(pairs, 0, 5), "471.5");
	// the reference times of a pair are tooltips of its trains, not a column
	QCOMPARE(pairs->columnCount(), 8);
	QCOMPARE(pairs->item(0, 0)->toolTip(), "Reference time: planned departure at A");
	QCOMPARE(pairs->horizontalHeaderItem(5)->text(), "Minimum headway (s)");
	const QTableWidget* excluded = table(partial, "areaCapacityExcluded");
	// the excluded traffic tab lists the unselected train
	if (excluded == nullptr)
		QFAIL("the excluded traffic tab lists the unselected train");
	QCOMPARE(excluded->rowCount(), 1);
	QCOMPARE(cell(excluded, 0, 0), "Unselected traffic");
	QCOMPARE(cell(excluded, 0, 1), "G");
	// the tabs
	QCOMPARE(tabTitle(partial, 0), "Resources");
	QCOMPARE(tabTitle(partial, 1), "Chain");
	QCOMPARE(tabTitle(partial, 2), "Excluded traffic (1)");
	const auto* statuses = partial.findChild<QLabel*>("areaCapacityStatuses");
	// the statuses line
	if (statuses == nullptr)
		QFAIL("the statuses line");
	QCOMPARE(statuses->text(), "Statuses: Unselected traffic (1)");
	const auto* partialContext = partial.findChild<QLabel*>("areaCapacityContext");
	// the header names the run, the area and the partial traffic
	QCOMPARE(context(partial), "Case A / Scenario B \u00b7 R / main / @0-B0@ to @7-B0@ \u00b7 partial traffic");
	if (partialContext == nullptr)
		QFAIL("the header names the run, the area and the partial traffic");
	QVERIFY2(partialContext->toolTip().contains("Rates count only the checked trains"),
		qPrintable("the header names the run, the area and the partial traffic: " + context(partial)));
	QVERIFY2(!partial.findChild<QLabel*>("dialogContext"), qPrintable("the header names the run, the area and the partial traffic: " + context(partial)));

	// All three trains selected: G runs the other way, so it counts in the rows and not in the chain.
	AreaCapacityWindow complete = makeWindow(analyze(data, lineScope({"F1", "F2", "G"})));
	complete.show();
	QApplication::processEvents();
	// the header of complete traffic, which says nothing about the traffic
	QCOMPARE(cycleFigures(complete), "471.5 s | Cycle, 13.1% | of 1 h");
	QCOMPARE(context(complete), "Case A / Scenario B \u00b7 R / main / @0-B0@ to @7-B0@");
	const QTableWidget* completeResources = table(complete, "areaCapacityResources");
	const int sixth = rowOf(completeResources, "6-B0");
	// an opposing train counts in the rows of the resources
	QCOMPARE_GE(sixth, 0);
	QCOMPARE(cell(completeResources, sixth, 3), "3");
	QCOMPARE(cell(completeResources, sixth, 5), "303");
	const QTableWidget* completeExcluded = table(complete, "areaCapacityExcluded");
	// an opposing train is listed as excluded from the chain
	if (completeExcluded == nullptr)
		QFAIL("an opposing train is listed as excluded from the chain");
	QCOMPARE(completeExcluded->rowCount(), 1);
	QCOMPARE(cell(completeExcluded, 0, 0), "Opposing train");

	// A resource that no selected train uses stays in the table and says so.
	AreaCapacityScope idle = lineScope({"F1", "F2"});
	idle.blockIds.push_back("@8-B0@");
	AreaCapacityWindow unused = makeWindow(analyze(data, idle));
	const QTableWidget* unusedResources = table(unused, "areaCapacityResources");
	const int last = rowOf(unusedResources, "8-B0");
	// a resource without traffic is shown as 0 with no traffic
	QCOMPARE_GE(last, 0);
	QCOMPARE(cell(unusedResources, last, 2), "no traffic");
	QCOMPARE(cell(unusedResources, last, 3), "0");
	QCOMPARE(cell(unusedResources, last, 5), "0");

	const QString text = allText(partial) + allText(complete);
	// no trains per hour figure is shown
	QVERIFY(!text.contains("per hour", Qt::CaseInsensitive));
	QVERIFY(!text.contains("tph", Qt::CaseInsensitive));
	QVERIFY(!text.contains("trains/h", Qt::CaseInsensitive));
}

void TestAreaCapacityWindow::showsEveryStatus() {
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
	// the figures without a chain
	QCOMPARE(cycleFigures(window), "- | Chain cannot be analysed, - | No valid period");
	QCOMPARE(figure(window, "areaCapacityGoverning"), "- | No governing resource");
	const QTableWidget* notes = table(window, "areaCapacityNotes");
	// a row for every note
	if (notes == nullptr)
		QFAIL("a row for every note");
	QCOMPARE(notes->rowCount(), 12);
	QCOMPARE(tabTitle(window, 3), "Notes (12)");
	const auto* statuses = window.findChild<QLabel*>("areaCapacityStatuses");
	for (int index = 0; index < 12; ++index) {
		const QString name = areaCapacityStatusName(kinds[index]);
		// the notes table shows the status name
		QVERIFY(!name.isEmpty());
		QCOMPARE(cell(notes, index, 0), name);
		// the statuses line names every status
		if (statuses == nullptr)
			QFAIL("the statuses line names every status");
		QVERIFY(statuses->text().contains(name + " (1)"));
		QVERIFY2(!areaCapacityStatusText({kinds[index], "T1", "T2", "X"}).isEmpty(), "every status has a meaning");
	}
	const QTableWidget* excluded = table(window, "areaCapacityExcluded");
	// four statuses concern traffic that the figures leave out
	if (excluded == nullptr)
		QFAIL("four statuses concern traffic that the figures leave out");
	QCOMPARE(excluded->rowCount(), 4);
	const auto* exportButton = window.findChild<QPushButton*>("areaCapacityExport");
	// nothing to export without a resource row
	if (exportButton == nullptr)
		QFAIL("nothing to export without a resource row");
	QVERIFY(!exportButton->isEnabled());
	QVERIFY(!exportButton->autoDefault());
}

// The panel holds the figures and the export. The statuses lie above the tabs with the look of a warning, the tables
// scroll sideways beside a panel of 200 px, and Tab goes through the header, the tabs and the panel.
void TestAreaCapacityWindow::laysOutPanelBesideTabs() {
	const CompletedRunData data = lineRun();
	AreaCapacityWindow window = makeWindow(analyze(data, lineScope({"F1", "F2"})));
	window.show();
	QApplication::processEvents();
	auto* panel = window.findChild<QWidget*>("resultPanel");
	auto* statuses = window.findChild<QLabel*>("areaCapacityStatuses");
	auto* tabs = window.findChild<QTabWidget*>("areaCapacityTabs");
	auto* exportButton = window.findChild<QPushButton*>("areaCapacityExport");
	// the window has a panel, statuses, tabs and an export
	if (panel == nullptr)
		QFAIL("the window has a panel, statuses, tabs and an export");
	if (statuses == nullptr)
		QFAIL("the window has a panel, statuses, tabs and an export");
	if (tabs == nullptr)
		QFAIL("the window has a panel, statuses, tabs and an export");
	if (exportButton == nullptr)
		QFAIL("the window has a panel, statuses, tabs and an export");
	bool inPanel = panel->isAncestorOf(exportButton);
	for (const char* name : {"areaCapacityGoverning", "areaCapacityCycle", "areaCapacityShare"})
		inPanel = inPanel && window.findChild<ResultFigure*>(name) && panel->isAncestorOf(window.findChild<ResultFigure*>(name));
	// the figures and the export lie in the panel, beside the tabs
	QVERIFY(inPanel);
	QVERIFY(!panel->isAncestorOf(tabs));
	// the export reads CSV...
	QCOMPARE(exportButton->text(), "CSV...");
	QCOMPARE(exportButton->accessibleName(), "Export CSV");
	QStringList headings;
	for (const QLabel* label : panel->findChildren<QLabel*>())
		if (label->property("resultSectionHeading").toBool() && label->isVisible())
			headings << label->text();
	// only the export has a heading
	QCOMPARE(headings, QStringList({"Export"}));
	// the statuses keep the look of a warning above the tabs
	QVERIFY(statuses->isVisible());
	QCOMPARE(statuses->property("dialogStatus").toString(), "warning");
	QVERIFY(!panel->isAncestorOf(statuses));
	QCOMPARE_LE(statuses->mapTo(&window, QPoint(0, statuses->height())).y(), tabs->mapTo(&window, QPoint()).y());

	const QTableWidget* resources = table(window, "areaCapacityResources");
	int columns = 0;
	for (int column = 0; column < resources->columnCount(); ++column)
		columns += resources->columnWidth(column);
	// a panel of 200 px and a resources table that scrolls sideways
	QCOMPARE_LE(window.width(), 960);
	QCOMPARE(panel->width(), 200);
	QCOMPARE_GT(columns, resources->viewport()->width());
	QCOMPARE_GT(resources->horizontalScrollBar()->maximum(), 0);
	window.findChild<QToolButton*>("areaCapacityPanelToggle")->setFocus(Qt::TabFocusReason);
	QStringList order;
	for (int step = 0; step < 3 && QApplication::focusWidget(); ++step) {
		QWidget* focused = QApplication::focusWidget();
		order << (qobject_cast<QTabBar*>(focused) ? QStringLiteral("tabs") : focused->objectName());
		pressTab(focused);
	}
	// Tab goes from the header to the tabs and their table
	QCOMPARE(order, QStringList({"areaCapacityPanelToggle", "tabs", "areaCapacityResources"}));
	exportButton->setFocus(Qt::TabFocusReason);
	pressTab(exportButton);
	// Tab leaves the export, the last control of the panel
	QVERIFY(QApplication::focusWidget() != exportButton);
	QVERIFY(!panel->isAncestorOf(QApplication::focusWidget()));
}

// Under the style sheet of the application the wrapped statuses and the tall Chain tab do not squeeze the header to its
// smallest height.
void TestAreaCapacityWindow::keepsHeaderHeightUnderStyleSheet() {
	QFile stylesheet(QStringLiteral(EGTRAIN_DIALOG_QSS));
	QVERIFY2(stylesheet.open(QIODevice::ReadOnly), "the style sheet opens");
	qApp->setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
	const auto clearStyleSheet = qScopeGuard([] { qApp->setStyleSheet(QString()); });
	const CompletedRunData data = lineRun();
	AreaCapacityWindow window = makeWindow(analyze(data, lineScope({"F1", "F2"})));
	window.show();
	QApplication::processEvents();
	const auto* header = window.findChild<QWidget*>("resultHeader");
	// the header keeps its height above the statuses
	if (header == nullptr)
		QFAIL("the header keeps its height above the statuses");
	QCOMPARE(header->height(), header->sizeHint().height());
}

// At 1.5 times the font in a short window the body of the dialog scrolls to the panel, which keeps its figures whole, and
// Close stays below the body.
void TestAreaCapacityWindow::scrollsBodyWithLargeFont() {
	const QFont original = QApplication::font();
	const auto restoreFont = qScopeGuard([&original] { QApplication::setFont(original); });
	QFont font = original;
	font.setPointSizeF(font.pointSizeF() * 1.5);
	QApplication::setFont(font);
	AreaCapacityWindow window = makeWindow(analyze(lineRun(), lineScope({"F1", "F2"})));
	window.show();
	window.resize(window.width(), 220);
	QApplication::processEvents();
	auto* scroll = window.findChild<QScrollArea*>("dialogBodyScroll");
	auto* panel = window.findChild<QWidget*>("resultPanel");
	auto* close = window.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Close);
	// the body scrolls to the whole panel and Close stays in the window
	if (scroll == nullptr)
		QFAIL("the body scrolls to the whole panel and Close stays in the window");
	if (panel == nullptr)
		QFAIL("the body scrolls to the whole panel and Close stays in the window");
	if (close == nullptr)
		QFAIL("the body scrolls to the whole panel and Close stays in the window");
	QCOMPARE_GT(scroll->verticalScrollBar()->maximum(), 0);
	QCOMPARE_GE(panel->height(), panel->minimumSizeHint().height());
	QVERIFY2(close->isVisible(), qPrintable(QString("the body scrolls to the whole panel and Close stays in the window: range %1, panel %2 of %3").arg(scroll->verticalScrollBar()->maximum()).arg(panel->height()).arg(panel->minimumSizeHint().height())));
	QCOMPARE_LE(close->mapTo(&window, QPoint(0, close->height())).y(), window.height());
}

void TestAreaCapacityWindow::exportsCsvWithProvenance() {
	QTemporaryDir directory;
	QVERIFY2(directory.isValid(), "temporary directory");
	const CompletedRunData data = lineRun();
	AreaCapacityWindow window = makeWindow(analyze(data, lineScope({"F1", "F2"})));
	const QString path = QDir(directory.path()).filePath("area_capacity.csv");
	QVERIFY2(window.exportCsvTo(path), "the export is written");
	const QStringList rows = lines(path);
	// header and records
	QCOMPARE(rows.size(), 1 + 1 + 8 + 1 + 1);
	QVERIFY(rows[0].startsWith("Record type,Area,Traffic,Period[s],Resource,"));
	// Cycle 471.5 s is 13.097... percent of 3600 s. The envelope 1147.5 s is 31.87... percent. The first and the
	// last train of the cycle are in the leader and follower columns.
	// the summary record has the cycle and the legacy envelope
	QCOMPARE_GT(rows.size(), 1);
	QVERIFY(rows[1].startsWith("summary,R / main / @0-B0@ to @7-B0@,partial traffic,3600,,,,,,,,F1,F2,,,,,471.5,13.09722"));
	QVERIFY(rows[1].contains(",1147.5,31.87"));
	QVERIFY(rows[1].endsWith(",,,,"));
	const auto record = [&rows](const QString& prefix) {
		for (const QString& row : rows)
			if (row.startsWith(prefix))
				return row;
		return QString();
	};
	// 174 s is 87 s of F1 and 87 s of F2 on @0. The rate 174 / 3600 is 4.83 percent.
	// a resource record
	QCOMPARE_GT(record("resource,R / main / @0-B0@ to @7-B0@,partial traffic,3600,@0-B0@,2,2,174,174,4.83").size(), 0);
	QVERIFY2(record("resource,R / main / @0-B0@ to @7-B0@,partial traffic,3600,@3-B0@,2,2,1033,701.5,28.69").endsWith(",yes" + QString(14, ',')),
		"the governing resource record");
	QVERIFY2(record("pair,").contains(",F1,F2,60,471.5,-411.5,"), "the pair record");
	QVERIFY2(record("note,").contains(",Unselected traffic,G,,"), "the note record");

	QFile sidecar(path + ".provenance.json");
	QVERIFY2(sidecar.open(QIODevice::ReadOnly), "the provenance sidecar is written");
	const QJsonObject run = QJsonDocument::fromJson(sidecar.readAll()).object().value("run").toObject();
	// the sidecar names the run
	QCOMPARE(run.value("case_name").toString(), "Case A");
	QCOMPARE(run.value("applied_scenario").toString(), "Scenario B");

	AreaCapacityWindow empty = makeWindow(AreaCapacityResult());
	const QString emptyPath = QDir(directory.path()).filePath("empty.csv");
	// a result without a resource row writes nothing
	QVERIFY(!empty.exportCsvTo(emptyPath));
	QVERIFY(!QFile::exists(emptyPath));
	QVERIFY(!QFile::exists(emptyPath + ".provenance.json"));
	QVERIFY2(areaCapacityCsv(AreaCapacityResult(), "area").empty(), "no export text without a resource row");
}

// After the save dialog of the export closes, cancelled or saved, the window is raised and active again, also when the
// main window took the activation while the dialog was open, as AppKit gives it on macOS.
void TestAreaCapacityWindow::keepsWindowInFrontAfterExport() {
	QTemporaryDir directory;
	QMainWindow mainWindow;
	mainWindow.resize(500, 400);
	mainWindow.show();
	const CompletedRunData data = lineRun();
	auto* window = new AreaCapacityWindow(analyze(data, lineScope({"F1", "F2"})), "R / main / @0-B0@ to @7-B0@", data.provenance, &mainWindow);
	window->show();
	window->activateWindow();
	auto* exportButton = window->findChild<QPushButton*>("areaCapacityExport");
	// the window is active before the export
	QVERIFY(directory.isValid());
	if (exportButton == nullptr)
		QFAIL("the window is active before the export");
	QVERIFY(exportButton->isEnabled());
	QVERIFY(waitFor([window] { return QApplication::activeWindow() == window; }));
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
		const char* message = path.isEmpty() ? "a cancelled export leaves the window active, though the main window took the activation"
											 : "a saved export leaves the window active, though the main window took the activation";
		QVERIFY2(mainActive, message);
		QVERIFY2(active, message);
	}
	QVERIFY2(QFile::exists(saved), "the saved export is written");
}

// The export asks for a CSV file under the name area_capacity.csv. A write that fails says so with the path as the
// person reads it, here in the Windows form.
void TestAreaCapacityWindow::asksForCsvFileOnExport() {
	QTemporaryDir directory;
	const CompletedRunData data = lineRun();
	AreaCapacityWindow window(analyze(data, lineScope({"F1", "F2"})), "R / main / @0-B0@ to @7-B0@", data.provenance);
	window.show();
	QPushButton* button = window.findChild<QPushButton*>("areaCapacityExport");
	// the window has its export
	QVERIFY(directory.isValid());
	if (button == nullptr)
		QFAIL("the window has its export");
	QVERIFY(button->isEnabled());
	const QChar separator = display_path::separator();
	display_path::separator() = QLatin1Char('\\');
	const QString path = QDir(directory.path()).filePath("missing/area.csv");
	const ExportDialog shown = answerExport(button, path);
	const QString expected = "Could not save the data to:\n" + display_path::format(path);
	display_path::separator() = separator;
	// the export asks for a CSV file
	QCOMPARE(shown.title, "Export Data");
	QCOMPARE(shown.filters, QStringList("CSV File (*.csv)"));
	QCOMPARE(shown.suggested, "area_capacity.csv");
	// a file that cannot be written is named as the person reads it
	QCOMPARE(shown.warnings, QStringList(expected));
}

void TestAreaCapacityWindow::choosesScopeInDialog() {
	const CompletedRunData data = lineRun();
	AreaCapacityScopeDialog dialog(data);
	dialog.show();
	QApplication::processEvents();
	auto* list = dialog.findChild<QListWidget*>("areaCapacityOccurrences");
	auto* buttons = dialog.findChild<QDialogButtonBox*>();
	QPushButton* accept = buttons ? buttons->button(QDialogButtonBox::Ok) : nullptr;
	// the dialog has its widgets
	if (list == nullptr)
		QFAIL("the dialog has its widgets");
	if (accept == nullptr)
		QFAIL("the dialog has its widgets");
	QVERIFY(combo(dialog, "areaCapacityRoute"));
	QVERIFY(combo(dialog, "areaCapacityLastBlock"));

	// The run lists F2 first. The dialog sorts by reference time, and G has none.
	// the trains that use the area, in the order of their reference time
	QCOMPARE(listed(dialog), QStringList({"F1", "F2", "G"}));
	AreaCapacityScope scope = dialog.scope();
	// the default scope is the whole first route with every listed train checked
	QCOMPARE(scope.routeIndex, 0);
	QCOMPARE(scope.blockIds.size(), std::size_t(8));
	QCOMPARE(scope.blockIds.front(), "@0-B0@");
	QCOMPARE(scope.secondRouteIndex, -1);
	QVERIFY(scope.secondBlockIds.empty());
	QCOMPARE_EQ(scope.periodSeconds, 3600.0);
	QCOMPARE(scope.occurrenceIds, std::vector<std::string>({"F1", "F2", "G"}));
	QVERIFY(scope.cycleEndOccurrenceId.empty());
	// every train is checked, the reference time is in the tooltip and a missing one is said
	QCOMPARE(list->item(0)->checkState(), Qt::Checked);
	QCOMPARE(list->item(0)->text(), "F1");
	QCOMPARE(list->item(0)->toolTip(), "Reference time: planned departure at A");
	QCOMPARE(list->item(2)->text(), "G, no reference time");
	// the scope numbers its blocks in the route
	QCOMPARE(scope.sectionIndices, std::vector<int>({0, 1, 2, 3, 4, 5, 6, 7}));

	// Cycle-closing train: F2 is after the first checked train. With F1 unchecked it is the first one.
	combo(dialog, "areaCapacityCycleEnd")->setCurrentIndex(2);
	// a cycle-closing train after the first one is accepted
	QCOMPARE(dialog.scope().cycleEndOccurrenceId, "F2");
	QVERIFY(accept->isEnabled());
	list->item(0)->setCheckState(Qt::Unchecked);
	const auto* status = dialog.findChild<QLabel*>("areaCapacityScopeStatus");
	// a cycle-closing train that is first is refused inline
	QVERIFY(!accept->isEnabled());
	if (status == nullptr)
		QFAIL("a cycle-closing train that is first is refused inline");
	QVERIFY(status->isVisible());
	QVERIFY(status->text().contains("cycle-closing"));
	list->item(0)->setCheckState(Qt::Checked);
	// the refusal goes away when the train is checked again
	QVERIFY(accept->isEnabled());
	QVERIFY(status->isHidden());

	// A cycle-closing train that runs the other way is accepted with a warning, because the chain leaves it out.
	combo(dialog, "areaCapacityCycleEnd")->setCurrentIndex(3);
	// a cycle-closing train that runs the other way is warned about
	QCOMPARE(dialog.scope().cycleEndOccurrenceId, "G");
	QVERIFY(accept->isEnabled());
	QVERIFY(status->isVisible());
	QVERIFY(status->text().contains("other direction"));
	combo(dialog, "areaCapacityCycleEnd")->setCurrentIndex(2);
	// the warning goes away with another cycle-closing train
	QVERIFY(accept->isEnabled());
	QVERIFY(status->isHidden());

	// Moving a row, as a drop does, changes the order of the chain and checks the cycle-closing train again: F2 is first now.
	QVERIFY2(list->model()->moveRow(QModelIndex(), 0, QModelIndex(), 2), "a row moves");
	// the displayed order is the order of the chain
	QCOMPARE(dialog.scope().occurrenceIds, std::vector<std::string>({"F2", "F1", "G"}));
	// moving a row checks the cycle-closing train against the new order
	QVERIFY(!accept->isEnabled());
	QVERIFY(status->isVisible());
	QVERIFY(status->text().contains("cycle-closing"));

	// A narrower range of the first route leaves G out; the second route brings it back.
	combo(dialog, "areaCapacityLastBlock")->setCurrentIndex(3);
	// a narrower range lists the trains that use it
	QCOMPARE(listed(dialog), QStringList({"F1", "F2"}));
	QCOMPARE(dialog.scope().blockIds.size(), std::size_t(4));
	// a new list has no cycle-closing train yet
	QVERIFY(dialog.scope().cycleEndOccurrenceId.empty());
	QVERIFY(accept->isEnabled());
	combo(dialog, "areaCapacitySecondRoute")->setCurrentIndex(2);
	scope = dialog.scope();
	// a second route adds its range to the area
	QCOMPARE(scope.secondRouteIndex, 1);
	QCOMPARE(scope.secondBlockIds, std::vector<std::string>({"@7-B0@", "@6-B0@"}));
	QCOMPARE(listed(dialog), QStringList({"F1", "F2", "G"}));
	combo(dialog, "areaCapacitySecondRoute")->setCurrentIndex(0);
	// no second route again
	QVERIFY(dialog.scope().secondBlockIds.empty());
	QVERIFY(!combo(dialog, "areaCapacitySecondFirstBlock")->isEnabled());

	list->item(0)->setCheckState(Qt::Unchecked);
	list->item(1)->setCheckState(Qt::Unchecked);
	// with no train checked the dialog warns and still accepts
	QVERIFY(accept->isEnabled());
	QVERIFY(dialog.scope().occurrenceIds.empty());
	QVERIFY(status->isVisible());
	accept->click();
	// OK accepts
	QCOMPARE(dialog.result(), QDialog::Accepted);

	CompletedRunData noRoutes;
	AreaCapacityScopeDialog empty(noRoutes);
	empty.show();
	QApplication::processEvents();
	auto* emptyButtons = empty.findChild<QDialogButtonBox*>();
	// a run without a route cannot be accepted
	if (emptyButtons == nullptr)
		QFAIL("a run without a route cannot be accepted");
	QVERIFY(!emptyButtons->button(QDialogButtonBox::Ok)->isEnabled());
	QCOMPARE(empty.scope().routeIndex, -1);
	QVERIFY(empty.scope().blockIds.empty());
}

// Scopes that give no figures or leave out traffic, built as the dialog builds them.
void TestAreaCapacityWindow::reportsScopesWithoutFigures() {
	const CompletedRunData data = lineRun();

	// An area without a resource is reported and nothing is computed or exported.
	AreaCapacityScope noArea = lineScope({"F1", "F2"});
	noArea.routeIndex = -1;
	noArea.blockIds.clear();
	AreaCapacityWindow empty = makeWindow(analyze(data, noArea));
	const auto* emptyStatuses = empty.findChild<QLabel*>("areaCapacityStatuses");
	const auto* emptyExport = empty.findChild<QPushButton*>("areaCapacityExport");
	// an area without a resource is reported
	QCOMPARE(figure(empty, "areaCapacityCycle"), "- | Chain cannot be analysed");
	QCOMPARE(table(empty, "areaCapacityResources")->rowCount(), 0);
	if (emptyStatuses == nullptr)
		QFAIL("an area without a resource is reported");
	QCOMPARE(emptyStatuses->text(), "Statuses: Empty scope (1)");
	if (emptyExport == nullptr)
		QFAIL("an area without a resource is reported");
	QVERIFY(!emptyExport->isEnabled());

	// A period of zero gives no percentage anywhere and an empty field in the export.
	AreaCapacityScope zero = lineScope({"F1", "F2", "G"});
	zero.periodSeconds = 0.0;
	const AreaCapacityResult noPeriod = analyze(data, zero);
	AreaCapacityWindow window = makeWindow(noPeriod);
	window.show();
	QApplication::processEvents();
	// the figures of a period of zero
	QCOMPARE(cycleFigures(window), "471.5 s | Cycle, - | No valid period");
	const auto* summary = window.findChild<QLabel*>("areaCapacityChainSummary");
	// the chain tab of a period of zero, with the reference times in the tooltip
	if (summary == nullptr)
		QFAIL("the chain tab of a period of zero, with the reference times in the tooltip");
	QVERIFY(summary->text().startsWith("Cycle 471.5 s, no valid period, from F1 to "));
	QVERIFY(summary->toolTip().startsWith("F1: planned departure at A"));
	const QTableWidget* resources = table(window, "areaCapacityResources");
	const int governing = rowOf(resources, "3-B0");
	// the rate of a period of zero is not available
	QCOMPARE_GE(governing, 0);
	QCOMPARE(cell(resources, governing, 1), "-");
	QCOMPARE(cell(resources, governing, 2), "governing resource");
	const auto* governingFigure = window.findChild<ResultFigure*>("areaCapacityGoverning");
	const auto* statuses = window.findChild<QLabel*>("areaCapacityStatuses");
	// the governing resource and the statuses of a period of zero
	QCOMPARE(figure(window, "areaCapacityGoverning"), "- | Governing: 3-B0");
	QVERIFY(governingFigure->toolTip().contains("No valid period"));
	if (statuses == nullptr)
		QFAIL("the governing resource and the statuses of a period of zero");
	QCOMPARE(statuses->text(), "Statuses: Invalid period (1), Opposing train (1)");
	const std::string csvText = areaCapacityCsv(noPeriod, "area");
	// the export leaves the period and the rate empty
	QCOMPARE_NE(csvText.find("summary,area,complete traffic,,"), std::string::npos);
	QCOMPARE_NE(csvText.find("resource,area,complete traffic,,@3-B0@,2,2,1033,701.5,,yes,"), std::string::npos);

	// A cycle-closing train that runs the other way is not in the chain.
	AreaCapacityScope opposing = lineScope({"F1", "F2", "G"});
	opposing.cycleEndOccurrenceId = "G";
	AreaCapacityWindow other = makeWindow(analyze(data, opposing));
	const auto* otherSummary = other.findChild<QLabel*>("areaCapacityChainSummary");
	// an opposing cycle-closing train is left out of the chain and listed as opposing
	QCOMPARE(figure(other, "areaCapacityCycle"), "- | No cycle-closing train");
	if (otherSummary == nullptr)
		QFAIL("an opposing cycle-closing train is left out of the chain and listed as opposing");
	QCOMPARE(otherSummary->text(), "No cycle-closing train was chosen.");
	QCOMPARE(rowOf(table(other, "areaCapacityExcluded"), "Opposing train"), 0);

	// A cycle-closing train that shares no resource with an earlier train of the chain closes no cycle.
	const auto chained = [](const char* id, const char* resource, double start) {
		AreaCapacityTrain value;
		value.train.runtimeId = id;
		value.train.profileReferenceTime = start;
		value.train.occupations = {occupation(resource, start, start + 20.0, start)};
		return value;
	};
	AreaCapacityInput unshared;
	unshared.resourceIds = {"X", "Y"};
	unshared.selected = {chained("A", "X", 0.0), chained("B", "Y", 20.0)};
	unshared.periodSeconds = 3600.0;
	unshared.cycleEndIdentity = "B";
	AreaCapacityWindow noCycle = makeWindow(analyzeAreaCapacity(unshared));
	const auto* noCycleSummary = noCycle.findChild<QLabel*>("areaCapacityChainSummary");
	// a cycle-closing train that shares no resource with an earlier train closes no cycle
	QCOMPARE(figure(noCycle, "areaCapacityCycle"), "- | No cycle");
	QCOMPARE(figure(noCycle, "areaCapacityShare"), "- | of 1 h");
	if (noCycleSummary == nullptr)
		QFAIL("a cycle-closing train that shares no resource with an earlier train closes no cycle");
	QCOMPARE(noCycleSummary->text(), "Train B shares no resource with an earlier train of the chain, so it closes no cycle.");

	// A train that occupies a resource twice counts both occupations and is noted.
	CompletedRunData repeated = data;
	repeated.trains[1].blockTimes.push_back(occupation("@0-B0@", 400.0, 450.0));
	AreaCapacityWindow twice = makeWindow(analyze(repeated, lineScope({"F1", "F2"})));
	const QTableWidget* twiceResources = table(twice, "areaCapacityResources");
	const int first = rowOf(twiceResources, "0-B0");
	const QTableWidget* twiceNotes = table(twice, "areaCapacityNotes");
	const int note = rowOf(twiceNotes, "Repeated resource");
	// a repeated resource is counted twice and noted
	QCOMPARE_GE(first, 0);
	QCOMPARE(cell(twiceResources, first, 3), "2");
	QCOMPARE(cell(twiceResources, first, 4), "3");
	QCOMPARE(cell(twiceResources, first, 5), "224");
	QCOMPARE_GE(note, 0);
	QCOMPARE(cell(twiceNotes, note, 1), "F1");
	QCOMPARE(cell(twiceNotes, note, 3), "0-B0");
}

void TestAreaCapacityWindow::opensWindowFromOptions() {
	const CompletedRunData data = lineRun();
	answerOptions(QDialogButtonBox::Cancel);
	// cancelling the options opens no window
	QCOMPARE(openAreaCapacityWindow(nullptr, data), nullptr);
	// Every listed train is checked and no cycle-closing train is chosen. G runs the other way, so the traffic is complete.
	answerOptions(QDialogButtonBox::Ok);
	QPointer<AreaCapacityWindow> window = openAreaCapacityWindow(nullptr, data);
	// the options open a window that is not modal and deletes itself
	if (window.isNull())
		QFAIL("the options open a window that is not modal and deletes itself");
	QVERIFY(window->isVisible());
	QVERIFY(!window->isModal());
	QVERIFY(window->testAttribute(Qt::WA_DeleteOnClose));
	// the window shows the chosen scope
	QCOMPARE(figure(*window, "areaCapacityCycle"), "- | No cycle-closing train");
	if (window)
		window->close();
	QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QVERIFY2(window.isNull(), "closing the window deletes it");
}

// The options and the window name the resources by their labels. The export and the tooltips keep the ids.
void TestAreaCapacityWindow::namesResourcesByLabels() {
	const CompletedRunData data = labelledRun();
	AreaCapacityScope scope = lineScope({"F1", "F2"});
	scope.secondRouteIndex = 1;
	scope.secondBlockIds = {"@7-B0@", "@6-B0@"};
	scope.secondSectionIndices = {0, 1};
	// the window names the area by the numbers of its blocks, the export by their ids
	QCOMPARE(areaCapacityText(data, scope), "R (main), blocks 1 to 8 and S, blocks 1 to 2");
	QCOMPARE(areaCapacityLabel(data, scope), "R / main / @0-B0@ to @7-B0@ and S / @7-B0@ to @6-B0@");

	AreaCapacityScopeDialog dialog(data);
	const QComboBox* first = combo(dialog, "areaCapacityFirstBlock");
	// the options name the blocks by their labels, with the short id as tooltip
	if (first == nullptr)
		QFAIL("the options name the blocks by their labels, with the short id as tooltip");
	QCOMPARE(first->itemText(0), "1: B0 0.000 to 0.230 km");
	QCOMPARE(first->itemData(0, Qt::ToolTipRole).toString(), "0-B0");
	QCOMPARE(combo(dialog, "areaCapacityLastBlock")->currentText(), "8: B0 1.610 to 1.840 km");

	const AreaCapacityScope one = lineScope({"F1", "F2"});
	AreaCapacityWindow window(analyze(data, one), areaCapacityLabel(data, one), data.provenance, nullptr, areaCapacityText(data, one),
		data.sectionLabels);
	// the header names the area by its block numbers and the panel the governing resource by its label
	QCOMPARE(context(window), "Case A / Scenario B \u00b7 R (main), blocks 1 to 8 \u00b7 partial traffic");
	QCOMPARE(figure(window, "areaCapacityGoverning"), "28.7% | Governing: B0 0.690 to 0.920 km");
	// Tied governing resources are all named, with semicolons between their labels, since a label can hold a comma.
	AreaCapacityResult tied = analyze(data, one);
	for (AreaCapacityResource& resource : tied.resources)
		resource.governing = resource.resourceId == "@3-B0@" || resource.resourceId == "@4-B0@";
	const AreaCapacityWindow tiedWindow(tied, areaCapacityLabel(data, one), data.provenance, nullptr, areaCapacityText(data, one),
		data.sectionLabels);
	const QString tiedCaption = "Governing: " + QString::fromStdString(data.sectionLabels.label("@3-B0@") + "; " + data.sectionLabels.label("@4-B0@"));
	// two governing resources
	QCOMPARE(tiedWindow.findChild<ResultFigure*>("areaCapacityGoverning")->caption(), tiedCaption);
	const QTableWidget* resources = table(window, "areaCapacityResources");
	const int governing = rowOf(resources, "3-B0");
	// a resource has its short id in the table and its label as tooltip
	QCOMPARE_GE(governing, 0);
	QCOMPARE(resources->item(governing, 0)->toolTip(), "B0 0.690 to 0.920 km");
	const QTableWidget* pairs = table(window, "areaCapacityPairs");
	// the evidence names the blocks by their short ids and its tooltip by their labels
	if (pairs == nullptr)
		QFAIL("the evidence names the blocks by their short ids and its tooltip by their labels");
	QCOMPARE(pairs->rowCount(), 1);
	QVERIFY2(cell(pairs, 0, 7).contains("-B0 / "),
		qPrintable("the evidence names the blocks by their short ids and its tooltip by their labels: " + cell(pairs, 0, 7)));
	QVERIFY2(!cell(pairs, 0, 7).contains("@"),
		qPrintable("the evidence names the blocks by their short ids and its tooltip by their labels: " + cell(pairs, 0, 7)));
	QVERIFY2(!pairs->item(0, 7)->toolTip().isEmpty(),
		qPrintable("the evidence names the blocks by their short ids and its tooltip by their labels: " + cell(pairs, 0, 7)));
	QVERIFY2(!pairs->item(0, 7)->toolTip().contains("@"),
		qPrintable("the evidence names the blocks by their short ids and its tooltip by their labels: " + cell(pairs, 0, 7)));
	QVERIFY2(pairs->item(0, 7)->toolTip().contains("B0 "),
		qPrintable("the evidence names the blocks by their short ids and its tooltip by their labels: " + cell(pairs, 0, 7)));
	const QTableWidget* compression = table(window, "areaCapacityCompression");
	int predecessorRow = -1;
	for (int row = 0; compression && row < compression->rowCount(); ++row)
		if (predecessorRow < 0 && cell(compression, row, 6).contains(" / "))
			predecessorRow = row;
	// the governing predecessor names its blocks by their short ids and its tooltip by their labels
	if (compression == nullptr)
		QFAIL("the governing predecessor names its blocks by their short ids and its tooltip by their labels");
	QCOMPARE_GE(predecessorRow, 0);
	QVERIFY(!cell(compression, predecessorRow, 6).contains("@"));
	QVERIFY(compression->item(predecessorRow, 6)->toolTip().contains("B0 "));
	QVERIFY(!compression->item(predecessorRow, 6)->toolTip().contains("@"));
	const QTableWidget* critical = table(window, "areaCapacityCritical");
	// The tables of the chain have the columns of the Capacity window, in the words of an area.
	QStringList pairHeaders;
	QStringList criticalHeaders;
	for (int column = 0; pairs && column < pairs->columnCount(); ++column)
		pairHeaders << pairs->horizontalHeaderItem(column)->text();
	for (int column = 0; critical && column < critical->columnCount(); ++column)
		criticalHeaders << critical->horizontalHeaderItem(column)->text();
	// the columns name resources
	QCOMPARE(pairHeaders.value(7), "Governing resource");
	QCOMPARE(criticalHeaders, QStringList({"Leader", "Follower", "Leader resource", "Follower resource", "Gap (s)"}));
	QStringList headings;
	for (const QLabel* label : window.findChildren<QLabel*>())
		if (label->text().startsWith("Pairs") || label->text().startsWith("Compression") || label->text().startsWith("Critical"))
			headings << label->text();
	// the headings of the chain
	QCOMPARE(headings, QStringList({"Pairs (1)", "Compression (2)", QString("Critical resources (%1)").arg(critical ? critical->rowCount() : -1)}));
	// a critical resource has its short id and its label as tooltip
	if (critical == nullptr)
		QFAIL("a critical resource has its short id and its label as tooltip");
	QCOMPARE_GT(critical->rowCount(), 0);
	QVERIFY(cell(critical, 0, 2).endsWith("-B0"));
	QVERIFY(!cell(critical, 0, 2).contains("@"));
	QCOMPARE(critical->item(0, 2)->toolTip(), QString::fromStdString(data.sectionLabels.label("@" + cell(critical, 0, 2).toStdString() + "@")));
	QCOMPARE(critical->item(0, 3)->toolTip(), QString::fromStdString(data.sectionLabels.label("@" + cell(critical, 0, 3).toStdString() + "@")));
	QVERIFY(critical->item(0, 2)->toolTip().startsWith("B0 "));
	AreaCapacityResult noted;
	noted.notes.push_back({AreaCapacityNoteKind::RepeatedResource, "F1", "", "@7-B0@"});
	AreaCapacityWindow notedWindow(noted, areaCapacityLabel(data, one), data.provenance, nullptr, QString(), data.sectionLabels);
	const QTableWidget* notes = table(notedWindow, "areaCapacityNotes");
	// a note names its resource by its short id with its label as tooltip, and its details are their own tooltip
	if (notes == nullptr)
		QFAIL("a note names its resource by its short id with its label as tooltip, and its details are their own tooltip");
	QCOMPARE(notes->rowCount(), 1);
	QCOMPARE(cell(notes, 0, 3), "7-B0");
	QCOMPARE(notes->item(0, 3)->toolTip(), "B0 1.610 to 1.840 km");
	QVERIFY2(cell(notes, 0, 4).contains("resource 7-B0 "),
		qPrintable("a note names its resource by its short id with its label as tooltip, and its details are their own tooltip: " + cell(notes, 0, 4)));
	QVERIFY2(!cell(notes, 0, 4).contains("@"),
		qPrintable("a note names its resource by its short id with its label as tooltip, and its details are their own tooltip: " + cell(notes, 0, 4)));
	QCOMPARE(notes->item(0, 4)->toolTip(), cell(notes, 0, 4));
	// a tab title gives its count only when it is not 0
	QCOMPARE(tabTitle(notedWindow, 2), "Excluded traffic");
	QCOMPARE(tabTitle(notedWindow, 3), "Notes (1)");

	QTemporaryDir directory;
	const QString path = QDir(directory.path()).filePath("area_capacity.csv");
	// the export names the area and the resources by their ids
	QVERIFY(directory.isValid());
	QVERIFY(window.exportCsvTo(path));
	QCOMPARE_GT(lines(path).size(), 1);
	QVERIFY(lines(path)[1].startsWith("summary,R / main / @0-B0@ to @7-B0@,"));
	QVERIFY(!lines(path).join("\n").contains("B0 0."));
}

QTEST_MAIN(TestAreaCapacityWindow)
#include "test_areacapacitywindow.moc"
