#include "editors/ScenarioPanel.h"

#include "scene/SceneWriter.h"
#include "support/FakePrompts.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontMetrics>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScopeGuard>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <string>
#include <vector>

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

// A panel with prompts, a run selection, diagnostics and a choice of advanced details of its own on a document that
// shows scenarioScene(), and the events of both.
struct ScenarioFixture {
	SceneDocument document;
	ScenarioPanel panel;
	FakePrompts prompts;
	RunSelection selection;
	std::vector<SceneDiagnostic> diagnostics;
	bool advanced = false;
	Events events{panel, document};

	ScenarioFixture() {
		panel.setPrompts(&prompts);
		panel.setRunSelection(&selection);
		panel.setDiagnostics(&diagnostics);
		panel.setAdvancedDetails([this] { return advanced; });
		panel.setDocument(&document);
		document.replace(scenarioScene(), false);
	}
};

class TestScenarioPanel : public QObject {
	Q_OBJECT

private slots:
	void showsNothingWithoutScene();
	void showsNewScene();
	void labelsFollowDiagnostics();
	void selectsEditsAndRenamesScenarios();
	void addsDeletesImportsAndExportsScenarios();
	void keepsScenariosOfSceneWithoutDefault();
	void showsAnotherScene();
	void detachedPanelIgnoresDocument();
	void selectsDefaultScenario();
	void rowsFitCounts();
	void forgetsDeletedDocument();
	void headerFollowsSelectedScenario();
	void pendingEditsAddNoScenario();
};

void TestScenarioPanel::showsNothingWithoutScene() {
	ScenarioPanel panel;
	// A panel without a document shows nothing and offers nothing.
	QCOMPARE(panel.listWidget()->count(), 0);
	QVERIFY(!panel.listWidget()->isEnabled());
	QVERIFY(!panel.blankButton()->isEnabled());
	QVERIFY(!panel.importButton()->isEnabled());
	QVERIFY(!panel.exportButton()->isEnabled());
	QVERIFY(!panel.idEdit()->isEnabled());
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
	// A panel stays disabled while its document holds no scene, and its actions change nothing.
	QVERIFY(!panel.blankButton()->isEnabled());
	QCOMPARE(document.revision(), quint64(0));
	QVERIFY(!document.isDirty());
	QVERIFY(document.model().scenarios.empty());
}

void TestScenarioPanel::showsNewScene() {
	ScenarioFixture f;
	ScenarioPanel& panel = f.panel;
	// A new scene is listed, and its default scenario is selected.
	QCOMPARE(items(panel.listWidget()), QStringList({"Baseline (default)", "Alternative\n1 incident, 1 delay"}));
	QCOMPARE(panel.listWidget()->currentRow(), 0);
	QCOMPARE(f.selection.scenarioId(), "base");
	QVERIFY(panel.listWidget()->isEnabled());
	// The selected scenario shows its fields, and the default scenario cannot be deleted.
	QCOMPARE(panel.idEdit()->text(), "base");
	QCOMPARE(panel.nameEdit()->text(), "Baseline");
	QCOMPARE(panel.descriptionEdit()->text(), "no incidents");
	QVERIFY(panel.idEdit()->isEnabled());
	QVERIFY(panel.blankButton()->isEnabled());
	QVERIFY(panel.duplicateButton()->isEnabled());
	QVERIFY(panel.importButton()->isEnabled());
	QVERIFY(panel.exportButton()->isEnabled());
	QVERIFY(!panel.deleteButton()->isEnabled());
	// Showing a scene edits nothing and selects nothing.
	QVERIFY(f.events.log.empty());
	QVERIFY(!f.document.isDirty());
	QCOMPARE(f.document.revision(), quint64(1));
	QVERIFY(!f.selection.isScenarioModified("base"));
}

