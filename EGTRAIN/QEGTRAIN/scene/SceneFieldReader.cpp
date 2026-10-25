#include "scene/SceneFieldReader.h"

#include "scene/SceneFields.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <unordered_set>
#include <utility>

using json = nlohmann::json;

namespace {

std::string joinPath(const std::string& parent, const std::string& key) {
	return parent.empty() ? key : parent + "." + key;
}

std::string withKey(const char* pattern, const char* key) {
	std::string message = pattern;
	const std::size_t at = message.find('%');
	if (at != std::string::npos)
		message.replace(at, 1, key);
	return message;
}

void rejectResultFields(const SceneFieldReader& reader, const json& object, const std::string& path) {
	if (!object.is_object())
		return;
	for (const auto& item : object.items()) {
		if (isSimulationResultField(item.key()))
			reader.report("scene.passengers.results", "Passenger input must not contain simulation-result fields",
				joinPath(path, item.key()));
	}
}

void decodeWindow(const SceneFieldReader& reader, const json& journey, const char* key, const std::string& journeyPath,
	double& start, double& end) {
	const std::string windowPath = joinPath(journeyPath, key);
	if (!journey.contains(key) || !journey[key].is_object()) {
		reader.report(kSceneFileFieldCodes.fieldMissing, std::string("Missing ") + key + " window", windowPath);
		return;
	}
	reader.numberField(journey[key], "start_seconds", windowPath, start);
	reader.numberField(journey[key], "end_seconds", windowPath, end);
}

ScenePassengerLeg decodePassengerLeg(const SceneFieldReader& reader, const json& value, const std::string& path) {
	ScenePassengerLeg leg;
	rejectResultFields(reader, value, path);
	reader.stringField(value, "id", path, leg.id);
	reader.stringField(value, "origin", path, leg.originStationId);
	reader.stringField(value, "destination", path, leg.destinationStationId);
	reader.stringField(value, "service", path, leg.serviceId);
	reader.integerField(value, "occurrence", path, leg.occurrence, false);
	return leg;
}

ScenePassengerJourney decodePassengerJourney(const SceneFieldReader& reader, const json& value, const std::string& path) {
	ScenePassengerJourney journey;
	rejectResultFields(reader, value, path);
	reader.stringField(value, "id", path, journey.id);
	reader.stringField(value, "activity", path, journey.activity, false);
	reader.stringField(value, "origin", path, journey.originStationId);
	reader.stringField(value, "destination", path, journey.destinationStationId);
	decodeWindow(reader, value, "planned_departure", path, journey.plannedDepartureStartSeconds,
		journey.plannedDepartureEndSeconds);
	decodeWindow(reader, value, "planned_arrival", path, journey.plannedArrivalStartSeconds,
		journey.plannedArrivalEndSeconds);
	if (reader.arraySection(value, "legs", path, true)) {
		for (std::size_t index = 0; index < value["legs"].size(); ++index)
			journey.legs.push_back(
				decodePassengerLeg(reader, value["legs"][index], path + ".legs[" + std::to_string(index) + "]"));
	}
	return journey;
}

// Reads the incidents section of root, which is at path.
void decodeIncidentArray(const SceneFieldReader& reader, const json& root, const std::string& path,
	std::vector<SceneIncident>& output) {
	if (!reader.arraySection(root, "incidents", path, true))
		return;
	for (std::size_t index = 0; index < root["incidents"].size(); ++index) {
		const std::string incidentPath = path.empty()
			? "incidents[" + std::to_string(index) + "]"
			: path + ".incidents[" + std::to_string(index) + "]";
		output.push_back(decodeIncident(reader, root["incidents"][index], incidentPath));
	}
}

} // namespace

void SceneFieldReader::report(const char* code, const std::string& message, const std::string& path,
	SceneSeverity severity) const {
	SceneDiagnostic diagnostic;
	diagnostic.severity = severity;
	diagnostic.code = code;
	diagnostic.file = std::string(file_);
	diagnostic.message = message;
	diagnostic.path = path;
	sink_.push_back(std::move(diagnostic));
}

bool SceneFieldReader::objectItem(const json& item, const std::string& path, const char* message) const {
	if (item.is_object())
		return true;
	report(codes_.itemInvalid, message, path);
	return false;
}

bool SceneFieldReader::arraySection(const json& object, const char* key, const std::string& path, bool required) const {
	if (!objectItem(object, path, codes_.sectionObjectMessage))
		return false;
	if (!object.contains(key)) {
		if (required)
			report(codes_.sectionInvalid, withKey(codes_.sectionMissingMessage, key), joinPath(path, key));
		return false;
	}
	if (!object[key].is_array()) {
		report(codes_.sectionInvalid, withKey(codes_.sectionMistypedMessage, key), joinPath(path, key));
		return false;
	}
	return true;
}

