#include "editors/PassengerPanel.h"
#include "editors/CompositionPanel.h"
#include "editors/EditorRules.h"
#include "editors/InfrastructurePanel.h"
#include "editors/ServicePanel.h"
#include "editors/infrastructure/TopologyFacets.h"

#include "scene/SceneDiagnostic.h"
#include "support/FakePrompts.h"
#include "widgets/ChoiceComboBox.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScopeGuard>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

static SceneStop stopAt(const char* stationId) {
	SceneStop stop;
	stop.stationId = stationId;
	return stop;
}

static ScenePassengerLeg leg(const char* id, const char* serviceId, const char* origin, const char* destination,
	int occurrence = 1) {
	ScenePassengerLeg value;
	value.id = id;
	value.serviceId = serviceId;
	value.originStationId = origin;
	value.destinationStationId = destination;
	value.occurrence = occurrence;
	return value;
}

// Three stations and three services: S1 runs once over A, B and C, S2 runs three times over A and C, and S3 repeats
// every half hour over A and B. P1 has two journeys, the first with two legs, and P2 has one journey with one leg.
static SceneModel passengerScene() {
	SceneModel scene;
	scene.name = "Passenger case";
	scene.settings.hasDuration = true;
	scene.settings.durationSeconds = 3600.0;
	for (const char* id : {"A", "B", "C"}) {
		SceneStation station;
		station.id = id;
		station.name = std::string("Station ") + id;
		scene.stations.push_back(station);
	}
	SceneService s1;
	s1.id = "S1";
	s1.stops = {stopAt("A"), stopAt("B"), stopAt("C")};
	SceneService s2;
	s2.id = "S2";
	s2.stops = {stopAt("A"), stopAt("C")};
	s2.hasRepeat = true;
	s2.hasRepeatCount = true;
	s2.repeatCount = 3;
	SceneService s3;
	s3.id = "S3";
	s3.stops = {stopAt("A"), stopAt("B")};
	s3.hasRepeat = true;
	s3.headwaySeconds = 1800.0;
	scene.services = {s1, s2, s3};

	ScenePassengerJourney j1;
	j1.id = "J1";
	j1.activity = "commute";
	j1.originStationId = "A";
	j1.destinationStationId = "C";
	j1.plannedDepartureStartSeconds = 100.0;
	j1.plannedDepartureEndSeconds = 200.0;
	j1.plannedArrivalStartSeconds = 300.0;
	j1.plannedArrivalEndSeconds = 400.0;
	j1.legs = {leg("L1", "S1", "A", "B"), leg("L2", "S1", "B", "C")};
	ScenePassengerJourney j2;
	j2.id = "J2";
	j2.originStationId = "B";
	j2.destinationStationId = "C";
	ScenePassengerJourney j3;
	j3.id = "J3";
	j3.originStationId = "A";
	j3.destinationStationId = "C";
	j3.legs = {leg("L3", "S2", "A", "C", 2)};
	ScenePassenger p1;
	p1.id = "P1";
	p1.journeys = {j1, j2};
	ScenePassenger p2;
	p2.id = "P2";
	p2.journeys = {j3};
	scene.passengers = {p1, p2};
	return scene;
}

using Log = std::vector<std::string>;

// The order in which a panel and its document report an edit, and what the panel puts on the status bar.
struct Events {
	Log log;
	std::vector<std::string> status;

	Events(PassengerPanel& panel, SceneDocument& document) {
		QObject::connect(&document, &SceneDocument::changed, &panel, [this] { log.push_back("documentChanged"); });
		QObject::connect(&panel, &PassengerPanel::changed, &panel, [this] { log.push_back("panelChanged"); });
		QObject::connect(&panel, &PassengerPanel::statusMessage, &panel, [this](const QString& text, int) {
			status.push_back(text.toStdString());
		});
	}
};

static const Log oneChange = {"documentChanged", "panelChanged"};

// What the user does to a text field: type, then leave the field.
static void typeAndLeave(QLineEdit* edit, const QString& text) {
	edit->setText(text);
	emit edit->editingFinished();
}

static void choose(QComboBox* combo, const char* data) {
	combo->setCurrentIndex(combo->findData(QString::fromUtf8(data)));
}

static std::vector<std::string> items(const QListWidget* list) {
	std::vector<std::string> texts;
	for (int row = 0; row < list->count(); ++row)
		texts.push_back(list->item(row)->text().toStdString());
	return texts;
}

static std::vector<std::string> comboData(const QComboBox* combo) {
	std::vector<std::string> values;
	for (int index = 0; index < combo->count(); ++index)
		values.push_back(combo->itemData(index).toString().toStdString());
	return values;
}

// The role of a message label: "help", the status ("error" or "warning"), or "" when it has none. A role that comes from the
// stylesheet rules leaves the label without a style sheet of its own.
static std::string role(const QLabel* label) {
	if (!label->styleSheet().isEmpty())
		return "inline style";
	if (label->property("dialogHelp").toBool())
		return label->property("dialogStatus").isValid() ? "help and status" : "help";
	return label->property("dialogStatus").toString().toStdString();
}

static std::vector<std::string> legIds(const ScenePassengerJourney& journey) {
	std::vector<std::string> ids;
	for (const auto& value : journey.legs)
		ids.push_back(value.id);
	return ids;
}

// A passenger of an import with one journey per entry of `journeys`, each with the legs that its entry names.
static ScenePassenger importedPassenger(const char* id, const std::vector<std::pair<const char*, std::vector<const char*>>>& journeys) {
	ScenePassenger passenger;
	passenger.id = id;
	for (const auto& entry : journeys) {
		ScenePassengerJourney journey;
		journey.id = entry.first;
		for (const char* legName : entry.second)
			journey.legs.push_back(leg(legName, "S1", "A", "B"));
		passenger.journeys.push_back(journey);
	}
	return passenger;
}

static std::vector<std::string> passengerIds(const std::vector<ScenePassenger>& passengers) {
	std::vector<std::string> ids;
	for (const auto& passenger : passengers)
		ids.push_back(passenger.id);
	return ids;
}

static bool writeFile(const QString& path, const char* text) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(text) == static_cast<qint64>(std::string(text).size());
}

// The folder of a legacy passenger import: the DAS and RouteChoice files with the given rows.
static bool writePassengerFolder(const QTemporaryDir& root, const char* dasRows, const char* routeRows) {
	const QString folder = QDir(root.path()).filePath("Passengers");
	return QDir().mkpath(folder)
		&& writeFile(QDir(folder).filePath("DAS_FrenchCaseStudy.csv"),
			(std::string("c0,person_id,c2,c3,journey_no,activity,destination,c7,c8,c9,arrival,c11,origin,c13,departure\n")
				+ dasRows)
				.c_str())
		&& writeFile(QDir(folder).filePath("RouteChoiceFC_EQ1.csv"),
			(std::string("person_id,destination,nb_transfers,r_service_lines_id_1\n") + routeRows).c_str());
}

// A panel with prompts of its own, which answer Yes, on a document that shows passengerScene(), and the events of both.
struct PassengerFixture {
	SceneDocument document;
	PassengerPanel panel;
	FakePrompts prompts;
	Events events{panel, document};

	PassengerFixture() {
		prompts.confirmAnswer = true;
		panel.setPrompts(&prompts);
		panel.setDocument(&document);
		document.replace(passengerScene(), false);
	}
};

// The passenger panel and a service panel, with prompts that answer Yes, on one document that shows passengerScene(). The
// labels and kinds of the changes of the document are kept in their order, by a connection made before those of the
// panels, as the window keeps its undo steps.
struct ServiceEditFixture {
	SceneDocument document;
	Log labels;
	std::vector<SceneDocument::ChangeKinds> kinds;
	FakePrompts prompts;
	PassengerPanel passengers;
	ServicePanel services;

	ServiceEditFixture() {
		QObject::connect(&document, &SceneDocument::changed, &document, [this](SceneDocument::ChangeKinds changedKinds) {
			labels.push_back(document.lastChangeLabel().toStdString());
			kinds.push_back(changedKinds);
		});
		prompts.confirmAnswer = true;
		passengers.setPrompts(&prompts);
		services.setPrompts(&prompts);
		passengers.setDocument(&document);
		services.setDocument(&document);
		document.replace(passengerScene(), false);
	}
};

