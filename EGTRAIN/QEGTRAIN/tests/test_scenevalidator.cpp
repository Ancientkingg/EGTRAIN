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
				"scene.platform.nodes.none"},
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
