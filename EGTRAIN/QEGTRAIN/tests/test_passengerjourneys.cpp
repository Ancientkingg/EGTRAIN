// The train state that passenger journeys are planned on (simulation/PassengerJourneys.h): how far a train has come in its stops, and
// the delay it is known to have.

#include "simulation/PassengerFlow.h"
#include "simulation/PassengerJourneys.h"
#include "simulation/RollingStock.h"
#include "simulation/SimulationRun.h"

#include <QTest>

#include <limits>
#include <list>
#include <string>
#include <vector>

namespace {

// A train with three stops that has not moved. Planned arrivals are 100, 200 and 300 s and planned departures 130, 230 and none.
Train threeStops() {
	Train train;
	train.numStations = 3;
	train.Stations.resize(3);
	for (int stop = 0; stop < 3; ++stop) {
		train.Stations[static_cast<std::size_t>(stop)].StepStopped = 0;
		train.StationArrivals[stop] = -1;
		train.ScheduledArrivals[stop] = 100.0 * (stop + 1);
		train.ScheduledDepartures[stop] = stop < 2 ? 100.0 * (stop + 1) + 30 : -1;
	}
	train.StoppedForServiceStop = false;
	train.OutOfSimulation = false;
	return train;
}

// liveTrainProgress of the train in a run without incidents that starts at 00:00:00.
passenger_routing::TrainProgress progressOf(const Train& train, int firstStop, int elapsed) {
	const SimulationRun run;
	return liveTrainProgress(run, train, firstStop, elapsed);
}

// The train stands at the stop, having arrived at the time.
void standAt(Train& train, int stop, double arrival) {
	for (int earlier = 0; earlier < stop; ++earlier)
		train.Stations[static_cast<std::size_t>(earlier)].StepStopped = 10;
	train.Stations[static_cast<std::size_t>(stop)].StepStopped = 1;
	train.StationArrivals[stop] = arrival;
	train.StoppedForServiceStop = true;
}

// A train that stops at A, B and C, planned to leave A at 10 s, B at base + 100 s and arrive at C at base + 200 s.
Train namedTrain(const std::string& description, int base) {
	Train train = threeStops();
	train.trainDescription = description;
	const char* names[] = {"A", "B", "C"};
	for (int stop = 0; stop < 3; ++stop) {
		Node& node = train.Stations[static_cast<std::size_t>(stop)];
		node.stationName = names[stop];
		node.stationPlatformId = std::string(names[stop]) + ".platform.1";
		node.X = 1.0 + stop;
		train.ScheduledArrivals[stop] = stop == 0 ? -1 : base + 100.0 * stop;
		train.ScheduledDepartures[stop] = stop == 2 ? -1 : base + 100.0 * stop - 90;
	}
	train.Start_Node_X = 0.0;
	return train;
}

// A run in which T1 stopped at A and B and left its route there, with one passenger on board for C who left A on T1. With a second
// train T2, planned to call at B and C later, the passenger can continue on it.
struct EndedTrainRun {
	SimulationRun run;
	PassengerFlow flow;

	explicit EndedTrainRun(bool withSecondTrain) {
		run.settings.startingSimulationTime = 0;
		run.trains.push_back(namedTrain("T1", 0));
		if (withSecondTrain)
			run.trains.push_back(namedTrain("T2", 200));
		run.numTrains = static_cast<int>(run.trains.size());
		Train& ended = run.trains[0];
		ended.Stations[0].StepStopped = 10;
		ended.Stations[1].StepStopped = 10;
		ended.StationArrivals[0] = 5;
		ended.StationArrivals[1] = 100;
		ended.OutOfSimulation = true;
		ended.Current_OnBoard_Passengers = 1;

		Trip trip;
		trip.TripID = "j.1";
		trip.JourneyID = "j";
		trip.Dep_Station_ID = "A";
		trip.Arr_Station_ID = "C";
		trip.Dep_Station_Platform_ID = "A.platform.1";
		trip.Arr_Station_Platform_ID = "C.platform.1";
		trip.TrainServiceDescription = "T1";
		trip.Planned_Arrival_Time = 200;
		trip.IsTripStarted = true;
		Journey journey;
		journey.ID = "j";
		journey.Dep_Station_ID = "A";
		journey.Arr_Station_ID = "C";
		journey.Trips.push_back(trip);
		journey.N_Trips = 1;
		journey.IsJourneyStarted = true;
		Passenger passenger;
		passenger.ID = "p";
		passenger.Journeys.push_back(journey);
		passenger.IsIntheNetwork = true;
		passenger.CurrentStatus = PassengerStatus::OnBoard;
		passenger.current_JourneyID = "j";
		passenger.current_TripID = "j.1";
		passenger.Current_Train_Boarded = "T1";
		passenger.Current_Arrival_Station = "C";
		run.passengers.push_back(passenger);
		flow.build(run.passengers);
	}

	const Passenger& passenger() const { return run.passengers.front(); }
};

// A train that stops at B and then at C. At B it uses the platform with this number, is planned to arrive at `arrival` and to
// leave 20 s later; it is planned at C 140 s after that.
Train trainFromB(const std::string& description, int platformNumber, double arrival) {
	Train train = threeStops();
	train.trainDescription = description;
	train.numStations = 2;
	train.Stations.resize(2);
	const char* names[] = {"B", "C"};
	for (int stop = 0; stop < 2; ++stop) {
		Node& node = train.Stations[static_cast<std::size_t>(stop)];
		node.stationName = names[stop];
		node.stationPlatformId = stop == 0 ? "B.platform." + std::to_string(platformNumber) : std::string("C.platform.1");
		node.X = 2.0 + stop;
		train.stationPlatformNumber[stop] = stop == 0 ? platformNumber : 1;
		train.StationDepartures[stop] = -1;
	}
	train.ScheduledArrivals[0] = arrival;
	train.ScheduledDepartures[0] = arrival + 20;
	train.ScheduledArrivals[1] = arrival + 160;
	train.ScheduledDepartures[1] = -1;
	train.Start_Node_X = 0.0;
	return train;
}

// A run at step 500 of a day that starts at 00:00:00, at station B:
// - T1 from A stands at platform 2, with passenger `rider` on board who changes there to T2 for C;
// - T2 called at platform 8 and has left, and passenger `waiter` has stood on platform 8 for it since 00:00:00;
// - T3 is planned to leave platform 2 at 560 s and T4 at 920 s, both for C.
// Platforms 2 and 8 are a walk of 180 s apart.
struct MissedChangeRun {
	SimulationRun run;
	static constexpr int kStep = 500;

	MissedChangeRun() {
		run.settings.startingSimulationTime = 0;
		Train arriving = namedTrain("T1", 400);
		arriving.numStations = 2;
		arriving.Stations.resize(2);
		arriving.Stations[1].stationPlatformId = "B.platform.2";
		arriving.stationPlatformNumber[1] = 2;
		arriving.Stations[0].StepStopped = 10;
		arriving.Stations[1].StepStopped = 2;
		arriving.StationArrivals[0] = 5;
		arriving.StationDepartures[0] = 310;
		arriving.StationArrivals[1] = 499;
		arriving.StationDepartures[1] = -1;
		arriving.StoppedForServiceStop = true;
		arriving.CurrentServiceStop = "B";
		arriving.CurrentServiceStopPlatform = "B.platform.2";
		arriving.currentServiceStopPlatformNumber = 2;
		arriving.MAX_OnBoard_Passengers = 100;
		arriving.Current_OnBoard_Passengers = 1;
		run.trains.push_back(arriving);
		Train gone = trainFromB("T2", 8, 380);
		gone.Stations[0].StepStopped = 10;
		gone.StationArrivals[0] = 380;
		gone.StationDepartures[0] = 400;
		run.trains.push_back(gone);
		run.trains.push_back(trainFromB("T3", 2, 540));
		run.trains.push_back(trainFromB("T4", 2, 900));
		run.numTrains = static_cast<int>(run.trains.size());

		const auto trip = [](const std::string& id, const std::string& journey, const std::string& from, const std::string& to,
							  const std::string& train) {
			Trip leg;
			leg.TripID = id;
			leg.JourneyID = journey;
			leg.Dep_Station_ID = from;
			leg.Arr_Station_ID = to;
			leg.TrainServiceDescription = train;
			return leg;
		};
		Trip toB = trip("r.1", "r", "A", "B", "T1");
		toB.Arr_Station_Platform_ID = "B.platform.2";
		toB.IsTripStarted = true;
		toB.Actual_Departure_Time = 250;
		toB.Actual_Boarding_Time = 300;
		Trip onward = trip("r.2", "r", "B", "C", "T2");
		onward.Dep_Station_Platform_ID = "B.platform.8";
		onward.depPlatformNumber = 8;
		Journey riding;
		riding.ID = "r";
		riding.Dep_Station_ID = "A";
		riding.Arr_Station_ID = "C";
		riding.Trips = {toB, onward};
		riding.N_Trips = 2;
		riding.IsJourneyStarted = true;
		Passenger rider;
		rider.ID = "rider";
		rider.Journeys.push_back(riding);
		rider.IsIntheNetwork = true;
		rider.CurrentStatus = PassengerStatus::OnBoard;
		rider.current_JourneyID = "r";
		rider.current_TripID = "r.1";
		rider.Current_Train_Boarded = "T1";
		rider.Current_Arrival_Station = "B";
		run.passengers.push_back(rider);

		Trip missed = trip("w.1", "w", "B", "C", "T2");
		missed.Dep_Station_Platform_ID = "B.platform.8";
		missed.depPlatformNumber = 8;
		missed.IsTripStarted = true;
		missed.Actual_Departure_Time = 0;
		Journey waiting;
		waiting.ID = "w";
		waiting.Dep_Station_ID = "B";
		waiting.Arr_Station_ID = "C";
		waiting.Trips = {missed};
		waiting.N_Trips = 1;
		waiting.IsJourneyStarted = true;
		Passenger waiter;
		waiter.ID = "waiter";
		waiter.Journeys.push_back(waiting);
		waiter.IsIntheNetwork = true;
		waiter.CurrentStatus = PassengerStatus::OnPlatform;
		waiter.current_JourneyID = "w";
		waiter.current_TripID = "w.1";
		waiter.current_location_ID = waiter.Current_WaitingStationID = "B";
		waiter.Current_WaitingStationPlatformID = "B.platform.8";
		waiter.Current_Train_To_Wait = "T2";
		waiter.Current_Arrival_Station = "C";
		run.passengers.push_back(waiter);
		run.numPassengers = 2;

		simulateTrainPassengerInteractions(run, kStep);
	}

