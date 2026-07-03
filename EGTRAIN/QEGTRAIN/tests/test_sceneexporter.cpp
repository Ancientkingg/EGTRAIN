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
	if (argc < 3) {
		std::cerr << "Usage: test_sceneexporter <copenhagen_dir> <brescia_dir>\n";
		return 1;
	}
	std::string copDir = argv[1];
	std::string breDir = argv[2];
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

	// 3. Exported tree shape
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

	// 4. Terminus/first-stop sentinels survive
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

	// 5. Minimal fixture scene
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

	// 6. Nonexistent scene dir
	{
		TempDir outDir;
		auto res = exportLegacyScene("/no/such/scene", outDir.dir);
		ok &= expect(!res.success(), "Nonexistent scene export fails");
	}

	if (!ok)
		return 1;
	std::cout << "all SceneExporter tests passed\n";
	return 0;
}
