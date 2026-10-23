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

/// The run of a scene named by its case and scenario, for titles and messages: "case / scenario", one of the two when the
/// other is empty, or "(none)" when both are.
QString completedRunContext(const RunProvenance& provenance);

/// The run of the scene that the window holds. The controller asks the user to review the run, prepares the scene in the
/// simulation, starts the simulation on a worker thread, and stops it on request. It keeps what the run leaves: the
/// status, the run as plain values, the delay baseline, and the clock time at which the run starts. It is the one place
/// in the window that reads the settings of the process (initial_variables). What a run changes in the rest of the
/// window, such as the canvas, the panels and the results dock, is done by the window, which the controller tells
/// through its Host.
class RunController : public QObject {
	Q_OBJECT

public:
	/// What Pause, Stop and Output Folder allow.
	enum class RunPhase {
		Idle,	  ///< No worker exists.
		Running,  ///< The worker runs.
		Stopping, ///< The worker was asked to stop and has not finished.
	};

	/// What the Run Results dock shows about the delay baseline.
	struct DelayBaselineState {
		/// The results are shown and the completed run has a scenario with no incidents and no entrance delays, so it
		/// can become the baseline.
		bool canSet = false;
		/// The results are shown, a baseline exists, and the completed run has incidents or entrance delays to compare.
		bool canCompare = false;
		/// One line on the baseline: "Baseline: <run>", why it was cleared, or "No baseline set".
		QString feedback;
	};

	/// What the controller asks of the window, and the points at which it tells the window that the run changed.
	class Host {
	public:
		virtual ~Host() = default;

		/// The telemetry record of the user action that is going on. Its events are dropped when telemetry is off.
		virtual telemetry::OperationObservation captureTelemetryOperation() const noexcept = 0;
		/// A scripted run has no user to answer a dialog.
		virtual bool dialogsSuppressed() const = 0;
		/// The user asked for advanced details: the run review shows the validation counts, the incident and entrance
		/// delay configuration, and the buttons to the loaded data and the validation messages.
		virtual bool advancedDetailsEnabled() const = 0;
		/// Telemetry is on, so the run is observed.
		virtual bool telemetryActive() const = 0;
		/// The delay between two steps that the speed slider asks for, in milliseconds.
		virtual int stepDelayMs() const = 0;
		/// The time over which the occurrences of the services are counted, in seconds: the duration of the command
		/// line, of the scene, or the horizon computed from the services.
		virtual double serviceOccurrenceDuration() const = 0;
		/// How many service occurrences the scene has in all. 0 while no scene is loaded.
		virtual int totalServiceOccurrences() const = 0;
		/// How many service occurrences enter inside the period of serviceOccurrenceDuration(). 0 while no scene is
		/// loaded.
		virtual int inPeriodServiceOccurrences() const = 0;
		/// The scenario that the editors show. Null when no scene is loaded or the scene has no scenario to select.
		virtual const SceneScenario* selectedScenario() const = 0;
		/// The incidents of selectedScenario(), or an empty list without one.
		virtual const std::vector<SceneIncident>& selectedScenarioIncidents() const = 0;
		/// The name of the scenario of the latest run, else of the selected scenario, else "(none)".
		virtual QString scenarioContext() const = 0;

		/// Takes the text of the fields that are being edited into the document, then validates the scene.
		virtual void validateScene() = 0;
		/// The run review dialog sends the user to the loaded data.
		virtual void showLoadedData() = 0;
		/// The run review dialog sends the user to the validation messages.
		virtual void showValidation() = 0;
		/// The scene is about to be prepared. The window shows that and lets the events through.
		virtual void runPreparing() = 0;
		/// The scene is about to be run: the canvas shows the preview or an earlier run, and ends that.
		virtual void teardownForRun() = 0;
		/// The results and the status of the earlier run are discarded, and the window shows that.
		virtual void invalidateRunResults() = 0;
		/// Calls `prepare`, which builds the run of the scene in the simulation, and measures it when startup timing is on.
		/// `identityOk` tells whether startup timing found the scene that it expects before and after; it is true when
		/// startup timing is off.
		virtual std::vector<SceneDiagnostic> timedPreparation(
			const std::function<std::vector<SceneDiagnostic>()>& prepare, bool& identityOk) = 0;
		/// What the actions of the window enable depends on the scene or on the run, and one of them changed.
		virtual void sceneActionsChanged() = 0;
		/// The loaded data tree shows the status of the run.
		virtual void loadedDataChanged() = 0;
		/// The case layers panel shows the case name and the clock time.
		virtual void caseLayersChanged() = 0;
		/// The window builds the canvas of the prepared run and fits the view.
		virtual void buildRunCanvas() = 0;
		/// Startup timing, when it is on, takes the prepared run over: it returns true, and the run does not start. It
		/// returns false when startup timing is off.
		virtual bool startupTimingHoldsRun(bool identityOk) = 0;
		/// Sets the clock time at which the simulation starts, in seconds since midnight, and shows it in the train list.
		virtual void setStartOffset(long long seconds) = 0;