	const Passenger& passenger(const char* id) const {
		return id == std::string("rider") ? run.passengers.front() : run.passengers.back();
	}
};

// A run at step 500 at station B: T1 from A stands at platform 2 with passenger `rider` on board, who changes there to T4 for C
// (planned to arrive at C at 1060). T3 also stands at platform 2, planned to leave at 510, and reaches C at `thirdAtC`. With
// t1GoesOn, T1 goes on to C at 600 and T3 does not stand there.
struct StandingTrainRun {
	SimulationRun run;

	explicit StandingTrainRun(double thirdAtC, bool t1GoesOn = false) {
		run.settings.startingSimulationTime = 0;
		Train arriving = namedTrain("T1", 400);
		if (!t1GoesOn) {
			arriving.numStations = 2;
			arriving.Stations.resize(2);
		}
		arriving.Stations[1].stationPlatformId = "B.platform.2";
		arriving.stationPlatformNumber[1] = 2;
		arriving.Stations[0].StepStopped = 10;
		arriving.Stations[1].StepStopped = 2;
		arriving.StationArrivals[0] = 5;
		arriving.StationDepartures[0] = 310;
		arriving.StationArrivals[1] = 499;
		arriving.StationDepartures[1] = -1;
		arriving.StoppedForServiceStop = true;
		arriving.CurrentServiceStop = "B";
		arriving.CurrentServiceStopPlatform = "B.platform.2";
		arriving.currentServiceStopPlatformNumber = 2;
		arriving.MAX_OnBoard_Passengers = 100;
		arriving.Current_OnBoard_Passengers = 1;
		run.trains.push_back(arriving);
		Train standing = trainFromB("T3", 2, thirdAtC - 160);
		standing.ScheduledDepartures[0] = 510;
		if (!t1GoesOn) {
			standing.Stations[0].StepStopped = 1;
			standing.StationArrivals[0] = 490;
			standing.StoppedForServiceStop = true;
			standing.CurrentServiceStop = "B";
			standing.CurrentServiceStopPlatform = "B.platform.2";
			standing.currentServiceStopPlatformNumber = 2;
			standing.MAX_OnBoard_Passengers = 100;
		}
		run.trains.push_back(standing);
		run.trains.push_back(trainFromB("T4", 2, 900));
		run.numTrains = static_cast<int>(run.trains.size());

		Trip toB;
		toB.TripID = "r.1";
		toB.JourneyID = "r";
		toB.Dep_Station_ID = "A";
		toB.Arr_Station_ID = "B";
		toB.Arr_Station_Platform_ID = "B.platform.2";
		toB.TrainServiceDescription = "T1";
		toB.IsTripStarted = true;
		toB.Actual_Departure_Time = 250;
		toB.Actual_Boarding_Time = 300;
		Trip onward;
		onward.TripID = "r.2";
		onward.JourneyID = "r";
		onward.Dep_Station_ID = "B";
		onward.Arr_Station_ID = "C";
		onward.Dep_Station_Platform_ID = "B.platform.2";
		onward.depPlatformNumber = 2;
		onward.TrainServiceDescription = "T4";
		onward.Planned_Arrival_Time = 1060;
		Journey riding;
		riding.ID = "r";
		riding.Dep_Station_ID = "A";
		riding.Arr_Station_ID = "C";
		riding.Trips = {toB, onward};
		riding.N_Trips = 2;
		riding.IsJourneyStarted = true;
		Passenger rider;
		rider.ID = "rider";
		rider.Journeys.push_back(riding);
		rider.IsIntheNetwork = true;
		rider.CurrentStatus = PassengerStatus::OnBoard;
		rider.current_JourneyID = "r";
		rider.current_TripID = "r.1";
		rider.Current_Train_Boarded = "T1";
		rider.Current_Arrival_Station = "B";
		run.passengers.push_back(rider);
		run.numPassengers = 1;

		simulateTrainPassengerInteractions(run, 500);
	}

	const Passenger& rider() const { return run.passengers.front(); }
};

// A run at step 500 at station B: passenger `waiter` has stood on platform 2 since 400 for T4, planned to reach C at 1060. T3 arrives
// at platform 2 in this step (its first step there) and reaches C at `thirdAtC`, with no room when `full`.
struct ArrivingTrainRun {
	SimulationRun run;

	explicit ArrivingTrainRun(double thirdAtC, bool full = false) {
		run.settings.startingSimulationTime = 0;
		const auto standAtB = [](Train& train, int arrival) {
			train.Stations[0].StepStopped = 1;
			train.StationArrivals[0] = arrival;
			train.StoppedForServiceStop = true;
			train.CurrentServiceStop = "B";
			train.CurrentServiceStopPlatform = "B.platform.2";
			train.currentServiceStopPlatformNumber = 2;
			train.MAX_OnBoard_Passengers = 100;
		};
		Train arriving = trainFromB("T3", 2, thirdAtC - 160);
		arriving.ScheduledDepartures[0] = 510;
		standAtB(arriving, 499);
		arriving.Current_OnBoard_Passengers = full ? 100 : 0;
		run.trains.push_back(arriving);
		run.trains.push_back(trainFromB("T4", 2, 900));
		run.numTrains = static_cast<int>(run.trains.size());

		Trip onward;
		onward.TripID = "w.1";
		onward.JourneyID = "w";
		onward.Dep_Station_ID = "B";
		onward.Arr_Station_ID = "C";
		onward.Dep_Station_Platform_ID = "B.platform.2";
		onward.depPlatformNumber = 2;
		onward.TrainServiceDescription = "T4";
		onward.Planned_Arrival_Time = 1060;
		onward.IsTripStarted = true;
		onward.Actual_Departure_Time = 400;
		Journey waiting;
		waiting.ID = "w";
		waiting.Dep_Station_ID = "B";
		waiting.Arr_Station_ID = "C";
		waiting.Trips = {onward};
		waiting.N_Trips = 1;
		waiting.IsJourneyStarted = true;
		Passenger waiter;
		waiter.ID = "waiter";
		waiter.Journeys.push_back(waiting);
		waiter.IsIntheNetwork = true;
		waiter.CurrentStatus = PassengerStatus::OnPlatform;
		waiter.current_JourneyID = "w";
		waiter.current_TripID = "w.1";
		waiter.current_location_ID = waiter.Current_WaitingStationID = "B";
		waiter.Current_WaitingStationPlatformID = "B.platform.2";
		waiter.Current_Train_To_Wait = "T4";
		waiter.Current_Arrival_Station = "C";
		run.passengers.push_back(waiter);
		run.numPassengers = 1;

		simulateTrainPassengerInteractions(run, 500);
	}

	const Passenger& waiter() const { return run.passengers.front(); }
};

// A run at station B that starts at 00:00:00. Passenger `waiter` has stood on B.platform.1 since 300 for T1, which is planned at B at
// 400 and at C at 1000 but has not come yet. T2 is planned at B at secondAtB, leaves 20 s later and is at C 140 s after that. Both
// trains call at B.platform.1.
struct LateTrainRun {
	SimulationRun run;
	PassengerFlow flow;

	explicit LateTrainRun(double secondAtB) {
		run.settings.startingSimulationTime = 0;
		Train late = trainFromB("T1", 1, 400);
		late.ScheduledArrivals[1] = 1000;
		run.trains.push_back(late);
		run.trains.push_back(trainFromB("T2", 1, secondAtB));
		run.numTrains = static_cast<int>(run.trains.size());

		Trip trip;
		trip.TripID = "w.1";
		trip.JourneyID = "w";
		trip.Dep_Station_ID = "B";
		trip.Arr_Station_ID = "C";
		trip.Dep_Station_Platform_ID = "B.platform.1";
		trip.Arr_Station_Platform_ID = "C.platform.1";
		trip.depPlatformNumber = 1;
		trip.TrainServiceDescription = "T1";
		trip.Planned_Departure_Time = 420;
		trip.Planned_Arrival_Time = 1000;
		trip.IsTripStarted = true;
		trip.Actual_Departure_Time = 300;
		Journey journey;
		journey.ID = "w";
		journey.Mode = JourneyMode::Live;
		journey.Dep_Station_ID = "B";
		journey.Arr_Station_ID = "C";
		journey.Trips = {trip};
		journey.N_Trips = 1;
		journey.IsJourneyStarted = true;
		journey.Waitingtime = 0;
		journey.Walkingtime = 0;
		Passenger waiter;
		waiter.ID = "waiter";
		waiter.Journeys.push_back(journey);
		waiter.IsIntheNetwork = true;
		waiter.CurrentStatus = PassengerStatus::OnPlatform;
		waiter.current_JourneyID = "w";
		waiter.current_TripID = "w.1";
		waiter.current_location_ID = waiter.Current_WaitingStationID = "B";
		waiter.Current_WaitingStationPlatformID = "B.platform.1";
		waiter.Current_Train_To_Wait = "T1";
		waiter.Current_Arrival_Station = "C";
		run.passengers.push_back(waiter);
		run.numPassengers = 1;
		flow.build(run.passengers);
	}

