#include "scene/SceneModel.h"
#include "scene/SectionInventory.h"
#include "scene/SceneValidator.h"
#include "scene/SignallingLevel.h"
#include "scene/RuntimeLimits.h"
#include "scene/SceneFields.h"

// The Qt headers come before the undef: Qt defines `signals`, and the scene model has a member of that name.
#include <QByteArray>
#include <QString>
#include <QTest>
#ifdef signals
#undef signals
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

static bool hasCode(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.code == code)
			return true;
	}
	return false;
}

static bool hasCodeAndSeverity(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code,
	SceneSeverity severity) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.code == code && diagnostic.severity == severity)
			return true;
	}
	return false;
}

static bool hasCodeAndPath(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code,
	const std::string& path) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.severity == SceneSeverity::Error
			&& diagnostic.code == code && diagnostic.path == path)
			return true;
	}
	return false;
}

static const SceneDiagnostic* findCode(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.code == code)
			return &diagnostic;
	}
	return nullptr;
}

static std::vector<SceneDiagnostic> findAll(const std::vector<SceneDiagnostic>& diagnostics,
	const std::string& code) {
	std::vector<SceneDiagnostic> found;
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.code == code)
			found.push_back(diagnostic);
	}
	return found;
}

static bool contains(const std::string& text, const std::string& part) {
	return text.find(part) != std::string::npos;
}

// Text for a data tag: "empty" for no text, and a byte outside printable ASCII as \xNN.
static std::string printable(const std::string& text) {
	if (text.empty())
		return "empty";
	std::string result;
	for (const unsigned char byte : text) {
		if (byte < 0x20 || byte > 0x7e) {
			char escaped[5];
			std::snprintf(escaped, sizeof(escaped), "\\x%02X", byte);
			result += escaped;
		} else {
			result += static_cast<char>(byte);
		}
	}
	return result;
}

static SceneModel completeScene() {
	SceneModel scene;
	scene.schemaVersion = 1;
	scene.name = "Validator scene";
	scene.baseTime = "08:00:00";
	scene.settings.hasDuration = true;
	scene.settings.durationSeconds = 3600.0;
	scene.tracks.push_back({"track-1"});
	scene.nodes.push_back({"node-1", "track-1", 0.0, 0.0});
	scene.nodes.push_back({"node-2", "track-1", 1.0, 0.0});
	scene.nodes.push_back({"node-3", "track-1", 2.0, 0.0});
	scene.arcs.push_back({"arc-1", "track-1", "node-1", "node-2", 0.0, 0.0, 40.0});
	scene.arcs.push_back({"arc-2", "track-1", "node-2", "node-3", 1000.0, -1.0, 35.0});
	scene.blocks.push_back({"block-1", "track-1", 1.0});
	scene.blocks.push_back({"block-2", "track-1", 1.0});
	scene.connections.push_back({"connection-1", "node-1", "node-2", false, 0.0});

	SceneStation origin;
	origin.id = "station-1";
	origin.name = "Origin";
	origin.platforms.push_back({"platform-1", {"node-1"}});
	scene.stations.push_back(origin);
	SceneStation destination;
	destination.id = "station-2";
	destination.name = "Destination";
	destination.platforms.push_back({"platform-2", {"node-3"}});
	scene.stations.push_back(destination);

	scene.signals.push_back({"signal-1", "block-1"});
	scene.routes.push_back({"route-1", {"block-1", "block-2"}, false, "", false});
	scene.signallingAreas.push_back({"area-1", 0.0, 2.0, 0, {}});

	SceneTrainUnit unit;
	unit.id = "unit-1";
	unit.hasPhysical = true;
	unit.physical.max_speed_ms = 40.0;
	unit.tractionCurve.push_back({{0.0, 40.0, 100000.0, 0.0, 0.0}});
	scene.trainUnits.push_back(unit);
	scene.compositions.push_back({"composition-1", {"unit-1"}});

	SceneService service;
	service.id = "service-1";
	service.composition = "composition-1";
	service.route = "route-1";
	SceneStop first;
	first.stationId = "station-1";
	first.platformId = "platform-1";
	first.hasPlannedDeparture = true;
	first.plannedDepartureSeconds = 100.0;
	SceneStop last;
	last.stationId = "station-2";
	last.platformId = "platform-2";
	last.hasPlannedArrival = true;
	last.plannedArrivalSeconds = 200.0;
	service.stops = {first, last};
	scene.services.push_back(service);

	SceneScenario scenario;
	scenario.id = "baseline";
	scenario.name = "Baseline";
	scenario.incidents.push_back({"incident-1", "signal_failure", "signal-1", 300.0, 600.0});
	scenario.entranceDelays.push_back({"service-1", 1, "station-1", 10.0});
	scene.scenarios.push_back(scenario);
	scene.defaultScenarioId = "baseline";

	ScenePassenger passenger;
	passenger.id = "passenger-1";
	ScenePassengerJourney journey;
	journey.id = "journey-1";
	journey.originStationId = "station-1";
	journey.destinationStationId = "station-2";
	journey.plannedDepartureStartSeconds = 0.0;
	journey.plannedDepartureEndSeconds = 120.0;
	journey.plannedArrivalStartSeconds = 180.0;
	journey.plannedArrivalEndSeconds = 300.0;
	journey.legs.push_back({"leg-1", "station-1", "station-2", "service-1", 1});
	passenger.journeys.push_back(journey);
	scene.passengers.push_back(passenger);
	return scene;
}

// Rolling stock of the Assignment scene (SLT_Sprinter). The limits are
// 1.09 * 0.75 / 9.81 = 0.08333 for braking and 209000 / (9.81 * 151000) = 0.1411
// for starting.
static SceneTrainUnit sprinterUnit() {
	SceneTrainUnit unit;
	unit.id = "unit-1";
	unit.hasPhysical = true;
	unit.physical.mass_of_traction_unit_kg = 151000.0;
	unit.physical.max_speed_ms = 36.111111111111;
	unit.physical.max_deceleration_ms2 = 0.75;
	unit.tractionCurve.push_back({{0.0, 8.611111111111, 209000.0, 0.0, 0.0}});
	unit.tractionCurve.push_back({{8.611111111111, 36.111111111111, 324607.2915, -17671.4923, 292.9005}});
	return unit;
}

// Rolling stock of the Paimpol scene (Draisy). Its larger deceleration gives
// a braking limit of 0.1643.
static SceneTrainUnit paimpolUnit() {
	SceneTrainUnit unit;
	unit.id = "unit-2";
	unit.hasPhysical = true;
	unit.physical.mass_of_traction_unit_kg = 19000.0;
	unit.physical.mass_of_a_wagon_kg = 19000.0;
	unit.physical.number_of_wagons = 1.0;
	unit.physical.max_speed_ms = 27.7778;
	unit.physical.max_deceleration_ms2 = 1.5;
	unit.tractionCurve.push_back({{0.0, 6.111, 50000.0, 0.0, 0.0}});
	unit.tractionCurve.push_back({{6.111, 16.6667, 100439.0, -10241.0, 318.41}});
	return unit;
}

// The first block holds arc-1 and the second block arc-2.
static SceneModel steepGradientScene(double firstGradient, double secondGradient) {
	SceneModel scene = completeScene();
	scene.trainUnits = {sprinterUnit()};
	scene.arcs[0].gradientPercent = firstGradient;
	scene.arcs[1].gradientPercent = secondGradient;
	return scene;
}

static int steepGradientWarningCount(const SceneModel& scene) {
	const auto diagnostics = validateScene(scene);
	return static_cast<int>(std::count_if(diagnostics.begin(), diagnostics.end(),
		[](const SceneDiagnostic& diagnostic) { return diagnostic.code == "scene.route.gradient.steep"; }));
}

// Three tracks without stations or routes. Connection a-to-b joins switch-a to switch-b, b-to-c joins switch-b to
// switch-c, and a-to-c joins switch-a to switch-c.
static SceneModel switchTopologyScene() {
	SceneModel switchTopology;
	switchTopology.tracks = {{"switch-a"}, {"switch-b"}, {"switch-c"}};
	switchTopology.nodes = {{"a.0", "switch-a", 0.0, 0.0}, {"a.1", "switch-a", 1.0, 0.0},
		{"b.0", "switch-b", 2.0, 0.0}, {"b.1", "switch-b", 4.0, 0.0},
		{"c.0", "switch-c", 5.0, 0.0}, {"c.1", "switch-c", 6.0, 0.0}};
	switchTopology.arcs = {{"a.arc", "switch-a", "a.0", "a.1", 0.0, 0.0, 20.0},
		{"b.arc", "switch-b", "b.0", "b.1", 0.0, 0.0, 20.0},
		{"c.arc", "switch-c", "c.0", "c.1", 0.0, 0.0, 20.0}};
	switchTopology.blocks = {{"a.block", "switch-a", 1.0}, {"b.block", "switch-b", 2.0},
		{"c.block", "switch-c", 1.0}};
	switchTopology.connections = {{"a-to-b", "a.1", "b.0", false, 0.0},
		{"b-to-c", "b.1", "c.0", false, 0.0}, {"a-to-c", "a.1", "c.0", false, 0.0}};
	return switchTopology;
}

// The id of the last section of the inventory that the connection derives, or empty when it derives none.
static std::string connectionSection(const SceneSectionInventory& inventory, const std::string& connectionId) {
	std::string id;
	for (const auto& section : inventory.sections) {
		if (section.sourceConnectionId == connectionId)
			id = section.id;
	}
	return id;
}

// The complete scene with a second track whose first node node-4 connects to node-3, the last node of track-1, and an
// area on each track that gives the other level.
static SceneModel conflictingDerivedAreasScene() {
	SceneModel conflictingDerivedAreas = completeScene();
	conflictingDerivedAreas.tracks.push_back({"track-2"});
	conflictingDerivedAreas.nodes.push_back({"node-4", "track-2", 3.0, 0.0});
	conflictingDerivedAreas.nodes.push_back({"node-5", "track-2", 4.0, 0.0});
	conflictingDerivedAreas.arcs.push_back(
		{"arc-3", "track-2", "node-4", "node-5", 0.0, 0.0, 35.0});
	conflictingDerivedAreas.blocks.push_back({"block-3", "track-2", 1.0});
	conflictingDerivedAreas.connections = {
		{"connection-1", "node-3", "node-4", false, 0.0}};
	conflictingDerivedAreas.signallingAreas = {
		{"first", 0.0, 4.0, 2, "track-1"}, {"second", 0.0, 4.0, 3, "track-2"}};
	return conflictingDerivedAreas;
}

// The message of the runnable warning about route sections without a signalling level, or empty when there is none.
static std::string levelMissingMessage(const SceneModel& candidate) {
	const SceneDiagnostic* warning = nullptr;
	const auto candidateDiagnostics = validateRunnableScene(candidate);
	warning = findCode(candidateDiagnostics, "scene.signalling.level.missing");
	return warning == nullptr ? std::string() : warning->message;
}

// Which area decides the level of each section.
static SceneSignallingAnalysis analyze(const SceneModel& candidate) {
	return analyzeSignallingAreas(candidate, buildSceneSectionInventory(candidate));
}

static SceneSectionSignalling sectionOf(const SceneSignallingAnalysis& analysis, const char* id) {
	const SceneSectionSignalling* section = analysis.section(id);
	return section == nullptr ? SceneSectionSignalling() : *section;
}

