#include "scene/SceneModel.h"
#include "scene/SceneFieldReader.h"
#include "scene/StagedDirectory.h"
#include "scene/Utf8Path.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

using json = nlohmann::json;
namespace fs = std::filesystem;

std::string buildSceneDirectorySnapshot(
	const std::vector<std::pair<std::string, std::string>>& files) {
	std::vector<std::pair<std::string, std::string>> sorted = files;
	std::sort(sorted.begin(), sorted.end(), [](const auto& first, const auto& second) {
		return first.first < second.first;
	});

	std::string snapshot;
	for (const auto& file : sorted) {
		snapshot += std::to_string(file.first.size());
		snapshot += ':';
		snapshot += file.first;
		snapshot += ':';
		snapshot += std::to_string(file.second.size());
		snapshot += ':';
		snapshot += file.second;
	}
	return snapshot;
}

SceneInputSnapshot readSceneDirectorySnapshot(const std::string& sceneDir) {
	SceneInputSnapshot result;
	std::error_code ec;
	const fs::path directory = pathFromUtf8(sceneDir);
	if (!fs::is_directory(directory, ec) || ec) {
		result.reason = "scene directory is missing or unreadable";
		return result;
	}

	std::vector<std::pair<std::string, std::string>> files;
	const auto read = [&](const char* name) {
		const fs::path path = directory / name;
		ec.clear();
		if (!fs::is_regular_file(path, ec) || ec) {
			result.reason = std::string("required scene input is missing or unreadable: ") + name;
			return false;
		}
		std::string content;
		if (!readFileBytes(path, content)) {
			result.reason = std::string("scene input is missing or unreadable: ") + name;
			return false;
		}
		files.emplace_back(name, std::move(content));
		return true;
	};
	const auto exists = [&](const char* name, bool& present) {
		ec.clear();
		present = fs::exists(directory / name, ec);
		if (!ec)
			return true;
		result.reason = std::string("scene input is missing or unreadable: ") + name;
		return false;
	};

	// An optional file counts when it exists, and then it has to be readable like a required one.
	for (const SceneFile& file : kSceneFiles) {
		bool present = file.required;
		if (!file.required && !exists(file.name, present))
			return result;
		if (present && !read(file.name))
			return result;
	}

	result.bytes = buildSceneDirectorySnapshot(files);
	return result;
}

std::string sceneOutputDirectoryComponent(const std::string& sceneName) {
	if (sceneName.empty() || sceneName.find_first_of("<>:\"/\\|?*") != std::string::npos
		|| std::any_of(sceneName.begin(), sceneName.end(), [](unsigned char value) {
			   return value <= 31;
		   })
		|| sceneName.back() == ' ' || sceneName.back() == '.')
		return "scene";
	const auto asciiLower = [](unsigned char value) {
		return value >= 'A' && value <= 'Z'
			? static_cast<unsigned char>(value + ('a' - 'A'))
			: value;
	};
	std::string basename = sceneName.substr(0, sceneName.find('.'));
	std::transform(basename.begin(), basename.end(), basename.begin(), asciiLower);
	const bool comOrLpt = basename.compare(0, 3, "com") == 0
		|| basename.compare(0, 3, "lpt") == 0;
	if (basename == "con" || basename == "prn" || basename == "aux" || basename == "nul"
		|| (comOrLpt && basename.size() == 4
			&& ((basename[3] >= '1' && basename[3] <= '9')
				|| static_cast<unsigned char>(basename[3]) == 0xB2
				|| static_cast<unsigned char>(basename[3]) == 0xB3
				|| static_cast<unsigned char>(basename[3]) == 0xB9))
		|| (comOrLpt && basename.size() == 5
			&& (basename.compare(3, 2, "\xC2\xB2") == 0
				|| basename.compare(3, 2, "\xC2\xB3") == 0
				|| basename.compare(3, 2, "\xC2\xB9") == 0)))
		return "scene";
	return sceneName;
}

