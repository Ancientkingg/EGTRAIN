#ifndef RAILMLIMPORTER_H
#define RAILMLIMPORTER_H

#include "scene/SceneImporter.h"

#include <string>

/// The choices of a railML import.
struct RailmlImportOptions {
	/// Id of the operating period whose train parts are imported. Empty imports the period that the most train parts
	/// refer to. Train parts without an operating period are always imported.
	std::string operatingPeriod;
};

/// Converts the railML 2.x file at railmlPath into a scene directory at sceneDir, written in a private directory and
/// then renamed into place, as importLegacyScene does. sceneName becomes the scene name; empty uses the file name
/// without its extension. The file holds the infrastructure and may hold rolling stock and a timetable. A railML 3 file
/// or a file that is not railML is refused with one error. The import report of the scene counts each railML element
/// kind with the railML file as its source, and the diagnostics name every default value, every element kind that has no
/// counterpart in the scene, and every reference that could not be resolved. docs/architecture/railml-import.md
/// describes the mapping.
SceneImportResult importRailmlScene(const std::string& railmlPath, const std::string& sceneDir,
	const std::string& sceneName, const RailmlImportOptions& options = {});

#endif // RAILMLIMPORTER_H
