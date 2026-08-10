#include "scene/SceneWriter.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <utility>

using json = nlohmann::json;
namespace fs = std::filesystem;

bool SceneSaveResult::success() const {
	return wroteAll && !hasErrors(diagnostics);
}

bool ScenarioLoadResult::success() const {
	return !hasErrors(diagnostics);
}

static void addWriteError(SceneSaveResult& result, const std::string& file,
		const std::string& message) {
	SceneDiagnostic diagnostic;
	diagnostic.severity = SceneSeverity::Error;
	diagnostic.code = "scene.save.write";
	diagnostic.file = file;
	diagnostic.message = message;
	result.diagnostics.push_back(diagnostic);
}

static bool writeJsonFile(SceneSaveResult& result, const fs::path& scenePath,
		const std::string& filename, const json& value) {
	std::ofstream output(scenePath / filename);
	if (!output) {
		addWriteError(result, filename, "Cannot open " + filename + " for writing");
		return false;
	}
	output << value.dump(4) << "\n";
	if (!output) {
		addWriteError(result, filename, "Cannot write " + filename);
		return false;
	}
	return true;
}

static json writePhysical(const SceneTrainPhysical& physical) {
	return {
		{"mass_of_traction_unit_kg", physical.mass_of_traction_unit_kg},
		{"mass_of_a_wagon_kg", physical.mass_of_a_wagon_kg},
		{"number_of_wagons", physical.number_of_wagons},
		{"max_speed_ms", physical.max_speed_ms},
		{"max_deceleration_ms2", physical.max_deceleration_ms2},
		{"frontal_area_m2", physical.frontal_area_m2},
		{"resistance_coefficient", physical.resistance_coefficient},
		{"jerk_ms3", physical.jerk_ms3},
		{"length_m", physical.length_m},
	};
}

static json writeTrainUnits(const SceneModel& scene) {
	json trainUnits = json::array();
	for (const auto& unit : scene.trainUnits) {
		json value = {{"id", unit.id}};
		if (unit.hasPhysical)
			value["physical"] = writePhysical(unit.physical);
		if (!unit.tractionCurve.empty()) {
			value["traction_curve"] = json::array();
			for (const auto& row : unit.tractionCurve)
				value["traction_curve"].push_back({row[0], row[1], row[2], row[3], row[4]});
		}
		if (!unit.sourceDataFile.empty() || !unit.sourceTractionFile.empty()) {
			json source = json::object();
			if (!unit.sourceDataFile.empty())
				source["data_file"] = unit.sourceDataFile;
			if (!unit.sourceTractionFile.empty())
				source["traction_file"] = unit.sourceTractionFile;
			value["source"] = source;
		}
		trainUnits.push_back(value);
	}
	return trainUnits;
}

static json writeCompositions(const SceneModel& scene) {
	json compositions = json::array();
	for (const auto& composition : scene.compositions) {
		compositions.push_back({{"id", composition.id}, {"units", composition.units}});
	}
	return compositions;
}

static json writeStops(const SceneService& service) {
	json stops = json::array();
	for (const auto& stop : service.stops) {
		json value = {
			{"station", stop.stationId},
			{"dwell_seconds", stop.dwellSeconds},
		};
		if (!stop.platformId.empty())
			value["platform"] = stop.platformId;
		if (stop.hasPlannedArrival)
			value["planned_arrival_seconds"] = stop.plannedArrivalSeconds;
		if (stop.hasPlannedDeparture)
			value["planned_departure_seconds"] = stop.plannedDepartureSeconds;
		stops.push_back(value);
	}
	return stops;
}

static json writeServices(const SceneModel& scene) {
	json services = json::array();
	for (const auto& service : scene.services) {
		json value = {
			{"id", service.id},
			{"composition", service.composition},
			{"route", service.route},
			{"stops", writeStops(service)},
		};
		if (!service.operatingCode.empty())
			value["operating_code"] = service.operatingCode;
		if (service.performancePercent != 100.0)
			value["performance_percent"] = service.performancePercent;
		if (service.hasMaximumSpeed)
			value["maximum_speed_kmh"] = service.maximumSpeedKmh;
		if (service.through)
			value["through"] = true;
		if (service.hasEntryTime)
			value["entry_time_seconds"] = service.entryTimeSeconds;
		if (service.hasRepeat) {
			value["repeat"] = {{"headway_seconds", service.headwaySeconds}};
			if (service.hasRepeatCount)
				value["repeat"]["count"] = service.repeatCount;
			if (service.hasOperatingCodeStep)
				value["repeat"]["operating_code_step"] = service.operatingCodeStep;
		}
		services.push_back(value);
	}
	return services;
}

