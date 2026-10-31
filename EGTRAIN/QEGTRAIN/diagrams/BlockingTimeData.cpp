#include "diagrams/BlockingTimeData.h"

#include "diagrams/RouteDiagramData.h"
#include "util/TrajectoryUtil.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <utility>

namespace {

// The route of the first train in the list, or of the first train when the list is empty, that runs on a known route.
int firstRouteOfTrains(const CompletedRunData& data, const std::vector<std::string>& trainIds) {
	for (const CompletedTrain& train : data.trains)
		if ((trainIds.empty() || std::find(trainIds.begin(), trainIds.end(), train.trainId) != trainIds.end())
			&& data.hasRoute(train.routeIndex))
			return train.routeIndex;
	return -1;
}

// Adds to the diagram the recorded samples of each train that lie inside one of its envelopes there, each moved in time
// by the shift of its train in `shifts` (0 s for a train without one). A repeated train of the diagram has the samples of
// the train it repeats.
void addRecordedTrajectories(BlockingTimeDiagramData& diagram, const CompletedRunData& data,
	const std::map<std::string, RouteDiagramProjection>& projections, const std::map<std::string, double>& shifts) {
	const double timestepSeconds = data.settings.timestepSeconds;
	std::vector<std::pair<const CompletedTrain*, std::string>> sampled; // A train of the run and the id it has in the chart
	for (const CompletedTrain& train : data.trains)
		sampled.emplace_back(&train, train.trainId);
	for (const auto& [repeat, source] : diagram.repeatedTrains)
		for (const CompletedTrain& train : data.trains)
			if (train.trainId == source)
				sampled.emplace_back(&train, repeat);
	for (const auto& [train, trainId] : sampled) {
		const CompletedTrajectory& trajectory = train->trajectory;
		const auto projection = projections.find(trainId);
		if (trajectory.segments.empty() || projection == projections.end())
			continue;
		const auto shift = shifts.find(trainId);
		const double shiftSeconds = shift == shifts.end() ? 0.0 : shift->second;
		std::vector<const BlockingTimeDiagramSegment*> own;
		for (const auto& block : diagram.segments)
			if (block.trainName == trainId)
				own.push_back(&block);
		for (const TrajectorySegment& valid : trajectory.segments) {
			bool open = false;
			for (int t = valid.first; t <= valid.last; ++t) {
				const double seconds = t * timestepSeconds + shiftSeconds;
				const double sourceKm = routeDiagramTrajectoryKm(trajectory.at(trajectory.position, t).value_or(0.0));
				const auto x = projection->second.map(sourceKm);
				const bool inside = x && std::any_of(own.begin(), own.end(), [&](const BlockingTimeDiagramSegment* block) {
					return seconds >= block->startTime && seconds <= block->endTime
						&& *x >= std::min(block->startPositionKm, block->endPositionKm)
						&& *x <= std::max(block->startPositionKm, block->endPositionKm);
				});
				if (!inside) {
					open = false;
					continue;
				}
				if (!open) {
					diagram.trajectories.push_back({trainId, {}});
					open = true;
				}
				diagram.trajectories.back().points.emplace_back(*x, seconds);
			}
		}
	}
}

// Places a segment that the route of its train does not project onto the reference route on the sections of `extents`
// that share a resource with its block, from the start of the first of them to the end of the last. False when no
// section with geometry shares one.
bool placeOnSharedSections(BlockingTimeDiagramSegment& segment, const std::vector<BlockSectionExtent>& extents) {
	const BlockSectionExtent* first = nullptr;
	const BlockSectionExtent* last = nullptr;
	for (const BlockSectionExtent& extent : extents) {
		if (!extent.geometryValid || !shareBlockingTimeResource(extent.id, segment.blockId))
			continue;
		if (!first)
			first = &extent;
		last = &extent;
	}
	if (!first)
		return false;
	segment.startPositionKm = first->startKm;
	segment.endPositionKm = last->endKm;
	segment.midPositionKm = (segment.startPositionKm + segment.endPositionKm) / 2.0;
	return true;
}

} // namespace

std::vector<BlockingTimeDiagramSegment> blockTimesFrom(const CompletedRunData& data) {
	std::vector<std::vector<BlockingTimeDiagramInput>> trains;
	std::vector<std::string> trainNames;
	trains.reserve(data.trains.size());
	trainNames.reserve(data.trains.size());
	for (const CompletedTrain& train : data.trains) {
		trains.push_back(train.blockTimes);
		trainNames.push_back(train.trainId);
	}
	return buildBlockingTimeDiagramSegments(trains, trainNames);
}

BlockingTimeScope defaultBlockingTimeScopeFrom(const CompletedRunData& data) {
	return defaultBlockingTimeScope(data.settings.horizonSeconds());
}

