#include "scene/SceneModel.h"
#include "scene/SignallingLevel.h"
#include "scene/SignallingLevelNames.h"
#include "simulation/Passengers.h"
#include "simulation/RollingStock.h"
#include "simulation/Signalling.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationRun.h"
#include "diagrams/CompletedRunData.h"
#include "diagrams/RouteDiagramCoordinates.h"
#include "util/Log.h"
#include "util/timeutil.hpp"

// The Qt headers come before the undef: Qt defines `signals`, and the scene model has a member of that name.
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#ifdef signals
#undef signals
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <sstream>
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

// The builders write the run of the process. The slots run in the order of their declaration, which is the order of the
// groups of checks before them, and each slot builds the scene that it reads, so that it also passes alone.
class TestOperationsBuilder : public QObject {
	Q_OBJECT

private slots:
	void recordedDrawsOfFixedSeeds_data();
	void recordedDrawsOfFixedSeeds();
	void generatorAdvancesAndLimitsDraws();
	void platformNumberOfId_data();
	void platformNumberOfId();
	void copiedBlockingTimeKeepsOwnTimes();
	void diagramFixtureSharesReferenceCoordinates();
	void buildsCompleteFixture();
	void directBlockSignalFailureStaysCompatible();
	void unboundSignalFailureIsRejected();
	void ambiguousSignalFailureIsRejected();
	void reducedBreakdownTargetsOneOccurrence();
	void buildsPlatformGeometryAndPassengers();
	void commandLineBufferAndRecoveryStay();
	void steppedOperatingCodesReachOccurrences();
	void serviceCapAndPerformanceApplyPerTrain();
	void occurrenceSelectionOmitsExcludedJourneys();
	void legacyReversePassengerLegIsOmitted();
	void repeatedStopsKeepOrderedPair();
	void sparseSelectionExpandsSelectedOccurrence();
	void outOfPatternEntranceDelayIsRejected();
	void rejectsInvalidEntranceDelay_data();
	void rejectsInvalidEntranceDelay();
	void sortsNonFiniteEventTimesLast();
	void invalidScenarioKeepsRuntimeState();
	void reversedRouteResolvesStopNodes();
	void resolvesPlatformsInRouteOrder();
	void stopsKeepPlatformNumbers();
	void routeExternalRowsKeepPlannedTimes();
	void shortHorizonOmitsLateJourneys();
	void durationOverrideSizesRuntimeVectors();
	void reducedPerformanceDelaysCompletion();
	void zeroStopThroughServiceStaysSafe();
	void stationStatisticsOfSmallPopulations();
	void passengerRatesAndDwellTerms();
	void indexesFindPassengersAndPlatforms();
	void boardingLooksUpMatchingIndex();
	void waitingPassengersAreListedByDeparture();
	void boardingWithoutPassengerGuiKeepsPlatformList();
	void brakingPointSearch();
	void brakingStartCacheMatchesComputation();
	void brakingStartCacheBelongsToOneTrain();
	void drawsBrakingCurveThatReachesSpeed();
	void brakingWithoutReachingSpeed_data();
	void brakingWithoutReachingSpeed();
	void brakingStepValues();
	void inverseStepsSolveForwardModel_data();
	void inverseStepsSolveForwardModel();
	void lateNativeStateUsesFallback();
	void zeroSpeedTerminalsKeepCoordinate();
	void changedLateTargetKeepsHistory();
	void rejectsInvalidInverseBranches();
	void linearInverseSolvesActualStartStep();
	void nonMonotoneInverseRecovers();
	void brakingLookupMatchesLinearReference();
	void backwardBrakingCursorMatchesLookup();
	void parkedTrainIsNotShortOfBrakingPoint();
	void timetablePointOfStopIgnoresLastPlace();
	void brakingPhase();
	void level4Transitions_data();
	void level4Transitions();
	void stationaryBrakingCompletesDwell();
	void signalFailureEvidence_data();
	void signalFailureEvidence();
	void seedsGiveRepeatableDraws();
	void preparesWithoutFileAccess();
	void firstRouteSectionWithVirtualSignal();
	void levelBorderAspects_data();
	void levelBorderAspects();
	void unsetLevelTakesNoChainOfLevel0();
	void trainStorageHoldsLastBuild();
	void singleTrackLocks();
	void signallingFindsSectionsById();
	void rollingStockMessages();
	void aspectIsRestrictive_data();
	void aspectIsRestrictive();
	void cleanup();
	void cleanupTestCase();

private:
	bool m_failed = false;
};

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
	defaultRun().numberGenerator = NumberGenerator(seed);
	return train.computePaxDependentDwellTimeAtStations(boarding, alighting, platformRate,
		7.0f, 0.32f, 18.23f, 0.564f, 4.838f, 22.24f, 0.04f, 0.562f);
}

// Draws of the generator for fixed seeds, recorded from the algorithm as it is.
// The uniform draws are one product of an integer below 2^31 and the constant
// 1 / 2147483647, so they are identical on every platform. The Gaussian draws
// pass through sqrt and log, which may differ in the last bits between math
// libraries, so they are compared with a tolerance far above that.
struct RecordedDraws {
	unsigned long seed;
	double uniform[4];
	int integer[6];
	double gaussian[4];
	double scaled[2];
};
static const RecordedDraws kRecordedDraws[] = {
	{kSeedA, {0.91197702843322281, 0.061727229068860051, 0.3969884758801146, 0.35162865992245668}, {91, 6, 39, 35, 82, 75},
		{-1.6577351000707685, -1.150935343416547, 0.54755957323394899, 0.69422240928046186},
		{6.6845297998584634, 7.6981293131669055}},
	{789350715, {0.71631288189269271, 0.78997236666733972, 0.84746502938096646, 0.094018201387495823}, {71, 78, 84, 9, 89, 67},
		{0.91194289224928671, 0.68028894411965013, 0.29900609005941814, 0.68283721049533164},
		{11.823885784498573, 11.3605778882393}},
	// Draws at both ends of the valid seed range: seed 1 and the largest seed.
	{1, {0.41599935685098144, 0.091964890757559287, 0.75641048595142113, 0.52970019333516261}, {41, 9, 75, 52, 93, 38},
		{-0.83685380259280617, -0.17227992407322446, 0.18711744825407453, 1.6154398490064397},
		{8.3262923948143879, 9.6554401518535506}},
	{kMaxRandomSeed, {0.93315776248143878, 0.61649792251014057, 0.58251402554219311, 0.31322728763950397},
		{93, 61, 58, 31, 29, 8}, {0.17116944175794566, 0.63643514664920209, -1.7312639403219627, 0.76485239833256835},
		{10.342338883515891, 11.272870293298404}},
};

void TestOperationsBuilder::recordedDrawsOfFixedSeeds_data() {
	QTest::addColumn<int>("index");
	for (int index = 0; index < static_cast<int>(std::size(kRecordedDraws)); ++index)
		QTest::newRow(("seed " + std::to_string(kRecordedDraws[index].seed)).c_str()) << index;
}

void TestOperationsBuilder::recordedDrawsOfFixedSeeds() {
	QFETCH(int, index);
	const RecordedDraws& row = kRecordedDraws[index];
	const auto close = [](double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; };
	NumberGenerator uniform(row.seed);
	for (int draw = 0; draw < 4; ++draw)
		QVERIFY2(close(uniform.getUniformFloat(), row.uniform[draw], 1e-15), "uniform draws of a fixed seed match");
	NumberGenerator integer(row.seed);
	for (int draw = 0; draw < 6; ++draw)
		QVERIFY2(integer.getUniformInteger(0, 99) == row.integer[draw], "integer draws of a fixed seed match");
	NumberGenerator gaussian(row.seed);
	for (int draw = 0; draw < 4; ++draw)
		QVERIFY2(close(gaussian.getGaussianFloat(), row.gaussian[draw], 1e-12), "Gaussian draws of a fixed seed match");
	NumberGenerator scaled(row.seed);
	for (int draw = 0; draw < 2; ++draw)
		QVERIFY2(close(scaled.getGaussianFloat(10, 2), row.scaled[draw], 1e-12), "scaled Gaussian draws of a fixed seed match");
}

void TestOperationsBuilder::generatorAdvancesAndLimitsDraws() {
	const auto close = [](double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; };
	NumberGenerator first(kSeedA), second(kSeedA);
	second.getUniformFloat();
	QVERIFY2(first.getUniformFloat() != second.getUniformFloat(), "draws advance the generator");

	// The largest seed is the number given for --seed.
	QCOMPARE(kMaxRandomSeed, 2147483646UL);

	// The 1286th draw of seed 1 is the first one that reaches the upper limit of 1 - 1.2e-7. The value
	// before the limit is applied is 2147483531 / 2147483647.
	const double aroundLimit[] = {0.51375901117630252, 0.961920628306419};
	NumberGenerator limit(1);
	for (int index = 0; index < 1284; ++index)
		limit.getUniformFloat();
	QVERIFY2(close(limit.getUniformFloat(), aroundLimit[0], 1e-15), "the draw before the upper limit matches");
	QVERIFY2(close(limit.getUniformFloat(), 1.0 - 1.2e-7, 1e-15), "a draw above 1 - 1.2e-7 is limited to 1 - 1.2e-7");
	QVERIFY2(close(limit.getUniformFloat(), aroundLimit[1], 1e-15), "the draw after the upper limit matches");

	// Restarting the run generator from a seed repeats its draws, and the default seed restores its initial state.
	const auto restoreGenerator = qScopeGuard([] { defaultRun().numberGenerator = NumberGenerator(kDefaultRandomSeed); });
	defaultRun().numberGenerator = NumberGenerator(kSeedA);
	const double firstRunDraw = defaultRun().numberGenerator.getUniformFloat();
	QVERIFY2(close(firstRunDraw, kRecordedDraws[0].uniform[0], 1e-15), "the run generator starts from the seed it is given");
	defaultRun().numberGenerator.getUniformFloat();
	defaultRun().numberGenerator = NumberGenerator(kSeedA);
	QVERIFY2(close(defaultRun().numberGenerator.getUniformFloat(), firstRunDraw, 1e-15), "restarting the run generator repeats its draws");
}

void TestOperationsBuilder::platformNumberOfId_data() {
	QTest::addColumn<QString>("id");
	QTest::addColumn<int>("number");
	QTest::newRow("label") << "Platform_4" << 4;
	QTest::newRow("two digits") << "Platform_12" << 12;
	QTest::newRow("number alone") << "3" << 3;
	QTest::newRow("truncated") << "2.9" << 2;
	QTest::newRow("trailing text") << "Platform_2a" << 2;
	QTest::newRow("exponent") << "Platform_1e2" << 100;
	QTest::newRow("negative") << "Platform_-2" << -2;
	QTest::newRow("leading space") << " Platform_5" << 5;
	QTest::newRow("number before the label") << "7_Platform_1" << 7;
	QTest::newRow("text before the label") << "x_Platform_7" << 0;
	QTest::newRow("label twice") << "Platform_Platform_3" << 0;
	QTest::newRow("label alone") << "Platform_" << 0;
	QTest::newRow("lower case") << "platform_6" << 0;
	QTest::newRow("scene id") << "B.platform.1" << 0;
	QTest::newRow("short id") << "P1" << 0;
	QTest::newRow("no platform") << "None" << 0;
	QTest::newRow("empty") << "" << 0;
	QTest::newRow("reads as not a number") << "Nantes.platform.1" << 0;
	QTest::newRow("reads as infinity") << "INF.platform.2" << 0;
	QTest::newRow("beyond int") << "Platform_1e30" << 0;
	QTest::newRow("below int") << "Platform_-1e30" << 0;
	QTest::newRow("largest int") << "Platform_2147483647.9" << std::numeric_limits<int>::max();
	QTest::newRow("lowest int") << "Platform_-2147483648.9" << std::numeric_limits<int>::min();
}

void TestOperationsBuilder::platformNumberOfId() {
	QFETCH(QString, id);
	QFETCH(int, number);
	QCOMPARE(platformNumber(id.toStdString()), number);
}

void TestOperationsBuilder::copiedBlockingTimeKeepsOwnTimes() {
	BlockingTimes source;
	source.setupTime = 2.0;
	source.sightReacTime = 5.0;
	BlockingTimes copy;
	copy = source;
	// A copied blocking time keeps its own sight and reaction time.
	QCOMPARE_EQ(copy.setupTime, 2.0);
	QCOMPARE_EQ(copy.sightReacTime, 5.0);
}

static bool sameSections(const std::vector<BlockSectionInput>& actual, const std::vector<BlockSectionInput>& expected) {
	if (actual.size() != expected.size())
		return false;
	for (std::size_t i = 0; i < actual.size(); ++i)
		if (actual[i].id != expected[i].id || std::fabs(actual[i].startKm - expected[i].startKm) > 1e-9
			|| std::fabs(actual[i].endKm - expected[i].endKm) > 1e-9)
			return false;
	return true;
}

void TestOperationsBuilder::diagramFixtureSharesReferenceCoordinates() {
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
	const auto diagramInfra = buildInfrastructureAndSignallingFromScene(defaultRun(), diagramScene);
	const auto diagramOps = hasErrors(diagramInfra) ? diagramInfra
													: buildOperationsFromScene(defaultRun(), diagramScene, "scenario.base");
	// Forward and reverse diagram fixture builds through native paths.
	QVERIFY(!hasErrors(diagramInfra));
	QVERIFY(!hasErrors(diagramOps));
	if (defaultRun().numTrains <= 1)
		return;
	const RouteDiagramPath reference = routeDiagramPath(defaultRun().infra.routes[defaultRun().trains[0].indexOfRoute], &diagramScene);
	const auto identity = buildRouteDiagramProjection(reference, reference);
	const Train& forward = defaultRun().trains[0];
	const Train& reverse = defaultRun().trains[defaultRun().numTrains - 1];
	const RouteDiagramPath reversePath = routeDiagramPath(defaultRun().infra.routes[reverse.indexOfRoute], &diagramScene);
	const auto reverseProjection = buildRouteDiagramProjection(reversePath, reference);
	const auto stopOf = [](const Train& train, int index) {
		CompletedStop stop;
		stop.nodeId = train.Stations[index].sceneNodeId;
		stop.routeX = train.Stations[index].X;
		return stop;
	};
	const auto forwardStop = routeDiagramStopPosition(stopOf(forward, 1), reference, identity);
	const auto backwardStop = routeDiagramStopPosition(stopOf(reverse, 1), reversePath, reverseProjection);
	// Native forward/reverse stop nodes share reference coordinates.
	QVERIFY(forwardStop.has_value());
	QVERIFY(backwardStop.has_value());
	QVERIFY(std::fabs(*forwardStop - 43.0) < 1e-6);
	QVERIFY(std::fabs(*backwardStop - 43.0) < 1e-6);
	const double forwardSample = forward.Stations[1].X * 1000.0;
	const double reverseSample = reverse.Stations[1].X * 1000.0;
	const auto forwardAtSample = identity.map(routeDiagramTrajectoryKm(forwardSample));
	const auto reverseAtSample = reverseProjection.map(routeDiagramTrajectoryKm(reverseSample));
	// Runtime sample metres project without a second direction reversal.
	QVERIFY(forwardAtSample.has_value());
	QVERIFY(reverseAtSample.has_value());
	QVERIFY(std::fabs(*forwardAtSample - *forwardStop) < 1e-6);
	QVERIFY(std::fabs(*reverseAtSample - *backwardStop) < 1e-6);
	// Forward: block.0 to block.2 over the nodes at 42 to 45 km. Reversed: the route length is the
	// end of the first listed block (45 km) and each section becomes [length - end, length - start],
	// so the sections run 0 to 1, 1 to 2 and 2 to 3 km in travel direction.
	const std::vector<BlockSectionInput> forwardSections = {{"@block.0@", 42.0, 43.0}, {"@block.1@", 43.0, 44.0},
		{"@block.2@", 44.0, 45.0}};
	const std::vector<BlockSectionInput> reverseSections = {{"@block.2@", 0.0, 1.0}, {"@block.1@", 1.0, 2.0},
		{"@block.0@", 2.0, 3.0}};
	QVERIFY2(sameSections(blockSectionInputsForRoute(defaultRun().infra.routes[forward.indexOfRoute]), forwardSections),
		"forward route sections keep the block ends in route order");
	QVERIFY2(sameSections(blockSectionInputsForRoute(defaultRun().infra.routes[reverse.indexOfRoute]), reverseSections),
		"reversed route sections increase along the travel direction");
}

// The complete fixture with the historical through flag, which its nonempty stops take precedence over.
static SceneModel throughScene() {
	SceneModel scene = completeScene();
	scene.services[0].through = true;
	return scene;
}

void TestOperationsBuilder::buildsCompleteFixture() {
	const std::string savedInputFolder = initial_variables.InputMainFolder;
	const auto restoreInputFolder = qScopeGuard([&savedInputFolder] { initial_variables.InputMainFolder = savedInputFolder; });
	SceneModel scene = throughScene();
	initial_variables.InputMainFolder = "/__egtrain_nonexistent_native_input__";
	auto infrastructureDiagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), scene);
	QVERIFY2(!hasErrors(infrastructureDiagnostics), "M2 infrastructure builder accepts the complete fixture");

	const auto diagnostics = buildOperationsFromScene(defaultRun(), scene, "scenario.selected");
	QVERIFY2(!hasErrors(diagnostics), "M3 operations builder accepts the complete fixture");
	// Historical through flag does not discard scheduled stops.
	QCOMPARE(defaultRun().trains[0].numStations, 3);
	SceneModel tooManyStops = completeScene();
	while (tooManyStops.services[0].stops.size() <= static_cast<std::size_t>(Train::kMaxTimetableStations))
		tooManyStops.services[0].stops.push_back(tooManyStops.services[0].stops.back());
	const auto tooManyStopsDiagnostics = buildOperationsFromScene(defaultRun(), tooManyStops, "scenario.base");
	QVERIFY2(hasCode(tooManyStopsDiagnostics, "scene.native.capacity.stops"),
		"native operations rejects one stop above the timetable limit");
	SceneModel tooManyTrains = completeScene();
	tooManyTrains.services[0].hasRepeatCount = true;
	tooManyTrains.services[0].repeatCount = Max_N_Reg + 1;
	const auto tooManyTrainsDiagnostics = buildOperationsFromScene(defaultRun(), tooManyTrains, "scenario.base");
	QVERIFY2(hasCode(tooManyTrainsDiagnostics, "scene.native.capacity.trains"),
		"native operations rejects one expanded train above the limit");
	// Native builders do not access or rewrite the legacy input folder.
	QCOMPARE(initial_variables.InputMainFolder, "/__egtrain_nonexistent_native_input__");
	// Repeat expansion populates train counts.
	QCOMPARE(defaultRun().numTrains, 3);
	// Occurrence IDs are canonical and stable.
	QCOMPARE(defaultRun().trains[0].trainDescription, "service.native-1");
	QCOMPARE(defaultRun().trains[1].trainDescription, "service.native-2");
	QCOMPARE(defaultRun().trains[2].trainDescription, "service.native-3");
	// Occurrences share the canonical service type.
	QCOMPARE(defaultRun().trains[0].type, "service.native");
	// Repeated services without a step expose readable operating codes.
	QCOMPARE(defaultRun().trains[0].operatingCode, "9707-1");
	QCOMPARE(defaultRun().trains[1].operatingCode, "9707-2");
	QCOMPARE(defaultRun().trains[2].operatingCode, "9707-3");
	// Ordered multi-unit physical and traction data is aggregated.
	QCOMPARE_EQ(defaultRun().trains[0].number_of_wagons, 3.0);
	QCOMPARE_EQ(defaultRun().trains[0].total_train_mass, 290.0);
	QCOMPARE(defaultRun().trains[0].velocityIntervals, 2);
	// Built train capacity follows the aggregated wagon count.
	QCOMPARE(defaultRun().trains[0].MAX_OnBoard_Passengers, 1200);
	QCOMPARE(defaultRun().trains[0].Current_OnBoard_Passengers, 0);
	// Canonical entry times remain scheduled times.
	QCOMPARE_EQ(defaultRun().trains[0].scheduled_departure_time, 100.0);
	QCOMPARE_EQ(defaultRun().trains[1].scheduled_departure_time, 130.0);
	QCOMPARE_EQ(defaultRun().trains[2].scheduled_departure_time, 160.0);
	// Optional timetable fields retain runtime -1 sentinels.
	QCOMPARE_EQ(defaultRun().trains[0].ScheduledArrivals[0], -1.0);
	QCOMPARE_EQ(defaultRun().trains[0].ScheduledArrivals[1], 120.0);
	QCOMPARE_EQ(defaultRun().trains[0].ScheduledDepartures[2], -1.0);
	// Planned references retain a valid route-origin station at kilometre zero.
	QVERIFY(defaultRun().trains[0].stationIsOnRoute(0));
	QCOMPARE_EQ(defaultRun().trains[0].stationRoutePositionMeters(0), 0.0);
	Node offRouteStation;
	offRouteStation.station = true;
	offRouteStation.stationName = "Outside";
	Train routeMembershipProbe;
	routeMembershipProbe.run_ = &defaultRun();
	routeMembershipProbe.Stations.push_back(offRouteStation);
	routeMembershipProbe.numStations = 1;
	routeMembershipProbe.indexOfRoute = defaultRun().trains[0].indexOfRoute;
	// Route-external static timetable stops remain inert in planned route results.
	QVERIFY(!routeMembershipProbe.stationIsOnRoute(0));
	QVERIFY(routeMembershipProbe.stationRoutePositionMeters(0) < 0.0);
	// Tractive-effort samples are allocated with the trajectory.
	QCOMPARE(defaultRun().trains[0].instant_train_tractive_effort.size(), defaultRun().trains[0].instant_spatial_position.size());
	// Selected entrance delays apply to the requested occurrence and stops.
	QCOMPARE_EQ(defaultRun().trains[1].ScheduledDepartures[0], 145.0);
	QCOMPARE_EQ(defaultRun().trains[1].ScheduledDepartures[1], 160.0);
	QCOMPARE_EQ(defaultRun().trains[1].EntranceDelay, 5.0);
	// The first delayed stop of the delayed occurrence names the entrance delay station.
	QVERIFY(defaultRun().trains[0].entranceDelayStationId.empty());
	QCOMPARE(defaultRun().trains[1].entranceDelayStationId, "Zero");
	QVERIFY(defaultRun().trains[2].entranceDelayStationId.empty());
	// Trains may enter at their scheduled times.
	QCOMPARE_EQ(defaultRun().trains[0].departure_time, 100.0);
	QCOMPARE_EQ(defaultRun().trains[1].departure_time, 130.0);
	QCOMPARE_EQ(defaultRun().trains[2].departure_time, 160.0);
	// Only the selected scenario reaches runtime incidents.
	QCOMPARE(defaultRun().incidents.size(), std::size_t(2));
	QCOMPARE(defaultRun().incidents[0].target, "signal.0");
	QCOMPARE(defaultRun().incidents[1].target, "service.native");
	// Legacy breakdown incidents retain full-hold runtime defaults.
	QCOMPARE(defaultRun().incidents[1].id, "incident.breakdown");
	QVERIFY(defaultRun().incidents[1].hasEndSeconds);
	QVERIFY(!defaultRun().incidents[1].hasOccurrence);
	QVERIFY(!defaultRun().incidents[1].hasReducedSpeed);
	// Signal failure resolves its protected section to the exact runtime ID.
	QCOMPARE(defaultRun().incidents.front().resolvedSectionIDs, std::vector<std::string>{"@block.1@"});
}

void TestOperationsBuilder::directBlockSignalFailureStaysCompatible() {
	SceneModel directBlockIncident = completeScene();
	directBlockIncident.signals.front().protectedSection.clear();
	directBlockIncident.scenarios[1].incidents[0].target = "block.1";
	const auto directInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), directBlockIncident);
	const auto directOperations = buildOperationsFromScene(defaultRun(), directBlockIncident, "scenario.selected");
	// Direct base-block signal failures remain compatible without signal binding.
	QVERIFY(!hasErrors(directInfrastructure));
	QVERIFY(!hasErrors(directOperations));
	QVERIFY(!defaultRun().incidents.empty());
	QCOMPARE(defaultRun().incidents.front().resolvedSectionIDs, std::vector<std::string>{"@block.1@"});
}

void TestOperationsBuilder::unboundSignalFailureIsRejected() {
	SceneModel unboundSignal = completeScene();
	unboundSignal.signals.front().protectedSection.clear();
	const auto unboundInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), unboundSignal);
	const auto unboundOperations = buildOperationsFromScene(defaultRun(), unboundSignal, "scenario.selected");
	// Targeting an unbound signal fails operations staging instead of guessing.
	QVERIFY(!hasErrors(unboundInfrastructure));
	QVERIFY(hasErrors(unboundOperations));
}

void TestOperationsBuilder::ambiguousSignalFailureIsRejected() {
	SceneModel ambiguousSignalTarget = completeScene();
	ambiguousSignalTarget.blocks.front().id = "signal.0";
	ambiguousSignalTarget.routes.front().blocks.front() = "signal.0";
	const auto ambiguousInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), ambiguousSignalTarget);
	const auto ambiguousOperations = buildOperationsFromScene(defaultRun(), ambiguousSignalTarget, "scenario.selected");
	// A signal failure target matching both a signal and block is rejected as ambiguous.
	QVERIFY(!hasErrors(ambiguousInfrastructure));
	QVERIFY(hasErrors(ambiguousOperations));
}

// The checks of the platform list read the run that this build left, as before.
void TestOperationsBuilder::reducedBreakdownTargetsOneOccurrence() {
	SceneModel occurrenceSpecific = completeScene();
	SceneIncident& occurrenceBreakdown = occurrenceSpecific.scenarios[1].incidents[1];
	occurrenceBreakdown.hasOccurrence = true;
	occurrenceBreakdown.occurrence = 2;
	occurrenceBreakdown.hasReducedSpeed = true;
	occurrenceBreakdown.reducedSpeedKmh = 40.0;
	occurrenceBreakdown.hasEndSeconds = false;
	occurrenceBreakdown.endSeconds = 0.0;
	occurrenceBreakdown.terminateAtDestination = true;
	const auto occurrenceInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), occurrenceSpecific);
	const auto occurrenceOperations = buildOperationsFromScene(defaultRun(), occurrenceSpecific, "scenario.selected");
	// Reduced breakdown staging targets one occurrence and continues without recovery.
	QVERIFY(!hasErrors(occurrenceInfrastructure));
	QVERIFY(!hasErrors(occurrenceOperations));
	QCOMPARE(defaultRun().incidents.size(), std::size_t(2));
	QVERIFY(defaultRun().incidents[1].hasOccurrence);
	QCOMPARE(defaultRun().incidents[1].occurrence, 2);
	QVERIFY(defaultRun().incidents[1].hasReducedSpeed);
	QVERIFY(std::fabs(defaultRun().incidents[1].reducedSpeedKmh - 40.0) < 1e-9);
	QVERIFY(!Incident_Holds_Train(defaultRun(), "service.native-2", 50));
	QVERIFY(defaultRun().trains[1].destinationTerminationRequested);
	QVERIFY(!defaultRun().trains[0].destinationTerminationRequested);
	QVERIFY(!defaultRun().trains[2].destinationTerminationRequested);
	defaultRun().trains[1].directIncidentIds.clear();
	// A nonbinding breakdown cap is not recorded as direct evidence.
	QVERIFY(std::fabs(defaultRun().trains[1].effectiveIncidentSpeedLimit(10.0, 50) - 10.0) < 1e-9);
	QVERIFY(defaultRun().trains[1].directIncidentIds.empty());
	// A binding cap records direct evidence only on the targeted occurrence.
	QVERIFY(std::fabs(defaultRun().trains[1].effectiveIncidentSpeedLimit(25.0, 50) - 40.0 / 3.6) < 1e-9);
	QCOMPARE(defaultRun().trains[1].directIncidentIds, std::vector<std::string>{"incident.breakdown"});
	QVERIFY(std::fabs(defaultRun().trains[0].effectiveIncidentSpeedLimit(25.0, 50) - 25.0) < 1e-9);
	SimulationIncident overlappingHold = defaultRun().incidents[1];
	overlappingHold.id = "incident.hold";
	overlappingHold.hasReducedSpeed = false;
	overlappingHold.reducedSpeedKmh = 0.0;
	overlappingHold.hasEndSeconds = true;
	overlappingHold.startSeconds = 45.0;
	overlappingHold.endSeconds = 55.0;
	defaultRun().incidents.push_back(overlappingHold);
	// An overlapping full hold dominates an earlier reduced-speed incident.
	QVERIFY(Incident_Holds_Train(defaultRun(), "service.native-2", 50));
	const SimulationIncident* const active = Active_Train_Breakdown(defaultRun(), "service.native-2", 50);
	if (active == nullptr)
		QFAIL("an overlapping full hold dominates an earlier reduced-speed incident");
	QCOMPARE(active->id, "incident.hold");
	SimulationIncident strictCap = defaultRun().incidents[1];
	strictCap.id = "incident.strict-cap";
	strictCap.reducedSpeedKmh = 30.0;
	defaultRun().incidents.push_back(strictCap);
	defaultRun().trains[1].directIncidentIds.clear();
	// The strictest concurrent cap supplies the governing direct evidence.
	QVERIFY(std::fabs(defaultRun().trains[1].effectiveIncidentSpeedLimit(25.0, 60) - 30.0 / 3.6) < 1e-9);
	QCOMPARE(defaultRun().trains[1].directIncidentIds, std::vector<std::string>{"incident.strict-cap"});
	// Native operations reuses the M2 platform list.
	QCOMPARE(defaultRun().platforms.size(), std::size_t(3));
	// Platform stopping lists use stable occurrence descriptions.
	QCOMPARE(defaultRun().platforms.front().List_Trains_Stopping_At_Platform.front(), "service.native-1");
}

void TestOperationsBuilder::buildsPlatformGeometryAndPassengers() {
	SceneModel geometry = completeScene();
	geometry.stations[0].platforms[0].hasLength = true;
	geometry.stations[0].platforms[0].lengthM = 140.0;
	geometry.stations[0].platforms[0].hasWidth = true;
	geometry.stations[0].platforms[0].widthM = 3.0;
	const auto geometryInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), geometry);
	const auto geometryOperations = buildOperationsFromScene(defaultRun(), geometry, "scenario.selected");
	// Explicit platform geometry reaches native operations.
	QVERIFY(!hasErrors(geometryInfrastructure));
	QVERIFY(!hasErrors(geometryOperations));
	QCOMPARE(defaultRun().platforms.size(), std::size_t(3));
	auto platform = defaultRun().platforms.begin();
	// Native platform capacity keeps the existing area formula.
	QCOMPARE_EQ(platform->length, 140.0);
	QCOMPARE_EQ(platform->width, 3.0);
	QCOMPARE(platform->Max_Passenger_Volume, static_cast<int>((140.0 * 3.0) / (3.14159 * std::pow(0.8, 2)) * 0.8));
	++platform;
	// Absent platform geometry keeps effective defaults.
	QVERIFY(platform != defaultRun().platforms.end());
	QCOMPARE_EQ(platform->length, 100.0);
	QCOMPARE_EQ(platform->width, 2.5);
	// Passenger journeys are built without filesystem input.
	QCOMPARE(defaultRun().passengers.size(), std::size_t(1));
	// The last accepted build is indexed.
	QVERIFY(!defaultRun().passengers.empty());
	QVERIFY(defaultRun().passengerIndex.covers(defaultRun().passengers));
	QVERIFY(defaultRun().passengerIndex.find(defaultRun().passengers.front().ID) == std::vector<Passenger*>({&defaultRun().passengers.front()}));
	QVERIFY(!defaultRun().platforms.empty());
	QVERIFY(defaultRun().platformIndex.covers(defaultRun().platforms));
	QVERIFY(defaultRun().platformIndex.find(defaultRun().platforms.front().StationID, defaultRun().platforms.front().ID)
		== &defaultRun().platforms.front());
	if (!defaultRun().passengers.front().Journeys.empty()) {
		const Journey& journey = defaultRun().passengers.front().Journeys.front();
		// Passenger windows are sampled in memory.
		QCOMPARE(journey.N_Trips, 2);
		QVERIFY(std::isfinite(journey.Actual_Planned_Departure_Time));
		QVERIFY(journey.Actual_Planned_Departure_Time >= 100.0);
		QVERIFY(journey.Actual_Planned_Departure_Time <= 110.0);
		if (!journey.Trips.empty()) {
			const Trip& first = journey.Trips.front();
			const Trip& last = journey.Trips.back();
			// Passenger legs map to occurrence stop platforms.
			QCOMPARE(first.TrainServiceDescription, "service.native-2");
			QCOMPARE(first.Dep_Station_Platform_ID, "platform.0");
			QCOMPARE(last.Arr_Station_Platform_ID, "platform.2");
			// Multi-leg planned times retain the existing endpoint-only semantics.
			QVERIFY(first.Planned_Departure_Time >= 100);
			QCOMPARE(first.Planned_Arrival_Time, -9999);
			QCOMPARE(last.Planned_Departure_Time, -9999);
			QVERIFY(last.Planned_Arrival_Time >= 130);
		}
		auto unrouted = defaultRun().passengers.front().Journeys.begin();
		++unrouted;
		// Legless canonical journeys remain present.
		QVERIFY(unrouted != defaultRun().passengers.front().Journeys.end());
		QCOMPARE(unrouted->N_Trips, 0);
	}
	// Canonical simulation settings are committed.
	QCOMPARE(initial_variables.name, "native-operations-fixture");
	QCOMPARE(initial_variables.startingSimulationTime, 23400);
	QCOMPARE_EQ(initial_variables.times, 90.0);
	QCOMPARE_EQ(initial_variables.bufferTime, 7.0);
	QCOMPARE_EQ(initial_variables.recoveryTimePercentage, 12.0);
	QCOMPARE(initial_variables.num_OrderLists, 0);
}

void TestOperationsBuilder::commandLineBufferAndRecoveryStay() {
	// A buffer time and a recovery time from the command line replace the ones of the scene. The operations are staged on
	// the infrastructure of the fixture.
	const auto clearOverrides = qScopeGuard([] {
		initial_variables.bufferTimeOverride = false;
		initial_variables.recoveryTimeOverride = false;
	});
	buildInfrastructureAndSignallingFromScene(defaultRun(), completeScene());
	initial_variables.bufferTimeOverride = true;
	initial_variables.bufferTime = 2.5;
	initial_variables.recoveryTimeOverride = true;
	initial_variables.recoveryTimePercentage = 3.5;
	const auto overridden = buildOperationsFromScene(defaultRun(), completeScene(), "scenario.selected");
	// The buffer time and the recovery time of the command line stay.
	QVERIFY(!hasErrors(overridden));
	QCOMPARE_EQ(initial_variables.bufferTime, 2.5);
	QCOMPARE_EQ(initial_variables.recoveryTimePercentage, 3.5);
}

