#include "diagrams/RunResults.h"

#include "util/TrajectoryUtil.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace {

RunResultValue availableValue(double value) {
	return std::isfinite(value) ? RunResultValue{true, value} : RunResultValue{};
}

RunResultValue availableTimetableValue(double value) {
	return std::isfinite(value) && value >= 0.0 ? RunResultValue{true, value} : RunResultValue{};
}

const TrainEvent* timetableEventForOccurrence(const Train& train, const std::string& stationId,
																int occurrence) {
	int seen = 0;
	for (const TrainEvent& event : train.TimetablePoints) {
		if (event.SuccessorID != stationId)
			continue;
		if (++seen == occurrence)
			return &event;
	}
	return nullptr;
}

RunResultValue delayValue(const RunResultValue& planned, const RunResultValue& simulated) {
	if (!planned.available || !simulated.available)
		return {};
	return availableValue(simulated.value - planned.value);
}

using OccurrenceKey = std::pair<std::string, int>;

OccurrenceKey occurrenceKey(const std::string& serviceId, int occurrence,
		const std::string& trainId = {}) {
	return {serviceId.empty() ? trainId : serviceId, occurrence};
}

std::set<OccurrenceKey> selectedOccurrences(const RunResults& results) {
	std::set<OccurrenceKey> keys;
	for (const TrainRunResult& train : results.trains)
		keys.insert(occurrenceKey(train.serviceId, train.occurrence, train.trainId));
	return keys;
}

struct FinalArrival {
	std::string stationId;
	int journeyIndex = 0;
	int callIndex = 0;
	RunResultValue arrival;
};

std::map<OccurrenceKey, FinalArrival> finalArrivals(
		const std::vector<TimetableResultRow>& rows) {
	std::map<OccurrenceKey, FinalArrival> result;
	for (const TimetableResultRow& row : rows) {
		result[occurrenceKey(row.serviceId, row.occurrence, row.trainId)] =
				{row.stationId, row.journeyIndex, row.callIndex, row.simulatedArrivalSeconds};
	}
	return result;
}

const TrainRunResult* runResultFor(const RunResults& results, const OccurrenceKey& key) {
	for (const TrainRunResult& train : results.trains)
		if (occurrenceKey(train.serviceId, train.occurrence, train.trainId) == key)
			return &train;
	return nullptr;
}

bool coveredAndFinite(const std::vector<double>& values, int first, int last) {
	if (first < 0 || last < first || last >= static_cast<int>(values.size()))
		return false;
	for (int index = first; index <= last; ++index) {
		if (!std::isfinite(values[static_cast<std::size_t>(index)]))
			return false;
	}
	return true;
}

void addTotal(const RunResultValue& value, double& sum, bool& complete) {
	if (!value.available) {
		complete = false;
		return;
	}
	sum += value.value;
}

} // namespace

std::vector<TimetableResultRow> buildTimetableResults(const std::vector<const Train*>& trains) {
	std::vector<TimetableResultRow> results;
	if (trains.empty())
		return results;

	for (const Train* trainPtr : trains) {
		if (!trainPtr)
			continue;
		const Train& train = *trainPtr;
		if (!train.Stations || train.numStations <= 0)
			continue;
		const int stationCount = std::min(train.numStations, static_cast<int>(Train::kMaxTimetableStations));
		if (train.numStations > Train::kMaxTimetableStations) {
			std::cerr << "Timetable results for train " << train.trainDescription << " truncated to "
					  << Train::kMaxTimetableStations << " stations ("
					  << train.numStations - Train::kMaxTimetableStations << " later stops dropped)\n";
		}
		for (int stationIndex = 0; stationIndex < stationCount; ++stationIndex) {
			TimetableResultRow row;
			row.trainId = train.trainDescription;
			row.operatingCode = train.operatingCode;
			row.serviceId = train.serviceId;
			row.occurrence = train.serviceOccurrence;
			row.stationId = train.stationNameForArrivalStats(stationIndex);
			row.journeyIndex = stationIndex + 1;
			for (int previous = 0; previous <= stationIndex; ++previous) {
				if (train.stationNameForArrivalStats(previous) == row.stationId)
					++row.callIndex;
			}
			row.plannedArrivalSeconds = availableTimetableValue(train.ScheduledArrivals[stationIndex]);
			row.plannedDepartureSeconds = availableTimetableValue(train.ScheduledDepartures[stationIndex]);

			if (const TrainEvent* event = timetableEventForOccurrence(train, row.stationId, row.callIndex)) {
				row.simulatedArrivalSeconds = availableTimetableValue(event->Time);
				row.simulatedDepartureSeconds = availableTimetableValue(event->Time2);
			}
			row.arrivalDelaySeconds = delayValue(row.plannedArrivalSeconds, row.simulatedArrivalSeconds);
			row.departureDelaySeconds = delayValue(row.plannedDepartureSeconds, row.simulatedDepartureSeconds);
			results.push_back(std::move(row));
		}
	}
	return results;
}

