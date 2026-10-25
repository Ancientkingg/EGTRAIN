#include "scene/SceneExporter.h"
#include "scene/SceneImporter.h"
#include "scene/SceneModel.h"
#include <cctype>
#include <algorithm>
#include <iostream>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <map>

#include <QTest>

namespace fs = std::filesystem;

static bool hasDiag(const std::vector<SceneDiagnostic>& diags, const std::string& code, SceneSeverity sev = SceneSeverity::Error) {
	for (const auto& d : diags) {
		if (d.code == code && d.severity == sev)
			return true;
	}
	return false;
}

struct TempDir {
	std::string dir;
	TempDir() {
		static int counter = 0;
		auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		fs::path temp = fs::temp_directory_path() / ("scene_exporter_test_" + std::to_string(stamp) + "_" + std::to_string(counter++));
		fs::create_directories(temp);
		dir = temp.string();
	}
	~TempDir() {
		std::error_code ec;
		fs::remove_all(dir, ec);
	}
};

// The file as text, so that the line ends of the platform read as "\n".
static std::string readText(const fs::path& path) {
	std::ifstream in(path);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The regular files below root and their bytes, by relative path.
static std::map<std::string, std::string> snapshot(const fs::path& root) {
	std::map<std::string, std::string> files;
	for (const auto& entry : fs::recursive_directory_iterator(root)) {
		if (!entry.is_regular_file()) continue;
		std::ifstream input(entry.path(), std::ios::binary);
		files.emplace(entry.path().lexically_relative(root).string(), std::string(std::istreambuf_iterator<char>(input), {}));
	}
	return files;
}

// A scene with the tracks "main", which becomes legacy track B0, and "B7", and two nodes on "main". The argument is appended to signalling.json.
static void writeAreaScene(const fs::path& scene, const std::string& signallingAreas) {
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"Area Export"})" << "\n";
	std::ofstream(scene / "infrastructure.json")
		<< R"({"tracks":[{"id":"main"},{"id":"B7"}],"nodes":[{"id":"n0","track":"main","x_km":0,"y_km":0},{"id":"n1","track":"main","x_km":1,"y_km":0}],"arcs":[],"blocks":[{"id":"block.a","track":"main","length_km":1},{"id":"block.b","track":"B7","length_km":1}]})"
		<< "\n";
	std::ofstream(scene / "stations.json") << R"({"stations":[]})" << "\n";
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[{"id":"route0","blocks":["block.a"]}])" << signallingAreas << "}\n";
	std::ofstream(scene / "rolling_stock.json") << R"({"train_units":[],"compositions":[]})" << "\n";
	std::ofstream(scene / "services.json") << R"({"services":[]})" << "\n";
}

// Japanese and Thai characters, as UTF-8. No Windows code page holds both.
static const std::string kUnicodeName = "scene-\xE6\x97\xA5\xE6\x9C\xAC-\xE0\xB8\x81";

static void printErrors(const std::vector<SceneDiagnostic>& diags, const char* label) {
	for (const auto& d : diags) {
		if (d.severity == SceneSeverity::Error)
			std::cerr << label << ": " << toDisplayText(d) << "\n";
	}
}

class TestSceneExporter : public QObject {
	Q_OBJECT

private slots:
	void minimalFixtureExportFails();
	void keepsSlashBlockReference();
	void leavesOutServiceColour();
	void numbersNonAsciiRouteIds();
	void missingSceneExportFails();
	void combinesMultiUnitComposition();
	void rejectsDegenerateTractionBands();
	void mergesNearDuplicateBandBoundaries();
	void synthesizesGuiLayout();
	void writesNoStationsCoordWithoutStations();
	void mapsMirroredBandByName();
	void writesNoGuiFilesWithoutNodes();
	void exportsIncidents();
	void writesNoIncidentsFileWithoutIncidents();
	void exportsCanonicalInfrastructure();
	void mapsBlockAndTrackIds();
	void keepsSignallingAreasInSidecar();
	void refusesNoncanonicalTrackMapping_data();
	void refusesNoncanonicalTrackMapping();
	void rejectsOverlappingOutput_data();
	void rejectsOverlappingOutput();
	void exportsBesideScene();
	void reExportReplacesRouteFiles();
	void reExportWritesCurrentScene();
	void reExportRemovesStaleImporterInputs();
	void exportsUnderUnicodeNames();
	void exportsLayoutAndSettingsFromViews();
};

// The minimal fixture scene does not export.
void TestSceneExporter::minimalFixtureExportFails() {
	TempDir outDir;
	const std::string fixture = (fs::path(EGTRAIN_TEST_SOURCE_DIR) / "fixtures/scenes/minimal").string();
	auto res = exportLegacyScene(fixture, outDir.dir);
	QVERIFY2(!res.success(), "Minimal scene export fails");
	QVERIFY2(hasDiag(res.diagnostics, "scene.export.ref"), "scene.export.ref produced");
}

// Legacy export preserves a slash-containing stored route token as one block reference.
void TestSceneExporter::keepsSlashBlockReference() {
	TempDir sceneDir, outDir;
	fs::path scene(sceneDir.dir);
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"Slash Block"})" << "\n";
	std::ofstream(scene / "infrastructure.json") << R"({"nodes":[],"arcs":[]})" << "\n";
	std::ofstream(scene / "stations.json") << R"({"stations":[{"id":"st","name":"Station","platforms":[]}]})" << "\n";
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[{"id":"route0","blocks":["Depot/1"]}]})" << "\n";
	std::ofstream(scene / "rolling_stock.json")
		<< R"({"train_units":[{"id":"unit","physical":{"mass_of_traction_unit_kg":1,"mass_of_a_wagon_kg":1,"number_of_wagons":0,"max_speed_ms":1,"max_deceleration_ms2":1,"frontal_area_m2":1,"resistance_coefficient":1,"jerk_ms3":1,"length_m":1},"traction_curve":[[0,1,1,0,0]]}],"compositions":[{"id":"comp","units":["unit"]}]})"
		<< "\n";
	std::ofstream(scene / "services.json")
		<< R"({"services":[{"id":"svc","composition":"comp","route":"route0","through":true,"entry_time_seconds":0,"stops":[{"station":"st","departure_seconds":0,"dwell_seconds":0}]},{"id":"empty","composition":"comp","route":"route0","entry_time_seconds":0,"stops":[]}]})"
		<< "\n";

	auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
	printErrors(res.diagnostics, "SlashBlock export");
	QVERIFY2(res.success(), "Slash block scene export succeeds");
	std::ifstream route(fs::path(outDir.dir) / "Routes" / "Route0.txt");
	std::string line;
	std::getline(route, line);
	// Slash block id remains wrapped.
	QCOMPARE(line, "@Depot/1@");
	std::ifstream timetable(fs::path(outDir.dir) / "TimeTable" / "svc.txt");
	std::getline(timetable, line);
	// Nonempty stops export despite historical through flag, retaining absent arrival.
	QCOMPARE(line, "st\t0\t-1\t0");
	// Zero stops export as an empty legacy timetable without a flag.
	QVERIFY(fs::exists(fs::path(outDir.dir) / "TimeTable" / "empty.txt"));
	QCOMPARE(fs::file_size(fs::path(outDir.dir) / "TimeTable" / "empty.txt"), std::uintmax_t(0));
}

// A service display colour has no place in the legacy format and is left out of the export.
void TestSceneExporter::leavesOutServiceColour() {
	TempDir sceneDir, outDir;
	fs::path scene(sceneDir.dir);
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"Service Colour"})" << "\n";
	std::ofstream(scene / "infrastructure.json") << R"({"nodes":[],"arcs":[]})" << "\n";
	std::ofstream(scene / "stations.json") << R"({"stations":[{"id":"st","name":"Station","platforms":[]}]})" << "\n";
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[{"id":"route0","blocks":["Depot/1"]}]})" << "\n";
	std::ofstream(scene / "rolling_stock.json")
		<< R"({"train_units":[{"id":"unit","physical":{"mass_of_traction_unit_kg":1,"mass_of_a_wagon_kg":1,"number_of_wagons":0,"max_speed_ms":1,"max_deceleration_ms2":1,"frontal_area_m2":1,"resistance_coefficient":1,"jerk_ms3":1,"length_m":1},"traction_curve":[[0,1,1,0,0]]}],"compositions":[{"id":"comp","units":["unit"]}]})"
		<< "\n";
	std::ofstream(scene / "services.json")
		<< R"({"services":[{"id":"svc","composition":"comp","route":"route0","visualization_color":"#3c8dd2","entry_time_seconds":0,"stops":[{"station":"st","departure_seconds":0,"dwell_seconds":0}]}]})"
		<< "\n";

	auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
	printErrors(res.diagnostics, "Service colour export");
	QVERIFY2(res.success(), "export of a service with a colour succeeds");
	QVERIFY2(fs::exists(fs::path(outDir.dir) / "TimeTable" / "svc.txt"), "service with a colour is exported");
	bool leaked = false;
	for (const auto& entry : fs::recursive_directory_iterator(outDir.dir)) {
		if (!entry.is_regular_file())
			continue;
		std::ifstream input(entry.path(), std::ios::binary);
		std::stringstream content;
		content << input.rdbuf();
		std::string text = content.str();
		for (char& character : text)
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		leaked |= text.find("3c8dd2") != std::string::npos || text.find("visualization") != std::string::npos;
	}
	QVERIFY2(!leaked, "legacy export does not contain the service colour");
}

