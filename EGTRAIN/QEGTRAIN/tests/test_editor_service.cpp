#include "editors/CaseSettingsPanel.h"
#include "editors/EditorHelpers.h"
#include "editors/ServicePanel.h"
#include "editors/TimetableStopsPanel.h"

#include "scene/SceneWriter.h"

#include "support/FakePrompts.h"

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
#include <QScopeGuard>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTabWidget>
#include <QTest>
#include <QToolTip>

#include <algorithm>
#include <functional>
#include <iostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

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


// A panel with prompts and a run selection of its own on a document that shows a scene, and the events of both since
// the panel was given the scene.
struct ServiceFixture {
	SceneDocument document;
	ServicePanel panel;
	FakePrompts prompts;
	RunSelection selection;
	Events events{panel, document};

	explicit ServiceFixture(const SceneModel& scene = serviceScene()) {
		panel.setPrompts(&prompts);
		panel.setRunSelection(&selection);
		panel.setDocument(&document);
		events.log.clear();
		document.replace(scene, false);
	}

	// The panel is hidden while the fixture is whole. A panel destroyed while shown loses the focus in its QWidget
	// destructor, and a field that had the focus then calls a slot of the panel whose own part is already gone.
	~ServiceFixture() { panel.hide(); }
};

class TestServicePanel : public QObject {
	Q_OBJECT

private slots:
	void keepsUntouchedNumbers();
	void showsNothingWithoutScene();
	void emptyStateSaysWhyNoServiceIsShown();
	void showsNewScene();
	void showsSelectedService();
	void listsOccurrences();
	void leavesOccurrencesOut();
	void editsFieldsOfService();
	void renamesAddsAndDeletesServices();
	void closesFieldsWhileRunUsesScene();
	void refusesEditsWhileRunUsesScene();
	void followsOtherPartsOfScene();
	void showsAnotherScene();
	void detachedPanelIgnoresDocument();
	void fieldsAreOneForm();
	void choicesShrink();
	void rowsWrapInNarrowPanel();
	void colourFieldStaysWhole();
	void routeTipNamesStations();
	void timetableIsOnOwnPage();
	void showsOtherCaseInEveryDock();
	void forgetsDeletedDocument();
};

// Save, Undo and validation commit the field that is being edited. A stored number that a field rounds or shows at a
// limit of its range reaches the saved file as it is while the user does not edit it, even when the field has the focus,
// and a number that the user types is stored as typed.
void TestServicePanel::keepsUntouchedNumbers() {
	SceneModel scene = serviceScene();
	SceneService& stored = scene.services[0];
	stored.hasEntryTime = true;
	stored.entryTimeSeconds = 600.5;
	stored.hasRepeat = true;
	stored.headwaySeconds = 0.5;
	stored.performancePercent = 150.0;
	stored.hasMaximumSpeed = true;
	stored.maximumSpeedKmh = 1200.0;
	ServiceFixture f(scene);
	SceneDocument& document = f.document;
	ServicePanel& panel = f.panel;
	Events& events = f.events;
	events.log.clear();
	// An entry time and a headway are shown with their fractions.
	QCOMPARE_EQ(panel.entryTimeSecondsEdit()->value(), 600.5);
	QCOMPARE(panel.entryTimeSecondsEdit()->text(), "600.5");
	QCOMPARE_EQ(panel.headwaySecondsEdit()->value(), 0.5);
	QCOMPARE(panel.headwaySecondsEdit()->text(), "0.5");

	const nlohmann::json saved = encodeServices(document.model());
	const quint64 revision = document.revision();
	panel.commitPendingEditors();
	panel.show();
	panel.activateWindow();
	for (QDoubleSpinBox* field : {panel.entryTimeSecondsEdit(), panel.headwaySecondsEdit(), panel.performancePercentEdit(),
			 panel.maximumSpeedKmhEdit()}) {
		QVERIFY(giveFocus(field));
		panel.commitPendingEditors();
	}
	QVERIFY(giveFocus(panel.idEdit()));
	panel.commitEntryTimeSeconds();
	panel.commitHeadwaySeconds();
	panel.commitPerformancePercent(panel.performancePercentEdit()->value());
	panel.commitMaximumSpeed();
	const SceneService& kept = document.model().services[0];
	// An entry time of 600.5 s, a headway of 0.5 s, a performance of 150 and a maximum speed of 1200 km/h are saved as stored.
	QCOMPARE_EQ(kept.entryTimeSeconds, 600.5);
	QCOMPARE_EQ(kept.headwaySeconds, 0.5);
	QCOMPARE_EQ(kept.performancePercent, 150.0);
	QCOMPARE_EQ(kept.maximumSpeedKmh, 1200.0);
	QVERIFY(encodeServices(document.model()) == saved);
	// Visiting the number fields and committing them adds no Undo step.
	QVERIFY(events.log.empty());
	QCOMPARE(document.revision(), revision);
	QVERIFY(!document.isDirty());

	QVERIFY(giveFocus(panel.entryTimeSecondsEdit()));
	spinText(panel.entryTimeSecondsEdit())->setText("610.25");
	panel.commitPendingEditors();
	// An entry time that is being typed is stored with its fraction.
	QCOMPARE_EQ(kept.entryTimeSeconds, 610.25);
	QCOMPARE(document.revision(), revision + 1);
	QCOMPARE(document.lastChangeLabel(), "Change service entry time");
	QVERIFY(giveFocus(panel.headwaySecondsEdit()));
	typeNumber(panel.headwaySecondsEdit(), "0.25");
	panel.commitPendingEditors();
	// A headway that is typed is stored with its fraction, and the other fields keep their values.
	QCOMPARE_EQ(kept.headwaySeconds, 0.25);
	QCOMPARE_EQ(kept.entryTimeSeconds, 610.25);
	QCOMPARE_EQ(kept.performancePercent, 150.0);
	QCOMPARE_EQ(kept.maximumSpeedKmh, 1200.0);
	QCOMPARE(document.revision(), revision + 2);
}