	void settle(int t) { run.passengerJourneys.settleTrains(run, flow, t); }
	const Passenger& waiter() const { return run.passengers.front(); }
	const Journey& journey() const { return waiter().Journeys.front(); }
};

// A train that calls at the stations in order, at platform 1 of each and 1 km apart from 2 km on. It is planned to arrive at each
// at the time given and to leave 20 s later, and has no departure at its last stop.
Train trainCallingAt(const std::string& description, const std::vector<std::string>& stations, const std::vector<double>& arrivals) {
	Train train = threeStops();
	train.trainDescription = description;
	const int count = static_cast<int>(stations.size());
	train.numStations = count;
	train.Stations.resize(stations.size());
	for (int stop = 0; stop < count; ++stop) {
		const std::size_t at = static_cast<std::size_t>(stop);
		Node& node = train.Stations[at];
		node.StepStopped = 0;
		node.stationName = stations[at];
		node.stationPlatformId = stations[at] + ".platform.1";
		node.X = 2.0 + stop;
		train.stationPlatformNumber[stop] = 1;
		train.StationArrivals[stop] = -1;
		train.ScheduledArrivals[stop] = arrivals[at];
		train.ScheduledDepartures[stop] = stop + 1 < count ? arrivals[at] + 20 : -1;
	}
	train.Start_Node_X = 0.0;
	return train;
}

// A trip on the train from one station to another, at platform 1 of each, planned to arrive at plannedArrival.
Trip tripOn(const std::string& id, const std::string& train, const std::string& from, const std::string& to, int plannedArrival) {
	Trip trip;
	trip.TripID = id;
	trip.Dep_Station_ID = from;
	trip.Arr_Station_ID = to;
	trip.Dep_Station_Platform_ID = from + ".platform.1";
	trip.Arr_Station_Platform_ID = to + ".platform.1";
	trip.depPlatformNumber = 1;
	trip.TrainServiceDescription = train;
	trip.Planned_Arrival_Time = plannedArrival;
	return trip;
}

// A run with these trains that starts at 00:00:00, with passenger `waiter` on a live journey: it rode the trips before `current`
// and has waited for the train of trip `current` on its platform since `since`.
struct WaitingRun {
	SimulationRun run;
	PassengerFlow flow;

	WaitingRun(const std::vector<Train>& trains, const std::vector<Trip>& trips, std::size_t current, int since) {
		run.settings.startingSimulationTime = 0;
		for (const Train& train : trains)
			run.trains.push_back(train);
		run.numTrains = static_cast<int>(run.trains.size());
		addPassenger("waiter", trips, current, since);
	}

	// Adds a passenger like `waiter`, after the passengers there are.
	void addPassenger(const std::string& id, std::vector<Trip> trips, std::size_t current, int since) {
		Journey journey;
		journey.ID = id + ".j";
		journey.Mode = JourneyMode::Live;
		journey.Dep_Station_ID = trips.front().Dep_Station_ID;
		journey.Arr_Station_ID = trips.back().Arr_Station_ID;
		for (std::size_t n = 0; n < trips.size(); ++n) {
			Trip& trip = trips[n];
			trip.JourneyID = journey.ID;
			trip.IsTripStarted = n <= current;
			trip.IsTripCompleted = n < current;
			trip.Actual_Departure_Time = n == current ? since : -9999;
			journey.Trips.push_back(trip);
		}
		journey.N_Trips = static_cast<int>(trips.size());
		journey.IsJourneyStarted = true;
		journey.Waitingtime = 0;
		journey.Walkingtime = 0;
		const Trip& waitFor = trips[current];
		Passenger passenger;
		passenger.ID = id;
		passenger.Journeys.push_back(journey);
		passenger.IsIntheNetwork = true;
		passenger.CurrentStatus = PassengerStatus::OnPlatform;
		passenger.current_JourneyID = journey.ID;
		passenger.current_TripID = waitFor.TripID;
		passenger.current_location_ID = passenger.Current_WaitingStationID = waitFor.Dep_Station_ID;
		passenger.Current_WaitingStationPlatformID = waitFor.Dep_Station_Platform_ID;
		passenger.Current_Train_To_Wait = waitFor.TrainServiceDescription;
		passenger.Current_Arrival_Station = waitFor.Arr_Station_ID;
		run.passengers.push_back(passenger);
		run.numPassengers = static_cast<int>(run.passengers.size());
		flow.build(run.passengers);
	}

	void settle(int t) { run.passengerJourneys.settleTrains(run, flow, t); }
	const Journey& journey() const { return run.passengers.front().Journeys.front(); }
	// The trains of the trips of the journey of `waiter`, in order.
	std::vector<std::string> trains() const {
		std::vector<std::string> names;
		for (const Trip& trip : journey().Trips)
			names.push_back(trip.TrainServiceDescription);
		return names;
	}
};

// A run whose route 0 is one section of three arcs: 0 to 1,000 m with a track speed limit of 10 m/s, 1,000 to 3,000 m with 30 m/s
// and 3,000 to 4,000 m with 40 m/s. Its train S-1 (threeStops) runs on it at up to 50 m/s and stops at 0, 1 and 3 km. With
// `capped`, a breakdown caps S-1 at 72 km/h (20 m/s) from 50 s to 1,000 s.
struct LimitedRun {
	SimulationRun run;

	explicit LimitedRun(bool capped = true) {
		Section section;
		const double ends[] = {0.0, 1.0, 3.0, 4.0};
		const double speeds[] = {10.0, 30.0, 40.0};
		for (int a = 0; a < 3; ++a) {
			Arc arc;
			arc.startNode.X = ends[a];
			arc.endNode.X = ends[a + 1];
			arc.speedLimit = speeds[a];
			section.arcs_in_signalling_block_section.push_back(arc);
		}
		section.total_arcs = 3;
		Route route;
		route.sequence_of_block_sections.push_back(section);
		route.N_Block_Sections = 1;
		run.infra.routes.push_back(route);
		run.infra.numRoutes = 1;
		Train train = threeStops();
		train.trainDescription = "S-1";
		train.indexOfRoute = 0;
		train.Start_Node_X = 0.0;
		train.max_train_speed = 50.0;
		for (int stop = 0; stop < 3; ++stop)
			train.Stations[static_cast<std::size_t>(stop)].X = stop == 2 ? 3.0 : stop;
		run.trains.push_back(train);
		run.numTrains = 1;
		if (capped) {
			SimulationIncident cap;
			cap.type = IncidentType::TrainBreakdown;
			cap.target = "S";
			cap.startSeconds = 50.0;
			cap.hasReducedSpeed = true;
			cap.reducedSpeedKmh = 72.0;
			cap.hasEndSeconds = true;
			cap.endSeconds = 1000.0;
			run.incidents.push_back(cap);
		}
	}

	Train& train() { return run.trains.front(); }

	// S-1 gets two more stops: it stops at 0 and 1 km, at a stop off its route, and at 3 and 4 km.
	void addStopOffTheRoute() {
		Train& longer = train();
		longer.numStations = 5;
		longer.Stations.resize(5);
		for (int stop = 0; stop < 5; ++stop) {
			Node& node = longer.Stations[static_cast<std::size_t>(stop)];
			node.X = stop;
			node.StepStopped = 0;
			longer.stationBlockSection[stop] = stop == 2 ? -1 : 0;
			longer.StationArrivals[stop] = -1;
			longer.ScheduledArrivals[stop] = 100.0 * (stop + 1);
			longer.ScheduledDepartures[stop] = stop < 4 ? 100.0 * (stop + 1) + 30 : -1;
		}
	}

	// S-1 left its first stop on time and runs with its head at 500 m at 150 s, the step before 151.
	void runAt500Metres() {
		Train& moving = train();
		moving.Stations[0].StepStopped = 10;
		moving.StationArrivals[0] = 100.0;
		moving.CanEnter = true;
		moving.earliestActiveTrajectoryIndex = 90;
		moving.instant_spatial_position.assign(200, 0.0);
		moving.instant_spatial_position[150] = 500.0;
	}
};

} // namespace