// Non-ASCII route ids are not treated as decimal route numbers.
void TestSceneExporter::numbersNonAsciiRouteIds() {
	TempDir sceneDir, outDir;
	fs::path scene(sceneDir.dir);
	const std::string nonAsciiRouteId = std::string("route") + "\xC3\xA9";
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"Non-ASCII Route Id"})" << "\n";
	std::ofstream(scene / "infrastructure.json") << R"({"nodes":[],"arcs":[]})" << "\n";
	std::ofstream(scene / "stations.json") << R"({"stations":[{"id":"st","name":"Station","platforms":[]}]})" << "\n";
	std::ofstream signalling(scene / "signalling.json");
	signalling << R"({"signals":[],"routes":[{"id":")" << nonAsciiRouteId << R"(","blocks":["ASCII"]}]})" << "\n";
	signalling.close();
	std::ofstream(scene / "rolling_stock.json")
		<< R"({"train_units":[{"id":"unit","physical":{"mass_of_traction_unit_kg":1,"mass_of_a_wagon_kg":1,"number_of_wagons":0,"max_speed_ms":1,"max_deceleration_ms2":1,"frontal_area_m2":1,"resistance_coefficient":1,"jerk_ms3":1,"length_m":1},"traction_curve":[[0,1,1,0,0]]}],"compositions":[{"id":"comp","units":["unit"]}]})"
		<< "\n";
	std::ofstream services(scene / "services.json");
	services << R"({"services":[{"id":"svc","composition":"comp","route":")" << nonAsciiRouteId
			 << R"(","entry_time_seconds":0,"stops":[{"station":"st","departure_seconds":0,"dwell_seconds":0}]}]})" << "\n";
	services.close();

	auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
	printErrors(res.diagnostics, "Non-ASCII route id export");
	QVERIFY2(res.success(), "Non-ASCII route id export succeeds");

	std::ifstream route(fs::path(outDir.dir) / "Routes" / "Route0.txt");
	std::string line;
	QVERIFY2(route.good(), "Non-ASCII route id uses sequential ASCII filename");
	std::getline(route, line);
	// Non-ASCII route id contents remain unchanged.
	QCOMPARE(line, "@ASCII@");
}

// A scene directory that does not exist does not export.
void TestSceneExporter::missingSceneExportFails() {
	TempDir outDir;
	auto res = exportLegacyScene("/no/such/scene", outDir.dir);
	QVERIFY2(!res.success(), "Nonexistent scene export fails");
}

// A composition of two units exports as one train.
void TestSceneExporter::combinesMultiUnitComposition() {
	TempDir sceneDir, outDir;
	fs::path scene(sceneDir.dir);
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"Multi Unit"})" << "\n";
	std::ofstream(scene / "infrastructure.json") << R"({"nodes":[],"arcs":[]})" << "\n";
	std::ofstream(scene / "stations.json") << R"({"stations":[{"id":"st","name":"Station","platforms":[]}]})" << "\n";
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[{"id":"route0","blocks":["Depot/1"]}]})" << "\n";
	std::ofstream(scene / "rolling_stock.json")
		<< R"({"train_units":[{"id":"unit1","physical":{"mass_of_traction_unit_kg":100,"mass_of_a_wagon_kg":50,"number_of_wagons":2,"max_speed_ms":80,"max_deceleration_ms2":1,"frontal_area_m2":10,"resistance_coefficient":0.01,"jerk_ms3":0.5,"length_m":50},"traction_curve":[[0,10,1,0,0],[10,20,1,0,0]]},{"id":"unit2","physical":{"mass_of_traction_unit_kg":200,"mass_of_a_wagon_kg":30,"number_of_wagons":1,"max_speed_ms":60,"max_deceleration_ms2":2,"frontal_area_m2":12,"resistance_coefficient":0.02,"jerk_ms3":0.8,"length_m":40},"traction_curve":[[5,15,0,1,0],[15,25,0,1,0]]}],"compositions":[{"id":"compMulti","units":["unit1","unit2"]}]})"
		<< "\n";
	std::ofstream(scene / "services.json")
		<< R"({"services":[{"id":"svcMulti","composition":"compMulti","route":"route0","entry_time_seconds":0,"stops":[{"station":"st","departure_seconds":0,"dwell_seconds":0}]}]})"
		<< "\n";

	auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
	printErrors(res.diagnostics, "MultiUnit export");
	QVERIFY2(res.success(), "Multi unit scene export succeeds");

	std::ifstream df(fs::path(outDir.dir) / "TrainData" / "compMulti.txt");
	QVERIFY2(df.good(), "Multi-unit TrainData file exists");
	double tmass = 0, wmass = 0, wcount = 0, maxspd = 0, maxdec = 0, farea = 0, rescoeff = 0, jerk = 0, len = 0;
	df >> tmass >> wmass >> wcount >> maxspd >> maxdec >> farea >> rescoeff >> jerk >> len;
	// Summed traction mass is 300.
	QCOMPARE_EQ(tmass, 300.0);
	// Summed wagon count is 3.
	QCOMPARE_EQ(wcount, 3.0);
	// Summed length is 90.
	QCOMPARE_EQ(len, 90.0);
	// Minimum max speed is 60.
	QCOMPARE_EQ(maxspd, 60.0);

	std::ifstream tf(fs::path(outDir.dir) / "TrainData" / "T_compMulti.txt");
	QVERIFY2(tf.good(), "Multi-unit traction file exists");
	double a = 0, b = 0, c0 = 0, c1 = 0, c2 = 0;
	tf >> a >> b >> c0 >> c1 >> c2;
	// Band 0-5 correct.
	QCOMPARE_EQ(a, 0.0);
	QCOMPARE_EQ(b, 5.0);
	QCOMPARE_EQ(c0, 1.0);
	QCOMPARE_EQ(c1, 0.0);
	QCOMPARE_EQ(c2, 0.0);
	tf >> a >> b >> c0 >> c1 >> c2;
	// Band 5-10 correct.
	QCOMPARE_EQ(a, 5.0);
	QCOMPARE_EQ(b, 10.0);
	QCOMPARE_EQ(c0, 1.0);
	QCOMPARE_EQ(c1, 1.0);
	QCOMPARE_EQ(c2, 0.0);
	tf >> a >> b >> c0 >> c1 >> c2;
	// Band 10-15 correct.
	QCOMPARE_EQ(a, 10.0);
	QCOMPARE_EQ(b, 15.0);
	QCOMPARE_EQ(c0, 1.0);
	QCOMPARE_EQ(c1, 1.0);
	QCOMPARE_EQ(c2, 0.0);
	tf >> a >> b >> c0 >> c1 >> c2;
	// Band 15-20 correct.
	QCOMPARE_EQ(a, 15.0);
	QCOMPARE_EQ(b, 20.0);
	QCOMPARE_EQ(c0, 1.0);
	QCOMPARE_EQ(c1, 1.0);
	QCOMPARE_EQ(c2, 0.0);
	tf >> a >> b >> c0 >> c1 >> c2;
	// Band 20-25 correct.
	QCOMPARE_EQ(a, 20.0);
	QCOMPARE_EQ(b, 25.0);
	QCOMPARE_EQ(c0, 0.0);
	QCOMPARE_EQ(c1, 1.0);
	QCOMPARE_EQ(c2, 0.0);
}

// A composition of two units with only degenerate traction bands does not export.
void TestSceneExporter::rejectsDegenerateTractionBands() {
	TempDir sceneDir, outDir;
	fs::path scene(sceneDir.dir);
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"Degenerate"})" << "\n";
	std::ofstream(scene / "infrastructure.json") << R"({"nodes":[],"arcs":[]})" << "\n";
	std::ofstream(scene / "stations.json") << R"({"stations":[{"id":"st","name":"Station","platforms":[]}]})" << "\n";
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[{"id":"route0","blocks":["Depot/1"]}]})" << "\n";
	std::ofstream(scene / "rolling_stock.json")
		<< R"({"train_units":[{"id":"unit1","physical":{"mass_of_traction_unit_kg":100,"mass_of_a_wagon_kg":50,"number_of_wagons":2,"max_speed_ms":80,"max_deceleration_ms2":1,"frontal_area_m2":10,"resistance_coefficient":0.01,"jerk_ms3":0.5,"length_m":50},"traction_curve":[[10,10,1,0,0]]},{"id":"unit2","physical":{"mass_of_traction_unit_kg":200,"mass_of_a_wagon_kg":30,"number_of_wagons":1,"max_speed_ms":60,"max_deceleration_ms2":2,"frontal_area_m2":12,"resistance_coefficient":0.02,"jerk_ms3":0.8,"length_m":40},"traction_curve":[[20,20,1,0,0]]}],"compositions":[{"id":"compDegen","units":["unit1","unit2"]}]})"
		<< "\n";
	std::ofstream(scene / "services.json")
		<< R"({"services":[{"id":"svcDegen","composition":"compDegen","route":"route0","entry_time_seconds":0,"stops":[{"station":"st","departure_seconds":0,"dwell_seconds":0}]}]})"
		<< "\n";

	auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
	QVERIFY2(!res.success(), "Degenerate multi-unit traction export fails");
	bool foundEmptyCurveDiag = false;
	for (const auto& d : res.diagnostics) {
		if (d.severity == SceneSeverity::Error && d.message.find("Combined traction curve is empty") != std::string::npos)
			foundEmptyCurveDiag = true;
	}
	QVERIFY2(foundEmptyCurveDiag, "Empty combined traction curve is diagnosed");
}

