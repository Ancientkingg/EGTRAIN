#ifndef SCENEEXPORTER_H
#define SCENEEXPORTER_H

#include "scene/SceneDiagnostic.h"
#include <string>
#include <vector>

/// The result of a legacy export.
struct SceneExportResult {
	bool wroteAll = false;					  ///< True when every legacy file was written.
	std::vector<SceneDiagnostic> diagnostics; ///< What the export found and could not represent.
	bool success() const;					  ///< True when wroteAll is set and no diagnostic is an error.
};

/// Writes the scene directory at sceneDir as a legacy case into outDir, which must not overlap the scene
/// or its legacy directory. Data the legacy format cannot hold is left out with a diagnostic.
SceneExportResult exportLegacyScene(const std::string& sceneDir, const std::string& outDir);

#endif // SCENEEXPORTER_H
