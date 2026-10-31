#include "editors/CountText.h"
#include "editors/ValidationPanel.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QByteArray>
#include <QHeaderView>
#include <QLabel>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QWidget>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

static SceneModel caseScene() {
	SceneModel scene;
	scene.name = "Case A";
	scene.description = "About Case A";
	scene.baseTime = "08:00:00";
	return scene;
}

static SceneDiagnostic diagnostic(SceneSeverity severity, const char* code, const char* message, const char* file = "") {
	SceneDiagnostic result;
	result.severity = severity;
	result.code = code;
	result.message = message;
	result.file = file;
	return result;
}

// The smallest scene that validates without an error: a track with one block, a station with a platform on it, a train
// unit, a composition, a route and a service that passes through.
static SceneModel runnableScene() {
	SceneModel scene = makeNewSceneModel();
	scene.name = "Runnable";
	scene.tracks.push_back(SceneTrack{"track"});
	SceneNode first;
	first.id = "node-1";
	first.trackId = "track";
	SceneNode second = first;
	second.id = "node-2";
	second.xKm = 1.0;
	scene.nodes = {first, second};
	SceneArc arc;
	arc.id = "arc";
	arc.trackId = "track";
	arc.fromNodeId = first.id;
	arc.toNodeId = second.id;
	arc.speedLimitMs = 1.0;
	scene.arcs.push_back(arc);
	SceneBlock block;
	block.id = "block";
	block.trackId = "track";
	block.lengthKm = 1.0;
	scene.blocks.push_back(block);
	ScenePlatform platform;
	platform.id = "platform";
	platform.nodeIds.push_back(first.id);
	SceneStation station;
	station.id = "station";
	station.name = "Station";
	station.platforms.push_back(platform);
	scene.stations.push_back(station);
	SceneTrainUnit unit;
	unit.id = "unit";
	unit.hasPhysical = true;
	unit.tractionCurve.push_back({0.0, 1.0, 1.0, 0.0, 0.0});
	scene.trainUnits.push_back(unit);
	SceneComposition composition;
	composition.id = "composition";
	composition.units.push_back(unit.id);
	scene.compositions.push_back(composition);
	SceneRoute route;
	route.id = "route";
	route.blocks.push_back(block.id);
	scene.routes.push_back(route);
	SceneService service;
	service.id = "service";
	service.composition = composition.id;
	service.route = route.id;
	service.through = true;
	scene.services.push_back(service);
	return scene;
}

static bool hasCode(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code) {
	return std::any_of(diagnostics.begin(), diagnostics.end(),
		[&code](const SceneDiagnostic& diagnostic) { return diagnostic.code == code; });
}

// The codes of the errors, for the message of a failed check.
static QByteArray errorCodes(const std::vector<SceneDiagnostic>& diagnostics) {
	QStringList codes;
	for (const SceneDiagnostic& diagnostic : diagnostics)
		if (diagnostic.severity == SceneSeverity::Error)
			codes << QString::fromStdString(diagnostic.code);
	return codes.join(QStringLiteral(", ")).toUtf8();
}

// The first child of a tree row with this text in its first column.
static QTreeWidgetItem* childRow(QTreeWidgetItem* parent, const QString& label) {
	for (int i = 0; i < parent->childCount(); ++i) {
		if (parent->child(i)->text(0) == label)
			return parent->child(i);
	}
	return nullptr;
}

// The order in which the panel reports a refresh.
struct Events {
	std::vector<std::string> log;

	explicit Events(ValidationPanel& panel) {
		QObject::connect(&panel, &ValidationPanel::statusChanged, &panel, [this] { log.push_back("statusChanged"); });
		QObject::connect(&panel, &ValidationPanel::refreshed, &panel, [this] { log.push_back("refreshed"); });
	}
};

using Log = std::vector<std::string>;

class TestValidationPanel : public QObject {
	Q_OBJECT

private slots:
	void clearsWithoutScene();
	void validatesScene();
	void writesStatus();
	void showsLoadedData();
	void showsZeroCountsWithAdvancedDetails();
	void saysOkOutsideRun();
	void activatesTargets();
	void selectsDiagnosticOfFile();
	void showsColumnsWithAdvancedDetails();
	void reusesValidationWithoutChange();
	void validatesAgainAfterEdit();
	void validatesAgainAfterUndo();
	void validatesAgainAfterWriteThatIsNoEdit();
	void validatesAgainForDurationOverride();
	void validatesAgainForNewScene();
	void runValidatesAgainAfterEdit();
	void runWithoutEditValidatesOnce();
};

