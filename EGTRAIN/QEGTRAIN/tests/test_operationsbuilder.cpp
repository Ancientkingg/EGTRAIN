#include "scene/SceneModel.h"
#include "simulation/Passengers.h"
#include "simulation/Optimisation.h"
#include "simulation/RollingStock.h"
#include "simulation/Signalling.h"
#include "simulation/Simulation.h"
#ifdef signals
#undef signals
#endif

#include <algorithm>
#include <cmath>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

Logger owl;

static bool expect(bool condition, const std::string& message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static SceneTrainUnit unit(const std::string& id, double tractionMass, double wagonMass,
		double wagons, double maxSpeed, double deceleration, double area, double resistance,
		double jerk, double length, const std::string& dataFile, const std::string& tractionFile,
		double firstBandForce, double secondBandForce) {
	SceneTrainUnit result;
	result.id = id;
	result.hasPhysical = true;
	result.physical = {tractionMass, wagonMass, wagons, maxSpeed, deceleration, area,
		resistance, jerk, length};
	result.tractionCurve = {{{0.0, 10.0, firstBandForce, 0.0, 0.0},
		{10.0, 20.0, secondBandForce, 0.0, 0.0}}};
	result.sourceDataFile = dataFile;
	result.sourceTractionFile = tractionFile;
	return result;
}

static SceneModel completeScene() {
	SceneModel scene;
	scene.name = "native-operations-fixture";
	scene.baseTime = "06:30:00";
	scene.settings.hasDuration = true;
	scene.settings.durationSeconds = 90.0;
	scene.settings.hasBufferTime = true;
	scene.settings.bufferTimeSeconds = 7.0;
	scene.settings.hasRecoveryTime = true;
	scene.settings.recoveryTimePercent = 12.0;
	scene.sourceFiles.insert("/does/not/exist/trains.json");
	scene.sourceFiles.insert("weird:provenance/services.json");

	scene.tracks = {{"track.native"}};
	scene.nodes = {{"node.0", "track.native", 0.0, 0.0},
		{"node.1", "track.native", 1.0, 0.0},
		{"node.2", "track.native", 2.0, 0.0},
		{"node.3", "track.native", 3.0, 0.0}};
	scene.arcs = {{"arc.0", "track.native", "node.0", "node.1", 0.0, 0.0, 20.0},
		{"arc.1", "track.native", "node.1", "node.2", 0.0, 0.0, 20.0},
		{"arc.2", "track.native", "node.2", "node.3", 0.0, 0.0, 20.0}};
	scene.blocks = {{"signal.0", "track.native", 1.0}, {"block.1", "track.native", 1.0},
		{"block.2", "track.native", 1.0}};
	scene.signals = {{"signal.0", "block.1"}};

	scene.stations = {
		{"station.0", "Zero", false, 0.0, {{"platform.0", {"node.0"}}}},
		{"station.1", "One", false, 0.0, {{"platform.1", {"node.1"}}}},
		{"station.2", "Two", false, 0.0, {{"platform.2", {"node.2"}}}}};
	scene.routes = {{"route.native", {"signal.0", "block.1", "block.2"}, false, {}, false}};
	scene.trainUnits = {
		unit("unit.1", 100.0, 40.0, 2.0, 30.0, 1.0, 2.0, 0.1, 1.0, 20.0,
				"/missing/unit-data", "weird:unit-traction", 1.0, 2.0),
		unit("unit.2", 80.0, 30.0, 1.0, 25.0, 1.2, 1.5, 0.2, 1.2, 10.0,
				"nonexistent:physical", "/not/a/file", 3.0, 4.0)};
	scene.compositions = {{"composition.native", {"unit.1", "unit.2"}}};

	SceneService service;
	service.id = "service.native";
	service.operatingCode = "9707";
	service.composition = "composition.native";
	service.route = "route.native";
	service.hasEntryTime = true;
	service.entryTimeSeconds = 100.0;
	service.hasRepeat = true;
	service.headwaySeconds = 30.0;
	service.stops = {
		{"station.0", "platform.0", false, true, -1.0, 110.0, 2.0},
		{"station.1", "platform.1", true, true, 120.0, 125.0, 3.0},
		{"station.2", "platform.2", true, false, 130.0, -1.0, 2.0}};
	scene.services.push_back(service);

	SceneScenario baseline;
	baseline.id = "scenario.base";
	scene.scenarios.push_back(baseline);
	SceneScenario selected;
	selected.id = "scenario.selected";
	selected.incidents = {{"incident.signal", "signal_failure", "signal.0", 20.0, 40.0},
		{"incident.breakdown", "train_breakdown", "service.native", 50.0, 70.0}};
	selected.entranceDelays = {{"service.native", 2, "station.0", 5.0},
		{"service.native", 2, "station.1", 5.0}};
	scene.scenarios.push_back(selected);
	scene.defaultScenarioId = "scenario.base";

	ScenePassenger passenger;
	passenger.id = "passenger.1";
	ScenePassengerJourney journey;
	journey.id = "journey.1";
	journey.activity = "work";
	journey.originStationId = "station.0";
	journey.destinationStationId = "station.2";
	journey.plannedDepartureStartSeconds = 100.0;
	journey.plannedDepartureEndSeconds = 110.0;
	journey.plannedArrivalStartSeconds = 130.0;
	journey.plannedArrivalEndSeconds = 140.0;
	journey.legs.push_back({"leg.1", "station.0", "station.1", "service.native", 2});
	journey.legs.push_back({"leg.2", "station.1", "station.2", "service.native", 2});
	passenger.journeys.push_back(journey);
	ScenePassengerJourney unrouted = journey;
	unrouted.id = "journey.unrouted";
	unrouted.legs.clear();
	passenger.journeys.push_back(unrouted);
	scene.passengers.push_back(passenger);
	return scene;
}

int main() {
	bool ok = true;
	SceneModel scene = completeScene();
	initial_variables.InputMainFolder = "/__egtrain_nonexistent_native_input__";
	InputMainFolder = initial_variables.InputMainFolder;
	auto infrastructureDiagnostics = buildInfrastructureAndSignallingFromScene(scene);
	ok &= expect(!hasErrors(infrastructureDiagnostics), "M2 infrastructure builder accepts the complete fixture");
	if (hasErrors(infrastructureDiagnostics))
		return 1;

	const auto diagnostics = buildOperationsFromScene(scene, "scenario.selected");
	ok &= expect(!hasErrors(diagnostics), "M3 operations builder accepts the complete fixture");
	ok &= expect(initial_variables.InputMainFolder == "/__egtrain_nonexistent_native_input__"
			&& InputMainFolder == initial_variables.InputMainFolder,
			"native builders do not access or rewrite the legacy input folder");
	ok &= expect(numRegions == 3 && N_Train == 3 && N_TrainD == 0, "repeat expansion populates train counts");
	ok &= expect(regional_train[0].trainDescription == "service.native-1"
			&& regional_train[1].trainDescription == "service.native-2"
			&& regional_train[2].trainDescription == "service.native-3", "occurrence IDs are canonical and stable");
	ok &= expect(regional_train[0].type == "service.native", "occurrences share the canonical service type");
	ok &= expect(regional_train[0].operatingCode == "9707-1"
			&& regional_train[1].operatingCode == "9707-2"
			&& regional_train[2].operatingCode == "9707-3",
			"repeated services without a step expose readable operating codes");
	ok &= expect(regional_train[0].number_of_wagons == 3.0
			&& regional_train[0].total_train_mass == 290.0
			&& regional_train[0].velocityIntervals == 2,
			"ordered multi-unit physical and traction data is aggregated (wagons="
			+ std::to_string(regional_train[0].number_of_wagons) + ", mass="
			+ std::to_string(regional_train[0].total_train_mass) + ", bands="
			+ std::to_string(regional_train[0].velocityIntervals) + ")");
	ok &= expect(regional_train[0].scheduled_departure_time == 100.0
			&& regional_train[1].scheduled_departure_time == 130.0
			&& regional_train[2].scheduled_departure_time == 160.0, "canonical entry times remain scheduled times");
	ok &= expect(regional_train[0].ScheduledArrivals[0] == -1.0
			&& regional_train[0].ScheduledArrivals[1] == 120.0
			&& regional_train[0].ScheduledDepartures[2] == -1.0, "optional timetable fields retain runtime -1 sentinels");
	ok &= expect(regional_train[0].stationIsOnRoute(0)
			&& regional_train[0].stationRoutePositionMeters(0) == 0.0,
			"planned references retain a valid route-origin station at kilometre zero");
	Node offRouteStation;
	offRouteStation.station = true;
	offRouteStation.stationName = "Outside";
	Train routeMembershipProbe;
	routeMembershipProbe.Stations = &offRouteStation;
	routeMembershipProbe.numStations = 1;
	routeMembershipProbe.indexOfRoute = regional_train[0].indexOfRoute;
	ok &= expect(!routeMembershipProbe.stationIsOnRoute(0)
			&& routeMembershipProbe.stationRoutePositionMeters(0) < 0.0,
			"route-external static timetable stops remain inert in planned route results");
	ok &= expect(regional_train[0].instant_train_tractive_effort.size()
			== regional_train[0].instant_spatial_position.size(),
			"tractive-effort samples are allocated with the trajectory");
	ok &= expect(regional_train[1].ScheduledDepartures[0] == 145.0
			&& regional_train[1].ScheduledDepartures[1] == 160.0
			&& regional_train[1].EntranceDelay == 5.0, "selected entrance delays apply to the requested occurrence and stops");
	ok &= expect(regional_train[0].departure_time == 1.0 && regional_train[1].departure_time == 1201.0
			&& regional_train[2].departure_time == 2401.0, "hourly departure retiming preserves the legacy algorithm");
	ok &= expect(simulationIncidents.size() == 2
			&& simulationIncidents[0].target == "signal.0"
			&& simulationIncidents[1].target == "service.native", "only the selected scenario reaches runtime incidents");
	ok &= expect(simulationIncidents[1].id == "incident.breakdown"
			&& simulationIncidents[1].hasEndSeconds
			&& !simulationIncidents[1].hasOccurrence
			&& !simulationIncidents[1].hasReducedSpeed,
			"legacy breakdown incidents retain full-hold runtime defaults");
	if (!simulationIncidents.empty())
		ok &= expect(simulationIncidents.front().resolvedSectionIDs
				== std::vector<std::string>{"@block.1@"},
				"signal failure resolves its protected section to the exact runtime ID");
	SceneModel directBlockIncident = completeScene();
	directBlockIncident.signals.front().protectedSection.clear();
	directBlockIncident.scenarios[1].incidents[0].target = "block.1";
	const auto directInfrastructure = buildInfrastructureAndSignallingFromScene(directBlockIncident);
	const auto directOperations = buildOperationsFromScene(directBlockIncident, "scenario.selected");
	ok &= expect(!hasErrors(directInfrastructure) && !hasErrors(directOperations)
				&& !simulationIncidents.empty()
				&& simulationIncidents.front().resolvedSectionIDs == std::vector<std::string>{"@block.1@"},
				"direct base-block signal failures remain compatible without signal binding");
	SceneModel unboundSignal = completeScene();
	unboundSignal.signals.front().protectedSection.clear();
	const auto unboundInfrastructure = buildInfrastructureAndSignallingFromScene(unboundSignal);
	const auto unboundOperations = buildOperationsFromScene(unboundSignal, "scenario.selected");
	ok &= expect(!hasErrors(unboundInfrastructure) && hasErrors(unboundOperations),
				"targeting an unbound signal fails operations staging instead of guessing");
	SceneModel occurrenceSpecific = completeScene();
	SceneIncident& occurrenceBreakdown = occurrenceSpecific.scenarios[1].incidents[1];
	occurrenceBreakdown.hasOccurrence = true;
	occurrenceBreakdown.occurrence = 2;
	occurrenceBreakdown.hasReducedSpeed = true;
	occurrenceBreakdown.reducedSpeedKmh = 40.0;
	occurrenceBreakdown.hasEndSeconds = false;
	occurrenceBreakdown.endSeconds = 0.0;
	occurrenceBreakdown.terminateAtDestination = true;
	const auto occurrenceInfrastructure = buildInfrastructureAndSignallingFromScene(occurrenceSpecific);
	const auto occurrenceOperations = buildOperationsFromScene(occurrenceSpecific, "scenario.selected");
	ok &= expect(!hasErrors(occurrenceInfrastructure) && !hasErrors(occurrenceOperations)
			&& simulationIncidents.size() == 2
			&& simulationIncidents[1].hasOccurrence && simulationIncidents[1].occurrence == 2
			&& simulationIncidents[1].hasReducedSpeed
			&& std::fabs(simulationIncidents[1].reducedSpeedKmh - 40.0) < 1e-9
			&& Incident_Holds_Train("service.native-2", 50) == false
			&& regional_train[1].destinationTerminationRequested
			&& !regional_train[0].destinationTerminationRequested
			&& !regional_train[2].destinationTerminationRequested,
			"reduced breakdown staging targets one occurrence and continues without recovery");
	regional_train[1].directIncidentIds.clear();
	ok &= expect(std::fabs(regional_train[1].effectiveIncidentSpeedLimit(10.0, 50) - 10.0) < 1e-9
			&& regional_train[1].directIncidentIds.empty(),
			"a nonbinding breakdown cap is not recorded as direct evidence");
	ok &= expect(std::fabs(regional_train[1].effectiveIncidentSpeedLimit(25.0, 50) - 40.0 / 3.6) < 1e-9
			&& regional_train[1].directIncidentIds == std::vector<std::string>{"incident.breakdown"}
			&& std::fabs(regional_train[0].effectiveIncidentSpeedLimit(25.0, 50) - 25.0) < 1e-9,
			"a binding cap records direct evidence only on the targeted occurrence");
	SimulationIncident overlappingHold = simulationIncidents[1];
	overlappingHold.id = "incident.hold";
	overlappingHold.hasReducedSpeed = false;
	overlappingHold.reducedSpeedKmh = 0.0;
	overlappingHold.hasEndSeconds = true;
	overlappingHold.startSeconds = 45.0;
	overlappingHold.endSeconds = 55.0;
	simulationIncidents.push_back(overlappingHold);
	ok &= expect(Incident_Holds_Train("service.native-2", 50)
			&& Active_Train_Breakdown("service.native-2", 50)->id == "incident.hold",
			"an overlapping full hold dominates an earlier reduced-speed incident");
	SimulationIncident strictCap = simulationIncidents[1];
	strictCap.id = "incident.strict-cap";
	strictCap.reducedSpeedKmh = 30.0;
	simulationIncidents.push_back(strictCap);
	regional_train[1].directIncidentIds.clear();
	ok &= expect(std::fabs(regional_train[1].effectiveIncidentSpeedLimit(25.0, 60) - 30.0 / 3.6) < 1e-9
			&& regional_train[1].directIncidentIds == std::vector<std::string>{"incident.strict-cap"},
			"the strictest concurrent cap supplies the governing direct evidence");
	ok &= expect(AllStationPlatforms.size() == 3, "native operations reuses the M2 platform list");
	if (!AllStationPlatforms.empty())
		ok &= expect(AllStationPlatforms.front().List_Trains_Stopping_At_Platform.front() == "service.native-1",
				"platform stopping lists use stable occurrence descriptions");
	ok &= expect(AllDailyPassengers.size() == 1, "passenger journeys are built without filesystem input");
	if (!AllDailyPassengers.empty() && !AllDailyPassengers.front().Journeys.empty()) {
		const Journey& journey = AllDailyPassengers.front().Journeys.front();
		ok &= expect(journey.N_Trips == 2 && std::isfinite(journey.Actual_Planned_Departure_Time)
				&& journey.Actual_Planned_Departure_Time >= 100.0
				&& journey.Actual_Planned_Departure_Time <= 110.0, "passenger windows are sampled in memory");
		if (!journey.Trips.empty()) {
			const Trip& first = journey.Trips.front();
			const Trip& last = journey.Trips.back();
			ok &= expect(first.TrainServiceDescription == "service.native-2"
					&& first.Dep_Station_Platform_ID == "platform.0"
					&& last.Arr_Station_Platform_ID == "platform.2", "passenger legs map to occurrence stop platforms");
			ok &= expect(first.Planned_Departure_Time >= 100 && first.Planned_Arrival_Time == -9999
					&& last.Planned_Departure_Time == -9999 && last.Planned_Arrival_Time >= 130,
					"multi-leg planned times retain the existing endpoint-only semantics");
		}
		auto unrouted = AllDailyPassengers.front().Journeys.begin();
		++unrouted;
		ok &= expect(unrouted != AllDailyPassengers.front().Journeys.end() && unrouted->N_Trips == 0,
				"legless canonical journeys remain present");
	}
	ok &= expect(initial_variables.name == "native-operations-fixture"
			&& initial_variables.startingSimulationTime == 23400
			&& initial_variables.times == 90.0
			&& initial_variables.bufferTime == 7
			&& initial_variables.recoveryTimePercentage == 12
			&& bufferTime == 7.0
			&& recoveryTimePercentage == 12.0
			&& initial_variables.num_OrderLists == 0, "canonical simulation settings are committed");

	SceneModel stepped = completeScene();
	stepped.services[0].operatingCode = "1723";
	stepped.services[0].hasRepeatCount = true;
	stepped.services[0].repeatCount = 4;
	stepped.services[0].hasOperatingCodeStep = true;
	stepped.services[0].operatingCodeStep = 2;
	const auto steppedInfrastructure = buildInfrastructureAndSignallingFromScene(stepped);
	const auto steppedOperations = buildOperationsFromScene(stepped, "scenario.selected");
	ok &= expect(!hasErrors(steppedInfrastructure) && !hasErrors(steppedOperations)
				&& numRegions == 4
				&& regional_train[0].operatingCode == "1723"
				&& regional_train[1].operatingCode == "1725"
				&& regional_train[3].operatingCode == "1729",
				"explicit repeat count and stepped operating codes reach each occurrence");

	SceneModel tuned = completeScene();
	tuned.services[0].performancePercent = 50.0;
	tuned.services[0].hasMaximumSpeed = true;
	tuned.services[0].maximumSpeedKmh = 20.0;
	SceneService sharedService = tuned.services[0];
	sharedService.id = "service.other";
	sharedService.operatingCode = "other";
	sharedService.hasRepeat = false;
	sharedService.hasRepeatCount = false;
	sharedService.hasOperatingCodeStep = false;
	sharedService.performancePercent = 100.0;
	sharedService.hasMaximumSpeed = false;
	sharedService.hasEntryTime = true;
	sharedService.entryTimeSeconds = 200.0;
	tuned.services.push_back(sharedService);
	const auto tunedInfrastructure = buildInfrastructureAndSignallingFromScene(tuned);
	const auto tunedOperations = buildOperationsFromScene(tuned, "scenario.selected");
	ok &= expect(!hasErrors(tunedInfrastructure) && !hasErrors(tunedOperations)
				&& numRegions == 4
				&& std::fabs(regional_train[0].compositionMaximumSpeedMs - 25.0) < 1e-9
				&& std::fabs(regional_train[0].max_train_speed - (20.0 / 3.6 * 0.5)) < 1e-9
				&& std::fabs(regional_train[3].max_train_speed - 25.0) < 1e-9,
				"service cap and performance apply in precedence order per train");
	ok &= expect(tuned.trainUnits[0].physical.max_speed_ms == 30.0
				&& tuned.trainUnits[1].physical.max_speed_ms == 25.0
				&& tuned.compositions[0].units == std::vector<std::string>{"unit.1", "unit.2"},
				"services sharing a composition do not mutate source rolling-stock data");
	{
		const double previousPerformance = regional_train[0].servicePerformancePercent;
		regional_train[0].servicePerformancePercent = 100.0;
		const double fullForce = regional_train[0].tractiveEffort(5.0);
		regional_train[0].servicePerformancePercent = 50.0;
		const double reducedForce = regional_train[0].tractiveEffort(5.0);
		regional_train[0].servicePerformancePercent = previousPerformance;
		ok &= expect(fullForce > 0.0 && std::fabs(reducedForce - fullForce * 0.5) < 1e-9,
				"performance scales tractive effort exactly once");
	}

	SceneModel selectedScene = completeScene();
	selectedScene.scenarios[1].entranceDelays.push_back(
			{"service.native", 1, "missing-station", -100.0});
	ScenePassengerJourney excludedJourney = selectedScene.passengers[0].journeys[0];
	excludedJourney.id = "journey.excluded";
	for (ScenePassengerLeg& leg : excludedJourney.legs)
		leg.occurrence = 1;
	selectedScene.passengers[0].journeys.push_back(excludedJourney);
	const SceneRunSelection onlySecond{{"service.native", 2}};
	const auto selectedInfrastructure = buildInfrastructureAndSignallingFromScene(selectedScene);
	const auto selectedOperations = buildOperationsFromScene(selectedScene, "scenario.selected", onlySecond);
	bool selectedJourneyHasTrips = false;
	bool excludedJourneyHasNoTrips = false;
	if (!AllDailyPassengers.empty()) {
		for (const Journey& journey : AllDailyPassengers.front().Journeys) {
			if (journey.ID == "journey.1")
				selectedJourneyHasTrips = journey.N_Trips == 2;
			if (journey.ID == "journey.excluded")
				excludedJourneyHasNoTrips = journey.N_Trips == 0;
		}
	}
	ok &= expect(!hasErrors(selectedInfrastructure) && !hasErrors(selectedOperations)
				&& numRegions == 1 && regional_train[0].trainDescription == "service.native-2"
				&& regional_train[0].EntranceDelay == 5.0
				&& selectedJourneyHasTrips && excludedJourneyHasNoTrips,
				"occurrence selection builds one train and skips excluded delays and passenger legs");

	SceneModel sparsePattern = completeScene();
	sparsePattern.services[0].hasRepeatCount = true;
	sparsePattern.services[0].repeatCount = 1000000000;
	const SceneRunSelection lateOccurrence{{"service.native", 999999999}};
	const auto sparseInfrastructure = buildInfrastructureAndSignallingFromScene(sparsePattern);
	const auto sparseOperations = buildOperationsFromScene(
			sparsePattern, "scenario.selected", lateOccurrence);
	ok &= expect(!hasErrors(sparseInfrastructure) && !hasErrors(sparseOperations)
				&& numRegions == 1
				&& regional_train[0].trainDescription == "service.native-999999999",
				"a sparse selection does not expand every occurrence in a large pattern");

	SceneModel outOfPattern = completeScene();
	outOfPattern.services[0].hasRepeatCount = true;
	outOfPattern.services[0].repeatCount = 1;
	const auto outOfPatternInfrastructure = buildInfrastructureAndSignallingFromScene(outOfPattern);
	const auto outOfPatternOperations = buildOperationsFromScene(outOfPattern, "scenario.selected");
	const SceneRunSelection firstOccurrence{{"service.native", 1}};
	const auto excludedOutOfPatternOperations = buildOperationsFromScene(
			outOfPattern, "scenario.selected", firstOccurrence);
	ok &= expect(!hasErrors(outOfPatternInfrastructure) && !hasErrors(outOfPatternOperations)
				&& !hasErrors(excludedOutOfPatternOperations)
				&& numRegions == 1 && regional_train[0].trainDescription == "service.native-1"
				&& regional_train[0].EntranceDelay == 0.0,
				"out-of-pattern entrance delays are skipped for all and selected runs");

	TrainEvent finiteLate;
	finiteLate.Time = 2.0;
	TrainEvent finiteEarly;
	finiteEarly.Time = 1.0;
	TrainEvent nanFirst;
	nanFirst.Time = std::numeric_limits<double>::quiet_NaN();
	nanFirst.trainDescription = "nan-first";
	TrainEvent nanLast;
	nanLast.Time = std::numeric_limits<double>::quiet_NaN();
	nanLast.trainDescription = "nan-last";
	std::list<TrainEvent> unorderedEvents = {nanFirst, finiteLate, finiteEarly, nanLast};
	orderListOfTrainEvents(unorderedEvents);
	const auto eventIt = unorderedEvents.begin();
	ok &= expect(unorderedEvents.size() == 4 && eventIt->Time == 1.0
				&& std::next(eventIt)->Time == 2.0
				&& std::isnan(std::next(eventIt, 2)->Time)
				&& std::isnan(std::next(eventIt, 3)->Time)
				&& std::next(eventIt, 2)->trainDescription == "nan-first"
				&& std::next(eventIt, 3)->trainDescription == "nan-last",
				"train event sorting terminates and places non-finite times last");

	const std::string previousDescription = regional_train[0].trainDescription;
	const std::size_t previousIncidentCount = simulationIncidents.size();
	const std::string previousName = initial_variables.name;
	const auto invalidDiagnostics = buildOperationsFromScene(scene, "scenario.missing");
	ok &= expect(hasErrors(invalidDiagnostics), "an invalid selected scenario is rejected");
	ok &= expect(regional_train[0].trainDescription == previousDescription
			&& simulationIncidents.size() == previousIncidentCount
			&& initial_variables.name == previousName, "invalid operations build preserves prior runtime state");

	SceneModel reversed = completeScene();
	reversed.routes[0].blocks = {"block.2", "block.1", "signal.0"};
	std::reverse(reversed.services[0].stops.begin(), reversed.services[0].stops.end());
	reversed.passengers.clear();
	const auto reversedInfrastructure = buildInfrastructureAndSignallingFromScene(reversed);
	const auto reversedOperations = buildOperationsFromScene(reversed, "scenario.selected");
	ok &= expect(!hasErrors(reversedInfrastructure) && !hasErrors(reversedOperations),
			"reversed routes resolve stop nodes without legacy node-list storage");

	SceneModel routeExternal = completeScene();
	routeExternal.routes[0].blocks = {"signal.0", "block.1"};
	routeExternal.stations.push_back(
			{"station.3", "Three", true, 3.0, {{"platform.3", {"node.3"}}}});
	routeExternal.services[0].stops.push_back(
			{"station.3", {}, true, true, -120.0, -60.0, 2.0});
	routeExternal.passengers.clear();
	const auto routeExternalInfrastructure = buildInfrastructureAndSignallingFromScene(routeExternal);
	const auto routeExternalOperations = buildOperationsFromScene(routeExternal, "scenario.base");
	ok &= expect(!hasErrors(routeExternalInfrastructure) && !hasErrors(routeExternalOperations),
			"pre-entry and post-exit timetable rows remain valid without an invented platform");
	ok &= expect(regional_train[0].numStations == 4
			&& regional_train[0].Stations[3].stationPlatformId == ""
			&& regional_train[0].ScheduledArrivals[3] == -120.0
			&& regional_train[0].ScheduledDepartures[3] == -60.0,
			"route-external timetable rows preserve negative relative planned times");

	SceneModel shortHorizon = completeScene();
	shortHorizon.settings.durationSeconds = 20.0;
	const auto shortInfrastructure = buildInfrastructureAndSignallingFromScene(shortHorizon);
	const auto shortOperations = buildOperationsFromScene(shortHorizon, "scenario.base");
	ok &= expect(!hasErrors(shortInfrastructure) && !hasErrors(shortOperations)
			&& numRegions == 1 && AllDailyPassengers.front().Journeys.front().N_Trips == 0,
			"passenger legs beyond a shortened run horizon are reported and skipped");

	initial_variables.times = 120.0;
	initial_variables.durationOverride = true;
	SceneModel extendedHorizon = completeScene();
	const auto extendedInfrastructure = buildInfrastructureAndSignallingFromScene(extendedHorizon);
	const auto extendedOperations = buildOperationsFromScene(extendedHorizon, "scenario.selected");
	ok &= expect(!hasErrors(extendedInfrastructure) && !hasErrors(extendedOperations)
				&& initial_variables.times == 120.0 && numRegions == 4
				&& regional_train[0].instant_train_speed.size() == 120,
				"duration override sizes repeated services and runtime vectors to the effective horizon");
	initial_variables.durationOverride = false;

	SceneModel trajectoryPerformance = completeScene();
	trajectoryPerformance.settings.durationSeconds = 600.0;
	trajectoryPerformance.services[0].hasEntryTime = true;
	trajectoryPerformance.services[0].entryTimeSeconds = 0.0;
	trajectoryPerformance.services[0].hasRepeatCount = true;
	trajectoryPerformance.services[0].repeatCount = 1;
	trajectoryPerformance.services[0].through = true;
	trajectoryPerformance.services[0].stops.clear();
	for (SceneTrainUnit& trainUnit : trajectoryPerformance.trainUnits)
		for (auto& band : trainUnit.tractionCurve)
			band[2] = 10000.0;
	trajectoryPerformance.services[0].performancePercent = 100.0;
	const auto trajectoryFullInfrastructure = buildInfrastructureAndSignallingFromScene(trajectoryPerformance);
	const auto trajectoryFullOperations = buildOperationsFromScene(trajectoryPerformance, "scenario.base");
	int fullPerformanceEnd = -1;
	if (!hasErrors(trajectoryFullInfrastructure) && !hasErrors(trajectoryFullOperations) && numRegions == 1) {
		TrainSimulationForComputingHW(signalCode1, signalCode2, signalCode3);
		fullPerformanceEnd = regional_train[0].End_Time;
	}
	trajectoryPerformance.services[0].performancePercent = 50.0;
	const auto trajectoryReducedInfrastructure = buildInfrastructureAndSignallingFromScene(trajectoryPerformance);
	const auto trajectoryReducedOperations = buildOperationsFromScene(trajectoryPerformance, "scenario.base");
	int reducedPerformanceEnd = -1;
	if (!hasErrors(trajectoryReducedInfrastructure) && !hasErrors(trajectoryReducedOperations) && numRegions == 1) {
		TrainSimulationForComputingHW(signalCode1, signalCode2, signalCode3);
		reducedPerformanceEnd = regional_train[0].End_Time;
	}
	ok &= expect(fullPerformanceEnd >= 0 && reducedPerformanceEnd > fullPerformanceEnd,
			"reduced performance delays native route completion");
	return ok ? 0 : 1;
}
