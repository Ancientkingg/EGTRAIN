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

class DispatchController : public QObject {
	Q_OBJECT

public:
	explicit DispatchController(QObject* parent = nullptr);
	~DispatchController() override;

	// Builds the run of the scene apart from the run of the controller and puts it in its place when the scene is
	// ready. A scene that is not ready ends the run that was there: the controller then holds a new run.
	std::vector<SceneDiagnostic> prepareScene(const SceneModel& scene,
		const std::string& selectedScenarioId = {},
		const SceneRunSelection& selectedOccurrences = {});

	// Returns the controller to a new run.
	void resetState();

	// The run that prepareScene made, with the results after it has been run. While a SimulationWorker is running,
	// the thread that steps the run owns its state, and the window reads only what a step does not change (the
	// infrastructure and the layout of the canvas) and sees the rest through the snapshots. Results are read after the
	// worker has joined.
	const SimulationRun& run() const;
	// The same run for the canvas, which writes the positions of the layout into the tracks, stations and connections. The
	// thread rules of run() apply.
	SimulationRun& mutableRun();

	void runSimulation();

	void Train_Simulation_Mixed_Signalling_With_Passengers(double v1, double v2, double v3);

	void printLastTrainServicePathDiagram();

	void setVectorSizesFromInput(int vec_size);

	std::shared_ptr<const GuiSimulationSnapshot> takeSimulationSnapshot();
	// A run that nothing draws needs no snapshots and no replay history. Off, the
	// run builds neither. Call this before the run starts.
	void setSnapshotsEnabled(bool enabled) { snapshotsEnabled_ = enabled; }
	// Only call these on the GUI thread before launch or after the worker has joined.
	void resetReplayCandidate() { replayCandidate_.clear(); }
	GuiReplayHistory takeReplayCandidate() { return std::exchange(replayCandidate_, GuiReplayHistory()); }
	// True when the last run executed every stage, also if a stop arrived after its last check.
	bool lastRunCompleted() const { return runCompleted_; }
	// The control of the run that started last, also when it has ended: the control of the window's
	// SimulationWorker, or the one that a run without a worker made for itself. Call this on the thread
	// that runs the simulation, or after the run has returned.
	std::shared_ptr<RunControl> runControl() const { return runControl_; }
	// Output folder that prepareScene applies to the run it prepares. Call these on the GUI
	// thread; a run keeps the folder it was prepared with.
	void setNextRunOutputFolder(const std::string& folder) { nextRunOutputFolder_ = folder; }
	const std::string& nextRunOutputFolder() const { return nextRunOutputFolder_; }

	// Starts the live stream (see io/LiveServer.h): every run that follows publishes a hello frame, a state
	// frame for every every-th step and the events of its end, and the stream outlives the runs. The endpoint
	// file goes to endpointDirectory. With commands, the commands that clients send (pause, resume, stop and
	// set_delay_ms) are handed to the control of the run at the top of each step, also while the run is
	// paused, and each is reported as an applied or rejected event. Call this before prepareScene and while no
	// run is going on. Returns false with the reason in *error when the server cannot start. A process that
	// never calls it has no server, no thread and no socket, and its runs build nothing for the stream.
	bool startLive(int port, int every, bool commands, const std::filesystem::path& endpointDirectory, std::string* error = nullptr);
	// Sends the frames that are still queued and ends the stream. Call this while no run is going on.
	void stopLive();
	// "tcp://127.0.0.1:<port>" of the state socket, or empty while the stream is off.
	std::string liveEndpoint() const;

signals:
	void iterationFinished(int timestep);
	void snapshotAvailable();
	void simulationFinished();
	// Execution-thread observations, independent of GUI result availability.
	void executionRejected();
	void executionBegan();
	void executionReturned(qint64 elapsedMs, bool cancelled);
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

// simulation object (global variable)
extern DispatchController simulation;

#endif // DISPATCHCONTROLLER_H
