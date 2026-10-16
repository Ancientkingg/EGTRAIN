#include "simulation/RollingStock.h"
#include "scene/SceneModel.h"
#include "scene/SectionInventory.h"
#include "simulation/Optimisation.h"
#include "simulation/Passengers.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

struct NativeStopPlan {
	std::size_t sourceStopIndex = 0;
	std::string stationId;
	std::string stationName;
	std::string platformId;
	Node node;
	double dwellSeconds = 0.0;
	bool hasPlannedArrival = false;
	bool hasPlannedDeparture = false;
	double plannedArrival = -1.0;
	double plannedDeparture = -1.0;
};

struct NativeTrainPlan {
	std::string routeId;
	std::string trainDescription;
	std::string type;
	std::string operatingCode;
	std::string serviceId;
	SceneTrainPhysical physical;
	std::vector<std::array<double, 5>> tractionCurve;
	std::vector<NativeStopPlan> stops;
	int occurrence = 1;
	int routeIndex = -1;
	bool direction = false;
	double servicePerformancePercent = 100.0;
	bool hasConfiguredMaximumSpeed = false;
	double configuredMaximumSpeedKmh = 0.0;
	double compositionMaximumSpeedMs = 0.0;
	double appliedMaximumSpeedMs = 0.0;
	double appliedMaximumSpeedKmh = 0.0;
	bool destinationTerminationRequested = false;
	double scheduledDeparture = 0.0;
	double entranceDelay = 0.0;
};

bool nativeFinite(double value) {
	return std::isfinite(value);
}

void addNativeDiagnostic(std::vector<SceneDiagnostic>& diagnostics, const std::string& code,
	const std::string& message, const std::string& file, const std::string& itemType,
	const std::string& itemId, const std::string& path = {}, const std::string& relatedId = {},
	const std::string& suggestedFix = {}, SceneSeverity severity = SceneSeverity::Error) {
	SceneDiagnostic diagnostic;
	diagnostic.severity = severity;
	diagnostic.code = code;
	diagnostic.message = message;
	diagnostic.file = file;
	diagnostic.itemType = itemType;
	diagnostic.itemId = itemId;
	diagnostic.path = path;
	diagnostic.relatedId = relatedId;
	diagnostic.suggestedFix = suggestedFix;
	diagnostics.push_back(std::move(diagnostic));
}

bool parseNativeBaseTime(const std::string& value, int& seconds) {
	if (value.size() != 8 || value[2] != ':' || value[5] != ':')
		return false;
	for (std::size_t i = 0; i < value.size(); ++i) {
		if (i == 2 || i == 5)
			continue;
		if (value[i] < '0' || value[i] > '9')
			return false;
	}
	const int hours = (value[0] - '0') * 10 + value[1] - '0';
	const int minutes = (value[3] - '0') * 10 + value[4] - '0';
	const int partSeconds = (value[6] - '0') * 10 + value[7] - '0';
	if (hours > 23 || minutes > 59 || partSeconds > 59)
		return false;
	seconds = hours * 3600 + minutes * 60 + partSeconds;
	return true;
}

template <typename T>
std::unordered_map<std::string, const T*> nativeIndexById(const std::vector<T>& values,
	std::vector<SceneDiagnostic>& diagnostics, const std::string& file,
	const std::string& itemType) {
	std::unordered_map<std::string, const T*> result;
	for (const T& value : values) {
		if (value.id.empty()) {
			addNativeDiagnostic(diagnostics, "scene.native.ref.id", "An item has an empty canonical ID",
				file, itemType, "");
			continue;
		}
		if (!result.emplace(value.id, &value).second)
			addNativeDiagnostic(diagnostics, "scene.native.ref.duplicate", "Duplicate canonical ID",
				file, itemType, value.id);
	}
	return result;
}

const ScenePlatform* nativePlatformForStation(const SceneStation& station, const std::string& platformId) {
	for (const ScenePlatform& platform : station.platforms)
		if (platform.id == platformId)
			return &platform;
	return nullptr;
}

const SceneStop* nativeStopForStation(const SceneService& service, const std::string& stationId) {
	for (const SceneStop& stop : service.stops)
		if (stop.stationId == stationId)
			return &stop;
	return nullptr;
}

const NativeStopPlan* nativeStopAtSourceIndex(const NativeTrainPlan& train, std::size_t sourceStopIndex) {
	for (const NativeStopPlan& stop : train.stops)
		if (stop.sourceStopIndex == sourceStopIndex)
			return &stop;
	return nullptr;
}

int nativeRouteIndex(const std::string& routeId) {
	for (std::size_t index = 0; index < train_route.size(); ++index)
		if (train_route[index].ID == routeId)
			return static_cast<int>(index);
	return -1;
}

const SceneStation* nativeStationForId(
	const std::unordered_map<std::string, const SceneStation*>& stations,
	const std::string& stationId) {
	const auto found = stations.find(stationId);
	return found == stations.end() ? nullptr : found->second;
}

bool nativeRuntimeNodeMatchesStation(const Node& node, const SceneStation& station,
	const std::string& stationName) {
	return (station.platforms.empty() ? node.station
									  : !node.stationPlatformId.empty() && node.stationPlatformId != "None")
		&& (node.stationName == stationName || node.stationName == station.id
			|| (node.station && node.stationName.empty()));
}

bool nativeRuntimeNodeForVisit(const Route& route, const SceneStopResolution& resolution,
	Node& result) {
	if (resolution.sectionIndex >= static_cast<std::size_t>(route.N_Block_Sections))
		return false;
	const Section& section = route.sequence_of_block_sections[resolution.sectionIndex];
	if (section.ID != resolution.sectionId)
		return false;
	const auto matches = [&resolution](const Node& node) {
		return !resolution.nodeId.empty() && node.sceneNodeId == resolution.nodeId;
	};
	if (matches(section.start_node)) {
		result = section.start_node;
		return true;
	}
	for (int arcIndex = 0; arcIndex < section.total_arcs; ++arcIndex) {
		if (!matches(section.arcs_in_signalling_block_section[arcIndex].endNode))
			continue;
		result = section.arcs_in_signalling_block_section[arcIndex].endNode;
		return true;
	}
	return false;
}

bool nativeRuntimePlatformExists(const std::string& platformId, const std::string& stationId,
	const std::string& stationName) {
	for (const StationPlatform& platform : AllStationPlatforms)
		if (platform.ID == platformId
			&& (platform.StationID == stationId || platform.StationID == stationName))
			return true;
	return false;
}


int nativeResolveRuntimeSection(const std::string& runtimeSectionId) {
	if (runtimeSectionId.empty())
		return -1;
	for (int index = 0; index < Blocks; ++index)
		if (signalling_block_sections[index].ID == runtimeSectionId)
			return index;
	return -1;
}

