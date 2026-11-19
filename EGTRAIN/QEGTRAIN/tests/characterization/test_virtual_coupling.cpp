// Virtual coupling (signalling level 4) on the line fixture.
//
//   test_virtual_coupling --fixture DIR [--trace] [--trace-level N [--leader KMH]]
//
// A slow leader runs ahead of a faster follower on the one route of the fixture, without stops. At level 4 the follower has to
// catch up, couple, follow the leader at a bounded gap and never overlap it. At level 3 the same scenario is the control: fixed
// blocks never put a train in following mode. The checks cover the cruise of the leader, up to the step in which it starts to
// brake for the end of the route. With --trace the run prints one line per step of the follower; --trace-level N prints it for
// level N, and --leader sets the speed limit of the leader in km/h.

#include "app/DispatchController.h"
#include "simulation/InitialParameters.h"
#include "simulation/Infrastructure.h"
#include "simulation/RollingStock.h"
#include "simulation/Signalling.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationRun.h"
#ifdef signals
#undef signals
#endif

#include <QCoreApplication>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr double kFixtureLengthKm = 16.0;
// The speed limits of the leader in km/h. The follower runs at the 36.1 m/s of the train unit. A leader at 43.2 km/h (12 m/s) is
// slow enough for the follower to couple while both cruise; at 72 km/h (20 m/s) the follower brakes to the leader's speed but stays
// farther behind the authority than the coupling distance.
constexpr double kSlowLeaderKmh = 43.2;
constexpr double kFastLeaderKmh = 72.0;
// The leader has started to brake for the end of the route when its speed is under this share of its cruising speed.
constexpr double kLeaderBrakingShare = 0.999;
// Documented thresholds of level 4: the speed difference to couple and the distance to the authority (docs/guides/signalling-levels.md).
constexpr double kCouplingSpeedDifference = 0.278;
constexpr double kCouplingDistance = 30.0;
// A coupled follower may differ in speed from the leader by this much. The braking that brings it to the coupling point can
// leave it below the leader's speed, and it then follows the leader's acceleration and not its speed until it decouples.
constexpr double kFollowingSpeedTolerance = 5.0;
// Speed difference of a follower that has cruised behind the leader for a long time. A follower that brakes and accelerates
// around the leader's speed stays within this.
constexpr double kSettledSpeedTolerance = 1.5;
constexpr int kSettledSteps = 100;
// The gap between the tail of the leader and the head of the follower stays under this while they cruise.
constexpr double kMaximumCruiseGap = 150.0;

struct Step {
	double leaderPosition = 0.0;
	double leaderSpeed = 0.0;
	double position = 0.0;
	double speed = 0.0;
	bool following = false;
	bool coupling = false;
	bool decoupling = false;
	bool unintentional = false;
	std::string leading;
};

struct Outcome {
	std::vector<std::string> problems;
	std::vector<Step> steps; // Indexed by step; only steps in which both trains run are meaningful.
	int bothFirst = 0;
	int bothLast = -1;
	double leaderLength = 0.0;
	double leaderCruise = 0.0; // Highest speed of the leader.
	int cruiseLast = -1;	   // Last step before the leader brakes for the end of the route.
};