bool sceneParseVisualizationColor(const std::string& text, int* red, int* green, int* blue) {
	if (text.size() != 7 || text[0] != '#')
		return false;
	int channel[3] = {};
	for (std::size_t index = 1; index < text.size(); ++index) {
		// Compared by range: the <cctype> tests can accept other characters in some locales.
		const char character = text[index];
		int digit = 0;
		if (character >= '0' && character <= '9')
			digit = character - '0';
		else if (character >= 'a' && character <= 'f')
			digit = character - 'a' + 10;
		else if (character >= 'A' && character <= 'F')
			digit = character - 'A' + 10;
		else
			return false;
		channel[(index - 1) / 2] = channel[(index - 1) / 2] * 16 + digit;
	}
	if (red)
		*red = channel[0];
	if (green)
		*green = channel[1];
	if (blue)
		*blue = channel[2];
	return true;
}

std::string sceneServiceVisualizationColor(const SceneModel& model, const std::string& serviceId) {
	const auto service = std::find_if(model.services.begin(), model.services.end(),
		[&serviceId](const SceneService& candidate) { return candidate.id == serviceId; });
	return service == model.services.end() ? std::string() : service->visualizationColor;
}

double sceneServiceScheduledEntry(const SceneService& service, int occurrence) {
	const double entry = service.hasEntryTime ? service.entryTimeSeconds
											  : (!service.stops.empty() && service.stops.front().hasPlannedDeparture
															&& std::isfinite(service.stops.front().plannedDepartureSeconds)
														? service.stops.front().plannedDepartureSeconds
														: 0.0);
	return entry + (service.hasRepeat ? (occurrence - 1.0) * service.headwaySeconds : 0.0);
}

int sceneServiceInWindowCount(const SceneService& service, double durationSeconds,
	const SceneRunSelection& selection) {
	if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0)
		return 0;
	const int total = sceneServiceOccurrenceCount(service, durationSeconds);
	const auto inWindow = [&](int occurrence) {
		const double entry = sceneServiceScheduledEntry(service, occurrence);
		return std::isfinite(entry) && entry >= 0.0 && entry < durationSeconds;
	};
	if (!selection.empty()) {
		int count = 0;
		for (const SceneServiceOccurrence& value : selection)
			if (value.serviceId == service.id && value.occurrence >= 1
				&& value.occurrence <= total && inWindow(value.occurrence))
				++count;
		return count;
	}
	if (!service.hasRepeat)
		return inWindow(1) ? 1 : 0;
	if (!std::isfinite(service.headwaySeconds) || service.headwaySeconds <= 0.0
		|| !std::isfinite(sceneServiceScheduledEntry(service)))
		return 0;
	const auto before = [&](double boundary) {
		int low = 0;
		int high = total;
		while (low < high) {
			const int middle = low + (high - low) / 2;
			if (sceneServiceScheduledEntry(service, middle + 1) < boundary)
				low = middle + 1;
			else
				high = middle;
		}
		return low;
	};
	return before(durationSeconds) - before(0.0);
}

int sceneServiceOccurrenceCount(const SceneService& service, double durationSeconds) {
	if (!service.hasRepeat)
		return 1;
	if (service.hasRepeatCount)
		return service.repeatCount > 0 ? service.repeatCount : 1;
	if (!std::isfinite(service.headwaySeconds) || service.headwaySeconds <= 0.0
		|| !std::isfinite(durationSeconds) || durationSeconds <= 0.0)
		return 1;
	const double rawCount = std::ceil(durationSeconds / service.headwaySeconds);
	if (!std::isfinite(rawCount) || rawCount >= static_cast<double>(INT_MAX))
		return INT_MAX;
	return std::max(1, static_cast<int>(rawCount));
}

std::string sceneServiceOccurrenceOperatingCode(const SceneService& service, int occurrence) {
	if (occurrence <= 0)
		return {};
	std::string base = service.operatingCode.empty() ? service.id : service.operatingCode;
	if (base.empty())
		return {};
	if (service.hasOperatingCodeStep) {
		if (!service.hasRepeat || service.operatingCodeStep == 0
			|| !std::all_of(base.begin(), base.end(), [](unsigned char value) {
				   return std::isdigit(value) != 0;
			   }))
			return {};
		try {
			const long long baseValue = std::stoll(base);
			const long long delta = static_cast<long long>(service.operatingCodeStep)
				* static_cast<long long>(occurrence - 1);
			if ((delta > 0 && baseValue > std::numeric_limits<long long>::max() - delta)
				|| (delta < 0 && baseValue < std::numeric_limits<long long>::min() - delta))
				return {};
			return std::to_string(baseValue + delta);
		} catch (const std::exception&) {
			return {};
		}
	}
	if (service.hasRepeat)
		return base + "-" + std::to_string(occurrence);
	return base;
}