		/// The run starts: the window ends the replay of an earlier run and shows the progress.
		virtual void runStarting() = 0;
		/// The worker is set up and its thread starts next.
		virtual void workerReady() = 0;
		/// Pause and Stop mean something while a run exists, and Output Folder only while none does.
		virtual void runPhaseChanged(RunPhase phase) = 0;
		/// The editors, the infrastructure dock and the actions follow the run state.
		virtual void runPanelsChanged() = 0;
		/// The worker is gone, so the transient states of the canvas end.
		virtual void playbackEnded() = 0;
		/// Stops following a train. Returns whether a train was followed.
		virtual bool endFollowing() = 0;
		/// The run kept results, which the Run Results dock shows.
		virtual void resultsKept() = 0;
		/// The run kept no results. The Run Results dock says why and hides.
		virtual void resultsDiscarded(bool stoppedByUser, bool sceneChangedDuringRun) = 0;
		/// The run is over and its results are kept or discarded. Returns true when a scripted run has ended the run for
		/// the window, which then does nothing more.
		virtual bool runFinished() = 0;
		/// The progress of the run is no longer shown.
		virtual void runProgressEnded() = 0;
		/// The history of the finished run, which the window can replay.
		virtual void replayReady(GuiReplayHistory history) = 0;
		/// The window follows the trains of the finished run again and enables what the run disabled.
		virtual void runEnded() = 0;
		/// The window has done everything for the run.
		virtual void runSettled() = 0;
		/// The delay baseline changed, so the Run Results dock and the diagram actions follow.
		virtual void delayBaselineChanged() = 0;
	};

	/// The controller is a child of `window`. It does not own `document`, `session`, `selection` or `host`, which have
	/// to outlive it.
	RunController(QMainWindow* window, SceneDocument* document, SceneSession* session, RunSelection& selection, Host& host);

	/// The next run shows the passengers at the platforms and in the trains.
	bool passengerGui() const;
	/// The next run starts with the passenger display on or off.
	void setPassengerGui(bool on);
	/// The duration that the command line gave, in seconds, which replaces the duration of the scene. Empty without one.
	std::optional<double> durationOverride() const;
	/// A case was named when the program started or was opened by the system, so no case chooser opens.
	bool caseNamedAtStartup() const;
	/// Records that a case has been named, so that no case chooser opens.
	void markCaseNamed();
	/// The name of the case that the settings describe.
	QString settingsCaseName() const;
	/// The next run writes to the folder of the scene `sceneName`, unless the user chose a folder.
	void selectSceneOutputFolder(const std::string& sceneName);

	/// The run exists from the moment that it starts until its worker has finished.
	bool running() const { return !m_worker.isNull(); }
	/// The worker of the run. Qt deletes it when its thread has finished; null while no run exists.
	SimulationWorker* worker() const { return m_worker.data(); }
	/// A run exists and has been asked to stop.
	bool stopRequested() const;
	/// Asks the run to stop. Returns whether a run existed.
	bool requestStop();
	/// Pauses the run, or resumes it when it is paused. Returns whether it is paused afterwards.
	bool togglePause();
	/// Sets the delay between two steps of the run, in milliseconds. Does nothing without a run.
	void setDelayMs(int delayMs);

