#include "editors/EntranceDelayPanel.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QStringList>

#include <iostream>
#include <string>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static SceneStop stop(const char* stationId, bool departs) {
	SceneStop result;
	result.stationId = stationId;
	result.hasPlannedDeparture = departs;
	return result;
}

static SceneEntranceDelay delay(const char* serviceId, int occurrence, const char* stationId, double seconds) {
	SceneEntranceDelay result;
	result.serviceId = serviceId;
	result.occurrence = occurrence;
	result.stationId = stationId;
	result.delaySeconds = seconds;
	return result;
}

// A case of one hour with three services. Service s1 has the service code 100 and runs every 20 minutes, so it has three
// occurrences. It calls at A and D, where it plans to depart, and at C, where it does not; it calls at C again with a
// departure, but a station counts by its first call. Service s2 runs once and calls at X. Service s3 has no stops. The
// scenario base holds two delays, and the scenario alt none.
static SceneModel delayScene() {
	SceneModel scene;
	scene.name = "Delays";
	scene.settings.hasDuration = true;
	scene.settings.durationSeconds = 3600.0;
	SceneService first;
	first.id = "s1";
	first.operatingCode = "100";
	first.route = "r1";
	first.hasRepeat = true;
	first.headwaySeconds = 1200.0;
	first.stops = {stop("A", true), stop("C", false), stop("D", true), stop("C", true)};
	SceneService second;
	second.id = "s2";
	second.route = "r1";
	second.stops = {stop("X", true)};
	SceneService third;
	third.id = "s3";
	third.route = "r1";
	scene.services = {first, second, third};
	SceneScenario base;
	base.id = "base";
	base.name = "Baseline";
	base.entranceDelays = {delay("s1", 2, "A", 30.0), delay("s2", 1, "X", 0.0)};
	SceneScenario alt;
	alt.id = "alt";
	alt.name = "Alternative";
	scene.scenarios = {base, alt};
	scene.defaultScenarioId = "base";
	return scene;
}

// Answers the questions of the panel and records the messages, without a dialog.
class FakePrompts : public UserPrompts {
public:
	int errorCount = 0;
	int confirmCount = 0;
	QString confirmQuestion;
	bool confirmAnswer = false;

	void showError(QWidget*, const QString&, const QString&, bool = false) override { ++errorCount; }
	bool confirm(QWidget*, const QString&, const QString& question) override {
		++confirmCount;
		confirmQuestion = question;
		return confirmAnswer;
	}
};

using Log = std::vector<std::string>;

// The order in which a panel and its document report an edit.
struct Events {
	Log log;
	SceneDocument::ChangeKinds kinds;

