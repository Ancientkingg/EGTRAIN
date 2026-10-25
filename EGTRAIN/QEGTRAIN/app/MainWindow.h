#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#ifdef signals
#define EGTRAIN_RESTORE_SIGNALS_KEYWORD
#undef signals
#endif
#include "scene/SceneModel.h"
#ifdef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#define signals Q_SIGNALS
#undef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#endif

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <QVector>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "app/FollowAvailability.h"
#include "app/GuiReplayHistory.h"
#include "app/ResultViews.h"
#include "app/RunResultsDock.h"
#include "app/RunController.h"
#include "app/RunSelection.h"
#include "app/SceneSession.h"
#include "diagrams/RunResults.h"
#include "graphics/NetworkPresenter.h"
#include "recovery/RecoveryStore.h"
#include "scene/SceneDiagnostic.h"
#include "widgets/InspectorPanel.h"

class AboutDialog;
class CaseSettingsPanel;
class ColorChoiceButton;
class CompositionPanel;
class ConsoleWidget;
class ElidedLabel;
class EntranceDelayPanel;
class FindDialog;
class FollowCamera;
class IncidentPanel;
class InfrastructurePanel;
class NetworkLegendWidget;
class NetworkScene;
class NetworkView;
class PassengerItem;
class PassengerPanel;
class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QDragEnterEvent;
class QDropEvent;
class QGraphicsItem;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPoint;
class QPointF;
class QProgressDialog;
class QPushButton;
class QSettings;
class QShortcut;
class QShowEvent;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QThread;
class QTimer;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QWidget;
class RecoveryController;
class SceneDocument;
class SceneEditorPanel;
class SceneHistory;
class ScenarioPanel;
class SelfUpdater;
class ServicePanel;
class SignalItem;
class SimulationWorker;
class StationNodeItem;
class StationOverlayItem;
class TelemetryConsent;
class TimeProgressBar;
class TimetableStopsPanel;
class TrackLineItem;
class TrainBodyItem;
class TrainItemGroup;
class TrainUnitPanel;
class TrainUnitSourceLinker;
class UpdateChecker;
class UpdateTelemetryController;
class UserPrompts;
class ValidationPanel;
namespace telemetry {
class OperationObservation;
} // namespace telemetry
struct CompletedRunData;
struct GuiPassengerState;
struct GuiSimulationSnapshot;
struct IncidentTarget;
struct SceneHistoryEntry;
struct StableRelease;
struct UpdateCheckResult;

/// Startup timing is on: the environment variable QEGTRAIN_STARTUP_TIMING is "1".
/// The folder for crash notes, or an empty string when this run keeps none. QEGTRAIN_CRASH_DIR names a folder for any
/// run. Otherwise a scripted run and a run on the offscreen or minimal platform keep none, so that they never meet the
/// notes of a user, and a normal run keeps them in AppLocalDataLocation/Crashes. Call it once the application exists.
QString crashNoteFolder();
bool startupTimingEnabled();
/// Starts the clock of startup timing and forgets the expected scene. Does nothing when startup timing is off.
void beginStartupTiming();
/// Nanoseconds since beginStartupTiming() started the clock.
qint64 startupTimingNowNanoseconds();
/// Writes one QEGTRAIN_TIMING line of JSON to standard output when startup timing is on. `elapsedNanoseconds` is
/// written in milliseconds. `iteration` 0 is the fresh process and higher ones are warm reloads; a negative
/// `generation` and empty `invocation` and `source` are left out.
void recordStartupTiming(const QString& phase, int iteration, int generation,
	qint64 elapsedNanoseconds, const QString& invocation = {}, const QString& source = {},
	bool identityOk = true, bool canonicalPreloadNested = false);
/// Records the scene at `path`, as `loaded` read it, as the scene that startup timing expects. Does nothing when
/// startup timing is off.
void setStartupTimingPreloadIdentity(const QString& path, const SceneLoadResult& loaded);

namespace Ui {
class MainWindow;
}

/// The main window: the network canvas, the editor and inspection docks, the menus and the toolbar. It owns the scene
/// document and the controllers that open, run and save the scene (SceneSession, RunController, ResultViews,
/// RunResultsDock), and serves as the Host of each of them.
class MainWindow : public QMainWindow, private NetworkPresenter::FrameHost, private SceneSession::Host, private RunController::Host, private InspectorPanel::Host, private ResultViews::Host, private RunResultsDock::Host {
	Q_OBJECT

public:
	/// Builds the window with the scene document, its controllers, the recovery of unsaved edits and the update and
	/// telemetry actions.
	explicit MainWindow(QWidget* parent = nullptr);
	~MainWindow() override;

