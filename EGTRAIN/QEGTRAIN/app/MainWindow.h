#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "telemetry/TelemetryOperation.h"

#ifdef signals
#define EGTRAIN_RESTORE_SIGNALS_KEYWORD
#undef signals
#endif
#include "scene/SceneModel.h"
#ifdef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#define signals Q_SIGNALS
#undef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#endif

#include <iostream>
#include <QMainWindow>
#include <QPainter>
#include <QColor>
#include <QBrush>
#include <QPixmap>
#include <QtGui>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QPen>
#include <array>
#include <optional>
#include <QGraphicsEllipseItem>
#include <QPoint>
#include <QSizePolicy>
#include <QLineF>
#include <QMessageBox>
#include <QWheelEvent>
#include <QScrollBar>
#include <QScrollArea>
#include <QMouseEvent>
#include <QObject>
#include <qmath.h>
#include <QApplication>
#include <QImage>
#include <string>
#include <QGraphicsTextItem>
#include <QLabel>
#include <QTextOption>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QTransform>
#include <cmath>
#include <QDockWidget>
#include <Qt>
#include <QLineEdit>
#include <QTreeWidget>
#include <sstream>
#include <QGraphicsPixmapItem>
#include <QGraphicsItemGroup>
#include <QList>
#include <QFont>
#include <QFormLayout>
#include <QGraphicsEffect>
#include <QTimer>
#include <QTime>
#include <algorithm>
#include <functional>
#include <limits>
#include <QVBoxLayout>
#include <list>
#include <vector>
#include "diagrams/BlockingTimeDiagram.h"
#include <map>
#include <set>
#include <utility>
#include <unordered_map>
#include <tuple>
#include <optional>
#include <memory>
#include <QStatusBar>
#include <QSlider>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QComboBox>
#include <QVariantAnimation>
#include <QPointer>
#include <QTableWidget>
#include <QListWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QIntValidator>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTabWidget>
#include <QSet>

#include "editors/SceneDocument.h"
#include "app/RunSelection.h"
#include "scene/SceneDiagnostic.h"
#include "scene/SceneValidator.h"
#include "scene/TrackPreview.h"

// charts
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QCategoryAxis>
QT_CHARTS_USE_NAMESPACE

class ConsoleWidget; // forward declaration for m_logPane
class UpdateChecker;
class SelfUpdater;
class TelemetryConsent;
class QSettings;
namespace telemetry { class TelemetrySender; }
class QProgressDialog;
class AboutDialog;
class CaseSettingsPanel;
class ColorChoiceButton;
class CompositionPanel;
class DelayAnalysisWindow;
class PassengerPanel;
class ServicePanel;
class UserPrompts;
class QPlainTextEdit;
class TimetableStopsPanel;
class TrainUnitPanel;
class TrainUnitSourceLinker;
class UserPrompts;
struct UpdateCheckResult;
struct StableRelease;

// custom GUI files
#include "graphics/NetworkView.h"
#include "graphics/FollowCamera.h"
#include "graphics/NetworkPresenter.h"
#include "graphics/NetworkScene.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/VirtualArcItem.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/PlatformItem.h"
#include "graphics/items/IconItem.h"
#include "graphics/items/PassengerItem.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include "graphics/items/TrainBadgeItem.h"
#include "widgets/NetworkLegendWidget.h"
#include "widgets/InfoDockWidget.h"
#include "graphics/items/HighlightEffect.h"
#include "widgets/TimeProgressBar.h"
#include "widgets/ElidedLabel.h"

// EGTRAIN files
#include "simulation/Infrastructure.h"
#include "simulation/Signalling.h"

#include "app/DispatchController.h"
#include "app/GuiSimulationSnapshot.h"
#include "app/GuiReplayHistory.h"
#include "app/FollowAvailability.h"

#include <QThread>
#include <QToolBar>

