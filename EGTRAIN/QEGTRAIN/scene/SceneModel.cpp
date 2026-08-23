#include "scene/SceneModel.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

bool readFile(const fs::path& path, std::string& content) {
	std::ifstream input(path, std::ios::binary);
	if (!input)
		return false;
	content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
	return true;
}

bool canonicalSnapshotFile(const std::string& file) {
	return file == "scene.json" || file == "infrastructure.json" || file == "stations.json"
			|| file == "signalling.json" || file == "rolling_stock.json" || file == "services.json"
			|| file == "scenarios.json" || file == "incidents.json" || file == "passengers.json"
			|| file == "views.json";
}

std::string joinPath(const std::string& parent, const std::string& key) {
	return parent.empty() ? key : parent + "." + key;
}

SceneLoadedData makeLoadedData(const std::string& category, const std::string& sourceFile,
		int parsedCount, const std::string& status) {
	SceneLoadedData data;
	data.category = category;
	data.sourceFile = sourceFile;
	data.parsedCount = parsedCount;
	data.status = status;
	return data;
}

std::string loadedDataDiagnosticStatus(const SceneDiagnosticCounts& counts) {
	if (counts.errors > 0)
		return "Invalid";
	if (counts.warnings > 0)
		return "Warning";
	return "Ready";
}

std::size_t loadedDataIndexForDiagnostic(const SceneModel& scene, const SceneDiagnostic& diagnostic) {
	if (!diagnostic.file.empty()) {
		for (std::size_t i = 0; i < scene.loadedData.size(); ++i) {
			if (scene.loadedData[i].sourceFile == diagnostic.file)
				return i;
		}
	}
	for (std::size_t i = 0; i < scene.loadedData.size(); ++i) {
		if (scene.loadedData[i].category == "scene")
			return i;
	}
	return 0;
}

} // namespace

std::string buildSceneDirectorySnapshot(
		const std::vector<std::pair<std::string, std::string>>& files) {
	std::vector<std::pair<std::string, std::string>> sorted = files;
	std::sort(sorted.begin(), sorted.end(), [](const auto& first, const auto& second) {
		return first.first < second.first;
	});

	std::string snapshot;
	for (const auto& file : sorted) {
		snapshot += std::to_string(file.first.size());
		snapshot += ':';
		snapshot += file.first;
		snapshot += ':';
		snapshot += std::to_string(file.second.size());
		snapshot += ':';
		snapshot += file.second;
	}
	return snapshot;
}

SceneInputSnapshot readSceneDirectorySnapshot(const std::string& sceneDir) {
	SceneInputSnapshot result;
	std::error_code ec;
	if (!fs::is_directory(sceneDir, ec) || ec) {
		result.reason = "scene directory is missing or unreadable";
		return result;
	}

	std::vector<std::pair<std::string, std::string>> files;
	const auto read = [&](const char* name, bool required) {
		const fs::path path = fs::path(sceneDir) / name;
		ec.clear();
		if (!fs::is_regular_file(path, ec) || ec) {
			if (required)
				result.reason = std::string("required scene input is missing or unreadable: ") + name;
			return !required;
		}
		std::string content;
		if (!readFile(path, content)) {
			result.reason = std::string("scene input is missing or unreadable: ") + name;
			return false;
		}
		files.emplace_back(name, std::move(content));
		return true;
	};
	const auto exists = [&](const char* name, bool& present) {
		ec.clear();
		present = fs::exists(fs::path(sceneDir) / name, ec);
		if (!ec)
			return true;
		result.reason = std::string("scene input is missing or unreadable: ") + name;
		return false;
	};

	for (const char* name : {"scene.json", "infrastructure.json", "stations.json", "signalling.json",
			"rolling_stock.json", "services.json"}) {
		if (!read(name, true))
			return result;
	}
	bool scenariosPresent = false;
	bool incidentsPresent = false;
	if (!exists("scenarios.json", scenariosPresent) || !exists("incidents.json", incidentsPresent))
		return result;
	if (scenariosPresent && !read("scenarios.json", true))
		return result;
	if (incidentsPresent && !read("incidents.json", true))
		return result;
	bool passengersPresent = false;
	if (!exists("passengers.json", passengersPresent)
			|| (passengersPresent && !read("passengers.json", true)))
		return result;
	bool viewsPresent = false;
	if (!exists("views.json", viewsPresent) || (viewsPresent && !read("views.json", true)))
		return result;

	result.bytes = buildSceneDirectorySnapshot(files);
	return result;
}

std::string sceneOutputDirectoryComponent(const std::string& sceneName) {
	if (sceneName.empty() || sceneName.find_first_of("<>:\"/\\|?*") != std::string::npos
			|| std::any_of(sceneName.begin(), sceneName.end(), [](unsigned char value) {
				return value <= 31;
			})
			|| sceneName.back() == ' ' || sceneName.back() == '.')
		return "scene";
	const auto asciiLower = [](unsigned char value) {
		return value >= 'A' && value <= 'Z'
				? static_cast<unsigned char>(value + ('a' - 'A')) : value;
	};
	std::string basename = sceneName.substr(0, sceneName.find('.'));
	std::transform(basename.begin(), basename.end(), basename.begin(), asciiLower);
	const bool comOrLpt = basename.compare(0, 3, "com") == 0
			|| basename.compare(0, 3, "lpt") == 0;
	if (basename == "con" || basename == "prn" || basename == "aux" || basename == "nul"
			|| (comOrLpt && basename.size() == 4
					&& ((basename[3] >= '1' && basename[3] <= '9')
							|| static_cast<unsigned char>(basename[3]) == 0xB2
							|| static_cast<unsigned char>(basename[3]) == 0xB3
							|| static_cast<unsigned char>(basename[3]) == 0xB9))
			|| (comOrLpt && basename.size() == 5
					&& (basename.compare(3, 2, "\xC2\xB2") == 0
							|| basename.compare(3, 2, "\xC2\xB3") == 0
							|| basename.compare(3, 2, "\xC2\xB9") == 0)))
		return "scene";
	return sceneName;
}

int sceneServiceOccurrenceCount(const SceneService& service, double durationSeconds) {
	if (!service.hasRepeat)
		return 1;
	if (service.hasRepeatCount)
		return service.repeatCount > 0 ? service.repeatCount : 1;
	if (!std::isfinite(service.headwaySeconds) || service.headwaySeconds <= 0.0
			|| !std::isfinite(durationSeconds) || durationSeconds <= 0.0)
		return 1;
	const double rawCount = std::ceil(durationSeconds / service.headwaySeconds);
	if (!std::isfinite(rawCount) || rawCount >= static_cast<double>(INT_MAX))
		return INT_MAX;
	return std::max(1, static_cast<int>(rawCount));
}

std::string sceneServiceOccurrenceOperatingCode(const SceneService& service, int occurrence) {
	if (occurrence <= 0)
		return {};
	const std::string base = service.operatingCode.empty() ? service.id : service.operatingCode;
	if (base.empty())
		return {};
	if (service.hasOperatingCodeStep) {
		if (!service.hasRepeat || service.operatingCodeStep == 0
				|| !std::all_of(base.begin(), base.end(), [](unsigned char value) {
					return std::isdigit(value) != 0;
				}))
			return {};
		try {
			const long long baseValue = std::stoll(base);
			const long long delta = static_cast<long long>(service.operatingCodeStep)
					* static_cast<long long>(occurrence - 1);
			if ((delta > 0 && baseValue > std::numeric_limits<long long>::max() - delta)
					|| (delta < 0 && baseValue < std::numeric_limits<long long>::min() - delta))
				return {};
			return std::to_string(baseValue + delta);
		} catch (const std::exception&) {
			return {};
		}
	}
	if (service.hasRepeat)
		return base + "-" + std::to_string(occurrence);
	return base;
}

bool resolveScenePassengerLegStops(const SceneService& service, const ScenePassengerLeg& leg,
		SceneServiceStopPair& result) {
	for (std::size_t originIndex = 0; originIndex < service.stops.size(); ++originIndex) {
		if (service.stops[originIndex].stationId != leg.originStationId)
			continue;
		for (std::size_t destinationIndex = originIndex + 1; destinationIndex < service.stops.size();
				++destinationIndex) {
			if (service.stops[destinationIndex].stationId == leg.destinationStationId) {
				result.originIndex = originIndex;
				result.destinationIndex = destinationIndex;
				return true;
			}
		}
	}
	return false;
}