// Runs the leader F1 (limited to leaderLimitKmh) and the follower F2 at one level; the trains have no stop at B.
Outcome runScenario(const std::string& fixture, int level, double leaderLimitKmh) {
	Outcome outcome;
	SceneLoadResult loaded = loadScene(fixture);
	if (hasErrors(loaded.diagnostics)) {
		for (const SceneDiagnostic& diagnostic : loaded.diagnostics)
			outcome.problems.push_back(toDisplayText(diagnostic));
		return outcome;
	}
	for (SceneService& service : loaded.scene.services) {
		if (service.id != "F1" && service.id != "F2")
			continue;
		service.stops.erase(std::remove_if(service.stops.begin(), service.stops.end(), [](const SceneStop& stop) { return stop.stationId == "B"; }),
			service.stops.end());
		// Ample time, so that the timetable never makes a train wait.
		for (SceneStop& stop : service.stops)
			if (stop.hasPlannedArrival)
				stop.plannedArrivalSeconds += 600.0;
		if (service.id == "F1") {
			service.hasMaximumSpeed = true;
			service.maximumSpeedKmh = leaderLimitKmh;
		}
	}
	SceneSignallingArea area;
	area.id = "area.all";
	area.startKm = 0.0;
	area.endKm = kFixtureLengthKm;
	area.level = level;
	loaded.scene.signallingAreas = {area};
	SceneRunSelection selection;
	selection.insert({"F1", 1});
	selection.insert({"F2", 1});

	QTemporaryDir outputDir;
	if (!outputDir.isValid()) {
		outcome.problems.push_back("cannot create a temporary output directory");
		return outcome;
	}
	initial_variables.GUI = 0;
	initial_variables.OutputMainFolder = outputDir.path().toStdString();
	initial_variables.InputMainFolder.clear();

	std::ostringstream sink;
	std::streambuf* saved = std::cout.rdbuf(sink.rdbuf());
	const std::vector<SceneDiagnostic> diagnostics = simulation.prepareScene(loaded.scene, "baseline", selection);
	if (hasErrors(diagnostics)) {
		std::cout.rdbuf(saved);
		for (const SceneDiagnostic& diagnostic : diagnostics)
			outcome.problems.push_back(toDisplayText(diagnostic));
		return outcome;
	}
	outcome.steps.resize(static_cast<std::size_t>(initial_variables.times));
	simulation.setSnapshotsEnabled(true);
	const auto connection = QObject::connect(&simulation, &DispatchController::snapshotAvailable, [&] {
		const auto snapshot = simulation.takeSimulationSnapshot();
		if (!snapshot)
			return;
		const int step = snapshot->timestep;
		if (simulation.run().numTrains != 2)
			return;
		const Train& leader = simulation.run().trains[0];
		const Train& follower = simulation.run().trains[1];
		Step& recorded = outcome.steps[static_cast<std::size_t>(step)];
		recorded.leaderPosition = leader.instant_spatial_position[step];
		recorded.leaderSpeed = leader.instant_train_speed[step];
		recorded.position = follower.instant_spatial_position[step];
		recorded.speed = follower.instant_train_speed[step];
		recorded.following = follower.IsTrainInFollowingMode;
		recorded.coupling = follower.IsTrainCoupling;
		recorded.decoupling = follower.IsTrainDecoupling;
		recorded.unintentional = follower.IsInUnintentionalDecoupling;
		recorded.leading = follower.LeadingTrainInFollowingMode;
	});
	if (timestep == 1.0)
		simulation.runSimulation();
	QObject::disconnect(connection);
	std::cout.rdbuf(saved);
	if (timestep != 1.0) {
		outcome.problems.push_back("the test indexes trajectories by whole seconds; timestep is " + std::to_string(timestep));
		return outcome;
	}
	if (simulation.run().numTrains != 2) {
		outcome.problems.push_back("expected 2 trains, got " + std::to_string(simulation.run().numTrains));
		return outcome;
	}
	const Train& leader = simulation.run().trains[0];
	const Train& follower = simulation.run().trains[1];
	if (leader.trainDescription != "F1-1" || follower.trainDescription != "F2-1") {
		outcome.problems.push_back("the trains of the run are " + leader.trainDescription + " and " + follower.trainDescription);
		return outcome;
	}
	outcome.leaderLength = leader.train_length;
	const auto lastStep = [](const Train& train) {
		const int available = static_cast<int>(std::min(train.instant_spatial_position.size(), train.instant_train_speed.size()));
		return std::min(train.End_Time, available - 1);
	};
	const auto firstStep = [](const Train& train) {
		return std::max(static_cast<int>(train.departure_time), train.earliestActiveTrajectoryIndex);
	};
	outcome.bothFirst = std::max(firstStep(leader), firstStep(follower));
	outcome.bothLast = std::min(lastStep(leader), lastStep(follower));
	for (int t = outcome.bothFirst; t <= outcome.bothLast; ++t)
		outcome.leaderCruise = std::max(outcome.leaderCruise, outcome.steps[static_cast<std::size_t>(t)].leaderSpeed);
	outcome.cruiseLast = outcome.bothFirst;
	for (int t = outcome.bothFirst; t <= outcome.bothLast; ++t) {
		if (outcome.steps[static_cast<std::size_t>(t)].leaderSpeed < kLeaderBrakingShare * outcome.leaderCruise && t > outcome.bothFirst + 1
			&& outcome.steps[static_cast<std::size_t>(t - 1)].leaderSpeed >= kLeaderBrakingShare * outcome.leaderCruise)
			break;
		outcome.cruiseLast = t;
	}
	return outcome;
}

