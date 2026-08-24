#ifndef SCENEBUNDLE_H
#define SCENEBUNDLE_H

#include "scene/SceneModel.h"
#include "scene/SceneWriter.h"

#include <optional>
#include <string>
#include <vector>

struct SceneBundleProbeResult {
	int schemaVersion = 0;
	std::optional<int> bundleVersion;
	std::string savedWithAppVersion;
	bool structurallyValid = false;
	std::vector<SceneDiagnostic> diagnostics;
};

// Reads only ZIP metadata and the bounded scene.json manifest. Unknown newer
// layouts are never extracted; v1 allowlist checks happen in the same shared
// inspection path used by the normal loader.
SceneBundleProbeResult probeSceneBundle(const std::string& bundlePath);

// Migration uses the same hostile-archive checks and extracts a safe older
// layout into a caller-owned staging directory for an explicit migration step.
SceneSaveResult extractSceneBundleForMigration(const std::string& bundlePath,
		const std::string& destinationDirectory);

SceneLoadResult loadSceneBundle(const std::string& bundlePath);
SceneLoadResult loadScenePath(const std::string& path);
SceneSaveResult saveSceneBundle(const SceneModel& scene, const std::string& bundlePath);
SceneSaveResult unpackSceneBundle(const std::string& bundlePath, const std::string& destinationDirectory);

#endif // SCENEBUNDLE_H
