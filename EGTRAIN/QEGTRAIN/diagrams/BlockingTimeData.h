#ifndef BLOCKINGTIMEDATA_H
#define BLOCKINGTIMEDATA_H

#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/BlockingTimeScope.h"
#include "diagrams/CapacityAnalysis.h"
#include "diagrams/CapacityScope.h"
#include "diagrams/CompletedRunData.h"
#include "diagrams/RouteDiagramCoordinates.h"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// What the blocking-time diagram of a finished run shows and exports. The functions read nothing but their arguments.
//
// Times are seconds from the start of the run and positions on a reference route are km, unless a name or comment
// says otherwise.

/// Every train's blocking-time segments, classified against the other trains.
std::vector<BlockingTimeDiagramSegment> blockTimesFrom(const CompletedRunData& data);
/// The scope of the whole run.
BlockingTimeScope defaultBlockingTimeScopeFrom(const CompletedRunData& data);
/// What the scope dialog offers for the run: its routes and its length.
BlockingTimeScopeChoices blockingTimeScopeChoicesFrom(const CompletedRunData& data);
/// The planned arrivals and departures of the trains of the scope at the stops on the scope's blocks, at their position
/// on the reference route. A reference with a NaN time breaks the line where a stop cannot be projected or lies outside
/// the selected sections. `paths` and `projections` are keyed by route index. Adds the stops that cannot be projected
/// to `omitted`.
std::vector<BlockingTimePlannedReference> plannedReferencesFrom(const CompletedRunData& data,
	const BlockingTimeScope& scope, const std::map<int, RouteDiagramPath>& paths,
	const std::map<int, RouteDiagramProjection>& projections, int& omitted);

/// A recorded trajectory inside the scope, as points of reference route X in km and elapsed seconds.
struct BlockingTimeTrajectory {
	std::string trainId;						   ///< Name of the train in the run
	std::vector<std::pair<double, double>> points; ///< (km, s) pairs in sample order
};

/// What the blocking-time diagram of a scope shows and exports.
struct BlockingTimeDiagramData {
	RouteDiagramPath referencePath;					  ///< The reference route
	std::vector<BlockingTimeDiagramSegment> segments; ///< Projected onto the reference route and inside the scope
	std::vector<BlockingTimeDiagramSegment> unmapped; ///< Could not be projected
	std::vector<BlockSectionExtent> extents;		  ///< The sections of the scope
	int routeSectionCount = 0;						  ///< The sections of the whole reference route
	/// Planned stop events inside the time range.
	std::vector<BlockingTimePlannedReference> plannedReferences;
	/// Clipped to the scope, for the chart.
	std::vector<std::vector<BlockingTimePlannedReference>> plannedLines;
	/// Recorded samples that lie inside an envelope of their train. The export writes a row for each of them.
	std::vector<BlockingTimeTrajectory> trajectories;
	/// Elapsed seconds that the chart spans: the interval of the scope, at least 1 s. For a compressed diagram, the
	/// envelopes with a margin (see compressedBlockingTimeDiagramData).
	std::pair<double, double> timeRange;
	int omitted = 0; ///< Endpoints and events that could not be projected
	/// Trains of the chart that repeat a train of the run, by id, with the id of that train: the first train one period
	/// later of a compressed diagram. Such a train lies on the route of its train and has its recorded movement, colour
	/// and service.
	std::map<std::string, std::string> repeatedTrains;
};

/// Empty when the scope has no reference route.
std::optional<BlockingTimeDiagramData> blockingTimeDiagramData(const CompletedRunData& data,
	const BlockingTimeScope& scope);
/// The compressed blocking-time diagram of a capacity result, on the route and the blocks of its scope: the compressed
/// occupations of `result` as envelopes, and the recorded samples of each train of the result that lie inside them,
/// moved in time as its occupations are (compressed minus profile reference). An envelope that the route of its train
/// does not project lies on the blocks of the scope that share a resource with it, from the first to the last. With scope.repeatFirstTrain the first
/// train one period later is a repeated train of the first train, whose samples move by its compressed reference minus
/// the profile reference of the first train. It has no planned references. The time range spans the envelopes with a
/// margin of 5 % of their span, at least 1 s, and starts below 0 s only when an envelope does; it is 0 s to 1 s without
/// an envelope. Empty when the scope has no route.
std::optional<BlockingTimeDiagramData> compressedBlockingTimeDiagramData(const CompletedRunData& data,
	const CapacityAnalysisScope& scope, const CapacityAnalysisResult& result);

#endif // BLOCKINGTIMEDATA_H