double gapAt(const Outcome& outcome, int t) {
	const Step& step = outcome.steps[static_cast<std::size_t>(t)];
	return step.leaderPosition - outcome.leaderLength - step.position;
}

void printTrace(const Outcome& outcome) {
	for (int t = outcome.bothFirst; t <= outcome.bothLast; ++t) {
		const Step& step = outcome.steps[static_cast<std::size_t>(t)];
		std::cout << "t=" << t << " gap=" << gapAt(outcome, t) << " v=" << step.speed << " vLeader=" << step.leaderSpeed
				  << " follow=" << step.following << " couple=" << step.coupling << " decouple=" << step.decoupling
				  << " unintentional=" << step.unintentional << " leading=" << step.leading << "\n";
	}
}

std::string number(double value) {
	std::ostringstream out;
	out.setf(std::ios::fixed);
	out.precision(3);
	out << value;
	return out.str();
}

// Steps from which the leader has cruised at its speed, and in which the follower runs behind it: the follower is up to speed.
int firstCruiseStep(const Outcome& outcome) {
	int t = outcome.bothFirst;
	while (t <= outcome.cruiseLast && outcome.steps[static_cast<std::size_t>(t)].leaderSpeed < kLeaderBrakingShare * outcome.leaderCruise)
		++t;
	return t;
}

// The first step in which the follower and the leader are both in the network and the leader cruises, to the last cruising step.
void checkNoOverlap(const std::string& assertion, const Outcome& outcome, std::vector<std::string>& failures) {
	for (int t = firstCruiseStep(outcome); t <= outcome.cruiseLast; ++t)
		if (gapAt(outcome, t) < 0.0) {
			failures.push_back(assertion + ": the follower overlaps the leader at step " + std::to_string(t) + ", gap " + number(gapAt(outcome, t)) + " m");
			return;
		}
}

// The follower of the fixed block control never follows.
void checkBlockLevel(const std::string& fixture, int level, std::vector<std::string>& failures) {
	const std::string assertion = "level " + std::to_string(level);
	const Outcome outcome = runScenario(fixture, level, kSlowLeaderKmh);
	for (const std::string& problem : outcome.problems)
		failures.push_back(assertion + ": " + problem);
	if (!outcome.problems.empty())
		return;
	if (outcome.cruiseLast <= firstCruiseStep(outcome)) {
		failures.push_back(assertion + ": the leader never cruises with the follower in the network");
		return;
	}
	checkNoOverlap(assertion, outcome, failures);
	for (int t = outcome.bothFirst; t <= outcome.bothLast; ++t) {
		const Step& step = outcome.steps[static_cast<std::size_t>(t)];
		if (step.following || step.coupling || step.decoupling || step.unintentional || step.leading != "None") {
			failures.push_back(assertion + ": the follower is in virtual coupling state at step " + std::to_string(t));
			break;
		}
	}
}

