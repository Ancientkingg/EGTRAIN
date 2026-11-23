// The train state that passenger journeys are planned on (simulation/PassengerJourneys.h): how far a train has come in its stops, and
// the delay it is known to have.

#include "simulation/PassengerFlow.h"
#include "simulation/PassengerJourneys.h"
#include "simulation/RollingStock.h"
#include "simulation/SimulationRun.h"

#include <QTest>

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
	void trainThatEndsEarlySetsDownPassengerWhoFindsNoRoute();
	void trainThatEndsEarlySetsDownPassengerWhoContinuesOnAnother();
	void trainThatEndsEarlyCountsThePassengersItSetsDown();
	void passengerWhoAlightsAfterTheChangeLeftPlansFromTheArrivalPlatform();
	void passengerWhoWaitedSinceMidnightKeepsTheWaitAndThePlatform();
	void passengerBoardsStandingTrainThatGoesDirectInTime();
	void passengerKeepsPlanWhenStandingTrainArrivesLater();
	void passengerDoesNotBoardTheTrainItLeft();
	void activityTextIsParsedCaseInsensitively();
	void statusNamesAreTheLegacyText();
};

void TestPassengerJourneys::trainBeforeItsFirstStopCanBeBoardedThere() {
	const Train train = threeStops();
	QCOMPARE(advanceFirstStop(train, 0), 0);
	const passenger_routing::TrainState state = liveTrainState(train, 0, 0);
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
	QCOMPARE(liveTrainState(train, 3, 400).firstStop, std::size_t(3));
}

void TestPassengerJourneys::delayIsThatOfTheLatestArrival() {
	Train train = threeStops();
	train.StationArrivals[0] = 160.0; // 60 s late at the first stop
	train.StationArrivals[1] = 245.0; // 45 s late at the second
	train.Stations[0].StepStopped = 10;
	train.Stations[1].StepStopped = 10;
	QCOMPARE(liveTrainState(train, 2, 250).delay, 45.0);
	// Before it arrived at the second stop, the delay is that of the first.
	train.StationArrivals[1] = -1;
	QCOMPARE(liveTrainState(train, 1, 170).delay, 60.0);
}

void TestPassengerJourneys::delayIsNeverNegative() {
	Train train = threeStops();
	train.StationArrivals[0] = 70.0; // 30 s early
	QCOMPARE(liveTrainState(train, 1, 80).delay, 0.0);
}

// Before the first recorded arrival, a train that entered later than it may enter is as late as its entry. A train that has not
// entered keeps its planned times, and an early entry is no negative delay. Once it arrived, the delay is that of the arrival.
void TestPassengerJourneys::trainThatEnteredLateIsLateBeforeItsFirstArrival() {
	Train train = threeStops();
	train.departure_time = 50.0;
	QCOMPARE(liveTrainState(train, 0, 100).delay, 0.0); // not entered
	train.CanEnter = true;
	train.earliestActiveTrajectoryIndex = 350;
	QCOMPARE(liveTrainState(train, 0, 400).delay, 300.0 * timestep);
	train.earliestActiveTrajectoryIndex = 40;
	QCOMPARE(liveTrainState(train, 0, 400).delay, 0.0);
	train.earliestActiveTrajectoryIndex = 350;
	train.StationArrivals[0] = 160.0;
	QCOMPARE(liveTrainState(train, 1, 400).delay, 60.0);
}

void TestPassengerJourneys::trainThatStandsPastItsDepartureIsAsLateAsItStands() {
	Train train = threeStops();
	standAt(train, 0, 100.0);
	// Planned to leave at 130 s: at 150 s it has stood 20 s over, and a passenger who arrives now can still board it.
	QCOMPARE(liveTrainState(train, 0, 120).delay, 0.0);
	QCOMPARE(liveTrainState(train, 0, 150).delay, 20.0);
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
