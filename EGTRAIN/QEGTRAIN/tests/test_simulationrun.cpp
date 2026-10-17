// Tests of the run state of simulation/SimulationRun.h.
//
// reset() and resetInfrastructure() have to return every member to the state of a new run, and the global names of the
// members have to refer to them. Runs in this process then show that nothing of one run reaches the next. A run that was
// stopped part way is followed by another scene, a failed preparation by a good one, and a scene with passengers runs twice.
// Every run after such a gap has to write the files that the first run of its scene wrote. prepareScene copies the settings
// of the application into the run, and the run goes on with the copy.
//
//   test_simulationrun LINE_SCENE_DIR PASSENGER_SCENE_DIR

#include "app/DispatchController.h"
#include "scene/SceneModel.h"
#include "simulation/InitialParameters.h"
#include "simulation/Infrastructure.h"
#include "simulation/Passengers.h"
#include "simulation/RollingStock.h"
#include "simulation/Signalling.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationRun.h"
#include "simulation/builders/InfrastructureBuilderInternal.h"
#ifdef signals
#undef signals
#endif

#include <QCoreApplication>
#include <QObject>
#include <QTemporaryDir>

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

bool expect(bool condition, const std::string& message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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
	const auto station = [](const Stations& s) {
		return s.stationName + "/" + std::to_string(s.N_Stopped_Trains) + "/" + std::to_string(s.totalArrivalDelay);
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
		{"totalInputDelays", station(totalDelays)},
		{"entranceInputDelays", station(entranceDelays)},
		{"disturbanceInput", station(disturbances)},
		{"finalStation", station(finalStation)},
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
// platforms, passengers, incidents and seeded generator, and the name of the final station.
bool carriesNothingOver(const SimulationRun& run) {
	const std::set<std::string> builderState = {"settings", "incidents", "platforms", "numPlatforms", "passengers", "numPassengers",
		"passengerIndex", "platformIndex", "finalStation", "trains", "numTrains", "numberGenerator"};
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
	for (Stations* station : {&totalDelays, &entranceDelays, &disturbances, &finalStation}) {
		station->stationName = "Final_Station";
		station->N_Stopped_Trains = 4;
		station->totalArrivalDelay = 30;
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

bool resetTests() {
	bool ok = true;
	SimulationRun& run = defaultRun();
	SimulationRun* const address = &run;
	ok &= expect(isNewRun(run), "the run of the process starts as a new run");

	fill(run);
	ok &= expect(membersOfNewRun(run).empty(), "the test fills every member");
	ok &= expect(&BlocksOccupied == &run.blocksOccupied && &BlocksConnected == &run.blocksConnected && &ETCS_MA == &run.etcsMa
			&& &simulationIncidents == &run.incidents && &VCmsgTimestep == &run.vcMsgTimestep && &VCmsgTrain == &run.vcMsgTrain
			&& &VCmsgText == &run.vcMsgText && &AllStationPlatforms == &run.platforms && &numAllStationPlatforms == &run.numPlatforms
			&& &AllDailyPassengers == &run.passengers && &numAllDailyPassengers == &run.numPassengers
			&& &AllDailyPassengerIndex == &run.passengerIndex && &AllStationPlatformIndex == &run.platformIndex
			&& &TotalInputDelays == &run.totalInputDelays && &EntranceInputDelays == &run.entranceInputDelays
			&& &DisturbanceInput == &run.disturbanceInput && &Final_Station == &run.finalStation
			&& &regional_train == &run.trains && &numRegions == &run.numTrains
			&& &S_delay == &run.signallingDelay && &Comp_Time_EGTRAIN == &run.compTimeEgtrain && &Comp_Time_ROMA == &run.compTimeRoma,
		"the global names refer to the members of the run");
	RunInfrastructure& infra = run.infra;
	ok &= expect(&numTrackLines == &infra.numTrackLines && &blockSets == &infra.blockSets && &numConnections == &infra.numConnections
			&& &connections == &infra.connections && &numStations == &infra.numStations && &StationArray == &infra.stations
			&& &Blocks == &infra.numBlocks && &signalling_block_sections == &infra.sections && &list_of_TDS == &infra.detectionSections
			&& &InfraElementsList == &infra.infraEvents
			&& &singleTrackLimits == &infra.singleTrackLimits && &singleTrackHeld == &infra.singleTrackHeld
			&& &singleTrackReservations == &infra.singleTrackReservations && &stationBoundarySections == &infra.stationBoundaries
			&& &N_Routes == &infra.numRoutes && &train_route == &infra.routes,
		"the global names refer to the members of the infrastructure");
	ok &= expect(blockSets.size() == static_cast<std::size_t>(kMaxTrackLines) && connections.size() == static_cast<std::size_t>(kMaxConnections)
			&& StationArray.size() == static_cast<std::size_t>(kMaxStations),
		"the arrays of the network have their full number of elements");
	ok &= expect(blockSets[0].sceneTrackId == "line" && connections[0].ID == "switch" && StationArray[0].stationName == "Station"
			&& Blocks == 2 && signalling_block_sections.size() == 2 && list_of_TDS.size() == 1 && train_route.size() == 1 && N_Routes == 1,
		"the infrastructure shows the values through the global names");
	ok &= expect(BlocksOccupied.size() == 1 && ETCS_MA.size() == 1 && simulationIncidents.size() == 1 && VCmsgText.size() == 1
			&& AllDailyPassengers.size() == 1 && numAllStationPlatforms == 1 && Final_Station.N_Stopped_Trains == 4 && numRegions == 2
			&& regional_train.size() == 2 && S_delay == 2,
		"the members show the values through the global names");

	// The elements of the arrays of the network stay where they are when the infrastructure is reset.
	const BlockSet* const firstTrack = &blockSets[0];
	const Connections* const firstConnection = &connections[0];
	const Stations* const firstStation = &StationArray[0];

	run.reset();
	ok &= expect(infrastructureIs(run, false) && Blocks == 2 && train_route.size() == 1,
		"reset leaves the infrastructure in place, because the scene builder replaces it");

	run.resetInfrastructure();
	ok &= expect(isNewRun(run), "reset and resetInfrastructure return every member to the state of a new run");
	ok &= expect(&defaultRun() == address && BlocksOccupied.empty() && ETCS_MA.empty() && AllDailyPassengers.empty() && regional_train.empty() && numRegions == 0
			&& S_delay == 0 && Blocks == 0 && train_route.empty() && N_Routes == 0 && list_of_TDS.empty(),
		"a reset keeps the run at its address, so the global names stay valid");
	ok &= expect(firstTrack == &blockSets[0] && firstConnection == &connections[0] && firstStation == &StationArray[0],
		"the arrays of the network keep their address when the infrastructure is reset");

	// The generator of a seeded run starts again from the default seed.
	run.numberGenerator = NumberGenerator(5);
	run.reset();
	ok &= expect(isNewRun(run), "reset returns the generator to the default seed");
	return ok;
}

// The files a run wrote, by path below its output folder, with their content.
using Files = std::map<std::string, std::string>;

struct Outcome {
	bool prepared = false;
	// Nothing of an earlier run is left when the run starts.
	bool startedClean = false;
	bool completed = false;
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
};

// Prepares the scene and runs it, as a run without a window does.
Outcome runScene(const RunSetup& setup) {
	Outcome outcome;
	QTemporaryDir output;
	initial_variables.GUI = 0;
	initial_variables.TSM = 0;
	initial_variables.RChoice = 0;
	initial_variables.PAX_GUI = setup.passengerGui;
	initial_variables.durationOverride = true;
	initial_variables.times = setup.horizonSeconds;
	initial_variables.exportDetailedTrajectories = setup.detailedFiles;
	initial_variables.OutputMainFolder = output.path().toStdString();
	initial_variables.InputMainFolder.clear();

	CoutSilencer silencer;
	outcome.prepared = !hasErrors(simulation.prepareScene(*setup.scene, "baseline", setup.selection)) && numRegions > 0;
	if (!outcome.prepared)
		return outcome;
	outcome.startedClean = carriesNothingOver(defaultRun());
	int snapshots = 0;
	QObject context;
	QObject::connect(&simulation, &DispatchController::snapshotAvailable, &context, [&] {
		// The mailbox only emits again once the snapshot has been taken.
		simulation.takeSimulationSnapshot();
		if (++snapshots == setup.stopAtSnapshot)
			simulation.runControl()->requestStop(); }, Qt::DirectConnection);
	simulation.runSimulation();
	outcome.completed = simulation.lastRunCompleted();
	outcome.files = readFiles(output.path().toStdString());
	return outcome;
}

bool sameFiles(const std::string& label, const Outcome& expected, const Outcome& actual) {
	bool ok = expect(actual.prepared && actual.completed, label + ": the run completes");
	ok &= expect(actual.startedClean, label + ": nothing of an earlier run is left when the run starts");
	ok &= expect(!expected.files.empty() && actual.files.size() == expected.files.size(), label + ": the run writes the same files");
	for (const auto& [name, content] : expected.files) {
		const auto found = actual.files.find(name);
		if (found == actual.files.end())
			ok &= expect(false, label + ": " + name + " is missing");
		else if (found->second != content)
			ok &= expect(false, label + ": " + name + " differs");
	}
	return ok;
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

bool gapTests(const SceneModel& line, const SceneModel& passengerScene) {
	bool ok = true;
	const SceneModel movingBlock = movingBlockLine(line);
	RunSetup lineRun;
	lineRun.scene = &movingBlock;
	lineRun.selection = {{"T1", 1}};
	lineRun.horizonSeconds = 1500;
	lineRun.detailedFiles = true;
	RunSetup passengerRun;
	passengerRun.scene = &passengerScene;
	passengerRun.horizonSeconds = 2400;
	passengerRun.passengerGui = true;

	// The first runs of the process: the files that later runs have to write again.
	const Outcome firstLine = runScene(lineRun);
	ok &= expect(firstLine.prepared && firstLine.completed && !firstLine.files.empty(), "the line scene runs");
	const Outcome firstPassengers = runScene(passengerRun);
	ok &= expect(firstPassengers.prepared && firstPassengers.completed && !AllDailyPassengers.empty(), "the passenger scene runs");

	ok &= sameFiles("passengers, second run", firstPassengers, runScene(passengerRun));

	RunSetup stoppedRun = lineRun;
	stoppedRun.stopAtSnapshot = 300;
	const Outcome stopped = runScene(stoppedRun);
	ok &= expect(stopped.prepared && !stopped.completed, "the line run stops part way");
	ok &= expect(!ETCS_MA.empty(), "the stopped run leaves movement authorities behind");
	ok &= sameFiles("a scene after a stopped run", firstPassengers, runScene(passengerRun));

	// A preparation that fails after the run state was used. The state has to be gone when the next scene prepares.
	fill(defaultRun());
	const std::vector<SceneDiagnostic> rejected = simulation.prepareScene(line, "no.such.scenario");
	ok &= expect(hasErrors(rejected), "a scene with an unknown scenario is rejected");
	ok &= expect(isNewRun(defaultRun()), "a rejected preparation leaves a new run");
	ok &= sameFiles("a scene after a failed preparation", firstLine, runScene(lineRun));
	return ok;
}

// prepareScene copies the settings of the application into the run, and the controller goes on with the copy when the application
// changes its settings. The buffer time, the recovery time and the output folder are the ones that the files of the run show.
bool settingsTests(const SceneModel& line) {
	bool ok = true;
	QTemporaryDir prepared;
	QTemporaryDir changed;
	initial_variables.GUI = 0;
	initial_variables.TSM = 0;
	initial_variables.RChoice = 0;
	initial_variables.PAX_GUI = false;
	initial_variables.durationOverride = true;
	initial_variables.times = 600;
	initial_variables.exportDetailedTrajectories = false;
	initial_variables.bufferTimeOverride = true;
	initial_variables.bufferTime = 2.5;
	initial_variables.recoveryTimeOverride = true;
	initial_variables.recoveryTimePercentage = 3.5;
	initial_variables.OutputMainFolder = prepared.path().toStdString();

	CoutSilencer silencer;
	ok &= expect(!hasErrors(simulation.prepareScene(line, "baseline", {{"T1", 1}})) && numRegions > 0, "the scene is prepared");
	const InitialParameters& settings = defaultRun().settings;
	ok &= expect(settings.times == 600 && settings.bufferTime == 2.5 && settings.recoveryTimePercentage == 3.5
			&& settings.OutputMainFolder == prepared.path().toStdString() && settings.name == line.name,
		"prepareScene copies the settings, with the buffer time, the recovery time and the output folder");

	initial_variables.bufferTime = 9;
	initial_variables.recoveryTimePercentage = 9;
	initial_variables.OutputMainFolder = changed.path().toStdString();
	ok &= expect(settings.bufferTime == 2.5 && settings.recoveryTimePercentage == 3.5 && settings.OutputMainFolder == prepared.path().toStdString(),
		"a change to the settings of the application does not reach the copy");
	simulation.runSimulation();
	const std::filesystem::path trajectories = std::filesystem::path(prepared.path().toStdString()) / "TrainTrajectories";
	ok &= expect(simulation.lastRunCompleted() && std::filesystem::exists(trajectories / "Stats_Stations.txt")
			&& !std::filesystem::exists(std::filesystem::path(changed.path().toStdString()) / "TrainTrajectories"),
		"the run writes to the output folder of the copy");

	simulation.resetState();
	ok &= expect(isNewRun(defaultRun()), "a reset returns the settings of the run to those of a new run");
	initial_variables.bufferTimeOverride = false;
	initial_variables.recoveryTimeOverride = false;
	return ok;
}

// The steps of a run that DispatchController runs for the trains and the signalling, on the run that is given.
void stepRun(SimulationRun& run, int t) {
	for (std::size_t index = 0; index < run.trains.size(); ++index) {
		Regional& train = run.trains[index];
		train.trajectoryComputationIncludingMovingBlock(t, signalCode1, signalCode2, signalCode3);
		train.recordEarliestActiveTrajectoryIndex(t);
		train.recordStationPassagesAtTime(t);
		train.checkTrainArrDep(static_cast<int>(index), t);
	}
	for (Regional& train : run.trains)
		Simulate_Train_Passenger_Interactions(run, t, run.settings.startingSimulationTime, train, run.passengers, run.platforms);
	run.etcsMa.clear();
	Occupy_Block_Sections_Of_Route(run, t);
	Apply_Signal_Failures_Mixed_Signalling(run, t);
	Apply_Single_Track_Authorities_Mixed_Signalling(run);
	ReportAllTrainPositionsToRBC(run, t, 50);
	protectStationAreas(run, t);
	releaseMixedSignallingSystem(run);
	activateMixedSignallingSystem(run);
	unlockDoubleSwitches(run);
	run.blocksOccupied.clear();
	run.blocksConnected.clear();
}

// The builders and the routines of a step work on the run that they are given. A second run, built from the scene and stepped while
// the run of the process is a new run, follows the trajectory that the controller gives the same scene in the run of the process, and
// leaves the run of the process as it was.
bool explicitRunTests(const SceneModel& line) {
	bool ok = true;
	const SceneModel movingBlock = movingBlockLine(line);
	RunSetup setup;
	setup.scene = &movingBlock;
	setup.selection = {{"T1", 1}};
	setup.horizonSeconds = 600;
	const Outcome reference = runScene(setup);
	ok &= expect(reference.prepared && reference.completed && !defaultRun().trains.empty(), "the controller runs the scene in the run of the process");
	if (!reference.completed || defaultRun().trains.empty())
		return false;
	const std::vector<double> positions = defaultRun().trains[0].instant_spatial_position;
	const std::vector<double> speeds = defaultRun().trains[0].instant_train_speed;
	const std::string description = defaultRun().trains[0].trainDescription;

	CoutSilencer silencer;
	simulation.resetState();
	ok &= expect(isNewRun(defaultRun()), "the run of the process is a new run before the second run is built");
	const auto other = std::make_unique<SimulationRun>();
	ok &= expect(!hasErrors(buildInfrastructureAndSignallingFromScene(*other, movingBlock)), "the infrastructure is built into a run of its own");
	ok &= expect(!hasErrors(buildOperationsFromScene(*other, movingBlock, "baseline", setup.selection)), "the operations are built into a run of its own");
	ok &= expect(other->numTrains == 1 && other->infra.numBlocks > 0 && !other->infra.routes.empty(), "the second run holds the trains and the infrastructure");
	ok &= expect(!other->trains.empty() && other->trains[0].run_ == other.get(), "a train of the second run belongs to it");
	other->settings = initial_variables;
	for (Regional& train : other->trains)
		train.departure_time = train.scheduled_departure_time;
	for (int t = 0; t < setup.horizonSeconds; ++t)
		stepRun(*other, t);
	ok &= expect(other->trains[0].trainDescription == description && other->trains[0].instant_spatial_position == positions
			&& other->trains[0].instant_train_speed == speeds,
		"the second run follows the trajectory of the same scene in the run of the process");
	ok &= expect(isNewRun(defaultRun()), "building and stepping another run leaves the run of the process as it was");
	return ok;
}

} // namespace

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	if (argc < 3) {
		std::cerr << "usage: test_simulationrun LINE_SCENE_DIR PASSENGER_SCENE_DIR\n";
		return 2;
	}
	bool ok = resetTests();

	SceneLoadResult line = loadScene(argv[1]);
	SceneLoadResult passengers = loadScene(argv[2]);
	if (!expect(!hasErrors(line.diagnostics) && !hasErrors(passengers.diagnostics), "the scenes load"))
		return 1;
	ok &= gapTests(line.scene, passengers.scene);
	ok &= settingsTests(line.scene);
	ok &= explicitRunTests(line.scene);
	return ok ? 0 : 1;
}
