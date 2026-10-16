#include "scene/SceneFieldReader.h"

#include <climits>
#include <utility>

using json = nlohmann::json;

namespace {

std::string joinPath(const std::string& parent, const std::string& key) {
	return parent.empty() ? key : parent + "." + key;
}

std::string withKey(const char* pattern, const char* key) {
	std::string message = pattern;
	const std::size_t at = message.find('%');
	if (at != std::string::npos)
		message.replace(at, 1, key);
	return message;
}

} // namespace

void SceneFieldReader::report(const char* code, const std::string& message, const std::string& path) const {
	SceneDiagnostic diagnostic;
	diagnostic.severity = SceneSeverity::Error;
	diagnostic.code = code;
	diagnostic.file = std::string(file_);
	diagnostic.message = message;
	diagnostic.path = path;
	sink_.push_back(std::move(diagnostic));
}

bool SceneFieldReader::objectItem(const json& item, const std::string& path, const char* message) const {
	if (item.is_object())
		return true;
	report(codes_.itemInvalid, message, path);
	return false;
}

bool SceneFieldReader::arraySection(const json& object, const char* key, const std::string& path, bool required) const {
	if (!objectItem(object, path, codes_.sectionObjectMessage))
		return false;
	if (!object.contains(key)) {
		if (required)
			report(codes_.sectionInvalid, withKey(codes_.sectionMissingMessage, key), joinPath(path, key));
		return false;
	}
	if (!object[key].is_array()) {
		report(codes_.sectionInvalid, withKey(codes_.sectionMistypedMessage, key), joinPath(path, key));
		return false;
	}
	return true;
}

bool SceneFieldReader::stringField(const json& object, const char* key, const std::string& path, std::string& output,
	bool required) const {
	if (!objectItem(object, path, codes_.itemMessage))
		return false;
	if (!object.contains(key)) {
		if (required)
			report(codes_.fieldMissing, std::string("Missing ") + key, joinPath(path, key));
		return false;
	}
	if (!object[key].is_string() || (required && object[key].get<std::string>().empty())) {
		report(codes_.fieldInvalid, std::string("Invalid ") + key, joinPath(path, key));
		return false;
	}
	output = object[key].get<std::string>();
	return true;
}

bool SceneFieldReader::numberField(const json& object, const char* key, const std::string& path, double& output,
	bool required) const {
	if (!objectItem(object, path, codes_.itemMessage))
		return false;
	if (!object.contains(key)) {
		if (required)
			report(codes_.fieldMissing, std::string("Missing ") + key, joinPath(path, key));
		return false;
	}
	if (!object[key].is_number()) {
		report(codes_.fieldInvalid, std::string("Invalid ") + key, joinPath(path, key));
		return false;
	}
	try {
		output = object[key].get<double>();
	} catch (const json::exception&) {
		report(codes_.fieldInvalid, std::string("Invalid ") + key, joinPath(path, key));
		return false;
	}
	return true;
}

bool SceneFieldReader::integerField(const json& object, const char* key, const std::string& path, int& output,
	bool required) const {
	if (!objectItem(object, path, codes_.itemMessage))
		return false;
	if (!object.contains(key)) {
		if (required)
			report(codes_.fieldMissing, std::string("Missing ") + key, joinPath(path, key));
		return false;
	}
	if (!object[key].is_number_integer()) {
		report(codes_.fieldInvalid, std::string("Invalid ") + key, joinPath(path, key));
		return false;
	}
	int value = 0;
	if (!readJsonInt(object[key], INT_MIN, INT_MAX, value)) {
		report(codes_.fieldInvalid, std::string("Invalid ") + key + ": integer out of range", joinPath(path, key));
		return false;
	}
	output = value;
	return true;
}

bool SceneFieldReader::boolField(const json& object, const char* key, const std::string& path, bool& output) const {
	if (!object.contains(key))
		return false;
	if (!object[key].is_boolean()) {
		report(codes_.fieldInvalid, std::string("Invalid ") + key, joinPath(path, key));
		return false;
	}
	output = object[key].get<bool>();
	return true;
}

SceneIncident decodeIncident(const SceneFieldReader& reader, const json& value, const std::string& path) {
	SceneIncident incident;
	reader.stringField(value, "id", path, incident.id);
	reader.stringField(value, "type", path, incident.type);
	reader.stringField(value, "target", path, incident.target);
	reader.numberField(value, "start_seconds", path, incident.startSeconds);
	incident.hasEndSeconds = reader.numberField(value, "end_seconds", path, incident.endSeconds, false);
	incident.hasOccurrence = reader.integerField(value, "occurrence", path, incident.occurrence, false);
	incident.hasReducedSpeed = reader.numberField(value, "reduced_speed_kmh", path, incident.reducedSpeedKmh, false);
	reader.boolField(value, "terminate_at_destination", path, incident.terminateAtDestination);
	return incident;
}

SceneEntranceDelay decodeEntranceDelay(const SceneFieldReader& reader, const json& value, const std::string& path) {
	SceneEntranceDelay delay;
	reader.stringField(value, "service", path, delay.serviceId);
	reader.integerField(value, "occurrence", path, delay.occurrence, false);
	reader.stringField(value, "station", path, delay.stationId);
	reader.numberField(value, "delay_seconds", path, delay.delaySeconds);
	return delay;
}