void TestValidationPanel::clearsWithoutScene() {
	QWidget host;
	ValidationPanel panel(&host);
	SceneDocument document;
	std::vector<SceneDiagnostic> diagnostics{diagnostic(SceneSeverity::Error, "e2e.old", "Old problem")};
	panel.setDocument(&document);
	panel.setDiagnostics(&diagnostics);
	panel.table()->setRowCount(3);
	panel.statusLabel()->setText("old status");
	new QTreeWidgetItem(panel.tree());
	Events events(panel);

	panel.refresh();
	// A refresh without a scene clears the diagnostics, the table, the status and the tree.
	QVERIFY(diagnostics.empty());
	QCOMPARE(panel.table()->rowCount(), 0);
	QVERIFY(panel.statusLabel()->text().isEmpty());
	QCOMPARE(panel.tree()->topLevelItemCount(), 0);
	// A refresh without a scene says that the dependents and then the status follow.
	QCOMPARE(events.log, Log({"refreshed", "statusChanged"}));
}

void TestValidationPanel::validatesScene() {
	QWidget host;
	ValidationPanel panel(&host);
	SceneDocument document;
	std::vector<SceneDiagnostic> diagnostics;
	int overrideAsked = 0;
	panel.setDocument(&document);
	panel.setDiagnostics(&diagnostics);
	panel.setDurationOverride([&overrideAsked]() -> std::optional<double> {
		++overrideAsked;
		return std::nullopt;
	});
	Events events(panel);

	document.replace(caseScene(), false);
	panel.refresh();
	// A refresh validates the scene, with the duration override that the window gives.
	QVERIFY(!diagnostics.empty());
	QCOMPARE(overrideAsked, 1);
	// The table has a row for each diagnostic.
	QCOMPARE(panel.table()->rowCount(), static_cast<int>(diagnostics.size()));
	bool sorted = true;
	bool hasError = false;
	int previous = 3;
	for (int row = 0; row < panel.table()->rowCount(); ++row) {
		const QString severity = panel.table()->item(row, 0)->text();
		const int rank = severity == "Error" ? 2 : (severity == "Warning" ? 1 : 0);
		hasError = hasError || rank == 2;
		sorted = sorted && rank <= previous;
		previous = rank;
	}
	// The table lists errors first.
	QVERIFY(hasError);
	QVERIFY(sorted);
	QVERIFY2(panel.statusLabel()->text().startsWith("Cannot run: "), "the status says that the scene cannot run");
	// A refresh with a scene says that the status and then the dependents follow.
	QCOMPARE(events.log, Log({"statusChanged", "refreshed"}));
	QTreeWidgetItem* root = panel.tree()->topLevelItem(0);
	// The tree shows the case, which is invalid while the scene has errors.
	QCOMPARE(panel.tree()->topLevelItemCount(), 1);
	if (root == nullptr) QFAIL("the tree shows the case, which is invalid while the scene has errors");
	QCOMPARE(root->text(0), "Case");
	QCOMPARE(root->text(3), "Invalid");

	const std::size_t shown = diagnostics.size();
	diagnostics.clear();
	panel.setDiagnostics(nullptr);
	panel.refresh();
	// A panel that is given no vector keeps its diagnostics to itself.
	QVERIFY(diagnostics.empty());
	QCOMPARE(panel.table()->rowCount(), static_cast<int>(shown));
}

void TestValidationPanel::writesStatus() {
	QWidget host;
	ValidationPanel panel(&host);
	std::vector<SceneDiagnostic> diagnostics;
	bool advanced = false;
	panel.setDiagnostics(&diagnostics);
	panel.setAdvancedDetails([&advanced] { return advanced; });
	int changes = 0;
	QObject::connect(&panel, &ValidationPanel::statusChanged, &panel, [&changes] { ++changes; });

	panel.updateStatus();
	// A clean scene has no status in normal mode.
	QVERIFY(panel.statusLabel()->text().isEmpty());
	QCOMPARE(changes, 1);
	diagnostics = {diagnostic(SceneSeverity::Warning, "e2e.warning", "Warning")};
	panel.updateStatus();
	QVERIFY2(panel.statusLabel()->text().isEmpty(), "a warning has no status in normal mode");
	diagnostics.push_back(diagnostic(SceneSeverity::Error, "e2e.error", "Error"));
	panel.updateStatus();
	// An error makes the status say that the scene cannot run.
	QCOMPARE(panel.statusLabel()->text(), "Cannot run: 1 error");
	advanced = true;
	panel.updateStatus();
	// Advanced details give the counts of the errors and the warnings.
	QCOMPARE(panel.statusLabel()->text(), "1 error, 1 warning");
	diagnostics.push_back(diagnostic(SceneSeverity::Info, "e2e.info", "Info"));
	panel.updateStatus();
	// Advanced details add the count of the infos.
	QCOMPARE(panel.statusLabel()->text(), "1 error, 1 warning, 1 info");
	diagnostics.push_back(diagnostic(SceneSeverity::Error, "e2e.error", "Error"));
	diagnostics.push_back(diagnostic(SceneSeverity::Warning, "e2e.warning", "Warning"));
	panel.updateStatus();
	// More than one of a kind has a plural.
	QCOMPARE(panel.statusLabel()->text(), "2 errors, 2 warnings, 1 info");
	advanced = false;
	panel.updateStatus();
	// Two errors have a plural in normal mode too.
	QCOMPARE(panel.statusLabel()->text(), "Cannot run: 2 errors");
	SceneDiagnosticCounts counts;
	counts.warnings = 3;
	// A count of zero is left out, and no message gives no status.
	QCOMPARE(ValidationPanel::statusText(counts, true), "3 warnings");
	QVERIFY(ValidationPanel::statusText(counts, false).isEmpty());
	QVERIFY(ValidationPanel::statusText(SceneDiagnosticCounts(), true).isEmpty());
	// A count takes the singular only for one.
	QCOMPARE(countText(0, "error", "errors"), "0 errors");
	QCOMPARE(countText(1, "error", "errors"), "1 error");
	QCOMPARE(countText(2, "delay", "delays"), "2 delays");
}

