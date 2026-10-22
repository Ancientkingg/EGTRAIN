#ifndef SCENEBUNDLE_H
#define SCENEBUNDLE_H

#include "scene/SceneModel.h"
#include "scene/SceneWriter.h"

#include <optional>
#include <string>
#include <vector>

/// What the manifest of an .egscene bundle says, read without extracting the bundle.
struct SceneBundleProbeResult {
	int schemaVersion = 0;					  ///< schema_version of the manifest; 0 when not read.
	std::optional<int> bundleVersion;		  ///< bundle_version of the manifest; empty when not read.
	std::string savedWithAppVersion;		  ///< Version of the application that saved the bundle; empty when not recorded.
	bool structurallyValid = false;			  ///< True when the archive and its manifest passed the checks.
	std::vector<SceneDiagnostic> diagnostics; ///< Problems found.
};

/// Reads only ZIP metadata and the bounded scene.json manifest. Unknown newer
/// layouts are never extracted; v1 allowlist checks happen in the same shared
/// inspection path used by the normal loader.
SceneBundleProbeResult probeSceneBundle(const std::string& bundlePath);

/// Migration uses the same hostile-archive checks and extracts a safe older
/// layout into a caller-owned staging directory for an explicit migration step.
SceneSaveResult extractSceneBundleForMigration(const std::string& bundlePath,
	const std::string& destinationDirectory);

/// Extracts a bundle of the current bundle and schema version to a temporary directory and loads it.
/// inputSnapshot holds the bytes of the bundle file.
SceneLoadResult loadSceneBundle(const std::string& bundlePath);
/// Loads a directory, or a missing path without the .egscene extension, with loadScene, and any other path
/// with loadSceneBundle.
SceneLoadResult loadScenePath(const std::string& path);
/// Writes a scene of the current schema version as an .egscene bundle, checks the archive, and then moves
/// it to bundlePath, replacing a regular file there.
SceneSaveResult saveSceneBundle(const SceneModel& scene, const std::string& bundlePath);
/// Extracts a bundle of the current bundle and schema version into destinationDirectory, which must not
/// exist yet, after the extracted scene loads without errors.
SceneSaveResult unpackSceneBundle(const std::string& bundlePath, const std::string& destinationDirectory);

#endif // SCENEBUNDLE_H