class TestPassengerJourneys : public QObject {
	Q_OBJECT

private slots:
	void trainBeforeItsFirstStopCanBeBoardedThere();
	void trainStandingAtAStopIsAtThatStop();
	void trainThatLeftAStopIsAtTheNext();
	void stopThatWasPassedIsLeftBehind();
	void trainThatLeftItsRouteHasNoStopLeft();
	void delayIsThatOfTheLatestArrival();
	void delayIsNeverNegative();
	void trainThatEnteredLateIsLateBeforeItsFirstArrival();
	void trainThatStandsPastItsDepartureIsAsLateAsItStands();
	void trainDoesNotHaltAtTheStopWhereItEnters();
	void limitedRunTakesTheLowerSpeedOfEachArc();
	void breakdownSpeedCapLimitsTheTrain();
	void trainWithoutSpeedCapIsNotLimited();
	void capThatEndsBeforeTheTrainSetsOffDoesNotDelayIt();
	void capThatLastsLongerTakesOver();
	void runsPassOverStopsOffTheRoute();
	void departureDelayCountsOnceTheTrainLeft();
	void timeMadeUpAtAStopCounts();
	void capWithAVeryLateEndIsOnePeriod();
	void capThatStartsLaterDoesNotHideOneThatAppliesNow();
	void capsThatEndWithinOneSecondKeepTheirPeriods();
	void trainBetweenStopsIsAsLateAsItIsOverdue();
	void trainHeldAtTheStartOfItsDwellIsNotOverdue();
	void trainThatEndsEarlySetsDownPassengerWhoFindsNoRoute();
	void trainThatEndsEarlySetsDownPassengerWhoContinuesOnAnother();
	void trainThatEndsEarlyCountsThePassengersItSetsDown();
	void passengerWhoAlightsAfterTheChangeLeftPlansFromTheArrivalPlatform();
	void passengerWhoWaitedSinceMidnightKeepsTheWaitAndThePlatform();
	void passengerBoardsStandingTrainThatGoesDirectInTime();
	void passengerKeepsPlanWhenStandingTrainArrivesLater();
	void passengerDoesNotBoardTheTrainItLeft();
	void waitingPassengerSwitchesToTrainThatArrivesAndGoesDirect();
	void waitingPassengerKeepsPlanWhenTheDirectTrainArrivesLater();
	void waitingPassengerIgnoresFullStandingTrain();
	void passengerLeavesLateTrainForAnEarlierOne();
	void passengerKeepsLateTrainWithinTheMargin();
	void passengerKeepsTrainLateUpToTheThreshold();
	void passengerPlansAgainOnlyWhenThePredictionChanges();
	void passengerWhoComesToWaitForALateTrainTakesAnEarlierOne();
	void passengerWhoKeepsTheTrainKeepsThePlaceInLine();
	void newPlanCountsTheTrainsTheJourneyRode();
	void passengerKeepsPlanThatMakesALaterCallOfTheTrain();
	void passengerKeepsTrainThatStopsWithoutAPlannedDeparture();
	void extremePredictionCountsAsTheLargestLateness();
	void heldTrainHasANewPredictionOnceAMinute();
	void cappedTrainHeldBetweenStopsHasANewPredictionOnceAMinute();
	void activityTextIsParsedCaseInsensitively();
	void statusNamesAreTheLegacyText();
};

void TestPassengerJourneys::trainBeforeItsFirstStopCanBeBoardedThere() {
	const Train train = threeStops();
	QCOMPARE(advanceFirstStop(train, 0), 0);
	const passenger_routing::TrainProgress state = progressOf(train, 0, 0);
	QCOMPARE(state.firstStop, std::size_t(0));
	QCOMPARE(state.delay, 0.0);
}

void TestPassengerJourneys::trainStandingAtAStopIsAtThatStop() {
	Train train = threeStops();
	standAt(train, 1, 210.0);
	QCOMPARE(advanceFirstStop(train, 0), 1);
	// A later call with the value of the earlier one gives the same stop.
	QCOMPARE(advanceFirstStop(train, 1), 1);
}

void TestPassengerJourneys::trainThatLeftAStopIsAtTheNext() {
	Train train = threeStops();
	standAt(train, 1, 210.0);
	train.StoppedForServiceStop = false;
	QCOMPARE(advanceFirstStop(train, 1), 2);
}

void TestPassengerJourneys::stopThatWasPassedIsLeftBehind() {
	Train train = threeStops();
	// The train never stood at stop 0, and stands at stop 1.
	train.Stations[1].StepStopped = 1;
	train.StationArrivals[0] = 90.0;
	train.StationArrivals[1] = 210.0;
	train.StoppedForServiceStop = true;
	QCOMPARE(advanceFirstStop(train, 0), 1);
}

void TestPassengerJourneys::trainThatLeftItsRouteHasNoStopLeft() {
	Train train = threeStops();
	standAt(train, 1, 210.0);
	train.OutOfSimulation = true;
	QCOMPARE(advanceFirstStop(train, 0), 3);
	QCOMPARE(progressOf(train, 3, 400).firstStop, std::size_t(3));
}

void TestPassengerJourneys::delayIsThatOfTheLatestArrival() {
	Train train = threeStops();
	train.StationArrivals[0] = 160.0; // 60 s late at the first stop
	train.StationArrivals[1] = 245.0; // 45 s late at the second
	train.Stations[0].StepStopped = 10;
	train.Stations[1].StepStopped = 10;
	QCOMPARE(progressOf(train, 2, 250).delay, 45.0);
	// Before it arrived at the second stop, the delay is that of the first.
	train.StationArrivals[1] = -1;
	QCOMPARE(progressOf(train, 1, 170).delay, 60.0);
}

void TestPassengerJourneys::delayIsNeverNegative() {
	Train train = threeStops();
	train.StationArrivals[0] = 70.0; // 30 s early
	QCOMPARE(progressOf(train, 1, 80).delay, 0.0);
}

// Before the first recorded arrival, a train that entered later than it may enter is as late as its entry. A train that has not
// entered keeps its planned times, and an early entry is no negative delay. Once it arrived, the delay is that of the arrival. Its
// stops are due at 500, 600 and 700 s, so that none is overdue here.
void TestPassengerJourneys::trainThatEnteredLateIsLateBeforeItsFirstArrival() {
	Train train = threeStops();
	for (int stop = 0; stop < 3; ++stop) {
		train.ScheduledArrivals[stop] = 500.0 + 100.0 * stop;
		train.ScheduledDepartures[stop] = stop < 2 ? 530.0 + 100.0 * stop : -1;
	}
	train.departure_time = 50.0;
	QCOMPARE(progressOf(train, 0, 100).delay, 0.0); // not entered
	train.CanEnter = true;
	train.earliestActiveTrajectoryIndex = 350;
	QCOMPARE(progressOf(train, 0, 400).delay, 300.0 * timestep);
	train.earliestActiveTrajectoryIndex = 40;
	QCOMPARE(progressOf(train, 0, 400).delay, 0.0);
	train.earliestActiveTrajectoryIndex = 350;
	train.StationArrivals[0] = 560.0;
	QCOMPARE(progressOf(train, 1, 590).delay, 60.0);
}

void TestPassengerJourneys::trainThatStandsPastItsDepartureIsAsLateAsItStands() {
	Train train = threeStops();
	standAt(train, 0, 100.0);
	// Planned to leave at 130 s: at 150 s it has stood 20 s over, and a passenger who arrives now can still board it.
	QCOMPARE(progressOf(train, 0, 120).delay, 0.0);
	QCOMPARE(progressOf(train, 0, 150).delay, 20.0);
}

void TestPassengerJourneys::trainDoesNotHaltAtTheStopWhereItEnters() {
	Train train = threeStops();
	train.Stations[0].X = 0.0;
	train.Stations[1].X = 4.0;
	train.Stations[2].X = 8.0;
	// Entering at the route start, exactly at stop 0.
	train.Start_Node_X = 0.0;
	QVERIFY(!haltsAtStop(train, 0));
	QVERIFY(haltsAtStop(train, 1));
	// Entering 10 m before stop 0 (standing before its first stop): it halts there.
	train.Start_Node_X = -0.010;
	QVERIFY(haltsAtStop(train, 0));
}

// Under a limit of 20 m/s the first arc runs at its track speed limit of 10 m/s and the others at the limit; past the arcs of the
// route the train runs at the limit. A train whose maximum speed is below the limit runs at that speed.
void TestPassengerJourneys::limitedRunTakesTheLowerSpeedOfEachArc() {
	LimitedRun limited;
	const std::vector<double> expected{0.0, 100.0, 200.0, 300.0};
	QCOMPARE(limitedRunningSeconds(limited.run, limited.train(), {0.0, 1000.0, 3000.0, 5000.0}, 20.0), expected);
	QCOMPARE(limitedRunningSeconds(limited.run, limited.train(), {500.0, 3500.0}, 20.0).back(), 175.0);
	QCOMPARE(limitedRunningSeconds(limited.run, limited.train(), {3000.0, 5000.0}, 20.0).back(), 100.0);
	limited.train().max_train_speed = 16.0;
	QCOMPARE(limitedRunningSeconds(limited.run, limited.train(), {1000.0, 3000.0}, 20.0).back(), 125.0);
}

