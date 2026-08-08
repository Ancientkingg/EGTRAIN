#include "scene/SceneImporter.h"
#include "scene/SceneValidator.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

static bool expect(bool condition, const std::string& message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static bool hasDiag(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code,
		SceneSeverity severity) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.code == code && diagnostic.severity == severity)
			return true;
	}
	return false;
}

struct TempDir {
	std::string dir;
	TempDir() {
		static int counter = 0;
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		const fs::path path = fs::temp_directory_path()
				/ ("scene_import_test_" + std::to_string(stamp) + "_" + std::to_string(counter++));
		fs::create_directories(path);
		dir = path.string();
	}
	~TempDir() {
		std::error_code error;
		fs::remove_all(dir, error);
	}
};

static void writeText(const fs::path& path, const std::string& content) {
	fs::create_directories(path.parent_path());
	std::ofstream output(path);
	output << content;
}

static void writeUtf16Le(const fs::path& path, const std::string& content) {
	fs::create_directories(path.parent_path());
	std::ofstream output(path, std::ios::binary);
	output.put(static_cast<char>(0xff));
	output.put(static_cast<char>(0xfe));
	for (const unsigned char value : content) {
		output.put(static_cast<char>(value));
		output.put('\0');
	}
}

static bool readJson(const fs::path& path, json& value) {
	std::ifstream input(path);
	if (!input)
		return false;
	try {
		input >> value;
		return true;
	} catch (...) {
		return false;
	}
}