void TestOperationsBuilder::steppedOperatingCodesReachOccurrences() {
	SceneModel stepped = completeScene();
	stepped.services[0].operatingCode = "1723";
	stepped.services[0].hasRepeatCount = true;
	stepped.services[0].repeatCount = 4;
	stepped.services[0].hasOperatingCodeStep = true;
	stepped.services[0].operatingCodeStep = 2;
	const auto steppedInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), stepped);
	const auto steppedOperations = buildOperationsFromScene(defaultRun(), stepped, "scenario.selected");
	// Explicit repeat count and stepped operating codes reach each occurrence.
	QVERIFY(!hasErrors(steppedInfrastructure));
	QVERIFY(!hasErrors(steppedOperations));
	QCOMPARE(defaultRun().numTrains, 4);
	QCOMPARE(defaultRun().trains[0].operatingCode, "1723");
	QCOMPARE(defaultRun().trains[1].operatingCode, "1725");
	QCOMPARE(defaultRun().trains[3].operatingCode, "1729");
}

void TestOperationsBuilder::serviceCapAndPerformanceApplyPerTrain() {
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
	const auto tunedInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), tuned);
	const auto tunedOperations = buildOperationsFromScene(defaultRun(), tuned, "scenario.selected");
	// Service cap and performance apply in precedence order per train.
	QVERIFY(!hasErrors(tunedInfrastructure));
	QVERIFY(!hasErrors(tunedOperations));
	QCOMPARE(defaultRun().numTrains, 4);
	QVERIFY(std::fabs(defaultRun().trains[0].compositionMaximumSpeedMs - 25.0) < 1e-9);
	QVERIFY(std::fabs(defaultRun().trains[0].max_train_speed - (20.0 / 3.6 * 0.5)) < 1e-9);
	QVERIFY(std::fabs(defaultRun().trains[3].max_train_speed - 25.0) < 1e-9);
	// Services sharing a composition do not mutate source rolling-stock data.
	QCOMPARE_EQ(tuned.trainUnits[0].physical.max_speed_ms, 30.0);
	QCOMPARE_EQ(tuned.trainUnits[1].physical.max_speed_ms, 25.0);
	QCOMPARE(tuned.compositions[0].units, std::vector<std::string>({"unit.1", "unit.2"}));
	const double previousPerformance = defaultRun().trains[0].servicePerformancePercent;
	defaultRun().trains[0].servicePerformancePercent = 100.0;
	const double fullForce = defaultRun().trains[0].tractiveEffort(5.0);
	defaultRun().trains[0].servicePerformancePercent = 50.0;
	const double reducedForce = defaultRun().trains[0].tractiveEffort(5.0);
	defaultRun().trains[0].servicePerformancePercent = previousPerformance;
	// Performance scales tractive effort exactly once.
	QVERIFY(fullForce > 0.0);
	QVERIFY(std::fabs(reducedForce - fullForce * 0.5) < 1e-9);
}

void TestOperationsBuilder::occurrenceSelectionOmitsExcludedJourneys() {
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
	const auto selectedInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), selectedScene);
	const auto selectedOperations = buildOperationsFromScene(defaultRun(), selectedScene, "scenario.selected", onlySecond);
	bool selectedJourneyHasTrips = false;
	bool excludedJourneyOmitted = true;
	bool mixedJourneyOmitted = true;
	if (!defaultRun().passengers.empty()) {
		for (const Journey& journey : defaultRun().passengers.front().Journeys) {
			if (journey.ID == "journey.1")
				selectedJourneyHasTrips = journey.N_Trips == 2;
			if (journey.ID == "journey.excluded")
				excludedJourneyOmitted = false;
			if (journey.ID == "journey.mixed")
				mixedJourneyOmitted = false;
		}
	}
	// Occurrence selection omits journeys with excluded passenger legs.
	QVERIFY(!hasErrors(selectedInfrastructure));
	QVERIFY(!hasErrors(selectedOperations));
	QCOMPARE(defaultRun().numTrains, 1);
	QCOMPARE(defaultRun().trains[0].trainDescription, "service.native-2");
	QCOMPARE_EQ(defaultRun().trains[0].EntranceDelay, 5.0);
	QVERIFY(selectedJourneyHasTrips);
	QVERIFY(excludedJourneyOmitted);
	QVERIFY(mixedJourneyOmitted);

	// The rejected build that follows keeps the run of the selection.
	SceneModel reversePassengerLeg = completeScene();
	reversePassengerLeg.passengers[0].journeys[0].originStationId = "station.2";
	reversePassengerLeg.passengers[0].journeys[0].destinationStationId = "station.1";
	reversePassengerLeg.passengers[0].journeys[0].legs = {
		{"leg.reverse", "station.2", "station.1", "service.native", 2}};
	const int previousReverseRegions = defaultRun().numTrains;
	const std::string previousReverseName = initial_variables.name;
	const std::size_t previousReversePassengerCount = defaultRun().passengers.size();
	const auto reversePassengerInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), reversePassengerLeg);
	const auto reversePassengerDiagnostics = buildOperationsFromScene(defaultRun(), reversePassengerLeg, "scenario.base", onlySecond);
	// Native preflight rejects reverse passenger legs before state publication.
	QVERIFY(!hasErrors(reversePassengerInfrastructure));
	QVERIFY(hasCode(reversePassengerDiagnostics, "scene.native.passenger.order"));
	QCOMPARE(defaultRun().numTrains, previousReverseRegions);
	QCOMPARE(initial_variables.name, previousReverseName);
	QCOMPARE(defaultRun().passengers.size(), previousReversePassengerCount);
}

void TestOperationsBuilder::legacyReversePassengerLegIsOmitted() {
	SceneModel legacyReversePassengerLeg = completeScene();
	legacyReversePassengerLeg.passengers[0].journeys[0].originStationId = "station.2";
	legacyReversePassengerLeg.passengers[0].journeys[0].destinationStationId = "station.1";
	legacyReversePassengerLeg.passengers[0].journeys[0].legs = {
		{"leg.reverse", "station.2", "station.1", "service.native", 2}};
	legacyReversePassengerLeg.importReport.push_back({"legacy_root", ""});
	const SceneRunSelection onlySecond{{"service.native", 2}};
	const auto legacyReverseInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), legacyReversePassengerLeg);
	const auto legacyReverseDiagnostics = buildOperationsFromScene(defaultRun(),
		legacyReversePassengerLeg, "scenario.base", onlySecond);
	bool legacyReverseJourneyOmitted = !defaultRun().passengers.empty();
	if (!defaultRun().passengers.empty()) {
		for (const Journey& journey : defaultRun().passengers.front().Journeys)
			legacyReverseJourneyOmitted = legacyReverseJourneyOmitted && journey.ID != "journey.1";
	}
	// Legacy reverse passenger legs are warned and omitted as a whole journey.
	QVERIFY(!hasErrors(legacyReverseInfrastructure));
	QVERIFY(!hasErrors(legacyReverseDiagnostics));
	QVERIFY(hasCodeAndSeverity(legacyReverseDiagnostics, "scene.native.passenger.order", SceneSeverity::Warning));
	QVERIFY(legacyReverseJourneyOmitted);
}

void TestOperationsBuilder::repeatedStopsKeepOrderedPair() {
	const SceneRunSelection onlySecond{{"service.native", 2}};
	SceneModel repeatedPassengerStops = completeScene();
	repeatedPassengerStops.services[0].stops = {
		{"station.2", "platform.2", true, true, 100.0, 110.0, 0.0},
		{"station.1", "platform.1", true, true, 120.0, 130.0, 0.0},
		{"station.2", "platform.2", true, true, 140.0, 150.0, 0.0}};
	buildInfrastructureAndSignallingFromScene(defaultRun(), repeatedPassengerStops);
	QVERIFY2(hasCode(buildOperationsFromScene(defaultRun(), repeatedPassengerStops, "scenario.base", onlySecond),
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
	const auto repeatedInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), repeatedPassengerStops);
	const auto repeatedOperations = buildOperationsFromScene(defaultRun(), repeatedPassengerStops, "scenario.base", onlySecond);
	bool repeatedJourneyStaged = false;
	if (!defaultRun().passengers.empty()) {
		for (const Journey& journey : defaultRun().passengers.front().Journeys) {
			if (journey.ID == "journey.1")
				repeatedJourneyStaged = journey.N_Trips == 1 && !journey.Trips.empty()
					&& journey.Trips.front().Dep_Station_Platform_ID == "platform.1"
					&& journey.Trips.front().Arr_Station_Platform_ID == "platform.2";
		}
	}
	// Native passenger staging follows the repeated-stop ordered pair.
	QVERIFY(!hasErrors(repeatedInfrastructure));
	QVERIFY(!hasErrors(repeatedOperations));
	QVERIFY(resolveScenePassengerLegStops(repeatedPassengerStops.services[0], repeatedJourney.legs[0], repeatedPair));
	QCOMPARE(repeatedPair.originIndex, std::size_t(1));
	QCOMPARE(repeatedPair.destinationIndex, std::size_t(2));
	QCOMPARE_EQ(defaultRun().trains[0].Stations[0].X, 0.0);
	QCOMPARE_EQ(defaultRun().trains[0].Stations[2].X, 2.0);
	QVERIFY(repeatedJourneyStaged);

	// The rejected build that follows keeps the run of the repeated stops and is staged on their infrastructure.
	SceneModel invalidUnselectedPassenger = completeScene();
	invalidUnselectedPassenger.passengers[0].journeys[0].legs[0].serviceId = "service.missing";
	invalidUnselectedPassenger.passengers[0].journeys[0].legs[0].occurrence = 1;
	const int previousRegions = defaultRun().numTrains;
	const std::string previousTrainDescription = defaultRun().trains[0].trainDescription;
	const std::string previousOperationsName = initial_variables.name;
	const std::size_t previousPassengerCount = defaultRun().passengers.size();
	const auto invalidUnselectedPassengerDiagnostics = buildOperationsFromScene(defaultRun(),
		invalidUnselectedPassenger, "scenario.selected", onlySecond);
	// Invalid unselected passenger legs are rejected before native state publication.
	QVERIFY(hasErrors(invalidUnselectedPassengerDiagnostics));
	QVERIFY(hasCode(invalidUnselectedPassengerDiagnostics, "scene.native.passenger.service"));
	QCOMPARE(defaultRun().numTrains, previousRegions);
	QCOMPARE(defaultRun().trains[0].trainDescription, previousTrainDescription);
	QCOMPARE(initial_variables.name, previousOperationsName);
	QCOMPARE(defaultRun().passengers.size(), previousPassengerCount);
}

void TestOperationsBuilder::sparseSelectionExpandsSelectedOccurrence() {
	SceneModel sparsePattern = completeScene();
	sparsePattern.services[0].hasRepeatCount = true;
	sparsePattern.services[0].repeatCount = 1000000000;
	const SceneRunSelection lateOccurrence{{"service.native", 999999999}};
	const auto sparseInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), sparsePattern);
	const auto sparseOperations = buildOperationsFromScene(defaultRun(),
		sparsePattern, "scenario.selected", lateOccurrence);
	// A sparse selection does not expand every occurrence in a large pattern.
	QVERIFY(!hasErrors(sparseInfrastructure));
	QVERIFY(!hasErrors(sparseOperations));
	QCOMPARE(defaultRun().numTrains, 1);
	QCOMPARE(defaultRun().trains[0].trainDescription, "service.native-999999999");
	const SceneRunSelection invalidSelections{{"service.native", 1000000001}, {"service.missing", 1}};
	const auto invalidSelectionOperations = buildOperationsFromScene(defaultRun(),
		sparsePattern, "scenario.selected", invalidSelections);
	// Invalid sparse selections retain native selection diagnostics.
	QVERIFY(hasCode(invalidSelectionOperations, "scene.native.selection.occurrence"));
	QVERIFY(hasCode(invalidSelectionOperations, "scene.native.selection.service"));
}

void TestOperationsBuilder::outOfPatternEntranceDelayIsRejected() {
	SceneModel outOfPattern = completeScene();
	outOfPattern.services[0].hasRepeatCount = true;
	outOfPattern.services[0].repeatCount = 1;
	const auto outOfPatternInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), outOfPattern);
	const auto outOfPatternOperations = buildOperationsFromScene(defaultRun(), outOfPattern, "scenario.selected");
	const SceneRunSelection firstOccurrence{{"service.native", 1}};
	const auto excludedOutOfPatternOperations = buildOperationsFromScene(defaultRun(),
		outOfPattern, "scenario.selected", firstOccurrence);
	// Out-of-pattern entrance delays are rejected for all and selected runs.
	QVERIFY(!hasErrors(outOfPatternInfrastructure));
	QVERIFY(hasCode(outOfPatternOperations, "scene.native.entrance.occurrence"));
	QVERIFY(hasCode(excludedOutOfPatternOperations, "scene.native.entrance.occurrence"));
}

// Entrance delays of the selected scenario that native staging rejects, in a run of all trains and in a run of the
// first occurrence.
struct InvalidEntranceDelay {
	const char* message;
	const char* code;
	void (*change)(SceneModel&);
};
static const InvalidEntranceDelay kInvalidEntranceDelays[] = {
	{"native staging rejects a non-finite entrance delay", "scene.native.entrance.value",
		[](SceneModel& scene) { scene.scenarios[1].entranceDelays[0].delaySeconds = std::numeric_limits<double>::quiet_NaN(); }},
	{"native staging rejects an entrance-delay station outside the service stops", "scene.native.entrance.station",
		[](SceneModel& scene) {
			scene.stations.push_back({"station.other", "Other", true, 3.0, {}});
			scene.scenarios[1].entranceDelays[0].stationId = "station.other";
		}},
	{"native staging rejects an entrance-delay stop without a planned departure", "scene.native.entrance.timetable",
		[](SceneModel& scene) { scene.scenarios[1].entranceDelays[0].stationId = "station.2"; }},
	{"native staging rejects conflicting delays for one service occurrence", "scene.native.entrance.conflict",
		[](SceneModel& scene) {
			scene.scenarios[1].entranceDelays.push_back(
				{"service.native", 2, "station.0", 10.0});
		}},
};

void TestOperationsBuilder::rejectsInvalidEntranceDelay_data() {
	QTest::addColumn<int>("index");
	for (int index = 0; index < static_cast<int>(std::size(kInvalidEntranceDelays)); ++index)
		QTest::newRow(kInvalidEntranceDelays[index].message) << index;
}

void TestOperationsBuilder::rejectsInvalidEntranceDelay() {
	QFETCH(int, index);
	const InvalidEntranceDelay& test = kInvalidEntranceDelays[index];
	SceneModel invalid = completeScene();
	test.change(invalid);
	const SceneRunSelection firstOccurrence{{"service.native", 1}};
	const auto infrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), invalid);
	const auto operations = buildOperationsFromScene(defaultRun(), invalid, "scenario.selected");
	const auto selectedOperations = buildOperationsFromScene(defaultRun(),
		invalid, "scenario.selected", firstOccurrence);
	QVERIFY(!hasErrors(infrastructure));
	QVERIFY(hasCode(operations, test.code));
	QVERIFY(hasCode(selectedOperations, test.code));
}

void TestOperationsBuilder::sortsNonFiniteEventTimesLast() {
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
	// Train event sorting terminates and places non-finite times last.
	QCOMPARE(unorderedEvents.size(), std::size_t(4));
	QCOMPARE_EQ(eventIt->Time, 1.0);
	QCOMPARE_EQ(std::next(eventIt)->Time, 2.0);
	QVERIFY(std::isnan(std::next(eventIt, 2)->Time));
	QVERIFY(std::isnan(std::next(eventIt, 3)->Time));
	QCOMPARE(std::next(eventIt, 2)->trainDescription, "nan-first");
	QCOMPARE(std::next(eventIt, 3)->trainDescription, "nan-last");
}

void TestOperationsBuilder::invalidScenarioKeepsRuntimeState() {
	// The runtime state of the complete fixture, which the invalid build keeps.
	const SceneModel scene = throughScene();
	QVERIFY2(!hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), scene)),
		"M2 infrastructure builder accepts the complete fixture");
	QVERIFY2(!hasErrors(buildOperationsFromScene(defaultRun(), scene, "scenario.selected")),
		"M3 operations builder accepts the complete fixture");
	const std::string previousDescription = defaultRun().trains[0].trainDescription;
	const std::size_t previousIncidentCount = defaultRun().incidents.size();
	const std::string previousName = initial_variables.name;
	const auto invalidDiagnostics = buildOperationsFromScene(defaultRun(), scene, "scenario.missing");
	QVERIFY2(hasErrors(invalidDiagnostics), "an invalid selected scenario is rejected");
	// Invalid operations build preserves prior runtime state.
	QCOMPARE(defaultRun().trains[0].trainDescription, previousDescription);
	QCOMPARE(defaultRun().incidents.size(), previousIncidentCount);
	QCOMPARE(initial_variables.name, previousName);
}

void TestOperationsBuilder::reversedRouteResolvesStopNodes() {
	SceneModel reversed = completeScene();
	reversed.routes[0].blocks = {"block.2", "block.1", "block.0"};
	std::reverse(reversed.services[0].stops.begin(), reversed.services[0].stops.end());
	reversed.passengers.clear();
	const auto reversedInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), reversed);
	const auto reversedOperations = buildOperationsFromScene(defaultRun(), reversed, "scenario.selected");
	// Reversed routes resolve stop nodes without legacy node-list storage.
	QVERIFY(!hasErrors(reversedInfrastructure));
	QVERIFY(!hasErrors(reversedOperations));
}

void TestOperationsBuilder::resolvesPlatformsInRouteOrder() {
	SceneModel ordered = completeScene();
	ordered.passengers.clear();
	ordered.services[0].stops[1].platformId.clear();
	buildInfrastructureAndSignallingFromScene(defaultRun(), ordered);
	// A unique reachable platform resolves without an explicit selection.
	QVERIFY(!hasErrors(buildOperationsFromScene(defaultRun(), ordered, "scenario.base")));
	QCOMPARE(defaultRun().trains[0].Stations[1].stationPlatformId, "platform.1");
	ordered.stations[1].platforms.push_back({"platform.other", {"node.3"}});
	buildInfrastructureAndSignallingFromScene(defaultRun(), ordered);
	QVERIFY2(hasCode(buildOperationsFromScene(defaultRun(), ordered, "scenario.base"),
				 "scene.native.ref.platform.ambiguous"),
		"multiple reachable platforms require an explicit choice");
	ordered.services[0].stops[1].platformId = "platform.other";
	QVERIFY2(hasCode(buildOperationsFromScene(defaultRun(), ordered, "scenario.base"), "scene.native.ref.stop.order"),
		"an explicit late platform cannot be followed by an earlier station");
}

// The number that the walking time of a transfer starts from: a stopped train has the number of its stop, and of its last stop at a
// station that it calls at twice.
void TestOperationsBuilder::stopsKeepPlatformNumbers() {
	SceneModel scene = completeScene();
	scene.passengers.clear();
	scene.stations[0].platforms[0].nodeIds = {"node.3"};
	scene.stations[1].platforms[0].id = "Platform_4";
	scene.stations[2].platforms = {{"Platform_3", {"node.0"}}, {"Platform_5", {"node.2"}}};
	scene.services[0].stops = {
		{"station.2", "Platform_3", true, true, 100.0, 110.0, 0.0},
		{"station.1", "Platform_4", true, true, 120.0, 130.0, 0.0},
		{"station.2", "Platform_5", true, true, 140.0, 150.0, 0.0}};
	QVERIFY(!hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), scene)));
	QVERIFY(!hasErrors(buildOperationsFromScene(defaultRun(), scene, "scenario.base")));
	Train train = defaultRun().trains[0];
	QCOMPARE(train.stationPlatformNumber[0], 3);
	QCOMPARE(train.stationPlatformNumber[1], 4);
	QCOMPARE(train.stationPlatformNumber[2], 5);

	Arc arrival;
	arrival.endNode = train.Stations[1];
	train.serviceStopStep(1, arrival, 0, false);
	QCOMPARE(train.currentServiceStopPlatformNumber, 4);
	arrival.endNode = train.Stations[0];
	train.serviceStopStep(1, arrival, 0, false);
	QCOMPARE(train.currentServiceStopPlatformNumber, 5);
}

void TestOperationsBuilder::routeExternalRowsKeepPlannedTimes() {
	SceneModel routeExternal = completeScene();
	routeExternal.routes[0].blocks = {"block.0", "block.1"};
	routeExternal.stations.push_back(
		{"station.3", "Three", true, 3.0, {{"platform.3", {"node.3"}}}});
	routeExternal.services[0].stops.push_back(
		{"station.3", {}, true, true, -120.0, -60.0, 2.0});
	routeExternal.passengers.clear();
	const auto routeExternalInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), routeExternal);
	const auto routeExternalOperations = buildOperationsFromScene(defaultRun(), routeExternal, "scenario.base");
	// Pre-entry and post-exit timetable rows remain valid without an invented platform.
	QVERIFY(!hasErrors(routeExternalInfrastructure));
	QVERIFY(!hasErrors(routeExternalOperations));
	// Route-external timetable rows preserve negative relative planned times.
	QCOMPARE(defaultRun().trains[0].numStations, 4);
	QCOMPARE(defaultRun().trains[0].Stations[3].stationPlatformId, "");
	QCOMPARE_EQ(defaultRun().trains[0].ScheduledArrivals[3], -120.0);
	QCOMPARE_EQ(defaultRun().trains[0].ScheduledDepartures[3], -60.0);
}

void TestOperationsBuilder::shortHorizonOmitsLateJourneys() {
	SceneModel shortHorizon = completeScene();
	shortHorizon.settings.durationSeconds = 20.0;
	const auto shortInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), shortHorizon);
	const auto shortOperations = buildOperationsFromScene(defaultRun(), shortHorizon, "scenario.base");
	// Passenger journeys beyond a shortened run horizon are omitted whole.
	QVERIFY(!hasErrors(shortInfrastructure));
	QVERIFY(!hasErrors(shortOperations));
	QCOMPARE(defaultRun().numTrains, 1);
	QCOMPARE(defaultRun().passengers.size(), std::size_t(1));
	QCOMPARE(defaultRun().passengers.front().Journeys.size(), std::size_t(1));
	QCOMPARE(defaultRun().passengers.front().Journeys.front().ID, "journey.unrouted");
	QCOMPARE(defaultRun().passengers.front().Journeys.front().N_Trips, 0);
}

void TestOperationsBuilder::durationOverrideSizesRuntimeVectors() {
	const auto clearOverride = qScopeGuard([] { initial_variables.durationOverride = false; });
	initial_variables.times = 120.0;
	initial_variables.durationOverride = true;
	SceneModel extendedHorizon = completeScene();
	const auto extendedInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), extendedHorizon);
	const auto extendedOperations = buildOperationsFromScene(defaultRun(), extendedHorizon, "scenario.selected");
	// Duration override sizes repeated services and runtime vectors to the effective horizon.
	QVERIFY(!hasErrors(extendedInfrastructure));
	QVERIFY(!hasErrors(extendedOperations));
	QCOMPARE_EQ(initial_variables.times, 120.0);
	QCOMPARE(defaultRun().numTrains, 4);
	QCOMPARE(defaultRun().trains[0].instant_train_speed.size(), std::size_t(120));
}

// The complete fixture with one train of strong stock that enters at 0 s and runs through without a stop, in a run of
// 600 s.
static SceneModel trajectoryPerformanceScene(double performancePercent) {
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
	trajectoryPerformance.services[0].performancePercent = performancePercent;
	return trajectoryPerformance;
}

void TestOperationsBuilder::reducedPerformanceDelaysCompletion() {
	SceneModel trajectoryPerformance = trajectoryPerformanceScene(100.0);
	const auto trajectoryFullInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), trajectoryPerformance);
	const auto trajectoryFullOperations = buildOperationsFromScene(defaultRun(), trajectoryPerformance, "scenario.base");
	// Runs the first train through the live movement step that the dispatch loop uses.
	const auto simulateFirstTrainMovement = [&]() {
		for (int t = 0; t < initial_variables.times; t++) {
			defaultRun().trains[0].trajectoryComputationIncludingMovingBlock(t, signalCode1, signalCode2, signalCode3);
			defaultRun().trains[0].recordEarliestActiveTrajectoryIndex(t);
		}
	};
	int fullPerformanceEnd = -1;
	if (!hasErrors(trajectoryFullInfrastructure) && !hasErrors(trajectoryFullOperations) && defaultRun().numTrains == 1) {
		simulateFirstTrainMovement();
		fullPerformanceEnd = defaultRun().trains[0].End_Time;
	}
	trajectoryPerformance.services[0].performancePercent = 50.0;
	const auto trajectoryReducedInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), trajectoryPerformance);
	const auto trajectoryReducedOperations = buildOperationsFromScene(defaultRun(), trajectoryPerformance, "scenario.base");
	int reducedPerformanceEnd = -1;
	if (!hasErrors(trajectoryReducedInfrastructure) && !hasErrors(trajectoryReducedOperations) && defaultRun().numTrains == 1) {
		simulateFirstTrainMovement();
		reducedPerformanceEnd = defaultRun().trains[0].End_Time;
	}
	// Reduced performance delays native route completion.
	QVERIFY(fullPerformanceEnd >= 0);
	QVERIFY(reducedPerformanceEnd > fullPerformanceEnd);
}

void TestOperationsBuilder::zeroStopThroughServiceStaysSafe() {
	const SceneModel trajectoryPerformance = trajectoryPerformanceScene(50.0);
	const auto safetyInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), trajectoryPerformance);
	const auto safetyOperations = buildOperationsFromScene(defaultRun(), trajectoryPerformance, "scenario.base");
	// Runtime safety fixture builds one zero-stop through service.
	QVERIFY(!hasErrors(safetyInfrastructure));
	QVERIFY(!hasErrors(safetyOperations));
	QCOMPARE(defaultRun().numTrains, 1);
	Train& through = defaultRun().trains[0];
	through.departure_time = 0.0;
	through.checkTrainArrDep(0);
	Stations finalProbe;
	finalProbe.stationName = "Final_Station";
	finalProbe.totalArrivalDelay = finalProbe.Max_TotalDelay = finalProbe.Max_Cons_Delay = 42.0;
	calculateDelayStatsAtStation(defaultRun(), finalProbe);
	// Zero-stop through service has unavailable final-station statistics.
	QCOMPARE(through.numStations, 0);
	QCOMPARE(finalProbe.N_Stopped_Trains, 0);
	QCOMPARE_EQ(finalProbe.Av_Arrival_Delay, -1.0);
	QCOMPARE_EQ(finalProbe.Std_Arrival_Delay, -1.0);
	QCOMPARE_EQ(finalProbe.totalArrivalDelay, 0.0);
	QCOMPARE_EQ(finalProbe.Max_TotalDelay, -1.0);
	QCOMPARE_EQ(finalProbe.Max_Cons_Delay, -1.0);
	finalProbe.totalArrivalDelay = finalProbe.Max_TotalDelay = finalProbe.Max_Cons_Delay = 42.0;
	calculatePosAndNegDelayStatsAtStation(defaultRun(), finalProbe);
	QCOMPARE(finalProbe.N_Stopped_Trains, 0);
	QCOMPARE_EQ(finalProbe.Av_Arrival_Delay, -1.0);
	QCOMPARE_EQ(finalProbe.Std_Arrival_Delay, -1.0);
	QCOMPARE_EQ(finalProbe.totalArrivalDelay, 0.0);
	QCOMPARE_EQ(finalProbe.Max_TotalDelay, -1.0);
	QCOMPARE_EQ(finalProbe.Max_Cons_Delay, -1.0);

	through.trajectoryComputationIncludingMovingBlock(0, signalCode1, signalCode2, signalCode3);
	// Time zero initializes the live trajectory without entering or advancing.
	QVERIFY(!through.CanEnter);
	QCOMPARE_EQ(through.instant_train_speed[0], 0.0);
	QCOMPARE_EQ(through.instant_spatial_position[0], through.Start_Node_X * 1000);

	Route& route = defaultRun().infra.routes[through.indexOfRoute];
	defaultRun().signallingDelay = 0.0;
	through.CanEnter = true;
	through.OutOfSimulation = false;
	const int nextHead = route.N_Block_Sections - 2;
	const Section& nextSection = route.sequence_of_block_sections[nextHead];
	through.instant_spatial_position[1] =
		(nextSection.start_node.X + nextSection.end_node.X) * 500.0;
	defaultRun().infra.stationBoundaries.clear();
	defaultRun().infra.stationBoundaries.emplace_back(&route.sequence_of_block_sections[nextHead + 1],
		route.reversed_direction, nullptr);
	defaultRun().blocksOccupied.clear();
	defaultRun().blocksConnected.clear();
	protectStationAreas(defaultRun(), 1);
	QVERIFY2(std::find(defaultRun().blocksConnected.begin(), defaultRun().blocksConnected.end(),
				 route.sequence_of_block_sections[nextHead + 1].ID)
			!= defaultRun().blocksConnected.end(),
		"zero-stop train keeps station-boundary route protection without stop indexing");

	// One section stored past N_Block_Sections: the lookahead must not read it.
	route.sequence_of_block_sections.emplace_back();
	{
		Section& outOfRoute = route.sequence_of_block_sections[route.N_Block_Sections];
		outOfRoute.ID = "out.of.route";
		const Section& tail = route.sequence_of_block_sections[route.N_Block_Sections - 1];
		through.instant_spatial_position[1] = (tail.start_node.X + tail.end_node.X) * 500.0;
		defaultRun().infra.stationBoundaries.clear();
		defaultRun().infra.stationBoundaries.emplace_back(&outOfRoute, route.reversed_direction, nullptr);
		defaultRun().blocksOccupied.clear();
		defaultRun().blocksConnected.clear();
		protectStationAreas(defaultRun(), 1);
		QVERIFY2(std::find(defaultRun().blocksConnected.begin(), defaultRun().blocksConnected.end(), outOfRoute.ID)
				== defaultRun().blocksConnected.end(),
			"station lookahead ignores sections beyond the actual route tail");
		defaultRun().infra.stationBoundaries.clear();
	}
	route.sequence_of_block_sections.pop_back();

	// The train ends at Asd and so does a train that has left. Nothing ahead is occupied, so the entrance stays free.
	Regional terminating;
	terminating.ID = through.ID + 1;
	terminating.type = through.type;
	terminating.OutOfSimulation = true;
	terminating.Stations.resize(1);
	terminating.Stations[0].stationName = "Asd";
	terminating.numStations = 1;
	const std::vector<Node> savedStations = through.Stations;
	through.Stations = terminating.Stations;
	through.numStations = 1;
	const Section& beforeEntrance = route.sequence_of_block_sections[nextHead];
	through.instant_spatial_position[1] = (beforeEntrance.start_node.X + beforeEntrance.end_node.X) * 500.0;
	defaultRun().trains.push_back(terminating);
	defaultRun().numTrains = 2;
	defaultRun().infra.stationBoundaries.emplace_back(&route.sequence_of_block_sections[nextHead + 1],
		route.reversed_direction, nullptr);
	defaultRun().blocksOccupied.clear();
	defaultRun().blocksConnected.clear();
	protectStationAreas(defaultRun(), 1);
	QVERIFY2(std::find(defaultRun().blocksOccupied.begin(), defaultRun().blocksOccupied.end(),
				 route.sequence_of_block_sections[nextHead + 1].ID)
			== defaultRun().blocksOccupied.end(),
		"a train that ends at Asd where a train that has left also ended does not occupy a free station entrance");
	defaultRun().infra.stationBoundaries.clear();
	defaultRun().trains.pop_back();
	defaultRun().numTrains = 1;
	defaultRun().trains[0].Stations = savedStations;
	defaultRun().trains[0].numStations = 0;
}

