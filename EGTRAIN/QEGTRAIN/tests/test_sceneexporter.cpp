#include "../SceneImporter.h"
#include "../SceneExporter.h"
#include "../SceneModel.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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
		fs::path temp = fs::temp_directory_path() /
						("scene_exporter_test_" + std::to_string(stamp) + "_" + std::to_string(counter++));
		fs::create_directories(temp);
		dir = temp.string();
	}
	~TempDir() {
		std::error_code ec;
		fs::remove_all(dir, ec);
	}
};

static void printErrors(const std::vector<SceneDiagnostic>& diags, const char* label) {
	for (const auto& d : diags) {
		if (d.severity == SceneSeverity::Error)
			std::cerr << label << ": " << toDisplayText(d) << "\n";
	}
}

static void eraseLegacySource(json& j) {
	if (j.contains("legacy_source")) {
		j.erase("legacy_source");
	}
}

static void sortArrayById(json& arr) {
	if (!arr.is_array())
		return;
	std::sort(arr.begin(), arr.end(), [](const json& a, const json& b) {
		std::string ida = a.value("id", "");
		std::string idb = b.value("id", "");
		return ida < idb;
	});
}

int main(int argc, char** argv) {
	if (argc < 4) {
		std::cerr << "Usage: test_sceneexporter <copenhagen_dir> <brescia_dir> <netherlands_dir>\n";
		return 1;
	}
	std::string copDir = argv[1];
	std::string breDir = argv[2];
	std::string nlDir = argv[3];
	bool ok = true;

	auto testRoundTrip = [&](const std::string& legacyDir, const std::string& name, TempDir& outDir) {
		TempDir scene1Dir, scene2Dir;
		auto importRes1 = importLegacyScene(legacyDir, scene1Dir.dir, name);
		printErrors(importRes1.diagnostics, (name + " import1").c_str());
		ok &= expect(importRes1.success(), (name + " import1 succeeded").c_str());

		auto exportRes = exportLegacyScene(scene1Dir.dir, outDir.dir);
		printErrors(exportRes.diagnostics, (name + " export").c_str());
		ok &= expect(exportRes.success(), (name + " export succeeded").c_str());

		auto importRes2 = importLegacyScene(outDir.dir, scene2Dir.dir, name);
		printErrors(importRes2.diagnostics, (name + " import2").c_str());
		ok &= expect(importRes2.success(), (name + " import2 succeeded").c_str());

		auto compareJson = [&](const std::string& fname, const std::string& arrayKey) {
			std::ifstream f1(fs::path(scene1Dir.dir) / fname);
			std::ifstream f2(fs::path(scene2Dir.dir) / fname);
			json j1, j2;
			f1 >> j1;
			f2 >> j2;
			eraseLegacySource(j1);
			eraseLegacySource(j2);
			if (!arrayKey.empty()) {
				sortArrayById(j1[arrayKey]);
				sortArrayById(j2[arrayKey]);
			}
			bool match = j1 == j2;
			ok &= expect(match, (name + " " + fname + " round-trips").c_str());
			if (!match) {
				std::cerr << "Diff in " << fname << "\n";
				if (fname == "rolling_stock.json") {
					std::cerr << "j1: " << j1.dump() << "\n\nj2: " << j2.dump() << "\n";
				}
			}
		};

		compareJson("services.json", "services");
		compareJson("signalling.json", "routes");
		compareJson("rolling_stock.json", "train_units");
		compareJson("stations.json", "stations");

		return outDir.dir;
	};

	// 1. Copenhagen round trip
	TempDir copOutDirWrap;
	std::string copOutDir = testRoundTrip(copDir, "Copenhagen", copOutDirWrap);

	// 2. Brescia round trip
	TempDir breOutDirWrap;
	testRoundTrip(breDir, "Brescia", breOutDirWrap);

	// 3. Netherlands switch-transition route tokens export unchanged.
	{
		TempDir sceneDir, outDir;
		auto importRes = importLegacyScene(nlDir, sceneDir.dir, "Netherlands");
		printErrors(importRes.diagnostics, "Netherlands import");
		ok &= expect(importRes.success(), "Netherlands import succeeded");
		auto exportRes = exportLegacyScene(sceneDir.dir, outDir.dir);
		printErrors(exportRes.diagnostics, "Netherlands export");
		ok &= expect(exportRes.success(), "Netherlands export succeeded");

		std::ifstream sourceRoute(fs::path(nlDir) / "Routes" / "Route0.txt");
		std::ifstream exportedRoute(fs::path(outDir.dir) / "Routes" / "Route0.txt");
		std::string sourceLine, exportedLine;
		std::getline(sourceRoute, sourceLine);
		std::getline(sourceRoute, sourceLine);
		std::getline(exportedRoute, exportedLine);
		std::getline(exportedRoute, exportedLine);
		ok &= expect(sourceLine.find('/') != std::string::npos && sourceLine.rfind("@", 0) == 0,
					 "Netherlands Route0 line 2 is a switch-transition token");
		ok &= expect(sourceLine == exportedLine, "Netherlands switch-transition token exports unchanged");
		ok &= expect(exportedLine.rfind("@@", 0) != 0, "Netherlands switch-transition token not double-wrapped");
	}

	// 4. Exported tree shape
	{
		fs::path cOut(copOutDir);
		ok &= expect(fs::exists(cOut / "trainNames.txt"), "trainNames.txt exists");
		ok &= expect(fs::is_directory(cOut / "Trains"), "Trains dir exists");
		ok &= expect(fs::is_directory(cOut / "TimeTable"), "TimeTable dir exists");
		ok &= expect(fs::is_directory(cOut / "TrainData"), "TrainData dir exists");
		ok &= expect(fs::is_directory(cOut / "Routes"), "Routes dir exists");
		ok &= expect(fs::exists(cOut / "Routes" / "Route0.txt"), "Route0.txt exists");

		auto checkLegacyFile = [&](const std::string& relPath) {
			fs::path srcFile = fs::path(copDir) / relPath;
			fs::path dstFile = cOut / relPath;
			if (fs::exists(srcFile)) {
				ok &= expect(fs::exists(dstFile), (relPath + " exists in export").c_str());
			} else {
				std::cerr << "skipped checking " << relPath << " (not in source)\n";
			}
		};

		checkLegacyFile("TrackLines/Stations.txt");
		checkLegacyFile("TrackLines/Connections.txt");
		checkLegacyFile("TMS/Timetable Order");
		checkLegacyFile("RoutesToWrite/RoutesToJoin.txt");
		checkLegacyFile("GUI/TimetableGraph.html");
		checkLegacyFile("GUI/StationsCoord.txt");
	}

	// 5. Terminus/first-stop sentinels survive
	{
		fs::path cOut(copOutDir);
		std::ifstream ttf(cOut / "TimeTable" / "A-Hillerod-Hundige.txt");
		if (ttf) {
			std::string line, firstLine, lastLine;
			while (std::getline(ttf, line)) {
				if (firstLine.empty())
					firstLine = line;
				if (!line.empty())
					lastLine = line;
			}
			std::stringstream ssFirst(firstLine), ssLast(lastLine);
			std::string st;
			double dwell, arr, dep;
			ssFirst >> st >> dwell >> arr >> dep;
			ok &= expect(arr == -1, "First row arrival is -1");
			ssLast >> st >> dwell >> arr >> dep;
			ok &= expect(dep == -1, "Last row departure is -1");
		} else {
			ok &= expect(false, "A-Hillerod-Hundige.txt not found");
		}
	}

	// 6. Minimal fixture scene
	{
		TempDir outDir;
		std::string fixture = fs::path(argv[0]).parent_path().parent_path().string() + "/EGTRAIN/QEGTRAIN/tests/fixtures/scenes/minimal";
		if (!fs::exists(fixture)) {
			// fallback if run from elsewhere
			fixture = "EGTRAIN/QEGTRAIN/tests/fixtures/scenes/minimal";
		}
		auto res = exportLegacyScene(fixture, outDir.dir);
		ok &= expect(!res.success(), "Minimal scene export fails");
		ok &= expect(hasDiag(res.diagnostics, "scene.export.ref"), "scene.export.ref produced");
	}

	// 7. Ordinary slash-containing block ids are still wrapped.
	{
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
			<< R"({"services":[{"id":"svc","composition":"comp","route":"route0","entry_time_seconds":0,"stops":[{"station":"st","departure_seconds":0,"dwell_seconds":0}]}]})"
			<< "\n";

		auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
		printErrors(res.diagnostics, "SlashBlock export");
		ok &= expect(res.success(), "Slash block scene export succeeds");
		std::ifstream route(fs::path(outDir.dir) / "Routes" / "Route0.txt");
		std::string line;
		std::getline(route, line);
		ok &= expect(line == "@Depot/1@", "Slash block id remains wrapped");
	}

	// 8. Nonexistent scene dir
	{
		TempDir outDir;
		auto res = exportLegacyScene("/no/such/scene", outDir.dir);
		ok &= expect(!res.success(), "Nonexistent scene export fails");
	}

	// 9. Multi-unit composition export
	{
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
		ok &= expect(res.success(), "Multi unit scene export succeeds");

		std::ifstream df(fs::path(outDir.dir) / "TrainData" / "compMulti.txt");
		if (expect(df.good(), "Multi-unit TrainData file exists")) {
			double tmass, wmass, wcount, maxspd, maxdec, farea, rescoeff, jerk, len;
			df >> tmass >> wmass >> wcount >> maxspd >> maxdec >> farea >> rescoeff >> jerk >> len;
			ok &= expect(tmass == 300, "Summed traction mass is 300");
			ok &= expect(wcount == 3, "Summed wagon count is 3");
			ok &= expect(len == 90, "Summed length is 90");
			ok &= expect(maxspd == 60, "Minimum max speed is 60");
		}

		std::ifstream tf(fs::path(outDir.dir) / "TrainData" / "T_compMulti.txt");
		if (expect(tf.good(), "Multi-unit traction file exists")) {
			double a, b, c0, c1, c2;
			tf >> a >> b >> c0 >> c1 >> c2;
			ok &= expect(a == 0 && b == 5 && c0 == 1 && c1 == 0 && c2 == 0, "Band 0-5 correct");
			tf >> a >> b >> c0 >> c1 >> c2;
			ok &= expect(a == 5 && b == 10 && c0 == 1 && c1 == 1 && c2 == 0, "Band 5-10 correct");
			tf >> a >> b >> c0 >> c1 >> c2;
			ok &= expect(a == 10 && b == 15 && c0 == 1 && c1 == 1 && c2 == 0, "Band 10-15 correct");
			tf >> a >> b >> c0 >> c1 >> c2;
			ok &= expect(a == 15 && b == 20 && c0 == 1 && c1 == 1 && c2 == 0, "Band 15-20 correct");
			tf >> a >> b >> c0 >> c1 >> c2;
			ok &= expect(a == 20 && b == 25 && c0 == 0 && c1 == 1 && c2 == 0, "Band 20-25 correct");
		}
	}

	// 10. Multi-unit composition with only degenerate traction bands fails
	{
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
		ok &= expect(!res.success(), "Degenerate multi-unit traction export fails");
		bool foundEmptyCurveDiag = false;
		for (const auto& d : res.diagnostics) {
			if (d.severity == SceneSeverity::Error && d.message.find("Combined traction curve is empty") != std::string::npos)
				foundEmptyCurveDiag = true;
		}
		ok &= expect(foundEmptyCurveDiag, "Empty combined traction curve is diagnosed");
	}

	// 11. Near-duplicate band boundaries collapse instead of forming sliver bands
	{
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
		ok &= expect(res.success(), "Near-duplicate boundary scene export succeeds");

		std::ifstream tf(fs::path(outDir.dir) / "TrainData" / "T_compUlp.txt");
		if (expect(tf.good(), "Ulp traction file exists")) {
			std::string first, second;
			std::getline(tf, first);
			std::getline(tf, second);
			double a, b, c0, c1, c2;
			std::istringstream(first) >> a >> b >> c0 >> c1 >> c2;
			ok &= expect(a == 0 && c0 == 3, "Single merged band sums both curves");
			ok &= expect(second.empty(), "No sliver band is emitted");
		}
	}

	// 12. Synthetic GUI layout generated from Stations and NodiCumPari
	{
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
		ok &= expect(res.success(), "GUI Synthesis scene export succeeds");

		fs::path guiDir = fs::path(outDir.dir) / "GUI";
		ok &= expect(fs::exists(guiDir / "StationsCoord.txt"), "GUI/StationsCoord.txt exists");

		std::ifstream sc(guiDir / "StationsCoord.txt");
		if (sc) {
			std::vector<std::string> lines;
			std::string line;
			while (std::getline(sc, line)) {
				if (!line.empty()) lines.push_back(line);
			}
			ok &= expect(lines.size() == 6, "GUI/StationsCoord.txt has 6 lines");
			if (lines.size() >= 4) {
				ok &= expect(lines[0] == "A\t1\t0\t0\t0", ("Line 1 (A at 0) correct: " + lines[0]).c_str());
				ok &= expect(lines[3] == "C\t1\t0.64000000000000001\t1\t100", ("Line 4 (C at 100) correct: " + lines[3]).c_str());
			}
		}

		std::ifstream td(guiDir / "caseStudyTrackData.txt");
		if (td) {
			std::vector<std::string> lines;
			std::string line;
			while (std::getline(td, line)) {
				if (!line.empty()) lines.push_back(line);
			}
			ok &= expect(lines.size() == 2, "GUI/caseStudyTrackData.txt has 2 lines");
			if (lines.size() >= 2) {
				ok &= expect(lines[0] == "0\t0\t0", ("Line 1 correct: " + lines[0]).c_str());
				ok &= expect(lines[1] == "1\t1\t1", ("Line 2 correct: " + lines[1]).c_str());
			}
		}
	}

	// 13. Scene with no legacy/TrackLines/Stations.txt exports successfully and writes no GUI/StationsCoord.txt
	{
		TempDir sceneDir, outDir;
		fs::path scene(sceneDir.dir);
		std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"No Stations"})" << "\n";
		std::ofstream(scene / "infrastructure.json") << R"({"nodes":[],"arcs":[]})" << "\n";
		std::ofstream(scene / "stations.json") << R"({"stations":[]})" << "\n";
		std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[]})" << "\n";
		std::ofstream(scene / "rolling_stock.json") << R"({"train_units":[],"compositions":[]})" << "\n";
		std::ofstream(scene / "services.json") << R"({"services":[]})" << "\n";

		auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
		ok &= expect(res.success(), "No Stations scene export succeeds");

		fs::path guiDir = fs::path(outDir.dir) / "GUI";
		ok &= expect(!fs::exists(guiDir / "StationsCoord.txt"), "GUI/StationsCoord.txt is not written");
	}

	// 14. Mirrored band listed before the reference trackline still maps by name
	{
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
		ok &= expect(res.success(), "GUI Order scene export succeeds");

		std::ifstream sc(fs::path(outDir.dir) / "GUI" / "StationsCoord.txt");
		if (expect(sc.good(), "GUI Order StationsCoord.txt exists")) {
			std::string line;
			std::getline(sc, line);
			ok &= expect(line == "C\t1\t0.64000000000000001\t1\t100",
						 ("Mirrored C listed first still maps to lon 0.64: " + line).c_str());
		}
	}

	// 15. Stations without any trackline node data generate no GUI files at all
	{
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
		ok &= expect(res.success(), "No Nodes scene export succeeds");
		ok &= expect(!fs::exists(fs::path(outDir.dir) / "GUI" / "StationsCoord.txt"), "No half-written StationsCoord.txt");
		ok &= expect(!fs::exists(fs::path(outDir.dir) / "GUI" / "caseStudyTrackData.txt"), "No half-written caseStudyTrackData.txt");
	}

	if (!ok)
		return 1;
	std::cout << "all SceneExporter tests passed\n";
	return 0;
}