// S-1 ran at 500 m at 150 s under the cap of 20 m/s: it reaches its second stop 50 s later, at the track speed limit of 10 m/s,
// and needs 100 s from there to its third stop, 30 s more than planned.
void TestPassengerJourneys::breakdownSpeedCapLimitsTheTrain() {
	LimitedRun limited;
	limited.runAt500Metres();
	const passenger_routing::TrainProgress progress = liveTrainProgress(limited.run, limited.train(), 1, 151);
	QVERIFY(progress.limited);
	QVERIFY(!progress.standing);
	QCOMPARE(progress.delay, 0.0);
	QCOMPARE(progress.limits.size(), std::size_t(1));
	QCOMPARE(progress.limits[0].ends, 1000.0);
	QCOMPARE(progress.nextStop, std::size_t(1));
	QCOMPARE(progress.nextArrival, 200.0);
	QCOMPARE(progress.limits[0].runs.size(), std::size_t(3));
	QCOMPARE(progress.limits[0].runs[1], 100.0);
	QCOMPARE(progress.limits[0].runs[2], -1.0);
	const passenger_routing::TrainTimetable timetable{{{0, 0, 100.0, 130.0}, {1, 0, 200.0, 230.0}, {2, 0, 300.0, -1.0}}};
	const passenger_routing::TrainState state = liveTrainState(limited.run, limited.train(), timetable, 1, 151);
	QVERIFY(state.slowed);
	QVERIFY(!state.late);
	QCOMPARE(state.arrival(timetable, 1), 200.0);
	QCOMPARE(state.arrival(timetable, 2), 330.0);
	// A stop off the route of the train has no running time.
	limited.train().stationBlockSection[2] = -1;
	QCOMPARE(liveTrainProgress(limited.run, limited.train(), 1, 151).limits[0].runs[1], -1.0);
}

// Before the cap starts, after it ends and without a cap the train is not limited.
void TestPassengerJourneys::trainWithoutSpeedCapIsNotLimited() {
	LimitedRun limited;
	QVERIFY(!liveTrainProgress(limited.run, limited.train(), 0, 40).limited);
	QVERIFY(!liveTrainProgress(limited.run, limited.train(), 0, 1000).limited);
	LimitedRun uncapped(false);
	uncapped.runAt500Metres();
	const passenger_routing::TrainProgress progress = liveTrainProgress(uncapped.run, uncapped.train(), 1, 151);
	QVERIFY(!progress.limited);
	QVERIFY(progress.limits.empty());
}

// At 50 s a cap of 36 km/h (10 m/s) applies until 100 s. S-1 has not entered; it may enter at 200 s at the start of its route
// and is planned at its second stop, 1 km on, at 250 s. It sets off after the cap has ended, so it is predicted there on time.
// A cap that lasts until 1,000 s makes it 50 s late there.
void TestPassengerJourneys::capThatEndsBeforeTheTrainSetsOffDoesNotDelayIt() {
	LimitedRun limited;
	limited.run.incidents[0].reducedSpeedKmh = 36.0;
	limited.run.incidents[0].endSeconds = 100.0;
	Train& waiting = limited.train();
	waiting.departure_time = 200.0;
	// A stop at the start of the route is not the end of an arc.
	waiting.stationBlockSection[0] = -1;
	const passenger_routing::TrainTimetable timetable{{{0, 0, -1.0, -1.0}, {1, 0, 250.0, 280.0}, {2, 0, 400.0, -1.0}}};
	const passenger_routing::TrainState state = liveTrainState(limited.run, waiting, timetable, 0, 50);
	QVERIFY(state.slowed);
	QCOMPARE(state.arrival(timetable, 1), 250.0);
	QCOMPARE(state.arrival(timetable, 2), 400.0);
	limited.run.incidents[0].endSeconds = 1000.0;
	QCOMPARE(liveTrainState(limited.run, waiting, timetable, 0, 50).arrival(timetable, 1), 300.0);
}

// Two caps apply at 60 s: 36 km/h (10 m/s) until 100 s, and 45 km/h (12.5 m/s) without an end. The simulator takes the lower one
// at every step, so the prediction runs at 10 m/s until 100 s and at 12.5 m/s after that. A cap that starts after now is not
// foreseen.
void TestPassengerJourneys::capThatLastsLongerTakesOver() {
	LimitedRun limited;
	limited.run.incidents[0].reducedSpeedKmh = 36.0;
	limited.run.incidents[0].endSeconds = 100.0;
	SimulationIncident lasting = limited.run.incidents[0];
	lasting.startSeconds = 0.0;
	lasting.reducedSpeedKmh = 45.0;
	lasting.hasEndSeconds = false;
	lasting.endSeconds = 0.0;
	limited.run.incidents.push_back(lasting);
	const passenger_routing::TrainProgress progress = liveTrainProgress(limited.run, limited.train(), 1, 60);
	QCOMPARE(progress.limits.size(), std::size_t(2));
	QCOMPARE(progress.limits[0].ends, 100.0);
	QCOMPARE(progress.limits[1].ends, passenger_routing::kUnreached);
	QCOMPARE(progress.limits[0].runs[1], 200.0);
	QCOMPARE(progress.limits[1].runs[1], 160.0);
	limited.run.incidents[1].startSeconds = 70.0;
	QCOMPARE(liveTrainProgress(limited.run, limited.train(), 1, 60).limits.size(), std::size_t(1));
}

// S-1 runs to its stop at 1 km, then calls at a stop off its route and at 3 and 4 km. The runs into and out of the stop off the
// route are not known; the run from 3 to 4 km is, at the cap of 20 m/s.
void TestPassengerJourneys::runsPassOverStopsOffTheRoute() {
	LimitedRun limited;
	limited.addStopOffTheRoute();
	limited.runAt500Metres();
	const passenger_routing::TrainProgress progress = liveTrainProgress(limited.run, limited.train(), 1, 151);
	QCOMPARE(progress.nextStop, std::size_t(1));
	QCOMPARE(progress.nextArrival, 200.0);
	QCOMPARE(progress.limits.size(), std::size_t(1));
	const std::vector<double>& runs = progress.limits.front().runs;
	QCOMPARE(runs[1], -1.0);
	QCOMPARE(runs[2], -1.0);
	QCOMPARE(runs[3], 50.0);
}

// The train arrived at A on time at 100, was due out at 130 and left at 431. Once it has left, it is 301 s late, above the
// threshold, so it is predicted at B at 501 and not on its planned 200.
void TestPassengerJourneys::departureDelayCountsOnceTheTrainLeft() {
	Train train = threeStops();
	train.Stations[0].StepStopped = 10;
	train.StationArrivals[0] = 100;
	train.StationDepartures[0] = 431;
	QCOMPARE(progressOf(train, 1, 440).delay, 301.0);
	const SimulationRun run;
	const passenger_routing::TrainTimetable timetable{{{0, 0, -1.0, 130.0}, {1, 0, 200.0, 230.0}, {2, 0, 300.0, -1.0}}};
	const passenger_routing::TrainState state = liveTrainState(run, train, timetable, 1, 440);
	QVERIFY(state.late);
	QCOMPARE(state.arrival(timetable, 1), 501.0);
}

// The train arrived at A at 500, 400 s late, and left at 701, 1 s after its planned departure at 700. It made up the time while it
// stood, so it is 1 s late and its later stops keep their planned times.
void TestPassengerJourneys::timeMadeUpAtAStopCounts() {
	Train train = threeStops();
	train.ScheduledDepartures[0] = 700.0;
	train.ScheduledArrivals[1] = 800.0;
	train.ScheduledDepartures[1] = 830.0;
	train.ScheduledArrivals[2] = 900.0;
	train.Stations[0].StepStopped = 10;
	train.StationArrivals[0] = 500;
	train.StationDepartures[0] = 701;
	QCOMPARE(progressOf(train, 1, 710).delay, 1.0);
	const SimulationRun run;
	const passenger_routing::TrainTimetable timetable{{{0, 0, -1.0, 700.0}, {1, 0, 800.0, 830.0}, {2, 0, 900.0, -1.0}}};
	const passenger_routing::TrainState state = liveTrainState(run, train, timetable, 1, 710);
	QVERIFY(!state.late);
	QCOMPARE(state.arrival(timetable, 1), 800.0);
}

// A cap that ends long after any time of the run gives one period that ends then.
void TestPassengerJourneys::capWithAVeryLateEndIsOnePeriod() {
	LimitedRun limited;
	limited.run.incidents[0].endSeconds = 3000000000.0;
	const passenger_routing::TrainProgress progress = liveTrainProgress(limited.run, limited.train(), 1, 60);
	QCOMPARE(progress.limits.size(), std::size_t(1));
	QCOMPARE(progress.limits[0].ends, 3000000000.0);
}

// At 60 s caps of 36 km/h until 100 s and 45 km/h without an end apply; a cap of 18 km/h from 90 s to 105 s has not started. The
// cap that starts later is not foreseen, and the 45 km/h cap still limits the train after 100 s.
void TestPassengerJourneys::capThatStartsLaterDoesNotHideOneThatAppliesNow() {
	LimitedRun limited;
	limited.run.incidents[0].reducedSpeedKmh = 36.0;
	limited.run.incidents[0].endSeconds = 100.0;
	SimulationIncident lasting = limited.run.incidents[0];
	lasting.startSeconds = 0.0;
	lasting.reducedSpeedKmh = 45.0;
	lasting.hasEndSeconds = false;
	lasting.endSeconds = 0.0;
	limited.run.incidents.push_back(lasting);
	SimulationIncident later = limited.run.incidents[0];
	later.startSeconds = 90.0;
	later.reducedSpeedKmh = 18.0;
	later.endSeconds = 105.0;
	limited.run.incidents.push_back(later);
	const passenger_routing::TrainProgress progress = liveTrainProgress(limited.run, limited.train(), 1, 60);
	QCOMPARE(progress.limits.size(), std::size_t(2));
	QCOMPARE(progress.limits[1].ends, passenger_routing::kUnreached);
	QCOMPARE(progress.limits[1].runs[1], 160.0);
}