void TestOperationsBuilder::stationStatisticsOfSmallPopulations() {
	SceneModel statisticsScene = completeScene();
	statisticsScene.services[0].hasRepeatCount = true;
	statisticsScene.services[0].repeatCount = 3;
	const auto statisticsInfrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), statisticsScene);
	const auto statisticsOperations = buildOperationsFromScene(defaultRun(), statisticsScene, "scenario.base");
	// Station statistics fixture builds three trains.
	QVERIFY(!hasErrors(statisticsInfrastructure));
	QVERIFY(!hasErrors(statisticsOperations));
	QCOMPARE(defaultRun().numTrains, 3);
	Stations statistics;
	statistics.stationName = "Final_Station";
	for (int trainIndex = 0; trainIndex < 3; ++trainIndex) {
		defaultRun().trains[trainIndex].numStations = 1;
		defaultRun().trains[trainIndex].StationArrivals[0] = -1;
		defaultRun().trains[trainIndex].ScheduledArrivals[0] = 0;
		defaultRun().trains[trainIndex].StationDelay[0] = -1;
		defaultRun().trains[trainIndex].StationConsecDelay[0] = -1;
	}

	defaultRun().numTrains = 0;
	calculateDelayStatsAtStation(defaultRun(), statistics);
	Compute_Input_Delays(defaultRun());
	// Empty delay populations keep the unavailable representation.
	QCOMPARE(statistics.N_Stopped_Trains, 0);
	QCOMPARE_EQ(statistics.Av_Arrival_Delay, -1.0);
	QCOMPARE_EQ(statistics.Std_Arrival_Delay, -1.0);
	QCOMPARE_EQ(statistics.totalArrivalDelay, 0.0);
	QCOMPARE_EQ(defaultRun().totalInputDelays.Av_Arrival_Delay, -1.0);
	QCOMPARE_EQ(defaultRun().entranceInputDelays.Std_Arrival_Delay, -1.0);
	QCOMPARE_EQ(defaultRun().disturbanceInput.Perc_Delayed_T, -1.0);

	defaultRun().numTrains = 1;
	defaultRun().trains[0].StationArrivals[0] = 100;
	defaultRun().trains[0].StationDelay[0] = 12;
	defaultRun().trains[0].StationConsecDelay[0] = 3;
	calculateDelayStatsAtStation(defaultRun(), statistics);
	// One positive station-delay sample has zero deviation.
	QCOMPARE_EQ(statistics.Av_Arrival_Delay, 12.0);
	QCOMPARE_EQ(statistics.Std_Arrival_Delay, 0.0);
	QCOMPARE_EQ(statistics.totalArrivalDelay, 12.0);
	QCOMPARE_EQ(statistics.Max_TotalDelay, 12.0);
	QVERIFY(std::isfinite(statistics.Perc_Delayed_T));

	defaultRun().trains[0].StationDelay[0] = -1;
	calculatePosAndNegDelayStatsAtStation(defaultRun(), statistics);
	// A one-second early arrival is not confused with the delay sentinel.
	QCOMPARE(statistics.N_Stopped_Trains, 1);
	QCOMPARE_EQ(statistics.Av_Arrival_Delay, -1.0);
	QCOMPARE_EQ(statistics.Std_Arrival_Delay, 0.0);
	QCOMPARE_EQ(statistics.totalArrivalDelay, -1.0);

	defaultRun().trains[0].StationDelay[0] = -12;
	defaultRun().trains[0].StationConsecDelay[0] = -3;
	calculatePosAndNegDelayStatsAtStation(defaultRun(), statistics);
	// One negative station-delay sample remains valid with zero deviation.
	QCOMPARE_EQ(statistics.Av_Arrival_Delay, -12.0);
	QCOMPARE_EQ(statistics.Std_Arrival_Delay, 0.0);
	QCOMPARE_EQ(statistics.Max_TotalDelay, -12.0);
	QCOMPARE_EQ(statistics.Max_Cons_Delay, -3.0);

	const double positiveDelays[] = {10, 20, 0};
	const double mixedDelays[] = {-10, 0, 20};
	for (int trainIndex = 0; trainIndex < 3; ++trainIndex) {
		defaultRun().trains[trainIndex].StationArrivals[0] = 100;
		defaultRun().trains[trainIndex].StationDelay[0] = positiveDelays[trainIndex];
		defaultRun().trains[trainIndex].StationConsecDelay[0] = trainIndex;
	}
	defaultRun().numTrains = 3;
	calculateDelayStatsAtStation(defaultRun(), statistics);
	// Multiple positive delays retain the delayed-train sample denominator.
	QVERIFY(std::abs(statistics.Av_Arrival_Delay - 15) < 1e-9);
	QVERIFY(std::abs(statistics.Std_Arrival_Delay - std::sqrt(50.0)) < 1e-9);
	QCOMPARE(statistics.N_Stopped_Trains, 3);
	QCOMPARE(statistics.N_Delayed_Arr, 2);
	QVERIFY(std::isfinite(statistics.Perc_Delayed_T));

	for (int trainIndex = 0; trainIndex < 3; ++trainIndex)
		defaultRun().trains[trainIndex].StationDelay[0] = mixedDelays[trainIndex];
	calculatePosAndNegDelayStatsAtStation(defaultRun(), statistics);
	// Mixed early and late arrivals use the all-sample denominator.
	QVERIFY(std::abs(statistics.Av_Arrival_Delay - (10.0 / 3.0)) < 1e-9);
	QVERIFY(std::abs(statistics.Std_Arrival_Delay - std::sqrt(700.0 / 3.0)) < 1e-9);
	QCOMPARE_EQ(statistics.Max_TotalDelay, 20.0);
	QVERIFY(std::isfinite(statistics.totalArrivalDelay));

	const double totalInputs[] = {0, 10, 20};
	const double entranceInputs[] = {0, 4, 8};
	for (int trainIndex = 0; trainIndex < 3; ++trainIndex) {
		defaultRun().trains[trainIndex].TotalInputDelays = totalInputs[trainIndex];
		defaultRun().trains[trainIndex].EntranceDelay = entranceInputs[trainIndex];
	}
	Compute_Input_Delays(defaultRun());
	// Multiple input-delay populations remain finite.
	QVERIFY(std::abs(defaultRun().totalInputDelays.Av_Arrival_Delay - 15) < 1e-9);
	QVERIFY(std::abs(defaultRun().totalInputDelays.Std_Arrival_Delay - std::sqrt(50.0)) < 1e-9);
	QVERIFY(std::abs(defaultRun().entranceInputDelays.Av_Arrival_Delay - 6) < 1e-9);
	QVERIFY(std::abs(defaultRun().disturbanceInput.Av_Arrival_Delay - 9) < 1e-9);
	QVERIFY(std::isfinite(defaultRun().entranceInputDelays.Std_Arrival_Delay));
	QVERIFY(std::isfinite(defaultRun().disturbanceInput.Std_Arrival_Delay));

	defaultRun().numTrains = 1;
	defaultRun().trains[0].TotalInputDelays = 0;
	defaultRun().trains[0].EntranceDelay = 0;
	Compute_Input_Delays(defaultRun());
	// One all-punctual input population reports finite zeros.
	QCOMPARE_EQ(defaultRun().totalInputDelays.Av_Arrival_Delay, 0.0);
	QCOMPARE_EQ(defaultRun().totalInputDelays.Std_Arrival_Delay, 0.0);
	QCOMPARE_EQ(defaultRun().entranceInputDelays.Std_Arrival_Delay, 0.0);
	QCOMPARE_EQ(defaultRun().disturbanceInput.Std_Arrival_Delay, 0.0);

	QTemporaryDir statisticsOutput;
	defaultRun().infra.numStations = 1;
	defaultRun().finalStation = statistics;
	Print_Station_Delay_Stats(defaultRun(), statisticsOutput.path().toStdString(), "pos");
	std::ifstream exported(statisticsOutput.filePath("Stats_Stations.txt").toStdString());
	bool finiteExport = exported.good();
	int finalStationRows = 0;
	for (std::string token; exported >> token;) {
		char* end = nullptr;
		const double value = std::strtod(token.c_str(), &end);
		if (end != token.c_str() && *end == '\0' && !std::isfinite(value))
			finiteExport = false;
		finalStationRows += token == "Final_Station" ? 1 : 0;
	}
	exported.close();
	QVERIFY2(finiteExport, "one-station statistics export contains no non-finite values");
	// The export names the fictitious station of the final-station statistics.
	QCOMPARE(finalStationRows, 1);

	// The statistics take the arrival of the timetable point, the one the timetable results report.
	Train& train = defaultRun().trains[0];
	TrainEvent point;
	point.SuccessorID = train.stationNameForArrivalStats(0);
	point.Time = 94;
	train.TimetablePoints.clear();
	train.TimetablePoints.push_back(point);
	train.StationArrivals[0] = 90; // recorded while the train was still braking into the station
	train.Determine_Actual_Station_Arrivals();
	// The delay statistics take the arrival of the timetable point.
	QCOMPARE_EQ(train.StationArrivals[0], 94.0);
	train.TimetablePoints.front().Time = -10000;
	train.Determine_Actual_Station_Arrivals();
	// A stop whose timetable point has no arrival has no arrival.
	QCOMPARE_EQ(train.StationArrivals[0], -1.0);
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
	// Each call at a station takes its own timetable point.
	QCOMPARE_EQ(train.StationArrivals[0], 94.0);
	QCOMPARE_EQ(train.StationArrivals[1], 300.0);
	train.TimetablePoints.clear();
	train.StationArrivalNames[0] = train.StationArrivalNames[1] = "None";
	train.numStations = singleStop;

	// A stop without a planned arrival has no arrival delay.
	train.StationArrivals[0] = 100;
	train.ScheduledArrivals[0] = -1;
	train.StationDelay[0] = -1;
	train.computeArrivalDelaysAtStations();
	QCOMPARE_EQ(train.StationDelay[0], -1.0);
	train.Compute_Pos_And_Neg_Arrival_Delays_At_Stations();
	calculatePosAndNegDelayStatsAtStation(defaultRun(), statistics);
	// A stop without a planned arrival is not in the signed statistics.
	QCOMPARE_EQ(train.StationDelay[0], -1.0);
	QCOMPARE(statistics.N_Stopped_Trains, 0);

	// A train that did not arrive has no arrival delay and is not counted as punctual.
	train.StationArrivals[0] = -1;
	train.ScheduledArrivals[0] = 90;
	train.computeArrivalDelaysAtStations();
	// A train that did not arrive has no arrival delay.
	QCOMPARE_EQ(train.StationDelay[0], -1.0);
	QCOMPARE_EQ(train.StationArrivals[0], -1.0);
	train.Compute_Pos_And_Neg_Arrival_Delays_At_Stations();
	calculatePosAndNegDelayStatsAtStation(defaultRun(), statistics);
	// A train that did not arrive is not in the signed statistics.
	QCOMPARE_EQ(train.StationDelay[0], -1.0);
	QCOMPARE(statistics.N_Stopped_Trains, 0);

	train.StationArrivals[0] = 100;
	train.Compute_Pos_And_Neg_Arrival_Delays_At_Stations();
	calculatePosAndNegDelayStatsAtStation(defaultRun(), statistics);
	// A stop with a planned arrival is in the signed statistics.
	QCOMPARE_EQ(train.StationDelay[0], 10.0);
	QCOMPARE(statistics.N_Stopped_Trains, 1);
	QCOMPARE_EQ(statistics.Av_Arrival_Delay, 10.0);
}

void TestOperationsBuilder::passengerRatesAndDwellTerms() {
	QVERIFY2(near(passengerOccupancyRatio(0, 600), 0.0), "occupancy ratio at 0 percent");
	QVERIFY2(near(passengerOccupancyRatio(300, 600), 0.5), "occupancy ratio at 50 percent");
	QVERIFY2(near(passengerOccupancyRatio(480, 600), 0.8), "occupancy ratio at 80 percent");
	QVERIFY2(near(passengerOccupancyRatio(600, 600), 1.0), "occupancy ratio at 100 percent");
	// Occupancy ratio of an unknown capacity is zero.
	QVERIFY(near(passengerOccupancyRatio(7, 0), 0.0));
	QVERIFY(near(passengerOccupancyRatio(7, -1), 0.0));
	// Capacity of a train without wagons.
	QCOMPARE(trainPassengerCapacity(0.0), 300);
	// Capacity of a train with one wagon.
	QCOMPARE(trainPassengerCapacity(1.0), 600);
	// Capacity of a train with three wagons.
	QCOMPARE(trainPassengerCapacity(3.0), 1200);

	Train train;
	train.run_ = &defaultRun();
	train.number_of_wagons = 3.0;
	train.MAX_OnBoard_Passengers = trainPassengerCapacity(train.number_of_wagons);
	const float beta1 = 0.32f, beta3 = 0.564f, beta7 = 0.562f;

	train.Current_OnBoard_Passengers = 600; // 50 percent of 1200
	const double lowOnboard = seededDwellTime(train, 0.5);
	QVERIFY2(near(lowOnboard, seededDwellTime(train, 0.5)), "equal seeds give equal dwell times");
	train.Current_OnBoard_Passengers = 960; // 80 percent of 1200
	const double highOnboard = seededDwellTime(train, 0.5);
	QVERIFY2(near(highOnboard - lowOnboard, beta7),
		"on-board congestion above 0.7 adds its term to the dwell time");

	train.Current_OnBoard_Passengers = 0;
	const double lowPlatform = seededDwellTime(train, 0.6);
	const double highPlatform = seededDwellTime(train, 0.7);
	QVERIFY2(near(highPlatform - lowPlatform, static_cast<double>(beta1) + beta3),
		"platform congestion above 0.65 adds its terms to the dwell time");
}

// Passengers and platforms are found by ID in list order.
void TestOperationsBuilder::indexesFindPassengersAndPlatforms() {
	std::list<Passenger> passengers(4);
	std::vector<Passenger*> order;
	for (Passenger& passenger : passengers)
		order.push_back(&passenger);
	const char* const passengerIds[] = {"A", "B", "A", "C"};
	for (std::size_t index = 0; index < order.size(); ++index)
		order[index]->ID = passengerIds[index];

	PassengerIndex passengerIndex;
	// A passenger index that was not built describes no list.
	QVERIFY(!passengerIndex.covers(passengers));
	QVERIFY(passengerIndex.find("A").empty());
	passengerIndex.build(passengers);
	QVERIFY2(passengerIndex.covers(passengers), "a passenger index describes the list it was built for");
	QVERIFY2(passengerIndex.find("A") == std::vector<Passenger*>({order[0], order[2]}),
		"a repeated passenger ID is found in list order");
	// A passenger ID is found once and an unknown one not at all.
	QVERIFY(passengerIndex.find("B") == std::vector<Passenger*>({order[1]}));
	QVERIFY(passengerIndex.find("Z").empty());
	const std::list<Passenger> passengerCopy = passengers;
	QVERIFY2(!passengerIndex.covers(passengerCopy), "a passenger index does not describe a copy of its list");
	passengers.emplace_back();
	QVERIFY2(!passengerIndex.covers(passengers), "a passenger index does not describe a list that grew");
	passengerIndex.clear();
	// A cleared passenger index describes no list.
	QVERIFY(!passengerIndex.covers(passengers));
	QVERIFY(passengerIndex.find("A").empty());

	std::list<StationPlatform> platforms(4);
	std::vector<const StationPlatform*> platformOrder;
	for (const StationPlatform& platform : platforms)
		platformOrder.push_back(&platform);
	const char* const platformStations[] = {"S1", "S1", "S1", "S2"};
	const char* const platformIds[] = {"P1", "P2", "P1", "P1"};
	{
		std::size_t index = 0;
		for (StationPlatform& platform : platforms) {
			platform.StationID = platformStations[index];
			platform.ID = platformIds[index];
			++index;
		}
	}
	PlatformIndex platformIndex;
	// A platform index that was not built describes no list.
	QVERIFY(!platformIndex.covers(platforms));
	QVERIFY(platformIndex.find("S1", "P1") == nullptr);
	platformIndex.build(platforms);
	QVERIFY2(platformIndex.covers(platforms), "a platform index describes the list it was built for");
	// A platform is found by station and platform ID, the first one when the IDs repeat.
	QVERIFY(platformIndex.find("S1", "P1") == platformOrder[0]);
	QVERIFY(platformIndex.find("S1", "P2") == platformOrder[1]);
	QVERIFY(platformIndex.find("S2", "P1") == platformOrder[3]);
	// An unknown platform is not found.
	QVERIFY(platformIndex.find("S2", "P2") == nullptr);
	QVERIFY(platformIndex.find("S3", "P1") == nullptr);
	platforms.emplace_back();
	QVERIFY2(!platformIndex.covers(platforms), "a platform index does not describe a list that grew");
	platformIndex.clear();
	// A cleared platform index describes no list.
	QVERIFY(!platformIndex.covers(platforms));
	QVERIFY(platformIndex.find("S1", "P1") == nullptr);
}

// Boarding takes passengers and platforms from the shared indexes for the lists those describe and from an index of its
// own for any other list.
void TestOperationsBuilder::boardingLooksUpMatchingIndex() {
	// Boarding at platform S1 P1 of the lists of the run, where the waiting list names Pb before Pa.
	std::list<Passenger> savedPassengers;
	savedPassengers.swap(defaultRun().passengers);
	std::list<StationPlatform> savedPlatforms;
	savedPlatforms.swap(defaultRun().platforms);
	const PassengerIndex savedPassengerIndex = defaultRun().passengerIndex;
	const PlatformIndex savedPlatformIndex = defaultRun().platformIndex;
	const bool savedPaxGui = defaultRun().settings.PAX_GUI;
	const auto restore = qScopeGuard([&] {
		eglog::setSink({});
		defaultRun().settings.PAX_GUI = savedPaxGui;
		defaultRun().passengers.swap(savedPassengers);
		defaultRun().platforms.swap(savedPlatforms);
		defaultRun().passengerIndex = savedPassengerIndex;
		defaultRun().platformIndex = savedPlatformIndex;
	});
	defaultRun().settings.PAX_GUI = true;
	eglog::setSink([](eglog::Category, eglog::Level, const std::string&) {});

	const auto waiting = [](const std::string& id) {
		Passenger passenger;
		passenger.ID = id;
		passenger.CurrentStatus = "OnPlatform";
		passenger.Current_Train_To_Wait = "T1";
		passenger.Current_WaitingStationID = "S1";
		return passenger;
	};
	const auto boardedIds = [](const std::list<Passenger>& everyone) {
		std::string boarded;
		for (const Passenger& passenger : everyone)
			if (passenger.CurrentStatus == "OnBoard")
				boarded += passenger.ID + " ";
		return boarded;
	};
	Train train;
	train.run_ = &defaultRun();
	train.StoppedForServiceStop = true;
	train.trainDescription = "T1";
	train.CurrentServiceStop = "S1";
	train.CurrentServiceStopPlatform = "P1";
	const auto board = [&train](int capacity, std::list<Passenger>& everyone, const std::list<StationPlatform>& everyPlatform) {
		train.MAX_OnBoard_Passengers = capacity;
		train.Current_OnBoard_Passengers = 0;
		Simulate_Train_Passenger_Interactions(defaultRun(), 10, 0, train, everyone, everyPlatform);
	};
	StationPlatform stop;
	stop.StationID = "S1";
	stop.ID = "P1";
	stop.Current_List_Pax_On_Platform = {{"Pb", 0.0}, {"Pa", 0.0}};
	defaultRun().platforms.push_back(stop);
	defaultRun().platformIndex.build(defaultRun().platforms);

	defaultRun().passengers = {waiting("Pa"), waiting("Pb"), waiting("Pa")};
	defaultRun().passengerIndex.build(defaultRun().passengers);
	board(1, defaultRun().passengers, defaultRun().platforms);
	// Passengers board in the order of the waiting list.
	QCOMPARE(boardedIds(defaultRun().passengers), "Pb ");
	defaultRun().passengers = {waiting("Pa"), waiting("Pb"), waiting("Pa")};
	defaultRun().passengerIndex.build(defaultRun().passengers);
	board(2, defaultRun().passengers, defaultRun().platforms);
	// Of two passengers with one ID the first in the list boards.
	QCOMPARE(boardedIds(defaultRun().passengers), "Pa Pb ");

	// A passenger added after the index was built still boards: the shared index no longer describes the list.
	defaultRun().passengers = {waiting("Pa"), waiting("Pb")};
	defaultRun().passengerIndex.build(defaultRun().passengers);
	defaultRun().passengers.push_back(waiting("Pc"));
	defaultRun().platforms.front().Current_List_Pax_On_Platform = {{"Pc", 0.0}};
	board(2, defaultRun().passengers, defaultRun().platforms);
	// A passenger added after the index was built boards.
	QCOMPARE(boardedIds(defaultRun().passengers), "Pc ");

	// Another list of the same size as the one of the run is not looked up in the index of the run.
	defaultRun().passengers = {waiting("Pa"), waiting("Pb")};
	defaultRun().passengerIndex.build(defaultRun().passengers);
	defaultRun().platforms.front().Current_List_Pax_On_Platform = {{"Pb", 0.0}};
	std::list<Passenger> others = {waiting("Pb"), waiting("Pa")};
	board(2, others, defaultRun().platforms);
	// A passenger list that is not the one of the run is looked up in its own index.
	QCOMPARE(boardedIds(others), "Pb ");
	QVERIFY(boardedIds(defaultRun().passengers).empty());

	// The same holds for a platform list.
	StationPlatform otherStop = stop;
	otherStop.Current_List_Pax_On_Platform = {{"Pa", 0.0}};
	const std::list<StationPlatform> otherPlatforms = {otherStop};
	others = {waiting("Pb"), waiting("Pa")};
	board(2, others, otherPlatforms);
	// A platform list that is not the one of the run is looked up in its own index.
	QCOMPARE(boardedIds(others), "Pa ");

	// A train at a platform that does not exist boards nobody.
	others = {waiting("Pb"), waiting("Pa")};
	train.CurrentServiceStopPlatform = "P9";
	board(2, others, defaultRun().platforms);
	// A train at an unknown platform boards nobody.
	QVERIFY(boardedIds(others).empty());
	QCOMPARE(train.Current_OnBoard_Passengers, 0);
}

// A passenger who waits at a platform for the second trip of a journey, which leaves at this time.
static Passenger passengerWaitingAt(const std::string& id, const std::string& station, const std::string& platform, int departure) {
	Passenger passenger;
	passenger.ID = id;
	passenger.IsIntheNetwork = true;
	passenger.CurrentStatus = "OnPlatform";
	passenger.Current_WaitingStationID = station;
	passenger.Current_WaitingStationPlatformID = platform;
	passenger.Current_Train_To_Wait = "T1";
	passenger.current_JourneyID = "J1";
	passenger.current_TripID = "A2";
	Journey journey;
	journey.ID = "J1";
	Trip firstTrip;
	firstTrip.TripID = "A1";
	firstTrip.Actual_Departure_Time = departure + 100;
	Trip secondTrip;
	secondTrip.TripID = "A2";
	secondTrip.Actual_Departure_Time = departure;
	journey.Trips = {firstTrip, secondTrip};
	passenger.Journeys.push_back(journey);
	return passenger;
}

// The waiting count and list of one platform worked out with a scan of all passengers for that platform.
static void scanWaitingList(StationPlatform& platform, const std::list<Passenger>& everyone) {
	platform.Current_N_Passengers = 0;
	platform.Current_List_Pax_On_Platform.clear();
	for (const Passenger& passenger : everyone) {
		if (!passenger.IsIntheNetwork || passenger.CurrentStatus != "OnPlatform" || passenger.Current_WaitingStationID != platform.StationID
			|| passenger.Current_WaitingStationPlatformID != platform.ID)
			continue;
		platform.Current_N_Passengers++;
		for (const Journey& journey : passenger.Journeys) {
			if (journey.ID != passenger.current_JourneyID)
				continue;
			for (const Trip& trip : journey.Trips) {
				if (trip.TripID == passenger.current_TripID) {
					platform.Current_List_Pax_On_Platform.push_back(std::make_pair(passenger.ID, trip.Actual_Departure_Time));
					break;
				}
			}
		}
	}
	platform.Current_List_Pax_On_Platform.sort(orderPassengerListOnPlatform);
}

static std::string waitingIds(const std::list<std::pair<std::string, double>>& waiting) {
	std::string ids;
	for (const auto& entry : waiting)
		ids += entry.first + " ";
	return ids;
}

// The passengers waiting at a platform are counted and listed in the order of their departure times, for one platform and for all
// platforms in one pass over the passengers.
void TestOperationsBuilder::waitingPassengersAreListedByDeparture() {
	std::list<Passenger> passengers;
	passengers.push_back(passengerWaitingAt("a1", "S1", "P1", 30));
	passengers.push_back(passengerWaitingAt("a2", "S1", "P1", 10));
	passengers.push_back(passengerWaitingAt("a3", "S1", "P1", 10));
	passengers.push_back(passengerWaitingAt("b1", "S1", "P2", 5));
	passengers.push_back(passengerWaitingAt("c1", "S2", "P1", 7));
	passengers.push_back(passengerWaitingAt("x1", "S9", "P1", 1)); // at a platform that is not in the list
	Passenger outside = passengerWaitingAt("d1", "S1", "P1", 1);
	outside.IsIntheNetwork = false;
	passengers.push_back(outside);
	Passenger onBoard = passengerWaitingAt("d2", "S1", "P1", 1);
	onBoard.CurrentStatus = "OnBoard";
	passengers.push_back(onBoard);
	Passenger withoutJourney = passengerWaitingAt("e1", "S1", "P1", 1);
	withoutJourney.Journeys.clear();
	passengers.push_back(withoutJourney);
	Passenger otherTrip = passengerWaitingAt("e2", "S1", "P1", 1);
	otherTrip.current_TripID = "A9";
	passengers.push_back(otherTrip);
	Passenger twoJourneys = passengerWaitingAt("f1", "S1", "P1", 20);
	twoJourneys.Journeys.push_back(twoJourneys.Journeys.front());
	passengers.push_back(twoJourneys);

	const char* const stations[] = {"S1", "S1", "S2", "S3", "S1"};
	const char* const platformIds[] = {"P1", "P2", "P1", "P1", "P1"};
	std::list<StationPlatform> platforms(5);
	{
		std::size_t index = 0;
		for (StationPlatform& platform : platforms) {
			platform.StationID = stations[index];
			platform.ID = platformIds[index];
			platform.Current_N_Passengers = 7;
			platform.Current_List_Pax_On_Platform = {{"old", 1.0}};
			++index;
		}
	}

	const WaitingAtPlatform first = passengersWaitingAtPlatform("S1", "P1", passengers);
	// The waiting passengers of a platform are counted and listed by departure time, in list order for equal times.
	QCOMPARE(first.count, 6);
	QCOMPARE(waitingIds(first.passengers), "a2 a3 f1 f1 a1 ");
	const WaitingAtPlatform nobody = passengersWaitingAtPlatform("S3", "P1", passengers);
	// A platform where nobody waits has no waiting passengers.
	QCOMPARE(nobody.count, 0);
	QVERIFY(nobody.passengers.empty());
	const bool unchanged = std::all_of(platforms.begin(), platforms.end(), [](const StationPlatform& platform) {
		return platform.Current_N_Passengers == 7 && waitingIds(platform.Current_List_Pax_On_Platform) == "old ";
	});
	QVERIFY2(unchanged, "working out the waiting passengers of a platform does not modify a platform");

	Update_List_Passengers_Waiting_At_ALL_Platforms(platforms, passengers);
	bool sameAsScan = true;
	for (const StationPlatform& platform : platforms) {
		StationPlatform scanned = platform;
		scanWaitingList(scanned, passengers);
		sameAsScan &= platform.Current_N_Passengers == scanned.Current_N_Passengers
			&& platform.Current_List_Pax_On_Platform == scanned.Current_List_Pax_On_Platform;
	}
	QVERIFY2(sameAsScan, "one pass over the passengers gives every platform the count and list of a scan for that platform");
	const std::vector<StationPlatform> updated(platforms.begin(), platforms.end());
	// Platforms get the waiting passengers of their station and platform.
	QCOMPARE(updated[0].Current_N_Passengers, 6);
	QCOMPARE(waitingIds(updated[0].Current_List_Pax_On_Platform), "a2 a3 f1 f1 a1 ");
	QCOMPARE(updated[1].Current_N_Passengers, 1);
	QCOMPARE(waitingIds(updated[1].Current_List_Pax_On_Platform), "b1 ");
	QCOMPARE(updated[2].Current_N_Passengers, 1);
	QCOMPARE(waitingIds(updated[2].Current_List_Pax_On_Platform), "c1 ");
	// A platform where nobody waits loses its previous list and count.
	QCOMPARE(updated[3].Current_N_Passengers, 0);
	QVERIFY(updated[3].Current_List_Pax_On_Platform.empty());
	// Platforms with the same IDs get the same waiting passengers.
	QCOMPARE(updated[4].Current_N_Passengers, updated[0].Current_N_Passengers);
	QVERIFY(updated[4].Current_List_Pax_On_Platform == updated[0].Current_List_Pax_On_Platform);
	std::list<StationPlatform> noPlatforms;
	Update_List_Passengers_Waiting_At_ALL_Platforms(noPlatforms, passengers);
	QVERIFY2(noPlatforms.empty(), "no platforms are made for the waiting passengers");
}

// Without the passenger GUI a boarding train works from the list of its own platform.
void TestOperationsBuilder::boardingWithoutPassengerGuiKeepsPlatformList() {
	// Without the passenger GUI the shared platform keeps the list it had: the train boards from the list worked out for the platform.
	std::list<Passenger> savedPassengers;
	savedPassengers.swap(defaultRun().passengers);
	const PassengerIndex savedPassengerIndex = defaultRun().passengerIndex;
	defaultRun().passengerIndex.clear();
	const bool savedPaxGui = defaultRun().settings.PAX_GUI;
	const auto restore = qScopeGuard([&] {
		eglog::setSink({});
		defaultRun().settings.PAX_GUI = savedPaxGui;
		defaultRun().passengers.swap(savedPassengers);
		defaultRun().passengerIndex = savedPassengerIndex;
	});
	defaultRun().settings.PAX_GUI = false;
	eglog::setSink([](eglog::Category, eglog::Level, const std::string&) {});

	defaultRun().passengers = {passengerWaitingAt("Pa", "S1", "P1", 30), passengerWaitingAt("Pb", "S1", "P1", 20)};
	StationPlatform stop;
	stop.StationID = "S1";
	stop.ID = "P1";
	stop.Current_N_Passengers = 5;
	stop.Current_List_Pax_On_Platform = {{"Pa", 0.0}, {"Pb", 0.0}};
	const std::list<StationPlatform> stopPlatforms = {stop};
	Train train;
	train.run_ = &defaultRun();
	train.StoppedForServiceStop = true;
	train.trainDescription = "T1";
	train.CurrentServiceStop = "S1";
	train.CurrentServiceStopPlatform = "P1";
	train.MAX_OnBoard_Passengers = 1;
	train.Current_OnBoard_Passengers = 0;
	Simulate_Train_Passenger_Interactions(defaultRun(), 10, 0, train, defaultRun().passengers, stopPlatforms);
	std::string boarded;
	for (const Passenger& passenger : defaultRun().passengers)
		if (passenger.CurrentStatus == "OnBoard")
			boarded += passenger.ID + " ";
	// Without the passenger GUI passengers board in the order of departure times.
	QCOMPARE(boarded, "Pb ");
	// Without the passenger GUI the shared platform is not given the waiting passengers.
	QCOMPARE(stopPlatforms.front().Current_N_Passengers, 5);
	QCOMPARE(waitingIds(stopPlatforms.front().Current_List_Pax_On_Platform), "Pa Pb ");
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
	train.run_ = &defaultRun();
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
	(void)junk;
}

static bool brakingTargetAdmissible(Train& train, double head, double speed, double targetSpeed, Section* sections, int count) {
	const Arc* arc = train.brakingArc(head, sections, count);
	if (!arc)
		return false;
	const double maximum = std::max(0.0, speed + (train.tractiveEffort(speed) - train.total_train_resistances(speed, arc->gradient, arc->curvature)) / (train.total_train_mass * train.massFactor) * timestep);
	return std::isfinite(targetSpeed) && targetSpeed >= std::max(0.0, speed - (train.max_train_decelaration + train.total_train_resistances(speed, arc->gradient, arc->curvature) / (train.total_train_mass * train.massFactor)) * timestep) && targetSpeed <= maximum;
}