// Near-duplicate band boundaries collapse instead of forming sliver bands.
void TestSceneExporter::mergesNearDuplicateBandBoundaries() {
	TempDir sceneDir, outDir;
	fs::path scene(sceneDir.dir);
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"Ulp"})" << "\n";
	std::ofstream(scene / "infrastructure.json") << R"({"nodes":[],"arcs":[]})" << "\n";
	std::ofstream(scene / "stations.json") << R"({"stations":[{"id":"st","name":"Station","platforms":[]}]})" << "\n";
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[{"id":"route0","blocks":["Depot/1"]}]})" << "\n";
	std::ofstream(scene / "rolling_stock.json")
		<< R"({"train_units":[{"id":"unit1","physical":{"mass_of_traction_unit_kg":100,"mass_of_a_wagon_kg":50,"number_of_wagons":2,"max_speed_ms":80,"max_deceleration_ms2":1,"frontal_area_m2":10,"resistance_coefficient":0.01,"jerk_ms3":0.5,"length_m":50},"traction_curve":[[0,10,1,0,0]]},{"id":"unit2","physical":{"mass_of_traction_unit_kg":200,"mass_of_a_wagon_kg":30,"number_of_wagons":1,"max_speed_ms":60,"max_deceleration_ms2":2,"frontal_area_m2":12,"resistance_coefficient":0.02,"jerk_ms3":0.8,"length_m":40},"traction_curve":[[0,10.000000000000002,2,0,0]]}],"compositions":[{"id":"compUlp","units":["unit1","unit2"]}]})"
		<< "\n";
	std::ofstream(scene / "services.json")
		<< R"({"services":[{"id":"svcUlp","composition":"compUlp","route":"route0","entry_time_seconds":0,"stops":[{"station":"st","departure_seconds":0,"dwell_seconds":0}]}]})"
		<< "\n";

	auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
	printErrors(res.diagnostics, "Ulp export");
	QVERIFY2(res.success(), "Near-duplicate boundary scene export succeeds");

	std::ifstream tf(fs::path(outDir.dir) / "TrainData" / "T_compUlp.txt");
	QVERIFY2(tf.good(), "Ulp traction file exists");
	std::string first, second;
	std::getline(tf, first);
	std::getline(tf, second);
	double a = 0, b = 0, c0 = 0, c1 = 0, c2 = 0;
	std::istringstream(first) >> a >> b >> c0 >> c1 >> c2;
	// Single merged band sums both curves.
	QCOMPARE_EQ(a, 0.0);
	QCOMPARE_EQ(c0, 3.0);
	QVERIFY2(second.empty(), "No sliver band is emitted");
}

// A GUI layout is generated from Stations.txt and NodiCumPari.txt.
void TestSceneExporter::synthesizesGuiLayout() {
	TempDir sceneDir, outDir;
	fs::path scene(sceneDir.dir);
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"GUI Synthesis"})" << "\n";
	std::ofstream(scene / "infrastructure.json") << R"({"nodes":[],"arcs":[]})" << "\n";
	std::ofstream(scene / "stations.json") << R"({"stations":[]})" << "\n";
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[]})" << "\n";
	std::ofstream(scene / "rolling_stock.json") << R"({"train_units":[],"compositions":[]})" << "\n";
	std::ofstream(scene / "services.json") << R"({"services":[]})" << "\n";

	fs::create_directories(scene / "legacy" / "TrackLines" / "B0");
	fs::create_directories(scene / "legacy" / "TrackLines" / "B1");

	std::ofstream(scene / "legacy" / "TrackLines" / "Stations.txt")
		<< "0\tA\n"
		<< "28\tB\n"
		<< "64\tC\n"
		<< "100\tC\n"
		<< "136\tB\n"
		<< "164\tA\n";

	std::ofstream(scene / "legacy" / "TrackLines" / "B0" / "NodiCumPari.txt")
		<< "0\t0\t0\n"
		<< "1\t28\t0\n"
		<< "2\t64\t0\n";

	std::ofstream(scene / "legacy" / "TrackLines" / "B1" / "NodiCumPari.txt")
		<< "0\t100\t0\n"
		<< "1\t136\t0\n"
		<< "2\t164\t0\n";

	auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
	QVERIFY2(res.success(), "GUI Synthesis scene export succeeds");

	fs::path guiDir = fs::path(outDir.dir) / "GUI";
	QVERIFY2(fs::exists(guiDir / "StationsCoord.txt"), "GUI/StationsCoord.txt exists");

	std::ifstream sc(guiDir / "StationsCoord.txt");
	if (sc) {
		std::vector<std::string> lines;
		std::string line;
		while (std::getline(sc, line)) {
			if (!line.empty()) lines.push_back(line);
		}
		// GUI/StationsCoord.txt has 6 lines.
		QCOMPARE(lines.size(), std::size_t(6));
		// Line 1 (A at 0) correct.
		QCOMPARE(lines[0], "A\t1\t0\t0\t0");
		// Line 4 (C at 100) correct.
		QCOMPARE(lines[3], "C\t1\t0.64000000000000001\t1\t100");
	}

	std::ifstream td(guiDir / "caseStudyTrackData.txt");
	if (td) {
		std::vector<std::string> lines;
		std::string line;
		while (std::getline(td, line)) {
			if (!line.empty()) lines.push_back(line);
		}
		// GUI/caseStudyTrackData.txt has 2 lines.
		QCOMPARE(lines.size(), std::size_t(2));
		// Line 1 correct.
		QCOMPARE(lines[0], "0\t0\t0");
		// Line 2 correct.
		QCOMPARE(lines[1], "1\t1\t1");
	}
}

// A scene with no legacy/TrackLines/Stations.txt exports and writes no GUI/StationsCoord.txt.
void TestSceneExporter::writesNoStationsCoordWithoutStations() {
	TempDir sceneDir, outDir;
	fs::path scene(sceneDir.dir);
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"No Stations"})" << "\n";
	std::ofstream(scene / "infrastructure.json") << R"({"nodes":[],"arcs":[]})" << "\n";
	std::ofstream(scene / "stations.json") << R"({"stations":[]})" << "\n";
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[]})" << "\n";
	std::ofstream(scene / "rolling_stock.json") << R"({"train_units":[],"compositions":[]})" << "\n";
	std::ofstream(scene / "services.json") << R"({"services":[]})" << "\n";

	auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
	QVERIFY2(res.success(), "No Stations scene export succeeds");

	fs::path guiDir = fs::path(outDir.dir) / "GUI";
	QVERIFY2(!fs::exists(guiDir / "StationsCoord.txt"), "GUI/StationsCoord.txt is not written");
}

// A mirrored band listed before the reference trackline still maps by name.
void TestSceneExporter::mapsMirroredBandByName() {
	TempDir sceneDir, outDir;
	fs::path scene(sceneDir.dir);
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"GUI Order"})" << "\n";
	std::ofstream(scene / "infrastructure.json") << R"({"nodes":[],"arcs":[]})" << "\n";
	std::ofstream(scene / "stations.json") << R"({"stations":[]})" << "\n";
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[]})" << "\n";
	std::ofstream(scene / "rolling_stock.json") << R"({"train_units":[],"compositions":[]})" << "\n";
	std::ofstream(scene / "services.json") << R"({"services":[]})" << "\n";

	fs::create_directories(scene / "legacy" / "TrackLines" / "B0");
	fs::create_directories(scene / "legacy" / "TrackLines" / "B1");

	std::ofstream(scene / "legacy" / "TrackLines" / "Stations.txt")
		<< "100\tC\n"
		<< "136\tB\n"
		<< "164\tA\n"
		<< "0\tA\n"
		<< "28\tB\n"
		<< "64\tC\n";

	std::ofstream(scene / "legacy" / "TrackLines" / "B0" / "NodiCumPari.txt")
		<< "0\t0\t0\n"
		<< "1\t28\t0\n"
		<< "2\t64\t0\n";

	std::ofstream(scene / "legacy" / "TrackLines" / "B1" / "NodiCumPari.txt")
		<< "0\t100\t0\n"
		<< "1\t136\t0\n"
		<< "2\t164\t0\n";

	auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
	QVERIFY2(res.success(), "GUI Order scene export succeeds");

	std::ifstream sc(fs::path(outDir.dir) / "GUI" / "StationsCoord.txt");
	QVERIFY2(sc.good(), "GUI Order StationsCoord.txt exists");
	std::string line;
	std::getline(sc, line);
	// Mirrored C listed first still maps to lon 0.64.
	QCOMPARE(line, "C\t1\t0.64000000000000001\t1\t100");
}

