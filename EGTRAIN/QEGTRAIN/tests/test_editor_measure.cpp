#include "editors/MeasurePanel.h"
#include "scene/SceneHistory.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTest>

#include <string>
#include <vector>

static SceneStop stop(const char* stationId) {
	SceneStop result;
	result.stationId = stationId;
	result.hasPlannedDeparture = true;
	return result;
}

static SceneMeasure measure(const char* id, const char* serviceId, bool hasOccurrence, int occurrence, double seconds, const char* station) {
	SceneMeasure result;
	result.id = id;
	result.serviceId = serviceId;
	result.hasOccurrence = hasOccurrence;
	result.occurrence = occurrence;
	result.timeSeconds = seconds;
	result.fromStationId = station;
	return result;
}

// A case of one hour with two services. Service s1 has the service code 100, runs every 20 minutes, so it has three
// occurrences, and calls at A, B, A and C. Service s2 runs once and calls at X and Y. The scenario base holds two measures, and
// the scenario alt none.
static SceneModel measureScene() {
	SceneModel scene;
	scene.name = "Measures";
	scene.settings.hasDuration = true;
	scene.settings.durationSeconds = 3600.0;
	SceneService first;
	first.id = "s1";
	first.operatingCode = "100";
	first.route = "r1";
	first.hasRepeat = true;
	first.headwaySeconds = 1200.0;
	first.stops = {stop("A"), stop("B"), stop("A"), stop("C")};
	SceneService second;
	second.id = "s2";
	second.route = "r1";
	second.stops = {stop("X"), stop("Y")};
	scene.services = {first, second};
	SceneScenario base;
	base.id = "base";
	base.name = "Baseline";
	base.measures = {measure("cancel", "s1", true, 2, 600.0, "B"), measure("all", "s2", false, 1, 0.0, "")};
	SceneScenario alt;
	alt.id = "alt";
	alt.name = "Alternative";
	scene.scenarios = {base, alt};
	scene.defaultScenarioId = "base";
	return scene;
}

static QStringList items(const QListWidget* list) {
	QStringList result;
	for (int row = 0; row < list->count(); ++row)
		result << list->item(row)->text();
	return result;
}

static QStringList choices(const QComboBox* combo) {
	QStringList result;
	for (int index = 0; index < combo->count(); ++index)
		result << combo->itemText(index);
	return result;
}

// A panel with a run selection of its own on a document that shows measureScene(), with the scenario base selected.
struct MeasureFixture {
	SceneDocument document;
	MeasurePanel panel;
	RunSelection selection;
	QStringList status;

	MeasureFixture() {
		panel.setRunSelection(&selection);
		panel.setDocument(&document);
		document.replace(measureScene(), false);
		selection.selectScenario("base");
		panel.refresh();
		QObject::connect(&panel, &MeasurePanel::statusMessage, &panel, [this](const QString& text, int) { status << text; });
	}

	// The panel is hidden while the fixture is whole, as in the other editor tests.
	~MeasureFixture() { panel.hide(); }

	const std::vector<SceneMeasure>& measures() const { return document.model().scenarios[0].measures; }
};

class TestMeasurePanel : public QObject {
	Q_OBJECT

private slots:
	void showsNothingWithoutScene();
	void listsAndShowsMeasures();
	void editsFieldsAsOneUndoableEditEach();
	void addsDuplicatesAndDeletes();
	void showsOtherScenario();
	void ignoresActionsWhileRunUsesScene();
};

void TestMeasurePanel::showsNothingWithoutScene() {
	MeasurePanel panel;
	QCOMPARE(panel.listWidget()->count(), 0);
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY(!panel.serviceCombo()->isEnabled());
	QVERIFY(!panel.everyOccurrenceCheck()->isEnabled());
	QVERIFY(!panel.occurrenceEdit()->isEnabled());
	QVERIFY(!panel.secondsEdit()->isEnabled());
	QVERIFY(!panel.stationCombo()->isEnabled());
	SceneDocument document;
	panel.setDocument(&document);
	panel.addMeasure();
	panel.duplicateMeasure();
	panel.deleteMeasure();
	panel.commitSeconds();
	panel.commitPendingEditors();
	QCOMPARE(document.revision(), quint64(0));
	QVERIFY(document.model().scenarios.empty());
}