#include "simulation/SimulationWorker.h"
#include "diagrams/CapacityAnalysis.h"
#include "diagrams/CompletedRunData.h"
#include "diagrams/RunResults.h"

// boolean to define if GUI is used or not
extern bool GUI;

using namespace std;

struct SceneSaveResult;

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

class MainWindow : public QMainWindow, private NetworkPresenter::FrameHost {
	Q_OBJECT

public:
	explicit MainWindow(QWidget* parent = 0);
	~MainWindow();

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

	// double to string with precision
	string to_string_precision(double value, int precision);

	// setup info dock widget
	void setupInfoDockWidget();
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
	void handleCloseInfoDockWidget();
	void displayPreviewInfo(QGraphicsItem* item);
	void displayNodeInfo(NodeItem* el);
	void displayStationNodeInfo(StationNodeItem* re);
	void displayArcInfo(TrackLineItem* line);
	void displayConnectionInfo(ConnectionItem* line);
	void displaySignallingInfo(SignalItem* signal);
	void displayTrainInfo(TrainBodyItem* trainItem);
	void displayPassengerInfo(PassengerItem* paxItem);
	void handleDisableHighlight();
	// updates according to simulation
	void waitForUpdates();
	void startSimulation(const telemetry::OperationObservation& operation);
	void runCurrent();
	void onSimulationFinished();
	void newScene();
	void openSceneDialog();
	void openSceneFolderDialog();
	void saveScene();
	void saveSceneAs();
	void runScene();
	void actionLoad_Network();
	// display train path diagrams
	void displayTrainPathDiagrams();
	void zoomIn();
	void zoomOut();
	void fitToView();

private:
	// the scripted GUI runs in tests/e2e
	friend class MainWindowTestAccess;
	Ui::MainWindow* ui;

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

	// dock widget
	QWidget* infoWidget;
	QVBoxLayout* infoWidgetMainLayout;
	InfoDockWidget* infoDockWidget;
	QDockWidget* m_runResultsDock = nullptr;
	QTableWidget* m_runResultsTable = nullptr;
	QPushButton* m_setDelayBaselineButton = nullptr;
	QPushButton* m_compareDelayButton = nullptr;
	QWidget* arcInfoWidget;
	QLineEdit* arcIDText;
	QLineEdit* arcFirstNodeIDText;
	QLineEdit* arcSecondNodeIDText;
	QLineEdit* arcTrackIDText;
	QLineEdit* arcLengthText;
	QLineEdit* arcCurvatureText;
	QLineEdit* arcGradientText;
	QLineEdit* arcSpeedLimitText;
	QLineEdit* arcOperationalStateText;
	QLineEdit* arcConnectedSignalsText;
	QFormLayout* arcFormLayout;
	QWidget* nodeInfoWidget;
	QLineEdit* nodeIDText;
	QLineEdit* nodeTrackIDText;
	QLineEdit* nodeXText;
	QLineEdit* nodeYText;
	QLineEdit* nodeStationNameText;
	QLineEdit* nodeRegionText;
	QLineEdit* nodeConnectedTracksText;
	QLineEdit* nodeSignalledText;
	QFormLayout* nodeFormLayout;
	QWidget* connectionInfoWidget;
	QLineEdit* connectionFirstTrackIDText;
	QLineEdit* connectionSecondTrackIDText;
	QLineEdit* connectionXFirstNodeText;
	QLineEdit* connectionXSecondNodeText;
	QFormLayout* connectionFormLayout;
	QWidget* signallingInfoWidget;
	QLineEdit* signallingTrackIDText;
	QLineEdit* signallingXText;
	QLineEdit* signallingIDSectionAheadText;
	QLineEdit* signallingLengthSectionAheadText;
	QLineEdit* signallingAspectText;
	QLineEdit* signallingProtectedSectionText;
	QLineEdit* signallingNextTrackText;
	QPlainTextEdit* signallingGroupDetails;
	QFormLayout* signallingFormLayout;
	QWidget* trainInfoWidget;
	QLineEdit* trainIDText;
	QLineEdit* trainTypeText;
	QLineEdit* trainLengthText;
	QLineEdit* trainWagonsText;
	QFormLayout* trainFormLayout;