void nativeCopyTrainPlan(const NativeTrainPlan& plan, Regional& train, int vectorSize) {
	train.g = kSceneGravityMs2;
	train.ID = plan.occurrence;
	train.type = plan.type;
	train.operatingCode = plan.operatingCode;
	train.serviceId = plan.serviceId;
	train.serviceOccurrence = plan.occurrence;
	train.servicePerformancePercent = plan.servicePerformancePercent;
	train.hasConfiguredMaximumSpeed = plan.hasConfiguredMaximumSpeed;
	train.configuredMaximumSpeedKmh = plan.configuredMaximumSpeedKmh;
	train.compositionMaximumSpeedMs = plan.compositionMaximumSpeedMs;
	train.appliedMaximumSpeedMs = plan.appliedMaximumSpeedMs;
	train.appliedMaximumSpeedKmh = plan.appliedMaximumSpeedKmh;
	train.destinationTerminationRequested = plan.destinationTerminationRequested;
	train.destinationTerminated = false;
	train.directIncidentIds.clear();
	train.firstDirectIncidentTime = -1.0;
	train.firstDirectIncidentLocation = -1.0;
	train.TrainRouteID = plan.routeId;
	train.indexOfRoute = plan.routeIndex;
	train.trainDescription = plan.trainDescription;
	train.direction = plan.direction;
	train.Start_Node_X = train_route[plan.routeIndex].x_of_start_node;
	train.mass_of_traction_unit = plan.physical.mass_of_traction_unit_kg;
	train.mass_of_a_wagon = plan.physical.mass_of_a_wagon_kg;
	train.number_of_wagons = plan.physical.number_of_wagons;
	train.MAX_OnBoard_Passengers = trainPassengerCapacity(train.number_of_wagons);
	train.max_train_speed = plan.appliedMaximumSpeedMs;
	train.max_train_decelaration = plan.physical.max_deceleration_ms2;
	train.frontal_wagon_area = plan.physical.frontal_area_m2;
	train.resistanceCoefficient = plan.physical.resistance_coefficient;
	train.Jerk = plan.physical.jerk_ms3;
	train.train_length = plan.physical.length_m;
	train.massPerWagonAxle = train.mass_of_a_wagon * train.number_of_wagons;
	train.total_train_mass = train.mass_of_traction_unit + train.massPerWagonAxle;
	train.massFactor = sceneTrainMassFactor(plan.physical);
	train.velocityIntervals = static_cast<int>(plan.tractionCurve.size());
	for (int index = 0; index < train.velocityIntervals; ++index) {
		train.Vlb[index] = plan.tractionCurve[index][0];
		train.Vub[index] = plan.tractionCurve[index][1];
		train.C0[index] = plan.tractionCurve[index][2];
		train.C1[index] = plan.tractionCurve[index][3];
		train.C2[index] = plan.tractionCurve[index][4];
	}
	train.scheduled_departure_time = plan.scheduledDeparture;
	train.departure_time = plan.scheduledDeparture;
	train.EntranceDelay = plan.entranceDelay;
	train.TotalInputDelays = plan.entranceDelay;
	train.Initialise_Gibson_Dwell_Time_Parameters(7, 0.32, 18.23, 0.564, 4.838, 22.24, 0.04, 0.562);
	train.numStations = static_cast<int>(plan.stops.size());
	train.Stations.resize(plan.stops.size());
	for (int index = 0; index < train.numStations; ++index) {
		train.Stations[index] = plan.stops[index].node;
		train.Stations[index].stationName = plan.stops[index].stationName;
		train.Stations[index].stationPlatformId = plan.stops[index].platformId;
		train.Stations[index].dwellTime = plan.stops[index].dwellSeconds;
		train.Stations[index].StopTime = plan.stops[index].dwellSeconds / timestep;
		train.StationArrivalNames[index] = plan.stops[index].stationName;
		train.ScheduledArrivals[index] = plan.stops[index].plannedArrival;
		train.ScheduledDepartures[index] = plan.stops[index].plannedDeparture;
	}
	train.setTrainVectorSizesFromInput(vectorSize);
	train.cacheStationPositions();
}

} // namespace

void prepareNativeOperationsState() {
	N_OrderLists = 0;
	numRegions = 0;
	N_Train = 0;
	N_TrainD = 0;
	numAllStationPlatforms = 0;
	numAllDailyPassengers = 0;
	for (OrderList& orderList : OL)
		orderList = OrderList();
	AllStationPlatforms.clear();
	AllDailyPassengers.clear();
	AllStationPlatformIndex.clear();
	AllDailyPassengerIndex.clear();
	simulationIncidents.clear();
	VCmsgTimestep.clear();
	VCmsgTrain.clear();
	VCmsgText.clear();
}

void resetNativeOperationsState() {
	prepareNativeOperationsState();
	regional_train = std::vector<Regional>();
}