void TestMeasurePanel::listsAndShowsMeasures() {
	MeasureFixture f;
	QCOMPARE(items(f.panel.listWidget()),
		QStringList({"cancel: cancel 100-2 at 600 s from B", "all: cancel s2 (every occurrence) at 0 s, complete"}));
	QCOMPARE(f.panel.listWidget()->currentRow(), 0);
	QCOMPARE(f.panel.serviceCombo()->currentData().toString(), "s1");
	QVERIFY(!f.panel.everyOccurrenceCheck()->isChecked());
	QVERIFY(f.panel.occurrenceEdit()->isEnabled());
	QCOMPARE(f.panel.occurrenceEdit()->value(), 2);
	QCOMPARE(f.panel.occurrenceEdit()->suffix(), " of 3");
	QCOMPARE_EQ(f.panel.secondsEdit()->value(), 600.0);
	// The stations are complete, then each station of the service once, in the order of its calls.
	QCOMPARE(choices(f.panel.stationCombo()), QStringList({"(complete)", "A", "B", "C"}));
	QCOMPARE(f.panel.stationCombo()->currentText(), "B");
	f.panel.listWidget()->setCurrentRow(1);
	QVERIFY(f.panel.everyOccurrenceCheck()->isChecked());
	QVERIFY(!f.panel.occurrenceEdit()->isEnabled());
	QCOMPARE(f.panel.stationCombo()->currentText(), "(complete)");
	QCOMPARE(choices(f.panel.stationCombo()), QStringList({"(complete)", "X", "Y"}));
	// Showing a scene edits nothing.
	QVERIFY(!f.document.isDirty());
	QVERIFY(!f.selection.isScenarioModified("base"));
}

void TestMeasurePanel::editsFieldsAsOneUndoableEditEach() {
	MeasureFixture f;
	f.panel.stationCombo()->setCurrentIndex(3);
	QCOMPARE(f.measures()[0].fromStationId, "C");
	QCOMPARE(f.document.lastChangeLabel(), "Change measure station");
	QVERIFY(f.selection.isScenarioModified("base"));
	f.panel.stationCombo()->setCurrentIndex(0);
	QCOMPARE(f.measures()[0].fromStationId, "");
	// Undo and Redo, as the window keeps them: each edit is one step of the history.
	SceneHistory history;
	history.reset(f.document.model());
	f.panel.secondsEdit()->setValue(900.0);
	f.panel.commitSeconds();
	QCOMPARE_EQ(f.measures()[0].timeSeconds, 900.0);
	QCOMPARE(f.document.lastChangeLabel(), "Change measure time");
	history.record(f.document.model());
	QCOMPARE(history.undoCount(), std::size_t{1});
	const SceneHistoryEntry* undone = history.undo();
	QVERIFY(undone);
	f.document.restore(*undone->scene, true);
	f.panel.refresh();
	QCOMPARE_EQ(f.measures()[0].timeSeconds, 600.0);
	QCOMPARE_EQ(f.panel.secondsEdit()->value(), 600.0);
	const SceneHistoryEntry* redone = history.redo();
	QVERIFY(redone);
	f.document.restore(*redone->scene, true);
	f.panel.refresh();
	QCOMPARE_EQ(f.measures()[0].timeSeconds, 900.0);
	QCOMPARE_EQ(f.panel.secondsEdit()->value(), 900.0);
	f.panel.occurrenceEdit()->setValue(3);
	f.panel.commitOccurrence();
	QCOMPARE(f.measures()[0].occurrence, 3);
	// Every occurrence names none, and the occurrence goes back to 1 so that the file does not name one.
	f.panel.everyOccurrenceCheck()->setChecked(true);
	QVERIFY(!f.measures()[0].hasOccurrence);
	QCOMPARE(f.measures()[0].occurrence, 1);
	QVERIFY(!f.panel.occurrenceEdit()->isEnabled());
	f.panel.everyOccurrenceCheck()->setChecked(false);
	QVERIFY(f.measures()[0].hasOccurrence);
	// Another service starts with every occurrence and no station.
	f.panel.stationCombo()->setCurrentIndex(2);
	f.panel.serviceCombo()->setCurrentIndex(1);
	QCOMPARE(f.measures()[0].serviceId, "s2");
	QVERIFY(!f.measures()[0].hasOccurrence);
	QCOMPARE(f.measures()[0].fromStationId, "");
	QCOMPARE(f.document.lastChangeLabel(), "Change measure service");
	QCOMPARE(f.measures().size(), std::size_t{2});
}

