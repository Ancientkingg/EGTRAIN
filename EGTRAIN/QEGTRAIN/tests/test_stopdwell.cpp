// Length of a stop with and without passengers.
//
// A train of the scene tests/fixtures/scenes/crossovers stands at its last stop. The test counts the steps it stands
// there, in the order of a step of the simulation: the movement of the train, then the passenger phase that may lengthen
// the stop. The planned dwell is set below and above the dwell that one boarding passenger needs, with nobody waiting and
// with one passenger waiting. The stop lasts the planned dwell unless the passengers need longer. A step is one second.

#include "scene/SceneModel.h"
#include "simulation/Infrastructure.h"
#include "simulation/Passengers.h"
#include "simulation/RollingStock.h"
#include "simulation/Signalling.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationRun.h"
#include "util/Log.h"

#include <QTest>

#include <cmath>
#include <list>
#include <string>

static bool near(double a, double b) {
	return std::fabs(a - b) < 1e-4;
}

namespace {

constexpr int kFirstStep = 2;
constexpr int kLastStep = 400;

// The train stands at the last stop of its service from step 2 and has no planned departure there.
Train trainAtLastStop(const Train& native, double plannedDwell) {
	Train service = native;
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

std::list<Passenger> waitingFor(const Train& train, int count) {
	std::list<Passenger> passengers;
	for (int index = 0; index < count; ++index) {
		Passenger passenger;
		passenger.ID = "P" + std::to_string(index);
		passenger.CurrentStatus = "OnPlatform";
		passenger.Current_Train_To_Wait = train.trainDescription;
		passenger.Current_WaitingStationID = train.Stations[train.numStations - 1].stationName;
		passengers.push_back(passenger);
	}
	return passengers;
}

// The number of steps at which the train stands at the stop, or -1 when it does not leave.
int lengthOfStop(const Train& native, double plannedDwell, int waiting) {
	Train service = trainAtLastStop(native, plannedDwell);
	const Node& stop = service.Stations[service.numStations - 1];
	std::list<Passenger> passengers = waitingFor(service, waiting);
	std::list<StationPlatform> platforms;
	StationPlatform platform;
	platform.StationID = stop.stationName;
	platform.ID = stop.stationPlatformId;
	for (const Passenger& passenger : passengers)
		platform.Current_List_Pax_On_Platform.emplace_back(passenger.ID, 0.0);
	platforms.push_back(platform);

	for (int step = kFirstStep; step < kLastStep; ++step) {
		service.trajectoryComputationIncludingMovingBlock(step, signalCode1, signalCode2, signalCode3);
		Simulate_Train_Passenger_Interactions(defaultRun(), step, 0, service, passengers, platforms);
		if (!service.StoppedForServiceStop)
			return step - kFirstStep;
	}
	return -1;
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

QTEST_APPLESS_MAIN(TestStopDwell)
#include "test_stopdwell.moc"
