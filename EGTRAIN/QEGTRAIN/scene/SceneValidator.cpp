#include "scene/SceneValidator.h"
#include <unordered_set>
#include <unordered_map>
#include <regex>

std::vector<SceneDiagnostic> validateSceneDirectory(const std::string& sceneDir) {
	SceneLoadResult loadRes = loadScene(sceneDir);
	if (hasErrors(loadRes.diagnostics)) {
		return loadRes.diagnostics;
	}
	std::vector<SceneDiagnostic> semantic = validateScene(loadRes.scene);
	std::vector<SceneDiagnostic> combined = loadRes.diagnostics; // keeps warnings/infos if any
	combined.insert(combined.end(), semantic.begin(), semantic.end());
	return combined;
}

std::vector<SceneDiagnostic> validateScene(const SceneModel& scene) {
	std::vector<SceneDiagnostic> diags;
	auto addDiag = [&](SceneSeverity sev, const std::string& code, const std::string& msg,
					   const std::string& file, const std::string& itemType, const std::string& itemId,
					   const std::string& path = "", const std::string& relatedId = "", const std::string& suggestedFix = "") {
		SceneDiagnostic d;
		d.severity = sev;
		d.code = code;
		d.message = msg;
		d.file = file;
		d.itemType = itemType;
		d.itemId = itemId;
		d.path = path;
		d.relatedId = relatedId;
		d.suggestedFix = suggestedFix;
		diags.push_back(d);
	};

	if (!scene.baseTime.empty()) {
		std::regex re("^\\d{2}:\\d{2}:\\d{2}$");
		if (!std::regex_match(scene.baseTime, re)) {
			addDiag(SceneSeverity::Error, "scene.basetime.invalid", "Invalid base_time format, must be HH:MM:SS",
					"scene.json", "scene", "", "base_time", "",
					"Write the base time as HH:MM:SS, for example 08:00:00");
		}
	}

	if (scene.compositions.empty() || scene.trainUnits.empty()) {
		addDiag(SceneSeverity::Error, "scene.trains.none", "No trains defined",
				"rolling_stock.json", "", "", "", "",
				"Add at least one train unit and one composition to rolling_stock.json");
	}
	if (scene.services.empty()) {
		addDiag(SceneSeverity::Error, "scene.services.none", "No services defined",
				"services.json", "", "", "", "",
				"Add at least one service to services.json");
	}

	std::unordered_map<std::string, const SceneStation*> stationMap;
	std::unordered_set<std::string> stationIds;
	for (const auto& st : scene.stations) {
		if (!stationIds.insert(st.id).second) {
			addDiag(SceneSeverity::Error, "scene.id.duplicate", "Duplicate station id",
					"stations.json", "station", st.id, "", "stations.json",
					"Give each station a unique id");
		}
		stationMap[st.id] = &st;
	}

	std::unordered_set<std::string> signalIds;
	for (const auto& sig : scene.signals) {
		if (!signalIds.insert(sig.id).second) {
			addDiag(SceneSeverity::Error, "scene.id.duplicate", "Duplicate signal id",
					"signalling.json", "signal", sig.id, "", "signalling.json",
					"Give each signal a unique id");
		}
	}

	std::unordered_set<std::string> routeIds;
	for (const auto& r : scene.routes) {
		if (!routeIds.insert(r.id).second) {
			addDiag(SceneSeverity::Error, "scene.id.duplicate", "Duplicate route id",
					"signalling.json", "route", r.id, "", "signalling.json",
					"Give each route a unique id");
		}
		if (r.blocks.empty()) {
			addDiag(SceneSeverity::Error, "scene.route.empty", "Route has no blocks",
					"signalling.json", "route", r.id, "routes[" + r.id + "].blocks", "",
					"List the block ids the route runs through");
		}
	}

	std::unordered_set<std::string> trainUnitIds;
	for (const auto& tu : scene.trainUnits) {
		if (!trainUnitIds.insert(tu.id).second) {
			addDiag(SceneSeverity::Error, "scene.id.duplicate", "Duplicate train unit id",
					"rolling_stock.json", "train_unit", tu.id, "", "rolling_stock.json",
					"Give each train unit a unique id");
		}
		const std::string tractionPath = "train_units[" + tu.id + "].traction_curve";
		if (tu.tractionCurve.empty()) {
			addDiag(SceneSeverity::Error, "scene.train.traction.empty", "Train unit has no traction data",
					"rolling_stock.json", "train_unit", tu.id, tractionPath, "",
					"Add at least one traction curve row");
		}
		for (size_t rowIndex = 0; rowIndex < tu.tractionCurve.size(); ++rowIndex) {
			const auto& row = tu.tractionCurve[rowIndex];
			if (row[0] >= row[1]) {
				addDiag(SceneSeverity::Error, "scene.train.traction.interval", "Traction curve lower speed must be below upper speed",
						"rolling_stock.json", "train_unit", tu.id,
						tractionPath + "[" + std::to_string(rowIndex) + "]", "",
						"Set the lower speed below the upper speed");
			}
			if (rowIndex == 0)
				continue;
			const auto& previous = tu.tractionCurve[rowIndex - 1];
			if (row[0] < previous[0]) {
				addDiag(SceneSeverity::Error, "scene.train.traction.order", "Traction curve rows are not in ascending speed order",
						"rolling_stock.json", "train_unit", tu.id, tractionPath, "",
						"Order rows by increasing lower speed");
			} else if (row[0] < previous[1]) {
				addDiag(SceneSeverity::Error, "scene.train.traction.overlap", "Traction curve intervals overlap",
						"rolling_stock.json", "train_unit", tu.id, tractionPath, "",
						"Adjust adjacent bounds so intervals do not overlap");
			}
		}
	}

	std::unordered_set<std::string> compositionIds;
	for (const auto& c : scene.compositions) {
		std::string path = "compositions[" + c.id + "]";
		if (!compositionIds.insert(c.id).second) {
			addDiag(SceneSeverity::Error, "scene.id.duplicate", "Duplicate composition id",
					"rolling_stock.json", "composition", c.id, "", "rolling_stock.json",
					"Give each composition a unique id");
		}
		if (c.units.empty()) {
			addDiag(SceneSeverity::Error, "scene.composition.empty", "Composition has no units",
					"rolling_stock.json", "composition", c.id, path + ".units", "",
					"List at least one train unit id");
		} else {
			for (const auto& u : c.units) {
				if (trainUnitIds.find(u) == trainUnitIds.end()) {
					addDiag(SceneSeverity::Error, "scene.ref.unresolved", "Composition refers to unknown train unit",
							"rolling_stock.json", "composition", c.id, path + ".units", u,
							"Add train unit " + u + " to rolling_stock.json or reference an existing one");
				}
			}
		}
	}

	std::unordered_set<std::string> serviceIds;
	for (const auto& s : scene.services) {
		std::string servicePath = "services[" + s.id + "]";
		if (!serviceIds.insert(s.id).second) {
			addDiag(SceneSeverity::Error, "scene.id.duplicate", "Duplicate service id",
					"services.json", "service", s.id, "", "services.json",
					"Give each service a unique id");
		}
		if (compositionIds.find(s.composition) == compositionIds.end()) {
			addDiag(SceneSeverity::Error, "scene.ref.unresolved", "Service refers to unknown composition",
					"services.json", "service", s.id, servicePath + ".composition", s.composition,
					"Add composition " + s.composition + " to rolling_stock.json or reference an existing one");
		}
		if (routeIds.find(s.route) == routeIds.end()) {
			addDiag(SceneSeverity::Error, "scene.ref.unresolved", "Service refers to unknown route",
					"services.json", "service", s.id, servicePath + ".route", s.route,
					"Add route " + s.route + " to signalling.json or reference an existing one");
		}
		if (s.hasRepeat && s.headwaySeconds <= 0.0) {
			addDiag(SceneSeverity::Error, "scene.repeat.invalid", "Non-positive headway",
					"services.json", "service", s.id, servicePath + ".repeat.headway_seconds", "",
					"Use a headway greater than 0 seconds");
		}
		if (s.stops.empty()) {
			if (!s.through) {
				addDiag(SceneSeverity::Warning, "scene.service.no_stops", "Service has no stops",
						"services.json", "service", s.id, servicePath + ".stops", "",
						"Add at least one stop to the service");
			}
		} else {
			if (s.through) {
				addDiag(SceneSeverity::Warning, "scene.service.through_stops", "Through service has stops",
						"services.json", "service", s.id, servicePath + ".through", "",
						"Remove the through flag or the stops");
			}
			bool hasLastDeparture = false;
			double lastDeparture = 0.0;
			for (size_t i = 0; i < s.stops.size(); ++i) {
				const auto& stop = s.stops[i];
				std::string stopPath = servicePath + ".stops[" + std::to_string(i) + "]";
				auto it = stationMap.find(stop.stationId);
				if (it == stationMap.end()) {
					addDiag(SceneSeverity::Error, "scene.ref.unresolved", "Stop refers to unknown station",
							"services.json", "service", s.id, stopPath + ".station", stop.stationId,
							"Add station " + stop.stationId + " to stations.json or reference an existing one");
				} else if (!stop.platformId.empty()) {
					bool found = false;
					for (const auto& p : it->second->platforms) {
						if (p.id == stop.platformId) {
							found = true;
							break;
						}
					}
					if (!found) {
						addDiag(SceneSeverity::Error, "scene.ref.platform", "Stop refers to platform not on station",
								"services.json", "service", s.id, stopPath + ".platform", stop.platformId,
								"Use a platform defined on station " + stop.stationId);
					}
				}

				// Negative times are allowed: schedules may reference instants
				// before the scene base time (real case-study data does this).
				if (stop.hasDeparture) {
					if (stop.hasArrival && stop.departureSeconds < stop.arrivalSeconds) {
						addDiag(SceneSeverity::Error, "scene.time.invalid", "Departure before arrival",
								"services.json", "service", s.id, stopPath + ".departure_seconds", "",
								"Set departure_seconds at or after arrival_seconds");
					}
					if (hasLastDeparture && stop.departureSeconds < lastDeparture) {
						addDiag(SceneSeverity::Warning, "scene.time.order", "Non-increasing departure times",
								"services.json", "service", s.id, stopPath + ".departure_seconds", "",
								"Order stops by increasing departure time");
					}
					hasLastDeparture = true;
					lastDeparture = stop.departureSeconds;
				}

				if (stop.dwellSeconds < 0.0) {
					addDiag(SceneSeverity::Error, "scene.dwell.invalid", "Negative dwell time",
							"services.json", "service", s.id, stopPath + ".dwell_seconds", "",
							"Use a dwell time of 0 or more seconds");
				}
				if (stop.hasArrival && stop.hasDeparture) {
					double window = stop.departureSeconds - stop.arrivalSeconds;
					if (stop.dwellSeconds > window) {
						addDiag(SceneSeverity::Warning, "scene.dwell.exceeds_window", "Dwell time exceeds departure - arrival window",
								"services.json", "service", s.id, stopPath + ".dwell_seconds", "",
								"Widen the arrival-departure window or reduce dwell_seconds");
					}
				}
			}
		}
	}

	std::unordered_set<std::string> incidentIds;
	for (const auto& inc : scene.incidents) {
		std::string path = "incidents[" + inc.id + "]";
		if (!incidentIds.insert(inc.id).second) {
			addDiag(SceneSeverity::Error, "scene.id.duplicate", "Duplicate incident id",
					"incidents.json", "incident", inc.id, "", "incidents.json",
					"Give each incident a unique id");
		}
		if (inc.type == "signal_failure") {
			if (signalIds.find(inc.target) == signalIds.end()) {
				addDiag(SceneSeverity::Error, "scene.ref.unresolved", "Incident refers to unknown signal",
						"incidents.json", "incident", inc.id, path + ".target", inc.target,
						"Point the incident at a signal defined in signalling.json");
			}
		} else if (inc.type == "train_breakdown") {
			if (serviceIds.find(inc.target) == serviceIds.end()) {
				addDiag(SceneSeverity::Error, "scene.ref.unresolved", "Incident refers to unknown service",
						"incidents.json", "incident", inc.id, path + ".target", inc.target,
						"Point the incident at a service defined in services.json");
			}
		} else {
			addDiag(SceneSeverity::Error, "scene.incident.type", "Unknown incident type",
					"incidents.json", "incident", inc.id, path + ".type", inc.type,
					"Use signal_failure or train_breakdown");
		}

		if (inc.endSeconds <= inc.startSeconds || inc.startSeconds < 0.0 || inc.endSeconds < 0.0) {
			addDiag(SceneSeverity::Error, "scene.incident.window", "Invalid incident time window",
					"incidents.json", "incident", inc.id, path, "",
					"Set end_seconds after start_seconds, both 0 or more");
		}
	}

	return diags;
}
