#include "scene/SceneExporter.h"
#include "scene/SceneImporter.h"
#include "scene/SceneModel.h"
#include "scene/SceneValidator.h"
#include "scene/SceneWriter.h"

#include <QByteArray>
#include <QScopeGuard>
#include <QTest>

#include <algorithm>
#include <chrono>
#include <climits>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

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

// Writes content as UTF-16 LE with one byte too many.
static void writeOddUtf16Le(const fs::path& path, const std::string& content) {
	writeUtf16Le(path, content);
	std::ofstream output(path, std::ios::binary | std::ios::app);
	output.put('\n');
}

// Writes a legacy case of one track, one station, one route and one service.
static void writeSmallLegacyCase(const fs::path& legacy) {
	writeText(legacy / "TrackLines/B0/NodiCumPari.txt", "0 0 0\n1 2 0\n");
	writeText(legacy / "TrackLines/Stations.txt", "0\tOrigin\n");
	writeText(legacy / "Routes/Route0.txt", "@0-B0@\n");
	writeText(legacy / "trainNames.txt", "Service.txt\n");
	writeText(legacy / "Trains/Service.txt", "Service 0 99999999 0 /Service.dat /Service-effort.dat /TimeTable/Service.txt\n");
	writeText(legacy / "Service.dat", "1 2 3 4 5 6 7 8 9\n");
	writeText(legacy / "Service-effort.dat", "0 1 2 3 4\n1 2 3 4 5\n");
	writeText(legacy / "TimeTable/Service.txt", "Origin 2 -1 10\n");
}

static bool hasReadDiag(const std::vector<SceneDiagnostic>& diagnostics, const fs::path& file, SceneSeverity severity) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.code == "scene.import.read" && diagnostic.severity == severity
			&& fs::u8path(diagnostic.file).lexically_normal() == file.lexically_normal())
			return true;
	}
	return false;
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

// One small fixture exercises the new fields together, including explicit
// Trains provenance and a coordinate that must remain unresolved.
static void writeSyntheticLegacyCase(const fs::path& legacy) {
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
	writeText(legacy / "EGTRAINInitialParameters.json",
		R"({"format_version":1,"base_time":"08:15:00","simulation_settings":{"duration_seconds":1234,"buffer_time_seconds":0,"recovery_time_percent":0}})");
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
		"x,p1,x,x,j1,work,Paimpol,x,x,x,8.25,x,Guingamp,x,8.00\n"
		"x,p2,x,x,j1,,Paimpol,x,x,x,8.75,x,Guingamp,x,8.50\n");
	writeText(legacy / "Passengers/RouteChoiceFC_EQ1.csv",
		"person_id,destination,nb_transfers,transfer_n1,r_service_lines_id1\n"
		"p1,Paimpol,0,,FixtureService-1\n");
	writeText(legacy / "Incidents.txt",
		"signal_failure\t0-B0\t10\t20\ntrain_breakdown\tFixtureService\t30\t40\n");
	writeText(legacy / "TimeTable/Scenarios_Entrance_Delays/Rollout_1.txt", "15\n20\n");
}

// The synthetic legacy case and its import.
struct SyntheticImport {
	TempDir legacyDir;
	TempDir output;
	SceneImportResult result;

	SyntheticImport() {
		writeSyntheticLegacyCase(legacyDir.dir);
		result = importLegacyScene(legacyDir.dir, output.dir, "Paimpol");
	}
};

// One more than the largest int.
static const long long kBeyondInt = static_cast<long long>(INT_MAX) + 1;

// Loads a copy of the scene directory scene in which mutate changed file, or gives an empty result when the copy fails.
static SceneLoadResult loadMutated(const std::string& scene, const char* file, const std::function<void(json&)>& mutate) {
	TempDir copyRoot;
	const fs::path copy = fs::path(copyRoot.dir) / "scene";
	std::error_code copyError;
	fs::copy(scene, copy, fs::copy_options::recursive, copyError);
	json value;
	if (copyError || !readJson(copy / file, value))
		return SceneLoadResult();
	mutate(value);
	std::ofstream out(copy / file, std::ios::trunc);
	out << value.dump(2) << "\n";
	out.close();
	return loadScene(copy.string());
}

// Writes a legacy case of two tracks with nodes, a station and an area file that the import cannot convert.
static void writeSidecarCase(const fs::path& legacy) {
	writeText(legacy / "TrackLines/B0/NodiCumPari.txt", "0 0 0\n1 2 0\n");
	writeText(legacy / "TrackLines/B7/NodiCumPari.txt", "0 0 1\n1 2 1\n");
	writeText(legacy / "TrackLines/Stations.txt", "0\tOrigin\n");
	writeText(legacy / "TrackLines/AreasCaseStudy.txt", "edited historical TXT\n");
}

// The signalling areas of the sidecar of writeSidecarCase().
static json sidecarAreas() {
	return json::parse(R"([{"id":" area\twith spaces ","start_km":-0.125,"end_km":2.125,"level":0},{"id":"main","start_km":0,"end_km":1,"level":5,"track":"B0"},{"id":"other","start_km":1,"end_km":2,"level":4,"track":"B7"}])");
}

// A valid sidecar with sidecarAreas().
static json validSidecar() {
	return {{"format_version", 1}, {"signalling_areas", sidecarAreas()}};
}