// The labels of the list.
void TestScenarioPanel::labelsFollowDiagnostics() {
	ScenarioFixture f;
	ScenarioPanel& panel = f.panel;
	f.advanced = true;
	panel.updatePresentation();
	// The details add nothing to a scenario without messages, and the tooltip gives the id and the description.
	QCOMPARE(panel.listWidget()->item(0)->text(), "Baseline (default)");
	QCOMPARE(panel.listWidget()->item(1)->text(), "Alternative\n1 incident, 1 delay");
	QCOMPARE(panel.listWidget()->item(0)->toolTip(), "ID: base\nno incidents");
	QCOMPARE(panel.listWidget()->item(1)->toolTip(), "ID: alt");
	f.diagnostics = {diagnostic(SceneSeverity::Warning, "scenarios[0].incidents[0]"), diagnostic(SceneSeverity::Error, "scenarios[1]"),
		diagnostic(SceneSeverity::Error, "scenarios[10]"), diagnostic(SceneSeverity::Error, "passengers[0]")};
	panel.updatePresentation();
	// The state of a scenario follows the diagnostics under its path.
	QCOMPARE(panel.listWidget()->item(0)->text(), "Baseline (default)\nhas warnings");
	QCOMPARE(panel.listWidget()->item(1)->text(), "Alternative\n1 incident, 1 delay, has errors");
	f.advanced = false;
	panel.updatePresentation();
	// The short list names only a scenario that is invalid.
	QCOMPARE(panel.listWidget()->item(0)->text(), "Baseline (default)");
	QCOMPARE(panel.listWidget()->item(1)->text(), "Alternative\n1 incident, 1 delay, has errors");
	f.diagnostics.clear();
	f.selection.markScenarioModified("alt");
	panel.updatePresentation();
	QVERIFY2(panel.listWidget()->item(1)->text() == "Alternative (modified)\n1 incident, 1 delay", "an edited scenario is marked");
}