bool resolveScenePassengerLegStops(const SceneService& service, const ScenePassengerLeg& leg,
	SceneServiceStopPair& result) {
	for (std::size_t originIndex = 0; originIndex < service.stops.size(); ++originIndex) {
		if (service.stops[originIndex].stationId != leg.originStationId)
			continue;
		for (std::size_t destinationIndex = originIndex + 1; destinationIndex < service.stops.size();
			++destinationIndex) {
			if (service.stops[destinationIndex].stationId == leg.destinationStationId) {
				result.originIndex = originIndex;
				result.destinationIndex = destinationIndex;
				return true;
			}
		}
	}
	return false;
}

SceneScenario makeBaselineScenario() {
	SceneScenario baseline;
	baseline.id = "baseline";
	baseline.name = "Baseline";
	return baseline;
}

double scenePlatformCapacity(double lengthM, double widthM) {
	return lengthM * widthM / (3.14159 * std::pow(0.8, 2)) * 0.8;
}

SceneModel makeNewSceneModel() {
	SceneModel scene;
	scene.schemaVersion = kCurrentSceneSchemaVersion;
	scene.name = "Untitled case";
	scene.baseTime = "08:00:00";
	scene.settings.hasDuration = true;
	scene.settings.durationSeconds = 3600.0;
	scene.settings.hasBufferTime = true;
	scene.settings.bufferTimeSeconds = 0.0;
	scene.settings.hasRecoveryTime = true;
	scene.settings.recoveryTimePercent = 0.0;
	const SceneScenario baseline = makeBaselineScenario();
	scene.defaultScenarioId = baseline.id;
	scene.scenarios.push_back(baseline);
	return scene;
}

double sceneTrainMassFactor(const SceneTrainPhysical& physical) {
	const double wagonMass = physical.mass_of_a_wagon_kg * physical.number_of_wagons;
	return (1.09 * physical.mass_of_traction_unit_kg + 1.06 * wagonMass)
		/ (physical.mass_of_traction_unit_kg + wagonMass);
}

