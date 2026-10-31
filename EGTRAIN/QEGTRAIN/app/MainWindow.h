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

#include "app/EditorDocks.h"
#include "app/FollowAvailability.h"
#include "app/GuiReplayHistory.h"
#include "app/ResultViews.h"
#include "app/RunResultsDock.h"
#include "app/RunController.h"
#include "app/RunSelection.h"
#include "app/SceneSession.h"
#include "app/StartupTiming.h"
#include "diagrams/RunResults.h"
#include "graphics/NetworkPresenter.h"
#include "recovery/RecoveryStore.h"
#include "scene/SceneDiagnostic.h"
#include "widgets/InspectorPanel.h"

class AboutDialog;
class ConsoleWidget;
class ElidedLabel;
class FindDialog;
class FollowCamera;
class NetworkLegendWidget;
class NetworkScene;
class NetworkView;
class PassengerItem;
class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDockWidget;
class QDragEnterEvent;
class QDropEvent;
class QGraphicsItem;
class QLabel;
class QMenu;
class QPoint;
class QPointF;
class QProgressDialog;
class QPushButton;
class QSettings;
class QShowEvent;
class QSlider;
class QThread;
class QTimer;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QWidget;
class RecoveryController;
class SceneDocument;
class SceneHistory;
class SelfUpdater;
class SignalItem;
class SimulationWorker;
class StationNodeItem;
class StationOverlayItem;
class TelemetryConsent;
class TimeProgressBar;
class TrackLineItem;
class TrainBodyItem;
class TrainItemGroup;
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

/// The folder for crash notes, or an empty string when this run keeps none. QEGTRAIN_CRASH_DIR names a folder for any
/// run. Otherwise a scripted run and a run on the offscreen or minimal platform keep none, so that they never meet the
/// notes of a user, and a normal run keeps them in AppLocalDataLocation/Crashes. Call it once the application exists.
QString crashNoteFolder();
/// Records the scene at `path`, as `loaded` read it, as the scene that startup timing expects. Does nothing when
/// startup timing is off.
void setStartupTimingPreloadIdentity(const QString& path, const SceneLoadResult& loaded);

namespace Ui {
class MainWindow;
}

/// The main window: the network canvas, the editor and inspection docks, the menus and the toolbar. It owns the scene
/// document and the controllers that open, run and save the scene (SceneSession, RunController, ResultViews,
/// RunResultsDock) and the editor docks (EditorDocks), and serves as the Host of each of them.
class MainWindow final : public QMainWindow, private NetworkPresenter::FrameHost, private SceneSession::Host, private RunController::Host, private InspectorPanel::Host, private ResultViews::Host, private RunResultsDock::Host, private EditorDocks::Host {
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
	// The windows of the result views, and the dock that opens them.
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
	// The editor panels and their docks at the right, which the constructor builds.
	EditorDocks m_editorDocks{this, *this};
	RunSelection m_runSelection;
	StartupTimingTrials m_startupTrials;

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

