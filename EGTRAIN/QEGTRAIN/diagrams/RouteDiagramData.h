#ifndef ROUTEDIAGRAMDATA_H
#define ROUTEDIAGRAMDATA_H

#include "diagrams/CompletedRunData.h"
#include "diagrams/RouteDiagramCoordinates.h"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// What the route diagrams of a finished run show and export, and what its Delays chart shows. The functions read
// nothing but their arguments.
//
// Times are seconds from the start of the run and positions on a reference route are km, unless a name or comment
// says otherwise.

/// The paths of the routes that trains run on and their projections onto a reference route.
struct RouteProjections {
	std::map<int, RouteDiagramPath> paths;				   ///< The path of each route that a train runs on, by route index
	std::map<int, RouteDiagramProjection> projections;	   ///< The projection of each of those routes, by route index
	std::map<std::string, RouteDiagramProjection> byTrain; ///< The projection of the route of each train, by train id
};

/// Projects the route of every train onto the reference route.
RouteProjections projectRoutes(const CompletedRunData& data, const RouteDiagramPath& reference);

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

/// What the route diagram shows.
struct RouteDiagramData {
	RouteDiagramPath referencePath;			///< The reference route
	std::vector<RouteDiagramSeries> series; ///< The lines of the chart
};

/// The train paths, or the timetable graph when `timetable` is set, on the reference route with the given index.
/// Empty when the route does not exist or has no nodes.
std::optional<RouteDiagramData> routeDiagramData(const CompletedRunData& data, bool timetable, int referenceIndex);

/// The export rows of routeDiagramData with the same arguments, one for each sample or stop event in the order of the
/// chart, with the events that have no point; the columns of buildRouteDiagramCsv. Empty when there is no diagram.
/// The rows are made on each call, so that a window keeps only the run until it exports.
std::vector<std::vector<std::string>> routeDiagramCsvRows(const CompletedRunData& data, bool timetable,
	int referenceIndex);

/// One arrival delay of the Delays chart, at a station of the reference route.
struct DelayDiagramPoint {
	double positionKm = 0.0;   ///< Of the station node on the reference route
	double delaySeconds = 0.0; ///< TimetableResultRow::arrivalDelaySeconds
	std::string station;	   ///< TimetableResultRow::stationId
	int journey = 0;		   ///< TimetableResultRow::journeyIndex
	int call = 0;			   ///< TimetableResultRow::callIndex
};

/// The arrival delays of one train at the stations of the reference route.
struct DelayDiagramSeries {
	std::string trainId;				   ///< Name of the train in the run
	std::vector<DelayDiagramPoint> points; ///< In journey order; empty when no call there has an arrival delay
};

/// What the Delays chart shows.
struct DelayDiagramData {
	RouteDiagramPath referencePath;			///< The reference route
	std::vector<DelayDiagramSeries> series; ///< The trains with a call at a station of the route, in run order
	std::vector<std::string> withoutCalls;	///< The other trains, in run order
};

/// The arrival delay of each call of each train at a station of the reference route with the given index, at the
/// position of that station on the route. A call is at the station nodes of the route whose station name is the station
/// of the call (TimetableResultRow::stationId holds the name). When the route passes the station at more than one
/// position, the call takes the first that the train reaches at or after its previous call at a station of the route,
/// moving as between its last two such calls that lie apart, or before those as from its first to its last call at a
/// station that the route passes once. Otherwise it takes the first of them on the route. Empty when the route does not
/// exist or has no nodes.
std::optional<DelayDiagramData> delayDiagramData(const CompletedRunData& data, int referenceIndex);

#endif // ROUTEDIAGRAMDATA_H
