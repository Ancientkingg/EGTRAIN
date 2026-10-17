#include <QElapsedTimer>
#include "app/DispatchController.h"
#include "io/ExternalState.h"
#include "io/LiveFrame.h"
#include "io/LiveServer.h"
#include "io/RttpXml.h"
#include "scene/SceneValidator.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationRun.h"
#include "simulation/SimulationWorker.h"
#include "util/Log.h"
#include "util/portability.h" // localtime_r shim on MSVC
#include "util/PlaybackProfiler.h"
#include <algorithm>
#include <numeric>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <chrono>
#include <memory>
#include <map>
#include <optional>
#include <random>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace std;

namespace {
// The step loop reports its progress once in this many steps.
constexpr int ProgressEverySteps = 100;

void ensureDirectory(const string& path) {
	if (!path.empty()) {
		std::filesystem::create_directories(path);
	}
}

// Remove an existing file without reporting a failure.
void removeRunOutput(const string& path) {
	std::error_code error;
	std::filesystem::remove(path, error);
}

// The step range of a signal failure in the simulation (see
// Apply_Signal_Failures_Mixed_Signalling): from the start, and to the end when
// the incident has one.
bool signalFailureActive(const SimulationIncident& incident, int timestep) {
	if (incident.type != "signal_failure" || timestep < incident.startSeconds)
		return false;
	const bool hasEnd = incident.hasEndSeconds || incident.endSeconds != 0.0;
	return !hasEnd || timestep <= incident.endSeconds;
}

// What names a train in a snapshot and does not change during a run.
GuiTrainState trainIdentity(int index, const Train& train) {
	GuiTrainState state;
	state.index = index;
	state.id = train.ID;
	state.type = train.type;
	state.description = train.trainDescription;
	state.operatingCode = train.operatingCode;
	state.serviceId = train.serviceId;
	state.occurrence = train.serviceOccurrence;
	state.routeIndex = train.indexOfRoute;
	return state;
}
} // namespace

