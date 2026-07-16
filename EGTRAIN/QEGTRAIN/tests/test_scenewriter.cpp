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

static const SceneLoadedData* findLoadedData(const SceneModel& scene, const std::string& category) {
	for (const auto& item : scene.loadedData) {
		if (item.category == category)
			return &item;
	}
	return nullptr;
}

static const SceneLoadedData* findLoadedDataChild(const SceneLoadedData* parent, const std::string& category) {
	if (!parent)
		return nullptr;
	for (const auto& item : parent->children) {
		if (item.category == category)
			return &item;
	}
	return nullptr;
}

static const SceneLoadedData* findLoadedDataChildSource(const SceneLoadedData* parent, const std::string& sourceFile) {
	if (!parent)
		return nullptr;
	for (const auto& item : parent->children) {
		if (item.sourceFile == sourceFile)
			return &item;
	}
	return nullptr;
}

struct ExpectedStop {
	const char* station;
	double dwell;
	bool hasArrival;
	double arrival;
	bool hasDeparture;
	double departure;
};

struct ExpectedService {
	const char* id;
	const char* route;
	double entry;
	const ExpectedStop* stops;
	size_t stopCount;
};

static bool expectAssignmentTimetable(const SceneModel& scene) {
	static const ExpectedStop ic1723Stops[] = {
		{"Gvc", 0, false, 0, true, 480},
		{"Gdg", 60, true, 1440, true, 1500},
		{"Ut", 0, true, 2580, false, 0},
	};
	static const ExpectedStop s19825Stops[] = {
		{"Gvc", 0, false, 0, true, 660},
		{"Gdg", 20, true, 2280, false, 0},
	};
	static const ExpectedStop ic2025Stops[] = {
		{"Gvc", 0, false, 0, true, 1380},
		{"Gdg", 60, true, 2340, true, 2400},
		{"Ut", 0, true, 3480, false, 0},
	};
	static const ExpectedStop s9827Stops[] = {
		{"Gvc", 0, false, 0, true, 1560},
		{"Gdg", 20, true, 3180, true, 3540},
		{"Ut", 0, true, 4920, false, 0},
	};
	static const ExpectedService expected[] = {
		{"IC1723", "route0", 420, ic1723Stops, 3},
		{"S19825", "route0", 600, s19825Stops, 2},
		{"IC2025", "route0", 1320, ic2025Stops, 3},
		{"S9827", "route0", 1500, s9827Stops, 3},
	};

	bool ok = expect(scene.services.size() == 4, "assignment timetable service count is 4");
	const size_t count = scene.services.size() < 4 ? scene.services.size() : 4;
	for (size_t i = 0; i < count; ++i) {
		const SceneService& service = scene.services[i];
		const ExpectedService& want = expected[i];
		ok &= expect(service.id == want.id, "service id and order match assignment timetable");
		ok &= expect(service.route == want.route, "service route matches assignment timetable");
		ok &= expect(service.composition == "sprinter_single", "service composition matches assignment timetable");
		ok &= expect(service.hasEntryTime && service.entryTimeSeconds == want.entry, "service entry time matches assignment timetable");
		ok &= expect(service.hasRepeat && service.headwaySeconds == 1800, "service repeat headway matches assignment timetable");
		ok &= expect(service.stops.size() == want.stopCount, "service stop shape matches assignment timetable");
		const size_t stopCount = service.stops.size() < want.stopCount ? service.stops.size() : want.stopCount;
		for (size_t j = 0; j < stopCount; ++j) {
			const SceneStop& stop = service.stops[j];
			const ExpectedStop& expectedStop = want.stops[j];
			ok &= expect(stop.stationId == expectedStop.station, "stop station matches assignment timetable");
			ok &= expect(stop.dwellSeconds == expectedStop.dwell, "stop dwell matches assignment timetable");
			ok &= expect(stop.hasArrival == expectedStop.hasArrival, "stop arrival shape matches assignment timetable");
			ok &= expect(!stop.hasArrival || stop.arrivalSeconds == expectedStop.arrival, "stop arrival matches assignment timetable");
			ok &= expect(stop.hasDeparture == expectedStop.hasDeparture, "stop departure shape matches assignment timetable");
			ok &= expect(!stop.hasDeparture || stop.departureSeconds == expectedStop.departure, "stop departure matches assignment timetable");
		}
	}
	return ok;
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
	ok &= expect(!scene.loadedData.empty(), "loaded data summary present");
	const SceneLoadedData* sceneData = findLoadedData(scene, "scene");
	ok &= expect(sceneData && sceneData->sourceFile == "scene.json" && sceneData->parsedCount == 1 && sceneData->status == "loaded", "scene loaded data summary");
	const SceneLoadedData* stationData = findLoadedData(scene, "stations");
	ok &= expect(stationData && stationData->sourceFile == "stations.json" && stationData->parsedCount == 3 && stationData->status == "loaded", "stations loaded data summary");
	const SceneLoadedData* stationRawData = findLoadedDataChild(stationData, "raw_file");
	ok &= expect(stationRawData && stationRawData->sourceFile == "stations.json" && stationRawData->parsedCount == 1 && stationRawData->status == "loaded", "stations raw file drilldown");
	const SceneLoadedData* stationParsedData = findLoadedDataChild(stationData, "parsed_objects");
	ok &= expect(stationParsedData && stationParsedData->sourceFile == "stations.json" && stationParsedData->parsedCount == 3 && stationParsedData->status == "loaded", "stations parsed object drilldown");
	const SceneLoadedData* timetableData = findLoadedData(scene, "timetable");
	ok &= expect(timetableData && timetableData->sourceFile == "services.json" && timetableData->parsedCount == 4 && timetableData->status == "loaded", "timetable loaded data summary");
	const SceneLoadedData* timetableRawData = findLoadedDataChild(timetableData, "raw_file");
	ok &= expect(timetableRawData && timetableRawData->sourceFile == "services.json" && timetableRawData->parsedCount == 1 && timetableRawData->status == "loaded", "timetable raw file drilldown");
	const SceneLoadedData* timetableParsedData = findLoadedDataChild(timetableData, "parsed_objects");
	ok &= expect(timetableParsedData && timetableParsedData->sourceFile == "services.json" && timetableParsedData->parsedCount == 4 && timetableParsedData->status == "loaded", "timetable parsed object drilldown");
	const SceneLoadedData* passengerData = findLoadedData(scene, "passenger_data");
	ok &= expect(passengerData && passengerData->status == "unimplemented", "passenger data visible as unimplemented");
	const SceneLoadedData* signallingData = findLoadedData(scene, "signalling");
	ok &= expect(signallingData && signallingData->sourceFile == "signalling.json" && signallingData->parsedCount == 2 && signallingData->status == "loaded", "signalling loaded data summary");
	const SceneLoadedData* signalsData = findLoadedDataChild(signallingData, "signals");
	ok &= expect(signalsData && signalsData->sourceFile == "signalling.json" && signalsData->parsedCount == 0 && signalsData->status == "loaded", "signalling signals drilldown");
	const SceneLoadedData* routesData = findLoadedDataChild(signallingData, "routes");
	ok &= expect(routesData && routesData->sourceFile == "signalling.json" && routesData->parsedCount == 2 && routesData->status == "loaded", "signalling routes drilldown");
	{
		SceneModel sceneWithDiagnostics = scene;
		std::vector<SceneDiagnostic> diagnostics;
		SceneDiagnostic serviceError;
		serviceError.severity = SceneSeverity::Error;
		serviceError.file = "services.json";
		diagnostics.push_back(serviceError);
		SceneDiagnostic stationWarning;
		stationWarning.severity = SceneSeverity::Warning;
		stationWarning.file = "stations.json";
		diagnostics.push_back(stationWarning);
		refreshLoadedDataDiagnostics(sceneWithDiagnostics, diagnostics);
		const SceneLoadedData* stationValidation = findLoadedDataChild(findLoadedData(sceneWithDiagnostics, "stations"), "validation");
		ok &= expect(stationValidation && stationValidation->parsedCount == 1 && stationValidation->status == "1 warning", "stations validation warning summary");
		const SceneLoadedData* timetableValidation = findLoadedDataChild(findLoadedData(sceneWithDiagnostics, "timetable"), "validation");
		ok &= expect(timetableValidation && timetableValidation->parsedCount == 1 && timetableValidation->status == "1 error", "timetable validation error summary");
		const SceneLoadedData* rollingValidation = findLoadedDataChild(findLoadedData(sceneWithDiagnostics, "rolling_stock"), "validation");
		ok &= expect(rollingValidation && rollingValidation->parsedCount == 0 && rollingValidation->status == "ok", "clean category validation summary");
	}
	ok &= expect(scene.baseTime == "07:00:00", "base_time loaded");
	ok &= expect(scene.stations.size() == 3, "station count loaded");
	ok &= expect(scene.routes.size() == 2, "route count loaded");
	ok &= expectAssignmentTimetable(scene);
	ok &= expect(scene.trainUnits.size() == 1, "train unit count loaded");
	ok &= expect(scene.compositions.size() == 1, "composition count loaded");

	{
		SceneModel sourceScene = scene;
		sourceScene.trainUnits[0].sourceDataFile = "/TrainData/LITRA_SA.txt";
		sourceScene.trainUnits[0].sourceTractionFile = "/TrainData/T_LITRA_SA.txt";
		refreshLoadedDataSummary(sourceScene);
		const SceneLoadedData* rollingStockData = findLoadedData(sourceScene, "rolling_stock");
		const SceneLoadedData* trainUnitsData = findLoadedDataChild(rollingStockData, "train_units");
		ok &= expect(trainUnitsData && trainUnitsData->parsedCount == 1 && trainUnitsData->status == "loaded", "rolling stock train unit drilldown");
		const SceneLoadedData* compositionsData = findLoadedDataChild(rollingStockData, "compositions");
		ok &= expect(compositionsData && compositionsData->parsedCount == 1 && compositionsData->status == "loaded", "rolling stock composition drilldown");
		const SceneLoadedData* sourceFilesData = findLoadedDataChild(rollingStockData, "source_files");
		ok &= expect(sourceFilesData && sourceFilesData->parsedCount == 2 && sourceFilesData->status == "loaded", "rolling stock source file drilldown");
		ok &= expect(findLoadedDataChildSource(sourceFilesData, "/TrainData/LITRA_SA.txt") != nullptr, "rolling stock LITRA source file visible");
		ok &= expect(findLoadedDataChildSource(sourceFilesData, "/TrainData/T_LITRA_SA.txt") != nullptr, "rolling stock T_LITRA source file visible");
	}

	SceneModel sceneToWrite = scene;
	if (!sceneToWrite.services.empty()) {
		sceneToWrite.services[0].through = true;
	}

	TempDir outDir;
	auto saveRes = saveScene(sceneToWrite, outDir.dir);
	printErrors(saveRes.diagnostics, "save");
	ok &= expect(saveRes.success(), "first save succeeds");
	ok &= expect(!fs::exists(fs::path(outDir.dir) / "legacy"), "saveScene does not create legacy folder");
	{
		std::ifstream sceneFile(fs::path(outDir.dir) / "scene.json");
		nlohmann::json sceneJson;
		sceneFile >> sceneJson;
		ok &= expect(!sceneJson.contains("loaded_data"), "loaded data metadata is not written to scene.json");
	}

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
	ok &= expect(reloaded.loadedData.size() == scene.loadedData.size(), "loaded data summary count round-trips");
	const SceneLoadedData* reloadedSceneData = findLoadedData(reloaded, "scene");
	ok &= expect(reloadedSceneData && reloadedSceneData->sourceFile == "scene.json" && reloadedSceneData->parsedCount == 1 && reloadedSceneData->status == "loaded", "loaded data scene round-trips");
	const SceneLoadedData* reloadedStationData = findLoadedData(reloaded, "stations");
	ok &= expect(reloadedStationData && reloadedStationData->sourceFile == "stations.json" && reloadedStationData->parsedCount == 3 && reloadedStationData->status == "loaded", "loaded data stations round-trips");
	const SceneLoadedData* reloadedStationRawData = findLoadedDataChild(reloadedStationData, "raw_file");
	ok &= expect(reloadedStationRawData && reloadedStationRawData->sourceFile == "stations.json" && reloadedStationRawData->parsedCount == 1 && reloadedStationRawData->status == "loaded", "loaded data station raw file round-trips");

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
		ok &= expect(service.id == "IC1723", "first service id round-trips");
		ok &= expect(service.through, "first service through flag round-trips");
		ok &= expect(service.composition == "sprinter_single", "first service composition round-trips");
		ok &= expect(service.route == "route0", "first service route round-trips");
		ok &= expect(service.hasEntryTime && service.entryTimeSeconds == 420, "entry time round-trips");
		ok &= expect(service.hasRepeat && service.headwaySeconds == 1800, "repeat headway round-trips");
		ok &= expect(service.stops.size() == 3, "first service stop count round-trips");
		if (service.stops.size() >= 3) {
			ok &= expect(service.stops[0].stationId == "Gvc", "first stop station round-trips");
			ok &= expect(!service.stops[0].hasArrival, "first stop missing arrival round-trips");
			ok &= expect(service.stops[0].hasDeparture && service.stops[0].departureSeconds == 480, "first stop departure round-trips");
			ok &= expect(service.stops[1].hasArrival && service.stops[1].arrivalSeconds == 1440, "middle stop arrival round-trips");
			ok &= expect(service.stops[1].hasDeparture && service.stops[1].departureSeconds == 1500, "middle stop departure round-trips");
			ok &= expect(service.stops[2].hasArrival && service.stops[2].arrivalSeconds == 2580, "last stop arrival round-trips");
			ok &= expect(!service.stops[2].hasDeparture, "last stop missing departure round-trips");
		}
	}

	{
		std::ifstream sceneFile(fs::path(outDir.dir) / "services.json");
		nlohmann::json svcsJson;
		sceneFile >> svcsJson;
		ok &= expect(svcsJson["services"][0]["through"] == true, "through written when true");
		if (svcsJson["services"].size() > 1) {
			ok &= expect(!svcsJson["services"][1].contains("through"), "through not written when false");
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

		auto bareReloadRes = loadScene(bareOut.dir);
		ok &= expect(!hasErrors(bareReloadRes.diagnostics), "bare scene reloads");
		const SceneLoadedData* bareStationData = findLoadedData(bareReloadRes.scene, "stations");
		ok &= expect(bareStationData && bareStationData->sourceFile == "stations.json" && bareStationData->parsedCount == 0 && bareStationData->status == "loaded", "empty stations file reported loaded");
		const SceneLoadedData* bareTimetableData = findLoadedData(bareReloadRes.scene, "timetable");
		ok &= expect(bareTimetableData && bareTimetableData->sourceFile == "services.json" && bareTimetableData->parsedCount == 0 && bareTimetableData->status == "loaded", "empty services file reported loaded");
	}

	{
		SceneModel incidentScene = scene;
		incidentScene.sourceFiles.erase("incidents.json");
		SceneIncident incident;
		incident.id = "signal_down";
		incident.type = "signal_failure";
		incident.target = "sigA";
		incidentScene.incidents.push_back(incident);
		refreshSavedSceneMetadata(incidentScene);
		const SceneLoadedData* incidentData = findLoadedData(incidentScene, "incidents");
		ok &= expect(incidentData && incidentData->sourceFile == "incidents.json" && incidentData->parsedCount == 1 && incidentData->status == "loaded", "saved incident metadata reports loaded");

		incidentScene.incidents.clear();
		refreshSavedSceneMetadata(incidentScene);
		incidentData = findLoadedData(incidentScene, "incidents");
		ok &= expect(incidentData && incidentData->sourceFile == "incidents.json" && incidentData->parsedCount == 0 && incidentData->status == "missing", "saved empty incident metadata reports missing");
	}

	if (!ok)
		return 1;
	std::cout << "all SceneWriter tests passed\n";
	return 0;
}