// Stations without any trackline node data generate no GUI files at all.
void TestSceneExporter::writesNoGuiFilesWithoutNodes() {
	TempDir sceneDir, outDir;
	fs::path scene(sceneDir.dir);
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"No Nodes"})" << "\n";
	std::ofstream(scene / "infrastructure.json") << R"({"nodes":[],"arcs":[]})" << "\n";
	std::ofstream(scene / "stations.json") << R"({"stations":[]})" << "\n";
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[]})" << "\n";
	std::ofstream(scene / "rolling_stock.json") << R"({"train_units":[],"compositions":[]})" << "\n";
	std::ofstream(scene / "services.json") << R"({"services":[]})" << "\n";

	fs::create_directories(scene / "legacy" / "TrackLines");
	std::ofstream(scene / "legacy" / "TrackLines" / "Stations.txt") << "0\tA\n";

	auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
	QVERIFY2(res.success(), "No Nodes scene export succeeds");
	QVERIFY2(!fs::exists(fs::path(outDir.dir) / "GUI" / "StationsCoord.txt"), "No half-written StationsCoord.txt");
	QVERIFY2(!fs::exists(fs::path(outDir.dir) / "GUI" / "caseStudyTrackData.txt"), "No half-written caseStudyTrackData.txt");
}

// Incidents export to the legacy Incidents.txt.
void TestSceneExporter::exportsIncidents() {
	TempDir sceneDir, outDir;
	fs::path scene(sceneDir.dir);
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"Incidents"})" << "\n";
	std::ofstream(scene / "infrastructure.json")
		<< R"({"tracks":[{"id":"B0"},{"id":"B1"}],"nodes":[{"id":"n0","track":"B0","x_km":-2,"y_km":0},{"id":"n1","track":"B0","x_km":-1,"y_km":0},{"id":"m0","track":"B1","x_km":0,"y_km":0},{"id":"m1","track":"B1","x_km":1,"y_km":0}],"arcs":[{"id":"a0","track":"B0","from":"n0","to":"n1","curvature_radius_m":0,"gradient_percent":0,"speed_limit_ms":20},{"id":"a1","track":"B1","from":"m0","to":"m1","curvature_radius_m":0,"gradient_percent":0,"speed_limit_ms":20}],"blocks":[{"id":"canonical-block","track":"B0","length_km":1},{"id":"Depot/1","track":"B0","length_km":1},{"id":"canonical-other","track":"B1","length_km":1}],"connections":[{"id":"switch","from":"n1","to":"m0"}]})"
		<< "\n";
	std::ofstream(scene / "stations.json") << R"({"stations":[{"id":"st","name":"Station","platforms":[]}]})" << "\n";
	std::ofstream(scene / "signalling.json") << R"({"signals":[{"id":"sig-x","protected_section":"@canonical-block@"},{"id":"sig-slash","protected_section":"Depot/1"},{"id":"sig-switch","protected_section":"@canonical-block@--1.000000/@canonical-other@-0.000000"},{"id":"sig-malformed","protected_section":"0-B0/1-B0"}],"routes":[{"id":"route0","blocks":["canonical-block","Depot/1","@canonical-block@--1.000000/@canonical-other@-0.000000"]}]})" << "\n";
	std::ofstream(scene / "rolling_stock.json")
		<< R"({"train_units":[{"id":"u1","physical":{"mass_of_traction_unit_kg":100,"mass_of_a_wagon_kg":50,"number_of_wagons":2,"max_speed_ms":80,"max_deceleration_ms2":1,"frontal_area_m2":10,"resistance_coefficient":0.01,"jerk_ms3":0.5,"length_m":50},"traction_curve":[[0,90,1,0,0]]}],"compositions":[{"id":"c1","units":["u1"]}]})"
		<< "\n";
	std::ofstream(scene / "services.json")
		<< R"({"services":[{"id":"svc1","operating_code":"legacy-svc","composition":"c1","route":"route0","entry_time_seconds":0,"stops":[{"station":"st","departure_seconds":0,"dwell_seconds":0}]}]})"
		<< "\n";
	std::ofstream(scene / "scenarios.json")
		<< R"({"default_scenario_id":"baseline","scenarios":[{"id":"baseline","name":"Baseline","incidents":[{"id":"inc1","type":"signal_failure","target":"sig-x","start_seconds":100,"end_seconds":300},{"id":"inc2","type":"signal_failure","target":"canonical-block","start_seconds":301,"end_seconds":302},{"id":"inc-slash","type":"signal_failure","target":"sig-slash","start_seconds":303,"end_seconds":304},{"id":"inc3","type":"signal_failure","target":"sig-switch","start_seconds":400,"end_seconds":500},{"id":"inc4","type":"signal_failure","target":"sig-malformed","start_seconds":501,"end_seconds":600},{"id":"inc5","type":"signal_failure","target":"nope","start_seconds":1,"end_seconds":2},{"id":"inc6","type":"train_breakdown","target":"svc1","start_seconds":25,"end_seconds":35}]},{"id":"alternate","name":"Alternate","incidents":[{"id":"inc7","type":"train_breakdown","target":"svc1","start_seconds":50,"end_seconds":150}]}]})"
		<< "\n";

	auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
	printErrors(res.diagnostics, "Incidents export");
	QVERIFY2(res.success(), "Incidents scene export succeeds");

	std::ifstream inf(fs::path(outDir.dir) / "Incidents.txt");
	QVERIFY2(inf.good(), "Incidents.txt exists");
	std::string l1, l2, l3, l4, l5, l6;
	std::getline(inf, l1);
	std::getline(inf, l2);
	std::getline(inf, l3);
	std::getline(inf, l4);
	std::getline(inf, l5);
	std::getline(inf, l6);
	// Bound signal exports its protected legacy block.
	QCOMPARE(l1, "signal_failure\t0-B0\t100\t300");
	// Direct canonical block target remains supported.
	QCOMPARE(l2, "signal_failure\t0-B0\t301\t302");
	// Legacy export maps the exact slash-containing block reference.
	QCOMPARE(l3, "signal_failure\t1-B0\t303\t304");
	// Unmatched target still written.
	QCOMPARE(l4, "signal_failure\tnope\t1\t2");
	// Breakdown uses the active operating code.
	QCOMPARE(l5, "train_breakdown\tlegacy-svc\t25\t35");
	QVERIFY2(l6.empty(), "Non-default scenario incident is not exported");
	QVERIFY2(hasDiag(res.diagnostics, "scene.export.adjusted", SceneSeverity::Warning), "Unmatched signal target warned");
	QVERIFY2(hasDiag(res.diagnostics, "scene.export.compatibility", SceneSeverity::Warning),
		"Connection-derived signal target reports the legacy format limit");

	std::ofstream(scene / "signalling.json")
		<< R"({"signals":[{"id":"canonical-block","protected_section":"canonical-other"}],"routes":[{"id":"route0","blocks":["canonical-block","canonical-other"]}]})"
		<< "\n";
	std::ofstream(scene / "scenarios.json")
		<< R"({"default_scenario_id":"baseline","scenarios":[{"id":"baseline","name":"Baseline","incidents":[{"id":"inc-ambiguous","type":"signal_failure","target":"canonical-block","start_seconds":1,"end_seconds":2}]}]})"
		<< "\n";
	TempDir ambiguousOut;
	auto ambiguous = exportLegacyScene(sceneDir.dir, ambiguousOut.dir);
	QVERIFY2(!ambiguous.success(), "Ambiguous incident export fails");
	QVERIFY2(hasDiag(ambiguous.diagnostics, "scene.ref.ambiguous"),
		"Ambiguous signal and section target is diagnosed during export");
	std::ifstream ambiguousIncidents(fs::path(ambiguousOut.dir) / "Incidents.txt");
	std::string ambiguousLine;
	std::getline(ambiguousIncidents, ambiguousLine);
	QVERIFY2(ambiguousLine.empty(), "Ambiguous signal failure is not silently retargeted");
}

// A scene without incidents writes no Incidents.txt.
void TestSceneExporter::writesNoIncidentsFileWithoutIncidents() {
	TempDir sceneDir, outDir;
	fs::path scene(sceneDir.dir);
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"No Incidents"})" << "\n";
	std::ofstream(scene / "infrastructure.json") << R"({"nodes":[],"arcs":[]})" << "\n";
	std::ofstream(scene / "stations.json") << R"({"stations":[]})" << "\n";
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[]})" << "\n";
	std::ofstream(scene / "rolling_stock.json") << R"({"train_units":[],"compositions":[]})" << "\n";
	std::ofstream(scene / "services.json") << R"({"services":[]})" << "\n";

	auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
	QVERIFY2(res.success(), "No Incidents scene export succeeds");
	QVERIFY2(!fs::exists(fs::path(outDir.dir) / "Incidents.txt"), "No Incidents.txt written");
}