void TestOperationsBuilder::brakingPointSearch() {
	const auto at = [](double value, double expected) { return std::fabs(value - expected) <= 1e-6; };
	Train train = brakingTrain(1.0);
	std::vector<Section> route = brakingRoute(0.0, 10.0, 0.0);

	// Solve the braking point with the declared actual-start forward model.
	for (double targetSpeed : {0.0, 10.0}) {
		const double point = train.BrakDist_Block(30, targetSpeed, 5000, route.data(), 1);
		double end = 0.0, endSpeed = 0.0;
		// Braking point reaches its actual endpoint with admissible terminal force.
		QVERIFY(point > 0);
		QVERIFY(point < 5000);
		if (targetSpeed == 0) {
			QVERIFY(train.predictBraking(point, 30, 0, route.data(), 1, end));
			QVERIFY(at(end, 4999.998));
		} else {
			train.DrawBrakingCurve(30, targetSpeed, 5000, route.data(), 1);
			QVERIFY(train.predictBrakingPrefix(point, 30, train.BrakStep - 1, route.data(), 1, end, endSpeed));
			QVERIFY((4999.998 - end) / timestep <= targetSpeed);
			QVERIFY(brakingTargetAdmissible(train, end, endSpeed, (4999.998 - end) / timestep, route.data(), 1));
		}
		// A cutoff above the braking point keeps it and a cutoff below gives a position not below the cutoff.
		QCOMPARE_EQ(train.BrakDist_Block(30, targetSpeed, 5000, route.data(), 1, point + 50), point);
		QVERIFY(train.BrakDist_Block(30, targetSpeed, 5000, route.data(), 1, point - 50) >= point - 50);
	}

	// No braking is needed below target speed.
	QCOMPARE_EQ(train.BrakDist_Block(10, 10, 5000, route.data(), 1), 5000.0);
	QCOMPARE_EQ(train.BrakDist_Block(10, 30, 5000, route.data(), 1), 5000.0);
	std::vector<Section> later = brakingRoute(2.0, 12.0, 0.0);
	const double beforeOrigin = train.BrakDist_Block(30, 0, 100, route.data(), 1);
	// Inverse leaving route at entry retains its computed upstream coordinate.
	QVERIFY(beforeOrigin < 0);
	QVERIFY(at(train.BrakDist_Block(30, 0, 2100, later.data(), 1), beforeOrigin + 2000));

	// The braking force cannot hold the train on a steep descent: the curve runs forward and
	// leaves the route at its end.
	std::vector<Section> steep = brakingRoute(0.0, 10.0, -0.5);
	// A braking curve that leaves the route at its end gives no braking point.
	QCOMPARE_EQ(train.BrakDist_Block(30.0, 10.0, 5000.0, steep.data(), 1), -1.0);
	// 1,999 steps are not enough on a long, almost balanced slope.
	Train slow = brakingTrain(0.001);
	std::vector<Section> farRoute = brakingRoute(0.0, 100.0, -0.0044);
	// A curve that does not reach the current speed in 1,999 steps gives no braking point.
	QCOMPARE_EQ(slow.BrakDist_Block(30.0, 0.0, 50000.0, farRoute.data(), 1), -1.0);

	// The result does not depend on what the stack held before the call.
	for (const double value : {0.0, 1e30, -1e30, std::numeric_limits<double>::quiet_NaN()}) {
		poisonStack(value);
		const double start = train.BrakDist_Block(30.0, 0.0, 100.0, route.data(), 1);
		poisonStack(value);
		const double end = train.BrakDist_Block(30.0, 10.0, 5000.0, steep.data(), 1);
		// The braking point of a curve that leaves the route does not depend on the stack.
		QVERIFY(at(start, beforeOrigin));
		QCOMPARE_EQ(end, -1.0);
	}
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

// Within a step of a run BrakDist_Block answers from the cache of the train with the result of the computation, for the
// results of that step and of the step before.
void TestOperationsBuilder::brakingStartCacheMatchesComputation() {
	const double inf = std::numeric_limits<double>::infinity();
	const auto same = [](double a, double b) { return std::memcmp(&a, &b, sizeof(double)) == 0; };
	Train train = brakingTrain(1.0);
	std::vector<Section> route = brakingRouteOfTwoArcs(4.9, 10.0, 0.02, -0.01);
	std::vector<Section> flat = brakingRoute(0.0, 10.0, 0.0);
	struct Query {
		double from, to, target, cutoff;
	};
	std::vector<Query> queries;
	for (double from : {0.0, 5.0, 12.5, 30.0, 36.1})
		for (double to : {0.0, 10.0, 20.0})
			for (double target : {100.0, 2500.0, 4900.0, 7000.0})
				for (double cutoff : {inf, 0.0, 3000.0})
					queries.push_back({from, to, target, cutoff});
	// Without braking the target comes back as it is, so 0 and -0 are two queries.
	queries.push_back({10.0, 10.0, 0.0, inf});
	queries.push_back({10.0, 10.0, -0.0, inf});
	const int all = static_cast<int>(queries.size());
	// Every call gives the result of the computation, bit for bit.
	const auto matches = [&](Train& probe, std::vector<Section>& sections) {
		return std::all_of(queries.begin(), queries.end(), [&](const Query& q) {
			const double start = probe.BrakDist_Block(q.from, q.to, q.target, sections.data(), 1, q.cutoff);
			return same(start, probe.uncachedBrakDistBlock(q.from, q.to, q.target, sections.data(), 1, q.cutoff));
		});
	};
	// The number of queries that the cache answers now.
	const auto held = [&](Train& probe, std::vector<Section>& sections) {
		return static_cast<int>(std::count_if(queries.begin(), queries.end(), [&](const Query& q) {
			double start = 0.0;
			return probe.stepCache.findBrakingStart({sections.data(), 1, q.from, q.to, q.target, q.cutoff}, start);
		}));
	};

	// Outside a step every call is computed and nothing is kept.
	QVERIFY(matches(train, route));
	train.stepCache.beginStep(1);
	QCOMPARE(held(train, route), 0);
	// Within a step the calls are kept, and a repeated call is answered from the cache.
	QVERIFY(matches(train, route));
	QCOMPARE(held(train, route), all);
	QVERIFY(matches(train, route));
	// The results belong to the sections of the call.
	QCOMPARE(held(train, flat), 0);
	QVERIFY(matches(train, flat));
	train.stepCache.endStep();
	QCOMPARE(held(train, route), 0);
	QVERIFY(matches(train, route));
	// The next step answers with the results of the step before.
	train.stepCache.beginStep(2);
	QCOMPARE(held(train, route), all);
	QVERIFY(matches(train, route));
	train.stepCache.endStep();
	// A step without calls leaves nothing for the step after it.
	train.stepCache.beginStep(3);
	train.stepCache.endStep();
	train.stepCache.beginStep(4);
	QCOMPARE(held(train, route), 0);
	QVERIFY(matches(train, route));
	train.stepCache.endStep();
	// A run starts at step 0, so a step that does not follow the last one drops the results. A change of the arcs, which only
	// the builders make, is then seen.
	const double before = train.uncachedBrakDistBlock(30.0, 0.0, 7000.0, route.data(), 1, inf);
	route[0].arcs_in_signalling_block_section[1].gradient = 0.03;
	QVERIFY(!same(before, train.uncachedBrakDistBlock(30.0, 0.0, 7000.0, route.data(), 1, inf)));
	train.stepCache.beginStep(0);
	QCOMPARE(held(train, route), 0);
	QVERIFY(matches(train, route));
	train.stepCache.endStep();
	// The result does not depend on the values that the signalling writes on the sections in every step, so the results of the
	// step before still hold after a change of them.
	for (Section& section : route) {
		section.code = 0;
		section.state = SignalState::Red;
		section.Occupied = true;
		section.exit_speed = 2.0;
		for (int k = 0; k < section.total_arcs; ++k) {
			Arc& arc = section.arcs_in_signalling_block_section[k];
			arc.speedInBraking = 3.0;
			arc.signalSpeedLimit = 4.0;
			arc.endNode.arcSpeedLimit = 5.0;
		}
	}
	train.stepCache.beginStep(1);
	QCOMPARE(held(train, route), all);
	QVERIFY(matches(train, route));
	train.stepCache.endStep();
}

// The braking-start cache of one train never answers for another train.
void TestOperationsBuilder::brakingStartCacheBelongsToOneTrain() {
	const double inf = std::numeric_limits<double>::infinity();
	std::vector<Section> route = brakingRoute(0.0, 10.0, 0.0);
	const TrainStepCache::BrakingQuery query{route.data(), 1, 30.0, 0.0, 5000.0, inf};
	Train strong = brakingTrain(1.0), weak = brakingTrain(0.5);
	strong.stepCache.beginStep(1);
	weak.stepCache.beginStep(1);
	const double strongStart = strong.BrakDist_Block(30.0, 0.0, 5000.0, route.data(), 1);
	const double weakStart = weak.BrakDist_Block(30.0, 0.0, 5000.0, route.data(), 1);
	// Two trains with the same query get the results of their own braking.
	QVERIFY(strongStart != weakStart);
	QCOMPARE_EQ(strongStart, strong.uncachedBrakDistBlock(30.0, 0.0, 5000.0, route.data(), 1, inf));
	QCOMPARE_EQ(weakStart, weak.uncachedBrakDistBlock(30.0, 0.0, 5000.0, route.data(), 1, inf));
	// A copy starts empty and outside a step, so a copy with another deceleration computes its own result.
	Train copy = strong;
	double start = 0.0;
	QVERIFY(!copy.stepCache.findBrakingStart(query, start));
	copy.max_train_decelaration = 0.5;
	copy.stepCache.beginStep(2);
	QVERIFY(!copy.stepCache.findBrakingStart(query, start));
	QCOMPARE_EQ(copy.BrakDist_Block(30.0, 0.0, 5000.0, route.data(), 1), weakStart);
	// An assignment empties the cache too.
	weak = strong;
	weak.max_train_decelaration = 0.5;
	weak.stepCache.beginStep(2);
	QVERIFY(!weak.stepCache.findBrakingStart(query, start));
	QCOMPARE_EQ(weak.BrakDist_Block(30.0, 0.0, 5000.0, route.data(), 1), weakStart);
	// Within a step an override of BrakDist_Block still takes the call.
	struct CountingLookup : Train {
		int calls = 0;
		double BrakDist_Block(double, double, double, Section*, int, double) override {
			++calls;
			return -1;
		}
	} counting;
	static_cast<Train&>(counting) = strong;
	counting.max_train_speed = 30.0;
	counting.stepCache.beginStep(1);
	QVERIFY(counting.needsSingleTrackReservation(1000.0, 10.0, 4000.0, route.data(), 1));
	QCOMPARE(counting.calls, 1);
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
// brakingTrain(1.0) from the braking-point tests, a timestep of one second, a speed of 36.1111 m/s and a target
// speed of 16.67 m/s.
void TestOperationsBuilder::drawsBrakingCurveThatReachesSpeed() {
	const auto at = [](double value, double expected) { return std::fabs(value - expected) <= 1e-6; };
	// A curve that reaches the speed is reported as such and is stored in driving order.
	Train train = brakingTrain(1.0);
	std::vector<Section> flat = brakingRoute(0.0, 10.0, 0.0);
	// A braking curve that reaches the current speed is stored from the first step to the target.
	QVERIFY(train.DrawBrakingCurve(30.0, 0.0, 5000.0, flat.data(), 1));
	QVERIFY(train.BrakStep > 0);
	QVERIFY(train.Vbrak[0] >= 30.0);
	QVERIFY(at(train.Sbrak[train.BrakStep], 4999.998));
	QCOMPARE_EQ(train.Vbrak[train.BrakStep], 0.0);
	// A speed that does not exceed the target speed has no curve either, and the curve before it is not kept.
	// A speed below the target speed gives no braking curve and marks the curve empty.
	QVERIFY(!train.DrawBrakingCurve(10.0, 20.0, 5000.0, flat.data(), 1));
	QCOMPARE(train.BrakStep, -1);
	train.DrawBrakingCurve(30.0, 0.0, 5000.0, flat.data(), 1);
	// A speed equal to the target speed gives no braking curve and marks the curve empty.
	QVERIFY(!train.DrawBrakingCurve(20.0, 20.0, 5000.0, flat.data(), 1));
	QCOMPARE(train.BrakStep, -1);
}

struct BrakingCase {
	const char* name;
	std::vector<Section> route;
	double position, targetPosition;
	int steps;
	bool fallsBelowTarget;
};

// In the first case the target at 1673 m lies on an arc with a gradient of
// -9.62, so the curve runs forward and leaves the route, while the train is on a flat arc before it and runs
// onto the steep arc during the steps. In the second case the target lies 100 m after the start of a flat
// route: the curve needs more than 500 m, so the braking point lies before the route. In the third case the
// train is 673 m before the target and slows down below the target speed on the flat arc.
static std::vector<BrakingCase> brakingCases() {
	return {
		{"a curve that leaves the route at its end", brakingRouteOfTwoArcs(1.594, 2.0, 0.0, -9.62), 1390.28, 1673.0, 8, false},
		{"a curve that leaves the route at its start", brakingRoute(0.0, 10.0, 0.0), 50.0, 100.0, 8, false},
		{"a curve that leaves the route at its end for a train that slows down below the target speed",
			brakingRouteOfTwoArcs(1.594, 2.0, 0.0, -9.62), 1000.0, 1673.0, 22, true},
	};
}

void TestOperationsBuilder::brakingWithoutReachingSpeed_data() {
	QTest::addColumn<int>("index");
	const std::vector<BrakingCase> cases = brakingCases();
	for (int index = 0; index < static_cast<int>(cases.size()); ++index)
		QTest::newRow(cases[index].name) << index;
}

void TestOperationsBuilder::brakingWithoutReachingSpeed() {
	QFETCH(int, index);
	const double speed = 36.1111, targetSpeed = 16.67;
	std::vector<BrakingCase> cases = brakingCases();
	BrakingCase& c = cases[index];
	const int steps = c.steps;
	Train fresh = brakingTrain(1.0);
	// The curve is reported as not reaching the speed and is marked empty.
	QVERIFY(!fresh.DrawBrakingCurve(speed, targetSpeed, c.targetPosition, c.route.data(), 1));
	QCOMPARE(fresh.BrakStep, -1);

	// A reachable curve was drawn before: nothing of it is read, and it is not kept.
	Train earlier = brakingTrain(1.0);
	std::vector<Section> flat = brakingRoute(0.0, 10.0, 0.0);
	// A reachable curve is drawn first.
	QVERIFY(earlier.DrawBrakingCurve(30.0, 0.0, 5000.0, flat.data(), 1));
	QVERIFY(earlier.BrakStep > 0);
	// The curve after it is reported as not reaching the speed and is marked empty.
	QVERIFY(!earlier.DrawBrakingCurve(speed, targetSpeed, c.targetPosition, c.route.data(), 1));
	QCOMPARE(earlier.BrakStep, -1);

	Train withoutCurve = brakingTrain(1.0);
	const BrakingRun reference = brakingRun(withoutCurve, c.route, c.position, speed, targetSpeed, c.targetPosition, steps);
	Train withCurve = brakingTrain(1.0);
	withCurve.DrawBrakingCurve(30.0, 0.0, 5000.0, flat.data(), 1);
	const BrakingRun run = brakingRun(withCurve, c.route, c.position, speed, targetSpeed, c.targetPosition, steps);
	// The steps do not depend on the curve that was drawn before.
	QCOMPARE(run.position, reference.position);
	QCOMPARE(run.speed, reference.speed);

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
	QVERIFY2(followsFullBraking, "position and speed follow the step of full braking");
	// A train moves forward by no more than its larger speed times the step and the step keeps the position before it.
	QVERIFY(boundHolds);
	QCOMPARE_EQ(run.position[1], c.position);
	QCOMPARE_EQ(run.speed[1], speed);
	// Every step is computed.
	QVERIFY(setOnlyWhereComputed);
	QVERIFY(std::isfinite(run.position[0]));
	QVERIFY(std::isfinite(run.position[1]));
	// A step without curve clears the gradient exception, also below the target speed.
	QVERIFY(run.gradientExceptionClear);
	QVERIFY(!c.fallsBelowTarget || run.speed[steps + 1] < targetSpeed);
	// The step is marked as a step without curve.
	QCOMPARE(run.eq[2], 54);
}

// Values of single steps of the two cases, computed with the formula of the step.
void TestOperationsBuilder::brakingStepValues() {
	const auto at = [](double value, double expected) { return std::fabs(value - expected) <= 1e-6; };
	const double speed = 36.1111, targetSpeed = 16.67;
	{
		std::vector<Section> route = brakingRouteOfTwoArcs(1.594, 2.0, 0.0, -9.62);
		Train train = brakingTrain(1.0);
		const BrakingRun run = brakingRun(train, route, 1390.28, speed, targetSpeed, 1673.0, 8);
		// First step of the case with the curve that leaves the route at its end, on the flat arc.
		QVERIFY(at(run.speed[2], 34.934396));
		QVERIFY(at(run.position[2], 1425.802748));
		// Step of the same case on the steep arc, where the train gains speed.
		QVERIFY(at(run.speed[9], 113.485645));
		QVERIFY(at(run.position[9], 1685.336327));
	}
	{
		std::vector<Section> route = brakingRoute(0.0, 10.0, 0.0);
		Train train = brakingTrain(1.0);
		const BrakingRun run = brakingRun(train, route, 50.0, speed, targetSpeed, 100.0, 8);
		// First step of the case with the braking point before the route.
		QVERIFY(at(run.speed[2], 34.934396));
		QVERIFY(at(run.position[2], 85.522748));
		Train search = brakingTrain(1.0);
		QVERIFY2(search.BrakDist_Block(speed, targetSpeed, 100.0, route.data(), 1) < 50.0,
			"the braking-point search gives a braking point before the train for the second case");
	}
}

// The routes of the inverse braking steps: one arc that is flat, rises or falls, and two arcs that change the gradient
// at 4.99 km, each without and with curvature.
static std::vector<std::vector<Section>> inverseBrakingRoutes() {
	std::vector<std::vector<Section>> routes;
	for (double gradient : {0.0, 0.005, -0.005})
		routes.push_back(brakingRoute(0, 10, gradient));
	for (const auto& gradients : {std::pair<double, double>{0, 0.005}, {0.005, 0}, {0, -0.005}, {-0.005, 0}}) {
		auto route = brakingRouteOfTwoArcs(4.99, 10, gradients.first, gradients.second);
		routes.push_back(route);
		route[0].arcs_in_signalling_block_section[0].curvature = 500;
		route[0].arcs_in_signalling_block_section[1].curvature = 1000;
		routes.push_back(route);
	}
	return routes;
}

void TestOperationsBuilder::inverseStepsSolveForwardModel_data() {
	QTest::addColumn<int>("index");
	for (int index = 0; index < static_cast<int>(inverseBrakingRoutes().size()); ++index)
		QTest::newRow(("route " + std::to_string(index)).c_str()) << index;
}

// Fractions shift the entry coordinate, not a linearly interpolated nonlinear flow.
void TestOperationsBuilder::inverseStepsSolveForwardModel() {
	QFETCH(int, index);
	const std::size_t routeIndex = static_cast<std::size_t>(index);
	std::vector<std::vector<Section>> routes = inverseBrakingRoutes();
	auto& route = routes[routeIndex];
	Train native = brakingTrain(1);
	QVERIFY2(native.DrawBrakingCurve(30, 0, 5000, route.data(), 1), "actual-start inverse is drawable");
	if (native.BrakStep < 0)
		return;
	for (int k = 0; k < native.BrakStep; ++k) {
		const double head = native.Sbrak[k], speed = native.Vbrak[k];
		const Arc* arc = native.brakingArc(head, route.data(), 1);
		// Every inverse native step solves the actual-start forward equation.
		if (arc == nullptr)
			QFAIL("every inverse native step solves the actual-start forward equation");
		QVERIFY(near(native.Vbrak[k + 1], native.brakingCurveSpeed(speed, *arc)));
		QVERIFY(near(native.Sbrak[k + 1], head + native.Vbrak[k + 1] * timestep));
	}
	for (double fraction : {0.0, 0.35, 0.8}) {
		Train train = brakingTrain(1);
		train.Sbrak = native.Sbrak;
		train.Vbrak = native.Vbrak;
		train.BrakStep = native.BrakStep;
		train.counter = 1;
		const int k = routeIndex < 3 ? 3 : std::max(0, native.BrakStep - 6);
		const double speed = native.Vbrak[k];
		const double head = native.Sbrak[k] - fraction * speed * timestep;
		train.instant_spatial_position = {head - speed, head, -1};
		train.instant_train_speed = {speed, speed, -1};
		train.Eq.assign(3, 0);
		train.Xobmin = 5000;
		train.Vobmin = 0;
		bool stopped = false;
		for (int step = 0; step < 100; ++step) {
			const double oldHead = train.instant_spatial_position[1], oldSpeed = train.instant_train_speed[1];
			const Arc* arc = train.brakingArc(oldHead, route.data(), 1);
			if (arc == nullptr)
				QFAIL("feasible continuation remains on route");
			const double minimum = train.brakingCurveSpeed(oldSpeed, *arc);
			const double maximum = std::max(0.0, oldSpeed + (train.tractiveEffort(oldSpeed) - train.total_train_resistances(oldSpeed, arc->gradient, arc->curvature)) / (train.total_train_mass * train.massFactor) * timestep);
			train.brakingStep(2, *arc, route.data(), 1);
			const double nextHead = train.instant_spatial_position[2], nextSpeed = train.instant_train_speed[2];
			std::ostringstream context;
			context.precision(17);
			context << "route=" << routeIndex << " fraction=" << fraction << " step=" << step << " head=" << oldHead
					<< " speed=" << oldSpeed << " next=" << nextHead << "/" << nextSpeed << " eq=" << train.Eq[2];
			QVERIFY2(train.Eq[2] != 54, ("feasible continuation does not take late fallback: " + context.str()).c_str());
			const std::string bounds = "actual-speed force, full subsequent model steps and right-endpoint distance: " + context.str();
			QVERIFY2(nextSpeed >= minimum - 1e-9, bounds.c_str());
			QVERIFY2(nextSpeed <= maximum + 1e-9, bounds.c_str());
			QVERIFY2(near(nextHead, oldHead + nextSpeed * timestep), bounds.c_str());
			QVERIFY2(nextHead >= oldHead, bounds.c_str());
			QVERIFY2(nextHead <= 5000, bounds.c_str());
			QVERIFY2(step == 0 || near(nextSpeed, minimum), bounds.c_str());
			// Continuation leaves prior samples immutable.
			QCOMPARE_EQ(train.instant_spatial_position[1], oldHead);
			QCOMPARE_EQ(train.instant_train_speed[1], oldSpeed);
			if (nextSpeed == 0) {
				QVERIFY2(near(nextHead, 4999.998), ("native/non-grid complete stop retains real anchor: " + context.str()).c_str());
				stopped = true;
				break;
			}
			train.instant_spatial_position[1] = nextHead;
			train.instant_train_speed[1] = nextSpeed;
		}
		QVERIFY2(stopped, "feasible native/non-grid continuation completes its stop");
	}
}

// The old downstream-sampled state is genuinely late under the corrected flow.
void TestOperationsBuilder::lateNativeStateUsesFallback() {
	auto route = brakingRouteOfTwoArcs(4.99, 10, 0, 0.005);
	Train train = brakingTrain(1);
	const double head = 4989.1453539500844, speed = 5.4322173106653109;
	double end = 0.0;
	// Old native state is late for the actual-start right-endpoint flow.
	QVERIFY(train.predictBraking(head, speed, 0, route.data(), 1, end));
	QVERIFY(end > 4999.998);
	const auto run = brakingRun(train, route, head, speed, 0, 5000, 6);
	// Separate legacy late-target mean-position fallback is unchanged.
	QCOMPARE(run.eq[2], 54);
	QVERIFY(near(run.position.back(), 5002.9173234323098));
	QCOMPARE_EQ(run.speed.back(), 0.0);
}

// The terminal vertical step and a duplicate zero-speed state require no geometry snap.
void TestOperationsBuilder::zeroSpeedTerminalsKeepCoordinate() {
	auto route = brakingRoute(0, 10, 0);
	Train train = brakingTrain(1);
	const auto run = brakingRun(train, route, 4999.998, 0.35, 0, 5000, 2);
	// Vertical and degenerate zero-speed terminals retain their coordinate.
	QCOMPARE_EQ(run.speed[2], 0.0);
	QCOMPARE_EQ(run.speed[3], 0.0);
	QCOMPARE_EQ(run.position[2], 4999.998);
	QCOMPARE_EQ(run.position[3], 4999.998);
}

// A changed target is redrawn from the actual state, never from a rewritten past.
void TestOperationsBuilder::changedLateTargetKeepsHistory() {
	auto route = brakingRoute(0, 10, 0);
	Train train = brakingTrain(1);
	train.Xob.assign(3, 5000);
	train.Vob.assign(3, 0);
	train.Xobmin = 4500;
	train.Vobmin = 0;
	train.counter = 1;
	train.instant_spatial_position = {4780, 4800, -1};
	train.instant_train_speed = {20, 20, -1};
	train.Eq.assign(3, 0);
	train.brakingStep(2, route[0].arcs_in_signalling_block_section[0], route.data(), 1);
	// Changed late target preserves immutable history and fallback.
	QCOMPARE(train.Eq[2], 54);
	QCOMPARE_EQ(train.instant_spatial_position[1], 4800.0);
	QCOMPARE_EQ(train.instant_train_speed[1], 20.0);
	QVERIFY(train.instant_spatial_position[2] > 4800);
}

// Fold and nonfinite branches are rejected explicitly, not selected by root order.
void TestOperationsBuilder::rejectsInvalidInverseBranches() {
	auto route = brakingRoute(0, 10, 0);
	Train train = brakingTrain(1);
	double head = 0.0, speed = 0.0;
	QVERIFY2(!train.inverseBrakingStep(5000, std::numeric_limits<double>::quiet_NaN(), route.data(), 1, head, speed),
		"nonfinite inverse is rejected");
	QVERIFY2(!train.inverseBrakingStep(5000, 10000, route.data(), 1, head, speed), "missing inverse branch is rejected");
}

// Exercise the linear inverse independently of aerodynamic quadratic terms.
void TestOperationsBuilder::linearInverseSolvesActualStartStep() {
	struct ConstantResistanceTrain : Train {
		double total_train_resistances(double, double gradient, double curvature) override {
			return gradient_resistances(gradient) + curvature_resistances(curvature);
		}
	};
	ConstantResistanceTrain train;
	static_cast<Train&>(train) = brakingTrain(1);
	auto route = brakingRoute(0, 100, 0);
	double head = 0.0, speed = 0.0;
	// Zero-quadratic inverse solves the linear actual-start step.
	QVERIFY(train.inverseBrakingStep(5000, 10, route.data(), 1, head, speed));
	QCOMPARE_EQ(head, 4990.0);
	QCOMPARE_EQ(speed, 11.0);
	Train quadratic = brakingTrain(1);
	QVERIFY2(!quadratic.inverseBrakingStep(30000, 10000, route.data(), 1, head, speed),
		"quadratic branch beyond its maximum has no admissible inverse");
}

// A real non-monotone inverse branch dips below its positive target speed,
// then recovers on a descent. Reaching the limit early is not sufficient:
// the remaining descent cannot hold it even with full braking.
void TestOperationsBuilder::nonMonotoneInverseRecovers() {
	auto route = brakingRouteOfTwoArcs(4.9, 10, 0.02, -0.2);
	Train train = brakingTrain(1);
	QVERIFY2(train.DrawBrakingCurve(35, 15, 5000, route.data(), 1), "non-monotone native inverse reaches its upstream speed");
	if (train.BrakStep < 0)
		return;
	const double minimum = *std::min_element(train.Vbrak.begin(), train.Vbrak.begin() + train.BrakStep + 1);
	bool exactModel = true, recovers = false;
	for (int k = 0; k < train.BrakStep; ++k) {
		const Arc* arc = train.brakingArc(train.Sbrak[k], route.data(), 1);
		exactModel &= arc && near(train.Vbrak[k + 1], train.brakingCurveSpeed(train.Vbrak[k], *arc))
			&& near(train.Sbrak[k + 1], train.Sbrak[k] + train.Vbrak[k + 1] * timestep);
		recovers |= train.Vbrak[k + 1] > train.Vbrak[k];
	}
	const double point = train.BrakDist_Block(35, 15, 5000, route.data(), 1);
	double firstLimitHead = 0.0;
	const bool early = train.predictBraking(train.Sbrak[0], train.Vbrak[0], 15, route.data(), 1, firstLimitHead);
	const auto& descent = route[0].arcs_in_signalling_block_section[1];
	const double holdingMinimum = train.brakingCurveSpeed(15, descent);
	// Native non-monotone branch obeys actual model, but the remaining descent cannot hold its target speed.
	QVERIFY(exactModel);
	QVERIFY(minimum < 15);
	QVERIFY(recovers);
	QVERIFY(early);
	QVERIFY(firstLimitHead < 4999.998);
	QVERIFY(holdingMinimum > 15);
	// Valid non-monotone target is not silently omitted by the braking-point producer.
	QVERIFY(point >= 0);
	QVERIFY(point <= train.Sbrak[0] + 35 * timestep);
	for (double fraction : {0.0, 0.35, 0.8}) {
		Train live = brakingTrain(1);
		live.Sbrak = train.Sbrak;
		live.Vbrak = train.Vbrak;
		live.BrakStep = train.BrakStep;
		live.counter = 1;
		const double startSpeed = train.Vbrak[0], start = train.Sbrak[0] - fraction * startSpeed * timestep;
		live.instant_spatial_position = {start - startSpeed, start, -1};
		live.instant_train_speed = {startSpeed, startSpeed, -1};
		live.Eq.assign(3, 0);
		live.Xob.assign(3, 5000);
		live.Vob.assign(3, 15);
		live.Xobmin = 5000;
		live.Vobmin = 15;
		bool reached = false, below = false, recovered = false;

		for (int step = 0; step < 100; ++step) {
			const double oldHead = live.instant_spatial_position[1], oldSpeed = live.instant_train_speed[1];
			const Arc* arc = live.brakingArc(oldHead, route.data(), 1);
			if (!arc) break;
			const double lower = live.brakingCurveSpeed(oldSpeed, *arc);
			const double upper = std::max(0.0, oldSpeed + (live.tractiveEffort(oldSpeed) - live.total_train_resistances(oldSpeed, arc->gradient, arc->curvature)) / (live.total_train_mass * live.massFactor) * timestep);
			live.brakingStep(2, *arc, route.data(), 1);
			const double nextHead = live.instant_spatial_position[2], nextSpeed = live.instant_train_speed[2];
			std::ostringstream context;
			context.precision(17);
			context << "fraction=" << fraction << " step=" << step << " old=" << oldHead << "/" << oldSpeed << " next=" << nextHead << "/" << nextSpeed << " eq=" << live.Eq[2];
			const bool terminal = near(nextHead, 4999.998) && nextSpeed > 0 && nextSpeed <= 15;
			const std::string plan = "positive endpoint plan obeys full prefix and admissible terminal force: " + context.str();
			QVERIFY2(live.Eq[2] != 54, plan.c_str());
			QVERIFY2(nextSpeed >= lower - 1e-9, plan.c_str());
			QVERIFY2(nextSpeed <= upper + 1e-9, plan.c_str());
			QVERIFY2(nextHead >= oldHead, plan.c_str());
			QVERIFY2(nextHead <= 5000, plan.c_str());
			QVERIFY2(near(nextHead, oldHead + nextSpeed * timestep), plan.c_str());
			QVERIFY2(step == 0 || terminal || near(nextSpeed, lower), plan.c_str());
			// Positive continuation keeps history immutable.
			QCOMPARE_EQ(live.instant_spatial_position[1], oldHead);
			QCOMPARE_EQ(live.instant_train_speed[1], oldSpeed);
			below |= nextSpeed < 15;
			recovered |= below && nextSpeed > oldSpeed;
			if (terminal) {
				reached = true;
				break;
			}
			if (nextHead > 5000 || nextSpeed == 0)
				break;
			live.instant_spatial_position[1] = nextHead;
			live.instant_train_speed[1] = nextSpeed;
		}
		// Native/non-grid non-monotone plan retains target through real recovery.
		QVERIFY(reached);
		QVERIFY(below);
		QVERIFY(recovered);
	}
}

// Native inverse states and non-grid entry must obey actual-start force sampling.
// The pre-reuse linear lookup and predictor bodies are kept as exact differential references.
struct LinearBrakingReference : Train {
	const Arc* brakingArc(double head, Section* sections, int count) const {
		for (int b = 0; b < count; ++b)
			for (int j = 0; j < sections[b].total_arcs; ++j) {
				const Arc& arc = sections[b].arcs_in_signalling_block_section[j];
				if (head >= arc.startNode.X * 1000 && head < arc.endNode.X * 1000)
					return &arc;
			}
		return nullptr;
	}

	bool predictBraking(double head, double speed, double targetSpeed, Section* sections, int count,
		double& end, std::vector<double>* positions = nullptr, std::vector<double>* speeds = nullptr) {
		for (int step = 0; step < 1999; ++step) {
			end = head; // An above-target route exit can reject a shooting bracket, never validate a plan.
			if (!std::isfinite(head) || !std::isfinite(speed) || speed < 0)
				return false;
			if (positions) {
				positions->push_back(head);
				speeds->push_back(speed);
			}
			if (speed <= targetSpeed) {
				end = head;
				return true;
			}
			const Arc* arc = brakingArc(head, sections, count);
			if (!arc)
				return false;
			speed = brakingCurveSpeed(speed, *arc);
			head += speed * timestep;
		}
		return false;
	}

	bool predictBrakingPrefix(double head, double speed, int steps, Section* sections, int count,
		double& end, double& endSpeed, std::vector<double>* positions = nullptr, std::vector<double>* speeds = nullptr) {
		if (steps < 0 || steps >= 1999)
			return false;
		for (int step = 0; step <= steps; ++step) {
			end = head;
			endSpeed = speed;
			if (!std::isfinite(head) || !std::isfinite(speed) || speed < 0)
				return false;
			if (positions) {
				positions->push_back(head);
				speeds->push_back(speed);
			}
			if (step == steps)
				return true;
			const Arc* arc = brakingArc(head, sections, count);
			if (!arc)
				return false;
			speed = brakingCurveSpeed(speed, *arc);
			head += speed * timestep;
		}
		return false;
	}
};

void TestOperationsBuilder::brakingLookupMatchesLinearReference() {
	Train train = brakingTrain(1);
	LinearBrakingReference reference;
	static_cast<Train&>(reference) = train;
	const auto exact = [](double a, double b) { return std::memcmp(&a, &b, sizeof(double)) == 0; };
	const auto exactVector = [](const std::vector<double>& a, const std::vector<double>& b) {
		return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(double)) == 0;
	};
	// A failed check ends this function only, so each call is followed by QTest::currentTestFailed().
	const auto compare = [&](std::vector<Section>& route, double head, double speed, int steps) {
		Section* sections = route.data();
		const int count = static_cast<int>(route.size());
		double end = -123, oldEnd = end, endSpeed = -456, oldSpeed = endSpeed;
		std::vector<double> positions{-123}, speeds{-456}, oldPositions = positions, oldSpeeds = speeds;
		bool result = train.predictBraking(head, speed, 0, sections, count, end, &positions, &speeds);
		bool oldResult = reference.predictBraking(head, speed, 0, sections, count, oldEnd, &oldPositions, &oldSpeeds);
		// Complete predictor equals old linear reference, including partial arrays and failure endpoint.
		QCOMPARE(result, oldResult);
		QVERIFY(exact(end, oldEnd));
		QVERIFY(exactVector(positions, oldPositions));
		QVERIFY(exactVector(speeds, oldSpeeds));
		end = oldEnd = -123;
		positions = oldPositions = {-123};
		speeds = oldSpeeds = {-456};
		result = train.predictBrakingPrefix(head, speed, steps, sections, count, end, endSpeed, &positions, &speeds);
		oldResult = reference.predictBrakingPrefix(head, speed, steps, sections, count, oldEnd, oldSpeed, &oldPositions, &oldSpeeds);
		// Fixed prefix equals old linear reference, including validation and failure outputs.
		QCOMPARE(result, oldResult);
		QVERIFY(exact(end, oldEnd));
		QVERIFY(exact(endSpeed, oldSpeed));
		QVERIFY(exactVector(positions, oldPositions));
		QVERIFY(exactVector(speeds, oldSpeeds));
		result = train.predictBraking(head, speed, 15, sections, count, end);
		oldResult = reference.predictBraking(head, speed, 15, sections, count, oldEnd);
		// Positive completion without vectors equals linear reference.
		QCOMPARE(result, oldResult);
		QVERIFY(exact(end, oldEnd));
		result = train.predictBrakingPrefix(head, speed, steps, sections, count, end, endSpeed);
		oldResult = reference.predictBrakingPrefix(head, speed, steps, sections, count, oldEnd, oldSpeed);
		// Prefix without vectors equals linear reference.
		QCOMPARE(result, oldResult);
		QVERIFY(exact(end, oldEnd));
		QVERIFY(exact(endSpeed, oldSpeed));
		// Walks from a copy of a cursor that looked up a head before, at or beyond the start equal the linear reference.
		for (double seedHead : {head - 25, head, head + 25}) {
			Train::BrakingArcCursor from;
			from.find(seedHead, sections, count);
			end = oldEnd = -123;
			positions = oldPositions = {-123};
			speeds = oldSpeeds = {-456};
			result = train.predictBraking(head, speed, 0, sections, count, end, &positions, &speeds, &from);
			oldResult = reference.predictBraking(head, speed, 0, sections, count, oldEnd, &oldPositions, &oldSpeeds);
			QCOMPARE(result, oldResult);
			QVERIFY(exact(end, oldEnd));
			QVERIFY(exactVector(positions, oldPositions));
			QVERIFY(exactVector(speeds, oldSpeeds));
			result = train.predictBrakingPrefix(head, speed, steps, sections, count, end, endSpeed, nullptr, nullptr, &from);
			oldResult = reference.predictBrakingPrefix(head, speed, steps, sections, count, oldEnd, oldSpeed);
			QCOMPARE(result, oldResult);
			QVERIFY(exact(end, oldEnd));
			QVERIFY(exact(endSpeed, oldSpeed));
		}
	};
	// An earlier-listed future-start overlap must interrupt reuse of the later-listed arc.
	auto overlap = brakingRouteOfTwoArcs(0.06, 0.1, 0.02, -0.005);
	overlap[0].arcs_in_signalling_block_section[0].startNode.X = 0.03;
	overlap[0].arcs_in_signalling_block_section[1].startNode.X = 0;
	double upper = 0.0;
	// Reuse ends at earlier-listed future start, not selected overlapping arc end.
	QVERIFY(train.brakingArc(10, overlap.data(), 1, &upper) == reference.brakingArc(10, overlap.data(), 1));
	QCOMPARE_EQ(upper, 30.0);
	double lookupHead = 0, safeUpper = 0;
	const Arc* reused = nullptr;
	for (double head : {10.0, std::nextafter(30.0, 0.0), 30.0, 45.0, std::nextafter(60.0, 0.0), 60.0, 80.0, 10.0, 100.0}) {
		if (!reused || head < lookupHead || !(head < safeUpper)) {
			reused = train.brakingArc(head, overlap.data(), 1, &safeUpper);
			lookupHead = head;
		}
		QVERIFY2(reused == reference.brakingArc(head, overlap.data(), 1),
			"safe interval keeps exact first match across overlap, boundary and backward movement");
	}
	// Advance through multiple sections before an earlier-listed future arc takes precedence.
	std::vector<Section> forward{
		brakingRoute(0.10, 0.12, 0.02)[0],
		brakingRoute(0.13, 0.125, 0.02)[0], // Invalid interval still contributes its future start.
		brakingRoute(0, 0.01, -0.005)[0],
		Section{},
		brakingRoute(0.01, 0.02, 0.02)[0],
		brakingRoute(0.02, 0.04, -0.005)[0],
		brakingRoute(0.05, 0.08, 0.02)[0], // A gap forces conservative restart and later recovery.
		brakingRoute(0.08, 0.16, -0.005)[0]};
	forward[3].total_arcs = 0;
	Train::BrakingArcCursor cursor;
	for (double head : {0.0, 10.0, 20.0, 35.0, 40.0, 50.0, 80.0,
			 std::nextafter(100.0, 0.0), 100.0, 119.0, 120.0, 130.0, 150.0, 160.0,
			 50.0, 20.0, 10.0, 0.0, 150.0}) {
		double cursorUpper = 0.0, linearUpper = std::numeric_limits<double>::infinity();
		const Arc* linear = reference.brakingArc(head, forward.data(), static_cast<int>(forward.size()));
		for (const Section& section : forward) {
			bool found = false;
			for (int j = 0; j < section.total_arcs; ++j) {
				const Arc& arc = section.arcs_in_signalling_block_section[j];
				if (&arc == linear) {
					linearUpper = std::min(linearUpper, arc.endNode.X * 1000);
					found = true;
					break;
				}
				if (arc.startNode.X * 1000 > head)
					linearUpper = std::min(linearUpper, arc.startNode.X * 1000);
			}
			if (found) break;
		}
		// Forward cursor and safe upper equal linear lookup across jumps, gaps, invalid arcs and backtracking.
		QVERIFY(cursor.find(head, forward.data(), static_cast<int>(forward.size()), &cursorUpper) == linear);
		QVERIFY(exact(cursorUpper, linearUpper));
		// One-off lookup retains exact safe upper including no match.
		QVERIFY(train.brakingArc(head, forward.data(), static_cast<int>(forward.size()), &upper) == linear);
		QVERIFY(exact(upper, linearUpper));
	}
	std::vector<Section> shortArcs;
	for (int k = 0; k < 80; ++k)
		shortArcs.push_back(brakingRoute(k * 0.002, (k + 1) * 0.002, k % 2 ? -0.005 : 0.02)[0]);
	std::vector<std::vector<Section>> routes{brakingRouteOfTwoArcs(0.03, 0.1, 0.02, -0.005), overlap, forward, shortArcs};
	auto gap = routes[0];
	gap[0].arcs_in_signalling_block_section[1].startNode.X = 0.04;
	routes.push_back(gap);
	// First-match order also holds across sections whose coordinate order differs.
	std::vector<Section> crossSection{brakingRoute(0.03, 0.06, 0.02)[0], brakingRoute(0, 0.1, -0.005)[0]};
	routes.push_back(crossSection);
	for (auto& route : routes)
		for (double head : {-1.0, 0.0, 10.0, std::nextafter(30.0, 0.0), 30.0, 35.0, 40.0, 60.0, 100.0})
			for (int steps : {-1, 0, 1, 12, 1998, 1999}) {
				compare(route, head, 30, steps);
				if (QTest::currentTestFailed())
					return;
			}
	compare(overlap, 10, 0, 12);
	if (QTest::currentTestFailed())
		return;
	compare(overlap, 10, -1, 12);
	if (QTest::currentTestFailed())
		return;
	compare(overlap, std::numeric_limits<double>::quiet_NaN(), 30, 12);
	if (QTest::currentTestFailed())
		return;
	compare(overlap, 10, std::numeric_limits<double>::infinity(), 12);
	if (QTest::currentTestFailed())
		return;
	// The existing positive-target inverse really dips below its ceiling and recovers.
	auto recovery = brakingRouteOfTwoArcs(4.9, 10, 0.02, -0.2);
	QVERIFY2(train.DrawBrakingCurve(35, 15, 5000, recovery.data(), 1), "differential positive prefix uses native recovery fixture");
	if (train.BrakStep < 0)
		return;
	compare(recovery, train.Sbrak[0], train.Vbrak[0], train.BrakStep - 1);
	if (QTest::currentTestFailed())
		return;
	std::vector<double> positions, speeds;
	double end = 0.0, endSpeed = 0.0;
	bool below = false, recovered = false;
	const bool predicted = train.predictBrakingPrefix(train.Sbrak[0], train.Vbrak[0], train.BrakStep - 1,
		recovery.data(), 1, end, endSpeed, &positions, &speeds);
	for (std::size_t k = 1; k < speeds.size(); ++k) {
		below |= speeds[k] < 15;
		recovered |= below && speeds[k] > speeds[k - 1];
	}
	// Exact differential prefix includes below-ceiling gradient recovery.
	QVERIFY(predicted);
	QVERIFY(below);
	QVERIFY(recovered);
}

// The backward cursor of the inverse braking walk finds the arc of brakingArc for every position, on both routes of a scene and
// on routes whose arcs overlap, leave gaps, are listed out of order or hold empty and invalid intervals.
void TestOperationsBuilder::backwardBrakingCursorMatchesLookup() {
	const double inf = std::numeric_limits<double>::infinity(), nan = std::numeric_limits<double>::quiet_NaN();
	const auto same = [](double a, double b) { return std::memcmp(&a, &b, sizeof(double)) == 0; };
	Train train = brakingTrain(1.0);
	// Looks up the heads in their order with cursor; true when every lookup gives the arc of brakingArc.
	const auto walk = [&train](Train::BackwardBrakingArcCursor& cursor, Section* sections, int count, const std::vector<double>& heads) {
		return std::all_of(heads.begin(), heads.end(), [&](double head) {
			return cursor.find(head, sections, count) == train.brakingArc(head, sections, count);
		});
	};
	// Every arc boundary (m) of the sections with its neighbours, and positions `spacing` m apart from 50 m beyond the highest
	// boundary to 50 m before the lowest, from high to low.
	const auto descendingHeads = [inf](const Section* sections, int count, double spacing) {
		std::vector<double> heads;
		for (int i = 0; i < count; ++i)
			for (int k = 0; k < sections[i].total_arcs; ++k) {
				const Arc& arc = sections[i].arcs_in_signalling_block_section[k];
				for (double boundary : {arc.startNode.X * 1000, arc.endNode.X * 1000})
					heads.insert(heads.end(), {std::nextafter(boundary, -inf), boundary, std::nextafter(boundary, inf)});
			}
		const double low = *std::min_element(heads.begin(), heads.end()), high = *std::max_element(heads.begin(), heads.end());
		const int positions = static_cast<int>((high - low + 100) / spacing);
		for (int k = 0; k <= positions; ++k)
			heads.push_back(high + 50 - k * spacing);
		std::sort(heads.begin(), heads.end(), std::greater<>());
		return heads;
	};

	const QString fixture = QDir(QStringLiteral(EGTRAIN_TEST_SOURCE_DIR)).filePath("fixtures/scenes/crossovers");
	const auto loaded = loadScene(fixture.toStdString());
	const SceneRunSelection firstF1{{"F1", 1}};
	if (hasErrors(loaded.diagnostics) || hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), loaded.scene))
		|| hasErrors(buildOperationsFromScene(defaultRun(), loaded.scene, "baseline", firstF1)) || defaultRun().numTrains != 1)
		QFAIL("crossover scene builds");
	Train& native = defaultRun().trains[0];
	// The scene has a route in each direction of travel.
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t{2});
	for (Route& route : defaultRun().infra.routes) {
		Section* sections = route.sequence_of_block_sections.data();
		const int count = route.N_Block_Sections;
		QVERIFY(count > 1);
		Train::BackwardBrakingArcCursor cursor;
		QVERIFY(walk(cursor, sections, count, descendingHeads(sections, count, 0.25)));
		// The arcs of the route are ordered, so the cursor moved back over them.
		QVERIFY(cursor.ordered);
		// The inverse braking walk with a cursor takes the steps of the walk without one, from standstill at the end of the route
		// until it leaves the route. The speed starts again from standstill above 30 m/s.
		Train::BackwardBrakingArcCursor stepCursor;
		double nextHead = route.x_of_end_node * 1000 - 10, nextSpeed = 0;
		int steps = 0;
		for (bool inside = true; inside; ++steps) {
			double head = -1, speed = -1, plainHead = -2, plainSpeed = -2;
			inside = native.inverseBrakingStep(nextHead, nextSpeed, sections, count, head, speed, &stepCursor);
			QCOMPARE(native.inverseBrakingStep(nextHead, nextSpeed, sections, count, plainHead, plainSpeed), inside);
			QVERIFY(same(head, plainHead));
			if (inside) {
				QVERIFY(same(speed, plainSpeed));
				nextHead = head;
				nextSpeed = speed > 30 ? 0 : speed;
			}
			QVERIFY(steps < 100000);
		}
		QVERIFY(steps > 10);
		QVERIFY(nextHead < sections[0].start_node.X * 1000 + 30);
	}

	auto overlap = brakingRouteOfTwoArcs(0.06, 0.1, 0.02, -0.005);
	overlap[0].arcs_in_signalling_block_section[0].startNode.X = 0.03;
	overlap[0].arcs_in_signalling_block_section[1].startNode.X = 0;
	auto gap = brakingRouteOfTwoArcs(0.03, 0.1, 0.02, -0.005);
	gap[0].arcs_in_signalling_block_section[1].startNode.X = 0.04;
	std::vector<Section> unordered{brakingRoute(0.10, 0.12, 0.02)[0], brakingRoute(0.13, 0.125, 0.02)[0],
		brakingRoute(0, 0.01, -0.005)[0], Section{}, brakingRoute(0.01, 0.02, 0.02)[0], brakingRoute(0.02, 0.04, -0.005)[0],
		brakingRoute(0.05, 0.08, 0.02)[0], brakingRoute(0.08, 0.16, -0.005)[0]};
	unordered[3].total_arcs = 0;
	std::vector<Section> crossSection{brakingRoute(0.03, 0.06, 0.02)[0], brakingRoute(0, 0.1, -0.005)[0]};
	// The arc from 60 m back to 40 m holds nothing, but the arc after it overlaps the first one.
	std::vector<Section> inverted{brakingRoute(0, 0.05, 0)[0], brakingRoute(0.06, 0.04, 0)[0], brakingRoute(0.04, 0.07, 0)[0]};
	// Ordered, with an empty section, an arc of no length and gaps.
	std::vector<Section> ordered{brakingRoute(0, 0.01, 0)[0], Section{}, brakingRoute(0.01, 0.02, 0)[0],
		brakingRoute(0.03, 0.03, 0)[0], brakingRoute(0.03, 0.05, 0)[0], brakingRoute(0.06, 0.08, 0)[0]};
	ordered[1].total_arcs = 0;
	// Ordered up to the second arc, with a later arc over the gap before it or before the first arc.
	std::vector<Section> coveredGap{brakingRoute(0, 0.01, 0)[0], brakingRoute(0.02, 0.03, 0)[0], brakingRoute(0.005, 0.025, 0)[0]};
	std::vector<Section> laterStart{brakingRoute(0.01, 0.02, 0)[0], brakingRoute(0.03, 0.04, 0)[0], brakingRoute(0, 0.005, 0)[0]};
	std::vector<Section> shortArcs;
	for (int k = 0; k < 80; ++k)
		shortArcs.push_back(brakingRoute(k * 0.002, (k + 1) * 0.002, 0)[0]);
	for (std::vector<Section>* route :
		{&overlap, &gap, &unordered, &crossSection, &inverted, &ordered, &coveredGap, &laterStart, &shortArcs}) {
		Section* sections = route->data();
		const int count = static_cast<int>(route->size());
		Train::BackwardBrakingArcCursor cursor;
		QVERIFY(walk(cursor, sections, count, descendingHeads(sections, count, 0.5)));
		// Moves forward, NaN and infinities between moves back.
		QVERIFY(walk(cursor, sections, count, {150, 70, 75, 30, nan, 25, 45, 5, 60, -inf, 55, inf, 10, 9, 70, 65, 45, 35, 3}));
	}
}

