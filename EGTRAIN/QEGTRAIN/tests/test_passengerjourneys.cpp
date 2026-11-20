// The train state that passenger journeys are planned on (simulation/PassengerJourneys.h): how far a train has come in its stops, and
// the delay it is known to have.

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
		passenger.CurrentStatus = "OnBoard";
		passenger.current_JourneyID = "j";
		passenger.current_TripID = "j.1";
		passenger.Current_Train_Boarded = "T1";
		passenger.Current_Arrival_Station = "C";
		run.passengers.push_back(passenger);
		flow.build(run.passengers, true);
	}

	const Passenger& passenger() const { return run.passengers.front(); }
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
	void trainThatStandsPastItsDepartureIsAsLateAsItStands();
	void trainDoesNotHaltAtTheStopWhereItEnters();
	void trainThatEndsEarlySetsDownPassengerWhoFindsNoRoute();
	void trainThatEndsEarlySetsDownPassengerWhoContinuesOnAnother();
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
	QVERIFY(passenger.IsIntheNetwork);
	QCOMPARE(passenger.CurrentStatus, std::string("OnPlatform"));
	QCOMPARE(passenger.Current_Train_To_Wait, std::string("T2"));
	QCOMPARE(passenger.Current_WaitingStationID, std::string("B"));
	QCOMPARE(ended.flow.waitingCount("B", "B.platform.1"), 1);
}

QTEST_APPLESS_MAIN(TestPassengerJourneys)
#include "test_passengerjourneys.moc"
