#include "diagrams/BlockingTimeScope.h"

#include "diagrams/SimulationHeaders.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

const char* blockingSegmentTypeName(BlockingTimeSegmentStyle style) {
	switch (style) {
		case BlockingTimeSegmentStyle::Station:
			return "station";
		case BlockingTimeSegmentStyle::Switch:
			return "switch";
		case BlockingTimeSegmentStyle::SwitchStation:
			return "switch/station";
		case BlockingTimeSegmentStyle::Critical:
			return "conflict";
		case BlockingTimeSegmentStyle::CriticalStation:
			return "conflict/station";
		case BlockingTimeSegmentStyle::Default:
		default:
			return "block";
	}
}

const char* blockingSegmentTypeName(const BlockingTimeDiagramSegment& segment) {
	return segment.capacityCritical ? "capacity critical block" : blockingSegmentTypeName(segment.style);
}

BlockingTimeScope defaultBlockingTimeScope() {
	BlockingTimeScope scope;
	scope.endTime = std::max(0.0, initial_variables.times * timestep);
	return scope;
}

std::vector<BlockingTimeDiagramSegment> buildAllBlockingTimeSegments() {
	std::vector<std::vector<BlockingTimeDiagramInput>> trains;
	std::vector<std::string> trainNames;
	trains.reserve(static_cast<std::size_t>(std::max(0, numRegions)));
	trainNames.reserve(static_cast<std::size_t>(std::max(0, numRegions)));
	for (int i = 0; i < numRegions; ++i) {
		const Train& t = regional_train[i];
		std::vector<BlockingTimeDiagramInput> blocks;
		blocks.reserve(static_cast<std::size_t>(std::max(0, t.N_BlockSections)));
		for (int j = 0; j < t.N_BlockSections; ++j) {
			BlockingTimeDiagramInput block;
			block.blockId = t.BlockTime[j].BlockID;
			block.startOccTime = t.BlockTime[j].StartOccTime;
			block.endOccTime = t.BlockTime[j].EndOccTime;
			block.posStart = t.BlockTime[j].PosStart;
			block.posEnd = t.BlockTime[j].PosEnd;
			block.switchName = t.BlockTime[j].SwitchName;
			block.stationName = t.BlockTime[j].stationName;
			block.isComplete = t.BlockTime[j].IsComplete;
			block.startApproachTime = t.BlockTime[j].StartApproachTime;
			block.startRunTime = t.BlockTime[j].StartRunTime;
			block.endRunTime = t.BlockTime[j].EndRunTime;
			block.endClearTime = t.BlockTime[j].EndClearTime;
			block.setupTime = t.BlockTime[j].setupTime;
			block.sightReactionTime = t.BlockTime[j].sightReacTime;
			block.releaseTime = t.BlockTime[j].ReleaseTime;
			block.runTimeMargin = t.BlockTime[j].RunTimeMargin;
			blocks.push_back(block);
		}
		trains.push_back(blocks);
		trainNames.push_back(t.trainDescription);
	}
	return buildBlockingTimeDiagramSegments(trains, trainNames);
}

// A segment whose ends do not both map is counted in `omitted` and, when `unmapped` is given, kept there unchanged.
std::vector<BlockingTimeDiagramSegment> projectBlockingSegments(
	const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::map<std::string, RouteDiagramProjection>& projections, int& omitted,
	std::vector<BlockingTimeDiagramSegment>* unmapped) {
	std::vector<BlockingTimeDiagramSegment> mapped;
	for (auto segment : segments) {
		const auto projection = projections.find(segment.trainName);
		const auto start = projection == projections.end() ? std::optional<double>()
														   : projection->second.map(segment.startPositionKm);
		const auto end = projection == projections.end() ? std::optional<double>()
														 : projection->second.map(segment.endPositionKm);
		if (!start || !end) {
			++omitted;
			if (unmapped)
				unmapped->push_back(std::move(segment));
			continue;
		}
		segment.startPositionKm = *start;
		segment.endPositionKm = *end;
		segment.midPositionKm = (*start + *end) / 2.0;
		mapped.push_back(std::move(segment));
	}
	return mapped;
}

std::vector<BlockingTimePlannedReference> buildBlockingTimePlannedReferences(
	const BlockingTimeScope& scope, const std::map<int, RouteDiagramPath>& paths,
	const std::map<int, RouteDiagramProjection>& projections, int& omitted) {
	std::vector<BlockingTimePlannedReference> references;
	const auto selectedSections = scope.routeIndex >= 0 && !scope.sectionIndices.empty()
		? scopedBlockSectionExtents(buildBlockSectionExtents(blockSectionInputsForRoute(train_route[scope.routeIndex])), scope.sectionIndices)
		: std::vector<BlockSectionExtent>();
	if (scope.routeIndex >= 0 && scope.trainIds.empty())
		return references;
	for (int i = 0; i < numRegions; ++i) {
		const Train& train = regional_train[i];
		if (!scope.trainIds.empty() && std::find(scope.trainIds.begin(), scope.trainIds.end(), train.trainDescription) == scope.trainIds.end())
			continue;
		if (train.Stations.empty())
			continue;
		const int stationCount = std::min(train.numStations, static_cast<int>(Train::kMaxTimetableStations));
		for (int stationIndex = 0; stationIndex < stationCount; ++stationIndex) {
			if (!train.stationIsOnRoute(stationIndex, scope.blockIds))
				continue;
			const auto path = paths.find(train.indexOfRoute);
			const auto projection = projections.find(train.indexOfRoute);
			const auto positionKm = path == paths.end() || projection == projections.end()
				? std::optional<double>()
				: routeDiagramStopPosition(train, stationIndex, path->second, projection->second);
			if (!positionKm) {
				++omitted;
				references.push_back({train.trainDescription, "", "", std::numeric_limits<double>::quiet_NaN(), 0.0});
				continue;
			}
			if (!scope.sectionIndices.empty() && !positionInBlockSections(*positionKm, selectedSections)) {
				references.push_back({train.trainDescription, "", "", std::numeric_limits<double>::quiet_NaN(), 0.0});
				continue; // Preserve the gap; do not bridge another visit of a repeated resource.
			}
			const auto append = [&](const char* eventType, double time) {
				if (!std::isfinite(time) || time < 0.0)
					return;
				references.push_back({train.trainDescription, train.stationNameForArrivalStats(stationIndex),
					eventType, time, *positionKm});
			};
			append("arrival", train.ScheduledArrivals[stationIndex]);
			append("departure", train.ScheduledDepartures[stationIndex]);
		}
	}
	return references;
}