// Caps of 36 km/h until 100.2 s and 45 km/h until 100.8 s: a run that starts at 100.5 s is still limited, at 45 km/h.
void TestPassengerJourneys::capsThatEndWithinOneSecondKeepTheirPeriods() {
	LimitedRun limited;
	limited.run.incidents[0].reducedSpeedKmh = 36.0;
	limited.run.incidents[0].endSeconds = 100.2;
	SimulationIncident second = limited.run.incidents[0];
	second.reducedSpeedKmh = 45.0;
	second.endSeconds = 100.8;
	limited.run.incidents.push_back(second);
	const passenger_routing::TrainProgress progress = liveTrainProgress(limited.run, limited.train(), 1, 60);
	QCOMPARE(progress.limits.size(), std::size_t(2));
	QCOMPARE(progress.limits[0].ends, 100.2);
	QCOMPARE(progress.limits[1].ends, 100.8);
	QCOMPARE(progress.limits[1].runs[1], 160.0);
}

// The train left its first stop on time and runs to its second stop, due at 200 s, with its head at 1,500 m. It is on time until then,
// 60 s late at 260 s, and at 600 s 400 s late, above the late threshold, so that its later stops are predicted 400 s late. Once it
// stands at the second stop, its arrival there gives the delay.
void TestPassengerJourneys::trainBetweenStopsIsAsLateAsItIsOverdue() {
	Train train = threeStops();
	for (int stop = 0; stop < 3; ++stop)
		train.Stations[static_cast<std::size_t>(stop)].X = 1.0 + stop;
	train.Stations[0].StepStopped = 10;
	train.StationArrivals[0] = 100;
	train.StationDepartures[0] = 130;
	train.CanEnter = true;
	train.earliestActiveTrajectoryIndex = 90;
	train.instant_spatial_position.assign(700, 1500.0);
	QCOMPARE(progressOf(train, 1, 190).delay, 0.0);
	QCOMPARE(progressOf(train, 1, 260).delay, 60.0);
	const SimulationRun run;
	const passenger_routing::TrainTimetable timetable{{{0, 0, 100.0, 130.0}, {1, 0, 200.0, 230.0}, {2, 0, 300.0, -1.0}}};
	const passenger_routing::TrainState state = liveTrainState(run, train, timetable, 1, 600);
	QVERIFY(state.late);
	QCOMPARE(state.arrival(timetable, 1), 600.0);
	QCOMPARE(state.arrival(timetable, 2), 700.0);
	standAt(train, 1, 270.0);
	QCOMPARE(progressOf(train, 1, 280).delay, 70.0);
}

// The train left A on time, passed two stops off its route without standing, and has stood since 400 s at P, on time, for a dwell
// until its planned departure at 1,000 s, with its head held 50 m short of the platform. Its first stop is P, and at 701 s, 301 s
// after it was due at P, it is not late, also when the caller still has the first stop off the route.
void TestPassengerJourneys::trainHeldAtTheStartOfItsDwellIsNotOverdue() {
	Train train = threeStops();
	train.numStations = 5;
	train.Stations.resize(5);
	for (int stop = 0; stop < 5; ++stop) {
		train.Stations[static_cast<std::size_t>(stop)].X = 1.0 + stop;
		train.Stations[static_cast<std::size_t>(stop)].StepStopped = 0;
		train.stationBlockSection[stop] = stop == 1 || stop == 2 ? -1 : 0;
		train.StationArrivals[stop] = -1;
		train.ScheduledArrivals[stop] = 100.0 * (stop + 1);
		train.ScheduledDepartures[stop] = stop < 4 ? 100.0 * (stop + 1) + 30 : -1;
	}
	train.ScheduledDepartures[3] = 1000.0;
	train.Stations[0].StepStopped = 10;
	train.StationArrivals[0] = 100;
	train.StationDepartures[0] = 130;
	train.CanEnter = true;
	train.earliestActiveTrajectoryIndex = 90;
	train.instant_spatial_position.assign(1100, 3950.0);
	train.Stations[3].StepStopped = 1;
	train.StationArrivals[3] = 400;
	train.StoppedForServiceStop = true;
	QCOMPARE(advanceFirstStop(train, 1), 3);
	QCOMPARE(progressOf(train, 3, 701).delay, 0.0);
	QCOMPARE(progressOf(train, 1, 701).delay, 0.0);
	QCOMPARE(progressOf(train, 3, 1010).delay, 10.0);
}

void TestPassengerJourneys::trainThatEndsEarlySetsDownPassengerWhoFindsNoRoute() {
	EndedTrainRun ended(false);
	ended.run.passengerJourneys.settleTrains(ended.run, ended.flow, 150);
	const Passenger& passenger = ended.passenger();
	const Journey& journey = passenger.Journeys.front();
	// The trip ends at B, where the train stopped last, and nothing goes on to C.
	QCOMPARE(journey.Trips.size(), std::size_t(1));
	QCOMPARE(journey.Trips.front().Arr_Station_ID, std::string("B"));
	QVERIFY(journey.Trips.front().IsTripCompleted);
	QCOMPARE(journey.Trips.front().Actual_Arrival_Time, 150);
	QVERIFY(journey.Outcome == JourneyOutcome::NoRoute);
	QVERIFY(journey.IsReplanned);
	QVERIFY(!journey.IsJourneyCompleted);
	QVERIFY(!passenger.IsIntheNetwork);
	QCOMPARE(passenger.StationExitedTheNetworkID, std::string("B"));
	QCOMPARE(ended.run.trains[0].Current_OnBoard_Passengers, 0);
}

void TestPassengerJourneys::trainThatEndsEarlySetsDownPassengerWhoContinuesOnAnother() {
	EndedTrainRun ended(true);
	ended.run.passengerJourneys.settleTrains(ended.run, ended.flow, 150);
	const Passenger& passenger = ended.passenger();
	const Journey& journey = passenger.Journeys.front();
	// T2 calls at B and C later: the passenger waits at B for it, on a trip planned from its timetable.
	QCOMPARE(journey.Trips.size(), std::size_t(2));
	QCOMPARE(journey.Trips.front().Arr_Station_ID, std::string("B"));
	const Trip& next = journey.Trips.back();
	QCOMPARE(next.TripID, std::string("j.live1"));
	QCOMPARE(next.TrainServiceDescription, std::string("T2"));
	QCOMPARE(next.Dep_Station_ID, std::string("B"));
	QCOMPARE(next.Arr_Station_ID, std::string("C"));
	QCOMPARE(next.Planned_Departure_Time, 210);
	QVERIFY(journey.IsReplanned);
	// The delay is measured against the first plan (arrival 200), not against the planned arrival of T2.
	QCOMPARE(journey.FirstPlanArrival, 200);
	QVERIFY(next.Planned_Arrival_Time != 200);
	QCOMPARE(journey.arrivalDelay(230, -5), 30);
	QVERIFY(passenger.IsIntheNetwork);
	QCOMPARE(passenger.CurrentStatus, PassengerStatus::OnPlatform);
	QCOMPARE(passenger.Current_Train_To_Wait, std::string("T2"));
	QCOMPARE(passenger.Current_WaitingStationID, std::string("B"));
	QCOMPARE(ended.flow.waitingCount("B", "B.platform.1"), 1);
}

// The passengers whom an ended train sets down alight at its last stop: the counts of that call say so, and the train
// leaves it empty.
void TestPassengerJourneys::trainThatEndsEarlyCountsThePassengersItSetsDown() {
	EndedTrainRun ended(false);
	ended.run.stopCalls.at(0, 1).loadAfter = 1;
	ended.run.passengerJourneys.settleTrains(ended.run, ended.flow, 150);
	const StopCallCounts* call = ended.run.stopCalls.find(0, 1);
	if (call == nullptr)
		QFAIL("the call at B has no counts");
	QCOMPARE(call->alighted, 1);
	QCOMPARE(call->loadAfter, 0);
}

// The train to change to has left before the passenger alights. The passenger has not set off for its platform, so the new
// plan starts on the arrival platform at the moment of alighting: T3 leaves from that platform a minute later.
void TestPassengerJourneys::passengerWhoAlightsAfterTheChangeLeftPlansFromTheArrivalPlatform() {
	const MissedChangeRun missed;
	const Passenger& rider = missed.passenger("rider");
	const Journey& journey = rider.Journeys.front();
	QCOMPARE(journey.Trips.size(), std::size_t(2));
	QVERIFY(journey.Trips.front().IsTripCompleted);
	QCOMPARE(journey.Trips.front().Actual_Arrival_Time, 500);
	const Trip& next = journey.Trips.back();
	QCOMPARE(next.TrainServiceDescription, std::string("T3"));
	QCOMPARE(next.Dep_Station_Platform_ID, std::string("B.platform.2"));
	// No walk to platform 8 and back, and the wait for T3 starts on arrival.
	QCOMPARE(journey.Walkingtime, 0.0);
	QCOMPARE(next.Actual_Departure_Time, 500);
	QVERIFY(journey.IsReplanned);
	QCOMPARE(rider.CurrentStatus, PassengerStatus::OnPlatform);
	QCOMPARE(rider.Current_Train_To_Wait, std::string("T3"));
	QCOMPARE(rider.Current_WaitingStationPlatformID, std::string("B.platform.2"));
}

