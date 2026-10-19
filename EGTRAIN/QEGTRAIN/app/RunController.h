#ifndef RUNCONTROLLER_H
#define RUNCONTROLLER_H

// RunSelection.h brings in SceneModel.h in the way that the member named signals needs, before any Qt header.
#include "app/RunSelection.h"
#include "app/GuiReplayHistory.h"
#include "diagrams/CompletedRunData.h"
#include "diagrams/RunResults.h"
#include "scene/SceneDiagnostic.h"
#include "simulation/SimulationWorker.h"
#include "telemetry/TelemetryOperation.h"

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThread>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class DelayAnalysisWindow;
class QMainWindow;
class SceneDocument;
class SceneSession;

// The run of a scene named by its case and scenario, for titles and messages.
QString completedRunContext(const RunProvenance& provenance);

// The run of the scene that the window holds. The controller asks the user to review the run, prepares the scene in the
// simulation, starts the simulation on a worker thread, and stops it on request. It keeps what the run leaves: the
// status, the run as plain values, the delay baseline, and the clock time at which the run starts. It is the one place
// in the window that reads the settings of the process (initial_variables). What a run changes in the rest of the
// window, such as the canvas, the panels and the results dock, is done by the window, which the controller tells
// through its Host.
class RunController : public QObject {
	Q_OBJECT

public:
	// What Pause, Stop, Start Time and Output Folder allow.
	enum class RunPhase {
		// No worker exists.
		Idle,
		// The worker runs.
		Running,
		// The worker was asked to stop and has not finished.
		Stopping,
	};

	// What the Run Results dock shows about the delay baseline.
	struct DelayBaselineState {
		bool canSet = false;
		bool canCompare = false;
		QString feedback;
	};

	// What the controller asks of the window, and the points at which it tells the window that the run changed.
	class Host {
	public:
		virtual ~Host() = default;

		virtual telemetry::OperationObservation captureTelemetryOperation() const noexcept = 0;
		// A scripted run has no user to answer a dialog.
		virtual bool dialogsSuppressed() const = 0;
		virtual bool advancedDetailsEnabled() const = 0;
		// Telemetry is on, so the run is observed.
		virtual bool telemetryActive() const = 0;
		// The delay between two steps that the speed slider asks for.
		virtual int stepDelayMs() const = 0;
		// The time over which the occurrences of the services are counted, and how many of them the scene has in all and
		// inside the period.
		virtual double serviceOccurrenceDuration() const = 0;
		virtual int totalServiceOccurrences() const = 0;
		virtual int inPeriodServiceOccurrences() const = 0;
		// The scenario that the editors show, the incidents it holds, and its name or the name of the scenario of the
		// latest run.
		virtual const SceneScenario* selectedScenario() const = 0;
		virtual const std::vector<SceneIncident>& selectedScenarioIncidents() const = 0;
		virtual QString scenarioContext() const = 0;

		// Takes the text of the fields that are being edited into the document, then validates the scene.
		virtual void validateScene() = 0;
		// The run review dialog sends the user to the loaded data or the validation messages.
		virtual void showLoadedData() = 0;
		virtual void showValidation() = 0;
		// The scene is about to be prepared. The window shows that and lets the events through.
		virtual void runPreparing() = 0;
		// The scene is about to be run: the canvas shows the preview or an earlier run, and ends that.
		virtual void teardownForRun() = 0;
		// The results and the status of the earlier run are discarded, and the window shows that.
		virtual void invalidateRunResults() = 0;
		// Calls `prepare`, which builds the run of the scene in the simulation, and measures it when startup timing is on.
		// `identityOk` tells whether startup timing found the scene that it expects before and after; it is true when
		// startup timing is off.
		virtual std::vector<SceneDiagnostic> timedPreparation(
			const std::function<std::vector<SceneDiagnostic>()>& prepare, bool& identityOk) = 0;
		// What the actions of the window enable depends on the scene or on the run, and one of them changed.
		virtual void sceneActionsChanged() = 0;
		// The loaded data tree shows the status of the run.
		virtual void loadedDataChanged() = 0;
		// The case layers panel shows the case name and the clock time.
		virtual void caseLayersChanged() = 0;
		// The window builds the canvas of the prepared run and fits the view.
		virtual void buildRunCanvas() = 0;
		// Startup timing, when it is on, takes the prepared run over: it returns true, and the run does not start. It
		// returns false when startup timing is off.
		virtual bool startupTimingHoldsRun(bool identityOk) = 0;
		// Sets the clock time at which the simulation starts and shows it in the train list.
		virtual void setStartOffset(long long seconds) = 0;