	/// The status of the run as the loaded data shows it: "Not built", "Ready", "Running", "Completed", "Stopped" or
	/// "Failed".
	const QString& runtimeStatus() const { return m_runtimeStatus; }
	/// The diagnostics with which the simulation prepared the last run.
	const std::vector<SceneDiagnostic>& runtimeDiagnostics() const { return m_runtimeDiagnostics; }
	/// The last run completed and its results are valid for the scene as it is.
	bool resultsAvailable() const { return m_resultsAvailable; }
	/// The finished run as values. Null while no run has completed.
	const std::shared_ptr<const CompletedRunData>& completedRun() const { return m_completedRun; }
	/// Results of a completed run exist and the simulation still holds the trajectories behind them.
	bool hasRunResults() const;
	/// How many service occurrences were selected for the last run.
	int lastRunSelectedOccurrences() const { return m_lastRunSelectedOccurrences; }
	/// How many service occurrences the scene had in all at the last run.
	int lastRunTotalOccurrences() const { return m_lastRunTotalOccurrences; }
	/// How many of the selected occurrences enter the simulation inside the run period.
	int lastRunSelectedInPeriodOccurrences() const { return m_lastRunSelectedInPeriodOccurrences; }
	/// The clock time of second 0 of the run, in seconds since midnight.
	long long startOffsetSeconds() const { return m_startOffsetSeconds; }
	/// Sets the clock time of second 0 of the run, in seconds since midnight.
	void setStartOffsetSeconds(long long seconds) { m_startOffsetSeconds = seconds; }

	/// The run against which Compare delays measures. Empty while none is set.
	const std::optional<DelayRunSnapshot>& delayBaseline() const { return m_delayBaseline; }
	/// What the Run Results dock shows and allows about the baseline. `resultsShown` tells whether the dock shows the
	/// results of a completed run.
	DelayBaselineState delayBaselineState(bool resultsShown) const;
	/// The completed run as the delay analysis takes it. It has no scenario when no run has completed.
	DelayRunSnapshot completedDelaySnapshot() const;
	/// Opens the delay analysis window for the completed run and the delay baseline. The window deletes itself when it
	/// closes. Null when there is no run.
	DelayAnalysisWindow* openDelayAnalysis();
	/// Forgets the delay baseline because the case was replaced, and says why.
	void clearDelayBaseline(const QString& reason);

	/// The scene was edited. With a run going on, its results are discarded when it ends and this returns true. Without a
	/// run it returns false, and the window discards the results now.
	bool sceneChanged();
	/// The results and the status of the earlier run are forgotten.
	void discardResults();
	/// The scene is about to be replaced by another.
	void sceneWillBeReplaced();

	/// The facts of the run review for the scene as it is now, as plain text with one fact per line. With `advanced` it adds
	/// the validation counts that are not zero and a line for each incident and entrance delay of the scenario.
	QString runReviewText(bool advanced) const;
	/// What the completed run records about its input: the case, the saved scene, the scenario, the settings of the
	/// process and the occurrences in the prepared run.
	RunProvenance captureRunProvenance() const;

	/// Starts the run on a worker thread. The scene has to be prepared.
	void startSimulation(const telemetry::OperationObservation& operation);
	/// Ends the worker and its thread, and returns the window to the state before the run. With requestStop, it asks the run
	/// to stop first.
	void clearSimulationWorker(bool requestStop);
	/// With a run going on: asks it to stop, shows the stopping state and keeps `action` for the moment its worker has
	/// finished. The last request replaces an earlier one. Returns false without a run; the caller then acts at once.
	bool deferUntilRunStopped(std::function<void()> action);
	/// The results of the run are kept or discarded, and the window follows.
	void onSimulationFinished();

public slots:
	/// Runs the scene that the window holds, if it is not running already.
	void runCurrent();
	/// Asks to review the run, prepares the scene, builds the canvas and starts the simulation.
	void runScene();
	/// Asks for the folder that the next runs write to. A run that is going on keeps its folder.
	void chooseOutputFolder();
	/// Makes the completed run the delay baseline. It has to have no incidents and no entrance delays.
	void setDelayBaseline();

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
	// The product observations in 0 to 2 and 5, and two isolated cancellation probes in 3 and 4.
	std::array<QMetaObject::Connection, 6> m_simulationObservations{};

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
