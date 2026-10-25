#include "editors/CaseSettingsPanel.h"
#include "editors/EditorHelpers.h"
#include "editors/ServicePanel.h"
#include "editors/TimetableStopsPanel.h"

#include "scene/SceneWriter.h"

#include "widgets/ChoiceComboBox.h"
#include "widgets/ColorChoiceButton.h"
#include "widgets/ElidedLabel.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolTip>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static SceneService service(const char* id, const char* compositionId, const char* routeId) {
	SceneService result;
	result.id = id;
	result.composition = compositionId;
	result.route = routeId;
	return result;
}

// Two compositions, two routes and three services over a case of one hour. Service s1 is used by an incident, an
// entrance delay and a passenger leg; s2 runs every 20 minutes; s3 is not used by anything.
static SceneModel serviceScene() {
	SceneModel scene;
	scene.name = "Services";
	scene.settings.hasDuration = true;
	scene.settings.durationSeconds = 3600.0;
	SceneComposition first;
	first.id = "c1";
	SceneComposition second;
	second.id = "c2";
	scene.compositions = {first, second};
	SceneRoute firstRoute;
	firstRoute.id = "r1";
	SceneRoute secondRoute;
	secondRoute.id = "r2";
	scene.routes = {firstRoute, secondRoute};
	scene.services = {service("s1", "c1", "r1"), service("s2", "c1", "r2"), service("s3", "c2", "r1")};
	scene.services[0].operatingCode = "100";
	scene.services[1].hasRepeat = true;
	scene.services[1].headwaySeconds = 1200.0;
	SceneScenario scenario;
	scenario.id = "base";
	SceneIncident breakdown;
	breakdown.id = "i1";
	breakdown.type = "train_breakdown";
	breakdown.target = "s1";
	scenario.incidents.push_back(breakdown);
	SceneIncident signalFailure;
	signalFailure.id = "i2";
	signalFailure.type = "signal_failure";
	signalFailure.target = "s1";
	scenario.incidents.push_back(signalFailure);
	SceneEntranceDelay delay;
	delay.serviceId = "s1";
	delay.stationId = "st1";
	scenario.entranceDelays.push_back(delay);
	scene.scenarios = {scenario};
	scene.defaultScenarioId = "base";
	ScenePassengerLeg leg;
	leg.id = "leg1";
	leg.serviceId = "s1";
	ScenePassengerJourney journey;
	journey.id = "j1";
	journey.legs.push_back(leg);
	ScenePassenger passenger;
	passenger.id = "p1";
	passenger.journeys.push_back(journey);
	scene.passengers = {passenger};
	return scene;
}

// Answers the questions of the panel and records the messages, without a dialog.
class FakePrompts : public UserPrompts {
public:
	QString errorTitle;
	QString errorMessage;
	bool errorWarning = false;
	int errorCount = 0;
	int confirmCount = 0;
	QString confirmQuestion;
	bool confirmAnswer = false;

	void showError(QWidget*, const QString& title, const QString& message, bool warningIcon = false) override {
		++errorCount;
		errorTitle = title;
		errorMessage = message;
		errorWarning = warningIcon;
	}
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

	Events(ServicePanel& panel, SceneDocument& document) {
		QObject::connect(&document, &SceneDocument::changed, &panel, [this](SceneDocument::ChangeKinds changedKinds) {
			log.push_back("documentChanged");
			kinds = changedKinds;
		});
		QObject::connect(&panel, &ServicePanel::occurrencesChanged, &panel, [this] { log.push_back("occurrences"); });
		QObject::connect(&panel, &ServicePanel::selectedServiceChanged, &panel,
			[this](const QString& id) { log.push_back("selected:" + id.toStdString()); });
		QObject::connect(&panel, &ServicePanel::serviceRenamed, &panel, [this](const QString& oldId, const QString& newId) {
			log.push_back("renamed:" + oldId.toStdString() + ">" + newId.toStdString());
		});
		QObject::connect(&panel, &ServicePanel::changed, &panel, [this] { log.push_back("panelChanged"); });
	}
};

static QStringList items(const QListWidget* list) {
	QStringList result;
	for (int row = 0; row < list->count(); ++row)
		result << list->item(row)->text();
	return result;
}

// The text of the tip that the combo shows when the pointer rests on it.
static QString shownTip(QComboBox* combo) {
	QHelpEvent help(QEvent::ToolTip, QPoint(4, 4), combo->mapToGlobal(QPoint(4, 4)));
	QApplication::sendEvent(combo, &help);
	const QString text = QToolTip::text();
	QToolTip::hideText();
	return text;
}

static bool checked(const QTableWidget* table, int row) {
	return table->item(row, 0)->checkState() == Qt::Checked;
}

// The text field of a number field.
static QLineEdit* spinText(QDoubleSpinBox* edit) {
	return edit->findChild<QLineEdit*>();
}

// What the user does to a number field: type text, then leave the field.
static void typeNumber(QDoubleSpinBox* edit, const QString& text) {
	spinText(edit)->setText(text);
	edit->interpretText();
}

// Gives a field of a shown panel the focus, as a click does.
static bool giveFocus(QWidget* edit) {
	edit->setFocus();
	QApplication::processEvents();
	if (!edit->hasFocus())
		std::cerr << "failed: " << edit->objectName().toStdString() << " did not get the focus\n";
	return edit->hasFocus();
}

