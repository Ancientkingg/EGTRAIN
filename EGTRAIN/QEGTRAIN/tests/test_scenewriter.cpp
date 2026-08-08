#include "scene/SceneModel.h"
#include "scene/SceneWriter.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static void printErrors(const std::vector<SceneDiagnostic>& diagnostics, const char* label) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.severity == SceneSeverity::Error)
			std::cerr << label << ": " << toDisplayText(diagnostic) << "\n";
	}
}

struct TempDir {
	fs::path path;

	TempDir() {
		static int counter = 0;
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		path = fs::temp_directory_path() / ("scene_writer_test_" + std::to_string(stamp) + "_"
				+ std::to_string(counter++));
		fs::create_directories(path);
	}

	~TempDir() {
		std::error_code error;
		fs::remove_all(path, error);
	}
};

static SceneModel completeScene() {
	SceneModel scene;
	scene.schemaVersion = 1;
	scene.name = "Canonical complete scene";
	scene.description = "Writer round-trip";
	scene.baseTime = "08:00:00";
	scene.settings.hasDuration = true;
	scene.settings.durationSeconds = 3600.0;
	scene.settings.hasBufferTime = true;
	scene.settings.bufferTimeSeconds = 120.0;
	scene.settings.hasRecoveryTime = true;
	scene.settings.recoveryTimePercent = 5.0;

	scene.tracks.push_back({"track-1"});
	scene.nodes.push_back({"node-1", "track-1", 0.0, 0.0});
	scene.nodes.push_back({"node-2", "track-1", 1.0, 0.0});
	scene.arcs.push_back({"arc-1", "track-1", "node-1", "node-2", 0.0, 0.0, 40.0});
	scene.blocks.push_back({"block-1", "track-1", 0.5});
	scene.blocks.push_back({"block-2", "track-1", 0.5});
	scene.connections.push_back({"connection-1", "node-1", "node-2", true, 30.0});

	SceneStation first;
	first.id = "station-1";
	first.name = "Origin";
	first.platforms.push_back({"platform-1", {"node-1"}});
	scene.stations.push_back(first);
	SceneStation second;
	second.id = "station-2";
	second.name = "Destination";
	second.platforms.push_back({"platform-2", {"node-2"}});
	scene.stations.push_back(second);

	scene.signals.push_back({"signal-1"});
	SceneRoute route;
	route.id = "route-1";
	route.blocks = {"block-1", "block-2"};
	route.hasCorridor = true;
	route.corridor = "corridor-1";
	route.reversed = true;
	scene.routes.push_back(route);
	scene.blockDependencies.push_back({"block-2", "block-1"});
	scene.singleTrackRestrictions.push_back({"block-1", "block-2", "block-1", "block-2"});
	scene.stationBoundaries.push_back({"block-1", true, "block-2", false});

	SceneTrainUnit unit;
	unit.id = "unit-1";
	unit.hasPhysical = true;
	unit.physical.mass_of_traction_unit_kg = 100000.0;
	unit.physical.max_speed_ms = 40.0;
	unit.physical.max_deceleration_ms2 = 0.7;
	unit.physical.length_m = 50.0;
	unit.tractionCurve.push_back({{0.0, 40.0, 100000.0, 0.0, 0.0}});
	unit.sourceDataFile = "/TrainData/unit-1.txt";
	scene.trainUnits.push_back(unit);
	scene.compositions.push_back({"composition-1", {"unit-1"}});

	SceneService service;
	service.id = "service-1";
	service.operatingCode = "R100";
	service.composition = "composition-1";
	service.route = "route-1";
	service.hasEntryTime = true;
	service.entryTimeSeconds = 60.0;
	SceneStop origin;
	origin.stationId = "station-1";
	origin.platformId = "platform-1";
	origin.hasPlannedDeparture = true;
	origin.plannedDepartureSeconds = 100.0;
	SceneStop destination;
	destination.stationId = "station-2";
	destination.platformId = "platform-2";
	destination.hasPlannedArrival = true;
	destination.plannedArrivalSeconds = 200.0;
	service.stops = {origin, destination};
	scene.services.push_back(service);

	SceneScenario baseline;
	baseline.id = "baseline";
	baseline.name = "Baseline";
	baseline.description = "No disruption";
	baseline.incidents.push_back({"incident-1", "signal_failure", "signal-1", 300.0, 600.0});
	baseline.entranceDelays.push_back({"service-1", 1, "station-1", 30.0});
	scene.scenarios.push_back(baseline);
	SceneScenario alternate;
	alternate.id = "alternate";
	alternate.name = "Alternate";
	scene.scenarios.push_back(alternate);
	scene.defaultScenarioId = "baseline";

	ScenePassenger passenger;
	passenger.id = "passenger-1";
	ScenePassengerJourney journey;
	journey.id = "journey-1";
	journey.activity = "commute";
	journey.originStationId = "station-1";
	journey.destinationStationId = "station-2";
	journey.plannedDepartureStartSeconds = 0.0;
	journey.plannedDepartureEndSeconds = 120.0;
	journey.plannedArrivalStartSeconds = 180.0;
	journey.plannedArrivalEndSeconds = 300.0;
	journey.legs.push_back({"leg-1", "station-1", "station-2", "service-1", 1});
	passenger.journeys.push_back(journey);
	scene.passengers.push_back(passenger);

	scene.importReport.push_back({"stations", "legacy/stations", 2, 2, 0, 0});
	return scene;
}

