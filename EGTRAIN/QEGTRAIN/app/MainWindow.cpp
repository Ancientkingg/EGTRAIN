#include "app/MainWindow.h"
#include "ui_MainWindow.h"
#include <QTableWidget>
#include <QHeaderView>
#include "util/TimeFormat.h"
#include "util/SpeedFormat.h"
#include "widgets/ConsoleWidget.h"
#include "diagrams/DiagramWindow.h"
#include "util/TrajectoryUtil.h"
#include "diagrams/BlockingTimeDiagram.h"
#include "graphics/VisualPolish.h"
#include "scene/SceneWriter.h"
#include "scene/SceneExporter.h"
#include "io/geocoding.h"
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLegendMarker>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include "io/InputValidation.h"
#include <cfloat>
#include <QThread>
#include <QCloseEvent>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QCoreApplication>
#include <QSignalBlocker>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <cstdio>
#include <map>

// utilization of GUI
// bool GUI = true; // GUI used by default

namespace {
const char* kRecentScenesKey = "recentScenes";
const int kMaxRecentScenes = 8;

void addLoadedDataTreeItem(QTreeWidget* tree, QTreeWidgetItem* parent, const SceneLoadedData& item) {
	auto* row = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
	row->setText(0, QString::fromStdString(item.category));
	row->setText(1, QString::fromStdString(item.sourceFile));
	row->setText(2, QString::number(item.parsedCount));
	row->setText(3, QString::fromStdString(item.status));
	for (const auto& child : item.children)
		addLoadedDataTreeItem(tree, row, child);
}

bool e2eDialogsSuppressed() {
	return qEnvironmentVariableIsSet("QEGTRAIN_E2E_VISUAL_POLISH") || qEnvironmentVariableIsSet("QEGTRAIN_E2E_SCENE_RUN") || qEnvironmentVariableIsSet("QEGTRAIN_E2E_EDITOR_SMOKE");
}

// modal boxes deadlock the env-gated smoke runs, which have no user to
// dismiss them; route the message to stderr instead
void showBlockingError(QWidget* parent, const QString& title, const QString& message, bool warningIcon = false) {
	if (e2eDialogsSuppressed()) {
		std::fprintf(stderr, "E2E_DIALOG_SUPPRESSED: %s: %s\n", title.toStdString().c_str(), message.toStdString().c_str());
		std::fflush(stderr);
		return;
	}
	if (warningIcon)
		QMessageBox::warning(parent, title, message);
	else
		QMessageBox::critical(parent, title, message);
}

QString firstDiagnosticMessage(const std::vector<SceneDiagnostic>& diagnostics) {
	for (const auto& d : diagnostics) {
		if (d.severity == SceneSeverity::Error)
			return QString::fromStdString(d.message);
	}
	if (!diagnostics.empty())
		return QString::fromStdString(diagnostics.front().message);
	return QString();
}

int errorDiagnosticCount(const std::vector<SceneDiagnostic>& diagnostics) {
	int count = 0;
	for (const auto& d : diagnostics) {
		if (d.severity == SceneSeverity::Error)
			++count;
	}
	return count;
}

int baseTimeToSeconds(const std::string& hhmmss) {
	if (hhmmss.empty())
		return 0;
	int h = 0, m = 0, s = 0;
	char extra = 0;
	if (std::sscanf(hhmmss.c_str(), "%d:%d:%d%c", &h, &m, &s, &extra) != 3)
		return 0;
	if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59)
		return 0;
	return h * 3600 + m * 60 + s;
}

double computeHorizon(const SceneModel& scene) {
	double maxSeconds = 0.0;
	for (const auto& service : scene.services) {
		if (service.entryTimeSeconds > maxSeconds)
			maxSeconds = service.entryTimeSeconds;
		for (const auto& stop : service.stops) {
			if (stop.hasArrival && stop.arrivalSeconds > maxSeconds)
				maxSeconds = stop.arrivalSeconds;
			if (stop.hasDeparture && stop.departureSeconds > maxSeconds)
				maxSeconds = stop.departureSeconds;
		}
	}
	double horizon = maxSeconds + 600.0;
	return horizon < 600.0 ? 600.0 : horizon;
}

int countTrackLineDirs(const QString& inputDir) {
	QDir trackLinesDir(inputDir + "/TrackLines");
	if (!trackLinesDir.exists())
		return 0;
	QFileInfoList entries = trackLinesDir.entryInfoList(QStringList("B*"), QDir::Dirs | QDir::NoDotAndDotDot | QDir::CaseSensitive);
	return entries.size();
}

QString stopRowLabel(const SceneStop& stop) {
	QString label = QString::fromStdString(stop.stationId);
	if (!stop.platformId.empty())
		label += QString(" @ %1").arg(QString::fromStdString(stop.platformId));
	return label;
}

bool copyDirectoryRecursively(const QString& sourcePath, const QString& targetPath) {
	QDir sourceDir(sourcePath);
	if (!sourceDir.exists())
		return false;

	QDir targetDir(targetPath);
	if (!targetDir.exists() && !QDir().mkpath(targetPath))
		return false;

	const QFileInfoList entries = sourceDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
	for (const QFileInfo& entry : entries) {
		const QString sourceFilePath = entry.absoluteFilePath();
		const QString targetFilePath = targetDir.filePath(entry.fileName());
		if (entry.isDir()) {
			if (!copyDirectoryRecursively(sourceFilePath, targetFilePath))
				return false;
		} else {
			QFile::remove(targetFilePath);
			if (!QFile::copy(sourceFilePath, targetFilePath))
				return false;
		}
	}
	return true;
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent),
										  ui(new Ui::MainWindow),
										  m_worker(nullptr),
										  m_workerThread(nullptr) {
	ui->setupUi(this);

	cout << "\n...PREPARING GUI...\n\n";

	// scene item sizes
	global_scale = 10;
	node_size = (int)2.8 * global_scale;
	station_node_size = (int)2.5 * node_size;
	station_name_graphID = -8; // to be on the opposite side of tracks
	line_width = 2;
	track_separation = 15 * global_scale;
	station_size = 30 * global_scale;
	geo_scale = 80000 * global_scale; // scale used to convert latitude,longitude to screen coordinates

	// initialize trainPaxInfoItem
	trainPaxInfoItem = nullptr;

	// initialize paxIconInfoItem
	paxIconInfoItem = nullptr;

	// define central widget
	centralWidget = new QWidget();

	// set network view
	networkView = new NetworkView(centralWidget);

	// set progress bar
	progressBar = new TimeProgressBar(centralWidget);

	// set main layout
	mainLayout = new QVBoxLayout();
	mainLayout->addWidget(networkView);
	mainLayout->addWidget(progressBar);

	// set layout in QWidget
	centralWidget->setLayout(mainLayout);

	// set as central layout of the main window
	setCentralWidget(centralWidget);

	// hide progress bar
	progressBar->hide();
	statusBar()->showMessage("Simulation complete");

	// setup speed slider (will be added to toolbar)
	m_speedSlider = new QSlider(Qt::Horizontal);
	m_speedSlider->setRange(0, 500);
	m_speedSlider->setValue(0);
	m_speedSlider->setMaximumWidth(200);
	m_speedSlider->setToolTip("Simulation speed: fastest compressed mode");
	m_speedLabel = new QLabel(simulationSpeedLabel(m_speedSlider->value()));

	// setup info dock widget
	setupInfoDockWidget();

	// load image for station icon
	station_pixmap = QPixmap("train_station.png");

	// load image for passenger icon
	pax_pixmap = QPixmap("pax_icon.png");
	pax_pixmap_scaled = pax_pixmap.scaled(QSize(station_size / 10, station_size / 10), Qt::KeepAspectRatio, Qt::SmoothTransformation);

	// create container of items to show on display area
	scene = new NetworkScene(networkView);
	networkView->setScene(scene);

	// zoom
	networkView->viewport()->installEventFilter(this);
	networkView->setMouseTracking(true);
	_modifiers = Qt::NoModifier;
	_zoom_factor_base = 1.0015;
	set_modifiers(Qt::NoModifier); // use mouse wheel to zoom in/out

	// connect elements from UI
	connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::handleHelpAbout);
	connect(ui->actionSimulationStart, &QAction::triggered, this, &MainWindow::startSimulation);
	connect(ui->actionSimulationStop, &QAction::triggered, this, [this]() {
		if (m_worker)
			m_worker->requestStop();
	});
	connect(infoDockWidget, &InfoDockWidget::closed, this, &MainWindow::handleCloseInfoDockWidget);

	connect(ui->displayTrainPathDiagrams, &QAction::triggered, this, &MainWindow::displayTrainPathDiagrams);

	connect(ui->actionLoad_Network, &QAction::triggered, this, &MainWindow::actionLoad_Network);

	ui->actionLoad_Network->setText("Load Legacy Case...");
	ui->actionLoad_Network->setShortcut(QKeySequence());
	QAction* openSceneAction = new QAction("Open Scene...", this);
	openSceneAction->setShortcut(QKeySequence::Open);
	m_saveSceneAction = new QAction("Save Scene", this);
	m_saveSceneAction->setShortcut(QKeySequence::Save);
	m_saveSceneAsAction = new QAction("Save Scene As...", this);
	m_runSceneAction = new QAction("Run Scene", this);
	m_recentScenesMenu = new QMenu("Recent Scenes", this);
	connect(openSceneAction, &QAction::triggered, this, &MainWindow::openSceneDialog);
	connect(m_saveSceneAction, &QAction::triggered, this, &MainWindow::saveScene);
	connect(m_saveSceneAsAction, &QAction::triggered, this, &MainWindow::saveSceneAs);
	connect(m_runSceneAction, &QAction::triggered, this, &MainWindow::runScene);
	if (ui->menuFile) {
		QAction* beforeAction = ui->actionLoad_Network;
		ui->menuFile->insertAction(beforeAction, openSceneAction);
		ui->menuFile->insertAction(beforeAction, m_saveSceneAction);
		ui->menuFile->insertAction(beforeAction, m_saveSceneAsAction);
		ui->menuFile->insertAction(beforeAction, m_runSceneAction);
		ui->menuFile->insertMenu(beforeAction, m_recentScenesMenu);
		ui->menuFile->insertSeparator(beforeAction);
	}
	rebuildRecentScenesMenu();
	updateSceneActions();

	// connect scene signals
	connect(scene, &NetworkScene::MousePressedOnNode, this, &MainWindow::displayNodeInfo);
	connect(scene, &NetworkScene::MousePressedOnStationNode, this, &MainWindow::displayStationNodeInfo);
	connect(scene, &NetworkScene::MousePressedOnArc, this, &MainWindow::displayArcInfo);
	connect(scene, &NetworkScene::MousePressedOnConnection, this, &MainWindow::displayConnectionInfo);
	connect(scene, &NetworkScene::MousePressedOnSignal, this, &MainWindow::displaySignallingInfo);
	connect(scene, &NetworkScene::MousePressedOnTrain, this, &MainWindow::displayTrainInfo);
	connect(scene, &NetworkScene::MousePressedOnPassenger, this, &MainWindow::displayPassengerInfo);
	connect(scene, &NetworkScene::DisableHighlight, this, &MainWindow::handleDisableHighlight);

	// connect signals from EGTRAIN simulation
	connect(&simulation, &DispatchController::iterationFinished, this, &MainWindow::waitForUpdates);

	// simulation control connections
	connect(ui->actionSimulationPause, &QAction::triggered, this, [this]() {
		if (!m_worker) {
			ui->actionSimulationPause->setChecked(false);
			return;
		}
		if (m_worker->isPauseRequested()) {
			m_worker->requestResume();
			ui->actionSimulationPause->setText("Pause");
		} else {
			m_worker->requestPause();
			ui->actionSimulationPause->setText("Resume");
		}
	});
	connect(m_speedSlider, &QSlider::valueChanged, this, [this](int value) {
		updateSpeedModeDisplay(value);
		if (m_worker)
			m_worker->setDelayMs(value);
	});

	// set application item
	QIcon window_icon = QIcon(QPixmap("window_icon.jpg"));
	setWindowIcon(window_icon);

	// effect on (last) clicked item
	effect = nullptr;

	// start GUI
	setupGUI();

	// --- Build toolbar ---
	m_toolBar = ui->mainToolBar;
	// Speed slider in toolbar (between sim controls and zoom)
	m_toolBar->insertSeparator(ui->actionSimulationStop);
	m_toolBar->insertWidget(ui->actionSimulationStop, m_speedLabel);
	m_toolBar->insertWidget(ui->actionSimulationStop, m_speedSlider);

	m_followTrainCombo = new QComboBox(this);
	m_followTrainCombo->setMinimumWidth(180);
	m_followTrainCombo->setMaximumWidth(260);
	m_followTrainCombo->setToolTip("Select a train to keep centered while the simulation runs");
	m_followAction = new QAction("Follow", this);
	m_followAction->setCheckable(true);
	m_followAction->setToolTip("Center the network view on the selected train");
	connect(m_followTrainCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
		if (m_updatingFollowCombo || index < 0)
			return;
		m_followTrainIndex = m_followTrainCombo->itemData(index).toInt();
	});
	connect(m_followAction, &QAction::toggled, this, [this](bool checked) {
		if (!checked) {
			m_followTrainIndex = -1;
			return;
		}
		if (m_followTrainCombo && m_followTrainCombo->currentIndex() >= 0)
			m_followTrainIndex = m_followTrainCombo->currentData().toInt();
	});
	m_toolBar->insertAction(ui->actionSimulationStop, m_followAction);
	m_toolBar->insertWidget(ui->actionSimulationStop, m_followTrainCombo);
	refreshFollowTrainChoices();

	// start time control (clock time the simulation begins at)
	QAction* startTimeAction = new QAction("Start Time", this);
	startTimeAction->setToolTip("Set the clock time the simulation starts at (HH:MM)");
	connect(startTimeAction, &QAction::triggered, this, &MainWindow::setStartTime);
	m_toolBar->insertAction(ui->actionSimulationStop, startTimeAction);

	// file menu: output folder chooser
	if (ui->menuFile) {
		QAction* outAction = new QAction("Set Output Folder...", this);
		connect(outAction, &QAction::triggered, this, &MainWindow::chooseOutputFolder);
		ui->menuFile->addAction(outAction);
	}

	// in-app logging pane (ConsoleWidget installs its own streambuf on construction)
	m_logPane = new ConsoleWidget(this);
	addDockWidget(Qt::BottomDockWidgetArea, m_logPane);
	if (ui->menuView)
		ui->menuView->addAction(m_logPane->toggleViewAction());

	// scene validation panel: dockable table of SceneDiagnostic entries, empty until a scene loads
	m_validationDock = new QDockWidget("Scene Validation", this);
	m_validationTable = new QTableWidget(0, 6, m_validationDock);
	QStringList validationHeaders;
	validationHeaders << "Severity" << "Code" << "Message" << "File" << "Path" << "Suggested Fix";
	m_validationTable->setHorizontalHeaderLabels(validationHeaders);
	m_validationTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_validationTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_validationTable->horizontalHeader()->setStretchLastSection(true);
	m_validationTable->resizeColumnsToContents();
	m_validationDock->setWidget(m_validationTable);
	addDockWidget(Qt::BottomDockWidgetArea, m_validationDock);
	tabifyDockWidget(m_logPane, m_validationDock);
	if (ui->menuView)
		ui->menuView->addAction(m_validationDock->toggleViewAction());

	m_loadedDataDock = new QDockWidget("Loaded Data", this);
	m_loadedDataTree = new QTreeWidget(m_loadedDataDock);
	m_loadedDataTree->setColumnCount(4);
	m_loadedDataTree->setHeaderLabels(QStringList() << "Category" << "Source" << "Count" << "Status");
	m_loadedDataTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_loadedDataTree->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_loadedDataTree->header()->setStretchLastSection(true);
	m_loadedDataDock->setWidget(m_loadedDataTree);
	addDockWidget(Qt::BottomDockWidgetArea, m_loadedDataDock);
	tabifyDockWidget(m_validationDock, m_loadedDataDock);
	if (ui->menuView)
		ui->menuView->addAction(m_loadedDataDock->toggleViewAction());

	// composition editor: dockable panel to view and edit train compositions.
	// this is the first editable scene panel, so it sets the pattern later
	// panes follow: a read-only list picks the record, a details area with
	// explicit fields/buttons edits it, no inline table editing.
	m_compositionDock = new QDockWidget("Compositions", this);
	QWidget* compositionWidget = new QWidget(m_compositionDock);
	QHBoxLayout* compositionLayout = new QHBoxLayout(compositionWidget);

	// left: the list of compositions and the buttons that manage it
	QWidget* compositionListPane = new QWidget(compositionWidget);
	QVBoxLayout* compositionListLayout = new QVBoxLayout(compositionListPane);
	compositionListLayout->addWidget(new QLabel("Compositions", compositionListPane));
	m_compositionListWidget = new QListWidget(compositionListPane);
	compositionListLayout->addWidget(m_compositionListWidget);
	QHBoxLayout* compositionButtonLayout = new QHBoxLayout();
	m_addCompositionButton = new QPushButton("Add Composition", compositionListPane);
	m_duplicateCompositionButton = new QPushButton("Duplicate", compositionListPane);
	m_deleteCompositionButton = new QPushButton("Delete", compositionListPane);
	compositionButtonLayout->addWidget(m_addCompositionButton);
	compositionButtonLayout->addWidget(m_duplicateCompositionButton);
	compositionButtonLayout->addWidget(m_deleteCompositionButton);
	compositionListLayout->addLayout(compositionButtonLayout);
	compositionLayout->addWidget(compositionListPane);

	// right: the selected composition's id and its ordered unit membership
	QWidget* compositionDetailPane = new QWidget(compositionWidget);
	QVBoxLayout* compositionDetailLayout = new QVBoxLayout(compositionDetailPane);
	compositionDetailLayout->addWidget(new QLabel("Composition Id", compositionDetailPane));
	m_compositionIdEdit = new QLineEdit(compositionDetailPane);
	compositionDetailLayout->addWidget(m_compositionIdEdit);
	compositionDetailLayout->addWidget(new QLabel("Units (in order)", compositionDetailPane));
	m_compositionUnitsListWidget = new QListWidget(compositionDetailPane);
	compositionDetailLayout->addWidget(m_compositionUnitsListWidget);
	QHBoxLayout* unitButtonLayout = new QHBoxLayout();
	m_addUnitButton = new QPushButton("Add Unit", compositionDetailPane);
	m_removeUnitButton = new QPushButton("Remove Unit", compositionDetailPane);
	m_moveUnitUpButton = new QPushButton("Move Up", compositionDetailPane);
	m_moveUnitDownButton = new QPushButton("Move Down", compositionDetailPane);
	unitButtonLayout->addWidget(m_addUnitButton);
	unitButtonLayout->addWidget(m_removeUnitButton);
	unitButtonLayout->addWidget(m_moveUnitUpButton);
	unitButtonLayout->addWidget(m_moveUnitDownButton);
	compositionDetailLayout->addLayout(unitButtonLayout);
	compositionLayout->addWidget(compositionDetailPane);

	m_compositionDock->setWidget(compositionWidget);
	addDockWidget(Qt::RightDockWidgetArea, m_compositionDock);
	if (ui->menuView)
		ui->menuView->addAction(m_compositionDock->toggleViewAction());

	connect(m_compositionListWidget, &QListWidget::currentRowChanged, this, [this](int) {
		updateCompositionDetailPanel();
	});
	connect(m_compositionUnitsListWidget, &QListWidget::currentRowChanged, this, [this](int) {
		updateCompositionUnitButtons();
	});
	connect(m_compositionIdEdit, &QLineEdit::editingFinished, this, &MainWindow::commitCompositionIdEdit);
	connect(m_addCompositionButton, &QPushButton::clicked, this, &MainWindow::addComposition);
	connect(m_duplicateCompositionButton, &QPushButton::clicked, this, &MainWindow::duplicateComposition);
	connect(m_deleteCompositionButton, &QPushButton::clicked, this, &MainWindow::deleteComposition);
	connect(m_addUnitButton, &QPushButton::clicked, this, &MainWindow::addUnitToComposition);
	connect(m_removeUnitButton, &QPushButton::clicked, this, &MainWindow::removeUnitFromComposition);
	connect(m_moveUnitUpButton, &QPushButton::clicked, this, &MainWindow::moveCompositionUnitUp);
	connect(m_moveUnitDownButton, &QPushButton::clicked, this, &MainWindow::moveCompositionUnitDown);

	// service editor: dockable panel for service-level fields plus the selected
	// service's timetable (stops). tabbed alongside Compositions, same list +
	// details shape; the stop list is a second, nested list + details editor
	// inside the service detail pane, since a stop only makes sense in the
	// context of the currently selected service.
	m_serviceDock = new QDockWidget("Services", this);
	QWidget* serviceWidget = new QWidget(m_serviceDock);
	QHBoxLayout* serviceLayout = new QHBoxLayout(serviceWidget);

	// left: the list of services and the buttons that manage it
	QWidget* serviceListPane = new QWidget(serviceWidget);
	QVBoxLayout* serviceListLayout = new QVBoxLayout(serviceListPane);
	serviceListLayout->addWidget(new QLabel("Services", serviceListPane));
	m_serviceListWidget = new QListWidget(serviceListPane);
	serviceListLayout->addWidget(m_serviceListWidget);
	QHBoxLayout* serviceButtonLayout = new QHBoxLayout();
	m_addServiceButton = new QPushButton("Add Service", serviceListPane);
	m_duplicateServiceButton = new QPushButton("Duplicate", serviceListPane);
	m_deleteServiceButton = new QPushButton("Delete", serviceListPane);
	serviceButtonLayout->addWidget(m_addServiceButton);
	serviceButtonLayout->addWidget(m_duplicateServiceButton);
	serviceButtonLayout->addWidget(m_deleteServiceButton);
	serviceListLayout->addLayout(serviceButtonLayout);
	serviceLayout->addWidget(serviceListPane);

	// right: the selected service's service-level fields
	QWidget* serviceDetailPane = new QWidget(serviceWidget);
	QVBoxLayout* serviceDetailLayout = new QVBoxLayout(serviceDetailPane);
	serviceDetailLayout->addWidget(new QLabel("Service Id", serviceDetailPane));
	m_serviceIdEdit = new QLineEdit(serviceDetailPane);
	serviceDetailLayout->addWidget(m_serviceIdEdit);
	serviceDetailLayout->addWidget(new QLabel("Composition", serviceDetailPane));
	m_serviceCompositionCombo = new QComboBox(serviceDetailPane);
	serviceDetailLayout->addWidget(m_serviceCompositionCombo);
	serviceDetailLayout->addWidget(new QLabel("Route", serviceDetailPane));
	m_serviceRouteCombo = new QComboBox(serviceDetailPane);
	serviceDetailLayout->addWidget(m_serviceRouteCombo);

	QHBoxLayout* entryTimeLayout = new QHBoxLayout();
	m_serviceHasEntryTimeCheck = new QCheckBox("Entry Time (s)", serviceDetailPane);
	m_serviceEntryTimeSecondsEdit = new QLineEdit(serviceDetailPane);
	m_serviceEntryTimeSecondsEdit->setValidator(
		new QIntValidator(0, std::numeric_limits<int>::max(), m_serviceEntryTimeSecondsEdit));
	entryTimeLayout->addWidget(m_serviceHasEntryTimeCheck);
	entryTimeLayout->addWidget(m_serviceEntryTimeSecondsEdit);
	serviceDetailLayout->addLayout(entryTimeLayout);

	QHBoxLayout* repeatLayout = new QHBoxLayout();
	m_serviceHasRepeatCheck = new QCheckBox("Repeat Headway (s)", serviceDetailPane);
	m_serviceHeadwaySecondsEdit = new QLineEdit(serviceDetailPane);
	m_serviceHeadwaySecondsEdit->setValidator(
		new QIntValidator(0, std::numeric_limits<int>::max(), m_serviceHeadwaySecondsEdit));
	repeatLayout->addWidget(m_serviceHasRepeatCheck);
	repeatLayout->addWidget(m_serviceHeadwaySecondsEdit);
	serviceDetailLayout->addLayout(repeatLayout);

	serviceDetailLayout->addWidget(new QLabel("Timetable (stops)", serviceDetailPane));
	m_stopListWidget = new QListWidget(serviceDetailPane);
	serviceDetailLayout->addWidget(m_stopListWidget);
	QHBoxLayout* stopButtonLayout = new QHBoxLayout();
	m_addStopButton = new QPushButton("Add Stop", serviceDetailPane);
	m_removeStopButton = new QPushButton("Remove Stop", serviceDetailPane);
	m_moveStopUpButton = new QPushButton("Move Up", serviceDetailPane);
	m_moveStopDownButton = new QPushButton("Move Down", serviceDetailPane);
	stopButtonLayout->addWidget(m_addStopButton);
	stopButtonLayout->addWidget(m_removeStopButton);
	stopButtonLayout->addWidget(m_moveStopUpButton);
	stopButtonLayout->addWidget(m_moveStopDownButton);
	serviceDetailLayout->addLayout(stopButtonLayout);

	serviceDetailLayout->addWidget(new QLabel("Stop Station", serviceDetailPane));
	m_stopStationCombo = new QComboBox(serviceDetailPane);
	serviceDetailLayout->addWidget(m_stopStationCombo);
	serviceDetailLayout->addWidget(new QLabel("Stop Platform", serviceDetailPane));
	m_stopPlatformCombo = new QComboBox(serviceDetailPane);
	serviceDetailLayout->addWidget(m_stopPlatformCombo);

	QHBoxLayout* stopArrivalLayout = new QHBoxLayout();
	m_stopHasArrivalCheck = new QCheckBox("Arrival (s)", serviceDetailPane);
	m_stopArrivalSecondsEdit = new QLineEdit(serviceDetailPane);
	m_stopArrivalSecondsEdit->setValidator(
		new QIntValidator(0, std::numeric_limits<int>::max(), m_stopArrivalSecondsEdit));
	stopArrivalLayout->addWidget(m_stopHasArrivalCheck);
	stopArrivalLayout->addWidget(m_stopArrivalSecondsEdit);
	serviceDetailLayout->addLayout(stopArrivalLayout);

	QHBoxLayout* stopDepartureLayout = new QHBoxLayout();
	m_stopHasDepartureCheck = new QCheckBox("Departure (s)", serviceDetailPane);
	m_stopDepartureSecondsEdit = new QLineEdit(serviceDetailPane);
	m_stopDepartureSecondsEdit->setValidator(
		new QIntValidator(0, std::numeric_limits<int>::max(), m_stopDepartureSecondsEdit));
	stopDepartureLayout->addWidget(m_stopHasDepartureCheck);
	stopDepartureLayout->addWidget(m_stopDepartureSecondsEdit);
	serviceDetailLayout->addLayout(stopDepartureLayout);

	serviceDetailLayout->addWidget(new QLabel("Dwell (s)", serviceDetailPane));
	m_stopDwellSecondsEdit = new QLineEdit(serviceDetailPane);
	m_stopDwellSecondsEdit->setValidator(new QIntValidator(0, std::numeric_limits<int>::max(), m_stopDwellSecondsEdit));
	serviceDetailLayout->addWidget(m_stopDwellSecondsEdit);

	serviceDetailLayout->addStretch();

	// the service detail pane plus the timetable editor is tall, so let it scroll
	// instead of overflowing the dock and overlapping the tab bar
	QScrollArea* serviceDetailScroll = new QScrollArea(serviceWidget);
	serviceDetailScroll->setWidgetResizable(true);
	serviceDetailScroll->setFrameShape(QFrame::NoFrame);
	serviceDetailScroll->setWidget(serviceDetailPane);
	serviceLayout->addWidget(serviceDetailScroll);

	m_serviceDock->setWidget(serviceWidget);
	addDockWidget(Qt::RightDockWidgetArea, m_serviceDock);
	tabifyDockWidget(m_compositionDock, m_serviceDock);
	if (ui->menuView)
		ui->menuView->addAction(m_serviceDock->toggleViewAction());

	connect(m_serviceListWidget, &QListWidget::currentRowChanged, this, [this](int) {
		updateServiceDetailPanel();
	});
	connect(m_serviceIdEdit, &QLineEdit::editingFinished, this, &MainWindow::commitServiceIdEdit);
	connect(m_serviceCompositionCombo, &QComboBox::currentTextChanged, this, &MainWindow::commitServiceComposition);
	connect(m_serviceRouteCombo, &QComboBox::currentTextChanged, this, &MainWindow::commitServiceRoute);
	connect(m_serviceHasEntryTimeCheck, &QCheckBox::toggled, this, &MainWindow::commitServiceHasEntryTime);
	connect(m_serviceEntryTimeSecondsEdit, &QLineEdit::editingFinished, this, &MainWindow::commitServiceEntryTimeSeconds);
	connect(m_serviceHasRepeatCheck, &QCheckBox::toggled, this, &MainWindow::commitServiceHasRepeat);
	connect(m_serviceHeadwaySecondsEdit, &QLineEdit::editingFinished, this, &MainWindow::commitServiceHeadwaySeconds);
	connect(m_addServiceButton, &QPushButton::clicked, this, &MainWindow::addService);
	connect(m_duplicateServiceButton, &QPushButton::clicked, this, &MainWindow::duplicateService);
	connect(m_deleteServiceButton, &QPushButton::clicked, this, &MainWindow::deleteService);

	connect(m_stopListWidget, &QListWidget::currentRowChanged, this, [this](int) {
		updateStopDetailPanel();
	});
	connect(m_addStopButton, &QPushButton::clicked, this, &MainWindow::addStop);
	connect(m_removeStopButton, &QPushButton::clicked, this, &MainWindow::removeStop);
	connect(m_moveStopUpButton, &QPushButton::clicked, this, &MainWindow::moveStopUp);
	connect(m_moveStopDownButton, &QPushButton::clicked, this, &MainWindow::moveStopDown);
	connect(m_stopStationCombo, &QComboBox::currentTextChanged, this, &MainWindow::commitStopStation);
	connect(m_stopPlatformCombo, &QComboBox::currentTextChanged, this, &MainWindow::commitStopPlatform);
	connect(m_stopHasArrivalCheck, &QCheckBox::toggled, this, &MainWindow::commitStopHasArrival);
	connect(m_stopArrivalSecondsEdit, &QLineEdit::editingFinished, this, &MainWindow::commitStopArrivalSeconds);
	connect(m_stopHasDepartureCheck, &QCheckBox::toggled, this, &MainWindow::commitStopHasDeparture);
	connect(m_stopDepartureSecondsEdit, &QLineEdit::editingFinished, this, &MainWindow::commitStopDepartureSeconds);
	connect(m_stopDwellSecondsEdit, &QLineEdit::editingFinished, this, &MainWindow::commitStopDwellSeconds);

	// incident editor: dockable panel for the flat list of scene incidents. each
	// incident has a type (signal_failure or train_breakdown) and a target whose
	// valid choices depend on that type, analogous to a stop's platform choices
	// depending on the selected station.
	m_incidentDock = new QDockWidget("Incidents", this);
	QWidget* incidentWidget = new QWidget(m_incidentDock);
	QHBoxLayout* incidentLayout = new QHBoxLayout(incidentWidget);

	// left: the list of incidents and the buttons that manage it
	QWidget* incidentListPane = new QWidget(incidentWidget);
	QVBoxLayout* incidentListLayout = new QVBoxLayout(incidentListPane);
	incidentListLayout->addWidget(new QLabel("Incidents", incidentListPane));
	m_incidentListWidget = new QListWidget(incidentListPane);
	incidentListLayout->addWidget(m_incidentListWidget);
	QHBoxLayout* incidentButtonLayout = new QHBoxLayout();
	m_addIncidentButton = new QPushButton("Add Incident", incidentListPane);
	m_duplicateIncidentButton = new QPushButton("Duplicate", incidentListPane);
	m_deleteIncidentButton = new QPushButton("Delete", incidentListPane);
	incidentButtonLayout->addWidget(m_addIncidentButton);
	incidentButtonLayout->addWidget(m_duplicateIncidentButton);
	incidentButtonLayout->addWidget(m_deleteIncidentButton);
	incidentListLayout->addLayout(incidentButtonLayout);
	incidentLayout->addWidget(incidentListPane);

	// right: the selected incident's fields
	QWidget* incidentDetailPane = new QWidget(incidentWidget);
	QVBoxLayout* incidentDetailLayout = new QVBoxLayout(incidentDetailPane);
	incidentDetailLayout->addWidget(new QLabel("Incident Id", incidentDetailPane));
	m_incidentIdEdit = new QLineEdit(incidentDetailPane);
	incidentDetailLayout->addWidget(m_incidentIdEdit);
	incidentDetailLayout->addWidget(new QLabel("Type", incidentDetailPane));
	m_incidentTypeCombo = new QComboBox(incidentDetailPane);
	m_incidentTypeCombo->addItem("signal_failure");
	m_incidentTypeCombo->addItem("train_breakdown");
	incidentDetailLayout->addWidget(m_incidentTypeCombo);
	incidentDetailLayout->addWidget(new QLabel("Target", incidentDetailPane));
	m_incidentTargetCombo = new QComboBox(incidentDetailPane);
	incidentDetailLayout->addWidget(m_incidentTargetCombo);
	incidentDetailLayout->addWidget(new QLabel("Start (s)", incidentDetailPane));
	m_incidentStartSecondsEdit = new QLineEdit(incidentDetailPane);
	m_incidentStartSecondsEdit->setValidator(
		new QIntValidator(0, std::numeric_limits<int>::max(), m_incidentStartSecondsEdit));
	incidentDetailLayout->addWidget(m_incidentStartSecondsEdit);
	incidentDetailLayout->addWidget(new QLabel("End (s)", incidentDetailPane));
	m_incidentEndSecondsEdit = new QLineEdit(incidentDetailPane);
	m_incidentEndSecondsEdit->setValidator(
		new QIntValidator(0, std::numeric_limits<int>::max(), m_incidentEndSecondsEdit));
	incidentDetailLayout->addWidget(m_incidentEndSecondsEdit);
	incidentDetailLayout->addStretch();

	// the detail pane may become taller than the dock, so let it scroll rather
	// than overflowing and overlapping the tab bar
	QScrollArea* incidentDetailScroll = new QScrollArea(incidentWidget);
	incidentDetailScroll->setWidgetResizable(true);
	incidentDetailScroll->setFrameShape(QFrame::NoFrame);
	incidentDetailScroll->setWidget(incidentDetailPane);
	incidentLayout->addWidget(incidentDetailScroll);

	m_incidentDock->setWidget(incidentWidget);
	addDockWidget(Qt::RightDockWidgetArea, m_incidentDock);
	tabifyDockWidget(m_serviceDock, m_incidentDock);
	if (ui->menuView)
		ui->menuView->addAction(m_incidentDock->toggleViewAction());

	connect(m_incidentListWidget, &QListWidget::currentRowChanged, this, [this](int) {
		updateIncidentDetailPanel();
	});
	connect(m_incidentIdEdit, &QLineEdit::editingFinished, this, &MainWindow::commitIncidentIdEdit);
	connect(m_incidentTypeCombo, &QComboBox::currentTextChanged, this, &MainWindow::commitIncidentType);
	connect(m_incidentTargetCombo, &QComboBox::currentTextChanged, this, &MainWindow::commitIncidentTarget);
	connect(m_incidentStartSecondsEdit, &QLineEdit::editingFinished, this, &MainWindow::commitIncidentStartSeconds);
	connect(m_incidentEndSecondsEdit, &QLineEdit::editingFinished, this, &MainWindow::commitIncidentEndSeconds);
	connect(m_addIncidentButton, &QPushButton::clicked, this, &MainWindow::addIncident);
	connect(m_duplicateIncidentButton, &QPushButton::clicked, this, &MainWindow::duplicateIncident);
	connect(m_deleteIncidentButton, &QPushButton::clicked, this, &MainWindow::deleteIncident);

	// permanent status-bar field for the scene validation summary, so it does
	// not clobber transient load/save messages
	m_validationStatusLabel = new QLabel(this);
	statusBar()->addPermanentWidget(m_validationStatusLabel);

	// route Qt log messages to std::cout so they flow through ConsoleWidget's streambuf
	qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& msg) {
		std::cout << msg.toStdString() << "\n";
	});

	// Connect zoom actions
	connect(ui->actionZoomIn, &QAction::triggered, this, &MainWindow::zoomIn);
	connect(ui->actionZoomOut, &QAction::triggered, this, &MainWindow::zoomOut);
	connect(ui->actionFitView, &QAction::triggered, this, &MainWindow::fitToView);
	connect(ui->actionQuit, &QAction::triggered, this, &QApplication::quit);

	// Diagrams menu
	m_diagramsMenu = menuBar()->addMenu("Diagrams");
	m_diagramsMenu->addAction("Speed / Distance (per train)...", this, &MainWindow::showSpeedDistanceDiagram);
	m_diagramsMenu->addAction("Speed / Time (per train)...", this, &MainWindow::showSpeedTimeDiagram);
	m_diagramsMenu->addAction("Time / Distance (per train)...", this, &MainWindow::showTimeDistanceDiagram);
	m_diagramsMenu->addSeparator();
	m_diagramsMenu->addAction("Timetable graph (train graph)...", this, &MainWindow::showTimetableGraph);
	m_diagramsMenu->addAction("Blocking-time overlay...", this, &MainWindow::showBlockingTimeDiagram);
	m_diagramsMenu->addAction("Timetable table (planned vs simulated)...", this, &MainWindow::showTimetableTable);
	m_diagramsMenu->addAction("Train delays...", this, &MainWindow::showDelayDiagram);

	// --- Status bar ---
	statusBar()->showMessage(QString("Ready - %1 (%2 tracks, %3 routes)")
								 .arg(QString::fromStdString(initial_variables.name))
								 .arg(initial_variables.numTrackLines)
								 .arg(initial_variables.N_Routes));

	refreshCompositionPanel();
	refreshServicePanel();
	refreshIncidentPanel();
}