	/// Shows the scene of the document on the canvas, with its station overlays and the trains to follow.
	void setupGUI();

	/// Stops a running simulation and empties the canvas, the replay and the selections, ready for another scene.
	void teardownGUI();

	/// Opens the scene folder or bundle at `dir` now, for the entry point of the program and the scene chooser.
	/// Returns false, with the scene unchanged, when it cannot be opened.
	bool openSceneDirectory(const QString& dir);

	/// Shows the window maximized, at most eight ninths of the screen when restored, and fits the view.
	void openGUI();

	/// Fits the canvas to the network, or to the preview bounds while a preview shows.
	void fitView();

	/// Creates the result views and the Run Results dock.
	void setupRunResultsDock();
	/// Writes the Run Results table of the completed run and shows the dock.
	void refreshRunResults();

	/// Results of a completed run exist (RunController::hasRunResults()).
	bool hasRunResults() const;
	/// Enables the diagram entries and the Run Results dock while results exist; disabled entries tell how to get them.
	void updateDiagramActions();
	/// The Editors menu, which is created and put in the menu bar on the first call.
	QMenu* editorsMenu();
	/// Shows the case chooser with the bundled and the recent scenes, and acts on the choice.
	void showStartupChooser();

	/// The last choice of View > Passengers, which the start uses when -pax is not given.
	static bool savedShowPassengers();

protected:
	/// On the first show: offers recovery and the case chooser, then the privacy and update questions.
	void showEvent(QShowEvent* e) override;
	/// Asks about unsaved changes and stops a running simulation before the window closes.
	void closeEvent(QCloseEvent* event) override;
	/// Accepts the drag of one scene file (SceneSession::dragEnter()).
	void dragEnterEvent(QDragEnterEvent* event) override;
	/// Opens the scene file that was dropped (SceneSession::drop()).
	void dropEvent(QDropEvent* event) override;
	/// Takes the tab bars of tabbed docks and their scroll buttons out of the Tab order. The menus raise each dock, and Qt
	/// keeps the bar of a single dock outside the window, where its focus cannot be seen. The bars show whole names and scroll.
	void childEvent(QChildEvent* event) override;
	/// Moves the focus as QWidget::focusNextPrevChild() does, but passes over the docks behind the front tab of a group.
	bool focusNextPrevChild(bool next) override;

public slots:
	/// Shows the About dialog.
	void handleHelpAbout();
	/// Takes the latest snapshot of the running simulation and draws it, at most about once every 33 ms. A snapshot that
	/// arrives after the run stopped is dropped.
	void waitForUpdates();
	/// Zooms the canvas in by a factor of 1.15.
	void zoomIn();
	/// Zooms the canvas out by a factor of 1.15.
	void zoomOut();
	/// fitView() as a slot.
	void fitToView();

private:
	// the scripted GUI runs in tests/e2e
	friend class MainWindowTestAccess;
	std::unique_ptr<Ui::MainWindow> ui;

	// central widget
	QWidget* centralWidget;

	// main layout
	QVBoxLayout* mainLayout;

	// scene
	QPointer<NetworkScene> scene;

	// QGraphicsView
	NetworkView* networkView;

	// The registries of the items on the scene, and the clearing of the scene
	std::unique_ptr<NetworkPresenter> m_presenter;

	// progess bar
	TimeProgressBar* progressBar;

	// The inspector dock: the form of the selected item
	InspectorPanel* m_inspector = nullptr;
	QDockWidget* m_runResultsDock = nullptr;
	QTableWidget* m_runResultsTable = nullptr;
	QPushButton* m_setDelayBaselineButton = nullptr;
	QPushButton* m_compareDelayButton = nullptr;
	// The windows of the result views, and the dock that opens them. The pointers above are parts of the dock.
	ResultViews* m_resultViews = nullptr;
	std::unique_ptr<RunResultsDock> m_runResultsPanel;

