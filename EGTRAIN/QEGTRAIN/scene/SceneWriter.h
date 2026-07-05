#ifndef SCENEWRITER_H
#define SCENEWRITER_H

#include "scene/SceneDiagnostic.h"
#include "scene/SceneModel.h"

#include <string>
#include <vector>

struct SceneSaveResult {
	bool wroteAll = false;
	std::vector<SceneDiagnostic> diagnostics;
	bool success() const;
};

SceneSaveResult saveScene(const SceneModel& scene, const std::string& sceneDir);

#endif // SCENEWRITER_H
