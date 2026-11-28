// Tests of the run state of simulation/SimulationRun.h.
//
// reset() and resetInfrastructure() have to return every member to the state of a new run. Runs in this process then show
// that nothing of one run reaches the next. A run that was stopped part way is followed by another scene, a failed preparation
// by a good one, and a scene with passengers runs twice.
// Every run after such a gap has to write the files that the first run of its scene wrote. prepareScene copies the settings
// of the application into the run, and the run goes on with the copy. The scene is built into a run of its own that replaces the
// run of the controller, and the snapshot of a step is made from the run that it is given.
//
// The scenes are the line fixture of tests/fixtures/scenes and Scenes/Paimpol.

#include "app/DispatchController.h"
#include "scene/SceneModel.h"
#include "simulation/DispatchMeasures.h"
#include "simulation/InitialParameters.h"
#include "simulation/Infrastructure.h"
#include "simulation/Passengers.h"
#include "simulation/RollingStock.h"
#include "simulation/Signalling.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationRun.h"
#ifdef signals
#undef signals
#endif

#include <QCoreApplication>
#include <QObject>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

class CoutSilencer {
public:
	CoutSilencer() : m_saved(std::cout.rdbuf(m_sink.rdbuf())) {}
	~CoutSilencer() { std::cout.rdbuf(m_saved); }

private:
	std::ostringstream m_sink;
	std::streambuf* m_saved;
};

// The members of the run by name, each described by a text that tells a new run from a used one. The structured binding
// names every member, so this file stops compiling when a member is added to the run. The names of the infrastructure start
// with "infra.".
using Description = std::vector<std::pair<std::string, std::string>>;

// The number of elements of an array that differ from a new element, by the test.
template <class Element, class Used>
std::string arrayText(const std::vector<Element>& elements, Used used) {
	return std::to_string(elements.size()) + "/" + std::to_string(std::count_if(elements.begin(), elements.end(), used));
}

Description describeInfrastructure(const RunInfrastructure& infrastructure) {
	const auto& [tracks, links, stationArray, trackCount, linkCount, stationCount, sections, blockCount, detectionSections, infraEvents,
		exceedPlan, limits, held, reservations, zones, zoneLimits, zoneRoutes, routeZone, zonesDerived, closures, keptOut, boundaries, routes,
		routeCount,
		sectionIdNumbers, routeSectionNumbers] = infrastructure;
	return {{"infra.blockSets", arrayText(tracks, [](const BlockSet& track) { return !track.sceneTrackId.empty() || !track.N.empty(); })},
		{"infra.connections", arrayText(links, [](const Connections& link) { return !link.ID.empty(); })},
		{"infra.stations", arrayText(stationArray, [](const Stations& station) { return !station.stationName.empty(); })},
		{"infra.numTrackLines", std::to_string(trackCount)},
		{"infra.numConnections", std::to_string(linkCount)},
		{"infra.numStations", std::to_string(stationCount)},
		{"infra.sections", std::to_string(sections.size()) + "/" + std::to_string(sections.capacity())},
		{"infra.numBlocks", std::to_string(blockCount)},
		{"infra.detectionSections", std::to_string(detectionSections.size())},
		{"infra.infraEvents", std::to_string(infraEvents.size())},
		{"infra.derivedSectionsExceedPlan", std::to_string(exceedPlan)},
		{"infra.singleTrackLimits", std::to_string(limits.size())},
		{"infra.singleTrackHeld", std::to_string(held.size())},
		{"infra.singleTrackReservations", std::to_string(reservations.size())},
		{"infra.singleTrackZones", std::to_string(zones.size())},
		{"infra.singleTrackZoneLimits", std::to_string(zoneLimits)},
		{"infra.singleTrackZoneRoutes", std::to_string(zoneRoutes)},
		{"infra.singleTrackRouteZone", std::to_string(routeZone.size())},
		{"infra.singleTrackZonesDerived", std::to_string(zonesDerived)},
		{"infra.singleTrackClosures", std::to_string(closures.size())},
		{"infra.singleTrackKeptOut", std::to_string(keptOut.size())},
		{"infra.stationBoundaries", std::to_string(boundaries.size())},
		{"infra.routes", std::to_string(routes.size())},
		{"infra.numRoutes", std::to_string(routeCount)},
		{"infra.sectionIdNumbers", std::to_string(sectionIdNumbers.size())},
		{"infra.routeSectionNumbers", std::to_string(routeSectionNumbers.size())}};
}

Description describe(const SimulationRun& run) {
	const auto& [settings, occupied, connected, authorities, incidents, measures, nextMeasure, appliedMeasures, vcTimesteps, vcTrains, vcTexts, platforms, numPlatforms, passengers,
		numPassengers, passengerIndex, platformIndex, passengerFlow, passengerJourneys, stopCalls, totalDelays, entranceDelays, disturbances, finalStation, trains, numTrains, generator, delay,
		egtrainTime, romaTime, infrastructure] = run;
	(void)passengerJourneys; // Planning state only; reset() and adopt() empty it like passengerFlow.
	(void)stopCalls;		 // The same for the counts of the calls at stops.
	const auto statistics = [](const DelayStats& s) {
		return std::to_string(s.N_Stopped_Trains) + "/" + std::to_string(s.totalArrivalDelay);
	};
	NumberGenerator draws = generator;
	const std::string draw = std::to_string(draws.getUniformFloat()) + "/" + std::to_string(draws.getGaussianFloat());
	Description description = {{"settings", settings.name + "/" + std::to_string(settings.times) + "/" + settings.OutputMainFolder + "/" + std::to_string(settings.bufferTime) + "/" + std::to_string(settings.recoveryTimePercentage) + "/" + std::to_string(settings.GUI)},
		{"blocksOccupied", std::to_string(occupied.size())},
		{"blocksConnected", std::to_string(connected.size())},
		{"etcsMa", std::to_string(authorities.size())},
		{"incidents", std::to_string(incidents.size())},
		{"measures", std::to_string(measures.size())},
		{"nextMeasure", std::to_string(nextMeasure)},
		{"appliedMeasures", std::to_string(appliedMeasures.size())},
		{"vcMsgTimestep", std::to_string(vcTimesteps.size())},
		{"vcMsgTrain", std::to_string(vcTrains.size())},
		{"vcMsgText", std::to_string(vcTexts.size())},
		{"platforms", std::to_string(platforms.size())},
		{"numPlatforms", std::to_string(numPlatforms)},
		{"passengers", std::to_string(passengers.size())},
		{"numPassengers", std::to_string(numPassengers)},
		{"passengerIndex", std::to_string(passengerIndex.covers(passengers))},
		{"platformIndex", std::to_string(platformIndex.covers(platforms))},
		{"passengerFlow", std::to_string(passengerFlow.covers(passengers))},
		{"totalInputDelays", statistics(totalDelays)},
		{"entranceInputDelays", statistics(entranceDelays)},
		{"disturbanceInput", statistics(disturbances)},
		{"finalStation", statistics(finalStation)},
		{"trains", std::to_string(trains.size()) + "/" + std::to_string(trains.capacity())},
		{"numTrains", std::to_string(numTrains)},
		{"numberGenerator", draw},
		{"signallingDelay", std::to_string(delay)},
		{"compTimeEgtrain", std::to_string(egtrainTime)},
		{"compTimeRoma", std::to_string(romaTime)}};
	const Description infra = describeInfrastructure(infrastructure);
	description.insert(description.end(), infra.begin(), infra.end());
	return description;
}

bool isInfrastructureMember(const std::string& name) {
	return name.rfind("infra.", 0) == 0;
}

// The names of the members of the run that are as in a new run.
std::vector<std::string> membersOfNewRun(const SimulationRun& run) {
	const auto fresh = std::make_unique<SimulationRun>();
	const Description now = describe(run);
	const Description initial = describe(*fresh);
	std::vector<std::string> names;
	for (std::size_t i = 0; i < now.size(); ++i)
		if (now[i] == initial[i])
			names.push_back(now[i].first);
	return names;
}

bool isNewRun(const SimulationRun& run) {
	return membersOfNewRun(run).size() == describe(run).size();
}

// True when the members of the infrastructure are as in a new run, or when none of them is.
bool infrastructureIs(const SimulationRun& run, bool isNew) {
	const std::vector<std::string> unchanged = membersOfNewRun(run);
	for (const auto& [name, text] : describe(run))
		if (isInfrastructureMember(name) && (std::find(unchanged.begin(), unchanged.end(), name) != unchanged.end()) != isNew)
			return false;
	return true;
}

// True when the run is as a new run in every member that the builders do not fill. A prepared scene has its settings, infrastructure,
// platforms, passengers, incidents, measures and seeded generator.
bool carriesNothingOver(const SimulationRun& run) {
	const std::set<std::string> builderState = {"settings", "incidents", "measures", "platforms", "numPlatforms", "passengers", "numPassengers",
		"passengerIndex", "platformIndex", "passengerFlow", "trains", "numTrains", "numberGenerator"};
	const std::vector<std::string> unchanged = membersOfNewRun(run);
	for (const auto& [name, text] : describe(run))
		if (!isInfrastructureMember(name) && builderState.count(name) == 0
			&& std::find(unchanged.begin(), unchanged.end(), name) == unchanged.end())
			return false;
	return true;
}

