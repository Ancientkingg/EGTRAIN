#ifndef SCENEIMPORTER_H
#define SCENEIMPORTER_H

#include "scene/SceneDiagnostic.h"
#include "scene/SceneModel.h"
#include <array>
#include <string>
#include <vector>

/// The result of importing a legacy case.
struct SceneImportResult {
	bool wroteScene = false;				  ///< True when the scene directory was published.
	std::vector<SceneDiagnostic> diagnostics; ///< What the import found and did.
	bool success() const;					  ///< True when wroteScene is set and no diagnostic is an error.
};

/// What happened to one row of a legacy passenger file.
struct ScenePassengerImportRow {
	std::string sourceFile;			   ///< Path of the file.
	int row = 0;					   ///< Number of the data row, counted from 1 without the header and blank lines.
	std::string passengerId;		   ///< Person id of the row; empty when it has none.
	bool accepted = false;			   ///< True when the row was converted.
	bool unresolvedReferences = false; ///< True when the row refers to something that could not be resolved.
	std::string context;			   ///< Text for the user about the row.
};

/// The passengers read from a legacy passenger file pair, with an account of every row.
struct ScenePassengerImportResult {
	std::vector<ScenePassenger> passengers;	   ///< The passengers converted.
	std::vector<ScenePassengerImportRow> rows; ///< One entry per data row of both files.
	std::vector<SceneImportReportRow> report;  ///< Counts per file, in the form of import_report.
	std::vector<SceneDiagnostic> diagnostics;  ///< Problems found.
	bool success() const;					   ///< True when no diagnostic is an error.
};

/// The physical data read from a legacy vehicle data file.
struct SceneTrainPhysicalSourceResult {
	SceneTrainPhysical physical;				   ///< The nine values, in the order of kSceneTrainPhysicalFields.
	std::string error;							   ///< Why the file was refused; empty on success.
	bool success() const { return error.empty(); } ///< True when error is empty.
};

/// The tractive effort curve read from a legacy traction file.
struct SceneTrainTractionSourceResult {
	std::vector<std::array<double, 5>> tractionCurve; ///< The rows, in the form of SceneTrainUnit::tractionCurve.
	std::string error;								  ///< Why the file was refused; empty on success.
	bool success() const { return error.empty(); }	  ///< True when error is empty.
};

/// Converts the legacy case in legacyDir into a scene directory at sceneDir, written in a private directory
/// and then renamed into place. sceneName becomes the scene name and selects the settings of a known case.
/// Skipped and unresolved records are counted in import_report.
SceneImportResult importLegacyScene(const std::string& legacyDir,
	const std::string& sceneDir,
	const std::string& sceneName);

/// Reads the legacy passenger files DAS_FrenchCaseStudy.csv and RouteChoiceFC_EQ1.csv from a case
/// directory or its Passengers directory, resolving stations and services against scene. Both files are
/// required. The scene is not changed.
ScenePassengerImportResult importLegacyPassengers(const std::string& legacyRootOrPassengerDir,
	const SceneModel& scene);

/// Reads a vehicle data file of exactly nine finite numbers in the order of kSceneTrainPhysicalFields,
/// with values the simulation can use. The editor reloads linked files with it. Unlike importLegacyScene,
/// which skips bad rows, it refuses an incomplete file.
SceneTrainPhysicalSourceResult parseTrainPhysicalSourceFile(const std::string& path);
/// Reads a traction file of rows of five finite numbers, each with v_lower below v_upper, in ascending
/// speed order. Like parseTrainPhysicalSourceFile it refuses an incomplete file.
SceneTrainTractionSourceResult parseTrainTractionSourceFile(const std::string& path);

#endif // SCENEIMPORTER_H
