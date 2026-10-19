#ifndef SCENEVALIDATOR_H
#define SCENEVALIDATOR_H

#include "scene/SceneModel.h"
#include <optional>
#include <vector>
#include <string>

/// Validates only the scene files and JSON shape: the diagnostics of loadScene. No cross-file semantic
/// checks are performed, so callers can show useful parse diagnostics for an
/// incomplete historical scene without a cascade of reference errors.
std::vector<SceneDiagnostic> validateSceneStructure(const std::string& sceneDir);

/// Validates the references, ids, values, timetables and incidents of the loaded model.
std::vector<SceneDiagnostic> validateScene(const SceneModel& scene);

/// Validates the model as validateScene does, plus the minimum complete model needed by a runnable simulation
/// and the capacities of the runtime. A non-empty selectedOccurrences counts only those occurrences against the
/// train capacity. effectiveDurationOverride, in seconds, replaces the duration of the scene settings.
std::vector<SceneDiagnostic> validateRunnableScene(const SceneModel& scene,
	const SceneRunSelection& selectedOccurrences = {},
	std::optional<double> effectiveDurationOverride = std::nullopt);

/// Loads, then validates with validateScene only if loading produced no Error diagnostics. Structural
/// errors must be fixed first to avoid semantic cascades from a partial model.
std::vector<SceneDiagnostic> validateSceneDirectory(const std::string& sceneDir);

/// Loads, then validates with validateRunnableScene only if loading produced no Error diagnostics.
std::vector<SceneDiagnostic> validateRunnableSceneDirectory(const std::string& sceneDir);

#endif // SCENEVALIDATOR_H