// Gives every member of the run a value that a new run does not have.
void fill(SimulationRun& run) {
	auto& [settings, occupied, connected, authorities, incidents, measures, nextMeasure, appliedMeasures, vcTimesteps, vcTrains, vcTexts, platforms, numPlatforms, passengers, numPassengers,
		passengerIndex, platformIndex, passengerFlow, passengerJourneys, stopCalls, totalDelays, entranceDelays, disturbances, finalStation, trains, numTrains, generator, delay, egtrainTime, romaTime,
		infrastructure] = run;
	(void)passengerJourneys;
	stopCalls.at(0, 0).boarded = 1;
	settings.name = "scene";
	settings.times = 90;
	settings.OutputMainFolder = "output";
	settings.bufferTime = 2.5;
	settings.recoveryTimePercentage = 3.5;
	settings.GUI = false;
	occupied.push_back("1-B0");
	connected.push_back("2-B0");
	authorities.emplace_back();
	incidents.emplace_back();
	measures.emplace_back();
	nextMeasure = 1;
	appliedMeasures.emplace_back();
	vcTimesteps.push_back(7);
	vcTrains.push_back("T1-1");
	vcTexts.push_back("Train is now decoupled");
	platforms.emplace_back();
	numPlatforms = 1;
	passengers.emplace_back();
	numPassengers = 1;
	passengerIndex.build(passengers);
	platformIndex.build(platforms);
	passengerFlow.build(passengers);
	for (DelayStats* statistics : {&totalDelays, &entranceDelays, &disturbances, &finalStation}) {
		statistics->N_Stopped_Trains = 4;
		statistics->totalArrivalDelay = 30;
	}
	trains.resize(2);
	numTrains = 2;
	generator.getUniformFloat();
	delay = 2;
	egtrainTime = 1.5;
	romaTime = 0.5;

	infrastructure.blockSets.resize(1);
	infrastructure.connections.resize(1);
	infrastructure.stations.resize(1);
	infrastructure.blockSets[0].sceneTrackId = "line";
	infrastructure.blockSets[0].N.resize(2);
	infrastructure.connections[0].ID = "switch";
	infrastructure.stations[0].stationName = "Station";
	infrastructure.numTrackLines = infrastructure.numConnections = infrastructure.numStations = 1;
	infrastructure.sections.resize(2);
	infrastructure.numBlocks = 2;
	infrastructure.detectionSections.emplace_back();
	infrastructure.infraEvents.emplace_back();
	infrastructure.derivedSectionsExceedPlan = true;
	infrastructure.singleTrackLimits.emplace_back("s1", "s2", "s0", "s3");
	infrastructure.singleTrackHeld.push_back(1);
	infrastructure.singleTrackReservations.emplace_back();
	infrastructure.singleTrackZones.emplace_back();
	infrastructure.singleTrackZoneLimits = infrastructure.singleTrackZoneRoutes = 1;
	infrastructure.singleTrackRouteZone.push_back(1);
	infrastructure.singleTrackZonesDerived = true;
	infrastructure.singleTrackClosures.push_back(0);
	infrastructure.singleTrackKeptOut.push_back(1);
	infrastructure.stationBoundaries.emplace_back(&infrastructure.sections[0], false, nullptr);
	infrastructure.routes.resize(1);
	infrastructure.numRoutes = 1;
	infrastructure.sectionIdNumbers.emplace("1-B0", 0);
	infrastructure.routeSectionNumbers.push_back({0});
}

// The files a run wrote, by path below its output folder, with their content.
using Files = std::map<std::string, std::string>;

struct Outcome {
	bool prepared = false;
	// Nothing of an earlier run is left when the run starts.
	bool startedClean = false;
	bool completed = false;
	// The trains belong to the run of the process and the indexes describe its lists.
	bool adopted = false;
	// The snapshot of the last step that the controller published.
	std::shared_ptr<const GuiSimulationSnapshot> lastSnapshot;
	// The branch of each step of each train, kept when the snapshot of the step is published; -1 for a step without one.
	std::vector<std::vector<int>> stepBranches;
	Files files;
};

// Keeps in branches the branch that each train of run took in step `step`.
void recordStepBranches(const SimulationRun& run, int step, std::vector<std::vector<int>>& branches) {
	branches.resize(run.trains.size());
	for (std::size_t i = 0; i < run.trains.size(); ++i) {
		std::vector<int>& train = branches[i];
		if (train.size() <= static_cast<std::size_t>(step))
			train.resize(static_cast<std::size_t>(step) + 1, -1);
		train[static_cast<std::size_t>(step)] = run.trains[i].stepBranch;
	}
}

// Reads the files below root, except Computing_Times.txt, whose wall-clock timings differ between any two runs.
Files readFiles(const std::filesystem::path& root) {
	Files files;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
		if (!entry.is_regular_file())
			continue;
		const std::string name = std::filesystem::relative(entry.path(), root).generic_string();
		if (name.find("Computing_Times.txt") != std::string::npos)
			continue;
		std::ifstream input(entry.path(), std::ios::binary);
		std::ostringstream content;
		content << input.rdbuf();
		files[name] = content.str();
	}
	return files;
}

struct RunSetup {
	const SceneModel* scene = nullptr;
	SceneRunSelection selection;
	int horizonSeconds = 0;
	bool passengerGui = false;
	bool detailedFiles = false;
	// The step whose snapshot is followed by a stop request; -1 lets the run end.
	int stopAtSnapshot = -1;
	// The settings of the application change after the scene is prepared: another output folder and a shorter horizon.
	bool changeSettingsAfterPreparation = false;
};

// True when every train of the run belongs to it and the indexes describe the lists of the run.
bool runIsWhole(SimulationRun& run) {
	return !run.trains.empty()
		&& std::all_of(run.trains.begin(), run.trains.end(), [&run](const Regional& train) { return train.run_ == &run; })
		&& run.platformIndex.covers(run.platforms) && run.passengerIndex.covers(run.passengers);
}

// Prepares the scene and runs it, as a run without a window does.
Outcome runScene(const RunSetup& setup) {
	Outcome outcome;
	QTemporaryDir output;
	initial_variables.GUI = 0;
	initial_variables.PAX_GUI = setup.passengerGui;
	initial_variables.durationOverride = true;
	initial_variables.times = setup.horizonSeconds;
	initial_variables.exportDetailedTrajectories = setup.detailedFiles;
	initial_variables.OutputMainFolder = output.path().toStdString();
	initial_variables.InputMainFolder.clear();

	CoutSilencer silencer;
	outcome.prepared = !hasErrors(simulation.prepareScene(*setup.scene, "baseline", setup.selection)) && simulation.run().numTrains > 0;
	if (!outcome.prepared)
		return outcome;
	outcome.startedClean = carriesNothingOver(defaultRun());
	outcome.adopted = runIsWhole(defaultRun()) && &simulation.run() == &defaultRun();
	if (setup.changeSettingsAfterPreparation) {
		initial_variables.OutputMainFolder = (std::filesystem::path(output.path().toStdString()) / "elsewhere").string();
		initial_variables.times = 1;
	}
	int snapshots = 0;
	QObject context;
	QObject::connect(&simulation, &DispatchController::snapshotAvailable, &context, [&] {
		// The mailbox only emits again once the snapshot has been taken.
		if (const auto snapshot = simulation.takeSimulationSnapshot()) {
			outcome.lastSnapshot = snapshot;
			recordStepBranches(simulation.run(), snapshot->timestep, outcome.stepBranches);
		}
		if (++snapshots == setup.stopAtSnapshot)
			simulation.runControl()->requestStop(); }, Qt::DirectConnection);
	simulation.runSimulation();
	outcome.completed = simulation.lastRunCompleted();
	outcome.files = readFiles(output.path().toStdString());
	return outcome;
}

// One network-wide area at ETCS level 3, so that the trains report their positions to the RBC at every step.
SceneModel movingBlockLine(const SceneModel& line) {
	SceneModel scene = line;
	SceneSignallingArea area;
	area.id = "area.all";
	area.startKm = 0.0;
	area.endKm = 16.0;
	area.level = 3;
	scene.signallingAreas = {area};
	return scene;
}

// The line with a service X that copies T1 without its entry time and its first stop, so that its train starts at B, in the
// middle of the route, with a dwell of 30 s and the given planned departure. T1 stands at B from 331 s to 660 s.
SceneModel lineWithStartAtB(const SceneModel& line, int level, double plannedDeparture) {
	SceneModel scene = movingBlockLine(line);
	scene.signallingAreas[0].level = level;
	SceneService starting = *std::find_if(scene.services.begin(), scene.services.end(),
		[](const SceneService& candidate) { return candidate.id == "T1"; });
	starting.id = starting.operatingCode = "X";
	starting.hasEntryTime = false;
	starting.stops.erase(starting.stops.begin());
	starting.stops[0].hasPlannedArrival = false;
	starting.stops[0].plannedDepartureSeconds = plannedDeparture;
	starting.stops[0].dwellSeconds = 30.0;
	scene.services.push_back(starting);
	return scene;
}