class TestPassengerPanel : public QObject {
	Q_OBJECT

private slots:
	void showsNothingWithoutScene();
	void showsNewScene();
	void selectsPassengersAndJourneys();
	void editsAddsAndDeletesPassengers();
	void editsJourneys();
	void movesLegs();
	void editsLegs();
	void showsDiagnosticCallouts();
	void ignoresEditsWhileRunUsesScene();
	void showsAnotherScene();
	void detachedPanelIgnoresDocument();
	void keepsSelectionOfReorderedScene();
	void keepsTypedTextWhenServiceIsAdded();
	void takesTypedTextQuietlyDuringOtherEdit();
	void keepsTypedServiceAndPassengerWhenTrackIsAdded();
	void forgetsDeletedDocument();
	void importsPassengers();
	void calloutsShowSelectedPassenger();
	void showsInvalidValues();
	void importRejectsTakenIds();
	void importRejectsRepeatedIds();
	void importRejectsEmptyIds();
	void importReportsRejectedPassengers();
};

void TestPassengerPanel::showsNothingWithoutScene() {
	PassengerPanel panel;
	// A panel without a document shows no passengers and offers no action.
	QCOMPARE(panel.passengerList()->count(), 0);
	QVERIFY(!panel.passengerList()->isEnabled());
	QVERIFY(!panel.addPassengerButton()->isEnabled());
	QVERIFY(!panel.importButton()->isEnabled());
	QVERIFY(panel.tabs()->isHidden());
	// A panel without a selection says what to select.
	QCOMPARE(panel.diagnosticLabel()->text(), "Select a passenger or add one.");
	QCOMPARE(role(panel.diagnosticLabel()), "help");
	QVERIFY(!panel.diagnosticLabel()->isHidden());
	QCOMPARE(panel.journeyOriginCombo()->itemText(0), "Select a journey or leg");
	QCOMPARE(panel.legServiceCombo()->itemText(0), "Select a leg");
	// A panel without an import result shows neither its heading nor its table.
	QVERIFY(panel.importResultHeading()->isHidden());
	QVERIFY(panel.importResultTable()->isHidden());
	for (const QComboBox* combo : {panel.journeyOriginCombo(), panel.journeyDestinationCombo(), panel.legOriginCombo(),
			 panel.legDestinationCombo(), panel.legServiceCombo()})
		QVERIFY2(dynamic_cast<const ChoiceComboBox*>(combo) != nullptr, "the station and service choices are choice combo boxes");
	SceneDocument document;
	panel.setDocument(&document);
	panel.addPassengerButton()->click();
	// Add while the document holds no scene changes nothing.
	QVERIFY(document.model().passengers.empty());
	QCOMPARE(document.revision(), quint64(0));
	QVERIFY(!document.isDirty());
}

void TestPassengerPanel::showsNewScene() {
	PassengerFixture f;
	PassengerPanel& panel = f.panel;
	// A new scene is shown in the passenger list with the first passenger selected.
	QCOMPARE(items(panel.passengerList()), std::vector<std::string>({"P1", "P2"}));
	QCOMPARE(panel.passengerList()->currentRow(), 0);
	QVERIFY(panel.passengerList()->isEnabled());
	QVERIFY(panel.addPassengerButton()->isEnabled());
	QVERIFY(panel.importButton()->isEnabled());
	QVERIFY(!panel.tabs()->isHidden());
	// The journeys of the passenger and the legs of its first journey are listed with the first of each selected.
	QCOMPARE(items(panel.journeyList()), std::vector<std::string>({"J1 (commute)", "J2"}));
	QCOMPARE(items(panel.legList()), std::vector<std::string>({"L1", "L2"}));
	QCOMPARE(panel.journeyList()->currentRow(), 0);
	QCOMPARE(panel.legList()->currentRow(), 0);
	// The id and activity fields show the selection.
	QCOMPARE(panel.passengerIdEdit()->text(), "P1");
	QCOMPARE(panel.journeyIdEdit()->text(), "J1");
	QCOMPARE(panel.journeyActivityEdit()->text(), "commute");
	QCOMPARE(panel.legIdEdit()->text(), "L1");
	// The journey stations are shown with their names.
	QCOMPARE(panel.journeyOriginCombo()->currentText(), "Station A (A)");
	QCOMPARE(panel.journeyOriginCombo()->currentData().toString(), "A");
	QCOMPARE(panel.journeyDestinationCombo()->currentData().toString(), "C");
	const auto& windows = panel.journeyWindowEdits();
	// The four time windows of the journey are shown.
	QCOMPARE_EQ(windows[0]->value(), 100.0);
	QCOMPARE_EQ(windows[1]->value(), 200.0);
	QCOMPARE_EQ(windows[2]->value(), 300.0);
	QCOMPARE_EQ(windows[3]->value(), 400.0);
	// The leg shows its service and stations, and offers the stops of the service.
	QCOMPARE(panel.legServiceCombo()->currentData().toString(), "S1");
	QCOMPARE(comboData(panel.legServiceCombo()), std::vector<std::string>({"S1", "S2", "S3"}));
	QCOMPARE(panel.legOriginCombo()->currentData().toString(), "A");
	QCOMPARE(panel.legDestinationCombo()->currentData().toString(), "B");
	QCOMPARE(comboData(panel.legOriginCombo()), std::vector<std::string>({"A", "B", "C"}));
	// A service that runs once allows the occurrences 0 and 1.
	QCOMPARE(panel.legOccurrenceEdit()->value(), 1);
	QCOMPARE(panel.legOccurrenceEdit()->minimum(), 0);
	QCOMPARE(panel.legOccurrenceEdit()->maximum(), 1);
	// The first leg cannot move up.
	QVERIFY(!panel.moveLegUpButton()->isEnabled());
	QVERIFY(panel.moveLegDownButton()->isEnabled());
	QVERIFY(panel.addLegButton()->isEnabled());
	QVERIFY(panel.deleteLegButton()->isEnabled());
	// Showing a scene edits nothing.
	QVERIFY(f.events.log.empty());
	QVERIFY(!f.document.isDirty());
	QCOMPARE(f.document.revision(), quint64(1));
}

void TestPassengerPanel::selectsPassengersAndJourneys() {
	PassengerFixture f;
	SceneDocument& document = f.document;
	PassengerPanel& panel = f.panel;
	panel.passengerList()->setCurrentRow(1);
	// Selecting another passenger shows its journey and leg, and counts the occurrences of its service.
	QCOMPARE(items(panel.journeyList()), std::vector<std::string>({"J3"}));
	QCOMPARE(items(panel.legList()), std::vector<std::string>({"L3"}));
	QCOMPARE(panel.legServiceCombo()->currentData().toString(), "S2");
	QCOMPARE(comboData(panel.legOriginCombo()), std::vector<std::string>({"A", "C"}));
	QCOMPARE(panel.legOccurrenceEdit()->value(), 2);
	QCOMPARE(panel.legOccurrenceEdit()->maximum(), 3);
	// Selecting edits nothing.
	QVERIFY(f.events.log.empty());
	QCOMPARE(document.revision(), quint64(1));
	// Ctrl+click on the selected passenger clears the selection and keeps the current row.
	panel.passengerList()->clearSelection();
	// A passenger list without a selection shows an empty passenger form.
	QCOMPARE(panel.passengerList()->currentRow(), 1);
	QVERIFY(panel.passengerList()->selectedItems().isEmpty());
	QVERIFY(panel.tabs()->isHidden());
	QVERIFY(panel.passengerIdEdit()->text().isEmpty());
	QVERIFY(!panel.passengerIdEdit()->isEnabled());
	QVERIFY(!panel.deletePassengerButton()->isEnabled());
	QCOMPARE(panel.journeyList()->count(), 0);
	// A passenger list without a selection says what to select.
	QCOMPARE(panel.diagnosticLabel()->text(), "Select a passenger or add one.");
	QCOMPARE(role(panel.diagnosticLabel()), "help");
	panel.commitPendingEditors();
	// Committing pending edits without a selected passenger shows no error and keeps the form empty.
	QCOMPARE(f.prompts.errorCount, 0);
	QVERIFY(panel.passengerIdEdit()->text().isEmpty());
	// Clearing the selection edits nothing.
	QVERIFY(f.events.log.empty());
	QCOMPARE(document.revision(), quint64(1));
	QVERIFY(!document.isDirty());
	panel.refresh();
	// Refreshing keeps the passenger list without a selection.
	QVERIFY(panel.passengerList()->selectedItems().isEmpty());
	QVERIFY(panel.tabs()->isHidden());
	QVERIFY(panel.passengerIdEdit()->text().isEmpty());
	panel.passengerList()->setCurrentRow(0);
	panel.journeyList()->setCurrentRow(1);
	// A journey without legs shows an empty leg editor that still allows Add.
	QCOMPARE(panel.legList()->count(), 0);
	QVERIFY(!panel.legIdEdit()->isEnabled());
	QVERIFY(panel.addLegButton()->isEnabled());
	QCOMPARE(panel.legServiceCombo()->itemText(0), "Select a leg");
	QCOMPARE(panel.legOriginCombo()->itemText(0), "Select a journey or leg");
	panel.refresh();
	QVERIFY2(panel.journeyList()->currentRow() == 1, "refreshing keeps the selected journey");
}

