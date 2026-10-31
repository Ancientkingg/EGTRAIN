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
#include <QTest>

#include <string>
#include <vector>

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

static const Log edit({"documentChanged", "panelChanged"});

// A panel with prompts and a run selection of its own on a document that shows delayScene(), with the scenario base
// selected.
struct DelayFixture {
	SceneDocument document;
	EntranceDelayPanel panel;
	FakePrompts prompts;
	RunSelection selection;
	Events events{panel, document};

	DelayFixture() {
		panel.setPrompts(&prompts);
		panel.setRunSelection(&selection);
		panel.setDocument(&document);
		document.replace(delayScene(), false);
		selection.selectScenario("base");
		panel.refresh();
	}

	// The panel is hidden while the fixture is whole. A panel destroyed while shown loses the focus in its QWidget
	// destructor, and a field that had the focus then calls a slot of the panel whose own part is already gone.
	~DelayFixture() { panel.hide(); }
};

class TestEntranceDelayPanel : public QObject {
	Q_OBJECT

private slots:
	void showsNothingWithoutScene();
	void listsDelaysOfSelectedScenario();
	void showsFieldsOfSelectedDelay();
	void showsFieldsOfAnotherDelay();
	void editsAddsAndDeletesDelays();
	void showsDelayOfMissingService();
	void addsDelayToSceneWithoutCalls();
	void addsScenarioOnlyWithAnEdit();
	void showsOtherScenario();
	void takesPendingEditOfFocusedField();
	void choicesFollowDocument();
	void ignoresActionsWhileRunUsesScene();
	void showsAnotherScene();
	void detachedPanelIgnoresDocument();
	void forgetsDeletedDocument();
	void longNamesDoNotWidenPanel();
};

void TestEntranceDelayPanel::showsNothingWithoutScene() {
	EntranceDelayPanel panel;
	// A panel without a document shows nothing and offers nothing, and does not point to the disabled Add button.
	QCOMPARE(panel.listWidget()->count(), 0);
	QVERIFY(!panel.listWidget()->isEnabled());
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY(!panel.serviceCombo()->isEnabled());
	QVERIFY(!panel.occurrenceEdit()->isEnabled());
	QVERIFY(!panel.stationCombo()->isEnabled());
	QVERIFY(!panel.secondsEdit()->isEnabled());
	QCOMPARE(panel.occurrenceContextLabel()->text(), QString());
	QVERIFY(panel.emptyStateLabel()->isHidden());
	SceneDocument document;
	panel.setDocument(&document);
	panel.addEntranceDelay();
	panel.duplicateEntranceDelay();
	panel.deleteEntranceDelay();
	panel.commitOccurrence();
	panel.commitSeconds();
	panel.commitPendingEditors();
	// A panel stays disabled while its document holds no scene, and its actions change nothing.
	QVERIFY(!panel.addButton()->isEnabled());
	QCOMPARE(document.revision(), quint64(0));
	QVERIFY(!document.isDirty());
	QVERIFY(document.model().scenarios.empty());
}

void TestEntranceDelayPanel::listsDelaysOfSelectedScenario() {
	DelayFixture f;
	// The delays of the selected scenario are listed with the service code, and the first is selected.
	QCOMPARE(items(f.panel.listWidget()), QStringList({"100-2 at A: +30 s", "s2 at X: +0 s"}));
	QCOMPARE(f.panel.listWidget()->currentRow(), 0);
	QVERIFY(f.panel.listWidget()->isEnabled());
	QVERIFY(f.panel.addButton()->isEnabled());
	QVERIFY(f.panel.duplicateButton()->isEnabled());
	QVERIFY(f.panel.deleteButton()->isEnabled());
	// Showing a scene edits nothing.
	QVERIFY(f.events.log.empty());
	QVERIFY(!f.document.isDirty());
	QCOMPARE(f.document.revision(), quint64(1));
	QVERIFY(!f.selection.isScenarioModified("base"));
}

