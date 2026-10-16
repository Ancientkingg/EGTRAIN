// Tests of the run state of simulation/SimulationRun.h.
//
// reset() has to return every member to the state of a new run, and the global names of the members have to refer
// to them. Runs in this process then show that nothing of one run reaches the next. A run that was stopped part way is
// followed by another scene, a failed preparation by a good one, and a scene with passengers runs twice. Every run after
// such a gap has to write the files that the first run of its scene wrote.
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

Logger owl;

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
// names every member, so this file stops compiling when a member is added to the run.
using Description = std::vector<std::pair<std::string, std::string>>;

Description describe(const SimulationRun& run) {
	const auto& [occupied, connected, authorities, incidents, vcTimesteps, vcTrains, vcTexts, platforms, numPlatforms, passengers, numPassengers,
		passengerIndex, platformIndex, totalDelays, entranceDelays, disturbances, finalStation, trains, numTrains, generator, delay, egtrainTime, romaTime] = run;
	const auto station = [](const Stations& s) {
		return s.stationName + "/" + std::to_string(s.N_Stopped_Trains) + "/" + std::to_string(s.totalArrivalDelay);
	};
	NumberGenerator draws = generator;
	const std::string draw = std::to_string(draws.getUniformFloat()) + "/" + std::to_string(draws.getGaussianFloat());
	return {{"blocksOccupied", std::to_string(occupied.size())},
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

// True when the run is as a new run in every member that the builders do not fill. A prepared scene has its platforms,
// passengers, incidents and seeded generator, and the name of the final station.
bool carriesNothingOver(const SimulationRun& run) {
	const std::set<std::string> builderState = {"incidents", "platforms", "numPlatforms", "passengers", "numPassengers",
		"passengerIndex", "platformIndex", "finalStation", "trains", "numTrains", "numberGenerator"};
	const std::vector<std::string> unchanged = membersOfNewRun(run);
	for (const auto& [name, text] : describe(run))
		if (builderState.count(name) == 0 && std::find(unchanged.begin(), unchanged.end(), name) == unchanged.end())
			return false;
	return true;
}

// Gives every member of the run a value that a new run does not have.
void fill(SimulationRun& run) {
	auto& [occupied, connected, authorities, incidents, vcTimesteps, vcTrains, vcTexts, platforms, numPlatforms, passengers, numPassengers,
		passengerIndex, platformIndex, totalDelays, entranceDelays, disturbances, finalStation, trains, numTrains, generator, delay, egtrainTime, romaTime] = run;
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
			&& &regional_train == &run.trains && &numRegions == &run.numTrains && &runNumberGenerator() == &run.numberGenerator
			&& &S_delay == &run.signallingDelay && &Comp_Time_EGTRAIN == &run.compTimeEgtrain && &Comp_Time_ROMA == &run.compTimeRoma,
		"the global names refer to the members of the run");
	ok &= expect(BlocksOccupied.size() == 1 && ETCS_MA.size() == 1 && simulationIncidents.size() == 1 && VCmsgText.size() == 1
			&& AllDailyPassengers.size() == 1 && numAllStationPlatforms == 1 && Final_Station.N_Stopped_Trains == 4 && numRegions == 2
			&& regional_train.size() == 2 && S_delay == 2,
		"the members show the values through the global names");

	run.reset();
	ok &= expect(isNewRun(run), "reset returns every member to the state of a new run");
	ok &= expect(&defaultRun() == address && BlocksOccupied.empty() && ETCS_MA.empty() && AllDailyPassengers.empty() && regional_train.empty() && numRegions == 0
			&& S_delay == 0,
		"reset keeps the run at its address, so the global names stay valid");

	// The generator of a seeded run starts again from the default seed.
	seedRunNumberGenerator(5);
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
	InputMainFolder.clear();
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
	return ok ? 0 : 1;
}
