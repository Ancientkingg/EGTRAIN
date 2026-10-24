#include "editors/IncidentPanel.h"
#include "scene/RuntimeLimits.h"
#include "scene/SceneWriter.h"
#include "widgets/ChoiceComboBox.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLoggingCategory>
#include <QProxyStyle>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QToolTip>

#include <iostream>
#include <string>
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

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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

// Answers the questions of the panel and records the messages, without a dialog.
class FakePrompts : public UserPrompts {
public:
	QString errorTitle;
	QString errorMessage;
	int errorCount = 0;
	int confirmCount = 0;
	QString confirmQuestion;
	bool confirmAnswer = false;

	void showError(QWidget*, const QString& title, const QString& message, bool = false) override {
		++errorCount;
		errorTitle = title;
		errorMessage = message;
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

static bool addsIncidentWithTarget(const Log& edit);
static bool namesTypes(const Log& edit);

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	// The icon of the pick button is a resource of the application, which this test does not have.
	QLoggingCategory::setFilterRules(QStringLiteral("qt.svg.warning=false"));
	bool ok = true;
	const Log edit({"documentChanged", "panelChanged"});
	ok &= addsIncidentWithTarget(edit);
	ok &= namesTypes(edit);

	{
		IncidentPanel panel;
		ok &= expect(panel.listWidget()->count() == 0 && !panel.listWidget()->isEnabled() && !panel.addButton()->isEnabled()
				&& !panel.idEdit()->isEnabled() && !panel.targetCombo()->isEnabled() && panel.targetCombo()->count() == 1,
			"a panel without a document shows nothing and offers nothing");
		SceneDocument document;
		panel.setDocument(&document);
		panel.addIncident();
		panel.duplicateIncident();
		panel.deleteIncident();
		panel.commitIdEdit();
		panel.commitPendingEditors();
		ok &= expect(!panel.addButton()->isEnabled() && document.revision() == 0 && !document.isDirty() && document.model().scenarios.empty(),
			"a panel stays disabled while its document holds no scene, and its actions change nothing");
	}

	{
		SceneDocument document;
		IncidentPanel panel;
		FakePrompts prompts;
		RunSelection selection;
		panel.setPrompts(&prompts);
		panel.setRunSelection(&selection);
		panel.setDocument(&document);
		Events events(panel, document);

		// refresh
		document.replace(incidentScene(), false);
		selection.selectScenario("base");
		panel.refresh();
		ok &= expect(items(panel.listWidget()) == QStringList({"i1: Signal failure", "i2: Train breakdown #2, 30 km/h"})
				&& panel.listWidget()->currentRow() == 0 && panel.listWidget()->isEnabled() && panel.addButton()->isEnabled()
				&& panel.duplicateButton()->isEnabled() && panel.deleteButton()->isEnabled(),
			"the incidents of the selected scenario are listed, and the first is selected");
		ok &= expect(panel.idEdit()->text() == "i1" && panel.typeCombo()->currentData().toString() == "signal_failure"
				&& panel.startSecondsEdit()->text() == "10" && panel.endSecondsEdit()->text() == "20" && panel.hasEndSecondsCheck()->isChecked()
				&& !panel.hasReducedSpeedCheck()->isEnabled() && !panel.reducedSpeedKmhEdit()->isEnabled()
				&& !panel.terminateAtDestinationCheck()->isEnabled() && !panel.hasReducedSpeedCheck()->isChecked(),
			"the selected signal failure shows its fields, and the fields of a breakdown wait");
		ok &= expect(choices(panel.targetCombo()) == QStringList({"Choose target", "sig1", "blk1", "s2"})
				&& panel.targetCombo()->currentText() == "sig1" && panel.targetCombo()->isEnabled(),
			"the targets of a signal failure are the signals and the blocks");
		ok &= expect(events.log.empty() && !document.isDirty() && document.revision() == 1 && !selection.isScenarioModified("base"),
			"showing a scene edits nothing");

		// a breakdown
		panel.listWidget()->setCurrentRow(1);
		ok &= expect(panel.idEdit()->text() == "i2" && panel.typeCombo()->currentData().toString() == "train_breakdown"
				&& panel.hasReducedSpeedCheck()->isEnabled() && panel.hasReducedSpeedCheck()->isChecked()
				&& panel.reducedSpeedKmhEdit()->isEnabled() && panel.reducedSpeedKmhEdit()->value() == 30.0
				&& panel.terminateAtDestinationCheck()->isEnabled() && !panel.terminateAtDestinationCheck()->isChecked(),
			"a breakdown shows its speed cap");
		ok &= expect(choices(panel.targetCombo())
					== QStringList({"Choose target", "s1-1 (route r1, +0 s)", "s1-2 (route r1, +1200 s)",
						"s1-3 (route r1, +2400 s)", "s2 (route r1, +0 s)"})
				&& panel.targetCombo()->currentIndex() == 2
				&& panel.targetCombo()->itemData(2, Qt::ToolTipRole).toString() == "s1-2 (route r1, +1200 s)\nService s1, run 2",
			"the targets of a breakdown are the occurrences of the services, and the occurrence of the incident is chosen");
		ok &= expect(events.log.empty(), "selecting an incident edits nothing");

		// the type decides which targets are valid
		panel.listWidget()->setCurrentRow(0);
		panel.typeCombo()->setCurrentIndex(panel.typeCombo()->findData("train_breakdown"));
		ok &= expect(document.model().scenarios[0].incidents[0].type == "train_breakdown" && document.model().scenarios[0].incidents[0].target.empty()
				&& panel.targetCombo()->currentIndex() == 0 && panel.hasReducedSpeedCheck()->isEnabled()
				&& choices(panel.targetCombo()).size() == 5 && panel.listWidget()->item(0)->text() == "i1: Train breakdown"
				&& events.log == edit && events.kinds == SceneDocument::Scenarios && document.lastChangeLabel() == "Change incident type"
				&& selection.isScenarioModified("base"),
			"a breakdown has no signal as its target, and the targets are the services");
		panel.targetCombo()->setCurrentIndex(choice(panel.targetCombo(), "s1", 3));
		ok &= expect(document.model().scenarios[0].incidents[0].target == "s1" && document.model().scenarios[0].incidents[0].hasOccurrence
				&& document.model().scenarios[0].incidents[0].occurrence == 3 && document.lastChangeLabel() == "Change incident target",
			"a chosen occurrence is the target of the breakdown");
		panel.targetCombo()->setCurrentIndex(choice(panel.targetCombo(), "s2", 1));
		panel.typeCombo()->setCurrentIndex(panel.typeCombo()->findData("signal_failure"));
		ok &= expect(document.model().scenarios[0].incidents[0].type == "signal_failure" && document.model().scenarios[0].incidents[0].target == "s2"
				&& !document.model().scenarios[0].incidents[0].hasOccurrence && document.model().scenarios[0].incidents[0].occurrence == 1
				&& panel.targetCombo()->currentText() == "s2" && !panel.hasReducedSpeedCheck()->isEnabled(),
			"a target that is valid for both types stays, and a signal failure has no occurrence");
		panel.targetCombo()->setCurrentIndex(choice(panel.targetCombo(), "sig1", 0));
		panel.typeCombo()->setCurrentIndex(panel.typeCombo()->findData("train_breakdown"));
		ok &= expect(document.model().scenarios[0].incidents[0].target.empty(), "a signal is dropped as the target of a breakdown");
		panel.typeCombo()->setCurrentIndex(panel.typeCombo()->findData("signal_failure"));
		ok &= expect(document.model().scenarios[0].incidents[0].target.empty() && panel.targetCombo()->currentIndex() == 0,
			"a signal failure starts without a target after the type changes");
		panel.targetCombo()->setCurrentIndex(choice(panel.targetCombo(), "sig1", 0));
		ok &= expect(document.model().scenarios[0].incidents[0].target == "sig1" && !document.model().scenarios[0].incidents[0].hasOccurrence,
			"a signal is the target of a signal failure");
		panel.targetCombo()->setCurrentIndex(0);
		ok &= expect(document.model().scenarios[0].incidents[0].target.empty(), "Choose target clears the target");
		panel.targetCombo()->setCurrentIndex(choice(panel.targetCombo(), "sig1", 0));

		// a breakdown that loses its speed cap when it becomes a signal failure
		panel.listWidget()->setCurrentRow(1);
		panel.typeCombo()->setCurrentIndex(panel.typeCombo()->findData("signal_failure"));
		const SceneIncident& converted = document.model().scenarios[0].incidents[1];
		ok &= expect(converted.type == "signal_failure" && !converted.hasReducedSpeed && converted.reducedSpeedKmh == 0.0 && !converted.hasOccurrence
				&& converted.occurrence == 1 && !converted.terminateAtDestination && !panel.hasReducedSpeedCheck()->isChecked()
				&& !panel.reducedSpeedKmhEdit()->isEnabled() && converted.target.empty(),
			"a signal failure has none of the fields of a breakdown");
		panel.typeCombo()->setCurrentIndex(panel.typeCombo()->findData("train_breakdown"));
		ok &= expect(!panel.hasReducedSpeedCheck()->isChecked() && panel.hasReducedSpeedCheck()->isEnabled() && !panel.reducedSpeedKmhEdit()->isEnabled()
				&& panel.reducedSpeedKmhEdit()->value() == 40.0,
			"the speed cap of a new breakdown is off and offers 40 km/h");

		// the speed cap
		events.log.clear();
		panel.hasReducedSpeedCheck()->setChecked(true);
		ok &= expect(converted.hasReducedSpeed && converted.reducedSpeedKmh == 40.0 && panel.reducedSpeedKmhEdit()->isEnabled() && events.log == edit
				&& document.lastChangeLabel() == "Change incident reduced speed",
			"switching the speed cap on writes the speed that is shown");
		panel.reducedSpeedKmhEdit()->setValue(45.5);
		panel.commitReducedSpeed();
		ok &= expect(converted.reducedSpeedKmh == 45.5 && converted.hasReducedSpeed
				&& panel.listWidget()->item(1)->text() == "i2: Train breakdown, 45.5 km/h",
			"a speed is written to the document and shown in the list");
		events.log.clear();
		panel.commitReducedSpeed();
		ok &= expect(events.log.empty(), "an unchanged speed does not touch the document");
		panel.hasReducedSpeedCheck()->setChecked(false);
		ok &= expect(!converted.hasReducedSpeed && converted.reducedSpeedKmh == 0.0 && !panel.reducedSpeedKmhEdit()->isEnabled()
				&& panel.reducedSpeedKmhEdit()->value() == 40.0,
			"switching the speed cap off removes it, and the field offers 40 km/h again");
		panel.refresh();
		ok &= expect(panel.listWidget()->item(1)->text() == "i2: Train breakdown", "the list shows the incident without a speed cap");

		// the end of the incident
		events.log.clear();
		panel.hasEndSecondsCheck()->setChecked(false);
		ok &= expect(!converted.hasEndSeconds && converted.endSeconds == 0.0 && panel.endSecondsEdit()->text() == "0" && events.log == edit
				&& !panel.hasEndSecondsCheck()->isChecked(),
			"a breakdown can go on without a recovery end");
		panel.endSecondsEdit()->setText("500");
		panel.commitEndSeconds();
		ok &= expect(converted.hasEndSeconds && converted.endSeconds == 500.0 && panel.hasEndSecondsCheck()->isChecked() == false,
			"a recovery end is written to the document");
		panel.refresh();
		ok &= expect(panel.hasEndSecondsCheck()->isChecked() && panel.endSecondsEdit()->text() == "500", "the recovery end is shown again");
		panel.endSecondsEdit()->setText("");
		panel.commitEndSeconds();
		ok &= expect(converted.endSeconds == 0.0 && converted.hasEndSeconds && panel.endSecondsEdit()->text() == "0",
			"a blank recovery end is shown and stored as zero");
		panel.hasEndSecondsCheck()->setChecked(true);
		ok &= expect(converted.hasEndSeconds && panel.hasEndSecondsCheck()->isChecked(), "the recovery end switches on");
		panel.startSecondsEdit()->setText("250");
		panel.commitStartSeconds();
		ok &= expect(converted.startSeconds == 250.0 && document.lastChangeLabel() == "Change incident start", "a start is written to the document");
		events.log.clear();
		panel.commitStartSeconds();
		ok &= expect(events.log.empty(), "an unchanged start does not touch the document");
		panel.terminateAtDestinationCheck()->setChecked(true);
		ok &= expect(converted.terminateAtDestination && events.log == edit, "a breakdown can end at the destination");

		// ids
		events.log.clear();
		prompts.errorCount = 0;
		panel.idEdit()->setText("  i2b  ");
		panel.commitIdEdit();
		ok &= expect(converted.id == "i2b" && panel.listWidget()->item(1)->text() == "i2b: Train breakdown" && events.log == edit
				&& document.lastChangeLabel() == "Rename incident",
			"a new id is written to the document, trimmed, and the list shows it");
		events.log.clear();
		panel.idEdit()->setText("i3");
		panel.commitIdEdit();
		ok &= expect(prompts.errorCount == 1 && prompts.errorTitle == "Duplicate Incident ID" && panel.idEdit()->text() == "i2b"
				&& converted.id == "i2b" && events.log.empty(),
			"an id that an incident of another scenario has is refused, and the field shows the old id again");
		panel.idEdit()->setText(" ");
		panel.commitIdEdit();
		ok &= expect(prompts.errorCount == 2 && prompts.errorTitle == "Invalid Incident ID" && panel.idEdit()->text() == "i2b" && events.log.empty(),
			"an empty id is refused, and the field shows the old id again");
		panel.idEdit()->setText("i2b");
		panel.commitIdEdit();
		ok &= expect(prompts.errorCount == 2 && events.log.empty(), "an unchanged id does not touch the document");

		// add, duplicate and delete
		document.markSaved();
		selection.clearModifiedScenarios();
		events.log.clear();
		panel.addIncident();
		const SceneIncident& added = document.model().scenarios[0].incidents.back();
		ok &= expect(document.model().scenarios[0].incidents.size() == 3 && added.id == "new_incident" && added.type == "signal_failure"
				&& added.hasEndSeconds && panel.listWidget()->count() == 3 && panel.listWidget()->currentRow() == 2
				&& panel.idEdit()->text() == "new_incident" && document.isDirty() && selection.isScenarioModified("base")
				&& events.log == edit && document.lastChangeLabel() == "Add incident",
			"an incident is added to the selected scenario, selected and shown");
		panel.addIncident();
		ok &= expect(document.model().scenarios[0].incidents.back().id == "new_incident_2", "the id of another new incident is unique");
		events.log.clear();
		panel.listWidget()->setCurrentRow(0);
		panel.duplicateIncident();
		ok &= expect(document.model().scenarios[0].incidents.size() == 5 && document.model().scenarios[0].incidents[1].id == "i1_copy"
				&& document.model().scenarios[0].incidents[1].target == "sig1" && panel.listWidget()->currentRow() == 1
				&& events.log == edit && document.lastChangeLabel() == "Duplicate incident",
			"a copy is put after the incident, with an id of its own, and selected");
		prompts.confirmCount = 0;
		events.log.clear();
		panel.deleteIncident();
		ok &= expect(prompts.confirmCount == 1 && prompts.confirmQuestion == "Delete incident 'i1_copy'?"
				&& document.model().scenarios[0].incidents.size() == 5 && events.log.empty(),
			"declining the question keeps the incident");
		prompts.confirmAnswer = true;
		panel.deleteIncident();
		ok &= expect(document.model().scenarios[0].incidents.size() == 4 && document.model().scenarios[0].incidents[1].id == "i2b"
				&& panel.listWidget()->count() == 4 && events.log == edit && document.lastChangeLabel() == "Delete incident",
			"a deleted incident is removed");

		// the other scenario
		selection.selectScenario("alt");
		panel.refresh();
		ok &= expect(items(panel.listWidget()) == QStringList({"i3: Train breakdown"}) && panel.idEdit()->text() == "i3"
				&& panel.targetCombo()->currentIndex() == 1 && panel.targetCombo()->currentText().startsWith("All runs of s2"),
			"the incidents of the selected scenario are shown, and a breakdown of every occurrence is a historical scope");
		events.log.clear();
		selection.clearModifiedScenarios();
		panel.startSecondsEdit()->setText("5");
		panel.commitStartSeconds();
		ok &= expect(document.model().scenarios[1].incidents[0].startSeconds == 5.0 && selection.isScenarioModified("alt")
				&& !selection.isScenarioModified("base"),
			"an edit marks the selected scenario");
		selection.selectScenario("base");
		panel.refresh();

		// pending edits
		document.markSaved();
		events.log.clear();
		panel.listWidget()->setCurrentRow(0);
		panel.startSecondsEdit()->setText("77");
		panel.commitPendingEditors();
		ok &= expect(document.model().scenarios[0].incidents[0].startSeconds == 77.0 && events.log == edit,
			"unfinished text is taken into the document");
		events.log.clear();
		panel.endSecondsEdit()->setText("99");
		panel.hasEndSecondsCheck()->blockSignals(true);
		panel.hasEndSecondsCheck()->setChecked(true);
		panel.hasEndSecondsCheck()->blockSignals(false);
		panel.commitPendingEditors();
		ok &= expect(document.model().scenarios[0].incidents[0].endSeconds == 99.0, "an unfinished recovery end is taken in when it is switched on");
		panel.commitPendingEditors();
		ok &= expect(events.log == edit, "committing again changes nothing");

		// the scene is in use by a run
		document.markSaved();
		events.log.clear();
		document.setEditable(false);
		panel.refresh();
		ok &= expect(!panel.listWidget()->isEnabled() && !panel.addButton()->isEnabled() && !panel.duplicateButton()->isEnabled()
				&& !panel.deleteButton()->isEnabled() && !panel.idEdit()->isEnabled() && !panel.typeCombo()->isEnabled()
				&& !panel.targetCombo()->isEnabled() && !panel.startSecondsEdit()->isEnabled() && !panel.endSecondsEdit()->isEnabled()
				&& !panel.hasEndSecondsCheck()->isEnabled() && !panel.hasReducedSpeedCheck()->isEnabled()
				&& !panel.terminateAtDestinationCheck()->isEnabled() && panel.idEdit()->text().isEmpty(),
			"nothing can be done with the incidents, and the fields are empty, while a run uses the scene");
		panel.idEdit()->setText("while-running");
		panel.startSecondsEdit()->setText("1");
		panel.commitPendingEditors();
		panel.commitIdEdit();
		panel.addIncident();
		panel.duplicateIncident();
		panel.deleteIncident();
		panel.typeCombo()->setCurrentIndex(panel.typeCombo()->findData("train_breakdown"));
		ok &= expect(document.model().scenarios[0].incidents[0].id == "i1" && document.model().scenarios[0].incidents.size() == 4 && events.log.empty()
				&& !document.isDirty(),
			"pending text and actions change nothing while a run uses the scene");
		document.setEditable(true);
		panel.refresh();
		ok &= expect(panel.idEdit()->text() == "i1" && panel.listWidget()->isEnabled(), "the incidents can be edited again after the run");

		// another scene replaces unfinished text without an edit
		panel.idEdit()->setText("abandoned");
		SceneModel other = incidentScene();
		other.scenarios[0].incidents.resize(1);
		other.scenarios[0].incidents[0].id = "only";
		document.replace(other, false);
		ok &= expect(items(panel.listWidget()) == QStringList({"only: Signal failure"}) && panel.idEdit()->text() == "only" && !document.isDirty(),
			"opening another scene replaces unfinished text without an edit");

		// detach
		panel.setDocument(nullptr);
		ok &= expect(panel.listWidget()->count() == 0 && !panel.listWidget()->isEnabled() && panel.targetCombo()->count() == 1,
			"a detached panel shows nothing");
		const quint64 revisionDetached = document.revision();
		panel.addIncident();
		document.replace(incidentScene(), false);
		ok &= expect(panel.listWidget()->count() == 0 && document.revision() == revisionDetached + 1,
			"a detached panel neither edits nor follows the document");
	}

	{
		// the targets follow the document
		SceneDocument document;
		IncidentPanel panel;
		RunSelection selection;
		panel.setRunSelection(&selection);
		panel.setDocument(&document);
		document.replace(incidentScene(), false);
		selection.selectScenario("base");
		panel.refresh();
		panel.listWidget()->setCurrentRow(1);
		ok &= expect(panel.targetCombo()->count() == 5, "the breakdown can have four occurrences as its target");

		SceneModel& model = document.mutableModel();
		model.services.push_back(service("s3"));
		ok &= expect(panel.targetCombo()->count() == 5, "a service that the document has not announced is not offered");
		document.markChanged(SceneDocument::Services, "Add service");
		ok &= expect(panel.targetCombo()->count() == 6 && choice(panel.targetCombo(), "s3", 1) == 5 && panel.targetCombo()->currentIndex() == 2,
			"a service that is added is offered, and the target of the incident stays");

		model.services[0].id = "s1b";
		model.scenarios[0].incidents[1].target = "s1b";
		document.markChanged(SceneDocument::Services | SceneDocument::Scenarios, "Rename service");
		ok &= expect(choice(panel.targetCombo(), "s1b", 2) == 2 && panel.targetCombo()->currentIndex() == 2 && panel.targetCombo()->count() == 6,
			"a renamed service is offered under its new id, and the target of the incident follows it");

		model.services.erase(model.services.begin() + 2);
		document.markChanged(SceneDocument::Services, "Delete service");
		ok &= expect(panel.targetCombo()->count() == 5, "a service that is deleted is no longer offered");

		model.services.erase(model.services.begin());
		document.markChanged(SceneDocument::Services, "Delete service");
		ok &= expect(panel.targetCombo()->currentText() == "s1b #2 (missing)" && panel.targetCombo()->currentData().toString() == "s1b"
				&& document.model().scenarios[0].incidents[1].target == "s1b",
			"a target that is gone is shown as such and is not changed");

		model.settings.durationSeconds = 7200.0;
		model.services.insert(model.services.begin(), service("s1"));
		model.services[0].hasRepeat = true;
		model.services[0].headwaySeconds = 1200.0;
		document.markChanged(SceneDocument::CaseSettings, "Edit case settings");
		ok &= expect(panel.targetCombo()->count() == 9, "a longer case has more occurrences to choose from");
		panel.setOccurrenceDuration([] { return 2400.0; });
		document.markChanged(SceneDocument::CaseSettings, "Edit case settings");
		ok &= expect(panel.targetCombo()->count() == 5, "the duration that the window gives counts the occurrences");
		panel.setOccurrenceDuration({});

		panel.listWidget()->setCurrentRow(0);
		sceneSignals(model).push_back(SceneSignal{"sig2", ""});
		document.markChanged(SceneDocument::Infrastructure, "Add signal");
		ok &= expect(panel.targetCombo()->findText("sig2") >= 0, "a signal that is added is offered to a signal failure");
		model.blocks.erase(model.blocks.begin());
		document.markChanged(SceneDocument::Infrastructure, "Delete block");
		ok &= expect(panel.targetCombo()->findText("blk1") < 0 && panel.targetCombo()->findText("sig2") >= 0,
			"a block that is deleted is no longer offered");
		sceneSignals(model).erase(sceneSignals(model).begin());
		document.markChanged(SceneDocument::Infrastructure, "Delete signal");
		ok &= expect(panel.targetCombo()->currentText() == "sig1 (missing)" && panel.targetCombo()->currentData().toString() == "sig1",
			"a signal that is gone is shown as the removed target of the signal failure");
		sceneSignals(model).push_back(SceneSignal{"sig3", ""});
		document.markChanged(SceneDocument::Scenarios, "Edit scenario");
		ok &= expect(panel.targetCombo()->findText("sig3") < 0, "the targets stay as they are when only scenarios change");
	}

	{
		auto* document = new SceneDocument;
		IncidentPanel panel;
		panel.setDocument(document);
		document->replace(incidentScene(), false);
		delete document;
		panel.idEdit()->setText("after the document");
		panel.commitIdEdit();
		panel.addIncident();
		panel.refresh();
		ok &= expect(panel.listWidget()->count() == 0 && !panel.addButton()->isEnabled(),
			"a panel whose document is gone shows nothing and edits nothing");
	}

	{
		// the empty state follows the list and the selection
		SceneDocument document;
		IncidentPanel panel;
		FakePrompts prompts;
		prompts.confirmAnswer = true;
		RunSelection selection;
		panel.setPrompts(&prompts);
		panel.setRunSelection(&selection);
		panel.setDocument(&document);
		ok &= expect(panel.emptyStateLabel()->isHidden(), "a panel without a scene does not point to Add Incident, which is disabled");
		document.replace(incidentScene(), false);
		selection.selectScenario("alt");
		panel.refresh();
		ok &= expect(panel.emptyStateLabel()->isHidden() && panel.emptyStateLabel()->text().isEmpty(),
			"a selected incident has no empty-state message");
		panel.deleteIncident();
		ok &= expect(panel.emptyStateLabel()->text() == "This scenario has no incidents. Use Add Incident."
				&& !panel.emptyStateLabel()->isHidden() && panel.emptyStateLabel()->property("dialogHelp").toBool()
				&& panel.addButton()->isEnabled() && !panel.idEdit()->isEnabled(),
			"a scenario without incidents says so, and the Add button it points to is enabled");
		panel.addIncident();
		ok &= expect(panel.emptyStateLabel()->isHidden(), "adding an incident removes the message");
		panel.listWidget()->setCurrentRow(-1);
		ok &= expect(panel.emptyStateLabel()->text() == "Select an incident or use Add Incident.",
			"a scenario with incidents and no selected row asks for a selection");
		document.setEditable(false);
		panel.refresh();
		ok &= expect(panel.emptyStateLabel()->isHidden(), "a scene that a run uses does not point to the disabled Add button");
	}

	{
		// a check box stands in front of the field it switches on
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
		ok &= expect(form && labelOf(panel.endSecondsEdit()) == panel.hasEndSecondsCheck()
				&& labelOf(panel.reducedSpeedKmhEdit()) == panel.hasReducedSpeedCheck(),
			"the recovery end and the reduced speed are fields of rows that start with their check boxes");
		ok &= expect(form && qobject_cast<QLabel*>(labelOf(panel.startSecondsEdit())) && qobject_cast<QLabel*>(labelOf(panel.idEdit())),
			"the other fields have text labels");
		panel.resize(420, 700);
		panel.show();
		QApplication::processEvents();
		const auto topLeft = [&panel](const QWidget* widget) { return widget->mapTo(&panel, QPoint(0, 0)); };
		const auto precedes = [&topLeft](const QWidget* check, const QWidget* field) {
			return topLeft(check).y() + check->height() <= topLeft(field).y();
		};
		ok &= expect(precedes(panel.hasEndSecondsCheck(), panel.endSecondsEdit())
				&& precedes(panel.hasReducedSpeedCheck(), panel.reducedSpeedKmhEdit()),
			"each check box is drawn above the field it switches on");
		const auto inside = [&panel, &topLeft](const QWidget* widget) { return panel.rect().contains(QRect(topLeft(widget), widget->size())); };
		ok &= expect(inside(panel.addButton()) && inside(panel.idEdit()), "the Add button and the first field are inside the panel");
		ok &= expect(panel.hasEndSecondsCheck()->text() == "End (s)", "the check box of the recovery end names its unit");
	}

	{
		// a scene without a scenario has no Add button that works, so the empty state does not point to it
		SceneModel scene = incidentScene();
		scene.scenarios.clear();
		SceneDocument document;
		IncidentPanel panel;
		panel.setDocument(&document);
		document.replace(scene, false);
		ok &= expect(panel.emptyStateLabel()->isHidden(), "without a scenario the empty state does not point to Add Incident");
	}

	{
		// the target can be picked on the network view
		SceneDocument document;
		IncidentPanel panel;
		RunSelection selection;
		panel.setRunSelection(&selection);
		QToolButton* pick = panel.pickTargetButton();
		ok &= expect(pick && pick->objectName() == "incidentPickTargetButton" && pick->isCheckable() && !pick->isEnabled()
				&& pick->accessibleName() == "Pick target on network view"
				&& pick->toolTip() == "Pick the target on the network view (Esc cancels)" && (pick->focusPolicy() & Qt::TabFocus),
			"a checkable pick button that Tab reaches stands next to the target combo, disabled without an incident");
		ok &= expect(pick && pick->parentWidget() == panel.targetCombo()->parentWidget(), "the pick button is in the fields of the incident");
		panel.setDocument(&document);
		document.replace(incidentScene(), false);
		selection.selectScenario("base");
		panel.refresh();
		Events events(panel, document);
		std::vector<bool> toggles;
		QObject::connect(&panel, &IncidentPanel::targetPickToggled, &panel, [&toggles](bool picking) { toggles.push_back(picking); });
		ok &= expect(pick->isEnabled() && panel.targetCombo()->isEnabled() && !panel.isPickingTarget(),
			"the pick button is enabled with the target combo");
		pick->click();
		ok &= expect(pick->isChecked() && panel.isPickingTarget() && toggles == std::vector<bool>{true} && events.log.empty(),
			"clicking the pick button starts picking and edits nothing");
		pick->click();
		ok &= expect(!panel.isPickingTarget() && toggles == std::vector<bool>({true, false}), "a second click ends picking");
		pick->click();
		panel.endTargetPick();
		ok &= expect(!pick->isChecked() && toggles == std::vector<bool>({true, false, true, false}), "the window can end picking");
		panel.endTargetPick();
		ok &= expect(toggles.size() == 4, "ending picking that has ended does nothing");
		pick->click();
		panel.listWidget()->setCurrentRow(1);
		ok &= expect(!panel.isPickingTarget(), "selecting another incident ends picking");
		pick->click();
		panel.refresh();
		ok &= expect(!panel.isPickingTarget(), "showing the incidents again, as after another scene or scenario, ends picking");
		panel.show();
		pick->click();
		panel.hide();
		ok &= expect(!panel.isPickingTarget(), "hiding the panel ends picking");
		pick->click();
		document.setEditable(false);
		panel.refreshTargets();
		ok &= expect(!panel.isPickingTarget() && !pick->isEnabled() && !panel.targetCombo()->isEnabled(),
			"picking ends, and the button is disabled with the combo, while a run uses the scene");
		ok &= expect(!panel.applyTarget({"signal_failure", "sig1", 0}) && events.log.empty(), "no target is taken while a run uses the scene");
		document.setEditable(true);
		panel.refresh();
		ok &= expect(pick->isEnabled(), "the pick button is enabled again after the run");

		// a picked target
		const auto shown = [&document]() -> const SceneIncident& { return document.model().scenarios[0].incidents[0]; };
		panel.listWidget()->setCurrentRow(0);
		events.log.clear();
		ok &= expect(panel.applyTarget({"signal_failure", "blk1", 0}) && shown().target == "blk1" && !shown().hasOccurrence
				&& events.log == edit && document.lastChangeLabel() == "Change incident target" && panel.targetCombo()->currentText() == "blk1",
			"a block is taken as the target of a signal failure");
		events.log.clear();
		ok &= expect(panel.applyTarget({"signal_failure", "blk1", 0}) && events.log.empty(), "the target that the incident has changes nothing");
		ok &= expect(!panel.applyTarget({"signal_failure", "@blk1@", 0}) && !panel.applyTarget({"signal_failure", "nope", 0})
				&& !panel.applyTarget({"signal_failure", "", 0}) && !panel.applyTarget({"train_breakdown", "sig1", 1})
				&& !panel.applyTarget({"train_breakdown", "s2", 2}) && !panel.applyTarget({"train_breakdown", "s1", 0})
				&& !panel.applyTarget({"train_breakdown", "s1", 4}) && !panel.applyTarget({"other", "s1", 1}) && !panel.applyTarget({})
				&& events.log.empty() && shown().type == "signal_failure" && shown().target == "blk1",
			"a target that the combo does not offer for its type is refused and changes nothing");
		ok &= expect(panel.applyTarget({"train_breakdown", "s1", 3}) && shown().type == "train_breakdown" && shown().target == "s1"
				&& shown().hasOccurrence && shown().occurrence == 3 && events.log == edit
				&& document.lastChangeLabel() == "Change incident target" && panel.typeCombo()->currentData().toString() == "train_breakdown"
				&& panel.targetCombo()->currentIndex() == choice(panel.targetCombo(), "s1", 3) && panel.hasReducedSpeedCheck()->isEnabled()
				&& panel.listWidget()->item(0)->text() == "i1: Train breakdown #3",
			"an occurrence of a service makes a signal failure a breakdown of it, in one edit");
		panel.hasReducedSpeedCheck()->setChecked(true);
		panel.terminateAtDestinationCheck()->setChecked(true);
		events.log.clear();
		ok &= expect(panel.applyTarget({"signal_failure", "sig1", 0}) && shown().type == "signal_failure" && shown().target == "sig1"
				&& !shown().hasOccurrence && shown().occurrence == 1 && !shown().hasReducedSpeed && shown().reducedSpeedKmh == 0.0
				&& !shown().terminateAtDestination && events.log == edit && panel.typeCombo()->currentData().toString() == "signal_failure"
				&& panel.targetCombo()->currentText() == "sig1" && panel.listWidget()->item(0)->text() == "i1: Signal failure",
			"a signal makes a breakdown a signal failure, which loses what only a breakdown has, in one edit");

		// the target of a new incident
		const std::size_t countBefore = document.model().scenarios[0].incidents.size();
		std::size_t countAtCall = 0;
		panel.setDefaultTarget([&document, &countAtCall] {
			countAtCall = document.model().scenarios[0].incidents.size();
			return IncidentTarget{"train_breakdown", "s1", 2};
		});
		events.log.clear();
		panel.addIncident();
		const SceneIncident& added = document.model().scenarios[0].incidents.back();
		ok &= expect(countAtCall == countBefore && added.id == "new_incident" && added.type == "train_breakdown" && added.target == "s1"
				&& added.hasOccurrence && added.occurrence == 2 && added.hasEndSeconds && events.log == edit
				&& document.lastChangeLabel() == "Add incident" && panel.typeCombo()->currentData().toString() == "train_breakdown"
				&& panel.targetCombo()->currentIndex() == choice(panel.targetCombo(), "s1", 2)
				&& panel.listWidget()->currentItem()->text() == "new_incident: Train breakdown #2",
			"a new incident gets the target that the window gives before the edit, in the same edit");
		panel.setDefaultTarget([] { return IncidentTarget{"signal_failure", "sig1", 0}; });
		panel.addIncident();
		ok &= expect(document.model().scenarios[0].incidents.back().type == "signal_failure"
				&& document.model().scenarios[0].incidents.back().target == "sig1",
			"a new incident can be a signal failure of the signal that the window gives");
		panel.setDefaultTarget([] { return IncidentTarget{"signal_failure", "missing", 0}; });
		panel.addIncident();
		ok &= expect(document.model().scenarios[0].incidents.back().type == "signal_failure"
				&& document.model().scenarios[0].incidents.back().target.empty(),
			"a target that the combo does not offer leaves the new incident without a target");
		panel.setDefaultTarget([] { return IncidentTarget(); });
		panel.addIncident();
		ok &= expect(document.model().scenarios[0].incidents.back().type == "signal_failure"
				&& document.model().scenarios[0].incidents.back().target.empty(),
			"without a target from the window a new incident is a signal failure without a target");
		panel.setDefaultTarget({});
		panel.addIncident();
		ok &= expect(document.model().scenarios[0].incidents.size() == countBefore + 5
				&& document.model().scenarios[0].incidents.back().target.empty(),
			"a panel without a target provider adds a signal failure without a target");
	}

	{
		// a picked occurrence after the ones that the combo lists
		SceneDocument document;
		IncidentPanel panel;
		RunSelection selection;
		panel.setRunSelection(&selection);
		panel.setDocument(&document);
		SceneModel scene = incidentScene();
		SceneService many = service("many");
		many.hasRepeat = true;
		many.hasRepeatCount = true;
		many.repeatCount = RuntimeLimits::kMaxExpandedTrains + 1;
		scene.services.push_back(many);
		document.replace(scene, false);
		selection.selectScenario("base");
		panel.refresh();
		panel.listWidget()->setCurrentRow(1);
		const int last = many.repeatCount;
		ok &= expect(choice(panel.targetCombo(), "many", last) < 0, "the combo does not list the last occurrence of a service over the limit");
		ok &= expect(panel.applyTarget({"train_breakdown", "many", last}) && document.model().scenarios[0].incidents[1].target == "many"
				&& document.model().scenarios[0].incidents[1].occurrence == last
				&& panel.targetCombo()->currentIndex() == choice(panel.targetCombo(), "many", last),
			"an occurrence that the combo does not list is taken and becomes its current choice");
	}

	{
		// In the form of macOS the fields grow to the width of the panel, and a long target is cut in the middle.
		QApplication::setStyle(new MacFormStyle);
		SceneDocument document;
		IncidentPanel panel;
		RunSelection selection;
		panel.setRunSelection(&selection);
		panel.setDocument(&document);
		SceneModel scene = incidentScene();
		scene.services[0].route = std::string(200, 'r');
		document.replace(scene, false);
		selection.selectScenario("base");
		panel.refresh();
		panel.listWidget()->setCurrentRow(1);
		panel.resize(320, 700);
		panel.show();
		QApplication::processEvents();
		const QFormLayout* form = panel.idEdit()->parentWidget()->findChild<QFormLayout*>();
		ok &= expect(form && form->fieldGrowthPolicy() == QFormLayout::AllNonFixedFieldsGrow, "the fields of the incident grow to the width of the form");
		const auto* target = dynamic_cast<const ChoiceComboBox*>(panel.targetCombo());
		ok &= expect(target && target->currentText().startsWith("s1-2 (route rrr") && target->displayText().contains(QChar(0x2026)),
			"a long target is cut with an ellipsis in a narrow panel");
		// The tip of the target says what the type takes as its target, below the current target.
		const QString trainTip = panel.targetCombo()->toolTip();
		ok &= expect(trainTip.contains("train")
				&& shownTip(panel.targetCombo())
					== panel.targetCombo()->currentText() + "\n" + panel.targetCombo()->currentData(Qt::ToolTipRole).toString() + "\n" + trainTip,
			"the tip of the target of a breakdown names a train, below the current target and its identity");
		panel.listWidget()->setCurrentRow(0);
		const QString signalTip = panel.targetCombo()->toolTip();
		ok &= expect(!signalTip.isEmpty() && !signalTip.contains("service", Qt::CaseInsensitive) && !signalTip.contains("train", Qt::CaseInsensitive),
			"the tip of the target of a signal failure names no service or train");
	}

	// A signal failure names its targets by the labels of their sections and keeps their ids.
	{
		SceneModel scene = incidentScene();
		scene.tracks = {{"T1"}};
		scene.nodes = {{"n1", "T1", 0.0, 0.0}, {"n2", "T1", 2.0, 0.0}};
		scene.arcs = {{"a1", "T1", "n1", "n2", 0.0, 0.0, 20.0}};
		scene.blocks = {{"blk1", "T1", 1.0}, {"s2", "T1", 1.0}};
		sceneSignals(scene)[0].protectedSection = "blk1";
		SceneDocument document;
		IncidentPanel panel;
		FakePrompts prompts;
		RunSelection selection;
		panel.setPrompts(&prompts);
		panel.setRunSelection(&selection);
		panel.setDocument(&document);
		document.replace(scene, false);
		selection.selectScenario("base");
		panel.refresh();
		panel.listWidget()->setCurrentRow(0);
		const QComboBox* target = panel.targetCombo();
		ok &= expect(choices(target) == QStringList({"Choose target", "sig1 (T1 0.000 to 1.000 km)", "T1 0.000 to 1.000 km", "T1 1.000 to 2.000 km"})
				&& target->currentIndex() == 1 && target->itemData(1, Qt::ToolTipRole).toString() == "sig1"
				&& target->itemData(2).toString() == "blk1" && target->itemData(2, Qt::ToolTipRole).toString() == "blk1",
			"the targets of a signal failure are named by their labels, with the id as data and tooltip");
		panel.targetCombo()->setCurrentIndex(3);
		ok &= expect(document.model().scenarios[0].incidents[0].target == "s2" && panel.targetCombo()->currentText() == "T1 1.000 to 2.000 km",
			"a block chosen by its label is saved by its id");
	}

	if (!ok)
		return 1;
	std::cout << "all incident panel tests passed\n";
	return 0;
}

// The window adds an incident with the target of a signal or a train of the network view: the panel takes that target
// instead of the default one, in the one edit that adds the incident, and selects the new row.
static bool addsIncidentWithTarget(const Log& edit) {
	bool ok = true;
	SceneDocument document;
	IncidentPanel panel;
	RunSelection selection;
	panel.setRunSelection(&selection);
	panel.setDocument(&document);
	document.replace(incidentScene(), false);
	selection.selectScenario("base");
	panel.refresh();
	panel.listWidget()->setCurrentRow(0);
	int defaultAsked = 0;
	panel.setDefaultTarget([&defaultAsked] {
		++defaultAsked;
		return IncidentTarget{"signal_failure", "sig1", 0};
	});
	Events events(panel, document);
	const std::vector<SceneIncident>& incidents = document.model().scenarios[0].incidents;
	const std::size_t countBefore = incidents.size();
	ok &= expect(panel.addIncident(IncidentTarget{"train_breakdown", "s1", 2}) && incidents.size() == countBefore + 1
			&& incidents.back().id == "new_incident" && incidents.back().type == "train_breakdown" && incidents.back().target == "s1"
			&& incidents.back().hasOccurrence && incidents.back().occurrence == 2 && incidents.back().hasEndSeconds
			&& incidents.back().startSeconds == 0.0 && events.log == edit && document.lastChangeLabel() == "Add incident"
			&& defaultAsked == 0 && panel.listWidget()->currentRow() == static_cast<int>(countBefore)
			&& panel.selectedIncident() == &document.mutableModel().scenarios[0].incidents.back()
			&& panel.targetCombo()->currentIndex() == choice(panel.targetCombo(), "s1", 2),
		"an incident added with a target has that target and its type in one edit, and its row is selected");
	events.log.clear();
	ok &= expect(panel.addIncident(IncidentTarget{"signal_failure", "blk1", 0}) && incidents.back().type == "signal_failure"
			&& incidents.back().target == "blk1" && !incidents.back().hasOccurrence && events.log == edit && defaultAsked == 0
			&& panel.listWidget()->currentRow() == static_cast<int>(countBefore) + 1,
		"an incident added with a block is a signal failure of it");
	events.log.clear();
	ok &= expect(!panel.addIncident(IncidentTarget{"signal_failure", "missing", 0}) && !panel.addIncident(IncidentTarget{"train_breakdown", "s1", 4})
			&& !panel.addIncident(IncidentTarget()) && incidents.size() == countBefore + 2 && events.log.empty(),
		"a target that the combo does not offer adds nothing");
	document.setEditable(false);
	ok &= expect(!panel.addIncident(IncidentTarget{"signal_failure", "sig1", 0}) && incidents.size() == countBefore + 2 && events.log.empty(),
		"nothing is added while a run uses the scene");
	return ok;
}

// The type combo shows the names of the types and keeps the type of the file as the data of each item: choosing an item
// by its name stores that type, and the file is written as before.
static bool namesTypes(const Log& edit) {
	bool ok = true;
	SceneDocument document;
	IncidentPanel panel;
	RunSelection selection;
	panel.setRunSelection(&selection);
	panel.setDocument(&document);
	document.replace(incidentScene(), false);
	selection.selectScenario("base");
	panel.refresh();
	QComboBox* type = panel.typeCombo();
	ok &= expect(choices(type) == QStringList({"Signal failure", "Train breakdown"}) && type->itemData(0).toString() == "signal_failure"
			&& type->itemData(1).toString() == "train_breakdown" && type->currentText() == "Signal failure",
		"the types are shown by name, with the type of the file as data");
	const nlohmann::json before = encodeScenarios(document.model());
	Events events(panel, document);
	type->setCurrentIndex(type->findText("Train breakdown"));
	const nlohmann::json changed = encodeScenarios(document.model());
	ok &= expect(document.model().scenarios[0].incidents[0].type == "train_breakdown"
			&& changed["scenarios"][0]["incidents"][0]["type"] == "train_breakdown" && events.log == edit,
		"choosing a type by its name stores the type of the file");
	events.log.clear();
	type->setCurrentIndex(type->findText("Signal failure"));
	panel.targetCombo()->setCurrentIndex(panel.targetCombo()->findData("sig1"));
	ok &= expect(document.model().scenarios[0].incidents[0].type == "signal_failure" && encodeScenarios(document.model()) == before,
		"choosing the first type and target again writes the file as before");
	panel.listWidget()->setCurrentRow(1);
	ok &= expect(type->currentText() == "Train breakdown" && type->currentData().toString() == "train_breakdown",
		"a breakdown shows the name of its type");
	return ok;
}