void TestEntranceDelayPanel::showsFieldsOfSelectedDelay() {
	DelayFixture f;
	// The services are offered by their first occurrence, and the service of the delay is chosen.
	QCOMPARE(choices(f.panel.serviceCombo()), QStringList({"100-1 (route r1, +0 s)", "s2 (route r1, +0 s)", "s3 (route r1, +0 s)"}));
	QCOMPARE(f.panel.serviceCombo()->currentData().toString(), "s1");
	QVERIFY(f.panel.serviceCombo()->isEnabled());
	// The occurrences of the service limit the occurrence.
	QCOMPARE(f.panel.occurrenceEdit()->minimum(), 1);
	QCOMPARE(f.panel.occurrenceEdit()->maximum(), 3);
	QCOMPARE(f.panel.occurrenceEdit()->value(), 2);
	QVERIFY(f.panel.occurrenceEdit()->isEnabled());
	// The context names the generated service, and the occurrence the number of occurrences.
	QCOMPARE(f.panel.occurrenceContextLabel()->text(), "100-2 (route r1, +1200 s)");
	QCOMPARE(f.panel.occurrenceEdit()->suffix(), " of 3");
	// The stations are the first calls of the service with a planned departure.
	QCOMPARE(choices(f.panel.stationCombo()), QStringList({"A", "D"}));
	QCOMPARE(f.panel.stationCombo()->currentText(), "A");
	QVERIFY(f.panel.stationCombo()->isEnabled());
	// The delay is shown.
	QCOMPARE_EQ(f.panel.secondsEdit()->value(), 30.0);
	QVERIFY(f.panel.secondsEdit()->isEnabled());
}

// Another delay shows its own fields, and selecting it edits nothing.
void TestEntranceDelayPanel::showsFieldsOfAnotherDelay() {
	DelayFixture f;
	f.panel.listWidget()->setCurrentRow(1);
	QCOMPARE(f.panel.serviceCombo()->currentData().toString(), "s2");
	QCOMPARE(f.panel.occurrenceEdit()->maximum(), 1);
	QCOMPARE(f.panel.occurrenceContextLabel()->text(), "s2 (route r1, +0 s)");
	QCOMPARE(f.panel.occurrenceEdit()->suffix(), " of 1");
	QCOMPARE(choices(f.panel.stationCombo()), QStringList({"X"}));
	QCOMPARE_EQ(f.panel.secondsEdit()->value(), 0.0);
	QVERIFY(f.events.log.empty());
}