bool buildSceneComposition(const SceneModel& scene, const std::string& compositionId,
	SceneCompositionRuntime& result, std::string& diagnostic) {
	result = SceneCompositionRuntime();
	diagnostic.clear();

	const SceneComposition* composition = nullptr;
	for (const auto& candidate : scene.compositions) {
		if (candidate.id == compositionId) {
			composition = &candidate;
			break;
		}
	}
	if (!composition) {
		diagnostic = "Unknown composition " + compositionId;
		return false;
	}
	if (composition->units.empty()) {
		diagnostic = "Composition " + compositionId + " has no units";
		return false;
	}

	std::unordered_map<std::string, const SceneTrainUnit*> unitsById;
	for (const auto& unit : scene.trainUnits)
		unitsById.emplace(unit.id, &unit);
	for (const auto& unitId : composition->units) {
		const auto it = unitsById.find(unitId);
		if (it == unitsById.end()) {
			diagnostic = "Missing unit " + unitId + " for composition " + compositionId;
			return false;
		}
		if (!it->second->hasPhysical || it->second->tractionCurve.empty()) {
			diagnostic = "Unit missing physical or traction data: " + unitId;
			return false;
		}
		result.units.push_back(it->second);
	}

	if (result.units.size() == 1) {
		result.physical = result.units.front()->physical;
		result.tractionCurve = result.units.front()->tractionCurve;
		return true;
	}

	double sumMassTraction = 0.0;
	double sumNumberWagons = 0.0;
	double sumWagonMass = 0.0;
	double minMaxSpeed = std::numeric_limits<double>::infinity();
	double minMaxDecel = std::numeric_limits<double>::infinity();
	const double firstFrontalArea = result.units.front()->physical.frontal_area_m2;
	double sumTotalMass = 0.0;
	double sumWeightedResistance = 0.0;
	double sumUnweightedResistance = 0.0;
	double minJerk = std::numeric_limits<double>::infinity();
	double sumLength = 0.0;

	for (const SceneTrainUnit* unit : result.units) {
		const SceneTrainPhysical& physical = unit->physical;
		sumMassTraction += physical.mass_of_traction_unit_kg;
		sumNumberWagons += physical.number_of_wagons;
		sumWagonMass += physical.number_of_wagons * physical.mass_of_a_wagon_kg;
		if (physical.max_speed_ms < minMaxSpeed)
			minMaxSpeed = physical.max_speed_ms;
		if (physical.max_deceleration_ms2 < minMaxDecel)
			minMaxDecel = physical.max_deceleration_ms2;
		const double unitTotalMass = physical.mass_of_traction_unit_kg
			+ physical.number_of_wagons * physical.mass_of_a_wagon_kg;
		sumTotalMass += unitTotalMass;
		sumWeightedResistance += unitTotalMass * physical.resistance_coefficient;
		sumUnweightedResistance += physical.resistance_coefficient;
		if (physical.jerk_ms3 < minJerk)
			minJerk = physical.jerk_ms3;
		sumLength += physical.length_m;
	}

	result.physical.mass_of_traction_unit_kg = sumMassTraction;
	result.physical.number_of_wagons = sumNumberWagons;
	result.physical.mass_of_a_wagon_kg = sumNumberWagons > 0.0
		? sumWagonMass / sumNumberWagons
		: 0.0;
	result.physical.max_speed_ms = minMaxSpeed;
	result.physical.max_deceleration_ms2 = minMaxDecel;
	result.physical.frontal_area_m2 = firstFrontalArea;
	result.physical.resistance_coefficient = sumTotalMass > 0.0
		? sumWeightedResistance / sumTotalMass
		: sumUnweightedResistance / result.units.size();
	result.physical.jerk_ms3 = minJerk;
	result.physical.length_m = sumLength;

	std::vector<double> boundaries;
	for (const SceneTrainUnit* unit : result.units) {
		for (const auto& row : unit->tractionCurve) {
			boundaries.push_back(row[0]);
			boundaries.push_back(row[1]);
		}
	}
	std::sort(boundaries.begin(), boundaries.end());
	const auto nearlyEqual = [](double left, double right) {
		return std::abs(left - right) <= 1e-9
			* std::max(1.0, std::max(std::abs(left), std::abs(right)));
	};
	boundaries.erase(std::unique(boundaries.begin(), boundaries.end(), nearlyEqual), boundaries.end());

	for (std::size_t index = 0; index + 1 < boundaries.size(); ++index) {
		const double lower = boundaries[index];
		const double upper = boundaries[index + 1];
		if (lower >= upper)
			continue;
		const double midpoint = (lower + upper) / 2.0;
		double sumC0 = 0.0;
		double sumC1 = 0.0;
		double sumC2 = 0.0;
		bool covered = false;
		for (const SceneTrainUnit* unit : result.units) {
			for (const auto& row : unit->tractionCurve) {
				if (midpoint >= row[0] && midpoint < row[1]) {
					sumC0 += row[2];
					sumC1 += row[3];
					sumC2 += row[4];
					covered = true;
				}
			}
		}
		if (covered)
			result.tractionCurve.push_back({lower, upper, sumC0, sumC1, sumC2});
	}

	if (result.tractionCurve.empty()) {
		diagnostic = "Combined traction curve is empty for composition " + compositionId;
		return false;
	}
	return true;
}

SceneScenario* defaultScenario(SceneModel& scene) {
	if (scene.scenarios.empty())
		scene.scenarios.push_back(makeBaselineScenario());
	if (scene.defaultScenarioId.empty())
		scene.defaultScenarioId = scene.scenarios.front().id;
	for (auto& scenario : scene.scenarios) {
		if (scenario.id == scene.defaultScenarioId)
			return &scenario;
	}
	return &scene.scenarios.front();
}