		// The run starts: the window ends the replay of an earlier run and shows the progress.
		virtual void runStarting() = 0;
		// The worker is set up and its thread starts next.
		virtual void workerReady() = 0;
		// Pause and Stop mean something while a run exists, and Start Time and Output Folder only while none does.
		virtual void runPhaseChanged(RunPhase phase) = 0;
		// The editors, the infrastructure dock and the actions follow the run state.
		virtual void runPanelsChanged() = 0;
		// The worker is gone, so the transient states of the canvas end.
		virtual void playbackEnded() = 0;
		// Stops following a train. Returns whether a train was followed.
		virtual bool endFollowing() = 0;
		// The run kept results, which the Run Results dock shows.
		virtual void resultsKept() = 0;
		// The run kept no results. The Run Results dock says why and hides.
		virtual void resultsDiscarded(bool stoppedByUser, bool sceneChangedDuringRun) = 0;
		// The run is over and its results are kept or discarded. Returns true when a scripted run has ended the run for
		// the window, which then does nothing more.
		virtual bool runFinished() = 0;
		// The progress of the run is no longer shown.
		virtual void runProgressEnded() = 0;
		// The history of the finished run, which the window can replay.
		virtual void replayReady(GuiReplayHistory history) = 0;
		// The window follows the trains of the finished run again and enables what the run disabled.
		virtual void runEnded() = 0;
		// The window has done everything for the run.
		virtual void runSettled() = 0;
		// The delay baseline changed, so the Run Results dock and the diagram actions follow.
		virtual void delayBaselineChanged() = 0;
	};

	RunController(QMainWindow* window, SceneDocument* document, SceneSession* session, RunSelection& selection, Host& host);

	// The settings of the process that the window reads through the controller.
	bool passengerGui() const;
	// The next run starts with the passenger display on or off.
	void setPassengerGui(bool on);
	// The duration that the command line gave, which replaces the duration of the scene.
	std::optional<double> durationOverride() const;
	// A case was named when the program started or was opened by the system, so no case chooser opens.
	bool caseNamedAtStartup() const;
	void markCaseNamed();
	// The name of the case that the settings describe, and the line of the status bar that tells it is ready.
	QString settingsCaseName() const;
	QString readyMessage() const;
	// The next run writes to the folder of the scene `sceneName`, unless the user chose a folder.
	void selectSceneOutputFolder(const std::string& sceneName);

	// The run exists from the moment that it starts until its worker has finished.
	bool running() const { return !m_worker.isNull(); }
	SimulationWorker* worker() const { return m_worker.data(); }
	bool stopRequested() const;
	// Asks the run to stop. Returns whether a run existed.
	bool requestStop();
	// Pauses the run, or resumes it when it is paused. Returns whether it is paused afterwards.
	bool togglePause();
	void setDelayMs(int delayMs);

	const QString& runtimeStatus() const { return m_runtimeStatus; }
	const std::vector<SceneDiagnostic>& runtimeDiagnostics() const { return m_runtimeDiagnostics; }
	bool resultsAvailable() const { return m_resultsAvailable; }
	// The finished run as values. Null while no run has completed.
	const std::shared_ptr<const CompletedRunData>& completedRun() const { return m_completedRun; }
	// Results of a completed run exist and the simulation still holds the trajectories behind them.
	bool hasRunResults() const;
	int lastRunSelectedOccurrences() const { return m_lastRunSelectedOccurrences; }
	int lastRunTotalOccurrences() const { return m_lastRunTotalOccurrences; }
	// How many of the selected occurrences enter the simulation inside the run period.
	int lastRunSelectedInPeriodOccurrences() const { return m_lastRunSelectedInPeriodOccurrences; }
	// The clock time of second 0 of the run, in seconds since midnight.
	long long startOffsetSeconds() const { return m_startOffsetSeconds; }
	void setStartOffsetSeconds(long long seconds) { m_startOffsetSeconds = seconds; }

