#include "editors/PassengerDemandPanel.h"

#include "scene/SceneDiagnostic.h"
#include "scene/SceneModel.h"
#include "support/FakePrompts.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QTest>

#include <memory>
#include <string>
#include <vector>

static ScenePassengerDemandSlice slice(const char* id, double start, double end,
	std::vector<ScenePassengerDemandCell> cells = {}) {
	ScenePassengerDemandSlice value;
	value.id = id;
	value.startSeconds = start;
	value.endSeconds = end;
	value.cells = std::move(cells);
	return value;
}

static constexpr double hour = 3600.0;

// Three stations A, B and C, a base time of 06:00:00 and a run of three hours. Slice "morning" covers 07:00 to 08:00
// and holds A to B 10 and B to C 5.
static SceneModel demandScene() {
	SceneModel scene;
	scene.name = "Demand case";
	scene.baseTime = "06:00:00";
	scene.settings.hasDuration = true;
	scene.settings.durationSeconds = 3.0 * hour;
	for (const char* id : {"A", "B", "C"}) {
		SceneStation station;
		station.id = id;
		station.name = std::string("Station ") + id;
		scene.stations.push_back(station);
	}
	scene.passengerDemand = {slice("morning", 7 * hour, 8 * hour, {{"A", "B", 10}, {"B", "C", 5}})};
	return scene;
}

// The panel and its document, with the label of every change and a copy of the model before it, which is what the window
// keeps for Undo.
struct DemandFixture {
	SceneDocument document;
	FakePrompts prompts;
	PassengerDemandPanel panel;
	std::vector<std::string> labels;
	std::vector<SceneModel> before;
	std::vector<std::string> status;
	int changedCount = 0;

	DemandFixture() {
		QObject::connect(&document, &SceneDocument::aboutToEdit, &panel, [this] { before.push_back(document.model()); });
		QObject::connect(&document, &SceneDocument::changed, &panel, [this] { labels.push_back(document.lastChangeLabel().toStdString()); });
		QObject::connect(&panel, &PassengerDemandPanel::changed, &panel, [this] { ++changedCount; });
		QObject::connect(&panel, &PassengerDemandPanel::statusMessage, &panel,
			[this](const QString& text, int) { status.push_back(text.toStdString()); });
		panel.setPrompts(&prompts);
		panel.setDocument(&document);
		document.replace(demandScene(), false);
		panel.resize(640, 640);
		panel.timeline()->resize(600, panel.timeline()->sizeHint().height());
	}

	// Restores the model before the latest edit, as Undo does, and returns the model that Redo brings back.
	SceneModel undo() {
		const SceneModel after = document.model();
		document.restore(before.back(), false);
		before.pop_back();
		panel.refresh();
		return after;
	}
	void redo(const SceneModel& after) {
		document.restore(after, false);
		panel.refresh();
	}

	const std::vector<ScenePassengerDemandSlice>& slices() const { return document.model().passengerDemand; }

	QTableWidgetItem* cell(int row, int column) const { return panel.matrixTable()->item(row, column); }
	void setCell(int row, int column, const QString& text) { cell(row, column)->setText(text); }

	// A drag on the timeline from one time to another, with the mouse events of a user.
	void drag(double fromSeconds, double toSeconds) {
		DemandTimeline* timeline = panel.timeline();
		const QPoint from(static_cast<int>(timeline->xOfSeconds(fromSeconds)), timeline->barsY());
		const QPoint to(static_cast<int>(timeline->xOfSeconds(toSeconds)), timeline->barsY());
		QTest::mousePress(timeline, Qt::LeftButton, Qt::NoModifier, from);
		QTest::mouseMove(timeline, (from + to) / 2);
		QTest::mouseMove(timeline, to);
		QTest::mouseRelease(timeline, Qt::LeftButton, Qt::NoModifier, to);
	}
};

// What the user does to a text field: type, then leave the field.
static void typeAndLeave(QLineEdit* edit, const QString& text) {
	edit->setText(text);
	emit edit->editingFinished();
}

static std::string role(const QLabel* label) {
	if (label->property("dialogHelp").toBool())
		return "help";
	return label->property("dialogStatus").toString().toStdString();
}

class TestDemandPanel : public QObject {
	Q_OBJECT

private slots:
	void rangeRules();
	void demandCountsAsPassengers();
	void isDisabledWithoutSceneOrStations();
	void showsSlicesAndMatrix();
	void addsSlicesAndUndoes();
	void draggingCreatesSlices();
	void refusesOverlaps();
	void movesEdgesByDragging();
	void editsPeriodThroughFields();
	void editsCells();
	void showsTotals();
	void lowersAboveLimit();
	void importsBelowLimitWhenSmaller();
	void mergesDuplicateCells();
	void resetsBoldAfterNewStation();
	void typedPeriodKeepsMinimumLength();
	void importsCsv();
	void namesCsvPeriodMismatch();
	void showsCsvErrors();
	void exportsCsvRoundTrip();
	void deletesSlices();
	void showsSlicesOutsideHorizon();
	void showsValidationMessages();
	void takesPendingEditors();
	void clearsCellWithReturn();
	void commitsCellWithReturn();
	void grabsPaimpol();
	void refreshesStructureWithStations();
	void showsOtherSliceWithoutStaleText();
	void restoresCellTextAndFlags();
	void showsChangesMadeWhileHidden();
	void fillsShownMatrixOfManyStationsQuickly();
	void sizesColumnsOfShownMatrixAfterFill();
};

// A case with only demand slices has passengers in a run, so the passenger layer and the map key show them.
void TestDemandPanel::demandCountsAsPassengers() {
	SceneModel scene;
	QVERIFY(!scene.hasPassengers());
	scene.passengerDemand.push_back({"slice-1", 3600.0, 7200.0, {{"A", "B", 5}}});
	QVERIFY(scene.hasPassengers());
	scene.passengerDemand.clear();
	scene.passengers.push_back({"p1", {}});
	QVERIFY(scene.hasPassengers());
}