const SceneScenario* defaultScenario(const SceneModel& scene) {
	if (scene.scenarios.empty())
		return nullptr;
	if (scene.defaultScenarioId.empty())
		return &scene.scenarios.front();
	for (const auto& scenario : scene.scenarios) {
		if (scenario.id == scene.defaultScenarioId)
			return &scenario;
	}
	return &scene.scenarios.front();
}

std::string sceneScenarioLabel(const SceneModel& scene, const SceneScenario& scenario,
	const std::vector<std::string>& markers) {
	const auto shownName = [](const SceneScenario& item) { return item.name.empty() ? item.id : item.name; };
	std::string name = shownName(scenario);
	std::vector<std::string> notes;
	if (std::any_of(scene.scenarios.begin(), scene.scenarios.end(), [&](const SceneScenario& other) {
			return &other != &scenario && other.id != scenario.id && shownName(other) == name;
		}))
		notes.push_back(scenario.id);
	notes.insert(notes.end(), markers.begin(), markers.end());
	if (notes.empty())
		return name;
	std::string label = name + " (" + notes.front();
	for (std::size_t index = 1; index < notes.size(); ++index)
		label += ", " + notes[index];
	return label + ")";
}

std::vector<SceneIncident>& defaultScenarioIncidents(SceneModel& scene) {
	return defaultScenario(scene)->incidents;
}

const std::vector<SceneIncident>& defaultScenarioIncidents(const SceneModel& scene) {
	static const std::vector<SceneIncident> empty;
	const SceneScenario* scenario = defaultScenario(scene);
	return scenario ? scenario->incidents : empty;
}

std::vector<SceneSignal>& sceneSignals(SceneModel& scene) {
	return scene.signals;
}

const std::vector<SceneSignal>& sceneSignals(const SceneModel& scene) {
	return scene.signals;
}

bool isSceneFileName(const std::string& name) {
	return std::any_of(kSceneFiles.begin(), kSceneFiles.end(), [&name](const SceneFile& file) {
		return name == file.name;
	});
}

bool sceneFileWritten(const SceneFile& file, const SceneModel& scene) {
	switch (file.write) {
		case SceneFileWrite::Always:
			return true;
		case SceneFileWrite::WithViews:
			return !scene.trackViews.empty() || !scene.stationViews.empty();
		case SceneFileWrite::WithPassengers:
			return !scene.passengers.empty();
		case SceneFileWrite::Never:
			return false;
	}
	return false;
}

void refreshSavedSceneMetadata(SceneModel& scene) {
	for (const SceneFile& file : kSceneFiles) {
		if (sceneFileWritten(file, scene))
			scene.sourceFiles.insert(file.name);
		else
			scene.sourceFiles.erase(file.name);
	}
}