void TestServicePanel::showsNothingWithoutScene() {
	ServicePanel panel;
	// A panel without a document shows nothing and offers nothing.
	QCOMPARE(panel.listWidget()->count(), 0);
	QVERIFY(!panel.listWidget()->isEnabled());
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY(!panel.idEdit()->isEnabled());
	QCOMPARE(panel.occurrenceTable()->rowCount(), 0);
	QVERIFY(panel.occurrenceSelectionLabel()->text().isEmpty());
	QVERIFY(!panel.selectAllButton()->isEnabled());
	SceneDocument document;
	panel.setDocument(&document);
	panel.addService();
	panel.duplicateService();
	panel.deleteService();
	panel.commitIdEdit();
	panel.commitPendingEditors();
	panel.selectNoneOccurrences();
	// A panel stays disabled while its document holds no scene, and its actions change nothing.
	QVERIFY(!panel.addButton()->isEnabled());
	QCOMPARE(document.revision(), quint64(0));
	QVERIFY(!document.isDirty());
	QVERIFY(document.model().services.empty());
}

// The message above the fields says why no service is shown.
void TestServicePanel::emptyStateSaysWhyNoServiceIsShown() {
	SceneDocument document;
	ServicePanel panel;
	// A panel without a document asks to open a case.
	QCOMPARE(panel.emptyStateLabel()->text(), "Open a case to edit services.");
	QVERIFY(panel.emptyStateLabel()->isVisibleTo(&panel));
	panel.setDocument(&document);
	QVERIFY2(panel.emptyStateLabel()->text() == "Open a case to edit services.", "a document without a scene asks to open a case");
	SceneModel scene = serviceScene();
	scene.services.clear();
	document.replace(scene, false);
	// A scene without services points to Add Service.
	QCOMPARE(panel.emptyStateLabel()->text(), "Select a service or use Add Service.");
	QVERIFY(panel.emptyStateLabel()->isVisibleTo(&panel));
	document.setEditable(false);
	panel.refresh();
	QVERIFY2(panel.emptyStateLabel()->text() == "No services.", "a scene without services does not point to Add Service while a run uses it");
	document.setEditable(true);
	document.replace(serviceScene(), false);
	QVERIFY2(panel.emptyStateLabel()->isHidden(), "the message is hidden while a service is shown");
}

void TestServicePanel::showsNewScene() {
	ServiceFixture f;
	ServicePanel& panel = f.panel;
	// A new scene is listed and its first service is selected.
	QCOMPARE(items(panel.listWidget()), QStringList({"s1", "s2", "s3"}));
	QCOMPARE(panel.listWidget()->currentRow(), 0);
	QVERIFY(panel.listWidget()->isEnabled());
	QVERIFY(panel.addButton()->isEnabled());
	QVERIFY(panel.duplicateButton()->isEnabled());
	QVERIFY(panel.deleteButton()->isEnabled());
	// The selected service shows its id, code, composition and route.
	QCOMPARE(panel.idEdit()->text(), "s1");
	QVERIFY(panel.idEdit()->isEnabled());
	QCOMPARE(panel.operatingCodeEdit()->text(), "100");
	QCOMPARE(panel.compositionCombo()->currentText(), "c1");
	QCOMPARE(panel.compositionCombo()->count(), 2);
	QCOMPARE(panel.routeCombo()->currentData().toString(), "r1");
	QCOMPARE(panel.routeCombo()->count(), 2);
	QCOMPARE(panel.categoryCombo()->currentIndex(), 0);
	QCOMPARE(panel.categoryCombo()->count(), 7);
	// The id and code fields show the start of their text.
	QCOMPARE(panel.idEdit()->cursorPosition(), 0);
	QCOMPARE(panel.operatingCodeEdit()->cursorPosition(), 0);
	// The optional fields wait for the box that switches them on.
	QVERIFY(!panel.hasEntryTimeCheck()->isChecked());
	QVERIFY(!panel.entryTimeSecondsEdit()->isEnabled());
	QVERIFY(!panel.hasRepeatCheck()->isChecked());
	QVERIFY(!panel.headwaySecondsEdit()->isEnabled());
	QVERIFY(!panel.hasRepeatCountCheck()->isEnabled());
	QVERIFY(!panel.repeatCountEdit()->isEnabled());
	QVERIFY(!panel.hasOperatingCodeStepCheck()->isEnabled());
	QVERIFY(!panel.hasMaximumSpeedCheck()->isChecked());
	QVERIFY(!panel.maximumSpeedKmhEdit()->isEnabled());
	QCOMPARE_EQ(panel.performancePercentEdit()->value(), 100.0);
	// Showing a scene edits nothing, and tells the timetable and the lists of occurrences.
	QCOMPARE(f.events.log, Log({"selected:s1", "occurrences"}));
	QVERIFY(!f.document.isDirty());
	QCOMPARE(f.document.revision(), quint64(1));
}

void TestServicePanel::showsSelectedService() {
	ServiceFixture f;
	ServicePanel& panel = f.panel;
	f.events.log.clear();
	panel.listWidget()->setCurrentRow(1);
	// Another selected service shows its own fields and tells the timetable.
	QCOMPARE(panel.idEdit()->text(), "s2");
	QVERIFY(panel.hasRepeatCheck()->isChecked());
	QVERIFY(panel.headwaySecondsEdit()->isEnabled());
	QCOMPARE_EQ(panel.headwaySecondsEdit()->value(), 1200.0);
	QCOMPARE(panel.routeCombo()->currentData().toString(), "r2");
	QCOMPARE(f.events.log, Log({"selected:s2"}));
}

// The occurrences: one for s1, three for s2 and one for s3.
void TestServicePanel::listsOccurrences() {
	ServiceFixture f;
	const QTableWidget* table = f.panel.occurrenceTable();
	// The occurrence table lists every occurrence of every service.
	QCOMPARE(table->rowCount(), 5);
	QVERIFY(table->isEnabled());
	QCOMPARE(table->item(1, 2)->text(), "r2");
	QCOMPARE(table->item(2, 3)->text(), "+1200 s");
	QCOMPARE(table->item(0, 1)->text(), "100");
	QCOMPARE(table->item(1, 1)->text(), "s2-1");
	QCOMPARE(table->item(1, 0)->data(Qt::UserRole).toString(), "s2");
	QCOMPARE(table->item(1, 0)->data(Qt::UserRole + 1).toInt(), 1);
	// The label counts the occurrences, and its tooltip gives the counting rule.
	QCOMPARE(f.panel.occurrenceSelectionLabel()->text(), "5 of 5 selected");
	QCOMPARE(f.panel.occurrenceSelectionLabel()->toolTip(), "An occurrence is in the run period when it enters from 0 up to 3600 s.");
	// The summary names the occurrences in the run period only when they differ from the selected ones.
	QCOMPARE(ServicePanel::occurrenceSummaryText(4, 3, 3, 4), "3 of 4 selected");
	QCOMPARE(ServicePanel::occurrenceSummaryText(4, 3, 2, 4), "3 of 4 selected, 2 in the run period");
	QCOMPARE(ServicePanel::occurrenceSummaryText(900, 900, 900, 700),
		"900 of 900 selected\nShowing the first 700. All of them run. Lower the repeat count to choose which.");
	QVERIFY2(table->item(0, 3)->toolTip() == "100 (route r1, +0 s)\nService s1, run 1", "a row names the generated service in its tooltip");
	QVERIFY2(generatedServiceLabel(f.document.model().services[1], 2) == "s2-2 (route r2, +1200 s)",
		"a generated service is named by its code, route and scheduled entry");
}

