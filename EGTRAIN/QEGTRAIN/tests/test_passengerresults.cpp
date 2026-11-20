// The passenger result files PassengerJourneys.txt, StationStops.txt and ODSummary.txt (simulation/PassengerResults.h): the
// counts of the passenger step at a call, and the statistics of the origin-destination summary.

#include "simulation/PassengerFlow.h"
#include "simulation/PassengerResults.h"
#include "simulation/RollingStock.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationRun.h"
#include "util/Log.h"

#include <QTest>

#include <sstream>

namespace {

// The lines of a result file.
std::vector<std::string> linesOf(const std::string& text) {
	std::vector<std::string> lines;
	std::istringstream in(text);
	std::string line;
	while (std::getline(in, line))
		lines.push_back(line);
	return lines;
}

// A journey from origin to destination with the given waiting time that completed at the arrival time after it appeared at 100.
Journey completedJourney(const std::string& origin, const std::string& destination, double wait, double arrival, int delay) {
	Journey journey;
	journey.ID = "j";
	journey.Dep_Station_ID = origin;
	journey.Arr_Station_ID = destination;
	journey.IsJourneyStarted = journey.IsJourneyCompleted = true;
	journey.Outcome = JourneyOutcome::Completed;
	journey.Actual_Departure_Time = 100;
	journey.Actual_Arrival_Time = arrival;
	journey.Waitingtime = wait;
	journey.totalJourneyArrivalDelay = delay;
	return journey;
}

Passenger passengerWith(const std::string& id, const Journey& journey) {
	Passenger passenger;
	passenger.ID = id;
	passenger.Journeys.push_back(journey);
	return passenger;
}

// A passenger at the station with a journey of one leg on train T1 to B, on the platform for it or on board.
Passenger riderOfT1(const std::string& id, const std::string& station, PassengerStatus status) {
	Passenger passenger;
	passenger.ID = id;
	Journey journey;
	journey.ID = id + ".j";
	journey.Dep_Station_ID = status == PassengerStatus::OnBoard ? "Z" : station;
	journey.Arr_Station_ID = status == PassengerStatus::OnBoard ? station : "B";
	journey.N_Trips = 1;
	Trip trip;
	trip.TripID = id + ".t";
	trip.JourneyID = journey.ID;
	trip.Dep_Station_ID = journey.Dep_Station_ID;
	trip.Arr_Station_ID = journey.Arr_Station_ID;
	trip.TrainServiceDescription = "T1";
	trip.IsTripStarted = true;
	trip.Actual_Departure_Time = 0;
	journey.Trips.push_back(trip);
	journey.IsJourneyStarted = true;
	passenger.Journeys.push_back(journey);
	passenger.IsIntheNetwork = true;
	passenger.CurrentStatus = status;
	passenger.current_JourneyID = journey.ID;
	passenger.current_TripID = trip.TripID;
	if (status == PassengerStatus::OnBoard) {
		passenger.Current_Train_Boarded = "T1";
		passenger.Current_Arrival_Station = station;
	} else {
		passenger.Current_Train_To_Wait = "T1";
		passenger.Current_WaitingStationID = station;
		passenger.Current_WaitingStationPlatformID = "A.platform.1";
		passenger.Current_Arrival_Station = "B";
	}
	return passenger;
}
} // namespace

class TestPassengerResults : public QObject {
	Q_OBJECT

private slots:
	void callCountsBoardedAlightedLeftBehindAndLoad();
	void stopRowTakesTimesFromTheTrain();
	void percentileIsNearestRank();
	void summaryAveragesCompletedJourneysOnly();
	void journeyRowListsRiddenLegs();
};

void TestPassengerResults::callCountsBoardedAlightedLeftBehindAndLoad() {
	eglog::setSink([](eglog::Category, eglog::Level, const std::string&) {});
	const auto restoreLog = qScopeGuard([] { eglog::setSink({}); });
	SimulationRun run;
	run.settings.PAX_GUI = false;
	Train train;
	train.trainDescription = "T1";
	train.numStations = 1;
	train.Stations.resize(1);
	train.Stations[0].stationName = "A";
	train.Stations[0].stationPlatformId = "A.platform.1";
	train.Stations[0].StepStopped = 2; // not the first step of the stand, which would apply the dwell model
	train.StoppedForServiceStop = true;
	train.CurrentServiceStop = "A";
	train.CurrentServiceStopPlatform = "A.platform.1";
	train.MAX_OnBoard_Passengers = 1;
	train.Current_OnBoard_Passengers = 1;
	run.trains.push_back(train);
	run.numTrains = 1;
	StationPlatform platform;
	platform.StationID = "A";
	platform.ID = "A.platform.1";
	const std::list<StationPlatform> platforms = {platform};
	// One passenger on board leaves at A; of the two who wait, one finds room and the other is left behind.
	std::list<Passenger> passengers = {riderOfT1("alights", "A", PassengerStatus::OnBoard), riderOfT1("boards", "A", PassengerStatus::OnPlatform),
		riderOfT1("refused", "A", PassengerStatus::OnPlatform)};

	Simulate_Train_Passenger_Interactions(run, 100, 0, run.trains[0], passengers, platforms);

	const StopCallCounts* counts = run.stopCalls.find(0, 0);
	QVERIFY(counts != nullptr);
	QCOMPARE(counts->alighted, 1);
	QCOMPARE(counts->boarded, 1);
	QCOMPARE(counts->leftBehind, 1);
	QCOMPARE(counts->loadAfter, 1);
	QVERIFY(run.stopCalls.find(0, 1) == nullptr);
	run.stopCalls.clear();
	QVERIFY(run.stopCalls.find(0, 0) == nullptr);
}

