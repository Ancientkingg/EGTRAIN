#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#define PI 3.14159265

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
#include <limits>
#include <QVBoxLayout>
#include <list>
#include <vector>
#include <map>
#include <set>
#include <utility>
#include <unordered_map>
#include <tuple>
#include <optional>
#include <QStatusBar>
#include <QSlider>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QGraphicsSimpleTextItem>
#include <QComboBox>
#include <QVariantAnimation>
#include <QPointer>
#include <QTableWidget>
#include <QListWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QIntValidator>
#include <QDoubleSpinBox>

#include "scene/SceneDiagnostic.h"
#include "scene/SceneValidator.h"

// charts
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QCategoryAxis>
QT_CHARTS_USE_NAMESPACE

class ConsoleWidget; // forward declaration for m_logPane

// custom GUI files
#include "graphics/NetworkView.h"
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

// EGTRAIN files
#include "simulation/Infrastructure.h"
#include "simulation/Signalling.h"

#include "app/DispatchController.h"
#include "app/GuiSimulationSnapshot.h"

#include "simulation/Rescheduling.h"

#include <QThread>
#include <QToolBar>

#include "simulation/SimulationWorker.h"
#include "diagrams/CapacityAnalysis.h"
#include "diagrams/RunResults.h"

// boolean to define if GUI is used or not
extern bool GUI;

using namespace std;

struct SceneSaveResult;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
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

	// painting
	void paintNode(QPointF coord, int size, int pen_width, int track, Node* Node);
	void paintStationNode(QPointF coord, int size, int pen_width, int track, Node* Node);
	void paintStationOverlay(QPointF coord, const StationVisual& visual, const string& sname);
	void paintStationPlatform(QPointF coord, int size, int pen_width, Node* Node);
	void paintTrainPassengerInfo(TrainItemGroup* trainItem);
	void paintPassengerInfoIcon(PassengerItem* paxItem);
	void paintArc(QPointF start, QPointF end, int pen_width, int track, Arc* Arc, int track_separation);
	void arcDrawing(QPointF start, QPointF end, int pen_width, int track, Arc* Arc);
	void paintConnection(QPointF start, QPointF end, int pen_width, Connections* connection);
	void paintSignal(double X, int size, int pen_width, int track, int track_separation, int sectionIndex);
	void paintTrain(const GuiTrainState& train, int size, int pen_width);
	QPointF Coord2ScreenPoint(double x, double y, double factor);
	void calculateStationCoordAndShift(int geo_scale);
	void egtrainPoint2Screen(double X, int track, double separation, double& graphX, double& graphY);
	void egtrainPoint2Screen(Node* Node, int track, double separation);
	void egtrainPoint2Screen(Connections* connections, int track1, int track2, double separation);

	// loading GIF
	void setLoadingGIF();
	void showLoadingGIF();
	void stopLoadingGIF();


	// fit view
	void fitView();

	// double to string with precision
	string to_string_precision(double value, int precision);

	// geographical functions
	void neighbourStations(double X, int tracklineID, int* stationIdx);
	bool hasTrackGeometry(int track) const;

	// setup info dock widget
	void setupInfoDockWidget();
	void setupRunResultsDock();
	void refreshRunResults();

	// update signal aspects
	void updateSignalAspect(const std::string& ID, double code, bool reversed);

	// get train polygon (list)
	void getTrainPolygonItemList(QList<TrainBodyItem*>* trainPolygonItemList, const GuiTrainState& train);
	void getTrainPolygon(QPolygonF* trainPolygon, int wagon, const GuiTrainState& train);

	// train path diagram
	void buildCorridorTrainPathDiagram(std::string corridor);
	bool hasRunResults() const;
	void updateDiagramActions();
	QMenu* editorsMenu();
	void showStartupChooser();

	// VCoupling notifications
	void checkVCouplingMsg(TrainItemGroup* trainItem, const GuiTrainState& train, int t);
	void paintVCouplingMsg(TrainItemGroup* trainItem, const std::string& message);

protected:
	void showEvent(QShowEvent* e) override;
	void closeEvent(QCloseEvent* event) override;