void TestDemandPanel::rangeRules() {
	using namespace DemandRanges;
	QCOMPARE(clockText(25200.0), QString("07:00:00"));
	QCOMPARE(clockText(86400.0), QString("24:00:00"));
	QCOMPARE(parseClock("07:30").value_or(-1), 27000.0);
	QCOMPARE(parseClock("24:00:00").value_or(-1), 86400.0);
	QVERIFY(!parseClock("24:00:01"));
	QVERIFY(!parseClock("7:61"));
	QVERIFY(!parseClock("noon"));
	const std::vector<Bar> bars = {{"a", 3600.0, 7200.0}, {"b", 10800.0, 14400.0}};
	// A drag that starts in the gap is cut off at the neighbours, and one that starts inside a bar gives nothing.
	QCOMPARE(newRange(bars, 8000.0, 20000.0)->second, 10800.0);
	QCOMPARE(newRange(bars, 8000.0, 0.0)->first, 7200.0);
	QVERIFY(!newRange(bars, 5000.0, 9000.0));
	QVERIFY(!newRange(bars, 7200.0, 7210.0));
	const auto moved = movedEdge(bars, "a", false, 20000.0, 60.0);
	QCOMPARE(moved.second, 10800.0);
	QCOMPARE(movedEdge(bars, "a", true, 0.0, 60.0).first, 0.0);
	QCOMPARE(movedEdge(bars, "b", true, 100.0, 60.0).first, 7200.0);
	QCOMPARE(movedEdge(bars, "a", true, 9000.0, 60.0).first, 7140.0);
	const auto free = firstFreeRange(bars, 0.0, 20000.0);
	QCOMPARE(free->first, 0.0);
	QCOMPARE(free->second, 3600.0);
}

void TestDemandPanel::isDisabledWithoutSceneOrStations() {
	PassengerDemandPanel detached;
	QVERIFY(!detached.body()->isEnabled());
	QVERIFY(!detached.addButton()->isEnabled());
	QVERIFY(!detached.matrixTable()->isEnabled());
	QVERIFY(!detached.timeline()->isEnabled());

	DemandFixture fixture;
	QVERIFY(fixture.panel.body()->isEnabled());
	SceneModel noStations = demandScene();
	noStations.stations.clear();
	fixture.document.replace(noStations, false);
	QVERIFY(!fixture.panel.body()->isEnabled());
	QVERIFY(!fixture.panel.addButton()->isEnabled());
	QVERIFY(!fixture.panel.timeline()->isEnabled());
	QVERIFY(fixture.panel.diagnosticLabel()->text().contains("no stations"));
	QVERIFY(!fixture.panel.addSliceInFreeSpace());
	QVERIFY(fixture.labels.empty());

	fixture.document.replace(demandScene(), false);
	fixture.document.setEditable(false);
	fixture.panel.refresh();
	QVERIFY(!fixture.panel.body()->isEnabled());
	fixture.document.setEditable(true);
	fixture.panel.refresh();
	QVERIFY(fixture.panel.body()->isEnabled());
}

void TestDemandPanel::showsSlicesAndMatrix() {
	DemandFixture fixture;
	QCOMPARE(fixture.panel.selectedSlice()->id, std::string("morning"));
	QCOMPARE(fixture.panel.sliceCombo()->count(), 1);
	QVERIFY(fixture.panel.sliceCombo()->itemText(0).contains("07:00-08:00"));
	QCOMPARE(fixture.panel.startEdit()->text(), QString("07:00:00"));
	QCOMPARE(fixture.panel.endEdit()->text(), QString("08:00:00"));
	QCOMPARE(fixture.panel.timeline()->horizonStart(), 6 * hour);
	QCOMPARE(fixture.panel.timeline()->horizonEnd(), 9 * hour);
	const QTableWidget* table = fixture.panel.matrixTable();
	QCOMPARE(table->rowCount(), 4);
	QCOMPARE(table->columnCount(), 4);
	QCOMPARE(table->verticalHeaderItem(0)->text(), QString("Station A"));
	QCOMPARE(table->horizontalHeaderItem(3)->text(), QString("Total"));
	QCOMPARE(fixture.cell(0, 1)->text(), QString("10"));
	QCOMPARE(fixture.cell(1, 2)->text(), QString("5"));
	QCOMPARE(fixture.cell(0, 2)->text(), QString());
	QVERIFY(fixture.cell(0, 1)->flags() & Qt::ItemIsEditable);
	// The diagonal and the totals cannot be edited.
	QVERIFY(!(fixture.cell(1, 1)->flags() & Qt::ItemIsEnabled));
	QVERIFY(!(fixture.cell(0, 3)->flags() & Qt::ItemIsEditable));
	QVERIFY(!fixture.cell(1, 1)->flags().testFlag(Qt::ItemIsEditable));
	QVERIFY(fixture.panel.timeline()->barRect("morning").isValid());
	QCOMPARE(fixture.panel.timeline()->accessibleName(), QString("Demand timeline"));
}

void TestDemandPanel::addsSlicesAndUndoes() {
	DemandFixture fixture;
	QTest::mouseClick(fixture.panel.addButton(), Qt::LeftButton);
	QCOMPARE(fixture.slices().size(), std::size_t(2));
	const ScenePassengerDemandSlice& added = fixture.slices().back();
	QCOMPARE(added.id, std::string("slice-1"));
	// The first free space of the horizon is before the morning slice: 06:00 to 07:00.
	QCOMPARE(added.startSeconds, 6 * hour);
	QCOMPARE(added.endSeconds, 7 * hour);
	QVERIFY(added.cells.empty());
	QCOMPARE(fixture.labels, std::vector<std::string>({"Add demand slice"}));
	QCOMPARE(fixture.changedCount, 1);
	QCOMPARE(fixture.panel.selectedSlice()->id, std::string("slice-1"));
	QCOMPARE(fixture.panel.sliceCombo()->count(), 2);

	QVERIFY(fixture.panel.addSlice(8 * hour, 8.5 * hour));
	QCOMPARE(fixture.slices().back().id, std::string("slice-2"));

	const SceneModel afterSecond = fixture.undo();
	QCOMPARE(fixture.slices().size(), std::size_t(2));
	QCOMPARE(fixture.panel.sliceCombo()->count(), 2);
	const SceneModel afterFirst = fixture.undo();
	QCOMPARE(fixture.slices().size(), std::size_t(1));
	QCOMPARE(fixture.panel.selectedSlice()->id, std::string("morning"));
	fixture.redo(afterFirst);
	QCOMPARE(fixture.slices().size(), std::size_t(2));
	QCOMPARE(fixture.panel.sliceCombo()->count(), 2);
	fixture.redo(afterSecond);
	QCOMPARE(fixture.slices().size(), std::size_t(3));
	QCOMPARE(fixture.panel.sliceCombo()->count(), 3);

	// A used id is skipped.
	fixture.document.mutableModel().passengerDemand.push_back(slice("slice-3", 20 * hour, 21 * hour));
	fixture.document.mutableModel().passengerDemand.push_back(slice("slice-2", 22 * hour, 23 * hour));
	fixture.panel.refresh();
	QVERIFY(fixture.panel.addSlice(12 * hour, 13 * hour));
	QCOMPARE(fixture.slices().back().id, std::string("slice-4"));
}