void TestOperationsBuilder::parkedTrainIsNotShortOfBrakingPoint() {
	const double brakingPoint = 46181.0;
	const double parked = std::nextafter(brakingPoint - kStopHoldbackM, 0.0);
	QVERIFY2(!isShortOfBrakingPoint(parked, brakingPoint),
		"a train parked one ulp below its hold-back position is not short of the braking point");
	QVERIFY2(isShortOfBrakingPoint(brakingPoint - 0.001, brakingPoint), "a train 1 mm short of the braking point still accelerates");
}

// A stop of 20 s at a point whose parked position is exact, one unit in the last place low or one high: the timetable point
// reports the same arrival and departure each time.
void TestOperationsBuilder::timetablePointOfStopIgnoresLastPlace() {
	// The trains take the route of the station statistics fixture.
	SceneModel statisticsScene = completeScene();
	statisticsScene.services[0].hasRepeatCount = true;
	statisticsScene.services[0].repeatCount = 3;
	QVERIFY(!hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), statisticsScene)));
	QVERIFY(!hasErrors(buildOperationsFromScene(defaultRun(), statisticsScene, "scenario.base")));
	if (defaultRun().trains.empty())
		QFAIL("the station statistics fixture builds a train");
	const double stoppingPoint = 46181.0;
	const double exact = stoppingPoint - kStopHoldbackM;
	const double savedTimes = defaultRun().settings.times;
	const auto restoreTimes = qScopeGuard([savedTimes] { defaultRun().settings.times = savedTimes; });
	defaultRun().settings.times = 60;
	for (const double parked : {exact, std::nextafter(exact, 0.0), std::nextafter(exact, std::numeric_limits<double>::infinity())}) {
		Train stopping;
		stopping.run_ = &defaultRun();
		stopping.indexOfRoute = defaultRun().trains[0].indexOfRoute;
		stopping.departure_time = 1.0;
		stopping.instant_spatial_position.assign(60, stoppingPoint + 500.0);
		for (int second = 0; second < 10; ++second)
			stopping.instant_spatial_position[second] = stoppingPoint - 1000.0 + 100.0 * second;
		for (int second = 10; second < 30; ++second)
			stopping.instant_spatial_position[second] = parked;
		stopping.instant_spatial_position[30] = stoppingPoint + kStopHoldbackM;
		TrainEvent point;
		stopping.computeArrivalAndDepartureAtLocation(stoppingPoint, point);
		// A timetable point reports arrival and departure of a stop whatever the last place of the parked position.
		QCOMPARE_EQ(point.Time, 9.0 * timestep);
		QCOMPARE_EQ(point.Time2, 29.0 * timestep);
		// A parked position is at the stop and the position after the stop is past it.
		QVERIFY(!isShortOfBrakingPoint(parked, stoppingPoint));
		QVERIFY(!isPastStopHoldback(parked, stoppingPoint));
		QVERIFY(isPastStopHoldback(stoppingPoint + kStopHoldbackM, stoppingPoint));
	}
	// A last stop: the train arrives and stays until the end of the run.
	Train terminating;
	terminating.run_ = &defaultRun();
	terminating.indexOfRoute = defaultRun().trains[0].indexOfRoute;
	terminating.departure_time = 1.0;
	terminating.instant_spatial_position.assign(60, exact);
	for (int second = 0; second < 10; ++second)
		terminating.instant_spatial_position[second] = stoppingPoint - 1000.0 + 100.0 * second;
	TrainEvent point;
	terminating.computeArrivalAndDepartureAtLocation(stoppingPoint, point);
	// A timetable point reports the arrival at a last stop and no departure.
	QCOMPARE_EQ(point.Time, 9.0 * timestep);
	QCOMPARE_EQ(point.Time2, TrainEvent().Time2);
}