// The edits of the fields, then a change of the service and the actions Add, Duplicate and Delete, which see the delay
// that the edits left.
void TestEntranceDelayPanel::editsAddsAndDeletesDelays() {
	DelayFixture f;
	SceneDocument& document = f.document;
	EntranceDelayPanel& panel = f.panel;
	Events& events = f.events;

	// A chosen station is written to the document, and the scenario counts as edited.
	panel.stationCombo()->setCurrentIndex(1);
	QCOMPARE(document.model().scenarios[0].entranceDelays[0].stationId, "D");
	QVERIFY(document.isDirty());
	QVERIFY(f.selection.isScenarioModified("base"));
	QCOMPARE(events.log, edit);
	QCOMPARE(events.kinds, SceneDocument::ChangeKinds(SceneDocument::Scenarios));
	QCOMPARE(document.lastChangeLabel(), "Change entrance delay station");
	QCOMPARE(panel.stationCombo()->currentText(), "D");
	events.log.clear();
	panel.stationCombo()->setCurrentIndex(1);
	QVERIFY2(events.log.empty(), "choosing the station that is chosen does not touch the document");
	// An occurrence is written to the document, and the context and the list show it.
	panel.occurrenceEdit()->setValue(3);
	panel.commitOccurrence();
	QCOMPARE(document.model().scenarios[0].entranceDelays[0].occurrence, 3);
	QCOMPARE(events.log, edit);
	QCOMPARE(panel.occurrenceContextLabel()->text(), "100-3 (route r1, +2400 s)");
	QCOMPARE(items(panel.listWidget())[0], "100-3 at D: +30 s");
	events.log.clear();
	panel.commitOccurrence();
	QVERIFY2(events.log.empty(), "an unchanged occurrence does not touch the document");
	// A delay that is negative is written to the document, and the list shows it.
	panel.secondsEdit()->setValue(-12.5);
	panel.commitSeconds();
	QCOMPARE_EQ(document.model().scenarios[0].entranceDelays[0].delaySeconds, -12.5);
	QCOMPARE(events.log, edit);
	QCOMPARE(items(panel.listWidget())[0], "100-3 at D: -12.5 s");
	events.log.clear();
	panel.commitSeconds();
	QVERIFY2(events.log.empty(), "an unchanged delay does not touch the document");

	// Another service starts at its first occurrence and at a station where it plans to depart.
	panel.serviceCombo()->setCurrentIndex(panel.serviceCombo()->findData("s2"));
	const SceneEntranceDelay& changed = document.model().scenarios[0].entranceDelays[0];
	QCOMPARE(changed.serviceId, "s2");
	QCOMPARE(changed.occurrence, 1);
	QCOMPARE(changed.stationId, "X");
	QCOMPARE_EQ(changed.delaySeconds, -12.5);
	QCOMPARE(choices(panel.stationCombo()), QStringList({"X"}));
	QCOMPARE(panel.occurrenceEdit()->maximum(), 1);
	QCOMPARE(document.lastChangeLabel(), "Change entrance delay service");
	// A service without a call where it departs has no station to choose.
	panel.serviceCombo()->setCurrentIndex(panel.serviceCombo()->findData("s3"));
	QCOMPARE(changed.serviceId, "s3");
	QVERIFY(changed.stationId.empty());
	QCOMPARE(panel.stationCombo()->currentText(), "(none)");
	QCOMPARE(choices(panel.stationCombo()), QStringList({"(none)"}));
	QCOMPARE(panel.occurrenceContextLabel()->text(), "s3 (route r1, +0 s)");
	// The first station is chosen again.
	panel.serviceCombo()->setCurrentIndex(panel.serviceCombo()->findData("s1"));
	QCOMPARE(changed.serviceId, "s1");
	QCOMPARE(changed.stationId, "A");
	QCOMPARE(changed.occurrence, 1);

	// A delay is added for the first service that departs from a station, selected and shown.
	document.markSaved();
	f.selection.clearModifiedScenarios();
	events.log.clear();
	panel.addEntranceDelay();
	const SceneEntranceDelay& added = document.model().scenarios[0].entranceDelays.back();
	QCOMPARE(document.model().scenarios[0].entranceDelays.size(), std::size_t(3));
	QCOMPARE(added.serviceId, "s1");
	QCOMPARE(added.stationId, "A");
	QCOMPARE(added.occurrence, 1);
	QCOMPARE_EQ(added.delaySeconds, 0.0);
	QCOMPARE(panel.listWidget()->count(), 3);
	QCOMPARE(panel.listWidget()->currentRow(), 2);
	QVERIFY(document.isDirty());
	QVERIFY(f.selection.isScenarioModified("base"));
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Add entrance delay");
	// A copy is put after the delay and selected.
	events.log.clear();
	panel.listWidget()->setCurrentRow(0);
	panel.duplicateEntranceDelay();
	QCOMPARE(document.model().scenarios[0].entranceDelays.size(), std::size_t(4));
	QCOMPARE(document.model().scenarios[0].entranceDelays[1].serviceId, "s1");
	QCOMPARE_EQ(document.model().scenarios[0].entranceDelays[1].delaySeconds, -12.5);
	QCOMPARE(panel.listWidget()->currentRow(), 1);
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Duplicate entrance delay");
	// Declining the question keeps the delay.
	f.prompts.confirmCount = 0;
	events.log.clear();
	panel.deleteEntranceDelay();
	QCOMPARE(f.prompts.confirmCount, 1);
	QCOMPARE(f.prompts.confirmQuestion, "Delete entrance delay for service 's1'?");
	QCOMPARE(document.model().scenarios[0].entranceDelays.size(), std::size_t(4));
	QVERIFY(events.log.empty());
	// A deleted delay is removed.
	f.prompts.confirmAnswer = true;
	panel.deleteEntranceDelay();
	QCOMPARE(document.model().scenarios[0].entranceDelays.size(), std::size_t(3));
	QCOMPARE(panel.listWidget()->count(), 3);
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Delete entrance delay");
}

// A delay of a service that is gone is shown as missing, without a code.
void TestEntranceDelayPanel::showsDelayOfMissingService() {
	DelayFixture f;
	f.document.mutableModel().scenarios[0].entranceDelays[1].serviceId = "gone";
	f.document.mutableModel().scenarios[0].entranceDelays[1].stationId.clear();
	f.panel.refresh();
	f.panel.listWidget()->setCurrentRow(1);
	QCOMPARE(f.panel.serviceCombo()->currentText(), "gone (missing)");
	QCOMPARE(f.panel.stationCombo()->currentText(), "(none)");
	QCOMPARE(f.panel.occurrenceContextLabel()->text(), QString());
	QCOMPARE(f.panel.occurrenceEdit()->suffix(), " of 1");
	QCOMPARE(items(f.panel.listWidget())[1], "gone #1 at (none): +0 s");
}

// A new delay of a scene without calls names the first service and no station.
void TestEntranceDelayPanel::addsDelayToSceneWithoutCalls() {
	DelayFixture f;
	SceneModel noStops = delayScene();
	for (SceneService& service : noStops.services)
		service.stops.clear();
	f.document.replace(noStops, false);
	f.panel.addEntranceDelay();
	QCOMPARE(f.document.model().scenarios[0].entranceDelays.back().serviceId, "s1");
	QVERIFY(f.document.model().scenarios[0].entranceDelays.back().stationId.empty());
}