// Canonical infrastructure exports without legacy passthrough data.
void TestSceneExporter::exportsCanonicalInfrastructure() {
	TempDir sceneDir, outDir;
	fs::path scene(sceneDir.dir);
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"Canonical Infrastructure"})" << "\n";
	std::ofstream(scene / "infrastructure.json")
		<< R"({"tracks":[{"id":"B0"},{"id":"B1"}],"nodes":[{"id":"B0.node.7","track":"B0","x_km":0,"y_km":0},{"id":"B0.node.8","track":"B0","x_km":1,"y_km":0},{"id":"B1.node.3","track":"B1","x_km":10,"y_km":1},{"id":"B1.node.4","track":"B1","x_km":11,"y_km":1}],"arcs":[{"id":"B0.arc.12","track":"B0","from":"B0.node.7","to":"B0.node.8","curvature_radius_m":1000,"gradient_percent":1,"speed_limit_ms":20},{"id":"B1.arc.4","track":"B1","from":"B1.node.3","to":"B1.node.4","curvature_radius_m":2000,"gradient_percent":-1,"speed_limit_ms":25}],"blocks":[{"id":"0-B0","track":"B0","length_km":1},{"id":"0-B1","track":"B1","length_km":1}],"connections":[]})"
		<< "\n";
	std::ofstream(scene / "stations.json")
		<< R"({"stations":[{"id":"origin","name":"Origin","platforms":[{"id":"p0","nodes":["B0.node.7"]}]},{"id":"transfer","name":"Transfer","platforms":[{"id":"p1","nodes":["B0.node.8"]}]},{"id":"destination","name":"Destination","position_km":5,"platforms":[{"id":"p2","nodes":["B1.node.3"]},{"id":"p3","nodes":["B1.node.4"]}]}]})"
		<< "\n";
	std::ofstream(scene / "signalling.json")
		<< R"({"signals":[],"routes":[{"id":"route0","blocks":["0-B0","0-B1"],"corridor":"A"}],"single_track_restrictions":[{"start_block":"0-B0","end_block":"0-B1","protected_start_block":"@0-B0@-1/@0-B1@-2","protected_end_block":"@0-B1@-3/@0-B0@-4"}],"station_boundaries":[{"entrance_block":"0-B0","exit_block":"0-B1","direction":true}]})"
		<< "\n";
	std::ofstream(scene / "rolling_stock.json")
		<< R"({"train_units":[{"id":"unit","physical":{"mass_of_traction_unit_kg":1,"mass_of_a_wagon_kg":1,"number_of_wagons":0,"max_speed_ms":1,"max_deceleration_ms2":1,"frontal_area_m2":1,"resistance_coefficient":1,"jerk_ms3":1,"length_m":1},"traction_curve":[[0,1,1,0,0]]}],"compositions":[{"id":"comp","units":["unit"]}]})"
		<< "\n";
	std::ofstream(scene / "services.json")
		<< R"({"services":[{"id":"svc","operating_code":"svc-code","composition":"comp","route":"route0","entry_time_seconds":0,"stops":[{"station":"origin","planned_departure_seconds":0,"dwell_seconds":0},{"station":"transfer","planned_arrival_seconds":1,"planned_departure_seconds":1,"dwell_seconds":0},{"station":"destination","planned_arrival_seconds":2,"dwell_seconds":0}]}]})"
		<< "\n";
	std::ofstream(scene / "passengers.json")
		<< R"({"passengers":[{"id":"p0","journeys":[{"id":"p0:1","activity":"Work","origin":"origin","destination":"destination","planned_departure":{"start_seconds":0,"end_seconds":1799},"planned_arrival":{"start_seconds":3600,"end_seconds":5399},"legs":[{"id":"p0:1.leg.1","origin":"origin","destination":"transfer","service":"svc","occurrence":1},{"id":"p0:1.leg.2","origin":"transfer","destination":"destination","service":"svc","occurrence":1}]}]}]})"
		<< "\n";

	auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
	printErrors(res.diagnostics, "Canonical infrastructure export");
	QVERIFY2(res.success(), "Canonical infrastructure export succeeds");
	const fs::path tracklines = fs::path(outDir.dir) / "TrackLines";
	QVERIFY2(fs::exists(tracklines / "B0" / "NodiCumPari.txt"), "B0 nodes exported");
	QVERIFY2(fs::exists(tracklines / "B0" / "ArchiCumPari.txt"), "B0 arcs exported");
	QVERIFY2(fs::exists(tracklines / "B0" / "BlockCumPari.txt"), "B0 blocks exported");
	QVERIFY2(fs::exists(tracklines / "B1" / "NodiCumPari.txt"), "B1 nodes exported");
	QVERIFY2(fs::exists(tracklines / "Connections.txt"), "Empty connections file exported");
	// Connections file remains empty.
	QCOMPARE(fs::file_size(tracklines / "Connections.txt"), std::uintmax_t(0));

	std::ifstream stations(tracklines / "Stations.txt");
	std::string first, second, third, fourth, fifth;
	std::getline(stations, first);
	std::getline(stations, second);
	std::getline(stations, third);
	std::getline(stations, fourth);
	std::getline(stations, fifth);
	// First platform anchor exported.
	QCOMPARE(first, "0\tOrigin");
	// Second platform anchor exported.
	QCOMPARE(second, "1\tTransfer");
	// Third platform anchor exported.
	QCOMPARE(third, "10\tDestination");
	// Fourth platform anchor exported.
	QCOMPARE(fourth, "11\tDestination");
	QVERIFY2(fifth.empty(), "Station position fallback is not duplicated");

	std::ifstream corridors(fs::path(outDir.dir) / "GUI" / "caseStudyRouteCorridors.txt");
	std::string line;
	QVERIFY2(fs::exists(fs::path(outDir.dir) / "GUI" / "caseStudyRouteCorridors.txt"), "Route corridor compatibility file exists");
	std::getline(corridors, line);
	// Route corridor compatibility row exported.
	QCOMPARE(line, "0\tA");
	std::ifstream restrictions(fs::path(outDir.dir) / "GUI" / "singleTrackLimits.txt");
	QVERIFY2(fs::exists(fs::path(outDir.dir) / "GUI" / "singleTrackLimits.txt"), "Single-track compatibility file exists");
	std::getline(restrictions, line);
	// Single-track restriction compatibility row exported.
	QCOMPARE(line, "@0-B0@\t@0-B1@\t@0-B0@-1/@0-B1@-2\t@0-B1@-3/@0-B0@-4");
	std::ifstream boundaries(fs::path(outDir.dir) / "GUI" / "stationBoundarySections.txt");
	QVERIFY2(fs::exists(fs::path(outDir.dir) / "GUI" / "stationBoundarySections.txt"), "Station-boundary compatibility file exists");
	std::getline(boundaries, line);
	// Station-boundary compatibility row exported.
	QCOMPARE(line, "@0-B0@\t@0-B1@\t1");

	std::ifstream das(fs::path(outDir.dir) / "Passengers" / "DAS_FrenchCaseStudy.csv");
	QVERIFY2(fs::exists(fs::path(outDir.dir) / "Passengers" / "DAS_FrenchCaseStudy.csv"), "Passenger DAS compatibility file exists");
	std::getline(das, line);
	std::getline(das, line);
	// Passenger DAS compatibility row exported.
	QCOMPARE(line, "1,p0,1,Work,1,Work,Destination,,PT,TRUE,1.25,,Origin,,0.25,1");
	std::ifstream routeChoice(fs::path(outDir.dir) / "Passengers" / "RouteChoiceFC_EQ1.csv");
	QVERIFY2(fs::exists(fs::path(outDir.dir) / "Passengers" / "RouteChoiceFC_EQ1.csv"), "Passenger route-choice compatibility file exists");
	std::getline(routeChoice, line);
	// Passenger route-choice header exported.
	QCOMPARE(line, "person_id,destination,nb_transfers,Transfer_N1,r_service_lines_id1,r_service_lines_id2");
	std::getline(routeChoice, line);
	// Passenger route-choice compatibility row exported.
	QCOMPARE(line, "p0,Destination,1,Transfer,svc-code-1,svc-code-1");
	QVERIFY2(!fs::exists(fs::path(outDir.dir) / "legacy"), "No legacy subtree created");
}

