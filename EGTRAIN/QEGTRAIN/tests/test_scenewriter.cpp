#include "scene/SceneModel.h"
#include "scene/SceneWriter.h"
#include "scene/StagedDirectory.h"
#include "scene/StopInsertion.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <iostream>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#include <unistd.h>
#endif

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

#ifndef _WIN32
static bool hasOnlyFile(const fs::path& directory, const std::string& filename) {
	size_t count = 0;
	bool found = false;
	for (const auto& entry : fs::directory_iterator(directory)) {
		++count;
		found = found || entry.path().filename() == filename;
	}
	return count == 1 && found;
}

// Limits the size of files the process can grow, so that a write fails part way.
struct FileSizeLimit {
	rlimit previous{};
	bool active = false;
	void (*previousHandler)(int) = SIG_DFL;

	explicit FileSizeLimit(rlim_t bytes) {
		if (getrlimit(RLIMIT_FSIZE, &previous) != 0)
			return;
		previousHandler = std::signal(SIGXFSZ, SIG_IGN);
		rlimit limit = previous;
		limit.rlim_cur = bytes;
		active = setrlimit(RLIMIT_FSIZE, &limit) == 0;
	}

	~FileSizeLimit() {
		if (active)
			setrlimit(RLIMIT_FSIZE, &previous);
		std::signal(SIGXFSZ, previousHandler);
	}
};
#endif

struct TempDir {
	fs::path path;