void TestDemandPanel::draggingCreatesSlices() {
	DemandFixture fixture;
	fixture.drag(8.5 * hour, 9 * hour);
	QCOMPARE(fixture.slices().size(), std::size_t(2));
	const ScenePassengerDemandSlice& added = fixture.slices().back();
	QCOMPARE(added.id, std::string("slice-1"));
	QCOMPARE(added.startSeconds, 8.5 * hour);
	QCOMPARE(added.endSeconds, 9 * hour);
	QCOMPARE(std::fmod(added.startSeconds, 60.0), 0.0);
	QCOMPARE(fixture.labels, std::vector<std::string>({"Add demand slice"}));
	QCOMPARE(fixture.panel.selectedSlice()->id, std::string("slice-1"));

	// Dragging right to left gives the same kind of slice.
	fixture.drag(6.5 * hour, 6.0 * hour);
	QCOMPARE(fixture.slices().size(), std::size_t(3));
	QCOMPARE(fixture.slices().back().startSeconds, 6 * hour);
	QCOMPARE(fixture.slices().back().endSeconds, 6.5 * hour);

	// A click on empty space, which is not a drag, adds nothing.
	fixture.drag(7.9 * hour, 7.9 * hour);
	QCOMPARE(fixture.slices().size(), std::size_t(3));
}

void TestDemandPanel::refusesOverlaps() {
	DemandFixture fixture;
	// Starts in the gap before the morning slice and runs into it: cut off at 07:00.
	fixture.drag(6.25 * hour, 7.5 * hour);
	QCOMPARE(fixture.slices().size(), std::size_t(2));
	QCOMPARE(fixture.slices().back().startSeconds, 6.25 * hour);
	QCOMPARE(fixture.slices().back().endSeconds, 7 * hour);
	// A slice that starts inside another is refused with a message, and nothing changes.
	const std::size_t edits = fixture.labels.size();
	QVERIFY(!fixture.panel.addSlice(7.2 * hour, 7.8 * hour));
	QCOMPARE(fixture.labels.size(), edits);
	QCOMPARE(role(fixture.panel.diagnosticLabel()), std::string("error"));
	QVERIFY(fixture.panel.diagnosticLabel()->text().contains("overlap"));
	// No room is left before 08:00 for a minute at 06:59.
	QVERIFY(!fixture.panel.addSlice(6.99 * hour, 7.2 * hour));
	QCOMPARE(fixture.labels.size(), edits);
	// A press on a bar selects it instead of starting a slice.
	fixture.drag(7.5 * hour, 7.6 * hour);
	QCOMPARE(fixture.slices().size(), std::size_t(2));
	QCOMPARE(fixture.panel.selectedSlice()->id, std::string("morning"));
}

void TestDemandPanel::movesEdgesByDragging() {
	DemandFixture fixture;
	QVERIFY(fixture.panel.addSlice(8.5 * hour, 9 * hour));
	QVERIFY(fixture.panel.selectSlice("morning"));
	// The end edge moves, snapped to a minute, and stops at the neighbour at 08:30.
	fixture.drag(8 * hour, 8.25 * hour);
	QCOMPARE(fixture.slices()[0].endSeconds, 8.25 * hour);
	QCOMPARE(fixture.labels.back(), std::string("Edit demand period"));
	fixture.drag(8.25 * hour, 8.9 * hour);
	QCOMPARE(fixture.slices()[0].endSeconds, 8.5 * hour);
	QCOMPARE(fixture.slices()[0].startSeconds, 7 * hour);
	// The start edge moves too, and cannot pass the end.
	fixture.drag(7 * hour, 6.5 * hour);
	QCOMPARE(fixture.slices()[0].startSeconds, 6.5 * hour);
	fixture.drag(6.5 * hour, 9 * hour);
	QCOMPARE(fixture.slices()[0].startSeconds, 8.5 * hour - 60.0);
	QCOMPARE(fixture.slices().size(), std::size_t(2));
}

void TestDemandPanel::editsPeriodThroughFields() {
	DemandFixture fixture;
	typeAndLeave(fixture.panel.startEdit(), "06:30:15");
	QCOMPARE(fixture.slices()[0].startSeconds, 6 * hour + 1815.0);
	QCOMPARE(fixture.labels, std::vector<std::string>({"Edit demand period"}));
	QCOMPARE(fixture.panel.startEdit()->text(), QString("06:30:15"));
	typeAndLeave(fixture.panel.endEdit(), "09:00");
	QCOMPARE(fixture.slices()[0].endSeconds, 9 * hour);
	QCOMPARE(fixture.panel.endEdit()->text(), QString("09:00:00"));
	// An end that crosses the neighbour is clamped to it, with a message.
	QVERIFY(fixture.panel.addSlice(10 * hour, 11 * hour));
	QVERIFY(fixture.panel.selectSlice("morning"));
	fixture.status.clear();
	typeAndLeave(fixture.panel.endEdit(), "10:30:00");
	QCOMPARE(fixture.slices()[0].endSeconds, 10 * hour);
	QCOMPARE(fixture.panel.endEdit()->text(), QString("10:00:00"));
	QCOMPARE(fixture.status.size(), std::size_t(1));
	// A start that is not a time, or an end before the start, changes nothing.
	const std::size_t edits = fixture.labels.size();
	typeAndLeave(fixture.panel.startEdit(), "soon");
	QCOMPARE(fixture.labels.size(), edits);
	QCOMPARE(role(fixture.panel.diagnosticLabel()), std::string("error"));
	QCOMPARE(fixture.panel.startEdit()->text(), QString("06:30:15"));
	typeAndLeave(fixture.panel.endEdit(), "05:00:00");
	QCOMPARE(fixture.slices()[0].endSeconds, 6 * hour + 1815.0 + DemandRanges::SnapSeconds);
	// One undo step for the edit.
	fixture.undo();
	QCOMPARE(fixture.slices()[0].endSeconds, 10 * hour);
}