	// MainWindow creates the worker and thread. Qt owns deletion through deleteLater connections.
	// QPointer nulls itself if Qt deletes either object before MainWindow clears the fields.
	QPointer<SimulationWorker> m_worker;
	QPointer<QThread> m_workerThread;
	// What Close, New or Open asked for while a run was active. It runs when the worker has finished.
	std::function<void()> m_afterRunAction;
	// Set only for the close() that follows a stopped run. The question about unsaved changes
	// was answered when the close was requested.
	bool m_closeConfirmed = false;
	// Three product observations and two isolated cancellation probes.
	std::array<QMetaObject::Connection, 5> m_simulationObservations{};
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
	int m_selectedTrainIndex = -1;
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
	QAction* m_checkForUpdatesAction = nullptr;
	QAction* m_automaticUpdateChecksAction = nullptr;
	// The messages and questions of the editors that have moved out of the window
	std::unique_ptr<UserPrompts> m_userPrompts;
	std::unique_ptr<QSettings> m_telemetrySettings;
	std::unique_ptr<TelemetryConsent> m_telemetryConsent;
	std::unique_ptr<telemetry::TelemetrySender> m_telemetrySender;
	UpdateChecker* m_updateChecker = nullptr;
	SelfUpdater* m_selfUpdater = nullptr;
	QProgressDialog* m_updateProgress = nullptr;
	bool m_manualUpdateCheck = false;

	bool m_promptedLoad = false; // ensures the load prompt only fires once

