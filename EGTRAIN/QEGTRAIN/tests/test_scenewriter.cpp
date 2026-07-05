#include "scene/SceneModel.h"
#include "scene/SceneWriter.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static void printErrors(const std::vector<SceneDiagnostic>& diags, const char* label) {
	for (const auto& d : diags) {
		if (d.severity == SceneSeverity::Error)
			std::cerr << label << ": " << toDisplayText(d) << "\n";
	}
}

struct TempDir {
	std::string dir;

	TempDir() {
		static int counter = 0;
		auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		fs::path temp = fs::temp_directory_path() / ("scene_writer_test_" + std::to_string(stamp) + "_" + std::to_string(counter++));
		fs::create_directories(temp);
		dir = temp.string();
	}

	~TempDir() {
		std::error_code ec;
		fs::remove_all(dir, ec);
	}
};

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: test_scenewriter <scenes_dir>\n";
		return 1;
	}

	bool ok = true;
	fs::path sceneDir = fs::path(argv[1]) / "Assignment_Gvc_Gdg_Ut";
	auto loadRes = loadScene(sceneDir.string());
	printErrors(loadRes.diagnostics, "load");
	ok &= expect(!hasErrors(loadRes.diagnostics), "assignment scene loads without structural errors");

	const SceneModel& scene = loadRes.scene;
	ok &= expect(scene.schemaVersion == 1, "schema version loaded");
	ok &= expect(scene.name == "Assignment Gvc-Gdg-Ut", "scene name loaded");
	ok &= expect(scene.description == "Den Haag Centraal - Gouda - Utrecht Centraal corridor", "description loaded");
	ok &= expect(scene.baseTime == "07:00:00", "base_time loaded");
	ok &= expect(scene.stations.size() == 3, "station count loaded");
	ok &= expect(scene.routes.size() == 2, "route count loaded");
	ok &= expect(scene.services.size() == 8, "service count loaded");
	ok &= expect(scene.trainUnits.size() == 1, "train unit count loaded");
	ok &= expect(scene.compositions.size() == 1, "composition count loaded");

	TempDir outDir;
	auto saveRes = saveScene(scene, outDir.dir);
	printErrors(saveRes.diagnostics, "save");
	ok &= expect(saveRes.success(), "first save succeeds");
	ok &= expect(!fs::exists(fs::path(outDir.dir) / "legacy"), "saveScene does not create legacy folder");

	auto reloadRes = loadScene(outDir.dir);
	printErrors(reloadRes.diagnostics, "reload");
	ok &= expect(!hasErrors(reloadRes.diagnostics), "saved scene reloads without structural errors");

	const SceneModel& reloaded = reloadRes.scene;
	ok &= expect(reloaded.schemaVersion == 1, "schema version round-trips");
	ok &= expect(reloaded.name == scene.name, "scene name round-trips");
	ok &= expect(reloaded.description == scene.description, "description round-trips");
	ok &= expect(reloaded.baseTime == scene.baseTime, "base_time round-trips");
	ok &= expect(reloaded.stations.size() == scene.stations.size(), "station count round-trips");
	ok &= expect(reloaded.routes.size() == scene.routes.size(), "route count round-trips");
	ok &= expect(reloaded.services.size() == scene.services.size(), "service count round-trips");
	ok &= expect(reloaded.trainUnits.size() == scene.trainUnits.size(), "train unit count round-trips");
	ok &= expect(reloaded.compositions.size() == scene.compositions.size(), "composition count round-trips");

	if (!reloaded.stations.empty()) {
		ok &= expect(reloaded.stations[0].id == "Gvc", "first station id round-trips");
		ok &= expect(reloaded.stations[0].name == "Den Haag Centraal", "first station name round-trips");
		ok &= expect(reloaded.stations[0].platforms.size() == 2, "first station platforms round-trip");
		ok &= expect(reloaded.stations[0].platforms[0].id == "p1", "first station platform id round-trips");
	}
	if (!reloaded.routes.empty()) {
		ok &= expect(reloaded.routes[0].id == "route0", "first route id round-trips");
		ok &= expect(reloaded.routes[0].blocks.size() == 32, "first route blocks round-trip");
		ok &= expect(reloaded.routes[0].blocks[0] == "0-B0", "first route block id round-trips");
	}
	if (!reloaded.services.empty()) {
		const SceneService& service = reloaded.services[0];
		ok &= expect(service.id == "svc_e1", "first service id round-trips");
		ok &= expect(service.composition == "sprinter_single", "first service composition round-trips");
		ok &= expect(service.route == "route0", "first service route round-trips");
		ok &= expect(service.hasEntryTime && service.entryTimeSeconds == 240, "entry time round-trips");
		ok &= expect(!service.hasRepeat, "missing repeat flag round-trips");
		ok &= expect(service.stops.size() == 3, "first service stop count round-trips");
		if (service.stops.size() >= 3) {
			ok &= expect(service.stops[0].stationId == "Gvc", "first stop station round-trips");
			ok &= expect(!service.stops[0].hasArrival, "first stop missing arrival round-trips");
			ok &= expect(service.stops[0].hasDeparture && service.stops[0].departureSeconds == 300, "first stop departure round-trips");
			ok &= expect(service.stops[1].hasArrival && service.stops[1].arrivalSeconds == 1260, "middle stop arrival round-trips");
			ok &= expect(service.stops[1].hasDeparture && service.stops[1].departureSeconds == 1320, "middle stop departure round-trips");
			ok &= expect(service.stops[2].hasArrival && service.stops[2].arrivalSeconds == 2520, "last stop arrival round-trips");
			ok &= expect(!service.stops[2].hasDeparture, "last stop missing departure round-trips");
		}
	}

	auto secondSaveRes = saveScene(reloaded, outDir.dir);
	printErrors(secondSaveRes.diagnostics, "second save");
	ok &= expect(secondSaveRes.success(), "second save into same directory succeeds");
	ok &= expect(!fs::exists(fs::path(outDir.dir) / "legacy"), "second save does not create legacy folder");

	{
		SceneModel bareScene;
		bareScene.schemaVersion = 1;
		bareScene.name = "Bare";

		SceneTrainUnit bareUnit;
		bareUnit.id = "bare_unit";
		bareUnit.hasPhysical = false;
		bareUnit.tractionCurve.clear();
		bareScene.trainUnits.push_back(bareUnit);

		SceneComposition comp;
		comp.id = "bare_comp";
		comp.units.push_back("bare_unit");
		bareScene.compositions.push_back(comp);

		TempDir bareOut;
		auto bareSaveRes = saveScene(bareScene, bareOut.dir);
		ok &= expect(bareSaveRes.success(), "bare scene save succeeds");

		std::ifstream inFile(fs::path(bareOut.dir) / "rolling_stock.json");
		nlohmann::json rollingStock;
		inFile >> rollingStock;
		auto unitArray = rollingStock["train_units"];
		ok &= expect(unitArray.is_array() && unitArray.size() == 1, "wrote one unit array");
		if (unitArray.is_array() && unitArray.size() == 1) {
			auto obj = unitArray[0];
			ok &= expect(!obj.contains("physical"), "physical block absent");
			ok &= expect(!obj.contains("traction_curve"), "traction_curve absent");
		}
	}

	if (!ok)
		return 1;
	std::cout << "all SceneWriter tests passed\n";
	return 0;
}