void TestValidationPanel::showsLoadedData() {
	QWidget host;
	ValidationPanel panel(&host);
	SceneDocument document;
	ValidationPanel::LoadedDataContext context;
	panel.setDocument(&document);
	document.replace(caseScene(), false);

	panel.refreshLoadedData();
	QTreeWidgetItem* root = panel.tree()->topLevelItem(0);
	QTreeWidgetItem* runtime = root ? childRow(root, "Run") : nullptr;
	// A tree without a context shows a case that came from nowhere and no run.
	if (root == nullptr) QFAIL("a tree without a context shows a case that came from nowhere and no run");
	if (runtime == nullptr) QFAIL("a tree without a context shows a case that came from nowhere and no run");
	QVERIFY(root->text(1).isEmpty());
	QCOMPARE(runtime->text(3), "Not run");
	// A tree without a context has no results and no applied scenario.
	if (runtime == nullptr) QFAIL("a tree without a context has no results and no applied scenario");
	QVERIFY(childRow(runtime, "Results"));
	QCOMPARE(childRow(runtime, "Results")->text(3), "Not run");
	QCOMPARE(childRow(runtime, "Scenario")->text(1), "(none)");
	// Without advanced details the tree leaves out the versions and the files, and a row of one fact has no count.
	if (root == nullptr) QFAIL("without advanced details the tree leaves out the versions and the files, and a row of one fact has no count");
	QVERIFY(!childRow(root, "Bundle version"));
	QVERIFY(!childRow(root, "Format version"));
	QVERIFY(!childRow(root, "Source files discovered"));
	QVERIFY(childRow(root, "Name"));
	QVERIFY(childRow(root, "Name")->text(2).isEmpty());
	QCOMPARE(childRow(root, "Name")->text(3), "OK");
	bool advancedTree = true;
	panel.setAdvancedDetails([&advancedTree] { return advancedTree; });
	panel.refreshLoadedData();
	root = panel.tree()->topLevelItem(0);
	// Advanced details show the versions and the files, and a scene that is no bundle has no bundle version.
	if (root == nullptr) QFAIL("advanced details show the versions and the files, and a scene that is no bundle has no bundle version");
	QVERIFY(childRow(root, "Format version"));
	QVERIFY(childRow(root, "Saved with"));
	QVERIFY(childRow(root, "Source files discovered"));
	QVERIFY(!childRow(root, "Bundle version"));

	context.sceneDir = "/cases/a";
	context.sceneIsBundle = true;
	context.runtimeStatus = "Completed";
	context.runtimeDiagnostics = {diagnostic(SceneSeverity::Warning, "e2e.run", "Run warning", "run.json")};
	context.appliedScenarioId = "scenario.a";
	context.resultsAvailable = true;
	panel.setLoadedDataContext([&context] { return context; });
	panel.refreshLoadedData();
	root = panel.tree()->topLevelItem(0);
	runtime = root ? childRow(root, "Run") : nullptr;
	// The tree is written again, not extended.
	QCOMPARE(panel.tree()->topLevelItemCount(), 1);
	if (root == nullptr) QFAIL("the tree is written again, not extended");
	QCOMPARE(root->text(1), "/cases/a");
	QTreeWidgetItem* bundle = root ? childRow(root, "Bundle version") : nullptr;
	// A bundle without a recorded version says so in the words of the other absent facts.
	if (bundle == nullptr) QFAIL("a bundle without a recorded version says so in the words of the other absent facts");
	QCOMPARE(bundle->text(1), "-");
	QCOMPARE(bundle->text(3), "Not present");
	context.bundleVersion = 1;
	panel.refreshLoadedData();
	bundle = childRow(panel.tree()->topLevelItem(0), "Bundle version");
	// A bundle shows its format version.
	if (bundle == nullptr) QFAIL("a bundle shows its format version");
	QCOMPARE(bundle->text(1), "1");
	QCOMPARE(bundle->text(3), "OK");
	advancedTree = false;
	panel.updateStatus();
	QVERIFY2(!childRow(panel.tree()->topLevelItem(0), "Bundle version"), "switching advanced details off writes the tree again without the versions");
	runtime = childRow(panel.tree()->topLevelItem(0), "Run");
	QTreeWidgetItem* results = runtime ? childRow(runtime, "Results") : nullptr;
	QTreeWidgetItem* applied = runtime ? childRow(runtime, "Scenario") : nullptr;
	QTreeWidgetItem* run = runtime ? childRow(runtime, "Messages") : nullptr;
	// A completed run has results.
	if (results == nullptr) QFAIL("a completed run has results");
	QVERIFY(results->text(2).isEmpty());
	QCOMPARE(results->text(3), "Ready");
	// The applied scenario and the state of the run are shown.
	if (applied == nullptr) QFAIL("the applied scenario and the state of the run are shown");
	QCOMPARE(applied->text(1), "scenario.a");
	QCOMPARE(applied->text(3), "Completed");
	QTreeWidgetItem* runtimeDiagnostic = run ? childRow(run, "Run warning") : nullptr;
	// The diagnostics of the run are listed under the runtime.
	if (run == nullptr) QFAIL("the diagnostics of the run are listed under the runtime");
	QCOMPARE(run->text(2), "1");
	if (runtimeDiagnostic == nullptr) QFAIL("the diagnostics of the run are listed under the runtime");
	QCOMPARE(runtimeDiagnostic->text(1), "run.json");
	QCOMPARE(runtimeDiagnostic->text(3), "Warning");
	QVERIFY(runtimeDiagnostic->toolTip(0).endsWith("Code: e2e.run"));
}

