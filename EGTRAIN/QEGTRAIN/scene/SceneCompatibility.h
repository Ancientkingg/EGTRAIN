#ifndef SCENECOMPATIBILITY_H
#define SCENECOMPATIBILITY_H

#include "scene/SceneModel.h"

#include <optional>
#include <string>
#include <vector>

class SceneMigrationRegistry;

/// The form in which a scene is stored.
enum class SceneSourceKind {
	Directory, ///< A directory of scene files.
	Bundle,	   ///< An .egscene bundle.
};

/// How the version of a scene relates to the versions this application reads.
enum class SceneCompatibilityClass {
	Current,		  ///< The current schema and, for a bundle, the current bundle layout.
	OlderMigratable,  ///< Older, and the migration registry has a path to the current version.
	OlderUnsupported, ///< Older without a migration path, or in the unsupported draft layout.
	Newer,			  ///< A newer schema or bundle layout.
	Malformed,		  ///< The version could not be read.
};

/// The version of a scene and what it means for opening it.
struct SceneCompatibilityProbeResult {
	int schemaVersion = 0;			  ///< schema_version of scene.json; 0 when not read.
	std::optional<int> bundleVersion; ///< bundle_version of a bundle; empty when not read or for a directory.
	std::string savedWithAppVersion;  ///< Version of the application that saved the scene; empty when not recorded.
	/// Directory or bundle.
	SceneSourceKind sourceKind = SceneSourceKind::Directory;
	/// The outcome.
	SceneCompatibilityClass classification = SceneCompatibilityClass::Malformed;
	std::vector<SceneDiagnostic> diagnostics; ///< Why the scene is malformed or rejected.
};

/// True for a scene that this application does not open because of its version or layout.
/// The diagnostics of the probe then name the reason.
inline bool isRejectedForVersion(const SceneCompatibilityProbeResult& probe) {
	return probe.classification == SceneCompatibilityClass::OlderUnsupported
		|| probe.classification == SceneCompatibilityClass::Newer;
}

/// Reads the version of the scene directory or bundle at path and classifies it against the shipped
/// migration registry, without loading the scene.
SceneCompatibilityProbeResult probeSceneCompatibility(const std::string& path);
/// The same, against the migration steps of registry.
SceneCompatibilityProbeResult probeSceneCompatibility(const std::string& path,
	const SceneMigrationRegistry& registry);

#endif // SCENECOMPATIBILITY_H