// The edits of a passenger, then pending text, Add and Delete, which see the id and the revision that the edits left.
void TestPassengerPanel::editsAddsAndDeletesPassengers() {
	PassengerFixture f;
	SceneDocument& document = f.document;
	PassengerPanel& panel = f.panel;
	FakePrompts& prompts = f.prompts;
	Events& events = f.events;

	// Passenger edits.
	typeAndLeave(panel.passengerIdEdit(), "  P9 ");
	// A finished passenger id is trimmed, written to the document and shown in the list.
	QCOMPARE(document.model().passengers[0].id, "P9");
	QCOMPARE(panel.passengerList()->item(0)->text(), "P9");
	QVERIFY(document.isDirty());
	QCOMPARE(document.revision(), quint64(2));
	QCOMPARE(document.lastChangeLabel(), "Rename passenger");
	QVERIFY2(events.log == oneChange, "a rename is one change of the document and then of the panel");
	events.log.clear();
	typeAndLeave(panel.passengerIdEdit(), "P2");
	// A passenger id that exists is refused with the old text restored.
	QCOMPARE(prompts.errorCount, 1);
	QCOMPARE(prompts.errorTitle, "Duplicate Passenger ID");
	QCOMPARE(prompts.errorMessage, "Choose a unique passenger ID.");
	QCOMPARE(panel.passengerIdEdit()->text(), "P9");
	QCOMPARE(document.model().passengers[0].id, "P9");
	QCOMPARE(document.revision(), quint64(2));
	QVERIFY(events.log.empty());
	typeAndLeave(panel.passengerIdEdit(), "");
	// An empty passenger id is refused with the old text restored.
	QCOMPARE(prompts.errorCount, 2);
	QCOMPARE(prompts.errorTitle, "Invalid Passenger ID");
	QCOMPARE(prompts.errorMessage, "Passenger IDs must not be empty.");
	QCOMPARE(panel.passengerIdEdit()->text(), "P9");
	QCOMPARE(document.revision(), quint64(2));
	QVERIFY(events.log.empty());
	typeAndLeave(panel.passengerIdEdit(), "P9");
	// Leaving the id unchanged does not touch the document.
	QCOMPARE(document.revision(), quint64(2));
	QVERIFY(events.log.empty());
	QCOMPARE(prompts.errorCount, 2);

	// Pending text and cancel.
	panel.passengerIdEdit()->setText("typed, not finished");
	panel.refresh();
	// Refreshing drops text that was not finished and leaves the document as it is.
	QCOMPARE(panel.passengerIdEdit()->text(), "P9");
	QCOMPARE(document.model().passengers[0].id, "P9");
	QCOMPARE(document.revision(), quint64(2));
	QVERIFY(events.log.empty());
	panel.passengerIdEdit()->setText("P1");
	panel.commitPendingEditors();
	// Committing the pending editors writes the text of the id field.
	QCOMPARE(document.model().passengers[0].id, "P1");
	QCOMPARE(document.revision(), quint64(3));
	QCOMPARE(events.log, oneChange);
	events.log.clear();
	panel.commitPendingEditors();
	// Committing with nothing pending changes nothing.
	QCOMPARE(document.revision(), quint64(3));
	QVERIFY(events.log.empty());

	// Add and delete a passenger.
	panel.addPassengerButton()->click();
	// Add appends a passenger with a free id and selects it.
	QCOMPARE(items(panel.passengerList()), std::vector<std::string>({"P1", "P2", "new_passenger"}));
	QCOMPARE(panel.passengerList()->currentRow(), 2);
	QCOMPARE(panel.passengerIdEdit()->text(), "new_passenger");
	QCOMPARE(panel.journeyList()->count(), 0);
	QVERIFY(panel.addJourneyButton()->isEnabled());
	// Add is one change.
	QCOMPARE(document.lastChangeLabel(), "Add passenger");
	QCOMPARE(events.log, oneChange);
	QCOMPARE(document.revision(), quint64(4));
	prompts.confirmAnswer = false;
	events.log.clear();
	panel.deletePassengerButton()->click();
	// A declined delete keeps the passenger.
	QCOMPARE(prompts.confirmCount, 1);
	QCOMPARE(prompts.confirmTitle, "Delete Passenger");
	QCOMPARE(document.model().passengers.size(), std::size_t(3));
	QVERIFY(events.log.empty());
	QCOMPARE(document.revision(), quint64(4));
	prompts.confirmAnswer = true;
	panel.deletePassengerButton()->click();
	// A confirmed delete removes the passenger and selects the last one that is left.
	QCOMPARE(items(panel.passengerList()), std::vector<std::string>({"P1", "P2"}));
	QCOMPARE(panel.passengerList()->currentRow(), 1);
	QCOMPARE(events.log, oneChange);
	QCOMPARE(document.revision(), quint64(5));
	QCOMPARE(document.lastChangeLabel(), "Delete passenger");
}

void TestPassengerPanel::editsJourneys() {
	PassengerFixture f;
	SceneDocument& document = f.document;
	PassengerPanel& panel = f.panel;
	FakePrompts& prompts = f.prompts;
	Events& events = f.events;
	panel.addJourneyButton()->click();
	const ScenePassenger& firstPassenger = document.model().passengers[0];
	// Add appends a journey between the first station and itself and selects it.
	QCOMPARE(firstPassenger.journeys.size(), std::size_t(3));
	QCOMPARE(firstPassenger.journeys[2].id, "P1.journey");
	QCOMPARE(firstPassenger.journeys[2].originStationId, "A");
	QCOMPARE(firstPassenger.journeys[2].destinationStationId, "A");
	QCOMPARE(panel.journeyList()->currentRow(), 2);
	QCOMPARE(events.log, oneChange);
	typeAndLeave(panel.journeyActivityEdit(), "shopping");
	// The activity is written to the journey and shown in the list.
	QCOMPARE(firstPassenger.journeys[2].activity, "shopping");
	QCOMPARE(panel.journeyList()->item(2)->text(), "P1.journey (shopping)");
	choose(panel.journeyOriginCombo(), "B");
	choose(panel.journeyDestinationCombo(), "C");
	// Choosing a station writes it to the journey.
	QCOMPARE(firstPassenger.journeys[2].originStationId, "B");
	QCOMPARE(firstPassenger.journeys[2].destinationStationId, "C");
	panel.journeyWindowEdits()[2]->setValue(1234.5);
	QVERIFY2(firstPassenger.journeys[2].plannedArrivalStartSeconds == 1234.5, "a time window is written to the journey");
	events.log.clear();
	typeAndLeave(panel.journeyIdEdit(), "J1");
	// A journey id used by another journey, even of another passenger, is refused.
	QCOMPARE(prompts.errorTitle, "Duplicate Journey ID");
	QCOMPARE(prompts.errorMessage, "Choose a unique journey ID.");
	QCOMPARE(panel.journeyIdEdit()->text(), "P1.journey");
	QVERIFY(events.log.empty());
	typeAndLeave(panel.journeyIdEdit(), "J3");
	// A journey id of another passenger is refused.
	QCOMPARE(prompts.errorTitle, "Duplicate Journey ID");
	QCOMPARE(prompts.errorMessage, "Choose a unique journey ID.");
	typeAndLeave(panel.journeyIdEdit(), "J4");
	// A free journey id is written to the journey and shown in the list.
	QCOMPARE(firstPassenger.journeys[2].id, "J4");
	QCOMPARE(panel.journeyList()->item(2)->text(), "J4 (shopping)");
	QCOMPARE(events.log, oneChange);
	prompts.confirmAnswer = false;
	panel.deleteJourneyButton()->click();
	// A declined delete keeps the journey.
	QCOMPARE(prompts.confirmTitle, "Delete Journey");
	QCOMPARE(firstPassenger.journeys.size(), std::size_t(3));
	prompts.confirmAnswer = true;
	panel.deleteJourneyButton()->click();
	// A confirmed delete removes the journey.
	QCOMPARE(firstPassenger.journeys.size(), std::size_t(2));
	QCOMPARE(panel.journeyList()->currentRow(), 1);
}