// The selected occurrences of scene in a run of its own, stepped for the given seconds. Empty when the run cannot be built.
std::unique_ptr<SimulationRun> steppedRun(const SceneModel& scene, const SceneRunSelection& selection, int seconds) {
	CoutSilencer silencer;
	// The run takes the duration of the scene, not a -h duration that an earlier test left in initial_variables: the
	// trains keep one sample per second of that duration, and stepping past it writes past their vectors.
	const bool durationOverride = initial_variables.durationOverride;
	initial_variables.durationOverride = false;
	const auto restoreOverride = qScopeGuard([durationOverride] { initial_variables.durationOverride = durationOverride; });
	auto run = std::make_unique<SimulationRun>();
	if (hasErrors(buildInfrastructureAndSignallingFromScene(*run, scene))
		|| hasErrors(buildOperationsFromScene(*run, scene, "baseline", selection)))
		return nullptr;
	if (seconds > scene.settings.durationSeconds)
		return nullptr;
	run->settings = initial_variables;
	run->settings.durationOverride = false;
	run->settings.times = scene.settings.durationSeconds;
	TrainMovementOrder order(*run);
	for (int t = 0; t < seconds; ++t)
		runSimulationStep(*run, t, order);
	return run;
}

// The train of the run with this description.
const Regional& trainOf(const SimulationRun& run, const char* description) {
	return *std::find_if(run.trains.begin(), run.trains.end(),
		[description](const Regional& train) { return train.trainDescription == description; });
}

// The run completes in the place of the earlier run and writes the files of the expected run. A failed check ends this
// function only, so the caller checks QTest::currentTestFailed() when more checks follow.
void checkSameFiles(const std::string& label, const Outcome& expected, const Outcome& actual) {
	// The run completes.
	QVERIFY2(actual.prepared, (label + ": the run completes").c_str());
	QVERIFY2(actual.completed, (label + ": the run completes").c_str());
	QVERIFY2(actual.startedClean, (label + ": nothing of an earlier run is left when the run starts").c_str());
	QVERIFY2(actual.adopted, (label + ": the prepared run is whole in the place of the earlier one").c_str());
	// The run writes the same files.
	QVERIFY2(!expected.files.empty(), (label + ": the run writes the same files").c_str());
	QVERIFY2(actual.files.size() == expected.files.size(), (label + ": the run writes the same files").c_str());
	for (const auto& [name, content] : expected.files) {
		const auto found = actual.files.find(name);
		if (found == actual.files.end())
			QFAIL((label + ": " + name + " is missing").c_str());
		QVERIFY2(found->second == content, (label + ": " + name + " differs").c_str());
	}
}

// The parts of two snapshots that the window draws, compared field by field.
bool sameSnapshot(const GuiSimulationSnapshot& a, const GuiSimulationSnapshot& b) {
	const auto sameTrain = [](const GuiTrainState& x, const GuiTrainState& y) {
		return x.index == y.index && x.id == y.id && x.description == y.description && x.routeIndex == y.routeIndex
			&& x.reversedDirection == y.reversedDirection && x.wagonCount == y.wagonCount && x.departureTime == y.departureTime
			&& x.routeAxisPosition == y.routeAxisPosition && x.speedKmh == y.speedKmh && x.wagonHeadPositions == y.wagonHeadPositions
			&& x.wagonTailPositions == y.wagonTailPositions && x.occupiedArcs.size() == y.occupiedArcs.size();
	};
	const auto sameSignal = [](const GuiSignalState& x, const GuiSignalState& y) {
		return x.sectionId == y.sectionId && x.code == y.code && x.reversedDirection == y.reversedDirection && x.level == y.level
			&& x.failed == y.failed;
	};
	const auto sameSection = [](const GuiSectionState& x, const GuiSectionState& y) {
		return x.sectionId == y.sectionId && x.prepared == y.prepared && x.blocked == y.blocked;
	};
	return a.timestep == b.timestep && a.totalTimesteps == b.totalTimesteps
		&& std::equal(a.trains.begin(), a.trains.end(), b.trains.begin(), b.trains.end(), sameTrain)
		&& std::equal(a.signalStates.begin(), a.signalStates.end(), b.signalStates.begin(), b.signalStates.end(), sameSignal)
		&& std::equal(a.sectionStates.begin(), a.sectionStates.end(), b.sectionStates.begin(), b.sectionStates.end(), sameSection)
		&& a.platforms.size() == b.platforms.size();
}

} // namespace

// The slots share the run of the process and run in the order of their declaration.
class TestSimulationRun : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void resetReturnsNewRun();
	void laterRunsWriteFilesOfFirstRun();
	void runKeepsCopyOfSettings();
	void byHandMeasureTakesEffectAtTheNextStep();
	void secondRunFollowsRunOfProcess();
	void stepsRunWithoutController();
	void trainWithoutEntryTimeLeavesFirstStopOnTime();
	void trainStartingAtStopWaitsForItsSections_data();
	void trainStartingAtStopWaitsForItsSections();
	void trainStartingAtStopWaitsForSingleTrack();
	void paimpolTrainsCrossOnSingleTrack();
	void paimpolNarrowRestrictionsCrossOnSingleTrack();
	void paimpolLongDwellsTwiceCrossOnSingleTrack();
	void trainStartingAtStopBlocksPointsUnderIt_data();
	void trainStartingAtStopBlocksPointsUnderIt();
	void trainStartingAtStopBlocksSectionsUnderIt_data();
	void trainStartingAtStopBlocksSectionsUnderIt();
	void entryWaitsForTrainThatCannotStop_data();
	void entryWaitsForTrainThatCannotStop();
	void platformListsAtMostFiftyPassengers();
	void platformCountsWaitingWithoutPassengerGui();

private:
	SceneModel m_line;
	SceneModel m_passengers;
};

void TestSimulationRun::initTestCase() {
	SceneLoadResult line = loadScene(EGTRAIN_TEST_SOURCE_DIR "/fixtures/scenes/line");
	SceneLoadResult passengers = loadScene(EGTRAIN_SCENES_DIR "/Paimpol");
	// The scenes load.
	QVERIFY(!hasErrors(line.diagnostics));
	QVERIFY(!hasErrors(passengers.diagnostics));
	m_line = std::move(line.scene);
	m_passengers = std::move(passengers.scene);
}

void TestSimulationRun::resetReturnsNewRun() {
	SimulationRun& run = defaultRun();
	SimulationRun* const address = &run;
	QVERIFY2(isNewRun(run), "the run of the process starts as a new run");

	fill(run);
	QVERIFY2(membersOfNewRun(run).empty(), "the test fills every member");
	RunInfrastructure& infra = run.infra;
	QCOMPARE(infra.blockSets.size(), std::size_t(1));
	// The infrastructure shows the values that the test set.
	QCOMPARE(infra.blockSets[0].sceneTrackId, "line");
	QCOMPARE(infra.connections[0].ID, "switch");
	QCOMPARE(infra.stations[0].stationName, "Station");
	QCOMPARE(infra.numBlocks, 2);
	QCOMPARE(infra.sections.size(), std::size_t(2));
	QCOMPARE(infra.detectionSections.size(), std::size_t(1));
	QCOMPARE(infra.routes.size(), std::size_t(1));
	QCOMPARE(infra.numRoutes, 1);
	// The members show the values that the test set.
	QCOMPARE(run.blocksOccupied.size(), std::size_t(1));
	QCOMPARE(run.etcsMa.size(), std::size_t(1));
	QCOMPARE(run.incidents.size(), std::size_t(1));
	QCOMPARE(run.vcMsgText.size(), std::size_t(1));
	QCOMPARE(run.passengers.size(), std::size_t(1));
	QCOMPARE(run.numPlatforms, 1);
	QCOMPARE(run.finalStation.N_Stopped_Trains, 4);
	QCOMPARE(run.numTrains, 2);
	QCOMPARE(run.trains.size(), std::size_t(2));
	QCOMPARE_EQ(run.signallingDelay, 2.0);

	run.reset();
	// Reset leaves the infrastructure in place, because the scene builder replaces it.
	QVERIFY(infrastructureIs(run, false));
	QCOMPARE(infra.numBlocks, 2);
	QCOMPARE(infra.routes.size(), std::size_t(1));

	run.resetInfrastructure();
	QVERIFY2(isNewRun(run), "reset and resetInfrastructure return every member to the state of a new run");
	// A reset keeps the run at its address.
	QCOMPARE(&defaultRun(), address);
	QVERIFY(run.blocksOccupied.empty());
	QVERIFY(run.etcsMa.empty());
	QVERIFY(run.passengers.empty());
	QVERIFY(run.trains.empty());
	QCOMPARE(run.numTrains, 0);
	QCOMPARE_EQ(run.signallingDelay, 0.0);
	QCOMPARE(infra.numBlocks, 0);
	QVERIFY(infra.routes.empty());
	QCOMPARE(infra.numRoutes, 0);
	QVERIFY(infra.detectionSections.empty());
	QVERIFY(infra.blockSets.empty());

	// The generator of a seeded run starts again from the default seed.
	run.numberGenerator = NumberGenerator(5);
	run.reset();
	QVERIFY2(isNewRun(run), "reset returns the generator to the default seed");
}