MainWindow::~MainWindow() {
	delete m_runStagingDir;
	delete ui;
}

void MainWindow::closeEvent(QCloseEvent* event) {
	if (maybeSaveScene()) {
		QMainWindow::closeEvent(event);
	} else {
		event->ignore();
	}
}

void MainWindow::openSceneDialog() {
	if (!maybeSaveScene())
		return;

	QString dir = QFileDialog::getExistingDirectory(this, "Open Scene", m_sceneDir);
	if (dir.isEmpty())
		return;

	openSceneDirectory(dir);
}

bool MainWindow::openSceneDirectory(const QString& dir) {
	QString scenePath = QDir(dir).absolutePath();
	const bool reloadingSameScene = m_sceneLoaded && QDir(m_sceneDir).absolutePath() == scenePath;
	auto result = loadScene(scenePath.toStdString());
	int errorCount = errorDiagnosticCount(result.diagnostics);
	if (errorCount > 0) {
		QString message = firstDiagnosticMessage(result.diagnostics);
		if (message.isEmpty())
			message = "Scene could not be opened.";
		message += QString("\n\nError count: %1").arg(errorCount);
		showBlockingError(this, "Cannot Open Scene", message);
		return false;
	}

	teardownGUI();
	simulation.resetState();
	delete m_runStagingDir;
	m_runStagingDir = nullptr;

	m_sceneDir = scenePath;
	m_sceneModel = result.scene;
	m_sceneLoaded = true;
	m_sceneDirty = false;
	updateSceneWindowTitle();
	updateSceneActions();
	addRecentScene(scenePath);
	statusBar()->showMessage(QString("%1: %2 (%3 services, %4 routes)")
								 .arg(reloadingSameScene ? "Scene reloaded" : "Scene loaded")
								 .arg(QString::fromStdString(m_sceneModel.name))
								 .arg(static_cast<int>(m_sceneModel.services.size()))
								 .arg(static_cast<int>(m_sceneModel.routes.size())));
	refreshValidationPanel();
	refreshCompositionPanel();
	refreshServicePanel();
	refreshIncidentPanel();
	return true;
}

void MainWindow::saveScene() {
	saveSceneToCurrentDir();
}

bool MainWindow::saveSceneToCurrentDir() {
	if (!m_sceneLoaded)
		return false;
	if (m_sceneDir.isEmpty())
		return saveSceneAsToDirectory();

	auto result = ::saveScene(m_sceneModel, m_sceneDir.toStdString());
	if (!result.success()) {
		QString message = firstDiagnosticMessage(result.diagnostics);
		if (message.isEmpty())
			message = "Scene could not be saved.";
		QMessageBox::critical(this, "Cannot Save Scene", message);
		return false;
	}

	refreshSavedSceneMetadata(m_sceneModel);
	m_sceneDirty = false;
	updateSceneWindowTitle();
	updateSceneActions();
	statusBar()->showMessage("Scene saved");
	refreshValidationPanel();
	refreshCompositionPanel();
	refreshServicePanel();
	refreshIncidentPanel();
	return true;
}

void MainWindow::saveSceneAs() {
	saveSceneAsToDirectory();
}

bool MainWindow::saveSceneAsToDirectory() {
	if (!m_sceneLoaded)
		return false;

	QString startDir = m_sceneDir.isEmpty() ? QDir::homePath() : m_sceneDir;
	QString dir = QFileDialog::getExistingDirectory(this, "Save Scene As", startDir);
	if (dir.isEmpty())
		return false;

	QString targetPath = QDir(dir).absolutePath();
	QDir targetDir(targetPath);
	if (targetDir.exists()) {
		bool isNonEmpty = !targetDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty();
		bool hasSceneJson = QFileInfo(targetDir.filePath("scene.json")).exists();
		if (isNonEmpty && !hasSceneJson) {
			auto response = QMessageBox::question(this,
												  "Save Scene As",
												  "The selected folder is not empty and does not contain scene.json. Save scene files there?",
												  QMessageBox::Yes | QMessageBox::No,
												  QMessageBox::No);
			if (response != QMessageBox::Yes)
				return false;
		}
	}

	if (!copyScenePassthroughFiles(targetPath))
		return false;

	auto result = ::saveScene(m_sceneModel, targetPath.toStdString());
	if (!result.success()) {
		QString message = firstDiagnosticMessage(result.diagnostics);
		if (message.isEmpty())
			message = "Scene could not be saved.";
		QMessageBox::critical(this, "Cannot Save Scene", message);
		return false;
	}

	m_sceneDir = targetPath;
	refreshSavedSceneMetadata(m_sceneModel);
	m_sceneDirty = false;
	updateSceneWindowTitle();
	updateSceneActions();
	addRecentScene(targetPath);
	statusBar()->showMessage("Scene saved");
	refreshValidationPanel();
	refreshCompositionPanel();
	refreshServicePanel();
	refreshIncidentPanel();
	return true;
}

bool MainWindow::copyScenePassthroughFiles(const QString& targetDir) {
	if (m_sceneDir.isEmpty())
		return true;

	QString sourcePath = QDir(m_sceneDir).absolutePath();
	QString targetPath = QDir(targetDir).absolutePath();

	QDir target(targetPath);
	if (!target.exists() && !QDir().mkpath(targetPath)) {
		QMessageBox::critical(this, "Cannot Save Scene", "Cannot create target scene directory.");
		return false;
	}

	QString sourceCanonical = QDir(sourcePath).canonicalPath();
	QString targetCanonical = QDir(targetPath).canonicalPath();
	if (!sourceCanonical.isEmpty() && sourceCanonical == targetCanonical)
		return true;
	if (!sourceCanonical.isEmpty() && targetCanonical.startsWith(sourceCanonical + "/")) {
		QMessageBox::critical(this, "Cannot Save Scene", "Cannot save a scene inside the current scene folder.");
		return false;
	}

	QDir sourceDir(sourcePath);

	QString sourceLegacy = sourceDir.filePath("legacy");
	if (QDir(sourceLegacy).exists()) {
		if (!copyDirectoryRecursively(sourceLegacy, target.filePath("legacy"))) {
			QMessageBox::critical(this, "Cannot Save Scene", "Cannot copy legacy passthrough data.");
			return false;
		}
	}

	QString sourceViews = sourceDir.filePath("views.json");
	if (QFileInfo(sourceViews).exists()) {
		QString targetViews = target.filePath("views.json");
		QFile::remove(targetViews);
		if (!QFile::copy(sourceViews, targetViews)) {
			QMessageBox::critical(this, "Cannot Save Scene", "Cannot copy views.json.");
			return false;
		}
	}

	return true;
}

bool MainWindow::maybeSaveScene() {
	if (!m_sceneDirty)
		return true;

	auto response = QMessageBox::warning(this,
										 "Unsaved Scene",
										 "The current scene has unsaved changes.",
										 QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
										 QMessageBox::Save);
	if (response == QMessageBox::Save)
		return saveSceneToCurrentDir();
	if (response == QMessageBox::Cancel)
		return false;
	return true;
}

void MainWindow::updateSceneActions() {
	if (m_saveSceneAction)
		m_saveSceneAction->setEnabled(m_sceneLoaded && m_sceneDirty);
	if (m_saveSceneAsAction)
		m_saveSceneAsAction->setEnabled(m_sceneLoaded);
	if (m_runSceneAction)
		m_runSceneAction->setEnabled(m_sceneLoaded);
	if (m_recentScenesMenu) {
		QSettings settings;
		m_recentScenesMenu->setEnabled(!settings.value(kRecentScenesKey).toStringList().isEmpty());
	}
}

void MainWindow::refreshValidationPanel() {
	if (!m_sceneLoaded) {
		m_sceneDiagnostics.clear();
		if (m_validationTable)
			m_validationTable->setRowCount(0);
		if (m_validationStatusLabel)
			m_validationStatusLabel->clear();
		refreshLoadedDataTree();
		return;
	}

	m_sceneDiagnostics = validateScene(m_sceneModel);

	std::vector<SceneDiagnostic> ordered = m_sceneDiagnostics;
	std::stable_sort(ordered.begin(), ordered.end(), [](const SceneDiagnostic& a, const SceneDiagnostic& b) {
		return static_cast<int>(a.severity) > static_cast<int>(b.severity);
	});

	if (m_validationTable) {
		m_validationTable->clearContents();
		m_validationTable->setRowCount(static_cast<int>(ordered.size()));
		for (int row = 0; row < static_cast<int>(ordered.size()); ++row) {
			const SceneDiagnostic& d = ordered[row];
			m_validationTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(severityLabel(d.severity))));
			m_validationTable->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(d.code)));
			m_validationTable->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(d.message)));
			m_validationTable->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(d.file)));
			m_validationTable->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(d.path)));
			m_validationTable->setItem(row, 5, new QTableWidgetItem(QString::fromStdString(d.suggestedFix)));
		}
		m_validationTable->resizeColumnsToContents();
	}

	SceneDiagnosticCounts counts = countDiagnostics(m_sceneDiagnostics);
	QString message = QString("Validation: %1 error(s), %2 warning(s)").arg(counts.errors).arg(counts.warnings);
	if (counts.infos > 0)
		message += QString(", %1 info").arg(counts.infos);
	if (m_validationStatusLabel)
		m_validationStatusLabel->setText(message);
	refreshLoadedDataTree();
}

void MainWindow::refreshLoadedDataTree() {
	if (!m_loadedDataTree)
		return;

	m_loadedDataTree->clear();
	if (!m_sceneLoaded)
		return;

	refreshLoadedDataSummary(m_sceneModel);
	refreshLoadedDataDiagnostics(m_sceneModel, m_sceneDiagnostics);
	for (const auto& item : m_sceneModel.loadedData) {
		addLoadedDataTreeItem(m_loadedDataTree, nullptr, item);
	}
	m_loadedDataTree->resizeColumnToContents(0);
	m_loadedDataTree->resizeColumnToContents(1);
}

void MainWindow::refreshCompositionPanel() {
	bool hasScene = m_sceneLoaded;

	if (m_compositionListWidget) {
		int previousRow = m_compositionListWidget->currentRow();
		const QSignalBlocker blocker(m_compositionListWidget);
		m_compositionListWidget->clear();
		if (hasScene) {
			for (const auto& composition : m_sceneModel.compositions)
				m_compositionListWidget->addItem(QString::fromStdString(composition.id));
		}
		int rowCount = m_compositionListWidget->count();
		int rowToSelect = -1;
		if (rowCount > 0) {
			if (previousRow < 0)
				rowToSelect = 0;
			else if (previousRow >= rowCount)
				rowToSelect = rowCount - 1; // keep selection near a deleted last row
			else
				rowToSelect = previousRow;
		}
		m_compositionListWidget->setCurrentRow(rowToSelect);
		m_compositionListWidget->setEnabled(hasScene);
	}

	if (m_addCompositionButton)
		m_addCompositionButton->setEnabled(hasScene);

	updateCompositionDetailPanel();
}

void MainWindow::updateCompositionDetailPanel() {
	int row = m_compositionListWidget ? m_compositionListWidget->currentRow() : -1;
	bool hasSelection = m_sceneLoaded && row >= 0 && row < static_cast<int>(m_sceneModel.compositions.size());

	if (m_compositionIdEdit) {
		const QSignalBlocker blocker(m_compositionIdEdit);
		m_compositionIdEdit->setText(hasSelection ? QString::fromStdString(m_sceneModel.compositions[row].id) : QString());
		m_compositionIdEdit->setEnabled(hasSelection);
	}

	if (m_compositionUnitsListWidget) {
		const QSignalBlocker blocker(m_compositionUnitsListWidget);
		m_compositionUnitsListWidget->clear();
		if (hasSelection) {
			for (const auto& unitId : m_sceneModel.compositions[row].units)
				m_compositionUnitsListWidget->addItem(QString::fromStdString(unitId));
		}
		m_compositionUnitsListWidget->setEnabled(hasSelection);
	}

	if (m_duplicateCompositionButton)
		m_duplicateCompositionButton->setEnabled(hasSelection);
	if (m_deleteCompositionButton)
		m_deleteCompositionButton->setEnabled(hasSelection);
	if (m_addUnitButton)
		m_addUnitButton->setEnabled(hasSelection && !m_sceneModel.trainUnits.empty());

	updateCompositionUnitButtons();
}

void MainWindow::updateCompositionUnitButtons() {
	int unitRow = m_compositionUnitsListWidget ? m_compositionUnitsListWidget->currentRow() : -1;
	int unitCount = m_compositionUnitsListWidget ? m_compositionUnitsListWidget->count() : 0;
	bool hasUnitSelection = unitRow >= 0;

	if (m_removeUnitButton)
		m_removeUnitButton->setEnabled(hasUnitSelection);
	if (m_moveUnitUpButton)
		m_moveUnitUpButton->setEnabled(hasUnitSelection && unitRow > 0);
	if (m_moveUnitDownButton)
		m_moveUnitDownButton->setEnabled(hasUnitSelection && unitRow < unitCount - 1);
}

std::string MainWindow::uniqueCompositionId(const std::string& baseId) const {
	auto idExists = [this](const std::string& id) {
		for (const auto& composition : m_sceneModel.compositions) {
			if (composition.id == id)
				return true;
		}
		return false;
	};

	std::string candidate = baseId;
	int suffix = 2;
	while (idExists(candidate)) {
		candidate = baseId + "_" + std::to_string(suffix);
		++suffix;
	}
	return candidate;
}

void MainWindow::addComposition() {
	if (!m_sceneLoaded)
		return;

	SceneComposition composition;
	composition.id = uniqueCompositionId("new_composition");
	m_sceneModel.compositions.push_back(composition);

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshCompositionPanel();

	if (m_compositionListWidget)
		m_compositionListWidget->setCurrentRow(static_cast<int>(m_sceneModel.compositions.size()) - 1);
}

void MainWindow::duplicateComposition() {
	if (!m_sceneLoaded || !m_compositionListWidget)
		return;
	int row = m_compositionListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.compositions.size()))
		return;

	SceneComposition duplicate = m_sceneModel.compositions[row];
	duplicate.id = uniqueCompositionId(duplicate.id + "_copy");
	m_sceneModel.compositions.insert(m_sceneModel.compositions.begin() + row + 1, duplicate);

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshCompositionPanel();

	if (m_compositionListWidget)
		m_compositionListWidget->setCurrentRow(row + 1);
}

void MainWindow::deleteComposition() {
	if (!m_sceneLoaded || !m_compositionListWidget)
		return;
	int row = m_compositionListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.compositions.size()))
		return;

	QString id = QString::fromStdString(m_sceneModel.compositions[row].id);
	auto response = QMessageBox::question(this,
										  "Delete Composition",
										  QString("Delete composition '%1'?").arg(id),
										  QMessageBox::Yes | QMessageBox::No,
										  QMessageBox::No);
	if (response != QMessageBox::Yes)
		return;

	m_sceneModel.compositions.erase(m_sceneModel.compositions.begin() + row);

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshCompositionPanel();
}

void MainWindow::commitCompositionIdEdit() {
	if (!m_sceneLoaded || !m_compositionListWidget || !m_compositionIdEdit)
		return;
	int row = m_compositionListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.compositions.size()))
		return;

	std::string newId = m_compositionIdEdit->text().trimmed().toStdString();
	if (newId == m_sceneModel.compositions[row].id)
		return;

	m_sceneModel.compositions[row].id = newId;

	// update the list row label in place instead of rebuilding the panel, so a
	// focus-out that lands on another control keeps that pending click intact
	if (QListWidgetItem* item = m_compositionListWidget->item(row)) {
		const QSignalBlocker blocker(m_compositionListWidget);
		item->setText(QString::fromStdString(newId));
	}

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::addUnitToComposition() {
	if (!m_sceneLoaded || !m_compositionListWidget)
		return;
	int row = m_compositionListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.compositions.size()))
		return;
	if (m_sceneModel.trainUnits.empty()) {
		QMessageBox::information(this, "Add Unit", "This scene has no train units defined.");
		return;
	}

	QStringList unitIds;
	for (const auto& unit : m_sceneModel.trainUnits)
		unitIds << QString::fromStdString(unit.id);

	bool ok = false;
	QString chosen = QInputDialog::getItem(this, "Add Unit", "Train unit:", unitIds, 0, false, &ok);
	if (!ok || chosen.isEmpty())
		return;

	m_sceneModel.compositions[row].units.push_back(chosen.toStdString());

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshCompositionPanel();

	if (m_compositionUnitsListWidget)
		m_compositionUnitsListWidget->setCurrentRow(m_compositionUnitsListWidget->count() - 1);
}

void MainWindow::removeUnitFromComposition() {
	if (!m_sceneLoaded || !m_compositionListWidget || !m_compositionUnitsListWidget)
		return;
	int row = m_compositionListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.compositions.size()))
		return;

	std::vector<std::string>& units = m_sceneModel.compositions[row].units;
	int unitRow = m_compositionUnitsListWidget->currentRow();
	if (unitRow < 0 || unitRow >= static_cast<int>(units.size()))
		return;

	units.erase(units.begin() + unitRow);

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshCompositionPanel();

	if (m_compositionUnitsListWidget) {
		int remaining = m_compositionUnitsListWidget->count();
		if (remaining > 0)
			m_compositionUnitsListWidget->setCurrentRow(unitRow < remaining ? unitRow : remaining - 1);
	}
}

void MainWindow::moveCompositionUnitUp() {
	if (!m_sceneLoaded || !m_compositionListWidget || !m_compositionUnitsListWidget)
		return;
	int row = m_compositionListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.compositions.size()))
		return;

	std::vector<std::string>& units = m_sceneModel.compositions[row].units;
	int unitRow = m_compositionUnitsListWidget->currentRow();
	if (unitRow <= 0 || unitRow >= static_cast<int>(units.size()))
		return;

	std::string moved = units[unitRow];
	units[unitRow] = units[unitRow - 1];
	units[unitRow - 1] = moved;

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshCompositionPanel();

	if (m_compositionUnitsListWidget)
		m_compositionUnitsListWidget->setCurrentRow(unitRow - 1);
}

void MainWindow::moveCompositionUnitDown() {
	if (!m_sceneLoaded || !m_compositionListWidget || !m_compositionUnitsListWidget)
		return;
	int row = m_compositionListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.compositions.size()))
		return;

	std::vector<std::string>& units = m_sceneModel.compositions[row].units;
	int unitRow = m_compositionUnitsListWidget->currentRow();
	if (unitRow < 0 || unitRow + 1 >= static_cast<int>(units.size()))
		return;

	std::string moved = units[unitRow];
	units[unitRow] = units[unitRow + 1];
	units[unitRow + 1] = moved;

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshCompositionPanel();

	if (m_compositionUnitsListWidget)
		m_compositionUnitsListWidget->setCurrentRow(unitRow + 1);
}

void MainWindow::refreshServicePanel() {
	bool hasScene = m_sceneLoaded;

	if (m_serviceListWidget) {
		int previousRow = m_serviceListWidget->currentRow();
		const QSignalBlocker blocker(m_serviceListWidget);
		m_serviceListWidget->clear();
		if (hasScene) {
			for (const auto& service : m_sceneModel.services)
				m_serviceListWidget->addItem(QString::fromStdString(service.id));
		}
		int rowCount = m_serviceListWidget->count();
		int rowToSelect = -1;
		if (rowCount > 0) {
			if (previousRow < 0)
				rowToSelect = 0;
			else if (previousRow >= rowCount)
				rowToSelect = rowCount - 1; // keep selection near a deleted last row
			else
				rowToSelect = previousRow;
		}
		m_serviceListWidget->setCurrentRow(rowToSelect);
		m_serviceListWidget->setEnabled(hasScene);
	}

	if (m_addServiceButton)
		m_addServiceButton->setEnabled(hasScene);

	updateServiceDetailPanel();
}

void MainWindow::updateServiceDetailPanel() {
	int row = m_serviceListWidget ? m_serviceListWidget->currentRow() : -1;
	bool hasSelection = m_sceneLoaded && row >= 0 && row < static_cast<int>(m_sceneModel.services.size());

	if (m_serviceIdEdit) {
		const QSignalBlocker blocker(m_serviceIdEdit);
		m_serviceIdEdit->setText(hasSelection ? QString::fromStdString(m_sceneModel.services[row].id) : QString());
		m_serviceIdEdit->setEnabled(hasSelection);
	}

	if (m_serviceCompositionCombo) {
		const QSignalBlocker blocker(m_serviceCompositionCombo);
		m_serviceCompositionCombo->clear();
		for (const auto& composition : m_sceneModel.compositions)
			m_serviceCompositionCombo->addItem(QString::fromStdString(composition.id));
		if (hasSelection) {
			QString currentComposition = QString::fromStdString(m_sceneModel.services[row].composition);
			if (m_serviceCompositionCombo->findText(currentComposition) < 0)
				m_serviceCompositionCombo->addItem(currentComposition); // dangling reference, still shown/selectable
			m_serviceCompositionCombo->setCurrentText(currentComposition);
		}
		m_serviceCompositionCombo->setEnabled(hasSelection);
	}

	if (m_serviceRouteCombo) {
		const QSignalBlocker blocker(m_serviceRouteCombo);
		m_serviceRouteCombo->clear();
		for (const auto& route : m_sceneModel.routes)
			m_serviceRouteCombo->addItem(QString::fromStdString(route.id));
		if (hasSelection) {
			QString currentRoute = QString::fromStdString(m_sceneModel.services[row].route);
			if (m_serviceRouteCombo->findText(currentRoute) < 0)
				m_serviceRouteCombo->addItem(currentRoute); // dangling reference, still shown/selectable
			m_serviceRouteCombo->setCurrentText(currentRoute);
		}
		m_serviceRouteCombo->setEnabled(hasSelection);
	}

	bool hasEntryTime = hasSelection && m_sceneModel.services[row].hasEntryTime;
	if (m_serviceHasEntryTimeCheck) {
		const QSignalBlocker blocker(m_serviceHasEntryTimeCheck);
		m_serviceHasEntryTimeCheck->setChecked(hasEntryTime);
		m_serviceHasEntryTimeCheck->setEnabled(hasSelection);
	}
	if (m_serviceEntryTimeSecondsEdit) {
		const QSignalBlocker blocker(m_serviceEntryTimeSecondsEdit);
		int seconds = hasSelection ? static_cast<int>(m_sceneModel.services[row].entryTimeSeconds) : 0;
		m_serviceEntryTimeSecondsEdit->setText(QString::number(seconds));
		m_serviceEntryTimeSecondsEdit->setEnabled(hasEntryTime);
	}

	bool hasRepeat = hasSelection && m_sceneModel.services[row].hasRepeat;
	if (m_serviceHasRepeatCheck) {
		const QSignalBlocker blocker(m_serviceHasRepeatCheck);
		m_serviceHasRepeatCheck->setChecked(hasRepeat);
		m_serviceHasRepeatCheck->setEnabled(hasSelection);
	}
	if (m_serviceHeadwaySecondsEdit) {
		const QSignalBlocker blocker(m_serviceHeadwaySecondsEdit);
		int seconds = hasSelection ? static_cast<int>(m_sceneModel.services[row].headwaySeconds) : 0;
		m_serviceHeadwaySecondsEdit->setText(QString::number(seconds));
		m_serviceHeadwaySecondsEdit->setEnabled(hasRepeat);
	}

	// the stop list is part of the service detail, so it refreshes whenever the
	// selected service changes
	refreshStopList();

	if (m_duplicateServiceButton)
		m_duplicateServiceButton->setEnabled(hasSelection);
	if (m_deleteServiceButton)
		m_deleteServiceButton->setEnabled(hasSelection);
}

std::string MainWindow::uniqueServiceId(const std::string& baseId) const {
	auto idExists = [this](const std::string& id) {
		for (const auto& service : m_sceneModel.services) {
			if (service.id == id)
				return true;
		}
		return false;
	};

	std::string candidate = baseId;
	int suffix = 2;
	while (idExists(candidate)) {
		candidate = baseId + "_" + std::to_string(suffix);
		++suffix;
	}
	return candidate;
}

void MainWindow::addService() {
	if (!m_sceneLoaded)
		return;

	SceneService service;
	service.id = uniqueServiceId("new_service");
	m_sceneModel.services.push_back(service);

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshServicePanel();

	if (m_serviceListWidget)
		m_serviceListWidget->setCurrentRow(static_cast<int>(m_sceneModel.services.size()) - 1);
}

void MainWindow::duplicateService() {
	if (!m_sceneLoaded || !m_serviceListWidget)
		return;
	int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;

	SceneService duplicate = m_sceneModel.services[row];
	duplicate.id = uniqueServiceId(duplicate.id + "_copy");
	m_sceneModel.services.insert(m_sceneModel.services.begin() + row + 1, duplicate);

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshServicePanel();

	if (m_serviceListWidget)
		m_serviceListWidget->setCurrentRow(row + 1);
}

void MainWindow::deleteService() {
	if (!m_sceneLoaded || !m_serviceListWidget)
		return;
	int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;

	QString id = QString::fromStdString(m_sceneModel.services[row].id);
	auto response = QMessageBox::question(this,
										  "Delete Service",
										  QString("Delete service '%1'?").arg(id),
										  QMessageBox::Yes | QMessageBox::No,
										  QMessageBox::No);
	if (response != QMessageBox::Yes)
		return;

	m_sceneModel.services.erase(m_sceneModel.services.begin() + row);

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshServicePanel();
}