// The order of the legs.
void TestPassengerPanel::movesLegs() {
	PassengerFixture f;
	SceneDocument& document = f.document;
	PassengerPanel& panel = f.panel;
	const ScenePassengerJourney& firstJourney = document.model().passengers[0].journeys[0];
	panel.moveLegDownButton()->click();
	// Move Down swaps the leg with the next one and keeps it selected.
	QCOMPARE(legIds(firstJourney), std::vector<std::string>({"L2", "L1"}));
	QCOMPARE(panel.legList()->currentRow(), 1);
	QCOMPARE(items(panel.legList()), std::vector<std::string>({"L2", "L1"}));
	QCOMPARE(f.events.log, oneChange);
	QCOMPARE(document.lastChangeLabel(), "Move leg");
	// The last leg cannot move down.
	QVERIFY(panel.moveLegUpButton()->isEnabled());
	QVERIFY(!panel.moveLegDownButton()->isEnabled());
	panel.moveLegDownButton()->click();
	QVERIFY2(legIds(firstJourney) == std::vector<std::string>({"L2", "L1"}), "a disabled Move Down does nothing");
	panel.moveLegUpButton()->click();
	// Move Up restores the order.
	QCOMPARE(legIds(firstJourney), std::vector<std::string>({"L1", "L2"}));
	QCOMPARE(panel.legList()->currentRow(), 0);
}

// Add, edit and delete a leg.
void TestPassengerPanel::editsLegs() {
	PassengerFixture f;
	SceneDocument& document = f.document;
	PassengerPanel& panel = f.panel;
	FakePrompts& prompts = f.prompts;
	Events& events = f.events;
	const ScenePassengerJourney& firstJourney = document.model().passengers[0].journeys[0];
	panel.addLegButton()->click();
	// Add appends a leg and selects it on the Legs tab.
	QCOMPARE(legIds(firstJourney), std::vector<std::string>({"L1", "L2", "J1.leg"}));
	QCOMPARE(panel.legList()->currentRow(), 2);
	QCOMPARE(panel.tabs()->currentIndex(), 1);
	QCOMPARE(events.log, oneChange);
	const ScenePassengerLeg& added = firstJourney.legs[2];
	// A new leg runs on the first service between its first two stops.
	QCOMPARE(added.serviceId, "S1");
	QCOMPARE(added.originStationId, "A");
	QCOMPARE(added.destinationStationId, "B");
	QCOMPARE(added.occurrence, 1);
	choose(panel.legServiceCombo(), "S3");
	// A leg on another service offers the stops of that service and counts its occurrences over the scene duration.
	QCOMPARE(added.serviceId, "S3");
	QCOMPARE(comboData(panel.legOriginCombo()), std::vector<std::string>({"A", "B"}));
	QCOMPARE(panel.legOccurrenceEdit()->maximum(), 2);
	panel.setOccurrenceDuration([] { return 7200.0; });
	panel.refresh();
	QVERIFY2(panel.legOccurrenceEdit()->maximum() == 4, "the occurrences are counted over the duration the window gives");
	choose(panel.legServiceCombo(), "S2");
	// A station that the new service does not stop at stays selected and is marked invalid.
	QCOMPARE(added.serviceId, "S2");
	QCOMPARE(panel.legDestinationCombo()->currentData().toString(), "B");
	QCOMPARE(panel.legDestinationCombo()->currentText(), "B (not a stop)");
	QCOMPARE(comboData(panel.legDestinationCombo()).back(), "B");
	QVERIFY2(panel.legDestinationCombo()->itemData(panel.legDestinationCombo()->currentIndex(), Qt::ToolTipRole).toString()
			== "Station B is not a stop of the service of this leg.",
		"the invalid station says why in its tooltip");
	choose(panel.legDestinationCombo(), "C");
	choose(panel.legOriginCombo(), "A");
	panel.legOccurrenceEdit()->setValue(3);
	// Choosing a stop and an occurrence writes them to the leg.
	QCOMPARE(added.destinationStationId, "C");
	QCOMPARE(added.occurrence, 3);
	QCOMPARE(panel.legOccurrenceEdit()->maximum(), 3);
	events.log.clear();
	typeAndLeave(panel.legIdEdit(), "L1");
	// A leg id that exists is refused.
	QCOMPARE(prompts.errorTitle, "Duplicate Leg ID");
	QCOMPARE(prompts.errorMessage, "Choose a unique leg ID.");
	QCOMPARE(panel.legIdEdit()->text(), "J1.leg");
	QVERIFY(events.log.empty());
	typeAndLeave(panel.legIdEdit(), "L9");
	// A free leg id is written to the leg and shown in the list.
	QCOMPARE(added.id, "L9");
	QCOMPARE(items(panel.legList()), std::vector<std::string>({"L1", "L2", "L9"}));
	QCOMPARE(events.log, oneChange);
	prompts.confirmAnswer = false;
	panel.deleteLegButton()->click();
	// A declined delete keeps the leg.
	QCOMPARE(prompts.confirmTitle, "Delete Leg");
	QCOMPARE(firstJourney.legs.size(), std::size_t(3));
	prompts.confirmAnswer = true;
	panel.deleteLegButton()->click();
	QVERIFY2(legIds(firstJourney) == std::vector<std::string>({"L1", "L2"}), "a confirmed delete removes the leg");
}

// The callouts of the diagnostics.
void TestPassengerPanel::showsDiagnosticCallouts() {
	PassengerFixture f;
	PassengerPanel& panel = f.panel;
	SceneDiagnostic legError;
	legError.severity = SceneSeverity::Error;
	legError.message = "Leg service is unknown";
	legError.path = "passengers[0].journeys[0].legs[1].service";
	SceneDiagnostic journeyWarning;
	journeyWarning.severity = SceneSeverity::Warning;
	journeyWarning.message = "Journey is odd";
	journeyWarning.itemType = "journey";
	journeyWarning.itemId = "J1";
	SceneDiagnostic otherPassenger;
	otherPassenger.severity = SceneSeverity::Info;
	otherPassenger.message = "Elsewhere";
	otherPassenger.path = "passengers[1].journeys[0]";
	std::vector<SceneDiagnostic> diagnostics = {legError, journeyWarning, otherPassenger};
	panel.setDiagnostics(&diagnostics);
	panel.refresh();
	// An error goes in the error callout.
	QCOMPARE(panel.diagnosticLabel()->text(), "Leg service is unknown");
	QCOMPARE(role(panel.diagnosticLabel()), "error");
	QVERIFY(!panel.diagnosticLabel()->isHidden());
	// A warning goes in the warning callout, apart from the error.
	QCOMPARE(panel.warningLabel()->text(), "Warning: Journey is odd");
	QCOMPARE(role(panel.warningLabel()), "warning");
	QVERIFY(!panel.warningLabel()->isHidden());
	diagnostics = {legError, otherPassenger};
	panel.refresh();
	// An error alone leaves the warning callout hidden.
	QCOMPARE(panel.diagnosticLabel()->text(), "Leg service is unknown");
	QCOMPARE(role(panel.diagnosticLabel()), "error");
	QVERIFY(panel.warningLabel()->isHidden());
	diagnostics = {journeyWarning, otherPassenger};
	panel.refresh();
	// A warning alone leaves the error callout hidden.
	QVERIFY(panel.diagnosticLabel()->isHidden());
	QCOMPARE(panel.warningLabel()->text(), "Warning: Journey is odd");
	QCOMPARE(role(panel.warningLabel()), "warning");
	QVERIFY(!panel.warningLabel()->isHidden());
	panel.passengerList()->setCurrentRow(1);
	// Another passenger shows its own messages, a note as a neutral callout.
	QCOMPARE(panel.warningLabel()->text(), "Note: Elsewhere");
	QCOMPARE(role(panel.warningLabel()), "neutral");
	QVERIFY(panel.diagnosticLabel()->isHidden());
	panel.passengerList()->setCurrentRow(0);
	panel.setDiagnostics(nullptr);
	panel.refresh();
	// A panel without diagnostics shows no callout.
	QVERIFY(panel.diagnosticLabel()->text().isEmpty());
	QVERIFY(panel.diagnosticLabel()->isHidden());
	QVERIFY(panel.warningLabel()->isHidden());
}