	// The parts of the window that the constructor builds, in the order in which it builds them.
	void setupHelpMenu();
	void setupSettingsAction();
	void setupCentralWidget();
	void setupReplayBar();
	void setupSpeedSlider();
	void setupNetworkScene();
	void connectNetworkView();
	void connectUiActions();
	void setupFileMenu();
	void connectSceneToInspector();
	void connectSimulationControls();
	void setupWindowIcon();
	void setupToolBar();
	void setupLayersDock();
	void setupFollowControls();
	void setupToolBarActions();
	void setupToolBarButtons();
	void setupOutputFolderAction();
	void setupLogPane();
	void setupValidationDocks();
	void setupAdvancedDetailsAction();
	// The Incidents tab takes the target of a new incident, and the target that it picks, from the network view, and the
	// button of the inspector adds an incident.
	void connectIncidentTargetPick();
	void setupStatusBarFields();
	void connectViewActions();
	void setupDiagramsMenu();
	// Puts the buttons of the toolbar, the speed slider and the train list in the Tab order, each taking the focus.
	void setCommandBarTabOrder();

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
	// SceneSession::Host: what the session asks of the window. captureTelemetryOperation and dialogsSuppressed also serve
	// RunController::Host.
	telemetry::OperationObservation captureTelemetryOperation() const noexcept override;
	bool dialogsSuppressed() const override;
	void commitPendingEdits() override;
	bool afterRunStopped(std::function<void()> action) override;
	void bringToFront() override;
	void offerUpdateCheck() override;
	QString recoveredSceneSource() const override;
	SceneLoadResult readScene(const QString& path, bool& identityOk) override;
	bool sceneOpened(const SceneSession::OpenedScene& opened) override;
	// Connects the signals of the session to the slots below and to updateSceneActions.
	void connectSceneSession();
	void sceneWillBeReplaced();
	void sceneCreated(const telemetry::OperationObservation& operation);
	void sceneSaved();
	// The steps that sceneCreated and sceneOpened share: the output folder, the delay baseline, the scenario of the next
	// run, the history, the recovery source, the run results, the title, the actions and the editors follow the scene that
	// has replaced the previous one. baselineNote says why a delay baseline is gone, saved marks the history as saved, and
	// resetInfrastructureSelection clears the selection of the infrastructure editor.
	void resetForNewScene(const QString& baselineNote, bool saved, const RecoverySource& recovery, bool resetInfrastructureSelection);
	// RunController::Host: what the controller asks of the window.
	bool advancedDetailsEnabled() const override;
	bool telemetryActive() const override;
	int stepDelayMs() const override;
	// The time in seconds over which the occurrences of a service are counted.
	double serviceOccurrenceDuration() const override;
	int totalServiceOccurrences() const override;
	const SceneScenario* selectedScenario() const override;
	const std::vector<SceneIncident>& selectedScenarioIncidents() const override;
	QString scenarioContext() const override;
	void validateScene() override;
	void showLoadedData() override;
	void showValidation() override;
	void teardownForRun() override;
	void invalidateRunResults() override;
	std::vector<SceneDiagnostic> timedPreparation(
		const std::function<std::vector<SceneDiagnostic>()>& prepare, bool& identityOk) override;
	void buildRunCanvas() override;
	bool startupTimingHoldsRun(bool identityOk) override;
	bool endFollowing() override;
	bool runFinished() override;
	void replayReady(GuiReplayHistory history) override;
	// Connects the signals of the controller to the slots below and to the functions of the window that follow the run.
	// workerReady and runSettled have no slot here; the scripted runs in tests/e2e connect to them.
	void connectRunController();
	void runPreparing();
	void runStarting();
	void runPhaseChanged(RunController::RunPhase phase);
	void runPanelsChanged();
	void resultsDiscarded(bool stoppedByUser, bool sceneChangedDuringRun);
	void runEnded();
	void delayBaselineChanged();
	// EditorDocks::Host: what the editor docks ask of the window. The other functions that it names are declared above and
	// below, marked override.
	void infrastructureSelectionChanged() override;
	void renameTractionDiagrams(const QString& oldId, const QString& newId) override;
	void setStartOffset(long long seconds) override;

	void registerEditorDock(QDockWidget* dock) override;
	QMenu* createPopupMenu() override;
	void revealEditorDock(QDockWidget* dock);
	void revealEditorDock(QDockWidget* dock, const telemetry::OperationObservation& operation);
	void renderTrackPreview(const SceneModel& sceneModel);
	void updateCaseLayersPanel();
	// Shows the passengers of the case when the user chose them and the case has passengers, and enables the switch.
	void updatePassengerSwitch();
	// Shows or hides the passenger counts, icons and platform bars, and sets them for the next run.
	void applyPassengerLayer(bool shown);
	void commitPendingEditorValues() override;
	void commitPendingCaseSettings();
	void commitCaseSettings();
	// Enables the infrastructure dock and shows the panel again.
	void refreshInfrastructurePanel(bool resetSelection = false) override;
	void refreshValidationPanel() override;
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

	// opens the tractive-effort plot of a unit
	void plotTrainUnitTraction(const SceneTrainUnit& unit) override;

	// train-unit editor
	void updateTrainUnitSourceStatus() override;
	void refreshInputTractionDiagrams(const std::string& unitId) override;

	// Shows the scenarios, the incidents and the entrance delays again.
	void refreshIncidentPanel() override;
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
	bool startupTimingIdentityMatches(const QString& path, const SceneModel& model) const;
	void clearReplay();
	void showReplayBar();
	bool writeReplayText(std::optional<int> shownTime);
	void seekReplay(int requestedTime);
	void renderSnapshot(bool historical);
	bool replayTrainHasPosition(int trainIndex) const;


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