// A passenger who has waited since 00:00:00 has waited from second 0, which is a time like any other: the wait for the
// train that left counts, and the new plan starts on the platform where the passenger stands. T3 leaves platform 2 before
// the walk of 180 s ends, so the passenger takes T4.
void TestPassengerJourneys::passengerWhoWaitedSinceMidnightKeepsTheWaitAndThePlatform() {
	const MissedChangeRun missed;
	const Passenger& waiter = missed.passenger("waiter");
	const Journey& journey = waiter.Journeys.front();
	QCOMPARE(journey.Trips.size(), std::size_t(1));
	const Trip& next = journey.Trips.back();
	QCOMPARE(next.TrainServiceDescription, std::string("T4"));
	QCOMPARE(journey.Waitingtime, 500.0);
	QCOMPARE(journey.Walkingtime, 180.0);
	QCOMPARE(next.Actual_Departure_Time, 680);
	QCOMPARE(waiter.Current_Train_To_Wait, std::string("T4"));
}

// After the change the passenger's plan is T4, which reaches C at 1060. T3 stands on the same platform and reaches C at 650, so
// the passenger takes T3.
void TestPassengerJourneys::passengerBoardsStandingTrainThatGoesDirectInTime() {
	const StandingTrainRun standing(650);
	const Passenger& rider = standing.rider();
	const Journey& journey = rider.Journeys.front();
	QCOMPARE(journey.Trips.size(), std::size_t(2));
	QCOMPARE(journey.Trips.back().TrainServiceDescription, std::string("T3"));
	QCOMPARE(journey.Trips.back().Arr_Station_ID, std::string("C"));
	QVERIFY(journey.IsReplanned);
	QCOMPARE(journey.FirstPlanArrival, 1060);
	QCOMPARE(rider.current_TripID, journey.Trips.back().TripID);
	QCOMPARE(journey.Walkingtime, 0.0);
	// The passenger waits for T3 on its platform.
	QCOMPARE(rider.CurrentStatus, PassengerStatus::OnPlatform);
	QCOMPARE(rider.Current_Train_To_Wait, std::string("T3"));
	QCOMPARE(rider.Current_WaitingStationPlatformID, std::string("B.platform.2"));
}

// T3 would reach C at 1100, later than the plan, so the passenger keeps waiting for T4.
void TestPassengerJourneys::passengerKeepsPlanWhenStandingTrainArrivesLater() {
	const StandingTrainRun standing(1100);
	const Passenger& rider = standing.rider();
	const Journey& journey = rider.Journeys.front();
	QCOMPARE(journey.Trips.back().TrainServiceDescription, std::string("T4"));
	QVERIFY(!journey.IsReplanned);
	QCOMPARE(rider.CurrentStatus, PassengerStatus::OnPlatform);
}

// T1, which the passenger has just left, goes on to C before T4 does, but it is not another train.
void TestPassengerJourneys::passengerDoesNotBoardTheTrainItLeft() {
	const StandingTrainRun standing(1100, true);
	const Journey& journey = standing.rider().Journeys.front();
	QCOMPARE(journey.Trips.back().TrainServiceDescription, std::string("T4"));
	QVERIFY(!journey.IsReplanned);
}

// The passenger waits for T4, which reaches C at 1060. T3 arrives at the platform and reaches C at 650.
void TestPassengerJourneys::waitingPassengerSwitchesToTrainThatArrivesAndGoesDirect() {
	const ArrivingTrainRun arrived(650);
	const Journey& journey = arrived.waiter().Journeys.front();
	QCOMPARE(journey.Trips.back().TrainServiceDescription, std::string("T3"));
	QVERIFY(journey.IsReplanned);
	QCOMPARE(journey.FirstPlanArrival, 1060);
	QCOMPARE(arrived.waiter().Current_Train_To_Wait, std::string("T3"));
}

void TestPassengerJourneys::waitingPassengerKeepsPlanWhenTheDirectTrainArrivesLater() {
	const ArrivingTrainRun arrived(1100);
	QCOMPARE(arrived.waiter().Current_Train_To_Wait, std::string("T4"));
	QVERIFY(!arrived.waiter().Journeys.front().IsReplanned);
}

// T3 reaches C in time but has no room, so the passenger keeps waiting for T4.
void TestPassengerJourneys::waitingPassengerIgnoresFullStandingTrain() {
	const ArrivingTrainRun arrived(650, true);
	QCOMPARE(arrived.waiter().Current_Train_To_Wait, std::string("T4"));
}

// At 800 s T1 has not come to B, where it was due at 400 s: it is 400 s late and predicted at C at 1,400 s. T2 leaves B at 810 s and
// reaches C at 950 s, so the passenger takes it. The journey counts as planned again, keeps the arrival of its first plan and the
// wait for T1, and the wait for T2 starts now.
void TestPassengerJourneys::passengerLeavesLateTrainForAnEarlierOne() {
	LateTrainRun late(790);
	late.settle(800);
	const Journey& journey = late.journey();
	QCOMPARE(journey.Trips.size(), std::size_t(1));
	QCOMPARE(journey.Trips.front().TrainServiceDescription, std::string("T2"));
	QCOMPARE(journey.Trips.front().Planned_Arrival_Time, 950);
	QCOMPARE(journey.Trips.front().Actual_Departure_Time, 800);
	QVERIFY(journey.IsReplanned);
	QCOMPARE(journey.FirstPlanArrival, 1000);
	QCOMPARE(journey.Waitingtime, 500.0);
	QCOMPARE(late.waiter().Current_Train_To_Wait, std::string("T2"));
	const std::vector<int> waiting{0};
	QCOMPARE(late.flow.waitingFor(late.flow.platformNumber("B", "B.platform.1"), late.flow.trainNumber("T2")), waiting);
}

// T2 reaches C at 1,360 s, 40 s before T1 is predicted there, which is within the margin: the passenger keeps waiting for T1. At
// 1,330 s, 70 s before T1, the passenger takes T2.
void TestPassengerJourneys::passengerKeepsLateTrainWithinTheMargin() {
	QCOMPARE(PassengerJourneys::kSwitchMarginSeconds, 60.0);
	LateTrainRun within(1200);
	within.settle(800);
	QCOMPARE(within.journey().Trips.front().TripID, std::string("w.1"));
	QVERIFY(!within.journey().IsReplanned);
	QCOMPARE(within.waiter().Current_Train_To_Wait, std::string("T1"));
	LateTrainRun beyond(1170);
	beyond.settle(800);
	QCOMPARE(beyond.waiter().Current_Train_To_Wait, std::string("T2"));
}

// At 650 s T1 is 250 s late, within the late threshold. The plans use its planned times, so the passenger is not planned again,
// although T2 reaches C earlier.
void TestPassengerJourneys::passengerKeepsTrainLateUpToTheThreshold() {
	LateTrainRun late(790);
	late.settle(650);
	QCOMPARE(late.waiter().Current_Train_To_Wait, std::string("T1"));
	QVERIFY(!late.journey().IsReplanned);
}

// Only the passengers of a train whose prediction changed plan again, and the prediction counts the lateness in whole minutes.
// The prediction of T1 at 800 s, 400 s or 6 whole minutes late, is first seen while nobody waits. At 801 s it is still 6 whole
// minutes late, so the passenger who now waits for T1 is not planned again, although T2 reaches C at 1,060 s. At 840 s T1 is 7
// whole minutes late, and the passenger takes T2.
void TestPassengerJourneys::passengerPlansAgainOnlyWhenThePredictionChanges() {
	LateTrainRun late(900);
	std::list<Passenger> nobody;
	late.flow.build(nobody);
	late.settle(800);
	late.flow.build(late.run.passengers);
	late.settle(800);
	QCOMPARE(late.waiter().Current_Train_To_Wait, std::string("T1"));
	late.settle(801);
	QCOMPARE(late.waiter().Current_Train_To_Wait, std::string("T1"));
	late.settle(840);
	QCOMPARE(late.waiter().Current_Train_To_Wait, std::string("T2"));
	QCOMPARE(late.journey().Trips.front().Planned_Arrival_Time, 1060);
}

// A passenger who comes to wait for T1 when it is already late, such as after a change, takes T2 as well.
void TestPassengerJourneys::passengerWhoComesToWaitForALateTrainTakesAnEarlierOne() {
	LateTrainRun late(790);
	late.run.passengerJourneys.checkWaiting(late.run, late.run.passengers.front(), 800);
	QCOMPARE(late.waiter().Current_Train_To_Wait, std::string("T2"));
	QVERIFY(late.journey().IsReplanned);
}

// At 800 s T1 is 400 s late and reaches C at 960 s, after T2 left C. The passenger, in line for T1 at B since 300 s, plans T1 and
// then T3, and keeps the place in line ahead of a passenger who came for T1 at 400 s. The time waited is counted at boarding.
void TestPassengerJourneys::passengerWhoKeepsTheTrainKeepsThePlaceInLine() {
	WaitingRun waiting({trainCallingAt("T1", {"B", "C"}, {400, 560}), trainCallingAt("T2", {"C", "D"}, {600, 700}),
						   trainCallingAt("T3", {"C", "D"}, {1000, 1100})},
		{tripOn("w.1", "T1", "B", "C", 560), tripOn("w.2", "T2", "C", "D", 700)}, 0, 300);
	waiting.addPassenger("other", {tripOn("o.1", "T1", "B", "C", 560)}, 0, 400);
	waiting.settle(800);
	const std::vector<std::string> trains{"T1", "T3"};
	QCOMPARE(waiting.trains(), trains);
	QVERIFY(waiting.journey().IsReplanned);
	QCOMPARE(waiting.journey().Trips.front().Actual_Departure_Time, 300);
	QCOMPARE(waiting.journey().Waitingtime, 0.0);
	const std::vector<int> line{0, 1};
	QCOMPARE(waiting.flow.waitingFor(waiting.flow.platformNumber("B", "B.platform.1"), waiting.flow.trainNumber("T1")), line);
}