GuiSimulationSnapshot buildGuiSimulationSnapshot(const SimulationRun& run, int timestep) {
	GuiSimulationSnapshot snapshot;
	snapshot.timestep = timestep;
	snapshot.totalTimesteps = run.settings.times;

	snapshot.trains.reserve(static_cast<std::size_t>(run.numTrains));
	for (int i = 0; i < run.numTrains; ++i) {
		const Train& train = run.trains[i];
		GuiTrainState state = trainIdentity(i, train);
		const bool validRoute = train.indexOfRoute >= 0 && train.indexOfRoute < static_cast<int>(run.infra.routes.size());
		state.reversedDirection = validRoute ? run.infra.routes[train.indexOfRoute].reversed_direction : false;
		state.wagonCount = static_cast<int>(train.number_of_wagons);
		state.length = train.train_length;
		state.departureTime = static_cast<int>(train.departure_time);
		state.outOfSimulation = train.OutOfSimulation;
		state.speedKmh = train.speedKmhAt(timestep);
		state.currentOnboardPassengers = train.Current_OnBoard_Passengers;
		state.maxOnboardPassengers = train.MAX_OnBoard_Passengers;

		if (validRoute && timestep >= 0 && timestep < static_cast<int>(train.instant_spatial_position.size())) {
			state.routeAxisPosition = train.instant_spatial_position[static_cast<std::size_t>(timestep)];
			state.wagonHeadPositions.reserve(static_cast<std::size_t>(state.wagonCount + 1));
			state.wagonTailPositions.reserve(static_cast<std::size_t>(state.wagonCount + 1));
			for (int wagon = 0; wagon <= state.wagonCount; ++wagon) {
				state.wagonHeadPositions.push_back(trainXPositionOf(train, timestep, wagon));
				state.wagonTailPositions.push_back(trainXPositionOf(train, timestep, wagon + 1));
			}
		}

		if (guiTrainPublishesOccupiedArcs(state)) {
			const int arcCount = std::min(train.Bs.total_arcs,
				static_cast<int>(sizeof(train.Bs.arcs_in_signalling_block_section)
					/ sizeof(train.Bs.arcs_in_signalling_block_section[0])));
			for (int arc = 0; arc < arcCount; ++arc) {
				state.occupiedArcs.push_back({train.Bs.trackLineId,
					train.Bs.arcs_in_signalling_block_section[arc].startNode.X});
			}
		}
		snapshot.trains.push_back(std::move(state));
	}

	// One entry per section ID and direction: a section that several routes
	// share has a copy, and a code, in each of them.
	GuiSignalStateList signalStates;
	const auto appendSignal = [&signalStates](std::string_view id, const Section& section, bool reversed) {
		if (!id.empty())
			signalStates.merge(id, reversed, static_cast<int>(section.code), section.SignallingLevel);
	};
	std::map<std::string, GuiSectionState> sectionStates;
	const int routeCount = std::min(run.infra.numRoutes, static_cast<int>(run.infra.routes.size()));
	for (int routeIndex = 0; routeIndex < routeCount; ++routeIndex) {
		const Route& route = run.infra.routes[routeIndex];
		for (int sectionIndex = 0; sectionIndex < route.N_Block_Sections; ++sectionIndex) {
			const Section& section = route.sequence_of_block_sections[sectionIndex];
			const std::string& id = section.ID;
			if (!id.empty()) {
				auto& state = sectionStates[id];
				state.sectionId = id;
				// Route copies intentionally merge by section ID. Any nonzero
				// signalling code, including an approach aspect, reports the
				// permissive-signalling cue. It is not a reservation or a
				// guarantee for the selected route direction.
				state.prepared = state.prepared || guiSectionReportsPermissiveSignalling(section.code);
			}
			if (id.find('/') == std::string::npos) {
				appendSignal(id, section, route.reversed_direction);
				continue;
			}
			const std::size_t first = id.find("@-");
			const std::size_t middle = id.find("/@", first == std::string::npos ? 0 : first + 1);
			const std::size_t last = id.find("@-", middle == std::string::npos ? 0 : middle + 1);
			if (first != std::string::npos && middle != std::string::npos && last != std::string::npos) {
				appendSignal(std::string_view(id).substr(0, first + 1), section, route.reversed_direction);
				appendSignal(std::string_view(id).substr(middle + 1, last - middle), section, route.reversed_direction);
			}
		}
	}
	// A failed section is blocked and its signals fail in both directions, for as long as the failure is active.
	for (const SimulationIncident& incident : run.incidents) {
		if (!signalFailureActive(incident, timestep))
			continue;
		for (const std::string& id : incident.resolvedSectionIDs) {
			if (id.empty())
				continue;
			auto& state = sectionStates[id];
			state.sectionId = id;
			state.blocked = true;
			signalStates.fail(id);
		}
	}
	snapshot.signalStates = signalStates.take();
	snapshot.sectionStates.reserve(sectionStates.size());
	for (auto& entry : sectionStates)
		snapshot.sectionStates.push_back(std::move(entry.second));

	for (const StationPlatform& platform : run.platforms) {
		GuiPlatformState state;
		state.stationId = platform.StationID;
		state.platformId = platform.ID;
		state.maxVolume = platform.Max_Passenger_Volume;
		for (const auto& passenger : platform.Current_List_Pax_On_Platform)
			state.passengerIds.push_back(passenger.first);
		for (const Passenger& passenger : run.passengers) {
			if (!passenger.IsIntheNetwork && passenger.TimeExitedTheNetwork > 0
				&& timestep >= passenger.TimeExitedTheNetwork && timestep <= passenger.TimeExitedTheNetwork + 5
				&& platform.StationID == passenger.StationExitedTheNetworkID
				&& platform.ID == passenger.PlatformExitedTheNetworkID)
				state.passengerIds.push_back(passenger.ID);
		}
		snapshot.platforms.push_back(std::move(state));
	}

	for (const Passenger& passenger : run.passengers) {
		GuiPassengerState state;
		state.id = passenger.ID;
		state.status = passenger.CurrentStatus;
		state.waitingPlatform = passenger.Current_WaitingStationPlatformID;
		state.nextTrain = passenger.Current_Train_To_Wait;
		const auto journey = std::find_if(passenger.Journeys.begin(), passenger.Journeys.end(),
			[&passenger](const Journey& value) { return value.ID == passenger.current_JourneyID; });
		if (journey != passenger.Journeys.end()) {
			const auto trip = std::find_if(journey->Trips.begin(), journey->Trips.end(),
				[&passenger](const Trip& value) { return value.TripID == passenger.current_TripID; });
			if (trip != journey->Trips.end())
				state.nextDestination = trip->Arr_Station_ID;
		}
		snapshot.passengers.push_back(std::move(state));
	}

	const std::size_t messageCount = std::min({run.vcMsgTimestep.size(), run.vcMsgTrain.size(), run.vcMsgText.size()});
	for (std::size_t i = 0; i < messageCount; ++i) {
		if (timestep >= run.vcMsgTimestep[i] && timestep <= run.vcMsgTimestep[i] + 40)
			snapshot.virtualCouplingMessages.push_back({run.vcMsgTimestep[i], run.vcMsgTrain[i], run.vcMsgText[i]});
	}
	return snapshot;
}

//-------

// simulation object (global variable)
DispatchController simulation;

// The controller holds the run of the process until the legacy names of its state are gone (see SimulationRun.h).
SimulationRun& DispatchController::mutableRun() {
	return defaultRun();
}

const SimulationRun& DispatchController::run() const {
	return defaultRun();
}

