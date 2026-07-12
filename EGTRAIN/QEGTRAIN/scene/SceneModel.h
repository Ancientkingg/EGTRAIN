#ifndef SCENEMODEL_H
#define SCENEMODEL_H

#include "scene/SceneDiagnostic.h"
#include <set>
#include <string>
#include <vector>

struct ScenePlatform {
	std::string id;
};

struct SceneStation {
	std::string id;
	std::string name;
	std::vector<ScenePlatform> platforms;
};

struct SceneSignal {
	std::string id;
};

struct SceneRoute {
	std::string id;
	std::vector<std::string> blocks;
};

struct SceneTrainPhysical {
	double mass_of_traction_unit_kg = 0.0;
	double mass_of_a_wagon_kg = 0.0;
	double number_of_wagons = 0.0;
	double max_speed_ms = 0.0;
	double max_deceleration_ms2 = 0.0;
	double frontal_area_m2 = 0.0;
	double resistance_coefficient = 0.0;
	double jerk_ms3 = 0.0;
	double length_m = 0.0;
};

struct SceneTrainUnit {
	std::string id;
	bool hasPhysical = false;
	SceneTrainPhysical physical;
	std::vector<std::array<double, 5>> tractionCurve;
	std::string sourceDataFile;
	std::string sourceTractionFile;
};

struct SceneComposition {
	std::string id;
	std::vector<std::string> units;
};

struct SceneStop {
	std::string stationId;
	std::string platformId;
	bool hasArrival = false;
	bool hasDeparture = false;
	double arrivalSeconds = 0.0;
	double departureSeconds = 0.0;
	double dwellSeconds = 0.0;
};

struct SceneService {
	std::string id;
	std::string composition;
	std::string route;
	bool through = false;
	bool hasEntryTime = false;
	double entryTimeSeconds = 0.0;
	bool hasRepeat = false;
	double headwaySeconds = 0.0;
	std::vector<SceneStop> stops;
};

struct SceneIncident {
	std::string id;
	std::string type; // "signal_failure" | "train_breakdown"
	std::string target;
	double startSeconds = 0.0;
	double endSeconds = 0.0;
};

struct SceneLoadedData {
	std::string category;
	std::string sourceFile;
	int parsedCount = 0;
	std::string status;
	std::vector<SceneLoadedData> children;
};

struct SceneModel {
	int schemaVersion = 0;
	std::string name;
	std::string description;
	std::string baseTime;

	std::vector<SceneLoadedData> loadedData;
	std::set<std::string> sourceFiles;
	std::vector<SceneStation> stations;
	std::vector<SceneSignal> signals;
	std::vector<SceneRoute> routes;
	std::vector<SceneTrainUnit> trainUnits;
	std::vector<SceneComposition> compositions;
	std::vector<SceneService> services;
	std::vector<SceneIncident> incidents;
};

struct SceneLoadResult {
	SceneModel scene;						  // partial on failure
	std::vector<SceneDiagnostic> diagnostics; // structural problems
};

SceneLoadResult loadScene(const std::string& sceneDir);
void refreshLoadedDataSummary(SceneModel& scene);
void refreshLoadedDataDiagnostics(SceneModel& scene, const std::vector<SceneDiagnostic>& diagnostics);
void refreshSavedSceneMetadata(SceneModel& scene);

#endif // SCENEMODEL_H
