#include "../SceneModel.h"
#include "../SceneValidator.h"
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
	ok &= expect(!hasErrors(diags), "valid fixture has zero error diagnostics");

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

	// 11. service with stops: []
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "services.json", [](json& j) { j["services"][0]["stops"] = json::array(); });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.service.no_stops"), "scene.service.no_stops");
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

	// 18. missing required fields
	{
		TempScene tmp(fixtureDir);
		modifyFile(tmp.dir, "services.json", [](json& j) {
			j["services"][0]["stops"][0].erase("departure_seconds");
			j["services"][0]["stops"][1].erase("arrival_seconds"); // not the first stop
		});
		modifyFile(tmp.dir, "scene.json", [](json& j) { j.erase("name"); });
		diags = validateSceneDirectory(tmp.dir);
		ok &= expect(expectDiag(diags, "scene.field.missing"), "missing fields -> scene.field.missing");
		int fieldErrors = 0;
		for (const auto& d : diags) {
			if (d.code == "scene.field.missing")
				++fieldErrors;
		}
		ok &= expect(fieldErrors == 3, "missing departure, later-stop arrival, and name each reported");
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

	if (!ok)
		return 1;

	std::cout << "all SceneValidator tests passed\n";
	return 0;
}
