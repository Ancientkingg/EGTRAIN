// Virtual coupling (signalling level 4) on the line fixture.
//
//   test_virtual_coupling --fixture DIR [--trace] [--trace-level N [--leader KMH] [--stop-at-b] [--split]]
//
// A slow leader runs ahead of a faster follower on the one route of the fixture, without stops. At level 4 the follower has to
// catch up, couple, follow the leader at a bounded gap and never overlap it. At level 3 the same scenario is the control: fixed
// blocks never put a train in following mode. The checks cover the cruise of the leader, up to the step in which it starts to
// brake for the end of the route. With --trace the run prints one line per step of the follower; --trace-level N prints it for
// level N, and --leader sets the speed limit of the leader in km/h. --stop-at-b makes the leader stop at B, and --split puts the
// follower on a route that leaves the route of the leader at B.

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
// slow enough for the follower to couple while both cruise; the follower couples while both run at 72 km/h (20 m/s) as well.
constexpr double kSlowLeaderKmh = 43.2;
constexpr double kFastLeaderKmh = 72.0;
// The leader has started to brake for the end of the route when its speed is under this share of its cruising speed.
constexpr double kLeaderBrakingShare = 0.999;
// Documented thresholds of level 4: the speed difference to couple and the distance to the authority (docs/guides/signalling-levels.md).
constexpr double kCouplingSpeedDifference = 0.278;
constexpr double kCouplingDistance = 30.0;
// A coupled follower may differ in speed from the leader by this much, for the few steps after the reported authority jumps to the
// start of the next section.
constexpr double kFollowingSpeedTolerance = 5.0;
// Speed difference of a follower that has cruised behind the leader for a long time. A follower that brakes and accelerates
// around the leader's speed stays within this.
constexpr double kSettledSpeedTolerance = 1.5;
constexpr int kSettledSteps = 100;
// The gap between the tail of the leader and the head of the follower stays under this while they cruise.
constexpr double kMaximumCruiseGap = 150.0;
// The authority that a leader reports lies this far behind its tail (m).
constexpr double kAuthorityMargin = 50.0;

