#include "scene/SceneValidator.h"

#include <algorithm>
#include <cmath>
#include <regex>
#include <unordered_map>
#include <unordered_set>

namespace {

struct DiagnosticBuilder {
	std::vector<SceneDiagnostic>& diagnostics;

	void add(SceneSeverity severity, const std::string& code, const std::string& message,
			const std::string& file, const std::string& itemType = "",
			const std::string& itemId = "", const std::string& path = "",
			const std::string& relatedId = "", const std::string& suggestedFix = "") {
		SceneDiagnostic diagnostic;
		diagnostic.severity = severity;
		diagnostic.code = code;
		diagnostic.message = message;
		diagnostic.file = file;
		diagnostic.itemType = itemType;
		diagnostic.itemId = itemId;
		diagnostic.path = path;
		diagnostic.relatedId = relatedId;
		diagnostic.suggestedFix = suggestedFix;
		diagnostics.push_back(diagnostic);
	}

	void error(const std::string& code, const std::string& message, const std::string& file,
			const std::string& itemType = "", const std::string& itemId = "",
			const std::string& path = "", const std::string& relatedId = "",
			const std::string& suggestedFix = "") {
		add(SceneSeverity::Error, code, message, file, itemType, itemId, path, relatedId, suggestedFix);
	}

	void warning(const std::string& code, const std::string& message, const std::string& file,
			const std::string& itemType = "", const std::string& itemId = "",
			const std::string& path = "", const std::string& relatedId = "",
			const std::string& suggestedFix = "") {
		add(SceneSeverity::Warning, code, message, file, itemType, itemId, path, relatedId, suggestedFix);
	}
};

std::string basicBlockId(const std::string& token) {
	if (!token.empty() && token.front() == '@') {
		const std::size_t end = token.find('@', 1);
		if (end != std::string::npos)
			return token.substr(1, end - 1);
	}
	const std::size_t position = token.find('@');
	return position == std::string::npos ? token : token.substr(0, position);
}

std::vector<std::string> routeComponents(const std::string& token) {
	std::vector<std::string> components;
	std::size_t begin = 0;
	while (begin <= token.size()) {
		const std::size_t slash = token.find('/', begin);
		const std::string part = token.substr(begin,
				slash == std::string::npos ? std::string::npos : slash - begin);
		if (!part.empty())
			components.push_back(basicBlockId(part));
		if (slash == std::string::npos)
			break;
		begin = slash + 1;
	}
	return components;
}

bool hasId(const std::unordered_set<std::string>& ids, const std::string& id) {
	return !id.empty() && ids.find(id) != ids.end();
}

template <typename T>
void collectIds(const std::vector<T>& items, const std::string& file, const std::string& type,
		const std::string& category, DiagnosticBuilder& diagnostics,
		std::unordered_set<std::string>& ids) {
	for (std::size_t index = 0; index < items.size(); ++index) {
		const std::string& id = items[index].id;
		const std::string path = category + "[" + std::to_string(index) + "].id";
		if (!ids.insert(id).second) {
			diagnostics.error("scene.id.duplicate", "Duplicate " + type + " id", file, type, id,
					path, id, "Give each " + type + " a unique id");
		}
	}
}

std::vector<SceneDiagnostic> validateCore(const SceneModel& scene, bool runnable) {
	std::vector<SceneDiagnostic> result;
	DiagnosticBuilder diagnostics{result};

	if (!scene.baseTime.empty()) {
		static const std::regex timePattern("^([01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]$");
		if (!std::regex_match(scene.baseTime, timePattern)) {
			diagnostics.error("scene.basetime.invalid", "Invalid base_time format, must be HH:MM:SS",
					"scene.json", "scene", "", "base_time", "",
					"Write the base time as HH:MM:SS, for example 08:00:00");
		}
	}
	if (scene.settings.hasDuration && scene.settings.durationSeconds <= 0.0) {
		diagnostics.error("scene.duration.invalid", "Simulation duration must be positive", "scene.json",
				"scene", "", "simulation_settings.duration_seconds", "",
				"Use a duration greater than 0 seconds");
	}
	if (scene.settings.hasBufferTime && scene.settings.bufferTimeSeconds < 0.0) {
		diagnostics.error("scene.buffer.invalid", "Simulation buffer time cannot be negative", "scene.json",
				"scene", "", "simulation_settings.buffer_time_seconds", "",
				"Use a buffer time of 0 or more seconds");
	}
	if (scene.settings.hasRecoveryTime && scene.settings.recoveryTimePercent < 0.0) {
		diagnostics.error("scene.recovery.invalid", "Recovery time cannot be negative", "scene.json",
				"scene", "", "simulation_settings.recovery_time_percent", "",
				"Use a recovery value of 0 or more");
	}
	if (scene.trainUnits.empty() || scene.compositions.empty())
		diagnostics.error("scene.trains.none", "No trains defined", "rolling_stock.json", "", "", "", "",
				"Add at least one train unit and one composition to rolling_stock.json");
	if (scene.services.empty())
		diagnostics.error("scene.services.none", "No services defined", "services.json", "", "", "", "",
				"Add at least one service to services.json");

	std::unordered_set<std::string> trackIds;
	collectIds(scene.tracks, "infrastructure.json", "track", "tracks", diagnostics, trackIds);
	std::unordered_set<std::string> nodeIds;
	collectIds(scene.nodes, "infrastructure.json", "node", "nodes", diagnostics, nodeIds);
	std::unordered_set<std::string> arcIds;
	collectIds(scene.arcs, "infrastructure.json", "arc", "arcs", diagnostics, arcIds);
	std::unordered_set<std::string> blockIds;
	collectIds(scene.blocks, "infrastructure.json", "block", "blocks", diagnostics, blockIds);
	std::unordered_set<std::string> connectionIds;
	collectIds(scene.connections, "infrastructure.json", "connection", "connections", diagnostics,
			connectionIds);
	for (std::size_t index = 0; index < scene.nodes.size(); ++index) {
		const SceneNode& node = scene.nodes[index];
		if (!hasId(trackIds, node.trackId)) {
			diagnostics.error("scene.ref.unresolved", "Node refers to unknown track", "infrastructure.json",
					"node", node.id, "nodes[" + std::to_string(index) + "].track", node.trackId,
					"Add track " + node.trackId + " or reference an existing track");
		}
	}
	for (std::size_t index = 0; index < scene.arcs.size(); ++index) {
		const SceneArc& arc = scene.arcs[index];
		const std::string path = "arcs[" + std::to_string(index) + "]";
		if (!hasId(trackIds, arc.trackId))
			diagnostics.error("scene.ref.unresolved", "Arc refers to unknown track", "infrastructure.json",
					"arc", arc.id, path + ".track", arc.trackId);
		if (!hasId(nodeIds, arc.fromNodeId))
			diagnostics.error("scene.ref.unresolved", "Arc refers to unknown start node", "infrastructure.json",
					"arc", arc.id, path + ".from", arc.fromNodeId);
		if (!hasId(nodeIds, arc.toNodeId))
			diagnostics.error("scene.ref.unresolved", "Arc refers to unknown end node", "infrastructure.json",
					"arc", arc.id, path + ".to", arc.toNodeId);
	}
	for (std::size_t index = 0; index < scene.blocks.size(); ++index) {
		if (!hasId(trackIds, scene.blocks[index].trackId))
			diagnostics.error("scene.ref.unresolved", "Block refers to unknown track", "infrastructure.json",
					"block", scene.blocks[index].id, "blocks[" + std::to_string(index) + "].track",
					scene.blocks[index].trackId);
	}
	for (std::size_t index = 0; index < scene.connections.size(); ++index) {
		const SceneConnection& connection = scene.connections[index];
		const std::string path = "connections[" + std::to_string(index) + "]";
		if (!hasId(nodeIds, connection.fromNodeId))
			diagnostics.error("scene.ref.unresolved", "Connection refers to unknown start node",
					"infrastructure.json", "connection", connection.id, path + ".from",
					connection.fromNodeId);
		if (!hasId(nodeIds, connection.toNodeId))
			diagnostics.error("scene.ref.unresolved", "Connection refers to unknown end node",
					"infrastructure.json", "connection", connection.id, path + ".to",
					connection.toNodeId);
	}

	std::unordered_set<std::string> stationIds;
	std::unordered_map<std::string, const SceneStation*> stations;
	for (std::size_t index = 0; index < scene.stations.size(); ++index) {
		const SceneStation& station = scene.stations[index];
		const std::string path = "stations[" + std::to_string(index) + "]";
		if (!stationIds.insert(station.id).second) {
			diagnostics.error("scene.id.duplicate", "Duplicate station id", "stations.json", "station",
					station.id, path + ".id", station.id);
		}
		stations[station.id] = &station;
		std::unordered_set<std::string> platformIds;
		for (std::size_t platformIndex = 0; platformIndex < station.platforms.size(); ++platformIndex) {
			const ScenePlatform& platform = station.platforms[platformIndex];
			const std::string platformPath = path + ".platforms[" + std::to_string(platformIndex) + "]";
			if (!platformIds.insert(platform.id).second)
				diagnostics.error("scene.id.duplicate", "Duplicate platform id on station", "stations.json",
						"platform", platform.id, platformPath + ".id", station.id);
			for (std::size_t nodeIndex = 0; nodeIndex < platform.nodeIds.size(); ++nodeIndex) {
				if (!hasId(nodeIds, platform.nodeIds[nodeIndex])) {
					diagnostics.error("scene.ref.unresolved", "Platform refers to unknown node", "stations.json",
							"platform", platform.id, platformPath + ".nodes[" + std::to_string(nodeIndex) + "]",
							platform.nodeIds[nodeIndex]);
				}
			}
		}
	}

	std::unordered_set<std::string> signalIds;
	collectIds(scene.signals, "signalling.json", "signal", "signals", diagnostics, signalIds);
	std::unordered_set<std::string> routeIds;
	collectIds(scene.routes, "signalling.json", "route", "routes", diagnostics, routeIds);
	std::unordered_set<std::string> routeBlockIds;
	for (std::size_t index = 0; index < scene.routes.size(); ++index) {
		const SceneRoute& route = scene.routes[index];
		const std::string path = "routes[" + std::to_string(index) + "]";
		if (route.blocks.empty()) {
			diagnostics.error("scene.route.empty", "Route has no blocks", "signalling.json", "route", route.id,
					path + ".blocks", "", "List the block ids the route runs through");
		}
		for (const auto& token : route.blocks) {
			for (const auto& component : routeComponents(token))
				routeBlockIds.insert(component);
		}
		if (!blockIds.empty()) {
			for (std::size_t blockIndex = 0; blockIndex < route.blocks.size(); ++blockIndex) {
				const std::string blockPath = path + ".blocks[" + std::to_string(blockIndex) + "]";
				for (const auto& component : routeComponents(route.blocks[blockIndex])) {
					if (!hasId(blockIds, component)) {
						diagnostics.error("scene.ref.unresolved", "Route refers to unknown block",
								"signalling.json", "route", route.id, blockPath, component,
								"Add block " + component + " or fix the route token");
					}
				}
			}
		}
	}
	auto blockReferenceKnown = [&](const std::string& reference) {
		const std::vector<std::string> components = routeComponents(reference);
		if (components.empty())
			return false;
		for (const auto& component : components) {
			const bool found = blockIds.empty() ? hasId(routeBlockIds, component)
					: hasId(blockIds, component);
			if (!found)
				return false;
		}
		return true;
	};
	for (std::size_t index = 0; index < scene.blockDependencies.size(); ++index) {
		const SceneBlockDependency& dependency = scene.blockDependencies[index];
		const std::string path = "block_dependencies[" + std::to_string(index) + "]";
		if (!blockReferenceKnown(dependency.block))
			diagnostics.error("scene.ref.unresolved", "Block dependency refers to unknown block",
					"signalling.json", "block_dependency", dependency.block, path + ".block",
					dependency.block);
		if (!blockReferenceKnown(dependency.dependsOn))
			diagnostics.error("scene.ref.unresolved", "Block dependency refers to unknown dependency",
					"signalling.json", "block_dependency", dependency.block, path + ".depends_on",
					dependency.dependsOn);
	}
	for (std::size_t index = 0; index < scene.singleTrackRestrictions.size(); ++index) {
		const SceneSingleTrackRestriction& restriction = scene.singleTrackRestrictions[index];
		const std::string path = "single_track_restrictions[" + std::to_string(index) + "]";
		const std::array<std::pair<const char*, const std::string*>, 4> roles = {{
			{"start_block", &restriction.startBlock},
			{"end_block", &restriction.endBlock},
			{"protected_start_block", &restriction.protectedStartBlock},
			{"protected_end_block", &restriction.protectedEndBlock},
		}};
		for (const auto& role : roles) {
			if (!blockReferenceKnown(*role.second))
				diagnostics.error("scene.ref.unresolved", "Single-track restriction refers to unknown block",
						"signalling.json", "single_track_restriction", *role.second,
						path + "." + role.first, *role.second);
		}
	}
	for (std::size_t index = 0; index < scene.stationBoundaries.size(); ++index) {
		const SceneStationBoundary& boundary = scene.stationBoundaries[index];
		const std::string path = "station_boundaries[" + std::to_string(index) + "]";
		if (!blockReferenceKnown(boundary.entranceBlock))
			diagnostics.error("scene.ref.unresolved", "Station boundary refers to unknown entrance block",
					"signalling.json", "station_boundary", boundary.entranceBlock,
					path + ".entrance_block", boundary.entranceBlock);
		if (boundary.hasExitBlock && !blockReferenceKnown(boundary.exitBlock))
			diagnostics.error("scene.ref.unresolved", "Station boundary refers to unknown exit block",
					"signalling.json", "station_boundary", boundary.exitBlock,
					path + ".exit_block", boundary.exitBlock);
	}

	std::unordered_set<std::string> trainUnitIds;
	collectIds(scene.trainUnits, "rolling_stock.json", "train unit", "train_units", diagnostics,
			trainUnitIds);
	for (std::size_t index = 0; index < scene.trainUnits.size(); ++index) {
		const SceneTrainUnit& unit = scene.trainUnits[index];
		const std::string path = "train_units[" + std::to_string(index) + "]";
		if (unit.tractionCurve.empty()) {
			diagnostics.error("scene.train.traction.empty", "Train unit has no traction data",
					"rolling_stock.json", "train_unit", unit.id, path + ".traction_curve", "",
					"Add at least one traction curve row");
		}
		if (runnable && !unit.hasPhysical) {
			diagnostics.error("scene.train.physical.missing", "Train unit has no physical parameters",
					"rolling_stock.json", "train_unit", unit.id, path + ".physical", "",
					"Add the train-unit physical parameters");
		}
		for (std::size_t rowIndex = 0; rowIndex < unit.tractionCurve.size(); ++rowIndex) {
			const auto& row = unit.tractionCurve[rowIndex];
			const std::string rowPath = path + ".traction_curve[" + std::to_string(rowIndex) + "]";
			if (!(row[0] < row[1])) {
				diagnostics.error("scene.train.traction.interval",
						"Traction curve lower speed must be below upper speed", "rolling_stock.json",
						"train_unit", unit.id, rowPath, "",
						"Set the lower speed below the upper speed");
			}
			if (rowIndex > 0) {
				const auto& previous = unit.tractionCurve[rowIndex - 1];
				if (row[0] < previous[0]) {
					diagnostics.error("scene.train.traction.order",
							"Traction curve rows are not in ascending speed order", "rolling_stock.json",
							"train_unit", unit.id, rowPath, "", "Order rows by increasing lower speed");
				} else if (row[0] < previous[1]) {
					diagnostics.error("scene.train.traction.overlap", "Traction curve intervals overlap",
							"rolling_stock.json", "train_unit", unit.id, rowPath, "",
							"Adjust adjacent bounds so intervals do not overlap");
				}
			}
		}
	}

	std::unordered_set<std::string> compositionIds;
	collectIds(scene.compositions, "rolling_stock.json", "composition", "compositions", diagnostics,
			compositionIds);
	for (std::size_t index = 0; index < scene.compositions.size(); ++index) {
		const SceneComposition& composition = scene.compositions[index];
		const std::string path = "compositions[" + std::to_string(index) + "]";
		if (composition.units.empty()) {
			diagnostics.error("scene.composition.empty", "Composition has no units", "rolling_stock.json",
					"composition", composition.id, path + ".units", "",
					"List at least one train unit id");
		}
		for (std::size_t unitIndex = 0; unitIndex < composition.units.size(); ++unitIndex) {
			if (!hasId(trainUnitIds, composition.units[unitIndex]))
				diagnostics.error("scene.ref.unresolved", "Composition refers to unknown train unit",
						"rolling_stock.json", "composition", composition.id,
						path + ".units[" + std::to_string(unitIndex) + "]", composition.units[unitIndex]);
		}
	}

	std::unordered_set<std::string> serviceIds;
	collectIds(scene.services, "services.json", "service", "services", diagnostics, serviceIds);
	for (std::size_t index = 0; index < scene.services.size(); ++index) {
		const SceneService& service = scene.services[index];
		const std::string path = "services[" + service.id + "]";
		if (!hasId(compositionIds, service.composition))
			diagnostics.error("scene.ref.unresolved", "Service refers to unknown composition", "services.json",
					"service", service.id, path + ".composition", service.composition);
		if (!hasId(routeIds, service.route))
			diagnostics.error("scene.ref.unresolved", "Service refers to unknown route", "services.json",
					"service", service.id, path + ".route", service.route);
		if (service.hasRepeat && service.headwaySeconds <= 0.0)
			diagnostics.error("scene.repeat.invalid", "Non-positive headway", "services.json", "service",
					service.id, path + ".repeat.headway_seconds", "",
					"Use a headway greater than 0 seconds");
		if (service.stops.empty()) {
			if (!service.through)
				diagnostics.warning("scene.service.no_stops", "Service has no stops", "services.json",
						"service", service.id, path + ".stops", "",
						"Add at least one stop or mark the service through");
		} else if (service.through) {
			diagnostics.warning("scene.service.through_stops", "Through service has stops", "services.json",
					"service", service.id, path + ".through");
		}
		bool hasPreviousDeparture = false;
		double previousDeparture = 0.0;
		for (std::size_t stopIndex = 0; stopIndex < service.stops.size(); ++stopIndex) {
			const SceneStop& stop = service.stops[stopIndex];
			const std::string stopPath = path + ".stops[" + std::to_string(stopIndex) + "]";
			auto station = stations.find(stop.stationId);
			if (station == stations.end()) {
				diagnostics.error("scene.ref.unresolved", "Stop refers to unknown station", "services.json",
						"service", service.id, stopPath + ".station", stop.stationId);
			} else if (!stop.platformId.empty()) {
				bool platformFound = false;
				for (const auto& platform : station->second->platforms) {
					if (platform.id == stop.platformId) {
						platformFound = true;
						break;
					}
				}
				if (!platformFound)
					diagnostics.error("scene.ref.platform", "Stop refers to platform not on station",
							"services.json", "service", service.id, stopPath + ".platform", stop.platformId);
			}
			if (stop.hasPlannedArrival && stop.hasPlannedDeparture
					&& stop.plannedDepartureSeconds < stop.plannedArrivalSeconds) {
				diagnostics.error("scene.time.invalid", "Departure before arrival", "services.json",
						"service", service.id, stopPath + ".planned_departure_seconds");
			}
			if (stop.hasPlannedDeparture) {
				if (hasPreviousDeparture && stop.plannedDepartureSeconds < previousDeparture)
					diagnostics.warning("scene.time.order", "Non-increasing departure times", "services.json",
							"service", service.id, stopPath + ".planned_departure_seconds");
				hasPreviousDeparture = true;
				previousDeparture = stop.plannedDepartureSeconds;
			} else if (stopIndex + 1 < service.stops.size()) {
				diagnostics.warning("scene.time.departure.missing",
						"Intermediate stop has no planned departure", "services.json", "service", service.id,
						stopPath + ".planned_departure_seconds");
			}
			if (stop.dwellSeconds < 0.0)
				diagnostics.error("scene.dwell.invalid", "Negative dwell time", "services.json", "service",
						service.id, stopPath + ".dwell_seconds", "",
						"Use a dwell time of 0 or more seconds");
			if (stop.hasPlannedArrival && stop.hasPlannedDeparture
					&& stop.dwellSeconds > stop.plannedDepartureSeconds - stop.plannedArrivalSeconds) {
				diagnostics.warning("scene.dwell.exceeds_window",
						"Dwell time exceeds departure - arrival window", "services.json", "service", service.id,
						stopPath + ".dwell_seconds");
			}
		}
	}

	std::unordered_set<std::string> scenarioIds;
	std::unordered_set<std::string> incidentIds;
	for (std::size_t scenarioIndex = 0; scenarioIndex < scene.scenarios.size(); ++scenarioIndex) {
		const SceneScenario& scenario = scene.scenarios[scenarioIndex];
		const std::string scenarioPath = "scenarios[" + std::to_string(scenarioIndex) + "]";
		if (!scenarioIds.insert(scenario.id).second)
			diagnostics.error("scene.id.duplicate", "Duplicate scenario id", "scenarios.json", "scenario",
					scenario.id, scenarioPath + ".id", scenario.id);
		for (std::size_t incidentIndex = 0; incidentIndex < scenario.incidents.size(); ++incidentIndex) {
			const SceneIncident& incident = scenario.incidents[incidentIndex];
			const std::string path = scenarioPath + ".incidents[" + std::to_string(incidentIndex) + "]";
			if (!incidentIds.insert(incident.id).second)
				diagnostics.error("scene.id.duplicate", "Duplicate incident id", "scenarios.json", "incident",
						incident.id, path + ".id", incident.id);
			if (incident.type == "signal_failure") {
				bool targetFound = hasId(signalIds, incident.target);
				if (!targetFound)
					targetFound = blockReferenceKnown(incident.target);
				if (!targetFound)
					diagnostics.error("scene.ref.unresolved", "Signal failure refers to unknown signal or block",
							"scenarios.json", "incident", incident.id, path + ".target", incident.target);
			} else if (incident.type == "train_breakdown") {
				if (!hasId(serviceIds, incident.target))
					diagnostics.error("scene.ref.unresolved", "Train breakdown refers to unknown service",
							"scenarios.json", "incident", incident.id, path + ".target", incident.target);
			} else {
				diagnostics.error("scene.incident.type", "Unknown incident type", "scenarios.json", "incident",
						incident.id, path + ".type", incident.type,
						"Use signal_failure or train_breakdown");
			}
			if (incident.endSeconds <= incident.startSeconds || incident.startSeconds < 0.0
					|| incident.endSeconds < 0.0)
				diagnostics.error("scene.incident.window", "Invalid incident time window", "scenarios.json",
						"incident", incident.id, path, "",
						"Set end_seconds after start_seconds, both 0 or more");
		}
		for (std::size_t delayIndex = 0; delayIndex < scenario.entranceDelays.size(); ++delayIndex) {
			const SceneEntranceDelay& delay = scenario.entranceDelays[delayIndex];
			const std::string path = scenarioPath + ".entrance_delays[" + std::to_string(delayIndex) + "]";
			if (!hasId(serviceIds, delay.serviceId))
				diagnostics.error("scene.ref.unresolved", "Entrance delay refers to unknown service",
						"scenarios.json", "entrance_delay", delay.serviceId, path + ".service", delay.serviceId);
			if (!hasId(stationIds, delay.stationId))
				diagnostics.error("scene.ref.unresolved", "Entrance delay refers to unknown station",
						"scenarios.json", "entrance_delay", delay.serviceId, path + ".station", delay.stationId);
			if (delay.occurrence <= 0)
				diagnostics.error("scene.occurrence.invalid", "Entrance delay occurrence must be positive",
						"scenarios.json", "entrance_delay", delay.serviceId, path + ".occurrence");
			if (delay.delaySeconds < 0.0)
				diagnostics.error("scene.delay.invalid", "Entrance delay cannot be negative", "scenarios.json",
						"entrance_delay", delay.serviceId, path + ".delay_seconds");
		}
	}
	if (!scene.defaultScenarioId.empty() && !hasId(scenarioIds, scene.defaultScenarioId))
		diagnostics.error("scene.ref.unresolved", "Default scenario refers to unknown scenario",
				"scenarios.json", "scene", "", "default_scenario_id", scene.defaultScenarioId);

	std::unordered_set<std::string> passengerIds;
	std::unordered_set<std::string> journeyIds;
	std::unordered_set<std::string> passengerLegIds;
	for (std::size_t passengerIndex = 0; passengerIndex < scene.passengers.size(); ++passengerIndex) {
		const ScenePassenger& passenger = scene.passengers[passengerIndex];
		const std::string passengerPath = "passengers[" + std::to_string(passengerIndex) + "]";
		if (!passengerIds.insert(passenger.id).second)
			diagnostics.error("scene.id.duplicate", "Duplicate passenger id", "passengers.json", "passenger",
					passenger.id, passengerPath + ".id", passenger.id);
		for (std::size_t journeyIndex = 0; journeyIndex < passenger.journeys.size(); ++journeyIndex) {
			const ScenePassengerJourney& journey = passenger.journeys[journeyIndex];
			const std::string journeyPath = passengerPath + ".journeys[" + std::to_string(journeyIndex) + "]";
			if (!journeyIds.insert(journey.id).second)
				diagnostics.error("scene.id.duplicate", "Duplicate passenger journey id", "passengers.json",
						"journey", journey.id, journeyPath + ".id", journey.id);
			if (!hasId(stationIds, journey.originStationId))
				diagnostics.error("scene.ref.unresolved", "Journey refers to unknown origin station",
						"passengers.json", "journey", journey.id, journeyPath + ".origin", journey.originStationId);
			if (!hasId(stationIds, journey.destinationStationId))
				diagnostics.error("scene.ref.unresolved", "Journey refers to unknown destination station",
						"passengers.json", "journey", journey.id, journeyPath + ".destination",
						journey.destinationStationId);
			if (journey.plannedDepartureStartSeconds < 0.0
					|| journey.plannedDepartureEndSeconds < journey.plannedDepartureStartSeconds)
				diagnostics.error("scene.passenger.window", "Invalid planned departure window", "passengers.json",
						"journey", journey.id, journeyPath + ".planned_departure");
			if (journey.plannedArrivalStartSeconds < 0.0
					|| journey.plannedArrivalEndSeconds < journey.plannedArrivalStartSeconds)
				diagnostics.error("scene.passenger.window", "Invalid planned arrival window", "passengers.json",
						"journey", journey.id, journeyPath + ".planned_arrival");
			if (journey.legs.empty())
				diagnostics.warning("scene.passenger.legs.empty", "Journey has no route-choice legs", "passengers.json",
						"journey", journey.id, journeyPath + ".legs");
			for (std::size_t legIndex = 0; legIndex < journey.legs.size(); ++legIndex) {
				const ScenePassengerLeg& leg = journey.legs[legIndex];
				const std::string legPath = journeyPath + ".legs[" + std::to_string(legIndex) + "]";
				if (!passengerLegIds.insert(leg.id).second)
					diagnostics.error("scene.id.duplicate", "Duplicate passenger leg id", "passengers.json",
							"leg", leg.id, legPath + ".id", leg.id);
				if (!hasId(stationIds, leg.originStationId))
					diagnostics.error("scene.ref.unresolved", "Passenger leg refers to unknown origin station",
							"passengers.json", "leg", leg.id, legPath + ".origin", leg.originStationId);
				if (!hasId(stationIds, leg.destinationStationId))
					diagnostics.error("scene.ref.unresolved", "Passenger leg refers to unknown destination station",
							"passengers.json", "leg", leg.id, legPath + ".destination",
							leg.destinationStationId);
				if (!hasId(serviceIds, leg.serviceId))
					diagnostics.error("scene.ref.unresolved", "Passenger leg refers to unknown service",
							"passengers.json", "leg", leg.id, legPath + ".service", leg.serviceId);
				if (leg.occurrence <= 0)
					diagnostics.error("scene.occurrence.invalid", "Passenger leg occurrence must be positive",
							"passengers.json", "leg", leg.id, legPath + ".occurrence");
				if (legIndex == 0 && leg.originStationId != journey.originStationId)
					diagnostics.error("scene.passenger.continuity", "First passenger leg does not start at journey origin",
							"passengers.json", "journey", journey.id, legPath + ".origin", leg.originStationId);
				if (legIndex > 0 && leg.originStationId != journey.legs[legIndex - 1].destinationStationId)
					diagnostics.error("scene.passenger.continuity", "Passenger legs are not continuous",
							"passengers.json", "journey", journey.id, legPath + ".origin", leg.originStationId);
				if (legIndex + 1 == journey.legs.size()
						&& leg.destinationStationId != journey.destinationStationId)
					diagnostics.error("scene.passenger.continuity",
							"Last passenger leg does not end at journey destination", "passengers.json", "journey",
							journey.id, legPath + ".destination", leg.destinationStationId);
			}
		}
	}

	if (runnable) {
		if (scene.baseTime.empty())
			diagnostics.error("scene.basetime.missing", "Runnable scene requires base_time", "scene.json",
					"scene", "", "base_time", "", "Set scene.json base_time to HH:MM:SS");
		if (!scene.settings.hasDuration)
			diagnostics.error("scene.duration.missing", "Runnable scene requires a positive duration",
					"scene.json", "scene", "", "simulation_settings.duration_seconds");
		if (scene.tracks.empty())
			diagnostics.error("scene.topology.tracks.none", "Runnable scene has no tracks",
					"infrastructure.json", "track", "", "tracks");
		if (scene.nodes.empty())
			diagnostics.error("scene.topology.nodes.none", "Runnable scene has no nodes",
					"infrastructure.json", "node", "", "nodes");
		if (scene.arcs.empty())
			diagnostics.error("scene.topology.arcs.none", "Runnable scene has no arcs",
					"infrastructure.json", "arc", "", "arcs");
		if (scene.blocks.empty())
			diagnostics.error("scene.topology.blocks.none", "Runnable scene has no blocks",
					"infrastructure.json", "block", "", "blocks");
		if (scene.stations.empty())
			diagnostics.error("scene.stations.none", "Runnable scene has no stations", "stations.json",
					"station", "", "stations");
		std::unordered_set<std::string> usedPlatforms;
		for (const auto& service : scene.services) {
			for (const auto& stop : service.stops) {
				if (!stop.platformId.empty())
					usedPlatforms.insert(stop.stationId + "\n" + stop.platformId);
			}
		}
		int boundPlatformCount = 0;
		for (std::size_t stationIndex = 0; stationIndex < scene.stations.size(); ++stationIndex) {
			const SceneStation& station = scene.stations[stationIndex];
			const std::string stationPath = "stations[" + std::to_string(stationIndex) + "]";
			for (const auto& platform : station.platforms) {
				if (!platform.nodeIds.empty())
					++boundPlatformCount;
				if (usedPlatforms.count(station.id + "\n" + platform.id) > 0 && platform.nodeIds.empty())
					diagnostics.error("scene.platform.nodes.none", "Platform has no bound nodes", "stations.json",
							"platform", platform.id, stationPath + ".platforms");
			}
		}
		if (boundPlatformCount == 0)
			diagnostics.error("scene.platforms.none", "Runnable scene has no bound platform nodes",
					"stations.json", "station", "", "stations[].platforms[].nodes");
		if (scene.routes.empty())
			diagnostics.error("scene.routes.none", "Runnable scene has no routes", "signalling.json", "route",
					"", "routes");
		if (scene.scenarios.empty())
			diagnostics.error("scene.scenarios.none", "Runnable scene has no scenarios", "scenarios.json",
					"scenario", "", "scenarios");
		if (scene.defaultScenarioId.empty())
			diagnostics.error("scene.scenario.default.missing", "Runnable scene requires a default scenario",
					"scenarios.json", "scene", "", "default_scenario_id");
	}

	return result;
}

} // namespace