// Canonical block and track IDs map consistently across legacy files.
void TestSceneExporter::mapsBlockAndTrackIds() {
	TempDir sceneDir, outDir;
	fs::path scene(sceneDir.dir);
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"Mapped Block IDs"})" << "\n";
	std::ofstream(scene / "infrastructure.json")
		<< R"({"tracks":[{"id":"main"}],"nodes":[],"arcs":[],"blocks":[{"id":"block.a","track":"main","length_km":1}]})"
		<< "\n";
	std::ofstream(scene / "stations.json") << R"({"stations":[]})" << "\n";
	std::ofstream(scene / "signalling.json")
		<< R"({"signals":[],"routes":[{"id":"route0","blocks":["block.a"]}],"single_track_restrictions":[{"start_block":"block.a","end_block":"block.a","protected_start_block":"@block.a@-1","protected_end_block":"@block.a@-2"}],"station_boundaries":[{"entrance_block":"block.a","exit_block":"block.a","direction":true}]})"
		<< "\n";
	std::ofstream(scene / "rolling_stock.json") << R"({"train_units":[],"compositions":[]})" << "\n";
	std::ofstream(scene / "services.json") << R"({"services":[]})" << "\n";

	auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
	printErrors(res.diagnostics, "Mapped block export");
	QVERIFY2(res.success(), "Mapped block scene export succeeds");
	std::string line;
	std::ifstream route(fs::path(outDir.dir) / "Routes" / "Route0.txt");
	std::getline(route, line);
	// Route uses the generated legacy block ID.
	QCOMPARE(line, "@0-B0@");
	std::ifstream restriction(fs::path(outDir.dir) / "GUI" / "singleTrackLimits.txt");
	std::getline(restriction, line);
	// Restriction uses the generated legacy block ID.
	QCOMPARE(line, "@0-B0@\t@0-B0@\t@0-B0@-1\t@0-B0@-2");
	std::ifstream boundary(fs::path(outDir.dir) / "GUI" / "stationBoundarySections.txt");
	std::getline(boundary, line);
	// Boundary uses the generated legacy block ID.
	QCOMPARE(line, "@0-B0@\t@0-B0@\t1");
}

// Canonical areas retain their exact fields in a versioned sidecar, using exported track IDs.
void TestSceneExporter::keepsSignallingAreasInSidecar() {
	TempDir sceneDir, outDir;
	const fs::path scene(sceneDir.dir), out(outDir.dir);
	const std::string areas = R"([{"id":" Fixed\tblock ","start_km":-0.125,"end_km":10.125,"level":0},{"id":"on-main","start_km":2.5,"end_km":5,"level":2,"track":"main"},{"id":"on-b7","start_km":0,"end_km":10,"level":4,"track":"B7"}])";
	writeAreaScene(scene, ",\"signalling_areas\":" + areas);
	const fs::path legacy = scene / "legacy" / "tracklines";
	fs::create_directories(legacy);
	std::ofstream(legacy / "egtrainsignallingareas.json") << "stale passthrough\n";
	std::ofstream(legacy / "areascasestudy.txt") << "Network\t-1\t99\t3\n";
	fs::create_directories(out / "TrackLines");
	std::ofstream(out / "TrackLines/EGTRAINSignallingAreas.json") << "stale destination\n";
	auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
	QVERIFY2(res.success(), "Canonical area export succeeds");
	const fs::path sidecar = out / "TrackLines/EGTRAINSignallingAreas.json";
	auto expected = nlohmann::json::parse(areas);
	expected[1]["track"] = "B0";
	QVERIFY2(nlohmann::json::parse(readText(sidecar), nullptr, false)
			== nlohmann::json({{"format_version", 1}, {"signalling_areas", expected}}),
		"Sidecar retains whitespace IDs, order, exact bounds, levels and mapped scopes");
	QVERIFY2(!std::any_of(fs::directory_iterator(out / "TrackLines"), fs::directory_iterator(), [](const auto& entry) { return entry.path().filename() == "egtrainsignallingareas.json"; }), "Case-insensitive passthrough sidecar is reserved");
	// TXT remains legacy compatibility output.
	QCOMPARE(readText(out / "TrackLines/AreasCaseStudy.txt"), "_Fixed_block_\t-0.125\t10.125\t0\non-main\t2.5\t5\t2\t0\non-b7\t0\t10\t4\t7\n");
	// Passthrough sources are untouched.
	QCOMPARE(readText(legacy / "egtrainsignallingareas.json"), "stale passthrough\n");
	QCOMPARE(readText(legacy / "areascasestudy.txt"), "Network\t-1\t99\t3\n");

	writeAreaScene(scene, "");
	res = exportLegacyScene(sceneDir.dir, outDir.dir);
	// Empty re-export removes stale TXT.
	QVERIFY(res.success());
	QVERIFY(!fs::exists(out / "TrackLines/AreasCaseStudy.txt"));
	QVERIFY(!fs::exists(out / "TrackLines/areascasestudy.txt"));
	QVERIFY2(nlohmann::json::parse(readText(sidecar), nullptr, false)
			== nlohmann::json({{"format_version", 1}, {"signalling_areas", nlohmann::json::array()}}),
		"Empty re-export publishes empty assignment");
	TempDir freshOut;
	res = exportLegacyScene(sceneDir.dir, freshOut.dir);
	// Empty export removes copied legacy TXT.
	QVERIFY(res.success());
	QVERIFY(!fs::exists(fs::path(freshOut.dir) / "TrackLines/AreasCaseStudy.txt"));
	QVERIFY(!fs::exists(fs::path(freshOut.dir) / "TrackLines/areascasestudy.txt"));

	// A failed export publishes nothing: the destination keeps the files and the sidecar of the last export.
	const auto published = snapshot(out);
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[{"id":"route0","blocks":["block.a"],"corridor":"bad\tcorridor"}]})";
	res = exportLegacyScene(sceneDir.dir, outDir.dir);
	// Late compatibility failure leaves the destination as it was.
	QVERIFY(!res.success());
	QVERIFY(snapshot(out) == published);

	std::ofstream(sidecar) << "stale destination\n";
	const auto stale = snapshot(out);
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[{"id":"route0","blocks":[]},{"id":"other","blocks":[]}]})";
	res = exportLegacyScene(sceneDir.dir, outDir.dir);
	// Failed compatibility export publishes no sidecar.
	QVERIFY(!res.success());
	QVERIFY(snapshot(out) == stale);

	writeAreaScene(scene, R"(,"signalling_areas":[{"id":"ghost","start_km":0,"end_km":1,"level":1,"track":"missing"}])");
	res = exportLegacyScene(sceneDir.dir, outDir.dir);
	// Unmappable canonical area fails export.
	QVERIFY(!res.success());
	QVERIFY(hasDiag(res.diagnostics, "scene.ref.unresolved"));
	QVERIFY(snapshot(out) == stale);
	writeAreaScene(scene, R"(,"signalling_areas":[{"id":"bad","start_km":1,"end_km":0,"level":2}])");
	res = exportLegacyScene(sceneDir.dir, outDir.dir);
	// Export reuses native area validation.
	QVERIFY(!res.success());
	QVERIFY(hasDiag(res.diagnostics, "scene.signalling_area.range"));
	QVERIFY(snapshot(out) == stale);

	writeAreaScene(scene, R"(,"signalling_areas":[{"id":"large-track","start_km":0,"end_km":1,"level":1,"track":"B999999999999999999999"}])");
	std::string infrastructure = readText(scene / "infrastructure.json");
	infrastructure.replace(infrastructure.find("B7"), 2, "B999999999999999999999");
	std::ofstream(scene / "infrastructure.json") << infrastructure;
	res = exportLegacyScene(sceneDir.dir, outDir.dir);
	// Unreconstructible exported track scope fails export.
	QVERIFY(!res.success());
	QVERIFY(hasDiag(res.diagnostics, "scene.export.ref"));
	QVERIFY(snapshot(out) == stale);

	writeAreaScene(scene, "");
	fs::remove(sidecar);
	fs::create_directories(sidecar);
	std::ofstream(sidecar / "blocked") << "keep\n";
	std::ofstream(out / "trainNames.txt") << "untouched\n";
	res = exportLegacyScene(sceneDir.dir, outDir.dir);
	// Unremovable stale sidecar fails before compatibility mutation.
	QVERIFY(!res.success());
	QCOMPARE(readText(out / "trainNames.txt"), "untouched\n");
	QCOMPARE(readText(sidecar / "blocked"), "keep\n");
}

void TestSceneExporter::refusesNoncanonicalTrackMapping_data() {
	QTest::addColumn<int>("variant");
	QTest::newRow("lone alias track") << 0;
	QTest::newRow("alias and canonical track with areas") << 1;
	QTest::newRow("alias next to a canonical track with an area") << 2;
	QTest::newRow("alias without areas") << 3;
	QTest::newRow("reused output with an alias folder") << 4;
	QTest::newRow("passthrough alias folder") << 5;
}

