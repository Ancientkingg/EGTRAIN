#include "diagrams/CompletedRunData.h"

#include "diagrams/RunExports.h"
#include "diagrams/SimulationHeaders.h"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <utility>

BlockingTimeDiagramInput blockingTimeInputFrom(const BlockingTimes& source) {
	BlockingTimeDiagramInput block;
	block.blockId = source.BlockID;
	block.startOccTime = source.StartOccTime;
	block.endOccTime = source.EndOccTime;
	block.posStart = source.PosStart;
	block.posEnd = source.PosEnd;
	block.switchName = source.SwitchName;
	block.stationName = source.stationName;
	block.isComplete = source.IsComplete;
	block.startApproachTime = source.StartApproachTime;
	block.startRunTime = source.StartRunTime;
	block.endRunTime = source.EndRunTime;
	block.endClearTime = source.EndClearTime;
	block.setupTime = source.setupTime;
	block.sightReactionTime = source.sightReacTime;
	block.releaseTime = source.ReleaseTime;
	block.runTimeMargin = source.RunTimeMargin;
	return block;
}

namespace {

std::vector<BlockingTimeDiagramInput> convertBlockTimes(const Train& train) {
	std::vector<BlockingTimeDiagramInput> blocks;
	blocks.reserve(static_cast<std::size_t>(std::max(0, train.N_BlockSections)));
	for (int j = 0; j < train.N_BlockSections; ++j)
		blocks.push_back(blockingTimeInputFrom(train.BlockTime[j]));
	return blocks;
}

// Each distinct block name of a trajectory is stored once.
class BlockNameTable {
public:
	explicit BlockNameTable(std::vector<std::string>& names) : m_names(names) {}

