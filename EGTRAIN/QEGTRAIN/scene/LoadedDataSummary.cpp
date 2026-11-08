#include "scene/LoadedDataSummary.h"

#include <utility>

namespace {

SceneLoadedData makeLoadedData(const std::string& category, const std::string& sourceFile,
	int parsedCount, SceneLoadedStatus status) {
	SceneLoadedData data;
	data.category = category;
	data.sourceFile = sourceFile;
	data.parsedCount = parsedCount;
	data.status = status;
	return data;
}

SceneLoadedStatus diagnosticStatus(const SceneDiagnosticCounts& counts) {
	if (counts.errors > 0)
		return SceneLoadedStatus::Invalid;
	if (counts.warnings > 0)
		return SceneLoadedStatus::Warning;
	return SceneLoadedStatus::Ready;
}

std::size_t rowIndexForDiagnostic(const std::vector<SceneLoadedData>& rows, const SceneDiagnostic& diagnostic) {
	if (!diagnostic.file.empty()) {
		for (std::size_t i = 0; i < rows.size(); ++i) {
			if (rows[i].sourceFile == diagnostic.file)
				return i;
		}
	}
	for (std::size_t i = 0; i < rows.size(); ++i) {
		if (rows[i].category == "scene")
			return i;
	}
	return 0;
}

// Adds a validation row with the diagnostic counts below each top row and marks the rows with errors or warnings.
void addValidationRows(std::vector<SceneLoadedData>& rows, const std::vector<SceneDiagnostic>& diagnostics) {
	std::vector<SceneDiagnosticCounts> counts(rows.size());
	for (const auto& diagnostic : diagnostics) {
		SceneDiagnosticCounts& count = counts[rowIndexForDiagnostic(rows, diagnostic)];
		switch (diagnostic.severity) {
			case SceneSeverity::Error:
				++count.errors;
				break;
			case SceneSeverity::Warning:
				++count.warnings;
				break;
			case SceneSeverity::Info:
				++count.infos;
				break;
		}
	}
	for (std::size_t i = 0; i < rows.size(); ++i) {
		const SceneDiagnosticCounts& count = counts[i];
		const int total = count.errors + count.warnings + count.infos;
		if (count.errors > 0)
			rows[i].status = SceneLoadedStatus::Invalid;
		else if (count.warnings > 0)
			rows[i].status = SceneLoadedStatus::Warning;
		SceneLoadedData validation = makeLoadedData("validation", rows[i].sourceFile, total, diagnosticStatus(count));
		validation.targetType = "validation";
		rows[i].children.push_back(std::move(validation));
	}
}

} // namespace