bool SceneFieldReader::stringField(const json& object, const char* key, const std::string& path, std::string& output,
	bool required, Empty empty) const {
	if (!objectItem(object, path, codes_.itemMessage))
		return false;
	if (!object.contains(key)) {
		if (required)
			report(codes_.fieldMissing, std::string("Missing ") + key, joinPath(path, key));
		return false;
	}
	if (!object[key].is_string() || (required && empty == Empty::Invalid && object[key].get<std::string>().empty())) {
		report(codes_.fieldInvalid, std::string("Invalid ") + key, joinPath(path, key));
		return false;
	}
	output = object[key].get<std::string>();
	return true;
}

bool SceneFieldReader::numberField(const json& object, const char* key, const std::string& path, double& output,
	bool required) const {
	if (!objectItem(object, path, codes_.itemMessage))
		return false;
	if (!object.contains(key)) {
		if (required)
			report(codes_.fieldMissing, std::string("Missing ") + key, joinPath(path, key));
		return false;
	}
	if (!object[key].is_number()) {
		report(codes_.fieldInvalid, std::string("Invalid ") + key, joinPath(path, key));
		return false;
	}
	try {
		output = object[key].get<double>();
	} catch (const json::exception&) {
		report(codes_.fieldInvalid, std::string("Invalid ") + key, joinPath(path, key));
		return false;
	}
	return true;
}

bool SceneFieldReader::integerField(const json& object, const char* key, const std::string& path, int& output,
	bool required) const {
	if (!objectItem(object, path, codes_.itemMessage))
		return false;
	if (!object.contains(key)) {
		if (required)
			report(codes_.fieldMissing, std::string("Missing ") + key, joinPath(path, key));
		return false;
	}
	if (!object[key].is_number_integer()) {
		report(codes_.fieldInvalid, std::string("Invalid ") + key, joinPath(path, key));
		return false;
	}
	int value = 0;
	if (!readJsonInt(object[key], INT_MIN, INT_MAX, value)) {
		report(codes_.fieldInvalid, std::string("Invalid ") + key + ": integer out of range", joinPath(path, key));
		return false;
	}
	output = value;
	return true;
}

bool SceneFieldReader::boolField(const json& object, const char* key, const std::string& path, bool& output) const {
	if (!object.contains(key))
		return false;
	if (!object[key].is_boolean()) {
		report(codes_.fieldInvalid, std::string("Invalid ") + key, joinPath(path, key));
		return false;
	}
	output = object[key].get<bool>();
	return true;
}

SceneIncident decodeIncident(const SceneFieldReader& reader, const json& value, const std::string& path) {
	SceneIncident incident;
	reader.stringField(value, "id", path, incident.id);
	reader.stringField(value, "type", path, incident.type);
	reader.stringField(value, "target", path, incident.target);
	reader.numberField(value, "start_seconds", path, incident.startSeconds);
	incident.hasEndSeconds = reader.numberField(value, "end_seconds", path, incident.endSeconds, false);
	incident.hasOccurrence = reader.integerField(value, "occurrence", path, incident.occurrence, false);
	incident.hasReducedSpeed = reader.numberField(value, "reduced_speed_kmh", path, incident.reducedSpeedKmh, false);
	reader.boolField(value, "terminate_at_destination", path, incident.terminateAtDestination);
	return incident;
}

SceneEntranceDelay decodeEntranceDelay(const SceneFieldReader& reader, const json& value, const std::string& path) {
	SceneEntranceDelay delay;
	reader.stringField(value, "service", path, delay.serviceId);
	reader.integerField(value, "occurrence", path, delay.occurrence, false);
	reader.stringField(value, "station", path, delay.stationId);
	reader.numberField(value, "delay_seconds", path, delay.delaySeconds);
	return delay;
}