static bool loadHasNoErrors(const fs::path& scenePath, SceneModel& scene) {
	SceneLoadResult loaded = loadScene(scenePath.string());
	printErrors(loaded.diagnostics, "load");
	scene = loaded.scene;
	return !hasErrors(loaded.diagnostics);
}

int main() {
	bool ok = true;
	TempDir temp;
	SceneModel source = completeScene();
	SceneSaveResult saved = saveScene(source, temp.path.string());
	printErrors(saved.diagnostics, "save");
	ok &= expect(saved.success(), "complete canonical scene saves");
	for (const char* file : {"scene.json", "infrastructure.json", "stations.json", "signalling.json",
			"rolling_stock.json", "services.json", "scenarios.json", "passengers.json"})
		ok &= expect(fs::exists(temp.path / file), "all canonical files are written");
	ok &= expect(!fs::exists(temp.path / "incidents.json"), "writer does not emit flat incidents.json");

	json services;
	{
		std::ifstream input(temp.path / "services.json");
		input >> services;
	}
	const json& firstStop = services["services"][0]["stops"][0];
	ok &= expect(services["services"][0]["operating_code"] == "R100",
			"writer emits the service operating code");
	ok &= expect(firstStop.contains("planned_departure_seconds"), "writer emits planned departure");
	ok &= expect(!firstStop.contains("departure_seconds"), "writer omits legacy departure alias");
	const json& lastStop = services["services"][0]["stops"][1];
	ok &= expect(lastStop.contains("planned_arrival_seconds"), "writer emits planned arrival");
	ok &= expect(!lastStop.contains("arrival_seconds"), "writer omits legacy arrival alias");

	json scenarios;
	{
		std::ifstream input(temp.path / "scenarios.json");
		input >> scenarios;
	}
	ok &= expect(scenarios["default_scenario_id"] == "baseline", "writer emits default_scenario_id");
	ok &= expect(scenarios["scenarios"].size() == 2, "writer emits named scenarios");

	json passengers;
	{
		std::ifstream input(temp.path / "passengers.json");
		input >> passengers;
	}
	ok &= expect(passengers["passengers"][0]["journeys"][0].contains("planned_departure"),
			"writer emits passenger departure window");
	ok &= expect(passengers["passengers"][0]["journeys"][0]["legs"].size() == 1,
			"writer emits ordered passenger legs");

	SceneModel reloaded;
	ok &= expect(loadHasNoErrors(temp.path, reloaded), "canonical scene reloads without structural errors");
	ok &= expect(reloaded.tracks.size() == 1 && reloaded.nodes.size() == 2 && reloaded.blocks.size() == 2,
			"topology round-trips");
	ok &= expect(reloaded.routes[0].corridor == "corridor-1" && reloaded.routes[0].reversed,
			"route corridor and direction round-trip");
	ok &= expect(reloaded.services[0].stops[0].hasPlannedDeparture
				&& reloaded.services[0].stops[0].plannedDepartureSeconds == 100.0,
			"planned departure round-trips");
	ok &= expect(reloaded.services[0].operatingCode == "R100", "service operating code round-trips");
	ok &= expect(reloaded.services[0].stops[1].hasPlannedArrival
				&& reloaded.services[0].stops[1].plannedArrivalSeconds == 200.0,
			"planned arrival round-trips");
	ok &= expect(reloaded.defaultScenarioId == "baseline" && reloaded.scenarios.size() == 2,
			"scenario selection round-trips");
	ok &= expect(reloaded.passengers.size() == 1 && reloaded.passengers[0].journeys.size() == 1,
			"passenger journey round-trips");
	ok &= expect(reloaded.trainUnits[0].sourceDataFile == "/TrainData/unit-1.txt"
				&& reloaded.trainUnits[0].sourceTractionFile.empty(),
			"rolling provenance fields are independently optional");

	// Historical aliases and flat incidents remain readable during migration.
	fs::remove(temp.path / "scenarios.json");
	{
		std::ofstream output(temp.path / "incidents.json");
		output << R"({"incidents":[{"id":"legacy-incident","type":"signal_failure","target":"signal-1","start_seconds":10,"end_seconds":20}]})";
	}
	services["services"][0]["stops"][0]["departure_seconds"] = services["services"][0]["stops"][0]["planned_departure_seconds"];
	services["services"][0]["stops"][0].erase("planned_departure_seconds");
	services["services"][0]["stops"][1]["arrival_seconds"] = services["services"][0]["stops"][1]["planned_arrival_seconds"];
	services["services"][0]["stops"][1].erase("planned_arrival_seconds");
	{
		std::ofstream output(temp.path / "services.json");
		output << services.dump(2) << "\n";
	}
	SceneModel historical;
	ok &= expect(loadHasNoErrors(temp.path, historical), "historical aliases and flat incidents load");
	ok &= expect(historical.defaultScenarioId == "baseline" && historical.scenarios.size() == 1,
			"flat incidents become the implicit baseline");
	ok &= expect(historical.scenarios[0].incidents.size() == 1
				&& historical.services[0].stops[0].hasPlannedDeparture,
			"flat incident and timetable aliases migrate into canonical fields");

	// A later successful save removes the stale compatibility file.
	SceneSaveResult resaved = saveScene(source, temp.path.string());
	ok &= expect(resaved.success() && !fs::exists(temp.path / "incidents.json"),
			"successful canonical save removes stale incidents after scenarios write");

	if (!ok)
		return 1;
	std::cout << "all SceneWriter tests passed\n";
	return 0;
}