void MainWindow::commitServiceIdEdit() {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_serviceIdEdit)
		return;
	int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;

	std::string newId = m_serviceIdEdit->text().trimmed().toStdString();
	if (newId == m_sceneModel.services[row].id)
		return;

	m_sceneModel.services[row].id = newId;

	// update the list row label in place instead of rebuilding the panel, so a
	// focus-out that lands on another control keeps that pending click intact
	if (QListWidgetItem* item = m_serviceListWidget->item(row)) {
		const QSignalBlocker blocker(m_serviceListWidget);
		item->setText(QString::fromStdString(newId));
	}

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::commitServiceComposition(const QString& text) {
	if (!m_sceneLoaded || !m_serviceListWidget)
		return;
	int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;

	std::string newComposition = text.toStdString();
	if (newComposition == m_sceneModel.services[row].composition)
		return;

	m_sceneModel.services[row].composition = newComposition;

	// the combo already shows the chosen value and the service list labels are
	// unchanged, so do not rebuild the panel here (that would close the popup)
	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::commitServiceRoute(const QString& text) {
	if (!m_sceneLoaded || !m_serviceListWidget)
		return;
	int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;

	std::string newRoute = text.toStdString();
	if (newRoute == m_sceneModel.services[row].route)
		return;

	m_sceneModel.services[row].route = newRoute;

	// the combo already shows the chosen value and the service list labels are
	// unchanged, so do not rebuild the panel here (that would close the popup)
	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::commitServiceHasEntryTime(bool checked) {
	if (!m_sceneLoaded || !m_serviceListWidget)
		return;
	int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;
	if (checked == m_sceneModel.services[row].hasEntryTime)
		return;

	m_sceneModel.services[row].hasEntryTime = checked;
	if (m_serviceEntryTimeSecondsEdit)
		m_serviceEntryTimeSecondsEdit->setEnabled(checked);

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::commitServiceEntryTimeSeconds() {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_serviceEntryTimeSecondsEdit)
		return;
	int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;

	bool ok = false;
	int seconds = m_serviceEntryTimeSecondsEdit->text().toInt(&ok);
	if (!ok)
		seconds = 0;

	// normalize a blank or partial entry back to a plain integer display
	{
		const QSignalBlocker blocker(m_serviceEntryTimeSecondsEdit);
		m_serviceEntryTimeSecondsEdit->setText(QString::number(seconds));
	}

	double newValue = static_cast<double>(seconds);
	if (newValue == m_sceneModel.services[row].entryTimeSeconds)
		return;

	m_sceneModel.services[row].entryTimeSeconds = newValue;

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::commitServiceHasRepeat(bool checked) {
	if (!m_sceneLoaded || !m_serviceListWidget)
		return;
	int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;
	if (checked == m_sceneModel.services[row].hasRepeat)
		return;

	m_sceneModel.services[row].hasRepeat = checked;
	if (m_serviceHeadwaySecondsEdit)
		m_serviceHeadwaySecondsEdit->setEnabled(checked);

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::commitServiceHeadwaySeconds() {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_serviceHeadwaySecondsEdit)
		return;
	int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;

	bool ok = false;
	int seconds = m_serviceHeadwaySecondsEdit->text().toInt(&ok);
	if (!ok)
		seconds = 0;

	// normalize a blank or partial entry back to a plain integer display
	{
		const QSignalBlocker blocker(m_serviceHeadwaySecondsEdit);
		m_serviceHeadwaySecondsEdit->setText(QString::number(seconds));
	}

	double newValue = static_cast<double>(seconds);
	if (newValue == m_sceneModel.services[row].headwaySeconds)
		return;

	m_sceneModel.services[row].headwaySeconds = newValue;

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::refreshStopList() {
	int serviceRow = m_serviceListWidget ? m_serviceListWidget->currentRow() : -1;
	bool hasService = m_sceneLoaded && serviceRow >= 0 && serviceRow < static_cast<int>(m_sceneModel.services.size());

	if (m_stopListWidget) {
		const QSignalBlocker blocker(m_stopListWidget);
		m_stopListWidget->clear();
		if (hasService) {
			for (const auto& stop : m_sceneModel.services[serviceRow].stops)
				m_stopListWidget->addItem(stopRowLabel(stop));
		}
		// stops belong to the selected service, so a rebuild starts at the first
		// stop; callers that mutate the same service set their own row afterward
		int rowCount = m_stopListWidget->count();
		m_stopListWidget->setCurrentRow(rowCount > 0 ? 0 : -1);
		m_stopListWidget->setEnabled(hasService);
	}

	if (m_addStopButton)
		m_addStopButton->setEnabled(hasService);

	updateStopDetailPanel();
}

void MainWindow::updateStopDetailPanel() {
	int serviceRow = m_serviceListWidget ? m_serviceListWidget->currentRow() : -1;
	bool hasService = m_sceneLoaded && serviceRow >= 0 && serviceRow < static_cast<int>(m_sceneModel.services.size());
	int stopRow = m_stopListWidget ? m_stopListWidget->currentRow() : -1;
	bool hasSelection = hasService && stopRow >= 0 &&
						stopRow < static_cast<int>(m_sceneModel.services[serviceRow].stops.size());

	static const SceneStop emptyStop;
	const SceneStop& stop = hasSelection ? m_sceneModel.services[serviceRow].stops[stopRow] : emptyStop;

	if (m_stopStationCombo) {
		const QSignalBlocker blocker(m_stopStationCombo);
		m_stopStationCombo->clear();
		for (const auto& station : m_sceneModel.stations)
			m_stopStationCombo->addItem(QString::fromStdString(station.id));
		if (hasSelection) {
			QString currentStation = QString::fromStdString(stop.stationId);
			if (m_stopStationCombo->findText(currentStation) < 0)
				m_stopStationCombo->addItem(currentStation); // dangling reference, still shown/selectable
			m_stopStationCombo->setCurrentText(currentStation);
		}
		m_stopStationCombo->setEnabled(hasSelection);
	}

	// the platform choices are scoped to the stop's own station; this also lets
	// a station change refresh only the platform combo without rebuilding the
	// station combo from inside its own signal
	refreshStopPlatformCombo();

	bool hasArrival = hasSelection && stop.hasArrival;
	if (m_stopHasArrivalCheck) {
		const QSignalBlocker blocker(m_stopHasArrivalCheck);
		m_stopHasArrivalCheck->setChecked(hasArrival);
		m_stopHasArrivalCheck->setEnabled(hasSelection);
	}
	if (m_stopArrivalSecondsEdit) {
		const QSignalBlocker blocker(m_stopArrivalSecondsEdit);
		int seconds = hasSelection ? static_cast<int>(stop.arrivalSeconds) : 0;
		m_stopArrivalSecondsEdit->setText(QString::number(seconds));
		m_stopArrivalSecondsEdit->setEnabled(hasArrival);
	}

	bool hasDeparture = hasSelection && stop.hasDeparture;
	if (m_stopHasDepartureCheck) {
		const QSignalBlocker blocker(m_stopHasDepartureCheck);
		m_stopHasDepartureCheck->setChecked(hasDeparture);
		m_stopHasDepartureCheck->setEnabled(hasSelection);
	}
	if (m_stopDepartureSecondsEdit) {
		const QSignalBlocker blocker(m_stopDepartureSecondsEdit);
		int seconds = hasSelection ? static_cast<int>(stop.departureSeconds) : 0;
		m_stopDepartureSecondsEdit->setText(QString::number(seconds));
		m_stopDepartureSecondsEdit->setEnabled(hasDeparture);
	}

	if (m_stopDwellSecondsEdit) {
		const QSignalBlocker blocker(m_stopDwellSecondsEdit);
		int seconds = hasSelection ? static_cast<int>(stop.dwellSeconds) : 0;
		m_stopDwellSecondsEdit->setText(QString::number(seconds));
		m_stopDwellSecondsEdit->setEnabled(hasSelection);
	}

	int stopCount = m_stopListWidget ? m_stopListWidget->count() : 0;
	if (m_removeStopButton)
		m_removeStopButton->setEnabled(hasSelection);
	if (m_moveStopUpButton)
		m_moveStopUpButton->setEnabled(hasSelection && stopRow > 0);
	if (m_moveStopDownButton)
		m_moveStopDownButton->setEnabled(hasSelection && stopRow < stopCount - 1);
}

void MainWindow::refreshStopPlatformCombo() {
	if (!m_stopPlatformCombo)
		return;

	int serviceRow = m_serviceListWidget ? m_serviceListWidget->currentRow() : -1;
	bool hasService = m_sceneLoaded && serviceRow >= 0 && serviceRow < static_cast<int>(m_sceneModel.services.size());
	int stopRow = m_stopListWidget ? m_stopListWidget->currentRow() : -1;
	bool hasSelection = hasService && stopRow >= 0 &&
						stopRow < static_cast<int>(m_sceneModel.services[serviceRow].stops.size());

	const QSignalBlocker blocker(m_stopPlatformCombo);
	m_stopPlatformCombo->clear();
	m_stopPlatformCombo->addItem(QString()); // blank choice: no platform
	if (hasSelection) {
		const SceneStop& stop = m_sceneModel.services[serviceRow].stops[stopRow];
		// look the station up by id rather than trusting the station combo text
		const SceneStation* selectedStation = nullptr;
		for (const auto& station : m_sceneModel.stations) {
			if (station.id == stop.stationId) {
				selectedStation = &station;
				break;
			}
		}
		if (selectedStation) {
			for (const auto& platform : selectedStation->platforms)
				m_stopPlatformCombo->addItem(QString::fromStdString(platform.id));
		}
		QString currentPlatform = QString::fromStdString(stop.platformId);
		if (!currentPlatform.isEmpty() && m_stopPlatformCombo->findText(currentPlatform) < 0)
			m_stopPlatformCombo->addItem(currentPlatform); // dangling reference, still shown/selectable
		m_stopPlatformCombo->setCurrentText(currentPlatform);
	}
	m_stopPlatformCombo->setEnabled(hasSelection);
}

void MainWindow::addStop() {
	if (!m_sceneLoaded || !m_serviceListWidget)
		return;
	int serviceRow = m_serviceListWidget->currentRow();
	if (serviceRow < 0 || serviceRow >= static_cast<int>(m_sceneModel.services.size()))
		return;

	SceneStop stop;
	if (!m_sceneModel.stations.empty())
		stop.stationId = m_sceneModel.stations.front().id;
	m_sceneModel.services[serviceRow].stops.push_back(stop);

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshStopList();

	if (m_stopListWidget)
		m_stopListWidget->setCurrentRow(static_cast<int>(m_sceneModel.services[serviceRow].stops.size()) - 1);
}

void MainWindow::removeStop() {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_stopListWidget)
		return;
	int serviceRow = m_serviceListWidget->currentRow();
	if (serviceRow < 0 || serviceRow >= static_cast<int>(m_sceneModel.services.size()))
		return;

	std::vector<SceneStop>& stops = m_sceneModel.services[serviceRow].stops;
	int stopRow = m_stopListWidget->currentRow();
	if (stopRow < 0 || stopRow >= static_cast<int>(stops.size()))
		return;

	stops.erase(stops.begin() + stopRow);

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshStopList();

	if (m_stopListWidget) {
		int remaining = m_stopListWidget->count();
		if (remaining > 0)
			m_stopListWidget->setCurrentRow(stopRow < remaining ? stopRow : remaining - 1);
	}
}

void MainWindow::moveStopUp() {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_stopListWidget)
		return;
	int serviceRow = m_serviceListWidget->currentRow();
	if (serviceRow < 0 || serviceRow >= static_cast<int>(m_sceneModel.services.size()))
		return;

	std::vector<SceneStop>& stops = m_sceneModel.services[serviceRow].stops;
	int stopRow = m_stopListWidget->currentRow();
	if (stopRow <= 0 || stopRow >= static_cast<int>(stops.size()))
		return;

	SceneStop moved = stops[stopRow];
	stops[stopRow] = stops[stopRow - 1];
	stops[stopRow - 1] = moved;

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshStopList();

	if (m_stopListWidget)
		m_stopListWidget->setCurrentRow(stopRow - 1);
}

void MainWindow::moveStopDown() {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_stopListWidget)
		return;
	int serviceRow = m_serviceListWidget->currentRow();
	if (serviceRow < 0 || serviceRow >= static_cast<int>(m_sceneModel.services.size()))
		return;

	std::vector<SceneStop>& stops = m_sceneModel.services[serviceRow].stops;
	int stopRow = m_stopListWidget->currentRow();
	if (stopRow < 0 || stopRow + 1 >= static_cast<int>(stops.size()))
		return;

	SceneStop moved = stops[stopRow];
	stops[stopRow] = stops[stopRow + 1];
	stops[stopRow + 1] = moved;

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshStopList();

	if (m_stopListWidget)
		m_stopListWidget->setCurrentRow(stopRow + 1);
}

void MainWindow::commitStopStation(const QString& text) {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_stopListWidget)
		return;
	int serviceRow = m_serviceListWidget->currentRow();
	if (serviceRow < 0 || serviceRow >= static_cast<int>(m_sceneModel.services.size()))
		return;
	std::vector<SceneStop>& stops = m_sceneModel.services[serviceRow].stops;
	int stopRow = m_stopListWidget->currentRow();
	if (stopRow < 0 || stopRow >= static_cast<int>(stops.size()))
		return;

	std::string newStation = text.toStdString();
	if (newStation == stops[stopRow].stationId)
		return;

	stops[stopRow].stationId = newStation;

	// the platform choices are scoped to the station, so drop a platform that
	// is no longer valid for the newly selected station
	bool platformValid = stops[stopRow].platformId.empty();
	for (const auto& station : m_sceneModel.stations) {
		if (station.id != newStation)
			continue;
		for (const auto& platform : station.platforms) {
			if (platform.id == stops[stopRow].platformId) {
				platformValid = true;
				break;
			}
		}
		break;
	}
	if (!platformValid)
		stops[stopRow].platformId.clear();

	// update the list row label in place instead of rebuilding the whole list
	if (QListWidgetItem* item = m_stopListWidget->item(stopRow)) {
		const QSignalBlocker blocker(m_stopListWidget);
		item->setText(stopRowLabel(stops[stopRow]));
	}

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();

	// the station changed, so rebuild only its platform combo; rebuilding the
	// station combo here would mean clearing it from inside its own signal
	refreshStopPlatformCombo();
}

void MainWindow::commitStopPlatform(const QString& text) {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_stopListWidget)
		return;
	int serviceRow = m_serviceListWidget->currentRow();
	if (serviceRow < 0 || serviceRow >= static_cast<int>(m_sceneModel.services.size()))
		return;
	std::vector<SceneStop>& stops = m_sceneModel.services[serviceRow].stops;
	int stopRow = m_stopListWidget->currentRow();
	if (stopRow < 0 || stopRow >= static_cast<int>(stops.size()))
		return;

	std::string newPlatform = text.toStdString();
	if (newPlatform == stops[stopRow].platformId)
		return;

	stops[stopRow].platformId = newPlatform;

	// update the list row label in place instead of rebuilding the whole list
	if (QListWidgetItem* item = m_stopListWidget->item(stopRow)) {
		const QSignalBlocker blocker(m_stopListWidget);
		item->setText(stopRowLabel(stops[stopRow]));
	}

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::commitStopHasArrival(bool checked) {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_stopListWidget)
		return;
	int serviceRow = m_serviceListWidget->currentRow();
	if (serviceRow < 0 || serviceRow >= static_cast<int>(m_sceneModel.services.size()))
		return;
	std::vector<SceneStop>& stops = m_sceneModel.services[serviceRow].stops;
	int stopRow = m_stopListWidget->currentRow();
	if (stopRow < 0 || stopRow >= static_cast<int>(stops.size()))
		return;
	if (checked == stops[stopRow].hasArrival)
		return;

	stops[stopRow].hasArrival = checked;
	if (m_stopArrivalSecondsEdit)
		m_stopArrivalSecondsEdit->setEnabled(checked);

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::commitStopHasDeparture(bool checked) {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_stopListWidget)
		return;
	int serviceRow = m_serviceListWidget->currentRow();
	if (serviceRow < 0 || serviceRow >= static_cast<int>(m_sceneModel.services.size()))
		return;
	std::vector<SceneStop>& stops = m_sceneModel.services[serviceRow].stops;
	int stopRow = m_stopListWidget->currentRow();
	if (stopRow < 0 || stopRow >= static_cast<int>(stops.size()))
		return;
	if (checked == stops[stopRow].hasDeparture)
		return;

	stops[stopRow].hasDeparture = checked;
	if (m_stopDepartureSecondsEdit)
		m_stopDepartureSecondsEdit->setEnabled(checked);

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::commitStopArrivalSeconds() {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_stopListWidget || !m_stopArrivalSecondsEdit)
		return;
	int serviceRow = m_serviceListWidget->currentRow();
	if (serviceRow < 0 || serviceRow >= static_cast<int>(m_sceneModel.services.size()))
		return;
	std::vector<SceneStop>& stops = m_sceneModel.services[serviceRow].stops;
	int stopRow = m_stopListWidget->currentRow();
	if (stopRow < 0 || stopRow >= static_cast<int>(stops.size()))
		return;

	bool ok = false;
	int seconds = m_stopArrivalSecondsEdit->text().toInt(&ok);
	if (!ok)
		seconds = 0;

	// normalize a blank or partial entry back to a plain integer display
	{
		const QSignalBlocker blocker(m_stopArrivalSecondsEdit);
		m_stopArrivalSecondsEdit->setText(QString::number(seconds));
	}

	double newValue = static_cast<double>(seconds);
	if (newValue == stops[stopRow].arrivalSeconds)
		return;

	stops[stopRow].arrivalSeconds = newValue;

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::commitStopDepartureSeconds() {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_stopListWidget || !m_stopDepartureSecondsEdit)
		return;
	int serviceRow = m_serviceListWidget->currentRow();
	if (serviceRow < 0 || serviceRow >= static_cast<int>(m_sceneModel.services.size()))
		return;
	std::vector<SceneStop>& stops = m_sceneModel.services[serviceRow].stops;
	int stopRow = m_stopListWidget->currentRow();
	if (stopRow < 0 || stopRow >= static_cast<int>(stops.size()))
		return;

	bool ok = false;
	int seconds = m_stopDepartureSecondsEdit->text().toInt(&ok);
	if (!ok)
		seconds = 0;

	// normalize a blank or partial entry back to a plain integer display
	{
		const QSignalBlocker blocker(m_stopDepartureSecondsEdit);
		m_stopDepartureSecondsEdit->setText(QString::number(seconds));
	}

	double newValue = static_cast<double>(seconds);
	if (newValue == stops[stopRow].departureSeconds)
		return;

	stops[stopRow].departureSeconds = newValue;

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::commitStopDwellSeconds() {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_stopListWidget || !m_stopDwellSecondsEdit)
		return;
	int serviceRow = m_serviceListWidget->currentRow();
	if (serviceRow < 0 || serviceRow >= static_cast<int>(m_sceneModel.services.size()))
		return;
	std::vector<SceneStop>& stops = m_sceneModel.services[serviceRow].stops;
	int stopRow = m_stopListWidget->currentRow();
	if (stopRow < 0 || stopRow >= static_cast<int>(stops.size()))
		return;

	bool ok = false;
	int seconds = m_stopDwellSecondsEdit->text().toInt(&ok);
	if (!ok)
		seconds = 0;

	// normalize a blank or partial entry back to a plain integer display
	{
		const QSignalBlocker blocker(m_stopDwellSecondsEdit);
		m_stopDwellSecondsEdit->setText(QString::number(seconds));
	}

	double newValue = static_cast<double>(seconds);
	if (newValue == stops[stopRow].dwellSeconds)
		return;

	stops[stopRow].dwellSeconds = newValue;

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::refreshIncidentPanel() {
	bool hasScene = m_sceneLoaded;

	if (m_incidentListWidget) {
		int previousRow = m_incidentListWidget->currentRow();
		const QSignalBlocker blocker(m_incidentListWidget);
		m_incidentListWidget->clear();
		if (hasScene) {
			for (const auto& incident : m_sceneModel.incidents) {
				QString label = QString::fromStdString(incident.id) + " (" + QString::fromStdString(incident.type) + ")";
				m_incidentListWidget->addItem(label);
			}
		}
		int rowCount = m_incidentListWidget->count();
		int rowToSelect = -1;
		if (rowCount > 0) {
			if (previousRow < 0)
				rowToSelect = 0;
			else if (previousRow >= rowCount)
				rowToSelect = rowCount - 1; // keep selection near a deleted last row
			else
				rowToSelect = previousRow;
		}
		m_incidentListWidget->setCurrentRow(rowToSelect);
		m_incidentListWidget->setEnabled(hasScene);
	}

	if (m_addIncidentButton)
		m_addIncidentButton->setEnabled(hasScene);

	updateIncidentDetailPanel();
}

void MainWindow::updateIncidentDetailPanel() {
	int row = m_incidentListWidget ? m_incidentListWidget->currentRow() : -1;
	bool hasSelection = m_sceneLoaded && row >= 0 && row < static_cast<int>(m_sceneModel.incidents.size());

	if (m_incidentIdEdit) {
		const QSignalBlocker blocker(m_incidentIdEdit);
		m_incidentIdEdit->setText(hasSelection ? QString::fromStdString(m_sceneModel.incidents[row].id) : QString());
		m_incidentIdEdit->setEnabled(hasSelection);
	}

	if (m_incidentTypeCombo) {
		const QSignalBlocker blocker(m_incidentTypeCombo);
		if (hasSelection) {
			QString currentType = QString::fromStdString(m_sceneModel.incidents[row].type);
			int typeIndex = m_incidentTypeCombo->findText(currentType);
			// unknown type: default to signal_failure
			m_incidentTypeCombo->setCurrentIndex(typeIndex >= 0 ? typeIndex : 0);
		} else {
			m_incidentTypeCombo->setCurrentIndex(0);
		}
		m_incidentTypeCombo->setEnabled(hasSelection);
	}

	// the target choices depend on the type; refresh the target combo after the
	// type combo is already showing the correct value
	refreshIncidentTargetCombo();

	if (m_incidentStartSecondsEdit) {
		const QSignalBlocker blocker(m_incidentStartSecondsEdit);
		int seconds = hasSelection ? static_cast<int>(m_sceneModel.incidents[row].startSeconds) : 0;
		m_incidentStartSecondsEdit->setText(QString::number(seconds));
		m_incidentStartSecondsEdit->setEnabled(hasSelection);
	}

	if (m_incidentEndSecondsEdit) {
		const QSignalBlocker blocker(m_incidentEndSecondsEdit);
		int seconds = hasSelection ? static_cast<int>(m_sceneModel.incidents[row].endSeconds) : 0;
		m_incidentEndSecondsEdit->setText(QString::number(seconds));
		m_incidentEndSecondsEdit->setEnabled(hasSelection);
	}

	if (m_duplicateIncidentButton)
		m_duplicateIncidentButton->setEnabled(hasSelection);
	if (m_deleteIncidentButton)
		m_deleteIncidentButton->setEnabled(hasSelection);
}

void MainWindow::refreshIncidentTargetCombo() {
	if (!m_incidentTargetCombo)
		return;

	int row = m_incidentListWidget ? m_incidentListWidget->currentRow() : -1;
	bool hasSelection = m_sceneLoaded && row >= 0 && row < static_cast<int>(m_sceneModel.incidents.size());

	const QSignalBlocker blocker(m_incidentTargetCombo);
	m_incidentTargetCombo->clear();
	m_incidentTargetCombo->addItem(QString()); // blank choice: no target

	if (hasSelection) {
		const SceneIncident& incident = m_sceneModel.incidents[row];
		// which pool of ids to offer depends on the current type
		QString typeText = m_incidentTypeCombo ? m_incidentTypeCombo->currentText() : QString();
		if (typeText == "signal_failure") {
#ifdef signals
#define EGTRAIN_INCIDENT_RESTORE_SIGNALS
#undef signals
#endif
			const auto& signalList = m_sceneModel.signals;
#ifdef EGTRAIN_INCIDENT_RESTORE_SIGNALS
#define signals Q_SIGNALS
#undef EGTRAIN_INCIDENT_RESTORE_SIGNALS
#endif
			for (int i = 0; i < static_cast<int>(signalList.size()); ++i)
				m_incidentTargetCombo->addItem(QString::fromStdString(signalList[i].id));
		} else {
			// train_breakdown or any unrecognised type: offer service ids
			for (const auto& service : m_sceneModel.services)
				m_incidentTargetCombo->addItem(QString::fromStdString(service.id));
		}
		QString currentTarget = QString::fromStdString(incident.target);
		if (!currentTarget.isEmpty() && m_incidentTargetCombo->findText(currentTarget) < 0)
			m_incidentTargetCombo->addItem(currentTarget); // dangling reference, still shown/selectable
		m_incidentTargetCombo->setCurrentText(currentTarget);
	}
	m_incidentTargetCombo->setEnabled(hasSelection);
}

std::string MainWindow::uniqueIncidentId(const std::string& baseId) const {
	auto idExists = [this](const std::string& id) {
		for (const auto& incident : m_sceneModel.incidents) {
			if (incident.id == id)
				return true;
		}
		return false;
	};

	std::string candidate = baseId;
	int suffix = 2;
	while (idExists(candidate)) {
		candidate = baseId + "_" + std::to_string(suffix);
		++suffix;
	}
	return candidate;
}

void MainWindow::addIncident() {
	if (!m_sceneLoaded)
		return;

	SceneIncident incident;
	incident.id = uniqueIncidentId("new_incident");
	incident.type = "signal_failure";
	m_sceneModel.incidents.push_back(incident);

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshIncidentPanel();

	if (m_incidentListWidget)
		m_incidentListWidget->setCurrentRow(static_cast<int>(m_sceneModel.incidents.size()) - 1);
}

void MainWindow::duplicateIncident() {
	if (!m_sceneLoaded || !m_incidentListWidget)
		return;
	int row = m_incidentListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.incidents.size()))
		return;

	SceneIncident duplicate = m_sceneModel.incidents[row];
	duplicate.id = uniqueIncidentId(duplicate.id + "_copy");
	m_sceneModel.incidents.insert(m_sceneModel.incidents.begin() + row + 1, duplicate);

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshIncidentPanel();

	if (m_incidentListWidget)
		m_incidentListWidget->setCurrentRow(row + 1);
}

void MainWindow::deleteIncident() {
	if (!m_sceneLoaded || !m_incidentListWidget)
		return;
	int row = m_incidentListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.incidents.size()))
		return;

	QString id = QString::fromStdString(m_sceneModel.incidents[row].id);
	auto response = QMessageBox::question(this,
										  "Delete Incident",
										  QString("Delete incident '%1'?").arg(id),
										  QMessageBox::Yes | QMessageBox::No,
										  QMessageBox::No);
	if (response != QMessageBox::Yes)
		return;

	m_sceneModel.incidents.erase(m_sceneModel.incidents.begin() + row);

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshIncidentPanel();
}

void MainWindow::commitIncidentIdEdit() {
	if (!m_sceneLoaded || !m_incidentListWidget || !m_incidentIdEdit)
		return;
	int row = m_incidentListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.incidents.size()))
		return;

	std::string newId = m_incidentIdEdit->text().trimmed().toStdString();
	if (newId == m_sceneModel.incidents[row].id)
		return;

	m_sceneModel.incidents[row].id = newId;

	// update the list row label in place instead of rebuilding the panel, so a
	// focus-out that lands on another control keeps that pending click intact
	if (QListWidgetItem* item = m_incidentListWidget->item(row)) {
		const QSignalBlocker blocker(m_incidentListWidget);
		QString label = QString::fromStdString(newId) + " (" + QString::fromStdString(m_sceneModel.incidents[row].type) + ")";
		item->setText(label);
	}

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::commitIncidentType(const QString& text) {
	if (!m_sceneLoaded || !m_incidentListWidget)
		return;
	int row = m_incidentListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.incidents.size()))
		return;

	std::string newType = text.toStdString();
	if (newType == m_sceneModel.incidents[row].type)
		return;

	m_sceneModel.incidents[row].type = newType;

	// when the type changes the valid target pool also changes; drop a target
	// that is no longer valid for the new type
	bool targetValid = m_sceneModel.incidents[row].target.empty();
	if (!targetValid) {
		if (newType == "signal_failure") {
#ifdef signals
#define EGTRAIN_INCIDENT_RESTORE_SIGNALS
#undef signals
#endif
			const auto& signalList = m_sceneModel.signals;
#ifdef EGTRAIN_INCIDENT_RESTORE_SIGNALS
#define signals Q_SIGNALS
#undef EGTRAIN_INCIDENT_RESTORE_SIGNALS
#endif
			for (int i = 0; i < static_cast<int>(signalList.size()); ++i) {
				if (signalList[i].id == m_sceneModel.incidents[row].target) {
					targetValid = true;
					break;
				}
			}
		} else {
			for (const auto& service : m_sceneModel.services) {
				if (service.id == m_sceneModel.incidents[row].target) {
					targetValid = true;
					break;
				}
			}
		}
	}
	if (!targetValid)
		m_sceneModel.incidents[row].target.clear();

	// update the list row label in place to reflect the new type
	if (QListWidgetItem* item = m_incidentListWidget->item(row)) {
		const QSignalBlocker blocker(m_incidentListWidget);
		QString label = QString::fromStdString(m_sceneModel.incidents[row].id) + " (" + text + ")";
		item->setText(label);
	}

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();

	// rebuild only the target combo; the type combo already shows the new value
	// so rebuilding it from inside its own signal is unnecessary and harmful
	refreshIncidentTargetCombo();
}

void MainWindow::commitIncidentTarget(const QString& text) {
	if (!m_sceneLoaded || !m_incidentListWidget)
		return;
	int row = m_incidentListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.incidents.size()))
		return;

	std::string newTarget = text.toStdString();
	if (newTarget == m_sceneModel.incidents[row].target)
		return;

	m_sceneModel.incidents[row].target = newTarget;

	// the combo already shows the chosen value; no panel rebuild needed
	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::commitIncidentStartSeconds() {
	if (!m_sceneLoaded || !m_incidentListWidget || !m_incidentStartSecondsEdit)
		return;
	int row = m_incidentListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.incidents.size()))
		return;

	bool ok = false;
	int seconds = m_incidentStartSecondsEdit->text().toInt(&ok);
	if (!ok)
		seconds = 0;

	// normalize a blank or partial entry back to a plain integer display
	{
		const QSignalBlocker blocker(m_incidentStartSecondsEdit);
		m_incidentStartSecondsEdit->setText(QString::number(seconds));
	}

	double newValue = static_cast<double>(seconds);
	if (newValue == m_sceneModel.incidents[row].startSeconds)
		return;

	m_sceneModel.incidents[row].startSeconds = newValue;

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::commitIncidentEndSeconds() {
	if (!m_sceneLoaded || !m_incidentListWidget || !m_incidentEndSecondsEdit)
		return;
	int row = m_incidentListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.incidents.size()))
		return;

	bool ok = false;
	int seconds = m_incidentEndSecondsEdit->text().toInt(&ok);
	if (!ok)
		seconds = 0;

	// normalize a blank or partial entry back to a plain integer display
	{
		const QSignalBlocker blocker(m_incidentEndSecondsEdit);
		m_incidentEndSecondsEdit->setText(QString::number(seconds));
	}

	double newValue = static_cast<double>(seconds);
	if (newValue == m_sceneModel.incidents[row].endSeconds)
		return;

	m_sceneModel.incidents[row].endSeconds = newValue;

	m_sceneDirty = true;
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::addRecentScene(const QString& path) {
	QString cleanPath = QDir(path).absolutePath();
	QSettings settings;
	QStringList recent = settings.value(kRecentScenesKey).toStringList();
	recent.removeAll(cleanPath);
	recent.prepend(cleanPath);
	while (recent.size() > kMaxRecentScenes)
		recent.removeLast();
	settings.setValue(kRecentScenesKey, recent);
	rebuildRecentScenesMenu();
	updateSceneActions();
}

void MainWindow::rebuildRecentScenesMenu() {
	if (!m_recentScenesMenu)
		return;

	m_recentScenesMenu->clear();
	QSettings settings;
	QStringList recent = settings.value(kRecentScenesKey).toStringList();
	QStringList cleanedRecent;
	bool pruned = false;
	for (const QString& path : recent) {
		if (!QFileInfo(QDir(path).filePath("scene.json")).exists()) {
			pruned = true;
		} else {
			cleanedRecent.append(path);
		}
	}
	if (pruned) {
		settings.setValue(kRecentScenesKey, cleanedRecent);
		recent = cleanedRecent;
	}
	for (const QString& path : recent) {
		QAction* action = m_recentScenesMenu->addAction(path);
		action->setData(path);
		connect(action, &QAction::triggered, this, [this, action]() {
			if (!maybeSaveScene())
				return;
			openSceneDirectory(action->data().toString());
		});
	}
	if (recent.isEmpty()) {
		QAction* emptyAction = m_recentScenesMenu->addAction("No Recent Scenes");
		emptyAction->setEnabled(false);
	}
	updateSceneActions();
}

void MainWindow::updateSceneWindowTitle() {
	if (m_sceneLoaded) {
		setWindowTitle(QString("EGTRAIN - %1[*]").arg(QString::fromStdString(m_sceneModel.name)));
	} else {
		setWindowTitle("EGTRAIN[*]");
	}
	setWindowModified(m_sceneDirty);
}

void MainWindow::updateSpeedModeDisplay(int value) {
	if (m_speedLabel)
		m_speedLabel->setText(simulationSpeedLabel(value));
	if (m_speedSlider) {
		m_speedSlider->setToolTip(QString("Simulation speed: %1 mode").arg(simulationSpeedMode(value).toLower()));
	}
}

void MainWindow::refreshFollowTrainChoices() {
	if (!m_followTrainCombo)
		return;

	int previousTrainIndex = m_followTrainIndex;
	m_updatingFollowCombo = true;
	const QSignalBlocker blocker(m_followTrainCombo);
	m_followTrainCombo->clear();

	for (int train = 0; train < numRegions; ++train) {
		QString label = QString::fromStdString(regional_train[train].trainDescription);
		if (label.isEmpty() || label == "None")
			label = QString("Train %1").arg(train + 1);
		m_followTrainCombo->addItem(label, train);
	}

	int comboIndex = m_followTrainCombo->findData(previousTrainIndex);
	if (comboIndex < 0 && m_followTrainCombo->count() > 0)
		comboIndex = 0;
	if (comboIndex >= 0)
		m_followTrainCombo->setCurrentIndex(comboIndex);

	if (m_followAction && m_followAction->isChecked() && comboIndex >= 0)
		m_followTrainIndex = m_followTrainCombo->itemData(comboIndex).toInt();
	else if (!m_followAction || !m_followAction->isChecked())
		m_followTrainIndex = -1;

	m_updatingFollowCombo = false;
}