struct Step {
	double leaderPosition = 0.0;
	double leaderSpeed = 0.0;
	double position = 0.0;
	double speed = 0.0;
	bool following = false;
	bool coupling = false;
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

// What differs between the scenarios besides the level and the speed of the leader.
struct Variant {
	bool leaderStopsAtB = false; // The leader keeps its stop at B, 8 km from the start; the follower never stops there.
	bool split = false;			 // The follower runs on a route that leaves the route of the leader at B.
	double leaderLengthM = 0.0;	 // When above 0, the length of the leader in m; the unit of the fixture otherwise.
};

// Position of the split in m: the end of the last section that the two routes share.
constexpr double kSplitPosition = 8000.0;

// Adds the track B1 that runs next to B0 from B to the end of the line, and the route routeSplit that follows B0 up to B and then B1.
void addSplitRoute(SceneModel& scene) {
	scene.tracks.push_back({"B1"});
	for (int index = 1; index <= 5; ++index) {
		const std::string node = "B1.node." + std::to_string(index);
		scene.nodes.push_back({node, "B1", kSplitPosition / 1000.0 + 2.0 * (index - 1), 1.0});
		if (index < 5) {
			SceneArc arc;
			arc.id = "B1.arc.10" + std::to_string(index - 1);
			arc.trackId = "B1";
			arc.fromNodeId = node;
			arc.toNodeId = "B1.node." + std::to_string(index + 1);
			arc.curvatureRadiusM = 10000.0;
			arc.speedLimitMs = 36.111111111111;
			scene.arcs.push_back(arc);
			scene.blocks.push_back({std::to_string(index - 1) + "-B1", "B1", 2.0});
		}
	}
	scene.connections.push_back({"connection.split", "B0.node.5", "B1.node.1", false, 0.0});
	SceneRoute route;
	route.id = "routeSplit";
	route.blocks = {"0-B0", "1-B0", "2-B0", "3-B0", "0-B1", "1-B1", "2-B1", "3-B1"};
	scene.routes.push_back(route);
}

// Runs the leader F1 (limited to leaderLimitKmh) and the follower F2 at one level; the trains have no stop at B unless variant says so.
Outcome runScenario(const std::string& fixture, int level, double leaderLimitKmh, const Variant& variant = {}) {
	Outcome outcome;
	SceneLoadResult loaded = loadScene(fixture);
	if (hasErrors(loaded.diagnostics)) {
		for (const SceneDiagnostic& diagnostic : loaded.diagnostics)
			outcome.problems.push_back(toDisplayText(diagnostic));
		return outcome;
	}
	if (variant.leaderLengthM > 0.0) {
		SceneTrainUnit shortUnit = loaded.scene.trainUnits.front();
		shortUnit.id = "shortUnit";
		shortUnit.physical.length_m = variant.leaderLengthM;
		loaded.scene.trainUnits.push_back(shortUnit);
		loaded.scene.compositions.push_back({"shortComposition", {"shortUnit"}});
	}
	for (SceneService& service : loaded.scene.services) {
		if (service.id != "F1" && service.id != "F2")
			continue;
		if (variant.leaderLengthM > 0.0 && service.id == "F1")
			service.composition = "shortComposition";
		const bool keepB = variant.leaderStopsAtB && service.id == "F1";
		const bool dropC = variant.split && service.id == "F2";
		service.stops.erase(std::remove_if(service.stops.begin(), service.stops.end(), [&](const SceneStop& stop) {
			return (stop.stationId == "B" && !keepB) || (stop.stationId == "C" && dropC);
		}),
			service.stops.end());
		if (variant.split && service.id == "F2")
			service.route = "routeSplit";
		// Ample time, so that the timetable never makes a train wait.
		for (SceneStop& stop : service.stops)
			if (stop.hasPlannedArrival && stop.stationId != "B")
				stop.plannedArrivalSeconds += 600.0;
		if (service.id == "F1") {
			service.hasMaximumSpeed = true;
			service.maximumSpeedKmh = leaderLimitKmh;
		}
	}
	if (variant.split)
		addSplitRoute(loaded.scene);
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
				  << " follow=" << step.following << " couple=" << step.coupling
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

// The follower never overlaps the leader in a step in which both run.
void checkNoOverlapUntilEnd(const std::string& assertion, const Outcome& outcome, std::vector<std::string>& failures) {
	for (int t = firstCruiseStep(outcome); t <= outcome.bothLast; ++t)
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
		if (step.following || step.coupling || step.unintentional || step.leading != "None") {
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
	// The follower holds the gap: it stays coupled for the whole cruise and, with the reported authority 50 m behind the tail,
	// within the 30 m window of the authority.
	int uncoupled = -1;
	for (int t = firstFollowing; t <= outcome.cruiseLast; ++t)
		if (!outcome.steps[static_cast<std::size_t>(t)].following) {
			uncoupled = t;
			break;
		}
	if (uncoupled >= 0)
		failures.push_back(assertion + ": the follower leaves following mode at step " + std::to_string(uncoupled));
	if (largestGap > kCouplingDistance + kAuthorityMargin)
		failures.push_back(assertion + ": the gap while following reaches " + number(largestGap) + " m, more than " + number(kCouplingDistance + kAuthorityMargin) + " m");
	// Late in the cruise the follower is settled behind the leader.
	double settledDifference = 0.0;
	for (int t = std::max(from, outcome.cruiseLast - kSettledSteps); t <= outcome.cruiseLast; ++t)
		settledDifference = std::max(settledDifference, std::fabs(outcome.steps[static_cast<std::size_t>(t)].speed - outcome.steps[static_cast<std::size_t>(t)].leaderSpeed));
	if (settledDifference > kSettledSpeedTolerance)
		failures.push_back(assertion + ": the speed of the follower still differs " + number(settledDifference) + " m/s from the leader at the end of the cruise");
	std::cout << "level 4, leader at " << number(kSlowLeaderKmh) << " km/h: coupled at step " << firstFollowing << ", " << followingSteps
			  << " steps in following mode, first unintentional decoupling at step " << firstUnintentional
			  << ", speed difference up to " << number(largestSpeedDifference) << " m/s, gap " << number(smallestGap) << " to " << number(largestGap) << " m\n";
}

// A leader at the given speed: the follower catches up and couples while both run, holds the speed of the leader, and does not run through it.
void checkCouplesWhileRunning(const std::string& fixture, double leaderKmh, std::vector<std::string>& failures) {
	const std::string assertion = "level 4, leader at " + number(leaderKmh) + " km/h";
	const Outcome outcome = runScenario(fixture, 4, leaderKmh);
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
	int firstFollowing = -1;
	for (int t = from; t <= outcome.cruiseLast && firstFollowing < 0; ++t)
		if (outcome.steps[static_cast<std::size_t>(t)].following)
			firstFollowing = t;
	if (firstFollowing < 0)
		failures.push_back(assertion + ": the follower does not couple while the leader cruises");
	else if (std::fabs(outcome.steps[static_cast<std::size_t>(firstFollowing)].speed - outcome.steps[static_cast<std::size_t>(firstFollowing)].leaderSpeed)
		> kCouplingSpeedDifference + 1e-6)
		failures.push_back(assertion + ": the follower couples with a speed difference of "
			+ number(std::fabs(outcome.steps[static_cast<std::size_t>(firstFollowing)].speed - outcome.steps[static_cast<std::size_t>(firstFollowing)].leaderSpeed)) + " m/s");
	// The follower reached the speed of the leader before the end of the cruise and stays behind it.
	double settledDifference = 0.0;
	double smallestGap = 1e9;
	double largestGap = 0.0;
	for (int t = outcome.cruiseLast - kSettledSteps; t <= outcome.cruiseLast; ++t) {
		const Step& step = outcome.steps[static_cast<std::size_t>(t)];
		settledDifference = std::max(settledDifference, std::fabs(step.speed - step.leaderSpeed));
		smallestGap = std::min(smallestGap, gapAt(outcome, t));
		largestGap = std::max(largestGap, gapAt(outcome, t));
		if (!step.following) {
			failures.push_back(assertion + ": the follower is not coupled at step " + std::to_string(t) + " of the end of the cruise");
			break;
		}
	}
	if (settledDifference > kSettledSpeedTolerance)
		failures.push_back(assertion + ": the speed of the follower still differs " + number(settledDifference) + " m/s from the leader at the end of the cruise");
	if (smallestGap <= 0.0 || largestGap > kMaximumCruiseGap)
		failures.push_back(assertion + ": the gap at the end of the cruise ranges from " + number(smallestGap) + " m to " + number(largestGap) + " m");
	std::cout << assertion << ": coupled at step " << firstFollowing << ", speed difference " << number(settledDifference) << " m/s, gap "
			  << number(smallestGap) << " to " << number(largestGap) << " m at the end of the cruise\n";
}

// The follower never overlaps the leader in any step in which both run, up to the last stop of the leader at the end of the route.
void checkNeverOverlaps(const std::string& fixture, double leaderKmh, const Variant& variant, const std::string& what, std::vector<std::string>& failures) {
	const std::string assertion = "level 4, leader at " + number(leaderKmh) + " km/h" + what;
	const Outcome outcome = runScenario(fixture, 4, leaderKmh, variant);
	for (const std::string& problem : outcome.problems)
		failures.push_back(assertion + ": " + problem);
	if (!outcome.problems.empty())
		return;
	double smallestGap = 1e9;
	int smallestStep = outcome.bothFirst;
	int following = 0;
	for (int t = firstCruiseStep(outcome); t <= outcome.bothLast; ++t) {
		if (gapAt(outcome, t) < smallestGap) {
			smallestGap = gapAt(outcome, t);
			smallestStep = t;
		}
		following += outcome.steps[static_cast<std::size_t>(t)].following ? 1 : 0;
	}
	if (smallestGap < 0.0)
		failures.push_back(assertion + ": the follower overlaps the leader at step " + std::to_string(smallestStep) + ", gap " + number(smallestGap) + " m");
	if (following == 0)
		failures.push_back(assertion + ": the follower never couples");
	// The leader that stops at B brakes for it from speed while the follower is coupled.
	double gapBeforeB = 1e9;
	for (int t = firstCruiseStep(outcome); t <= outcome.bothLast && variant.leaderStopsAtB; ++t)
		if (outcome.steps[static_cast<std::size_t>(t)].leaderPosition < kSplitPosition + 100.0)
			gapBeforeB = std::min(gapBeforeB, gapAt(outcome, t));
	std::cout << assertion << ": smallest gap " << number(smallestGap) << " m at step " << smallestStep << ", " << following << " steps coupled";
	if (variant.leaderStopsAtB)
		std::cout << ", smallest gap while the leader stops at B " << number(gapBeforeB) << " m";
	std::cout << "\n";
}

// The follower runs on a route that leaves the route of the leader at B. It keeps its gap while the tail of the leader is before the
// split, leaves following mode when the tail has cleared it, and then drives on without braking for the leader.
void checkSplitRoutes(const std::string& fixture, std::vector<std::string>& failures) {
	const std::string assertion = "level 4, routes that split at B";
	Variant variant;
	variant.split = true;
	const Outcome outcome = runScenario(fixture, 4, kSlowLeaderKmh + 10.8, variant);
	for (const std::string& problem : outcome.problems)
		failures.push_back(assertion + ": " + problem);
	if (!outcome.problems.empty())
		return;
	int coupled = -1;
	int uncoupled = -1;
	int atSplit = -1;
	for (int t = firstCruiseStep(outcome); t <= outcome.bothLast; ++t) {
		const Step& step = outcome.steps[static_cast<std::size_t>(t)];
		if (atSplit < 0 && step.position >= kSplitPosition)
			atSplit = t;
		if (coupled < 0 && step.following)
			coupled = t;
		else if (coupled >= 0 && uncoupled < 0 && !step.following)
			uncoupled = t;
		if (atSplit < 0 && gapAt(outcome, t) < 0.0) {
			failures.push_back(assertion + ": the follower overlaps the leader at step " + std::to_string(t) + ", gap " + number(gapAt(outcome, t)) + " m");
			break;
		}
	}
	if (coupled < 0 || atSplit < 0) {
		failures.push_back(assertion + ": the follower does not couple before the split");
		return;
	}
	if (uncoupled < 0) {
		failures.push_back(assertion + ": the follower is in following mode for the whole run");
		return;
	}
	// It leaves following mode when the tail of the leader has cleared the split, allowing for the report delay of one step, and not before.
	const Step& left = outcome.steps[static_cast<std::size_t>(uncoupled)];
	const double tail = left.leaderPosition - outcome.leaderLength;
	if (tail < kSplitPosition - left.leaderSpeed)
		failures.push_back(assertion + ": the follower leaves following mode while the tail of the leader is at " + number(tail) + " m, before the split at "
			+ number(kSplitPosition) + " m");
	// And not long after: once the tail of the leader has cleared the split, the leader holds nothing on the route of the follower.
	constexpr int kReportSteps = 3;
	int cleared = -1;
	for (int t = firstCruiseStep(outcome); t <= outcome.bothLast && cleared < 0; ++t)
		if (outcome.steps[static_cast<std::size_t>(t)].leaderPosition - outcome.leaderLength > kSplitPosition)
			cleared = t;
	if (cleared < 0 || uncoupled > cleared + kReportSteps)
		failures.push_back(assertion + ": the follower is still in following mode at step " + std::to_string(uncoupled - 1) + ", and the tail of the leader cleared the split at step "
			+ std::to_string(cleared));
	// After the split the leader is no reason to stop: the follower keeps the speed it had and then runs faster than the leader.
	double lowestSpeed = 1e9;
	double highestSpeed = 0.0;
	const double leaderSpeed = outcome.steps[static_cast<std::size_t>(uncoupled)].leaderSpeed;
	for (int t = uncoupled; t <= outcome.bothLast; ++t) {
		lowestSpeed = std::min(lowestSpeed, outcome.steps[static_cast<std::size_t>(t)].speed);
		highestSpeed = std::max(highestSpeed, outcome.steps[static_cast<std::size_t>(t)].speed);
	}
	if (lowestSpeed < leaderSpeed - 1.0)
		failures.push_back(assertion + ": the follower slows to " + number(lowestSpeed) + " m/s after the split, under the " + number(leaderSpeed) + " m/s of the leader");
	if (highestSpeed < leaderSpeed + kFollowingSpeedTolerance)
		failures.push_back(assertion + ": the follower does not run on faster than the leader after the split; its highest speed is " + number(highestSpeed) + " m/s");
	std::cout << assertion << ": coupled at step " << coupled << ", leaves following mode at step " << uncoupled << " at "
			  << number(outcome.steps[static_cast<std::size_t>(uncoupled)].position) << " m, speed " << number(lowestSpeed) << " to " << number(highestSpeed) << " m/s afterwards\n";
}

// A leader shorter than the 50 m margin of a front authority runs with the follower coupled close behind it. The follower is
// no leader for it: the leader keeps cruising at its limit until it is near the end of the route, and the follower never overlaps it.
void checkShortLeaderCruises(const std::string& fixture, std::vector<std::string>& failures) {
	constexpr double kLeaderKmh = 54.0;
	constexpr double kShortLeaderLength = 15.0;
	constexpr double kBrakingForEndOfRoute = 15000.0; // The route is 16 km; the leader brakes for its end after this position.
	const std::string assertion = "level 4, leader of " + number(kShortLeaderLength) + " m at " + number(kLeaderKmh) + " km/h";
	Variant variant;
	variant.leaderLengthM = kShortLeaderLength;
	const Outcome outcome = runScenario(fixture, 4, kLeaderKmh, variant);
	for (const std::string& problem : outcome.problems)
		failures.push_back(assertion + ": " + problem);
	if (!outcome.problems.empty())
		return;
	int slowedAt = -1;
	int following = 0;
	for (int t = firstCruiseStep(outcome); t <= outcome.bothLast; ++t) {
		const Step& step = outcome.steps[static_cast<std::size_t>(t)];
		following += step.following ? 1 : 0;
		if (slowedAt < 0 && step.leaderPosition < kBrakingForEndOfRoute && step.leaderSpeed < kLeaderBrakingShare * outcome.leaderCruise)
			slowedAt = t;
	}
	if (slowedAt >= 0)
		failures.push_back(assertion + ": the leader slows to " + number(outcome.steps[static_cast<std::size_t>(slowedAt)].leaderSpeed) + " m/s at step "
			+ std::to_string(slowedAt) + " although the line ahead of it is free");
	if (following == 0)
		failures.push_back(assertion + ": the follower never couples");
	checkNoOverlapUntilEnd(assertion, outcome, failures);
	std::cout << assertion << ": " << following << " steps coupled, leader slows at step " << slowedAt << "\n";
}

} // namespace

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);