	// Set only for the close() that follows a stopped run. The question about unsaved changes
	// was answered when the close was requested.
	bool m_closeConfirmed = false;
	QSlider* m_speedSlider;
	QLabel* m_speedLabel;
	QAction* m_followAction = nullptr;
	QComboBox* m_followTrainCombo = nullptr;
	// Says in one sentence whether the selected train is followed, and why not.
	ElidedLabel* m_followStatusLabel = nullptr;
	// Moves the view to the followed train. It belongs to the network view.
	FollowCamera* m_followCamera = nullptr;
	// What each row of the train list shows: the phase of its train and whether the displayed
	// snapshot holds the train. A row is written again only when this changes.
	QVector<std::optional<std::pair<FollowPhase, bool>>> m_followRowShown;
	QPointer<QMenu> m_sceneContextMenu;
	QPointer<AboutDialog> m_aboutDialog;
	int m_followTrainIndex = -1;
	bool m_updatingFollowCombo = false;
	qint64 m_lastRenderMs = 0;
	// Draws the newest delivered snapshot once the 33 ms render limit is over,
	// for a pause or an end of run that no later snapshot follows.
	QTimer* m_trailingRenderTimer = nullptr;
	QPointer<SimulationWorker> m_trailingRenderWorker;
	std::shared_ptr<const GuiSimulationSnapshot> m_lastRenderedSnapshot;
	bool m_playbackProfileViewApplied = false;

	// toolbar
	QToolBar* m_toolBar;
	QAction* m_openCaseAction = nullptr;
	QAction* m_newSceneAction = nullptr;
	// The messages and questions of the editors that have moved out of the window
	std::unique_ptr<UserPrompts> m_userPrompts;
	// The update checks, the self update, the privacy dialogs and the telemetry sender
	std::unique_ptr<UpdateTelemetryController> m_updateTelemetry;

	bool m_promptedLoad = false; // ensures the load prompt only fires once

	ConsoleWidget* m_logPane = nullptr; // in-app log output
	QMenu* m_diagramsMenu = nullptr;	// Diagrams top-level menu
	QHash<QDockWidget*, QAction*> m_editorActions;
	QMenu* m_editorsMenu = nullptr; // Editors top-level menu (dock toggles)
	// The scene, its unsaved state and its revision. The editors work on it through their panels.
	SceneDocument* m_document = nullptr;
	// Opens, creates and saves scenes. Holds where the scene came from and the diagnostics of its last validation.
	SceneSession* m_session = nullptr;
	// Reviews, prepares, starts and stops the run, and keeps what the run leaves.
	RunController* m_runController = nullptr;
	// Undo and Redo. The history holds the scene as it was after each edit, and sceneDocumentChanged adds to it.
	std::unique_ptr<SceneHistory> m_history;
	// True while the editors are refreshed with a restored scene. Until then they hold the scene before it, so what they
	// hold is not committed.
	bool m_restoringHistory = false;
	// Recovery copies of the scene while it has unsaved edits. Null when this run keeps none.
	std::unique_ptr<RecoveryController> m_recovery;
	// The file or folder that the scene came from, and its size and time when the window last read or wrote it.
	RecoverySource m_recoverySource;
	// The file that a recovered scene is a copy of. Save As starts there.
	QString m_recoveredSourceHint;
	bool m_recoveryFailureShown = false;
	// The message about a failed autosave while the target of an incident is picked. It is shown when the pick ends.
	QString m_heldRecoveryFailure;
	QAction* m_undoAction = nullptr;
	QAction* m_redoAction = nullptr;
	QAction* m_findAction = nullptr;
	FindDialog* m_findDialog = nullptr;
	bool m_committingPendingEditorValues = false;
	// The editor panels, in the order in which commitPendingEditorValues takes their pending edits.
	std::vector<SceneEditorPanel*> m_editorPanels;
	QAction* m_saveSceneAction = nullptr;
	QAction* m_saveSceneAsAction = nullptr;
	QAction* m_saveSceneAsFolderAction = nullptr;
	QAction* m_advancedDetailsAction = nullptr;
	QAction* m_runSceneAction = nullptr;
	QAction* m_outputFolderAction = nullptr;
	// The validation table, the loaded data tree and the validation status of the status bar belong to m_validation.
	ValidationPanel* m_validation = nullptr;
	QDockWidget* m_validationDock = nullptr;
	QDockWidget* m_loadedDataDock = nullptr;
	QDockWidget* m_caseSettingsDock = nullptr;
	CaseSettingsPanel* m_caseSettingsPanel = nullptr;
	// The fields of m_caseSettingsPanel, for the scripted runs in tests/e2e
	QLineEdit* m_caseNameEdit = nullptr;
	QLineEdit* m_caseDescriptionEdit = nullptr;
	QLineEdit* m_caseBaseTimeEdit = nullptr;
	QDoubleSpinBox* m_caseDurationSecondsEdit = nullptr;
	QDoubleSpinBox* m_caseBufferSecondsEdit = nullptr;
	QDoubleSpinBox* m_caseRecoveryPercentEdit = nullptr;
	QDockWidget* m_infrastructureDock = nullptr;
	InfrastructurePanel* m_infrastructurePanel = nullptr;
	// Parts of m_infrastructurePanel, for the scripted runs in tests/e2e and the facets that MainWindow implements
	QComboBox* m_infrastructureFacetCombo = nullptr;
	QTableWidget* m_infrastructureTable = nullptr;
	QPushButton* m_addInfrastructureButton = nullptr;
	QPushButton* m_deleteInfrastructureButton = nullptr;
	QComboBox* m_blockTrackFilterCombo = nullptr;
	QPushButton* m_insertBlockButton = nullptr;
	QPushButton* m_moveBlockUpButton = nullptr;
	QPushButton* m_moveBlockDownButton = nullptr;
	QComboBox* m_routeSectionCatalogCombo = nullptr;
	QListWidget* m_routeSectionListWidget = nullptr;
	QPushButton* m_addRouteSectionButton = nullptr;
	QPushButton* m_removeRouteSectionButton = nullptr;
	QPushButton* m_moveRouteSectionUpButton = nullptr;
	QPushButton* m_moveRouteSectionDownButton = nullptr;
	RunSelection m_runSelection;
	QLabel* m_runResultsSummaryLabel = nullptr;
	QLabel* m_delayFeedbackLabel = nullptr;
	int m_startupTimingIteration = 0;
	int m_startupTimingWarmTrials = 0;
	QString m_startupTimingScenePath;