void MainWindow::runVisualPolishE2E() {
	if (m_e2eFinished)
		return;
	if (allTrains.isEmpty() && m_e2eAttempts < 20) {
		++m_e2eAttempts;
		QTimer::singleShot(500, this, &MainWindow::runVisualPolishE2E);
		return;
	}
	m_e2eFinished = true;

	bool ok = true;
	QStringList failures;

	if (!m_speedSlider || !m_speedLabel) {
		ok = false;
		failures << "missing speed controls";
	} else {
		m_speedSlider->setValue(250);
		QApplication::processEvents();
		if (!m_speedLabel->text().contains("+250 ms")) {
			ok = false;
			failures << "speed label did not update";
		}
		m_speedSlider->setValue(0);
	}

	if (!m_followAction || !m_followTrainCombo || m_followTrainCombo->count() == 0) {
		ok = false;
		failures << "missing follow controls";
	} else {
		m_followTrainCombo->setCurrentIndex(0);
		m_followAction->setChecked(true);
		QApplication::processEvents();
		if (!m_followAction->isChecked() || m_followTrainIndex < 0) {
			ok = false;
			failures << "follow mode did not activate";
		}
	}

	if (allTrains.isEmpty()) {
		ok = false;
		failures << "no train items rendered";
	}

	QString screenshotPath = qEnvironmentVariable("QEGTRAIN_E2E_SCREENSHOT");
	if (!screenshotPath.isEmpty()) {
		QPixmap image = networkView->grab();
		if (!image.save(screenshotPath)) {
			ok = false;
			failures << "screenshot save failed";
		}
	}

	if (ok) {
		std::fprintf(stdout, "E2E_VISUAL_POLISH_OK\n");
		std::fflush(stdout);
		if (m_worker && m_workerThread && m_workerThread->isRunning()) {
			connect(m_workerThread, &QThread::finished, qApp, []() {
				QCoreApplication::exit(0);
			});
			m_worker->requestStop();
			return;
		}
		QCoreApplication::exit(0);
		return;
	}

	std::fprintf(stderr, "E2E_VISUAL_POLISH_FAIL: %s\n", failures.join(", ").toStdString().c_str());
	std::fflush(stderr);
	if (m_worker && m_workerThread && m_workerThread->isRunning()) {
		connect(m_workerThread, &QThread::finished, qApp, []() {
			QCoreApplication::exit(2);
		});
		m_worker->requestStop();
		return;
	}
	QCoreApplication::exit(2);
}

// env-gated scene render smoke: open the scene from QEGTRAIN_E2E_SCENE, run it,
// and verify the network view draws real geometry instead of a collapsed point
void MainWindow::runSceneRenderE2E() {
	if (m_e2eFinished)
		return;

	if (!m_sceneLoaded) {
		QString sceneDir = qEnvironmentVariable("QEGTRAIN_E2E_SCENE");
		if (sceneDir.isEmpty() || !openSceneDirectory(sceneDir)) {
			std::fprintf(stderr, "E2E_SCENE_RENDER_FAIL: scene did not open\n");
			std::fflush(stderr);
			QCoreApplication::exit(2);
			return;
		}
		runScene();
		if (!m_worker) {
			std::fprintf(stderr, "E2E_SCENE_RENDER_FAIL: scene run did not start\n");
			std::fflush(stderr);
			QCoreApplication::exit(2);
			return;
		}
	}

	if (allTrains.isEmpty() && m_e2eAttempts < 40) {
		++m_e2eAttempts;
		QTimer::singleShot(500, this, &MainWindow::runSceneRenderE2E);
		return;
	}
	m_e2eFinished = true;

	bool ok = true;
	QStringList failures;

	if (allTrains.isEmpty()) {
		ok = false;
		failures << "no train items rendered";
	}

	if (scene->items().size() < 10) {
		ok = false;
		failures << "scene has too few items";
	}

	QRectF bounds = scene->itemsBoundingRect();
	if (bounds.width() < 100.0) {
		ok = false;
		failures << "network geometry did not spread horizontally";
	}

	// the items existing is not enough; they must be inside the viewport
	QRectF visible = networkView->mapToScene(networkView->viewport()->rect()).boundingRect();
	if (!visible.intersects(bounds)) {
		ok = false;
		failures << "network geometry is outside the viewport";
	}

	QString screenshotPath = qEnvironmentVariable("QEGTRAIN_E2E_SCREENSHOT");
	if (!screenshotPath.isEmpty()) {
		QPixmap image = networkView->grab();
		if (!image.save(screenshotPath)) {
			ok = false;
			failures << "screenshot save failed";
		}
	}

	if (ok) {
		std::fprintf(stdout, "E2E_SCENE_RENDER_OK\n");
		std::fflush(stdout);
		if (m_worker && m_workerThread && m_workerThread->isRunning()) {
			connect(m_workerThread, &QThread::finished, qApp, []() {
				QCoreApplication::exit(0);
			});
			m_worker->requestStop();
			return;
		}
		QCoreApplication::exit(0);
		return;
	}

	std::fprintf(stderr, "E2E_SCENE_RENDER_FAIL: %s\n", failures.join(", ").toStdString().c_str());
	std::fflush(stderr);
	if (m_worker && m_workerThread && m_workerThread->isRunning()) {
		connect(m_workerThread, &QThread::finished, qApp, []() {
			QCoreApplication::exit(2);
		});
		m_worker->requestStop();
		return;
	}
	QCoreApplication::exit(2);
}

void MainWindow::runEditorSmokeE2E() {
	if (m_editorE2eFinished)
		return;
	m_editorE2eFinished = true;

	bool ok = true;
	QStringList failures;

	// step a: load scene from env
	QString scenePath = qEnvironmentVariable("QEGTRAIN_E2E_SCENE");
	if (scenePath.isEmpty()) {
		ok = false;
		failures << "no scene path";
	} else {
		bool opened = openSceneDirectory(scenePath);
		if (!opened || !m_sceneLoaded) {
			ok = false;
			failures << "scene did not open";
		} else {
			bool reopened = openSceneDirectory(scenePath);
			if (!reopened || !m_sceneLoaded || QDir(m_sceneDir).absolutePath() != QDir(scenePath).absolutePath()) {
				ok = false;
				failures << "scene did not reopen";
			}
			if (!allTrains.isEmpty() || !allArcs.isEmpty()) {
				ok = false;
				failures << "legacy scene state not cleared";
			}
			QString alternateScenePath = qEnvironmentVariable("QEGTRAIN_E2E_SCENE_ALT");
			if (!alternateScenePath.isEmpty()
					&& QDir(alternateScenePath).absolutePath() != QDir(scenePath).absolutePath()) {
				bool switched = openSceneDirectory(alternateScenePath);
				if (!switched || !m_sceneLoaded
						|| QDir(m_sceneDir).absolutePath() != QDir(alternateScenePath).absolutePath()) {
					ok = false;
					failures << "alternate scene did not open";
				}
				if (!allTrains.isEmpty() || !allArcs.isEmpty()) {
					ok = false;
					failures << "legacy scene state not cleared on alternate";
				}
				bool restored = openSceneDirectory(scenePath);
				if (!restored || !m_sceneLoaded) {
					ok = false;
					failures << "scene did not reopen after alternate";
				}
			}
			if (QDir(m_sceneDir).absolutePath() != QDir(scenePath).absolutePath()) {
				ok = false;
				failures << "primary scene not restored";
			}
		}
	}

	// step b: validation assertions
	if (m_sceneLoaded) {
		if (!m_loadedDataTree || m_loadedDataTree->topLevelItemCount() <= 0) {
			ok = false;
			failures << "loaded data tree empty";
		} else if (m_loadedDataTree->topLevelItem(0)->childCount() <= 0) {
			ok = false;
			failures << "loaded data tree lacks drilldown rows";
		}
		if (hasErrors(m_sceneDiagnostics)) {
			ok = false;
			failures << "validation errors on open";
		}
		if (!m_compositionListWidget || m_compositionListWidget->count() <= 0) {
			ok = false;
			failures << "compositions pane empty";
		}
		if (!m_serviceListWidget || m_serviceListWidget->count() <= 0) {
			ok = false;
			failures << "services pane empty";
		}
	}

	// step c: minimal scene operation
	if (m_sceneLoaded && m_compositionListWidget) {
		int before = m_compositionListWidget->count();
		addComposition();
		QApplication::processEvents();
		if (!m_sceneDirty) {
			ok = false;
			failures << "edit did not mark dirty";
		}
		if (m_compositionListWidget->count() != before + 1) {
			ok = false;
			failures << "add composition did not apply";
		}
	}

	// step d: save
	if (m_sceneLoaded) {
		QString outBase = qEnvironmentVariable("QEGTRAIN_E2E_OUT");
		QTemporaryDir tmpDir;
		if (outBase.isEmpty())
			outBase = tmpDir.path();
		QString outScenePath = QDir(outBase).filePath("editor_smoke_scene");
		auto result = ::saveScene(m_sceneModel, outScenePath.toStdString());
		if (!result.success()) {
			ok = false;
			failures << "save failed";
		}
	}

	if (ok) {
		std::fprintf(stdout, "E2E_EDITOR_SMOKE_OK\n");
		std::fflush(stdout);
		QCoreApplication::exit(0);
		return;
	}
	std::fprintf(stderr, "E2E_EDITOR_SMOKE_FAIL: %s\n", failures.join(", ").toStdString().c_str());
	std::fflush(stderr);
	QCoreApplication::exit(1);
}

void MainWindow::clearSimulationWorker(bool requestStop) {
	if (m_worker && requestStop)
		m_worker->requestStop();
	if (m_workerThread && m_workerThread->isRunning()) {
		m_workerThread->quit();
		m_workerThread->wait(3000);
	}
	m_worker = nullptr;
	m_workerThread = nullptr;
}

void MainWindow::stopTrainAnimation(int train) {
	QVariantAnimation* animation = m_trainAnimations.take(train);
	if (!animation)
		return;

	animation->stop();
	animation->deleteLater();
}

void MainWindow::stopTrainAnimations() {
	for (QVariantAnimation* animation : m_trainAnimations) {
		if (!animation)
			continue;
		animation->stop();
		animation->deleteLater();
	}
	m_trainAnimations.clear();
}

// setup GUI
void MainWindow::setupGUI() {

	// initialize qpoints
	QPointF pt, pt_prev, ptc, pts, pte;

	//// MANUAL DRAWING - LONDON

	//// list of tracks at London Waterloo
	// vector<int> WAT = { 22,23,24,25,26,27,18,28,29,30,31,32,19,33,20,21 };

	//// check first/last switch for non-main tracklines
	// double beginX, endX;
	// for (int i = 0; i != WAT.size(); i++) {
	//	beginX = DBL_MAX;
	//	endX = -1;

	//	for (int c = 0; c < numConnections; c++) {
	//		if (connections[c].idFirstTrackLine == WAT[i]) {
	//			if (WAT[i] == 20 && connections[c].xFirstNode < beginX) { beginX = connections[c].xFirstNode; }
	//			if (connections[c].xFirstNode > endX) { endX = connections[c].xFirstNode; }
	//		}
	//		else if (connections[c].idSecondTrackLine == WAT[i]) {
	//			if (WAT[i] == 20 && connections[c].xSecondNode < beginX) { beginX = connections[c].xSecondNode; }
	//			if (connections[c].xSecondNode > endX) { endX = connections[c].xSecondNode; }
	//		}
	//	}

	//	if (WAT[i] == 20) { blockSets[WAT[i]].firstSwitchX = beginX; } // just for track 20
	//	blockSets[WAT[i]].lastSwitchX = endX;

	//}

	//// assign levels manually
	// blockSets[3].graphID = 0;
	// blockSets[22].graphID = 1; //blockSets[22].member[3].end_node.drawingEnd = 0;
	// blockSets[1].graphID = 2;
	// blockSets[23].graphID = 3; //blockSets[23].member[3].end_node.drawingEnd = 0;
	// blockSets[24].graphID = 4; //blockSets[24].member[6].end_node.drawingEnd = 0;
	// blockSets[25].graphID = 5; //blockSets[25].member[3].end_node.drawingEnd = 0;
	// blockSets[2].graphID = 6;
	// blockSets[26].graphID = 7; //blockSets[26].member[4].end_node.drawingEnd = 0;
	// blockSets[27].graphID = 8; //blockSets[27].member[4].end_node.drawingEnd = 0;
	// blockSets[18].graphID = 9; //blockSets[18].member[30].end_node.drawingEnd = 0;
	// blockSets[28].graphID = 10; //blockSets[28].member[3].end_node.drawingEnd = 0;
	// blockSets[29].graphID = 11; //blockSets[29].member[5].end_node.drawingEnd = 0;
	// blockSets[30].graphID = 12; //blockSets[30].member[3].end_node.drawingEnd = 0;
	// blockSets[0].graphID = 13;
	// blockSets[31].graphID = 14; //blockSets[31].member[4].end_node.drawingEnd = 0;
	// blockSets[32].graphID = 15; //blockSets[32].member[8].end_node.drawingEnd = 0;
	// blockSets[19].graphID = 16; //blockSets[19].member[16].end_node.drawingEnd = 0;
	// blockSets[33].graphID = 17; //blockSets[33].member[4].end_node.drawingEnd = 0;
	// blockSets[20].graphID = 18; //blockSets[20].member[14].end_node.drawingEnd = 0; for (int i = 0; i < 9; i++) { blockSets[20].member[i].end_node.drawingBegin = 0; }
	// blockSets[21].graphID = 19; //blockSets[21].member[13].end_node.drawingEnd = 0;

	// blockSets[4].graphID = 14; blockSets[5].graphID = 7; blockSets[6].graphID = 15; blockSets[7].graphID = 7; blockSets[8].graphID = 12; blockSets[9].graphID = 5; blockSets[10].graphID = 15; blockSets[11].graphID = 8; blockSets[12].graphID = 3;
	// blockSets[13].graphID = -1; blockSets[14].graphID = 15; blockSets[15].graphID = 7; blockSets[16].graphID = -1; blockSets[17].graphID = -2; blockSets[34].graphID = 14; blockSets[35].graphID = -1; blockSets[36].graphID = 14; blockSets[37].graphID = 7;
	// blockSets[38].graphID = 14; blockSets[39].graphID = 11; blockSets[40].graphID = 10; blockSets[41].graphID = 3; blockSets[42].graphID = -1; blockSets[43].graphID = -2; blockSets[44].graphID = 8; blockSets[45].graphID = 9;

	// MANUAL DRAWING - CASE STUDY
	setGraphIDsCaseStudy();

	//// AUTOMATIC DRAWING (set levels to avoid overlaps)
	//// assign graphical levels
	// int max_level = 0; // last graphical level
	// int assign = 0; // level assigned

	//// assign first trackline to level 0
	// blockSets[0].graphID = 0;

	//// assign levels for all tracklines (except first)
	// for (int track = 1; track < numTrackLines; track++) {

	//	assign = 0;

	//	// start with level 0
	//	blockSets[track].graphID = 0;

	//	// find if level is available
	//	while (assign == 0) {
	//		for (int id = 0; id < numTrackLines; id++) {
	//			if (id == numTrackLines - 1) {
	//				// no overlap with all tracklines
	//				assign = 1;
	//				// update max_level
	//				if (blockSets[track].graphID > max_level)
	//					max_level = blockSets[track].graphID;
	//			}
	//			else if (track == id) // ignore same track
	//				continue;
	//			else if (blockSets[id].graphID == blockSets[track].graphID) { // same level
	//				// check if x coordinates overlap
	//				// ((track_x_end < id_x_start) || (track_x_start > id_x_end))
	//				if ((blockSets[track].member[blockSets[track].len-1].end_node.X < blockSets[id].member[0].start_node.X) ||
	//					(blockSets[track].member[0].start_node.X > blockSets[id].member[blockSets[id].len-1].end_node.X)) {
	//					// no overlap
	//					continue;
	//				}
	//				else { // overlap -> try next level
	//					blockSets[track].graphID++;
	//					break; // start from beginning with new level
	//				}
	//			}
	//		}
	//	}
	//}

	// calculate screen coordinates and shifts for each station (graphical levels)
	calculateStationCoordAndShift(geo_scale);

	// draw station icons and print names
	for (int i = 0; i < numStations; i++) {
		// ignore virtual stations (name contains "virtual")
		if (StationArray[i].stationName.find("virtual") != std::string::npos) {
			continue;
		}

		// use avg shift
		double avgShiftX = 0, avgShiftY = 0;
		for (int j = 0; j < StationArray[i].regions.size(); j++) {
			avgShiftX += StationArray[i].shiftX[StationArray[i].regions[j]];
			avgShiftY += StationArray[i].shiftY[StationArray[i].regions[j]];
		}
		avgShiftX /= StationArray[i].regions.size();
		avgShiftY /= StationArray[i].regions.size();

		// shifted point to print station name
		if ((StationArray[i].stationName == "Koge") ||
			(StationArray[i].stationName == "Olby") ||
			(StationArray[i].stationName == "KogeNord") ||
			(StationArray[i].stationName == "Jersie") ||
			(StationArray[i].stationName == "SolrodStrand") ||
			(StationArray[i].stationName == "Karlslunde") ||
			(StationArray[i].stationName == "Greve") ||
			(StationArray[i].stationName == "Hundige") ||
			(StationArray[i].stationName == "Ishoj") ||
			(StationArray[i].stationName == "Vallensbaek") ||
			(StationArray[i].stationName == "BrondbyStrand") ||
			(StationArray[i].stationName == "Avedore") ||
			(StationArray[i].stationName == "Brondbyoster") ||
			(StationArray[i].stationName == "Frihedem") ||
			(StationArray[i].stationName == "Amarken") ||
			(StationArray[i].stationName == "Sydhavn") ||
			(StationArray[i].stationName == "Sjaelor") ||
			(StationArray[i].stationName == "Friheden") ||
			(StationArray[i].stationName == "BallerupStorage") ||

			(StationArray[i].stationName == "NyEllebjergAE")

		) {
			pt.setX(StationArray[i].graphX + avgShiftX * station_name_graphID * track_separation);
			pt.setY(900 + StationArray[i].graphY + avgShiftY * station_name_graphID * track_separation);
		} else if ((StationArray[i].stationName == "Frederikssund") ||
				   (StationArray[i].stationName == "Vinge") ||
				   (StationArray[i].stationName == "Olstykke") ||
				   (StationArray[i].stationName == "Egedal") ||
				   (StationArray[i].stationName == "Stenlose") ||
				   (StationArray[i].stationName == "Vekso") ||
				   (StationArray[i].stationName == "Olstykke") ||
				   (StationArray[i].stationName == "Kildedal") ||
				   (StationArray[i].stationName == "Malov") ||
				   (StationArray[i].stationName == "Ballerup") ||
				   (StationArray[i].stationName == "Malmparken") ||
				   (StationArray[i].stationName == "Skovlunde") ||
				   (StationArray[i].stationName == "Herlev") ||
				   (StationArray[i].stationName == "Husum") ||
				   (StationArray[i].stationName == "Islev") ||
				   (StationArray[i].stationName == "Jyllingevej") ||
				   (StationArray[i].stationName == "Vanlose") ||
				   (StationArray[i].stationName == "FlintholmCH") ||
				   (StationArray[i].stationName == "PeterBangsvej") ||
				   (StationArray[i].stationName == "Langgade") ||
				   (StationArray[i].stationName == "Valby") ||
				   (StationArray[i].stationName == "Carlsberg") ||
				   (StationArray[i].stationName == "KobenhavnH") ||
				   (StationArray[i].stationName == "Vesterport") ||
				   (StationArray[i].stationName == "Norreport") ||
				   (StationArray[i].stationName == "Osterport_t") ||
				   (StationArray[i].stationName == "Osterport") ||
				   (StationArray[i].stationName == "Nordhavn") ||

				   (StationArray[i].stationName == "Dybbolsbro")

		) {
			pt.setX(StationArray[i].graphX + avgShiftX * station_name_graphID * track_separation);
			pt.setY(-920 + StationArray[i].graphY + avgShiftY * station_name_graphID * track_separation);
		} else if ((StationArray[i].stationName == "HojeTaastrup") ||
				   (StationArray[i].stationName == "Taastrup") ||
				   (StationArray[i].stationName == "Glostrup") ||

				   (StationArray[i].stationName == "Rodovre") ||
				   (StationArray[i].stationName == "Hvidovre") ||
				   (StationArray[i].stationName == "DanshojBBx") ||
				   (StationArray[i].stationName == "DanshojBBx") ||
				   (StationArray[i].stationName == "Albertslund")) {
			pt.setX(StationArray[i].graphX + avgShiftX * station_name_graphID * track_separation);
			pt.setY(-2800 + StationArray[i].graphY + avgShiftY * station_name_graphID * track_separation);
		} else if ( // line to Hellerup
			(StationArray[i].stationName == "DanshojF") ||
			(StationArray[i].stationName == "VigerslevAlle") ||
			(StationArray[i].stationName == "Alholm") ||
			(StationArray[i].stationName == "KBHallen") ||
			(StationArray[i].stationName == "FlintholmF") ||
			(StationArray[i].stationName == "Grondal") ||
			(StationArray[i].stationName == "Fuglebakken") ||
			(StationArray[i].stationName == "Norrebro") ||
			(StationArray[i].stationName == "Bispebjerg") ||
			(StationArray[i].stationName == "Charlottenlund") ||
			(StationArray[i].stationName == "Ordrup") ||
			(StationArray[i].stationName == "Hellerup") ||
			(StationArray[i].stationName == "HellerupStorage") ||
			(StationArray[i].stationName == "RyparkenF") ||
			(StationArray[i].stationName == "Klampenborg") ||
			(StationArray[i].stationName == "NyEllebjergF")) {
			pt.setX(StationArray[i].graphX + avgShiftX * station_name_graphID * track_separation);
			pt.setY(-3500 + StationArray[i].graphY + avgShiftY * station_name_graphID * track_separation);
		} else if ( // line to Hillerod
			(StationArray[i].stationName == "Bernstorffsvej") ||
			(StationArray[i].stationName == "Gentofte") ||
			(StationArray[i].stationName == "Lyngby") ||
			(StationArray[i].stationName == "Sorgenfri") ||
			(StationArray[i].stationName == "Virum") ||
			(StationArray[i].stationName == "Holte") ||
			(StationArray[i].stationName == "Birkerod") ||
			(StationArray[i].stationName == "Allerod") ||
			(StationArray[i].stationName == "Hillerod")) {
			pt.setX(StationArray[i].graphX + avgShiftX * station_name_graphID * track_separation);
			pt.setY(-2200 + StationArray[i].graphY + avgShiftY * station_name_graphID * track_separation);
		} else {
			pt.setX(StationArray[i].graphX + avgShiftX * station_name_graphID * track_separation);
			pt.setY(StationArray[i].graphY + avgShiftY * station_name_graphID * track_separation);
		}

		// print station name
		paintStationName(pt, StationArray[i].stationName, station_size);

		// shifted point to draw icon (draw icon above the text)
		pt.setY(pt.y() - station_size / 2);

		// draw station icon
		paintStationIcon(pt, station_size);
	}

	// draw connections
	QPointF station1, station2;
	int st_index;
	QPointF ptc1, ptc2;
	for (int c = 0; c < numConnections; c++) {
		// get shifted connections
		egtrainPoint2Screen(&connections[c], connections[c].idFirstTrackLine, connections[c].idSecondTrackLine, track_separation);
		ptc1.setX(connections[c].graphXFirstNode);
		ptc1.setY(connections[c].graphYFirstNode);
		ptc2.setX(connections[c].graphXSecondNode);
		ptc2.setY(connections[c].graphYSecondNode);

		paintConnection(ptc1, ptc2, line_width, &connections[c]);
	}

	// draw tracklines
	for (int track = 0; track < numTrackLines; track++) {
		// run over all arcs
		for (int i = 0; i < blockSets[track].len; i++) {
			// get shifted nodes
			egtrainPoint2Screen(&blockSets[track].member[i].startNode, track, track_separation);
			egtrainPoint2Screen(&blockSets[track].member[i].endNode, track, track_separation);
			pt.setX(blockSets[track].member[i].startNode.graphX);
			pt.setY(blockSets[track].member[i].startNode.graphY);
			pte.setX(blockSets[track].member[i].endNode.graphX);
			pte.setY(blockSets[track].member[i].endNode.graphY);

			// -----------------------------------------------------------------------
			// London Waterloo manual drawing
			// if (blockSets[track].member[i].startNode.X < blockSets[track].firstSwitchX) { pt_prev = pt; continue; }
			// if (blockSets[track].member[i].startNode.X > blockSets[track].lastSwitchX) { break; }
			// -----------------------------------------------------------------------

			// draw nodes
			if (i > 0 && !empty(blockSets[track].member[i - 1].endNode.stationName)) { // station Node (only visible from end nodes [end_node] -> i>1)
				paintStationNode(pt, station_node_size, line_width, track, &blockSets[track].member[i].startNode);
				if (initial_variables.PAX_GUI) {
					paintStationPlatform(pt, station_node_size, line_width, &blockSets[track].member[i - 1].endNode);
				}
			}
			// if it is a double switch then do not paint nodes Copenhagen case
			else if (blockSets[track].numNodes == 4) {
				paintNode(pt, static_cast<int>(0.1), line_width, track, &blockSets[track].member[i].startNode);
			} else {
				paintNode(pt, node_size, line_width, track, &blockSets[track].member[i].startNode);
			}

			// -----------------------------------------------------------------------
			// London Waterloo manual drawing
			// if (i > 0 && blockSets[track].member[i - 1].startNode.X < blockSets[track].firstSwitchX) { pt_prev = pt; continue; }
			// -----------------------------------------------------------------------

			// draw arcs
			if (i > 0) {
				// for the Copenhagen case double switches
				if ((blockSets[track].numNodes == 4) && (InputMainFolder == "Input/Input_EGTRAIN_Banedanmark")) {
					if (i == 2)
						paintArc(pt_prev, pt, line_width, track, &blockSets[track].member[i - 1], track_separation);

				} else
					paintArc(pt_prev, pt, line_width, track, &blockSets[track].member[i - 1], track_separation);
			}

			// draw last Arc and last Node - end_node Node needed
			if (i == (blockSets[track].len - 1)) {
				// draw last Arc
				if ((blockSets[track].numNodes == 4) && (InputMainFolder == "Input/Input_EGTRAIN_Banedanmark")) {
					continue;
				} else
					paintArc(pt, pte, line_width, track, &blockSets[track].member[i], track_separation);

				// draw last end_node Node
				if (!empty(blockSets[track].member[i].endNode.stationName)) { // station Node
					paintStationNode(pte, station_node_size, line_width, track, &blockSets[track].member[i].endNode);
					if (initial_variables.PAX_GUI) {
						paintStationPlatform(pte, station_node_size, line_width, &blockSets[track].member[i].endNode);
					}
				}
				// if it is a double switch then do not paint nodes /Copenhagen case
				else if ((blockSets[track].numNodes == 4) && (InputMainFolder == "Input/Input_EGTRAIN_Banedanmark")) {
					paintNode(pte, static_cast<int>(0.1), line_width, track, &blockSets[track].member[i].endNode);
				} else {
					paintNode(pte, node_size, line_width, track, &blockSets[track].member[i].endNode);
				}
			}

			pt_prev = pt;
		}
	}

	// draw signalling
	for (int i = 0; i < Blocks; i++) {
		// draw signal
		if (signalling_block_sections[i].trackLineId != -1) { // compound signalling_block_sections are not drawn

			// -----------------------------------------------------------------------
			// London Waterloo manual drawing
			// if (signalling_block_sections[i].start_node.X < blockSets[signalling_block_sections[i].trackLineId].firstSwitchX || signalling_block_sections[i].start_node.X > blockSets[signalling_block_sections[i].trackLineId].lastSwitchX) { continue; }
			// -----------------------------------------------------------------------

			// ignore first signal of trackline
			if ((i == 0) || (signalling_block_sections[i].trackLineId != signalling_block_sections[i - 1].trackLineId)) {
				continue;
			}

			// ignore 'virtual' signals (in the middle of double switches)
			if (signalling_block_sections[i].start_node.virtualSignal) {
				continue;
			}

			// draw signals at beggining of block section
			paintSignal(signalling_block_sections[i].start_node.X, node_size, line_width, signalling_block_sections[i].trackLineId, track_separation, i);
		}
	}

	buildSignalIndex();

	// hide objects from unused tracklines
	hideUnusedTracks();

	// draw virtual inter region connections
	drawVirtualInterRegionConnections();

	refreshFollowTrainChoices();

	// hide unused menus
	// ui->menuFile->setTitle("");
	// View menu is populated from the .ui file
	// ui->menuTools->setTitle("");
	// ui->menuAbout->setTitle("");
}

// open GUI
void MainWindow::openGUI() {
	// display window maximized
	showMaximized();

	// fit view to window
	fitView();
}

void MainWindow::showEvent(QShowEvent* e) {
	QMainWindow::showEvent(e);
	if (m_promptedLoad)
		return;
	m_promptedLoad = true;
	extern InitialParameters initial_variables;
	// skip dialog when -n was given so scripted runs load directly
	if (!initial_variables.nArgProvided)
		QTimer::singleShot(0, this, &MainWindow::actionLoad_Network);

	// verification hook: auto-start the simulation when QEGTRAIN_AUTOSTART is set
	if (qEnvironmentVariableIsSet("QEGTRAIN_AUTOSTART"))
		QTimer::singleShot(1500, this, &MainWindow::startSimulation);
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_VISUAL_POLISH"))
		QTimer::singleShot(2600, this, &MainWindow::runVisualPolishE2E);
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_EDITOR_SMOKE"))
		// let the startup case load and the docks settle before the smoke runs
		QTimer::singleShot(1000, this, &MainWindow::runEditorSmokeE2E);
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_SCENE_RUN"))
		QTimer::singleShot(1000, this, &MainWindow::runSceneRenderE2E);
}

// starts EGTRAIN simulation on a worker thread
void MainWindow::startSimulation() {
	if (m_worker)
		return; // already running

	if (m_sceneLoaded) {
		refreshValidationPanel();
		if (hasErrors(m_sceneDiagnostics)) {
			int errorCount = countDiagnostics(m_sceneDiagnostics).errors;
			showBlockingError(this, "Cannot Run Simulation",
							  QString("The scene has %1 validation error(s) that must be fixed before running.").arg(errorCount), true);
			return;
		}
	}

	// show progress bar
	progressBar->show();
	statusBar()->showMessage("Simulation running...");

	// create worker and thread; sync slider before run() starts
	m_worker = new SimulationWorker();
	m_worker->setDelayMs(m_speedSlider->value());
	m_workerThread = new QThread(this);
	m_worker->moveToThread(m_workerThread);

	// when the thread starts, begin the simulation
	connect(m_workerThread, &QThread::started, m_worker, &SimulationWorker::run);
	// when simulation finishes on the worker, handle results on main thread
	connect(m_worker, &SimulationWorker::simulationFinished, this, &MainWindow::onSimulationFinished);
	// clean up when thread finishes
	connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
	connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);

	m_workerThread->start();
}

// handle simulation completion on the main thread
void MainWindow::onSimulationFinished() {
	// print last services
	simulation.printLastTrainServicePathDiagram();

	// hide progress bar
	progressBar->hide();
	statusBar()->showMessage("Simulation complete");
	ui->actionSimulationPause->setText("Pause");
	ui->actionSimulationPause->setChecked(false);

	// simulation finished - ask if user wants to see train path diagram
	if (!qEnvironmentVariableIsSet("QEGTRAIN_E2E_VISUAL_POLISH"))
		askForTrainPathDiagram();

	// cleanup thread
	clearSimulationWorker(false);
}

void MainWindow::teardownGUI() {
	// Stop any running simulation before clearing scene objects it may reference.
	clearSimulationWorker(true);

	stopTrainAnimations();

	// Clearing the scene deletes all owned QGraphicsItems.
	scene->clear();

	// clear() keeps the explicitly pinned scene rect, so fitView would keep
	// fitting the previous network's extents; unset it to track the new items
	scene->setSceneRect(QRectF());

	// Clear list pointers - the items were owned by the scene and are now deleted.
	m_trainSpeedLabels.clear(); // items deleted by scene->clear() above
	m_prevTrainPositions.clear();
	allTrains.clear();
	allSignals.clear();
	m_signalsByAheadId.clear();
	allArcs.clear();
	allPlatforms.clear();
	trainPaxInfoItem = nullptr;
	trainPaxItem = nullptr;
	paxIconInfoItem = nullptr;
	paxIconItem = nullptr;
	regionStations.clear();
	virtualInterRegionConnections.clear();
	m_followTrainIndex = -1;
	m_e2eAttempts = 0;
	m_e2eFinished = false;
	if (m_followAction)
		m_followAction->setChecked(false);
	if (m_followTrainCombo)
		m_followTrainCombo->clear();

	// scene->clear() deleted the QGraphicsItemGroup* objects; clear the dangling pointer list.
	extern QList<QGraphicsItemGroup*> VCmsgItems;
	VCmsgItems.clear();
}

