#pragma once

#include "diagrams/CompletedRunData.h"
#include "diagrams/RouteDiagramData.h"
#include "scene/PassengerDwellModel.h"

#include <optional>
#include <string>
#include <vector>

/// One contiguous segment of a train path along a reference route, coloured by the passenger load carried after a call.
struct TrainLoadSegment {
	std::string trainId;				   ///< Name of the train in the run
	int trainIndex = 0;					   ///< Position of the train in the run
	std::string routeId;				   ///< Route of the train
	std::string fromStation;			   ///< Station of the call that set this load; empty before the first call
	std::string toStation;				   ///< Station of the next call the train reached; empty after the last call
	int load = 0;						   ///< Passengers on board along this segment
	int capacity = 0;					   ///< Maximum passengers on board (Train::MAX_OnBoard_Passengers); 0 when unknown
	int boarded = 0;					   ///< Passengers who boarded at the call that started this segment
	int alighted = 0;					   ///< Passengers who alighted at the call that started this segment
	int leftBehind = 0;					   ///< Passengers refused at the call that started this segment because the train was full
	bool hasCall = false;				   ///< True when this segment follows an actual stop call
	std::vector<RouteDiagramPoint> points; ///< Projected trajectory points along this segment, in sample order

	/// Occupancy rate (load / capacity), or negative when capacity is not positive.
	double occupancyRate() const {
		return capacity > 0 ? static_cast<double>(load) / static_cast<double>(capacity) : -1.0;
	}

	/// True when capacity is positive and the occupancy rate exceeds crowdingThreshold.
	bool isCrowded(double crowdingThreshold) const {
		return capacity > 0 && occupancyRate() > crowdingThreshold;
	}
};

/// On-board passenger load of every train along a reference route, split into per-call segments.
struct TrainLoadDiagramData {
	RouteDiagramPath referencePath;													 ///< The reference route
	double crowdingThreshold = kDefaultPassengerOnboardCrowdingThreshold;			 ///< Occupancy rate above which a train counts as crowded
	bool hasPassengers = false;														 ///< True when the run simulated passengers
	std::vector<TrainLoadSegment> segments;											 ///< Segments of every train, in run and sample order
};

/// The train paths split into per-call passenger load segments on the reference route with the given index.
/// Empty when the route does not exist or has no nodes.
std::optional<TrainLoadDiagramData> trainLoadDiagramData(const CompletedRunData& data, int referenceIndex);

/// CSV text of the load segments of visible trains: train, reference route, source route, from station, to station,
/// start time, end time, load, capacity, occupancy rate, crowded flag, boarded, alighted and left behind.
/// Empty when no visible train has a segment.
std::string buildTrainLoadCsv(const TrainLoadDiagramData& diagram, const std::vector<std::string>& visibleTrainIds);