	long long m_startOffsetSeconds = 0; // simulation start, seconds since midnight
	ConsoleWidget* m_logPane = nullptr; // in-app log output
	QMenu* m_diagramsMenu = nullptr;	// Diagrams top-level menu
	QHash<QDockWidget*, QAction*> m_editorActions;
	QMenu* m_editorsMenu = nullptr; // Editors top-level menu (dock toggles)
	QString m_sceneDir;
	std::string m_savedSceneSha256;
	// The scene is owned by the document. The references below let the code that has not moved to the editors read
	// and write it under its old names.
	SceneDocument* m_document = new SceneDocument(this);
	SceneModel& m_sceneModel = m_document->mutableModel();
	bool& m_sceneLoaded = m_document->m_loaded;
	bool& m_sceneDirty = m_document->m_dirty;
	quint64& m_sceneRevision = m_document->m_revision;
	bool m_sceneIsBundle = false;
	std::optional<int> m_sceneBundleVersion;
	bool m_committingPendingEditorValues = false;
	QAction* m_saveSceneAction = nullptr;
	QAction* m_saveSceneAsAction = nullptr;
	QAction* m_saveSceneAsFolderAction = nullptr;
	QAction* m_advancedDetailsAction = nullptr;
	QAction* m_runSceneAction = nullptr;
	QAction* m_outputFolderAction = nullptr;
	QMenu* m_recentScenesMenu = nullptr;
	QDockWidget* m_validationDock = nullptr;
	QTableWidget* m_validationTable = nullptr;
	QLabel* m_validationStatusLabel = nullptr;
	QDockWidget* m_loadedDataDock = nullptr;
	QTreeWidget* m_loadedDataTree = nullptr;
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
	QComboBox* m_infrastructureFacetCombo = nullptr;
	QTableWidget* m_infrastructureTable = nullptr;
	QPushButton* m_addInfrastructureButton = nullptr;
	QPushButton* m_deleteInfrastructureButton = nullptr;
	QComboBox* m_blockTrackFilterCombo = nullptr;
	QPushButton* m_insertBlockButton = nullptr;
	QPushButton* m_moveBlockUpButton = nullptr;
	QPushButton* m_moveBlockDownButton = nullptr;
	QWidget* m_routeSectionDetailWidget = nullptr;
	QComboBox* m_routeSectionCatalogCombo = nullptr;
	QListWidget* m_routeSectionListWidget = nullptr;
	QPushButton* m_addRouteSectionButton = nullptr;
	QPushButton* m_removeRouteSectionButton = nullptr;
	QPushButton* m_moveRouteSectionUpButton = nullptr;
	QPushButton* m_moveRouteSectionDownButton = nullptr;
	std::vector<int> m_blockRowModelIndices;
	QString m_infrastructureSelectionId;
	std::vector<SceneDiagnostic> m_sceneDiagnostics;
	QString m_runtimeStatus = QStringLiteral("Not built");
	std::vector<SceneDiagnostic> m_runtimeDiagnostics;
	bool m_resultsAvailable = false;
	bool m_sceneChangedDuringRun = false;
	RunSelection m_runSelection;
	// The finished run as values. The Run Results dock shows it and the result windows share it, so each window keeps
	// the run it was opened for. Null while no run has completed.
	std::shared_ptr<const CompletedRunData> m_completedRun;
	RunProvenance m_pendingRunProvenance;
	std::optional<DelayRunSnapshot> m_delayBaseline;
	QString m_delayBaselineStatus;
	QLabel* m_runResultsSummaryLabel = nullptr;
	QLabel* m_delayFeedbackLabel = nullptr;
	int m_lastRunSelectedOccurrences = 0;
	int m_lastRunTotalOccurrences = 0;
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
	QListWidget* m_scenarioListWidget = nullptr;
	QLineEdit* m_scenarioIdEdit = nullptr;
	QLineEdit* m_scenarioNameEdit = nullptr;
	QLineEdit* m_scenarioDescriptionEdit = nullptr;
	QPushButton* m_blankScenarioButton = nullptr;
	QPushButton* m_duplicateScenarioButton = nullptr;
	QPushButton* m_deleteScenarioButton = nullptr;
	QPushButton* m_importScenarioButton = nullptr;
	QPushButton* m_exportScenarioButton = nullptr;
	QListWidget* m_incidentListWidget = nullptr;	 // one row per SceneIncident
	QLineEdit* m_incidentIdEdit = nullptr;			 // id of the selected incident
	QComboBox* m_incidentTypeCombo = nullptr;		 // "signal_failure" | "train_breakdown"
	QComboBox* m_incidentTargetCombo = nullptr;		 // signal id or service id depending on type
	QLineEdit* m_incidentStartSecondsEdit = nullptr; // whole seconds
	QLineEdit* m_incidentEndSecondsEdit = nullptr;	 // whole seconds
	QCheckBox* m_incidentHasReducedSpeedCheck = nullptr;
	QDoubleSpinBox* m_incidentReducedSpeedKmhEdit = nullptr;
	QCheckBox* m_incidentHasEndSecondsCheck = nullptr;
	QCheckBox* m_incidentTerminateAtDestinationCheck = nullptr;
	QPushButton* m_addIncidentButton = nullptr;
	QPushButton* m_duplicateIncidentButton = nullptr;
	QPushButton* m_deleteIncidentButton = nullptr;
	// entrance-delay editor: rows belong to the selected scenario
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
	QString m_selectedStationName;
	QString m_previewSelectedStationId;
	bool m_hasSelectedStationIdentity = false;
	double m_selectedStationNodeId = 0.0;
	int m_selectedStationTrack = -1;
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
	// Sets the clock time at which the simulation starts and shows it in the train list.
	void setStartOffset(long long seconds);
	// Writes the list entries, the status label and the Follow control from followAvailability
	// and returns its answer for the selected train.
	FollowAvailability updateFollowAvailability();
	bool trainHasGeometry(int trainIndex) const;
	void updateSpeedModeDisplay(int value);
	void updateSceneActions();
	void setupUpdateActions();
	void showPrivacySettings(bool initialPrompt);
	void startTelemetryAfterInitialConsent();
	void maybePromptForUpdateChecks();
	void startUpdateCheck(bool manual);
	void handleUpdateCheckFinished(const UpdateCheckResult& result);
	void startSelfUpdate(const StableRelease& release);
	void handleSelfUpdateFinished(bool success, const QString& error);
	void showSceneContextMenu(QGraphicsItem* item, const QPointF& scenePos, const QPoint& screenPos, bool keyboard);
	void centerSceneItem(QGraphicsItem* item);
	// Moves the view to the followed train at once, or stops the camera when the view does not follow it.
	void cutToFollowedTrain(const FollowAvailability& following);
	void setFollowTrain(int trainIndex);
	void displayTrainDetails(TrainBodyItem* trainItem, bool changeFollowMode);
	TrainItemGroup* resolveTrainItem(int trainIndex) const;
	TrainBodyItem* resolveTrainBodyItem(int trainIndex) const;
	StationNodeItem* resolveStationNodeItem(double nodeId, int track) const;
	TrackLineItem* resolveArcItem(double arcId, int track) const;
	SignalItem* resolveSignalItem(int track, double position, bool reversed) const;
	PassengerItem* resolvePassengerItem(const std::string& passengerId) const;
	void addRecentScene(const QString& path);
	void rebuildRecentScenesMenu();
	bool requestOpenScene(const QString& path);
	bool requestOpenScene(const QString& path, const telemetry::OperationObservation& operation);
	bool openRequestedScene(const QString& path, const telemetry::OperationObservation& operation);
	void createNewScene(const telemetry::OperationObservation& operation);
	bool openSceneDirectory(const QString& path, const telemetry::OperationObservation& operation);
	void importLegacyCase(const QString& sourceDir, const QString& destinationDir,
		const telemetry::OperationObservation& operation);
	telemetry::OperationObservation captureTelemetryOperation() const noexcept;
	void registerEditorDock(QDockWidget* dock);
	QMenu* createPopupMenu() override;
	void revealEditorDock(QDockWidget* dock);
	void revealEditorDock(QDockWidget* dock, const telemetry::OperationObservation& operation);
	bool maybeSaveScene();
	void renderTrackPreview(const SceneModel& sceneModel);
	void clearPreviewInspection();
	QGraphicsItem* resolvePreviewItem(const QString& kind, const QString& id,
		const QString& track, bool reversed, quint64 revision) const;
	bool finishSceneSave(const SceneSaveResult& result, const telemetry::OperationObservation& operation,
		std::optional<telemetry::ExportKind> kind);
	bool saveSceneToCurrentDir();
	bool saveSceneAsToBundle();
	bool saveSceneAsToBundle(const telemetry::OperationObservation& operation);
	bool saveSceneAsToDirectory();
	bool copyScenePassthroughFiles(const QString& targetDir, const telemetry::OperationObservation& operation);
	void updateSceneWindowTitle();
	void updateCaseLayersPanel();
	void commitPendingEditorValues();
	void commitPendingCaseSettings();
	void commitCaseSettings();
	void refreshInfrastructurePanel(bool resetSelection = false);
	void refreshInfrastructureTable(bool resetSelection = false);
	void refreshBlockTrackFilter();
	void refreshRouteSectionPanel();
	void commitInfrastructureCell(int row, int column);
	void commitPlatformGeometryCell(int row, int column, double value);
	void addInfrastructureEntity();
	void insertBlock();
	void moveBlock(int offset);
	void addRouteSection();
	void removeRouteSection();
	void moveRouteSectionUp();
	void moveRouteSectionDown();
	void deleteInfrastructureEntity();
	void updateInfrastructureSelection();
	void refreshValidationPanel();
	void refreshLoadedDataTree();
	void updateScenarioPresentation();
	bool advancedDetailsEnabled() const;
	void updateDiagnosticPresentation();
	void activateLoadedDataItem(QTreeWidgetItem* item);
	void markSceneDirty();
	void sceneDocumentChanged();
	void invalidateRunResults();

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