void TestPassengerResults::stopRowTakesTimesFromTheTrain() {
	SimulationRun run;
	run.settings.startingSimulationTime = 1000;
	Train train;
	train.trainDescription = "T1";
	train.numStations = 3;
	train.Stations.resize(3); // the train passes C without standing
	for (int stop = 0; stop < 3; ++stop) {
		train.Stations[static_cast<std::size_t>(stop)].stationName = std::string(1, static_cast<char>('A' + stop));
		train.Stations[static_cast<std::size_t>(stop)].stationPlatformId = "P";
	}
	train.MAX_OnBoard_Passengers = 4;
	train.StationArrivals[0] = 20; // the timetable point is passed before the train stands
	train.StationStandStarts[0] = 50;
	train.StationDepartures[0] = 80;
	train.StationStandStarts[1] = 200; // stood at B and had not left by the end
	train.Stations[0].StepStopped = train.Stations[1].StepStopped = 31;
	train.StationArrivals[2] = 300;
	run.trains.push_back(train);
	run.numTrains = 1;
	StopCallCounts& first = run.stopCalls.at(0, 0);
	first.boarded = 3;
	first.alighted = 1;
	first.leftBehind = 2;
	first.loadAfter = 3;

	const std::vector<std::string> lines = linesOf(stationStopsText(run));
	QCOMPARE(lines.size(), std::size_t(3));
	QCOMPARE(lines[0], std::string("Train\tStation\tPlatform\tArrival\tDeparture\tBoarded\tAlighted\tLeftBehind\tLoadAfterDeparture\tCapacity\tLoadFactor"));
	QCOMPARE(lines[1], std::string("T1\tA\tP\t1050\t1080\t3\t1\t2\t3\t4\t0.750"));
	QCOMPARE(lines[2], std::string("T1\tB\tP\t1200\t-1\t0\t0\t0\t0\t4\t0.000"));
}

void TestPassengerResults::percentileIsNearestRank() {
	std::vector<double> twenty;
	for (int value = 20; value >= 1; --value)
		twenty.push_back(value);
	QCOMPARE(nearestRankPercentile(twenty, 95), 19.0);
	QCOMPARE(nearestRankPercentile({7, 3, 5, 9}, 95), 9.0);
	QCOMPARE(nearestRankPercentile({42}, 95), 42.0);
}

void TestPassengerResults::summaryAveragesCompletedJourneysOnly() {
	Journey lost = completedJourney("B", "A", 0, 0, 0);
	lost.IsJourneyCompleted = false;
	lost.Outcome = JourneyOutcome::NoRoute;
	Journey pending = completedJourney("A", "B", 0, 0, 0);
	pending.IsJourneyCompleted = false;
	pending.Outcome = JourneyOutcome::InProgress;
	const std::list<Passenger> passengers = {
		passengerWith("p1", completedJourney("A", "B", 10, 400, 5)),
		passengerWith("p2", completedJourney("A", "B", 20, 500, -15)),
		passengerWith("p3", pending),
		passengerWith("p4", lost),
	};

	const std::vector<std::string> lines = linesOf(originDestinationSummaryText(passengers));
	QCOMPARE(lines.size(), std::size_t(3));
	QCOMPARE(lines[0], std::string("Origin\tDestination\tJourneys\tCompleted\tNoRoute\tMeanWait\tP95Wait\tMeanTravel\tMeanDelay"));
	QCOMPARE(lines[1], std::string("A\tB\t3\t2\t0\t15.000\t20\t350.000\t-5.000"));
	QCOMPARE(lines[2], std::string("B\tA\t1\t0\t1\t-\t-\t-\t-"));
}

void TestPassengerResults::journeyRowListsRiddenLegs() {
	Journey journey = completedJourney("A", "C", 30, 700, 12);
	journey.Mode = JourneyMode::Live;
	journey.IsReplanned = true;
	journey.Walkingtime = 45;
	for (const char* train : {"T1", "T2"}) {
		Trip trip;
		trip.TrainServiceDescription = train;
		trip.IsTripCompleted = true;
		trip.Actual_Boarding_Time = std::string(train) == "T1" ? 200 : 400;
		trip.Actual_Arrival_Time = std::string(train) == "T1" ? 300 : 650;
		journey.Trips.push_back(trip);
	}
	Journey waiting = completedJourney("A", "C", 0, 0, 0);
	waiting.ID = "k";
	waiting.IsJourneyStarted = waiting.IsJourneyCompleted = false;
	waiting.Outcome = JourneyOutcome::InProgress;
	const std::list<Passenger> passengers = {passengerWith("p1", journey), passengerWith("p2", waiting)};

	const std::vector<std::string> lines = linesOf(passengerJourneysText(passengers));
	QCOMPARE(lines.size(), std::size_t(3));
	QCOMPARE(lines[1], std::string("p1\tj\tlive\tA\tC\t100\tT1+T2\t30\t45\t350\t700\t12\t1\tcompleted"));
	QCOMPARE(lines[2], std::string("p2\tk\tassigned\tA\tC\t-1\t-\t0\t0\t0\t-\t-\t0\tin_progress"));
}

QTEST_APPLESS_MAIN(TestPassengerResults)
#include "test_passengerresults.moc"