public slots:
	void handleHelpAbout();
	void handleCloseInfoDockWidget();
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
	void updateSignalling();
	void updatePlatforms(int t);
	void updateTrainPaxInfo();
	void updatePaxIconInfo();
	void removeTrainPaxInfoIcon();
	void removePaxInfoIcon();
	void updateBlockOccupationStatus(const GuiTrainState& train);
	void releaseBlockOccupationStatus();
	void updateTrainPosition(int t);
	void startSimulation();
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
	Ui::MainWindow* ui;

	// central widget
	QWidget* centralWidget;

	// main layout
	QVBoxLayout* mainLayout;

	// scene
	QPointer<NetworkScene> scene;

	// QGraphicsView
	NetworkView* networkView;

	// progess bar
	TimeProgressBar* progressBar;

	// loading
	QLabel* loadingLabel;
	QMovie* loadingMovie;

	// passenger icon
	QPixmap pax_pixmap;
	QPixmap pax_pixmap_scaled;

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
	QFormLayout* signallingFormLayout;
	QWidget* trainInfoWidget;
	QLineEdit* trainIDText;
	QLineEdit* trainTypeText;
	QLineEdit* trainLengthText;
	QLineEdit* trainWagonsText;
	QFormLayout* trainFormLayout;

	// effect on clicked item
	HighlightEffect* effect;

	// MainWindow creates the worker and thread. Qt owns deletion through deleteLater connections.
	// QPointer nulls itself if Qt deletes either object before MainWindow clears the fields.
	QPointer<SimulationWorker> m_worker;
	QPointer<QThread> m_workerThread;
	QSlider* m_speedSlider;
	QLabel* m_speedLabel;
	QAction* m_followAction = nullptr;
	QComboBox* m_followTrainCombo = nullptr;
	QPointer<QMenu> m_sceneContextMenu;
	int m_followTrainIndex = -1;
	bool m_updatingFollowCombo = false;
	int m_e2eAttempts = 0;
	bool m_e2eFinished = false;
	bool m_editorE2eFinished = false;
	QMap<int, QPointF> m_prevTrainPositions;
	QMap<int, QGraphicsSimpleTextItem*> m_trainSpeedLabels; // per-train speed overlay
	QMap<int, TrainBadgeItem*> m_trainBadges;
	QMap<int, QVariantAnimation*> m_trainAnimations;
	qint64 m_lastRenderMs = 0;
	std::map<std::string, std::vector<TrackLineItem*>> m_tracksBySectionId;
	std::map<std::pair<int, double>, TrackLineItem*> m_tracksByOccupiedArc;
	std::set<TrackLineItem*> m_activeTrackItems;

	// toolbar
	QToolBar* m_toolBar;
	QAction* m_openCaseAction = nullptr;
	QAction* m_newSceneAction = nullptr;

	bool m_promptedLoad = false; // ensures the load prompt only fires once

	long long m_startOffsetSeconds = 0; // simulation start, seconds since midnight
	ConsoleWidget* m_logPane = nullptr; // in-app log output
	QMenu* m_diagramsMenu = nullptr;	// Diagrams top-level menu
	QMenu* m_editorsMenu = nullptr;		// Editors top-level menu (dock toggles)
	QString m_sceneDir;
	SceneModel m_sceneModel;
	bool m_sceneLoaded = false;
	bool m_sceneIsBundle = false;
	bool m_sceneDirty = false;
	QAction* m_saveSceneAction = nullptr;
	QAction* m_saveSceneAsAction = nullptr;
	QAction* m_saveSceneAsFolderAction = nullptr;
	QAction* m_runSceneAction = nullptr;
	QMenu* m_recentScenesMenu = nullptr;
	QDockWidget* m_validationDock = nullptr;
	QTableWidget* m_validationTable = nullptr;
	QLabel* m_validationStatusLabel = nullptr;
	QDockWidget* m_loadedDataDock = nullptr;
	QTreeWidget* m_loadedDataTree = nullptr;
	QDockWidget* m_caseSettingsDock = nullptr;
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
	std::string m_selectedScenarioId;
	std::string m_appliedScenarioId;
	std::set<std::string> m_modifiedScenarioIds;
	RunResults m_completedRunResults;
	std::vector<TimetableResultRow> m_completedTimetableResults;
	std::optional<DelayRunSnapshot> m_delayBaseline;
	quint64 m_sceneRevision = 0;
	QLabel* m_runResultsSummaryLabel = nullptr;
	int m_lastRunSelectedOccurrences = 0;
	int m_lastRunTotalOccurrences = 0;

	// train-unit editor dock: physical values and piecewise traction rows
	QDockWidget* m_trainUnitDock = nullptr;
	QListWidget* m_trainUnitListWidget = nullptr;
	QLineEdit* m_trainUnitIdEdit = nullptr;
	std::array<QDoubleSpinBox*, 9> m_trainUnitPhysicalEdits{};
	QLineEdit* m_trainUnitSourceDataEdit = nullptr;
	QLineEdit* m_trainUnitSourceTractionEdit = nullptr;
	QTableWidget* m_trainUnitTractionTable = nullptr;
	QPushButton* m_addTrainUnitButton = nullptr;
	QPushButton* m_duplicateTrainUnitButton = nullptr;
	QPushButton* m_deleteTrainUnitButton = nullptr;
	QPushButton* m_addTrainUnitTractionButton = nullptr;
	QPushButton* m_removeTrainUnitTractionButton = nullptr;

	// composition editor dock (first editable scene panel)
	QDockWidget* m_compositionDock = nullptr;
	QListWidget* m_compositionListWidget = nullptr;		 // one row per SceneComposition
	QLineEdit* m_compositionIdEdit = nullptr;			 // id of the selected composition
	QListWidget* m_compositionUnitsListWidget = nullptr; // ordered unit ids of the selected composition
	QPushButton* m_addCompositionButton = nullptr;
	QPushButton* m_duplicateCompositionButton = nullptr;
	QPushButton* m_deleteCompositionButton = nullptr;
	QPushButton* m_addUnitButton = nullptr;
	QPushButton* m_removeUnitButton = nullptr;
	QPushButton* m_moveUnitUpButton = nullptr;
	QPushButton* m_moveUnitDownButton = nullptr;
	QLabel* m_compositionUnitSourceDataLabel = nullptr;		// original parameter source of the selected unit
	QLabel* m_compositionUnitSourceTractionLabel = nullptr; // original tractive-effort source of the selected unit
	QLabel* m_compositionUnitWarningLabel = nullptr;		// missing or mismatched association
	QPushButton* m_plotTractionButton = nullptr;			// open the tractive-effort plot
	QPushButton* m_plotTrainUnitTractionButton = nullptr;

	// service editor dock (service-level fields, plus the per-stop timetable editor)
	QDockWidget* m_serviceDock = nullptr;
	QListWidget* m_serviceListWidget = nullptr;		// one row per SceneService
	QLineEdit* m_serviceIdEdit = nullptr;			// id of the selected service
	QLineEdit* m_serviceOperatingCodeEdit = nullptr;
	QComboBox* m_serviceCompositionCombo = nullptr; // references a SceneComposition.id
	QComboBox* m_serviceRouteCombo = nullptr;		// references a SceneRoute.id
	QCheckBox* m_serviceThroughCheck = nullptr;
	QCheckBox* m_serviceHasEntryTimeCheck = nullptr;
	QLineEdit* m_serviceEntryTimeSecondsEdit = nullptr; // whole seconds
	QCheckBox* m_serviceHasRepeatCheck = nullptr;
	QLineEdit* m_serviceHeadwaySecondsEdit = nullptr; // whole seconds
	QCheckBox* m_serviceHasRepeatCountCheck = nullptr;
	QLineEdit* m_serviceRepeatCountEdit = nullptr;
	QDoubleSpinBox* m_servicePerformancePercentEdit = nullptr;
	QCheckBox* m_serviceHasMaximumSpeedCheck = nullptr;
	QDoubleSpinBox* m_serviceMaximumSpeedKmhEdit = nullptr;
	QCheckBox* m_serviceHasOperatingCodeStepCheck = nullptr;
	QLineEdit* m_serviceOperatingCodeStepEdit = nullptr;
	QPushButton* m_addServiceButton = nullptr;
	QPushButton* m_duplicateServiceButton = nullptr;
	QPushButton* m_deleteServiceButton = nullptr;
	QTableWidget* m_serviceOccurrenceTable = nullptr;
	QPushButton* m_selectAllOccurrencesButton = nullptr;
	QPushButton* m_selectNoneOccurrencesButton = nullptr;
	QLabel* m_serviceOccurrenceSelectionLabel = nullptr;
	bool m_updatingServiceOccurrencePreview = false;
	SceneRunSelection m_excludedSceneOccurrences;

	// stop (timetable) editor: edits the selected service's ordered stops
	QListWidget* m_stopListWidget = nullptr; // one row per SceneStop of the selected service
	QPushButton* m_addStopButton = nullptr;
	QPushButton* m_removeStopButton = nullptr;
	QPushButton* m_moveStopUpButton = nullptr;
	QPushButton* m_moveStopDownButton = nullptr;
	QComboBox* m_stopStationCombo = nullptr;  // references a SceneStation.id
	QComboBox* m_stopPlatformCombo = nullptr; // references a ScenePlatform.id of the selected station, blank allowed
	QCheckBox* m_stopHasArrivalCheck = nullptr;
	QLineEdit* m_stopArrivalSecondsEdit = nullptr; // whole seconds
	QCheckBox* m_stopHasDepartureCheck = nullptr;
	QLineEdit* m_stopDepartureSecondsEdit = nullptr; // whole seconds
	QLineEdit* m_stopDwellSecondsEdit = nullptr;	 // whole seconds, always present

	// scenario library and selected scenario's incident editor
	QDockWidget* m_incidentDock = nullptr;
	QListWidget* m_scenarioListWidget = nullptr;
	QLineEdit* m_scenarioIdEdit = nullptr;
	QLineEdit* m_scenarioNameEdit = nullptr;
	QLineEdit* m_scenarioDescriptionEdit = nullptr;
	QPushButton* m_blankScenarioButton = nullptr;
	QPushButton* m_duplicateScenarioButton = nullptr;
	QPushButton* m_importScenarioButton = nullptr;
	QPushButton* m_exportScenarioButton = nullptr;
	QListWidget* m_incidentListWidget = nullptr;	 // one row per SceneIncident
	QLineEdit* m_incidentIdEdit = nullptr;			 // id of the selected incident
	QComboBox* m_incidentTypeCombo = nullptr;		 // "signal_failure" | "train_breakdown"
	QComboBox* m_incidentTargetCombo = nullptr;		 // signal id or service id depending on type
	QLineEdit* m_incidentStartSecondsEdit = nullptr; // whole seconds
	QLineEdit* m_incidentEndSecondsEdit = nullptr;	 // whole seconds
	QCheckBox* m_incidentHasOccurrenceCheck = nullptr;
	QLineEdit* m_incidentOccurrenceEdit = nullptr;
	QCheckBox* m_incidentHasReducedSpeedCheck = nullptr;
	QDoubleSpinBox* m_incidentReducedSpeedKmhEdit = nullptr;
	QCheckBox* m_incidentHasEndSecondsCheck = nullptr;
	QCheckBox* m_incidentTerminateAtDestinationCheck = nullptr;
	QPushButton* m_addIncidentButton = nullptr;
	QPushButton* m_duplicateIncidentButton = nullptr;
	QPushButton* m_deleteIncidentButton = nullptr;

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
	bool m_stationLayerVisible = true;
	bool m_stationNamesVisible = true;
	bool m_trainLayerVisible = true;
	bool m_trainSpeedLabelsVisible = true;
	bool m_signalLayerVisible = true;
	bool m_passengerLayerVisible = true;
	QList<QGraphicsItem*> m_stationDecorations;
	QList<StationOverlayItem*> m_stationOverlays;
	QString m_selectedStationName;
	QList<QGraphicsItem*> m_signalDecorations;
	QMap<int, QGraphicsItemGroup*> m_vcMessageItems;
	NetworkLegendWidget* m_networkLegendWidget = nullptr;
	std::shared_ptr<const GuiSimulationSnapshot> m_snapshot;

	void buildPerTrainDiagram(int mode); // 0 speed/distance, 1 speed/time, 2 time/distance, 3 simulated effort/distance
	void refreshFollowTrainChoices();
	void updateSpeedModeDisplay(int value);
	void updateSceneActions();
	void showSceneContextMenu(QGraphicsItem* item, const QPointF& scenePos, const QPoint& screenPos, bool keyboard);
	void centerSceneItem(QGraphicsItem* item);
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
	bool maybeSaveScene();
	void renderTrackPreview(const SceneModel& sceneModel);
	bool finishSceneSave(const SceneSaveResult& result);
	bool saveSceneToCurrentDir();
	bool saveSceneAsToBundle();
	bool saveSceneAsToDirectory();
	bool copyScenePassthroughFiles(const QString& targetDir);
	void updateSceneWindowTitle();
	void updateCaseLayersPanel();
	void refreshCaseSettingsPanel();
	void commitPendingCaseSettings();
	void commitCaseSettings();
	void refreshInfrastructurePanel();
	void refreshInfrastructureTable();
	void refreshBlockTrackFilter();
	void refreshRouteSectionPanel();
	void commitInfrastructureCell(int row, int column);
	void addInfrastructureEntity();
	void insertBlock();
	void moveBlockUp();
	void moveBlockDown();
	void addRouteSection();
	void removeRouteSection();
	void moveRouteSectionUp();
	void moveRouteSectionDown();
	void deleteInfrastructureEntity();
	void updateInfrastructureSelection();
	std::string uniqueInfrastructureId(const std::string& baseId, const QString& facet) const;
	void refreshValidationPanel();
	void refreshLoadedDataTree();
	void activateLoadedDataItem(QTreeWidgetItem* item);
	void markSceneDirty();
	void invalidateRunResults();

	// composition editor
	void refreshCompositionPanel();
	void updateCompositionDetailPanel();
	void updateCompositionUnitButtons();
	void addComposition();
	void duplicateComposition();
	void deleteComposition();
	void commitCompositionIdEdit();
	void addUnitToComposition();
	void removeUnitFromComposition();
	void moveCompositionUnitUp();
	void moveCompositionUnitDown();
	void plotSelectedCompositionUnitTraction(); // open the tractive-effort plot for the selected unit
	void plotTrainUnitTraction(const SceneTrainUnit& unit);
	const SceneTrainUnit* trainUnitById(const std::string& id) const;
	std::string uniqueCompositionId(const std::string& baseId) const;

	// train-unit editor
	void refreshTrainUnitPanel();
	void updateTrainUnitDetailPanel();
	void refreshTrainUnitTractionTable();
	void addTrainUnit();
	void duplicateTrainUnit();
	void deleteTrainUnit();
	void commitTrainUnitIdEdit();
	void commitTrainUnitSources();
	void commitTrainUnitPhysical(int fieldIndex);
	void addTrainUnitTractionRow();
	void removeTrainUnitTractionRow();
	void commitTrainUnitTractionCell(int row, int column, double value);
	std::string uniqueTrainUnitId(const std::string& baseId) const;

	// service editor (service-level fields; stops are edited by the stop editor below)
	void refreshServicePanel();
	void updateServiceDetailPanel();
	void addService();
	void duplicateService();
	void deleteService();
	void commitServiceIdEdit();
	void commitServiceOperatingCode();
	void commitServiceComposition(const QString& text);
	void commitServiceRoute(const QString& text);
	void commitServiceThrough(bool checked);
	void commitServiceHasEntryTime(bool checked);
	void commitServiceEntryTimeSeconds();
	void commitServiceHasRepeat(bool checked);
	void commitServiceHeadwaySeconds();
	void commitServiceHasRepeatCount(bool checked);
	void commitServiceRepeatCount();
	void commitServicePerformancePercent(double value);
	void commitServiceHasMaximumSpeed(bool checked);
	void commitServiceMaximumSpeed();
	void commitServiceHasOperatingCodeStep(bool checked);
	void commitServiceOperatingCodeStep();
	void commitPendingServiceSettings();
	void refreshServiceOccurrencePreview();
	void updateServiceOccurrenceSelection(QTableWidgetItem* item);
	void selectAllServiceOccurrences();
	void selectNoneServiceOccurrences();
	double serviceOccurrenceDuration() const;
	int totalServiceOccurrences() const;
	int selectedServiceOccurrences() const;
	SceneRunSelection selectedSceneOccurrences() const;
	void pruneExcludedServiceOccurrences();
	void migrateExcludedServiceOccurrences(const std::string& oldId, const std::string& newId);
	std::string uniqueServiceId(const std::string& baseId) const;

	// stop (timetable) editor: edits the selected service's stops in place
	void refreshStopList();
	void updateStopDetailPanel();
	void refreshStopPlatformCombo();
	void addStop();
	void removeStop();
	void moveStopUp();
	void moveStopDown();
	void commitStopStation(const QString& text);
	void commitStopPlatform(const QString& text);
	void commitStopHasArrival(bool checked);
	void commitStopHasDeparture(bool checked);
	void commitStopArrivalSeconds();
	void commitStopDepartureSeconds();
	void commitStopDwellSeconds();

	// incident editor
	void refreshScenarioList();
	void refreshIncidentPanel();
	void updateScenarioDetailPanel();
	void selectScenario(int row);
	void addBlankScenario();
	void duplicateScenario();
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
	void commitIncidentOccurrence();
	void commitIncidentHasOccurrence(bool checked);
	void commitIncidentReducedSpeed();
	void commitIncidentHasReducedSpeed(bool checked);
	void commitIncidentHasEndSeconds(bool checked);
	void commitIncidentTerminateAtDestination(bool checked);
	std::string uniqueIncidentId(const std::string& baseId) const;
	std::string uniqueScenarioId(const std::string& baseId) const;
	SceneScenario* selectedScenario();
	const SceneScenario* selectedScenario() const;
	SceneIncident* selectedIncident();
	std::vector<SceneIncident>& selectedScenarioIncidents();
	const std::vector<SceneIncident>& selectedScenarioIncidents() const;
	void markScenarioModified();
	QString scenarioContext() const;
	bool showRunReview();
	void setDelayBaseline();
	void showDelayComparison();
	DelayRunSnapshot completedDelaySnapshot() const;

	void runVisualPolishE2E();
	void runStationOverlayE2E();
	void runEditorSmokeE2E();
	void runSceneRenderE2E();
	void runTrackPreviewE2E();
	void runLegacyImportE2E();
	void clearSimulationWorker(bool requestStop);
	void stopTrainAnimation(int train);
	void stopTrainAnimations();

	// list of signals
	QList<SignalItem*> allSignals;
	std::unordered_map<std::string, QList<SignalItem*>> m_signalsByAheadId;
	void buildSignalIndex();
	void buildTrackIndexes();
	void updateStationOverlayDegrees();
	void updateViewportOverlays();
	void updateZoomStatus();
	void updateTimeline(int timestep, int totalTimesteps);
	bool paxTextVisible() const;
	void updateNetworkLegend();
	bool hasRawRunResults() const;

	// list of train items (whose simulation is running)
	QList<TrainItemGroup*> allTrains;

	// list of arcs
	QList<TrackLineItem*> allArcs;

	// list of platform items
	QList<PlatformItem*> allPlatforms;

	// Non-owning scene item observers. scene->clear() deletes these items.
	QGraphicsItemGroup* trainPaxInfoItem;
	TrainItemGroup* trainPaxItem;

	// Non-owning scene item observers. scene->clear() deletes these items.
	QGraphicsItemGroup* paxIconInfoItem;
	PassengerItem* paxIconItem;

	// associates regions and station indexes
	std::vector<std::vector<int>> regionStations;

	// scene item sizes
	int global_scale;
	int node_size;
	int station_node_size;
	int station_name_graphID; // to be on the opposite side of tracks
	int line_width;
	int track_separation;
	int station_size;
	int geo_scale; // scale used to convert latitude,longitude to screen coordinates

private slots:
	void setStartTime();
	void chooseOutputFolder();
	void showSpeedDistanceDiagram();
	void showSpeedTimeDiagram();
	void showTimeDistanceDiagram();
	void showTractiveEffortDistanceDiagram();
	void showTimetableGraph();
	void showTimetableTable();
	void showDelayDiagram();
	void showBlockingTimeDiagram();
	void showCapacityAnalysis();
	void showCompressedBlockingTimeDiagram(const CapacityAnalysisResult& result, const QString& sectionLabel);
	void focusTrainInScene(const QString& trainId); // centre the network view on a diagram selection

};

#endif // MAINWINDOW_H