// The selection, the edits of the fields and the renames, then pending text and a run, which see the name and the id
// that the edits left.
void TestScenarioPanel::selectsEditsAndRenamesScenarios() {
	ScenarioFixture f;
	SceneDocument& document = f.document;
	ScenarioPanel& panel = f.panel;
	FakePrompts& prompts = f.prompts;
	RunSelection& selection = f.selection;
	Events& events = f.events;

	// Selection.
	panel.listWidget()->setCurrentRow(1);
	// Another selected scenario is kept in the run selection and shown, and the scene is not changed.
	QCOMPARE(selection.scenarioId(), "alt");
	QCOMPARE(events.log, Log({"selected:alt"}));
	QVERIFY(!document.isDirty());
	QCOMPARE(document.revision(), quint64(1));
	QCOMPARE(panel.idEdit()->text(), "alt");
	QCOMPARE(panel.nameEdit()->text(), "Alternative");
	QVERIFY(panel.descriptionEdit()->text().isEmpty());
	QVERIFY(panel.deleteButton()->isEnabled());
	events.log.clear();
	panel.selectScenario(1);
	QVERIFY2(events.log.empty(), "selecting the scenario that is selected tells nobody");
	panel.selectScenario(7);
	panel.selectScenario(-1);
	// A position outside the scene selects nothing.
	QVERIFY(events.log.empty());
	QCOMPARE(selection.scenarioId(), "alt");

	// Edits of the fields.
	events.log.clear();
	panel.nameEdit()->setText("Alternative route");
	panel.commitNameEdit();
	// A name is written to the document, the scenario counts as edited and the list shows the name.
	QCOMPARE(document.model().scenarios[1].name, "Alternative route");
	QVERIFY(document.isDirty());
	QVERIFY(selection.isScenarioModified("alt"));
	QCOMPARE(events.log, Log({"documentChanged", "panelChanged"}));
	QCOMPARE(events.kinds, SceneDocument::ChangeKinds(SceneDocument::Scenarios));
	QCOMPARE(document.lastChangeLabel(), "Change scenario name");
	QCOMPARE(panel.listWidget()->item(1)->text(), "Alternative route (modified)\n1 incident, 1 delay");
	events.log.clear();
	panel.commitNameEdit();
	QVERIFY2(events.log.empty(), "an unchanged name does not touch the document");
	panel.descriptionEdit()->setText("a longer route");
	panel.commitDescriptionEdit();
	// A description is written to the document.
	QCOMPARE(document.model().scenarios[1].description, "a longer route");
	QCOMPARE(document.lastChangeLabel(), "Change scenario description");
	QCOMPARE(events.log, Log({"documentChanged", "panelChanged"}));

	// Rename: the default scenario and the selection follow.
	events.log.clear();
	panel.idEdit()->setText("  alt2  ");
	panel.commitIdEdit();
	// A new id is written to the document, trimmed, and the selection follows it.
	QCOMPARE(document.model().scenarios[1].id, "alt2");
	QCOMPARE(selection.scenarioId(), "alt2");
	QVERIFY(selection.isScenarioModified("alt2"));
	QVERIFY(!selection.isScenarioModified("alt"));
	QCOMPARE(document.model().defaultScenarioId, "base");
	QCOMPARE(document.lastChangeLabel(), "Rename scenario");
	QCOMPARE(events.log, Log({"documentChanged", "panelChanged"}));
	QVERIFY(panel.listWidget()->item(1)->toolTip().startsWith("ID: alt2"));
	panel.listWidget()->setCurrentRow(0);
	selection.markScenarioModified("base");
	events.log.clear();
	panel.idEdit()->setText("baseline");
	panel.commitIdEdit();
	// Renaming the default scenario moves the default along with it.
	QCOMPARE(document.model().scenarios[0].id, "baseline");
	QCOMPARE(document.model().defaultScenarioId, "baseline");
	QCOMPARE(selection.scenarioId(), "baseline");
	QVERIFY(selection.isScenarioModified("baseline"));
	QVERIFY(!selection.isScenarioModified("base"));
	QCOMPARE(panel.listWidget()->item(0)->text(), "Baseline (default, modified)");
	prompts.errorCount = 0;
	events.log.clear();
	panel.idEdit()->setText("alt2");
	panel.commitIdEdit();
	// An id that another scenario has is refused, and the field shows the old id again.
	QCOMPARE(prompts.errorCount, 1);
	QCOMPARE(prompts.errorTitle, "Duplicate Scenario ID");
	QCOMPARE(panel.idEdit()->text(), "baseline");
	QCOMPARE(document.model().scenarios[0].id, "baseline");
	QVERIFY(events.log.empty());
	panel.idEdit()->setText("   ");
	panel.commitIdEdit();
	// An empty id is refused, and the field shows the old id again.
	QCOMPARE(prompts.errorCount, 2);
	QCOMPARE(prompts.errorTitle, "Invalid Scenario ID");
	QCOMPARE(panel.idEdit()->text(), "baseline");
	QVERIFY(events.log.empty());
	panel.idEdit()->setText("baseline");
	panel.commitIdEdit();
	// An unchanged id does not touch the document.
	QCOMPARE(prompts.errorCount, 2);
	QVERIFY(events.log.empty());

	// Pending edits.
	document.markSaved();
	events.log.clear();
	panel.nameEdit()->setText("Pending name");
	panel.commitPendingEditors();
	// Unfinished text is taken into the document.
	QCOMPARE(document.model().scenarios[0].name, "Pending name");
	QVERIFY(document.isDirty());
	panel.commitPendingEditors();
	QVERIFY2(events.log == Log({"documentChanged", "panelChanged"}), "committing again changes nothing");

	// The scene is in use by a run.
	document.markSaved();
	events.log.clear();
	document.setEditable(false);
	panel.refresh();
	// Nothing can be done with the scenarios, and the fields are empty, while a run uses the scene.
	QVERIFY(!panel.listWidget()->isEnabled());
	QVERIFY(!panel.idEdit()->isEnabled());
	QVERIFY(!panel.nameEdit()->isEnabled());
	QVERIFY(!panel.descriptionEdit()->isEnabled());
	QVERIFY(!panel.blankButton()->isEnabled());
	QVERIFY(!panel.duplicateButton()->isEnabled());
	QVERIFY(!panel.deleteButton()->isEnabled());
	QVERIFY(!panel.importButton()->isEnabled());
	QVERIFY(!panel.exportButton()->isEnabled());
	QVERIFY(panel.idEdit()->text().isEmpty());
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
	// Pending text, selections and actions change nothing and report nothing while a run uses the scene.
	QCOMPARE(document.model().scenarios[0].name, "Pending name");
	QCOMPARE(document.model().scenarios.size(), std::size_t(2));
	QVERIFY(events.log.empty());
	QCOMPARE(selection.scenarioId(), "baseline");
	QCOMPARE(prompts.errorCount, 0);
	document.setEditable(true);
	panel.refresh();
	// The scenarios can be edited again after the run.
	QCOMPARE(panel.idEdit()->text(), "baseline");
	QVERIFY(panel.listWidget()->isEnabled());
}