	// train-unit editor dock: physical values and piecewise traction rows
	QDockWidget* m_trainUnitDock = nullptr;
	TrainUnitPanel* m_trainUnitPanel = nullptr;
	TrainUnitSourceLinker* m_trainUnitSourceLinker = nullptr;
	// Parts of m_trainUnitPanel, for the scripted runs in tests/e2e
	QListWidget* m_trainUnitListWidget = nullptr;
	QLineEdit* m_trainUnitIdEdit = nullptr;
	std::array<QDoubleSpinBox*, 9> m_trainUnitPhysicalEdits{};
	QLineEdit* m_trainUnitSourceDataEdit = nullptr;
	QLineEdit* m_trainUnitSourceTractionEdit = nullptr;
	QTableWidget* m_trainUnitTractionTable = nullptr;
	QPushButton* m_addTrainUnitButton = nullptr;
	QPushButton* m_addTrainUnitTractionButton = nullptr;

	// composition editor dock
	QDockWidget* m_compositionDock = nullptr;
	CompositionPanel* m_compositionPanel = nullptr;
	// Parts of m_compositionPanel, for the scripted runs in tests/e2e
	QListWidget* m_compositionListWidget = nullptr;
	QLineEdit* m_compositionIdEdit = nullptr;
	QListWidget* m_compositionUnitsListWidget = nullptr;
	QPushButton* m_addCompositionButton = nullptr;
	QPushButton* m_addUnitButton = nullptr;
	QLabel* m_compositionUnitSourceDataLabel = nullptr;
	QPushButton* m_plotTractionButton = nullptr;
	QPushButton* m_plotTrainUnitTractionButton = nullptr;

	// service editor dock (service-level fields, plus the per-stop timetable editor)
	QDockWidget* m_serviceDock = nullptr;
	ServicePanel* m_servicePanel = nullptr;
	// Parts of m_servicePanel, for the scripted runs in tests/e2e
	QListWidget* m_serviceListWidget = nullptr;
	QLineEdit* m_serviceIdEdit = nullptr;
	QLineEdit* m_serviceOperatingCodeEdit = nullptr;
	QComboBox* m_serviceCategoryCombo = nullptr;
	ColorChoiceButton* m_serviceColorButton = nullptr;
	QComboBox* m_serviceCompositionCombo = nullptr;
	QComboBox* m_serviceRouteCombo = nullptr;
	QCheckBox* m_serviceHasEntryTimeCheck = nullptr;
	QDoubleSpinBox* m_serviceEntryTimeSecondsEdit = nullptr;
	QCheckBox* m_serviceHasRepeatCheck = nullptr;
	QDoubleSpinBox* m_serviceHeadwaySecondsEdit = nullptr;
	QCheckBox* m_serviceHasRepeatCountCheck = nullptr;
	QLineEdit* m_serviceRepeatCountEdit = nullptr;
	QDoubleSpinBox* m_servicePerformancePercentEdit = nullptr;
	QCheckBox* m_serviceHasMaximumSpeedCheck = nullptr;
	QDoubleSpinBox* m_serviceMaximumSpeedKmhEdit = nullptr;
	QCheckBox* m_serviceHasOperatingCodeStepCheck = nullptr;
	QLineEdit* m_serviceOperatingCodeStepEdit = nullptr;
	QPushButton* m_addServiceButton = nullptr;
	QTableWidget* m_serviceOccurrenceTable = nullptr;
	QPushButton* m_selectNoneOccurrencesButton = nullptr;