// The preview leaves occurrences out, then a service with more occurrences than a run expands limits it and forgets
// what the preview left out.
void TestServicePanel::leavesOccurrencesOut() {
	ServiceFixture f;
	SceneDocument& document = f.document;
	ServicePanel& panel = f.panel;
	RunSelection& selection = f.selection;
	Events& events = f.events;

	// The preview: leave an occurrence out.
	events.log.clear();
	panel.occurrenceTable()->item(2, 0)->setCheckState(Qt::Unchecked);
	// Leaving an occurrence out is kept in the run selection and does not change the scene.
	QVERIFY(selection.isExcluded(SceneServiceOccurrence{"s2", 2}));
	QCOMPARE(selection.excluded().size(), std::size_t(1));
	QVERIFY(!checked(panel.occurrenceTable(), 2));
	QVERIFY(checked(panel.occurrenceTable(), 1));
	QCOMPARE(panel.occurrenceSelectionLabel()->text(), "4 of 5 selected");
	QCOMPARE(events.log, Log({"occurrences"}));
	QVERIFY(!document.isDirty());
	QCOMPARE(document.revision(), quint64(1));
	events.log.clear();
	panel.selectNoneOccurrences();
	// Select none leaves every occurrence out.
	QCOMPARE(selection.excluded().size(), std::size_t(5));
	QVERIFY(!checked(panel.occurrenceTable(), 0));
	QCOMPARE(panel.occurrenceSelectionLabel()->text(), "0 of 5 selected");
	QVERIFY(!document.isDirty());
	panel.selectAllOccurrences();
	// Select all includes every occurrence again.
	QVERIFY(selection.excluded().empty());
	QVERIFY(checked(panel.occurrenceTable(), 4));
	QVERIFY(!document.isDirty());
	selection.setExcluded(SceneServiceOccurrence{"s2", 2}, true);
	selection.setExcluded(SceneServiceOccurrence{"s2", 3}, true);
	selection.setExcluded(SceneServiceOccurrence{"gone", 1}, true);
	panel.setOccurrenceDuration([] { return 2400.0; });
	panel.refreshOccurrencePreview();
	// The duration of the window counts the occurrences, and what is left out beyond them is forgotten.
	QCOMPARE(panel.occurrenceTable()->rowCount(), 4);
	QCOMPARE(selection.excluded().size(), std::size_t(1));
	QVERIFY(selection.isExcluded(SceneServiceOccurrence{"s2", 2}));
	QVERIFY(!checked(panel.occurrenceTable(), 2));
	panel.setOccurrenceDuration({});

	// A service with more occurrences than a run expands.
	panel.listWidget()->setCurrentRow(2);
	panel.hasRepeatCheck()->setChecked(true);
	panel.hasRepeatCountCheck()->setChecked(true);
	panel.repeatCountEdit()->setText("800");
	panel.commitRepeatCount();
	// A preview is limited to the trains a run expands, and then cannot be used to leave occurrences out.
	QCOMPARE(panel.occurrenceTable()->rowCount(), 700);
	QVERIFY(!panel.occurrenceTable()->isEnabled());
	QVERIFY(!panel.selectAllButton()->isEnabled());
	QVERIFY(!panel.selectNoneButton()->isEnabled());
	QVERIFY(panel.occurrenceSelectionLabel()->text().contains("\nShowing the first 700. All of them run."));
	panel.selectNoneOccurrences();
	QVERIFY2(selection.excluded().empty(), "Select none does nothing while the preview is limited");
}

