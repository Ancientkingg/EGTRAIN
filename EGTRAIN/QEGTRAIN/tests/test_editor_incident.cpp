#include "editors/EditorRules.h"
#include "editors/IncidentPanel.h"
#include "editors/InfrastructurePanel.h"
#include "editors/infrastructure/TopologyFacets.h"
#include "scene/RuntimeLimits.h"
#include "scene/SceneWriter.h"
#include "support/FakePrompts.h"
#include "widgets/ChoiceComboBox.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QLoggingCategory>
#include <QProxyStyle>
#include <QPushButton>
#include <QScopeGuard>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QToolButton>
#include <QToolTip>

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The macOS style, where a form keeps each field at its size hint.
class MacFormStyle : public QProxyStyle {
public:
	MacFormStyle() : QProxyStyle(QStringLiteral("Fusion")) {}
	int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const override {
		if (hint == SH_FormLayoutFieldGrowthPolicy)
			return QFormLayout::FieldsStayAtSizeHint;
		return QProxyStyle::styleHint(hint, option, widget, returnData);
	}
};

static SceneService service(const char* id) {
	SceneService result;
	result.id = id;
	result.route = "r1";
	return result;
}

static SceneIncident signalFailure(const char* id, const char* target) {
	SceneIncident incident;
	incident.id = id;
	incident.type = "signal_failure";
	incident.target = target;
	incident.startSeconds = 10.0;
	incident.endSeconds = 20.0;
	incident.hasEndSeconds = true;
	return incident;
}

static SceneIncident breakdown(const char* id, const char* target) {
	SceneIncident incident;
	incident.id = id;
	incident.type = "train_breakdown";
	incident.target = target;
	incident.startSeconds = 100.0;
	incident.endSeconds = 200.0;
	incident.hasEndSeconds = true;
	return incident;
}

// A case of one hour with a signal, two blocks and two services. Service s1 runs every 20 minutes and s2 once; a block
// is also called s2. The scenario base is the default and holds a signal failure and a breakdown of the second run of s1
// with a speed cap. The scenario alt holds one breakdown that is not limited to a run.
static SceneModel incidentScene() {
	SceneModel scene;
	scene.name = "Incidents";
	scene.settings.hasDuration = true;
	scene.settings.durationSeconds = 3600.0;
	SceneSignal signal;
	signal.id = "sig1";
	sceneSignals(scene) = {signal};
	SceneBlock first;
	first.id = "blk1";
	SceneBlock second;
	second.id = "s2";
	scene.blocks = {first, second};
	SceneService repeated = service("s1");
	repeated.hasRepeat = true;
	repeated.headwaySeconds = 1200.0;
	scene.services = {repeated, service("s2")};
	SceneScenario base;
	base.id = "base";
	base.name = "Baseline";
	SceneIncident capped = breakdown("i2", "s1");
	capped.hasOccurrence = true;
	capped.occurrence = 2;
	capped.hasReducedSpeed = true;
	capped.reducedSpeedKmh = 30.0;
	base.incidents = {signalFailure("i1", "sig1"), capped};
	SceneScenario alt;
	alt.id = "alt";
	alt.name = "Alternative";
	alt.incidents = {breakdown("i3", "s2")};
	scene.scenarios = {base, alt};
	scene.defaultScenarioId = "base";
	return scene;
}

using Log = std::vector<std::string>;

// The order in which a panel and its document report an edit.
struct Events {
	Log log;
	SceneDocument::ChangeKinds kinds;

