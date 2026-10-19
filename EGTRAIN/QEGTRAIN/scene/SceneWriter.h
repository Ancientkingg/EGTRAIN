#ifndef SCENEWRITER_H
#define SCENEWRITER_H

#include "scene/SceneDiagnostic.h"
#include "scene/SceneModel.h"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

/// The result of writing a scene directory, a bundle or a scenario file.
struct SceneSaveResult {
	bool wroteAll = false; ///< True when every file was written and published.
	/// True once the filesystem was changed or a change was tried, private staging included.
	bool writeAttempted = false;
	std::vector<SceneDiagnostic> diagnostics; ///< Problems found while writing.
	/// The bytes written, in the form of SceneLoadResult::inputSnapshot; empty unless the write succeeded.
	std::string inputSnapshot;
	bool success() const; ///< True when wroteAll is set and no diagnostic is an error.
};

/// The result of reading one scenario from a standalone scenario file.
struct ScenarioLoadResult {
	SceneScenario scenario;					  ///< The scenario read.
	std::vector<SceneDiagnostic> diagnostics; ///< Problems found while reading.
	bool success() const;					  ///< True when no diagnostic is an error.
};

/// Writes the scene files into a private directory next to sceneDir, loads them back, and then renames
/// the directory into place. On failure the previous directory stays as it was. views.json and
/// passengers.json are written only when the scene has views or passengers.
SceneSaveResult saveScene(const SceneModel& scene, const std::string& sceneDir);
/// Writes one scenario as a standalone scenario file, replacing the file at filePath.
SceneSaveResult saveScenarioJson(const SceneScenario& scenario, const std::string& filePath);
/// Reads one scenario from a standalone scenario file.
ScenarioLoadResult loadScenarioJson(const std::string& filePath);

/// The content of passengers.json. A journey without an activity leaves the key out.
nlohmann::json encodePassengers(const std::vector<ScenePassenger>& passengers);

/// The content of infrastructure.json.
nlohmann::json encodeInfrastructure(const SceneModel& scene);
/// The content of stations.json.
nlohmann::json encodeStations(const SceneModel& scene);
/// A signalling area as signalling.json holds it.
nlohmann::json encodeSignallingArea(const SceneSignallingArea& area);
/// The content of signalling.json.
nlohmann::json encodeSignalling(const SceneModel& scene);
/// The content of rolling_stock.json.
nlohmann::json encodeRollingStock(const SceneModel& scene);
/// The content of services.json.
nlohmann::json encodeServices(const SceneModel& scene);
/// The content of scenarios.json.
nlohmann::json encodeScenarios(const SceneModel& scene);
/// The content of views.json.
nlohmann::json encodeViews(const SceneModel& scene);
/// The content of scene.json.
nlohmann::json encodeSceneMetadata(const SceneModel& scene);

#endif // SCENEWRITER_H
