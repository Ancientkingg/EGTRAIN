#include "scene/SceneModel.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

bool readFile(const fs::path& path, std::string& content) {
	std::ifstream input(path);
	if (!input)
		return false;
	content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
	return true;
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
	std::vector<std::string> parts;
	if (counts.errors > 0)
		parts.push_back(std::to_string(counts.errors) + (counts.errors == 1 ? " error" : " errors"));
	if (counts.warnings > 0)
		parts.push_back(std::to_string(counts.warnings) + (counts.warnings == 1 ? " warning" : " warnings"));
	if (counts.infos > 0)
		parts.push_back(std::to_string(counts.infos) + (counts.infos == 1 ? " info" : " infos"));
	if (parts.empty())
		return "ok";
	std::string status = parts.front();
	for (std::size_t i = 1; i < parts.size(); ++i)
		status += ", " + parts[i];
	return status;
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

	auto statusForFile = [&](const std::string& sourceFile) {
		return scene.sourceFiles.count(sourceFile) > 0 ? "loaded" : "missing";
	};
	auto add = [&](const std::string& category, const std::string& sourceFile, int parsedCount) {
		const std::string status = statusForFile(sourceFile);
		SceneLoadedData data = makeLoadedData(category, sourceFile, parsedCount, status);
		if (!sourceFile.empty()) {
			data.children.push_back(makeLoadedData("raw_file", sourceFile, status == "loaded" ? 1 : 0, status));
			data.children.push_back(makeLoadedData("parsed_objects", sourceFile, parsedCount, status));
			data.children.push_back(makeLoadedData("derived_simulation", "", 0,
					status == "loaded" ? "not_built" : status));
		}
		scene.loadedData.push_back(data);
	};
	auto addChild = [&](const std::string& category, const std::string& sourceFile, int parsedCount,
			const std::string& status) {
		scene.loadedData.back().children.push_back(makeLoadedData(category, sourceFile, parsedCount, status));
	};

	add("scene", "scene.json", scene.schemaVersion > 0 ? 1 : 0);
	add("infrastructure", "infrastructure.json", static_cast<int>(scene.tracks.size() + scene.nodes.size()
			+ scene.arcs.size() + scene.blocks.size() + scene.connections.size()));
	const std::string infrastructureStatus = scene.loadedData.back().status;
	addChild("tracks", "infrastructure.json", static_cast<int>(scene.tracks.size()), infrastructureStatus);
	addChild("nodes", "infrastructure.json", static_cast<int>(scene.nodes.size()), infrastructureStatus);
	addChild("arcs", "infrastructure.json", static_cast<int>(scene.arcs.size()), infrastructureStatus);
	addChild("blocks", "infrastructure.json", static_cast<int>(scene.blocks.size()), infrastructureStatus);
	addChild("connections", "infrastructure.json", static_cast<int>(scene.connections.size()), infrastructureStatus);

	add("stations", "stations.json", static_cast<int>(scene.stations.size()));
	int platformCount = 0;
	for (const auto& station : scene.stations)
		platformCount += static_cast<int>(station.platforms.size());
	addChild("platforms", "stations.json", platformCount, scene.loadedData.back().status);

	add("timetable", "services.json", static_cast<int>(scene.services.size()));

	add("rolling_stock", "rolling_stock.json", static_cast<int>(scene.trainUnits.size()
			+ scene.compositions.size()));
	const std::string rollingStatus = scene.loadedData.back().status;
	addChild("train_units", "rolling_stock.json", static_cast<int>(scene.trainUnits.size()), rollingStatus);
	addChild("compositions", "rolling_stock.json", static_cast<int>(scene.compositions.size()), rollingStatus);
	SceneLoadedData sourceFiles = makeLoadedData("source_files", "", 0, "missing");
	for (const auto& unit : scene.trainUnits) {
		if (!unit.sourceDataFile.empty()) {
			sourceFiles.children.push_back(makeLoadedData("data_file", unit.sourceDataFile, 1, "loaded"));
			++sourceFiles.parsedCount;
		}
		if (!unit.sourceTractionFile.empty()) {
			sourceFiles.children.push_back(makeLoadedData("traction_file", unit.sourceTractionFile, 1, "loaded"));
			++sourceFiles.parsedCount;
		}
	}
	if (sourceFiles.parsedCount > 0)
		sourceFiles.status = "loaded";
	scene.loadedData.back().children.push_back(sourceFiles);

	int signallingCount = static_cast<int>(scene.signals.size() + scene.routes.size()
			+ scene.blockDependencies.size() + scene.singleTrackRestrictions.size()
			+ scene.stationBoundaries.size());
	add("signalling", "signalling.json", signallingCount);
	const std::string signallingStatus = scene.loadedData.back().status;
	addChild("signals", "signalling.json", static_cast<int>(scene.signals.size()), signallingStatus);
	addChild("routes", "signalling.json", static_cast<int>(scene.routes.size()), signallingStatus);
	addChild("dependencies", "signalling.json", static_cast<int>(scene.blockDependencies.size()), signallingStatus);
	addChild("restrictions", "signalling.json", static_cast<int>(scene.singleTrackRestrictions.size()
			+ scene.stationBoundaries.size()), signallingStatus);

	int incidentCount = 0;
	int entranceDelayCount = 0;
	for (const auto& scenario : scene.scenarios) {
		incidentCount += static_cast<int>(scenario.incidents.size());
		entranceDelayCount += static_cast<int>(scenario.entranceDelays.size());
	}
	add("scenarios", "scenarios.json", static_cast<int>(scene.scenarios.size()));
	const std::string scenariosStatus = scene.loadedData.back().status;
	const std::string incidentSource = scene.sourceFiles.count("scenarios.json") > 0
			? "scenarios.json" : "incidents.json";
	addChild("incidents", incidentSource, incidentCount, scenariosStatus);
	addChild("entrance_delays", "scenarios.json", entranceDelayCount, scenariosStatus);

	add("passengers", "passengers.json", static_cast<int>(scene.passengers.size()));
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
		scene.loadedData[i].children.push_back(makeLoadedData("validation", scene.loadedData[i].sourceFile,
				total, loadedDataDiagnosticStatus(count)));
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

	auto parseObject = [&](const std::string& file, json& value, bool required) {
		std::string content;
		if (!readFile(fs::path(sceneDir) / file, content)) {
			if (required)
				addError("scene.file.missing", file, "Required file is missing");
			return false;
		}
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
		output = object[key].get<int>();
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
	const bool incidentsOk = !scenariosPresent
			&& parseObject("incidents.json", incidentsJson, incidentsPresent);
	const bool passengersOk = parseObject("passengers.json", passengersJson,
			fs::exists(fs::path(sceneDir) / "passengers.json"));
	parseObject("views.json", viewsJson, false);

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

	if (signallingOk) {
		if (arraySection(signallingJson, "signals", "signalling.json", "", true)) {
			for (std::size_t index = 0; index < signallingJson["signals"].size(); ++index) {
				const std::string path = "signals[" + std::to_string(index) + "]";
				SceneSignal signal;
				stringField(signallingJson["signals"][index], "id", "signalling.json", path, signal.id);
				result.scene.signals.push_back(signal);
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
			numberField(value, "end_seconds", file, incidentPath, incident.endSeconds);
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
	return result;
}