// A count of problems that is zero shows only with advanced details.
void TestValidationPanel::showsZeroCountsWithAdvancedDetails() {
	QWidget host;
	ValidationPanel panel(&host);
	SceneDocument document;
	panel.setDocument(&document);
	SceneModel imported = caseScene();
	SceneImportReportRow stations;
	stations.category = "stations";
	stations.sourceFile = "Stations.txt";
	stations.sourceCount = 3;
	stations.convertedCount = 2;
	stations.skippedCount = 1;
	imported.importReport.push_back(stations);
	document.replace(imported, false);
	bool advanced = false;
	panel.setAdvancedDetails([&advanced] { return advanced; });
	const auto metadataRow = [&panel]() -> QTreeWidgetItem* {
		QTreeWidgetItem* root = panel.tree()->topLevelItem(0);
		return root ? childRow(root, "Case metadata") : nullptr;
	};
	const auto importRow = [&metadataRow]() -> QTreeWidgetItem* {
		QTreeWidgetItem* report = metadataRow() ? childRow(metadataRow(), "Import report") : nullptr;
		return report ? childRow(report, "Stations") : nullptr;
	};
	panel.refreshLoadedData();
	QTreeWidgetItem* row = importRow();
	// Without advanced details the tree leaves out the counts of problems that are zero.
	if (row == nullptr) QFAIL("without advanced details the tree leaves out the counts of problems that are zero");
	QVERIFY(childRow(row, "Source records"));
	QVERIFY(childRow(row, "Skipped"));
	QCOMPARE(childRow(row, "Skipped")->text(2), "1");
	QVERIFY(!childRow(row, "Unresolved references"));
	QVERIFY(!childRow(metadataRow(), "Validation"));
	advanced = true;
	panel.refreshLoadedData();
	row = importRow();
	// Advanced details show the counts of zero.
	if (row == nullptr) QFAIL("advanced details show the counts of zero");
	QVERIFY(childRow(row, "Unresolved references"));
	QCOMPARE(childRow(row, "Unresolved references")->text(2), "0");
	QVERIFY(childRow(metadataRow(), "Validation"));
	QCOMPARE(childRow(metadataRow(), "Validation")->text(2), "0");
	// A count of zero problems says OK, as the other rows without a problem do.
	if (row == nullptr) QFAIL("a count of zero problems says OK, as the other rows without a problem do");
	QCOMPARE(childRow(row, "Unresolved references")->text(3), "OK");
	QCOMPARE(childRow(metadataRow(), "Validation")->text(3), "OK");
	// Rows that open deeper in the tree widen the Category column, so that their names show whole.
	QTreeWidget* tree = panel.tree();
	// QTreeView hides the public sizeHintForColumn of its base, which still calls the tree's own version.
	const auto contentWidth = [tree] { return static_cast<QAbstractItemView*>(tree)->sizeHintForColumn(0); };
	const int closedWidth = tree->columnWidth(0);
	metadataRow()->setExpanded(true);
	childRow(metadataRow(), "Import report")->setExpanded(true);
	importRow()->setExpanded(true);
	// The Category column fits the rows that an expanded row shows.
	QCOMPARE_GT(tree->columnWidth(0), closedWidth);
	QCOMPARE_GE(tree->columnWidth(0), contentWidth());
	importRow()->setExpanded(false);
	// The Category column narrows again when a row closes.
	QCOMPARE(tree->columnWidth(0), std::max(contentWidth(), tree->header()->sectionSizeHint(0)));
}