// The edits of the fields of a service, then pending text and a refresh, which see the code that the edits left.
void TestServicePanel::editsFieldsOfService() {
	ServiceFixture f;
	SceneDocument& document = f.document;
	ServicePanel& panel = f.panel;
	Events& events = f.events;
	// An edit that lists the occurrences again, and one that does not.
	const Log editWithOccurrences({"documentChanged", "occurrences", "panelChanged"});
	const Log editOnly({"documentChanged", "panelChanged"});

	events.log.clear();
	document.markSaved();
	panel.operatingCodeEdit()->setText("  200  ");
	panel.commitOperatingCode();
	// A service code is written to the document, trimmed, and the occurrences are listed again.
	QCOMPARE(document.model().services[0].operatingCode, "200");
	QVERIFY(document.isDirty());
	QCOMPARE(events.log, editWithOccurrences);
	QCOMPARE(events.kinds, SceneDocument::ChangeKinds(SceneDocument::Services));
	QCOMPARE(document.lastChangeLabel(), "Change service code");
	QCOMPARE(panel.occurrenceTable()->item(0, 1)->text(), "200");
	events.log.clear();
	panel.commitOperatingCode();
	QVERIFY2(events.log.empty(), "an unchanged service code does not touch the document");

	panel.categoryCombo()->setCurrentIndex(panel.categoryCombo()->findData("Freight"));
	// A chosen category is written to the document.
	QCOMPARE(document.model().services[0].category, "Freight");
	QCOMPARE(events.log, editOnly);
	events.log.clear();
	panel.categoryCombo()->setCurrentIndex(0);
	// No category clears the category.
	QVERIFY(document.model().services[0].category.empty());
	QCOMPARE(events.log, editOnly);

	events.log.clear();
	panel.colorButton()->chooseColor(QColor("#336699"));
	// A chosen colour is written to the document.
	QCOMPARE(document.model().services[0].visualizationColor, "#336699");
	QCOMPARE(events.log, editOnly);
	events.log.clear();
	panel.colorButton()->resetColor();
	// The default colour clears the colour.
	QVERIFY(document.model().services[0].visualizationColor.empty());
	QCOMPARE(events.log, editOnly);

	events.log.clear();
	panel.compositionCombo()->setCurrentText("c2");
	// A chosen composition is written to the document without listing the occurrences.
	QCOMPARE(document.model().services[0].composition, "c2");
	QCOMPARE(events.log, editOnly);
	QCOMPARE(panel.compositionCombo()->currentText(), "c2");

	events.log.clear();
	panel.routeCombo()->setCurrentIndex(panel.routeCombo()->findData("r2"));
	// A chosen route is written to the document and tells the timetable that its route has changed.
	QCOMPARE(document.model().services[0].route, "r2");
	QCOMPARE(events.log, Log({"documentChanged", "occurrences", "selected:s1", "panelChanged"}));
	QCOMPARE(panel.occurrenceTable()->item(0, 2)->text(), "r2");

	events.log.clear();
	panel.hasEntryTimeCheck()->setChecked(true);
	// The entry time switches on.
	QVERIFY(document.model().services[0].hasEntryTime);
	QVERIFY(panel.entryTimeSecondsEdit()->isEnabled());
	QCOMPARE(events.log, editWithOccurrences);
	events.log.clear();
	typeNumber(panel.entryTimeSecondsEdit(), "90");
	panel.commitEntryTimeSeconds();
	// An entry time is written to the document and shown in the occurrences.
	QCOMPARE_EQ(document.model().services[0].entryTimeSeconds, 90.0);
	QCOMPARE(events.log, editWithOccurrences);
	QCOMPARE(panel.occurrenceTable()->item(0, 3)->text(), "+90 s");
	events.log.clear();
	typeNumber(panel.entryTimeSecondsEdit(), "");
	panel.commitEntryTimeSeconds();
	// A blank entry time shows the stored one again.
	QCOMPARE_EQ(document.model().services[0].entryTimeSeconds, 90.0);
	QCOMPARE(panel.entryTimeSecondsEdit()->text(), "90");
	QVERIFY(events.log.empty());
	typeNumber(panel.entryTimeSecondsEdit(), "0");
	panel.commitEntryTimeSeconds();
	QVERIFY2(document.model().services[0].entryTimeSeconds == 0.0, "an entry time can be zero");

	panel.hasRepeatCheck()->setChecked(true);
	panel.hasRepeatCountCheck()->setChecked(true);
	panel.hasOperatingCodeStepCheck()->setChecked(true);
	// The repeat count and the code step start at one.
	QVERIFY(document.model().services[0].hasRepeat);
	QVERIFY(document.model().services[0].hasRepeatCount);
	QCOMPARE(document.model().services[0].repeatCount, 1);
	QVERIFY(document.model().services[0].hasOperatingCodeStep);
	QCOMPARE(document.model().services[0].operatingCodeStep, 1);
	QCOMPARE(panel.repeatCountEdit()->text(), "1");
	QCOMPARE(panel.operatingCodeStepEdit()->text(), "1");
	QVERIFY(panel.repeatCountEdit()->isEnabled());
	QVERIFY(panel.operatingCodeStepEdit()->isEnabled());
	panel.repeatCountEdit()->setText("3");
	panel.commitRepeatCount();
	panel.operatingCodeEdit()->setText("200");
	panel.operatingCodeStepEdit()->setText("0");
	panel.commitOperatingCodeStep();
	// The repeat count is written to the document and a code step of zero becomes one.
	QCOMPARE(document.model().services[0].repeatCount, 3);
	QCOMPARE(document.model().services[0].operatingCodeStep, 1);
	QCOMPARE(panel.operatingCodeStepEdit()->text(), "1");
	QCOMPARE(panel.occurrenceTable()->rowCount(), 7);
	QCOMPARE(panel.occurrenceTable()->item(2, 1)->text(), "202");
	panel.operatingCodeStepEdit()->setText("-2");
	panel.commitOperatingCodeStep();
	QVERIFY2(document.model().services[0].operatingCodeStep == -2, "a negative code step is kept");
	typeNumber(panel.headwaySecondsEdit(), "600");
	panel.commitHeadwaySeconds();
	QVERIFY2(document.model().services[0].headwaySeconds == 600.0, "a headway is written to the document");
	panel.hasRepeatCheck()->setChecked(false);
	// Switching the repeat off switches the repeat count and the code step off.
	QVERIFY(!document.model().services[0].hasRepeat);
	QVERIFY(!document.model().services[0].hasRepeatCount);
	QVERIFY(!document.model().services[0].hasOperatingCodeStep);
	QVERIFY(!panel.hasRepeatCountCheck()->isChecked());
	QVERIFY(!panel.hasRepeatCountCheck()->isEnabled());
	QVERIFY(!panel.hasOperatingCodeStepCheck()->isChecked());
	QVERIFY(!panel.repeatCountEdit()->isEnabled());
	QVERIFY(!panel.headwaySecondsEdit()->isEnabled());

	events.log.clear();
	panel.performancePercentEdit()->setValue(80.5);
	// A performance is written to the document and shown in the occurrences.
	QCOMPARE_EQ(document.model().services[0].performancePercent, 80.5);
	QCOMPARE(events.log, editWithOccurrences);
	QCOMPARE(panel.occurrenceTable()->item(0, 4)->text(), "80.5");
	panel.hasMaximumSpeedCheck()->setChecked(true);
	// Switching the maximum speed on starts at 100 km/h.
	QVERIFY(document.model().services[0].hasMaximumSpeed);
	QCOMPARE_EQ(document.model().services[0].maximumSpeedKmh, 100.0);
	QCOMPARE_EQ(panel.maximumSpeedKmhEdit()->value(), 100.0);
	QVERIFY(panel.maximumSpeedKmhEdit()->isEnabled());
	QCOMPARE(panel.occurrenceTable()->item(0, 5)->text(), "100");
	panel.maximumSpeedKmhEdit()->setValue(87.25);
	panel.commitMaximumSpeed();
	// A maximum speed is written to the document.
	QCOMPARE_EQ(document.model().services[0].maximumSpeedKmh, 87.25);
	QCOMPARE(panel.occurrenceTable()->item(0, 5)->text(), "87.25");

	// Pending edits: a number field commits the text in it only while it has the focus.
	events.log.clear();
	panel.show();
	panel.activateWindow();
	panel.hasEntryTimeCheck()->setChecked(true);
	spinText(panel.entryTimeSecondsEdit())->setText("45");
	panel.operatingCodeEdit()->setText("300");
	// Text that is still being typed is not in the document.
	QCOMPARE_EQ(document.model().services[0].entryTimeSeconds, 0.0);
	QCOMPARE(document.model().services[0].operatingCode, "200");
	panel.commitPendingEditors();
	// Committing the pending editors takes the text of the code, and not a number in a field without the focus.
	QCOMPARE_EQ(document.model().services[0].entryTimeSeconds, 0.0);
	QCOMPARE(document.model().services[0].operatingCode, "300");
	QVERIFY(giveFocus(panel.entryTimeSecondsEdit()));
	panel.commitPendingEditors();
	QVERIFY2(document.model().services[0].entryTimeSeconds == 45.0,
		"committing the pending editors takes the number that is being typed in the field with the focus");
	panel.hide();

	// Cancel: text that was typed and not finished is dropped when the panel is refreshed.
	events.log.clear();
	document.markSaved();
	const quint64 revisionBeforeCancel = document.revision();
	panel.operatingCodeEdit()->setText("abandoned");
	panel.refresh();
	// Refreshing drops unfinished text and leaves the document as it is.
	QCOMPARE(panel.operatingCodeEdit()->text(), "300");
	QCOMPARE(document.model().services[0].operatingCode, "300");
	QVERIFY(!document.isDirty());
	QCOMPARE(document.revision(), revisionBeforeCancel);
}