RunResults buildRunResults(const std::vector<const Train*>& trains, double timestep) {
	RunResults results;
	if (trains.empty() || !std::isfinite(timestep))
		return results;

	results.trains.reserve(trains.size());
	for (const Train* trainPtr : trains) {
		if (!trainPtr)
			continue;
		const Train& train = *trainPtr;
		TrainRunResult row;
		row.trainId = train.trainDescription;
		row.operatingCode = train.operatingCode;
		row.serviceId = train.serviceId;
		row.occurrence = train.serviceOccurrence;
		row.performancePercent = train.servicePerformancePercent;
		row.hasConfiguredMaximumSpeed = train.hasConfiguredMaximumSpeed;
		row.configuredMaximumSpeedKmh = train.configuredMaximumSpeedKmh;
		row.compositionMaximumSpeedMs = train.compositionMaximumSpeedMs;
		row.appliedMaximumSpeedMs = train.appliedMaximumSpeedMs;
		row.appliedMaximumSpeedKmh = train.appliedMaximumSpeedKmh;
		row.directIncidentIds = train.directIncidentIds;
		if (!row.directIncidentIds.empty() && std::isfinite(train.firstDirectIncidentTime)
				&& train.firstDirectIncidentTime >= 0.0) {
			row.firstDirectIncidentTime = availableValue(train.firstDirectIncidentTime);
			row.firstDirectIncidentLocation = availableValue(train.firstDirectIncidentLocation);
		}
		row.destinationTerminationRequested = train.destinationTerminationRequested;
		row.destinationTerminated = train.destinationTerminated;

		const bool boundsInPositionSeries = train.earliestActiveTrajectoryIndex >= 0 &&
			train.End_Time >= train.earliestActiveTrajectoryIndex &&
			train.End_Time < static_cast<int>(train.instant_spatial_position.size());
		const auto segments = boundsInPositionSeries
			? validTrajectorySegments(train.instant_spatial_position,
									train.earliestActiveTrajectoryIndex, train.End_Time)
			: std::vector<TrajectorySegment>();

		if (!segments.empty() && std::isfinite(timestep)) {
			const int first = segments.front().first;
			const int last = segments.back().last;
			row.startSeconds = availableValue(trajectoryTimeSeconds(first, timestep));
			row.endSeconds = availableValue(trajectoryTimeSeconds(last, timestep));
			if (row.startSeconds.available && row.endSeconds.available)
				row.travelSeconds = availableValue(row.endSeconds.value - row.startSeconds.value);

			const bool energySeriesAvailable =
				coveredAndFinite(train.instant_train_power_consumption, first, last) &&
				coveredAndFinite(train.instant_train_energy_consumption, first, last);
			if (energySeriesAvailable) {
				row.energyConsumedKWh = availableValue(energyMJKWh(train.TotalEnergyConsumed));
				row.energyWithRegenKWh = availableValue(energyMJKWh(train.TotalEnergyConsWithRegBrak));
				row.substationKWh = availableValue(energyMJKWh(train.TotalEnergySubstationRequest));
				row.substationWithRegenKWh = availableValue(energyMJKWh(train.TotalEnergySubstRequestWithRegBrak));
			}
		}

		results.trains.push_back(std::move(row));
	}

	double energyConsumed = 0.0;
	double energyWithRegen = 0.0;
	double substation = 0.0;
	double substationWithRegen = 0.0;
	double networkStart = std::numeric_limits<double>::infinity();
	double networkEnd = -std::numeric_limits<double>::infinity();
	bool networkTimesComplete = !results.trains.empty();
	bool energyConsumedComplete = true;
	bool energyWithRegenComplete = true;
	bool substationComplete = true;
	bool substationWithRegenComplete = true;
	for (const TrainRunResult& row : results.trains) {
		if (!row.startSeconds.available || !row.endSeconds.available || !row.travelSeconds.available) {
			networkTimesComplete = false;
		} else {
			networkStart = std::min(networkStart, row.startSeconds.value);
			networkEnd = std::max(networkEnd, row.endSeconds.value);
		}
		addTotal(row.energyConsumedKWh, energyConsumed, energyConsumedComplete);
		addTotal(row.energyWithRegenKWh, energyWithRegen, energyWithRegenComplete);
		addTotal(row.substationKWh, substation, substationComplete);
		addTotal(row.substationWithRegenKWh, substationWithRegen,
				substationWithRegenComplete);
	}
	if (networkTimesComplete) {
		results.networkStartSeconds = availableValue(networkStart);
		results.networkEndSeconds = availableValue(networkEnd);
		results.networkTravelSeconds = availableValue(networkEnd - networkStart);
	}
	if (energyConsumedComplete)
		results.energyConsumedKWh = availableValue(energyConsumed);
	if (energyWithRegenComplete)
		results.energyWithRegenKWh = availableValue(energyWithRegen);
	if (substationComplete)
		results.substationKWh = availableValue(substation);
	if (substationWithRegenComplete)
		results.substationWithRegenKWh = availableValue(substationWithRegen);
	return results;
}

