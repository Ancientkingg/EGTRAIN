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

RunResults buildRunResults(const Train* trains, int trainCount, double timestep) {
	RunResults results;
	if (!trains || trainCount <= 0 || !std::isfinite(timestep))
		return results;

	results.trains.reserve(static_cast<std::size_t>(trainCount));
	for (int trainIndex = 0; trainIndex < trainCount; ++trainIndex) {
		const Train& train = trains[trainIndex];
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