void MainWindow::chooseOutputFolder() {
	extern InitialParameters initial_variables;
	QString dir = QFileDialog::getExistingDirectory(this, "Choose Output Folder",
													QString::fromStdString(initial_variables.OutputMainFolder));
	if (dir.isEmpty())
		return;
	QDir().mkpath(dir);
	initial_variables.OutputMainFolder = dir.toStdString();
	statusBar()->showMessage(QString("Output folder: %1").arg(dir), 4000);
}

void MainWindow::setStartTime() {
	bool ok = false;
	QString current = QString::fromStdString(formatSimTime(0, m_startOffsetSeconds)).left(5);
	QString text = QInputDialog::getText(this, "Start Time",
										 "Simulation start time (HH:MM):", QLineEdit::Normal, current, &ok);
	if (!ok)
		return;
	long long secs = parseClockToSeconds(text.toStdString());
	if (secs < 0) {
		QMessageBox::warning(this, "Invalid Time", "Please enter the time as HH:MM, for example 08:30.");
		return;
	}
	m_startOffsetSeconds = secs;
	statusBar()->showMessage(QString("Start time set to %1").arg(text), 3000);
}

void MainWindow::runScene() {
	extern InitialParameters initial_variables;
	if (!m_sceneLoaded)
		return;

	refreshValidationPanel();
	if (hasErrors(m_sceneDiagnostics)) {
		int errorCount = countDiagnostics(m_sceneDiagnostics).errors;
		showBlockingError(this, "Cannot Run Scene",
						  QString("The scene has %1 validation error(s) that must be fixed before running.").arg(errorCount), true);
		return;
	}

	// Stage into a local dir first; only swap it into m_runStagingDir after the
	// running simulation is torn down, so a live worker's input/output files are
	// never deleted out from under it and a failed run leaves nothing locked.
	auto* stagingDir = new QTemporaryDir();
	if (!stagingDir->isValid()) {
		delete stagingDir;
		showBlockingError(this, "Cannot Run Scene", "Could not create a temporary staging directory for the run.");
		return;
	}

	QString sceneStagingPath = QDir(stagingDir->path()).absoluteFilePath("scene");
	QString inputStagingPath = QDir(stagingDir->path()).absoluteFilePath("input");

	SceneSaveResult saveResult = ::saveScene(m_sceneModel, sceneStagingPath.toStdString());
	if (!saveResult.success()) {
		QString message = firstDiagnosticMessage(saveResult.diagnostics);
		if (message.isEmpty())
			message = "Scene could not be staged for the run.";
		delete stagingDir;
		showBlockingError(this, "Cannot Run Scene", message);
		return;
	}

	if (!m_sceneDir.isEmpty()) {
		QString sourceLegacy = QDir(m_sceneDir).filePath("legacy");
		if (QDir(sourceLegacy).exists()) {
			QString targetLegacy = QDir(sceneStagingPath).filePath("legacy");
			if (!copyDirectoryRecursively(sourceLegacy, targetLegacy)) {
				delete stagingDir;
				showBlockingError(this, "Cannot Run Scene", "Cannot copy legacy passthrough data for the run.");
				return;
			}
		}
	}

	SceneExportResult exportResult = exportLegacyScene(sceneStagingPath.toStdString(), inputStagingPath.toStdString());
	if (!exportResult.success()) {
		QString message = firstDiagnosticMessage(exportResult.diagnostics);
		if (message.isEmpty())
			message = "Scene could not be exported for the run.";
		delete stagingDir;
		showBlockingError(this, "Cannot Run Scene", message);
		return;
	}

	statusBar()->showMessage("Preparing scene simulation...");
	QApplication::processEvents();

	teardownGUI();
	simulation.resetState();
	delete m_runStagingDir;
	m_runStagingDir = stagingDir;

	// set globals for the scene run (resetState just zeroed them):
	QString absInput = QDir(inputStagingPath).absolutePath();
	InputMainFolder = absInput.toStdString();					// GLOBAL (Infrastructure.h)
	initial_variables.InputMainFolder = absInput.toStdString(); // member, used by setupEgtrain for Connections/trainNames
	QString absOutput = QDir(m_runStagingDir->path()).absoluteFilePath("output");
	QDir().mkpath(absOutput);
	initial_variables.OutputMainFolder = absOutput.toStdString();
	initial_variables.GUI = 1;
	numTrackLines = countTrackLineDirs(absInput);			 // GLOBAL
	N_Routes = static_cast<int>(m_sceneModel.routes.size()); // GLOBAL
	bufferTime = 0;
	recoveryTimePercentage = 0;					// GLOBALS
	train_route = std::vector<Route>(N_Routes); // GLOBAL, size like main.cpp
	initial_variables.startingSimulationTime = baseTimeToSeconds(m_sceneModel.baseTime);
	initial_variables.times = computeHorizon(m_sceneModel);

	simulation.setupEgtrain();
	readStationInfo(); // main.cpp does this at startup; reloads skip it otherwise
	simulation.prepareSimulation();
	setupGUI();
	fitView(); // the previous case's viewport rarely covers the new geometry

	// scene stays loaded; only the case-study load path clears it
	updateSceneWindowTitle();
	updateSceneActions();

	startSimulation();
	statusBar()->showMessage(QString("Running scene: %1").arg(QString::fromStdString(m_sceneModel.name)));
}

void MainWindow::actionLoad_Network() {
	extern InitialParameters initial_variables;
	if (!maybeSaveScene())
		return;

	QDialog dlg(this);
	dlg.setWindowTitle("Select Case Study");
	dlg.setMinimumWidth(320);

	auto* layout = new QVBoxLayout(&dlg);
	layout->addWidget(new QLabel("Choose a case study to load:", &dlg));
	layout->addSpacing(8);

	struct CaseStudy {
		int id;
		const char* name;
		const char* description;
	};
	const CaseStudy cases[] = {
		{1, "Netherlands", "268 track sections, 74 routes"},
		{2, "Paimpol", "6 track sections, 15 routes (French branch line)"},
		{3, "Copenhagen", "168 track sections, 24 routes (S-Bane metro)"},
		{4, "Milano-Brescia", "38 track sections, 48 routes"},
	};

	QButtonGroup* group = new QButtonGroup(&dlg);
	for (const auto& cs : cases) {
		auto* rb = new QRadioButton(
			QString("%1 - %2").arg(cs.name).arg(cs.description), &dlg);
		rb->setProperty("caseId", cs.id);
		if (cs.id == 1)
			rb->setChecked(true);
		group->addButton(rb);
		layout->addWidget(rb);
	}

	layout->addSpacing(8);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

	if (dlg.exec() != QDialog::Accepted)
		return;

	int chosen = 1;
	for (QAbstractButton* btn : group->buttons()) {
		if (btn->isChecked()) {
			chosen = btn->property("caseId").toInt();
			break;
		}
	}

	statusBar()->showMessage("Loading...");
	QApplication::processEvents();

	teardownGUI();
	initial_variables.set_case(chosen);
	initial_variables.GUI = 1; // ensure GUI stays enabled regardless of case defaults

	InputCheckResult check = validateCaseStudyInput(initial_variables.InputMainFolder);
	if (!check.ok) {
		QMessageBox::critical(this, "Cannot Load Network", QString::fromStdString(check.message));
		return;
	}

	simulation.resetState();
	simulation.setupEgtrain();
	readStationInfo(); // main.cpp does this at startup; reloads skip it otherwise
	simulation.prepareSimulation();
	setupGUI();
	fitView(); // the previous case's viewport rarely covers the new geometry
	m_sceneDir.clear();
	m_sceneModel = SceneModel();
	m_sceneLoaded = false;
	m_sceneDirty = false;
	updateSceneWindowTitle();
	updateSceneActions();

	statusBar()->showMessage(QString("Loaded: %1 (%2 tracks, %3 routes)")
								 .arg(QString::fromStdString(initial_variables.name))
								 .arg(initial_variables.numTrackLines)
								 .arg(initial_variables.N_Routes));
}

// draws a Node
void MainWindow::paintNode(QPointF coord, int size, int pen_width, int track, Node* Node) {
	QPen pen = QPen(Qt::lightGray);
	pen.setWidth(0);
	pen.setCosmetic(true);

	// draws using rectangle with center on top-left corner (center_x,center_y,width,height)
	QRectF rect = QRectF(0, 0, size, size);
	rect.moveCenter(coord);

	NodeItem* el = new NodeItem(rect);
	el->setPen(pen);
	el->setBrush(Qt::lightGray);

	// add track and Node pointer to ellipse item
	el->track = track;
	el->node = Node;

	// add Node to scene
	scene->addItem(el);
}

// draws a station Node
void MainWindow::paintStationNode(QPointF coord, int size, int pen_width, int track, Node* Node) {
	bool hasPlatformId = Node && !Node->stationPlatformId.empty() && Node->stationPlatformId != "None";
	StationVisual visual = classifyStation(hasPlatformId, Node ? Node->numConnections : 0);
	QPen pen = QPen(visual.outline);
	pen.setWidth(0);
	pen.setCosmetic(true);

	// draws using rectangle with center on top-left corner (center_x,center_y,width,height)
	QRectF rect = QRectF(0, 0, size, size);
	rect.moveCenter(coord);

	StationNodeItem* el = new StationNodeItem(rect);
	el->setPen(pen);
	el->setBrush(visual.fill);

	// add track and Node pointer to rect item
	el->track = track;
	el->node = Node;

	// add station Node to scene
	scene->addItem(el);
}

// draws a station icon above the track
void MainWindow::paintStationIcon(QPointF coord, int size) {
	IconItem* item = new IconItem(station_pixmap.scaled(QSize(size, size), Qt::KeepAspectRatio, Qt::SmoothTransformation));
	scene->addItem(item);

	item->setPos(coord.x() - (size / 2), coord.y() - (size / 2));
	item->setTransformationMode(Qt::SmoothTransformation);

	// fit to window
	networkView->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
}

// adds the name of each station above the track
// size is just to set the vertical distance above the station Node
void MainWindow::paintStationName(QPointF coord, string sname, int size) {
	// add spaces to string
	int length = sname.length();
	for (int i = 1; i < length; i++) // ignore possible initial uppercase (start from 1)
	{
		int c = sname[i];
		if (isupper(c)) {
			sname.insert(i, " ");
			i++; // go to the next (added space)
		}
	}

	// convert string
	QString name = QString::fromStdString(sname);

	// set text size
	QFont font = QFont();
	font.setPixelSize((int)size / 5);

	// add text
	QGraphicsTextItem* station_name = new QGraphicsTextItem;
	station_name->setPlainText(name);
	station_name->setFont(font);
	station_name->setPos(coord.x() - (station_name->boundingRect().width() / 2), coord.y() - (station_name->boundingRect().height() / 2));
	station_name->setDefaultTextColor(Qt::white);
	station_name->setZValue(3); // draw on top of every item
	scene->addItem(station_name);

	// fit to window
	networkView->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
}

// paint platform next to station Node (upper side)
void MainWindow::paintStationPlatform(QPointF coord, int size, int pen_width, Node* Node) {
	QPen pen = QPen(Qt::white);
	pen.setWidth(0);
	pen.setCosmetic(true);

	// draws using rectangle with center on top-left corner (center_x,center_y,width,height)
	QRectF rect = QRectF(0, 0, 5 * size, 0.9 * size);

	// platform on top of station Node
	coord.setY(coord.y() - 1.15 * size);

	rect.moveCenter(coord);

	auto* platformItem = new PlatformItem(rect);
	platformItem->setPen(pen);
	platformItem->setBrush(Qt::white);

	// find corresponding station platform
	auto platformIt = std::find_if(AllStationPlatforms.begin(), AllStationPlatforms.end(),
								   [&Node](auto const& plat) { return Node->stationName == plat.StationID && Node->stationPlatformId == plat.ID; });

	// platform found
	if (platformIt != AllStationPlatforms.end()) {
		// add StationPlatform pointer to rect item
		platformItem->platform = &(*platformIt);
	}

	// string with pax counter
	std::stringstream ss;
	ss << "Pax on platform: " << platformIt->Current_N_Passengers << "\n Max pax volume: " << platformIt->Max_Passenger_Volume;

	// convert string
	QString text = QString::fromStdString(ss.str());

	// set text size
	QFont font = QFont();
	font.setPixelSize((int)station_size / 15);

	auto* textItem = new QGraphicsTextItem(text);
	textItem->setDefaultTextColor(Qt::white);
	textItem->setFont(font);
	textItem->setPos(QPointF(platformItem->sceneBoundingRect().center().x() - textItem->boundingRect().width() / 2, platformItem->sceneBoundingRect().top() - textItem->boundingRect().height()));
	textItem->setZValue(3); // draw on top of every item

	// add textItem pointer to rect item
	platformItem->textIcon = textItem;

	// add to scene
	scene->addItem(platformItem);
	scene->addItem(textItem);

	// hide textItem at first
	textItem->hide();

	// add item to allPlatforms list
	allPlatforms.push_back(platformItem);

	// fit to window
	networkView->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
}

// paint train passenger info on top of train
void MainWindow::paintTrainPassengerInfo(TrainItemGroup* trainItem) {
	// train head graphical coordinates
	TrainBodyItem* headPolygon = trainItem->trainPolygonItemList->at(0);
	QPointF frontUp = headPolygon->polygon().first();
	QPointF frontDown = headPolygon->polygon().last();

	// create line from train to message
	QPointF start = frontUp + frontDown;
	start /= 2;
	qreal dx = 0;
	qreal dy = -1 * track_separation;
	QPointF end = start + QPointF(dx, dy);

	// paint line
	QPen pen = QPen(QColor(242, 161, 106));
	pen.setWidth(line_width);
	pen.setCosmetic(true);

	// draws a line from start to end with a given line width
	QGraphicsLineItem* line = new QGraphicsLineItem(QLineF(start.x(), start.y(), end.x(), end.y()));
	line->setPen(pen);

	// text box position
	QPointF coord = end; // coord is the center of box and text in x-axis and bottom of box in y-axis

	// set text size
	QFont font = QFont();
	font.setPixelSize((int)station_size / 15);

	// string to display
	std::stringstream ss;
	ss << "Current onboard pax: " << trainItem->train->Current_OnBoard_Passengers << "\nMax onboard pax: " << trainItem->train->MAX_OnBoard_Passengers;

	// paint text
	QGraphicsTextItem* text = new QGraphicsTextItem;
	text->setPlainText(QString::fromStdString(ss.str())); // text without background
	text->setDefaultTextColor(Qt::black);
	text->setFont(font);

	// draw box around text
	QGraphicsRectItem* textBox = new QGraphicsRectItem;
	textBox->setRect(QRectF(0, 0, 1.15 * text->boundingRect().width(), 1.15 * text->boundingRect().height())); // box 15% bigger than text rect on both directions
	textBox->setBrush(QColor(242, 161, 106));
	textBox->setPos(coord.x() - (textBox->boundingRect().width() / 2), coord.y() - (textBox->boundingRect().height()));

	// set text position (center of text box)
	QPointF textPos = textBox->pos();
	textPos.rx() += 0.5 * (textBox->boundingRect().width() - text->boundingRect().width());
	textPos.ry() += 0.5 * (textBox->boundingRect().height() - text->boundingRect().height());
	text->setPos(textPos);

	// create group
	QGraphicsItemGroup* msgGroup = new QGraphicsItemGroup;
	msgGroup->addToGroup(line);
	msgGroup->addToGroup(textBox);
	msgGroup->addToGroup(text);
	msgGroup->setZValue(5);

	// add group to scene
	scene->addItem(msgGroup);

	// set current items
	trainPaxInfoItem = msgGroup;
	trainPaxItem = trainItem;
}

// paint passenger info on top of passenger icon
void MainWindow::paintPassengerInfoIcon(PassengerItem* paxItem) {
	// create line from icon to message
	QPointF start = QPointF(paxItem->sceneBoundingRect().center().x(), paxItem->sceneBoundingRect().top());
	qreal dx = 0;
	qreal dy = -0.75 * track_separation;
	QPointF end = start + QPointF(dx, dy);

	// paint line
	QPen pen = QPen(QColor(242, 161, 106));
	pen.setWidth(line_width);
	pen.setCosmetic(true);

	// draws a line from start to end with a given line width
	QGraphicsLineItem* line = new QGraphicsLineItem(QLineF(start.x(), start.y(), end.x(), end.y()));
	line->setPen(pen);

	// text box position
	QPointF coord = end; // coord is the center of box and text in x-axis and bottom of box in y-axis

	// set text size
	QFont font = QFont();
	font.setPixelSize((int)station_size / 15);

	std::string nextDestination;

	// find current journey
	std::string currentJourneyId = paxItem->passenger->current_JourneyID;
	auto currentJourneyIt = std::find_if(paxItem->passenger->Journeys.begin(), paxItem->passenger->Journeys.end(),
										 [&currentJourneyId](auto const& journey) { return currentJourneyId == journey.ID; });

	// journey found
	if (currentJourneyIt != paxItem->passenger->Journeys.end()) {
		// find trip
		std::string currentTripId = paxItem->passenger->current_TripID;
		auto currentTripIt = std::find_if(currentJourneyIt->Trips.begin(), currentJourneyIt->Trips.end(),
										  [&currentTripId](auto const& trip) { return currentTripId == trip.TripID; });

		// trip found
		if (currentTripIt != currentJourneyIt->Trips.end()) {
			nextDestination = currentTripIt->Arr_Station_ID;
		}
	}

	// string to display
	std::stringstream ss;
	ss << "Pax ID: " << paxItem->passenger->ID << "\nStatus: " << paxItem->passenger->CurrentStatus
	   << " (" << paxItem->passenger->Current_WaitingStationPlatformID << ")"
	   << "\nNext train: " << paxItem->passenger->Current_Train_To_Wait;

	if (!nextDestination.empty()) {
		ss << "\nNext destination: " << nextDestination;
	}

	// paint text
	QGraphicsTextItem* text = new QGraphicsTextItem;
	text->setPlainText(QString::fromStdString(ss.str())); // text without background
	text->setDefaultTextColor(Qt::black);
	text->setFont(font);

	// draw box around text
	QGraphicsRectItem* textBox = new QGraphicsRectItem;
	textBox->setRect(QRectF(0, 0, 1.15 * text->boundingRect().width(), 1.15 * text->boundingRect().height())); // box 15% bigger than text rect on both directions
	textBox->setBrush(QColor(242, 161, 106));
	textBox->setPos(coord.x() - (textBox->boundingRect().width() / 2), coord.y() - (textBox->boundingRect().height()));

	// set text position (center of text box)
	QPointF textPos = textBox->pos();
	textPos.rx() += 0.5 * (textBox->boundingRect().width() - text->boundingRect().width());
	textPos.ry() += 0.5 * (textBox->boundingRect().height() - text->boundingRect().height());
	text->setPos(textPos);

	// create group
	QGraphicsItemGroup* msgGroup = new QGraphicsItemGroup;
	msgGroup->addToGroup(line);
	msgGroup->addToGroup(textBox);
	msgGroup->addToGroup(text);
	msgGroup->setZValue(5);

	// add group to scene
	scene->addItem(msgGroup);

	// set current items
	paxIconInfoItem = msgGroup;
	paxIconItem = paxItem;
}

// paint text
void MainWindow::paintText(QPointF coord, string sname, int size) {
	// convert string
	QString name = QString::fromStdString(sname);

	// add text
	QGraphicsTextItem* text = new QGraphicsTextItem;
	text->setPlainText(name);
	text->setPos(coord.x() - (text->boundingRect().width() / 2), coord.y() - (text->boundingRect().height() / 2));
	text->setDefaultTextColor(Qt::white);
	scene->addItem(text);

	// fit to window
	networkView->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
}

// draws a simple line
void MainWindow::paintLine(QPointF start, QPointF end, int pen_width) {
	QPen pen = QPen(Qt::white);
	pen.setWidth(pen_width);
	pen.setCosmetic(true);

	// draws a line from start to end with a given line width
	scene->addLine(QLineF(start.x(), start.y(), end.x(), end.y()), pen);

	// fit to window
	networkView->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
}

// draws an Arc
void MainWindow::paintArc(QPointF start, QPointF end, int pen_width, int track, Arc* Arc, int track_separation) {
	arcDrawing(start, end, pen_width, track, Arc);
}

// Arc drawing
void MainWindow::arcDrawing(QPointF start, QPointF end, int pen_width, int track, Arc* Arc) {
	TrackVisual visual = classifyTrackSpeed(Arc ? Arc->speedLimit : 0.0);
	QPen pen = QPen(visual.color);
	pen.setWidth(std::max(pen_width, visual.width));
	pen.setCosmetic(true);

	// draws a line from start to end with a given line width
	TrackLineItem* line = new TrackLineItem(QLineF(start.x(), start.y(), end.x(), end.y()));
	line->setPen(pen);

	// add track and Arc pointer to line item
	line->track = track;
	line->arc = Arc;

	// RIGOS
	allArcs.push_back(line);

	// add item to scene
	scene->addItem(line);

	// fit to window
	networkView->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
}

// virtual Arc drawing
void MainWindow::virtualArcDrawing(QPointF start, QPointF middle, QPointF end, int pen_width, int track, Arc* Arc) {
	TrackVisual visual = classifyTrackSpeed(Arc ? Arc->speedLimit : 0.0);
	QPen pen = QPen(visual.color);
	pen.setWidth(std::max(pen_width, visual.width));
	pen.setCosmetic(true);

	// draws a line from start to end with a given line width
	VirtualArcItem* line = new VirtualArcItem(QLineF(start.x(), start.y(), middle.x(), middle.y()), QLineF(middle.x(), middle.y(), end.x(), end.y()));
	line->setPen(pen);

	// add track and Arc pointer to line item
	line->track = track;
	line->arc = Arc;

	// add item to scene
	scene->addItem(line);

	// fit to window
	networkView->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
}

// draws a connection
void MainWindow::paintConnection(QPointF start, QPointF end, int pen_width, Connections* connection) {
	QPen pen = QPen(Qt::white);
	pen.setWidth(pen_width);
	pen.setCosmetic(true);

	// draws a line from start to end with a given line width
	ConnectionItem* line = new ConnectionItem(QLineF(start.x(), start.y(), end.x(), end.y()));
	line->setPen(pen);

	// add connection pointer to line item
	line->connection = connection;

	// add item to scene
	scene->addItem(line);

	// fit to window
	networkView->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
}

// draws trackside signals (X is the beginning of the block section - except for the last signal of each trackline)
void MainWindow::paintSignal(double X, int size, int pen_width, int track, int track_separation, int sectionIndex) {
	int graphID = blockSets[track].graphID;

	// positions
	double postStartX, postStartY;
	double postEndX, postEndY;
	double basisStartX, basisStartY;
	double basisEndX, basisEndY;
	double plateCenterX, plateCenterY;

	// get coordinates
	egtrainPoint2Screen(X, track, track_separation, basisStartX, basisStartY);
	postStartX = basisStartX;
	postStartY = basisStartY;
	egtrainPoint2Screen(X - 0.008, track, track_separation, postEndX, postEndY);

	// place signal on trackside (with signal deltas)
	// last segment
	int stationIdx[2];
	neighbourStations(X, track, stationIdx);
	int index = stationIdx[1];

	basisStartX -= StationArray[index].signalDeltaX[blockSets[track].region] * 0.10 * track_separation;
	basisStartY -= StationArray[index].signalDeltaY[blockSets[track].region] * 0.10 * track_separation;
	basisEndX = basisStartX - StationArray[index].signalDeltaX[blockSets[track].region] * 0.20 * track_separation;
	basisEndY = basisStartY - StationArray[index].signalDeltaY[blockSets[track].region] * 0.20 * track_separation;

	postStartX -= StationArray[index].signalDeltaX[blockSets[track].region] * 0.20 * track_separation;
	postStartY -= StationArray[index].signalDeltaY[blockSets[track].region] * 0.20 * track_separation;
	postEndX -= StationArray[index].signalDeltaX[blockSets[track].region] * 0.20 * track_separation;
	postEndY -= StationArray[index].signalDeltaY[blockSets[track].region] * 0.20 * track_separation;

	// post/basis
	QPen penPost = QPen(Qt::white);
	penPost.setWidth(pen_width);
	penPost.setCosmetic(true);

	// plate
	QPen penPlate = QPen();
	penPlate.setWidth(0);
	// draws using rectangle with center on top-left corner (center_x,center_y,width,height)
	QRectF rect = QRectF(0, 0, size, size);

	// post #1
	QGraphicsLineItem* post1 = new QGraphicsLineItem(QLineF(postStartX, postStartY, postEndX, postEndY));
	post1->setPen(penPost);

	// plate #1
	plateCenterX = postEndX;
	plateCenterY = postEndY;
	rect.moveCenter(QPoint(plateCenterX, plateCenterY));

	SignalItem* plate1 = new SignalItem(rect);
	plate1->setPen(penPlate);
	plate1->setBrush(Qt::green);

	// add trackID, direction and signalling_block_sections pointers to signal item
	plate1->trackID = signalling_block_sections[sectionIndex].trackLineId;
	plate1->X = X;
	plate1->reversedDirection = 1;
	// last signal of trackline
	if (X == signalling_block_sections[sectionIndex].end_node.X) {
		plate1->sectionAhead = &signalling_block_sections[sectionIndex];
	}
	// remaining signals
	else {
		if (sectionIndex > 0 && signalling_block_sections[sectionIndex - 1].trackLineId == signalling_block_sections[sectionIndex].trackLineId) {
			plate1->sectionAhead = &signalling_block_sections[sectionIndex - 1];
		}
		plate1->sectionBehind = &signalling_block_sections[sectionIndex];
	}

	// basis #1
	QGraphicsLineItem* basis1 = new QGraphicsLineItem(QLineF(basisStartX, basisStartY, basisEndX, basisEndY));
	basis1->setPen(penPost);

	// basis #2
	basisStartX += 2 * StationArray[index].signalDeltaX[blockSets[track].region] * 0.10 * track_separation;
	basisStartY += 2 * StationArray[index].signalDeltaY[blockSets[track].region] * 0.10 * track_separation;
	basisEndX = basisStartX + StationArray[index].signalDeltaX[blockSets[track].region] * 0.20 * track_separation;
	basisEndY = basisStartY + StationArray[index].signalDeltaY[blockSets[track].region] * 0.20 * track_separation;

	// post #2
	egtrainPoint2Screen(X + 0.008, track, track_separation, postEndX, postEndY);
	postStartX += 2 * StationArray[index].signalDeltaX[blockSets[track].region] * 0.20 * track_separation;
	postStartY += 2 * StationArray[index].signalDeltaY[blockSets[track].region] * 0.20 * track_separation;
	postEndX += StationArray[index].signalDeltaX[blockSets[track].region] * 0.20 * track_separation;
	postEndY += StationArray[index].signalDeltaY[blockSets[track].region] * 0.20 * track_separation;

	QGraphicsLineItem* post2 = new QGraphicsLineItem(QLineF(postStartX, postStartY, postEndX, postEndY));
	post2->setPen(penPost);

	// plate #2
	plateCenterX = postEndX;
	plateCenterY = postEndY;
	rect.moveCenter(QPoint(plateCenterX, plateCenterY));

	SignalItem* plate2 = new SignalItem(rect);
	plate2->setPen(penPlate);
	plate2->setBrush(Qt::green);

	// add trackID, direction and signalling_block_sections pointers to signal item
	plate2->trackID = signalling_block_sections[sectionIndex].trackLineId;
	plate2->X = X;
	plate2->reversedDirection = 0;
	// last signal of trackline
	if (X == signalling_block_sections[sectionIndex].end_node.X) {
		plate2->sectionBehind = &signalling_block_sections[sectionIndex];
	}
	// remaining signals
	else {
		plate2->sectionAhead = &signalling_block_sections[sectionIndex];
		if (sectionIndex > 0 && signalling_block_sections[sectionIndex - 1].trackLineId == signalling_block_sections[sectionIndex].trackLineId) {
			plate2->sectionBehind = &signalling_block_sections[sectionIndex - 1];
		}
	}

	QGraphicsLineItem* basis2 = new QGraphicsLineItem(QLineF(basisStartX, basisStartY, basisEndX, basisEndY));
	basis2->setPen(penPost);

	// create group
	QGraphicsItemGroup* signalGroup = new QGraphicsItemGroup();
	signalGroup->addToGroup(post1);
	signalGroup->addToGroup(plate1);
	signalGroup->addToGroup(basis1);
	signalGroup->addToGroup(post2);
	signalGroup->addToGroup(plate2);
	signalGroup->addToGroup(basis2);

	// add signal to scene
	scene->addItem(signalGroup);

	// fit to window
	networkView->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);

	// add signals to allSignals list
	allSignals.push_back(plate1);
	allSignals.push_back(plate2);
}

// draws a train
void MainWindow::paintTrain(int trainIndex, int t, int size, int pen_width, Train* train) {
	TrainVisual visual = classifyTrainType(train ? train->type : "", train ? train->trainDescription : "");
	QPen pen = QPen(visual.outline);
	pen.setWidthF(3);

	// create train polygon item list
	QList<TrainBodyItem*>* trainPolygonItemList = new QList<TrainBodyItem*>();

	// create group of polygons
	TrainItemGroup* trainPolygonGroup = new TrainItemGroup();

	// get polygon for each wagon
	for (int wagon = 0; wagon <= train->number_of_wagons; wagon++) { // using <= because train.number_of_wagons excludes loco/first wagon
		QPolygonF* trainPolygon = new QPolygonF();
		getTrainPolygon(trainPolygon, wagon, t, trainIndex);

		// train graphical item
		TrainBodyItem* trainItem = new TrainBodyItem(*trainPolygon);
		trainItem->setPen(pen);
		trainItem->setBrush(visual.fill);
		trainItem->setOpacity(1);

		// add train pointer and index to the item
		trainItem->train = train;
		trainItem->index = trainIndex;

		// add to polygon group
		trainPolygonGroup->addToGroup(trainItem);

		// add item to list
		trainPolygonItemList->push_back(trainItem);
	}

	// add train pointer and index to the item
	trainPolygonGroup->train = train;
	trainPolygonGroup->index = trainIndex;
	trainPolygonGroup->trainPolygonItemList = trainPolygonItemList;

	// add train to scene
	scene->addItem(trainPolygonGroup);

	// add train to allTrains list
	allTrains.push_back(trainPolygonGroup);
}

// converts geodetic to cartesian coordinates
QPointF MainWindow::Coord2ScreenPoint(double x, double y, double factor) {
	// width and height are from the map (screen)
	double width = networkView->width();
	double height = networkView->height();
	double mercN = log(tan((PI / 4) + (x * PI / 180 / 2)));

	x = fmod((width * (180 + y) / 360), (width + (width / 2)));

	y = (height / 2) - (width * mercN / (2 * PI));

	return QPointF(factor * x, factor * y);
}

void MainWindow::gentle_zoom(double factor) {
	networkView->scale(factor, factor);
	networkView->centerOn(target_scene_pos);
	QPointF delta_viewport_pos = target_viewport_pos - QPointF(networkView->viewport()->width() / 2.0, networkView->viewport()->height() / 2.0);
	QPointF viewport_center = networkView->mapFromScene(target_scene_pos) - delta_viewport_pos;
	networkView->centerOn(networkView->mapToScene(viewport_center.toPoint()));
	emit zoomed();
}

// zoom
void MainWindow::set_modifiers(Qt::KeyboardModifiers modifiers) {
	_modifiers = modifiers;
}

void MainWindow::set_zoom_factor_base(double value) {
	_zoom_factor_base = value;
}

// zoom
bool MainWindow::eventFilter(QObject* object, QEvent* event) {
	if (event->type() == QEvent::MouseMove) {
		QMouseEvent* mouse_event = static_cast<QMouseEvent*>(event);
		QPointF delta = target_viewport_pos - mouse_event->pos();
		if (qAbs(delta.x()) > 5 || qAbs(delta.y()) > 5) {
			target_viewport_pos = mouse_event->pos();
			target_scene_pos = networkView->mapToScene(mouse_event->pos());
		}
	} else if (event->type() == QEvent::Wheel) {
		QWheelEvent* wheel_event = static_cast<QWheelEvent*>(event);
		if (QApplication::keyboardModifiers() == _modifiers) {
			if (wheel_event->angleDelta().y() != 0) {
				double angle = wheel_event->angleDelta().y();
				double factor = qPow(_zoom_factor_base, angle);
				gentle_zoom(factor);
				return true;
			}
		}
	}
	Q_UNUSED(object)
	return false;
}