BlockingTimeScopeChoices blockingTimeScopeChoicesFrom(const CompletedRunData& data) {
	BlockingTimeScopeChoices choices;
	choices.caseEndSeconds = data.settings.horizonSeconds();
	choices.stepSeconds = data.settings.timestepSeconds;
	choices.sectionLabels = data.sectionLabels;
	for (const CompletedRoute& route : data.routes) {
		BlockingTimeScopeChoices::Route choice;
		choice.id = route.id;
		choice.corridor = route.corridor;
		for (const BlockSectionInput& block : route.blocks)
			choice.blockIds.push_back(block.id);
		choices.routes.push_back(std::move(choice));
	}
	return choices;
}

std::vector<BlockingTimePlannedReference> plannedReferencesFrom(const CompletedRunData& data,
	const BlockingTimeScope& scope, const std::map<int, RouteDiagramPath>& paths,
	const std::map<int, RouteDiagramProjection>& projections, int& omitted) {
	std::vector<BlockingTimePlannedReference> references;
	if (scope.routeIndex >= 0 && scope.blockIds.empty() && scope.trainIds.empty())
		return references; // A route without blocks and no train list has no stop in its scope.
	const auto selectedSections = data.hasRoute(scope.routeIndex) && !scope.sectionIndices.empty()
		? scopedBlockSectionExtents(buildBlockSectionExtents(data.routes[static_cast<std::size_t>(scope.routeIndex)].blocks), scope.sectionIndices)
		: std::vector<BlockSectionExtent>();
	for (const CompletedTrain& train : data.trains) {
		if (!scope.trainIds.empty() && std::find(scope.trainIds.begin(), scope.trainIds.end(), train.trainId) == scope.trainIds.end())
			continue;
		for (const CompletedStop& stop : train.stops) {
			if (!stop.onRoute(scope.blockIds))
				continue;
			const auto path = paths.find(train.routeIndex);
			const auto projection = projections.find(train.routeIndex);
			const auto positionKm = path == paths.end() || projection == projections.end()
				? std::optional<double>()
				: routeDiagramStopPosition(stop, path->second, projection->second);
			if (!positionKm) {
				++omitted;
				references.push_back({train.trainId, "", "", std::numeric_limits<double>::quiet_NaN(), 0.0});
				continue;
			}
			if (!scope.sectionIndices.empty() && !positionInBlockSections(*positionKm, selectedSections)) {
				references.push_back({train.trainId, "", "", std::numeric_limits<double>::quiet_NaN(), 0.0});
				continue; // Preserve the gap; do not bridge another visit of a repeated resource.
			}
			const auto append = [&](const char* eventType, double seconds) {
				if (!std::isfinite(seconds) || seconds < 0.0)
					return;
				references.push_back({train.trainId, stop.station, eventType, seconds, *positionKm});
			};
			append("arrival", stop.scheduledArrival);
			append("departure", stop.scheduledDeparture);
		}
	}
	return references;
}

std::optional<BlockingTimeDiagramData> blockingTimeDiagramData(const CompletedRunData& data,
	const BlockingTimeScope& scope) {
	// Empty filters keep every envelope, so a route without blocks and no train list keeps none itself.
	const std::vector<BlockingTimeDiagramSegment> scoped = scope.routeIndex >= 0 && scope.blockIds.empty() && scope.trainIds.empty()
		? std::vector<BlockingTimeDiagramSegment>()
		: filterBlockingTimeDiagramSegments(blockTimesFrom(data), scope.trainIds, scope.blockIds, scope.startTime,
			  scope.endTime);
	const int referenceIndex = scope.routeIndex >= 0 ? scope.routeIndex : firstRouteOfTrains(data, scope.trainIds);
	if (!data.hasRoute(referenceIndex))
		return std::nullopt;
	const CompletedRoute& referenceRoute = data.routes[static_cast<std::size_t>(referenceIndex)];

	BlockingTimeDiagramData result;
	result.referencePath = referenceRoute.path;
	result.timeRange = {scope.startTime, std::max(scope.endTime, scope.startTime + 1.0)};
	const RouteProjections maps = projectRoutes(data, result.referencePath);
	result.segments = projectBlockingSegments(scoped, maps.byTrain, result.omitted, &result.unmapped);
	const auto allExtents = buildBlockSectionExtents(referenceRoute.blocks);
	result.extents = scopedBlockSectionExtents(allExtents, scope.sectionIndices);
	result.routeSectionCount = static_cast<int>(allExtents.size());
	matchBlockingSegmentsToSections(result.segments, allExtents, result.referencePath.id);
	if (!scope.sectionIndices.empty())
		result.segments.erase(std::remove_if(result.segments.begin(), result.segments.end(),
								  [&result](const auto& segment) { return !blockingSegmentInSections(segment, result.extents); }),
			result.segments.end());
	const auto allPlanned = plannedReferencesFrom(data, scope, maps.paths, maps.projections, result.omitted);
	result.plannedReferences = filterBlockingTimePlannedReferences(allPlanned, scope.startTime, scope.endTime);
	result.plannedLines = clipBlockingTimePlannedReferences(allPlanned, scope.startTime, scope.endTime);
	// The envelopes are clipped to the interval of the scope, so the samples inside them are too.
	addRecordedTrajectories(result, data, maps.byTrain, {});
	return result;
}

