#include "scene/SceneModel.h"
#include "scene/SignallingLevelNames.h"
#include "simulation/Passengers.h"
#include "simulation/Optimisation.h"
#include "simulation/RollingStock.h"
#include "simulation/Signalling.h"
#include "simulation/Simulation.h"
#include "diagrams/RouteDiagramCoordinates.h"

// The Qt headers come before the undef: Qt defines `signals`, and the scene model has a member of that name.
#include <QDir>
#include <QTemporaryDir>
#ifdef signals
#undef signals
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#if defined(__SANITIZE_ADDRESS__)
#define TEST_ADDRESS_SANITIZER 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TEST_ADDRESS_SANITIZER 1
#endif
#endif
#ifdef TEST_ADDRESS_SANITIZER
#include <sanitizer/asan_interface.h>
#endif
#if defined(_MSC_VER)
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif

Logger owl;

static bool expect(bool condition, const std::string& message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static bool hasCode(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code) {
	return std::any_of(diagnostics.begin(), diagnostics.end(), [&code](const SceneDiagnostic& diagnostic) {
		return diagnostic.code == code;
	});
}

static bool hasCodeAndSeverity(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code,
	SceneSeverity severity) {
	return std::any_of(diagnostics.begin(), diagnostics.end(), [&code, severity](const SceneDiagnostic& diagnostic) {
		return diagnostic.code == code && diagnostic.severity == severity;
	});
}

static SceneTrainUnit unit(const std::string& id, double tractionMass, double wagonMass,
	double wagons, double maxSpeed, double deceleration, double area, double resistance,
	double jerk, double length, const std::string& dataFile, const std::string& tractionFile,
	double firstBandForce, double secondBandForce) {
	SceneTrainUnit result;
	result.id = id;
	result.hasPhysical = true;
	result.physical = {tractionMass, wagonMass, wagons, maxSpeed, deceleration, area,
		resistance, jerk, length};
	result.tractionCurve = {{{0.0, 10.0, firstBandForce, 0.0, 0.0},
		{10.0, 20.0, secondBandForce, 0.0, 0.0}}};
	result.sourceDataFile = dataFile;
	result.sourceTractionFile = tractionFile;
	return result;
}

static SceneModel completeScene() {
	SceneModel scene;
	scene.name = "native-operations-fixture";
	scene.baseTime = "06:30:00";
	scene.settings.hasDuration = true;
	scene.settings.durationSeconds = 90.0;
	scene.settings.hasBufferTime = true;
	scene.settings.bufferTimeSeconds = 7.0;
	scene.settings.hasRecoveryTime = true;
	scene.settings.recoveryTimePercent = 12.0;
	scene.sourceFiles.insert("/does/not/exist/trains.json");
	scene.sourceFiles.insert("weird:provenance/services.json");

	scene.tracks = {{"track.native"}};
	scene.nodes = {{"node.0", "track.native", 0.0, 0.0},
		{"node.1", "track.native", 1.0, 0.0},
		{"node.2", "track.native", 2.0, 0.0},
		{"node.3", "track.native", 3.0, 0.0}};
	scene.arcs = {{"arc.0", "track.native", "node.0", "node.1", 0.0, 0.0, 20.0},
		{"arc.1", "track.native", "node.1", "node.2", 0.0, 0.0, 20.0},
		{"arc.2", "track.native", "node.2", "node.3", 0.0, 0.0, 20.0}};
	scene.blocks = {{"block.0", "track.native", 1.0}, {"block.1", "track.native", 1.0},
		{"block.2", "track.native", 1.0}};
	scene.signals = {{"signal.0", "block.1"}};

	scene.stations = {
		{"station.0", "Zero", false, 0.0, {{"platform.0", {"node.0"}}}},
		{"station.1", "One", false, 0.0, {{"platform.1", {"node.1"}}}},
		{"station.2", "Two", false, 0.0, {{"platform.2", {"node.2"}}}}};
	scene.routes = {{"route.native", {"block.0", "block.1", "block.2"}, false, {}, false}};
	scene.trainUnits = {
		unit("unit.1", 100.0, 40.0, 2.0, 30.0, 1.0, 2.0, 0.1, 1.0, 20.0,
			"/missing/unit-data", "weird:unit-traction", 1.0, 2.0),
		unit("unit.2", 80.0, 30.0, 1.0, 25.0, 1.2, 1.5, 0.2, 1.2, 10.0,
			"nonexistent:physical", "/not/a/file", 3.0, 4.0)};
	scene.compositions = {{"composition.native", {"unit.1", "unit.2"}}};

	SceneService service;
	service.id = "service.native";
	service.operatingCode = "9707";
	service.composition = "composition.native";
	service.route = "route.native";
	service.hasEntryTime = true;
	service.entryTimeSeconds = 100.0;
	service.hasRepeat = true;
	service.headwaySeconds = 30.0;
	service.stops = {
		{"station.0", "platform.0", false, true, -1.0, 110.0, 2.0},
		{"station.1", "platform.1", true, true, 120.0, 125.0, 3.0},
		{"station.2", "platform.2", true, false, 130.0, -1.0, 2.0}};
	scene.services.push_back(service);

	SceneScenario baseline;
	baseline.id = "scenario.base";
	scene.scenarios.push_back(baseline);
	SceneScenario selected;
	selected.id = "scenario.selected";
	selected.incidents = {{"incident.signal", "signal_failure", "signal.0", 20.0, 40.0},
		{"incident.breakdown", "train_breakdown", "service.native", 50.0, 70.0}};
	selected.entranceDelays = {{"service.native", 2, "station.0", 5.0},
		{"service.native", 2, "station.1", 5.0}};
	scene.scenarios.push_back(selected);
	scene.defaultScenarioId = "scenario.base";

	ScenePassenger passenger;
	passenger.id = "passenger.1";
	ScenePassengerJourney journey;
	journey.id = "journey.1";
	journey.activity = "work";
	journey.originStationId = "station.0";
	journey.destinationStationId = "station.2";
	journey.plannedDepartureStartSeconds = 100.0;
	journey.plannedDepartureEndSeconds = 110.0;
	journey.plannedArrivalStartSeconds = 130.0;
	journey.plannedArrivalEndSeconds = 140.0;
	journey.legs.push_back({"leg.1", "station.0", "station.1", "service.native", 2});
	journey.legs.push_back({"leg.2", "station.1", "station.2", "service.native", 2});
	passenger.journeys.push_back(journey);
	ScenePassengerJourney unrouted = journey;
	unrouted.id = "journey.unrouted";
	unrouted.legs.clear();
	passenger.journeys.push_back(unrouted);
	scene.passengers.push_back(passenger);
	return scene;
}

static bool near(double a, double b) {
	return std::fabs(a - b) < 1e-9;
}

static const unsigned long kSeedA = 123456789;
static const unsigned long kSeedB = 987654321;

// Dwell time of a boarding and alighting flow. The passenger split at the doors
// is drawn from the run generator, which is reseeded before each call so that
// every call gets the same draws.
static double seededDwellTime(Train& train, double platformRate, int boarding = 40, int alighting = 20,
	unsigned long seed = kSeedA) {
	seedRunNumberGenerator(seed);
	return train.computePaxDependentDwellTimeAtStations(boarding, alighting, platformRate,
		7.0f, 0.32f, 18.23f, 0.564f, 4.838f, 22.24f, 0.04f, 0.562f);
}

// Draws of the generator for fixed seeds, recorded from the algorithm as it is.
// The uniform draws are one product of an integer below 2^31 and the constant
// 1 / 2147483647, so they are identical on every platform. The Gaussian draws
// pass through sqrt and log, which may differ in the last bits between math
// libraries, so they are compared with a tolerance far above that.
static bool generatorTests() {
	bool ok = true;
	const auto close = [](double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; };
	const double uniformA[] = {0.91197702843322281, 0.061727229068860051, 0.3969884758801146, 0.35162865992245668};
	const int integerA[] = {91, 6, 39, 35, 82, 75};
	const double gaussianA[] = {-1.6577351000707685, -1.150935343416547, 0.54755957323394899, 0.69422240928046186};
	const double scaledA[] = {6.6845297998584634, 7.6981293131669055};
	const double uniformB[] = {0.71631288189269271, 0.78997236666733972, 0.84746502938096646, 0.094018201387495823};
	const int integerB[] = {71, 78, 84, 9, 89, 67};
	const double gaussianB[] = {0.91194289224928671, 0.68028894411965013, 0.29900609005941814, 0.68283721049533164};
	const double scaledB[] = {11.823885784498573, 11.3605778882393};
	// Draws at both ends of the valid seed range: seed 1 and the largest seed.
	const double uniformC[] = {0.41599935685098144, 0.091964890757559287, 0.75641048595142113, 0.52970019333516261};
	const int integerC[] = {41, 9, 75, 52, 93, 38};
	const double gaussianC[] = {-0.83685380259280617, -0.17227992407322446, 0.18711744825407453, 1.6154398490064397};
	const double scaledC[] = {8.3262923948143879, 9.6554401518535506};
	const double uniformD[] = {0.93315776248143878, 0.61649792251014057, 0.58251402554219311, 0.31322728763950397};
	const int integerD[] = {93, 61, 58, 31, 29, 8};
	const double gaussianD[] = {0.17116944175794566, 0.63643514664920209, -1.7312639403219627, 0.76485239833256835};
	const double scaledD[] = {10.342338883515891, 11.272870293298404};
	const struct {
		unsigned long seed;
		const double* uniform;
		const int* integer;
		const double* gaussian;
		const double* scaled;
	} recorded[] = {{kSeedA, uniformA, integerA, gaussianA, scaledA}, {789350715, uniformB, integerB, gaussianB, scaledB},
		{1, uniformC, integerC, gaussianC, scaledC}, {kMaxRandomSeed, uniformD, integerD, gaussianD, scaledD}};
	for (const auto& row : recorded) {
		NumberGenerator uniform(row.seed);
		for (int index = 0; index < 4; ++index)
			ok &= expect(close(uniform.getUniformFloat(), row.uniform[index], 1e-15), "uniform draws of a fixed seed match");
		NumberGenerator integer(row.seed);
		for (int index = 0; index < 6; ++index)
			ok &= expect(integer.getUniformInteger(0, 99) == row.integer[index], "integer draws of a fixed seed match");
		NumberGenerator gaussian(row.seed);
		for (int index = 0; index < 4; ++index)
			ok &= expect(close(gaussian.getGaussianFloat(), row.gaussian[index], 1e-12),
				"Gaussian draws of a fixed seed match");
		NumberGenerator scaled(row.seed);
		for (int index = 0; index < 2; ++index)
			ok &= expect(close(scaled.getGaussianFloat(10, 2), row.scaled[index], 1e-12),
				"scaled Gaussian draws of a fixed seed match");
	}
	NumberGenerator first(kSeedA), second(kSeedA);
	second.getUniformFloat();
	ok &= expect(first.getUniformFloat() != second.getUniformFloat(), "draws advance the generator");

	ok &= expect(kMaxRandomSeed == 2147483646UL, "the largest seed is the number given for --seed");

	// The 1286th draw of seed 1 is the first one that reaches the upper limit of 1 - 1.2e-7. The value
	// before the limit is applied is 2147483531 / 2147483647.
	const double aroundLimit[] = {0.51375901117630252, 0.961920628306419};
	NumberGenerator limit(1);
	for (int index = 0; index < 1284; ++index)
		limit.getUniformFloat();
	ok &= expect(close(limit.getUniformFloat(), aroundLimit[0], 1e-15), "the draw before the upper limit matches");
	ok &= expect(close(limit.getUniformFloat(), 1.0 - 1.2e-7, 1e-15), "a draw above 1 - 1.2e-7 is limited to 1 - 1.2e-7");
	ok &= expect(close(limit.getUniformFloat(), aroundLimit[1], 1e-15), "the draw after the upper limit matches");

	// Restarting the run generator from a seed repeats its draws, and the default seed restores its initial state.
	seedRunNumberGenerator(kSeedA);
	const double firstRunDraw = runNumberGenerator().getUniformFloat();
	ok &= expect(close(firstRunDraw, uniformA[0], 1e-15), "the run generator starts from the seed it is given");
	runNumberGenerator().getUniformFloat();
	seedRunNumberGenerator(kSeedA);
	ok &= expect(close(runNumberGenerator().getUniformFloat(), firstRunDraw, 1e-15), "restarting the run generator repeats its draws");
	seedRunNumberGenerator(kDefaultRandomSeed);
	return ok;
}

// Passenger time windows of the fixture after a scene is prepared with a seed.
static std::vector<double> sampledWindows(const SceneModel& scene, unsigned long seed) {
	initial_variables.randomSeed = seed;
	std::vector<double> windows;
	if (hasErrors(buildInfrastructureAndSignallingFromScene(scene))
		|| hasErrors(buildOperationsFromScene(scene, "scenario.base")))
		return windows;
	for (const Passenger& passenger : AllDailyPassengers)
		for (const Journey& journey : passenger.Journeys) {
			windows.push_back(journey.Actual_Planned_Departure_Time);
			windows.push_back(journey.Actual_Planned_Arrival_Time);
		}
	return windows;
}

static bool seedTests(const SceneModel& scene) {
	bool ok = true;
	Train train;
	train.number_of_wagons = 3.0;
	train.MAX_OnBoard_Passengers = trainPassengerCapacity(train.number_of_wagons);
	const double dwellA = seededDwellTime(train, 0.5, 800, 800, kSeedA);
	ok &= expect(near(dwellA, seededDwellTime(train, 0.5, 800, 800, kSeedA)), "equal seeds give equal dwell times");
	ok &= expect(!near(dwellA, seededDwellTime(train, 0.5, 800, 800, kSeedB)), "different seeds give different dwell times");

	// Draws continue from the generator state instead of restarting for every car.
	seedRunNumberGenerator(kSeedA);
	const double firstStop = train.computePaxDependentDwellTimeAtStations(800, 800, 0.5,
		7.0f, 0.32f, 18.23f, 0.564f, 4.838f, 22.24f, 0.04f, 0.562f);
	const double secondStop = train.computePaxDependentDwellTimeAtStations(800, 800, 0.5,
		7.0f, 0.32f, 18.23f, 0.564f, 4.838f, 22.24f, 0.04f, 0.562f);
	ok &= expect(near(firstStop, dwellA) && !near(firstStop, secondStop), "a second stop draws on from the first");

	const std::vector<double> windowsA = sampledWindows(scene, kSeedA);
	ok &= expect(windowsA.size() == 4, "the fixture has two sampled journeys");
	ok &= expect(sampledWindows(scene, kSeedA) == windowsA, "equal seeds sample equal passenger windows");
	ok &= expect(sampledWindows(scene, kSeedB) != windowsA, "different seeds sample different passenger windows");
	ok &= expect(sampledWindows(scene, kSeedA) == windowsA, "a run with another seed in between does not change a rerun");
	ok &= expect(initial_variables.randomSeed == kSeedA, "the seed of the run is kept in the run settings");
	return ok;
}

// Neither the dwell computation nor the scene preparation leaves a file in the working directory.
static bool noFileAccessTests(const SceneModel& scene) {
	QTemporaryDir workDir;
	const QString previousDir = QDir::currentPath();
	if (!expect(workDir.isValid() && QDir::setCurrent(workDir.path()), "temporary working directory"))
		return false;
	Train train;
	train.number_of_wagons = 3.0;
	train.MAX_OnBoard_Passengers = trainPassengerCapacity(train.number_of_wagons);
	seededDwellTime(train, 0.5, 800, 800);
	bool ok = expect(!sampledWindows(scene, kSeedA).empty(), "the scene is prepared in the temporary directory");
	const QStringList entries = QDir(workDir.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
	ok &= expect(entries.isEmpty(), "no file appears in the working directory (found: " + entries.join(",").toStdString() + ")");
	ok &= expect(QDir::setCurrent(previousDir), "working directory restored");
	return ok;
}

// Braking-point search. A route of one section with one arc, from startKm to endKm.
static std::vector<Section> brakingRoute(double startKm, double endKm, double gradient) {
	std::vector<Section> route(1);
	Section& section = route[0];
	section.start_node.X = startKm;
	section.end_node.X = endKm;
	section.total_arcs = 1;
	section.arcs_in_signalling_block_section[0].startNode.X = startKm;
	section.arcs_in_signalling_block_section[0].endNode.X = endKm;
	section.arcs_in_signalling_block_section[0].gradient = gradient;
	section.arcs_in_signalling_block_section[0].curvature = 0.0;
	return route;
}

static Train brakingTrain(double deceleration) {
	Train train;
	train.g = 9.81;
	train.mass_of_traction_unit = 100000.0;
	train.total_train_mass = 100000.0;
	train.massFactor = 1.09;
	train.max_train_decelaration = deceleration;
	train.Jerk = 0.75;
	return train;
}

// Fills the stack below the caller with a value, so that a local array that is read before it is
// written takes the value of the caller's choice.
static TEST_NOINLINE void poisonStack(double value) {
	volatile double junk[6000];
	for (int index = 0; index < 6000; ++index)
		junk[index] = value;
}

static bool brakingPointTests() {
	bool ok = true;
	const double savedTimestep = timestep;
	timestep = 1.0;
	const auto at = [](double value, double expected) { return std::fabs(value - expected) <= 1e-6; };
	Train train = brakingTrain(1.0);
	std::vector<Section> route = brakingRoute(0.0, 10.0, 0.0);

	// The curve reaches the current speed inside the route: the braking point is interpolated
	// between the two steps around that speed.
	ok &= expect(at(train.BrakDist_Block(30.0, 0.0, 5000.0, route.data(), 1), 4601.818895),
		"braking point of a stop from 30 m/s on a flat route");
	ok &= expect(at(train.BrakDist_Block(30.0, 10.0, 5000.0, route.data(), 1), 4644.466708),
		"braking point of a slowdown from 30 to 10 m/s on a flat route");
	ok &= expect(train.BrakDist_Block(10.0, 10.0, 5000.0, route.data(), 1) == 5000.0
			&& train.BrakDist_Block(10.0, 30.0, 5000.0, route.data(), 1) == 5000.0,
		"no braking is needed when the current speed does not exceed the target speed");

	// The curve passes the start of the route before it reaches the current speed: the braking
	// point lies before the route and is the last abscissa that was computed.
	std::vector<Section> later = brakingRoute(2.0, 12.0, 0.0);
	ok &= expect(at(train.BrakDist_Block(30.0, 0.0, 100.0, route.data(), 1), -9.763361),
		"a braking curve that leaves the route at its start gives a braking point before the route");
	ok &= expect(at(train.BrakDist_Block(30.0, 0.0, 2100.0, later.data(), 1), 1990.236639),
		"the start of the route is the one of the first section");

	// The braking force cannot hold the train on a steep descent: the curve runs forward and
	// leaves the route at its end.
	std::vector<Section> steep = brakingRoute(0.0, 10.0, -0.5);
	ok &= expect(train.BrakDist_Block(30.0, 10.0, 5000.0, steep.data(), 1) == -1,
		"a braking curve that leaves the route at its end gives no braking point");
	// 1,999 steps are not enough on a long, almost balanced slope.
	Train slow = brakingTrain(0.001);
	std::vector<Section> farRoute = brakingRoute(0.0, 100.0, -0.0044);
	ok &= expect(slow.BrakDist_Block(30.0, 0.0, 50000.0, farRoute.data(), 1) == -1,
		"a curve that does not reach the current speed in 1,999 steps gives no braking point");

	// The result does not depend on what the stack held before the call.
	for (const double value : {0.0, 1e30, -1e30, std::numeric_limits<double>::quiet_NaN()}) {
		poisonStack(value);
		const double start = train.BrakDist_Block(30.0, 0.0, 100.0, route.data(), 1);
		poisonStack(value);
		const double end = train.BrakDist_Block(30.0, 10.0, 5000.0, steep.data(), 1);
		ok &= expect(at(start, -9.763361) && end == -1,
			"the braking point of a curve that leaves the route does not depend on the stack");
	}
	timestep = savedTimestep;
	return ok;
}

// Braking phase. A route of one section with two arcs: the first from 0 km to splitKm with the gradient
// firstGradient, the second from splitKm to endKm with the gradient secondGradient.
static std::vector<Section> brakingRouteOfTwoArcs(double splitKm, double endKm, double firstGradient, double secondGradient) {
	std::vector<Section> route = brakingRoute(0.0, endKm, firstGradient);
	Section& section = route[0];
	section.total_arcs = 2;
	section.arcs_in_signalling_block_section[0].endNode.X = splitKm;
	section.arcs_in_signalling_block_section[1].startNode.X = splitKm;
	section.arcs_in_signalling_block_section[1].endNode.X = endKm;
	section.arcs_in_signalling_block_section[1].gradient = secondGradient;
	section.arcs_in_signalling_block_section[1].curvature = 0.0;
	return route;
}

struct BrakingRun {
	std::vector<double> position, speed;
	std::vector<int> eq;
	bool gradientExceptionClear = true; // GradientExceptionInBraking was false after every step
};

// Takes `steps` braking steps towards (targetSpeed, targetPosition) of a train that ran at `speed` and is at
// `position`, one second before. The vectors have exactly the size the steps need, with NaN in the entries that
// are not set. GradientExceptionInBraking is set before each step.
static BrakingRun brakingRun(Train& train, std::vector<Section>& route, double position, double speed,
	double targetSpeed, double targetPosition, int steps) {
	const int size = steps + 2;
	BrakingRun run;
	run.position.assign(size, std::numeric_limits<double>::quiet_NaN());
	run.speed.assign(size, std::numeric_limits<double>::quiet_NaN());
	run.eq.assign(size, 0);
	run.position[0] = position - speed * timestep;
	run.position[1] = position;
	run.speed[0] = run.speed[1] = speed;
	train.instant_spatial_position = run.position;
	train.instant_train_speed = run.speed;
	train.Eq = run.eq;
	train.Xobmin = targetPosition;
	train.Vobmin = targetSpeed;
	for (int t = 2; t < size; ++t) {
		const Section& section = route[0];
		Arc arc = section.arcs_in_signalling_block_section[0];
		for (int j = 0; j < section.total_arcs; ++j) {
			const Arc& candidate = section.arcs_in_signalling_block_section[j];
			if (train.instant_spatial_position[t - 1] >= candidate.startNode.X * 1000
				&& train.instant_spatial_position[t - 1] < candidate.endNode.X * 1000)
				arc = candidate;
		}
		train.GradientExceptionInBraking = true;
		train.brakingStep(t, arc, route.data(), 1);
		run.gradientExceptionClear &= !train.GradientExceptionInBraking;
	}
	run.position = train.instant_spatial_position;
	run.speed = train.instant_train_speed;
	run.eq = train.Eq;
	return run;
}

// The braking phase of a train whose braking curve does not reach its speed. The train has the values of
// brakingTrain(1.0) from the braking-point tests, a timestep of one second and the speed and target speed of the
// Lebanon trace (36.1111 and 16.67 m/s).
static bool brakingCurveTests() {
	bool ok = true;
	const double savedTimestep = timestep;
	timestep = 1.0;
	const auto at = [](double value, double expected) { return std::fabs(value - expected) <= 1e-6; };
	const double speed = 36.1111, targetSpeed = 16.67;

	// A curve that reaches the speed is reported as such and is stored in driving order.
	{
		Train train = brakingTrain(1.0);
		std::vector<Section> flat = brakingRoute(0.0, 10.0, 0.0);
		ok &= expect(train.DrawBrakingCurve(30.0, 0.0, 5000.0, flat.data(), 1) && train.BrakStep > 0
				&& train.Vbrak[0] >= 30.0 && at(train.Sbrak[train.BrakStep], 4999.998) && train.Vbrak[train.BrakStep] == 0.0,
			"a braking curve that reaches the current speed is stored from the first step to the target");
		// A speed that does not exceed the target speed has no curve either, and the curve before it is not kept.
		ok &= expect(!train.DrawBrakingCurve(10.0, 20.0, 5000.0, flat.data(), 1) && train.BrakStep == -1,
			"a speed below the target speed gives no braking curve and marks the curve empty");
		train.DrawBrakingCurve(30.0, 0.0, 5000.0, flat.data(), 1);
		ok &= expect(!train.DrawBrakingCurve(20.0, 20.0, 5000.0, flat.data(), 1) && train.BrakStep == -1,
			"a speed equal to the target speed gives no braking curve and marks the curve empty");
	}

	struct Case {
		const char* name;
		std::vector<Section> route;
		double position, targetPosition;
		int steps;
		bool fallsBelowTarget;
	};
	// The first case has the shape of the Lebanon trace: the target at 1673 m lies on an arc with a gradient of
	// -9.62, so the curve runs forward and leaves the route, while the train is on a flat arc before it and runs
	// onto the steep arc during the steps. In the second case the target lies 100 m after the start of a flat
	// route: the curve needs more than 500 m, so the braking point lies before the route. In the third case the
	// train is 673 m before the target and slows down below the target speed on the flat arc.
	std::vector<Case> cases = {
		{"a curve that leaves the route at its end", brakingRouteOfTwoArcs(1.594, 2.0, 0.0, -9.62), 1390.28, 1673.0, 8, false},
		{"a curve that leaves the route at its start", brakingRoute(0.0, 10.0, 0.0), 50.0, 100.0, 8, false},
		{"a curve that leaves the route at its end for a train that slows down below the target speed",
			brakingRouteOfTwoArcs(1.594, 2.0, 0.0, -9.62), 1000.0, 1673.0, 22, true},
	};
	for (Case& c : cases) {
		const std::string name = c.name;
		const int steps = c.steps;
		Train fresh = brakingTrain(1.0);
		ok &= expect(!fresh.DrawBrakingCurve(speed, targetSpeed, c.targetPosition, c.route.data(), 1) && fresh.BrakStep == -1,
			name + ": the curve is reported as not reaching the speed and is marked empty");

		// A reachable curve was drawn before: nothing of it is read, and it is not kept.
		Train earlier = brakingTrain(1.0);
		std::vector<Section> flat = brakingRoute(0.0, 10.0, 0.0);
		ok &= expect(earlier.DrawBrakingCurve(30.0, 0.0, 5000.0, flat.data(), 1) && earlier.BrakStep > 0,
			name + ": a reachable curve is drawn first");
		ok &= expect(!earlier.DrawBrakingCurve(speed, targetSpeed, c.targetPosition, c.route.data(), 1) && earlier.BrakStep == -1,
			name + ": the curve after it is reported as not reaching the speed and is marked empty");

		Train withoutCurve = brakingTrain(1.0);
		const BrakingRun reference = brakingRun(withoutCurve, c.route, c.position, speed, targetSpeed, c.targetPosition, steps);
		Train withCurve = brakingTrain(1.0);
		withCurve.DrawBrakingCurve(30.0, 0.0, 5000.0, flat.data(), 1);
		const BrakingRun run = brakingRun(withCurve, c.route, c.position, speed, targetSpeed, c.targetPosition, steps);
		ok &= expect(run.position == reference.position && run.speed == reference.speed,
			name + ": the steps do not depend on the curve that was drawn before");

		// Each step is the step of full braking: the deceleration is the full braking force plus the
		// resistances, divided by the mass with its mass factor, and the train moves by the mean speed.
		bool followsFullBraking = true, boundHolds = true, setOnlyWhereComputed = true;
		for (int t = 1; t < steps + 2; ++t) {
			const double moved = run.position[t] - run.position[t - 1];
			boundHolds &= moved >= 0.0 && moved <= std::max(run.speed[t - 1], run.speed[t]) * timestep + 1e-9;
			if (t < 2)
				continue;
			const double mass = withoutCurve.total_train_mass * withoutCurve.massFactor;
			const Arc* arc = nullptr;
			for (int j = 0; j < c.route[0].total_arcs; ++j) {
				const Arc& candidate = c.route[0].arcs_in_signalling_block_section[j];
				if (run.position[t - 1] >= candidate.startNode.X * 1000 && run.position[t - 1] < candidate.endNode.X * 1000)
					arc = &candidate;
			}
			if (arc == nullptr)
				arc = &c.route[0].arcs_in_signalling_block_section[0];
			const double deceleration = (mass * withoutCurve.max_train_decelaration
											+ withoutCurve.total_train_resistances(run.speed[t - 1], arc->gradient, arc->curvature))
				/ mass;
			const double expectedSpeed = std::max(0.0, run.speed[t - 1] - deceleration * timestep);
			followsFullBraking &= std::fabs(run.speed[t] - expectedSpeed) <= 1e-9
				&& std::fabs(run.position[t] - (run.position[t - 1] + (run.speed[t - 1] + expectedSpeed) / 2 * timestep)) <= 1e-9;
			setOnlyWhereComputed &= std::isfinite(run.position[t]) && std::isfinite(run.speed[t]);
		}
		ok &= expect(followsFullBraking, name + ": position and speed follow the step of full braking");
		ok &= expect(boundHolds && run.position[1] == c.position && run.speed[1] == speed,
			name + ": a train moves forward by no more than its larger speed times the step and the step keeps the position before it");
		ok &= expect(setOnlyWhereComputed && std::isfinite(run.position[0]) && std::isfinite(run.position[1]),
			name + ": every step is computed");
		ok &= expect(run.gradientExceptionClear && (!c.fallsBelowTarget || run.speed[steps + 1] < targetSpeed),
			name + ": a step without curve clears the gradient exception, also below the target speed");
		ok &= expect(run.eq[2] == 54, name + ": the step is marked as a step without curve");
	}

	// Values of single steps of the two cases, computed with the formula of the step.
	{
		std::vector<Section> route = brakingRouteOfTwoArcs(1.594, 2.0, 0.0, -9.62);
		Train train = brakingTrain(1.0);
		const BrakingRun run = brakingRun(train, route, 1390.28, speed, targetSpeed, 1673.0, 8);
		ok &= expect(at(run.speed[2], 34.934396) && at(run.position[2], 1425.802748),
			"first step of the case with the curve that leaves the route at its end, on the flat arc");
		ok &= expect(at(run.speed[9], 113.485645) && at(run.position[9], 1685.336327),
			"step of the same case on the steep arc, where the train gains speed");
	}
	{
		std::vector<Section> route = brakingRoute(0.0, 10.0, 0.0);
		Train train = brakingTrain(1.0);
		const BrakingRun run = brakingRun(train, route, 50.0, speed, targetSpeed, 100.0, 8);
		ok &= expect(at(run.speed[2], 34.934396) && at(run.position[2], 85.522748),
			"first step of the case with the braking point before the route");
		Train search = brakingTrain(1.0);
		ok &= expect(search.BrakDist_Block(speed, targetSpeed, 100.0, route.data(), 1) < 50.0,
			"the braking-point search gives a braking point before the train for the second case");
	}
	timestep = savedTimestep;
	return ok;
}

static bool passengerRateTests() {
	bool ok = true;
	ok &= expect(near(passengerOccupancyRatio(0, 600), 0.0), "occupancy ratio at 0 percent");
	ok &= expect(near(passengerOccupancyRatio(300, 600), 0.5), "occupancy ratio at 50 percent");
	ok &= expect(near(passengerOccupancyRatio(480, 600), 0.8), "occupancy ratio at 80 percent");
	ok &= expect(near(passengerOccupancyRatio(600, 600), 1.0), "occupancy ratio at 100 percent");
	ok &= expect(near(passengerOccupancyRatio(7, 0), 0.0) && near(passengerOccupancyRatio(7, -1), 0.0),
		"occupancy ratio of an unknown capacity is zero");
	ok &= expect(trainPassengerCapacity(0.0) == 300, "capacity of a train without wagons");
	ok &= expect(trainPassengerCapacity(1.0) == 600, "capacity of a train with one wagon");
	ok &= expect(trainPassengerCapacity(3.0) == 1200, "capacity of a train with three wagons");

	Train train;
	train.number_of_wagons = 3.0;
	train.MAX_OnBoard_Passengers = trainPassengerCapacity(train.number_of_wagons);
	const float beta1 = 0.32f, beta3 = 0.564f, beta7 = 0.562f;

	train.Current_OnBoard_Passengers = 600; // 50 percent of 1200
	const double lowOnboard = seededDwellTime(train, 0.5);
	ok &= expect(near(lowOnboard, seededDwellTime(train, 0.5)), "equal seeds give equal dwell times");
	train.Current_OnBoard_Passengers = 960; // 80 percent of 1200
	const double highOnboard = seededDwellTime(train, 0.5);
	ok &= expect(near(highOnboard - lowOnboard, beta7),
		"on-board congestion above 0.7 adds its term to the dwell time");

	train.Current_OnBoard_Passengers = 0;
	const double lowPlatform = seededDwellTime(train, 0.6);
	const double highPlatform = seededDwellTime(train, 0.7);
	ok &= expect(near(highPlatform - lowPlatform, static_cast<double>(beta1) + beta3),
		"platform congestion above 0.65 adds its terms to the dwell time");

	return ok;
}

// Block sections of 2 km on the heap, in an array of exactly n, so that a sanitizer reports an
// access before the first or after the last section. The speed limit and the train length are
// the ones of the characterization line scene.
static std::vector<Section> boundarySections(int n) {
	std::vector<Section> sections(n);
	for (int i = 0; i < n; ++i) {
		Section& section = sections[i];
		section.ID = "@boundary." + std::to_string(i) + "@";
		section.start_node.X = 2.0 * i;
		section.end_node.X = 2.0 * (i + 1);
		section.total_arcs = 1;
		section.arcs_in_signalling_block_section[0].speedLimit = 36.111111111111;
	}
	return sections;
}

// Sections 0 to 3 of the level west and 4 to 7 of the level east, after the routines of levels 0, 1 and 2 have run, in
// the order of a step, for a train whose tail is in the section tail and whose head is in the next one.
static std::vector<Section> borderAspects(int west, int east, int tail) {
	std::vector<Section> sections = boundarySections(8);
	for (int i = 0; i < 8; ++i) {
		sections[i].SignallingLevel = i < 4 ? west : east;
		sections[i].code = 270;
		sections[i].arcs_in_signalling_block_section[0].signalSpeedLimit = 999;
	}
	std::vector<char> occupied(8, 0);
	occupied[tail] = occupied[tail + 1] = 1;
	atbMixedSignalling(signalCode1, signalCode3, sections.data(), 8, occupied.data());
	etcsLev1MixedSignalling(signalCode3, sections.data(), 8, occupied.data());
	etcsLev2MixedSignalling(signalCode3, sections.data(), 8, occupied.data());
	return sections;
}

// At a border between level 0 and level 1 or 2 an occupied section keeps code 0 and the sections behind it get the
// chain of their own level, on both sides of the border.
static bool levelBorderAspectTests() {
	bool ok = true;
	const auto limit = [](const Section& section) { return section.arcs_in_signalling_block_section[0].signalSpeedLimit; };
	const auto chain = [](const std::vector<Section>& sections, int first, int last) {
		std::string codes;
		for (int i = first; i <= last; ++i)
			codes += std::to_string(static_cast<int>(sections[i].code)) + " ";
		return codes;
	};
	for (int level : {1, 2}) {
		const std::string name = "level " + std::to_string(level);
		// The tail is in the last section of level 0 and the head in the first section of the other level.
		std::vector<Section> sections = borderAspects(0, level, 3);
		ok &= expect(chain(sections, 0, 4) == "270 180 75 0 0 " && limit(sections[2]) == signalCode1 && limit(sections[1]) == 999,
			"a section of level 0 keeps code 0 while the head of its train is in " + name);
		// The train is two sections past the border, so only the chain behind it crosses the border.
		sections = borderAspects(0, level, 5);
		ok &= expect(chain(sections, 2, 6) == "270 180 75 0 0 " && limit(sections[4]) == 999 && limit(sections[3]) == 999,
			"a section of level 0 behind an occupied section of " + name + " keeps the chain of level 0");
		// The same two positions with the levels the other way round.
		sections = borderAspects(level, 0, 3);
		ok &= expect(chain(sections, 0, 4) == "270 180 75 0 0 " && limit(sections[2]) == 999,
			"a section of " + name + " keeps code 0 while the head of its train is in level 0");
		sections = borderAspects(level, 0, 4);
		ok &= expect(chain(sections, 1, 5) == "270 180 75 0 0 ",
			"a section of " + name + " before an occupied section of level 0 shows 75");
		sections = borderAspects(level, 0, 5);
		ok &= expect(chain(sections, 2, 6) == "270 180 75 0 0 ",
			"the sections of " + name + " continue the chain behind a section of level 0 with code 75");
		ok &= expect(limit(sections[4]) == signalCode1 && limit(sections[3]) == 999 && limit(sections[2]) == 999,
			"the routine of " + name + " leaves the speed limit of a level 0 section with code 75");
		// A section without a level takes the chain of an occupied section of level 1 or 2 and of no other level.
		sections = borderAspects(kSignallingLevelUnset, level, 4);
		ok &= expect(chain(sections, 1, 5) == "270 180 75 0 0 ", "a section without a level takes the chain behind an occupied section of " + name);
	}
	std::vector<Section> sections = borderAspects(kSignallingLevelUnset, 0, 4);
	ok &= expect(chain(sections, 1, 5) == "270 270 270 0 0 ", "a section without a level takes no chain behind an occupied section of level 0");
	return ok;
}

// A train whose delayed position at index 1 is headPosition.
static void placeBoundaryTrain(Train& train, double headPosition) {
	train.type = "T";
	train.ID = 1.0;
	train.indexOfRoute = 0;
	train.train_length = 70.0;
	train.departure_time = 0.0;
	train.CanEnter = true;
	train.OutOfSimulation = false;
	train.instant_spatial_position = {headPosition - 100.0, headPosition};
}

// The section before a train that sits on the first section of its route is not read when that
// section starts with a virtual signal.
static bool routeBoundaryTests() {
	bool ok = true;
	std::vector<Route> savedRoutes;
	savedRoutes.swap(train_route);
	train_route.resize(1);
	train_route[0].x_of_end_node = 100.0;
	const double savedTimestep = timestep;
	const double savedDelay = S_delay;
	const auto savedOccupied = BlocksOccupied;
	const auto savedConnected = BlocksConnected;
	timestep = 1.0;
	S_delay = 0.0;

	{
		// The tail is in the second section of the route, which follows a first section that starts with a
		// virtual signal. The first section is the second half of a double switch over two global sections.
		// The section before the route is a double switch too and is poisoned for a sanitizer, so that
		// reading it is reported, and it releases visibly when it is read in a plain build.
		const int savedBlocks = Blocks;
		std::vector<Section> savedSections(2);
		savedSections.swap(signalling_block_sections);
		Blocks = 2;
		signalling_block_sections[0].ID = "@boundary.a@";
		signalling_block_sections[1].ID = "@boundary.b@";
		std::vector<Section> storage = boundarySections(3);
		Section* sections = storage.data() + 1;
		storage[0].ID = "@boundary.a@-2.0/@boundary.b@-2.0";
		sections[0].ID = "@boundary.a@-1.0/@boundary.b@-1.0";
		sections[0].start_node.virtualSignal = true;
		Train train;
		placeBoundaryTrain(train, 5000.0);
		BlocksOccupied.clear();
		BlocksConnected.clear();
#ifdef TEST_ADDRESS_SANITIZER
		ASAN_POISON_MEMORY_REGION(&storage[0], sizeof(Section));
#endif
		train.Det_Section_Occupied_By_Train(1, sections, 2);
#ifdef TEST_ADDRESS_SANITIZER
		ASAN_UNPOISON_MEMORY_REGION(&storage[0], sizeof(Section));
#endif
		const bool beforeRouteUntouched = std::find(BlocksConnected.begin(), BlocksConnected.end(), storage[0].ID) == BlocksConnected.end();
		savedSections.swap(signalling_block_sections);
		Blocks = savedBlocks;
		ok &= expect(BlocksOccupied.size() == 1 && BlocksOccupied.front() == sections[1].ID && beforeRouteUntouched,
			"a first section that starts with a virtual signal has no double switch before it to release");
	}

	timestep = savedTimestep;
	S_delay = savedDelay;
	BlocksOccupied = savedOccupied;
	BlocksConnected = savedConnected;
	train_route.swap(savedRoutes);
	return ok;
}

// regional_train holds exactly the trains of the last build and is released by a reset.
static bool regionalTrainStorageTests() {
	bool ok = true;
	const auto buildTrains = [](int count) {
		SceneModel scene = completeScene();
		scene.services[0].hasRepeatCount = true;
		scene.services[0].repeatCount = count;
		const auto infrastructure = buildInfrastructureAndSignallingFromScene(scene);
		return hasErrors(infrastructure) ? infrastructure : buildOperationsFromScene(scene, "scenario.base");
	};
	for (const int count : {3, 5, 2}) {
		ok &= expect(!hasErrors(buildTrains(count)), "train storage fixture builds");
		ok &= expect(numRegions == count && regional_train.size() == static_cast<std::size_t>(count)
				&& regional_train.capacity() == static_cast<std::size_t>(count),
			"the train storage holds exactly the trains of the build");
	}

	const Regional* const keptStorage = regional_train.data();
	SceneModel rejected = completeScene();
	ok &= expect(hasErrors(buildOperationsFromScene(rejected, "scenario.missing"))
			&& regional_train.data() == keptStorage && regional_train.size() == 2
			&& !regional_train[0].Stations.empty(),
		"a rejected build leaves the train storage untouched");

	resetNativeOperationsState();
	ok &= expect(numRegions == 0 && regional_train.empty() && regional_train.capacity() == 0,
		"a reset releases the train storage");
	return ok;
}

// A single-track section is held by the direction of the trains in it. Trains of the other direction see
// its sections as occupied, trains of the same direction do not, and the sections are released when it
// changes holder or becomes free.
static bool singleTrackLockTests() {
	bool ok = true;
	std::vector<Route> savedRoutes;
	savedRoutes.swap(train_route);
	const auto savedLimits = singleTrackLimits;
	const auto savedOccupied = BlocksOccupied;
	const auto savedConnected = BlocksConnected;
	const auto savedHeld = singleTrackHeld;
	const int savedRegions = numRegions;
	// Two trains of its own, whatever the last build left in the storage.
	std::vector<Regional> savedTrains(2);
	savedTrains.swap(regional_train);
	const double savedTimestep = timestep;
	const double savedDelay = S_delay;
	timestep = 1.0;
	S_delay = 0.0;

	// Two routes over sections lock.0 to lock.5 of 2 km: route 0 forward, route 1 the same sections reversed.
	train_route.resize(2);
	for (int r = 0; r < 2; ++r) {
		Route& route = train_route[r];
		route.N_Block_Sections = 6;
		route.sequence_of_block_sections.resize(6);
		route.reversed_direction = (r == 1);
		const std::vector<Section> sections = boundarySections(6);
		for (int b = 0; b < 6; ++b) {
			route.sequence_of_block_sections[b] = sections[b];
			route.sequence_of_block_sections[b].ID = "lock." + std::to_string(r == 0 ? b : 5 - b);
			Arc& arc = route.sequence_of_block_sections[b].arcs_in_signalling_block_section[0];
			arc.startNode.X = 2.0 * b;
			arc.endNode.X = 2.0 * (b + 1);
			arc.gradient = arc.curvature = 0.0;
		}
	}
	singleTrackLimits.clear();
	singleTrackLimits.emplace_back("lock.1", "lock.4", "lock.0", "lock.5");
	resetSingleTrackLocks();

	// The zone of a route is derived once: neighbouring sections form one interval of route position.
	ok &= expect(singleTrackZone(0, 0).intervals.size() == 1 && singleTrackZone(0, 0).intervals[0].first == 0.0
			&& singleTrackZone(0, 0).intervals[0].second == 12000.0 && singleTrackZone(0, 0).sectionIDs.size() == 6,
		"neighbouring zone sections merge into one interval");
	ok &= expect(singleTrackZone(0, 1).sectionIDs.size() == 6 && singleTrackZone(0, 1).sectionIDs.front() == "lock.5",
		"the zone sections of a route are listed in route order");
	ok &= expect(singleTrackZone(1, 0).intervals.empty() && singleTrackZone(0, 2).intervals.empty(),
		"a limit or a route that does not exist has no zone");

	auto place = [](int k, int route, double head, bool active) {
		Regional& train = regional_train[k];
		train.indexOfRoute = route;
		train.departure_time = 0.0;
		train.CanEnter = true;
		train.OutOfSimulation = !active;
		train.train_length = 70.0;
		train.instant_spatial_position = {head - 100.0, head};
		train.instant_train_speed = {36.111111111111, 36.111111111111};
		// SLT_Sprinter values from the unchanged characterization fixture.
		train.trainDescription = "lock-train-" + std::to_string(k);
		train.Start_Node_X = 0.0;
		train.total_train_mass = train.mass_of_traction_unit = 151000.0;
		train.massFactor = 1.09;
		train.max_train_speed = 36.111111111111;
		train.max_train_decelaration = train.Jerk = 0.75;
		train.frontal_wagon_area = 1.45;
		train.resistanceCoefficient = 0.004;
		train.velocityIntervals = 2;
		train.Vlb[0] = 0.0;
		train.Vub[0] = train.Vlb[1] = 8.611111111111;
		train.Vub[1] = train.max_train_speed;
		train.C0[0] = 209000.0;
		train.C0[1] = 324607.2915;
		train.C1[1] = -17671.4923;
		train.C2[1] = 292.9005;
	};
	auto has = [](const std::list<std::string>& list, const std::string& id) {
		return std::find(list.begin(), list.end(), id) != list.end();
	};
	numRegions = 2;
	place(0, 0, 3000.0, true);	// forward, in lock.1
	place(1, 1, 3000.0, false); // reversed, not in the network
	BlocksOccupied.clear();
	BlocksConnected.clear();

	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld.size() == 1 && singleTrackHeld[0] == 1, "a forward train in the section holds it forward");
	ok &= expect(has(BlocksConnected, "lock.0") && has(BlocksConnected, "lock.3") && has(BlocksConnected, "lock.5"),
		"the sections are released when the holder changes, so that stale aspects are reset");
	ok &= expect(occupySingleTrackForRoute(0) == 0 && BlocksOccupied.empty(),
		"a route in the direction of the holder is not affected");
	ok &= expect(occupySingleTrackForRoute(1) == 6 && BlocksOccupied.size() == 6 && has(BlocksOccupied, "lock.5")
			&& has(BlocksOccupied, "lock.0"),
		"a route against the holder sees the protected and plain sections as occupied");
	BlocksOccupied.clear();
	BlocksOccupied.push_back("lock.2");
	ok &= expect(occupySingleTrackForRoute(1) == 5 && BlocksOccupied.size() == 6,
		"a section that is occupied already is not added twice");
	BlocksOccupied.clear();

	// A second forward train follows: still held forward, nothing new is released, the follower is not affected.
	place(1, 0, 1000.0, true); // forward, in lock.0 (the protected section before the first plain section)
	BlocksConnected.clear();
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 1 && BlocksConnected.empty(), "a following train in the same direction keeps the holder");

	// A train against the holder enters while the first one is still inside: it does not take the section.
	place(1, 1, 3000.0, true); // reversed, in lock.4
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 1, "a train of the other direction that enters later does not take the section");

	// The forward train leaves: the section passes to the reversed train, and is released on the change.
	place(0, 0, 3000.0, false);
	BlocksConnected.clear();
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == -1 && has(BlocksConnected, "lock.2"), "the section passes to the remaining direction and is released");
	ok &= expect(occupySingleTrackForRoute(0) == 6 && occupySingleTrackForRoute(1) == 0,
		"now the forward route is held back and the reversed route is not");
	BlocksOccupied.clear();

	// A forward train enters while the reversed holder is still inside: the holder keeps the section.
	place(0, 0, 3000.0, true); // forward, in lock.1
	BlocksConnected.clear();
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == -1 && BlocksConnected.empty(),
		"the reversed holder keeps the section when a forward train is inside as well");
	place(0, 0, 3000.0, false);

	// Nobody is left: the section is free and is released once.
	place(1, 1, 3000.0, false);
	BlocksConnected.clear();
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 0 && has(BlocksConnected, "lock.1"), "a section without trains is free and released");
	BlocksConnected.clear();
	updateSingleTrackLocks(1);
	ok &= expect(BlocksConnected.empty() && occupySingleTrackForRoute(0) == 0
			&& occupySingleTrackForRoute(1) == 0,
		"a free section is not released again and holds nobody back");

	// A train in the protected section before the first plain section already holds the section.
	place(0, 0, 100.0, true);
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 1, "a train in the protected section before the section already holds it");
	place(0, 0, 100.0, false);
	updateSingleTrackLocks(1);

	// Reserve before the last braking opportunity; retain the owner through a temporary stop.
	place(0, 0, -50.0, true);
	place(1, 1, -50.0, true);
	BlocksConnected.clear();
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 1 && has(BlocksConnected, "lock.0") && has(BlocksConnected, "lock.5"),
		"two running trains about to enter reserve forward and release the zone sections");
	place(0, 0, -200.0, true);
	BlocksConnected.clear();
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 1 && BlocksConnected.empty(),
		"a reservation survives its owner slowing or stopping before entry");
	place(0, 0, 50.0, true);
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 1, "the forward train keeps the reservation once inside");

	for (int runningRoute : {0, 1}) {
		resetSingleTrackLocks();
		place(0, runningRoute, -50.0, true);
		place(1, 1 - runningRoute, 0.0, true);
		regional_train[1].CanEnter = false;
		regional_train[1].departure_time = 2.0;
		regional_train[1].Start_Node_X = 0.0;
		regional_train[1].train_length = 70.0;
		updateSingleTrackLocks(1);
		BlocksOccupied.clear();
		ok &= expect(singleTrackHeld[0] == 1 && occupySingleTrackForRoute(1) == 6,
			"a running train and an opposing entry due next step reserve forward and hold the reversed route");
		BlocksOccupied.clear();
		regional_train[1].departure_time = 3.0;
		updateSingleTrackLocks(1);
		ok &= expect(singleTrackHeld[0] == (runningRoute == 0 ? 1 : -1), "retargeting a waiting entry cancels it but does not cancel the running request");
		place(1, 1 - runningRoute, -50.0, false);
		resetSingleTrackLocks();
		updateSingleTrackLocks(1);
		ok &= expect(singleTrackHeld[0] == (runningRoute == 0 ? 1 : -1), "one approaching direction reserves before its braking opportunity");
	}

	// Waiting entries need no previous trajectory sample, even at the first step.
	for (int k = 0; k < 2; ++k) {
		place(k, k, 0.0, true);
		regional_train[k].CanEnter = false;
		regional_train[k].departure_time = 1.0;
		regional_train[k].Start_Node_X = 0.0;
		regional_train[k].instant_spatial_position.clear();
	}
	updateSingleTrackLocks(0);
	ok &= expect(singleTrackHeld[0] == 1, "opposing entries due at the first step reserve without trajectory samples");

	// An existing reversed holder is not displaced by two opposing requests.
	const int savedApproachRegions = numRegions;
	regional_train.resize(3);
	numRegions = 3;
	place(0, 1, 50.0, true);
	place(1, 0, -50.0, true);
	place(2, 1, -50.0, true);
	singleTrackHeld[0] = -1;
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == -1, "a reversed holder inside keeps the zone while both directions approach");
	regional_train.resize(2);
	numRegions = savedApproachRegions;
	place(0, 0, -50.0, false);
	place(1, 1, -50.0, false);
	updateSingleTrackLocks(1);

	// A held section gives the routes against the holder an End of Authority in front of the zone at level 3 and 4,
	// once per step, and none for the holder's direction.
	singleTrackLimits.clear();
	singleTrackLimits.emplace_back("lock.2", "lock.3", "lock.1", "lock.4");
	resetSingleTrackLocks();
	place(0, 0, 1950.0, true);
	place(1, 1, 1950.0, true);
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 1, "opposing running trains reserve at an interior zone boundary");
	for (double previous : {1950.0, 2000.0}) {
		regional_train[0].instant_spatial_position[0] = previous;
		updateSingleTrackLocks(1);
		ok &= expect(singleTrackHeld[0] == 1, "an existing reservation does not depend on the last displacement");
	}
	// A follower already committed to this direction must survive the leader's clearance while it waits outside.
	resetSingleTrackLocks();
	regional_train.resize(3);
	numRegions = 3;
	place(0, 0, 3500.0, true);
	place(1, 0, 1950.0, true);
	place(2, 1, 1000.0, false);
	updateSingleTrackLocks(1);
	place(0, 0, 3500.0, false);
	regional_train[1].instant_train_speed = {0.0, 0.0};
	place(2, 1, 1950.0, true);
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 1, "a committed follower keeps the direction after its leader clears, even while stopped");
	regional_train[1].OutOfSimulation = true;
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == -1, "termination cancels the last pending owner and gives the opponent its turn");
	regional_train[2].departure_time = 10.0;
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 0, "changing the owner's entry identity cancels its reservation");
	place(2, 1, 1950.0, true);
	updateSingleTrackLocks(1);
	regional_train[2].indexOfRoute = -1;
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 0, "a removed route cancels its pending owner");
	place(2, 1, 1950.0, true);
	updateSingleTrackLocks(1);
	regional_train.resize(2);
	numRegions = 2;
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 0, "removing a pending train releases the zone");
	place(0, 0, 1950.0, true);
	updateSingleTrackLocks(1);
	train_route[0].x_of_end_node += 1.0;
	regional_train[0].instant_spatial_position[1] = 1000.0;
	regional_train[0].instant_train_speed[1] = 0.0;
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 0, "a changed destination cancels a pending reservation");
	train_route[0].x_of_end_node -= 1.0;
	place(0, 0, 1999.5, true);
	regional_train[0].instant_train_speed = {0.0, 0.0};
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 1, "a stationary train can consume its braking opportunity by accelerating next step");
	place(0, 0, 10100.0, true);
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 0, "passage of the reserved interval releases its owner");
	struct FailedBrakingLookup : Regional {
		double result = -1, requestedSpeed = 0;
		double BrakDist_Block(double speed, double, double, Section*, int) override {
			requestedSpeed = speed;
			return result;
		}
	} probe;
	static_cast<Train&>(probe) = regional_train[0];
	for (double result : {-1.0, std::numeric_limits<double>::quiet_NaN(), -10.0}) {
		probe.result = result;
		ok &= expect(probe.needsSingleTrackReservation(1000.0, 0.0, 2000.0,
						 train_route[0].sequence_of_block_sections.data(), 6),
			"failed, non-finite or before-origin braking points request conservatively");
		ok &= expect(probe.requestedSpeed > 0, "reservation braking lookup includes possible next-step acceleration from rest");
	}
	const auto savedAuthorities = ETCS_MA;
	std::vector<std::vector<int>> savedLevels;
	for (const Route& route : train_route) {
		savedLevels.emplace_back();
		for (const Section& section : route.sequence_of_block_sections)
			savedLevels.back().push_back(section.SignallingLevel);
	}
	auto setLevel = [](int level) {
		for (Route& route : train_route)
			for (Section& section : route.sequence_of_block_sections)
				section.SignallingLevel = level;
	};
	setLevel(3);
	place(0, 0, 3500.0, true); // forward, in lock.1
	place(1, 1, 3000.0, false);
	ETCS_MA.clear();
	updateSingleTrackLocks(1);
	Apply_Single_Track_Authorities_Mixed_Signalling();
	Apply_Single_Track_Authorities_Mixed_Signalling();
	ok &= expect(ETCS_MA.size() == 1 && ETCS_MA.front().BSID == "lock.5" && ETCS_MA.front().ReversedDirection
			&& ETCS_MA.front().type == "SignalFailure" && ETCS_MA.front().TrainInfo.trainDescription == "single_track:lock.2",
		"a forward holder gives the reversed route one authority in front of the zone");
	setLevel(2);
	ETCS_MA.clear();
	Apply_Single_Track_Authorities_Mixed_Signalling();
	ok &= expect(ETCS_MA.size() == 1 && ETCS_MA.front().ReversedDirection,
		"fixed-block trains also get a direction-filtered entry authority");
	setLevel(kSignallingLevelUnset);
	ETCS_MA.clear();
	Apply_Single_Track_Authorities_Mixed_Signalling();
	ok &= expect(ETCS_MA.empty(), "a section without signalling gets no single-track authority");
	setLevel(4);
	place(0, 0, 3500.0, false);
	place(1, 1, 3500.0, true); // reversed, in lock.4
	updateSingleTrackLocks(1);
	ETCS_MA.clear();
	Apply_Single_Track_Authorities_Mixed_Signalling();
	ok &= expect(ETCS_MA.size() == 1 && ETCS_MA.front().BSID == "lock.0" && !ETCS_MA.front().ReversedDirection,
		"a reversed holder gives the forward route an authority in front of the zone");
	place(1, 1, 3500.0, false);
	updateSingleTrackLocks(1);
	ETCS_MA.clear();
	Apply_Single_Track_Authorities_Mixed_Signalling();
	ok &= expect(singleTrackHeld[0] == 0 && ETCS_MA.empty(), "a free section gives no authority");
	ETCS_MA = savedAuthorities;
	for (std::size_t r = 0; r < train_route.size(); ++r)
		for (std::size_t b = 0; b < savedLevels[r].size(); ++b)
			train_route[r].sequence_of_block_sections[b].SignallingLevel = savedLevels[r][b];

	// A zone with gaps keeps one interval per run of neighbouring sections, and a train in a gap is not inside.
	singleTrackLimits.clear();
	singleTrackLimits.emplace_back("lock.2", "lock.3", "lock.0", "lock.5");
	resetSingleTrackLocks();
	ok &= expect(singleTrackZone(0, 0).intervals.size() == 3 && singleTrackZone(0, 0).sectionIDs.size() == 4,
		"separate zone sections give separate intervals");
	place(0, 0, 3000.0, true); // forward, in lock.1, between the protected and the plain sections
	regional_train[0].instant_train_speed = {0.0, 0.0};
	place(1, 1, 3000.0, false);
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld.size() == 1 && singleTrackHeld[0] == 0, "a train between zone sections does not hold the zone");
	place(0, 0, 5000.0, true); // forward, in lock.2
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 1, "a train in a zone section holds the zone");
	place(0, 0, 50.0, true); // forward, in lock.0 with the rear before the start of the route
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 1, "a train whose rear is before the start of the route is inside the first section");
	place(0, 0, 5000.0, false);
	updateSingleTrackLocks(1);

	// At level 5 the next disconnected interval's approach must already be committed when the tail clears
	// the first interval. Drive the real movement and signalling routines across that clearance and the gap.
	setLevel(5);
	for (Route& route : train_route)
		route.x_of_end_node = route.sequence_of_block_sections.back().end_node.X;
	resetSingleTrackLocks();
	place(0, 1, 2000.0, true);
	place(1, 0, 0.0, true);
	regional_train[1].CanEnter = false;
	constexpr int steps = 65;
	for (Regional& train : regional_train) {
		const double head = train.instant_spatial_position[1];
		const double speed = train.CanEnter ? train.instant_train_speed[1] : 0.0;
		train.setTrainVectorSizesFromInput(steps);
		train.instant_spatial_position[0] = head - speed * timestep;
		train.instant_spatial_position[1] = head;
		train.instant_train_speed[0] = train.instant_train_speed[1] = speed;
	}
	const int savedRouteCount = N_Routes;
	N_Routes = static_cast<int>(train_route.size());
	ETCS_MA.clear();
	auto signalStep = [](int step) {
		BlocksOccupied.clear();
		Occupy_Block_Sections_Of_Route(step);
		ETCS_MA.clear();
		Apply_Single_Track_Authorities_Mixed_Signalling();
		releaseMixedSignallingSystem();
		activateMixedSignallingSystem();
	};
	signalStep(1);
	ok &= expect(singleTrackReservations[0].size() == 2
			&& singleTrackReservations[0][1].interval == std::make_pair(4000.0, 8000.0),
		"the moving holder commits the next interval before clearing the first, but not the distant third interval");
	bool retainedAcrossGap = true, safeApproach = true, waiterHeld = true, crossedGap = false;
	double largestSpeedDrop = 0.0;
	Regional& holder = regional_train[0];
	for (int step = 2; step < steps; ++step) {
		const double previousHead = holder.instant_spatial_position[step - 1];
		const double previousSpeed = holder.instant_train_speed[step - 1];
		const double deceleration = holder.max_train_decelaration
			+ holder.total_train_resistances(previousSpeed, 0.0, 0.0) / (holder.total_train_mass * holder.massFactor);
		holder.trajectoryComputationIncludingMovingBlock(step, signalCode1, signalCode2, signalCode3);
		largestSpeedDrop = std::max(largestSpeedDrop, previousSpeed - holder.instant_train_speed[step]);
		safeApproach &= holder.instant_train_speed[step - 1] == previousSpeed
			&& previousSpeed - holder.instant_train_speed[step] <= deceleration * timestep + 1e-9
			&& holder.instant_spatial_position[step] >= previousHead;
		regional_train[1].trajectoryComputationIncludingMovingBlock(step, signalCode1, signalCode2, signalCode3);
		waiterHeld &= !regional_train[1].CanEnter;
		signalStep(step);
		const double head = holder.instant_spatial_position[step];
		if (head - holder.train_length >= 2000.0 && head < 4000.0) {
			crossedGap = true;
			retainedAcrossGap &= singleTrackHeld[0] == -1
				&& train_route[1].sequence_of_block_sections[1].arcs_in_signalling_block_section[0].signalSpeedLimit >= holder.instant_train_speed[step];
		}
	}
	ok &= expect(crossedGap && retainedAcrossGap && waiterHeld,
		"the moving reversed holder retains direction and its approach speed limit across the gap while the forward opponent waits");
	ok &= expect(safeApproach, "gap handover neither rewrites the previous speed nor exceeds physical deceleration; largest speed drop=" + std::to_string(largestSpeedDrop));
	ok &= expect(holder.instant_spatial_position.back() - holder.train_length >= 4000.0,
		"the moving holder reaches the next disconnected interval without an instantaneous stop");
	N_Routes = savedRouteCount;
	ETCS_MA = savedAuthorities;

	// At level 2 the same next interval is still beyond braking lookahead, so clearing the first releases it.
	setLevel(2);
	resetSingleTrackLocks();
	place(0, 1, 2000.0, true);
	place(1, 0, 0.0, true);
	regional_train[1].CanEnter = false;
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == -1 && singleTrackReservations[0].size() == 1,
		"a distant next stretch outside braking lookahead is not committed");
	place(0, 1, 2070.0, true);
	updateSingleTrackLocks(1);
	ok &= expect(singleTrackHeld[0] == 1,
		"tail clearance releases the direction to a waiting opponent when the distant next stretch is uncommitted");

	// New routes are not read through the zones of the old ones.
	train_route.resize(1);
	ok &= expect(singleTrackZone(0, 1).intervals.empty() && singleTrackZone(0, 0).intervals.size() == 3,
		"the zones follow the current routes");
	resetSingleTrackLocks();

	timestep = savedTimestep;
	S_delay = savedDelay;
	numRegions = savedRegions;
	regional_train.swap(savedTrains);
	singleTrackLimits = savedLimits;
	singleTrackHeld = savedHeld;
	BlocksOccupied = savedOccupied;
	BlocksConnected = savedConnected;
	resetSingleTrackLocks();
	train_route.swap(savedRoutes);
	return ok;
}