SceneScenario decodeScenarioFile(const SceneFieldReader& reader, const json& root) {
	SceneScenario scenario;
	const auto allowed = [](const std::string& name) {
		return name == "id" || name == "name" || name == "description" || name == "incidents" || name == "entrance_delays";
	};
	for (const auto& field : root.items()) {
		if (!allowed(field.key()))
			reader.report("scene.scenario.field.unknown", "Scenario JSON does not allow field " + field.key(), field.key());
	}
	reader.stringField(root, "id", "scenario", scenario.id);
	reader.stringField(root, "name", "scenario", scenario.name, true, SceneFieldReader::Empty::Allowed);
	reader.stringField(root, "description", "scenario", scenario.description, false);
	if (reader.arraySection(root, "incidents", "", true)) {
		for (std::size_t index = 0; index < root["incidents"].size(); ++index) {
			const json& item = root["incidents"][index];
			const std::string path = "incidents[" + std::to_string(index) + "]";
			if (reader.objectItem(item, path, "Incident must be an object"))
				scenario.incidents.push_back(decodeIncident(reader, item, path));
		}
	}
	if (reader.arraySection(root, "entrance_delays", "", false)) {
		for (std::size_t index = 0; index < root["entrance_delays"].size(); ++index) {
			const json& item = root["entrance_delays"][index];
			const std::string path = "entrance_delays[" + std::to_string(index) + "]";
			if (reader.objectItem(item, path, "Entrance delay must be an object"))
				scenario.entranceDelays.push_back(decodeEntranceDelay(reader, item, path));
		}
	}
	return scenario;
}

SceneSignallingArea decodeSignallingArea(const SceneFieldReader& reader, const json& value, const std::string& path) {
	SceneSignallingArea area;
	reader.stringField(value, "id", path, area.id);
	reader.numberField(value, "start_km", path, area.startKm);
	reader.numberField(value, "end_km", path, area.endKm);
	reader.integerField(value, "level", path, area.level);
	reader.stringField(value, "track", path, area.trackId, false);
	return area;
}

bool isSimulationResultField(const std::string& name) {
	return name == "results" || name == "simulation_results" || name.find("simulated_") == 0
		|| name.find("actual_") == 0 || name == "delay_seconds" || name.find("arrival_delay") != std::string::npos
		|| name.find("departure_delay") != std::string::npos;
}

void decodeSceneMetadata(const SceneFieldReader& reader, const json& root, SceneModel& scene) {
	if (!root.contains("schema_version")) {
		reader.report("scene.version.missing", "Missing schema_version", "schema_version");
	} else if (!readJsonInt(root["schema_version"], kCurrentSceneSchemaVersion,
				   kCurrentSceneSchemaVersion, scene.schemaVersion)) {
		reader.report("scene.version.unsupported",
			"Unsupported schema_version, must be the current integer "
				+ std::to_string(kCurrentSceneSchemaVersion),
			"schema_version");
	}
	reader.stringField(root, "name", "", scene.name, true, SceneFieldReader::Empty::Allowed);
	reader.stringField(root, "saved_with_app_version", "",
		scene.savedWithAppVersion, false);
	reader.stringField(root, "description", "", scene.description, false);
	reader.stringField(root, "base_time", "", scene.baseTime, false);
	if (root.contains("units")) {
		const json& units = root["units"];
		if (!units.is_object()) {
			reader.report("scene.field.missing", "units must be an object", "units");
		} else {
			for (const auto& unit : {std::pair<const char*, const char*>("distance", "m"),
					 std::pair<const char*, const char*>("time", "s"),
					 std::pair<const char*, const char*>("speed", "m/s")}) {
				if (units.contains(unit.first) && (!units[unit.first].is_string() || units[unit.first].get<std::string>() != unit.second)) {
					reader.report("scene.units.unsupported",
						std::string("Unsupported ") + unit.first + " unit",
						std::string("units.") + unit.first);
				}
			}
		}
	}
	if (root.contains("simulation_settings")) {
		const json& settings = root["simulation_settings"];
		if (!settings.is_object()) {
			reader.report("scene.field.missing",
				"simulation_settings must be an object", "simulation_settings");
		} else {
			scene.settings.hasDuration = reader.numberField(settings, "duration_seconds",
				"simulation_settings", scene.settings.durationSeconds, false);
			scene.settings.hasBufferTime = reader.numberField(settings, "buffer_time_seconds",
				"simulation_settings", scene.settings.bufferTimeSeconds, false);
			scene.settings.hasRecoveryTime = reader.numberField(settings, "recovery_time_percent",
				"simulation_settings", scene.settings.recoveryTimePercent, false);
		}
	}
	if (root.contains("import_report")) {
		if (reader.arraySection(root, "import_report", "", false)) {
			for (std::size_t index = 0; index < root["import_report"].size(); ++index) {
				const json& value = root["import_report"][index];
				const std::string path = "import_report[" + std::to_string(index) + "]";
				SceneImportReportRow row;
				reader.stringField(value, "category", path, row.category);
				reader.stringField(value, "source_file", path, row.sourceFile, false);
				reader.integerField(value, "source_count", path, row.sourceCount);
				reader.integerField(value, "converted_count", path, row.convertedCount);
				reader.integerField(value, "skipped_count", path, row.skippedCount);
				reader.integerField(value, "unresolved_references", path,
					row.unresolvedReferences);
				scene.importReport.push_back(row);
			}
		}
	}
}