void TestOperationsBuilder::brakingPhase() {
	SceneModel scene = completeScene();
	scene.settings.durationSeconds = 600;
	scene.services[0].entryTimeSeconds = 0;
	scene.services[0].hasRepeatCount = true;
	scene.services[0].repeatCount = 1;
	scene.services[0].stops.clear();
	scene.passengers.clear();
	for (auto& stock : scene.trainUnits)
		for (auto& band : stock.tractionCurve)
			band[2] = 10000; // Same native stock as the existing performance movement test.
	const auto infrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), scene);
	const auto operations = buildOperationsFromScene(defaultRun(), scene, "scenario.base");
	if (hasErrors(infrastructure) || hasErrors(operations) || defaultRun().numTrains != 1)
		QFAIL("phase fixture prepares native train");
	Train& train = defaultRun().trains[0];
	Route& phaseRoute = defaultRun().infra.routes[train.indexOfRoute];
	phaseRoute.sequence_of_block_sections[0].arcs_in_signalling_block_section[0].speedInBraking = 10;
	train.CanEnter = true;
	train.departure_time = 0;
	train.instant_spatial_position[0] = 690;
	train.instant_spatial_position[1] = 700;
	train.instant_train_speed[0] = train.instant_train_speed[1] = 10;
	train.Xob[1] = 1000;
	train.Vob[1] = 10;
	train.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
	// Reached speed target is retained until its actual boundary.
	QCOMPARE_EQ(train.instant_train_speed[2], 10.0);
	QCOMPARE_EQ(train.instant_spatial_position[2], 710.0);
	QCOMPARE(train.Eq[2], 12);
	// Cruising does not rewrite its prior speed or position.
	QCOMPARE_EQ(train.instant_train_speed[1], 10.0);
	QCOMPARE_EQ(train.instant_spatial_position[1], 700.0);
	// A sudden current-arc restriction cannot be met by rewriting either endpoint speed.
	phaseRoute.sequence_of_block_sections[0].arcs_in_signalling_block_section[0].signalSpeedLimit = 5;
	train.instant_spatial_position[1] = 700;
	train.instant_train_speed[1] = 10;
	train.counter = 0;
	const Arc& phaseArc = phaseRoute.sequence_of_block_sections[0].arcs_in_signalling_block_section[0];
	const double minimumSpeed = std::max(0.0, 10 - (train.max_train_decelaration + train.total_train_resistances(10, phaseArc.gradient, phaseArc.curvature) / (train.total_train_mass * train.massFactor)) * timestep);
	train.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
	// Late current speed limit uses the model's full-braking step, not a cap or Eq12 history rewrite.
	QCOMPARE_EQ(train.instant_train_speed[1], 10.0);
	QVERIFY(near(train.instant_train_speed[2], minimumSpeed));
	QCOMPARE(train.Eq[2], 54);
	QVERIFY(near(train.instant_spatial_position[2], 700 + (10 + minimumSpeed) / 2 * timestep));
	// Retain the original native stock for the executed steep-descent probe below.
	SceneModel targetScene = scene;
	targetScene.trainUnits = completeScene().trainUnits;
	if (hasErrors(buildOperationsFromScene(defaultRun(), targetScene, "scenario.base")) || defaultRun().numTrains != 1)
		QFAIL("target-selection fixture prepares original native stock");
	const Train targetStock = defaultRun().trains[0];
	// Measure the real native stock's candidate, not an invented acceleration.
	const QString fixture = QDir(QStringLiteral(EGTRAIN_TEST_SOURCE_DIR)).filePath("fixtures/scenes/crossovers");
	const auto loaded = loadScene(fixture.toStdString());
	const SceneRunSelection firstF1{{"F1", 1}};
	if (hasErrors(loaded.diagnostics) || hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), loaded.scene))
		|| hasErrors(buildOperationsFromScene(defaultRun(), loaded.scene, "baseline", firstF1)) || defaultRun().numTrains != 1)
		QFAIL("native crossover stock prepares for crossing detection");
	Train& native = defaultRun().trains[0];
	Route& nativeRoute = defaultRun().infra.routes[native.indexOfRoute];
	// Target-selection cases use real native stock, not the high-traction acceleration fixture.
	{
		Route& route = nativeRoute;
		// A satisfied nearby speed target must not mask the farther stop's last braking opportunity.
		{
			const Route savedRoute = nativeRoute;
			for (double ceiling : {10.0, 11.0}) {
				Train live = native;
				live.numStations = 0;
				live.CanEnter = true;
				live.departure_time = 0;
				nativeRoute.sequence_of_block_sections = brakingRoute(0, 10, 0);
				nativeRoute.N_Block_Sections = 1;
				nativeRoute.x_of_end_node = 10;
				const double speed = 10;
				const double point = live.BrakDist_Block(speed, 0, 5000, nativeRoute.sequence_of_block_sections.data(), 1);
				const double head = point - speed * timestep / 2;
				const double satisfied = point - speed * timestep / 4;
				nativeRoute.sequence_of_block_sections = brakingRouteOfTwoArcs(satisfied / 1000, 10, 0, 0);
				auto& section = nativeRoute.sequence_of_block_sections[0];
				section.code = 270;
				for (int k = 0; k < 2; ++k) {
					section.arcs_in_signalling_block_section[k].speedLimit = speed;
					section.arcs_in_signalling_block_section[k].signalSpeedLimit = speed;
				}
				section.arcs_in_signalling_block_section[0].speedInBraking = ceiling;
				section.arcs_in_signalling_block_section[1].endNode.X = 5;
				section.arcs_in_signalling_block_section[1].speedInBraking = 0;
				live.counter = 0;
				live.instant_spatial_position[0] = head - speed * timestep;
				live.instant_spatial_position[1] = head;
				live.instant_train_speed[0] = live.instant_train_speed[1] = speed;
				live.Xob[1] = live.Vob[1] = 0;
				// Satisfied positive target precedes stop BX crossed by this movement.
				QVERIFY(head < satisfied);
				QVERIFY(satisfied < point);
				QVERIFY(head + speed * timestep > point);
				live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
				// Equal/below-ceiling demand selects restrictive stop rather than satisfied target.
				QCOMPARE_EQ(live.Xob[2], 5000.0);
				QCOMPARE_EQ(live.Vob[2], 0.0);
				QCOMPARE(live.Eq[2], 55);
				bool stopped = false;
				for (int time = 2; time < 100; ++time) {
					if (time > 2)
						live.trajectoryComputationIncludingMovingBlock(time, signalCode1, signalCode2, signalCode3);
					// Satisfied-target stop keeps previously published samples immutable.
					QCOMPARE_EQ(live.instant_spatial_position[0], head - speed * timestep);
					QCOMPARE_EQ(live.instant_spatial_position[1], head);
					QCOMPARE_EQ(live.instant_train_speed[0], speed);
					QCOMPARE_EQ(live.instant_train_speed[1], speed);
					if (live.Eq[time] != 55 && live.Eq[time] != 52 && live.Eq[time] != 53)
						break;
					if (live.instant_train_speed[time] == 0) {
						stopped = true;
						QVERIFY2(near(live.instant_spatial_position[time], 4999.998), "revealed stop reaches its actual zero-speed anchor");
						break;
					}
				}
				QVERIFY2(stopped, "equal/below-ceiling real selector completes the stop without late fallback");
			}
			nativeRoute = savedRoute;
		}
		// On a supported steep descent, even full braking cannot hold the retained ceiling.
		{
			const Route savedRoute = route;
			route.sequence_of_block_sections = brakingRouteOfTwoArcs(4.9, 10, 0.02, -0.2);
			route.N_Block_Sections = 1;
			route.x_of_end_node = 10;
			auto& section = route.sequence_of_block_sections[0];
			section.code = 270;
			for (int k = 0; k < 2; ++k) {
				auto& running = section.arcs_in_signalling_block_section[k];
				running.speedLimit = running.signalSpeedLimit = running.speedInBraking = 40;
			}
			section.arcs_in_signalling_block_section[1].endNode.X = 5;
			section.arcs_in_signalling_block_section[1].speedInBraking = 15;
			Train prepared = targetStock;
			prepared.CanEnter = true;
			prepared.departure_time = 0;
			prepared.g = 9.81;
			prepared.mass_of_traction_unit = prepared.total_train_mass = 100000;
			prepared.massFactor = 1.09;
			prepared.max_train_decelaration = 1;
			prepared.max_train_speed = 40;
			for (double speed : {15.0, 14.9}) {
				Train live = prepared;
				live.counter = 0;
				live.instant_spatial_position[0] = 4999.998 - speed * timestep;
				live.instant_spatial_position[1] = 4999.998;
				live.instant_train_speed[0] = live.instant_train_speed[1] = speed;
				live.Xob[1] = 5000;
				live.Vob[1] = 15;
				const Arc& descent = section.arcs_in_signalling_block_section[1];
				const double minimum = live.brakingCurveSpeed(speed, descent);
				const double resistance = live.total_train_resistances(speed, descent.gradient, descent.curvature);
				const double fullEffort = -live.total_train_mass * live.massFactor * live.max_train_decelaration;
				// Retained hold/capped acceleration is below the actual-start full-braking bound.
				QVERIFY(minimum > 15);
				QVERIFY(resistance < fullEffort);
				live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
				// Infeasible retained hold/cap uses force-feasible full-model mean-displacement fallback.
				QCOMPARE(live.Eq[2], 54);
				QCOMPARE(live.counter, 0);
				QCOMPARE_EQ(live.instant_train_speed[2], minimum);
				QVERIFY(near(live.instant_spatial_position[2], 4999.998 + (speed + minimum) / 2 * timestep));
				QVERIFY(near(live.total_train_mass * live.massFactor * (live.instant_train_speed[2] - speed) / timestep + resistance, fullEffort));
				// Infeasible ceiling fallback preserves published history.
				QCOMPARE_EQ(live.instant_spatial_position[0], 4999.998 - speed * timestep);
				QCOMPARE_EQ(live.instant_spatial_position[1], 4999.998);
				QCOMPARE_EQ(live.instant_train_speed[0], speed);
				QCOMPARE_EQ(live.instant_train_speed[1], speed);
			}
			// Start upstream through the real selector; recovery below the ceiling is still an active braking plan.
			Train live = prepared;
			QVERIFY2(live.DrawBrakingCurve(35, 15, 5000, route.sequence_of_block_sections.data(), 1),
				"real recovery fixture has a native nonmonotone curve");
			if (live.BrakStep > 0) {
				const double head = live.Sbrak[0], speed = live.Vbrak[0];
				live.counter = 1;
				live.instant_spatial_position[0] = head - speed * timestep;
				live.instant_spatial_position[1] = head;
				live.instant_train_speed[0] = live.instant_train_speed[1] = speed;
				live.Xob[1] = 5000;
				live.Vob[1] = 15;
				bool below = false, recovered = false, reached = false;
				for (int time = 2; time < 100; ++time) {
					const double oldHead = live.instant_spatial_position[time - 1], oldSpeed = live.instant_train_speed[time - 1];
					live.trajectoryComputationIncludingMovingBlock(time, signalCode1, signalCode2, signalCode3);
					const double nextHead = live.instant_spatial_position[time], nextSpeed = live.instant_train_speed[time];
					std::ostringstream context;
					context.precision(17);
					context << " t=" << time << " old=" << oldHead << "/" << oldSpeed << " next=" << nextHead << "/" << nextSpeed
							<< " eq=" << live.Eq[time] << " target=" << live.Xob[time] << "/" << live.Vob[time];
					const std::string plan = "real active recovery retains target, right-endpoint plan and immutable history" + context.str();
					QVERIFY2(live.Eq[time] != 54, plan.c_str());
					QVERIFY2(live.Xob[time] == 5000, plan.c_str());
					QVERIFY2(live.Vob[time] == 15, plan.c_str());
					QVERIFY2(near(nextHead, oldHead + nextSpeed * timestep), plan.c_str());
					QVERIFY2(live.instant_spatial_position[time - 1] == oldHead, plan.c_str());
					QVERIFY2(live.instant_train_speed[time - 1] == oldSpeed, plan.c_str());
					below |= nextSpeed < 15;
					recovered |= below && nextSpeed > oldSpeed;
					if (near(nextHead, 4999.998) && nextSpeed > 0 && nextSpeed <= 15) {
						reached = true;
						break;
					}
					if (nextHead > 5000 || nextSpeed == 0 || live.Eq[time] == 54)
						break;
				}
				// Upstream real selector keeps nonmonotone recovery through its positive endpoint.
				QVERIFY(below);
				QVERIFY(recovered);
				QVERIFY(reached);
			}
			route = savedRoute;
		}
	}
	// The phase selector and its current-limit override must agree at a rounded BX.
	{
		struct BoundaryTrain : Train {
			double point = 0;
			double BrakDist_Block(double, double targetSpeed, double target, Section*, int, double) override {
				return target == 2000 && targetSpeed == 0 ? point : -1;
			}
		};
		const Route savedRoute = nativeRoute;
		for (auto& section : nativeRoute.sequence_of_block_sections)
			for (int k = 0; k < section.total_arcs; ++k) {
				auto& running = section.arcs_in_signalling_block_section[k];
				running.signalSpeedLimit = 5;
				running.speedInBraking = running.endNode.X == 2 ? 0 : native.max_train_speed;
			}
		BoundaryTrain cached;
		static_cast<Train&>(cached) = native;
		cached.CanEnter = true;
		cached.departure_time = 0;
		cached.Xobmin = 2000;
		cached.Vobmin = 0;
		std::vector<double> positions, speeds;
		double end = 0;
		const bool drawn = cached.DrawBrakingCurve(10, 0, 2000, nativeRoute.sequence_of_block_sections.data(), nativeRoute.N_Block_Sections);
		const bool predicted = drawn && cached.predictBraking(cached.Sbrak[0], cached.Vbrak[0], 0, nativeRoute.sequence_of_block_sections.data(), nativeRoute.N_Block_Sections, end, &positions, &speeds);
		// Boundary fixture has a complete actual-start cached stop.
		QVERIFY(predicted);
		QVERIFY(near(end, 1999.998));
		QVERIFY(positions.size() > 3);
		std::copy(positions.begin(), positions.end(), cached.Sbrak.begin());
		std::copy(speeds.begin(), speeds.end(), cached.Vbrak.begin());
		cached.BrakStep = static_cast<int>(positions.size()) - 1;
		if (cached.BrakStep > 2) {
			const double head = cached.Sbrak[1], speed = cached.Vbrak[1];
			const Arc& arc = *cached.brakingArc(head, nativeRoute.sequence_of_block_sections.data(), nativeRoute.N_Block_Sections);
			const double nextSpeed = cached.brakingCurveSpeed(speed, arc), nextHead = head + nextSpeed * timestep;
			// Cached boundary continuation is the full-model step.
			QCOMPARE_EQ(nextSpeed, cached.Vbrak[2]);
			QCOMPARE_EQ(nextHead, cached.Sbrak[2]);
			for (double point : {head, std::nextafter(head, 0.0), std::nextafter(head, std::numeric_limits<double>::infinity()), head + 1}) {
				BoundaryTrain live = cached;
				live.point = point;
				live.counter = 1;
				live.GradientExceptionInBraking = false;
				live.instant_spatial_position[0] = head - speed * timestep;
				live.instant_spatial_position[1] = head;
				live.instant_train_speed[0] = live.instant_train_speed[1] = speed;
				live.Xob[1] = 2000;
				live.Vob[1] = 0;
				live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
				const bool shortOfPoint = isShortOfBrakingPoint(head, point);
				// Boundary dispatch uses the chosen BX and retains published history.
				QCOMPARE_EQ(live.instant_spatial_position[1], head);
				QCOMPARE_EQ(live.instant_train_speed[1], speed);
				QCOMPARE_EQ(live.BX[2], point);
				QVERIFY(speed > arc.signalSpeedLimit);
				// Equivalent rounded BX retains valid full-model cache; genuinely short current-limit state retains fallback.
				if (shortOfPoint) {
					QCOMPARE(live.Eq[2], 54);
					QCOMPARE(live.counter, 0);
					QCOMPARE_EQ(live.instant_train_speed[2], nextSpeed);
					QVERIFY(near(live.instant_spatial_position[2], head + (speed + nextSpeed) / 2 * timestep));
				} else {
					QCOMPARE(live.Eq[2], 52);
					QCOMPARE(live.counter, 1);
					QCOMPARE_EQ(live.instant_train_speed[2], nextSpeed);
					QCOMPARE_EQ(live.instant_spatial_position[2], nextHead);
					QCOMPARE(live.Sbrak, cached.Sbrak);
					QCOMPARE(live.Vbrak, cached.Vbrak);
				}
			}
		}
		nativeRoute = savedRoute;
	}
	// Translation and adjacent representable targets cannot turn a valid ceiling
	// into a rejection merely because subtraction does not invert rounded addition.
	{
		const double speed = 21.189627947945194, ceiling = 11.111000000000001;
		for (double head : {1988.8869999999999, 2488.8870000000002}) {
			const double endSpeed = 11.349688391781495;
			const Arc* arc = native.brakingArc(head, nativeRoute.sequence_of_block_sections.data(), nativeRoute.N_Block_Sections);
			const double minimum = native.brakingCurveSpeed(endSpeed, *arc);
			const double maximum = endSpeed + (native.tractiveEffort(endSpeed) - native.total_train_resistances(endSpeed, arc->gradient, arc->curvature)) / (native.total_train_mass * native.massFactor) * timestep;
			for (double bound : {minimum, ceiling, maximum}) {
				const double anchor = head + bound * timestep;
				for (double adjacent : {head, std::nextafter(head, 0.0), std::nextafter(head, 3000.0)}) {
					double terminal = 0.0;
					const bool constructed = native.constructBrakingTerminal(adjacent, endSpeed, anchor, bound == maximum ? maximum + 1 : ceiling,
						nativeRoute.sequence_of_block_sections.data(), nativeRoute.N_Block_Sections, terminal);
					const double roundoff = 8 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::fabs(anchor));
					// Translated/nextafter terminal controls keep exact actual-state bounds and forward roundoff contract.
					QVERIFY(constructed);
					QVERIFY(terminal >= minimum);
					QVERIFY(terminal <= maximum);
					QVERIFY(bound == maximum || terminal <= ceiling);
					QVERIFY(std::fabs(adjacent + terminal * timestep - anchor) <= roundoff);
				}
			}
			double terminal = 0.0;
			// Materially unreachable, empty-interval and nonfinite terminal controls fail closed.
			QVERIFY(!native.constructBrakingTerminal(head, endSpeed, head + (ceiling + 0.001) * timestep, ceiling,
				nativeRoute.sequence_of_block_sections.data(), nativeRoute.N_Block_Sections, terminal));
			QVERIFY(!native.constructBrakingTerminal(head, endSpeed, head + (minimum - 0.001) * timestep, ceiling,
				nativeRoute.sequence_of_block_sections.data(), nativeRoute.N_Block_Sections, terminal));
			QVERIFY(!native.constructBrakingTerminal(head, endSpeed, head + minimum * timestep, minimum - 0.001,
				nativeRoute.sequence_of_block_sections.data(), nativeRoute.N_Block_Sections, terminal));
			QVERIFY(!native.constructBrakingTerminal(head, endSpeed, std::numeric_limits<double>::quiet_NaN(), ceiling,
				nativeRoute.sequence_of_block_sections.data(), nativeRoute.N_Block_Sections, terminal));
		}

		// Positive comparison has the actual reverse crossover route.
		QCOMPARE(defaultRun().infra.routes.size(), std::size_t(2));
		QVERIFY(defaultRun().infra.routes[1].reversed_direction);
		for (double target : {2000.0, 2500.0, std::nextafter(2000.0, 0.0), std::nextafter(2000.0, 3000.0)}) {
			Route& targetRoute = target == 2500 ? defaultRun().infra.routes[1] : nativeRoute;
			const double point = native.BrakDist_Block(speed, ceiling, target, targetRoute.sequence_of_block_sections.data(), targetRoute.N_Block_Sections);
			Train live = native;
			// Equivalent translated/nextafter positive ceiling produces an upstream braking point.
			QVERIFY(point >= 0);
			QVERIFY(live.DrawBrakingCurve(speed, ceiling, target, targetRoute.sequence_of_block_sections.data(), targetRoute.N_Block_Sections));
			if (live.BrakStep < 0) continue;
			live.Xobmin = target;
			live.Vobmin = ceiling;
			const Arc* startArc = live.brakingArc(point, targetRoute.sequence_of_block_sections.data(), targetRoute.N_Block_Sections);
			if (startArc == nullptr)
				QFAIL("positive producer supports a real admissible endpoint plan");
			QVERIFY2(live.planPositiveBraking(point, speed, *startArc, targetRoute.sequence_of_block_sections.data(), targetRoute.N_Block_Sections, live.BrakStep),
				"positive producer supports a real admissible endpoint plan");
			const int last = live.BrakStep;
			if (last < 1) continue;
			const double end = live.Sbrak[last - 1], endSpeed = live.Vbrak[last - 1], terminal = live.Vbrak[last];
			const double roundoff = 8 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::fabs(target - 0.002));
			// Positive terminal is within exact force/ceiling bounds and forward-computes the anchor within roundoff.
			QVERIFY(terminal <= ceiling);
			QVERIFY(brakingTargetAdmissible(live, end, endSpeed, terminal, targetRoute.sequence_of_block_sections.data(), targetRoute.N_Block_Sections));
			QCOMPARE_EQ(live.Sbrak[last], end + terminal * timestep);
			QVERIFY(std::fabs(live.Sbrak[last] - (target - 0.002)) <= roundoff);
		}
	}

	for (int station = 0; station < native.numStations; ++station) {
		const int block = native.stationBlockSection[station], arc = native.stationArc[station];
		if (block >= 0 && arc >= 0) {
			auto& stopArc = nativeRoute.sequence_of_block_sections[block].arcs_in_signalling_block_section[arc];
			stopArc.endNode.station = 1;
			stopArc.speedInBraking = 0;
		}
	}
	for (bool accelerating : {false, true}) {
		const double speed = 10;
		const double start = native.BrakDist_Block(speed, 0, 2400, nativeRoute.sequence_of_block_sections.data(), nativeRoute.N_Block_Sections);
		const double head = start - (accelerating ? speed * timestep + 1 : speed * timestep / 2);
		Arc running;
		for (const auto& section : nativeRoute.sequence_of_block_sections)
			for (int arc = 0; arc < section.total_arcs; ++arc) {
				const auto& candidate = section.arcs_in_signalling_block_section[arc];
				if (head >= candidate.startNode.X * 1000 && head < candidate.endNode.X * 1000)
					running = candidate;
			}
		const double limit = accelerating ? running.speedLimit : speed;
		for (auto& section : nativeRoute.sequence_of_block_sections)
			for (int arc = 0; arc < section.total_arcs; ++arc)
				section.arcs_in_signalling_block_section[arc].signalSpeedLimit = limit;
		const double acceleration = (native.tractiveEffort(speed) - native.total_train_resistances(speed, running.gradient, running.curvature)) / (native.total_train_mass * native.massFactor);
		const double candidateSpeed = accelerating ? std::min(limit, speed + acceleration * timestep) : speed;
		const double candidateHead = head + (speed + candidateSpeed) / 2 * timestep;
		const double candidateStart = native.BrakDist_Block(candidateSpeed, 0, 2400, nativeRoute.sequence_of_block_sections.data(), nativeRoute.N_Block_Sections);
		// Non-braking candidate crosses envelope; acceleration case needs candidate speed as well as position.
		QVERIFY(head < start);
		QVERIFY(candidateHead >= candidateStart);
		QVERIFY(!accelerating || candidateHead < start);
		native.CanEnter = true;
		native.OutOfSimulation = false;
		native.departure_time = 0;
		native.counter = 0;
		native.instant_spatial_position[0] = head - speed;
		native.instant_spatial_position[1] = head;
		native.instant_train_speed[0] = native.instant_train_speed[1] = speed;
		native.Xob[1] = native.Vob[1] = 0;
		native.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
		// Accelerating/cruising crossing enters the forward curve before the old braking point.
		QCOMPARE(native.Eq[2], 55);
		QCOMPARE_EQ(native.instant_spatial_position[1], head);
		QCOMPARE_EQ(native.instant_train_speed[1], speed);
	}
	// The obsolete starting-effort bound must not authorize a high-speed phase step.
	{
		Train power = native;
		Arc arc = *power.brakingArc(2350, nativeRoute.sequence_of_block_sections.data(), nativeRoute.N_Block_Sections);
		const double speed = 30, mass = power.total_train_mass * power.massFactor;
		const double resistance = power.total_train_resistances(speed, arc.gradient, arc.curvature);
		const double maximum = speed + (power.tractiveEffort(speed) - resistance) / mass * timestep;
		const double startingMaximum = speed + (power.tractiveEffort(0) - resistance) / mass * timestep;
		QVERIFY2(startingMaximum > maximum, "native high-speed stock has a lower actual-speed effort than starting effort");
		const double invalid = (maximum + startingMaximum) / 2;
		power.instant_spatial_position = {2320, 2350, -1};
		power.instant_train_speed = {speed, speed, -1};
		power.Eq.assign(3, 0);
		power.Sbrak[0] = 2350;
		power.Sbrak[1] = 2350 + invalid * timestep;
		power.Vbrak[0] = speed;
		power.Vbrak[1] = invalid;
		power.BrakStep = 1;
		power.counter = 1;
		power.Xob.assign(3, 2400);
		power.Vob.assign(3, 0);
		power.Xobmin = 2400;
		power.Vobmin = 0;
		power.brakingStep(2, arc, nativeRoute.sequence_of_block_sections.data(), nativeRoute.N_Block_Sections);
		// Actual-speed power-band bound rejects a starting-effort overdraw without rewriting speed.
		QVERIFY(power.instant_train_speed[2] <= maximum + 1e-9);
		QCOMPARE(power.Eq[2], 54);
		QCOMPARE_EQ(power.instant_train_speed[1], speed);
		// Reacceleration in the separate late-target fallback also obeys actual effort.
		arc.gradient = -0.2;
		power.instant_spatial_position = {970, 1000, -1};
		power.instant_train_speed = {speed, speed, -1};
		const double downhillMaximum = speed + (power.tractiveEffort(speed) - power.total_train_resistances(speed, arc.gradient, arc.curvature)) / mass * timestep;
		power.fullBrakingStep(2, arc);
		// Late-target model reacceleration obeys actual-speed power bound and unchanged mean displacement.
		QCOMPARE(power.Eq[2], 54);
		QVERIFY(power.instant_train_speed[2] > speed);
		QVERIFY(power.instant_train_speed[2] <= downhillMaximum + 1e-9);
		QVERIFY(near(power.instant_spatial_position[2], 1000 + (speed + power.instant_train_speed[2]) / 2 * timestep));
	}

	// Start upstream and use the real phase selector, with native crossover stock.
	// The final ten metres change gradient; no fixture file or rolling stock is changed.
	{
		Section& last = nativeRoute.sequence_of_block_sections.back();
		// index + 1 stays inside the 20 arcs of a section.
		const int index = std::min(last.total_arcs, 19) - 1;
		const Arc original = last.arcs_in_signalling_block_section[index];
		// Upstream gradient fixture ends at 5000 metres.
		QCOMPARE_EQ(original.endNode.X, 5.0);
		QVERIFY(last.total_arcs < 20);
		last.arcs_in_signalling_block_section[index].endNode.X = 4.99;
		last.arcs_in_signalling_block_section[index].endNode.station = 0;
		last.arcs_in_signalling_block_section[index].endNode.stationName = "None";
		last.arcs_in_signalling_block_section[index].speedInBraking = native.max_train_speed;
		last.arcs_in_signalling_block_section[index + 1] = original;
		last.arcs_in_signalling_block_section[index + 1].startNode.X = 4.99;
		last.arcs_in_signalling_block_section[index + 1].gradient = 0.005;
		last.arcs_in_signalling_block_section[index + 1].speedInBraking = 0;
		++last.total_arcs;
		for (int station = 0; station < native.numStations; ++station)
			if (native.stationBlockSection[station] == nativeRoute.N_Block_Sections - 1 && native.stationArc[station] == index)
				native.stationArc[station] = index + 1;
		for (auto& section : nativeRoute.sequence_of_block_sections)
			for (int arc = 0; arc < section.total_arcs; ++arc) {
				auto& running = section.arcs_in_signalling_block_section[arc];
				running.signalSpeedLimit = native.max_train_speed;
				running.speedInBraking = running.endNode.station ? 0 : native.max_train_speed;
			}

		native.CanEnter = true;
		native.OutOfSimulation = false;
		native.departure_time = 0;
		native.counter = 0;
		native.instant_spatial_position[0] = 4170;
		native.instant_spatial_position[1] = 4200;
		native.instant_train_speed[0] = native.instant_train_speed[1] = 30;
		native.Xob[1] = native.Vob[1] = 0;
		bool braked = false, stopped = false;
		for (int time = 2; time < 100; ++time) {
			const double oldHead = native.instant_spatial_position[time - 1], oldSpeed = native.instant_train_speed[time - 1];
			native.trajectoryComputationIncludingMovingBlock(time, signalCode1, signalCode2, signalCode3);
			const double nextHead = native.instant_spatial_position[time], nextSpeed = native.instant_train_speed[time];
			if (native.Eq[time] == 55 || native.Eq[time] == 53 || native.Eq[time] == 52) {
				const Arc* running = native.brakingArc(oldHead, nativeRoute.sequence_of_block_sections.data(), nativeRoute.N_Block_Sections);
				const double wheel = native.total_train_mass * native.massFactor * (nextSpeed - oldSpeed) / timestep
					+ native.total_train_resistances(oldSpeed, running->gradient, running->curvature);
				// Feasible curve effort and power use the actual selected speed change.
				QVERIFY(near(native.instant_train_tractive_effort[time], wheel));
				QVERIFY(near(native.instant_train_power_consumption[time], wheel * oldSpeed));
			}

			// Full upstream phase run stays feasible with immutable history.
			QCOMPARE_EQ(native.instant_spatial_position[time - 1], oldHead);
			QCOMPARE_EQ(native.instant_train_speed[time - 1], oldSpeed);
			QVERIFY(nextHead >= oldHead);
			QVERIFY(nextHead <= 5000);
			QVERIFY(native.Eq[time] != 54);
			if (!braked && (native.Eq[time] == 55 || native.Eq[time] == 53 || native.Eq[time] == 52)) {
				braked = true;
			}
			if (nextSpeed == 0) {
				stopped = true;
				// Full upstream real phase run stops at gradient anchor.
				QVERIFY(braked);
				QVERIFY(near(nextHead, 4999.998));
				break;
			}
		}
		// Upstream run selects braking early and completes real stop.
		QVERIFY(braked);
		QVERIFY(stopped);
	}

	// First service entry must survive movement until the existing passenger phase initializes dwell.
	{
		Train service = native;
		const int station = service.numStations - 1;
		service.Stations[station].StepStopped = 0;
		service.Stations[station].StopTime = 0;
		service.ScheduledDepartures[station] = -1;
		service.stop = 1;
		service.instant_spatial_position[1] = service.stationRoutePositionMeters(station) - 0.002;
		service.instant_train_speed[1] = 0;
		service.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
		// Zero-authored dwell retains first service entry for the post-movement passenger initializer.
		QCOMPARE(service.Eq[2], 5);
		QVERIFY(service.StoppedForServiceStop);
		QCOMPARE(service.Stations[station].StepStopped, 1);
		QCOMPARE(service.stop, 2);
		QCOMPARE_EQ(service.instant_train_speed[2], 0.0);
		// One passenger boards, so the passenger phase sets a dwell.
		Passenger boarding;
		boarding.ID = "boarding";
		boarding.CurrentStatus = "OnPlatform";
		boarding.Current_Train_To_Wait = service.trainDescription;
		boarding.Current_WaitingStationID = service.Stations[station].stationName;
		std::list<Passenger> passengers{boarding};
		StationPlatform platform;
		platform.StationID = service.Stations[station].stationName;
		platform.ID = service.Stations[station].stationPlatformId;
		platform.Current_List_Pax_On_Platform = {{boarding.ID, 0.0}};
		std::list<StationPlatform> platforms{platform};
		const bool savedPaxGui = defaultRun().settings.PAX_GUI;
		defaultRun().settings.PAX_GUI = true;
		Simulate_Train_Passenger_Interactions(defaultRun(), 2, initial_variables.startingSimulationTime, service, passengers, platforms);
		defaultRun().settings.PAX_GUI = savedPaxGui;
		QVERIFY2(service.Stations[station].StopTime > 0, "existing passenger formula initializes dwell on the first active service step");
		service.trajectoryComputationIncludingMovingBlock(3, signalCode1, signalCode2, signalCode3);
		// Initialized service dwell retains its entry clock and stationary state.
		QVERIFY(service.StoppedForServiceStop);
		QCOMPARE_EQ(service.instant_train_speed[3], 0.0);
		QCOMPARE(service.stop, 2);
	}
	// Live-target lifecycle and interruptions share one native, feasible stop plan.
	{
		const Route savedRoute = nativeRoute;
		nativeRoute.sequence_of_block_sections = brakingRoute(0, 10, 0);
		nativeRoute.N_Block_Sections = 1;
		nativeRoute.x_of_end_node = 10;
		auto& section = nativeRoute.sequence_of_block_sections[0];
		section.code = 270;
		section.arcs_in_signalling_block_section[0].endNode.X = 5;
		section.arcs_in_signalling_block_section[0].speedInBraking = 0;
		section.arcs_in_signalling_block_section[0].speedLimit = native.max_train_speed;
		section.arcs_in_signalling_block_section[0].signalSpeedLimit = native.max_train_speed;
		Train cached = native;
		cached.numStations = 0;
		cached.Xobmin = 5000;
		cached.Vobmin = 0;
		const double speed = 10;
		const double head = cached.BrakDist_Block(speed, 0, 5000, nativeRoute.sequence_of_block_sections.data(), 1) - 5;
		const bool planned = cached.planBraking(head, speed, section.arcs_in_signalling_block_section[0], nativeRoute.sequence_of_block_sections.data(), 1);
		// Arbitration lifecycle fixture has a feasible native cached stop.
		QVERIFY(planned);
		QVERIFY(cached.BrakStep > 1);
		cached.counter = 1;
		cached.instant_spatial_position[1] = head;
		cached.instant_train_speed[1] = speed;
		cached.Xob[1] = 5000;
		cached.Vob[1] = 0;
		for (int change = 0; change < 4; ++change) {
			Train live = cached;
			section.arcs_in_signalling_block_section[0].speedInBraking = change == 0 ? live.max_train_speed : change == 1 ? 12
																														  : 0;
			if (change == 2)
				live.Sbrak[0] += 1; // Not a matching actual state.
			if (change == 3) {
				live.Vbrak[1] = speed + 100; // Outside the actual-speed traction bound.
				live.Sbrak[1] = head + live.Vbrak[1] * timestep;
			}
			live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
			// Cleared/changed incumbent or mismatched/force-invalid cache cannot retain stale braking.
			if (change < 2) {
				QCOMPARE(live.Eq[2], 1);
				QCOMPARE(live.counter, 0);
			} else {
				QVERIFY(live.Eq[2] != 52);
			}
		}
		const Section baseSection = section;
		for (double ceiling : {0.0, 8.0}) {
			double lowerEnd = 0.0, proposalEnd = 0.0;
			const double minimum = cached.brakingCurveSpeed(speed, baseSection.arcs_in_signalling_block_section[0]);
			const bool bracket = cached.predictBraking(head + minimum * timestep, minimum, ceiling, nativeRoute.sequence_of_block_sections.data(), 1, lowerEnd)
				&& cached.predictBraking(cached.Sbrak[1], cached.Vbrak[1], ceiling, nativeRoute.sequence_of_block_sections.data(), 1, proposalEnd);
			// Native continuation leaves a stricter target opportunity at the previous actual state.
			QVERIFY(bracket);
			QVERIFY(lowerEnd < proposalEnd);
			const double target = (lowerEnd + proposalEnd) / 2 + 0.002;
			nativeRoute.sequence_of_block_sections = brakingRouteOfTwoArcs(target / 1000, 5, 0, 0);
			auto& competing = nativeRoute.sequence_of_block_sections[0];
			competing.code = 270;
			for (int arc = 0; arc < competing.total_arcs; ++arc) {
				competing.arcs_in_signalling_block_section[arc].speedLimit = native.max_train_speed;
				competing.arcs_in_signalling_block_section[arc].signalSpeedLimit = native.max_train_speed;
			}
			competing.arcs_in_signalling_block_section[0].speedInBraking = ceiling;
			competing.arcs_in_signalling_block_section[1].speedInBraking = 0;
			Train live = cached;
			live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
			// New stricter stop/positive target interrupts cached proposal from immutable previous actual state.
			QCOMPARE_EQ(live.Xob[2], competing.arcs_in_signalling_block_section[0].endNode.X * 1000);
			QCOMPARE_EQ(live.Vob[2], ceiling);
			QVERIFY(live.Eq[2] != 52);
			QCOMPARE_EQ(live.instant_spatial_position[1], head);
			QCOMPARE_EQ(live.instant_train_speed[1], speed);
			nativeRoute.sequence_of_block_sections = {baseSection};
		}
		// The cached stop stays upstream of a descent where a farther positive inverse fails.
		nativeRoute.sequence_of_block_sections = brakingRouteOfTwoArcs(5, 10, 0, -1);
		auto& downstream = nativeRoute.sequence_of_block_sections[0];
		downstream.code = 270;
		for (int arc = 0; arc < downstream.total_arcs; ++arc) {
			downstream.arcs_in_signalling_block_section[arc].speedLimit = cached.max_train_speed;
			downstream.arcs_in_signalling_block_section[arc].signalSpeedLimit = cached.max_train_speed;
		}
		downstream.arcs_in_signalling_block_section[0].speedInBraking = 0;
		downstream.arcs_in_signalling_block_section[1].speedInBraking = 8;
		Train live = cached;
		// Farther positive inverse is unavailable beyond the feasible cached stop.
		QCOMPARE_EQ(live.BrakDist_Block(live.Vbrak[1], 8, 10000, nativeRoute.sequence_of_block_sections.data(), 1), -1.0);
		live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
		// Unavailable farther target does not discard a cached plan ending at an earlier stop.
		QCOMPARE_EQ(live.Xob[2], 5000.0);
		QCOMPARE_EQ(live.Vob[2], 0.0);
		QCOMPARE(live.Eq[2], 52);
		QCOMPARE_EQ(live.instant_spatial_position[2], cached.Sbrak[1]);
		QCOMPARE_EQ(live.instant_train_speed[2], cached.Vbrak[1]);
		nativeRoute = savedRoute;
	}
	// Executed Copenhagen geometry: the terminal ceiling recovery loses the farther stop by millimetres.
	{
		const QString path = QDir(QStringLiteral(EGTRAIN_TEST_SOURCE_DIR)).filePath("../Scenes/Copenhagen");
		const auto copenhagen = loadScene(path.toStdString());
		const SceneRunSelection firstService{{"A-Hillerod-Hundige", 1}};
		if (hasErrors(copenhagen.diagnostics) || hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), copenhagen.scene))
			|| hasErrors(buildOperationsFromScene(defaultRun(), copenhagen.scene, "baseline", firstService))
			|| defaultRun().numTrains != 1)
			QFAIL("Copenhagen arbitration fixture prepares committed native stock and route");
		Train live = defaultRun().trains[0];
		Route& route = defaultRun().infra.routes[live.indexOfRoute];
		const double head = 30462.886999999988, speed = 10.616039654442153;
		const double boundary = 30473.999999999989, ceiling = 11.111000000000001, stopTarget = 30544.999999999996;
		// Native builders precede the aspect speed pass; initialize clear runtime targets before imposing this witness.
		for (auto& block : route.sequence_of_block_sections) {
			block.code = 270;
			for (int index = 0; index < block.total_arcs; ++index)
				block.arcs_in_signalling_block_section[index].speedInBraking = live.max_train_speed;
		}
		for (int station = 0; station < live.numStations; ++station)
			route.sequence_of_block_sections[live.stationBlockSection[station]].arcs_in_signalling_block_section[live.stationArc[station]].speedInBraking = 0;
		for (auto& block : route.sequence_of_block_sections)
			for (int index = 0; index < block.total_arcs; ++index)
				if (block.arcs_in_signalling_block_section[index].endNode.X * 1000 == boundary)
					block.arcs_in_signalling_block_section[index].speedInBraking = ceiling;
		live.numStations = 0;
		live.CanEnter = true;
		live.departure_time = 0;
		live.counter = 1;
		live.GradientExceptionInBraking = true;
		live.BrakStep = 1;
		live.Sbrak[0] = head;
		live.Vbrak[0] = speed;
		live.Sbrak[1] = head + ceiling * timestep;
		live.Vbrak[1] = ceiling;
		live.instant_spatial_position[1] = head;
		live.instant_train_speed[1] = speed;
		live.Xob[1] = boundary;
		live.Vob[1] = ceiling;
		const Arc* arc = live.brakingArc(head, route.sequence_of_block_sections.data(), route.N_Block_Sections);
		double end = 0.0;
		// Locally force-valid Copenhagen recovery would consume the competing stop's last opportunity.
		if (arc == nullptr)
			QFAIL("locally force-valid Copenhagen recovery would consume the competing stop's last opportunity");
		QVERIFY(brakingTargetAdmissible(live, head, speed, ceiling, route.sequence_of_block_sections.data(), route.N_Block_Sections));
		QVERIFY(live.predictBraking(live.Sbrak[1], ceiling, 0, route.sequence_of_block_sections.data(), route.N_Block_Sections, end));
		QVERIFY(end > stopTarget - 0.002);
		bool stopped = false;
		for (int time = 2; time < 100; ++time) {
			const double oldHead = live.instant_spatial_position[time - 1], oldSpeed = live.instant_train_speed[time - 1];
			live.trajectoryComputationIncludingMovingBlock(time, signalCode1, signalCode2, signalCode3);
			if (time <= 3) {
				std::cout.precision(17);
				std::cout << "Copenhagen arbitration t=" << time << " target=" << live.Xob[time] << "/" << live.Vob[time]
						  << " state=" << live.instant_spatial_position[time] << "/" << live.instant_train_speed[time] << " eq=" << live.Eq[time] << "\n";
			}
			// Copenhagen stop preempts recovery and retains actual cached continuation despite hypothetical acceleration ceiling.
			QCOMPARE_EQ(live.Xob[time], stopTarget);
			QCOMPARE_EQ(live.Vob[time], 0.0);
			QVERIFY(live.Eq[time] != 54);
			QCOMPARE_EQ(live.instant_spatial_position[time - 1], oldHead);
			QCOMPARE_EQ(live.instant_train_speed[time - 1], oldSpeed);
			QCOMPARE_EQ(live.instant_spatial_position[time], oldHead + live.instant_train_speed[time] * timestep);
			if (live.Eq[time] == 54 || live.Xob[time] != stopTarget)
				break;
			if (live.instant_train_speed[time] == 0) {
				stopped = true;
				QVERIFY2(near(live.instant_spatial_position[time], stopTarget - 0.002), "Copenhagen stop reaches anchored zero without late fallback");
				break;
			}
		}
		// Two-step Copenhagen arbitration finishes the exact stop without rewriting the initial state.
		QVERIFY(stopped);
		QCOMPARE_EQ(live.instant_spatial_position[1], head);
		QCOMPARE_EQ(live.instant_train_speed[1], speed);
	}
}

void TestOperationsBuilder::level4Transitions_data() {
	QTest::addColumn<bool>("reversed");
	QTest::newRow("forward") << false;
	QTest::newRow("reversed") << true;
}

// Real movement calls with native stock and in-memory authority snapshots isolate level-4 transitions.
void TestOperationsBuilder::level4Transitions() {
	QFETCH(bool, reversed);
	const QString fixture = QDir(QStringLiteral(EGTRAIN_TEST_SOURCE_DIR)).filePath("fixtures/scenes/crossovers");
	const auto loaded = loadScene(fixture.toStdString());
	const SceneRunSelection firstF1{{"F1", 1}};
	if (hasErrors(loaded.diagnostics) || hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), loaded.scene))
		|| hasErrors(buildOperationsFromScene(defaultRun(), loaded.scene, "baseline", firstF1)) || defaultRun().numTrains != 1)
		QFAIL("level-4 transition fixture prepares native crossover stock");
	const Train stock = defaultRun().trains[0];
	Route& route = defaultRun().infra.routes[stock.indexOfRoute];
	const Route savedRoute = route;
	const auto savedAuthorities = defaultRun().etcsMa;
	const auto restore = qScopeGuard([&] {
		route = savedRoute;
		defaultRun().etcsMa = savedAuthorities;
	});
	route.sequence_of_block_sections = brakingRoute(0, 10, 0);
	route.N_Block_Sections = 1;
	route.x_of_end_node = 10;
	route.reversed_direction = reversed;
	auto& section = route.sequence_of_block_sections[0];
	section.ID = "level4.block";
	section.GeoXBegNode = reversed ? 10000 : 0;
	section.SignallingLevel = 4;
	section.code = 270;
	auto& arc = section.arcs_in_signalling_block_section[0];
	arc.speedLimit = arc.signalSpeedLimit = arc.speedInBraking = stock.max_train_speed;
	auto authority = [&](double head, const std::string& leader) {
		MovementAuthority ma;
		ma.BSID = section.ID;
		ma.type = "TrainEnd";
		ma.typePart = "Tale";
		ma.ReversedDirection = reversed;
		ma.RelativePosEoA = head;
		ma.AbsPosEoA = reversed ? 10000 - head : head;
		ma.TrainInfo.trainDescription = leader;
		ma.TrainInfo.NextSectionID = "None";
		ma.TrainInfo.TrainSpeed = ma.TrainInfo.Acceleration = 0;
		return ma;
	};
	auto actual = [&](double head, double speed) {
		Train live = stock;
		live.numStations = 0;
		live.CanEnter = true;
		live.OutOfSimulation = false;
		live.departure_time = 0;
		live.counter = live.CounterFollowingMode = 0;
		live.IsTrainInFollowingMode = live.IsTrainCoupling = live.IsTrainDecoupling = false;
		live.IsInUnintentionalDecoupling = live.IsTrainStoppedForEoA = live.BrakingForEoA = false;
		live.StoppedForServiceStop = false;
		live.instant_spatial_position[0] = live.instant_spatial_position[1] = head;
		live.instant_train_speed[0] = live.instant_train_speed[1] = speed;
		live.instant_spatial_position[2] = live.instant_train_speed[2] = -123;
		live.Eq[2] = -123;
		live.Xob[1] = live.Vob[1] = 0;
		return live;
	};
	{
		Train live = actual(1000, 20);
		auto behind = authority(990, "BEHIND");
		behind.TrainInfo.TrainSpeed = 35;
		behind.TrainInfo.Acceleration = -0.5;
		defaultRun().etcsMa = {behind};
		arc.speedInBraking = 0;
		live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
		// A behind braking Tale cannot project into a forward authority in either direction.
		QCOMPARE_EQ(live.Xob[2], 10000.0);
		QVERIFY(!live.BrakingForEoA);
		arc.speedInBraking = stock.max_train_speed;
	}
	{
		Train live = actual(1000, 20);
		defaultRun().etcsMa.clear();
		live.IsTrainInFollowingMode = true;
		live.LeadingTrainInFollowingMode = "FRONT";
		live.Predicted_MA_To_DecoupleAt = authority(1500, "FRONT");
		bool continueCached = false;
		live.europeanVitalComputerWithListsImproved(1000, 20, 1, route.sequence_of_block_sections.data(), 1, continueCached);
		// A stored decoupling authority is a stop target, and the train in following mode that brakes for it is decoupling.
		QCOMPARE_EQ(live.Xobmin, 1500.0);
		QCOMPARE_EQ(live.Vobmin, 0.0);
		QVERIFY(live.BrakingForEoA);
		QVERIFY(live.IsTrainDecoupling);
	}
	{
		Train live = actual(1000, 10);
		auto front = authority(1020, "FRONT");
		front.typePart = "Front";
		front.TrainInfo.TrainSpeed = 10.01;
		defaultRun().etcsMa = {front};
		live.IsTrainCoupling = true;
		live.Predicted_MA_To_CoupleAt = front;
		arc.signalSpeedLimit = 10;
		live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
		QVERIFY2(!live.IsTrainInFollowingMode, "same-direction Front cannot activate stale coupling");
		front.ReversedDirection = !reversed;
		defaultRun().etcsMa = {front};
		live = actual(1000, 10);
		live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
		// Opposing Front remains a stopping authority in either route direction.
		QVERIFY(near(live.Xob[2], 1020));
		QCOMPARE_EQ(live.Vob[2], 0.0);
		QVERIFY(live.BrakingForEoA);
		arc.signalSpeedLimit = stock.max_train_speed;
	}
	for (double leaderSpeed : {0.2, 0.0}) {
		Train live = actual(1000, 10);
		auto leader = authority(1200, "STOPPING");
		leader.TrainInfo.TrainSpeed = leaderSpeed;
		leader.TrainInfo.Acceleration = -0.5;
		defaultRun().etcsMa = {leader};
		const double raw = live.authorityRoutePosition(leader, section);
		double end = 0;
		// A one-step native stop has no right-endpoint displacement at either leader speed.
		QVERIFY(live.predictBraking(raw, leaderSpeed, 0, &section, 1, end));
		QCOMPARE_EQ(end, raw);
		live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
		// Projected stop excludes the inverse anchor holdback and stays fixed when the leader reaches zero.
		QCOMPARE_EQ(live.Xob[2], raw);
		QCOMPARE_EQ(live.Vob[2], 0.0);
	}
	{
		const Arc originalArc = arc;
		const Node originalEnd = section.end_node;
		const double originalRouteEnd = route.x_of_end_node;
		arc.endNode.X = section.end_node.X = route.x_of_end_node = 1.001;
		arc.endNode.station = 1;
		arc.endNode.stationName = stock.Stations[stock.numStations - 1].stationName;
		arc.speedInBraking = 0;
		for (bool dwellComplete : {false, true}) {
			Train live = actual(1000, 0);
			live.numStations = 1;
			live.Stations[0] = stock.Stations[stock.numStations - 1];
			live.stationBlockSection[0] = live.stationArc[0] = 0;
			live.Stations[0].StopTime = dwellComplete ? 0 : 3;
			live.Stations[0].StepStopped = 1;
			live.ScheduledDepartures[0] = 0;
			live.stop = 1;
			live.StoppedForServiceStop = true;
			live.Last_MA_StoppedAt = live.Last_Received_MA = authority(1000.002, "OLD");
			live.IsTrainStoppedForEoA = live.BrakingForEoA = true;
			defaultRun().etcsMa = {authority(1000.005, "NEW")};
			live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
			// Station hold and departure cannot cross a replacement authority within the station window.
			QCOMPARE_EQ(live.instant_spatial_position[2], 1000.0);
			QCOMPARE_EQ(live.instant_train_speed[2], 0.0);
			QVERIFY(live.StoppedForServiceStop);
			QCOMPARE(live.stop, 1);
			QVERIFY(near(live.Xob[2], 1000.005));
			if (dwellComplete) {
				defaultRun().etcsMa.clear();
				live.trajectoryComputationIncludingMovingBlock(3, signalCode1, signalCode2, signalCode3);
				// Completed station service can depart once the replacement authority clears.
				QVERIFY(!live.StoppedForServiceStop);
				QVERIFY(live.instant_train_speed[3] > 0);
			}
		}
		arc = originalArc;
		section.end_node = originalEnd;
		route.x_of_end_node = originalRouteEnd;
	}
	const auto saved = authority(1000.002, "SAVED");
	{
		Train live = actual(1000, 0);
		live.Last_MA_StoppedAt = live.Last_Received_MA = saved;
		live.IsTrainStoppedForEoA = live.BrakingForEoA = true;
		auto failure = authority(1000.001, "signal_failure:test");
		failure.type = "SignalFailure"; // A synthetic competing snapshot, not a canonical incident position.
		defaultRun().etcsMa = {saved, failure};
		live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
		// Saved Tale holds actual state, not the newly selected failure position.
		QVERIFY(live.IsTrainStoppedForEoA);
		QCOMPARE_EQ(live.instant_spatial_position[2], 1000.0);
		QCOMPARE_EQ(live.instant_train_speed[2], 0.0);
		QCOMPARE(live.Last_Received_MA.type, "SignalFailure");
	}
	for (bool replacement : {false, true}) {
		Train live = actual(1000, 0);
		live.Last_MA_StoppedAt = live.Last_Received_MA = saved;
		live.IsTrainStoppedForEoA = live.BrakingForEoA = true;
		live.stop = 1;
		defaultRun().etcsMa.clear();
		if (replacement)
			defaultRun().etcsMa.push_back(authority(1000.003, "NEW"));
		live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
		// Removed saved Tale releases to the actual-state successor, retaining new near Tale or open acceleration.
		QVERIFY(!live.IsTrainStoppedForEoA);
		if (replacement) {
			QCOMPARE(live.Eq[2], 54);
			QCOMPARE_EQ(live.instant_spatial_position[2], 1000.0);
			QCOMPARE_EQ(live.instant_train_speed[2], 0.0);
			QVERIFY(near(live.Xob[2], 1000.003));
			QVERIFY(live.BrakingForEoA);
			QCOMPARE(live.stop, 1);
		} else {
			QCOMPARE(live.Eq[2], 1);
			QVERIFY(live.instant_spatial_position[2] > 1000);
			QVERIFY(live.instant_train_speed[2] > 0);
		}
	}
	for (int mismatch = 0; mismatch < 6; ++mismatch) {
		Train live = actual(1000, 0);
		live.Last_MA_StoppedAt = saved;
		live.IsTrainStoppedForEoA = live.BrakingForEoA = true;
		auto changed = saved;
		if (mismatch == 0) changed.type = "SignalFailure";
		if (mismatch == 1) changed.typePart = "Front";
		if (mismatch == 2) changed.ReversedDirection = !reversed;
		if (mismatch == 3) changed.AbsPosEoA += 1;
		if (mismatch == 4) live.Last_MA_StoppedAt.RelativePosEoA = 999;
		if (mismatch == 5) live.Last_MA_StoppedAt.RelativePosEoA = 1005;
		defaultRun().etcsMa = {changed};
		live.checkIfMovementAuthorityStillValid(0, 1000, section);
		// Saved hold rejects changed identity, moved authority, behind or out-of-range distance.
		QVERIFY(!live.IsTrainStoppedForEoA);
		QVERIFY(!live.BrakingForEoA);
	}
	{
		Train live = actual(1000, 0);
		auto behind = authority(999, "BEHIND");
		live.Last_Received_MA = behind;
		live.BrakingForEoA = true;
		defaultRun().etcsMa = {behind};
		live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
		// A behind authority cannot be captured as a stationary hold.
		QVERIFY(!live.IsTrainStoppedForEoA);
		QVERIFY(live.instant_spatial_position[2] > 1000);
	}
	{
		// Cache identity uses the same route conversion as the published authority, including mirrored roundoff.
		const double target = reversed ? section.GeoXBegNode - saved.AbsPosEoA + section.start_node.X * 1000 : saved.AbsPosEoA;
		const double terminal = target - 0.002;
		Train live = actual(terminal, 0.1);
		live.IsTrainInFollowingMode = true;
		live.LeadingTrainInFollowingMode = saved.TrainInfo.trainDescription;
		live.CounterFollowingMode = live.counter = 1;
		live.BrakStep = 1;
		live.Sbrak[0] = live.Sbrak[1] = terminal;
		live.Vbrak[0] = 0.1;
		live.Vbrak[1] = 0;
		live.Xob[1] = target;
		live.Vob[1] = 0;
		defaultRun().etcsMa = {saved};
		live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
		// Active following cannot replace a native anchored zero-speed terminal.
		QCOMPARE(live.Eq[2], 52);
		QCOMPARE_EQ(live.instant_spatial_position[2], terminal);
		QCOMPARE_EQ(live.instant_train_speed[2], 0.0);
	}
	{
		Train control = actual(1000, 10);
		auto front = authority(1020, "FRONT_ONLY");
		front.typePart = "Front";
		front.TrainInfo.TrainSpeed = 10;
		defaultRun().etcsMa = {front};
		arc.signalSpeedLimit = 10;
		Train live = control;
		live.IsTrainInFollowingMode = true;
		live.LeadingTrainInFollowingMode = "FRONT_ONLY";
		live.CounterFollowingMode = 1;
		control.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
		live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
		// An ineligible live Front produces no following proposal and preserves the complete normal result.
		QCOMPARE(live.Eq[2], control.Eq[2]);
		QCOMPARE_EQ(live.instant_spatial_position[2], control.instant_spatial_position[2]);
		QCOMPARE_EQ(live.instant_train_speed[2], control.instant_train_speed[2]);
		QCOMPARE(live.counter, control.counter);
		QCOMPARE_EQ(live.instant_train_tractive_effort[2], control.instant_train_tractive_effort[2]);
		QCOMPARE_EQ(live.instant_train_energy_consumption[2], control.instant_train_energy_consumption[2]);
		arc.signalSpeedLimit = stock.max_train_speed;
	}
	for (int intent = 0; intent < 3; ++intent) {
		Train live = actual(1000, 10);
		auto leader = authority(1020, "LIVE");
		leader.TrainInfo.TrainSpeed = intent == 0 ? 10 : intent == 1 ? 1
																	 : 9.9;
		leader.TrainInfo.Acceleration = intent == 0 ? 0 : intent == 1 ? -0.5
																	  : -0.9;
		defaultRun().etcsMa = {leader};
		live.IsTrainInFollowingMode = true;
		live.LeadingTrainInFollowingMode = "LIVE";
		live.CounterFollowingMode = 1;
		arc.signalSpeedLimit = 10;
		const double minimum = live.brakingCurveSpeed(10, arc);
		live.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3);
		// Force-invalid leader-relative intent preserves braking; admissible nonbraking following still runs.
		QVERIFY(live.instant_train_speed[2] >= minimum);
		QVERIFY(live.instant_train_speed[2] <= 10);
		if (intent == 1)
			QVERIFY(live.Eq[2] != 1002);
		else
			QCOMPARE(live.Eq[2], 1002);
		if (intent != 1) {
			const double effort = live.total_train_mass * live.massFactor * (live.instant_train_speed[2] - 10) / timestep
				+ live.total_train_resistances(10, arc.gradient, arc.curvature);
			// Accepted following effort and power use its own actual speed change and resistance.
			QVERIFY(near(live.instant_train_tractive_effort[2], effort));
			QVERIFY(near(live.instant_train_power_consumption[2], effort * 10));
		}
		arc.signalSpeedLimit = stock.max_train_speed;
	}
}

