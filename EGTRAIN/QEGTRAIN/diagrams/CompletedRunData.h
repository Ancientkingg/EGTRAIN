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

struct SceneModel;

// A finished run as plain values. It holds nothing of the simulation, so a window can show and export it
// after another run has replaced the simulation state. Only captureCompletedRun reads the simulation.

struct CompletedRunSettings {
	double timestepSeconds = 1.0;
	double horizonSteps = 0.0;		  // Number of timesteps of the run
	long long startOffsetSeconds = 0; // Clock time of second 0, in seconds since midnight
	std::string caseName;

	double horizonSeconds() const { return horizonSteps * timestepSeconds; }
};

// The samples of one train over its active range. A series holds the samples from firstSample on and ends where
// the train's own series ended, so a sample the train did not record is absent, not zero.
struct CompletedTrajectory {
	static constexpr std::uint32_t kNoBlock = 0xFFFFFFFFu;

	int firstSample = 0;
	std::vector<double> position;			 // m
	std::vector<double> speed;				 // m/s
	std::vector<double> power;				 // W
	std::vector<double> energy;				 // MJ
	std::vector<double> effort;				 // N
	std::vector<std::uint32_t> block;		 // Index into CompletedRunData::blockNames, or kNoBlock
	std::vector<TrajectorySegment> segments; // Runs of valid position samples, as sample indices

	std::optional<double> at(const std::vector<double>& series, int sample) const {
		const long long offset = static_cast<long long>(sample) - firstSample;
		if (offset < 0 || offset >= static_cast<long long>(series.size()))
			return std::nullopt;
		return series[static_cast<std::size_t>(offset)];
	}
	std::uint32_t blockAt(int sample) const {
		const long long offset = static_cast<long long>(sample) - firstSample;
		return offset < 0 || offset >= static_cast<long long>(block.size()) ? kNoBlock : block[static_cast<std::size_t>(offset)];
	}
};

// One stop of a train, in service order.
struct CompletedStop {
	std::string station;					// Name used by the arrival statistics
	std::string nodeId;						// Scene node of the stop
	double routeX = 0.0;					// Route abscissa of the stop node in km
	double routePositionMeters = -1.0;		// Position on the route, -1 when the station is not on the route
	double scheduledArrival = 0.0;			// Negative when the stop has none
	double scheduledDeparture = 0.0;		// Negative when the stop has none
	std::vector<std::string> routeBlockIds; // Ids of the route's block sections that hold the station

	// Whether the station lies on the route, within the allowed block sections when any are given.
	bool onRoute(const std::vector<std::string>& allowedBlockIds = {}) const {
		if (allowedBlockIds.empty())
			return !routeBlockIds.empty();
		return std::any_of(routeBlockIds.begin(), routeBlockIds.end(), [&allowedBlockIds](const std::string& id) {
			return std::find(allowedBlockIds.begin(), allowedBlockIds.end(), id) != allowedBlockIds.end();
		});
	}
};

struct CompletedTrain {
	std::string trainId;
	std::string operatingCode;
	std::string serviceId;
	int serviceOccurrence = 1;
	int routeIndex = -1; // Index into CompletedRunData::routes, or out of range
	std::string routeId;
	double scheduledDepartureTime = 0.0;
	int runStartTime = 0;
	std::vector<BlockingTimeDiagramInput> blockTimes;
	std::vector<CompletedStop> stops;
	CompletedTrajectory trajectory;
};

struct CompletedRoute {
	std::string id;
	std::string corridor;
	std::vector<BlockSectionInput> blocks; // In route order
	RouteDiagramPath path;
};

struct CompletedRunData {
	CompletedRunSettings settings;
	std::vector<std::string> blockNames; // Names of the blocks that the trajectories refer to
	std::vector<CompletedTrain> trains;	 // In run order
	std::vector<CompletedRoute> routes;	 // In the order of the simulation's routes
	RunResults results;
	std::vector<TimetableResultRow> timetable;
	RunProvenance provenance;

	bool hasRoute(int routeIndex) const { return routeIndex >= 0 && routeIndex < static_cast<int>(routes.size()); }
};

// Copies the finished run out of the simulation globals. The scene gives the route nodes their station names and
// may be null.
CompletedRunData captureCompletedRun(const SceneModel* scene, long long startOffsetSeconds,
	const RunProvenance& provenance);

#endif // COMPLETEDRUNDATA_H
