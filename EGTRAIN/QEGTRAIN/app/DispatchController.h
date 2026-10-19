#ifndef DISPATCHCONTROLLER_H
#define DISPATCHCONTROLLER_H

#include "app/GuiSimulationSnapshot.h"
#include "app/GuiReplayHistory.h"
#include "simulation/Optimisation.h"
#include "simulation/RunControl.h"

#include <QObject>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#ifdef signals
/// Set when the Qt keyword signals is defined here. SceneModel.h has a member of that name, so the keyword is removed
/// while it is included and defined again after it.
#define EGTRAIN_RESTORE_SIGNALS_KEYWORD
#undef signals
#endif
#include "scene/SceneModel.h"
#ifdef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#define signals Q_SIGNALS
#undef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#endif

struct DispatchLive;
struct SimulationRun;

/// Prepares a scene into the run of the process and runs it.
///
/// The process has one controller, the global simulation, and every controller works on defaultRun(). prepareScene, resetState,
/// the live stream functions and the reading of the results are called on the GUI thread, or on the main thread of a run without
/// a window, while no run is going on. runSimulation runs on the thread of a SimulationWorker in the window and on the main thread
/// without a window. The signals of a run are emitted on the thread of the run.
class DispatchController : public QObject {
	Q_OBJECT

public:
	/// A controller without a live stream, with snapshots on.
	explicit DispatchController(QObject* parent = nullptr);
	/// Ends the live stream, if it runs.
	~DispatchController() override;

	/// Builds the run of the scene apart from the run of the controller and puts it in its place when the scene is
	/// ready. A scene that is not ready ends the run that was there: the controller then holds a new run.
	///
	/// selectedScenarioId names the scenario whose incidents and delays apply; empty takes the default scenario of the scene,
	/// or its first. selectedOccurrences lists the service occurrences that run; empty runs them all. The run takes a copy of
	/// initial_variables as its settings, writes to its output folder, which is made if needed, and starts without the files
	/// that a run appends to. Returns the diagnostics of the validation and the builders.
	std::vector<SceneDiagnostic> prepareScene(const SceneModel& scene,
		const std::string& selectedScenarioId = {},
		const SceneRunSelection& selectedOccurrences = {});

	/// Returns the controller to a new run: the run of the process is cleared, the infrastructure included, and a waiting
	/// snapshot is dropped.
	void resetState();

	/// The run that prepareScene made, with the results after it has been run. While a SimulationWorker is running,
	/// the thread that steps the run owns its state, and the window reads only what a step does not change (the
	/// infrastructure and the layout of the canvas) and sees the rest through the snapshots. Results are read after the
	/// worker has joined.
	const SimulationRun& run() const;
	/// The same run for the canvas, which writes the positions of the layout into the tracks, stations and connections. The
	/// thread rules of run() apply.
	SimulationRun& mutableRun();

	/// Runs the prepared run on the calling thread: the steps, then the energy, the timetable points, the delay statistics,
	/// the blocking times and the output files. A stop is checked between these stages and ends the run there. A run without
	/// trains is not run: it emits executionRejected and logs an error. The control of the run is that of
	/// SimulationWorker::active(), or a new one when there is no worker.
	void runSimulation();

	/// The steps of a run, t from 0 while t < settings.times, each in seconds from the start of the run. v1, v2 and v3 go to
	/// Train::trajectoryComputationIncludingMovingBlock; runSimulation passes signalCode1 to signalCode3, in m/s. Called by
	/// runSimulation, which sets the control of the run first.
	void Train_Simulation_Mixed_Signalling_With_Passengers(double v1, double v2, double v3);

	/// Appends the service path diagram of each train that departs before the end of the run to
	/// TrainTrajectories/TrainServicePathDiagram.txt in the output folder of the run. The window calls it after a run that was not
	/// stopped, and main after its run.
	void printLastTrainServicePathDiagram();

	/// Gives the per-step vectors of every train vec_size entries of 0 and sets their End_Time to vec_size - 1. Nothing in the
	/// program calls it; the operations builder sizes the vectors.
	void setVectorSizesFromInput(int vec_size);

