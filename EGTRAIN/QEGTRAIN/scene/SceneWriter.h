#ifndef SCENEWRITER_H
#define SCENEWRITER_H

#include "scene/SceneDiagnostic.h"
#include "scene/SceneModel.h"

#include <string>
#include <vector>

struct SceneSaveResult {
	bool wroteAll = false;
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

#endif // SCENEWRITER_H
