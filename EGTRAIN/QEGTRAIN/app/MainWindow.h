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
class QMovie;
class QPoint;
class QPointF;
class QProgressDialog;
class QPushButton;
class QSettings;
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
struct BlockingTimeDiagramSegment;
struct BlockSectionExtent;
struct CapacityAnalysisResult;
struct CompletedRunData;
struct GuiPassengerState;
struct GuiSimulationSnapshot;
struct SceneHistoryEntry;
struct StableRelease;
struct UpdateCheckResult;

using namespace std;

bool startupTimingEnabled();
void beginStartupTiming();
qint64 startupTimingNowNanoseconds();
void recordStartupTiming(const QString& phase, int iteration, int generation,
	qint64 elapsedNanoseconds, const QString& invocation = {}, const QString& source = {},
	bool identityOk = true, bool canonicalPreloadNested = false);
void setStartupTimingPreloadIdentity(const QString& path, const SceneLoadResult& loaded);

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow, private NetworkPresenter::FrameHost, private SceneSession::Host, private RunController::Host, private InspectorPanel::Host {
	Q_OBJECT

public:
	explicit MainWindow(QWidget* parent = nullptr);
	~MainWindow() override;

	// setup GUI
	void setupGUI();

	// teardown GUI (clears scene + item lists for in-place case study reload)
	void teardownGUI();

	// Load a canonical scene for the application entry point and scene chooser.
	bool openSceneDirectory(const QString& dir);

	// open GUI
	void openGUI();

	// track ordering
	void orderTracks();

	// loading GIF
	void setLoadingGIF();
	void showLoadingGIF();
	void stopLoadingGIF();


	// fit view
	void fitView();

	void setupRunResultsDock();
	void refreshRunResults();

	// train path diagram
	bool hasRunResults() const;
	void updateDiagramActions();
	QMenu* editorsMenu();
	void showStartupChooser();

protected:
	void showEvent(QShowEvent* e) override;
	void closeEvent(QCloseEvent* event) override;
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dropEvent(QDropEvent* event) override;

public slots:
	void handleHelpAbout();
	// updates according to simulation
	void waitForUpdates();
	// display train path diagrams
	void displayTrainPathDiagrams();
	void zoomIn();
	void zoomOut();
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

	// loading
	QLabel* loadingLabel;
	QMovie* loadingMovie;

	// The Simulation Info dock: the form of the selected item
	InspectorPanel* m_inspector = nullptr;
	QDockWidget* m_runResultsDock = nullptr;
	QTableWidget* m_runResultsTable = nullptr;
	QPushButton* m_setDelayBaselineButton = nullptr;
	QPushButton* m_compareDelayButton = nullptr;

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
	QAction* m_undoAction = nullptr;
	QAction* m_redoAction = nullptr;
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
	QLineEdit* m_serviceEntryTimeSecondsEdit = nullptr;
	QCheckBox* m_serviceHasRepeatCheck = nullptr;
	QLineEdit* m_serviceHeadwaySecondsEdit = nullptr;
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
	QLineEdit* m_incidentStartSecondsEdit = nullptr;
	QLineEdit* m_incidentEndSecondsEdit = nullptr;
	QCheckBox* m_incidentHasReducedSpeedCheck = nullptr;
	QDoubleSpinBox* m_incidentReducedSpeedKmhEdit = nullptr;
	QCheckBox* m_incidentHasEndSecondsCheck = nullptr;
	QCheckBox* m_incidentTerminateAtDestinationCheck = nullptr;
	QPushButton* m_addIncidentButton = nullptr;
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

	void buildPerTrainDiagram(int mode); // 0 speed/distance, 1 speed/time, 2 time/distance, 3 simulated effort/distance
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
	void offerUpdateCheck() override;
	void sceneActionsChanged() override;
	QString recoveredSceneSource() const override;
	SceneLoadResult readScene(const QString& path, bool& identityOk) override;
	void sceneWillBeReplaced() override;
	void sceneCreated(const telemetry::OperationObservation& operation) override;
	bool sceneOpened(const SceneSession::OpenedScene& opened) override;
	void sceneSaved() override;
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
	// Offers the copies that an earlier run left behind. Returns whether the user opened one.
	bool offerRecovery();
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

	// InspectorPanel::Host: what the Simulation Info dock asks of the window.
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

	// NetworkPresenter::FrameHost: the parts of a frame that the follow camera, the legend and the signal inspector take.
	bool isTrainOverlayPromoted(int trainIndex) const override;
	void trainWillUpdate(int trainIndex) override;
	bool trainPlaced(int trainIndex, const QPointF& center) override;
	void trainHidden(int trainIndex) override;
	bool trainAdded(int trainIndex, bool firstLiveTrain) override;
	void signalsApplied() override;
	void trainsApplied(bool anyAdded, bool cameraMoved) override;

	void updateViewportOverlays();
	void updateSignalCues();
	void updateZoomStatus();
	void updateTimeline(int timestep, int totalTimesteps);
	void updateNetworkLegend();

private slots:
	void showSpeedDistanceDiagram();
	void showSpeedTimeDiagram();
	void showTimeDistanceDiagram();
	void showTractiveEffortDistanceDiagram();
	void showTimetableGraph();
	void buildRouteDiagram(const std::shared_ptr<const CompletedRunData>& run, bool timetable, int referenceIndex);
	void showTimetableTable();
	void showDelayDiagram();
	void showDelayAnalysis();
	void showBlockingTimeDiagram();
	void showCapacityAnalysis();
	void showAreaCapacity();
	void showCompressedBlockingTimeDiagram(const CapacityAnalysisResult& result, const QString& sectionLabel,
		const std::shared_ptr<const CompletedRunData>& run,
		const std::vector<BlockingTimeDiagramSegment>& segments, double axisMinKm, double axisMaxKm, const QString& referenceId,
		const std::vector<BlockSectionExtent>& extents, const std::vector<BlockingTimeDiagramSegment>& unmapped);
	void focusTrainInScene(const QString& trainId); // centre the network view on a diagram selection
};

#endif // MAINWINDOW_H
