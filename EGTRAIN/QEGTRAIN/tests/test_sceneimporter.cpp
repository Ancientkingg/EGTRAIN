#include "scene/SceneImporter.h"
#include "scene/SceneModel.h"
#include "scene/SceneValidator.h"

#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

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

// Japanese and Thai characters, as UTF-8. No Windows code page holds both.
static const std::string kUnicodeName = "scene-\xE6\x97\xA5\xE6\x9C\xAC-\xE0\xB8\x81";

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

static bool hasDiagAt(const std::vector<SceneDiagnostic>& diagnostics, const std::string& file,
	const std::string& path) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.file == file && diagnostic.path == path
			&& (diagnostic.severity == SceneSeverity::Error || diagnostic.severity == SceneSeverity::Warning))
			return true;
	}
	return false;
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

// The errors of one file as "code path: message", in the order in which the loader reported them.
static std::vector<std::string> errorsIn(const std::vector<SceneDiagnostic>& diagnostics, const std::string& file) {
	std::vector<std::string> lines;
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.file == file && diagnostic.severity == SceneSeverity::Error)
			lines.push_back(diagnostic.code + " " + diagnostic.path + ": " + diagnostic.message);
	}
	return lines;
}

// One error per key, worded "<what> <key>" and located at <prefix>.<key>.
static std::vector<std::string> fieldErrors(const std::string& prefix, const std::string& what,
	const std::vector<const char*>& keys) {
	std::vector<std::string> lines;
	for (const char* key : keys)
		lines.push_back("scene.field.missing " + prefix + "." + key + ": " + what + " " + key);
	return lines;
}

static void append(std::vector<std::string>& lines, const std::vector<std::string>& more) {
	lines.insert(lines.end(), more.begin(), more.end());
}

// Malformed incidents and entrance delays, read from scenarios.json and from the legacy incidents.json. The directory
// holds only that file, so the loader reports the other scene files as missing under their own names.
static bool checkScenarioItemDiagnostics() {
	bool ok = true;
	const auto load = [](const char* file, const json& content) {
		TempDir directory;
		writeText(fs::path(directory.dir) / file, content.dump());
		return loadScene(directory.dir);
	};
	const auto scenarioFile = [](const json& incidents, const json& entranceDelays) {
		json scenario = {{"id", "s"}, {"name", "n"}, {"incidents", incidents}, {"entrance_delays", entranceDelays}};
		return json({{"default_scenario_id", "s"}, {"scenarios", json::array({scenario})}});
	};
	const json wrongTypes = {{"id", "i"}, {"type", 5}, {"target", ""}, {"start_seconds", "a"}, {"end_seconds", "b"},
		{"occurrence", 1.5}, {"reduced_speed_kmh", true}, {"terminate_at_destination", "c"}};
	// The keys of wrongTypes that are reported, in the order of the schema.
	const std::vector<const char*> wrongTypeKeys = {"type", "target", "start_seconds", "end_seconds", "occurrence",
		"reduced_speed_kmh", "terminate_at_destination"};
	const json wrongDelay = {{"service", 3}, {"occurrence", "x"}, {"delay_seconds", "d"}};
	const std::string notObject = "scene.item.invalid ";

	std::vector<std::string> expected;
	append(expected, fieldErrors("scenarios[0].incidents[0]", "Invalid", wrongTypeKeys));
	append(expected, fieldErrors("scenarios[0].incidents[1]", "Missing", {"id", "type", "target", "start_seconds"}));
	expected.push_back("scene.field.missing scenarios[0].entrance_delays[0].service: Invalid service");
	expected.push_back("scene.field.missing scenarios[0].entrance_delays[0].occurrence: Invalid occurrence");
	expected.push_back("scene.field.missing scenarios[0].entrance_delays[0].station: Missing station");
	expected.push_back("scene.field.missing scenarios[0].entrance_delays[0].delay_seconds: Invalid delay_seconds");
	append(expected, fieldErrors("scenarios[0].entrance_delays[1]", "Missing", {"service", "station", "delay_seconds"}));
	SceneLoadResult loaded = load("scenarios.json",
		scenarioFile(json::array({wrongTypes, json::object()}), json::array({wrongDelay, json::object()})));
	ok &= expect(errorsIn(loaded.diagnostics, "scenarios.json") == expected,
		"Malformed incident and entrance delay fields are reported in schema order");
	ok &= expect(loaded.scene.scenarios.size() == 1 && loaded.scene.scenarios[0].incidents.size() == 2
			&& loaded.scene.scenarios[0].incidents[0].id == "i" && !loaded.scene.scenarios[0].incidents[0].hasEndSeconds
			&& !loaded.scene.scenarios[0].incidents[0].hasOccurrence && loaded.scene.scenarios[0].incidents[0].occurrence == 1
			&& !loaded.scene.scenarios[0].incidents[0].hasReducedSpeed
			&& !loaded.scene.scenarios[0].incidents[0].terminateAtDestination
			&& loaded.scene.scenarios[0].entranceDelays.size() == 2,
		"Malformed incident fields keep their defaults and the items are still read");

	// An item that is not an object is reported once by every field that is read from it.
	expected.assign(7, notObject + "scenarios[0].incidents[0]: Array item must be an object");
	expected.insert(expected.end(), 4, notObject + "scenarios[0].entrance_delays[0]: Array item must be an object");
	loaded = load("scenarios.json", scenarioFile(json::array({7}), json::array({7})));
	ok &= expect(errorsIn(loaded.diagnostics, "scenarios.json") == expected,
		"An incident and an entrance delay that are not objects are reported once per field");

	loaded = load("scenarios.json", scenarioFile("x", "x"));
	ok &= expect(errorsIn(loaded.diagnostics, "scenarios.json")
			== std::vector<std::string>{"scene.section.missing scenarios[0].incidents: Mistyped incidents section",
				"scene.section.missing scenarios[0].entrance_delays: Mistyped entrance_delays section"},
		"Incident and entrance delay sections that are not arrays are reported");
	loaded = load("scenarios.json",
		json({{"default_scenario_id", "s"}, {"scenarios", json::array({json({{"id", "s"}, {"name", "n"}})})}}));
	ok &= expect(errorsIn(loaded.diagnostics, "scenarios.json")
			== std::vector<std::string>{"scene.section.missing scenarios[0].incidents: Missing incidents section"},
		"A missing incidents section is reported and a missing entrance delay section is not");

	expected.clear();
	append(expected, fieldErrors("incidents[0]", "Invalid", wrongTypeKeys));
	expected.insert(expected.end(), 7, notObject + "incidents[1]: Array item must be an object");
	loaded = load("incidents.json", json({{"incidents", json::array({wrongTypes, 7})}}));
	ok &= expect(errorsIn(loaded.diagnostics, "incidents.json") == expected,
		"Malformed incidents of the legacy incidents file are reported with root paths");
	loaded = load("incidents.json", json({{"incidents", "x"}}));
	ok &= expect(errorsIn(loaded.diagnostics, "incidents.json")
			== std::vector<std::string>{"scene.section.missing incidents: Mistyped incidents section"},
		"A legacy incidents section that is not an array is reported");
	loaded = load("incidents.json", json::object());
	ok &= expect(errorsIn(loaded.diagnostics, "incidents.json")
			== std::vector<std::string>{"scene.section.missing incidents: Missing incidents section"},
		"A legacy incidents file without a section is reported");
	return ok;
}