std::vector<SceneDiagnostic> validateSceneStructure(const std::string& sceneDir) {
	return loadScene(sceneDir).diagnostics;
}

std::vector<SceneDiagnostic> validateScene(const SceneModel& scene) {
	return validateCore(scene, false);
}

std::vector<SceneDiagnostic> validateRunnableScene(const SceneModel& scene) {
	return validateCore(scene, true);
}

std::vector<SceneDiagnostic> validateSceneDirectory(const std::string& sceneDir) {
	SceneLoadResult loadResult = loadScene(sceneDir);
	if (hasErrors(loadResult.diagnostics))
		return loadResult.diagnostics;
	std::vector<SceneDiagnostic> diagnostics = loadResult.diagnostics;
	const std::vector<SceneDiagnostic> semantic = validateScene(loadResult.scene);
	diagnostics.insert(diagnostics.end(), semantic.begin(), semantic.end());
	return diagnostics;
}

std::vector<SceneDiagnostic> validateRunnableSceneDirectory(const std::string& sceneDir) {
	SceneLoadResult loadResult = loadScene(sceneDir);
	if (hasErrors(loadResult.diagnostics))
		return loadResult.diagnostics;
	std::vector<SceneDiagnostic> diagnostics = loadResult.diagnostics;
	const std::vector<SceneDiagnostic> runnable = validateRunnableScene(loadResult.scene);
	diagnostics.insert(diagnostics.end(), runnable.begin(), runnable.end());
	return diagnostics;
}
