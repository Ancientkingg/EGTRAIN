#include "scene/SceneExporter.h"
#include "scene/SceneModel.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>

namespace fs = std::filesystem;

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

int main() {
	bool ok = true;

	// 6. Minimal fixture scene
	{
		TempDir outDir;
		const std::string fixture = (fs::path(__FILE__).parent_path() / "fixtures/scenes/minimal").string();
		auto res = exportLegacyScene(fixture, outDir.dir);
		ok &= expect(!res.success(), "Minimal scene export fails");
		ok &= expect(hasDiag(res.diagnostics, "scene.export.ref"), "scene.export.ref produced");
	}

	// 7. Legacy export preserves a slash-containing stored route token as one block reference.
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

	// 8. Non-ASCII route ids are not treated as decimal route numbers.
	{
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
		ok &= expect(res.success(), "Non-ASCII route id export succeeds");

		std::ifstream route(fs::path(outDir.dir) / "Routes" / "Route0.txt");
		std::string line;
		ok &= expect(route.good(), "Non-ASCII route id uses sequential ASCII filename");
		std::getline(route, line);
		ok &= expect(line == "@ASCII@", "Non-ASCII route id contents remain unchanged");
	}

	// 9. Nonexistent scene dir
	{
		TempDir outDir;
		auto res = exportLegacyScene("/no/such/scene", outDir.dir);
		ok &= expect(!res.success(), "Nonexistent scene export fails");
	}

	// 10. Multi-unit composition export
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

	// 11. Multi-unit composition with only degenerate traction bands fails
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

	// 12. Near-duplicate band boundaries collapse instead of forming sliver bands
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

	// 13. Synthetic GUI layout generated from Stations and NodiCumPari
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

	// 14. Scene with no legacy/TrackLines/Stations.txt exports successfully and writes no GUI/StationsCoord.txt
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

	// 15. Mirrored band listed before the reference trackline still maps by name
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

	// 16. Stations without any trackline node data generate no GUI files at all
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

	// 17. Incidents export to the legacy Incidents.txt
	{
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
		ok &= expect(res.success(), "Incidents scene export succeeds");

		std::ifstream inf(fs::path(outDir.dir) / "Incidents.txt");
		if (expect(inf.good(), "Incidents.txt exists")) {
			std::string l1, l2, l3, l4, l5, l6;
			std::getline(inf, l1);
			std::getline(inf, l2);
			std::getline(inf, l3);
			std::getline(inf, l4);
			std::getline(inf, l5);
			std::getline(inf, l6);
			ok &= expect(l1 == "signal_failure\t0-B0\t100\t300",
					("Bound signal exports its protected legacy block: " + l1).c_str());
			ok &= expect(l2 == "signal_failure\t0-B0\t301\t302",
					("Direct canonical block target remains supported: " + l2).c_str());
			ok &= expect(l3 == "signal_failure\t1-B0\t303\t304",
					("Legacy export maps the exact slash-containing block reference: " + l3).c_str());
			ok &= expect(l4 == "signal_failure\tnope\t1\t2", ("Unmatched target still written: " + l4).c_str());
			ok &= expect(l5 == "train_breakdown\tlegacy-svc\t25\t35",
					("Breakdown uses the active operating code: " + l5).c_str());
			ok &= expect(l6.empty(), "Non-default scenario incident is not exported");
		}
		ok &= expect(hasDiag(res.diagnostics, "scene.export.adjusted", SceneSeverity::Warning), "Unmatched signal target warned");
		ok &= expect(hasDiag(res.diagnostics, "scene.export.compatibility", SceneSeverity::Warning),
				"Connection-derived signal target reports the legacy format limit");
	}

	// 18. Scene without incidents writes no Incidents.txt
	{
		TempDir sceneDir, outDir;
		fs::path scene(sceneDir.dir);
		std::ofstream(scene / "scene.json") << R"({"schema_version":1,"name":"No Incidents"})" << "\n";
		std::ofstream(scene / "infrastructure.json") << R"({"nodes":[],"arcs":[]})" << "\n";
		std::ofstream(scene / "stations.json") << R"({"stations":[]})" << "\n";
		std::ofstream(scene / "signalling.json") << R"({"signals":[],"routes":[]})" << "\n";
		std::ofstream(scene / "rolling_stock.json") << R"({"train_units":[],"compositions":[]})" << "\n";
		std::ofstream(scene / "services.json") << R"({"services":[]})" << "\n";

		auto res = exportLegacyScene(sceneDir.dir, outDir.dir);
		ok &= expect(res.success(), "No Incidents scene export succeeds");
		ok &= expect(!fs::exists(fs::path(outDir.dir) / "Incidents.txt"), "No Incidents.txt written");
	}

	// 19. Canonical infrastructure exports without legacy passthrough data.
	{
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
		ok &= expect(res.success(), "Canonical infrastructure export succeeds");
		const fs::path tracklines = fs::path(outDir.dir) / "TrackLines";
		ok &= expect(fs::exists(tracklines / "B0" / "NodiCumPari.txt"), "B0 nodes exported");
		ok &= expect(fs::exists(tracklines / "B0" / "ArchiCumPari.txt"), "B0 arcs exported");
		ok &= expect(fs::exists(tracklines / "B0" / "BlockCumPari.txt"), "B0 blocks exported");
		ok &= expect(fs::exists(tracklines / "B1" / "NodiCumPari.txt"), "B1 nodes exported");
		ok &= expect(fs::exists(tracklines / "Connections.txt"), "Empty connections file exported");
		ok &= expect(fs::file_size(tracklines / "Connections.txt") == 0, "Connections file remains empty");

		std::ifstream stations(tracklines / "Stations.txt");
		std::string first, second, third, fourth, fifth;
		std::getline(stations, first);
		std::getline(stations, second);
		std::getline(stations, third);
		std::getline(stations, fourth);
		std::getline(stations, fifth);
		ok &= expect(first == "0\tOrigin", "First platform anchor exported");
		ok &= expect(second == "1\tTransfer", "Second platform anchor exported");
		ok &= expect(third == "10\tDestination", "Third platform anchor exported");
		ok &= expect(fourth == "11\tDestination", "Fourth platform anchor exported");
		ok &= expect(fifth.empty(), "Station position fallback is not duplicated");

		std::ifstream corridors(fs::path(outDir.dir) / "GUI" / "caseStudyRouteCorridors.txt");
		std::string line;
		ok &= expect(fs::exists(fs::path(outDir.dir) / "GUI" / "caseStudyRouteCorridors.txt"), "Route corridor compatibility file exists");
		std::getline(corridors, line);
		ok &= expect(line == "0\tA", "Route corridor compatibility row exported");
		std::ifstream restrictions(fs::path(outDir.dir) / "GUI" / "singleTrackLimits.txt");
		ok &= expect(fs::exists(fs::path(outDir.dir) / "GUI" / "singleTrackLimits.txt"), "Single-track compatibility file exists");
		std::getline(restrictions, line);
		ok &= expect(line == "@0-B0@\t@0-B1@\t@0-B0@-1/@0-B1@-2\t@0-B1@-3/@0-B0@-4", "Single-track restriction compatibility row exported");
		std::ifstream boundaries(fs::path(outDir.dir) / "GUI" / "stationBoundarySections.txt");
		ok &= expect(fs::exists(fs::path(outDir.dir) / "GUI" / "stationBoundarySections.txt"), "Station-boundary compatibility file exists");
		std::getline(boundaries, line);
		ok &= expect(line == "@0-B0@\t@0-B1@\t1", "Station-boundary compatibility row exported");

		std::ifstream das(fs::path(outDir.dir) / "Passengers" / "DAS_FrenchCaseStudy.csv");
		ok &= expect(fs::exists(fs::path(outDir.dir) / "Passengers" / "DAS_FrenchCaseStudy.csv"), "Passenger DAS compatibility file exists");
		std::getline(das, line);
		std::getline(das, line);
		ok &= expect(line == "1,p0,1,Work,1,Work,Destination,,PT,TRUE,1.25,,Origin,,0.25,1", "Passenger DAS compatibility row exported");
		std::ifstream routeChoice(fs::path(outDir.dir) / "Passengers" / "RouteChoiceFC_EQ1.csv");
		ok &= expect(fs::exists(fs::path(outDir.dir) / "Passengers" / "RouteChoiceFC_EQ1.csv"), "Passenger route-choice compatibility file exists");
		std::getline(routeChoice, line);
		ok &= expect(line == "person_id,destination,nb_transfers,Transfer_N1,r_service_lines_id1,r_service_lines_id2", "Passenger route-choice header exported");
		std::getline(routeChoice, line);
		ok &= expect(line == "p0,Destination,1,Transfer,svc-code-1,svc-code-1", "Passenger route-choice compatibility row exported");
		ok &= expect(!fs::exists(fs::path(outDir.dir) / "legacy"), "No legacy subtree created");
	}

	// 20. Canonical block and track IDs map consistently across legacy files.
	{
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
		ok &= expect(res.success(), "Mapped block scene export succeeds");
		std::string line;
		std::ifstream route(fs::path(outDir.dir) / "Routes" / "Route0.txt");
		std::getline(route, line);
		ok &= expect(line == "@0-B0@", "Route uses the generated legacy block ID");
		std::ifstream restriction(fs::path(outDir.dir) / "GUI" / "singleTrackLimits.txt");
		std::getline(restriction, line);
		ok &= expect(line == "@0-B0@\t@0-B0@\t@0-B0@-1\t@0-B0@-2",
				"Restriction uses the generated legacy block ID");
		std::ifstream boundary(fs::path(outDir.dir) / "GUI" / "stationBoundarySections.txt");
		std::getline(boundary, line);
		ok &= expect(line == "@0-B0@\t@0-B0@\t1", "Boundary uses the generated legacy block ID");
	}

	if (!ok)
		return 1;
	std::cout << "all SceneExporter tests passed\n";
	return 0;
}