// Add, Duplicate and Delete, then Import and Export, which see the scenarios that the earlier actions left.
void TestScenarioPanel::addsDeletesImportsAndExportsScenarios() {
	QTemporaryDir directory;
	QVERIFY2(directory.isValid(), "a temporary folder is available");
	ScenarioFixture f;
	SceneDocument& document = f.document;
	ScenarioPanel& panel = f.panel;
	FakePrompts& prompts = f.prompts;
	RunSelection& selection = f.selection;
	Events& events = f.events;

	// Add a blank scenario.
	panel.addBlankScenario();
	// A blank scenario is added, selected and shown, and the window is told which scenario to show.
	QCOMPARE(document.model().scenarios.size(), std::size_t(3));
	QCOMPARE(document.model().scenarios[2].id, "new_scenario");
	QCOMPARE(document.model().scenarios[2].name, "New scenario");
	QCOMPARE(selection.scenarioId(), "new_scenario");
	QVERIFY(selection.isScenarioModified("new_scenario"));
	QCOMPARE(panel.listWidget()->count(), 3);
	QCOMPARE(panel.listWidget()->currentRow(), 2);
	QCOMPARE(panel.idEdit()->text(), "new_scenario");
	QVERIFY(document.isDirty());
	QCOMPARE(document.lastChangeLabel(), "Add scenario");
	QCOMPARE(events.kinds, SceneDocument::ChangeKinds(SceneDocument::Scenarios));
	QCOMPARE(events.log, Log({"documentChanged", "selected:new_scenario", "panelChanged"}));
	panel.addBlankScenario();
	// The id of another blank scenario is unique.
	QCOMPARE(document.model().scenarios.back().id, "new_scenario_2");
	QCOMPARE(selection.scenarioId(), "new_scenario_2");
	// Two scenarios with one name show their IDs in the list and in the header, and the others do not.
	QCOMPARE(panel.listWidget()->item(2)->text(), "New scenario (new_scenario, modified)");
	QCOMPARE(panel.listWidget()->item(3)->text(), "New scenario (new_scenario_2, modified)");
	QVERIFY(panel.listWidget()->item(1)->text().startsWith("Alternative\n"));
	QCOMPARE(panel.headerText(), "Scenario: New scenario (new_scenario_2)");

	// Duplicate.
	panel.listWidget()->setCurrentRow(1);
	events.log.clear();
	panel.duplicateScenario();
	const SceneScenario& copy = document.model().scenarios.back();
	// A copy has an id of its own, unique incident ids and the entries of the original, and is selected.
	QCOMPARE(document.model().scenarios.size(), std::size_t(5));
	QCOMPARE(copy.id, "alt_copy");
	QCOMPARE(copy.name, "Copy of Alternative");
	QCOMPARE(copy.incidents.size(), std::size_t(1));
	QCOMPARE(copy.incidents[0].id, "i1_copy");
	QCOMPARE(copy.entranceDelays.size(), std::size_t(1));
	QCOMPARE(document.model().scenarios[1].incidents[0].id, "i1");
	QCOMPARE(selection.scenarioId(), "alt_copy");
	QCOMPARE(document.lastChangeLabel(), "Duplicate scenario");
	QCOMPARE(events.log, Log({"documentChanged", "selected:alt_copy", "panelChanged"}));

	// Delete.
	prompts.errorCount = 0;
	prompts.confirmCount = 0;
	panel.listWidget()->setCurrentRow(0);
	events.log.clear();
	panel.deleteScenario();
	// The default scenario is not deleted.
	QCOMPARE(prompts.errorCount, 1);
	QCOMPARE(prompts.errorTitle, "Cannot Delete Scenario");
	QCOMPARE(prompts.errorMessage, "The default scenario cannot be deleted.");
	QCOMPARE(prompts.confirmCount, 0);
	QCOMPARE(document.model().scenarios.size(), std::size_t(5));
	QVERIFY(events.log.empty());
	panel.listWidget()->setCurrentRow(1);
	events.log.clear();
	panel.deleteScenario();
	// Declining the question keeps the scenario.
	QCOMPARE(prompts.confirmCount, 1);
	QCOMPARE(prompts.confirmQuestion, "Delete scenario 'alt'?");
	QCOMPARE(document.model().scenarios.size(), std::size_t(5));
	QVERIFY(events.log.empty());
	prompts.confirmAnswer = true;
	panel.deleteScenario();
	// A deleted scenario is removed and the default scenario is selected in its place.
	QCOMPARE(document.model().scenarios.size(), std::size_t(4));
	QCOMPARE(document.model().scenarios[1].id, "new_scenario");
	QCOMPARE(selection.scenarioId(), "base");
	QCOMPARE(panel.listWidget()->count(), 4);
	QCOMPARE(panel.listWidget()->currentRow(), 0);
	QCOMPARE(document.lastChangeLabel(), "Delete scenario");
	QCOMPARE(events.log, Log({"documentChanged", "selected:base", "panelChanged"}));

	// Import.
	const QString dir = directory.path();
	SceneScenario imported;
	imported.id = "imported";
	imported.name = "Imported";
	imported.incidents.push_back(breakdown("i9", "s1"));
	QVERIFY2(writeScenario(dir + "/imported.json", imported), "the scenario file is written");
	QString chosen;
	panel.setImportFileChooser([&chosen] { return chosen; });
	prompts.errorCount = 0;
	events.log.clear();
	events.status.clear();
	panel.importScenario();
	// Cancelling the file chooser imports nothing.
	QCOMPARE(document.model().scenarios.size(), std::size_t(4));
	QVERIFY(events.log.empty());
	QCOMPARE(prompts.errorCount, 0);
	chosen = dir + "/imported.json";
	panel.importScenario();
	// An imported scenario is added and selected, and nothing needs to be explained.
	QCOMPARE(document.model().scenarios.size(), std::size_t(5));
	QCOMPARE(document.model().scenarios.back().id, "imported");
	QCOMPARE(document.model().scenarios.back().incidents.size(), std::size_t(1));
	QCOMPARE(selection.scenarioId(), "imported");
	QVERIFY(selection.isScenarioModified("imported"));
	QCOMPARE(document.lastChangeLabel(), "Import scenario");
	QCOMPARE(prompts.infoCount, 0);
	QCOMPARE(events.status, QStringList({"Imported scenario imported."}));
	QCOMPARE(events.log, Log({"documentChanged", "selected:imported", "panelChanged"}));

	// The id and the incident id of the file are in use.
	SceneScenario conflicting;
	conflicting.id = "imported";
	conflicting.name = "Again";
	conflicting.incidents.push_back(breakdown("i9", "s1"));
	QVERIFY2(writeScenario(dir + "/conflicting.json", conflicting), "the conflicting scenario file is written");
	chosen = dir + "/conflicting.json";
	events.log.clear();
	events.status.clear();
	panel.importScenario();
	// A scenario id and an incident id that are in use are made unique.
	QCOMPARE(document.model().scenarios.size(), std::size_t(6));
	QCOMPARE(document.model().scenarios.back().id, "imported_2");
	QCOMPARE(document.model().scenarios.back().incidents[0].id, "i9_2");
	QCOMPARE(selection.scenarioId(), "imported_2");
	QCOMPARE(document.model().scenarios[4].incidents[0].id, "i9");
	const QString explanation = "Imported scenario imported_2. imported was taken, so it is now imported_2. 1 incident ID was renamed to stay unique.";
	// The user is told which ids were changed.
	QCOMPARE(events.status, QStringList({explanation}));
	QCOMPARE(prompts.infoCount, 1);
	QCOMPARE(prompts.infoTitle, "Scenario ID Adjusted");
	QCOMPARE(prompts.infoMessage, explanation);

	// Files that cannot be imported.
	SceneScenario unusable;
	unusable.id = "unusable";
	unusable.name = "Unusable";
	unusable.incidents.push_back(breakdown("i10", "gone"));
	QVERIFY2(writeScenario(dir + "/unusable.json", unusable), "the unusable scenario file is written");
	chosen = dir + "/unusable.json";
	prompts.errorCount = 0;
	events.log.clear();
	panel.importScenario();
	// A scenario that does not validate against the scene is refused, and the scene stays as it is.
	QCOMPARE(prompts.errorCount, 1);
	QCOMPARE(prompts.errorTitle, "Scenario Import Failed");
	QVERIFY(prompts.errorMessage.contains("unknown service"));
	QCOMPARE(document.model().scenarios.size(), std::size_t(6));
	QVERIFY(events.log.empty());
	QCOMPARE(selection.scenarioId(), "imported_2");
	unusable.incidents[0] = breakdown("i10", "s1");
	unusable.incidents[0].type = "meteor";
	QVERIFY2(writeScenario(dir + "/meteor.json", unusable), "the scenario file with an unknown incident type is written");
	chosen = dir + "/meteor.json";
	prompts.errorCount = 0;
	panel.importScenario();
	// A scenario with an unknown incident type is refused.
	QCOMPARE(prompts.errorCount, 1);
	QCOMPARE(prompts.errorTitle, "Scenario Import Failed");
	QVERIFY(prompts.errorMessage.contains("Unknown incident type"));
	QCOMPARE(document.model().scenarios.size(), std::size_t(6));
	QVERIFY2(prompts.errorMessage == "Unknown incident type\n\nUse signal_failure or train_breakdown",
		"the refusal gives the message and the fix as two paragraphs");
	QFile broken(dir + "/broken.json");
	// The broken file is written.
	QVERIFY(broken.open(QIODevice::WriteOnly));
	QCOMPARE_GT(broken.write("{ not json"), 0);
	broken.close();
	chosen = dir + "/broken.json";
	panel.importScenario();
	// A file that is not a scenario is refused.
	QCOMPARE(prompts.errorCount, 2);
	QCOMPARE(prompts.errorTitle, "Scenario Import Failed");
	QCOMPARE(document.model().scenarios.size(), std::size_t(6));
	chosen = dir + "/missing.json";
	panel.importScenario();
	// A file that is not there is refused.
	QCOMPARE(prompts.errorCount, 3);
	QCOMPARE(prompts.errorTitle, "Scenario Import Failed");
	QCOMPARE(document.model().scenarios.size(), std::size_t(6));

	// Export.
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
	// Cancelling the file chooser exports nothing, and the file chooser suggests the id of the scenario.
	QCOMPARE(observed, 1);
	QCOMPARE(suggested, "new_scenario.json");
	QVERIFY(events.status.isEmpty());
	QVERIFY(events.log.empty());
	QVERIFY(reported.empty());
	target = dir + "/exported";
	panel.exportScenario();
	const ScenarioLoadResult exported = loadScenarioJson((dir + "/exported.json").toStdString());
	// The selected scenario is written to the file, which gets the extension .json.
	QVERIFY(exported.success());
	QCOMPARE(exported.scenario.id, "new_scenario");
	QCOMPARE(exported.scenario.name, "New scenario");
	QCOMPARE(events.status, QStringList({"Exported scenario new_scenario"}));
	QVERIFY(reported.empty());
	QVERIFY(events.log.empty());
	panel.listWidget()->setCurrentRow(4);
	target = dir + "/exported_imported.json";
	panel.exportScenario();
	const ScenarioLoadResult exportedImport = loadScenarioJson(target.toStdString());
	// An exported scenario can be read again with its incidents.
	QVERIFY(exportedImport.success());
	QCOMPARE(exportedImport.scenario.id, "imported");
	QCOMPARE(exportedImport.scenario.incidents.size(), std::size_t(1));
	QCOMPARE(exportedImport.scenario.incidents[0].id, "i9");
	events.status.clear();
	prompts.errorCount = 0;
	target = dir + "/no such folder/exported.json";
	panel.exportScenario();
	// A file that cannot be written is reported to the user and to the observer.
	QCOMPARE(prompts.errorCount, 1);
	QCOMPARE(prompts.errorTitle, "Scenario Export Failed");
	QVERIFY(!reported.empty());
	QVERIFY(events.status.isEmpty());
	panel.setExportObserver({});
	panel.exportScenario();
	QVERIFY2(prompts.errorCount == 2, "an export without an observer still reports the failure to the user");
}