// A follower that is faster than its leader catches up, couples, follows it at a bounded gap and couples again after an
// unintentional decoupling, without ever overlapping it.
void checkCoupling(const std::string& fixture, bool trace, std::vector<std::string>& failures) {
	const std::string assertion = "level 4, leader at " + number(kSlowLeaderKmh) + " km/h";
	const Outcome outcome = runScenario(fixture, 4, kSlowLeaderKmh);
	for (const std::string& problem : outcome.problems)
		failures.push_back(assertion + ": " + problem);
	if (!outcome.problems.empty())
		return;
	if (trace)
		printTrace(outcome);
	const int from = firstCruiseStep(outcome);
	if (outcome.cruiseLast <= from) {
		failures.push_back(assertion + ": the leader never cruises with the follower in the network");
		return;
	}
	checkNoOverlap(assertion, outcome, failures);

	int firstFollowing = -1;
	int followingSteps = 0;
	int firstUnintentional = -1;
	int recoupled = -1;
	bool leaderNamed = true;
	double largestSpeedDifference = 0.0;
	double smallestGap = 1e9;
	double largestGap = 0.0;
	for (int t = from; t <= outcome.cruiseLast; ++t) {
		const Step& step = outcome.steps[static_cast<std::size_t>(t)];
		if (!step.following)
			continue;
		if (firstFollowing < 0)
			firstFollowing = t;
		++followingSteps;
		leaderNamed = leaderNamed && step.leading == "F1-1";
		if (step.unintentional && firstUnintentional < 0)
			firstUnintentional = t;
		if (!step.unintentional && firstUnintentional >= 0 && recoupled < 0)
			recoupled = t;
		largestSpeedDifference = std::max(largestSpeedDifference, std::fabs(step.speed - step.leaderSpeed));
		smallestGap = std::min(smallestGap, gapAt(outcome, t));
		largestGap = std::max(largestGap, gapAt(outcome, t));
	}
	if (firstFollowing < 0) {
		failures.push_back(assertion + ": the follower never enters following mode");
		return;
	}
	if (!leaderNamed)
		failures.push_back(assertion + ": LeadingTrainInFollowingMode is not the leader F1-1 while following");
	if (followingSteps < kSettledSteps)
		failures.push_back(assertion + ": the follower is in following mode in only " + std::to_string(followingSteps) + " steps");
	if (largestSpeedDifference > kFollowingSpeedTolerance)
		failures.push_back(assertion + ": the follower differs " + number(largestSpeedDifference) + " m/s from the leader while following");
	if (smallestGap < 0.0 || largestGap > kMaximumCruiseGap)
		failures.push_back(assertion + ": the gap while following ranges from " + number(smallestGap) + " m to " + number(largestGap) + " m");
	// The follower is faster than the leader before it couples, and it couples at nearly the speed of the leader.
	const Step& coupled = outcome.steps[static_cast<std::size_t>(firstFollowing)];
	if (std::fabs(coupled.speed - coupled.leaderSpeed) > kCouplingSpeedDifference + 1e-6)
		failures.push_back(assertion + ": the follower couples at step " + std::to_string(firstFollowing) + " with a speed difference of "
			+ number(std::fabs(coupled.speed - coupled.leaderSpeed)) + " m/s");
	if (outcome.steps[static_cast<std::size_t>(from)].speed > outcome.steps[static_cast<std::size_t>(from)].leaderSpeed
		&& firstFollowing <= from)
		failures.push_back(assertion + ": the follower is faster than the leader in the first cruising step and already follows");
	// A follower that falls more than 30 m behind the authority decouples and then couples again.
	if (firstUnintentional < 0 || recoupled < 0)
		failures.push_back(assertion + ": no unintentional decoupling followed by a new coupling while following");
	// Late in the cruise the follower is settled behind the leader.
	double settledDifference = 0.0;
	for (int t = std::max(from, outcome.cruiseLast - kSettledSteps); t <= outcome.cruiseLast; ++t)
		settledDifference = std::max(settledDifference, std::fabs(outcome.steps[static_cast<std::size_t>(t)].speed - outcome.steps[static_cast<std::size_t>(t)].leaderSpeed));
	if (settledDifference > kSettledSpeedTolerance)
		failures.push_back(assertion + ": the speed of the follower still differs " + number(settledDifference) + " m/s from the leader at the end of the cruise");
	std::cout << "level 4, leader at " << number(kSlowLeaderKmh) << " km/h: coupled at step " << firstFollowing << ", " << followingSteps
			  << " steps in following mode, first unintentional decoupling at step " << firstUnintentional << ", coupled again at step " << recoupled
			  << ", speed difference up to " << number(largestSpeedDifference) << " m/s, gap " << number(smallestGap) << " to " << number(largestGap) << " m\n";
}