	// stop (timetable) editor: edits the selected service's ordered stops
	TimetableStopsPanel* m_timetableStopsPanel = nullptr;
	// Parts of m_timetableStopsPanel, for the scripted runs in tests/e2e
	QTableWidget* m_stopTableWidget = nullptr;
	QPushButton* m_addStopButton = nullptr;
	QPushButton* m_insertStopAfterButton = nullptr;
	QPushButton* m_moveStopUpButton = nullptr;
	QPushButton* m_moveStopDownButton = nullptr;
	QComboBox* m_stopTimeModeCombo = nullptr;
	QLabel* m_stopTimeBaseLabel = nullptr;

	// scenario library and selected scenario's incident editor
	QDockWidget* m_incidentDock = nullptr;
	ScenarioPanel* m_scenarioPanel = nullptr;
	// Parts of m_scenarioPanel, for the scripted runs in tests/e2e
	QListWidget* m_scenarioListWidget = nullptr;
	QLineEdit* m_scenarioIdEdit = nullptr;
	QLineEdit* m_scenarioNameEdit = nullptr;
	QLineEdit* m_scenarioDescriptionEdit = nullptr;
	QPushButton* m_blankScenarioButton = nullptr;
	QPushButton* m_duplicateScenarioButton = nullptr;
	QPushButton* m_deleteScenarioButton = nullptr;
	IncidentPanel* m_incidentPanel = nullptr;
	// Parts of m_incidentPanel, for the scripted runs in tests/e2e
	QListWidget* m_incidentListWidget = nullptr;
	QLineEdit* m_incidentIdEdit = nullptr;
	QComboBox* m_incidentTypeCombo = nullptr;
	QComboBox* m_incidentTargetCombo = nullptr;
	QDoubleSpinBox* m_incidentStartSecondsEdit = nullptr;
	QDoubleSpinBox* m_incidentEndSecondsEdit = nullptr;
	QCheckBox* m_incidentHasReducedSpeedCheck = nullptr;
	QDoubleSpinBox* m_incidentReducedSpeedKmhEdit = nullptr;
	QCheckBox* m_incidentHasEndSecondsCheck = nullptr;
	QCheckBox* m_incidentTerminateAtDestinationCheck = nullptr;
	QPushButton* m_addIncidentButton = nullptr;
	// Esc while the target of an incident is picked on the network view; enabled only then.
	QShortcut* m_endTargetPickShortcut = nullptr;
	// entrance-delay editor: rows belong to the selected scenario
	EntranceDelayPanel* m_entranceDelayPanel = nullptr;
	// Parts of m_entranceDelayPanel, for the scripted runs in tests/e2e
	QListWidget* m_entranceDelayListWidget = nullptr;
	QComboBox* m_entranceDelayServiceCombo = nullptr;
	QSpinBox* m_entranceDelayOccurrenceEdit = nullptr;
	QLabel* m_entranceDelayOccurrenceContextLabel = nullptr;
	QComboBox* m_entranceDelayStationCombo = nullptr;
	QDoubleSpinBox* m_entranceDelaySecondsEdit = nullptr;
	QPushButton* m_addEntranceDelayButton = nullptr;
	QPushButton* m_duplicateEntranceDelayButton = nullptr;
	QPushButton* m_deleteEntranceDelayButton = nullptr;

	// Compact shell state. These pointers observe scene-owned items without
	// taking ownership; teardownGUI clears them after scene->clear().
	QDockWidget* m_caseLayersDock = nullptr;
	QLabel* m_caseNameLabel = nullptr;
	QLabel* m_caseReadinessLabel = nullptr;
	QLabel* m_zoomStatusLabel = nullptr;
	QCheckBox* m_stationLayerCheck = nullptr;
	QCheckBox* m_stationNamesCheck = nullptr;
	QCheckBox* m_trainLayerCheck = nullptr;
	QCheckBox* m_trainSpeedLabelsCheck = nullptr;
	QCheckBox* m_signalLayerCheck = nullptr;
	QCheckBox* m_passengerLayerCheck = nullptr;
	QAction* m_showMapKeyAction = nullptr;
	QAction* m_showPassengersAction = nullptr;
	// The passengers that the user chose to show. A case without passengers shows none and keeps the choice.
	bool m_passengerChoice = false;
	NetworkLegendWidget* m_networkLegendWidget = nullptr;
	std::shared_ptr<const GuiSimulationSnapshot> m_snapshot;
	GuiReplayHistory m_completedReplay;
	QWidget* m_replayBar = nullptr;
	QSlider* m_replaySlider = nullptr;
	QLabel* m_replayLabel = nullptr;
	QLabel* m_replayCoverageLabel = nullptr;
	QPushButton* m_replayStartButton = nullptr;
	QPushButton* m_replayPlayButton = nullptr;
	QTimer* m_replayTimer = nullptr;
	bool m_replayActive = false;
	int m_replayRequestedTime = 0;
	bool m_showingTrackPreview = true;