	/// Takes the newest snapshot that a step published, or nullptr when none waits. The GUI thread calls it after
	/// snapshotAvailable.
	std::shared_ptr<const GuiSimulationSnapshot> takeSimulationSnapshot();
	/// A run that nothing draws needs no snapshots and no replay history. Off, the
	/// run builds neither. Call this before the run starts.
	void setSnapshotsEnabled(bool enabled) { snapshotsEnabled_ = enabled; }
	/// Drops the replay history of the last run. The thread rule of takeReplayCandidate applies.
	void resetReplayCandidate() { replayCandidate_.clear(); }
	/// Hands over the replay history that the last run recorded and keeps an empty one. Only call this on the GUI thread before
	/// launch or after the worker has joined.
	GuiReplayHistory takeReplayCandidate() { return std::exchange(replayCandidate_, GuiReplayHistory()); }
	/// True when the last run executed every stage, also if a stop arrived after its last check.
	bool lastRunCompleted() const { return runCompleted_; }
	/// The control of the run that started last, also when it has ended: the control of the window's
	/// SimulationWorker, or the one that a run without a worker made for itself. Call this on the thread
	/// that runs the simulation, or after the run has returned.
	std::shared_ptr<RunControl> runControl() const { return runControl_; }
	/// Sets the output folder that prepareScene applies to the run it prepares; empty keeps the folder of initial_variables.
	/// Call this on the GUI thread; a run keeps the folder it was prepared with.
	void setNextRunOutputFolder(const std::string& folder) { nextRunOutputFolder_ = folder; }
	/// The output folder that prepareScene applies to the next run, or empty. Call this on the GUI thread.
	const std::string& nextRunOutputFolder() const { return nextRunOutputFolder_; }

	/// Starts the live stream (see io/LiveServer.h): every run that follows publishes a hello frame, a state
	/// frame for every every-th step and the events of its end, and the stream outlives the runs. The endpoint
	/// file goes to endpointDirectory. With commands, the commands that clients send (pause, resume, stop and
	/// set_delay_ms) are handed to the control of the run at the top of each step, also while the run is
	/// paused, and each is reported as an applied or rejected event. Call this before prepareScene and while no
	/// run is going on. Returns false with the reason in *error when the server cannot start. A process that
	/// never calls it has no server, no thread and no socket, and its runs build nothing for the stream.
	/// A port of 0 takes a free port, and an every below 1 counts as 1.
	bool startLive(int port, int every, bool commands, const std::filesystem::path& endpointDirectory, std::string* error = nullptr);
	/// Sends the frames that are still queued and ends the stream. Call this while no run is going on.
	void stopLive();
	/// "tcp://127.0.0.1:<port>" of the state socket, or empty while the stream is off.
	std::string liveEndpoint() const;

signals:
	/// Nothing emits this signal.
	void iterationFinished(int timestep);
	/// A step has published a snapshot and no earlier one waits to be taken. Fetch it with takeSimulationSnapshot.
	void snapshotAvailable();
	/// Nothing emits this signal. SimulationWorker::simulationFinished marks the end of a run in the window.
	void simulationFinished();
	/// Execution-thread observations, independent of GUI result availability: runSimulation turns down a run without trains.
	void executionRejected();
	/// Execution-thread observation: runSimulation starts the run.
	void executionBegan();
	/// Execution-thread observation, emitted once for each run that began: elapsedMs is the time since executionBegan in
	/// milliseconds, and cancelled is true for a run that a stop ended.
	void executionReturned(qint64 elapsedMs, bool cancelled);
	/// Execution-thread observation: the steps have ended and the stages after them start.
	void executionPostprocessing();

private:
	void beginScenePreparation();
	void publishSimulationSnapshot(int timestep);

	GuiSimulationSnapshotMailbox snapshotMailbox_;
	GuiReplayHistory replayCandidate_;
	std::unique_ptr<DispatchLive> live_;
	// Name of the scene that prepareScene prepared last. It is kept only while the live stream runs.
	std::string liveSceneName_;
	bool snapshotsEnabled_ = true;
	std::atomic<bool> runCompleted_{false};
	std::shared_ptr<RunControl> runControl_ = std::make_shared<RunControl>();
	std::string nextRunOutputFolder_;
};

/// The controller of the process. SimulationWorker(QObject*) runs it, and main and the window prepare and read their runs with it.
extern DispatchController simulation;

#endif // DISPATCHCONTROLLER_H
