#include "diagrams/RouteDiagramData.h"

#include "util/CsvWriter.h"
#include "util/TrajectoryUtil.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <utility>

namespace {

// Where the stop with the given index lies on the reference route.
std::optional<double> stopPosition(const CompletedTrain& train, int index, const RouteDiagramPath& source,
	const RouteDiagramProjection& projection) {
	if (index < 0 || index >= static_cast<int>(train.stops.size()))
		return {};
	return routeDiagramStopPosition(train.stops[static_cast<std::size_t>(index)], source, projection);
}

} // namespace

RouteProjections projectRoutes(const CompletedRunData& data, const RouteDiagramPath& reference) {
	RouteProjections maps;
	for (const CompletedTrain& train : data.trains) {
		const int index = train.routeIndex;
		if (!data.hasRoute(index))
			continue;
		if (!maps.paths.count(index)) {
			maps.paths.emplace(index, data.routes[static_cast<std::size_t>(index)].path);
			maps.projections.emplace(index, buildRouteDiagramProjection(maps.paths.at(index), reference));
		}
		maps.byTrain.emplace(train.trainId, maps.projections.at(index));
	}
	return maps;
}

std::vector<std::pair<int, std::string>> usedRouteReferencesFrom(const CompletedRunData& data) {
	std::vector<std::pair<int, std::string>> used;
	for (const CompletedTrain& train : data.trains) {
		const int index = train.routeIndex;
		if (data.hasRoute(index)
			&& std::none_of(used.begin(), used.end(), [index](const auto& route) { return route.first == index; }))
			used.emplace_back(index, data.routes[static_cast<std::size_t>(index)].id);
	}
	return used;
}

namespace {

// routeDiagramData, which adds the export row of each sample or stop event to `csvRows` when it is given.
std::optional<RouteDiagramData> buildRouteDiagram(const CompletedRunData& data, bool timetable, int referenceIndex,
	std::vector<std::vector<std::string>>* csvRows) {
	if (!data.hasRoute(referenceIndex))
		return std::nullopt;
	RouteDiagramData result;
	result.referencePath = data.routes[static_cast<std::size_t>(referenceIndex)].path;
	if (result.referencePath.nodes.empty())
		return std::nullopt;
	const RouteProjections maps = projectRoutes(data, result.referencePath);
	const auto record = [&](const CompletedTrain& train, const char* kind, const std::string& station,
							int journey, int call, const RunResultValue& time, std::optional<double> x) {
		if (!csvRows)
			return;
		csvRows->push_back({train.trainId, result.referencePath.id, data.routes[static_cast<std::size_t>(train.routeIndex)].id,
			kind, station, journey ? std::to_string(journey) : "", call ? std::to_string(call) : "",
			time.available ? csv::formatDouble(time.value) : "", x ? csv::formatDouble(*x) : ""});
	};
	for (std::size_t trainIndex = 0; trainIndex < data.trains.size(); ++trainIndex) {
		const CompletedTrain& train = data.trains[trainIndex];
		const auto path = maps.paths.find(train.routeIndex);
		const auto projection = maps.projections.find(train.routeIndex);
		if (path == maps.paths.end() || projection == maps.projections.end())
			continue;
		const auto newSeries = [&](RouteDiagramSeries::Kind kind, const std::string& event, bool planned) {
			RouteDiagramSeries series;
			series.kind = kind;
			series.trainId = train.trainId;
			series.trainIndex = static_cast<int>(trainIndex);
			series.event = event;
			series.planned = planned;
			return series;
		};
		if (timetable) {
			std::vector<const TimetableResultRow*> rows;
			for (const TimetableResultRow& row : data.timetable)
				if (row.trainId == train.trainId)
					rows.push_back(&row);
			// Separate series at every absent or unprojectable event. Never connect
			// across a missing schedule value or an unavailable station visit.
			for (int kind = 0; kind < 4; ++kind) {
				const char* event = kind == 0 ? "planned arrival" : kind == 1 ? "planned departure"
					: kind == 2												  ? "simulated arrival"
																			  : "simulated departure";
				bool open = false;
				for (const TimetableResultRow* row : rows) {
					const auto x = stopPosition(train, row->journeyIndex - 1, path->second, projection->second);
					const RunResultValue time = kind == 0 ? row->plannedArrivalSeconds : kind == 1 ? row->plannedDepartureSeconds
						: kind == 2																   ? row->simulatedArrivalSeconds
																								   : row->simulatedDepartureSeconds;
					record(train, event, row->stationId, row->journeyIndex, row->callIndex, time, x);
					if (!x || !time.available) {
						open = false;
						continue;
					}
					if (!open) {
						result.series.push_back(newSeries(RouteDiagramSeries::Kind::Event, event, kind < 2));
						open = true;
					}
					result.series.back().points.push_back({*x, time.value, row->stationId, row->journeyIndex, row->callIndex});
				}
			}
			// A dwell is vertical: fixed reference distance, advancing time.
			for (const TimetableResultRow* row : rows) {
				const auto x = stopPosition(train, row->journeyIndex - 1, path->second, projection->second);
				for (int planned = 0; planned < 2; ++planned) {
					const auto arrival = planned ? row->plannedArrivalSeconds : row->simulatedArrivalSeconds;
					const auto departure = planned ? row->plannedDepartureSeconds : row->simulatedDepartureSeconds;
					if (!x || !arrival.available || !departure.available)
						continue;
					RouteDiagramSeries dwell = newSeries(RouteDiagramSeries::Kind::Dwell, planned ? "planned dwell" : "simulated dwell", planned != 0);
					dwell.points.push_back({*x, arrival.value, row->stationId, row->journeyIndex, row->callIndex});
					dwell.points.push_back({*x, departure.value, row->stationId, row->journeyIndex, row->callIndex});
					result.series.push_back(std::move(dwell));
				}
			}
		} else {
			const CompletedTrajectory& trajectory = train.trajectory;
			for (const TrajectorySegment& valid : trajectory.segments) {
				bool open = false;
				for (int t = valid.first; t <= valid.last; ++t) {
					const double sourceKm = routeDiagramTrajectoryKm(trajectory.at(trajectory.position, t).value_or(0.0));
					const auto x = projection->second.map(sourceKm);
					const RunResultValue time{true, t * data.settings.timestepSeconds};
					record(train, "trajectory", "", 0, 0, time, x);
					if (!x) {
						open = false;
						continue;
					}
					if (!open) {
						RouteDiagramSeries line = newSeries(RouteDiagramSeries::Kind::Trajectory, std::string(), false);
						line.routeId = data.routes[static_cast<std::size_t>(train.routeIndex)].id;
						result.series.push_back(std::move(line));
						open = true;
					}
					result.series.back().points.push_back({*x, time.value, std::string(), 0, 0});
				}
			}
		}
	}
	return result;
}

} // namespace

