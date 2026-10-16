#pragma once

#include "scene/SceneDiagnostic.h"
#include "scene/SceneModel.h"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

// The codes and message wording of the errors that a SceneFieldReader reports. In the section messages % stands for the
// key. The scene files and the standalone scenario file each have their own set.
struct SceneFieldCodes {
	const char* itemInvalid;		  // code: an item is not an object
	const char* itemMessage;		  // message for an item that a field is read from
	const char* sectionObjectMessage; // message for an item that a section is read from
	const char* fieldMissing;		  // code: a required key is absent
	const char* fieldInvalid;		  // code: a key has the wrong type or a value that is not allowed
	const char* sectionInvalid;		  // code: a section is absent or is not an array
	const char* sectionMissingMessage;
	const char* sectionMistypedMessage;
};

inline constexpr SceneFieldCodes kSceneFileFieldCodes{
	"scene.item.invalid",
	"Array item must be an object",
	"Expected an object",
	"scene.field.missing",
	"scene.field.missing",
	"scene.section.missing",
	"Missing % section",
	"Mistyped % section",
};

inline constexpr SceneFieldCodes kScenarioFileFieldCodes{
	"scene.scenario.item.invalid",
	"Scenario field must be an object",
	"Scenario field must be an object",
	"scene.scenario.field.missing",
	"scene.scenario.field.type",
	"scene.scenario.section.type",
	"% must be an array",
	"% must be an array",
};

// Reads typed fields from the JSON of one scene file and appends an error to the sink for every field that is absent or
// invalid. Each reader returns whether it stored a value; an optional field that is absent is not an error. The file name
// and the sink must outlive the reader.
class SceneFieldReader {
public:
	SceneFieldReader(std::string_view file, const SceneFieldCodes& codes, std::vector<SceneDiagnostic>& sink)
		: file_(file), codes_(codes), sink_(sink) {}

	// Reports item and returns false when it is not an object. arraySection and the field readers (except boolField) do
	// this themselves for the object that they read from, once per call; call it to report an item once, with its own
	// message, before its fields are read.
	bool objectItem(const nlohmann::json& item, const std::string& path, const char* message) const;
	bool arraySection(const nlohmann::json& object, const char* key, const std::string& path, bool required) const;
	bool stringField(const nlohmann::json& object, const char* key, const std::string& path, std::string& output,
		bool required = true) const;
	bool numberField(const nlohmann::json& object, const char* key, const std::string& path, double& output,
		bool required = true) const;
	// A value that does not fit an int is invalid.
	bool integerField(const nlohmann::json& object, const char* key, const std::string& path, int& output,
		bool required = true) const;
	// Always optional. Reports nothing for an absent key or for an item that is not an object.
	bool boolField(const nlohmann::json& object, const char* key, const std::string& path, bool& output) const;

private:
	void report(const char* code, const std::string& message, const std::string& path) const;

	std::string_view file_;
	const SceneFieldCodes& codes_;
	std::vector<SceneDiagnostic>& sink_;
};

// Reads an incident or an entrance delay of a scenario. The reader reports each field that is absent or invalid, in the
// order of the file schema, and the result keeps the defaults of the fields that it could not read.
SceneIncident decodeIncident(const SceneFieldReader& reader, const nlohmann::json& value, const std::string& path);
SceneEntranceDelay decodeEntranceDelay(const SceneFieldReader& reader, const nlohmann::json& value, const std::string& path);