	// The time in seconds over which the occurrences of a service are counted.
	double serviceOccurrenceDuration() const;
	int totalServiceOccurrences() const;
	int inPeriodServiceOccurrences() const;

	// incident editor
	void refreshScenarioList();
	void refreshIncidentPanel();
	void updateScenarioDetailPanel();
	void selectScenario(int row);
	void addBlankScenario();
	void duplicateScenario();
	void deleteScenario();
	void importScenario();
	void exportScenario();
	void commitScenarioIdEdit();
	void commitScenarioNameEdit();
	void commitScenarioDescriptionEdit();
	void updateIncidentDetailPanel();
	void refreshIncidentTargetCombo();
	void addIncident();
	void duplicateIncident();
	void deleteIncident();
	void commitIncidentIdEdit();
	void commitIncidentType(const QString& text);
	void commitIncidentTarget(const QString& text);
	void commitIncidentStartSeconds();
	void commitIncidentEndSeconds();
	void commitIncidentReducedSpeed();
	void commitIncidentHasReducedSpeed(bool checked);
	void commitIncidentHasEndSeconds(bool checked);
	void commitIncidentTerminateAtDestination(bool checked);
	void refreshEntranceDelayPanel();
	void updateEntranceDelayDetailPanel();
	void addEntranceDelay();
	void duplicateEntranceDelay();
	void deleteEntranceDelay();
	void commitEntranceDelayService(int index);
	void commitEntranceDelayOccurrence();
	void commitEntranceDelayStation(int index);
	void commitEntranceDelaySeconds();
	SceneEntranceDelay* selectedEntranceDelay();
	SceneScenario* selectedScenario();
	const SceneScenario* selectedScenario() const;
	SceneIncident* selectedIncident();
	std::vector<SceneIncident>& selectedScenarioIncidents();
	const std::vector<SceneIncident>& selectedScenarioIncidents() const;
	void markScenarioModified();
	QString scenarioContext() const;
	QString runReviewText() const;
	bool showRunReview();
	void setDelayBaseline();
	// Opens the delay analysis window for the completed run and the delay baseline. Null when there is no run.
	DelayAnalysisWindow* openDelayAnalysis();
	// The same, on the tab that compares the run with the baseline.
	void showDelayComparison();
	void refreshRunResultsSummary();
	DelayRunSnapshot completedDelaySnapshot() const;
	RunProvenance captureRunProvenance() const;