void decodeInfrastructure(const SceneFieldReader& reader, const json& root, SceneModel& scene) {
	if (reader.arraySection(root, "tracks", "", false)) {
		for (std::size_t index = 0; index < root["tracks"].size(); ++index) {
			const std::string path = "tracks[" + std::to_string(index) + "]";
			SceneTrack track;
			reader.stringField(root["tracks"][index], "id", path, track.id);
			scene.tracks.push_back(track);
		}
	}
	if (reader.arraySection(root, "nodes", "", true)) {
		for (std::size_t index = 0; index < root["nodes"].size(); ++index) {
			const std::string path = "nodes[" + std::to_string(index) + "]";
			SceneNode node;
			reader.stringField(root["nodes"][index], "id", path, node.id);
			reader.stringField(root["nodes"][index], "track", path,
				node.trackId);
			reader.numberField(root["nodes"][index], "x_km", path,
				node.xKm);
			reader.numberField(root["nodes"][index], "y_km", path,
				node.yKm);
			scene.nodes.push_back(node);
		}
	}
	if (reader.arraySection(root, "arcs", "", true)) {
		for (std::size_t index = 0; index < root["arcs"].size(); ++index) {
			const std::string path = "arcs[" + std::to_string(index) + "]";
			SceneArc arc;
			reader.stringField(root["arcs"][index], "id", path, arc.id);
			reader.stringField(root["arcs"][index], "track", path,
				arc.trackId);
			reader.stringField(root["arcs"][index], "from", path,
				arc.fromNodeId);
			reader.stringField(root["arcs"][index], "to", path,
				arc.toNodeId);
			reader.numberField(root["arcs"][index], "curvature_radius_m",
				path, arc.curvatureRadiusM);
			reader.numberField(root["arcs"][index], "gradient_percent",
				path, arc.gradientPercent);
			reader.numberField(root["arcs"][index], "speed_limit_ms",
				path, arc.speedLimitMs);
			scene.arcs.push_back(arc);
		}
	}
	if (reader.arraySection(root, "blocks", "", false)) {
		for (std::size_t index = 0; index < root["blocks"].size(); ++index) {
			const std::string path = "blocks[" + std::to_string(index) + "]";
			SceneBlock block;
			reader.stringField(root["blocks"][index], "id", path,
				block.id);
			reader.stringField(root["blocks"][index], "track", path,
				block.trackId);
			reader.numberField(root["blocks"][index], "length_km", path,
				block.lengthKm);
			scene.blocks.push_back(block);
		}
	}
	if (reader.arraySection(root, "connections", "", false)) {
		for (std::size_t index = 0; index < root["connections"].size(); ++index) {
			const std::string path = "connections[" + std::to_string(index) + "]";
			SceneConnection connection;
			reader.stringField(root["connections"][index], "id", path,
				connection.id);
			reader.stringField(root["connections"][index], "from", path,
				connection.fromNodeId);
			reader.stringField(root["connections"][index], "to", path,
				connection.toNodeId);
			connection.hasSpeedLimit = reader.numberField(root["connections"][index],
				"speed_limit_ms", path, connection.speedLimitMs, false);
			scene.connections.push_back(connection);
		}
	}
}