void TestSimulationRun::laterRunsWriteFilesOfFirstRun() {
	const SceneModel movingBlock = movingBlockLine(m_line);
	RunSetup lineRun;
	lineRun.scene = &movingBlock;
	lineRun.selection = {{"T1", 1}};
	lineRun.horizonSeconds = 1500;
	lineRun.detailedFiles = true;
	RunSetup passengerRun;
	passengerRun.scene = &m_passengers;
	passengerRun.horizonSeconds = 2400;
	passengerRun.passengerGui = true;

	// The first runs of the process: the files that later runs have to write again.
	const Outcome firstLine = runScene(lineRun);
	// The line scene runs.
	QVERIFY(firstLine.prepared);
	QVERIFY(firstLine.completed);
	QVERIFY(!firstLine.files.empty());
	const Outcome firstPassengers = runScene(passengerRun);
	// The passenger scene runs.
	QVERIFY(firstPassengers.prepared);
	QVERIFY(firstPassengers.completed);
	QVERIFY(!simulation.run().passengers.empty());

	checkSameFiles("passengers, second run", firstPassengers, runScene(passengerRun));
	if (QTest::currentTestFailed())
		return;

	// The routines of the run read its settings, so settings of the application that change after preparation reach no file.
	RunSetup changedRun = lineRun;
	changedRun.changeSettingsAfterPreparation = true;
	checkSameFiles("a change of the settings after preparation", firstLine, runScene(changedRun));
	if (QTest::currentTestFailed())
		return;

	RunSetup stoppedRun = lineRun;
	stoppedRun.stopAtSnapshot = 300;
	const Outcome stopped = runScene(stoppedRun);
	// The line run stops part way.
	QVERIFY(stopped.prepared);
	QVERIFY(!stopped.completed);
	QVERIFY2(!simulation.run().etcsMa.empty(), "the stopped run leaves movement authorities behind");
	checkSameFiles("a scene after a stopped run", firstPassengers, runScene(passengerRun));
	if (QTest::currentTestFailed())
		return;

	// A preparation that fails after the run state was used. The state has to be gone when the next scene prepares.
	fill(defaultRun());
	const std::vector<SceneDiagnostic> rejected = simulation.prepareScene(m_line, "no.such.scenario");
	QVERIFY2(hasErrors(rejected), "a scene with an unknown scenario is rejected");
	QVERIFY2(isNewRun(defaultRun()), "a rejected preparation leaves a new run");
	checkSameFiles("a scene after a failed preparation", firstLine, runScene(lineRun));
}

// prepareScene copies the settings of the application into the run, and the controller goes on with the copy when the application
// changes its settings. The buffer time, the recovery time and the output folder are the ones that the files of the run show.
void TestSimulationRun::runKeepsCopyOfSettings() {
	QTemporaryDir prepared;
	QTemporaryDir changed;
	initial_variables.GUI = 0;
	initial_variables.PAX_GUI = false;
	initial_variables.durationOverride = true;
	initial_variables.times = 600;
	initial_variables.exportDetailedTrajectories = false;
	initial_variables.bufferTimeOverride = true;
	initial_variables.bufferTime = 2.5;
	initial_variables.recoveryTimeOverride = true;
	initial_variables.recoveryTimePercentage = 3.5;
	initial_variables.OutputMainFolder = prepared.path().toStdString();
	const auto clearOverrides = qScopeGuard([] {
		initial_variables.bufferTimeOverride = false;
		initial_variables.recoveryTimeOverride = false;
	});

	CoutSilencer silencer;
	// The scene is prepared.
	QVERIFY(!hasErrors(simulation.prepareScene(m_line, "baseline", {{"T1", 1}})));
	QCOMPARE_GT(simulation.run().numTrains, 0);
	const InitialParameters& settings = defaultRun().settings;
	// prepareScene copies the settings, with the buffer time, the recovery time and the output folder.
	QCOMPARE_EQ(settings.times, 600.0);
	QCOMPARE_EQ(settings.bufferTime, 2.5);
	QCOMPARE_EQ(settings.recoveryTimePercentage, 3.5);
	QCOMPARE(settings.OutputMainFolder, prepared.path().toStdString());
	QCOMPARE(settings.name, m_line.name);

	initial_variables.bufferTime = 9;
	initial_variables.recoveryTimePercentage = 9;
	initial_variables.OutputMainFolder = changed.path().toStdString();
	// A change to the settings of the application does not reach the copy.
	QCOMPARE_EQ(settings.bufferTime, 2.5);
	QCOMPARE_EQ(settings.recoveryTimePercentage, 3.5);
	QCOMPARE(settings.OutputMainFolder, prepared.path().toStdString());
	simulation.runSimulation();
	const std::filesystem::path trajectories = std::filesystem::path(prepared.path().toStdString()) / "TrainTrajectories";
	// The run writes to the output folder of the copy.
	QVERIFY(simulation.lastRunCompleted());
	QVERIFY(std::filesystem::exists(trajectories / "Stats_Stations.txt"));
	QVERIFY(!std::filesystem::exists(std::filesystem::path(changed.path().toStdString()) / "TrainTrajectories"));

	simulation.resetState();
	QVERIFY2(isNewRun(defaultRun()), "a reset returns the settings of the run to those of a new run");
}

// A measure taken by hand while the run goes on reaches the run through the queue of the controller and takes effect at the top
// of the next step, through applyMeasure, which records it. The run refuses a measure once it has no step left. The window submits
// from its own thread; here the measure is submitted right after the snapshot of a step, on the thread of the run.
void TestSimulationRun::byHandMeasureTakesEffectAtTheNextStep() {
	QTemporaryDir output;
	initial_variables.GUI = 0;
	initial_variables.PAX_GUI = false;
	initial_variables.durationOverride = true;
	initial_variables.times = 600;
	initial_variables.exportDetailedTrajectories = false;
	initial_variables.OutputMainFolder = output.path().toStdString();
	CoutSilencer silencer;
	QVERIFY(!hasErrors(simulation.prepareScene(m_line, "baseline", {{"T1", 1}})));
	QCOMPARE(simulation.run().numTrains, 1);
	// Before the run, the window offers every stop of the train.
	QVERIFY(!simulation.trainStopsAhead(0, -9999.0).empty());
	QVERIFY(simulation.trainStopsAhead(1, -9999.0).empty());
	DispatchMeasure measure;
	measure.id = "by_hand_1";
	measure.serviceId = "T1";
	measure.occurrence = 1;
	// No run goes on yet.
	QVERIFY(!simulation.submitMeasure(measure));

	int submittedAt = -1;
	QObject context;
	QObject::connect(&simulation, &DispatchController::snapshotAvailable, &context, [&] {
		const auto snapshot = simulation.takeSimulationSnapshot();
		if (snapshot && submittedAt < 0 && snapshot->timestep >= 200 && simulation.submitMeasure(measure))
			submittedAt = snapshot->timestep; }, Qt::DirectConnection);
	simulation.runSimulation();
	QVERIFY(simulation.lastRunCompleted());
	QCOMPARE_GE(submittedAt, 200);
	const SimulationRun& run = simulation.run();
	QCOMPARE(run.appliedMeasures.size(), std::size_t{1});
	const AppliedMeasure& applied = run.appliedMeasures.front();
	QCOMPARE(applied.measure.id, std::string("by_hand_1"));
	QVERIFY(applied.measure.source == MeasureSource::ByHand);
	QCOMPARE(applied.step, submittedAt + 1);
	QCOMPARE_EQ(applied.measure.timeSeconds, static_cast<double>(submittedAt + 1));
	QCOMPARE(applied.effects.size(), std::size_t{1});
	QVERIFY2(measureChangedTrain(applied.effects.front()), measureEffectText(applied.effects.front()).c_str());
	QVERIFY(run.trains[0].OutOfSimulation);
	QCOMPARE_LT(run.trains[0].End_Time, 600);
	// After the last step the queue refuses a measure instead of keeping it.
	QVERIFY(!simulation.submitMeasure(measure));

	simulation.resetState();
}