static json writeScenarioValue(const SceneScenario& scenario) {
	json value = {
		{"id", scenario.id},
		{"name", scenario.name},
		{"incidents", json::array()},
		{"entrance_delays", json::array()},
	};
	if (!scenario.description.empty())
		value["description"] = scenario.description;
	for (const auto& incident : scenario.incidents) {
		value["incidents"].push_back({
			{"id", incident.id},
			{"type", incident.type},
			{"target", incident.target},
			{"start_seconds", incident.startSeconds},
			{"end_seconds", incident.endSeconds},
		});
	}
	for (const auto& delay : scenario.entranceDelays) {
		value["entrance_delays"].push_back({
			{"service", delay.serviceId},
			{"occurrence", delay.occurrence},
			{"station", delay.stationId},
			{"delay_seconds", delay.delaySeconds},
		});
	}
	return value;
}

static json writeScenarios(const SceneModel& scene) {
	json scenarios = json::array();
	if (scene.scenarios.empty()) {
		SceneScenario baseline;
		baseline.id = "baseline";
		baseline.name = "Baseline";
		scenarios.push_back(writeScenarioValue(baseline));
	} else {
		for (const auto& scenario : scene.scenarios)
			scenarios.push_back(writeScenarioValue(scenario));
	}
	std::string defaultId = scene.defaultScenarioId;
	if (defaultId.empty())
		defaultId = scene.scenarios.empty() ? "baseline" : scene.scenarios.front().id;
	return {{"default_scenario_id", defaultId}, {"scenarios", scenarios}};
}

static void addScenarioDiagnostic(ScenarioLoadResult& result, SceneSeverity severity,
		const std::string& code, const std::string& message, const std::string& path = "") {
	SceneDiagnostic diagnostic;
	diagnostic.severity = severity;
	diagnostic.code = code;
	diagnostic.file = "scenario.json";
	diagnostic.message = message;
	diagnostic.path = path;
	result.diagnostics.push_back(std::move(diagnostic));
}

static bool scenarioString(const json& object, const char* key, ScenarioLoadResult& result,
		std::string& output, bool required, const std::string& path) {
	if (!object.is_object()) {
		addScenarioDiagnostic(result, SceneSeverity::Error, "scene.scenario.item.invalid",
				"Scenario field must be an object", path);
		return false;
	}
	if (!object.contains(key)) {
		if (required)
			addScenarioDiagnostic(result, SceneSeverity::Error, "scene.scenario.field.missing",
					std::string("Missing ") + key, path + "." + key);
		return false;
	}
	if (!object[key].is_string() || (required && object[key].get<std::string>().empty())) {
		addScenarioDiagnostic(result, SceneSeverity::Error, "scene.scenario.field.type",
				std::string("Invalid ") + key, path + "." + key);
		return false;
	}
	output = object[key].get<std::string>();
	return true;
}

static bool scenarioNumber(const json& object, const char* key, ScenarioLoadResult& result,
		double& output, const std::string& path) {
	if (!object.contains(key)) {
		addScenarioDiagnostic(result, SceneSeverity::Error, "scene.scenario.field.missing",
				std::string("Missing ") + key, path + "." + key);
		return false;
	}
	if (!object[key].is_number()) {
		addScenarioDiagnostic(result, SceneSeverity::Error, "scene.scenario.field.type",
				std::string("Invalid ") + key, path + "." + key);
		return false;
	}
	try {
		output = object[key].get<double>();
	} catch (const json::exception&) {
		addScenarioDiagnostic(result, SceneSeverity::Error, "scene.scenario.field.type",
				std::string("Invalid ") + key, path + "." + key);
		return false;
	}
	return true;
}