	void handleStartupTimingPaint(const QString& kind, int generation, qint64 elapsedNanoseconds);
	bool startupTimingIdentityMatches(const QString& path, const SceneModel& model,
		const std::string* inputSnapshot = nullptr) const;
	void failStartupTiming(const QString& message);
	void clearSimulationWorker(bool requestStop);
	bool deferUntilRunStopped(std::function<void()> action);
	void runDeferredAction();
	void clearReplay();
	void showReplayBar();
	bool writeReplayText(std::optional<int> shownTime);
	void seekReplay(int requestedTime);
	void renderSnapshot(bool historical);
	bool replayTrainHasPosition(int trainIndex) const;

	void bindStationOverlaySources();
	void updateStationOverlayDegrees();
	GuiPassengerState passengerState(const std::string& id) const;

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
	bool hasRawRunResults() const;

private slots:
	void setStartTime();
	void chooseOutputFolder();
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
		const std::shared_ptr<const CompletedRunData>& run, std::vector<BlockingTimeDiagramSegment> segments,
		double axisMinKm, double axisMaxKm, const QString& referenceId,
		std::vector<BlockSectionExtent> extents, std::vector<BlockingTimeDiagramSegment> unmapped);
	void focusTrainInScene(const QString& trainId); // centre the network view on a diagram selection
};

#endif // MAINWINDOW_H