// A leader at 72 km/h: the follower brakes from 130 km/h to the speed of the leader at a distance, and does not run through it.
void checkFastLeader(const std::string& fixture, std::vector<std::string>& failures) {
	const std::string assertion = "level 4, leader at " + number(kFastLeaderKmh) + " km/h";
	const Outcome outcome = runScenario(fixture, 4, kFastLeaderKmh);
	for (const std::string& problem : outcome.problems)
		failures.push_back(assertion + ": " + problem);
	if (!outcome.problems.empty())
		return;
	const int from = firstCruiseStep(outcome);
	if (outcome.cruiseLast <= from + kSettledSteps) {
		failures.push_back(assertion + ": the leader cruises with the follower in the network for too few steps");
		return;
	}
	checkNoOverlap(assertion, outcome, failures);
	// The follower reached the speed of the leader before the end of the cruise and stays behind it.
	double settledDifference = 0.0;
	double smallestGap = 1e9;
	double largestGap = 0.0;
	for (int t = outcome.cruiseLast - kSettledSteps; t <= outcome.cruiseLast; ++t) {
		const Step& step = outcome.steps[static_cast<std::size_t>(t)];
		settledDifference = std::max(settledDifference, std::fabs(step.speed - step.leaderSpeed));
		smallestGap = std::min(smallestGap, gapAt(outcome, t));
		largestGap = std::max(largestGap, gapAt(outcome, t));
	}
	if (settledDifference > kSettledSpeedTolerance)
		failures.push_back(assertion + ": the speed of the follower still differs " + number(settledDifference) + " m/s from the leader at the end of the cruise");
	if (smallestGap <= 0.0 || largestGap > kMaximumCruiseGap)
		failures.push_back(assertion + ": the gap at the end of the cruise ranges from " + number(smallestGap) + " m to " + number(largestGap) + " m");
	std::cout << "level 4, leader at " << number(kFastLeaderKmh) << " km/h: speed difference " << number(settledDifference) << " m/s, gap "
			  << number(smallestGap) << " to " << number(largestGap) << " m at the end of the cruise\n";
}

} // namespace

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);

	std::string fixture;
	bool trace = false;
	int traceLevel = -1;
	double leaderKmh = kSlowLeaderKmh;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--fixture" && i + 1 < argc)
			fixture = argv[++i];
		else if (arg == "--leader" && i + 1 < argc)
			leaderKmh = std::atof(argv[++i]);
		else if (arg == "--trace-level" && i + 1 < argc)
			traceLevel = std::atoi(argv[++i]);
		else if (arg == "--trace")
			trace = true;
		else {
			std::cerr << "unknown argument '" << arg << "'\n";
			return 2;
		}
	}
	if (fixture.empty()) {
		std::cerr << "usage: test_virtual_coupling --fixture DIR [--trace] [--trace-level N [--leader KMH]]\n";
		return 2;
	}

	if (traceLevel >= 0) {
		printTrace(runScenario(fixture, traceLevel, leaderKmh));
		return 0;
	}
	std::vector<std::string> failures;
	checkCoupling(fixture, trace, failures);
	checkFastLeader(fixture, failures);
	checkBlockLevel(fixture, 3, failures);
	for (const std::string& failure : failures)
		std::cerr << "FAIL virtual coupling " << failure << "\n";
	if (!failures.empty())
		return 1;
	std::cout << "PASS virtual coupling\n";
	return 0;
}