void TestDemandPanel::editsCells() {
	DemandFixture fixture;
	fixture.setCell(0, 2, "7");
	QCOMPARE(fixture.slices()[0].cells.size(), std::size_t(3));
	QCOMPARE(fixture.slices()[0].cells.back().originStationId, std::string("A"));
	QCOMPARE(fixture.slices()[0].cells.back().destinationStationId, std::string("C"));
	QCOMPARE(fixture.slices()[0].cells.back().count, 7);
	QCOMPARE(fixture.labels, std::vector<std::string>({"Edit demand"}));
	QCOMPARE(fixture.changedCount, 1);
	// Changing a count keeps the cell in place.
	fixture.setCell(0, 1, "12");
	QCOMPARE(fixture.slices()[0].cells[0].count, 12);
	QCOMPARE(fixture.slices()[0].cells.size(), std::size_t(3));
	// Clearing a cell to 0 or to nothing removes the sparse cell.
	fixture.setCell(0, 1, "0");
	QCOMPARE(fixture.slices()[0].cells.size(), std::size_t(2));
	QCOMPARE(fixture.cell(0, 1)->text(), QString());
	fixture.setCell(0, 2, "");
	QCOMPARE(fixture.slices()[0].cells.size(), std::size_t(1));
	QCOMPARE(fixture.slices()[0].cells[0].originStationId, std::string("B"));
	// The same value again, or 0 for a missing cell, is not an edit.
	const std::size_t edits = fixture.labels.size();
	fixture.setCell(1, 2, "5");
	fixture.setCell(2, 0, "0");
	QCOMPARE(fixture.labels.size(), edits);
	// Text that is not a count is refused and the cell shows the stored value again.
	fixture.setCell(1, 2, "many");
	QCOMPARE(fixture.slices()[0].cells[0].count, 5);
	QCOMPARE(fixture.cell(1, 2)->text(), QString("5"));
	QCOMPARE(role(fixture.panel.diagnosticLabel()), std::string("error"));
	fixture.setCell(1, 2, "-4");
	QCOMPARE(fixture.slices()[0].cells[0].count, 5);
	QCOMPARE(fixture.labels.size(), edits);
	// Undo gives back the cell.
	fixture.undo();
	QCOMPARE(fixture.slices()[0].cells.size(), std::size_t(2));
	// A total above the limit of the scene is refused.
	fixture.setCell(0, 2, QString::number(kMaxScenePassengerDemand));
	QCOMPARE(fixture.slices()[0].cells.size(), std::size_t(2));
	QVERIFY(fixture.panel.diagnosticLabel()->text().contains("together"));
}

void TestDemandPanel::showsTotals() {
	DemandFixture fixture;
	QTableWidget* table = fixture.panel.matrixTable();
	// Rows are origins: A sends 10, B sends 5, C sends 0. Columns are destinations: A gets 0, B gets 10, C gets 5.
	QCOMPARE(fixture.cell(0, 3)->text(), QString("10"));
	QCOMPARE(fixture.cell(1, 3)->text(), QString("5"));
	QCOMPARE(fixture.cell(2, 3)->text(), QString("0"));
	QCOMPARE(fixture.cell(3, 0)->text(), QString("0"));
	QCOMPARE(fixture.cell(3, 1)->text(), QString("10"));
	QCOMPARE(fixture.cell(3, 2)->text(), QString("5"));
	QCOMPARE(fixture.cell(3, 3)->text(), QString("15"));
	QCOMPARE(fixture.panel.totalLabel()->text(), QString("Slice total: 15 passengers"));
	fixture.setCell(2, 0, "20");
	QCOMPARE(fixture.cell(2, 3)->text(), QString("20"));
	QCOMPARE(fixture.cell(3, 0)->text(), QString("20"));
	QCOMPARE(fixture.cell(3, 3)->text(), QString("35"));
	QCOMPARE(fixture.panel.totalLabel()->text(), QString("Slice total: 35 passengers"));
	QCOMPARE(table->rowCount(), 4);
}

// A file with more demand than the limit allows loads, and an edit that does not raise the total is still applied.
void TestDemandPanel::lowersAboveLimit() {
	DemandFixture fixture;
	SceneModel scene = demandScene();
	scene.passengerDemand[0].cells = {{"A", "B", 600000}, {"B", "C", 600000}, {"C", "A", 100}};
	fixture.document.replace(scene, false);
	QCOMPARE(fixture.panel.totalLabel()->text(), QString("Slice total: 1200100 passengers"));
	// A cell that is raised is refused.
	fixture.setCell(0, 1, "600001");
	QCOMPARE(fixture.slices()[0].cells[0].count, 600000);
	QVERIFY(fixture.panel.diagnosticLabel()->text().contains("together"));
	// A cell that is lowered or cleared is applied.
	fixture.setCell(0, 1, "500000");
	QCOMPARE(fixture.slices()[0].cells[0].count, 500000);
	fixture.setCell(2, 0, "");
	QCOMPARE(fixture.slices()[0].cells.size(), std::size_t(2));
}

// An import into a slice of a scene above the limit is refused only when it makes the slice larger.
void TestDemandPanel::importsBelowLimitWhenSmaller() {
	DemandFixture fixture;
	SceneModel scene = demandScene();
	scene.passengerDemand[0].cells = {{"A", "B", 600000}, {"B", "C", 500000}};
	fixture.document.replace(scene, false);
	const std::size_t edits = fixture.labels.size();
	QVERIFY(!fixture.panel.importCsvText("O/D,A,B,C\nA,0,600000,0\nB,0,0,500001\nC,0,0,0\n"));
	QCOMPARE(fixture.labels.size(), edits);
	QVERIFY(fixture.panel.importCsvText("O/D,A,B,C\nA,0,600000,0\nB,0,0,450000\nC,0,0,0\n"));
	QCOMPARE(fixture.panel.totalLabel()->text(), QString("Slice total: 1050000 passengers"));
}

// The matrix shows the sum of the cells of a pair, and an edit replaces all of them with one cell.
void TestDemandPanel::mergesDuplicateCells() {
	DemandFixture fixture;
	SceneModel scene = demandScene();
	scene.passengerDemand[0].cells = {{"A", "B", 3}, {"B", "C", 5}, {"A", "B", 4}};
	fixture.document.replace(scene, false);
	QCOMPARE(fixture.cell(0, 1)->text(), QString("7"));
	fixture.setCell(0, 1, "5");
	QCOMPARE(fixture.slices()[0].cells.size(), std::size_t(2));
	QCOMPARE(fixture.slices()[0].cells[0].originStationId, std::string("A"));
	QCOMPARE(fixture.slices()[0].cells[0].count, 5);
	QCOMPARE(fixture.cell(0, 1)->text(), QString("5"));
	// The shown sum also merges the cells. Text that only reads as the same number is needed to reach the edit.
	fixture.document.replace(scene, false);
	fixture.setCell(0, 1, "07");
	QCOMPARE(fixture.slices()[0].cells.size(), std::size_t(2));
	QCOMPARE(fixture.slices()[0].cells[0].count, 7);
	// Clearing removes all of them.
	fixture.document.replace(scene, false);
	fixture.setCell(0, 1, "0");
	QCOMPARE(fixture.slices()[0].cells.size(), std::size_t(1));
	QCOMPARE(fixture.slices()[0].cells[0].originStationId, std::string("B"));
}