// "Ready" is a state of the run; the case and its data say OK when they have no problem, and a message of the run
// says its severity.
void TestValidationPanel::saysOkOutsideRun() {
	QWidget host;
	ValidationPanel panel(&host);
	SceneDocument document;
	SceneModel clean = caseScene();
	for (const char* file : {"scene.json", "infrastructure.json", "stations.json", "signalling.json", "rolling_stock.json",
			 "services.json", "scenarios.json"})
		clean.sourceFiles.insert(file);
	document.replace(clean, false);
	panel.setDocument(&document);
	panel.setAdvancedDetails([] { return true; });
	ValidationPanel::LoadedDataContext context;
	context.runtimeStatus = "Ready";
	context.runtimeDiagnostics = {diagnostic(SceneSeverity::Info, "e2e.note", "Run note")};
	panel.setLoadedDataContext([&context] { return context; });
	panel.refreshLoadedData();
	QTreeWidgetItem* root = panel.tree()->topLevelItem(0);
	QTreeWidgetItem* run = root ? childRow(root, "Run") : nullptr;
	QStringList readyOutsideRun;
	for (QTreeWidgetItemIterator it(panel.tree()); *it; ++it) {
		bool inRun = false;
		for (QTreeWidgetItem* item = *it; item && !inRun; item = item->parent())
			inRun = item == run;
		if (!inRun && (*it)->text(3) == "Ready")
			readyOutsideRun << (*it)->text(0);
	}
	// The case and its data without a problem say OK, and only the run says Ready.
	if (root == nullptr) QFAIL("the case and its data without a problem say OK, and only the run says Ready");
	QCOMPARE(root->text(3), "OK");
	if (run == nullptr) QFAIL("the case and its data without a problem say OK, and only the run says Ready");
	QCOMPARE(run->text(3), "Ready");
	QVERIFY(readyOutsideRun.isEmpty());
	context.runtimeDiagnostics.insert(context.runtimeDiagnostics.begin(), diagnostic(SceneSeverity::Error, "e2e.fail", "Run error"));
	panel.refreshLoadedData();
	root = panel.tree()->topLevelItem(0);
	run = root ? childRow(root, "Run") : nullptr;
	QTreeWidgetItem* messages = run ? childRow(run, "Messages") : nullptr;
	QTreeWidgetItem* failRow = messages ? childRow(messages, "Run error") : nullptr;
	QTreeWidgetItem* noteRow = messages ? childRow(messages, "Run note") : nullptr;
	// A message of the run says its severity as the Case Validation table does, and its code in the tooltip.
	if (failRow == nullptr) QFAIL("a message of the run says its severity as the Case Validation table does, and its code in the tooltip");
	QCOMPARE(failRow->text(3), "Error");
	QCOMPARE(failRow->toolTip(0), "Run error\nCode: e2e.fail");
	if (noteRow == nullptr) QFAIL("a message of the run says its severity as the Case Validation table does, and its code in the tooltip");
	QCOMPARE(noteRow->text(3), "Info");

	// A long message does not push Count and Status out of view, and its tooltip shows it whole with its fix.
	const std::string longMessage(200, 'm');
	SceneDiagnostic longDiagnostic = diagnostic(SceneSeverity::Warning, "e2e.long", longMessage.c_str());
	longDiagnostic.suggestedFix = "Shorten it";
	context.runtimeDiagnostics.push_back(longDiagnostic);
	panel.refreshLoadedData();
	run = childRow(panel.tree()->topLevelItem(0), "Run");
	messages = run ? childRow(run, "Messages") : nullptr;
	QTreeWidgetItem* longRow = messages ? childRow(messages, QString::fromStdString(longMessage)) : nullptr;
	if (longRow == nullptr) QFAIL("the Category column stops growing at its limit, and a long message shows whole in its tooltip");
	run->setExpanded(true);
	messages->setExpanded(true);
	// The Category column stops growing at its limit, and a long message shows whole in its tooltip.
	QCOMPARE(panel.tree()->columnWidth(0), ValidationPanel::kTreeTextColumnWidth);
	QCOMPARE(longRow->toolTip(0), QString::fromStdString(longMessage + "\n\nShorten it\nCode: e2e.long"));
}