	std::string fixture;
	bool trace = false;
	int traceLevel = -1;
	double leaderKmh = kSlowLeaderKmh;
	Variant variant;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--fixture" && i + 1 < argc)
			fixture = argv[++i];
		else if (arg == "--leader" && i + 1 < argc)
			leaderKmh = std::atof(argv[++i]);
		else if (arg == "--trace-level" && i + 1 < argc)
			traceLevel = std::atoi(argv[++i]);
		else if (arg == "--stop-at-b")
			variant.leaderStopsAtB = true;
		else if (arg == "--split")
			variant.split = true;
		else if (arg == "--leader-length" && i + 1 < argc)
			variant.leaderLengthM = std::atof(argv[++i]);
		else if (arg == "--trace")
			trace = true;
		else {
			std::cerr << "unknown argument '" << arg << "'\n";
			return 2;
		}
	}
	if (fixture.empty()) {
		std::cerr << "usage: test_virtual_coupling --fixture DIR [--trace] [--trace-level N [--leader KMH] [--stop-at-b] [--split]]\n";
		return 2;
	}

	if (traceLevel >= 0) {
		printTrace(runScenario(fixture, traceLevel, leaderKmh, variant));
		return 0;
	}
	std::vector<std::string> failures;
	checkCoupling(fixture, trace, failures);
	for (const double kmh : {54.0, kFastLeaderKmh, 90.0})
		checkCouplesWhileRunning(fixture, kmh, failures);
	for (const double kmh : {kSlowLeaderKmh, 54.0, kFastLeaderKmh, 90.0}) {
		checkNeverOverlaps(fixture, kmh, {}, ", to its last stop", failures);
		Variant braking;
		braking.leaderStopsAtB = true;
		checkNeverOverlaps(fixture, kmh, braking, ", stopping at B", failures);
	}
	checkSplitRoutes(fixture, failures);
	checkShortLeaderCruises(fixture, failures);
	checkBlockLevel(fixture, 3, failures);
	for (const std::string& failure : failures)
		std::cerr << "FAIL virtual coupling " << failure << "\n";
	if (!failures.empty())
		return 1;
	std::cout << "PASS virtual coupling\n";
	return 0;
}