	const std::optional<DelayRunSnapshot>& delayBaseline() const { return m_delayBaseline; }
	DelayBaselineState delayBaselineState(bool resultsShown) const;
	// The completed run as the delay analysis takes it. It has no scenario when no run has completed.
	DelayRunSnapshot completedDelaySnapshot() const;
	// Opens the delay analysis window for the completed run and the delay baseline. Null when there is no run.
	DelayAnalysisWindow* openDelayAnalysis();
	// Forgets the delay baseline because the case was replaced, and says why.
	void clearDelayBaseline(const QString& reason);

	// The scene was edited. With a run going on, its results are discarded when it ends and this returns true. Without a
	// run it returns false, and the window discards the results now.
	bool sceneChanged();
	// The results and the status of the earlier run are forgotten.
	void discardResults();
	// The scene is about to be replaced by another.
	void sceneWillBeReplaced();

	// The facts of the run review for the scene as it is now, and what the completed run records about its input.
	QString runReviewText() const;
	RunProvenance captureRunProvenance() const;

	// Starts the run on a worker thread. The scene has to be prepared.
	void startSimulation(const telemetry::OperationObservation& operation);
	// Ends the worker and its thread, and returns the window to the state before the run. With requestStop, it asks the run
	// to stop first.
	void clearSimulationWorker(bool requestStop);
	// With a run going on: asks it to stop, shows the stopping state and keeps `action` for the moment its worker has
	// finished. The last request replaces an earlier one. Returns false without a run; the caller then acts at once.
	bool deferUntilRunStopped(std::function<void()> action);
	// The results of the run are kept or discarded, and the window follows.
	void onSimulationFinished();

public slots:
	// Runs the scene that the window holds, if it is not running already.
	void runCurrent();
	// Asks to review the run, prepares the scene, builds the canvas and starts the simulation.
	void runScene();
	void chooseOutputFolder();
	void setStartTime();
	void setDelayBaseline();
	void showDelayComparison();

private:
	// The scripted runs in tests/e2e set the state of the controller directly.
	friend class MainWindowTestAccess;

	// Asks the user to look at the run before it starts. Returns whether the run starts.
	bool showRunReview();
	void runDeferredAction();
	void showError(const QString& title, const QString& message);
	static bool hasRawRunResults();

	QMainWindow* m_window;
	SceneDocument* m_document;
	SceneSession* m_session;
	RunSelection& m_selection;
	Host& m_host;

	// The controller creates the worker and thread. Qt owns deletion through deleteLater connections.
	// QPointer nulls itself if Qt deletes either object before the controller clears the fields.
	QPointer<SimulationWorker> m_worker;
	QPointer<QThread> m_workerThread;
	// What Close, New or Open asked for while a run was active. It runs when the worker has finished.
	std::function<void()> m_afterRunAction;
	// Three product observations and two isolated cancellation probes.
	std::array<QMetaObject::Connection, 5> m_simulationObservations{};

	QString m_runtimeStatus = QStringLiteral("Not built");
	std::vector<SceneDiagnostic> m_runtimeDiagnostics;
	bool m_resultsAvailable = false;
	bool m_sceneChangedDuringRun = false;
	std::shared_ptr<const CompletedRunData> m_completedRun;
	RunProvenance m_pendingRunProvenance;
	std::optional<DelayRunSnapshot> m_delayBaseline;
	QString m_delayBaselineStatus;
	int m_lastRunSelectedOccurrences = 0;
	int m_lastRunTotalOccurrences = 0;
	int m_lastRunSelectedInPeriodOccurrences = 0;
	long long m_startOffsetSeconds;
};

#endif // RUNCONTROLLER_H