// A scene without scenarios stays as it is until the first delay adds the baseline scenario in one edit.
void TestEntranceDelayPanel::addsScenarioOnlyWithAnEdit() {
	DelayFixture f;
	SceneModel scene = delayScene();
	scene.scenarios.clear();
	scene.defaultScenarioId.clear();
	f.document.replace(scene, false);
	f.panel.duplicateEntranceDelay();
	f.panel.deleteEntranceDelay();
	QVERIFY(f.document.model().scenarios.empty());
	QVERIFY(!f.document.isDirty());
	f.panel.addEntranceDelay();
	QCOMPARE(f.document.model().scenarios.size(), std::size_t(1));
	QCOMPARE(f.document.model().scenarios[0].entranceDelays.size(), std::size_t(1));
	QVERIFY(f.document.isDirty());
	QCOMPARE(f.events.log, edit);
	QCOMPARE(f.events.kinds, SceneDocument::ChangeKinds(SceneDocument::Scenarios));
}

void TestEntranceDelayPanel::showsOtherScenario() {
	DelayFixture f;
	EntranceDelayPanel& panel = f.panel;
	// A scenario without delays shows an empty list and says so, and a delay can be added.
	f.selection.selectScenario("alt");
	panel.refresh();
	QCOMPARE(panel.listWidget()->count(), 0);
	QVERIFY(panel.addButton()->isEnabled());
	QVERIFY(!panel.duplicateButton()->isEnabled());
	QVERIFY(!panel.deleteButton()->isEnabled());
	QCOMPARE(panel.occurrenceContextLabel()->text(), QString());
	QCOMPARE(panel.emptyStateLabel()->text(), "This scenario has no entrance delays. Use Add.");
	QVERIFY(!panel.emptyStateLabel()->isHidden());
	QVERIFY(panel.emptyStateLabel()->property("dialogHelp").toBool());
	QVERIFY(!panel.serviceCombo()->isEnabled());
	// A delay is added to the selected scenario.
	panel.addEntranceDelay();
	QCOMPARE(f.document.model().scenarios[1].entranceDelays.size(), std::size_t(1));
	QCOMPARE(f.document.model().scenarios[0].entranceDelays.size(), std::size_t(2));
	QVERIFY(f.selection.isScenarioModified("alt"));
	QVERIFY(!f.selection.isScenarioModified("base"));
	QVERIFY2(panel.emptyStateLabel()->isHidden(), "the selected delay has no empty-state message");
	// A scenario with delays and no selected row asks for a selection.
	panel.listWidget()->setCurrentRow(-1);
	QCOMPARE(panel.emptyStateLabel()->text(), "Select an entrance delay or use Add.");
	QVERIFY(!panel.emptyStateLabel()->isHidden());
}

// Pending edits belong to the field that has the focus.
void TestEntranceDelayPanel::takesPendingEditOfFocusedField() {
	DelayFixture f;
	f.document.markSaved();
	f.events.log.clear();
	f.panel.show();
	f.panel.activateWindow();
	f.panel.secondsEdit()->setValue(77.0);
	f.panel.commitPendingEditors();
	// A value in a field that does not have the focus is not pending.
	QCOMPARE_EQ(f.document.model().scenarios[0].entranceDelays[0].delaySeconds, 30.0);
	QVERIFY(f.events.log.empty());
	f.panel.secondsEdit()->setFocus();
	QApplication::processEvents();
	f.panel.commitPendingEditors();
	// A value in the field that has the focus is taken into the document.
	QCOMPARE_EQ(f.document.model().scenarios[0].entranceDelays[0].delaySeconds, 77.0);
	QCOMPARE(f.events.log, edit);
}

// The choices follow the document.
void TestEntranceDelayPanel::choicesFollowDocument() {
	DelayFixture f;
	SceneDocument& document = f.document;
	EntranceDelayPanel& panel = f.panel;
	document.mutableModel().services[0].stops[2].stationId = "E";
	// A station that the document has not announced is not offered.
	QCOMPARE(choices(panel.stationCombo()), QStringList({"A", "D"}));
	document.markChanged(SceneDocument::Services, "Edit timetable stop");
	// The stations follow the timetable of the service, and the selection stays.
	QCOMPARE(choices(panel.stationCombo()), QStringList({"A", "E"}));
	QCOMPARE(panel.listWidget()->currentRow(), 0);
	document.mutableModel().settings.durationSeconds = 7200.0;
	document.markChanged(SceneDocument::CaseSettings, "Edit case settings");
	// A longer case gives the service more occurrences.
	QCOMPARE(panel.occurrenceEdit()->maximum(), 6);
	QCOMPARE(panel.occurrenceEdit()->suffix(), " of 6");
	panel.setOccurrenceDuration([] { return 2400.0; });
	document.markChanged(SceneDocument::CaseSettings, "Edit case settings");
	// The duration that the window gives counts the occurrences.
	QCOMPARE(panel.occurrenceEdit()->maximum(), 2);
	panel.setOccurrenceDuration({});
	document.mutableModel().services[0].operatingCode = "200";
	document.markChanged(SceneDocument::Services, "Change service code");
	QVERIFY2(items(panel.listWidget())[0].startsWith("200-2 at "), "the list follows the service code");
	document.mutableModel().services[0].operatingCode = "300";
	document.markChanged(SceneDocument::Scenarios, "Edit scenario");
	QVERIFY2(items(panel.listWidget())[0].startsWith("200-2 at "), "the delays stay as they are when only scenarios change");
}