static bool listed(const std::vector<std::string>& ids, const char* id) {
	return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// The complete scene whose incident is a breakdown of service-1 that reduces its speed and has no end.
static SceneModel reducedBreakdownScene() {
	SceneModel reducedBreakdown = completeScene();
	SceneIncident& reducedIncident = reducedBreakdown.scenarios[0].incidents[0];
	reducedIncident.type = "train_breakdown";
	reducedIncident.target = "service-1";
	reducedIncident.startSeconds = 300.0;
	reducedIncident.endSeconds = 0.0;
	reducedIncident.hasEndSeconds = false;
	reducedIncident.hasReducedSpeed = true;
	reducedIncident.reducedSpeedKmh = 40.0;
	reducedIncident.hasOccurrence = true;
	reducedIncident.occurrence = 1;
	return reducedBreakdown;
}

static std::vector<SceneDiagnostic> implausible(const SceneModel& scene) {
	return findAll(validateScene(scene), "scene.train.value.implausible");
}

// Every number of a rolling stock unit is plausible up to its bound and gets one warning beyond it, above or below.
// value is the number in scene.
static void checkRange(const SceneModel& scene, double& value, const SceneValueRange& range, const std::string& path) {
	value = range.plausibleMax;
	QVERIFY2(implausible(scene).empty(), ("the upper bound is plausible at " + path).c_str());
	for (const double beyond : {range.plausibleMax * 2.0 + 1.0, range.plausibleMin - 1.0}) {
		value = beyond;
		const auto found = implausible(scene);
		// A value beyond the bound gets one warning at the path.
		QCOMPARE(found.size(), std::size_t(1));
		QVERIFY(found[0].severity == SceneSeverity::Warning);
		QCOMPARE(found[0].path, path);
	}
}

// A change to the complete scene that names its data row.
struct SceneChange {
	const char* name;
	void (*apply)(SceneModel&);
};

// Scenes whose areas are correct: the complete scene, a network-wide and a track-scoped area of different levels,
// and two areas of different levels that meet on a section edge.
static const SceneChange kCorrectAreaScenes[] = {
	{"clean", [](SceneModel&) {}},
	{"alignedAreas", [](SceneModel& alignedAreas) {
		 alignedAreas.signallingAreas = {{"network", 0.0, 2.0, 2, {}}, {"track", 0.0, 2.0, 4, "track-1"}};
	 }},
	{"edgesAdjacent", [](SceneModel& edgesAdjacent) {
		 edgesAdjacent.signallingAreas = {{"first", 0.0, 1.0, 2, {}}, {"second", 1.0, 2.0, 3, {}}};
	 }},
};

// The names of the signalling levels.
static const std::pair<int, const char*> kLevelLabels[] = {
	{kSignallingLevelUnset, "No signalling"}, {0, "0 ATB fixed block"},
	{1, "1 ETCS Level 1 fixed block"}, {2, "2 ETCS Level 2 fixed block"},
	{3, "3 ETCS Level 3 moving block"}, {4, "4 Virtual coupling"}, {5, "5 BACC track circuits"},
	{-1, "Invalid level -1"}, {6, "Invalid level 6"}, {-99999998, "Invalid level -99999998"}};

// The numbers of the signalling levels in the file format.
static const std::pair<SignallingLevel, int> kLevelNumbers[] = {{SignallingLevel::Atb, 0},
	{SignallingLevel::EtcsLevel1, 1}, {SignallingLevel::EtcsLevel2, 2}, {SignallingLevel::EtcsLevel3, 3},
	{SignallingLevel::VirtualCoupling, 4}, {SignallingLevel::Bacc, 5}};

struct FailureCase {
	const char* name;
	void (*mutate)(SceneModel&);
	const char* code;
	const char* path = nullptr;
};

// Changes to the complete scene that runnable validation reports with code, at path when it is given.
static const FailureCase kFailureCases[] = {
	{"empty track id", [](SceneModel& scene) { scene.tracks[0].id.clear(); }, "scene.id.empty",
		"tracks[0].id"},
	{"empty node id", [](SceneModel& scene) { scene.nodes[0].id.clear(); }, "scene.id.empty",
		"nodes[0].id"},
	{"empty arc id", [](SceneModel& scene) { scene.arcs[0].id.clear(); }, "scene.id.empty",
		"arcs[0].id"},
	{"empty block id", [](SceneModel& scene) { scene.blocks[0].id.clear(); }, "scene.id.empty",
		"blocks[0].id"},
	{"empty connection id", [](SceneModel& scene) { scene.connections[0].id.clear(); }, "scene.id.empty",
		"connections[0].id"},
	{"empty station id", [](SceneModel& scene) { scene.stations[0].id.clear(); }, "scene.id.empty",
		"stations[0].id"},
	{"empty platform id", [](SceneModel& scene) { scene.stations[0].platforms[0].id.clear(); }, "scene.id.empty",
		"stations[0].platforms[0].id"},
	{"duplicate route id", [](SceneModel& scene) { scene.routes.push_back(scene.routes[0]); },
		"scene.id.duplicate"},
	{"unknown composition unit", [](SceneModel& scene) { scene.compositions[0].units[0] = "missing-unit"; },
		"scene.ref.unresolved"},
	{"unknown stop platform", [](SceneModel& scene) { scene.services[0].stops[0].platformId = "missing-platform"; },
		"scene.ref.platform"},
	{"unknown scenario incident target", [](SceneModel& scene) {
		 scene.scenarios[0].incidents[0].target = "missing-signal";
	 },
		"scene.ref.unresolved"},
	{"missing base time", [](SceneModel& scene) { scene.baseTime.clear(); }, "scene.basetime.missing"},
	{"out-of-range base time", [](SceneModel& scene) { scene.baseTime = "24:00:00"; }, "scene.basetime.invalid"},
	{"non-positive duration", [](SceneModel& scene) { scene.settings.durationSeconds = 0.0; }, "scene.duration.invalid"},
	{"non-finite duration", [](SceneModel& scene) {
		 scene.settings.durationSeconds = std::numeric_limits<double>::quiet_NaN();
	 },
		"scene.duration.invalid"},
	{"performance below range", [](SceneModel& scene) {
		 scene.services[0].performancePercent = 0.5;
	 },
		"scene.performance.invalid", "services[service-1].performance_percent"},
	{"non-finite maximum speed", [](SceneModel& scene) {
		 scene.services[0].hasMaximumSpeed = true;
		 scene.services[0].maximumSpeedKmh = std::numeric_limits<double>::infinity();
	 },
		"scene.speed.invalid", "services[service-1].maximum_speed_kmh"},
	{"non-positive repeat count", [](SceneModel& scene) {
		 scene.services[0].hasRepeat = true;
		 scene.services[0].headwaySeconds = 30.0;
		 scene.services[0].hasRepeatCount = true;
		 scene.services[0].repeatCount = 0;
	 },
		"scene.repeat.count.invalid", "services[service-1].repeat.count"},
	{"non-decimal operating-code step", [](SceneModel& scene) {
		 scene.services[0].operatingCode = "R100";
		 scene.services[0].hasRepeat = true;
		 scene.services[0].headwaySeconds = 30.0;
		 scene.services[0].hasOperatingCodeStep = true;
		 scene.services[0].operatingCodeStep = 2;
	 },
		"scene.repeat.step.invalid", "services[service-1].repeat.operating_code_step"},
	{"overflowing operating-code step", [](SceneModel& scene) {
		 scene.services[0].operatingCode = "9223372036854775806";
		 scene.services[0].hasRepeat = true;
		 scene.services[0].headwaySeconds = 30.0;
		 scene.services[0].hasRepeatCount = true;
		 scene.services[0].repeatCount = 2;
		 scene.services[0].hasOperatingCodeStep = true;
		 scene.services[0].operatingCodeStep = 2;
	 },
		"scene.repeat.step.invalid", "services[service-1].repeat.operating_code_step"},
	{"entrance delay beyond explicit pattern", [](SceneModel& scene) {
		 scene.services[0].hasRepeat = true;
		 scene.services[0].headwaySeconds = 30.0;
		 scene.services[0].hasRepeatCount = true;
		 scene.services[0].repeatCount = 1;
		 scene.scenarios[0].entranceDelays[0].occurrence = 2;
	 },
		"scene.entrance.occurrence.out_of_horizon", "scenarios[0].entrance_delays[0].occurrence"},
	{"non-finite entrance delay", [](SceneModel& scene) {
		 scene.scenarios[0].entranceDelays[0].delaySeconds = std::numeric_limits<double>::quiet_NaN();
	 },
		"scene.delay.invalid", "scenarios[0].entrance_delays[0].delay_seconds"},
	{"entrance delay station outside service stops", [](SceneModel& scene) {
		 scene.stations.push_back({"station-3", "Unused", false, 0.0, {}});
		 scene.scenarios[0].entranceDelays[0].stationId = "station-3";
	 },
		"scene.entrance.station", "scenarios[0].entrance_delays[0].station"},
	{"entrance delay stop without departure", [](SceneModel& scene) {
		 scene.scenarios[0].entranceDelays[0].stationId = "station-2";
	 },
		"scene.entrance.timetable", "scenarios[0].entrance_delays[0].station"},
	{"conflicting entrance delays", [](SceneModel& scene) {
		 scene.scenarios[0].entranceDelays.push_back({"service-1", 1, "station-1", 20.0});
	 },
		"scene.entrance.conflict", "scenarios[0].entrance_delays[1].delay_seconds"},
	{"non-finite node coordinate", [](SceneModel& scene) {
		 scene.nodes[0].xKm = std::numeric_limits<double>::quiet_NaN();
	 },
		"scene.node.coordinate.invalid", "nodes[0].x_km"},
	{"negative arc curvature", [](SceneModel& scene) {
		 scene.arcs[0].curvatureRadiusM = -1.0;
	 },
		"scene.arc.curvature.invalid", "arcs[0].curvature_radius_m"},
	{"non-positive arc speed", [](SceneModel& scene) { scene.arcs[0].speedLimitMs = 0.0; }, "scene.arc.speed.invalid", "arcs[0].speed_limit_ms"},
	{"non-positive block length", [](SceneModel& scene) { scene.blocks[0].lengthKm = 0.0; }, "scene.block.length.invalid", "blocks[0].length_km"},
	{"invalid optional connection speed", [](SceneModel& scene) {
		 scene.connections[0].hasSpeedLimit = true;
		 scene.connections[0].speedLimitMs = 0.0;
	 },
		"scene.connection.speed.invalid", "connections[0].speed_limit_ms"},
	{"arc endpoint on another track", [](SceneModel& scene) {
		 scene.tracks.push_back({"track-2"});
		 scene.nodes[1].trackId = "track-2";
	 },
		"scene.topology.track", "arcs[0].to"},
	{"arc self-loop", [](SceneModel& scene) { scene.arcs[0].toNodeId = "node-1"; }, "scene.topology.loop", "arcs[0].to"},
	{"ambiguous outgoing arc", [](SceneModel& scene) { scene.arcs[1].fromNodeId = "node-1"; }, "scene.topology.ambiguous", "tracks[0].nodes"},
	{"disconnected node", [](SceneModel& scene) {
		 scene.nodes.push_back({"node-4", "track-1", 3.0, 0.0});
	 },
		"scene.topology.disconnected", "tracks[0].nodes"},
	{"descending chain order", [](SceneModel& scene) { scene.nodes[1].xKm = -1.0; }, "scene.topology.order", "arcs[0].to"},
	{"empty tracks", [](SceneModel& scene) { scene.tracks.clear(); }, "scene.topology.tracks.none"},
	{"empty nodes", [](SceneModel& scene) { scene.nodes.clear(); }, "scene.topology.nodes.none"},
	{"empty arcs", [](SceneModel& scene) { scene.arcs.clear(); }, "scene.topology.arcs.none"},
	{"empty blocks", [](SceneModel& scene) { scene.blocks.clear(); }, "scene.topology.blocks.none"},
	{"empty routes", [](SceneModel& scene) { scene.routes.clear(); }, "scene.routes.none"},
	{"unbound platform", [](SceneModel& scene) {
		 scene.stations[0].platforms[0].nodeIds.clear();
	 },
		"scene.platform.nodes.none", "stations[0].platforms[0].nodes"},
	{"unanchored station", [](SceneModel& scene) { scene.stations[0].platforms.clear(); }, "scene.station.anchor.missing", "stations[0]"},
	{"non-finite station position", [](SceneModel& scene) {
		 scene.stations[0].hasPosition = true;
		 scene.stations[0].positionKm = std::numeric_limits<double>::quiet_NaN();
	 },
		"scene.station.position.invalid", "stations[0].position_km"},
	{"conflicting platform node", [](SceneModel& scene) {
		 scene.stations[1].platforms[0].nodeIds = {"node-1"};
	 },
		"scene.platform.node.conflict", "stations[1].platforms[0].nodes[0]"},
	{"platform outside service route", [](SceneModel& scene) {
		 scene.tracks.push_back({"track-2"});
		 scene.nodes.push_back({"node-4", "track-2", 0.0, 1.0});
		 scene.nodes.push_back({"node-5", "track-2", 1.0, 1.0});
		 scene.arcs.push_back({"arc-3", "track-2", "node-4", "node-5", 0.0, 0.0, 35.0});
		 scene.blocks.push_back({"block-3", "track-2", 1.0});
		 scene.stations[1].platforms[0].nodeIds = {"node-5"};
	 },
		"scene.ref.platform.route", "services[service-1].stops[1].platform"},
	{"platform outside composite switch route", [](SceneModel& scene) {
		 scene.tracks = {{"track-A"}, {"track-B"}};
		 scene.nodes = {
			 {"a-0", "track-A", 0.0, 0.0}, {"a-05", "track-A", 0.5, 0.0},
			 {"a-2", "track-A", 2.0, 0.0}, {"b-0", "track-B", 0.0, 1.0},
			 {"b-15", "track-B", 1.5, 1.0}, {"b-2", "track-B", 2.0, 1.0}};
		 scene.arcs = {
			 {"a-1", "track-A", "a-0", "a-05", 0.0, 0.0, 40.0},
			 {"a-2", "track-A", "a-05", "a-2", 0.0, 0.0, 40.0},
			 {"b-1", "track-B", "b-0", "b-15", 0.0, 0.0, 40.0},
			 {"b-2", "track-B", "b-15", "b-2", 0.0, 0.0, 40.0}};
		 scene.blocks = {{"A", "track-A", 2.0}, {"B", "track-B", 2.0}};
		 scene.connections = {{"switch", "a-05", "b-15", false, 0.0}};
		 scene.stations[0].platforms[0].nodeIds = {"a-0"};
		 scene.stations[1].platforms[0].nodeIds = {"a-2"};
		 scene.routes[0].blocks = {"@A@-0.500000/@B@-1.500000"};
	 },
		"scene.ref.platform.route", "services[service-1].stops[1].platform"},
	{"unknown route block", [](SceneModel& scene) { scene.routes[0].blocks[0] = "missing-block"; }, "scene.ref.unresolved"},
	{"unknown default scenario", [](SceneModel& scene) { scene.defaultScenarioId = "missing"; }, "scene.ref.unresolved"},
	{"discontinuous passenger leg", [](SceneModel& scene) {
		 scene.passengers[0].journeys[0].legs[0].destinationStationId = "station-1";
	 },
		"scene.passenger.continuity"},
};

class TestSceneValidator : public QObject {
	Q_OBJECT

private slots:
	void completeSceneValidates();
	void runnableValidationNeedsTrains();
	void acceptsValidServiceColour_data();
	void acceptsValidServiceColour();
	void warnsAboutInvalidServiceColour_data();
	void warnsAboutInvalidServiceColour();
	void parsesColourChannels();
	void rejectsMalformedColourText_data();
	void rejectsMalformedColourText();
	void looksUpServiceColour();
	void ordersTimetableEvents();
	void keepsShortDwellWindowAdvisory();
	void rejectsInvalidTimes_data();
	void rejectsInvalidTimes();
	void ignoresNegativeOffRouteContext();
	void stopsOverrideThroughFlag();
	void makesSafeOutputComponent_data();
	void makesSafeOutputComponent();
	void rejectsUnsafeSceneNames();
	void limitsTimetableStops();
	void limitsExpandedTrains();
	void rejectsNonFinitePlatformLength();
	void rejectsNonPositivePlatformWidth();
	void needsUsablePlatformCapacity();
	void platformHoldsAtLeastOnePassenger();
	void defaultPlatformHoldsAbout99Passengers();
	void rejectsNonFinitePassengerWindow();
	void legDestinationIsStopOfService();
	void rejectsReversePassengerLeg();
	void resolvesRepeatedServiceStations();
	void derivesSectionInventory();
	void doesNotResolveDuplicateBaseIds();
	void rejectsDisconnectedRouteOrder();
	void resolvesStopsOnReversedRoute();
	void rejectsDirectionChange();
	void rejectsMarkedForwardRoute_data();
	void rejectsMarkedForwardRoute();
	void rejectsMarkedOneSectionRoute();
	void skipsDirectionOfUnresolvedMarkedRoute();
	void joinsConnectionDerivedSections();
	void sharesLegacySectionTokenMatching();
	void rejectsWrongSwitchBranch();
	void rejectsLegacyWrongBranchFork();
	void rejectsLegacyDerivedReversal();
	void rejectsLegacyRegionalUTurn();
	void reportsCrossRegionJumps();
	void reportsUnboundSignal();
	void rejectsMalformedSignalBinding();
	void acceptsBlockSignalFailureTarget();
	void rejectsAmbiguousSignalFailureTarget();
	void validatesNetworkAndTrackAreas();
	void rejectsDuplicateAreaIds();
	void rejectsAreaRangeThatDoesNotIncrease();
	void rangeErrorNamesTrackWithoutBlocks();
	void rangeErrorGivesExtentOfTrack();
	void rejectsAreaLevelAboveFive();
	void resolvesAreaTrack();
	void allowsOverlapWithoutSharedSection();
	void rejectsConflictingNetworkAreas();
	void rejectsConflictingTrackAreas();
	void rejectsConflictOnDerivedSection();
	void measuresBlockExtent();
	void warnsAboutRouteSectionsWithoutArea();
	void partialAreaNamesUncoveredSections();
	void trackAreaCoversDerivedSection();
	void skipsSectionsOnNoRoute();
	void sceneWithoutRoutesHasNoLevelWarning();
	void levelWarningIgnoresRouteDirection();
	void levelWarningFollowsAreaEdges();
	void reportsConflictOncePerPair();
	void namesTheOneConflictingSection();
	void agreeingOrSeparateAreasDoNotConflict();
	void sameLevelOverlapDoesNotConflict();
	void warnsAboutAreaEndInsideSection();
	void warnsAboutAreaStartInsideSection();
	void warnsAboutEachCuttingEdge();
	void skipsCutSectionOnNoRoute();
	void alignedAreaEdgeGivesNoSplitWarning_data();
	void alignedAreaEdgeGivesNoSplitWarning();
	void oneWarningNamesSectionsOfOtherTracks();
	void warnsAboutAreaOutsideNetwork();
	void areaInsideOneSectionIsEmpty();
	void emptyTrackAreaGivesExtentOfTrack();
	void invalidAreasGetNoEmptyOrSplitWarning();
	void correctSceneGetsNoAreaWarning_data();
	void correctSceneGetsNoAreaWarning();
	void fixWithoutAreasGivesNetworkRange();
	void namesAreaBeforeStretch();
	void namesBothAreasAroundStretch();
	void givesEachTrackItsOwnStretch();
	void namesAreaInsideStretch();
	void listsSeparateStretchesOfTrack();
	void namesSignallingLevels_data();
	void namesSignallingLevels();
	void describesSignallingLevels();
	void numbersSignallingLevels_data();
	void numbersSignallingLevels();
	void acceptsLevelsZeroToFive_data();
	void acceptsLevelsZeroToFive();
	void rejectsLevelsOutsideZeroToFive_data();
	void rejectsLevelsOutsideZeroToFive();
	void fixedBlockLevels_data();
	void fixedBlockLevels();
	void movingBlockLevels_data();
	void movingBlockLevels();
	void levelsOutsideZeroToFiveHaveNoKind_data();
	void levelsOutsideZeroToFiveHaveNoKind();
	void storesUnsetLevel();
	void acceptsAreaOfLevelFive();
	void rejectsAreaOfLevelMinusOne();
	void invalidLevelAreaIsNotEmpty();
	void areaWithoutSystemHasOwnMessage();
	void otherInvalidLevelKeepsLevelMessage();
	void networkAreaDecidesItsSections();
	void trackAreaOverridesNetworkArea();
	void listsConflictsInAreaOrder();
	void firstOfAgreeingAreasDecides();
	void areaEdgesNameCutSections();
	void areaEdgesFollowTolerance();
	void unusableAreasTakeNoPart();
	void areaWithoutSystemTakesNoPart();
	void countsRouteSectionsOfArea();
	void restrictionActsAtEveryLevel_data();
	void restrictionActsAtEveryLevel();
	void warnsAboutRestrictionWithoutLevel();
	void warnsAboutSteepDescent();
	void countsSteepGradientWarnings_data();
	void countsSteepGradientWarnings();
	void reversedRouteTurnsAscentIntoDescent();
	void reversedRouteTurnsDescentIntoAscent();
	void servicesOfOneCompositionShareWarning();
	void judgesEachCompositionOnItsOwnLimit();
	void skipsGradientOfUnknownComposition();
	void skipsGradientOfUnresolvedRoute();
	void boundsPhysicalValues_data();
	void boundsPhysicalValues();
	void boundsTractionValues_data();
	void boundsTractionValues();
	void namesAtMostFiveSectionsOfLongRoute();
	void acceptsTrackOfManyNodes();
	void acceptsSceneOfManySections();
	void suggestsNegativeChainage();
	void conflictingSectionHasNoLevelWarning();
	void rejectsBrokenScene_data();
	void rejectsBrokenScene();
	void rejectsReservedSlashInBlockId();
	void reducedBreakdownMayOmitEnd();
	void durationOverrideAppliesToOccurrences();
	void fullHoldBreakdownNeedsEnd();
	void breakdownOccurrenceMustBePositive();
	void breakdownOccurrenceMustBeInRepeatRange();
	void reducedBreakdownSpeedMustBePositive();
	void breakdownEndMustFollowStart();
	void signalFailureRejectsBreakdownFields();
	void expandsRepeatedService();
	void namesOccurrencesWithoutStep();
	void allowsNegativeArcGradient();
	void keepsCoordinateTolerance();
	void rejectsBlockLayoutOverflow();
	void warnsAboutClippedFinalBlock();
	void enforcesBlockArcCapacity();
	void rejectsDerivedSectionIdCollision();
	void rejectsRuntimeBlockIdCollision();
	void enforcesRouteTokenCapacity();
	void enforcesEndpointFanout();
	void enforcesDependencyFanout();
	void validatesCompositeRouteEntries();
	void countsOccurrencesInWindow();
	void validatesHistoricalFixture();
	void reportsMissingSceneDirectory();
	void formatsDialogText();
};

void TestSceneValidator::completeSceneValidates() {
	const SceneModel clean = completeScene();
	QVERIFY2(!hasCode(validateScene(clean), "scene.topology.tracks.none"),
		"semantic validation does not reject complete topology");
	QVERIFY2(validateScene(clean).empty(), "complete scene passes semantic validation");
	QVERIFY2(validateRunnableScene(clean).empty(), "complete scene passes runnable validation");
}

void TestSceneValidator::runnableValidationNeedsTrains() {
	const SceneModel clean = completeScene();
	SceneModel withoutTrains = clean;
	withoutTrains.services.clear();
	withoutTrains.compositions.clear();
	withoutTrains.trainUnits.clear();
	withoutTrains.scenarios[0].entranceDelays.clear();
	withoutTrains.passengers.clear();
	QVERIFY2(!hasErrors(validateScene(withoutTrains)), "a scene without trains or services passes semantic validation");
	const auto withoutTrainsRunnable = validateRunnableScene(withoutTrains);
	// Runnable validation needs trains and services.
	QVERIFY(hasCodeAndSeverity(withoutTrainsRunnable, "scene.trains.none", SceneSeverity::Error));
	QVERIFY(hasCodeAndSeverity(withoutTrainsRunnable, "scene.services.none", SceneSeverity::Error));
}

void TestSceneValidator::acceptsValidServiceColour_data() {
	QTest::addColumn<QByteArray>("value");
	for (const char* value : {"", "#112233", "#abcdef", "#ABCDEF", "#aBc012"})
		QTest::newRow(printable(value).c_str()) << QByteArray(value);
}

void TestSceneValidator::acceptsValidServiceColour() {
	QFETCH(QByteArray, value);
	SceneModel colored = completeScene();
	colored.services[0].visualizationColor = value.toStdString();
	// Unset and valid service colours give no diagnostic.
	QVERIFY(!hasCode(validateScene(colored), "scene.service.color.invalid"));
	QVERIFY(validateScene(colored).empty());
}

void TestSceneValidator::warnsAboutInvalidServiceColour_data() {
	QTest::addColumn<QByteArray>("value");
	for (const char* value : {"#fff", "#12345g", "red", "#1234567", " #112233", "#112233 "})
		QTest::newRow(printable(value).c_str()) << QByteArray(value);
}

void TestSceneValidator::warnsAboutInvalidServiceColour() {
	QFETCH(QByteArray, value);
	SceneModel colored = completeScene();
	colored.services[0].visualizationColor = value.toStdString();
	const auto diagnostics = validateScene(colored);
	const SceneDiagnostic* invalidColor = findCode(diagnostics, "scene.service.color.invalid");
	// Invalid service colour gives one warning naming the service and field.
	QCOMPARE(diagnostics.size(), std::size_t(1));
	if (invalidColor == nullptr)
		QFAIL("the warning is not reported");
	QVERIFY(invalidColor->severity == SceneSeverity::Warning);
	QCOMPARE(invalidColor->file, "services.json");
	QCOMPARE(invalidColor->itemType, "service");
	QCOMPARE(invalidColor->itemId, "service-1");
	QCOMPARE(invalidColor->path, "services[service-1].visualization_color");
	QCOMPARE(invalidColor->relatedId, value.toStdString());
	QVERIFY(contains(invalidColor->message, "The default colour is used"));
	QVERIFY(contains(invalidColor->suggestedFix, "#3C8DD2"));
	// Invalid service colour does not block a run.
	QVERIFY(!hasErrors(diagnostics));
	QCOMPARE(validateRunnableScene(colored).size(), std::size_t(1));
}

void TestSceneValidator::parsesColourChannels() {
	int red = -1, green = -1, blue = -1;
	// Upper-case colour parses to its channels.
	QVERIFY(sceneParseVisualizationColor("#3C8DD2", &red, &green, &blue));
	QCOMPARE(red, 0x3C);
	QCOMPARE(green, 0x8D);
	QCOMPARE(blue, 0xD2);
	red = green = blue = -1;
	// Lower-case colour parses to its channels.
	QVERIFY(sceneParseVisualizationColor("#3c8dd2", &red, &green, &blue));
	QCOMPARE(red, 0x3C);
	QCOMPARE(green, 0x8D);
	QCOMPARE(blue, 0xD2);
	// Colour bounds parse without outputs too.
	QVERIFY(sceneParseVisualizationColor("#000000", &red, &green, &blue));
	QCOMPARE(red, 0);
	QCOMPARE(green, 0);
	QCOMPARE(blue, 0);
	QVERIFY(sceneParseVisualizationColor("#FFffFF", &red, &green, &blue));
	QCOMPARE(red, 255);
	QCOMPARE(green, 255);
	QCOMPARE(blue, 255);
	QVERIFY(sceneParseVisualizationColor("#112233"));
}

void TestSceneValidator::rejectsMalformedColourText_data() {
	QTest::addColumn<QByteArray>("value");
	for (const char* value : {"", "#", "#fff", "#ffff", "#fffff", "#fffffff", "#AARRGGBB", "#80112233", "112233",
			 "#11223g", "#11 233", " #112233", "#112233 ", "#112233\n", "red", "#+12233", "#-12233", "#0x1233",
			 "#12345\xB2", "#1234\xC3\xA9"})
		QTest::newRow(printable(value).c_str()) << QByteArray(value);
}

void TestSceneValidator::rejectsMalformedColourText() {
	QFETCH(QByteArray, value);
	int red = -1, green = -1, blue = -1;
	// Malformed colour text is rejected and leaves outputs unchanged.
	QVERIFY(!sceneParseVisualizationColor(value.toStdString(), &red, &green, &blue));
	QCOMPARE(red, -1);
	QCOMPARE(green, -1);
	QCOMPARE(blue, -1);
}

void TestSceneValidator::looksUpServiceColour() {
	const SceneModel clean = completeScene();
	SceneModel lookup = clean;
	lookup.services[0].visualizationColor = "#3c8dd2";
	// Service colour lookup returns the stored text, or empty for none or an unknown id.
	QCOMPARE(sceneServiceVisualizationColor(lookup, "service-1"), "#3c8dd2");
	QVERIFY(sceneServiceVisualizationColor(clean, "service-1").empty());
	QVERIFY(sceneServiceVisualizationColor(lookup, "service-9").empty());
	QVERIFY(sceneServiceVisualizationColor(lookup, "").empty());
}

void TestSceneValidator::ordersTimetableEvents() {
	const SceneModel clean = completeScene();
	SceneModel timetable = clean;
	timetable.services[0].stops[0].hasPlannedArrival = true;
	timetable.services[0].stops[0].plannedArrivalSeconds = 90.0;
	QVERIFY2(!hasErrors(validateScene(timetable)), "implicit departure entry allows origin pre-departure dwell");
	timetable.services[0].hasEntryTime = true;
	timetable.services[0].entryTimeSeconds = 100.0;
	QVERIFY2(hasCodeAndSeverity(validateScene(timetable), "scene.time.order", SceneSeverity::Error),
		"first arrival before explicit entry blocks Run");
	timetable.services[0].stops[0].plannedDepartureSeconds = 95.0;
	const auto beforeEntry = validateScene(timetable);
	// Each event before entry is diagnosed without regressing the ordering cursor.
	QCOMPARE(std::count_if(beforeEntry.begin(), beforeEntry.end(), [](const SceneDiagnostic& d) {
		return d.code == "scene.time.order";
	}),
		std::ptrdiff_t(2));
	timetable.services[0].entryTimeSeconds = 0.0;
	timetable.services[0].stops[0].hasPlannedDeparture = false;
	timetable.services[0].stops[1].plannedArrivalSeconds = 80.0;
	QVERIFY2(hasCodeAndSeverity(validateScene(timetable), "scene.time.order", SceneSeverity::Error),
		"arrival-only row constrains the next event");
}

void TestSceneValidator::keepsShortDwellWindowAdvisory() {
	SceneModel timetable = completeScene();
	timetable.services[0].stops[0].hasPlannedArrival = true;
	timetable.services[0].stops[0].plannedArrivalSeconds = 90.0;
	timetable.services[0].stops[0].dwellSeconds = 20.0;
	// Short dwell window remains advisory for historical schedules.
	QVERIFY(hasCodeAndSeverity(validateScene(timetable), "scene.dwell.exceeds_window", SceneSeverity::Warning));
	QVERIFY(!hasErrors(validateScene(timetable)));
}

void TestSceneValidator::rejectsInvalidTimes_data() {
	QTest::addColumn<double>("invalid");
	QTest::newRow("negative") << -1.0;
	QTest::newRow("infinity") << std::numeric_limits<double>::infinity();
	QTest::newRow("NaN") << std::numeric_limits<double>::quiet_NaN();
}

void TestSceneValidator::rejectsInvalidTimes() {
	QFETCH(double, invalid);
	SceneModel timetable = completeScene();
	timetable.services[0].hasEntryTime = true;
	timetable.services[0].entryTimeSeconds = invalid;
	timetable.services[0].stops[0].plannedDepartureSeconds = invalid;
	timetable.services[0].stops[0].dwellSeconds = invalid;
	const auto errors = validateScene(timetable);
	// Entry, planned times and dwell reject invalid numbers.
	QVERIFY(hasCode(errors, "scene.time.entry.invalid"));
	QVERIFY(hasCode(errors, "scene.time.invalid"));
	QVERIFY(hasCode(errors, "scene.dwell.invalid"));
}

void TestSceneValidator::ignoresNegativeOffRouteContext() {
	SceneModel timetable = completeScene();
	timetable.stations.push_back({"context", "Outside", true, 10.0, {}});
	timetable.services[0].stops.insert(timetable.services[0].stops.begin(),
		{"context", "", true, true, -20.0, -10.0, 0.0});
	timetable.services[0].hasEntryTime = true;
	timetable.services[0].entryTimeSeconds = 100.0;
	QVERIFY2(!hasErrors(validateScene(timetable)), "negative off-route context does not constrain runnable chronology");
}

void TestSceneValidator::stopsOverrideThroughFlag() {
	SceneModel timetable = completeScene();
	timetable.services[0].through = true;
	QVERIFY2(validateScene(timetable).empty(), "nonempty stops override historical through flag");
	timetable.services[0].through = false;
	timetable.services[0].stops.clear();
	timetable.passengers.clear();
	timetable.scenarios[0].entranceDelays.clear();
	QVERIFY2(validateRunnableScene(timetable).empty(), "empty stops need no through flag");
}

void TestSceneValidator::makesSafeOutputComponent_data() {
	QTest::addColumn<QByteArray>("name");
	QTest::addColumn<QByteArray>("expected");
	const std::vector<std::pair<std::string, std::string>> outputNameCases = {
		{"Readable Scene", "Readable Scene"},
		{"M\xC3\xBCnchen", "M\xC3\xBCnchen"},
		{".temp", ".temp"},
		{"COM10", "COM10"},
		{"LPT0", "LPT0"},
		{"auxiliary", "auxiliary"},
		{"", "scene"},
		{"../outside", "scene"},
		{"..\\outside", "scene"},
		{".", "scene"},
		{"..", "scene"},
		{"/absolute", "scene"},
		{"\\absolute", "scene"},
		{"//server/share", "scene"},
		{"\\\\server\\share", "scene"},
		{"C:", "scene"},
		{"C:\\outside", "scene"},
		{"CON", "scene"},
		{"cOn.txt", "scene"},
		{"PRN.log", "scene"},
		{"aUx.cfg", "scene"},
		{"NUL.json", "scene"},
		{"COM1", "scene"},
		{"com9.txt", "scene"},
		{"LPT1", "scene"},
		{"lpt9.csv", "scene"},
		{std::string("COM") + "\xC2\xB9.txt", "scene"},
		{std::string("lpt") + "\xC2\xB2", "scene"},
		{std::string("COM") + "\xB3", "scene"},
		{"bad<name", "scene"},
		{"bad>name", "scene"},
		{"bad:name", "scene"},
		{"bad\"name", "scene"},
		{"bad/name", "scene"},
		{"bad\\name", "scene"},
		{"bad|name", "scene"},
		{"bad?name", "scene"},
		{"bad*name", "scene"},
		{std::string("control") + '\x01', "scene"},
		{"trailing ", "scene"},
		{"trailing.", "scene"},
	};
	for (const auto& outputName : outputNameCases) {
		QTest::newRow(printable(outputName.first).c_str())
			<< QByteArray::fromStdString(outputName.first) << QByteArray::fromStdString(outputName.second);
	}
}

void TestSceneValidator::makesSafeOutputComponent() {
	QFETCH(QByteArray, name);
	QFETCH(QByteArray, expected);
	const std::filesystem::path outputRoot = "scene-output-root";
	const std::string component = sceneOutputDirectoryComponent(name.toStdString());
	const std::filesystem::path joined = outputRoot / component;
	// The scene output component is safe for the name.
	QCOMPARE(component, expected.toStdString());
	QVERIFY(joined.lexically_normal().parent_path() == outputRoot);
}

void TestSceneValidator::rejectsUnsafeSceneNames() {
	const SceneModel clean = completeScene();
	SceneModel unsafeSceneName = clean;
	unsafeSceneName.name = "../outside";
	QVERIFY2(hasCodeAndPath(validateRunnableScene(unsafeSceneName), "scene.name.path", "name"),
		"runnable validation rejects unsafe scene output names");
	SceneModel reservedSceneName = clean;
	reservedSceneName.name = "CON.txt";
	QVERIFY2(hasCodeAndPath(validateRunnableScene(reservedSceneName), "scene.name.path", "name"),
		"runnable validation rejects reserved scene output names");
}

void TestSceneValidator::limitsTimetableStops() {
	SceneModel stopLimit = completeScene();
	while (stopLimit.services[0].stops.size() < static_cast<std::size_t>(RuntimeLimits::kMaxTimetableStops))
		stopLimit.services[0].stops.push_back(stopLimit.services[0].stops.back());
	QVERIFY2(!hasCode(validateRunnableScene(stopLimit), "scene.capacity.runtime"),
		"runnable validation accepts the exact timetable stop limit");
	SceneModel tooManyStops = stopLimit;
	tooManyStops.services[0].stops.push_back(tooManyStops.services[0].stops.back());
	QVERIFY2(hasCodeAndPath(validateRunnableScene(tooManyStops), "scene.capacity.runtime",
				 "services[service-1].stops"),
		"runnable validation rejects one stop above the limit");
}

void TestSceneValidator::limitsExpandedTrains() {
	const SceneModel clean = completeScene();
	SceneModel trainLimit = clean;
	trainLimit.services[0].hasRepeat = true;
	trainLimit.services[0].headwaySeconds = 1.0;
	trainLimit.services[0].hasRepeatCount = true;
	trainLimit.services[0].repeatCount = RuntimeLimits::kMaxExpandedTrains;
	QVERIFY2(!hasCode(validateRunnableScene(trainLimit), "scene.capacity.runtime"),
		"runnable validation accepts the exact expanded train limit");
	SceneModel tooManyTrains = trainLimit;
	tooManyTrains.services[0].repeatCount = RuntimeLimits::kMaxExpandedTrains + 1;
	QVERIFY2(hasCodeAndPath(validateRunnableScene(tooManyTrains), "scene.capacity.runtime", "services"),
		"runnable validation rejects one expanded train above the limit");
	SceneModel horizonTrainLimit = clean;
	horizonTrainLimit.services[0].hasRepeat = true;
	horizonTrainLimit.services[0].headwaySeconds = 1.0;
	QVERIFY2(hasCode(validateRunnableScene(horizonTrainLimit), "scene.capacity.runtime"),
		"runnable validation rejects saved-horizon train expansion above the limit");
	QVERIFY2(!hasCode(validateRunnableScene(horizonTrainLimit, {}, std::optional<double>(1.0)),
				 "scene.capacity.runtime"),
		"runnable validation uses an effective duration override for train capacity");
	const SceneRunSelection sparseSelection{{tooManyTrains.services[0].id,
		RuntimeLimits::kMaxExpandedTrains + 1}};
	QVERIFY2(!hasCode(validateRunnableScene(tooManyTrains, sparseSelection), "scene.capacity.runtime"),
		"runnable validation applies train capacity to a sparse selected run");
	const SceneRunSelection invalidSelections{{tooManyTrains.services[0].id,
												  RuntimeLimits::kMaxExpandedTrains + 2},
		{"service-missing", 1}};
	QVERIFY2(!hasCode(validateRunnableScene(tooManyTrains, invalidSelections), "scene.capacity.runtime"),
		"invalid selected rows do not fall back to all-scene train capacity");
}

void TestSceneValidator::rejectsNonFinitePlatformLength() {
	SceneModel invalidPlatformLength = completeScene();
	invalidPlatformLength.stations[0].platforms[0].hasLength = true;
	invalidPlatformLength.stations[0].platforms[0].lengthM = std::numeric_limits<double>::infinity();
	QVERIFY2(hasCodeAndPath(validateScene(invalidPlatformLength), "scene.platform.length.invalid",
				 "stations[0].platforms[0].length_m"),
		"explicit non-finite platform length is rejected at its field");
}

void TestSceneValidator::rejectsNonPositivePlatformWidth() {
	SceneModel invalidPlatformWidth = completeScene();
	invalidPlatformWidth.stations[0].platforms[0].hasWidth = true;
	invalidPlatformWidth.stations[0].platforms[0].widthM = 0.0;
	QVERIFY2(hasCodeAndPath(validateScene(invalidPlatformWidth), "scene.platform.width.invalid",
				 "stations[0].platforms[0].width_m"),
		"explicit non-positive platform width is rejected at its field");
}

void TestSceneValidator::needsUsablePlatformCapacity() {
	const SceneModel clean = completeScene();
	SceneModel invalidPlatformCapacity = clean;
	invalidPlatformCapacity.stations[0].platforms[0].hasLength = true;
	invalidPlatformCapacity.stations[0].platforms[0].lengthM = 0.01;
	invalidPlatformCapacity.stations[0].platforms[0].hasWidth = true;
	invalidPlatformCapacity.stations[0].platforms[0].widthM = 0.01;
	QVERIFY2(hasCodeAndPath(validateScene(invalidPlatformCapacity), "scene.platform.capacity.invalid",
				 "stations[0].platforms[0]"),
		"platform geometry must produce a usable integer capacity");
	QVERIFY2(!hasCode(validateScene(clean), "scene.platform.capacity.invalid"),
		"a platform without a stated size has a usable capacity");
}

void TestSceneValidator::platformHoldsAtLeastOnePassenger() {
	SceneModel smallPlatform = completeScene();
	smallPlatform.stations[0].platforms[0].hasLength = true;
	smallPlatform.stations[0].platforms[0].lengthM = 1.0;
	smallPlatform.stations[0].platforms[0].hasWidth = true;
	smallPlatform.stations[0].platforms[0].widthM = 2.5;
	QVERIFY2(hasCode(validateScene(smallPlatform), "scene.platform.capacity.invalid"),
		"a platform that holds less than one passenger is rejected");
	smallPlatform.stations[0].platforms[0].widthM = 2.6;
	QVERIFY2(!hasCode(validateScene(smallPlatform), "scene.platform.capacity.invalid"),
		"a platform that holds one passenger is accepted");
}

void TestSceneValidator::defaultPlatformHoldsAbout99Passengers() {
	// The default platform size holds about 99 passengers.
	QVERIFY(std::abs(scenePlatformCapacity(kDefaultPlatformLengthM, kDefaultPlatformWidthM) - 99.47) < 0.01);
	QCOMPARE_EQ(ScenePlatform().lengthM, kDefaultPlatformLengthM);
	QCOMPARE_EQ(ScenePlatform().widthM, kDefaultPlatformWidthM);
}

void TestSceneValidator::rejectsNonFinitePassengerWindow() {
	SceneModel nonFinitePassengerWindow = completeScene();
	nonFinitePassengerWindow.passengers[0].journeys[0].plannedArrivalEndSeconds =
		std::numeric_limits<double>::quiet_NaN();
	QVERIFY2(hasCodeAndPath(validateScene(nonFinitePassengerWindow), "scene.passenger.window",
				 "passengers[0].journeys[0].planned_arrival"),
		"non-finite passenger windows are rejected");
}

void TestSceneValidator::legDestinationIsStopOfService() {
	SceneModel passengerMissingStop = completeScene();
	passengerMissingStop.services[0].stops.pop_back();
	const auto passengerStopDiagnostics = validateScene(passengerMissingStop);
	QVERIFY2(hasCodeAndPath(passengerStopDiagnostics, "scene.passenger.leg.stop",
				 "passengers[0].journeys[0].legs[0].destination"),
		"passenger leg destination must be a stop of its service");
}

void TestSceneValidator::rejectsReversePassengerLeg() {
	SceneModel reversePassengerLeg = completeScene();
	reversePassengerLeg.passengers[0].journeys[0].originStationId = "station-2";
	reversePassengerLeg.passengers[0].journeys[0].destinationStationId = "station-1";
	reversePassengerLeg.passengers[0].journeys[0].legs[0].originStationId = "station-2";
	reversePassengerLeg.passengers[0].journeys[0].legs[0].destinationStationId = "station-1";
	QVERIFY2(hasCodeAndPath(validateScene(reversePassengerLeg), "scene.passenger.leg.order",
				 "passengers[0].journeys[0].legs[0].destination"),
		"passenger leg rejects a reverse ordered service pair");
	SceneModel legacyReversePassengerLeg = reversePassengerLeg;
	legacyReversePassengerLeg.importReport.push_back({"legacy_root", ""});
	const auto legacyReverseDiagnostics = validateScene(legacyReversePassengerLeg);
	// Legacy reverse passenger legs remain loadable with an actionable warning.
	QVERIFY(hasCodeAndSeverity(legacyReverseDiagnostics, "scene.passenger.leg.order", SceneSeverity::Warning));
	QVERIFY(!hasCodeAndPath(legacyReverseDiagnostics, "scene.passenger.leg.order",
		"passengers[0].journeys[0].legs[0].destination"));
}

void TestSceneValidator::resolvesRepeatedServiceStations() {
	SceneModel repeatedPassengerStops = completeScene();
	repeatedPassengerStops.services[0].stops = {
		{"station-2", "platform-2", true, true, 100.0, 110.0, 0.0},
		{"station-1", "platform-1", true, true, 120.0, 130.0, 0.0},
		{"station-2", "platform-2", true, true, 140.0, 150.0, 0.0}};
	SceneServiceStopPair repeatedPair;
	// Repeated service stations resolve to an ordered stop pair.
	QVERIFY(resolveScenePassengerLegStops(repeatedPassengerStops.services[0],
		repeatedPassengerStops.passengers[0].journeys[0].legs[0], repeatedPair));
	QCOMPARE(repeatedPair.originIndex, std::size_t(1));
	QCOMPARE(repeatedPair.destinationIndex, std::size_t(2));
	QVERIFY(!hasCode(validateScene(repeatedPassengerStops), "scene.passenger.leg.order"));
}

void TestSceneValidator::derivesSectionInventory() {
	const SceneSectionInventory inventory = buildSceneSectionInventory(completeScene());
	// Section inventory derives canonical base and connection section IDs.
	QCOMPARE(inventory.sections.size(), std::size_t(4));
	QCOMPARE(inventory.sections[0].id, "@block-1@");
	QCOMPARE(inventory.sections[1].id, "@block-2@");
	QCOMPARE(inventory.sections[2].id, "@block-1@-0.000000/@block-1@-1.000000");
	QCOMPARE(inventory.sections[3].id, "@block-1@-0.000000/@block-2@-1.000000");
	// Section resolver accepts base aliases but rejects unknown compound tokens.
	QVERIFY(inventory.resolve("block-1") != nullptr);
	QCOMPARE(inventory.resolve("block-1")->id, "@block-1@");
	QVERIFY(inventory.resolve("@block-1@-0.000000/@block-2@-1.000000") != nullptr);
	QVERIFY(inventory.resolve("@block-1@-10/@block-2@-20") == nullptr);
}

void TestSceneValidator::doesNotResolveDuplicateBaseIds() {
	SceneModel duplicateSection = completeScene();
	duplicateSection.blocks.push_back({"block-1", "track-1", 0.5});
	const SceneSectionInventory duplicateInventory = buildSceneSectionInventory(duplicateSection);
	// Duplicate base IDs are not resolved ambiguously.
	QVERIFY(duplicateInventory.ambiguous("block-1"));
	QVERIFY(duplicateInventory.resolve("block-1") == nullptr);
}

void TestSceneValidator::rejectsDisconnectedRouteOrder() {
	SceneModel disconnectedRoute = completeScene();
	disconnectedRoute.routes[0].blocks = {"block-1", "block-1"};
	QVERIFY2(hasCode(validateScene(disconnectedRoute), "scene.route.disconnected"),
		"authored disconnected route order is rejected");
}

void TestSceneValidator::resolvesStopsOnReversedRoute() {
	SceneModel reverseRoute = completeScene();
	reverseRoute.routes[0].blocks = {"block-2", "block-1"};
	QVERIFY2(hasCode(validateScene(reverseRoute), "scene.ref.stop.order"),
		"stop order follows the route direction rather than unordered membership");
	std::reverse(reverseRoute.services[0].stops.begin(), reverseRoute.services[0].stops.end());
	const auto reverseTraversal = buildSceneRouteTraversal(reverseRoute, reverseRoute.routes[0]);
	// Reversed stops resolve on a reversed route.
	QVERIFY(reverseTraversal.resolved);
	QCOMPARE(reverseTraversal.direction, -1);
	QCOMPARE(reverseTraversal.visits.front().stationId, "station-2");
	QVERIFY(!hasCode(validateScene(reverseRoute), "scene.ref.stop.order"));
	QVERIFY2(!hasCode(validateScene(reverseRoute), "scene.route.disconnected"),
		"coherent reverse route order remains valid");
	const auto unmarkedReverseDiagnostics = validateScene(reverseRoute);
	QVERIFY2(!hasCode(unmarkedReverseDiagnostics, "scene.route.direction"),
		"an unmarked reverse route has no direction error");
	reverseRoute.routes[0].reversed = true;
	const auto markedReverseDiagnostics = validateScene(reverseRoute);
	// A marked reverse route has the same diagnostics as an unmarked reverse route.
	QVERIFY(!hasCode(markedReverseDiagnostics, "scene.route.direction"));
	QCOMPARE(markedReverseDiagnostics.size(), unmarkedReverseDiagnostics.size());
	QVERIFY(std::equal(markedReverseDiagnostics.begin(), markedReverseDiagnostics.end(),
		unmarkedReverseDiagnostics.begin(), [](const SceneDiagnostic& left, const SceneDiagnostic& right) {
			return toDisplayText(left) == toDisplayText(right);
		}));
}

void TestSceneValidator::rejectsDirectionChange() {
	SceneModel directionChange = completeScene();
	directionChange.routes[0].blocks = {"block-1", "block-2", "block-1"};
	const auto directionErrors = findAll(validateScene(directionChange), "scene.route.direction");
	// A route cannot change direction between connected sections, and the message gives the labels of the sections.
	QCOMPARE(directionErrors.size(), std::size_t(1));
	QCOMPARE(directionErrors[0].message, "Route changes direction between block-1 (track-1 0.000 to 1.000 km) -> block-2 "
										 "(track-1 1.000 to 2.000 km) and block-2 (track-1 1.000 to 2.000 km) -> block-1 "
										 "(track-1 0.000 to 1.000 km)");
	QCOMPARE(directionErrors[0].relatedId, "forward: block-1 -> block-2; reverse: block-2 -> block-1");
	directionChange.routes[0].reversed = true;
	const auto changeErrors = findAll(validateScene(directionChange), "scene.route.direction");
	// A marked direction-changing route gets no second direction error.
	QCOMPARE(changeErrors.size(), std::size_t(1));
	QCOMPARE(changeErrors[0].itemId, "route-1");
	QCOMPARE(changeErrors[0].path, "routes[0].blocks");
}

void TestSceneValidator::rejectsMarkedForwardRoute_data() {
	QTest::addColumn<bool>("runnable");
	QTest::newRow("validateScene") << false;
	QTest::newRow("validateRunnableScene") << true;
}

void TestSceneValidator::rejectsMarkedForwardRoute() {
	QFETCH(bool, runnable);
	SceneModel markedForward = completeScene();
	markedForward.routes[0].reversed = true;
	const auto diagnostics = runnable ? validateRunnableScene(markedForward) : validateScene(markedForward);
	const auto errors = findAll(diagnostics, "scene.route.direction");
	// Plain and runnable validation reject a marked forward route with its id and flag path.
	QCOMPARE(errors.size(), std::size_t(1));
	QVERIFY(errors[0].severity == SceneSeverity::Error);
	QCOMPARE(errors[0].itemId, "route-1");
	QCOMPARE(errors[0].path, "routes[0].reversed");
	QVERIFY(contains(errors[0].message, "route-1"));
	QCOMPARE(errors[0].file, "signalling.json");
	QCOMPARE(errors[0].itemType, "route");
	QCOMPARE(errors[0].suggestedFix, "List the sections in reverse order or remove reversed");
}

void TestSceneValidator::rejectsMarkedOneSectionRoute() {
	SceneModel markedShort = completeScene();
	markedShort.routes.push_back({"route-short", {"block-1"}, false, "", true});
	const auto shortErrors = findAll(validateScene(markedShort), "scene.route.direction");
	// Only the marked one-section route is rejected at its own index.
	QCOMPARE(shortErrors.size(), std::size_t(1));
	QVERIFY(shortErrors[0].severity == SceneSeverity::Error);
	QCOMPARE(shortErrors[0].itemId, "route-short");
	QCOMPARE(shortErrors[0].path, "routes[1].reversed");
}

void TestSceneValidator::skipsDirectionOfUnresolvedMarkedRoute() {
	SceneModel markedUnresolved = completeScene();
	markedUnresolved.routes[0].blocks = {"block-1", "missing-block"};
	markedUnresolved.routes[0].reversed = true;
	const auto unresolvedDiagnostics = validateScene(markedUnresolved);
	// A marked route with an unresolved section gets no direction error.
	QVERIFY(hasCode(unresolvedDiagnostics, "scene.ref.unresolved"));
	QVERIFY(!hasCode(unresolvedDiagnostics, "scene.route.direction"));
}

void TestSceneValidator::joinsConnectionDerivedSections() {
	const SceneModel switchTopology = switchTopologyScene();
	const SceneSectionInventory switchInventory = buildSceneSectionInventory(switchTopology);
	const std::string aToB = connectionSection(switchInventory, "a-to-b");
	const std::string bToC = connectionSection(switchInventory, "b-to-c");
	SceneModel switchChain = switchTopology;
	switchChain.routes.push_back({"switch-route", {aToB, bToC}, false, "", false});
	const auto switchChainDiagnostics = validateScene(switchChain);
	switchChain.stations = {{"A", "A", false, 0.0, {{"A.p", {"a.0"}}}},
		{"B-entry", "B entry", false, 0.0, {{"B.in", {"b.0"}}}},
		{"B-exit", "B exit", false, 0.0, {{"B.out", {"b.1"}}}},
		{"C", "C", false, 0.0, {{"C.p", {"c.1"}}}}};
	const auto switchTraversal = buildSceneRouteTraversal(switchChain, switchChain.routes[0]);
	// Overlapping connection sections retain only their clipped platform anchors.
	QVERIFY(switchTraversal.resolved);
	QCOMPARE(switchTraversal.visits.size(), std::size_t(4));
	QCOMPARE(switchTraversal.visits[1].nodeId, "b.0");
	QCOMPARE(switchTraversal.visits[1].sectionIndex, std::size_t(0));
	QCOMPARE(switchTraversal.visits[2].nodeId, "b.1");
	QCOMPARE(switchTraversal.visits[2].sectionIndex, std::size_t(1));
	// Connection-derived sections join through the shared exit and entry block.
	QVERIFY(!aToB.empty());
	QVERIFY(!bToC.empty());
	QVERIFY(!hasCode(switchChainDiagnostics, "scene.route.disconnected"));
	QVERIFY(!hasCode(switchChainDiagnostics, "scene.route.direction"));
}

void TestSceneValidator::sharesLegacySectionTokenMatching() {
	// Legacy section token matching is shared and touching intervals do not overlap.
	QVERIFY(sceneSectionsOverlap("@A@same", 0.0, 2.0, "@B@same", 1.0, 3.0));
	QVERIFY(!sceneSectionsOverlap("@A@", 0.0, 1.0, "@A@", 1.0, 2.0));
}

void TestSceneValidator::rejectsWrongSwitchBranch() {
	const SceneModel switchTopology = switchTopologyScene();
	const SceneSectionInventory switchInventory = buildSceneSectionInventory(switchTopology);
	const std::string aToB = connectionSection(switchInventory, "a-to-b");
	const std::string aToC = connectionSection(switchInventory, "a-to-c");
	SceneModel forkedSwitchChain = switchTopology;
	forkedSwitchChain.routes.push_back({"forked-route", {aToB, aToC}, false, "", false});
	// Overlapping switch sections on the wrong branch are rejected.
	QVERIFY(!aToC.empty());
	QVERIFY(hasCode(validateScene(forkedSwitchChain), "scene.route.disconnected"));
}

void TestSceneValidator::rejectsLegacyWrongBranchFork() {
	SceneModel reversedFork = switchTopologyScene();
	reversedFork.nodes[0].xKm = 100.0;
	reversedFork.nodes[1].xKm = 101.0;
	reversedFork.nodes[2].xKm = 0.0;
	reversedFork.nodes[3].xKm = 1.0;
	reversedFork.nodes[4].xKm = 2.0;
	reversedFork.nodes[5].xKm = 3.0;
	const SceneSectionInventory reversedInventory = buildSceneSectionInventory(reversedFork);
	std::string bToA;
	std::string cToA;
	for (const auto& section : reversedInventory.sections) {
		if (section.sourceConnectionId == "a-to-b")
			bToA = section.id;
		else if (section.sourceConnectionId == "a-to-c")
			cToA = section.id;
	}
	reversedFork.importReport.push_back({"legacy_root", ""});
	reversedFork.routes.push_back({"reversed-fork", {bToA, cToA}, false, "", false});
	// Legacy compatibility cannot turn a wrong-branch switch fork into a regional jump.
	QVERIFY(!bToA.empty());
	QVERIFY(!cToA.empty());
	QVERIFY(hasCode(validateScene(reversedFork), "scene.route.disconnected"));
}

void TestSceneValidator::rejectsLegacyDerivedReversal() {
	const SceneModel switchTopology = switchTopologyScene();
	const SceneSectionInventory switchInventory = buildSceneSectionInventory(switchTopology);
	const std::string aToB = connectionSection(switchInventory, "a-to-b");
	const std::string bToC = connectionSection(switchInventory, "b-to-c");
	SceneModel switchDirectionChange = switchTopology;
	switchDirectionChange.importReport.push_back({"legacy_root", ""});
	switchDirectionChange.routes.push_back(
		{"switch-u-turn", {aToB, bToC, aToB}, false, "", false});
	QVERIFY2(hasCode(validateScene(switchDirectionChange), "scene.route.direction"),
		"legacy provenance cannot hide a connection-derived route reversal");
}

void TestSceneValidator::rejectsLegacyRegionalUTurn() {
	SceneModel regionalSwitchDirectionChange = switchTopologyScene();
	regionalSwitchDirectionChange.blocks = {{"a.block", "switch-a", 1.0},
		{"b.left", "switch-b", 1.0}, {"b.right", "switch-b", 1.0},
		{"c.block", "switch-c", 1.0}};
	std::string regionalAToB;
	std::string regionalBToC;
	for (const auto& section : buildSceneSectionInventory(regionalSwitchDirectionChange).sections) {
		if (section.sourceConnectionId == "a-to-b")
			regionalAToB = section.id;
		else if (section.sourceConnectionId == "b-to-c")
			regionalBToC = section.id;
	}
	regionalSwitchDirectionChange.importReport.push_back({"legacy_root", ""});
	regionalSwitchDirectionChange.routes.push_back(
		{"regional-switch-u-turn", {regionalAToB, regionalBToC, regionalAToB}, false, "", false});
	// Legacy regional bridges cannot suppress both directions of a derived U-turn.
	QVERIFY(!regionalAToB.empty());
	QVERIFY(!regionalBToC.empty());
	QVERIFY(hasCode(validateScene(regionalSwitchDirectionChange), "scene.route.direction"));
	SceneModel mixedRegionalDirectionChange = regionalSwitchDirectionChange;
	mixedRegionalDirectionChange.routes = {{"mixed-regional-switch-u-turn",
		{"b.left", regionalBToC, regionalAToB}, false, "", false}};
	QVERIFY2(hasCode(validateScene(mixedRegionalDirectionChange), "scene.route.direction"),
		"an ordinary transition cannot hide an opposing legacy derived-section direction");
}

void TestSceneValidator::reportsCrossRegionJumps() {
	SceneModel regionJump = completeScene();
	regionJump.tracks.push_back({"region-track"});
	regionJump.nodes.push_back({"region-node-1", "region-track", 100.0, 0.0});
	regionJump.nodes.push_back({"region-node-2", "region-track", 101.0, 0.0});
	regionJump.arcs.push_back({"region-arc", "region-track", "region-node-1", "region-node-2",
		0.0, 0.0, 20.0});
	regionJump.blocks.push_back({"region-block", "region-track", 1.0});
	regionJump.routes.push_back({"region-route", {"block-2", "region-block"}, false, "", false});
	regionJump.importReport.push_back({"legacy_root", ""});
	const auto regionJumpDiagnostics = validateScene(regionJump);
	// Legacy cross-region coordinate discontinuities remain visible and compatible.
	QVERIFY(hasCode(regionJumpDiagnostics, "scene.route.region_jump"));
	QVERIFY(!hasCode(regionJumpDiagnostics, "scene.route.disconnected"));
	SceneModel segmentedRegionJump = regionJump;
	segmentedRegionJump.blocks.back().lengthKm = 0.5;
	segmentedRegionJump.blocks.push_back({"region-block-2", "region-track", 0.5});
	segmentedRegionJump.routes = {{"segmented-region-route",
		{"block-1", "block-2", "region-block-2", "region-block"}, false, "", false}};
	const auto segmentedRegionDiagnostics = validateScene(segmentedRegionJump);
	// Legacy route direction is checked independently on each connected regional segment.
	QVERIFY(hasCode(segmentedRegionDiagnostics, "scene.route.region_jump"));
	QVERIFY(!hasCode(segmentedRegionDiagnostics, "scene.route.direction"));
	regionJump.importReport.clear();
	QVERIFY2(hasCode(validateScene(regionJump), "scene.route.disconnected"),
		"new canonical scenes reject undeclared cross-region route jumps");
}

void TestSceneValidator::reportsUnboundSignal() {
	SceneModel unboundSignal = completeScene();
	unboundSignal.signals[0].protectedSection.clear();
	QVERIFY2(hasCode(validateScene(unboundSignal), "scene.signal.binding.missing"),
		"unbound signals produce an actionable binding diagnostic");
}

void TestSceneValidator::rejectsMalformedSignalBinding() {
	SceneModel invalidSignalBinding = completeScene();
	invalidSignalBinding.signals[0].protectedSection = "@block-1@-10/@block-2@-20";
	QVERIFY2(hasCode(validateScene(invalidSignalBinding), "scene.signal.binding.unresolved"),
		"malformed signal section bindings are rejected");
}

void TestSceneValidator::acceptsBlockSignalFailureTarget() {
	SceneModel directBlockIncident = completeScene();
	directBlockIncident.scenarios[0].incidents[0].target = "block-2";
	QVERIFY2(validateScene(directBlockIncident).empty(),
		"direct base-block signal-failure targets remain compatible");
}

void TestSceneValidator::rejectsAmbiguousSignalFailureTarget() {
	SceneModel ambiguousSignalTarget = completeScene();
	ambiguousSignalTarget.signals[0].id = "block-1";
	ambiguousSignalTarget.scenarios[0].incidents[0].target = "block-1";
	QVERIFY2(hasCode(validateScene(ambiguousSignalTarget), "scene.ref.ambiguous"),
		"signal failures reject an ID that identifies both a signal and section");
}

void TestSceneValidator::validatesNetworkAndTrackAreas() {
	SceneModel validAreas = completeScene();
	validAreas.signallingAreas = {
		{"network-area", 0.0, 2.0, 2, {}},
		{"track-area", 0.25, 1.75, 4, "track-1"},
	};
	QVERIFY2(validateScene(validAreas).empty(),
		"network-wide and track-scoped signalling areas validate");
}

void TestSceneValidator::rejectsDuplicateAreaIds() {
	SceneModel duplicateArea = completeScene();
	duplicateArea.signallingAreas = {{"area", 0.0, 1.0, 2, {}}, {"area", 1.0, 2.0, 2, {}}};
	QVERIFY2(hasCode(validateScene(duplicateArea), "scene.id.duplicate"),
		"signalling area IDs must be unique");
}

void TestSceneValidator::rejectsAreaRangeThatDoesNotIncrease() {
	SceneModel invalidAreaRange = completeScene();
	invalidAreaRange.signallingAreas = {{"area", 1.0, 1.0, 2, {}}};
	QVERIFY2(hasCode(validateScene(invalidAreaRange), "scene.signalling_area.range"),
		"signalling area ranges must increase");
	auto rangeErrors = findAll(validateScene(invalidAreaRange), "scene.signalling_area.range");
	const std::string rangeMessage = "Signalling area area starts at 1.000000 km and ends at 1.000000 km, but its start must be below its end "
									 "(blocks of the network cover 0.000000 to 2.000000 km)";
	// The range error names the area, both values and the extent of the network.
	QCOMPARE(rangeErrors.size(), std::size_t(1));
	QCOMPARE(rangeErrors[0].message, rangeMessage);
	invalidAreaRange.signallingAreas[0].startKm = std::numeric_limits<double>::quiet_NaN();
	QVERIFY2(hasCode(validateScene(invalidAreaRange), "scene.signalling_area.range"),
		"signalling area coordinates must be finite");
}

void TestSceneValidator::rangeErrorNamesTrackWithoutBlocks() {
	SceneModel trackWithoutBlocks = completeScene();
	trackWithoutBlocks.tracks.push_back({"track-2"});
	trackWithoutBlocks.signallingAreas = {{"other", 1.0, 0.5, 2, "track-2"}};
	const auto rangeErrors = findAll(validateScene(trackWithoutBlocks), "scene.signalling_area.range");
	// The range error of an area on a track without blocks says so.
	QCOMPARE(rangeErrors.size(), std::size_t(1));
	QVERIFY(contains(rangeErrors[0].message, "(track track-2 has no blocks)"));
}

void TestSceneValidator::rangeErrorGivesExtentOfTrack() {
	SceneModel invalidTrackRange = completeScene();
	invalidTrackRange.signallingAreas = {{"scoped", 2.5, 1.0, 2, "track-1"}};
	const auto rangeErrors = findAll(validateScene(invalidTrackRange), "scene.signalling_area.range");
	// The range error of a track-scoped area gives the extent of that track.
	QCOMPARE(rangeErrors.size(), std::size_t(1));
	QVERIFY(contains(rangeErrors[0].message, "starts at 2.500000 km and ends at 1.000000 km"));
	QVERIFY(contains(rangeErrors[0].message, "(blocks of track track-1 cover 0.000000 to 2.000000 km)"));
}

void TestSceneValidator::rejectsAreaLevelAboveFive() {
	SceneModel invalidAreaLevel = completeScene();
	invalidAreaLevel.signallingAreas = {{"area", 0.0, 1.0, 6, {}}};
	QVERIFY2(hasCode(validateScene(invalidAreaLevel), "scene.signalling_area.level"),
		"signalling area levels must be between zero and five");
	const auto levelErrors = findAll(validateScene(invalidAreaLevel), "scene.signalling_area.level");
	// The level error names the area and the offending level.
	QCOMPARE(levelErrors.size(), std::size_t(1));
	QCOMPARE(levelErrors[0].message, "Signalling area area has level 6, which is not a signalling system");
}

void TestSceneValidator::resolvesAreaTrack() {
	SceneModel unknownAreaTrack = completeScene();
	unknownAreaTrack.signallingAreas = {{"area", 0.0, 1.0, 2, "missing-track"}};
	QVERIFY2(hasCodeAndPath(validateScene(unknownAreaTrack), "scene.ref.unresolved",
				 "signalling_areas[0].track"),
		"signalling area track references must resolve");
	const auto unknownTrackDiagnostics = validateScene(unknownAreaTrack);
	const auto unknownTrackErrors = findAll(unknownTrackDiagnostics, "scene.ref.unresolved");
	const std::string unknownTrackMessage = "Signalling area area refers to unknown track missing-track. Tracks of the network: track-1. "
											"Blocks of the network cover 0.000000 to 2.000000 km";
	// The unknown track error names the area, the track, the known tracks and the extent of the network.
	QCOMPARE(unknownTrackErrors.size(), std::size_t(1));
	QCOMPARE(unknownTrackErrors[0].message, unknownTrackMessage);
}

void TestSceneValidator::allowsOverlapWithoutSharedSection() {
	SceneModel overlapWithoutSharedSection = completeScene();
	overlapWithoutSharedSection.signallingAreas = {
		{"first", 0.0, 1.5, 2, {}}, {"second", 1.0, 2.0, 3, {}}};
	QVERIFY2(!hasCode(validateRunnableScene(overlapWithoutSharedSection),
				 "scene.signalling_area.conflict"),
		"coordinate overlap is allowed when no complete section receives both levels");
}

void TestSceneValidator::rejectsConflictingNetworkAreas() {
	SceneModel conflictingNetworkAreas = completeScene();
	conflictingNetworkAreas.signallingAreas = {
		{"first", 0.0, 2.0, 2, {}}, {"second", 0.0, 2.0, 3, {}}};
	QVERIFY2(hasCode(validateRunnableScene(conflictingNetworkAreas),
				 "scene.signalling_area.conflict"),
		"network-wide areas cannot assign different levels to one runtime section");
}

void TestSceneValidator::rejectsConflictingTrackAreas() {
	SceneModel conflictingTrackAreas = completeScene();
	conflictingTrackAreas.signallingAreas = {
		{"first", 0.0, 2.0, 2, "track-1"}, {"second", 0.0, 2.0, 3, "track-1"}};
	QVERIFY2(hasCode(validateRunnableScene(conflictingTrackAreas),
				 "scene.signalling_area.conflict"),
		"same-track areas cannot assign different levels to one runtime section");
}

void TestSceneValidator::rejectsConflictOnDerivedSection() {
	const SceneModel conflictingDerivedAreas = conflictingDerivedAreasScene();
	QVERIFY2(hasCode(validateRunnableScene(conflictingDerivedAreas),
				 "scene.signalling_area.conflict"),
		"different track scopes cannot conflict on one derived switch section");
}

// The extent of the blocks of the network and of one track. The connection derives a section of track-1
// that reaches into the blocks of track-2, so a derived section would stretch the extent of track-1.
void TestSceneValidator::measuresBlockExtent() {
	const SceneSectionInventory extentInventory = buildSceneSectionInventory(conflictingDerivedAreasScene());
	bool derivedReachesPastTrack = false;
	for (const SceneSectionDescriptor& section : extentInventory.sections)
		derivedReachesPastTrack |= section.connectionDerived && section.firstTrackId == "track-1" && section.endKm > 2.0;
	const SceneBlockExtent networkExtent = sceneBlockExtent(extentInventory, "");
	// The extent of the blocks of the network spans all tracks.
	QVERIFY(networkExtent.found);
	QCOMPARE_EQ(networkExtent.startKm, 0.0);
	QCOMPARE_EQ(networkExtent.endKm, 4.0);
	const SceneBlockExtent firstTrackExtent = sceneBlockExtent(extentInventory, "track-1");
	// The extent of the blocks of a track leaves out the sections that a connection derives.
	QVERIFY(derivedReachesPastTrack);
	QVERIFY(firstTrackExtent.found);
	QCOMPARE_EQ(firstTrackExtent.startKm, 0.0);
	QCOMPARE_EQ(firstTrackExtent.endKm, 2.0);
	const SceneBlockExtent secondTrackExtent = sceneBlockExtent(extentInventory, "track-2");
	// The extent of the blocks of another track is its own.
	QVERIFY(secondTrackExtent.found);
	QCOMPARE_EQ(secondTrackExtent.startKm, 3.0);
	QCOMPARE_EQ(secondTrackExtent.endKm, 4.0);
	const SceneBlockExtent unknownTrackExtent = sceneBlockExtent(extentInventory, "no-such-track");
	QVERIFY2(!unknownTrackExtent.found, "an unknown track has no block extent");
}

void TestSceneValidator::warnsAboutRouteSectionsWithoutArea() {
	const SceneModel clean = completeScene();
	const std::string levelMissing = "scene.signalling.level.missing";
	SceneModel noAreas = clean;
	noAreas.signallingAreas.clear();
	const auto noAreaDiagnostics = validateRunnableScene(noAreas);
	const SceneDiagnostic* noAreaWarning = findCode(noAreaDiagnostics, levelMissing);
	// Route sections without any signalling area produce one warning.
	if (noAreaWarning == nullptr)
		QFAIL("the warning is not reported");
	QVERIFY(noAreaWarning->severity == SceneSeverity::Warning);
	QCOMPARE(noAreaWarning->file, "signalling.json");
	QCOMPARE(noAreaWarning->path, "signalling_areas");
	QVERIFY(noAreaWarning->message.rfind("No signalling area is defined. 2 of 2 route sections", 0) == 0);
	QVERIFY(contains(noAreaWarning->message, "(track track-1)"));
	QVERIFY(contains(noAreaWarning->suggestedFix, "0.000000 to 2.000000 km"));
	// Missing signalling level is a runnable-only warning.
	QVERIFY(!hasErrors(noAreaDiagnostics));
	QVERIFY(!hasCode(validateScene(noAreas), levelMissing));
	QVERIFY2(!hasCode(validateRunnableScene(clean), levelMissing),
		"a network-wide area covering all route sections removes the warning");
}

void TestSceneValidator::partialAreaNamesUncoveredSections() {
	SceneModel partialArea = completeScene();
	partialArea.signallingAreas = {{"partial", 0.0, 1.5, 2, {}}};
	const auto partialDiagnostics = validateRunnableScene(partialArea);
	const SceneDiagnostic* partialWarning = findCode(partialDiagnostics, "scene.signalling.level.missing");
	// A partial area names only the route sections it does not contain.
	if (partialWarning == nullptr)
		QFAIL("the warning is not reported");
	QVERIFY(contains(partialWarning->message, "1 of 2 route sections has no signalling system and runs"));
	QVERIFY(contains(partialWarning->message, "block-2"));
	QVERIFY(!contains(partialWarning->message, "block-1"));
	QVERIFY(!contains(partialWarning->message, "No signalling area"));
	QVERIFY(contains(partialWarning->suggestedFix, "1.000000 to 2.000000 km"));
}

void TestSceneValidator::trackAreaCoversDerivedSection() {
	SceneModel scopedArea = conflictingDerivedAreasScene();
	const SceneSectionInventory scopedInventory = buildSceneSectionInventory(scopedArea);
	std::string derivedSectionId;
	for (const auto& section : scopedInventory.sections) {
		if (section.connectionDerived)
			derivedSectionId = section.id;
	}
	scopedArea.routes[0].blocks = {"block-1", "block-2", derivedSectionId, "block-3"};
	scopedArea.signallingAreas = {{"scoped", 0.0, 10.0, 2, "track-1"}};
	const auto scopedDiagnostics = validateRunnableScene(scopedArea);
	const SceneDiagnostic* scopedWarning = findCode(scopedDiagnostics, "scene.signalling.level.missing");
	// A track-scoped area covers its tracks and the derived section that touches them.
	QVERIFY(!derivedSectionId.empty());
	if (scopedWarning == nullptr)
		QFAIL("the warning is not reported");
	QVERIFY(contains(scopedWarning->message, "1 of 4 route sections"));
	QVERIFY(contains(scopedWarning->message, "block-3"));
	QVERIFY(contains(scopedWarning->message, "(track track-2)"));
	QVERIFY(!contains(scopedWarning->message, derivedSectionId));
}

void TestSceneValidator::skipsSectionsOnNoRoute() {
	SceneModel unusedSection = completeScene();
	unusedSection.routes[0].blocks = {"block-1"};
	unusedSection.signallingAreas.clear();
	const auto unusedDiagnostics = validateRunnableScene(unusedSection);
	const SceneDiagnostic* unusedWarning = findCode(unusedDiagnostics, "scene.signalling.level.missing");
	// Sections on no route are not reported.
	if (unusedWarning == nullptr)
		QFAIL("the warning is not reported");
	QVERIFY(contains(unusedWarning->message, "1 of 1 route sections"));
	QVERIFY(!contains(unusedWarning->message, "block-2"));
}

void TestSceneValidator::sceneWithoutRoutesHasNoLevelWarning() {
	SceneModel noRoutes = completeScene();
	noRoutes.routes.clear();
	noRoutes.signallingAreas.clear();
	QVERIFY2(!hasCode(validateRunnableScene(noRoutes), "scene.signalling.level.missing"),
		"a scene without routes has no route sections to report");
}

// The warning does not depend on the direction of the route and follows the builder at the area edges.
void TestSceneValidator::levelWarningIgnoresRouteDirection() {
	SceneModel reversedLevels = completeScene();
	reversedLevels.routes[0].blocks = {"block-2", "block-1"};
	// The test route runs in decreasing chainage.
	QCOMPARE(buildSceneRouteTraversal(reversedLevels, reversedLevels.routes[0]).direction, -1);
	reversedLevels.signallingAreas.clear();
	// A reversed route without areas names its sections in route order.
	QVERIFY(contains(levelMissingMessage(reversedLevels), "2 of 2 route sections have no signalling system"));
	QVERIFY(contains(levelMissingMessage(reversedLevels), "@block-2@ (track-1 1.000 to 2.000 km), @block-1@ (track-1 0.000 to 1.000 km)"));
	reversedLevels.signallingAreas = {{"partial", 0.0, 1.5, 2, {}}};
	const std::string reversedPartial = levelMissingMessage(reversedLevels);
	// A reversed route gets the same warning as the forward route.
	QVERIFY(contains(reversedPartial, "1 of 2 route sections has no signalling system"));
	QVERIFY(contains(reversedPartial, "@block-2@"));
	QVERIFY(!contains(reversedPartial, "@block-1@"));
	reversedLevels.signallingAreas = {{"all", 0.0, 2.0, 2, {}}};
	QVERIFY2(levelMissingMessage(reversedLevels).empty(), "a reversed route covered by an area has no warning");
}

void TestSceneValidator::levelWarningFollowsAreaEdges() {
	SceneModel edges = completeScene();
	edges.signallingAreas = {{"first", 0.0, 1.0, 2, {}}};
	std::string edgeMessage = levelMissingMessage(edges);
	// A section that ends exactly at the area end is covered.
	QVERIFY(contains(edgeMessage, "1 of 2 route sections"));
	QVERIFY(contains(edgeMessage, "@block-2@"));
	QVERIFY(!contains(edgeMessage, "@block-1@"));
	edges.signallingAreas = {{"first", 0.0, 1.0 - 5e-9, 2, {}}};
	QVERIFY2(contains(levelMissingMessage(edges), "1 of 2 route sections"),
		"an area end just inside the tolerance still covers the section");
	edges.signallingAreas = {{"first", 0.0, 1.0 - 1e-7, 2, {}}};
	QVERIFY2(contains(levelMissingMessage(edges), "2 of 2 route sections"),
		"an area end outside the tolerance does not cover the section");
	edges.signallingAreas = {{"second", 1.0 + 5e-9, 2.0, 2, {}}};
	edgeMessage = levelMissingMessage(edges);
	// An area start just inside the tolerance still covers the section.
	QVERIFY(contains(edgeMessage, "1 of 2 route sections"));
	QVERIFY(contains(edgeMessage, "@block-1@"));
	QVERIFY(!contains(edgeMessage, "@block-2@"));
	edges.signallingAreas = {{"second", 1.0 + 1e-7, 2.0, 2, {}}};
	QVERIFY2(contains(levelMissingMessage(edges), "2 of 2 route sections"),
		"an area start outside the tolerance does not cover the section");
	edges.signallingAreas = {{"first", 0.0, 1.0, 2, {}}, {"second", 1.0, 2.0, 3, {}}};
	// Adjacent areas that meet on a section edge cover both sections without a conflict.
	QVERIFY(levelMissingMessage(edges).empty());
	QVERIFY(!hasCode(validateRunnableScene(edges), "scene.signalling_area.conflict"));
	edges.signallingAreas = {{"first", 0.0, 1.5, 2, {}}, {"second", 1.5, 2.0, 3, {}}};
	edgeMessage = levelMissingMessage(edges);
	// A section that crosses the edge between two areas is reported.
	QVERIFY(contains(edgeMessage, "1 of 2 route sections"));
	QVERIFY(contains(edgeMessage, "@block-2@"));
	edges.signallingAreas.push_back({"bridge", 0.5, 2.0, 3, {}});
	QVERIFY2(levelMissingMessage(edges).empty(),
		"a third area that contains the straddling section removes the warning");
}

// Conflicts are reported once for each pair of areas, with the number of sections and where they lie.
void TestSceneValidator::reportsConflictOncePerPair() {
	SceneModel conflictingNetworkAreas = completeScene();
	conflictingNetworkAreas.signallingAreas = {
		{"first", 0.0, 2.0, 2, {}}, {"second", 0.0, 2.0, 3, {}}};
	const auto pairDiagnostics = findAll(validateRunnableScene(conflictingNetworkAreas), "scene.signalling_area.conflict");
	// Two areas that disagree about two sections give one conflict that names both areas, levels and the sections.
	QCOMPARE(pairDiagnostics.size(), std::size_t(1));
	QCOMPARE(pairDiagnostics[0].itemId, "second");
	QCOMPARE(pairDiagnostics[0].relatedId, "first");
	QCOMPARE(pairDiagnostics[0].path, "signalling_areas[1]");
	QCOMPARE(pairDiagnostics[0].message, "Signalling area first (2 ETCS Level 2 fixed block) and signalling area "
										 "second (3 ETCS Level 3 moving block) assign different levels to 4 sections, the first being "
										 "@block-1@ (track-1 0.000 to 1.000 km), between 0.000000 and 2.000000 km");
}

void TestSceneValidator::namesTheOneConflictingSection() {
	SceneModel singleConflict = completeScene();
	singleConflict.signallingAreas = {{"wide", 0.0, 2.0, 0, {}}, {"narrow", 1.0, 2.0, 5, {}}};
	const auto singleConflictDiagnostics = findAll(validateRunnableScene(singleConflict), "scene.signalling_area.conflict");
	const std::string singleConflictMessage = "Signalling area wide (0 ATB fixed block) and signalling area narrow "
											  "(5 BACC track circuits) assign different levels to 1 section: "
											  "@block-2@ (track-1 1.000 to 2.000 km), between 1.000000 and 2.000000 km";
	// A conflict about one section names that section and its range.
	QCOMPARE(singleConflictDiagnostics.size(), std::size_t(1));
	QCOMPARE(singleConflictDiagnostics[0].message, singleConflictMessage);
}

void TestSceneValidator::agreeingOrSeparateAreasDoNotConflict() {
	const std::string conflictCode = "scene.signalling_area.conflict";
	const SceneModel clean = completeScene();
	SceneModel overlapWithoutSharedSection = clean;
	overlapWithoutSharedSection.signallingAreas = {
		{"first", 0.0, 1.5, 2, {}}, {"second", 1.0, 2.0, 3, {}}};
	// Areas that agree or share no complete section give no conflict.
	QVERIFY(findAll(validateRunnableScene(clean), conflictCode).empty());
	QVERIFY(findAll(validateRunnableScene(overlapWithoutSharedSection), conflictCode).empty());
}

void TestSceneValidator::sameLevelOverlapDoesNotConflict() {
	SceneModel sameLevelOverlap = completeScene();
	sameLevelOverlap.signallingAreas = {{"a", 0.0, 2.0, 2, {}}, {"b", 0.0, 2.0, 2, "track-1"}, {"c", 1.0, 2.0, 2, {}}};
	QVERIFY2(findAll(validateRunnableScene(sameLevelOverlap), "scene.signalling_area.conflict").empty(),
		"overlapping areas with the same level give no conflict");
}

// An area edge inside a route section leaves that section out of the area.
void TestSceneValidator::warnsAboutAreaEndInsideSection() {
	const std::string splitCode = "scene.signalling_area.splits_section";
	SceneModel splitEnd = completeScene();
	splitEnd.signallingAreas = {{"head", 0.0, 1.5, 2, {}}};
	const auto splitEndDiagnostics = validateRunnableScene(splitEnd);
	const auto splitEndWarnings = findAll(splitEndDiagnostics, splitCode);
	// An area end inside a route section gives one warning that names the section, the edge and the section range.
	QCOMPARE(splitEndWarnings.size(), std::size_t(1));
	QVERIFY(splitEndWarnings[0].severity == SceneSeverity::Warning);
	QCOMPARE(splitEndWarnings[0].file, "signalling.json");
	QCOMPARE(splitEndWarnings[0].itemId, "head");
	QCOMPARE(splitEndWarnings[0].path, "signalling_areas[0].end_km");
	QCOMPARE(splitEndWarnings[0].relatedId, "@block-2@");
	QCOMPARE(splitEndWarnings[0].message, "The end of signalling area head (1.500000 km) lies inside route section "
										  "@block-2@ (track-1 1.000 to 2.000 km), so the section is not part of the area");
	QVERIFY(contains(splitEndWarnings[0].suggestedFix, "1.000000 or 2.000000 km"));
	QVERIFY(!hasErrors(splitEndDiagnostics));
	QVERIFY(!hasCode(validateScene(splitEnd), splitCode));
}

void TestSceneValidator::warnsAboutAreaStartInsideSection() {
	SceneModel splitStart = completeScene();
	splitStart.signallingAreas = {{"tail", 0.5, 2.0, 2, {}}};
	const auto splitStartWarnings = findAll(validateRunnableScene(splitStart), "scene.signalling_area.splits_section");
	// An area start inside a route section gives one warning.
	QCOMPARE(splitStartWarnings.size(), std::size_t(1));
	QCOMPARE(splitStartWarnings[0].path, "signalling_areas[0].start_km");
	QCOMPARE(splitStartWarnings[0].message, "The start of signalling area tail (0.500000 km) lies inside route section "
											"@block-1@ (track-1 0.000 to 1.000 km), so the section is not part of the area");
}

void TestSceneValidator::warnsAboutEachCuttingEdge() {
	SceneModel splitBoth = completeScene();
	splitBoth.signallingAreas = {{"inner", 0.5, 1.5, 2, {}}};
	// Each edge that cuts a section gives its own warning.
	QCOMPARE(findAll(validateRunnableScene(splitBoth), "scene.signalling_area.splits_section").size(), std::size_t(2));
}

void TestSceneValidator::skipsCutSectionOnNoRoute() {
	SceneModel splitOffRoute = completeScene();
	splitOffRoute.routes[0].blocks = {"block-1"};
	splitOffRoute.signallingAreas = {{"head", 0.0, 1.5, 2, {}}};
	QVERIFY2(findAll(validateRunnableScene(splitOffRoute), "scene.signalling_area.splits_section").empty(),
		"an edge inside a section that is on no route gives no warning");
}

void TestSceneValidator::alignedAreaEdgeGivesNoSplitWarning_data() {
	QTest::addColumn<double>("startKm");
	QTest::addColumn<double>("endKm");
	QTest::addColumn<QString>("track");
	QTest::newRow("exact") << 0.0 << 1.0 << QString();
	QTest::newRow("inside") << 0.0 << 1.0 - 5e-9 << QString();
	QTest::newRow("start") << 1.0 + 5e-9 << 2.0 << QString();
	QTest::newRow("all") << 0.0 << 2.0 << QString("track-1");
}

void TestSceneValidator::alignedAreaEdgeGivesNoSplitWarning() {
	QFETCH(double, startKm);
	QFETCH(double, endKm);
	QFETCH(QString, track);
	SceneModel aligned = completeScene();
	aligned.signallingAreas = {SceneSignallingArea{QTest::currentDataTag(), startKm, endKm, 2, track.toStdString()}};
	QVERIFY2(findAll(validateRunnableScene(aligned), "scene.signalling_area.splits_section").empty(),
		"an area edge on a section edge, within the tolerance, gives no warning");
}

void TestSceneValidator::oneWarningNamesSectionsOfOtherTracks() {
	SceneModel splitMany = completeScene();
	splitMany.tracks.push_back({"track-2"});
	splitMany.nodes.push_back({"node-4", "track-2", 0.0, 1.0});
	splitMany.nodes.push_back({"node-5", "track-2", 2.0, 1.0});
	splitMany.arcs.push_back({"arc-3", "track-2", "node-4", "node-5", 0.0, 0.0, 35.0});
	splitMany.blocks.push_back({"block-3", "track-2", 2.0});
	splitMany.routes[0].blocks = {"block-1", "block-2", "block-3"};
	splitMany.signallingAreas = {{"head", 0.0, 1.5, 2, {}}};
	const auto splitManyWarnings = findAll(validateRunnableScene(splitMany), "scene.signalling_area.splits_section");
	// An edge that cuts sections of several tracks gives one warning that names the others.
	QCOMPARE(splitManyWarnings.size(), std::size_t(1));
	QVERIFY(contains(splitManyWarnings[0].message, "@block-2@ (track-1 1.000 to 2.000 km), so"));
	QVERIFY(contains(splitManyWarnings[0].message, ". @block-3@ (track-2 0.000 to 2.000 km) is cut the same way"));
}

// An area that contains no complete section.
void TestSceneValidator::warnsAboutAreaOutsideNetwork() {
	const std::string emptyCode = "scene.signalling_area.empty";
	SceneModel emptyArea = completeScene();
	emptyArea.signallingAreas = {{"far", 5.0, 6.0, 2, {}}, {"a", 0.0, 2.0, 2, {}}};
	const auto emptyDiagnostics = validateRunnableScene(emptyArea);
	const auto emptyWarnings = findAll(emptyDiagnostics, emptyCode);
	// An area outside the network gives one warning with the extent of the network.
	QCOMPARE(emptyWarnings.size(), std::size_t(1));
	QVERIFY(emptyWarnings[0].severity == SceneSeverity::Warning);
	QCOMPARE(emptyWarnings[0].file, "signalling.json");
	QCOMPARE(emptyWarnings[0].itemId, "far");
	QCOMPARE(emptyWarnings[0].path, "signalling_areas[0]");
	QCOMPARE(emptyWarnings[0].message, "Signalling area far (5.000000 to 6.000000 km) contains no complete section "
									   "(blocks of the network cover 0.000000 to 2.000000 km)");
	QVERIFY(!hasErrors(emptyDiagnostics));
	QVERIFY(!hasCode(validateScene(emptyArea), emptyCode));
}

void TestSceneValidator::areaInsideOneSectionIsEmpty() {
	SceneModel insideSection = completeScene();
	insideSection.signallingAreas = {{"small", 0.2, 0.8, 2, {}}, {"a", 0.0, 2.0, 2, {}}};
	const auto insideWarnings = findAll(validateRunnableScene(insideSection), "scene.signalling_area.empty");
	// An area inside one section contains no section and its edges cut that section.
	QCOMPARE(insideWarnings.size(), std::size_t(1));
	QCOMPARE(insideWarnings[0].itemId, "small");
	QCOMPARE(findAll(validateRunnableScene(insideSection), "scene.signalling_area.splits_section").size(), std::size_t(2));
}

void TestSceneValidator::emptyTrackAreaGivesExtentOfTrack() {
	SceneModel trackExtent = completeScene();
	trackExtent.signallingAreas = {{"far", 5.0, 6.0, 2, "track-1"}, {"a", 0.0, 2.0, 2, {}}};
	const auto trackExtentWarnings = findAll(validateRunnableScene(trackExtent), "scene.signalling_area.empty");
	// A track-scoped area outside its track gives the extent of that track.
	QCOMPARE(trackExtentWarnings.size(), std::size_t(1));
	QVERIFY(contains(trackExtentWarnings[0].message, "blocks of track track-1 cover 0.000000 to 2.000000 km"));
}

void TestSceneValidator::invalidAreasGetNoEmptyOrSplitWarning() {
	SceneModel unusableAreas = completeScene();
	unusableAreas.signallingAreas = {{"inverted", 2.0, 0.0, 2, {}}, {"level", 0.0, 2.0, 7, {}},
		{"track", 0.0, 2.0, 2, "missing-track"}, {"a", 0.0, 2.0, 2, {}}};
	const auto unusableDiagnostics = validateRunnableScene(unusableAreas);
	// Areas that are already invalid get no empty or split warning.
	QVERIFY(findAll(unusableDiagnostics, "scene.signalling_area.empty").empty());
	QVERIFY(findAll(unusableDiagnostics, "scene.signalling_area.splits_section").empty());
}

void TestSceneValidator::correctSceneGetsNoAreaWarning_data() {
	QTest::addColumn<int>("index");
	for (int index = 0; index < static_cast<int>(std::size(kCorrectAreaScenes)); ++index)
		QTest::newRow(kCorrectAreaScenes[index].name) << index;
}

void TestSceneValidator::correctSceneGetsNoAreaWarning() {
	QFETCH(int, index);
	SceneModel correct = completeScene();
	kCorrectAreaScenes[index].apply(correct);
	const auto correctDiagnostics = validateRunnableScene(correct);
	// A correct scene gets no area warning.
	QVERIFY(!hasCode(correctDiagnostics, "scene.signalling_area.empty"));
	QVERIFY(!hasCode(correctDiagnostics, "scene.signalling_area.splits_section"));
	QVERIFY(!hasCode(correctDiagnostics, "scene.signalling_area.conflict"));
}

// The warning about route sections without a level names the stretches and the areas next to them.
void TestSceneValidator::fixWithoutAreasGivesNetworkRange() {
	SceneModel noAreas = completeScene();
	noAreas.signallingAreas.clear();
	const auto stretchNone = findAll(validateRunnableScene(noAreas), "scene.signalling.level.missing");
	const std::string stretchNoneFix = "In Infrastructure > Signalling areas add a network-wide area covering "
									   "0.000000 to 2.000000 km, or a track-scoped area for each track listed";
	// Without areas the fix gives the range of the network and no list of stretches.
	QCOMPARE(stretchNone.size(), std::size_t(1));
	QCOMPARE(stretchNone[0].suggestedFix, stretchNoneFix);
}

void TestSceneValidator::namesAreaBeforeStretch() {
	SceneModel partialArea = completeScene();
	partialArea.signallingAreas = {{"partial", 0.0, 1.5, 2, {}}};
	const auto stretchPartial = findAll(validateRunnableScene(partialArea), "scene.signalling.level.missing");
	const std::string stretchPartialMessage =
		"1 of 2 route sections has no signalling system and runs without signalling: @block-2@ (track-1 1.000 to 2.000 km) (track track-1)";
	const std::string stretchPartialFix = "or a track-scoped area for each track listed. Without a signalling system: "
										  "track-1 1.000000 to 2.000000 km (after area partial)";
	// A stretch after an area names that area.
	QCOMPARE(stretchPartial.size(), std::size_t(1));
	QCOMPARE(stretchPartial[0].message, stretchPartialMessage);
	QVERIFY(contains(stretchPartial[0].suggestedFix, stretchPartialFix));
}

void TestSceneValidator::namesBothAreasAroundStretch() {
	SceneModel gapBetween = conflictingDerivedAreasScene();
	gapBetween.routes[0].blocks = {"block-3"};
	gapBetween.signallingAreas = {{"left", 0.0, 3.0, 2, "track-2"}, {"right", 4.0, 5.0, 2, {}}};
	const auto stretchBetween = findAll(validateRunnableScene(gapBetween), "scene.signalling.level.missing");
	const std::string stretchBetweenFix =
		". Without a signalling system: track-2 3.000000 to 4.000000 km (between area left and area right)";
	// A stretch between two areas names both.
	QCOMPARE(stretchBetween.size(), std::size_t(1));
	QVERIFY(contains(stretchBetween[0].suggestedFix, stretchBetweenFix));
}

void TestSceneValidator::givesEachTrackItsOwnStretch() {
	SceneModel gapBefore = conflictingDerivedAreasScene();
	gapBefore.routes[0].blocks = {"block-1", "block-2", "block-3"};
	gapBefore.signallingAreas = {{"right", 4.0, 5.0, 2, {}}};
	const auto stretchBefore = findAll(validateRunnableScene(gapBefore), "scene.signalling.level.missing");
	const std::string stretchBeforeFix = "track-1 0.000000 to 2.000000 km (before area right), "
										 "track-2 3.000000 to 4.000000 km (before area right)";
	// Each track gets its own stretch, before the area that follows it.
	QCOMPARE(stretchBefore.size(), std::size_t(1));
	QVERIFY(contains(stretchBefore[0].suggestedFix, stretchBeforeFix));
}

void TestSceneValidator::namesAreaInsideStretch() {
	SceneModel splitBoth = completeScene();
	splitBoth.signallingAreas = {{"inner", 0.5, 1.5, 2, {}}};
	const auto stretchInside = findAll(validateRunnableScene(splitBoth), "scene.signalling.level.missing");
	// A stretch with an area inside it says so.
	QCOMPARE(stretchInside.size(), std::size_t(1));
	QVERIFY(contains(stretchInside[0].suggestedFix, "track-1 0.000000 to 2.000000 km (area inner lies inside it)"));
}

void TestSceneValidator::listsSeparateStretchesOfTrack() {
	SceneModel gapRuns = completeScene();
	gapRuns.blocks = {{"block-1", "track-1", 1.0}, {"block-2", "track-1", 1.0}, {"block-3", "track-1", 1.0}};
	gapRuns.nodes.push_back({"node-4", "track-1", 3.0, 0.0});
	gapRuns.arcs.push_back({"arc-3", "track-1", "node-3", "node-4", 0.0, 0.0, 35.0});
	gapRuns.routes[0].blocks = {"block-1", "block-2", "block-3"};
	gapRuns.signallingAreas = {{"middle", 1.0, 2.0, 2, {}}};
	const auto stretchRuns = findAll(validateRunnableScene(gapRuns), "scene.signalling.level.missing");
	// Separate uncovered stretches of one track are listed one by one.
	QCOMPARE(stretchRuns.size(), std::size_t(1));
	QVERIFY(contains(stretchRuns[0].message, "2 of 3 route sections have no signalling system"));
	QVERIFY(contains(stretchRuns[0].suggestedFix, "track-1 0.000000 to 1.000000 km (before area middle), "
												  "track-1 2.000000 to 3.000000 km (after area middle)"));
}

void TestSceneValidator::namesSignallingLevels_data() {
	QTest::addColumn<int>("level");
	QTest::addColumn<QString>("label");
	for (const auto& entry : kLevelLabels)
		QTest::newRow(entry.second) << entry.first << QString(entry.second);
}

void TestSceneValidator::namesSignallingLevels() {
	QFETCH(int, level);
	QFETCH(QString, label);
	// A signalling level has its label.
	QCOMPARE(signallingLevelName(level), label.toStdString());
}

void TestSceneValidator::describesSignallingLevels() {
	std::vector<std::string> levelDescriptions;
	for (const auto& entry : kLevelLabels)
		levelDescriptions.push_back(signallingLevelDescription(entry.first));
	QVERIFY2(std::find(levelDescriptions.begin(), levelDescriptions.end(), "") == levelDescriptions.end(),
		"every signalling level has a description");
	// The valid levels differ in description and the invalid ones say which levels are valid.
	QCOMPARE(std::set<std::string>(levelDescriptions.begin(), levelDescriptions.begin() + 7).size(), std::size_t(7));
	QCOMPARE(levelDescriptions[7], "Valid levels are 0 to 5.");
	QCOMPARE(levelDescriptions[8], levelDescriptions[7]);
}

// The numbers, the range and the unset value of the signalling levels.
void TestSceneValidator::numbersSignallingLevels_data() {
	QTest::addColumn<int>("index");
	for (int index = 0; index < static_cast<int>(std::size(kLevelNumbers)); ++index)
		QTest::addRow("%d", kLevelNumbers[index].second) << index;
}

void TestSceneValidator::numbersSignallingLevels() {
	QFETCH(int, index);
	// A signalling level has the number of the file format.
	QCOMPARE(levelValue(kLevelNumbers[index].first), kLevelNumbers[index].second);
}

void TestSceneValidator::acceptsLevelsZeroToFive_data() {
	QTest::addColumn<int>("level");
	for (int level = 0; level <= 5; ++level)
		QTest::addRow("%d", level) << level;
}

void TestSceneValidator::acceptsLevelsZeroToFive() {
	QFETCH(int, level);
	QVERIFY2(isValidSignallingLevel(level), "the numbers 0 to 5 are signalling levels");
}

// Numbers outside 0 to 5.
static void addInvalidLevelRows() {
	QTest::addColumn<int>("level");
	for (const int level : {-1, 6, kSignallingLevelUnset, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
		QTest::addRow("%d", level) << level;
}

void TestSceneValidator::rejectsLevelsOutsideZeroToFive_data() {
	addInvalidLevelRows();
}

void TestSceneValidator::rejectsLevelsOutsideZeroToFive() {
	QFETCH(int, level);
	QVERIFY2(!isValidSignallingLevel(level), "numbers outside 0 to 5 are not signalling levels");
}

void TestSceneValidator::fixedBlockLevels_data() {
	QTest::addColumn<int>("level");
	for (const int level : {levelValue(SignallingLevel::Atb), levelValue(SignallingLevel::EtcsLevel1), levelValue(SignallingLevel::EtcsLevel2),
			 levelValue(SignallingLevel::Bacc)})
		QTest::addRow("%d", level) << level;
}

void TestSceneValidator::fixedBlockLevels() {
	QFETCH(int, level);
	// Levels 0, 1, 2 and 5 are fixed block levels.
	QVERIFY(isFixedBlockLevel(level));
	QVERIFY(!isMovingBlockLevel(level));
}

void TestSceneValidator::movingBlockLevels_data() {
	QTest::addColumn<int>("level");
	for (const int level : {levelValue(SignallingLevel::EtcsLevel3), levelValue(SignallingLevel::VirtualCoupling)})
		QTest::addRow("%d", level) << level;
}

void TestSceneValidator::movingBlockLevels() {
	QFETCH(int, level);
	// Levels 3 and 4 are moving block levels.
	QVERIFY(isMovingBlockLevel(level));
	QVERIFY(!isFixedBlockLevel(level));
}

void TestSceneValidator::levelsOutsideZeroToFiveHaveNoKind_data() {
	addInvalidLevelRows();
}

void TestSceneValidator::levelsOutsideZeroToFiveHaveNoKind() {
	QFETCH(int, level);
	// Numbers outside 0 to 5 are in neither kind of level.
	QVERIFY(!isFixedBlockLevel(level));
	QVERIFY(!isMovingBlockLevel(level));
}

void TestSceneValidator::storesUnsetLevel() {
	// The unset level has its stored value.
	QCOMPARE(kSignallingLevelUnset, -99999999);
}

void TestSceneValidator::acceptsAreaOfLevelFive() {
	SceneModel highestAreaLevel = completeScene();
	highestAreaLevel.signallingAreas = {{"area", 0.0, 1.0, 5, {}}};
	QVERIFY2(findAll(validateScene(highestAreaLevel), "scene.signalling_area.level").empty(),
		"an area of level 5 has a valid level");
}

void TestSceneValidator::rejectsAreaOfLevelMinusOne() {
	SceneModel negativeAreaLevel = completeScene();
	negativeAreaLevel.signallingAreas = {{"area", 0.0, 1.0, -1, {}}};
	const auto negativeLevelErrors = findAll(validateScene(negativeAreaLevel), "scene.signalling_area.level");
	// An area of level -1 has an invalid level.
	QCOMPARE(negativeLevelErrors.size(), std::size_t(1));
	QCOMPARE(negativeLevelErrors[0].message, "Signalling area area has level -1, which is not a signalling system");
}

void TestSceneValidator::invalidLevelAreaIsNotEmpty() {
	SceneModel invalidAreaLevel = completeScene();
	invalidAreaLevel.signallingAreas = {{"area", 0.0, 1.0, 6, {}}};
	QVERIFY2(findAll(validateRunnableScene(invalidAreaLevel), "scene.signalling_area.empty").empty(),
		"an area with an invalid level takes no part in the warning about empty areas");
}

void TestSceneValidator::areaWithoutSystemHasOwnMessage() {
	SceneModel unchosenAreaSystem = completeScene();
	unchosenAreaSystem.signallingAreas = {{"area", 0.0, 1.0, kSignallingLevelUnset, "track-1"}};
	const auto unchosenSystemErrors = findAll(validateScene(unchosenAreaSystem), "scene.signalling_area.level");
	// An area without a chosen signalling system has its own message and fix.
	QCOMPARE(unchosenSystemErrors.size(), std::size_t(1));
	QVERIFY(unchosenSystemErrors[0].severity == SceneSeverity::Error);
	QCOMPARE(unchosenSystemErrors[0].message, "Signalling area area has no signalling system");
	QCOMPARE(unchosenSystemErrors[0].file, "signalling.json");
	QCOMPARE(unchosenSystemErrors[0].itemType, "signalling_area");
	QCOMPARE(unchosenSystemErrors[0].itemId, "area");
	QCOMPARE(unchosenSystemErrors[0].path, "signalling_areas[0].level");
	QCOMPARE(unchosenSystemErrors[0].relatedId, "track-1");
	QCOMPARE(unchosenSystemErrors[0].suggestedFix, "Choose a signalling system (level 0 to 5) for the area in Infrastructure > Signalling areas");
}

void TestSceneValidator::otherInvalidLevelKeepsLevelMessage() {
	SceneModel nearUnsetLevel = completeScene();
	nearUnsetLevel.signallingAreas = {{"area", 0.0, 1.0, kSignallingLevelUnset + 1, "track-1"}};
	const auto nearUnsetErrors = findAll(validateScene(nearUnsetLevel), "scene.signalling_area.level");
	// Any other level outside 0 to 5 keeps the level message and fix.
	QCOMPARE(nearUnsetErrors.size(), std::size_t(1));
	QCOMPARE(nearUnsetErrors[0].message, "Signalling area area has level -99999998, which is not a signalling system");
	QCOMPARE(nearUnsetErrors[0].relatedId, "track-1");
	QCOMPARE(nearUnsetErrors[0].suggestedFix, "Choose a signalling system (level 0 to 5)");
}

void TestSceneValidator::networkAreaDecidesItsSections() {
	const SceneSignallingAnalysis cleanAnalysis = analyze(completeScene());
	// A network-wide area decides the level of the sections it contains.
	QCOMPARE(sectionOf(cleanAnalysis, "@block-1@").level, 0);
	QCOMPARE(sectionOf(cleanAnalysis, "@block-1@").decidingArea, std::size_t(0));
	QCOMPARE(sectionOf(cleanAnalysis, "@block-2@").level, 0);
	QVERIFY(sectionOf(cleanAnalysis, "@block-2@").onRoute);
	QVERIFY(sectionOf(cleanAnalysis, "@block-1@").conflicts.empty());
	QVERIFY2(cleanAnalysis.section("@no-such-block@") == nullptr, "an unknown section has no analysis");
	// An area that holds every section counts them and splits none.
	QCOMPARE(cleanAnalysis.areas.size(), std::size_t(1));
	QCOMPARE_GE(cleanAnalysis.areas[0].sectionCount, std::size_t(2));
	QCOMPARE(cleanAnalysis.areas[0].routeSectionCount, std::size_t(2));
	QVERIFY(cleanAnalysis.areas[0].sectionsSplitByStart.empty());
	QVERIFY(cleanAnalysis.areas[0].sectionsSplitByEnd.empty());
}

void TestSceneValidator::trackAreaOverridesNetworkArea() {
	SceneModel scoped = completeScene();
	scoped.signallingAreas = {{"network", 0.0, 2.0, 1, {}}, {"track", 0.0, 1.0, 3, "track-1"},
		{"elsewhere", 0.0, 2.0, 5, "track-2"}};
	const SceneSignallingAnalysis scopedAnalysis = analyze(scoped);
	// A track-scoped area overrides the network-wide area without a conflict.
	QCOMPARE(sectionOf(scopedAnalysis, "@block-1@").level, 3);
	QCOMPARE(sectionOf(scopedAnalysis, "@block-1@").decidingArea, std::size_t(1));
	QVERIFY(sectionOf(scopedAnalysis, "@block-1@").conflicts.empty());
	// The network-wide area decides where the track-scoped area does not reach.
	QCOMPARE(sectionOf(scopedAnalysis, "@block-2@").level, 1);
	QCOMPARE(sectionOf(scopedAnalysis, "@block-2@").decidingArea, std::size_t(0));
	// An area counts the sections it contains even when another area decides them, and none on another track.
	QCOMPARE(scopedAnalysis.areas[0].routeSectionCount, std::size_t(2));
	QCOMPARE(scopedAnalysis.areas[1].routeSectionCount, std::size_t(1));
	QCOMPARE(scopedAnalysis.areas[2].sectionCount, std::size_t(0));
}

void TestSceneValidator::listsConflictsInAreaOrder() {
	SceneModel conflicting = completeScene();
	conflicting.signallingAreas = {{"a", 0.0, 2.0, 2, {}}, {"b", 0.0, 1.0, 2, {}}, {"c", 0.0, 2.0, 3, {}},
		{"d", 0.0, 2.0, 4, {}}, {"t1", 0.0, 2.0, 1, "track-1"}, {"t2", 1.0, 2.0, 5, "track-1"}};
	const SceneSignallingAnalysis conflictAnalysis = analyze(conflicting);
	const SceneSectionSignalling conflictOne = sectionOf(conflictAnalysis, "@block-1@");
	// Areas that disagree with the first area of their scope are listed as conflicts in area order.
	QCOMPARE(conflictOne.level, 1);
	QCOMPARE(conflictOne.decidingArea, std::size_t(4));
	QCOMPARE(conflictOne.conflicts.size(), std::size_t(2));
	QCOMPARE(conflictOne.conflicts[0].firstArea, std::size_t(0));
	QCOMPARE(conflictOne.conflicts[0].area, std::size_t(2));
	QVERIFY(!conflictOne.conflicts[0].trackScoped);
	QCOMPARE(conflictOne.conflicts[1].area, std::size_t(3));
	QVERIFY(!conflictOne.conflicts[1].trackScoped);
	const SceneSectionSignalling conflictTwo = sectionOf(conflictAnalysis, "@block-2@");
	// Conflicts of the network-wide scope come before those of the track scope.
	QCOMPARE(conflictTwo.level, 1);
	QCOMPARE(conflictTwo.conflicts.size(), std::size_t(3));
	QVERIFY(conflictTwo.conflicts[2].trackScoped);
	QCOMPARE(conflictTwo.conflicts[2].firstArea, std::size_t(4));
	QCOMPARE(conflictTwo.conflicts[2].area, std::size_t(5));
	const auto conflictingRun = validateRunnableScene(conflicting);
	std::vector<std::string> conflictErrors;
	for (const SceneDiagnostic& diagnostic : findAll(conflictingRun, "scene.signalling_area.conflict")) {
		// An area conflict is an error on the signalling file.
		QVERIFY(diagnostic.severity == SceneSeverity::Error);
		QCOMPARE(diagnostic.file, "signalling.json");
		QCOMPARE(diagnostic.itemType, "signalling_area");
		conflictErrors.push_back(diagnostic.itemId + " " + diagnostic.path + " " + diagnostic.relatedId);
	}
	// The validator reports one conflict for each pair of areas that disagree about a section.
	QCOMPARE(conflictErrors, std::vector<std::string>({"c signalling_areas[2] a", "d signalling_areas[3] a", "t2 signalling_areas[5] t1"}));
}

void TestSceneValidator::firstOfAgreeingAreasDecides() {
	SceneModel agreeing = completeScene();
	agreeing.signallingAreas = {{"a", 0.0, 2.0, 2, {}}, {"b", 0.0, 1.5, 2, {}}};
	// Areas that give the same level do not conflict and the first one decides.
	QVERIFY(sectionOf(analyze(agreeing), "@block-1@").conflicts.empty());
	QCOMPARE(sectionOf(analyze(agreeing), "@block-1@").decidingArea, std::size_t(0));
}

void TestSceneValidator::areaEdgesNameCutSections() {
	SceneModel splitting = completeScene();
	splitting.signallingAreas = {{"head", 0.0, 1.5, 2, {}}, {"tail", 0.5, 2.0, 3, {}}};
	const SceneSignallingAnalysis splitAnalysis = analyze(splitting);
	// An area edge names the sections it cuts through.
	QVERIFY(listed(splitAnalysis.areas[0].sectionsSplitByEnd, "@block-2@"));
	QVERIFY(!listed(splitAnalysis.areas[0].sectionsSplitByEnd, "@block-1@"));
	QVERIFY(splitAnalysis.areas[0].sectionsSplitByStart.empty());
	QVERIFY(listed(splitAnalysis.areas[1].sectionsSplitByStart, "@block-1@"));
	QVERIFY(!listed(splitAnalysis.areas[1].sectionsSplitByStart, "@block-2@"));
	QVERIFY(splitAnalysis.areas[1].sectionsSplitByEnd.empty());
	// A section that crosses the edge of an area is not counted for it.
	QCOMPARE(splitAnalysis.areas[0].routeSectionCount, std::size_t(1));
	QCOMPARE(splitAnalysis.areas[1].routeSectionCount, std::size_t(1));
	QCOMPARE(sectionOf(splitAnalysis, "@block-1@").level, 2);
	QCOMPARE(sectionOf(splitAnalysis, "@block-2@").level, 3);
	splitting.signallingAreas = {{"head", 0.0, 1.5, 2, {}}, {"tail", 1.5, 2.0, 3, {}}};
	// A section cut by the edge between two areas has no level.
	QCOMPARE(sectionOf(analyze(splitting), "@block-2@").level, kSignallingLevelUnset);
	QCOMPARE(sectionOf(analyze(splitting), "@block-2@").decidingArea, SceneSectionSignalling::kNoArea);
}

void TestSceneValidator::areaEdgesFollowTolerance() {
	SceneModel tolerance = completeScene();
	tolerance.signallingAreas = {{"in", 0.0, 1.0 - 5e-9, 2, {}}};
	SceneSignallingAnalysis toleranceAnalysis = analyze(tolerance);
	// An area end just inside the tolerance holds the section and does not cut it.
	QCOMPARE(sectionOf(toleranceAnalysis, "@block-1@").level, 2);
	QVERIFY(!listed(toleranceAnalysis.areas[0].sectionsSplitByEnd, "@block-1@"));
	tolerance.signallingAreas = {{"out", 0.0, 1.0 - 1e-7, 2, {}}};
	toleranceAnalysis = analyze(tolerance);
	// An area end outside the tolerance cuts the section.
	QCOMPARE(sectionOf(toleranceAnalysis, "@block-1@").level, kSignallingLevelUnset);
	QVERIFY(listed(toleranceAnalysis.areas[0].sectionsSplitByEnd, "@block-1@"));
	tolerance.signallingAreas = {{"in", 1.0 + 5e-9, 2.0, 2, {}}};
	toleranceAnalysis = analyze(tolerance);
	// An area start just inside the tolerance holds the section and does not cut it.
	QCOMPARE(sectionOf(toleranceAnalysis, "@block-2@").level, 2);
	QVERIFY(!listed(toleranceAnalysis.areas[0].sectionsSplitByStart, "@block-2@"));
}

void TestSceneValidator::unusableAreasTakeNoPart() {
	SceneModel unusable = completeScene();
	unusable.signallingAreas = {{"inverted", 2.0, 0.0, 2, {}}, {"empty", 1.0, 1.0, 2, {}},
		{"high", 0.0, 2.0, 6, {}}, {"low", 0.0, 2.0, -1, {}},
		{"infinite", 0.0, std::numeric_limits<double>::infinity(), 2, {}}};
	const SceneSignallingAnalysis unusableAnalysis = analyze(unusable);
	bool unusableCounted = false;
	for (const SceneAreaSignalling& area : unusableAnalysis.areas)
		unusableCounted |= area.sectionCount != 0 || !area.sectionsSplitByStart.empty() || !area.sectionsSplitByEnd.empty();
	// Areas with an empty range or a level outside 0 to 5 take no part.
	QVERIFY(!unusableCounted);
	QCOMPARE(sectionOf(unusableAnalysis, "@block-1@").level, kSignallingLevelUnset);
	QVERIFY(sectionOf(unusableAnalysis, "@block-1@").conflicts.empty());
}

void TestSceneValidator::areaWithoutSystemTakesNoPart() {
	SceneModel unchosenSystemAnalysis = completeScene();
	unchosenSystemAnalysis.signallingAreas = {{"unchosen", 0.0, 1.5, kSignallingLevelUnset, {}}, {"chosen", 0.0, 2.0, 2, {}}};
	const SceneSignallingAnalysis unchosenAnalysis = analyze(unchosenSystemAnalysis);
	const auto unchosenRun = validateRunnableScene(unchosenSystemAnalysis);
	// An area without a chosen system takes no part in the analysis.
	QCOMPARE(unchosenAnalysis.areas[0].sectionCount, std::size_t(0));
	QVERIFY(unchosenAnalysis.areas[0].sectionsSplitByStart.empty());
	QVERIFY(unchosenAnalysis.areas[0].sectionsSplitByEnd.empty());
	QCOMPARE(sectionOf(unchosenAnalysis, "@block-1@").level, 2);
	QCOMPARE(sectionOf(unchosenAnalysis, "@block-1@").decidingArea, std::size_t(1));
	QVERIFY(sectionOf(unchosenAnalysis, "@block-1@").conflicts.empty());
	QVERIFY(sectionOf(unchosenAnalysis, "@block-2@").conflicts.empty());
	// An area without a chosen system gives no conflict, split or empty warning.
	QVERIFY(findAll(unchosenRun, "scene.signalling_area.conflict").empty());
	QVERIFY(findAll(unchosenRun, "scene.signalling_area.splits_section").empty());
	QVERIFY(findAll(unchosenRun, "scene.signalling_area.empty").empty());
	QCOMPARE(findAll(unchosenRun, "scene.signalling_area.level").size(), std::size_t(1));
}

void TestSceneValidator::countsRouteSectionsOfArea() {
	SceneModel offRoute = completeScene();
	offRoute.routes[0].blocks = {"block-1"};
	const SceneSignallingAnalysis offRouteAnalysis = analyze(offRoute);
	// The route sections of an area are those of its sections that a route uses.
	QVERIFY(sectionOf(offRouteAnalysis, "@block-1@").onRoute);
	QVERIFY(!sectionOf(offRouteAnalysis, "@block-2@").onRoute);
	QCOMPARE(offRouteAnalysis.areas[0].routeSectionCount, std::size_t(1));
	QCOMPARE_GE(offRouteAnalysis.areas[0].sectionCount, std::size_t(2));
	offRoute.routes[0].blocks = {"block-2", "block-1"};
	// A route in decreasing chainage uses the same sections.
	QCOMPARE(analyze(offRoute).areas[0].routeSectionCount, std::size_t(2));
}

void TestSceneValidator::restrictionActsAtEveryLevel_data() {
	QTest::addColumn<int>("level");
	for (const int level : {0, 1, 2, 3, 4, 5})
		QTest::addRow("%d", level) << level;
}

void TestSceneValidator::restrictionActsAtEveryLevel() {
	QFETCH(int, level);
	SceneModel restricted = completeScene();
	restricted.singleTrackRestrictions = {{"block-1", "block-2", "block-1", "block-2"}};
	restricted.signallingAreas = {{"network", 0.0, 2.0, level, {}}};
	QVERIFY2(!hasCode(validateRunnableScene(restricted), "scene.single_track.no_effect"),
		"a single-track restriction at any signalling level acts and gets no warning");
}

void TestSceneValidator::warnsAboutRestrictionWithoutLevel() {
	const std::string noEffect = "scene.single_track.no_effect";
	SceneModel restricted = completeScene();
	restricted.singleTrackRestrictions = {{"block-1", "block-2", "block-1", "block-2"}};
	restricted.signallingAreas.clear();
	const auto unsignalledDiagnostics = validateRunnableScene(restricted);
	const SceneDiagnostic* unsignalledWarning = findCode(unsignalledDiagnostics, noEffect);
	// A single-track restriction without a signalling level warns that it has no effect.
	if (unsignalledWarning == nullptr)
		QFAIL("the warning is not reported");
	QVERIFY(unsignalledWarning->severity == SceneSeverity::Warning);
	QCOMPARE(unsignalledWarning->file, "signalling.json");
	QCOMPARE(unsignalledWarning->path, "single_track_restrictions[0]");
	QVERIFY(contains(unsignalledWarning->message,
		"Single-track restriction 0 (start block-1 (track-1 0.000 to 1.000 km), end block-2 (track-1 1.000 "
		"to 2.000 km), protected start block-1 (track-1 0.000 to 1.000 km), protected end block-2 "
		"(track-1 1.000 to 2.000 km)) has no effect, because these sections have no signalling system: start block-1 "
		"(track-1 0.000 to 1.000 km), end block-2"));
	QVERIFY(contains(unsignalledWarning->message, ", protected end block-2 (track-1 1.000 to 2.000 km)"));
	QCOMPARE(unsignalledWarning->suggestedFix, "In Infrastructure > Signalling areas give these sections a signalling system");
	QVERIFY(!hasErrors(unsignalledDiagnostics));
	QVERIFY(!hasCode(validateScene(restricted), noEffect));
	QVERIFY(hasCode(unsignalledDiagnostics, "scene.signalling.level.missing"));
	restricted.singleTrackRestrictions = {{"block-1", "block-2", "block-1", "block-2"}, {"block-2", "block-1", "block-2", "block-1"}};
	std::size_t noEffectCount = 0;
	for (const SceneDiagnostic& diagnostic : validateRunnableScene(restricted))
		noEffectCount += diagnostic.code == noEffect ? 1 : 0;
	// Each single-track restriction gets one warning.
	QCOMPARE(noEffectCount, std::size_t(2));
	restricted.singleTrackRestrictions = {{"block-1", "block-2", "block-1", "block-2"}};
	restricted.signallingAreas = {{"partial", 0.0, 1.5, 2, {}}};
	const auto partialRestrictionDiagnostics = validateRunnableScene(restricted);
	const SceneDiagnostic* partialRestrictionWarning = findCode(partialRestrictionDiagnostics, noEffect);
	// The warning names only the blocks without a signalling level.
	if (partialRestrictionWarning == nullptr)
		QFAIL("the warning is not reported");
	QVERIFY(contains(partialRestrictionWarning->message, "no signalling system: end block-2 (track-1 1.000 to 2.000 km), protected end block-2"));
	QVERIFY(!contains(partialRestrictionWarning->message, "no signalling system: start"));
	restricted.singleTrackRestrictions.clear();
	restricted.signallingAreas.clear();
	QVERIFY2(!hasCode(validateRunnableScene(restricted), noEffect), "a scene without restrictions has nothing to warn about");
}

void TestSceneValidator::warnsAboutSteepDescent() {
	const std::string steepCode = "scene.route.gradient.steep";
	const SceneModel steepDescent = steepGradientScene(-0.09, 0.0);
	const auto steepDescentDiagnostics = validateScene(steepDescent);
	const SceneDiagnostic* steepDescentWarning = findCode(steepDescentDiagnostics, steepCode);
	// A descent steeper than the braking limit gives a warning that names the arc and both limits.
	if (steepDescentWarning == nullptr)
		QFAIL("the warning is not reported");
	QVERIFY(steepDescentWarning->severity == SceneSeverity::Warning);
	QCOMPARE(steepDescentWarning->file, "infrastructure.json");
	QVERIFY(contains(steepDescentWarning->message, "Route route-1 with composition composition-1: 1 of 2 arcs is"));
	QVERIFY(contains(steepDescentWarning->message, "arc-1 (-0.09)"));
	QVERIFY(contains(steepDescentWarning->message, "limit 0.08333"));
	QVERIFY(contains(steepDescentWarning->message, "limit 0.1411"));
	QVERIFY(contains(steepDescentWarning->message, "too steep for the train to brake on as a descent"));
	QVERIFY(contains(steepDescentWarning->suggestedFix, "0.01 is a 1 percent slope"));
	QVERIFY(!hasErrors(steepDescentDiagnostics));
	QVERIFY2(hasCode(validateRunnableScene(steepDescent), steepCode),
		"runnable validation reports the steep gradient too");
}

void TestSceneValidator::countsSteepGradientWarnings_data() {
	QTest::addColumn<double>("firstGradient");
	QTest::addColumn<double>("secondGradient");
	QTest::addColumn<int>("warnings");
	QTest::newRow("an ascent steeper than the starting limit gives a warning") << 0.0 << 0.15 << 1;
	QTest::newRow("an ascent above the braking limit but below the starting limit gives no warning") << 0.0 << 0.09 << 0;
	QTest::newRow("arcs just below both limits give no warning") << -0.08 << 0.14 << 0;
}

void TestSceneValidator::countsSteepGradientWarnings() {
	QFETCH(double, firstGradient);
	QFETCH(double, secondGradient);
	QFETCH(int, warnings);
	QCOMPARE(steepGradientWarningCount(steepGradientScene(firstGradient, secondGradient)), warnings);
}

void TestSceneValidator::reversedRouteTurnsAscentIntoDescent() {
	SceneModel reversedRoute = steepGradientScene(0.09, 0.0);
	reversedRoute.routes[0].blocks = {"block-2", "block-1"};
	// An ascent in the stored direction is a descent on a reversed route.
	QCOMPARE(buildSceneRouteTraversal(reversedRoute, reversedRoute.routes[0]).direction, -1);
	QCOMPARE(steepGradientWarningCount(reversedRoute), 1);
}

void TestSceneValidator::reversedRouteTurnsDescentIntoAscent() {
	SceneModel reversedRoute = steepGradientScene(-0.09, 0.0);
	reversedRoute.routes[0].blocks = {"block-2", "block-1"};
	// A descent in the stored direction is an ascent below the starting limit on a reversed route.
	QCOMPARE(steepGradientWarningCount(reversedRoute), 0);
}

void TestSceneValidator::servicesOfOneCompositionShareWarning() {
	SceneModel twoServices = steepGradientScene(-0.09, 0.0);
	twoServices.services.push_back(twoServices.services.front());
	twoServices.services.back().id = "service-2";
	// Services with the same route and composition share one warning.
	QCOMPARE(steepGradientWarningCount(twoServices), 1);
}

void TestSceneValidator::judgesEachCompositionOnItsOwnLimit() {
	const std::string steepCode = "scene.route.gradient.steep";
	SceneModel twoCompositions = steepGradientScene(-0.1, 0.0);
	twoCompositions.trainUnits.push_back(paimpolUnit());
	twoCompositions.compositions.push_back({"composition-2", {"unit-2"}});
	twoCompositions.services.push_back(twoCompositions.services.front());
	twoCompositions.services.back().id = "service-2";
	twoCompositions.services.back().composition = "composition-2";
	const auto twoCompositionDiagnostics = validateScene(twoCompositions);
	// A composition with a larger deceleration is judged on its own limit.
	QCOMPARE(steepGradientWarningCount(twoCompositions), 1);
	QVERIFY(contains(findCode(twoCompositionDiagnostics, steepCode)->message, "composition-1"));
	QVERIFY(!contains(findCode(twoCompositionDiagnostics, steepCode)->message, "composition-2"));
	twoCompositions.arcs[0].gradientPercent = -0.2;
	// Each composition on the route gets its own warning.
	QCOMPARE(steepGradientWarningCount(twoCompositions), 2);
}

void TestSceneValidator::skipsGradientOfUnknownComposition() {
	SceneModel unknownComposition = steepGradientScene(-0.09, 0.0);
	unknownComposition.services[0].composition = "missing";
	// A service without a resolvable composition gets no gradient warning.
	QCOMPARE(steepGradientWarningCount(unknownComposition), 0);
}

void TestSceneValidator::skipsGradientOfUnresolvedRoute() {
	SceneModel unknownRoute = steepGradientScene(-0.09, 0.0);
	unknownRoute.routes[0].blocks = {"missing-block"};
	// A route without resolvable sections gets no gradient warning.
	QCOMPARE(steepGradientWarningCount(unknownRoute), 0);
}

void TestSceneValidator::boundsPhysicalValues_data() {
	QTest::addColumn<int>("index");
	for (int index = 0; index < static_cast<int>(kSceneTrainPhysicalFields.size()); ++index)
		QTest::newRow(kSceneTrainPhysicalFields[index].key) << index;
}

void TestSceneValidator::boundsPhysicalValues() {
	QFETCH(int, index);
	const SceneTrainPhysicalField& field = kSceneTrainPhysicalFields[index];
	SceneModel scene = completeScene();
	checkRange(scene, scene.trainUnits[0].physical.*field.member, field.range,
		std::string("train_units[0].physical.") + field.key);
}

void TestSceneValidator::boundsTractionValues_data() {
	QTest::addColumn<int>("column");
	for (int column = 0; column < static_cast<int>(kSceneTractionColumns.size()); ++column)
		QTest::newRow(kSceneTractionColumns[column].key) << column;
}

void TestSceneValidator::boundsTractionValues() {
	QFETCH(int, column);
	SceneModel scene = completeScene();
	checkRange(scene, scene.trainUnits[0].tractionCurve[0][column], kSceneTractionColumns[column].range,
		"train_units[0].traction_curve[0][" + std::to_string(column) + "]");
}

void TestSceneValidator::namesAtMostFiveSectionsOfLongRoute() {
	const std::string levelMissing = "scene.signalling.level.missing";
	SceneModel longRoute = completeScene();
	longRoute.nodes.clear();
	longRoute.arcs.clear();
	longRoute.blocks.clear();
	longRoute.connections.clear();
	longRoute.routes[0].blocks.clear();
	longRoute.signallingAreas.clear();
	for (int index = 0; index <= 200; ++index)
		longRoute.nodes.push_back({"n" + std::to_string(index), "track-1", static_cast<double>(index), 0.0});
	for (int index = 0; index < 200; ++index) {
		longRoute.arcs.push_back({"a" + std::to_string(index), "track-1", "n" + std::to_string(index),
			"n" + std::to_string(index + 1), 0.0, 0.0, 40.0});
		longRoute.blocks.push_back({"b" + std::to_string(index), "track-1", 1.0});
		longRoute.routes[0].blocks.push_back("b" + std::to_string(index));
	}
	const auto longDiagnostics = validateRunnableScene(longRoute);
	const SceneDiagnostic* longWarning = findCode(longDiagnostics, levelMissing);
	// A long route gets one warning that names at most five sections.
	QCOMPARE(std::count_if(longDiagnostics.begin(), longDiagnostics.end(), [&](const SceneDiagnostic& d) { return d.code == levelMissing; }),
		std::ptrdiff_t(1));
	if (longWarning == nullptr)
		QFAIL("the warning is not reported");
	QVERIFY(contains(longWarning->message, "200 of 200 route sections"));
	QVERIFY(contains(longWarning->message, "@b4@ (track-1 4.000 to 5.000 km) and 195 more"));
	QVERIFY(!contains(longWarning->message, "@b5@"));
	QVERIFY(contains(longWarning->message, "(track track-1)"));
}

void TestSceneValidator::acceptsTrackOfManyNodes() {
	SceneModel longTrack = completeScene();
	for (int index = 4; index <= 1601; ++index)
		longTrack.nodes.push_back({"node-" + std::to_string(index), "track-1", static_cast<double>(index - 1), 0.0});
	for (int index = 3; index <= 1600; ++index)
		longTrack.arcs.push_back({"arc-" + std::to_string(index), "track-1", "node-" + std::to_string(index),
			"node-" + std::to_string(index + 1), 0.0, 0.0, 40.0});
	for (int index = 3; index <= 96; ++index)
		longTrack.blocks.push_back({"block-" + std::to_string(index), "track-1", 17.0});
	QVERIFY2(!hasCode(validateRunnableScene(longTrack), "scene.capacity.runtime"),
		"a track with more than 1500 nodes and arcs is not rejected for size");
}

void TestSceneValidator::acceptsSceneOfManySections() {
	SceneModel manySections = completeScene();
	for (int index = 4; index <= 6002; ++index)
		manySections.nodes.push_back({"node-" + std::to_string(index), "track-1", static_cast<double>(index - 1), 0.0});
	for (int index = 3; index <= 6001; ++index) {
		manySections.arcs.push_back({"arc-" + std::to_string(index), "track-1", "node-" + std::to_string(index),
			"node-" + std::to_string(index + 1), 0.0, 0.0, 40.0});
		manySections.blocks.push_back({"block-" + std::to_string(index), "track-1", 1.0});
	}
	QVERIFY2(!hasCode(validateRunnableScene(manySections), "scene.capacity.runtime"),
		"a scene with more than 6000 sections is not rejected for size");
}

void TestSceneValidator::suggestsNegativeChainage() {
	SceneModel negativeChainage = completeScene();
	negativeChainage.nodes = {{"node-1", "track-1", -2.0, 0.0}, {"node-2", "track-1", -1.0, 0.0},
		{"node-3", "track-1", 0.0, 0.0}};
	negativeChainage.connections.clear();
	negativeChainage.signallingAreas.clear();
	const auto negativeDiagnostics = validateRunnableScene(negativeChainage);
	const SceneDiagnostic* negativeWarning = findCode(negativeDiagnostics, "scene.signalling.level.missing");
	// The suggested area range reports negative chainage.
	if (negativeWarning == nullptr)
		QFAIL("the warning is not reported");
	QVERIFY(contains(negativeWarning->suggestedFix, "-2.000000 to 0.000000 km"));
}

void TestSceneValidator::conflictingSectionHasNoLevelWarning() {
	SceneModel conflictingNetworkAreas = completeScene();
	conflictingNetworkAreas.signallingAreas = {
		{"first", 0.0, 2.0, 2, {}}, {"second", 0.0, 2.0, 3, {}}};
	const auto conflictDiagnostics = validateRunnableScene(conflictingNetworkAreas);
	// A section with conflicting areas does not also get the missing level warning.
	QVERIFY(hasCode(conflictDiagnostics, "scene.signalling_area.conflict"));
	QVERIFY(!hasCode(conflictDiagnostics, "scene.signalling.level.missing"));
}

void TestSceneValidator::rejectsBrokenScene_data() {
	QTest::addColumn<int>("index");
	for (int index = 0; index < static_cast<int>(std::size(kFailureCases)); ++index)
		QTest::newRow(kFailureCases[index].name) << index;
}

void TestSceneValidator::rejectsBrokenScene() {
	QFETCH(int, index);
	const FailureCase& test = kFailureCases[index];
	SceneModel broken = completeScene();
	test.mutate(broken);
	const auto diagnostics = validateRunnableScene(broken);
	QVERIFY(hasCode(diagnostics, test.code));
	if (test.path)
		QVERIFY(hasCodeAndPath(diagnostics, test.code, test.path));
}

void TestSceneValidator::rejectsReservedSlashInBlockId() {
	SceneModel reservedBlockId = completeScene();
	reservedBlockId.blocks[0].id = "Depot/1";
	reservedBlockId.routes[0].blocks[0] = "Depot/1";
	reservedBlockId.signals[0].protectedSection = "Depot/1";
	const auto reservedBlockIdDiagnostics = validateRunnableScene(reservedBlockId);
	// Reserved slash in block id reports one actionable validation error.
	QCOMPARE(reservedBlockIdDiagnostics.size(), std::size_t(1));
	QVERIFY(hasCodeAndPath(reservedBlockIdDiagnostics, "scene.id.reserved", "blocks[0].id"));
}

void TestSceneValidator::reducedBreakdownMayOmitEnd() {
	const SceneModel reducedBreakdown = reducedBreakdownScene();
	QVERIFY2(validateRunnableScene(reducedBreakdown).empty(),
		"reduced-speed breakdown may omit recovery end");
}

void TestSceneValidator::durationOverrideAppliesToOccurrences() {
	SceneModel extendedOccurrence = reducedBreakdownScene();
	extendedOccurrence.settings.durationSeconds = 1.0;
	extendedOccurrence.services[0].hasRepeat = true;
	extendedOccurrence.services[0].headwaySeconds = 10.0;
	extendedOccurrence.scenarios[0].incidents[0].occurrence = 2;
	extendedOccurrence.scenarios[0].entranceDelays[0].occurrence = 2;
	const auto savedHorizonDiagnostics = validateRunnableScene(extendedOccurrence);
	// Saved horizon rejects occurrence-specific rows outside its repeat pattern.
	QVERIFY(hasCode(savedHorizonDiagnostics, "scene.occurrence.invalid"));
	QVERIFY(hasCode(savedHorizonDiagnostics, "scene.entrance.occurrence.out_of_horizon"));
	const auto extendedHorizonDiagnostics = validateRunnableScene(
		extendedOccurrence, {}, std::optional<double>(20.0));
	// Duration override applies to breakdown and entrance-delay occurrences.
	QVERIFY(!hasCode(extendedHorizonDiagnostics, "scene.occurrence.invalid"));
	QVERIFY(!hasCode(extendedHorizonDiagnostics, "scene.entrance.occurrence.out_of_horizon"));
}

void TestSceneValidator::fullHoldBreakdownNeedsEnd() {
	SceneModel fullHoldWithoutEnd = reducedBreakdownScene();
	fullHoldWithoutEnd.scenarios[0].incidents[0].hasReducedSpeed = false;
	fullHoldWithoutEnd.scenarios[0].incidents[0].reducedSpeedKmh = 0.0;
	QVERIFY2(hasCode(validateRunnableScene(fullHoldWithoutEnd), "scene.incident.window"),
		"legacy full-hold breakdown requires an end");
}

void TestSceneValidator::breakdownOccurrenceMustBePositive() {
	SceneModel invalidBreakdown = reducedBreakdownScene();
	invalidBreakdown.scenarios[0].incidents[0].occurrence = 0;
	QVERIFY2(hasCode(validateRunnableScene(invalidBreakdown), "scene.occurrence.invalid"),
		"breakdown occurrence must be positive");
}

void TestSceneValidator::breakdownOccurrenceMustBeInRepeatRange() {
	SceneModel invalidBreakdown = reducedBreakdownScene();
	invalidBreakdown.services[0].hasRepeat = true;
	invalidBreakdown.services[0].headwaySeconds = 30.0;
	invalidBreakdown.services[0].hasRepeatCount = true;
	invalidBreakdown.services[0].repeatCount = 1;
	invalidBreakdown.scenarios[0].incidents[0].occurrence = 2;
	QVERIFY2(hasCode(validateRunnableScene(invalidBreakdown), "scene.occurrence.invalid"),
		"breakdown occurrence must be inside the configured repeat range");
}

void TestSceneValidator::reducedBreakdownSpeedMustBePositive() {
	SceneModel invalidBreakdown = reducedBreakdownScene();
	invalidBreakdown.scenarios[0].incidents[0].reducedSpeedKmh = 0.0;
	QVERIFY2(hasCode(validateRunnableScene(invalidBreakdown), "scene.incident.speed"),
		"reduced breakdown speed must be positive");
}

void TestSceneValidator::breakdownEndMustFollowStart() {
	SceneModel invalidBreakdown = reducedBreakdownScene();
	invalidBreakdown.scenarios[0].incidents[0].hasEndSeconds = true;
	invalidBreakdown.scenarios[0].incidents[0].endSeconds = 300.0;
	QVERIFY2(hasCode(validateRunnableScene(invalidBreakdown), "scene.incident.window"),
		"breakdown recovery end must be after start");
}

void TestSceneValidator::signalFailureRejectsBreakdownFields() {
	SceneModel invalidBreakdown = reducedBreakdownScene();
	invalidBreakdown.scenarios[0].incidents[0].terminateAtDestination = true;
	invalidBreakdown.scenarios[0].incidents[0].type = "signal_failure";
	invalidBreakdown.scenarios[0].incidents[0].target = "signal-1";
	QVERIFY2(hasCode(validateRunnableScene(invalidBreakdown), "scene.incident.fields"),
		"signal failures reject breakdown-only fields");
}

void TestSceneValidator::expandsRepeatedService() {
	SceneService repeated = completeScene().services[0];
	repeated.operatingCode = "1723";
	repeated.hasRepeat = true;
	repeated.headwaySeconds = 30.0;
	repeated.hasRepeatCount = true;
	repeated.repeatCount = 3;
	repeated.hasOperatingCodeStep = true;
	repeated.operatingCodeStep = 2;
	// Explicit repeat count overrides the duration horizon.
	QCOMPARE(sceneServiceOccurrenceCount(repeated, 1.0), 3);
	// Decimal operating-code step expands occurrences predictably.
	QCOMPARE(sceneServiceOccurrenceOperatingCode(repeated, 1), "1723");
	QCOMPARE(sceneServiceOccurrenceOperatingCode(repeated, 2), "1725");
	QCOMPARE(sceneServiceOccurrenceOperatingCode(repeated, 3), "1727");
}

void TestSceneValidator::namesOccurrencesWithoutStep() {
	SceneService readableRepeat = completeScene().services[0];
	readableRepeat.hasRepeat = true;
	readableRepeat.headwaySeconds = 30.0;
	// Repeated services without a step expose readable occurrence codes.
	QCOMPARE(sceneServiceOccurrenceOperatingCode(readableRepeat, 2), "service-1-2");
}

void TestSceneValidator::allowsNegativeArcGradient() {
	SceneModel negativeGradient = completeScene();
	negativeGradient.arcs[0].gradientPercent = -100.0;
	QVERIFY2(validateScene(negativeGradient).empty(), "negative arc gradient is allowed");
}

void TestSceneValidator::keepsCoordinateTolerance() {
	SceneModel coordinateTolerance = completeScene();
	coordinateTolerance.nodes[1].xKm = -5e-9;
	QVERIFY2(validateScene(coordinateTolerance).empty(), "native coordinate tolerance is preserved");
}

void TestSceneValidator::rejectsBlockLayoutOverflow() {
	SceneModel overflowingBlocks = completeScene();
	overflowingBlocks.blocks[0].lengthKm = 2.1;
	QVERIFY2(hasCode(validateRunnableScene(overflowingBlocks), "scene.capacity.runtime"),
		"block layout overflow is rejected");
}

void TestSceneValidator::warnsAboutClippedFinalBlock() {
	SceneModel clippedFinalBlock = completeScene();
	clippedFinalBlock.blocks[1].lengthKm = 1.5;
	QVERIFY2(hasCode(validateRunnableScene(clippedFinalBlock), "scene.native.block.clipped"),
		"an overlong final block retains its compatibility clipping warning");
}

void TestSceneValidator::enforcesBlockArcCapacity() {
	SceneModel twentyOneArcBlock = completeScene();
	std::string previousNode = twentyOneArcBlock.nodes.back().id;
	for (int index = 3; index <= 22; ++index) {
		const std::string node = "long-node-" + std::to_string(index);
		twentyOneArcBlock.nodes.push_back({node, "track-1", static_cast<double>(index), 0.0});
		twentyOneArcBlock.arcs.push_back({"long-arc-" + std::to_string(index), "track-1",
			previousNode, node, 0.0, 0.0, 35.0});
		previousNode = node;
	}
	twentyOneArcBlock.blocks[0].lengthKm = 21.0;
	QVERIFY2(hasCode(validateRunnableScene(twentyOneArcBlock), "scene.capacity.runtime"),
		"block section arc capacity is enforced");
}

void TestSceneValidator::rejectsDerivedSectionIdCollision() {
	SceneModel derivedIdCollision = completeScene();
	derivedIdCollision.tracks.push_back({"track-2"});
	derivedIdCollision.nodes.push_back({"node-4", "track-2", 3.0, 0.0});
	derivedIdCollision.nodes.push_back({"node-5", "track-2", 4.0, 0.0});
	derivedIdCollision.arcs.push_back({"arc-3", "track-2", "node-4", "node-5", 0.0, 0.0, 35.0});
	derivedIdCollision.blocks.push_back({"block-3", "track-2", 1.0});
	derivedIdCollision.connections.push_back({"switch-1", "node-2", "node-4", false, 0.0});
	derivedIdCollision.connections.push_back({"switch-2", "node-2", "node-4", false, 0.0});
	QVERIFY2(hasCode(validateRunnableScene(derivedIdCollision), "scene.capacity.runtime"),
		"derived switch section ID collisions are rejected");
}

void TestSceneValidator::rejectsRuntimeBlockIdCollision() {
	SceneModel runtimeIdCollision = completeScene();
	runtimeIdCollision.blocks[0].id = "block.a";
	runtimeIdCollision.blocks[1].id = "@block.a@";
	runtimeIdCollision.routes[0].blocks = {"block.a", "@block.a@"};
	QVERIFY2(hasCode(validateRunnableScene(runtimeIdCollision), "scene.capacity.runtime"),
		"runtime block ID normalization rejects collisions");
	SceneModel incompleteRuntimeIdCollision = runtimeIdCollision;
	incompleteRuntimeIdCollision.trainUnits.clear();
	incompleteRuntimeIdCollision.compositions.clear();
	incompleteRuntimeIdCollision.services.clear();
	QVERIFY2(hasCode(validateRunnableScene(incompleteRuntimeIdCollision), "scene.capacity.runtime"),
		"unrelated incomplete authoring does not hide infrastructure runtime errors");
}

void TestSceneValidator::enforcesRouteTokenCapacity() {
	SceneModel routeCapacity = completeScene();
	routeCapacity.routes[0].blocks.assign(601, "block-1");
	QVERIFY2(hasCode(validateRunnableScene(routeCapacity), "scene.capacity.runtime"),
		"route block-token capacity is enforced");
}

void TestSceneValidator::enforcesEndpointFanout() {
	SceneModel endpointFanout = completeScene();
	for (int index = 0; index < 6; ++index)
		endpointFanout.connections.push_back({"fanout-" + std::to_string(index), "node-1", "node-3", false, 0.0});
	QVERIFY2(hasCode(validateRunnableScene(endpointFanout), "scene.capacity.runtime"),
		"node endpoint fanout capacity is enforced");
}

void TestSceneValidator::enforcesDependencyFanout() {
	SceneModel dependencyFanout = completeScene();
	dependencyFanout.blocks[0].lengthKm = 0.2;
	dependencyFanout.blocks[1].lengthKm = 0.18;
	dependencyFanout.blockDependencies.push_back({"block-1", "block-2"});
	for (int index = 0; index < 9; ++index) {
		const std::string target = "dependency-target-" + std::to_string(index);
		dependencyFanout.blocks.push_back({target, "track-1", 0.18});
		dependencyFanout.blockDependencies.push_back({"block-1", target});
	}
	QVERIFY2(hasCode(validateRunnableScene(dependencyFanout), "scene.capacity.runtime"),
		"derived and explicit dependency fanout capacity is enforced");
}

// Composite route entries resolve each basic block after stripping @...@ positions.
void TestSceneValidator::validatesCompositeRouteEntries() {
	SceneModel composite = completeScene();
	composite.routes[0].blocks = {"@block-1@-0.000000/@block-2@-1.000000"};
	composite.blockDependencies.push_back({"@block-1@-0.000000/@block-2@-1.000000", "block-1"});
	composite.singleTrackRestrictions.push_back({"block-1", "block-2",
		"block-1", "block-2"});
	composite.stationBoundaries.push_back({"block-1", true, "block-2", false});
	QVERIFY2(validateScene(composite).empty(), "composite route block components validate");
	composite.scenarios[0].incidents[0].target = "block-2";
	QVERIFY2(validateScene(composite).empty(), "signal failure accepts a basic block target");
}

void TestSceneValidator::countsOccurrencesInWindow() {
	SceneService counted;
	counted.id = "counted";
	counted.stops.emplace_back();
	counted.stops.front().hasPlannedDeparture = true;
	counted.stops.front().plannedDepartureSeconds = 120.0;
	counted.hasRepeat = true;
	counted.headwaySeconds = 60.0;
	counted.hasRepeatCount = true;
	counted.repeatCount = 5;
	// Implicit first departure and exclusive horizon determine in-period count.
	QCOMPARE(sceneServiceInWindowCount(counted, 240.0), 2);
	counted.hasEntryTime = true;
	counted.entryTimeSeconds = -60.0;
	// Explicit entry wins and negative entries are outside the horizon.
	QCOMPARE_EQ(sceneServiceScheduledEntry(counted, 2), 0.0);
	QCOMPARE(sceneServiceInWindowCount(counted, 180.0), 3);
	// Selected in-period count retains stable identity and horizon limits.
	QCOMPARE(sceneServiceInWindowCount(counted, 180.0,
				 {{"counted", 1}, {"counted", 2}, {"counted", 5}, {"other", 2}}),
		1);
	counted.repeatCount = std::numeric_limits<int>::max();
	// Large configured totals stay intact while window counting is bounded.
	QCOMPARE(sceneServiceInWindowCount(counted, 180.0), 3);
	QCOMPARE(sceneServiceOccurrenceCount(counted, 180.0), counted.repeatCount);
}

void TestSceneValidator::validatesHistoricalFixture() {
	const std::string fixtureDir = EGTRAIN_TEST_SOURCE_DIR "/fixtures/scenes/minimal";
	QVERIFY2(!hasErrors(validateSceneStructure(fixtureDir)),
		"historical fixture remains structurally loadable");
	const auto runnableFixture = validateRunnableSceneDirectory(fixtureDir);
	QVERIFY2(hasCode(runnableFixture, "scene.topology.tracks.none"),
		"incomplete historical fixture fails runnable validation");
}

void TestSceneValidator::reportsMissingSceneDirectory() {
	QVERIFY2(hasCode(validateSceneStructure("/no/such/scene/dir"), "scene.dir.missing"),
		"missing directory reports structural error");
}

// A dialog shows the message and the fix as two paragraphs, and only the message when there is no fix.
void TestSceneValidator::formatsDialogText() {
	SceneDiagnostic dialogDiagnostic;
	dialogDiagnostic.message = "Service s1 has no route";
	// A diagnostic without a fix shows its message alone.
	QCOMPARE(toDialogText(dialogDiagnostic), "Service s1 has no route");
	dialogDiagnostic.suggestedFix = "Choose a route";
	// A diagnostic with a fix shows the fix as a second paragraph.
	QCOMPARE(toDialogText(dialogDiagnostic), "Service s1 has no route\n\nChoose a route");
}

QTEST_APPLESS_MAIN(TestSceneValidator)
#include "test_scenevalidator.moc"
