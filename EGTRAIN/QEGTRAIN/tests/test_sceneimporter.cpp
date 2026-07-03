#include "../SceneImporter.h"
#include "../SceneModel.h"
#include "../SceneValidator.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
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

static int countDiag(const std::vector<SceneDiagnostic>& diags, const std::string& code, SceneSeverity sev) {
	int count = 0;
	for (const auto& d : diags) {
		if (d.code == code && d.severity == sev)
			count++;
	}
	return count;
}

struct TempDir {
	std::string dir;
	TempDir() {
		static int counter = 0;
		auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		fs::path temp = fs::temp_directory_path() /
						("scene_test_" + std::to_string(stamp) + "_" + std::to_string(counter++));
		fs::create_directories(temp);
		dir = temp.string();
	}
	~TempDir() {
		std::error_code ec;
		fs::remove_all(dir, ec);
	}
};

static std::vector<std::string> readTokens(const std::string& line) {
	std::vector<std::string> tokens;
	std::stringstream ss(line);
	std::string token;
	while (ss >> token) {
		tokens.push_back(token);
	}
	return tokens;
}

static void printErrors(const std::vector<SceneDiagnostic>& diags, const char* label) {
	for (const auto& d : diags) {
		if (d.severity == SceneSeverity::Error)
			std::cerr << label << ": " << toDisplayText(d) << "\n";
	}
}

