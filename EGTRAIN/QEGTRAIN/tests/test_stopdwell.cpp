// Length of a stop with and without passengers.
//
//   test_stopdwell SCENE_DIR
//
// A train of the scene stands at its last stop. The test counts the steps it stands there, in the order of a step of the
// simulation: the movement of the train, then the passenger phase that may lengthen the stop. The planned dwell is set
// below and above the dwell that one boarding passenger needs, with nobody waiting and with one passenger waiting.
// The stop lasts the planned dwell unless the passengers need longer. A step is one second.

#include "scene/SceneModel.h"
#include "simulation/Infrastructure.h"
#include "simulation/Passengers.h"
#include "simulation/RollingStock.h"
#include "simulation/Signalling.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationRun.h"
#include "util/Log.h"

#include <cmath>
#include <iostream>
#include <list>
#include <string>

Logger owl;

static bool expect(bool condition, const std::string& message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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

} // namespace

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "usage: test_stopdwell SCENE_DIR\n";
		return 2;
	}
	const auto loaded = loadScene(argv[1]);
	if (hasErrors(loaded.diagnostics) || hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), loaded.scene))
		|| hasErrors(buildOperationsFromScene(defaultRun(), loaded.scene, "baseline", {{"F1", 1}})) || numRegions != 1) {
		std::cerr << "failed: the scene does not prepare a run\n";
		return 1;
	}
	defaultRun().settings.PAX_GUI = true;
	const Train& native = regional_train[0];
	bool ok = true;
	eglog::setSink([](eglog::Category, eglog::Level, const std::string&) {});

	// The dwell of the passengers alone: nobody gives none, one passenger gives the fixed time of the stop and the
	// time of that passenger at the one door that is used.
	Train formula = native;
	formula.Current_OnBoard_Passengers = 0;
	const float* beta = formula.GibsonDwellTimeParameters;
	const auto dwell = [&formula, beta](int boarding, int alighting) {
		return formula.computePaxDependentDwellTimeAtStations(boarding, alighting, 0.0, beta[0], beta[1], beta[2], beta[3], beta[4], beta[5], beta[6], beta[7]);
	};
	const double boardingOnly = static_cast<double>(beta[0]) + beta[2];
	const double alightingOnly = static_cast<double>(beta[0]) + beta[5] * std::exp(-static_cast<double>(beta[6]));
	ok &= expect(dwell(0, 0) == 0.0, "nobody boards or alights: no dwell of the passengers");
	ok &= expect(near(dwell(1, 0), boardingOnly), "one boarding passenger: the fixed time and one boarding time");
	ok &= expect(near(dwell(0, 1), alightingOnly), "one alighting passenger: the fixed time and one alighting time");
	ok &= expect(dwell(80, 0) > dwell(1, 0) && dwell(0, 80) > dwell(0, 1), "more passengers need a longer dwell");

	// Planned dwells below and above the dwell that one boarding passenger needs (25 s with the parameters of the builder).
	const int needed = static_cast<int>(boardingOnly);
	const int below = needed - 15;
	const int above = needed + 15;
	ok &= expect(lengthOfStop(native, below, 0) == below, "nobody waits, planned dwell below the passenger dwell: the planned dwell");
	ok &= expect(lengthOfStop(native, above, 0) == above, "nobody waits, planned dwell above the passenger dwell: the planned dwell");
	ok &= expect(lengthOfStop(native, below, 1) == needed, "one passenger boards, planned dwell below the passenger dwell: the passenger dwell");
	ok &= expect(lengthOfStop(native, above, 1) == above, "one passenger boards, planned dwell above the passenger dwell: the planned dwell");
	ok &= expect(lengthOfStop(native, below, 8) > needed, "more passengers lengthen the stop");

	return ok ? 0 : 1;
}
