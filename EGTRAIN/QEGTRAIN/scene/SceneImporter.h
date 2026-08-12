#ifndef SCENEIMPORTER_H
#define SCENEIMPORTER_H

#include "scene/SceneDiagnostic.h"
#include "scene/SceneModel.h"
#include <string>
#include <vector>

struct SceneImportResult {
	bool wroteScene = false;
	std::vector<SceneDiagnostic> diagnostics;
	bool success() const;
};

struct ScenePassengerImportRow {
	std::string sourceFile;
	int row = 0;
	std::string passengerId;
	bool accepted = false;
	bool unresolvedReferences = false;
	std::string context;
};

struct ScenePassengerImportResult {
	std::vector<ScenePassenger> passengers;
	std::vector<ScenePassengerImportRow> rows;
	std::vector<SceneImportReportRow> report;
	std::vector<SceneDiagnostic> diagnostics;
	bool success() const;
};

SceneImportResult importLegacyScene(const std::string& legacyDir,
									const std::string& sceneDir,
									const std::string& sceneName);

ScenePassengerImportResult importLegacyPassengers(const std::string& legacyRootOrPassengerDir,
													const SceneModel& scene);

#endif // SCENEIMPORTER_H