// A scene that has no default scenario on record cannot lose a scenario.
void TestScenarioPanel::keepsScenariosOfSceneWithoutDefault() {
	SceneDocument document;
	ScenarioPanel panel;
	FakePrompts prompts;
	panel.setPrompts(&prompts);
	panel.setDocument(&document);
	SceneModel scene = scenarioScene();
	scene.defaultScenarioId.clear();
	document.replace(scene, false);
	panel.listWidget()->setCurrentRow(1);
	QVERIFY2(!panel.deleteButton()->isEnabled(), "Delete is off while the scene names no default scenario");
	panel.deleteScenario();
	// A scenario is not deleted from a scene without a default scenario.
	QCOMPARE(prompts.errorCount, 1);
	QCOMPARE(prompts.errorTitle, "Cannot Delete Scenario");
	QCOMPARE(prompts.errorMessage, "This case has no default scenario. Fix the validation errors first.");
	QCOMPARE(document.model().scenarios.size(), std::size_t(2));
}

// Another scene replaces the scenarios and the unfinished text.
void TestScenarioPanel::showsAnotherScene() {
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
	// Opening another scene replaces the scenarios and unfinished text without an edit.
	QCOMPARE(items(panel.listWidget()), QStringList({"Only (default)"}));
	QCOMPARE(panel.nameEdit()->text(), "Only");
	QCOMPARE(panel.listWidget()->currentRow(), 0);
	QVERIFY(!document.isDirty());
}