	std::uint32_t indexOf(const std::string& name) {
		const auto found = m_indices.find(name);
		if (found != m_indices.end())
			return found->second;
		const auto index = static_cast<std::uint32_t>(m_names.size());
		m_names.push_back(name);
		m_indices.emplace(name, index);
		return index;
	}

private:
	std::vector<std::string>& m_names;
	std::unordered_map<std::string, std::uint32_t> m_indices;
};

std::vector<double> sliceSeries(const std::vector<double>& series, int first, int last) {
	if (first >= static_cast<int>(series.size()))
		return {};
	const auto end = std::min(series.size(), static_cast<std::size_t>(last) + 1);
	return std::vector<double>(series.begin() + first, series.begin() + static_cast<std::ptrdiff_t>(end));
}

// The samples from the first to the last valid position of the train. Nothing outside the valid runs is read.
CompletedTrajectory captureTrajectory(const Train& train, BlockNameTable& blocks) {
	CompletedTrajectory trajectory;
	if (train.earliestActiveTrajectoryIndex < 0)
		return trajectory;
	trajectory.segments = validTrajectorySegments(train.instant_spatial_position,
		train.earliestActiveTrajectoryIndex, train.End_Time);
	if (trajectory.segments.empty())
		return trajectory;
	const int first = trajectory.segments.front().first;
	const int last = trajectory.segments.back().last;
	trajectory.firstSample = first;
	trajectory.position = sliceSeries(train.instant_spatial_position, first, last);
	trajectory.speed = sliceSeries(train.instant_train_speed, first, last);
	trajectory.power = sliceSeries(train.instant_train_power_consumption, first, last);
	trajectory.energy = sliceSeries(train.instant_train_energy_consumption, first, last);
	trajectory.effort = sliceSeries(train.instant_train_tractive_effort, first, last);
	const auto& occupied = train.instant_block_section_occupied;
	for (int sample = first; sample <= last && sample < static_cast<int>(occupied.size()); ++sample)
		trajectory.block.push_back(blocks.indexOf(occupied[static_cast<std::size_t>(sample)]));
	return trajectory;
}

// The ids of the sections of a route that hold a station node, found as Train::stationIsOnRoute finds them.
std::vector<std::string> sectionsWithStation(const Route& route, const std::string& stationName) {
	std::vector<std::string> ids;
	if (stationName.empty() || stationName == "None")
		return ids;
	const auto matches = [&stationName](const Node& node) {
		return node.station && node.stationName == stationName;
	};
	const std::size_t count = std::min(static_cast<std::size_t>(std::max(route.N_Block_Sections, 0)),
		route.sequence_of_block_sections.size());
	for (std::size_t h = 0; h < count; ++h) {
		const Section& section = route.sequence_of_block_sections[h];
		bool found = matches(section.start_node) || matches(section.end_node);
		for (int arcIndex = 0; !found && arcIndex < section.total_arcs; ++arcIndex)
			found = matches(section.arcs_in_signalling_block_section[arcIndex].endNode);
		if (found && std::find(ids.begin(), ids.end(), section.ID) == ids.end())
			ids.push_back(section.ID);
	}
	return ids;
}

// Trains that run on one route share the search for a station.
class StationSections {
public:
	const std::vector<std::string>& of(int routeIndex, const std::string& stationName) {
		auto found = m_cache.find({routeIndex, stationName});
		if (found == m_cache.end())
			found = m_cache.emplace(std::make_pair(routeIndex, stationName),
							   sectionsWithStation(train_route[static_cast<std::size_t>(routeIndex)], stationName))
						.first;
		return found->second;
	}

private:
	std::map<std::pair<int, std::string>, std::vector<std::string>> m_cache;
};

std::vector<CompletedStop> captureStops(const Train& train, StationSections& sections) {
	std::vector<CompletedStop> stops;
	const int count = std::min({train.numStations, static_cast<int>(Train::kMaxTimetableStations),
		static_cast<int>(train.Stations.size())});
	const bool onKnownRoute = train.indexOfRoute >= 0 && train.indexOfRoute < static_cast<int>(train_route.size());
	for (int index = 0; index < count; ++index) {
		const Node& node = train.Stations[static_cast<std::size_t>(index)];
		CompletedStop stop;
		stop.station = train.stationNameForArrivalStats(index);
		stop.nodeId = node.sceneNodeId;
		stop.routeX = node.X;
		stop.routePositionMeters = train.stationRoutePositionMeters(index);
		stop.scheduledArrival = train.ScheduledArrivals[index];
		stop.scheduledDeparture = train.ScheduledDepartures[index];
		if (onKnownRoute)
			stop.routeBlockIds = sections.of(train.indexOfRoute, node.stationName);
		stops.push_back(std::move(stop));
	}
	return stops;
}

CompletedTrain captureTrain(const Train& source, BlockNameTable& blocks, StationSections& sections) {
	CompletedTrain train;
	train.trainId = source.trainDescription;
	train.operatingCode = source.operatingCode;
	train.serviceId = source.serviceId;
	train.serviceOccurrence = source.serviceOccurrence;
	train.routeIndex = source.indexOfRoute;
	train.routeId = source.TrainRouteID;
	train.scheduledDepartureTime = source.scheduled_departure_time;
	train.runStartTime = source.RunStartTime;
	train.blockTimes = convertBlockTimes(source);
	train.stops = captureStops(source, sections);
	train.trajectory = captureTrajectory(source, blocks);
	return train;
}

} // namespace

CompletedRunData captureCompletedRun(const SceneModel* scene, long long startOffsetSeconds,
	const RunProvenance& provenance) {
	CompletedRunData data;
	data.settings.timestepSeconds = timestep;
	data.settings.horizonSteps = initial_variables.times;
	data.settings.startOffsetSeconds = startOffsetSeconds;
	data.settings.caseName = provenance.caseName;
	data.provenance = provenance;

	const std::vector<const Train*> trains = runResultTrainPointers();
	data.results = buildRunResults(trains, timestep);
	data.timetable = buildTimetableResults(trains);

	BlockNameTable blocks(data.blockNames);
	StationSections sections;
	data.trains.reserve(trains.size());
	for (const Train* train : trains)
		data.trains.push_back(captureTrain(*train, blocks, sections));

	data.routes.reserve(train_route.size());
	for (const Route& route : train_route) {
		CompletedRoute captured;
		captured.id = route.ID;
		captured.corridor = route.corridor;
		captured.reversedDirection = route.reversed_direction;
		captured.blocks = blockSectionInputsForRoute(route);
		captured.path = routeDiagramPath(route, scene);
		data.routes.push_back(std::move(captured));
	}
	return data;
}