// Save, Undo and validation commit the field that is being edited. A stored number that a field rounds or shows at a
// limit of its range reaches the saved file as it is while the user does not edit it, even when the field has the focus,
// and a number that the user types is stored as typed.
static bool keepsUntouchedNumbers() {
	bool ok = true;
	SceneModel scene = serviceScene();
	SceneService& stored = scene.services[0];
	stored.hasEntryTime = true;
	stored.entryTimeSeconds = 600.5;
	stored.hasRepeat = true;
	stored.headwaySeconds = 0.5;
	stored.performancePercent = 150.0;
	stored.hasMaximumSpeed = true;
	stored.maximumSpeedKmh = 1200.0;
	SceneDocument document;
	ServicePanel panel;
	panel.setDocument(&document);
	document.replace(scene, false);
	Events events(panel, document);
	ok &= expect(panel.entryTimeSecondsEdit()->value() == 600.5 && panel.entryTimeSecondsEdit()->text() == "600.5"
			&& panel.headwaySecondsEdit()->value() == 0.5 && panel.headwaySecondsEdit()->text() == "0.5",
		"an entry time and a headway are shown with their fractions");

	const nlohmann::json saved = encodeServices(document.model());
	const quint64 revision = document.revision();
	panel.commitPendingEditors();
	panel.show();
	panel.activateWindow();
	for (QDoubleSpinBox* field : {panel.entryTimeSecondsEdit(), panel.headwaySecondsEdit(), panel.performancePercentEdit(),
			 panel.maximumSpeedKmhEdit()}) {
		ok &= giveFocus(field);
		panel.commitPendingEditors();
	}
	ok &= giveFocus(panel.idEdit());
	panel.commitEntryTimeSeconds();
	panel.commitHeadwaySeconds();
	panel.commitPerformancePercent(panel.performancePercentEdit()->value());
	panel.commitMaximumSpeed();
	const SceneService& kept = document.model().services[0];
	ok &= expect(kept.entryTimeSeconds == 600.5 && kept.headwaySeconds == 0.5 && kept.performancePercent == 150.0
			&& kept.maximumSpeedKmh == 1200.0 && encodeServices(document.model()) == saved,
		"an entry time of 600.5 s, a headway of 0.5 s, a performance of 150 and a maximum speed of 1200 km/h are saved as stored");
	ok &= expect(events.log.empty() && document.revision() == revision && !document.isDirty(),
		"visiting the number fields and committing them adds no Undo step");

	ok &= giveFocus(panel.entryTimeSecondsEdit());
	spinText(panel.entryTimeSecondsEdit())->setText("610.25");
	panel.commitPendingEditors();
	ok &= expect(kept.entryTimeSeconds == 610.25 && document.revision() == revision + 1
			&& document.lastChangeLabel() == "Change service entry time",
		"an entry time that is being typed is stored with its fraction");
	ok &= giveFocus(panel.headwaySecondsEdit());
	typeNumber(panel.headwaySecondsEdit(), "0.25");
	panel.commitPendingEditors();
	ok &= expect(kept.headwaySeconds == 0.25 && kept.entryTimeSeconds == 610.25 && kept.performancePercent == 150.0
			&& kept.maximumSpeedKmh == 1200.0 && document.revision() == revision + 2,
		"a headway that is typed is stored with its fraction, and the other fields keep their values");
	panel.hide();
	return ok;
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;
	ok &= keepsUntouchedNumbers();
	// An edit that lists the occurrences again, and one that does not.
	const Log editWithOccurrences({"documentChanged", "occurrences", "panelChanged"});
	const Log editOnly({"documentChanged", "panelChanged"});

	{
		ServicePanel panel;
		ok &= expect(panel.listWidget()->count() == 0 && !panel.listWidget()->isEnabled() && !panel.addButton()->isEnabled()
				&& !panel.idEdit()->isEnabled() && panel.occurrenceTable()->rowCount() == 0
				&& panel.occurrenceSelectionLabel()->text().isEmpty() && !panel.selectAllButton()->isEnabled(),
			"a panel without a document shows nothing and offers nothing");
		SceneDocument document;
		panel.setDocument(&document);
		panel.addService();
		panel.duplicateService();
		panel.deleteService();
		panel.commitIdEdit();
		panel.commitPendingEditors();
		panel.selectNoneOccurrences();
		ok &= expect(!panel.addButton()->isEnabled() && document.revision() == 0 && !document.isDirty()
				&& document.model().services.empty(),
			"a panel stays disabled while its document holds no scene, and its actions change nothing");
	}

	{
		// the message above the fields says why no service is shown
		SceneDocument document;
		ServicePanel panel;
		ok &= expect(panel.emptyStateLabel()->text() == "Open a case to edit services." && panel.emptyStateLabel()->isVisibleTo(&panel),
			"a panel without a document asks to open a case");
		panel.setDocument(&document);
		ok &= expect(panel.emptyStateLabel()->text() == "Open a case to edit services.", "a document without a scene asks to open a case");
		SceneModel scene = serviceScene();
		scene.services.clear();
		document.replace(scene, false);
		ok &= expect(panel.emptyStateLabel()->text() == "Select a service or use Add Service." && panel.emptyStateLabel()->isVisibleTo(&panel),
			"a scene without services points to Add Service");
		document.setEditable(false);
		panel.refresh();
		ok &= expect(panel.emptyStateLabel()->text() == "No services.", "a scene without services does not point to Add Service while a run uses it");
		document.setEditable(true);
		document.replace(serviceScene(), false);
		ok &= expect(panel.emptyStateLabel()->isHidden(), "the message is hidden while a service is shown");
	}

	{
		SceneDocument document;
		ServicePanel panel;
		FakePrompts prompts;
		RunSelection selection;
		panel.setPrompts(&prompts);
		panel.setRunSelection(&selection);
		panel.setDocument(&document);
		Events events(panel, document);

		// refresh
		document.replace(serviceScene(), false);
		ok &= expect(items(panel.listWidget()) == QStringList({"s1", "s2", "s3"}) && panel.listWidget()->currentRow() == 0
				&& panel.listWidget()->isEnabled() && panel.addButton()->isEnabled() && panel.duplicateButton()->isEnabled()
				&& panel.deleteButton()->isEnabled(),
			"a new scene is listed and its first service is selected");
		ok &= expect(panel.idEdit()->text() == "s1" && panel.idEdit()->isEnabled() && panel.operatingCodeEdit()->text() == "100"
				&& panel.compositionCombo()->currentText() == "c1" && panel.compositionCombo()->count() == 2
				&& panel.routeCombo()->currentData().toString() == "r1" && panel.routeCombo()->count() == 2
				&& panel.categoryCombo()->currentIndex() == 0 && panel.categoryCombo()->count() == 7,
			"the selected service shows its id, code, composition and route");
		ok &= expect(panel.idEdit()->cursorPosition() == 0 && panel.operatingCodeEdit()->cursorPosition() == 0,
			"the id and code fields show the start of their text");
		ok &= expect(!panel.hasEntryTimeCheck()->isChecked() && !panel.entryTimeSecondsEdit()->isEnabled()
				&& !panel.hasRepeatCheck()->isChecked() && !panel.headwaySecondsEdit()->isEnabled()
				&& !panel.hasRepeatCountCheck()->isEnabled() && !panel.repeatCountEdit()->isEnabled()
				&& !panel.hasOperatingCodeStepCheck()->isEnabled() && !panel.hasMaximumSpeedCheck()->isChecked()
				&& !panel.maximumSpeedKmhEdit()->isEnabled() && panel.performancePercentEdit()->value() == 100.0,
			"the optional fields wait for the box that switches them on");
		ok &= expect(events.log == Log({"selected:s1", "occurrences"}) && !document.isDirty() && document.revision() == 1,
			"showing a scene edits nothing, and tells the timetable and the lists of occurrences");

		// selection
		events.log.clear();
		panel.listWidget()->setCurrentRow(1);
		ok &= expect(panel.idEdit()->text() == "s2" && panel.hasRepeatCheck()->isChecked() && panel.headwaySecondsEdit()->isEnabled()
				&& panel.headwaySecondsEdit()->value() == 1200.0 && panel.routeCombo()->currentData().toString() == "r2"
				&& events.log == Log({"selected:s2"}),
			"another selected service shows its own fields and tells the timetable");

		// occurrences: one for s1, three for s2 and one for s3
		ok &= expect(panel.occurrenceTable()->rowCount() == 5 && panel.occurrenceTable()->isEnabled()
				&& panel.occurrenceTable()->item(1, 2)->text() == "r2" && panel.occurrenceTable()->item(2, 3)->text() == "+1200 s"
				&& panel.occurrenceTable()->item(0, 1)->text() == "100" && panel.occurrenceTable()->item(1, 1)->text() == "s2-1"
				&& panel.occurrenceTable()->item(1, 0)->data(Qt::UserRole).toString() == "s2"
				&& panel.occurrenceTable()->item(1, 0)->data(Qt::UserRole + 1).toInt() == 1,
			"the occurrence table lists every occurrence of every service");
		ok &= expect(panel.occurrenceSelectionLabel()->text() == "5 of 5 selected"
				&& panel.occurrenceSelectionLabel()->toolTip() == "An occurrence is in the run period when it enters from 0 up to 3600 s.",
			"the label counts the occurrences, and its tooltip gives the counting rule");
		ok &= expect(ServicePanel::occurrenceSummaryText(4, 3, 3, 4) == "3 of 4 selected"
				&& ServicePanel::occurrenceSummaryText(4, 3, 2, 4) == "3 of 4 selected, 2 in the run period"
				&& ServicePanel::occurrenceSummaryText(900, 900, 900, 700)
					== "900 of 900 selected\nShowing the first 700. All of them run. Lower the repeat count to choose which.",
			"the summary names the occurrences in the run period only when they differ from the selected ones");
		ok &= expect(panel.occurrenceTable()->item(0, 3)->toolTip() == "100 (route r1, +0 s)\nService s1, run 1",
			"a row names the generated service in its tooltip");
		ok &= expect(generatedServiceLabel(document.model().services[1], 2) == "s2-2 (route r2, +1200 s)",
			"a generated service is named by its code, route and scheduled entry");

		// the preview: leave an occurrence out
		events.log.clear();
		panel.occurrenceTable()->item(2, 0)->setCheckState(Qt::Unchecked);
		ok &= expect(selection.isExcluded(SceneServiceOccurrence{"s2", 2}) && selection.excluded().size() == 1
				&& !checked(panel.occurrenceTable(), 2) && checked(panel.occurrenceTable(), 1)
				&& panel.occurrenceSelectionLabel()->text() == "4 of 5 selected" && events.log == Log({"occurrences"})
				&& !document.isDirty() && document.revision() == 1,
			"leaving an occurrence out is kept in the run selection and does not change the scene");
		events.log.clear();
		panel.selectNoneOccurrences();
		ok &= expect(selection.excluded().size() == 5 && !checked(panel.occurrenceTable(), 0)
				&& panel.occurrenceSelectionLabel()->text() == "0 of 5 selected" && !document.isDirty(),
			"Select none leaves every occurrence out");
		panel.selectAllOccurrences();
		ok &= expect(selection.excluded().empty() && checked(panel.occurrenceTable(), 4) && !document.isDirty(),
			"Select all includes every occurrence again");
		selection.setExcluded(SceneServiceOccurrence{"s2", 2}, true);
		selection.setExcluded(SceneServiceOccurrence{"s2", 3}, true);
		selection.setExcluded(SceneServiceOccurrence{"gone", 1}, true);
		panel.setOccurrenceDuration([] { return 2400.0; });
		panel.refreshOccurrencePreview();
		ok &= expect(panel.occurrenceTable()->rowCount() == 4 && selection.excluded().size() == 1
				&& selection.isExcluded(SceneServiceOccurrence{"s2", 2}) && !checked(panel.occurrenceTable(), 2),
			"the duration of the window counts the occurrences, and what is left out beyond them is forgotten");
		panel.setOccurrenceDuration({});

		// a service with more occurrences than a run expands
		panel.listWidget()->setCurrentRow(2);
		panel.hasRepeatCheck()->setChecked(true);
		panel.hasRepeatCountCheck()->setChecked(true);
		panel.repeatCountEdit()->setText("800");
		panel.commitRepeatCount();
		ok &= expect(panel.occurrenceTable()->rowCount() == 700 && !panel.occurrenceTable()->isEnabled()
				&& !panel.selectAllButton()->isEnabled() && !panel.selectNoneButton()->isEnabled()
				&& panel.occurrenceSelectionLabel()->text().contains("\nShowing the first 700. All of them run."),
			"a preview is limited to the trains a run expands, and then cannot be used to leave occurrences out");
		panel.selectNoneOccurrences();
		ok &= expect(selection.excluded().empty(), "Select none does nothing while the preview is limited");
		panel.repeatCountEdit()->setText("2");
		panel.commitRepeatCount();
		panel.hasRepeatCountCheck()->setChecked(false);
		panel.hasRepeatCheck()->setChecked(false);
		panel.listWidget()->setCurrentRow(0);

		// edits of the fields of a service
		events.log.clear();
		document.markSaved();
		panel.operatingCodeEdit()->setText("  200  ");
		panel.commitOperatingCode();
		ok &= expect(document.model().services[0].operatingCode == "200" && document.isDirty() && events.log == editWithOccurrences
				&& events.kinds == SceneDocument::Services && document.lastChangeLabel() == "Change service code"
				&& panel.occurrenceTable()->item(0, 1)->text() == "200",
			"a service code is written to the document, trimmed, and the occurrences are listed again");
		events.log.clear();
		panel.commitOperatingCode();
		ok &= expect(events.log.empty(), "an unchanged service code does not touch the document");

		panel.categoryCombo()->setCurrentIndex(panel.categoryCombo()->findData("Freight"));
		ok &= expect(document.model().services[0].category == "Freight" && events.log == editOnly,
			"a chosen category is written to the document");
		events.log.clear();
		panel.categoryCombo()->setCurrentIndex(0);
		ok &= expect(document.model().services[0].category.empty() && events.log == editOnly, "No category clears the category");

		events.log.clear();
		panel.colorButton()->chooseColor(QColor("#336699"));
		ok &= expect(document.model().services[0].visualizationColor == "#336699" && events.log == editOnly,
			"a chosen colour is written to the document");
		events.log.clear();
		panel.colorButton()->resetColor();
		ok &= expect(document.model().services[0].visualizationColor.empty() && events.log == editOnly,
			"the default colour clears the colour");

		events.log.clear();
		panel.compositionCombo()->setCurrentText("c2");
		ok &= expect(document.model().services[0].composition == "c2" && events.log == editOnly
				&& panel.compositionCombo()->currentText() == "c2",
			"a chosen composition is written to the document without listing the occurrences");

		events.log.clear();
		panel.routeCombo()->setCurrentIndex(panel.routeCombo()->findData("r2"));
		ok &= expect(document.model().services[0].route == "r2"
				&& events.log == Log({"documentChanged", "occurrences", "selected:s1", "panelChanged"})
				&& panel.occurrenceTable()->item(0, 2)->text() == "r2",
			"a chosen route is written to the document and tells the timetable that its route has changed");

		events.log.clear();
		panel.hasEntryTimeCheck()->setChecked(true);
		ok &= expect(document.model().services[0].hasEntryTime && panel.entryTimeSecondsEdit()->isEnabled()
				&& events.log == editWithOccurrences,
			"the entry time switches on");
		events.log.clear();
		typeNumber(panel.entryTimeSecondsEdit(), "90");
		panel.commitEntryTimeSeconds();
		ok &= expect(document.model().services[0].entryTimeSeconds == 90.0 && events.log == editWithOccurrences
				&& panel.occurrenceTable()->item(0, 3)->text() == "+90 s",
			"an entry time is written to the document and shown in the occurrences");
		events.log.clear();
		typeNumber(panel.entryTimeSecondsEdit(), "");
		panel.commitEntryTimeSeconds();
		ok &= expect(document.model().services[0].entryTimeSeconds == 90.0 && panel.entryTimeSecondsEdit()->text() == "90"
				&& events.log.empty(),
			"a blank entry time shows the stored one again");
		typeNumber(panel.entryTimeSecondsEdit(), "0");
		panel.commitEntryTimeSeconds();
		ok &= expect(document.model().services[0].entryTimeSeconds == 0.0, "an entry time can be zero");

		panel.hasRepeatCheck()->setChecked(true);
		panel.hasRepeatCountCheck()->setChecked(true);
		panel.hasOperatingCodeStepCheck()->setChecked(true);
		ok &= expect(document.model().services[0].hasRepeat && document.model().services[0].hasRepeatCount
				&& document.model().services[0].repeatCount == 1 && document.model().services[0].hasOperatingCodeStep
				&& document.model().services[0].operatingCodeStep == 1 && panel.repeatCountEdit()->text() == "1"
				&& panel.operatingCodeStepEdit()->text() == "1" && panel.repeatCountEdit()->isEnabled()
				&& panel.operatingCodeStepEdit()->isEnabled(),
			"the repeat count and the code step start at one");
		panel.repeatCountEdit()->setText("3");
		panel.commitRepeatCount();
		panel.operatingCodeEdit()->setText("200");
		panel.operatingCodeStepEdit()->setText("0");
		panel.commitOperatingCodeStep();
		ok &= expect(document.model().services[0].repeatCount == 3 && document.model().services[0].operatingCodeStep == 1
				&& panel.operatingCodeStepEdit()->text() == "1" && panel.occurrenceTable()->rowCount() == 7
				&& panel.occurrenceTable()->item(2, 1)->text() == "202",
			"the repeat count is written to the document and a code step of zero becomes one");
		panel.operatingCodeStepEdit()->setText("-2");
		panel.commitOperatingCodeStep();
		ok &= expect(document.model().services[0].operatingCodeStep == -2, "a negative code step is kept");
		typeNumber(panel.headwaySecondsEdit(), "600");
		panel.commitHeadwaySeconds();
		ok &= expect(document.model().services[0].headwaySeconds == 600.0, "a headway is written to the document");
		panel.hasRepeatCheck()->setChecked(false);
		ok &= expect(!document.model().services[0].hasRepeat && !document.model().services[0].hasRepeatCount
				&& !document.model().services[0].hasOperatingCodeStep && !panel.hasRepeatCountCheck()->isChecked()
				&& !panel.hasRepeatCountCheck()->isEnabled() && !panel.hasOperatingCodeStepCheck()->isChecked()
				&& !panel.repeatCountEdit()->isEnabled() && !panel.headwaySecondsEdit()->isEnabled(),
			"switching the repeat off switches the repeat count and the code step off");

		events.log.clear();
		panel.performancePercentEdit()->setValue(80.5);
		ok &= expect(document.model().services[0].performancePercent == 80.5 && events.log == editWithOccurrences
				&& panel.occurrenceTable()->item(0, 4)->text() == "80.5",
			"a performance is written to the document and shown in the occurrences");
		panel.hasMaximumSpeedCheck()->setChecked(true);
		ok &= expect(document.model().services[0].hasMaximumSpeed && document.model().services[0].maximumSpeedKmh == 100.0
				&& panel.maximumSpeedKmhEdit()->value() == 100.0 && panel.maximumSpeedKmhEdit()->isEnabled()
				&& panel.occurrenceTable()->item(0, 5)->text() == "100",
			"switching the maximum speed on starts at 100 km/h");
		panel.maximumSpeedKmhEdit()->setValue(87.25);
		panel.commitMaximumSpeed();
		ok &= expect(document.model().services[0].maximumSpeedKmh == 87.25 && panel.occurrenceTable()->item(0, 5)->text() == "87.25",
			"a maximum speed is written to the document");

		// pending edits: a number field commits the text in it only while it has the focus
		events.log.clear();
		panel.show();
		panel.activateWindow();
		panel.hasEntryTimeCheck()->setChecked(true);
		spinText(panel.entryTimeSecondsEdit())->setText("45");
		panel.operatingCodeEdit()->setText("300");
		ok &= expect(document.model().services[0].entryTimeSeconds == 0.0 && document.model().services[0].operatingCode == "200",
			"text that is still being typed is not in the document");
		panel.commitPendingEditors();
		ok &= expect(document.model().services[0].entryTimeSeconds == 0.0 && document.model().services[0].operatingCode == "300",
			"committing the pending editors takes the text of the code, and not a number in a field without the focus");
		ok &= giveFocus(panel.entryTimeSecondsEdit());
		panel.commitPendingEditors();
		ok &= expect(document.model().services[0].entryTimeSeconds == 45.0,
			"committing the pending editors takes the number that is being typed in the field with the focus");
		panel.hide();

		// cancel: text that was typed and not finished is dropped when the panel is refreshed
		events.log.clear();
		document.markSaved();
		const quint64 revisionBeforeCancel = document.revision();
		panel.operatingCodeEdit()->setText("abandoned");
		panel.refresh();
		ok &= expect(panel.operatingCodeEdit()->text() == "300" && document.model().services[0].operatingCode == "300"
				&& !document.isDirty() && document.revision() == revisionBeforeCancel,
			"refreshing drops unfinished text and leaves the document as it is");

		// rename: the references follow the service
		selection.setExcluded(SceneServiceOccurrence{"s1", 1}, true);
		events.log.clear();
		panel.idEdit()->setText("");
		panel.commitIdEdit();
		ok &= expect(prompts.errorCount == 1 && prompts.errorWarning && prompts.errorTitle == "Invalid Service ID"
				&& panel.idEdit()->text() == "s1" && document.model().services[0].id == "s1",
			"an empty id is dropped and the field shows the old id");
		panel.idEdit()->setText("s2");
		panel.commitIdEdit();
		ok &= expect(prompts.errorCount == 2 && prompts.errorTitle == "Duplicate Service ID" && panel.idEdit()->text() == "s1"
				&& document.model().services[0].id == "s1",
			"an id that another service has is dropped and the field shows the old id");
		panel.idEdit()->setText("  s1  ");
		panel.commitIdEdit();
		ok &= expect(prompts.errorCount == 2 && events.log.empty(), "rejected and unchanged ids do not touch the document");
		document.markSaved();
		panel.idEdit()->setText("s1_renamed");
		panel.commitIdEdit();
		const SceneModel& renamed = document.model();
		ok &= expect(renamed.services[0].id == "s1_renamed" && renamed.services[1].id == "s2" && document.isDirty()
				&& document.lastChangeLabel() == "Rename service",
			"a rename is written to the document and marks it changed");
		ok &= expect(renamed.scenarios[0].incidents[0].target == "s1_renamed" && renamed.scenarios[0].incidents[1].target == "s1"
				&& renamed.scenarios[0].entranceDelays[0].serviceId == "s1_renamed"
				&& renamed.passengers[0].journeys[0].legs[0].serviceId == "s1_renamed",
			"a rename rewrites the breakdown incidents, entrance delays and passenger legs that name the service");
		ok &= expect(selection.isExcluded(SceneServiceOccurrence{"s1_renamed", 1}) && !selection.isExcluded(SceneServiceOccurrence{"s1", 1}),
			"a rename moves the occurrences that the run leaves out to the new id");
		ok &= expect(events.log == Log({"documentChanged", "occurrences", "selected:s1_renamed", "renamed:s1>s1_renamed", "panelChanged"})
				&& events.kinds.testFlag(SceneDocument::Services) && events.kinds.testFlag(SceneDocument::Scenarios)
				&& events.kinds.testFlag(SceneDocument::Passengers),
			"a rename reports the services, scenarios and passengers it touched, and the renamed service, which the timetable follows");
		ok &= expect(items(panel.listWidget()) == QStringList({"s1_renamed", "s2", "s3"}) && panel.listWidget()->currentRow() == 0
				&& panel.idEdit()->text() == "s1_renamed" && panel.occurrenceTable()->item(0, 0)->data(Qt::UserRole).toString() == "s1_renamed"
				&& !checked(panel.occurrenceTable(), 0),
			"a rename keeps the selection and shows the new id in the list and in the occurrences");
		selection.includeAll();

		// add and duplicate
		events.log.clear();
		panel.addService();
		ok &= expect(document.model().services.size() == 4 && document.model().services[3].id == "new_service"
				&& panel.listWidget()->currentRow() == 3 && panel.idEdit()->text() == "new_service"
				&& events.log.front() == "documentChanged" && events.log.back() == "selected:new_service"
				&& events.kinds == SceneDocument::Services,
			"a new service is added at the end and selected");
		panel.addService();
		ok &= expect(document.model().services[4].id == "new_service_2", "a new service gets an unused id");
		panel.listWidget()->setCurrentRow(1);
		events.log.clear();
		panel.duplicateService();
		ok &= expect(items(panel.listWidget()) == QStringList({"s1_renamed", "s2", "s2_copy", "s3", "new_service", "new_service_2"})
				&& document.model().services[2].hasRepeat && document.model().services[2].headwaySeconds == 1200.0
				&& panel.listWidget()->currentRow() == 2 && events.log.front() == "documentChanged",
			"a duplicate follows its original, has its fields and is selected");

		// delete
		panel.listWidget()->setCurrentRow(0);
		events.log.clear();
		panel.deleteService();
		ok &= expect(prompts.errorCount == 3 && prompts.errorWarning && prompts.errorTitle == "Cannot Delete Service"
				&& prompts.errorMessage.contains("s1_renamed") && prompts.errorMessage.contains("incident")
				&& prompts.errorMessage.contains("entrance delay") && prompts.errorMessage.contains("leg1")
				&& prompts.confirmCount == 0 && document.model().services.size() == 6 && events.log.empty(),
			"a service that incidents, entrance delays or passenger legs use is refused, naming them");
		panel.listWidget()->setCurrentRow(3);
		events.log.clear();
		selection.setExcluded(SceneServiceOccurrence{"s3", 1}, true);
		prompts.confirmAnswer = false;
		panel.deleteService();
		ok &= expect(prompts.confirmCount == 1 && prompts.confirmQuestion == "Delete service 's3'?"
				&& document.model().services.size() == 6 && events.log.empty() && selection.isExcluded(SceneServiceOccurrence{"s3", 1}),
			"answering No keeps the service");
		prompts.confirmAnswer = true;
		panel.deleteService();
		ok &= expect(items(panel.listWidget()) == QStringList({"s1_renamed", "s2", "s2_copy", "new_service", "new_service_2"})
				&& panel.listWidget()->currentRow() == 3 && events.log.front() == "documentChanged"
				&& !selection.isExcluded(SceneServiceOccurrence{"s3", 1}),
			"answering Yes deletes the service, forgets what was left out of it and keeps a row selected");
		panel.listWidget()->setCurrentRow(4);
		panel.deleteService();
		ok &= expect(panel.listWidget()->currentRow() == 3 && panel.listWidget()->count() == 4,
			"deleting the last service selects the new last one");

		// a run does not close the services for the model but closes the fields
		panel.listWidget()->setCurrentRow(1);
		document.setEditable(false);
		panel.refresh();
		ok &= expect(!panel.listWidget()->isEnabled() && !panel.addButton()->isEnabled() && !panel.idEdit()->isEnabled()
				&& !panel.duplicateButton()->isEnabled() && !panel.deleteButton()->isEnabled()
				&& !panel.routeCombo()->isEnabled() && !panel.occurrenceTable()->isEnabled() && !panel.selectAllButton()->isEnabled(),
			"the fields are closed while a run uses the scene");
		events.log.clear();
		const SceneService before = document.model().services[1];
		panel.categoryCombo()->setCurrentIndex(1);
		panel.routeCombo()->setCurrentIndex(0);
		panel.colorButton()->chooseColor(QColor("#112233"));
		panel.commitPendingEditors();
		selection.setExcluded(SceneServiceOccurrence{"s2", 1}, true);
		panel.selectAllOccurrences();
		ok &= expect(events.log.empty() && document.model().services[1].category == before.category
				&& document.model().services[1].route == before.route && document.model().services[1].visualizationColor.empty()
				&& selection.isExcluded(SceneServiceOccurrence{"s2", 1}),
			"the fields, the pending edits and the occurrence buttons change nothing while a run uses the scene");
		document.setEditable(true);
		selection.includeAll();
		panel.refresh();

		// another scene replaces unfinished text without an edit
		events.log.clear();
		panel.idEdit()->setText("abandoned");
		SceneModel other;
		other.services = {service("other", "", "")};
		document.replace(other, false);
		ok &= expect(items(panel.listWidget()) == QStringList({"other"}) && panel.idEdit()->text() == "other"
				&& panel.compositionCombo()->count() == 1 && panel.compositionCombo()->currentText().isEmpty()
				&& panel.routeCombo()->currentText() == "(none)" && !document.isDirty()
				&& events.log == Log({"selected:other", "occurrences"}),
			"opening another scene replaces unfinished text without an edit");

		// detach
		panel.setDocument(nullptr);
		ok &= expect(panel.listWidget()->count() == 0 && !panel.listWidget()->isEnabled() && panel.occurrenceTable()->rowCount() == 0,
			"a detached panel shows nothing");
		const quint64 revisionDetached = document.revision();
		panel.addService();
		document.replace(serviceScene(), false);
		ok &= expect(panel.listWidget()->count() == 0 && document.revision() == revisionDetached + 1,
			"a detached panel neither edits nor follows the document");
	}

	{
		// the fields are one form: each label or check box is in the row of its field, and the choices can shrink
		ServicePanel panel;
		auto* form = panel.pages()->widget(0)->findChild<QFormLayout*>();
		ok &= expect(form && form->rowWrapPolicy() == QFormLayout::WrapLongRows, "the fields of a service are a form that wraps long rows");
		if (form) {
			const auto labelText = [form](QWidget* field) {
				auto* label = qobject_cast<QLabel*>(form->labelForField(field));
				return label ? label->text() : QString();
			};
			ok &= expect(labelText(panel.idEdit()) == "Service ID" && labelText(panel.operatingCodeEdit()) == "Operating code"
					&& labelText(panel.categoryCombo()) == "Category" && labelText(panel.colorButton()) == "Colour"
					&& labelText(panel.compositionCombo()) == "Composition" && labelText(panel.routeCombo()) == "Route"
					&& labelText(panel.performancePercentEdit()) == "Performance (%)",
				"each field has its label in its row");
			ok &= expect(form->labelForField(panel.entryTimeSecondsEdit()) == panel.hasEntryTimeCheck()
					&& form->labelForField(panel.headwaySecondsEdit()) == panel.hasRepeatCheck()
					&& form->labelForField(panel.repeatCountEdit()) == panel.hasRepeatCountCheck()
					&& form->labelForField(panel.maximumSpeedKmhEdit()) == panel.hasMaximumSpeedCheck()
					&& form->labelForField(panel.operatingCodeStepEdit()) == panel.hasOperatingCodeStepCheck(),
				"each check box is in the row of the field it switches on");
		}
		bool shrinks = true;
		for (QComboBox* choice : {panel.categoryCombo(), panel.compositionCombo(), panel.routeCombo()}) {
			choice->addItem(QString(120, QLatin1Char('x')));
			shrinks = shrinks && dynamic_cast<ChoiceComboBox*>(choice) && choice->minimumSizeHint().width() < choice->sizeHint().width();
		}
		ok &= expect(shrinks, "the category, composition and route are choices that can shrink below their longest entry");
	}

	{
		// a row keeps its field beside its label while there is room and puts it under the label in a narrow panel
		ServicePanel panel;
		auto* form = panel.pages()->widget(0)->findChild<QFormLayout*>();
		ok &= expect(form && form->hasHeightForWidth() && form->heightForWidth(form->minimumSize().width()) > form->heightForWidth(4000),
			"the fields are taller in a narrow panel, because their rows wrap");
	}

	{
		// in a short panel, where the fields scroll, the buttons of the colour field stay whole at every width, also where the
		// field just fits beside its label
		SceneDocument document;
		ServicePanel panel;
		panel.setDocument(&document);
		document.replace(serviceScene(), false);
		panel.show();
		const auto* choose = panel.colorButton()->findChild<QPushButton*>("serviceColorChooseButton");
		const auto* reset = panel.colorButton()->findChild<QPushButton*>("serviceColorDefaultButton");
		const auto* colorValue = panel.colorButton()->findChild<QLabel*>("serviceColorValue");
		const qreal defaultWidth = QFontMetricsF(colorValue->font()).horizontalAdvance(QStringLiteral("Default"));
		QStringList cut;
		for (int width = 200; width <= 900; ++width) {
			panel.resize(width, 300);
			QApplication::processEvents();
			if (choose->width() < choose->sizeHint().width() || reset->width() < reset->sizeHint().width() || colorValue->width() < defaultWidth)
				cut << QString::number(panel.width());
		}
		if (!cut.isEmpty())
			std::cerr << "cut at panel widths " << cut.join(' ').toStdString() << "\n";
		ok &= expect(choose && reset && cut.isEmpty(), "the Choose and Default buttons and the Default text of the colour field are whole at every width");
		panel.resize(300, 300);
		panel.colorButton()->setColor("not a colour, and longer than the field");
		QApplication::processEvents();
		const auto* shown = dynamic_cast<const ElidedLabel*>(colorValue);
		ok &= expect(shown && shown->displayText().endsWith(QChar(0x2026)) && colorValue->toolTip() == colorValue->text(),
			"a colour text that does not fit ends with an ellipsis and is whole in the tooltip");
	}

	{
		// over the closed route field the tip follows the text of the field with the stations of the route
		SceneDocument document;
		ServicePanel panel;
		panel.setDocument(&document);
		document.replace(serviceScene(), false);
		panel.show();
		QComboBox* route = panel.routeCombo();
		const QString stations = route->currentData(Qt::ToolTipRole).toString();
		ok &= expect(!stations.isEmpty() && shownTip(route) == route->currentText() + "\n" + stations,
			"the tip of the route field gives its text and the stations of the route");
		ok &= expect(route->toolTip() == stations, "the route field keeps the stations as its tooltip");
	}

	{
		// a widget put under the fields is a child of the panel
		ServicePanel panel;
		auto* timetable = new QWidget;
		panel.setTimetableWidget(timetable);
		ok &= expect(panel.pages()->widget(1)->isAncestorOf(timetable) && panel.pages()->count() == 3,
			"the timetable is on its own page between the fields and the occurrences");
	}

	{
		// opening another case shows its data in every dock of the services, also when it has the selected service
		SceneDocument document;
		ServicePanel panel;
		TimetableStopsPanel timetable;
		CaseSettingsPanel caseSettings;
		QObject::connect(&panel, &ServicePanel::selectedServiceChanged, &timetable, &TimetableStopsPanel::setServiceId);
		panel.setDocument(&document);
		timetable.setDocument(&document);
		caseSettings.setDocument(&document);
		const auto dwell = [&timetable]() {
			const QTableWidgetItem* item = timetable.table()->item(0, 2);
			return item ? item->text() : QString();
		};
		SceneModel first = serviceScene();
		first.services[0].stops.push_back(SceneStop());
		first.services[0].stops[0].stationId = "st1";
		first.services[0].stops[0].dwellSeconds = 60.0;
		document.replace(first, false);
		ok &= expect(panel.idEdit()->text() == "s1" && dwell() == "60" && caseSettings.nameEdit()->text() == "Services"
				&& panel.occurrenceTable()->rowCount() == 5,
			"a case shows its first service, its dwell, its name and its occurrences");

		SceneModel second = first;
		second.name = "Services changed";
		second.services[0].stops[0].dwellSeconds = 180.0;
		second.services[0].hasRepeat = true;
		second.services[0].headwaySeconds = 1200.0;
		document.replace(second, false);
		ok &= expect(panel.idEdit()->text() == "s1" && dwell() == "180" && caseSettings.nameEdit()->text() == "Services changed"
				&& panel.occurrenceTable()->rowCount() == 7,
			"another case with the selected service shows its dwell, its name and its occurrences");

		// the selected service is listed at another row
		panel.listWidget()->setCurrentRow(2);
		SceneModel reordered = first;
		reordered.services[2].stops.push_back(SceneStop());
		reordered.services[2].stops[0].stationId = "st1";
		reordered.services[2].stops[0].dwellSeconds = 90.0;
		std::reverse(reordered.services.begin(), reordered.services.end());
		document.replace(reordered, false);
		ok &= expect(items(panel.listWidget()) == QStringList({"s3", "s2", "s1"}) && panel.listWidget()->currentRow() == 0
				&& panel.idEdit()->text() == "s3" && dwell() == "90",
			"opening a scene that lists the selected service at another row keeps it selected and shows its stops");
	}

	{
		auto* document = new SceneDocument;
		ServicePanel panel;
		panel.setDocument(document);
		document->replace(serviceScene(), false);
		delete document;
		panel.idEdit()->setText("after the document");
		panel.commitIdEdit();
		panel.addService();
		panel.refresh();
		ok &= expect(panel.listWidget()->count() == 0 && !panel.addButton()->isEnabled(),
			"a panel whose document is gone shows nothing and edits nothing");
	}

	if (!ok)
		return 1;
	std::cout << "all service panel tests passed\n";
	return 0;
}