// A station that is added turns the totals row and column into station rows, which are not bold.
void TestDemandPanel::resetsBoldAfterNewStation() {
	DemandFixture fixture;
	QVERIFY(fixture.cell(3, 0)->font().bold());
	QVERIFY(!fixture.cell(0, 1)->font().bold());
	SceneModel scene = demandScene();
	SceneStation station;
	station.id = "D";
	station.name = "Station D";
	scene.stations.push_back(station);
	fixture.document.replace(scene, false);
	fixture.panel.refresh();
	QCOMPARE(fixture.panel.matrixTable()->rowCount(), 5);
	QVERIFY(!fixture.cell(3, 0)->font().bold());
	QVERIFY(!fixture.cell(0, 3)->font().bold());
	QVERIFY(!fixture.cell(3, 3)->font().bold());
	QVERIFY(fixture.cell(4, 0)->font().bold());
	QVERIFY(fixture.cell(0, 4)->font().bold());
	QVERIFY(fixture.cell(4, 4)->font().bold());
}

// A start or end that is typed leaves a slice of the shortest length that the timeline allows.
void TestDemandPanel::typedPeriodKeepsMinimumLength() {
	DemandFixture fixture;
	typeAndLeave(fixture.panel.startEdit(), "09:00:00");
	QCOMPARE(fixture.slices()[0].startSeconds, 8 * hour - DemandRanges::SnapSeconds);
	QCOMPARE(fixture.slices()[0].endSeconds, 8 * hour);
	fixture.document.replace(demandScene(), false);
	typeAndLeave(fixture.panel.endEdit(), "05:00:00");
	QCOMPARE(fixture.slices()[0].startSeconds, 7 * hour);
	QCOMPARE(fixture.slices()[0].endSeconds, 7 * hour + DemandRanges::SnapSeconds);
}

void TestDemandPanel::importsCsv() {
	DemandFixture fixture;
	QVERIFY(fixture.panel.addSlice(8 * hour, 9 * hour));
	const std::string csv = "Time Reference,3600\nO/D,Station A,Station B,Station C\nStation A,0,3,4\nStation B,,0,0\nStation C,9,0,0\n";
	QVERIFY(fixture.panel.importCsvText(QString::fromStdString(csv)));
	QCOMPARE(fixture.labels.back(), std::string("Import demand CSV"));
	const ScenePassengerDemandSlice& imported = fixture.slices().back();
	QCOMPARE(imported.cells.size(), std::size_t(3));
	QCOMPARE(imported.cells[0].destinationStationId, std::string("B"));
	QCOMPARE(imported.cells[2].originStationId, std::string("C"));
	QCOMPARE(fixture.cell(2, 0)->text(), QString("9"));
	// The other slice is untouched, and an import replaces the cells that the slice had.
	QCOMPARE(fixture.slices()[0].cells.size(), std::size_t(2));
	QVERIFY(fixture.panel.importCsvText("O/D,A,B,C\nA,0,1,0\nB,0,0,0\nC,0,0,0\n"));
	QCOMPARE(fixture.slices().back().cells.size(), std::size_t(1));
	QCOMPARE(fixture.slices().back().cells[0].count, 1);
	QCOMPARE(fixture.panel.totalLabel()->text(), QString("Slice total: 1 passengers"));
	fixture.undo();
	QCOMPARE(fixture.slices().back().cells.size(), std::size_t(3));
	// The buttons ask for the file through the chooser; an empty answer cancels.
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath("demand.csv");
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write("O/D,A,B,C\nA,0,0,2\nB,0,0,0\nC,0,0,0\n");
	file.close();
	bool saveRequested = true;
	QString answer;
	fixture.panel.setCsvFileChooser([&](bool save) {
		saveRequested = save;
		return answer;
	});
	const std::size_t edits = fixture.labels.size();
	QTest::mouseClick(fixture.panel.importButton(), Qt::LeftButton);
	QVERIFY(!saveRequested);
	QCOMPARE(fixture.labels.size(), edits);
	answer = path;
	QTest::mouseClick(fixture.panel.importButton(), Qt::LeftButton);
	QCOMPARE(fixture.slices().back().cells.size(), std::size_t(1));
	QCOMPARE(fixture.slices().back().cells[0].destinationStationId, std::string("C"));
	QVERIFY(!fixture.panel.importCsvFile(directory.filePath("missing.csv")));
	QCOMPARE(role(fixture.panel.diagnosticLabel()), std::string("error"));
}

// The period that a CSV file names is not used to scale the counts, and the message says so when it differs.
void TestDemandPanel::namesCsvPeriodMismatch() {
	DemandFixture fixture;
	QVERIFY(fixture.panel.addSlice(8 * hour, 8.25 * hour));
	fixture.status.clear();
	QVERIFY(fixture.panel.importCsvText("Time Reference,900\nO/D,A,B,C\nA,0,3,0\nB,0,0,0\nC,0,0,0\n"));
	QCOMPARE(fixture.status, std::vector<std::string>({"Imported 3 passengers into the demand slice."}));
	fixture.status.clear();
	QVERIFY(fixture.panel.importCsvText("Time Reference,3600\nO/D,A,B,C\nA,0,4,0\nB,0,0,0\nC,0,0,0\n"));
	QCOMPARE(fixture.status.size(), std::size_t(1));
	const QString message = QString::fromStdString(fixture.status[0]);
	QVERIFY2(message.startsWith("Imported 4 passengers into the demand slice."), qPrintable(message));
	QVERIFY2(message.contains("01:00:00") && message.contains("00:15:00") && message.contains("not scaled"), qPrintable(message));
	QCOMPARE(fixture.slices().back().cells[0].count, 4);
	// A file without a period gives the plain message.
	fixture.status.clear();
	QVERIFY(fixture.panel.importCsvText("O/D,A,B,C\nA,0,2,0\nB,0,0,0\nC,0,0,0\n"));
	QCOMPARE(fixture.status, std::vector<std::string>({"Imported 2 passengers into the demand slice."}));
}

