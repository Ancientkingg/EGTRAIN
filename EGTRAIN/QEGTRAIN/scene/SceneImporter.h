#ifndef SCENEIMPORTER_H
#define SCENEIMPORTER_H

#include "scene/SceneDiagnostic.h"
#include "scene/SceneModel.h"
#include <array>
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

struct SceneTrainPhysicalSourceResult {
	SceneTrainPhysical physical;
	std::string error;
	bool success() const { return error.empty(); }
};

struct SceneTrainTractionSourceResult {
	std::vector<std::array<double, 5>> tractionCurve;
	std::string error;
	bool success() const { return error.empty(); }
};

SceneImportResult importLegacyScene(const std::string& legacyDir,
									const std::string& sceneDir,
									const std::string& sceneName);

ScenePassengerImportResult importLegacyPassengers(const std::string& legacyRootOrPassengerDir,
														const SceneModel& scene);

// Strict live-reload entry points. The historical importer below intentionally
// keeps its permissive row handling; these functions require a complete file.
SceneTrainPhysicalSourceResult parseTrainPhysicalSourceFile(const std::string& path);
SceneTrainTractionSourceResult parseTrainTractionSourceFile(const std::string& path);

#endif // SCENEIMPORTER_H