int main() {
	bool ok = true;
	// One small fixture exercises the new fields together, including explicit
	// Trains provenance and a coordinate that must remain unresolved.
	{
		TempDir legacyDir, output;
		const fs::path legacy(legacyDir.dir);
		writeText(legacy / "TrackLines/B0/NodiCumPari.txt", "1 0 0\n2 1 0\n");
		writeText(legacy / "TrackLines/B1/NodiCumPari.txt", "1 0 1\n2 1 1\n");
		writeText(legacy / "TrackLines/B0/ArchiCumPari.txt", "7 1 2 100 2 30\n");
		writeText(legacy / "TrackLines/B1/ArchiCumPari.txt", "7 1 2 200 3 40\n");
		writeText(legacy / "TrackLines/B0/BlockCumPari.txt", "99 0.5\n");
		writeUtf16Le(legacy / "TrackLines/B1/BlockCumPari.txt", "88 0.6\r\n");
		writeText(legacy / "TrackLines/Connections.txt", "0 0 1 0 7\n0 9 1 9 8\n");
		writeText(legacy / "TrackLines/Stations.txt", "0\tGuingamp\n1\tPaimpol\n");
		writeText(legacy / "Routes/Route0.txt", "@0-B0@\n");
		writeText(legacy / "Routes/Route2.txt", "@0-B1@\n");
		writeText(legacy / "RoutesToWrite/RoutesToJoin.txt", "0 2 Reverse\n");
		writeText(legacy / "GUI/caseStudyRouteCorridors.txt", "0\tfixture-corridor\n");
		writeText(legacy / "GUI/singleTrackLimits.txt", "@0-B0@\t@0-B1@\t@0-B0@\t@0-B1@\n");
		writeText(legacy / "GUI/stationBoundarySections.txt", "@0-B0@\t@0-B1@\t1\n");
		writeText(legacy / "trainNames.txt", "fixture.txt\nfixture-duplicate.txt\n");
		writeText(legacy / "Trains/fixture.txt",
				"FixtureService 0 99999999 0 /vehicle.dat /effort-curve.dat /TimeTable/fixture.txt\n");
		writeText(legacy / "Trains/fixture-duplicate.txt",
				"FixtureService 0 99999999 2 /vehicle.dat /effort-curve.dat /TimeTable/fixture.txt\n");
		writeText(legacy / "vehicle.dat", "1 2 3 4 5 6 7 8 9\n");
		writeText(legacy / "effort-curve.dat", "0 1 2 3 4\n");
		writeText(legacy / "TimeTable/fixture.txt", "Guingamp 2 -1 10\nPaimpol 3 20 -1\n");
		writeText(legacy / "Incidents.txt",
				"signal_failure\t0-B0\t10\t20\ntrain_breakdown\tFixtureService\t30\t40\n");
		writeText(legacy / "TimeTable/Scenarios_Entrance_Delays/Rollout_1.txt", "15\n20\n");

		const auto result = importLegacyScene(legacyDir.dir, output.dir, "Paimpol");
		ok &= expect(result.success(), "Complete synthetic fixture imports");
		if (result.wroteScene) {
			json scene, infrastructure, rolling, services, signalling, scenarios;
			ok &= expect(readJson(fs::path(output.dir) / "scene.json", scene), "Synthetic scene readable");
			ok &= expect(readJson(fs::path(output.dir) / "infrastructure.json", infrastructure), "Synthetic infrastructure readable");
			ok &= expect(readJson(fs::path(output.dir) / "rolling_stock.json", rolling), "Synthetic rolling stock readable");
			ok &= expect(readJson(fs::path(output.dir) / "services.json", services), "Synthetic services readable");
			ok &= expect(readJson(fs::path(output.dir) / "signalling.json", signalling), "Synthetic signalling readable");
			ok &= expect(readJson(fs::path(output.dir) / "scenarios.json", scenarios), "Synthetic scenarios readable");
			ok &= expect(scene["base_time"] == "07:10:40"
					&& scene["simulation_settings"]["duration_seconds"] == 9000.0, "Known settings imported");
			bool rootReport = false, coordinateReport = false;
			for (const auto& row : scene["import_report"]) {
				if (row["category"] == "legacy_root")
					rootReport = row["source_count"] == 1 && row["converted_count"] == 1;
				if (row["category"] == "infrastructure.connections")
					coordinateReport = row["source_count"] == 2 && row["converted_count"] == 1
							&& row["skipped_count"] == 1 && row["unresolved_references"] == 1;
			}
			ok &= expect(rootReport, "Synthetic root provenance report");
			ok &= expect(coordinateReport, "Synthetic unresolved coordinate report");
			ok &= expect(infrastructure["nodes"][0]["id"] == "B0.node.1", "Stable node id");
			ok &= expect(infrastructure["arcs"][0]["from"] == "B0.node.1"
					&& infrastructure["arcs"][0]["to"] == "B0.node.2", "Arc node references");
			ok &= expect(infrastructure["blocks"][0]["id"] == "0-B0"
					&& infrastructure["blocks"][0]["length_km"] == 0.5, "Row-position block id");
			ok &= expect(infrastructure["blocks"].size() == 2
					&& infrastructure["blocks"][1]["id"] == "0-B1"
					&& infrastructure["blocks"][1]["length_km"] == 0.6,
					"UTF-16LE block data is imported");
			ok &= expect(infrastructure["connections"].size() == 1
					&& infrastructure["connections"][0]["from"] == "B0.node.1", "Resolved connection ref");
			ok &= expect(rolling["train_units"].size() == 1
					&& rolling["train_units"][0]["source"]["data_file"] == "/vehicle.dat"
					&& rolling["train_units"][0]["source"]["traction_file"] == "/effort-curve.dat",
					"Rolling relation follows explicit Trains paths");
			ok &= expect(services["services"][0]["operating_code"] == "FixtureService",
					"Active legacy train identity is retained separately from the service id");
			const auto& stops = services["services"][0]["stops"];
			ok &= expect(stops[0].contains("planned_departure_seconds")
					&& !stops[0].contains("planned_arrival_seconds")
					&& !stops[0].contains("departure_seconds"), "Missing arrival stays absent");
			ok &= expect(stops[1].contains("planned_arrival_seconds")
					&& !stops[1].contains("planned_departure_seconds")
					&& !stops[1].contains("arrival_seconds"), "Missing departure stays absent");
			ok &= expect(signalling["routes"][0]["corridor"] == "fixture-corridor"
					&& signalling["single_track_restrictions"][0]["start_block"] == "0-B0"
					&& signalling["station_boundaries"][0]["entrance_block"] == "0-B0", "Signalling roles imported");
			ok &= expect(signalling["routes"].size() == 3
					&& signalling["routes"][2]["id"] == "route3"
					&& signalling["routes"][2]["reversed"] == true,
					"Sparse route ids leave a stable append id for joined routes");
			ok &= expect(scenarios["default_scenario_id"] == "baseline"
					&& scenarios["scenarios"].size() == 2
					&& scenarios["scenarios"][0]["incidents"].size() == 3
					&& scenarios["scenarios"][1]["incidents"].size() == 3
					&& scenarios["scenarios"][1]["incidents"][0]["id"] == "rollout_1.incident.1"
					&& scenarios["scenarios"][1]["entrance_delays"].size() == 4,
					"Rollout retains global incidents and entrance delays");
			ok &= expect(scenarios["scenarios"][0]["incidents"][1]["target"] == "FixtureService"
					&& scenarios["scenarios"][0]["incidents"][2]["target"] == "FixtureService_2"
					&& hasDiag(result.diagnostics, "scene.import.expanded", SceneSeverity::Warning),
					"Duplicate operating-code incident expands to every matching canonical service");
		}
	}

	// Atomic path safety: reject overlap, and preserve an existing destination
	// when malformed legacy input cannot be imported.
	{
		TempDir root;
		const fs::path legacy = fs::path(root.dir) / "legacy";
		const fs::path nestedScene = legacy / "nested-scene";
		writeText(legacy / "TrackLines/Stations.txt", "0\tSource\n");
		const auto result = importLegacyScene(legacy.string(), nestedScene.string(), "Overlap");
		ok &= expect(!result.wroteScene && hasDiag(result.diagnostics, "scene.import.path", SceneSeverity::Error),
				"Overlapping source and destination are rejected");
		ok &= expect(!fs::exists(nestedScene), "Overlap rejection does not create destination");
	}
	{
		TempDir legacyDir, output;
		writeText(fs::path(legacyDir.dir) / "TrackLines/Stations.txt", "0\tSource\n");
		writeText(fs::path(legacyDir.dir) / "Trains/broken.txt", "malformed\n");
		writeText(fs::path(output.dir) / "scene.json", "existing scene bytes\n");
		const auto result = importLegacyScene(legacyDir.dir, output.dir, "Malformed");
		ok &= expect(!result.success() && !result.wroteScene, "Malformed source is not published");
		std::ifstream scene(fs::path(output.dir) / "scene.json");
		std::string bytes((std::istreambuf_iterator<char>(scene)), std::istreambuf_iterator<char>());
		ok &= expect(bytes == "existing scene bytes\n", "Malformed import preserves destination");
	}

	if (!ok)
		return 1;
	std::cout << "all SceneImporter tests passed\n";
	return 0;
}