void TestDemandPanel::showsCsvErrors() {
	DemandFixture fixture;
	const std::size_t edits = fixture.labels.size();
	QVERIFY(!fixture.panel.importCsvText("O/D,A,Nowhere\nA,0,4\nNowhere,1,0\n"));
	QCOMPARE(fixture.labels.size(), edits);
	QCOMPARE(fixture.slices()[0].cells.size(), std::size_t(2));
	QCOMPARE(role(fixture.panel.diagnosticLabel()), std::string("error"));
	QVERIFY(fixture.panel.diagnosticLabel()->text().contains("not imported"));
	QVERIFY(fixture.panel.diagnosticLabel()->text().contains("Nowhere"));
	QVERIFY(!fixture.panel.importCsvText("O/D,A,B,C\nA,5,1,0\nB,0,0,0\nC,0,0,0\n"));
	QCOMPARE(fixture.slices()[0].cells.size(), std::size_t(2));
	// The next refresh brings the help text back.
	fixture.panel.refresh();
	QCOMPARE(role(fixture.panel.diagnosticLabel()), std::string("help"));
}

void TestDemandPanel::exportsCsvRoundTrip() {
	DemandFixture fixture;
	const QString text = fixture.panel.csvText();
	QVERIFY(text.contains("O/D,A,B,C"));
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath("out.csv");
	bool saveRequested = false;
	fixture.panel.setCsvFileChooser([&](bool save) {
		saveRequested = save;
		return path;
	});
	QTest::mouseClick(fixture.panel.exportButton(), Qt::LeftButton);
	QVERIFY(saveRequested);
	QFile file(path);
	QVERIFY(file.open(QIODevice::ReadOnly));
	QCOMPARE(QString::fromUtf8(file.readAll()), text);
	file.close();
	// The file reads back into another slice with the same cells.
	QVERIFY(fixture.panel.addSlice(8 * hour, 9 * hour));
	QVERIFY(fixture.panel.importCsvFile(path));
	const auto& original = fixture.slices()[0].cells;
	const auto& copy = fixture.slices().back().cells;
	QCOMPARE(copy.size(), original.size());
	for (std::size_t index = 0; index < copy.size(); ++index) {
		QCOMPARE(copy[index].originStationId, original[index].originStationId);
		QCOMPARE(copy[index].destinationStationId, original[index].destinationStationId);
		QCOMPARE(copy[index].count, original[index].count);
	}
	QVERIFY(!fixture.panel.exportCsvFile(directory.filePath("no/such/folder/out.csv")));
	QCOMPARE(role(fixture.panel.diagnosticLabel()), std::string("error"));
}

void TestDemandPanel::deletesSlices() {
	DemandFixture fixture;
	QVERIFY(fixture.panel.addSlice(8 * hour, 9 * hour));
	QVERIFY(fixture.panel.selectSlice("morning"));
	QTest::mouseClick(fixture.panel.deleteButton(), Qt::LeftButton);
	QCOMPARE(fixture.slices().size(), std::size_t(1));
	QCOMPARE(fixture.slices()[0].id, std::string("slice-1"));
	QCOMPARE(fixture.labels.back(), std::string("Delete demand slice"));
	QCOMPARE(fixture.status.back(), std::string("Deleted demand slice 'morning'. Undo restores it."));
	QCOMPARE(fixture.panel.selectedSlice()->id, std::string("slice-1"));
	// The Delete key on the timeline does the same, and the panel is empty afterwards.
	QTest::keyClick(fixture.panel.timeline(), Qt::Key_Delete);
	QVERIFY(fixture.slices().empty());
	QCOMPARE(fixture.panel.selectedSlice(), nullptr);
	QVERIFY(!fixture.panel.deleteButton()->isEnabled());
	QVERIFY(!fixture.panel.startEdit()->isEnabled());
	QVERIFY(!fixture.cell(0, 1)->flags().testFlag(Qt::ItemIsEditable));
	QVERIFY(!fixture.panel.deleteSelectedSlice());
	fixture.undo();
	QCOMPARE(fixture.slices().size(), std::size_t(1));
	QCOMPARE(fixture.panel.selectedSlice()->id, std::string("slice-1"));
}

void TestDemandPanel::showsSlicesOutsideHorizon() {
	DemandFixture fixture;
	fixture.document.mutableModel().passengerDemand.push_back(slice("night", 1 * hour, 2 * hour));
	fixture.document.mutableModel().passengerDemand.push_back(slice("evening", 20 * hour, 21 * hour));
	fixture.panel.refresh();
	QCOMPARE(fixture.panel.sliceCombo()->count(), 3);
	QVERIFY(fixture.panel.sliceCombo()->itemText(1).contains("outside the horizon"));
	QVERIFY(fixture.panel.sliceCombo()->itemText(2).contains("outside the horizon"));
	const DemandTimeline* timeline = fixture.panel.timeline();
	QVERIFY(timeline->barRect("night").isValid());
	QCOMPARE(timeline->barRect("night").left(), 8);
	QVERIFY(timeline->barRect("evening").right() >= timeline->width() - 9);
	// Clicking a stub selects the slice.
	QTest::mouseClick(fixture.panel.timeline(), Qt::LeftButton, Qt::NoModifier, timeline->barRect("night").center());
	QCOMPARE(fixture.panel.selectedSlice()->id, std::string("night"));
	QCOMPARE(fixture.panel.startEdit()->text(), QString("01:00:00"));
	// The selection also works from the list.
	fixture.panel.sliceCombo()->setCurrentIndex(2);
	emit fixture.panel.sliceCombo()->activated(2);
	QCOMPARE(fixture.panel.selectedSlice()->id, std::string("evening"));
}

void TestDemandPanel::showsValidationMessages() {
	DemandFixture fixture;
	SceneDiagnostic ofSlice;
	ofSlice.message = "Demand slice morning is wrong";
	ofSlice.itemType = "demand_slice";
	ofSlice.itemId = "morning";
	SceneDiagnostic other = ofSlice;
	other.message = "Demand slice other is wrong";
	other.itemId = "other";
	const std::vector<SceneDiagnostic> diagnostics = {other, ofSlice};
	fixture.panel.setDiagnostics(&diagnostics);
	fixture.panel.refresh();
	QCOMPARE(role(fixture.panel.diagnosticLabel()), std::string("error"));
	QVERIFY(fixture.panel.diagnosticLabel()->text().contains("morning is wrong"));
	QVERIFY(!fixture.panel.diagnosticLabel()->text().contains("other"));
	fixture.panel.setDiagnostics(nullptr);
	fixture.panel.refresh();
	QCOMPARE(role(fixture.panel.diagnosticLabel()), std::string("help"));
}

