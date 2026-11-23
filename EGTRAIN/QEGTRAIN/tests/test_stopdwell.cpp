// Length of a stop with and without passengers.
//
// A train of the scene tests/fixtures/scenes/crossovers stands at its last stop. The test counts the steps it stands
// there, in the order of a step of the simulation: the movement of the train, then the passenger phase that may lengthen
// the stop. The planned dwell is set below and above the dwell that one boarding passenger needs, with nobody waiting and
// with one passenger waiting. The stop lasts the planned dwell unless the passengers need longer. A step is one second.

#include "scene/SceneModel.h"
#include "simulation/Infrastructure.h"
#include "simulation/NumberGenerator.h"
#include "simulation/Passengers.h"
#include "simulation/RollingStock.h"
#include "simulation/Signalling.h"
#include "simulation/PassengerFlow.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationRun.h"
#include "support/PassengerStep.h"
#include "util/Log.h"

#include <QTest>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <list>
#include <string>

static bool near(double a, double b) {
	return std::fabs(a - b) < 1e-4;
}

namespace {

constexpr int kFirstStep = 2;
constexpr int kLastStep = 400;

// The train entered at step 1, stands at the last stop of its service from step 2 and has no planned departure there.
Train trainAtLastStop(const Train& native, double plannedDwell) {
	Train service = native;
	service.departure_time = 1;
	const int station = service.numStations - 1;
	service.Stations[station].StepStopped = 0;
	service.Stations[station].StopTime = plannedDwell;
	service.ScheduledDepartures[station] = -1;
	service.stop = 1;
	service.instant_spatial_position[1] = service.stationRoutePositionMeters(station) - 0.002;
	service.instant_train_speed[1] = 0;
	service.trainDescription = "T1";
	return service;
}

// Passengers who wait at the last stop of the train for it, with a journey from there to the stop before it.
std::list<Passenger> waitingFor(const Train& train, int count) {
	std::list<Passenger> passengers;
	const Node& stop = train.Stations[train.numStations - 1];
	for (int index = 0; index < count; ++index) {
		Passenger passenger;
		passenger.ID = "P" + std::to_string(index);
		passenger.IsIntheNetwork = true;
		passenger.CurrentStatus = PassengerStatus::OnPlatform;
		passenger.Current_Train_To_Wait = train.trainDescription;
		passenger.Current_WaitingStationID = stop.stationName;
		passenger.Current_WaitingStationPlatformID = stop.stationPlatformId;
		Journey journey;
		journey.ID = passenger.ID + ".j";
		journey.Dep_Station_ID = stop.stationName;
		journey.Arr_Station_ID = train.Stations[0].stationName;
		journey.N_Trips = 1;
		journey.IsJourneyStarted = true;
		Trip trip;
		trip.TripID = passenger.ID + ".t";
		trip.JourneyID = journey.ID;
		trip.Dep_Station_ID = stop.stationName;
		trip.Arr_Station_ID = journey.Arr_Station_ID;
		trip.Dep_Station_Platform_ID = stop.stationPlatformId;
		trip.TrainServiceDescription = train.trainDescription;
		trip.IsTripStarted = true;
		trip.Actual_Departure_Time = 0;
		journey.Trips.push_back(trip);
		passenger.Journeys.push_back(journey);
		passenger.current_JourneyID = journey.ID;
		passenger.current_TripID = trip.TripID;
		passenger.Current_Arrival_Station = journey.Arr_Station_ID;
		passengers.push_back(passenger);
	}
	return passengers;
}

// The number of steps at which the train stands at the stop, or -1 when it does not leave. The train is the only train of the run.
int lengthOfStop(const Train& native, double plannedDwell, int waiting) {
	Train service = trainAtLastStop(native, plannedDwell);
	const Node& stop = service.Stations[service.numStations - 1];
	StationPlatform platform;
	platform.StationID = stop.stationName;
	platform.ID = stop.stationPlatformId;
	std::list<Passenger> passengers = waitingFor(service, waiting);
	for (const Passenger& passenger : passengers)
		platform.Current_List_Pax_On_Platform.emplace_back(passenger.ID, 0.0);
	PassengerStepScope scope(defaultRun());
	scope.set({service}, std::move(passengers), {platform});
	defaultRun().settings.startingSimulationTime = 0;
	Train& moving = defaultRun().trains[0];

	for (int step = kFirstStep; step < kLastStep; ++step) {
		moving.trajectoryComputationIncludingMovingBlock(step);
		simulateTrainPassengerInteractions(defaultRun(), step);
		if (!moving.StoppedForServiceStop)
			return step - kFirstStep;
	}
	return -1;
}

// A passenger on board of the train who alights at its last stop and waits there, on the platform of the train, for a train T2
// to the first stop.
Passenger changingAtLastStop(const Train& train) {
	const Node& stop = train.Stations[train.numStations - 1];
	Passenger passenger;
	passenger.ID = "changer";
	passenger.IsIntheNetwork = true;
	passenger.CurrentStatus = PassengerStatus::OnBoard;
	passenger.Current_Train_Boarded = train.trainDescription;
	passenger.Current_Arrival_Station = stop.stationName;
	Journey journey;
	journey.ID = "changer.j";
	journey.Dep_Station_ID = train.Stations[1].stationName;
	journey.Arr_Station_ID = train.Stations[0].stationName;
	journey.N_Trips = 2;
	journey.IsJourneyStarted = true;
	Trip arriving;
	arriving.TripID = "changer.1";
	arriving.JourneyID = journey.ID;
	arriving.Dep_Station_ID = journey.Dep_Station_ID;
	arriving.Arr_Station_ID = stop.stationName;
	arriving.Arr_Station_Platform_ID = stop.stationPlatformId;
	arriving.TrainServiceDescription = train.trainDescription;
	arriving.IsTripStarted = true;
	Trip onward;
	onward.TripID = "changer.2";
	onward.JourneyID = journey.ID;
	onward.Dep_Station_ID = stop.stationName;
	onward.Dep_Station_Platform_ID = stop.stationPlatformId;
	onward.Arr_Station_ID = journey.Arr_Station_ID;
	onward.TrainServiceDescription = "T2";
	journey.Trips = {arriving, onward};
	passenger.Journeys.push_back(journey);
	passenger.current_JourneyID = journey.ID;
	passenger.current_TripID = arriving.TripID;
	return passenger;
}

// The dwell that the passengers give the train at its last stop when one passenger boards and one alights who then waits on
// the platform of the train for another train. The platform has room for `capacity` passengers.
double dwellWithChangeOnPlatform(const Train& native, int capacity) {
	Train service = trainAtLastStop(native, 0.0);
	service.Current_OnBoard_Passengers = 1;
	const int station = service.numStations - 1;
	StationPlatform platform;
	platform.StationID = service.Stations[station].stationName;
	platform.ID = service.Stations[station].stationPlatformId;
	platform.Max_Passenger_Volume = capacity;
	std::list<Passenger> passengers = waitingFor(service, 1);
	platform.Current_List_Pax_On_Platform.emplace_back(passengers.front().ID, 0.0);
	passengers.push_back(changingAtLastStop(service));
	PassengerStepScope scope(defaultRun());
	scope.set({service}, std::move(passengers), {platform});
	defaultRun().settings.startingSimulationTime = 0;
	Train& moving = defaultRun().trains[0];
	moving.trajectoryComputationIncludingMovingBlock(kFirstStep);
	simulateTrainPassengerInteractions(defaultRun(), kFirstStep);
	return moving.Stations[station].StopTime;
}

// The dwell of the passengers when one passenger boards: the fixed time of the stop and the time of that passenger at
// the one door that is used.
double boardingOnlyDwell(const Train& train) {
	const float* beta = train.GibsonDwellTimeParameters;
	return static_cast<double>(beta[0]) + beta[2];
}

const Train& nativeTrain() {
	return defaultRun().trains[0];
}

} // namespace

