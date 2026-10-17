// Fixed block signalling over two crossovers that follow each other directly.
//
//   test_crossover_chain --fixture DIR --level N
//
// The fixture is a chain of five tracks. The two link tracks make a double
// switch each, and the middle track carries the second half of the first
// double switch and the first half of the second one on one long block. One
// network-wide signalling area of the given level is set here. A run with one
// train must not stand anywhere but at a platform. A train behind another one
// must not overlap it and must not enter the second crossover while the first
// train is in it; it waits before the first crossover rather than standing on either half.

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
	std::vector<double> deceleration;
	std::vector<double> acceleration;
	std::vector<bool> bookkeeping;
};

// What one run of the fixture leaves behind.
struct Outcome {
	std::vector<Trajectory> trains;
	// Positions along the route of the trains, in m. platforms holds the start and the end of the route and the platforms between them.
	// firstStart and firstEnd bound the first crossover; secondStart and secondEnd bound the second crossover.
	std::vector<double> platforms;
	double routeEnd = 0.0;
	double firstStart = 0.0;
	double firstEnd = 0.0;
	double secondStart = 0.0;
	double secondEnd = 0.0;
	std::vector<std::string> measurementProblems;
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
	const Section& second = route.sequence_of_block_sections[switches[1]];
	const Section& third = route.sequence_of_block_sections[switches[2]];
	const Section& fourth = route.sequence_of_block_sections[switches[3]];
	outcome.firstStart = route.sequence_of_block_sections[switches[0]].start_node.X * 1000.0;
	outcome.firstEnd = second.end_node.X * 1000.0;
	outcome.secondStart = third.start_node.X * 1000.0;
	outcome.secondEnd = fourth.end_node.X * 1000.0;
}