	TempDir() {
		static int counter = 0;
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		path = fs::temp_directory_path() / ("scene_writer_test_" + std::to_string(stamp) + "_" + std::to_string(counter++));
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

// Every field the writer can emit holds a value that differs from its default and from the other fields of its struct,
// so that a dropped or swapped field changes the saved files. Fields that are not set here keep the value of
// completeScene(). The values are arbitrary test data.
static SceneModel populatedScene() {
	SceneModel scene = completeScene();
	scene.importReport[0] = {"stations", "legacy/stations.txt", 11, 7, 3, 2};

	scene.tracks.push_back({"track-2"});
	scene.trackViews.push_back({"track-2", 3, 4, true});
	scene.nodes[0] = {"node-1", "track-1", 0.5, -0.25};
	scene.nodes[1] = {"node-2", "track-2", 1.75, 0.625};
	scene.arcs[0].curvatureRadiusM = 1250.5;
	scene.arcs[0].gradientPercent = -1.5;
	scene.blocks[0].lengthKm = 0.625;
	scene.blocks[1].lengthKm = 1.125;
	scene.blocks.push_back({"block-3", "track-1", 0.875});
	scene.blocks.push_back({"block-4", "track-2", 1.25});

	scene.stations[0].hasPosition = true;
	scene.stations[0].positionKm = 12.5;
	ScenePlatform& platform = scene.stations[0].platforms[0];
	platform.nodeIds = {"node-1", "node-2"};
	platform.hasLength = true;
	platform.lengthM = 213.5;
	platform.hasWidth = true;
	platform.widthM = 4.25;

	scene.singleTrackRestrictions[0] = {"block-1", "block-2", "block-3", "block-4"};
	scene.stationBoundaries[0].direction = true;
	scene.stationBoundaries.push_back({"block-3", false, "", false});

	SceneTrainUnit& unit = scene.trainUnits[0];
	unit.physical = {81234.5, 12345.25, 7.0, 44.5, 0.875, 9.75, 0.0125, 1.375, 63.5};
	unit.tractionCurve.clear();
	unit.tractionCurve.push_back({{1.5, 21.5, 91000.5, 410.25, 3.125}});
	unit.tractionCurve.push_back({{21.5, 41.5, 81000.25, 320.75, 2.0625}});
	unit.sourceTractionFile = "/TrainData/unit-1-traction.txt";

	SceneService& service = scene.services[0];
	service.performancePercent = 87.5;
	service.hasMaximumSpeed = true;
	service.maximumSpeedKmh = 137.5;
	service.through = true;
	service.category = "regional";
	service.visualizationColor = "#1A2B3C";
	service.hasRepeat = true;
	service.headwaySeconds = 900.0;
	service.hasRepeatCount = true;
	service.repeatCount = 3;
	service.hasOperatingCodeStep = true;
	service.operatingCodeStep = 2;
	service.stops[0].hasPlannedArrival = true;
	service.stops[0].plannedArrivalSeconds = 80.0;
	service.stops[0].dwellSeconds = 20.0;
	service.stops[1].dwellSeconds = 45.0;

	scene.defaultScenarioId = "alternate";
	scene.scenarios[0].entranceDelays[0].occurrence = 3;
	scene.scenarios[0].entranceDelays.push_back({"service-1", 1, "station-2", 12.5});
	SceneIncident& breakdown = scene.scenarios[1].incidents[0];
	breakdown.hasEndSeconds = true;
	breakdown.endSeconds = 1500.0;
	breakdown.reducedSpeedKmh = 35.5;

	ScenePassengerJourney& journey = scene.passengers[0].journeys[0];
	journey.plannedDepartureStartSeconds = 15.5;
	journey.plannedDepartureEndSeconds = 125.5;
	journey.plannedArrivalStartSeconds = 185.25;
	journey.plannedArrivalEndSeconds = 305.75;
	journey.legs.push_back({"leg-2", "station-2", "station-1", "service-1", 2});
	return scene;
}

static bool loadHasNoErrors(const fs::path& scenePath, SceneModel& scene) {
	SceneLoadResult loaded = loadScene(scenePath.string());
	printErrors(loaded.diagnostics, "load");
	scene = loaded.scene;
	return !hasErrors(loaded.diagnostics);
}

static const char* const kCanonicalSceneFiles[] = {"scene.json", "infrastructure.json", "stations.json",
	"signalling.json", "rolling_stock.json", "services.json", "scenarios.json", "passengers.json", "views.json"};

// Every save writes these. passengers.json and views.json follow the content of the scene.
static const char* const kAlwaysWrittenFiles[] = {"scene.json", "infrastructure.json", "stations.json",
	"signalling.json", "rolling_stock.json", "services.json", "scenarios.json"};

// The parsed content of a scene file, or an empty object when the file does not exist.
static json readJsonFile(const fs::path& path) {
	if (!fs::exists(path))
		return json::object();
	json value = json::parse(readBytes(path), nullptr, false);
	return value.is_discarded() ? json::object() : value;
}

// Names every file that is in one directory only or has other bytes in the two.
static bool sameDirectoryBytes(const fs::path& first, const fs::path& second, const std::string& label) {
	const auto firstFiles = readDirectoryBytes(first);
	const auto secondFiles = readDirectoryBytes(second);
	bool same = true;
	for (const auto& file : firstFiles) {
		const auto other = secondFiles.find(file.first);
		if (other == secondFiles.end()) {
			std::cerr << "scene codec " << label << ": " << file.first << " is only in the first save\n";
			same = false;
		} else if (other->second != file.second) {
			const auto difference = std::mismatch(file.second.begin(), file.second.end(), other->second.begin(),
				other->second.end());
			std::cerr << "scene codec " << label << ": " << file.first << " differs at byte "
					  << (difference.first - file.second.begin()) << "\n";
			same = false;
		}
	}
	for (const auto& file : secondFiles) {
		if (firstFiles.count(file.first) == 0) {
			std::cerr << "scene codec " << label << ": " << file.first << " is only in the second save\n";
			same = false;
		}
	}
	return same;
}

// A save holds only canonical files, always the seven that every save writes, and never the flat incidents.json.
static bool hasCanonicalFileNames(const fs::path& directory, const std::string& label) {
	const auto files = readDirectoryBytes(directory);
	bool canonical = true;
	for (const auto& file : files) {
		const std::string& name = file.first;
		if (std::none_of(std::begin(kCanonicalSceneFiles), std::end(kCanonicalSceneFiles),
				[&name](const char* known) { return name == known; })) {
			std::cerr << "scene codec " << label << ": " << name << " is not a canonical scene file\n";
			canonical = false;
		}
	}
	for (const char* name : kAlwaysWrittenFiles) {
		if (files.count(name) == 0) {
			std::cerr << "scene codec " << label << ": " << name << " is missing\n";
			canonical = false;
		}
	}
	return canonical;
}

// The entries of the array at keys[depth], or, when deeper keys follow, of the arrays below each entry added up.
// A missing file, key or array counts as zero.
static std::size_t countEntries(const json& value, const std::vector<const char*>& keys, std::size_t depth = 0) {
	if (!value.is_object() || !value.contains(keys[depth]) || !value[keys[depth]].is_array())
		return 0;
	const json& entries = value[keys[depth]];
	if (depth + 1 == keys.size())
		return entries.size();
	std::size_t total = 0;
	for (const json& entry : entries)
		total += countEntries(entry, keys, depth + 1);
	return total;
}

struct EntryCount {
	const char* file;
	std::vector<const char*> keys;
};

// Loads the source, saves it, loads the save and saves again. The second save must equal the first one, and the first
// save must keep every entry of the source.
static bool checkSceneCodec(const fs::path& source, const std::string& label, const TempDir& temp) {
	const auto check = [&label](bool condition, const std::string& what) {
		return expect(condition, ("scene codec " + label + ": " + what).c_str());
	};
	SceneModel model;
	if (!check(loadHasNoErrors(source, model), "the source loads without errors"))
		return false;
	const fs::path first = temp.path / "first";
	const SceneSaveResult firstSave = saveScene(model, first.string());
	printErrors(firstSave.diagnostics, "first save");
	if (!check(firstSave.success(), "the first save succeeds"))
		return false;
	SceneModel reloaded;
	if (!check(loadHasNoErrors(first, reloaded) && reloaded.savedWithAppVersion == EGTRAIN_APP_VERSION,
			"the first save loads without errors and records the current app version"))
		return false;
	const fs::path second = temp.path / "second";
	const SceneSaveResult secondSave = saveScene(reloaded, second.string());
	printErrors(secondSave.diagnostics, "second save");
	if (!check(secondSave.success(), "the second save succeeds"))
		return false;
	if (!check(sameDirectoryBytes(first, second, label), "the second save equals the first save byte for byte"))
		return false;
	if (!check(hasCanonicalFileNames(first, label), "the first save holds the canonical files and nothing else"))
		return false;

	std::map<std::string, json> sourceFiles;
	std::map<std::string, json> savedFiles;
	for (const char* name : kCanonicalSceneFiles) {
		sourceFiles[name] = readJsonFile(source / name);
		savedFiles[name] = readJsonFile(first / name);
	}
	// Without scenarios.json the loader creates one baseline scenario and takes its incidents from the flat incidents.json.
	if (!fs::exists(source / "scenarios.json")) {
		json baseline = {{"incidents", json::array()}, {"entrance_delays", json::array()}};
		const json flatIncidents = readJsonFile(source / "incidents.json");
		if (flatIncidents.contains("incidents"))
			baseline["incidents"] = flatIncidents["incidents"];
		sourceFiles["scenarios.json"] = {{"scenarios", json::array({baseline})}};
	}
	const std::vector<EntryCount> counts = {
		{"scene.json", {"import_report"}},
		{"infrastructure.json", {"tracks"}},
		{"infrastructure.json", {"nodes"}},
		{"infrastructure.json", {"arcs"}},
		{"infrastructure.json", {"blocks"}},
		{"infrastructure.json", {"connections"}},
		{"stations.json", {"stations"}},
		{"stations.json", {"stations", "platforms"}},
		{"signalling.json", {"signals"}},
		{"signalling.json", {"routes"}},
		{"signalling.json", {"block_dependencies"}},
		{"signalling.json", {"single_track_restrictions"}},
		{"signalling.json", {"station_boundaries"}},
		{"signalling.json", {"signalling_areas"}},
		{"rolling_stock.json", {"train_units"}},
		{"rolling_stock.json", {"compositions"}},
		{"services.json", {"services"}},
		{"services.json", {"services", "stops"}},
		{"scenarios.json", {"scenarios"}},
		{"scenarios.json", {"scenarios", "incidents"}},
		{"scenarios.json", {"scenarios", "entrance_delays"}},
		{"passengers.json", {"passengers"}},
		{"passengers.json", {"passengers", "journeys"}},
		{"passengers.json", {"passengers", "journeys", "legs"}},
		{"views.json", {"tracks"}},
		{"views.json", {"stations"}},
	};
	bool ok = true;
	for (const EntryCount& count : counts) {
		const std::size_t expected = countEntries(sourceFiles[count.file], count.keys);
		const std::size_t actual = countEntries(savedFiles[count.file], count.keys);
		std::string name = count.file;
		for (const char* key : count.keys)
			name += std::string(" ") + key;
		ok &= check(expected == actual,
			name + ": the source has " + std::to_string(expected) + " entries and the first save has " + std::to_string(actual));
	}
	const json& sourceScene = sourceFiles["scene.json"];
	const json& savedScene = savedFiles["scene.json"];
	for (const char* key : {"name", "description", "base_time", "schema_version", "simulation_settings"}) {
		if (sourceScene.contains(key))
			ok &= check(savedScene.contains(key) && savedScene[key] == sourceScene[key],
				std::string("scene.json ") + key + " has the value of the source");
	}
	return ok;
}

// Checks every scene directory under the Scenes directory and the line and minimal fixtures that sit in
// tests/fixtures/scenes next to it.
static bool checkCommittedSceneCodecs(fs::path scenes) {
	if (!scenes.has_filename())
		scenes = scenes.parent_path();
	const fs::path fixtures = scenes.parent_path() / "tests" / "fixtures" / "scenes";
	std::set<fs::path> directories;
	std::error_code error;
	for (fs::directory_iterator entry(scenes, error), end; !error && entry != end; entry.increment(error)) {
		if (entry->is_directory(error) && entry->path().filename().string().front() != '.')
			directories.insert(entry->path());
	}
	bool ok = expect(!error, "scene codec: the Scenes directory can be read");
	ok &= expect(!directories.empty(), "scene codec: the Scenes directory holds scene directories");
	std::vector<std::pair<std::string, fs::path>> sources;
	for (const fs::path& directory : directories)
		sources.emplace_back(directory.filename().string(), directory);
	sources.emplace_back("fixture line", fixtures / "line");
	sources.emplace_back("fixture minimal", fixtures / "minimal");
	for (const auto& source : sources) {
		TempDir temp;
		ok &= checkSceneCodec(source.second, source.first, temp);
	}
	std::cout << "scene codec: " << sources.size() << " scenes saved and loaded again\n";
	return ok;
}

// The pointer with every array index replaced by "#", so that all entries of an array share one table row.
static std::string withoutIndexes(const std::string& pointer) {
	std::string result;
	for (std::size_t position = 0; position < pointer.size();) {
		const std::size_t end = std::min(pointer.find('/', position + 1), pointer.size());
		const std::string token = pointer.substr(position + 1, end - position - 1);
		const bool index = !token.empty() && token.find_first_not_of("0123456789") == std::string::npos;
		result += "/" + (index ? std::string("#") : token);
		position = end;
	}
	return result;
}

// Adds the pointer of every value that is neither an object nor an array of objects or arrays.
static void collectLeaves(const json& value, const std::string& pointer, std::set<std::string>& leaves) {
	if (value.is_object()) {
		for (auto member = value.begin(); member != value.end(); ++member)
			collectLeaves(member.value(), pointer + "/" + member.key(), leaves);
	} else if (value.is_array()
		&& std::any_of(value.begin(), value.end(), [](const json& item) { return item.is_structured(); })) {
		for (std::size_t index = 0; index < value.size(); ++index)
			collectLeaves(value[index], pointer + "/" + std::to_string(index), leaves);
	} else {
		leaves.insert(withoutIndexes(pointer));
	}
}

// Saves the populated model, checks every key of the written files against a table of values, then loads the save and
// saves again.
static bool checkPopulatedScene(const TempDir& temp) {
	const std::string label = "populated scene";
	const SceneModel model = populatedScene();
	const fs::path first = temp.path / "first";
	const SceneSaveResult firstSave = saveScene(model, first.string());
	printErrors(firstSave.diagnostics, "populated save");
	if (!expect(firstSave.success(), "scene codec populated scene: the model saves"))
		return false;

	struct WrittenValue {
		const char* file;
		const char* pointer;
		json expected;
	};
	// One row per key the writer can emit. The populated model sets every flag that makes the writer emit an optional key.
	const std::vector<WrittenValue> table = {
		{"scene.json", "/schema_version", 1},
		{"scene.json", "/name", "Canonical complete scene"},
		{"scene.json", "/description", "Writer round-trip"},
		{"scene.json", "/base_time", "08:00:00"},
		{"scene.json", "/saved_with_app_version", EGTRAIN_APP_VERSION},
		{"scene.json", "/units/distance", "m"},
		{"scene.json", "/units/time", "s"},
		{"scene.json", "/units/speed", "m/s"},
		{"scene.json", "/simulation_settings/duration_seconds", 3600.0},
		{"scene.json", "/simulation_settings/buffer_time_seconds", 120.0},
		{"scene.json", "/simulation_settings/recovery_time_percent", 5.0},
		{"scene.json", "/import_report/0/category", "stations"},
		{"scene.json", "/import_report/0/source_file", "legacy/stations.txt"},
		{"scene.json", "/import_report/0/source_count", 11},
		{"scene.json", "/import_report/0/converted_count", 7},
		{"scene.json", "/import_report/0/skipped_count", 3},
		{"scene.json", "/import_report/0/unresolved_references", 2},
		{"infrastructure.json", "/tracks/0/id", "track-1"},
		{"infrastructure.json", "/tracks/1/id", "track-2"},
		{"infrastructure.json", "/nodes/0/id", "node-1"},
		{"infrastructure.json", "/nodes/0/track", "track-1"},
		{"infrastructure.json", "/nodes/0/x_km", 0.5},
		{"infrastructure.json", "/nodes/0/y_km", -0.25},
		{"infrastructure.json", "/nodes/1/id", "node-2"},
		{"infrastructure.json", "/nodes/1/track", "track-2"},
		{"infrastructure.json", "/nodes/1/x_km", 1.75},
		{"infrastructure.json", "/nodes/1/y_km", 0.625},
		{"infrastructure.json", "/arcs/0/id", "arc-1"},
		{"infrastructure.json", "/arcs/0/track", "track-1"},
		{"infrastructure.json", "/arcs/0/from", "node-1"},
		{"infrastructure.json", "/arcs/0/to", "node-2"},
		{"infrastructure.json", "/arcs/0/curvature_radius_m", 1250.5},
		{"infrastructure.json", "/arcs/0/gradient_percent", -1.5},
		{"infrastructure.json", "/arcs/0/speed_limit_ms", 40.0},
		{"infrastructure.json", "/blocks/0/id", "block-1"},
		{"infrastructure.json", "/blocks/0/track", "track-1"},
		{"infrastructure.json", "/blocks/0/length_km", 0.625},
		{"infrastructure.json", "/blocks/1/id", "block-2"},
		{"infrastructure.json", "/blocks/1/length_km", 1.125},
		{"infrastructure.json", "/blocks/2/id", "block-3"},
		{"infrastructure.json", "/blocks/3/track", "track-2"},
		{"infrastructure.json", "/blocks/3/length_km", 1.25},
		{"infrastructure.json", "/connections/0/id", "connection-1"},
		{"infrastructure.json", "/connections/0/from", "node-1"},
		{"infrastructure.json", "/connections/0/to", "node-2"},
		{"infrastructure.json", "/connections/0/speed_limit_ms", 30.0},
		{"stations.json", "/stations/0/id", "station-1"},
		{"stations.json", "/stations/0/name", "Origin"},
		{"stations.json", "/stations/0/position_km", 12.5},
		{"stations.json", "/stations/0/platforms/0/id", "platform-1"},
		{"stations.json", "/stations/0/platforms/0/nodes", json::array({"node-1", "node-2"})},
		{"stations.json", "/stations/0/platforms/0/length_m", 213.5},
		{"stations.json", "/stations/0/platforms/0/width_m", 4.25},
		{"stations.json", "/stations/1/id", "station-2"},
		{"stations.json", "/stations/1/name", "Destination"},
		{"stations.json", "/stations/1/platforms/0/id", "platform-2"},
		{"stations.json", "/stations/1/platforms/0/nodes", json::array({"node-2"})},
		{"signalling.json", "/signals/0/id", "signal-1"},
		{"signalling.json", "/signals/0/protected_section", "@block-1@"},
		{"signalling.json", "/signalling_areas/0/id", "area-1"},
		{"signalling.json", "/signalling_areas/0/start_km", 0.25},
		{"signalling.json", "/signalling_areas/0/end_km", 0.75},
		{"signalling.json", "/signalling_areas/0/level", 4},
		{"signalling.json", "/signalling_areas/0/track", "track-1"},
		{"signalling.json", "/routes/0/id", "route-1"},
		{"signalling.json", "/routes/0/blocks", json::array({"block-1", "block-2"})},
		{"signalling.json", "/routes/0/corridor", "corridor-1"},
		{"signalling.json", "/routes/0/reversed", true},
		{"signalling.json", "/block_dependencies/0/block", "block-2"},
		{"signalling.json", "/block_dependencies/0/depends_on", "block-1"},
		{"signalling.json", "/single_track_restrictions/0/start_block", "block-1"},
		{"signalling.json", "/single_track_restrictions/0/end_block", "block-2"},
		{"signalling.json", "/single_track_restrictions/0/protected_start_block", "block-3"},
		{"signalling.json", "/single_track_restrictions/0/protected_end_block", "block-4"},
		{"signalling.json", "/station_boundaries/0/entrance_block", "block-1"},
		{"signalling.json", "/station_boundaries/0/exit_block", "block-2"},
		{"signalling.json", "/station_boundaries/0/direction", true},
		{"signalling.json", "/station_boundaries/1/entrance_block", "block-3"},
		{"signalling.json", "/station_boundaries/1/direction", false},
		{"rolling_stock.json", "/train_units/0/id", "unit-1"},
		{"rolling_stock.json", "/train_units/0/physical/mass_of_traction_unit_kg", 81234.5},
		{"rolling_stock.json", "/train_units/0/physical/mass_of_a_wagon_kg", 12345.25},
		{"rolling_stock.json", "/train_units/0/physical/number_of_wagons", 7.0},
		{"rolling_stock.json", "/train_units/0/physical/max_speed_ms", 44.5},
		{"rolling_stock.json", "/train_units/0/physical/max_deceleration_ms2", 0.875},
		{"rolling_stock.json", "/train_units/0/physical/frontal_area_m2", 9.75},
		{"rolling_stock.json", "/train_units/0/physical/resistance_coefficient", 0.0125},
		{"rolling_stock.json", "/train_units/0/physical/jerk_ms3", 1.375},
		{"rolling_stock.json", "/train_units/0/physical/length_m", 63.5},
		{"rolling_stock.json", "/train_units/0/traction_curve/0", json::array({1.5, 21.5, 91000.5, 410.25, 3.125})},
		{"rolling_stock.json", "/train_units/0/traction_curve/1", json::array({21.5, 41.5, 81000.25, 320.75, 2.0625})},
		{"rolling_stock.json", "/train_units/0/source/data_file", "/TrainData/unit-1.txt"},
		{"rolling_stock.json", "/train_units/0/source/traction_file", "/TrainData/unit-1-traction.txt"},
		{"rolling_stock.json", "/compositions/0/id", "composition-1"},
		{"rolling_stock.json", "/compositions/0/units", json::array({"unit-1"})},
		{"services.json", "/services/0/id", "service-1"},
		{"services.json", "/services/0/composition", "composition-1"},
		{"services.json", "/services/0/route", "route-1"},
		{"services.json", "/services/0/operating_code", "R100"},
		{"services.json", "/services/0/category", "regional"},
		{"services.json", "/services/0/visualization_color", "#1A2B3C"},
		{"services.json", "/services/0/performance_percent", 87.5},
		{"services.json", "/services/0/maximum_speed_kmh", 137.5},
		{"services.json", "/services/0/through", true},
		{"services.json", "/services/0/entry_time_seconds", 60.0},
		{"services.json", "/services/0/repeat/headway_seconds", 900.0},
		{"services.json", "/services/0/repeat/count", 3},
		{"services.json", "/services/0/repeat/operating_code_step", 2},
		{"services.json", "/services/0/stops/0/station", "station-1"},
		{"services.json", "/services/0/stops/0/platform", "platform-1"},
		{"services.json", "/services/0/stops/0/planned_arrival_seconds", 80.0},
		{"services.json", "/services/0/stops/0/planned_departure_seconds", 100.0},
		{"services.json", "/services/0/stops/0/dwell_seconds", 20.0},
		{"services.json", "/services/0/stops/1/station", "station-2"},
		{"services.json", "/services/0/stops/1/planned_arrival_seconds", 200.0},
		{"services.json", "/services/0/stops/1/dwell_seconds", 45.0},
		{"scenarios.json", "/default_scenario_id", "alternate"},
		{"scenarios.json", "/scenarios/0/id", "baseline"},
		{"scenarios.json", "/scenarios/0/name", "Baseline"},
		{"scenarios.json", "/scenarios/0/description", "No disruption"},
		{"scenarios.json", "/scenarios/0/incidents/0/id", "incident-1"},
		{"scenarios.json", "/scenarios/0/incidents/0/type", "signal_failure"},
		{"scenarios.json", "/scenarios/0/incidents/0/target", "signal-1"},
		{"scenarios.json", "/scenarios/0/incidents/0/start_seconds", 300.0},
		{"scenarios.json", "/scenarios/0/incidents/0/end_seconds", 600.0},
		{"scenarios.json", "/scenarios/0/entrance_delays/0/service", "service-1"},
		{"scenarios.json", "/scenarios/0/entrance_delays/0/occurrence", 3},
		{"scenarios.json", "/scenarios/0/entrance_delays/0/station", "station-1"},
		{"scenarios.json", "/scenarios/0/entrance_delays/0/delay_seconds", 30.0},
		{"scenarios.json", "/scenarios/0/entrance_delays/1/occurrence", 1},
		{"scenarios.json", "/scenarios/0/entrance_delays/1/station", "station-2"},
		{"scenarios.json", "/scenarios/0/entrance_delays/1/delay_seconds", 12.5},
		{"scenarios.json", "/scenarios/1/id", "alternate"},
		{"scenarios.json", "/scenarios/1/name", "Alternate"},
		{"scenarios.json", "/scenarios/1/entrance_delays", json::array()},
		{"scenarios.json", "/scenarios/1/incidents/0/id", "breakdown-2"},
		{"scenarios.json", "/scenarios/1/incidents/0/type", "train_breakdown"},
		{"scenarios.json", "/scenarios/1/incidents/0/target", "service-1"},
		{"scenarios.json", "/scenarios/1/incidents/0/start_seconds", 900.0},
		{"scenarios.json", "/scenarios/1/incidents/0/end_seconds", 1500.0},
		{"scenarios.json", "/scenarios/1/incidents/0/occurrence", 2},
		{"scenarios.json", "/scenarios/1/incidents/0/reduced_speed_kmh", 35.5},
		{"scenarios.json", "/scenarios/1/incidents/0/terminate_at_destination", true},
		{"passengers.json", "/passengers/0/id", "passenger-1"},
		{"passengers.json", "/passengers/0/journeys/0/id", "journey-1"},
		{"passengers.json", "/passengers/0/journeys/0/activity", "commute"},
		{"passengers.json", "/passengers/0/journeys/0/origin", "station-1"},
		{"passengers.json", "/passengers/0/journeys/0/destination", "station-2"},
		{"passengers.json", "/passengers/0/journeys/0/planned_departure/start_seconds", 15.5},
		{"passengers.json", "/passengers/0/journeys/0/planned_departure/end_seconds", 125.5},
		{"passengers.json", "/passengers/0/journeys/0/planned_arrival/start_seconds", 185.25},
		{"passengers.json", "/passengers/0/journeys/0/planned_arrival/end_seconds", 305.75},
		{"passengers.json", "/passengers/0/journeys/0/legs/0/id", "leg-1"},
		{"passengers.json", "/passengers/0/journeys/0/legs/0/origin", "station-1"},
		{"passengers.json", "/passengers/0/journeys/0/legs/0/destination", "station-2"},
		{"passengers.json", "/passengers/0/journeys/0/legs/0/service", "service-1"},
		{"passengers.json", "/passengers/0/journeys/0/legs/0/occurrence", 1},
		{"passengers.json", "/passengers/0/journeys/0/legs/1/id", "leg-2"},
		{"passengers.json", "/passengers/0/journeys/0/legs/1/origin", "station-2"},
		{"passengers.json", "/passengers/0/journeys/0/legs/1/destination", "station-1"},
		{"passengers.json", "/passengers/0/journeys/0/legs/1/service", "service-1"},
		{"passengers.json", "/passengers/0/journeys/0/legs/1/occurrence", 2},
		{"views.json", "/tracks/0/track", "track-1"},
		{"views.json", "/tracks/0/level", -2},
		{"views.json", "/tracks/0/region", 1},
		{"views.json", "/tracks/0/visible", false},
		{"views.json", "/tracks/1/track", "track-2"},
		{"views.json", "/tracks/1/level", 3},
		{"views.json", "/tracks/1/region", 4},
		{"views.json", "/stations/0/station", "station-1"},
		{"views.json", "/stations/0/latitude", 55.6761},
		{"views.json", "/stations/0/longitude", 12.5683},
		{"views.json", "/stations/0/regions/0/id", 1},
		{"views.json", "/stations/0/regions/0/position_km", 0.25},
		{"views.json", "/stations/0/regions/1/id", 2},
		{"views.json", "/stations/0/regions/1/position_km", 0.75},
		{"views.json", "/stations/0/corridors", json::array({"main", "branch"})},
	};
	bool ok = true;
	std::map<std::string, json> written;
	std::set<std::string> leaves;
	for (const char* name : kCanonicalSceneFiles) {
		written[name] = readJsonFile(first / name);
		collectLeaves(written[name], "/" + std::string(name), leaves);
	}
	std::size_t wrong = 0;
	for (const WrittenValue& row : table) {
		const json::json_pointer pointer(row.pointer);
		const json& document = written[row.file];
		if (!document.contains(pointer) || document.at(pointer) != row.expected) {
			std::cerr << "scene codec " << label << ": " << row.file << " " << row.pointer << " should be "
					  << row.expected.dump() << " but is "
					  << (document.contains(pointer) ? document.at(pointer).dump() : std::string("missing")) << "\n";
			++wrong;
		}
		leaves.erase(withoutIndexes("/" + std::string(row.file) + row.pointer));
	}
	for (const std::string& leaf : leaves)
		std::cerr << "scene codec " << label << ": the written key " << leaf << " has no row in the table\n";
	ok &= expect(wrong == 0, "scene codec populated scene: every key of the written files has the value of the model");
	ok &= expect(leaves.empty(), "scene codec populated scene: the table has a row for every key of the written files");

	SceneModel reloaded;
	if (!expect(loadHasNoErrors(first, reloaded), "scene codec populated scene: the save loads without errors"))
		return false;
	ok &= expect(reloaded.trackViews.size() == model.trackViews.size()
			&& reloaded.stationViews.size() == model.stationViews.size(),
		"scene codec populated scene: the load keeps every track view and station view");
	const fs::path second = temp.path / "second";
	const SceneSaveResult secondSave = saveScene(reloaded, second.string());
	printErrors(secondSave.diagnostics, "populated second save");
	if (!expect(secondSave.success(), "scene codec populated scene: the second save succeeds"))
		return false;
	ok &= expect(sameDirectoryBytes(first, second, label),
		"scene codec populated scene: the second save equals the first save byte for byte");
	return ok;
}

static std::set<std::string> entryNames(const fs::path& directory) {
	std::set<std::string> names;
	for (const auto& entry : fs::directory_iterator(directory))
		names.insert(entry.path().filename().string());
	return names;
}

// A save publishes its staging directory as the scene directory. The entries of a private root show every staging or
// backup sibling that a save leaves.
static bool checkSavePublication() {
	bool ok = true;
	TempDir temp;
	const fs::path root = temp.path;
	const fs::path destination = root / "scene";
	const SceneModel scene = completeScene();

	const SceneSaveResult first = saveScene(scene, destination.string());
	printErrors(first.diagnostics, "publication first save");
	ok &= expect(first.success(), "save into an absent destination succeeds");
	{
		std::ofstream marker(destination / "marker.txt", std::ios::binary);
		marker << "marker\n";
	}
	const SceneSaveResult second = saveScene(scene, destination.string());
	printErrors(second.diagnostics, "publication second save");
	ok &= expect(second.success() && fs::exists(destination / "marker.txt"), "save over an existing destination succeeds");
	ok &= expect(entryNames(root) == std::set<std::string>{"scene"}, "saves leave no staging or backup sibling");

#ifndef _WIN32
	// The published directory is the staging directory, so it keeps its owner-only mode.
	const fs::perms groupAndOther = fs::status(destination).permissions() & (fs::perms::group_all | fs::perms::others_all);
	ok &= expect(groupAndOther == fs::perms::none, "saved scene directory has no group and no other permission bits");

	// A read-only parent refuses the staging directory. This does not apply to root.
	fs::permissions(root, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
	if (access(root.c_str(), W_OK) != 0) {
		const fs::path other = root / "other";
		const SceneSaveResult refused = saveScene(scene, other.string());
		const bool reported = std::any_of(refused.diagnostics.begin(), refused.diagnostics.end(), [](const SceneDiagnostic& diagnostic) {
			return diagnostic.severity == SceneSeverity::Error && diagnostic.code == "scene.save.write"
				&& diagnostic.message.rfind("Cannot create a private scene staging directory", 0) == 0;
		});
		ok &= expect(!refused.success() && reported, "save into a read-only parent reports the staging directory");
		ok &= expect(!fs::exists(other), "failed save creates no destination");
		ok &= expect(entryNames(root) == std::set<std::string>{"scene"}, "failed save leaves no staging sibling");
	}
	fs::permissions(root, fs::perms::owner_all, fs::perm_options::replace);
#endif
	return ok;
}

// The staged directory helper: creation, removal when the object goes out of scope, and publication in each outcome.
static bool checkStagedDirectory() {
	bool ok = true;
	using Files = std::map<std::string, std::string>;
	const auto put = [](const fs::path& path, const std::string& bytes) {
		std::ofstream output(path, std::ios::binary);
		output << bytes;
	};

	{
		TempDir temp;
		StagedDirectory staged;
		const std::error_code created = createStagedDirectory(temp.path, "scene.staging-", true, staged);
		ok &= expect(!created && fs::is_directory(staged.path) && fs::is_empty(staged.path) && staged.path.parent_path() == temp.path
				&& staged.path.filename().string().rfind("scene.staging-", 0) == 0,
			"staged directory is created empty in the parent with the prefix");
#ifndef _WIN32
		const fs::perms groupAndOther = fs::status(staged.path).permissions() & (fs::perms::group_all | fs::perms::others_all);
		ok &= expect(groupAndOther == fs::perms::none, "owner-only staged directory has no group and no other permission bits");
#endif
		StagedDirectory plain;
		ok &= expect(!createStagedDirectory(temp.path, "scene.staging-", false, plain) && fs::is_directory(plain.path),
			"staged directory without the owner-only mode is created");
	}

	{
		TempDir temp;
		fs::path discardedPath, releasedPath;
		{
			StagedDirectory discarded, released;
			ok &= expect(!createStagedDirectory(temp.path, "discarded-", false, discarded)
					&& !createStagedDirectory(temp.path, "released-", false, released),
				"staged directories are created");
			discardedPath = discarded.path;
			releasedPath = released.path;
			released.release();
			ok &= expect(released.path.empty(), "release clears the path");
		}
		ok &= expect(!discardedPath.empty() && !fs::exists(discardedPath), "staged directory is removed when it goes out of scope");
		ok &= expect(!releasedPath.empty() && fs::is_directory(releasedPath), "released directory stays on disk");
	}

	{
		TempDir temp;
		const fs::path destination = temp.path / "scene";
		StagedDirectory staged;
		ok &= expect(!createStagedDirectory(temp.path, "scene.staging-", true, staged), "staged directory is created for the first publication");
		put(staged.path / "new.txt", "new\n");
		const StagedPublishResult published = publishStagedDirectory(staged, destination, fs::path());
		ok &= expect(published.step == StagedPublishStep::Published && !published.error && !published.restoreError
				&& !published.removeBackupError,
			"publication into an absent destination succeeds");
		ok &= expect(fs::is_directory(destination) && readDirectoryBytes(destination) == Files{{"new.txt", "new\n"}}
				&& staged.path.empty() && entryNames(temp.path) == std::set<std::string>{"scene"},
			"published destination holds the staged files and nothing else is left");
	}

	{
		TempDir temp;
		const fs::path destination = temp.path / "scene";
		fs::create_directory(destination);
		put(destination / "old.txt", "old\n");
		StagedDirectory staged;
		fs::path backup;
		ok &= expect(!createStagedDirectory(temp.path, "scene.staging-", true, staged)
				&& !uniqueSiblingPath(temp.path, "scene.backup-", backup) && !fs::exists(backup)
				&& backup.filename().string().rfind("scene.backup-", 0) == 0,
			"staged directory and backup name are reserved");
		put(staged.path / "new.txt", "new\n");
		const StagedPublishResult published = publishStagedDirectory(staged, destination, backup);
		ok &= expect(published.step == StagedPublishStep::Published && !published.error && !published.restoreError
				&& !published.removeBackupError,
			"publication over an existing destination succeeds");
		ok &= expect(fs::is_directory(destination) && readDirectoryBytes(destination) == Files{{"new.txt", "new\n"}}
				&& entryNames(temp.path) == std::set<std::string>{"scene"},
			"replaced destination holds only the new files and no backup is left");
	}

	{
		// The staging directory is gone, so the rename into place fails and the old destination is moved back.
		TempDir temp;
		const fs::path destination = temp.path / "scene";
		fs::create_directory(destination);
		put(destination / "old.txt", "old\n");
		StagedDirectory staged;
		fs::path backup;
		ok &= expect(!createStagedDirectory(temp.path, "scene.staging-", true, staged)
				&& !uniqueSiblingPath(temp.path, "scene.backup-", backup),
			"staged directory and backup name are reserved for the failing rename");
		fs::remove_all(staged.path);
		const StagedPublishResult failed = publishStagedDirectory(staged, destination, backup);
		ok &= expect(failed.step == StagedPublishStep::Rename && failed.error && !failed.restoreError && !failed.removeBackupError,
			"failed rename into place is reported as the Rename step");
		ok &= expect(fs::is_directory(destination) && readDirectoryBytes(destination) == Files{{"old.txt", "old\n"}}
				&& entryNames(temp.path) == std::set<std::string>{"scene"},
			"failed rename into place restores the old destination and leaves no backup");
	}

	{
		TempDir temp;
		const fs::path destination = temp.path / "scene";
		fs::path stagingPath;
		{
			StagedDirectory staged;
			fs::path backup;
			ok &= expect(!createStagedDirectory(temp.path, "scene.staging-", true, staged)
					&& !uniqueSiblingPath(temp.path, "scene.backup-", backup),
				"staged directory and backup name are reserved for the missing destination");
			stagingPath = staged.path;
			const StagedPublishResult published = publishStagedDirectory(staged, destination, backup);
			ok &= expect(published.step == StagedPublishStep::MoveToBackup && published.error && !fs::exists(destination),
				"missing destination is reported as the MoveToBackup step");
			ok &= expect(fs::is_directory(stagingPath), "staging directory is still owned after a failed move to the backup");
		}
		ok &= expect(entryNames(temp.path).empty(), "staging directory is removed after a failed move to the backup");
	}
	return ok;
}

int main(int argc, char** argv) {
	bool ok = true;
	TempDir temp;
	SceneModel source = completeScene();
	source.savedWithAppVersion = "0.9.0";
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
	ok &= expect(saved.success() && saved.writeAttempted, "complete canonical scene saves after a write attempt");
	ok &= expect(!saveScene(source, "").writeAttempted, "empty destination rejects before filesystem mutation");
	for (const char* file : {"scene.json", "infrastructure.json", "stations.json", "signalling.json",
			 "rolling_stock.json", "services.json", "scenarios.json", "passengers.json", "views.json"})
		ok &= expect(fs::exists(temp.path / file), "all canonical files are written");
	const std::string savedSnapshot = saved.inputSnapshot;
	const SceneInputSnapshot onDiskSnapshot = readSceneDirectorySnapshot(temp.path.string());
	ok &= expect(!savedSnapshot.empty() && onDiskSnapshot.reason.empty()
			&& savedSnapshot == onDiskSnapshot.bytes,
		"successful save retains the exact framed canonical input snapshot");
	ok &= expect(!fs::exists(temp.path / "incidents.json"), "writer does not emit flat incidents.json");
	json savedScene;
	{
		std::ifstream input(temp.path / "scene.json");
		input >> savedScene;
	}
	ok &= expect(savedScene["saved_with_app_version"] == EGTRAIN_APP_VERSION,
		"writer records the authoritative current app version");
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
	ok &= expect(!failedSave.success() && failedSave.writeAttempted && hasErrors(failedSave.diagnostics),
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
	ok &= expect(standaloneSave.success() && standaloneSave.writeAttempted, "standalone scenario JSON saves after a write attempt");
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
	for (const char* occurrence : {"4294967298", "2147483648", "-2147483649", "1.5"}) {
		{
			std::ofstream output(invalidScenarioPath, std::ios::trunc);
			output << R"({"id":"wide","name":"wide","incidents":[],"entrance_delays":[{"service":"s","station":"t","delay_seconds":1,"occurrence":)"
				   << occurrence << "}]}";
		}
		const ScenarioLoadResult wideScenario = loadScenarioJson(invalidScenarioPath.string());
		ok &= expect(!wideScenario.success() && !wideScenario.diagnostics.empty()
				&& wideScenario.diagnostics.front().path == "entrance_delays[0].occurrence",
			"standalone scenario rejects an occurrence that is not an int");
	}
	{
		std::ofstream output(invalidScenarioPath, std::ios::trunc);
		output << R"({"id":"edge","name":"edge","incidents":[],"entrance_delays":[{"service":"s","station":"t","delay_seconds":1,"occurrence":2147483647}]})";
	}
	const ScenarioLoadResult edgeScenario = loadScenarioJson(invalidScenarioPath.string());
	ok &= expect(edgeScenario.success() && edgeScenario.scenario.entranceDelays.size() == 1
			&& edgeScenario.scenario.entranceDelays[0].occurrence == 2147483647,
		"standalone scenario reads an occurrence at the int limit");

	{
		const std::string standaloneBytes = readBytes(standaloneScenarioPath);
		ok &= expect(standaloneBytes == json::parse(standaloneBytes).dump(4) + "\n",
			"standalone scenario JSON keeps four-space indentation and a final newline");
		SceneScenario minimal;
		minimal.id = "min";
		minimal.name = "Minimal";
		const fs::path minimalPath = temp.path / "minimal-scenario.json";
		ok &= expect(saveScenarioJson(minimal, minimalPath.string()).success(),
			"minimal standalone scenario saves");
		ok &= expect(readBytes(minimalPath) == "{\n"
											   "    \"entrance_delays\": [],\n"
											   "    \"id\": \"min\",\n"
											   "    \"incidents\": [],\n"
											   "    \"name\": \"Minimal\"\n"
											   "}\n",
			"standalone scenario bytes match the expected file");

		const std::string previousBytes = readBytes(standaloneScenarioPath);
		SceneScenario replacement = minimal;
		replacement.id = "replacement";
		ok &= expect(saveScenarioJson(replacement, standaloneScenarioPath.string()).success(),
			"standalone scenario replaces an existing file");
		ok &= expect(loadScenarioJson(standaloneScenarioPath.string()).scenario.id == "replacement"
				&& readBytes(standaloneScenarioPath) != previousBytes,
			"replaced standalone scenario holds the new content");
		ok &= expect(!hasSiblingArtifact(standaloneScenarioPath, "tmp"),
			"standalone scenario save leaves no temporary file after success");
	}
	{
		TempDir failing;
		const fs::path blockedPath = failing.path / "blocked.json";
		fs::create_directory(blockedPath);
		{
			std::ofstream output(blockedPath / "keep.txt");
			output << "keep";
		}
		SceneScenario scenario;
		scenario.id = "blocked";
		scenario.name = "Blocked";
		const SceneSaveResult blocked = saveScenarioJson(scenario, blockedPath.string());
		ok &= expect(!blocked.success() && !blocked.wroteAll && hasErrors(blocked.diagnostics),
			"standalone scenario save reports a destination that cannot be replaced");
		ok &= expect(fs::is_directory(blockedPath) && readBytes(blockedPath / "keep.txt") == "keep"
				&& !hasSiblingArtifact(blockedPath, "tmp"),
			"failed replace leaves the destination and no temporary file");

		const SceneSaveResult missing = saveScenarioJson(scenario,
			(failing.path / "missing" / "scenario.json").string());
		ok &= expect(!missing.success() && hasErrors(missing.diagnostics)
				&& !fs::exists(failing.path / "missing"),
			"standalone scenario save reports a missing directory");
	}
#ifndef _WIN32
	{
		TempDir failing;
		const fs::path scenarioPath = failing.path / "scenario.json";
		const std::string original = "original scenario\n";
		{
			std::ofstream output(scenarioPath, std::ios::binary);
			output << original;
		}
		SceneScenario scenario = source.scenarios[0];
		{
			// The write exceeds the limit part way and fails.
			const FileSizeLimit limit(16);
			ok &= expect(limit.active, "file size limit can be applied");
			const SceneSaveResult truncated = saveScenarioJson(scenario, scenarioPath.string());
			ok &= expect(!truncated.success() && hasErrors(truncated.diagnostics),
				"standalone scenario save reports a write that cannot complete");
		}
		ok &= expect(readBytes(scenarioPath) == original && hasOnlyFile(failing.path, "scenario.json"),
			"failed write leaves the original scenario and no temporary file");

		// A read-only directory refuses the temporary file. This does not apply to root.
		fs::permissions(failing.path, fs::perms::owner_read | fs::perms::owner_exec,
			fs::perm_options::replace);
		if (access(failing.path.c_str(), W_OK) != 0) {
			const SceneSaveResult readOnly = saveScenarioJson(scenario, scenarioPath.string());
			ok &= expect(!readOnly.success() && hasErrors(readOnly.diagnostics),
				"standalone scenario save reports a read-only directory");
			ok &= expect(readBytes(scenarioPath) == original,
				"read-only directory leaves the original scenario");
		}
		fs::permissions(failing.path, fs::perms::owner_all, fs::perm_options::replace);
		ok &= expect(hasOnlyFile(failing.path, "scenario.json"),
			"read-only directory leaves no temporary file");

		// A read-only file is not replaced. This does not apply to root.
		fs::permissions(scenarioPath, fs::perms::owner_read, fs::perm_options::replace);
		if (access(scenarioPath.c_str(), W_OK) != 0) {
			const SceneSaveResult readOnlyFile = saveScenarioJson(scenario, scenarioPath.string());
			ok &= expect(!readOnlyFile.success() && hasErrors(readOnlyFile.diagnostics),
				"standalone scenario save reports a read-only file");
			ok &= expect(readBytes(scenarioPath) == original && hasOnlyFile(failing.path, "scenario.json"),
				"read-only file keeps its content and no temporary file remains");
		}
		fs::permissions(scenarioPath, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
	}
#endif
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
			&& !legacyServices["services"][0].contains("category")
			&& !legacyServices["services"][0].contains("visualization_color")
			&& !legacyServices["services"][0].contains("maximum_speed_kmh")
			&& !legacyServices["services"][0].contains("repeat"),
		"default service properties remain omitted for legacy scenes");
	SceneModel missingCategory;
	ok &= expect(loadHasNoErrors(legacyDefaultsPath, missingCategory)
			&& missingCategory.services[0].category.empty(),
		"missing category defaults to empty");
	legacyServices["services"][0]["category"] = 42;
	{
		std::ofstream output(legacyDefaultsPath / "services.json");
		output << legacyServices.dump(2) << "\n";
	}
	ok &= expect(hasErrors(loadScene(legacyDefaultsPath.string()).diagnostics),
		"non-string category is rejected");
	SceneModel missingColor;
	legacyServices["services"][0].erase("category");
	{
		std::ofstream output(legacyDefaultsPath / "services.json");
		output << legacyServices.dump(2) << "\n";
	}
	ok &= expect(loadHasNoErrors(legacyDefaultsPath, missingColor)
			&& missingColor.services[0].visualizationColor.empty(),
		"missing colour defaults to empty");
	legacyServices["services"][0]["visualization_color"] = 42;
	{
		std::ofstream output(legacyDefaultsPath / "services.json");
		output << legacyServices.dump(2) << "\n";
	}
	ok &= expect(hasErrors(loadScene(legacyDefaultsPath.string()).diagnostics),
		"non-string visualization colour is rejected");
	for (const char* color : {"#3c8dd2", "#3C8DD2", "#3c8DD2", "not-a-colour"}) {
		SceneModel colored = completeScene();
		colored.services[0].visualizationColor = color;
		const fs::path coloredPath = temp.path / "colored-service";
		ok &= expect(saveScene(colored, coloredPath.string()).success(), "service with colour saves");
		json coloredServices;
		{
			std::ifstream input(coloredPath / "services.json");
			input >> coloredServices;
		}
		SceneModel coloredReloaded;
		ok &= expect(coloredServices["services"][0].value("visualization_color", "") == color
				&& loadHasNoErrors(coloredPath, coloredReloaded)
				&& coloredReloaded.services[0].visualizationColor == color,
			"service colour is written and read back exactly as given");
	}
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
	ok &= expect(reloaded.savedWithAppVersion == EGTRAIN_APP_VERSION,
		"saved app version round-trips");

	const fs::path missingVersionPath = temp.path / "missing-version";
	ok &= expect(saveScene(source, missingVersionPath.string()).success(),
		"scene with saved app version saves before optional-field check");
	json missingVersionScene;
	{
		std::ifstream input(missingVersionPath / "scene.json");
		input >> missingVersionScene;
	}
	missingVersionScene.erase("saved_with_app_version");
	{
		std::ofstream output(missingVersionPath / "scene.json");
		output << missingVersionScene.dump(2) << "\n";
	}
	SceneModel missingVersion;
	ok &= expect(loadHasNoErrors(missingVersionPath, missingVersion)
			&& missingVersion.savedWithAppVersion.empty(),
		"missing saved app version remains valid and empty");

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

	// A stop inserted into a committed service keeps its place and every planned time through save and reload.
	if (argc > 1) {
		SceneModel committed;
		ok &= expect(loadHasNoErrors(fs::path(argv[1]) / "Paimpol", committed),
			"committed Paimpol scene loads");
		SceneService* service = nullptr;
		for (SceneService& candidate : committed.services)
			if (candidate.id == "Guin-Paim-EXPRESS-1")
				service = &candidate;
		ok &= expect(service != nullptr && service->stops.size() == 3, "committed service has three stops");
		if (service != nullptr && service->stops.size() == 3) {
			const SceneStopInsertionWindow window = sceneStopInsertionWindow(committed, *service, 1);
			ok &= expect(window.ok && !window.visits.visits.empty(),
				"a route visit is free between the first two stops");
			SceneStop inserted;
			inserted.stationId = window.visits.visits.front().stationId;
			inserted.platformId = window.visits.visits.front().platformId;
			inserted.hasPlannedArrival = true;
			inserted.plannedArrivalSeconds = 800.0;
			inserted.hasPlannedDeparture = true;
			inserted.plannedDepartureSeconds = 830.0;
			inserted.dwellSeconds = 30.0;
			const std::vector<SceneStop> original = service->stops;
			const SceneStopInsertionResult insertion = insertSceneStop(committed, *service, 1, inserted);
			ok &= expect(insertion.inserted, "a stop is inserted between the first two stops");
			const fs::path insertDir = temp.path / "inserted-stop";
			const SceneSaveResult insertSave = saveScene(committed, insertDir.string());
			printErrors(insertSave.diagnostics, "insert save");
			ok &= expect(insertSave.success(), "a scene with an inserted stop saves");
			SceneModel reloadedInsert;
			ok &= expect(loadHasNoErrors(insertDir, reloadedInsert), "a scene with an inserted stop reloads");
			const SceneService* reloadedService = nullptr;
			for (const SceneService& candidate : reloadedInsert.services)
				if (candidate.id == "Guin-Paim-EXPRESS-1")
					reloadedService = &candidate;
			const std::vector<SceneStop> expected = {original[0], inserted, original[1], original[2]};
			bool sameStops = reloadedService != nullptr && reloadedService->stops.size() == expected.size();
			for (std::size_t index = 0; sameStops && index < expected.size(); ++index) {
				const SceneStop& left = reloadedService->stops[index];
				const SceneStop& right = expected[index];
				sameStops = left.stationId == right.stationId && left.platformId == right.platformId
					&& left.hasPlannedArrival == right.hasPlannedArrival
					&& left.hasPlannedDeparture == right.hasPlannedDeparture
					&& left.plannedArrivalSeconds == right.plannedArrivalSeconds
					&& left.plannedDepartureSeconds == right.plannedDepartureSeconds
					&& left.dwellSeconds == right.dwellSeconds;
			}
			ok &= expect(sameStops, "reload keeps the stop order and every planned time");
		}
	} else {
		std::cerr << "skipped: the committed-scene insertion check needs the Scenes directory argument\n";
	}

	// A save is compared with a save, because the committed files are not always in the writer's form (indent, key
	// order) and the writer always writes scenarios.json. The entry counts and the identity keys of scene.json are
	// compared with the source, so that a loader and a writer that both drop a field cannot pass.
	if (argc > 1) {
		ok &= checkCommittedSceneCodecs(fs::path(argv[1]));
	} else {
		std::cerr << "skipped: the scene codec checks of the committed scenes need the Scenes directory argument\n";
	}
	{
		TempDir populatedTemp;
		ok &= checkPopulatedScene(populatedTemp);
	}
	ok &= checkSavePublication();
	ok &= checkStagedDirectory();

	if (!ok)
		return 1;
	std::cout << "all SceneWriter tests passed\n";
	return 0;
}