// The scene is in use by a run.
void TestEntranceDelayPanel::ignoresActionsWhileRunUsesScene() {
	DelayFixture f;
	SceneDocument& document = f.document;
	EntranceDelayPanel& panel = f.panel;
	document.markSaved();
	f.events.log.clear();
	document.setEditable(false);
	panel.refresh();
	const int shownRow = panel.listWidget()->currentRow();
	panel.listWidget()->setCurrentRow(-1);
	QVERIFY2(panel.emptyStateLabel()->isHidden(), "a scene that a run uses does not point to the disabled Add button");
	panel.listWidget()->setCurrentRow(shownRow);
	// Nothing can be done with the delays while a run uses the scene.
	QVERIFY(!panel.listWidget()->isEnabled());
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY(!panel.duplicateButton()->isEnabled());
	QVERIFY(!panel.deleteButton()->isEnabled());
	QVERIFY(!panel.serviceCombo()->isEnabled());
	QVERIFY(!panel.occurrenceEdit()->isEnabled());
	QVERIFY(!panel.stationCombo()->isEnabled());
	QVERIFY(!panel.secondsEdit()->isEnabled());
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
	// Actions change nothing while a run uses the scene: the delay keeps the 30 s of the scene.
	QCOMPARE_EQ(document.model().scenarios[0].entranceDelays[0].delaySeconds, 30.0);
	QCOMPARE(document.model().scenarios[0].entranceDelays.size(), std::size_t(2));
	QVERIFY(f.events.log.empty());
	QVERIFY(!document.isDirty());
}

// Opening another scene shows its delays without an edit.
void TestEntranceDelayPanel::showsAnotherScene() {
	DelayFixture f;
	SceneModel other = delayScene();
	other.scenarios[0].entranceDelays.resize(1);
	other.scenarios[0].entranceDelays[0].delaySeconds = 4.0;
	f.document.replace(other, false);
	QCOMPARE(items(f.panel.listWidget()), QStringList({"100-2 at A: +4 s"}));
	QVERIFY(!f.document.isDirty());
}

void TestEntranceDelayPanel::detachedPanelIgnoresDocument() {
	DelayFixture f;
	f.panel.setDocument(nullptr);
	// A detached panel shows nothing.
	QCOMPARE(f.panel.listWidget()->count(), 0);
	QVERIFY(!f.panel.listWidget()->isEnabled());
	QCOMPARE(f.panel.serviceCombo()->count(), 0);
	const quint64 revisionDetached = f.document.revision();
	f.panel.addEntranceDelay();
	f.document.replace(delayScene(), false);
	// A detached panel neither edits nor follows the document.
	QCOMPARE(f.panel.listWidget()->count(), 0);
	QCOMPARE(f.document.revision(), revisionDetached + 1);
}

// A panel whose document is gone shows nothing and edits nothing.
void TestEntranceDelayPanel::forgetsDeletedDocument() {
	auto* document = new SceneDocument;
	EntranceDelayPanel panel;
	panel.setDocument(document);
	document->replace(delayScene(), false);
	delete document;
	panel.commitSeconds();
	panel.addEntranceDelay();
	panel.refresh();
	QCOMPARE(panel.listWidget()->count(), 0);
	QVERIFY(!panel.addButton()->isEnabled());
}

void TestEntranceDelayPanel::longNamesDoNotWidenPanel() {
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
	// The fields show the long names.
	QCOMPARE(panel.serviceCombo()->currentData().toString(), QString::fromStdString(longService));
	QCOMPARE(panel.stationCombo()->currentText(), QString::fromStdString(longStation));
	QCOMPARE_LT(panel.minimumSizeHint().width(), 400);
}

QTEST_MAIN(TestEntranceDelayPanel)
#include "test_editor_entrancedelay.moc"
