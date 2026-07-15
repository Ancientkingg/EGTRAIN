#include "scene/SceneImporter.h"
#include "scene/SceneModel.h"
#include "scene/SceneValidator.h"
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
	if (argc < 7) {
		std::cerr << "Usage: test_sceneimporter <copenhagen_dir> <brescia_dir> <netherlands_dir> <banedanmark_dir> <paimpol_dir> <paimpol_alt_dir>\n";
		return 1;
	}
	std::string copDir = argv[1];
	std::string breDir = argv[2];
	std::string nlDir = argv[3];
	std::string banDir = argv[4];
	std::string paimpolDir = argv[5];
	std::string paimpolAltDir = argv[6];
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
		// Several services depart their first stop before the scene base time;
		// a negative first departure is legal and is not an ordering warning.
		ok &= expect(!hasDiag(vDiags, "scene.time.order", SceneSeverity::Warning),
					 "Brescia has no departure ordering warnings");
		// Stop-less services import as through services, so a fresh import
		// carries no missing-stops warnings.
		ok &= expect(!hasDiag(vDiags, "scene.service.no_stops", SceneSeverity::Warning),
					 "Brescia import has no missing-stops warnings");

		std::ifstream sFile(fs::path(outDir.dir) / "services.json");
		json sJson;
		sFile >> sJson;
		ok &= expect(sJson["services"].size() == 62, "Brescia 62 services");
		int throughCount = 0;
		for (const auto& s : sJson["services"]) {
			if (s.value("through", false)) {
				throughCount++;
				ok &= expect(s["stops"].empty(), "through services import without stops");
			}
		}
		ok &= expect(throughCount == 22, "Brescia imports 22 through services");

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
		route << "@A@1.000/@B@2.000\n";
		route << "not-a-route-token\n";
		route.close();

		auto res = importLegacyScene(lDir.dir, outDir.dir, "MalformedRoute");
		ok &= expect(countDiag(res.diagnostics, "scene.import.parse", SceneSeverity::Warning) == 1,
					 "scene.import.parse warning only for malformed route token");

		bool malformedRowFlagged = false;
		for (const auto& d : res.diagnostics) {
			if (d.code == "scene.import.parse" && d.severity == SceneSeverity::Warning &&
				d.message == "Malformed route block token at row 1")
				malformedRowFlagged = true;
		}
		ok &= expect(malformedRowFlagged, "parse warning names the malformed token row");
	}

	// 12. Non-ASCII route filenames are not treated as decimal route numbers.
	{
		TempDir lDir, outDir;
		fs::create_directories(fs::path(lDir.dir) / "Routes");
		fs::create_directories(fs::path(lDir.dir) / "TrackLines");
		std::ofstream(lDir.dir + "/TrackLines/Stations.txt") << "0\tASCII\n";
		std::ofstream(lDir.dir + "/Routes/Route12.txt") << "@ASCII@\n";
		const std::string nonAsciiRoute = std::string("Route") + "\xC3\xA9.txt";
		std::ofstream(fs::path(lDir.dir) / "Routes" / nonAsciiRoute) << "@NonAscii@\n";

		auto res = importLegacyScene(lDir.dir, outDir.dir, "NonAsciiRouteFilename");
		printErrors(res.diagnostics, "Non-ASCII route filename import");
		ok &= expect(res.success(), "Non-ASCII route filename import succeeds");

		std::ifstream signalling(fs::path(outDir.dir) / "signalling.json");
		json signallingJson;
		signalling >> signallingJson;
		ok &= expect(signallingJson["routes"].size() == 1, "Non-ASCII route filename is skipped");
		if (!signallingJson["routes"].empty()) {
			ok &= expect(signallingJson["routes"][0]["id"] == "route12", "ASCII route filename remains accepted");
			ok &= expect(signallingJson["routes"][0]["blocks"][0] == "ASCII", "ASCII route contents remain unchanged");
		}
	}

	// 13. Flat Tracklines payloads import without inventing semantic data and
	// preserve the canonical runtime infrastructure layout.
	{
		TempDir lDir, outDir;
		fs::create_directories(fs::path(outDir.dir) / "legacy/Tracklines/B7");
		std::ofstream(fs::path(outDir.dir) / "legacy/Tracklines/B7/stale.txt") << "stale\n";
		std::ofstream(lDir.dir + "/Stations.txt") << "0\tFlatStart\n1.5\tFlatEnd\n";
		std::ofstream(lDir.dir + "/Connections.txt") << "0\t0.5\t1\t0.5\n";
		std::ofstream(lDir.dir + "/TrackandStations.txt") << "0\nFlatStart FlatEnd\n";
		fs::create_directories(fs::path(lDir.dir) / "B0");
		std::ofstream(lDir.dir + "/B0/NodiCumPari.txt") << "1\t0\t0\n2\t1\t0\n";
		std::ofstream(lDir.dir + "/unrelated.txt") << "must not be copied\n";

		auto res = importLegacyScene(lDir.dir, outDir.dir, "FlatFixture");
		printErrors(res.diagnostics, "Flat fixture import");
		ok &= expect(res.success(), "Flat fixture import succeeds");

		std::ifstream stations(fs::path(outDir.dir) / "stations.json");
		json stationsJson;
		stations >> stationsJson;
		ok &= expect(stationsJson["stations"].size() == 2, "Flat fixture stations parsed");
		ok &= expect(stationsJson["stations"][0]["id"] == "FlatStart", "Flat fixture first station parsed");

		std::ifstream services(fs::path(outDir.dir) / "services.json");
		json servicesJson;
		services >> servicesJson;
		ok &= expect(servicesJson["services"].empty(), "Flat fixture has no invented services");
		std::ifstream rolling(fs::path(outDir.dir) / "rolling_stock.json");
		json rollingJson;
		rolling >> rollingJson;
		ok &= expect(rollingJson["train_units"].empty(), "Flat fixture has no invented train units");
		ok &= expect(rollingJson["compositions"].empty(), "Flat fixture has no invented compositions");
		std::ifstream signalling(fs::path(outDir.dir) / "signalling.json");
		json signallingJson;
		signalling >> signallingJson;
		ok &= expect(signallingJson["routes"].empty(), "Flat fixture has no invented routes");

		auto validation = validateSceneDirectory(outDir.dir);
		ok &= expect(hasDiag(validation, "scene.trains.none"), "Flat fixture reports no trains semantic diagnostic");
		ok &= expect(hasDiag(validation, "scene.services.none"), "Flat fixture reports no services semantic diagnostic");
		ok &= expect(fs::exists(fs::path(outDir.dir) / "legacy/Tracklines/Stations.txt"), "Flat fixture canonical stations passthrough");
		ok &= expect(fs::exists(fs::path(outDir.dir) / "legacy/Tracklines/Connections.txt"), "Flat fixture canonical connections passthrough");
		ok &= expect(fs::exists(fs::path(outDir.dir) / "legacy/Tracklines/TrackandStations.txt"), "Flat fixture canonical track passthrough");
		ok &= expect(fs::exists(fs::path(outDir.dir) / "legacy/Tracklines/B0/NodiCumPari.txt"), "Flat fixture block passthrough");
		ok &= expect(!fs::exists(fs::path(outDir.dir) / "legacy/Tracklines/B7"), "Flat fixture removes stale block passthrough");
		ok &= expect(!fs::exists(fs::path(outDir.dir) / "legacy/unrelated.txt"), "Flat fixture excludes unrelated root files");
	}

	// 14. Reject overlapping source and destination paths before touching either.
	{
		TempDir root;
		const fs::path legacyPath = fs::path(root.dir) / "legacy";
		const fs::path scenePath = legacyPath / "nested-scene";
		fs::create_directories(fs::path(legacyPath) / "TrackLines");
		const fs::path markerPath = fs::path(legacyPath) / "source-marker.txt";
		std::ofstream(markerPath) << "source stays unchanged\n";
		std::ofstream(fs::path(legacyPath) / "TrackLines/Stations.txt") << "0\tNestedSource\n";

		auto res = importLegacyScene(legacyPath.string(), scenePath.string(), "NestedDestination");
		ok &= expect(!res.wroteScene, "Destination inside source is rejected");
		ok &= expect(hasDiag(res.diagnostics, "scene.import.path", SceneSeverity::Error),
					 "Destination inside source reports a path error");
		std::ifstream marker(markerPath);
		std::string markerContent((std::istreambuf_iterator<char>(marker)), std::istreambuf_iterator<char>());
		ok &= expect(markerContent == "source stays unchanged\n", "Destination inside source preserves source marker");
		ok &= expect(!fs::exists(scenePath), "Destination inside source does not create destination");
	}

	{
		TempDir root;
		const fs::path legacyPath = fs::path(root.dir) / "legacy";
		fs::create_directories(fs::path(legacyPath) / "TrackLines");
		const fs::path markerPath = fs::path(legacyPath) / "source-marker.txt";
		std::ofstream(markerPath) << "identical source stays unchanged\n";
		std::ofstream(fs::path(legacyPath) / "TrackLines/Stations.txt") << "0\tIdenticalSource\n";

		auto res = importLegacyScene(legacyPath.string(), legacyPath.string(), "IdenticalPaths");
		ok &= expect(!res.wroteScene, "Identical source and destination are rejected");
		ok &= expect(hasDiag(res.diagnostics, "scene.import.path", SceneSeverity::Error),
					 "Identical source and destination report a path error");
		std::ifstream marker(markerPath);
		std::string markerContent((std::istreambuf_iterator<char>(marker)), std::istreambuf_iterator<char>());
		ok &= expect(markerContent == "identical source stays unchanged\n", "Identical paths preserve source marker");
		ok &= expect(!fs::exists(legacyPath / "scene.json"), "Identical paths do not write scene");
	}

	{
		TempDir root;
		const fs::path scenePath = fs::path(root.dir) / "scene";
		const fs::path legacyPath = scenePath / "legacy";
		fs::create_directories(fs::path(legacyPath) / "TrackLines");
		const fs::path markerPath = fs::path(legacyPath) / "source-marker.txt";
		std::ofstream(markerPath) << "nested source stays unchanged\n";
		std::ofstream(fs::path(legacyPath) / "TrackLines/Stations.txt") << "0\tNestedSource\n";

		auto res = importLegacyScene(legacyPath.string(), scenePath.string(), "LegacySubdirectory");
		ok &= expect(!res.wroteScene, "Source at destination legacy is rejected");
		ok &= expect(hasDiag(res.diagnostics, "scene.import.path", SceneSeverity::Error),
					 "Source at destination legacy reports a path error");
		std::ifstream marker(markerPath);
		std::string markerContent((std::istreambuf_iterator<char>(marker)), std::istreambuf_iterator<char>());
		ok &= expect(markerContent == "nested source stays unchanged\n", "Source at destination legacy preserves marker");
		ok &= expect(!fs::exists(scenePath / "scene.json"), "Source at destination legacy does not write scene");
	}

	// A shared path prefix is not containment: sibling destinations remain valid.
	{
		TempDir root;
		const fs::path legacyPath = fs::path(root.dir) / "legacy";
		const fs::path scenePath = fs::path(root.dir) / "legacy-copy";
		fs::create_directories(fs::path(legacyPath) / "TrackLines");
		std::ofstream(fs::path(legacyPath) / "TrackLines/Stations.txt") << "0\tSiblingSource\n";

		auto res = importLegacyScene(legacyPath.string(), scenePath.string(), "SiblingDestination");
		ok &= expect(res.success(), "Sibling destination with shared prefix remains valid");
		ok &= expect(fs::exists(scenePath / "scene.json"), "Sibling destination receives scene");
	}

	// 16. Import errors preserve an existing destination and clean temporary
	// siblings instead of publishing a partial scene.
	{
		TempDir lDir, outDir;
		fs::create_directories(fs::path(lDir.dir) / "TrackLines");
		fs::create_directories(fs::path(lDir.dir) / "Trains");
		std::ofstream(lDir.dir + "/TrackLines/Stations.txt") << "0\tPreservedSource\n";
		std::ofstream(lDir.dir + "/Trains/broken.txt") << "malformed train input\n";

		fs::create_directories(fs::path(outDir.dir) / "legacy");
		std::ofstream(fs::path(outDir.dir) / "scene.json") << "existing scene bytes\n";
		std::ofstream(fs::path(outDir.dir) / "services.json") << "existing services bytes\n";
		std::ofstream(fs::path(outDir.dir) / "legacy/stale.txt") << "existing passthrough bytes\n";

		std::vector<std::string> siblingsBefore;
		for (const auto& entry : fs::directory_iterator(fs::path(outDir.dir).parent_path()))
			siblingsBefore.push_back(entry.path().filename().string());
		std::sort(siblingsBefore.begin(), siblingsBefore.end());

		auto res = importLegacyScene(lDir.dir, outDir.dir, "PreservedDestination");
		ok &= expect(!res.success(), "Malformed source import fails");
		ok &= expect(!res.wroteScene, "Failed import does not publish a scene");

		std::ifstream sceneFile(fs::path(outDir.dir) / "scene.json");
		std::string sceneBytes((std::istreambuf_iterator<char>(sceneFile)), std::istreambuf_iterator<char>());
		ok &= expect(sceneBytes == "existing scene bytes\n", "Failed import preserves existing scene bytes");
		std::ifstream servicesFile(fs::path(outDir.dir) / "services.json");
		std::string servicesBytes((std::istreambuf_iterator<char>(servicesFile)), std::istreambuf_iterator<char>());
		ok &= expect(servicesBytes == "existing services bytes\n", "Failed import preserves existing JSON bytes");
		std::ifstream staleFile(fs::path(outDir.dir) / "legacy/stale.txt");
		std::string staleBytes((std::istreambuf_iterator<char>(staleFile)), std::istreambuf_iterator<char>());
		ok &= expect(staleBytes == "existing passthrough bytes\n", "Failed import preserves existing passthrough");

		std::vector<std::string> siblingsAfter;
		for (const auto& entry : fs::directory_iterator(fs::path(outDir.dir).parent_path()))
			siblingsAfter.push_back(entry.path().filename().string());
		std::sort(siblingsAfter.begin(), siblingsAfter.end());
		ok &= expect(siblingsAfter == siblingsBefore, "Failed import cleans staging and backup siblings");
	}

	// 17. Permission errors while selecting a staging sibling return without
	// publishing or changing an existing destination.
	{
		TempDir lDir, outDir;
		fs::create_directories(fs::path(lDir.dir) / "TrackLines");
		std::ofstream(lDir.dir + "/TrackLines/Stations.txt") << "0\tPermissionSource\n";

		const fs::path destinationParent = fs::path(outDir.dir) / "restricted";
		const fs::path scenePath = destinationParent / "scene";
		fs::create_directories(scenePath);
		const fs::path markerPath = scenePath / "marker.txt";
		std::ofstream(markerPath) << "destination stays unchanged\n";

		std::error_code statusEc;
		const fs::perms originalPermissions = fs::status(destinationParent, statusEc).permissions();
		if (statusEc) {
			std::cerr << "skipped: unable to inspect destination parent permissions: " << statusEc.message() << "\n";
		} else {
			std::error_code restrictEc;
			fs::permissions(destinationParent, fs::perms::none, fs::perm_options::replace, restrictEc);
			if (restrictEc) {
				std::error_code restoreEc;
				fs::permissions(destinationParent, originalPermissions, fs::perm_options::replace, restoreEc);
				ok &= expect(!restoreEc, "Permission test restores destination parent after setup failure");
				std::cerr << "skipped: unable to remove destination parent access: " << restrictEc.message() << "\n";
			} else {
				std::error_code probeEc;
				fs::exists(destinationParent / "probe", probeEc);
				if (!probeEc) {
					std::error_code restoreEc;
					fs::permissions(destinationParent, originalPermissions, fs::perm_options::replace, restoreEc);
					ok &= expect(!restoreEc, "Permission test restores destination parent when probe succeeds");
					std::cerr << "skipped: destination parent permissions did not produce an fs::exists error\n";
				} else {
					auto res = importLegacyScene(lDir.dir, scenePath.string(), "PermissionError");
					std::error_code restoreEc;
					fs::permissions(destinationParent, originalPermissions, fs::perm_options::replace, restoreEc);
					ok &= expect(!restoreEc, "Permission test restores destination parent after import");
					if (!restoreEc) {
						ok &= expect(!res.wroteScene, "Permission-error import wroteScene false");
						ok &= expect(!res.success(), "Permission-error import success false");
						bool foundDestinationDiagnostic = false;
						for (const auto& d : res.diagnostics) {
							if (d.severity == SceneSeverity::Error && d.code == "scene.import.missing"
								&& d.file == scenePath.string()) {
								foundDestinationDiagnostic = true;
								break;
							}
						}
						ok &= expect(foundDestinationDiagnostic,
								"Permission-error import names the scene destination");
						std::ifstream marker(markerPath);
						std::string markerContent((std::istreambuf_iterator<char>(marker)), std::istreambuf_iterator<char>());
						ok &= expect(markerContent == "destination stays unchanged\n",
								"Permission-error import preserves destination marker");
					}
				}
			}
		}
	}

	// 18. Import errors identify the concrete source path that failed.
	{
		TempDir lDir, outDir;
		fs::create_directories(fs::path(lDir.dir) / "TrackLines");
		fs::create_directories(fs::path(lDir.dir) / "Trains");
		std::ofstream(lDir.dir + "/TrackLines/Stations.txt") << "0\tDiagnosticSource\n";
		std::ofstream(lDir.dir + "/Trains/reference.txt")
			<< "Svc 0 99999999 0 /Data.txt /Trac.txt /MissingTimetable.txt\n";
		std::ofstream(lDir.dir + "/Trains/traction-reference.txt")
			<< "SvcTraction 0 99999999 0 /ValidData.txt /MissingTraction.txt /MissingTimetable2.txt\n";
		std::ofstream(lDir.dir + "/ValidData.txt") << "1 2 3 4 5 6 7 8 9\n";

		auto res = importLegacyScene(lDir.dir, outDir.dir, "DiagnosticPaths");
		bool allErrorsNamePaths = true;
		for (const auto& d : res.diagnostics) {
			if (d.severity == SceneSeverity::Error && d.file.empty())
				allErrorsNamePaths = false;
		}
		ok &= expect(allErrorsNamePaths, "Every import error names a failing path");

		const fs::path missingTimetable = fs::path(lDir.dir) / "MissingTimetable.txt";
		const fs::path missingData = fs::path(lDir.dir) / "Data.txt";
		const fs::path missingTraction = fs::path(lDir.dir) / "MissingTraction.txt";
		const fs::path missingRoute = fs::path(lDir.dir) / "Routes/Route0.txt";
		bool foundTimetable = false;
		bool foundData = false;
		bool foundTraction = false;
		bool foundRoute = false;
		for (const auto& d : res.diagnostics) {
			if (d.code == "scene.import.ref" && d.message.find("Missing timetable file") != std::string::npos)
				foundTimetable = foundTimetable || d.file == missingTimetable.string();
			if (d.code == "scene.import.ref" && d.message.find("Missing train data file") != std::string::npos)
				foundData = foundData || d.file == missingData.string();
			if (d.code == "scene.import.ref" && d.message.find("Missing traction file") != std::string::npos)
				foundTraction = foundTraction || d.file == missingTraction.string();
			if (d.code == "scene.import.ref" && d.message.find("Missing route file") != std::string::npos)
				foundRoute = foundRoute || d.file == missingRoute.string();
		}
		ok &= expect(foundTimetable, "Missing timetable diagnostic names referenced timetable path");
		ok &= expect(foundData, "Missing train data diagnostic names referenced data path");
		ok &= expect(foundTraction, "Missing traction diagnostic names referenced traction path");
		ok &= expect(foundRoute, "Missing route diagnostic names referenced route path");
	}

	// 19. Every committed legacy input, including the Banedanmark alias and
	// both Paimpol variants, keeps the importer output shape.
	auto checkCommittedImport = [&](const std::string& legacyDir, const std::string& sceneName,
								   size_t stationCount, size_t unitCount, size_t serviceCount, size_t routeCount) {
		TempDir outDir;
		auto res = importLegacyScene(legacyDir, outDir.dir, sceneName);
		printErrors(res.diagnostics, (sceneName + " import").c_str());
		ok &= expect(res.success(), (sceneName + " import succeeds").c_str());

		std::ifstream sceneFile(fs::path(outDir.dir) / "scene.json");
		json sceneJson;
		sceneFile >> sceneJson;
		ok &= expect(sceneJson["name"] == sceneName, (sceneName + " scene name preserved").c_str());
		std::ifstream stationsFile(fs::path(outDir.dir) / "stations.json");
		json stationsJson;
		stationsFile >> stationsJson;
		std::ifstream rollingFile(fs::path(outDir.dir) / "rolling_stock.json");
		json rollingJson;
		rollingFile >> rollingJson;
		std::ifstream servicesFile(fs::path(outDir.dir) / "services.json");
		json servicesJson;
		servicesFile >> servicesJson;
		std::ifstream signallingFile(fs::path(outDir.dir) / "signalling.json");
		json signallingJson;
		signallingFile >> signallingJson;
		ok &= expect(stationsJson["stations"].size() == stationCount, (sceneName + " station output shape").c_str());
		ok &= expect(rollingJson["train_units"].size() == unitCount, (sceneName + " train-unit output shape").c_str());
		ok &= expect(rollingJson["compositions"].size() == unitCount, (sceneName + " composition output shape").c_str());
		ok &= expect(servicesJson["services"].size() == serviceCount, (sceneName + " service output shape").c_str());
		ok &= expect(signallingJson["routes"].size() == routeCount, (sceneName + " route output shape").c_str());
		ok &= expect(fs::exists(fs::path(outDir.dir) / "legacy"), (sceneName + " legacy passthrough exists").c_str());
	};
	checkCommittedImport(banDir, "Banedanmark", 94, 1, 24, 24);
	checkCommittedImport(paimpolDir, "Paimpol", 10, 1, 2, 15);
	checkCommittedImport(paimpolAltDir, "Paimpol alternative journeys", 10, 1, 1, 14);

	if (!ok)
		return 1;
	std::cout << "all SceneImporter tests passed\n";
	return 0;
}
