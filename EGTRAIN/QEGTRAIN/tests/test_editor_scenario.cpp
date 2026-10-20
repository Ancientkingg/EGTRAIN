#include "editors/ScenarioPanel.h"

#include "scene/SceneWriter.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <iostream>
#include <string>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static SceneIncident breakdown(const char* id, const char* target) {
	SceneIncident incident;
	incident.id = id;
	incident.type = "train_breakdown";
	incident.target = target;
	incident.startSeconds = 10.0;
	incident.endSeconds = 20.0;
	incident.hasEndSeconds = true;
	return incident;
}

// A service and two scenarios. The scenario base is the default and has nothing in it; alt has an incident and an
// entrance delay.
static SceneModel scenarioScene() {
	SceneModel scene;
	scene.name = "Scenarios";
	SceneService service;
	service.id = "s1";
	scene.services = {service};
	SceneScenario base;
	base.id = "base";
	base.name = "Baseline";
	base.description = "no incidents";
	SceneScenario alt;
	alt.id = "alt";
	alt.name = "Alternative";
	alt.incidents.push_back(breakdown("i1", "s1"));
	SceneEntranceDelay delay;
	delay.serviceId = "s1";
	delay.stationId = "st1";
	alt.entranceDelays.push_back(delay);
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
	int infoCount = 0;
	QString infoTitle;
	QString infoMessage;
	int confirmCount = 0;
	QString confirmQuestion;
	bool confirmAnswer = false;

	void showError(QWidget*, const QString& title, const QString& message, bool = false) override {
		++errorCount;
		errorTitle = title;
		errorMessage = message;
	}
	void showInfo(QWidget*, const QString& title, const QString& message) override {
		++infoCount;
		infoTitle = title;
		infoMessage = message;
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
	QStringList status;

	Events(ScenarioPanel& panel, SceneDocument& document) {
		QObject::connect(&document, &SceneDocument::changed, &panel, [this](SceneDocument::ChangeKinds changedKinds) {
			log.push_back("documentChanged");
			kinds = changedKinds;
		});
		QObject::connect(&panel, &ScenarioPanel::scenarioSelected, &panel,
			[this](const QString& id) { log.push_back("selected:" + id.toStdString()); });
		QObject::connect(&panel, &ScenarioPanel::statusMessage, &panel, [this](const QString& text, int) { status << text; });
		QObject::connect(&panel, &ScenarioPanel::changed, &panel, [this] { log.push_back("panelChanged"); });
	}
};

// How the list names a scenario: the id, a dash, and then the rest of the label.
static QString label(const char* id, const char* rest) {
	return QStringLiteral("%1 \u2014 %2").arg(QString::fromLatin1(id), QString::fromLatin1(rest));
}

static QStringList items(const QListWidget* list) {
	QStringList result;
	for (int row = 0; row < list->count(); ++row)
		result << list->item(row)->text();
	return result;
}

static SceneDiagnostic diagnostic(SceneSeverity severity, const char* path) {
	SceneDiagnostic result;
	result.severity = severity;
	result.code = "test.scenario";
	result.message = "a problem";
	result.path = path;
	return result;
}

static bool writeScenario(const QString& path, const SceneScenario& scenario) {
	return saveScenarioJson(scenario, path.toStdString()).success();
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;
	QTemporaryDir directory;
	ok &= expect(directory.isValid(), "a temporary folder is available");

	{
		ScenarioPanel panel;
		ok &= expect(panel.listWidget()->count() == 0 && !panel.listWidget()->isEnabled() && !panel.blankButton()->isEnabled()
				&& !panel.importButton()->isEnabled() && !panel.exportButton()->isEnabled() && !panel.idEdit()->isEnabled(),
			"a panel without a document shows nothing and offers nothing");
		SceneDocument document;
		panel.setDocument(&document);
		panel.addBlankScenario();
		panel.duplicateScenario();
		panel.deleteScenario();
		panel.importScenario();
		panel.exportScenario();
		panel.selectScenario(0);
		panel.commitIdEdit();
		panel.commitPendingEditors();
		ok &= expect(!panel.blankButton()->isEnabled() && document.revision() == 0 && !document.isDirty()
				&& document.model().scenarios.empty(),
			"a panel stays disabled while its document holds no scene, and its actions change nothing");
	}

	{
		SceneDocument document;
		ScenarioPanel panel;
		FakePrompts prompts;
		RunSelection selection;
		std::vector<SceneDiagnostic> diagnostics;
		bool advanced = false;
		panel.setPrompts(&prompts);
		panel.setRunSelection(&selection);
		panel.setDiagnostics(&diagnostics);
		panel.setAdvancedDetails([&advanced] { return advanced; });
		panel.setDocument(&document);
		Events events(panel, document);

		// refresh
		document.replace(scenarioScene(), false);
		ok &= expect(items(panel.listWidget())
					== QStringList({label("base", "Baseline | 0 incident(s) | 0 delay(s) | no incidents | default"),
						label("alt", "Alternative | 1 incident(s) | 1 delay(s)")})
				&& panel.listWidget()->currentRow() == 0 && selection.scenarioId() == "base" && panel.listWidget()->isEnabled(),
			"a new scene is listed, and its default scenario is selected");
		ok &= expect(panel.idEdit()->text() == "base" && panel.nameEdit()->text() == "Baseline"
				&& panel.descriptionEdit()->text() == "no incidents" && panel.idEdit()->isEnabled()
				&& panel.blankButton()->isEnabled() && panel.duplicateButton()->isEnabled() && panel.importButton()->isEnabled()
				&& panel.exportButton()->isEnabled() && !panel.deleteButton()->isEnabled(),
			"the selected scenario shows its fields, and the default scenario cannot be deleted");
		ok &= expect(events.log.empty() && !document.isDirty() && document.revision() == 1 && !selection.isScenarioModified("base"),
			"showing a scene edits nothing and selects nothing");

		// the labels
		advanced = true;
		panel.updatePresentation();
		ok &= expect(panel.listWidget()->item(0)->text() == label("base", "Baseline | no incidents | 0 incident(s) | 0 delay(s) | Ready | default")
				&& panel.listWidget()->item(1)->text() == label("alt", "Alternative | (no description) | 1 incident(s) | 1 delay(s) | Ready")
				&& panel.listWidget()->item(1)->toolTip() == panel.listWidget()->item(1)->text(),
			"the details add the description and the state of each scenario");
		diagnostics = {diagnostic(SceneSeverity::Warning, "scenarios[0].incidents[0]"), diagnostic(SceneSeverity::Error, "scenarios[1]"),
			diagnostic(SceneSeverity::Error, "scenarios[10]"), diagnostic(SceneSeverity::Error, "passengers[0]")};
		panel.updatePresentation();
		ok &= expect(panel.listWidget()->item(0)->text().contains("| Warning | default")
				&& panel.listWidget()->item(1)->text().endsWith("| Invalid"),
			"the state of a scenario follows the diagnostics under its path");
		advanced = false;
		panel.updatePresentation();
		ok &= expect(panel.listWidget()->item(0)->text() == label("base", "Baseline | 0 incident(s) | 0 delay(s) | no incidents | default")
				&& panel.listWidget()->item(1)->text() == label("alt", "Alternative | 1 incident(s) | 1 delay(s) | Invalid"),
			"the short list names only a scenario that is invalid");
		diagnostics.clear();
		selection.markScenarioModified("alt");
		panel.updatePresentation();
		ok &= expect(panel.listWidget()->item(1)->text().endsWith("| 1 delay(s) | modified"), "an edited scenario is marked");
		selection.clearModifiedScenarios();
		panel.updatePresentation();

		// selection
		panel.listWidget()->setCurrentRow(1);
		ok &= expect(selection.scenarioId() == "alt" && events.log == Log({"selected:alt"}) && !document.isDirty()
				&& document.revision() == 1 && panel.idEdit()->text() == "alt" && panel.nameEdit()->text() == "Alternative"
				&& panel.descriptionEdit()->text().isEmpty() && panel.deleteButton()->isEnabled(),
			"another selected scenario is kept in the run selection and shown, and the scene is not changed");
		events.log.clear();
		panel.selectScenario(1);
		ok &= expect(events.log.empty(), "selecting the scenario that is selected tells nobody");
		panel.selectScenario(7);
		panel.selectScenario(-1);
		ok &= expect(events.log.empty() && selection.scenarioId() == "alt", "a position outside the scene selects nothing");

		// edits of the fields
		events.log.clear();
		panel.nameEdit()->setText("Alternative route");
		panel.commitNameEdit();
		ok &= expect(document.model().scenarios[1].name == "Alternative route" && document.isDirty() && selection.isScenarioModified("alt")
				&& events.log == Log({"documentChanged", "panelChanged"}) && events.kinds == SceneDocument::Scenarios
				&& document.lastChangeLabel() == "Change scenario name"
				&& panel.listWidget()->item(1)->text() == label("alt", "Alternative route | 1 incident(s) | 1 delay(s) | modified"),
			"a name is written to the document, the scenario counts as edited and the list shows the name");
		events.log.clear();
		panel.commitNameEdit();
		ok &= expect(events.log.empty(), "an unchanged name does not touch the document");
		panel.descriptionEdit()->setText("a longer route");
		panel.commitDescriptionEdit();
		ok &= expect(document.model().scenarios[1].description == "a longer route" && document.lastChangeLabel() == "Change scenario description"
				&& events.log == Log({"documentChanged", "panelChanged"}),
			"a description is written to the document");

		// rename: the default scenario and the selection follow
		events.log.clear();
		panel.idEdit()->setText("  alt2  ");
		panel.commitIdEdit();
		ok &= expect(document.model().scenarios[1].id == "alt2" && selection.scenarioId() == "alt2" && selection.isScenarioModified("alt2")
				&& !selection.isScenarioModified("alt") && document.model().defaultScenarioId == "base"
				&& document.lastChangeLabel() == "Rename scenario" && events.log == Log({"documentChanged", "panelChanged"})
				&& panel.listWidget()->item(1)->text().startsWith(label("alt2", "")),
			"a new id is written to the document, trimmed, and the selection follows it");
		panel.listWidget()->setCurrentRow(0);
		selection.markScenarioModified("base");
		events.log.clear();
		panel.idEdit()->setText("baseline");
		panel.commitIdEdit();
		ok &= expect(document.model().scenarios[0].id == "baseline" && document.model().defaultScenarioId == "baseline"
				&& selection.scenarioId() == "baseline" && selection.isScenarioModified("baseline") && !selection.isScenarioModified("base")
				&& panel.listWidget()->item(0)->text().endsWith("| default | modified"),
			"renaming the default scenario moves the default along with it");
		prompts.errorCount = 0;
		events.log.clear();
		panel.idEdit()->setText("alt2");
		panel.commitIdEdit();
		ok &= expect(prompts.errorCount == 1 && prompts.errorTitle == "Scenario ID already exists" && panel.idEdit()->text() == "baseline"
				&& document.model().scenarios[0].id == "baseline" && events.log.empty(),
			"an id that another scenario has is refused, and the field shows the old id again");
		panel.idEdit()->setText("   ");
		panel.commitIdEdit();
		ok &= expect(prompts.errorCount == 2 && prompts.errorTitle == "Invalid Scenario ID" && panel.idEdit()->text() == "baseline"
				&& events.log.empty(),
			"an empty id is refused, and the field shows the old id again");
		panel.idEdit()->setText("baseline");
		panel.commitIdEdit();
		ok &= expect(prompts.errorCount == 2 && events.log.empty(), "an unchanged id does not touch the document");

		// pending edits
		document.markSaved();
		events.log.clear();
		panel.nameEdit()->setText("Pending name");
		panel.commitPendingEditors();
		ok &= expect(document.model().scenarios[0].name == "Pending name" && document.isDirty(), "unfinished text is taken into the document");
		panel.commitPendingEditors();
		ok &= expect(events.log == Log({"documentChanged", "panelChanged"}), "committing again changes nothing");

		// the scene is in use by a run
		document.markSaved();
		events.log.clear();
		document.setEditable(false);
		panel.refresh();
		ok &= expect(!panel.listWidget()->isEnabled() && !panel.idEdit()->isEnabled() && !panel.nameEdit()->isEnabled()
				&& !panel.descriptionEdit()->isEnabled() && !panel.blankButton()->isEnabled() && !panel.duplicateButton()->isEnabled()
				&& !panel.deleteButton()->isEnabled() && !panel.importButton()->isEnabled() && !panel.exportButton()->isEnabled()
				&& panel.idEdit()->text().isEmpty(),
			"nothing can be done with the scenarios, and the fields are empty, while a run uses the scene");
		panel.nameEdit()->setText("while running");
		prompts.errorCount = 0;
		panel.commitPendingEditors();
		panel.commitIdEdit();
		panel.commitNameEdit();
		panel.commitDescriptionEdit();
		panel.selectScenario(1);
		panel.addBlankScenario();
		panel.duplicateScenario();
		panel.deleteScenario();
		panel.importScenario();
		ok &= expect(document.model().scenarios[0].name == "Pending name" && document.model().scenarios.size() == 2 && events.log.empty()
				&& selection.scenarioId() == "baseline" && prompts.errorCount == 0,
			"pending text, selections and actions change nothing and report nothing while a run uses the scene");
		document.setEditable(true);
		panel.refresh();
		ok &= expect(panel.idEdit()->text() == "baseline" && panel.listWidget()->isEnabled(), "the scenarios can be edited again after the run");
		document.replace(scenarioScene(), false);
		selection.selectScenario("base");
		selection.clearModifiedScenarios();
		panel.refresh();

		// add a blank scenario
		events.log.clear();
		panel.addBlankScenario();
		ok &= expect(document.model().scenarios.size() == 3 && document.model().scenarios[2].id == "new_scenario"
				&& document.model().scenarios[2].name == "New scenario" && selection.scenarioId() == "new_scenario"
				&& selection.isScenarioModified("new_scenario") && panel.listWidget()->count() == 3 && panel.listWidget()->currentRow() == 2
				&& panel.idEdit()->text() == "new_scenario" && document.isDirty() && document.lastChangeLabel() == "Add scenario"
				&& events.kinds == SceneDocument::Scenarios
				&& events.log == Log({"documentChanged", "selected:new_scenario", "panelChanged"}),
			"a blank scenario is added, selected and shown, and the window is told which scenario to show");
		panel.addBlankScenario();
		ok &= expect(document.model().scenarios.back().id == "new_scenario_2" && selection.scenarioId() == "new_scenario_2",
			"the id of another blank scenario is unique");

		// duplicate
		panel.listWidget()->setCurrentRow(1);
		events.log.clear();
		panel.duplicateScenario();
		const SceneScenario& copy = document.model().scenarios.back();
		ok &= expect(document.model().scenarios.size() == 5 && copy.id == "alt_copy" && copy.name == "Copy of Alternative"
				&& copy.incidents.size() == 1 && copy.incidents[0].id == "i1_copy" && copy.entranceDelays.size() == 1
				&& document.model().scenarios[1].incidents[0].id == "i1" && selection.scenarioId() == "alt_copy"
				&& document.lastChangeLabel() == "Duplicate scenario"
				&& events.log == Log({"documentChanged", "selected:alt_copy", "panelChanged"}),
			"a copy has an id of its own, unique incident ids and the entries of the original, and is selected");

		// delete
		prompts.errorCount = 0;
		prompts.confirmCount = 0;
		panel.listWidget()->setCurrentRow(0);
		events.log.clear();
		panel.deleteScenario();
		ok &= expect(prompts.errorCount == 1 && prompts.errorTitle == "Cannot Delete Scenario"
				&& prompts.errorMessage == "The default scenario cannot be deleted." && prompts.confirmCount == 0
				&& document.model().scenarios.size() == 5 && events.log.empty(),
			"the default scenario is not deleted");
		panel.listWidget()->setCurrentRow(1);
		events.log.clear();
		panel.deleteScenario();
		ok &= expect(prompts.confirmCount == 1 && prompts.confirmQuestion == "Delete scenario 'alt'?" && document.model().scenarios.size() == 5
				&& events.log.empty(),
			"declining the question keeps the scenario");
		prompts.confirmAnswer = true;
		panel.deleteScenario();
		ok &= expect(document.model().scenarios.size() == 4 && document.model().scenarios[1].id == "new_scenario"
				&& selection.scenarioId() == "base" && panel.listWidget()->count() == 4 && panel.listWidget()->currentRow() == 0
				&& document.lastChangeLabel() == "Delete scenario"
				&& events.log == Log({"documentChanged", "selected:base", "panelChanged"}),
			"a deleted scenario is removed and the default scenario is selected in its place");

		// import
		const QString dir = directory.path();
		SceneScenario imported;
		imported.id = "imported";
		imported.name = "Imported";
		imported.incidents.push_back(breakdown("i9", "s1"));
		ok &= expect(writeScenario(dir + "/imported.json", imported), "the scenario file is written");
		QString chosen;
		panel.setImportFileChooser([&chosen] { return chosen; });
		prompts.errorCount = 0;
		events.log.clear();
		events.status.clear();
		panel.importScenario();
		ok &= expect(document.model().scenarios.size() == 4 && events.log.empty() && prompts.errorCount == 0, "cancelling the file chooser imports nothing");
		chosen = dir + "/imported.json";
		panel.importScenario();
		ok &= expect(document.model().scenarios.size() == 5 && document.model().scenarios.back().id == "imported"
				&& document.model().scenarios.back().incidents.size() == 1 && selection.scenarioId() == "imported"
				&& selection.isScenarioModified("imported") && document.lastChangeLabel() == "Import scenario" && prompts.infoCount == 0
				&& events.status == QStringList({"Imported scenario imported"})
				&& events.log == Log({"documentChanged", "selected:imported", "panelChanged"}),
			"an imported scenario is added and selected, and nothing needs to be explained");

		// the id and the incident id of the file are in use
		SceneScenario conflicting;
		conflicting.id = "imported";
		conflicting.name = "Again";
		conflicting.incidents.push_back(breakdown("i9", "s1"));
		ok &= expect(writeScenario(dir + "/conflicting.json", conflicting), "the conflicting scenario file is written");
		chosen = dir + "/conflicting.json";
		events.log.clear();
		events.status.clear();
		panel.importScenario();
		ok &= expect(document.model().scenarios.size() == 6 && document.model().scenarios.back().id == "imported_2"
				&& document.model().scenarios.back().incidents[0].id == "i9_2" && selection.scenarioId() == "imported_2"
				&& document.model().scenarios[4].incidents[0].id == "i9",
			"a scenario id and an incident id that are in use are made unique");
		const QString explanation = "Imported scenario imported_2 (ID imported already existed; imported as imported_2) 1 incident ID(s) adjusted to remain globally unique.";
		ok &= expect(events.status == QStringList({explanation}) && prompts.infoCount == 1 && prompts.infoTitle == "Scenario ID adjusted"
				&& prompts.infoMessage == explanation,
			"the user is told which ids were changed");

		// files that cannot be imported
		SceneScenario unusable;
		unusable.id = "unusable";
		unusable.name = "Unusable";
		unusable.incidents.push_back(breakdown("i10", "gone"));
		ok &= expect(writeScenario(dir + "/unusable.json", unusable), "the unusable scenario file is written");
		chosen = dir + "/unusable.json";
		prompts.errorCount = 0;
		events.log.clear();
		panel.importScenario();
		ok &= expect(prompts.errorCount == 1 && prompts.errorTitle == "Scenario import rejected"
				&& prompts.errorMessage.contains("unknown service") && document.model().scenarios.size() == 6 && events.log.empty()
				&& selection.scenarioId() == "imported_2",
			"a scenario that does not validate against the scene is refused, and the scene stays as it is");
		unusable.incidents[0] = breakdown("i10", "s1");
		unusable.incidents[0].type = "meteor";
		ok &= expect(writeScenario(dir + "/meteor.json", unusable), "the scenario file with an unknown incident type is written");
		chosen = dir + "/meteor.json";
		prompts.errorCount = 0;
		panel.importScenario();
		ok &= expect(prompts.errorCount == 1 && prompts.errorTitle == "Scenario import rejected"
				&& prompts.errorMessage.contains("Unknown incident type") && document.model().scenarios.size() == 6,
			"a scenario with an unknown incident type is refused");
		QFile broken(dir + "/broken.json");
		ok &= expect(broken.open(QIODevice::WriteOnly) && broken.write("{ not json") > 0, "the broken file is written");
		broken.close();
		chosen = dir + "/broken.json";
		panel.importScenario();
		ok &= expect(prompts.errorCount == 2 && prompts.errorTitle == "Scenario import failed" && document.model().scenarios.size() == 6,
			"a file that is not a scenario is refused");
		chosen = dir + "/missing.json";
		panel.importScenario();
		ok &= expect(prompts.errorCount == 3 && prompts.errorTitle == "Scenario import failed" && document.model().scenarios.size() == 6,
			"a file that is not there is refused");

		// export
		panel.listWidget()->setCurrentRow(1);
		QString suggested;
		QString target;
		panel.setExportFileChooser([&](const QString& name) {
			suggested = name;
			return target;
		});
		int observed = 0;
		std::vector<SceneDiagnostic> reported;
		panel.setExportObserver([&] {
			++observed;
			return [&reported](const std::vector<SceneDiagnostic>& diagnosticsOfFailure) { reported = diagnosticsOfFailure; };
		});
		events.log.clear();
		events.status.clear();
		panel.exportScenario();
		ok &= expect(observed == 1 && suggested == "new_scenario.json" && events.status.isEmpty() && events.log.empty() && reported.empty(),
			"cancelling the file chooser exports nothing, and the file chooser suggests the id of the scenario");
		target = dir + "/exported";
		panel.exportScenario();
		const ScenarioLoadResult exported = loadScenarioJson((dir + "/exported.json").toStdString());
		ok &= expect(exported.success() && exported.scenario.id == "new_scenario" && exported.scenario.name == "New scenario"
				&& events.status == QStringList({"Exported scenario new_scenario"}) && reported.empty() && events.log.empty(),
			"the selected scenario is written to the file, which gets the extension .json");
		panel.listWidget()->setCurrentRow(4);
		target = dir + "/exported_imported.json";
		panel.exportScenario();
		const ScenarioLoadResult exportedImport = loadScenarioJson(target.toStdString());
		ok &= expect(exportedImport.success() && exportedImport.scenario.id == "imported" && exportedImport.scenario.incidents.size() == 1
				&& exportedImport.scenario.incidents[0].id == "i9",
			"an exported scenario can be read again with its incidents");
		events.status.clear();
		prompts.errorCount = 0;
		target = dir + "/no such folder/exported.json";
		panel.exportScenario();
		ok &= expect(prompts.errorCount == 1 && prompts.errorTitle == "Scenario export failed" && !reported.empty() && events.status.isEmpty(),
			"a file that cannot be written is reported to the user and to the observer");
		panel.setExportObserver({});
		panel.exportScenario();
		ok &= expect(prompts.errorCount == 2, "an export without an observer still reports the failure to the user");
	}

	{
		// a scene that has no default scenario on record cannot lose a scenario
		SceneDocument document;
		ScenarioPanel panel;
		FakePrompts prompts;
		panel.setPrompts(&prompts);
		panel.setDocument(&document);
		SceneModel scene = scenarioScene();
		scene.defaultScenarioId.clear();
		document.replace(scene, false);
		panel.listWidget()->setCurrentRow(1);
		ok &= expect(!panel.deleteButton()->isEnabled(), "Delete is off while the scene names no default scenario");
		panel.deleteScenario();
		ok &= expect(prompts.errorCount == 1 && prompts.errorTitle == "Cannot Delete Scenario"
				&& prompts.errorMessage.startsWith("The scene has no valid persisted default scenario") && document.model().scenarios.size() == 2,
			"a scenario is not deleted from a scene without a default scenario");
	}

	{
		// another scene replaces the scenarios and the unfinished text
		SceneDocument document;
		ScenarioPanel panel;
		RunSelection selection;
		panel.setRunSelection(&selection);
		panel.setDocument(&document);
		document.replace(scenarioScene(), false);
		panel.nameEdit()->setText("abandoned");
		SceneModel other;
		SceneScenario only;
		only.id = "only";
		only.name = "Only";
		other.scenarios = {only};
		other.defaultScenarioId = "only";
		selection.selectScenario("only");
		document.replace(other, false);
		ok &= expect(items(panel.listWidget()) == QStringList({label("only", "Only | 0 incident(s) | 0 delay(s) | default")}) && panel.nameEdit()->text() == "Only"
				&& panel.listWidget()->currentRow() == 0 && !document.isDirty(),
			"opening another scene replaces the scenarios and unfinished text without an edit");

		// detach
		panel.setDocument(nullptr);
		ok &= expect(panel.listWidget()->count() == 0 && !panel.listWidget()->isEnabled() && panel.idEdit()->text().isEmpty(),
			"a detached panel shows nothing");
		const quint64 revisionDetached = document.revision();
		panel.addBlankScenario();
		document.replace(scenarioScene(), false);
		ok &= expect(panel.listWidget()->count() == 0 && document.revision() == revisionDetached + 1,
			"a detached panel neither edits nor follows the document");
	}

	{
		// a scene without an id selected falls back to its default scenario
		SceneDocument document;
		ScenarioPanel panel;
		RunSelection selection;
		panel.setRunSelection(&selection);
		panel.setDocument(&document);
		SceneModel scene = scenarioScene();
		scene.defaultScenarioId = "alt";
		document.replace(scene, false);
		ok &= expect(selection.scenarioId() == "alt" && panel.listWidget()->currentRow() == 1 && panel.idEdit()->text() == "alt",
			"the default scenario is selected when no scenario is");
	}

	{
		auto* document = new SceneDocument;
		ScenarioPanel panel;
		panel.setDocument(document);
		document->replace(scenarioScene(), false);
		delete document;
		panel.nameEdit()->setText("after the document");
		panel.commitNameEdit();
		panel.addBlankScenario();
		panel.refresh();
		ok &= expect(panel.listWidget()->count() == 0 && !panel.blankButton()->isEnabled(),
			"a panel whose document is gone shows nothing and edits nothing");
	}

	{
		// the header text names the selected scenario and follows its id and name
		SceneDocument document;
		ScenarioPanel panel;
		RunSelection selection;
		panel.setRunSelection(&selection);
		QStringList headers;
		QObject::connect(&panel, &ScenarioPanel::headerTextChanged, &panel, [&headers](const QString& text) { headers << text; });
		panel.setDocument(&document);
		ok &= expect(panel.headerText() == "No scenario selected", "a panel without a scene names no scenario");
		document.replace(scenarioScene(), false);
		ok &= expect(panel.headerText() == "Scenario: base (Baseline)" && headers.last() == panel.headerText(),
			"the header names the default scenario when the scene is loaded");
		panel.listWidget()->setCurrentRow(1);
		ok &= expect(headers.last() == "Scenario: alt (Alternative)", "selecting another scenario changes the header");
		panel.nameEdit()->setText("Renamed");
		panel.commitNameEdit();
		ok &= expect(headers.last() == "Scenario: alt (Renamed)", "renaming the selected scenario changes the header");
		panel.idEdit()->setText("other");
		panel.commitIdEdit();
		ok &= expect(headers.last() == "Scenario: other (Renamed)", "changing the id of the selected scenario changes the header");
		panel.nameEdit()->setText("");
		panel.commitNameEdit();
		ok &= expect(panel.headerText() == "Scenario: other", "a scenario without a name is named by its id");
	}

	if (!ok)
		return 1;
	std::cout << "all scenario panel tests passed\n";
	return 0;
}