// setup qdockwidget showing info when clicking items
void MainWindow::setupInfoDockWidget() {
	// main widget
	infoWidget = new QWidget();

	// Arc info widget
	arcInfoWidget = new QWidget();
	arcIDText = new QLineEdit(arcInfoWidget);
	arcFirstNodeIDText = new QLineEdit(arcInfoWidget);
	arcSecondNodeIDText = new QLineEdit(arcInfoWidget);
	arcTrackIDText = new QLineEdit(arcInfoWidget);
	arcLengthText = new QLineEdit(arcInfoWidget);
	arcCurvatureText = new QLineEdit(arcInfoWidget);
	arcGradientText = new QLineEdit(arcInfoWidget);
	arcSpeedLimitText = new QLineEdit(arcInfoWidget);
	arcFormLayout = new QFormLayout();
	arcFormLayout->addRow("Arc ID", arcIDText);
	arcFormLayout->addRow("First Node ID", arcFirstNodeIDText);
	arcFormLayout->addRow("Second Node ID", arcSecondNodeIDText);
	arcFormLayout->addRow("Track ID", arcTrackIDText);
	arcFormLayout->addRow("Length (m)", arcLengthText);
	arcFormLayout->addRow("Curvature (m)", arcCurvatureText);
	arcFormLayout->addRow("Gradient", arcGradientText);
	arcFormLayout->addRow("Speed Limit (m/s)", arcSpeedLimitText);
	arcInfoWidget->setLayout(arcFormLayout);

	// Node info widget
	nodeInfoWidget = new QWidget();
	nodeIDText = new QLineEdit(nodeInfoWidget);
	nodeTrackIDText = new QLineEdit(nodeInfoWidget);
	nodeXText = new QLineEdit(nodeInfoWidget);
	nodeYText = new QLineEdit(nodeInfoWidget);
	nodeFormLayout = new QFormLayout();
	nodeFormLayout->addRow("Node ID", nodeIDText);
	nodeFormLayout->addRow("Track ID", nodeTrackIDText);
	nodeFormLayout->addRow("X (m)", nodeXText);
	nodeFormLayout->addRow("Y (m)", nodeYText);
	nodeInfoWidget->setLayout(nodeFormLayout);

	// connection info widget
	connectionInfoWidget = new QWidget();
	connectionFirstTrackIDText = new QLineEdit(connectionInfoWidget);
	connectionSecondTrackIDText = new QLineEdit(connectionInfoWidget);
	connectionXFirstNodeText = new QLineEdit(connectionInfoWidget);
	connectionXSecondNodeText = new QLineEdit(connectionInfoWidget);
	connectionFormLayout = new QFormLayout();
	connectionFormLayout->addRow("First Track ID", connectionFirstTrackIDText);
	connectionFormLayout->addRow("Second Track ID", connectionSecondTrackIDText);
	connectionFormLayout->addRow("X First Node (m)", connectionXFirstNodeText);
	connectionFormLayout->addRow("X Second Node (m)", connectionXSecondNodeText);
	connectionInfoWidget->setLayout(connectionFormLayout);

	// signalling info widget
	signallingInfoWidget = new QWidget();
	signallingTrackIDText = new QLineEdit(signallingInfoWidget);
	signallingXText = new QLineEdit(signallingInfoWidget);
	signallingIDSectionAheadText = new QLineEdit(signallingInfoWidget);
	signallingLengthSectionAheadText = new QLineEdit(signallingInfoWidget);
	signallingFormLayout = new QFormLayout();
	signallingFormLayout->addRow("Track ID", signallingTrackIDText);
	signallingFormLayout->addRow("X (m)", signallingXText);
	signallingFormLayout->addRow("ID of Block Section", signallingIDSectionAheadText);
	signallingFormLayout->addRow("Length of Block Section (m)", signallingLengthSectionAheadText);
	signallingInfoWidget->setLayout(signallingFormLayout);

	// train info widget
	trainInfoWidget = new QWidget();
	trainIDText = new QLineEdit(trainInfoWidget);
	trainTypeText = new QLineEdit(trainInfoWidget);
	trainLengthText = new QLineEdit(trainInfoWidget);
	trainWagonsText = new QLineEdit(trainInfoWidget);
	trainFormLayout = new QFormLayout();
	trainFormLayout->addRow("ID", trainIDText);
	trainFormLayout->addRow("Type", trainTypeText);
	trainFormLayout->addRow("Length (m)", trainLengthText);
	trainFormLayout->addRow("Number of wagons", trainWagonsText);
	trainInfoWidget->setLayout(trainFormLayout);

	// main layout
	infoWidgetMainLayout = new QVBoxLayout();
	infoWidgetMainLayout->addWidget(arcInfoWidget);
	infoWidgetMainLayout->addWidget(nodeInfoWidget);
	infoWidgetMainLayout->addWidget(connectionInfoWidget);
	infoWidgetMainLayout->addWidget(signallingInfoWidget);
	infoWidgetMainLayout->addWidget(trainInfoWidget);
	infoWidgetMainLayout->addStretch();
	infoWidget->setLayout(infoWidgetMainLayout);

	// dock widget
	infoDockWidget = new InfoDockWidget("Simulation Info");
	infoDockWidget->setWidget(infoWidget);
	infoDockWidget->setAllowedAreas(Qt::RightDockWidgetArea);
	addDockWidget(Qt::RightDockWidgetArea, infoDockWidget);

	// hide widgets
	infoDockWidget->hide();
	arcInfoWidget->hide();
	nodeInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	removeTrainPaxInfoIcon();
	removePaxInfoIcon();
}

// help from menu
void MainWindow::handleHelpAbout() {
	QMessageBox::information(
		this,
		tr("About"),
		tr("Made at TU Delft"));
}

// hides all widgets from the dock widget
// removes highlight from last clicked item
void MainWindow::handleCloseInfoDockWidget() {
	// hide all widgets
	arcInfoWidget->hide();
	nodeInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	removeTrainPaxInfoIcon();
	removePaxInfoIcon();

	// remove highlight
	if (effect) {
		delete effect;
		effect = nullptr;
	}
}

// shows Node info on dock widget
void MainWindow::displayNodeInfo(NodeItem* el) {
	// update Node info displayed on widget
	nodeIDText->setText(QString::fromStdString(to_string_precision(el->node->ID, 0)));
	nodeXText->setText(QString::fromStdString(to_string_precision(1000 * el->node->X, 2))); // km to m
	nodeYText->setText(QString::fromStdString(to_string_precision(1000 * el->node->Y, 2))); // km to m
	nodeTrackIDText->setText(QString::fromStdString(to_string_precision(el->track, 0)));

	// hide other widgets
	arcInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	removeTrainPaxInfoIcon();
	removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle("Node Info");
	infoDockWidget->show();
	nodeInfoWidget->show();

	// effect on clicked item
	if (!effect) {
		effect = new HighlightEffect(Qt::blue, 1);
	}
	el->setGraphicsEffect(effect);
}

// shows station Node info on dock widget
void MainWindow::displayStationNodeInfo(StationNodeItem* re) {
	// update Node info displayed on widget
	nodeIDText->setText(QString::fromStdString(to_string_precision(re->node->ID, 0)));
	nodeXText->setText(QString::fromStdString(to_string_precision(1000 * re->node->X, 2))); // km to m
	nodeYText->setText(QString::fromStdString(to_string_precision(1000 * re->node->Y, 2))); // km to m
	nodeTrackIDText->setText(QString::fromStdString(to_string_precision(re->track, 0)));

	// hide other widgets
	arcInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	removeTrainPaxInfoIcon();
	removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle("Station Node Info");
	infoDockWidget->show();
	nodeInfoWidget->show();

	// effect on clicked item
	if (!effect) {
		effect = new HighlightEffect(Qt::blue, 1);
	}
	re->setGraphicsEffect(effect);
}

// shows Arc info on dock widget
void MainWindow::displayArcInfo(TrackLineItem* line) {
	// update Arc info displayed on widget
	arcIDText->setText(QString::fromStdString(to_string_precision(line->arc->ID, 0)));
	arcFirstNodeIDText->setText(QString::fromStdString(to_string_precision(line->arc->startNode.ID, 0)));
	arcSecondNodeIDText->setText(QString::fromStdString(to_string_precision(line->arc->endNode.ID, 0)));
	arcTrackIDText->setText(QString::fromStdString(to_string_precision(line->track, 0)));
	arcLengthText->setText(QString::fromStdString(to_string_precision(line->arc->length, 2)));
	arcCurvatureText->setText(QString::fromStdString(to_string_precision(line->arc->curvature, 2)));
	arcGradientText->setText(QString::fromStdString(to_string_precision(line->arc->gradient, 3)));
	arcSpeedLimitText->setText(QString::fromStdString(to_string_precision(line->arc->speedLimit, 2)));

	// hide other widgets
	nodeInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	removeTrainPaxInfoIcon();
	removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle("Arc Info");
	infoDockWidget->show();
	arcInfoWidget->show();

	// effect on clicked item
	if (!effect) {
		effect = new HighlightEffect(Qt::blue, 1);
	}
	line->setGraphicsEffect(effect);
}

// shows connection info on dock widget
void MainWindow::displayConnectionInfo(ConnectionItem* line) {
	// update connection info displayed on widget
	connectionFirstTrackIDText->setText(QString::fromStdString(to_string_precision(line->connection->idFirstTrackLine, 0)));
	connectionSecondTrackIDText->setText(QString::fromStdString(to_string_precision(line->connection->idSecondTrackLine, 0)));
	connectionXFirstNodeText->setText(QString::fromStdString(to_string_precision(1000 * line->connection->xFirstNode, 2)));	  // km to m
	connectionXSecondNodeText->setText(QString::fromStdString(to_string_precision(1000 * line->connection->xSecondNode, 2))); // km to m

	// hide other widgets
	arcInfoWidget->hide();
	nodeInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	removeTrainPaxInfoIcon();
	removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle("Connection Info");
	infoDockWidget->show();
	connectionInfoWidget->show();

	// effect on clicked item
	if (!effect) {
		effect = new HighlightEffect(Qt::blue, 1);
	}
	line->setGraphicsEffect(effect);
}

// shows signalling info on dock widget
void MainWindow::displaySignallingInfo(SignalItem* signal) {
	// update signalling info displayed on widget
	signallingTrackIDText->setText(QString::fromStdString(to_string_precision(signal->trackID, 0)));
	signallingXText->setText(QString::fromStdString(to_string_precision(1000 * signal->X, 2))); // km to m

	// check if section ahead exists
	if (signal->sectionAhead) {
		signallingIDSectionAheadText->setText(QString::fromStdString(signal->sectionAhead->ID));
		signallingLengthSectionAheadText->setText(QString::fromStdString(to_string_precision(1000 * signal->sectionAhead->length, 2))); // km to m
	} else {
		signallingIDSectionAheadText->setText("");
		signallingLengthSectionAheadText->setText("");
	}

	// hide other widgets
	arcInfoWidget->hide();
	connectionInfoWidget->hide();
	nodeInfoWidget->hide();
	trainInfoWidget->hide();
	removeTrainPaxInfoIcon();
	removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle("Signalling Info");
	infoDockWidget->show();
	signallingInfoWidget->show();

	// effect on clicked item
	if (!effect) {
		effect = new HighlightEffect(Qt::blue, 1);
	}
	signal->setGraphicsEffect(effect);
}

// shows train info on dock widget
void MainWindow::displayTrainInfo(TrainBodyItem* trainItem) {
	// update train info displayed on widget
	trainIDText->setText(QString::fromStdString(to_string_precision(trainItem->train->ID, 0)));
	trainTypeText->setText(QString::fromStdString(trainItem->train->type));
	trainLengthText->setText(QString::fromStdString(to_string_precision(trainItem->train->train_length, 0)));
	trainWagonsText->setText(QString::fromStdString(to_string_precision(trainItem->train->number_of_wagons, 0)));

	// hide other widgets
	arcInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	nodeInfoWidget->hide();
	removeTrainPaxInfoIcon();
	removePaxInfoIcon();

	// show pax info
	if (initial_variables.PAX_GUI) {
		TrainItemGroup* groupItem = qgraphicsitem_cast<TrainItemGroup*>(trainItem->parentItem());
		if (groupItem) {
			paintTrainPassengerInfo(groupItem);
		}
	}

	// show widget
	infoDockWidget->setWindowTitle("Train Info");
	infoDockWidget->show();
	trainInfoWidget->show();

	// effect on clicked item
	if (!effect) {
		effect = new HighlightEffect(Qt::blue, 1);
	}
	if (trainItem->parentItem()) {
		trainItem->parentItem()->setGraphicsEffect(effect);
	} // effect on entire train
}

// show text icon on top of passenger icon
void MainWindow::displayPassengerInfo(PassengerItem* paxItem) {
	// hide other widgets
	infoDockWidget->hide();
	arcInfoWidget->hide();
	nodeInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	removeTrainPaxInfoIcon();
	removePaxInfoIcon();

	// show pax info
	paintPassengerInfoIcon(paxItem);
}

void MainWindow::handleDisableHighlight() {}

// interpolate cartesian coordinates
QPointF MainWindow::interpolateCartesian(QPointF start, QPointF end, qreal x1, qreal x2, qreal x) {
	// new point
	QPointF interp_point;

	// perform linear interpolation
	interp_point.setX(start.x() + (((x - x1) / (x2 - x1)) * (end.x() - start.x())));
	interp_point.setY(start.y() + (((x - x1) / (x2 - x1)) * (end.y() - start.y())));

	return interp_point;
}

// get station screen coordinates
// find shift vector for each station/region
// find delta X,Y to place trackside signals
void MainWindow::calculateStationCoordAndShift(int geo_scale) {
	double alpha;	 // angle between two arcs (with a station in the middle)
	double gamma;	 // angle for shift (considered to be the angle with screen x axis (pointing down), being positive clockwise)
	double beta;	 // angle between first Arc and screen x axis
	double dot, det; // dot product and determinant
	double x1, x2, x3;
	double y1, y2, y3;
	QPointF pt;

	// get station screen coordinates
	for (int i = 0; i < numStations; i++) { // need a separate for as the one below accesses positions ahead of i
		// convert geodetic to cartesian coordinates
		pt = Coord2ScreenPoint(StationArray[i].latitude, StationArray[i].longitude, geo_scale);

		// save coordinates
		StationArray[i].graphX = pt.x();
		StationArray[i].graphY = pt.y();
	}

	// find number of regions
	for (int i = 0; i < numStations; i++) {
		for (int j = 0; j < StationArray[i].regions.size(); j++) {
			// increase size if needed
			if (StationArray[i].regions[j] >= regionStations.size()) {
				regionStations.resize(StationArray[i].regions[j] + 1);
			}
			regionStations[StationArray[i].regions[j]].push_back(i);
		}
	}

	// create X-ordered array of stations per region
	for (int i = 0; i < regionStations.size(); i++) {
		for (int j = 0; j < regionStations[i].size(); j++) {
			std::sort(regionStations[i].begin(), regionStations[i].end(), [](const int& a, const int& b) -> bool { return StationArray[a].X < StationArray[b].X; });
		}
	}

	// calculate shifts and signal deltas per region
	for (int r = 0; r < regionStations.size(); r++) {
		if (regionStations[r].size() < 2) {
			continue;
		}
		if (regionStations[r].size() == 2) {
			int s1 = regionStations[r][0];
			int s2 = regionStations[r][1];
			x1 = StationArray[s1].graphX;
			y1 = StationArray[s1].graphY;
			x2 = StationArray[s2].graphX;
			y2 = StationArray[s2].graphY;
			beta = atan2(-(y1 - y2), (x1 - x2));
			StationArray[s1].signalDeltaX[r] = sin(beta);
			StationArray[s1].signalDeltaY[r] = cos(beta);
			StationArray[s1].shiftX[r] = cos(PI / 2 - beta);
			StationArray[s1].shiftY[r] = sin(PI / 2 - beta);
			StationArray[s2].signalDeltaX[r] = sin(beta);
			StationArray[s2].signalDeltaY[r] = cos(beta);
			StationArray[s2].shiftX[r] = cos(PI / 2 - beta);
			StationArray[s2].shiftY[r] = sin(PI / 2 - beta);
			continue;
		}
		for (int i = 0; i < regionStations[r].size(); i++) {
			if (i == regionStations[r].size() - 1) { // special case - last Arc
				// use shifts of previous Arc
				StationArray[regionStations[r][i]].shiftX[r] = StationArray[regionStations[r][i - 1]].shiftX[r];
				StationArray[regionStations[r][i]].shiftY[r] = StationArray[regionStations[r][i - 1]].shiftY[r];

				// special case for signal deltas (need to use next Arc)
				x1 = StationArray[regionStations[r][i - 1]].graphX;
				x2 = StationArray[regionStations[r][i]].graphX;
				y1 = StationArray[regionStations[r][i - 1]].graphY;
				y2 = StationArray[regionStations[r][i]].graphY;

				beta = atan2(-(y1 - y2), (x1 - x2));

				// calculate trackside signal deltas
				StationArray[regionStations[r][i]].signalDeltaX[r] = sin(beta);
				StationArray[regionStations[r][i]].signalDeltaY[r] = cos(beta);
			} else {
				if (i == 0) { // special case - first Arc - do the same as 1st regular case
					x1 = StationArray[regionStations[r][i]].graphX;
					x2 = StationArray[regionStations[r][i + 1]].graphX;
					x3 = StationArray[regionStations[r][i + 2]].graphX;
					y1 = StationArray[regionStations[r][i]].graphY;
					y2 = StationArray[regionStations[r][i + 1]].graphY;
					y3 = StationArray[regionStations[r][i + 2]].graphY;

				} else { // regular case
					x1 = StationArray[regionStations[r][i - 1]].graphX;
					x2 = StationArray[regionStations[r][i]].graphX;
					x3 = StationArray[regionStations[r][i + 1]].graphX;
					y1 = StationArray[regionStations[r][i - 1]].graphY;
					y2 = StationArray[regionStations[r][i]].graphY;
					y3 = StationArray[regionStations[r][i + 1]].graphY;
				}

				dot = (x1 - x2) * (x3 - x2) + (y1 - y2) * (y3 - y2); // dot product
				det = (x1 - x2) * (y3 - y2) - (y1 - y2) * (x3 - x2); // determinant
				alpha = atan2(det, dot);							 // atan2(y, x)

				// adjust alpha to 0 to 2*PI range
				if (alpha < 0) {
					alpha += 2 * PI;
				}

				beta = atan2(-(y1 - y2), (x1 - x2));

				gamma = alpha / 2 - beta;

				// calculate trackside signal deltas
				StationArray[regionStations[r][i]].signalDeltaX[r] = sin(beta);
				StationArray[regionStations[r][i]].signalDeltaY[r] = cos(beta);

				// calculate shifts
				StationArray[regionStations[r][i]].shiftX[r] = cos(gamma);
				StationArray[regionStations[r][i]].shiftY[r] = sin(gamma);
			}
		}
	}
}

// double to string with precision
string MainWindow::to_string_precision(double value, int precision) {
	std::ostringstream out;
	out.precision(precision);
	out << std::fixed << value;
	return out.str();
}

// show network
void MainWindow::showNetwork() {
	ui->centralWidget->show();
}

// hide network
void MainWindow::hideNetwork() {
	ui->centralWidget->hide();
}

// fit view
void MainWindow::fitView() {
	// fit the items actually on the scene; the scene rect is unreliable after
	// a reload because Qt's automatic rect only ever grows across loads
	QRectF initialSceneRect = scene->itemsBoundingRect();
	if (initialSceneRect.isEmpty())
		initialSceneRect = scene->sceneRect();
	qreal expansionFactorX = 0.05;
	qreal expansionFactorY = 0.05;
	scene->setSceneRect(initialSceneRect.adjusted(-0.5 * expansionFactorX * initialSceneRect.width(), -0.5 * expansionFactorY * initialSceneRect.height(), 0.5 * expansionFactorX * initialSceneRect.width(), 0.5 * expansionFactorY * initialSceneRect.height()));

	// fit to window
	networkView->fitInView(initialSceneRect, Qt::KeepAspectRatio);
}

// check if all tracks are assigned to a graphical level
int MainWindow::allTracksAssigned() {

	for (int i = 0; i < numTrackLines; i++) {
		if (blockSets[i].graphID == -1) {
			return 0;
		} // track not assigned
	}

	return 1; // all tracks assigned
}

// finds the indexes of the two closest stations given a point
void MainWindow::neighbourStations(double X, int tracklineID, int* stationIdx) {
	std::vector<int> trackStIdx = regionStations[blockSets[tracklineID].region];

	// find neighbour stations
	// check if value is inside/outside X range of stations
	if (X < StationArray[trackStIdx[0]].regionX[blockSets[tracklineID].region]) { // x before first station (interpolation with fisrt two stations)
		stationIdx[0] = trackStIdx[0];
		stationIdx[1] = trackStIdx[1];
	} else if (X >= StationArray[trackStIdx[trackStIdx.size() - 1]].regionX[blockSets[tracklineID].region]) { // x >= last station (interpolation with last two stations)
		stationIdx[0] = trackStIdx[trackStIdx.size() - 2];
		stationIdx[1] = trackStIdx[trackStIdx.size() - 1];
	} else { // point inside range of stations (interpolation with two closest stations)
		// find closest stations
		for (int i = 0; i < (trackStIdx.size() - 1); i++) {
			if ((X >= StationArray[trackStIdx[i]].regionX[blockSets[tracklineID].region]) && (X < StationArray[trackStIdx[i + 1]].regionX[blockSets[tracklineID].region])) { // right side of the interval must be open
				stationIdx[0] = trackStIdx[i];
				stationIdx[1] = trackStIdx[i + 1];
				break;
			}
		}
	}
}

// get shifted screen coordinates of a point (given 1D X coordinate)
void MainWindow::egtrainPoint2Screen(double X, int track, double separation, double& graphX, double& graphY) {
	double graphID = (double)blockSets[track].graphID;

	// find index of closest stations
	int stationIdx[2];
	neighbourStations(X, track, stationIdx);

	// shifted stations
	double x1, x2, y1, y2;

	x1 = StationArray[stationIdx[0]].graphX + StationArray[stationIdx[0]].shiftX[blockSets[track].region] * graphID * separation;
	x2 = StationArray[stationIdx[1]].graphX + StationArray[stationIdx[1]].shiftX[blockSets[track].region] * graphID * separation;
	y1 = StationArray[stationIdx[0]].graphY + StationArray[stationIdx[0]].shiftY[blockSets[track].region] * graphID * separation;
	y2 = StationArray[stationIdx[1]].graphY + StationArray[stationIdx[1]].shiftY[blockSets[track].region] * graphID * separation;

	// linear interpolation of geodetic coordinates using know coordinates of closest stations
	graphX = x1 + (((X - StationArray[stationIdx[0]].regionX[blockSets[track].region]) / (StationArray[stationIdx[1]].regionX[blockSets[track].region] - StationArray[stationIdx[0]].regionX[blockSets[track].region])) * (x2 - x1));
	graphY = y1 + (((X - StationArray[stationIdx[0]].regionX[blockSets[track].region]) / (StationArray[stationIdx[1]].regionX[blockSets[track].region] - StationArray[stationIdx[0]].regionX[blockSets[track].region])) * (y2 - y1));
}

// get shifted screen coordinates of a Node
void MainWindow::egtrainPoint2Screen(Node* Node, int track, double separation) {
	int stationIdx[2];
	neighbourStations(Node->X, track, stationIdx);
	int graphID = blockSets[track].graphID;

	// shifted stations
	double x1, x2, y1, y2;

	x1 = StationArray[stationIdx[0]].graphX + StationArray[stationIdx[0]].shiftX[blockSets[track].region] * graphID * separation;
	x2 = StationArray[stationIdx[1]].graphX + StationArray[stationIdx[1]].shiftX[blockSets[track].region] * graphID * separation;
	y1 = StationArray[stationIdx[0]].graphY + StationArray[stationIdx[0]].shiftY[blockSets[track].region] * graphID * separation;
	y2 = StationArray[stationIdx[1]].graphY + StationArray[stationIdx[1]].shiftY[blockSets[track].region] * graphID * separation;

	// linear interpolation of geodetic coordinates using known coordinates of closest stations
	Node->graphX = x1 + (((Node->X - StationArray[stationIdx[0]].regionX[blockSets[track].region]) / (StationArray[stationIdx[1]].regionX[blockSets[track].region] - StationArray[stationIdx[0]].regionX[blockSets[track].region])) * (x2 - x1));
	Node->graphY = y1 + (((Node->X - StationArray[stationIdx[0]].regionX[blockSets[track].region]) / (StationArray[stationIdx[1]].regionX[blockSets[track].region] - StationArray[stationIdx[0]].regionX[blockSets[track].region])) * (y2 - y1));
}

// get shifted screen coordinates of a Node
void MainWindow::egtrainPoint2Screen(Connections* connections, int track1, int track2, double separation) {
	int graphID1 = blockSets[track1].graphID;
	int graphID2 = blockSets[track2].graphID;

	// find index of closest stations
	int stationIdx[2];
	neighbourStations(connections->xFirstNode, track1, stationIdx);

	// shifted stations
	double x1, x2, y1, y2;

	x1 = StationArray[stationIdx[0]].graphX + StationArray[stationIdx[0]].shiftX[blockSets[track1].region] * graphID1 * separation;
	x2 = StationArray[stationIdx[1]].graphX + StationArray[stationIdx[1]].shiftX[blockSets[track1].region] * graphID1 * separation;
	y1 = StationArray[stationIdx[0]].graphY + StationArray[stationIdx[0]].shiftY[blockSets[track1].region] * graphID1 * separation;
	y2 = StationArray[stationIdx[1]].graphY + StationArray[stationIdx[1]].shiftY[blockSets[track1].region] * graphID1 * separation;

	// linear interpolation of geodetic coordinates using know coordinates of closest stations
	connections->graphXFirstNode = x1 + (((connections->xFirstNode - StationArray[stationIdx[0]].regionX[blockSets[track1].region]) / (StationArray[stationIdx[1]].regionX[blockSets[track1].region] - StationArray[stationIdx[0]].regionX[blockSets[track1].region])) * (x2 - x1));
	connections->graphYFirstNode = y1 + (((connections->xFirstNode - StationArray[stationIdx[0]].regionX[blockSets[track1].region]) / (StationArray[stationIdx[1]].regionX[blockSets[track1].region] - StationArray[stationIdx[0]].regionX[blockSets[track1].region])) * (y2 - y1));

	// find index of closest stations
	neighbourStations(connections->xSecondNode, track2, stationIdx);

	x1 = StationArray[stationIdx[0]].graphX + StationArray[stationIdx[0]].shiftX[blockSets[track2].region] * graphID2 * separation;
	x2 = StationArray[stationIdx[1]].graphX + StationArray[stationIdx[1]].shiftX[blockSets[track2].region] * graphID2 * separation;
	y1 = StationArray[stationIdx[0]].graphY + StationArray[stationIdx[0]].shiftY[blockSets[track2].region] * graphID2 * separation;
	y2 = StationArray[stationIdx[1]].graphY + StationArray[stationIdx[1]].shiftY[blockSets[track2].region] * graphID2 * separation;

	// linear interpolation of geodetic coordinates using know coordinates of closest stations
	connections->graphXSecondNode = x1 + (((connections->xSecondNode - StationArray[stationIdx[0]].regionX[blockSets[track2].region]) / (StationArray[stationIdx[1]].regionX[blockSets[track2].region] - StationArray[stationIdx[0]].regionX[blockSets[track2].region])) * (x2 - x1));
	connections->graphYSecondNode = y1 + (((connections->xSecondNode - StationArray[stationIdx[0]].regionX[blockSets[track2].region]) / (StationArray[stationIdx[1]].regionX[blockSets[track2].region] - StationArray[stationIdx[0]].regionX[blockSets[track2].region])) * (y2 - y1));
}

// slot to update GUI at each timestep (no longer blocks; simulation runs on worker thread)
void MainWindow::waitForUpdates(int timestep) {
	QString clock = QString::fromStdString(formatSimTime(timestep, m_startOffsetSeconds));
	QString endClock = QString::fromStdString(formatSimTime(static_cast<long long>(initial_variables.times), m_startOffsetSeconds));
	statusBar()->showMessage(QString("Simulation running  %1 / %2").arg(clock, endClock));

	if (progressBar) {
		progressBar->setProgress(timestep);
	}

	qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (now - m_lastRenderMs >= 33 || timestep >= initial_variables.times - 1) {
		updateSignalling();
		updateTrainPosition(timestep);

		// pax info
		if (initial_variables.PAX_GUI) {
			updatePlatforms(timestep);
			updatePaxIconInfo();
			updateTrainPaxInfo();
		}

		m_lastRenderMs = now;
	}
}

// slot to update signal aspects
void MainWindow::updateSignalling() {
	int index1, index2, index3;
	int r;
	string ID1, ID2;

	// update for each route
	for (int r = 0; r < N_Routes; r++) {
		for (int i = 0; i < train_route[r].N_Block_Sections; i++) {
			// regular case -> change signalling_block_sections entry signal
			if (train_route[r].sequence_of_block_sections[i].ID.find('/') == std::string::npos) {
				// reversed -> update signalling_block_sections entry signal
				updateSignalAspect(train_route[r].sequence_of_block_sections[i].ID, train_route[r].sequence_of_block_sections[i].code, train_route[r].reversed_direction);
				// updateBlockOccupationStatus(allArcs);
			}
			// transition of block section
			else {
				// update both block sections
				index1 = train_route[r].sequence_of_block_sections[i].ID.find("@-");
				index2 = train_route[r].sequence_of_block_sections[i].ID.find("/@", index1 + 1);
				index3 = train_route[r].sequence_of_block_sections[i].ID.find("@-", index2 + 1);
				if (index1 != std::string::npos && index2 != std::string::npos && index3 != std::string::npos) {
					ID1 = train_route[r].sequence_of_block_sections[i].ID.substr(0, index1 + 1);
					ID2 = train_route[r].sequence_of_block_sections[i].ID.substr(index2 + 1, index3 - index2);
					// reversed -> update both entry signals
					updateSignalAspect(ID1, train_route[r].sequence_of_block_sections[i].code, train_route[r].reversed_direction);
					updateSignalAspect(ID2, train_route[r].sequence_of_block_sections[i].code, train_route[r].reversed_direction);
				}
			}
		}
	}
}

// slot to update passenger counter at platforms
void MainWindow::updatePlatforms(int t) {
	// show text items from the beginning of the simulation
	if (t == 0) {
		for (auto* platformIcon : allPlatforms) {
			platformIcon->textIcon->show();
		}
	}

	for (auto* platformIcon : allPlatforms) {
		// remove existing pax icons
		std::string currentlyHighlightedPaxID;
		for (auto* icon : platformIcon->passengerIcons) {
			// if icon had a message, store its pax ID
			if (icon == paxIconItem) {
				currentlyHighlightedPaxID = icon->passenger->ID;
				paxIconItem = nullptr;
			}

			scene->removeItem(icon);
		}
		qDeleteAll(platformIcon->passengerIcons.begin(), platformIcon->passengerIcons.end());
		platformIcon->passengerIcons.clear();

		// list of current passengers
		list<pair<string, double>> Extended_Current_List_Pax_On_Platform; // current list + passengers leaving network
		std::copy(platformIcon->platform->Current_List_Pax_On_Platform.begin(), platformIcon->platform->Current_List_Pax_On_Platform.end(), std::back_inserter(Extended_Current_List_Pax_On_Platform));

		// check if any passengers just left the network (these are still shown)
		for (auto const& pax : AllDailyPassengers) {
			if (pax.IsIntheNetwork == false && pax.TimeExitedTheNetwork > 0 && (t >= pax.TimeExitedTheNetwork) && (t <= pax.TimeExitedTheNetwork + 5) // show passengers leaving the network for 5 seconds
				&& platformIcon->platform->StationID == pax.StationExitedTheNetworkID && platformIcon->platform->ID == pax.PlatformExitedTheNetworkID) {
				Extended_Current_List_Pax_On_Platform.push_back({pax.ID, -9999}); // 2nd element of the pair is irrelevant for the GUI
			}
		}

		int nPaxIcons = Extended_Current_List_Pax_On_Platform.size();

		// string with pax counter
		std::stringstream ss;
		ss << "Pax on platform: " << nPaxIcons << "\nMax pax volume: " << platformIcon->platform->Max_Passenger_Volume;

		// convert string
		QString text = QString::fromStdString(ss.str());

		// update text item
		auto originalCenter = platformIcon->textIcon->boundingRect().center();
		platformIcon->textIcon->setPlainText(text);
		auto newCenter = platformIcon->textIcon->boundingRect().center();
		auto delta = originalCenter - newCenter;
		platformIcon->textIcon->moveBy(delta.x(), delta.y());

		// add icons
		if (nPaxIcons > 0) {
			qreal iconSpacing = platformIcon->sceneBoundingRect().width() / nPaxIcons;
			qreal iconX = platformIcon->sceneBoundingRect().left() + iconSpacing / 2;

			for (auto const& paxOnPlatform : Extended_Current_List_Pax_On_Platform) {
				std::string paxID = paxOnPlatform.first;
				int iconSize = station_size / 10;
				auto* item = new PassengerItem(pax_pixmap_scaled);
				item->setPos(QPointF(iconX - iconSize / 2, platformIcon->sceneBoundingRect().center().y() - iconSize / 2));
				item->setTransformationMode(Qt::SmoothTransformation);

				// find corresponding passenger object
				auto passengerIt = std::find_if(AllDailyPassengers.begin(), AllDailyPassengers.end(),
												[&paxID](auto const& pax) { return paxID == pax.ID; });

				// passenger found
				if (passengerIt != AllDailyPassengers.end()) {
					// add Passenger pointer to pax item
					item->passenger = &(*passengerIt);
				}

				// update current highlighted pax icon
				if (!currentlyHighlightedPaxID.empty() && item->passenger->ID == currentlyHighlightedPaxID) {
					paxIconItem = item;
				}

				scene->addItem(item);
				platformIcon->passengerIcons.push_back(item);

				iconX += iconSpacing;
			}
		}
	}
}