class TestStopDwell : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void passengerDwell();
	void stopLength_data();
	void stopLength();
	void morePassengersLengthenStop();
	void passengerWhoChangesOnThePlatformCountsOnce();
	void stationPositionsCacheMatchesComputation();
	void fractionalWagonCount_data();
	void fractionalWagonCount();

private:
	// The dwell that one boarding passenger needs (25 s with the parameters of the builder), and planned dwells below and
	// above it.
	int m_needed = 0;
	int m_below = 0;
	int m_above = 0;
};

void TestStopDwell::initTestCase() {
	// The scene prepares a run.
	const auto loaded = loadScene(EGTRAIN_TEST_SOURCE_DIR "/fixtures/scenes/crossovers");
	QVERIFY(!hasErrors(loaded.diagnostics));
	QVERIFY(!hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), loaded.scene)));
	QVERIFY(!hasErrors(buildOperationsFromScene(defaultRun(), loaded.scene, "baseline", {{"F1", 1}})));
	QCOMPARE(defaultRun().numTrains, 1);
	defaultRun().settings.PAX_GUI = true;
	eglog::setSink([](eglog::Category, eglog::Level, const std::string&) {});
	m_needed = static_cast<int>(boardingOnlyDwell(nativeTrain()));
	m_below = m_needed - 15;
	m_above = m_needed + 15;
}

