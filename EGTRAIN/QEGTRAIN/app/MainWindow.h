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
class QTemporaryDir; // forward declaration for m_runStagingDir

// custom GUI files
#include "graphics/NetworkView.h"
#include "graphics/NetworkScene.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/VirtualArcItem.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/PlatformItem.h"
#include "graphics/items/IconItem.h"
#include "graphics/items/PassengerItem.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include "graphics/items/TrainBadgeItem.h"
#include "graphics/items/NetworkLegendItem.h"
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

// boolean to define if GUI is used or not
extern bool GUI;

using namespace std;

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

	// open GUI
	void openGUI();

	// track ordering
	void orderTracks();
	int allTracksAssigned();

	// painting
	void paintNode(QPointF coord, int size, int pen_width, int track, Node* Node);
	void paintStationNode(QPointF coord, int size, int pen_width, int track, Node* Node);
	void paintStationIcon(QPointF coord, const StationVisual& visual);
	void paintStationName(QPointF coord, string sname, int size);
	void paintStationPlatform(QPointF coord, int size, int pen_width, Node* Node);
	void paintTrainPassengerInfo(TrainItemGroup* trainItem);
	void paintPassengerInfoIcon(PassengerItem* paxItem);
	void paintText(QPointF coord, string sname, int size);
	void paintLine(QPointF start, QPointF end, int pen_width);
	void paintArc(QPointF start, QPointF end, int pen_width, int track, Arc* Arc, int track_separation);
	void arcDrawing(QPointF start, QPointF end, int pen_width, int track, Arc* Arc);
	void virtualArcDrawing(QPointF start, QPointF middle, QPointF end, int pen_width, int track, Arc* Arc);
	void paintConnection(QPointF start, QPointF end, int pen_width, Connections* connection);
	void paintSignal(double X, int size, int pen_width, int track, int track_separation, int sectionIndex);
	void paintTrain(const GuiTrainState& train, int size, int pen_width);
	QPointF Coord2ScreenPoint(double x, double y, double factor);
	QPointF interpolateCartesian(QPointF start, QPointF end, qreal x1, qreal x2, qreal x);
	void calculateStationCoordAndShift(int geo_scale);
	void egtrainPoint2Screen(double X, int track, double separation, double& graphX, double& graphY);
	void egtrainPoint2Screen(Node* Node, int track, double separation);
	void egtrainPoint2Screen(Connections* connections, int track1, int track2, double separation);

	// mouse wheel zoom
	void gentle_zoom(double factor);
	void set_modifiers(Qt::KeyboardModifiers modifiers);
	void set_zoom_factor_base(double value);

	// loading GIF
	void setLoadingGIF();
	void showLoadingGIF();
	void stopLoadingGIF();

	// display/hide network
	void showNetwork();
	void hideNetwork();

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
	void askForTrainPathDiagram();

	// VCoupling notifications
	void checkVCouplingMsg(TrainItemGroup* trainItem, const GuiTrainState& train, int t);
	void paintVCouplingMsg(TrainItemGroup* trainItem, const std::string& message);

	// hide objects of unused tracks
	void hideUnusedTracks();

	// draw virtual inter region connections
	void drawVirtualInterRegionConnections();

	// set graphical levels for case study from file (manually defined)
	void setGraphIDsCaseStudy();

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
	void onSimulationFinished();
	void openSceneDialog();
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

	// zoom
	Qt::KeyboardModifiers _modifiers;
	double _zoom_factor_base;
	QPointF target_scene_pos, target_viewport_pos;
	bool eventFilter(QObject* object, QEvent* event) override;

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

	bool m_promptedLoad = false; // ensures the load prompt only fires once

	long long m_startOffsetSeconds = 0; // simulation start, seconds since midnight
	ConsoleWidget* m_logPane = nullptr; // in-app log output
	QMenu* m_diagramsMenu = nullptr;	// Diagrams top-level menu
	QString m_sceneDir;
	SceneModel m_sceneModel;
	bool m_sceneLoaded = false;
	bool m_sceneDirty = false;
	QAction* m_saveSceneAction = nullptr;
	QAction* m_saveSceneAsAction = nullptr;
	QAction* m_runSceneAction = nullptr;
	QMenu* m_recentScenesMenu = nullptr;
	QDockWidget* m_validationDock = nullptr;
	QTableWidget* m_validationTable = nullptr;
	QLabel* m_validationStatusLabel = nullptr;
	QDockWidget* m_loadedDataDock = nullptr;
	QTreeWidget* m_loadedDataTree = nullptr;
	std::vector<SceneDiagnostic> m_sceneDiagnostics;

	// train-unit editor dock: physical values and piecewise traction rows
	QDockWidget* m_trainUnitDock = nullptr;
	QListWidget* m_trainUnitListWidget = nullptr;
	QLineEdit* m_trainUnitIdEdit = nullptr;
	std::array<QDoubleSpinBox*, 9> m_trainUnitPhysicalEdits{};
	QLabel* m_trainUnitSourceDataLabel = nullptr;
	QLabel* m_trainUnitSourceTractionLabel = nullptr;
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
	QLabel* m_compositionUnitSourceDataLabel = nullptr;		// LITRA source of the selected unit
	QLabel* m_compositionUnitSourceTractionLabel = nullptr; // T_LITRA source of the selected unit
	QLabel* m_compositionUnitWarningLabel = nullptr;		// missing or mismatched association
	QPushButton* m_plotTractionButton = nullptr;			// open the tractive-effort plot

	// service editor dock (service-level fields, plus the per-stop timetable editor)
	QDockWidget* m_serviceDock = nullptr;
	QListWidget* m_serviceListWidget = nullptr;		// one row per SceneService
	QLineEdit* m_serviceIdEdit = nullptr;			// id of the selected service
	QComboBox* m_serviceCompositionCombo = nullptr; // references a SceneComposition.id
	QComboBox* m_serviceRouteCombo = nullptr;		// references a SceneRoute.id
	QCheckBox* m_serviceHasEntryTimeCheck = nullptr;
	QLineEdit* m_serviceEntryTimeSecondsEdit = nullptr; // whole seconds
	QCheckBox* m_serviceHasRepeatCheck = nullptr;
	QLineEdit* m_serviceHeadwaySecondsEdit = nullptr; // whole seconds
	QPushButton* m_addServiceButton = nullptr;
	QPushButton* m_duplicateServiceButton = nullptr;
	QPushButton* m_deleteServiceButton = nullptr;

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

	// incident editor dock: edits the scene's flat list of incidents
	QDockWidget* m_incidentDock = nullptr;
	QListWidget* m_incidentListWidget = nullptr;	 // one row per SceneIncident
	QLineEdit* m_incidentIdEdit = nullptr;			 // id of the selected incident
	QComboBox* m_incidentTypeCombo = nullptr;		 // "signal_failure" | "train_breakdown"
	QComboBox* m_incidentTargetCombo = nullptr;		 // signal id or service id depending on type
	QLineEdit* m_incidentStartSecondsEdit = nullptr; // whole seconds
	QLineEdit* m_incidentEndSecondsEdit = nullptr;	 // whole seconds
	QPushButton* m_addIncidentButton = nullptr;
	QPushButton* m_duplicateIncidentButton = nullptr;
	QPushButton* m_deleteIncidentButton = nullptr;

	QTemporaryDir* m_runStagingDir = nullptr; // owned staging area for the currently running scene

	// Compact shell state. These pointers observe scene-owned items without
	// taking ownership; teardownGUI clears them after scene->clear().
	QDockWidget* m_caseLayersDock = nullptr;
	QLabel* m_caseNameLabel = nullptr;
	QLabel* m_toolbarCaseLabel = nullptr;
	QLabel* m_simulationClockLabel = nullptr;
	QCheckBox* m_stationLayerCheck = nullptr;
	QCheckBox* m_trainLayerCheck = nullptr;
	QCheckBox* m_signalLayerCheck = nullptr;
	QCheckBox* m_passengerLayerCheck = nullptr;
	bool m_stationLayerVisible = true;
	bool m_trainLayerVisible = true;
	bool m_signalLayerVisible = true;
	bool m_passengerLayerVisible = true;
	QList<QGraphicsItem*> m_stationDecorations;
	QList<QGraphicsTextItem*> m_stationNames;
	QList<QGraphicsItem*> m_signalDecorations;
	QMap<int, QGraphicsItemGroup*> m_vcMessageItems;
	NetworkLegendItem* m_networkLegend = nullptr;
	std::shared_ptr<const GuiSimulationSnapshot> m_snapshot;

	void buildPerTrainDiagram(int mode); // 0 speed/distance, 1 speed/time, 2 time/distance
	void refreshFollowTrainChoices();
	void updateSpeedModeDisplay(int value);
	void updateSceneActions();
	void addRecentScene(const QString& path);
	void rebuildRecentScenesMenu();
	bool maybeSaveScene();
	bool openSceneDirectory(const QString& dir);
	void renderTrackPreview(const QString& sceneDir);
	bool saveSceneToCurrentDir();
	bool saveSceneAsToDirectory();
	bool copyScenePassthroughFiles(const QString& targetDir);
	void updateSceneWindowTitle();
	void updateCaseLayersPanel();
	void refreshValidationPanel();
	void refreshLoadedDataTree();

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
	void commitServiceComposition(const QString& text);
	void commitServiceRoute(const QString& text);
	void commitServiceHasEntryTime(bool checked);
	void commitServiceEntryTimeSeconds();
	void commitServiceHasRepeat(bool checked);
	void commitServiceHeadwaySeconds();
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
	void refreshIncidentPanel();
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
	std::string uniqueIncidentId(const std::string& baseId) const;

	void runVisualPolishE2E();
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
	void updateViewportOverlays();
	void ensureNetworkLegend();

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

	// container to store virtual inter region connections
	std::vector<Connections> virtualInterRegionConnections;

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
	void showTimetableGraph();
	void showTimetableTable();
	void showDelayDiagram();
	void showBlockingTimeDiagram();
	void focusTrainInScene(const QString& trainId); // centre the network view on a diagram selection

signals:
	void zoomed(); // zoom
};

#endif // MAINWINDOW_H