static std::string readBytes(const fs::path& path) {
	std::ifstream input(path);
	return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

class TestSceneImporter : public QObject {
	Q_OBJECT

private slots:
	void readsStrictTrainSourceFiles();
	void importsSyntheticCase();
	void rejectsUnsupportedSchemaVersion_data();
	void rejectsUnsupportedSchemaVersion();
	void rejectsInvalidRepeatCount_data();
	void rejectsInvalidRepeatCount();
	void rejectsOutOfRangeLegOccurrence_data();
	void rejectsOutOfRangeLegOccurrence();
	void readsIntegerFieldsWithinTheirRange();
	void replacesDestinationInOneRename();
	void reportsReadOnlyStagingParent();
	void warnsAboutBackupItCannotRemove();
	void keepsOptionalFilesOptional();
	void importsPassengersOnly();
	void rejectsOverlappingPaths();
	void keepsDestinationOfMalformedSource();
	void readsSidecarAreas();
	void malformedSidecarKeepsDestination_data();
	void malformedSidecarKeepsDestination();
	void emptyOrAbsentSidecar();
	void markedConversionKeepsTrackFolders();
	void marksCopenhagenDoubleSwitches_data();
	void marksCopenhagenDoubleSwitches();
	void importsUnicodeNames();
	void lastExportDecidesRoutesAndIncidents();
	void reportsOddSizedUtf16Files();
	void skipsNonFiniteNumbers();
	void reportsFilesThatCannotBeOpened();
	void reportsFolderThatCannotBeRead();
	void reportsMalformedScenarioItems();
	void reportsMalformedPassengers();
};

void TestSceneImporter::readsStrictTrainSourceFiles() {
	TempDir root;
	const fs::path physical = fs::path(root.dir) / "physical.txt";
	const fs::path traction = fs::path(root.dir) / "traction.txt";
	writeText(physical, "100 50 0 80 1 12 -0.2 0 30\n");
	writeText(traction, "0 10 100 0 0\n\n10 20 90 0 0\n");
	const auto parsedPhysical = parseTrainPhysicalSourceFile(physical.string());
	const auto parsedTraction = parseTrainTractionSourceFile(traction.string());
	// Strict physical source accepts the native zero-wagon domain.
	QVERIFY(parsedPhysical.success());
	QCOMPARE_EQ(parsedPhysical.physical.number_of_wagons, 0.0);
	// Strict traction source accepts complete rows and blank lines.
	QVERIFY(parsedTraction.success());
	QCOMPARE(parsedTraction.tractionCurve.size(), std::size_t(2));
	writeText(physical, "101 52 3 83 1.5 12.5 0.25 0.75 31\n");
	const auto orderedPhysical = parseTrainPhysicalSourceFile(physical.string());
	// Strict physical source assigns the nine values in file order.
	QVERIFY(orderedPhysical.success());
	QCOMPARE_EQ(orderedPhysical.physical.mass_of_traction_unit_kg, 101.0);
	QCOMPARE_EQ(orderedPhysical.physical.mass_of_a_wagon_kg, 52.0);
	QCOMPARE_EQ(orderedPhysical.physical.number_of_wagons, 3.0);
	QCOMPARE_EQ(orderedPhysical.physical.max_speed_ms, 83.0);
	QCOMPARE_EQ(orderedPhysical.physical.max_deceleration_ms2, 1.5);
	QCOMPARE_EQ(orderedPhysical.physical.frontal_area_m2, 12.5);
	QCOMPARE_EQ(orderedPhysical.physical.resistance_coefficient, 0.25);
	QCOMPARE_EQ(orderedPhysical.physical.jerk_ms3, 0.75);
	QCOMPARE_EQ(orderedPhysical.physical.length_m, 31.0);
	writeText(physical, "100 50 0 80 1 12 -0.2 0 30 extra\n");
	QVERIFY2(!parseTrainPhysicalSourceFile(physical.string()).success(), "Strict physical source rejects extra tokens");
	writeText(traction, "0 10 100 0 0\npartial\n");
	QVERIFY2(!parseTrainTractionSourceFile(traction.string()).success(),
		"Strict traction source rejects a malformed row instead of skipping it");
	writeText(traction, "0 10 100 0 0\n5 20 90 0 0\n");
	QVERIFY2(!parseTrainTractionSourceFile(traction.string()).success(), "Strict traction source rejects overlapping intervals");
}

void TestSceneImporter::importsSyntheticCase() {
	const SyntheticImport synthetic;
	const TempDir& legacyDir = synthetic.legacyDir;
	const TempDir& output = synthetic.output;
	const SceneImportResult& result = synthetic.result;
	QVERIFY2(result.success(), "Complete synthetic fixture imports");
	json scene, infrastructure, rolling, services, signalling, scenarios, views;
	QVERIFY2(readJson(fs::path(output.dir) / "scene.json", scene), "Synthetic scene readable");
	QVERIFY2(readJson(fs::path(output.dir) / "infrastructure.json", infrastructure), "Synthetic infrastructure readable");
	QVERIFY2(readJson(fs::path(output.dir) / "rolling_stock.json", rolling), "Synthetic rolling stock readable");
	QVERIFY2(readJson(fs::path(output.dir) / "services.json", services), "Synthetic services readable");
	QVERIFY2(readJson(fs::path(output.dir) / "signalling.json", signalling), "Synthetic signalling readable");
	QVERIFY2(readJson(fs::path(output.dir) / "scenarios.json", scenarios), "Synthetic scenarios readable");
	QVERIFY2(readJson(fs::path(output.dir) / "views.json", views), "Synthetic views readable");
	// Settings of the sidecar are imported before the compiled ones.
	QVERIFY(scene["base_time"] == "08:15:00");
	QVERIFY(scene["simulation_settings"]["duration_seconds"] == 1234.0);
	// Legacy import records the creating application version.
	QVERIFY(scene["schema_version"] == kCurrentSceneSchemaVersion);
	QVERIFY(scene["saved_with_app_version"] == EGTRAIN_APP_VERSION);
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
	QVERIFY2(rootReport, "Synthetic root provenance report");
	QVERIFY2(coordinateReport, "Synthetic unresolved coordinate report");
	// Legacy area rows are reported as source and skipped, none converted.
	QCOMPARE(areasRows, 1);
	QVERIFY(areasReport);
	// Synthetic display metadata provenance report.
	QCOMPARE(trackViewSources, 2);
	QCOMPARE(trackViewConversions, 2);
	QVERIFY(stationViewReport);
	// Legacy GUI layout is preserved as native display metadata.
	QCOMPARE(views["tracks"].size(), std::size_t(2));
	QVERIFY(views["tracks"][0]["track"] == "B0");
	QVERIFY(views["tracks"][0]["level"] == 0);
	QVERIFY(views["tracks"][0]["region"] == 2);
	QVERIFY(!views["tracks"][0].contains("visible"));
	QVERIFY(views["tracks"][1]["track"] == "B1");
	QVERIFY(views["tracks"][1]["level"] == 0);
	QVERIFY(views["tracks"][1]["region"] == 0);
	QVERIFY(views["tracks"][1]["visible"] == false);
	QCOMPARE(views["stations"].size(), std::size_t(2));
	QVERIFY(views["stations"][0]["station"] == "Guingamp");
	QVERIFY(views["stations"][0]["regions"][0]["position_km"] == 0.0);
	QVERIFY2(!fs::exists(fs::path(output.dir) / "legacy"), "Canonical import does not emit a legacy passthrough subtree");
	QVERIFY2(infrastructure["nodes"][0]["id"] == "B0.node.1", "Stable node id");
	// Arc node references.
	QVERIFY(infrastructure["arcs"][0]["from"] == "B0.node.1");
	QVERIFY(infrastructure["arcs"][0]["to"] == "B0.node.2");
	// Row-position block id.
	QVERIFY(infrastructure["blocks"][0]["id"] == "0-B0");
	QVERIFY(infrastructure["blocks"][0]["length_km"] == 0.5);
	// UTF-16LE block data is imported with negative length normalized.
	QCOMPARE_GE(infrastructure["blocks"].size(), std::size_t(2));
	QVERIFY(infrastructure["blocks"][1]["id"] == "0-B1");
	QVERIFY(infrastructure["blocks"][1]["length_km"] == 0.6);
	// Known Paimpol B4 source arc is repaired narrowly.
	QVERIFY(infrastructure["arcs"].back()["id"] == "B4.arc.108");
	QVERIFY(infrastructure["arcs"].back()["from"] == "B4.node.9");
	QVERIFY(infrastructure["arcs"].back()["to"] == "B4.node.10");
	QVERIFY(hasDiag(result.diagnostics, "scene.import.adjusted", SceneSeverity::Warning));
	// Repeated traction alternatives retain the final monotonic band.
	QCOMPARE(rolling["train_units"][0]["traction_curve"].size(), std::size_t(3));
	QVERIFY(rolling["train_units"][0]["traction_curve"][0][1] == 6.0);
	QVERIFY(hasDiag(result.diagnostics, "scene.import.adjusted", SceneSeverity::Warning));
	// Resolved connection ref.
	QCOMPARE(infrastructure["connections"].size(), std::size_t(1));
	QVERIFY(infrastructure["connections"][0]["from"] == "B0.node.1");
	// Rolling relation follows explicit Trains paths.
	QCOMPARE(rolling["train_units"].size(), std::size_t(1));
	QVERIFY(rolling["train_units"][0]["source"]["data_file"] == "/vehicle.dat");
	QVERIFY(rolling["train_units"][0]["source"]["traction_file"] == "/effort-curve.dat");
	QVERIFY2(rolling["train_units"][0]["physical"]
			== json({{"mass_of_traction_unit_kg", 1.0}, {"mass_of_a_wagon_kg", 2.0}, {"number_of_wagons", 3.0},
				{"max_speed_ms", 4.0}, {"max_deceleration_ms2", 5.0}, {"frontal_area_m2", 6.0},
				{"resistance_coefficient", 7.0}, {"jerk_ms3", 8.0}, {"length_m", 9.0}}),
		"Vehicle data values follow the physical field order");
	QVERIFY2(services["services"][0]["operating_code"] == "FixtureService",
		"Active legacy train identity is retained separately from the service id");
	const auto& stops = services["services"][0]["stops"];
	// Missing arrival stays absent.
	QVERIFY(stops[0].contains("planned_departure_seconds"));
	QVERIFY(!stops[0].contains("planned_arrival_seconds"));
	QVERIFY(!stops[0].contains("departure_seconds"));
	// Departure before arrival is clamped at the conversion boundary.
	QVERIFY(stops[1]["planned_arrival_seconds"] == 20.0);
	QVERIFY(stops[1]["planned_departure_seconds"] == 20.0);
	QVERIFY(hasDiag(result.diagnostics, "scene.import.adjusted", SceneSeverity::Warning));
	// Signalling roles imported.
	QVERIFY(signalling["routes"][0]["corridor"] == "fixture-corridor");
	QVERIFY(signalling["single_track_restrictions"][0]["start_block"] == "0-B0");
	QVERIFY(signalling["station_boundaries"][0]["entrance_block"] == "0-B0");
	QVERIFY2(!signalling.contains("signalling_areas"), "Legacy area rows are not converted to signalling areas");
	bool areasWarning = false;
	for (const auto& diagnostic : result.diagnostics) {
		if (diagnostic.code == "scene.import.areas.skipped" && diagnostic.severity == SceneSeverity::Warning
			&& diagnostic.message.find("holds 7 signalling area rows") != std::string::npos
			&& diagnostic.message.find("Guingamp 0 0.55 0") != std::string::npos
			&& diagnostic.message.find("Lane two 1 2 2 track line 2") != std::string::npos
			&& diagnostic.message.find("row 3 is not readable") != std::string::npos
			&& diagnostic.message.find("E 3 4 0, and 2 more") != std::string::npos
			&& diagnostic.message.find("F 4 5 0") == std::string::npos
			&& diagnostic.file.find("AreasCaseStudy.txt") != std::string::npos && !diagnostic.suggestedFix.empty())
			areasWarning = true;
	}
	QVERIFY2(areasWarning, "An area file is reported by a warning that names its first five rows and how to add areas");
	// Sparse route ids leave a stable append id for joined routes.
	QCOMPARE(signalling["routes"].size(), std::size_t(3));
	QVERIFY(signalling["routes"][2]["id"] == "route3");
	QVERIFY(signalling["routes"][2]["reversed"] == true);
	// Rollout retains global incidents and entrance delays.
	QVERIFY(scenarios["default_scenario_id"] == "baseline");
	QCOMPARE(scenarios["scenarios"].size(), std::size_t(2));
	QCOMPARE(scenarios["scenarios"][0]["incidents"].size(), std::size_t(3));
	QCOMPARE(scenarios["scenarios"][1]["incidents"].size(), std::size_t(3));
	QVERIFY(scenarios["scenarios"][1]["incidents"][0]["id"] == "rollout_1.incident.1");
	QCOMPARE(scenarios["scenarios"][1]["entrance_delays"].size(), std::size_t(4));
	// Duplicate operating-code incident expands to every matching canonical service.
	QVERIFY(scenarios["scenarios"][0]["incidents"][1]["target"] == "FixtureService");
	QVERIFY(scenarios["scenarios"][0]["incidents"][2]["target"] == "FixtureService_2");
	QVERIFY(hasDiag(result.diagnostics, "scene.import.expanded", SceneSeverity::Warning));
	json passengers;
	// Full legacy import reuses the passenger conversion seam.
	QVERIFY(readJson(fs::path(output.dir) / "passengers.json", passengers));
	QCOMPARE(passengers["passengers"].size(), std::size_t(2));
	QCOMPARE(passengers["passengers"][0]["journeys"][0]["legs"].size(), std::size_t(1));
	const SceneLoadResult importedScene = loadScene(output.dir);
	QVERIFY2(passengers == encodePassengers(importLegacyPassengers(legacyDir.dir, importedScene.scene).passengers),
		"Full legacy import writes the passengers with the scene writer's encoding");
	const SceneModel& model = importedScene.scene;
	json stationsFile;
	// Full legacy import writes each file with the scene writer's encoding.
	QVERIFY(readJson(fs::path(output.dir) / "stations.json", stationsFile));
	QVERIFY(scene == encodeSceneMetadata(model));
	QVERIFY(infrastructure == encodeInfrastructure(model));
	QVERIFY(stationsFile == encodeStations(model));
	QVERIFY(signalling == encodeSignalling(model));
	QVERIFY(rolling == encodeRollingStock(model));
	QVERIFY(services == encodeServices(model));
	QVERIFY(scenarios == encodeScenarios(model));
	QVERIFY(views == encodeViews(model));
	// A journey without an activity leaves the key out of the imported file.
	QVERIFY(passengers["passengers"][0]["journeys"][0]["activity"] == "work");
	QVERIFY(!passengers["passengers"][1]["journeys"][0].contains("activity"));
}

// Integer fields must be rejected, not narrowed, when the JSON number does not fit an int.
void TestSceneImporter::rejectsUnsupportedSchemaVersion_data() {
	QTest::addColumn<QByteArray>("text");
	for (const json& badVersion : {json(4294967297LL), json(kBeyondInt), json(-1), json(1.5), json("1"),
			 json(18446744073709551615ULL)})
		QTest::newRow(badVersion.dump().c_str()) << QByteArray::fromStdString(badVersion.dump());
}

// Unsupported or out-of-range schema_version is rejected.
void TestSceneImporter::rejectsUnsupportedSchemaVersion() {
	QFETCH(QByteArray, text);
	const json badVersion = json::parse(text.toStdString());
	const SyntheticImport synthetic;
	const auto loaded = loadMutated(synthetic.output.dir, "scene.json", [&](json& value) { value["schema_version"] = badVersion; });
	QVERIFY(hasDiag(loaded.diagnostics, "scene.version.unsupported", SceneSeverity::Error));
	QVERIFY(hasDiagAt(loaded.diagnostics, "scene.json", "schema_version"));
	QCOMPARE(loaded.scene.schemaVersion, 0);
}

void TestSceneImporter::rejectsInvalidRepeatCount_data() {
	QTest::addColumn<QByteArray>("text");
	for (const json& badCount : {json(4294967297LL), json(kBeyondInt), json(-kBeyondInt - 1), json(1.5), json("2")})
		QTest::newRow(badCount.dump().c_str()) << QByteArray::fromStdString(badCount.dump());
}

// Invalid repeat count is rejected.
void TestSceneImporter::rejectsInvalidRepeatCount() {
	QFETCH(QByteArray, text);
	const json badCount = json::parse(text.toStdString());
	const SyntheticImport synthetic;
	const auto loaded = loadMutated(synthetic.output.dir, "services.json", [&](json& value) {
		value["services"][0]["repeat"] = {{"headway_seconds", 600.0}, {"count", badCount}};
	});
	QVERIFY(hasDiag(loaded.diagnostics, "scene.field.missing", SceneSeverity::Error));
	QVERIFY(hasDiagAt(loaded.diagnostics, "services.json", "services[0].repeat.count"));
}

void TestSceneImporter::rejectsOutOfRangeLegOccurrence_data() {
	QTest::addColumn<QByteArray>("text");
	for (const json& badOccurrence : {json(4294967298LL), json(kBeyondInt)})
		QTest::newRow(badOccurrence.dump().c_str()) << QByteArray::fromStdString(badOccurrence.dump());
}

// Out-of-range leg occurrence is rejected.
void TestSceneImporter::rejectsOutOfRangeLegOccurrence() {
	QFETCH(QByteArray, text);
	const json badOccurrence = json::parse(text.toStdString());
	const SyntheticImport synthetic;
	const auto loaded = loadMutated(synthetic.output.dir, "passengers.json", [&](json& value) {
		value["passengers"][0]["journeys"][0]["legs"][0]["occurrence"] = badOccurrence;
	});
	QVERIFY(hasDiag(loaded.diagnostics, "scene.field.missing", SceneSeverity::Error));
	QVERIFY(hasDiagAt(loaded.diagnostics, "passengers.json", "passengers[0].journeys[0].legs[0].occurrence"));
}

void TestSceneImporter::readsIntegerFieldsWithinTheirRange() {
	const SyntheticImport synthetic;
	const std::string& output = synthetic.output.dir;
	const auto unmodified = loadMutated(output, "scene.json", [](json&) {});
	// Unmodified synthetic scene loads without errors.
	QVERIFY(!unmodified.scene.sourceFiles.empty());
	QVERIFY(!hasErrors(unmodified.diagnostics));
	const auto maxCount = loadMutated(output, "services.json", [](json& value) {
		value["services"][0]["repeat"] = {{"headway_seconds", 600.0}, {"count", INT_MAX}};
	});
	// Repeat count at the int limit is read unchanged.
	QVERIFY(!hasErrors(maxCount.diagnostics));
	QCOMPARE(maxCount.scene.services[0].repeatCount, INT_MAX);
	const auto minStep = loadMutated(output, "services.json", [](json& value) {
		value["services"][0]["repeat"] = {{"headway_seconds", 600.0}, {"operating_code_step", INT_MIN}};
	});
	// Operating code step at the int minimum is read unchanged.
	QVERIFY(!hasErrors(minStep.diagnostics));
	QCOMPARE(minStep.scene.services[0].operatingCodeStep, INT_MIN);
	const auto stepBelowMin = loadMutated(output, "services.json", [](json& value) {
		value["services"][0]["repeat"] = {{"headway_seconds", 600.0}, {"operating_code_step", -kBeyondInt - 1}};
	});
	QVERIFY2(hasDiagAt(stepBelowMin.diagnostics, "services.json", "services[0].repeat.operating_code_step"),
		"Operating code step below the int minimum is rejected");
	const auto maxOccurrence = loadMutated(output, "passengers.json", [](json& value) {
		value["passengers"][0]["journeys"][0]["legs"][0]["occurrence"] = INT_MAX;
	});
	// Leg occurrence at the int limit is read unchanged.
	QVERIFY(!hasErrors(maxOccurrence.diagnostics));
	QCOMPARE(maxOccurrence.scene.passengers[0].journeys[0].legs[0].occurrence, INT_MAX);
	const auto negativeOccurrence = loadMutated(output, "passengers.json", [](json& value) {
		value["passengers"][0]["journeys"][0]["legs"][0]["occurrence"] = -1;
	});
	QVERIFY2(hasDiag(validateScene(negativeOccurrence.scene), "scene.occurrence.invalid", SceneSeverity::Error),
		"Negative leg occurrence is reported by validation");
	const auto wrappedDelay = loadMutated(output, "scenarios.json", [](json& value) {
		value["scenarios"][1]["entrance_delays"][0]["occurrence"] = 4294967297LL;
	});
	QVERIFY2(hasDiagAt(wrappedDelay.diagnostics, "scenarios.json", "scenarios[1].entrance_delays[0].occurrence"),
		"Out-of-range entrance delay occurrence is rejected");
	const auto wrappedReport = loadMutated(output, "scene.json", [](json& value) {
		value["import_report"][0]["source_count"] = 4294967297LL;
	});
	QVERIFY2(hasDiagAt(wrappedReport.diagnostics, "scene.json", "import_report[0].source_count"),
		"Out-of-range import report count is rejected");
	const auto wrappedView = loadMutated(output, "views.json", [](json& value) {
		value["tracks"][0]["level"] = 4294967296LL;
	});
	// Out-of-range track display level skips the row.
	QVERIFY(hasDiag(wrappedView.diagnostics, "scene.views.row", SceneSeverity::Warning));
	QCOMPARE(wrappedView.scene.trackViews.size(), std::size_t(1));
	const auto textSwitch = loadMutated(output, "views.json", [](json& value) {
		value["tracks"][0]["double_switch"] = "yes";
	});
	// A double switch that is not a boolean skips the row.
	QVERIFY(hasDiag(textSwitch.diagnostics, "scene.views.row", SceneSeverity::Warning));
	QCOMPARE(textSwitch.scene.trackViews.size(), std::size_t(1));
	const auto wrappedRegion = loadMutated(output, "views.json", [](json& value) {
		value["stations"][0]["regions"][0]["id"] = 4294967296LL;
	});
	QVERIFY2(hasDiag(wrappedRegion.diagnostics, "scene.views.row", SceneSeverity::Warning),
		"Out-of-range station region id skips the row");
}

// An import publishes its staging directory in one rename. The entries of a private root show every staging or
// backup sibling that an import leaves.
void TestSceneImporter::replacesDestinationInOneRename() {
	TempDir legacyDir;
	writeSyntheticLegacyCase(legacyDir.dir);
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
	// Import over an existing directory replaces the whole directory.
	QVERIFY(replaced.success());
	QVERIFY(replaced.wroteScene);
	QVERIFY(fs::exists(published / "scene.json"));
	QVERIFY(!fs::exists(published / "marker.txt"));
	// Import over an existing directory leaves no staging or backup sibling.
	QCOMPARE(entryNames(root), std::set<std::string>{"scene"});
	const fs::path fresh = root / "fresh";
	const auto created = importLegacyScene(legacyDir.dir, fresh.string(), "Paimpol");
	// Import into an absent destination succeeds.
	QVERIFY(created.success());
	QVERIFY(created.wroteScene);
	QVERIFY(fs::exists(fresh / "scene.json"));
	// Import into an absent destination leaves no staging sibling.
	QCOMPARE(entryNames(root), std::set<std::string>({"scene", "fresh"}));
#ifndef _WIN32
	// An import keeps the default mode of a new directory.
	const fs::path probe = root / "probe";
	fs::create_directory(probe);
	QVERIFY2(fs::status(published).permissions() == fs::status(probe).permissions(),
		"Imported scene directory has the default mode of a new directory");
	fs::remove(probe);
#endif
}

// A read-only parent refuses the staging directory. This does not apply to root.
void TestSceneImporter::reportsReadOnlyStagingParent() {
#ifdef _WIN32
	QSKIP("POSIX permissions");
#else
	TempDir legacyDir;
	writeSyntheticLegacyCase(legacyDir.dir);
	TempDir readOnlyRoot;
	const fs::path readOnlyParent(readOnlyRoot.dir);
	fs::permissions(readOnlyParent, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
	const auto restore = qScopeGuard(
		[&readOnlyParent] { fs::permissions(readOnlyParent, fs::perms::owner_all, fs::perm_options::replace); });
	if (access(readOnlyParent.c_str(), W_OK) != 0) {
		const fs::path blocked = readOnlyParent / "scene";
		const auto refused = importLegacyScene(legacyDir.dir, blocked.string(), "Paimpol");
		bool reported = false;
		for (const auto& diagnostic : refused.diagnostics) {
			reported = reported
				|| (diagnostic.severity == SceneSeverity::Error && diagnostic.code == "scene.save.write"
					&& diagnostic.message == "Cannot create a private case staging directory");
		}
		// Import into a read-only parent reports the staging directory.
		QVERIFY(!refused.wroteScene);
		QVERIFY(reported);
		QVERIFY2(!fs::exists(blocked), "Failed import creates no destination");
	}
#endif
}

// A backup that cannot be emptied is reported as a warning and the import still succeeds. This does not apply to root.
void TestSceneImporter::warnsAboutBackupItCannotRemove() {
#ifdef _WIN32
	QSKIP("POSIX permissions");
#else
	TempDir legacyDir;
	writeSyntheticLegacyCase(legacyDir.dir);
	TempDir keptRoot;
	const fs::path keptScene = fs::path(keptRoot.dir) / "scene";
	const fs::path locked = keptScene / "locked";
	writeText(locked / "keep.txt", "keep\n");
	fs::permissions(locked, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
	const auto unlock = qScopeGuard([&locked] {
		std::error_code lockedModeError;
		fs::permissions(locked, fs::perms::owner_all, fs::perm_options::replace, lockedModeError);
	});
	if (access(locked.c_str(), W_OK) != 0) {
		const auto kept = importLegacyScene(legacyDir.dir, keptScene.string(), "Paimpol");
		fs::path backup;
		for (const auto& diagnostic : kept.diagnostics) {
			if (diagnostic.severity == SceneSeverity::Warning && diagnostic.code == "scene.save.cleanup"
				&& diagnostic.message.rfind("Cannot remove the previous case generation", 0) == 0)
				backup = diagnostic.file;
		}
		const auto unlockBackup = qScopeGuard([&backup] {
			if (!backup.empty()) {
				std::error_code backupModeError;
				fs::permissions(backup / "locked", fs::perms::owner_all, fs::perm_options::replace, backupModeError);
			}
		});
		// Import still succeeds when the backup cannot be removed.
		QVERIFY(kept.success());
		QVERIFY(fs::exists(keptScene / "scene.json"));
		// Import warns about the backup that it could not remove.
		QVERIFY(!backup.empty());
		QVERIFY(fs::exists(backup / "locked" / "keep.txt"));
	}
#endif
}

// The synthetic case with an unsupported settings file, then without one and then both passenger files, then with a
// block file that cannot be converted.
void TestSceneImporter::keepsOptionalFilesOptional() {
	TempDir legacyDir, partialOutput, noPassengerOutput;
	const fs::path legacy(legacyDir.dir);
	writeSyntheticLegacyCase(legacy);
	writeText(legacy / "EGTRAINInitialParameters.json", "{\"format_version\":2}\n");
	TempDir malformedOutput;
	const auto malformedResult = importLegacyScene(legacyDir.dir, malformedOutput.dir, "Paimpol");
	json malformedScene;
	// An unsupported settings file is reported and the compiled mapping is used.
	QVERIFY(malformedResult.success());
	QVERIFY(hasDiag(malformedResult.diagnostics, "scene.import.settings", SceneSeverity::Warning));
	QVERIFY(readJson(fs::path(malformedOutput.dir) / "scene.json", malformedScene));
	QVERIFY(malformedScene["base_time"] == "07:10:40");
	fs::remove(legacy / "Passengers/RouteChoiceFC_EQ1.csv");
	const auto partialResult = importLegacyScene(legacyDir.dir, partialOutput.dir, "Paimpol");
	// Whole legacy import treats a one-file passenger pair as optional with a warning.
	QVERIFY(partialResult.success());
	QVERIFY(hasDiag(partialResult.diagnostics, "scene.import.passengers", SceneSeverity::Warning));
	QVERIFY(!fs::exists(fs::path(partialOutput.dir) / "passengers.json"));
	fs::remove(legacy / "Passengers/DAS_FrenchCaseStudy.csv");
	const auto noPassengerResult = importLegacyScene(legacyDir.dir, noPassengerOutput.dir, "Paimpol");
	// Whole legacy import keeps absent passenger files optional.
	QVERIFY(noPassengerResult.success());
	QVERIFY(!fs::exists(fs::path(noPassengerOutput.dir) / "passengers.json"));

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
	QVERIFY2(!importedInvalidBlock, "A UTF-16LE file with an unpaired surrogate is not imported");
}

// Passenger-only import accepts either case-root or passenger-directory input,
// keeps parseable unresolved rows, and records malformed source rows.
void TestSceneImporter::importsPassengersOnly() {
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
	// Passenger tokens normalize separators and omit unresolved dangling legs.
	QVERIFY(imported.success());
	QCOMPARE(imported.passengers.size(), std::size_t(3));
	QCOMPARE(imported.passengers[0].journeys[0].legs.size(), std::size_t(1));
	QVERIFY(imported.passengers[1].journeys[0].legs.empty());
	QCOMPARE(imported.passengers[0].journeys[0].legs[0].serviceId, "Guin-Paim-EXPRESS-1");
	QCOMPARE(imported.passengers[0].journeys[0].legs[0].occurrence, 1);
	// Passenger station and finite non-quarter time resolution preserve legacy buckets.
	QCOMPARE(imported.passengers[0].journeys[0].originStationId, "origin");
	QCOMPARE(imported.passengers[0].journeys[0].destinationStationId, "destination");
	QCOMPARE(imported.passengers[2].journeys[0].originStationId, "origin");
	QCOMPARE(imported.passengers[2].journeys[0].destinationStationId, "destination");
	QCOMPARE_EQ(imported.passengers[2].journeys[0].plannedDepartureStartSeconds, 8.0 * 3600.0);
	QCOMPARE_EQ(imported.passengers[2].journeys[0].plannedDepartureEndSeconds, 8.0 * 3600.0);
	SceneModel ambiguousScene = scene;
	ambiguousScene.stations.push_back({"destination-alt", "Destination", false, 1.0, {}});
	const ScenePassengerImportResult ambiguous = importLegacyPassengers(root.dir, ambiguousScene);
	// Ambiguous display-name station matches remain unresolved.
	QVERIFY(ambiguous.success());
	QCOMPARE(ambiguous.passengers[0].journeys[0].destinationStationId, "Destination");
	QVERIFY(ambiguous.rows[0].unresolvedReferences);
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
	// Passenger-only import reports row outcomes, subjects, and context.
	QVERIFY(sawRejected);
	QVERIFY(sawUnresolved);
	QVERIFY(rowsIdentifyPassengers);
	bool sawInvalidTimeRow = false;
	for (const auto& row : imported.rows) {
		if (row.passengerId == "p4" && row.row == 5 && !row.accepted
			&& row.context == "Invalid passenger DAS arrival/departure time"
			&& row.sourceFile.find("DAS_FrenchCaseStudy.csv") != std::string::npos)
			sawInvalidTimeRow = true;
	}
	QVERIFY2(sawInvalidTimeRow, "Malformed DAS time is rejected with row and passenger context");
	bool sawDasRowPath = false, sawRouteChoiceRowPath = false;
	for (const auto& diagnostic : imported.diagnostics) {
		if (diagnostic.file.find("DAS_FrenchCaseStudy.csv") != std::string::npos
			&& diagnostic.path.find("passengers.das.rows[") == 0)
			sawDasRowPath = true;
		if (diagnostic.file.find("RouteChoiceFC_EQ1.csv") != std::string::npos
			&& diagnostic.path.find("passengers.route_choice.rows[") == 0)
			sawRouteChoiceRowPath = true;
	}
	// Passenger-only import diagnostics retain CSV source paths.
	QVERIFY(sawDasRowPath);
	QVERIFY(sawRouteChoiceRowPath);
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
	// Known service stop/order mismatch is reported without a dangling leg.
	QVERIFY(knownServiceMarkedUnresolved);
	QVERIFY(knownServiceRowDiagnostic);
	QVERIFY(orderMismatch.passengers[0].journeys[0].legs.empty());
	QVERIFY(!hasDiag(validateScene(orderMismatchScene), "scene.passenger.leg.order", SceneSeverity::Error));
	const ScenePassengerImportResult direct = importLegacyPassengers(passengerDir.string(), scene);
	// Passenger-only import accepts the passenger directory shape.
	QCOMPARE(direct.passengers.size(), imported.passengers.size());
	QCOMPARE(direct.rows.size(), imported.rows.size());
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
	// Known Tregonnau station spelling maps through the explicit alias.
	QVERIFY(spellingAlias.success());
	QCOMPARE(spellingAlias.passengers.size(), std::size_t(1));
	QCOMPARE(spellingAlias.passengers[0].journeys[0].legs.size(), std::size_t(1));
	QCOMPARE(spellingAlias.passengers[0].journeys[0].destinationStationId, "Tregonneau_Squiffiec");
	QCOMPARE(spellingAlias.passengers[0].journeys[0].legs[0].destinationStationId, "Tregonneau_Squiffiec");
	// A repeated journey number gets a suffix, and the legs of a transfer continue the journey id.
	TempDir transferRoot;
	writeText(fs::path(transferRoot.dir) / "Passengers/DAS_FrenchCaseStudy.csv",
		"h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11,h12,h13,h14\n"
		"x,t1,x,x,j1,work,Destination,x,x,x,8.25,x,Origin,x,8.00\n"
		"x,t1,x,x,j1,,Middle,x,x,x,9.75,x,Origin,x,9.00\n");
	writeText(fs::path(transferRoot.dir) / "Passengers/RouteChoiceFC_EQ1.csv",
		"person_id,destination,nb_transfers,transfer_n1,r_service_lines_id1,r_service_lines_id2\n"
		"t1,Destination,1,Middle,ServiceA-1,ServiceB-2\n");
	SceneModel transferScene = scene;
	transferScene.stations.push_back({"middle", "Middle", false, 0.5, {}});
	SceneService serviceA = scene.services[0];
	serviceA.id = serviceA.operatingCode = "ServiceA";
	serviceA.stops = {{"origin", ""}, {"middle", ""}};
	SceneService serviceB = serviceA;
	serviceB.id = serviceB.operatingCode = "ServiceB";
	serviceB.stops = {{"middle", ""}, {"destination", ""}};
	transferScene.services = {serviceA, serviceB};
	const ScenePassengerImportResult transfer = importLegacyPassengers(transferRoot.dir, transferScene);
	// Passenger journeys keep the legacy ids, activities and time windows.
	QVERIFY(transfer.success());
	QCOMPARE(transfer.passengers.size(), std::size_t(1));
	QCOMPARE(transfer.passengers[0].id, "t1");
	QCOMPARE(transfer.passengers[0].journeys.size(), std::size_t(2));
	QCOMPARE(transfer.passengers[0].journeys[0].id, "t1:j1");
	QCOMPARE(transfer.passengers[0].journeys[1].id, "t1:j1_2");
	QCOMPARE(transfer.passengers[0].journeys[0].activity, "work");
	QVERIFY(transfer.passengers[0].journeys[1].activity.empty());
	QCOMPARE_EQ(transfer.passengers[0].journeys[0].plannedDepartureStartSeconds, 8.0 * 3600.0);
	QCOMPARE_EQ(transfer.passengers[0].journeys[0].plannedArrivalEndSeconds, 8.0 * 3600.0 + 1799.0);
	QCOMPARE_EQ(transfer.passengers[0].journeys[1].plannedDepartureEndSeconds, 9.0 * 3600.0);
	QCOMPARE_EQ(transfer.passengers[0].journeys[1].plannedArrivalStartSeconds, 9.0 * 3600.0 + 1800.0);
	const std::vector<ScenePassengerLeg>& transferLegs = transfer.passengers[0].journeys[0].legs;
	// Passenger transfer legs continue the journey id and keep the service occurrence.
	QCOMPARE(transferLegs.size(), std::size_t(2));
	QCOMPARE(transferLegs[0].id, "t1:j1.leg.1");
	QCOMPARE(transferLegs[0].originStationId, "origin");
	QCOMPARE(transferLegs[0].destinationStationId, "middle");
	QCOMPARE(transferLegs[0].serviceId, "ServiceA");
	QCOMPARE(transferLegs[0].occurrence, 1);
	QCOMPARE(transferLegs[1].id, "t1:j1.leg.2");
	QCOMPARE(transferLegs[1].originStationId, "middle");
	QCOMPARE(transferLegs[1].destinationStationId, "destination");
	QCOMPARE(transferLegs[1].serviceId, "ServiceB");
	QCOMPARE(transferLegs[1].occurrence, 2);
	const ScenePassengerImportResult missing = importLegacyPassengers(
		(fs::path(root.dir) / "missing").string(), scene);
	// Passenger-only import rejects a missing exact file pair.
	QVERIFY(!missing.success());
	QVERIFY(hasDiag(missing.diagnostics, "scene.import.passengers.missing", SceneSeverity::Error));
	TempDir partial;
	writeText(fs::path(partial.dir) / "DAS_FrenchCaseStudy.csv", das);
	const ScenePassengerImportResult partialOnly = importLegacyPassengers(partial.dir, scene);
	// Passenger-only import rejects a partial exact file pair.
	QVERIFY(!partialOnly.success());
	QVERIFY(hasDiag(partialOnly.diagnostics, "scene.import.passengers.missing", SceneSeverity::Error));
	TempDir aliases;
	writeText(fs::path(aliases.dir) / "Passengers/PassengersDAS.csv", das);
	writeText(fs::path(aliases.dir) / "Passengers/PassengersRouteChoice.csv", routes);
	const ScenePassengerImportResult aliasResult = importLegacyPassengers(aliases.dir, scene);
	// Passenger-only import does not accept speculative passenger filename aliases.
	QVERIFY(!aliasResult.success());
	QVERIFY(hasDiag(aliasResult.diagnostics, "scene.import.passengers.missing", SceneSeverity::Error));
}

// Atomic path safety: reject overlap, and preserve an existing destination
// when malformed legacy input cannot be imported.
void TestSceneImporter::rejectsOverlappingPaths() {
	TempDir root;
	const fs::path legacy = fs::path(root.dir) / "legacy";
	const fs::path nestedScene = legacy / "nested-scene";
	writeText(legacy / "TrackLines/Stations.txt", "0\tSource\n");
	const auto result = importLegacyScene(legacy.string(), nestedScene.string(), "Overlap");
	// Overlapping source and destination are rejected.
	QVERIFY(!result.wroteScene);
	QVERIFY(hasDiag(result.diagnostics, "scene.import.path", SceneSeverity::Error));
	QVERIFY2(!fs::exists(nestedScene), "Overlap rejection does not create destination");
}

void TestSceneImporter::keepsDestinationOfMalformedSource() {
	TempDir legacyDir, output;
	writeText(fs::path(legacyDir.dir) / "TrackLines/Stations.txt", "0\tSource\n");
	writeText(fs::path(legacyDir.dir) / "Trains/broken.txt", "malformed\n");
	writeText(fs::path(output.dir) / "scene.json", "existing scene bytes\n");
	const auto result = importLegacyScene(legacyDir.dir, output.dir, "Malformed");
	// Malformed source is not published.
	QVERIFY(!result.success());
	QVERIFY(!result.wroteScene);
	// Malformed import preserves destination.
	QCOMPARE(readBytes(fs::path(output.dir) / "scene.json"), "existing scene bytes\n");
}

// A sidecar is authoritative, but malformed canonical metadata never replaces a destination.
void TestSceneImporter::readsSidecarAreas() {
	TempDir legacyDir, output;
	const fs::path legacy(legacyDir.dir), destination(output.dir);
	writeSidecarCase(legacy);
	const fs::path sidecar = legacy / "TrackLines/egtrainsignallingareas.json";
	const json areas = sidecarAreas();
	writeText(sidecar, validSidecar().dump());
	auto imported = importLegacyScene(legacyDir.dir, output.dir, "Areas");
	json signalling, scene;
	// Sidecar preserves exact native fields and order regardless of TXT.
	QVERIFY(imported.success());
	QVERIFY(readJson(destination / "signalling.json", signalling));
	QVERIFY(signalling["signalling_areas"] == areas);
	QVERIFY2(!hasDiag(imported.diagnostics, "scene.import.areas.skipped", SceneSeverity::Warning),
		"Authoritative sidecar suppresses historical nonconversion warning");
	readJson(destination / "scene.json", scene);
	int reports = 0;
	for (const auto& row : scene["import_report"]) {
		if (row["category"] != "signalling.areas") continue;
		++reports;
		// Sidecar import report counts actual canonical conversions.
		QVERIFY(fs::equivalent(row["source_file"].get<std::string>(), sidecar));
		QVERIFY(row["source_count"] == 3);
		QVERIFY(row["converted_count"] == 3);
		QVERIFY(row["skipped_count"] == 0);
		QVERIFY(row["unresolved_references"] == 0);
	}
	// One authoritative area report row.
	QCOMPARE(reports, 1);
}

void TestSceneImporter::malformedSidecarKeepsDestination_data() {
	QTest::addColumn<QByteArray>("text");
	for (const char* text : {"{", "[]", R"({"format_version":2,"signalling_areas":[]})",
			 R"({"format_version":1.0,"signalling_areas":[]})", R"({"format_version":4294967297,"signalling_areas":[]})",
			 R"({"signalling_areas":[]})", R"({"format_version":1})", R"({"format_version":1,"signalling_areas":{}})"})
		QTest::newRow(text) << QByteArray(text);
	const json valid = validSidecar();
	for (const auto& field : {"id", "start_km", "end_km", "level"}) {
		json missing = valid;
		missing["signalling_areas"][0].erase(field);
		QTest::newRow((std::string("without ") + field).c_str()) << QByteArray::fromStdString(missing.dump());
	}
	for (const auto& field : {"id", "start_km", "end_km", "level", "track"}) {
		json badType = valid;
		badType["signalling_areas"][0][field] = nullptr;
		QTest::newRow((std::string("null ") + field).c_str()) << QByteArray::fromStdString(badType.dump());
	}
	for (const auto& mutation : std::vector<json>{
			 {{"track", "B99"}}, {{"track", "main"}}, {{"level", 4294967297LL}}, {{"level", 1.5}},
			 {{"level", -99999999}}, {{"level", 6}}, {{"start_km", 3}}, {{"id", ""}}, {{"id", "main"}}}) {
		json badArea = valid;
		badArea["signalling_areas"][0].update(mutation);
		QTest::newRow(mutation.dump().c_str()) << QByteArray::fromStdString(badArea.dump());
	}
}

// Malformed sidecar preserves existing destination.
void TestSceneImporter::malformedSidecarKeepsDestination() {
	QFETCH(QByteArray, text);
	TempDir legacyDir, output;
	const fs::path legacy(legacyDir.dir), destination(output.dir);
	writeSidecarCase(legacy);
	const fs::path sidecar = legacy / "TrackLines/egtrainsignallingareas.json";
	writeText(sidecar, text.toStdString());
	writeText(destination / "scene.json", "existing scene bytes\n");
	writeText(destination / "marker", "existing marker\n");
	const auto imported = importLegacyScene(legacyDir.dir, output.dir, "Areas");
	QVERIFY(!imported.success());
	QVERIFY(!imported.wroteScene);
	QVERIFY(hasErrors(imported.diagnostics));
	QCOMPARE(readBytes(destination / "scene.json"), "existing scene bytes\n");
	QVERIFY(fs::exists(destination / "marker"));
}

void TestSceneImporter::emptyOrAbsentSidecar() {
	TempDir legacyDir, output;
	const fs::path legacy(legacyDir.dir), destination(output.dir);
	writeSidecarCase(legacy);
	const fs::path sidecar = legacy / "TrackLines/egtrainsignallingareas.json";
	json signalling;
	writeText(sidecar, json({{"format_version", 1}, {"signalling_areas", json::array()}}).dump());
	auto imported = importLegacyScene(legacyDir.dir, output.dir, "Areas");
	// Empty sidecar remains authoritative over TXT.
	QVERIFY(imported.success());
	QVERIFY(readJson(destination / "signalling.json", signalling));
	QVERIFY(!signalling.contains("signalling_areas"));
	QVERIFY(!hasDiag(imported.diagnostics, "scene.import.areas.skipped", SceneSeverity::Warning));
	fs::remove(sidecar);
	imported = importLegacyScene(legacyDir.dir, output.dir, "Areas");
	// Historical absence keeps existing warning and nonconversion.
	QVERIFY(imported.success());
	QVERIFY(hasDiag(imported.diagnostics, "scene.import.areas.skipped", SceneSeverity::Warning));
	QVERIFY(readJson(destination / "signalling.json", signalling));
	QVERIFY(!signalling.contains("signalling_areas"));
}

// Marked conversion must not collapse directory aliases or rewrite route references.
void TestSceneImporter::markedConversionKeepsTrackFolders() {
	TempDir legacyDir, output;
	const fs::path legacy(legacyDir.dir), destination(output.dir);
	writeSidecarCase(legacy);
	const fs::path sidecar = legacy / "TrackLines/egtrainsignallingareas.json";
	const json valid = validSidecar();
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
		const auto imported = importLegacyScene(legacyDir.dir, output.dir, "Areas");
		const std::string bytes = readBytes(destination / "scene.json");
		const std::string markerBytes = readBytes(destination / "marker");
		const std::string message = "Noncanonical or duplicate candidates fail marked import and preserve destination, including empty/unscoped metadata: variant "
			+ std::to_string(variant);
		QVERIFY2(!imported.success(), message.c_str());
		QVERIFY2(!imported.wroteScene, message.c_str());
		QVERIFY2(hasDiag(imported.diagnostics, "scene.import.ref", SceneSeverity::Error), message.c_str());
		QVERIFY2(bytes == "existing scene bytes\n", message.c_str());
		QVERIFY2(markerBytes == "existing marker\n", message.c_str());
	}
	fs::remove_all(legacy / "TrackLines/B07");
	fs::rename(legacy / "TrackLines/B7", legacy / "TrackLines/b7");
	writeText(sidecar, valid.dump());
	auto imported = importLegacyScene(legacyDir.dir, output.dir, "Areas");
	json signalling;
	// A lowercase prefix with canonical decimal suffix still reconstructs correctly.
	QVERIFY(imported.success());
	QVERIFY(readJson(destination / "signalling.json", signalling));
	QVERIFY(signalling["signalling_areas"] == sidecarAreas());
	fs::remove(sidecar);
	writeText(legacy / "EGTRAINSignallingAreas.json", "malformed metadata at an unrecognised root path");
	imported = importLegacyScene(legacyDir.dir, output.dir, "Areas");
	// Only the reserved TrackLines sidecar path is recognised.
	QVERIFY(imported.success());
	QVERIFY(hasDiag(imported.diagnostics, "scene.import.areas.skipped", SceneSeverity::Warning));
}

// The original drew every track of four nodes of the Copenhagen case as a double switch, and no track of another case.
// The legacy files cannot hold the drawing, so an export of the case keeps it only through an import as that case.
void TestSceneImporter::marksCopenhagenDoubleSwitches_data() {
	QTest::addColumn<QString>("name");
	QTest::newRow("Copenhagen") << "Copenhagen";
	QTest::newRow("Other") << "Other";
}

void TestSceneImporter::marksCopenhagenDoubleSwitches() {
	QFETCH(QString, name);
	TempDir legacyDir;
	const fs::path legacy(legacyDir.dir);
	writeText(legacy / "TrackLines/B0/NodiCumPari.txt", "1 0 0\n2 0.01 0\n3 0.02 0\n4 0.03 0\n");
	writeText(legacy / "TrackLines/B1/NodiCumPari.txt", "1 0 1\n2 0.03 1\n");
	writeText(legacy / "TrackLines/Stations.txt", "0\tOrigin\n");
	writeText(legacy / "GUI/caseStudyTrackData.txt", "0\t0\t0\n1\t1\t0\n");
	TempDir output, exported, reimported;
	json views, reimportedViews;
	const bool imported = importLegacyScene(legacyDir.dir, output.dir, name.toStdString()).success()
		&& readJson(fs::path(output.dir) / "views.json", views) && views["tracks"].size() == 2;
	const bool copenhagen = name == "Copenhagen";
	// The import marks the double switches of the original.
	QVERIFY(imported);
	QCOMPARE(views["tracks"][0].contains("double_switch"), copenhagen);
	QVERIFY(!copenhagen || views["tracks"][0]["double_switch"] == true);
	QVERIFY(!views["tracks"][1].contains("double_switch"));
	// The case keeps its double switches through an export and an import.
	QVERIFY(exportLegacyScene(output.dir, exported.dir).success());
	QVERIFY(importLegacyScene(exported.dir, reimported.dir, name.toStdString()).success());
	QVERIFY(readJson(fs::path(reimported.dir) / "views.json", reimportedViews));
	QVERIFY(reimportedViews["tracks"] == views["tracks"]);
}

// Legacy data with Unicode names: the case folder, the files that the manifest and the train file name, and the scene
// destination.
void TestSceneImporter::importsUnicodeNames() {
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
	// Legacy data with Unicode names imports to a directory with a Unicode name.
	QVERIFY(imported.success());
	QVERIFY(readJson(destination / "rolling_stock.json", rolling));
	QVERIFY(readJson(destination / "scene.json", scene));
	bool named = false;
	for (const auto& unit : rolling["train_units"])
		named = named || (unit.contains("source") && unit["source"]["data_file"] == "/" + kUnicodeName + ".dat");
	QVERIFY2(named, "Train data file with a Unicode name is found and recorded");
	bool reported = false;
	for (const auto& row : scene["import_report"])
		reported = reported || row["source_file"] == (legacy / "trainNames.txt").u8string();
	QVERIFY2(reported, "Import report names a manifest below a Unicode folder as UTF-8");
	QVERIFY2(!hasDiag(imported.diagnostics, "scene.import.missing", SceneSeverity::Error),
		"No file with a Unicode name is reported missing");
	const fs::path absent = fs::path(root.dir) / fs::u8path("absent-" + kUnicodeName);
	const auto refused = importLegacyScene(absent.u8string(), destination.u8string(), "Unicode");
	bool namedAbsent = false;
	for (const auto& diagnostic : refused.diagnostics)
		namedAbsent = namedAbsent || (diagnostic.code == "scene.import.missing" && diagnostic.file == absent.u8string());
	QVERIFY2(namedAbsent, "A missing case folder with a Unicode name is reported as UTF-8");
}

// A case with three routes and an incident and then a case with one route and none, exported into the same folder,
// imports with one route and no incident.
void TestSceneImporter::lastExportDecidesRoutesAndIncidents() {
	TempDir root;
	const fs::path threeRoutes = fs::path(root.dir) / "three";
	const fs::path oneRoute = fs::path(root.dir) / "one";
	writeSmallLegacyCase(threeRoutes);
	writeText(threeRoutes / "Routes/Route1.txt", "@0-B0@\n");
	writeText(threeRoutes / "Routes/Route2.txt", "@0-B0@\n");
	writeText(threeRoutes / "Incidents.txt", "signal_failure\t0-B0\t10\t20\n");
	writeSmallLegacyCase(oneRoute);
	const fs::path exported = fs::path(root.dir) / "exported";
	const fs::path reimported = fs::path(root.dir) / "reimported";
	json threeSignalling, signalling, scenarios;
	// A case with three routes and an incident imports and exports.
	QVERIFY(importLegacyScene(threeRoutes.string(), (fs::path(root.dir) / "three-scene").string(), "Three").success());
	QVERIFY(readJson(fs::path(root.dir) / "three-scene/signalling.json", threeSignalling));
	QCOMPARE(threeSignalling["routes"].size(), std::size_t(3));
	QVERIFY(exportLegacyScene((fs::path(root.dir) / "three-scene").string(), exported.string()).success());
	QVERIFY(fs::is_regular_file(exported / "Incidents.txt"));
	// A case with one route exports into the same folder.
	QVERIFY(importLegacyScene(oneRoute.string(), (fs::path(root.dir) / "one-scene").string(), "One").success());
	QVERIFY(exportLegacyScene((fs::path(root.dir) / "one-scene").string(), exported.string()).success());
	// The folder imports with the one route of the last export.
	QVERIFY(importLegacyScene(exported.string(), reimported.string(), "One").success());
	QVERIFY(readJson(reimported / "signalling.json", signalling));
	QCOMPARE(signalling["routes"].size(), std::size_t(1));
	bool incidents = false;
	if (readJson(reimported / "scenarios.json", scenarios))
		for (const auto& scenario : scenarios["scenarios"])
			incidents = incidents || !scenario["incidents"].empty();
	QVERIFY2(!incidents, "The folder imports without the incident of the first export");
}

// A UTF-16 file with an odd byte count cannot be read. A manifest is then replaced by the Trains directory, and a
// rollout is left out.
void TestSceneImporter::reportsOddSizedUtf16Files() {
	TempDir root;
	const fs::path legacy = fs::path(root.dir) / "odd";
	const fs::path destination = fs::path(root.dir) / "odd-scene";
	writeSmallLegacyCase(legacy);
	writeOddUtf16Le(legacy / "trainNames.txt", "Service.txt\n");
	const fs::path rollout = legacy / "TimeTable/Scenarios_Entrance_Delays/Rollout_1.txt";
	writeOddUtf16Le(rollout, "5\n");
	const auto imported = importLegacyScene(legacy.string(), destination.string(), "Odd");
	json services, scenarios;
	// A case with an odd-sized UTF-16 manifest and rollout imports.
	QVERIFY(imported.success());
	QVERIFY(readJson(destination / "services.json", services));
	QVERIFY(readJson(destination / "scenarios.json", scenarios));
	// An odd-sized UTF-16 manifest is reported as unreadable, not as missing.
	QVERIFY(hasReadDiag(imported.diagnostics, legacy / "trainNames.txt", SceneSeverity::Warning));
	QVERIFY(!hasDiag(imported.diagnostics, "scene.import.info", SceneSeverity::Info));
	// An unreadable manifest falls back to the Trains directory.
	QCOMPARE(services["services"].size(), std::size_t(1));
	bool rolloutImported = false;
	for (const auto& scenario : scenarios["scenarios"])
		rolloutImported = rolloutImported || scenario["id"] == "rollout_1";
	// An odd-sized UTF-16 rollout is reported and left out.
	QVERIFY(hasReadDiag(imported.diagnostics, rollout, SceneSeverity::Warning));
	QVERIFY(!rolloutImported);
}

// A token that std::stod reads as NaN or infinity is a malformed number. A node row with one is skipped with a warning
// that names the file and the row, and a Trains file with one stops the import. Passenger files without a passenger
// give no passengers.json, and the scene files have no carriage return on any platform.
void TestSceneImporter::skipsNonFiniteNumbers() {
	TempDir root;
	const fs::path legacy = fs::path(root.dir) / "nonfinite";
	const fs::path destination = fs::path(root.dir) / "nonfinite-scene";
	writeSmallLegacyCase(legacy);
	const fs::path nodes = legacy / "TrackLines/B0/NodiCumPari.txt";
	writeText(nodes, "0 0 0\n1 2 0\n2 nan 0\n3 2 inf\n");
	writeText(legacy / "Passengers/DAS_FrenchCaseStudy.csv", "h0,h1,h2,h3,h4,h5,h6,h7,h8,h9,h10,h11,h12,h13,h14\n");
	writeText(legacy / "Passengers/RouteChoiceFC_EQ1.csv", "person_id,destination,nb_transfers\n");
	const auto imported = importLegacyScene(legacy.string(), destination.string(), "NonFinite");
	const auto skippedRow = [&](const std::string& message) {
		return std::any_of(imported.diagnostics.begin(), imported.diagnostics.end(), [&](const SceneDiagnostic& diagnostic) {
			return diagnostic.code == "scene.import.parse" && diagnostic.severity == SceneSeverity::Warning
				&& diagnostic.message == message && fs::u8path(diagnostic.file).lexically_normal() == nodes.lexically_normal();
		});
	};
	// A node row with nan or inf is skipped with a warning that names the file and the row.
	QVERIFY(skippedRow("Invalid node row 2"));
	QVERIFY(skippedRow("Invalid node row 3"));
	const SceneLoadResult loaded = loadScene(destination.u8string());
	// A case with skipped non-finite node rows imports to a scene that loads.
	QVERIFY(imported.success());
	QVERIFY(!hasErrors(loaded.diagnostics));
	QCOMPARE(loaded.scene.nodes.size(), std::size_t(2));
	// Passenger files that give no passenger write no passengers.json.
	QVERIFY(imported.success());
	QVERIFY(!fs::exists(destination / "passengers.json"));
	bool carriageReturn = false;
	for (const auto& entry : fs::directory_iterator(destination)) {
		std::ifstream input(entry.path(), std::ios::binary);
		const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		carriageReturn = carriageReturn || bytes.find('\r') != std::string::npos;
	}
	// The imported scene files contain no carriage return.
	QVERIFY(imported.success());
	QVERIFY(!carriageReturn);

	writeText(legacy / "Trains/Service.txt", "Service inf 99999999 0 /Service.dat /Service-effort.dat /TimeTable/Service.txt\n");
	const fs::path refused = fs::path(root.dir) / "infinite-scene";
	const auto infinite = importLegacyScene(legacy.string(), refused.string(), "NonFinite");
	// A Trains file with an infinite entry time stops the import and publishes no scene.
	QVERIFY(!infinite.success());
	QVERIFY(hasDiag(infinite.diagnostics, "scene.import.parse", SceneSeverity::Error));
	QVERIFY(!fs::exists(refused));
}

// A file that cannot be opened is reported. A required file stops the import and an optional one is skipped. This
// does not apply to root.
void TestSceneImporter::reportsFilesThatCannotBeOpened() {
#ifdef _WIN32
	QSKIP("POSIX permissions");
#else
	if (geteuid() != 0) {
		TempDir root;
		const fs::path legacy = fs::path(root.dir) / "closed-file";
		writeSmallLegacyCase(legacy);
		const fs::path incidents = legacy / "Incidents.txt";
		writeText(incidents, "signal_failure\t1\t0\t10\n");
		fs::permissions(incidents, fs::perms::none, fs::perm_options::replace);
		const auto optional = importLegacyScene(legacy.string(), (fs::path(root.dir) / "optional").string(), "Closed");
		// An optional file that cannot be opened is reported and skipped.
		QVERIFY(optional.success());
		QVERIFY(hasReadDiag(optional.diagnostics, incidents, SceneSeverity::Warning));
		const fs::path stations = legacy / "TrackLines/Stations.txt";
		fs::permissions(stations, fs::perms::none, fs::perm_options::replace);
		const fs::path refused = fs::path(root.dir) / "required";
		const auto required = importLegacyScene(legacy.string(), refused.string(), "Closed");
		// A station file that cannot be opened stops the import.
		QVERIFY(!required.success());
		QVERIFY(hasReadDiag(required.diagnostics, stations, SceneSeverity::Error));
		QVERIFY(!fs::exists(refused));
	}
#endif
}

// A case under a folder that cannot be read gives a diagnostic and does not throw. This does not apply to root.
void TestSceneImporter::reportsFolderThatCannotBeRead() {
#ifdef _WIN32
	QSKIP("POSIX permissions");
#else
	TempDir root;
	const fs::path closed = fs::path(root.dir) / "closed";
	fs::create_directories(closed / "case");
	fs::permissions(closed, fs::perms::none, fs::perm_options::replace);
	const auto restore = qScopeGuard([&closed] { fs::permissions(closed, fs::perms::owner_all, fs::perm_options::replace); });
	if (geteuid() != 0) {
		const std::string legacy = (closed / "case").string();
		QVERIFY2(hasDiag(importLegacyScene(legacy, (fs::path(root.dir) / "scene").string(), "Closed").diagnostics,
					 "scene.import.missing", SceneSeverity::Error),
			"Import from a folder that cannot be read reports it");
		QVERIFY2(hasDiag(importLegacyPassengers(legacy, SceneModel()).diagnostics, "scene.import.passengers.missing",
					 SceneSeverity::Error),
			"Passenger import from a folder that cannot be read reports the missing files");
	}
#endif
}

// Malformed incidents and entrance delays, read from scenarios.json and from the legacy incidents.json. The directory
// holds only that file, so the loader reports the other scene files as missing under their own names.
void TestSceneImporter::reportsMalformedScenarioItems() {
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
	// Malformed incident and entrance delay fields are reported in schema order.
	QCOMPARE(errorsIn(loaded.diagnostics, "scenarios.json"), expected);
	// Malformed incident fields keep their defaults and the items are still read.
	QCOMPARE(loaded.scene.scenarios.size(), std::size_t(1));
	QCOMPARE(loaded.scene.scenarios[0].incidents.size(), std::size_t(2));
	QCOMPARE(loaded.scene.scenarios[0].incidents[0].id, "i");
	QVERIFY(!loaded.scene.scenarios[0].incidents[0].hasEndSeconds);
	QVERIFY(!loaded.scene.scenarios[0].incidents[0].hasOccurrence);
	QCOMPARE(loaded.scene.scenarios[0].incidents[0].occurrence, 1);
	QVERIFY(!loaded.scene.scenarios[0].incidents[0].hasReducedSpeed);
	QVERIFY(!loaded.scene.scenarios[0].incidents[0].terminateAtDestination);
	QCOMPARE(loaded.scene.scenarios[0].entranceDelays.size(), std::size_t(2));

	// An item that is not an object is reported once by every field that is read from it.
	expected.assign(7, notObject + "scenarios[0].incidents[0]: Array item must be an object");
	expected.insert(expected.end(), 4, notObject + "scenarios[0].entrance_delays[0]: Array item must be an object");
	loaded = load("scenarios.json", scenarioFile(json::array({7}), json::array({7})));
	// An incident and an entrance delay that are not objects are reported once per field.
	QCOMPARE(errorsIn(loaded.diagnostics, "scenarios.json"), expected);

	loaded = load("scenarios.json", scenarioFile("x", "x"));
	// Incident and entrance delay sections that are not arrays are reported.
	QCOMPARE(errorsIn(loaded.diagnostics, "scenarios.json"),
		std::vector<std::string>({"scene.section.missing scenarios[0].incidents: Mistyped incidents section",
			"scene.section.missing scenarios[0].entrance_delays: Mistyped entrance_delays section"}));
	loaded = load("scenarios.json",
		json({{"default_scenario_id", "s"}, {"scenarios", json::array({json({{"id", "s"}, {"name", "n"}})})}}));
	// A missing incidents section is reported and a missing entrance delay section is not.
	QCOMPARE(errorsIn(loaded.diagnostics, "scenarios.json"),
		std::vector<std::string>{"scene.section.missing scenarios[0].incidents: Missing incidents section"});

	expected.clear();
	append(expected, fieldErrors("incidents[0]", "Invalid", wrongTypeKeys));
	expected.insert(expected.end(), 7, notObject + "incidents[1]: Array item must be an object");
	loaded = load("incidents.json", json({{"incidents", json::array({wrongTypes, 7})}}));
	// Malformed incidents of the legacy incidents file are reported with root paths.
	QCOMPARE(errorsIn(loaded.diagnostics, "incidents.json"), expected);
	loaded = load("incidents.json", json({{"incidents", "x"}}));
	// A legacy incidents section that is not an array is reported.
	QCOMPARE(errorsIn(loaded.diagnostics, "incidents.json"),
		std::vector<std::string>{"scene.section.missing incidents: Mistyped incidents section"});
	loaded = load("incidents.json", json::object());
	// A legacy incidents file without a section is reported.
	QCOMPARE(errorsIn(loaded.diagnostics, "incidents.json"),
		std::vector<std::string>{"scene.section.missing incidents: Missing incidents section"});
}

// Malformed passengers, read from passengers.json. The directory holds only that file, so the loader reports the other
// scene files as missing under their own names.
void TestSceneImporter::reportsMalformedPassengers() {
	const auto load = [](const json& content) {
		TempDir directory;
		writeText(fs::path(directory.dir) / "passengers.json", content.dump());
		return loadScene(directory.dir);
	};
	const std::string results = "scene.passengers.results ";
	const std::string resultMessage = ": Passenger input must not contain simulation-result fields";
	const std::string notObject = "scene.item.invalid ";
	const std::string journeys = "passengers[0].journeys[0]";
	const std::string leg = journeys + ".legs[0]";

	// Every field has the wrong type. A simulation result is reported before the fields of its item.
	const json wrongLeg = {{"actual_arrival", 1}, {"id", 1}, {"origin", 2}, {"destination", 3}, {"service", 4},
		{"occurrence", 1.5}};
	const json wrongJourney = {{"delay_seconds", 1}, {"id", 1}, {"activity", 2}, {"origin", 3}, {"destination", 4},
		{"planned_departure", 5}, {"planned_arrival", {{"start_seconds", "a"}, {"end_seconds", "b"}}},
		{"legs", json::array({wrongLeg})}};
	const json wrongPassenger = {{"id", 5}, {"results", json::object()}, {"journeys", json::array({wrongJourney})}};
	std::vector<std::string> expected = {
		"scene.field.missing passengers[0].id: Invalid id",
		results + "passengers[0].results" + resultMessage,
		results + journeys + ".delay_seconds" + resultMessage,
		"scene.field.missing " + journeys + ".id: Invalid id",
		"scene.field.missing " + journeys + ".activity: Invalid activity",
		"scene.field.missing " + journeys + ".origin: Invalid origin",
		"scene.field.missing " + journeys + ".destination: Invalid destination",
		"scene.field.missing " + journeys + ".planned_departure: Missing planned_departure window",
		"scene.field.missing " + journeys + ".planned_arrival.start_seconds: Invalid start_seconds",
		"scene.field.missing " + journeys + ".planned_arrival.end_seconds: Invalid end_seconds",
		results + leg + ".actual_arrival" + resultMessage,
		"scene.field.missing " + leg + ".id: Invalid id",
		"scene.field.missing " + leg + ".origin: Invalid origin",
		"scene.field.missing " + leg + ".destination: Invalid destination",
		"scene.field.missing " + leg + ".service: Invalid service",
		"scene.field.missing " + leg + ".occurrence: Invalid occurrence",
	};
	SceneLoadResult loaded = load({{"passengers", json::array({wrongPassenger})}});
	// Malformed passenger fields are reported in schema order.
	QCOMPARE(errorsIn(loaded.diagnostics, "passengers.json"), expected);
	// Malformed passenger fields keep their defaults and the items are still read.
	QCOMPARE(loaded.scene.passengers.size(), std::size_t(1));
	QCOMPARE(loaded.scene.passengers[0].journeys.size(), std::size_t(1));
	QCOMPARE(loaded.scene.passengers[0].journeys[0].legs.size(), std::size_t(1));
	QCOMPARE(loaded.scene.passengers[0].journeys[0].legs[0].occurrence, 1);

	// Absent fields. A leg without an occurrence is not an error.
	loaded = load({{"passengers", json::array({json::object(), {{"id", "p"}, {"journeys", json::array({json::object()})}}, {{"id", "q"}, {"journeys", json::array({{{"legs", json::array({json::object()})}}})}}})}});
	expected = {
		"scene.field.missing passengers[0].id: Missing id",
		"scene.section.missing passengers[0].journeys: Missing journeys section",
		"scene.field.missing passengers[1].journeys[0].id: Missing id",
		"scene.field.missing passengers[1].journeys[0].origin: Missing origin",
		"scene.field.missing passengers[1].journeys[0].destination: Missing destination",
		"scene.field.missing passengers[1].journeys[0].planned_departure: Missing planned_departure window",
		"scene.field.missing passengers[1].journeys[0].planned_arrival: Missing planned_arrival window",
		"scene.section.missing passengers[1].journeys[0].legs: Missing legs section",
		"scene.field.missing passengers[2].journeys[0].id: Missing id",
		"scene.field.missing passengers[2].journeys[0].origin: Missing origin",
		"scene.field.missing passengers[2].journeys[0].destination: Missing destination",
		"scene.field.missing passengers[2].journeys[0].planned_departure: Missing planned_departure window",
		"scene.field.missing passengers[2].journeys[0].planned_arrival: Missing planned_arrival window",
		"scene.field.missing passengers[2].journeys[0].legs[0].id: Missing id",
		"scene.field.missing passengers[2].journeys[0].legs[0].origin: Missing origin",
		"scene.field.missing passengers[2].journeys[0].legs[0].destination: Missing destination",
		"scene.field.missing passengers[2].journeys[0].legs[0].service: Missing service",
	};
	// Absent passenger fields and sections are reported.
	QCOMPARE(errorsIn(loaded.diagnostics, "passengers.json"), expected);

	// An item that is not an object is reported by every field that is read from it.
	loaded = load({{"passengers", json::array({7})}});
	expected = {notObject + "passengers[0]: Array item must be an object", notObject + "passengers[0]: Expected an object"};
	// A passenger that is not an object is reported by its id and its journeys.
	QCOMPARE(errorsIn(loaded.diagnostics, "passengers.json"), expected);
	loaded = load({{"passengers", json::array({{{"id", "p"}, {"journeys", json::array({7})}}})}});
	expected.assign(4, notObject + journeys + ": Array item must be an object");
	expected.push_back("scene.field.missing " + journeys + ".planned_departure: Missing planned_departure window");
	expected.push_back("scene.field.missing " + journeys + ".planned_arrival: Missing planned_arrival window");
	expected.push_back(notObject + journeys + ": Expected an object");
	// A journey that is not an object is reported by its fields, its windows and its legs.
	QCOMPARE(errorsIn(loaded.diagnostics, "passengers.json"), expected);
	loaded = load({{"passengers", json::array({{{"id", "p"}, {"journeys", json::array({{{"id", "j"}, {"origin", "a"}, {"destination", "b"}, {"planned_departure", json::object()}, {"planned_arrival", json::object()}, {"legs", json::array({7})}}})}}})}});
	expected = {"scene.field.missing " + journeys + ".planned_departure.start_seconds: Missing start_seconds",
		"scene.field.missing " + journeys + ".planned_departure.end_seconds: Missing end_seconds",
		"scene.field.missing " + journeys + ".planned_arrival.start_seconds: Missing start_seconds",
		"scene.field.missing " + journeys + ".planned_arrival.end_seconds: Missing end_seconds"};
	expected.insert(expected.end(), 5, notObject + leg + ": Array item must be an object");
	// Empty windows and a leg that is not an object are reported by their fields.
	QCOMPARE(errorsIn(loaded.diagnostics, "passengers.json"), expected);

	// Sections.
	loaded = load({{"passengers", "x"}});
	// A passengers section that is not an array is reported.
	QCOMPARE(errorsIn(loaded.diagnostics, "passengers.json"),
		std::vector<std::string>{"scene.section.missing passengers: Mistyped passengers section"});
	loaded = load(json::object());
	// A passengers file without a section is reported.
	QCOMPARE(errorsIn(loaded.diagnostics, "passengers.json"),
		std::vector<std::string>{"scene.section.missing passengers: Missing passengers section"});
	loaded = load({{"passengers", json::array({{{"id", "p"}, {"journeys", "x"}}})}});
	// A journeys section that is not an array is reported.
	QCOMPARE(errorsIn(loaded.diagnostics, "passengers.json"),
		std::vector<std::string>{"scene.section.missing passengers[0].journeys: Mistyped journeys section"});
}

QTEST_APPLESS_MAIN(TestSceneImporter)
#include "test_sceneimporter.moc"