void TestOperationsBuilder::stationaryBrakingCompletesDwell() {
	const QString fixture = QDir(QStringLiteral(EGTRAIN_TEST_SOURCE_DIR)).filePath("fixtures/scenes/crossovers");
	const auto loaded = loadScene(fixture.toStdString());
	const SceneRunSelection firstF1{{"F1", 1}};
	if (hasErrors(loaded.diagnostics) || hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), loaded.scene))
		|| hasErrors(buildOperationsFromScene(defaultRun(), loaded.scene, "baseline", firstF1)) || defaultRun().numTrains != 1)
		QFAIL("level-4 transition fixture prepares native crossover stock");
	const auto savedAuthorities = defaultRun().etcsMa;
	const auto restoreAuthorities = qScopeGuard([&savedAuthorities] { defaultRun().etcsMa = savedAuthorities; });
	defaultRun().etcsMa.clear();
	Train service = defaultRun().trains[0];
	const int station = service.numStations - 1;
	const double head = service.stationRoutePositionMeters(station) - 0.002;
	service.CanEnter = true;
	service.departure_time = 0;
	service.counter = 0;
	service.Stations[station].StepStopped = 0;
	service.Stations[station].StopTime = 3;
	service.ScheduledDepartures[station] = 7;
	service.instant_spatial_position[1] = head;
	service.instant_train_speed[1] = 0;
	for (int time = 2; time <= 8; ++time) {
		service.trajectoryComputationIncludingMovingBlock(time, signalCode1, signalCode2, signalCode3);
		// Zero-speed braking fallback preserves the first service entry clock throughout dwell.
		QCOMPARE(service.stop, 2);
		QCOMPARE_EQ(service.Xob[time], service.stationRoutePositionMeters(station));
		// Real-path stationary braking completes dwell and scheduled departure without resetting the clock.
		if (time <= 7) {
			QVERIFY(service.StoppedForServiceStop);
			QCOMPARE_EQ(service.instant_train_speed[time], 0.0);
		} else {
			QVERIFY(!service.StoppedForServiceStop);
			QVERIFY(service.instant_train_speed[time] > 0);
			QVERIFY(service.instant_spatial_position[time] > service.stationRoutePositionMeters(station));
		}
	}
}

void TestOperationsBuilder::signalFailureEvidence_data() {
	QTest::addColumn<bool>("reversed");
	QTest::newRow("forward") << false;
	QTest::newRow("reversed") << true;
}

// A route of three sections of 1 km with a failed third section. The leading train stands at the red aspect of the second
// section and the train behind it stands at the red aspect of the first, held by the leader.
void TestOperationsBuilder::signalFailureEvidence() {
	QFETCH(bool, reversed);
	const QString fixture = QDir(QStringLiteral(EGTRAIN_TEST_SOURCE_DIR)).filePath("fixtures/scenes/crossovers");
	const auto loaded = loadScene(fixture.toStdString());
	const SceneRunSelection firstF1{{"F1", 1}};
	if (hasErrors(loaded.diagnostics) || hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), loaded.scene))
		|| hasErrors(buildOperationsFromScene(defaultRun(), loaded.scene, "baseline", firstF1)) || defaultRun().numTrains != 1)
		QFAIL("signal failure evidence fixture prepares native crossover stock");
	const Train stock = defaultRun().trains[0];
	Route& route = defaultRun().infra.routes[stock.indexOfRoute];
	const Route savedRoute = route;
	const auto savedAuthorities = defaultRun().etcsMa;
	const auto restore = qScopeGuard([&] {
		route = savedRoute;
		defaultRun().etcsMa = savedAuthorities;
	});
	route.sequence_of_block_sections = {brakingRoute(0, 1, 0)[0], brakingRoute(1, 2, 0)[0], brakingRoute(2, 3, 0)[0]};
	route.N_Block_Sections = 3;
	route.x_of_end_node = 3;
	route.reversed_direction = reversed;
	for (int k = 0; k < 3; ++k) {
		Section& section = route.sequence_of_block_sections[k];
		section.ID = "evidence." + std::to_string(k);
		section.GeoXBegNode = reversed ? 10000 : 0;
		section.SignallingLevel = 0;
		section.code = 270;
		section.state = k < 2 ? SignalState::Red : SignalState::Green;
		Arc& arc = section.arcs_in_signalling_block_section[0];
		arc.speedLimit = arc.signalSpeedLimit = arc.speedInBraking = stock.max_train_speed;
	}
	// The authority a failure of the third section gives, anchored at the end of the second.
	auto failure = [&](const std::string& description) {
		MovementAuthority ma;
		ma.BSID = "evidence.1";
		ma.type = "SignalFailure";
		ma.typePart = "Front";
		ma.ReversedDirection = reversed;
		ma.EoA_Dist_From_BSID_Beg = 1000;
		ma.AbsPosEoA = reversed ? 9000 : 2000;
		ma.TrainInfo.trainDescription = description;
		ma.TrainInfo.NextSectionID = "evidence.2";
		return ma;
	};
	auto standing = [&](double head) {
		Train live = stock;
		live.numStations = 0;
		live.CanEnter = true;
		live.OutOfSimulation = false;
		live.departure_time = 0;
		live.counter = live.CounterFollowingMode = 0;
		live.IsTrainInFollowingMode = live.IsTrainCoupling = live.IsTrainDecoupling = false;
		live.IsInUnintentionalDecoupling = live.IsTrainStoppedForEoA = live.BrakingForEoA = false;
		live.StoppedForServiceStop = false;
		live.directIncidentIds.clear();
		live.firstDirectIncidentTime = live.firstDirectIncidentLocation = -1.0;
		live.instant_spatial_position[0] = live.instant_spatial_position[1] = head;
		live.instant_train_speed[0] = live.instant_train_speed[1] = 0;
		live.Xob[1] = live.Vob[1] = 0;
		return live;
	};
	const auto runStep = [](Train& train) { train.trajectoryComputationIncludingMovingBlock(2, signalCode1, signalCode2, signalCode3); };
	defaultRun().etcsMa = {failure("signal_failure:incident.test")};
	Train leader = standing(1999.9999);
	Train follower = standing(999.9999);
	runStep(leader);
	runStep(follower);
	// A train held at the red aspect before a failed section records the failure on the first step.
	QVERIFY(leader.directIncidentIds == std::vector<std::string>({"incident.test"}));
	QCOMPARE_EQ(leader.firstDirectIncidentTime, 2 * timestep);
	QCOMPARE_EQ(leader.firstDirectIncidentLocation, defaultRun().etcsMa.front().AbsPosEoA);
	QCOMPARE_EQ(leader.instant_train_speed[2], 0.0);
	// A train held only by the train ahead records no failure.
	QVERIFY(follower.directIncidentIds.empty());
	QCOMPARE_EQ(follower.instant_train_speed[2], 0.0);
	Train shortOfEntry = standing(1950);
	runStep(shortOfEntry);
	QVERIFY2(shortOfEntry.directIncidentIds.empty(), "a train that stands 50 m before the failed section records no failure");
	route.sequence_of_block_sections[1].state = SignalState::Green;
	Train clearAspect = standing(1999.9999);
	runStep(clearAspect);
	QVERIFY2(clearAspect.directIncidentIds.empty(), "a train that stands at an aspect that is not red records no failure");
	route.sequence_of_block_sections[1].state = SignalState::Red;
	MovementAuthority elsewhere = failure("signal_failure:incident.test");
	elsewhere.TrainInfo.NextSectionID = "evidence.9";
	defaultRun().etcsMa = {elsewhere};
	Train otherRoute = standing(1999.9999);
	runStep(otherRoute);
	QVERIFY2(otherRoute.directIncidentIds.empty(), "a failed section that does not follow on the route of the train is not recorded");
	MovementAuthority opposing = failure("signal_failure:incident.test");
	opposing.ReversedDirection = !reversed;
	defaultRun().etcsMa = {opposing};
	Train otherDirection = standing(1999.9999);
	runStep(otherDirection);
	QVERIFY2(otherDirection.directIncidentIds.empty(), "a failure authority of the opposite direction is not recorded");
	defaultRun().etcsMa = {failure("single_track:lock.1")};
	Train singleTrack = standing(1999.9999);
	runStep(singleTrack);
	QVERIFY2(singleTrack.directIncidentIds.empty(), "a held single-track section is not recorded as a signal failure");
}

// Passenger time windows of the fixture after a scene is prepared with a seed.
static std::vector<double> sampledWindows(const SceneModel& scene, unsigned long seed) {
	initial_variables.randomSeed = seed;
	std::vector<double> windows;
	if (hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), scene))
		|| hasErrors(buildOperationsFromScene(defaultRun(), scene, "scenario.base")))
		return windows;
	for (const Passenger& passenger : defaultRun().passengers)
		for (const Journey& journey : passenger.Journeys) {
			windows.push_back(journey.Actual_Planned_Departure_Time);
			windows.push_back(journey.Actual_Planned_Arrival_Time);
		}
	return windows;
}

void TestOperationsBuilder::seedsGiveRepeatableDraws() {
	const SceneModel scene = completeScene();
	const unsigned long savedSeed = initial_variables.randomSeed;
	const auto restoreSeed = qScopeGuard([savedSeed] {
		initial_variables.randomSeed = savedSeed;
		defaultRun().numberGenerator = NumberGenerator(kDefaultRandomSeed);
	});
	Train train;
	train.run_ = &defaultRun();
	train.number_of_wagons = 3.0;
	train.MAX_OnBoard_Passengers = trainPassengerCapacity(train.number_of_wagons);
	const double dwellA = seededDwellTime(train, 0.5, 800, 800, kSeedA);
	QVERIFY2(near(dwellA, seededDwellTime(train, 0.5, 800, 800, kSeedA)), "equal seeds give equal dwell times");
	QVERIFY2(!near(dwellA, seededDwellTime(train, 0.5, 800, 800, kSeedB)), "different seeds give different dwell times");

	// Draws continue from the generator state instead of restarting for every car.
	defaultRun().numberGenerator = NumberGenerator(kSeedA);
	const double firstStop = train.computePaxDependentDwellTimeAtStations(800, 800, 0.5,
		7.0f, 0.32f, 18.23f, 0.564f, 4.838f, 22.24f, 0.04f, 0.562f);
	const double secondStop = train.computePaxDependentDwellTimeAtStations(800, 800, 0.5,
		7.0f, 0.32f, 18.23f, 0.564f, 4.838f, 22.24f, 0.04f, 0.562f);
	// A second stop draws on from the first.
	QVERIFY(near(firstStop, dwellA));
	QVERIFY(!near(firstStop, secondStop));

	const std::vector<double> windowsA = sampledWindows(scene, kSeedA);
	// The fixture has two sampled journeys.
	QCOMPARE(windowsA.size(), std::size_t(4));
	// Equal seeds sample equal passenger windows.
	QCOMPARE(sampledWindows(scene, kSeedA), windowsA);
	QVERIFY2(sampledWindows(scene, kSeedB) != windowsA, "different seeds sample different passenger windows");
	// A run with another seed in between does not change a rerun.
	QCOMPARE(sampledWindows(scene, kSeedA), windowsA);
	// The seed of the run is kept in the run settings.
	QCOMPARE(initial_variables.randomSeed, kSeedA);
}

// Neither the dwell computation nor the scene preparation leaves a file in the working directory.
void TestOperationsBuilder::preparesWithoutFileAccess() {
	const SceneModel scene = completeScene();
	const unsigned long savedSeed = initial_variables.randomSeed;
	const auto restoreSeed = qScopeGuard([savedSeed] {
		initial_variables.randomSeed = savedSeed;
		defaultRun().numberGenerator = NumberGenerator(kDefaultRandomSeed);
	});
	QTemporaryDir workDir;
	const QString previousDir = QDir::currentPath();
	// Temporary working directory.
	QVERIFY(workDir.isValid());
	QVERIFY(QDir::setCurrent(workDir.path()));
	const auto restoreDir = qScopeGuard([&previousDir] { QDir::setCurrent(previousDir); });
	Train train;
	train.run_ = &defaultRun();
	train.number_of_wagons = 3.0;
	train.MAX_OnBoard_Passengers = trainPassengerCapacity(train.number_of_wagons);
	seededDwellTime(train, 0.5, 800, 800);
	QVERIFY2(!sampledWindows(scene, kSeedA).empty(), "the scene is prepared in the temporary directory");
	const QStringList entries = QDir(workDir.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
	QVERIFY2(entries.isEmpty(), ("no file appears in the working directory (found: " + entries.join(",").toStdString() + ")").c_str());
	QVERIFY2(QDir::setCurrent(previousDir), "working directory restored");
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

// Sections 0 to 3 of the level west and 4 to 7 of the level east, after the signalling step has run on them as a route, for a
// train whose tail is in the section tail and whose head is in the next one.
static std::vector<Section> borderAspects(int west, int east, int tail) {
	std::vector<Section> sections = boundarySections(8);
	for (int i = 0; i < 8; ++i) {
		sections[i].SignallingLevel = i < 4 ? west : east;
		sections[i].code = 270;
		sections[i].arcs_in_signalling_block_section[0].signalSpeedLimit = 999;
	}
	std::vector<Route> savedRoutes;
	savedRoutes.swap(defaultRun().infra.routes);
	const int savedRouteCount = defaultRun().infra.numRoutes;
	const auto savedOccupied = defaultRun().blocksOccupied;
	defaultRun().infra.routes.resize(1);
	defaultRun().infra.routes[0].N_Block_Sections = 8;
	defaultRun().infra.routes[0].sequence_of_block_sections = std::move(sections);
	defaultRun().infra.numRoutes = 1;
	defaultRun().infra.numberRouteSections();
	defaultRun().blocksOccupied = {defaultRun().infra.routes[0].sequence_of_block_sections[tail].ID,
		defaultRun().infra.routes[0].sequence_of_block_sections[tail + 1].ID};
	activateMixedSignallingSystem(defaultRun());
	sections = std::move(defaultRun().infra.routes[0].sequence_of_block_sections);
	defaultRun().blocksOccupied = savedOccupied;
	defaultRun().infra.numRoutes = savedRouteCount;
	defaultRun().infra.routes.swap(savedRoutes);
	defaultRun().infra.numberRouteSections();
	return sections;
}

static double borderLimit(const Section& section) {
	return section.arcs_in_signalling_block_section[0].signalSpeedLimit;
}

// The codes of the sections first to last, each followed by a space.
static std::string aspectChain(const std::vector<Section>& sections, int first, int last) {
	std::string codes;
	for (int i = first; i <= last; ++i)
		codes += std::to_string(static_cast<int>(sections[i].code)) + " ";
	return codes;
}

void TestOperationsBuilder::levelBorderAspects_data() {
	QTest::addColumn<int>("level");
	QTest::newRow("level 1") << 1;
	QTest::newRow("level 2") << 2;
}

// At a border between level 0 and level 1 or 2 an occupied section keeps code 0 and the sections behind it get the
// chain of their own level, on both sides of the border.
void TestOperationsBuilder::levelBorderAspects() {
	QFETCH(int, level);
	const std::string name = "level " + std::to_string(level);
	// The tail is in the last section of level 0 and the head in the first section of the other level.
	std::vector<Section> sections = borderAspects(0, level, 3);
	const std::string headInLevel = "a section of level 0 keeps code 0 while the head of its train is in " + name;
	QVERIFY2(aspectChain(sections, 0, 4) == "270 180 75 0 0 ", headInLevel.c_str());
	QVERIFY2(borderLimit(sections[2]) == signalCode1, headInLevel.c_str());
	QVERIFY2(borderLimit(sections[1]) == 999, headInLevel.c_str());
	// The train is two sections past the border, so only the chain behind it crosses the border.
	sections = borderAspects(0, level, 5);
	const std::string behindLevel = "a section of level 0 behind an occupied section of " + name + " keeps the chain of level 0";
	QVERIFY2(aspectChain(sections, 2, 6) == "270 180 75 0 0 ", behindLevel.c_str());
	QVERIFY2(borderLimit(sections[4]) == 999, behindLevel.c_str());
	QVERIFY2(borderLimit(sections[3]) == 999, behindLevel.c_str());
	// The same two positions with the levels the other way round.
	sections = borderAspects(level, 0, 3);
	const std::string headInLevel0 = "a section of " + name + " keeps code 0 while the head of its train is in level 0";
	QVERIFY2(aspectChain(sections, 0, 4) == "270 180 75 0 0 ", headInLevel0.c_str());
	QVERIFY2(borderLimit(sections[2]) == 999, headInLevel0.c_str());
	sections = borderAspects(level, 0, 4);
	QVERIFY2(aspectChain(sections, 1, 5) == "270 180 75 0 0 ",
		("a section of " + name + " before an occupied section of level 0 shows 75").c_str());
	sections = borderAspects(level, 0, 5);
	QVERIFY2(aspectChain(sections, 2, 6) == "270 180 75 0 0 ",
		("the sections of " + name + " continue the chain behind a section of level 0 with code 75").c_str());
	const std::string keepsLimit = "the routine of " + name + " leaves the speed limit of a level 0 section with code 75";
	QVERIFY2(borderLimit(sections[4]) == signalCode1, keepsLimit.c_str());
	QVERIFY2(borderLimit(sections[3]) == 999, keepsLimit.c_str());
	QVERIFY2(borderLimit(sections[2]) == 999, keepsLimit.c_str());
	// A section without a level takes the chain of an occupied section of level 1 or 2 and of no other level.
	sections = borderAspects(kSignallingLevelUnset, level, 4);
	QVERIFY2(aspectChain(sections, 1, 5) == "270 180 75 0 0 ",
		("a section without a level takes the chain behind an occupied section of " + name).c_str());
}

void TestOperationsBuilder::unsetLevelTakesNoChainOfLevel0() {
	const std::vector<Section> sections = borderAspects(kSignallingLevelUnset, 0, 4);
	// A section without a level takes no chain behind an occupied section of level 0.
	QCOMPARE(aspectChain(sections, 1, 5), "270 270 270 0 0 ");
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
void TestOperationsBuilder::firstRouteSectionWithVirtualSignal() {
	std::vector<Route> savedRoutes;
	savedRoutes.swap(defaultRun().infra.routes);
	defaultRun().infra.routes.resize(1);
	defaultRun().infra.routes[0].x_of_end_node = 100.0;
	const double savedDelay = defaultRun().signallingDelay;
	const auto savedOccupied = defaultRun().blocksOccupied;
	const auto savedConnected = defaultRun().blocksConnected;
	const auto restore = qScopeGuard([&] {
		defaultRun().signallingDelay = savedDelay;
		defaultRun().blocksOccupied = savedOccupied;
		defaultRun().blocksConnected = savedConnected;
		defaultRun().infra.routes.swap(savedRoutes);
	});
	defaultRun().signallingDelay = 0.0;

	// The tail is in the second section of the route, which follows a first section that starts with a
	// virtual signal. The first section is the second half of a double switch over two sections of the infrastructure.
	// The section before the route is a double switch too and is poisoned for a sanitizer, so that
	// reading it is reported, and it releases visibly when it is read in a plain build.
	const int savedBlocks = defaultRun().infra.numBlocks;
	std::vector<Section> savedSections(2);
	savedSections.swap(defaultRun().infra.sections);
	defaultRun().infra.numBlocks = 2;
	defaultRun().infra.sections[0].ID = "@boundary.a@";
	defaultRun().infra.sections[1].ID = "@boundary.b@";
	std::vector<Section> storage = boundarySections(3);
	Section* sections = storage.data() + 1;
	storage[0].ID = "@boundary.a@-2.0/@boundary.b@-2.0";
	sections[0].ID = "@boundary.a@-1.0/@boundary.b@-1.0";
	sections[0].start_node.virtualSignal = true;
	Train train;
	train.run_ = &defaultRun();
	placeBoundaryTrain(train, 5000.0);
	defaultRun().blocksOccupied.clear();
	defaultRun().blocksConnected.clear();
#ifdef TEST_ADDRESS_SANITIZER
	ASAN_POISON_MEMORY_REGION(&storage[0], sizeof(Section));
#endif
	train.Det_Section_Occupied_By_Train(1, sections, 2);
#ifdef TEST_ADDRESS_SANITIZER
	ASAN_UNPOISON_MEMORY_REGION(&storage[0], sizeof(Section));
#endif
	const bool beforeRouteUntouched = std::find(defaultRun().blocksConnected.begin(), defaultRun().blocksConnected.end(),
										  storage[0].ID)
		== defaultRun().blocksConnected.end();
	savedSections.swap(defaultRun().infra.sections);
	defaultRun().infra.numBlocks = savedBlocks;
	// A first section that starts with a virtual signal has no double switch before it to release.
	QCOMPARE(defaultRun().blocksOccupied.size(), std::size_t(1));
	QCOMPARE(defaultRun().blocksOccupied.front(), sections[1].ID);
	QVERIFY(beforeRouteUntouched);
}

// The trains of the run are exactly those of the last build, and a reset releases them.
void TestOperationsBuilder::trainStorageHoldsLastBuild() {
	const auto clearLists = qScopeGuard([] {
		defaultRun().passengers.clear();
		defaultRun().platforms.clear();
	});
	const auto buildTrains = [](int count) {
		SceneModel scene = completeScene();
		scene.services[0].hasRepeatCount = true;
		scene.services[0].repeatCount = count;
		const auto infrastructure = buildInfrastructureAndSignallingFromScene(defaultRun(), scene);
		return hasErrors(infrastructure) ? infrastructure : buildOperationsFromScene(defaultRun(), scene, "scenario.base");
	};
	for (const int count : {3, 5, 2}) {
		QVERIFY2(!hasErrors(buildTrains(count)), "train storage fixture builds");
		// The train storage holds exactly the trains of the build.
		QCOMPARE(defaultRun().numTrains, count);
		QCOMPARE(defaultRun().trains.size(), static_cast<std::size_t>(count));
		QCOMPARE(defaultRun().trains.capacity(), static_cast<std::size_t>(count));
	}

	// A platform list that an infrastructure build fills again to the same size is not described by the old index.
	const auto platformCountBefore = defaultRun().platforms.size();
	// An infrastructure build clears the platform index.
	QVERIFY(!hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), completeScene())));
	QCOMPARE(defaultRun().platforms.size(), platformCountBefore);
	QVERIFY(!defaultRun().platformIndex.covers(defaultRun().platforms));
	QVERIFY2(!hasErrors(buildTrains(2)), "train storage fixture builds again");

	const Regional* const keptStorage = defaultRun().trains.data();
	SceneModel rejected = completeScene();
	// A rejected build leaves the train storage untouched.
	QVERIFY(hasErrors(buildOperationsFromScene(defaultRun(), rejected, "scenario.missing")));
	QVERIFY(defaultRun().trains.data() == keptStorage);
	QCOMPARE(defaultRun().trains.size(), std::size_t(2));
	QVERIFY(!defaultRun().trains[0].Stations.empty());

	// A build indexes the passengers and the platforms.
	QVERIFY(defaultRun().passengerIndex.covers(defaultRun().passengers));
	QVERIFY(defaultRun().platformIndex.covers(defaultRun().platforms));
	const std::size_t passengerCount = defaultRun().passengers.size();
	const std::size_t platformCount = defaultRun().platforms.size();
	resetNativeOperationsState(defaultRun());
	// A reset releases the train storage.
	QCOMPARE(defaultRun().numTrains, 0);
	QVERIFY(defaultRun().trains.empty());
	QCOMPARE(defaultRun().trains.capacity(), std::size_t(0));
	defaultRun().passengers.resize(passengerCount);
	defaultRun().platforms.resize(platformCount);
	// A reset clears the indexes of lists that are filled again to the same size.
	QVERIFY(!defaultRun().passengerIndex.covers(defaultRun().passengers));
	QVERIFY(!defaultRun().platformIndex.covers(defaultRun().platforms));
}