void TestValidationPanel::activatesTargets() {
	QWidget host;
	ValidationPanel panel(&host);
	std::vector<QString> activated;
	QObject::connect(&panel, &ValidationPanel::targetActivated, &panel,
		[&activated](const QString& type, QTreeWidgetItem* item) { activated.push_back(type + ":" + item->text(0)); });

	auto* plain = new QTreeWidgetItem(panel.tree(), QStringList{"plain"});
	auto* service = new QTreeWidgetItem(panel.tree(), QStringList{"svc.a"});
	service->setData(0, ValidationPanel::kTargetTypeRole, QString("service"));
	panel.activateItem(nullptr);
	panel.activateItem(plain);
	QVERIFY2(activated.empty(), "a row without a kind opens nothing");
	panel.activateItem(service);
	// A row with a kind says which kind and which row.
	QCOMPARE(activated, std::vector<QString>({"service:svc.a"}));
	emit panel.tree()->itemActivated(service, 0);
	// Activating a row of the tree activates its target.
	QCOMPARE(activated.size(), std::size_t(2));
}

void TestValidationPanel::selectsDiagnosticOfFile() {
	QWidget host;
	ValidationPanel panel(&host);
	panel.table()->setRowCount(2);
	for (const auto& [row, file] : {std::pair<int, const char*>{0, "services.json"}, {1, "infrastructure.json"}}) {
		auto* severity = new QTableWidgetItem("Error");
		severity->setData(ValidationPanel::kFileRole, QString(file));
		panel.table()->setItem(row, 0, severity);
	}

	panel.selectDiagnosticOfFile("infrastructure.json");
	// The row of the scene file is selected.
	QVERIFY(panel.table()->selectionModel()->isRowSelected(1, QModelIndex()));
	QVERIFY(!panel.table()->selectionModel()->isRowSelected(0, QModelIndex()));
	panel.selectDiagnosticOfFile("missing.json");
	QVERIFY2(panel.table()->selectionModel()->isRowSelected(1, QModelIndex()), "a file without a row leaves the selection as it was");
}

// The table shows the severity, the message and the fix; the code, the file and the path are in the tooltip of the
// row and are columns only with advanced details.
void TestValidationPanel::showsColumnsWithAdvancedDetails() {
	QWidget host;
	ValidationPanel panel(&host);
	SceneDocument document;
	std::vector<SceneDiagnostic> diagnostics;
	bool advanced = false;
	panel.setDocument(&document);
	panel.setDiagnostics(&diagnostics);
	panel.setAdvancedDetails([&advanced] { return advanced; });
	SceneModel scene = caseScene();
	scene.baseTime.clear();
	document.replace(scene, false);
	panel.refresh();
	QTableWidget* table = panel.table();
	QStringList headers;
	for (int column = 0; column < table->columnCount(); ++column)
		headers << table->horizontalHeaderItem(column)->text();
	const auto shown = [table] {
		QStringList columns;
		for (int column = 0; column < table->columnCount(); ++column)
			if (!table->isColumnHidden(column))
				columns << table->horizontalHeaderItem(column)->text();
		return columns;
	};
	int baseTimeRow = -1;
	for (int row = 0; row < table->rowCount(); ++row)
		if (table->item(row, 1)->text() == "scene.basetime.missing")
			baseTimeRow = row;
	// Without advanced details the table shows the severity, the message and the fix.
	QCOMPARE(headers, QStringList({"Severity", "Code", "Message", "File", "Path", "Fix"}));
	QCOMPARE(shown(), QStringList({"Severity", "Message", "Fix"}));
	// A row keeps its file as data and names its code, file and path in its tooltip, one per line.
	QCOMPARE_GE(baseTimeRow, 0);
	QCOMPARE(table->item(baseTimeRow, 0)->text(), "Error");
	QCOMPARE(table->item(baseTimeRow, 0)->data(ValidationPanel::kFileRole).toString(), "scene.json");
	QCOMPARE(table->item(baseTimeRow, 2)->toolTip(), "Code: scene.basetime.missing\nFile: scene.json\nPath: base_time");
	QCOMPARE(table->item(baseTimeRow, 5)->toolTip(), table->item(baseTimeRow, 2)->toolTip());
	advanced = true;
	panel.updateStatus();
	// Advanced details show every column.
	QCOMPARE(shown(), headers);
	advanced = false;
	panel.updateStatus();
	// The columns follow advanced details back.
	QCOMPARE(shown(), QStringList({"Severity", "Message", "Fix"}));
}