// Loads the fixture, runs the given services at one level and copies what the tests need out of the simulation.
Outcome runServices(const std::string& fixture, int level, const std::vector<std::string>& services,
	const std::vector<std::string>& reversedRoutes = {}) {
	Outcome outcome;
	SceneLoadResult loaded = loadScene(fixture);
	if (hasErrors(loaded.diagnostics)) {
		for (const SceneDiagnostic& diagnostic : loaded.diagnostics)
			outcome.problems.push_back(toDisplayText(diagnostic));
		return outcome;
	}
	if (std::find(services.begin(), services.end(), "R2") != services.end()) {
		auto reverse = std::find_if(loaded.scene.services.begin(), loaded.scene.services.end(), [](const SceneService& service) { return service.id == "R1"; });
		if (reverse == loaded.scene.services.end()) {
			outcome.problems.push_back("cannot copy unknown reverse service R1");
			return outcome;
		}
		SceneService follower = *reverse;
		follower.id = follower.operatingCode = "R2";
		follower.entryTimeSeconds += 60;
		for (auto& stop : follower.stops) {
			if (stop.hasPlannedArrival)
				stop.plannedArrivalSeconds += 60;
			if (stop.hasPlannedDeparture)
				stop.plannedDepartureSeconds += 60;
		}
		loaded.scene.services.push_back(std::move(follower));
	}
	for (const std::string& id : reversedRoutes) {
		auto route = std::find_if(loaded.scene.routes.begin(), loaded.scene.routes.end(),
			[&id](const SceneRoute& candidate) { return candidate.id == id; });
		if (route == loaded.scene.routes.end()) {
			outcome.problems.push_back("cannot mark unknown route " + id + " reversed");
			return outcome;
		}
		route->reversed = true;
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
	initial_variables.InputMainFolder.clear();

	std::ostringstream sink;
	std::streambuf* saved = std::cout.rdbuf(sink.rdbuf());
	const std::vector<SceneDiagnostic> diagnostics = simulation.prepareScene(loaded.scene, "baseline", selection);
	// Read each completed step before the next movement can rewrite a stored speed while capping to
	// an arc limit. Physical checks must use the speed actually reported at the previous step.
	std::vector<Trajectory> observed(numRegions);
	for (Trajectory& trajectory : observed) {
		trajectory.position.resize(initial_variables.times);
		trajectory.speed.resize(initial_variables.times);
		trajectory.deceleration.resize(initial_variables.times);
		trajectory.acceleration.resize(initial_variables.times);
		trajectory.bookkeeping.resize(initial_variables.times);
	}
	simulation.setSnapshotsEnabled(true);
	const auto connection = QObject::connect(&simulation, &DispatchController::snapshotAvailable, [&] {
		const auto snapshot = simulation.takeSimulationSnapshot();
		if (!snapshot)
			return;
		const int step = snapshot->timestep;
		for (int k = 0; k < numRegions; ++k) {
			Train& train = regional_train[k];
			Trajectory& trajectory = observed[k];
			trajectory.position[step] = train.instant_spatial_position[step];
			trajectory.speed[step] = train.instant_train_speed[step];
			if (step == 0)
				continue;
			const Route& route = train_route[train.indexOfRoute];
			for (const Section& section : route.sequence_of_block_sections)
				for (int j = 0; j < section.total_arcs; ++j)
					if (const Arc& arc = section.arcs_in_signalling_block_section[j]; trajectory.position[step - 1] >= arc.startNode.X * 1000 && trajectory.position[step - 1] < arc.endNode.X * 1000) {
						trajectory.deceleration[step] = train.max_train_decelaration
							+ train.total_train_resistances(trajectory.speed[step - 1], arc.gradient, arc.curvature) / (train.total_train_mass * train.massFactor);
						trajectory.acceleration[step] = (train.tractiveEffort(trajectory.speed[step - 1])
															- train.total_train_resistances(trajectory.speed[step - 1], arc.gradient, arc.curvature))
							/ (train.total_train_mass * train.massFactor);
						if (trajectory.speed[step - 1] == 0 && trajectory.position[step] != trajectory.position[step - 1]
							&& (train.Eq[step] == 5 || train.Eq[step] == 6)) {
							const double node = arc.endNode.X * 1000;
							const double oldHead = trajectory.position[step - 1], nextHead = trajectory.position[step];
							const double nextSpeed = trajectory.speed[step];
							// Match only the prescribed source-expression endpoints using the
							// existing anchored-plan coordinate comparison, never arbitrary movement.
							const auto sameCoordinate = [](double a, double b) { return std::fabs(a - b) < 1e-9; };
							const bool hold = sameCoordinate(nextHead, node - 0.0001) && nextSpeed == 0;
							const bool release = sameCoordinate(nextHead, node + 0.0001) && nextSpeed == 0.0001;
							const bool anchored = sameCoordinate(oldHead, node - 0.002);
							const bool holding = sameCoordinate(oldHead, node - 0.0001);
							const bool geometry = (anchored && (hold || release)) || (holding && release);
							bool predicate = false;
							double dwell = 0, departure = 0;
							bool red = false;
							const Section& running = train.Bs;
							if (train.Eq[step] == 5 && arc.endNode.station == 1 && arc.endNode.respectOrder == 0 && node - oldHead < 4) {
								for (int station = 0; station < train.numStations; ++station)
									if (train.Stations[station].stationName == arc.endNode.stationName) {
										dwell = train.Stations[station].StepStopped > train.Stations[station].StopTime ? 0 : train.Stations[station].StopTime;
										departure = train.ScheduledDepartures[station];
									}
								red = running.total_arcs > 0
									&& std::fabs(running.arcs_in_signalling_block_section[running.total_arcs - 1].endNode.X - arc.endNode.X) < 1e-9
									&& running.state == SignalState::Red;
								const bool mustHold = step - train.stop <= dwell || step <= departure || train.IsTrainStoppedForEoA || red;
								predicate = hold ? mustHold : release && !mustHold;
							} else if (train.Eq[step] == 6 && arc.endNode.station != 1 && arc.endNode.respectOrder == 0 && node - oldHead < 4) {
								red = running.state == SignalState::Red;
								const bool mustHold = red || train.IsTrainStoppedForEoA;
								predicate = hold ? mustHold : release && !mustHold;
							}
							trajectory.bookkeeping[step] = geometry && predicate;
						}
					}
		}
	});
	if (!hasErrors(diagnostics) && timestep == 1.0)
		simulation.runSimulation();
	QObject::disconnect(connection);
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
		Train& train = regional_train[i];
		Trajectory trajectory = std::move(observed[i]);
		trajectory.name = train.trainDescription;
		trajectory.length = train.train_length;
		const int available = static_cast<int>(std::min(train.instant_spatial_position.size(), train.instant_train_speed.size()));
		trajectory.first = std::max(static_cast<int>(train.departure_time), train.earliestActiveTrajectoryIndex);
		trajectory.last = std::min(train.End_Time, available - 1);
		if (services.size() == 2) {
			int rewrites = 0;
			for (int t = 0; t <= trajectory.last; ++t)
				if (trajectory.position[t] != train.instant_spatial_position[t] || trajectory.speed[t] != train.instant_train_speed[t])
					++rewrites;
			if (rewrites)
				outcome.measurementProblems.push_back(train.trainDescription + ": published prior samples were rewritten");
			bool hasB = false;
			for (int station = 0; station < train.numStations; ++station) {
				if (train.Stations[station].stationName != "B")
					continue;
				hasB = true;
				const double platform = train.stationBlockSection[station] >= 0 && train.stationArc[station] >= 0
					? train_route[train.indexOfRoute].sequence_of_block_sections[train.stationBlockSection[station]].arcs_in_signalling_block_section[train.stationArc[station]].endNode.X * 1000
					: -1;
				int firstStop = -1, lastStop = -1, departure = -1;
				for (int t = trajectory.first; t <= trajectory.last; ++t) {
					if (std::abs(trajectory.position[t] - platform) <= kPlatformReach && trajectory.speed[t] == 0) {
						if (firstStop < 0) firstStop = t;
						lastStop = t;
					} else if (lastStop >= 0 && departure < 0 && trajectory.position[t] > platform && trajectory.speed[t] > 0)
						departure = t;
				}
				const double planned = train.ScheduledDepartures[station];
				const double dwell = train.Stations[station].StopTime;
				if (firstStop < 0 || departure < 0 || dwell != 60 || lastStop - firstStop < dwell || departure <= planned
					|| train.StationArrivals[station] != firstStop)
					outcome.measurementProblems.push_back(train.trainDescription + ": scheduled B stop/recorded arrival, dwell60 or planned departure missing");
			}
			if (!hasB)
				outcome.measurementProblems.push_back(train.trainDescription + ": fixture has no scheduled B stop");
		}
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
	std::vector<std::string>& failures, const std::vector<std::string>& reversedRoutes = {}) {
	const Outcome outcome = runServices(fixture, level, {service}, reversedRoutes);
	for (const std::string& problem : outcome.problems)
		failures.push_back(assertion + ": " + problem);
	if (!outcome.problems.empty())
		return;
	expectReachesEnd(assertion, outcome, outcome.trains[0], failures);
	expectOnlyStopsAtPlatforms(assertion, outcome, outcome.trains[0], failures);
}

// A follower waits before the first half while the leader occupies the next crossover.
void checkFollower(const std::string& fixture, int level, std::vector<std::string>& failures, bool reversed = false) {
	const std::string assertion = reversed ? "leader R1 and follower R2" : "leader F1 and follower F2";
	const Outcome outcome = runServices(fixture, level, reversed ? std::vector<std::string>{"R1", "R2"} : std::vector<std::string>{"F1", "F2"});
	for (const std::string& problem : outcome.problems)
		failures.push_back(assertion + ": " + problem);
	if (!outcome.problems.empty())
		return;
	for (const std::string& problem : outcome.measurementProblems)
		failures.push_back(assertion + ": " + problem);
	const Trajectory& leader = outcome.trains[0];
	const Trajectory& follower = outcome.trains[1];
	// The checks below take the first train of the run as the leader.
	if (leader.name != (reversed ? "R1-1" : "F1-1") || follower.name != (reversed ? "R2-1" : "F2-1")) {
		failures.push_back(assertion + ": the trains of the run are " + leader.name + " and " + follower.name + ", not the requested leader and follower");
		return;
	}
	expectReachesEnd(assertion + ", both reach the last platform", outcome, leader, failures);
	expectReachesEnd(assertion + ", both reach the last platform", outcome, follower, failures);

	const int from = std::max(leader.first, follower.first);
	const int to = std::min(leader.last, follower.last);
	int overlapStep = -1;
	int bothInside = -1;
	int waitStep = -1;
	for (int t = from; t <= to; ++t) {
		if (overlapStep < 0 && follower.position[t] > leader.position[t] - leader.length)
			overlapStep = t;
		const bool leaderInside = inside(leader, t, outcome.secondStart, outcome.secondEnd);
		if (leaderInside && inside(follower, t, outcome.secondStart, outcome.secondEnd) && bothInside < 0)
			bothInside = t;
		if (leaderInside && follower.speed[t] < kStandstill && follower.position[t] <= outcome.firstStart + kInsideMargin
			&& !nearPlatform(outcome, follower.position[t]) && waitStep < 0)
			waitStep = t;
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
	if (waitStep < 0)
		failures.push_back(assertion + ", the follower waits before the first half: it never stands before " + metres(outcome.firstStart) + " m while the leader is in the second crossover");
	for (const Trajectory& train : outcome.trains)
		for (int step = std::max(1, train.first); step <= train.last; ++step) {
			if (train.speed[step] < kStandstill && !nearPlatform(outcome, train.position[step])
				&& (inside(train, step, outcome.firstStart, outcome.firstEnd) || inside(train, step, outcome.secondStart, outcome.secondEnd))) {
				failures.push_back(assertion + ": " + train.name + " stands on a crossover at step " + std::to_string(step));
				break;
			}
			const double displacement = train.position[step] - train.position[step - 1];
			if (!std::isfinite(displacement) || !std::isfinite(train.speed[step - 1]) || !std::isfinite(train.speed[step])
				|| !std::isfinite(train.deceleration[step]) || !std::isfinite(train.acceleration[step]) || displacement < -1e-6
				|| (!train.bookkeeping[step] && (displacement < std::min(train.speed[step - 1], train.speed[step]) * timestep - 1e-6 || displacement > std::max(train.speed[step - 1], train.speed[step]) * timestep + 1e-6))
				|| train.speed[step - 1] - train.speed[step] > train.deceleration[step] * timestep + 1e-6
				|| train.speed[step] - train.speed[step - 1] > train.acceleration[step] * timestep + 1e-6) {
				failures.push_back(assertion + ": " + train.name + " exceeds a physical movement/braking bound at step " + std::to_string(step)
					+ ": displacement=" + metres(displacement) + ", speeds=" + metres(train.speed[step - 1]) + "/" + metres(train.speed[step])
					+ ", deceleration=" + metres(train.deceleration[step]));
				break;
			}
		}
}

// A late departure would occupy the exit while the other train can no longer stop before the first
// half. Check the shared admission path on real route copies in both travel directions.
void checkLateAdmission(const std::string& fixture, int level, std::vector<std::string>& failures) {
	SceneLoadResult loaded = loadScene(fixture);
	if (hasErrors(loaded.diagnostics)) {
		failures.push_back("late admission: cannot load fixture");
		return;
	}
	SceneSignallingArea area;
	area.id = "area.all";
	area.startKm = 0.0;
	area.endKm = kAreaEndKm;
	area.level = level;
	loaded.scene.signallingAreas = {area};
	QTemporaryDir outputDir;
	initial_variables.GUI = initial_variables.TSM = initial_variables.RChoice = 0;
	initial_variables.OutputMainFolder = outputDir.path().toStdString();
	initial_variables.InputMainFolder.clear();
	const auto diagnostics = simulation.prepareScene(loaded.scene, "baseline", {{"F1", 1}, {"R1", 1}});
	if (!outputDir.isValid() || hasErrors(diagnostics) || numRegions != 2) {
		failures.push_back("late admission: cannot prepare trains");
		return;
	}
	for (int direction = 0; direction < 2; ++direction) {
		Train& arrival = regional_train[direction];
		Train& departure = regional_train[1 - direction];
		Route& route = train_route[arrival.indexOfRoute];
		int first = -1;
		for (int b = 0; b + 2 < route.N_Block_Sections; ++b)
			if (route.sequence_of_block_sections[b].end_node.virtualSignal && route.sequence_of_block_sections[b + 1].start_node.virtualSignal)
				first = b;
		if (first < 0) {
			failures.push_back("late admission: route has no crossover with an exit");
			continue;
		}
		const double entry = route.sequence_of_block_sections[first].start_node.X * 1000;
		Section origin = route.sequence_of_block_sections[first + 2];
		origin.code = 270;
		departure.Start_Node_X = origin.start_node.X;
		departure.departure_time = arrival.departure_time = 0;
		arrival.CanEnter = true;
		arrival.OutOfSimulation = departure.OutOfSimulation = false;
		auto admitted = [&](double head, double speed) {
			arrival.instant_spatial_position[0] = head;
			arrival.instant_train_speed[0] = speed;
			// A train earlier in the movement order may already have its current sample. Admission must
			// still cover the next movement from the same previous sample as the later train.
			arrival.instant_spatial_position[1] = head + speed * timestep;
			arrival.instant_train_speed[1] = speed;
			departure.CanEnter = false;
			departure.counter2 = 0;
			BlocksOccupied.clear();
			departure.checkEntrance(1, &origin);
			return departure.CanEnter;
		};
		const std::string assertion = "late admission, direction " + std::to_string(direction);
		if (admitted(entry - 30, 20))
			failures.push_back(assertion + ": departure enters after the arrival has passed its braking point");
		if (departure.counter2 != 0 || !BlocksOccupied.empty())
			failures.push_back(assertion + ": a refused departure changes admission state or occupancy");
		if (!admitted(entry - 1000, 20) || !admitted(entry - 150, 0))
			failures.push_back(assertion + ": a distant or safely stopped arrival unnecessarily holds the exit");
		if (admitted(entry + 1, 0))
			failures.push_back(assertion + ": departure enters while the arrival is inside the crossover");
		if (!admitted(origin.end_node.X * 1000 + arrival.train_length, 0))
			failures.push_back(assertion + ": tail clearance does not release admission");
		if (level == 1 || level == 2) {
			const double brakingStart = arrival.BrakDist_Block(20, 0, entry, route.sequence_of_block_sections.data(), route.N_Block_Sections);
			if (admitted(brakingStart - 10, 20))
				failures.push_back(assertion + ": next-step movement consumes the remaining braking margin");
		}
		const std::string exitId = origin.ID;
		origin.ID = "connected.origin";
		origin.N_ConnectedBS = 1;
		origin.IDConnectedBS[0] = exitId;
		if (admitted(entry - 30, 20))
			failures.push_back(assertion + ": connected origin occupancy ignores a committed arrival");
		origin.N_ConnectedBS = 0;
		if (!admitted(entry - 30, 20))
			failures.push_back(assertion + ": an unrelated origin is held");
		origin.ID = exitId;
		arrival.OutOfSimulation = true;
		if (!admitted(entry - 30, 20))
			failures.push_back(assertion + ": an exited arrival still holds admission");
		BlocksOccupied.clear();
	}
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
	checkLoneTrain("lone forward train F1", fixture, level, "F1", failures);
	checkLoneTrain("lone reverse train R1", fixture, level, "R1", failures);
	checkFollower(fixture, level, failures);
	checkFollower(fixture, level, failures, true);
	checkLateAdmission(fixture, level, failures);
	checkLoneTrain("marked reverse train R1", fixture, level, "R1", failures, {"route1"});
	const Outcome refused = runServices(fixture, level, {"F1"}, {"route0"});
	const bool directionError = std::any_of(refused.problems.begin(), refused.problems.end(), [](const std::string& problem) {
		return problem.find("scene.route.direction") != std::string::npos && problem.find("route0") != std::string::npos;
	});
	if (!directionError || !refused.trains.empty()) {
		std::string message = "marked forward route route0 must be refused; trains: " + std::to_string(refused.trains.size());
		for (const std::string& problem : refused.problems)
			message += "; " + problem;
		failures.push_back(message);
	}

	const std::string name = "crossover chain at level " + std::to_string(level);
	for (const std::string& failure : failures)
		std::cerr << "FAIL " << name << " " << failure << "\n";
	if (!failures.empty())
		return 1;
	std::cout << "PASS " << name << "\n";
	return 0;
}
