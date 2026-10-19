#ifndef COMPLETEDRUNDATA_H
#define COMPLETEDRUNDATA_H

#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/RouteDiagramCoordinates.h"
#include "diagrams/RunResults.h"
#include "util/TrajectoryUtil.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class Route;
struct SceneModel;
struct SimulationRun;

// A finished run as plain values. It holds nothing of the simulation, so a window can show and export it
// after another run has replaced the simulation state. Only captureCompletedRun reads the simulation.
//
// A sample index is a step of the run. Times are seconds from the start of the run (sample index times
// timestepSeconds) unless a name or comment says otherwise.

/// The time frame of the run.
struct CompletedRunSettings {
	double timestepSeconds = 1.0;	  ///< Length of one step in seconds
	double horizonSteps = 0.0;		  ///< Number of timesteps of the run
	long long startOffsetSeconds = 0; ///< Clock time of second 0, in seconds since midnight
	std::string caseName;			  ///< Name of the scene that was run

	/// Length of the run in seconds.
	double horizonSeconds() const { return horizonSteps * timestepSeconds; }
};

/// The samples of one train over its active range. A series holds the samples from firstSample on and ends where
/// the train's own series ended, so a sample the train did not record is absent, not zero.
struct CompletedTrajectory {
	static constexpr std::uint32_t kNoBlock = 0xFFFFFFFFu; ///< Value of block for a sample on no block

	int firstSample = 0;					 ///< Sample index of the first element of every series
	std::vector<double> position;			 ///< Head position along the route of the train, in m
	std::vector<double> speed;				 ///< m/s
	std::vector<double> power;				 ///< W
	std::vector<double> energy;				 ///< MJ
	std::vector<double> effort;				 ///< Tractive effort at the wheel, in N
	std::vector<std::uint32_t> block;		 ///< Index into CompletedRunData::blockNames, or kNoBlock
	std::vector<TrajectorySegment> segments; ///< Runs of valid position samples, as sample indices

	/// The value of `series` at a sample index, or empty when the series has no such sample.
	std::optional<double> at(const std::vector<double>& series, int sample) const {
		const long long offset = static_cast<long long>(sample) - firstSample;
		if (offset < 0 || offset >= static_cast<long long>(series.size()))
			return std::nullopt;
		return series[static_cast<std::size_t>(offset)];
	}
	/// The block index at a sample index, or kNoBlock when there is none.
	std::uint32_t blockAt(int sample) const {
		const long long offset = static_cast<long long>(sample) - firstSample;
		return offset < 0 || offset >= static_cast<long long>(block.size()) ? kNoBlock : block[static_cast<std::size_t>(offset)];
	}
};

/// One stop of a train, in service order.
struct CompletedStop {
	std::string station;					///< Name used by the arrival statistics
	std::string nodeId;						///< Scene node of the stop
	double routeX = 0.0;					///< Route abscissa of the stop node in km
	double routePositionMeters = -1.0;		///< Position on the route in m, -1 when the station is not on the route
	double scheduledArrival = 0.0;			///< Planned arrival in seconds of the run, negative when the stop has none
	double scheduledDeparture = 0.0;		///< Planned departure in seconds of the run, negative when the stop has none
	std::vector<std::string> routeBlockIds; ///< Ids of the route's block sections that hold the station

	/// Whether the station lies on the route, within the allowed block sections when any are given.
	bool onRoute(const std::vector<std::string>& allowedBlockIds = {}) const {
		if (allowedBlockIds.empty())
			return !routeBlockIds.empty();
		return std::any_of(routeBlockIds.begin(), routeBlockIds.end(), [&allowedBlockIds](const std::string& id) {
			return std::find(allowedBlockIds.begin(), allowedBlockIds.end(), id) != allowedBlockIds.end();
		});
	}
};

/// One train of the run: its identity, blocking times, stops and recorded samples.
struct CompletedTrain {
	std::string trainId;				 ///< Name of the train in the run
	std::string operatingCode;			 ///< Operating code of the service
	std::string serviceId;				 ///< Scene service that the train runs
	int serviceOccurrence = 1;			 ///< Occurrence of the service, from 1
	int routeIndex = -1;				 ///< Index into CompletedRunData::routes, or out of range
	std::string routeId;				 ///< Id of the route of the train
	double scheduledDepartureTime = 0.0; ///< Scheduled entry of the service occurrence, in seconds of the run
	/// Step before the first step in which the train moves, in seconds of the run. 0 when the blocking-time
	/// stage has not set it.
	int runStartTime = 0;
	std::vector<BlockingTimeDiagramInput> blockTimes; ///< Blocking times of the train, as Train::BlockTime holds them
	std::vector<CompletedStop> stops;				  ///< In service order
	CompletedTrajectory trajectory;					  ///< Recorded samples
};

/// One route of the run.
struct CompletedRoute {
	std::string id;						   ///< Route id
	std::string corridor;				   ///< Corridor name, empty when the route has none
	bool reversedDirection = false;		   ///< True when trains run opposite to the route definition direction
	std::vector<BlockSectionInput> blocks; ///< In route order
	RouteDiagramPath path;				   ///< Nodes of the route, with the stations of the scene
};

/// The finished run that the result windows show and export.
struct CompletedRunData {
	CompletedRunSettings settings;			   ///< Time frame of the run
	std::vector<std::string> blockNames;	   ///< Names of the blocks that the trajectories refer to
	std::vector<CompletedTrain> trains;		   ///< In run order
	std::vector<CompletedRoute> routes;		   ///< In the order of the simulation's routes
	RunResults results;						   ///< Run Results table
	std::vector<TimetableResultRow> timetable; ///< Planned and simulated times of every stop
	RunProvenance provenance;				   ///< Inputs and settings of the run, for the export sidecars

	/// True when routeIndex indexes routes.
	bool hasRoute(int routeIndex) const { return routeIndex >= 0 && routeIndex < static_cast<int>(routes.size()); }
};

/// The route as the nodes of its path, with the stations of the scene when it is given. It reads the simulation route
/// and serves the capture.
RouteDiagramPath routeDiagramPath(const Route& route, const SceneModel* scene);
/// The block sections of the route in route order, with their ends in km. It reads the simulation route and serves the
/// capture.
std::vector<BlockSectionInput> blockSectionInputsForRoute(const Route& route);

/// Copies the finished run out of the simulation. The scene gives the route nodes their station names and may be null.
/// `startOffsetSeconds` is the clock time of second 0, in seconds since midnight.
CompletedRunData captureCompletedRun(const SimulationRun& run, const SceneModel* scene, long long startOffsetSeconds,
	const RunProvenance& provenance);

#endif // COMPLETEDRUNDATA_H
