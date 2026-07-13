#include "diagrams/RunResults.h"

#include "util/TrajectoryUtil.h"

#include <algorithm>
#include <cmath>
#include <limits>
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
		const int stationCount = std::min(train.numStations, 40);
		for (int stationIndex = 0; stationIndex < stationCount; ++stationIndex) {
			TimetableResultRow row;
			row.trainId = train.trainDescription;
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
