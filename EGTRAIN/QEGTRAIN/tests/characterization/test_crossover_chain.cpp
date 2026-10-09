// Fixed block signalling over two crossovers that follow each other directly.
//
//   test_crossover_chain --fixture DIR --level N
//
// The fixture is a chain of five tracks. The two link tracks make a double
// switch each, and the middle track carries the second half of the first
// double switch and the first half of the second one on one long block. One
// network-wide signalling area of the given level is set here. A run with one
// train must not stand anywhere but at a platform, and a train behind another
// one must wait in front of the second crossover for as long as the first train
// stands in it.

#include "app/DispatchController.h"
#include "simulation/InitialParameters.h"
#include "simulation/Infrastructure.h"
#include "simulation/RollingStock.h"
#include "simulation/Signalling.h"
#include "simulation/Simulation.h"
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

Logger owl;

namespace {

// End of the signalling area in km; it covers the chainage of every track.
constexpr double kAreaEndKm = 6.0;
constexpr double kStandstill = 0.01;	  // Speed below this is a standstill (m/s).
constexpr double kPlatformReach = 1.0;	  // A stop at a platform is this close to its node (m).
constexpr double kInsideMargin = 0.01;	  // Head position past a section start that counts as inside (m).
constexpr int kSwitchSectionsInChain = 4; // Two double switches of two sections each.

struct Trajectory {
	std::string name;
	double length = 0.0;
	int first = 0; // First and last active step.
	int last = -1;
	std::vector<double> position;
	std::vector<double> speed;
};

// What one run of the fixture leaves behind.
struct Outcome {
	std::vector<Trajectory> trains;
	// Positions along the route of the trains, in m: the platforms, the route end, and the second crossover.
	std::vector<double> platforms;
	double routeEnd = 0.0;
	double secondStart = 0.0;
	double secondEnd = 0.0;
	std::vector<std::string> problems; // The fixture or the run is not as the test needs it.
};

// The four switch sections of the chain follow one another on the route, and each double switch is a section with the
// signal at its end followed by a section with the signal at its start.
void checkChain(const Route& route, Outcome& outcome) {
	std::vector<int> switches;
	for (int b = 0; b < route.N_Block_Sections; ++b)
		if (route.sequence_of_block_sections[b].withSwitchDiv)
			switches.push_back(b);
	if (static_cast<int>(switches.size()) != kSwitchSectionsInChain) {
		outcome.problems.push_back("route " + route.ID + " has " + std::to_string(switches.size()) + " switch sections, not "
			+ std::to_string(kSwitchSectionsInChain));
		return;
	}
	for (int i = 1; i < kSwitchSectionsInChain; ++i)
		if (switches[i] != switches[i - 1] + 1)
			outcome.problems.push_back("route " + route.ID + ": the switch sections do not follow one another");
	for (int i = 0; i < kSwitchSectionsInChain; ++i) {
		const Section& section = route.sequence_of_block_sections[switches[i]];
		const bool firstHalf = i % 2 == 0;
		const bool flagged = firstHalf ? section.end_node.virtualSignal && !section.start_node.virtualSignal
									   : section.start_node.virtualSignal && !section.end_node.virtualSignal;
		if (!flagged)
			outcome.problems.push_back("route " + route.ID + ": switch section " + section.ID + " has the wrong virtual signal flags");
	}
	const Section& third = route.sequence_of_block_sections[switches[2]];
	const Section& fourth = route.sequence_of_block_sections[switches[3]];
	outcome.secondStart = third.start_node.X * 1000.0;
	outcome.secondEnd = fourth.end_node.X * 1000.0;
}

// Loads the fixture, runs the given services at one level and copies what the tests need out of the simulation.
Outcome runServices(const std::string& fixture, int level, const std::vector<std::string>& services) {
	Outcome outcome;
	SceneLoadResult loaded = loadScene(fixture);
	if (hasErrors(loaded.diagnostics)) {
		for (const SceneDiagnostic& diagnostic : loaded.diagnostics)
			outcome.problems.push_back(toDisplayText(diagnostic));
		return outcome;
	}
	SceneSignallingArea area;
	area.id = "area.all";
	area.startKm = 0.0;
	area.endKm = kAreaEndKm;
	area.level = level;
	loaded.scene.signallingAreas = {area};
	SceneRunSelection selection;
	for (const std::string& service : services)
		selection.insert({service, 1});

	QTemporaryDir outputDir;
	if (!outputDir.isValid()) {
		outcome.problems.push_back("cannot create a temporary output directory");
		return outcome;
	}
	initial_variables.GUI = 0;
	initial_variables.TSM = 0;
	initial_variables.RChoice = 0;
	initial_variables.OutputMainFolder = outputDir.path().toStdString();
	InputMainFolder.clear();
	initial_variables.InputMainFolder.clear();

	std::ostringstream sink;
	std::streambuf* saved = std::cout.rdbuf(sink.rdbuf());
	const std::vector<SceneDiagnostic> diagnostics = simulation.prepareScene(loaded.scene, "baseline", selection);
	if (!hasErrors(diagnostics) && timestep == 1.0)
		simulation.runSimulation();
	std::cout.rdbuf(saved);
	if (hasErrors(diagnostics)) {
		for (const SceneDiagnostic& diagnostic : diagnostics)
			outcome.problems.push_back(toDisplayText(diagnostic));
		return outcome;
	}
	if (timestep != 1.0) {
		outcome.problems.push_back("the test indexes trajectories by whole seconds; timestep is " + std::to_string(timestep));
		return outcome;
	}
	if (numRegions != static_cast<int>(services.size())) {
		outcome.problems.push_back("expected " + std::to_string(services.size()) + " trains, got " + std::to_string(numRegions));
		return outcome;
	}

	for (int i = 0; i < numRegions; ++i) {
		const Train& train = regional_train[i];
		Trajectory trajectory;
		trajectory.name = train.trainDescription;
		trajectory.length = train.train_length;
		const int available = static_cast<int>(std::min(train.instant_spatial_position.size(), train.instant_train_speed.size()));
		trajectory.first = std::max(static_cast<int>(train.departure_time), train.earliestActiveTrajectoryIndex);
		trajectory.last = std::min(train.End_Time, available - 1);
		trajectory.position = train.instant_spatial_position;
		trajectory.speed = train.instant_train_speed;
		outcome.trains.push_back(std::move(trajectory));
	}

	// All trains of a run use the first route of the first train.
	const Route& route = train_route[regional_train[0].indexOfRoute];
	checkChain(route, outcome);
	outcome.routeEnd = route.x_of_end_node * 1000.0;
	outcome.platforms.push_back(route.x_of_start_node * 1000.0);
	outcome.platforms.push_back(outcome.routeEnd);
	for (int b = 0; b < route.N_Block_Sections; ++b) {
		const Section& section = route.sequence_of_block_sections[b];
		for (int j = 0; j < section.total_arcs; ++j)
			if (!section.arcs_in_signalling_block_section[j].endNode.stationName.empty())
				outcome.platforms.push_back(section.arcs_in_signalling_block_section[j].endNode.X * 1000.0);
	}
	return outcome;
}

std::string metres(double value) {
	std::ostringstream out;
	out.setf(std::ios::fixed);
	out.precision(3);
	out << value;
	return out.str();
}

bool nearPlatform(const Outcome& outcome, double x) {
	return std::any_of(outcome.platforms.begin(), outcome.platforms.end(),
		[x](double platform) { return std::fabs(x - platform) <= kPlatformReach; });
}

double furthest(const Trajectory& train) {
	double best = 0.0;
	for (int t = train.first; t <= train.last; ++t)
		best = std::max(best, train.position[t]);
	return best;
}

// The train reaches the end of its route, which is its last platform.
void expectReachesEnd(const std::string& assertion, const Outcome& outcome, const Trajectory& train, std::vector<std::string>& failures) {
	const double reached = furthest(train);
	if (reached < outcome.routeEnd - kPlatformReach)
		failures.push_back(assertion + ": " + train.name + " reaches " + metres(reached) + " m and not the last platform at "
			+ metres(outcome.routeEnd) + " m");
}

// The train stands only at a platform.
void expectOnlyStopsAtPlatforms(const std::string& assertion, const Outcome& outcome, const Trajectory& train, std::vector<std::string>& failures) {
	for (int t = train.first; t <= train.last; ++t) {
		if (train.speed[t] >= kStandstill || nearPlatform(outcome, train.position[t]))
			continue;
		int end = t;
		while (end + 1 <= train.last && train.speed[end + 1] < kStandstill)
			++end;
		failures.push_back(assertion + ": " + train.name + " stands at " + metres(train.position[t]) + " m from step " + std::to_string(t)
			+ " to step " + std::to_string(end) + ", away from every platform");
		return;
	}
}

bool inside(const Trajectory& train, int t, double start, double end) {
	return train.position[t] > start + kInsideMargin && train.position[t] - train.length < end;
}

// A train alone on the chain, in one direction.
void checkLoneTrain(const std::string& assertion, const std::string& fixture, int level, const std::string& service,
	std::vector<std::string>& failures) {
	const Outcome outcome = runServices(fixture, level, {service});
	for (const std::string& problem : outcome.problems)
		failures.push_back(assertion + ": " + problem);
	if (!outcome.problems.empty())
		return;
	expectReachesEnd(assertion, outcome, outcome.trains[0], failures);
	expectOnlyStopsAtPlatforms(assertion, outcome, outcome.trains[0], failures);
}

// A train behind another one on the forward route.
void checkFollower(const std::string& fixture, int level, std::vector<std::string>& failures) {
	const std::string assertion = "(b) leader F1 and follower F2";
	const Outcome outcome = runServices(fixture, level, {"F1", "F2"});
	for (const std::string& problem : outcome.problems)
		failures.push_back(assertion + ": " + problem);
	if (!outcome.problems.empty())
		return;
	const Trajectory& leader = outcome.trains[0];
	const Trajectory& follower = outcome.trains[1];
	expectReachesEnd(assertion + ", both reach the last platform", outcome, leader, failures);
	expectReachesEnd(assertion + ", both reach the last platform", outcome, follower, failures);

	const int from = std::max(leader.first, follower.first);
	const int to = std::min(leader.last, follower.last);
	int overlapStep = -1;
	int bothInside = -1;
	int waitsBehind = -1;
	for (int t = from; t <= to; ++t) {
		if (overlapStep < 0 && follower.position[t] > leader.position[t] - leader.length)
			overlapStep = t;
		const bool leaderInside = inside(leader, t, outcome.secondStart, outcome.secondEnd);
		if (leaderInside && inside(follower, t, outcome.secondStart, outcome.secondEnd) && bothInside < 0)
			bothInside = t;
		if (leaderInside && follower.speed[t] < kStandstill && follower.position[t] <= outcome.secondStart && waitsBehind < 0)
			waitsBehind = t;
	}
	if (overlapStep >= 0)
		failures.push_back(assertion + ", the follower stays behind the rear of the leader: at step " + std::to_string(overlapStep)
			+ " its head is at " + metres(follower.position[overlapStep]) + " m and the rear of the leader at "
			+ metres(leader.position[overlapStep] - leader.length) + " m");
	if (bothInside >= 0)
		failures.push_back(assertion + ", the follower stays out of the second crossover while the leader is in it: at step "
			+ std::to_string(bothInside) + " the follower is at " + metres(follower.position[bothInside]) + " m and the leader at "
			+ metres(leader.position[bothInside]) + " m, the crossover runs from " + metres(outcome.secondStart) + " m to "
			+ metres(outcome.secondEnd) + " m");
	if (waitsBehind < 0)
		failures.push_back(assertion + ", the follower waits in front of the second crossover while the leader is in it: it never stands there");
}

} // namespace

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);

	std::string fixture;
	int level = -1;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--fixture" && i + 1 < argc)
			fixture = argv[++i];
		else if (arg == "--level" && i + 1 < argc)
			level = std::atoi(argv[++i]);
		else {
			std::cerr << "unknown argument '" << arg << "'\n";
			return 2;
		}
	}
	if (fixture.empty() || level < 0 || level > 5) {
		std::cerr << "usage: test_crossover_chain --fixture DIR --level N\n";
		return 2;
	}

	std::vector<std::string> failures;
	checkLoneTrain("(a) lone forward train F1", fixture, level, "F1", failures);
	checkLoneTrain("(a) lone reverse train R1", fixture, level, "R1", failures);
	checkFollower(fixture, level, failures);

	const std::string name = "crossover chain at level " + std::to_string(level);
	for (const std::string& failure : failures)
		std::cerr << "FAIL " << name << " " << failure << "\n";
	if (!failures.empty())
		return 1;
	std::cout << "PASS " << name << "\n";
	return 0;
}
