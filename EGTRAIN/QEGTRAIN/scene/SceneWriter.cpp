#include "scene/SceneWriter.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

bool SceneSaveResult::success() const {
	return wroteAll && !hasErrors(diagnostics);
}

static void addWriteError(SceneSaveResult& result, const std::string& file, const std::string& message) {
	SceneDiagnostic d;
	d.severity = SceneSeverity::Error;
	d.code = "scene.save.write";
	d.file = file;
	d.message = message;
	result.diagnostics.push_back(d);
}

static bool writeJsonFile(SceneSaveResult& result, const fs::path& scenePath, const std::string& filename, const json& value) {
	fs::path path = scenePath / filename;
	std::ofstream out(path);
	if (!out.good()) {
		addWriteError(result, filename, "Cannot open " + filename + " for writing");
		return false;
	}

	out << value.dump(4) << "\n";
	if (!out.good()) {
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

static json writeTractionCurve(const SceneTrainUnit& unit) {
	json curve = json::array();
	for (const auto& row : unit.tractionCurve) {
		curve.push_back({row[0], row[1], row[2], row[3], row[4]});
	}
	return curve;
}

static json writeTrainUnits(const SceneModel& scene) {
	json trainUnits = json::array();
	for (const auto& unit : scene.trainUnits) {
		json unitJson = {
			{"id", unit.id}};
		if (unit.hasPhysical) {
			unitJson["physical"] = writePhysical(unit.physical);
		}
		if (!unit.tractionCurve.empty()) {
			unitJson["traction_curve"] = writeTractionCurve(unit);
		}

		if (!unit.sourceDataFile.empty() || !unit.sourceTractionFile.empty()) {
			json source = json::object();
			if (!unit.sourceDataFile.empty())
				source["data_file"] = unit.sourceDataFile;
			if (!unit.sourceTractionFile.empty())
				source["traction_file"] = unit.sourceTractionFile;
			unitJson["source"] = source;
		}

		trainUnits.push_back(unitJson);
	}
	return trainUnits;
}

static json writeCompositions(const SceneModel& scene) {
	json compositions = json::array();
	for (const auto& composition : scene.compositions) {
		json units = json::array();
		for (const auto& unit : composition.units) {
			units.push_back(unit);
		}
		compositions.push_back({
			{"id", composition.id},
			{"units", units},
		});
	}
	return compositions;
}

static json writeStops(const SceneService& service) {
	json stops = json::array();
	for (const auto& stop : service.stops) {
		json stopJson = {
			{"station", stop.stationId},
			{"dwell_seconds", stop.dwellSeconds},
		};
		if (!stop.platformId.empty())
			stopJson["platform"] = stop.platformId;
		if (stop.hasArrival)
			stopJson["arrival_seconds"] = stop.arrivalSeconds;
		if (stop.hasDeparture)
			stopJson["departure_seconds"] = stop.departureSeconds;
		stops.push_back(stopJson);
	}
	return stops;
}

static json writeServices(const SceneModel& scene) {
	json services = json::array();
	for (const auto& service : scene.services) {
		json serviceJson = {
			{"id", service.id},
			{"composition", service.composition},
			{"route", service.route},
		};
		if (service.hasEntryTime)
			serviceJson["entry_time_seconds"] = service.entryTimeSeconds;
		if (service.hasRepeat)
			serviceJson["repeat"] = {{"headway_seconds", service.headwaySeconds}};
		serviceJson["stops"] = writeStops(service);
		services.push_back(serviceJson);
	}
	return services;
}

SceneSaveResult saveScene(const SceneModel& scene, const std::string& sceneDir) {
	SceneSaveResult result;
	fs::path scenePath(sceneDir);
	std::error_code ec;
	fs::create_directories(scenePath, ec);
	if (ec) {
		addWriteError(result, "", "Cannot create scene directory: " + ec.message());
		return result;
	}

	json sceneJson = {
		{"schema_version", scene.schemaVersion},
		{"name", scene.name},
	};
	if (!scene.description.empty())
		sceneJson["description"] = scene.description;
	if (!scene.baseTime.empty())
		sceneJson["base_time"] = scene.baseTime;
	sceneJson["units"] = {{"distance", "m"}, {"time", "s"}, {"speed", "m/s"}};

	json stations = json::array();
	for (const auto& station : scene.stations) {
		json platforms = json::array();
		for (const auto& platform : station.platforms) {
			platforms.push_back({{"id", platform.id}});
		}
		stations.push_back({
			{"id", station.id},
			{"name", station.name},
			{"platforms", platforms},
		});
	}

	json signals = json::array();
	for (const auto& signal : scene.signals) {
		signals.push_back({{"id", signal.id}});
	}

	json routes = json::array();
	for (const auto& route : scene.routes) {
		json blocks = json::array();
		for (const auto& block : route.blocks) {
			blocks.push_back(block);
		}
		routes.push_back({
			{"id", route.id},
			{"blocks", blocks},
		});
	}

	bool wroteAll = writeJsonFile(result, scenePath, "scene.json", sceneJson) &&
					writeJsonFile(result, scenePath, "infrastructure.json", {{"nodes", json::array()}, {"arcs", json::array()}}) &&
					writeJsonFile(result, scenePath, "stations.json", {{"stations", stations}}) &&
					writeJsonFile(result, scenePath, "signalling.json", {{"signals", signals}, {"routes", routes}}) &&
					writeJsonFile(result, scenePath, "rolling_stock.json", {{"train_units", writeTrainUnits(scene)}, {"compositions", writeCompositions(scene)}}) &&
					writeJsonFile(result, scenePath, "services.json", {{"services", writeServices(scene)}});

	fs::path incidentsPath = scenePath / "incidents.json";
	if (wroteAll) {
		if (scene.incidents.empty()) {
			ec.clear();
			fs::remove(incidentsPath, ec);
			if (ec) {
				addWriteError(result, "incidents.json", "Cannot remove incidents.json: " + ec.message());
				wroteAll = false;
			}
		} else {
			json incidents = json::array();
			for (const auto& incident : scene.incidents) {
				incidents.push_back({
					{"id", incident.id},
					{"type", incident.type},
					{"target", incident.target},
					{"start_seconds", incident.startSeconds},
					{"end_seconds", incident.endSeconds},
				});
			}
			wroteAll = writeJsonFile(result, scenePath, "incidents.json", {{"incidents", incidents}});
		}
	}

	result.wroteAll = wroteAll && !hasErrors(result.diagnostics);
	return result;
}
