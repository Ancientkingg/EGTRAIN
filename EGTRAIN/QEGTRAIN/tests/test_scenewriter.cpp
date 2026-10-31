#include "scene/LoadedDataSummary.h"
#include "scene/SceneModel.h"
#include "scene/SceneWriter.h"
#include "scene/SignallingLevel.h"
#include "scene/StagedDirectory.h"
#include "scene/StopInsertion.h"

#include <QString>
#include <QTest>

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

	sceneSignals(scene).push_back({"signal-1", "@block-1@"});
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
	scene.trackViews.push_back({"track-2", 3, 4, true, true});
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

// completeScene() with the optional platform geometry and service properties set, as saved by an older version.
static SceneModel authoredScene() {
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
	return source;
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

// The scene directories under the Scenes directory, without hidden ones. Sets error when the directory cannot be read.
static std::set<fs::path> committedSceneDirectories(std::error_code& error) {
	std::set<fs::path> directories;
	for (fs::directory_iterator entry(fs::path(EGTRAIN_SCENES_DIR), error), end; !error && entry != end;
		entry.increment(error)) {
		if (entry->is_directory(error) && entry->path().filename().string().front() != '.')
			directories.insert(entry->path());
	}
	return directories;
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

struct WrittenValue {
	const char* file;
	const char* pointer;
	json expected;
};

// One row per key the writer can emit. The populated model sets every flag that makes the writer emit an optional key.
static const std::vector<WrittenValue>& populatedSceneTable() {
	static const std::vector<WrittenValue> table = {
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
		{"views.json", "/tracks/1/double_switch", true},
		{"views.json", "/stations/0/station", "station-1"},
		{"views.json", "/stations/0/latitude", 55.6761},
		{"views.json", "/stations/0/longitude", 12.5683},
		{"views.json", "/stations/0/regions/0/id", 1},
		{"views.json", "/stations/0/regions/0/position_km", 0.25},
		{"views.json", "/stations/0/regions/1/id", 2},
		{"views.json", "/stations/0/regions/1/position_km", 0.75},
		{"views.json", "/stations/0/corridors", json::array({"main", "branch"})},
	};
	return table;
}

static std::set<std::string> entryNames(const fs::path& directory) {
	std::set<std::string> names;
	for (const auto& entry : fs::directory_iterator(directory))
		names.insert(entry.path().filename().u8string());
	return names;
}

// Japanese and Thai characters, as UTF-8. No Windows code page holds both.
static const std::string kUnicodeName = "scene-\xE6\x97\xA5\xE6\x9C\xAC-\xE0\xB8\x81";

using Files = std::map<std::string, std::string>;

static void put(const fs::path& path, const std::string& bytes) {
	std::ofstream output(path, std::ios::binary);
	output << bytes;
}

// The errors of a scenario file as "code path: message", in the order in which the loader reported them.
static std::vector<std::string> scenarioErrors(const ScenarioLoadResult& loaded) {
	std::vector<std::string> lines;
	for (const auto& diagnostic : loaded.diagnostics) {
		if (diagnostic.file == "scenario.json" && diagnostic.severity == SceneSeverity::Error)
			lines.push_back(diagnostic.code + " " + diagnostic.path + ": " + diagnostic.message);
	}
	return lines;
}

// One error per key, worded "<what> <key>" and located at <prefix>.<key>.
static std::vector<std::string> fieldErrors(const std::string& code, const std::string& prefix, const std::string& what,
	const std::vector<const char*>& keys) {
	std::vector<std::string> lines;
	for (const char* key : keys)
		lines.push_back(code + " " + prefix + "." + key + ": " + what + " " + key);
	return lines;
}

static void append(std::vector<std::string>& lines, const std::vector<std::string>& more) {
	lines.insert(lines.end(), more.begin(), more.end());
}

class TestSceneWriter : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void savesCompleteScene();
	void failedSaveKeepsGeneration();
	void writesPlatformGeometry();
	void writesSignallingAreas();
	void omitsAbsentSignallingAreas();
	void keepsUnchosenSignallingLevel();
	void writesServiceFields();
	void writesScenarios();
	void savesStandaloneScenario();
	void savesDuplicatedScenario();
	void diagnosesInvalidScenario();
	void rejectsNonIntOccurrence_data();
	void rejectsNonIntOccurrence();
	void readsOccurrenceAtIntLimit();
	void writesMinimalScenarioBytes();
	void reportsBlockedScenarioDestination();
	void keepsScenarioOnFailedWrite();
	void writesPassengers();
	void encodesEmptyPassengerParts();
	void reloadsCompleteScene();
	void omitsLegacyDefaults();
	void roundTripsServiceColour_data();
	void roundTripsServiceColour();
	void missingAppVersionIsValid();
	void summarisesLoadedData();
	void summarisesLoadedDataStatuses();
	void savesBaselineScenarioWithoutScenarios();
	void readsHistoricalAliases();
	void insertsStopIntoCommittedService();
	void scenesFolderHoldsScenes();
	void sceneCodec_data();
	void sceneCodec();
	void populatedSceneKeys_data();
	void populatedSceneKeys();
	void populatedSceneTableIsComplete();
	void populatedSceneRoundTrips();
	void publishesSave();
	void savesUnderUnicodeNames();
	void createsStagedDirectory();
	void removesStagedDirectoryOutOfScope();
	void publishesIntoAbsentDestination();
	void publishesOverExistingDestination();
	void restoresDestinationAfterFailedRename();
	void reportsMissingDestination();
	void reportsScenarioFileDiagnostics();
	void savesEmptyNames();

private:
	TempDir m_temp;
	// The save of populatedScene() in m_temp, made once.
	fs::path m_populated;
	// The parsed files of that save, by file name.
	std::map<std::string, json> m_written;
};

void TestSceneWriter::initTestCase() {
	m_populated = m_temp.path / "populated";
	const SceneSaveResult saved = saveScene(populatedScene(), m_populated.string());
	printErrors(saved.diagnostics, "populated save");
	QVERIFY2(saved.success(), "scene codec populated scene: the model saves");
	for (const char* name : kCanonicalSceneFiles)
		m_written[name] = readJsonFile(m_populated / name);
}

// A save writes every canonical file and keeps the bytes it wrote as its snapshot.
void TestSceneWriter::savesCompleteScene() {
	TempDir temp;
	const SceneModel source = authoredScene();
	const SceneSaveResult saved = saveScene(source, temp.path.string());
	printErrors(saved.diagnostics, "save");
	// Complete canonical scene saves after a write attempt.
	QVERIFY(saved.success());
	QVERIFY(saved.writeAttempted);
	QVERIFY2(!saveScene(source, "").writeAttempted, "empty destination rejects before filesystem mutation");
	for (const char* file : {"scene.json", "infrastructure.json", "stations.json", "signalling.json",
			 "rolling_stock.json", "services.json", "scenarios.json", "passengers.json", "views.json"})
		QVERIFY2(fs::exists(temp.path / file), (std::string("all canonical files are written (") + file + ")").c_str());
	const std::string savedSnapshot = saved.inputSnapshot;
	const SceneInputSnapshot onDiskSnapshot = readSceneDirectorySnapshot(temp.path.string());
	// Successful save retains the exact framed canonical input snapshot.
	QVERIFY(!savedSnapshot.empty());
	QVERIFY(onDiskSnapshot.reason.empty());
	QVERIFY(savedSnapshot == onDiskSnapshot.bytes);
	QVERIFY2(!fs::exists(temp.path / "incidents.json"), "writer does not emit flat incidents.json");
	json savedScene;
	{
		std::ifstream input(temp.path / "scene.json");
		input >> savedScene;
	}
	QVERIFY2(savedScene["saved_with_app_version"] == EGTRAIN_APP_VERSION,
		"writer records the authoritative current app version");
}

// A save that fails leaves the previous save as it was, and the next save keeps the files that are not scene files.
void TestSceneWriter::failedSaveKeepsGeneration() {
	TempDir temp;
	const SceneModel source = authoredScene();
	const SceneSaveResult firstSave = saveScene(source, temp.path.string());
	QVERIFY2(firstSave.success(), "complete canonical scene saves");
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
	// Malformed UTF-8 fails without publishing a partial generation.
	QVERIFY(!failedSave.success());
	QVERIFY(failedSave.writeAttempted);
	QVERIFY(hasErrors(failedSave.diagnostics));
	QVERIFY2(readDirectoryBytes(temp.path) == originalGeneration,
		"failed save preserves every byte of the previous generation");
	// Failed save removes sibling staging and backup artifacts.
	QVERIFY(!hasSiblingArtifact(temp.path, "staging"));
	QVERIFY(!hasSiblingArtifact(temp.path, "backup"));
	const SceneSaveResult replacementSave = saveScene(source, temp.path.string());
	// Successful generation replacement preserves unmanaged scene contents.
	QVERIFY(replacementSave.success());
	QVERIFY(fs::exists(temp.path / "generation-marker.txt"));
	QVERIFY(fs::exists(temp.path / "notes" / "operator.txt"));
	// Load retains the exact framed snapshot emitted by save, without the files that are not scene files.
	QVERIFY(loadScene(temp.path.string()).inputSnapshot == firstSave.inputSnapshot);
}

// The platform length and width are written only when they were set.
void TestSceneWriter::writesPlatformGeometry() {
	TempDir temp;
	QVERIFY2(saveScene(authoredScene(), temp.path.string()).success(), "complete canonical scene saves");
	json stations;
	{
		std::ifstream input(temp.path / "stations.json");
		input >> stations;
	}
	// Writer emits explicitly authored platform geometry.
	QVERIFY(stations["stations"][0]["platforms"][0]["length_m"] == 125.0);
	QVERIFY(stations["stations"][0]["platforms"][0]["width_m"] == 3.75);
	// Writer omits absent platform geometry.
	QVERIFY(!stations["stations"][1]["platforms"][0].contains("length_m"));
	QVERIFY(!stations["stations"][1]["platforms"][0].contains("width_m"));
}

// The signalling areas and the section a signal protects are written with all their fields.
void TestSceneWriter::writesSignallingAreas() {
	TempDir temp;
	QVERIFY2(saveScene(authoredScene(), temp.path.string()).success(), "complete canonical scene saves");
	json signalling;
	{
		std::ifstream input(temp.path / "signalling.json");
		input >> signalling;
	}
	// Writer emits signalling area fields.
	QCOMPARE(signalling["signalling_areas"].size(), std::size_t(1));
	QVERIFY(signalling["signalling_areas"][0]["id"] == "area-1");
	QVERIFY(signalling["signalling_areas"][0]["start_km"] == 0.25);
	QVERIFY(signalling["signalling_areas"][0]["end_km"] == 0.75);
	QVERIFY(signalling["signalling_areas"][0]["level"] == 4);
	QVERIFY(signalling["signalling_areas"][0]["track"] == "track-1");
	// Writer emits an explicitly bound signal section.
	QCOMPARE(signalling["signals"].size(), std::size_t(1));
	QVERIFY(signalling["signals"][0]["id"] == "signal-1");
	QVERIFY(signalling["signals"][0]["protected_section"] == "@block-1@");
}

// A scene without signalling areas and a signal without a section are written and read without either.
void TestSceneWriter::omitsAbsentSignallingAreas() {
	TempDir temp;
	SceneModel absentAreas = completeScene();
	absentAreas.signallingAreas.clear();
	sceneSignals(absentAreas)[0].protectedSection.clear();
	const fs::path absentAreasPath = temp.path / "absent-signalling-areas";
	QVERIFY2(saveScene(absentAreas, absentAreasPath.string()).success(), "scene without signalling areas saves");
	json absentSignalling;
	{
		std::ifstream input(absentAreasPath / "signalling.json");
		input >> absentSignalling;
	}
	QVERIFY2(!absentSignalling.contains("signalling_areas"), "writer preserves an absent signalling area array");
	QVERIFY2(!absentSignalling["signals"][0].contains("protected_section"),
		"writer omits an empty protected-section binding");
	SceneModel absentAreasReloaded;
	// Scene without signalling areas reloads with no inferred defaults.
	QVERIFY(loadHasNoErrors(absentAreasPath, absentAreasReloaded));
	QVERIFY(absentAreasReloaded.signallingAreas.empty());
	// ID-only signal input round-trips without inventing a binding.
	QCOMPARE(sceneSignals(absentAreasReloaded).size(), std::size_t(1));
	QVERIFY(sceneSignals(absentAreasReloaded)[0].protectedSection.empty());
}

// An area whose signalling system is not chosen keeps that level through save and load.
void TestSceneWriter::keepsUnchosenSignallingLevel() {
	TempDir temp;
	SceneModel unchosenSystem = completeScene();
	unchosenSystem.signallingAreas[0].level = kSignallingLevelUnset;
	const fs::path unchosenSystemPath = temp.path / "unchosen-signalling-system";
	QVERIFY2(saveScene(unchosenSystem, unchosenSystemPath.string()).success(),
		"scene with an area whose signalling system is not chosen saves");
	json unchosenSignalling;
	{
		std::ifstream input(unchosenSystemPath / "signalling.json");
		input >> unchosenSignalling;
	}
	// Writer emits the level of an area whose signalling system is not chosen.
	QCOMPARE(unchosenSignalling["signalling_areas"].size(), std::size_t(1));
	QVERIFY(unchosenSignalling["signalling_areas"][0]["level"] == kSignallingLevelUnset);
	SceneModel unchosenReloaded;
	// An area whose signalling system is not chosen keeps its level through save and load.
	QVERIFY(loadHasNoErrors(unchosenSystemPath, unchosenReloaded));
	QCOMPARE(unchosenReloaded.signallingAreas.size(), std::size_t(1));
	QCOMPARE(unchosenReloaded.signallingAreas[0].level, kSignallingLevelUnset);
}

// The optional service properties are written, and the stops use the planned times without the old aliases.
void TestSceneWriter::writesServiceFields() {
	TempDir temp;
	QVERIFY2(saveScene(authoredScene(), temp.path.string()).success(), "complete canonical scene saves");
	json services;
	{
		std::ifstream input(temp.path / "services.json");
		input >> services;
	}
	const json& firstStop = services["services"][0]["stops"][0];
	QVERIFY2(services["services"][0]["operating_code"] == "R100", "writer emits the service operating code");
	// Writer emits optional performance and maximum speed.
	QVERIFY(services["services"][0]["performance_percent"] == 87.5);
	QVERIFY(services["services"][0]["maximum_speed_kmh"] == 120.0);
	// Writer emits explicit repeat count and operating-code step.
	QVERIFY(services["services"][0]["repeat"]["count"] == 3);
	QVERIFY(services["services"][0]["repeat"]["operating_code_step"] == 2);
	QVERIFY2(firstStop.contains("planned_departure_seconds"), "writer emits planned departure");
	QVERIFY2(!firstStop.contains("departure_seconds"), "writer omits legacy departure alias");
	const json& lastStop = services["services"][0]["stops"][1];
	QVERIFY2(lastStop.contains("planned_arrival_seconds"), "writer emits planned arrival");
	QVERIFY2(!lastStop.contains("arrival_seconds"), "writer omits legacy arrival alias");
}

// The scenarios are written with the default scenario and the fields of an enhanced breakdown.
void TestSceneWriter::writesScenarios() {
	TempDir temp;
	QVERIFY2(saveScene(authoredScene(), temp.path.string()).success(), "complete canonical scene saves");
	json scenarios;
	{
		std::ifstream input(temp.path / "scenarios.json");
		input >> scenarios;
	}
	QVERIFY2(scenarios["default_scenario_id"] == "baseline", "writer emits default_scenario_id");
	// Writer emits named scenarios.
	QCOMPARE(scenarios["scenarios"].size(), std::size_t(2));
	const json& enhancedJson = scenarios["scenarios"][1]["incidents"][0];
	// Writer preserves occurrence-specific reduced breakdown without recovery end.
	QVERIFY(enhancedJson["occurrence"] == 2);
	QVERIFY(enhancedJson["reduced_speed_kmh"] == 40.0);
	QVERIFY(enhancedJson["terminate_at_destination"] == true);
	QVERIFY(!enhancedJson.contains("end_seconds"));
}

// A scenario saved on its own holds only scenario data, reads back, and is replaced by the next save.
void TestSceneWriter::savesStandaloneScenario() {
	TempDir temp;
	const SceneModel source = authoredScene();
	const fs::path standaloneScenarioPath = temp.path / "baseline-scenario.json";
	const SceneSaveResult standaloneSave = saveScenarioJson(source.scenarios[0], standaloneScenarioPath.string());
	// Standalone scenario JSON saves after a write attempt.
	QVERIFY(standaloneSave.success());
	QVERIFY(standaloneSave.writeAttempted);
	json standaloneScenario;
	{
		std::ifstream input(standaloneScenarioPath);
		input >> standaloneScenario;
	}
	// Standalone scenario JSON contains only scenario data.
	QVERIFY(standaloneScenario.is_object());
	QVERIFY(standaloneScenario["id"] == "baseline");
	QCOMPARE(standaloneScenario["entrance_delays"].size(), std::size_t(1));
	QVERIFY(!standaloneScenario.contains("scenarios"));
	QVERIFY(!standaloneScenario.contains("infrastructure"));
	const ScenarioLoadResult standaloneLoad = loadScenarioJson(standaloneScenarioPath.string());
	// Standalone scenario JSON round-trips incidents and entrance delays.
	QVERIFY(standaloneLoad.success());
	QCOMPARE(standaloneLoad.scenario.id, source.scenarios[0].id);
	QCOMPARE(standaloneLoad.scenario.description, source.scenarios[0].description);
	QCOMPARE(standaloneLoad.scenario.incidents.size(), source.scenarios[0].incidents.size());
	QCOMPARE(standaloneLoad.scenario.entranceDelays.size(), std::size_t(1));
	QCOMPARE(standaloneLoad.scenario.entranceDelays[0].serviceId, "service-1");
	QCOMPARE(standaloneLoad.scenario.entranceDelays[0].occurrence, 1);
	QCOMPARE(standaloneLoad.scenario.entranceDelays[0].stationId, "station-1");
	QCOMPARE_EQ(standaloneLoad.scenario.entranceDelays[0].delaySeconds, 30.0);

	const std::string standaloneBytes = readBytes(standaloneScenarioPath);
	// Standalone scenario JSON keeps four-space indentation and a final newline.
	QCOMPARE(standaloneBytes, json::parse(standaloneBytes).dump(4) + "\n");
	SceneScenario replacement;
	replacement.id = "replacement";
	replacement.name = "Minimal";
	QVERIFY2(saveScenarioJson(replacement, standaloneScenarioPath.string()).success(),
		"standalone scenario replaces an existing file");
	// Replaced standalone scenario holds the new content.
	QCOMPARE(loadScenarioJson(standaloneScenarioPath.string()).scenario.id, "replacement");
	QVERIFY(readBytes(standaloneScenarioPath) != standaloneBytes);
	QVERIFY2(!hasSiblingArtifact(standaloneScenarioPath, "tmp"),
		"standalone scenario save leaves no temporary file after success");
}

// A copy of a scenario with new IDs saves and reads back with those IDs.
void TestSceneWriter::savesDuplicatedScenario() {
	TempDir temp;
	SceneScenario duplicateScenario = authoredScene().scenarios[0];
	duplicateScenario.id = "baseline-copy";
	duplicateScenario.incidents[0].id = "incident-copy";
	const fs::path duplicateScenarioPath = temp.path / "baseline-copy.json";
	QVERIFY2(saveScenarioJson(duplicateScenario, duplicateScenarioPath.string()).success(),
		"duplicated scenario saves through the standalone boundary");
	const ScenarioLoadResult duplicateLoad = loadScenarioJson(duplicateScenarioPath.string());
	// Duplicated scenario round-trips with independent IDs.
	QVERIFY(duplicateLoad.success());
	QCOMPARE(duplicateLoad.scenario.id, "baseline-copy");
	QCOMPARE(duplicateLoad.scenario.incidents[0].id, "incident-copy");
}

// A scenario file with a name of the wrong type and an incomplete incident is rejected.
void TestSceneWriter::diagnosesInvalidScenario() {
	TempDir temp;
	const fs::path invalidScenarioPath = temp.path / "invalid-scenario.json";
	{
		std::ofstream output(invalidScenarioPath);
		output << R"({"id":"broken","name":5,"incidents":[{"id":"only-id"}]})";
	}
	const ScenarioLoadResult invalidScenario = loadScenarioJson(invalidScenarioPath.string());
	// Standalone scenario parser diagnoses structural and type errors.
	QVERIFY(!invalidScenario.success());
	QVERIFY(hasErrors(invalidScenario.diagnostics));
	QVERIFY(!invalidScenario.diagnostics.empty());
}

void TestSceneWriter::rejectsNonIntOccurrence_data() {
	// The occurrence as JSON text.
	QTest::addColumn<QString>("occurrence");
	QTest::newRow("wraps to 2") << QString("4294967298");
	QTest::newRow("one above the int range") << QString("2147483648");
	QTest::newRow("one below the int range") << QString("-2147483649");
	QTest::newRow("fraction") << QString("1.5");
}

// An entrance delay occurrence that is not an int is rejected at its key.
void TestSceneWriter::rejectsNonIntOccurrence() {
	QFETCH(QString, occurrence);
	TempDir temp;
	const fs::path invalidScenarioPath = temp.path / "invalid-scenario.json";
	{
		std::ofstream output(invalidScenarioPath, std::ios::trunc);
		output << R"({"id":"wide","name":"wide","incidents":[],"entrance_delays":[{"service":"s","station":"t","delay_seconds":1,"occurrence":)"
			   << occurrence.toStdString() << "}]}";
	}
	const ScenarioLoadResult wideScenario = loadScenarioJson(invalidScenarioPath.string());
	// Standalone scenario rejects an occurrence that is not an int.
	QVERIFY(!wideScenario.success());
	QVERIFY(!wideScenario.diagnostics.empty());
	QCOMPARE(wideScenario.diagnostics.front().path, "entrance_delays[0].occurrence");
}

// An entrance delay occurrence at the int limit is read.
void TestSceneWriter::readsOccurrenceAtIntLimit() {
	TempDir temp;
	const fs::path invalidScenarioPath = temp.path / "invalid-scenario.json";
	{
		std::ofstream output(invalidScenarioPath, std::ios::trunc);
		output << R"({"id":"edge","name":"edge","incidents":[],"entrance_delays":[{"service":"s","station":"t","delay_seconds":1,"occurrence":2147483647}]})";
	}
	const ScenarioLoadResult edgeScenario = loadScenarioJson(invalidScenarioPath.string());
	// Standalone scenario reads an occurrence at the int limit.
	QVERIFY(edgeScenario.success());
	QCOMPARE(edgeScenario.scenario.entranceDelays.size(), std::size_t(1));
	QCOMPARE(edgeScenario.scenario.entranceDelays[0].occurrence, 2147483647);
}

// A scenario with only an ID and a name is written as these exact bytes.
void TestSceneWriter::writesMinimalScenarioBytes() {
	TempDir temp;
	SceneScenario minimal;
	minimal.id = "min";
	minimal.name = "Minimal";
	const fs::path minimalPath = temp.path / "minimal-scenario.json";
	QVERIFY2(saveScenarioJson(minimal, minimalPath.string()).success(), "minimal standalone scenario saves");
	// Standalone scenario bytes match the expected file.
	QCOMPARE(readBytes(minimalPath), "{\n"
									 "    \"entrance_delays\": [],\n"
									 "    \"id\": \"min\",\n"
									 "    \"incidents\": [],\n"
									 "    \"name\": \"Minimal\"\n"
									 "}\n");
}

// A scenario save into a folder or into a missing folder fails and changes nothing.
void TestSceneWriter::reportsBlockedScenarioDestination() {
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
	// Standalone scenario save reports a destination that cannot be replaced.
	QVERIFY(!blocked.success());
	QVERIFY(!blocked.wroteAll);
	QVERIFY(hasErrors(blocked.diagnostics));
	// Failed replace leaves the destination and no temporary file.
	QVERIFY(fs::is_directory(blockedPath));
	QCOMPARE(readBytes(blockedPath / "keep.txt"), "keep");
	QVERIFY(!hasSiblingArtifact(blockedPath, "tmp"));

	const SceneSaveResult missing = saveScenarioJson(scenario, (failing.path / "missing" / "scenario.json").string());
	// Standalone scenario save reports a missing directory.
	QVERIFY(!missing.success());
	QVERIFY(hasErrors(missing.diagnostics));
	QVERIFY(!fs::exists(failing.path / "missing"));
}

// A scenario save that cannot write its file keeps the old file and leaves no temporary file. The permissions are
// restored before the checks, so that a failing check leaves a folder that can be removed.
void TestSceneWriter::keepsScenarioOnFailedWrite() {
#ifdef _WIN32
	QSKIP("file size limits and permission bits are POSIX");
#else
	TempDir failing;
	const fs::path scenarioPath = failing.path / "scenario.json";
	const std::string original = "original scenario\n";
	{
		std::ofstream output(scenarioPath, std::ios::binary);
		output << original;
	}
	SceneScenario scenario = authoredScene().scenarios[0];
	bool limited = false;
	SceneSaveResult truncated;
	{
		// The write exceeds the limit part way and fails.
		const FileSizeLimit limit(16);
		limited = limit.active;
		truncated = saveScenarioJson(scenario, scenarioPath.string());
	}
	QVERIFY2(limited, "file size limit can be applied");
	// Standalone scenario save reports a write that cannot complete.
	QVERIFY(!truncated.success());
	QVERIFY(hasErrors(truncated.diagnostics));
	// Failed write leaves the original scenario and no temporary file.
	QCOMPARE(readBytes(scenarioPath), original);
	QVERIFY(hasOnlyFile(failing.path, "scenario.json"));

	// A read-only directory refuses the temporary file. This does not apply to root.
	fs::permissions(failing.path, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
	const bool readOnlyDirectory = access(failing.path.c_str(), W_OK) != 0;
	SceneSaveResult readOnly;
	if (readOnlyDirectory)
		readOnly = saveScenarioJson(scenario, scenarioPath.string());
	fs::permissions(failing.path, fs::perms::owner_all, fs::perm_options::replace);
	if (readOnlyDirectory) {
		// Standalone scenario save reports a read-only directory.
		QVERIFY(!readOnly.success());
		QVERIFY(hasErrors(readOnly.diagnostics));
		// Read-only directory leaves the original scenario.
		QCOMPARE(readBytes(scenarioPath), original);
	}
	QVERIFY2(hasOnlyFile(failing.path, "scenario.json"), "read-only directory leaves no temporary file");

	// A read-only file is not replaced. This does not apply to root.
	fs::permissions(scenarioPath, fs::perms::owner_read, fs::perm_options::replace);
	const bool readOnlyFile = access(scenarioPath.c_str(), W_OK) != 0;
	SceneSaveResult readOnlyFileSave;
	if (readOnlyFile)
		readOnlyFileSave = saveScenarioJson(scenario, scenarioPath.string());
	fs::permissions(scenarioPath, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
	if (readOnlyFile) {
		// Standalone scenario save reports a read-only file.
		QVERIFY(!readOnlyFileSave.success());
		QVERIFY(hasErrors(readOnlyFileSave.diagnostics));
		// Read-only file keeps its content and no temporary file remains.
		QCOMPARE(readBytes(scenarioPath), original);
		QVERIFY(hasOnlyFile(failing.path, "scenario.json"));
	}
#endif
}

// The passengers are written as the passenger encoder writes them.
void TestSceneWriter::writesPassengers() {
	TempDir temp;
	const SceneModel source = authoredScene();
	QVERIFY2(saveScene(source, temp.path.string()).success(), "complete canonical scene saves");
	json passengers;
	{
		std::ifstream input(temp.path / "passengers.json");
		input >> passengers;
	}
	QVERIFY2(passengers["passengers"][0]["journeys"][0].contains("planned_departure"),
		"writer emits passenger departure window");
	// Writer emits ordered passenger legs.
	QCOMPARE(passengers["passengers"][0]["journeys"][0]["legs"].size(), std::size_t(1));
	QVERIFY2(passengers == encodePassengers(source.passengers), "saved passengers.json is the encoded passengers");
}

// The passenger encoder leaves out what is empty, except the windows and the leg list.
void TestSceneWriter::encodesEmptyPassengerParts() {
	ScenePassenger passenger;
	passenger.id = "p";
	passenger.journeys.resize(1);
	const json encoded = encodePassengers({passenger});
	const json& journey = encoded["passengers"][0]["journeys"][0];
	// Encoder leaves out an empty activity and keeps the windows and an empty leg list.
	QVERIFY(!journey.contains("activity"));
	QVERIFY(journey["legs"].empty());
	QCOMPARE(journey["planned_arrival"].size(), std::size_t(2));
	QVERIFY2(encodePassengers({}) == json({{"passengers", json::array()}}),
		"encoder writes an empty passenger list as an empty array");
}

// A saved scene loads with every field it was saved with, and its snapshot is the bytes of the save.
void TestSceneWriter::reloadsCompleteScene() {
	TempDir temp;
	const SceneSaveResult saved = saveScene(authoredScene(), temp.path.string());
	QVERIFY2(saved.success(), "complete canonical scene saves");
	const std::string savedSnapshot = saved.inputSnapshot;
	SceneModel reloaded;
	QVERIFY2(loadHasNoErrors(temp.path, reloaded), "canonical scene reloads without structural errors");
	const SceneLoadResult loadedSnapshot = loadScene(temp.path.string());
	// Load retains the exact framed snapshot emitted by save.
	QVERIFY(!loadedSnapshot.inputSnapshot.empty());
	QVERIFY(loadedSnapshot.inputSnapshot == savedSnapshot);
	{
		std::ofstream output(temp.path / "services.json", std::ios::binary | std::ios::app);
		output << ' ';
	}
	const SceneInputSnapshot changedOnDisk = readSceneDirectorySnapshot(temp.path.string());
	// External canonical-file changes do not mutate retained snapshots.
	QVERIFY(changedOnDisk.reason.empty());
	QVERIFY(changedOnDisk.bytes != savedSnapshot);
	QVERIFY(saved.inputSnapshot == savedSnapshot);
	QVERIFY(loadedSnapshot.inputSnapshot == savedSnapshot);
	// Topology round-trips.
	QCOMPARE(reloaded.tracks.size(), std::size_t(1));
	QCOMPARE(reloaded.nodes.size(), std::size_t(2));
	QCOMPARE(reloaded.blocks.size(), std::size_t(2));
	// Authored display layout round-trips.
	QCOMPARE(reloaded.trackViews.size(), std::size_t(1));
	QCOMPARE(reloaded.trackViews[0].trackId, "track-1");
	QCOMPARE(reloaded.trackViews[0].level, -2);
	QCOMPARE(reloaded.trackViews[0].region, 1);
	QVERIFY(!reloaded.trackViews[0].visible);
	QCOMPARE(reloaded.stationViews.size(), std::size_t(1));
	QCOMPARE(reloaded.stationViews[0].stationId, "station-1");
	QVERIFY((reloaded.stationViews[0].regions == std::vector<std::pair<int, double>>({{1, 0.25}, {2, 0.75}})));
	QVERIFY((reloaded.stationViews[0].corridors == std::vector<std::string>({"main", "branch"})));
	// Route corridor and direction round-trip.
	QCOMPARE(reloaded.routes[0].corridor, "corridor-1");
	QVERIFY(reloaded.routes[0].reversed);
	// Signalling area fields round-trip.
	QCOMPARE(reloaded.signallingAreas.size(), std::size_t(1));
	QCOMPARE(reloaded.signallingAreas[0].id, "area-1");
	QCOMPARE_EQ(reloaded.signallingAreas[0].startKm, 0.25);
	QCOMPARE_EQ(reloaded.signallingAreas[0].endKm, 0.75);
	QCOMPARE(reloaded.signallingAreas[0].level, 4);
	QCOMPARE(reloaded.signallingAreas[0].trackId, "track-1");
	// Protected-section binding round-trips through folder JSON.
	QCOMPARE(sceneSignals(reloaded).size(), std::size_t(1));
	QCOMPARE(sceneSignals(reloaded)[0].protectedSection, "@block-1@");
	// Planned departure round-trips.
	QVERIFY(reloaded.services[0].stops[0].hasPlannedDeparture);
	QCOMPARE_EQ(reloaded.services[0].stops[0].plannedDepartureSeconds, 100.0);
	// Service operating code round-trips.
	QCOMPARE(reloaded.services[0].operatingCode, "R100");
	// Platform geometry presence and effective defaults round-trip.
	QVERIFY(reloaded.stations[0].platforms[0].hasLength);
	QCOMPARE_EQ(reloaded.stations[0].platforms[0].lengthM, 125.0);
	QVERIFY(reloaded.stations[0].platforms[0].hasWidth);
	QCOMPARE_EQ(reloaded.stations[0].platforms[0].widthM, 3.75);
	QVERIFY(!reloaded.stations[1].platforms[0].hasLength);
	QCOMPARE_EQ(reloaded.stations[1].platforms[0].lengthM, 100.0);
	QVERIFY(!reloaded.stations[1].platforms[0].hasWidth);
	QCOMPARE_EQ(reloaded.stations[1].platforms[0].widthM, 2.5);
	// Optional service runtime properties round-trip.
	QCOMPARE_EQ(reloaded.services[0].performancePercent, 87.5);
	QVERIFY(reloaded.services[0].hasMaximumSpeed);
	QCOMPARE_EQ(reloaded.services[0].maximumSpeedKmh, 120.0);
	QVERIFY(reloaded.services[0].hasRepeatCount);
	QCOMPARE(reloaded.services[0].repeatCount, 3);
	QVERIFY(reloaded.services[0].hasOperatingCodeStep);
	QCOMPARE(reloaded.services[0].operatingCodeStep, 2);
	// Enhanced breakdown fields round-trip through canonical scene files.
	QCOMPARE(reloaded.scenarios[1].incidents.size(), std::size_t(1));
	QVERIFY(reloaded.scenarios[1].incidents[0].hasOccurrence);
	QCOMPARE(reloaded.scenarios[1].incidents[0].occurrence, 2);
	QVERIFY(reloaded.scenarios[1].incidents[0].hasReducedSpeed);
	QCOMPARE_EQ(reloaded.scenarios[1].incidents[0].reducedSpeedKmh, 40.0);
	QVERIFY(!reloaded.scenarios[1].incidents[0].hasEndSeconds);
	QVERIFY(reloaded.scenarios[1].incidents[0].terminateAtDestination);
	// Planned arrival round-trips.
	QVERIFY(reloaded.services[0].stops[1].hasPlannedArrival);
	QCOMPARE_EQ(reloaded.services[0].stops[1].plannedArrivalSeconds, 200.0);
	// Scenario selection round-trips.
	QCOMPARE(reloaded.defaultScenarioId, "baseline");
	QCOMPARE(reloaded.scenarios.size(), std::size_t(2));
	// Passenger journey round-trips.
	QCOMPARE(reloaded.passengers.size(), std::size_t(1));
	QCOMPARE(reloaded.passengers[0].journeys.size(), std::size_t(1));
	// Rolling provenance fields are independently optional.
	QCOMPARE(reloaded.trainUnits[0].sourceDataFile, "/TrainData/unit-1.txt");
	QVERIFY(reloaded.trainUnits[0].sourceTractionFile.empty());
	// Saved app version round-trips.
	QCOMPARE(reloaded.savedWithAppVersion, EGTRAIN_APP_VERSION);
}

// A service with default properties is written without them, and the loader takes a missing category or colour and
// rejects one of the wrong type.
void TestSceneWriter::omitsLegacyDefaults() {
	TempDir temp;
	SceneModel legacyDefaults = completeScene();
	legacyDefaults.scenarios[1].incidents[0].hasReducedSpeed = false;
	legacyDefaults.scenarios[1].incidents[0].reducedSpeedKmh = 40.125;
	const fs::path legacyDefaultsPath = temp.path / "legacy-defaults";
	QVERIFY2(saveScene(legacyDefaults, legacyDefaultsPath.string()).success(), "legacy-default service still saves");
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
	// Writer preserves a nonzero reduced speed when its presence flag is stale.
	QVERIFY(normalizedScenarios["scenarios"][1]["incidents"][0]["reduced_speed_kmh"] == 40.125);
	QVERIFY(!normalizedScenarios["scenarios"][1]["incidents"][0].contains("end_seconds"));
	// Default service properties remain omitted for legacy scenes.
	QVERIFY(!legacyServices["services"][0].contains("performance_percent"));
	QVERIFY(!legacyServices["services"][0].contains("category"));
	QVERIFY(!legacyServices["services"][0].contains("visualization_color"));
	QVERIFY(!legacyServices["services"][0].contains("maximum_speed_kmh"));
	QVERIFY(!legacyServices["services"][0].contains("repeat"));
	SceneModel missingCategory;
	// Missing category defaults to empty.
	QVERIFY(loadHasNoErrors(legacyDefaultsPath, missingCategory));
	QVERIFY(missingCategory.services[0].category.empty());
	legacyServices["services"][0]["category"] = 42;
	{
		std::ofstream output(legacyDefaultsPath / "services.json");
		output << legacyServices.dump(2) << "\n";
	}
	QVERIFY2(hasErrors(loadScene(legacyDefaultsPath.string()).diagnostics), "non-string category is rejected");
	SceneModel missingColor;
	legacyServices["services"][0].erase("category");
	{
		std::ofstream output(legacyDefaultsPath / "services.json");
		output << legacyServices.dump(2) << "\n";
	}
	// Missing colour defaults to empty.
	QVERIFY(loadHasNoErrors(legacyDefaultsPath, missingColor));
	QVERIFY(missingColor.services[0].visualizationColor.empty());
	legacyServices["services"][0]["visualization_color"] = 42;
	{
		std::ofstream output(legacyDefaultsPath / "services.json");
		output << legacyServices.dump(2) << "\n";
	}
	QVERIFY2(hasErrors(loadScene(legacyDefaultsPath.string()).diagnostics),
		"non-string visualization colour is rejected");
}

void TestSceneWriter::roundTripsServiceColour_data() {
	QTest::addColumn<QString>("color");
	QTest::newRow("lower case") << QString("#3c8dd2");
	QTest::newRow("upper case") << QString("#3C8DD2");
	QTest::newRow("mixed case") << QString("#3c8DD2");
	QTest::newRow("not a colour") << QString("not-a-colour");
}

// A service colour is written and read back as given, without a check of its form.
void TestSceneWriter::roundTripsServiceColour() {
	QFETCH(QString, color);
	const std::string given = color.toStdString();
	TempDir temp;
	SceneModel colored = completeScene();
	colored.services[0].visualizationColor = given;
	const fs::path coloredPath = temp.path / "colored-service";
	QVERIFY2(saveScene(colored, coloredPath.string()).success(), "service with colour saves");
	json coloredServices;
	{
		std::ifstream input(coloredPath / "services.json");
		input >> coloredServices;
	}
	SceneModel coloredReloaded;
	// Service colour is written and read back exactly as given.
	QCOMPARE(coloredServices["services"][0].value("visualization_color", ""), given);
	QVERIFY(loadHasNoErrors(coloredPath, coloredReloaded));
	QCOMPARE(coloredReloaded.services[0].visualizationColor, given);
}

// A scene.json without the saved app version loads.
void TestSceneWriter::missingAppVersionIsValid() {
	TempDir temp;
	const fs::path missingVersionPath = temp.path / "missing-version";
	QVERIFY2(saveScene(authoredScene(), missingVersionPath.string()).success(),
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
	// Missing saved app version remains valid and empty.
	QVERIFY(loadHasNoErrors(missingVersionPath, missingVersion));
	QVERIFY(missingVersion.savedWithAppVersion.empty());
}

// The student-facing loaded-data summary distinguishes source, parsed, optional, and validation states and carries
// only concrete editor targets.
void TestSceneWriter::summarisesLoadedData() {
	TempDir temp;
	QVERIFY2(saveScene(authoredScene(), temp.path.string()).success(), "complete canonical scene saves");
	SceneModel reloaded;
	QVERIFY2(loadHasNoErrors(temp.path, reloaded), "canonical scene reloads without structural errors");
	const std::vector<SceneLoadedData> summary = buildLoadedDataSummary(reloaded, reloaded.sourceFiles, {});
	const auto findCategory = [](const std::vector<SceneLoadedData>& rows,
								  const std::string& category) -> const SceneLoadedData* {
		for (const auto& row : rows) {
			if (row.category == category)
				return &row;
		}
		return nullptr;
	};
	const SceneLoadedData* rolling = findCategory(summary, "rolling_stock");
	const SceneLoadedData* units = rolling ? findCategory(rolling->children, "train_units") : nullptr;
	// Loaded category reports parsed canonical data.
	if (rolling == nullptr)
		QFAIL("there is no rolling_stock row");
	QCOMPARE(rolling->status, SceneLoadedStatus::Parsed);
	// Loaded train-unit row owns its editor target, data, curve, and provenance.
	if (units == nullptr)
		QFAIL("there is no train_units row");
	QVERIFY(!units->children.empty());
	QCOMPARE(units->children.front().targetType, "train_unit");
	QCOMPARE(units->children.front().category, "unit-1");
	QVERIFY(findCategory(units->children.front().children, "train_unit_parameters"));
	QVERIFY(findCategory(units->children.front().children, "tractive_effort_curve"));
	QVERIFY(findCategory(units->children.front().children, "import_provenance"));
	const SceneLoadedData* infrastructure = findCategory(summary, "infrastructure");
	// Infrastructure summary resolves to the existing network view.
	if (infrastructure == nullptr)
		QFAIL("there is no infrastructure row");
	QCOMPARE(infrastructure->targetType, "network");
	{
		SceneModel twins = reloaded;
		SceneScenario first;
		first.id = "twin_a";
		first.name = "Twin";
		SceneScenario second = first;
		second.id = "twin_b";
		SceneScenario other;
		other.id = "solo";
		other.name = "Solo";
		twins.scenarios = {first, second, other};
		twins.defaultScenarioId = "twin_a";
		const std::vector<SceneLoadedData> twinSummary = buildLoadedDataSummary(twins, twins.sourceFiles, {});
		const SceneLoadedData* scenarioRows = findCategory(twinSummary, "scenarios");
		const SceneLoadedData* available = scenarioRows ? findCategory(scenarioRows->children, "available_scenarios") : nullptr;
		std::vector<std::string> labels;
		if (available)
			for (const auto& row : available->children)
				labels.push_back(row.category);
		QVERIFY2((labels == std::vector<std::string>({"Twin (twin_a, default)", "Twin (twin_b)", "Solo"})),
			"two scenarios with one name are told apart by their IDs in the loaded data");
	}

	SceneModel withoutOptional = reloaded;
	withoutOptional.sourceFiles.erase("scenarios.json");
	withoutOptional.sourceFiles.erase("passengers.json");
	withoutOptional.scenarios.clear();
	withoutOptional.passengers.clear();
	const std::vector<SceneLoadedData> optionalSummary = buildLoadedDataSummary(withoutOptional, withoutOptional.sourceFiles, {});
	const SceneLoadedData* scenariosRow = findCategory(optionalSummary, "scenarios");
	const SceneLoadedData* passengersRow = findCategory(optionalSummary, "passengers");
	// Absent optional scene inputs are labelled explicitly.
	if (scenariosRow == nullptr)
		QFAIL("there is no scenarios row");
	QCOMPARE(scenariosRow->status, SceneLoadedStatus::MissingOptional);
	if (passengersRow == nullptr)
		QFAIL("there is no passengers row");
	QCOMPARE(passengersRow->status, SceneLoadedStatus::MissingOptional);
	QVERIFY2(withoutOptional.scenarios.empty(), "loaded-data refresh does not create canonical input");

	SceneDiagnostic rollingWarning;
	rollingWarning.severity = SceneSeverity::Warning;
	rollingWarning.file = "rolling_stock.json";
	const std::vector<SceneLoadedData> warned = buildLoadedDataSummary(reloaded, reloaded.sourceFiles, {rollingWarning});
	rolling = findCategory(warned, "rolling_stock");
	// Validation warning is visible on its loaded-data category.
	if (rolling == nullptr)
		QFAIL("there is no rolling_stock row");
	QCOMPARE(rolling->status, SceneLoadedStatus::Warning);
	QVERIFY(!rolling->children.empty());
	QCOMPARE(rolling->children.back().status, SceneLoadedStatus::Warning);
}

// The Loaded Data summary of a small scene without passengers.json and with one warning gives each row its status.
void TestSceneWriter::summarisesLoadedDataStatuses() {
	SceneModel scene;
	scene.schemaVersion = 1;
	scene.sourceFiles = {"scene.json", "infrastructure.json", "stations.json", "signalling.json", "rolling_stock.json",
		"services.json", "scenarios.json"};
	scene.tracks.push_back({"track-1"});
	SceneTrainUnit unit;
	unit.id = "unit-1";
	unit.tractionCurve.push_back({0.0, 100.0, 0.0, 0.0, 0.0});
	scene.trainUnits.push_back(unit);
	SceneService service;
	service.id = "service-1";
	scene.services.push_back(service);
	scene.scenarios.push_back(makeBaselineScenario());
	scene.defaultScenarioId = scene.scenarios[0].id;
	SceneDiagnostic warning;
	warning.severity = SceneSeverity::Warning;
	warning.code = "scene.test.warning";
	warning.file = "services.json";

	const std::vector<SceneLoadedData> summary = buildLoadedDataSummary(scene, scene.sourceFiles, {warning});
	// A missing row fails the test, because the default status Parsed would pass the checks that expect it.
	const auto row = [](const std::vector<SceneLoadedData>& rows, const std::string& category) -> const SceneLoadedData& {
		static const SceneLoadedData none;
		for (const auto& item : rows)
			if (item.category == category)
				return item;
		QTest::qFail(("no row " + category).c_str(), __FILE__, __LINE__);
		return none;
	};
	QCOMPARE(summary.size(), std::size_t(8));
	// A present file is Parsed, its source file Loaded, and a validation row without diagnostics Ready.
	const SceneLoadedData& sceneRow = row(summary, "scene");
	QCOMPARE(sceneRow.status, SceneLoadedStatus::Parsed);
	QCOMPARE(row(sceneRow.children, "source_file").status, SceneLoadedStatus::Loaded);
	QCOMPARE(row(sceneRow.children, "parsed_objects").status, SceneLoadedStatus::Parsed);
	QCOMPARE(row(sceneRow.children, "validation").status, SceneLoadedStatus::Ready);
	QCOMPARE(row(sceneRow.children, "validation").parsedCount, 0);
	// A scene without an import report has it as a missing optional part.
	QCOMPARE(row(sceneRow.children, "import_report").status, SceneLoadedStatus::MissingOptional);
	QCOMPARE(row(summary, "infrastructure").status, SceneLoadedStatus::Parsed);
	QCOMPARE(row(row(summary, "infrastructure").children, "tracks").parsedCount, 1);
	// The warning of services.json marks the timetable row and its validation row.
	const SceneLoadedData& timetable = row(summary, "timetable");
	QCOMPARE(timetable.status, SceneLoadedStatus::Warning);
	QCOMPARE(timetable.children.back().category, "validation");
	QCOMPARE(timetable.children.back().status, SceneLoadedStatus::Warning);
	QCOMPARE(timetable.children.back().parsedCount, 1);
	QCOMPARE(row(timetable.children, "services").status, SceneLoadedStatus::Parsed);
	// A unit without physical values has them Invalid; its traction curve can be plotted.
	const SceneLoadedData& unitRow = row(row(row(summary, "rolling_stock").children, "train_units").children, "unit-1");
	QCOMPARE(unitRow.targetType, "train_unit");
	QCOMPARE(row(unitRow.children, "train_unit_parameters").status, SceneLoadedStatus::Invalid);
	const SceneLoadedData& curve = row(unitRow.children, "tractive_effort_curve");
	QCOMPARE(curve.status, SceneLoadedStatus::Parsed);
	QCOMPARE(row(curve.children, "Plot traction curve").status, SceneLoadedStatus::Ready);
	QCOMPARE(row(unitRow.children, "import_provenance").status, SceneLoadedStatus::MissingOptional);
	QCOMPARE(row(summary, "scenarios").status, SceneLoadedStatus::Parsed);
	// The missing optional passengers.json, and its source file and objects.
	const SceneLoadedData& passengers = row(summary, "passengers");
	QCOMPARE(passengers.status, SceneLoadedStatus::MissingOptional);
	QCOMPARE(row(passengers.children, "source_file").status, SceneLoadedStatus::MissingOptional);
	QCOMPARE(row(passengers.children, "parsed_objects").status, SceneLoadedStatus::MissingOptional);
	QCOMPARE(row(passengers.children, "validation").status, SceneLoadedStatus::Ready);

	// A required file that is missing is Invalid, and an error of its file makes its row Invalid too.
	std::set<std::string> withoutSignalling = scene.sourceFiles;
	withoutSignalling.erase("signalling.json");
	SceneDiagnostic error = warning;
	error.severity = SceneSeverity::Error;
	error.file = "stations.json";
	const std::vector<SceneLoadedData> broken = buildLoadedDataSummary(scene, withoutSignalling, {error});
	QCOMPARE(row(broken, "signalling").status, SceneLoadedStatus::Invalid);
	QCOMPARE(row(row(broken, "signalling").children, "source_file").status, SceneLoadedStatus::Invalid);
	QCOMPARE(row(broken, "stations").status, SceneLoadedStatus::Invalid);
	QCOMPARE(row(broken, "stations").children.back().status, SceneLoadedStatus::Invalid);
	QCOMPARE(row(broken, "timetable").status, SceneLoadedStatus::Parsed);
}

// A scene without scenarios is written with the baseline scenario.
void TestSceneWriter::savesBaselineScenarioWithoutScenarios() {
	const SceneScenario baseline = makeBaselineScenario();
	// The baseline scenario has no incidents or entrance delays.
	QCOMPARE(baseline.id, "baseline");
	QCOMPARE(baseline.name, "Baseline");
	QVERIFY(baseline.incidents.empty());
	QVERIFY(baseline.entranceDelays.empty());
	TempDir noScenarioDir;
	SceneModel noScenarios = authoredScene();
	noScenarios.scenarios.clear();
	noScenarios.defaultScenarioId.clear();
	const SceneSaveResult noScenarioSave = saveScene(noScenarios, noScenarioDir.path.string());
	const json noScenarioFile = readJsonFile(noScenarioDir.path / "scenarios.json");
	// A scene without scenarios is saved with the baseline scenario.
	QVERIFY(noScenarioSave.success());
	QVERIFY(noScenarioFile["default_scenario_id"] == "baseline");
	QCOMPARE(noScenarioFile["scenarios"].size(), std::size_t(1));
	QVERIFY(noScenarioFile["scenarios"][0]["id"] == "baseline");
	QVERIFY(noScenarioFile["scenarios"][0]["name"] == "Baseline");
	// A new scene and an editor request default to the baseline scenario.
	QCOMPARE(makeNewSceneModel().defaultScenarioId, baseline.id);
	QCOMPARE(defaultScenario(noScenarios)->id, baseline.id);
}

// Historical aliases and flat incidents remain readable during migration, and the next save removes the flat file.
void TestSceneWriter::readsHistoricalAliases() {
	TempDir temp;
	const SceneModel source = authoredScene();
	QVERIFY2(saveScene(source, temp.path.string()).success(), "complete canonical scene saves");
	json services;
	{
		std::ifstream input(temp.path / "services.json");
		input >> services;
	}
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
	QVERIFY2(loadHasNoErrors(temp.path, historical), "historical aliases and flat incidents load");
	// Flat incidents become the implicit baseline.
	QCOMPARE(historical.defaultScenarioId, "baseline");
	QCOMPARE(historical.scenarios.size(), std::size_t(1));
	// Flat incident and timetable aliases migrate into canonical fields.
	QCOMPARE(historical.scenarios[0].incidents.size(), std::size_t(1));
	QVERIFY(historical.services[0].stops[0].hasPlannedDeparture);

	// A later successful save removes the stale compatibility file.
	SceneSaveResult resaved = saveScene(source, temp.path.string());
	// Successful canonical save removes stale incidents after scenarios write.
	QVERIFY(resaved.success());
	QVERIFY(!fs::exists(temp.path / "incidents.json"));
}

// A stop inserted into a committed service keeps its place and every planned time through save and reload.
void TestSceneWriter::insertsStopIntoCommittedService() {
	TempDir temp;
	SceneModel committed;
	QVERIFY2(loadHasNoErrors(fs::path(EGTRAIN_SCENES_DIR) / "Paimpol", committed), "committed Paimpol scene loads");
	SceneService* service = nullptr;
	for (SceneService& candidate : committed.services)
		if (candidate.id == "Guin-Paim-EXPRESS-1")
			service = &candidate;
	// Committed service has three stops.
	if (service == nullptr)
		QFAIL("there is no service Guin-Paim-EXPRESS-1");
	QCOMPARE(service->stops.size(), std::size_t(3));
	const SceneStopInsertionWindow window = sceneStopInsertionWindow(committed, *service, 1);
	// A route visit is free between the first two stops.
	QVERIFY(window.ok);
	QVERIFY(!window.visits.visits.empty());
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
	QVERIFY2(insertion.inserted, "a stop is inserted between the first two stops");
	const fs::path insertDir = temp.path / "inserted-stop";
	const SceneSaveResult insertSave = saveScene(committed, insertDir.string());
	printErrors(insertSave.diagnostics, "insert save");
	QVERIFY2(insertSave.success(), "a scene with an inserted stop saves");
	SceneModel reloadedInsert;
	QVERIFY2(loadHasNoErrors(insertDir, reloadedInsert), "a scene with an inserted stop reloads");
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
	QVERIFY2(sameStops, "reload keeps the stop order and every planned time");
}

void TestSceneWriter::scenesFolderHoldsScenes() {
	std::error_code error;
	const std::set<fs::path> directories = committedSceneDirectories(error);
	QVERIFY2(!error, "scene codec: the Scenes directory can be read");
	QVERIFY2(!directories.empty(), "scene codec: the Scenes directory holds scene directories");
}

// Every scene directory under the Scenes directory, and the line and minimal fixtures.
void TestSceneWriter::sceneCodec_data() {
	QTest::addColumn<QString>("directory");
	std::error_code error;
	for (const fs::path& directory : committedSceneDirectories(error))
		QTest::newRow(directory.filename().string().c_str()) << QString::fromStdString(directory.u8string());
	const fs::path fixtures = fs::path(EGTRAIN_TEST_SOURCE_DIR) / "fixtures" / "scenes";
	QTest::newRow("fixture line") << QString::fromStdString((fixtures / "line").u8string());
	QTest::newRow("fixture minimal") << QString::fromStdString((fixtures / "minimal").u8string());
}

// Loads the source, saves it, loads the save and saves again. The second save must equal the first one, and the first
// save must keep every entry of the source. A save is compared with a save, because the committed files are not always
// in the writer's form (indent, key order) and the writer always writes scenarios.json. The entry counts and the
// identity keys of scene.json are compared with the source, so that a loader and a writer that both drop a field
// cannot pass.
void TestSceneWriter::sceneCodec() {
	QFETCH(QString, directory);
	const fs::path source = fs::u8path(directory.toStdString());
	const std::string label = QTest::currentDataTag();
	TempDir temp;
	SceneModel model;
	QVERIFY2(loadHasNoErrors(source, model), "the source loads without errors");
	const fs::path first = temp.path / "first";
	const SceneSaveResult firstSave = saveScene(model, first.string());
	printErrors(firstSave.diagnostics, "first save");
	QVERIFY2(firstSave.success(), "the first save succeeds");
	SceneModel reloaded;
	// The first save loads without errors and records the current app version.
	QVERIFY(loadHasNoErrors(first, reloaded));
	QCOMPARE(reloaded.savedWithAppVersion, EGTRAIN_APP_VERSION);
	const fs::path second = temp.path / "second";
	const SceneSaveResult secondSave = saveScene(reloaded, second.string());
	printErrors(secondSave.diagnostics, "second save");
	QVERIFY2(secondSave.success(), "the second save succeeds");
	QVERIFY2(sameDirectoryBytes(first, second, label), "the second save equals the first save byte for byte");
	QVERIFY2(hasCanonicalFileNames(first, label), "the first save holds the canonical files and nothing else");

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
	for (const EntryCount& count : counts) {
		const std::size_t expected = countEntries(sourceFiles[count.file], count.keys);
		const std::size_t actual = countEntries(savedFiles[count.file], count.keys);
		std::string name = count.file;
		for (const char* key : count.keys)
			name += std::string(" ") + key;
		QVERIFY2(expected == actual, (name + ": the source has " + std::to_string(expected) + " entries and the first save has " + std::to_string(actual)).c_str());
	}
	const json& sourceScene = sourceFiles["scene.json"];
	const json& savedScene = savedFiles["scene.json"];
	for (const char* key : {"name", "description", "base_time", "schema_version", "simulation_settings"}) {
		if (sourceScene.contains(key)) {
			const std::string message = std::string("scene.json ") + key + " has the value of the source";
			QVERIFY2(savedScene.contains(key), message.c_str());
			QVERIFY2(savedScene[key] == sourceScene[key], message.c_str());
		}
	}
}

void TestSceneWriter::populatedSceneKeys_data() {
	// The index of the row in populatedSceneTable().
	QTest::addColumn<int>("row");
	const std::vector<WrittenValue>& table = populatedSceneTable();
	for (std::size_t index = 0; index < table.size(); ++index)
		QTest::newRow((std::string(table[index].file) + " " + table[index].pointer).c_str()) << static_cast<int>(index);
}

// Every key of the files written for the populated model has the value of the model.
void TestSceneWriter::populatedSceneKeys() {
	QFETCH(int, row);
	const WrittenValue& value = populatedSceneTable()[static_cast<std::size_t>(row)];
	const json::json_pointer pointer(value.pointer);
	const json& document = m_written.at(value.file);
	// Scene codec populated scene: every key of the written files has the value of the model.
	QVERIFY2(document.contains(pointer), ("should be " + value.expected.dump() + " but is missing").c_str());
	QVERIFY2(document.at(pointer) == value.expected,
		("should be " + value.expected.dump() + " but is " + document.at(pointer).dump()).c_str());
}

// The table of populatedSceneKeys has a row for every key the writer wrote.
void TestSceneWriter::populatedSceneTableIsComplete() {
	std::set<std::string> leaves;
	for (const auto& file : m_written)
		collectLeaves(file.second, "/" + file.first, leaves);
	for (const WrittenValue& value : populatedSceneTable())
		leaves.erase(withoutIndexes("/" + std::string(value.file) + value.pointer));
	std::string missing;
	for (const std::string& leaf : leaves)
		missing += " " + leaf;
	QVERIFY2(leaves.empty(),
		("scene codec populated scene: the table has a row for every key of the written files; no row for" + missing)
			.c_str());
}

// The save of the populated model loads with its views and saves again to the same bytes.
void TestSceneWriter::populatedSceneRoundTrips() {
	QVERIFY2(!m_written.at("views.json").contains(json::json_pointer("/tracks/0/double_switch")),
		"scene codec populated scene: a track view that is not a double switch is written without double_switch");
	const SceneModel model = populatedScene();
	SceneModel reloaded;
	QVERIFY2(loadHasNoErrors(m_populated, reloaded), "scene codec populated scene: the save loads without errors");
	// Scene codec populated scene: the load keeps every track view and station view.
	QCOMPARE(reloaded.trackViews.size(), model.trackViews.size());
	QCOMPARE(reloaded.stationViews.size(), model.stationViews.size());
	// Scene codec populated scene: the load keeps the double switch of a track view.
	QCOMPARE(reloaded.trackViews.size(), std::size_t(2));
	QVERIFY(!reloaded.trackViews[0].doubleSwitch);
	QVERIFY(reloaded.trackViews[1].doubleSwitch);
	TempDir temp;
	const fs::path second = temp.path / "second";
	const SceneSaveResult secondSave = saveScene(reloaded, second.string());
	printErrors(secondSave.diagnostics, "populated second save");
	QVERIFY2(secondSave.success(), "scene codec populated scene: the second save succeeds");
	QVERIFY2(sameDirectoryBytes(m_populated, second, "populated scene"),
		"scene codec populated scene: the second save equals the first save byte for byte");
}

// A save publishes its staging directory as the scene directory. The entries of a private root show every staging or
// backup sibling that a save leaves.
void TestSceneWriter::publishesSave() {
	TempDir temp;
	const fs::path root = temp.path;
	const fs::path destination = root / "scene";
	const SceneModel scene = completeScene();

	const SceneSaveResult first = saveScene(scene, destination.string());
	printErrors(first.diagnostics, "publication first save");
	QVERIFY2(first.success(), "save into an absent destination succeeds");
	{
		std::ofstream marker(destination / "marker.txt", std::ios::binary);
		marker << "marker\n";
	}
	const SceneSaveResult second = saveScene(scene, destination.string());
	printErrors(second.diagnostics, "publication second save");
	// Save over an existing destination succeeds.
	QVERIFY(second.success());
	QVERIFY(fs::exists(destination / "marker.txt"));
	QVERIFY2((entryNames(root) == std::set<std::string>{"scene"}), "saves leave no staging or backup sibling");

#ifndef _WIN32
	// The published directory is the staging directory, so it keeps its owner-only mode.
	const fs::perms groupAndOther = fs::status(destination).permissions() & (fs::perms::group_all | fs::perms::others_all);
	QVERIFY2(groupAndOther == fs::perms::none, "saved scene directory has no group and no other permission bits");

	// A read-only parent refuses the staging directory. This does not apply to root. The permissions are restored
	// before the checks.
	const fs::path other = root / "other";
	fs::permissions(root, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
	const bool readOnly = access(root.c_str(), W_OK) != 0;
	SceneSaveResult refused;
	if (readOnly)
		refused = saveScene(scene, other.string());
	fs::permissions(root, fs::perms::owner_all, fs::perm_options::replace);
	if (readOnly) {
		const bool reported = std::any_of(refused.diagnostics.begin(), refused.diagnostics.end(), [](const SceneDiagnostic& diagnostic) {
			return diagnostic.severity == SceneSeverity::Error && diagnostic.code == "scene.save.write"
				&& diagnostic.message.rfind("Cannot create a private case staging directory", 0) == 0;
		});
		// Save into a read-only parent reports the staging directory.
		QVERIFY(!refused.success());
		QVERIFY(reported);
		QVERIFY2(!fs::exists(other), "failed save creates no destination");
		QVERIFY2((entryNames(root) == std::set<std::string>{"scene"}), "failed save leaves no staging sibling");
	}
#endif
}

// A scene directory and a scenario file with a Unicode name are written and read under that name, and the staging,
// backup and temporary siblings made from the name are removed again.
void TestSceneWriter::savesUnderUnicodeNames() {
	TempDir temp;
	const fs::path root = temp.path / fs::u8path(kUnicodeName);
	const fs::path destination = root / fs::u8path(kUnicodeName);
	const SceneModel scene = completeScene();

	for (int pass = 0; pass < 2; ++pass) {
		const SceneSaveResult saved = saveScene(scene, destination.u8string());
		printErrors(saved.diagnostics, "Unicode save");
		// A scene saves to a directory with a Unicode name, also over an existing one.
		QVERIFY(saved.success());
		QVERIFY(fs::is_regular_file(destination / "scene.json"));
	}
	const SceneLoadResult loaded = loadScene(destination.u8string());
	printErrors(loaded.diagnostics, "Unicode load");
	// A scene directory with a Unicode name loads.
	QVERIFY(!hasErrors(loaded.diagnostics));
	QVERIFY(loaded.inputSnapshot == readSceneDirectorySnapshot(destination.u8string()).bytes);

	const fs::path scenarioFile = root / fs::u8path(kUnicodeName + ".json");
	for (int pass = 0; pass < 2; ++pass)
		QVERIFY2(saveScenarioJson(scene.scenarios[0], scenarioFile.u8string()).success(),
			"a scenario saves to a file with a Unicode name, also over an existing one");
	// A scenario file with a Unicode name loads.
	QCOMPARE(loadScenarioJson(scenarioFile.u8string()).scenario.id, scene.scenarios[0].id);
	// Saves under Unicode names leave no staging, backup or temporary sibling.
	QVERIFY(fs::is_directory(root));
	QVERIFY((entryNames(root) == std::set<std::string>{destination.filename().u8string(), scenarioFile.filename().u8string()}));

	const std::string prefix = kUnicodeName + ".staging-";
	StagedDirectory staged;
	fs::path sibling;
	// A staged directory is named after a Unicode prefix.
	QVERIFY(!createStagedDirectory(root, prefix, false, staged));
	QVERIFY(staged.path.filename().u8string().rfind(prefix, 0) == 0);
	QVERIFY(fs::is_directory(staged.path));
	// A sibling path is named after a Unicode prefix.
	QVERIFY(!uniqueSiblingPath(root, prefix, sibling));
	QVERIFY(sibling.filename().u8string().rfind(prefix, 0) == 0);
}

// A staged directory is created empty and private in its parent, or without the owner-only mode.
void TestSceneWriter::createsStagedDirectory() {
	TempDir temp;
	StagedDirectory staged;
	const std::error_code created = createStagedDirectory(temp.path, "scene.staging-", true, staged);
	// Staged directory is created empty in the parent with the prefix.
	QVERIFY(!created);
	QVERIFY(fs::is_directory(staged.path));
	QVERIFY(fs::is_empty(staged.path));
	QVERIFY(staged.path.parent_path() == temp.path);
	QVERIFY(staged.path.filename().string().rfind("scene.staging-", 0) == 0);
#ifndef _WIN32
	const fs::perms groupAndOther = fs::status(staged.path).permissions() & (fs::perms::group_all | fs::perms::others_all);
	QVERIFY2(groupAndOther == fs::perms::none, "owner-only staged directory has no group and no other permission bits");
#endif
	StagedDirectory plain;
	// Staged directory without the owner-only mode is created.
	QVERIFY(!createStagedDirectory(temp.path, "scene.staging-", false, plain));
	QVERIFY(fs::is_directory(plain.path));
}

// A staged directory is removed when it goes out of scope, unless it was released.
void TestSceneWriter::removesStagedDirectoryOutOfScope() {
	TempDir temp;
	fs::path discardedPath, releasedPath;
	{
		StagedDirectory discarded, released;
		// Staged directories are created.
		QVERIFY(!createStagedDirectory(temp.path, "discarded-", false, discarded));
		QVERIFY(!createStagedDirectory(temp.path, "released-", false, released));
		discardedPath = discarded.path;
		releasedPath = released.path;
		released.release();
		QVERIFY2(released.path.empty(), "release clears the path");
	}
	// Staged directory is removed when it goes out of scope.
	QVERIFY(!discardedPath.empty());
	QVERIFY(!fs::exists(discardedPath));
	// Released directory stays on disk.
	QVERIFY(!releasedPath.empty());
	QVERIFY(fs::is_directory(releasedPath));
}

// Publication into an absent destination moves the staged directory into place.
void TestSceneWriter::publishesIntoAbsentDestination() {
	TempDir temp;
	const fs::path destination = temp.path / "scene";
	StagedDirectory staged;
	QVERIFY2(!createStagedDirectory(temp.path, "scene.staging-", true, staged),
		"staged directory is created for the first publication");
	put(staged.path / "new.txt", "new\n");
	const StagedPublishResult published = publishStagedDirectory(staged, destination, fs::path());
	// Publication into an absent destination succeeds.
	QVERIFY(published.step == StagedPublishStep::Published);
	QVERIFY(!published.error);
	QVERIFY(!published.restoreError);
	QVERIFY(!published.removeBackupError);
	// Published destination holds the staged files and nothing else is left.
	QVERIFY(fs::is_directory(destination));
	QVERIFY((readDirectoryBytes(destination) == Files{{"new.txt", "new\n"}}));
	QVERIFY(staged.path.empty());
	QVERIFY((entryNames(temp.path) == std::set<std::string>{"scene"}));
}

// Publication over an existing destination replaces it and removes the backup.
void TestSceneWriter::publishesOverExistingDestination() {
	TempDir temp;
	const fs::path destination = temp.path / "scene";
	fs::create_directory(destination);
	put(destination / "old.txt", "old\n");
	StagedDirectory staged;
	fs::path backup;
	// Staged directory and backup name are reserved.
	QVERIFY(!createStagedDirectory(temp.path, "scene.staging-", true, staged));
	QVERIFY(!uniqueSiblingPath(temp.path, "scene.backup-", backup));
	QVERIFY(!fs::exists(backup));
	QVERIFY(backup.filename().string().rfind("scene.backup-", 0) == 0);
	put(staged.path / "new.txt", "new\n");
	const StagedPublishResult published = publishStagedDirectory(staged, destination, backup);
	// Publication over an existing destination succeeds.
	QVERIFY(published.step == StagedPublishStep::Published);
	QVERIFY(!published.error);
	QVERIFY(!published.restoreError);
	QVERIFY(!published.removeBackupError);
	// Replaced destination holds only the new files and no backup is left.
	QVERIFY(fs::is_directory(destination));
	QVERIFY((readDirectoryBytes(destination) == Files{{"new.txt", "new\n"}}));
	QVERIFY((entryNames(temp.path) == std::set<std::string>{"scene"}));
}

// The staging directory is gone, so the rename into place fails and the old destination is moved back.
void TestSceneWriter::restoresDestinationAfterFailedRename() {
	TempDir temp;
	const fs::path destination = temp.path / "scene";
	fs::create_directory(destination);
	put(destination / "old.txt", "old\n");
	StagedDirectory staged;
	fs::path backup;
	// Staged directory and backup name are reserved for the failing rename.
	QVERIFY(!createStagedDirectory(temp.path, "scene.staging-", true, staged));
	QVERIFY(!uniqueSiblingPath(temp.path, "scene.backup-", backup));
	fs::remove_all(staged.path);
	const StagedPublishResult failed = publishStagedDirectory(staged, destination, backup);
	// Failed rename into place is reported as the Rename step.
	QVERIFY(failed.step == StagedPublishStep::Rename);
	QVERIFY(failed.error);
	QVERIFY(!failed.restoreError);
	QVERIFY(!failed.removeBackupError);
	// Failed rename into place restores the old destination and leaves no backup.
	QVERIFY(fs::is_directory(destination));
	QVERIFY((readDirectoryBytes(destination) == Files{{"old.txt", "old\n"}}));
	QVERIFY((entryNames(temp.path) == std::set<std::string>{"scene"}));
}

// A destination that is missing fails the move to the backup, and the staging directory is removed afterwards.
void TestSceneWriter::reportsMissingDestination() {
	TempDir temp;
	const fs::path destination = temp.path / "scene";
	fs::path stagingPath;
	{
		StagedDirectory staged;
		fs::path backup;
		// Staged directory and backup name are reserved for the missing destination.
		QVERIFY(!createStagedDirectory(temp.path, "scene.staging-", true, staged));
		QVERIFY(!uniqueSiblingPath(temp.path, "scene.backup-", backup));
		stagingPath = staged.path;
		const StagedPublishResult published = publishStagedDirectory(staged, destination, backup);
		// Missing destination is reported as the MoveToBackup step.
		QVERIFY(published.step == StagedPublishStep::MoveToBackup);
		QVERIFY(published.error);
		QVERIFY(!fs::exists(destination));
		QVERIFY2(fs::is_directory(stagingPath), "staging directory is still owned after a failed move to the backup");
	}
	QVERIFY2(entryNames(temp.path).empty(), "staging directory is removed after a failed move to the backup");
}

// Malformed incidents and entrance delays of a standalone scenario file.
void TestSceneWriter::reportsScenarioFileDiagnostics() {
	TempDir temp;
	const fs::path file = temp.path / "malformed-scenario.json";
	const auto load = [&](const json& incidents, const json& entranceDelays) {
		json scenario = {{"id", "s"}, {"name", "n"}};
		if (!incidents.is_null())
			scenario["incidents"] = incidents;
		if (!entranceDelays.is_null())
			scenario["entrance_delays"] = entranceDelays;
		std::ofstream(file, std::ios::trunc) << scenario.dump();
		return loadScenarioJson(file.string());
	};
	const std::string type = "scene.scenario.field.type";
	const std::string missing = "scene.scenario.field.missing";
	const json wrongTypes = {{"id", "i"}, {"type", 5}, {"target", ""}, {"start_seconds", "a"}, {"end_seconds", "b"},
		{"occurrence", 1.5}, {"reduced_speed_kmh", true}, {"terminate_at_destination", "c"}};
	// The keys of wrongTypes that are reported, in the order of the schema.
	const std::vector<const char*> wrongTypeKeys = {"type", "target", "start_seconds", "end_seconds", "occurrence",
		"reduced_speed_kmh", "terminate_at_destination"};
	const json wrongDelay = {{"service", 3}, {"occurrence", "x"}, {"delay_seconds", "d"}};

	std::vector<std::string> expected;
	append(expected, fieldErrors(type, "incidents[0]", "Invalid", wrongTypeKeys));
	append(expected, fieldErrors(missing, "incidents[1]", "Missing", {"id", "type", "target", "start_seconds"}));
	expected.push_back(type + " entrance_delays[0].service: Invalid service");
	expected.push_back(type + " entrance_delays[0].occurrence: Invalid occurrence");
	expected.push_back(missing + " entrance_delays[0].station: Missing station");
	expected.push_back(type + " entrance_delays[0].delay_seconds: Invalid delay_seconds");
	append(expected, fieldErrors(missing, "entrance_delays[1]", "Missing", {"service", "station", "delay_seconds"}));
	ScenarioLoadResult loaded = load(json::array({wrongTypes, json::object()}), json::array({wrongDelay, json::object()}));
	QVERIFY2(scenarioErrors(loaded) == expected,
		"scenario file reports malformed incident and entrance delay fields in schema order");
	// Scenario file keeps the defaults of malformed fields and still reads the items.
	QCOMPARE(loaded.scenario.incidents.size(), std::size_t(2));
	QCOMPARE(loaded.scenario.incidents[0].id, "i");
	QVERIFY(!loaded.scenario.incidents[0].hasEndSeconds);
	QVERIFY(!loaded.scenario.incidents[0].hasOccurrence);
	QCOMPARE(loaded.scenario.incidents[0].occurrence, 1);
	QVERIFY(!loaded.scenario.incidents[0].hasReducedSpeed);
	QVERIFY(!loaded.scenario.incidents[0].terminateAtDestination);
	QCOMPARE(loaded.scenario.entranceDelays.size(), std::size_t(2));

	// An item that is not an object is reported once and is not read.
	loaded = load(json::array({7}), json::array({7}));
	// Scenario file reports an incident and an entrance delay that are not objects once each.
	QVERIFY((scenarioErrors(loaded)
		== std::vector<std::string>{"scene.scenario.item.invalid incidents[0]: Incident must be an object",
			"scene.scenario.item.invalid entrance_delays[0]: Entrance delay must be an object"}));
	QVERIFY(loaded.scenario.incidents.empty());
	QVERIFY(loaded.scenario.entranceDelays.empty());

	loaded = load("x", "x");
	QVERIFY2((scenarioErrors(loaded)
				 == std::vector<std::string>{"scene.scenario.section.type incidents: incidents must be an array",
					 "scene.scenario.section.type entrance_delays: entrance_delays must be an array"}),
		"scenario file reports sections that are not arrays");
	loaded = load(nullptr, nullptr);
	QVERIFY2((scenarioErrors(loaded)
				 == std::vector<std::string>{"scene.scenario.section.type incidents: incidents must be an array"}),
		"scenario file requires the incidents section and not the entrance delay section");
}

// A case, a station and a scenario may have an empty name. A save loads what it wrote, so the loader takes an empty
// name, and a missing one is still an error.
void TestSceneWriter::savesEmptyNames() {
	TempDir temp;
	SceneModel scene = completeScene();
	scene.name.clear();
	scene.stations[0].name.clear();
	scene.scenarios[0].name.clear();
	const fs::path scenePath = temp.path / "empty-names";
	const SceneSaveResult saved = saveScene(scene, scenePath.string());
	printErrors(saved.diagnostics, "empty names save");
	QVERIFY2(saved.success(), "a scene with an empty case, station and scenario name saves");
	SceneModel reloaded;
	// The empty names load as empty.
	QVERIFY(loadHasNoErrors(scenePath, reloaded));
	QVERIFY(reloaded.name.empty());
	QCOMPARE(reloaded.stations.size(), std::size_t(2));
	QVERIFY(reloaded.stations[0].name.empty());
	QCOMPARE(reloaded.stations[1].name, scene.stations[1].name);
	QCOMPARE(reloaded.scenarios.size(), std::size_t(2));
	QVERIFY(reloaded.scenarios[0].name.empty());

	const fs::path scenarioFile = temp.path / "empty-name-scenario.json";
	QVERIFY2(saveScenarioJson(scene.scenarios[0], scenarioFile.string()).success(), "a scenario with an empty name is exported");
	const ScenarioLoadResult imported = loadScenarioJson(scenarioFile.string());
	// A scenario file with an empty name is imported.
	QVERIFY(!hasErrors(imported.diagnostics));
	QVERIFY(imported.scenario.name.empty());
	QCOMPARE(imported.scenario.id, scene.scenarios[0].id);

	json stations = readJsonFile(scenePath / "stations.json");
	stations["stations"][0].erase("name");
	std::ofstream(scenePath / "stations.json", std::ios::trunc) << stations.dump(4) << "\n";
	const SceneLoadResult missing = loadScene(scenePath.string());
	bool missingName = false;
	for (const SceneDiagnostic& diagnostic : missing.diagnostics)
		missingName = missingName
			|| (diagnostic.severity == SceneSeverity::Error && diagnostic.code == "scene.field.missing"
				&& diagnostic.file == "stations.json" && diagnostic.path == "stations[0].name");
	QVERIFY2(missingName, "a station without a name key is still an error");
}

QTEST_APPLESS_MAIN(TestSceneWriter)
#include "test_scenewriter.moc"
