#include "editors/PassengerPanel.h"

#include "scene/SceneDiagnostic.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>

#include <iostream>
#include <string>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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

// Answers the questions of the panel and keeps what it was asked and told, without a dialog.
struct RecordingPrompts : UserPrompts {
	bool answer = true;
	std::vector<std::string> questions;
	std::vector<std::string> errors;

	void showError(QWidget*, const QString& title, const QString& message, bool) override {
		errors.push_back(title.toStdString() + ": " + message.toStdString());
	}
	bool confirm(QWidget*, const QString& title, const QString&) override {
		questions.push_back(title.toStdString());
		return answer;
	}
};

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

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;

	{
		PassengerPanel panel;
		ok &= expect(panel.passengerList()->count() == 0 && !panel.passengerList()->isEnabled()
				&& !panel.addPassengerButton()->isEnabled() && !panel.importButton()->isEnabled()
				&& panel.tabs()->isHidden(),
			"a panel without a document shows no passengers and offers no action");
		ok &= expect(panel.diagnosticLabel()->text() == "Select a passenger or use Add to create one."
				&& role(panel.diagnosticLabel()) == "help" && !panel.diagnosticLabel()->isHidden()
				&& panel.journeyOriginCombo()->itemText(0) == "Select a journey or leg"
				&& panel.legServiceCombo()->itemText(0) == "Select a leg",
			"a panel without a selection says what to select");
		SceneDocument document;
		panel.setDocument(&document);
		panel.addPassengerButton()->click();
		ok &= expect(document.model().passengers.empty() && document.revision() == 0 && !document.isDirty(),
			"Add while the document holds no scene changes nothing");
	}

	{
		SceneDocument document;
		PassengerPanel panel;
		RecordingPrompts prompts;
		panel.setPrompts(&prompts);
		panel.setDocument(&document);
		Events events(panel, document);

		// refresh
		document.replace(passengerScene(), false);
		ok &= expect(items(panel.passengerList()) == std::vector<std::string>({"P1", "P2"})
				&& panel.passengerList()->currentRow() == 0 && panel.passengerList()->isEnabled()
				&& panel.addPassengerButton()->isEnabled() && panel.importButton()->isEnabled()
				&& !panel.tabs()->isHidden(),
			"a new scene is shown in the passenger list with the first passenger selected");
		ok &= expect(items(panel.journeyList()) == std::vector<std::string>({"J1 | commute", "J2"})
				&& items(panel.legList()) == std::vector<std::string>({"L1", "L2"})
				&& panel.journeyList()->currentRow() == 0 && panel.legList()->currentRow() == 0,
			"the journeys of the passenger and the legs of its first journey are listed with the first of each selected");
		ok &= expect(panel.passengerIdEdit()->text() == "P1" && panel.journeyIdEdit()->text() == "J1"
				&& panel.journeyActivityEdit()->text() == "commute" && panel.legIdEdit()->text() == "L1",
			"the id and activity fields show the selection");
		ok &= expect(panel.journeyOriginCombo()->currentText() == "A | Station A"
				&& panel.journeyOriginCombo()->currentData().toString() == "A"
				&& panel.journeyDestinationCombo()->currentData().toString() == "C",
			"the journey stations are shown with their names");
		const auto& windows = panel.journeyWindowEdits();
		ok &= expect(windows[0]->value() == 100.0 && windows[1]->value() == 200.0 && windows[2]->value() == 300.0
				&& windows[3]->value() == 400.0,
			"the four time windows of the journey are shown");
		ok &= expect(panel.legServiceCombo()->currentData().toString() == "S1"
				&& comboData(panel.legServiceCombo()) == std::vector<std::string>({"S1", "S2", "S3"})
				&& panel.legOriginCombo()->currentData().toString() == "A"
				&& panel.legDestinationCombo()->currentData().toString() == "B"
				&& comboData(panel.legOriginCombo()) == std::vector<std::string>({"A", "B", "C"}),
			"the leg shows its service and stations, and offers the stops of the service");
		ok &= expect(panel.legOccurrenceEdit()->value() == 1 && panel.legOccurrenceEdit()->minimum() == 0
				&& panel.legOccurrenceEdit()->maximum() == 1,
			"a service that runs once allows the occurrences 0 and 1");
		ok &= expect(!panel.moveLegUpButton()->isEnabled() && panel.moveLegDownButton()->isEnabled()
				&& panel.addLegButton()->isEnabled() && panel.deleteLegButton()->isEnabled(),
			"the first leg cannot move up");
		ok &= expect(events.log.empty() && !document.isDirty() && document.revision() == 1,
			"showing a scene edits nothing");

		// selection
		panel.passengerList()->setCurrentRow(1);
		ok &= expect(items(panel.journeyList()) == std::vector<std::string>({"J3"})
				&& items(panel.legList()) == std::vector<std::string>({"L3"})
				&& panel.legServiceCombo()->currentData().toString() == "S2"
				&& comboData(panel.legOriginCombo()) == std::vector<std::string>({"A", "C"})
				&& panel.legOccurrenceEdit()->value() == 2 && panel.legOccurrenceEdit()->maximum() == 3,
			"selecting another passenger shows its journey and leg, and counts the occurrences of its service");
		ok &= expect(events.log.empty() && document.revision() == 1, "selecting edits nothing");
		panel.passengerList()->setCurrentRow(0);
		panel.journeyList()->setCurrentRow(1);
		ok &= expect(panel.legList()->count() == 0 && !panel.legIdEdit()->isEnabled() && panel.addLegButton()->isEnabled()
				&& panel.legServiceCombo()->itemText(0) == "Select a leg"
				&& panel.legOriginCombo()->itemText(0) == "Select a journey or leg",
			"a journey without legs shows an empty leg editor that still allows Add");
		panel.refresh();
		ok &= expect(panel.journeyList()->currentRow() == 1, "refreshing keeps the selected journey");
		panel.journeyList()->setCurrentRow(0);

		// passenger edits
		typeAndLeave(panel.passengerIdEdit(), "  P9 ");
		ok &= expect(document.model().passengers[0].id == "P9" && panel.passengerList()->item(0)->text() == "P9"
				&& document.isDirty() && document.revision() == 2 && document.lastChangeLabel() == "Rename passenger",
			"a finished passenger id is trimmed, written to the document and shown in the list");
		ok &= expect(events.log == oneChange, "a rename is one change of the document and then of the panel");
		events.log.clear();
		typeAndLeave(panel.passengerIdEdit(), "P2");
		ok &= expect(prompts.errors == std::vector<std::string>({"Passenger ID already exists: Choose a unique passenger ID."})
				&& panel.passengerIdEdit()->text() == "P9" && document.model().passengers[0].id == "P9"
				&& document.revision() == 2 && events.log.empty(),
			"a passenger id that exists is refused with the old text restored");
		typeAndLeave(panel.passengerIdEdit(), "");
		ok &= expect(prompts.errors.size() == 2
				&& prompts.errors[1] == "Invalid Passenger ID: Passenger IDs must not be empty."
				&& panel.passengerIdEdit()->text() == "P9" && document.revision() == 2 && events.log.empty(),
			"an empty passenger id is refused with the old text restored");
		typeAndLeave(panel.passengerIdEdit(), "P9");
		ok &= expect(document.revision() == 2 && events.log.empty() && prompts.errors.size() == 2,
			"leaving the id unchanged does not touch the document");

		// pending text and cancel
		panel.passengerIdEdit()->setText("typed, not finished");
		panel.refresh();
		ok &= expect(panel.passengerIdEdit()->text() == "P9" && document.model().passengers[0].id == "P9"
				&& document.revision() == 2 && events.log.empty(),
			"refreshing drops text that was not finished and leaves the document as it is");
		panel.passengerIdEdit()->setText("P1");
		panel.commitPendingEditors();
		ok &= expect(document.model().passengers[0].id == "P1" && document.revision() == 3 && events.log == oneChange,
			"committing the pending editors writes the text of the id field");
		events.log.clear();
		panel.commitPendingEditors();
		ok &= expect(document.revision() == 3 && events.log.empty(), "committing with nothing pending changes nothing");

		// add and delete a passenger
		panel.addPassengerButton()->click();
		ok &= expect(items(panel.passengerList()) == std::vector<std::string>({"P1", "P2", "new_passenger"})
				&& panel.passengerList()->currentRow() == 2 && panel.passengerIdEdit()->text() == "new_passenger"
				&& panel.journeyList()->count() == 0 && panel.addJourneyButton()->isEnabled(),
			"Add appends a passenger with a free id and selects it");
		ok &= expect(document.lastChangeLabel() == "Add passenger" && events.log == oneChange && document.revision() == 4,
			"Add is one change");
		prompts.answer = false;
		events.log.clear();
		panel.deletePassengerButton()->click();
		ok &= expect(prompts.questions == std::vector<std::string>({"Delete Passenger"})
				&& document.model().passengers.size() == 3 && events.log.empty() && document.revision() == 4,
			"a declined delete keeps the passenger");
		prompts.answer = true;
		panel.deletePassengerButton()->click();
		ok &= expect(items(panel.passengerList()) == std::vector<std::string>({"P1", "P2"})
				&& panel.passengerList()->currentRow() == 1 && events.log == oneChange && document.revision() == 5
				&& document.lastChangeLabel() == "Delete passenger",
			"a confirmed delete removes the passenger and selects the last one that is left");
		panel.passengerList()->setCurrentRow(0);

		// journeys
		events.log.clear();
		panel.addJourneyButton()->click();
		const ScenePassenger& firstPassenger = document.model().passengers[0];
		ok &= expect(firstPassenger.journeys.size() == 3 && firstPassenger.journeys[2].id == "P1.journey"
				&& firstPassenger.journeys[2].originStationId == "A" && firstPassenger.journeys[2].destinationStationId == "A"
				&& panel.journeyList()->currentRow() == 2 && events.log == oneChange,
			"Add appends a journey between the first station and itself and selects it");
		typeAndLeave(panel.journeyActivityEdit(), "shopping");
		ok &= expect(firstPassenger.journeys[2].activity == "shopping"
				&& panel.journeyList()->item(2)->text() == "P1.journey | shopping",
			"the activity is written to the journey and shown in the list");
		choose(panel.journeyOriginCombo(), "B");
		choose(panel.journeyDestinationCombo(), "C");
		ok &= expect(firstPassenger.journeys[2].originStationId == "B" && firstPassenger.journeys[2].destinationStationId == "C",
			"choosing a station writes it to the journey");
		panel.journeyWindowEdits()[2]->setValue(1234.5);
		ok &= expect(firstPassenger.journeys[2].plannedArrivalStartSeconds == 1234.5,
			"a time window is written to the journey");
		events.log.clear();
		typeAndLeave(panel.journeyIdEdit(), "J1");
		ok &= expect(prompts.errors.back() == "Journey ID already exists: Choose a unique journey ID."
				&& panel.journeyIdEdit()->text() == "P1.journey" && events.log.empty(),
			"a journey id used by another journey, even of another passenger, is refused");
		typeAndLeave(panel.journeyIdEdit(), "J3");
		ok &= expect(prompts.errors.back() == "Journey ID already exists: Choose a unique journey ID.",
			"a journey id of another passenger is refused");
		typeAndLeave(panel.journeyIdEdit(), "J4");
		ok &= expect(firstPassenger.journeys[2].id == "J4" && panel.journeyList()->item(2)->text() == "J4 | shopping"
				&& events.log == oneChange,
			"a free journey id is written to the journey and shown in the list");
		prompts.answer = false;
		panel.deleteJourneyButton()->click();
		ok &= expect(prompts.questions.back() == "Delete Journey" && firstPassenger.journeys.size() == 3,
			"a declined delete keeps the journey");
		prompts.answer = true;
		panel.deleteJourneyButton()->click();
		ok &= expect(firstPassenger.journeys.size() == 2 && panel.journeyList()->currentRow() == 1,
			"a confirmed delete removes the journey");
		panel.journeyList()->setCurrentRow(0);

		// legs: order
		const ScenePassengerJourney& firstJourney = document.model().passengers[0].journeys[0];
		events.log.clear();
		panel.moveLegDownButton()->click();
		ok &= expect(legIds(firstJourney) == std::vector<std::string>({"L2", "L1"}) && panel.legList()->currentRow() == 1
				&& items(panel.legList()) == std::vector<std::string>({"L2", "L1"}) && events.log == oneChange
				&& document.lastChangeLabel() == "Move leg",
			"Move Down swaps the leg with the next one and keeps it selected");
		ok &= expect(panel.moveLegUpButton()->isEnabled() && !panel.moveLegDownButton()->isEnabled(),
			"the last leg cannot move down");
		panel.moveLegDownButton()->click();
		ok &= expect(legIds(firstJourney) == std::vector<std::string>({"L2", "L1"}), "a disabled Move Down does nothing");
		panel.moveLegUpButton()->click();
		ok &= expect(legIds(firstJourney) == std::vector<std::string>({"L1", "L2"}) && panel.legList()->currentRow() == 0,
			"Move Up restores the order");

		// legs: add, edit, delete
		events.log.clear();
		panel.addLegButton()->click();
		ok &= expect(legIds(firstJourney) == std::vector<std::string>({"L1", "L2", "J1.leg"})
				&& panel.legList()->currentRow() == 2 && panel.tabs()->currentIndex() == 1 && events.log == oneChange,
			"Add appends a leg and selects it on the Legs tab");
		const ScenePassengerLeg& added = firstJourney.legs[2];
		ok &= expect(added.serviceId == "S1" && added.originStationId == "A" && added.destinationStationId == "B"
				&& added.occurrence == 1,
			"a new leg runs on the first service between its first two stops");
		choose(panel.legServiceCombo(), "S3");
		ok &= expect(added.serviceId == "S3" && comboData(panel.legOriginCombo()) == std::vector<std::string>({"A", "B"})
				&& panel.legOccurrenceEdit()->maximum() == 2,
			"a leg on another service offers the stops of that service and counts its occurrences over the scene duration");
		panel.setOccurrenceDuration([] { return 7200.0; });
		panel.refresh();
		ok &= expect(panel.legOccurrenceEdit()->maximum() == 4, "the occurrences are counted over the duration the window gives");
		choose(panel.legServiceCombo(), "S2");
		ok &= expect(added.serviceId == "S2" && panel.legDestinationCombo()->currentData().toString() == "B"
				&& panel.legDestinationCombo()->currentText() == "Invalid: B" && comboData(panel.legDestinationCombo()).back() == "B",
			"a station that the new service does not stop at stays selected and is marked invalid");
		choose(panel.legDestinationCombo(), "C");
		choose(panel.legOriginCombo(), "A");
		panel.legOccurrenceEdit()->setValue(3);
		ok &= expect(added.destinationStationId == "C" && added.occurrence == 3 && panel.legOccurrenceEdit()->maximum() == 3,
			"choosing a stop and an occurrence writes them to the leg");
		events.log.clear();
		typeAndLeave(panel.legIdEdit(), "L1");
		ok &= expect(prompts.errors.back() == "Leg ID already exists: Choose a unique leg ID." && panel.legIdEdit()->text() == "J1.leg"
				&& events.log.empty(),
			"a leg id that exists is refused");
		typeAndLeave(panel.legIdEdit(), "L9");
		ok &= expect(added.id == "L9" && items(panel.legList()) == std::vector<std::string>({"L1", "L2", "L9"})
				&& events.log == oneChange,
			"a free leg id is written to the leg and shown in the list");
		prompts.answer = false;
		panel.deleteLegButton()->click();
		ok &= expect(prompts.questions.back() == "Delete Leg" && firstJourney.legs.size() == 3, "a declined delete keeps the leg");
		prompts.answer = true;
		panel.deleteLegButton()->click();
		ok &= expect(legIds(firstJourney) == std::vector<std::string>({"L1", "L2"}), "a confirmed delete removes the leg");

		// diagnostics
		SceneDiagnostic legError;
		legError.severity = SceneSeverity::Error;
		legError.message = "Leg service is unknown";
		legError.path = "passengers[0].journeys[0].legs[1].service";
		SceneDiagnostic journeyWarning;
		journeyWarning.severity = SceneSeverity::Warning;
		journeyWarning.message = "Journey is odd";
		journeyWarning.itemId = "J1";
		SceneDiagnostic otherPassenger;
		otherPassenger.severity = SceneSeverity::Info;
		otherPassenger.message = "Elsewhere";
		otherPassenger.path = "passengers[1].journeys[0]";
		std::vector<SceneDiagnostic> diagnostics = {legError, journeyWarning, otherPassenger};
		panel.setDiagnostics(&diagnostics);
		panel.refresh();
		ok &= expect(panel.diagnosticLabel()->text() == "ERROR: Leg service is unknown\nWARNING: Journey is odd"
				&& role(panel.diagnosticLabel()) == "error",
			"the messages for the selected passenger are shown as an error when there is one");
		diagnostics = {journeyWarning, otherPassenger};
		panel.refresh();
		ok &= expect(panel.diagnosticLabel()->text() == "WARNING: Journey is odd" && role(panel.diagnosticLabel()) == "warning",
			"a warning alone is shown as a warning");
		panel.passengerList()->setCurrentRow(1);
		ok &= expect(panel.diagnosticLabel()->text() == "INFO: Elsewhere", "another passenger shows its own messages");
		panel.passengerList()->setCurrentRow(0);
		panel.setDiagnostics(nullptr);
		panel.refresh();
		ok &= expect(panel.diagnosticLabel()->text() == "No validation messages for this passenger selection."
				&& role(panel.diagnosticLabel()) == "help" && !panel.diagnosticLabel()->isHidden(),
			"a panel without diagnostics shows none, as help");

		// a run closes the document
		const quint64 revisionBeforeRun = document.revision();
		const std::size_t passengerCount = document.model().passengers.size();
		events.log.clear();
		panel.legList()->setCurrentRow(0);
		document.setEditable(false);
		panel.addPassengerButton()->click();
		panel.moveLegDownButton()->click();
		typeAndLeave(panel.passengerIdEdit(), "edited during a run");
		ok &= expect(document.model().passengers.size() == passengerCount && document.model().passengers[0].id == "P1"
				&& legIds(firstJourney) == std::vector<std::string>({"L1", "L2"}) && document.revision() == revisionBeforeRun
				&& events.log.empty(),
			"a document that is closed for edits is not edited");
		panel.refresh();
		ok &= expect(!panel.passengerList()->isEnabled() && !panel.addPassengerButton()->isEnabled()
				&& !panel.importButton()->isEnabled() && !panel.passengerIdEdit()->isEnabled() && !panel.legList()->isEnabled(),
			"a refresh disables the controls while the document is closed for edits");
		document.setEditable(true);
		panel.refresh();
		ok &= expect(panel.passengerList()->isEnabled() && panel.addPassengerButton()->isEnabled() && panel.legIdEdit()->isEnabled(),
			"a refresh enables the controls when the document is open again");

		// another scene
		document.replace(passengerScene(), false);
		ok &= expect(items(panel.passengerList()) == std::vector<std::string>({"P1", "P2"}) && panel.passengerList()->currentRow() == 0
				&& items(panel.journeyList()) == std::vector<std::string>({"J1 | commute", "J2"}),
			"opening another scene shows it");

		// detach
		panel.setDocument(nullptr);
		ok &= expect(panel.passengerList()->count() == 0 && !panel.addPassengerButton()->isEnabled(), "a detached panel shows nothing");
		const quint64 detachedRevision = document.revision();
		panel.addPassengerButton()->click();
		document.replace(passengerScene(), false);
		ok &= expect(panel.passengerList()->count() == 0 && document.revision() == detachedRevision + 1,
			"a detached panel neither edits nor follows the document");
	}

	{
		auto* document = new SceneDocument;
		PassengerPanel panel;
		RecordingPrompts prompts;
		panel.setPrompts(&prompts);
		panel.setDocument(document);
		document->replace(passengerScene(), false);
		delete document;
		panel.addPassengerButton()->click();
		panel.passengerIdEdit()->setText("after the document");
		panel.commitPendingEditors();
		panel.refresh();
		ok &= expect(panel.passengerList()->count() == 0 && !panel.addPassengerButton()->isEnabled() && prompts.errors.empty(),
			"a panel whose document is gone shows nothing and edits nothing");
	}

	{
		SceneDocument document;
		PassengerPanel panel;
		RecordingPrompts prompts;
		panel.setPrompts(&prompts);
		panel.setDocument(&document);
		Events events(panel, document);
		document.replace(passengerScene(), false);

		// import
		QTemporaryDir fixture;
		ok &= expect(fixture.isValid()
				&& writePassengerFolder(fixture,
					"x,imp1,x,x,1,import,C,x,x,x,8.25,x,A,x,7.25\n"
					"x,P2,x,x,1,collision,C,x,x,x,8.25,x,A,x,7.25\n"
					"x,imp_unresolved,x,x,1,unresolved,C,x,x,x,8.25,x,missing-origin,x,7.25\n"
					"malformed,row\n",
					"imp1,C,0,S1-1\n"
					"P2,C,0,S1-1\n"
					"imp_unresolved,C,0,missing-service-1\n"
					"bad\n"),
			"the legacy passenger fixture can be written");
		QString chosenFolder = fixture.path();
		int asked = 0;
		panel.setImportFolderChooser([&] {
			++asked;
			return chosenFolder;
		});
		const std::size_t reportsBefore = document.model().importReport.size();
		panel.importButton()->click();
		const auto statusOf = [&](int row) { return panel.importResultTable()->item(row, 2)->text(); };
		std::vector<std::string> rows;
		for (int row = 0; row < panel.importResultTable()->rowCount(); ++row) {
			std::string text;
			for (int column = 0; column < panel.importResultTable()->columnCount(); ++column)
				text += (column == 0 ? "" : " | ") + panel.importResultTable()->item(row, column)->text().toStdString();
			rows.push_back(text);
		}
		const std::vector<std::string> fileRows = {
			"DAS_FrenchCaseStudy.csv | 1 | Accepted | Accepted",
			"DAS_FrenchCaseStudy.csv | 2 | Rejected | Passenger ID collides with existing or earlier imported data",
			"DAS_FrenchCaseStudy.csv | 3 | Unresolved | Accepted with unresolved station reference",
			"DAS_FrenchCaseStudy.csv | 4 | Rejected | Malformed passenger DAS row",
			"RouteChoiceFC_EQ1.csv | 1 | Accepted | Accepted",
			"RouteChoiceFC_EQ1.csv | 2 | Rejected | Passenger ID collides with existing or earlier imported data",
			"RouteChoiceFC_EQ1.csv | 3 | Unresolved | Accepted with unresolved service or station reference",
			"RouteChoiceFC_EQ1.csv | 4 | Rejected | Malformed passenger route-choice row"};
		ok &= expect(asked == 1 && rows.size() == fileRows.size() + 1
				&& rows[0].find(" (batch) | - | Rejected | Passenger, journey, or leg ID collides") != std::string::npos
				&& std::vector<std::string>(rows.begin() + 1, rows.end()) == fileRows,
			"the result table lists the rows of both files with their accepted, rejected and unresolved status, after the passenger that collides");
		ok &= expect(!panel.importResultTable()->isHidden(), "the result table appears with the first row");
		ok &= expect(items(panel.passengerList()) == std::vector<std::string>({"P1", "P2", "imp1", "imp_unresolved"}),
			"the accepted and the unresolved passengers are appended, and the one that collides is not");
		ok &= expect(document.model().importReport.size() > reportsBefore && document.isDirty()
				&& document.lastChangeLabel() == "Import passengers" && events.log == oneChange,
			"the import is recorded in the report and is one change");
		ok &= expect(events.status == std::vector<std::string>({"Passenger import: 2 accepted, 1 rejected; unresolved rows remain visible"}),
			"the status message counts the accepted and rejected passengers");
		ok &= expect(prompts.errors.empty(), "an import that read its files reports no error");

		// a rejected import
		const ScenePassenger lastBefore = document.model().passengers.back();
		const std::size_t passengersBefore = document.model().passengers.size();
		const quint64 revisionBefore = document.revision();
		events.log.clear();
		QTemporaryDir partial;
		ok &= expect(partial.isValid() && writePassengerFolder(partial, "", ""), "the partial fixture can be written");
		QFile::remove(QDir(partial.path()).filePath("Passengers/RouteChoiceFC_EQ1.csv"));
		chosenFolder = partial.path();
		panel.importButton()->click();
		ok &= expect(document.model().passengers.size() == passengersBefore && document.model().passengers.back().id == lastBefore.id
				&& document.revision() == revisionBefore && events.log.empty(),
			"an import that fails leaves the scene alone");
		ok &= expect(panel.importResultTable()->rowCount() == 1 && statusOf(0) == "Rejected" && prompts.errors.size() == 1
				&& prompts.errors[0].rfind("Passenger Import Diagnostics: ", 0) == 0
				&& events.status.back() == "Passenger import rejected; the scene was not changed",
			"an import that fails shows one rejected row and tells the user why");

		// no folder
		chosenFolder.clear();
		panel.importButton()->click();
		ok &= expect(asked == 3 && panel.importResultTable()->rowCount() == 1 && prompts.errors.size() == 1
				&& events.log.empty(),
			"an import without a folder does nothing");

		// the table is cleared with the scene
		panel.clearImportResult();
		ok &= expect(panel.importResultTable()->rowCount() == 0 && panel.importResultTable()->isHidden(),
			"clearing the import result empties and hides the table");
		chosenFolder = fixture.path();
		panel.importButton()->click();
		ok &= expect(panel.importResultTable()->rowCount() > 0, "the table fills again with the next import");
		panel.setDocument(nullptr);
		ok &= expect(panel.importResultTable()->rowCount() == 0 && panel.importResultTable()->isHidden(),
			"a panel without a scene shows no import result");

		// an import needs a document open for edits
		panel.setDocument(&document);
		const int askedBefore = asked;
		document.setEditable(false);
		panel.importButton()->click();
		ok &= expect(asked == askedBefore, "Import does not ask for a folder while the document is closed for edits");
	}

	if (!ok)
		return 1;
	std::cout << "all passenger panel tests passed\n";
	return 0;
}