// slot to update passenger counter from clicked train
void MainWindow::updateTrainPaxInfo() {
	if (trainPaxInfoItem) {
		// remove existing item
		removeTrainPaxInfoIcon();

		if (trainPaxItem) {
			paintTrainPassengerInfo(trainPaxItem);
		}
	}
}

// slot to update passenger info from clicked passenger icon
void MainWindow::updatePaxIconInfo() {
	if (paxIconInfoItem) {
		// remove existing item
		removePaxInfoIcon();

		if (paxIconItem) {
			paintPassengerInfoIcon(paxIconItem);
		}
	}
}

void MainWindow::removeTrainPaxInfoIcon() {
	if (trainPaxInfoItem) {
		scene->removeItem(trainPaxInfoItem);
		delete trainPaxInfoItem;
		trainPaxInfoItem = nullptr;
	}
}

void MainWindow::removePaxInfoIcon() {
	if (paxIconInfoItem) {
		scene->removeItem(paxIconInfoItem);
		delete paxIconInfoItem;
		paxIconInfoItem = nullptr;
	}
}

void MainWindow::updateBlockOccupationStatus(Regional regional_train) {
	TrackLineItem* track;
	// std::cout << regional_train.Bs.arcs_in_signalling_block_section[0].endNode.X << " " << regional_train.trainDescription << "\n";

	// for (int i = 0; i < allArcs.size(); i++) {
	//	track = allArcs.at(i);
	for (int i = 0; i < allArcs.size(); i++) {
		for (int j = 0; j < regional_train.Bs.total_arcs; j++) {

			track = allArcs.at(i);

			if ((track->arc->startNode.X == regional_train.Bs.arcs_in_signalling_block_section[j].startNode.X) && (track->track == regional_train.Bs.trackLineId)) {
				// std::cout << regional_train.Bs.arcs_in_signalling_block_section[j].endNode.X << " " << regional_train.trainDescription << "\n";
				//  effect on clicked item
				effect = new HighlightEffect(Qt::red, 1);
				track->setGraphicsEffect(effect);
			}
		}
	}
}

void MainWindow::releaseBlockOccupationStatus() {
	TrackLineItem* track;
	// std::cout << regional_train.Bs.arcs_in_signalling_block_section[0].endNode.X << " " << regional_train.trainDescription << "\n";

	// for (int i = 0; i < allArcs.size(); i++) {
	//	track = allArcs.at(i);
	for (int i = 0; i < allArcs.size(); i++) {
		track = allArcs.at(i);
		effect = new HighlightEffect(Qt::white, 1);
		track->setGraphicsEffect(effect);
	}
}

void MainWindow::updateSignalAspect(const std::string& ID, double code, bool reversed) {
	if (m_signalsByAheadId.find(ID) == m_signalsByAheadId.end())
		return;
	for (auto* signal : m_signalsByAheadId.at(ID)) {
		if (signal->reversedDirection == reversed) {
			if (code == 270 || code == 180) {
				signal->setBrush(Qt::green);
			} else if (code == 75) {
				signal->setBrush(Qt::yellow);
			} else if (code == 0) {
				signal->setBrush(Qt::red);
			}
			signal->update();
		}
	}
}

// updates train positions given a specific timestep
void MainWindow::updateTrainPosition(int t) {
	// update every train
	releaseBlockOccupationStatus(); //
	for (int train = 0; train < numRegions; train++) {
		// check if train item exists
		TrainItemGroup* trainItem = nullptr;
		for (auto it = allTrains.begin(); it != allTrains.end(); ++it) {
			if ((*it)->index == train) {
				trainItem = *it;
				break;
			}
		}

		// trainItem found
		if (trainItem) {
			// update train position
			if (!regional_train[train].OutOfSimulation) {
				// capture previous scene center for interpolation
				QPointF oldCenter;
				auto prevIt = m_prevTrainPositions.find(train);
				if (prevIt != m_prevTrainPositions.end())
					oldCenter = prevIt.value();
				else
					oldCenter = trainItem->sceneBoundingRect().center();

				getTrainPolygonItemList(trainItem->trainPolygonItemList, t, train);
				checkVCouplingMsg(trainItem, train, t);

				updateBlockOccupationStatus(regional_train[train]);

				// animate smooth transition from old position to new position
				QPointF newCenter = trainItem->sceneBoundingRect().center();

				// update or create the speed label for this train
				QString speedText = QString::fromStdString(
					formatSpeedLabel(regional_train[train].speedKmhAt(t)));
				QGraphicsSimpleTextItem* speedLabel = m_trainSpeedLabels.value(train, nullptr);
				if (!speedLabel) {
					speedLabel = scene->addSimpleText(speedText);
					speedLabel->setZValue(1000);
					speedLabel->setBrush(Qt::black);
					m_trainSpeedLabels[train] = speedLabel;
				}
				speedLabel->setText(speedText);
				speedLabel->setVisible(true);
				m_prevTrainPositions[train] = newCenter;
				QPointF delta = oldCenter - newCenter;
				if (delta.manhattanLength() > 0.5) {
					stopTrainAnimation(train);
					trainItem->setPos(delta);
					speedLabel->setPos(newCenter.x() + delta.x() + 6, newCenter.y() + delta.y() - 18);
					QVariantAnimation* interp = new QVariantAnimation(this);
					m_trainAnimations[train] = interp;
					interp->setDuration(120);
					interp->setStartValue(QVariant::fromValue(delta));
					interp->setEndValue(QVariant::fromValue(QPointF(0, 0)));
					interp->setEasingCurve(QEasingCurve::Linear);
					connect(interp, &QVariantAnimation::valueChanged, this, [trainItem, speedLabel, newCenter](const QVariant& val) {
						QPointF offset = val.toPointF();
						trainItem->setPos(offset);
						speedLabel->setPos(newCenter.x() + offset.x() + 6, newCenter.y() + offset.y() - 18);
					});
					connect(interp, &QVariantAnimation::finished, this, [this, train, interp]() {
						if (m_trainAnimations.value(train, nullptr) == interp)
							m_trainAnimations.remove(train);
						interp->deleteLater();
					});
					interp->start();
				} else {
					stopTrainAnimation(train);
					trainItem->setPos(QPointF(0, 0));
					speedLabel->setPos(newCenter.x() + 6, newCenter.y() - 18);
				}
				if (m_followAction && m_followAction->isChecked() && m_followTrainIndex == train)
					networkView->centerOn(newCenter);
			}
			// hide train whose simulation is finished
			else {
				stopTrainAnimation(train);
				trainItem->hide();
				if (m_trainSpeedLabels.contains(train))
					m_trainSpeedLabels[train]->setVisible(false);
			}
		}
		// new train starting; >= not == because the frame throttle may drop
		// the exact departure frame, and the recorded trajectory (not the live
		// train state) is what says whether the train is on the network at t
		else if (t >= regional_train[train].departure_time &&
				 t < (int)regional_train[train].instant_spatial_position.size() &&
				 regional_train[train].instant_spatial_position[t] != -9999) {
			paintTrain(train, t, node_size, line_width, &regional_train[train]);
			if (m_followAction && m_followAction->isChecked() && m_followTrainIndex == train && !allTrains.isEmpty())
				networkView->centerOn(allTrains.last());
		}
	}

	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_VISUAL_POLISH") && !m_e2eFinished && !allTrains.isEmpty())
		runVisualPolishE2E();
}

// returns the full train shape (list of polygons)
void MainWindow::getTrainPolygonItemList(QList<TrainBodyItem*>* trainPolygonItemList, int t, int train) {
	// get the polygon of each wagon
	for (int wagon = 0; wagon <= regional_train[train].number_of_wagons; wagon++) { // using <= because train.number_of_wagons excludes loco/first wagon
		QPolygonF trainPolygon;
		getTrainPolygon(&trainPolygon, wagon, t, train);
		trainPolygonItemList->at(wagon)->setPolygon(trainPolygon);
	}
}

// returns the shape of an one-wagon train (polygon)
// used to get each wagon of a multi-wagon train
void MainWindow::getTrainPolygon(QPolygonF* trainPolygon, int wagon, int t, int train) {
	double headX, tailX, connectionX1, connectionX2, x0, x1, x2, x3, y0, y1, y2, y3;
	double dot, det, beta, alpha, gamma;
	std::vector<double> posX, routeX;
	int prevIndex, index, revPrevIndex;
	QPointF ptTrainEdge, ptConnection1, ptConnection2, ptBegin1, ptEnd2;
	std::string ID1, ID2, connection1, connection2;
	Section currBS, BS1, BS2;
	QVector<QPointF> trainPointsUp, trainPointsDown, trainPointsComplete;
	QVector<int> trainPointsStIndex, trainPointsStRegion, trainPointsGraphID;
	int revStIndex;												  // used to add station points to the polygon (needed for reversed routes)
	double total_nW = regional_train[train].number_of_wagons + 1; // train.number_of_wagons does not include loco/first wagon

	// train position on X axis
	// reversed route
	if (train_route[regional_train[train].indexOfRoute].reversed_direction) {
		headX = regional_train[train].trainXPosition(t, wagon);
		tailX = regional_train[train].trainXPosition(t, wagon + 1);
		revStIndex = 1;
	}
	// non-reversed route
	else {
		headX = regional_train[train].trainXPosition(t, wagon);
		tailX = regional_train[train].trainXPosition(t, wagon + 1);
		revStIndex = 0;
	}

	// get occupied signalling_block_sections (list of index on SeqBS)
	std::vector<int> occupiedBS; // we already know head signalling_block_sections but not the index on SeqBS

	// find block sections where the train is (full length)
	int headIndex = -1, tailIndex = -1;
	for (int h = train_route[regional_train[train].indexOfRoute].N_Block_Sections - 1; h >= 0; h--) {
		// using route axis with head at (regional_train[train].instant_spatial_position[t] - wagon * (regional_train[train].train_length / total_nW))
		if (((regional_train[train].instant_spatial_position[t] - wagon * (regional_train[train].train_length / total_nW)) < train_route[regional_train[train].indexOfRoute].sequence_of_block_sections[h].end_node.X * 1000) && ((regional_train[train].instant_spatial_position[t] - wagon * (regional_train[train].train_length / total_nW)) >= train_route[regional_train[train].indexOfRoute].sequence_of_block_sections[h].start_node.X * 1000)) {
			occupiedBS.push_back(h);
			headIndex = h;
		}
		// using route axis with tail at (regional_train[train].instant_spatial_position[t] - (wagon + 1) * (regional_train[train].train_length / total_nW))
		if (((regional_train[train].instant_spatial_position[t] - (wagon + 1) * (regional_train[train].train_length / total_nW)) < train_route[regional_train[train].indexOfRoute].sequence_of_block_sections[h].end_node.X * 1000) && ((regional_train[train].instant_spatial_position[t] - (wagon + 1) * (regional_train[train].train_length / total_nW)) >= train_route[regional_train[train].indexOfRoute].sequence_of_block_sections[h].start_node.X * 1000)) {
			if (h != headIndex) {
				occupiedBS.push_back(h);
			} // avoid adding the same signalling_block_sections twice (entire train in the same signalling_block_sections)
			tailIndex = h;
			break;
		}
		// section in between
		else if (h != headIndex && headIndex != -1) { // already added head signalling_block_sections
			occupiedBS.push_back(h);
		}
	}

	// get all relevant points from occupied signalling_block_sections (head, tail and also connection points)
	for (int i = 0; i < occupiedBS.size(); i++) { // run from head signalling_block_sections to tail signalling_block_sections (can be the same)
		currBS = train_route[regional_train[train].indexOfRoute].sequence_of_block_sections[occupiedBS[i]];

		if (headIndex == tailIndex && tailIndex == occupiedBS[i]) {
			posX = {headX, tailX};
			routeX = {(regional_train[train].instant_spatial_position[t] / 1000) - wagon * ((regional_train[train].train_length / 1000) / total_nW), (regional_train[train].instant_spatial_position[t] / 1000) - (wagon + 1) * ((regional_train[train].train_length / 1000) / total_nW)};
		} // head and tail in this section
		else if (occupiedBS[i] == headIndex) {
			posX = {headX};
			routeX = {(regional_train[train].instant_spatial_position[t] / 1000) - wagon * ((regional_train[train].train_length / 1000) / total_nW)};
		} // only head
		else if (occupiedBS[i] == tailIndex) {
			posX = {tailX};
			routeX = {(regional_train[train].instant_spatial_position[t] / 1000) - (wagon + 1) * ((regional_train[train].train_length / 1000) / total_nW)};
		} // only tail
		else {
			posX = {-DBL_MAX};
			routeX = {-DBL_MAX};
		} // section in between (no head or tail)

		// in case head and tail in the same signalling_block_sections (need to get both)
		for (int j = 0; j < posX.size(); j++) {
			// single signalling_block_sections -> get head or tail
			if (currBS.ID.find('/') == std::string::npos) {
				if (occupiedBS[i] != headIndex && occupiedBS[i] != tailIndex) {
					continue;
				} // single section in between (no relevant points)

				// get station index to extract shifts
				int stationIdx[2];
				neighbourStations(posX[j], currBS.trackLineId, stationIdx);
				prevIndex = stationIdx[0];
				index = stationIdx[1];
				revPrevIndex = index * (1 - revStIndex) + prevIndex * revStIndex; // if reversed route, gives previous station

				// get point
				egtrainPoint2Screen(posX[j], currBS.trackLineId, track_separation, ptTrainEdge.rx(), ptTrainEdge.ry());

				// add points to vectors
				trainPointsUp.push_back(QPointF(ptTrainEdge.x() - StationArray[index].signalDeltaX[blockSets[currBS.trackLineId].region] * 0.10 * track_separation, ptTrainEdge.y() - StationArray[index].signalDeltaY[blockSets[currBS.trackLineId].region] * 0.10 * track_separation));
				trainPointsDown.push_front(QPointF(ptTrainEdge.x() + StationArray[index].signalDeltaX[blockSets[currBS.trackLineId].region] * 0.10 * track_separation, ptTrainEdge.y() + StationArray[index].signalDeltaY[blockSets[currBS.trackLineId].region] * 0.10 * track_separation));
				trainPointsStIndex.push_back(revPrevIndex);
				trainPointsStRegion.push_back(blockSets[currBS.trackLineId].region);
				trainPointsGraphID.push_back(blockSets[currBS.trackLineId].graphID);
			}
			// compound signalling_block_sections -> get connection points and/or tail or head
			else {
				int index1 = currBS.ID.find("@-");
				int index2 = currBS.ID.find("/@", index1 + 1);
				int index3 = currBS.ID.find("@-", index2 + 1);
				if (index1 != std::string::npos && index2 != std::string::npos && index3 != std::string::npos) {
					ID1 = currBS.ID.substr(0, index1 + 1);
					ID2 = currBS.ID.substr(index2 + 1, index3 - index2);
					connection1 = currBS.ID.substr(index1 + 2, index2 - index1 - 2);
					connection2 = currBS.ID.substr(index3 + 2, string::npos);
					connectionX1 = atof(connection1.c_str());
					connectionX2 = atof(connection2.c_str());

					// find single block sections (needed for the graphID's)
					for (int b = 0; b < Blocks; b++) {
						if (ID1.compare(signalling_block_sections[b].ID) == 0) {
							BS1 = signalling_block_sections[b];
						} else if (ID2.compare(signalling_block_sections[b].ID) == 0) {
							BS2 = signalling_block_sections[b];
						}
					}

					// get beginning and end of connection to interpolate
					egtrainPoint2Screen(connectionX1, BS1.trackLineId, track_separation, ptConnection1.rx(), ptConnection1.ry());
					egtrainPoint2Screen(connectionX2, BS2.trackLineId, track_separation, ptConnection2.rx(), ptConnection2.ry());

					// get beginning of BS1 and end of BS2 to calculate shifts
					egtrainPoint2Screen(BS1.start_node.X, BS1.trackLineId, track_separation, ptBegin1.rx(), ptBegin1.ry());
					egtrainPoint2Screen(BS2.end_node.X, BS2.trackLineId, track_separation, ptEnd2.rx(), ptEnd2.ry());

					// get station indexes (used to add station points)
					int stationIdx[2];
					neighbourStations(connectionX1, BS1.trackLineId, stationIdx);
					int prevIndexConnection1 = stationIdx[0];
					int indexConnection1 = stationIdx[1];
					int revPrevIndexConnection1 = indexConnection1 * (1 - revStIndex) + prevIndexConnection1 * revStIndex; // if reversed route, gives previous sation
					neighbourStations(connectionX2, BS2.trackLineId, stationIdx);
					int prevIndexConnection2 = stationIdx[0];
					int indexConnection2 = stationIdx[1];
					int revPrevIndexConnection2 = indexConnection2 * (1 - revStIndex) + prevIndexConnection2 * revStIndex; // if reversed route, gives previous sation

					// linear interpolation of geodetic coordinates using known coordinates of closest stations
					x0 = ptBegin1.x();
					y0 = ptBegin1.y();
					x1 = ptConnection1.x();
					y1 = ptConnection1.y();
					x2 = ptConnection2.x();
					y2 = ptConnection2.y();
					x3 = ptEnd2.x();
					y3 = ptEnd2.y();

					// shift on beginning of connection
					dot = (x0 - x1) * (x2 - x1) + (y0 - y1) * (y2 - y1); // dot product
					det = (x0 - x1) * (y2 - y1) - (y0 - y1) * (x2 - x1); // determinant
					alpha = atan2(det, dot);							 // atan2(y, x)
					if (alpha < 0) {
						alpha += 2 * PI;
					} // adjust alpha to 0 to 2*PI range
					beta = atan2(-(y0 - y1), (x0 - x1));
					gamma = alpha / 2 - beta;
					double connectionStartShiftX = cos(gamma);
					double connectionStartShiftY = sin(gamma);

					// shift on end of connection
					dot = (x1 - x2) * (x3 - x2) + (y1 - y2) * (y3 - y2); // dot product
					det = (x1 - x2) * (y3 - y2) - (y1 - y2) * (x3 - x2); // determinant
					alpha = atan2(det, dot);							 // atan2(y, x)
					if (alpha < 0) {
						alpha += 2 * PI;
					} // adjust alpha to 0 to 2*PI range
					beta = atan2(-(y1 - y2), (x1 - x2));
					gamma = alpha / 2 - beta;
					double connectionEndShiftX = cos(gamma);
					double connectionEndShiftY = sin(gamma);

					// calculate connection deltas (inside connection)
					beta = atan2(-(y1 - y2), (x1 - x2));
					double connectionDeltaX = sin(beta);
					double connectionDeltaY = cos(beta);

					// add connection points if it is a middle section
					// add connection points if it is the tail signalling_block_sections and head already added
					if ((posX[j] == -DBL_MAX) || (posX.size() == 1 && occupiedBS[i] == tailIndex)) { // adding connection points before adding tail of the train
						// non-reversed route (add first connection 2)
						if (!train_route[regional_train[train].indexOfRoute].reversed_direction) {
							if (headX > connectionX2 && tailX < connectionX2) {
								trainPointsUp.push_back(QPointF(ptConnection2.x() - connectionEndShiftX * 0.10 * track_separation, ptConnection2.y() - connectionEndShiftY * 0.10 * track_separation));
								trainPointsDown.push_front(QPointF(ptConnection2.x() + connectionEndShiftX * 0.10 * track_separation, ptConnection2.y() + connectionEndShiftY * 0.10 * track_separation));
								trainPointsStIndex.push_back(revPrevIndexConnection2);
								trainPointsStRegion.push_back(blockSets[BS2.trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS2.trackLineId].graphID);
							}
							if (headX > connectionX1 && tailX < connectionX1) {
								trainPointsUp.push_back(QPointF(ptConnection1.x() - connectionStartShiftX * 0.10 * track_separation, ptConnection1.y() - connectionStartShiftY * 0.10 * track_separation));
								trainPointsDown.push_front(QPointF(ptConnection1.x() + connectionStartShiftX * 0.10 * track_separation, ptConnection1.y() + connectionStartShiftY * 0.10 * track_separation));
								trainPointsStIndex.push_back(revPrevIndexConnection1);
								trainPointsStRegion.push_back(blockSets[BS1.trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS1.trackLineId].graphID);
							}
						}
						// reversed route (add first connection 1)
						else {
							if (tailX > connectionX1 && headX < connectionX1) {
								trainPointsUp.push_back(QPointF(ptConnection1.x() - connectionStartShiftX * 0.10 * track_separation, ptConnection1.y() - connectionStartShiftY * 0.10 * track_separation));
								trainPointsDown.push_front(QPointF(ptConnection1.x() + connectionStartShiftX * 0.10 * track_separation, ptConnection1.y() + connectionStartShiftY * 0.10 * track_separation));
								trainPointsStIndex.push_back(revPrevIndexConnection1);
								trainPointsStRegion.push_back(blockSets[BS1.trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS1.trackLineId].graphID);
							}
							if (tailX > connectionX2 && headX < connectionX2) {
								trainPointsUp.push_back(QPointF(ptConnection2.x() - connectionEndShiftX * 0.10 * track_separation, ptConnection2.y() - connectionEndShiftY * 0.10 * track_separation));
								trainPointsDown.push_front(QPointF(ptConnection2.x() + connectionEndShiftX * 0.10 * track_separation, ptConnection2.y() + connectionEndShiftY * 0.10 * track_separation));
								trainPointsStIndex.push_back(revPrevIndexConnection2);
								trainPointsStRegion.push_back(blockSets[BS2.trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS2.trackLineId].graphID);
							}
						}
					}

					// head/tail in the 1st track and before connection
					if (routeX[j] >= currBS.start_node.X && posX[j] <= connectionX1) {
						// get station index to extract shifts
						int stationIdx[2];
						neighbourStations(posX[j], BS1.trackLineId, stationIdx);
						prevIndex = stationIdx[0];
						index = stationIdx[1];
						revPrevIndex = index * (1 - revStIndex) + prevIndex * revStIndex; // if reversed route, gives previous station

						egtrainPoint2Screen(posX[j], BS1.trackLineId, track_separation, ptTrainEdge.rx(), ptTrainEdge.ry());
						trainPointsUp.push_back(QPointF(ptTrainEdge.x() - StationArray[index].signalDeltaX[blockSets[BS1.trackLineId].region] * 0.10 * track_separation, ptTrainEdge.y() - StationArray[index].signalDeltaY[blockSets[BS1.trackLineId].region] * 0.10 * track_separation));
						trainPointsDown.push_front(QPointF(ptTrainEdge.x() + StationArray[index].signalDeltaX[blockSets[BS1.trackLineId].region] * 0.10 * track_separation, ptTrainEdge.y() + StationArray[index].signalDeltaY[blockSets[BS1.trackLineId].region] * 0.10 * track_separation));
						trainPointsStIndex.push_back(revPrevIndex);
						trainPointsStRegion.push_back(blockSets[BS1.trackLineId].region);
						trainPointsGraphID.push_back(blockSets[BS1.trackLineId].graphID);
					}
					// head/tail inside the connection
					else if (posX[j] > connectionX1 && posX[j] < connectionX2) {
						ptTrainEdge.setX(x1 + (((posX[j] - connectionX1) / (connectionX2 - connectionX1)) * (x2 - x1)));
						ptTrainEdge.setY(y1 + (((posX[j] - connectionX1) / (connectionX2 - connectionX1)) * (y2 - y1)));

						// train polygon
						trainPointsUp.push_back(QPointF(ptTrainEdge.x() - connectionDeltaX * 0.10 * track_separation, ptTrainEdge.y() - connectionDeltaY * 0.10 * track_separation));
						trainPointsDown.push_front(QPointF(ptTrainEdge.x() + connectionDeltaX * 0.10 * track_separation, ptTrainEdge.y() + connectionDeltaY * 0.10 * track_separation));
						trainPointsStIndex.push_back(-1);
						trainPointsStRegion.push_back(-1);
						trainPointsGraphID.push_back(INT_MIN);
					}
					// head/tail in the 2nd track and after connection
					else if (posX[j] >= connectionX2 && routeX[j] < currBS.end_node.X) {
						// get station index to extract shifts
						int stationIdx[2];
						neighbourStations(posX[j], BS2.trackLineId, stationIdx);
						prevIndex = stationIdx[0];
						index = stationIdx[1];
						revPrevIndex = index * (1 - revStIndex) + prevIndex * revStIndex; // if reversed route, gives previous station

						egtrainPoint2Screen(posX[j], BS2.trackLineId, track_separation, ptTrainEdge.rx(), ptTrainEdge.ry());
						trainPointsUp.push_back(QPointF(ptTrainEdge.x() - StationArray[index].signalDeltaX[blockSets[BS2.trackLineId].region] * 0.10 * track_separation, ptTrainEdge.y() - StationArray[index].signalDeltaY[blockSets[BS2.trackLineId].region] * 0.10 * track_separation));
						trainPointsDown.push_front(QPointF(ptTrainEdge.x() + StationArray[index].signalDeltaX[blockSets[BS2.trackLineId].region] * 0.10 * track_separation, ptTrainEdge.y() + StationArray[index].signalDeltaY[blockSets[BS2.trackLineId].region] * 0.10 * track_separation));
						trainPointsStIndex.push_back(revPrevIndex);
						trainPointsStRegion.push_back(blockSets[BS2.trackLineId].region);
						trainPointsGraphID.push_back(blockSets[BS2.trackLineId].graphID);
					}

					// add connection points if it is head section and tail is in another signalling_block_sections
					// add connection points if head and tail in the same section
					if ((occupiedBS[i] == headIndex && headIndex != tailIndex) || (posX.size() == 2 && j == 0)) { // adding connection points after adding head of the train
						// non-reversed route (add first connection 2)
						if (!train_route[regional_train[train].indexOfRoute].reversed_direction) {
							if (headX > connectionX2 && tailX < connectionX2) {
								trainPointsUp.push_back(QPointF(ptConnection2.x() - connectionEndShiftX * 0.10 * track_separation, ptConnection2.y() - connectionEndShiftY * 0.10 * track_separation));
								trainPointsDown.push_front(QPointF(ptConnection2.x() + connectionEndShiftX * 0.10 * track_separation, ptConnection2.y() + connectionEndShiftY * 0.10 * track_separation));
								trainPointsStIndex.push_back(revPrevIndexConnection2);
								trainPointsStRegion.push_back(blockSets[BS2.trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS2.trackLineId].graphID);
							}
							if (headX > connectionX1 && tailX < connectionX1) {
								trainPointsUp.push_back(QPointF(ptConnection1.x() - connectionStartShiftX * 0.10 * track_separation, ptConnection1.y() - connectionStartShiftY * 0.10 * track_separation));
								trainPointsDown.push_front(QPointF(ptConnection1.x() + connectionStartShiftX * 0.10 * track_separation, ptConnection1.y() + connectionStartShiftY * 0.10 * track_separation));
								trainPointsStIndex.push_back(revPrevIndexConnection1);
								trainPointsStRegion.push_back(blockSets[BS1.trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS1.trackLineId].graphID);
							}
						}
						// reversed route (add first connection 1)
						else {
							if (tailX > connectionX1 && headX < connectionX1) {
								trainPointsUp.push_back(QPointF(ptConnection1.x() - connectionStartShiftX * 0.10 * track_separation, ptConnection1.y() - connectionStartShiftY * 0.10 * track_separation));
								trainPointsDown.push_front(QPointF(ptConnection1.x() + connectionStartShiftX * 0.10 * track_separation, ptConnection1.y() + connectionStartShiftY * 0.10 * track_separation));
								trainPointsStIndex.push_back(revPrevIndexConnection1);
								trainPointsStRegion.push_back(blockSets[BS1.trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS1.trackLineId].graphID);
							}
							if (tailX > connectionX2 && headX < connectionX2) {
								trainPointsUp.push_back(QPointF(ptConnection2.x() - connectionEndShiftX * 0.10 * track_separation, ptConnection2.y() - connectionEndShiftY * 0.10 * track_separation));
								trainPointsDown.push_front(QPointF(ptConnection2.x() + connectionEndShiftX * 0.10 * track_separation, ptConnection2.y() + connectionEndShiftY * 0.10 * track_separation));
								trainPointsStIndex.push_back(revPrevIndexConnection2);
								trainPointsStRegion.push_back(blockSets[BS2.trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS2.trackLineId].graphID);
							}
						}
					}
				}
			}
		}
	}

	// add station points to polygon when necessary (change in shifts)
	int added = 0; // needed when adding new points to the vectors
	for (int i = 1; i < trainPointsStIndex.size(); i++) {
		// inside connection (no change in shift)
		if (trainPointsStIndex[i] == -1) {
			continue;
		}

		// change in shift when previous point is a connection (assuming no consecutive points inside connection)
		// change in shift when previous point is not a connection
		// ignore change in shift if region also changes
		if ((trainPointsStIndex[i - 1] == -1 && i >= 2 && trainPointsStIndex[i] != trainPointsStIndex[i - 2] && trainPointsStRegion[i] == trainPointsStRegion[i - 2]) ||
			(trainPointsStIndex[i - 1] != -1 && trainPointsStIndex[i] != trainPointsStIndex[i - 1] && trainPointsStRegion[i] == trainPointsStRegion[i - 1])) {

			QPointF stationUp = QPointF(StationArray[trainPointsStIndex[i]].graphX + StationArray[trainPointsStIndex[i]].shiftX[trainPointsStRegion[i]] * track_separation * (trainPointsGraphID[i] - 0.10), StationArray[trainPointsStIndex[i]].graphY + StationArray[trainPointsStIndex[i]].shiftY[trainPointsStRegion[i]] * track_separation * (trainPointsGraphID[i] - 0.10));
			QPointF stationDown = QPointF(StationArray[trainPointsStIndex[i]].graphX + StationArray[trainPointsStIndex[i]].shiftX[trainPointsStRegion[i]] * track_separation * (trainPointsGraphID[i] + 0.10), StationArray[trainPointsStIndex[i]].graphY + StationArray[trainPointsStIndex[i]].shiftY[trainPointsStRegion[i]] * track_separation * (trainPointsGraphID[i] + 0.10));
			trainPointsUp.insert(i + added, stationUp);
			trainPointsDown.insert(i + added, stationDown);
			added++;
		}
	}

	// join Up and Down vectors to build the polygon
	trainPointsComplete.append(trainPointsUp);
	trainPointsComplete.append(trainPointsDown);

	QPolygonF UpdatedTrainPolygon(trainPointsComplete);
	trainPolygon->swap(UpdatedTrainPolygon);
}

// display train path diagrams for all corridors (only working for two)
void MainWindow::displayTrainPathDiagrams() {
	// check if there is a 2nd corridor
	int noCorridors = 1;
	for (int i = 0; i < numRegions; i++) {
		if (train_route[regional_train[i].indexOfRoute].corridor == "blockSets") {
			noCorridors++;
			break;
		}
	}

	// generate a diagram for each corridor
	for (int i = 0; i < noCorridors; i++) {
		std::string corridor(1, char('A' + i));
		buildCorridorTrainPathDiagram(corridor);
	}
}

