#ifndef SCENEVALIDATOR_H
#define SCENEVALIDATOR_H

#include "scene/SceneModel.h"
#include <vector>
#include <string>

// Semantically validate the loaded model.
std::vector<SceneDiagnostic> validateScene(const SceneModel& scene);

// Convenience: load, then validate ONLY if loading produced no Error diagnostics.
// Structural errors must be fixed first; this prevents cascade noise (semantic
// checks against a half-loaded model) in the future validation panel.
std::vector<SceneDiagnostic> validateSceneDirectory(const std::string& sceneDir);

#endif // SCENEVALIDATOR_H
