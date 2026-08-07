#include "scene/SceneModel.h"
#include "scene/SceneValidator.h"

#include <iostream>
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
	scene.arcs.push_back({"arc-1", "track-1", "node-1", "node-2", 0.0, 0.0, 40.0});
	scene.blocks.push_back({"block-1", "track-1", 0.5});
	scene.blocks.push_back({"block-2", "track-1", 0.5});
	scene.connections.push_back({"connection-1", "node-1", "node-2", false, 0.0});

	SceneStation origin;
	origin.id = "station-1";
	origin.name = "Origin";
	origin.platforms.push_back({"platform-1", {"node-1"}});
	scene.stations.push_back(origin);
	SceneStation destination;
	destination.id = "station-2";
	destination.name = "Destination";
	destination.platforms.push_back({"platform-2", {"node-2"}});
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
	};
	const FailureCase cases[] = {
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
		{"non-positive duration", [](SceneModel& scene) { scene.settings.durationSeconds = 0.0; },
				"scene.duration.invalid"},
		{"empty tracks", [](SceneModel& scene) { scene.tracks.clear(); }, "scene.topology.tracks.none"},
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
		ok &= expect(hasCode(diagnostics, test.code), test.name);
	}

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