// build train path diagram for a single corridor
void MainWindow::buildCorridorTrainPathDiagram(std::string corridor) {
	double minRangeX = DBL_MAX, maxRangeX = -1;
	QList<QLineSeries*> seriesToAdd;
	std::pair<double, double> corridorJumpX = {0, 0}; // length, lower bound X

	// create chart
	QChart* chart = new QChart();

	// custom title
	QString title = "Train Path Diagram: corridor ";
	title.append(QString::fromStdString(corridor));
	QFont font;
	font.setPixelSize(18);
	chart->setTitleFont(font);
	chart->setTitleBrush(QBrush(Qt::black));
	chart->setTitle(title);

	// create series for each train
	bool NoPrint[300];
	for (int k = 0; k < 300; k++) {
		NoPrint[k] = false;
	}

	for (int i = 0; i < numRegions; i++) {
		// check route corridor
		if (train_route[regional_train[i].indexOfRoute].corridor == corridor) {
			QLineSeries* trainSeries = new QLineSeries();

			for (int t = 0; t < initial_variables.times; t++) {
				if ((regional_train[i].instant_spatial_position[t] == -9999) || (t == regional_train[i].End_Time + 1))
					NoPrint[i] = true;

				if ((regional_train[i].instant_spatial_position[t] != -9999) && (NoPrint[i] == 0))
					// non-reversed route (same with/without jump)
					if (!train_route[regional_train[i].indexOfRoute].reversed_direction) {
						*trainSeries << QPointF(t * timestep, (regional_train[i].instant_spatial_position[t]) / 1000);
					}
					// reversed route without jump
					else if (train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.first == 0) {
						*trainSeries << QPointF(t * timestep, (train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute - regional_train[i].instant_spatial_position[t]) / 1000);
					}
					// reversed route with jump
					else {
						*trainSeries << QPointF(t * timestep, ((train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute - regional_train[i].instant_spatial_position[t]) / 1000) - train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.first);
						corridorJumpX.first = train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.first;
						corridorJumpX.second = train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.second;
					}

				else
					break;
			}

			// check if series has points (simulated train)
			if (trainSeries->count() != 0) {
				// update X range
				if (!train_route[regional_train[i].indexOfRoute].reversed_direction) {
					if (trainSeries->pointsVector().at(0).y() < minRangeX) {
						minRangeX = trainSeries->pointsVector().at(0).y();
					}
					if (trainSeries->pointsVector().at(trainSeries->count() - 1).y() > maxRangeX) {
						maxRangeX = trainSeries->pointsVector().at(trainSeries->count() - 1).y();
					}
				} else {
					if (trainSeries->pointsVector().at(trainSeries->count() - 1).y() < minRangeX) {
						minRangeX = trainSeries->pointsVector().at(trainSeries->count() - 1).y();
					}
					if (trainSeries->pointsVector().at(0).y() > maxRangeX) {
						maxRangeX = trainSeries->pointsVector().at(0).y();
					}
				}

				trainSeries->setName(QString::fromStdString(regional_train[i].trainDescription));

				// add to list of series
				seriesToAdd.append(trainSeries);
			} else {
				// delete series
				delete trainSeries;
			}
		}
	}

	// division of time axis
	int interval = 300; // fractions of 5 min
	if (initial_variables.times * timestep > 7200) {
		interval *= 2; // increase to 10min (long simulations)
	}
	int numIntervals = ceil(initial_variables.times * timestep / interval);

	// define x axis (time)
	QValueAxis* axisX = new QValueAxis;
	axisX->setRange(0, interval * numIntervals); // t = [0, (times-1)*timestep]
	axisX->setTickCount(numIntervals + 1);
	axisX->setLabelFormat("%.0f");
	axisX->setTitleText("Time (s)");
	chart->addAxis(axisX, Qt::AlignTop);

	// define y axis (distance)
	QValueAxis* axisY = new QValueAxis;
	axisY->setTickType(QValueAxis::TicksDynamic);
	axisY->setTickAnchor(0);
	axisY->setTickInterval(5); // ticks every 5km
	axisY->setReverse(true);   // revert y axis
	axisY->setLabelFormat("%.0f");
	axisY->setGridLineVisible(false); // hide grid lines
	axisY->setTitleText("Distance (km)");
	chart->addAxis(axisY, Qt::AlignLeft);

	// define y axis (stations)
	QCategoryAxis* axisYStations = new QCategoryAxis;
	// create ticks with station names
	for (int i = 0; i < numStations; i++) {
		// ignore stations out of range (correct maxRange for jump)
		if ((StationArray[i].X <= (maxRangeX + corridorJumpX.first + 0.001)) && StationArray[i].X >= (minRangeX - 0.001)) {
			// check if station belongs to the corridor
			if (std::find(StationArray[i].corridors.begin(), StationArray[i].corridors.end(), corridor) != StationArray[i].corridors.end()) {
				// correct X in case of jump
				if (corridorJumpX.first != 0 && StationArray[i].X > corridorJumpX.second) {
					axisYStations->append(QString::fromStdString(StationArray[i].stationName), (StationArray[i].X - corridorJumpX.first));
				}
				// no jump
				else {
					axisYStations->append(QString::fromStdString(StationArray[i].stationName), StationArray[i].X);
				}
			}
		}
	}
	axisYStations->setRange(floor(minRangeX), ceil(maxRangeX));
	axisYStations->setReverse(true); // revert y axis
	axisYStations->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
	chart->addAxis(axisYStations, Qt::AlignRight);

	// add series to chart
	for (int i = 0; i < seriesToAdd.count(); i++) {
		// add series to chart
		chart->addSeries(seriesToAdd.at(i));

		// attach axes to series
		seriesToAdd.at(i)->attachAxis(axisX);
		seriesToAdd.at(i)->attachAxis(axisYStations);
		seriesToAdd.at(i)->attachAxis(axisY);
	}

	// create chartView
	QChartView* chartView = new QChartView(chart);
	chartView->setRenderHint(QPainter::Antialiasing);

	// create layout for dialog
	QVBoxLayout* chartWindowLayout = new QVBoxLayout();

	// add chartView to layout
	chartWindowLayout->addWidget(chartView);

	// create dialog (second window)
	QDialog* chartWindow = new QDialog(this);
	chartWindow->setModal(false);
	chartWindow->setWindowTitle(title);

	// set layout of dialog
	chartWindow->setLayout(chartWindowLayout);

	// adjust window size
	chartWindow->resize(1000, 600);

	// open dialog
	chartWindow->show();
}

void MainWindow::askForTrainPathDiagram() {
	// modal prompts deadlock the env-gated smoke runs
	if (e2eDialogsSuppressed())
		return;

	// dialog to ask for train path diagram
	QMessageBox::StandardButton reply;
	reply = QMessageBox::question(this, "Simulation Finished", "Do you want to generate the train path diagrams?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (reply == QMessageBox::Yes) {
		displayTrainPathDiagrams();
	}

	// version not showing train path diagram
	QMessageBox msgBox;
	msgBox.setText("Simulation finished.");
	msgBox.exec();
}

// manage VCoupling notifications
void MainWindow::checkVCouplingMsg(TrainItemGroup* trainItem, int train, int t) {
	//// hide signals
	// if (t == 101) {
	//	for (int i = 0; i < allSignals.size(); i++) {
	//		allSignals.at(i)->group()->hide();
	//	}
	// }

	// TEST CAMERA VIEW
	/*if (trainItem->trainPolygonItemList->size() > 2 && trainItem->trainPolygonItemList->at(2)->polygon().size() > 3 && trainItem->train->trainDescription.compare(regional_train[0].trainDescription) == 0) {
		QPointF centerPoint;
		centerPoint.setX(0.5 * (trainItem->trainPolygonItemList->at(0)->polygon().last().x() + trainItem->trainPolygonItemList->at(2)->polygon().first().x()));
		centerPoint.setY(0.5 * (trainItem->trainPolygonItemList->at(0)->polygon().last().y() + trainItem->trainPolygonItemList->at(2)->polygon().first().y()));
		networkView->centerOn(centerPoint);
	}*/
	// if (/*t >= 342 &&*/ allTrains.size() > 1 && allTrains.at(1)->trainPolygonItemList->size() > 2 && allTrains.at(1)->trainPolygonItemList->at(2)->polygon().size() > 3 && trainItem->train->trainDescription.compare(regional_train[1].trainDescription) == 0) {
	//	QPointF centerPoint;
	//	centerPoint.setX(0.5 * (allTrains.at(0)->trainPolygonItemList->at(0)->polygon().last().x() + allTrains.at(1)->trainPolygonItemList->at(2)->polygon().first().x()));
	//	centerPoint.setY(0.5 * (allTrains.at(0)->trainPolygonItemList->at(0)->polygon().last().y() + allTrains.at(1)->trainPolygonItemList->at(2)->polygon().first().y()));
	//	networkView->centerOn(centerPoint);

	//	// always showing window around trains
	//	/*QPointF rectTopLeft = QPointF(allTrains.at(0)->trainPolygonItemList->at(0)->polygon().last().x(), allTrains.at(1)->trainPolygonItemList->at(2)->polygon().first().y());
	//	qreal rectW = allTrains.at(1)->trainPolygonItemList->at(2)->polygon().first().x() - allTrains.at(0)->trainPolygonItemList->at(0)->polygon().last().x();
	//	qreal rectH = allTrains.at(0)->trainPolygonItemList->at(0)->polygon().last().y() - allTrains.at(1)->trainPolygonItemList->at(2)->polygon().first().y();
	//	QRectF rectToShow = QRectF(rectTopLeft.x(), rectTopLeft.y(), rectW, rectH);
	//	// expand scene rect so that items can be centered when zooming
	//	QRectF initialSceneRect = rectToShow;
	//	qreal expansionFactorX = 0.5;
	//	qreal expansionFactorY = 0.5;
	//	rectToShow.adjust(-0.5 * expansionFactorX * initialSceneRect.width(), -0.5 * expansionFactorY * initialSceneRect.height(), 0.5 * expansionFactorX * initialSceneRect.width(), 0.5 * expansionFactorY * initialSceneRect.height());
	//	networkView->fitInView(rectToShow, Qt::KeepAspectRatio);*/
	//}

	// delete 'old' messages
	for (int i = 0; i < VCmsgTimestep.size();) {
		if (t > (VCmsgTimestep[i] + 40)) { // remove message after 40 timesteps
			VCmsgTimestep.erase(VCmsgTimestep.begin() + i);
			VCmsgTrain.erase(VCmsgTrain.begin() + i);
			VCmsgText.erase(VCmsgText.begin() + i);

			// remove group and child items from scene (if not removed yet)
			if (VCmsgItems[i]) {
				qDeleteAll(VCmsgItems[i]->childItems());
				scene->destroyItemGroup(VCmsgItems[i]);
			}
			VCmsgItems.erase(VCmsgItems.begin() + i);
		} else {
			++i;
		}
	}

	// check messages (reversed loop to print most recent message in case of two close messages)
	int updatedTrain = false;
	for (int i = VCmsgText.size() - 1; i >= 0; i--) {

		// ignore messages of other trains
		if (VCmsgTrain[i].compare(regional_train[train].trainDescription) != 0) {
			continue;
		}

		// new message
		if (t == VCmsgTimestep[i]) {
			// paint notification
			paintVCouplingMsg(trainItem, VCmsgText[i], t, i);
			updatedTrain = true;
		}

		// message to update (position)
		else if (t > VCmsgTimestep[i] && t <= (VCmsgTimestep[i] + 40)) { // keep message during 40 timesteps (or until new message)
			// remove current group and child items from scene (if not removed yet)
			if (VCmsgItems[i]) {
				qDeleteAll(VCmsgItems[i]->childItems());
				scene->destroyItemGroup(VCmsgItems[i]);
				VCmsgItems[i] = nullptr;
			}

			// paint updated message
			if (!updatedTrain) {
				paintVCouplingMsg(trainItem, VCmsgText[i], t, i);
				updatedTrain = true;
			}
		}
	}
}

// paint VCoupling notification
void MainWindow::paintVCouplingMsg(TrainItemGroup* trainItem, string message, int t, int msgIndex) {
	// train head graphical coordinates
	TrainBodyItem* headPolygon = trainItem->trainPolygonItemList->at(0);
	QPointF frontUp = headPolygon->polygon().first();
	QPointF frontDown = headPolygon->polygon().last();

	// create line from train to message
	QPointF start = frontUp + frontDown;
	start /= 2;
	qreal dx = 0;
	qreal dy = -2 * track_separation;
	QPointF end = start + QPointF(dx, dy);

	// paint line
	QPen pen = QPen(QColor(242, 161, 106));
	pen.setWidth(line_width);
	pen.setCosmetic(true);

	// draws a line from start to end with a given line width
	QGraphicsLineItem* line = new QGraphicsLineItem(QLineF(start.x(), start.y(), end.x(), end.y()));
	line->setPen(pen);

	// text box position
	QPointF coord = end; // coord is the center of box and text in x-axis and bottom of box in y-axis

	// set text size
	QFont font = QFont();
	font.setPixelSize((int)station_size / 6);

	// paint text
	QGraphicsTextItem* text = new QGraphicsTextItem;
	text->setPlainText(QString::fromStdString(message)); // text without background
	text->setDefaultTextColor(Qt::white);
	text->setFont(font);

	// draw box around text
	QGraphicsRectItem* textBox = new QGraphicsRectItem;
	textBox->setRect(QRectF(0, 0, 1.25 * text->boundingRect().width(), 1.25 * text->boundingRect().height())); // box 25% bigger than text rect on both directions
	textBox->setBrush(QColor(242, 161, 106));
	textBox->setPos(coord.x() - (textBox->boundingRect().width() / 2), coord.y() - (textBox->boundingRect().height()));

	// set text position (center of text box)
	QPointF textPos = textBox->pos();
	textPos.rx() += 0.5 * (textBox->boundingRect().width() - text->boundingRect().width());
	textPos.ry() += 0.5 * (textBox->boundingRect().height() - text->boundingRect().height());
	text->setPos(textPos);

	// create group
	QGraphicsItemGroup* msgGroup = new QGraphicsItemGroup;
	msgGroup->addToGroup(line);
	msgGroup->addToGroup(textBox);
	msgGroup->addToGroup(text);
	msgGroup->setZValue(5);

	// add group to scene
	scene->addItem(msgGroup);

	// add/update group on items vector
	if (msgIndex < VCmsgItems.size()) {
		VCmsgItems[msgIndex] = msgGroup;
	}
}

// hides all objects of unused tracks (including connections)
void MainWindow::hideUnusedTracks() {
	std::list<int> unusedTracks = std::list<int>();
	int track;

	// collect unused tracks from file
	ifstream file(initial_variables.InputMainFolder + "/GUI/unusedTracks.txt");
	if (file.is_open()) {
		while (file >> track) {
			unusedTracks.push_back(track);
		}
	}

	// tracks were not collected correctly
	if (unusedTracks.empty()) {
		return;
	}

	// hide items from unused tracks
	for (auto& item : scene->items()) {
		// filter nodes
		NodeItem* Node = qgraphicsitem_cast<NodeItem*>(item);

		if (Node) {
			auto it = std::find(unusedTracks.begin(), unusedTracks.end(), Node->track);
			if (it != unusedTracks.end()) {
				Node->hide();
			}
		}

		// filter station nodes
		StationNodeItem* stationNode = qgraphicsitem_cast<StationNodeItem*>(item);

		if (stationNode) {
			auto it = std::find(unusedTracks.begin(), unusedTracks.end(), stationNode->track);
			if (it != unusedTracks.end()) {
				stationNode->hide();
			}
		}

		// filter arcs (Arc line items)
		TrackLineItem* Arc = qgraphicsitem_cast<TrackLineItem*>(item);

		if (Arc) {
			auto it = std::find(unusedTracks.begin(), unusedTracks.end(), Arc->track);
			if (it != unusedTracks.end()) {
				Arc->hide();
			}
		}

		// filter connections (connection line items)
		ConnectionItem* connection = qgraphicsitem_cast<ConnectionItem*>(item);

		if (connection) {
			auto itFirst = std::find(unusedTracks.begin(), unusedTracks.end(), connection->connection->idFirstTrackLine);
			auto itSecond = std::find(unusedTracks.begin(), unusedTracks.end(), connection->connection->idSecondTrackLine);
			if ((itFirst != unusedTracks.end()) || (itSecond != unusedTracks.end())) {
				connection->hide();
			}
		}

		// filter signals (signalling group items)
		SignalItem* signal = qgraphicsitem_cast<SignalItem*>(item);

		if (signal) {
			if (signal->sectionAhead) {
				auto itAhead = std::find(unusedTracks.begin(), unusedTracks.end(), signal->sectionAhead->trackLineId);
				if (itAhead != unusedTracks.end()) {
					signal->parentItem()->hide(); // hide group
				}
			} else if (signal->sectionBehind) {
				auto itBehind = std::find(unusedTracks.begin(), unusedTracks.end(), signal->sectionBehind->trackLineId);
				if (itBehind != unusedTracks.end()) {
					signal->parentItem()->hide(); // hide group
				}
			}
		}
	}
}

// set graphical levels/regions and route corridors for the case study from file (manually assigned)
void MainWindow::setGraphIDsCaseStudy() {
	std::string line;

	// read trackline levels from file
	ifstream levelsFile(initial_variables.InputMainFolder + "/GUI/caseStudyTrackData.txt");
	if (levelsFile.is_open()) {
		while (getline(levelsFile, line)) {
			// ignore empty lines (in the end of the files)
			if (line.empty())
				continue;

			std::string token;
			std::vector<std::string> tokens;
			char delimiter = '\t';
			std::istringstream tokenStream(line);
			while (std::getline(tokenStream, token, delimiter)) {
				tokens.push_back(token);
			}

			int track = std::stoi(tokens[0]);

			// assign level
			if (!tokens[1].empty()) {
				blockSets[track].graphID = std::stoi(tokens[1]);
			}

			// assign region
			blockSets[track].region = std::stoi(tokens[2]);
		}
	}

	// read route corridors from file
	ifstream corridorsFile(initial_variables.InputMainFolder + "/GUI/caseStudyRouteCorridors.txt");
	if (corridorsFile.is_open()) {
		while (getline(corridorsFile, line)) {
			// ignore empty lines (in the end of the files)
			if (line.empty())
				continue;

			std::string token;
			std::vector<std::string> tokens;
			char delimiter = '\t';
			std::istringstream tokenStream(line);
			while (std::getline(tokenStream, token, delimiter)) {
				tokens.push_back(token);
			}

			int routeID = std::stoi(tokens[0]);

			// assign corridor
			if (!tokens[1].empty()) {
				train_route[routeID].corridor = tokens[1][0];
			}
		}
	}
}

// draw virtual inter region connections
void MainWindow::drawVirtualInterRegionConnections() {
	std::string line;

	// read data from file from file
	ifstream file(initial_variables.InputMainFolder + "/GUI/caseStudyVirtualInterRegionConnections.txt");
	if (file.is_open()) {
		while (getline(file, line)) {
			// ignore empty lines (in the end of the files)
			if (line.empty())
				continue;

			std::string token;
			std::vector<std::string> tokens;
			char delimiter = '\t';
			std::istringstream tokenStream(line);
			while (std::getline(tokenStream, token, delimiter)) {
				tokens.push_back(token);
			}

			int firstTrack = std::stoi(tokens[0]);

			double X1 = std::stod(tokens[1]);

			int secondTrack = std::stoi(tokens[2]);

			double X2 = std::stod(tokens[3]);

			// create (virtual) connection object
			Connections virtualConnection;
			virtualConnection.ID = "virtualInterRegionConnection";
			virtualConnection.idFirstTrackLine = firstTrack;
			virtualConnection.idSecondTrackLine = secondTrack;
			virtualConnection.xFirstNode = X1;
			virtualConnection.xSecondNode = X2;

			// add to container
			virtualInterRegionConnections.push_back(virtualConnection);
		}

		// draw (virtual) connections
		QPointF ptc1, ptc2;
		for (int i = 0; i < virtualInterRegionConnections.size(); i++) {
			// get shifted connections
			egtrainPoint2Screen(&virtualInterRegionConnections[i], virtualInterRegionConnections[i].idFirstTrackLine, virtualInterRegionConnections[i].idSecondTrackLine, track_separation);
			ptc1.setX(virtualInterRegionConnections[i].graphXFirstNode);
			ptc1.setY(virtualInterRegionConnections[i].graphYFirstNode);
			ptc2.setX(virtualInterRegionConnections[i].graphXSecondNode);
			ptc2.setY(virtualInterRegionConnections[i].graphYSecondNode);

			paintConnection(ptc1, ptc2, line_width, &virtualInterRegionConnections[i]);
		}
	}
}

// --- Zoom controls ---
void MainWindow::zoomIn() {
	networkView->scale(1.15, 1.15);
}

void MainWindow::zoomOut() {
	networkView->scale(1.0 / 1.15, 1.0 / 1.15);
}

void MainWindow::fitToView() {
	fitView();
}

// --- Per-train diagram slots ---
void MainWindow::showSpeedDistanceDiagram() {
	buildPerTrainDiagram(0);
}
void MainWindow::showSpeedTimeDiagram() {
	buildPerTrainDiagram(1);
}
void MainWindow::showTimeDistanceDiagram() {
	buildPerTrainDiagram(2);
}

// mode 0: speed vs distance, 1: speed vs time, 2: time vs distance
void MainWindow::buildPerTrainDiagram(int mode) {
	// require a completed run
	if (numRegions <= 0 || regional_train[0].trajectorySize() == 0) {
		QMessageBox::information(this, "No Data", "Run a simulation first to view train diagrams.");
		return;
	}

	const char* titles[] = {"Speed vs Distance", "Speed vs Time", "Time vs Distance"};
	QChart* chart = new QChart();
	chart->setTitle(titles[mode]);

	// one series per train; legend distinguishes them
	for (int tr = 0; tr < numRegions; tr++) {
		Train& train = regional_train[tr];
		int n = train.trajectorySize();
		if (n == 0)
			continue;
		QLineSeries* series = new QLineSeries();
		series->setName(QString::fromStdString(train.trainDescription));
		for (int i = 0; i < n; i++) {
			double tSec = trajectoryTimeSeconds(i, timestep);
			double dist = train.positionKmAt(i);
			double spd = train.speedKmhAt(i);
			if (mode == 0)
				series->append(dist, spd); // x: distance (km), y: speed (km/h)
			else if (mode == 1)
				series->append(tSec, spd); // x: time (s),      y: speed (km/h)
			else
				series->append(tSec, dist); // x: time (s),      y: distance (km)
		}
		chart->addSeries(series);
	}
	chart->createDefaultAxes();

	DiagramWindow* win = new DiagramWindow(titles[mode], this);
	win->setChart(chart);
	win->setTimeAxisX(mode == 1 || mode == 2, m_startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

void MainWindow::showTimetableTable() {
	if (numRegions <= 0 || regional_train[0].trajectorySize() == 0) {
		QMessageBox::information(this, "No Data", "Run a simulation first...");
		return;
	}

	QDialog* dlg = new QDialog(this);
	dlg->setWindowTitle("Timetable: planned vs simulated");
	dlg->resize(700, 500);
	dlg->setAttribute(Qt::WA_DeleteOnClose);

	QVBoxLayout* layout = new QVBoxLayout(dlg);
	QTableWidget* table = new QTableWidget();
	table->setColumnCount(5);
	table->setHorizontalHeaderLabels({"Train", "Station", "Planned", "Simulated", "Delay"});

	int row = 0;
	for (int i = 0; i < numRegions; i++) {
		Train& t = regional_train[i];
		if (t.numStations <= 0)
			continue;
		for (int s = 0; s < t.numStations; s++) {
			if (t.ScheduledArrivals[s] <= 0 && t.StationArrivals[s] <= 0)
				continue;

			table->insertRow(row);
			QTableWidgetItem* itemTrain = new QTableWidgetItem(QString::fromStdString(t.trainDescription));
			QTableWidgetItem* itemStation = new QTableWidgetItem(QString::fromStdString(t.stationNameForArrivalStats(s)));

			QString plannedStr = QString::fromStdString(formatSimTime((long long)t.ScheduledArrivals[s], m_startOffsetSeconds));
			QString simStr = QString::fromStdString(formatSimTime((long long)t.StationArrivals[s], m_startOffsetSeconds));

			int delaySeconds = (int)t.StationDelay[s];
			QString delayStr = QString("%1%2 s").arg(delaySeconds >= 0 ? "+" : "").arg(delaySeconds);

			QTableWidgetItem* itemPlanned = new QTableWidgetItem(plannedStr);
			QTableWidgetItem* itemSim = new QTableWidgetItem(simStr);
			QTableWidgetItem* itemDelay = new QTableWidgetItem(delayStr);

			if (delaySeconds > 60) {
				itemDelay->setForeground(QBrush(Qt::red));
			} else if (delaySeconds > 0) {
				itemDelay->setForeground(QBrush(Qt::darkYellow));
			} else {
				itemDelay->setForeground(QBrush(Qt::darkGreen));
			}

			table->setItem(row, 0, itemTrain);
			table->setItem(row, 1, itemStation);
			table->setItem(row, 2, itemPlanned);
			table->setItem(row, 3, itemSim);
			table->setItem(row, 4, itemDelay);
			row++;
		}
	}
	table->resizeColumnsToContents();
	layout->addWidget(table);
	dlg->show();
}

void MainWindow::showDelayDiagram() {
	if (numRegions <= 0 || regional_train[0].trajectorySize() == 0) {
		QMessageBox::information(this, "No Data", "Run a simulation first...");
		return;
	}

	QChart* chart = new QChart();
	chart->setTitle("Delay along journey (minutes)");

	for (int i = 0; i < numRegions; i++) {
		Train& t = regional_train[i];
		if (t.numStations <= 0)
			continue;

		QLineSeries* series = new QLineSeries();
		series->setName(QString::fromStdString(t.trainDescription));
		bool hasData = false;
		for (int s = 0; s < t.numStations; s++) {
			if (t.ScheduledArrivals[s] <= 0 && t.StationArrivals[s] <= 0)
				continue;
			series->append(s, t.StationDelay[s] / 60.0);
			hasData = true;
		}
		if (hasData) {
			chart->addSeries(series);
		} else {
			delete series;
		}
	}
	chart->createDefaultAxes();
	if (!chart->axes(Qt::Horizontal).isEmpty()) {
		chart->axes(Qt::Horizontal).first()->setTitleText("Station index");
	}
	if (!chart->axes(Qt::Vertical).isEmpty()) {
		chart->axes(Qt::Vertical).first()->setTitleText("Delay (min)");
	}

	DiagramWindow* win = new DiagramWindow("Delay along journey (minutes)", this);
	win->setChart(chart);
	win->setTimeAxisX(false, m_startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

void MainWindow::showTimetableGraph() {
	if (numRegions <= 0 || regional_train[0].trajectorySize() == 0) {
		QMessageBox::information(this, "No Data", "Run a simulation first...");
		return;
	}

	QChart* chart = new QChart();
	chart->setTitle("Train graph: planned (dashed) vs simulated (solid)");

	for (int i = 0; i < numRegions; i++) {
		Train& t = regional_train[i];
		if (t.numStations <= 0)
			continue;

		QLineSeries* simSeries = new QLineSeries();
		simSeries->setName(QString::fromStdString(t.trainDescription));
		QLineSeries* planSeries = new QLineSeries();
		planSeries->setName(QString::fromStdString(t.trainDescription) + " (planned)");

		bool hasData = false;
		for (int s = 0; s < t.numStations; s++) {
			if (t.ScheduledArrivals[s] <= 0 && t.StationArrivals[s] <= 0)
				continue;

			simSeries->append(t.StationArrivals[s], s);
			planSeries->append(t.ScheduledArrivals[s], s);
			hasData = true;
		}
		if (hasData) {
			chart->addSeries(simSeries);
			chart->addSeries(planSeries);

			QPen planPen = planSeries->pen();
			planPen.setStyle(Qt::DashLine);
			planPen.setColor(simSeries->pen().color());
			planSeries->setPen(planPen);
		} else {
			delete simSeries;
			delete planSeries;
		}
	}
	chart->createDefaultAxes();

	DiagramWindow* win = new DiagramWindow("Train graph: planned (dashed) vs simulated (solid)", this);
	win->setChart(chart);
	win->setTimeAxisX(true, m_startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

void MainWindow::showBlockingTimeDiagram() {
	if (numRegions <= 0 || regional_train[0].trajectorySize() == 0) {
		QMessageBox::information(this, "No Data", "Run a simulation first...");
		return;
	}

	QChart* chart = new QChart();
	chart->setTitle("Blocking-time overlay");

	std::vector<std::vector<BlockingTimeDiagramInput>> trains;
	std::vector<std::string> trainNames;
	trains.reserve(numRegions);
	trainNames.reserve(numRegions);
	for (int i = 0; i < numRegions; i++) {
		Train& t = regional_train[i];
		std::vector<BlockingTimeDiagramInput> blocks;
		blocks.reserve(t.N_BlockTimeComplete);
		for (int j = 0; j < t.N_BlockTimeComplete; j++) {
			BlockingTimeDiagramInput block;
			block.blockId = t.BlockTime[j].BlockID;
			block.startOccTime = t.BlockTime[j].StartOccTime;
			block.endOccTime = t.BlockTime[j].EndOccTime;
			block.posStart = t.BlockTime[j].PosStart;
			block.posEnd = t.BlockTime[j].PosEnd;
			block.switchName = t.BlockTime[j].SwitchName;
			block.stationName = t.BlockTime[j].stationName;
			block.isComplete = t.BlockTime[j].IsComplete;
			blocks.push_back(block);
		}
		trains.push_back(blocks);
		trainNames.push_back(t.trainDescription);
	}

	std::vector<BlockingTimeDiagramSegment> segments = buildBlockingTimeDiagramSegments(trains, trainNames);
	if (segments.empty()) {
		delete chart;
		QMessageBox::information(this, "No Data", "No complete blocking-time data is available for this simulation.");
		return;
	}

	QColor defaultColor(100, 160, 240, 150);
	QColor stationColor(60, 110, 190, 180);
	QColor switchColor(240, 210, 40, 180);
	QColor switchStationColor(200, 160, 20, 190);
	QColor criticalColor(220, 50, 50, 200);
	QColor criticalStationColor(170, 30, 30, 220);

	auto colorForStyle = [&](BlockingTimeSegmentStyle style) {
		switch (style) {
			case BlockingTimeSegmentStyle::Station:
				return stationColor;
			case BlockingTimeSegmentStyle::Switch:
				return switchColor;
			case BlockingTimeSegmentStyle::SwitchStation:
				return switchStationColor;
			case BlockingTimeSegmentStyle::Critical:
				return criticalColor;
			case BlockingTimeSegmentStyle::CriticalStation:
				return criticalStationColor;
			case BlockingTimeSegmentStyle::Default:
			default:
				return defaultColor;
		}
	};
	auto suffixForStyle = [](BlockingTimeSegmentStyle style) {
		switch (style) {
			case BlockingTimeSegmentStyle::Station:
				return " (station)";
			case BlockingTimeSegmentStyle::Switch:
				return " (switch)";
			case BlockingTimeSegmentStyle::SwitchStation:
				return " (switch/station)";
			case BlockingTimeSegmentStyle::Critical:
				return " (critical)";
			case BlockingTimeSegmentStyle::CriticalStation:
				return " (critical/station)";
			case BlockingTimeSegmentStyle::Default:
			default:
				return "";
		}
	};

	std::map<std::string, bool> legendEntries;
	for (const BlockingTimeDiagramSegment& segment : segments) {
		QLineSeries* series = new QLineSeries();
		std::string legendKey = segment.trainName + suffixForStyle(segment.style);
		series->setName(QString::fromStdString(legendKey));
		bool firstLegendEntry = legendEntries.find(legendKey) == legendEntries.end();

		QPen pen(colorForStyle(segment.style));
		pen.setWidthF(segment.penWidth);
		series->setPen(pen);
		series->append(segment.startTime, segment.midPositionKm);
		series->append(segment.endTime, segment.midPositionKm);
		chart->addSeries(series);
		if (firstLegendEntry) {
			legendEntries[legendKey] = true;
		} else {
			for (QLegendMarker* marker : chart->legend()->markers(series))
				marker->setVisible(false);
		}
	}

	QLineSeries* dummySwitch = new QLineSeries();
	dummySwitch->setName("Key: Switch");
	QPen dummySwitchPen(switchColor);
	dummySwitchPen.setWidthF(4.0);
	dummySwitch->setPen(dummySwitchPen);
	chart->addSeries(dummySwitch);

	QLineSeries* dummyCritical = new QLineSeries();
	dummyCritical->setName("Key: Critical");
	QPen dummyCriticalPen(criticalColor);
	dummyCriticalPen.setWidthF(4.0);
	dummyCritical->setPen(dummyCriticalPen);
	chart->addSeries(dummyCritical);

	chart->createDefaultAxes();
	if (!chart->axes(Qt::Horizontal).isEmpty()) {
		chart->axes(Qt::Horizontal).first()->setTitleText("Time");
	}
	if (!chart->axes(Qt::Vertical).isEmpty()) {
		chart->axes(Qt::Vertical).first()->setTitleText("Position (km)");
	}

	DiagramWindow* win = new DiagramWindow("Blocking-time overlay", this);
	win->setChart(chart);
	win->setTimeAxisX(true, m_startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

void MainWindow::buildSignalIndex() {
	m_signalsByAheadId.clear();
	for (auto* signal : allSignals) {
		if (signal->sectionAhead) {
			m_signalsByAheadId[signal->sectionAhead->ID].push_back(signal);
		}
	}
}