// A run closes the document.
void TestPassengerPanel::ignoresEditsWhileRunUsesScene() {
	PassengerFixture f;
	SceneDocument& document = f.document;
	PassengerPanel& panel = f.panel;
	const ScenePassengerJourney& firstJourney = document.model().passengers[0].journeys[0];
	const quint64 revisionBeforeRun = document.revision();
	const std::size_t passengerCount = document.model().passengers.size();
	panel.legList()->setCurrentRow(0);
	document.setEditable(false);
	panel.addPassengerButton()->click();
	panel.moveLegDownButton()->click();
	typeAndLeave(panel.passengerIdEdit(), "edited during a run");
	// A document that is closed for edits is not edited.
	QCOMPARE(document.model().passengers.size(), passengerCount);
	QCOMPARE(document.model().passengers[0].id, "P1");
	QCOMPARE(legIds(firstJourney), std::vector<std::string>({"L1", "L2"}));
	QCOMPARE(document.revision(), revisionBeforeRun);
	QVERIFY(f.events.log.empty());
	panel.refresh();
	// A refresh disables the controls while the document is closed for edits.
	QVERIFY(!panel.passengerList()->isEnabled());
	QVERIFY(!panel.addPassengerButton()->isEnabled());
	QVERIFY(!panel.importButton()->isEnabled());
	QVERIFY(!panel.passengerIdEdit()->isEnabled());
	QVERIFY(!panel.legList()->isEnabled());
	document.setEditable(true);
	panel.refresh();
	// A refresh enables the controls when the document is open again.
	QVERIFY(panel.passengerList()->isEnabled());
	QVERIFY(panel.addPassengerButton()->isEnabled());
	QVERIFY(panel.legIdEdit()->isEnabled());
}

void TestPassengerPanel::showsAnotherScene() {
	PassengerFixture f;
	f.document.replace(passengerScene(), false);
	// Opening another scene shows it.
	QCOMPARE(items(f.panel.passengerList()), std::vector<std::string>({"P1", "P2"}));
	QCOMPARE(f.panel.passengerList()->currentRow(), 0);
	QCOMPARE(items(f.panel.journeyList()), std::vector<std::string>({"J1 (commute)", "J2"}));
}

void TestPassengerPanel::detachedPanelIgnoresDocument() {
	PassengerFixture f;
	f.panel.setDocument(nullptr);
	// A detached panel shows nothing.
	QCOMPARE(f.panel.passengerList()->count(), 0);
	QVERIFY(!f.panel.addPassengerButton()->isEnabled());
	const quint64 detachedRevision = f.document.revision();
	f.panel.addPassengerButton()->click();
	f.document.replace(passengerScene(), false);
	// A detached panel neither edits nor follows the document.
	QCOMPARE(f.panel.passengerList()->count(), 0);
	QCOMPARE(f.document.revision(), detachedRevision + 1);
}

// Opening a scene that lists the selected passenger at another row keeps it selected.
void TestPassengerPanel::keepsSelectionOfReorderedScene() {
	SceneDocument document;
	PassengerPanel panel;
	panel.setDocument(&document);
	document.replace(passengerScene(), false);
	panel.passengerList()->setCurrentRow(1);
	SceneModel reordered = passengerScene();
	std::reverse(reordered.passengers.begin(), reordered.passengers.end());
	document.replace(reordered, false);
	QCOMPARE(items(panel.passengerList()), std::vector<std::string>({"P2", "P1"}));
	QCOMPARE(panel.passengerList()->currentRow(), 0);
	QCOMPARE(panel.passengerIdEdit()->text(), "P2");
}

// Text that is still being typed stays when the service panel adds a service, which shows the passengers again: the panel
// takes the text into the document first, as an edit of its own after the other one.
void TestPassengerPanel::keepsTypedTextWhenServiceIsAdded() {
	ServiceEditFixture f;
	f.passengers.passengerIdEdit()->setText("P1_typed");
	f.services.addButton()->click();
	// The service is added, and the typed id is taken in after it.
	QCOMPARE(f.document.model().services.size(), std::size_t(4));
	QCOMPARE(f.document.model().passengers[0].id, "P1_typed");
	QCOMPARE(f.labels, Log({"Add service", "Rename passenger"}));
	QCOMPARE(f.kinds[0], SceneDocument::ChangeKinds(SceneDocument::Services));
	QCOMPARE(f.kinds[1], SceneDocument::ChangeKinds(SceneDocument::Passengers));
	// The field and the list show the typed id.
	QCOMPARE(f.passengers.passengerIdEdit()->text(), "P1_typed");
	QCOMPARE(items(f.passengers.passengerList()), std::vector<std::string>({"P1_typed", "P2"}));
}

// The panel takes the text while the other panel is still in the middle of its edit, and says nothing then. The window
// answers changed() by taking the text of every panel, and the service panel, whose list still shows a deleted service,
// would give the service after it the id of the deleted one.
void TestPassengerPanel::takesTypedTextQuietlyDuringOtherEdit() {
	ServiceEditFixture f;
	// As in the window, a change that one panel reports takes the text of the other.
	QObject::connect(&f.passengers, &PassengerPanel::changed, &f.services, [&f] { f.services.commitPendingEditors(); });
	QObject::connect(&f.services, &ServicePanel::changed, &f.passengers, [&f] { f.passengers.commitPendingEditors(); });
	f.services.addButton()->click();
	f.services.listWidget()->setCurrentRow(2);
	f.labels.clear();
	f.passengers.passengerIdEdit()->setText("P1_typed");
	f.services.deleteButton()->click();
	// S3 is deleted, the service after it keeps its id, and the typed id is taken in after the delete.
	std::vector<std::string> serviceIds;
	for (const SceneService& service : f.document.model().services)
		serviceIds.push_back(service.id);
	QCOMPARE(serviceIds, std::vector<std::string>({"S1", "S2", "new_service"}));
	QCOMPARE(f.document.model().passengers[0].id, "P1_typed");
	QCOMPARE(f.labels, Log({"Delete service", "Rename passenger"}));
}

