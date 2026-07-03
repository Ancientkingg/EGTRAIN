#ifndef SCENEEXPORTER_H
#define SCENEEXPORTER_H

#include "SceneDiagnostic.h"
#include <string>
#include <vector>

struct SceneExportResult {
	bool wroteAll = false;
	std::vector<SceneDiagnostic> diagnostics;
	bool success() const;
};

SceneExportResult exportLegacyScene(const std::string& sceneDir, const std::string& outDir);

#endif // SCENEEXPORTER_H