SceneModel makeNewSceneModel() {
	SceneModel scene;
	scene.schemaVersion = 1;
	scene.name = "Untitled Case Study";
	scene.baseTime = "08:00:00";
	scene.settings.hasDuration = true;
	scene.settings.durationSeconds = 3600.0;
	scene.settings.hasBufferTime = true;
	scene.settings.bufferTimeSeconds = 0.0;
	scene.settings.hasRecoveryTime = true;
	scene.settings.recoveryTimePercent = 0.0;
	scene.defaultScenarioId = "baseline";
	scene.scenarios.push_back({"baseline", "Baseline", {}, {}, {}});
	return scene;
}

bool buildSceneComposition(const SceneModel& scene, const std::string& compositionId,
		SceneCompositionRuntime& result, std::string& diagnostic) {
	result = SceneCompositionRuntime();
	diagnostic.clear();

	const SceneComposition* composition = nullptr;
	for (const auto& candidate : scene.compositions) {
		if (candidate.id == compositionId) {
			composition = &candidate;
			break;
		}
	}
	if (!composition) {
		diagnostic = "Unknown composition " + compositionId;
		return false;
	}
	if (composition->units.empty()) {
		diagnostic = "Composition " + compositionId + " has no units";
		return false;
	}

	std::unordered_map<std::string, const SceneTrainUnit*> unitsById;
	for (const auto& unit : scene.trainUnits)
		unitsById.emplace(unit.id, &unit);
	for (const auto& unitId : composition->units) {
		const auto it = unitsById.find(unitId);
		if (it == unitsById.end()) {
			diagnostic = "Missing unit " + unitId + " for composition " + compositionId;
			return false;
		}
		if (!it->second->hasPhysical || it->second->tractionCurve.empty()) {
			diagnostic = "Unit missing physical or traction data: " + unitId;
			return false;
		}
		result.units.push_back(it->second);
	}

	if (result.units.size() == 1) {
		result.physical = result.units.front()->physical;
		result.tractionCurve = result.units.front()->tractionCurve;
		return true;
	}

	double sumMassTraction = 0.0;
	double sumNumberWagons = 0.0;
	double sumWagonMass = 0.0;
	double minMaxSpeed = std::numeric_limits<double>::infinity();
	double minMaxDecel = std::numeric_limits<double>::infinity();
	const double firstFrontalArea = result.units.front()->physical.frontal_area_m2;
	double sumTotalMass = 0.0;
	double sumWeightedResistance = 0.0;
	double sumUnweightedResistance = 0.0;
	double minJerk = std::numeric_limits<double>::infinity();
	double sumLength = 0.0;

	for (const SceneTrainUnit* unit : result.units) {
		const SceneTrainPhysical& physical = unit->physical;
		sumMassTraction += physical.mass_of_traction_unit_kg;
		sumNumberWagons += physical.number_of_wagons;
		sumWagonMass += physical.number_of_wagons * physical.mass_of_a_wagon_kg;
		if (physical.max_speed_ms < minMaxSpeed)
			minMaxSpeed = physical.max_speed_ms;
		if (physical.max_deceleration_ms2 < minMaxDecel)
			minMaxDecel = physical.max_deceleration_ms2;
		const double unitTotalMass = physical.mass_of_traction_unit_kg
				+ physical.number_of_wagons * physical.mass_of_a_wagon_kg;
		sumTotalMass += unitTotalMass;
		sumWeightedResistance += unitTotalMass * physical.resistance_coefficient;
		sumUnweightedResistance += physical.resistance_coefficient;
		if (physical.jerk_ms3 < minJerk)
			minJerk = physical.jerk_ms3;
		sumLength += physical.length_m;
	}

	result.physical.mass_of_traction_unit_kg = sumMassTraction;
	result.physical.number_of_wagons = sumNumberWagons;
	result.physical.mass_of_a_wagon_kg = sumNumberWagons > 0.0
			? sumWagonMass / sumNumberWagons : 0.0;
	result.physical.max_speed_ms = minMaxSpeed;
	result.physical.max_deceleration_ms2 = minMaxDecel;
	result.physical.frontal_area_m2 = firstFrontalArea;
	result.physical.resistance_coefficient = sumTotalMass > 0.0
			? sumWeightedResistance / sumTotalMass
			: sumUnweightedResistance / result.units.size();
	result.physical.jerk_ms3 = minJerk;
	result.physical.length_m = sumLength;

	std::vector<double> boundaries;
	for (const SceneTrainUnit* unit : result.units) {
		for (const auto& row : unit->tractionCurve) {
			boundaries.push_back(row[0]);
			boundaries.push_back(row[1]);
		}
	}
	std::sort(boundaries.begin(), boundaries.end());
	const auto nearlyEqual = [](double left, double right) {
		return std::abs(left - right) <= 1e-9
				* std::max(1.0, std::max(std::abs(left), std::abs(right)));
	};
	boundaries.erase(std::unique(boundaries.begin(), boundaries.end(), nearlyEqual), boundaries.end());

	for (std::size_t index = 0; index + 1 < boundaries.size(); ++index) {
		const double lower = boundaries[index];
		const double upper = boundaries[index + 1];
		if (lower >= upper)
			continue;
		const double midpoint = (lower + upper) / 2.0;
		double sumC0 = 0.0;
		double sumC1 = 0.0;
		double sumC2 = 0.0;
		bool covered = false;
		for (const SceneTrainUnit* unit : result.units) {
			for (const auto& row : unit->tractionCurve) {
				if (midpoint >= row[0] && midpoint < row[1]) {
					sumC0 += row[2];
					sumC1 += row[3];
					sumC2 += row[4];
					covered = true;
				}
			}
		}
		if (covered)
			result.tractionCurve.push_back({lower, upper, sumC0, sumC1, sumC2});
	}

	if (result.tractionCurve.empty()) {
		diagnostic = "Combined traction curve is empty for composition " + compositionId;
		return false;
	}
	return true;
}

SceneScenario* defaultScenario(SceneModel& scene) {
	if (scene.scenarios.empty()) {
		SceneScenario baseline;
		baseline.id = "baseline";
		baseline.name = "Baseline";
		scene.scenarios.push_back(baseline);
	}
	if (scene.defaultScenarioId.empty())
		scene.defaultScenarioId = scene.scenarios.front().id;
	for (auto& scenario : scene.scenarios) {
		if (scenario.id == scene.defaultScenarioId)
			return &scenario;
	}
	return &scene.scenarios.front();
}

const SceneScenario* defaultScenario(const SceneModel& scene) {
	if (scene.scenarios.empty())
		return nullptr;
	if (scene.defaultScenarioId.empty())
		return &scene.scenarios.front();
	for (const auto& scenario : scene.scenarios) {
		if (scenario.id == scene.defaultScenarioId)
			return &scenario;
	}
	return &scene.scenarios.front();
}

std::vector<SceneIncident>& defaultScenarioIncidents(SceneModel& scene) {
	return defaultScenario(scene)->incidents;
}

const std::vector<SceneIncident>& defaultScenarioIncidents(const SceneModel& scene) {
	static const std::vector<SceneIncident> empty;
	const SceneScenario* scenario = defaultScenario(scene);
	return scenario ? scenario->incidents : empty;
}

