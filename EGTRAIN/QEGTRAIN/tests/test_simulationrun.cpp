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
		exceedPlan, limits, held, reservations, zones, zoneLimits, zoneRoutes, routeZone, zonesDerived, boundaries, routes, routeCount] = infrastructure;
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
		{"infra.stationBoundaries", std::to_string(boundaries.size())},
		{"infra.routes", std::to_string(routes.size())},
		{"infra.numRoutes", std::to_string(routeCount)}};
}

Description describe(const SimulationRun& run) {
	const auto& [settings, occupied, connected, authorities, incidents, vcTimesteps, vcTrains, vcTexts, platforms, numPlatforms, passengers,
		numPassengers, passengerIndex, platformIndex, totalDelays, entranceDelays, disturbances, finalStation, trains, numTrains, generator, delay,
		egtrainTime, romaTime, infrastructure] = run;
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
		{"vcMsgTimestep", std::to_string(vcTimesteps.size())},
		{"vcMsgTrain", std::to_string(vcTrains.size())},
		{"vcMsgText", std::to_string(vcTexts.size())},
		{"platforms", std::to_string(platforms.size())},
		{"numPlatforms", std::to_string(numPlatforms)},
		{"passengers", std::to_string(passengers.size())},
		{"numPassengers", std::to_string(numPassengers)},
		{"passengerIndex", std::to_string(passengerIndex.covers(passengers))},
		{"platformIndex", std::to_string(platformIndex.covers(platforms))},
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
// platforms, passengers, incidents and seeded generator.
bool carriesNothingOver(const SimulationRun& run) {
	const std::set<std::string> builderState = {"settings", "incidents", "platforms", "numPlatforms", "passengers", "numPassengers",
		"passengerIndex", "platformIndex", "trains", "numTrains", "numberGenerator"};
	const std::vector<std::string> unchanged = membersOfNewRun(run);
	for (const auto& [name, text] : describe(run))
		if (!isInfrastructureMember(name) && builderState.count(name) == 0
			&& std::find(unchanged.begin(), unchanged.end(), name) == unchanged.end())
			return false;
	return true;
}

// Gives every member of the run a value that a new run does not have.
void fill(SimulationRun& run) {
	auto& [settings, occupied, connected, authorities, incidents, vcTimesteps, vcTrains, vcTexts, platforms, numPlatforms, passengers, numPassengers,
		passengerIndex, platformIndex, totalDelays, entranceDelays, disturbances, finalStation, trains, numTrains, generator, delay, egtrainTime, romaTime,
		infrastructure] = run;
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
	vcTimesteps.push_back(7);
	vcTrains.push_back("T1-1");
	vcTexts.push_back("Train is now decoupled");
	platforms.emplace_back();
	numPlatforms = 1;
	passengers.emplace_back();
	numPassengers = 1;
	passengerIndex.build(passengers);
	platformIndex.build(platforms);
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
	infrastructure.stationBoundaries.emplace_back(&infrastructure.sections[0], false, nullptr);
	infrastructure.routes.resize(1);
	infrastructure.numRoutes = 1;
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
	Files files;
};

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
		if (const auto snapshot = simulation.takeSimulationSnapshot())
			outcome.lastSnapshot = snapshot;
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
		&& a.platforms.size() == b.platforms.size() && a.passengers.size() == b.passengers.size();
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
	void secondRunFollowsRunOfProcess();
	void stepsRunWithoutController();

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
	// The arrays of the network have their full number of elements.
	QCOMPARE(infra.blockSets.size(), static_cast<std::size_t>(kMaxTrackLines));
	QCOMPARE(infra.connections.size(), static_cast<std::size_t>(kMaxConnections));
	QCOMPARE(infra.stations.size(), static_cast<std::size_t>(kMaxStations));
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

	// The elements of the arrays of the network stay where they are when the infrastructure is reset.
	const BlockSet* const firstTrack = &infra.blockSets[0];
	const Connections* const firstConnection = &infra.connections[0];
	const Stations* const firstStation = &infra.stations[0];

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
	// The arrays of the network keep their address when the infrastructure is reset.
	QVERIFY(firstTrack == &infra.blockSets[0]);
	QVERIFY(firstConnection == &infra.connections[0]);
	QVERIFY(firstStation == &infra.stations[0]);

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
	for (int t = 0; t < other->settings.times; ++t)
		runSimulationStep(*other, t, order);

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
		QVERIFY2(actual.Eq == expected.Eq, (label + ": branches of the steps").c_str());
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
	// write the same files. Without detailed files they look for a stop four times.
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
	QVERIFY(written.count("TrainTrajectories/BlockingTimes.txt") == 1);
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

QTEST_GUILESS_MAIN(TestSimulationRun)
#include "test_simulationrun.moc"
