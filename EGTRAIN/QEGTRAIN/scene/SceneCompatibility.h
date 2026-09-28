#ifndef SCENECOMPATIBILITY_H
#define SCENECOMPATIBILITY_H

#include "scene/SceneModel.h"

#include <optional>
#include <string>
#include <vector>

class SceneMigrationRegistry;

enum class SceneSourceKind {
	Directory,
	Bundle,
};

enum class SceneCompatibilityClass {
	Current,
	OlderMigratable,
	OlderUnsupported,
	Newer,
	Malformed,
};

struct SceneCompatibilityProbeResult {
	int schemaVersion = 0;
	std::optional<int> bundleVersion;
	std::string savedWithAppVersion;
	SceneSourceKind sourceKind = SceneSourceKind::Directory;
	SceneCompatibilityClass classification = SceneCompatibilityClass::Malformed;
	std::vector<SceneDiagnostic> diagnostics;
};

SceneCompatibilityProbeResult probeSceneCompatibility(const std::string& path);
SceneCompatibilityProbeResult probeSceneCompatibility(const std::string& path,
		const SceneMigrationRegistry& registry);

#endif // SCENECOMPATIBILITY_H
