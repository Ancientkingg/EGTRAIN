#include "SceneModel.h"
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

static bool readFile(const std::string& path, std::string& content) {
	std::ifstream f(path);
	if (!f.good())
		return false;
	content.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	return true;
}

static std::string joinPath(const std::string& parent, const std::string& key) {
	return parent.empty() ? key : parent + "." + key;
}

SceneLoadResult loadScene(const std::string& sceneDir) {
	SceneLoadResult result;
	auto addError = [&](const std::string& code, const std::string& file, const std::string& msg, const std::string& path = "") {
		SceneDiagnostic d;
		d.severity = SceneSeverity::Error;
		d.code = code;
		d.file = file;
		d.message = msg;
		d.path = path;
		result.diagnostics.push_back(d);
	};

	if (!fs::exists(sceneDir) || !fs::is_directory(sceneDir)) {
		addError("scene.dir.missing", "", "Scene directory missing or invalid");
		return result;
	}

	auto parseJsonFile = [&](const std::string& filename, json& outJson) -> bool {
		std::string path = (fs::path(sceneDir) / filename).string();
		std::string content;
		if (!readFile(path, content)) {
			addError("scene.file.missing", filename, "Required file is missing");
			return false;
		}
		try {
			outJson = json::parse(content);
		} catch (const json::parse_error& e) {
			addError("scene.json.parse", filename, std::string("JSON parse error: ") + e.what());
			return false;
		}
		if (!outJson.is_object()) {
			addError("scene.section.missing", filename, "Root element must be an object");
			return false;
		}
		return true;
	};

	// A required top-level key that must hold an array.
	auto requireArraySection = [&](const json& root, const char* key, const std::string& file) -> bool {
		if (!root.contains(key) || !root[key].is_array()) {
			addError("scene.section.missing", file, std::string("Missing or mistyped ") + key + " section");
			return false;
		}
		return true;
	};
	// A required field that must hold a non-empty string. Reports and returns
	// false on failure; loading continues so one pass surfaces every problem.
	auto requireString = [&](const json& obj, const char* key, const std::string& file, const std::string& path, std::string& out) -> bool {
		if (!obj.contains(key) || !obj[key].is_string() || obj[key].get<std::string>().empty()) {
			addError("scene.field.missing", file, std::string("Missing or invalid ") + key, joinPath(path, key));
			return false;
		}
		out = obj[key].get<std::string>();
		return true;
	};
	auto requireNumber = [&](const json& obj, const char* key, const std::string& file, const std::string& path, double& out) -> bool {
		if (!obj.contains(key) || !obj[key].is_number()) {
			addError("scene.field.missing", file, std::string("Missing or invalid ") + key, joinPath(path, key));
			return false;
		}
		out = obj[key].get<double>();
		return true;
	};

	json sceneJson, infraJson, stationsJson, sigJson, rollingJson, servicesJson;
	bool sceneOk = parseJsonFile("scene.json", sceneJson);
	bool infraOk = parseJsonFile("infrastructure.json", infraJson);
	bool stationsOk = parseJsonFile("stations.json", stationsJson);
	bool sigOk = parseJsonFile("signalling.json", sigJson);
	bool rollingOk = parseJsonFile("rolling_stock.json", rollingJson);
	bool servicesOk = parseJsonFile("services.json", servicesJson);

	// Optional files
	json incidentsJson, viewsJson;
	bool incidentsOk = false;
	if (fs::exists(fs::path(sceneDir) / "incidents.json")) {
		incidentsOk = parseJsonFile("incidents.json", incidentsJson);
	}
	if (fs::exists(fs::path(sceneDir) / "views.json")) {
		parseJsonFile("views.json", viewsJson); // just parse-check
	}

	if (sceneOk) {
		if (!sceneJson.contains("schema_version")) {
			addError("scene.version.missing", "scene.json", "Missing schema_version");
		} else if (!sceneJson["schema_version"].is_number_integer() || sceneJson["schema_version"].get<int>() != 1) {
			addError("scene.version.unsupported", "scene.json", "Unsupported schema_version, must be the integer 1");
		} else {
			result.scene.schemaVersion = sceneJson["schema_version"].get<int>();
		}
		requireString(sceneJson, "name", "scene.json", "", result.scene.name);
		if (sceneJson.contains("base_time")) {
			if (sceneJson["base_time"].is_string()) {
				result.scene.baseTime = sceneJson["base_time"].get<std::string>();
			} else {
				addError("scene.field.missing", "scene.json", "base_time must be a string", "base_time");
			}
		}
		if (sceneJson.contains("units")) {
			if (!sceneJson["units"].is_object()) {
				addError("scene.field.missing", "scene.json", "units must be an object", "units");
			} else {
				const auto& units = sceneJson["units"];
				if (units.contains("distance") && units["distance"] != "m") {
					addError("scene.units.unsupported", "scene.json", "Distance unit must be m", "units.distance");
				}
				if (units.contains("time") && units["time"] != "s") {
					addError("scene.units.unsupported", "scene.json", "Time unit must be s", "units.time");
				}
				if (units.contains("speed") && units["speed"] != "m/s") {
					addError("scene.units.unsupported", "scene.json", "Speed unit must be m/s", "units.speed");
				}
			}
		}
	}

	if (infraOk) {
		requireArraySection(infraJson, "nodes", "infrastructure.json");
		requireArraySection(infraJson, "arcs", "infrastructure.json");
	}

	if (stationsOk && requireArraySection(stationsJson, "stations", "stations.json")) {
		size_t idx = 0;
		for (const auto& st : stationsJson["stations"]) {
			std::string path = "stations[" + std::to_string(idx++) + "]";
			SceneStation s;
			requireString(st, "id", "stations.json", path, s.id);
			requireString(st, "name", "stations.json", path, s.name);
			if (st.contains("platforms") && st["platforms"].is_array()) {
				size_t pidx = 0;
				for (const auto& pf : st["platforms"]) {
					ScenePlatform p;
					requireString(pf, "id", "stations.json", path + ".platforms[" + std::to_string(pidx++) + "]", p.id);
					s.platforms.push_back(p);
				}
			}
			result.scene.stations.push_back(s);
		}
	}

	if (sigOk) {
		if (requireArraySection(sigJson, "signals", "signalling.json")) {
			size_t idx = 0;
			for (const auto& sg : sigJson["signals"]) {
				SceneSignal s;
				requireString(sg, "id", "signalling.json", "signals[" + std::to_string(idx++) + "]", s.id);
				result.scene.signals.push_back(s);
			}
		}
		if (requireArraySection(sigJson, "routes", "signalling.json")) {
			size_t idx = 0;
			for (const auto& rt : sigJson["routes"]) {
				std::string path = "routes[" + std::to_string(idx++) + "]";
				SceneRoute r;
				requireString(rt, "id", "signalling.json", path, r.id);
				if (!rt.contains("blocks") || !rt["blocks"].is_array()) {
					addError("scene.field.missing", "signalling.json", "Missing or invalid blocks", path + ".blocks");
				} else {
					for (const auto& blk : rt["blocks"]) {
						if (blk.is_string())
							r.blocks.push_back(blk.get<std::string>());
					}
				}
				result.scene.routes.push_back(r);
			}
		}
	}

	if (rollingOk) {
		if (requireArraySection(rollingJson, "train_units", "rolling_stock.json")) {
			size_t idx = 0;
			for (const auto& tu : rollingJson["train_units"]) {
				std::string tuPath = "train_units[" + std::to_string(idx++) + "]";
				SceneTrainUnit t;
				requireString(tu, "id", "rolling_stock.json", tuPath, t.id);
				if (tu.contains("physical")) {
					if (!tu["physical"].is_object()) {
						addError("scene.field.missing", "rolling_stock.json", "physical must be an object", tuPath + ".physical");
					} else {
						t.hasPhysical = true;
						auto& ph = tu["physical"];
						requireNumber(ph, "mass_of_traction_unit_kg", "rolling_stock.json", tuPath + ".physical", t.physical.mass_of_traction_unit_kg);
						requireNumber(ph, "mass_of_a_wagon_kg", "rolling_stock.json", tuPath + ".physical", t.physical.mass_of_a_wagon_kg);
						requireNumber(ph, "number_of_wagons", "rolling_stock.json", tuPath + ".physical", t.physical.number_of_wagons);
						requireNumber(ph, "max_speed_ms", "rolling_stock.json", tuPath + ".physical", t.physical.max_speed_ms);
						requireNumber(ph, "max_deceleration_ms2", "rolling_stock.json", tuPath + ".physical", t.physical.max_deceleration_ms2);
						requireNumber(ph, "frontal_area_m2", "rolling_stock.json", tuPath + ".physical", t.physical.frontal_area_m2);
						requireNumber(ph, "resistance_coefficient", "rolling_stock.json", tuPath + ".physical", t.physical.resistance_coefficient);
						requireNumber(ph, "jerk_ms3", "rolling_stock.json", tuPath + ".physical", t.physical.jerk_ms3);
						requireNumber(ph, "length_m", "rolling_stock.json", tuPath + ".physical", t.physical.length_m);
					}
				}
				if (tu.contains("traction_curve")) {
					if (!tu["traction_curve"].is_array()) {
						addError("scene.field.missing", "rolling_stock.json", "traction_curve must be an array", tuPath + ".traction_curve");
					} else {
						size_t tcIdx = 0;
						for (const auto& row : tu["traction_curve"]) {
							if (!row.is_array() || row.size() != 5) {
								addError("scene.field.missing", "rolling_stock.json", "traction_curve row must be an array of 5 numbers", tuPath + ".traction_curve[" + std::to_string(tcIdx) + "]");
							} else {
								bool ok = true;
								std::array<double, 5> arr;
								for (int i = 0; i < 5; ++i) {
									if (!row[i].is_number()) {
										ok = false;
										break;
									}
									arr[i] = row[i].get<double>();
								}
								if (!ok) {
									addError("scene.field.missing", "rolling_stock.json", "traction_curve row must contain numbers", tuPath + ".traction_curve[" + std::to_string(tcIdx) + "]");
								} else {
									t.tractionCurve.push_back(arr);
								}
							}
							tcIdx++;
						}
					}
				}
				if (tu.contains("source")) {
					if (!tu["source"].is_object()) {
						addError("scene.field.missing", "rolling_stock.json", "source must be an object", tuPath + ".source");
					} else {
						auto& src = tu["source"];
						requireString(src, "data_file", "rolling_stock.json", tuPath + ".source", t.sourceDataFile);
						requireString(src, "traction_file", "rolling_stock.json", tuPath + ".source", t.sourceTractionFile);
					}
				}
				result.scene.trainUnits.push_back(t);
			}
		}
		if (requireArraySection(rollingJson, "compositions", "rolling_stock.json")) {
			size_t idx = 0;
			for (const auto& cp : rollingJson["compositions"]) {
				std::string path = "compositions[" + std::to_string(idx++) + "]";
				SceneComposition c;
				requireString(cp, "id", "rolling_stock.json", path, c.id);
				if (!cp.contains("units") || !cp["units"].is_array()) {
					addError("scene.field.missing", "rolling_stock.json", "Missing or invalid units", path + ".units");
				} else {
					for (const auto& u : cp["units"]) {
						if (u.is_string())
							c.units.push_back(u.get<std::string>());
					}
				}
				result.scene.compositions.push_back(c);
			}
		}
	}

	if (servicesOk && requireArraySection(servicesJson, "services", "services.json")) {
		size_t idx = 0;
		for (const auto& sv : servicesJson["services"]) {
			std::string path = "services[" + std::to_string(idx++) + "]";
			SceneService s;
			requireString(sv, "id", "services.json", path, s.id);
			requireString(sv, "composition", "services.json", path, s.composition);
			requireString(sv, "route", "services.json", path, s.route);
			if (sv.contains("entry_time_seconds")) {
				if (sv["entry_time_seconds"].is_number()) {
					s.hasEntryTime = true;
					s.entryTimeSeconds = sv["entry_time_seconds"].get<double>();
				} else {
					addError("scene.field.missing", "services.json", "entry_time_seconds must be a number", path + ".entry_time_seconds");
				}
			}
			if (sv.contains("repeat")) {
				if (!sv["repeat"].is_object()) {
					addError("scene.field.missing", "services.json", "repeat must be an object", path + ".repeat");
				} else {
					s.hasRepeat = true;
					requireNumber(sv["repeat"], "headway_seconds", "services.json", path + ".repeat", s.headwaySeconds);
				}
			}
			if (!sv.contains("stops") || !sv["stops"].is_array()) {
				addError("scene.field.missing", "services.json", "Missing or invalid stops", path + ".stops");
			} else {
				size_t sidx = 0;
				for (const auto& stp : sv["stops"]) {
					std::string stopPath = path + ".stops[" + std::to_string(sidx) + "]";
					SceneStop t;
					requireString(stp, "station", "services.json", stopPath, t.stationId);
					if (stp.contains("platform")) {
						if (stp["platform"].is_string()) {
							t.platformId = stp["platform"].get<std::string>();
						} else {
							addError("scene.field.missing", "services.json", "platform must be a string", stopPath + ".platform");
						}
					}
					// arrival_seconds is optional on any stop: an absent arrival
					// means the stop has no scheduled arrival time.
					if (stp.contains("arrival_seconds")) {
						if (stp["arrival_seconds"].is_number()) {
							t.hasArrival = true;
							t.arrivalSeconds = stp["arrival_seconds"].get<double>();
						} else {
							addError("scene.field.missing", "services.json", "arrival_seconds must be a number", stopPath + ".arrival_seconds");
						}
					}
					if (stp.contains("departure_seconds")) {
						if (stp["departure_seconds"].is_number()) {
							t.hasDeparture = true;
							t.departureSeconds = stp["departure_seconds"].get<double>();
						} else {
							addError("scene.field.missing", "services.json", "departure_seconds must be a number", stopPath + ".departure_seconds");
						}
					} else if (sidx < sv["stops"].size() - 1) {
						addError("scene.field.missing", "services.json", "Missing departure_seconds (only the last stop may omit it)", stopPath + ".departure_seconds");
					}
					requireNumber(stp, "dwell_seconds", "services.json", stopPath, t.dwellSeconds);
					s.stops.push_back(t);
					++sidx;
				}
			}
			result.scene.services.push_back(s);
		}
	}

	if (incidentsOk && requireArraySection(incidentsJson, "incidents", "incidents.json")) {
		size_t idx = 0;
		for (const auto& inc : incidentsJson["incidents"]) {
			std::string path = "incidents[" + std::to_string(idx++) + "]";
			SceneIncident i;
			requireString(inc, "id", "incidents.json", path, i.id);
			requireString(inc, "type", "incidents.json", path, i.type);
			requireString(inc, "target", "incidents.json", path, i.target);
			requireNumber(inc, "start_seconds", "incidents.json", path, i.startSeconds);
			requireNumber(inc, "end_seconds", "incidents.json", path, i.endSeconds);
			result.scene.incidents.push_back(i);
		}
	}

	return result;
}