// A refresh that finds the scene and the duration override as they were validated shows that validation again: the
// diagnostics, the table, the status and the tree, and it says so as a refresh that validates does.
void TestValidationPanel::reusesValidationWithoutChange() {
	QWidget host;
	ValidationPanel panel(&host);
	SceneDocument document;
	std::vector<SceneDiagnostic> diagnostics;
	panel.setDocument(&document);
	panel.setDiagnostics(&diagnostics);
	document.replace(caseScene(), false);
	panel.refresh();
	QCOMPARE(panel.validationCount(), 1);
	const std::vector<SceneDiagnostic> validated = diagnostics;
	QVERIFY(!validated.empty());

	// The window clears the list when it opens a scene, and a scripted run writes its own diagnostics into it.
	diagnostics.clear();
	panel.table()->setRowCount(0);
	panel.statusLabel()->clear();
	panel.tree()->clear();
	Events events(panel);
	panel.refresh();
	// The scene is not validated again.
	QCOMPARE(panel.validationCount(), 1);
	// The diagnostics of the validation are written and shown again.
	QCOMPARE(diagnostics.size(), validated.size());
	for (std::size_t index = 0; index < validated.size(); ++index)
		QCOMPARE(diagnostics[index].code, validated[index].code);
	QCOMPARE(panel.table()->rowCount(), static_cast<int>(validated.size()));
	QVERIFY(panel.statusLabel()->text().startsWith("Cannot run: "));
	QCOMPARE(panel.tree()->topLevelItemCount(), 1);
	QCOMPARE(events.log, Log({"statusChanged", "refreshed"}));
}

// An edit validates again and shows the new diagnostics.
void TestValidationPanel::validatesAgainAfterEdit() {
	QWidget host;
	ValidationPanel panel(&host);
	SceneDocument document;
	std::vector<SceneDiagnostic> diagnostics;
	panel.setDocument(&document);
	panel.setDiagnostics(&diagnostics);
	document.replace(runnableScene(), false);
	panel.refresh();
	QVERIFY2(!hasErrors(diagnostics), errorCodes(diagnostics).constData());
	QCOMPARE(panel.validationCount(), 1);

	document.edit(SceneDocument::CaseSettings, "Edit case settings", [](SceneModel& model) { model.baseTime.clear(); });
	panel.refresh();
	QCOMPARE(panel.validationCount(), 2);
	QVERIFY(hasCode(diagnostics, "scene.basetime.missing"));
}

// Undo puts an earlier scene back, which validates again and shows its diagnostics.
void TestValidationPanel::validatesAgainAfterUndo() {
	QWidget host;
	ValidationPanel panel(&host);
	SceneDocument document;
	std::vector<SceneDiagnostic> diagnostics;
	panel.setDocument(&document);
	panel.setDiagnostics(&diagnostics);
	const SceneModel runnable = runnableScene();
	document.replace(runnable, false);
	document.edit(SceneDocument::CaseSettings, "Edit case settings", [](SceneModel& model) { model.baseTime.clear(); });
	panel.refresh();
	QCOMPARE(panel.validationCount(), 1);
	QVERIFY(hasCode(diagnostics, "scene.basetime.missing"));

	document.restore(runnable, false);
	panel.refresh();
	QCOMPARE(panel.validationCount(), 2);
	QVERIFY2(!hasErrors(diagnostics), errorCodes(diagnostics).constData());
}

// A write through mutableModel(), which is not an edit, validates again: the window sets a default scenario that way, a
// save records the files, and the scripted runs set up their scenes.
void TestValidationPanel::validatesAgainAfterWriteThatIsNoEdit() {
	QWidget host;
	ValidationPanel panel(&host);
	SceneDocument document;
	std::vector<SceneDiagnostic> diagnostics;
	panel.setDocument(&document);
	panel.setDiagnostics(&diagnostics);
	document.replace(runnableScene(), false);
	panel.refresh();
	QCOMPARE(panel.validationCount(), 1);

	const quint64 revision = document.revision();
	document.mutableModel().defaultScenarioId.clear();
	// The write is no edit, so the revision stays.
	QCOMPARE(document.revision(), revision);
	panel.refresh();
	QCOMPARE(panel.validationCount(), 2);
	QVERIFY(hasCode(diagnostics, "scene.scenario.default.missing"));
}