// The passenger rode T1 from A to B and waits there for T2 to D, which is 400 s late at 800 s and reaches D at 1,400 s. T3 and T4
// reach D at 1,050 s with a change at C. With one transfer allowed the journey has no change left, so the passenger keeps T2; with
// two, the passenger takes T3 and T4.
void TestPassengerJourneys::newPlanCountsTheTrainsTheJourneyRode() {
	for (const int allowed : {1, 2}) {
		Train first = trainCallingAt("T1", {"A", "B"}, {100, 300});
		first.OutOfSimulation = true;
		WaitingRun waiting({first, trainCallingAt("T2", {"B", "D"}, {400, 1000}), trainCallingAt("T3", {"B", "C"}, {820, 900}),
							   trainCallingAt("T4", {"C", "D"}, {950, 1050})},
			{tripOn("w.1", "T1", "A", "B", 300), tripOn("w.2", "T2", "B", "D", 1000)}, 1, 300);
		waiting.run.settings.passengerMaxTransfers = allowed;
		waiting.settle(800);
		const std::vector<std::string> kept{"T1", "T2"};
		const std::vector<std::string> changed{"T1", "T3", "T4"};
		QCOMPARE(waiting.trains(), allowed == 1 ? kept : changed);
		QCOMPARE(waiting.journey().IsReplanned, allowed == 2);
	}
}

// T calls at B at 400 s and again at 600 s, after a call at X, and reaches C at 800 s. At 800 s it is 400 s late, so that it leaves B
// at 820 s and at 1,020 s. The passenger is on the way to the platform until 830 s and can make only the second call, which is the
// plan the passenger has: it is not planned again.
void TestPassengerJourneys::passengerKeepsPlanThatMakesALaterCallOfTheTrain() {
	WaitingRun waiting({trainCallingAt("T", {"B", "X", "B", "C"}, {400, 500, 600, 800})}, {tripOn("w.1", "T", "B", "C", 800)}, 0, 830);
	waiting.settle(800);
	QCOMPARE(waiting.journey().Trips.front().TripID, std::string("w.1"));
	QVERIFY(!waiting.journey().IsReplanned);
}

// T1 has no planned departure at B, where it still stops and leaves when it gets there. At 800 s it is 400 s late, so that it
// reaches B now and C at 1,400 s. T2 reaches C at 1,460 s, later, so the passenger keeps T1.
void TestPassengerJourneys::passengerKeepsTrainThatStopsWithoutAPlannedDeparture() {
	Train late = trainCallingAt("T1", {"B", "C"}, {400, 1000});
	late.ScheduledDepartures[0] = -1;
	WaitingRun waiting({late, trainCallingAt("T2", {"B", "C"}, {1300, 1460})}, {tripOn("w.1", "T1", "B", "C", 1000)}, 0, 300);
	waiting.settle(800);
	QCOMPARE(waiting.trains(), std::vector<std::string>{"T1"});
	QVERIFY(!waiting.journey().IsReplanned);
}

// A lateness above the range of an int, or one that is not a number, counts as the largest int, and one below it as the smallest; a
// predicted time that is not a number counts as none. Under a cap of 1e-9 km/h, which takes the train some 57,000 years to its next
// stop, the first lateness is the largest int.
void TestPassengerJourneys::extremePredictionCountsAsTheLargestLateness() {
	constexpr int kLargest = std::numeric_limits<int>::max();
	constexpr int kSmallest = std::numeric_limits<int>::min();
	const passenger_routing::TrainTimetable timetable{{{0, 0, 100.0, 130.0}, {1, 0, 200.0, -1.0}}};
	passenger_routing::TrainState state;
	state.late = true;
	state.arrivals = {std::numeric_limits<double>::infinity(), 1e30};
	state.departures = {std::numeric_limits<double>::quiet_NaN(), -1.0};
	// The first lateness is infinite, the same time again is infinity minus infinity, and 1e30 s is infinitely earlier than it.
	const std::vector<int> extreme{1, 0, kLargest, kLargest, -1, kSmallest, -1};
	QCOMPARE(predictionSignature(timetable, state), extreme);

	LimitedRun limited;
	limited.run.incidents.front().reducedSpeedKmh = 1e-9;
	limited.runAt500Metres();
	const passenger_routing::TrainTimetable route{{{0, 0, 100.0, 130.0}, {1, 0, 200.0, 230.0}, {2, 0, 300.0, -1.0}}};
	const passenger_routing::TrainState capped = liveTrainState(limited.run, limited.train(), route, 1, 151);
	QVERIFY(capped.slowed);
	const std::vector<int> crawling = predictionSignature(route, capped);
	QCOMPARE(crawling.size(), std::size_t(7));
	QCOMPARE(crawling[2], kLargest);
}

// A train with 30 stops, whose planned times fall on 50 different seconds of the minute, is held before its first stop, due at
// 120 s. From 421 s on it is late and gets a second later at every step, and its lateness at every stop moves to another whole
// minute once a minute: in the 120 steps up to 540 s its passengers plan again three times.
void TestPassengerJourneys::heldTrainHasANewPredictionOnceAMinute() {
	SimulationRun run;
	run.settings.startingSimulationTime = 0;
	std::vector<std::string> stations;
	std::vector<double> arrivals;
	for (int stop = 0; stop < 30; ++stop) {
		stations.push_back("S" + std::to_string(stop));
		arrivals.push_back(120.0 + 61.0 * stop);
	}
	run.trains.push_back(trainCallingAt("H", stations, arrivals));
	run.numTrains = 1;
	int evaluations = 0;
	for (int t = 421; t <= 540; ++t)
		evaluations += static_cast<int>(run.passengerJourneys.trainsWithNewPrediction(run, t).size());
	QCOMPARE(evaluations, 3);
}

// S-1 runs under a cap of 25.2 km/h (7 m/s) on a line with 20 stops 1 km apart, planned 41 s apart with their planned times on 40
// different seconds of the minute, and the cap adds about 101.86 s to every run. It left its first stop on time and is held 500 m
// short of the second, so that it sets off a second later at every step, and its lateness at each stop ahead grows with it from a
// different fraction of a minute. It is late from 482 s on. Its prediction still changes once a minute: in the 120 steps from 490 s to
// 609 s its passengers plan again three times, at the first step and when it is 7 and 8 whole minutes late at the next stop.
void TestPassengerJourneys::cappedTrainHeldBetweenStopsHasANewPredictionOnceAMinute() {
	constexpr int kStops = 20;
	SimulationRun run;
	run.settings.startingSimulationTime = 0;
	Section section;
	for (int a = 0; a < kStops + 2; ++a) {
		Arc arc;
		arc.startNode.X = a;
		arc.endNode.X = a + 1;
		arc.speedLimit = 30.0;
		section.arcs_in_signalling_block_section.push_back(arc);
	}
	section.total_arcs = kStops + 2;
	Route route;
	route.sequence_of_block_sections.push_back(section);
	route.N_Block_Sections = 1;
	run.infra.routes.push_back(route);
	run.infra.numRoutes = 1;
	std::vector<std::string> stations;
	std::vector<double> arrivals;
	for (int stop = 0; stop < kStops; ++stop) {
		stations.push_back("S" + std::to_string(stop));
		arrivals.push_back(120.0 + 61.0 * stop);
	}
	Train held = trainCallingAt("S-1", stations, arrivals);
	held.indexOfRoute = 0;
	held.max_train_speed = 50.0;
	held.Stations[0].StepStopped = 10;
	held.StationArrivals[0] = 120;
	held.StationDepartures[0] = 140;
	held.CanEnter = true;
	held.earliestActiveTrajectoryIndex = 100;
	held.instant_spatial_position.assign(700, 2500.0);
	run.trains.push_back(held);
	run.numTrains = 1;
	SimulationIncident cap;
	cap.type = IncidentType::TrainBreakdown;
	cap.target = "S";
	cap.startSeconds = 50.0;
	cap.hasReducedSpeed = true;
	cap.reducedSpeedKmh = 25.2;
	cap.hasEndSeconds = true;
	cap.endSeconds = 100000.0;
	run.incidents.push_back(cap);
	int evaluations = 0;
	for (int t = 490; t <= 609; ++t)
		evaluations += static_cast<int>(run.passengerJourneys.trainsWithNewPrediction(run, t).size());
	QCOMPARE(evaluations, 3);
}

void TestPassengerJourneys::activityTextIsParsedCaseInsensitively() {
	QVERIFY(passengerActivityFromText("") == PassengerActivity::None);
	QVERIFY(passengerActivityFromText("Home") == PassengerActivity::Home);
	QVERIFY(passengerActivityFromText("WORK") == PassengerActivity::Work);
	QVERIFY(passengerActivityFromText("leisure") == PassengerActivity::Leisure);
	QVERIFY(passengerActivityFromText("shopping") == PassengerActivity::Other);
}

void TestPassengerJourneys::statusNamesAreTheLegacyText() {
	QCOMPARE(std::string(passengerStatusName(PassengerStatus::Outside)), std::string("None"));
	QCOMPARE(std::string(passengerStatusName(PassengerStatus::OnPlatform)), std::string("OnPlatform"));
	QCOMPARE(std::string(passengerStatusName(PassengerStatus::OnBoard)), std::string("OnBoard"));
}

QTEST_APPLESS_MAIN(TestPassengerJourneys)
#include "test_passengerjourneys.moc"
