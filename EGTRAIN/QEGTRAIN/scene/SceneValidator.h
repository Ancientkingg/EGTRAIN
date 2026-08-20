#ifndef SCENEVALIDATOR_H
#define SCENEVALIDATOR_H

#include "scene/SceneModel.h"
#include <vector>
#include <string>

// Validate only the scene files and JSON shape.  No cross-file semantic
// checks are performed, so callers can show useful parse diagnostics for an
// incomplete historical scene without a cascade of reference errors.
std::vector<SceneDiagnostic> validateSceneStructure(const std::string& sceneDir);

// Semantically validate the loaded model.
std::vector<SceneDiagnostic> validateScene(const SceneModel& scene);

// Validate the minimum complete model needed by a runnable simulation.
std::vector<SceneDiagnostic> validateRunnableScene(const SceneModel& scene,
		const SceneRunSelection& selectedOccurrences = {});

// Load, then validate ONLY if loading produced no Error diagnostics. Structural
// errors must be fixed first to avoid semantic cascades from a partial model.
std::vector<SceneDiagnostic> validateSceneDirectory(const std::string& sceneDir);

// The same directory boundary with runnable-completeness checks included.
std::vector<SceneDiagnostic> validateRunnableSceneDirectory(const std::string& sceneDir);

#endif // SCENEVALIDATOR_H