void TestMeasurePanel::addsDuplicatesAndDeletes() {
	MeasureFixture f;
	f.panel.addMeasure();
	QCOMPARE(f.measures().size(), std::size_t{3});
	QCOMPARE(f.measures()[2].id, "cancel_2");
	QCOMPARE(f.measures()[2].serviceId, "s1");
	QVERIFY(!f.measures()[2].hasOccurrence);
	QCOMPARE(f.measures()[2].fromStationId, "");
	QCOMPARE(f.panel.listWidget()->currentRow(), 2);
	QCOMPARE(f.document.lastChangeLabel(), "Add measure");
	f.panel.listWidget()->setCurrentRow(0);
	f.panel.duplicateMeasure();
	QCOMPARE(f.measures().size(), std::size_t{4});
	QCOMPARE(f.measures()[1].id, "cancel_copy");
	QCOMPARE(f.measures()[1].fromStationId, "B");
	QCOMPARE(f.panel.listWidget()->currentRow(), 1);
	f.panel.deleteMeasure();
	QCOMPARE(f.measures().size(), std::size_t{3});
	QCOMPARE(f.measures()[1].id, "all");
	QCOMPARE(f.status, QStringList({"Deleted measure 'cancel_copy'. Undo restores it."}));
}

void TestMeasurePanel::showsOtherScenario() {
	MeasureFixture f;
	f.selection.selectScenario("alt");
	f.panel.refresh();
	QCOMPARE(f.panel.listWidget()->count(), 0);
	QVERIFY(!f.panel.emptyStateLabel()->isHidden());
	QCOMPARE(f.panel.emptyStateLabel()->text(), "This scenario has no measures. Use Add.");
	f.panel.addMeasure();
	QCOMPARE(f.document.model().scenarios[1].measures.size(), std::size_t{1});
	QCOMPARE(f.document.model().scenarios[1].measures[0].id, "cancel_2");
	QVERIFY(f.selection.isScenarioModified("alt"));
}

void TestMeasurePanel::ignoresActionsWhileRunUsesScene() {
	MeasureFixture f;
	f.document.markSaved();
	f.document.setEditable(false);
	f.panel.refresh();
	QVERIFY(!f.panel.listWidget()->isEnabled());
	QVERIFY(!f.panel.addButton()->isEnabled());
	QVERIFY(!f.panel.duplicateButton()->isEnabled());
	QVERIFY(!f.panel.deleteButton()->isEnabled());
	QVERIFY(!f.panel.serviceCombo()->isEnabled());
	QVERIFY(!f.panel.stationCombo()->isEnabled());
	QVERIFY(!f.panel.secondsEdit()->isEnabled());
	f.panel.secondsEdit()->setValue(5.0);
	f.panel.commitSeconds();
	f.panel.commitStation(0);
	f.panel.commitService(1);
	f.panel.commitEveryOccurrence(true);
	f.panel.addMeasure();
	f.panel.duplicateMeasure();
	f.panel.deleteMeasure();
	QVERIFY(!f.document.isDirty());
	QCOMPARE(f.measures().size(), std::size_t{2});
}

QTEST_MAIN(TestMeasurePanel)
#include "test_editor_measure.moc"