static bool scenarioInteger(const json& object, const char* key, ScenarioLoadResult& result,
		int& output, const std::string& path) {
	if (!object.contains(key))
		return true;
	if (!object[key].is_number_integer()) {
		addScenarioDiagnostic(result, SceneSeverity::Error, "scene.scenario.field.type",
				std::string("Invalid ") + key, path + "." + key);
		return false;
	}
	try {
		output = object[key].get<int>();
	} catch (const json::exception&) {
		addScenarioDiagnostic(result, SceneSeverity::Error, "scene.scenario.field.type",
				std::string("Invalid ") + key, path + "." + key);
		return false;
	}
	return true;
}

static void parseScenarioIncident(const json& value, std::size_t index,
		ScenarioLoadResult& result) {
	const std::string path = "incidents[" + std::to_string(index) + "]";
	if (!value.is_object()) {
		addScenarioDiagnostic(result, SceneSeverity::Error, "scene.scenario.item.invalid",
				"Incident must be an object", path);
		return;
	}
	SceneIncident incident;
	scenarioString(value, "id", result, incident.id, true, path);
	scenarioString(value, "type", result, incident.type, true, path);
	scenarioString(value, "target", result, incident.target, true, path);
	scenarioNumber(value, "start_seconds", result, incident.startSeconds, path);
	scenarioNumber(value, "end_seconds", result, incident.endSeconds, path);
	result.scenario.incidents.push_back(std::move(incident));
}

static void parseScenarioEntranceDelay(const json& value, std::size_t index,
		ScenarioLoadResult& result) {
	const std::string path = "entrance_delays[" + std::to_string(index) + "]";
	if (!value.is_object()) {
		addScenarioDiagnostic(result, SceneSeverity::Error, "scene.scenario.item.invalid",
				"Entrance delay must be an object", path);
		return;
	}
	SceneEntranceDelay delay;
	scenarioString(value, "service", result, delay.serviceId, true, path);
	scenarioInteger(value, "occurrence", result, delay.occurrence, path);
	scenarioString(value, "station", result, delay.stationId, true, path);
	scenarioNumber(value, "delay_seconds", result, delay.delaySeconds, path);
	result.scenario.entranceDelays.push_back(std::move(delay));
}

SceneSaveResult saveScenarioJson(const SceneScenario& scenario, const std::string& filePath) {
	SceneSaveResult result;
	const fs::path path(filePath);
	result.wroteAll = writeJsonFile(result, path.parent_path(), path.filename().string(),
		writeScenarioValue(scenario));
	return result;
}

ScenarioLoadResult loadScenarioJson(const std::string& filePath) {
	ScenarioLoadResult result;
	std::ifstream input{fs::path(filePath)};
	if (!input) {
		addScenarioDiagnostic(result, SceneSeverity::Error, "scene.scenario.file.missing",
				"Cannot open scenario JSON");
		return result;
	}

	json value;
	try {
		input >> value;
	} catch (const json::parse_error& error) {
		addScenarioDiagnostic(result, SceneSeverity::Error, "scene.scenario.json.parse",
				std::string("JSON parse error: ") + error.what());
		return result;
	}
	if (!value.is_object()) {
		addScenarioDiagnostic(result, SceneSeverity::Error, "scene.scenario.root.type",
				"Scenario JSON root must be an object");
		return result;
	}

	const std::set<std::string> allowed = {"id", "name", "description", "incidents", "entrance_delays"};
	for (const auto& field : value.items()) {
		if (allowed.find(field.key()) == allowed.end())
			addScenarioDiagnostic(result, SceneSeverity::Error, "scene.scenario.field.unknown",
					"Scenario JSON does not allow field " + field.key(), field.key());
	}
	scenarioString(value, "id", result, result.scenario.id, true, "scenario");
	scenarioString(value, "name", result, result.scenario.name, true, "scenario");
	scenarioString(value, "description", result, result.scenario.description, false, "scenario");
	if (!value.contains("incidents") || !value["incidents"].is_array()) {
		addScenarioDiagnostic(result, SceneSeverity::Error, "scene.scenario.section.type",
				"incidents must be an array", "incidents");
	} else {
		for (std::size_t index = 0; index < value["incidents"].size(); ++index)
			parseScenarioIncident(value["incidents"][index], index, result);
	}
	if (value.contains("entrance_delays")) {
		if (!value["entrance_delays"].is_array()) {
			addScenarioDiagnostic(result, SceneSeverity::Error, "scene.scenario.section.type",
					"entrance_delays must be an array", "entrance_delays");
		} else {
			for (std::size_t index = 0; index < value["entrance_delays"].size(); ++index)
				parseScenarioEntranceDelay(value["entrance_delays"][index], index, result);
		}
	}
	return result;
}

