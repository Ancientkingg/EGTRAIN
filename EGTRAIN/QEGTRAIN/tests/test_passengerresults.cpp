// The passenger result files PassengerJourneys.txt, StationStops.txt and ODSummary.txt (simulation/PassengerResults.h): the
// counts of the passenger step at a call, and the statistics of the origin-destination summary.

#include "simulation/PassengerFlow.h"
#include "simulation/PassengerResults.h"
#include "simulation/RollingStock.h"
#include "simulation/SimulationRun.h"
#include "support/PassengerStep.h"
#include "util/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <sstream>
#include <stdexcept>

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
	if (status == PassengerStatus::OnPlatform)
		trip.Dep_Station_Platform_ID = station + ".platform.1";
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
// The platform of the station.
StationPlatform platformAt(const std::string& station) {
	StationPlatform platform;
	platform.StationID = station;
	platform.ID = station + ".platform.1";
	return platform;
}

// A passenger outside the network whose journey, from A to B on train T1, starts at this time.
Passenger passengerStartingAt(int start) {
	Passenger passenger = riderOfT1("late", "A", PassengerStatus::OnPlatform);
	Journey& journey = passenger.Journeys.front();
	Trip& trip = journey.Trips.front();
	trip.IsTripStarted = false;
	trip.Actual_Departure_Time = -9999;
	journey.IsJourneyStarted = false;
	journey.Actual_Planned_Departure_Time = start;
	passenger.IsIntheNetwork = false;
	passenger.CurrentStatus = PassengerStatus::Outside;
	passenger.current_JourneyID = passenger.current_TripID = passenger.Current_Train_To_Wait = kNoneId;
	passenger.Current_WaitingStationID = passenger.Current_WaitingStationPlatformID = passenger.Current_Arrival_Station = kNoneId;
	return passenger;
}

// The passenger step of step t as a run makes it: the passengers whose journey starts enter, then the trains alight and board.
void runStep(SimulationRun& run, int t) {
	enterPassengersAndRefreshPlatforms(run, t);
	simulateTrainPassengerInteractions(run, t);
}
} // namespace

class TestPassengerResults : public QObject {
	Q_OBJECT

private slots:
	void callCountsBoardedAlightedLeftBehindAndLoad();
	void secondCallOfAStationHasItsOwnCounts();
	void leftBehindPassengerIsCountedOncePerCall();
	void stopRowTakesTimesFromTheTrain();
	void latestCallsOfAStationAreNewestFirstAndBounded();
	void percentileIsNearestRank();
	void summaryAveragesCompletedJourneysOnly();
	void journeyRowListsRiddenLegs();
	void runWithoutPassengersWritesOnlyHeaders();
	void stopOfTrainWithoutCapacityHasNoLoadFactor();
	void unwritableFolderThrowsWithThePath();
};

