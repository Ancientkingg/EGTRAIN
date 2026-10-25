#ifndef LOADEDDATASUMMARY_H
#define LOADEDDATASUMMARY_H

#include "scene/SceneDiagnostic.h"
#include "scene/SceneModel.h"

#include <set>
#include <string>
#include <vector>

/// The state of a row of the Loaded Data summary.
enum class SceneLoadedStatus {
	Parsed,			 ///< Objects read from a file that is present.
	Loaded,			 ///< A file that is present.
	Ready,			 ///< Something that can be used as it is, such as a count of problems that is zero.
	Warning,		 ///< Data with a warning.
	Invalid,		 ///< A required file that is missing, or data with an error.
	MissingOptional, ///< An optional file or part that the scene does not have.
};

/// A row of the Loaded Data summary. It is derived from the scene and never saved.
struct SceneLoadedData {
	std::string category; ///< What the row counts, such as "infrastructure", "nodes" or "validation".
	/// The file the row counts: a scene file, or the legacy source for import and provenance rows;
	/// empty when none.
	std::string sourceFile;
	int parsedCount = 0;								  ///< Number of objects counted.
	SceneLoadedStatus status = SceneLoadedStatus::Parsed; ///< The state of what the row counts.
	/// What activating the row opens, such as "network", "service" or "validation"; empty for nothing.
	std::string targetType;
	std::vector<SceneLoadedData> children; ///< The rows below this one.
};

/// The Loaded Data summary of a scene: a row for each scene file with the parts of the scene below it. `files` names the
/// scene files that are present; a required file that is not is Invalid and an optional one MissingOptional. Each of these
/// rows ends with a validation row that counts the diagnostics of its file (the scene row also counts those of no file or
/// of a file without a row), and the row becomes Invalid or Warning when one of them is an error or a warning.
std::vector<SceneLoadedData> buildLoadedDataSummary(const SceneModel& scene, const std::set<std::string>& files,
	const std::vector<SceneDiagnostic>& diagnostics);

#endif // LOADEDDATASUMMARY_H