SceneLoadResult loadScene(const std::string& sceneDir) {
	SceneLoadResult result;
	auto addDiagnostic = [&](SceneSeverity severity, const std::string& code,
							 const std::string& file, const std::string& message, const std::string& path = "") {
		SceneDiagnostic diagnostic;
		diagnostic.severity = severity;
		diagnostic.code = code;
		diagnostic.file = file;
		diagnostic.message = message;
		diagnostic.path = path;
		result.diagnostics.push_back(diagnostic);
	};
	auto addError = [&](const std::string& code, const std::string& file,
						const std::string& message, const std::string& path = "") {
		addDiagnostic(SceneSeverity::Error, code, file, message, path);
	};
	auto addWarning = [&](const std::string& code, const std::string& file,
						  const std::string& message, const std::string& path = "") {
		addDiagnostic(SceneSeverity::Warning, code, file, message, path);
	};

	const fs::path directory = pathFromUtf8(sceneDir);
	std::error_code directoryError;
	const fs::file_type directoryType = fs::status(directory, directoryError).type();
	if (directoryType != fs::file_type::directory) {
		if (directoryError && directoryType != fs::file_type::not_found)
			addError("scene.dir.unreadable", "", "Cannot read the case folder: " + directoryError.message());
		else
			addError("scene.dir.missing", "", "Case folder missing or invalid");
		return result;
	}

	std::vector<std::pair<std::string, std::string>> inputFiles;
	auto parseObject = [&](const std::string& file, json& value, bool required, bool parseValue = true) {
		std::string content;
		if (!readFileBytes(directory / file, content)) {
			if (required)
				addError("scene.file.missing", file, "Required file is missing");
			return false;
		}
		if (isSceneFileName(file))
			inputFiles.emplace_back(file, content);
		if (!parseValue)
			return true;
		try {
			value = json::parse(content);
		} catch (const json::parse_error& error) {
			addError("scene.json.parse", file, std::string("JSON parse error: ") + error.what());
			return false;
		}
		if (!value.is_object()) {
			addError("scene.section.missing", file, "Root element must be an object");
			return false;
		}
		result.scene.sourceFiles.insert(file);
		return true;
	};

	json sceneJson;
	json infrastructureJson;
	json stationsJson;
	json signallingJson;
	json rollingStockJson;
	json servicesJson;
	json scenariosJson;
	json incidentsJson;
	json passengersJson;
	json viewsJson;
	const bool sceneOk = parseObject("scene.json", sceneJson, true);
	const bool infrastructureOk = parseObject("infrastructure.json", infrastructureJson, true);
	const bool stationsOk = parseObject("stations.json", stationsJson, true);
	const bool signallingOk = parseObject("signalling.json", signallingJson, true);
	const bool rollingStockOk = parseObject("rolling_stock.json", rollingStockJson, true);
	const bool servicesOk = parseObject("services.json", servicesJson, true);
	// A file that cannot be checked counts as absent.
	std::error_code presenceError;
	const bool scenariosPresent = fs::exists(directory / "scenarios.json", presenceError);
	const bool incidentsPresent = fs::exists(directory / "incidents.json", presenceError);
	const bool scenariosOk = parseObject("scenarios.json", scenariosJson, scenariosPresent);
	const bool incidentsOk = parseObject("incidents.json", incidentsJson, incidentsPresent, !scenariosPresent);
	const bool passengersOk = parseObject("passengers.json", passengersJson,
		fs::exists(directory / "passengers.json", presenceError));
	const bool viewsOk = parseObject("views.json", viewsJson, false);

	// Each file is read with a reader of its own. views.json names tracks and stations, so it is read after
	// infrastructure.json and stations.json.
	const auto readerFor = [&result](const char* file) {
		return SceneFieldReader(file, kSceneFileFieldCodes, result.diagnostics);
	};
	if (sceneOk)
		decodeSceneMetadata(readerFor("scene.json"), sceneJson, result.scene);
	if (infrastructureOk)
		decodeInfrastructure(readerFor("infrastructure.json"), infrastructureJson, result.scene);
	if (stationsOk)
		decodeStations(readerFor("stations.json"), stationsJson, result.scene);
	if (viewsOk)
		decodeViews(readerFor("views.json"), viewsJson, result.scene);
	if (signallingOk)
		decodeSignalling(readerFor("signalling.json"), signallingJson, result.scene);
	if (rollingStockOk)
		decodeRollingStock(readerFor("rolling_stock.json"), rollingStockJson, result.scene);
	if (servicesOk)
		decodeServices(readerFor("services.json"), servicesJson, result.scene);

	if (scenariosPresent && incidentsPresent) {
		addWarning("scene.compatibility.incidents_ignored", "incidents.json",
			"incidents.json is ignored, because the case has scenarios.json");
	}
	if (scenariosPresent) {
		if (scenariosOk)
			decodeScenarios(readerFor("scenarios.json"), scenariosJson, result.scene);
	} else {
		SceneScenario baseline = makeBaselineScenario();
		result.scene.defaultScenarioId = baseline.id;
		result.scene.scenarios.push_back(baseline);
		if (incidentsPresent && incidentsOk)
			decodeHistoricalIncidents(readerFor("incidents.json"), incidentsJson, result.scene);
	}

	if (passengersOk)
		decodePassengers(readerFor("passengers.json"), passengersJson, result.scene);

	result.inputSnapshot = buildSceneDirectorySnapshot(inputFiles);
	return result;
}
