#include "scene/SceneModel.h"
#include "scene/SceneValidator.h"

#include <iostream>
#include <limits>
#include <string>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static bool hasCode(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.code == code)
			return true;
	}
	return false;
}

static bool hasCodeAndPath(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code,
		const std::string& path) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.code == code && diagnostic.path == path)
			return true;
	}
	return false;
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

	scene.signals.push_back({"signal-1"});
	scene.routes.push_back({"route-1", {"block-1", "block-2"}, false, "", false});

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

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: test_scenevalidator <fixture_dir>\n";
		return 1;
	}

	bool ok = true;
	const SceneModel clean = completeScene();
	ok &= expect(!hasCode(validateScene(clean), "scene.topology.tracks.none"),
			"semantic validation does not reject complete topology");
	ok &= expect(validateScene(clean).empty(), "complete scene passes semantic validation");
	ok &= expect(validateRunnableScene(clean).empty(), "complete scene passes runnable validation");
	SceneModel validAreas = clean;
	validAreas.signallingAreas = {
		{"network-area", 0.0, 2.0, 2, {}},
		{"track-area", 0.25, 1.75, 4, "track-1"},
	};
	ok &= expect(validateScene(validAreas).empty(),
			"network-wide and track-scoped signalling areas validate");
	SceneModel duplicateArea = clean;
	duplicateArea.signallingAreas = {{"area", 0.0, 1.0, 2, {}}, {"area", 1.0, 2.0, 2, {}}};
	ok &= expect(hasCode(validateScene(duplicateArea), "scene.id.duplicate"),
			"signalling area IDs must be unique");
	SceneModel invalidAreaRange = clean;
	invalidAreaRange.signallingAreas = {{"area", 1.0, 1.0, 2, {}}};
	ok &= expect(hasCode(validateScene(invalidAreaRange), "scene.signalling_area.range"),
			"signalling area ranges must increase");
	invalidAreaRange.signallingAreas[0].startKm = std::numeric_limits<double>::quiet_NaN();
	ok &= expect(hasCode(validateScene(invalidAreaRange), "scene.signalling_area.range"),
			"signalling area coordinates must be finite");
	SceneModel invalidAreaLevel = clean;
	invalidAreaLevel.signallingAreas = {{"area", 0.0, 1.0, 6, {}}};
	ok &= expect(hasCode(validateScene(invalidAreaLevel), "scene.signalling_area.level"),
			"signalling area levels must be between zero and five");
	SceneModel unknownAreaTrack = clean;
	unknownAreaTrack.signallingAreas = {{"area", 0.0, 1.0, 2, "missing-track"}};
	ok &= expect(hasCodeAndPath(validateScene(unknownAreaTrack), "scene.ref.unresolved",
			"signalling_areas[0].track"), "signalling area track references must resolve");
	SceneModel overlapWithoutSharedSection = clean;
	overlapWithoutSharedSection.signallingAreas = {
		{"first", 0.0, 1.5, 2, {}}, {"second", 1.0, 2.0, 3, {}}};
	ok &= expect(!hasCode(validateRunnableScene(overlapWithoutSharedSection),
			"scene.signalling_area.conflict"),
			"coordinate overlap is allowed when no complete section receives both levels");
	SceneModel conflictingNetworkAreas = clean;
	conflictingNetworkAreas.signallingAreas = {
		{"first", 0.0, 2.0, 2, {}}, {"second", 0.0, 2.0, 3, {}}};
	ok &= expect(hasCode(validateRunnableScene(conflictingNetworkAreas),
			"scene.signalling_area.conflict"),
			"network-wide areas cannot assign different levels to one runtime section");
	SceneModel conflictingTrackAreas = clean;
	conflictingTrackAreas.signallingAreas = {
		{"first", 0.0, 2.0, 2, "track-1"}, {"second", 0.0, 2.0, 3, "track-1"}};
	ok &= expect(hasCode(validateRunnableScene(conflictingTrackAreas),
			"scene.signalling_area.conflict"),
			"same-track areas cannot assign different levels to one runtime section");
	SceneModel conflictingDerivedAreas = clean;
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
	ok &= expect(hasCode(validateRunnableScene(conflictingDerivedAreas),
			"scene.signalling_area.conflict"),
			"different track scopes cannot conflict on one derived switch section");

	struct FailureCase {
		const char* name;
		void (*mutate)(SceneModel&);
		const char* code;
		const char* path = nullptr;
	};
	const FailureCase cases[] = {
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
			}, "scene.ref.unresolved"},
		{"missing base time", [](SceneModel& scene) { scene.baseTime.clear(); }, "scene.basetime.missing"},
		{"out-of-range base time", [](SceneModel& scene) { scene.baseTime = "24:00:00"; },
				"scene.basetime.invalid"},
		{"non-positive duration", [](SceneModel& scene) { scene.settings.durationSeconds = 0.0; },
				"scene.duration.invalid"},
		{"non-finite duration", [](SceneModel& scene) {
				scene.settings.durationSeconds = std::numeric_limits<double>::quiet_NaN();
			}, "scene.duration.invalid"},
		{"performance below range", [](SceneModel& scene) {
				scene.services[0].performancePercent = 0.5;
			}, "scene.performance.invalid", "services[service-1].performance_percent"},
		{"non-finite maximum speed", [](SceneModel& scene) {
				scene.services[0].hasMaximumSpeed = true;
				scene.services[0].maximumSpeedKmh = std::numeric_limits<double>::infinity();
			}, "scene.speed.invalid", "services[service-1].maximum_speed_kmh"},
		{"non-positive repeat count", [](SceneModel& scene) {
				scene.services[0].hasRepeat = true;
				scene.services[0].headwaySeconds = 30.0;
				scene.services[0].hasRepeatCount = true;
				scene.services[0].repeatCount = 0;
			}, "scene.repeat.count.invalid", "services[service-1].repeat.count"},
		{"non-decimal operating-code step", [](SceneModel& scene) {
				scene.services[0].operatingCode = "R100";
				scene.services[0].hasRepeat = true;
				scene.services[0].headwaySeconds = 30.0;
				scene.services[0].hasOperatingCodeStep = true;
				scene.services[0].operatingCodeStep = 2;
			}, "scene.repeat.step.invalid", "services[service-1].repeat.operating_code_step"},
		{"overflowing operating-code step", [](SceneModel& scene) {
				scene.services[0].operatingCode = "9223372036854775806";
				scene.services[0].hasRepeat = true;
				scene.services[0].headwaySeconds = 30.0;
				scene.services[0].hasRepeatCount = true;
				scene.services[0].repeatCount = 2;
				scene.services[0].hasOperatingCodeStep = true;
				scene.services[0].operatingCodeStep = 2;
			}, "scene.repeat.step.invalid", "services[service-1].repeat.operating_code_step"},
		{"entrance delay beyond explicit pattern", [](SceneModel& scene) {
				scene.services[0].hasRepeat = true;
				scene.services[0].headwaySeconds = 30.0;
				scene.services[0].hasRepeatCount = true;
				scene.services[0].repeatCount = 1;
				scene.scenarios[0].entranceDelays[0].occurrence = 2;
			}, "scene.entrance.occurrence.out_of_horizon", "scenarios[0].entrance_delays[0].occurrence"},
		{"non-finite node coordinate", [](SceneModel& scene) {
				scene.nodes[0].xKm = std::numeric_limits<double>::quiet_NaN();
			}, "scene.node.coordinate.invalid", "nodes[0].x_km"},
		{"negative arc curvature", [](SceneModel& scene) { scene.arcs[0].curvatureRadiusM = -1.0; },
				"scene.arc.curvature.invalid", "arcs[0].curvature_radius_m"},
		{"non-positive arc speed", [](SceneModel& scene) { scene.arcs[0].speedLimitMs = 0.0; },
				"scene.arc.speed.invalid", "arcs[0].speed_limit_ms"},
		{"non-positive block length", [](SceneModel& scene) { scene.blocks[0].lengthKm = 0.0; },
				"scene.block.length.invalid", "blocks[0].length_km"},
		{"invalid optional connection speed", [](SceneModel& scene) {
				scene.connections[0].hasSpeedLimit = true;
				scene.connections[0].speedLimitMs = 0.0;
			}, "scene.connection.speed.invalid", "connections[0].speed_limit_ms"},
		{"arc endpoint on another track", [](SceneModel& scene) {
				scene.tracks.push_back({"track-2"});
				scene.nodes[1].trackId = "track-2";
			}, "scene.topology.track", "arcs[0].to"},
		{"arc self-loop", [](SceneModel& scene) { scene.arcs[0].toNodeId = "node-1"; },
				"scene.topology.loop", "arcs[0].to"},
		{"ambiguous outgoing arc", [](SceneModel& scene) { scene.arcs[1].fromNodeId = "node-1"; },
				"scene.topology.ambiguous", "tracks[0].nodes"},
		{"disconnected node", [](SceneModel& scene) {
				scene.nodes.push_back({"node-4", "track-1", 3.0, 0.0});
			}, "scene.topology.disconnected", "tracks[0].nodes"},
		{"descending chain order", [](SceneModel& scene) { scene.nodes[1].xKm = -1.0; },
				"scene.topology.order", "arcs[0].to"},
		{"empty tracks", [](SceneModel& scene) { scene.tracks.clear(); }, "scene.topology.tracks.none"},
		{"empty nodes", [](SceneModel& scene) { scene.nodes.clear(); }, "scene.topology.nodes.none"},
		{"empty arcs", [](SceneModel& scene) { scene.arcs.clear(); }, "scene.topology.arcs.none"},
		{"empty blocks", [](SceneModel& scene) { scene.blocks.clear(); }, "scene.topology.blocks.none"},
		{"empty routes", [](SceneModel& scene) { scene.routes.clear(); }, "scene.routes.none"},
		{"unbound platform", [](SceneModel& scene) { scene.stations[0].platforms[0].nodeIds.clear(); },
				"scene.platform.nodes.none", "stations[0].platforms[0].nodes"},
		{"unanchored station", [](SceneModel& scene) { scene.stations[0].platforms.clear(); },
				"scene.station.anchor.missing", "stations[0]"},
		{"non-finite station position", [](SceneModel& scene) {
				scene.stations[0].hasPosition = true;
				scene.stations[0].positionKm = std::numeric_limits<double>::quiet_NaN();
			}, "scene.station.position.invalid", "stations[0].position_km"},
		{"conflicting platform node", [](SceneModel& scene) {
				scene.stations[1].platforms[0].nodeIds = {"node-1"};
			}, "scene.platform.node.conflict", "stations[1].platforms[0].nodes[0]"},
		{"platform outside service route", [](SceneModel& scene) {
				scene.tracks.push_back({"track-2"});
				scene.nodes.push_back({"node-4", "track-2", 0.0, 1.0});
				scene.nodes.push_back({"node-5", "track-2", 1.0, 1.0});
				scene.arcs.push_back({"arc-3", "track-2", "node-4", "node-5", 0.0, 0.0, 35.0});
				scene.blocks.push_back({"block-3", "track-2", 1.0});
				scene.stations[1].platforms[0].nodeIds = {"node-5"};
			}, "scene.ref.platform.route", "services[service-1].stops[1].platform"},
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
			}, "scene.ref.platform.route", "services[service-1].stops[1].platform"},
		{"unknown route block", [](SceneModel& scene) { scene.routes[0].blocks[0] = "missing-block"; },
				"scene.ref.unresolved"},
		{"unknown default scenario", [](SceneModel& scene) { scene.defaultScenarioId = "missing"; },
				"scene.ref.unresolved"},
		{"discontinuous passenger leg", [](SceneModel& scene) {
				scene.passengers[0].journeys[0].legs[0].destinationStationId = "station-1";
			}, "scene.passenger.continuity"},
	};
	for (const auto& test : cases) {
		SceneModel broken = clean;
		test.mutate(broken);
		const auto diagnostics = validateRunnableScene(broken);
		bool passed = hasCode(diagnostics, test.code);
		if (test.path)
			passed = passed && hasCodeAndPath(diagnostics, test.code, test.path);
		ok &= expect(passed, test.name);
	}
	SceneModel reducedBreakdown = clean;
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
	ok &= expect(validateRunnableScene(reducedBreakdown).empty(),
			"reduced-speed breakdown may omit recovery end");

	SceneModel fullHoldWithoutEnd = reducedBreakdown;
	fullHoldWithoutEnd.scenarios[0].incidents[0].hasReducedSpeed = false;
	fullHoldWithoutEnd.scenarios[0].incidents[0].reducedSpeedKmh = 0.0;
	ok &= expect(hasCode(validateRunnableScene(fullHoldWithoutEnd), "scene.incident.window"),
			"legacy full-hold breakdown requires an end");

	SceneModel invalidBreakdown = reducedBreakdown;
	invalidBreakdown.scenarios[0].incidents[0].occurrence = 0;
	ok &= expect(hasCode(validateRunnableScene(invalidBreakdown), "scene.occurrence.invalid"),
			"breakdown occurrence must be positive");
	invalidBreakdown = reducedBreakdown;
	invalidBreakdown.services[0].hasRepeat = true;
	invalidBreakdown.services[0].headwaySeconds = 30.0;
	invalidBreakdown.services[0].hasRepeatCount = true;
	invalidBreakdown.services[0].repeatCount = 1;
	invalidBreakdown.scenarios[0].incidents[0].occurrence = 2;
	ok &= expect(hasCode(validateRunnableScene(invalidBreakdown), "scene.occurrence.invalid"),
			"breakdown occurrence must be inside the configured repeat range");
	invalidBreakdown = reducedBreakdown;
	invalidBreakdown.scenarios[0].incidents[0].reducedSpeedKmh = 0.0;
	ok &= expect(hasCode(validateRunnableScene(invalidBreakdown), "scene.incident.speed"),
			"reduced breakdown speed must be positive");
	invalidBreakdown = reducedBreakdown;
	invalidBreakdown.scenarios[0].incidents[0].hasEndSeconds = true;
	invalidBreakdown.scenarios[0].incidents[0].endSeconds = 300.0;
	ok &= expect(hasCode(validateRunnableScene(invalidBreakdown), "scene.incident.window"),
			"breakdown recovery end must be after start");
	invalidBreakdown = reducedBreakdown;
	invalidBreakdown.scenarios[0].incidents[0].terminateAtDestination = true;
	invalidBreakdown.scenarios[0].incidents[0].type = "signal_failure";
	invalidBreakdown.scenarios[0].incidents[0].target = "signal-1";
	ok &= expect(hasCode(validateRunnableScene(invalidBreakdown), "scene.incident.fields"),
			"signal failures reject breakdown-only fields");
	SceneService repeated = clean.services[0];
	repeated.operatingCode = "1723";
	repeated.hasRepeat = true;
	repeated.headwaySeconds = 30.0;
	repeated.hasRepeatCount = true;
	repeated.repeatCount = 3;
	repeated.hasOperatingCodeStep = true;
	repeated.operatingCodeStep = 2;
	ok &= expect(sceneServiceOccurrenceCount(repeated, 1.0) == 3,
			"explicit repeat count overrides the duration horizon");
	ok &= expect(sceneServiceOccurrenceOperatingCode(repeated, 1) == "1723"
				&& sceneServiceOccurrenceOperatingCode(repeated, 2) == "1725"
				&& sceneServiceOccurrenceOperatingCode(repeated, 3) == "1727",
			"decimal operating-code step expands occurrences predictably");
	SceneService readableRepeat = clean.services[0];
	readableRepeat.hasRepeat = true;
	readableRepeat.headwaySeconds = 30.0;
	ok &= expect(sceneServiceOccurrenceOperatingCode(readableRepeat, 2) == "service-1-2",
			"repeated services without a step expose readable occurrence codes");
	SceneModel negativeGradient = clean;
	negativeGradient.arcs[0].gradientPercent = -100.0;
	ok &= expect(validateScene(negativeGradient).empty(), "negative arc gradient is allowed");
	SceneModel coordinateTolerance = clean;
	coordinateTolerance.nodes[1].xKm = -5e-9;
	ok &= expect(validateScene(coordinateTolerance).empty(), "native coordinate tolerance is preserved");

	SceneModel overflowingBlocks = clean;
	overflowingBlocks.blocks[0].lengthKm = 2.1;
	ok &= expect(hasCode(validateRunnableScene(overflowingBlocks), "scene.capacity.runtime"),
			"block layout overflow is rejected");

	SceneModel twentyOneArcBlock = clean;
	std::string previousNode = twentyOneArcBlock.nodes.back().id;
	for (int index = 3; index <= 22; ++index) {
		const std::string node = "long-node-" + std::to_string(index);
		twentyOneArcBlock.nodes.push_back({node, "track-1", static_cast<double>(index), 0.0});
		twentyOneArcBlock.arcs.push_back({"long-arc-" + std::to_string(index), "track-1",
				previousNode, node, 0.0, 0.0, 35.0});
		previousNode = node;
	}
	twentyOneArcBlock.blocks[0].lengthKm = 21.0;
	ok &= expect(hasCode(validateRunnableScene(twentyOneArcBlock), "scene.capacity.runtime"),
			"block section arc capacity is enforced");

	SceneModel derivedIdCollision = clean;
	derivedIdCollision.tracks.push_back({"track-2"});
	derivedIdCollision.nodes.push_back({"node-4", "track-2", 3.0, 0.0});
	derivedIdCollision.nodes.push_back({"node-5", "track-2", 4.0, 0.0});
	derivedIdCollision.arcs.push_back({"arc-3", "track-2", "node-4", "node-5", 0.0, 0.0, 35.0});
	derivedIdCollision.blocks.push_back({"block-3", "track-2", 1.0});
	derivedIdCollision.connections.push_back({"switch-1", "node-2", "node-4", false, 0.0});
	derivedIdCollision.connections.push_back({"switch-2", "node-2", "node-4", false, 0.0});
	ok &= expect(hasCode(validateRunnableScene(derivedIdCollision), "scene.capacity.runtime"),
			"derived switch section ID collisions are rejected");

	SceneModel runtimeIdCollision = clean;
	runtimeIdCollision.blocks[0].id = "block.a";
	runtimeIdCollision.blocks[1].id = "@block.a@";
	runtimeIdCollision.routes[0].blocks = {"block.a", "@block.a@"};
	ok &= expect(hasCode(validateRunnableScene(runtimeIdCollision), "scene.capacity.runtime"),
			"runtime block ID normalization rejects collisions");
	SceneModel incompleteRuntimeIdCollision = runtimeIdCollision;
	incompleteRuntimeIdCollision.trainUnits.clear();
	incompleteRuntimeIdCollision.compositions.clear();
	incompleteRuntimeIdCollision.services.clear();
	ok &= expect(hasCode(validateRunnableScene(incompleteRuntimeIdCollision), "scene.capacity.runtime"),
			"unrelated incomplete authoring does not hide infrastructure runtime errors");

	SceneModel routeCapacity = clean;
	routeCapacity.routes[0].blocks.assign(601, "block-1");
	ok &= expect(hasCode(validateRunnableScene(routeCapacity), "scene.capacity.runtime"),
			"route block-token capacity is enforced");

	SceneModel endpointFanout = clean;
	for (int index = 0; index < 6; ++index)
		endpointFanout.connections.push_back({"fanout-" + std::to_string(index), "node-1", "node-3", false, 0.0});
	ok &= expect(hasCode(validateRunnableScene(endpointFanout), "scene.capacity.runtime"),
			"node endpoint fanout capacity is enforced");

	SceneModel dependencyFanout = clean;
	dependencyFanout.blocks[0].lengthKm = 0.2;
	dependencyFanout.blocks[1].lengthKm = 0.18;
	dependencyFanout.blockDependencies.push_back({"block-1", "block-2"});
	for (int index = 0; index < 9; ++index) {
		const std::string target = "dependency-target-" + std::to_string(index);
		dependencyFanout.blocks.push_back({target, "track-1", 0.18});
		dependencyFanout.blockDependencies.push_back({"block-1", target});
	}
	ok &= expect(hasCode(validateRunnableScene(dependencyFanout), "scene.capacity.runtime"),
			"derived and explicit dependency fanout capacity is enforced");

	// Composite route entries resolve each basic block after stripping @...@ positions.
	SceneModel composite = clean;
	composite.routes[0].blocks = {"@block-1@-10/@block-2@-20"};
	composite.blockDependencies.push_back({"@block-1@-10/@block-2@-20", "@block-1@-10"});
	composite.singleTrackRestrictions.push_back({"@block-1@-10", "@block-2@-20",
			"@block-1@-10", "@block-2@-20"});
	composite.stationBoundaries.push_back({"@block-1@-10", true, "@block-2@-20", false});
	ok &= expect(validateScene(composite).empty(), "composite route block components validate");
	composite.scenarios[0].incidents[0].target = "block-2";
	ok &= expect(validateScene(composite).empty(), "signal failure accepts a basic block target");

	const std::string fixtureDir = argv[1];
	ok &= expect(!hasErrors(validateSceneStructure(fixtureDir)),
			"historical fixture remains structurally loadable");
	const auto runnableFixture = validateRunnableSceneDirectory(fixtureDir);
	ok &= expect(hasCode(runnableFixture, "scene.topology.tracks.none"),
			"incomplete historical fixture fails runnable validation");
	ok &= expect(hasCode(validateSceneStructure("/no/such/scene/dir"), "scene.dir.missing"),
			"missing directory reports structural error");

	if (!ok)
		return 1;
	std::cout << "all SceneValidator tests passed\n";
	return 0;
}