void TestScenarioPanel::detachedPanelIgnoresDocument() {
	SceneDocument document;
	ScenarioPanel panel;
	RunSelection selection;
	panel.setRunSelection(&selection);
	panel.setDocument(&document);
	document.replace(scenarioScene(), false);
	panel.setDocument(nullptr);
	// A detached panel shows nothing.
	QCOMPARE(panel.listWidget()->count(), 0);
	QVERIFY(!panel.listWidget()->isEnabled());
	QVERIFY(panel.idEdit()->text().isEmpty());
	const quint64 revisionDetached = document.revision();
	panel.addBlankScenario();
	document.replace(scenarioScene(), false);
	// A detached panel neither edits nor follows the document.
	QCOMPARE(panel.listWidget()->count(), 0);
	QCOMPARE(document.revision(), revisionDetached + 1);
}

// A scene without an id selected falls back to its default scenario.
void TestScenarioPanel::selectsDefaultScenario() {
	SceneDocument document;
	ScenarioPanel panel;
	RunSelection selection;
	panel.setRunSelection(&selection);
	panel.setDocument(&document);
	SceneModel scene = scenarioScene();
	scene.defaultScenarioId = "alt";
	document.replace(scene, false);
	// The default scenario is selected when no scenario is.
	QCOMPARE(selection.scenarioId(), "alt");
	QCOMPARE(panel.listWidget()->currentRow(), 1);
	QCOMPARE(panel.idEdit()->text(), "alt");
}