int main() {
	bool ok = generatorTests();
	SceneModel scene = completeScene();
	// Exercise native route construction, stop resolution and chart coordinate
	// extraction together. The second route traverses the same track backwards.
	SceneModel diagramScene = completeScene();
	for (auto& node : diagramScene.nodes) {
		node.xKm += 42.0;
		node.yKm = 900.0 + node.xKm; // map geometry is not the route X basis
	}
	SceneRoute reverseRoute = diagramScene.routes.front();
	reverseRoute.id = "route.reverse";
	reverseRoute.blocks = {"block.2", "block.1", "block.0"};
	reverseRoute.reversed = true;
	diagramScene.routes.push_back(reverseRoute);
	SceneService reverseService = diagramScene.services.front();
	reverseService.id = "service.reverse";
	reverseService.route = reverseRoute.id;
	reverseService.stops = {reverseService.stops[2], reverseService.stops[1], reverseService.stops[0]};
	diagramScene.services.push_back(reverseService);
	const auto diagramInfra = buildInfrastructureAndSignallingFromScene(diagramScene);
	const auto diagramOps = hasErrors(diagramInfra) ? diagramInfra
													: buildOperationsFromScene(diagramScene, "scenario.base");
	ok &= expect(!hasErrors(diagramInfra) && !hasErrors(diagramOps),
		"forward and reverse diagram fixture builds through native paths");
	if (!hasErrors(diagramInfra) && !hasErrors(diagramOps) && numRegions > 1) {
		const RouteDiagramPath reference = routeDiagramPath(train_route[regional_train[0].indexOfRoute], &diagramScene);
		const auto identity = buildRouteDiagramProjection(reference, reference);
		const Train& forward = regional_train[0];
		const Train& reverse = regional_train[numRegions - 1];
		const RouteDiagramPath reversePath = routeDiagramPath(train_route[reverse.indexOfRoute], &diagramScene);
		const auto reverseProjection = buildRouteDiagramProjection(reversePath, reference);
		const auto forwardStop = routeDiagramStopPosition(forward, 1, reference, identity);
		const auto backwardStop = routeDiagramStopPosition(reverse, 1, reversePath, reverseProjection);
		ok &= expect(forwardStop && backwardStop && std::fabs(*forwardStop - 43.0) < 1e-6
				&& std::fabs(*backwardStop - 43.0) < 1e-6,
			"native forward/reverse stop nodes share reference coordinates");
		const double forwardSample = forward.Stations[1].X * 1000.0;
		const double reverseSample = reverse.Stations[1].X * 1000.0;
		const auto forwardAtSample = identity.map(routeDiagramTrajectoryKm(forwardSample));
		const auto reverseAtSample = reverseProjection.map(routeDiagramTrajectoryKm(reverseSample));
		ok &= expect(forwardAtSample && reverseAtSample && std::fabs(*forwardAtSample - *forwardStop) < 1e-6
				&& std::fabs(*reverseAtSample - *backwardStop) < 1e-6,
			"runtime sample metres project without a second direction reversal");
	}
	scene.services[0].through = true; // Nonempty stops take precedence over this historical flag.
	initial_variables.InputMainFolder = "/__egtrain_nonexistent_native_input__";
	InputMainFolder = initial_variables.InputMainFolder;
	auto infrastructureDiagnostics = buildInfrastructureAndSignallingFromScene(scene);
	ok &= expect(!hasErrors(infrastructureDiagnostics), "M2 infrastructure builder accepts the complete fixture");
	if (hasErrors(infrastructureDiagnostics))
		return 1;

	const auto diagnostics = buildOperationsFromScene(scene, "scenario.selected");
	ok &= expect(!hasErrors(diagnostics), "M3 operations builder accepts the complete fixture");
	ok &= expect(regional_train[0].numStations == 3, "historical through flag does not discard scheduled stops");
	SceneModel tooManyStops = completeScene();
	while (tooManyStops.services[0].stops.size() <= static_cast<std::size_t>(Train::kMaxTimetableStations))
		tooManyStops.services[0].stops.push_back(tooManyStops.services[0].stops.back());
	const auto tooManyStopsDiagnostics = buildOperationsFromScene(tooManyStops, "scenario.base");
	ok &= expect(hasCode(tooManyStopsDiagnostics, "scene.native.capacity.stops"),
		"native operations rejects one stop above the timetable limit");
	SceneModel tooManyTrains = completeScene();
	tooManyTrains.services[0].hasRepeatCount = true;
	tooManyTrains.services[0].repeatCount = Max_N_Reg + 1;
	const auto tooManyTrainsDiagnostics = buildOperationsFromScene(tooManyTrains, "scenario.base");
	ok &= expect(hasCode(tooManyTrainsDiagnostics, "scene.native.capacity.trains"),
		"native operations rejects one expanded train above the limit");
	ok &= expect(initial_variables.InputMainFolder == "/__egtrain_nonexistent_native_input__"
			&& InputMainFolder == initial_variables.InputMainFolder,
		"native builders do not access or rewrite the legacy input folder");
	ok &= expect(numRegions == 3 && N_Train == 3 && N_TrainD == 0, "repeat expansion populates train counts");
	ok &= expect(regional_train[0].trainDescription == "service.native-1"
			&& regional_train[1].trainDescription == "service.native-2"
			&& regional_train[2].trainDescription == "service.native-3",
		"occurrence IDs are canonical and stable");
	ok &= expect(regional_train[0].type == "service.native", "occurrences share the canonical service type");
	ok &= expect(regional_train[0].operatingCode == "9707-1"
			&& regional_train[1].operatingCode == "9707-2"
			&& regional_train[2].operatingCode == "9707-3",
		"repeated services without a step expose readable operating codes");
	ok &= expect(regional_train[0].number_of_wagons == 3.0
			&& regional_train[0].total_train_mass == 290.0
			&& regional_train[0].velocityIntervals == 2,
		"ordered multi-unit physical and traction data is aggregated (wagons="
			+ std::to_string(regional_train[0].number_of_wagons) + ", mass="
			+ std::to_string(regional_train[0].total_train_mass) + ", bands="
			+ std::to_string(regional_train[0].velocityIntervals) + ")");
	ok &= expect(regional_train[0].MAX_OnBoard_Passengers == 1200
			&& regional_train[0].Current_OnBoard_Passengers == 0,
		"built train capacity follows the aggregated wagon count (capacity="
			+ std::to_string(regional_train[0].MAX_OnBoard_Passengers) + ")");
	ok &= expect(regional_train[0].scheduled_departure_time == 100.0
			&& regional_train[1].scheduled_departure_time == 130.0
			&& regional_train[2].scheduled_departure_time == 160.0,
		"canonical entry times remain scheduled times");
	ok &= expect(regional_train[0].ScheduledArrivals[0] == -1.0
			&& regional_train[0].ScheduledArrivals[1] == 120.0
			&& regional_train[0].ScheduledDepartures[2] == -1.0,
		"optional timetable fields retain runtime -1 sentinels");
	ok &= expect(regional_train[0].stationIsOnRoute(0)
			&& regional_train[0].stationRoutePositionMeters(0) == 0.0,
		"planned references retain a valid route-origin station at kilometre zero");
	Node offRouteStation;
	offRouteStation.station = true;
	offRouteStation.stationName = "Outside";
	Train routeMembershipProbe;
	routeMembershipProbe.Stations.push_back(offRouteStation);
	routeMembershipProbe.numStations = 1;
	routeMembershipProbe.indexOfRoute = regional_train[0].indexOfRoute;
	ok &= expect(!routeMembershipProbe.stationIsOnRoute(0)
			&& routeMembershipProbe.stationRoutePositionMeters(0) < 0.0,
		"route-external static timetable stops remain inert in planned route results");
	ok &= expect(regional_train[0].instant_train_tractive_effort.size()
			== regional_train[0].instant_spatial_position.size(),
		"tractive-effort samples are allocated with the trajectory");
	ok &= expect(regional_train[1].ScheduledDepartures[0] == 145.0
			&& regional_train[1].ScheduledDepartures[1] == 160.0
			&& regional_train[1].EntranceDelay == 5.0,
		"selected entrance delays apply to the requested occurrence and stops");
	ok &= expect(regional_train[0].departure_time == 1.0 && regional_train[1].departure_time == 1201.0
			&& regional_train[2].departure_time == 2401.0,
		"hourly departure retiming preserves the legacy algorithm");
	ok &= expect(simulationIncidents.size() == 2
			&& simulationIncidents[0].target == "signal.0"
			&& simulationIncidents[1].target == "service.native",
		"only the selected scenario reaches runtime incidents");
	ok &= expect(simulationIncidents[1].id == "incident.breakdown"
			&& simulationIncidents[1].hasEndSeconds
			&& !simulationIncidents[1].hasOccurrence
			&& !simulationIncidents[1].hasReducedSpeed,
		"legacy breakdown incidents retain full-hold runtime defaults");
	if (!simulationIncidents.empty())
		ok &= expect(simulationIncidents.front().resolvedSectionIDs
				== std::vector<std::string>{"@block.1@"},
			"signal failure resolves its protected section to the exact runtime ID");
	SceneModel directBlockIncident = completeScene();
	directBlockIncident.signals.front().protectedSection.clear();
	directBlockIncident.scenarios[1].incidents[0].target = "block.1";
	const auto directInfrastructure = buildInfrastructureAndSignallingFromScene(directBlockIncident);
	const auto directOperations = buildOperationsFromScene(directBlockIncident, "scenario.selected");
	ok &= expect(!hasErrors(directInfrastructure) && !hasErrors(directOperations)
			&& !simulationIncidents.empty()
			&& simulationIncidents.front().resolvedSectionIDs == std::vector<std::string>{"@block.1@"},
		"direct base-block signal failures remain compatible without signal binding");
	SceneModel unboundSignal = completeScene();
	unboundSignal.signals.front().protectedSection.clear();
	const auto unboundInfrastructure = buildInfrastructureAndSignallingFromScene(unboundSignal);
	const auto unboundOperations = buildOperationsFromScene(unboundSignal, "scenario.selected");
	ok &= expect(!hasErrors(unboundInfrastructure) && hasErrors(unboundOperations),
		"targeting an unbound signal fails operations staging instead of guessing");
	SceneModel ambiguousSignalTarget = completeScene();
	ambiguousSignalTarget.blocks.front().id = "signal.0";
	ambiguousSignalTarget.routes.front().blocks.front() = "signal.0";
	const auto ambiguousInfrastructure = buildInfrastructureAndSignallingFromScene(ambiguousSignalTarget);
	const auto ambiguousOperations = buildOperationsFromScene(ambiguousSignalTarget, "scenario.selected");
	ok &= expect(!hasErrors(ambiguousInfrastructure) && hasErrors(ambiguousOperations),
		"a signal failure target matching both a signal and block is rejected as ambiguous");
	SceneModel occurrenceSpecific = completeScene();
	SceneIncident& occurrenceBreakdown = occurrenceSpecific.scenarios[1].incidents[1];
	occurrenceBreakdown.hasOccurrence = true;
	occurrenceBreakdown.occurrence = 2;
	occurrenceBreakdown.hasReducedSpeed = true;
	occurrenceBreakdown.reducedSpeedKmh = 40.0;
	occurrenceBreakdown.hasEndSeconds = false;
	occurrenceBreakdown.endSeconds = 0.0;
	occurrenceBreakdown.terminateAtDestination = true;
	const auto occurrenceInfrastructure = buildInfrastructureAndSignallingFromScene(occurrenceSpecific);
	const auto occurrenceOperations = buildOperationsFromScene(occurrenceSpecific, "scenario.selected");
	ok &= expect(!hasErrors(occurrenceInfrastructure) && !hasErrors(occurrenceOperations)
			&& simulationIncidents.size() == 2
			&& simulationIncidents[1].hasOccurrence && simulationIncidents[1].occurrence == 2
			&& simulationIncidents[1].hasReducedSpeed
			&& std::fabs(simulationIncidents[1].reducedSpeedKmh - 40.0) < 1e-9
			&& Incident_Holds_Train("service.native-2", 50) == false
			&& regional_train[1].destinationTerminationRequested
			&& !regional_train[0].destinationTerminationRequested
			&& !regional_train[2].destinationTerminationRequested,
		"reduced breakdown staging targets one occurrence and continues without recovery");
	regional_train[1].directIncidentIds.clear();
	ok &= expect(std::fabs(regional_train[1].effectiveIncidentSpeedLimit(10.0, 50) - 10.0) < 1e-9
			&& regional_train[1].directIncidentIds.empty(),
		"a nonbinding breakdown cap is not recorded as direct evidence");
	ok &= expect(std::fabs(regional_train[1].effectiveIncidentSpeedLimit(25.0, 50) - 40.0 / 3.6) < 1e-9
			&& regional_train[1].directIncidentIds == std::vector<std::string>{"incident.breakdown"}
			&& std::fabs(regional_train[0].effectiveIncidentSpeedLimit(25.0, 50) - 25.0) < 1e-9,
		"a binding cap records direct evidence only on the targeted occurrence");
	SimulationIncident overlappingHold = simulationIncidents[1];
	overlappingHold.id = "incident.hold";
	overlappingHold.hasReducedSpeed = false;
	overlappingHold.reducedSpeedKmh = 0.0;
	overlappingHold.hasEndSeconds = true;
	overlappingHold.startSeconds = 45.0;
	overlappingHold.endSeconds = 55.0;
	simulationIncidents.push_back(overlappingHold);
	ok &= expect(Incident_Holds_Train("service.native-2", 50)
			&& Active_Train_Breakdown("service.native-2", 50)->id == "incident.hold",
		"an overlapping full hold dominates an earlier reduced-speed incident");
	SimulationIncident strictCap = simulationIncidents[1];
	strictCap.id = "incident.strict-cap";
	strictCap.reducedSpeedKmh = 30.0;
	simulationIncidents.push_back(strictCap);
	regional_train[1].directIncidentIds.clear();
	ok &= expect(std::fabs(regional_train[1].effectiveIncidentSpeedLimit(25.0, 60) - 30.0 / 3.6) < 1e-9
			&& regional_train[1].directIncidentIds == std::vector<std::string>{"incident.strict-cap"},
		"the strictest concurrent cap supplies the governing direct evidence");
	ok &= expect(AllStationPlatforms.size() == 3, "native operations reuses the M2 platform list");
	if (!AllStationPlatforms.empty())
		ok &= expect(AllStationPlatforms.front().List_Trains_Stopping_At_Platform.front() == "service.native-1",
			"platform stopping lists use stable occurrence descriptions");
	SceneModel geometry = completeScene();
	geometry.stations[0].platforms[0].hasLength = true;
	geometry.stations[0].platforms[0].lengthM = 140.0;
	geometry.stations[0].platforms[0].hasWidth = true;
	geometry.stations[0].platforms[0].widthM = 3.0;
	const auto geometryInfrastructure = buildInfrastructureAndSignallingFromScene(geometry);
	const auto geometryOperations = buildOperationsFromScene(geometry, "scenario.selected");
	ok &= expect(!hasErrors(geometryInfrastructure) && !hasErrors(geometryOperations)
			&& AllStationPlatforms.size() == 3,
		"explicit platform geometry reaches native operations");
	if (AllStationPlatforms.size() == 3) {
		auto platform = AllStationPlatforms.begin();
		ok &= expect(platform->length == 140.0 && platform->width == 3.0
				&& platform->Max_Passenger_Volume == static_cast<int>((140.0 * 3.0) / (3.14159 * std::pow(0.8, 2)) * 0.8),
			"native platform capacity keeps the existing area formula");
		++platform;
		ok &= expect(platform != AllStationPlatforms.end() && platform->length == 100.0
				&& platform->width == 2.5,
			"absent platform geometry keeps effective defaults");
	}
	const std::string geometryPreviousName = initial_variables.name;
	const int geometryPreviousRegions = numRegions;
	const double previousPlatformLength = AllStationPlatforms.empty()
		? -1.0
		: AllStationPlatforms.front().length;
	SceneModel invalidGeometry = completeScene();
	invalidGeometry.stations[0].platforms[0].hasLength = true;
	invalidGeometry.stations[0].platforms[0].lengthM = std::numeric_limits<double>::max();
	const auto invalidGeometryDiagnostics = buildOperationsFromScene(invalidGeometry, "scenario.selected");
	ok &= expect(hasErrors(invalidGeometryDiagnostics)
			&& hasCode(invalidGeometryDiagnostics, "scene.native.platform.capacity")
			&& numRegions == geometryPreviousRegions
			&& initial_variables.name == geometryPreviousName
			&& (!AllStationPlatforms.empty() && AllStationPlatforms.front().length == previousPlatformLength),
		"unsafe platform capacity is rejected before native state publication");
	ok &= expect(AllDailyPassengers.size() == 1, "passenger journeys are built without filesystem input");
	if (!AllDailyPassengers.empty() && !AllDailyPassengers.front().Journeys.empty()) {
		const Journey& journey = AllDailyPassengers.front().Journeys.front();
		ok &= expect(journey.N_Trips == 2 && std::isfinite(journey.Actual_Planned_Departure_Time)
				&& journey.Actual_Planned_Departure_Time >= 100.0
				&& journey.Actual_Planned_Departure_Time <= 110.0,
			"passenger windows are sampled in memory");
		if (!journey.Trips.empty()) {
			const Trip& first = journey.Trips.front();
			const Trip& last = journey.Trips.back();
			ok &= expect(first.TrainServiceDescription == "service.native-2"
					&& first.Dep_Station_Platform_ID == "platform.0"
					&& last.Arr_Station_Platform_ID == "platform.2",
				"passenger legs map to occurrence stop platforms");
			ok &= expect(first.Planned_Departure_Time >= 100 && first.Planned_Arrival_Time == -9999
					&& last.Planned_Departure_Time == -9999 && last.Planned_Arrival_Time >= 130,
				"multi-leg planned times retain the existing endpoint-only semantics");
		}
		auto unrouted = AllDailyPassengers.front().Journeys.begin();
		++unrouted;
		ok &= expect(unrouted != AllDailyPassengers.front().Journeys.end() && unrouted->N_Trips == 0,
			"legless canonical journeys remain present");
	}
	auto routeChoicePassengers = AllDailyPassengers;
	Passenger& active = routeChoicePassengers.front();
	active.IsIntheNetwork = true;
	active.current_JourneyID = active.Journeys.front().ID;
	Passenger inactive = active;
	inactive.ID = "inactive.passenger";
	inactive.IsIntheNetwork = false;
	routeChoicePassengers.push_back(inactive);
	Passenger activeLegless = active;
	activeLegless.ID = "legless.passenger";
	const auto leglessJourney = std::next(activeLegless.Journeys.begin());
	activeLegless.current_JourneyID = leglessJourney->ID;
	const int leglessDepartureTime = static_cast<int>(leglessJourney->Actual_Planned_Departure_Time);
	routeChoicePassengers.push_back(activeLegless);
	const nlohmann::json routeChoice = routeChoicePayload(routeChoicePassengers, 17);
	ok &= expect(routeChoice == routeChoicePayload(routeChoicePassengers, 17)
			&& routeChoice["time"] == 17 && routeChoice["passengers"].size() == 2
			&& routeChoice["passengers"]["passenger.1--1.0"]["origin"] == "Zero"
			&& routeChoice["passengers"]["passenger.1--1.0"]["destination"] == "Two"
			&& routeChoice["passengers"]["legless.passenger--1.0"]["origin"] == "Zero"
			&& routeChoice["passengers"]["legless.passenger--1.0"]["destination"] == "Two"
			&& routeChoice["passengers"]["legless.passenger--1.0"]["departure_time"] == leglessDepartureTime
			&& routeChoicePayload({}, 17)["passengers"].empty(),
		"route-choice sharing deterministically includes routed and legless active journeys");
	ok &= expect(initial_variables.name == "native-operations-fixture"
			&& initial_variables.startingSimulationTime == 23400
			&& initial_variables.times == 90.0
			&& initial_variables.bufferTime == 7
			&& initial_variables.recoveryTimePercentage == 12
			&& bufferTime == 7.0
			&& recoveryTimePercentage == 12.0
			&& initial_variables.num_OrderLists == 0,
		"canonical simulation settings are committed");

	SceneModel stepped = completeScene();
	stepped.services[0].operatingCode = "1723";
	stepped.services[0].hasRepeatCount = true;
	stepped.services[0].repeatCount = 4;
	stepped.services[0].hasOperatingCodeStep = true;
	stepped.services[0].operatingCodeStep = 2;
	const auto steppedInfrastructure = buildInfrastructureAndSignallingFromScene(stepped);
	const auto steppedOperations = buildOperationsFromScene(stepped, "scenario.selected");
	ok &= expect(!hasErrors(steppedInfrastructure) && !hasErrors(steppedOperations)
			&& numRegions == 4
			&& regional_train[0].operatingCode == "1723"
			&& regional_train[1].operatingCode == "1725"
			&& regional_train[3].operatingCode == "1729",
		"explicit repeat count and stepped operating codes reach each occurrence");

	SceneModel tuned = completeScene();
	tuned.services[0].performancePercent = 50.0;
	tuned.services[0].hasMaximumSpeed = true;
	tuned.services[0].maximumSpeedKmh = 20.0;
	SceneService sharedService = tuned.services[0];
	sharedService.id = "service.other";
	sharedService.operatingCode = "other";
	sharedService.hasRepeat = false;
	sharedService.hasRepeatCount = false;
	sharedService.hasOperatingCodeStep = false;
	sharedService.performancePercent = 100.0;
	sharedService.hasMaximumSpeed = false;
	sharedService.hasEntryTime = true;
	sharedService.entryTimeSeconds = 200.0;
	tuned.services.push_back(sharedService);
	const auto tunedInfrastructure = buildInfrastructureAndSignallingFromScene(tuned);
	const auto tunedOperations = buildOperationsFromScene(tuned, "scenario.selected");
	ok &= expect(!hasErrors(tunedInfrastructure) && !hasErrors(tunedOperations)
			&& numRegions == 4
			&& std::fabs(regional_train[0].compositionMaximumSpeedMs - 25.0) < 1e-9
			&& std::fabs(regional_train[0].max_train_speed - (20.0 / 3.6 * 0.5)) < 1e-9
			&& std::fabs(regional_train[3].max_train_speed - 25.0) < 1e-9,
		"service cap and performance apply in precedence order per train");
	ok &= expect(tuned.trainUnits[0].physical.max_speed_ms == 30.0
			&& tuned.trainUnits[1].physical.max_speed_ms == 25.0
			&& tuned.compositions[0].units == std::vector<std::string>{"unit.1", "unit.2"},
		"services sharing a composition do not mutate source rolling-stock data");
	{
		const double previousPerformance = regional_train[0].servicePerformancePercent;
		regional_train[0].servicePerformancePercent = 100.0;
		const double fullForce = regional_train[0].tractiveEffort(5.0);
		regional_train[0].servicePerformancePercent = 50.0;
		const double reducedForce = regional_train[0].tractiveEffort(5.0);
		regional_train[0].servicePerformancePercent = previousPerformance;
		ok &= expect(fullForce > 0.0 && std::fabs(reducedForce - fullForce * 0.5) < 1e-9,
			"performance scales tractive effort exactly once");
	}

	SceneModel selectedScene = completeScene();
	selectedScene.scenarios[1].entranceDelays.push_back(
		{"service.native", 1, "station.0", 7.0});
	ScenePassengerJourney excludedJourney = selectedScene.passengers[0].journeys[0];
	excludedJourney.id = "journey.excluded";
	for (ScenePassengerLeg& leg : excludedJourney.legs) {
		leg.id += ".excluded";
		leg.occurrence = 1;
	}
	selectedScene.passengers[0].journeys.push_back(excludedJourney);
	ScenePassengerJourney mixedJourney = selectedScene.passengers[0].journeys[0];
	mixedJourney.id = "journey.mixed";
	mixedJourney.legs[0].id += ".mixed";
	mixedJourney.legs[0].occurrence = 1;
	mixedJourney.legs[1].id += ".mixed";
	mixedJourney.legs[1].occurrence = 2;
	selectedScene.passengers[0].journeys.push_back(mixedJourney);
	const SceneRunSelection onlySecond{{"service.native", 2}};
	const auto selectedInfrastructure = buildInfrastructureAndSignallingFromScene(selectedScene);
	const auto selectedOperations = buildOperationsFromScene(selectedScene, "scenario.selected", onlySecond);
	bool selectedJourneyHasTrips = false;
	bool excludedJourneyOmitted = true;
	bool mixedJourneyOmitted = true;
	if (!AllDailyPassengers.empty()) {
		for (const Journey& journey : AllDailyPassengers.front().Journeys) {
			if (journey.ID == "journey.1")
				selectedJourneyHasTrips = journey.N_Trips == 2;
			if (journey.ID == "journey.excluded")
				excludedJourneyOmitted = false;
			if (journey.ID == "journey.mixed")
				mixedJourneyOmitted = false;
		}
	}
	ok &= expect(!hasErrors(selectedInfrastructure) && !hasErrors(selectedOperations)
			&& numRegions == 1 && regional_train[0].trainDescription == "service.native-2"
			&& regional_train[0].EntranceDelay == 5.0
			&& selectedJourneyHasTrips && excludedJourneyOmitted && mixedJourneyOmitted,
		"occurrence selection omits journeys with excluded passenger legs");
	SceneModel reversePassengerLeg = completeScene();
	reversePassengerLeg.passengers[0].journeys[0].originStationId = "station.2";
	reversePassengerLeg.passengers[0].journeys[0].destinationStationId = "station.1";
	reversePassengerLeg.passengers[0].journeys[0].legs = {
		{"leg.reverse", "station.2", "station.1", "service.native", 2}};
	const int previousReverseRegions = numRegions;
	const std::string previousReverseName = initial_variables.name;
	const std::size_t previousReversePassengerCount = AllDailyPassengers.size();
	const auto reversePassengerInfrastructure = buildInfrastructureAndSignallingFromScene(reversePassengerLeg);
	const auto reversePassengerDiagnostics = buildOperationsFromScene(reversePassengerLeg, "scenario.base", onlySecond);
	ok &= expect(!hasErrors(reversePassengerInfrastructure)
			&& hasCode(reversePassengerDiagnostics, "scene.native.passenger.order")
			&& numRegions == previousReverseRegions
			&& initial_variables.name == previousReverseName
			&& AllDailyPassengers.size() == previousReversePassengerCount,
		"native preflight rejects reverse passenger legs before state publication");
	SceneModel legacyReversePassengerLeg = reversePassengerLeg;
	legacyReversePassengerLeg.importReport.push_back({"legacy_root"});
	const auto legacyReverseInfrastructure = buildInfrastructureAndSignallingFromScene(legacyReversePassengerLeg);
	const auto legacyReverseDiagnostics = buildOperationsFromScene(
		legacyReversePassengerLeg, "scenario.base", onlySecond);
	bool legacyReverseJourneyOmitted = !AllDailyPassengers.empty();
	if (!AllDailyPassengers.empty()) {
		for (const Journey& journey : AllDailyPassengers.front().Journeys)
			legacyReverseJourneyOmitted = legacyReverseJourneyOmitted && journey.ID != "journey.1";
	}
	ok &= expect(!hasErrors(legacyReverseInfrastructure) && !hasErrors(legacyReverseDiagnostics)
			&& hasCodeAndSeverity(legacyReverseDiagnostics, "scene.native.passenger.order", SceneSeverity::Warning)
			&& legacyReverseJourneyOmitted,
		"legacy reverse passenger legs are warned and omitted as a whole journey");
	SceneModel repeatedPassengerStops = completeScene();
	repeatedPassengerStops.services[0].stops = {
		{"station.2", "platform.2", true, true, 100.0, 110.0, 0.0},
		{"station.1", "platform.1", true, true, 120.0, 130.0, 0.0},
		{"station.2", "platform.2", true, true, 140.0, 150.0, 0.0}};
	buildInfrastructureAndSignallingFromScene(repeatedPassengerStops);
	ok &= expect(hasCode(buildOperationsFromScene(repeatedPassengerStops, "scenario.base", onlySecond),
					 "scene.native.ref.stop.order"),
		"repeated calls cannot reuse an earlier route visit");
	// Two distinct visits to the same platform, separated by station One.
	repeatedPassengerStops.stations[0].platforms[0].nodeIds = {"node.3"};
	repeatedPassengerStops.stations[2].platforms[0].nodeIds = {"node.0", "node.2"};
	ScenePassengerJourney& repeatedJourney = repeatedPassengerStops.passengers[0].journeys[0];
	repeatedJourney.originStationId = "station.1";
	repeatedJourney.destinationStationId = "station.2";
	repeatedJourney.legs = {{"leg.repeated", "station.1", "station.2", "service.native", 2}};
	SceneServiceStopPair repeatedPair;
	const auto repeatedInfrastructure = buildInfrastructureAndSignallingFromScene(repeatedPassengerStops);
	const auto repeatedOperations = buildOperationsFromScene(repeatedPassengerStops, "scenario.base", onlySecond);
	bool repeatedJourneyStaged = false;
	if (!AllDailyPassengers.empty()) {
		for (const Journey& journey : AllDailyPassengers.front().Journeys) {
			if (journey.ID == "journey.1")
				repeatedJourneyStaged = journey.N_Trips == 1 && !journey.Trips.empty()
					&& journey.Trips.front().Dep_Station_Platform_ID == "platform.1"
					&& journey.Trips.front().Arr_Station_Platform_ID == "platform.2";
		}
	}
	ok &= expect(!hasErrors(repeatedInfrastructure) && !hasErrors(repeatedOperations)
			&& resolveScenePassengerLegStops(repeatedPassengerStops.services[0], repeatedJourney.legs[0], repeatedPair)
			&& repeatedPair.originIndex == 1 && repeatedPair.destinationIndex == 2
			&& regional_train[0].Stations[0].X == 0.0 && regional_train[0].Stations[2].X == 2.0
			&& repeatedJourneyStaged,
		"native passenger staging follows the repeated-stop ordered pair");
	SceneModel invalidUnselectedPassenger = completeScene();
	invalidUnselectedPassenger.passengers[0].journeys[0].legs[0].serviceId = "service.missing";
	invalidUnselectedPassenger.passengers[0].journeys[0].legs[0].occurrence = 1;
	const int previousRegions = numRegions;
	const std::string previousTrainDescription = regional_train[0].trainDescription;
	const std::string previousOperationsName = initial_variables.name;
	const std::size_t previousPassengerCount = AllDailyPassengers.size();
	const auto invalidUnselectedPassengerDiagnostics = buildOperationsFromScene(
		invalidUnselectedPassenger, "scenario.selected", onlySecond);
	ok &= expect(hasErrors(invalidUnselectedPassengerDiagnostics)
			&& hasCode(invalidUnselectedPassengerDiagnostics, "scene.native.passenger.service")
			&& numRegions == previousRegions
			&& regional_train[0].trainDescription == previousTrainDescription
			&& initial_variables.name == previousOperationsName
			&& AllDailyPassengers.size() == previousPassengerCount,
		"invalid unselected passenger legs are rejected before native state publication");

	SceneModel sparsePattern = completeScene();
	sparsePattern.services[0].hasRepeatCount = true;
	sparsePattern.services[0].repeatCount = 1000000000;
	const SceneRunSelection lateOccurrence{{"service.native", 999999999}};
	const auto sparseInfrastructure = buildInfrastructureAndSignallingFromScene(sparsePattern);
	const auto sparseOperations = buildOperationsFromScene(
		sparsePattern, "scenario.selected", lateOccurrence);
	ok &= expect(!hasErrors(sparseInfrastructure) && !hasErrors(sparseOperations)
			&& numRegions == 1
			&& regional_train[0].trainDescription == "service.native-999999999",
		"a sparse selection does not expand every occurrence in a large pattern");
	const SceneRunSelection invalidSelections{{"service.native", 1000000001}, {"service.missing", 1}};
	const auto invalidSelectionOperations = buildOperationsFromScene(
		sparsePattern, "scenario.selected", invalidSelections);
	ok &= expect(hasCode(invalidSelectionOperations, "scene.native.selection.occurrence")
			&& hasCode(invalidSelectionOperations, "scene.native.selection.service"),
		"invalid sparse selections retain native selection diagnostics");

	SceneModel outOfPattern = completeScene();
	outOfPattern.services[0].hasRepeatCount = true;
	outOfPattern.services[0].repeatCount = 1;
	const auto outOfPatternInfrastructure = buildInfrastructureAndSignallingFromScene(outOfPattern);
	const auto outOfPatternOperations = buildOperationsFromScene(outOfPattern, "scenario.selected");
	const SceneRunSelection firstOccurrence{{"service.native", 1}};
	const auto excludedOutOfPatternOperations = buildOperationsFromScene(
		outOfPattern, "scenario.selected", firstOccurrence);
	ok &= expect(!hasErrors(outOfPatternInfrastructure)
			&& hasCode(outOfPatternOperations, "scene.native.entrance.occurrence")
			&& hasCode(excludedOutOfPatternOperations, "scene.native.entrance.occurrence"),
		"out-of-pattern entrance delays are rejected for all and selected runs");

	auto rejectsEntranceDelay = [&ok, &firstOccurrence](SceneModel invalid, const std::string& code,
									const std::string& message) {
		const auto infrastructure = buildInfrastructureAndSignallingFromScene(invalid);
		const auto operations = buildOperationsFromScene(invalid, "scenario.selected");
		const auto selectedOperations = buildOperationsFromScene(
			invalid, "scenario.selected", firstOccurrence);
		ok &= expect(!hasErrors(infrastructure) && hasCode(operations, code)
				&& hasCode(selectedOperations, code),
			message);
	};
	SceneModel nonFiniteDelay = completeScene();
	nonFiniteDelay.scenarios[1].entranceDelays[0].delaySeconds = std::numeric_limits<double>::quiet_NaN();
	rejectsEntranceDelay(nonFiniteDelay, "scene.native.entrance.value",
		"native staging rejects a non-finite entrance delay");
	SceneModel nonStopDelay = completeScene();
	nonStopDelay.stations.push_back({"station.other", "Other", true, 3.0, {}});
	nonStopDelay.scenarios[1].entranceDelays[0].stationId = "station.other";
	rejectsEntranceDelay(nonStopDelay, "scene.native.entrance.station",
		"native staging rejects an entrance-delay station outside the service stops");
	SceneModel missingDepartureDelay = completeScene();
	missingDepartureDelay.scenarios[1].entranceDelays[0].stationId = "station.2";
	rejectsEntranceDelay(missingDepartureDelay, "scene.native.entrance.timetable",
		"native staging rejects an entrance-delay stop without a planned departure");
	SceneModel conflictingDelay = completeScene();
	conflictingDelay.scenarios[1].entranceDelays.push_back(
		{"service.native", 2, "station.0", 10.0});
	rejectsEntranceDelay(conflictingDelay, "scene.native.entrance.conflict",
		"native staging rejects conflicting delays for one service occurrence");

	TrainEvent finiteLate;
	finiteLate.Time = 2.0;
	TrainEvent finiteEarly;
	finiteEarly.Time = 1.0;
	TrainEvent nanFirst;
	nanFirst.Time = std::numeric_limits<double>::quiet_NaN();
	nanFirst.trainDescription = "nan-first";
	TrainEvent nanLast;
	nanLast.Time = std::numeric_limits<double>::quiet_NaN();
	nanLast.trainDescription = "nan-last";
	std::list<TrainEvent> unorderedEvents = {nanFirst, finiteLate, finiteEarly, nanLast};
	orderListOfTrainEvents(unorderedEvents);
	const auto eventIt = unorderedEvents.begin();
	ok &= expect(unorderedEvents.size() == 4 && eventIt->Time == 1.0
			&& std::next(eventIt)->Time == 2.0
			&& std::isnan(std::next(eventIt, 2)->Time)
			&& std::isnan(std::next(eventIt, 3)->Time)
			&& std::next(eventIt, 2)->trainDescription == "nan-first"
			&& std::next(eventIt, 3)->trainDescription == "nan-last",
		"train event sorting terminates and places non-finite times last");

	const std::string previousDescription = regional_train[0].trainDescription;
	const std::size_t previousIncidentCount = simulationIncidents.size();
	const std::string previousName = initial_variables.name;
	const auto invalidDiagnostics = buildOperationsFromScene(scene, "scenario.missing");
	ok &= expect(hasErrors(invalidDiagnostics), "an invalid selected scenario is rejected");
	ok &= expect(regional_train[0].trainDescription == previousDescription
			&& simulationIncidents.size() == previousIncidentCount
			&& initial_variables.name == previousName,
		"invalid operations build preserves prior runtime state");

	SceneModel reversed = completeScene();
	reversed.routes[0].blocks = {"block.2", "block.1", "block.0"};
	std::reverse(reversed.services[0].stops.begin(), reversed.services[0].stops.end());
	reversed.passengers.clear();
	const auto reversedInfrastructure = buildInfrastructureAndSignallingFromScene(reversed);
	const auto reversedOperations = buildOperationsFromScene(reversed, "scenario.selected");
	ok &= expect(!hasErrors(reversedInfrastructure) && !hasErrors(reversedOperations),
		"reversed routes resolve stop nodes without legacy node-list storage");
	SceneModel ordered = completeScene();
	ordered.passengers.clear();
	ordered.services[0].stops[1].platformId.clear();
	buildInfrastructureAndSignallingFromScene(ordered);
	ok &= expect(!hasErrors(buildOperationsFromScene(ordered, "scenario.base"))
			&& regional_train[0].Stations[1].stationPlatformId == "platform.1",
		"a unique reachable platform resolves without an explicit selection");
	ordered.stations[1].platforms.push_back({"platform.other", {"node.3"}});
	buildInfrastructureAndSignallingFromScene(ordered);
	ok &= expect(hasCode(buildOperationsFromScene(ordered, "scenario.base"),
					 "scene.native.ref.platform.ambiguous"),
		"multiple reachable platforms require an explicit choice");
	ordered.services[0].stops[1].platformId = "platform.other";
	ok &= expect(hasCode(buildOperationsFromScene(ordered, "scenario.base"), "scene.native.ref.stop.order"),
		"an explicit late platform cannot be followed by an earlier station");

	SceneModel routeExternal = completeScene();
	routeExternal.routes[0].blocks = {"block.0", "block.1"};
	routeExternal.stations.push_back(
		{"station.3", "Three", true, 3.0, {{"platform.3", {"node.3"}}}});
	routeExternal.services[0].stops.push_back(
		{"station.3", {}, true, true, -120.0, -60.0, 2.0});
	routeExternal.passengers.clear();
	const auto routeExternalInfrastructure = buildInfrastructureAndSignallingFromScene(routeExternal);
	const auto routeExternalOperations = buildOperationsFromScene(routeExternal, "scenario.base");
	ok &= expect(!hasErrors(routeExternalInfrastructure) && !hasErrors(routeExternalOperations),
		"pre-entry and post-exit timetable rows remain valid without an invented platform");
	ok &= expect(regional_train[0].numStations == 4
			&& regional_train[0].Stations[3].stationPlatformId == ""
			&& regional_train[0].ScheduledArrivals[3] == -120.0
			&& regional_train[0].ScheduledDepartures[3] == -60.0,
		"route-external timetable rows preserve negative relative planned times");

	SceneModel shortHorizon = completeScene();
	shortHorizon.settings.durationSeconds = 20.0;
	const auto shortInfrastructure = buildInfrastructureAndSignallingFromScene(shortHorizon);
	const auto shortOperations = buildOperationsFromScene(shortHorizon, "scenario.base");
	ok &= expect(!hasErrors(shortInfrastructure) && !hasErrors(shortOperations)
			&& numRegions == 1 && AllDailyPassengers.size() == 1
			&& AllDailyPassengers.front().Journeys.size() == 1
			&& AllDailyPassengers.front().Journeys.front().ID == "journey.unrouted"
			&& AllDailyPassengers.front().Journeys.front().N_Trips == 0,
		"passenger journeys beyond a shortened run horizon are omitted whole");

	initial_variables.times = 120.0;
	initial_variables.durationOverride = true;
	SceneModel extendedHorizon = completeScene();
	const auto extendedInfrastructure = buildInfrastructureAndSignallingFromScene(extendedHorizon);
	const auto extendedOperations = buildOperationsFromScene(extendedHorizon, "scenario.selected");
	ok &= expect(!hasErrors(extendedInfrastructure) && !hasErrors(extendedOperations)
			&& initial_variables.times == 120.0 && numRegions == 4
			&& regional_train[0].instant_train_speed.size() == 120,
		"duration override sizes repeated services and runtime vectors to the effective horizon");
	initial_variables.durationOverride = false;

	SceneModel trajectoryPerformance = completeScene();
	trajectoryPerformance.settings.durationSeconds = 600.0;
	trajectoryPerformance.services[0].hasEntryTime = true;
	trajectoryPerformance.services[0].entryTimeSeconds = 0.0;
	trajectoryPerformance.services[0].hasRepeatCount = true;
	trajectoryPerformance.services[0].repeatCount = 1;
	trajectoryPerformance.services[0].through = false;
	trajectoryPerformance.services[0].stops.clear();
	trajectoryPerformance.passengers.clear();
	for (SceneTrainUnit& trainUnit : trajectoryPerformance.trainUnits)
		for (auto& band : trainUnit.tractionCurve)
			band[2] = 10000.0;
	trajectoryPerformance.services[0].performancePercent = 100.0;
	const auto trajectoryFullInfrastructure = buildInfrastructureAndSignallingFromScene(trajectoryPerformance);
	const auto trajectoryFullOperations = buildOperationsFromScene(trajectoryPerformance, "scenario.base");
	// Runs the first train through the live movement step that the dispatch loop uses.
	const auto simulateFirstTrainMovement = [&]() {
		for (int t = 0; t < initial_variables.times; t++) {
			regional_train[0].trajectoryComputationIncludingMovingBlock(t, signalCode1, signalCode2, signalCode3);
			regional_train[0].recordEarliestActiveTrajectoryIndex(t);
		}
	};
	int fullPerformanceEnd = -1;
	if (!hasErrors(trajectoryFullInfrastructure) && !hasErrors(trajectoryFullOperations) && numRegions == 1) {
		simulateFirstTrainMovement();
		fullPerformanceEnd = regional_train[0].End_Time;
	}
	trajectoryPerformance.services[0].performancePercent = 50.0;
	const auto trajectoryReducedInfrastructure = buildInfrastructureAndSignallingFromScene(trajectoryPerformance);
	const auto trajectoryReducedOperations = buildOperationsFromScene(trajectoryPerformance, "scenario.base");
	int reducedPerformanceEnd = -1;
	if (!hasErrors(trajectoryReducedInfrastructure) && !hasErrors(trajectoryReducedOperations) && numRegions == 1) {
		simulateFirstTrainMovement();
		reducedPerformanceEnd = regional_train[0].End_Time;
	}
	ok &= expect(fullPerformanceEnd >= 0 && reducedPerformanceEnd > fullPerformanceEnd,
		"reduced performance delays native route completion");

	const auto safetyInfrastructure = buildInfrastructureAndSignallingFromScene(trajectoryPerformance);
	const auto safetyOperations = buildOperationsFromScene(trajectoryPerformance, "scenario.base");
	ok &= expect(!hasErrors(safetyInfrastructure) && !hasErrors(safetyOperations) && numRegions == 1,
		"runtime safety fixture builds one zero-stop through service");
	if (!hasErrors(safetyInfrastructure) && !hasErrors(safetyOperations) && numRegions == 1) {
		Train& through = regional_train[0];
		through.departure_time = 0.0;
		through.checkTrainArrDep(0, 0);
		Stations finalProbe;
		finalProbe.stationName = "Final_Station";
		finalProbe.totalArrivalDelay = finalProbe.Max_TotalDelay = finalProbe.Max_Cons_Delay = 42.0;
		calculateDelayStatsAtStation(finalProbe);
		const bool delayUnavailable = finalProbe.N_Stopped_Trains == 0
			&& finalProbe.Av_Arrival_Delay == -1.0 && finalProbe.Std_Arrival_Delay == -1.0
			&& finalProbe.totalArrivalDelay == 0.0 && finalProbe.Max_TotalDelay == -1.0
			&& finalProbe.Max_Cons_Delay == -1.0;
		finalProbe.totalArrivalDelay = finalProbe.Max_TotalDelay = finalProbe.Max_Cons_Delay = 42.0;
		calculatePosAndNegDelayStatsAtStation(finalProbe);
		ok &= expect(through.numStations == 0 && delayUnavailable && finalProbe.N_Stopped_Trains == 0
				&& finalProbe.Av_Arrival_Delay == -1.0 && finalProbe.Std_Arrival_Delay == -1.0
				&& finalProbe.totalArrivalDelay == 0.0 && finalProbe.Max_TotalDelay == -1.0
				&& finalProbe.Max_Cons_Delay == -1.0,
			"zero-stop through service has unavailable final-station statistics");

		through.trajectoryComputationIncludingMovingBlock(0, signalCode1, signalCode2, signalCode3);
		ok &= expect(!through.CanEnter && through.instant_train_speed[0] == 0.0
				&& through.instant_spatial_position[0] == through.Start_Node_X * 1000,
			"time zero initializes the live trajectory without entering or advancing");

		Route& route = train_route[through.indexOfRoute];
		S_delay = 0.0;
		through.CanEnter = true;
		through.OutOfSimulation = false;
		const int nextHead = route.N_Block_Sections - 2;
		const Section& nextSection = route.sequence_of_block_sections[nextHead];
		through.instant_spatial_position[1] =
			(nextSection.start_node.X + nextSection.end_node.X) * 500.0;
		stationBoundarySections.clear();
		stationBoundarySections.emplace_back(&route.sequence_of_block_sections[nextHead + 1],
			route.reversed_direction, nullptr);
		BlocksOccupied.clear();
		BlocksConnected.clear();
		protectStationAreas(1);
		ok &= expect(std::find(BlocksConnected.begin(), BlocksConnected.end(),
						 route.sequence_of_block_sections[nextHead + 1].ID)
				!= BlocksConnected.end(),
			"zero-stop train keeps station-boundary route protection without stop indexing");

		// One section stored past N_Block_Sections: the lookahead must not read it.
		route.sequence_of_block_sections.emplace_back();
		{
			Section& outOfRoute = route.sequence_of_block_sections[route.N_Block_Sections];
			outOfRoute.ID = "out.of.route";
			const Section& tail = route.sequence_of_block_sections[route.N_Block_Sections - 1];
			through.instant_spatial_position[1] = (tail.start_node.X + tail.end_node.X) * 500.0;
			stationBoundarySections.clear();
			stationBoundarySections.emplace_back(&outOfRoute, route.reversed_direction, nullptr);
			BlocksOccupied.clear();
			BlocksConnected.clear();
			protectStationAreas(1);
			ok &= expect(std::find(BlocksConnected.begin(), BlocksConnected.end(), outOfRoute.ID)
					== BlocksConnected.end(),
				"station lookahead ignores sections beyond the actual route tail");
			stationBoundarySections.clear();
		}
		route.sequence_of_block_sections.pop_back();
	}

	SceneModel statisticsScene = completeScene();
	statisticsScene.services[0].hasRepeatCount = true;
	statisticsScene.services[0].repeatCount = 3;
	const auto statisticsInfrastructure = buildInfrastructureAndSignallingFromScene(statisticsScene);
	const auto statisticsOperations = buildOperationsFromScene(statisticsScene, "scenario.base");
	ok &= expect(!hasErrors(statisticsInfrastructure) && !hasErrors(statisticsOperations)
			&& numRegions == 3,
		"station statistics fixture builds three trains");
	ok &= expect(Final_Station.stationName == "Final_Station",
		"building a scene names the fictitious station of the final-station statistics");
	if (!hasErrors(statisticsInfrastructure) && !hasErrors(statisticsOperations) && numRegions == 3) {
		Stations statistics;
		statistics.stationName = "Final_Station";
		for (int trainIndex = 0; trainIndex < 3; ++trainIndex) {
			regional_train[trainIndex].numStations = 1;
			regional_train[trainIndex].StationArrivals[0] = -1;
			regional_train[trainIndex].ScheduledArrivals[0] = 0;
			regional_train[trainIndex].StationDelay[0] = -1;
			regional_train[trainIndex].StationConsecDelay[0] = -1;
		}

		numRegions = 0;
		calculateDelayStatsAtStation(statistics);
		Compute_Input_Delays();
		ok &= expect(statistics.N_Stopped_Trains == 0 && statistics.Av_Arrival_Delay == -1
				&& statistics.Std_Arrival_Delay == -1 && statistics.totalArrivalDelay == 0
				&& TotalInputDelays.Av_Arrival_Delay == -1
				&& EntranceInputDelays.Std_Arrival_Delay == -1
				&& DisturbanceInput.Perc_Delayed_T == -1,
			"empty delay populations keep the unavailable representation");

		numRegions = 1;
		regional_train[0].StationArrivals[0] = 100;
		regional_train[0].StationDelay[0] = 12;
		regional_train[0].StationConsecDelay[0] = 3;
		calculateDelayStatsAtStation(statistics);
		ok &= expect(statistics.Av_Arrival_Delay == 12 && statistics.Std_Arrival_Delay == 0
				&& statistics.totalArrivalDelay == 12 && statistics.Max_TotalDelay == 12
				&& std::isfinite(statistics.Perc_Delayed_T),
			"one positive station-delay sample has zero deviation");

		regional_train[0].StationDelay[0] = -1;
		calculatePosAndNegDelayStatsAtStation(statistics);
		ok &= expect(statistics.N_Stopped_Trains == 1 && statistics.Av_Arrival_Delay == -1
				&& statistics.Std_Arrival_Delay == 0 && statistics.totalArrivalDelay == -1,
			"a one-second early arrival is not confused with the delay sentinel");

		regional_train[0].StationDelay[0] = -12;
		regional_train[0].StationConsecDelay[0] = -3;
		calculatePosAndNegDelayStatsAtStation(statistics);
		ok &= expect(statistics.Av_Arrival_Delay == -12 && statistics.Std_Arrival_Delay == 0
				&& statistics.Max_TotalDelay == -12 && statistics.Max_Cons_Delay == -3,
			"one negative station-delay sample remains valid with zero deviation");

		const double positiveDelays[] = {10, 20, 0};
		const double mixedDelays[] = {-10, 0, 20};
		for (int trainIndex = 0; trainIndex < 3; ++trainIndex) {
			regional_train[trainIndex].StationArrivals[0] = 100;
			regional_train[trainIndex].StationDelay[0] = positiveDelays[trainIndex];
			regional_train[trainIndex].StationConsecDelay[0] = trainIndex;
		}
		numRegions = 3;
		calculateDelayStatsAtStation(statistics);
		ok &= expect(std::abs(statistics.Av_Arrival_Delay - 15) < 1e-9
				&& std::abs(statistics.Std_Arrival_Delay - std::sqrt(50.0)) < 1e-9
				&& statistics.N_Stopped_Trains == 3 && statistics.N_Delayed_Arr == 2
				&& std::isfinite(statistics.Perc_Delayed_T),
			"multiple positive delays retain the delayed-train sample denominator");

		for (int trainIndex = 0; trainIndex < 3; ++trainIndex)
			regional_train[trainIndex].StationDelay[0] = mixedDelays[trainIndex];
		calculatePosAndNegDelayStatsAtStation(statistics);
		ok &= expect(std::abs(statistics.Av_Arrival_Delay - (10.0 / 3.0)) < 1e-9
				&& std::abs(statistics.Std_Arrival_Delay - std::sqrt(700.0 / 3.0)) < 1e-9
				&& statistics.Max_TotalDelay == 20 && std::isfinite(statistics.totalArrivalDelay),
			"mixed early and late arrivals use the all-sample denominator");

		const double totalInputs[] = {0, 10, 20};
		const double entranceInputs[] = {0, 4, 8};
		for (int trainIndex = 0; trainIndex < 3; ++trainIndex) {
			regional_train[trainIndex].TotalInputDelays = totalInputs[trainIndex];
			regional_train[trainIndex].EntranceDelay = entranceInputs[trainIndex];
		}
		Compute_Input_Delays();
		ok &= expect(std::abs(TotalInputDelays.Av_Arrival_Delay - 15) < 1e-9
				&& std::abs(TotalInputDelays.Std_Arrival_Delay - std::sqrt(50.0)) < 1e-9
				&& std::abs(EntranceInputDelays.Av_Arrival_Delay - 6) < 1e-9
				&& std::abs(DisturbanceInput.Av_Arrival_Delay - 9) < 1e-9
				&& std::isfinite(EntranceInputDelays.Std_Arrival_Delay)
				&& std::isfinite(DisturbanceInput.Std_Arrival_Delay),
			"multiple input-delay populations remain finite");

		numRegions = 1;
		regional_train[0].TotalInputDelays = 0;
		regional_train[0].EntranceDelay = 0;
		Compute_Input_Delays();
		ok &= expect(TotalInputDelays.Av_Arrival_Delay == 0
				&& TotalInputDelays.Std_Arrival_Delay == 0
				&& EntranceInputDelays.Std_Arrival_Delay == 0
				&& DisturbanceInput.Std_Arrival_Delay == 0,
			"one all-punctual input population reports finite zeros");

		QTemporaryDir statisticsOutput;
		numStations = 1;
		Final_Station = statistics;
		Print_Station_Delay_Stats(statisticsOutput.path().toStdString(), "pos");
		std::ifstream exported(statisticsOutput.filePath("Stats_Stations.txt").toStdString());
		bool finiteExport = exported.good();
		for (std::string token; exported >> token;) {
			char* end = nullptr;
			const double value = std::strtod(token.c_str(), &end);
			if (end != token.c_str() && *end == '\0' && !std::isfinite(value))
				finiteExport = false;
		}
		ok &= expect(finiteExport, "one-station statistics export contains no non-finite values");

		// The statistics take the arrival of the timetable point, the one the timetable results report.
		Train& train = regional_train[0];
		TrainEvent point;
		point.SuccessorID = train.stationNameForArrivalStats(0);
		point.Time = 94;
		train.TimetablePoints.clear();
		train.TimetablePoints.push_back(point);
		train.StationArrivals[0] = 90; // recorded while the train was still braking into the station
		train.Determine_Actual_Station_Arrivals();
		ok &= expect(train.StationArrivals[0] == 94, "the delay statistics take the arrival of the timetable point");
		train.TimetablePoints.front().Time = -10000;
		train.Determine_Actual_Station_Arrivals();
		ok &= expect(train.StationArrivals[0] == -1, "a stop whose timetable point has no arrival has no arrival");
		train.TimetablePoints.clear();

		// A station served twice takes the timetable points in the order of the calls.
		const int singleStop = train.numStations;
		train.numStations = 2;
		train.StationArrivalNames[0] = train.StationArrivalNames[1] = "Probe";
		TrainEvent firstCall;
		firstCall.SuccessorID = "Probe";
		firstCall.Time = 94;
		TrainEvent secondCall = firstCall;
		secondCall.Time = 300;
		train.TimetablePoints.push_back(firstCall);
		train.TimetablePoints.push_back(secondCall);
		train.StationArrivals[0] = train.StationArrivals[1] = -1;
		train.Determine_Actual_Station_Arrivals();
		ok &= expect(train.StationArrivals[0] == 94 && train.StationArrivals[1] == 300,
			"each call at a station takes its own timetable point");
		train.TimetablePoints.clear();
		train.StationArrivalNames[0] = train.StationArrivalNames[1] = "None";
		train.numStations = singleStop;

		// A stop without a planned arrival has no arrival delay.
		train.StationArrivals[0] = 100;
		train.ScheduledArrivals[0] = -1;
		train.StationDelay[0] = -1;
		train.computeArrivalDelaysAtStations();
		ok &= expect(train.StationDelay[0] == -1, "a stop without a planned arrival has no arrival delay");
		train.Compute_Pos_And_Neg_Arrival_Delays_At_Stations();
		calculatePosAndNegDelayStatsAtStation(statistics);
		ok &= expect(train.StationDelay[0] == -1 && statistics.N_Stopped_Trains == 0,
			"a stop without a planned arrival is not in the signed statistics");

		// A train that did not arrive has no arrival delay and is not counted as punctual.
		train.StationArrivals[0] = -1;
		train.ScheduledArrivals[0] = 90;
		train.computeArrivalDelaysAtStations();
		ok &= expect(train.StationDelay[0] == -1 && train.StationArrivals[0] == -1,
			"a train that did not arrive has no arrival delay");
		train.Compute_Pos_And_Neg_Arrival_Delays_At_Stations();
		calculatePosAndNegDelayStatsAtStation(statistics);
		ok &= expect(train.StationDelay[0] == -1 && statistics.N_Stopped_Trains == 0,
			"a train that did not arrive is not in the signed statistics");

		train.StationArrivals[0] = 100;
		train.Compute_Pos_And_Neg_Arrival_Delays_At_Stations();
		calculatePosAndNegDelayStatsAtStation(statistics);
		ok &= expect(train.StationDelay[0] == 10 && statistics.N_Stopped_Trains == 1 && statistics.Av_Arrival_Delay == 10,
			"a stop with a planned arrival is in the signed statistics");
	}
	ok &= passengerRateTests();
	ok &= brakingPointTests();
	ok &= brakingCurveTests();
	{
		const double brakingPoint = 46181.0;
		const double parked = std::nextafter(brakingPoint - kStopHoldbackM, 0.0);
		ok &= expect(!isShortOfBrakingPoint(parked, brakingPoint),
			"a train parked one ulp below its hold-back position is not short of the braking point");
		ok &= expect(isShortOfBrakingPoint(brakingPoint - 0.001, brakingPoint),
			"a train 1 mm short of the braking point still accelerates");
	}
	{
		// A stop of 20 s at a point whose parked position is exact, one unit in the last place
		// low or one high: the timetable point reports the same arrival and departure each time.
		const double stoppingPoint = 46181.0;
		const double exact = stoppingPoint - kStopHoldbackM;
		const int savedTimes = initial_variables.times;
		initial_variables.times = 60;
		for (const double parked : {exact, std::nextafter(exact, 0.0),
				 std::nextafter(exact, std::numeric_limits<double>::infinity())}) {
			Train stopping;
			stopping.indexOfRoute = regional_train[0].indexOfRoute;
			stopping.departure_time = 1.0;
			stopping.instant_spatial_position.assign(60, stoppingPoint + 500.0);
			for (int second = 0; second < 10; ++second)
				stopping.instant_spatial_position[second] = stoppingPoint - 1000.0 + 100.0 * second;
			for (int second = 10; second < 30; ++second)
				stopping.instant_spatial_position[second] = parked;
			stopping.instant_spatial_position[30] = stoppingPoint + kStopHoldbackM;
			TrainEvent point;
			stopping.computeArrivalAndDepartureAtLocation(stoppingPoint, point);
			ok &= expect(point.Time == 9.0 * timestep && point.Time2 == 29.0 * timestep,
				"a timetable point reports arrival and departure of a stop whatever the last place of the parked position");
			ok &= expect(!isShortOfBrakingPoint(parked, stoppingPoint) && !isPastStopHoldback(parked, stoppingPoint)
					&& isPastStopHoldback(stoppingPoint + kStopHoldbackM, stoppingPoint),
				"a parked position is at the stop and the position after the stop is past it");
		}
		{
			// A last stop: the train arrives and stays until the end of the run.
			Train terminating;
			terminating.indexOfRoute = regional_train[0].indexOfRoute;
			terminating.departure_time = 1.0;
			terminating.instant_spatial_position.assign(60, exact);
			for (int second = 0; second < 10; ++second)
				terminating.instant_spatial_position[second] = stoppingPoint - 1000.0 + 100.0 * second;
			TrainEvent point;
			terminating.computeArrivalAndDepartureAtLocation(stoppingPoint, point);
			ok &= expect(point.Time == 9.0 * timestep && point.Time2 == TrainEvent().Time2,
				"a timetable point reports the arrival at a last stop and no departure");
		}
		initial_variables.times = savedTimes;
	}
	ok &= seedTests(completeScene());
	ok &= noFileAccessTests(completeScene());
	ok &= routeBoundaryTests();
	ok &= levelBorderAspectTests();
	ok &= regionalTrainStorageTests();
	ok &= singleTrackLockTests();
	if (ok) std::cout << "native forward/reverse route diagram coordinates passed\n";
	return ok ? 0 : 1;
}
