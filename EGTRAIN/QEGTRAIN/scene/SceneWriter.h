#ifndef SCENEWRITER_H
#define SCENEWRITER_H

#include "scene/SceneDiagnostic.h"
#include "scene/SceneModel.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct SceneSaveResult {
	bool wroteAll = false;
	// Includes private staging; false until the first mutating filesystem attempt.
	bool writeAttempted = false;
	std::vector<SceneDiagnostic> diagnostics;
	std::string inputSnapshot;
	bool success() const;
};

struct ScenarioLoadResult {
	SceneScenario scenario;
	std::vector<SceneDiagnostic> diagnostics;
	bool success() const;
};

SceneSaveResult saveScene(const SceneModel& scene, const std::string& sceneDir);
SceneSaveResult saveScenarioJson(const SceneScenario& scenario, const std::string& filePath);
ScenarioLoadResult loadScenarioJson(const std::string& filePath);

// The content of passengers.json. A journey without an activity leaves the key out.
nlohmann::json encodePassengers(const std::vector<ScenePassenger>& passengers);

// The content of infrastructure.json.
nlohmann::json encodeInfrastructure(const SceneModel& scene);
// The content of stations.json.
nlohmann::json encodeStations(const SceneModel& scene);
// A signalling area as signalling.json holds it.
nlohmann::json encodeSignallingArea(const SceneSignallingArea& area);
// The content of signalling.json.
nlohmann::json encodeSignalling(const SceneModel& scene);
// The content of rolling_stock.json.
nlohmann::json encodeRollingStock(const SceneModel& scene);
// The content of services.json.
nlohmann::json encodeServices(const SceneModel& scene);

#endif // SCENEWRITER_H
