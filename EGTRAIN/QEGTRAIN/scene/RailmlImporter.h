#ifndef RAILMLIMPORTER_H
#define RAILMLIMPORTER_H

#include "scene/SceneImporter.h"

#include <string>
#include <vector>

/// The choices of a railML import.
struct RailmlImportOptions {
	/// Id of the operating period whose train parts are imported. Empty imports the period that the most train parts
	/// refer to. Train parts without an operating period are always imported.
	std::string operatingPeriod;
};

/// An operating period of a railML file.
struct RailmlOperatingPeriod {
	/// The id of the operatingPeriod element, which RailmlImportOptions::operatingPeriod takes.
	std::string id;
	/// Its name; empty when the file gives none.
	std::string name;
	/// The number of train parts that an import of the period takes: those that refer to it and those without a period.
	int trainParts = 0;
};

/// The operating periods of a railML file, for a choice before the import.
struct RailmlOperatingPeriods {
	/// The periods that train parts refer to, in the order of the file.
	std::vector<RailmlOperatingPeriod> periods;
	/// The id of the period that importRailmlScene imports when none is chosen: the one with the most train parts.
	std::string defaultPeriod;
};

/// Reads the operating periods of the railML 2.x file at railmlPath. Periods without train parts are left out. The
/// result is empty when the file cannot be read or is not railML 2; the import then reports why.
RailmlOperatingPeriods listRailmlOperatingPeriods(const std::string& railmlPath);

/// Converts the railML 2.x file at railmlPath into a scene directory at sceneDir, written in a private directory and
/// then renamed into place, as importLegacyScene does. sceneName becomes the scene name; empty uses the file name
/// without its extension. The file holds the infrastructure and may hold rolling stock and a timetable. A railML 3 file
/// or a file that is not railML is refused with one error, and so is a sceneDir that holds the file. A file that uses
/// one id for two elements of one kind, or holds a time or a count far out of range, is refused before anything is
/// converted, and a converted scene that fails validateScene is not written. The import report of the scene counts each
/// railML element kind with the railML file as its source, and the diagnostics name every default value, every element
/// kind that has no counterpart in the scene, and every reference that could not be resolved.
/// docs/architecture/railml-import.md describes the mapping.
SceneImportResult importRailmlScene(const std::string& railmlPath, const std::string& sceneDir,
	const std::string& sceneName, const RailmlImportOptions& options = {});

#endif // RAILMLIMPORTER_H
