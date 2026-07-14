#include "scene/SceneModel.h"
#include "scene/SceneValidator.h"
#include <iostream>
#include <filesystem>
#include <fstream>
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

static bool expectDiag(const std::vector<SceneDiagnostic>& diags, const std::string& code, SceneSeverity sev = SceneSeverity::Error) {
	if (!hasDiag(diags, code, sev)) {
		std::cerr << "failed: expected diagnostic " << code << "\n";
		return false;
	}
	return true;
}

// Copy of the fixture scene in a unique temp directory, removed on scope exit.
struct TempScene {
	std::string dir;
	explicit TempScene(const std::string& fixtureDir) {
		static int counter = 0;
		auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		fs::path temp = fs::temp_directory_path() /
						("scene_test_" + std::to_string(stamp) + "_" + std::to_string(counter++));
		fs::create_directories(temp);
		fs::copy(fixtureDir, temp, fs::copy_options::recursive);
		dir = temp.string();
	}
	~TempScene() {
		std::error_code ec;
		fs::remove_all(dir, ec);
	}
};

static void modifyFile(const std::string& dir, const std::string& filename, std::function<void(json&)> modifier) {
	fs::path path = fs::path(dir) / filename;
	std::ifstream in(path);
	json j;
	in >> j;
	in.close();
	modifier(j);
	std::ofstream out(path);
	out << j.dump(4);
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: test_scenevalidator <fixture_dir>\n";
		return 1;
	}
	std::string fixtureDir = argv[1];
	bool ok = true;

	// 1. valid fixture
	auto diags = validateSceneDirectory(fixtureDir);
	ok &= expect(expectDiag(diags, "scene.train.traction.empty"), "bare fixture reports empty traction data");

	// 2. nonexistent directory
	diags = validateSceneDirectory("/no/such/scene/dir");
	ok &= expect(expectDiag(diags, "scene.dir.missing"), "scene.dir.missing on nonexistent directory");

	// 3. deleted services.json
	{
		TempScene tmp(fixtureDir);
		fs::remove(fs::path(tmp.dir) / "services.json");
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.file.missing"), "scene.file.missing for services.json");
	}

	// 4. truncated JSON in stations.json
	{
		TempScene tmp(fixtureDir);
		std::ofstream out(fs::path(tmp.dir) / "stations.json", std::ios::trunc);
		out << "{ \"stations\": ["; // Truncated
		out.close();
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.json.parse"), "scene.json.parse for truncated JSON");
	}

	// 5. scene.json without schema_version
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "scene.json", [](json& j) { j.erase("schema_version"); });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.version.missing"), "scene.version.missing");
	}

	// 6. schema_version 999
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "scene.json", [](json& j) { j["schema_version"] = 999; });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.version.unsupported"), "scene.version.unsupported");
	}

	// 7. stop with unknown station id
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "services.json", [](json& j) { j["services"][0]["stops"][0]["station"] = "unknown_st"; });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.ref.unresolved"), "unknown station -> scene.ref.unresolved");
		bool found = false;
		for (const auto& d : diags) {
			if (d.code == "scene.ref.unresolved" && !d.itemId.empty() && d.relatedId == "unknown_st") {
				found = true;
			}
		}
		ok &= expect(found, "unresolved station diagnostic carries itemId and relatedId");
	}

	// 8. service with unknown composition
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "services.json", [](json& j) { j["services"][0]["composition"] = "unknown_comp"; });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.ref.unresolved"), "unknown comp -> scene.ref.unresolved");
	}

	// 9. platform not on stop's station
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "services.json", [](json& j) { j["services"][0]["stops"][0]["platform"] = "unknown_plat"; });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.ref.platform"), "unknown platform -> scene.ref.platform");
	}

	// 10. compositions emptied; services emptied
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "rolling_stock.json", [](json& j) { j["compositions"] = json::array(); j["train_units"] = json::array(); });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.trains.none"), "scene.trains.none");

		modifyFile(tmp.dir, "services.json", [](json& j) { j["services"] = json::array(); });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.services.none"), "scene.services.none");
	}

	// 11. empty stops and the through flag
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "services.json", [](json& j) {
			j["services"][0]["stops"] = json::array();
			j["services"][0]["through"] = true;
		});
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(!hasDiag(diags, "scene.service.no_stops", SceneSeverity::Warning), "empty stops with through=true -> no scene.service.no_stops");
	}
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "services.json", [](json& j) { j["services"][0]["stops"] = json::array(); });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.service.no_stops", SceneSeverity::Warning), "empty stops without through -> scene.service.no_stops");
	}
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "services.json", [](json& j) { j["services"][0]["through"] = true; });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.service.through_stops", SceneSeverity::Warning), "through=true with stops -> scene.service.through_stops");
	}

	// 11b. departure order: a negative first departure is legal, a decreasing departure is not
	{
		TempScene tmp(fixtureDir);
		std::string id0, id1;
		modifyFile(tmp.dir, "services.json", [&](json& j) {
			id0 = j["services"][0]["id"].get<std::string>();
			id1 = j["services"][1]["id"].get<std::string>();
			j["services"][0]["stops"][0]["departure_seconds"] = -600;
			j["services"][0]["stops"][1]["arrival_seconds"] = 240;
			j["services"][0]["stops"][1]["departure_seconds"] = 300;
			j["services"][1]["stops"][1]["arrival_seconds"] = 240;
			j["services"][1]["stops"][1]["departure_seconds"] = 300;
		});
		diags = validateSceneDirectory(tmp.dir);
		bool orderDiagFirst = false;
		bool orderDiagSecond = false;
		for (const auto& d : diags) {
			if (d.code != "scene.time.order")
				continue;
			if (d.itemId == id0)
				orderDiagFirst = true;
			if (d.itemId == id1)
				orderDiagSecond = true;
		}
		ok &= expect(!orderDiagFirst, "negative first departure -> no scene.time.order");
		ok &= expect(orderDiagSecond, "departure before previous stop -> scene.time.order");
	}

	// 12. departure < arrival; negative dwell
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "services.json", [](json& j) {
			j["services"][0]["stops"][0]["arrival_seconds"] = 100.0;
			j["services"][0]["stops"][0]["departure_seconds"] = 90.0;
			j["services"][0]["stops"][0]["dwell_seconds"] = -5.0;
		});
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.time.invalid"), "departure < arrival -> scene.time.invalid");
		ok &= expect(expectDiag(diags, "scene.dwell.invalid"), "negative dwell -> scene.dwell.invalid");
		bool pathFound = false;
		for (const auto& d : diags) {
			if (d.code == "scene.time.invalid" && d.path.find("stops[0]") != std::string::npos) {
				pathFound = true;
			}
		}
		ok &= expect(pathFound, "time diagnostic names the stop in its path");
	}

	// 13. dwell larger than departure-arrival window
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "services.json", [](json& j) {
			j["services"][0]["stops"][0]["arrival_seconds"] = 100.0;
			j["services"][0]["stops"][0]["departure_seconds"] = 120.0;
			j["services"][0]["stops"][0]["dwell_seconds"] = 30.0;
		});
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.dwell.exceeds_window", SceneSeverity::Warning), "dwell > window -> scene.dwell.exceeds_window Warning");
	}

	// 14. incident with unknown target
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "incidents.json", [](json& j) { j["incidents"][0]["target"] = "unknown_sig"; });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.ref.unresolved"), "incident unknown target -> scene.ref.unresolved");
		bool found = false;
		for (const auto& d : diags) {
			if (d.code == "scene.ref.unresolved" && d.itemType == "incident") {
				found = true;
			}
		}
		ok &= expect(found, "unresolved incident target has itemType incident");
	}

	// 15. composition with unknown unit id; composition with units: []; route with blocks: []
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "rolling_stock.json", [](json& j) {
			j["compositions"][0]["units"][0] = "unknown_unit";
		});
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.ref.unresolved"), "unknown unit -> scene.ref.unresolved");

		modifyFile(tmp.dir, "rolling_stock.json", [](json& j) { j["compositions"][0]["units"] = json::array(); });
		modifyFile(tmp.dir, "signalling.json", [](json& j) { j["routes"][0]["blocks"] = json::array(); });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.composition.empty"), "scene.composition.empty");
		ok &= expect(expectDiag(diags, "scene.route.empty"), "scene.route.empty");
	}

	// 16. cascade policy: scene with a structural error -> no semantic ones
	{
		TempScene tmp(fixtureDir);
		std::ofstream out(fs::path(tmp.dir) / "services.json", std::ios::trunc);
		out << "{ bad json }";
		out.close();
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.json.parse"), "cascade policy structural error present");
		ok &= expect(!hasDiag(diags, "scene.services.none", SceneSeverity::Error), "semantic errors skipped");
	}

	// 17. mistyped required section
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "services.json", [](json& j) { j["services"] = json::object(); });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.section.missing"), "mistyped services section -> scene.section.missing");
	}

	// 18. missing required fields; arrival is optional on any stop
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "services.json", [](json& j) {
			j["services"][0]["stops"][0].erase("departure_seconds"); // first stop -> missing
			j["services"][0]["stops"][1].erase("arrival_seconds");	 // allowed: no scheduled arrival
		});
		modifyFile(tmp.dir, "scene.json", [](json& j) { j.erase("name"); });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.field.missing"), "missing fields -> scene.field.missing");
		int fieldErrors = 0;
		for (const auto& d : diags) {
			if (d.code == "scene.field.missing")
				++fieldErrors;
		}
		ok &= expect(fieldErrors == 2, "missing departure and name reported; omitted arrival is valid");
	}

	// 18b. repeat headway <= 0
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "services.json", [](json& j) {
			j["services"][1]["repeat"]["headway_seconds"] = 0;
		});
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.repeat.invalid"), "headway <= 0 -> scene.repeat.invalid");
	}

	// 19. unknown incident type
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "incidents.json", [](json& j) { j["incidents"][0]["type"] = "signal_flicker"; });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.incident.type"), "unknown incident type -> scene.incident.type");
	}

	// 20. unsupported units
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "scene.json", [](json& j) { j["units"]["time"] = "min"; });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.units.unsupported"), "unsupported unit -> scene.units.unsupported");
	}

	// 21. Diagnostic unit checks
	SceneDiagnostic unitDiag;
	unitDiag.severity = SceneSeverity::Warning;
	unitDiag.code = "test.code";
	unitDiag.message = "Test message";
	unitDiag.file = "test.json";
	unitDiag.itemType = "service";
	unitDiag.itemId = "svc1";
	unitDiag.path = "services[0].stops[0]";
	unitDiag.relatedId = "st1";
	unitDiag.suggestedFix = "Change to st2";

	std::string text = toDisplayText(unitDiag);
	ok &= expect(text.find("warning") != std::string::npos, "text contains severity");
	ok &= expect(text.find("test.code") != std::string::npos, "text contains code");
	ok &= expect(text.find("svc1") != std::string::npos, "text contains itemId");
	ok &= expect(text.find("services[0].stops[0]") != std::string::npos, "text contains path");
	ok &= expect(text.find("Change to st2") != std::string::npos, "text contains fix");

	json jDiag = toJson(unitDiag);
	ok &= expect(jDiag["severity"] == "warning", "json severity");
	ok &= expect(jDiag["code"] == "test.code", "json code");
	ok &= expect(jDiag["relatedId"] == "st1", "json relatedId");
	ok &= expect(jDiag["path"] == "services[0].stops[0]", "json path");

	std::vector<SceneDiagnostic> errs = {unitDiag};
	ok &= expect(!hasErrors(errs), "warning is not an error");
	unitDiag.severity = SceneSeverity::Error;
	errs = {unitDiag};
	ok &= expect(hasErrors(errs), "error is an error");

	// 22. In-memory SceneModel: countDiagnostics on a clean model vs. one with
	// a service referencing a nonexistent composition and route.
	{
		auto buildCleanModel = []() {
			SceneModel scene;
			scene.schemaVersion = 1;
			scene.name = "unit_test_scene";

			SceneStation station;
			station.id = "st1";
			station.name = "Station One";
			ScenePlatform platform;
			platform.id = "p1";
			station.platforms.push_back(platform);
			scene.stations.push_back(station);

			SceneTrainUnit unit;
			unit.id = "tu1";
			unit.tractionCurve.push_back({{0.0, 1.0, 1.0, 0.0, 0.0}});
			scene.trainUnits.push_back(unit);

			SceneComposition comp;
			comp.id = "comp1";
			comp.units.push_back("tu1");
			scene.compositions.push_back(comp);

			SceneRoute route;
			route.id = "r1";
			route.blocks.push_back("b1");
			scene.routes.push_back(route);

			SceneService service;
			service.id = "svc1";
			service.composition = "comp1";
			service.route = "r1";
			SceneStop stop;
			stop.stationId = "st1";
			stop.platformId = "p1";
			stop.hasArrival = true;
			stop.arrivalSeconds = 100.0;
			stop.hasDeparture = true;
			stop.departureSeconds = 200.0;
			stop.dwellSeconds = 50.0;
			service.stops.push_back(stop);
			scene.services.push_back(service);

			return scene;
		};

		SceneModel clean = buildCleanModel();
		auto cleanDiags = validateScene(clean);
		SceneDiagnosticCounts cleanCounts = countDiagnostics(cleanDiags);
		ok &= expect(cleanCounts.errors == 0, "clean in-memory model has zero errors");

		SceneModel broken = buildCleanModel();
		broken.services[0].composition = "unknown_comp";
		broken.services[0].route = "unknown_route";
		auto brokenDiags = validateScene(broken);
		ok &= expect(hasErrors(brokenDiags), "broken in-memory model reports at least one error");
		SceneDiagnosticCounts brokenCounts = countDiagnostics(brokenDiags);
		ok &= expect(brokenCounts.errors >= 2, "unknown composition and route each report an error");
		ok &= expect(expectDiag(brokenDiags, "scene.ref.unresolved"), "unknown composition/route -> scene.ref.unresolved");
	}

	// 23. train-unit traction curve semantics
	{
		auto buildTractionModel = [](const std::vector<std::array<double, 5>>& curve) {
			SceneModel scene;
			scene.schemaVersion = 1;
			SceneTrainUnit unit;
			unit.id = "tu_curve";
			unit.hasPhysical = false;
			unit.tractionCurve = curve;
			scene.trainUnits.push_back(unit);
			SceneComposition composition;
			composition.id = "comp_curve";
			composition.units.push_back(unit.id);
			scene.compositions.push_back(composition);
			SceneRoute route;
			route.id = "route_curve";
			route.blocks.push_back("block_curve");
			scene.routes.push_back(route);
			SceneService service;
			service.id = "svc_curve";
			service.composition = composition.id;
			service.route = route.id;
			service.through = true;
			scene.services.push_back(service);
			return scene;
		};

		auto diagsFor = [&](const std::vector<std::array<double, 5>>& curve) {
			return validateScene(buildTractionModel(curve));
		};
		std::vector<std::array<double, 5>> emptyCurve;
		ok &= expect(expectDiag(diagsFor(emptyCurve), "scene.train.traction.empty"),
				"empty traction curve -> scene.train.traction.empty");
		ok &= expect(expectDiag(diagsFor({{{10.0, 10.0, 1.0, 0.0, 0.0}}}), "scene.train.traction.interval"),
				"degenerate traction interval -> scene.train.traction.interval");
		ok &= expect(expectDiag(diagsFor({{{10.0, 20.0, 1.0, 0.0, 0.0}}, {{0.0, 5.0, 1.0, 0.0, 0.0}}}),
					"scene.train.traction.order"),
				"descending traction rows -> scene.train.traction.order");
		ok &= expect(expectDiag(diagsFor({{{0.0, 10.0, 1.0, 0.0, 0.0}}, {{5.0, 20.0, 1.0, 0.0, 0.0}}}),
					"scene.train.traction.overlap"),
				"overlapping traction rows -> scene.train.traction.overlap");
		const auto adjacentDiags = diagsFor({{{0.0, 10.0, 1.0, 0.0, 0.0}}, {{10.0, 20.0, 1.0, 0.0, 0.0}}});
		ok &= expect(!hasDiag(adjacentDiags, "scene.train.traction.empty")
				&& !hasDiag(adjacentDiags, "scene.train.traction.interval")
				&& !hasDiag(adjacentDiags, "scene.train.traction.order")
				&& !hasDiag(adjacentDiags, "scene.train.traction.overlap"),
				"adjacent traction rows are valid");
	}

	if (!ok)
		return 1;

	std::cout << "all SceneValidator tests passed\n";
	return 0;
}