	void refreshFollowTrainChoices();
	// What followAvailability needs to know about the train of a row of the train list.
	FollowAvailabilityInput followInput(int row) const;
	// followAvailability for a train, or for no train when it is not in the list.
	FollowAvailability followAvailabilityOf(int trainIndex) const;
	// Writes the list entries, the status label and the Follow control from followAvailability
	// and returns its answer for the selected train.
	FollowAvailability updateFollowAvailability();
	bool trainHasGeometry(int trainIndex) const;
	void updateSpeedModeDisplay(int value);
	void updateSceneActions();
	void showSceneContextMenu(QGraphicsItem* item, const QPointF& scenePos, const QPoint& screenPos, bool keyboard);
	void centerSceneItem(QGraphicsItem* item);
	// Moves the view to the followed train at once, or stops the camera when the view does not follow it.
	void cutToFollowedTrain(const FollowAvailability& following);
	void setFollowTrain(int trainIndex);
	TrainItemGroup* resolveTrainItem(int trainIndex) const;
	TrainBodyItem* resolveTrainBodyItem(int trainIndex) const;
	StationNodeItem* resolveStationNodeItem(double nodeId, int track) const;
	TrackLineItem* resolveArcItem(double arcId, int track) const;
	SignalItem* resolveSignalItem(int track, double position, bool reversed) const;
	PassengerItem* resolvePassengerItem(const std::string& passengerId) const;
	// SceneSession::Host: what the session asks of the window, and the points at which it tells the window that the scene
	// changed. captureTelemetryOperation, dialogsSuppressed and sceneActionsChanged also serve RunController::Host.
	telemetry::OperationObservation captureTelemetryOperation() const noexcept override;
	bool dialogsSuppressed() const override;
	void commitPendingEdits() override;
	bool afterRunStopped(std::function<void()> action) override;
	void bringToFront() override;
	void offerUpdateCheck() override;
	void sceneActionsChanged() override;
	QString recoveredSceneSource() const override;
	SceneLoadResult readScene(const QString& path, bool& identityOk) override;
	void sceneWillBeReplaced() override;
	void sceneCreated(const telemetry::OperationObservation& operation) override;
	bool sceneOpened(const SceneSession::OpenedScene& opened) override;
	void sceneSaved() override;
	// The steps that sceneCreated and sceneOpened share: the output folder, the delay baseline, the scenario of the next
	// run, the history, the recovery source, the run results, the title, the actions and the editors follow the scene that
	// has replaced the previous one. baselineNote says why a delay baseline is gone, saved marks the history as saved, and
	// resetInfrastructureSelection clears the selection of the infrastructure editor.
	void resetForNewScene(const QString& baselineNote, bool saved, const RecoverySource& recovery, bool resetInfrastructureSelection);
	// RunController::Host: what the controller asks of the window, and the points at which it tells the window that the
	// run changed.
	bool advancedDetailsEnabled() const override;
	bool telemetryActive() const override;
	int stepDelayMs() const override;
	// The time in seconds over which the occurrences of a service are counted.
	double serviceOccurrenceDuration() const override;
	int totalServiceOccurrences() const override;
	int inPeriodServiceOccurrences() const override;
	const SceneScenario* selectedScenario() const override;
	const std::vector<SceneIncident>& selectedScenarioIncidents() const override;
	QString scenarioContext() const override;
	void validateScene() override;
	void showLoadedData() override;
	void showValidation() override;
	void runPreparing() override;
	void teardownForRun() override;
	void invalidateRunResults() override;
	std::vector<SceneDiagnostic> timedPreparation(
		const std::function<std::vector<SceneDiagnostic>()>& prepare, bool& identityOk) override;
	void loadedDataChanged() override;
	void caseLayersChanged() override;
	void buildRunCanvas() override;
	bool startupTimingHoldsRun(bool identityOk) override;
	void setStartOffset(long long seconds) override;
	void runStarting() override;
	void workerReady() override;
	void runPhaseChanged(RunController::RunPhase phase) override;
	void runPanelsChanged() override;
	void playbackEnded() override;
	bool endFollowing() override;
	void resultsKept() override;
	void resultsDiscarded(bool stoppedByUser, bool sceneChangedDuringRun) override;
	bool runFinished() override;
	void runProgressEnded() override;
	void replayReady(GuiReplayHistory history) override;
	void runEnded() override;
	void runSettled() override;
	void delayBaselineChanged() override;