void TestPassengerResults::callCountsBoardedAlightedLeftBehindAndLoad() {
	eglog::setSink([](eglog::Category, eglog::Level, const std::string&) {});
	const auto restoreLog = qScopeGuard([] { eglog::setSink({}); });
	SimulationRun run;
	run.settings.PAX_GUI = false;
	run.settings.startingSimulationTime = 0;
	Train train = trainCalling("T1", {"A", "B"});
	standAtStop(train, 0, 2); // not the first step of the stand, which would apply the dwell model
	train.MAX_OnBoard_Passengers = 1;
	train.Current_OnBoard_Passengers = 1;
	// One passenger on board leaves at A; of the two who wait, one finds room and the other is left behind.
	PassengerStepScope scope(run);
	scope.set({train},
		{riderOfT1("alights", "A", PassengerStatus::OnBoard), riderOfT1("boards", "A", PassengerStatus::OnPlatform),
			riderOfT1("refused", "A", PassengerStatus::OnPlatform)},
		{platformAt("A")});

	runStep(run, 100);

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

// A train calls at A, B and A. A passenger who boards at the second call is counted on that call and not on the first.
void TestPassengerResults::secondCallOfAStationHasItsOwnCounts() {
	eglog::setSink([](eglog::Category, eglog::Level, const std::string&) {});
	const auto restoreLog = qScopeGuard([] { eglog::setSink({}); });
	SimulationRun run;
	run.settings.PAX_GUI = false;
	run.settings.startingSimulationTime = 0;
	Train train = trainCalling("T1", {"A", "B", "A"});
	standAtStop(train, 2, 0); // the calls at A and B are over
	train.StoppedForServiceStop = false;
	train.MAX_OnBoard_Passengers = 4;
	PassengerStepScope scope(run);
	scope.set({train}, {passengerStartingAt(190)}, {platformAt("A")});

	// The train is on its way back to A, and the passenger has not started yet.
	runStep(run, 160);
	standAtStop(run.trains[0], 2, 2);
	runStep(run, 200);

	QCOMPARE(run.stopCalls.at(0, 0).boarded, 0);
	const StopCallCounts* second = run.stopCalls.find(0, 2);
	QVERIFY(second != nullptr);
	QCOMPARE(second->boarded, 1);
}

// A full train leaves behind the passengers who wait for it at each of its calls, and counts each of them once at that call.
void TestPassengerResults::leftBehindPassengerIsCountedOncePerCall() {
	eglog::setSink([](eglog::Category, eglog::Level, const std::string&) {});
	const auto restoreLog = qScopeGuard([] { eglog::setSink({}); });
	SimulationRun run;
	run.settings.PAX_GUI = false;
	run.settings.startingSimulationTime = 0;
	Train train = trainCalling("T1", {"A", "B", "A"});
	standAtStop(train, 0, 2);
	train.MAX_OnBoard_Passengers = 1;
	train.Current_OnBoard_Passengers = 1;
	PassengerStepScope scope(run);
	scope.set({train}, {riderOfT1("refused", "A", PassengerStatus::OnPlatform), passengerStartingAt(190)}, {platformAt("A")});

	for (int t = 100; t < 103; ++t)
		runStep(run, t);
	QCOMPARE(run.stopCalls.at(0, 0).leftBehind, 1);
	// Without another train the passenger who was left behind finds no route and leaves.
	QCOMPARE(run.passengers.front().Journeys.front().Outcome, JourneyOutcome::NoRoute);

	// The train leaves A, calls at B and comes back to A, still full. The passenger who starts now waits for it and is left behind.
	Train& later = run.trains[0];
	later.Stations[0].StepStopped = later.Stations[1].StepStopped = 10;
	later.StationDepartures[0] = 110;
	later.StationDepartures[1] = 150;
	later.Stations[2].StepStopped = 0;
	later.StoppedForServiceStop = false;
	runStep(run, 160);
	standAtStop(later, 2, 1);
	for (int t = 200; t < 203; ++t) {
		runStep(run, t);
		later.Stations[2].StepStopped++;
	}
	QCOMPARE(run.stopCalls.at(0, 0).leftBehind, 1);
	QCOMPARE(run.stopCalls.at(0, 2).leftBehind, 1);
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

void TestPassengerResults::latestCallsOfAStationAreNewestFirstAndBounded() {
	SimulationRun run;
	run.settings.startingSimulationTime = 1000;
	// Two trains call at A five times in all and at B once; the second train has an operating code.
	for (int n = 0; n < 2; ++n) {
		Train train;
		train.trainDescription = n == 0 ? "T1" : "T2";
		train.operatingCode = n == 0 ? "" : "OC2";
		train.numStations = 3;
		train.Stations.resize(3);
		for (int stop = 0; stop < 3; ++stop) {
			train.Stations[static_cast<std::size_t>(stop)].stationName = stop == 1 ? "B" : "A";
			train.Stations[static_cast<std::size_t>(stop)].StepStopped = 5;
		}
		const double starts[2][3] = {{100, 400, 700}, {250, 550, -1}};
		for (int stop = 0; stop < 3; ++stop) {
			train.StationStandStarts[stop] = starts[n][stop];
			train.StationDepartures[stop] = starts[n][stop] >= 0 ? starts[n][stop] + 30 : -1;
		}
		run.trains.push_back(train);
	}
	run.numTrains = 2;
	run.stopCalls.at(0, 2).boarded = 4; // the latest call of A is the third stop of the first train
	run.stopCalls.at(0, 2).leftBehind = 1;
	run.stopCalls.at(0, 2).loadAfter = 9;

	// every call, by arrival
	const std::vector<GuiStationCall> all = stationCalls(run);
	QCOMPARE(all.size(), std::size_t(5));
	for (std::size_t i = 1; i < all.size(); ++i)
		QVERIFY(all[i - 1].arrival <= all[i].arrival);
	QCOMPARE(all[1].train, std::string("OC2"));
	QCOMPARE(all[0].clock, 1100);

	// live: the latest three of A, newest first, and the one of B
	std::vector<GuiStationCall> latest = latestStationCalls(run, 800);
	const auto ofStation = [](const std::vector<GuiStationCall>& calls, const std::string& station) {
		std::vector<GuiStationCall> found;
		for (const GuiStationCall& call : calls)
			if (call.station == station)
				found.push_back(call);
		return found;
	};
	std::vector<GuiStationCall> a = ofStation(latest, "A");
	QCOMPARE(a.size(), std::size_t(3));
	QCOMPARE(a[0].arrival, 700);
	QCOMPARE(a[0].boarded, 4);
	QCOMPARE(a[0].leftBehind, 1);
	QCOMPARE(a[0].load, 9);
	QCOMPARE(a[1].arrival, 250);
	QCOMPARE(a[2].arrival, 100);
	QCOMPARE(ofStation(latest, "B").size(), std::size_t(2));
	// a call that has not begun by the step is not listed
	QCOMPARE(ofStation(latestStationCalls(run, 300), "A").size(), std::size_t(2));

	// replay, from the finished run: the same calls, and the one standing at the step has no counts
	std::vector<GuiStationCall> replayed;
	appendLatestStationCalls(all, 710, replayed);
	a = ofStation(replayed, "A");
	QCOMPARE(a.size(), std::size_t(3));
	QCOMPARE(a[0].arrival, 700);
	QVERIFY(!a[0].counted);
	QVERIFY(a[1].counted);
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

// A run without passengers and trains has a header line and nothing else in each of the three files.
void TestPassengerResults::runWithoutPassengersWritesOnlyHeaders() {
	SimulationRun run;
	PassengerStepScope scope(run);
	const std::string journeysHeader = "PaxID\tJourneyID\tMode\tOrigin\tDestination\tAppeared\tTrains\tWait\tWalk\tInVehicle\tArrival\tDelay\tReplanned\tOutcome\n";
	const std::string stopsHeader = "Train\tStation\tPlatform\tArrival\tDeparture\tBoarded\tAlighted\tLeftBehind\tLoadAfterDeparture\tCapacity\tLoadFactor\n";
	const std::string summaryHeader = "Origin\tDestination\tJourneys\tCompleted\tNoRoute\tMeanWait\tP95Wait\tMeanTravel\tMeanDelay\n";
	QCOMPARE(passengerJourneysText(run.passengers), journeysHeader);
	QCOMPARE(stationStopsText(run), stopsHeader);
	QCOMPARE(originDestinationSummaryText(run.passengers), summaryHeader);

	QTemporaryDir folder;
	QVERIFY(folder.isValid());
	printPassengerResultFiles(run, folder.path().toStdString());
	const auto contentOf = [&folder](const char* name) {
		QFile file(QDir(folder.path()).filePath(QString::fromLatin1(name)));
		return file.open(QIODevice::ReadOnly) ? file.readAll().toStdString() : std::string("<missing>");
	};
	QCOMPARE(contentOf("PassengerJourneys.txt"), journeysHeader);
	QCOMPARE(contentOf("StationStops.txt"), stopsHeader);
	QCOMPARE(contentOf("ODSummary.txt"), summaryHeader);
}

// A train without seats has a stop row with the capacity 0 and a dash for the load factor, not a division by zero.
void TestPassengerResults::stopOfTrainWithoutCapacityHasNoLoadFactor() {
	SimulationRun run;
	run.settings.startingSimulationTime = 0;
	Train train = trainCalling("T1", {"A"});
	train.MAX_OnBoard_Passengers = 0;
	train.Stations[0].StepStopped = 5;
	train.StationStandStarts[0] = 40;
	train.StationDepartures[0] = 60;
	PassengerStepScope scope(run);
	scope.set({train}, {}, {});
	run.stopCalls.at(0, 0).loadAfter = 2;

	const std::vector<std::string> lines = linesOf(stationStopsText(run));
	QCOMPARE(lines.size(), std::size_t(2));
	QCOMPARE(lines[1], std::string("T1\tA\tA.platform.1\t40\t60\t0\t0\t0\t2\t0\t-"));
}

// A folder that does not exist, and a path below a file, end the write with the error that names the file.
void TestPassengerResults::unwritableFolderThrowsWithThePath() {
	SimulationRun run;
	PassengerStepScope scope(run);
	QTemporaryDir folder;
	QVERIFY(folder.isValid());
	const std::string missing = folder.filePath("missing").toStdString();
	try {
		printPassengerResultFiles(run, missing);
		QFAIL("a missing folder must stop the write");
	} catch (const std::runtime_error& error) {
		const std::string message = error.what();
		QVERIFY2(message.rfind("Cannot write " + missing + "/PassengerJourneys.txt: ", 0) == 0, message.c_str());
	}
	QVERIFY(!QFileInfo::exists(QString::fromStdString(missing)));

	QFile blocker(folder.filePath("blocker"));
	QVERIFY(blocker.open(QIODevice::WriteOnly));
	blocker.close();
	const std::string underFile = folder.filePath("blocker").toStdString();
	try {
		printPassengerResultFiles(run, underFile);
		QFAIL("a path below a file must stop the write");
	} catch (const std::runtime_error& error) {
		QVERIFY2(std::string(error.what()).rfind("Cannot write " + underFile + "/PassengerJourneys.txt: ", 0) == 0, error.what());
	}
}

QTEST_APPLESS_MAIN(TestPassengerResults)
#include "test_passengerresults.moc"
