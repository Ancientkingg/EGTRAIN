#include "scene/SceneWriter.h"
#include "scene/SceneFieldReader.h"
#include "scene/SceneFields.h"
#include "scene/StagedDirectory.h"
#include "scene/Utf8Path.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>
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

static void addCleanupWarning(SceneSaveResult& result, const fs::path& path,
	const std::string& message) {
	SceneDiagnostic diagnostic;
	diagnostic.severity = SceneSeverity::Warning;
	diagnostic.code = "scene.save.cleanup";
	diagnostic.file = path.u8string();
	diagnostic.message = message;
	result.diagnostics.push_back(std::move(diagnostic));
}

static bool inspectDestination(const fs::path& destination, bool& exists,
	SceneSaveResult& result) {
	std::error_code ec;
	const auto status = fs::symlink_status(destination, ec);
	if (ec && ec != std::errc::no_such_file_or_directory) {
		addWriteError(result, destination.u8string(), "Cannot inspect scene destination: " + ec.message());
		return false;
	}
	exists = !ec && status.type() != fs::file_type::not_found;
	if (exists && status.type() != fs::file_type::directory) {
		addWriteError(result, destination.u8string(),
			"Scene destination must be a directory and cannot be a symlink");
		return false;
	}
	return true;
}

static bool serializeJson(SceneSaveResult& result, const std::string& filename,
	const json& value, std::string& bytes) {
	try {
		bytes = value.dump(4) + "\n";
	} catch (const json::exception& error) {
		addWriteError(result, filename, "Cannot serialize " + filename + ": " + error.what());
		return false;
	}
	return true;
}

static bool writeBytes(SceneSaveResult& result, const std::string& filename, const fs::path& path,
	const std::string& bytes) {
	std::ofstream output(path, std::ios::binary);
	if (!output) {
		addWriteError(result, filename, "Cannot open " + filename + " for writing");
		return false;
	}
	output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	output.close();
	if (!output) {
		addWriteError(result, filename, "Cannot write " + filename);
		return false;
	}
	return true;
}

// Writes into a scene directory that is private to the save, so truncating is safe.
static bool writeJsonFile(SceneSaveResult& result, const fs::path& scenePath,
	const std::string& filename, const json& value) {
	std::string bytes;
	if (!serializeJson(result, filename, value, bytes))
		return false;
	result.writeAttempted = true;
	return writeBytes(result, filename, scenePath / filename, bytes);
}

// Writes a file that may already exist: the old content stays until the new content is complete.
static bool writeJsonFileReplacing(SceneSaveResult& result, const fs::path& path,
	const json& value) {
	const std::string filename = path.filename().u8string();
	std::string bytes;
	if (!serializeJson(result, filename, value, bytes))
		return false;
	fs::path target = path;
	std::error_code ec;
	if (fs::is_symlink(path, ec)) {
		target = fs::canonical(path, ec);
		if (ec) {
			addWriteError(result, filename, "Cannot resolve " + filename + ": " + ec.message());
			return false;
		}
	}
	// A rename replaces a file that cannot be opened for writing, so that is refused first.
	ec.clear();
	if (fs::is_regular_file(target, ec) && !std::ofstream(target, std::ios::binary | std::ios::app)) {
		addWriteError(result, filename, "Cannot open " + filename + " for writing");
		return false;
	}
	fs::path temporary;
	if (uniqueSiblingPath(target.parent_path(), target.filename().u8string() + ".tmp-", temporary)) {
		addWriteError(result, filename, "Cannot create a temporary file for " + filename);
		return false;
	}
	result.writeAttempted = true;
	bool published = false;
	if (writeBytes(result, filename, temporary, bytes)) {
		ec.clear();
		const fs::file_status existing = fs::status(target, ec);
		if (!ec && fs::is_regular_file(existing))
			fs::permissions(temporary, existing.permissions(), fs::perm_options::replace, ec);
		ec.clear();
		fs::rename(temporary, target, ec);
		if (ec)
			addWriteError(result, filename, "Cannot replace " + filename + ": " + ec.message());
		else
			published = true;
	}
	if (!published) {
		ec.clear();
		fs::remove(temporary, ec);
	}
	return published;
}