// The dwell of the passengers alone: nobody gives none, one passenger gives the fixed time of the stop and the time of
// that passenger at the one door that is used.
void TestStopDwell::passengerDwell() {
	Train formula = nativeTrain();
	formula.Current_OnBoard_Passengers = 0;
	const float* beta = formula.GibsonDwellTimeParameters;
	const auto dwell = [&formula, beta](int boarding, int alighting) {
		return formula.computePaxDependentDwellTimeAtStations(boarding, alighting, 0.0, beta[0], beta[1], beta[2], beta[3], beta[4], beta[5], beta[6], beta[7]);
	};
	const double boardingOnly = boardingOnlyDwell(formula);
	const double alightingOnly = static_cast<double>(beta[0]) + beta[5] * std::exp(-static_cast<double>(beta[6]));
	QCOMPARE_EQ(dwell(0, 0), 0.0);
	QVERIFY2(near(dwell(1, 0), boardingOnly), "one boarding passenger: the fixed time and one boarding time");
	QVERIFY2(near(dwell(0, 1), alightingOnly), "one alighting passenger: the fixed time and one alighting time");
	// More passengers need a longer dwell.
	QCOMPARE_GT(dwell(80, 0), dwell(1, 0));
	QCOMPARE_GT(dwell(0, 80), dwell(0, 1));
}

void TestStopDwell::stopLength_data() {
	QTest::addColumn<int>("planned");
	QTest::addColumn<int>("waiting");
	QTest::addColumn<int>("expected");
	QTest::newRow("nobody waits, planned dwell below the passenger dwell: the planned dwell") << m_below << 0 << m_below;
	QTest::newRow("nobody waits, planned dwell above the passenger dwell: the planned dwell") << m_above << 0 << m_above;
	QTest::newRow("one passenger boards, planned dwell below the passenger dwell: the passenger dwell") << m_below << 1 << m_needed;
	QTest::newRow("one passenger boards, planned dwell above the passenger dwell: the planned dwell") << m_above << 1 << m_above;
}

void TestStopDwell::stopLength() {
	QFETCH(int, planned);
	QFETCH(int, waiting);
	QFETCH(int, expected);
	QCOMPARE(lengthOfStop(nativeTrain(), planned, waiting), expected);
}

void TestStopDwell::morePassengersLengthenStop() {
	QCOMPARE_GT(lengthOfStop(nativeTrain(), m_below, 8), m_needed);
}

// One passenger waits for the train and boards. One alights and waits on the same platform for another train. The platform
// holds 2 passengers then: the one who waited and the one from the train. That is half of a platform for 4, under the
// crowding threshold, so the dwell is that of a platform with room to spare. A platform for 2 is crowded and lengthens it.
void TestStopDwell::passengerWhoChangesOnThePlatformCountsOnce() {
	const double roomy = dwellWithChangeOnPlatform(nativeTrain(), 1000);
	QCOMPARE_GT(roomy, 0.0);
	QVERIFY2(near(dwellWithChangeOnPlatform(nativeTrain(), 4), roomy), "2 passengers do not crowd a platform for 4");
	QCOMPARE_GT(dwellWithChangeOnPlatform(nativeTrain(), 2), roomy);
}