// Text that is still being typed in the service and passenger panels stays when the infrastructure panel adds a track,
// which shows both again. The service panel follows the document first here, so the rename of the typed service, which
// changes the passengers too, has the passenger panel take its text inside the take of the service panel. Then a
// composition that is added has the service panel take its text again.
void TestPassengerPanel::keepsTypedServiceAndPassengerWhenTrackIsAdded() {
	SceneDocument document;
	Log labels;
	std::vector<SceneDocument::ChangeKinds> kinds;
	QObject::connect(&document, &SceneDocument::changed, &document, [&](SceneDocument::ChangeKinds changedKinds) {
		labels.push_back(document.lastChangeLabel().toStdString());
		kinds.push_back(changedKinds);
	});
	FakePrompts prompts;
	ServicePanel services;
	PassengerPanel passengers;
	InfrastructurePanel infrastructure;
	CompositionPanel compositions;
	services.setPrompts(&prompts);
	passengers.setPrompts(&prompts);
	infrastructure.setPrompts(&prompts);
	compositions.setPrompts(&prompts);
	for (std::unique_ptr<InfrastructureFacet>& facet : makeTopologyFacets())
		infrastructure.setFacet(std::move(facet));
	services.setDocument(&document);
	passengers.setDocument(&document);
	infrastructure.setDocument(&document);
	compositions.setDocument(&document);
	document.replace(passengerScene(), false);
	// The window shows the infrastructure of a scene that it opens.
	infrastructure.refresh();
	infrastructure.facetCombo()->setCurrentIndex(infrastructure.facetCombo()->findData("tracks"));
	services.idEdit()->setText("S1_typed");
	passengers.passengerIdEdit()->setText("P1_typed");
	infrastructure.addButton()->click();
	// The track is added, then the typed service id, which the legs of P1 follow, and then the typed passenger id.
	QCOMPARE(document.model().tracks.size(), std::size_t(1));
	QCOMPARE(document.model().services[0].id, "S1_typed");
	const ScenePassenger& passenger = document.model().passengers[0];
	QCOMPARE(passenger.id, "P1_typed");
	QCOMPARE(passenger.journeys[0].legs[0].serviceId, "S1_typed");
	QCOMPARE(labels, Log({"Add track", "Rename service", "Rename passenger"}));
	QCOMPARE(kinds[0], SceneDocument::ChangeKinds(SceneDocument::Infrastructure));
	QCOMPARE(kinds[1], SceneDocument::ChangeKinds(SceneDocument::Services | SceneDocument::Scenarios | SceneDocument::Passengers));
	QCOMPARE(kinds[2], SceneDocument::ChangeKinds(SceneDocument::Passengers));
	// The fields and the lists show the typed ids.
	QCOMPARE(services.idEdit()->text(), "S1_typed");
	QCOMPARE(passengers.passengerIdEdit()->text(), "P1_typed");
	QCOMPARE(items(services.listWidget()), std::vector<std::string>({"S1_typed", "S2", "S3"}));
	QCOMPARE(items(passengers.passengerList()), std::vector<std::string>({"P1_typed", "P2"}));

	labels.clear();
	services.idEdit()->setText("S1_again");
	compositions.addButton()->click();
	// The composition is added, and the typed service id is taken in after it.
	QCOMPARE(document.model().compositions.size(), std::size_t(1));
	QCOMPARE(document.model().services[0].id, "S1_again");
	QCOMPARE(labels, Log({"Add composition", "Rename service"}));
	QCOMPARE(services.idEdit()->text(), "S1_again");
}

void TestPassengerPanel::forgetsDeletedDocument() {
	auto* document = new SceneDocument;
	PassengerPanel panel;
	FakePrompts prompts;
	prompts.confirmAnswer = true;
	panel.setPrompts(&prompts);
	panel.setDocument(document);
	document->replace(passengerScene(), false);
	delete document;
	panel.addPassengerButton()->click();
	panel.passengerIdEdit()->setText("after the document");
	panel.commitPendingEditors();
	panel.refresh();
	// A panel whose document is gone shows nothing and edits nothing.
	QCOMPARE(panel.passengerList()->count(), 0);
	QVERIFY(!panel.addPassengerButton()->isEnabled());
	QCOMPARE(prompts.errorCount, 0);
}

// An import, an import that fails, one without a folder, and the result table that the imports leave.
void TestPassengerPanel::importsPassengers() {
	PassengerFixture f;
	SceneDocument& document = f.document;
	PassengerPanel& panel = f.panel;
	FakePrompts& prompts = f.prompts;
	Events& events = f.events;

	QTemporaryDir fixture;
	// The legacy passenger fixture can be written.
	QVERIFY(fixture.isValid());
	QVERIFY(writePassengerFolder(fixture,
		"x,imp1,x,x,1,import,C,x,x,x,8.25,x,A,x,7.25\n"
		"x,P2,x,x,1,collision,C,x,x,x,8.25,x,A,x,7.25\n"
		"x,imp_unresolved,x,x,1,unresolved,C,x,x,x,8.25,x,missing-origin,x,7.25\n"
		"malformed,row\n",
		"imp1,C,0,S1-1\n"
		"P2,C,0,S1-1\n"
		"imp_unresolved,C,0,missing-service-1\n"
		"bad\n"));
	QString chosenFolder = fixture.path();
	int asked = 0;
	panel.setImportFolderChooser([&] {
		++asked;
		return chosenFolder;
	});
	const std::size_t reportsBefore = document.model().importReport.size();
	panel.importButton()->click();
	const auto statusOf = [&](int row) { return panel.importResultTable()->item(row, 3)->text(); };
	std::vector<std::string> rows;
	for (int row = 0; row < panel.importResultTable()->rowCount(); ++row) {
		std::string text;
		for (int column = 0; column < panel.importResultTable()->columnCount(); ++column)
			text += (column == 0 ? "" : " | ") + panel.importResultTable()->item(row, column)->text().toStdString();
		rows.push_back(text);
	}
	const std::vector<std::string> fileRows = {
		"DAS_FrenchCaseStudy.csv | 1 | imp1 | Accepted | Accepted",
		"DAS_FrenchCaseStudy.csv | 2 | P2 | Rejected | Passenger ID already exists",
		"DAS_FrenchCaseStudy.csv | 3 | imp_unresolved | Unresolved | Accepted with unresolved station reference",
		"DAS_FrenchCaseStudy.csv | 4 | - | Rejected | Malformed passenger DAS row",
		"RouteChoiceFC_EQ1.csv | 1 | imp1 | Accepted | Accepted",
		"RouteChoiceFC_EQ1.csv | 2 | P2 | Rejected | Passenger ID already exists",
		"RouteChoiceFC_EQ1.csv | 3 | imp_unresolved | Unresolved | Accepted with unresolved service or station reference",
		"RouteChoiceFC_EQ1.csv | 4 | - | Rejected | Malformed passenger route-choice row"};
	// The result table lists the rows of both files with their accepted, rejected and unresolved status, after the passenger
	// that collides.
	QCOMPARE(asked, 1);
	QCOMPARE(rows.size(), fileRows.size() + 1);
	QVERIFY(rows[0].find(" (batch) | - | P2 | Rejected | Passenger, journey or leg ID already exists") != std::string::npos);
	QCOMPARE(std::vector<std::string>(rows.begin() + 1, rows.end()), fileRows);
	// The result table appears with the first row, under its heading.
	QVERIFY(!panel.importResultTable()->isHidden());
	QVERIFY(!panel.importResultHeading()->isHidden());
	QCOMPARE(panel.importResultHeading()->text(), "Import results");
	bool cellsExplained = true;
	for (int row = 0; row < panel.importResultTable()->rowCount(); ++row) {
		for (int column = 0; column < panel.importResultTable()->columnCount(); ++column) {
			const QTableWidgetItem* item = panel.importResultTable()->item(row, column);
			cellsExplained = cellsExplained && (column == 0 ? item->toolTip().contains(item->text().section(' ', 0, 0)) && item->toolTip().size() > item->text().section(' ', 0, 0).size() : item->toolTip() == item->text());
		}
	}
	QVERIFY2(cellsExplained, "every cell of the result table has its text as tooltip, and the source its path");
	panel.resize(1000, 1800);
	panel.show();
	// The panel is hidden before the fixture destroys it, also when a check fails.
	const auto hide = qScopeGuard([&panel] { panel.hide(); });
	QApplication::processEvents();
	QTableWidget* resultTable = panel.importResultTable();
	QVERIFY2(resultTable->viewport()->height() >= 8 * resultTable->verticalHeader()->defaultSectionSize(),
		"the result table shows at least eight rows");
	QVERIFY2(items(panel.passengerList()) == std::vector<std::string>({"P1", "P2", "imp1", "imp_unresolved"}),
		"the accepted and the unresolved passengers are appended, and the one that collides is not");
	// The import is recorded in the report and is one change.
	QCOMPARE_GT(document.model().importReport.size(), reportsBefore);
	QVERIFY(document.isDirty());
	QCOMPARE(document.lastChangeLabel(), "Import passengers");
	QCOMPARE(events.log, oneChange);
	QVERIFY2(events.status == std::vector<std::string>({"Imported 2 passengers, 1 rejected"}),
		"the status message counts the accepted and rejected passengers");
	QVERIFY2(prompts.errorCount == 0, "an import that read its files reports no error");

	// A rejected import.
	const ScenePassenger lastBefore = document.model().passengers.back();
	const std::size_t passengersBefore = document.model().passengers.size();
	const quint64 revisionBefore = document.revision();
	events.log.clear();
	QTemporaryDir partial;
	// The partial fixture can be written.
	QVERIFY(partial.isValid());
	QVERIFY(writePassengerFolder(partial, "", ""));
	QFile::remove(QDir(partial.path()).filePath("Passengers/RouteChoiceFC_EQ1.csv"));
	chosenFolder = partial.path();
	panel.importButton()->click();
	// An import that fails leaves the scene alone.
	QCOMPARE(document.model().passengers.size(), passengersBefore);
	QCOMPARE(document.model().passengers.back().id, lastBefore.id);
	QCOMPARE(document.revision(), revisionBefore);
	QVERIFY(events.log.empty());
	// An import that fails shows one rejected row and tells the user why.
	QCOMPARE(panel.importResultTable()->rowCount(), 1);
	QCOMPARE(statusOf(0), "Rejected");
	QCOMPARE(prompts.errorCount, 1);
	QCOMPARE(prompts.errorTitle, "Passenger Import Failed");
	QCOMPARE(events.status.back(), "Passenger import failed. Nothing was changed");

	// No folder.
	chosenFolder.clear();
	panel.importButton()->click();
	// An import without a folder does nothing.
	QCOMPARE(asked, 3);
	QCOMPARE(panel.importResultTable()->rowCount(), 1);
	QCOMPARE(prompts.errorCount, 1);
	QVERIFY(events.log.empty());

	// The table is cleared with the scene.
	panel.clearImportResult();
	// Clearing the import result empties and hides the table and its heading.
	QCOMPARE(panel.importResultTable()->rowCount(), 0);
	QVERIFY(panel.importResultTable()->isHidden());
	QVERIFY(panel.importResultHeading()->isHidden());
	chosenFolder = fixture.path();
	panel.importButton()->click();
	QVERIFY2(panel.importResultTable()->rowCount() > 0, "the table fills again with the next import");
	panel.setDocument(nullptr);
	// A panel without a scene shows no import result.
	QCOMPARE(panel.importResultTable()->rowCount(), 0);
	QVERIFY(panel.importResultTable()->isHidden());
	QVERIFY(panel.importResultHeading()->isHidden());

	// An import needs a document open for edits.
	panel.setDocument(&document);
	const int askedBefore = asked;
	document.setEditable(false);
	panel.importButton()->click();
	QVERIFY2(asked == askedBefore, "Import does not ask for a folder while the document is closed for edits");
}