	void registerEditorDock(QDockWidget* dock);
	QMenu* createPopupMenu() override;
	void revealEditorDock(QDockWidget* dock);
	void revealEditorDock(QDockWidget* dock, const telemetry::OperationObservation& operation);
	void renderTrackPreview(const SceneModel& sceneModel);
	void updateCaseLayersPanel();
	// Shows the passengers of the case when the user chose them and the case has passengers, and enables the switch.
	void updatePassengerSwitch();
	// Shows or hides the passenger counts, icons and platform bars, and sets them for the next run.
	void applyPassengerLayer(bool shown);
	void commitPendingEditorValues();
	void commitPendingCaseSettings();
	void commitCaseSettings();
	// Enables the infrastructure dock and shows the panel again.
	void refreshInfrastructurePanel(bool resetSelection = false);
	void refreshValidationPanel();
	// Opens the editor or the view that a row of the loaded data tree stands for.
	void openLoadedDataTarget(const QString& targetType, QTreeWidgetItem* item);
	void sceneDocumentChanged();
	void invalidateAfterSceneChange();
	void setupEditMenu();
	// Opens Find over the stations, signals and trains on the canvas.
	void showFind();
	// Centres the view on the item with this label, zooms in to read it and selects it as a click does.
	void goToFound(const QString& label);
	void undoScene();
	void redoScene();
	void stepSceneHistory(bool redo);
	void restoreSceneFromHistory(const SceneHistoryEntry& entry);
	void refreshAllEditorPanels();
	void updateUndoRedoActions();
	// Autosave and recovery. The scene changed, was saved, or was replaced by another scene.
	void startRecovery();
	void recoverySceneChanged();
	void recoverySceneSaved();
	void recoverySceneReplaced(const RecoverySource& source);
	// Offers the copies that an earlier run left behind. Returns whether the user opened one. A crash note of the last run
	// is named in the offer, or in a notice of its own when there is no copy to offer.
	bool offerRecovery(const QString& crashNote = QString());
	bool openRecoveredScene(const RecoveryCandidate& candidate);
	std::set<std::string> modifiedScenarioIds() const;

	// passenger editor: canonical passengers, journeys, and ordered legs
	QDockWidget* m_passengerDock = nullptr;
	PassengerPanel* m_passengerPanel = nullptr;
	// The fields of m_passengerPanel, for the scripted runs in tests/e2e
	QListWidget* m_passengerListWidget = nullptr;
	QLineEdit* m_passengerIdEdit = nullptr;
	QPushButton* m_addPassengerButton = nullptr;
	QTabWidget* m_passengerTabs = nullptr;
	QListWidget* m_passengerJourneyListWidget = nullptr;
	QPushButton* m_addPassengerJourneyButton = nullptr;
	QLineEdit* m_passengerJourneyIdEdit = nullptr;
	QLineEdit* m_passengerJourneyActivityEdit = nullptr;
	QComboBox* m_passengerJourneyOriginCombo = nullptr;
	QComboBox* m_passengerJourneyDestinationCombo = nullptr;
	std::array<QDoubleSpinBox*, 4> m_passengerJourneyWindowEdits{};
	QListWidget* m_passengerLegListWidget = nullptr;
	QPushButton* m_addPassengerLegButton = nullptr;
	QLineEdit* m_passengerLegIdEdit = nullptr;
	QComboBox* m_passengerLegOriginCombo = nullptr;
	QComboBox* m_passengerLegDestinationCombo = nullptr;
	QComboBox* m_passengerLegServiceCombo = nullptr;
	QSpinBox* m_passengerLegOccurrenceEdit = nullptr;

	// opens the tractive-effort plot of a unit
	void plotTrainUnitTraction(const SceneTrainUnit& unit);

	// train-unit editor
	void updateTrainUnitSourceStatus();
	void refreshInputTractionDiagrams(const std::string& unitId);