DelayComparisonResult compareDelayRuns(const DelayRunSnapshot& baseline,
		const DelayRunSnapshot& scenario) {
	DelayComparisonResult result;
	const auto reject = [&result](const std::string& message) {
		result.valid = false;
		result.diagnostic = message;
		return result;
	};
	if (baseline.scenarioId.empty() || scenario.scenarioId.empty()
			|| baseline.scenarioId == scenario.scenarioId)
		return reject("Baseline and scenario IDs must be present and different");
	if (baseline.caseRevision != scenario.caseRevision)
		return reject("Baseline and scenario runs use different scene revisions");
	if (baseline.baseTimeSeconds != scenario.baseTimeSeconds
			|| baseline.durationSeconds != scenario.durationSeconds
			|| baseline.timestep != scenario.timestep)
		return reject("Baseline and scenario time settings do not match");
	if (baseline.hasIncidents || baseline.hasEntranceDelays)
		return reject("Delay baseline must be incident-free and have no entrance delays");
	if (!scenario.hasIncidents || scenario.hasEntranceDelays)
		return reject("Delay comparison scenario must have incidents and no entrance delays");
	if (selectedOccurrences(baseline.run) != selectedOccurrences(scenario.run))
		return reject("Baseline and scenario selected occurrence identities do not match");

	bool hasDirectEvidence = false;
	for (const TrainRunResult& train : scenario.run.trains)
		hasDirectEvidence = hasDirectEvidence || !train.directIncidentIds.empty();
	if (!hasDirectEvidence)
		return reject("Incident run has no direct incident evidence for attribution");

	const auto baselineArrivals = finalArrivals(baseline.timetable);
	const auto scenarioArrivals = finalArrivals(scenario.timetable);
	double total = 0.0;
	for (const OccurrenceKey& key : selectedOccurrences(scenario.run)) {
		const auto baselineIt = baselineArrivals.find(key);
		const auto scenarioIt = scenarioArrivals.find(key);
		if (baselineIt == baselineArrivals.end() || scenarioIt == scenarioArrivals.end())
			return reject("A selected occurrence has no final timetable endpoint");
		const FinalArrival& baselineFinal = baselineIt->second;
		const FinalArrival& scenarioFinal = scenarioIt->second;
		if (baselineFinal.stationId != scenarioFinal.stationId
				|| baselineFinal.journeyIndex != scenarioFinal.journeyIndex
				|| baselineFinal.callIndex != scenarioFinal.callIndex)
			return reject("Baseline and scenario final timetable endpoints do not match");
		if (!baselineFinal.arrival.available || !scenarioFinal.arrival.available)
			return reject("Baseline and scenario require a simulated arrival at every final timetable endpoint");
		const double contribution = scenarioFinal.arrival.value - baselineFinal.arrival.value;
		if (!std::isfinite(contribution) || contribution <= 0.0)
			continue;
		const TrainRunResult* train = runResultFor(scenario.run, key);
		if (!train)
			continue;
		DelayComparisonRow row;
		row.serviceId = key.first;
		row.occurrence = key.second;
		row.operatingCode = train->operatingCode;
		row.baselineFinalArrival = baselineFinal.arrival;
		row.scenarioFinalArrival = scenarioFinal.arrival;
		row.positiveContribution = availableValue(contribution);
		row.attribution = train->directIncidentIds.empty() ? "secondary" : "primary";
		row.incidentIds = train->directIncidentIds;
		row.firstDirectTime = train->firstDirectIncidentTime;
		row.firstDirectLocation = train->firstDirectIncidentLocation;
		row.destinationTerminationRequested = train->destinationTerminationRequested;
		row.destinationTerminated = train->destinationTerminated;
		result.rows.push_back(std::move(row));
		total += contribution;
	}
	result.valid = true;
	result.totalArrivalDelay = availableValue(total);
	return result;
}