// The callouts list the messages of the selected passenger only, and at most five lines each.
void TestPassengerPanel::calloutsShowSelectedPassenger() {
	SceneDocument document;
	PassengerPanel panel;
	panel.setDocument(&document);
	SceneModel scene = passengerScene();
	scene.passengers.clear();
	ScenePassenger first;
	first.id = "X1";
	for (int index = 0; index < 8; ++index) {
		ScenePassengerJourney journey;
		journey.id = "XJ" + std::to_string(index);
		journey.originStationId = "A";
		journey.destinationStationId = "B";
		first.journeys.push_back(journey);
	}
	ScenePassenger second;
	second.id = "X2";
	ScenePassengerJourney otherJourney;
	otherJourney.id = "OJ";
	otherJourney.originStationId = "A";
	otherJourney.destinationStationId = "B";
	second.journeys.push_back(otherJourney);
	scene.passengers = {first, second};
	document.replace(scene, false);

	const auto diagnostic = [](SceneSeverity severity, const char* message, const char* type, const char* id, const std::string& path) {
		SceneDiagnostic value;
		value.severity = severity;
		value.message = message;
		value.itemType = type;
		value.itemId = id;
		value.path = path;
		return value;
	};
	std::vector<SceneDiagnostic> diagnostics;
	diagnostics.push_back(diagnostic(SceneSeverity::Error, "Scene has no duration", "scene", "", ""));
	diagnostics.push_back(diagnostic(SceneSeverity::Warning, "Scene note", "", "", "settings"));
	for (int index = 0; index < 8; ++index) {
		diagnostics.push_back(diagnostic(SceneSeverity::Warning, "Journey has no route-choice legs", "journey",
			("XJ" + std::to_string(index)).c_str(), "passengers[0].journeys[" + std::to_string(index) + "].legs"));
	}
	for (int index = 0; index < 7; ++index)
		diagnostics.push_back(diagnostic(SceneSeverity::Error, "Journey window is wrong", "", "", "passengers[0].journeys[0].window"));
	diagnostics.push_back(diagnostic(SceneSeverity::Error, "Other passenger error", "journey", "OJ", "passengers[1].journeys[0]"));
	diagnostics.push_back(diagnostic(SceneSeverity::Warning, "Other leg note", "leg", "OL", ""));
	panel.setDiagnostics(&diagnostics);
	panel.refresh();
	panel.passengerList()->setCurrentRow(0);
	// The first journey of the passenger is selected and has no leg.
	QCOMPARE(panel.legList()->count(), 0);
	QCOMPARE(panel.journeyList()->currentRow(), 0);
	const QStringList errorLines = panel.diagnosticLabel()->text().split('\n');
	// The errors are those of the passenger, five lines and the count of the rest.
	QCOMPARE(errorLines.size(), qsizetype(6));
	QCOMPARE(errorLines.front(), "Journey window is wrong");
	QCOMPARE(errorLines.back(), "and 2 more");
	QVERIFY(!panel.diagnosticLabel()->text().contains("Scene"));
	QVERIFY(!panel.diagnosticLabel()->text().contains("Other"));
	// The tooltip of the error callout lists all of them.
	QCOMPARE(panel.diagnosticLabel()->toolTip().split('\n').size(), qsizetype(7));
	QVERIFY(!panel.diagnosticLabel()->toolTip().contains("and 2 more"));
	const QStringList noteLines = panel.warningLabel()->text().split('\n');
	// The warnings are those of the passenger, five lines and the count of the rest.
	QCOMPARE(noteLines.size(), qsizetype(6));
	QCOMPARE(noteLines.front(), "Warning: Journey has no route-choice legs");
	QCOMPARE(noteLines.back(), "and 3 more");
	QVERIFY(!panel.warningLabel()->text().contains("Scene"));
	QVERIFY(!panel.warningLabel()->text().contains("Other"));
	QVERIFY2(panel.warningLabel()->toolTip().split('\n').size() == 8, "the tooltip of the warning callout lists all of them");

	panel.passengerList()->setCurrentRow(1);
	// Another passenger shows only its own message, without a tooltip when all of it is shown.
	QCOMPARE(panel.diagnosticLabel()->text(), "Other passenger error");
	QVERIFY(panel.diagnosticLabel()->toolTip().isEmpty());
	QVERIFY(panel.warningLabel()->isHidden());
	QVERIFY(panel.warningLabel()->toolTip().isEmpty());

	// The Legs tab of a journey without a leg offers Add and nothing else.
	// A journey without a leg enables Add only.
	QVERIFY(panel.addLegButton()->isEnabled());
	QVERIFY(!panel.legOccurrenceEdit()->isEnabled());
	QVERIFY(!panel.legIdEdit()->isEnabled());
	QVERIFY(!panel.legOriginCombo()->isEnabled());
	QVERIFY(!panel.legServiceCombo()->isEnabled());
	QVERIFY(!panel.deleteLegButton()->isEnabled());
	panel.journeyList()->setCurrentRow(-1);
	// Add and Occurrence are off while no journey is selected.
	QVERIFY(!panel.addLegButton()->isEnabled());
	QVERIFY(!panel.legOccurrenceEdit()->isEnabled());
	panel.passengerList()->setCurrentRow(0);
	// A passenger with journeys selects one, and Add is on.
	QVERIFY(panel.addLegButton()->isEnabled());
	QVERIFY(!panel.legOccurrenceEdit()->isEnabled());
}