std::vector<SceneDiagnostic> DispatchController::prepareScene(const SceneModel& scene,
	const std::string& selectedScenarioId, const SceneRunSelection& selectedOccurrences) {
	using DetailClock = std::chrono::steady_clock;
	const char* detailGate = std::getenv("QEGTRAIN_STARTUP_NATIVE_DETAIL");
	const char* timingGate = std::getenv("QEGTRAIN_STARTUP_TIMING");
	const bool detailEnabled = detailGate && std::strcmp(detailGate, "1") == 0
		&& timingGate && std::strcmp(timingGate, "1") == 0;
	static unsigned long long detailInvocation = 0;
	const unsigned long long invocation = detailEnabled ? ++detailInvocation : 0;
	const auto now = [detailEnabled] {
		return detailEnabled ? DetailClock::now() : DetailClock::time_point{};
	};
	const auto started = now();
	std::chrono::nanoseconds resetSetupTime{};
	std::chrono::nanoseconds validationTime{};
	std::chrono::nanoseconds infrastructureTime{};
	std::chrono::nanoseconds operationsTime{};
	std::chrono::nanoseconds outputTime{};
	const auto elapsed = [](DetailClock::time_point from, DetailClock::time_point to) {
		return std::chrono::duration_cast<std::chrono::nanoseconds>(to - from);
	};
	const auto reportDetail = [&](const char* stage) {
		if (!detailEnabled)
			return;
		const auto total = resetSetupTime + validationTime + infrastructureTime + operationsTime + outputTime;
		const auto milliseconds = [](std::chrono::nanoseconds duration) {
			return std::chrono::duration<double, std::milli>(duration).count();
		};
		// A marker line of the startup timing, written as it is and not through the logging core.
		std::fprintf(stdout, "QEGTRAIN_NATIVE_DETAIL invocation=%llu total_ms=%.6f reset_setup_ms=%.6f runnable_validation_ms=%.6f infrastructure_signalling_ms=%.6f operations_ms=%.6f output_directory_setup_ms=%.6f stage=%s\n",
			invocation, milliseconds(total), milliseconds(resetSetupTime), milliseconds(validationTime),
			milliseconds(infrastructureTime), milliseconds(operationsTime), milliseconds(outputTime), stage);
		std::fflush(stdout);
	};

	beginScenePreparation();
	if (live_)
		liveSceneName_ = scene.name;
	const std::optional<double> effectiveDurationOverride = initial_variables.durationOverride
		? std::optional<double>(initial_variables.times)
		: std::nullopt;
	auto checkpoint = now();
	resetSetupTime = elapsed(started, checkpoint);
	std::vector<SceneDiagnostic> diagnostics = validateRunnableScene(scene, selectedOccurrences,
		effectiveDurationOverride);
	auto next = now();
	validationTime = elapsed(checkpoint, next);
	checkpoint = next;
	if (hasErrors(diagnostics)) {
		resetState();
		reportDetail("runnable_validation_failure");
		return diagnostics;
	}

	// The run is built apart from the run of the controller, which a failed scene must not leave half built.
	const auto staged = std::make_unique<SimulationRun>();
	const std::vector<SceneDiagnostic> infrastructure =
		buildInfrastructureAndSignallingFromScene(*staged, scene);
	diagnostics.insert(diagnostics.end(), infrastructure.begin(), infrastructure.end());
	const bool infrastructureFailed = hasErrors(diagnostics);
	if (infrastructureFailed)
		resetState();
	next = now();
	infrastructureTime = elapsed(checkpoint, next);
	checkpoint = next;
	if (infrastructureFailed) {
		reportDetail("infrastructure_signalling_failure");
		return diagnostics;
	}

	const std::vector<SceneDiagnostic> operations =
		buildOperationsFromScene(*staged, scene, selectedScenarioId, selectedOccurrences);
	diagnostics.insert(diagnostics.end(), operations.begin(), operations.end());
	if (hasErrors(diagnostics)) {
		resetState();
		next = now();
		operationsTime = elapsed(checkpoint, next);
		reportDetail("operations_failure");
		return diagnostics;
	}
	if (initial_variables.RChoice) {
		const bool hasUsablePassengerJourney = std::any_of(staged->passengers.begin(), staged->passengers.end(),
			[](const Passenger& passenger) { return !passenger.Journeys.empty(); });
		if (!hasUsablePassengerJourney) {
			diagnostics.push_back({SceneSeverity::Error, "scene.route_choice.passengers.none",
				"Route choice requires at least one usable passenger journey in the prepared run",
				"passengers.json", "passenger", {}, "passengers", {},
				"Add a passenger journey within the selected run"});
			resetState();
			next = now();
			operationsTime = elapsed(checkpoint, next);
			reportDetail("operations_failure");
			return diagnostics;
		}
	}
	next = now();
	operationsTime = elapsed(checkpoint, next);
	checkpoint = next;

	if (!nextRunOutputFolder_.empty())
		initial_variables.OutputMainFolder = nextRunOutputFolder_;
	if (initial_variables.OutputMainFolder.empty())
		initial_variables.OutputMainFolder = "Output";
	ensureDirectory(initial_variables.OutputMainFolder);
	ensureDirectory(initial_variables.OutputMainFolder + "/TrainTrajectories");
	ensureDirectory(initial_variables.OutputMainFolder + "/PassengerStatus");
	ensureDirectory(initial_variables.OutputMainFolder + "/Rescheduling");
	ensureDirectory(initial_variables.OutputMainFolder + "/TrainTrajectories/RoutesGenerated");
	// Train::printTrainServicePathDiagram and Train::printTrainArrDepMsg append, so each run starts without their files.
	removeRunOutput(initial_variables.OutputMainFolder + "/TrainTrajectories/TrainServicePathDiagram.txt");
	removeRunOutput(initial_variables.OutputMainFolder + "/Rescheduling/EGTRAINOutput.txt");
	// The run reads its settings from this copy.
	staged->settings = initial_variables;
	mutableRun().adopt(std::move(*staged));
	outputTime = elapsed(checkpoint, now());
	reportDetail("success");
	return diagnostics;
}

