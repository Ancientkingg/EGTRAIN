#ifndef SCENEMODEL_H
#define SCENEMODEL_H

#include "SceneDiagnostic.h"
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

struct SceneTrainUnit {
	std::string id;
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
	std::vector<SceneStop> stops;
};

struct SceneIncident {
	std::string id;
	std::string type; // "signal_failure" | "train_breakdown"
	std::string target;
	double startSeconds = 0.0;
	double endSeconds = 0.0;
};

struct SceneModel {
	int schemaVersion = 0;
	std::string name;
	std::string baseTime;

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

#endif // SCENEMODEL_H