static json writePassengers(const SceneModel& scene) {
	json passengers = json::array();
	for (const auto& passenger : scene.passengers) {
		json value = {{"id", passenger.id}, {"journeys", json::array()}};
		for (const auto& journey : passenger.journeys) {
			json journeyValue = {
				{"id", journey.id},
				{"origin", journey.originStationId},
				{"destination", journey.destinationStationId},
				{"planned_departure", {
					{"start_seconds", journey.plannedDepartureStartSeconds},
					{"end_seconds", journey.plannedDepartureEndSeconds},
				}},
				{"planned_arrival", {
					{"start_seconds", journey.plannedArrivalStartSeconds},
					{"end_seconds", journey.plannedArrivalEndSeconds},
				}},
				{"legs", json::array()},
			};
			if (!journey.activity.empty())
				journeyValue["activity"] = journey.activity;
			for (const auto& leg : journey.legs) {
				journeyValue["legs"].push_back({
					{"id", leg.id},
					{"origin", leg.originStationId},
					{"destination", leg.destinationStationId},
					{"service", leg.serviceId},
					{"occurrence", leg.occurrence},
				});
			}
			value["journeys"].push_back(journeyValue);
		}
		passengers.push_back(value);
	}
	return {{"passengers", passengers}};
}

SceneSaveResult saveScene(const SceneModel& scene, const std::string& sceneDir) {
	SceneSaveResult result;
	const fs::path scenePath(sceneDir);
	std::error_code ec;
	fs::create_directories(scenePath, ec);
	if (ec) {
		addWriteError(result, "", "Cannot create scene directory: " + ec.message());
		return result;
	}

	json sceneJson = {
		{"schema_version", scene.schemaVersion},
		{"name", scene.name},
		{"units", {{"distance", "m"}, {"time", "s"}, {"speed", "m/s"}}},
	};
	if (!scene.description.empty())
		sceneJson["description"] = scene.description;
	if (!scene.baseTime.empty())
		sceneJson["base_time"] = scene.baseTime;
	json settings = json::object();
	if (scene.settings.hasDuration)
		settings["duration_seconds"] = scene.settings.durationSeconds;
	if (scene.settings.hasBufferTime)
		settings["buffer_time_seconds"] = scene.settings.bufferTimeSeconds;
	if (scene.settings.hasRecoveryTime)
		settings["recovery_time_percent"] = scene.settings.recoveryTimePercent;
	if (!settings.empty())
		sceneJson["simulation_settings"] = settings;
	if (!scene.importReport.empty()) {
		sceneJson["import_report"] = json::array();
		for (const auto& row : scene.importReport) {
			sceneJson["import_report"].push_back({
				{"category", row.category},
				{"source_file", row.sourceFile},
				{"source_count", row.sourceCount},
				{"converted_count", row.convertedCount},
				{"skipped_count", row.skippedCount},
				{"unresolved_references", row.unresolvedReferences},
			});
		}
	}

	json infrastructure = {
		{"tracks", json::array()},
		{"nodes", json::array()},
		{"arcs", json::array()},
		{"blocks", json::array()},
		{"connections", json::array()},
	};
	for (const auto& track : scene.tracks)
		infrastructure["tracks"].push_back({{"id", track.id}});
	for (const auto& node : scene.nodes) {
		infrastructure["nodes"].push_back({
			{"id", node.id},
			{"track", node.trackId},
			{"x_km", node.xKm},
			{"y_km", node.yKm},
		});
	}
	for (const auto& arc : scene.arcs) {
		infrastructure["arcs"].push_back({
			{"id", arc.id},
			{"track", arc.trackId},
			{"from", arc.fromNodeId},
			{"to", arc.toNodeId},
			{"curvature_radius_m", arc.curvatureRadiusM},
			{"gradient_percent", arc.gradientPercent},
			{"speed_limit_ms", arc.speedLimitMs},
		});
	}
	for (const auto& block : scene.blocks) {
		infrastructure["blocks"].push_back({
			{"id", block.id},
			{"track", block.trackId},
			{"length_km", block.lengthKm},
		});
	}
	for (const auto& connection : scene.connections) {
		json value = {
			{"id", connection.id},
			{"from", connection.fromNodeId},
			{"to", connection.toNodeId},
		};
		if (connection.hasSpeedLimit)
			value["speed_limit_ms"] = connection.speedLimitMs;
		infrastructure["connections"].push_back(value);
	}

	json stations = json::array();
	for (const auto& station : scene.stations) {
		json value = {{"id", station.id}, {"name", station.name}, {"platforms", json::array()}};
		if (station.hasPosition)
			value["position_km"] = station.positionKm;
		for (const auto& platform : station.platforms) {
			value["platforms"].push_back({{"id", platform.id}, {"nodes", platform.nodeIds}});
		}
		stations.push_back(value);
	}

	json signalling = {
		{"signals", json::array()},
		{"routes", json::array()},
		{"block_dependencies", json::array()},
		{"single_track_restrictions", json::array()},
		{"station_boundaries", json::array()},
	};
	for (const auto& signal : scene.signals)
		signalling["signals"].push_back({{"id", signal.id}});
	for (const auto& route : scene.routes) {
		json value = {{"id", route.id}, {"blocks", route.blocks}};
		if (route.hasCorridor || !route.corridor.empty())
			value["corridor"] = route.corridor;
		if (route.reversed)
			value["reversed"] = true;
		signalling["routes"].push_back(value);
	}
	for (const auto& dependency : scene.blockDependencies) {
		signalling["block_dependencies"].push_back({
			{"block", dependency.block},
			{"depends_on", dependency.dependsOn},
		});
	}
	for (const auto& restriction : scene.singleTrackRestrictions) {
		signalling["single_track_restrictions"].push_back({
			{"start_block", restriction.startBlock},
			{"end_block", restriction.endBlock},
			{"protected_start_block", restriction.protectedStartBlock},
			{"protected_end_block", restriction.protectedEndBlock},
		});
	}
	for (const auto& boundary : scene.stationBoundaries) {
		json value = {{"entrance_block", boundary.entranceBlock}, {"direction", boundary.direction}};
		if (boundary.hasExitBlock || !boundary.exitBlock.empty())
			value["exit_block"] = boundary.exitBlock;
		signalling["station_boundaries"].push_back(value);
	}

	bool wroteAll = true;
	wroteAll = writeJsonFile(result, scenePath, "scene.json", sceneJson) && wroteAll;
	wroteAll = writeJsonFile(result, scenePath, "infrastructure.json", infrastructure) && wroteAll;
	wroteAll = writeJsonFile(result, scenePath, "stations.json", {{"stations", stations}}) && wroteAll;
	wroteAll = writeJsonFile(result, scenePath, "signalling.json", signalling) && wroteAll;
	wroteAll = writeJsonFile(result, scenePath, "rolling_stock.json",
			{{"train_units", writeTrainUnits(scene)}, {"compositions", writeCompositions(scene)}}) && wroteAll;
	wroteAll = writeJsonFile(result, scenePath, "services.json", {{"services", writeServices(scene)}}) && wroteAll;
	wroteAll = writeJsonFile(result, scenePath, "scenarios.json", writeScenarios(scene)) && wroteAll;
	if (!scene.passengers.empty()) {
		wroteAll = writeJsonFile(result, scenePath, "passengers.json", writePassengers(scene)) && wroteAll;
	}

	// Keep the old flat file until every new canonical file was persisted.
	if (wroteAll) {
		ec.clear();
		fs::remove(scenePath / "incidents.json", ec);
		if (ec) {
			addWriteError(result, "incidents.json", "Cannot remove incidents.json: " + ec.message());
			wroteAll = false;
		}
		if (scene.passengers.empty()) {
			ec.clear();
			fs::remove(scenePath / "passengers.json", ec);
			if (ec) {
				addWriteError(result, "passengers.json",
						"Cannot remove passengers.json: " + ec.message());
				wroteAll = false;
			}
		}
	}

	result.wroteAll = wroteAll && !hasErrors(result.diagnostics);
	return result;
}