	// Shows the scenarios, the incidents and the entrance delays again.
	void refreshIncidentPanel();
	// The target that the selection on the network view gives an incident: a signal failure for a signal head, a breakdown
	// of its occurrence for a train. The type is empty when the selection gives none, and `why` then says why when it is
	// given. `selected` names what is selected, "signal", "train" or "item", for when it gives no target or the incident
	// panel does not offer it, and is empty when nothing is selected.
	IncidentTarget selectedIncidentTarget(QString& selected, QString* why = nullptr) const;
	// The target that an item of the network view gives an incident, as selectedIncidentTarget does for the selection: a
	// signal head of the track preview by its kind and id, which give none for another kind, a signal of the run, or a
	// train of the run by its index. The type is empty when the item gives none, and `why` then says why when it is given.
	IncidentTarget previewIncidentTarget(const QString& kind, const QString& id, QString* why = nullptr) const;
	IncidentTarget signalIncidentTarget(const SignalItem& signal, QString* why = nullptr) const;
	IncidentTarget trainIncidentTarget(int trainIndex, QString* why = nullptr) const;
	// Why the context menu of a signal or a train (`what`) and the inspector cannot add an incident with the target of the
	// item now, or empty when they can. `why` says why the item gives no target.
	QString viewIncidentBlocked(const IncidentTarget& target, const QString& what, const QString& why) const;
	// Shows the button of the inspector that adds an incident while it shows a signal or a train, enabled as the context
	// menu of the item is.
	void updateInspectorIncidentAction();
	// Adds an incident with the target of a signal or a train of the network view to the selected scenario, as Add
	// Incident does, and shows it on the Incidents tab with the focus on its start. Nothing happens when the incident
	// panel does not offer the target or the scene cannot be edited.
	void addIncidentFromView(const IncidentTarget& target);
	// Starts or ends picking the target of the selected incident on the network view: the cursor of the view, the hint in
	// the status bar and the Esc shortcut.
	void setIncidentTargetPick(bool picking);
	// While picking, gives the selected incident the target of what was just selected on the network view.
	void pickIncidentTarget();
	SceneScenario* selectedScenario();
	std::vector<SceneIncident>& selectedScenarioIncidents();
	void refreshRunResultsSummary();

	void handleStartupTimingPaint(const QString& kind, int generation, qint64 elapsedNanoseconds);
	bool startupTimingIdentityMatches(const QString& path, const SceneModel& model,
		const std::string* inputSnapshot = nullptr) const;
	void failStartupTiming(const QString& message);
	void clearReplay();
	void showReplayBar();
	bool writeReplayText(std::optional<int> shownTime);
	void seekReplay(int requestedTime);
	void renderSnapshot(bool historical);
	bool replayTrainHasPosition(int trainIndex) const;

	void bindStationOverlaySources();
	void updateStationOverlayDegrees();

	// InspectorPanel::Host: what the inspector dock asks of the window. passengerGui is the passenger display of the
	// prepared run; RunController::nextRunShowsPassengers is that of the next run.
	bool showingTrackPreview() const override;
	const SceneModel* loadedScene() const override;
	quint64 loadedSceneRevision() const override;
	int regionOfTrack(int track) const override;
	bool passengerGui() const override;
	GuiPassengerState passengerState(const std::string& id) const override;
	bool replayActive() const override;
	void closeSceneContextMenu() override;
	void inspectionChanged() override;
	void signalInspected() override;
	void followTrain(int trainIndex) override;

	// ResultViews::Host and RunResultsDock::Host: what the result windows and the dock ask of the window. captureTelemetryOperation,
	// dialogsSuppressed and advancedDetailsEnabled also serve the hosts above.
	const SceneModel& sceneModel() const override;
	void resultMessage(const QString& text, int milliseconds) override;
	void runResultsRefreshed() override;

	// NetworkPresenter::FrameHost: the parts of a frame that the follow camera, the legend and the signal inspector take.
	bool isTrainOverlayPromoted(int trainIndex) const override;
	void trainWillUpdate(int trainIndex) override;
	bool trainPlaced(int trainIndex, const QPointF& center) override;
	void trainMoved(int trainIndex, const QPointF& center) override;
	void trainHidden(int trainIndex) override;
	bool trainAdded(int trainIndex, bool firstLiveTrain) override;
	void signalsApplied() override;
	void trainsApplied(bool anyAdded, bool cameraMoved) override;

	// These open a result view, for the scripted runs in tests/e2e.
	void buildRouteDiagram(const std::shared_ptr<const CompletedRunData>& run, bool timetable, int referenceIndex);
	void buildPerTrainDiagram(int mode);
	void showDelayDiagram();
	void showBlockingTimeDiagram();

	void updateViewportOverlays();
	const StationOverlayItem* followedStation() const;
	bool followedStationChanged() const;
	void updateSignalCues();
	void updateZoomStatus();
	void updateTimeline(int timestep, int totalTimesteps);
	void updateNetworkLegend();

private slots:
	void focusTrainInScene(const QString& trainId); // centre the network view on a diagram selection
};

#endif // MAINWINDOW_H
