#pragma once

#include "simulation/RollingStock.h"
#include "simulation/SimulationRun.h"

#include <cstddef>
#include <list>
#include <string>
#include <utility>
#include <vector>

/// Empties the trains, the passengers and the platforms of a run for a test of the passenger step, and puts back what they held,
/// with the passenger GUI setting and the start time, when it goes out of scope. The passenger flow, the journey planner and the
/// call counts are emptied at both ends, so that the next step of the run plans on what the test sets up.
class PassengerStepScope {
public:
	/// Saves what `run` holds and empties it.
	explicit PassengerStepScope(SimulationRun& run)
		: m_run(run), m_paxGui(run.settings.PAX_GUI), m_start(run.settings.startingSimulationTime), m_numTrains(run.numTrains),
		  m_numPassengers(run.numPassengers), m_numPlatforms(run.numPlatforms), m_passengerIndex(run.passengerIndex),
		  m_platformIndex(run.platformIndex) {
		m_trains.swap(run.trains);
		m_passengers.swap(run.passengers);
		m_platforms.swap(run.platforms);
		run.numTrains = run.numPassengers = run.numPlatforms = 0;
		clearPassengerState();
	}
	PassengerStepScope(const PassengerStepScope&) = delete;
	PassengerStepScope& operator=(const PassengerStepScope&) = delete;
	/// Puts back what the run held.
	~PassengerStepScope() {
		m_run.trains.swap(m_trains);
		m_run.passengers.swap(m_passengers);
		m_run.platforms.swap(m_platforms);
		m_run.numTrains = m_numTrains;
		m_run.numPassengers = m_numPassengers;
		m_run.numPlatforms = m_numPlatforms;
		m_run.passengerIndex = m_passengerIndex;
		m_run.platformIndex = m_platformIndex;
		m_run.settings.PAX_GUI = m_paxGui;
		m_run.settings.startingSimulationTime = m_start;
		clearPassengerState();
	}

	/// Sets the trains, passengers and platforms that the test describes, with the indexes the application builds with them.
	void set(std::vector<Train> trains, std::list<Passenger> passengers, std::list<StationPlatform> platforms) {
		m_run.trains = std::move(trains);
		m_run.numTrains = static_cast<int>(m_run.trains.size());
		m_run.passengers = std::move(passengers);
		m_run.numPassengers = static_cast<int>(m_run.passengers.size());
		m_run.platforms = std::move(platforms);
		m_run.numPlatforms = static_cast<int>(m_run.platforms.size());
		m_run.platformIndex.build(m_run.platforms);
		m_run.passengerIndex.build(m_run.passengers);
		clearPassengerState();
	}

private:
	void clearPassengerState() {
		m_run.passengerFlow.clear();
		m_run.passengerJourneys.clear();
		m_run.stopCalls.clear();
	}

	SimulationRun& m_run;
	bool m_paxGui;
	int m_start;
	int m_numTrains;
	int m_numPassengers;
	int m_numPlatforms;
	PassengerIndex m_passengerIndex;
	PlatformIndex m_platformIndex;
	std::vector<Train> m_trains;
	std::list<Passenger> m_passengers;
	std::list<StationPlatform> m_platforms;
};

/// A train that is planned to call at the stations in this order, at the platform "<station>.platform.1" of each, and has not
/// moved. A stop has no planned times, and the stops lie 1 km apart from the start of the route, so that the train halts at each.
inline Train trainCalling(const std::string& description, const std::vector<std::string>& stations) {
	Train train;
	train.trainDescription = description;
	train.numStations = static_cast<int>(stations.size());
	train.Stations.resize(stations.size());
	for (std::size_t stop = 0; stop < stations.size(); ++stop) {
		Node& node = train.Stations[stop];
		node.stationName = stations[stop];
		node.stationPlatformId = stations[stop] + ".platform.1";
		node.X = 1.0 + static_cast<double>(stop);
		node.StepStopped = 0;
		train.ScheduledArrivals[stop] = train.ScheduledDepartures[stop] = -1;
		train.stationPlatformNumber[stop] = 1;
	}
	train.Start_Node_X = 0.0;
	train.StoppedForServiceStop = false;
	return train;
}

/// Puts the train at the stop, which `steps` steps ago it reached, after it called at every earlier stop: the train stands at the
/// stop the way a run leaves it, with the stop as the current service stop.
inline void standAtStop(Train& train, int stop, int steps) {
	for (int earlier = 0; earlier < stop; ++earlier) {
		train.Stations[static_cast<std::size_t>(earlier)].StepStopped = 10;
		train.StationDepartures[earlier] = 10.0 * (earlier + 1);
	}
	Node& node = train.Stations[static_cast<std::size_t>(stop)];
	node.StepStopped = steps;
	train.StationDepartures[stop] = -1;
	train.StoppedForServiceStop = true;
	train.CurrentServiceStop = node.stationName;
	train.CurrentServiceStopPlatform = node.stationPlatformId;
	train.currentServiceStopPlatformNumber = train.stationPlatformNumber[stop];
}