// The builders and the routines of a step work on the run that they are given. A second run, built from the scene and stepped while
// the run of the process is a new run, follows the trajectory that the controller gives the same scene in the run of the process, and
// leaves the run of the process as it was.
void TestSimulationRun::secondRunFollowsRunOfProcess() {
	const SceneModel movingBlock = movingBlockLine(m_line);
	RunSetup setup;
	setup.scene = &movingBlock;
	setup.selection = {{"T1", 1}};
	setup.horizonSeconds = 600;
	const Outcome reference = runScene(setup);
	// The controller runs the scene in the run of the process.
	QVERIFY(reference.prepared);
	QVERIFY(reference.completed);
	QVERIFY(!defaultRun().trains.empty());
	const std::vector<double> positions = defaultRun().trains[0].instant_spatial_position;
	const std::vector<double> speeds = defaultRun().trains[0].instant_train_speed;
	const std::string description = defaultRun().trains[0].trainDescription;

	CoutSilencer silencer;
	simulation.resetState();
	QVERIFY2(isNewRun(defaultRun()), "the run of the process is a new run before the second run is built");
	const auto other = std::make_unique<SimulationRun>();
	QVERIFY2(!hasErrors(buildInfrastructureAndSignallingFromScene(*other, movingBlock)), "the infrastructure is built into a run of its own");
	QVERIFY2(!hasErrors(buildOperationsFromScene(*other, movingBlock, "baseline", setup.selection)), "the operations are built into a run of its own");
	// The second run holds the trains and the infrastructure.
	QCOMPARE(other->numTrains, 1);
	QCOMPARE_GT(other->infra.numBlocks, 0);
	QVERIFY(!other->infra.routes.empty());
	// A train of the second run belongs to it.
	QVERIFY(!other->trains.empty());
	QVERIFY(other->trains[0].run_ == other.get());
	other->settings = initial_variables;
	for (Regional& train : other->trains)
		train.departure_time = train.scheduled_departure_time;
	TrainMovementOrder order(*other);
	for (int t = 0; t < setup.horizonSeconds; ++t)
		runSimulationStep(*other, t, order);
	// The second run follows the trajectory of the same scene in the run of the process.
	QCOMPARE(other->trains[0].trainDescription, description);
	QVERIFY(other->trains[0].instant_spatial_position == positions);
	QVERIFY(other->trains[0].instant_train_speed == speeds);
	QVERIFY2(isNewRun(defaultRun()), "building and stepping another run leaves the run of the process as it was");

	// The snapshot is made from the run that is given: the second run shows its trains and signals, the new run of the process
	// shows none, and the snapshot equals the one that the controller published for the last step of the same scene.
	const int lastStep = setup.horizonSeconds - 1;
	const GuiSimulationSnapshot fromOther = buildGuiSimulationSnapshot(*other, lastStep);
	const GuiSimulationSnapshot fromProcess = buildGuiSimulationSnapshot(defaultRun(), lastStep);
	// The snapshot shows the run that it is made from.
	QCOMPARE(fromOther.trains.size(), std::size_t(1));
	QCOMPARE(fromOther.trains[0].description, description);
	QCOMPARE_EQ(fromOther.trains[0].routeAxisPosition, positions[lastStep]);
	QCOMPARE(fromOther.totalTimesteps, setup.horizonSeconds);
	QVERIFY(!fromOther.signalStates.empty());
	QVERIFY(!fromOther.sectionStates.empty());
	// The snapshot of the new run of the process shows nothing of the second run.
	QVERIFY(fromProcess.trains.empty());
	QVERIFY(fromProcess.signalStates.empty());
	QVERIFY(fromProcess.sectionStates.empty());
	QCOMPARE(fromProcess.totalTimesteps, 0);
	// The snapshot of the second run equals the one that the controller published for the same scene.
	if (reference.lastSnapshot == nullptr)
		QFAIL("the controller published no snapshot");
	QCOMPARE(reference.lastSnapshot->timestep, lastStep);
	QVERIFY(sameSnapshot(*reference.lastSnapshot, fromOther));
}

// The simulation library runs a case without the controller. A run that the builders make and that runSimulationStep steps, from
// the first step to the last, moves the trains and the passengers of the passenger scene as the controller does in the run of the
// process, to the end of the routes. runSimulationPostprocessing then gives it the blocking times and the files of that run.
void TestSimulationRun::stepsRunWithoutController() {
	RunSetup setup;
	setup.scene = &m_passengers;
	// The duration of the scene, in which both trains reach the end of their routes.
	setup.horizonSeconds = 9000;
	setup.passengerGui = true;
	const Outcome reference = runScene(setup);
	// The controller runs the scene in the run of the process.
	QVERIFY(reference.prepared);
	QVERIFY(reference.completed);
	const SimulationRun& controlled = defaultRun();
	QCOMPARE_GT(controlled.numTrains, 1);
	QVERIFY(!controlled.passengers.empty());

	CoutSilencer silencer;
	const auto other = std::make_unique<SimulationRun>();
	QVERIFY(!hasErrors(buildInfrastructureAndSignallingFromScene(*other, m_passengers)));
	QVERIFY(!hasErrors(buildOperationsFromScene(*other, m_passengers, "baseline", setup.selection)));
	other->settings = initial_variables;
	QCOMPARE_EQ(other->settings.times, controlled.settings.times);
	QVERIFY(other->settings.PAX_GUI);
	TrainMovementOrder order(*other);
	std::vector<std::vector<int>> stepBranches;
	for (int t = 0; t < other->settings.times; ++t) {
		runSimulationStep(*other, t, order);
		recordStepBranches(*other, t, stepBranches);
	}

	// Every train moves as in the run of the controller, and the run ends with the trains out of the network.
	QCOMPARE(other->numTrains, controlled.numTrains);
	int finished = 0;
	for (int i = 0; i < controlled.numTrains; ++i) {
		const Regional& expected = controlled.trains[i];
		const Regional& actual = other->trains[i];
		const std::string label = expected.trainDescription;
		QCOMPARE(actual.trainDescription, expected.trainDescription);
		QVERIFY2(actual.instant_spatial_position == expected.instant_spatial_position, (label + ": positions").c_str());
		QVERIFY2(actual.instant_train_speed == expected.instant_train_speed, (label + ": speeds").c_str());
		QVERIFY2(stepBranches[static_cast<std::size_t>(i)] == reference.stepBranches[static_cast<std::size_t>(i)],
			(label + ": branches of the steps").c_str());
		QCOMPARE(actual.End_Time, expected.End_Time);
		QCOMPARE(actual.OutOfSimulation, expected.OutOfSimulation);
		QCOMPARE(actual.Current_OnBoard_Passengers, expected.Current_OnBoard_Passengers);
		finished += actual.OutOfSimulation ? 1 : 0;
	}
	QCOMPARE_GT(finished, 0);
	// Every passenger ends the run where the passenger ends it in the run of the controller.
	QCOMPARE(other->passengers.size(), controlled.passengers.size());
	int exited = 0;
	auto expectedPassenger = controlled.passengers.begin();
	for (const Passenger& actual : other->passengers) {
		QCOMPARE(actual.ID, expectedPassenger->ID);
		QCOMPARE(actual.CurrentStatus, expectedPassenger->CurrentStatus);
		QCOMPARE(actual.IsIntheNetwork, expectedPassenger->IsIntheNetwork);
		QCOMPARE_EQ(actual.TimeExitedTheNetwork, expectedPassenger->TimeExitedTheNetwork);
		exited += !actual.IsIntheNetwork && actual.TimeExitedTheNetwork > 0 ? 1 : 0;
		++expectedPassenger;
	}
	QCOMPARE_GT(exited, 0);
	// The trains and the signalling of the last step are those of the run of the controller.
	if (reference.lastSnapshot == nullptr)
		QFAIL("the controller published no snapshot");
	const int lastStep = static_cast<int>(other->settings.times) - 1;
	QCOMPARE(reference.lastSnapshot->timestep, lastStep);
	QVERIFY(sameSnapshot(*reference.lastSnapshot, buildGuiSimulationSnapshot(*other, lastStep)));

	// The stages after the steps also need no controller. They give every train the blocking times of the run of the controller and
	// write its files, except PassengerStatus.txt, which the controller writes after the steps. Without detailed files they look for
	// a stop four times.
	QTemporaryDir output;
	const std::filesystem::path outputFolder = output.path().toStdString();
	std::filesystem::create_directories(outputFolder / "TrainTrajectories");
	std::filesystem::create_directories(outputFolder / "PassengerStatus");
	other->settings.OutputMainFolder = outputFolder.string();
	int stopChecks = 0;
	const bool stagesEnded = runSimulationPostprocessing(*other, [&stopChecks] {
		++stopChecks;
		return false;
	});
	QVERIFY(stagesEnded);
	QCOMPARE(stopChecks, 4);
	int blockingTimes = 0;
	for (int i = 0; i < controlled.numTrains; ++i) {
		QCOMPARE(other->trains[i].N_BlockSections, controlled.trains[i].N_BlockSections);
		blockingTimes += other->trains[i].N_BlockSections;
	}
	QCOMPARE_GT(blockingTimes, 0);
	const Files written = readFiles(outputFolder);
	QVERIFY(written.count("TrainTrajectories/TimetablePoints.txt") == 1);
	QVERIFY(written.count("TrainTrajectories/Stats_Stations.txt") == 1);
	QVERIFY(written.count("TrainTrajectories/Pos&Neg_Stats_Stations.txt") == 1);
	QVERIFY(written.count("TrainTrajectories/BlockingTimes.txt") == 1);
	QVERIFY(written.count("PassengerStatus/JourneyDelays.txt") == 1);
	for (const auto& [name, content] : written) {
		const auto found = reference.files.find(name);
		if (found == reference.files.end())
			QFAIL((name + " is not a file of the run of the controller").c_str());
		QVERIFY2(found->second == content, (name + " differs").c_str());
	}

	// A stop at the first check ends the stages after the energy files.
	QTemporaryDir stoppedOutput;
	other->settings.OutputMainFolder = stoppedOutput.path().toStdString();
	QVERIFY(!runSimulationPostprocessing(*other, [] { return true; }));
	const Files stoppedFiles = readFiles(stoppedOutput.path().toStdString());
	QCOMPARE(stoppedFiles.size(), std::size_t(2));
	QVERIFY(stoppedFiles.count("EnergyConsumptionPerTrain.txt") == 1);
	QVERIFY(stoppedFiles.count("TotalEnergyConsumption.txt") == 1);
}

