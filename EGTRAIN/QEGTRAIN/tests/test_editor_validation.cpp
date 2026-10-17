#include "editors/ValidationPanel.h"

#include <QApplication>
#include <QLabel>
#include <QString>
#include <QTableWidget>
#include <QTreeWidget>
#include <QWidget>

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;

	{
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
		ok &= expect(diagnostics.empty() && panel.table()->rowCount() == 0 && panel.statusLabel()->text().isEmpty()
				&& panel.tree()->topLevelItemCount() == 0,
			"a refresh without a scene clears the diagnostics, the table, the status and the tree");
		ok &= expect(events.log == Log({"refreshed", "statusChanged"}),
			"a refresh without a scene says that the dependents and then the status follow");
	}

	{
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
		ok &= expect(!diagnostics.empty() && overrideAsked == 1,
			"a refresh validates the scene, with the duration override that the window gives");
		ok &= expect(panel.table()->rowCount() == static_cast<int>(diagnostics.size()),
			"the table has a row for each diagnostic");
		bool sorted = true;
		bool hasError = false;
		int previous = 3;
		for (int row = 0; row < panel.table()->rowCount(); ++row) {
			const QString severity = panel.table()->item(row, 0)->text();
			const int rank = severity == "error" ? 2 : (severity == "warning" ? 1 : 0);
			hasError = hasError || rank == 2;
			sorted = sorted && rank <= previous;
			previous = rank;
		}
		ok &= expect(hasError && sorted, "the table lists errors first");
		ok &= expect(panel.statusLabel()->text().startsWith("Not ready: "), "the status says that the scene is not ready");
		ok &= expect(events.log == Log({"statusChanged", "refreshed"}),
			"a refresh with a scene says that the status and then the dependents follow");
		QTreeWidgetItem* root = panel.tree()->topLevelItem(0);
		ok &= expect(panel.tree()->topLevelItemCount() == 1 && root && root->text(0) == "Case Study" && root->text(3) == "Invalid",
			"the tree shows the case, which is invalid while the scene has errors");

		const std::size_t shown = diagnostics.size();
		diagnostics.clear();
		panel.setDiagnostics(nullptr);
		panel.refresh();
		ok &= expect(diagnostics.empty() && panel.table()->rowCount() == static_cast<int>(shown),
			"a panel that is given no vector keeps its diagnostics to itself");
	}

	{
		QWidget host;
		ValidationPanel panel(&host);
		std::vector<SceneDiagnostic> diagnostics;
		bool advanced = false;
		panel.setDiagnostics(&diagnostics);
		panel.setAdvancedDetails([&advanced] { return advanced; });
		int changes = 0;
		QObject::connect(&panel, &ValidationPanel::statusChanged, &panel, [&changes] { ++changes; });

		panel.updateStatus();
		ok &= expect(panel.statusLabel()->text().isEmpty() && changes == 1, "a clean scene has no status in normal mode");
		diagnostics = {diagnostic(SceneSeverity::Warning, "e2e.warning", "Warning")};
		panel.updateStatus();
		ok &= expect(panel.statusLabel()->text().isEmpty(), "a warning has no status in normal mode");
		diagnostics.push_back(diagnostic(SceneSeverity::Error, "e2e.error", "Error"));
		panel.updateStatus();
		ok &= expect(panel.statusLabel()->text() == "Not ready: 1 validation error(s) must be fixed",
			"an error makes the status say that the scene is not ready");
		advanced = true;
		panel.updateStatus();
		ok &= expect(panel.statusLabel()->text() == "Validation: 1 error(s), 1 warning(s)",
			"advanced details give the counts of the errors and the warnings");
		diagnostics.push_back(diagnostic(SceneSeverity::Info, "e2e.info", "Info"));
		panel.updateStatus();
		ok &= expect(panel.statusLabel()->text() == "Validation: 1 error(s), 1 warning(s), 1 info",
			"advanced details add the count of the infos");
	}

	{
		QWidget host;
		ValidationPanel panel(&host);
		SceneDocument document;
		ValidationPanel::LoadedDataContext context;
		panel.setDocument(&document);
		document.replace(caseScene(), false);

		panel.refreshLoadedData();
		QTreeWidgetItem* root = panel.tree()->topLevelItem(0);
		QTreeWidgetItem* runtime = root ? childRow(root, "Runtime and results") : nullptr;
		ok &= expect(root && runtime && root->text(1).isEmpty() && runtime->text(3) == "Not built",
			"a tree without a context shows a case that came from nowhere and no run");
		ok &= expect(runtime && childRow(runtime, "Results") && childRow(runtime, "Results")->text(3) == "Not built"
				&& childRow(runtime, "Applied scenario")->text(1) == "(none)",
			"a tree without a context has no results and no applied scenario");
		ok &= expect(root && !childRow(root, "Bundle format version"), "a scene that is no bundle has no bundle format version");

		context.sceneDir = "/cases/a";
		context.sceneIsBundle = true;
		context.runtimeStatus = "Completed";
		context.runtimeDiagnostics = {diagnostic(SceneSeverity::Warning, "e2e.run", "Run warning", "run.json")};
		context.appliedScenarioId = "scenario.a";
		context.resultsAvailable = true;
		panel.setLoadedDataContext([&context] { return context; });
		panel.refreshLoadedData();
		root = panel.tree()->topLevelItem(0);
		runtime = root ? childRow(root, "Runtime and results") : nullptr;
		ok &= expect(panel.tree()->topLevelItemCount() == 1 && root && root->text(1) == "/cases/a",
			"the tree is written again, not extended");
		QTreeWidgetItem* bundle = root ? childRow(root, "Bundle format version") : nullptr;
		ok &= expect(bundle && bundle->text(1) == "(not recorded)" && bundle->text(3) == "Missing",
			"a bundle without a recorded version is shown as missing");
		context.bundleVersion = 1;
		panel.refreshLoadedData();
		bundle = childRow(panel.tree()->topLevelItem(0), "Bundle format version");
		ok &= expect(bundle && bundle->text(1) == "1" && bundle->text(3) == "Loaded", "a bundle shows its format version");
		runtime = childRow(panel.tree()->topLevelItem(0), "Runtime and results");
		QTreeWidgetItem* results = runtime ? childRow(runtime, "Results") : nullptr;
		QTreeWidgetItem* applied = runtime ? childRow(runtime, "Applied scenario") : nullptr;
		QTreeWidgetItem* run = runtime ? childRow(runtime, "Runtime") : nullptr;
		ok &= expect(results && results->text(2) == "1" && results->text(3) == "Ready", "a completed run has results");
		ok &= expect(applied && applied->text(1) == "scenario.a" && applied->text(3) == "Completed",
			"the applied scenario and the state of the run are shown");
		QTreeWidgetItem* runtimeDiagnostic = run ? childRow(run, "e2e.run: Run warning") : nullptr;
		ok &= expect(run && run->text(2) == "1" && runtimeDiagnostic && runtimeDiagnostic->text(1) == "run.json"
				&& runtimeDiagnostic->text(3) == "Warning",
			"the diagnostics of the run are listed under the runtime");
	}

	{
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
		ok &= expect(activated.empty(), "a row without a kind opens nothing");
		panel.activateItem(service);
		ok &= expect(activated == std::vector<QString>({"service:svc.a"}), "a row with a kind says which kind and which row");
		emit panel.tree()->itemActivated(service, 0);
		ok &= expect(activated.size() == 2, "activating a row of the tree activates its target");
	}

	{
		QWidget host;
		ValidationPanel panel(&host);
		panel.table()->setRowCount(2);
		panel.table()->setItem(0, 3, new QTableWidgetItem("services.json"));
		panel.table()->setItem(1, 3, new QTableWidgetItem("infrastructure.json"));

		panel.selectDiagnosticOfFile("infrastructure.json");
		ok &= expect(panel.table()->selectionModel()->isRowSelected(1, QModelIndex()) && !panel.table()->selectionModel()->isRowSelected(0, QModelIndex()),
			"the row of the scene file is selected");
		panel.selectDiagnosticOfFile("missing.json");
		ok &= expect(panel.table()->selectionModel()->isRowSelected(1, QModelIndex()),
			"a file without a row leaves the selection as it was");
	}

	return ok ? 0 : 1;
}