// A single-track section is held by the direction of the trains in it. Trains of the other direction see
// its sections as occupied, trains of the same direction do not, and the sections are released when it
// changes holder or becomes free.
void TestOperationsBuilder::singleTrackLocks() {
	std::vector<Route> savedRoutes;
	savedRoutes.swap(defaultRun().infra.routes);
	const auto savedLimits = defaultRun().infra.singleTrackLimits;
	const auto savedOccupied = defaultRun().blocksOccupied;
	const auto savedConnected = defaultRun().blocksConnected;
	const auto savedHeld = defaultRun().infra.singleTrackHeld;
	const int savedRegions = defaultRun().numTrains;
	// Two trains of its own, whatever the last build left in the storage.
	std::vector<Regional> savedTrains(2);
	savedTrains.swap(defaultRun().trains);
	const double savedDelay = defaultRun().signallingDelay;
	const auto savedAuthorities = defaultRun().etcsMa;
	const int savedRouteCount = defaultRun().infra.numRoutes;
	const auto restore = qScopeGuard([&] {
		defaultRun().signallingDelay = savedDelay;
		defaultRun().numTrains = savedRegions;
		defaultRun().trains.swap(savedTrains);
		defaultRun().infra.singleTrackLimits = savedLimits;
		defaultRun().infra.singleTrackHeld = savedHeld;
		defaultRun().blocksOccupied = savedOccupied;
		defaultRun().blocksConnected = savedConnected;
		defaultRun().etcsMa = savedAuthorities;
		defaultRun().infra.numRoutes = savedRouteCount;
		resetSingleTrackLocks(defaultRun());
		defaultRun().infra.routes.swap(savedRoutes);
		defaultRun().infra.numberRouteSections();
	});
	defaultRun().signallingDelay = 0.0;

	// Two routes over sections lock.0 to lock.5 of 2 km: route 0 forward, route 1 the same sections reversed.
	defaultRun().infra.routes.resize(2);
	for (int r = 0; r < 2; ++r) {
		Route& route = defaultRun().infra.routes[r];
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
	defaultRun().infra.numberRouteSections();
	defaultRun().infra.singleTrackLimits.clear();
	defaultRun().infra.singleTrackLimits.emplace_back("lock.1", "lock.4", "lock.0", "lock.5");
	resetSingleTrackLocks(defaultRun());

	// The zone of a route is derived once: neighbouring sections form one interval of route position.
	// Neighbouring zone sections merge into one interval.
	QCOMPARE(singleTrackZone(defaultRun(), 0, 0).intervals.size(), std::size_t(1));
	QCOMPARE_EQ(singleTrackZone(defaultRun(), 0, 0).intervals[0].first, 0.0);
	QCOMPARE_EQ(singleTrackZone(defaultRun(), 0, 0).intervals[0].second, 12000.0);
	QCOMPARE(singleTrackZone(defaultRun(), 0, 0).sectionIDs.size(), std::size_t(6));
	// The zone sections of a route are listed in route order.
	QCOMPARE(singleTrackZone(defaultRun(), 0, 1).sectionIDs.size(), std::size_t(6));
	QCOMPARE(singleTrackZone(defaultRun(), 0, 1).sectionIDs.front(), "lock.5");
	// A limit or a route that does not exist has no zone.
	QVERIFY(singleTrackZone(defaultRun(), 1, 0).intervals.empty());
	QVERIFY(singleTrackZone(defaultRun(), 0, 2).intervals.empty());

	auto place = [](int k, int route, double head, bool active) {
		Regional& train = defaultRun().trains[k];
		train.run_ = &defaultRun();
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
	defaultRun().numTrains = 2;
	place(0, 0, 3000.0, true);	// forward, in lock.1
	place(1, 1, 3000.0, false); // reversed, not in the network
	defaultRun().blocksOccupied.clear();
	defaultRun().blocksConnected.clear();

	updateSingleTrackLocks(defaultRun(), 1);
	// A forward train in the section holds it forward.
	QCOMPARE(defaultRun().infra.singleTrackHeld.size(), std::size_t(1));
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 1);
	// The sections are released when the holder changes, so that stale aspects are reset.
	QVERIFY(has(defaultRun().blocksConnected, "lock.0"));
	QVERIFY(has(defaultRun().blocksConnected, "lock.3"));
	QVERIFY(has(defaultRun().blocksConnected, "lock.5"));
	// A route in the direction of the holder is not affected.
	QCOMPARE(occupySingleTrackForRoute(defaultRun(), 0), 0);
	QVERIFY(defaultRun().blocksOccupied.empty());
	// A route against the holder sees the protected and plain sections as occupied.
	QCOMPARE(occupySingleTrackForRoute(defaultRun(), 1), 6);
	QCOMPARE(defaultRun().blocksOccupied.size(), std::size_t(6));
	QVERIFY(has(defaultRun().blocksOccupied, "lock.5"));
	QVERIFY(has(defaultRun().blocksOccupied, "lock.0"));
	defaultRun().blocksOccupied.clear();
	defaultRun().blocksOccupied.push_back("lock.2");
	// A section that is occupied already is not added twice.
	QCOMPARE(occupySingleTrackForRoute(defaultRun(), 1), 5);
	QCOMPARE(defaultRun().blocksOccupied.size(), std::size_t(6));
	defaultRun().blocksOccupied.clear();

	// A second forward train follows: still held forward, nothing new is released, the follower is not affected.
	place(1, 0, 1000.0, true); // forward, in lock.0 (the protected section before the first plain section)
	defaultRun().blocksConnected.clear();
	updateSingleTrackLocks(defaultRun(), 1);
	// A following train in the same direction keeps the holder.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 1);
	QVERIFY(defaultRun().blocksConnected.empty());

	// A train against the holder enters while the first one is still inside: it does not take the section.
	place(1, 1, 3000.0, true); // reversed, in lock.4
	updateSingleTrackLocks(defaultRun(), 1);
	// A train of the other direction that enters later does not take the section.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 1);

	// The forward train leaves: the section passes to the reversed train, and is released on the change.
	place(0, 0, 3000.0, false);
	defaultRun().blocksConnected.clear();
	updateSingleTrackLocks(defaultRun(), 1);
	// The section passes to the remaining direction and is released.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], -1);
	QVERIFY(has(defaultRun().blocksConnected, "lock.2"));
	// Now the forward route is held back and the reversed route is not.
	QCOMPARE(occupySingleTrackForRoute(defaultRun(), 0), 6);
	QCOMPARE(occupySingleTrackForRoute(defaultRun(), 1), 0);
	defaultRun().blocksOccupied.clear();

	// A forward train enters while the reversed holder is still inside: the holder keeps the section.
	place(0, 0, 3000.0, true); // forward, in lock.1
	defaultRun().blocksConnected.clear();
	updateSingleTrackLocks(defaultRun(), 1);
	// The reversed holder keeps the section when a forward train is inside as well.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], -1);
	QVERIFY(defaultRun().blocksConnected.empty());
	place(0, 0, 3000.0, false);

	// Nobody is left: the section is free and is released once.
	place(1, 1, 3000.0, false);
	defaultRun().blocksConnected.clear();
	updateSingleTrackLocks(defaultRun(), 1);
	// A section without trains is free and released.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 0);
	QVERIFY(has(defaultRun().blocksConnected, "lock.1"));
	defaultRun().blocksConnected.clear();
	updateSingleTrackLocks(defaultRun(), 1);
	// A free section is not released again and holds nobody back.
	QVERIFY(defaultRun().blocksConnected.empty());
	QCOMPARE(occupySingleTrackForRoute(defaultRun(), 0), 0);
	QCOMPARE(occupySingleTrackForRoute(defaultRun(), 1), 0);

	// A train in the protected section before the first plain section already holds the section.
	place(0, 0, 100.0, true);
	updateSingleTrackLocks(defaultRun(), 1);
	// A train in the protected section before the section already holds it.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 1);
	place(0, 0, 100.0, false);
	updateSingleTrackLocks(defaultRun(), 1);

	// Reserve before the last braking opportunity; retain the owner through a temporary stop.
	place(0, 0, -50.0, true);
	place(1, 1, -50.0, true);
	defaultRun().blocksConnected.clear();
	updateSingleTrackLocks(defaultRun(), 1);
	// Two running trains about to enter reserve forward and release the zone sections.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 1);
	QVERIFY(has(defaultRun().blocksConnected, "lock.0"));
	QVERIFY(has(defaultRun().blocksConnected, "lock.5"));
	place(0, 0, -200.0, true);
	defaultRun().blocksConnected.clear();
	updateSingleTrackLocks(defaultRun(), 1);
	// A reservation survives its owner slowing or stopping before entry.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 1);
	QVERIFY(defaultRun().blocksConnected.empty());
	place(0, 0, 50.0, true);
	updateSingleTrackLocks(defaultRun(), 1);
	// The forward train keeps the reservation once inside.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 1);

	for (int runningRoute : {0, 1}) {
		resetSingleTrackLocks(defaultRun());
		place(0, runningRoute, -50.0, true);
		place(1, 1 - runningRoute, 0.0, true);
		defaultRun().trains[1].CanEnter = false;
		defaultRun().trains[1].departure_time = 2.0;
		defaultRun().trains[1].Start_Node_X = 0.0;
		defaultRun().trains[1].train_length = 70.0;
		updateSingleTrackLocks(defaultRun(), 1);
		defaultRun().blocksOccupied.clear();
		// A running train and an opposing entry due next step reserve forward and hold the reversed route.
		QCOMPARE(defaultRun().infra.singleTrackHeld[0], 1);
		QCOMPARE(occupySingleTrackForRoute(defaultRun(), 1), 6);
		defaultRun().blocksOccupied.clear();
		defaultRun().trains[1].departure_time = 3.0;
		updateSingleTrackLocks(defaultRun(), 1);
		// Retargeting a waiting entry cancels it but does not cancel the running request.
		QCOMPARE(defaultRun().infra.singleTrackHeld[0], runningRoute == 0 ? 1 : -1);
		place(1, 1 - runningRoute, -50.0, false);
		resetSingleTrackLocks(defaultRun());
		updateSingleTrackLocks(defaultRun(), 1);
		// One approaching direction reserves before its braking opportunity.
		QCOMPARE(defaultRun().infra.singleTrackHeld[0], runningRoute == 0 ? 1 : -1);
	}

	// Waiting entries need no previous trajectory sample, even at the first step.
	for (int k = 0; k < 2; ++k) {
		place(k, k, 0.0, true);
		defaultRun().trains[k].CanEnter = false;
		defaultRun().trains[k].departure_time = 1.0;
		defaultRun().trains[k].Start_Node_X = 0.0;
		defaultRun().trains[k].instant_spatial_position.clear();
	}
	updateSingleTrackLocks(defaultRun(), 0);
	// Opposing entries due at the first step reserve without trajectory samples.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 1);

	// An existing reversed holder is not displaced by two opposing requests.
	const int savedApproachRegions = defaultRun().numTrains;
	defaultRun().trains.resize(3);
	defaultRun().numTrains = 3;
	place(0, 1, 50.0, true);
	place(1, 0, -50.0, true);
	place(2, 1, -50.0, true);
	defaultRun().infra.singleTrackHeld[0] = -1;
	updateSingleTrackLocks(defaultRun(), 1);
	// A reversed holder inside keeps the zone while both directions approach.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], -1);
	defaultRun().trains.resize(2);
	defaultRun().numTrains = savedApproachRegions;
	place(0, 0, -50.0, false);
	place(1, 1, -50.0, false);
	updateSingleTrackLocks(defaultRun(), 1);

	// A held section gives the routes against the holder an End of Authority in front of the zone at level 3 and 4,
	// once per step, and none for the holder's direction.
	defaultRun().infra.singleTrackLimits.clear();
	defaultRun().infra.singleTrackLimits.emplace_back("lock.2", "lock.3", "lock.1", "lock.4");
	resetSingleTrackLocks(defaultRun());
	place(0, 0, 1950.0, true);
	place(1, 1, 1950.0, true);
	updateSingleTrackLocks(defaultRun(), 1);
	// Opposing running trains reserve at an interior zone boundary.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 1);
	for (double previous : {1950.0, 2000.0}) {
		defaultRun().trains[0].instant_spatial_position[0] = previous;
		updateSingleTrackLocks(defaultRun(), 1);
		// An existing reservation does not depend on the last displacement.
		QCOMPARE(defaultRun().infra.singleTrackHeld[0], 1);
	}
	// A follower already committed to this direction must survive the leader's clearance while it waits outside.
	resetSingleTrackLocks(defaultRun());
	defaultRun().trains.resize(3);
	defaultRun().numTrains = 3;
	place(0, 0, 3500.0, true);
	place(1, 0, 1950.0, true);
	place(2, 1, 1000.0, false);
	updateSingleTrackLocks(defaultRun(), 1);
	place(0, 0, 3500.0, false);
	defaultRun().trains[1].instant_train_speed = {0.0, 0.0};
	place(2, 1, 1950.0, true);
	updateSingleTrackLocks(defaultRun(), 1);
	// A committed follower keeps the direction after its leader clears, even while stopped.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 1);
	defaultRun().trains[1].OutOfSimulation = true;
	updateSingleTrackLocks(defaultRun(), 1);
	// Termination cancels the last pending owner and gives the opponent its turn.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], -1);
	defaultRun().trains[2].departure_time = 10.0;
	updateSingleTrackLocks(defaultRun(), 1);
	// Changing the owner's entry identity cancels its reservation.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 0);
	place(2, 1, 1950.0, true);
	updateSingleTrackLocks(defaultRun(), 1);
	defaultRun().trains[2].indexOfRoute = -1;
	updateSingleTrackLocks(defaultRun(), 1);
	// A removed route cancels its pending owner.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 0);
	place(2, 1, 1950.0, true);
	updateSingleTrackLocks(defaultRun(), 1);
	defaultRun().trains.resize(2);
	defaultRun().numTrains = 2;
	updateSingleTrackLocks(defaultRun(), 1);
	// Removing a pending train releases the zone.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 0);
	place(0, 0, 1950.0, true);
	updateSingleTrackLocks(defaultRun(), 1);
	defaultRun().infra.routes[0].x_of_end_node += 1.0;
	defaultRun().trains[0].instant_spatial_position[1] = 1000.0;
	defaultRun().trains[0].instant_train_speed[1] = 0.0;
	updateSingleTrackLocks(defaultRun(), 1);
	// A changed destination cancels a pending reservation.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 0);
	defaultRun().infra.routes[0].x_of_end_node -= 1.0;
	place(0, 0, 1999.5, true);
	defaultRun().trains[0].instant_train_speed = {0.0, 0.0};
	updateSingleTrackLocks(defaultRun(), 1);
	// A stationary train can consume its braking opportunity by accelerating next step.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 1);
	place(0, 0, 10100.0, true);
	updateSingleTrackLocks(defaultRun(), 1);
	// Passage of the reserved interval releases its owner.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 0);
	struct FailedBrakingLookup : Regional {
		double result = -1, requestedSpeed = 0;
		double BrakDist_Block(double speed, double, double, Section*, int, double) override {
			requestedSpeed = speed;
			return result;
		}
	} probe;
	static_cast<Train&>(probe) = defaultRun().trains[0];
	for (double result : {-1.0, std::numeric_limits<double>::quiet_NaN(), -10.0}) {
		probe.result = result;
		QVERIFY2(probe.needsSingleTrackReservation(1000.0, 0.0, 2000.0,
					 defaultRun().infra.routes[0].sequence_of_block_sections.data(), 6),
			"failed, non-finite or before-origin braking points request conservatively");
		QVERIFY2(probe.requestedSpeed > 0, "reservation braking lookup includes possible next-step acceleration from rest");
	}
	std::vector<std::vector<int>> savedLevels;
	for (const Route& route : defaultRun().infra.routes) {
		savedLevels.emplace_back();
		for (const Section& section : route.sequence_of_block_sections)
			savedLevels.back().push_back(section.SignallingLevel);
	}
	auto setLevel = [](int level) {
		for (Route& route : defaultRun().infra.routes)
			for (Section& section : route.sequence_of_block_sections)
				section.SignallingLevel = level;
	};
	setLevel(3);
	place(0, 0, 3500.0, true); // forward, in lock.1
	place(1, 1, 3000.0, false);
	defaultRun().etcsMa.clear();
	updateSingleTrackLocks(defaultRun(), 1);
	Apply_Single_Track_Authorities_Mixed_Signalling(defaultRun());
	Apply_Single_Track_Authorities_Mixed_Signalling(defaultRun());
	// A forward holder gives the reversed route one authority in front of the zone.
	QCOMPARE(defaultRun().etcsMa.size(), std::size_t(1));
	QCOMPARE(defaultRun().etcsMa.front().BSID, "lock.5");
	QVERIFY(defaultRun().etcsMa.front().ReversedDirection);
	QCOMPARE(defaultRun().etcsMa.front().type, "SignalFailure");
	QCOMPARE(defaultRun().etcsMa.front().TrainInfo.trainDescription, "single_track:lock.2");
	setLevel(2);
	defaultRun().etcsMa.clear();
	Apply_Single_Track_Authorities_Mixed_Signalling(defaultRun());
	// Fixed-block trains also get a direction-filtered entry authority.
	QCOMPARE(defaultRun().etcsMa.size(), std::size_t(1));
	QVERIFY(defaultRun().etcsMa.front().ReversedDirection);
	setLevel(kSignallingLevelUnset);
	defaultRun().etcsMa.clear();
	Apply_Single_Track_Authorities_Mixed_Signalling(defaultRun());
	QVERIFY2(defaultRun().etcsMa.empty(), "a section without signalling gets no single-track authority");
	setLevel(4);
	place(0, 0, 3500.0, false);
	place(1, 1, 3500.0, true); // reversed, in lock.4
	updateSingleTrackLocks(defaultRun(), 1);
	defaultRun().etcsMa.clear();
	Apply_Single_Track_Authorities_Mixed_Signalling(defaultRun());
	// A reversed holder gives the forward route an authority in front of the zone.
	QCOMPARE(defaultRun().etcsMa.size(), std::size_t(1));
	QCOMPARE(defaultRun().etcsMa.front().BSID, "lock.0");
	QVERIFY(!defaultRun().etcsMa.front().ReversedDirection);
	place(1, 1, 3500.0, false);
	updateSingleTrackLocks(defaultRun(), 1);
	defaultRun().etcsMa.clear();
	Apply_Single_Track_Authorities_Mixed_Signalling(defaultRun());
	// A free section gives no authority.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 0);
	QVERIFY(defaultRun().etcsMa.empty());
	defaultRun().etcsMa = savedAuthorities;
	for (std::size_t r = 0; r < defaultRun().infra.routes.size(); ++r)
		for (std::size_t b = 0; b < savedLevels[r].size(); ++b)
			defaultRun().infra.routes[r].sequence_of_block_sections[b].SignallingLevel = savedLevels[r][b];

	// A zone with gaps keeps one interval per run of neighbouring sections, and a train in a gap is not inside.
	defaultRun().infra.singleTrackLimits.clear();
	defaultRun().infra.singleTrackLimits.emplace_back("lock.2", "lock.3", "lock.0", "lock.5");
	resetSingleTrackLocks(defaultRun());
	// Separate zone sections give separate intervals.
	QCOMPARE(singleTrackZone(defaultRun(), 0, 0).intervals.size(), std::size_t(3));
	QCOMPARE(singleTrackZone(defaultRun(), 0, 0).sectionIDs.size(), std::size_t(4));
	place(0, 0, 3000.0, true); // forward, in lock.1, between the protected and the plain sections
	defaultRun().trains[0].instant_train_speed = {0.0, 0.0};
	place(1, 1, 3000.0, false);
	updateSingleTrackLocks(defaultRun(), 1);
	// A train between zone sections does not hold the zone.
	QCOMPARE(defaultRun().infra.singleTrackHeld.size(), std::size_t(1));
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 0);
	place(0, 0, 5000.0, true); // forward, in lock.2
	updateSingleTrackLocks(defaultRun(), 1);
	// A train in a zone section holds the zone.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 1);
	place(0, 0, 50.0, true); // forward, in lock.0 with the rear before the start of the route
	updateSingleTrackLocks(defaultRun(), 1);
	// A train whose rear is before the start of the route is inside the first section.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 1);
	place(0, 0, 5000.0, false);
	updateSingleTrackLocks(defaultRun(), 1);

	// At level 5 the next disconnected interval's approach must already be committed when the tail clears
	// the first interval. Drive the real movement and signalling routines across that clearance and the gap.
	setLevel(5);
	for (Route& route : defaultRun().infra.routes)
		route.x_of_end_node = route.sequence_of_block_sections.back().end_node.X;
	resetSingleTrackLocks(defaultRun());
	place(0, 1, 2000.0, true);
	place(1, 0, 0.0, true);
	defaultRun().trains[1].CanEnter = false;
	constexpr int steps = 65;
	for (Regional& train : defaultRun().trains) {
		const double head = train.instant_spatial_position[1];
		const double speed = train.CanEnter ? train.instant_train_speed[1] : 0.0;
		train.setTrainVectorSizesFromInput(steps);
		train.instant_spatial_position[0] = head - speed * timestep;
		train.instant_spatial_position[1] = head;
		train.instant_train_speed[0] = train.instant_train_speed[1] = speed;
	}
	defaultRun().infra.numRoutes = static_cast<int>(defaultRun().infra.routes.size());
	defaultRun().etcsMa.clear();
	auto signalStep = [](int step) {
		defaultRun().blocksOccupied.clear();
		Occupy_Block_Sections_Of_Route(defaultRun(), step);
		defaultRun().etcsMa.clear();
		Apply_Single_Track_Authorities_Mixed_Signalling(defaultRun());
		releaseMixedSignallingSystem(defaultRun());
		activateMixedSignallingSystem(defaultRun());
	};
	signalStep(1);
	// The moving holder commits the next interval before clearing the first, but not the distant third interval.
	QCOMPARE(defaultRun().infra.singleTrackReservations[0].size(), std::size_t(2));
	QVERIFY(defaultRun().infra.singleTrackReservations[0][1].interval == std::make_pair(4000.0, 8000.0));
	bool retainedAcrossGap = true, safeApproach = true, waiterHeld = true, crossedGap = false;
	double largestSpeedDrop = 0.0;
	Regional& holder = defaultRun().trains[0];
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
		defaultRun().trains[1].trajectoryComputationIncludingMovingBlock(step, signalCode1, signalCode2, signalCode3);
		waiterHeld &= !defaultRun().trains[1].CanEnter;
		signalStep(step);
		const double head = holder.instant_spatial_position[step];
		if (head - holder.train_length >= 2000.0 && head < 4000.0) {
			crossedGap = true;
			retainedAcrossGap &= defaultRun().infra.singleTrackHeld[0] == -1
				&& defaultRun().infra.routes[1].sequence_of_block_sections[1].arcs_in_signalling_block_section[0].signalSpeedLimit
					>= holder.instant_train_speed[step];
		}
	}
	// The moving reversed holder retains direction and its approach speed limit across the gap while the forward opponent waits.
	QVERIFY(crossedGap);
	QVERIFY(retainedAcrossGap);
	QVERIFY(waiterHeld);
	QVERIFY2(safeApproach, ("gap handover neither rewrites the previous speed nor exceeds physical deceleration; largest speed drop=" + std::to_string(largestSpeedDrop)).c_str());
	QVERIFY2(holder.instant_spatial_position.back() - holder.train_length >= 4000.0,
		"the moving holder reaches the next disconnected interval without an instantaneous stop");
	defaultRun().infra.numRoutes = savedRouteCount;
	defaultRun().etcsMa = savedAuthorities;

	// At level 2 the same next interval is still beyond braking lookahead, so clearing the first releases it.
	setLevel(2);
	resetSingleTrackLocks(defaultRun());
	place(0, 1, 2000.0, true);
	place(1, 0, 0.0, true);
	defaultRun().trains[1].CanEnter = false;
	updateSingleTrackLocks(defaultRun(), 1);
	// A distant next stretch outside braking lookahead is not committed.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], -1);
	QCOMPARE(defaultRun().infra.singleTrackReservations[0].size(), std::size_t(1));
	place(0, 1, 2070.0, true);
	updateSingleTrackLocks(defaultRun(), 1);
	// Tail clearance releases the direction to a waiting opponent when the distant next stretch is uncommitted.
	QCOMPARE(defaultRun().infra.singleTrackHeld[0], 1);

	// New routes are not read through the zones of the old ones.
	defaultRun().infra.routes.resize(1);
	// The zones follow the current routes.
	QVERIFY(singleTrackZone(defaultRun(), 0, 1).intervals.empty());
	QCOMPARE(singleTrackZone(defaultRun(), 0, 0).intervals.size(), std::size_t(3));
	resetSingleTrackLocks(defaultRun());
}

static bool listHas(const std::list<std::string>& ids, const std::string& id) {
	return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// True when every route section has the number of its ID and different IDs have different numbers.
static bool numbersFollowIds(const RunInfrastructure& infra) {
	if (infra.routeSectionNumbers.size() != infra.routes.size())
		return false;
	std::vector<std::string> idOfNumber(infra.sectionIdNumbers.size());
	for (const auto& [id, number] : infra.sectionIdNumbers) {
		const auto index = static_cast<std::size_t>(number);
		if (number < 0 || index >= idOfNumber.size() || !idOfNumber[index].empty())
			return false;
		idOfNumber[index] = id;
	}
	for (std::size_t r = 0; r < infra.routes.size(); ++r) {
		const Route& route = infra.routes[r];
		const std::vector<int>& numbers = infra.routeSectionNumbers[r];
		if (numbers.size() != static_cast<std::size_t>(route.N_Block_Sections))
			return false;
		for (std::size_t h = 0; h < numbers.size(); ++h) {
			const auto index = static_cast<std::size_t>(numbers[h]);
			if (numbers[h] < 0 || index >= idOfNumber.size() || idOfNumber[index] != route.sequence_of_block_sections[h].ID)
				return false;
		}
	}
	return true;
}

// The signalling step finds the occupied and the connected sections of each route by the number of their ID. Two routes share
// a section with a third, and the route against the holder of a single-track limit treats the zone as occupied for itself only.
// Each route sees exactly the sections whose ID is in the lists, as a comparison of the IDs gives them.
void TestOperationsBuilder::signallingFindsSectionsById() {
	// The builder numbers the routes that it builds.
	const auto built = std::make_unique<SimulationRun>();
	QVERIFY(!hasErrors(buildInfrastructureAndSignallingFromScene(*built, throughScene())));
	QVERIFY(!built->infra.routes.empty());
	QVERIFY(numbersFollowIds(built->infra));

	// Sections of level 3, where the activation gives code 0 to the sections that the route sees as occupied and nothing else.
	const auto makeRoute = [](const std::vector<std::string>& ids, bool reversed) {
		Route route;
		route.N_Block_Sections = static_cast<int>(ids.size());
		route.reversed_direction = reversed;
		route.sequence_of_block_sections = boundarySections(route.N_Block_Sections);
		for (std::size_t b = 0; b < ids.size(); ++b) {
			Section& section = route.sequence_of_block_sections[b];
			section.ID = ids[b];
			section.SignallingLevel = 3;
			section.code = 270;
		}
		return route;
	};
	const auto run = std::make_unique<SimulationRun>();
	RunInfrastructure& infra = run->infra;
	// Route 1 runs against the holder of the limit over z1 and z2, route 2 after it runs with the holder.
	infra.routes = {makeRoute({"a", "shared", "b"}, false), makeRoute({"shared", "z1", "z2", "c"}, true),
		makeRoute({"d", "z1", "z2", "shared", "e"}, false)};
	infra.numRoutes = 3;
	infra.numberRouteSections();
	QVERIFY(numbersFollowIds(infra));
	// One number for an ID that several routes have.
	QCOMPARE(infra.sectionIdNumbers.size(), std::size_t(8));
	QCOMPARE(infra.routeSectionNumbers[1][0], infra.routeSectionNumbers[0][1]);
	QCOMPARE(infra.routeSectionNumbers[2][3], infra.routeSectionNumbers[0][1]);
	infra.singleTrackLimits.emplace_back("z1", "z2", "z1", "z2");
	resetSingleTrackLocks(*run);
	infra.singleTrackHeld = {1};

	// A train of route 0 occupies the shared section. An ID that no route has changes nothing.
	const std::list<std::string> occupied = {"shared", "elsewhere"};
	run->blocksOccupied = occupied;
	activateMixedSignallingSystem(*run);
	// The sections that route 1 added for itself are gone from the list again.
	QVERIFY(run->blocksOccupied == occupied);
	for (int r = 0; r < infra.numRoutes; ++r) {
		const Route& route = infra.routes[r];
		const std::vector<std::string>& zone = singleTrackZone(*run, 0, r).sectionIDs;
		for (int h = 0; h < route.N_Block_Sections; ++h) {
			const std::string& id = route.sequence_of_block_sections[h].ID;
			const bool expected = listHas(occupied, id)
				|| (route.reversed_direction && std::find(zone.begin(), zone.end(), id) != zone.end());
			QVERIFY2((route.sequence_of_block_sections[h].code == 0) == expected,
				("route " + std::to_string(r) + " sees " + id + (expected ? " as occupied" : " as free")).c_str());
		}
	}

	// The release clears a connected section and the four sections before it on every route that has it.
	for (Route& route : infra.routes)
		for (Section& section : route.sequence_of_block_sections)
			section.code = 5;
	const std::list<std::string> connected = {"shared", "elsewhere"};
	run->blocksConnected = connected;
	releaseMixedSignallingSystem(*run);
	QVERIFY(run->blocksConnected == connected);
	for (int r = 0; r < infra.numRoutes; ++r) {
		const Route& route = infra.routes[r];
		for (int h = 0; h < route.N_Block_Sections; ++h) {
			bool expected = false;
			for (int k = h; k < route.N_Block_Sections && k <= h + 4; ++k)
				expected = expected || listHas(connected, route.sequence_of_block_sections[k].ID);
			QVERIFY2((route.sequence_of_block_sections[h].code == 270) == expected,
				("route " + std::to_string(r) + " releases " + route.sequence_of_block_sections[h].ID + ": " + (expected ? "yes" : "no")).c_str());
		}
	}
}

// The messages of the rolling stock, passenger and run settings code are messages of the logging core. A sink collects them, because
// the console shows neither the category nor the level, and each text is compared with the exact wording, quirks included (two spaces
// where a name is empty, no space after "Train", a number that follows a line end).
void TestOperationsBuilder::rollingStockMessages() {
	using eglog::Category;
	using eglog::Level;
	struct Message {
		Category category;
		Level level;
		std::string text;
	};
	// Collects the messages and puts the default sink and the default rules back on every path out of the function.
	struct Capture {
		std::vector<Message> messages;
		Capture() {
			eglog::setSink([this](Category category, Level level, const std::string& text) { this->messages.push_back(Message{category, level, text}); });
		}
		~Capture() {
			eglog::setSink({});
			eglog::configure("");
		}
		std::vector<Message> take() {
			std::vector<Message> taken;
			taken.swap(messages);
			return taken;
		}
	} capture;
	const auto visible = [](std::string text) {
		for (std::size_t at = 0; (at = text.find_first_of("\r\n", at)) != std::string::npos; at += 2)
			text.replace(at, 1, text[at] == '\r' ? "\\r" : "\\n");
		return text;
	};
	// A failed check ends this function only, so each call is followed by QTest::currentTestFailed().
	const auto shows = [&](const std::vector<Message>& found, const std::vector<Message>& wanted, const std::string& what) {
		bool same = found.size() == wanted.size();
		for (std::size_t index = 0; same && index < found.size(); ++index)
			same = found[index].category == wanted[index].category && found[index].level == wanted[index].level
				&& found[index].text == wanted[index].text;
		std::string described;
		if (!same) {
			const auto describe = [&](const std::string& title, const std::vector<Message>& messages) {
				described += "\n  " + title + ":";
				for (const Message& message : messages)
					described += std::string("\n    ") + eglog::categoryName(message.category) + " " + eglog::levelName(message.level)
						+ " '" + visible(message.text) + "'";
			};
			describe("found", found);
			describe("wanted", wanted);
		}
		QVERIFY2(same, (what + described).c_str());
	};

	// The settings of a run: a case number that selects a case study, and one that selects nothing.
	InitialParameters parameters(0);
	shows(capture.take(), {}, "run settings without a case write nothing");
	if (QTest::currentTestFailed())
		return;
	parameters.set_case(2);
	shows(capture.take(), {{Category::SimSetup, Level::Info, "\nSelected Case study: 2\n"}}, "a selected case study");
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(!parameters.name.empty(), "a selected case study has a name");
	parameters.set_case(6);
	shows(capture.take(), {{Category::SimSetup, Level::Warning, "No case selected"}}, "a case number that selects nothing");
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(parameters.name.empty(), "a case number that selects nothing clears the name");

	// A timetable with more stations than the arrays hold is cut to the limit.
	const int limit = Train::kMaxTimetableStations;
	// A station count within the limit is kept.
	QCOMPARE(Train::clampStationCount(limit - 1, "T1"), limit - 1);
	QCOMPARE(Train::clampStationCount(limit, "T1"), limit);
	shows(capture.take(), {}, "a station count within the limit");
	if (QTest::currentTestFailed())
		return;
	// A station count over the limit is cut to the limit.
	QCOMPARE(Train::clampStationCount(limit + 5, "T1"), limit);
	shows(capture.take(),
		{{Category::SimSetup, Level::Warning,
			"Train T1 serves " + std::to_string(limit + 5) + " stations but the timetable arrays hold " + std::to_string(limit)
				+ "; dropping the last 5 stops\n"}},
		"a station count over the limit");
	if (QTest::currentTestFailed())
		return;

	Train train;
	train.run_ = &defaultRun();
	train.trainDescription = "T1";

	// A blocking time with no name and no recorded events writes both errors; with a name only the second.
	BlockingTimes blocking;
	blocking.BlockID.clear();
	blocking.LocationWithSwitch = false;
	blocking.checkIfLastInfraElementStatusIsInLineWithTrainNeeds("T1", {});
	shows(capture.take(),
		{{Category::SimSetup, Level::Error,
			 "ERROR 988: Name of InfraElement has not been assigned in the function to update the status of infrastructure elements "
			 "for Block Section  on Route of Train T1\n"},
			{Category::SimSetup, Level::Error, "Error: Element  Not Found\n"}},
		"a blocking time with no name");
	if (QTest::currentTestFailed())
		return;
	blocking.BlockID = "B1";
	blocking.checkIfLastInfraElementStatusIsInLineWithTrainNeeds("T1", {});
	shows(capture.take(), {{Category::SimSetup, Level::Error, "Error: Element B1 Not Found\n"}}, "a blocking time of an element with no events");
	if (QTest::currentTestFailed())
		return;

	// A train whose leader reports no movement authority any more leaves following mode, whether the message is on or off.
	{
		const auto savedAuthorities = defaultRun().etcsMa;
		const auto savedMessageTimes = defaultRun().vcMsgTimestep;
		const auto savedMessageTrains = defaultRun().vcMsgTrain;
		const auto savedMessageTexts = defaultRun().vcMsgText;
		const auto restoreRun = qScopeGuard([&] {
			defaultRun().etcsMa = savedAuthorities;
			defaultRun().vcMsgTimestep = savedMessageTimes;
			defaultRun().vcMsgTrain = savedMessageTrains;
			defaultRun().vcMsgText = savedMessageTexts;
		});
		defaultRun().etcsMa.clear();
		const auto follow = [&train] {
			train.IsTrainInFollowingMode = true;
			train.LeadingTrainInFollowingMode = "L1";
		};
		follow();
		train.leaveFollowingModeWhenLeaderIsGone(7);
		shows(capture.take(),
			{{Category::SimEvents, Level::Info, "At time instant 7 train T1 is decoupled from train L1, which reports no position any more\n"}},
			"a train whose leader reports no position any more");
		if (QTest::currentTestFailed())
			return;
		// A train whose leader is gone leaves following mode.
		QVERIFY(!train.IsTrainInFollowingMode);
		QCOMPARE(train.LeadingTrainInFollowingMode, "None");
		QVERIFY2(eglog::configure("sim.events=false"), "the rule that switches sim.events off is well formed");
		follow();
		train.leaveFollowingModeWhenLeaderIsGone(7);
		shows(capture.take(), {}, "a train whose leader is gone with sim.events off");
		if (QTest::currentTestFailed())
			return;
		QVERIFY2(!train.IsTrainInFollowingMode, "a train whose leader is gone leaves following mode with sim.events off");
		eglog::configure("");
		train.leaveFollowingModeWhenLeaderIsGone(7);
		shows(capture.take(), {}, "a train that is not in following mode");
		if (QTest::currentTestFailed())
			return;
	}

	// A target speed under the starting speed is an error, and the train then cannot accelerate on the section.
	// No acceleration time for a target speed under the starting speed.
	QCOMPARE_EQ(train.AccelerationTimeFollowingMode(10, 5, 0, 100, nullptr, 0), -99999.0);
	shows(capture.take(),
		{{Category::SimEvents, Level::Error,
			 "ERROR: Train T1 is trying to compute an average acceleration distance with a target speed V2 lower than the starting speed V1. "
			 "A braking curve should be computed in this case. Please check the reason why this is happening\n"},
			{Category::SimEvents, Level::Warning,
				"Train T1 has air resistances larger than tractive effort on this section with V1: 10 V2: 5 X1: 0 X2: 100. "
				"The Cruising mode will be applied since trains cannot accelerate\n"}},
		"a target speed under the starting speed");
	if (QTest::currentTestFailed())
		return;

	// A passenger who alights where the next trip of the journey does not start.
	train.StoppedForServiceStop = true;
	train.CurrentServiceStop = "S1";
	train.CurrentServiceStopPlatform = "P1";
	Passenger alighting;
	alighting.ID = "Pax1";
	alighting.CurrentStatus = "OnBoard";
	alighting.Current_Train_Boarded = "T1";
	alighting.Current_Arrival_Station = "S1";
	alighting.current_TripID = "A1";
	alighting.current_JourneyID = "J1";
	Journey journey;
	journey.ID = "J1";
	Trip firstTrip;
	firstTrip.TripID = "A1";
	firstTrip.Arr_Station_ID = "S1";
	Trip nextTrip;
	nextTrip.TripID = "A2";
	nextTrip.Dep_Station_ID = "S2";
	journey.Trips = {firstTrip, nextTrip};
	alighting.Journeys.push_back(journey);
	std::list<Passenger> passengers{alighting};
	Simulate_Train_Passenger_Interactions(defaultRun(), 10, 0, train, passengers, {});
	shows(capture.take(),
		{{Category::SimPax, Level::Warning,
			"Warning: Passenger Pax1will depart from a station different than the one planned for active trip A2 in journey J1\n"}},
		"a passenger who alights where the next trip does not start");
	if (QTest::currentTestFailed())
		return;

	// A passenger who waits for a train that is full. The waiting list of the platform is the one given when PAX_GUI is set.
	const bool savedPaxGui = defaultRun().settings.PAX_GUI;
	defaultRun().settings.PAX_GUI = true;
	StationPlatform platform;
	platform.ID = "P1";
	platform.StationID = "S1";
	platform.Current_List_Pax_On_Platform.push_back({"Pax2", 0.0});
	Passenger waiting;
	waiting.ID = "Pax2";
	waiting.CurrentStatus = "OnPlatform";
	waiting.Current_Train_To_Wait = "T1";
	waiting.Current_WaitingStationID = "S1";
	passengers = {waiting};
	train.MAX_OnBoard_Passengers = 0;
	train.Current_OnBoard_Passengers = 0;
	Simulate_Train_Passenger_Interactions(defaultRun(), 10, 0, train, passengers, {platform});
	defaultRun().settings.PAX_GUI = savedPaxGui;
	shows(capture.take(),
		{{Category::SimPax, Level::Info, "Call RouteChoice model as passenger ID: Pax2 could not board train T1 as it was full\n"}},
		"a passenger who waits for a train that is full");
	if (QTest::currentTestFailed())
		return;

	// A route of one section from 0 to 10 km.
	std::vector<Route> savedRoutes;
	savedRoutes.swap(defaultRun().infra.routes);
	const auto restoreRoutes = qScopeGuard([&savedRoutes] { defaultRun().infra.routes.swap(savedRoutes); });
	defaultRun().infra.routes.resize(1);
	defaultRun().infra.routes[0].N_Block_Sections = 1;
	defaultRun().infra.routes[0].reversed_direction = false;
	defaultRun().infra.routes[0].sequence_of_block_sections.resize(1);
	Section& routeSection = defaultRun().infra.routes[0].sequence_of_block_sections[0];
	routeSection.start_node.X = 0.0;
	routeSection.end_node.X = 10.0;
	routeSection.GeoXBegNode = 0.0;
	train.CanEnter = true;

	// Arrival at the last station and departure from the first: the position of the train is read from the route.
	QTemporaryDir output;
	QVERIFY2(output.isValid(), "temporary output folder");
	const std::string savedOutputFolder = defaultRun().settings.OutputMainFolder;
	const auto restoreOutputFolder = qScopeGuard([&savedOutputFolder] { defaultRun().settings.OutputMainFolder = savedOutputFolder; });
	defaultRun().settings.OutputMainFolder = output.path().toStdString();
	train.Stations.resize(2);
	train.Stations[0].stationName = "A";
	train.Stations[0].X = 1.0;
	train.Stations[1].stationName = "B";
	train.Stations[1].X = 2.0;
	train.numStations = 2;
	train.instant_spatial_position = {500.0, 1000.0, 1500.0, 1750.0, 2000.0};
	train.instant_train_speed = {0.0, 5.0, 5.0, 0.0, 0.0};
	const auto checkAt = [&](int second) {
		train.checkTrainArrDep(second);
		return capture.take();
	};
	shows(checkAt(0), {}, "a train at the start of the route");
	if (QTest::currentTestFailed())
		return;
	shows(checkAt(2), {}, "a train between the stations");
	if (QTest::currentTestFailed())
		return;
	shows(checkAt(1),
		{{Category::SimEvents, Level::Info, "\n>>> T1 DEPARTED from A at t = 1 - " + simulationTime(1, defaultRun().settings.startingSimulationTime) + "\n"}},
		"a train that departs from its first station");
	if (QTest::currentTestFailed())
		return;
	shows(checkAt(4),
		{{Category::SimEvents, Level::Info, "\n<<< T1 ARRIVED at B at t = 4 - " + simulationTime(4, defaultRun().settings.startingSimulationTime) + "\n"}},
		"a train that arrives at its last station");
	if (QTest::currentTestFailed())
		return;
	QVERIFY2(QDir(output.path()).isEmpty(), "an arrival at the last station writes no file");
}

// The aspect of a section is restrictive when it is double red at level 5 and red at every other level.
struct AspectCase {
	const char* name;
	SignalState state;
	bool stopsAtAtb, stopsAtBacc;
};

static const AspectCase kAspectCases[] = {
	{"green", SignalState::Green, false, false},
	{"yellow", SignalState::Yellow, false, false},
	{"red", SignalState::Red, true, false},
	{"double red", SignalState::RedRed, false, true},
};

void TestOperationsBuilder::aspectIsRestrictive_data() {
	QTest::addColumn<int>("index");
	for (int index = 0; index < static_cast<int>(std::size(kAspectCases)); ++index)
		QTest::newRow(kAspectCases[index].name) << index;
}

void TestOperationsBuilder::aspectIsRestrictive() {
	QFETCH(int, index);
	const AspectCase& c = kAspectCases[index];
	Train train;
	train.run_ = &defaultRun();
	Section section;
	section.state = c.state;
	section.SignallingLevel = levelValue(SignallingLevel::Atb);
	QVERIFY2(train.isAspectRestrictive(section) == c.stopsAtAtb, ("stop for the signal state " + std::to_string(index) + " at level 0").c_str());
	section.SignallingLevel = levelValue(SignallingLevel::Bacc);
	QVERIFY2(train.isAspectRestrictive(section) == c.stopsAtBacc, ("stop for the signal state " + std::to_string(index) + " at level 5").c_str());
}

void TestOperationsBuilder::cleanup() {
	m_failed = m_failed || QTest::currentTestFailed();
}

// tools/e2e/route_diagram_smoke.py looks for this line.
void TestOperationsBuilder::cleanupTestCase() {
	if (!m_failed)
		std::cout << "native forward/reverse route diagram coordinates passed\n";
}

QTEST_APPLESS_MAIN(TestOperationsBuilder)
#include "test_operationsbuilder.moc"