// Another duration override validates again, and its result counts the occurrences of that duration.
void TestValidationPanel::validatesAgainForDurationOverride() {
	QWidget host;
	ValidationPanel panel(&host);
	SceneDocument document;
	std::vector<SceneDiagnostic> diagnostics;
	std::optional<double> durationOverride;
	panel.setDocument(&document);
	panel.setDiagnostics(&diagnostics);
	panel.setDurationOverride([&durationOverride] { return durationOverride; });
	SceneModel scene = runnableScene();
	// Every minute: 60 runs in the hour of the scene, 1000 in a duration of 60000 s, more than a run holds.
	scene.services.front().hasRepeat = true;
	scene.services.front().headwaySeconds = 60.0;
	document.replace(scene, false);
	panel.refresh();
	QVERIFY2(!hasErrors(diagnostics), errorCodes(diagnostics).constData());
	QCOMPARE(panel.validationCount(), 1);

	durationOverride = 60000.0;
	panel.refresh();
	QCOMPARE(panel.validationCount(), 2);
	QVERIFY(hasCode(diagnostics, "scene.capacity.runtime"));
	panel.refresh();
	// The same override does not validate again.
	QCOMPARE(panel.validationCount(), 2);
	durationOverride.reset();
	panel.refresh();
	QCOMPARE(panel.validationCount(), 3);
	QVERIFY2(!hasErrors(diagnostics), errorCodes(diagnostics).constData());
}

// Another scene validates again and shows its own diagnostics.
void TestValidationPanel::validatesAgainForNewScene() {
	QWidget host;
	ValidationPanel panel(&host);
	SceneDocument document;
	std::vector<SceneDiagnostic> diagnostics;
	panel.setDocument(&document);
	panel.setDiagnostics(&diagnostics);
	document.replace(runnableScene(), false);
	panel.refresh();
	QVERIFY2(!hasErrors(diagnostics), errorCodes(diagnostics).constData());
	QCOMPARE(panel.validationCount(), 1);

	document.replace(caseScene(), false);
	panel.refresh();
	QCOMPARE(panel.validationCount(), 2);
	QVERIFY(hasCode(diagnostics, "scene.services.none"));
	// Another document, whose scene has changed as often as that of the first one.
	SceneDocument other;
	other.replace(caseScene(), false);
	other.replace(runnableScene(), false);
	QCOMPARE(other.modelGeneration(), document.modelGeneration());
	panel.setDocument(&other);
	panel.refresh();
	// A panel that is given another document validates its scene.
	QCOMPARE(panel.validationCount(), 3);
	QVERIFY2(!hasErrors(diagnostics), errorCodes(diagnostics).constData());
}

// RunController::runScene asks the window to validate, which refreshes this panel, and refuses the run when the
// diagnostics have an error. This is that step.
static bool runAllowed(ValidationPanel& panel, const std::vector<SceneDiagnostic>& diagnostics) {
	panel.refresh();
	return !hasErrors(diagnostics);
}

// A Run after an edit validates the edited scene and refuses it when the edit made it invalid.
void TestValidationPanel::runValidatesAgainAfterEdit() {
	QWidget host;
	ValidationPanel panel(&host);
	SceneDocument document;
	std::vector<SceneDiagnostic> diagnostics;
	panel.setDocument(&document);
	panel.setDiagnostics(&diagnostics);
	document.replace(runnableScene(), false);
	panel.refresh();
	QVERIFY2(runAllowed(panel, diagnostics), errorCodes(diagnostics).constData());
	QCOMPARE(panel.validationCount(), 1);

	document.edit(SceneDocument::Services, "Delete service", [](SceneModel& model) { model.services.clear(); });
	QVERIFY(!runAllowed(panel, diagnostics));
	QCOMPARE(panel.validationCount(), 2);
	QVERIFY(hasCode(diagnostics, "scene.services.none"));
}

// A Run with no edit since the last validation does not validate again, and still refuses a scene with errors.
void TestValidationPanel::runWithoutEditValidatesOnce() {
	QWidget host;
	ValidationPanel panel(&host);
	SceneDocument document;
	std::vector<SceneDiagnostic> diagnostics;
	panel.setDocument(&document);
	panel.setDiagnostics(&diagnostics);
	document.replace(caseScene(), false);
	panel.refresh();
	QCOMPARE(panel.validationCount(), 1);

	QVERIFY(!runAllowed(panel, diagnostics));
	QCOMPARE(panel.validationCount(), 1);
	// After the refused run the window shows the validation again, still without validating.
	QVERIFY(!runAllowed(panel, diagnostics));
	QCOMPARE(panel.validationCount(), 1);
	QVERIFY(hasCode(diagnostics, "scene.services.none"));
}

QTEST_MAIN(TestValidationPanel)
#include "test_editor_validation.moc"
