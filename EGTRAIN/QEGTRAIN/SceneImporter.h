#ifndef SCENEIMPORTER_H
#define SCENEIMPORTER_H

#include "SceneDiagnostic.h"
#include <string>
#include <vector>

struct SceneImportResult {
	bool wroteScene = false;
	std::vector<SceneDiagnostic> diagnostics;
	bool success() const;
};

SceneImportResult importLegacyScene(const std::string& legacyDir,
									const std::string& sceneDir,
									const std::string& sceneName);

#endif // SCENEIMPORTER_H