// A rename, whose references follow the service, then Add, Duplicate and Delete, which see the new id and the errors
// that the rename reported.
void TestServicePanel::renamesAddsAndDeletesServices() {
	ServiceFixture f;
	SceneDocument& document = f.document;
	ServicePanel& panel = f.panel;
	FakePrompts& prompts = f.prompts;
	RunSelection& selection = f.selection;
	Events& events = f.events;

	// Rename: the references follow the service.
	selection.setExcluded(SceneServiceOccurrence{"s1", 1}, true);
	events.log.clear();
	panel.idEdit()->setText("");
	panel.commitIdEdit();
	// An empty id is dropped and the field shows the old id.
	QCOMPARE(prompts.errorCount, 1);
	QVERIFY(prompts.errorWarning);
	QCOMPARE(prompts.errorTitle, "Invalid Service ID");
	QCOMPARE(panel.idEdit()->text(), "s1");
	QCOMPARE(document.model().services[0].id, "s1");
	panel.idEdit()->setText("s2");
	panel.commitIdEdit();
	// An id that another service has is dropped and the field shows the old id.
	QCOMPARE(prompts.errorCount, 2);
	QCOMPARE(prompts.errorTitle, "Duplicate Service ID");
	QCOMPARE(panel.idEdit()->text(), "s1");
	QCOMPARE(document.model().services[0].id, "s1");
	panel.idEdit()->setText("  s1  ");
	panel.commitIdEdit();
	// Rejected and unchanged ids do not touch the document.
	QCOMPARE(prompts.errorCount, 2);
	QVERIFY(events.log.empty());
	document.markSaved();
	panel.idEdit()->setText("s1_renamed");
	panel.commitIdEdit();
	const SceneModel& renamed = document.model();
	// A rename is written to the document and marks it changed.
	QCOMPARE(renamed.services[0].id, "s1_renamed");
	QCOMPARE(renamed.services[1].id, "s2");
	QVERIFY(document.isDirty());
	QCOMPARE(document.lastChangeLabel(), "Rename service");
	// A rename rewrites the breakdown incidents, entrance delays and passenger legs that name the service.
	QCOMPARE(renamed.scenarios[0].incidents[0].target, "s1_renamed");
	QCOMPARE(renamed.scenarios[0].incidents[1].target, "s1");
	QCOMPARE(renamed.scenarios[0].entranceDelays[0].serviceId, "s1_renamed");
	QCOMPARE(renamed.passengers[0].journeys[0].legs[0].serviceId, "s1_renamed");
	// A rename moves the occurrences that the run leaves out to the new id.
	QVERIFY(selection.isExcluded(SceneServiceOccurrence{"s1_renamed", 1}));
	QVERIFY(!selection.isExcluded(SceneServiceOccurrence{"s1", 1}));
	// A rename reports the services, scenarios and passengers it touched, and the renamed service, which the timetable
	// follows.
	QCOMPARE(events.log, Log({"documentChanged", "occurrences", "selected:s1_renamed", "renamed:s1>s1_renamed", "panelChanged"}));
	QVERIFY(events.kinds.testFlag(SceneDocument::Services));
	QVERIFY(events.kinds.testFlag(SceneDocument::Scenarios));
	QVERIFY(events.kinds.testFlag(SceneDocument::Passengers));
	// A rename keeps the selection and shows the new id in the list and in the occurrences.
	QCOMPARE(items(panel.listWidget()), QStringList({"s1_renamed", "s2", "s3"}));
	QCOMPARE(panel.listWidget()->currentRow(), 0);
	QCOMPARE(panel.idEdit()->text(), "s1_renamed");
	QCOMPARE(panel.occurrenceTable()->item(0, 0)->data(Qt::UserRole).toString(), "s1_renamed");
	QVERIFY(!checked(panel.occurrenceTable(), 0));
	selection.includeAll();

	// Add and duplicate.
	events.log.clear();
	panel.addService();
	// A new service is added at the end and selected.
	QCOMPARE(document.model().services.size(), std::size_t(4));
	QCOMPARE(document.model().services[3].id, "new_service");
	QCOMPARE(panel.listWidget()->currentRow(), 3);
	QCOMPARE(panel.idEdit()->text(), "new_service");
	QCOMPARE(events.log.front(), "documentChanged");
	QCOMPARE(events.log.back(), "selected:new_service");
	QCOMPARE(events.kinds, SceneDocument::ChangeKinds(SceneDocument::Services));
	panel.addService();
	QVERIFY2(document.model().services[4].id == "new_service_2", "a new service gets an unused id");
	panel.listWidget()->setCurrentRow(1);
	events.log.clear();
	panel.duplicateService();
	// A duplicate follows its original, has its fields and is selected.
	QCOMPARE(items(panel.listWidget()), QStringList({"s1_renamed", "s2", "s2_copy", "s3", "new_service", "new_service_2"}));
	QVERIFY(document.model().services[2].hasRepeat);
	QCOMPARE_EQ(document.model().services[2].headwaySeconds, 1200.0);
	QCOMPARE(panel.listWidget()->currentRow(), 2);
	QCOMPARE(events.log.front(), "documentChanged");

	// Delete.
	panel.listWidget()->setCurrentRow(0);
	events.log.clear();
	panel.deleteService();
	// A service that incidents, entrance delays or passenger legs use is refused, naming them.
	QCOMPARE(prompts.errorCount, 3);
	QVERIFY(prompts.errorWarning);
	QCOMPARE(prompts.errorTitle, "Cannot Delete Service");
	QVERIFY(prompts.errorMessage.contains("s1_renamed"));
	QVERIFY(prompts.errorMessage.contains("incident"));
	QVERIFY(prompts.errorMessage.contains("entrance delay"));
	QVERIFY(prompts.errorMessage.contains("leg1"));
	QCOMPARE(prompts.confirmCount, 0);
	QCOMPARE(document.model().services.size(), std::size_t(6));
	QVERIFY(events.log.empty());
	panel.listWidget()->setCurrentRow(3);
	events.log.clear();
	selection.setExcluded(SceneServiceOccurrence{"s3", 1}, true);
	prompts.confirmAnswer = false;
	panel.deleteService();
	// Answering No keeps the service.
	QCOMPARE(prompts.confirmCount, 1);
	QCOMPARE(prompts.confirmQuestion, "Delete service 's3'?");
	QCOMPARE(document.model().services.size(), std::size_t(6));
	QVERIFY(events.log.empty());
	QVERIFY(selection.isExcluded(SceneServiceOccurrence{"s3", 1}));
	prompts.confirmAnswer = true;
	panel.deleteService();
	// Answering Yes deletes the service, forgets what was left out of it and keeps a row selected.
	QCOMPARE(items(panel.listWidget()), QStringList({"s1_renamed", "s2", "s2_copy", "new_service", "new_service_2"}));
	QCOMPARE(panel.listWidget()->currentRow(), 3);
	QCOMPARE(events.log.front(), "documentChanged");
	QVERIFY(!selection.isExcluded(SceneServiceOccurrence{"s3", 1}));
	panel.listWidget()->setCurrentRow(4);
	panel.deleteService();
	// Deleting the last service selects the new last one.
	QCOMPARE(panel.listWidget()->currentRow(), 3);
	QCOMPARE(panel.listWidget()->count(), 4);
}