// Within a step of a run the route position of a stop can come from the cache of the train. It equals the computation for every
// stop on both routes of the scene, also after a change of the route, of the X of a stop or of the arrival name of a stop at X 0.
void TestStopDwell::stationPositionsCacheMatchesComputation() {
	Train train = nativeTrain();
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t{2});
	// The first stop, A, lies at X 0, so the cache does not keep its position.
	QCOMPARE(train.numStations, 3);
	QCOMPARE_EQ(train.Stations[0].X, 0.0);
	QVERIFY(train.Stations[1].X != 0 && train.Stations[2].X != 0);
	// Every stop, and an index on either side, gives the result of the computation, bit for bit.
	const auto matches = [&train] {
		for (int stop = -1; stop <= train.numStations; ++stop) {
			const double cached = train.stationRoutePositionMeters(stop);
			const double computed = train.uncachedStationRoutePositionMeters(stop);
			if (std::memcmp(&cached, &computed, sizeof(double)) != 0)
				return false;
		}
		return true;
	};
	// The number of stops whose position the cache holds now.
	const auto held = [&train] {
		int count = 0;
		for (int stop = 0; stop < train.numStations; ++stop) {
			double position = 0.0;
			count += train.stepCache.findStationPosition(stop, train.indexOfRoute, train.Stations[stop].X, position) ? 1 : 0;
		}
		return count;
	};

	int step = 1;
	for (int route : {0, 1, 0}) {
		// A run does not change the route of a train, but the route is part of the key.
		train.indexOfRoute = route;
		train.stepCache.beginStep(step++);
		QCOMPARE(held(), 0);
		QVERIFY(matches());
		QCOMPARE(held(), 2);
		train.stepCache.endStep();
		// The positions are kept while each step follows the one before.
		train.stepCache.beginStep(step++);
		QCOMPARE(held(), 2);
		QVERIFY(matches());
		train.stepCache.endStep();
	}
	// The X of a stop is part of the key.
	const double second = train.uncachedStationRoutePositionMeters(1);
	train.Stations[1].X += 0.05;
	QVERIFY(train.uncachedStationRoutePositionMeters(1) != second);
	train.stepCache.beginStep(step++);
	QCOMPARE(held(), 1);
	QVERIFY(matches());
	// A stop at X 0 takes its position from the stations of the run by its arrival name, which an arrival can change.
	const double byName = train.uncachedStationRoutePositionMeters(0);
	train.StationArrivalNames[0] = train.Stations[2].stationName;
	QVERIFY(train.uncachedStationRoutePositionMeters(0) != byName);
	QVERIFY(matches());
	train.stepCache.endStep();
	// Outside a step every position is computed, and a step that does not follow the last one starts empty.
	QCOMPARE(held(), 0);
	QVERIFY(matches());
	train.stepCache.beginStep(0);
	QCOMPARE(held(), 0);
	train.stepCache.endStep();
}

void TestStopDwell::fractionalWagonCount_data() {
	QTest::addColumn<double>("wagons");
	QTest::newRow("half a wagon: the two doors of the power unit") << 0.5;
	QTest::newRow("two and a half wagons: the six doors of three cars") << 2.5;
}

// A scene can give a unit a fractional number of wagons. The passengers are split over the power unit and the whole
// wagons, two doors to a car, so the fraction adds no door: one boarding passenger gives the fixed time and one boarding
// time, and a larger flow gives the dwell of the whole wagons.
void TestStopDwell::fractionalWagonCount() {
	QFETCH(double, wagons);
	// The two dwells of a comparison draw the same split of the passengers at the doors.
	const NumberGenerator draws = defaultRun().numberGenerator;
	const auto dwell = [&draws](double count, int boarding, int alighting) {
		Train formula = nativeTrain();
		formula.number_of_wagons = count;
		formula.Current_OnBoard_Passengers = 0;
		defaultRun().numberGenerator = draws;
		const float* beta = formula.GibsonDwellTimeParameters;
		return formula.computePaxDependentDwellTimeAtStations(boarding, alighting, 0.0, beta[0], beta[1], beta[2], beta[3], beta[4], beta[5], beta[6], beta[7]);
	};
	QCOMPARE(dwell(wagons, 1, 0), boardingOnlyDwell(nativeTrain()));
	QCOMPARE_EQ(dwell(wagons, 40, 20), dwell(std::floor(wagons), 40, 20));
}

QTEST_APPLESS_MAIN(TestStopDwell)
#include "test_stopdwell.moc"