// The trains and passengers of the run that the scene replaces are released before the new ones are built.
void DispatchController::beginScenePreparation() {
	snapshotMailbox_.take();
	prepareNativeOperationsState(mutableRun());
}

// Reset all native runtime state before loading another canonical scene.
void DispatchController::resetState() {
	snapshotMailbox_.take();
	resetNativeInfrastructureState(mutableRun());
	resetNativeOperationsState(mutableRun());
}

std::shared_ptr<const GuiSimulationSnapshot> DispatchController::takeSimulationSnapshot() {
	return snapshotMailbox_.take();
}

// The live stream of the process and the state of the run that publishes to it. Only the thread that runs
// the steps uses the members other than the server, and startLive and stopLive are called while no run goes on.
struct DispatchLive {
	LiveServer server;
	// A state frame for every every-th step.
	int every = 1;
	// The server takes commands, which the run fetches at the top of each step.
	bool commands = false;
	// True from the hello frame of a run to its last event.
	bool inRun = false;
	LiveRunInfo run;
	// Position of the next frame in the run.
	std::uint64_t seq = 0;
	// Number of steps that have run.
	int steps = 0;
	// The snapshot of the last state frame that held the signals.
	std::shared_ptr<const GuiSimulationSnapshot> signalsFrom;
};