void TestDemandPanel::takesPendingEditors() {
	DemandFixture fixture;
	fixture.panel.startEdit()->setText("06:45:00");
	fixture.panel.startEdit()->setModified(true);
	fixture.panel.commitPendingEditors();
	QCOMPARE(fixture.slices()[0].startSeconds, 6 * hour + 2700.0);
	QCOMPARE(fixture.labels.back(), std::string("Edit demand period"));
	// A cell that is open for typing is committed too.
	fixture.panel.show();
	QTableWidget* table = fixture.panel.matrixTable();
	table->setCurrentCell(2, 0);
	table->edit(table->model()->index(2, 0));
	QLineEdit* editor = table->viewport()->findChild<QLineEdit*>();
	QVERIFY(editor);
	editor->setText("8");
	fixture.panel.commitPendingEditors();
	QCOMPARE(fixture.slices()[0].cells.back().count, 8);
	QCOMPARE(fixture.slices()[0].cells.back().originStationId, std::string("C"));
}

// A cell that is emptied in its editor and committed with Return holds no passengers.
void TestDemandPanel::clearsCellWithReturn() {
	DemandFixture fixture;
	fixture.panel.show();
	const auto hide = qScopeGuard([&fixture] { fixture.panel.hide(); });
	QTableWidget* table = fixture.panel.matrixTable();
	table->setCurrentCell(0, 1);
	table->edit(table->model()->index(0, 1));
	QLineEdit* editor = table->viewport()->findChild<QLineEdit*>();
	QVERIFY(editor);
	QTest::keyClick(editor, Qt::Key_A, Qt::ControlModifier);
	QTest::keyClick(editor, Qt::Key_Backspace);
	QVERIFY(editor->text().isEmpty());
	QTest::keyClick(editor, Qt::Key_Return);
	QTRY_COMPARE(fixture.slices()[0].cells.size(), std::size_t(1));
	QCOMPARE(fixture.cell(0, 1)->text(), QString());
}

// The same path with a number, which shows that the key reaches the editor and the commit is waited for.
void TestDemandPanel::commitsCellWithReturn() {
	DemandFixture fixture;
	fixture.panel.show();
	const auto hide = qScopeGuard([&fixture] { fixture.panel.hide(); });
	QTableWidget* table = fixture.panel.matrixTable();
	table->setCurrentCell(0, 1);
	table->edit(table->model()->index(0, 1));
	QLineEdit* editor = table->viewport()->findChild<QLineEdit*>();
	QVERIFY(editor);
	QTest::keyClick(editor, Qt::Key_A, Qt::ControlModifier);
	QTest::keyClicks(editor, "12");
	QTest::keyClick(editor, Qt::Key_Return);
	QTRY_COMPARE(fixture.slices()[0].cells[0].count, 12);
}

void TestDemandPanel::grabsPaimpol() {
	const QByteArray target = qgetenv("EGTRAIN_DEMAND_SHOT");
	if (target.isEmpty())
		QSKIP("Set EGTRAIN_DEMAND_SHOT to a PNG path to save a picture of the Demand tab.");
	SceneDocument document;
	QTabWidget tabs;
	auto* demand = new PassengerDemandPanel;
	document.replace(loadScene(QFINDTESTDATA("../Scenes/Paimpol").toStdString()).scene, false);
	demand->setDocument(&document);
	tabs.addTab(new QWidget, "Passengers");
	tabs.addTab(demand, "Demand");
	tabs.setCurrentIndex(1);
	tabs.resize(760, 640);
	tabs.show();
	QVERIFY(QTest::qWaitForWindowExposed(&tabs));
	QVERIFY(tabs.grab().save(QString::fromLocal8Bit(target)));
}

// A station that is renamed or added changes the headers, the tooltips of the cells and the place of the hatched diagonal.
void TestDemandPanel::refreshesStructureWithStations() {
	DemandFixture fixture;
	QTableWidget* table = fixture.panel.matrixTable();
	QCOMPARE(table->verticalHeaderItem(1)->text(), QString("Station B"));
	QCOMPARE(fixture.cell(0, 1)->toolTip(), QString("Station A to Station B"));
	SceneModel scene = demandScene();
	scene.stations[1].name = "Harbour";
	fixture.document.replace(scene, false);
	fixture.panel.refresh();
	QCOMPARE(table->verticalHeaderItem(1)->text(), QString("Harbour"));
	QCOMPARE(table->horizontalHeaderItem(1)->text(), QString("Harbour"));
	QCOMPARE(fixture.cell(0, 1)->toolTip(), QString("Station A to Harbour"));
	QCOMPARE(fixture.cell(1, 2)->data(Qt::AccessibleDescriptionRole).toString(), QString("Harbour to Station C"));
	QCOMPARE(fixture.cell(3, 1)->toolTip(), QString("Total to Harbour"));
	QCOMPARE(fixture.cell(0, 1)->text(), QString("10"));
	SceneStation station;
	station.id = "D";
	station.name = "Station D";
	scene.stations.insert(scene.stations.begin(), station);
	fixture.document.replace(scene, false);
	fixture.panel.refresh();
	QCOMPARE(table->rowCount(), 5);
	QCOMPARE(table->verticalHeaderItem(0)->text(), QString("Station D"));
	// The old cells moved one place on, and the diagonal and the totals stand where they belong.
	QCOMPARE(fixture.cell(1, 2)->text(), QString("10"));
	QCOMPARE(fixture.cell(2, 3)->text(), QString("5"));
	QCOMPARE(fixture.cell(0, 1)->text(), QString());
	QCOMPARE(fixture.cell(1, 1)->flags(), Qt::ItemFlags());
	QCOMPARE(fixture.cell(1, 1)->background().style(), Qt::BDiagPattern);
	QCOMPARE(fixture.cell(1, 2)->background().style(), Qt::NoBrush);
	QVERIFY(fixture.cell(1, 2)->flags().testFlag(Qt::ItemIsEditable));
	QCOMPARE(fixture.cell(1, 4)->text(), QString("10"));
	QCOMPARE(fixture.cell(4, 4)->text(), QString("15"));
	QVERIFY(fixture.cell(4, 4)->font().bold());
}

