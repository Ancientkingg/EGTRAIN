#ifndef RUNEXPORTS_H
#define RUNEXPORTS_H

#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/BlockingTimeScope.h"
#include "diagrams/CapacityScope.h"
#include "diagrams/CompletedRunData.h"
#include "diagrams/RunResults.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class Train;

// CSV text of the finished run. Most functions here read the run from the simulation globals
// regional_train, numRegions, train_route, initial_variables and timestep, not from arguments.

std::vector<const Train*> runResultTrainPointers();
std::string csvValue(const RunResultValue& value);

std::string buildTrajectoryCsv(const QStringList& visibleTrainIds);
std::string buildTimetableCsv(const QStringList& visibleTrainIds);
std::string buildBlockingTimeCsv(const QStringList& visibleTrainIds,
	const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::vector<BlockingTimePlannedReference>& plannedReferences,
	const std::vector<std::vector<std::string>>& trajectories = {},
	const std::vector<BlockSectionExtent>& extents = {},
	const std::vector<BlockingTimeDiagramSegment>& unmapped = {});
std::string buildBlockingTimeCsv(const QStringList& visibleTrainIds);

QStringList allTrainIds();
std::function<std::string(const QStringList&)> snapshotCsv(
	const std::function<std::string(const QStringList&)>& provider);
bool writeCsvFileWithProvenance(const QString& path, const std::string& content,
	const RunProvenance& provenance);

// The same exports and diagram inputs from a completed run. They read nothing but their arguments and give the
// same text and values as the functions above and in BlockingTimeScope.h and CapacityScope.h do for the same run.

QStringList trainIdsFrom(const CompletedRunData& data);
std::string trajectoryCsvFrom(const CompletedRunData& data, const QStringList& visibleTrainIds);
std::string timetableCsvFrom(const CompletedRunData& data, const QStringList& visibleTrainIds);
// The whole run, the first route that a train runs on as reference, no recorded trajectories.
std::string blockingTimeCsvFrom(const CompletedRunData& data, const QStringList& visibleTrainIds);

// Every train's blocking-time segments, classified against the other trains.
std::vector<BlockingTimeDiagramSegment> blockTimesFrom(const CompletedRunData& data);
BlockingTimeScope defaultBlockingTimeScopeFrom(const CompletedRunData& data);
std::vector<BlockingTimePlannedReference> plannedReferencesFrom(const CompletedRunData& data,
	const BlockingTimeScope& scope, const std::map<int, RouteDiagramPath>& paths,
	const std::map<int, RouteDiagramProjection>& projections, int& omitted);

// A recorded trajectory inside the scope, as points of reference route X in km and elapsed seconds.
struct BlockingTimeTrajectory {
	std::string trainId;
	std::vector<std::pair<double, double>> points;
};

// What the blocking-time diagram of a scope shows and exports.
struct BlockingTimeDiagramData {
	RouteDiagramPath referencePath;
	std::vector<BlockingTimeDiagramSegment> segments; // Projected onto the reference route and inside the scope
	std::vector<BlockingTimeDiagramSegment> unmapped; // Could not be projected
	std::vector<BlockSectionExtent> extents;		  // The sections of the scope
	std::vector<BlockingTimePlannedReference> plannedReferences;
	std::vector<std::vector<BlockingTimePlannedReference>> plannedLines; // Clipped to the scope, for the chart
	std::vector<BlockingTimeTrajectory> trajectories;
	std::vector<std::vector<std::string>> trajectoryRows; // The export rows of the trajectories
	int omitted = 0;									  // Endpoints and events that could not be projected
};

// Empty when the scope has no reference route.
std::optional<BlockingTimeDiagramData> blockingTimeDiagramData(const CompletedRunData& data,
	const BlockingTimeScope& scope);

// The occupations of one train in the scope, with the reference time that the capacity analysis uses.
CapacityAnalysisTrain capacityInputFrom(const CompletedTrain& train, const CapacityAnalysisScope& scope);
// The trains with occupations in the scope. The order follows scope.occurrenceIds when it is given.
std::vector<CapacityAnalysisTrain> capacityInputsFrom(const CompletedRunData& data,
	const CapacityAnalysisScope& scope);
std::string capacityScopeIssueFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope);
CapacityAnalysisScope firstCapacityScopeWithPairFrom(const CompletedRunData& data);
QString capacityScopeLabelFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope);
// Empty when the result cannot be analysed.
std::string capacityAnalysisCsv(const CapacityAnalysisResult& result, const QString& sectionLabel);
// Empty when the scope has an issue or the occurrences do not form a chain.
std::string capacityCsvFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope);

// The routes that trains run on, as (route index, route id) in order of first use.
std::vector<std::pair<int, std::string>> usedRouteReferencesFrom(const CompletedRunData& data);

struct RouteDiagramPoint {
	double positionKm = 0.0; // On the reference route
	double seconds = 0.0;
	std::string station; // Timetable series only
	int journey = 0;
	int call = 0;
};

// One line of the route diagram. A line ends where a point is missing or cannot be projected.
struct RouteDiagramSeries {
	enum class Kind { Trajectory,
		Event,
		Dwell };
	Kind kind = Kind::Trajectory;
	std::string trainId;
	int trainIndex = 0;	  // Position of the train in the run
	std::string event;	  // "planned arrival", "simulated dwell" and so on; empty for a trajectory
	bool planned = false; // Of the timetable, not of the simulation
	std::string routeId;  // Route of the train; trajectories only
	std::vector<RouteDiagramPoint> points;
};

struct RouteDiagramData {
	RouteDiagramPath referencePath;
	std::vector<RouteDiagramSeries> series;
	std::vector<std::vector<std::string>> csvRows; // The export rows, with the events that have no point
};

// The train paths, or the timetable graph when `timetable` is set, on the reference route with the given index.
// Empty when the route does not exist or has no nodes.
std::optional<RouteDiagramData> routeDiagramData(const CompletedRunData& data, bool timetable, int referenceIndex);

#endif