// A row is two lines high, and its counts line fits the list at the width that the panel asks for.
void TestScenarioPanel::rowsFitCounts() {
	SceneDocument document;
	ScenarioPanel panel;
	panel.setDocument(&document);
	document.replace(scenarioScene(), false);
	panel.resize(panel.sizeHint());
	panel.show();
	// The panel is hidden before it is destroyed, also when a check fails.
	const auto hide = qScopeGuard([&panel] { panel.hide(); });
	const QListWidget* list = panel.listWidget();
	const QFontMetrics metrics = list->fontMetrics();
	// Each row shows its counts on a second line that the list has room for.
	QCOMPARE_GE(list->visualItemRect(list->item(1)).height(), 2 * metrics.lineSpacing());
	QCOMPARE_LE(metrics.horizontalAdvance(QStringLiteral("1 incident, 1 delay")), list->viewport()->width());
}

void TestScenarioPanel::forgetsDeletedDocument() {
	auto* document = new SceneDocument;
	ScenarioPanel panel;
	panel.setDocument(document);
	document->replace(scenarioScene(), false);
	delete document;
	panel.nameEdit()->setText("after the document");
	panel.commitNameEdit();
	panel.addBlankScenario();
	panel.refresh();
	// A panel whose document is gone shows nothing and edits nothing.
	QCOMPARE(panel.listWidget()->count(), 0);
	QVERIFY(!panel.blankButton()->isEnabled());
}