void TestServicePanel::closesFieldsWhileRunUsesScene() {
	ServiceFixture f;
	SceneDocument& document = f.document;
	ServicePanel& panel = f.panel;
	RunSelection& selection = f.selection;
	panel.listWidget()->setCurrentRow(1);
	document.setEditable(false);
	panel.refresh();
	// The fields are closed while a run uses the scene.
	QVERIFY(!panel.listWidget()->isEnabled());
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY(!panel.idEdit()->isEnabled());
	QVERIFY(!panel.duplicateButton()->isEnabled());
	QVERIFY(!panel.deleteButton()->isEnabled());
	QVERIFY(!panel.routeCombo()->isEnabled());
	QVERIFY(!panel.occurrenceTable()->isEnabled());
	QVERIFY(!panel.selectAllButton()->isEnabled());
	f.events.log.clear();
	const SceneService before = document.model().services[1];
	panel.categoryCombo()->setCurrentIndex(1);
	panel.routeCombo()->setCurrentIndex(0);
	panel.colorButton()->chooseColor(QColor("#112233"));
	panel.commitPendingEditors();
	selection.setExcluded(SceneServiceOccurrence{"s2", 1}, true);
	panel.selectAllOccurrences();
	// The fields, the pending edits and the occurrence buttons change nothing while a run uses the scene.
	QVERIFY(f.events.log.empty());
	QCOMPARE(document.model().services[1].category, before.category);
	QCOMPARE(document.model().services[1].route, before.route);
	QVERIFY(document.model().services[1].visualizationColor.empty());
	QVERIFY(selection.isExcluded(SceneServiceOccurrence{"s2", 1}));
}

// While a run uses the scene no field and no button reaches the services, also when the panel still shows them as open.
void TestServicePanel::refusesEditsWhileRunUsesScene() {
	const std::vector<std::pair<QString, std::function<void(ServicePanel&)>>> commits = {
		{"id", [](ServicePanel& panel) {
			 panel.idEdit()->setText("renamed");
			 panel.commitIdEdit();
		 }},
		{"code", [](ServicePanel& panel) {
			 panel.operatingCodeEdit()->setText("999");
			 panel.commitOperatingCode();
		 }},
		{"composition", [](ServicePanel& panel) { panel.commitComposition("c2"); }},
		{"entry time on", [](ServicePanel& panel) { panel.commitHasEntryTime(true); }},
		{"entry time", [](ServicePanel& panel) {
			 typeNumber(panel.entryTimeSecondsEdit(), "60");
			 panel.commitEntryTimeSeconds();
		 }},
		{"repeat off", [](ServicePanel& panel) { panel.commitHasRepeat(false); }},
		{"headway", [](ServicePanel& panel) {
			 typeNumber(panel.headwaySecondsEdit(), "60");
			 panel.commitHeadwaySeconds();
		 }},
		{"repeat count on", [](ServicePanel& panel) { panel.commitHasRepeatCount(true); }},
		{"repeat count", [](ServicePanel& panel) {
			 panel.repeatCountEdit()->setText("7");
			 panel.commitRepeatCount();
		 }},
		{"performance", [](ServicePanel& panel) { panel.commitPerformancePercent(50.0); }},
		{"maximum speed on", [](ServicePanel& panel) { panel.commitHasMaximumSpeed(true); }},
		{"maximum speed", [](ServicePanel& panel) {
			 typeNumber(panel.maximumSpeedKmhEdit(), "80");
			 panel.commitMaximumSpeed();
		 }},
		{"code step on", [](ServicePanel& panel) { panel.commitHasOperatingCodeStep(true); }},
		{"code step", [](ServicePanel& panel) {
			 panel.operatingCodeStepEdit()->setText("3");
			 panel.commitOperatingCodeStep();
		 }},
		{"add", [](ServicePanel& panel) { panel.addService(); }},
		{"duplicate", [](ServicePanel& panel) { panel.duplicateService(); }},
		{"delete", [](ServicePanel& panel) { panel.deleteService(); }},
	};
	const auto fields = [](const SceneService& s) {
		return std::tie(s.id, s.operatingCode, s.composition, s.performancePercent, s.hasMaximumSpeed, s.maximumSpeedKmh,
			s.hasEntryTime, s.entryTimeSeconds, s.hasRepeat, s.headwaySeconds, s.hasRepeatCount, s.repeatCount,
			s.hasOperatingCodeStep, s.operatingCodeStep);
	};
	QStringList reached;
	for (const auto& [name, commit] : commits) {
		ServiceFixture f;
		f.prompts.confirmAnswer = true;
		f.panel.listWidget()->setCurrentRow(1);
		f.document.setEditable(false);
		f.events.log.clear();
		const SceneService before = f.document.model().services[1];
		const quint64 revision = f.document.revision();
		commit(f.panel);
		if (f.document.model().services.size() != 3 || fields(f.document.model().services[1]) != fields(before)
			|| f.document.revision() != revision || !f.events.log.empty())
			reached << name;
	}
	QCOMPARE(reached, QStringList());

	// An edit that touches a part that stays open during a run goes through, and any other is refused.
	ServiceFixture f;
	f.document.setEditable(false);
	const auto rename = [](SceneModel& model) { model.name = "edited"; };
	QVERIFY(!f.document.edit(SceneDocument::Services | SceneDocument::Passengers, "Refused", rename));
	QVERIFY(f.document.model().name == "Services");
	QVERIFY(f.document.edit(SceneDocument::Compositions | SceneDocument::Services, "Open", rename));
	QVERIFY(f.document.model().name == "edited");
	QCOMPARE(f.document.lastChangeLabel(), "Open");
}