std::vector<SceneLoadedData> buildLoadedDataSummary(const SceneModel& scene, const std::set<std::string>& files,
	const std::vector<SceneDiagnostic>& diagnostics) {
	std::vector<SceneLoadedData> rows;

	auto statusForFile = [&](const std::string& sourceFile, bool optional) {
		return files.count(sourceFile) > 0 ? SceneLoadedStatus::Parsed
										   : (optional ? SceneLoadedStatus::MissingOptional : SceneLoadedStatus::Invalid);
	};
	auto add = [&](const std::string& category, const std::string& sourceFile, int parsedCount,
				   bool optional = false) {
		const SceneLoadedStatus status = statusForFile(sourceFile, optional);
		SceneLoadedData data = makeLoadedData(category, sourceFile, parsedCount, status);
		if (!sourceFile.empty()) {
			const bool present = files.count(sourceFile) > 0;
			data.children.push_back(makeLoadedData("source_file", sourceFile, present ? 1 : 0,
				present ? SceneLoadedStatus::Loaded : status));
			data.children.push_back(makeLoadedData("parsed_objects", sourceFile, parsedCount,
				present ? SceneLoadedStatus::Parsed : status));
		}
		rows.push_back(data);
	};
	auto addChild = [&](const std::string& category, const std::string& sourceFile, int parsedCount,
						SceneLoadedStatus status) -> SceneLoadedData& {
		rows.back().children.push_back(makeLoadedData(category, sourceFile, parsedCount, status));
		return rows.back().children.back();
	};
	auto addTarget = [](SceneLoadedData& parent, const std::string& category,
						 const std::string& sourceFile, const std::string& targetType) -> SceneLoadedData& {
		SceneLoadedData target = makeLoadedData(category, sourceFile, 1, SceneLoadedStatus::Parsed);
		target.targetType = targetType;
		parent.children.push_back(std::move(target));
		return parent.children.back();
	};

	add("scene", "scene.json", scene.schemaVersion > 0 ? 1 : 0);
	SceneLoadedData& importReport = addChild("import_report", "scene.json",
		static_cast<int>(scene.importReport.size()),
		scene.importReport.empty() ? SceneLoadedStatus::MissingOptional : SceneLoadedStatus::Parsed);
	bool importHasIssues = false;
	for (const auto& reportRow : scene.importReport) {
		const bool hasIssue = reportRow.skippedCount > 0 || reportRow.unresolvedReferences > 0;
		importHasIssues = importHasIssues || hasIssue;
		SceneLoadedData row = makeLoadedData(reportRow.category, reportRow.sourceFile,
			reportRow.convertedCount, hasIssue ? SceneLoadedStatus::Warning : SceneLoadedStatus::Parsed);
		row.children.push_back(makeLoadedData("source_records", reportRow.sourceFile,
			reportRow.sourceCount, SceneLoadedStatus::Parsed));
		row.children.push_back(makeLoadedData("skipped", reportRow.sourceFile,
			reportRow.skippedCount, reportRow.skippedCount > 0 ? SceneLoadedStatus::Warning : SceneLoadedStatus::Ready));
		row.children.push_back(makeLoadedData("unresolved_references", reportRow.sourceFile,
			reportRow.unresolvedReferences,
			reportRow.unresolvedReferences > 0 ? SceneLoadedStatus::Warning : SceneLoadedStatus::Ready));
		importReport.children.push_back(std::move(row));
	}
	if (importHasIssues) {
		importReport.status = SceneLoadedStatus::Warning;
		rows.back().status = SceneLoadedStatus::Warning;
	}
	add("infrastructure", "infrastructure.json",
		static_cast<int>(scene.tracks.size() + scene.nodes.size() + scene.arcs.size() + scene.blocks.size()
			+ scene.connections.size()));
	rows.back().targetType = "network";
	const SceneLoadedStatus infrastructureStatus = rows.back().status;
	addChild("tracks", "infrastructure.json", static_cast<int>(scene.tracks.size()), infrastructureStatus).targetType = "network";
	addChild("nodes", "infrastructure.json", static_cast<int>(scene.nodes.size()), infrastructureStatus).targetType = "network";
	addChild("arcs", "infrastructure.json", static_cast<int>(scene.arcs.size()), infrastructureStatus).targetType = "network";
	addChild("blocks", "infrastructure.json", static_cast<int>(scene.blocks.size()), infrastructureStatus).targetType = "network";
	addChild("connections", "infrastructure.json", static_cast<int>(scene.connections.size()), infrastructureStatus).targetType = "network";

	add("stations", "stations.json", static_cast<int>(scene.stations.size()));
	rows.back().targetType = "network";
	int platformCount = 0;
	for (const auto& station : scene.stations)
		platformCount += static_cast<int>(station.platforms.size());
	addChild("platforms", "stations.json", platformCount, rows.back().status).targetType = "network";

	add("timetable", "services.json", static_cast<int>(scene.services.size()));
	SceneLoadedData& services = addChild("services", "services.json",
		static_cast<int>(scene.services.size()), rows.back().status);
	for (const auto& service : scene.services)
		addTarget(services, service.id, "services.json", "service");

	add("rolling_stock", "rolling_stock.json", static_cast<int>(scene.trainUnits.size() + scene.compositions.size()));
	const SceneLoadedStatus rollingStatus = rows.back().status;
	SceneLoadedData& trainUnits = addChild("train_units", "rolling_stock.json",
		static_cast<int>(scene.trainUnits.size()), rollingStatus);
	for (const auto& unit : scene.trainUnits) {
		SceneLoadedData& unitRow = addTarget(trainUnits, unit.id, "rolling_stock.json", "train_unit");
		unitRow.children.push_back(makeLoadedData("train_unit_parameters", "rolling_stock.json",
			unit.hasPhysical ? 1 : 0, unit.hasPhysical ? SceneLoadedStatus::Parsed : SceneLoadedStatus::Invalid));
		SceneLoadedData curve = makeLoadedData("tractive_effort_curve", "rolling_stock.json",
			static_cast<int>(unit.tractionCurve.size()),
			unit.tractionCurve.empty() ? SceneLoadedStatus::Invalid : SceneLoadedStatus::Parsed);
		if (!unit.tractionCurve.empty()) {
			SceneLoadedData plot = makeLoadedData("Plot traction curve", "rolling_stock.json", 1, SceneLoadedStatus::Ready);
			plot.targetType = "train_unit_plot";
			curve.children.push_back(std::move(plot));
		}
		unitRow.children.push_back(std::move(curve));
		SceneLoadedData provenance = makeLoadedData("import_provenance", "", 0, SceneLoadedStatus::MissingOptional);
		if (!unit.sourceDataFile.empty()) {
			provenance.children.push_back(makeLoadedData("original_parameter_source",
				unit.sourceDataFile, 1, SceneLoadedStatus::Parsed));
			++provenance.parsedCount;
		}
		if (!unit.sourceTractionFile.empty()) {
			provenance.children.push_back(makeLoadedData("original_tractive_effort_source",
				unit.sourceTractionFile, 1, SceneLoadedStatus::Parsed));
			++provenance.parsedCount;
		}
		if (provenance.parsedCount > 0)
			provenance.status = SceneLoadedStatus::Parsed;
		unitRow.children.push_back(std::move(provenance));
	}
	SceneLoadedData& compositions = addChild("compositions", "rolling_stock.json",
		static_cast<int>(scene.compositions.size()), rollingStatus);
	for (const auto& composition : scene.compositions)
		addTarget(compositions, composition.id, "rolling_stock.json", "composition");

	int signallingCount = static_cast<int>(scene.signals.size() + scene.signallingAreas.size() + scene.routes.size()
		+ scene.blockDependencies.size() + scene.singleTrackRestrictions.size()
		+ scene.stationBoundaries.size());
	add("signalling", "signalling.json", signallingCount);
	rows.back().targetType = "network";
	const SceneLoadedStatus signallingStatus = rows.back().status;
	addChild("signals", "signalling.json", static_cast<int>(scene.signals.size()), signallingStatus).targetType = "network";
	addChild("signalling_areas", "signalling.json", static_cast<int>(scene.signallingAreas.size()), signallingStatus).targetType = "network";
	addChild("routes", "signalling.json", static_cast<int>(scene.routes.size()), signallingStatus).targetType = "network";
	addChild("dependencies", "signalling.json", static_cast<int>(scene.blockDependencies.size()), signallingStatus).targetType = "network";
	addChild("restrictions", "signalling.json",
		static_cast<int>(scene.singleTrackRestrictions.size() + scene.stationBoundaries.size()), signallingStatus)
		.targetType = "network";

	int incidentCount = 0;
	int entranceDelayCount = 0;
	for (const auto& scenario : scene.scenarios) {
		incidentCount += static_cast<int>(scenario.incidents.size());
		entranceDelayCount += static_cast<int>(scenario.entranceDelays.size());
	}
	const std::string scenarioSource = files.count("scenarios.json") > 0
		? "scenarios.json"
		: (files.count("incidents.json") > 0 ? "incidents.json" : "scenarios.json");
	add("scenarios", scenarioSource, static_cast<int>(scene.scenarios.size()), true);
	const SceneLoadedStatus scenariosStatus = rows.back().status;
	SceneLoadedData& scenarioItems = addChild("available_scenarios", scenarioSource,
		static_cast<int>(scene.scenarios.size()), scenariosStatus);
	const SceneScenario* selectedScenario = defaultScenario(scene);
	const std::string effectiveDefaultScenario = selectedScenario ? selectedScenario->id : std::string();
	for (const auto& scenario : scene.scenarios) {
		const std::string label = scenario.id == effectiveDefaultScenario
			? sceneScenarioLabel(scene, scenario, {"default"})
			: sceneScenarioLabel(scene, scenario);
		SceneLoadedData item = makeLoadedData(label, scenarioSource,
			static_cast<int>(scenario.incidents.size() + scenario.entranceDelays.size()), scenariosStatus);
		for (const auto& incident : scenario.incidents) {
			SceneLoadedData incidentItem = makeLoadedData(incident.id, scenarioSource, 1, scenariosStatus);
			if (scenario.id == effectiveDefaultScenario) {
				incidentItem.targetType = "incident";
			} else
				incidentItem.category = "Incident: " + incident.id;
			item.children.push_back(std::move(incidentItem));
		}
		scenarioItems.children.push_back(std::move(item));
	}
	addChild("incidents", scenarioSource, incidentCount, scenariosStatus);
	addChild("entrance_delays", scenarioSource, entranceDelayCount, scenariosStatus);

	add("passengers", "passengers.json", static_cast<int>(scene.passengers.size()), true);

	addValidationRows(rows, diagnostics);
	return rows;
}