// The train of a service without an entry time starts at its first stop, here B in the middle of the route with a dwell of 60 s
// and a planned departure at 660 s. It stands there from 600 s and leaves at 660 s, and the stop has both times in the results.
// Its timetable entry time stays the planned departure, and the section of the stop has a blocking time from 600 s.
void TestSimulationRun::trainWithoutEntryTimeLeavesFirstStopOnTime() {
	SceneModel scene = movingBlockLine(m_line);
	scene.signallingAreas[0].level = 0;
	const auto service = std::find_if(scene.services.begin(), scene.services.end(),
		[](const SceneService& candidate) { return candidate.id == "T1"; });
	QVERIFY(service != scene.services.end());
	service->hasEntryTime = false;
	service->stops.erase(service->stops.begin());

	const std::unique_ptr<SimulationRun> run = steppedRun(scene, {{"T1", 1}}, 1500);
	if (run == nullptr || run->numTrains != 1)
		QFAIL("the run is not built");
	Regional& train = run->trains[0];
	const double atStop = 8000.0 - kStopHoldbackM;
	QCOMPARE_EQ(train.departure_time, 600.0);
	QCOMPARE_EQ(train.scheduled_departure_time, 660.0);
	QCOMPARE(train.instant_spatial_position[600], atStop);
	QCOMPARE(train.instant_spatial_position[660], atStop);
	QCOMPARE_GT(train.instant_spatial_position[661], atStop);
	train.ComputeTimetablingPoints();
	const TrainEvent* firstStop = train.timetablePointOfStop(0);
	if (firstStop == nullptr)
		QFAIL("the first stop has no timetable point");
	QCOMPARE_EQ(firstStop->Time, 600.0);
	QCOMPARE_EQ(firstStop->Time2, 660.0);

	train.ComputeBlockingTimesInMixedSignallingAreas(5, 3, 0.5, 0, 0, 0);
	const BlockingTimes* ofStop = nullptr;
	for (int index = 0; index < train.N_BlockSections; ++index)
		if (train.BlockTime[index].BlockID == "@3-B0@")
			ofStop = &train.BlockTime[index];
	if (ofStop == nullptr)
		QFAIL("the section of the stop has no blocking time");
	QVERIFY(ofStop->IsComplete);
	QCOMPARE_EQ(ofStop->StartOccTime, 600.0);
}

void TestSimulationRun::trainStartingAtStopWaitsForItsSections_data() {
	QTest::addColumn<int>("level");
	QTest::addColumn<double>("plannedDeparture");
	QTest::addColumn<double>("trainLength");
	// T1 stands at B when X is due there. At level 3 no authority of T1 is at the start of the section.
	QTest::newRow("level 3, a train at the stop") << 3 << 400.0 << 70.0;
	// T1 is in the section before that of B when X is due. The section of B is free, but the tail of X lies on the one before.
	QTest::newRow("level 0, a train under the tail") << 0 << 250.0 << 2500.0;
}

void TestSimulationRun::trainStartingAtStopWaitsForItsSections() {
	QFETCH(int, level);
	QFETCH(double, plannedDeparture);
	QFETCH(double, trainLength);
	SceneModel scene = lineWithStartAtB(m_line, level, plannedDeparture);
	for (SceneTrainUnit& unit : scene.trainUnits)
		unit.physical.length_m = trainLength;
	const std::unique_ptr<SimulationRun> run = steppedRun(scene, {{"T1", 1}, {"X", 1}}, 1500);
	if (run == nullptr || run->numTrains != 2)
		QFAIL("the run is not built");
	// X starts when T1 has left B, at 660 s, and not when it is due.
	QCOMPARE_EQ(trainOf(*run, "X-1").departure_time, plannedDeparture - 30.0);
	QCOMPARE_GT(trainOf(*run, "X-1").earliestActiveTrajectoryIndex, 660);
}

// A train that starts at Brelidy-Plouec, a halt inside a single-track stretch of Paimpol, is due while a train of the other
// direction runs towards it in that stretch. It starts when that train has passed the halt and reached Pontrieaux_Halte, its
// next stop, and it reaches the end of its route.
void TestSimulationRun::trainStartingAtStopWaitsForSingleTrack() {
	SceneModel scene = m_passengers;
	// The demand of Paimpol lengthens the dwell of the other train at Guingamp; this test is about the single track.
	scene.passengerDemand.clear();
	SceneService reverse = *std::find_if(scene.services.begin(), scene.services.end(),
		[](const SceneService& candidate) { return candidate.id == "Guin-Paim-ALL-STOPS-1"; });
	reverse.id = reverse.operatingCode = "REV";
	reverse.route = "route1";
	reverse.hasEntryTime = false;
	reverse.stops = {{"Brelidy-Plouec", "Brelidy-Plouec.platform.1", false, true, 0.0, 2600.0, 30.0},
		{"Guingamp", "Guingamp.platform.1", false, false, 0.0, 0.0, 0.0}};
	scene.services.push_back(reverse);
	const std::unique_ptr<SimulationRun> run = steppedRun(scene, {{"Guin-Paim-ALL-STOPS-1", 1}, {"REV", 1}}, 4600);
	if (run == nullptr || run->numTrains != 2)
		QFAIL("the run is not built");
	const Regional& train = trainOf(*run, "REV-1");
	const double otherAtNextStop = trainOf(*run, "Guin-Paim-ALL-STOPS-1-1").StationArrivals[4];
	QCOMPARE_EQ(train.departure_time, 2570.0);
	QCOMPARE_GT(otherAtNextStop, 2570.0);
	QCOMPARE_GT(static_cast<double>(train.earliestActiveTrajectoryIndex), otherAtNextStop);
	QVERIFY(train.OutOfSimulation);
}

// The eight trains of the Paimpol timetable cross on the single track at the loops of Tregonneau_Squiffiec, Pontrieaux and
// Lancerf. The first two express trains stand at Pontrieaux together, which the timetable plans from 1570 s to 1627 s. The
// passengers lengthen the dwells of the all-stops trains, so Paim-Guin-EXPRESS-2 reaches Tregonneau_Squiffiec while
// Guin-Paim-ALL-STOPS-1 still runs towards the loop there. The express waits on the main track clear of the points of the
// loop, and every train reaches the end of its route, the last ones after the 9000 s of the case.
void TestSimulationRun::paimpolTrainsCrossOnSingleTrack() {
	SceneModel scene = m_passengers;
	scene.settings.durationSeconds = 11000.0;
	SceneRunSelection firstRuns;
	for (const SceneService& service : scene.services)
		firstRuns.insert({service.id, 1});
	const std::unique_ptr<SimulationRun> run = steppedRun(scene, firstRuns, 11000);
	if (run == nullptr || run->numTrains != 8)
		QFAIL("the run is not built");
	int expressAtPontrieaux = 0;
	for (Regional& train : run->trains) {
		QVERIFY2(train.OutOfSimulation, train.trainDescription.c_str());
		const bool firstExpress = train.trainDescription == "Guin-Paim-EXPRESS-1-1" || train.trainDescription == "Paim-Guin-EXPRESS-2-1";
		// Pontrieaux lies at km 21.584.
		if (firstExpress && std::abs(train.trainXPosition(1600) - 21.584) < 0.3)
			++expressAtPontrieaux;
	}
	QCOMPARE(expressAtPontrieaux, 2);
}