// The panel shows again what another editor changes: the routes, the compositions and the duration of the case.
void TestServicePanel::followsOtherPartsOfScene() {
	ServiceFixture f;
	ServicePanel& panel = f.panel;
	panel.listWidget()->setCurrentRow(1);
	const int routes = panel.routeCombo()->count();
	QVERIFY(f.document.edit(SceneDocument::Infrastructure, "Add route", [](SceneModel& model) {
		SceneRoute route;
		route.id = "r3";
		model.routes.push_back(route);
	}));
	QCOMPARE(panel.routeCombo()->count(), routes + 1);
	QVERIFY(f.document.edit(SceneDocument::Compositions, "Add composition", [](SceneModel& model) {
		SceneComposition composition;
		composition.id = "c3";
		model.compositions.push_back(composition);
	}));
	QVERIFY(panel.compositionCombo()->findText("c3") >= 0);
	const int occurrences = panel.occurrenceTable()->rowCount();
	QVERIFY(f.document.edit(SceneDocument::CaseSettings, "Edit case settings",
		[](SceneModel& model) { model.settings.durationSeconds = 7200.0; }));
	QVERIFY(panel.occurrenceTable()->rowCount() > occurrences);
}

// Another scene replaces unfinished text without an edit.
void TestServicePanel::showsAnotherScene() {
	ServiceFixture f;
	ServicePanel& panel = f.panel;
	f.events.log.clear();
	panel.idEdit()->setText("abandoned");
	SceneModel other;
	other.services = {service("other", "", "")};
	f.document.replace(other, false);
	// Opening another scene replaces unfinished text without an edit.
	QCOMPARE(items(panel.listWidget()), QStringList({"other"}));
	QCOMPARE(panel.idEdit()->text(), "other");
	QCOMPARE(panel.compositionCombo()->count(), 1);
	QVERIFY(panel.compositionCombo()->currentText().isEmpty());
	QCOMPARE(panel.routeCombo()->currentText(), "(none)");
	QVERIFY(!f.document.isDirty());
	QCOMPARE(f.events.log, Log({"selected:other", "occurrences"}));
}

void TestServicePanel::detachedPanelIgnoresDocument() {
	ServiceFixture f;
	f.panel.setDocument(nullptr);
	// A detached panel shows nothing.
	QCOMPARE(f.panel.listWidget()->count(), 0);
	QVERIFY(!f.panel.listWidget()->isEnabled());
	QCOMPARE(f.panel.occurrenceTable()->rowCount(), 0);
	const quint64 revisionDetached = f.document.revision();
	f.panel.addService();
	f.document.replace(serviceScene(), false);
	// A detached panel neither edits nor follows the document.
	QCOMPARE(f.panel.listWidget()->count(), 0);
	QCOMPARE(f.document.revision(), revisionDetached + 1);
}

// The fields are one form: each label or check box is in the row of its field.
void TestServicePanel::fieldsAreOneForm() {
	ServicePanel panel;
	auto* form = panel.pages()->widget(0)->findChild<QFormLayout*>();
	// The fields of a service are a form that wraps long rows.
	if (form == nullptr)
		QFAIL("the fields of a service are not a form");
	QCOMPARE(form->rowWrapPolicy(), QFormLayout::WrapLongRows);
	const auto labelText = [form](QWidget* field) {
		auto* label = qobject_cast<QLabel*>(form->labelForField(field));
		return label ? label->text() : QString();
	};
	// Each field has its label in its row.
	QCOMPARE(labelText(panel.idEdit()), "Service ID");
	QCOMPARE(labelText(panel.operatingCodeEdit()), "Operating code");
	QCOMPARE(labelText(panel.categoryCombo()), "Category");
	QCOMPARE(labelText(panel.colorButton()), "Colour");
	QCOMPARE(labelText(panel.compositionCombo()), "Composition");
	QCOMPARE(labelText(panel.routeCombo()), "Route");
	QCOMPARE(labelText(panel.performancePercentEdit()), "Performance (%)");
	// Each check box is in the row of the field it switches on.
	QVERIFY(form->labelForField(panel.entryTimeSecondsEdit()) == panel.hasEntryTimeCheck());
	QVERIFY(form->labelForField(panel.headwaySecondsEdit()) == panel.hasRepeatCheck());
	QVERIFY(form->labelForField(panel.repeatCountEdit()) == panel.hasRepeatCountCheck());
	QVERIFY(form->labelForField(panel.maximumSpeedKmhEdit()) == panel.hasMaximumSpeedCheck());
	QVERIFY(form->labelForField(panel.operatingCodeStepEdit()) == panel.hasOperatingCodeStepCheck());
}

// The choices of the form can shrink.
void TestServicePanel::choicesShrink() {
	ServicePanel panel;
	bool shrinks = true;
	for (QComboBox* choice : {panel.categoryCombo(), panel.compositionCombo(), panel.routeCombo()}) {
		choice->addItem(QString(120, QLatin1Char('x')));
		shrinks = shrinks && dynamic_cast<ChoiceComboBox*>(choice) && choice->minimumSizeHint().width() < choice->sizeHint().width();
	}
	QVERIFY2(shrinks, "the category, composition and route are choices that can shrink below their longest entry");
}