static bool copyExistingSceneContents(const fs::path& source, const fs::path& staging,
	SceneSaveResult& result) {
	std::error_code ec;
	fs::copy(source, staging, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
	if (ec) {
		addWriteError(result, source.u8string(), "Cannot stage the previous scene contents: " + ec.message());
		return false;
	}
	for (const char* filename : {"scene.json", "infrastructure.json", "stations.json",
			 "signalling.json", "rolling_stock.json", "services.json", "scenarios.json",
			 "passengers.json", "views.json", "incidents.json"}) {
		ec.clear();
		fs::remove_all(staging / filename, ec);
		if (ec) {
			addWriteError(result, filename, "Cannot prepare staged scene file: " + ec.message());
			return false;
		}
	}
	return true;
}

static json writePhysical(const SceneTrainPhysical& physical) {
	json value = json::object();
	for (const SceneTrainPhysicalField& field : kSceneTrainPhysicalFields)
		value[field.key] = physical.*field.member;
	return value;
}

json encodeSignallingArea(const SceneSignallingArea& area) {
	json value = {
		{"id", area.id},
		{"start_km", area.startKm},
		{"end_km", area.endKm},
		{"level", area.level},
	};
	if (!area.trackId.empty())
		value["track"] = area.trackId;
	return value;
}

json encodeSignalling(const SceneModel& scene) {
	json signalling = {
		{"signals", json::array()},
		{"routes", json::array()},
		{"block_dependencies", json::array()},
		{"single_track_restrictions", json::array()},
		{"station_boundaries", json::array()},
	};
	for (const auto& signal : scene.signals) {
		json value = {{"id", signal.id}};
		if (!signal.protectedSection.empty())
			value["protected_section"] = signal.protectedSection;
		signalling["signals"].push_back(std::move(value));
	}
	if (!scene.signallingAreas.empty()) {
		signalling["signalling_areas"] = json::array();
		for (const auto& area : scene.signallingAreas)
			signalling["signalling_areas"].push_back(encodeSignallingArea(area));
	}
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
	return signalling;
}

json encodeRollingStock(const SceneModel& scene) {
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
	json compositions = json::array();
	for (const auto& composition : scene.compositions) {
		compositions.push_back({{"id", composition.id}, {"units", composition.units}});
	}
	return {{"train_units", trainUnits}, {"compositions", compositions}};
}

static json encodeStops(const SceneService& service) {
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

json encodeServices(const SceneModel& scene) {
	json services = json::array();
	for (const auto& service : scene.services) {
		json value = {
			{"id", service.id},
			{"composition", service.composition},
			{"route", service.route},
			{"stops", encodeStops(service)},
		};
		if (!service.operatingCode.empty())
			value["operating_code"] = service.operatingCode;
		if (!service.category.empty())
			value["category"] = service.category;
		if (!service.visualizationColor.empty())
			value["visualization_color"] = service.visualizationColor;
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
	return {{"services", services}};
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
		const bool hasReducedSpeed = incident.hasReducedSpeed || incident.reducedSpeedKmh != 0.0;
		json incidentValue = {
			{"id", incident.id},
			{"type", incident.type},
			{"target", incident.target},
			{"start_seconds", incident.startSeconds},
		};
		// Keep the historical five-field shape byte-for-byte in behavior while
		// allowing reduced-speed incidents to run until the destination.
		if (!hasReducedSpeed || incident.hasEndSeconds || incident.endSeconds != 0.0)
			incidentValue["end_seconds"] = incident.endSeconds;
		if (incident.hasOccurrence || incident.occurrence != 1)
			incidentValue["occurrence"] = incident.occurrence;
		if (hasReducedSpeed)
			incidentValue["reduced_speed_kmh"] = incident.reducedSpeedKmh;
		if (incident.terminateAtDestination)
			incidentValue["terminate_at_destination"] = true;
		value["incidents"].push_back(std::move(incidentValue));
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

json encodeScenarios(const SceneModel& scene) {
	json scenarios = json::array();
	if (scene.scenarios.empty()) {
		scenarios.push_back(writeScenarioValue(makeBaselineScenario()));
	} else {
		for (const auto& scenario : scene.scenarios)
			scenarios.push_back(writeScenarioValue(scenario));
	}
	std::string defaultId = scene.defaultScenarioId;
	if (defaultId.empty())
		defaultId = scene.scenarios.empty() ? makeBaselineScenario().id : scene.scenarios.front().id;
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

SceneSaveResult saveScenarioJson(const SceneScenario& scenario, const std::string& filePath) {
	SceneSaveResult result;
	result.wroteAll = writeJsonFileReplacing(result, pathFromUtf8(filePath), writeScenarioValue(scenario));
	return result;
}

ScenarioLoadResult loadScenarioJson(const std::string& filePath) {
	ScenarioLoadResult result;
	std::ifstream input{pathFromUtf8(filePath)};
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

	const SceneFieldReader reader("scenario.json", kScenarioFileFieldCodes, result.diagnostics);
	result.scenario = decodeScenarioFile(reader, value);
	return result;
}

json encodePassengers(const std::vector<ScenePassenger>& passengers) {
	json values = json::array();
	for (const auto& passenger : passengers) {
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
		values.push_back(value);
	}
	return {{"passengers", values}};
}

json encodeInfrastructure(const SceneModel& scene) {
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
	return infrastructure;
}

json encodeStations(const SceneModel& scene) {
	json stations = json::array();
	for (const auto& station : scene.stations) {
		json value = {{"id", station.id}, {"name", station.name}, {"platforms", json::array()}};
		if (station.hasPosition)
			value["position_km"] = station.positionKm;
		for (const auto& platform : station.platforms) {
			json platformValue = {{"id", platform.id}, {"nodes", platform.nodeIds}};
			if (platform.hasLength)
				platformValue["length_m"] = platform.lengthM;
			if (platform.hasWidth)
				platformValue["width_m"] = platform.widthM;
			value["platforms"].push_back(std::move(platformValue));
		}
		stations.push_back(value);
	}
	return {{"stations", stations}};
}

json encodeViews(const SceneModel& scene) {
	json tracks = json::array();
	for (const auto& view : scene.trackViews) {
		json value = {{"track", view.trackId}, {"level", view.level}, {"region", view.region}};
		if (!view.visible)
			value["visible"] = false;
		tracks.push_back(std::move(value));
	}

	json stations = json::array();
	for (const auto& view : scene.stationViews) {
		json regions = json::array();
		for (const auto& region : view.regions)
			regions.push_back({{"id", region.first}, {"position_km", region.second}});
		json value = {
			{"station", view.stationId},
			{"latitude", view.latitude},
			{"longitude", view.longitude},
			{"regions", std::move(regions)},
		};
		if (!view.corridors.empty())
			value["corridors"] = view.corridors;
		stations.push_back(std::move(value));
	}
	return {{"tracks", tracks}, {"stations", stations}};
}

json encodeSceneMetadata(const SceneModel& scene) {
	json metadata = {
		{"schema_version", scene.schemaVersion},
		{"name", scene.name},
		{"saved_with_app_version", EGTRAIN_APP_VERSION},
		{"units", {{"distance", "m"}, {"time", "s"}, {"speed", "m/s"}}},
	};
	if (!scene.description.empty())
		metadata["description"] = scene.description;
	if (!scene.baseTime.empty())
		metadata["base_time"] = scene.baseTime;
	json settings = json::object();
	if (scene.settings.hasDuration)
		settings["duration_seconds"] = scene.settings.durationSeconds;
	if (scene.settings.hasBufferTime)
		settings["buffer_time_seconds"] = scene.settings.bufferTimeSeconds;
	if (scene.settings.hasRecoveryTime)
		settings["recovery_time_percent"] = scene.settings.recoveryTimePercent;
	if (!settings.empty())
		metadata["simulation_settings"] = settings;
	if (!scene.importReport.empty()) {
		metadata["import_report"] = json::array();
		for (const auto& row : scene.importReport) {
			metadata["import_report"].push_back({
				{"category", row.category},
				{"source_file", row.sourceFile},
				{"source_count", row.sourceCount},
				{"converted_count", row.convertedCount},
				{"skipped_count", row.skippedCount},
				{"unresolved_references", row.unresolvedReferences},
			});
		}
	}
	return metadata;
}

static SceneSaveResult writeSceneGeneration(const SceneModel& scene, const fs::path& scenePath) {
	SceneSaveResult result;

	const auto writeCanonical = [&](const std::string& filename, const json& value) {
		return writeJsonFile(result, scenePath, filename, value);
	};
	bool wroteAll = true;
	wroteAll = writeCanonical("scene.json", encodeSceneMetadata(scene)) && wroteAll;
	wroteAll = writeCanonical("infrastructure.json", encodeInfrastructure(scene)) && wroteAll;
	wroteAll = writeCanonical("stations.json", encodeStations(scene)) && wroteAll;
	wroteAll = writeCanonical("signalling.json", encodeSignalling(scene)) && wroteAll;
	wroteAll = writeCanonical("rolling_stock.json", encodeRollingStock(scene)) && wroteAll;
	wroteAll = writeCanonical("services.json", encodeServices(scene)) && wroteAll;
	wroteAll = writeCanonical("scenarios.json", encodeScenarios(scene)) && wroteAll;
	const bool hasViews = !scene.trackViews.empty() || !scene.stationViews.empty();
	if (hasViews)
		wroteAll = writeCanonical("views.json", encodeViews(scene)) && wroteAll;
	if (!scene.passengers.empty()) {
		wroteAll = writeCanonical("passengers.json", encodePassengers(scene.passengers)) && wroteAll;
	}

	result.wroteAll = wroteAll && !hasErrors(result.diagnostics);
	return result;
}

SceneSaveResult saveScene(const SceneModel& scene, const std::string& sceneDir) {
	SceneSaveResult result;
	const fs::path destination = pathFromUtf8(sceneDir);
	const std::string destinationName = destination.filename().u8string();
	if (destination.empty() || destinationName.empty() || destinationName == "."
		|| destinationName == "..") {
		addWriteError(result, sceneDir, "Scene destination must be a named directory");
		return result;
	}

	const fs::path parent = destination.parent_path().empty() ? fs::path(".")
															  : destination.parent_path();
	std::error_code ec;
	result.writeAttempted = true;
	fs::create_directories(parent, ec);
	if (ec) {
		addWriteError(result, parent.u8string(), "Cannot create scene parent directory: " + ec.message());
		return result;
	}
	ec.clear();
	const auto parentStatus = fs::symlink_status(parent, ec);
	if (ec || parentStatus.type() != fs::file_type::directory) {
		addWriteError(result, parent.u8string(), "Scene destination parent must be a directory");
		return result;
	}

	bool hadDestination = false;
	if (!inspectDestination(destination, hadDestination, result))
		return result;

	StagedDirectory staging;
	ec = createStagedDirectory(parent, destinationName + ".staging-", true, staging);
	if (ec) {
		addWriteError(result, destination.u8string(), "Cannot create a private scene staging directory");
		return result;
	}
	if (hadDestination && !copyExistingSceneContents(destination, staging.path, result))
		return result;
	const bool stagingAttempted = result.writeAttempted;
	result = writeSceneGeneration(scene, staging.path);
	result.writeAttempted = result.writeAttempted || stagingAttempted;
	if (!result.wroteAll || hasErrors(result.diagnostics))
		return result;

	const SceneLoadResult staged = loadScene(staging.path.u8string());
	result.diagnostics.insert(result.diagnostics.end(), staged.diagnostics.begin(), staged.diagnostics.end());
	if (hasErrors(staged.diagnostics)) {
		result.wroteAll = false;
		return result;
	}
	const std::string stagedSnapshot = staged.inputSnapshot;

	if (!inspectDestination(destination, hadDestination, result)) {
		result.wroteAll = false;
		return result;
	}

	fs::path backup;
	if (hadDestination) {
		ec = uniqueSiblingPath(parent, destinationName + ".backup-", backup);
		if (ec) {
			addWriteError(result, destination.u8string(), "Cannot reserve a private scene backup path");
			result.wroteAll = false;
			return result;
		}
	}

	const StagedPublishResult published = publishStagedDirectory(staging, destination, backup);
	if (published.step == StagedPublishStep::MoveToBackup) {
		addWriteError(result, destination.u8string(), "Cannot move the previous scene generation: " + published.error.message());
		result.wroteAll = false;
		return result;
	}
	if (published.step == StagedPublishStep::Rename) {
		addWriteError(result, destination.u8string(), "Cannot publish scene generation: " + published.error.message());
		if (published.restoreError)
			addWriteError(result, destination.u8string(), "Cannot restore the previous scene generation: " + published.restoreError.message());
		result.wroteAll = false;
		return result;
	}
	if (published.removeBackupError)
		addCleanupWarning(result, backup,
			"Cannot remove the previous scene generation: " + published.removeBackupError.message());

	result.wroteAll = !hasErrors(result.diagnostics);
	if (result.wroteAll)
		result.inputSnapshot = stagedSnapshot;
	return result;
}