void refreshLoadedDataSummary(SceneModel& scene) {
	scene.loadedData.clear();

	auto statusForFile = [&](const std::string& sourceFile, bool optional) {
		return scene.sourceFiles.count(sourceFile) > 0 ? "Parsed"
			: (optional ? "Missing optional" : "Invalid");
	};
	auto add = [&](const std::string& category, const std::string& sourceFile, int parsedCount,
			bool optional = false) {
		const std::string status = statusForFile(sourceFile, optional);
		SceneLoadedData data = makeLoadedData(category, sourceFile, parsedCount, status);
		if (!sourceFile.empty()) {
			const bool present = scene.sourceFiles.count(sourceFile) > 0;
			data.children.push_back(makeLoadedData("source_file", sourceFile, present ? 1 : 0,
					present ? "Loaded" : status));
			data.children.push_back(makeLoadedData("parsed_objects", sourceFile, parsedCount,
					present ? "Parsed" : status));
		}
		scene.loadedData.push_back(data);
	};
	auto addChild = [&](const std::string& category, const std::string& sourceFile, int parsedCount,
			const std::string& status) -> SceneLoadedData& {
		scene.loadedData.back().children.push_back(makeLoadedData(category, sourceFile, parsedCount, status));
		return scene.loadedData.back().children.back();
	};
	auto addTarget = [](SceneLoadedData& parent, const std::string& category,
			const std::string& sourceFile, const std::string& targetType) -> SceneLoadedData& {
		SceneLoadedData target = makeLoadedData(category, sourceFile, 1, "Parsed");
		target.targetType = targetType;
		parent.children.push_back(std::move(target));
		return parent.children.back();
	};

	add("scene", "scene.json", scene.schemaVersion > 0 ? 1 : 0);
	SceneLoadedData& importReport = addChild("import_report", "scene.json",
			static_cast<int>(scene.importReport.size()),
			scene.importReport.empty() ? "Missing optional" : "Parsed");
	bool importHasIssues = false;
	for (const auto& reportRow : scene.importReport) {
		const bool hasIssue = reportRow.skippedCount > 0 || reportRow.unresolvedReferences > 0;
		importHasIssues = importHasIssues || hasIssue;
		SceneLoadedData row = makeLoadedData(reportRow.category, reportRow.sourceFile,
				reportRow.convertedCount, hasIssue ? "Warning" : "Parsed");
		row.children.push_back(makeLoadedData("source_records", reportRow.sourceFile,
				reportRow.sourceCount, "Parsed"));
		row.children.push_back(makeLoadedData("skipped", reportRow.sourceFile,
				reportRow.skippedCount, reportRow.skippedCount > 0 ? "Warning" : "Ready"));
		row.children.push_back(makeLoadedData("unresolved_references", reportRow.sourceFile,
				reportRow.unresolvedReferences,
				reportRow.unresolvedReferences > 0 ? "Warning" : "Ready"));
		importReport.children.push_back(std::move(row));
	}
	if (importHasIssues) {
		importReport.status = "Warning";
		scene.loadedData.back().status = "Warning";
	}
	add("infrastructure", "infrastructure.json", static_cast<int>(scene.tracks.size() + scene.nodes.size()
			+ scene.arcs.size() + scene.blocks.size() + scene.connections.size()));
	scene.loadedData.back().targetType = "network";
	const std::string infrastructureStatus = scene.loadedData.back().status;
	addChild("tracks", "infrastructure.json", static_cast<int>(scene.tracks.size()), infrastructureStatus).targetType = "network";
	addChild("nodes", "infrastructure.json", static_cast<int>(scene.nodes.size()), infrastructureStatus).targetType = "network";
	addChild("arcs", "infrastructure.json", static_cast<int>(scene.arcs.size()), infrastructureStatus).targetType = "network";
	addChild("blocks", "infrastructure.json", static_cast<int>(scene.blocks.size()), infrastructureStatus).targetType = "network";
	addChild("connections", "infrastructure.json", static_cast<int>(scene.connections.size()), infrastructureStatus).targetType = "network";

	add("stations", "stations.json", static_cast<int>(scene.stations.size()));
	scene.loadedData.back().targetType = "network";
	int platformCount = 0;
	for (const auto& station : scene.stations)
		platformCount += static_cast<int>(station.platforms.size());
	addChild("platforms", "stations.json", platformCount, scene.loadedData.back().status).targetType = "network";

	add("timetable", "services.json", static_cast<int>(scene.services.size()));
	SceneLoadedData& services = addChild("services", "services.json",
			static_cast<int>(scene.services.size()), scene.loadedData.back().status);
	for (const auto& service : scene.services)
		addTarget(services, service.id, "services.json", "service");

	add("rolling_stock", "rolling_stock.json", static_cast<int>(scene.trainUnits.size()
			+ scene.compositions.size()));
	const std::string rollingStatus = scene.loadedData.back().status;
	SceneLoadedData& trainUnits = addChild("train_units", "rolling_stock.json",
			static_cast<int>(scene.trainUnits.size()), rollingStatus);
	for (const auto& unit : scene.trainUnits) {
		SceneLoadedData& unitRow = addTarget(trainUnits, unit.id, "rolling_stock.json", "train_unit");
		unitRow.children.push_back(makeLoadedData("train_unit_parameters", "rolling_stock.json",
				unit.hasPhysical ? 1 : 0, unit.hasPhysical ? "Parsed" : "Invalid"));
		SceneLoadedData curve = makeLoadedData("tractive_effort_curve", "rolling_stock.json",
				static_cast<int>(unit.tractionCurve.size()), unit.tractionCurve.empty() ? "Invalid" : "Parsed");
		if (!unit.tractionCurve.empty()) {
			SceneLoadedData plot = makeLoadedData("Plot tractive effort", "rolling_stock.json", 1, "Ready");
			plot.targetType = "train_unit_plot";
			curve.children.push_back(std::move(plot));
		}
		unitRow.children.push_back(std::move(curve));
		SceneLoadedData provenance = makeLoadedData("import_provenance", "", 0, "Missing optional");
		if (!unit.sourceDataFile.empty()) {
			provenance.children.push_back(makeLoadedData("original_parameter_source",
					unit.sourceDataFile, 1, "Parsed"));
			++provenance.parsedCount;
		}
		if (!unit.sourceTractionFile.empty()) {
			provenance.children.push_back(makeLoadedData("original_tractive_effort_source",
					unit.sourceTractionFile, 1, "Parsed"));
			++provenance.parsedCount;
		}
		if (provenance.parsedCount > 0)
			provenance.status = "Parsed";
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
	scene.loadedData.back().targetType = "network";
	const std::string signallingStatus = scene.loadedData.back().status;
	addChild("signals", "signalling.json", static_cast<int>(scene.signals.size()), signallingStatus).targetType = "network";
	addChild("signalling_areas", "signalling.json", static_cast<int>(scene.signallingAreas.size()), signallingStatus).targetType = "network";
	addChild("routes", "signalling.json", static_cast<int>(scene.routes.size()), signallingStatus).targetType = "network";
	addChild("dependencies", "signalling.json", static_cast<int>(scene.blockDependencies.size()), signallingStatus).targetType = "network";
	addChild("restrictions", "signalling.json", static_cast<int>(scene.singleTrackRestrictions.size()
			+ scene.stationBoundaries.size()), signallingStatus).targetType = "network";

	int incidentCount = 0;
	int entranceDelayCount = 0;
	for (const auto& scenario : scene.scenarios) {
		incidentCount += static_cast<int>(scenario.incidents.size());
		entranceDelayCount += static_cast<int>(scenario.entranceDelays.size());
	}
	const std::string scenarioSource = scene.sourceFiles.count("scenarios.json") > 0
			? "scenarios.json"
			: (scene.sourceFiles.count("incidents.json") > 0 ? "incidents.json" : "scenarios.json");
	add("scenarios", scenarioSource, static_cast<int>(scene.scenarios.size()), true);
	const std::string scenariosStatus = scene.loadedData.back().status;
	SceneLoadedData& scenarioItems = addChild("available_scenarios", scenarioSource,
			static_cast<int>(scene.scenarios.size()), scenariosStatus);
	const SceneScenario* selectedScenario = defaultScenario(static_cast<const SceneModel&>(scene));
	const std::string effectiveDefaultScenario = selectedScenario ? selectedScenario->id : std::string();
	for (const auto& scenario : scene.scenarios) {
		std::string label = "Scenario: " + (scenario.name.empty()
				? scenario.id : scenario.name + " [" + scenario.id + "]");
		if (scenario.id == effectiveDefaultScenario)
			label += " (default)";
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
}

void refreshLoadedDataDiagnostics(SceneModel& scene, const std::vector<SceneDiagnostic>& diagnostics) {
	if (scene.loadedData.empty())
		refreshLoadedDataSummary(scene);
	if (scene.loadedData.empty())
		return;

	std::vector<SceneDiagnosticCounts> counts(scene.loadedData.size());
	for (auto& item : scene.loadedData) {
		item.children.erase(std::remove_if(item.children.begin(), item.children.end(),
				[](const SceneLoadedData& child) { return child.category == "validation"; }),
			item.children.end());
	}
	for (const auto& diagnostic : diagnostics) {
		SceneDiagnosticCounts& count = counts[loadedDataIndexForDiagnostic(scene, diagnostic)];
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
	for (std::size_t i = 0; i < scene.loadedData.size(); ++i) {
		const SceneDiagnosticCounts& count = counts[i];
		const int total = count.errors + count.warnings + count.infos;
		if (count.errors > 0)
			scene.loadedData[i].status = "Invalid";
		else if (count.warnings > 0)
			scene.loadedData[i].status = "Warning";
		SceneLoadedData validation = makeLoadedData("validation", scene.loadedData[i].sourceFile,
				total, loadedDataDiagnosticStatus(count));
		validation.targetType = "validation";
		scene.loadedData[i].children.push_back(std::move(validation));
	}
}

void refreshSavedSceneMetadata(SceneModel& scene) {
	for (const char* file : {"scene.json", "infrastructure.json", "stations.json", "signalling.json",
			"rolling_stock.json", "services.json", "scenarios.json"})
		scene.sourceFiles.insert(file);
	scene.sourceFiles.erase("incidents.json");
	if (scene.passengers.empty())
		scene.sourceFiles.erase("passengers.json");
	else
		scene.sourceFiles.insert("passengers.json");
	if (scene.trackViews.empty() && scene.stationViews.empty())
		scene.sourceFiles.erase("views.json");
	else
		scene.sourceFiles.insert("views.json");
	refreshLoadedDataSummary(scene);
}

SceneLoadResult loadScene(const std::string& sceneDir) {
	SceneLoadResult result;
	auto addDiagnostic = [&](SceneSeverity severity, const std::string& code,
			const std::string& file, const std::string& message, const std::string& path = "") {
		SceneDiagnostic diagnostic;
		diagnostic.severity = severity;
		diagnostic.code = code;
		diagnostic.file = file;
		diagnostic.message = message;
		diagnostic.path = path;
		result.diagnostics.push_back(diagnostic);
	};
	auto addError = [&](const std::string& code, const std::string& file,
			const std::string& message, const std::string& path = "") {
		addDiagnostic(SceneSeverity::Error, code, file, message, path);
	};
	auto addWarning = [&](const std::string& code, const std::string& file,
			const std::string& message, const std::string& path = "") {
		addDiagnostic(SceneSeverity::Warning, code, file, message, path);
	};

	if (!fs::is_directory(sceneDir)) {
		addError("scene.dir.missing", "", "Scene directory missing or invalid");
		return result;
	}

	std::vector<std::pair<std::string, std::string>> inputFiles;
	auto parseObject = [&](const std::string& file, json& value, bool required, bool parseValue = true) {
		std::string content;
		if (!readFile(fs::path(sceneDir) / file, content)) {
			if (required)
				addError("scene.file.missing", file, "Required file is missing");
			return false;
		}
		if (canonicalSnapshotFile(file))
			inputFiles.emplace_back(file, content);
		if (!parseValue)
			return true;
		try {
			value = json::parse(content);
		} catch (const json::parse_error& error) {
			addError("scene.json.parse", file, std::string("JSON parse error: ") + error.what());
			return false;
		}
		if (!value.is_object()) {
			addError("scene.section.missing", file, "Root element must be an object");
			return false;
		}
		result.scene.sourceFiles.insert(file);
		return true;
	};
	auto arraySection = [&](const json& object, const char* key, const std::string& file,
			const std::string& path, bool required) {
		if (!object.is_object()) {
			addError("scene.item.invalid", file, "Expected an object", path);
			return false;
		}
		if (!object.contains(key)) {
			if (required)
				addError("scene.section.missing", file, std::string("Missing ") + key + " section",
						joinPath(path, key));
			return false;
		}
		if (!object[key].is_array()) {
			addError("scene.section.missing", file, std::string("Mistyped ") + key + " section",
					joinPath(path, key));
			return false;
		}
		return true;
	};
	auto stringField = [&](const json& object, const char* key, const std::string& file,
			const std::string& path, std::string& output, bool required = true) {
		if (!object.is_object()) {
			addError("scene.item.invalid", file, "Array item must be an object", path);
			return false;
		}
		if (!object.contains(key)) {
			if (required)
				addError("scene.field.missing", file, std::string("Missing ") + key,
						joinPath(path, key));
			return false;
		}
		if (!object[key].is_string() || (required && object[key].get<std::string>().empty())) {
			addError("scene.field.missing", file, std::string("Invalid ") + key,
					joinPath(path, key));
			return false;
		}
		output = object[key].get<std::string>();
		return true;
	};
	auto numberField = [&](const json& object, const char* key, const std::string& file,
			const std::string& path, double& output, bool required = true) {
		if (!object.is_object()) {
			addError("scene.item.invalid", file, "Array item must be an object", path);
			return false;
		}
		if (!object.contains(key)) {
			if (required)
				addError("scene.field.missing", file, std::string("Missing ") + key,
						joinPath(path, key));
			return false;
		}
		if (!object[key].is_number()) {
			addError("scene.field.missing", file, std::string("Invalid ") + key,
					joinPath(path, key));
			return false;
		}
		output = object[key].get<double>();
		return true;
	};
	auto integerField = [&](const json& object, const char* key, const std::string& file,
			const std::string& path, int& output, bool required = true) {
		if (!object.is_object()) {
			addError("scene.item.invalid", file, "Array item must be an object", path);
			return false;
		}
		if (!object.contains(key)) {
			if (required)
				addError("scene.field.missing", file, std::string("Missing ") + key,
						joinPath(path, key));
			return false;
		}
		if (!object[key].is_number_integer()) {
			addError("scene.field.missing", file, std::string("Invalid ") + key,
					joinPath(path, key));
			return false;
		}
		try {
			output = object[key].get<int>();
		} catch (const json::exception&) {
			addError("scene.field.missing", file, std::string("Invalid ") + key,
					joinPath(path, key));
			return false;
		}
		return true;
	};
	auto isSimulationResultField = [](const std::string& name) {
		return name == "results" || name == "simulation_results"
				|| name.find("simulated_") == 0 || name.find("actual_") == 0
				|| name == "delay_seconds"
				|| name.find("arrival_delay") != std::string::npos
				|| name.find("departure_delay") != std::string::npos;
	};

	json sceneJson;
	json infrastructureJson;
	json stationsJson;
	json signallingJson;
	json rollingStockJson;
	json servicesJson;
	json scenariosJson;
	json incidentsJson;
	json passengersJson;
	json viewsJson;
	const bool sceneOk = parseObject("scene.json", sceneJson, true);
	const bool infrastructureOk = parseObject("infrastructure.json", infrastructureJson, true);
	const bool stationsOk = parseObject("stations.json", stationsJson, true);
	const bool signallingOk = parseObject("signalling.json", signallingJson, true);
	const bool rollingStockOk = parseObject("rolling_stock.json", rollingStockJson, true);
	const bool servicesOk = parseObject("services.json", servicesJson, true);
	const bool scenariosPresent = fs::exists(fs::path(sceneDir) / "scenarios.json");
	const bool incidentsPresent = fs::exists(fs::path(sceneDir) / "incidents.json");
	const bool scenariosOk = parseObject("scenarios.json", scenariosJson, scenariosPresent);
	const bool incidentsOk = parseObject("incidents.json", incidentsJson, incidentsPresent, !scenariosPresent);
	const bool passengersOk = parseObject("passengers.json", passengersJson,
			fs::exists(fs::path(sceneDir) / "passengers.json"));
	const bool viewsOk = parseObject("views.json", viewsJson, false);

	if (sceneOk) {
		if (!sceneJson.contains("schema_version")) {
			addError("scene.version.missing", "scene.json", "Missing schema_version", "schema_version");
		} else if (!sceneJson["schema_version"].is_number_integer()
				|| sceneJson["schema_version"].get<int>() != 1) {
			addError("scene.version.unsupported", "scene.json",
					"Unsupported schema_version, must be the integer 1", "schema_version");
		} else {
			result.scene.schemaVersion = sceneJson["schema_version"].get<int>();
		}
		stringField(sceneJson, "name", "scene.json", "", result.scene.name);
		stringField(sceneJson, "saved_with_app_version", "scene.json", "",
				result.scene.savedWithAppVersion, false);
		stringField(sceneJson, "description", "scene.json", "", result.scene.description, false);
		stringField(sceneJson, "base_time", "scene.json", "", result.scene.baseTime, false);
		if (sceneJson.contains("units")) {
			const json& units = sceneJson["units"];
			if (!units.is_object()) {
				addError("scene.field.missing", "scene.json", "units must be an object", "units");
			} else {
				for (const auto& unit : {std::pair<const char*, const char*>("distance", "m"),
						std::pair<const char*, const char*>("time", "s"),
						std::pair<const char*, const char*>("speed", "m/s")}) {
					if (units.contains(unit.first) && (!units[unit.first].is_string()
							|| units[unit.first].get<std::string>() != unit.second)) {
						addError("scene.units.unsupported", "scene.json",
								std::string("Unsupported ") + unit.first + " unit",
								std::string("units.") + unit.first);
					}
				}
			}
		}
		if (sceneJson.contains("simulation_settings")) {
			const json& settings = sceneJson["simulation_settings"];
			if (!settings.is_object()) {
				addError("scene.field.missing", "scene.json",
						"simulation_settings must be an object", "simulation_settings");
			} else {
				result.scene.settings.hasDuration = numberField(settings, "duration_seconds", "scene.json",
						"simulation_settings", result.scene.settings.durationSeconds, false);
				result.scene.settings.hasBufferTime = numberField(settings, "buffer_time_seconds", "scene.json",
						"simulation_settings", result.scene.settings.bufferTimeSeconds, false);
				result.scene.settings.hasRecoveryTime = numberField(settings, "recovery_time_percent", "scene.json",
						"simulation_settings", result.scene.settings.recoveryTimePercent, false);
			}
		}
		if (sceneJson.contains("import_report")) {
			if (arraySection(sceneJson, "import_report", "scene.json", "", false)) {
				for (std::size_t index = 0; index < sceneJson["import_report"].size(); ++index) {
					const json& value = sceneJson["import_report"][index];
					const std::string path = "import_report[" + std::to_string(index) + "]";
					SceneImportReportRow row;
					stringField(value, "category", "scene.json", path, row.category);
					stringField(value, "source_file", "scene.json", path, row.sourceFile, false);
					integerField(value, "source_count", "scene.json", path, row.sourceCount);
					integerField(value, "converted_count", "scene.json", path, row.convertedCount);
					integerField(value, "skipped_count", "scene.json", path, row.skippedCount);
					integerField(value, "unresolved_references", "scene.json", path,
							row.unresolvedReferences);
					result.scene.importReport.push_back(row);
				}
			}
		}
	}

	if (infrastructureOk) {
		if (arraySection(infrastructureJson, "tracks", "infrastructure.json", "", false)) {
			for (std::size_t index = 0; index < infrastructureJson["tracks"].size(); ++index) {
				const std::string path = "tracks[" + std::to_string(index) + "]";
				SceneTrack track;
				stringField(infrastructureJson["tracks"][index], "id", "infrastructure.json", path, track.id);
				result.scene.tracks.push_back(track);
			}
		}
		if (arraySection(infrastructureJson, "nodes", "infrastructure.json", "", true)) {
			for (std::size_t index = 0; index < infrastructureJson["nodes"].size(); ++index) {
				const std::string path = "nodes[" + std::to_string(index) + "]";
				SceneNode node;
				stringField(infrastructureJson["nodes"][index], "id", "infrastructure.json", path, node.id);
				stringField(infrastructureJson["nodes"][index], "track", "infrastructure.json", path,
						node.trackId);
				numberField(infrastructureJson["nodes"][index], "x_km", "infrastructure.json", path,
						node.xKm);
				numberField(infrastructureJson["nodes"][index], "y_km", "infrastructure.json", path,
						node.yKm);
				result.scene.nodes.push_back(node);
			}
		}
		if (arraySection(infrastructureJson, "arcs", "infrastructure.json", "", true)) {
			for (std::size_t index = 0; index < infrastructureJson["arcs"].size(); ++index) {
				const std::string path = "arcs[" + std::to_string(index) + "]";
				SceneArc arc;
				stringField(infrastructureJson["arcs"][index], "id", "infrastructure.json", path, arc.id);
				stringField(infrastructureJson["arcs"][index], "track", "infrastructure.json", path,
						arc.trackId);
				stringField(infrastructureJson["arcs"][index], "from", "infrastructure.json", path,
						arc.fromNodeId);
				stringField(infrastructureJson["arcs"][index], "to", "infrastructure.json", path,
						arc.toNodeId);
				numberField(infrastructureJson["arcs"][index], "curvature_radius_m", "infrastructure.json",
						path, arc.curvatureRadiusM);
				numberField(infrastructureJson["arcs"][index], "gradient_percent", "infrastructure.json",
						path, arc.gradientPercent);
				numberField(infrastructureJson["arcs"][index], "speed_limit_ms", "infrastructure.json",
						path, arc.speedLimitMs);
				result.scene.arcs.push_back(arc);
			}
		}
		if (arraySection(infrastructureJson, "blocks", "infrastructure.json", "", false)) {
			for (std::size_t index = 0; index < infrastructureJson["blocks"].size(); ++index) {
				const std::string path = "blocks[" + std::to_string(index) + "]";
				SceneBlock block;
				stringField(infrastructureJson["blocks"][index], "id", "infrastructure.json", path,
						block.id);
				stringField(infrastructureJson["blocks"][index], "track", "infrastructure.json", path,
						block.trackId);
				numberField(infrastructureJson["blocks"][index], "length_km", "infrastructure.json", path,
						block.lengthKm);
				result.scene.blocks.push_back(block);
			}
		}
		if (arraySection(infrastructureJson, "connections", "infrastructure.json", "", false)) {
			for (std::size_t index = 0; index < infrastructureJson["connections"].size(); ++index) {
				const std::string path = "connections[" + std::to_string(index) + "]";
				SceneConnection connection;
				stringField(infrastructureJson["connections"][index], "id", "infrastructure.json", path,
						connection.id);
				stringField(infrastructureJson["connections"][index], "from", "infrastructure.json", path,
						connection.fromNodeId);
				stringField(infrastructureJson["connections"][index], "to", "infrastructure.json", path,
						connection.toNodeId);
				connection.hasSpeedLimit = numberField(infrastructureJson["connections"][index],
						"speed_limit_ms", "infrastructure.json", path, connection.speedLimitMs, false);
				result.scene.connections.push_back(connection);
			}
		}
	}

	if (stationsOk && arraySection(stationsJson, "stations", "stations.json", "", true)) {
		for (std::size_t index = 0; index < stationsJson["stations"].size(); ++index) {
			const std::string stationPath = "stations[" + std::to_string(index) + "]";
			const json& value = stationsJson["stations"][index];
			SceneStation station;
			stringField(value, "id", "stations.json", stationPath, station.id);
			stringField(value, "name", "stations.json", stationPath, station.name);
			station.hasPosition = numberField(value, "position_km", "stations.json", stationPath,
					station.positionKm, false);
			if (value.contains("platforms")) {
				if (!value["platforms"].is_array()) {
					addError("scene.field.missing", "stations.json", "platforms must be an array",
							stationPath + ".platforms");
				} else {
					for (std::size_t platformIndex = 0; platformIndex < value["platforms"].size();
							++platformIndex) {
						const std::string platformPath = stationPath + ".platforms["
								+ std::to_string(platformIndex) + "]";
						const json& platformValue = value["platforms"][platformIndex];
						ScenePlatform platform;
						stringField(platformValue, "id", "stations.json", platformPath, platform.id);
						platform.hasLength = numberField(platformValue, "length_m", "stations.json",
								platformPath, platform.lengthM, false);
						platform.hasWidth = numberField(platformValue, "width_m", "stations.json",
								platformPath, platform.widthM, false);
						if (platformValue.contains("nodes")) {
							if (!platformValue["nodes"].is_array()) {
								addError("scene.field.missing", "stations.json",
										"nodes must be an array", platformPath + ".nodes");
							} else {
								for (std::size_t nodeIndex = 0; nodeIndex < platformValue["nodes"].size();
										++nodeIndex) {
									if (!platformValue["nodes"][nodeIndex].is_string()) {
										addError("scene.field.missing", "stations.json",
												"Platform node reference must be a string",
												platformPath + ".nodes["
												+ std::to_string(nodeIndex) + "]");
										continue;
									}
									platform.nodeIds.push_back(platformValue["nodes"][nodeIndex].get<std::string>());
								}
							}
						}
						station.platforms.push_back(platform);
					}
				}
			}
			result.scene.stations.push_back(station);
		}
	}

	if (viewsOk) {
		const auto viewWarning = [&](const std::string& path, const std::string& message) {
			addWarning("scene.views.row", "views.json", message, path);
		};
		std::unordered_set<std::string> trackIds;
		for (const auto& track : result.scene.tracks)
			trackIds.insert(track.id);
		std::unordered_set<std::string> stationIds;
		for (const auto& station : result.scene.stations)
			stationIds.insert(station.id);

		if (viewsJson.contains("tracks")) {
			if (!viewsJson["tracks"].is_array()) {
				viewWarning("tracks", "views.tracks must be an array; display rows skipped");
			} else {
				for (std::size_t index = 0; index < viewsJson["tracks"].size(); ++index) {
					const std::string path = "tracks[" + std::to_string(index) + "]";
					const json& value = viewsJson["tracks"][index];
					if (!value.is_object() || !value.contains("track") || !value["track"].is_string()
							|| value["track"].get<std::string>().empty()
							|| !value.contains("level") || !value["level"].is_number_integer()
							|| !value.contains("region") || !value["region"].is_number_integer()) {
						viewWarning(path, "Invalid track display row; row skipped");
						continue;
					}
					SceneTrackView view;
					view.trackId = value["track"].get<std::string>();
					try {
						view.level = value["level"].get<int>();
						view.region = value["region"].get<int>();
						if (value.contains("visible"))
							view.visible = value["visible"].get<bool>();
					} catch (const json::exception&) {
						viewWarning(path, "Invalid track display row; row skipped");
						continue;
					}
					if (view.region < 0) {
						viewWarning(path, "Track display row has a negative region; row skipped");
						continue;
					}
					if (trackIds.count(view.trackId) == 0) {
						viewWarning(path, "Track display row refers to an unknown track; row skipped");
						continue;
					}
					const auto duplicate = std::find_if(result.scene.trackViews.begin(),
							result.scene.trackViews.end(), [&view](const SceneTrackView& candidate) {
								return candidate.trackId == view.trackId;
							});
					if (duplicate != result.scene.trackViews.end()) {
						viewWarning(path, "Duplicate track display row; row skipped");
						continue;
					}
					result.scene.trackViews.push_back(std::move(view));
				}
			}
		}

		if (viewsJson.contains("stations")) {
			if (!viewsJson["stations"].is_array()) {
				viewWarning("stations", "views.stations must be an array; display rows skipped");
			} else {
				for (std::size_t index = 0; index < viewsJson["stations"].size(); ++index) {
					const std::string path = "stations[" + std::to_string(index) + "]";
					const json& value = viewsJson["stations"][index];
					if (!value.is_object() || !value.contains("station") || !value["station"].is_string()
							|| value["station"].get<std::string>().empty()
							|| !value.contains("latitude") || !value["latitude"].is_number()
							|| !value.contains("longitude") || !value["longitude"].is_number()
							|| !value.contains("regions") || !value["regions"].is_array()) {
						viewWarning(path, "Invalid station display row; row skipped");
						continue;
					}
					SceneStationView view;
					view.stationId = value["station"].get<std::string>();
					view.latitude = value["latitude"].get<double>();
					view.longitude = value["longitude"].get<double>();
					if (!std::isfinite(view.latitude) || !std::isfinite(view.longitude)
							|| stationIds.count(view.stationId) == 0) {
						viewWarning(path, stationIds.count(view.stationId) == 0
								? "Station display row refers to an unknown station; row skipped"
								: "Station display row has non-finite coordinates; row skipped");
						continue;
					}
					bool validRegions = !value["regions"].empty();
					std::unordered_set<int> regionIds;
					for (std::size_t regionIndex = 0; regionIndex < value["regions"].size(); ++regionIndex) {
						const json& regionValue = value["regions"][regionIndex];
						if (!regionValue.is_object() || !regionValue.contains("id")
								|| !regionValue["id"].is_number_integer()
								|| !regionValue.contains("position_km")
								|| !regionValue["position_km"].is_number()) {
							validRegions = false;
							break;
						}
						try {
							const int regionId = regionValue["id"].get<int>();
							const double positionKm = regionValue["position_km"].get<double>();
							if (regionId < 0 || !std::isfinite(positionKm) || !regionIds.insert(regionId).second) {
								validRegions = false;
								break;
							}
							view.regions.emplace_back(regionId, positionKm);
						} catch (const json::exception&) {
							validRegions = false;
							break;
						}
					}
					if (!validRegions) {
						viewWarning(path, "Invalid station display region row; row skipped");
						continue;
					}
					if (value.contains("corridors")) {
						if (!value["corridors"].is_array()) {
							viewWarning(path, "Invalid station display corridors; row skipped");
							continue;
						}
						for (const auto& corridor : value["corridors"]) {
							if (!corridor.is_string()) {
								validRegions = false;
								break;
							}
							view.corridors.push_back(corridor.get<std::string>());
						}
						if (!validRegions) {
							viewWarning(path, "Invalid station display corridor row; row skipped");
							continue;
						}
					}
					const auto duplicate = std::find_if(result.scene.stationViews.begin(),
							result.scene.stationViews.end(), [&view](const SceneStationView& candidate) {
								return candidate.stationId == view.stationId;
							});
					if (duplicate != result.scene.stationViews.end()) {
						viewWarning(path, "Duplicate station display row; row skipped");
						continue;
					}
					result.scene.stationViews.push_back(std::move(view));
				}
			}
		}
	}

	if (signallingOk) {
		if (arraySection(signallingJson, "signals", "signalling.json", "", true)) {
			for (std::size_t index = 0; index < signallingJson["signals"].size(); ++index) {
				const std::string path = "signals[" + std::to_string(index) + "]";
				SceneSignal signal;
				stringField(signallingJson["signals"][index], "id", "signalling.json", path, signal.id);
				stringField(signallingJson["signals"][index], "protected_section", "signalling.json", path,
						signal.protectedSection, false);
				result.scene.signals.push_back(signal);
			}
		}
		if (arraySection(signallingJson, "signalling_areas", "signalling.json", "", false)) {
			for (std::size_t index = 0; index < signallingJson["signalling_areas"].size(); ++index) {
				const std::string path = "signalling_areas[" + std::to_string(index) + "]";
				const json& value = signallingJson["signalling_areas"][index];
				SceneSignallingArea area;
				stringField(value, "id", "signalling.json", path, area.id);
				numberField(value, "start_km", "signalling.json", path, area.startKm);
				numberField(value, "end_km", "signalling.json", path, area.endKm);
				integerField(value, "level", "signalling.json", path, area.level);
				stringField(value, "track", "signalling.json", path, area.trackId, false);
				result.scene.signallingAreas.push_back(std::move(area));
			}
		}
		if (arraySection(signallingJson, "routes", "signalling.json", "", true)) {
			for (std::size_t index = 0; index < signallingJson["routes"].size(); ++index) {
				const std::string path = "routes[" + std::to_string(index) + "]";
				const json& value = signallingJson["routes"][index];
				SceneRoute route;
				stringField(value, "id", "signalling.json", path, route.id);
				if (arraySection(value, "blocks", "signalling.json", path, true)) {
					for (std::size_t blockIndex = 0; blockIndex < value["blocks"].size(); ++blockIndex) {
						if (!value["blocks"][blockIndex].is_string()) {
							addError("scene.field.missing", "signalling.json",
									"Route block reference must be a string",
									path + ".blocks[" + std::to_string(blockIndex) + "]");
							continue;
						}
						route.blocks.push_back(value["blocks"][blockIndex].get<std::string>());
					}
				}
				route.hasCorridor = stringField(value, "corridor", "signalling.json", path,
						route.corridor, false);
				if (value.contains("reversed")) {
					if (!value["reversed"].is_boolean())
						addError("scene.field.missing", "signalling.json", "reversed must be a boolean",
								path + ".reversed");
					else
						route.reversed = value["reversed"].get<bool>();
				}
				result.scene.routes.push_back(route);
			}
		}
		if (arraySection(signallingJson, "block_dependencies", "signalling.json", "", false)) {
			for (std::size_t index = 0; index < signallingJson["block_dependencies"].size(); ++index) {
				const std::string path = "block_dependencies[" + std::to_string(index) + "]";
				SceneBlockDependency dependency;
				stringField(signallingJson["block_dependencies"][index], "block", "signalling.json", path,
						dependency.block);
				stringField(signallingJson["block_dependencies"][index], "depends_on", "signalling.json",
						path, dependency.dependsOn);
				result.scene.blockDependencies.push_back(dependency);
			}
		}
		if (arraySection(signallingJson, "single_track_restrictions", "signalling.json", "", false)) {
			for (std::size_t index = 0; index < signallingJson["single_track_restrictions"].size(); ++index) {
				const std::string path = "single_track_restrictions[" + std::to_string(index) + "]";
				const json& value = signallingJson["single_track_restrictions"][index];
				SceneSingleTrackRestriction restriction;
				stringField(value, "start_block", "signalling.json", path, restriction.startBlock);
				stringField(value, "end_block", "signalling.json", path, restriction.endBlock);
				stringField(value, "protected_start_block", "signalling.json", path,
						restriction.protectedStartBlock);
				stringField(value, "protected_end_block", "signalling.json", path,
						restriction.protectedEndBlock);
				result.scene.singleTrackRestrictions.push_back(restriction);
			}
		}
		if (arraySection(signallingJson, "station_boundaries", "signalling.json", "", false)) {
			for (std::size_t index = 0; index < signallingJson["station_boundaries"].size(); ++index) {
				const std::string path = "station_boundaries[" + std::to_string(index) + "]";
				const json& value = signallingJson["station_boundaries"][index];
				SceneStationBoundary boundary;
				stringField(value, "entrance_block", "signalling.json", path, boundary.entranceBlock);
				boundary.hasExitBlock = stringField(value, "exit_block", "signalling.json", path,
						boundary.exitBlock, false);
				if (value.contains("direction")) {
					if (!value["direction"].is_boolean())
						addError("scene.field.missing", "signalling.json", "direction must be a boolean",
								path + ".direction");
					else
						boundary.direction = value["direction"].get<bool>();
				}
				result.scene.stationBoundaries.push_back(boundary);
			}
		}
	}

	if (rollingStockOk) {
		if (arraySection(rollingStockJson, "train_units", "rolling_stock.json", "", true)) {
			for (std::size_t index = 0; index < rollingStockJson["train_units"].size(); ++index) {
				const std::string path = "train_units[" + std::to_string(index) + "]";
				const json& value = rollingStockJson["train_units"][index];
				SceneTrainUnit unit;
				stringField(value, "id", "rolling_stock.json", path, unit.id);
				if (value.contains("physical")) {
					if (!value["physical"].is_object()) {
						addError("scene.field.missing", "rolling_stock.json", "physical must be an object",
								path + ".physical");
					} else {
						const json& physical = value["physical"];
						unit.hasPhysical = true;
						numberField(physical, "mass_of_traction_unit_kg", "rolling_stock.json",
								path + ".physical", unit.physical.mass_of_traction_unit_kg);
						numberField(physical, "mass_of_a_wagon_kg", "rolling_stock.json",
								path + ".physical", unit.physical.mass_of_a_wagon_kg);
						numberField(physical, "number_of_wagons", "rolling_stock.json",
								path + ".physical", unit.physical.number_of_wagons);
						numberField(physical, "max_speed_ms", "rolling_stock.json",
								path + ".physical", unit.physical.max_speed_ms);
						numberField(physical, "max_deceleration_ms2", "rolling_stock.json",
								path + ".physical", unit.physical.max_deceleration_ms2);
						numberField(physical, "frontal_area_m2", "rolling_stock.json",
								path + ".physical", unit.physical.frontal_area_m2);
						numberField(physical, "resistance_coefficient", "rolling_stock.json",
								path + ".physical", unit.physical.resistance_coefficient);
						numberField(physical, "jerk_ms3", "rolling_stock.json",
								path + ".physical", unit.physical.jerk_ms3);
						numberField(physical, "length_m", "rolling_stock.json",
								path + ".physical", unit.physical.length_m);
					}
				}
				if (value.contains("traction_curve")) {
					if (!value["traction_curve"].is_array()) {
						addError("scene.field.missing", "rolling_stock.json",
								"traction_curve must be an array", path + ".traction_curve");
					} else {
						for (std::size_t rowIndex = 0; rowIndex < value["traction_curve"].size(); ++rowIndex) {
							const json& row = value["traction_curve"][rowIndex];
							const std::string rowPath = path + ".traction_curve["
									+ std::to_string(rowIndex) + "]";
							if (!row.is_array() || row.size() != 5) {
								addError("scene.field.missing", "rolling_stock.json",
										"traction_curve row must contain five numbers", rowPath);
								continue;
							}
							std::array<double, 5> values{};
							bool valid = true;
							for (std::size_t valueIndex = 0; valueIndex < values.size(); ++valueIndex) {
								if (!row[valueIndex].is_number()) {
									valid = false;
									break;
								}
								values[valueIndex] = row[valueIndex].get<double>();
							}
							if (!valid) {
								addError("scene.field.missing", "rolling_stock.json",
										"traction_curve row must contain numbers", rowPath);
							} else {
								unit.tractionCurve.push_back(values);
							}
						}
					}
				}
				if (value.contains("source")) {
					if (!value["source"].is_object()) {
						addError("scene.field.missing", "rolling_stock.json", "source must be an object",
								path + ".source");
					} else {
						const json& source = value["source"];
						stringField(source, "data_file", "rolling_stock.json", path + ".source",
								unit.sourceDataFile, false);
						stringField(source, "traction_file", "rolling_stock.json", path + ".source",
								unit.sourceTractionFile, false);
					}
				}
				result.scene.trainUnits.push_back(unit);
			}
		}
		if (arraySection(rollingStockJson, "compositions", "rolling_stock.json", "", true)) {
			for (std::size_t index = 0; index < rollingStockJson["compositions"].size(); ++index) {
				const std::string path = "compositions[" + std::to_string(index) + "]";
				const json& value = rollingStockJson["compositions"][index];
				SceneComposition composition;
				stringField(value, "id", "rolling_stock.json", path, composition.id);
				if (!arraySection(value, "units", "rolling_stock.json", path, true)) {
					result.scene.compositions.push_back(composition);
					continue;
				}
				for (std::size_t unitIndex = 0; unitIndex < value["units"].size(); ++unitIndex) {
					if (!value["units"][unitIndex].is_string()) {
						addError("scene.field.missing", "rolling_stock.json",
								"Composition unit reference must be a string",
								path + ".units[" + std::to_string(unitIndex) + "]");
						continue;
					}
					composition.units.push_back(value["units"][unitIndex].get<std::string>());
				}
				result.scene.compositions.push_back(composition);
			}
		}
	}

	if (servicesOk && arraySection(servicesJson, "services", "services.json", "", true)) {
		for (std::size_t index = 0; index < servicesJson["services"].size(); ++index) {
			const std::string path = "services[" + std::to_string(index) + "]";
			const json& value = servicesJson["services"][index];
			SceneService service;
			stringField(value, "id", "services.json", path, service.id);
			stringField(value, "operating_code", "services.json", path, service.operatingCode, false);
			stringField(value, "composition", "services.json", path, service.composition);
			stringField(value, "route", "services.json", path, service.route);
			numberField(value, "performance_percent", "services.json", path,
				service.performancePercent, false);
			service.hasMaximumSpeed = numberField(value, "maximum_speed_kmh", "services.json", path,
				service.maximumSpeedKmh, false);
			if (value.contains("through")) {
				if (!value["through"].is_boolean())
					addError("scene.field.missing", "services.json", "through must be a boolean",
							path + ".through");
				else
					service.through = value["through"].get<bool>();
			}
			service.hasEntryTime = numberField(value, "entry_time_seconds", "services.json", path,
					service.entryTimeSeconds, false);
			if (value.contains("repeat")) {
				if (!value["repeat"].is_object()) {
					addError("scene.field.missing", "services.json", "repeat must be an object",
							path + ".repeat");
				} else {
					service.hasRepeat = true;
					numberField(value["repeat"], "headway_seconds", "services.json", path + ".repeat",
							service.headwaySeconds);
					service.hasRepeatCount = integerField(value["repeat"], "count", "services.json",
							path + ".repeat", service.repeatCount, false);
					service.hasOperatingCodeStep = integerField(value["repeat"], "operating_code_step",
							"services.json", path + ".repeat", service.operatingCodeStep, false);
				}
			}
			if (arraySection(value, "stops", "services.json", path, true)) {
				for (std::size_t stopIndex = 0; stopIndex < value["stops"].size(); ++stopIndex) {
					const std::string stopPath = path + ".stops[" + std::to_string(stopIndex) + "]";
					const json& stopValue = value["stops"][stopIndex];
					SceneStop stop;
					if (stopValue.is_object()) {
						for (const auto& field : stopValue.items()) {
							if (isSimulationResultField(field.key()))
								addError("scene.timetable.results", "services.json",
										"Service stop input must not contain simulation-result fields",
										stopPath + "." + field.key());
						}
					}
					stringField(stopValue, "station", "services.json", stopPath, stop.stationId);
					stringField(stopValue, "platform", "services.json", stopPath, stop.platformId, false);
					stop.hasPlannedArrival = numberField(stopValue, "planned_arrival_seconds", "services.json",
							stopPath, stop.plannedArrivalSeconds, false);
					if (!stop.hasPlannedArrival)
						stop.hasPlannedArrival = numberField(stopValue, "arrival_seconds", "services.json",
								stopPath, stop.plannedArrivalSeconds, false);
					stop.hasPlannedDeparture = numberField(stopValue, "planned_departure_seconds", "services.json",
							stopPath, stop.plannedDepartureSeconds, false);
					if (!stop.hasPlannedDeparture)
						stop.hasPlannedDeparture = numberField(stopValue, "departure_seconds", "services.json",
								stopPath, stop.plannedDepartureSeconds, false);
					numberField(stopValue, "dwell_seconds", "services.json", stopPath, stop.dwellSeconds);
					service.stops.push_back(stop);
				}
			}
			result.scene.services.push_back(service);
		}
	}

	auto parseIncidentArray = [&](const json& root, const std::string& file, const std::string& path,
			std::vector<SceneIncident>& output) {
		if (!arraySection(root, "incidents", file, path, true))
			return;
		for (std::size_t index = 0; index < root["incidents"].size(); ++index) {
			const std::string incidentPath = path.empty()
					? "incidents[" + std::to_string(index) + "]"
					: path + ".incidents[" + std::to_string(index) + "]";
			const json& value = root["incidents"][index];
			SceneIncident incident;
			stringField(value, "id", file, incidentPath, incident.id);
			stringField(value, "type", file, incidentPath, incident.type);
			stringField(value, "target", file, incidentPath, incident.target);
			numberField(value, "start_seconds", file, incidentPath, incident.startSeconds);
			incident.hasEndSeconds = numberField(value, "end_seconds", file, incidentPath,
					incident.endSeconds, false);
			incident.hasOccurrence = integerField(value, "occurrence", file, incidentPath,
					incident.occurrence, false);
			incident.hasReducedSpeed = numberField(value, "reduced_speed_kmh", file, incidentPath,
					incident.reducedSpeedKmh, false);
			if (value.contains("terminate_at_destination")) {
				if (!value["terminate_at_destination"].is_boolean()) {
					addError("scene.field.missing", file, "Invalid terminate_at_destination",
							joinPath(incidentPath, "terminate_at_destination"));
				} else {
					incident.terminateAtDestination = value["terminate_at_destination"].get<bool>();
				}
			}
			output.push_back(incident);
		}
	};

	if (scenariosPresent && incidentsPresent) {
		addWarning("scene.compatibility.incidents_ignored", "incidents.json",
				"scenarios.json is authoritative; incidents.json was ignored for compatibility");
	}
	if (scenariosPresent) {
		if (scenariosOk) {
			stringField(scenariosJson, "default_scenario_id", "scenarios.json", "",
					result.scene.defaultScenarioId);
			if (arraySection(scenariosJson, "scenarios", "scenarios.json", "", true)) {
				for (std::size_t index = 0; index < scenariosJson["scenarios"].size(); ++index) {
					const std::string path = "scenarios[" + std::to_string(index) + "]";
					const json& value = scenariosJson["scenarios"][index];
					SceneScenario scenario;
					stringField(value, "id", "scenarios.json", path, scenario.id);
					stringField(value, "name", "scenarios.json", path, scenario.name);
					stringField(value, "description", "scenarios.json", path, scenario.description, false);
					parseIncidentArray(value, "scenarios.json", path, scenario.incidents);
					if (arraySection(value, "entrance_delays", "scenarios.json", path, false)) {
						for (std::size_t delayIndex = 0; delayIndex < value["entrance_delays"].size();
								++delayIndex) {
							const std::string delayPath = path + ".entrance_delays["
									+ std::to_string(delayIndex) + "]";
							const json& delayValue = value["entrance_delays"][delayIndex];
							SceneEntranceDelay delay;
							stringField(delayValue, "service", "scenarios.json", delayPath, delay.serviceId);
							integerField(delayValue, "occurrence", "scenarios.json", delayPath,
									delay.occurrence, false);
							stringField(delayValue, "station", "scenarios.json", delayPath, delay.stationId);
							numberField(delayValue, "delay_seconds", "scenarios.json", delayPath,
								delay.delaySeconds);
							scenario.entranceDelays.push_back(delay);
						}
					}
					result.scene.scenarios.push_back(scenario);
				}
			}
		}
	} else {
		SceneScenario baseline;
		baseline.id = "baseline";
		baseline.name = "Baseline";
		result.scene.defaultScenarioId = baseline.id;
		if (incidentsPresent && incidentsOk)
			parseIncidentArray(incidentsJson, "incidents.json", "", baseline.incidents);
		result.scene.scenarios.push_back(baseline);
	}

	if (passengersOk && arraySection(passengersJson, "passengers", "passengers.json", "", true)) {
		auto rejectSimulationResultFields = [&](const json& object, const std::string& path) {
			if (!object.is_object())
				return;
			for (const auto& key : object.items()) {
				if (isSimulationResultField(key.key())) {
					addError("scene.passengers.results", "passengers.json",
							"Passenger input must not contain simulation-result fields",
							path + "." + key.key());
				}
			}
		};
		for (std::size_t passengerIndex = 0; passengerIndex < passengersJson["passengers"].size();
				++passengerIndex) {
			const std::string passengerPath = "passengers[" + std::to_string(passengerIndex) + "]";
			const json& value = passengersJson["passengers"][passengerIndex];
			ScenePassenger passenger;
			stringField(value, "id", "passengers.json", passengerPath, passenger.id);
			rejectSimulationResultFields(value, passengerPath);
			if (arraySection(value, "journeys", "passengers.json", passengerPath, true)) {
				for (std::size_t journeyIndex = 0; journeyIndex < value["journeys"].size(); ++journeyIndex) {
					const std::string journeyPath = passengerPath + ".journeys["
							+ std::to_string(journeyIndex) + "]";
					const json& journeyValue = value["journeys"][journeyIndex];
					ScenePassengerJourney journey;
					rejectSimulationResultFields(journeyValue, journeyPath);
					stringField(journeyValue, "id", "passengers.json", journeyPath, journey.id);
					stringField(journeyValue, "activity", "passengers.json", journeyPath, journey.activity, false);
					stringField(journeyValue, "origin", "passengers.json", journeyPath,
							journey.originStationId);
					stringField(journeyValue, "destination", "passengers.json", journeyPath,
							journey.destinationStationId);
					if (journeyValue.contains("planned_departure")
							&& journeyValue["planned_departure"].is_object()) {
						const std::string windowPath = journeyPath + ".planned_departure";
						numberField(journeyValue["planned_departure"], "start_seconds", "passengers.json",
								windowPath, journey.plannedDepartureStartSeconds);
						numberField(journeyValue["planned_departure"], "end_seconds", "passengers.json",
								windowPath, journey.plannedDepartureEndSeconds);
					} else {
						addError("scene.field.missing", "passengers.json",
								"Missing planned_departure window", journeyPath + ".planned_departure");
					}
					if (journeyValue.contains("planned_arrival")
							&& journeyValue["planned_arrival"].is_object()) {
						const std::string windowPath = journeyPath + ".planned_arrival";
						numberField(journeyValue["planned_arrival"], "start_seconds", "passengers.json",
								windowPath, journey.plannedArrivalStartSeconds);
						numberField(journeyValue["planned_arrival"], "end_seconds", "passengers.json",
								windowPath, journey.plannedArrivalEndSeconds);
					} else {
						addError("scene.field.missing", "passengers.json",
								"Missing planned_arrival window", journeyPath + ".planned_arrival");
					}
					if (arraySection(journeyValue, "legs", "passengers.json", journeyPath, true)) {
						for (std::size_t legIndex = 0; legIndex < journeyValue["legs"].size(); ++legIndex) {
							const std::string legPath = journeyPath + ".legs["
									+ std::to_string(legIndex) + "]";
							const json& legValue = journeyValue["legs"][legIndex];
							ScenePassengerLeg leg;
							rejectSimulationResultFields(legValue, legPath);
							stringField(legValue, "id", "passengers.json", legPath, leg.id);
							stringField(legValue, "origin", "passengers.json", legPath, leg.originStationId);
							stringField(legValue, "destination", "passengers.json", legPath,
									leg.destinationStationId);
							stringField(legValue, "service", "passengers.json", legPath, leg.serviceId);
							integerField(legValue, "occurrence", "passengers.json", legPath,
									leg.occurrence, false);
							journey.legs.push_back(leg);
						}
					}
					passenger.journeys.push_back(journey);
				}
			}
			result.scene.passengers.push_back(passenger);
		}
	}

	refreshLoadedDataSummary(result.scene);
	result.inputSnapshot = buildSceneDirectorySnapshot(inputFiles);
	return result;
}
