#include "diagrams/TrainLoadData.h"

#include "util/CsvWriter.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

// A stop at which the train actually stood during the run, with its arrival time and counts.
struct ActualStopCall {
	double standStartSeconds = 0.0;
	std::string station;
	int loadAfter = 0;
	int boarded = 0;
	int alighted = 0;
	int leftBehind = 0;
};

std::vector<ActualStopCall> actualCallsOf(const CompletedTrain& train) {
	std::vector<ActualStopCall> calls;
	for (const CompletedStop& stop : train.stops) {
		if (!stop.stopped || !std::isfinite(stop.standStartSeconds) || stop.standStartSeconds < 0.0)
			continue;
		ActualStopCall call;
		call.standStartSeconds = stop.standStartSeconds;
		call.station = stop.station;
		call.loadAfter = stop.loadAfter;
		call.boarded = stop.boarded;
		call.alighted = stop.alighted;
		call.leftBehind = stop.leftBehind;
		calls.push_back(std::move(call));
	}
	std::stable_sort(calls.begin(), calls.end(), [](const ActualStopCall& a, const ActualStopCall& b) {
		return a.standStartSeconds < b.standStartSeconds;
	});
	return calls;
}

// Index of the latest call whose stand began at or before timeSeconds, or -1 before the first call.
int activeCallIndexAt(const std::vector<ActualStopCall>& calls, double timeSeconds) {
	int active = -1;
	for (std::size_t i = 0; i < calls.size(); ++i) {
		if (calls[i].standStartSeconds <= timeSeconds)
			active = static_cast<int>(i);
		else
			break;
	}
	return active;
}

TrainLoadSegment makeSegment(const CompletedTrain& train, int trainIndex, const std::string& routeId,
	const std::vector<ActualStopCall>& calls, int callIndex) {
	TrainLoadSegment segment;
	segment.trainId = train.trainId;
	segment.trainIndex = trainIndex;
	segment.routeId = routeId;
	segment.capacity = train.passengerCapacity;
	if (callIndex >= 0 && callIndex < static_cast<int>(calls.size())) {
		const ActualStopCall& call = calls[static_cast<std::size_t>(callIndex)];
		segment.hasCall = true;
		segment.fromStation = call.station;
		segment.load = call.loadAfter;
		segment.boarded = call.boarded;
		segment.alighted = call.alighted;
		segment.leftBehind = call.leftBehind;
		if (callIndex + 1 < static_cast<int>(calls.size()))
			segment.toStation = calls[static_cast<std::size_t>(callIndex + 1)].station;
	} else if (!calls.empty()) {
		segment.toStation = calls.front().station;
	}
	return segment;
}

double clampedThreshold(double threshold) {
	if (!std::isfinite(threshold))
		return kDefaultPassengerOnboardCrowdingThreshold;
	return std::clamp(threshold, 0.0, 1.0);
}

} // namespace

std::optional<TrainLoadDiagramData> trainLoadDiagramData(const CompletedRunData& data, int referenceIndex) {
	if (!data.hasRoute(referenceIndex))
		return std::nullopt;
	TrainLoadDiagramData result;
	result.referencePath = data.routes[static_cast<std::size_t>(referenceIndex)].path;
	if (result.referencePath.nodes.empty())
		return std::nullopt;
	result.crowdingThreshold = clampedThreshold(data.settings.passengerOnboardCrowdingThreshold);
	result.hasPassengers = data.settings.hasPassengers;

	const RouteProjections maps = projectRoutes(data, result.referencePath);
	for (std::size_t trainIndex = 0; trainIndex < data.trains.size(); ++trainIndex) {
		const CompletedTrain& train = data.trains[trainIndex];
		const auto projection = maps.projections.find(train.routeIndex);
		if (projection == maps.projections.end())
			continue;
		const std::string& routeId = data.routes[static_cast<std::size_t>(train.routeIndex)].id;
		const std::vector<ActualStopCall> calls = actualCallsOf(train);
		const CompletedTrajectory& trajectory = train.trajectory;

		for (const TrajectorySegment& valid : trajectory.segments) {
			bool open = false;
			int openCall = -2;
			for (int sample = valid.first; sample <= valid.last; ++sample) {
				const double sourceKm = routeDiagramTrajectoryKm(trajectory.at(trajectory.position, sample).value_or(0.0));
				const auto x = projection->second.map(sourceKm);
				if (!x) {
					open = false;
					continue;
				}
				const double seconds = sample * data.settings.timestepSeconds;
				const int callIndex = activeCallIndexAt(calls, seconds);
				const RouteDiagramPoint point{*x, seconds, std::string(), 0, 0};
				if (!open) {
					TrainLoadSegment segment = makeSegment(train, static_cast<int>(trainIndex), routeId, calls, callIndex);
					segment.points.push_back(point);
					result.segments.push_back(std::move(segment));
					open = true;
					openCall = callIndex;
				} else if (callIndex != openCall) {
					// Share the boundary point so adjacent coloured segments connect without a gap.
					result.segments.back().points.push_back(point);
					if (sample < valid.last) {
						TrainLoadSegment segment = makeSegment(train, static_cast<int>(trainIndex), routeId, calls, callIndex);
						segment.points.push_back(point);
						result.segments.push_back(std::move(segment));
						openCall = callIndex;
					} else {
						open = false;
					}
				} else {
					result.segments.back().points.push_back(point);
				}
			}
		}
	}
	return result;
}

std::string buildTrainLoadCsv(const TrainLoadDiagramData& diagram, const std::vector<std::string>& visibleTrainIds) {
	if (visibleTrainIds.empty())
		return {};
	const auto isVisible = [&visibleTrainIds](const std::string& trainId) {
		return std::find(visibleTrainIds.begin(), visibleTrainIds.end(), trainId) != visibleTrainIds.end();
	};
	std::vector<std::vector<std::string>> rows;
	for (const TrainLoadSegment& segment : diagram.segments) {
		if (segment.points.empty() || !isVisible(segment.trainId))
			continue;
		const double rate = segment.occupancyRate();
		rows.push_back({
			segment.trainId,
			diagram.referencePath.id,
			segment.routeId,
			segment.fromStation,
			segment.toStation,
			csv::formatDouble(segment.points.front().seconds),
			csv::formatDouble(segment.points.back().seconds),
			std::to_string(segment.load),
			std::to_string(segment.capacity),
			rate >= 0.0 ? csv::formatDouble(rate) : std::string(),
			segment.capacity > 0 ? (segment.isCrowded(diagram.crowdingThreshold) ? "yes" : "no") : std::string(),
			segment.hasCall ? std::to_string(segment.boarded) : std::string(),
			segment.hasCall ? std::to_string(segment.alighted) : std::string(),
			segment.hasCall ? std::to_string(segment.leftBehind) : std::string(),
		});
	}
	if (rows.empty())
		return {};
	return csv::makeDocument({"Train", "Reference route", "Source route", "From station", "To station",
								 "Start time[s]", "End time[s]", "Load", "Capacity", "Occupancy rate", "Crowded",
								 "Boarded", "Alighted", "Left behind"},
		rows);
}