	Events(EntranceDelayPanel& panel, SceneDocument& document) {
		QObject::connect(&document, &SceneDocument::changed, &panel, [this](SceneDocument::ChangeKinds changedKinds) {
			log.push_back("documentChanged");
			kinds = changedKinds;
		});
		QObject::connect(&panel, &EntranceDelayPanel::changed, &panel, [this] { log.push_back("panelChanged"); });
	}
};

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

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;
	const Log edit({"documentChanged", "panelChanged"});

	{
		EntranceDelayPanel panel;
		ok &= expect(panel.listWidget()->count() == 0 && !panel.listWidget()->isEnabled() && !panel.addButton()->isEnabled()
				&& !panel.serviceCombo()->isEnabled() && !panel.occurrenceEdit()->isEnabled() && !panel.stationCombo()->isEnabled()
				&& !panel.secondsEdit()->isEnabled() && panel.occurrenceContextLabel()->text().isEmpty()
				&& panel.emptyStateLabel()->isHidden(),
			"a panel without a document shows nothing and offers nothing, and does not point to the disabled Add button");
		SceneDocument document;
		panel.setDocument(&document);
		panel.addEntranceDelay();
		panel.duplicateEntranceDelay();
		panel.deleteEntranceDelay();
		panel.commitOccurrence();
		panel.commitSeconds();
		panel.commitPendingEditors();
		ok &= expect(!panel.addButton()->isEnabled() && document.revision() == 0 && !document.isDirty() && document.model().scenarios.empty(),
			"a panel stays disabled while its document holds no scene, and its actions change nothing");
	}

	{
		SceneDocument document;
		EntranceDelayPanel panel;
		FakePrompts prompts;
		RunSelection selection;
		panel.setPrompts(&prompts);
		panel.setRunSelection(&selection);
		panel.setDocument(&document);
		Events events(panel, document);

		// refresh
		document.replace(delayScene(), false);
		selection.selectScenario("base");
		panel.refresh();
		ok &= expect(items(panel.listWidget()) == QStringList({"s1 / #2 @ A: 30 s | code 100-2", "s2 / #1 @ X: 0 s | code s2"})
				&& panel.listWidget()->currentRow() == 0 && panel.listWidget()->isEnabled() && panel.addButton()->isEnabled()
				&& panel.duplicateButton()->isEnabled() && panel.deleteButton()->isEnabled(),
			"the delays of the selected scenario are listed with the service code, and the first is selected");
		ok &= expect(events.log.empty() && !document.isDirty() && document.revision() == 1 && !selection.isScenarioModified("base"),
			"showing a scene edits nothing");

		// the fields
		ok &= expect(choices(panel.serviceCombo())
					== QStringList({"100-1 | route r1 | entry +0 s", "s2 | route r1 | entry +0 s", "s3 | route r1 | entry +0 s"})
				&& panel.serviceCombo()->currentData().toString() == "s1" && panel.serviceCombo()->isEnabled(),
			"the services are offered by their first occurrence, and the service of the delay is chosen");
		ok &= expect(panel.occurrenceEdit()->minimum() == 1 && panel.occurrenceEdit()->maximum() == 3 && panel.occurrenceEdit()->value() == 2
				&& panel.occurrenceEdit()->isEnabled(),
			"the occurrences of the service limit the occurrence");
		ok &= expect(panel.occurrenceContextLabel()->text() == "Valid occurrence range: 1..3 | 100-2 | route r1 | entry +1200 s",
			"the context names the range of occurrences and the generated service");
		ok &= expect(choices(panel.stationCombo()) == QStringList({"A", "D"}) && panel.stationCombo()->currentText() == "A"
				&& panel.stationCombo()->isEnabled(),
			"the stations are the first calls of the service with a planned departure");
		ok &= expect(panel.secondsEdit()->value() == 30.0 && panel.secondsEdit()->isEnabled(), "the delay is shown");

		// another delay
		panel.listWidget()->setCurrentRow(1);
		ok &= expect(panel.serviceCombo()->currentData().toString() == "s2" && panel.occurrenceEdit()->maximum() == 1
				&& panel.occurrenceContextLabel()->text() == "Valid occurrence range: 1..1 | s2 | route r1 | entry +0 s"
				&& choices(panel.stationCombo()) == QStringList({"X"}) && panel.secondsEdit()->value() == 0.0 && events.log.empty(),
			"another delay shows its own fields, and selecting it edits nothing");
		panel.listWidget()->setCurrentRow(0);

		// edits
		panel.stationCombo()->setCurrentIndex(1);
		ok &= expect(document.model().scenarios[0].entranceDelays[0].stationId == "D" && document.isDirty() && selection.isScenarioModified("base")
				&& events.log == edit && events.kinds == SceneDocument::Scenarios && document.lastChangeLabel() == "Change entrance delay station"
				&& panel.stationCombo()->currentText() == "D",
			"a chosen station is written to the document, and the scenario counts as edited");
		events.log.clear();
		panel.stationCombo()->setCurrentIndex(1);
		ok &= expect(events.log.empty(), "choosing the station that is chosen does not touch the document");
		panel.occurrenceEdit()->setValue(3);
		panel.commitOccurrence();
		ok &= expect(document.model().scenarios[0].entranceDelays[0].occurrence == 3 && events.log == edit
				&& panel.occurrenceContextLabel()->text().endsWith("100-3 | route r1 | entry +2400 s")
				&& items(panel.listWidget())[0] == "s1 / #3 @ D: 30 s | code 100-3",
			"an occurrence is written to the document, and the context and the list show it");
		events.log.clear();
		panel.commitOccurrence();
		ok &= expect(events.log.empty(), "an unchanged occurrence does not touch the document");
		panel.secondsEdit()->setValue(-12.5);
		panel.commitSeconds();
		ok &= expect(document.model().scenarios[0].entranceDelays[0].delaySeconds == -12.5 && events.log == edit
				&& items(panel.listWidget())[0] == "s1 / #3 @ D: -12.5 s | code 100-3",
			"a delay that is negative is written to the document, and the list shows it");
		events.log.clear();
		panel.commitSeconds();
		ok &= expect(events.log.empty(), "an unchanged delay does not touch the document");

		// another service starts at its first occurrence and its first station
		panel.serviceCombo()->setCurrentIndex(panel.serviceCombo()->findData("s2"));
		const SceneEntranceDelay& changed = document.model().scenarios[0].entranceDelays[0];
		ok &= expect(changed.serviceId == "s2" && changed.occurrence == 1 && changed.stationId == "X" && changed.delaySeconds == -12.5
				&& choices(panel.stationCombo()) == QStringList({"X"}) && panel.occurrenceEdit()->maximum() == 1
				&& document.lastChangeLabel() == "Change entrance delay service",
			"another service starts at its first occurrence and at a station where it plans to depart");
		panel.serviceCombo()->setCurrentIndex(panel.serviceCombo()->findData("s3"));
		ok &= expect(changed.serviceId == "s3" && changed.stationId.empty() && panel.stationCombo()->currentText() == "Invalid: (empty)"
				&& choices(panel.stationCombo()) == QStringList({"Invalid: (empty)"})
				&& panel.occurrenceContextLabel()->text() == "Valid occurrence range: 1..1 | s3 | route r1 | entry +0 s",
			"a service without a call where it departs has no station to choose");
		panel.serviceCombo()->setCurrentIndex(panel.serviceCombo()->findData("s1"));
		ok &= expect(changed.serviceId == "s1" && changed.stationId == "A" && changed.occurrence == 1, "the first station is chosen again");

		// a service that is not in the scene
		document.mutableModel().scenarios[0].entranceDelays[1].serviceId = "gone";
		document.mutableModel().scenarios[0].entranceDelays[1].stationId.clear();
		panel.refresh();
		panel.listWidget()->setCurrentRow(1);
		ok &= expect(panel.serviceCombo()->currentText() == "Invalid: gone" && panel.stationCombo()->currentText() == "Invalid: (empty)"
				&& panel.occurrenceContextLabel()->text() == "Valid occurrence range: 1..1 | Generated operating code: (unavailable)"
				&& items(panel.listWidget())[1] == "gone / #1 @ (empty): 0 s",
			"a delay of a service that is gone is shown as invalid, without a code");
		document.mutableModel().scenarios[0].entranceDelays[1] = delay("s2", 1, "X", 0.0);
		panel.refresh();

		// add, duplicate and delete
		document.markSaved();
		selection.clearModifiedScenarios();
		events.log.clear();
		panel.addEntranceDelay();
		const SceneEntranceDelay& added = document.model().scenarios[0].entranceDelays.back();
		ok &= expect(document.model().scenarios[0].entranceDelays.size() == 3 && added.serviceId == "s1" && added.stationId == "A"
				&& added.occurrence == 1 && added.delaySeconds == 0.0 && panel.listWidget()->count() == 3 && panel.listWidget()->currentRow() == 2
				&& document.isDirty() && selection.isScenarioModified("base") && events.log == edit && document.lastChangeLabel() == "Add entrance delay",
			"a delay is added for the first service that departs from a station, selected and shown");
		events.log.clear();
		panel.listWidget()->setCurrentRow(0);
		panel.duplicateEntranceDelay();
		ok &= expect(document.model().scenarios[0].entranceDelays.size() == 4 && document.model().scenarios[0].entranceDelays[1].serviceId == "s1"
				&& document.model().scenarios[0].entranceDelays[1].delaySeconds == -12.5 && panel.listWidget()->currentRow() == 1 && events.log == edit
				&& document.lastChangeLabel() == "Duplicate entrance delay",
			"a copy is put after the delay and selected");
		prompts.confirmCount = 0;
		events.log.clear();
		panel.deleteEntranceDelay();
		ok &= expect(prompts.confirmCount == 1 && prompts.confirmQuestion == "Delete entrance delay for service 's1'?"
				&& document.model().scenarios[0].entranceDelays.size() == 4 && events.log.empty(),
			"declining the question keeps the delay");
		prompts.confirmAnswer = true;
		panel.deleteEntranceDelay();
		ok &= expect(document.model().scenarios[0].entranceDelays.size() == 3 && panel.listWidget()->count() == 3 && events.log == edit
				&& document.lastChangeLabel() == "Delete entrance delay",
			"a deleted delay is removed");

		// a scene without stops puts the first service in the new delay
		SceneModel noStops = delayScene();
		for (SceneService& service : noStops.services)
			service.stops.clear();
		document.replace(noStops, false);
		panel.addEntranceDelay();
		ok &= expect(document.model().scenarios[0].entranceDelays.back().serviceId == "s1" && document.model().scenarios[0].entranceDelays.back().stationId.empty(),
			"a new delay of a scene without calls names the first service and no station");
		document.replace(delayScene(), false);
		selection.selectScenario("base");
		selection.clearModifiedScenarios();
		panel.refresh();

		// the other scenario
		selection.selectScenario("alt");
		panel.refresh();
		ok &= expect(panel.listWidget()->count() == 0 && panel.addButton()->isEnabled() && !panel.duplicateButton()->isEnabled()
				&& !panel.deleteButton()->isEnabled() && panel.occurrenceContextLabel()->text().isEmpty()
				&& panel.emptyStateLabel()->text() == "This scenario has no entrance delays. Use Add."
				&& !panel.emptyStateLabel()->isHidden() && panel.emptyStateLabel()->property("dialogHelp").toBool()
				&& !panel.serviceCombo()->isEnabled(),
			"a scenario without delays shows an empty list and says so, and a delay can be added");
		panel.addEntranceDelay();
		ok &= expect(document.model().scenarios[1].entranceDelays.size() == 1 && document.model().scenarios[0].entranceDelays.size() == 2
				&& selection.isScenarioModified("alt") && !selection.isScenarioModified("base"),
			"a delay is added to the selected scenario");
		ok &= expect(panel.emptyStateLabel()->isHidden(), "the selected delay has no empty-state message");
		panel.listWidget()->setCurrentRow(-1);
		ok &= expect(panel.emptyStateLabel()->text() == "Select an entrance delay or use Add." && !panel.emptyStateLabel()->isHidden(),
			"a scenario with delays and no selected row asks for a selection");
		panel.listWidget()->setCurrentRow(0);
		selection.selectScenario("base");
		panel.refresh();

		// pending edits belong to the field that has the focus
		document.markSaved();
		events.log.clear();
		panel.show();
		panel.activateWindow();
		panel.secondsEdit()->setValue(77.0);
		panel.commitPendingEditors();
		ok &= expect(document.model().scenarios[0].entranceDelays[0].delaySeconds == 30.0 && events.log.empty(),
			"a value in a field that does not have the focus is not pending");
		panel.secondsEdit()->setFocus();
		QApplication::processEvents();
		panel.commitPendingEditors();
		ok &= expect(document.model().scenarios[0].entranceDelays[0].delaySeconds == 77.0 && events.log == edit,
			"a value in the field that has the focus is taken into the document");
		panel.hide();
		panel.refresh();

		// the choices follow the document
		events.log.clear();
		document.mutableModel().services[0].stops[2].stationId = "E";
		ok &= expect(choices(panel.stationCombo()) == QStringList({"A", "D"}), "a station that the document has not announced is not offered");
		document.markChanged(SceneDocument::Services, "Edit timetable stop");
		ok &= expect(choices(panel.stationCombo()) == QStringList({"A", "E"}) && panel.listWidget()->currentRow() == 0,
			"the stations follow the timetable of the service, and the selection stays");
		document.mutableModel().settings.durationSeconds = 7200.0;
		document.markChanged(SceneDocument::CaseSettings, "Edit case settings");
		ok &= expect(panel.occurrenceEdit()->maximum() == 6 && panel.occurrenceContextLabel()->text().startsWith("Valid occurrence range: 1..6"),
			"a longer case gives the service more occurrences");
		panel.setOccurrenceDuration([] { return 2400.0; });
		document.markChanged(SceneDocument::CaseSettings, "Edit case settings");
		ok &= expect(panel.occurrenceEdit()->maximum() == 2, "the duration that the window gives counts the occurrences");
		panel.setOccurrenceDuration({});
		document.mutableModel().services[0].operatingCode = "200";
		document.markChanged(SceneDocument::Services, "Change service code");
		ok &= expect(items(panel.listWidget())[0].endsWith("code 200-2"), "the list follows the service code");
		document.mutableModel().services[0].operatingCode = "300";
		document.markChanged(SceneDocument::Scenarios, "Edit scenario");
		ok &= expect(items(panel.listWidget())[0].endsWith("code 200-2"), "the delays stay as they are when only scenarios change");
		document.mutableModel().services[0].operatingCode = "100";
		document.mutableModel().settings.durationSeconds = 3600.0;
		document.markChanged(SceneDocument::Services | SceneDocument::CaseSettings, "Edit");

		// the scene is in use by a run
		document.markSaved();
		events.log.clear();
		document.setEditable(false);
		panel.refresh();
		ok &= expect(!panel.listWidget()->isEnabled() && !panel.addButton()->isEnabled() && !panel.duplicateButton()->isEnabled()
				&& !panel.deleteButton()->isEnabled() && !panel.serviceCombo()->isEnabled() && !panel.occurrenceEdit()->isEnabled()
				&& !panel.stationCombo()->isEnabled() && !panel.secondsEdit()->isEnabled(),
			"nothing can be done with the delays while a run uses the scene");
		panel.secondsEdit()->setValue(5.0);
		panel.occurrenceEdit()->setValue(2);
		panel.commitPendingEditors();
		panel.commitSeconds();
		panel.commitOccurrence();
		panel.commitStation(0);
		panel.commitService(0);
		panel.addEntranceDelay();
		panel.duplicateEntranceDelay();
		panel.deleteEntranceDelay();
		ok &= expect(document.model().scenarios[0].entranceDelays[0].delaySeconds == 77.0 && document.model().scenarios[0].entranceDelays.size() == 2
				&& events.log.empty() && !document.isDirty(),
			"actions change nothing while a run uses the scene");
		document.setEditable(true);
		panel.refresh();

		// another scene
		SceneModel other = delayScene();
		other.scenarios[0].entranceDelays.resize(1);
		other.scenarios[0].entranceDelays[0].delaySeconds = 4.0;
		document.replace(other, false);
		ok &= expect(items(panel.listWidget()) == QStringList({"s1 / #2 @ A: 4 s | code 100-2"}) && !document.isDirty(),
			"opening another scene shows its delays without an edit");

		// detach
		panel.setDocument(nullptr);
		ok &= expect(panel.listWidget()->count() == 0 && !panel.listWidget()->isEnabled() && panel.serviceCombo()->count() == 0,
			"a detached panel shows nothing");
		const quint64 revisionDetached = document.revision();
		panel.addEntranceDelay();
		document.replace(delayScene(), false);
		ok &= expect(panel.listWidget()->count() == 0 && document.revision() == revisionDetached + 1,
			"a detached panel neither edits nor follows the document");
	}

	{
		auto* document = new SceneDocument;
		EntranceDelayPanel panel;
		panel.setDocument(document);
		document->replace(delayScene(), false);
		delete document;
		panel.commitSeconds();
		panel.addEntranceDelay();
		panel.refresh();
		ok &= expect(panel.listWidget()->count() == 0 && !panel.addButton()->isEnabled(),
			"a panel whose document is gone shows nothing and edits nothing");
	}

	{
		// long names of services and stations do not widen the panel
		SceneModel scene = delayScene();
		const std::string longService(150, 's');
		const std::string longStation(150, 'a');
		scene.services[0].id = longService;
		scene.services[0].stops[0].stationId = longStation;
		scene.scenarios[0].entranceDelays[0] = delay(longService.c_str(), 2, longStation.c_str(), 30.0);
		SceneDocument document;
		EntranceDelayPanel panel;
		panel.setDocument(&document);
		document.replace(scene, false);
		panel.listWidget()->setCurrentRow(0);
		ok &= expect(panel.serviceCombo()->currentData().toString() == QString::fromStdString(longService)
				&& panel.stationCombo()->currentText() == QString::fromStdString(longStation),
			"the fields show the long names");
		ok &= expect(panel.minimumSizeHint().width() < 400, "long names of services and stations do not widen the panel");
	}

	if (!ok)
		return 1;
	std::cout << "all entrance delay panel tests passed\n";
	return 0;
}