// Another slice shows its own texts and totals, and a cell that the slice does not have is empty.
void TestDemandPanel::showsOtherSliceWithoutStaleText() {
	DemandFixture fixture;
	SceneModel scene = demandScene();
	scene.passengerDemand.push_back(slice("evening", 17 * hour, 18 * hour, {{"C", "A", 3}}));
	fixture.document.replace(scene, false);
	fixture.panel.refresh();
	QCOMPARE(fixture.cell(0, 1)->text(), QString("10"));
	QCOMPARE(fixture.cell(3, 3)->text(), QString("15"));
	QVERIFY(fixture.panel.selectSlice("evening"));
	QCOMPARE(fixture.cell(0, 1)->text(), QString());
	QCOMPARE(fixture.cell(1, 2)->text(), QString());
	QCOMPARE(fixture.cell(2, 0)->text(), QString("3"));
	QCOMPARE(fixture.cell(2, 3)->text(), QString("3"));
	QCOMPARE(fixture.cell(3, 0)->text(), QString("3"));
	QCOMPARE(fixture.cell(3, 1)->text(), QString("0"));
	QCOMPARE(fixture.cell(3, 3)->text(), QString("3"));
	QVERIFY(fixture.panel.totalLabel()->text().contains("3 passengers"));
	QVERIFY(fixture.cell(2, 0)->flags().testFlag(Qt::ItemIsEditable));
	QVERIFY(fixture.panel.selectSlice("morning"));
	QCOMPARE(fixture.cell(2, 0)->text(), QString());
	QCOMPARE(fixture.cell(0, 1)->text(), QString("10"));
	QCOMPARE(fixture.cell(3, 3)->text(), QString("15"));
}

// A cell that the user typed into shows the stored value again when the edit is refused or changes nothing, and the
// cells lose their flags when the document is no longer editable.
void TestDemandPanel::restoresCellTextAndFlags() {
	DemandFixture fixture;
	fixture.setCell(0, 1, "abc");
	QCOMPARE(fixture.cell(0, 1)->text(), QString("10"));
	QCOMPARE(fixture.slices()[0].cells[0].count, 10);
	fixture.setCell(0, 1, "010");
	QCOMPARE(fixture.cell(0, 1)->text(), QString("10"));
	QCOMPARE(fixture.slices()[0].cells[0].count, 10);
	QVERIFY(fixture.cell(0, 1)->flags().testFlag(Qt::ItemIsEditable));
	fixture.document.setEditable(false);
	fixture.panel.refresh();
	QCOMPARE(fixture.cell(0, 1)->flags(), Qt::ItemFlags());
	QCOMPARE(fixture.cell(0, 1)->text(), QString("10"));
	fixture.document.setEditable(true);
	fixture.panel.refresh();
	QVERIFY(fixture.cell(0, 1)->flags().testFlag(Qt::ItemIsEditable));
}

// A change that is made while the panel is hidden shows when the panel is shown.
void TestDemandPanel::showsChangesMadeWhileHidden() {
	DemandFixture fixture;
	QVERIFY(!fixture.panel.isVisible());
	SceneModel scene = demandScene();
	scene.stations[2].name = "Terminus";
	scene.passengerDemand[0].cells.push_back({"C", "A", 4});
	fixture.document.replace(scene, false);
	fixture.panel.show();
	const auto hide = qScopeGuard([&fixture] { fixture.panel.hide(); });
	QTableWidget* table = fixture.panel.matrixTable();
	QCOMPARE(table->horizontalHeaderItem(2)->text(), QString("Terminus"));
	QCOMPARE(fixture.cell(2, 0)->text(), QString("4"));
	QCOMPARE(fixture.cell(3, 3)->text(), QString("19"));
}

// While the table is on show, a header that sizes its sections to the contents measures every section again at each
// change of an item. A fill holds the header sizes and measures once, so a matrix of 40 stations is filled in
// milliseconds when the editing state changes or another slice is shown. Measuring per item takes seconds for each.
void TestDemandPanel::fillsShownMatrixOfManyStationsQuickly() {
	constexpr int kStations = 40;
	DemandFixture fixture;
	SceneModel scene = demandScene();
	scene.stations.clear();
	for (int index = 0; index < kStations; ++index) {
		SceneStation station;
		station.id = "S" + std::to_string(100 + index);
		station.name = "Station number " + std::to_string(100 + index);
		scene.stations.push_back(station);
	}
	const auto cellsOf = [&scene](int step, int count) {
		std::vector<ScenePassengerDemandCell> cells;
		for (int index = 0; index < kStations * 10; ++index) {
			const int origin = index % kStations;
			const int destination = (index * step + 3) % kStations;
			if (origin != destination)
				cells.push_back({scene.stations[static_cast<std::size_t>(origin)].id, scene.stations[static_cast<std::size_t>(destination)].id, count});
		}
		return cells;
	};
	scene.passengerDemand = {slice("morning", 7 * hour, 8 * hour, cellsOf(7, 5)), slice("evening", 8 * hour, 9 * hour, cellsOf(11, 120))};
	fixture.document.replace(scene, false);
	fixture.panel.show();
	const auto hide = qScopeGuard([&fixture] { fixture.panel.hide(); });
	QVERIFY(QTest::qWaitForWindowExposed(&fixture.panel));
	QCoreApplication::processEvents();

	QElapsedTimer timer;
	timer.start();
	fixture.document.setEditable(false);
	fixture.panel.refresh();
	QCoreApplication::processEvents();
	QCOMPARE(fixture.cell(0, 1)->flags(), Qt::ItemFlags());
	fixture.document.setEditable(true);
	fixture.panel.refresh();
	QCoreApplication::processEvents();
	QVERIFY(fixture.cell(0, 1)->flags().testFlag(Qt::ItemIsEditable));
	QVERIFY(fixture.panel.selectSlice("evening"));
	QCoreApplication::processEvents();
	const qint64 elapsed = timer.elapsed();
	QVERIFY2(elapsed < 2000, qPrintable(QString("three fills of the shown matrix took %1 ms").arg(elapsed)));
	QCOMPARE(fixture.panel.matrixTable()->horizontalHeader()->sectionResizeMode(0), QHeaderView::ResizeToContents);
}

// The headers size the sections to the contents again after a fill: a column gets wider for a longer count.
void TestDemandPanel::sizesColumnsOfShownMatrixAfterFill() {
	DemandFixture fixture;
	SceneModel scene = demandScene();
	for (SceneStation& station : scene.stations)
		station.name = station.id;
	fixture.document.replace(scene, false);
	fixture.panel.show();
	const auto hide = qScopeGuard([&fixture] { fixture.panel.hide(); });
	QVERIFY(QTest::qWaitForWindowExposed(&fixture.panel));
	QCoreApplication::processEvents();
	QTableWidget* table = fixture.panel.matrixTable();
	const int before = table->columnWidth(1);
	fixture.setCell(0, 1, "123456");
	QCoreApplication::processEvents();
	QCOMPARE(fixture.cell(0, 1)->text(), QString("123456"));
	QCOMPARE(table->horizontalHeader()->sectionResizeMode(1), QHeaderView::ResizeToContents);
	QCOMPARE_GT(table->columnWidth(1), before);
}

QTEST_MAIN(TestDemandPanel)
#include "test_editor_demand.moc"
