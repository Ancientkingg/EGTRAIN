#ifndef DISPATCHCONTROLLER_H
#define DISPATCHCONTROLLER_H

#include "app/GuiSimulationSnapshot.h"
#include "app/GuiReplayHistory.h"
#include "simulation/Optimisation.h"

#include <QObject>
#include <atomic>
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

class DispatchController : public QObject {
	Q_OBJECT

public:
	explicit DispatchController(QObject* parent = nullptr) : QObject(parent) {}

	std::vector<SceneDiagnostic> prepareScene(const SceneModel& scene,
			const std::string& selectedScenarioId = {},
			const SceneRunSelection& selectedOccurrences = {});

	void resetState();

	void runSimulation();

	void Train_Simulation_Mixed_Signalling_With_Passengers(double v1, double v2, double v3);

	void printLastTrainServicePathDiagram();

	void setVectorSizesFromInput(int vec_size);

	std::shared_ptr<const GuiSimulationSnapshot> takeSimulationSnapshot();
	// Only call these on the GUI thread before launch or after the worker has joined.
	void resetReplayCandidate() { replayCandidate_.clear(); }
	GuiReplayHistory takeReplayCandidate() { return std::exchange(replayCandidate_, GuiReplayHistory()); }
	// True when the last run executed every stage, also if a stop arrived after its last check.
	bool lastRunCompleted() const { return runCompleted_; }
	// Output folder that prepareScene applies to the run it prepares. Call these on the GUI
	// thread; a run keeps the folder it was prepared with.
	void setNextRunOutputFolder(const std::string& folder) { nextRunOutputFolder_ = folder; }
	const std::string& nextRunOutputFolder() const { return nextRunOutputFolder_; }

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
	std::atomic<bool> runCompleted_{false};
	std::string nextRunOutputFolder_;
};

// simulation object (global variable)
extern DispatchController simulation;

#endif // DISPATCHCONTROLLER_H