// Marked conversion refuses every noncanonical candidate, even without scoped areas.
void TestSceneExporter::refusesNoncanonicalTrackMapping() {
	QFETCH(int, variant);
	TempDir sourceDir, outDir;
	const fs::path source(sourceDir.dir), out(outDir.dir);
	writeAreaScene(source, "");
	auto infrastructure = nlohmann::json::parse(readText(source / "infrastructure.json"));
	auto signalling = nlohmann::json::parse(readText(source / "signalling.json"));
	if (variant < 4) {
		infrastructure["tracks"][0]["id"] = "B07";
		for (const auto& category : {"nodes", "blocks"})
			for (auto& item : infrastructure[category])
				if (item["track"] == "main") item["track"] = "B07";
		// The lone alias has an actual route reference, not just an area.
		if (variant == 0) {
			infrastructure["tracks"].erase(1);
			infrastructure["blocks"].erase(1);
		}
		signalling["signalling_areas"] = nlohmann::json::array();
		if (variant < 3) signalling["signalling_areas"].push_back({{"id", "seven"}, {"start_km", 0}, {"end_km", 1}, {"level", 2}, {"track", variant == 2 ? "B7" : "B07"}});
		if (variant == 1) signalling["signalling_areas"].push_back({{"id", "other"}, {"start_km", 0}, {"end_km", 1}, {"level", 2}, {"track", "B7"}});
		std::ofstream(source / "infrastructure.json") << infrastructure.dump();
		std::ofstream(source / "signalling.json") << signalling.dump();
	} else {
		const fs::path alias = (variant == 4 ? out : source / "legacy") / "TrackLines/B07";
		fs::create_directories(alias);
		std::ofstream(alias / "NodiCumPari.txt") << "0 0 0\n1 1 0\n";
	}
	fs::create_directories(out / "TrackLines");
	std::ofstream(out / "TrackLines/EGTRAINSignallingAreas.json") << "stale\n";
	const auto before = snapshot(out);
	const auto exported = exportLegacyScene(sourceDir.dir, outDir.dir);
	// Noncanonical track mapping, unscoped alias, reused output and passthrough alias refuse authoritative export.
	QVERIFY(!exported.success());
	QVERIFY(hasDiag(exported.diagnostics, "scene.export.ref"));
	QVERIFY(snapshot(out) == before);
}

void TestSceneExporter::rejectsOverlappingOutput_data() {
	QTest::addColumn<int>("variant");
	QTest::newRow("output is the passthrough folder") << 0;
	QTest::newRow("output is the scene folder") << 1;
	QTest::newRow("output holds the scene folder") << 2;
	QTest::newRow("output inside the scene folder") << 3;
	QTest::newRow("output is a link to the passthrough folder") << 4;
	QTest::newRow("output holds a link into the passthrough folder") << 5;
	QTest::newRow("output inside the target of a passthrough link") << 6;
}

// Reject overlap before cleanup, including resolved source and passthrough aliases.
void TestSceneExporter::rejectsOverlappingOutput() {
	QFETCH(int, variant);
	TempDir rootDir;
	const fs::path root(rootDir.dir), source = root / "source", outside = root / "passthrough";
	fs::create_directories(source);
	writeAreaScene(source, "");
	fs::path passthrough = source / "legacy";
	std::error_code ec;
	if (variant == 6) {
		fs::create_directories(outside);
		fs::create_directory_symlink(outside, passthrough, ec);
	} else fs::create_directories(passthrough);
	if (ec)
		QSKIP(("unsupported passthrough symlink: " + ec.message()).c_str());
	fs::create_directories(passthrough / "TrackLines");
	std::ofstream(passthrough / "TrackLines/AreasCaseStudy.txt") << "original TXT\n";
	std::ofstream(passthrough / "TrackLines/EGTRAINSignallingAreas.json") << "original sidecar\n";
	fs::path out;
	if (variant == 0) out = passthrough;
	if (variant == 1) out = source;
	if (variant == 2) out = root;
	if (variant == 3) out = source / "missing/output";
	if (variant == 4) {
		out = root / "alias";
		fs::create_directory_symlink(passthrough, out, ec);
	}
	if (variant == 5) {
		out = root / "destination";
		fs::create_directories(out);
		fs::create_directory_symlink(passthrough / "TrackLines", out / "tracklines", ec);
	}
	if (variant == 6) out = outside / "missing/output";
	if (ec)
		QSKIP(("unsupported output symlink: " + ec.message()).c_str());
	const auto sourceBefore = snapshot(source), passthroughBefore = snapshot(passthrough);
	const auto exported = exportLegacyScene(source.string(), out.string());
	// Overlap rejection preserves all canonical and passthrough paths and bytes before cleanup.
	QVERIFY(!exported.success());
	QVERIFY(hasDiag(exported.diagnostics, "scene.export.path"));
	QVERIFY(snapshot(source) == sourceBefore);
	QVERIFY(snapshot(passthrough) == passthroughBefore);
}

// An export into a missing folder beside the scene succeeds and leaves the scene as it was.
void TestSceneExporter::exportsBesideScene() {
	TempDir rootDir;
	const fs::path source = fs::path(rootDir.dir) / "source", out = fs::path(rootDir.dir) / "sibling/missing/output";
	fs::create_directories(source);
	writeAreaScene(source, "");
	const auto before = snapshot(source);
	// Non-overlapping sibling export supports missing destination leaves and preserves source bytes.
	QVERIFY(exportLegacyScene(source.string(), out.string()).success());
	QVERIFY(snapshot(source) == before);
}

// A re-export into a used folder replaces the route files, which the importer reads all of, and keeps the files that
// the export does not write. A failed export changes nothing.
void TestSceneExporter::reExportReplacesRouteFiles() {
	TempDir sceneDir, outDir, emptyDir;
	const fs::path scene(sceneDir.dir), out(outDir.dir);
	writeAreaScene(scene, "");
	std::ofstream(scene / "signalling.json")
		<< R"({"signals":[],"routes":[{"id":"route0","blocks":["block.a"]},{"id":"route1","blocks":["block.a"]},{"id":"route2","blocks":["block.b"]}]})"
		<< "\n";
	// A scene with three routes exports.
	QVERIFY(exportLegacyScene(sceneDir.dir, outDir.dir).success());
	QVERIFY(fs::is_regular_file(out / "Routes/Route2.txt"));
	std::ofstream(out / "notes.txt") << "mine\n";
	std::ofstream(out / "Routes/notes.txt") << "mine\n";
	writeAreaScene(scene, "");
	QVERIFY2(exportLegacyScene(sceneDir.dir, outDir.dir).success(), "A scene with one route exports into the same folder");
	std::vector<std::string> routeFiles;
	for (const auto& entry : fs::directory_iterator(out / "Routes"))
		routeFiles.push_back(entry.path().filename().string());
	std::sort(routeFiles.begin(), routeFiles.end());
	// A re-export removes the old route files and keeps the files it does not write.
	QVERIFY((routeFiles == std::vector<std::string>{"Route0.txt", "notes.txt"}));
	QCOMPARE(readText(out / "notes.txt"), "mine\n");

	const auto published = snapshot(out);
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[{"id":"route0","blocks":["block.a"],"corridor":"bad\tcorridor"}]})";
	// A failed re-export leaves the folder as it was.
	QVERIFY(!exportLegacyScene(sceneDir.dir, outDir.dir).success());
	QVERIFY(snapshot(out) == published);
	// A failed export into an empty folder writes nothing.
	QVERIFY(!exportLegacyScene(sceneDir.dir, emptyDir.dir).success());
	QVERIFY(fs::is_empty(emptyDir.dir));
	const fs::path missing = fs::path(emptyDir.dir) / "missing";
	// A failed export into a new folder creates nothing.
	QVERIFY(!exportLegacyScene(sceneDir.dir, missing.string()).success());
	QVERIFY(fs::is_empty(emptyDir.dir));
}

// A re-export into a used folder writes its own copy of every file that it writes, so the folder has the station name
// and the node coordinate of the scene as it is now.
void TestSceneExporter::reExportWritesCurrentScene() {
	TempDir sceneDir, outDir;
	const fs::path scene(sceneDir.dir), out(outDir.dir);
	writeAreaScene(scene, "");
	std::ofstream(scene / "stations.json") << R"({"stations":[{"id":"st","name":"Old","platforms":[{"id":"p","nodes":["n1"]}]}]})" << "\n";
	// A scene with a station exports.
	QVERIFY(exportLegacyScene(sceneDir.dir, outDir.dir).success());
	QCOMPARE(readText(out / "TrackLines/Stations.txt"), "1\tOld\n");
	QCOMPARE(readText(out / "TrackLines/B0/NodiCumPari.txt"), "0\t0\t0\n1\t1\t0\n");
	std::ofstream(scene / "stations.json") << R"({"stations":[{"id":"st","name":"New","platforms":[{"id":"p","nodes":["n1"]}]}]})" << "\n";
	std::string infrastructure = readText(scene / "infrastructure.json");
	infrastructure.replace(infrastructure.find(R"("x_km":1)"), 8, R"("x_km":2)");
	std::ofstream(scene / "infrastructure.json") << infrastructure;
	// A re-export into the same folder writes the new station name and node coordinate.
	QVERIFY(exportLegacyScene(sceneDir.dir, outDir.dir).success());
	QCOMPARE(readText(out / "TrackLines/Stations.txt"), "2\tNew\n");
	QCOMPARE(readText(out / "TrackLines/B0/NodiCumPari.txt"), "0\t0\t0\n1\t2\t0\n");
	QVERIFY(readText(out / "GUI/StationsCoord.txt").rfind("New\t", 0) == 0);
}