std::optional<RouteDiagramData> routeDiagramData(const CompletedRunData& data, bool timetable, int referenceIndex) {
	return buildRouteDiagram(data, timetable, referenceIndex, nullptr);
}

std::vector<std::vector<std::string>> routeDiagramCsvRows(const CompletedRunData& data, bool timetable,
	int referenceIndex) {
	std::vector<std::vector<std::string>> rows;
	buildRouteDiagram(data, timetable, referenceIndex, &rows);
	return rows;
}

std::optional<DelayDiagramData> delayDiagramData(const CompletedRunData& data, int referenceIndex) {
	if (!data.hasRoute(referenceIndex))
		return std::nullopt;
	DelayDiagramData result;
	result.referencePath = data.routes[static_cast<std::size_t>(referenceIndex)].path;
	if (result.referencePath.nodes.empty())
		return std::nullopt;
	// The positions of each station on the route in path order, once each: two sections share the node between them.
	std::map<std::string, std::vector<double>> stations;
	for (const RouteDiagramNode& node : result.referencePath.nodes) {
		if (node.stationName.empty() || !std::isfinite(node.positionKm))
			continue;
		std::vector<double>& positions = stations[node.stationName];
		if (std::find(positions.begin(), positions.end(), node.positionKm) == positions.end())
			positions.push_back(node.positionKm);
	}
	for (const CompletedTrain& train : data.trains) {
		std::vector<const TimetableResultRow*> calls; // at stations of the route, in journey order
		for (const TimetableResultRow& row : data.timetable)
			if (row.trainId == train.trainId && stations.count(row.stationId))
				calls.push_back(&row);
		if (calls.empty()) {
			result.withoutCalls.push_back(train.trainId);
			continue;
		}
		// The train moves up the route (1) or down it (-1) as between its last two calls that lie apart, and before
		// those as from its first to its last call at a station that the route passes once; 0 when neither says.
		std::vector<double> once;
		for (const TimetableResultRow* call : calls)
			if (const std::vector<double>& positions = stations.at(call->stationId); positions.size() == 1)
				once.push_back(positions.front());
		double direction = 0.0;
		if (once.size() >= 2 && once.front() != once.back())
			direction = once.back() > once.front() ? 1.0 : -1.0;
		DelayDiagramSeries series;
		series.trainId = train.trainId;
		std::optional<double> previous;
		for (const TimetableResultRow* call : calls) {
			const std::vector<double>& positions = stations.at(call->stationId);
			double x = positions.front();
			if (previous && direction != 0.0) {
				std::optional<double> ahead;
				for (const double position : positions)
					if ((position - *previous) * direction >= 0.0 && (!ahead || std::abs(position - *previous) < std::abs(*ahead - *previous)))
						ahead = position;
				x = ahead.value_or(x);
			}
			if (previous && x != *previous)
				direction = x > *previous ? 1.0 : -1.0;
			previous = x;
			if (call->arrivalDelaySeconds.available)
				series.points.push_back({x, call->arrivalDelaySeconds.value, call->stationId, call->journeyIndex, call->callIndex});
		}
		result.series.push_back(std::move(series));
	}
	return result;
}
