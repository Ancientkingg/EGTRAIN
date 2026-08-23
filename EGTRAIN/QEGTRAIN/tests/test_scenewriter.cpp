#include "scene/SceneModel.h"
#include "scene/SceneWriter.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <iostream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static std::string readBytes(const fs::path& path) {
	std::ifstream input(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

static std::map<std::string, std::string> readDirectoryBytes(const fs::path& directory) {
	std::map<std::string, std::string> files;
	for (const auto& entry : fs::directory_iterator(directory)) {
		if (entry.is_regular_file())
			files.emplace(entry.path().filename().string(), readBytes(entry.path()));
	}
	return files;
}

static bool hasSiblingArtifact(const fs::path& destination, const std::string& kind) {
	const fs::path parent = destination.parent_path().empty() ? fs::path(".")
			: destination.parent_path();
	const std::string prefix = destination.filename().string() + "." + kind + "-";
	for (const auto& entry : fs::directory_iterator(parent)) {
		if (entry.path().filename().string().rfind(prefix, 0) == 0)
			return true;
	}
	return false;
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
	scene.trackViews.push_back({"track-1", -2, 1, false});

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
	SceneStationView stationView;
	stationView.stationId = "station-1";
	stationView.latitude = 55.6761;
	stationView.longitude = 12.5683;
	stationView.regions = {{1, 0.25}, {2, 0.75}};
	stationView.corridors = {"main", "branch"};
	scene.stationViews.push_back(stationView);

	scene.signals.push_back({"signal-1", "@block-1@"});
	scene.signallingAreas.push_back({"area-1", 0.25, 0.75, 4, "track-1"});
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
	SceneIncident enhancedBreakdown{"breakdown-2", "train_breakdown", "service-1", 900.0, 0.0};
	enhancedBreakdown.hasOccurrence = true;
	enhancedBreakdown.occurrence = 2;
	enhancedBreakdown.hasReducedSpeed = true;
	enhancedBreakdown.reducedSpeedKmh = 40.0;
	enhancedBreakdown.terminateAtDestination = true;
	alternate.incidents.push_back(enhancedBreakdown);
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
	source.stations[0].platforms[0].hasLength = true;
	source.stations[0].platforms[0].lengthM = 125.0;
	source.stations[0].platforms[0].hasWidth = true;
	source.stations[0].platforms[0].widthM = 3.75;
	source.services[0].performancePercent = 87.5;
	source.services[0].hasMaximumSpeed = true;
	source.services[0].maximumSpeedKmh = 120.0;
	source.services[0].hasRepeat = true;
	source.services[0].headwaySeconds = 900.0;
	source.services[0].hasRepeatCount = true;
	source.services[0].repeatCount = 3;
	source.services[0].hasOperatingCodeStep = true;
	source.services[0].operatingCodeStep = 2;
	SceneSaveResult saved = saveScene(source, temp.path.string());
	printErrors(saved.diagnostics, "save");
	ok &= expect(saved.success(), "complete canonical scene saves");
	for (const char* file : {"scene.json", "infrastructure.json", "stations.json", "signalling.json",
			"rolling_stock.json", "services.json", "scenarios.json", "passengers.json", "views.json"})
		ok &= expect(fs::exists(temp.path / file), "all canonical files are written");
	const std::string savedSnapshot = saved.inputSnapshot;
	const SceneInputSnapshot onDiskSnapshot = readSceneDirectorySnapshot(temp.path.string());
	ok &= expect(!savedSnapshot.empty() && onDiskSnapshot.reason.empty()
			&& savedSnapshot == onDiskSnapshot.bytes,
			"successful save retains the exact framed canonical input snapshot");
	ok &= expect(!fs::exists(temp.path / "incidents.json"), "writer does not emit flat incidents.json");
	{
		std::ofstream marker(temp.path / "generation-marker.txt", std::ios::binary);
		marker << "original generation marker\n";
	}
	fs::create_directory(temp.path / "notes");
	{
		std::ofstream note(temp.path / "notes" / "operator.txt", std::ios::binary);
		note << "keep with scene\n";
	}
	const auto originalGeneration = readDirectoryBytes(temp.path);
	SceneModel malformed = source;
	malformed.passengers[0].journeys[0].activity = std::string("\xC3\x28", 2);
	const SceneSaveResult failedSave = saveScene(malformed, temp.path.string());
	ok &= expect(!failedSave.success() && hasErrors(failedSave.diagnostics),
			"malformed UTF-8 fails without publishing a partial generation");
	ok &= expect(readDirectoryBytes(temp.path) == originalGeneration,
			"failed save preserves every byte of the previous generation");
	ok &= expect(!hasSiblingArtifact(temp.path, "staging")
			&& !hasSiblingArtifact(temp.path, "backup"),
			"failed save removes sibling staging and backup artifacts");
	const SceneSaveResult replacementSave = saveScene(source, temp.path.string());
	ok &= expect(replacementSave.success()
			&& fs::exists(temp.path / "generation-marker.txt")
			&& fs::exists(temp.path / "notes" / "operator.txt"),
			"successful generation replacement preserves unmanaged scene contents");
	json stations;
	{
		std::ifstream input(temp.path / "stations.json");
		input >> stations;
	}
	ok &= expect(stations["stations"][0]["platforms"][0]["length_m"] == 125.0
			&& stations["stations"][0]["platforms"][0]["width_m"] == 3.75,
			"writer emits explicitly authored platform geometry");
	ok &= expect(!stations["stations"][1]["platforms"][0].contains("length_m")
			&& !stations["stations"][1]["platforms"][0].contains("width_m"),
			"writer omits absent platform geometry");
	json signalling;
	{
		std::ifstream input(temp.path / "signalling.json");
		input >> signalling;
	}
	ok &= expect(signalling["signalling_areas"].size() == 1
			&& signalling["signalling_areas"][0]["id"] == "area-1"
			&& signalling["signalling_areas"][0]["start_km"] == 0.25
			&& signalling["signalling_areas"][0]["end_km"] == 0.75
			&& signalling["signalling_areas"][0]["level"] == 4
			&& signalling["signalling_areas"][0]["track"] == "track-1",
			"writer emits signalling area fields");
	ok &= expect(signalling["signals"].size() == 1
				&& signalling["signals"][0]["id"] == "signal-1"
				&& signalling["signals"][0]["protected_section"] == "@block-1@",
				"writer emits an explicitly bound signal section");
	SceneModel absentAreas = completeScene();
	absentAreas.signallingAreas.clear();
	absentAreas.signals[0].protectedSection.clear();
	const fs::path absentAreasPath = temp.path / "absent-signalling-areas";
	ok &= expect(saveScene(absentAreas, absentAreasPath.string()).success(),
			"scene without signalling areas saves");
	json absentSignalling;
	{
		std::ifstream input(absentAreasPath / "signalling.json");
		input >> absentSignalling;
	}
	ok &= expect(!absentSignalling.contains("signalling_areas"),
			"writer preserves an absent signalling area array");
	ok &= expect(!absentSignalling["signals"][0].contains("protected_section"),
			"writer omits an empty protected-section binding");
	SceneModel absentAreasReloaded;
	ok &= expect(loadHasNoErrors(absentAreasPath, absentAreasReloaded)
			&& absentAreasReloaded.signallingAreas.empty(),
			"scene without signalling areas reloads with no inferred defaults");
	ok &= expect(absentAreasReloaded.signals.size() == 1
				&& absentAreasReloaded.signals[0].protectedSection.empty(),
				"ID-only signal input round-trips without inventing a binding");

	json services;
	{
		std::ifstream input(temp.path / "services.json");
		input >> services;
	}
	const json& firstStop = services["services"][0]["stops"][0];
	ok &= expect(services["services"][0]["operating_code"] == "R100",
			"writer emits the service operating code");
	ok &= expect(services["services"][0]["performance_percent"] == 87.5
				&& services["services"][0]["maximum_speed_kmh"] == 120.0,
			"writer emits optional performance and maximum speed");
	ok &= expect(services["services"][0]["repeat"]["count"] == 3
				&& services["services"][0]["repeat"]["operating_code_step"] == 2,
			"writer emits explicit repeat count and operating-code step");
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
	const json& enhancedJson = scenarios["scenarios"][1]["incidents"][0];
	ok &= expect(enhancedJson["occurrence"] == 2
			&& enhancedJson["reduced_speed_kmh"] == 40.0
			&& enhancedJson["terminate_at_destination"] == true
			&& !enhancedJson.contains("end_seconds"),
			"writer preserves occurrence-specific reduced breakdown without recovery end");

	const fs::path standaloneScenarioPath = temp.path / "baseline-scenario.json";
	const SceneSaveResult standaloneSave = saveScenarioJson(source.scenarios[0], standaloneScenarioPath.string());
	ok &= expect(standaloneSave.success(), "standalone scenario JSON saves");
	json standaloneScenario;
	{
		std::ifstream input(standaloneScenarioPath);
		input >> standaloneScenario;
	}
	ok &= expect(standaloneScenario.is_object()
			&& standaloneScenario["id"] == "baseline"
			&& standaloneScenario["entrance_delays"].size() == 1
			&& !standaloneScenario.contains("scenarios")
			&& !standaloneScenario.contains("infrastructure"),
			"standalone scenario JSON contains only scenario data");
	const ScenarioLoadResult standaloneLoad = loadScenarioJson(standaloneScenarioPath.string());
	ok &= expect(standaloneLoad.success()
			&& standaloneLoad.scenario.id == source.scenarios[0].id
			&& standaloneLoad.scenario.description == source.scenarios[0].description
			&& standaloneLoad.scenario.incidents.size() == source.scenarios[0].incidents.size()
			&& standaloneLoad.scenario.entranceDelays.size() == 1
			&& standaloneLoad.scenario.entranceDelays[0].serviceId == "service-1"
			&& standaloneLoad.scenario.entranceDelays[0].occurrence == 1
			&& standaloneLoad.scenario.entranceDelays[0].stationId == "station-1"
			&& standaloneLoad.scenario.entranceDelays[0].delaySeconds == 30.0,
			"standalone scenario JSON round-trips incidents and entrance delays");
	SceneScenario duplicateScenario = source.scenarios[0];
	duplicateScenario.id = "baseline-copy";
	duplicateScenario.incidents[0].id = "incident-copy";
	const fs::path duplicateScenarioPath = temp.path / "baseline-copy.json";
	ok &= expect(saveScenarioJson(duplicateScenario, duplicateScenarioPath.string()).success(),
			"duplicated scenario saves through the standalone boundary");
	const ScenarioLoadResult duplicateLoad = loadScenarioJson(duplicateScenarioPath.string());
	ok &= expect(duplicateLoad.success() && duplicateLoad.scenario.id == "baseline-copy"
			&& duplicateLoad.scenario.incidents[0].id == "incident-copy",
			"duplicated scenario round-trips with independent IDs");
	const fs::path invalidScenarioPath = temp.path / "invalid-scenario.json";
	{
		std::ofstream output(invalidScenarioPath);
		output << R"({"id":"broken","name":5,"incidents":[{"id":"only-id"}]})";
	}
	const ScenarioLoadResult invalidScenario = loadScenarioJson(invalidScenarioPath.string());
	ok &= expect(!invalidScenario.success() && hasErrors(invalidScenario.diagnostics)
			&& !invalidScenario.diagnostics.empty(),
			"standalone scenario parser diagnoses structural and type errors");
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
	const SceneLoadResult loadedSnapshot = loadScene(temp.path.string());
	ok &= expect(!loadedSnapshot.inputSnapshot.empty()
			&& loadedSnapshot.inputSnapshot == savedSnapshot,
			"load retains the exact framed snapshot emitted by save");
	{
		std::ofstream output(temp.path / "services.json", std::ios::binary | std::ios::app);
		output << ' ';
	}
	const SceneInputSnapshot changedOnDisk = readSceneDirectorySnapshot(temp.path.string());
	ok &= expect(changedOnDisk.reason.empty() && changedOnDisk.bytes != savedSnapshot
			&& saved.inputSnapshot == savedSnapshot
			&& loadedSnapshot.inputSnapshot == savedSnapshot,
			"external canonical-file changes do not mutate retained snapshots");
	ok &= expect(reloaded.tracks.size() == 1 && reloaded.nodes.size() == 2 && reloaded.blocks.size() == 2,
			"topology round-trips");
	ok &= expect(reloaded.trackViews.size() == 1
			&& reloaded.trackViews[0].trackId == "track-1"
			&& reloaded.trackViews[0].level == -2
			&& reloaded.trackViews[0].region == 1
			&& !reloaded.trackViews[0].visible
			&& reloaded.stationViews.size() == 1
			&& reloaded.stationViews[0].stationId == "station-1"
			&& reloaded.stationViews[0].regions == std::vector<std::pair<int, double>>({{1, 0.25}, {2, 0.75}})
			&& reloaded.stationViews[0].corridors == std::vector<std::string>({"main", "branch"}),
			"authored display layout round-trips");
	ok &= expect(reloaded.routes[0].corridor == "corridor-1" && reloaded.routes[0].reversed,
			"route corridor and direction round-trip");
	ok &= expect(reloaded.signallingAreas.size() == 1
			&& reloaded.signallingAreas[0].id == "area-1"
			&& reloaded.signallingAreas[0].startKm == 0.25
			&& reloaded.signallingAreas[0].endKm == 0.75
			&& reloaded.signallingAreas[0].level == 4
			&& reloaded.signallingAreas[0].trackId == "track-1",
			"signalling area fields round-trip");
	ok &= expect(reloaded.signals.size() == 1
				&& reloaded.signals[0].protectedSection == "@block-1@",
				"protected-section binding round-trips through folder JSON");
	ok &= expect(reloaded.services[0].stops[0].hasPlannedDeparture
				&& reloaded.services[0].stops[0].plannedDepartureSeconds == 100.0,
			"planned departure round-trips");
	ok &= expect(reloaded.services[0].operatingCode == "R100", "service operating code round-trips");
	ok &= expect(reloaded.stations[0].platforms[0].hasLength
			&& reloaded.stations[0].platforms[0].lengthM == 125.0
			&& reloaded.stations[0].platforms[0].hasWidth
			&& reloaded.stations[0].platforms[0].widthM == 3.75
			&& !reloaded.stations[1].platforms[0].hasLength
			&& reloaded.stations[1].platforms[0].lengthM == 100.0
			&& !reloaded.stations[1].platforms[0].hasWidth
			&& reloaded.stations[1].platforms[0].widthM == 2.5,
			"platform geometry presence and effective defaults round-trip");
	ok &= expect(reloaded.services[0].performancePercent == 87.5
				&& reloaded.services[0].hasMaximumSpeed
				&& reloaded.services[0].maximumSpeedKmh == 120.0
				&& reloaded.services[0].hasRepeatCount && reloaded.services[0].repeatCount == 3
				&& reloaded.services[0].hasOperatingCodeStep
				&& reloaded.services[0].operatingCodeStep == 2,
				"optional service runtime properties round-trip");
	ok &= expect(reloaded.scenarios[1].incidents.size() == 1
				&& reloaded.scenarios[1].incidents[0].hasOccurrence
				&& reloaded.scenarios[1].incidents[0].occurrence == 2
				&& reloaded.scenarios[1].incidents[0].hasReducedSpeed
				&& reloaded.scenarios[1].incidents[0].reducedSpeedKmh == 40.0
				&& !reloaded.scenarios[1].incidents[0].hasEndSeconds
				&& reloaded.scenarios[1].incidents[0].terminateAtDestination,
				"enhanced breakdown fields round-trip through canonical scene files");

	SceneModel legacyDefaults = completeScene();
	legacyDefaults.scenarios[1].incidents[0].hasReducedSpeed = false;
	legacyDefaults.scenarios[1].incidents[0].reducedSpeedKmh = 40.125;
	const fs::path legacyDefaultsPath = temp.path / "legacy-defaults";
	ok &= expect(saveScene(legacyDefaults, legacyDefaultsPath.string()).success(),
			"legacy-default service still saves");
	json legacyServices;
	{
		std::ifstream input(legacyDefaultsPath / "services.json");
		input >> legacyServices;
	}
	json normalizedScenarios;
	{
		std::ifstream input(legacyDefaultsPath / "scenarios.json");
		input >> normalizedScenarios;
	}
	ok &= expect(normalizedScenarios["scenarios"][1]["incidents"][0]["reduced_speed_kmh"] == 40.125
			&& !normalizedScenarios["scenarios"][1]["incidents"][0].contains("end_seconds"),
			"writer preserves a nonzero reduced speed when its presence flag is stale");
	ok &= expect(!legacyServices["services"][0].contains("performance_percent")
				&& !legacyServices["services"][0].contains("maximum_speed_kmh")
				&& !legacyServices["services"][0].contains("repeat"),
				"default service properties remain omitted for legacy scenes");
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

	// The student-facing loaded-data summary distinguishes source, parsed,
	// optional, and validation states and carries only concrete editor targets.
	refreshLoadedDataSummary(reloaded);
	const auto findCategory = [](const std::vector<SceneLoadedData>& rows,
			const std::string& category) -> const SceneLoadedData* {
		for (const auto& row : rows) {
			if (row.category == category)
				return &row;
		}
		return nullptr;
	};
	const SceneLoadedData* rolling = findCategory(reloaded.loadedData, "rolling_stock");
	const SceneLoadedData* units = rolling ? findCategory(rolling->children, "train_units") : nullptr;
	ok &= expect(rolling && rolling->status == "Parsed", "loaded category reports parsed canonical data");
	ok &= expect(units && !units->children.empty()
				&& units->children.front().targetType == "train_unit"
				&& units->children.front().category == "unit-1"
				&& findCategory(units->children.front().children, "train_unit_parameters")
				&& findCategory(units->children.front().children, "tractive_effort_curve")
				&& findCategory(units->children.front().children, "import_provenance"),
				"loaded train-unit row owns its editor target, data, curve, and provenance");
	const SceneLoadedData* infrastructure = findCategory(reloaded.loadedData, "infrastructure");
	ok &= expect(infrastructure && infrastructure->targetType == "network",
				"infrastructure summary resolves to the existing network view");

	SceneModel withoutOptional = reloaded;
	withoutOptional.sourceFiles.erase("scenarios.json");
	withoutOptional.sourceFiles.erase("passengers.json");
	withoutOptional.scenarios.clear();
	withoutOptional.passengers.clear();
	refreshLoadedDataSummary(withoutOptional);
	const SceneLoadedData* scenariosRow = findCategory(withoutOptional.loadedData, "scenarios");
	const SceneLoadedData* passengersRow = findCategory(withoutOptional.loadedData, "passengers");
	ok &= expect(scenariosRow && scenariosRow->status == "Missing optional"
				&& passengersRow && passengersRow->status == "Missing optional",
				"absent optional scene inputs are labelled explicitly");
	ok &= expect(withoutOptional.scenarios.empty(), "loaded-data refresh does not create canonical input");

	SceneDiagnostic rollingWarning;
	rollingWarning.severity = SceneSeverity::Warning;
	rollingWarning.file = "rolling_stock.json";
	refreshLoadedDataDiagnostics(reloaded, {rollingWarning});
	rolling = findCategory(reloaded.loadedData, "rolling_stock");
	ok &= expect(rolling && rolling->status == "Warning"
				&& !rolling->children.empty() && rolling->children.back().status == "Warning",
				"validation warning is visible on its loaded-data category");

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