namespace {
std::string newRunId() {
	static const char digits[] = "0123456789abcdef";
	std::random_device random;
	std::string id;
	for (int word = 0; word < 2; ++word) {
		const std::uint32_t bits = random();
		for (int shift = 28; shift >= 0; shift -= 4)
			id += digits[(bits >> shift) & 0xF];
	}
	return id;
}

// Text that is not UTF-8 becomes U+FFFD, so a frame is always made.
std::shared_ptr<const std::string> frameText(const nlohmann::json& frame) {
	return std::make_shared<const std::string>(frame.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
}

// Reports the outcome of a command that a client sent: applied, or turned down for the reason.
void pushLiveCommandEvent(DispatchLive& live, const LiveCommand& command, const char* rejectedBecause) {
	live.server.pushEvent(frameText(makeCommandEventFrame(live.run, live.seq++, command, live.steps, rejectedBecause)));
}

// Turns down the commands that wait for the run, which will not take them.
void rejectLiveCommands(DispatchLive& live, const char* reason) {
	while (const std::optional<LiveCommand> command = live.server.tryPopCommand())
		pushLiveCommandEvent(live, *command, reason);
}

// Opens the run in the stream: new run id, the time axis of the run and the hello frame with its trains.
void beginLiveRun(DispatchLive& live, const std::string& sceneName, const SimulationRun& run) {
	live.run.runId = newRunId();
	live.run.sceneName = sceneName;
	const InitialParameters& settings = run.settings;
	live.run.baseTimeSeconds = settings.startingSimulationTime;
	live.run.horizonSteps = static_cast<int>(settings.times);
	live.run.stepSeconds = timestep;
	live.seq = 0;
	live.steps = 0;
	live.signalsFrom.reset();
	live.inRun = true;
	GuiSimulationSnapshot identities;
	identities.trains.reserve(static_cast<std::size_t>(run.numTrains));
	for (int i = 0; i < run.numTrains; ++i)
		identities.trains.push_back(trainIdentity(i, run.trains[i]));
	live.server.pushHello(live.run.runId, frameText(makeHelloFrame(live.run, live.seq++, identities)));
	rejectLiveCommands(live, "no_active_run");
}

// The signal list goes into the first state frame of a run and into each frame in which it differs from the last that held it.
void publishLiveState(DispatchLive& live, const std::shared_ptr<const GuiSimulationSnapshot>& snapshot) {
	const bool withSignals = !live.signalsFrom || liveSignalsChanged(live.signalsFrom->signalStates, snapshot->signalStates);
	if (withSignals)
		live.signalsFrom = snapshot;
	live.server.pushState(frameText(makeStateFrame(live.run, live.seq++, *snapshot, withSignals)));
}

RunCommand toRunCommand(const LiveCommand& command) {
	RunCommand result;
	result.delayMs = command.delayMs;
	switch (command.kind) {
		case LiveCommandKind::Pause:
			result.kind = RunCommandKind::Pause;
			break;
		case LiveCommandKind::Resume:
			result.kind = RunCommandKind::Resume;
			break;
		case LiveCommandKind::Stop:
			result.kind = RunCommandKind::Stop;
			break;
		case LiveCommandKind::SetDelayMs:
			result.kind = RunCommandKind::SetDelayMs;
			break;
	}
	return result;
}

// Hands the commands that clients have sent to the control of the run, which applies them before the
// next step, and reports each in the stream. steps is the number of steps that have run.
void takeLiveCommands(DispatchLive& live, RunControl& control, int steps) {
	live.steps = steps;
	while (const std::optional<LiveCommand> command = live.server.tryPopCommand())
		pushLiveCommandEvent(live, *command, control.submit(toRunCommand(*command)) ? nullptr : "queue_full");
}

// Reports a change in the life of the run, and ends the run in the stream when the event is its last.
// Once the steps have ended the run takes no command, so the ones that wait are turned down first.
void pushLiveEvent(DispatchLive& live, LiveEvent event) {
	if (!live.inRun)
		return;
	rejectLiveCommands(live, "run_ended");
	live.server.pushEvent(frameText(makeEventFrame(live.run, live.seq++, event, live.steps)));
	if (event != LiveEvent::StepsFinished) {
		live.inRun = false;
		live.signalsFrom.reset();
	}
}
} // namespace

DispatchController::DispatchController(QObject* parent) : QObject(parent) {}

DispatchController::~DispatchController() = default;

bool DispatchController::startLive(int port, int every, bool commands, const std::filesystem::path& endpointDirectory, std::string* error) {
	if (live_) {
		if (error)
			*error = "the live stream is running already";
		return false;
	}
	auto live = std::make_unique<DispatchLive>();
	if (!live->server.start(port, commands, endpointDirectory, error))
		return false;
	live->every = std::max(every, 1);
	live->commands = commands;
	live_ = std::move(live);
	return true;
}

void DispatchController::stopLive() {
	live_.reset();
}

std::string DispatchController::liveEndpoint() const {
	return live_ ? live_->server.stateEndpoint() : std::string();
}

void DispatchController::publishSimulationSnapshot(int timestep) {
	// A run that nothing draws still builds the snapshot of a step for which the live stream has a frame due.
	const bool liveDue = live_ && live_->inRun && timestep % live_->every == 0;
	if (!snapshotsEnabled_ && !liveDue) {
		PlaybackProfiler::instance().noteTimestep(timestep);
		return;
	}
	QEGTRAIN_PROFILE_SCOPE("worker/playback_step/snapshot_build_publish", "worker", "");
	std::shared_ptr<const GuiSimulationSnapshot> snapshot;
	{
		QEGTRAIN_PROFILE_SCOPE("worker/playback_step/snapshot_build_publish/build_gui_snapshot", "worker",
			"worker/playback_step/snapshot_build_publish");
		snapshot = std::make_shared<const GuiSimulationSnapshot>(buildGuiSimulationSnapshot(run(), timestep));
	}
	if (liveDue)
		publishLiveState(*live_, snapshot);
	if (snapshotsEnabled_) {
		replayCandidate_.record(snapshot);
		{
			QEGTRAIN_PROFILE_SCOPE("worker/playback_step/snapshot_build_publish/mailbox_publish", "worker",
				"worker/playback_step/snapshot_build_publish");
			if (snapshotMailbox_.publish(std::move(snapshot)))
				emit snapshotAvailable();
		}
	}
	PlaybackProfiler::instance().noteTimestep(timestep);
}

void DispatchController::runSimulation() {

	runCompleted_ = false;

	SimulationRun& run = mutableRun();
	if (run.numTrains <= 0) {
		emit executionRejected();
		EG_LOG(AppRun, Error) << "ERROR: Cannot run simulation because zero trains were loaded.\n";
		return;
	}
	const InitialParameters& settings = run.settings;
	const std::string trajectoryFolder = settings.OutputMainFolder + "/TrainTrajectories";

	// The worker of a run in the window made its control before the run, so a stop requested earlier is
	// kept. A run without a worker, as with -g 0, makes one for itself.
	const SimulationWorker* worker = SimulationWorker::active();
	runControl_ = worker ? worker->control() : std::make_shared<RunControl>();

	QElapsedTimer observationTimer;
	observationTimer.start();
	emit executionBegan();
	if (live_)
		beginLiveRun(*live_, liveSceneName_, run);

	// A stopped run reports its cancellation here and skips the remaining stages.
	const auto stoppedHere = [&] {
		if (!runControl_->isStopRequested())
			return false;
		if (live_)
			pushLiveEvent(*live_, LiveEvent::RunCancelled);
		emit executionReturned(observationTimer.elapsed(), true);
		return true;
	};

	EG_LOG(AppRun, Info) << "\n\nSimulating Train Runs...\n\n";

	// Setting departing time of train to the scheduled one
	for (int i = 0; i < run.numTrains; i++) {

		run.trains[i].departure_time = run.trains[i].scheduled_departure_time;
	}

	Train_Simulation_Mixed_Signalling_With_Passengers(signalCode1, signalCode2, signalCode3); // Function to Launch EGTRAIN considering passenger flow simulation

	if (stoppedHere())
		return;

	emit executionPostprocessing();
	ComputeEnergyConsumptionForAllTrains(run.trains.data(), run.numTrains);
	ComputeTimetableEnergyConsumption(run.trains.data(), run.numTrains, settings.OutputMainFolder);

	if (stoppedHere())
		return;

	clock_t StartRun = clock();

	// sorting recorded events of all infrastructure elements in chronological order
	SortRecordedEventsForAllInfrastructureElements(run.infra.infraEvents);

	if (stoppedHere())
		return;

	EG_LOG(AppRun, Info) << "Computing Blocking Times....\n";

	clock_t EndRun = clock();

	double TimeElapsed = 0;

	TimeElapsed = (double)((EndRun - StartRun) / CLOCKS_PER_SEC); // NOLINT(bugprone-integer-division): the log shows whole seconds
	EG_LOG(AppRun, Info) << "TimeElapsed is : " << TimeElapsed;

	// Compute passage times at timetable points
	Compute_TimetablingPoints_For_All_Trains(run.trains.data(), run.numTrains);

	// Calculate Train Delays
	calculateArrivalDelayAllTrains(run);
	calculateDelayStatsForAllStations(run);
	calculateDelayStatsAtStation(run, run.finalStation); // Computing the delay stats at the final station of all the trains
	Compute_Input_Delays(run);							 // Computing the amount of entrance delays and disturbances set in input in the scenario
														 // Print Delay at stations in the right folder
	Print_Station_Delay_Stats(run, trajectoryFolder, "pos");

	// Print Out Passenger Delays
	printPassengerTotalJourneyDelay(run.passengers, settings.OutputMainFolder + "/PassengerStatus");

	// Calculating positive and negative Train Delays to debug the program
	calculatePosAndNegArrivalDelayAllTrains(run);
	calculatePosAndNegDelayStatsForAllStations(run);
	calculatePosAndNegDelayStatsAtStation(run, run.finalStation); // Calculating the pos and neg Delay stats at final station of all trains
																  // Print aggregated results of positive and negative delays at stations
	Print_Station_Delay_Stats(run, trajectoryFolder, "pos&neg");

	Print_Computing_Times(run, trajectoryFolder); // Printing the total computation time of ROMA and EGTRAIN


	if (stoppedHere())
		return;

	EG_LOG(AppRun, Info) << "\n\n Computing Train Blocking Times....\n\n";

	ComputeBlockingTimesInMixedSignallingForAllTrains(run, 5, (3 + settings.bufferTime), 0.5, 50, trajectoryFolder, 0, settings.recoveryTimePercentage); // Computing Blocking Times in mixed signalling Areas

	// The dense path diagram is part of the detailed trajectories.
	if (settings.exportDetailedTrajectories)
		PrintTrainPathDiagram(run, run.trains.data(), run.numTrains, trajectoryFolder);

	PrintTimetablePoints(run, trajectoryFolder);

	// print trajectories
	// before in Regional destructor - moved here because vectors are deleted automatically in the destructor and it is no longer possible to use them there
	if (stoppedHere())
		return;
	// The per-train files and their folder are part of the detailed trajectories.
	if (settings.exportDetailedTrajectories) {
		ensureDirectory(settings.OutputMainFolder + "/TEMP");
		for (int i = 0; i < run.numTrains; i++) {
			if (stoppedHere())
				return;
			run.trains[i].PrintTrajectory();
		}
	}
	// The window reports the end of the run here. A run without a window leaves it to main.
	if (worker) {
		EG_LOG(AppRun, Info) << "\n End of Simulation";
	}
	runCompleted_ = true;
	if (live_)
		pushLiveEvent(*live_, LiveEvent::RunCompleted);
	emit executionReturned(observationTimer.elapsed(), false);
}

void DispatchController::Train_Simulation_Mixed_Signalling_With_Passengers(double v1, double v2, double v3) {
	SimulationRun& run = mutableRun();
	const InitialParameters& settings = run.settings;
	nlohmann::json jsmsg;

	// The trains by departure time; trains due at the same time stay in the order of the trains of the run.
	std::vector<int> dueOrder(run.numTrains);
	std::iota(dueOrder.begin(), dueOrder.end(), 0);
	std::stable_sort(dueOrder.begin(), dueOrder.end(), [&](int a, int b) {
		return run.trains[a].departure_time < run.trains[b].departure_time;
	});
	std::vector<int> movementOrder;
	int lastStep = -1;

	RunControl& control = *runControl_;
	// The commands of live clients are passed to the control at the top of each step and while a pause waits.
	std::function<void()> fetchCommands;
	if (live_ && live_->commands)
		fetchCommands = [this, &control, &lastStep] { takeLiveCommands(*live_, control, lastStep + 1); };
	for (int t = 0; t < settings.times; t++) {
		// Queued commands, pause, stop and the step delay take effect here, before the step moves anything.
		if (!control.beginStep(fetchCommands))
			break;
		lastStep = t;
		{
			QEGTRAIN_PROFILE_SCOPE("worker/playback_step/compute", "worker", "");
			clock_t startEGTRAIN = clock(); // EGTRAIN start time
			if (t % ProgressEverySteps == 0) {
				EG_LOG(AppRun, Info) << "\r Time of simulation is " << t;
			}

			{
				QEGTRAIN_PROFILE_SCOPE("worker/playback_step/compute/passenger_entry_platform_refresh", "worker",
					"worker/playback_step/compute");
				// Simulate entrance process of passengers on the railway network according to route choice
				checkJourneyStartForAllPassengers(t, settings.startingSimulationTime, run.passengers);

				// This function will update the list of all waiting passengers at all platforms in the network at every instant
				// To reduce the computation time it is instead recommended that the function to update the list of waiting passengers at platform is only used in the functional "Simulate_Train_Passenger_Interaction"
				// In that case  please comment the line below and uncomment the corresponding function Update_List_Passengers_Waiting_At_Platform in that function
				if (settings.PAX_GUI) {
					Update_List_Passengers_Waiting_At_ALL_Platforms(run.platforms, run.passengers);
				}
			}

			{
				QEGTRAIN_PROFILE_SCOPE("worker/playback_step/compute/train_movement", "worker",
					"worker/playback_step/compute");
				// Simulate train movement at each simulation step
				// Every train keeps its place in the order of the trains of the run, except that the trains that are due and still
				// waiting to enter take the places of those trains in the order in which they are due. Of several trains
				// waiting at the entry of a route the one due first then enters first.
				movementOrder.resize(run.numTrains);
				std::iota(movementOrder.begin(), movementOrder.end(), 0);
				const auto dueAndWaiting = [&](int j) { return !run.trains[j].CanEnter && t >= run.trains[j].departure_time; };
				auto nextDue = dueOrder.begin();
				for (int& place : movementOrder) {
					if (!dueAndWaiting(place))
						continue;
					while (!dueAndWaiting(*nextDue))
						++nextDue;
					place = *nextDue++;
				}
				for (const int j : movementOrder) {
					run.trains[j].trajectoryComputationIncludingMovingBlock(t, v1, v2, v3);
					run.trains[j].recordEarliestActiveTrajectoryIndex(t);
					run.trains[j].recordStationPassagesAtTime(t);

					// check if train arrived at destination or departed from origin
					run.trains[j].checkTrainArrDep(j, t);
				}
			}

			{
				QEGTRAIN_PROFILE_SCOPE("worker/playback_step/compute/train_passenger_state_payload", "worker",
					"worker/playback_step/compute");
				// Here we prepare the Traffic State data
				for (int n = 0; n < run.numTrains; n++) {

					// The interaction Trains and passengers and corresponding dwell time dependency at platform is simulated here in a non multi-thread fashion
					// Simulating interaction with the passengers and changing dwell times according to the chosen passenger-dependent dwell time function
					Simulate_Train_Passenger_Interactions(run, t, settings.startingSimulationTime, run.trains[n], run.passengers, run.platforms);

					// round to 2 decimals
					double xPosition = std::ceil(run.trains[n].trainXPosition(t) * 100.0) / 100.0;

					// The traffic-state payload is only built when it is shared (-TSM 1)
					if (settings.TSM) {
						double trainSpeed = std::ceil((run.trains[n].instant_train_speed[t] * 3.6) * 100.0) / 100.0;
						jsmsg["trains"][run.trains[n].trainDescription]["km-point"] = xPosition;
						jsmsg["trains"][run.trains[n].trainDescription]["speed"] = trainSpeed;

						jsmsg["trains"][run.trains[n].trainDescription]["BlockOccupied"] = run.trains[n].Bs.ID;
						jsmsg["trains"][run.trains[n].trainDescription]["lastOccTime"] = run.trains[n].ComputeLastOccupationTime_real_time(t, run.trains[n].Bs.ID, 10);
						jsmsg["trains"][run.trains[n].trainDescription]["direction"] = run.infra.routes[run.trains[n].indexOfRoute].reversed_direction;

						jsmsg["trains"][run.trains[n].trainDescription]["depTime"] = run.trains[n].departure_time;
						if (run.trains[n].departure_time <= t) {
							char c = '-';
							int index = run.trains[n].Bs.ID.find(c);

							jsmsg["trains"][run.trains[n].trainDescription]["trackID"] = run.trains[n].Bs.ID.substr(index + 1, run.trains[n].Bs.ID.length() - index - 2);
							jsmsg["trains"][run.trains[n].trainDescription]["inArea"] = 1;
						} else
							jsmsg["trains"][run.trains[n].trainDescription]["inArea"] = 0;
					}
				}
			}

			// for the ZeroMQbroker
			if (settings.TSM) {
				jsmsg["time"] = t;
				const std::string xml = trafficStateMonitoring_xml(jsmsg);
				EG_LOG(IoZmq, Debug) << "\n\n Sending the following Traffic State XML file\n"
									 << xml;
				send_external_state(jsmsg, xml, "tcp://127.0.0.1:5555");
			}

			if (settings.RChoice) {
				const nlohmann::json route_choice_json = routeChoicePayload(run.passengers, t);
				const std::string xml = routeChoice_xml(route_choice_json);
				EG_LOG(IoZmq, Debug) << "\n\n Sending the following Route Choice XML file\n"
									 << xml;
				send_external_state(route_choice_json, xml, "tcp://127.0.0.1:5556");
			}

			{
				QEGTRAIN_PROFILE_SCOPE("worker/playback_step/compute/infrastructure_signalling_cleanup", "worker",
					"worker/playback_step/compute");
				run.etcsMa.clear(); // Clear the list containing all the Movement Authorities given to the trains at the previous instant

				Occupy_Block_Sections_Of_Route(run, t); // Fill in the lists Blocks_Occupied and BlocksConnected

				// Occupy failed sections and give them an End of Authority so both
				// aspect-driven and moving-block trains react to the incident
				Apply_Signal_Failures_Mixed_Signalling(run, t);

				// Held single-track sections give trains of the other direction an End of Authority at levels 3 and 4
				Apply_Single_Track_Authorities_Mixed_Signalling(run);

				// Only levels 3 and 4
				ReportAllTrainPositionsToRBC(run, t, 50);

				// function to protect all station areas
				protectStationAreas(run, t);

				releaseMixedSignallingSystem(run); // Release Blocks connected with the one really occupied by a train

				activateMixedSignallingSystem(run); // Apply the rules of the signalling system for all the Blocks contained

				unlockDoubleSwitches(run); // unlock double switches (otherwise trains stop in the middle of double switches)

				run.blocksOccupied.clear();	 // Clear the list of the occupied blocks
				run.blocksConnected.clear(); // Clear the list of the connected blocks
			}

			clock_t endEGTRAIN = clock();																	// variable that sets the time in which EGTRAIN ends
			run.compTimeEgtrain = run.compTimeEgtrain + double(endEGTRAIN - startEGTRAIN) / CLOCKS_PER_SEC; // computing the cumulated computation time of EGTRAIN
		}

		publishSimulationSnapshot(t);
	}
	if (live_) {
		live_->steps = lastStep + 1;
		pushLiveEvent(*live_, LiveEvent::StepsFinished);
	}
	// The file holds passengers in the network at the last step that ran, including a stopped run.
	// Write it once because each write replaces the file.
	if (lastStep >= 0)
		printCurrentPassengerStatus(lastStep, settings.startingSimulationTime, run.passengers, settings.OutputMainFolder + "/PassengerStatus");
}

void DispatchController::printLastTrainServicePathDiagram() {
	SimulationRun& run = mutableRun();
	const InitialParameters& settings = run.settings;
	// print last services
	for (int i = 0; i < run.numTrains; i++) {
		// print only trains running during simulation (departure_time >= settings.times indicate that trains were not used, e.g. if only sprinters were used, IC are not printed)
		if (run.trains[i].departure_time < settings.times) {
			if (!run.trains[i].OutOfSimulation) {
				TrajectoryEndTimeOverride temporaryEnd(run.trains[i].End_Time, settings.times - 1);
				run.trains[i].printTrainServicePathDiagram(settings.OutputMainFolder + "/TrainTrajectories", -1); // -1 indicates no next service
			} else {
				run.trains[i].printTrainServicePathDiagram(settings.OutputMainFolder + "/TrainTrajectories", -1);
			}
		}
	}
}

void DispatchController::setVectorSizesFromInput(int vec_size) {
	SimulationRun& run = mutableRun();
	for (int i = 0; i < run.numTrains; i++) {
		// define vector sizes with length of simulation from user input
		run.trains[i].setTrainVectorSizesFromInput(vec_size);
	}
}