// The single-track restrictions of Paimpol one block shorter at each end, as they were before they reached the switch sections
// at their ends. Such a switch section shares track with the main-track block next to it, so the zones still take that block in,
// and the eight trains reach the end of their routes. Before, Paim-Guin-EXPRESS-2 entered 32-B0 while Guin-Paim-ALL-STOPS-1
// still needed the switch section over it, and both stood there from about 2500 s to the end of the run.
void TestSimulationRun::paimpolNarrowRestrictionsCrossOnSingleTrack() {
	SceneModel scene = m_passengers;
	scene.settings.durationSeconds = 11000.0;
	auto shifted = [](const std::string& block, int by) {
		const std::size_t dash = block.find('-');
		return std::to_string(std::stoi(block.substr(0, dash)) + by) + block.substr(dash);
	};
	for (SceneSingleTrackRestriction& restriction : scene.singleTrackRestrictions) {
		restriction.startBlock = shifted(restriction.startBlock, 1);
		restriction.endBlock = shifted(restriction.endBlock, -1);
	}
	SceneRunSelection firstRuns;
	for (const SceneService& service : scene.services)
		firstRuns.insert({service.id, 1});
	const std::unique_ptr<SimulationRun> run = steppedRun(scene, firstRuns, 11000);
	if (run == nullptr || run->numTrains != 8)
		QFAIL("the run is not built");
	for (const Regional& train : run->trains)
		QVERIFY2(train.OutOfSimulation, train.trainDescription.c_str());
}

// The first two runs of the eight Paimpol services, with the passenger dwell coefficients beta2 and beta5 ten times their
// defaults, so that the all-stops trains dwell long. Trains of one direction wait at a loop for the next zone while more trains
// of that direction come up behind them, and the trains that are due when the line ahead of them is full wait to enter. All
// sixteen trains reach the end of their routes. Before, trains of both directions stood in the zones on either side of
// Pontrieaux, behind the trains that waited on its loop and main track, from about 15000 s to the end of the run.
void TestSimulationRun::paimpolLongDwellsTwiceCrossOnSingleTrack() {
	SceneModel scene = m_passengers;
	scene.settings.durationSeconds = 30000.0;
	scene.settings.passengerDwellCoefficients = std::vector<double>{7.0, 0.32, 18.23, 0.564, 4.838, 22.24, 0.04, 0.562};
	SceneRunSelection twoRuns;
	for (const SceneService& service : scene.services) {
		twoRuns.insert({service.id, 1});
		twoRuns.insert({service.id, 2});
	}
	const std::unique_ptr<SimulationRun> run = steppedRun(scene, twoRuns, 30000);
	if (run == nullptr || run->numTrains != 16)
		QFAIL("the run is not built");
	for (const Regional& train : run->trains)
		QVERIFY2(train.OutOfSimulation, train.trainDescription.c_str());
}

void TestSimulationRun::trainStartingAtStopBlocksPointsUnderIt_data() {
	QTest::addColumn<int>("level");
	QTest::newRow("level 3") << 3;
	QTest::newRow("level 4") << 4;
}

// A train of 400 m that starts at Pontrieaux on route0 of Paimpol stands over the points of the loop about 340 m behind the
// platform. At levels 3 and 4 their blocking times are complete and start in the step in which the train enters.
void TestSimulationRun::trainStartingAtStopBlocksPointsUnderIt() {
	QFETCH(int, level);
	SceneModel scene = m_passengers;
	for (SceneSignallingArea& area : scene.signallingAreas)
		area.level = level;
	SceneService starting = *std::find_if(scene.services.begin(), scene.services.end(),
		[](const SceneService& candidate) { return candidate.id == "Guin-Paim-ALL-STOPS-1"; });
	starting.id = starting.operatingCode = "START";
	starting.route = "route0";
	starting.hasEntryTime = false;
	starting.stops = {{"Pontrieaux", "Pontrieaux.platform.2", false, true, 0.0, 600.0, 60.0},
		{"Paimpol", "", false, false, 0.0, 0.0, 0.0}};
	scene.services.push_back(starting);
	for (SceneTrainUnit& unit : scene.trainUnits)
		unit.physical.length_m = 400.0;
	const std::unique_ptr<SimulationRun> run = steppedRun(scene, {{"START", 1}}, 2500);
	if (run == nullptr || run->numTrains != 1)
		QFAIL("the run is not built");
	Regional& train = run->trains[0];
	QCOMPARE_EQ(train.earliestActiveTrajectoryIndex, 540);
	train.ComputeBlockingTimesInMixedSignallingAreas(5, 3, 0.5, 50, 0, 0);
	const double head = train.Start_Node_X * 1000;
	int under = 0;
	for (int index = 0; index < train.N_BlockSections; ++index) {
		const BlockingTimes& time = train.BlockTime[index];
		if (time.PosStart <= head - train.train_length || time.PosStart > head)
			continue;
		++under;
		QVERIFY2(time.IsComplete, time.BlockID.c_str());
		QCOMPARE_EQ(time.StartOccTime, 540.0);
	}
	QCOMPARE_EQ(under, 2);
}

void TestSimulationRun::trainStartingAtStopBlocksSectionsUnderIt_data() {
	QTest::addColumn<int>("level");
	QTest::addColumn<double>("trainLength");
	QTest::addColumn<int>("sectionsUnder");
	QTest::newRow("level 0, tail one section behind") << 0 << 2500.0 << 1;
	QTest::newRow("level 2, tail one section behind") << 2 << 2500.0 << 1;
	QTest::newRow("level 0, tail in the section of the stop") << 0 << 1500.0 << 0;
}

// The line has sections of 2 km and B ends the section 3-B0. A train of 2500 m that starts at B has its tail in the section
// 2-B0 behind that of the stop, which it occupies from the step of entry until the tail has left it. A train of 1500 m
// has nothing behind the section of the stop. The sections that the train never touches stay incomplete.
void TestSimulationRun::trainStartingAtStopBlocksSectionsUnderIt() {
	QFETCH(int, level);
	QFETCH(double, trainLength);
	QFETCH(int, sectionsUnder);
	SceneModel scene = lineWithStartAtB(m_line, level, 400.0);
	for (SceneTrainUnit& unit : scene.trainUnits)
		unit.physical.length_m = trainLength;
	scene.settings.durationSeconds = 2500.0;
	const std::unique_ptr<SimulationRun> run = steppedRun(scene, {{"X", 1}}, 2500);
	if (run == nullptr || run->numTrains != 1)
		QFAIL("the run is not built");
	Regional& train = run->trains[0];
	QCOMPARE_EQ(train.earliestActiveTrajectoryIndex, 370);
	train.ComputeBlockingTimesInMixedSignallingAreas(5, 3, 0.5, 50, 0, 0);
	const double head = train.Start_Node_X * 1000;
	int under = 0;
	for (int index = 0; index < train.N_BlockSections; ++index) {
		const BlockingTimes& time = train.BlockTime[index];
		if (time.PosEnd > head || time.PosEnd <= head - train.train_length || time.PosStart > head)
			continue;
		if (time.BlockID == "@3-B0@")
			continue;
		++under;
		QVERIFY2(time.IsComplete, time.BlockID.c_str());
		QCOMPARE_EQ(time.StartOccTime, 370.0);
		QCOMPARE_EQ(time.StartRunTime, 370);
		QCOMPARE_EQ(time.EndRunTime, 370);
		QCOMPARE_GT(time.EndOccTime, time.EndClearTime);
		QCOMPARE_GT(time.EndClearTime, 370);
	}
	QCOMPARE_EQ(under, sectionsUnder);
	QVERIFY(train.N_BlockTimeComplete == 5 + sectionsUnder);
}

// The first step in which the two trains overlap, or -1 when they never do.
int firstOverlap(const Regional& one, const Regional& two, int steps) {
	for (int t = std::max({one.earliestActiveTrajectoryIndex, two.earliestActiveTrajectoryIndex, 0}); t < steps; ++t) {
		if (one.earliestActiveTrajectoryIndex < 0 || two.earliestActiveTrajectoryIndex < 0 || (one.OutOfSimulation && t > one.End_Time)
			|| (two.OutOfSimulation && t > two.End_Time))
			continue;
		const double a = one.instant_spatial_position[t], b = two.instant_spatial_position[t];
		if ((a >= b ? a - one.train_length - b : b - two.train_length - a) < 0)
			return t;
	}
	return -1;
}