int main(int argc, char** argv) {
	if (argc < 4) {
		std::cerr << "Usage: test_sceneimporter <copenhagen_dir> <brescia_dir> <netherlands_dir>\n";
		return 1;
	}
	std::string copDir = argv[1];
	std::string breDir = argv[2];
	std::string nlDir = argv[3];
	bool ok = true;

	auto runPreservationSweep = [&](const std::string& legacyDir, const std::string& sceneDir) {
		fs::path lPath(legacyDir);
		fs::path sPath(sceneDir);
		std::ifstream sJsonFile(sPath / "services.json");
		json sJson;
		sJsonFile >> sJson;
		std::ifstream rJsonFile(sPath / "rolling_stock.json");
		json rJson;
		rJsonFile >> rJson;

		auto hasUnit = [&](const std::string& id) {
			for (const auto& tu : rJson["train_units"]) {
				if (tu["id"] == id)
					return true;
			}
			return false;
		};

		std::vector<std::string> trains;
		std::ifstream tnFile(lPath / "trainNames.txt");
		if (tnFile.good()) {
			std::string line;
			while (std::getline(tnFile, line)) {
				std::stringstream ss(line);
				std::string t;
				ss >> t;
				if (!t.empty() && t.find("txt") != std::string::npos)
					trains.push_back(t);
				else if (!t.empty() && t.find("TXT") != std::string::npos)
					trains.push_back(t);
			}
		}

		for (const auto& tf : trains) {
			fs::path tfPath = lPath / "Trains" / tf;
			std::ifstream f(tfPath);
			std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			auto tokens = readTokens(content);
			if (tokens.size() < 7)
				continue;

			std::string sName = tokens[0];
			std::string routeId = "route" + tokens[3];
			fs::path dp(tokens[4]);
			std::string compId = dp.stem().string();

			// Expected stop sequence straight from the legacy timetable: the
			// station name is every token before the last three numbers, joined
			// without a separator, exactly as the importer writes it.
			std::string ttPathRel = tokens[6];
			if (!ttPathRel.empty() && (ttPathRel[0] == '/' || ttPathRel[0] == '\\')) {
				ttPathRel = ttPathRel.substr(1);
			}
			std::ifstream ttf(lPath / ttPathRel);
			std::vector<std::string> stations;
			std::vector<double> dwells;
			std::string ttLine;
			while (std::getline(ttf, ttLine)) {
				auto ttTokens = readTokens(ttLine);
				if (ttTokens.size() < 4)
					continue;
				std::string station = ttTokens[0];
				for (size_t i = 1; i + 3 < ttTokens.size(); ++i)
					station += ttTokens[i];
				stations.push_back(station);
				dwells.push_back(std::stod(ttTokens[ttTokens.size() - 3]));
			}

			bool foundSvc = false;
			for (const auto& s : sJson["services"]) {
				if (s["id"] == sName) {
					foundSvc = true;
					ok &= expect(s["route"] == routeId, ("Route matched for " + sName).c_str());
					ok &= expect(s["composition"] == compId, ("Composition matched for " + sName).c_str());
					bool countOk = s["stops"].size() == stations.size();
					ok &= expect(countOk, ("Stop count matched for " + sName).c_str());
					if (countOk) {
						for (size_t i = 0; i < stations.size(); ++i) {
							ok &= expect(s["stops"][i]["station"] == stations[i],
										 ("Stop " + std::to_string(i) + " station matched for " + sName).c_str());
							ok &= expect(s["stops"][i]["dwell_seconds"] == dwells[i],
										 ("Stop " + std::to_string(i) + " dwell matched for " + sName).c_str());
						}
					}
					break;
				}
			}
			ok &= expect(foundSvc, ("Service found in output for " + sName).c_str());
			ok &= expect(hasUnit(compId), ("Unit found for " + compId).c_str());
		}

		// Legacy passthrough assertions
		ok &= expect(fs::exists(sPath / "legacy"), "Legacy dir exists");
		bool hasTracklines = fs::exists(sPath / "legacy" / "Tracklines") || fs::exists(sPath / "legacy" / "TrackLines");
		ok &= expect(hasTracklines, "Tracklines legacy passthrough");

		std::ifstream sceneFile(sPath / "scene.json");
		json scJson;
		sceneFile >> scJson;
		ok &= expect(scJson.contains("legacy_source") && scJson["legacy_source"] == legacyDir, "Legacy source present in scene.json");
	};

	// 1-5. Copenhagen
	{
		TempDir outDir;
		auto res = importLegacyScene(copDir, outDir.dir, "Copenhagen");
		printErrors(res.diagnostics, "Copenhagen import");
		ok &= expect(res.success(), "Copenhagen import succeeds with zero errors");
		// The known data anomaly (E_Holte_Koge_2: departure before arrival at
		// KogeNord) must surface as an adjustment warning, not vanish silently.
		ok &= expect(hasDiag(res.diagnostics, "scene.import.adjusted", SceneSeverity::Warning),
					 "Copenhagen data anomaly reported as adjustment warning");

		auto vDiags = validateSceneDirectory(outDir.dir);
		printErrors(vDiags, "Copenhagen validate");
		ok &= expect(!hasErrors(vDiags), "Copenhagen validate zero errors");

		std::ifstream sFile(fs::path(outDir.dir) / "services.json");
		json sJson;
		sFile >> sJson;
		ok &= expect(sJson["services"].size() == 24, "Copenhagen 24 services");

		std::ifstream rFile(fs::path(outDir.dir) / "signalling.json");
		json rJson;
		rFile >> rJson;
		ok &= expect(rJson["routes"].size() == 24, "Copenhagen 24 routes");

		std::ifstream stFile(fs::path(outDir.dir) / "stations.json");
		json stJson;
		stFile >> stJson;
		ok &= expect(stJson["stations"].size() == 94, "Copenhagen 94 stations");

		std::ifstream rsFile(fs::path(outDir.dir) / "rolling_stock.json");
		json rsJson;
		rsFile >> rsJson;
		ok &= expect(rsJson["train_units"].size() > 0, "Copenhagen has train units");

		bool foundA = false;
		for (const auto& s : sJson["services"]) {
			if (s["id"] == "A-Hillerod-Hundige") {
				foundA = true;
				ok &= expect(s["route"] == "route12", "A-Hillerod-Hundige route12");
				ok &= expect(s["entry_time_seconds"] == 400.0, "entry_time_seconds 400");
				ok &= expect(s.contains("repeat") && s["repeat"]["headway_seconds"] == 1200.0, "headway 1200");
				ok &= expect(s["stops"].size() == 23, "23 stops");
				ok &= expect(s["stops"][0]["departure_seconds"] == 460.0, "First stop departure 460");
				ok &= expect(!s["stops"][0].contains("arrival_seconds"), "First stop no arrival");
				ok &= expect(s["stops"].back()["arrival_seconds"] == 4300.0, "Last stop arrival 4300");
				ok &= expect(!s["stops"].back().contains("departure_seconds"), "Last stop no departure");
				ok &= expect(s["stops"][0]["dwell_seconds"] == 20.0, "Dwell 20 on stop 0");
			}
		}
		ok &= expect(foundA, "Spot-check A-Hillerod-Hundige found");

		bool foundH = false;
		for (const auto& st : stJson["stations"]) {
			if (st["id"] == "Hillerod")
				foundH = true;
		}
		ok &= expect(foundH, "Hillerod station found");

		bool foundL = false;
		for (const auto& tu : rsJson["train_units"]) {
			if (tu["id"] == "LITRA_SA") {
				foundL = true;
				ok &= expect(tu["physical"]["length_m"] == 84.0, "LITRA_SA length 84");
				ok &= expect(tu["traction_curve"].size() > 0, "LITRA_SA has traction curve");
			}
		}
		ok &= expect(foundL, "LITRA_SA unit found");

		runPreservationSweep(copDir, outDir.dir);
	}

	// 6-7. Brescia
	{
		TempDir outDir;
		auto res = importLegacyScene(breDir, outDir.dir, "Brescia");
		printErrors(res.diagnostics, "Brescia import");
		ok &= expect(res.success(), "Brescia import succeeds with zero errors");

		auto vDiags = validateSceneDirectory(outDir.dir);
		printErrors(vDiags, "Brescia validate");
		ok &= expect(!hasErrors(vDiags), "Brescia validate zero errors");
		// 23092 has an out-of-order departure sequence in the legacy data; the
		// validator surfaces it as an ordering warning.
		ok &= expect(hasDiag(vDiags, "scene.time.order", SceneSeverity::Warning),
					 "Brescia out-of-order departures reported as warning");

		std::ifstream sFile(fs::path(outDir.dir) / "services.json");
		json sJson;
		sFile >> sJson;
		ok &= expect(sJson["services"].size() == 62, "Brescia 62 services");

		std::ifstream rFile(fs::path(outDir.dir) / "signalling.json");
		json rJson;
		rFile >> rJson;
		ok &= expect(rJson["routes"].size() == 48, "Brescia 48 routes");

		bool found10450 = false;
		int repeatCount = 0;
		for (const auto& s : sJson["services"]) {
			if (s["id"] == "10450") {
				found10450 = true;
				ok &= expect(s["entry_time_seconds"] == 115.0, "10450 entry 115");
				ok &= expect(!s.contains("repeat"), "10450 no repeat");
				ok &= expect(s["route"] == "route44", "10450 route44");
			}
			if (s.contains("repeat"))
				repeatCount++;
		}
		ok &= expect(found10450, "10450 found");
		ok &= expect(repeatCount == 2, "Exactly 2 services have repeat field");

		runPreservationSweep(breDir, outDir.dir);
	}

	// 8. Netherlands assignment source imports into a validated scene.
	{
		TempDir outDir;
		auto res = importLegacyScene(nlDir, outDir.dir, "Netherlands");
		printErrors(res.diagnostics, "Netherlands import");
		ok &= expect(res.success(), "Netherlands import succeeds with zero errors");
		ok &= expect(countDiag(res.diagnostics, "scene.import.parse", SceneSeverity::Warning) == 0,
					 "Netherlands complex route tokens are preserved without parse warnings");

		auto vDiags = validateSceneDirectory(outDir.dir);
		printErrors(vDiags, "Netherlands validate");
		ok &= expect(!hasErrors(vDiags), "Netherlands validate zero errors");

		std::ifstream sFile(fs::path(outDir.dir) / "services.json");
		json sJson;
		sFile >> sJson;
		ok &= expect(sJson["services"].size() == 8, "Netherlands 8 services");

		std::ifstream rsFile(fs::path(outDir.dir) / "rolling_stock.json");
		json rsJson;
		rsFile >> rsJson;
		ok &= expect(rsJson["train_units"].size() == 1, "Netherlands 1 train unit");
		ok &= expect(rsJson["compositions"].size() == 1, "Netherlands 1 composition");

		std::ifstream rFile(fs::path(outDir.dir) / "signalling.json");
		json rJson;
		rFile >> rJson;
		ok &= expect(rJson["routes"].size() == 74, "Netherlands 74 routes");
		ok &= expect(rJson["routes"][0]["blocks"].size() == 53, "Netherlands route0 preserves every token");
		bool route0HasSwitchToken = false;
		for (const auto& block : rJson["routes"][0]["blocks"]) {
			if (block.get<std::string>().find('/') != std::string::npos) {
				route0HasSwitchToken = true;
				break;
			}
		}
		ok &= expect(route0HasSwitchToken, "Netherlands route0 preserves switch transition token");
	}

	// 9. Error: Nonexistent directory
	{
		TempDir outDir;
		auto res = importLegacyScene("/no/such/legacy/dir", outDir.dir, "NoDir");
		ok &= expect(!res.wroteScene, "Nonexistent legacy dir wroteScene false");
		ok &= expect(!res.success(), "Nonexistent legacy dir success false");
		ok &= expect(hasDiag(res.diagnostics, "scene.import.missing"), "scene.import.missing produced");
	}

	// 10. Error: Synthetic mini legacy folder
	{
		TempDir lDir, outDir;
		fs::create_directories(fs::path(lDir.dir) / "Trains");

		std::ofstream tf1(fs::path(lDir.dir) / "Trains" / "t1.txt");
		tf1 << "Svc1 0 99999999 999 /Data.txt /Trac.txt /TT.txt\n"; // route 999
		tf1.close();

		std::ofstream tf2(fs::path(lDir.dir) / "Trains" / "t2.txt");
		tf2 << "Svc2 0 99999999\n"; // truncated
		tf2.close();

		auto res = importLegacyScene(lDir.dir, outDir.dir, "Synth");
		ok &= expect(hasDiag(res.diagnostics, "scene.import.ref"), "scene.import.ref for route out of range");
		ok &= expect(hasDiag(res.diagnostics, "scene.import.parse"), "scene.import.parse for truncated file");
	}

	// 11. Error: malformed route tokens still report parse warnings.
	{
		TempDir lDir, outDir;
		fs::create_directories(fs::path(lDir.dir) / "Routes");
		std::ofstream route(fs::path(lDir.dir) / "Routes" / "Route0.txt");
		route << "not-a-route-token\n";
		route.close();

		auto res = importLegacyScene(lDir.dir, outDir.dir, "MalformedRoute");
		ok &= expect(hasDiag(res.diagnostics, "scene.import.parse", SceneSeverity::Warning),
					 "scene.import.parse warning for malformed route token");
	}

	if (!ok)
		return 1;
	std::cout << "all SceneImporter tests passed\n";
	return 0;
}