std::optional<BlockingTimeDiagramData> compressedBlockingTimeDiagramData(const CompletedRunData& data,
	const CapacityAnalysisScope& scope, const CapacityAnalysisResult& result) {
	if (!data.hasRoute(scope.routeIndex))
		return std::nullopt;
	const CompletedRoute& route = data.routes[static_cast<std::size_t>(scope.routeIndex)];
	BlockingTimeDiagramData diagram;
	diagram.referencePath = route.path;
	RouteProjections maps = projectRoutes(data, diagram.referencePath);
	// The first train one period later lies on the route of the first train.
	const std::string nextPeriod = capacityNextPeriodIdentity(result.firstIdentity);
	if (scope.repeatFirstTrain
		&& std::find(result.trainIdentities.begin(), result.trainIdentities.end(), nextPeriod) != result.trainIdentities.end()) {
		diagram.repeatedTrains.emplace(nextPeriod, result.firstIdentity);
		const auto projection = maps.byTrain.find(result.firstIdentity);
		if (projection != maps.byTrain.end())
			maps.byTrain.emplace(nextPeriod, projection->second);
	}
	// A compressed occupation can start before 0 s, which validBlockingTimeDiagramInput takes for a missing time, so the
	// segments are built from the occupations moved to 0 s or later and then moved back.
	double lift = 0.0;
	for (const auto& occupations : result.compressedOccupations)
		for (const BlockingTimeDiagramInput& occupation : occupations)
			lift = std::max(lift, -occupation.startOccTime);
	auto lifted = result.compressedOccupations;
	for (auto& occupations : lifted)
		for (BlockingTimeDiagramInput& occupation : occupations) {
			occupation.startOccTime += lift;
			occupation.endOccTime += lift;
		}
	auto shifted = buildBlockingTimeDiagramSegments(lifted, result.trainIdentities);
	for (BlockingTimeDiagramSegment& segment : shifted) {
		segment.startTime -= lift;
		segment.endTime -= lift;
		segment.originalStartTime -= lift;
		segment.originalEndTime -= lift;
	}
	restoreCompressedOriginalTimes(shifted, result.compression);
	diagram.segments = projectBlockingSegments(shifted, maps.byTrain, diagram.omitted, &diagram.unmapped);
	const auto allExtents = buildBlockSectionExtents(route.blocks);
	diagram.extents = scopedBlockSectionExtents(allExtents, scope.sectionIndices);
	diagram.routeSectionCount = static_cast<int>(allExtents.size());
	// Every analysed occupation shares a resource with a block of the scope, so one that the route of its train does not
	// project is drawn on the blocks of the scope that share a resource with it.
	std::vector<BlockingTimeDiagramSegment> unplaced;
	for (BlockingTimeDiagramSegment& segment : diagram.unmapped) {
		if (placeOnSharedSections(segment, diagram.extents)) {
			diagram.segments.push_back(std::move(segment));
			--diagram.omitted;
		} else {
			unplaced.push_back(std::move(segment));
		}
	}
	diagram.unmapped = std::move(unplaced);
	matchBlockingSegmentsToSections(diagram.segments, allExtents, diagram.referencePath.id);
	// The samples move as the occupations do, the shift that restoreCompressedOriginalTimes takes back. Those of the first
	// train one period later are the samples of the first train, so they move from its profile reference.
	std::map<std::string, double> shifts;
	for (const CapacityCompressionRow& row : result.compression) {
		const bool repeated = diagram.repeatedTrains.count(row.identity) > 0;
		shifts.emplace(row.identity, row.compressedReference - (repeated ? result.compression.front() : row).originalReference);
	}
	addRecordedTrajectories(diagram, data, maps.byTrain, shifts);
	diagram.timeRange = {0.0, 1.0};
	if (!diagram.segments.empty()) {
		double first = diagram.segments.front().startTime;
		double last = diagram.segments.front().endTime;
		for (const auto& segment : diagram.segments) {
			first = std::min(first, segment.startTime);
			last = std::max(last, segment.endTime);
		}
		const double margin = std::max(1.0, 0.05 * (last - first));
		diagram.timeRange = {first >= 0.0 ? std::max(0.0, first - margin) : first - margin, last + margin};
	}
	return diagram;
}