void TestSimulationRun::entryWaitsForTrainThatCannotStop_data() {
	QTest::addColumn<int>("level");
	QTest::addColumn<QString>("start");
	QTest::addColumn<double>("due");
	QTest::addColumn<bool>("onTime");
	// T1 runs at 130 km/h and is 800 m before 6 km at 230 s and 440 m at 240 s. X starts at B (8 km) or enters at 6 km where its
	// route starts, so T1 has to stop before 6 km at the fixed-block levels and behind the tail of X at level 3.
	QTest::newRow("level 0, at B, T1 can stop") << 0 << "B" << 230.0 << true;
	QTest::newRow("level 0, at B, T1 cannot stop") << 0 << "B" << 240.0 << false;
	QTest::newRow("level 5, at B, T1 cannot stop") << 5 << "B" << 240.0 << false;
	QTest::newRow("level 3, at B, T1 stops behind X") << 3 << "B" << 240.0 << true;
	QTest::newRow("level 0, route at 6 km, T1 cannot stop") << 0 << "3-B0" << 240.0 << false;
	QTest::newRow("level 3, route at 6 km, T1 cannot stop") << 3 << "3-B0" << 240.0 << false;
	// T1 stands at B, at the end of the section before 8 km, from 331 s to 660 s, and leaves it in step 661. A train of a route
	// that starts at 8 km would stand on it. A train that starts at D, at 10 km, waits for it at level 0, because T1 can leave
	// B in the step in which that train would enter. At level 3 its tail is far enough ahead of B.
	QTest::newRow("level 0, route at 8 km, T1 under X") << 0 << "4-B0" << 400.0 << false;
	QTest::newRow("level 0, at D, T1 stands") << 0 << "D" << 400.0 << false;
	QTest::newRow("level 0, at D, T1 leaves B") << 0 << "D" << 661.0 << false;
	QTest::newRow("level 3, at D, T1 leaves B") << 3 << "D" << 661.0 << true;
}

// X, a copy of T1, starts at a stop (B or D) or enters where its route starts (at the start of a block). It waits while T1 is
// under it, or runs and is too close to stop in front of it, and enters on time otherwise. The two trains never overlap.
void TestSimulationRun::entryWaitsForTrainThatCannotStop() {
	QFETCH(int, level);
	QFETCH(QString, start);
	QFETCH(double, due);
	QFETCH(bool, onTime);
	SceneModel scene = lineWithStartAtB(m_line, level, due + 300.0);
	SceneService& entering = scene.services.back();
	entering.stops[0].dwellSeconds = 300.0;
	if (start == "D") {
		SceneStation station;
		station.id = station.name = "D";
		station.platforms = {{"D.platform.1", {"B0.node.6"}}};
		scene.stations.push_back(station);
		entering.stops[0].stationId = "D";
		entering.stops[0].platformId = "D.platform.1";
	} else if (start != "B") {
		SceneRoute midway;
		midway.id = "routeMidway";
		for (int block = start.left(1).toInt(); block < 8; ++block)
			midway.blocks.push_back(std::to_string(block) + "-B0");
		scene.routes.push_back(midway);
		entering.route = midway.id;
		entering.hasEntryTime = true;
		entering.entryTimeSeconds = due;
		entering.stops.erase(entering.stops.begin());
	}
	const int steps = 1500;
	const std::unique_ptr<SimulationRun> run = steppedRun(scene, {{"T1", 1}, {"X", 1}}, steps);
	if (run == nullptr || run->numTrains != 2)
		QFAIL("the run is not built");
	const Regional& train = trainOf(*run, "X-1");
	QCOMPARE_EQ(train.departure_time, due);
	if (onTime)
		QCOMPARE_EQ(train.earliestActiveTrajectoryIndex, static_cast<int>(due));
	else
		QCOMPARE_GT(train.earliestActiveTrajectoryIndex, static_cast<int>(due));
	QCOMPARE_EQ(firstOverlap(train, trainOf(*run, "T1-1"), steps), -1);
}

QTEST_GUILESS_MAIN(TestSimulationRun)
// A platform counts the passengers who wait on it and lists the first kGuiPlatformPassengerIcons of them. A passenger who
// has left the network there is no longer on the platform, at whatever time the run starts.
void TestSimulationRun::platformListsAtMostFiftyPassengers() {
	const auto make = [](int waiting, int exited, int exitedElsewhere, int start = 0) {
		auto run = std::make_unique<SimulationRun>();
		run->settings.startingSimulationTime = start;
		run->platforms.resize(2);
		for (int i = 0; i < 2; ++i) {
			StationPlatform& platform = *std::next(run->platforms.begin(), i);
			platform.StationID = "S";
			platform.ID = i == 0 ? "P0" : "P1";
		}
		const int total = waiting + exited + exitedElsewhere;
		for (int i = 0; i < total; ++i) {
			Passenger passenger;
			passenger.ID = "p" + std::to_string(i);
			if (i < waiting) {
				passenger.IsIntheNetwork = true;
				passenger.CurrentStatus = PassengerStatus::OnPlatform;
				passenger.Current_Train_To_Wait = "T" + std::to_string(i % 3);
				Journey journey;
				journey.ID = "J1";
				journey.Arr_Station_ID = i % 4 == 0 ? "North" : "South";
				passenger.Journeys.push_back(journey);
				passenger.current_JourneyID = journey.ID;
				run->platforms.front().Current_List_Pax_On_Platform.push_back({passenger.ID, 0.0});
			} else {
				passenger.TimeExitedTheNetwork = start + 100;
				passenger.StationExitedTheNetworkID = "S";
				passenger.PlatformExitedTheNetworkID = i < waiting + exited ? "P0" : "P1";
			}
			run->passengers.push_back(passenger);
		}
		run->numPassengers = total;
		run->passengerIndex.build(run->passengers);
		return run;
	};
	const auto ids = [](const GuiPlatformState& platform) {
		std::vector<std::string> result;
		for (const GuiPassengerState& passenger : platform.passengers) result.push_back(passenger.id);
		return result;
	};
	const auto range = [](int from, int to) {
		std::vector<std::string> result;
		for (int i = from; i < to; ++i) result.push_back("p" + std::to_string(i));
		return result;
	};

	// More waiting passengers than the list holds: the first 50 in order, all counted.
	auto crowded = make(60, 3, 2);
	GuiSimulationSnapshot snapshot = buildGuiSimulationSnapshot(*crowded, 102);
	QCOMPARE(snapshot.platforms.size(), std::size_t(2));
	QCOMPARE(snapshot.platforms[0].passengerCount, 60);
	QCOMPARE(ids(snapshot.platforms[0]), range(0, 50));
	QCOMPARE(snapshot.platforms[0].passengers[7].status, std::string("OnPlatform"));
	QCOMPARE(snapshot.platforms[0].passengers[7].nextTrain, std::string("T1"));
	// The destinations count all 60 who wait, not only the 50 listed: 15 go north, 45 south.
	QCOMPARE(snapshot.platforms[0].destinations.size(), std::size_t(2));
	QCOMPARE(snapshot.platforms[0].destinations[0].destination, std::string("South"));
	QCOMPARE(snapshot.platforms[0].destinations[0].count, 45);
	QCOMPARE(snapshot.platforms[0].destinations[1].destination, std::string("North"));
	QCOMPARE(snapshot.platforms[0].destinations[1].count, 15);
	QVERIFY(snapshot.platforms[1].destinations.empty());
	// Nobody waits on the other platform: the two who left the network there two seconds ago are gone.
	QCOMPARE(snapshot.platforms[1].passengerCount, 0);
	QVERIFY(snapshot.platforms[1].passengers.empty());

	// Fewer than the list holds: all of them, without the five who left there.
	auto fewer = make(48, 5, 0);
	snapshot = buildGuiSimulationSnapshot(*fewer, 100);
	QCOMPARE(snapshot.platforms[0].passengerCount, 48);
	QCOMPARE(ids(snapshot.platforms[0]), range(0, 48));

	// The same in a run that starts at 07:00, at the step in which they left and at the step with the number of that time.
	constexpr int kStart = 7 * 3600;
	auto morning = make(48, 5, 0, kStart);
	for (const int step : {100, 102, kStart + 100, kStart + 102}) {
		snapshot = buildGuiSimulationSnapshot(*morning, step);
		QCOMPARE(snapshot.platforms[0].passengerCount, 48);
		QCOMPARE(ids(snapshot.platforms[0]), range(0, 48));
	}
}

// Without the passenger GUI the run keeps no platform lists. The count of a platform then comes from the passenger flow,
// which counts the waiting passengers in every mode, as a headless run with the live stream needs.
void TestSimulationRun::platformCountsWaitingWithoutPassengerGui() {
	auto run = std::make_unique<SimulationRun>();
	run->platforms.resize(1);
	run->platforms.front().StationID = "S";
	run->platforms.front().ID = "P0";
	for (int i = 0; i < 7; ++i) {
		Passenger passenger;
		passenger.ID = "p" + std::to_string(i);
		passenger.IsIntheNetwork = true;
		passenger.CurrentStatus = PassengerStatus::OnPlatform;
		passenger.Current_WaitingStationID = "S";
		passenger.Current_WaitingStationPlatformID = "P0";
		passenger.Current_Train_To_Wait = "T0";
		run->passengers.push_back(passenger);
	}
	run->numPassengers = 7;
	run->passengerIndex.build(run->passengers);
	run->passengerFlow.build(run->passengers);
	QVERIFY(!run->settings.PAX_GUI);
	QVERIFY(run->platforms.front().Current_List_Pax_On_Platform.empty());
	const GuiSimulationSnapshot snapshot = buildGuiSimulationSnapshot(*run, 10);
	QCOMPARE(snapshot.platforms.size(), std::size_t(1));
	QCOMPARE(snapshot.platforms[0].passengerCount, 7);
}

#include "test_simulationrun.moc"