int main() {
	bool ok = true;
	{
		TempDir root;
		const fs::path physical = fs::path(root.dir) / "physical.txt";
		const fs::path traction = fs::path(root.dir) / "traction.txt";
		writeText(physical, "100 50 0 80 1 12 -0.2 0 30\n");
		writeText(traction, "0 10 100 0 0\n\n10 20 90 0 0\n");
		const auto parsedPhysical = parseTrainPhysicalSourceFile(physical.string());
		const auto parsedTraction = parseTrainTractionSourceFile(traction.string());
		ok &= expect(parsedPhysical.success() && parsedPhysical.physical.number_of_wagons == 0.0,
			"Strict physical source accepts the native zero-wagon domain");
		ok &= expect(parsedTraction.success() && parsedTraction.tractionCurve.size() == 2,
			"Strict traction source accepts complete rows and blank lines");
		writeText(physical, "101 52 3 83 1.5 12.5 0.25 0.75 31\n");
		const auto orderedPhysical = parseTrainPhysicalSourceFile(physical.string());
		ok &= expect(orderedPhysical.success()
				&& orderedPhysical.physical.mass_of_traction_unit_kg == 101.0
				&& orderedPhysical.physical.mass_of_a_wagon_kg == 52.0
				&& orderedPhysical.physical.number_of_wagons == 3.0
				&& orderedPhysical.physical.max_speed_ms == 83.0
				&& orderedPhysical.physical.max_deceleration_ms2 == 1.5
				&& orderedPhysical.physical.frontal_area_m2 == 12.5
				&& orderedPhysical.physical.resistance_coefficient == 0.25
				&& orderedPhysical.physical.jerk_ms3 == 0.75
				&& orderedPhysical.physical.length_m == 31.0,
			"Strict physical source assigns the nine values in file order");
		writeText(physical, "100 50 0 80 1 12 -0.2 0 30 extra\n");
		ok &= expect(!parseTrainPhysicalSourceFile(physical.string()).success(),
			"Strict physical source rejects extra tokens");
		writeText(traction, "0 10 100 0 0\npartial\n");
		ok &= expect(!parseTrainTractionSourceFile(traction.string()).success(),
			"Strict traction source rejects a malformed row instead of skipping it");
		writeText(traction, "0 10 100 0 0\n5 20 90 0 0\n");
		ok &= expect(!parseTrainTractionSourceFile(traction.string()).success(),
			"Strict traction source rejects overlapping intervals");
	}
	// One small fixture exercises the new fields together, including explicit
	// Trains provenance and a coordinate that must remain unresolved.
	{
		TempDir legacyDir, output, partialOutput, noPassengerOutput;
		const fs::path legacy(legacyDir.dir);
		writeText(legacy / "TrackLines/B0/NodiCumPari.txt", "1 0 0\n2 1 0\n");
		writeText(legacy / "TrackLines/B1/NodiCumPari.txt", "1 0 1\n2 1 1\n");
		writeText(legacy / "TrackLines/B0/ArchiCumPari.txt", "7 1 2 100 2 30\n");
		writeText(legacy / "TrackLines/B1/ArchiCumPari.txt", "7 1 2 200 3 40\n");
		writeText(legacy / "TrackLines/B0/BlockCumPari.txt", "99 0.5\n");
		writeUtf16Le(legacy / "TrackLines/B1/BlockCumPari.txt", "88 -0.6\r\n");
		writeText(legacy / "TrackLines/B4/NodiCumPari.txt", "1 0 4\n2 1 4\n3 2 4\n4 3 4\n5 4 4\n6 5 4\n7 6 4\n8 7 4\n9 8 4\n10 9 4\n");
		writeText(legacy / "TrackLines/B4/ArchiCumPari.txt", "108 1 10 10000 0 20\n");
		writeText(legacy / "TrackLines/B4/BlockCumPari.txt", "1 1\n");
		writeText(legacy / "TrackLines/Connections.txt", "0 0 1 0 7\n0 9 1 9 8\n");
		writeText(legacy / "TrackLines/Stations.txt", "0\tGuingamp\n1\tPaimpol\n");
		writeText(legacy / "TrackLines/AreasCaseStudy.txt",
			"Guingamp\t0\t0.55\t0\nLane two\t1\t2\t2\t2\nbroken row\nD\t2\t3\t0\nE\t3\t4\t0\nF\t4\t5\t0\nG\t5\t6\t0\n");
		writeText(legacy / "Routes/Route0.txt", "@0-B0@\n");
		writeText(legacy / "Routes/Route2.txt", "@0-B1@\n");
		writeText(legacy / "RoutesToWrite/RoutesToJoin.txt", "0 2 Reverse\n");
		writeText(legacy / "GUI/caseStudyRouteCorridors.txt", "0\tfixture-corridor\n");
		writeText(legacy / "GUI/caseStudyTrackData.txt", "0\t\t2\n");
		writeText(legacy / "GUI/unusedTracks.txt", "1\n");
		writeText(legacy / "GUI/StationsCoord.txt",
			"Guingamp full name\t1\t0.1\t2\t0.0\tfixture-corridor\n"
			"Paimpol full name\t1\t0.2\t2\t1.0\tfixture-corridor\n");
		writeText(legacy / "GUI/singleTrackLimits.txt", "@0-B0@\t@0-B1@\t@0-B0@\t@0-B1@\n");
		writeText(legacy / "GUI/stationBoundarySections.txt", "@0-B0@\t@0-B1@\t1\n");
		writeText(legacy / "trainNames.txt", "fixture.txt\nfixture-duplicate.txt\n");
		writeText(legacy / "Trains/fixture.txt",
			"FixtureService 0 99999999 0 /vehicle.dat /effort-curve.dat /TimeTable/fixture.txt\n");
		writeText(legacy / "Trains/fixture-duplicate.txt",
			"FixtureService 0 99999999 2 /vehicle.dat /effort-curve.dat /TimeTable/fixture.txt\n");
		writeText(legacy / "vehicle.dat", "1 2 3 4 5 6 7 8 9\n");
		writeText(legacy / "effort-curve.dat",
			"0 1 2 3 4\n1 2 3 4 5\n2 3 4 5 6\n"
			"0 6 7 8 9\n1 7 8 9 10\n2 8 9 10 11\n");
		writeText(legacy / "TimeTable/fixture.txt", "Guingamp 2 -1 10\nPaimpol 3 20 10\n");
		writeText(legacy / "Passengers/DAS_FrenchCaseStudy.csv",
			"h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11,h12,h13,h14\n"
			"x,p1,x,x,j1,work,Paimpol,x,x,x,8.25,x,Guingamp,x,8.00\n");
		writeText(legacy / "Passengers/RouteChoiceFC_EQ1.csv",
			"person_id,destination,nb_transfers,transfer_n1,r_service_lines_id1\n"
			"p1,Paimpol,0,,FixtureService-1\n");
		writeText(legacy / "Incidents.txt",
			"signal_failure\t0-B0\t10\t20\ntrain_breakdown\tFixtureService\t30\t40\n");
		writeText(legacy / "TimeTable/Scenarios_Entrance_Delays/Rollout_1.txt", "15\n20\n");

		const auto result = importLegacyScene(legacyDir.dir, output.dir, "Paimpol");
		ok &= expect(result.success(), "Complete synthetic fixture imports");
		if (result.wroteScene) {
			json scene, infrastructure, rolling, services, signalling, scenarios, views;
			ok &= expect(readJson(fs::path(output.dir) / "scene.json", scene), "Synthetic scene readable");
			ok &= expect(readJson(fs::path(output.dir) / "infrastructure.json", infrastructure), "Synthetic infrastructure readable");
			ok &= expect(readJson(fs::path(output.dir) / "rolling_stock.json", rolling), "Synthetic rolling stock readable");
			ok &= expect(readJson(fs::path(output.dir) / "services.json", services), "Synthetic services readable");
			ok &= expect(readJson(fs::path(output.dir) / "signalling.json", signalling), "Synthetic signalling readable");
			ok &= expect(readJson(fs::path(output.dir) / "scenarios.json", scenarios), "Synthetic scenarios readable");
			ok &= expect(readJson(fs::path(output.dir) / "views.json", views), "Synthetic views readable");
			ok &= expect(scene["base_time"] == "07:10:40"
					&& scene["simulation_settings"]["duration_seconds"] == 9000.0,
				"Known settings imported");
			ok &= expect(scene["schema_version"] == kCurrentSceneSchemaVersion
					&& scene["saved_with_app_version"] == EGTRAIN_APP_VERSION,
				"Legacy import records the creating application version");
			bool rootReport = false, coordinateReport = false, stationViewReport = false, areasReport = false;
			int areasRows = 0;
			int trackViewSources = 0;
			int trackViewConversions = 0;
			for (const auto& row : scene["import_report"]) {
				if (row["category"] == "legacy_root")
					rootReport = row["source_count"] == 1 && row["converted_count"] == 1;
				if (row["category"] == "infrastructure.connections")
					coordinateReport = row["source_count"] == 2 && row["converted_count"] == 1
						&& row["skipped_count"] == 1 && row["unresolved_references"] == 1;
				if (row["category"] == "signalling.areas") {
					++areasRows;
					areasReport = row["source_count"] == 7 && row["skipped_count"] == 7 && row["converted_count"] == 0
						&& row["unresolved_references"] == 0;
				}
				if (row["category"] == "views.tracks") {
					trackViewSources += row["source_count"].get<int>();
					trackViewConversions += row["converted_count"].get<int>();
				}
				if (row["category"] == "views.stations")
					stationViewReport = row["source_count"] == 2 && row["converted_count"] == 2;
			}
			ok &= expect(rootReport, "Synthetic root provenance report");
			ok &= expect(coordinateReport, "Synthetic unresolved coordinate report");
			ok &= expect(areasRows == 1 && areasReport, "Legacy area rows are reported as source and skipped, none converted");
			ok &= expect(trackViewSources == 2 && trackViewConversions == 2 && stationViewReport,
				"Synthetic display metadata provenance report");
			ok &= expect(views["tracks"].size() == 2 && views["tracks"][0]["track"] == "B0"
					&& views["tracks"][0]["level"] == 0 && views["tracks"][0]["region"] == 2
					&& !views["tracks"][0].contains("visible") && views["tracks"][1]["track"] == "B1"
					&& views["tracks"][1]["level"] == 0 && views["tracks"][1]["region"] == 0
					&& views["tracks"][1]["visible"] == false
					&& views["stations"].size() == 2 && views["stations"][0]["station"] == "Guingamp"
					&& views["stations"][0]["regions"][0]["position_km"] == 0.0,
				"Legacy GUI layout is preserved as native display metadata");
			ok &= expect(!fs::exists(fs::path(output.dir) / "legacy"),
				"Canonical import does not emit a legacy passthrough subtree");
			ok &= expect(infrastructure["nodes"][0]["id"] == "B0.node.1", "Stable node id");
			ok &= expect(infrastructure["arcs"][0]["from"] == "B0.node.1"
					&& infrastructure["arcs"][0]["to"] == "B0.node.2",
				"Arc node references");
			ok &= expect(infrastructure["blocks"][0]["id"] == "0-B0"
					&& infrastructure["blocks"][0]["length_km"] == 0.5,
				"Row-position block id");
			ok &= expect(infrastructure["blocks"].size() >= 2
					&& infrastructure["blocks"][1]["id"] == "0-B1"
					&& infrastructure["blocks"][1]["length_km"] == 0.6,
				"UTF-16LE block data is imported with negative length normalized");
			ok &= expect(infrastructure["arcs"].back()["id"] == "B4.arc.108"
					&& infrastructure["arcs"].back()["from"] == "B4.node.9"
					&& infrastructure["arcs"].back()["to"] == "B4.node.10"
					&& hasDiag(result.diagnostics, "scene.import.adjusted", SceneSeverity::Warning),
				"Known Paimpol B4 source arc is repaired narrowly");
			ok &= expect(rolling["train_units"][0]["traction_curve"].size() == 3
					&& rolling["train_units"][0]["traction_curve"][0][1] == 6.0
					&& hasDiag(result.diagnostics, "scene.import.adjusted", SceneSeverity::Warning),
				"Repeated traction alternatives retain the final monotonic band");
			ok &= expect(infrastructure["connections"].size() == 1
					&& infrastructure["connections"][0]["from"] == "B0.node.1",
				"Resolved connection ref");
			ok &= expect(rolling["train_units"].size() == 1
					&& rolling["train_units"][0]["source"]["data_file"] == "/vehicle.dat"
					&& rolling["train_units"][0]["source"]["traction_file"] == "/effort-curve.dat",
				"Rolling relation follows explicit Trains paths");
			ok &= expect(rolling["train_units"][0]["physical"]
					== json({{"mass_of_traction_unit_kg", 1.0}, {"mass_of_a_wagon_kg", 2.0},
						{"number_of_wagons", 3.0}, {"max_speed_ms", 4.0}, {"max_deceleration_ms2", 5.0},
						{"frontal_area_m2", 6.0}, {"resistance_coefficient", 7.0}, {"jerk_ms3", 8.0},
						{"length_m", 9.0}}),
				"Vehicle data values follow the physical field order");
			ok &= expect(services["services"][0]["operating_code"] == "FixtureService",
				"Active legacy train identity is retained separately from the service id");
			const auto& stops = services["services"][0]["stops"];
			ok &= expect(stops[0].contains("planned_departure_seconds")
					&& !stops[0].contains("planned_arrival_seconds")
					&& !stops[0].contains("departure_seconds"),
				"Missing arrival stays absent");
			ok &= expect(stops[1]["planned_arrival_seconds"] == 20.0
					&& stops[1]["planned_departure_seconds"] == 20.0
					&& hasDiag(result.diagnostics, "scene.import.adjusted", SceneSeverity::Warning),
				"Departure before arrival is clamped at the conversion boundary");
			ok &= expect(signalling["routes"][0]["corridor"] == "fixture-corridor"
					&& signalling["single_track_restrictions"][0]["start_block"] == "0-B0"
					&& signalling["station_boundaries"][0]["entrance_block"] == "0-B0",
				"Signalling roles imported");
			ok &= expect(!signalling.contains("signalling_areas"), "Legacy area rows are not converted to signalling areas");
			bool areasWarning = false;
			for (const auto& diagnostic : result.diagnostics) {
				if (diagnostic.code == "scene.import.areas.skipped" && diagnostic.severity == SceneSeverity::Warning
					&& diagnostic.message.find("holds 7 signalling area rows") != std::string::npos
					&& diagnostic.message.find("Guingamp 0 0.55 0") != std::string::npos
					&& diagnostic.message.find("Lane two 1 2 2 track line 2") != std::string::npos
					&& diagnostic.message.find("row 3 is not readable") != std::string::npos
					&& diagnostic.message.find("E 3 4 0; and 2 more") != std::string::npos
					&& diagnostic.message.find("F 4 5 0") == std::string::npos
					&& diagnostic.file.find("AreasCaseStudy.txt") != std::string::npos && !diagnostic.suggestedFix.empty())
					areasWarning = true;
			}
			ok &= expect(areasWarning, "An area file is reported by a warning that names its first five rows and how to add areas");
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
			json passengers;
			ok &= expect(readJson(fs::path(output.dir) / "passengers.json", passengers)
					&& passengers["passengers"].size() == 1
					&& passengers["passengers"][0]["journeys"][0]["legs"].size() == 1,
				"Full legacy import reuses the passenger conversion seam");

			// Integer fields must be rejected, not narrowed, when the JSON number does not fit an int.
			const auto loadMutated = [&](const char* file, const std::function<void(json&)>& mutate) {
				TempDir copyRoot;
				const fs::path copy = fs::path(copyRoot.dir) / "scene";
				std::error_code copyError;
				fs::copy(output.dir, copy, fs::copy_options::recursive, copyError);
				json value;
				if (copyError || !readJson(copy / file, value))
					return SceneLoadResult();
				mutate(value);
				std::ofstream out(copy / file, std::ios::trunc);
				out << value.dump(2) << "\n";
				out.close();
				return loadScene(copy.string());
			};
			const auto unmodified = loadMutated("scene.json", [](json&) {});
			ok &= expect(!unmodified.scene.sourceFiles.empty() && !hasErrors(unmodified.diagnostics),
				"Unmodified synthetic scene loads without errors");
			const long long beyondInt = static_cast<long long>(INT_MAX) + 1;
			for (const json& badVersion : {json(4294967297LL), json(beyondInt), json(-1), json(1.5), json("1"),
					 json(18446744073709551615ULL)}) {
				const auto loaded = loadMutated("scene.json", [&](json& value) { value["schema_version"] = badVersion; });
				ok &= expect(hasDiag(loaded.diagnostics, "scene.version.unsupported", SceneSeverity::Error)
						&& hasDiagAt(loaded.diagnostics, "scene.json", "schema_version")
						&& loaded.scene.schemaVersion == 0,
					"Unsupported or out-of-range schema_version is rejected: " + badVersion.dump());
			}
			for (const json& badCount : {json(4294967297LL), json(beyondInt), json(-beyondInt - 1), json(1.5),
					 json("2")}) {
				const auto loaded = loadMutated("services.json", [&](json& value) {
					value["services"][0]["repeat"] = {{"headway_seconds", 600.0}, {"count", badCount}};
				});
				ok &= expect(hasDiag(loaded.diagnostics, "scene.field.missing", SceneSeverity::Error)
						&& hasDiagAt(loaded.diagnostics, "services.json", "services[0].repeat.count"),
					"Invalid repeat count is rejected: " + badCount.dump());
			}
			const auto maxCount = loadMutated("services.json", [](json& value) {
				value["services"][0]["repeat"] = {{"headway_seconds", 600.0}, {"count", INT_MAX}};
			});
			ok &= expect(!hasErrors(maxCount.diagnostics) && maxCount.scene.services[0].repeatCount == INT_MAX,
				"Repeat count at the int limit is read unchanged");
			const auto minStep = loadMutated("services.json", [](json& value) {
				value["services"][0]["repeat"] = {{"headway_seconds", 600.0}, {"operating_code_step", INT_MIN}};
			});
			ok &= expect(!hasErrors(minStep.diagnostics) && minStep.scene.services[0].operatingCodeStep == INT_MIN,
				"Operating code step at the int minimum is read unchanged");
			const auto stepBelowMin = loadMutated("services.json", [&](json& value) {
				value["services"][0]["repeat"] = {{"headway_seconds", 600.0}, {"operating_code_step", -beyondInt - 1}};
			});
			ok &= expect(hasDiagAt(stepBelowMin.diagnostics, "services.json", "services[0].repeat.operating_code_step"),
				"Operating code step below the int minimum is rejected");
			for (const json& badOccurrence : {json(4294967298LL), json(beyondInt)}) {
				const auto loaded = loadMutated("passengers.json", [&](json& value) {
					value["passengers"][0]["journeys"][0]["legs"][0]["occurrence"] = badOccurrence;
				});
				ok &= expect(hasDiag(loaded.diagnostics, "scene.field.missing", SceneSeverity::Error)
						&& hasDiagAt(loaded.diagnostics, "passengers.json",
							"passengers[0].journeys[0].legs[0].occurrence"),
					"Out-of-range leg occurrence is rejected: " + badOccurrence.dump());
			}
			const auto maxOccurrence = loadMutated("passengers.json", [](json& value) {
				value["passengers"][0]["journeys"][0]["legs"][0]["occurrence"] = INT_MAX;
			});
			ok &= expect(!hasErrors(maxOccurrence.diagnostics)
					&& maxOccurrence.scene.passengers[0].journeys[0].legs[0].occurrence == INT_MAX,
				"Leg occurrence at the int limit is read unchanged");
			const auto negativeOccurrence = loadMutated("passengers.json", [](json& value) {
				value["passengers"][0]["journeys"][0]["legs"][0]["occurrence"] = -1;
			});
			ok &= expect(hasDiag(validateScene(negativeOccurrence.scene), "scene.occurrence.invalid",
							 SceneSeverity::Error),
				"Negative leg occurrence is reported by validation");
			const auto wrappedDelay = loadMutated("scenarios.json", [](json& value) {
				value["scenarios"][1]["entrance_delays"][0]["occurrence"] = 4294967297LL;
			});
			ok &= expect(hasDiagAt(wrappedDelay.diagnostics, "scenarios.json",
							 "scenarios[1].entrance_delays[0].occurrence"),
				"Out-of-range entrance delay occurrence is rejected");
			const auto wrappedReport = loadMutated("scene.json", [](json& value) {
				value["import_report"][0]["source_count"] = 4294967297LL;
			});
			ok &= expect(hasDiagAt(wrappedReport.diagnostics, "scene.json", "import_report[0].source_count"),
				"Out-of-range import report count is rejected");
			const auto wrappedView = loadMutated("views.json", [](json& value) {
				value["tracks"][0]["level"] = 4294967296LL;
			});
			ok &= expect(hasDiag(wrappedView.diagnostics, "scene.views.row", SceneSeverity::Warning)
					&& wrappedView.scene.trackViews.size() == 1,
				"Out-of-range track display level skips the row");
			const auto wrappedRegion = loadMutated("views.json", [](json& value) {
				value["stations"][0]["regions"][0]["id"] = 4294967296LL;
			});
			ok &= expect(hasDiag(wrappedRegion.diagnostics, "scene.views.row", SceneSeverity::Warning),
				"Out-of-range station region id skips the row");
		}
		// An import publishes its staging directory in one rename. The entries of a private root show every staging or
		// backup sibling that an import leaves.
		{
			TempDir publishRoot;
			const fs::path root(publishRoot.dir);
			const auto entryNames = [](const fs::path& directory) {
				std::set<std::string> names;
				for (const auto& entry : fs::directory_iterator(directory))
					names.insert(entry.path().filename().string());
				return names;
			};
			const fs::path published = root / "scene";
			writeText(published / "marker.txt", "marker\n");
			const auto replaced = importLegacyScene(legacyDir.dir, published.string(), "Paimpol");
			ok &= expect(replaced.success() && replaced.wroteScene && fs::exists(published / "scene.json")
					&& !fs::exists(published / "marker.txt"),
				"Import over an existing directory replaces the whole directory");
			ok &= expect(entryNames(root) == std::set<std::string>{"scene"},
				"Import over an existing directory leaves no staging or backup sibling");
			const fs::path fresh = root / "fresh";
			const auto created = importLegacyScene(legacyDir.dir, fresh.string(), "Paimpol");
			ok &= expect(created.success() && created.wroteScene && fs::exists(fresh / "scene.json"),
				"Import into an absent destination succeeds");
			ok &= expect(entryNames(root) == std::set<std::string>{"scene", "fresh"},
				"Import into an absent destination leaves no staging sibling");
#ifndef _WIN32
			// An import keeps the default mode of a new directory.
			const fs::path probe = root / "probe";
			fs::create_directory(probe);
			ok &= expect(fs::status(published).permissions() == fs::status(probe).permissions(),
				"Imported scene directory has the default mode of a new directory");
			fs::remove(probe);

			// A read-only parent refuses the staging directory. This does not apply to root.
			TempDir readOnlyRoot;
			const fs::path readOnlyParent(readOnlyRoot.dir);
			fs::permissions(readOnlyParent, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
			if (access(readOnlyParent.c_str(), W_OK) != 0) {
				const fs::path blocked = readOnlyParent / "scene";
				const auto refused = importLegacyScene(legacyDir.dir, blocked.string(), "Paimpol");
				bool reported = false;
				for (const auto& diagnostic : refused.diagnostics) {
					reported = reported
						|| (diagnostic.severity == SceneSeverity::Error && diagnostic.code == "scene.import.missing"
							&& diagnostic.message.rfind("Cannot create staging directory", 0) == 0);
				}
				ok &= expect(!refused.wroteScene && reported, "Import into a read-only parent reports the staging directory");
				ok &= expect(!fs::exists(blocked), "Failed import creates no destination");
			}
			fs::permissions(readOnlyParent, fs::perms::owner_all, fs::perm_options::replace);

			// A backup that cannot be emptied is reported as a warning and the import still succeeds. This does not apply to root.
			TempDir keptRoot;
			const fs::path keptScene = fs::path(keptRoot.dir) / "scene";
			const fs::path locked = keptScene / "locked";
			writeText(locked / "keep.txt", "keep\n");
			fs::permissions(locked, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
			if (access(locked.c_str(), W_OK) != 0) {
				const auto kept = importLegacyScene(legacyDir.dir, keptScene.string(), "Paimpol");
				fs::path backup;
				for (const auto& diagnostic : kept.diagnostics) {
					if (diagnostic.severity == SceneSeverity::Warning && diagnostic.code == "scene.import.cleanup"
						&& diagnostic.message.rfind("Could not remove import backup", 0) == 0)
						backup = diagnostic.file;
				}
				ok &= expect(kept.success() && fs::exists(keptScene / "scene.json"),
					"Import still succeeds when the backup cannot be removed");
				ok &= expect(!backup.empty() && fs::exists(backup / "locked" / "keep.txt"),
					"Import warns about the backup that it could not remove");
				if (!backup.empty()) {
					std::error_code backupModeError;
					fs::permissions(backup / "locked", fs::perms::owner_all, fs::perm_options::replace, backupModeError);
				}
			}
			std::error_code lockedModeError;
			fs::permissions(locked, fs::perms::owner_all, fs::perm_options::replace, lockedModeError);
#endif
		}
		fs::remove(legacy / "Passengers/RouteChoiceFC_EQ1.csv");
		const auto partialResult = importLegacyScene(legacyDir.dir, partialOutput.dir, "Paimpol");
		ok &= expect(partialResult.success()
				&& hasDiag(partialResult.diagnostics, "scene.import.passengers", SceneSeverity::Warning)
				&& !fs::exists(fs::path(partialOutput.dir) / "passengers.json"),
			"Whole legacy import treats a one-file passenger pair as optional with a warning");
		fs::remove(legacy / "Passengers/DAS_FrenchCaseStudy.csv");
		const auto noPassengerResult = importLegacyScene(legacyDir.dir, noPassengerOutput.dir, "Paimpol");
		ok &= expect(noPassengerResult.success()
				&& !fs::exists(fs::path(noPassengerOutput.dir) / "passengers.json"),
			"Whole legacy import keeps absent passenger files optional");

		// A UTF-16LE file that ends in an unpaired surrogate cannot be converted, so none
		// of its rows is imported, not even the valid row before the surrogate.
		{
			std::ofstream invalid(legacy / "TrackLines/B1/BlockCumPari.txt", std::ios::binary | std::ios::trunc);
			const unsigned char bytes[] = {0xff, 0xfe, '8', 0, '8', 0, ' ', 0, '-', 0, '0', 0, '.', 0, '6', 0,
				'\r', 0, '\n', 0, 0x00, 0xd8};
			invalid.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
		}
		TempDir invalidOutput;
		const auto invalidResult = importLegacyScene(legacyDir.dir, invalidOutput.dir, "Paimpol");
		json invalidInfrastructure;
		bool importedInvalidBlock = false;
		if (invalidResult.wroteScene && readJson(fs::path(invalidOutput.dir) / "infrastructure.json", invalidInfrastructure))
			for (const auto& block : invalidInfrastructure["blocks"])
				importedInvalidBlock = importedInvalidBlock || block["id"] == "0-B1";
		ok &= expect(!importedInvalidBlock, "A UTF-16LE file with an unpaired surrogate is not imported");
	}

	// Passenger-only import accepts either case-root or passenger-directory input,
	// keeps parseable unresolved rows, and records malformed source rows.
	{
		TempDir root;
		const fs::path passengerDir = fs::path(root.dir) / "Passengers";
		const std::string das = "header0,header1,header2,header3,header4,header5,header6,header7,header8,header9,header10,header11,header12,header13,header14\n"
								"x,p1,x,x,j1,work,Destination,x,x,x,8.25,x,Origin,x,8.00\n"
								"malformed\n"
								"x,p2,x,x,j2,work,Destination,x,x,x,8.25,x,Unknown,x,8.00\n"
								"x,p3,x,x,j3,work,destination,x,x,x,8.25,x,origin,x,8.50\n"
								"x,p4,x,x,j4,work,Destination,x,x,x,nan,x,Origin,x,8.00\n";
		const std::string routes = "person_id,destination,nb_transfers,transfer_n1,r_service_lines_id1\n"
								   "p1,Destination,0,,Guin-Paim_EXPRESS-1-1\n"
								   "short\n"
								   "p2,Destination,0,,unknown-3\n"
								   "p3,destination,0,,Guin-Paim_EXPRESS-1-1\n";
		writeText(passengerDir / "DAS_FrenchCaseStudy.csv", das);
		writeText(passengerDir / "RouteChoiceFC_EQ1.csv", routes);
		SceneModel scene;
		scene.stations = {{"origin", "Origin", false, 0.0, {}}, {"destination", "Destination", false, 1.0, {}}};
		scene.services.push_back({"Guin-Paim-EXPRESS-1", "Guin-Paim-EXPRESS-1", {}, {}, 100.0, false, 0.0, false, false, 0.0,
			false, 0.0, false, 0, false, 0, {}, {}, {}});
		scene.services[0].stops = {{"origin", ""}, {"destination", ""}};
		const ScenePassengerImportResult imported = importLegacyPassengers(root.dir, scene);
		ok &= expect(imported.success() && imported.passengers.size() == 3
				&& imported.passengers[0].journeys[0].legs.size() == 1
				&& imported.passengers[1].journeys[0].legs.empty()
				&& imported.passengers[0].journeys[0].legs[0].serviceId == "Guin-Paim-EXPRESS-1"
				&& imported.passengers[0].journeys[0].legs[0].occurrence == 1,
			"Passenger tokens normalize separators and omit unresolved dangling legs");
		ok &= expect(imported.passengers[0].journeys[0].originStationId == "origin"
				&& imported.passengers[0].journeys[0].destinationStationId == "destination"
				&& imported.passengers[2].journeys[0].originStationId == "origin"
				&& imported.passengers[2].journeys[0].destinationStationId == "destination"
				&& imported.passengers[2].journeys[0].plannedDepartureStartSeconds == 8.0 * 3600.0
				&& imported.passengers[2].journeys[0].plannedDepartureEndSeconds == 8.0 * 3600.0,
			"Passenger station and finite non-quarter time resolution preserve legacy buckets");
		SceneModel ambiguousScene = scene;
		ambiguousScene.stations.push_back({"destination-alt", "Destination", false, 1.0, {}});
		const ScenePassengerImportResult ambiguous = importLegacyPassengers(root.dir, ambiguousScene);
		ok &= expect(ambiguous.success()
				&& ambiguous.passengers[0].journeys[0].destinationStationId == "Destination"
				&& ambiguous.rows[0].unresolvedReferences,
			"Ambiguous display-name station matches remain unresolved");
		bool sawRejected = false, sawUnresolved = false;
		bool rowsIdentifyPassengers = false;
		for (const auto& row : imported.rows) {
			if (!row.accepted)
				sawRejected = true;
			if (row.accepted && row.unresolvedReferences)
				sawUnresolved = true;
			if (row.accepted && row.passengerId == "p1")
				rowsIdentifyPassengers = true;
		}
		ok &= expect(sawRejected && sawUnresolved && rowsIdentifyPassengers,
			"Passenger-only import reports row outcomes, subjects, and context");
		bool sawInvalidTimeRow = false;
		for (const auto& row : imported.rows) {
			if (row.passengerId == "p4" && row.row == 5 && !row.accepted
				&& row.context == "Invalid passenger DAS arrival/departure time"
				&& row.sourceFile.find("DAS_FrenchCaseStudy.csv") != std::string::npos)
				sawInvalidTimeRow = true;
		}
		ok &= expect(sawInvalidTimeRow, "Malformed DAS time is rejected with row and passenger context");
		bool sawDasRowPath = false, sawRouteChoiceRowPath = false;
		for (const auto& diagnostic : imported.diagnostics) {
			if (diagnostic.file.find("DAS_FrenchCaseStudy.csv") != std::string::npos
				&& diagnostic.path.find("passengers.das.rows[") == 0)
				sawDasRowPath = true;
			if (diagnostic.file.find("RouteChoiceFC_EQ1.csv") != std::string::npos
				&& diagnostic.path.find("passengers.route_choice.rows[") == 0)
				sawRouteChoiceRowPath = true;
		}
		ok &= expect(sawDasRowPath && sawRouteChoiceRowPath,
			"Passenger-only import diagnostics retain CSV source paths");
		SceneModel orderMismatchScene = scene;
		orderMismatchScene.services[0].stops = {{"destination", ""}, {"origin", ""}};
		const ScenePassengerImportResult orderMismatch = importLegacyPassengers(root.dir, orderMismatchScene);
		bool knownServiceMarkedUnresolved = false;
		bool knownServiceRowDiagnostic = false;
		for (const auto& row : orderMismatch.rows) {
			if (row.passengerId == "p1" && row.row == 1 && row.accepted && row.unresolvedReferences)
				knownServiceMarkedUnresolved = true;
		}
		for (const auto& diagnostic : orderMismatch.diagnostics) {
			if (diagnostic.code == "scene.import.ref"
				&& diagnostic.path == "passengers.route_choice.rows[1]")
				knownServiceRowDiagnostic = true;
		}
		orderMismatchScene.passengers = orderMismatch.passengers;
		ok &= expect(knownServiceMarkedUnresolved && knownServiceRowDiagnostic
				&& orderMismatch.passengers[0].journeys[0].legs.empty()
				&& !hasDiag(validateScene(orderMismatchScene), "scene.passenger.leg.order", SceneSeverity::Error),
			"Known service stop/order mismatch is reported without a dangling leg");
		const ScenePassengerImportResult direct = importLegacyPassengers(passengerDir.string(), scene);
		ok &= expect(direct.passengers.size() == imported.passengers.size()
				&& direct.rows.size() == imported.rows.size(),
			"Passenger-only import accepts the passenger directory shape");
		TempDir aliasRoot;
		writeText(fs::path(aliasRoot.dir) / "Passengers/DAS_FrenchCaseStudy.csv",
			"h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11,h12,h13,h14\n"
			"x,alias,x,x,j5,work,Tregonnau Squiffiec,x,x,x,8.25,x,Origin,x,8.00\n");
		writeText(fs::path(aliasRoot.dir) / "Passengers/RouteChoiceFC_EQ1.csv",
			"person_id,destination,nb_transfers,transfer_n1,r_service_lines_id1\n"
			"alias,Tregonnau Squiffiec,0,,Guin-Paim_EXPRESS-1-1\n");
		SceneModel aliasScene = scene;
		aliasScene.stations.push_back({"Tregonneau_Squiffiec", "Tregonneau_Squiffiec", false, 2.0, {}});
		aliasScene.services[0].stops = {{"origin", ""}, {"Tregonneau_Squiffiec", ""}};
		const ScenePassengerImportResult spellingAlias = importLegacyPassengers(aliasRoot.dir, aliasScene);
		ok &= expect(spellingAlias.success() && spellingAlias.passengers.size() == 1
				&& spellingAlias.passengers[0].journeys[0].legs.size() == 1
				&& spellingAlias.passengers[0].journeys[0].destinationStationId == "Tregonneau_Squiffiec"
				&& spellingAlias.passengers[0].journeys[0].legs[0].destinationStationId == "Tregonneau_Squiffiec",
			"Known Tregonnau station spelling maps through the explicit alias");
		const ScenePassengerImportResult missing = importLegacyPassengers(
			(fs::path(root.dir) / "missing").string(), scene);
		ok &= expect(!missing.success()
				&& hasDiag(missing.diagnostics, "scene.import.passengers.missing", SceneSeverity::Error),
			"Passenger-only import rejects a missing exact file pair");
		TempDir partial;
		writeText(fs::path(partial.dir) / "DAS_FrenchCaseStudy.csv", das);
		const ScenePassengerImportResult partialOnly = importLegacyPassengers(partial.dir, scene);
		ok &= expect(!partialOnly.success()
				&& hasDiag(partialOnly.diagnostics, "scene.import.passengers.missing", SceneSeverity::Error),
			"Passenger-only import rejects a partial exact file pair");
		TempDir aliases;
		writeText(fs::path(aliases.dir) / "Passengers/PassengersDAS.csv", das);
		writeText(fs::path(aliases.dir) / "Passengers/PassengersRouteChoice.csv", routes);
		const ScenePassengerImportResult aliasResult = importLegacyPassengers(aliases.dir, scene);
		ok &= expect(!aliasResult.success()
				&& hasDiag(aliasResult.diagnostics, "scene.import.passengers.missing", SceneSeverity::Error),
			"Passenger-only import does not accept speculative passenger filename aliases");
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

	// A sidecar is authoritative, but malformed canonical metadata never replaces a destination.
	{
		TempDir legacyDir, output;
		const fs::path legacy(legacyDir.dir), destination(output.dir);
		writeText(legacy / "TrackLines/B0/NodiCumPari.txt", "0 0 0\n1 2 0\n");
		writeText(legacy / "TrackLines/B7/NodiCumPari.txt", "0 0 1\n1 2 1\n");
		writeText(legacy / "TrackLines/Stations.txt", "0\tOrigin\n");
		writeText(legacy / "TrackLines/AreasCaseStudy.txt", "edited historical TXT\n");
		const fs::path sidecar = legacy / "TrackLines/egtrainsignallingareas.json";
		const json areas = json::parse(R"([{"id":" area\twith spaces ","start_km":-0.125,"end_km":2.125,"level":0},{"id":"main","start_km":0,"end_km":1,"level":5,"track":"B0"},{"id":"other","start_km":1,"end_km":2,"level":4,"track":"B7"}])");
		const json valid = {{"format_version", 1}, {"signalling_areas", areas}};
		writeText(sidecar, valid.dump());
		auto imported = importLegacyScene(legacyDir.dir, output.dir, "Areas");
		json signalling, scene;
		ok &= expect(imported.success() && readJson(destination / "signalling.json", signalling)
				&& signalling["signalling_areas"] == areas,
			"Sidecar preserves exact native fields and order regardless of TXT");
		ok &= expect(!hasDiag(imported.diagnostics, "scene.import.areas.skipped", SceneSeverity::Warning), "Authoritative sidecar suppresses historical nonconversion warning");
		readJson(destination / "scene.json", scene);
		int reports = 0;
		for (const auto& row : scene["import_report"]) {
			if (row["category"] != "signalling.areas") continue;
			++reports;
			ok &= expect(fs::equivalent(row["source_file"].get<std::string>(), sidecar) && row["source_count"] == 3
					&& row["converted_count"] == 3 && row["skipped_count"] == 0 && row["unresolved_references"] == 0,
				"Sidecar import report counts actual canonical conversions");
		}
		ok &= expect(reports == 1, "One authoritative area report row");

		std::vector<std::string> malformed = {"{", "[]", R"({"format_version":2,"signalling_areas":[]})",
			R"({"format_version":1.0,"signalling_areas":[]})", R"({"format_version":4294967297,"signalling_areas":[]})",
			R"({"signalling_areas":[]})", R"({"format_version":1})", R"({"format_version":1,"signalling_areas":{}})"};
		for (const auto& field : {"id", "start_km", "end_km", "level"}) {
			json missing = valid;
			missing["signalling_areas"][0].erase(field);
			malformed.push_back(missing.dump());
		}
		for (const auto& field : {"id", "start_km", "end_km", "level", "track"}) {
			json badType = valid;
			badType["signalling_areas"][0][field] = nullptr;
			malformed.push_back(badType.dump());
		}
		for (const auto& mutation : std::vector<json>{
				 {{"track", "B99"}}, {{"track", "main"}}, {{"level", 4294967297LL}}, {{"level", 1.5}},
				 {{"level", -99999999}}, {{"level", 6}}, {{"start_km", 3}}, {{"id", ""}}, {{"id", "main"}}}) {
			json badArea = valid;
			badArea["signalling_areas"][0].update(mutation);
			malformed.push_back(badArea.dump());
		}
		for (const auto& text : malformed) {
			writeText(sidecar, text);
			writeText(destination / "scene.json", "existing scene bytes\n");
			writeText(destination / "marker", "existing marker\n");
			imported = importLegacyScene(legacyDir.dir, output.dir, "Areas");
			std::ifstream oldScene(destination / "scene.json");
			std::string bytes((std::istreambuf_iterator<char>(oldScene)), std::istreambuf_iterator<char>());
			ok &= expect(!imported.success() && !imported.wroteScene && hasErrors(imported.diagnostics)
					&& bytes == "existing scene bytes\n" && fs::exists(destination / "marker"),
				"Malformed sidecar preserves existing destination: " + text);
		}
		writeText(sidecar, json({{"format_version", 1}, {"signalling_areas", json::array()}}).dump());
		imported = importLegacyScene(legacyDir.dir, output.dir, "Areas");
		ok &= expect(imported.success() && readJson(destination / "signalling.json", signalling)
				&& signalling["signalling_areas"] == json::array(),
			"Empty sidecar remains authoritative over TXT");
		fs::remove(sidecar);
		imported = importLegacyScene(legacyDir.dir, output.dir, "Areas");
		ok &= expect(imported.success() && hasDiag(imported.diagnostics, "scene.import.areas.skipped", SceneSeverity::Warning)
				&& readJson(destination / "signalling.json", signalling) && !signalling.contains("signalling_areas"),
			"Historical absence keeps existing warning and nonconversion");

		// Marked conversion must not collapse directory aliases or rewrite route references.
		writeText(destination / "scene.json", "existing scene bytes\n");
		writeText(destination / "marker", "existing marker\n");
		fs::rename(legacy / "TrackLines/B7", legacy / "TrackLines/B07");
		json renumbered = valid;
		renumbered["signalling_areas"][2]["track"] = "B07";
		for (int variant : {0, 1, 2, 3}) {
			if (variant == 1) writeText(legacy / "TrackLines/B7/NodiCumPari.txt", "0 0 1\n1 2 1\n");
			json metadata = renumbered;
			if (variant == 1) metadata["signalling_areas"].push_back({{"id", "seven"}, {"start_km", 0}, {"end_km", 2}, {"level", 4}, {"track", "B7"}});
			if (variant == 2) metadata = valid; // Only B7 is scoped; B07 is still a candidate.
			if (variant == 3) metadata["signalling_areas"] = json::array();
			writeText(sidecar, metadata.dump());
			imported = importLegacyScene(legacyDir.dir, output.dir, "Areas");
			std::ifstream saved(destination / "scene.json");
			std::string bytes((std::istreambuf_iterator<char>(saved)), {});
			std::ifstream marker(destination / "marker");
			std::string markerBytes((std::istreambuf_iterator<char>(marker)), {});
			ok &= expect(!imported.success() && !imported.wroteScene && hasDiag(imported.diagnostics, "scene.import.ref", SceneSeverity::Error)
					&& bytes == "existing scene bytes\n" && markerBytes == "existing marker\n",
				"Noncanonical or duplicate candidates fail marked import and preserve destination, including empty/unscoped metadata");
		}
		fs::remove_all(legacy / "TrackLines/B07");
		fs::rename(legacy / "TrackLines/B7", legacy / "TrackLines/b7");
		writeText(sidecar, valid.dump());
		imported = importLegacyScene(legacyDir.dir, output.dir, "Areas");
		ok &= expect(imported.success() && readJson(destination / "signalling.json", signalling) && signalling["signalling_areas"] == areas,
			"A lowercase prefix with canonical decimal suffix still reconstructs correctly");
		fs::remove(sidecar);
		writeText(legacy / "EGTRAINSignallingAreas.json", "malformed metadata at an unrecognised root path");
		imported = importLegacyScene(legacyDir.dir, output.dir, "Areas");
		ok &= expect(imported.success() && hasDiag(imported.diagnostics, "scene.import.areas.skipped", SceneSeverity::Warning), "Only the reserved TrackLines sidecar path is recognised");
	}

	// Legacy data with Unicode names: the case folder, the files that the manifest and the train file name, and the scene
	// destination.
	{
		TempDir root;
		const fs::path legacy = fs::path(root.dir) / fs::u8path(kUnicodeName);
		const fs::path destination = fs::path(root.dir) / fs::u8path("scene-" + kUnicodeName);
		writeText(legacy / "TrackLines/B0/NodiCumPari.txt", "0 0 0\n1 2 0\n");
		writeText(legacy / "TrackLines/Stations.txt", "0\tOrigin\n");
		writeText(legacy / "Routes/Route0.txt", "@0-B0@\n");
		writeText(legacy / "trainNames.txt", kUnicodeName + ".txt\n");
		writeText(legacy / "Trains" / fs::u8path(kUnicodeName + ".txt"),
			"Service 0 99999999 0 /" + kUnicodeName + ".dat /" + kUnicodeName + "-effort.dat /TimeTable/" + kUnicodeName + ".txt\n");
		writeText(legacy / fs::u8path(kUnicodeName + ".dat"), "1 2 3 4 5 6 7 8 9\n");
		writeText(legacy / fs::u8path(kUnicodeName + "-effort.dat"), "0 1 2 3 4\n1 2 3 4 5\n");
		writeText(legacy / "TimeTable" / fs::u8path(kUnicodeName + ".txt"), "Origin 2 -1 10\n");
		const auto imported = importLegacyScene(legacy.u8string(), destination.u8string(), "Unicode");
		for (const auto& diagnostic : imported.diagnostics)
			if (diagnostic.severity == SceneSeverity::Error)
				std::cerr << "Unicode import: " << toDisplayText(diagnostic) << "\n";
		json rolling, scene;
		ok &= expect(imported.success() && readJson(destination / "rolling_stock.json", rolling) && readJson(destination / "scene.json", scene),
			"Legacy data with Unicode names imports to a directory with a Unicode name");
		bool named = false;
		for (const auto& unit : rolling["train_units"])
			named = named || (unit.contains("source") && unit["source"]["data_file"] == "/" + kUnicodeName + ".dat");
		ok &= expect(named, "Train data file with a Unicode name is found and recorded");
		bool reported = false;
		for (const auto& row : scene["import_report"])
			reported = reported || row["source_file"] == (legacy / "trainNames.txt").u8string();
		ok &= expect(reported, "Import report names a manifest below a Unicode folder as UTF-8");
		ok &= expect(!hasDiag(imported.diagnostics, "scene.import.missing", SceneSeverity::Error),
			"No file with a Unicode name is reported missing");
		const fs::path absent = fs::path(root.dir) / fs::u8path("absent-" + kUnicodeName);
		const auto refused = importLegacyScene(absent.u8string(), destination.u8string(), "Unicode");
		bool namedAbsent = false;
		for (const auto& diagnostic : refused.diagnostics)
			namedAbsent = namedAbsent || (diagnostic.code == "scene.import.missing" && diagnostic.file == absent.u8string());
		ok &= expect(namedAbsent, "A missing case folder with a Unicode name is reported as UTF-8");
	}

	ok &= checkScenarioItemDiagnostics();

	if (!ok)
		return 1;
	std::cout << "all SceneImporter tests passed\n";
	return 0;
}