// A row keeps its field beside its label while there is room and puts it under the label in a narrow panel.
void TestServicePanel::rowsWrapInNarrowPanel() {
	ServicePanel panel;
	auto* form = panel.pages()->widget(0)->findChild<QFormLayout*>();
	// The fields are taller in a narrow panel, because their rows wrap.
	if (form == nullptr)
		QFAIL("the fields of a service are not a form");
	QVERIFY(form->hasHeightForWidth());
	QCOMPARE_GT(form->heightForWidth(form->minimumSize().width()), form->heightForWidth(4000));
}

// In a short panel, where the fields scroll, the buttons of the colour field stay whole at every width, also where the
// field just fits beside its label.
void TestServicePanel::colourFieldStaysWhole() {
	SceneDocument document;
	ServicePanel panel;
	panel.setDocument(&document);
	document.replace(serviceScene(), false);
	panel.show();
	// The panel is hidden before it is destroyed, also when a check fails.
	const auto hide = qScopeGuard([&panel] { panel.hide(); });
	const auto* choose = panel.colorButton()->findChild<QPushButton*>("serviceColorChooseButton");
	const auto* reset = panel.colorButton()->findChild<QPushButton*>("serviceColorDefaultButton");
	const auto* colorValue = panel.colorButton()->findChild<QLabel*>("serviceColorValue");
	// The Choose and Default buttons and the Default text of the colour field are whole at every width.
	if (choose == nullptr)
		QFAIL("the colour field has no Choose button");
	if (reset == nullptr)
		QFAIL("the colour field has no Default button");
	const qreal defaultWidth = QFontMetricsF(colorValue->font()).horizontalAdvance(QStringLiteral("Default"));
	QStringList cut;
	for (int width = 200; width <= 900; ++width) {
		panel.resize(width, 300);
		QApplication::processEvents();
		if (choose->width() < choose->sizeHint().width() || reset->width() < reset->sizeHint().width() || colorValue->width() < defaultWidth)
			cut << QString::number(panel.width());
	}
	QVERIFY2(cut.isEmpty(),
		qPrintable("the Choose and Default buttons and the Default text of the colour field are whole at every width; cut at panel widths " + cut.join(' ')));
	panel.resize(300, 300);
	panel.colorButton()->setColor("not a colour, and longer than the field");
	QApplication::processEvents();
	const auto* shown = dynamic_cast<const ElidedLabel*>(colorValue);
	// A colour text that does not fit ends with an ellipsis and is whole in the tooltip.
	if (shown == nullptr)
		QFAIL("the text of the colour field is not an elided label");
	QVERIFY(shown->displayText().endsWith(QChar(0x2026)));
	QCOMPARE(colorValue->toolTip(), colorValue->text());
}

// Over the closed route field the tip follows the text of the field with the stations of the route.
void TestServicePanel::routeTipNamesStations() {
	SceneDocument document;
	ServicePanel panel;
	panel.setDocument(&document);
	document.replace(serviceScene(), false);
	panel.show();
	// The panel is hidden before it is destroyed, also when a check fails.
	const auto hide = qScopeGuard([&panel] { panel.hide(); });
	QComboBox* route = panel.routeCombo();
	const QString stations = route->currentData(Qt::ToolTipRole).toString();
	// The tip of the route field gives its text and the stations of the route.
	QVERIFY(!stations.isEmpty());
	QCOMPARE(shownTip(route), route->currentText() + "\n" + stations);
	QVERIFY2(route->toolTip() == stations, "the route field keeps the stations as its tooltip");
}

// A widget put under the fields is a child of the panel.
void TestServicePanel::timetableIsOnOwnPage() {
	ServicePanel panel;
	auto* timetable = new QWidget;
	panel.setTimetableWidget(timetable);
	// The timetable is on its own page between the fields and the occurrences.
	QVERIFY(panel.pages()->widget(1)->isAncestorOf(timetable));
	QCOMPARE(panel.pages()->count(), 3);
}

// Opening another case shows its data in every dock of the services, also when it has the selected service.
void TestServicePanel::showsOtherCaseInEveryDock() {
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
	// A case shows its first service, its dwell, its name and its occurrences.
	QCOMPARE(panel.idEdit()->text(), "s1");
	QCOMPARE(dwell(), "60");
	QCOMPARE(caseSettings.nameEdit()->text(), "Services");
	QCOMPARE(panel.occurrenceTable()->rowCount(), 5);

	SceneModel second = first;
	second.name = "Services changed";
	second.services[0].stops[0].dwellSeconds = 180.0;
	second.services[0].hasRepeat = true;
	second.services[0].headwaySeconds = 1200.0;
	document.replace(second, false);
	// Another case with the selected service shows its dwell, its name and its occurrences.
	QCOMPARE(panel.idEdit()->text(), "s1");
	QCOMPARE(dwell(), "180");
	QCOMPARE(caseSettings.nameEdit()->text(), "Services changed");
	QCOMPARE(panel.occurrenceTable()->rowCount(), 7);

	// The selected service is listed at another row.
	panel.listWidget()->setCurrentRow(2);
	SceneModel reordered = first;
	reordered.services[2].stops.push_back(SceneStop());
	reordered.services[2].stops[0].stationId = "st1";
	reordered.services[2].stops[0].dwellSeconds = 90.0;
	std::reverse(reordered.services.begin(), reordered.services.end());
	document.replace(reordered, false);
	// Opening a scene that lists the selected service at another row keeps it selected and shows its stops.
	QCOMPARE(items(panel.listWidget()), QStringList({"s3", "s2", "s1"}));
	QCOMPARE(panel.listWidget()->currentRow(), 0);
	QCOMPARE(panel.idEdit()->text(), "s3");
	QCOMPARE(dwell(), "90");
}

void TestServicePanel::forgetsDeletedDocument() {
	auto* document = new SceneDocument;
	ServicePanel panel;
	panel.setDocument(document);
	document->replace(serviceScene(), false);
	delete document;
	panel.idEdit()->setText("after the document");
	panel.commitIdEdit();
	panel.addService();
	panel.refresh();
	// A panel whose document is gone shows nothing and edits nothing.
	QCOMPARE(panel.listWidget()->count(), 0);
	QVERIFY(!panel.addButton()->isEnabled());
}

QTEST_MAIN(TestServicePanel)
#include "test_editor_service.moc"