// A re-export into a used folder removes the inputs of the importer that the earlier export wrote and this one does
// not, so the folder imports as the new scene. The files and folders that the importer does not read stay.
void TestSceneExporter::reExportRemovesStaleImporterInputs() {
	TempDir sceneDir, outDir, importDir;
	const fs::path scene(sceneDir.dir), out(outDir.dir);
	const fs::path rollout = fs::path("TimeTable") / "Scenarios_Entrance_Delays" / "Rollout_1.txt";
	writeAreaScene(scene, "");
	std::ofstream(scene / "stations.json") << R"({"stations":[{"id":"st","name":"Station","platforms":[{"id":"p","nodes":["n1"]}]}]})" << "\n";
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[{"id":"route0","blocks":["block.a"],"corridor":"North"}]})" << "\n";
	fs::create_directories((scene / "legacy" / rollout).parent_path());
	std::ofstream(scene / "legacy" / rollout) << "60\n";
	// A scene with two tracks, a route corridor and a rollout exports.
	QVERIFY(exportLegacyScene(sceneDir.dir, outDir.dir).success());
	QVERIFY(fs::is_directory(out / "TrackLines/B7"));
	QVERIFY(fs::is_regular_file(out / "GUI/caseStudyRouteCorridors.txt"));
	QVERIFY(fs::is_regular_file(out / rollout));
	std::ofstream(out / "notes.txt") << "mine\n";
	std::ofstream(out / "TrackLines/B0/notes.txt") << "mine\n";
	fs::create_directories(out / "Extra");
	std::ofstream(out / "Extra/data.txt") << "mine\n";

	fs::remove_all(scene / "legacy");
	std::ofstream(scene / "infrastructure.json")
		<< R"({"tracks":[{"id":"main"}],"nodes":[{"id":"n0","track":"main","x_km":0,"y_km":0},{"id":"n1","track":"main","x_km":1,"y_km":0}],"arcs":[],"blocks":[{"id":"block.a","track":"main","length_km":1}]})"
		<< "\n";
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[{"id":"route0","blocks":["block.a"]}]})" << "\n";
	// A re-export of a smaller scene removes the old track folder, corridor file and rollout.
	QVERIFY(exportLegacyScene(sceneDir.dir, outDir.dir).success());
	QVERIFY(!fs::exists(out / "TrackLines/B7"));
	QVERIFY(!fs::exists(out / "GUI/caseStudyRouteCorridors.txt"));
	QVERIFY(!fs::exists(out / rollout));
	// A re-export keeps the files and folders that the importer does not read.
	QCOMPARE(readText(out / "notes.txt"), "mine\n");
	QCOMPARE(readText(out / "TrackLines/B0/notes.txt"), "mine\n");
	QCOMPARE(readText(out / "Extra/data.txt"), "mine\n");

	const fs::path imported = fs::path(importDir.dir) / "case";
	const SceneImportResult importResult = importLegacyScene(outDir.dir, imported.string(), "Smaller");
	const SceneLoadResult loaded = loadScene(imported.string());
	printErrors(importResult.diagnostics, "Re-export import");
	// The folder of a re-export imports as the smaller scene.
	QVERIFY(importResult.success());
	QCOMPARE(loaded.scene.tracks.size(), std::size_t(1));
	QVERIFY(std::none_of(loaded.scene.routes.begin(), loaded.scene.routes.end(), [](const SceneRoute& route) { return route.hasCorridor; }));
	QVERIFY(std::none_of(loaded.scene.scenarios.begin(), loaded.scene.scenarios.end(),
		[](const SceneScenario& scenario) { return scenario.id == "rollout_1"; }));
}

// A scene directory, an export destination, the legacy data below the scene and the source files of a train unit,
// all with Unicode names.
void TestSceneExporter::exportsUnderUnicodeNames() {
	TempDir rootDir;
	const fs::path scene = fs::path(rootDir.dir) / fs::u8path(kUnicodeName);
	const fs::path out = fs::path(rootDir.dir) / fs::u8path("export-" + kUnicodeName);
	fs::create_directories(scene / "legacy");
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"Unicode Export"})" << "\n";
	std::ofstream(scene / "infrastructure.json") << R"({"nodes":[],"arcs":[]})" << "\n";
	std::ofstream(scene / "stations.json") << R"({"stations":[{"id":"st","name":"Station","platforms":[]}]})" << "\n";
	std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[{"id":"route0","blocks":["Depot/1"]}]})" << "\n";
	std::ofstream(scene / "rolling_stock.json")
		<< R"({"train_units":[{"id":"unit","physical":{"mass_of_traction_unit_kg":1,"mass_of_a_wagon_kg":1,"number_of_wagons":0,"max_speed_ms":1,"max_deceleration_ms2":1,"frontal_area_m2":1,"resistance_coefficient":1,"jerk_ms3":1,"length_m":1},"traction_curve":[[0,1,1,0,0]],"source":{"data_file":"/)"
		<< kUnicodeName << R"(.dat","traction_file":"/)" << kUnicodeName << R"(-effort.dat"}}],"compositions":[{"id":"comp","units":["unit"]}]})"
		<< "\n";
	std::ofstream(scene / "services.json")
		<< R"({"services":[{"id":"svc","composition":"comp","route":"route0","entry_time_seconds":0,"stops":[{"station":"st","departure_seconds":0,"dwell_seconds":0}]}]})"
		<< "\n";
	std::ofstream(scene / "legacy" / fs::u8path(kUnicodeName + ".txt")) << "passthrough\n";

	const auto res = exportLegacyScene(scene.u8string(), out.u8string());
	printErrors(res.diagnostics, "Unicode export");
	QVERIFY2(res.success(), "export from a scene directory with a Unicode name to a destination with a Unicode name succeeds");
	// Export into a destination with a Unicode name writes its files.
	QVERIFY(fs::is_regular_file(out / "Routes" / "Route0.txt"));
	QVERIFY(fs::is_regular_file(out / "TimeTable" / "svc.txt"));
	// Train data files with Unicode names are exported under those names.
	QVERIFY(fs::is_regular_file(out / "TrainData" / fs::u8path(kUnicodeName + ".dat")));
	QVERIFY(fs::is_regular_file(out / "TrainData" / fs::u8path(kUnicodeName + "-effort.dat")));
	QVERIFY2(fs::is_regular_file(out / fs::u8path(kUnicodeName + ".txt")),
		"legacy data with a Unicode name is copied");
}

// The layout and the settings of a scene with views come from the scene, not from the track lines.
void TestSceneExporter::exportsLayoutAndSettingsFromViews() {
	TempDir sceneDir, outDir;
	const fs::path scene(sceneDir.dir), out(outDir.dir);
	writeAreaScene(scene, "");
	const std::string settings = R"("base_time":"07:10:40","simulation_settings":{"duration_seconds":9000,"buffer_time_seconds":0,"recovery_time_percent":0})";
	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"Layout Export",)" << settings << "}\n";
	std::ofstream(scene / "stations.json") << R"({"stations":[{"id":"st","name":"Station","platforms":[]}]})" << "\n";
	std::ofstream(scene / "views.json")
		<< R"({"tracks":[{"track":"main","level":2,"region":3},{"track":"B7","level":1,"region":0,"visible":false}],"stations":[{"station":"st","latitude":52.5,"longitude":0.125,"regions":[{"id":3,"position_km":1.5},{"id":4,"position_km":2}],"corridors":["A","B,C"]}]})"
		<< "\n";
	const auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
	QVERIFY2(res.success(), "Export of a scene with views succeeds");
	// Station layout is written from the views with latitude, regions and corridors.
	QCOMPARE(readText(out / "GUI/StationsCoord.txt"), "Station\t52.5\t0.125\t3,4\t1.5,2\tA\n");
	// Track level and region are written from the views.
	QCOMPARE(readText(out / "GUI/caseStudyTrackData.txt"), "0\t2\t3\n7\t1\t0\n");
	// Hidden tracks are listed.
	QCOMPARE(readText(out / "GUI/unusedTracks.txt"), "7\n");
	// A corridor that the text file cannot hold is reported once.
	QCOMPARE(std::count_if(res.diagnostics.begin(), res.diagnostics.end(),
				 [](const SceneDiagnostic& d) { return d.code == "scene.export.compatibility" && d.severity == SceneSeverity::Warning; }),
		std::ptrdiff_t(1));
	QVERIFY2(nlohmann::json::parse(readText(out / "EGTRAINInitialParameters.json"), nullptr, false)
			== nlohmann::json::parse(R"({"format_version":1,)" + settings + "}"),
		"Base time and simulation settings are written to the sidecar");

	std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"Layout Export"})" << "\n";
	// A scene without settings removes a stale sidecar.
	QVERIFY(exportLegacyScene(sceneDir.dir, outDir.dir).success());
	QVERIFY(!fs::exists(out / "EGTRAINInitialParameters.json"));

	std::ofstream(scene / "views.json") << R"({"tracks":[{"track":"main","level":2,"region":3,"double_switch":true}]})" << "\n";
	TempDir switchDir;
	const auto doubleSwitch = exportLegacyScene(sceneDir.dir, switchDir.dir);
	// A double switch that the track data cannot hold is reported.
	QVERIFY(doubleSwitch.success());
	QCOMPARE(readText(fs::path(switchDir.dir) / "GUI/caseStudyTrackData.txt"), "0\t2\t3\n");
	QVERIFY(hasDiag(doubleSwitch.diagnostics, "scene.export.compatibility", SceneSeverity::Warning));
}

QTEST_APPLESS_MAIN(TestSceneExporter)
#include "test_sceneexporter.moc"