// The header text names the selected scenario and follows its id and name.
void TestScenarioPanel::headerFollowsSelectedScenario() {
	SceneDocument document;
	ScenarioPanel panel;
	RunSelection selection;
	panel.setRunSelection(&selection);
	QStringList headers;
	QObject::connect(&panel, &ScenarioPanel::headerTextChanged, &panel, [&headers](const QString& text) { headers << text; });
	panel.setDocument(&document);
	QVERIFY2(panel.headerText() == "No scenario selected", "a panel without a scene names no scenario");
	document.replace(scenarioScene(), false);
	// The header names the default scenario when the scene is loaded.
	QCOMPARE(panel.headerText(), "Scenario: Baseline");
	QCOMPARE(headers.last(), panel.headerText());
	panel.listWidget()->setCurrentRow(1);
	QVERIFY2(headers.last() == "Scenario: Alternative", "selecting another scenario changes the header");
	panel.nameEdit()->setText("Renamed");
	panel.commitNameEdit();
	QVERIFY2(headers.last() == "Scenario: Renamed", "renaming the selected scenario changes the header");
	panel.idEdit()->setText("other");
	panel.commitIdEdit();
	QVERIFY2(headers.last() == "Scenario: Renamed", "changing the id of a scenario with a name keeps the header");
	panel.nameEdit()->setText("");
	panel.commitNameEdit();
	QVERIFY2(panel.headerText() == "Scenario: other", "a scenario without a name is named by its id");
}

// Save and Run commit the pending fields first, which on a scene without scenarios has nothing to change.
void TestScenarioPanel::pendingEditsAddNoScenario() {
	ScenarioFixture f;
	SceneModel scene = scenarioScene();
	scene.scenarios.clear();
	scene.defaultScenarioId.clear();
	f.document.replace(scene, false);
	f.panel.commitPendingEditors();
	QVERIFY(f.document.model().scenarios.empty());
	QVERIFY(f.document.model().defaultScenarioId.empty());
	QVERIFY(!f.document.isDirty());
}

QTEST_MAIN(TestScenarioPanel)
#include "test_editor_scenario.moc"