// Values that the scene does not allow stay selected as invalid entries, say why, and are not written back.
void TestPassengerPanel::showsInvalidValues() {
	SceneDocument document;
	PassengerPanel panel;
	panel.setDocument(&document);
	SceneModel scene = passengerScene();
	ScenePassengerJourney& journey = scene.passengers[0].journeys[0];
	journey.originStationId = "ZZ";
	journey.destinationStationId = "";
	journey.legs[0].serviceId = "S9";
	journey.legs[0].originStationId = "C";
	journey.legs[0].destinationStationId = "";
	document.replace(scene, false);
	const auto tip = [](const QComboBox* combo) {
		return combo->itemData(combo->currentIndex(), Qt::ToolTipRole).toString().toStdString();
	};
	// A journey station that is unknown or empty is shown as invalid with its reason.
	QCOMPARE(panel.journeyOriginCombo()->currentText(), "ZZ (missing)");
	QCOMPARE(tip(panel.journeyOriginCombo()), "Station ZZ is not in this case.");
	QCOMPARE(panel.journeyDestinationCombo()->currentText(), "(none)");
	QCOMPARE(tip(panel.journeyDestinationCombo()), "No station is set.");
	// A leg service or station that is unknown, not served or empty is shown as invalid with its reason.
	QCOMPARE(panel.legServiceCombo()->currentText(), "S9 (missing)");
	QCOMPARE(tip(panel.legServiceCombo()), "Service S9 is not in this case.");
	QCOMPARE(panel.legOriginCombo()->currentText(), "C (not a stop)");
	QCOMPARE(tip(panel.legOriginCombo()), "Station C is not a stop of the service of this leg.");
	QCOMPARE(panel.legDestinationCombo()->currentText(), "(none)");
	QCOMPARE(tip(panel.legDestinationCombo()), "No station is set.");
	// The choices that are valid have no tooltip.
	QVERIFY(panel.journeyOriginCombo()->itemData(0, Qt::ToolTipRole).toString().isEmpty());
	QVERIFY(panel.legServiceCombo()->itemData(0, Qt::ToolTipRole).toString().isEmpty());
	// Showing invalid values does not write them back.
	QCOMPARE(document.model().passengers[0].journeys[0].originStationId, "ZZ");
	QCOMPARE(document.model().passengers[0].journeys[0].legs[0].serviceId, "S9");
	QCOMPARE(document.revision(), quint64(1));
	QVERIFY(!document.isDirty());
}

// An imported passenger whose id, or the id of one of its journeys or legs, the scene already has is rejected, and the
// others are added in their order.
void TestPassengerPanel::importRejectsTakenIds() {
	const SceneModel scene = passengerScene();
	ScenePassengerImportResult imported;
	imported.passengers = {
		importedPassenger("P1", {{"J10", {"L10"}}}),
		importedPassenger("P3", {{"J1", {"L11"}}}),
		importedPassenger("P4", {{"J12", {"L2"}}}),
		importedPassenger("P5", {{"J13", {"L13", "L14"}}, {"J14", {}}}),
		importedPassenger("P6", {{"J15", {"L15"}}}),
	};
	const PassengerImportMerge merge = mergeImportedPassengers(scene, imported, "/import");
	QCOMPARE(passengerIds(merge.accepted), std::vector<std::string>({"P5", "P6"}));
	QCOMPARE(merge.accepted[0].journeys.size(), std::size_t(2));
	QCOMPARE(merge.rejected, std::vector<std::string>({"P1", "P3", "P4"}));
	QVERIFY(merge.rejects("P3"));
	QVERIFY(!merge.rejects("P5"));
	QVERIFY(merge.changesScene());
}

// An id that repeats inside one imported passenger, or that an earlier accepted passenger of the import has, rejects the
// passenger. The ids of a rejected passenger stay free.
void TestPassengerPanel::importRejectsRepeatedIds() {
	const SceneModel scene = passengerScene();
	ScenePassengerImportResult imported;
	imported.passengers = {
		importedPassenger("P10", {{"J20", {"L20"}}, {"J20", {"L21"}}}),
		importedPassenger("P11", {{"J22", {"L22"}}, {"J23", {"L22"}}}),
		importedPassenger("P12", {{"J24", {"L24"}}}),
		importedPassenger("P12", {{"J25", {"L25"}}}),
		importedPassenger("P13", {{"J24", {"L26"}}}),
		importedPassenger("P14", {{"J27", {"L24"}}}),
		importedPassenger("P15", {{"J20", {"L22"}}}),
	};
	const PassengerImportMerge merge = mergeImportedPassengers(scene, imported, "/import");
	QCOMPARE(passengerIds(merge.accepted), std::vector<std::string>({"P12", "P15"}));
	QCOMPARE(merge.accepted[0].journeys[0].id, "J24");
	QCOMPARE(merge.rejected, std::vector<std::string>({"P10", "P11", "P12", "P13", "P14"}));
}

// An empty passenger, journey or leg id rejects the passenger. A rejected passenger without an id names no row of the
// import as rejected.
void TestPassengerPanel::importRejectsEmptyIds() {
	const SceneModel scene = passengerScene();
	ScenePassengerImportResult imported;
	imported.passengers = {
		importedPassenger("", {{"J30", {"L30"}}}),
		importedPassenger("P31", {{"", {"L31"}}}),
		importedPassenger("P32", {{"J32", {""}}}),
		importedPassenger("P33", {{"J33", {"L33"}}}),
	};
	const PassengerImportMerge merge = mergeImportedPassengers(scene, imported, "/import");
	QCOMPARE(passengerIds(merge.accepted), std::vector<std::string>({"P33"}));
	QCOMPARE(merge.rejected, std::vector<std::string>({"", "P31", "P32"}));
	QVERIFY(!merge.rejects(""));
	QVERIFY(merge.rejects("P32"));
}

// The report rows of the import are kept, and a row of the editor counts the accepted and rejected passengers when one was
// rejected. An import without passengers or rows does not change the scene.
void TestPassengerPanel::importReportsRejectedPassengers() {
	const SceneModel scene = passengerScene();
	ScenePassengerImportResult imported;
	imported.report = {{"passengers", "/import/DAS.csv", 3, 3, 0, 1}};
	imported.passengers = {importedPassenger("P1", {{"J40", {"L40"}}}), importedPassenger("P41", {{"J41", {"L41"}}})};
	PassengerImportMerge merge = mergeImportedPassengers(scene, imported, "/import");
	QCOMPARE(merge.report.size(), std::size_t(2));
	QCOMPARE(merge.report[0].category, "passengers");
	QCOMPARE(merge.report[0].unresolvedReferences, 1);
	const SceneImportReportRow& editorRow = merge.report[1];
	QCOMPARE(editorRow.category, "passengers.editor");
	QCOMPARE(editorRow.sourceFile, "/import");
	QCOMPARE(editorRow.sourceCount, 2);
	QCOMPARE(editorRow.convertedCount, 1);
	QCOMPARE(editorRow.skippedCount, 1);
	QCOMPARE(editorRow.unresolvedReferences, 0);

	imported.passengers.erase(imported.passengers.begin());
	merge = mergeImportedPassengers(scene, imported, "/import");
	QVERIFY2(merge.report.size() == 1, "without a rejected passenger the editor adds no row");

	imported.passengers.clear();
	merge = mergeImportedPassengers(scene, imported, "/import");
	QVERIFY2(merge.changesScene(), "report rows alone change the scene");
	imported.report.clear();
	merge = mergeImportedPassengers(scene, imported, "/import");
	QVERIFY(merge.accepted.empty());
	QVERIFY(merge.report.empty());
	QVERIFY(!merge.changesScene());
}

QTEST_MAIN(TestPassengerPanel)
#include "test_editor_passenger.moc"