void decodeStations(const SceneFieldReader& reader, const json& root, SceneModel& scene) {
	if (!reader.arraySection(root, "stations", "", true))
		return;
	for (std::size_t index = 0; index < root["stations"].size(); ++index) {
		const std::string stationPath = "stations[" + std::to_string(index) + "]";
		const json& value = root["stations"][index];
		SceneStation station;
		reader.stringField(value, "id", stationPath, station.id);
		reader.stringField(value, "name", stationPath, station.name, true, SceneFieldReader::Empty::Allowed);
		station.hasPosition = reader.numberField(value, "position_km", stationPath,
			station.positionKm, false);
		if (value.contains("platforms")) {
			if (!value["platforms"].is_array()) {
				reader.report("scene.field.missing", "platforms must be an array",
					stationPath + ".platforms");
			} else {
				for (std::size_t platformIndex = 0; platformIndex < value["platforms"].size();
					++platformIndex) {
					const std::string platformPath = stationPath + ".platforms["
						+ std::to_string(platformIndex) + "]";
					const json& platformValue = value["platforms"][platformIndex];
					ScenePlatform platform;
					reader.stringField(platformValue, "id", platformPath, platform.id);
					platform.hasLength = reader.numberField(platformValue, "length_m",
						platformPath, platform.lengthM, false);
					platform.hasWidth = reader.numberField(platformValue, "width_m",
						platformPath, platform.widthM, false);
					if (platformValue.contains("nodes")) {
						if (!platformValue["nodes"].is_array()) {
							reader.report("scene.field.missing",
								"nodes must be an array", platformPath + ".nodes");
						} else {
							for (std::size_t nodeIndex = 0; nodeIndex < platformValue["nodes"].size();
								++nodeIndex) {
								if (!platformValue["nodes"][nodeIndex].is_string()) {
									reader.report("scene.field.missing",
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
		scene.stations.push_back(station);
	}
}

void decodeViews(const SceneFieldReader& reader, const json& root, SceneModel& scene) {
	const auto viewWarning = [&reader](const std::string& path, const std::string& message) {
		reader.report("scene.views.row", message, path, SceneSeverity::Warning);
	};
	std::unordered_set<std::string> trackIds;
	for (const auto& track : scene.tracks)
		trackIds.insert(track.id);
	std::unordered_set<std::string> stationIds;
	for (const auto& station : scene.stations)
		stationIds.insert(station.id);

	if (root.contains("tracks")) {
		if (!root["tracks"].is_array()) {
			viewWarning("tracks", "Track display rows are not a list, so they are skipped");
		} else {
			for (std::size_t index = 0; index < root["tracks"].size(); ++index) {
				const std::string path = "tracks[" + std::to_string(index) + "]";
				const json& value = root["tracks"][index];
				if (!value.is_object() || !value.contains("track") || !value["track"].is_string()
					|| value["track"].get<std::string>().empty()
					|| !value.contains("level") || !value["level"].is_number_integer()
					|| !value.contains("region") || !value["region"].is_number_integer()) {
					viewWarning(path, "Track display row is invalid and is skipped");
					continue;
				}
				SceneTrackView view;
				view.trackId = value["track"].get<std::string>();
				if (!readJsonInt(value["level"], INT_MIN, INT_MAX, view.level)
					|| !readJsonInt(value["region"], INT_MIN, INT_MAX, view.region)) {
					viewWarning(path, "Track display row is invalid and is skipped");
					continue;
				}
				try {
					if (value.contains("visible"))
						view.visible = value["visible"].get<bool>();
					if (value.contains("double_switch"))
						view.doubleSwitch = value["double_switch"].get<bool>();
				} catch (const json::exception&) {
					viewWarning(path, "Track display row is invalid and is skipped");
					continue;
				}
				if (view.region < 0) {
					viewWarning(path, "Track display row has a negative region and is skipped");
					continue;
				}
				if (trackIds.count(view.trackId) == 0) {
					viewWarning(path, "Track display row names a track that does not exist and is skipped");
					continue;
				}
				const auto duplicate = std::find_if(scene.trackViews.begin(),
					scene.trackViews.end(), [&view](const SceneTrackView& candidate) {
						return candidate.trackId == view.trackId;
					});
				if (duplicate != scene.trackViews.end()) {
					viewWarning(path, "Track display row is a duplicate and is skipped");
					continue;
				}
				scene.trackViews.push_back(std::move(view));
			}
		}
	}

	if (root.contains("stations")) {
		if (!root["stations"].is_array()) {
			viewWarning("stations", "Station display rows are not a list, so they are skipped");
		} else {
			for (std::size_t index = 0; index < root["stations"].size(); ++index) {
				const std::string path = "stations[" + std::to_string(index) + "]";
				const json& value = root["stations"][index];
				if (!value.is_object() || !value.contains("station") || !value["station"].is_string()
					|| value["station"].get<std::string>().empty()
					|| !value.contains("latitude") || !value["latitude"].is_number()
					|| !value.contains("longitude") || !value["longitude"].is_number()
					|| !value.contains("regions") || !value["regions"].is_array()) {
					viewWarning(path, "Station display row is invalid and is skipped");
					continue;
				}
				SceneStationView view;
				view.stationId = value["station"].get<std::string>();
				view.latitude = value["latitude"].get<double>();
				view.longitude = value["longitude"].get<double>();
				if (!std::isfinite(view.latitude) || !std::isfinite(view.longitude)
					|| stationIds.count(view.stationId) == 0) {
					viewWarning(path,
						stationIds.count(view.stationId) == 0
							? "Station display row names a station that does not exist and is skipped"
							: "Station display row has an invalid position and is skipped");
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
					int regionId = 0;
					if (!readJsonInt(regionValue["id"], INT_MIN, INT_MAX, regionId)) {
						validRegions = false;
						break;
					}
					try {
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
					viewWarning(path, "Station display row has an invalid region and is skipped");
					continue;
				}
				if (value.contains("corridors")) {
					if (!value["corridors"].is_array()) {
						viewWarning(path, "Station display row has invalid corridors and is skipped");
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
						viewWarning(path, "Station display row has an invalid corridor and is skipped");
						continue;
					}
				}
				const auto duplicate = std::find_if(scene.stationViews.begin(),
					scene.stationViews.end(), [&view](const SceneStationView& candidate) {
						return candidate.stationId == view.stationId;
					});
				if (duplicate != scene.stationViews.end()) {
					viewWarning(path, "Station display row is a duplicate and is skipped");
					continue;
				}
				scene.stationViews.push_back(std::move(view));
			}
		}
	}
}

void decodeSignalling(const SceneFieldReader& reader, const json& root, SceneModel& scene) {
	if (reader.arraySection(root, "signals", "", true)) {
		for (std::size_t index = 0; index < root["signals"].size(); ++index) {
			const std::string path = "signals[" + std::to_string(index) + "]";
			SceneSignal signal;
			reader.stringField(root["signals"][index], "id", path, signal.id);
			reader.stringField(root["signals"][index], "protected_section", path,
				signal.protectedSection, false);
			scene.signals.push_back(signal);
		}
	}
	if (reader.arraySection(root, "signalling_areas", "", false)) {
		for (std::size_t index = 0; index < root["signalling_areas"].size(); ++index) {
			const std::string path = "signalling_areas[" + std::to_string(index) + "]";
			scene.signallingAreas.push_back(
				decodeSignallingArea(reader, root["signalling_areas"][index], path));
		}
	}
	if (reader.arraySection(root, "routes", "", true)) {
		for (std::size_t index = 0; index < root["routes"].size(); ++index) {
			const std::string path = "routes[" + std::to_string(index) + "]";
			const json& value = root["routes"][index];
			SceneRoute route;
			reader.stringField(value, "id", path, route.id);
			if (reader.arraySection(value, "blocks", path, true)) {
				for (std::size_t blockIndex = 0; blockIndex < value["blocks"].size(); ++blockIndex) {
					if (!value["blocks"][blockIndex].is_string()) {
						reader.report("scene.field.missing",
							"Route block reference must be a string",
							path + ".blocks[" + std::to_string(blockIndex) + "]");
						continue;
					}
					route.blocks.push_back(value["blocks"][blockIndex].get<std::string>());
				}
			}
			route.hasCorridor = reader.stringField(value, "corridor", path,
				route.corridor, false);
			if (value.contains("reversed")) {
				if (!value["reversed"].is_boolean())
					reader.report("scene.field.missing", "reversed must be a boolean",
						path + ".reversed");
				else
					route.reversed = value["reversed"].get<bool>();
			}
			scene.routes.push_back(route);
		}
	}
	if (reader.arraySection(root, "block_dependencies", "", false)) {
		for (std::size_t index = 0; index < root["block_dependencies"].size(); ++index) {
			const std::string path = "block_dependencies[" + std::to_string(index) + "]";
			SceneBlockDependency dependency;
			reader.stringField(root["block_dependencies"][index], "block", path,
				dependency.block);
			reader.stringField(root["block_dependencies"][index], "depends_on",
				path, dependency.dependsOn);
			scene.blockDependencies.push_back(dependency);
		}
	}
	if (reader.arraySection(root, "single_track_restrictions", "", false)) {
		for (std::size_t index = 0; index < root["single_track_restrictions"].size(); ++index) {
			const std::string path = "single_track_restrictions[" + std::to_string(index) + "]";
			const json& value = root["single_track_restrictions"][index];
			SceneSingleTrackRestriction restriction;
			reader.stringField(value, "start_block", path, restriction.startBlock);
			reader.stringField(value, "end_block", path, restriction.endBlock);
			reader.stringField(value, "protected_start_block", path,
				restriction.protectedStartBlock);
			reader.stringField(value, "protected_end_block", path,
				restriction.protectedEndBlock);
			scene.singleTrackRestrictions.push_back(restriction);
		}
	}
	if (reader.arraySection(root, "station_boundaries", "", false)) {
		for (std::size_t index = 0; index < root["station_boundaries"].size(); ++index) {
			const std::string path = "station_boundaries[" + std::to_string(index) + "]";
			const json& value = root["station_boundaries"][index];
			SceneStationBoundary boundary;
			reader.stringField(value, "entrance_block", path, boundary.entranceBlock);
			boundary.hasExitBlock = reader.stringField(value, "exit_block", path,
				boundary.exitBlock, false);
			if (value.contains("direction")) {
				if (!value["direction"].is_boolean())
					reader.report("scene.field.missing", "direction must be a boolean",
						path + ".direction");
				else
					boundary.direction = value["direction"].get<bool>();
			}
			scene.stationBoundaries.push_back(boundary);
		}
	}
}

void decodeRollingStock(const SceneFieldReader& reader, const json& root, SceneModel& scene) {
	if (reader.arraySection(root, "train_units", "", true)) {
		for (std::size_t index = 0; index < root["train_units"].size(); ++index) {
			const std::string path = "train_units[" + std::to_string(index) + "]";
			const json& value = root["train_units"][index];
			SceneTrainUnit unit;
			reader.stringField(value, "id", path, unit.id);
			if (value.contains("physical")) {
				if (!value["physical"].is_object()) {
					reader.report("scene.field.missing", "physical must be an object",
						path + ".physical");
				} else {
					const json& physical = value["physical"];
					unit.hasPhysical = true;
					for (const SceneTrainPhysicalField& field : kSceneTrainPhysicalFields)
						reader.numberField(physical, field.key, path + ".physical",
							unit.physical.*field.member);
				}
			}
			if (value.contains("traction_curve")) {
				if (!value["traction_curve"].is_array()) {
					reader.report("scene.field.missing",
						"traction_curve must be an array", path + ".traction_curve");
				} else {
					for (std::size_t rowIndex = 0; rowIndex < value["traction_curve"].size(); ++rowIndex) {
						const json& row = value["traction_curve"][rowIndex];
						const std::string rowPath = path + ".traction_curve["
							+ std::to_string(rowIndex) + "]";
						if (!row.is_array() || row.size() != 5) {
							reader.report("scene.field.missing",
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
							reader.report("scene.field.missing",
								"traction_curve row must contain numbers", rowPath);
						} else {
							unit.tractionCurve.push_back(values);
						}
					}
				}
			}
			if (value.contains("source")) {
				if (!value["source"].is_object()) {
					reader.report("scene.field.missing", "source must be an object",
						path + ".source");
				} else {
					const json& source = value["source"];
					reader.stringField(source, "data_file", path + ".source",
						unit.sourceDataFile, false);
					reader.stringField(source, "traction_file", path + ".source",
						unit.sourceTractionFile, false);
				}
			}
			scene.trainUnits.push_back(unit);
		}
	}
	if (reader.arraySection(root, "compositions", "", true)) {
		for (std::size_t index = 0; index < root["compositions"].size(); ++index) {
			const std::string path = "compositions[" + std::to_string(index) + "]";
			const json& value = root["compositions"][index];
			SceneComposition composition;
			reader.stringField(value, "id", path, composition.id);
			if (!reader.arraySection(value, "units", path, true)) {
				scene.compositions.push_back(composition);
				continue;
			}
			for (std::size_t unitIndex = 0; unitIndex < value["units"].size(); ++unitIndex) {
				if (!value["units"][unitIndex].is_string()) {
					reader.report("scene.field.missing",
						"Composition unit reference must be a string",
						path + ".units[" + std::to_string(unitIndex) + "]");
					continue;
				}
				composition.units.push_back(value["units"][unitIndex].get<std::string>());
			}
			scene.compositions.push_back(composition);
		}
	}
}

void decodeServices(const SceneFieldReader& reader, const json& root, SceneModel& scene) {
	if (!reader.arraySection(root, "services", "", true))
		return;
	for (std::size_t index = 0; index < root["services"].size(); ++index) {
		const std::string path = "services[" + std::to_string(index) + "]";
		const json& value = root["services"][index];
		SceneService service;
		reader.stringField(value, "id", path, service.id);
		reader.stringField(value, "operating_code", path, service.operatingCode, false);
		reader.stringField(value, "category", path, service.category, false);
		reader.stringField(value, "visualization_color", path, service.visualizationColor, false);
		reader.stringField(value, "composition", path, service.composition);
		reader.stringField(value, "route", path, service.route);
		reader.numberField(value, "performance_percent", path,
			service.performancePercent, false);
		service.hasMaximumSpeed = reader.numberField(value, "maximum_speed_kmh", path,
			service.maximumSpeedKmh, false);
		if (value.contains("through")) {
			if (!value["through"].is_boolean())
				reader.report("scene.field.missing", "through must be a boolean",
					path + ".through");
			else
				service.through = value["through"].get<bool>();
		}
		service.hasEntryTime = reader.numberField(value, "entry_time_seconds", path,
			service.entryTimeSeconds, false);
		if (value.contains("repeat")) {
			if (!value["repeat"].is_object()) {
				reader.report("scene.field.missing", "repeat must be an object",
					path + ".repeat");
			} else {
				service.hasRepeat = true;
				reader.numberField(value["repeat"], "headway_seconds", path + ".repeat",
					service.headwaySeconds);
				service.hasRepeatCount = reader.integerField(value["repeat"], "count",
					path + ".repeat", service.repeatCount, false);
				service.hasOperatingCodeStep = reader.integerField(value["repeat"], "operating_code_step",
					path + ".repeat", service.operatingCodeStep, false);
			}
		}
		if (reader.arraySection(value, "stops", path, true)) {
			for (std::size_t stopIndex = 0; stopIndex < value["stops"].size(); ++stopIndex) {
				const std::string stopPath = path + ".stops[" + std::to_string(stopIndex) + "]";
				const json& stopValue = value["stops"][stopIndex];
				SceneStop stop;
				if (stopValue.is_object()) {
					for (const auto& field : stopValue.items()) {
						if (isSimulationResultField(field.key()))
							reader.report("scene.timetable.results",
								"Service stop input must not contain simulation-result fields",
								stopPath + "." + field.key());
					}
				}
				reader.stringField(stopValue, "station", stopPath, stop.stationId);
				reader.stringField(stopValue, "platform", stopPath, stop.platformId, false);
				stop.hasPlannedArrival = reader.numberField(stopValue, "planned_arrival_seconds",
					stopPath, stop.plannedArrivalSeconds, false);
				if (!stop.hasPlannedArrival)
					stop.hasPlannedArrival = reader.numberField(stopValue, "arrival_seconds",
						stopPath, stop.plannedArrivalSeconds, false);
				stop.hasPlannedDeparture = reader.numberField(stopValue, "planned_departure_seconds",
					stopPath, stop.plannedDepartureSeconds, false);
				if (!stop.hasPlannedDeparture)
					stop.hasPlannedDeparture = reader.numberField(stopValue, "departure_seconds",
						stopPath, stop.plannedDepartureSeconds, false);
				reader.numberField(stopValue, "dwell_seconds", stopPath, stop.dwellSeconds);
				service.stops.push_back(stop);
			}
		}
		scene.services.push_back(service);
	}
}

void decodeScenarios(const SceneFieldReader& reader, const json& root, SceneModel& scene) {
	reader.stringField(root, "default_scenario_id", "", scene.defaultScenarioId);
	if (reader.arraySection(root, "scenarios", "", true)) {
		for (std::size_t index = 0; index < root["scenarios"].size(); ++index) {
			const std::string path = "scenarios[" + std::to_string(index) + "]";
			const json& value = root["scenarios"][index];
			SceneScenario scenario;
			reader.stringField(value, "id", path, scenario.id);
			reader.stringField(value, "name", path, scenario.name, true, SceneFieldReader::Empty::Allowed);
			reader.stringField(value, "description", path, scenario.description, false);
			decodeIncidentArray(reader, value, path, scenario.incidents);
			if (reader.arraySection(value, "entrance_delays", path, false)) {
				for (std::size_t delayIndex = 0; delayIndex < value["entrance_delays"].size();
					++delayIndex) {
					const std::string delayPath = path + ".entrance_delays["
						+ std::to_string(delayIndex) + "]";
					scenario.entranceDelays.push_back(
						decodeEntranceDelay(reader, value["entrance_delays"][delayIndex], delayPath));
				}
			}
			scene.scenarios.push_back(scenario);
		}
	}
}

void decodeHistoricalIncidents(const SceneFieldReader& reader, const json& root, SceneModel& scene) {
	decodeIncidentArray(reader, root, "", defaultScenarioIncidents(scene));
}

void decodePassengers(const SceneFieldReader& reader, const json& root, SceneModel& scene) {
	if (!reader.arraySection(root, "passengers", "", true))
		return;
	for (std::size_t index = 0; index < root["passengers"].size(); ++index) {
		const std::string path = "passengers[" + std::to_string(index) + "]";
		const json& value = root["passengers"][index];
		ScenePassenger passenger;
		reader.stringField(value, "id", path, passenger.id);
		rejectResultFields(reader, value, path);
		if (reader.arraySection(value, "journeys", path, true)) {
			for (std::size_t journeyIndex = 0; journeyIndex < value["journeys"].size(); ++journeyIndex)
				passenger.journeys.push_back(decodePassengerJourney(reader, value["journeys"][journeyIndex],
					path + ".journeys[" + std::to_string(journeyIndex) + "]"));
		}
		scene.passengers.push_back(std::move(passenger));
	}
}
