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

void rejectResultFields(const SceneFieldReader& reader, const json& object, const std::string& path) {
	if (!object.is_object())
		return;
	for (const auto& item : object.items()) {
		if (isSimulationResultField(item.key()))
			reader.report("scene.passengers.results", "Passenger input must not contain simulation-result fields",
				joinPath(path, item.key()));
	}
}

void decodeWindow(const SceneFieldReader& reader, const json& journey, const char* key, const std::string& journeyPath,
	double& start, double& end) {
	const std::string windowPath = joinPath(journeyPath, key);
	if (!journey.contains(key) || !journey[key].is_object()) {
		reader.report(kSceneFileFieldCodes.fieldMissing, std::string("Missing ") + key + " window", windowPath);
		return;
	}
	reader.numberField(journey[key], "start_seconds", windowPath, start);
	reader.numberField(journey[key], "end_seconds", windowPath, end);
}

ScenePassengerLeg decodePassengerLeg(const SceneFieldReader& reader, const json& value, const std::string& path) {
	ScenePassengerLeg leg;
	rejectResultFields(reader, value, path);
	reader.stringField(value, "id", path, leg.id);
	reader.stringField(value, "origin", path, leg.originStationId);
	reader.stringField(value, "destination", path, leg.destinationStationId);
	reader.stringField(value, "service", path, leg.serviceId);
	reader.integerField(value, "occurrence", path, leg.occurrence, false);
	return leg;
}

ScenePassengerJourney decodePassengerJourney(const SceneFieldReader& reader, const json& value, const std::string& path) {
	ScenePassengerJourney journey;
	rejectResultFields(reader, value, path);
	reader.stringField(value, "id", path, journey.id);
	reader.stringField(value, "activity", path, journey.activity, false);
	reader.stringField(value, "origin", path, journey.originStationId);
	reader.stringField(value, "destination", path, journey.destinationStationId);
	decodeWindow(reader, value, "planned_departure", path, journey.plannedDepartureStartSeconds,
		journey.plannedDepartureEndSeconds);
	decodeWindow(reader, value, "planned_arrival", path, journey.plannedArrivalStartSeconds,
		journey.plannedArrivalEndSeconds);
	if (reader.arraySection(value, "legs", path, true)) {
		for (std::size_t index = 0; index < value["legs"].size(); ++index)
			journey.legs.push_back(
				decodePassengerLeg(reader, value["legs"][index], path + ".legs[" + std::to_string(index) + "]"));
	}
	return journey;
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

SceneScenario decodeScenarioFile(const SceneFieldReader& reader, const json& root) {
	SceneScenario scenario;
	const auto allowed = [](const std::string& name) {
		return name == "id" || name == "name" || name == "description" || name == "incidents" || name == "entrance_delays";
	};
	for (const auto& field : root.items()) {
		if (!allowed(field.key()))
			reader.report("scene.scenario.field.unknown", "Scenario JSON does not allow field " + field.key(), field.key());
	}
	reader.stringField(root, "id", "scenario", scenario.id);
	reader.stringField(root, "name", "scenario", scenario.name);
	reader.stringField(root, "description", "scenario", scenario.description, false);
	if (reader.arraySection(root, "incidents", "", true)) {
		for (std::size_t index = 0; index < root["incidents"].size(); ++index) {
			const json& item = root["incidents"][index];
			const std::string path = "incidents[" + std::to_string(index) + "]";
			if (reader.objectItem(item, path, "Incident must be an object"))
				scenario.incidents.push_back(decodeIncident(reader, item, path));
		}
	}
	if (reader.arraySection(root, "entrance_delays", "", false)) {
		for (std::size_t index = 0; index < root["entrance_delays"].size(); ++index) {
			const json& item = root["entrance_delays"][index];
			const std::string path = "entrance_delays[" + std::to_string(index) + "]";
			if (reader.objectItem(item, path, "Entrance delay must be an object"))
				scenario.entranceDelays.push_back(decodeEntranceDelay(reader, item, path));
		}
	}
	return scenario;
}

SceneSignallingArea decodeSignallingArea(const SceneFieldReader& reader, const json& value, const std::string& path) {
	SceneSignallingArea area;
	reader.stringField(value, "id", path, area.id);
	reader.numberField(value, "start_km", path, area.startKm);
	reader.numberField(value, "end_km", path, area.endKm);
	reader.integerField(value, "level", path, area.level);
	reader.stringField(value, "track", path, area.trackId, false);
	return area;
}

bool isSimulationResultField(const std::string& name) {
	return name == "results" || name == "simulation_results" || name.find("simulated_") == 0
		|| name.find("actual_") == 0 || name == "delay_seconds" || name.find("arrival_delay") != std::string::npos
		|| name.find("departure_delay") != std::string::npos;
}

std::vector<ScenePassenger> decodePassengers(const SceneFieldReader& reader, const json& root) {
	std::vector<ScenePassenger> passengers;
	if (!reader.arraySection(root, "passengers", "", true))
		return passengers;
	for (std::size_t index = 0; index < root["passengers"].size(); ++index) {
		const std::string path = "passengers[" + std::to_string(index) + "]";
		const json& value = root["passengers"][index];
		ScenePassenger passenger;
		reader.stringField(value, "id", path, passenger.id);
		rejectResultFields(reader, value, path);
		if (reader.arraySection(value, "journeys", path, true)) {
			for (std::size_t journeyIndex = 0; journeyIndex < value["journeys"].size(); ++journeyIndex)
				passenger.journeys.push_back(decodePassengerJourney(reader, value["journeys"][journeyIndex],
					path + ".journeys[" + std::to_string(journeyIndex) + "]"));
		}
		passengers.push_back(std::move(passenger));
	}
	return passengers;
}