std::vector<SceneDiagnostic> buildOperationsFromScene(const SceneModel& scene,
	const std::string& selectedScenarioId, const SceneRunSelection& selectedOccurrences) {
	// The passenger windows below and every draw of the run come from this generator.
	seedRunNumberGenerator(initial_variables.randomSeed);
	std::vector<SceneDiagnostic> diagnostics;
	nativeIndexById(scene.trainUnits, diagnostics, "trains.json", "train_unit");
	nativeIndexById(scene.compositions, diagnostics, "trains.json", "composition");
	const auto stationById = nativeIndexById(scene.stations, diagnostics, "stations.json", "station");
	const auto routes = nativeIndexById(scene.routes, diagnostics, "signalling.json", "route");
	const auto serviceById = nativeIndexById(scene.services, diagnostics, "services.json", "service");
	const bool hasLegacyImport = std::any_of(scene.importReport.begin(), scene.importReport.end(),
		[](const SceneImportReportRow& row) { return row.category == "legacy_root"; });
	for (std::size_t stationIndex = 0; stationIndex < scene.stations.size(); ++stationIndex) {
		const SceneStation& station = scene.stations[stationIndex];
		for (std::size_t platformIndex = 0; platformIndex < station.platforms.size(); ++platformIndex) {
			const ScenePlatform& platform = station.platforms[platformIndex];
			const std::string path = "stations[" + std::to_string(stationIndex) + "].platforms["
				+ std::to_string(platformIndex) + "]";
			if (platform.hasLength && (!nativeFinite(platform.lengthM) || platform.lengthM <= 0.0))
				addNativeDiagnostic(diagnostics, "scene.native.platform.length", "Platform length_m must be positive and finite",
					"stations.json", "platform", platform.id, path + ".length_m", {},
					"Use a platform length greater than 0 metres");
			if (platform.hasWidth && (!nativeFinite(platform.widthM) || platform.widthM <= 0.0))
				addNativeDiagnostic(diagnostics, "scene.native.platform.width", "Platform width_m must be positive and finite",
					"stations.json", "platform", platform.id, path + ".width_m", {},
					"Use a platform width greater than 0 metres");
			const double effectiveLength = platform.hasLength ? platform.lengthM : 100.0;
			const double effectiveWidth = platform.hasWidth ? platform.widthM : 2.5;
			const double capacity = effectiveLength * effectiveWidth
				/ (3.14159 * std::pow(0.8, 2)) * 0.8;
			if (nativeFinite(effectiveLength) && effectiveLength > 0.0
				&& nativeFinite(effectiveWidth) && effectiveWidth > 0.0
				&& (!nativeFinite(capacity) || capacity < 1.0
					|| capacity > static_cast<double>(INT_MAX)))
				addNativeDiagnostic(diagnostics, "scene.native.platform.capacity",
					"Platform geometry produces an unsupported passenger capacity", "stations.json",
					"platform", platform.id, path, {},
					"Use dimensions that produce at least one passenger and fit the runtime capacity field");
		}
	}
	if (scene.services.empty())
		addNativeDiagnostic(diagnostics, "scene.native.services.none", "A runnable scene requires at least one service",
			"services.json", "service", "", "services");

	int baseTime = 0;
	if (!parseNativeBaseTime(scene.baseTime, baseTime))
		addNativeDiagnostic(diagnostics, "scene.native.time.base", "base_time must be HH:MM:SS",
			"scene.json", "scene", scene.name, "base_time");
	const double durationSeconds = initial_variables.durationOverride
		? initial_variables.times
		: scene.settings.durationSeconds;
	if (!scene.settings.hasDuration || !nativeFinite(durationSeconds)
		|| durationSeconds < 1.0
		|| durationSeconds > static_cast<double>(INT_MAX))
		addNativeDiagnostic(diagnostics, "scene.native.time.duration", "A positive finite simulation duration is required",
			"scene.json", "scene", scene.name, "settings.duration_seconds");
	if (scene.settings.hasBufferTime && (!nativeFinite(scene.settings.bufferTimeSeconds) || scene.settings.bufferTimeSeconds < 0.0))
		addNativeDiagnostic(diagnostics, "scene.native.settings.buffer", "buffer_time_seconds must be finite and non-negative",
			"scene.json", "scene", scene.name, "settings.buffer_time_seconds");
	if (scene.settings.hasRecoveryTime && (!nativeFinite(scene.settings.recoveryTimePercent) || scene.settings.recoveryTimePercent < 0.0))
		addNativeDiagnostic(diagnostics, "scene.native.settings.recovery", "recovery_time_percent must be finite and non-negative",
			"scene.json", "scene", scene.name, "settings.recovery_time_percent");
	std::unordered_map<std::string, int> repeatCounts;
	for (const SceneService& service : scene.services)
		repeatCounts[service.id] = sceneServiceOccurrenceCount(service, durationSeconds);

	const auto defaultScenario = [&]() -> const SceneScenario* {
		if (!selectedScenarioId.empty()) {
			for (const SceneScenario& scenario : scene.scenarios)
				if (scenario.id == selectedScenarioId)
					return &scenario;
			return nullptr;
		}
		if (!scene.defaultScenarioId.empty()) {
			for (const SceneScenario& scenario : scene.scenarios)
				if (scenario.id == scene.defaultScenarioId)
					return &scenario;
			return nullptr;
		}
		return scene.scenarios.empty() ? nullptr : &scene.scenarios.front();
	};
	const SceneScenario* scenario = defaultScenario();
	if (scenario == nullptr)
		addNativeDiagnostic(diagnostics, "scene.native.scenario", "The selected/default scenario does not exist",
			"scenarios.json", "scenario", selectedScenarioId.empty() ? scene.defaultScenarioId : selectedScenarioId);

	std::unordered_map<std::string, int> routeById;
	for (const auto& pair : routes) {
		const int runtimeIndex = nativeRouteIndex(pair.first);
		if (runtimeIndex < 0 || runtimeIndex >= static_cast<int>(train_route.size())
			|| train_route[runtimeIndex].N_Block_Sections <= 0)
			addNativeDiagnostic(diagnostics, "scene.native.ref.route", "Route is not available in the built runtime infrastructure",
				"signalling.json", "route", pair.first, "routes[" + pair.first + "]");
		else
			routeById[pair.first] = runtimeIndex;
	}

	std::vector<NativeTrainPlan> trains;
	std::map<SceneServiceOccurrence, std::size_t> occurrenceIndex;
	if (!selectedOccurrences.empty()) {
		for (const SceneServiceOccurrence& selection : selectedOccurrences) {
			const auto serviceIt = serviceById.find(selection.serviceId);
			const auto countIt = repeatCounts.find(selection.serviceId);
			if (serviceIt == serviceById.end() || countIt == repeatCounts.end()) {
				addNativeDiagnostic(diagnostics, "scene.native.selection.service",
					"Selected occurrence refers to an unknown service", "services.json", "selection",
					selection.serviceId, "selected_occurrences", selection.serviceId);
			} else if (selection.occurrence < 1 || selection.occurrence > countIt->second) {
				addNativeDiagnostic(diagnostics, "scene.native.selection.occurrence",
					"Selected occurrence is outside the service horizon", "services.json", "selection",
					selection.serviceId, "selected_occurrences", selection.serviceId + "-" + std::to_string(selection.occurrence));
			}
		}
	}
	const auto occurrenceSelected = [&selectedOccurrences](const std::string& serviceId, int occurrence) {
		return selectedOccurrences.empty()
			|| selectedOccurrences.count(SceneServiceOccurrence{serviceId, occurrence}) > 0;
	};
	const SceneSectionInventory sectionInventory = buildSceneSectionInventory(scene);
	for (const SceneService& service : scene.services) {
		if (service.id.empty())
			continue;
		if (!nativeFinite(service.performancePercent) || service.performancePercent < 1.0
			|| service.performancePercent > 100.0)
			addNativeDiagnostic(diagnostics, "scene.native.service.performance",
				"Service performance_percent must be finite and between 1 and 100",
				"services.json", "service", service.id, "services[" + service.id + "].performance_percent");
		if (service.hasMaximumSpeed
			&& (!nativeFinite(service.maximumSpeedKmh) || service.maximumSpeedKmh <= 0.0))
			addNativeDiagnostic(diagnostics, "scene.native.service.speed",
				"Service maximum_speed_kmh must be positive and finite",
				"services.json", "service", service.id, "services[" + service.id + "].maximum_speed_kmh");
		if (service.hasRepeatCount && (!service.hasRepeat || service.repeatCount <= 0))
			addNativeDiagnostic(diagnostics, "scene.native.service.repeat_count",
				"Repeated service count must be a positive integer inside repeat",
				"services.json", "service", service.id, "services[" + service.id + "].repeat.count");
		if (service.hasOperatingCodeStep && sceneServiceOccurrenceOperatingCode(service, 1).empty())
			addNativeDiagnostic(diagnostics, "scene.native.service.operating_code_step",
				"Operating code step requires a nonzero step and a decimal base operating code",
				"services.json", "service", service.id,
				"services[" + service.id + "].repeat.operating_code_step");
		if (service.stops.size() > Train::kMaxTimetableStations)
			addNativeDiagnostic(diagnostics, "scene.native.capacity.stops", "Service stops exceed the runtime timetable capacity",
				"services.json", "service", service.id, "services[" + service.id + "].stops", {},
				std::to_string(Train::kMaxTimetableStations));
		const auto routeIt = routeById.find(service.route);
		if (routeIt == routeById.end())
			addNativeDiagnostic(diagnostics, "scene.native.ref.route", "Service route is unknown or unavailable",
				"services.json", "service", service.id, "services[" + service.id + "].route", service.route);
		SceneCompositionRuntime composition;
		std::string compositionDiagnostic;
		if (!buildSceneComposition(scene, service.composition, composition, compositionDiagnostic))
			addNativeDiagnostic(diagnostics, "scene.native.ref.composition",
				compositionDiagnostic.empty() ? "Service composition is unknown or invalid" : compositionDiagnostic,
				"trains.json", "service", service.id, "services[" + service.id + "].composition", service.composition);
		if (!composition.tractionCurve.empty()
			&& composition.tractionCurve.size() > 20)
			addNativeDiagnostic(diagnostics, "scene.native.capacity.traction", "Traction curve exceeds the runtime 20-band capacity",
				"trains.json", "composition", service.composition, "compositions[" + service.composition + "].units");
		if (!nativeFinite(composition.physical.mass_of_traction_unit_kg)
			|| !nativeFinite(composition.physical.mass_of_a_wagon_kg)
			|| !nativeFinite(composition.physical.number_of_wagons)
			|| !nativeFinite(composition.physical.max_speed_ms)
			|| !nativeFinite(composition.physical.max_deceleration_ms2)
			|| !nativeFinite(composition.physical.frontal_area_m2)
			|| !nativeFinite(composition.physical.resistance_coefficient)
			|| !nativeFinite(composition.physical.jerk_ms3)
			|| !nativeFinite(composition.physical.length_m)
			|| composition.physical.mass_of_traction_unit_kg < 0.0
			|| composition.physical.mass_of_a_wagon_kg < 0.0
			|| composition.physical.number_of_wagons < 0.0
			|| composition.physical.max_speed_ms <= 0.0
			|| composition.physical.max_deceleration_ms2 <= 0.0
			|| composition.physical.frontal_area_m2 < 0.0
			|| composition.physical.jerk_ms3 < 0.0
			|| composition.physical.length_m < 0.0
			|| composition.physical.mass_of_traction_unit_kg + composition.physical.mass_of_a_wagon_kg * composition.physical.number_of_wagons <= 0.0)
			addNativeDiagnostic(diagnostics, "scene.native.train.physical", "Composition physical values are non-finite or have no positive train mass",
				"trains.json", "service", service.id, "services[" + service.id + "].composition", service.composition);
		for (const auto& band : composition.tractionCurve) {
			if (!nativeFinite(band[0]) || !nativeFinite(band[1]) || !nativeFinite(band[2])
				|| !nativeFinite(band[3]) || !nativeFinite(band[4]) || band[1] <= band[0])
				addNativeDiagnostic(diagnostics, "scene.native.train.traction", "Composition contains an invalid traction band",
					"trains.json", "service", service.id, "services[" + service.id + "].composition", service.composition);
		}
		if (routeIt == routeById.end() || composition.tractionCurve.size() > 20)
			continue;

		const int occurrences = repeatCounts[service.id];
		double headway = 0.0;
		if (service.hasRepeat) {
			headway = service.headwaySeconds;
			if (!nativeFinite(headway) || headway <= 0.0)
				addNativeDiagnostic(diagnostics, "scene.native.timetable.repeat", "Repeated services require a positive finite headway",
					"services.json", "service", service.id, "services[" + service.id + "].headway_seconds");
			else if (service.hasRepeatCount && service.repeatCount <= 0)
				addNativeDiagnostic(diagnostics, "scene.native.timetable.repeat_count",
					"Repeated service count must be a positive integer", "services.json", "service", service.id,
					"services[" + service.id + "].repeat.count");
			else if (!service.hasRepeatCount) {
				const double rawCount = std::ceil(durationSeconds / headway);
				if (!nativeFinite(rawCount) || rawCount > static_cast<double>(INT_MAX))
					addNativeDiagnostic(diagnostics, "scene.native.capacity.occurrences", "Service repeat count exceeds the runtime integer capacity",
						"services.json", "service", service.id, "services[" + service.id + "].headway_seconds");
			}
		}
		if (service.hasOperatingCodeStep
			&& !sceneServiceOccurrenceOperatingCode(service, 1).empty()
			&& sceneServiceOccurrenceOperatingCode(service, occurrences).empty())
			addNativeDiagnostic(diagnostics, "scene.native.service.operating_code_step",
				"Operating code progression exceeds the supported integer range",
				"services.json", "service", service.id,
				"services[" + service.id + "].repeat.operating_code_step");

		std::vector<int> occurrencesToBuild;
		if (selectedOccurrences.empty()) {
			if (occurrences <= Max_N_Reg)
				for (int occurrence = 1; occurrence <= occurrences; ++occurrence)
					occurrencesToBuild.push_back(occurrence);
		} else {
			for (const SceneServiceOccurrence& selection : selectedOccurrences)
				if (selection.serviceId == service.id && selection.occurrence >= 1
					&& selection.occurrence <= occurrences)
					occurrencesToBuild.push_back(selection.occurrence);
		}
		const std::size_t selectedCount = selectedOccurrences.empty()
			? static_cast<std::size_t>(occurrences)
			: occurrencesToBuild.size();
		if (selectedCount > Max_N_Reg || trains.size() + selectedCount > Max_N_Reg) {
			addNativeDiagnostic(diagnostics, "scene.native.capacity.trains", "Expanded service occurrences exceed the runtime train capacity",
				"services.json", "service", service.id, "services[" + service.id + "].headway_seconds", {},
				std::to_string(Max_N_Reg));
			continue;
		}

		const Route& runtimeRoute = train_route[routeIt->second];
		const SceneRouteTraversal routeTraversal = buildSceneRouteTraversal(scene, *routes.at(service.route), sectionInventory);
		const std::vector<SceneStopResolution> stopResolutions =
			resolveSceneServiceStops(scene, service, routeTraversal);
		if (service.hasEntryTime && (!nativeFinite(service.entryTimeSeconds) || service.entryTimeSeconds < 0.0))
			addNativeDiagnostic(diagnostics, "scene.native.timetable.entry", "Entry time must be finite and non-negative",
				"services.json", "service", service.id, "services[" + service.id + "].entry_seconds");
		std::vector<NativeStopPlan> baseStops;
		for (std::size_t stopIndex = 0; stopIndex < service.stops.size(); ++stopIndex) {
			const SceneStop& stop = service.stops[stopIndex];
			const SceneStation* station = nativeStationForId(stationById, stop.stationId);
			if (station == nullptr) {
				addNativeDiagnostic(diagnostics, "scene.native.ref.station", "Stop station is unknown",
					"services.json", "service", service.id, "services[" + service.id + "].stops[" + std::to_string(stopIndex) + "].station_id", stop.stationId);
				continue;
			}
			const std::string stationName = station->name.empty() ? station->id : station->name;
			std::string resolvedPlatformId = stop.platformId;
			const ScenePlatform* explicitPlatform = nullptr;
			if (!stop.platformId.empty()) {
				explicitPlatform = nativePlatformForStation(*station, stop.platformId);
				if (explicitPlatform == nullptr || !nativeRuntimePlatformExists(stop.platformId, station->id, stationName))
					addNativeDiagnostic(diagnostics, "scene.native.ref.platform", "Explicit stop platform is unknown or not built in the runtime infrastructure",
						"services.json", "service", service.id, "services[" + service.id + "].stops[" + std::to_string(stopIndex) + "].platform_id", stop.platformId);
			}
			Node selectedNode;
			bool selected = false;
			const SceneStopResolution resolution = stopIndex < stopResolutions.size()
				? stopResolutions[stopIndex]
				: SceneStopResolution();
			if (resolution.status == SceneStopResolutionStatus::Resolved) {
				selected = nativeRuntimeNodeForVisit(runtimeRoute, resolution, selectedNode);
				if (!selected)
					addNativeDiagnostic(diagnostics, "scene.native.ref.platform",
						"Resolved stop visit is not present in the built runtime route",
						"services.json", "service", service.id,
						"services[" + service.id + "].stops[" + std::to_string(stopIndex) + "].platform_id",
						resolution.nodeId);
				else if (!nativeRuntimeNodeMatchesStation(selectedNode, *station, stationName)
					|| (!stop.platformId.empty() && selectedNode.stationPlatformId != stop.platformId)) {
					addNativeDiagnostic(diagnostics, "scene.native.ref.platform",
						"Resolved stop visit does not retain its canonical station/platform identity",
						"services.json", "service", service.id,
						"services[" + service.id + "].stops[" + std::to_string(stopIndex) + "].platform_id",
						stop.platformId);
					selected = false;
				}
				resolvedPlatformId = stop.platformId.empty()
					? (resolution.visitIndex < routeTraversal.visits.size()
							  ? routeTraversal.visits[resolution.visitIndex].platformId
							  : std::string())
					: stop.platformId;
			} else if (resolution.status == SceneStopResolutionStatus::OffRouteContext
				&& stop.platformId.empty()) {
				// Legacy timetables may retain stops before a train enters, or after it
				// leaves, its simulated route. Keep those schedule rows without inventing
				// a platform assignment; they remain inert in route station matching.
				selectedNode.station = true;
				selectedNode.stationName = stationName;
				selectedNode.stationPlatformId = "None";
				if (station->hasPosition)
					selectedNode.X = station->positionKm;
				else if (!station->platforms.empty()) {
					for (const StationPlatform& platform : AllStationPlatforms) {
						if (platform.ID == station->platforms.front().id) {
							selectedNode.X = platform.X;
							selectedNode.Y = platform.Y;
							break;
						}
					}
				}
				selected = true;
			} else {
				const char* code = resolution.status == SceneStopResolutionStatus::AmbiguousPlatform
					? "scene.native.ref.platform.ambiguous"
					: resolution.status == SceneStopResolutionStatus::OutOfOrder
					? "scene.native.ref.stop.order"
					: resolution.status == SceneStopResolutionStatus::UnresolvedRoute
					? "scene.native.ref.stop.route"
					: "scene.native.ref.platform";
				const char* message = resolution.status == SceneStopResolutionStatus::AmbiguousPlatform
					? "A stop without a platform resolves to multiple ordered route platforms"
					: resolution.status == SceneStopResolutionStatus::OutOfOrder
					? "Stop is not reachable after the preceding ordered route visit"
					: resolution.status == SceneStopResolutionStatus::UnresolvedRoute
					? "Stop cannot be resolved because the service route has no ordered traversal"
					: "Explicit stop platform is not present on the ordered service route";
				addNativeDiagnostic(diagnostics, code, message, "services.json", "service", service.id,
					"services[" + service.id + "].stops[" + std::to_string(stopIndex) + "]",
					stop.platformId.empty() ? stop.stationId : stop.platformId);
			}
			if (!selected)
				continue;
			if ((stop.hasPlannedArrival && !nativeFinite(stop.plannedArrivalSeconds))
				|| (stop.hasPlannedDeparture && !nativeFinite(stop.plannedDepartureSeconds))
				|| !nativeFinite(stop.dwellSeconds) || stop.dwellSeconds < 0.0)
				addNativeDiagnostic(diagnostics, "scene.native.timetable.stop", "Stop timetable values must be finite and dwell must be non-negative",
					"services.json", "service", service.id, "services[" + service.id + "].stops[" + std::to_string(stopIndex) + "]");
			if (stop.hasPlannedArrival && stop.hasPlannedDeparture
				&& stop.plannedDepartureSeconds < stop.plannedArrivalSeconds)
				addNativeDiagnostic(diagnostics, "scene.native.timetable.order", "Planned departure precedes planned arrival",
					"services.json", "service", service.id, "services[" + service.id + "].stops[" + std::to_string(stopIndex) + "]");
			NativeStopPlan stopPlan;
			stopPlan.sourceStopIndex = stopIndex;
			stopPlan.stationId = stop.stationId;
			stopPlan.stationName = stationName;
			stopPlan.platformId = resolvedPlatformId;
			stopPlan.node = selectedNode;
			stopPlan.dwellSeconds = stop.dwellSeconds;
			stopPlan.hasPlannedArrival = stop.hasPlannedArrival;
			stopPlan.hasPlannedDeparture = stop.hasPlannedDeparture;
			stopPlan.plannedArrival = stop.hasPlannedArrival ? stop.plannedArrivalSeconds : -1.0;
			stopPlan.plannedDeparture = stop.hasPlannedDeparture ? stop.plannedDepartureSeconds : -1.0;
			baseStops.push_back(std::move(stopPlan));
		}

		for (const int occurrence : occurrencesToBuild) {
			NativeTrainPlan plan;
			plan.routeId = service.route;
			plan.trainDescription = service.id + "-" + std::to_string(occurrence);
			plan.type = service.id;
			plan.operatingCode = sceneServiceOccurrenceOperatingCode(service, occurrence);
			plan.serviceId = service.id;
			plan.physical = composition.physical;
			plan.tractionCurve = composition.tractionCurve;
			plan.occurrence = occurrence;
			plan.routeIndex = routeIt->second;
			plan.direction = runtimeRoute.reversed_direction;
			plan.servicePerformancePercent = service.performancePercent;
			plan.hasConfiguredMaximumSpeed = service.hasMaximumSpeed;
			plan.configuredMaximumSpeedKmh = service.maximumSpeedKmh;
			plan.compositionMaximumSpeedMs = composition.physical.max_speed_ms;
			const double commandedMaximumSpeed = service.hasMaximumSpeed
				? std::min(composition.physical.max_speed_ms, service.maximumSpeedKmh / 3.6)
				: composition.physical.max_speed_ms;
			plan.appliedMaximumSpeedMs = service.performancePercent == 100.0
				? commandedMaximumSpeed
				: commandedMaximumSpeed * service.performancePercent / 100.0;
			plan.appliedMaximumSpeedKmh = plan.appliedMaximumSpeedMs * 3.6;
			const double offset = service.hasRepeat ? (occurrence - 1) * headway : 0.0;
			plan.scheduledDeparture = sceneServiceScheduledEntry(service, occurrence);
			plan.stops = baseStops;
			for (NativeStopPlan& stop : plan.stops) {
				if (stop.hasPlannedArrival)
					stop.plannedArrival += offset;
				if (stop.hasPlannedDeparture)
					stop.plannedDeparture += offset;
			}
			occurrenceIndex.emplace(SceneServiceOccurrence{service.id, occurrence}, trains.size());
			trains.push_back(std::move(plan));
		}
	}

	if (trains.size() > Max_N_Reg)
		addNativeDiagnostic(diagnostics, "scene.native.capacity.trains", "Expanded train count exceeds the runtime capacity",
			"services.json", "scene", scene.name, "services", {}, std::to_string(Max_N_Reg));

	std::vector<SimulationIncident> stagedIncidents;
	std::map<SceneServiceOccurrence, double> occurrenceDelay;
	std::set<std::pair<SceneServiceOccurrence, std::string>> appliedDelayStations;
	if (scenario != nullptr) {
		for (const SceneIncident& incident : scenario->incidents) {
			bool valid = true;
			if (incident.type != "signal_failure" && incident.type != "train_breakdown") {
				addNativeDiagnostic(diagnostics, "scene.native.incident.type", "Unknown incident type",
					"scenarios.json", "incident", incident.id, "incidents[" + incident.id + "].type");
				continue;
			}
			const bool hasOccurrence = incident.hasOccurrence || incident.occurrence != 1;
			const bool hasReducedSpeed = incident.hasReducedSpeed || incident.reducedSpeedKmh != 0.0;
			const bool hasEnd = incident.hasEndSeconds || incident.endSeconds != 0.0;
			if (!nativeFinite(incident.startSeconds) || incident.startSeconds < 0.0) {
				addNativeDiagnostic(diagnostics, "scene.native.incident.time",
					"Incident start must be finite and non-negative", "scenarios.json", "incident",
					incident.id, "incidents[" + incident.id + "].start_seconds");
				valid = false;
			}
			if (incident.type == "signal_failure") {
				if (hasOccurrence || hasReducedSpeed || incident.terminateAtDestination) {
					addNativeDiagnostic(diagnostics, "scene.native.incident.fields",
						"Signal failures do not accept breakdown-only fields", "scenarios.json", "incident",
						incident.id, "incidents[" + incident.id + "]");
					valid = false;
				}
				if (!hasEnd || !nativeFinite(incident.endSeconds)
					|| incident.endSeconds <= incident.startSeconds) {
					addNativeDiagnostic(diagnostics, "scene.native.incident.time",
						"Signal failure requires end after start", "scenarios.json", "incident", incident.id,
						"incidents[" + incident.id + "].end_seconds");
					valid = false;
				}
			} else {
				if (hasOccurrence
					&& (incident.occurrence < 1
						|| repeatCounts.find(incident.target) == repeatCounts.end()
						|| incident.occurrence > repeatCounts[incident.target])) {
					addNativeDiagnostic(diagnostics, "scene.native.incident.occurrence",
						"Breakdown occurrence is outside the configured service pattern", "scenarios.json",
						"incident", incident.id, "incidents[" + incident.id + "].occurrence");
					valid = false;
				}
				if (hasReducedSpeed && (!nativeFinite(incident.reducedSpeedKmh) || incident.reducedSpeedKmh <= 0.0)) {
					addNativeDiagnostic(diagnostics, "scene.native.incident.speed",
						"Reduced breakdown speed must be positive and finite", "scenarios.json", "incident",
						incident.id, "incidents[" + incident.id + "].reduced_speed_kmh");
					valid = false;
				}
				if (!hasReducedSpeed && !hasEnd) {
					addNativeDiagnostic(diagnostics, "scene.native.incident.time",
						"A full-hold breakdown requires end_seconds", "scenarios.json", "incident",
						incident.id, "incidents[" + incident.id + "].end_seconds");
					valid = false;
				}
				if (hasEnd && (!nativeFinite(incident.endSeconds) || incident.endSeconds <= incident.startSeconds)) {
					addNativeDiagnostic(diagnostics, "scene.native.incident.time",
						"Incident end must be after start", "scenarios.json", "incident", incident.id,
						"incidents[" + incident.id + "].end_seconds");
					valid = false;
				}
			}
			SimulationIncident runtimeIncident;
			runtimeIncident.id = incident.id;
			runtimeIncident.type = incident.type;
			runtimeIncident.target = incident.target;
			runtimeIncident.hasOccurrence = hasOccurrence;
			runtimeIncident.occurrence = incident.occurrence;
			runtimeIncident.hasReducedSpeed = hasReducedSpeed;
			runtimeIncident.reducedSpeedKmh = incident.reducedSpeedKmh;
			runtimeIncident.terminateAtDestination = incident.terminateAtDestination;
			runtimeIncident.hasEndSeconds = hasEnd;
			runtimeIncident.startSeconds = incident.startSeconds;
			runtimeIncident.endSeconds = incident.endSeconds;
			if (incident.type == "signal_failure") {
				const SceneSectionDescriptor* targetSection = nullptr;
				const auto signal = std::find_if(scene.signals.begin(), scene.signals.end(),
					[&incident](const SceneSignal& candidate) { return candidate.id == incident.target; });
				const SceneSectionDescriptor* directSection = sectionInventory.resolve(incident.target);
				const bool ambiguousTarget = signal != scene.signals.end() && directSection != nullptr;
				if (ambiguousTarget) {
					addNativeDiagnostic(diagnostics, "scene.native.incident.target.ambiguous",
						"Signal failure target matches both a signal and a section",
						"scenarios.json", "incident", incident.id,
						"incidents[" + incident.id + "].target", incident.target);
					valid = false;
				} else if (signal != scene.signals.end()) {
					if (!signal->protectedSection.empty())
						targetSection = sectionInventory.resolve(signal->protectedSection);
				} else {
					targetSection = directSection;
				}
				const int sectionIndex = targetSection == nullptr ? -1
																  : nativeResolveRuntimeSection(targetSection->id);
				if (sectionIndex >= 0)
					runtimeIncident.resolvedSectionIDs.push_back(signalling_block_sections[sectionIndex].ID);
				else if (!ambiguousTarget) {
					addNativeDiagnostic(diagnostics, "scene.native.incident.target", "Signal failure target does not resolve to an exact runtime section",
						"scenarios.json", "incident", incident.id, "incidents[" + incident.id + "].target", incident.target);
					valid = false;
				}
			} else if (serviceById.count(incident.target) == 0) {
				addNativeDiagnostic(diagnostics, "scene.native.incident.target", "Train breakdown target must be a canonical service ID",
					"scenarios.json", "incident", incident.id, "incidents[" + incident.id + "].target", incident.target);
				valid = false;
			}
			if (valid)
				stagedIncidents.push_back(std::move(runtimeIncident));
		}
		for (const SceneEntranceDelay& delay : scenario->entranceDelays) {
			const auto serviceIt = serviceById.find(delay.serviceId);
			const auto countIt = repeatCounts.find(delay.serviceId);
			const SceneServiceOccurrence key{delay.serviceId, delay.occurrence};
			if (serviceIt == serviceById.end() || countIt == repeatCounts.end() || delay.occurrence < 1) {
				addNativeDiagnostic(diagnostics, "scene.native.entrance.ref", "Entrance delay service/occurrence is unknown",
					"scenarios.json", "entrance_delay", delay.serviceId, "entrance_delays", delay.serviceId);
				continue;
			}
			const bool outsidePattern = delay.occurrence > countIt->second;
			if (outsidePattern) {
				addNativeDiagnostic(diagnostics, "scene.native.entrance.occurrence",
					"Entrance delay occurrence is outside the configured service pattern",
					"scenarios.json", "entrance_delay", delay.serviceId, "entrance_delays",
					delay.serviceId + "-" + std::to_string(delay.occurrence));
				continue;
			}
			if (!nativeFinite(delay.delaySeconds) || delay.delaySeconds < 0.0) {
				addNativeDiagnostic(diagnostics, "scene.native.entrance.value", "Entrance delay must be finite and non-negative",
					"scenarios.json", "entrance_delay", delay.serviceId, "entrance_delays", delay.stationId);
				continue;
			}
			const SceneStop* stop = nativeStopForStation(*serviceIt->second, delay.stationId);
			if (stop == nullptr) {
				addNativeDiagnostic(diagnostics, "scene.native.entrance.station", "Entrance delay station is not a stop of the service",
					"scenarios.json", "entrance_delay", delay.serviceId, "entrance_delays", delay.stationId);
				continue;
			}
			if (!stop->hasPlannedDeparture) {
				addNativeDiagnostic(diagnostics, "scene.native.entrance.timetable", "Entrance delay cannot be applied to an absent planned departure",
					"scenarios.json", "entrance_delay", delay.serviceId, "entrance_delays", delay.stationId);
				continue;
			}
			const auto existing = occurrenceDelay.find(key);
			if (existing != occurrenceDelay.end() && existing->second != delay.delaySeconds) {
				addNativeDiagnostic(diagnostics, "scene.native.entrance.conflict", "Conflicting entrance delays target one service occurrence",
					"scenarios.json", "entrance_delay", delay.serviceId, "entrance_delays", delay.stationId);
				continue;
			}
			occurrenceDelay[key] = delay.delaySeconds;
			if (!selectedOccurrences.empty()
				&& !occurrenceSelected(delay.serviceId, delay.occurrence))
				continue;
			if (!appliedDelayStations.insert({key, delay.stationId}).second)
				continue;
			const auto trainIt = occurrenceIndex.find(key);
			if (trainIt == occurrenceIndex.end()) {
				addNativeDiagnostic(diagnostics, "scene.native.entrance.ref", "Entrance delay train occurrence was not built",
					"scenarios.json", "entrance_delay", delay.serviceId, "entrance_delays", delay.serviceId);
				continue;
			}
			NativeTrainPlan& train = trains[trainIt->second];
			train.entranceDelay = delay.delaySeconds;
			for (NativeStopPlan& trainStop : train.stops) {
				if (trainStop.stationId == delay.stationId) {
					trainStop.plannedDeparture += delay.delaySeconds;
					break;
				}
			}
		}
		for (NativeTrainPlan& train : trains) {
			for (const SimulationIncident& incident : stagedIncidents) {
				if (incident.type == "train_breakdown" && incident.terminateAtDestination
					&& incident.target == train.serviceId
					&& (!incident.hasOccurrence || incident.occurrence == train.occurrence)) {
					train.destinationTerminationRequested = true;
					break;
				}
			}
		}
	}

	// Validate passenger references before occurrence selection.  A filtered run
	// must not hide an invalid canonical passenger row from the native contract.
	std::unordered_set<std::string> passengerIds;
	std::unordered_set<std::string> journeyIds;
	std::unordered_set<std::string> passengerLegIds;
	for (std::size_t passengerIndex = 0; passengerIndex < scene.passengers.size(); ++passengerIndex) {
		const ScenePassenger& sourcePassenger = scene.passengers[passengerIndex];
		const std::string passengerPath = "passengers[" + std::to_string(passengerIndex) + "]";
		if (!passengerIds.insert(sourcePassenger.id).second)
			addNativeDiagnostic(diagnostics, "scene.native.passenger.id", "Duplicate passenger id",
				"passengers.json", "passenger", sourcePassenger.id, passengerPath + ".id");
		for (std::size_t journeyIndex = 0; journeyIndex < sourcePassenger.journeys.size(); ++journeyIndex) {
			const ScenePassengerJourney& sourceJourney = sourcePassenger.journeys[journeyIndex];
			const std::string journeyPath = passengerPath + ".journeys[" + std::to_string(journeyIndex) + "]";
			if (!journeyIds.insert(sourceJourney.id).second)
				addNativeDiagnostic(diagnostics, "scene.native.passenger.id", "Duplicate passenger journey id",
					"passengers.json", "journey", sourceJourney.id, journeyPath + ".id");
			if (nativeStationForId(stationById, sourceJourney.originStationId) == nullptr)
				addNativeDiagnostic(diagnostics, "scene.native.passenger.station", "Passenger journey origin station is unknown",
					"passengers.json", "journey", sourceJourney.id, journeyPath + ".origin",
					sourceJourney.originStationId);
			if (nativeStationForId(stationById, sourceJourney.destinationStationId) == nullptr)
				addNativeDiagnostic(diagnostics, "scene.native.passenger.station", "Passenger journey destination station is unknown",
					"passengers.json", "journey", sourceJourney.id, journeyPath + ".destination",
					sourceJourney.destinationStationId);
			if (!nativeFinite(sourceJourney.plannedDepartureStartSeconds)
				|| !nativeFinite(sourceJourney.plannedDepartureEndSeconds)
				|| sourceJourney.plannedDepartureStartSeconds < 0.0
				|| sourceJourney.plannedDepartureEndSeconds < sourceJourney.plannedDepartureStartSeconds)
				addNativeDiagnostic(diagnostics, "scene.native.passenger.window", "Passenger planned departure window is invalid",
					"passengers.json", "journey", sourceJourney.id, journeyPath + ".planned_departure",
					{}, "Use finite non-negative bounds with start no later than end");
			if (!nativeFinite(sourceJourney.plannedArrivalStartSeconds)
				|| !nativeFinite(sourceJourney.plannedArrivalEndSeconds)
				|| sourceJourney.plannedArrivalStartSeconds < 0.0
				|| sourceJourney.plannedArrivalEndSeconds < sourceJourney.plannedArrivalStartSeconds)
				addNativeDiagnostic(diagnostics, "scene.native.passenger.window", "Passenger planned arrival window is invalid",
					"passengers.json", "journey", sourceJourney.id, journeyPath + ".planned_arrival",
					{}, "Use finite non-negative bounds with start no later than end");
			for (std::size_t legIndex = 0; legIndex < sourceJourney.legs.size(); ++legIndex) {
				const ScenePassengerLeg& sourceLeg = sourceJourney.legs[legIndex];
				const std::string legPath = journeyPath + ".legs[" + std::to_string(legIndex) + "]";
				if (!passengerLegIds.insert(sourceLeg.id).second)
					addNativeDiagnostic(diagnostics, "scene.native.passenger.id", "Duplicate passenger leg id",
						"passengers.json", "leg", sourceLeg.id, legPath + ".id");
				if (nativeStationForId(stationById, sourceLeg.originStationId) == nullptr)
					addNativeDiagnostic(diagnostics, "scene.native.passenger.station", "Passenger leg origin station is unknown",
						"passengers.json", "leg", sourceLeg.id, legPath + ".origin",
						sourceLeg.originStationId);
				if (nativeStationForId(stationById, sourceLeg.destinationStationId) == nullptr)
					addNativeDiagnostic(diagnostics, "scene.native.passenger.station", "Passenger leg destination station is unknown",
						"passengers.json", "leg", sourceLeg.id, legPath + ".destination",
						sourceLeg.destinationStationId);
				const auto serviceIt = serviceById.find(sourceLeg.serviceId);
				if (serviceIt == serviceById.end()) {
					addNativeDiagnostic(diagnostics, "scene.native.passenger.service", "Passenger leg refers to an unknown service",
						"passengers.json", "leg", sourceLeg.id, legPath + ".service", sourceLeg.serviceId);
				} else {
					const bool hasOriginStop = std::any_of(serviceIt->second->stops.begin(), serviceIt->second->stops.end(),
						[&sourceLeg](const SceneStop& stop) { return stop.stationId == sourceLeg.originStationId; });
					const bool hasDestinationStop = std::any_of(serviceIt->second->stops.begin(), serviceIt->second->stops.end(),
						[&sourceLeg](const SceneStop& stop) { return stop.stationId == sourceLeg.destinationStationId; });
					if (!hasOriginStop)
						addNativeDiagnostic(diagnostics, "scene.native.passenger.stop",
							"Passenger leg origin is not a stop of the referenced service",
							"passengers.json", "leg", sourceLeg.id, legPath + ".origin", sourceLeg.serviceId,
							"Choose an origin station from the service stop pattern");
					if (!hasDestinationStop)
						addNativeDiagnostic(diagnostics, "scene.native.passenger.stop",
							"Passenger leg destination is not a stop of the referenced service",
							"passengers.json", "leg", sourceLeg.id, legPath + ".destination", sourceLeg.serviceId,
							"Choose a destination station from the service stop pattern");
					if (hasOriginStop && hasDestinationStop) {
						SceneServiceStopPair stopPair;
						if (!resolveScenePassengerLegStops(*serviceIt->second, sourceLeg, stopPair))
							addNativeDiagnostic(diagnostics, "scene.native.passenger.order",
								"Passenger leg destination must follow its origin in the service stop pattern",
								"passengers.json", "leg", sourceLeg.id, legPath + ".destination", sourceLeg.serviceId,
								"Choose an ordered origin/destination pair from the service stop pattern",
								hasLegacyImport ? SceneSeverity::Warning : SceneSeverity::Error);
					}
				}
				if (sourceLeg.occurrence <= 0)
					addNativeDiagnostic(diagnostics, "scene.native.passenger.occurrence",
						"Passenger leg occurrence must be positive", "passengers.json", "leg", sourceLeg.id,
						legPath + ".occurrence", {}, "Use a positive occurrence number");
				if (legIndex == 0 && sourceLeg.originStationId != sourceJourney.originStationId)
					addNativeDiagnostic(diagnostics, "scene.native.passenger.continuity",
						"First passenger leg does not start at journey origin", "passengers.json", "journey",
						sourceJourney.id, legPath + ".origin", sourceLeg.originStationId);
				if (legIndex > 0
					&& sourceLeg.originStationId != sourceJourney.legs[legIndex - 1].destinationStationId)
					addNativeDiagnostic(diagnostics, "scene.native.passenger.continuity",
						"Passenger legs are not continuous", "passengers.json", "journey", sourceJourney.id,
						legPath + ".origin", sourceLeg.originStationId);
				if (legIndex + 1 == sourceJourney.legs.size()
					&& sourceLeg.destinationStationId != sourceJourney.destinationStationId)
					addNativeDiagnostic(diagnostics, "scene.native.passenger.continuity",
						"Last passenger leg does not end at journey destination", "passengers.json", "journey",
						sourceJourney.id, legPath + ".destination", sourceLeg.destinationStationId);
			}
		}
	}

	if (hasErrors(diagnostics))
		return diagnostics;

	InitialParameters stagedParameters = initial_variables;
	stagedParameters.name = scene.name;
	stagedParameters.startingSimulationTime = baseTime;
	stagedParameters.times = durationSeconds;
	stagedParameters.bufferTime = scene.settings.hasBufferTime
		? static_cast<int>(std::llround(scene.settings.bufferTimeSeconds))
		: 0;
	stagedParameters.recoveryTimePercentage = scene.settings.hasRecoveryTime
		? static_cast<int>(std::llround(scene.settings.recoveryTimePercent))
		: 0;
	stagedParameters.numTrackLines = static_cast<int>(scene.tracks.size());
	stagedParameters.N_Routes = static_cast<int>(scene.routes.size());
	stagedParameters.num_OrderLists = 0;

	const int vectorSize = std::max(1, static_cast<int>(std::ceil(durationSeconds / timestep)));
	std::list<StationPlatform> stagedPlatforms = AllStationPlatforms;
	for (StationPlatform& platform : stagedPlatforms) {
		const auto stationIt = stationById.find(platform.StationID);
		const ScenePlatform* sourcePlatform = nullptr;
		if (stationIt != stationById.end()) {
			sourcePlatform = nativePlatformForStation(*stationIt->second, platform.ID);
			platform.StationID = stationIt->second->name.empty() ? stationIt->second->id : stationIt->second->name;
		}
		platform.length = sourcePlatform != nullptr && sourcePlatform->hasLength
			? sourcePlatform->lengthM
			: 100.0;
		platform.width = sourcePlatform != nullptr && sourcePlatform->hasWidth
			? sourcePlatform->widthM
			: 2.5;
		platform.Max_Passenger_Volume = static_cast<int>((platform.length * platform.width)
			/ (3.14159 * std::pow(0.8, 2)) * 0.8);
		platform.Current_N_Passengers = 0;
		platform.Current_List_Pax_On_Platform.clear();
		platform.List_Trains_Stopping_At_Platform.clear();
	}
	for (const NativeTrainPlan& train : trains) {
		for (const NativeStopPlan& stop : train.stops) {
			for (StationPlatform& platform : stagedPlatforms) {
				if (platform.ID == stop.platformId
					&& platform.StationID == stop.stationName) {
					platform.List_Trains_Stopping_At_Platform.push_back(train.trainDescription);
					break;
				}
			}
		}
	}

	std::list<Passenger> stagedPassengers;
	for (const ScenePassenger& sourcePassenger : scene.passengers) {
		Passenger passenger;
		passenger.ID = sourcePassenger.id;
		for (const ScenePassengerJourney& sourceJourney : sourcePassenger.journeys) {
			Journey journey;
			journey.ID = sourceJourney.id;
			journey.Journey_Activity_Type = sourceJourney.activity;
			const SceneStation* origin = stationById.at(sourceJourney.originStationId);
			const SceneStation* destination = stationById.at(sourceJourney.destinationStationId);
			journey.Dep_Station_ID = origin->name.empty() ? origin->id : origin->name;
			journey.Arr_Station_ID = destination->name.empty() ? destination->id : destination->name;
			journey.Planned_Departure_Time = sourceJourney.plannedDepartureStartSeconds;
			journey.Planned_Arrival_Time = sourceJourney.plannedArrivalStartSeconds;
			bool omitJourney = false;
			for (const ScenePassengerLeg& sourceLeg : sourceJourney.legs) {
				const SceneServiceOccurrence key{sourceLeg.serviceId, sourceLeg.occurrence};
				if (!selectedOccurrences.empty() && !occurrenceSelected(sourceLeg.serviceId, sourceLeg.occurrence)) {
					omitJourney = true;
					break;
				}
				const auto trainIt = occurrenceIndex.find(key);
				if (trainIt == occurrenceIndex.end() && serviceById.count(sourceLeg.serviceId) != 0) {
					addNativeDiagnostic(diagnostics, "scene.native.passenger.occurrence",
						"Passenger leg refers to a service occurrence outside the simulation horizon",
						"passengers.json", "leg", sourceLeg.id,
						"passengers[" + sourcePassenger.id + "].journeys[" + sourceJourney.id + "].legs",
						sourceLeg.serviceId + "-" + std::to_string(sourceLeg.occurrence), {}, SceneSeverity::Warning);
					omitJourney = true;
					break;
				}
				SceneServiceStopPair stopPair;
				const auto serviceIt = serviceById.find(sourceLeg.serviceId);
				if (serviceIt == serviceById.end()
					|| !resolveScenePassengerLegStops(*serviceIt->second, sourceLeg, stopPair)) {
					omitJourney = true;
					break;
				}
				const auto originStop = nativeStopAtSourceIndex(trains[trainIt->second], stopPair.originIndex);
				const auto destinationStop = nativeStopAtSourceIndex(trains[trainIt->second], stopPair.destinationIndex);
				if (originStop == nullptr || destinationStop == nullptr) {
					addNativeDiagnostic(diagnostics, "scene.native.passenger.leg", "Passenger leg does not resolve to one train occurrence and two stops",
						"passengers.json", "leg", sourceLeg.id, "passengers[" + sourcePassenger.id + "].journeys[" + sourceJourney.id + "].legs", sourceLeg.serviceId);
					omitJourney = true;
					break;
				}
				Trip trip;
				trip.TripID = sourceLeg.id;
				trip.JourneyID = journey.ID;
				trip.Trip_Activity_Type = journey.Journey_Activity_Type;
				trip.Dep_Station_ID = originStop->stationName;
				trip.Arr_Station_ID = destinationStop->stationName;
				trip.Dep_Station_Platform_ID = originStop->platformId;
				trip.Arr_Station_Platform_ID = destinationStop->platformId;
				trip.TrainServiceDescription = trains[trainIt->second].trainDescription;
				journey.Trips.push_back(std::move(trip));
			}
			if (omitJourney)
				continue;
			journey.N_Trips = static_cast<int>(journey.Trips.size());
			passenger.Journeys.push_back(std::move(journey));
		}
		stagedPassengers.push_back(std::move(passenger));
	}
	if (hasErrors(diagnostics))
		return diagnostics;

	const auto sampleWindow = [](double minimum, double maximum) {
		return runNumberGenerator().getUniformFloat(minimum, maximum);
	};
	for (const ScenePassenger& sourcePassenger : scene.passengers) {
		const auto stagedPassenger = std::find_if(stagedPassengers.begin(), stagedPassengers.end(),
			[&sourcePassenger](const Passenger& candidate) { return candidate.ID == sourcePassenger.id; });
		if (stagedPassenger == stagedPassengers.end())
			continue;
		for (const ScenePassengerJourney& sourceJourney : sourcePassenger.journeys) {
			auto stagedJourney = std::find_if(stagedPassenger->Journeys.begin(), stagedPassenger->Journeys.end(),
				[&sourceJourney](const Journey& candidate) { return candidate.ID == sourceJourney.id; });
			if (stagedJourney == stagedPassenger->Journeys.end())
				continue;
			stagedJourney->Actual_Planned_Departure_Time = sampleWindow(
				sourceJourney.plannedDepartureStartSeconds, sourceJourney.plannedDepartureEndSeconds);
			stagedJourney->Actual_Planned_Arrival_Time = sampleWindow(
				sourceJourney.plannedArrivalStartSeconds, sourceJourney.plannedArrivalEndSeconds);
			if (!stagedJourney->Trips.empty()) {
				stagedJourney->Trips.front().Planned_Departure_Time =
					static_cast<int>(stagedJourney->Actual_Planned_Departure_Time);
				stagedJourney->Trips.back().Planned_Arrival_Time =
					static_cast<int>(stagedJourney->Actual_Planned_Arrival_Time);
			}
		}
	}

	initial_variables = stagedParameters;
	if (!initial_variables.bufferTimeOverride)
		bufferTime = initial_variables.bufferTime;
	if (!initial_variables.recoveryTimeOverride)
		recoveryTimePercentage = initial_variables.recoveryTimePercentage;
	N_OrderLists = 0;
	// The new storage exists before the old trains are released.
	std::vector<Regional> storage(trains.size());
	regional_train = std::move(storage);
	numRegions = static_cast<int>(trains.size());
	N_Train = 0;
	N_TrainD = 0;
	for (const NativeTrainPlan& train : trains)
		if (train.direction)
			++N_TrainD;
		else
			++N_Train;
	for (std::size_t index = 0; index < trains.size(); ++index)
		nativeCopyTrainPlan(trains[index], regional_train[index], vectorSize);
	changeTrainDepartureTimesForHourlyTimetabling(regional_train.data(), numRegions);
	AllStationPlatforms = std::move(stagedPlatforms);
	numAllStationPlatforms = static_cast<int>(AllStationPlatforms.size());
	AllDailyPassengers = std::move(stagedPassengers);
	numAllDailyPassengers = static_cast<int>(AllDailyPassengers.size());
	AllStationPlatformIndex.build(AllStationPlatforms);
	AllDailyPassengerIndex.build(AllDailyPassengers);
	simulationIncidents = std::move(stagedIncidents);
	return diagnostics;
}
