#ifndef RUNEXPORTS_H
#define RUNEXPORTS_H

#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/BlockingTimeScope.h"
#include "diagrams/CapacityScope.h"
#include "diagrams/CompletedRunData.h"
#include "diagrams/RunResults.h"

#include <QImage>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// CSV text of a finished run and the inputs of its diagrams. The functions read nothing but their arguments.
//
// Times are seconds from the start of the run and positions on a reference route are km, unless a name or comment
// says otherwise. A CSV export lists only the trains in `visibleTrainIds` and is empty when it has no row.

/// The CSV field of a value: the number, or an empty field when it is unavailable.
std::string csvValue(const RunResultValue& value);

class QTableView;

/// The whole table as an image: every row and column at its natural size, with no scroll bars.
QImage fullTableImage(const QTableView& table);

/// The blocking-time CSV: the envelopes of the visible trains in `segments`, their planned stop references, the recorded
/// trajectory rows, the envelopes that could not be projected and one row for each section in `extents`. Every row has
/// the columns of blockingTimeCsvHeader().
std::string buildBlockingTimeCsv(const QStringList& visibleTrainIds,
	const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::vector<BlockingTimePlannedReference>& plannedReferences,
	const std::vector<std::vector<std::string>>& trajectories = {},
	const std::vector<BlockSectionExtent>& extents = {},
	const std::vector<BlockingTimeDiagramSegment>& unmapped = {});

/// Writes a CSV export and its provenance sidecar. False when a write fails.
bool writeCsvFileWithProvenance(const QString& path, const std::string& content,
	const RunProvenance& provenance);

/// The ids of the trains of the run, in run order.
QStringList trainIdsFrom(const CompletedRunData& data);
/// The recorded samples: time in s, position in m, speed in m/s, power in kW, tractive effort in kN, energy in kWh and
/// the block, one row for each valid sample.
std::string trajectoryCsvFrom(const CompletedRunData& data, const QStringList& visibleTrainIds);
/// The planned and simulated times and the delays of every stop, in seconds.
std::string timetableCsvFrom(const CompletedRunData& data, const QStringList& visibleTrainIds);

/// The CSV provider of a result window: it shares the run and gives one export of it for the trains that the window
/// shows, whatever the simulation holds by then.
using CompletedRunCsv = std::string (*)(const CompletedRunData& data, const QStringList& visibleTrainIds);
/// A provider that calls `csv` on the shared run with the visible train ids.
std::function<std::string(const QStringList&)> csvProviderFrom(std::shared_ptr<const CompletedRunData> run,
	CompletedRunCsv csv);

/// The whole run, the first route that a train runs on as reference, no recorded trajectories.
std::string blockingTimeCsvFrom(const CompletedRunData& data, const QStringList& visibleTrainIds);

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
	/// Planned stop events inside the time range.
	std::vector<BlockingTimePlannedReference> plannedReferences;
	/// Clipped to the scope, for the chart.
	std::vector<std::vector<BlockingTimePlannedReference>> plannedLines;
	std::vector<BlockingTimeTrajectory> trajectories;	  ///< Recorded samples that lie inside an envelope of their train
	std::vector<std::vector<std::string>> trajectoryRows; ///< The export rows of the trajectories
	/// Elapsed seconds that the chart spans: the interval of the scope, at least 1 s. For a compressed diagram, the
	/// envelopes with a margin (see compressedBlockingTimeDiagramData).
	std::pair<double, double> timeRange;
	int omitted = 0; ///< Endpoints and events that could not be projected
};

/// Empty when the scope has no reference route.
std::optional<BlockingTimeDiagramData> blockingTimeDiagramData(const CompletedRunData& data,
	const BlockingTimeScope& scope);
/// The compressed blocking-time diagram of a capacity result, on the route and the blocks of its scope: the compressed
/// occupations of `result` as envelopes, and the recorded samples of each train of the result that lie inside them,
/// moved in time as its occupations are (compressed minus profile reference). It has no planned references. The time
/// range spans the envelopes with a margin of 5 % of their span, at least 1 s, and starts below 0 s only when an envelope
/// does; it is 0 s to 1 s without an envelope. Empty when the scope has no route.
std::optional<BlockingTimeDiagramData> compressedBlockingTimeDiagramData(const CompletedRunData& data,
	const CapacityAnalysisScope& scope, const CapacityAnalysisResult& result);

/// The evidence as text, "leader/follower (leader=… s; follower=… s; candidate=… s)", joined with "; ". With `labels`
/// the blocks are named by their labels, "leader / follower", for the tooltips of the window; the export names them by
/// their ids.
std::string capacityEvidenceText(const std::vector<CapacityHeadwayEvidence>& evidence, const SceneSectionLabels* labels = nullptr);
/// The capacity CSV of the result, with times in seconds and the cycle share in percent. Empty when the result cannot
/// be analysed or has no cycle time or no cycle share.
std::string capacityAnalysisCsv(const CapacityAnalysisResult& result, const QString& sectionLabel);
/// Empty when the scope has an issue or the occurrences do not form a chain.
std::string capacityCsvFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope);

/// The routes that trains run on, as (route index, route id) in order of first use.
std::vector<std::pair<int, std::string>> usedRouteReferencesFrom(const CompletedRunData& data);

/// One point of a route diagram line.
struct RouteDiagramPoint {
	double positionKm = 0.0; ///< On the reference route
	double seconds = 0.0;	 ///< Time of the point, in seconds of the run
	std::string station;	 ///< Timetable series only
	int journey = 0;		 ///< TimetableResultRow::journeyIndex; 0 for a trajectory
	int call = 0;			 ///< TimetableResultRow::callIndex; 0 for a trajectory
};

/// One line of the route diagram. A line ends where a point is missing or cannot be projected.
struct RouteDiagramSeries {
	/// What the line shows.
	enum class Kind { Trajectory, ///< Recorded positions of the train
		Event,					  ///< One kind of planned or simulated stop event, over the stops of the train
		/// A stop, from its arrival to its departure at one position
		Dwell };
	Kind kind = Kind::Trajectory;		   ///< What the line shows
	std::string trainId;				   ///< Name of the train in the run
	int trainIndex = 0;					   ///< Position of the train in the run
	std::string event;					   ///< "planned arrival", "simulated dwell" and so on; empty for a trajectory
	bool planned = false;				   ///< Of the timetable, not of the simulation
	std::string routeId;				   ///< Route of the train; trajectories only
	std::vector<RouteDiagramPoint> points; ///< In sample or stop order
};

/// What the route diagram shows and exports.
struct RouteDiagramData {
	RouteDiagramPath referencePath;				   ///< The reference route
	std::vector<RouteDiagramSeries> series;		   ///< The lines of the chart
	std::vector<std::vector<std::string>> csvRows; ///< The export rows, with the events that have no point
};

/// The train paths, or the timetable graph when `timetable` is set, on the reference route with the given index.
/// Empty when the route does not exist or has no nodes.
std::optional<RouteDiagramData> routeDiagramData(const CompletedRunData& data, bool timetable, int referenceIndex);

#endif