	Events(IncidentPanel& panel, SceneDocument& document) {
		QObject::connect(&document, &SceneDocument::changed, &panel, [this](SceneDocument::ChangeKinds changedKinds) {
			log.push_back("documentChanged");
			kinds = changedKinds;
		});
		QObject::connect(&panel, &IncidentPanel::changed, &panel, [this] { log.push_back("panelChanged"); });
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

// The text of the tip that the combo shows when the pointer rests on it.
static QString shownTip(QComboBox* combo) {
	QHelpEvent help(QEvent::ToolTip, QPoint(4, 4), combo->mapToGlobal(QPoint(4, 4)));
	QApplication::sendEvent(combo, &help);
	const QString text = QToolTip::text();
	QToolTip::hideText();
	return text;
}

// The position of the choice of the target `id` and `occurrence`, or -1.
static int choice(const QComboBox* combo, const char* id, int occurrence) {
	for (int index = 0; index < combo->count(); ++index) {
		if (combo->itemData(index).toString() == id && combo->itemData(index, Qt::UserRole + 1).toInt() == occurrence)
			return index;
	}
	return -1;
}

// The text field of a number field.
// The texts of the entries of target choices.
static QStringList texts(const IncidentTargetChoices& choices) {
	QStringList result;
	for (const IncidentTargetChoice& entry : choices.entries)
		result << entry.text;
	return result;
}

// The run of each entry of target choices, -1 for an entry without one.
static QList<int> runs(const IncidentTargetChoices& choices) {
	QList<int> result;
	for (const IncidentTargetChoice& entry : choices.entries)
		result << entry.occurrence.value_or(-1);
	return result;
}

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

static const Log edit({"documentChanged", "panelChanged"});

// A panel with prompts and a run selection of its own on a document that shows a scene, with the scenario base selected,
// and the events of both.
struct IncidentFixture {
	SceneDocument document;
	IncidentPanel panel;
	FakePrompts prompts;
	RunSelection selection;
	Events events{panel, document};

	explicit IncidentFixture(const SceneModel& scene = incidentScene()) {
		panel.setPrompts(&prompts);
		panel.setRunSelection(&selection);
		panel.setDocument(&document);
		document.replace(scene, false);
		selection.selectScenario("base");
		panel.refresh();
	}

	// The panel is hidden while the fixture is whole. A panel destroyed while shown loses the focus in its QWidget
	// destructor, and a field that had the focus then calls a slot of the panel whose own part is already gone.
	~IncidentFixture() { panel.hide(); }
};

class TestIncidentPanel : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void addsIncidentWithTarget();
	void namesTypes();
	void keepsUntouchedNumbers();
	void showsNothingWithoutScene();
	void showsIncidentsOfScenario();
	void showsBreakdown();
	void typeDecidesTargets();
	void editsAddsAndDeletesIncidents();
	void showsOtherScenario();
	void commitsTextOfFocusedField();
	void showsAnotherScene();
	void detachedPanelIgnoresDocument();
	void targetsFollowDocument();
	void keepsTypedTextWhenTrackIsAdded();
	void forgetsDeletedDocument();
	void emptyStateFollowsSelection();
	void checkBoxStandsBeforeItsField();
	void emptyStateWithoutScenario();
	void addsScenarioOnlyWithAnEdit();
	void pickButtonStandsByTarget();
	void picksTargetOnNetworkView();
	void takesPickedTarget();
	void givesNewIncidentTarget();
	void takesOccurrenceThatComboDoesNotList();
	void namesTargetsByLabels();
	void choicesListSignalFailureTargetsOnce();
	void choicesListRunsOfBreakdown();
	void choicesStopAtRunLimit();
	void choicesNameRepeatedTexts();
	void fieldsGrowInFormOfMacOs();
};

void TestIncidentPanel::initTestCase() {
	// The icon of the pick button is a resource of the application, which this test does not have.
	QLoggingCategory::setFilterRules(QStringLiteral("qt.svg.warning=false"));
}

// The window adds an incident with the target of a signal or a train of the network view: the panel takes that target
// instead of the default one, in the one edit that adds the incident, and selects the new row.
void TestIncidentPanel::addsIncidentWithTarget() {
	IncidentFixture f;
	SceneDocument& document = f.document;
	IncidentPanel& panel = f.panel;
	Events& events = f.events;
	panel.listWidget()->setCurrentRow(0);
	int defaultAsked = 0;
	panel.setDefaultTarget([&defaultAsked] {
		++defaultAsked;
		return IncidentTarget{"signal_failure", "sig1", 0};
	});
	const std::vector<SceneIncident>& incidents = document.model().scenarios[0].incidents;
	const std::size_t countBefore = incidents.size();
	// An incident added with a target has that target and its type in one edit, and its row is selected.
	QVERIFY(panel.addIncident(IncidentTarget{"train_breakdown", "s1", 2}));
	QCOMPARE(incidents.size(), countBefore + 1);
	QCOMPARE(incidents.back().id, "new_incident");
	QCOMPARE(incidents.back().type, "train_breakdown");
	QCOMPARE(incidents.back().target, "s1");
	QVERIFY(incidents.back().hasOccurrence);
	QCOMPARE(incidents.back().occurrence, 2);
	QVERIFY(incidents.back().hasEndSeconds);
	QCOMPARE_EQ(incidents.back().startSeconds, 0.0);
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Add incident");
	QCOMPARE(defaultAsked, 0);
	QCOMPARE(panel.listWidget()->currentRow(), static_cast<int>(countBefore));
	QVERIFY(panel.selectedIncident() == &document.mutableModel().scenarios[0].incidents.back());
	QCOMPARE(panel.targetCombo()->currentIndex(), choice(panel.targetCombo(), "s1", 2));
	events.log.clear();
	// An incident added with a block is a signal failure of it.
	QVERIFY(panel.addIncident(IncidentTarget{"signal_failure", "blk1", 0}));
	QCOMPARE(incidents.back().type, "signal_failure");
	QCOMPARE(incidents.back().target, "blk1");
	QVERIFY(!incidents.back().hasOccurrence);
	QCOMPARE(events.log, edit);
	QCOMPARE(defaultAsked, 0);
	QCOMPARE(panel.listWidget()->currentRow(), static_cast<int>(countBefore) + 1);
	events.log.clear();
	// A target that the combo does not offer adds nothing.
	QVERIFY(!panel.addIncident(IncidentTarget{"signal_failure", "missing", 0}));
	QVERIFY(!panel.addIncident(IncidentTarget{"train_breakdown", "s1", 4}));
	QVERIFY(!panel.addIncident(IncidentTarget()));
	QCOMPARE(incidents.size(), countBefore + 2);
	QVERIFY(events.log.empty());
	document.setEditable(false);
	// Nothing is added while a run uses the scene.
	QVERIFY(!panel.addIncident(IncidentTarget{"signal_failure", "sig1", 0}));
	QCOMPARE(incidents.size(), countBefore + 2);
	QVERIFY(events.log.empty());
}

// The type combo shows the names of the types and keeps the type of the file as the data of each item: choosing an item
// by its name stores that type, and the file is written as before.
void TestIncidentPanel::namesTypes() {
	IncidentFixture f;
	SceneDocument& document = f.document;
	IncidentPanel& panel = f.panel;
	QComboBox* type = panel.typeCombo();
	// The types are shown by name, with the type of the file as data.
	QCOMPARE(choices(type), QStringList({"Signal failure", "Train breakdown"}));
	QCOMPARE(type->itemData(0).toString(), "signal_failure");
	QCOMPARE(type->itemData(1).toString(), "train_breakdown");
	QCOMPARE(type->currentText(), "Signal failure");
	const nlohmann::json before = encodeScenarios(document.model());
	type->setCurrentIndex(type->findText("Train breakdown"));
	const nlohmann::json changed = encodeScenarios(document.model());
	// Choosing a type by its name stores the type of the file.
	QCOMPARE(document.model().scenarios[0].incidents[0].type, "train_breakdown");
	QVERIFY(changed["scenarios"][0]["incidents"][0]["type"] == "train_breakdown");
	QCOMPARE(f.events.log, edit);
	f.events.log.clear();
	type->setCurrentIndex(type->findText("Signal failure"));
	panel.targetCombo()->setCurrentIndex(panel.targetCombo()->findData("sig1"));
	// Choosing the first type and target again writes the file as before.
	QCOMPARE(document.model().scenarios[0].incidents[0].type, "signal_failure");
	QVERIFY(encodeScenarios(document.model()) == before);
	panel.listWidget()->setCurrentRow(1);
	// A breakdown shows the name of its type.
	QCOMPARE(type->currentText(), "Train breakdown");
	QCOMPARE(type->currentData().toString(), "train_breakdown");
}

// Save, Undo and validation commit the field that is being edited. A start and an end with fractions of a second, and a
// speed cap above the range of its field, reach the saved file as stored while the user does not edit them, even when
// the field has the focus. The End field shows the end while its box is off, and visiting it does not switch the end on.
void TestIncidentPanel::keepsUntouchedNumbers() {
	SceneModel scene = incidentScene();
	SceneIncident& failure = scene.scenarios[0].incidents[0];
	failure.startSeconds = 10.5;
	failure.endSeconds = 20.75;
	SceneIncident& capped = scene.scenarios[0].incidents[1];
	capped.startSeconds = 100.25;
	capped.hasEndSeconds = false;
	capped.endSeconds = 0.0;
	capped.reducedSpeedKmh = 1200.0;
	IncidentFixture f(scene);
	SceneDocument& document = f.document;
	IncidentPanel& panel = f.panel;
	Events& events = f.events;
	// A start and an end are shown with their fractions.
	QCOMPARE_EQ(panel.startSecondsEdit()->value(), 10.5);
	QCOMPARE(panel.startSecondsEdit()->text(), "10.5");
	QCOMPARE_EQ(panel.endSecondsEdit()->value(), 20.75);
	QCOMPARE(panel.endSecondsEdit()->text(), "20.75");

	const nlohmann::json saved = encodeScenarios(document.model());
	const quint64 revision = document.revision();
	panel.commitPendingEditors();
	panel.show();
	panel.activateWindow();
	for (QDoubleSpinBox* field : {panel.startSecondsEdit(), panel.endSecondsEdit()}) {
		QVERIFY(giveFocus(field));
		panel.commitPendingEditors();
	}
	panel.listWidget()->setCurrentRow(1);
	// The End field of a breakdown without an end can be edited.
	QVERIFY(!panel.hasEndSecondsCheck()->isChecked());
	QVERIFY(panel.endSecondsEdit()->isEnabled());
	QVERIFY(panel.reducedSpeedKmhEdit()->isEnabled());
	for (QDoubleSpinBox* field : {panel.startSecondsEdit(), panel.endSecondsEdit(), panel.reducedSpeedKmhEdit()}) {
		QVERIFY(giveFocus(field));
		panel.commitPendingEditors();
	}
	QVERIFY(giveFocus(panel.idEdit()));
	panel.commitStartSeconds();
	panel.commitEndSeconds();
	panel.commitReducedSpeed();
	const std::vector<SceneIncident>& kept = document.model().scenarios[0].incidents;
	// A start of 10.5 s, an end of 20.75 s, a breakdown without an end and a speed cap of 1200 km/h are saved as stored.
	QCOMPARE_EQ(kept[0].startSeconds, 10.5);
	QCOMPARE_EQ(kept[0].endSeconds, 20.75);
	QCOMPARE_EQ(kept[1].startSeconds, 100.25);
	QVERIFY(!kept[1].hasEndSeconds);
	QCOMPARE_EQ(kept[1].reducedSpeedKmh, 1200.0);
	QVERIFY(encodeScenarios(document.model()) == saved);
	// Visiting the number fields and committing them adds no Undo step.
	QVERIFY(events.log.empty());
	QCOMPARE(document.revision(), revision);
	QVERIFY(!document.isDirty());

	QVERIFY(giveFocus(panel.endSecondsEdit()));
	spinText(panel.endSecondsEdit())->setText("130.5");
	panel.commitPendingEditors();
	// An end that is being typed while its box is off is stored with its fraction and switches the end on.
	QVERIFY(kept[1].hasEndSeconds);
	QCOMPARE_EQ(kept[1].endSeconds, 130.5);
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Change incident end");
	QVERIFY(giveFocus(panel.startSecondsEdit()));
	typeNumber(panel.startSecondsEdit(), "110.75");
	panel.commitPendingEditors();
	// A start that is typed is stored with its fraction, and the speed cap keeps its value.
	QCOMPARE_EQ(kept[1].startSeconds, 110.75);
	QCOMPARE_EQ(kept[1].reducedSpeedKmh, 1200.0);
	QCOMPARE(document.revision(), revision + 2);
}

void TestIncidentPanel::showsNothingWithoutScene() {
	IncidentPanel panel;
	// A panel without a document shows nothing and offers nothing.
	QCOMPARE(panel.listWidget()->count(), 0);
	QVERIFY(!panel.listWidget()->isEnabled());
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY(!panel.idEdit()->isEnabled());
	QVERIFY(!panel.targetCombo()->isEnabled());
	QCOMPARE(panel.targetCombo()->count(), 1);
	SceneDocument document;
	panel.setDocument(&document);
	panel.addIncident();
	panel.duplicateIncident();
	panel.deleteIncident();
	panel.commitIdEdit();
	panel.commitPendingEditors();
	// A panel stays disabled while its document holds no scene, and its actions change nothing.
	QVERIFY(!panel.addButton()->isEnabled());
	QCOMPARE(document.revision(), quint64(0));
	QVERIFY(!document.isDirty());
	QVERIFY(document.model().scenarios.empty());
}

void TestIncidentPanel::showsIncidentsOfScenario() {
	IncidentFixture f;
	IncidentPanel& panel = f.panel;
	// The incidents of the selected scenario are listed, and the first is selected.
	QCOMPARE(items(panel.listWidget()), QStringList({"i1: Signal failure", "i2: Train breakdown #2, 30 km/h"}));
	QCOMPARE(panel.listWidget()->currentRow(), 0);
	QVERIFY(panel.listWidget()->isEnabled());
	QVERIFY(panel.addButton()->isEnabled());
	QVERIFY(panel.duplicateButton()->isEnabled());
	QVERIFY(panel.deleteButton()->isEnabled());
	// The selected signal failure shows its fields, and the fields of a breakdown wait.
	QCOMPARE(panel.idEdit()->text(), "i1");
	QCOMPARE(panel.typeCombo()->currentData().toString(), "signal_failure");
	QCOMPARE(panel.startSecondsEdit()->text(), "10");
	QCOMPARE(panel.endSecondsEdit()->text(), "20");
	QVERIFY(panel.hasEndSecondsCheck()->isChecked());
	QVERIFY(!panel.hasReducedSpeedCheck()->isEnabled());
	QVERIFY(!panel.reducedSpeedKmhEdit()->isEnabled());
	QVERIFY(!panel.terminateAtDestinationCheck()->isEnabled());
	QVERIFY(!panel.hasReducedSpeedCheck()->isChecked());
	// The targets of a signal failure are the signals and the blocks.
	QCOMPARE(choices(panel.targetCombo()), QStringList({"Choose target", "sig1", "blk1", "s2"}));
	QCOMPARE(panel.targetCombo()->currentText(), "sig1");
	QVERIFY(panel.targetCombo()->isEnabled());
	// Showing a scene edits nothing.
	QVERIFY(f.events.log.empty());
	QVERIFY(!f.document.isDirty());
	QCOMPARE(f.document.revision(), quint64(1));
	QVERIFY(!f.selection.isScenarioModified("base"));
}

void TestIncidentPanel::showsBreakdown() {
	IncidentFixture f;
	IncidentPanel& panel = f.panel;
	panel.listWidget()->setCurrentRow(1);
	// A breakdown shows its speed cap.
	QCOMPARE(panel.idEdit()->text(), "i2");
	QCOMPARE(panel.typeCombo()->currentData().toString(), "train_breakdown");
	QVERIFY(panel.hasReducedSpeedCheck()->isEnabled());
	QVERIFY(panel.hasReducedSpeedCheck()->isChecked());
	QVERIFY(panel.reducedSpeedKmhEdit()->isEnabled());
	QCOMPARE_EQ(panel.reducedSpeedKmhEdit()->value(), 30.0);
	QVERIFY(panel.terminateAtDestinationCheck()->isEnabled());
	QVERIFY(!panel.terminateAtDestinationCheck()->isChecked());
	// The targets of a breakdown are the occurrences of the services, and the occurrence of the incident is chosen.
	QCOMPARE(choices(panel.targetCombo()), QStringList({"Choose target", "s1-1 (route r1, +0 s)", "s1-2 (route r1, +1200 s)", "s1-3 (route r1, +2400 s)", "s2 (route r1, +0 s)"}));
	QCOMPARE(panel.targetCombo()->currentIndex(), 2);
	QCOMPARE(panel.targetCombo()->itemData(2, Qt::ToolTipRole).toString(), "s1-2 (route r1, +1200 s)\nService s1, run 2");
	QVERIFY2(f.events.log.empty(), "selecting an incident edits nothing");
}

// The type decides which targets are valid.
void TestIncidentPanel::typeDecidesTargets() {
	IncidentFixture f;
	SceneDocument& document = f.document;
	IncidentPanel& panel = f.panel;
	const SceneIncident& incident = document.model().scenarios[0].incidents[0];
	panel.listWidget()->setCurrentRow(0);
	panel.typeCombo()->setCurrentIndex(panel.typeCombo()->findData("train_breakdown"));
	// A breakdown has no signal as its target, and the targets are the services.
	QCOMPARE(incident.type, "train_breakdown");
	QVERIFY(incident.target.empty());
	QCOMPARE(panel.targetCombo()->currentIndex(), 0);
	QVERIFY(panel.hasReducedSpeedCheck()->isEnabled());
	QCOMPARE(choices(panel.targetCombo()).size(), qsizetype(5));
	QCOMPARE(panel.listWidget()->item(0)->text(), "i1: Train breakdown");
	QCOMPARE(f.events.log, edit);
	QCOMPARE(f.events.kinds, SceneDocument::ChangeKinds(SceneDocument::Scenarios));
	QCOMPARE(document.lastChangeLabel(), "Change incident type");
	QVERIFY(f.selection.isScenarioModified("base"));
	panel.targetCombo()->setCurrentIndex(choice(panel.targetCombo(), "s1", 3));
	// A chosen occurrence is the target of the breakdown.
	QCOMPARE(incident.target, "s1");
	QVERIFY(incident.hasOccurrence);
	QCOMPARE(incident.occurrence, 3);
	QCOMPARE(document.lastChangeLabel(), "Change incident target");
	panel.targetCombo()->setCurrentIndex(choice(panel.targetCombo(), "s2", 1));
	panel.typeCombo()->setCurrentIndex(panel.typeCombo()->findData("signal_failure"));
	// A target that is valid for both types stays, and a signal failure has no occurrence.
	QCOMPARE(incident.type, "signal_failure");
	QCOMPARE(incident.target, "s2");
	QVERIFY(!incident.hasOccurrence);
	QCOMPARE(incident.occurrence, 1);
	QCOMPARE(panel.targetCombo()->currentText(), "s2");
	QVERIFY(!panel.hasReducedSpeedCheck()->isEnabled());
	panel.targetCombo()->setCurrentIndex(choice(panel.targetCombo(), "sig1", 0));
	panel.typeCombo()->setCurrentIndex(panel.typeCombo()->findData("train_breakdown"));
	QVERIFY2(incident.target.empty(), "a signal is dropped as the target of a breakdown");
	panel.typeCombo()->setCurrentIndex(panel.typeCombo()->findData("signal_failure"));
	// A signal failure starts without a target after the type changes.
	QVERIFY(incident.target.empty());
	QCOMPARE(panel.targetCombo()->currentIndex(), 0);
	panel.targetCombo()->setCurrentIndex(choice(panel.targetCombo(), "sig1", 0));
	// A signal is the target of a signal failure.
	QCOMPARE(incident.target, "sig1");
	QVERIFY(!incident.hasOccurrence);
	panel.targetCombo()->setCurrentIndex(0);
	QVERIFY2(incident.target.empty(), "Choose target clears the target");
}

// A breakdown that becomes a signal failure and a breakdown again, its fields and its id, then Add, Duplicate and
// Delete, which see that id, and a run, which sees the incidents that are left.
void TestIncidentPanel::editsAddsAndDeletesIncidents() {
	IncidentFixture f;
	SceneDocument& document = f.document;
	IncidentPanel& panel = f.panel;
	FakePrompts& prompts = f.prompts;
	RunSelection& selection = f.selection;
	Events& events = f.events;

	// A breakdown that loses its speed cap when it becomes a signal failure.
	panel.listWidget()->setCurrentRow(1);
	panel.typeCombo()->setCurrentIndex(panel.typeCombo()->findData("signal_failure"));
	const SceneIncident& converted = document.model().scenarios[0].incidents[1];
	// A signal failure has none of the fields of a breakdown.
	QCOMPARE(converted.type, "signal_failure");
	QVERIFY(!converted.hasReducedSpeed);
	QCOMPARE_EQ(converted.reducedSpeedKmh, 0.0);
	QVERIFY(!converted.hasOccurrence);
	QCOMPARE(converted.occurrence, 1);
	QVERIFY(!converted.terminateAtDestination);
	QVERIFY(!panel.hasReducedSpeedCheck()->isChecked());
	QVERIFY(!panel.reducedSpeedKmhEdit()->isEnabled());
	QVERIFY(converted.target.empty());
	panel.typeCombo()->setCurrentIndex(panel.typeCombo()->findData("train_breakdown"));
	// The speed cap of a new breakdown is off and offers 40 km/h.
	QVERIFY(!panel.hasReducedSpeedCheck()->isChecked());
	QVERIFY(panel.hasReducedSpeedCheck()->isEnabled());
	QVERIFY(!panel.reducedSpeedKmhEdit()->isEnabled());
	QCOMPARE_EQ(panel.reducedSpeedKmhEdit()->value(), 40.0);

	// The speed cap.
	events.log.clear();
	panel.hasReducedSpeedCheck()->setChecked(true);
	// Switching the speed cap on writes the speed that is shown.
	QVERIFY(converted.hasReducedSpeed);
	QCOMPARE_EQ(converted.reducedSpeedKmh, 40.0);
	QVERIFY(panel.reducedSpeedKmhEdit()->isEnabled());
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Change incident reduced speed");
	panel.reducedSpeedKmhEdit()->setValue(45.5);
	panel.commitReducedSpeed();
	// A speed is written to the document and shown in the list.
	QCOMPARE_EQ(converted.reducedSpeedKmh, 45.5);
	QVERIFY(converted.hasReducedSpeed);
	QCOMPARE(panel.listWidget()->item(1)->text(), "i2: Train breakdown, 45.5 km/h");
	events.log.clear();
	panel.commitReducedSpeed();
	QVERIFY2(events.log.empty(), "an unchanged speed does not touch the document");
	panel.hasReducedSpeedCheck()->setChecked(false);
	// Switching the speed cap off removes it, and the field offers 40 km/h again.
	QVERIFY(!converted.hasReducedSpeed);
	QCOMPARE_EQ(converted.reducedSpeedKmh, 0.0);
	QVERIFY(!panel.reducedSpeedKmhEdit()->isEnabled());
	QCOMPARE_EQ(panel.reducedSpeedKmhEdit()->value(), 40.0);
	panel.refresh();
	QVERIFY2(panel.listWidget()->item(1)->text() == "i2: Train breakdown", "the list shows the incident without a speed cap");

	// The end of the incident.
	events.log.clear();
	panel.hasEndSecondsCheck()->setChecked(false);
	// A breakdown can go on without a recovery end.
	QVERIFY(!converted.hasEndSeconds);
	QCOMPARE_EQ(converted.endSeconds, 0.0);
	QCOMPARE(panel.endSecondsEdit()->text(), "0");
	QCOMPARE(events.log, edit);
	QVERIFY(!panel.hasEndSecondsCheck()->isChecked());
	typeNumber(panel.endSecondsEdit(), "500");
	panel.commitEndSeconds();
	// A recovery end is written to the document.
	QVERIFY(converted.hasEndSeconds);
	QCOMPARE_EQ(converted.endSeconds, 500.0);
	QVERIFY(panel.hasEndSecondsCheck()->isChecked() == false);
	panel.refresh();
	// The recovery end is shown again.
	QVERIFY(panel.hasEndSecondsCheck()->isChecked());
	QCOMPARE(panel.endSecondsEdit()->text(), "500");
	events.log.clear();
	typeNumber(panel.endSecondsEdit(), "");
	panel.commitEndSeconds();
	// A blank recovery end shows the stored one again.
	QCOMPARE_EQ(converted.endSeconds, 500.0);
	QVERIFY(converted.hasEndSeconds);
	QCOMPARE(panel.endSecondsEdit()->text(), "500");
	QVERIFY(events.log.empty());
	panel.hasEndSecondsCheck()->setChecked(true);
	// The recovery end switches on.
	QVERIFY(converted.hasEndSeconds);
	QVERIFY(panel.hasEndSecondsCheck()->isChecked());
	typeNumber(panel.startSecondsEdit(), "250");
	panel.commitStartSeconds();
	// A start is written to the document.
	QCOMPARE_EQ(converted.startSeconds, 250.0);
	QCOMPARE(document.lastChangeLabel(), "Change incident start");
	events.log.clear();
	panel.commitStartSeconds();
	QVERIFY2(events.log.empty(), "an unchanged start does not touch the document");
	panel.terminateAtDestinationCheck()->setChecked(true);
	// A breakdown can end at the destination.
	QVERIFY(converted.terminateAtDestination);
	QCOMPARE(events.log, edit);

	// Ids.
	events.log.clear();
	prompts.errorCount = 0;
	panel.idEdit()->setText("  i2b  ");
	panel.commitIdEdit();
	// A new id is written to the document, trimmed, and the list shows it.
	QCOMPARE(converted.id, "i2b");
	QCOMPARE(panel.listWidget()->item(1)->text(), "i2b: Train breakdown");
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Rename incident");
	events.log.clear();
	panel.idEdit()->setText("i3");
	panel.commitIdEdit();
	// An id that an incident of another scenario has is refused, and the field shows the old id again.
	QCOMPARE(prompts.errorCount, 1);
	QCOMPARE(prompts.errorTitle, "Duplicate Incident ID");
	QCOMPARE(panel.idEdit()->text(), "i2b");
	QCOMPARE(converted.id, "i2b");
	QVERIFY(events.log.empty());
	panel.idEdit()->setText(" ");
	panel.commitIdEdit();
	// An empty id is refused, and the field shows the old id again.
	QCOMPARE(prompts.errorCount, 2);
	QCOMPARE(prompts.errorTitle, "Invalid Incident ID");
	QCOMPARE(panel.idEdit()->text(), "i2b");
	QVERIFY(events.log.empty());
	panel.idEdit()->setText("i2b");
	panel.commitIdEdit();
	// An unchanged id does not touch the document.
	QCOMPARE(prompts.errorCount, 2);
	QVERIFY(events.log.empty());

	// Add, duplicate and delete.
	document.markSaved();
	selection.clearModifiedScenarios();
	events.log.clear();
	panel.addIncident();
	const SceneIncident& added = document.model().scenarios[0].incidents.back();
	// An incident is added to the selected scenario, selected and shown.
	QCOMPARE(document.model().scenarios[0].incidents.size(), std::size_t(3));
	QCOMPARE(added.id, "new_incident");
	QCOMPARE(added.type, "signal_failure");
	QVERIFY(added.hasEndSeconds);
	QCOMPARE(panel.listWidget()->count(), 3);
	QCOMPARE(panel.listWidget()->currentRow(), 2);
	QCOMPARE(panel.idEdit()->text(), "new_incident");
	QVERIFY(document.isDirty());
	QVERIFY(selection.isScenarioModified("base"));
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Add incident");
	panel.addIncident();
	QVERIFY2(document.model().scenarios[0].incidents.back().id == "new_incident_2", "the id of another new incident is unique");
	events.log.clear();
	panel.listWidget()->setCurrentRow(0);
	panel.duplicateIncident();
	// A copy is put after the incident, with an id of its own, and selected.
	QCOMPARE(document.model().scenarios[0].incidents.size(), std::size_t(5));
	QCOMPARE(document.model().scenarios[0].incidents[1].id, "i1_copy");
	QCOMPARE(document.model().scenarios[0].incidents[1].target, "sig1");
	QCOMPARE(panel.listWidget()->currentRow(), 1);
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Duplicate incident");
	prompts.confirmCount = 0;
	events.log.clear();
	panel.deleteIncident();
	// Declining the question keeps the incident.
	QCOMPARE(prompts.confirmCount, 1);
	QCOMPARE(prompts.confirmQuestion, "Delete incident 'i1_copy'?");
	QCOMPARE(document.model().scenarios[0].incidents.size(), std::size_t(5));
	QVERIFY(events.log.empty());
	prompts.confirmAnswer = true;
	panel.deleteIncident();
	// A deleted incident is removed.
	QCOMPARE(document.model().scenarios[0].incidents.size(), std::size_t(4));
	QCOMPARE(document.model().scenarios[0].incidents[1].id, "i2b");
	QCOMPARE(panel.listWidget()->count(), 4);
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Delete incident");

	// The scene is in use by a run.
	document.markSaved();
	events.log.clear();
	panel.listWidget()->setCurrentRow(0);
	document.setEditable(false);
	panel.refresh();
	// Nothing can be done with the incidents, and the fields are empty, while a run uses the scene.
	QVERIFY(!panel.listWidget()->isEnabled());
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY(!panel.duplicateButton()->isEnabled());
	QVERIFY(!panel.deleteButton()->isEnabled());
	QVERIFY(!panel.idEdit()->isEnabled());
	QVERIFY(!panel.typeCombo()->isEnabled());
	QVERIFY(!panel.targetCombo()->isEnabled());
	QVERIFY(!panel.startSecondsEdit()->isEnabled());
	QVERIFY(!panel.endSecondsEdit()->isEnabled());
	QVERIFY(!panel.hasEndSecondsCheck()->isEnabled());
	QVERIFY(!panel.hasReducedSpeedCheck()->isEnabled());
	QVERIFY(!panel.terminateAtDestinationCheck()->isEnabled());
	QVERIFY(panel.idEdit()->text().isEmpty());
	panel.idEdit()->setText("while-running");
	spinText(panel.startSecondsEdit())->setText("1");
	panel.commitPendingEditors();
	panel.commitIdEdit();
	panel.addIncident();
	panel.duplicateIncident();
	panel.deleteIncident();
	panel.typeCombo()->setCurrentIndex(panel.typeCombo()->findData("train_breakdown"));
	// Pending text and actions change nothing while a run uses the scene.
	QCOMPARE(document.model().scenarios[0].incidents[0].id, "i1");
	QCOMPARE(document.model().scenarios[0].incidents.size(), std::size_t(4));
	QVERIFY(events.log.empty());
	QVERIFY(!document.isDirty());
	document.setEditable(true);
	panel.refresh();
	// The incidents can be edited again after the run.
	QCOMPARE(panel.idEdit()->text(), "i1");
	QVERIFY(panel.listWidget()->isEnabled());
}

void TestIncidentPanel::showsOtherScenario() {
	IncidentFixture f;
	IncidentPanel& panel = f.panel;
	RunSelection& selection = f.selection;
	selection.selectScenario("alt");
	panel.refresh();
	// The incidents of the selected scenario are shown, and a breakdown of every occurrence is a historical scope.
	QCOMPARE(items(panel.listWidget()), QStringList({"i3: Train breakdown"}));
	QCOMPARE(panel.idEdit()->text(), "i3");
	QCOMPARE(panel.targetCombo()->currentIndex(), 1);
	QVERIFY(panel.targetCombo()->currentText().startsWith("All runs of s2"));
	f.events.log.clear();
	selection.clearModifiedScenarios();
	typeNumber(panel.startSecondsEdit(), "5");
	panel.commitStartSeconds();
	// An edit marks the selected scenario.
	QCOMPARE_EQ(f.document.model().scenarios[1].incidents[0].startSeconds, 5.0);
	QVERIFY(selection.isScenarioModified("alt"));
	QVERIFY(!selection.isScenarioModified("base"));
}

// Pending edits: a number field commits the text in it only while it has the focus.
void TestIncidentPanel::commitsTextOfFocusedField() {
	IncidentFixture f;
	SceneDocument& document = f.document;
	IncidentPanel& panel = f.panel;
	Events& events = f.events;
	document.markSaved();
	events.log.clear();
	panel.listWidget()->setCurrentRow(0);
	panel.show();
	panel.activateWindow();
	spinText(panel.startSecondsEdit())->setText("77");
	panel.commitPendingEditors();
	// Unfinished text in a field without the focus is not pending.
	QCOMPARE_EQ(document.model().scenarios[0].incidents[0].startSeconds, 10.0);
	QVERIFY(events.log.empty());
	QVERIFY(giveFocus(panel.startSecondsEdit()));
	panel.commitPendingEditors();
	// Unfinished text in the field with the focus is taken into the document.
	QCOMPARE_EQ(document.model().scenarios[0].incidents[0].startSeconds, 77.0);
	QCOMPARE(events.log, edit);
	events.log.clear();
	QVERIFY(giveFocus(panel.endSecondsEdit()));
	spinText(panel.endSecondsEdit())->setText("99");
	panel.commitPendingEditors();
	QVERIFY2(document.model().scenarios[0].incidents[0].endSeconds == 99.0, "an unfinished recovery end is taken in");
	panel.commitPendingEditors();
	QVERIFY2(events.log == edit, "committing again changes nothing");
}

// Another scene replaces unfinished text without an edit.
void TestIncidentPanel::showsAnotherScene() {
	IncidentFixture f;
	IncidentPanel& panel = f.panel;
	panel.idEdit()->setText("abandoned");
	SceneModel other = incidentScene();
	other.scenarios[0].incidents.resize(1);
	other.scenarios[0].incidents[0].id = "only";
	f.document.replace(other, false);
	// Opening another scene replaces unfinished text without an edit.
	QCOMPARE(items(panel.listWidget()), QStringList({"only: Signal failure"}));
	QCOMPARE(panel.idEdit()->text(), "only");
	QVERIFY(!f.document.isDirty());
}

void TestIncidentPanel::detachedPanelIgnoresDocument() {
	IncidentFixture f;
	f.panel.setDocument(nullptr);
	// A detached panel shows nothing.
	QCOMPARE(f.panel.listWidget()->count(), 0);
	QVERIFY(!f.panel.listWidget()->isEnabled());
	QCOMPARE(f.panel.targetCombo()->count(), 1);
	const quint64 revisionDetached = f.document.revision();
	f.panel.addIncident();
	f.document.replace(incidentScene(), false);
	// A detached panel neither edits nor follows the document.
	QCOMPARE(f.panel.listWidget()->count(), 0);
	QCOMPARE(f.document.revision(), revisionDetached + 1);
}

// The targets follow the document.
void TestIncidentPanel::targetsFollowDocument() {
	IncidentFixture f;
	SceneDocument& document = f.document;
	IncidentPanel& panel = f.panel;
	panel.listWidget()->setCurrentRow(1);
	QVERIFY2(panel.targetCombo()->count() == 5, "the breakdown can have four occurrences as its target");

	SceneModel& model = document.mutableModel();
	model.services.push_back(service("s3"));
	QVERIFY2(panel.targetCombo()->count() == 5, "a service that the document has not announced is not offered");
	document.markChanged(SceneDocument::Services, "Add service");
	// A service that is added is offered, and the target of the incident stays.
	QCOMPARE(panel.targetCombo()->count(), 6);
	QCOMPARE(choice(panel.targetCombo(), "s3", 1), 5);
	QCOMPARE(panel.targetCombo()->currentIndex(), 2);

	model.services[0].id = "s1b";
	model.scenarios[0].incidents[1].target = "s1b";
	document.markChanged(SceneDocument::Services | SceneDocument::Scenarios, "Rename service");
	// A renamed service is offered under its new id, and the target of the incident follows it.
	QCOMPARE(choice(panel.targetCombo(), "s1b", 2), 2);
	QCOMPARE(panel.targetCombo()->currentIndex(), 2);
	QCOMPARE(panel.targetCombo()->count(), 6);

	model.services.erase(model.services.begin() + 2);
	document.markChanged(SceneDocument::Services, "Delete service");
	QVERIFY2(panel.targetCombo()->count() == 5, "a service that is deleted is no longer offered");

	model.services.erase(model.services.begin());
	document.markChanged(SceneDocument::Services, "Delete service");
	// A target that is gone is shown as such and is not changed.
	QCOMPARE(panel.targetCombo()->currentText(), "s1b #2 (missing)");
	QCOMPARE(panel.targetCombo()->currentData().toString(), "s1b");
	QCOMPARE(document.model().scenarios[0].incidents[1].target, "s1b");

	model.settings.durationSeconds = 7200.0;
	model.services.insert(model.services.begin(), service("s1"));
	model.services[0].hasRepeat = true;
	model.services[0].headwaySeconds = 1200.0;
	document.markChanged(SceneDocument::CaseSettings, "Edit case settings");
	QVERIFY2(panel.targetCombo()->count() == 9, "a longer case has more occurrences to choose from");
	panel.setOccurrenceDuration([] { return 2400.0; });
	document.markChanged(SceneDocument::CaseSettings, "Edit case settings");
	QVERIFY2(panel.targetCombo()->count() == 5, "the duration that the window gives counts the occurrences");
	panel.setOccurrenceDuration({});

	panel.listWidget()->setCurrentRow(0);
	sceneSignals(model).push_back(SceneSignal{"sig2", ""});
	document.markChanged(SceneDocument::Infrastructure, "Add signal");
	QVERIFY2(panel.targetCombo()->findText("sig2") >= 0, "a signal that is added is offered to a signal failure");
	model.blocks.erase(model.blocks.begin());
	document.markChanged(SceneDocument::Infrastructure, "Delete block");
	// A block that is deleted is no longer offered.
	QCOMPARE_LT(panel.targetCombo()->findText("blk1"), 0);
	QCOMPARE_GE(panel.targetCombo()->findText("sig2"), 0);
	sceneSignals(model).erase(sceneSignals(model).begin());
	document.markChanged(SceneDocument::Infrastructure, "Delete signal");
	// A signal that is gone is shown as the removed target of the signal failure.
	QCOMPARE(panel.targetCombo()->currentText(), "sig1 (missing)");
	QCOMPARE(panel.targetCombo()->currentData().toString(), "sig1");
	sceneSignals(model).push_back(SceneSignal{"sig3", ""});
	document.markChanged(SceneDocument::Scenarios, "Edit scenario");
	QVERIFY2(panel.targetCombo()->findText("sig3") < 0, "the targets stay as they are when only scenarios change");
}

// Text that is still being typed stays when the infrastructure panel adds a track, which shows the incidents again: the
// panel takes the text into the document first, as edits of its own after the other one. The labels and kinds of the
// changes are kept in their order, by a connection made before those of the panels, as the window keeps its undo steps.
void TestIncidentPanel::keepsTypedTextWhenTrackIsAdded() {
	SceneDocument document;
	Log labels;
	std::vector<SceneDocument::ChangeKinds> kinds;
	QObject::connect(&document, &SceneDocument::changed, &document, [&](SceneDocument::ChangeKinds changedKinds) {
		labels.push_back(document.lastChangeLabel().toStdString());
		kinds.push_back(changedKinds);
	});
	FakePrompts prompts;
	RunSelection selection;
	IncidentPanel panel;
	InfrastructurePanel infrastructure;
	panel.setPrompts(&prompts);
	panel.setRunSelection(&selection);
	panel.setDocument(&document);
	infrastructure.setPrompts(&prompts);
	for (std::unique_ptr<InfrastructureFacet>& facet : makeTopologyFacets())
		infrastructure.setFacet(std::move(facet));
	infrastructure.setDocument(&document);
	document.replace(incidentScene(), false);
	selection.selectScenario("base");
	panel.refresh();
	// The window shows the infrastructure of a scene that it opens.
	infrastructure.refresh();
	infrastructure.facetCombo()->setCurrentIndex(infrastructure.facetCombo()->findData("tracks"));
	panel.listWidget()->setCurrentRow(0);
	panel.show();
	// The panel is hidden before it is destroyed, also when a check fails.
	const auto hide = qScopeGuard([&panel] { panel.hide(); });
	panel.activateWindow();
	panel.idEdit()->setText("i1_typed");
	QVERIFY(giveFocus(panel.startSecondsEdit()));
	spinText(panel.startSecondsEdit())->setText("77");
	infrastructure.addButton()->click();
	// The track is added, and the typed id and start are taken in after it, each as an edit of its own.
	QCOMPARE(document.model().tracks.size(), std::size_t(1));
	const SceneIncident& incident = document.model().scenarios[0].incidents[0];
	QCOMPARE(incident.id, "i1_typed");
	QCOMPARE_EQ(incident.startSeconds, 77.0);
	QCOMPARE(labels, Log({"Add track", "Rename incident", "Change incident start"}));
	QCOMPARE(kinds[0], SceneDocument::ChangeKinds(SceneDocument::Infrastructure));
	QCOMPARE(kinds[1], SceneDocument::ChangeKinds(SceneDocument::Scenarios));
	QCOMPARE(kinds[2], SceneDocument::ChangeKinds(SceneDocument::Scenarios));
	// The fields show the typed values.
	QCOMPARE(panel.idEdit()->text(), "i1_typed");
	QCOMPARE(spinText(panel.startSecondsEdit())->text(), "77");
}

void TestIncidentPanel::forgetsDeletedDocument() {
	auto* document = new SceneDocument;
	IncidentPanel panel;
	panel.setDocument(document);
	document->replace(incidentScene(), false);
	delete document;
	panel.idEdit()->setText("after the document");
	panel.commitIdEdit();
	panel.addIncident();
	panel.refresh();
	// A panel whose document is gone shows nothing and edits nothing.
	QCOMPARE(panel.listWidget()->count(), 0);
	QVERIFY(!panel.addButton()->isEnabled());
}

// The empty state follows the list and the selection.
void TestIncidentPanel::emptyStateFollowsSelection() {
	SceneDocument document;
	IncidentPanel panel;
	FakePrompts prompts;
	prompts.confirmAnswer = true;
	RunSelection selection;
	panel.setPrompts(&prompts);
	panel.setRunSelection(&selection);
	panel.setDocument(&document);
	QVERIFY2(panel.emptyStateLabel()->isHidden(), "a panel without a scene does not point to Add Incident, which is disabled");
	document.replace(incidentScene(), false);
	selection.selectScenario("alt");
	panel.refresh();
	// A selected incident has no empty-state message.
	QVERIFY(panel.emptyStateLabel()->isHidden());
	QVERIFY(panel.emptyStateLabel()->text().isEmpty());
	panel.deleteIncident();
	// A scenario without incidents says so, and the Add button it points to is enabled.
	QCOMPARE(panel.emptyStateLabel()->text(), "This scenario has no incidents. Use Add Incident.");
	QVERIFY(!panel.emptyStateLabel()->isHidden());
	QVERIFY(panel.emptyStateLabel()->property("dialogHelp").toBool());
	QVERIFY(panel.addButton()->isEnabled());
	QVERIFY(!panel.idEdit()->isEnabled());
	panel.addIncident();
	QVERIFY2(panel.emptyStateLabel()->isHidden(), "adding an incident removes the message");
	panel.listWidget()->setCurrentRow(-1);
	QVERIFY2(panel.emptyStateLabel()->text() == "Select an incident or use Add Incident.",
		"a scenario with incidents and no selected row asks for a selection");
	document.setEditable(false);
	panel.refresh();
	QVERIFY2(panel.emptyStateLabel()->isHidden(), "a scene that a run uses does not point to the disabled Add button");
}

// A check box stands in front of the field it switches on.
void TestIncidentPanel::checkBoxStandsBeforeItsField() {
	SceneDocument document;
	IncidentPanel panel;
	panel.setDocument(&document);
	document.replace(incidentScene(), false);
	QFormLayout* form = panel.idEdit()->parentWidget()->findChild<QFormLayout*>();
	const auto labelOf = [form](QWidget* field) -> QWidget* {
		int row = -1;
		QFormLayout::ItemRole role = QFormLayout::SpanningRole;
		if (!form)
			return nullptr;
		form->getWidgetPosition(field, &row, &role);
		QLayoutItem* label = row >= 0 && role == QFormLayout::FieldRole ? form->itemAt(row, QFormLayout::LabelRole) : nullptr;
		return label ? label->widget() : nullptr;
	};
	// The recovery end and the reduced speed are fields of rows that start with their check boxes.
	if (form == nullptr)
		QFAIL("the fields of an incident are not a form");
	QVERIFY(labelOf(panel.endSecondsEdit()) == panel.hasEndSecondsCheck());
	QVERIFY(labelOf(panel.reducedSpeedKmhEdit()) == panel.hasReducedSpeedCheck());
	// The other fields have text labels.
	QVERIFY(qobject_cast<QLabel*>(labelOf(panel.startSecondsEdit())));
	QVERIFY(qobject_cast<QLabel*>(labelOf(panel.idEdit())));
	panel.resize(420, 700);
	panel.show();
	// The panel is hidden before it is destroyed, also when a check fails.
	const auto hide = qScopeGuard([&panel] { panel.hide(); });
	QApplication::processEvents();
	const auto topLeft = [&panel](const QWidget* widget) { return widget->mapTo(&panel, QPoint(0, 0)); };
	const auto precedes = [&topLeft](const QWidget* check, const QWidget* field) {
		return topLeft(check).y() + check->height() <= topLeft(field).y();
	};
	// Each check box is drawn above the field it switches on.
	QVERIFY(precedes(panel.hasEndSecondsCheck(), panel.endSecondsEdit()));
	QVERIFY(precedes(panel.hasReducedSpeedCheck(), panel.reducedSpeedKmhEdit()));
	const auto inside = [&panel, &topLeft](const QWidget* widget) { return panel.rect().contains(QRect(topLeft(widget), widget->size())); };
	// The Add button and the first field are inside the panel.
	QVERIFY(inside(panel.addButton()));
	QVERIFY(inside(panel.idEdit()));
	QVERIFY2(panel.hasEndSecondsCheck()->text() == "End (s)", "the check box of the recovery end names its unit");
}

// A scene without a scenario has no Add button that works, so the empty state does not point to it.
void TestIncidentPanel::emptyStateWithoutScenario() {
	SceneModel scene = incidentScene();
	scene.scenarios.clear();
	SceneDocument document;
	IncidentPanel panel;
	panel.setDocument(&document);
	document.replace(scene, false);
	QVERIFY2(panel.emptyStateLabel()->isHidden(), "without a scenario the empty state does not point to Add Incident");
}

// A scene without scenarios stays as it is until the first incident adds the baseline scenario in one edit.
void TestIncidentPanel::addsScenarioOnlyWithAnEdit() {
	SceneModel scene = incidentScene();
	scene.scenarios.clear();
	scene.defaultScenarioId.clear();
	IncidentFixture f(scene);
	f.panel.commitPendingEditors();
	QVERIFY(f.document.model().scenarios.empty());
	QVERIFY(!f.document.isDirty());
	f.panel.addIncident();
	QCOMPARE(f.document.model().scenarios.size(), std::size_t(1));
	QCOMPARE(f.document.model().scenarios[0].incidents.size(), std::size_t(1));
	QVERIFY(f.document.isDirty());
	QCOMPARE(f.events.log, edit);
}

// The target can be picked on the network view.
void TestIncidentPanel::pickButtonStandsByTarget() {
	IncidentPanel panel;
	RunSelection selection;
	panel.setRunSelection(&selection);
	QToolButton* pick = panel.pickTargetButton();
	// A checkable pick button that Tab reaches stands next to the target combo, disabled without an incident.
	if (pick == nullptr)
		QFAIL("the panel has no pick button");
	QCOMPARE(pick->objectName(), "incidentPickTargetButton");
	QVERIFY(pick->isCheckable());
	QVERIFY(!pick->isEnabled());
	QCOMPARE(pick->accessibleName(), "Pick target on network view");
	QCOMPARE(pick->toolTip(), "Pick the target on the network view (Esc cancels)");
	QVERIFY((pick->focusPolicy() & Qt::TabFocus) != 0);
	QVERIFY2(pick->parentWidget() == panel.targetCombo()->parentWidget(), "the pick button is in the fields of the incident");
}

void TestIncidentPanel::picksTargetOnNetworkView() {
	IncidentFixture f;
	IncidentPanel& panel = f.panel;
	QToolButton* pick = panel.pickTargetButton();
	if (pick == nullptr)
		QFAIL("the panel has no pick button");
	std::vector<bool> toggles;
	QObject::connect(&panel, &IncidentPanel::targetPickToggled, &panel, [&toggles](bool picking) { toggles.push_back(picking); });
	// The pick button is enabled with the target combo.
	QVERIFY(pick->isEnabled());
	QVERIFY(panel.targetCombo()->isEnabled());
	QVERIFY(!panel.isPickingTarget());
	pick->click();
	// Clicking the pick button starts picking and edits nothing.
	QVERIFY(pick->isChecked());
	QVERIFY(panel.isPickingTarget());
	QCOMPARE(toggles, std::vector<bool>{true});
	QVERIFY(f.events.log.empty());
	pick->click();
	// A second click ends picking.
	QVERIFY(!panel.isPickingTarget());
	QCOMPARE(toggles, std::vector<bool>({true, false}));
	pick->click();
	panel.endTargetPick();
	// The window can end picking.
	QVERIFY(!pick->isChecked());
	QCOMPARE(toggles, std::vector<bool>({true, false, true, false}));
	panel.endTargetPick();
	QVERIFY2(toggles.size() == 4, "ending picking that has ended does nothing");
	pick->click();
	panel.listWidget()->setCurrentRow(1);
	QVERIFY2(!panel.isPickingTarget(), "selecting another incident ends picking");
	pick->click();
	panel.refresh();
	QVERIFY2(!panel.isPickingTarget(), "showing the incidents again, as after another scene or scenario, ends picking");
	pick->click();
	QVERIFY(f.document.edit(SceneDocument::Infrastructure, "Edit track", [](SceneModel&) {}));
	QVERIFY2(!panel.isPickingTarget(), "an infrastructure edit can take away what can be picked, so it ends picking");
	f.events.log.clear();
	panel.show();
	pick->click();
	panel.hide();
	QVERIFY2(!panel.isPickingTarget(), "hiding the panel ends picking");
	pick->click();
	f.document.setEditable(false);
	panel.refreshTargets();
	// Picking ends, and the button is disabled with the combo, while a run uses the scene.
	QVERIFY(!panel.isPickingTarget());
	QVERIFY(!pick->isEnabled());
	QVERIFY(!panel.targetCombo()->isEnabled());
	// No target is taken while a run uses the scene.
	QVERIFY(!panel.applyTarget({"signal_failure", "sig1", 0}));
	QVERIFY(f.events.log.empty());
	f.document.setEditable(true);
	panel.refresh();
	QVERIFY2(pick->isEnabled(), "the pick button is enabled again after the run");
}

// A picked target.
void TestIncidentPanel::takesPickedTarget() {
	IncidentFixture f;
	SceneDocument& document = f.document;
	IncidentPanel& panel = f.panel;
	Events& events = f.events;
	const auto shown = [&document]() -> const SceneIncident& { return document.model().scenarios[0].incidents[0]; };
	panel.listWidget()->setCurrentRow(0);
	events.log.clear();
	// A block is taken as the target of a signal failure.
	QVERIFY(panel.applyTarget({"signal_failure", "blk1", 0}));
	QCOMPARE(shown().target, "blk1");
	QVERIFY(!shown().hasOccurrence);
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Change incident target");
	QCOMPARE(panel.targetCombo()->currentText(), "blk1");
	events.log.clear();
	// The target that the incident has changes nothing.
	QVERIFY(panel.applyTarget({"signal_failure", "blk1", 0}));
	QVERIFY(events.log.empty());
	// A target that the combo does not offer for its type is refused and changes nothing.
	QVERIFY(!panel.applyTarget({"signal_failure", "@blk1@", 0}));
	QVERIFY(!panel.applyTarget({"signal_failure", "nope", 0}));
	QVERIFY(!panel.applyTarget({"signal_failure", "", 0}));
	QVERIFY(!panel.applyTarget({"train_breakdown", "sig1", 1}));
	QVERIFY(!panel.applyTarget({"train_breakdown", "s2", 2}));
	QVERIFY(!panel.applyTarget({"train_breakdown", "s1", 0}));
	QVERIFY(!panel.applyTarget({"train_breakdown", "s1", 4}));
	QVERIFY(!panel.applyTarget({"other", "s1", 1}));
	QVERIFY(!panel.applyTarget({}));
	QVERIFY(events.log.empty());
	QCOMPARE(shown().type, "signal_failure");
	QCOMPARE(shown().target, "blk1");
	// An occurrence of a service makes a signal failure a breakdown of it, in one edit.
	QVERIFY(panel.applyTarget({"train_breakdown", "s1", 3}));
	QCOMPARE(shown().type, "train_breakdown");
	QCOMPARE(shown().target, "s1");
	QVERIFY(shown().hasOccurrence);
	QCOMPARE(shown().occurrence, 3);
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Change incident target");
	QCOMPARE(panel.typeCombo()->currentData().toString(), "train_breakdown");
	QCOMPARE(panel.targetCombo()->currentIndex(), choice(panel.targetCombo(), "s1", 3));
	QVERIFY(panel.hasReducedSpeedCheck()->isEnabled());
	QCOMPARE(panel.listWidget()->item(0)->text(), "i1: Train breakdown #3");
	panel.hasReducedSpeedCheck()->setChecked(true);
	panel.terminateAtDestinationCheck()->setChecked(true);
	events.log.clear();
	// A signal makes a breakdown a signal failure, which loses what only a breakdown has, in one edit.
	QVERIFY(panel.applyTarget({"signal_failure", "sig1", 0}));
	QCOMPARE(shown().type, "signal_failure");
	QCOMPARE(shown().target, "sig1");
	QVERIFY(!shown().hasOccurrence);
	QCOMPARE(shown().occurrence, 1);
	QVERIFY(!shown().hasReducedSpeed);
	QCOMPARE_EQ(shown().reducedSpeedKmh, 0.0);
	QVERIFY(!shown().terminateAtDestination);
	QCOMPARE(events.log, edit);
	QCOMPARE(panel.typeCombo()->currentData().toString(), "signal_failure");
	QCOMPARE(panel.targetCombo()->currentText(), "sig1");
	QCOMPARE(panel.listWidget()->item(0)->text(), "i1: Signal failure");
}

// The target of a new incident.
void TestIncidentPanel::givesNewIncidentTarget() {
	IncidentFixture f;
	SceneDocument& document = f.document;
	IncidentPanel& panel = f.panel;
	Events& events = f.events;
	const std::size_t countBefore = document.model().scenarios[0].incidents.size();
	std::size_t countAtCall = 0;
	panel.setDefaultTarget([&document, &countAtCall] {
		countAtCall = document.model().scenarios[0].incidents.size();
		return IncidentTarget{"train_breakdown", "s1", 2};
	});
	events.log.clear();
	panel.addIncident();
	const SceneIncident& added = document.model().scenarios[0].incidents.back();
	// A new incident gets the target that the window gives before the edit, in the same edit.
	QCOMPARE(countAtCall, countBefore);
	QCOMPARE(added.id, "new_incident");
	QCOMPARE(added.type, "train_breakdown");
	QCOMPARE(added.target, "s1");
	QVERIFY(added.hasOccurrence);
	QCOMPARE(added.occurrence, 2);
	QVERIFY(added.hasEndSeconds);
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Add incident");
	QCOMPARE(panel.typeCombo()->currentData().toString(), "train_breakdown");
	QCOMPARE(panel.targetCombo()->currentIndex(), choice(panel.targetCombo(), "s1", 2));
	QCOMPARE(panel.listWidget()->currentItem()->text(), "new_incident: Train breakdown #2");
	panel.setDefaultTarget([] { return IncidentTarget{"signal_failure", "sig1", 0}; });
	panel.addIncident();
	// A new incident can be a signal failure of the signal that the window gives.
	QCOMPARE(document.model().scenarios[0].incidents.back().type, "signal_failure");
	QCOMPARE(document.model().scenarios[0].incidents.back().target, "sig1");
	panel.setDefaultTarget([] { return IncidentTarget{"signal_failure", "missing", 0}; });
	panel.addIncident();
	// A target that the combo does not offer leaves the new incident without a target.
	QCOMPARE(document.model().scenarios[0].incidents.back().type, "signal_failure");
	QVERIFY(document.model().scenarios[0].incidents.back().target.empty());
	panel.setDefaultTarget([] { return IncidentTarget(); });
	panel.addIncident();
	// Without a target from the window a new incident is a signal failure without a target.
	QCOMPARE(document.model().scenarios[0].incidents.back().type, "signal_failure");
	QVERIFY(document.model().scenarios[0].incidents.back().target.empty());
	panel.setDefaultTarget({});
	panel.addIncident();
	// A panel without a target provider adds a signal failure without a target.
	QCOMPARE(document.model().scenarios[0].incidents.size(), countBefore + 5);
	QVERIFY(document.model().scenarios[0].incidents.back().target.empty());
}

// A picked occurrence after the ones that the combo lists.
void TestIncidentPanel::takesOccurrenceThatComboDoesNotList() {
	SceneModel scene = incidentScene();
	SceneService many = service("many");
	many.hasRepeat = true;
	many.hasRepeatCount = true;
	many.repeatCount = RuntimeLimits::kMaxExpandedTrains + 1;
	scene.services.push_back(many);
	IncidentFixture f(scene);
	IncidentPanel& panel = f.panel;
	panel.listWidget()->setCurrentRow(1);
	const int last = many.repeatCount;
	QVERIFY2(choice(panel.targetCombo(), "many", last) < 0, "the combo does not list the last occurrence of a service over the limit");
	// An occurrence that the combo does not list is taken and becomes its current choice.
	QVERIFY(panel.applyTarget({"train_breakdown", "many", last}));
	QCOMPARE(f.document.model().scenarios[0].incidents[1].target, "many");
	QCOMPARE(f.document.model().scenarios[0].incidents[1].occurrence, last);
	QCOMPARE(panel.targetCombo()->currentIndex(), choice(panel.targetCombo(), "many", last));
}

// A signal failure names its targets by the labels of their sections and keeps their ids.
void TestIncidentPanel::namesTargetsByLabels() {
	SceneModel scene = incidentScene();
	scene.tracks = {{"T1"}};
	scene.nodes = {{"n1", "T1", 0.0, 0.0}, {"n2", "T1", 2.0, 0.0}};
	scene.arcs = {{"a1", "T1", "n1", "n2", 0.0, 0.0, 20.0}};
	scene.blocks = {{"blk1", "T1", 1.0}, {"s2", "T1", 1.0}};
	sceneSignals(scene)[0].protectedSection = "blk1";
	IncidentFixture f(scene);
	IncidentPanel& panel = f.panel;
	panel.listWidget()->setCurrentRow(0);
	const QComboBox* target = panel.targetCombo();
	// The targets of a signal failure are named by their labels, with the id as data and tooltip.
	QCOMPARE(choices(target), QStringList({"Choose target", "sig1 (T1 0.000 to 1.000 km)", "T1 0.000 to 1.000 km", "T1 1.000 to 2.000 km"}));
	QCOMPARE(target->currentIndex(), 1);
	QCOMPARE(target->itemData(1, Qt::ToolTipRole).toString(), "sig1");
	QCOMPARE(target->itemData(2).toString(), "blk1");
	QCOMPARE(target->itemData(2, Qt::ToolTipRole).toString(), "blk1");
	panel.targetCombo()->setCurrentIndex(3);
	// A block chosen by its label is saved by its id.
	QCOMPARE(f.document.model().scenarios[0].incidents[0].target, "s2");
	QCOMPARE(panel.targetCombo()->currentText(), "T1 1.000 to 2.000 km");
}

// A signal failure is offered each signal and then each block once, without an empty id, and a target that the scene does
// not have as missing. Its entries name no run and have the id as tooltip.
void TestIncidentPanel::choicesListSignalFailureTargetsOnce() {
	SceneModel scene = incidentScene();
	SceneSignal again;
	again.id = "sig1";
	SceneSignal unnamed;
	SceneSignal other;
	other.id = "sig2";
	sceneSignals(scene) = {sceneSignals(scene)[0], again, unnamed, other};
	SceneBlock sameAsSignal;
	sameAsSignal.id = "sig2";
	scene.blocks.push_back(sameAsSignal);
	scene.blocks.push_back(SceneBlock());
	QCOMPARE(signalFailureTargets(scene), std::vector<std::string>({"sig1", "sig2", "blk1", "s2"}));

	SceneIncident incident = signalFailure("i1", "sig2");
	IncidentTargetChoices choices = incidentTargetChoices(scene, &incident, 3600.0);
	QCOMPARE(texts(choices), QStringList({"Choose target", "sig1", "sig2", "blk1", "s2"}));
	QCOMPARE(choices.current, 2);
	QCOMPARE(runs(choices), QList<int>({-1, -1, -1, -1, -1}));
	QCOMPARE(choices.entries[3].target, "blk1");
	QCOMPARE(choices.entries[3].toolTip, "blk1");
	QCOMPARE(choices.entries[0].toolTip, QString());

	incident.target = "gone";
	choices = incidentTargetChoices(scene, &incident, 3600.0);
	QCOMPARE(texts(choices).last(), "gone (missing)");
	QCOMPARE(choices.entries.back().target, "gone");
	QCOMPARE(choices.current, 5);

	incident.target.clear();
	choices = incidentTargetChoices(scene, &incident, 3600.0);
	QCOMPARE(choices.entries.size(), std::size_t(5));
	QCOMPARE(choices.current, 0);

	choices = incidentTargetChoices(scene, nullptr, 3600.0);
	QCOMPARE(texts(choices), QStringList({"Choose target"}));
	QCOMPARE(choices.current, 0);
}

// A breakdown is offered all runs of its service when it names no run, then each run of each service, counted over the
// duration given. A service or run that the scene does not have is listed as missing.
void TestIncidentPanel::choicesListRunsOfBreakdown() {
	const SceneModel scene = incidentScene();
	SceneIncident incident = breakdown("i3", "s1");
	IncidentTargetChoices choices = incidentTargetChoices(scene, &incident, 3600.0);
	const QStringList listed({"s1-1 (route r1, +0 s)", "s1-2 (route r1, +1200 s)", "s1-3 (route r1, +2400 s)", "s2 (route r1, +0 s)"});
	QCOMPARE(texts(choices), QStringList({"Choose target", "All runs of s1"}) + listed);
	QCOMPARE(runs(choices), QList<int>({-1, 0, 1, 2, 3, 1}));
	QCOMPARE(choices.current, 1);
	QCOMPARE(choices.entries[1].toolTip, "All runs of s1\nService s1, all runs");
	QCOMPARE(choices.entries[3].toolTip, "s1-2 (route r1, +1200 s)\nService s1, run 2");
	QCOMPARE(choices.entries[0].toolTip, "Choose target");
	QCOMPARE(texts(incidentTargetChoices(scene, &incident, 1200.0)), QStringList({"Choose target", "All runs of s1", "s1-1 (route r1, +0 s)", "s2 (route r1, +0 s)"}));

	incident.target = "gone";
	choices = incidentTargetChoices(scene, &incident, 3600.0);
	QCOMPARE(texts(choices), QStringList({"Choose target", "gone (missing)"}) + listed);
	QCOMPARE(choices.entries[1].occurrence, std::optional<int>(0));
	QCOMPARE(choices.current, 1);

	incident.target = "s1";
	incident.hasOccurrence = true;
	incident.occurrence = 3;
	choices = incidentTargetChoices(scene, &incident, 3600.0);
	QCOMPARE(texts(choices), QStringList({"Choose target"}) + listed);
	QCOMPARE(choices.current, 3);

	incident.occurrence = 4;
	choices = incidentTargetChoices(scene, &incident, 3600.0);
	QCOMPARE(texts(choices), QStringList({"Choose target"}) + listed + QStringList({"s1 #4 (missing)"}));
	QCOMPARE(choices.entries.back().occurrence, std::optional<int>(4));
	QCOMPARE(choices.current, 5);

	incident.target = "gone";
	incident.occurrence = 2;
	choices = incidentTargetChoices(scene, &incident, 3600.0);
	QCOMPARE(texts(choices).last(), "gone #2 (missing)");
	QCOMPARE(choices.current, 5);
}

// The runs offered stop at the limit of runs of all services together. A run past it that the incident names is added
// after them.
void TestIncidentPanel::choicesStopAtRunLimit() {
	SceneModel scene = incidentScene();
	SceneService many = service("many");
	many.hasRepeat = true;
	many.hasRepeatCount = true;
	many.repeatCount = RuntimeLimits::kMaxExpandedTrains;
	many.headwaySeconds = 60.0;
	scene.services.insert(scene.services.begin() + 1, many);
	SceneIncident incident = breakdown("i3", "many");
	incident.hasOccurrence = true;
	incident.occurrence = 2;
	IncidentTargetChoices choices = incidentTargetChoices(scene, &incident, 3600.0);
	const std::size_t limit = static_cast<std::size_t>(RuntimeLimits::kMaxExpandedTrains);
	QCOMPARE(choices.entries.size(), 1 + limit);
	QCOMPARE(choices.entries[4].text, "many-1 (route r1, +0 s)");
	QCOMPARE(choices.current, 5);
	QCOMPARE(choices.entries.back().occurrence, std::optional<int>(RuntimeLimits::kMaxExpandedTrains - 3));
	QVERIFY2(!texts(choices).contains("s2 (route r1, +0 s)"), "a service after the limit is not offered");

	incident.occurrence = RuntimeLimits::kMaxExpandedTrains;
	choices = incidentTargetChoices(scene, &incident, 3600.0);
	QCOMPARE(choices.entries.size(), 2 + limit);
	QCOMPARE(choices.current, static_cast<int>(1 + limit));
	QCOMPARE(choices.entries.back().text, QString("many-%1 (route r1, +%2 s)").arg(RuntimeLimits::kMaxExpandedTrains).arg((RuntimeLimits::kMaxExpandedTrains - 1) * 60));
	QCOMPARE(choices.entries.back().occurrence, std::optional<int>(RuntimeLimits::kMaxExpandedTrains));
}

// Entries that read the same name their service and run, in the text and on a line of the tooltip of their own.
void TestIncidentPanel::choicesNameRepeatedTexts() {
	SceneModel scene = incidentScene();
	SceneService first = service("a");
	first.operatingCode = "X";
	SceneService second = service("b");
	second.operatingCode = "X";
	scene.services = {first, second};
	SceneIncident incident = breakdown("i3", "b");
	incident.hasOccurrence = true;
	const IncidentTargetChoices choices = incidentTargetChoices(scene, &incident, 3600.0);
	QCOMPARE(texts(choices), QStringList({"Choose target", "X (route r1, +0 s) (service a, run 1)", "X (route r1, +0 s) (service b, run 1)"}));
	QCOMPARE(choices.current, 2);
	QCOMPARE(choices.entries[2].toolTip, "X (route r1, +0 s)\nService b, run 1");
}

// In the form of macOS the fields grow to the width of the panel, and a long target is cut in the middle. The
// application style stays changed after this slot, so it comes last.
void TestIncidentPanel::fieldsGrowInFormOfMacOs() {
	QApplication::setStyle(new MacFormStyle);
	SceneModel scene = incidentScene();
	scene.services[0].route = std::string(200, 'r');
	IncidentFixture f(scene);
	IncidentPanel& panel = f.panel;
	panel.listWidget()->setCurrentRow(1);
	panel.resize(320, 700);
	panel.show();
	QApplication::processEvents();
	const QFormLayout* form = panel.idEdit()->parentWidget()->findChild<QFormLayout*>();
	// The fields of the incident grow to the width of the form.
	if (form == nullptr)
		QFAIL("the fields of an incident are not a form");
	QCOMPARE(form->fieldGrowthPolicy(), QFormLayout::AllNonFixedFieldsGrow);
	const auto* target = dynamic_cast<const ChoiceComboBox*>(panel.targetCombo());
	// A long target is cut with an ellipsis in a narrow panel.
	if (target == nullptr)
		QFAIL("the target is not a choice combo");
	QVERIFY(target->currentText().startsWith("s1-2 (route rrr"));
	QVERIFY(target->displayText().contains(QChar(0x2026)));
	// The tip of the target says what the type takes as its target, below the current target.
	const QString trainTip = panel.targetCombo()->toolTip();
	// The tip of the target of a breakdown names a train, below the current target and its identity.
	QVERIFY(trainTip.contains("train"));
	QCOMPARE(shownTip(panel.targetCombo()),
		panel.targetCombo()->currentText() + "\n" + panel.targetCombo()->currentData(Qt::ToolTipRole).toString() + "\n" + trainTip);
	panel.listWidget()->setCurrentRow(0);
	const QString signalTip = panel.targetCombo()->toolTip();
	// The tip of the target of a signal failure names no service or train.
	QVERIFY(!signalTip.isEmpty());
	QVERIFY(!signalTip.contains("service", Qt::CaseInsensitive));
	QVERIFY(!signalTip.contains("train", Qt::CaseInsensitive));
}

QTEST_MAIN(TestIncidentPanel)
#include "test_editor_incident.moc"
