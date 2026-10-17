#include "app/MainWindow.h"
#include "app/CannotOpenMessage.h"
#include "app/DialogFolders.h"
#include "app/DispatchController.h"
#include "app/FollowAvailability.h"
#include "app/GuiReplayHistory.h"
#include "app/GuiSimulationSnapshot.h"
#include "graphics/SignalItemRoles.h"
#include "app/MainWindowSupport.h"
#include "app/MainWindowTestHooks.h"
#include "app/PathMessages.h"
#include "app/RunSelection.h"
#include "app/OpenRequestRelay.h"
#include "app/ReleaseNotesText.h"
#include "app/ReplayCoverage.h"
#include "app/RuntimeGeometryFromRun.h"
#include "app/SceneOpenRequest.h"
#include "app/SceneSavePolicy.h"
#include "app/UpdateTelemetryController.h"
#include "ui_MainWindow.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QAccessible>
#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFont>
#include <QGraphicsEffect>
#include <QGraphicsEllipseItem>
#include <QGraphicsItemGroup>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QImage>
#include <QIntValidator>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMovie>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextOption>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QTransform>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <QtCharts/QChartView>
#include <qmath.h>
#include <iostream>
#include <list>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>
QT_CHARTS_USE_NAMESPACE
#include "util/TimeFormat.h"
#include "util/SpeedFormat.h"
#include "widgets/ConsoleWidget.h"
#include "widgets/ChoiceComboBox.h"
#include "widgets/ColorChoiceButton.h"
#include "widgets/DialogLayout.h"
#include "widgets/EditorLayout.h"
#include "widgets/UserPrompts.h"
#include "widgets/AboutDialog.h"
#include "widgets/CaseChooserDialog.h"
#include "widgets/ElidedLabel.h"
#include "widgets/FocusWheel.h"
#include "widgets/InfoDockWidget.h"
#include "widgets/NetworkLegendWidget.h"
#include "widgets/TimeProgressBar.h"
#include "graphics/FollowCamera.h"
#include "graphics/NetworkPresenter.h"
#include "graphics/NetworkScene.h"
#include "graphics/NetworkView.h"
#include "graphics/items/ConnectionItem.h"
#include "graphics/items/HighlightEffect.h"
#include "graphics/items/IconItem.h"
#include "graphics/items/NodeItem.h"
#include "graphics/items/PassengerItem.h"
#include "graphics/items/PlatformItem.h"
#include "graphics/items/SignalItem.h"
#include "graphics/items/StationNodeItem.h"
#include "graphics/items/StationOverlayItem.h"
#include "graphics/items/TrackLineItem.h"
#include "graphics/items/TrainBadgeItem.h"
#include "graphics/items/TrainBodyItem.h"
#include "graphics/items/TrainItemGroup.h"
#include "graphics/items/VirtualArcItem.h"
#include "simulation/Infrastructure.h"
#include "simulation/Signalling.h"
#include "simulation/SimulationWorker.h"
#include <QScrollBar>
#include "diagrams/DiagramWindow.h"
#include "simulation/SimulationRun.h"
#include "diagrams/TrainFilterButton.h"
#include "diagrams/RouteDiagramCoordinates.h"
#include "diagrams/RouteReferenceChoice.h"
#include "diagrams/RunExports.h"
#include "diagrams/RunResults.h"
#include "diagrams/TimetableTableWindow.h"
#include "diagrams/TrainColors.h"
#include "util/TrajectoryUtil.h"
#include "util/CsvWriter.h"
#include "util/PlaybackProfiler.h"
#include "diagrams/AreaCapacityWindow.h"
#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/BlockingTimeScope.h"
#include "diagrams/CapacityAnalysis.h"
#include "diagrams/CapacityScope.h"
#include "diagrams/TractionCurve.h"
#include "editors/CaseSettingsPanel.h"
#include "editors/CompositionPanel.h"
#include "editors/EntranceDelayPanel.h"
#include "editors/IncidentPanel.h"
#include "editors/InfrastructurePanel.h"
#include "editors/PassengerPanel.h"
#include "editors/ScenarioPanel.h"
#include "editors/SceneDocument.h"
#include "editors/SceneEditorPanel.h"
#include "editors/ServicePanel.h"
#include "editors/TimetableStopsPanel.h"
#include "editors/TrainUnitPanel.h"
#include "editors/TrainUnitSourceLinker.h"
#include "editors/ValidationPanel.h"
#include "editors/infrastructure/BlockRouteFacets.h"
#include "editors/infrastructure/SignalFacets.h"
#include "editors/infrastructure/StationFacets.h"
#include "editors/infrastructure/TopologyFacets.h"
#include "graphics/SignalGeometry.h"
#include "graphics/AnnotationPlacement.h"
#include "graphics/VisualPolish.h"
#include "recovery/RecoveryController.h"
#include "recovery/RecoveryPrompt.h"
#include "scene/SceneBundle.h"
#include "scene/SceneCompatibility.h"
#include "scene/SceneMigration.h"
#include "scene/SceneWriter.h"
#include "scene/SceneExporter.h"
#include "scene/SceneHistory.h"
#include "scene/SceneImporter.h"
#include "scene/SceneReferences.h"
#include "scene/SceneValidator.h"
#include "scene/StopInsertion.h"
#include "scene/TrackPreview.h"
#include "simulation/Passengers.h"
#include "update/UpdateNotices.h"
#include <QtCharts/QLineSeries>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QCategoryAxis>
#include <QtCharts/QLegendMarker>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <cfloat>
#include <QThread>
#include <QCloseEvent>
#include <QGraphicsSceneHoverEvent>
#include <QMouseEvent>
#include <QDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QShortcut>
#include <QScopedValueRollback>
#include <QSettings>
#include <QIcon>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTemporaryDir>
#include <QToolButton>
#include <QStyleOptionSlider>
#include <QUrl>
#include <QTreeWidgetItemIterator>
#include <QStringList>
#include <QTabWidget>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <functional>
#include <initializer_list>
#include <memory>


namespace {
class ReplaySlider : public QSlider {
public:
	explicit ReplaySlider(QWidget* parent) : QSlider(Qt::Horizontal, parent) {}
protected:
	void mousePressEvent(QMouseEvent* event) override {
		if (event->button() == Qt::LeftButton) {
			QStyleOptionSlider option;
			initStyleOption(&option);
			const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option,
				QStyle::SC_SliderGroove, this);
			const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option,
				QStyle::SC_SliderHandle, this);
			if (!handle.contains(event->pos())) {
				const int span = groove.width() - handle.width();
				const int position = event->pos().x() - groove.x() - handle.width() / 2;
				setValue(QStyle::sliderValueFromPosition(minimum(), maximum(), position, span,
					invertedAppearance()));
				event->accept();
				return;
			}
		}
		QSlider::mousePressEvent(event);
	}
};

using StartupClock = std::chrono::steady_clock;
StartupClock::time_point g_startupTimingOrigin;
quint64 g_startupTimingSequence = 0;
struct StartupIdentity {
	QString path;
	int schemaVersion = 0;
	std::string name;
	std::string baseTime;
	std::string defaultScenarioId;
	SceneSimulationSettings settings;
	std::array<std::size_t, 22> collectionSizes{};
	std::string inputSnapshot;
	bool set = false;
};
StartupIdentity g_startupIdentity;

#ifdef signals
#define EGTRAIN_RESTORE_STARTUP_SIGNALS
#undef signals
#endif
StartupIdentity startupIdentity(const QString& path, const SceneModel& model,
	const std::string& inputSnapshot = {}) {
	return {QFileInfo(path).absoluteFilePath(), model.schemaVersion, model.name, model.baseTime,
		model.defaultScenarioId, model.settings,
		{model.loadedData.size(), model.sourceFiles.size(), model.importReport.size(),
			model.tracks.size(), model.trackViews.size(), model.nodes.size(), model.arcs.size(),
			model.blocks.size(), model.connections.size(), model.stations.size(),
			model.stationViews.size(), model.signals.size(), model.signallingAreas.size(),
			model.routes.size(), model.blockDependencies.size(), model.singleTrackRestrictions.size(),
			model.stationBoundaries.size(), model.trainUnits.size(), model.compositions.size(),
			model.services.size(), model.scenarios.size(), model.passengers.size()},
		inputSnapshot, true};
}
#ifdef EGTRAIN_RESTORE_STARTUP_SIGNALS
#define signals Q_SIGNALS
#undef EGTRAIN_RESTORE_STARTUP_SIGNALS
#endif

bool sameStartupSettings(const SceneSimulationSettings& left,
	const SceneSimulationSettings& right) {
	return left.hasDuration == right.hasDuration && left.durationSeconds == right.durationSeconds
		&& left.hasBufferTime == right.hasBufferTime
		&& left.bufferTimeSeconds == right.bufferTimeSeconds
		&& left.hasRecoveryTime == right.hasRecoveryTime
		&& left.recoveryTimePercent == right.recoveryTimePercent;
}

bool sameStartupIdentity(const StartupIdentity& left, const StartupIdentity& right) {
	return left.set && right.set && left.path == right.path
		&& left.schemaVersion == right.schemaVersion && left.name == right.name
		&& left.baseTime == right.baseTime && left.defaultScenarioId == right.defaultScenarioId
		&& sameStartupSettings(left.settings, right.settings)
		&& left.collectionSizes == right.collectionSizes;
}

constexpr int kOverlayMargin = 12;

bool inputTractionHasNegativeEffort(const SceneTrainUnit& unit) {
	return std::any_of(unit.tractionCurve.begin(), unit.tractionCurve.end(), [](const auto& row) {
		if (row[1] < row[0])
			return false;
		const auto effort = [&row](double speed) { return row[2] + row[3] * speed + row[4] * speed * speed; };
		const double minimumAt = row[4] > 0.0 ? std::clamp(-row[3] / (2.0 * row[4]), row[0], row[1]) : row[0];
		return effort(minimumAt) < 0.0 || effort(row[1]) < 0.0;
	});
}

QString inputTractionContext(const SceneTrainUnit& unit, const QString& displayId = QString()) {
	QString context = QString("Rolling stock unit: %1")
						  .arg(displayId.isNull() ? QString::fromStdString(unit.id) : displayId);
	if (!unit.sourceTractionFile.empty())
		context += QString(" | Source: %1").arg(QString::fromStdString(unit.sourceTractionFile));
	return context;
}

QString inputTractionWarning(const SceneTrainUnit& unit) {
	return inputTractionHasNegativeEffort(unit)
		? QStringLiteral("Negative effort below the displayed 0 kN range.")
		: QString();
}

QChart* buildInputTractionChart(const SceneTrainUnit& unit) {
	const auto samples = sampleTractionCurve(unit.tractionCurve);
	QChart* chart = new QChart();
	chart->setTitle("Input traction characteristic");
	QLineSeries* series = new QLineSeries();
	series->setName(QString::fromStdString(unit.id));
	series->setProperty("trainId", QString::fromStdString(unit.id));
	for (const auto& point : samples)
		series->append(point.first * 3.6, point.second / 1000.0);
	series->setPointsVisible(samples.size() == 1);
	chart->addSeries(series);
	chart->createDefaultAxes();
	if (!chart->axes(Qt::Horizontal).isEmpty())
		chart->axes(Qt::Horizontal).first()->setTitleText("Speed (km/h)");
	if (auto* effortAxis = qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).value(0))) {
		effortAxis->setTitleText("Tractive effort (kN)");
		effortAxis->setRange(0.0, std::max(1.0, effortAxis->max()));
	}
	if (inputTractionHasNegativeEffort(unit))
		chart->setTitle(chart->title() + "<br>Curve contains negative effort below the default 0 kN view");
	return chart;
}

void attachRunProvenance(DiagramWindow* window, RunProvenance provenance) {
	window->setProvenanceWriter([provenance = std::move(provenance)](
									const QString& path, const char* kind, const std::string& bytes) {
		return writeRunArtifactWithProvenance(path.toStdString(), kind, bytes, provenance);
	});
}

const QColor kBlockingSwitchColor(240, 210, 40, 180);
const QColor kBlockingCriticalColor(220, 50, 50, 200);
const QColor kBlockingCapacityColor(235, 175, 20, 230);

void appendInspectedPoint(QLineSeries* series, double x, double y, const QString& context) {
	series->append(x, y);
	QStringList points = series->property("inspectionPoints").toStringList();
	points.append(context);
	series->setProperty("inspectionPoints", points);
}

void addBlockingTimeSeries(QChart* chart, const std::vector<BlockingTimeDiagramSegment>& segments,
	bool useCapacityCriticalStyle) {
	const auto colorFor = [useCapacityCriticalStyle](const BlockingTimeDiagramSegment& segment) {
		if (useCapacityCriticalStyle && segment.capacityCritical)
			return kBlockingCapacityColor;
		switch (segment.style) {
			case BlockingTimeSegmentStyle::Station: return QColor(60, 110, 190, 180);
			case BlockingTimeSegmentStyle::Switch: return kBlockingSwitchColor;
			case BlockingTimeSegmentStyle::SwitchStation: return QColor(200, 160, 20, 190);
			case BlockingTimeSegmentStyle::Critical: return kBlockingCriticalColor;
			case BlockingTimeSegmentStyle::CriticalStation: return QColor(170, 30, 30, 220);
			case BlockingTimeSegmentStyle::Default:
			default: return QColor(100, 160, 240, 150);
		}
	};
	const auto suffixFor = [useCapacityCriticalStyle](const BlockingTimeDiagramSegment& segment) {
		if (useCapacityCriticalStyle && segment.capacityCritical)
			return std::string(" (capacity critical block)");
		if (segment.style == BlockingTimeSegmentStyle::Default)
			return std::string();
		return " (" + std::string(blockingSegmentTypeName(segment.style)) + ")";
	};
	std::map<std::string, bool> legendEntries;
	for (const BlockingTimeDiagramSegment& segment : segments) {
		const double left = std::min(segment.startPositionKm, segment.endPositionKm);
		const double right = std::max(segment.startPositionKm, segment.endPositionKm);
		const std::string legendKey = segment.trainName + suffixFor(segment);
		const QString name = QString::fromStdString(legendKey);
		const QString trainId = QString::fromStdString(segment.trainName);
		const QColor color = colorFor(segment);
		if (left != right) {
			auto* lower = new QLineSeries();
			auto* upper = new QLineSeries();
			lower->append(left, segment.startTime);
			lower->append(right, segment.startTime);
			upper->append(left, segment.endTime);
			upper->append(right, segment.endTime);
			auto* fill = new QAreaSeries(upper, lower);
			fill->setName(name);
			fill->setProperty("trainId", trainId);
			QColor translucent = color;
			translucent.setAlpha(65);
			fill->setBrush(translucent);
			fill->setPen(Qt::NoPen);
			chart->addSeries(fill);
			for (QLegendMarker* marker : chart->legend()->markers(fill)) marker->setVisible(false);
		}
		auto* outline = new QLineSeries();
		outline->setName(name);
		outline->setProperty("trainId", trainId);
		outline->setProperty("layer", "calculated blocking envelope");
		outline->setProperty("inspectionFilled", left != right);
		QPen pen(color.darker(135));
		pen.setWidthF(2.0);
		outline->setPen(pen);
		const QString referenceSuffix = segment.referenceNote.empty() ? QString() : " | " + QString::fromStdString(segment.referenceNote);
		outline->setProperty("inspectionInterval",
			QString("Calculated blocking envelope | Resource: %1 | Type: %2 | "
					"Directed X: %3 to %4 km | Display: %5 to %6 s | Original: %7 to %8 s | "
					"Original approach: %9 | Original run start: %10 | Original run end: %11 | Original clearance: %12")
					.arg(QString::fromStdString(segment.blockId), blockingSegmentTypeName(segment))
					.arg(segment.startPositionKm)
					.arg(segment.endPositionKm)
					.arg(segment.startTime)
					.arg(segment.endTime)
					.arg(segment.originalStartTime)
					.arg(segment.originalEndTime)
					.arg(segment.startApproachTime >= 0 ? QString::number(segment.startApproachTime) : "unavailable")
					.arg(segment.startRunTime >= 0 ? QString::number(segment.startRunTime) : "unavailable")
					.arg(segment.endRunTime >= 0 ? QString::number(segment.endRunTime) : "unavailable")
					.arg(segment.endClearTime >= 0 ? QString::number(segment.endClearTime) : "unavailable")
				+ QString(" | Setup: %1 | Sight reaction: %2 | Release: %3 | Run margin: %4")
					.arg(segment.setupTime >= 0 ? QString::number(segment.setupTime) : "unavailable")
					.arg(segment.sightReactionTime >= 0 ? QString::number(segment.sightReactionTime) : "unavailable")
					.arg(segment.releaseTime >= 0 ? QString::number(segment.releaseTime) : "unavailable")
					.arg(segment.runTimeMargin >= 0 ? QString::number(segment.runTimeMargin) : "unavailable")
				+ referenceSuffix);
		outline->append(left, segment.startTime);
		outline->append(right, segment.startTime);
		outline->append(right, segment.endTime);
		outline->append(left, segment.endTime);
		if (left != right) outline->append(left, segment.startTime);
		chart->addSeries(outline);
		if (!legendEntries.emplace(legendKey, true).second)
			for (QLegendMarker* marker : chart->legend()->markers(outline)) marker->setVisible(false);
	}
}

// Real identities retained in the immutable export, including trains with no projectable series.
QStringList blockingTimeTrainIds(const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::vector<BlockingTimePlannedReference>& references,
	const std::vector<std::vector<std::string>>& trajectories,
	const std::vector<BlockingTimeDiagramSegment>& unmapped) {
	QStringList ids;
	const auto add = [&ids](const std::string& name) {
		const QString id = QString::fromStdString(name);
		if (!id.isEmpty() && !ids.contains(id)) ids.append(id);
	};
	for (const auto& segment : segments) add(segment.trainName);
	for (const auto& reference : references) add(reference.trainName);
	for (const auto& row : trajectories)
		if (!row.empty()) add(row.front());
	for (const auto& segment : unmapped) add(segment.trainName);
	return ids;
}

// Prompt for a path and write CSV text atomically. A cancelled dialog or a write
// failure never leaves a partial file behind.
void saveCsvInteractive(QWidget* parent, const QString& suggestedName,
	const telemetry::OperationObservation& operation, const std::function<std::string()>& provider,
	const std::function<bool(const QString&, const std::string&)>& artifactWriter) {
	const std::string content = provider();
	if (content.empty()) {
		QMessageBox::information(parent, "Nothing to export", "There is no data to export.");
		return;
	}
	QString path = QFileDialog::getSaveFileName(parent, "Export Data", dialog_folders::suggestedPath(suggestedName), "CSV File (*.csv)");
	if (path.isEmpty())
		return;
	if (QFileInfo(path).suffix().compare("csv", Qt::CaseInsensitive) != 0)
		path += ".csv";
	const bool written = artifactWriter(path, content);
	operation.exportFinished(telemetry::ExportKind::Csv, written, true, telemetry::Error::IoFailure);
	if (!written) {
		QMessageBox::warning(parent, "Export failed", path_message::writeFailed("export the data and provenance to", path));
	}
}

bool e2eDialogsSuppressed() {
	return startupTimingEnabled()
		|| PlaybackProfiler::enabled()
		|| mainWindowTestHooks().dialogsSuppressed;
}

// The folder for recovery copies, or an empty string when this run keeps none. A run that has no user to answer a
// question keeps copies only in the folder that QEGTRAIN_RECOVERY_DIR names, so that it never meets the copies of a user.
QString recoveryRoot() {
	const QString named = qEnvironmentVariable("QEGTRAIN_RECOVERY_DIR");
	if (!named.isEmpty())
		return QDir(named).absolutePath();
	const QString platform = QGuiApplication::platformName();
	if (e2eDialogsSuppressed() || platform == QLatin1String("offscreen") || platform == QLatin1String("minimal"))
		return {};
	const QString data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
	return data.isEmpty() ? QString() : QDir(data).filePath(QStringLiteral("Recovery"));
}

// The scene at path as the source of a recovery copy: a bundle file, or a folder that is judged by its scene.json.
RecoverySource recoverySourceOf(const QString& path, bool isBundle) {
	RecoverySource source;
	source.kind = isBundle ? RecoverySourceKind::Bundle : RecoverySourceKind::Directory;
	source.path = QFileInfo(path).absoluteFilePath();
	const QFileInfo file(isBundle ? source.path : QDir(source.path).filePath(QStringLiteral("scene.json")));
	if (file.isFile()) {
		source.sizeBytes = file.size();
		source.modified = file.lastModified().toUTC();
	}
	return source;
}

void showBlockingError(QWidget* parent, const QString& title, const QString& message, bool warningIcon = false) {
	UserPrompts(e2eDialogsSuppressed).showError(parent, title, message, warningIcon);
}

double computeHorizon(const SceneModel& scene) {
	double maxSeconds = 0.0;
	for (const auto& service : scene.services) {
		if (service.entryTimeSeconds > maxSeconds)
			maxSeconds = service.entryTimeSeconds;
		for (const auto& stop : service.stops) {
			if (stop.hasPlannedArrival && stop.plannedArrivalSeconds > maxSeconds)
				maxSeconds = stop.plannedArrivalSeconds;
			if (stop.hasPlannedDeparture && stop.plannedDepartureSeconds > maxSeconds)
				maxSeconds = stop.plannedDepartureSeconds;
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

} // namespace

bool startupTimingEnabled() {
	return qEnvironmentVariable("QEGTRAIN_STARTUP_TIMING") == QLatin1String("1");
}

void beginStartupTiming() {
	if (startupTimingEnabled()) {
		g_startupTimingOrigin = StartupClock::now();
		g_startupTimingSequence = 0;
		g_startupIdentity = StartupIdentity();
	}
}

qint64 startupTimingNowNanoseconds() {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		StartupClock::now() - g_startupTimingOrigin)
		.count();
}

void recordStartupTiming(const QString& phase, int iteration, int generation,
	qint64 elapsedNanoseconds, const QString& invocation, const QString& source,
	bool identityOk, bool canonicalPreloadNested) {
	if (!startupTimingEnabled())
		return;
	QJsonObject record{{QStringLiteral("event"), QStringLiteral("complete")},
		{QStringLiteral("sequence"), static_cast<qint64>(g_startupTimingSequence++)},
		{QStringLiteral("phase"), phase}, {QStringLiteral("iteration"), iteration},
		{QStringLiteral("temperature"), iteration == 0 ? QStringLiteral("fresh_process") : QStringLiteral("warm_in_process")},
		{QStringLiteral("elapsed_ms"), elapsedNanoseconds / 1000000.0},
		{QStringLiteral("identity_ok"), identityOk}};
	if (generation >= 0)
		record.insert(QStringLiteral("generation"), generation);
	if (!invocation.isEmpty())
		record.insert(QStringLiteral("invocation"), invocation);
	if (!source.isEmpty())
		record.insert(QStringLiteral("source"), source);
	if (canonicalPreloadNested)
		record.insert(QStringLiteral("canonical_preload_nested"), true);
	if (phase.endsWith(QLatin1String("paint")))
		record.insert(QStringLiteral("paint_scope"), QStringLiteral("completed_qt_viewport_paint_handling"));
	const QByteArray json = QJsonDocument(record).toJson(QJsonDocument::Compact);
	std::fprintf(stdout, "QEGTRAIN_TIMING %s\n", json.constData());
	std::fflush(stdout);
}

void setStartupTimingPreloadIdentity(const QString& path, const SceneLoadResult& loaded) {
	if (startupTimingEnabled())
		g_startupIdentity = startupIdentity(path, loaded.scene, loaded.inputSnapshot);
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent),
										  ui(std::make_unique<Ui::MainWindow>()) {
	QFile themeFile(":/styles/egtrain.qss");
	if (themeFile.open(QIODevice::ReadOnly | QIODevice::Text))
		qApp->setStyleSheet(QString::fromUtf8(themeFile.readAll()));

	ui->setupUi(this);
	m_userPrompts = std::make_unique<UserPrompts>(e2eDialogsSuppressed);
	m_history = std::make_unique<SceneHistory>();
	m_document = new SceneDocument(this);
	m_session = new SceneSession(this, m_document, *this);
	m_runController = new RunController(this, m_document, m_session, m_runSelection, *this);
	startRecovery();
	connect(m_document, &SceneDocument::changed, this, &MainWindow::sceneDocumentChanged);
	setupEditMenu();
	m_updateTelemetry = std::make_unique<UpdateTelemetryController>(
		this, [this] { return m_session->maybeSaveScene(); },
		[this] {
			if (m_recovery)
				m_recovery->shutdown();
		});
	m_updateTelemetry->setupUpdateActions(ui->menuHelp);
	if (ui->menuHelp) {
		auto* privacy = ui->menuHelp->addAction(QStringLiteral("Privacy && diagnostics..."));
		privacy->setObjectName(QStringLiteral("actionPrivacyDiagnostics"));
		privacy->setShortcut(QKeySequence::Preferences);
		connect(privacy, &QAction::triggered, this, [this] { m_updateTelemetry->showPrivacySettings(false); });
	}

	EG_LOG(AppRun, Info) << "\n...PREPARING GUI...\n\n";

	// define central widget
	centralWidget = new QWidget();

	// set network view
	networkView = new NetworkView(centralWidget);
	networkView->setObjectName("networkView");
	m_followCamera = new FollowCamera(networkView, networkView);
	// QGraphicsView accepts drops by default. Disable that path so drops over
	// the viewport route to MainWindow without changing its pan interaction.
	networkView->setAcceptDrops(false);
	networkView->viewport()->setAcceptDrops(false);
	setAcceptDrops(true);

	// set progress bar
	progressBar = new TimeProgressBar(centralWidget);

	// set main layout
	mainLayout = new QVBoxLayout();
	mainLayout->addWidget(networkView);
	mainLayout->addWidget(progressBar);
	m_replayBar = new QWidget(centralWidget);
	m_replayBar->setObjectName("completedReplayBar");
	auto* replayLayout = new QVBoxLayout(m_replayBar);
	replayLayout->setContentsMargins(4, 2, 4, 2);
	auto* replayControls = new QHBoxLayout();
	m_replayStartButton = new QPushButton("Start", m_replayBar);
	auto* replayEnd = new QPushButton("End", m_replayBar);
	m_replayPlayButton = new QPushButton("Play", m_replayBar);
	m_replaySlider = new ReplaySlider(m_replayBar);
	m_replaySlider->setObjectName("completedReplaySlider");
	m_replaySlider->setAccessibleName("Replay time");
	m_replaySlider->setFocusPolicy(Qt::StrongFocus);
	m_replaySlider->setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("000000000000")));
	// The selected time sits beside the slider on one line. It keeps the width of its longest text, so the
	// slider does not change its width while the user seeks.
	m_replayLabel = new QLabel(m_replayBar);
	m_replayLabel->setMinimumWidth(fontMetrics().horizontalAdvance(QStringLiteral("Replay at 000000 s")));
	// The coverage has a row of its own and is the same for every selected time.
	m_replayCoverageLabel = new QLabel(m_replayBar);
	m_replayCoverageLabel->setWordWrap(true);
	m_replayCoverageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	replayControls->addWidget(m_replayStartButton);
	replayControls->addWidget(m_replayPlayButton);
	replayControls->addWidget(replayEnd);
	replayControls->addWidget(m_replaySlider, 1);
	replayControls->addWidget(m_replayLabel);
	replayLayout->addLayout(replayControls);
	replayLayout->addWidget(m_replayCoverageLabel);
	mainLayout->addWidget(m_replayBar);
	m_replayBar->hide();
	m_replayTimer = new QTimer(this);
	m_replayTimer->setInterval(1000); // One simulated second per wall second.
	connect(m_replaySlider, &QSlider::valueChanged, this, &MainWindow::seekReplay);
	connect(m_replayStartButton, &QPushButton::clicked, this, [this]() { seekReplay(m_completedReplay.firstTime()); });
	connect(replayEnd, &QPushButton::clicked, this, [this]() { seekReplay(m_completedReplay.lastTime()); });
	connect(m_replayPlayButton, &QPushButton::clicked, this, [this]() {
		if (m_replayTimer->isActive()) m_replayTimer->stop();
		else if (!m_completedReplay.empty()) {
			if (m_replayRequestedTime >= m_completedReplay.lastTime()) seekReplay(m_completedReplay.firstTime());
			m_replayTimer->start();
		}
		m_replayPlayButton->setText(m_replayTimer->isActive() ? "Pause" : "Play");
	});
	connect(m_replayTimer, &QTimer::timeout, this, [this]() {
		seekReplay(m_replayRequestedTime + 1);
		if (m_replayRequestedTime >= m_completedReplay.lastTime()) {
			m_replayTimer->stop();
			m_replayPlayButton->setText("Play");
		}
	});

	// set layout in QWidget
	centralWidget->setLayout(mainLayout);

	// set as central layout of the main window
	setCentralWidget(centralWidget);

	// hide progress bar
	progressBar->hide();
	statusBar()->showMessage("Simulation complete");

	// setup speed slider (will be added to toolbar)
	m_speedSlider = new QSlider(Qt::Horizontal);
	m_speedSlider->setRange(0, kMaxStepDelayMs);
	m_speedSlider->setValue(kMaxStepDelayMs);
	m_speedSlider->setMaximumWidth(108);
	m_speedSlider->setToolTip("Simulation speed: fastest");
	m_speedSlider->setObjectName("speedSlider");
	m_speedLabel = new QLabel(simulationSpeedLabel(stepDelayForSlider(m_speedSlider->value())), this);
	m_speedLabel->setObjectName("speedModeLabel");
	m_speedLabel->hide();

	// create container of items to show on display area
	scene = new NetworkScene(networkView);
	m_presenter = std::make_unique<NetworkPresenter>(scene.data(), networkView);
	m_presenter->setGeometrySource(makeRuntimeGeometryFromRun(simulation));
	m_presenter->setFrameHost(this);
	networkView->setScene(scene);

	// setup info dock widget
	m_inspector = new InspectorPanel(this, scene.data(), *m_presenter, *this);
	setupRunResultsDock();

	connect(networkView, &NetworkView::viewportChanged, this, [this]() {
		// The camera changes the position of the view and never its scale, so the overlays and the zoom
		// label stay as they are. The station of the followed train is refreshed with the frame.
		if (m_followCamera->movingView())
			return;
		updateViewportOverlays();
		updateZoomStatus();
	});
	connect(networkView, &NetworkView::interactionFinished,
		this, &MainWindow::updateViewportOverlays);
	connect(networkView, &NetworkView::timingPaintCompleted,
		this, &MainWindow::handleStartupTimingPaint);
	connect(networkView, &NetworkView::playbackProfileBegan, this, [this]() {
		QTimer::singleShot(PlaybackProfiler::instance().durationMs(), this, [this]() {
			const int currentTimestep = m_snapshot ? m_snapshot->timestep : -1;
			if (PlaybackProfiler::instance().freeze(currentTimestep))
				m_runController->requestStop();
		});
	});
	networkView->setMouseTracking(true);

	// connect elements from UI
	connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::handleHelpAbout);
	connect(ui->actionSimulationStart, &QAction::triggered, m_runController, &RunController::runCurrent);
	connect(ui->actionSimulationStop, &QAction::triggered, this, [this]() {
		if (m_runController->requestStop()) {
			// Stop is a lifecycle boundary for transient playback overlays. The
			// worker may still publish one queued snapshot while it exits; the
			// delivery guard below drops it instead of repainting these states.
			m_presenter->clearOperationalTrackStates();
			m_followCamera->stop();
		}
	});

	connect(ui->displayTrainPathDiagrams, &QAction::triggered, this, &MainWindow::displayTrainPathDiagrams);

	connect(ui->actionLoad_Network, &QAction::triggered, m_session, &SceneSession::actionLoad_Network);

	ui->actionLoad_Network->setText("Load Legacy Case...");
	ui->actionLoad_Network->setShortcut(QKeySequence());
	m_newSceneAction = new QAction("New Case Study...", this);
	m_newSceneAction->setObjectName("actionNewCaseStudy");
	m_newSceneAction->setShortcut(QKeySequence::New);
	QAction* openSceneAction = new QAction("Open Case Study...", this);
	openSceneAction->setObjectName("actionOpenCaseStudyBundle");
	openSceneAction->setShortcut(QKeySequence::Open);
	QAction* openSceneFolderAction = new QAction("Open Scene Folder...", this);
	openSceneFolderAction->setObjectName("actionOpenSceneFolder");
	m_saveSceneAction = new QAction("Save Scene", this);
	m_saveSceneAction->setObjectName("actionSaveScene");
	m_saveSceneAction->setShortcut(QKeySequence::Save);
	m_saveSceneAsAction = new QAction("Save Case Study As...", this);
	m_saveSceneAsAction->setObjectName("actionSaveCaseStudyBundle");
	m_saveSceneAsFolderAction = new QAction("Save Scene As Folder...", this);
	m_saveSceneAsFolderAction->setObjectName("actionSaveSceneFolder");
	m_runSceneAction = new QAction("Run Scene", this);
	m_runSceneAction->setObjectName("actionRunScene");
	connect(m_newSceneAction, &QAction::triggered, m_session, &SceneSession::newScene);
	connect(openSceneAction, &QAction::triggered, m_session, &SceneSession::openSceneDialog);
	connect(openSceneFolderAction, &QAction::triggered, m_session, &SceneSession::openSceneFolderDialog);
	connect(m_saveSceneAction, &QAction::triggered, m_session, &SceneSession::saveScene);
	connect(m_saveSceneAsAction, &QAction::triggered, m_session, &SceneSession::saveSceneAs);
	connect(m_saveSceneAsFolderAction, &QAction::triggered, this, [this]() { m_session->saveSceneAsToDirectory(); });
	connect(m_runSceneAction, &QAction::triggered, m_runController, &RunController::runScene);
	if (ui->menuFile) {
		QAction* beforeAction = ui->actionLoad_Network;
		ui->menuFile->insertAction(beforeAction, m_newSceneAction);
		ui->menuFile->insertAction(beforeAction, openSceneAction);
		ui->menuFile->insertAction(beforeAction, openSceneFolderAction);
		ui->menuFile->insertAction(beforeAction, m_saveSceneAction);
		ui->menuFile->insertAction(beforeAction, m_saveSceneAsAction);
		ui->menuFile->insertAction(beforeAction, m_saveSceneAsFolderAction);
		ui->menuFile->insertAction(beforeAction, m_runSceneAction);
		ui->menuFile->insertMenu(beforeAction, m_session->recentScenesMenu());
		ui->menuFile->insertSeparator(beforeAction);
	}
	m_session->rebuildRecentScenesMenu();
	updateSceneActions();

	// connect scene signals
	connect(scene, &NetworkScene::MousePressedOnPreview, m_inspector, &InspectorPanel::displayPreviewInfo);
	connect(scene, &NetworkScene::MousePressedOnNode, m_inspector, &InspectorPanel::displayNodeInfo);
	connect(scene, &NetworkScene::MousePressedOnStationNode, m_inspector, &InspectorPanel::displayStationNodeInfo);
	connect(scene, &NetworkScene::MousePressedOnArc, m_inspector, &InspectorPanel::displayArcInfo);
	connect(scene, &NetworkScene::MousePressedOnConnection, m_inspector, &InspectorPanel::displayConnectionInfo);
	connect(scene, &NetworkScene::MousePressedOnSignal, m_inspector, &InspectorPanel::displaySignallingInfo);
	connect(scene, &NetworkScene::MousePressedOnTrain, m_inspector, &InspectorPanel::displayTrainInfo);
	connect(scene, &NetworkScene::MousePressedOnPassenger, m_inspector, &InspectorPanel::displayPassengerInfo);
	connect(scene, &NetworkScene::ContextMenuRequested, this, &MainWindow::showSceneContextMenu);
	connect(scene, &NetworkScene::DisableHighlight, m_inspector, &InspectorPanel::handleDisableHighlight);

	// connect signals from EGTRAIN simulation
	connect(&simulation, &DispatchController::snapshotAvailable, this, &MainWindow::waitForUpdates,
		Qt::QueuedConnection);

	// simulation control connections
	connect(ui->actionSimulationPause, &QAction::triggered, this, [this]() {
		if (!m_runController->running()) {
			ui->actionSimulationPause->setChecked(false);
			return;
		}
		ui->actionSimulationPause->setText(m_runController->togglePause() ? "Resume" : "Pause");
	});
	connect(m_speedSlider, &QSlider::valueChanged, this, [this](int value) {
		const int delayMs = stepDelayForSlider(value);
		updateSpeedModeDisplay(delayMs);
		m_runController->setDelayMs(delayMs);
	});

	// set application item
	QIcon window_icon;
	window_icon.addFile(":/app/egtrain-16.png", QSize(16, 16));
	window_icon.addFile(":/app/egtrain-32.png", QSize(32, 32));
	window_icon.addFile(":/app/egtrain-48.png", QSize(48, 48));
	window_icon.addFile(":/app/egtrain-64.png", QSize(64, 64));
	window_icon.addFile(":/app/egtrain-128.png", QSize(128, 128));
	window_icon.addFile(":/app/egtrain-256.png", QSize(256, 256));
	window_icon.addFile(":/app/egtrain-512.png", QSize(512, 512));
	window_icon.addFile(":/app/egtrain-1024.png", QSize(1024, 1024));
	setWindowIcon(window_icon);

	// --- Build toolbar ---
	m_toolBar = ui->mainToolBar;
	m_toolBar->clear();
	m_toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	m_toolBar->setIconSize(QSize(16, 16));
	m_toolBar->setFocusPolicy(Qt::NoFocus);
	m_toolBar->setFixedHeight(36);
	ui->actionSimulationStart->setText("Run");
	ui->actionSimulationStart->setObjectName("actionRun");
	ui->actionSimulationStart->setIcon(QIcon(":/icons/run.svg"));
	ui->actionSimulationStart->setProperty("iconResource", ":/icons/run.svg");
	ui->actionSimulationPause->setObjectName("actionPause");
	ui->actionSimulationPause->setIcon(QIcon(":/icons/pause.svg"));
	ui->actionSimulationPause->setProperty("iconResource", ":/icons/pause.svg");
	ui->actionSimulationStop->setObjectName("actionStop");
	ui->actionSimulationStop->setIcon(QIcon(":/icons/stop.svg"));
	ui->actionSimulationStop->setProperty("iconResource", ":/icons/stop.svg");
	ui->actionSimulationPause->setEnabled(false);
	ui->actionSimulationStop->setEnabled(false);
	ui->actionSimulationStart->setToolTip("Run simulation (Ctrl+R)");
	ui->actionSimulationPause->setToolTip("Pause or resume simulation (Ctrl+.)");
	ui->actionSimulationStop->setToolTip("Stop simulation (Ctrl+Esc)");


	// Keep the scenario state and the four useful display layers in the primary
	// left rail. All scene pointers below are non-owning; QGraphicsScene owns the
	// actual items and teardownGUI clears these observer lists after scene->clear().
	m_caseLayersDock = new QDockWidget("Case and Layers", this);
	m_caseLayersDock->setObjectName("caseLayersDock");
	m_caseLayersDock->setAllowedAreas(Qt::LeftDockWidgetArea);
	QWidget* caseLayersWidget = new QWidget(m_caseLayersDock);
	QVBoxLayout* caseLayersLayout = new QVBoxLayout(caseLayersWidget);
	QLabel* caseTitle = new QLabel("Current case study", caseLayersWidget);
	caseTitle->setObjectName("caseStudyTitleLabel");
	caseLayersLayout->addWidget(caseTitle);
	m_caseNameLabel = EditorLayout::wrappedLabel(QString(), caseLayersWidget);
	m_caseNameLabel->setObjectName("caseNameLabel");
	caseLayersLayout->addWidget(m_caseNameLabel);
	m_caseReadinessLabel = EditorLayout::wrappedLabel(QString(), caseLayersWidget);
	m_caseReadinessLabel->setObjectName("caseReadinessLabel");
	caseLayersLayout->addWidget(m_caseReadinessLabel);
	QLabel* layersTitle = new QLabel("Layers", caseLayersWidget);
	layersTitle->setObjectName("layersTitleLabel");
	caseLayersLayout->addWidget(layersTitle);
	m_stationLayerCheck = new QCheckBox("Stations and platforms", caseLayersWidget);
	m_stationLayerCheck->setObjectName("layerStationsPlatforms");
	m_stationLayerCheck->setChecked(true);
	m_stationLayerCheck->setToolTip("Show station symbols and platforms");
	m_stationNamesCheck = new QCheckBox("Station names", caseLayersWidget);
	m_stationNamesCheck->setObjectName("layerStationNames");
	m_stationNamesCheck->setProperty("secondaryLayerToggle", true);
	m_stationNamesCheck->setChecked(true);
	m_stationNamesCheck->setToolTip("Show station names without changing station symbols");
	m_trainLayerCheck = new QCheckBox("Trains", caseLayersWidget);
	m_trainLayerCheck->setObjectName("layerTrains");
	m_trainLayerCheck->setChecked(true);
	m_trainLayerCheck->setToolTip("Show trains");
	m_trainSpeedLabelsCheck = new QCheckBox("Train speed labels", caseLayersWidget);
	m_trainSpeedLabelsCheck->setObjectName("layerTrainSpeedLabels");
	m_trainSpeedLabelsCheck->setProperty("secondaryLayerToggle", true);
	m_trainSpeedLabelsCheck->setChecked(true);
	m_trainSpeedLabelsCheck->setToolTip("Show live speed in detailed train labels; overview markers stay compact.");
	m_signalLayerCheck = new QCheckBox("Signals", caseLayersWidget);
	m_signalLayerCheck->setObjectName("layerSignals");
	m_signalLayerCheck->setChecked(true);
	m_signalLayerCheck->setToolTip("Show signals and live aspects");
	m_passengerLayerCheck = new QCheckBox("Passengers", caseLayersWidget);
	m_passengerLayerCheck->setObjectName("layerPassengers");
	m_passengerLayerCheck->setChecked(true);
	m_passengerLayerCheck->setToolTip("Show passenger counts and icons");
	caseLayersLayout->addWidget(m_stationLayerCheck);
	caseLayersLayout->addWidget(m_stationNamesCheck);
	caseLayersLayout->addWidget(m_trainLayerCheck);
	caseLayersLayout->addWidget(m_trainSpeedLabelsCheck);
	caseLayersLayout->addWidget(m_signalLayerCheck);
	caseLayersLayout->addWidget(m_passengerLayerCheck);
	m_networkLegendWidget = new NetworkLegendWidget(caseLayersWidget);
	caseLayersLayout->addWidget(m_networkLegendWidget);
	caseLayersLayout->addStretch();
	auto* caseLayersScroll = new EditorLayout::VerticalScrollArea(m_caseLayersDock);
	caseLayersScroll->setObjectName("caseLayersScroll");
	caseLayersScroll->setWidget(caseLayersWidget);
	m_caseLayersDock->setWidget(caseLayersScroll);
	addDockWidget(Qt::LeftDockWidgetArea, m_caseLayersDock);
	if (ui->menuView) {
		// zoom actions above, panel toggles below
		ui->menuView->addSeparator();
		ui->menuView->addAction(m_caseLayersDock->toggleViewAction());
		m_showMapKeyAction = ui->menuView->addAction("Map key");
		m_showMapKeyAction->setObjectName("actionShowMapKey");
		connect(m_showMapKeyAction, &QAction::triggered, this, [this]() {
			m_caseLayersDock->show();
			m_caseLayersDock->raise();
			m_networkLegendWidget->setExpanded(true);
		});
		// Without a menu entry a closed Run Results dock stayed unreachable
		// until the next run finished.
		if (m_runResultsDock)
			ui->menuView->addAction(m_runResultsDock->toggleViewAction());
	}
	connect(m_stationLayerCheck, &QCheckBox::toggled, this, [this](bool checked) {
		m_presenter->m_stationLayerVisible = checked;
		for (auto* item : m_presenter->m_stationDecorations)
			if (item)
				item->setVisible(checked);
		for (auto* label : m_presenter->m_stationLabels)
			if (label)
				label->setVisible(checked && m_presenter->m_stationNamesVisible);
		// updateViewportOverlays owns station name visibility
		updateViewportOverlays();
	});
	connect(m_stationNamesCheck, &QCheckBox::toggled, this, [this](bool checked) {
		m_presenter->m_stationNamesVisible = checked;
		for (auto* overlay : m_presenter->m_stationOverlays)
			if (overlay)
				overlay->setNameVisible(checked);
		for (auto* label : m_presenter->m_stationLabels)
			if (label)
				label->setVisible(checked && m_presenter->m_stationLayerVisible);
		updateViewportOverlays();
	});
	connect(m_trainLayerCheck, &QCheckBox::toggled, this, [this](bool checked) {
		m_presenter->m_trainLayerVisible = checked;
		for (auto* train : m_presenter->allTrains)
			if (train)
				train->setVisible(checked && (m_replayActive ? replayTrainHasPosition(train->index) : !train->outOfSimulation));
		for (auto it = m_presenter->m_trainBadges.cbegin(); it != m_presenter->m_trainBadges.cend(); ++it) {
			if (!it.value())
				continue;
			auto trainIt = std::find_if(m_presenter->allTrains.cbegin(), m_presenter->allTrains.cend(),
				[it](const TrainItemGroup* train) { return train && train->index == it.key(); });
			it.value()->setVisible(checked && trainIt != m_presenter->allTrains.cend()
				&& (m_replayActive ? replayTrainHasPosition(it.key()) : !(*trainIt)->outOfSimulation));
		}
		updateViewportOverlays();
		cutToFollowedTrain(updateFollowAvailability());
	});
	connect(m_trainSpeedLabelsCheck, &QCheckBox::toggled, this, [this](bool checked) {
		m_presenter->m_trainSpeedLabelsVisible = checked;
		for (auto* badge : m_presenter->m_trainBadges)
			if (badge)
				badge->setSpeedVisible(checked);
	});
	connect(m_signalLayerCheck, &QCheckBox::toggled, this, [this](bool checked) {
		m_presenter->m_signalLayerVisible = checked;
		updateViewportOverlays();
	});
	connect(m_passengerLayerCheck, &QCheckBox::toggled, this, [this](bool checked) {
		m_presenter->m_passengerLayerVisible = checked;
		for (auto* platform : m_presenter->allPlatforms) {
			if (!platform)
				continue;
			if (platform->textIcon)
				platform->textIcon->setVisible(m_presenter->paxTextVisible());
			for (auto* icon : platform->passengerIcons)
				if (icon)
					icon->setVisible(checked);
		}
		if (m_presenter->trainPaxInfoItem)
			m_presenter->trainPaxInfoItem->setVisible(checked);
		if (m_presenter->paxIconInfoItem)
			m_presenter->paxIconInfoItem->setVisible(checked);
		if (checked) {
			m_presenter->updateTrainPaxInfo();
			m_presenter->updatePaxIconInfo(m_snapshot.get(),
				m_replayActive ? NetworkPresenter::Playback::Replay : NetworkPresenter::Playback::Live);
		}
	});
	updateCaseLayersPanel();

	m_followTrainCombo = new QComboBox(this);
	m_followTrainCombo->setObjectName("followTrainCombo");
	m_followTrainCombo->setMinimumWidth(180);
	m_followTrainCombo->setMaximumWidth(200);
	m_followTrainCombo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	m_followTrainCombo->setFocusPolicy(Qt::StrongFocus);
	m_followAction = new QAction("Follow", this);
	m_followAction->setObjectName("actionFollow");
	m_followAction->setCheckable(true);
	// One sentence in the status bar says whether the selected train is followed, and why not.
	m_followStatusLabel = new ElidedLabel(this);
	m_followStatusLabel->setObjectName("followStatusLabel");
	m_followStatusLabel->setAccessibleName("Follow status");
	m_followStatusLabel->setMaximumWidth(kFollowStatusMaxWidth);
	statusBar()->addPermanentWidget(m_followStatusLabel);
	connect(m_followTrainCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
		if (m_updatingFollowCombo || index < 0)
			return;
		if (m_followAction && m_followAction->isChecked())
			setFollowTrain(m_followTrainCombo->itemData(index).toInt());
		else
			updateFollowAvailability();
	});
	connect(m_followAction, &QAction::toggled, this, [this](bool checked) {
		if (!checked) {
			setFollowTrain(-1);
			return;
		}
		if (m_followTrainCombo && m_followTrainCombo->currentIndex() >= 0)
			setFollowTrain(m_followTrainCombo->currentData().toInt());
		else
			setFollowTrain(-1);
	});
	refreshFollowTrainChoices();

	// The one toolbar entry for every way of opening a case goes through the
	// existing chooser so bundled scenes, recents, folders, and legacy import
	// keep a single entry point.
	m_openCaseAction = new QAction("Open Case", this);
	m_openCaseAction->setObjectName("actionOpenCase");
	m_openCaseAction->setProperty("chooserEntryPoint", "showStartupChooser");
	m_openCaseAction->setToolTip("Choose a bundled, recent, scene-folder, or legacy case");
	connect(m_openCaseAction, &QAction::triggered, this, &MainWindow::showStartupChooser);

	connect(ui->actionStartTime, &QAction::triggered, m_runController, &RunController::setStartTime);
	ui->actionStartTime->setToolTip("Set the clock time the simulation starts at (HH:MM)");

	ui->actionZoomIn->setObjectName("actionZoomIn");
	ui->actionZoomIn->setIcon(QIcon(":/icons/zoom-in.svg"));
	ui->actionZoomIn->setToolTip("Zoom in on the network view (Ctrl++)");
	ui->actionZoomOut->setObjectName("actionZoomOut");
	ui->actionZoomOut->setIcon(QIcon(":/icons/zoom-out.svg"));
	ui->actionZoomOut->setToolTip("Zoom out on the network view (Ctrl+-)");
	ui->actionFitView->setObjectName("actionFit");
	ui->actionFitView->setText("Fit");
	ui->actionFitView->setIcon(QIcon());

	const auto addToolbarButton = [this](QAction* action, const char* objectName, Qt::ToolButtonStyle style) {
		m_toolBar->addAction(action);
		if (auto* button = qobject_cast<QToolButton*>(m_toolBar->widgetForAction(action))) {
			button->setObjectName(objectName);
			button->setToolButtonStyle(style);
			button->setAccessibleName(action->text());
			button->setAccessibleDescription(action->toolTip());
			button->setFocusPolicy(Qt::StrongFocus);
			button->setAutoRaise(true);
			button->setFixedHeight(32);
			// Accessibility can toggle the button without activating its action.
			// Reconcile after a normal click finishes so it cannot toggle twice.
			connect(button, &QToolButton::toggled, button, [button]() {
				QAction* defaultAction = button->defaultAction();
				if (defaultAction && defaultAction->isCheckable()
						&& defaultAction->isChecked() != button->isChecked())
					defaultAction->setChecked(button->isChecked()); }, Qt::QueuedConnection);
			const QString name = QString::fromLatin1(objectName);
			if (name == "actionZoomInButton" || name == "actionZoomOutButton")
				button->setFixedWidth(34);
		}
	};
	const auto addToolbarLabel = [this](const char* text, const char* objectName) {
		auto* label = new QLabel(QString::fromLatin1(text), m_toolBar);
		label->setObjectName(objectName);
		label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
		label->setFocusPolicy(Qt::NoFocus);
		label->setFixedHeight(24);
		label->setAccessibleName(QString::fromLatin1(text));
		m_toolBar->addWidget(label);
		return label;
	};
	const auto addToolbarGroupSeparator = [this](const char* objectName, const char* description) {
		QAction* separator = m_toolBar->addSeparator();
		separator->setObjectName(objectName);
		separator->setProperty("toolbarGroupBoundary", true);
		separator->setToolTip(QString::fromLatin1(description));
		return separator;
	};

	m_openCaseAction->setProperty("toolbarGroup", "Case");
	addToolbarButton(m_openCaseAction, "openCaseButton", Qt::ToolButtonTextOnly);
	addToolbarGroupSeparator("separatorCasePlayback", "Case commands / playback commands");
	ui->actionSimulationStart->setProperty("toolbarGroup", "Playback");
	ui->actionSimulationPause->setProperty("toolbarGroup", "Playback");
	ui->actionSimulationStop->setProperty("toolbarGroup", "Playback");
	addToolbarButton(ui->actionSimulationStart, "actionRunButton", Qt::ToolButtonTextBesideIcon);
	addToolbarButton(ui->actionSimulationPause, "actionPauseButton", Qt::ToolButtonTextBesideIcon);
	// The button reads Pause or Resume. Its width fits the longer text, so the toolbar does not move when the text changes.
	if (auto* pauseToolButton = qobject_cast<QToolButton*>(m_toolBar->widgetForAction(ui->actionSimulationPause))) {
		const QString shownText = ui->actionSimulationPause->text();
		int widestText = 0;
		for (const char* candidate : {"Pause", "Resume"}) {
			ui->actionSimulationPause->setText(QString::fromLatin1(candidate));
			widestText = qMax(widestText, pauseToolButton->sizeHint().width());
		}
		ui->actionSimulationPause->setText(shownText);
		pauseToolButton->setFixedWidth(widestText);
	}
	addToolbarButton(ui->actionSimulationStop, "actionStopButton", Qt::ToolButtonTextBesideIcon);
	QLabel* slowerLabel = addToolbarLabel("Slower", "speedSlowerLabel");
	slowerLabel->setProperty("toolbarGroup", "Playback");
	m_speedSlider->setFixedWidth(80);
	m_speedSlider->setFixedHeight(24);
	m_speedSlider->setProperty("toolbarGroup", "Playback");
	m_speedSlider->setAccessibleName("Simulation speed");
	m_speedSlider->setAccessibleDescription("Adjust simulation speed; right is faster");
	m_speedSlider->setFocusPolicy(Qt::StrongFocus);
	m_toolBar->addWidget(m_speedSlider);
	QLabel* fasterLabel = addToolbarLabel("Faster", "speedFasterLabel");
	fasterLabel->setProperty("toolbarGroup", "Playback");
	addToolbarGroupSeparator("separatorPlaybackView", "Playback commands / view commands");
	m_followAction->setProperty("toolbarGroup", "View");
	ui->actionZoomIn->setProperty("toolbarGroup", "View");
	ui->actionZoomOut->setProperty("toolbarGroup", "View");
	ui->actionFitView->setProperty("toolbarGroup", "View");
	addToolbarButton(m_followAction, "actionFollowButton", Qt::ToolButtonTextOnly);
	m_followTrainCombo->setFixedWidth(180);
	m_followTrainCombo->setFixedHeight(32);
	m_followTrainCombo->setAccessibleName("Train to follow");
	m_toolBar->addWidget(m_followTrainCombo);
	m_followTrainCombo->setFocusPolicy(Qt::StrongFocus);
	addToolbarButton(ui->actionZoomIn, "actionZoomInButton", Qt::ToolButtonIconOnly);
	addToolbarButton(ui->actionZoomOut, "actionZoomOutButton", Qt::ToolButtonIconOnly);
	addToolbarButton(ui->actionFitView, "actionFitButton", Qt::ToolButtonTextOnly);

	const auto setCommandBarTabOrder = [this]() {
		QWidget* openCaseButton = m_toolBar->widgetForAction(m_openCaseAction);
		QWidget* runButton = m_toolBar->widgetForAction(ui->actionSimulationStart);
		QWidget* pauseButton = m_toolBar->widgetForAction(ui->actionSimulationPause);
		QWidget* stopButton = m_toolBar->widgetForAction(ui->actionSimulationStop);
		QWidget* followButton = m_toolBar->widgetForAction(m_followAction);
		QWidget* zoomInButton = m_toolBar->widgetForAction(ui->actionZoomIn);
		QWidget* zoomOutButton = m_toolBar->widgetForAction(ui->actionZoomOut);
		QWidget* fitButton = m_toolBar->widgetForAction(ui->actionFitView);
		const QList<QWidget*> commandButtons{
			openCaseButton, runButton, pauseButton, stopButton, followButton, zoomInButton, zoomOutButton, fitButton};
		for (QWidget* widget : commandButtons) {
			if (widget)
				widget->setFocusPolicy(Qt::StrongFocus);
		}
		if (m_speedSlider)
			m_speedSlider->setFocusPolicy(Qt::StrongFocus);
		if (m_followTrainCombo)
			m_followTrainCombo->setFocusPolicy(Qt::StrongFocus);
		QWidget::setTabOrder(openCaseButton, runButton);
		QWidget::setTabOrder(runButton, pauseButton);
		QWidget::setTabOrder(pauseButton, stopButton);
		QWidget::setTabOrder(stopButton, m_speedSlider);
		QWidget::setTabOrder(m_speedSlider, followButton);
		QWidget::setTabOrder(followButton, m_followTrainCombo);
		QWidget::setTabOrder(m_followTrainCombo, zoomInButton);
		QWidget::setTabOrder(zoomInButton, zoomOutButton);
		QWidget::setTabOrder(zoomOutButton, fitButton);
	};
	setCommandBarTabOrder();
	QTimer::singleShot(0, this, setCommandBarTabOrder);
	// The toolbar button exists now, so it can take its description.
	updateFollowAvailability();

	// file menu: output folder chooser
	if (ui->menuFile) {
		m_outputFolderAction = new QAction("Set Output Folder...", this);
		connect(m_outputFolderAction, &QAction::triggered, m_runController, &RunController::chooseOutputFolder);
		ui->menuFile->addAction(m_outputFolderAction);
	}

	// in-app logging pane (ConsoleWidget installs its own streambuf on construction)
	m_logPane = new ConsoleWidget(this);
	addDockWidget(Qt::BottomDockWidgetArea, m_logPane);
	m_logPane->hide();
	m_logPane->toggleViewAction()->setShortcut(QKeySequence("Ctrl+Shift+L"));
	if (ui->menuView)
		ui->menuView->addAction(m_logPane->toggleViewAction());

	// scene validation panel: dockable table of SceneDiagnostic entries, empty until a scene loads
	m_validation = new ValidationPanel(this);
	m_validation->setDocument(m_document);
	m_validation->setDiagnostics(m_session->mutableDiagnostics());
	m_validation->setAdvancedDetails([this] { return advancedDetailsEnabled(); });
	m_validation->setDurationOverride([this] {
		return m_runController->durationOverride();
	});
	m_validation->setLoadedDataContext([this] {
		ValidationPanel::LoadedDataContext context;
		context.sceneDir = m_session->sceneDir();
		context.sceneIsBundle = m_session->sceneIsBundle();
		context.bundleVersion = m_session->sceneBundleVersion();
		context.runtimeStatus = m_runController->runtimeStatus();
		context.runtimeDiagnostics = m_runController->runtimeDiagnostics();
		context.appliedScenarioId = m_runSelection.appliedScenarioId();
		context.resultsAvailable = m_runController->resultsAvailable();
		return context;
	});
	connect(m_validation, &ValidationPanel::statusChanged, this, &MainWindow::updateCaseLayersPanel);
	connect(m_validation, &ValidationPanel::refreshed, this, [this] {
		m_scenarioPanel->refresh();
		m_passengerPanel->refresh();
		updateSceneActions();
	});
	connect(m_validation, &ValidationPanel::targetActivated, this, &MainWindow::openLoadedDataTarget);
	m_validationDock = new QDockWidget("Scene Validation", this);
	m_validationDock->setObjectName("sceneValidationDock");
	m_validationDock->setWidget(m_validation->table());
	addDockWidget(Qt::BottomDockWidgetArea, m_validationDock);
	m_validationDock->hide();
	tabifyDockWidget(m_logPane, m_validationDock);
	m_validationDock->toggleViewAction()->setShortcut(QKeySequence("Ctrl+Shift+V"));
	if (ui->menuView)
		ui->menuView->addAction(m_validationDock->toggleViewAction());

	m_loadedDataDock = new QDockWidget("Loaded Data", this);
	m_loadedDataDock->setObjectName("loadedDataDock");
	m_loadedDataDock->setWidget(m_validation->tree());
	addDockWidget(Qt::BottomDockWidgetArea, m_loadedDataDock);
	m_loadedDataDock->hide();
	tabifyDockWidget(m_validationDock, m_loadedDataDock);
	tabifyDockWidget(m_loadedDataDock, m_runResultsDock);
	if (ui->menuView)
		ui->menuView->addAction(m_loadedDataDock->toggleViewAction());

	m_advancedDetailsAction = new QAction(QStringLiteral("Advanced / Developer details"), this);
	m_advancedDetailsAction->setObjectName(QStringLiteral("actionAdvancedDetails"));
	m_advancedDetailsAction->setCheckable(true);
	m_advancedDetailsAction->setToolTip(QStringLiteral(
		"Show technical inventories, validation details, and non-blocking warnings"));
	QSettings detailsSettings;
	m_advancedDetailsAction->setChecked(detailsSettings.value(kAdvancedDetailsKey, false).toBool());
	if (ui->menuView) {
		ui->menuView->addSeparator();
		ui->menuView->addAction(m_advancedDetailsAction);
	}
	connect(m_advancedDetailsAction, &QAction::toggled, this, [this](bool enabled) {
		QSettings settings;
		settings.setValue(kAdvancedDetailsKey, enabled);
		settings.sync();
		if (m_loadedDataDock) {
			m_loadedDataDock->setVisible(enabled);
			if (enabled)
				m_loadedDataDock->raise();
		}
		if (!enabled && m_validationDock)
			m_validationDock->hide();
		m_validation->updateStatus();
		m_scenarioPanel->updatePresentation();
		refreshRunResultsSummary();
		updateDiagramActions();
	});

	m_caseSettingsDock = new QDockWidget("Case Settings", this);
	m_caseSettingsDock->setObjectName("caseSettingsDock");
	m_caseSettingsPanel = new CaseSettingsPanel(m_caseSettingsDock);
	m_caseNameEdit = m_caseSettingsPanel->nameEdit();
	m_caseDescriptionEdit = m_caseSettingsPanel->descriptionEdit();
	m_caseBaseTimeEdit = m_caseSettingsPanel->baseTimeEdit();
	m_caseDurationSecondsEdit = m_caseSettingsPanel->durationSecondsEdit();
	m_caseBufferSecondsEdit = m_caseSettingsPanel->bufferSecondsEdit();
	m_caseRecoveryPercentEdit = m_caseSettingsPanel->recoveryPercentEdit();
	m_caseSettingsDock->setWidget(m_caseSettingsPanel);
	addDockWidget(Qt::RightDockWidgetArea, m_caseSettingsDock);
	m_caseSettingsDock->hide();
	registerEditorDock(m_caseSettingsDock);
	m_caseSettingsPanel->setDocument(m_document);
	m_caseSettingsDock->setEnabled(false);
	connect(m_document, &SceneDocument::replaced, m_caseSettingsDock, [this] { m_caseSettingsDock->setEnabled(true); });
	connect(m_caseSettingsPanel, &CaseSettingsPanel::baseTimeChanged, this,
		[this](const QString& baseTime) { setStartOffset(baseTimeToSeconds(baseTime.toStdString())); });
	connect(m_caseSettingsPanel, &CaseSettingsPanel::changed, this, [this] {
		refreshValidationPanel();
		m_servicePanel->refreshOccurrencePreview();
		m_timetableStopsPanel->refresh();
	});

	// Infrastructure editor: one small facet selector and a table keep all
	// canonical infrastructure fields visible without introducing a second model.
	m_infrastructureDock = new QDockWidget("Infrastructure", this);
	m_infrastructureDock->setObjectName("infrastructureDock");
	m_infrastructurePanel = new InfrastructurePanel(m_infrastructureDock);
	m_infrastructureFacetCombo = m_infrastructurePanel->facetCombo();
	m_infrastructureTable = m_infrastructurePanel->table();
	m_addInfrastructureButton = m_infrastructurePanel->addButton();
	m_deleteInfrastructureButton = m_infrastructurePanel->deleteButton();
	m_blockTrackFilterCombo = m_infrastructurePanel->blockTrackFilterCombo();
	m_insertBlockButton = m_infrastructurePanel->insertBlockButton();
	m_moveBlockUpButton = m_infrastructurePanel->moveBlockUpButton();
	m_moveBlockDownButton = m_infrastructurePanel->moveBlockDownButton();
	m_routeSectionCatalogCombo = m_infrastructurePanel->routeSectionCatalogCombo();
	m_routeSectionListWidget = m_infrastructurePanel->routeSectionListWidget();
	m_addRouteSectionButton = m_infrastructurePanel->addRouteSectionButton();
	m_removeRouteSectionButton = m_infrastructurePanel->removeRouteSectionButton();
	m_moveRouteSectionUpButton = m_infrastructurePanel->moveRouteSectionUpButton();
	m_moveRouteSectionDownButton = m_infrastructurePanel->moveRouteSectionDownButton();
	m_infrastructureDock->setWidget(m_infrastructurePanel);
	addDockWidget(Qt::RightDockWidgetArea, m_infrastructureDock);
	m_infrastructureDock->hide();
	registerEditorDock(m_infrastructureDock);
	m_infrastructurePanel->setPrompts(m_userPrompts.get());
	for (std::unique_ptr<InfrastructureFacet>& facet : makeTopologyFacets())
		m_infrastructurePanel->setFacet(std::move(facet));
	for (std::unique_ptr<InfrastructureFacet>& facet : makeBlockRouteFacets())
		m_infrastructurePanel->setFacet(std::move(facet));
	for (std::unique_ptr<InfrastructureFacet>& facet : makeStationFacets())
		m_infrastructurePanel->setFacet(std::move(facet));
	for (std::unique_ptr<InfrastructureFacet>& facet : makeSignalFacets())
		m_infrastructurePanel->setFacet(std::move(facet));
	m_infrastructurePanel->setDocument(m_document);
	connect(m_infrastructurePanel, &InfrastructurePanel::selectionChanged, this, [this]() {
		if (m_document->isLoaded() && !m_runController->running() && !m_runController->resultsAvailable())
			renderTrackPreview(m_document->model());
	});
	connect(m_infrastructurePanel, &InfrastructurePanel::changed, this, &MainWindow::refreshValidationPanel);
	connect(m_infrastructurePanel, &InfrastructurePanel::entitiesChanged, this, [this](const QString& facet) {
		if (facet == QStringLiteral("stations") || facet == QStringLiteral("platforms") || facet == QStringLiteral("routes"))
			m_servicePanel->refresh();
		if (facet == QStringLiteral("blocks") || facet == QStringLiteral("signals"))
			refreshIncidentPanel();
	});
	connect(m_infrastructurePanel, &InfrastructurePanel::statusMessage, this,
		[this](const QString& text, int timeoutMs) { statusBar()->showMessage(text, timeoutMs); });
	connect(m_infrastructureDock, &QDockWidget::visibilityChanged, this,
		[this](bool visible) {
			if (visible)
				m_infrastructurePanel->updateSelection();
		});
	refreshInfrastructurePanel();

	m_trainUnitDock = new QDockWidget("Rolling stock units", this);
	m_trainUnitDock->setObjectName("trainUnitDock");
	m_trainUnitPanel = new TrainUnitPanel(m_trainUnitDock);
	m_trainUnitListWidget = m_trainUnitPanel->listWidget();
	m_trainUnitIdEdit = m_trainUnitPanel->idEdit();
	m_trainUnitPhysicalEdits = m_trainUnitPanel->physicalEdits();
	m_trainUnitSourceDataEdit = m_trainUnitPanel->sourceDataEdit();
	m_trainUnitSourceTractionEdit = m_trainUnitPanel->sourceTractionEdit();
	m_trainUnitTractionTable = m_trainUnitPanel->tractionTable();
	m_addTrainUnitButton = m_trainUnitPanel->addButton();
	m_addTrainUnitTractionButton = m_trainUnitPanel->addTractionRowButton();
	m_plotTrainUnitTractionButton = m_trainUnitPanel->plotButton();
	m_trainUnitDock->setWidget(m_trainUnitPanel);
	addDockWidget(Qt::RightDockWidgetArea, m_trainUnitDock);
	registerEditorDock(m_trainUnitDock);
	m_trainUnitPanel->setPrompts(m_userPrompts.get());
	m_trainUnitSourceLinker = new TrainUnitSourceLinker(this);
	m_trainUnitSourceLinker->setDialogParent(this);
	m_trainUnitSourceLinker->setDocument(m_document);
	connect(m_trainUnitSourceLinker, &TrainUnitSourceLinker::statusChanged, this, &MainWindow::updateTrainUnitSourceStatus);
	connect(m_trainUnitSourceLinker, &TrainUnitSourceLinker::commitPendingEditsRequested, this, &MainWindow::commitPendingEditorValues);
	connect(m_trainUnitSourceLinker, &TrainUnitSourceLinker::statusMessage, this,
		[this](const QString& message, int milliseconds) { statusBar()->showMessage(message, milliseconds); });
	connect(m_trainUnitSourceLinker, &TrainUnitSourceLinker::linksCleared, this, [this] {
		for (DiagramWindow* window : findChildren<DiagramWindow*>()) {
			if (window && window->property("inputTrainUnitId").isValid())
				window->close();
		}
	});
	connect(m_trainUnitSourceLinker, &TrainUnitSourceLinker::reloaded, this, [this](const QStringList& tractionUnitIds) {
		m_trainUnitPanel->refresh();
		m_compositionPanel->refresh();
		refreshValidationPanel();
		for (const QString& unitId : tractionUnitIds)
			refreshInputTractionDiagrams(unitId.toStdString());
	});
	connect(m_trainUnitPanel, &TrainUnitPanel::detailsShown, this, &MainWindow::updateTrainUnitSourceStatus);
	connect(m_trainUnitPanel, &TrainUnitPanel::sourceLinkRequested, this, [this](bool traction) {
		commitPendingEditorValues();
		if (const SceneTrainUnit* unit = m_trainUnitPanel->selectedUnit())
			m_trainUnitSourceLinker->link(unit->id, traction);
	});
	connect(m_trainUnitPanel, &TrainUnitPanel::sourceUnlinkRequested, this, [this](bool traction) {
		if (const SceneTrainUnit* unit = m_trainUnitPanel->selectedUnit())
			m_trainUnitSourceLinker->unlink(unit->id, traction);
	});
	connect(m_trainUnitPanel, &TrainUnitPanel::sourceRetryRequested, this, [this](bool traction) {
		if (const SceneTrainUnit* unit = m_trainUnitPanel->selectedUnit())
			m_trainUnitSourceLinker->retry(unit->id, traction);
	});
	connect(m_trainUnitPanel, &TrainUnitPanel::unitRenamed, this, [this](const QString& oldId, const QString& newId) {
		m_trainUnitSourceLinker->renameUnit(oldId.toStdString(), newId.toStdString());
		const SceneTrainUnit* unit = sceneTrainUnitById(m_document->model(), newId.toStdString());
		for (DiagramWindow* window : findChildren<DiagramWindow*>()) {
			if (window && unit && window->property("inputTrainUnitId").toString() == oldId) {
				window->setProperty("inputTrainUnitId", newId);
				window->setPresentation("Input traction characteristic", inputTractionContext(*unit), inputTractionWarning(*unit));
			}
		}
	});
	connect(m_trainUnitPanel, &TrainUnitPanel::unitDeleted, this, [this](const QString& id) {
		m_trainUnitSourceLinker->forgetUnit(id.toStdString());
		refreshInputTractionDiagrams(id.toStdString());
	});
	connect(m_trainUnitPanel, &TrainUnitPanel::changed, this, &MainWindow::refreshValidationPanel);
	const auto plotUnitTraction = [this](const QString& unitId) {
		if (const SceneTrainUnit* unit = sceneTrainUnitById(m_document->model(), unitId.toStdString()))
			plotTrainUnitTraction(*unit);
	};
	connect(m_trainUnitPanel, &TrainUnitPanel::tractionPlotRequested, this, plotUnitTraction);
	m_trainUnitPanel->setDocument(m_document);

	m_compositionDock = new QDockWidget("Compositions", this);
	m_compositionDock->setObjectName("compositionDock");
	m_compositionPanel = new CompositionPanel(m_compositionDock);
	m_compositionListWidget = m_compositionPanel->listWidget();
	m_compositionIdEdit = m_compositionPanel->idEdit();
	m_compositionUnitsListWidget = m_compositionPanel->unitsListWidget();
	m_addCompositionButton = m_compositionPanel->addButton();
	m_addUnitButton = m_compositionPanel->addUnitButton();
	m_compositionUnitSourceDataLabel = m_compositionPanel->unitSourceDataLabel();
	m_plotTractionButton = m_compositionPanel->plotTractionButton();
	m_compositionDock->setWidget(m_compositionPanel);
	addDockWidget(Qt::RightDockWidgetArea, m_compositionDock);
	m_compositionDock->hide();
	tabifyDockWidget(m_trainUnitDock, m_compositionDock);
	registerEditorDock(m_compositionDock);
	m_compositionPanel->setPrompts(m_userPrompts.get());
	m_compositionPanel->setDocument(m_document);
	connect(m_compositionPanel, &CompositionPanel::compositionIdsChanged, this, [this] { m_servicePanel->updateDetail(); });
	connect(m_compositionPanel, &CompositionPanel::changed, this, &MainWindow::refreshValidationPanel);
	connect(m_compositionPanel, &CompositionPanel::tractionPlotRequested, this, plotUnitTraction);
	connect(m_trainUnitPanel, &TrainUnitPanel::trainUnitsChanged, m_compositionPanel, &CompositionPanel::refresh);
	connect(m_trainUnitPanel, &TrainUnitPanel::sourceReferencesChanged, m_compositionPanel, &CompositionPanel::refreshUnitDetails);

	// service editor: dockable panel for the service-level fields, with the timetable (stops)
	// of the selected service below them. Tabbed alongside Compositions.
	m_serviceDock = new QDockWidget("Services", this);
	m_serviceDock->setObjectName("serviceDock");
	m_servicePanel = new ServicePanel(m_serviceDock);
	m_serviceListWidget = m_servicePanel->listWidget();
	m_serviceIdEdit = m_servicePanel->idEdit();
	m_serviceOperatingCodeEdit = m_servicePanel->operatingCodeEdit();
	m_serviceCategoryCombo = m_servicePanel->categoryCombo();
	m_serviceColorButton = m_servicePanel->colorButton();
	m_serviceCompositionCombo = m_servicePanel->compositionCombo();
	m_serviceRouteCombo = m_servicePanel->routeCombo();
	m_serviceHasEntryTimeCheck = m_servicePanel->hasEntryTimeCheck();
	m_serviceEntryTimeSecondsEdit = m_servicePanel->entryTimeSecondsEdit();
	m_serviceHasRepeatCheck = m_servicePanel->hasRepeatCheck();
	m_serviceHeadwaySecondsEdit = m_servicePanel->headwaySecondsEdit();
	m_serviceHasRepeatCountCheck = m_servicePanel->hasRepeatCountCheck();
	m_serviceRepeatCountEdit = m_servicePanel->repeatCountEdit();
	m_servicePerformancePercentEdit = m_servicePanel->performancePercentEdit();
	m_serviceHasMaximumSpeedCheck = m_servicePanel->hasMaximumSpeedCheck();
	m_serviceMaximumSpeedKmhEdit = m_servicePanel->maximumSpeedKmhEdit();
	m_serviceHasOperatingCodeStepCheck = m_servicePanel->hasOperatingCodeStepCheck();
	m_serviceOperatingCodeStepEdit = m_servicePanel->operatingCodeStepEdit();
	m_addServiceButton = m_servicePanel->addButton();
	m_serviceOccurrenceTable = m_servicePanel->occurrenceTable();
	m_selectNoneOccurrencesButton = m_servicePanel->selectNoneButton();

	m_timetableStopsPanel = new TimetableStopsPanel(m_servicePanel);
	m_stopTableWidget = m_timetableStopsPanel->table();
	m_addStopButton = m_timetableStopsPanel->addButton();
	m_insertStopAfterButton = m_timetableStopsPanel->insertAfterButton();
	m_moveStopUpButton = m_timetableStopsPanel->moveUpButton();
	m_moveStopDownButton = m_timetableStopsPanel->moveDownButton();
	m_stopTimeModeCombo = m_timetableStopsPanel->timeModeCombo();
	m_stopTimeBaseLabel = m_timetableStopsPanel->timeBaseLabel();
	m_servicePanel->setTimetableWidget(m_timetableStopsPanel);

	m_serviceDock->setWidget(m_servicePanel);
	addDockWidget(Qt::RightDockWidgetArea, m_serviceDock);
	m_serviceDock->hide();
	tabifyDockWidget(m_compositionDock, m_serviceDock);
	registerEditorDock(m_serviceDock);

	// scenario library and incident editor. each
	// incident has a type (signal_failure or train_breakdown) and a target whose
	// valid choices depend on that type, analogous to a stop's platform choices
	// depending on the selected station.
	m_incidentDock = new QDockWidget("Incidents", this);
	m_incidentDock->setObjectName("incidentDock");
	QWidget* incidentWidget = new QWidget(m_incidentDock);
	QVBoxLayout* incidentLayout = new QVBoxLayout(incidentWidget);

	m_scenarioPanel = new ScenarioPanel(incidentWidget);
	m_scenarioListWidget = m_scenarioPanel->listWidget();
	m_scenarioIdEdit = m_scenarioPanel->idEdit();
	m_scenarioNameEdit = m_scenarioPanel->nameEdit();
	m_scenarioDescriptionEdit = m_scenarioPanel->descriptionEdit();
	m_blankScenarioButton = m_scenarioPanel->blankButton();
	m_duplicateScenarioButton = m_scenarioPanel->duplicateButton();
	m_deleteScenarioButton = m_scenarioPanel->deleteButton();

	// The header names the scenario whose entries the tabs show, also while the scenario list is on another tab. It stays on
	// one line, and a name wider than the dock is cut at the edge; the tooltip has all of it.
	QLabel* scenarioHeaderLabel = new QLabel(incidentWidget);
	scenarioHeaderLabel->setObjectName("scenarioHeaderLabel");
	scenarioHeaderLabel->setTextFormat(Qt::PlainText);
	scenarioHeaderLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	QFont scenarioHeaderFont = scenarioHeaderLabel->font();
	scenarioHeaderFont.setBold(true);
	scenarioHeaderLabel->setFont(scenarioHeaderFont);
	incidentLayout->addWidget(scenarioHeaderLabel, 0);

	QTabWidget* scenarioEditorTabs = new QTabWidget(incidentWidget);
	scenarioEditorTabs->setObjectName("scenarioEditorTabs");
	scenarioEditorTabs->addTab(m_scenarioPanel, "Scenario");

	m_incidentPanel = new IncidentPanel(scenarioEditorTabs);
	m_incidentListWidget = m_incidentPanel->listWidget();
	m_incidentIdEdit = m_incidentPanel->idEdit();
	m_incidentTypeCombo = m_incidentPanel->typeCombo();
	m_incidentTargetCombo = m_incidentPanel->targetCombo();
	m_incidentStartSecondsEdit = m_incidentPanel->startSecondsEdit();
	m_incidentEndSecondsEdit = m_incidentPanel->endSecondsEdit();
	m_incidentHasReducedSpeedCheck = m_incidentPanel->hasReducedSpeedCheck();
	m_incidentReducedSpeedKmhEdit = m_incidentPanel->reducedSpeedKmhEdit();
	m_incidentHasEndSecondsCheck = m_incidentPanel->hasEndSecondsCheck();
	m_incidentTerminateAtDestinationCheck = m_incidentPanel->terminateAtDestinationCheck();
	m_addIncidentButton = m_incidentPanel->addButton();
	scenarioEditorTabs->addTab(m_incidentPanel, "Incidents");

	m_entranceDelayPanel = new EntranceDelayPanel(scenarioEditorTabs);
	m_entranceDelayListWidget = m_entranceDelayPanel->listWidget();
	m_entranceDelayServiceCombo = m_entranceDelayPanel->serviceCombo();
	m_entranceDelayOccurrenceEdit = m_entranceDelayPanel->occurrenceEdit();
	m_entranceDelayOccurrenceContextLabel = m_entranceDelayPanel->occurrenceContextLabel();
	m_entranceDelayStationCombo = m_entranceDelayPanel->stationCombo();
	m_entranceDelaySecondsEdit = m_entranceDelayPanel->secondsEdit();
	m_addEntranceDelayButton = m_entranceDelayPanel->addButton();
	m_duplicateEntranceDelayButton = m_entranceDelayPanel->duplicateButton();
	m_deleteEntranceDelayButton = m_entranceDelayPanel->deleteButton();
	scenarioEditorTabs->addTab(m_entranceDelayPanel, "Entrance delays");
	scenarioEditorTabs->setMinimumSize(0, 0);
	incidentLayout->addWidget(scenarioEditorTabs, 1);

	m_incidentDock->setWidget(incidentWidget);
	addDockWidget(Qt::RightDockWidgetArea, m_incidentDock);
	m_incidentDock->hide();
	m_trainUnitDock->hide();
	tabifyDockWidget(m_serviceDock, m_incidentDock);
	registerEditorDock(m_incidentDock);

	m_scenarioPanel->setPrompts(m_userPrompts.get());
	m_scenarioPanel->setRunSelection(&m_runSelection);
	m_scenarioPanel->setDiagnostics(&m_session->diagnostics());
	m_scenarioPanel->setAdvancedDetails([this] { return advancedDetailsEnabled(); });
	m_scenarioPanel->setExportObserver([this] {
		const auto operation = captureTelemetryOperation();
		return [operation](const std::vector<SceneDiagnostic>& diagnostics) {
			operation.failure(telemetry::Operation::Export, telemetryDiagnosticError(diagnostics));
		};
	});
	m_scenarioPanel->setDocument(m_document);
	const auto showScenarioHeader = [scenarioHeaderLabel](const QString& text) {
		scenarioHeaderLabel->setText(text);
		scenarioHeaderLabel->setToolTip(text);
	};
	showScenarioHeader(m_scenarioPanel->headerText());
	connect(m_scenarioPanel, &ScenarioPanel::headerTextChanged, scenarioHeaderLabel, showScenarioHeader);
	connect(m_scenarioPanel, &ScenarioPanel::scenarioSelected, this, [this] {
		invalidateRunResults();
		refreshIncidentPanel();
	});
	connect(m_scenarioPanel, &ScenarioPanel::statusMessage, this,
		[this](const QString& text, int timeoutMs) { statusBar()->showMessage(text, timeoutMs); });
	connect(m_scenarioPanel, &ScenarioPanel::changed, this, &MainWindow::refreshValidationPanel);
	m_incidentPanel->setPrompts(m_userPrompts.get());
	m_incidentPanel->setRunSelection(&m_runSelection);
	m_incidentPanel->setOccurrenceDuration([this] { return serviceOccurrenceDuration(); });
	m_incidentPanel->setDocument(m_document);
	connect(m_incidentPanel, &IncidentPanel::changed, this, &MainWindow::refreshValidationPanel);
	m_entranceDelayPanel->setPrompts(m_userPrompts.get());
	m_entranceDelayPanel->setRunSelection(&m_runSelection);
	m_entranceDelayPanel->setOccurrenceDuration([this] { return serviceOccurrenceDuration(); });
	m_entranceDelayPanel->setDocument(m_document);
	connect(m_entranceDelayPanel, &EntranceDelayPanel::changed, this, &MainWindow::refreshValidationPanel);

	// Passenger authoring: a deliberately direct list/detail editor over the
	// canonical SceneModel.  Journeys and legs remain nested under the selected
	// passenger; no second tree model or transient copy is needed.
	m_passengerDock = new QDockWidget("Passengers", this);
	m_passengerDock->setObjectName("passengerDock");
	m_passengerPanel = new PassengerPanel(m_passengerDock);
	m_passengerListWidget = m_passengerPanel->passengerList();
	m_passengerIdEdit = m_passengerPanel->passengerIdEdit();
	m_addPassengerButton = m_passengerPanel->addPassengerButton();
	m_passengerTabs = m_passengerPanel->tabs();
	m_passengerJourneyListWidget = m_passengerPanel->journeyList();
	m_addPassengerJourneyButton = m_passengerPanel->addJourneyButton();
	m_passengerJourneyIdEdit = m_passengerPanel->journeyIdEdit();
	m_passengerJourneyActivityEdit = m_passengerPanel->journeyActivityEdit();
	m_passengerJourneyOriginCombo = m_passengerPanel->journeyOriginCombo();
	m_passengerJourneyDestinationCombo = m_passengerPanel->journeyDestinationCombo();
	m_passengerJourneyWindowEdits = m_passengerPanel->journeyWindowEdits();
	m_passengerLegListWidget = m_passengerPanel->legList();
	m_addPassengerLegButton = m_passengerPanel->addLegButton();
	m_passengerLegIdEdit = m_passengerPanel->legIdEdit();
	m_passengerLegOriginCombo = m_passengerPanel->legOriginCombo();
	m_passengerLegDestinationCombo = m_passengerPanel->legDestinationCombo();
	m_passengerLegServiceCombo = m_passengerPanel->legServiceCombo();
	m_passengerLegOccurrenceEdit = m_passengerPanel->legOccurrenceEdit();
	m_passengerDock->setWidget(m_passengerPanel);
	// A dock's viewport, rather than its form, limits the main window size.
	// Keep wide tables and enlarged-font controls reachable when space is tight.
	for (QDockWidget* dock : {m_caseSettingsDock, m_infrastructureDock, m_trainUnitDock,
			 m_compositionDock, m_serviceDock, m_incidentDock, m_passengerDock}) {
		auto* content = dock->widget();
		auto* scroll = new QScrollArea(dock);
		scroll->setObjectName("editorContentScroll");
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		scroll->setMinimumSize(0, 0);
		scroll->setWidget(content);
		dock->setWidget(scroll);
		// The wheel scrolls the dock over a field and changes the field only once it has focus.
		FocusWheel::watch(content);
	}
	addDockWidget(Qt::RightDockWidgetArea, m_passengerDock);
	m_passengerDock->hide();
	tabifyDockWidget(m_serviceDock, m_passengerDock);
	registerEditorDock(m_passengerDock);

	m_passengerPanel->setPrompts(m_userPrompts.get());
	m_passengerPanel->setDiagnostics(&m_session->diagnostics());
	m_passengerPanel->setOccurrenceDuration([this] { return serviceOccurrenceDuration(); });
	m_passengerPanel->setImportFolderChooser([this]() -> QString {
		if (const auto& passengerSource = mainWindowTestHooks().passengerSource) {
			const QString sourcePath = passengerSource();
			if (!sourcePath.isEmpty())
				return sourcePath;
		}
		const QString startDir = m_session->sceneDir().isEmpty() ? QDir::homePath() : QFileInfo(m_session->sceneDir()).absolutePath();
		return QFileDialog::getExistingDirectory(this, "Select Legacy Passenger Folder", startDir);
	});
	m_passengerPanel->setDocument(m_document);
	m_passengerDock->setEnabled(false);
	connect(m_passengerPanel, &PassengerPanel::statusMessage, this,
		[this](const QString& text, int timeoutMs) { statusBar()->showMessage(text, timeoutMs); });
	connect(m_passengerPanel, &PassengerPanel::changed, this, &MainWindow::refreshValidationPanel);

	m_servicePanel->setPrompts(m_userPrompts.get());
	m_servicePanel->setRunSelection(&m_runSelection);
	m_servicePanel->setOccurrenceDuration([this] { return serviceOccurrenceDuration(); });
	connect(m_servicePanel, &ServicePanel::selectedServiceChanged, m_timetableStopsPanel, &TimetableStopsPanel::setServiceId);
	connect(m_timetableStopsPanel, &TimetableStopsPanel::changed, this, &MainWindow::refreshValidationPanel);
	connect(m_servicePanel, &ServicePanel::changed, this, &MainWindow::refreshValidationPanel);
	m_servicePanel->setDocument(m_document);
	m_timetableStopsPanel->setDocument(m_document);
	m_serviceDock->setEnabled(false);
	m_editorPanels = {m_caseSettingsPanel, m_trainUnitPanel, m_infrastructurePanel, m_passengerPanel, m_compositionPanel,
		m_servicePanel, m_timetableStopsPanel, m_scenarioPanel, m_incidentPanel, m_entranceDelayPanel};

	// permanent status-bar field for the scene validation summary, so it does
	// not clobber transient load/save messages
	statusBar()->addPermanentWidget(m_validation->statusLabel());
	m_zoomStatusLabel = new QLabel(this);
	m_zoomStatusLabel->setObjectName("zoomStatusLabel");
	m_zoomStatusLabel->setToolTip("Network zoom");
	statusBar()->addPermanentWidget(m_zoomStatusLabel);
	updateZoomStatus();

	// Connect zoom actions
	connect(ui->actionZoomIn, &QAction::triggered, this, &MainWindow::zoomIn);
	connect(ui->actionZoomOut, &QAction::triggered, this, &MainWindow::zoomOut);
	connect(ui->actionFitView, &QAction::triggered, this, &MainWindow::fitToView);
	ui->actionFitView->setToolTip("Restore the full network view (Ctrl+0)");
	// Quit closes the window, so it asks about unsaved changes and stops a running simulation
	// like the close button does. The application exits when its last window has closed.
	connect(ui->actionQuit, &QAction::triggered, this, &QWidget::close);

	// Diagrams menu
	m_diagramsMenu = menuBar()->addMenu("Diagrams");
	m_diagramsMenu->addAction("Speed / Distance (per train)...", this, &MainWindow::showSpeedDistanceDiagram);
	m_diagramsMenu->addAction("Speed / Time (per train)...", this, &MainWindow::showSpeedTimeDiagram);
	m_diagramsMenu->addAction("Time / Distance (per train)...", this, &MainWindow::showTimeDistanceDiagram);
	m_diagramsMenu->addAction("Simulated tractive effort / Distance (per train)...", this, &MainWindow::showTractiveEffortDistanceDiagram);
	m_diagramsMenu->addSeparator();
	m_diagramsMenu->addAction("Timetable graph (planned vs simulated stops)...", this, &MainWindow::showTimetableGraph);
	m_diagramsMenu->addAction("Blocking-time overlay...", this, &MainWindow::showBlockingTimeDiagram);
	m_diagramsMenu->addAction("Capacity analysis...", this, &MainWindow::showCapacityAnalysis);
	m_diagramsMenu->addAction("Capacity by area...", this, &MainWindow::showAreaCapacity);
	m_diagramsMenu->addAction("Timetable table (planned vs simulated)...", this, &MainWindow::showTimetableTable);
	m_diagramsMenu->addAction("Train delays...", this, &MainWindow::showDelayDiagram);
	m_diagramsMenu->addAction("Delay analysis...", this, &MainWindow::showDelayAnalysis);
	// Train paths belongs with the other charts; retire the one-entry Tools menu.
	m_diagramsMenu->addSeparator();
	ui->displayTrainPathDiagrams->setText("Train paths (simulated movement)...");
	m_diagramsMenu->addAction(ui->displayTrainPathDiagrams);
	if (ui->menuTools)
		menuBar()->removeAction(ui->menuTools->menuAction());
	updateDiagramActions();

	// --- Status bar ---
	statusBar()->showMessage(m_runController->readyMessage());

	refreshIncidentPanel();
	refreshInfrastructurePanel();
	// Dock/editor construction above can rebuild the top-level focus chain;
	// restore the command-bar sequence after every child widget exists.
	setCommandBarTabOrder();
	if (startupTimingEnabled()) {
		bool ok = false;
		m_startupTimingWarmTrials = qEnvironmentVariableIntValue("QEGTRAIN_STARTUP_WARM_TRIALS", &ok);
		if (!ok || m_startupTimingWarmTrials < 0)
			m_startupTimingWarmTrials = 0;
	}
}

MainWindow::~MainWindow() {
	m_updateTelemetry->stopTelemetry();
	m_runController->clearSimulationWorker(true);
	if (const auto windowDestroyed = mainWindowTestHooks().windowDestroyed)
		windowDestroyed();
}

void MainWindow::closeEvent(QCloseEvent* event) {
	const bool confirmed = m_closeConfirmed;
	m_closeConfirmed = false;
	if (!confirmed && !m_session->maybeSaveScene()) {
		event->ignore();
		return;
	}
	// A running simulation is stopped first. The window closes when its worker has finished.
	if (m_runController->deferUntilRunStopped([this] { m_closeConfirmed = true; close(); })) {
		event->ignore();
		return;
	}
	m_runController->clearSimulationWorker(true);
	// The window closes now, so nothing is left to recover: the user saved the scene or discarded it.
	if (m_recovery)
		m_recovery->shutdown();
	QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
	m_session->dragEnter(event);
}
void MainWindow::dropEvent(QDropEvent* event) {
	m_session->drop(event);
}
telemetry::OperationObservation MainWindow::captureTelemetryOperation() const noexcept {
	return m_updateTelemetry->captureOperation();
}

void MainWindow::registerEditorDock(QDockWidget* dock) {
	m_editorActions.insert(dock, telemetry::registerEditor(dock, editorsMenu(), [this] { return captureTelemetryOperation(); }));
	// The dock also takes Undo and Redo, so that they work while it floats as a window of its own.
	dock->addAction(m_undoAction);
	dock->addAction(m_redoAction);
}

QMenu* MainWindow::createPopupMenu() {
	QMenu* menu = QMainWindow::createPopupMenu();
	if (!menu) return nullptr;
	for (auto it = m_editorActions.cbegin(); it != m_editorActions.cend(); ++it) {
		QAction* original = it.key()->toggleViewAction();
		if (!menu->actions().contains(original)) continue;
		menu->insertAction(original, it.value());
		menu->removeAction(original);
	}
	return menu;
}

void MainWindow::revealEditorDock(QDockWidget* dock) {
	revealEditorDock(dock, captureTelemetryOperation());
}

void MainWindow::revealEditorDock(QDockWidget* dock, const telemetry::OperationObservation& operation) {
	if (dock == m_caseSettingsDock || dock == m_infrastructureDock || dock == m_trainUnitDock
		|| dock == m_compositionDock || dock == m_serviceDock || dock == m_incidentDock
		|| dock == m_passengerDock)
		telemetry::revealEditor(dock, operation);
	else if (dock) {
		dock->show();
		dock->raise();
	}
}

bool MainWindow::openSceneDirectory(const QString& dir) {
	return m_session->openSceneDirectory(dir);
}

bool MainWindow::dialogsSuppressed() const {
	return e2eDialogsSuppressed();
}

void MainWindow::commitPendingEdits() {
	commitPendingEditorValues();
}

bool MainWindow::afterRunStopped(std::function<void()> action) {
	return m_runController->deferUntilRunStopped(std::move(action));
}

void MainWindow::bringToFront() {
	setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
	raise();
	activateWindow();
	if (const auto& hook = mainWindowTestHooks().windowBroughtToFront)
		hook(*this);
}

void MainWindow::offerUpdateCheck() {
	m_updateTelemetry->startUpdateCheck(true);
}

void MainWindow::sceneActionsChanged() {
	updateSceneActions();
}

QString MainWindow::recoveredSceneSource() const {
	return m_recoveredSourceHint;
}

SceneLoadResult MainWindow::readScene(const QString& path, bool& identityOk) {
	const qint64 loadStarted = startupTimingEnabled() ? startupTimingNowNanoseconds() : 0;
	SceneLoadResult result = loadScenePath(path.toStdString());
	identityOk = !startupTimingEnabled()
		|| (sameStartupIdentity(g_startupIdentity, startupIdentity(path, result.scene, result.inputSnapshot))
			&& g_startupIdentity.inputSnapshot == result.inputSnapshot);
	if (startupTimingEnabled())
		recordStartupTiming(QStringLiteral("canonical_load"), m_startupTimingIteration, -1,
			startupTimingNowNanoseconds() - loadStarted, QStringLiteral("scene_open"),
			QStringLiteral("MainWindow::openSceneDirectory"), identityOk);
	return result;
}

void MainWindow::sceneWillBeReplaced() {
	teardownGUI();
	simulation.resetState();
	m_runSelection.includeAll();
	m_runController->sceneWillBeReplaced();
}

void MainWindow::sceneCreated(const telemetry::OperationObservation& operation) {
	m_history->reset(m_document->model());
	recoverySceneReplaced(RecoverySource());
	m_runController->selectSceneOutputFolder(m_document->model().name);
	m_runController->clearDelayBaseline(QStringLiteral(
		"Delay baseline cleared: a new case study was created. Complete a run with no incidents or entrance delays to set a new baseline."));
	m_runSelection.selectScenario(m_document->model().defaultScenarioId);
	m_runSelection.clearModifiedScenarios();
	m_passengerPanel->clearImportResult();
	m_runController->setStartOffsetSeconds(baseTimeToSeconds(m_document->model().baseTime));
	invalidateRunResults();
	m_session->updateSceneWindowTitle();
	updateCaseLayersPanel();
	updateSceneActions();
	statusBar()->showMessage("New case study created; save it to choose a location");
	refreshIncidentPanel();
	refreshInfrastructurePanel();
	refreshValidationPanel();
	renderTrackPreview(m_document->model());
	telemetry::revealEditor(m_caseSettingsDock, operation);
	telemetry::revealEditor(m_infrastructureDock, operation);
}

bool MainWindow::sceneOpened(const SceneSession::OpenedScene& opened) {
	m_runController->selectSceneOutputFolder(m_document->model().name);
	m_runController->clearDelayBaseline(QStringLiteral(
		"Delay baseline cleared: another case study was opened. Complete a run with no incidents or entrance delays to set a new baseline."));
	m_runSelection.selectScenario(m_document->model().defaultScenarioId);
	if (m_runSelection.scenarioId().empty() && !m_document->model().scenarios.empty()) {
		m_runSelection.selectScenario(m_document->model().scenarios.front().id);
		m_document->mutableModel().defaultScenarioId = m_runSelection.scenarioId();
	}
	m_history->reset(m_document->model(), !opened.recovered);
	recoverySceneReplaced(opened.recovered ? RecoverySource() : recoverySourceOf(opened.path, opened.bundle));
	m_runSelection.clearModifiedScenarios();
	m_passengerPanel->clearImportResult();
	invalidateRunResults();
	m_runController->setStartOffsetSeconds(baseTimeToSeconds(m_document->model().baseTime));
	m_session->updateSceneWindowTitle();
	updateCaseLayersPanel();
	updateSceneActions();
	if (!opened.recovered)
		m_session->addRecentScene(opened.path);
	refreshIncidentPanel();
	refreshInfrastructurePanel(!opened.reloadingSameScene);
	refreshValidationPanel();
	renderTrackPreview(m_document->model());
	statusBar()->showMessage(QString("%1: %2 (%3 services, %4 routes)")
			.arg(opened.reloadingSameScene ? "Scene reloaded" : "Scene loaded")
			.arg(QString::fromStdString(m_document->model().name))
			.arg(static_cast<int>(m_document->model().services.size()))
			.arg(static_cast<int>(m_document->model().routes.size())));
	if (startupTimingEnabled()) {
		if (!opened.timingIdentityOk) {
			failStartupTiming(QStringLiteral("scene identity, counts, or canonical input snapshot changed"));
			return false;
		}
		if (m_startupTimingScenePath.isEmpty())
			m_startupTimingScenePath = opened.path;
		networkView->armTimingPaint(QStringLiteral("preview"), m_startupTimingIteration);
	}
	if (m_loadedDataDock && advancedDetailsEnabled()) {
		m_loadedDataDock->show();
		m_loadedDataDock->raise();
	}
	return true;
}

void MainWindow::sceneSaved() {
	m_history->markSaved();
	m_document->markSaved();
	recoverySceneSaved();
	m_runSelection.clearModifiedScenarios();
	m_session->updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	m_compositionPanel->refresh();
	m_trainUnitPanel->refresh();
	m_servicePanel->refresh();
	refreshIncidentPanel();
	refreshInfrastructurePanel();
	m_passengerPanel->refresh();
	// Last, because refreshing the panels redraws the track preview, which writes its own status message.
	statusBar()->showMessage("Scene saved");
}

bool MainWindow::telemetryActive() const {
	return m_updateTelemetry->sender() != nullptr;
}

int MainWindow::stepDelayMs() const {
	return stepDelayForSlider(m_speedSlider->value());
}

void MainWindow::validateScene() {
	refreshValidationPanel();
}

void MainWindow::showLoadedData() {
	if (m_loadedDataDock) {
		m_loadedDataDock->show();
		m_loadedDataDock->raise();
	}
}

void MainWindow::showValidation() {
	if (m_validationDock) {
		m_validationDock->show();
		m_validationDock->raise();
	}
}

void MainWindow::runPreparing() {
	statusBar()->showMessage("Preparing scene simulation...");
	if (!startupTimingEnabled())
		QApplication::processEvents();
}

void MainWindow::teardownForRun() {
	const QRectF previewFitBounds = m_presenter->m_previewFitBounds;
	const QPointF previewZoomFocus = m_presenter->m_previewZoomFocus;
	teardownGUI();
	m_presenter->m_previewFitBounds = previewFitBounds;
	m_presenter->m_previewZoomFocus = previewZoomFocus;
}

std::vector<SceneDiagnostic> MainWindow::timedPreparation(
	const std::function<std::vector<SceneDiagnostic>()>& prepare, bool& identityOk) {
	const bool identityBeforePrepare = !startupTimingEnabled()
		|| startupTimingIdentityMatches(m_session->sceneDir(), m_document->model());
	const qint64 prepareStarted = startupTimingEnabled() ? startupTimingNowNanoseconds() : 0;
	std::vector<SceneDiagnostic> diagnostics = prepare();
	identityOk = identityBeforePrepare
		&& (!startupTimingEnabled() || startupTimingIdentityMatches(m_session->sceneDir(), m_document->model()));
	if (startupTimingEnabled())
		recordStartupTiming(QStringLiteral("native_runtime_construction"), m_startupTimingIteration,
			-1, startupTimingNowNanoseconds() - prepareStarted,
			QStringLiteral("simulation.prepareScene"), QStringLiteral("MainWindow::runScene"),
			identityOk);
	return diagnostics;
}

void MainWindow::loadedDataChanged() {
	m_validation->refreshLoadedData();
}

void MainWindow::caseLayersChanged() {
	updateCaseLayersPanel();
}

void MainWindow::buildRunCanvas() {
	setupGUI();
	fitView();
	if (PlaybackProfiler::enabled())
		m_playbackProfileViewApplied = false;
}

bool MainWindow::startupTimingHoldsRun(bool identityOk) {
	if (!startupTimingEnabled())
		return false;
	if (!identityOk) {
		failStartupTiming(QStringLiteral("scene identity or counts changed during native preparation"));
		return true;
	}
	networkView->armTimingPaint(QStringLiteral("runtime"), m_startupTimingIteration);
	statusBar()->showMessage(QString("Prepared scene: %1").arg(QString::fromStdString(m_document->model().name)));
	return true;
}

void MainWindow::runStarting() {
	clearReplay();
	progressBar->show();
	statusBar()->showMessage("Simulation running...");
}

void MainWindow::workerReady() {
	if (const auto hook = mainWindowTestHooks().runStarting)
		hook(*this);
}

void MainWindow::runPhaseChanged(RunController::RunPhase phase) {
	switch (phase) {
		case RunController::RunPhase::Idle:
			// Pause and Stop only mean something while a worker exists.
			if (ui->actionSimulationPause) {
				ui->actionSimulationPause->setEnabled(false);
				ui->actionSimulationPause->setText("Pause");
				ui->actionSimulationPause->setChecked(false);
			}
			if (ui->actionSimulationStop)
				ui->actionSimulationStop->setEnabled(false);
			if (m_outputFolderAction)
				m_outputFolderAction->setEnabled(true);
			if (ui->actionStartTime)
				ui->actionStartTime->setEnabled(true);
			break;
		case RunController::RunPhase::Running:
			ui->actionSimulationPause->setEnabled(true);
			ui->actionSimulationStop->setEnabled(true);
			// A run keeps the output folder and the start time it was prepared with.
			if (m_outputFolderAction)
				m_outputFolderAction->setEnabled(false);
			ui->actionStartTime->setEnabled(false);
			break;
		case RunController::RunPhase::Stopping:
			ui->actionSimulationPause->setEnabled(false);
			ui->actionSimulationStop->setEnabled(false);
			break;
	}
}

void MainWindow::runPanelsChanged() {
	refreshInfrastructurePanel();
	refreshIncidentPanel();
	updateSceneActions();
}

void MainWindow::playbackEnded() {
	m_presenter->clearOperationalTrackStates();
}

bool MainWindow::endFollowing() {
	const bool hadFollowTarget = m_followTrainIndex >= 0
		|| (m_followAction && m_followAction->isChecked());
	setFollowTrain(-1);
	return hadFollowTarget;
}

void MainWindow::resultsKept() {
	refreshRunResults();
}

void MainWindow::resultsDiscarded(bool stoppedByUser, bool sceneChangedDuringRun) {
	// The inspector shows values of the run that is gone.
	m_inspector->handleDisableHighlight();
	if (m_runResultsTable)
		m_runResultsTable->setRowCount(0);
	if (m_runResultsSummaryLabel)
		m_runResultsSummaryLabel->setText(QString("No results | Case: %1 | Scenario: %2 | Status: %3%4")
				.arg(QString::fromStdString(m_document->model().name), scenarioContext(), m_runController->runtimeStatus(),
					sceneChangedDuringRun ? QStringLiteral(" (scene changed during run)") : QString()));
	if (m_runResultsDock) {
		m_runResultsDock->setWindowTitle(
			QStringLiteral("Run Results \u2014 %1 (%2)")
				.arg(scenarioContext(), stoppedByUser ? QStringLiteral("stopped") : QStringLiteral("failed")));
		m_runResultsDock->hide();
	}
}

bool MainWindow::runFinished() {
	const auto hook = mainWindowTestHooks().runFinished;
	return hook && hook(*this);
}

void MainWindow::runProgressEnded() {
	progressBar->hide();
}

void MainWindow::replayReady(GuiReplayHistory history) {
	m_completedReplay = std::move(history);
	if (!m_completedReplay.empty() || m_completedReplay.oversize())
		showReplayBar();
}

void MainWindow::runEnded() {
	// A completed run can be replayed, so a train that has left can be followed in it.
	updateFollowAvailability();
	runPanelsChanged();
	m_trainUnitSourceLinker->processChanges();
}

void MainWindow::runSettled() {
	if (const auto hook = mainWindowTestHooks().runSettled)
		hook(*this);
}

void MainWindow::delayBaselineChanged() {
	refreshRunResults();
	updateDiagramActions();
}

void MainWindow::renderTrackPreview(const SceneModel& sceneModel) {
	m_inspector->clearPreviewInspection();
	m_presenter->resetPreviewFit();
	const QString selectedInfrastructureId = m_infrastructurePanel->selectionId();
	const bool hasRuntimeGraphics = static_cast<bool>(m_snapshot) || !m_presenter->allTrains.isEmpty()
		|| !m_presenter->allArcs.isEmpty() || !m_presenter->allSignals.isEmpty()
		|| !m_presenter->allPlatforms.isEmpty() || !m_presenter->m_stationOverlays.isEmpty()
		|| !m_presenter->m_stationDecorations.isEmpty() || !m_presenter->m_signalDecorations.isEmpty()
		|| !m_presenter->m_vcMessageItems.isEmpty() || !m_presenter->m_trainAnimations.isEmpty()
		|| !m_presenter->m_tracksBySectionId.empty() || !m_presenter->m_tracksByOccupiedArc.empty()
		|| !m_presenter->m_activeTrackItems.empty();
	if (hasRuntimeGraphics) {
		// A dirty edit invalidates run results; clear the runtime-owned item
		// observers before rebuilding the authoring preview so no stale pointers
		// remain after scene->clear().
		teardownGUI();
		m_infrastructurePanel->setSelectionId(selectedInfrastructureId);
	} else if (scene) {
		m_presenter->m_inspectedSignal = nullptr;
		scene->clear();
	}
	m_showingTrackPreview = true;
	m_presenter->effect = nullptr;
	NetworkPresenter::PreviewOptions options;
	options.selectedFacet = m_infrastructurePanel->facetKey().toStdString();
	options.selectedId = m_infrastructurePanel->selectionId().toStdString();
	options.markSelection = m_infrastructureDock && m_infrastructureDock->isVisible();
	options.platformBars = m_runController->passengerGui();
	const NetworkPresenter::PreviewOutcome outcome = m_presenter->showPreview(sceneModel, options);
	if (!outcome.available) {
		statusBar()->showMessage("Scene loaded - no track preview available; simulation not running");
		return;
	}
	if (outcome.fitted)
		updateViewportOverlays();
	updateNetworkLegend();
	statusBar()->showMessage(QString("Previewing %1 trackline(s); simulation not running").arg(outcome.trackLines));
}

void MainWindow::startRecovery() {
	m_recovery.reset();
	const QString root = recoveryRoot();
	if (root.isEmpty())
		return;
	m_recovery = std::make_unique<RecoveryController>(root);
	connect(m_recovery.get(), &RecoveryController::writeFailed, this, [this](const QString& message) {
		if (m_recoveryFailureShown)
			return;
		m_recoveryFailureShown = true;
		statusBar()->showMessage(QStringLiteral("Autosave unavailable: %1. Save the case study to keep your edits.").arg(message));
	});
}

void MainWindow::recoverySceneChanged() {
	if (!m_recovery)
		return;
	if (!m_document->isDirty()) {
		m_recovery->noteClean();
		return;
	}
	RecoverySource source = m_recoverySource;
	source.displayName = QString::fromStdString(m_document->model().name);
	m_recovery->noteEdited(m_history->head(), source, m_document->revision());
}

void MainWindow::recoverySceneSaved() {
	m_recoverySource = recoverySourceOf(m_session->sceneDir(), m_session->sceneIsBundle());
	m_recoveredSourceHint.clear();
	if (m_recovery)
		m_recovery->noteSaved();
}

void MainWindow::recoverySceneReplaced(const RecoverySource& source) {
	m_recoverySource = source;
	m_recoveredSourceHint.clear();
	m_recoveryFailureShown = false;
	if (!m_recovery)
		return;
	m_recovery->noteReplaced();
	// A scene that is new or recovered has no saved state, so a copy of it is due.
	if (m_document->isDirty())
		recoverySceneChanged();
}

bool MainWindow::offerRecovery() {
	if (!m_recovery)
		return false;
	RecoveryScan scan = m_recovery->scan();
	const auto openable = [&scan]() {
		return std::any_of(scan.candidates.begin(), scan.candidates.end(),
			[](const RecoveryCandidate& candidate) { return candidate.state == RecoveryCandidateState::Openable; });
	};
	// A copy that this version cannot open is listed with the others and never offered alone.
	while (openable()) {
		RecoveryPrompt prompt(scan, this);
		if (const auto& dialogShown = mainWindowTestHooks().dialogShown)
			dialogShown(prompt, "recoveryPrompt");
		prompt.exec();
		const int index = prompt.selectedIndex();
		if (index < 0 || index >= scan.candidates.size())
			return false;
		const RecoveryCandidate candidate = scan.candidates.at(index);
		if (prompt.result() == RecoveryPrompt::Recover)
			return openRecoveredScene(candidate);
		if (prompt.result() != RecoveryPrompt::Discard)
			return false;
		if (!m_recovery->discard(candidate).ok)
			statusBar()->showMessage(QStringLiteral("A recovery copy could not be removed."), 8000);
		scan.candidates.removeAt(index);
		scan.removedOverCap = 0;
	}
	return false;
}

bool MainWindow::openRecoveredScene(const RecoveryCandidate& candidate) {
	// The copy is no file that the user opened, so nothing about it is reported.
	if (!m_session->openSceneDirectory(candidate.bundlePath, telemetry::OperationObservation(), SceneSession::OpenOrigin::Recovered))
		return false;
	// The scene stays unsaved and untitled. Its original is not touched until the user saves over it.
	m_recoverySource = candidate.source;
	m_recoveredSourceHint = candidate.source.path;
	// The copy now belongs to this scene, so that a crash right after the recovery loses nothing.
	if (m_recovery && !m_recovery->adopt(candidate).ok)
		recoverySceneChanged();
	statusBar()->showMessage(QStringLiteral("Recovered %1. It is not saved: choose Save As to keep it.")
			.arg(QString::fromStdString(m_document->model().name)));
	return true;
}

void MainWindow::updateSceneActions() {
	if (m_saveSceneAction)
		m_saveSceneAction->setEnabled(m_document->isLoaded());
	if (m_saveSceneAsAction)
		m_saveSceneAsAction->setEnabled(m_document->isLoaded());
	if (m_saveSceneAsFolderAction)
		m_saveSceneAsFolderAction->setEnabled(m_document->isLoaded());
	if (m_runSceneAction)
		m_runSceneAction->setEnabled(m_document->isLoaded() && !m_runController->running() && !hasErrors(m_session->diagnostics()));
	if (ui->actionSimulationStart) {
		const bool sceneRunnable = m_document->isLoaded() && !m_runController->running() && !hasErrors(m_session->diagnostics());
		ui->actionSimulationStart->setEnabled(sceneRunnable);
		ui->actionSimulationStart->setToolTip(QString("Run simulation (Ctrl+R)"));
	}
	if (m_serviceDock)
		m_serviceDock->setEnabled(m_document->isLoaded() && !m_runController->running());
	if (m_passengerDock)
		m_passengerDock->setEnabled(m_document->isLoaded() && !m_runController->running());
	updateUndoRedoActions();
	updateDiagramActions();
	if (QMenu* recentScenesMenu = m_session->recentScenesMenu())
		recentScenesMenu->setEnabled(!SceneSession::recentScenes().isEmpty());
}

bool MainWindow::advancedDetailsEnabled() const {
	if (m_advancedDetailsAction)
		return m_advancedDetailsAction->isChecked();
	QSettings settings;
	return settings.value(kAdvancedDetailsKey, false).toBool();
}

void MainWindow::refreshValidationPanel() {
	if (m_committingPendingEditorValues)
		return;
	commitPendingEditorValues();
	m_validation->refresh();
}

void MainWindow::openLoadedDataTarget(const QString& targetType, QTreeWidgetItem* item) {
	const auto operation = captureTelemetryOperation();
	const QString targetId = item->text(0);
	auto raiseDock = [this, operation](QDockWidget* dock) { revealEditorDock(dock, operation); };
	if (targetType == "network") {
		if (networkView) {
			networkView->setFocus(Qt::OtherFocusReason);
			fitToView();
			statusBar()->showMessage("Showing the existing network view", 3000);
		}
		return;
	}
	if (targetType == "validation") {
		raiseDock(m_validationDock);
		m_validation->selectDiagnosticOfFile(item->text(1));
		return;
	}
	if (targetType == "train_unit_plot") {
		const QString unitId = item->parent() && item->parent()->parent()
			? item->parent()->parent()->text(0)
			: QString();
		const SceneTrainUnit* unit = sceneTrainUnitById(m_document->model(), unitId.toStdString());
		if (unit && !unit->tractionCurve.empty())
			plotTrainUnitTraction(*unit);
		return;
	}

	if (targetType == "train_unit") {
		if (m_trainUnitPanel->selectUnit(targetId))
			raiseDock(m_trainUnitDock);
		return;
	}

	QListWidget* targetList = nullptr;
	QDockWidget* targetDock = nullptr;
	if (targetType == "composition") {
		targetList = m_compositionListWidget;
		targetDock = m_compositionDock;
	} else if (targetType == "service") {
		targetList = m_serviceListWidget;
		targetDock = m_serviceDock;
	}
	if (targetList) {
		const QList<QListWidgetItem*> matches = targetList->findItems(targetId, Qt::MatchExactly);
		if (!matches.isEmpty()) {
			targetList->setCurrentItem(matches.front());
			raiseDock(targetDock);
		}
	} else if (targetType == "incident" && m_incidentListWidget) {
		const auto& incidents = selectedScenarioIncidents();
		for (int row = 0; row < static_cast<int>(incidents.size()); ++row) {
			if (QString::fromStdString(incidents[static_cast<std::size_t>(row)].id) == targetId) {
				m_incidentListWidget->setCurrentRow(row);
				if (auto* tabs = m_incidentDock->findChild<QTabWidget*>("scenarioEditorTabs"))
					tabs->setCurrentWidget(m_incidentPanel);
				raiseDock(m_incidentDock);
				return;
			}
		}
	}
}

void MainWindow::sceneDocumentChanged() {
	m_history->record(m_document->model(), modifiedScenarioIds());
	invalidateAfterSceneChange();
	recoverySceneChanged();
}

void MainWindow::invalidateAfterSceneChange() {
	if (m_runController->sceneChanged()) {
		m_validation->refreshLoadedData();
		updateDiagramActions();
	} else {
		invalidateRunResults();
	}
	m_session->updateSceneWindowTitle();
	updateSceneActions();
}

namespace {

// The text of an Undo or Redo action: the verb, and the label of the edit that it would revert or repeat.
QString historyActionText(const QString& verb, const std::string& label) {
	return label.empty() ? verb : QStringLiteral("%1 %2").arg(verb, QString::fromStdString(label));
}

} // namespace

void MainWindow::setupEditMenu() {
	m_undoAction = new QAction(historyActionText(QStringLiteral("Undo"), {}), this);
	m_undoAction->setObjectName(QStringLiteral("actionUndo"));
	m_undoAction->setShortcut(QKeySequence::Undo);
	m_redoAction = new QAction(historyActionText(QStringLiteral("Redo"), {}), this);
	m_redoAction->setObjectName(QStringLiteral("actionRedo"));
	m_redoAction->setShortcuts(QKeySequence::Redo);
	for (QAction* action : {m_undoAction, m_redoAction}) {
		action->setShortcutContext(Qt::WindowShortcut);
		action->setEnabled(false);
		ui->menuEdit->addAction(action);
	}
	connect(m_undoAction, &QAction::triggered, this, &MainWindow::undoScene);
	connect(m_redoAction, &QAction::triggered, this, &MainWindow::redoScene);
}

void MainWindow::updateUndoRedoActions() {
	const bool editable = m_document->isLoaded() && !m_runController->running();
	const bool canUndo = editable && m_history->canUndo();
	const bool canRedo = editable && m_history->canRedo();
	m_undoAction->setEnabled(canUndo);
	m_redoAction->setEnabled(canRedo);
	m_undoAction->setText(historyActionText(QStringLiteral("Undo"), canUndo ? m_history->undoLabel() : std::string()));
	m_redoAction->setText(historyActionText(QStringLiteral("Redo"), canRedo ? m_history->redoLabel() : std::string()));
}

std::set<std::string> MainWindow::modifiedScenarioIds() const {
	std::set<std::string> ids;
	for (const SceneScenario& scenario : m_document->model().scenarios) {
		if (m_runSelection.isScenarioModified(scenario.id))
			ids.insert(scenario.id);
	}
	return ids;
}

void MainWindow::undoScene() {
	stepSceneHistory(false);
}

void MainWindow::redoScene() {
	stepSceneHistory(true);
}

void MainWindow::stepSceneHistory(bool redo) {
	if (!m_document->isLoaded() || m_runController->running())
		return;
	// A value that is still being typed becomes the edit that Undo reverts.
	commitPendingEditorValues();
	if (!(redo ? m_history->canRedo() : m_history->canUndo()))
		return;
	const QString label = QString::fromStdString(redo ? m_history->redoLabel() : m_history->undoLabel());
	restoreSceneFromHistory(*(redo ? m_history->redo() : m_history->undo()));
	statusBar()->showMessage(QString("%1 %2").arg(redo ? "Redid" : "Undid", label), 8000);
}

void MainWindow::restoreSceneFromHistory(const SceneHistoryEntry& entry) {
	const QScopedValueRollback<bool> restoring(m_restoringHistory, true);
	std::vector<std::string> previousUnitIds;
	for (const SceneTrainUnit& unit : m_document->model().trainUnits)
		previousUnitIds.push_back(unit.id);

	m_document->restore(*entry.scene, !m_history->isAtSavedPoint());

	// What the window keeps about the scene by id follows the restored scene. A rolling stock link stays with a unit that
	// still has its id, and is dropped for one that is gone.
	m_runSelection.clearModifiedScenarios();
	for (const std::string& id : entry.modifiedScenarioIds)
		m_runSelection.markScenarioModified(id);
	const bool scenarioKept = std::any_of(m_document->model().scenarios.begin(), m_document->model().scenarios.end(),
		[this](const SceneScenario& scenario) { return scenario.id == m_runSelection.scenarioId(); });
	if (!scenarioKept) {
		const SceneScenario* fallback = defaultScenario(m_document->model());
		m_runSelection.selectScenario(fallback ? fallback->id : std::string());
	}
	for (const std::string& id : previousUnitIds) {
		if (m_trainUnitSourceLinker->linkOf(id) && !sceneTrainUnitById(m_document->model(), id))
			m_trainUnitSourceLinker->forgetUnit(id);
	}
	setStartOffset(baseTimeToSeconds(m_document->model().baseTime));
	m_runSelection.pruneExcluded(m_document->model(), serviceOccurrenceDuration());

	invalidateAfterSceneChange();
	refreshAllEditorPanels();
	updateCaseLayersPanel();
	recoverySceneChanged();
}

// Shows the scene in every editor, in the order in which sceneOpened refreshes them: validation last.
void MainWindow::refreshAllEditorPanels() {
	m_caseSettingsPanel->refresh();
	m_trainUnitPanel->refresh();
	m_compositionPanel->refresh();
	m_servicePanel->refresh();
	m_timetableStopsPanel->refresh();
	refreshIncidentPanel();
	refreshInfrastructurePanel();
	refreshValidationPanel();
}

void MainWindow::invalidateRunResults() {
	if (!m_runController->running()) {
		if (m_replayActive && m_document->isLoaded() && scene) {
			setFollowTrain(-1);
			m_inspector->handleCloseInfoDockWidget();
			renderTrackPreview(m_document->model()); // Teardown clears history and graphics together.
		}
		clearReplay();
	}
	m_runController->discardResults();
	if (m_runResultsTable)
		m_runResultsTable->setRowCount(0);
	if (m_runResultsSummaryLabel)
		m_runResultsSummaryLabel->setText(QString("No completed run | Scenario: %1").arg(scenarioContext()));
	if (m_runResultsDock)
		m_runResultsDock->setWindowTitle(QString("Run Results — %1 (not built)").arg(scenarioContext()));
	if (m_runResultsDock)
		m_runResultsDock->hide();
	m_validation->refreshLoadedData();
	updateDiagramActions();
	refreshFollowTrainChoices();
}

void MainWindow::commitPendingEditorValues() {
	// The editors hold the scene before a restored one until they are refreshed, and that is not an edit.
	if (m_committingPendingEditorValues || m_restoringHistory)
		return;
	m_committingPendingEditorValues = true;
	// The scenario, incident and entrance delay controls are disabled and cleared during a run, and their panels take
	// nothing then, because the document is not editable.
	for (SceneEditorPanel* panel : m_editorPanels)
		panel->commitPendingEditors();
	m_committingPendingEditorValues = false;
}

void MainWindow::commitPendingCaseSettings() {
	m_caseSettingsPanel->commitPendingEditors();
}

void MainWindow::commitCaseSettings() {
	m_caseSettingsPanel->commit();
}

void MainWindow::refreshInfrastructurePanel(bool resetSelection) {
	m_infrastructureDock->setEnabled(m_document->isLoaded() && !m_runController->running());
	m_infrastructurePanel->refresh(resetSelection);
}

void MainWindow::updateTrainUnitSourceStatus() {
	const SceneTrainUnit* unit = m_trainUnitPanel->selectedUnit();
	const TrainUnitSourceLinker::Link* state = unit ? m_trainUnitSourceLinker->linkOf(unit->id) : nullptr;
	m_trainUnitPanel->setSourceState(false, state ? state->dataPath : QString(), state ? state->dataStatus : QString());
	m_trainUnitPanel->setSourceState(true, state ? state->tractionPath : QString(), state ? state->tractionStatus : QString());
}

void MainWindow::refreshInputTractionDiagrams(const std::string& unitId) {
	const SceneTrainUnit* unit = sceneTrainUnitById(m_document->model(), unitId);
	for (DiagramWindow* window : findChildren<DiagramWindow*>()) {
		if (!window || window->property("inputTrainUnitId").toString() != QString::fromStdString(unitId)
			|| !window->isVisible())
			continue;
		if (!unit || unit->tractionCurve.empty()) {
			window->close();
			continue;
		}
		window->setPresentation("Input traction characteristic", inputTractionContext(*unit),
			inputTractionWarning(*unit));
		window->setChart(buildInputTractionChart(*unit));
	}
}

void MainWindow::plotTrainUnitTraction(const SceneTrainUnit& unit) {
	QChart* chart = buildInputTractionChart(unit);

	DiagramWindow* win = new DiagramWindow("Input traction characteristic", this);
	win->setTelemetryCapture([this] { return captureTelemetryOperation(); });
	win->setProperty("inputTrainUnitId", QString::fromStdString(unit.id));
	win->setChart(chart);
	win->setPresentation("Input traction characteristic", inputTractionContext(unit), inputTractionWarning(unit));
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

double MainWindow::serviceOccurrenceDuration() const {
	if (const std::optional<double> durationOverride = m_runController->durationOverride())
		return *durationOverride;
	return m_document->model().settings.hasDuration
		? m_document->model().settings.durationSeconds
		: computeHorizon(m_document->model());
}

int MainWindow::totalServiceOccurrences() const {
	return m_document->isLoaded() ? sceneTotalOccurrences(m_document->model(), serviceOccurrenceDuration()) : 0;
}

int MainWindow::inPeriodServiceOccurrences() const {
	return m_document->isLoaded() ? sceneInPeriodOccurrences(m_document->model(), serviceOccurrenceDuration()) : 0;
}

SceneScenario* MainWindow::selectedScenario() {
	return m_document->isLoaded() ? m_runSelection.selectedScenario(m_document->mutableModel()) : nullptr;
}

const SceneScenario* MainWindow::selectedScenario() const {
	return m_document->isLoaded() ? m_runSelection.selectedScenario(m_document->model()) : nullptr;
}

std::vector<SceneIncident>& MainWindow::selectedScenarioIncidents() {
	static std::vector<SceneIncident> empty;
	SceneScenario* scenario = selectedScenario();
	return scenario ? scenario->incidents : empty;
}

const std::vector<SceneIncident>& MainWindow::selectedScenarioIncidents() const {
	static const std::vector<SceneIncident> empty;
	const SceneScenario* scenario = selectedScenario();
	return scenario ? scenario->incidents : empty;
}

QString MainWindow::scenarioContext() const {
	if (!m_runSelection.appliedScenarioId().empty())
		return QString::fromStdString(m_runSelection.appliedScenarioId());
	const SceneScenario* scenario = selectedScenario();
	return scenario ? QString::fromStdString(scenario->id) : QStringLiteral("(none)");
}

void MainWindow::refreshIncidentPanel() {
	m_scenarioPanel->refresh();
	m_incidentPanel->refresh();
	m_entranceDelayPanel->refresh();
}

void MainWindow::updateCaseLayersPanel() {
	if (!m_caseNameLabel)
		return;

	QString caseName = m_document->isLoaded() ? QString::fromStdString(m_document->model().name)
											  : m_runController->settingsCaseName();
	if (caseName.trimmed().isEmpty())
		caseName = QStringLiteral("Canonical scene");
	if (caseName.trimmed().isEmpty())
		caseName = QStringLiteral("Default case study");
	if (m_caseNameLabel)
		m_caseNameLabel->setText(EditorLayout::breakableName(caseName));

	QString readiness = "Ready to run";
	bool blocked = false;
	if (m_document->isLoaded()) {
		for (const SceneDiagnostic& diagnostic : m_session->diagnostics()) {
			if (diagnostic.severity != SceneSeverity::Error)
				continue;
			readiness = QString::fromStdString(diagnostic.message);
			blocked = true;
			break;
		}
	} else {
		readiness = "No canonical scene loaded";
		blocked = true;
	}
	if (m_caseReadinessLabel) {
		m_caseReadinessLabel->setText(readiness);
		m_caseReadinessLabel->setProperty("blocking", blocked);
		m_caseReadinessLabel->style()->unpolish(m_caseReadinessLabel);
		m_caseReadinessLabel->style()->polish(m_caseReadinessLabel);
	}
	updateNetworkLegend();
}

void MainWindow::updateSpeedModeDisplay(int delayMs) {
	if (m_speedLabel)
		m_speedLabel->setText(simulationSpeedLabel(delayMs));
	if (m_speedSlider) {
		m_speedSlider->setToolTip(QString("Simulation speed: %1").arg(simulationSpeedMode(delayMs).toLower()));
	}
}

void MainWindow::refreshFollowTrainChoices() {
	if (!m_followTrainCombo)
		return;

	const int previousTrainIndex = m_followTrainIndex;
	m_updatingFollowCombo = true;
	const QSignalBlocker blocker(m_followTrainCombo);
	m_followTrainCombo->clear();
	m_followRowShown.clear();

	// The data of a row is the train index and the name of the train. The text of the row is
	// the name with a state, which updateFollowAvailability writes. A list without trains has
	// one row that says why.
	if (!m_showingTrackPreview && simulation.run().numTrains > 0) {
		for (int train = 0; train < simulation.run().numTrains; ++train) {
			const QString label = QString::fromStdString(followTrainName(simulation.run().trains[train].trainDescription, train));
			m_followTrainCombo->addItem(label, train);
			m_followTrainCombo->setItemData(train, label, kFollowNameRole);
		}
	} else {
		m_followTrainCombo->addItem(QString(), -1);
	}

	const int comboIndex = qMax(0, m_followTrainCombo->findData(previousTrainIndex));
	m_followTrainCombo->setCurrentIndex(comboIndex);

	if (m_followAction && m_followAction->isChecked()) {
		if (m_followTrainCombo->itemData(comboIndex).toInt() == previousTrainIndex) {
			m_followTrainIndex = previousTrainIndex;
		} else {
			setFollowTrain(-1);
			statusBar()->showMessage("Follow stopped: the selected train is no longer available", 5000);
		}
	} else {
		setFollowTrain(-1);
	}

	m_updatingFollowCombo = false;
	updateViewportOverlays();
	updateFollowAvailability();
}

FollowAvailabilityInput MainWindow::followInput(int row) const {
	FollowAvailabilityInput in;
	in.replay = m_replayActive || !m_completedReplay.empty();
	if (!m_completedReplay.empty())
		in.replayEndTime = m_completedReplay.lastTime();
	in.trainsLayerVisible = m_presenter->m_trainLayerVisible;
	in.clockOffsetSeconds = m_runController->startOffsetSeconds();
	if (m_snapshot)
		in.timestep = m_snapshot->timestep;
	const int train = m_followTrainCombo && row >= 0 ? m_followTrainCombo->itemData(row).toInt() : -1;
	in.hasRun = train >= 0 && !m_showingTrackPreview && simulation.run().numTrains > 0;
	in.sceneHasServices = in.hasRun || !m_document->model().services.empty();
	if (train < 0)
		return in;
	in.name = m_followTrainCombo->itemData(row, kFollowNameRole).toString().toStdString();
	in.followOn = m_followAction && m_followAction->isChecked() && m_followTrainIndex == train;
	in.drawable = trainHasGeometry(train);
	if (m_snapshot) {
		const auto state = std::find_if(m_snapshot->trains.cbegin(), m_snapshot->trains.cend(),
			[train](const GuiTrainState& candidate) { return candidate.index == train; });
		if (state != m_snapshot->trains.cend())
			in.train = &*state;
	}
	return in;
}

FollowAvailability MainWindow::followAvailabilityOf(int trainIndex) const {
	return followAvailability(followInput(m_followTrainCombo ? m_followTrainCombo->findData(trainIndex) : -1));
}

void MainWindow::setStartOffset(long long seconds) {
	m_runController->setStartOffsetSeconds(seconds);
	// The entries of the trains that have not entered show clock times.
	m_followRowShown.clear();
	updateFollowAvailability();
	// The replay row names the first kept time as a clock time.
	if (m_replayBar && m_replayBar->isVisible() && !m_completedReplay.empty())
		writeReplayText(m_replayActive && m_snapshot ? std::optional<int>(m_snapshot->timestep) : std::nullopt);
}

bool MainWindow::trainHasGeometry(int trainIndex) const {
	const auto item = std::find_if(m_presenter->allTrains.cbegin(), m_presenter->allTrains.cend(),
		[trainIndex](const TrainItemGroup* candidate) { return candidate && candidate->index == trainIndex; });
	if (item == m_presenter->allTrains.cend() || !(*item)->trainPolygonItemList)
		return false;
	return std::any_of((*item)->trainPolygonItemList->cbegin(), (*item)->trainPolygonItemList->cend(),
		[](const TrainBodyItem* body) { return body && !body->polygon().isEmpty(); });
}

FollowAvailability MainWindow::updateFollowAvailability() {
	FollowAvailability selected;
	if (!m_followTrainCombo)
		return selected;
	const int rows = m_followTrainCombo->count();
	if (m_followRowShown.size() != rows)
		m_followRowShown = QVector<std::optional<std::pair<FollowPhase, bool>>>(rows);
	const int current = m_followTrainCombo->currentIndex();
	bool written = false;
	for (int row = 0; row < rows; ++row) {
		const FollowAvailabilityInput in = followInput(row);
		const std::pair<FollowPhase, bool> shown(followPhase(in), in.train != nullptr);
		const bool changed = m_followRowShown[row] != shown;
		if (!changed && row != current)
			continue;
		const FollowAvailability availability = followAvailability(in);
		if (row == current)
			selected = availability;
		if (changed) {
			m_followTrainCombo->setItemText(row, QString::fromStdString(availability.entryText));
			m_followRowShown[row] = shown;
			written = true;
		}
	}
	if (written) {
		// The list is as narrow as the toolbar needs, so its popup is as wide as the longest entry,
		// which shows the state and the scheduled time after a long name.
		QAbstractItemView* popup = m_followTrainCombo->view();
		int widest = popup->sizeHintForColumn(0);
		for (int row = 0; row < rows; ++row)
			widest = qMax(widest, popup->fontMetrics().horizontalAdvance(m_followTrainCombo->itemText(row)));
		popup->setMinimumWidth(widest + 2 * popup->frameWidth() + popup->verticalScrollBar()->sizeHint().width());
	}

	const QString status = QString::fromStdString(selected.statusText);
	if (m_followStatusLabel && m_followStatusLabel->text() != status) {
		m_followStatusLabel->setText(status);
		m_followStatusLabel->setToolTip(status);
	}
	if (m_followAction) {
		m_followAction->setEnabled(selected.controlEnabled);
		if (m_followAction->toolTip() != status)
			m_followAction->setToolTip(status);
	}
	if (m_followTrainCombo->toolTip() != status)
		m_followTrainCombo->setToolTip(status);
	QWidget* followButton = m_followAction && m_toolBar ? m_toolBar->widgetForAction(m_followAction) : nullptr;
	for (QWidget* control : {followButton, static_cast<QWidget*>(m_followTrainCombo)}) {
		if (control && control->accessibleDescription() != status)
			control->setAccessibleDescription(status);
	}
	return selected;
}

TrainItemGroup* MainWindow::resolveTrainItem(int trainIndex) const {
	if (!scene)
		return nullptr;
	for (auto* item : scene->items()) {
		auto* train = qgraphicsitem_cast<TrainItemGroup*>(item);
		if (train && train->scene() == scene && train->index == trainIndex)
			return train;
	}
	return nullptr;
}

bool MainWindow::replayTrainHasPosition(int trainIndex) const {
	if (!m_snapshot) return false;
	const auto state = std::find_if(m_snapshot->trains.cbegin(), m_snapshot->trains.cend(),
		[trainIndex](const GuiTrainState& train) { return train.index == trainIndex; });
	return state != m_snapshot->trains.cend()
		&& guiReplayTrainHasPosition(*state, m_snapshot->timestep);
}

TrainBodyItem* MainWindow::resolveTrainBodyItem(int trainIndex) const {
	if (!scene)
		return nullptr;
	for (auto* item : scene->items()) {
		auto* body = qgraphicsitem_cast<TrainBodyItem*>(item);
		if (!body || body->scene() != scene)
			continue;
		auto* group = qgraphicsitem_cast<TrainItemGroup*>(body->parentItem());
		if (group && group->scene() == scene && group->index == trainIndex)
			return body;
	}
	return nullptr;
}

StationNodeItem* MainWindow::resolveStationNodeItem(double nodeId, int track) const {
	if (!scene)
		return nullptr;
	for (auto* item : scene->items()) {
		auto* station = qgraphicsitem_cast<StationNodeItem*>(item);
		if (station && station->scene() == scene && station->track == track && station->node
			&& station->node->ID == nodeId)
			return station;
	}
	return nullptr;
}

TrackLineItem* MainWindow::resolveArcItem(double arcId, int track) const {
	if (!scene)
		return nullptr;
	for (auto* item : scene->items()) {
		auto* arc = qgraphicsitem_cast<TrackLineItem*>(item);
		if (arc && arc->scene() == scene && arc->track == track && arc->arc && arc->arc->ID == arcId)
			return arc;
	}
	return nullptr;
}

SignalItem* MainWindow::resolveSignalItem(int track, double position, bool reversed) const {
	if (!scene)
		return nullptr;
	for (auto* item : scene->items()) {
		auto* signal = qgraphicsitem_cast<SignalItem*>(item);
		if (signal && signal->scene() == scene && signal->trackID == track && signal->X == position
			&& signal->reversedDirection == reversed)
			return signal;
	}
	return nullptr;
}

PassengerItem* MainWindow::resolvePassengerItem(const std::string& passengerId) const {
	if (!scene)
		return nullptr;
	for (auto* item : scene->items()) {
		auto* passenger = qgraphicsitem_cast<PassengerItem*>(item);
		if (passenger && passenger->scene() == scene && passenger->passengerId == passengerId)
			return passenger;
	}
	return nullptr;
}

void MainWindow::centerSceneItem(QGraphicsItem* item) {
	if (networkView && item && item->scene() == scene)
		networkView->centerOn(item->sceneBoundingRect().center());
}

void MainWindow::cutToFollowedTrain(const FollowAvailability& following) {
	const TrainItemGroup* item = following.canAct ? resolveTrainItem(m_followTrainIndex) : nullptr;
	if (item)
		m_followCamera->follow(item->sceneBoundingRect().center(), true);
	else
		m_followCamera->stop();
}

void MainWindow::setFollowTrain(int trainIndex) {
	const auto synchronizeButton = [this]() {
		if (m_toolBar && m_followAction) {
			if (auto* button = qobject_cast<QToolButton*>(m_toolBar->widgetForAction(m_followAction))) {
				const QSignalBlocker blocker(button);
				button->setChecked(m_followAction->isChecked());
			}
		}
	};
	if (trainIndex < 0) {
		const bool wasFollowing = m_followTrainIndex >= 0
			|| (m_followAction && m_followAction->isChecked());
		if (m_followAction && m_followAction->isChecked()) {
			const QSignalBlocker blocker(m_followAction);
			m_followAction->setChecked(false);
		}
		m_followTrainIndex = -1;
		m_followCamera->stop();
		synchronizeButton();
		updateViewportOverlays();
		updateFollowAvailability();
		if (wasFollowing)
			statusBar()->showMessage("Follow disabled", 3000);
		return;
	}

	const int comboIndex = m_followTrainCombo ? m_followTrainCombo->findData(trainIndex) : -1;
	if (trainIndex >= simulation.run().numTrains || (m_followTrainCombo && comboIndex < 0)) {
		setFollowTrain(-1);
		statusBar()->showMessage("Cannot follow the selected train: it is no longer available", 5000);
		return;
	}

	const FollowAvailability offer = followAvailability(followInput(comboIndex));
	if (!offer.canArm) {
		setFollowTrain(-1);
		statusBar()->showMessage(QString::fromStdString(offer.statusText), 5000);
		return;
	}

	if (m_followTrainCombo && comboIndex >= 0) {
		const QSignalBlocker blocker(m_followTrainCombo);
		m_followTrainCombo->setCurrentIndex(comboIndex);
	}
	if (m_followAction && !m_followAction->isChecked()) {
		const QSignalBlocker blocker(m_followAction);
		m_followAction->setChecked(true);
	}
	m_followTrainIndex = trainIndex;
	synchronizeButton();
	updateViewportOverlays();

	const FollowAvailability following = updateFollowAvailability();
	cutToFollowedTrain(following);
	statusBar()->showMessage(QString::fromStdString(following.statusText), 5000);
}

// The menus are shown with popup() and delete themselves when they close, so this function returns while a menu is open.
// The scripted GUI reads and triggers the open menu through m_sceneContextMenu; exec() on a stack menu would block it.
void MainWindow::showSceneContextMenu(QGraphicsItem* item, const QPointF& /*scenePos*/, const QPoint& screenPos, bool /*keyboard*/) {

	if (m_sceneContextMenu) {
		m_sceneContextMenu->close();
		m_sceneContextMenu.clear();
	}
	if (item && item->data(PreviewGraphics::Kind).isValid()) {
		const QString kind = item->data(PreviewGraphics::Kind).toString();
		if (kind == "node" || kind == "connection")
			return; // No new node/connection menus.
		const QString id = item->data(PreviewGraphics::Id).toString();
		const QString track = item->data(PreviewGraphics::TrackId).toString();
		const bool reversed = item->data(PreviewGraphics::Reversed).toBool();
		const quint64 revision = m_document->revision();
		QMenu* menu = new QMenu(this);
		menu->setAttribute(Qt::WA_DeleteOnClose);
		menu->setTitle("Preview: " + id);
		const auto resolve = [this, kind, id, track, reversed, revision]() {
			return m_inspector->resolvePreviewItem(kind, id, track, reversed, revision);
		};
		connect(menu->addAction("Show details"), &QAction::triggered, this, [this, resolve]() {
			if (auto* current = resolve()) m_inspector->displayPreviewInfo(current);
		});
		connect(menu->addAction("Center in view"), &QAction::triggered, this, [this, resolve]() {
			if (auto* current = resolve()) centerSceneItem(current);
		});
		connect(menu->addAction("Copy canonical ID"), &QAction::triggered, this, [resolve, id, track, reversed]() {
			if (resolve() && QApplication::clipboard())
				QApplication::clipboard()->setText(track.isEmpty() ? id
																   : QString("track %1; section %2; %3").arg(track, id, reversed ? "reverse" : "forward"));
		});
		m_sceneContextMenu = menu;
		menu->popup(screenPos);
		return;
	}
	if (qgraphicsitem_cast<NodeItem*>(item) || qgraphicsitem_cast<ConnectionItem*>(item))
		return;

	QMenu* menu = new QMenu(this);
	menu->setAttribute(Qt::WA_DeleteOnClose);
	menu->setToolTipsVisible(true);
	auto addDeferred = [menu](const QString& text, const QString& explanation) {
		QAction* action = menu->addAction(text);
		action->setEnabled(false);
		action->setToolTip(explanation);
		action->setStatusTip(explanation);
		return action;
	};
	auto openMenu = [this, menu, screenPos]() {
		m_sceneContextMenu = menu;
		menu->popup(screenPos);
	};

	if (auto* trainBody = qgraphicsitem_cast<TrainBodyItem*>(item)) {
		auto* group = qgraphicsitem_cast<TrainItemGroup*>(trainBody->parentItem());
		if (!group || group->scene() != scene) {
			delete menu;
			return;
		}
		const int trainIndex = group->index;
		menu->setTitle("Train");
		QAction* details = menu->addAction("Show details");
		details->setIcon(QIcon(classifyTrainType(group->trainType, group->trainDescription).iconResource));
		connect(details, &QAction::triggered, this, [this, trainIndex]() {
			if (auto* body = resolveTrainBodyItem(trainIndex))
				m_inspector->displayTrainDetails(body, false);
		});
		QAction* center = menu->addAction("Center in view");
		connect(center, &QAction::triggered, this, [this, trainIndex]() {
			if (auto* current = resolveTrainItem(trainIndex))
				centerSceneItem(current);
		});
		QAction* follow = menu->addAction("Follow train");
		connect(follow, &QAction::triggered, this, [this, trainIndex]() {
			if (resolveTrainItem(trainIndex))
				setFollowTrain(trainIndex);
		});
		QAction* copy = menu->addAction("Copy train index");
		connect(copy, &QAction::triggered, this, [this, trainIndex]() {
			if (resolveTrainItem(trainIndex) && QApplication::clipboard())
				QApplication::clipboard()->setText(QString::number(trainIndex));
		});
		menu->addSeparator();
		addDeferred("Planned route and related infrastructure",
			"Requires the train-to-route association and an infrastructure query.");
		openMenu();
		return;
	}

	if (auto* station = qgraphicsitem_cast<StationNodeItem*>(item)) {
		if (!station->node || station->scene() != scene) {
			delete menu;
			return;
		}
		const double nodeId = station->node->ID;
		const int track = station->track;
		menu->setTitle("Station");
		QAction* details = menu->addAction("Show details");
		details->setIcon(QIcon(":/icons/station-dark.svg"));
		connect(details, &QAction::triggered, this, [this, nodeId, track]() {
			if (auto* current = resolveStationNodeItem(nodeId, track))
				m_inspector->displayStationNodeInfo(current);
		});
		QAction* center = menu->addAction("Center in view");
		connect(center, &QAction::triggered, this, [this, nodeId, track]() {
			if (auto* current = resolveStationNodeItem(nodeId, track))
				centerSceneItem(current);
		});
		QAction* copy = menu->addAction("Copy node ID");
		connect(copy, &QAction::triggered, this, [this, nodeId, track]() {
			if (resolveStationNodeItem(nodeId, track) && QApplication::clipboard())
				QApplication::clipboard()->setText(QString::number(nodeId, 'g',
					std::numeric_limits<double>::max_digits10));
		});
		menu->addSeparator();
		addDeferred("Filtered station arrivals and departures",
			"Requires the station timetable association and an arrivals/departures query.");
		openMenu();
		return;
	}

	if (auto* arc = qgraphicsitem_cast<TrackLineItem*>(item)) {
		if (!arc->arc || arc->scene() != scene) {
			delete menu;
			return;
		}
		const double arcId = arc->arc->ID;
		const int track = arc->track;
		menu->setTitle("Track");
		QAction* details = menu->addAction("Show details");
		connect(details, &QAction::triggered, this, [this, arcId, track]() {
			if (auto* current = resolveArcItem(arcId, track))
				m_inspector->displayArcInfo(current);
		});
		QAction* center = menu->addAction("Center in view");
		connect(center, &QAction::triggered, this, [this, arcId, track]() {
			if (auto* current = resolveArcItem(arcId, track))
				centerSceneItem(current);
		});
		QAction* copy = menu->addAction("Copy arc ID");
		connect(copy, &QAction::triggered, this, [this, arcId, track]() {
			if (resolveArcItem(arcId, track) && QApplication::clipboard())
				QApplication::clipboard()->setText(QString::number(arcId, 'g',
					std::numeric_limits<double>::max_digits10));
		});
		menu->addSeparator();
		addDeferred("Trains currently using this track",
			"Requires the track-occupancy association and an active-train query.");
		addDeferred("Incidents affecting this track",
			"Requires the track-incident association and an incident query.");
		openMenu();
		return;
	}

	if (auto* signal = qgraphicsitem_cast<SignalItem*>(item)) {
		if (signal->scene() != scene) {
			delete menu;
			return;
		}
		const int track = signal->trackID;
		const double position = signal->X;
		const bool reversed = signal->reversedDirection;
		menu->setTitle("Signal");
		QAction* details = menu->addAction("Show details");
		details->setIcon(QIcon(classifySignalAspect(signal->failed() ? 0 : signal->aspectCode()).iconResource));
		connect(details, &QAction::triggered, this, [this, track, position, reversed]() {
			if (auto* current = resolveSignalItem(track, position, reversed))
				m_inspector->displaySignallingInfo(current);
		});
		QAction* center = menu->addAction("Center in view");
		connect(center, &QAction::triggered, this, [this, track, position, reversed]() {
			if (auto* current = resolveSignalItem(track, position, reversed))
				centerSceneItem(current);
		});
		QAction* copy = menu->addAction("Copy signal location");
		connect(copy, &QAction::triggered, this, [this, track, position, reversed]() {
			if (resolveSignalItem(track, position, reversed) && QApplication::clipboard())
				QApplication::clipboard()->setText(QString("track %1 @ %2 (%3)")
						.arg(track)
						.arg(position, 0, 'g', std::numeric_limits<double>::max_digits10)
						.arg(reversed ? "reverse" : "forward"));
		});
		menu->addSeparator();
		addDeferred("Next train approaching this signal",
			"Requires the signal route association and an approaching-train query.");
		openMenu();
		return;
	}

	if (auto* passenger = qgraphicsitem_cast<PassengerItem*>(item)) {
		if (passenger->scene() != scene) {
			delete menu;
			return;
		}
		const std::string passengerId = passenger->passengerId;
		menu->setTitle("Passenger");
		QAction* details = menu->addAction("Show details");
		details->setIcon(QIcon(":/icons/passenger.svg"));
		connect(details, &QAction::triggered, this, [this, passengerId]() {
			if (auto* current = resolvePassengerItem(passengerId))
				m_inspector->displayPassengerInfo(current);
		});
		QAction* center = menu->addAction("Center in view");
		connect(center, &QAction::triggered, this, [this, passengerId]() {
			if (auto* current = resolvePassengerItem(passengerId))
				centerSceneItem(current);
		});
		QAction* copy = menu->addAction("Copy passenger ID");
		connect(copy, &QAction::triggered, this, [this, passengerId]() {
			if (resolvePassengerItem(passengerId) && QApplication::clipboard())
				QApplication::clipboard()->setText(QString::fromStdString(passengerId));
		});
		openMenu();
		return;
	}

	if (item)
		return;
	menu->setTitle("Network");
	QAction* fit = menu->addAction("Fit whole network");
	connect(fit, &QAction::triggered, this, &MainWindow::fitView);
	QAction* clear = menu->addAction("Clear selection");
	connect(clear, &QAction::triggered, this, [this]() {
		if (scene)
			scene->clearSelection();
		m_inspector->handleDisableHighlight();
	});
	QAction* stop = menu->addAction("Stop following train");
	connect(stop, &QAction::triggered, this, [this]() { setFollowTrain(-1); });
	openMenu();
}

void MainWindow::clearReplay() {
	if (m_replayTimer) m_replayTimer->stop();
	if (m_replayPlayButton) m_replayPlayButton->setText("Play");
	if (m_replayBar) m_replayBar->hide();
	m_replayActive = false;
	if (m_replaySlider) m_replaySlider->setEnabled(true);
	if (m_replayPlayButton) m_replayPlayButton->setEnabled(true);
	m_completedReplay.clear();
	simulation.resetReplayCandidate();
}

// Writes the texts of the replay row and the tooltip of Start for the history of the run, with the
// time of the displayed frame when there is one. It returns whether the slider and Play can be used.
bool MainWindow::writeReplayText(std::optional<int> shownTime) {
	const ReplayBarText bar = replayBarText(replayCoverageOf(m_completedReplay, m_runController->startOffsetSeconds()), shownTime);
	m_replayLabel->setText(QString::fromStdString(bar.selected));
	m_replayCoverageLabel->setText(QString::fromStdString(bar.coverage));
	m_replayStartButton->setToolTip(QString::fromStdString(bar.startTip));
	return bar.usable;
}

// Writes the replay row for the history of the finished run: the interval of the slider, the
// texts, the tooltip of Start and whether the slider and Play can be used. A history without
// frames gives an empty slider.
void MainWindow::showReplayBar() {
	m_replayRequestedTime = m_completedReplay.lastTime();
	{
		const QSignalBlocker blocker(m_replaySlider);
		m_replaySlider->setRange(m_completedReplay.firstTime(), m_completedReplay.lastTime());
		m_replaySlider->setValue(m_replayRequestedTime);
	}
	const bool usable = writeReplayText(std::nullopt);
	m_replaySlider->setEnabled(usable);
	m_replayPlayButton->setEnabled(usable);
	m_replayBar->show();
}

// setup GUI
void MainWindow::setupGUI() {
	m_showingTrackPreview = false;

	const bool hasSharedPreview = !m_presenter->m_cachedTrackPreview.lines.empty();
	m_presenter->showRuntime(m_document->model());

	bindStationOverlaySources();
	if (!hasSharedPreview)
		updateStationOverlayDegrees();
	updateViewportOverlays();

	refreshFollowTrainChoices();
}

bool MainWindow::startupTimingIdentityMatches(const QString& path, const SceneModel& model,
	const std::string* inputSnapshot) const {
	const StartupIdentity current = startupIdentity(
		path, model, inputSnapshot ? *inputSnapshot : std::string());
	return sameStartupIdentity(g_startupIdentity, current)
		&& (!inputSnapshot || g_startupIdentity.inputSnapshot == *inputSnapshot);
}

void MainWindow::failStartupTiming(const QString& message) {
	std::fprintf(stderr, "QEGTRAIN_TIMING_ERROR %s\n", message.toUtf8().constData());
	std::fflush(stderr);
	QCoreApplication::exit(2);
}

void MainWindow::handleStartupTimingPaint(const QString& kind, int generation,
	qint64 elapsedNanoseconds) {
	if (!startupTimingEnabled() || generation != m_startupTimingIteration)
		return;
	const bool identityOk = startupTimingIdentityMatches(m_session->sceneDir(), m_document->model());
	recordStartupTiming(kind == QLatin1String("preview")
			? QStringLiteral("first_preview_paint")
			: QStringLiteral("first_runtime_paint"),
		m_startupTimingIteration, generation, elapsedNanoseconds, kind,
		QStringLiteral("NetworkView::viewportEvent"), identityOk);
	if (!identityOk) {
		failStartupTiming(QStringLiteral("scene identity or counts changed before first paint"));
		return;
	}
	if (kind == QLatin1String("preview")) {
		QTimer::singleShot(0, m_runController, &RunController::runScene);
		return;
	}
	if (kind != QLatin1String("runtime")) {
		failStartupTiming(QStringLiteral("unexpected paint generation kind"));
		return;
	}
	if (m_startupTimingIteration >= m_startupTimingWarmTrials) {
		QCoreApplication::exit(0);
		return;
	}
	++m_startupTimingIteration;
	QTimer::singleShot(0, this, [this]() {
		if (!openSceneDirectory(m_startupTimingScenePath))
			failStartupTiming(QStringLiteral("warm scene reload failed"));
	});
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
	if (mainWindowTestHooks().suppressStartupPrompts) {
		if (const auto& afterFirstShow = mainWindowTestHooks().afterFirstShow)
			afterFirstShow(*this);
		return;
	}
	// A file that the system opens arrives from the relay that main created, from a second process or,
	// on macOS, as an open event. It takes the steps of File > Open Case Study. A modal dialog, such as
	// the first-run questions or the question about unsaved changes, holds it back until the dialog has
	// closed.
	auto* relay = findChild<OpenRequestRelay*>(QString(), Qt::FindDirectChildrenOnly);
	if (relay) {
		connect(relay, &OpenRequestRelay::openRequested, this, [this, relay](const QString& request) {
			// A chooser that is open gives way to the request.
			QDialog* chooser = findChild<QDialog*>(QStringLiteral("caseChooserDialog"));
			if (chooser)
				chooser->reject();
			if (chooser || QApplication::activeModalWidget()) {
				relay->postpone(request);
				return;
			}
			bringToFront();
			const SceneOpenChoice choice = classifyOpenPaths({request});
			if (!choice.rejection.isEmpty()) {
				statusBar()->showMessage(QStringLiteral("Open request rejected: %1. Scene unchanged.").arg(choice.rejection), 8000);
				return;
			}
			// The request names the case, so a startup chooser that has not opened yet stays closed.
			m_runController->markCaseNamed();
			m_session->requestOpenScene(choice.path);
		});
	}
	// Keep update consent behind the startup chooser. Scripted launches have no
	// chooser, so the same queued callback runs after the initial window shows.
	const auto startup = [this, relay]() {
		const bool recovered = offerRecovery();
		if (!recovered && !m_runController->caseNamedAtStartup() && !(relay && relay->hasPendingRequest()))
			showStartupChooser();
		m_updateTelemetry->showPrivacySettings(true);
		m_updateTelemetry->startTelemetryAfterInitialConsent();
		reportInstallerUpdate(this);
		reportRolledBackUpdate(this);
		m_updateTelemetry->maybePromptForUpdateChecks();
	};
#ifdef Q_OS_MACOS
	// Finder hands over the file that started the program as an open event, and that event reaches
	// the application only after this callback has run. A chooser that is open by then holds it back
	// until the user closes it, so the chooser waits for the event loop to deliver the event first.
	QTimer::singleShot(0, this, [this, relay, startup]() {
		constexpr int kOpenEventWaitMs = 300;
		if (relay && !m_runController->caseNamedAtStartup() && !relay->hasPendingRequest())
			QTimer::singleShot(kOpenEventWaitMs, this, startup);
		else
			startup();
	});
#else
	QTimer::singleShot(0, this, startup);
#endif

	// The playback profiler starts the run itself.
	if (PlaybackProfiler::enabled()) {
		if (PlaybackProfiler::startupTimingConflict()) {
			std::fprintf(stderr, "QEGTRAIN_PLAYBACK_PROFILE {\"type\":\"error\",\"reason\":\"startup_timing_enabled\"}\n");
			QCoreApplication::exit(2);
			return;
		}
		m_speedSlider->setValue(kMaxStepDelayMs
			- qEnvironmentVariableIntValue("QEGTRAIN_PLAYBACK_PROFILE_DELAY_MS"));
		QTimer::singleShot(1500, m_runController, &RunController::runCurrent);
	}
	if (const auto& afterFirstShow = mainWindowTestHooks().afterFirstShow)
		afterFirstShow(*this);
}

// Raw trajectories can survive a scene edit; the explicit availability flag is
// the ownership boundary for result actions.
bool MainWindow::hasRunResults() const {
	return m_runController->hasRunResults();
}

// Enable the diagram entries only while run results exist; a disabled entry
// explains itself instead of answering with a modal error box.
void MainWindow::updateDiagramActions() {
	const bool available = hasRunResults();
	const QString hint = available ? QString() : QString("Run a simulation to enable this diagram");
	if (m_diagramsMenu) {
		for (QAction* action : m_diagramsMenu->actions()) {
			action->setEnabled(available);
			action->setStatusTip(hint);
			action->setToolTip(hint);
		}
	}
	if (ui->displayTrainPathDiagrams) {
		ui->displayTrainPathDiagrams->setEnabled(available);
		ui->displayTrainPathDiagrams->setStatusTip(hint);
	}
	if (m_runResultsDock) {
		if (auto* viewsButton = m_runResultsDock->findChild<QToolButton*>("runResultsViewsButton"))
			viewsButton->setEnabled(available);
		for (QPushButton* button : m_runResultsDock->findChildren<QPushButton*>()) {
			if (button->objectName().startsWith("resultView_"))
				button->setEnabled(available);
		}
		for (QAction* action : m_runResultsDock->findChildren<QAction*>())
			if (action->objectName().startsWith("resultView_"))
				action->setEnabled(available);
	}
	const RunController::DelayBaselineState delay = m_runController->delayBaselineState(available);
	if (m_setDelayBaselineButton) {
		m_setDelayBaselineButton->setEnabled(delay.canSet);
		m_setDelayBaselineButton->setToolTip(delay.canSet
				? QStringLiteral("Freeze this completed run with no incidents or entrance delays as the delay baseline")
				: QStringLiteral("Complete a run with no incidents or entrance delays before setting a delay baseline"));
	}
	if (m_compareDelayButton) {
		m_compareDelayButton->setEnabled(delay.canCompare);
		m_compareDelayButton->setToolTip(delay.canCompare
				? QStringLiteral("Compare the completed run with the frozen delay baseline")
				: QStringLiteral("Run a completed scenario with incidents or entrance delays after setting a baseline"));
	}
	if (m_delayFeedbackLabel)
		m_delayFeedbackLabel->setText(delay.feedback);
}

// Lazily created so the menu only appears once an editor dock registers.
QMenu* MainWindow::editorsMenu() {
	if (!m_editorsMenu) {
		m_editorsMenu = new QMenu("Editors", this);
		menuBar()->insertMenu(ui->menuSimulation->menuAction(), m_editorsMenu);
	}
	return m_editorsMenu;
}

// First-launch case chooser: bundled scenes and recents by name, with the
// generic open and legacy import flows as buttons. Replaces the bare
// "Select Legacy Case Folder" dialog that used to open over the blank canvas.
void MainWindow::showStartupChooser() {
	const QString currentCaseName = m_document->isLoaded()
		? QString::fromStdString(m_document->model().name)
		: QString();

	// Bundled scenes: below the working directory, next to the executable (Windows
	// and Linux builds), in the macOS bundle, or in a Linux install prefix.
	const QString applicationDir = QCoreApplication::applicationDirPath();
	const QStringList sceneRoots = {
		QDir::currentPath() + "/Scenes",
		applicationDir + "/Scenes",
		applicationDir + "/../Resources/Scenes",
		applicationDir + "/../share/EGTRAIN/Scenes"};
	const QStringList recentPaths = SceneSession::recentScenes();
	CaseChooserDialog dialog({currentCaseName, sceneRoots, recentPaths}, this);
	if (const auto& dialogShown = mainWindowTestHooks().dialogShown)
		dialogShown(dialog, "startupChooser");
	dialog.exec();

	const CaseChooserDialog::Outcome outcome = dialog.outcome();
	switch (outcome) {
		case CaseChooserDialog::Outcome::OpenSelected:
			if (!dialog.chosenPath().isEmpty()) m_session->requestOpenScene(dialog.chosenPath());
			break;
		case CaseChooserDialog::Outcome::BrowseFolder: m_session->openSceneFolderDialog(); break;
		case CaseChooserDialog::Outcome::ImportLegacy: m_session->actionLoad_Network(); break;
		case CaseChooserDialog::Outcome::NewCase: m_session->newScene(); break;
		case CaseChooserDialog::Outcome::Continue:
			if (!currentCaseName.isEmpty())
				statusBar()->showMessage(QString("Ready - %1").arg(currentCaseName));
			break;
	}
	if (const auto& dialogClosed = mainWindowTestHooks().dialogClosed)
		dialogClosed(dialog, "startupChooser");
}

void MainWindow::showDelayAnalysis() {
	m_runController->openDelayAnalysis();
}

void MainWindow::teardownGUI() {
	m_inspector->clearPreviewInspection();
	m_showingTrackPreview = true;
	// Stop any running simulation before clearing scene objects it may reference.
	m_runController->clearSimulationWorker(true);
	clearReplay();
	progressBar->hide();

	// The presenter stops the movement of the trains, deletes all items of the scene and drops the pointers to them.
	m_presenter->clear();
	m_inspector->clearSelectedStation();
	if (m_networkLegendWidget)
		m_networkLegendWidget->setCaseContent(NetworkLegendContent());
	m_infrastructurePanel->setSelectionId(QString());

	setFollowTrain(-1);
	m_inspector->clearSelectedTrain();

	m_snapshot.reset();
	refreshFollowTrainChoices();
	m_presenter->resetPreviewFit();

	// clear() keeps the explicitly pinned scene rect, so fitView would keep
	// fitting the previous network's extents; unset it after invalidating the
	// overlay caches because this emits viewportChanged().
	scene->setSceneRect(QRectF());
	if (networkView)
		networkView->fitToTopology();
}

// setup qdockwidget showing info when clicking items
void MainWindow::setupRunResultsDock() {
	m_runResultsDock = new QDockWidget("Run Results", this);
	m_runResultsDock->setObjectName("runResultsDock");
	m_runResultsDock->setAllowedAreas(Qt::AllDockWidgetAreas);
	m_runResultsTable = new QTableWidget(m_runResultsDock);
	m_runResultsTable->setObjectName("runResultsTable");
	m_runResultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_runResultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_runResultsTable->setColumnCount(11);
	m_runResultsTable->setHorizontalHeaderLabels({"Train", "Operating code", "Performance (%)", "Maximum speed (km/h)", "Start time (hh:mm:ss)", "End time (hh:mm:ss)", "Travel time (h:mm:ss)",
		"Energy (kWh)", "Energy with regen (kWh)",
		"Substation (kWh)", "Substation with regen (kWh)"});
	m_runResultsTable->setAlternatingRowColors(true);
	m_runResultsTable->horizontalHeader()->setStretchLastSection(true);

	QWidget* container = new QWidget(m_runResultsDock);
	QVBoxLayout* containerLayout = new QVBoxLayout(container);
	containerLayout->setContentsMargins(0, 0, 0, 0);
	m_runResultsSummaryLabel = EditorLayout::wrappedLabel(QString(), container);
	m_runResultsSummaryLabel->setObjectName("runResultsContextLabel");
	containerLayout->addWidget(m_runResultsSummaryLabel);
	auto* resultsCaption = new QLabel(container);
	resultsCaption->setObjectName("runResultsViewsCaption");
	DialogLayout::setHelp(resultsCaption, "Results");
	containerLayout->addWidget(resultsCaption);
	QHBoxLayout* resultViews = new QHBoxLayout();
	auto* diagramsButton = new QToolButton(container);
	diagramsButton->setText("Open result view...");
	diagramsButton->setObjectName("runResultsViewsButton");
	diagramsButton->setPopupMode(QToolButton::InstantPopup);
	QMenu* resultMenu = new QMenu(diagramsButton);
	diagramsButton->setMenu(resultMenu);
	const auto addResultViewAction = [this, resultMenu](const QString& text, auto slot) {
		QAction* action = resultMenu->addAction(text);
		action->setObjectName(QString("resultView_%1").arg(text).remove(' '));
		connect(action, &QAction::triggered, this, slot);
	};
	addResultViewAction("Timetable", &MainWindow::showTimetableTable);
	addResultViewAction("Timetable graph (planned vs simulated stops)", &MainWindow::showTimetableGraph);
	addResultViewAction("Delays", &MainWindow::showDelayDiagram);
	addResultViewAction("Delay analysis", &MainWindow::showDelayAnalysis);
	resultMenu->addSeparator();
	addResultViewAction("Speed / distance", &MainWindow::showSpeedDistanceDiagram);
	addResultViewAction("Speed / time", &MainWindow::showSpeedTimeDiagram);
	addResultViewAction("Time / distance", &MainWindow::showTimeDistanceDiagram);
	addResultViewAction("Tractive effort / distance", &MainWindow::showTractiveEffortDistanceDiagram);
	resultMenu->addSeparator();
	addResultViewAction("Train paths (simulated movement)", &MainWindow::displayTrainPathDiagrams);
	addResultViewAction("Blocking time", &MainWindow::showBlockingTimeDiagram);
	addResultViewAction("Capacity", &MainWindow::showCapacityAnalysis);
	addResultViewAction("Capacity by area", &MainWindow::showAreaCapacity);
	resultViews->addWidget(diagramsButton);
	QPushButton* exportCsvBtn = new QPushButton("Export CSV...", container);
	exportCsvBtn->setObjectName("resultView_ExportCSV");
	exportCsvBtn->setToolTip("Write travel time and energy per train to a CSV file");
	connect(exportCsvBtn, &QPushButton::clicked, this, [this]() {
		const auto operation = captureTelemetryOperation();
		const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
		saveCsvInteractive(
			this, "run_summary.csv", operation,
			[run] { return run ? buildRunSummaryCsv(run->results) : std::string(); },
			[run](const QString& path, const std::string& bytes) {
				return writeRunArtifactWithProvenance(path.toStdString(), "csv", bytes, run ? run->provenance : RunProvenance());
			});
	});
	QPushButton* exportPngBtn = new QPushButton("Export PNG...", container);
	exportPngBtn->setObjectName("resultView_ExportPNG");
	exportPngBtn->setToolTip("Save the table as an image");
	connect(exportPngBtn, &QPushButton::clicked, this, [this]() {
		const auto operation = captureTelemetryOperation();
		QString path = QFileDialog::getSaveFileName(this, "Export Table", dialog_folders::suggestedPath("run_summary.png"), "PNG Image (*.png)");
		if (path.isEmpty())
			return;
		if (QFileInfo(path).suffix().compare("png", Qt::CaseInsensitive) != 0)
			path += ".png";
		QByteArray data;
		QBuffer buffer(&data);
		if (!buffer.open(QIODevice::WriteOnly) || !fullTableImage(*m_runResultsTable).save(&buffer, "PNG")) {
			operation.failure(telemetry::Operation::Export, telemetry::Error::InternalFailure);
			QMessageBox::warning(this, "Export failed", path_message::writeFailed("write the image to", path));
			return;
		}
		const std::string bytes(data.constData(), static_cast<std::size_t>(data.size()));
		const bool written = writeRunArtifactWithProvenance(path.toStdString(), "png", bytes,
			m_runController->completedRun() ? m_runController->completedRun()->provenance : RunProvenance());
		operation.exportFinished(telemetry::ExportKind::Png, written, true, telemetry::Error::IoFailure);
		if (!written)
			QMessageBox::warning(this, "Export failed", path_message::writeFailed("export the image and provenance to", path));
	});
	resultViews->addWidget(exportCsvBtn);
	resultViews->addWidget(exportPngBtn);
	resultViews->addStretch();
	containerLayout->addLayout(resultViews);
	QHBoxLayout* delayRow = new QHBoxLayout();
	auto* delayCaption = new QLabel(container);
	delayCaption->setObjectName("delayComparisonCaption");
	DialogLayout::setHelp(delayCaption, "Delay comparison");
	containerLayout->addWidget(delayCaption);
	m_delayFeedbackLabel = EditorLayout::wrappedLabel(QString(), container);
	m_delayFeedbackLabel->setObjectName("delayFeedbackLabel");
	containerLayout->addWidget(m_delayFeedbackLabel);
	m_setDelayBaselineButton = new QPushButton("Set delay baseline", container);
	m_setDelayBaselineButton->setObjectName("setDelayBaselineButton");
	m_setDelayBaselineButton->setToolTip("Freeze this completed run with no incidents or entrance delays as the delay baseline");
	connect(m_setDelayBaselineButton, &QPushButton::clicked, m_runController, &RunController::setDelayBaseline);
	delayRow->addWidget(m_setDelayBaselineButton);
	m_compareDelayButton = new QPushButton("Compare delays", container);
	m_compareDelayButton->setObjectName("compareDelayButton");
	m_compareDelayButton->setToolTip("Compare the completed run with the frozen delay baseline");
	connect(m_compareDelayButton, &QPushButton::clicked, m_runController, &RunController::showDelayComparison);
	delayRow->addWidget(m_compareDelayButton);
	delayRow->addStretch();
	containerLayout->addLayout(delayRow);
	m_runResultsTable->setMinimumSize(0, 0);
	containerLayout->addWidget(m_runResultsTable, 1);
	m_runResultsDock->setWidget(container);
	addDockWidget(Qt::BottomDockWidgetArea, m_runResultsDock);
	m_runResultsDock->hide();
}

void MainWindow::refreshRunResults() {
	if (!m_runResultsDock || !m_runResultsTable || !m_runController->completedRun() || m_runController->completedRun()->results.trains.empty())
		return;
	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	refreshRunResultsSummary();
	m_runResultsDock->setWindowTitle(QStringLiteral("Run Results \u2014 %1").arg(completedRunContext(run->provenance)));

	const RunResults& results = run->results;
	const int totalColumns = 11;
	m_runResultsTable->clear();
	m_runResultsTable->setColumnCount(totalColumns);
	m_runResultsTable->setHorizontalHeaderLabels(
		{"Train", "Operating code", "Performance (%)", "Maximum speed (km/h)", "Start time (hh:mm:ss)", "End time (hh:mm:ss)",
			"Travel time (h:mm:ss)", "Energy consumed (kWh)", "Energy consumed with regenerative braking (kWh)",
			"Substation request (kWh)", "Substation request with regenerative braking (kWh)"});
	m_runResultsTable->setRowCount(static_cast<int>(results.trains.size()) + 1);

	// Start and end as clock times, travel time as a duration, energy with one
	// decimal; a dash marks values the run did not produce.
	const auto clockText = [&run](const RunResultValue& value) {
		return value.available
			? QString::fromStdString(formatSimTime(static_cast<long long>(value.value), run->settings.startOffsetSeconds))
			: QStringLiteral("-");
	};
	const auto durationText = [](const RunResultValue& value) {
		if (!value.available)
			return QStringLiteral("-");
		const long long total = static_cast<long long>(value.value);
		return QString("%1:%2:%3")
			.arg(total / 3600)
			.arg((total % 3600) / 60, 2, 10, QChar('0'))
			.arg(total % 60, 2, 10, QChar('0'));
	};
	const auto energyText = [](const RunResultValue& value) {
		return value.available ? QString::number(value.value, 'f', 1) : QStringLiteral("-");
	};
	for (int row = 0; row < static_cast<int>(results.trains.size()); ++row) {
		const TrainRunResult& result = results.trains[static_cast<std::size_t>(row)];
		m_runResultsTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(result.trainId)));
		m_runResultsTable->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(result.operatingCode)));
		m_runResultsTable->setItem(row, 2,
			new QTableWidgetItem(QString::number(result.performancePercent, 'f', 1)));
		m_runResultsTable->setItem(row, 3,
			new QTableWidgetItem(QString::number(result.appliedMaximumSpeedKmh, 'f', 1)));
		m_runResultsTable->setItem(row, 4, new QTableWidgetItem(clockText(result.startSeconds)));
		m_runResultsTable->setItem(row, 5, new QTableWidgetItem(clockText(result.endSeconds)));
		m_runResultsTable->setItem(row, 6, new QTableWidgetItem(durationText(result.travelSeconds)));
		m_runResultsTable->setItem(row, 7, new QTableWidgetItem(energyText(result.energyConsumedKWh)));
		m_runResultsTable->setItem(row, 8, new QTableWidgetItem(energyText(result.energyWithRegenKWh)));
		m_runResultsTable->setItem(row, 9, new QTableWidgetItem(energyText(result.substationKWh)));
		m_runResultsTable->setItem(row, 10, new QTableWidgetItem(energyText(result.substationWithRegenKWh)));
	}

	const int totalRow = static_cast<int>(results.trains.size());
	m_runResultsTable->setItem(totalRow, 0, new QTableWidgetItem(QStringLiteral("Network total")));
	m_runResultsTable->setItem(totalRow, 1, new QTableWidgetItem(QStringLiteral("-")));
	m_runResultsTable->setItem(totalRow, 2, new QTableWidgetItem(QStringLiteral("-")));
	m_runResultsTable->setItem(totalRow, 3, new QTableWidgetItem(QStringLiteral("-")));
	m_runResultsTable->setItem(totalRow, 4, new QTableWidgetItem(clockText(results.networkStartSeconds)));
	m_runResultsTable->setItem(totalRow, 5, new QTableWidgetItem(clockText(results.networkEndSeconds)));
	m_runResultsTable->setItem(totalRow, 6, new QTableWidgetItem(durationText(results.networkTravelSeconds)));
	m_runResultsTable->setItem(totalRow, 7, new QTableWidgetItem(energyText(results.energyConsumedKWh)));
	m_runResultsTable->setItem(totalRow, 8, new QTableWidgetItem(energyText(results.energyWithRegenKWh)));
	m_runResultsTable->setItem(totalRow, 9, new QTableWidgetItem(energyText(results.substationKWh)));
	m_runResultsTable->setItem(totalRow, 10, new QTableWidgetItem(energyText(results.substationWithRegenKWh)));
	m_runResultsTable->resizeColumnsToContents();
	const bool openingResults = !m_runResultsDock->isVisible();
	m_runResultsDock->show();
	m_runResultsDock->raise();
	if (openingResults && !m_runResultsDock->isFloating()
		&& dockWidgetArea(m_runResultsDock) == Qt::BottomDockWidgetArea)
		resizeDocks({m_runResultsDock}, {height() / 2}, Qt::Vertical);
	if (const auto runResultsRefreshed = mainWindowTestHooks().runResultsRefreshed)
		runResultsRefreshed(*this);
}

void MainWindow::refreshRunResultsSummary() {
	if (!m_runResultsSummaryLabel || !m_runController->completedRun() || m_runController->completedRun()->results.trains.empty())
		return;
	int destinationTerminationCount = 0;
	for (const TrainRunResult& result : m_runController->completedRun()->results.trains) {
		if (result.destinationTerminated)
			++destinationTerminationCount;
	}
	QString summary = QString("Run: %1 | Occurrences: %2 | Status: Completed")
						  .arg(completedRunContext(m_runController->completedRun()->provenance),
							  QString::fromStdString(runOccurrencesText(m_runController->lastRunSelectedOccurrences(),
								  m_runController->lastRunTotalOccurrences(), m_runController->lastRunSelectedInPeriodOccurrences())));
	if (advancedDetailsEnabled()) {
		summary += QString(" | %1 | Destination terminations: %2")
					   .arg(QString::fromStdString(directIncidentEvidenceText(m_runController->completedRun()->results)))
					   .arg(destinationTerminationCount);
	}
	if (const auto& baseline = m_runController->delayBaseline())
		summary += QString("\nDelay baseline: %1").arg(completedRunContext(baseline->provenance));
	m_runResultsSummaryLabel->setText(summary);
}

// help from menu
void MainWindow::handleHelpAbout() {
	if (!m_aboutDialog)
		m_aboutDialog = new AboutDialog(this);
	m_aboutDialog->show();
	m_aboutDialog->raise();
	m_aboutDialog->activateWindow();
}

// the details of a passenger in the snapshot; only the id is known when the snapshot does not list the passenger
GuiPassengerState MainWindow::passengerState(const std::string& id) const {
	return passengerStateIn(m_snapshot.get(), id);
}

bool MainWindow::showingTrackPreview() const {
	return m_showingTrackPreview;
}

const SceneModel* MainWindow::loadedScene() const {
	return m_document->isLoaded() ? &m_document->model() : nullptr;
}

quint64 MainWindow::loadedSceneRevision() const {
	return m_document->revision();
}

int MainWindow::regionOfTrack(int track) const {
	return makeRuntimeGeometryFromRun(simulation)->regionOfTrack(track);
}

bool MainWindow::passengerGui() const {
	return m_runController->passengerGui();
}

bool MainWindow::replayActive() const {
	return m_replayActive;
}

void MainWindow::closeSceneContextMenu() {
	if (m_sceneContextMenu) {
		m_sceneContextMenu->close();
		m_sceneContextMenu.clear();
	}
}

void MainWindow::inspectionChanged() {
	updateViewportOverlays();
}

void MainWindow::signalInspected() {
	updateSignalCues();
}

void MainWindow::followTrain(int trainIndex) {
	setFollowTrain(trainIndex);
}

// fit view
void MainWindow::fitView() {
	updateNetworkLegend();
	if (networkView) {
		if (!m_presenter->m_previewFitBounds.isEmpty())
			networkView->fitToBounds(m_presenter->m_previewFitBounds);
		else
			networkView->fitToTopology();
		m_followCamera->settle();
	}
	updateViewportOverlays();
	updateZoomStatus();
}

// slot to update GUI at each timestep (no longer blocks; simulation runs on worker thread)

void MainWindow::updateTimeline(int timestep, int totalTimesteps) {
	QEGTRAIN_PROFILE_SCOPE("gui/snapshot_delivery/timeline", "gui", "gui/snapshot_delivery");
	if (progressBar)
		progressBar->setProgress(timestep, totalTimesteps, m_runController->startOffsetSeconds());
}

void MainWindow::waitForUpdates() {
	QEGTRAIN_PROFILE_SCOPE("gui/snapshot_delivery", "gui", "");
	// A queued notification can outlive its worker or arrive after Stop. Drain
	// the mailbox, but never apply that late publication to the current scene.
	if (!m_runController->running() || m_runController->runtimeStatus() != QStringLiteral("Running")
		|| m_runController->stopRequested()) {
		simulation.takeSimulationSnapshot();
		return;
	}
	const auto snapshot = simulation.takeSimulationSnapshot();
	if (!snapshot)
		return;
	m_snapshot = snapshot;
	PlaybackProfiler::instance().noteDelivery();
	const int timestep = snapshot->timestep;
	if (const auto snapshotDelivered = mainWindowTestHooks().snapshotDelivered)
		snapshotDelivered(*this);
	updateTimeline(timestep, snapshot->totalTimesteps);

	qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (now - m_lastRenderMs >= 33 || timestep >= snapshot->totalTimesteps - 1) {
		PlaybackProfiler::instance().noteRenderedUpdate();
		renderSnapshot(false);
		m_lastRenderedSnapshot = snapshot;
		m_lastRenderMs = now;
		if (m_trailingRenderTimer)
			m_trailingRenderTimer->stop();
		if (const auto snapshotDrawn = mainWindowTestHooks().snapshotDrawn)
			snapshotDrawn(*this);
	} else {
		// The render limit skipped this snapshot. The worker may pause before it
		// publishes another one, so draw the newest when the limit is over. The last
		// step of a run is always drawn by the branch above.
		if (!m_trailingRenderTimer) {
			m_trailingRenderTimer = new QTimer(this);
			m_trailingRenderTimer->setSingleShot(true);
			connect(m_trailingRenderTimer, &QTimer::timeout, this, [this]() {
				if (!m_snapshot || m_snapshot == m_lastRenderedSnapshot || m_replayActive || !m_runController->running()
					|| m_runController->worker() != m_trailingRenderWorker || m_runController->stopRequested()
					|| m_runController->runtimeStatus() != QStringLiteral("Running"))
					return;
				renderSnapshot(false);
				m_lastRenderedSnapshot = m_snapshot;
				m_lastRenderMs = QDateTime::currentMSecsSinceEpoch();
				if (const auto snapshotDrawn = mainWindowTestHooks().snapshotDrawn)
					snapshotDrawn(*this);
			});
		}
		m_trailingRenderWorker = m_runController->worker();
		if (!m_trailingRenderTimer->isActive())
			m_trailingRenderTimer->start(static_cast<int>(33 - (now - m_lastRenderMs)));
	}

	if (PlaybackProfiler::enabled() && !m_playbackProfileViewApplied
		&& !PlaybackProfiler::instance().measuring()
		&& !PlaybackProfiler::instance().frozen()) {
		TrainItemGroup* selected = nullptr;
		for (TrainItemGroup* train : m_presenter->allTrains)
			if (train && train->isVisible() && (!selected || train->index < selected->index))
				selected = train;
		if (selected) {
			m_playbackProfileViewApplied = true;
			setFollowTrain(-1);
			networkView->fitToTopology();
			if (qEnvironmentVariable("QEGTRAIN_PLAYBACK_PROFILE_VIEW") == QLatin1String("dense")) {
				networkView->centerOn(selected->sceneBoundingRect().center());
				networkView->zoomBy(3.0);
			}
			PlaybackProfiler::instance().arm(timestep);
			networkView->viewport()->update();
		}
	}
}

void MainWindow::seekReplay(int requestedTime) {
	if (m_completedReplay.empty() || m_runController->running()) return;
	m_replayRequestedTime = qBound(m_completedReplay.firstTime(), requestedTime, m_completedReplay.lastTime());
	const auto frame = m_completedReplay.atOrBefore(m_replayRequestedTime);
	if (!frame) return;
	m_replayActive = true;
	// Passenger journey inspection is not historical. Retain train, station and
	// signal selection; the shared renderer refreshes their frame-derived values.
	m_presenter->removePaxInfoIcon();
	m_presenter->paxIconItem = nullptr;
	m_presenter->stopTrainAnimations();
	m_presenter->m_prevTrainPositions.clear();
	m_snapshot = frame;
	{
		const QSignalBlocker blocker(m_replaySlider);
		m_replaySlider->setValue(m_replayRequestedTime);
	}
	writeReplayText(frame->timestep);
	renderSnapshot(true);
	if (m_presenter->trainPaxItem && !replayTrainHasPosition(m_presenter->trainPaxItem->index))
		m_presenter->removeTrainPaxInfoIcon();
	else if (m_presenter->trainPaxItem && !m_presenter->trainPaxInfoItem && m_runController->passengerGui()
		&& m_presenter->m_passengerLayerVisible)
		m_presenter->paintTrainPassengerInfo(m_presenter->trainPaxItem);
	updateViewportOverlays();
}

void MainWindow::renderSnapshot(bool historical) {
	if (!m_snapshot) return;
	m_presenter->apply(*m_snapshot, historical ? NetworkPresenter::Playback::Replay : NetworkPresenter::Playback::Live);

	if (const auto frameRendered = mainWindowTestHooks().frameRendered)
		frameRendered(*this, historical);
}

// Throttled delivery can skip the whole visible lifetime of a train.
void MainWindow::trainWillUpdate(int trainIndex) {
	if (m_followTrainIndex != trainIndex)
		return;
	const FollowAvailability following = followAvailabilityOf(trainIndex);
	if (following.switchOff) {
		setFollowTrain(-1);
		statusBar()->showMessage(QString::fromStdString(following.statusText), 5000);
	}
}

bool MainWindow::trainPlaced(int trainIndex, const QPointF& center) {
	if (m_followTrainIndex != trainIndex)
		return false;
	const FollowAvailability following = followAvailabilityOf(trainIndex);
	if (!following.canAct) {
		m_followCamera->stop();
		return false;
	}
	m_followCamera->follow(center, !following.glide);
	return true;
}

void MainWindow::trainHidden(int trainIndex) {
	if (m_followTrainIndex == trainIndex)
		m_followCamera->stop();
}

bool MainWindow::trainAdded(int trainIndex, bool firstLiveTrain) {
	TrainItemGroup* newTrain = resolveTrainItem(trainIndex);
	if (firstLiveTrain && newTrain && newTrain->isVisible())
		centerSceneItem(newTrain);
	if (m_followTrainIndex != trainIndex)
		return false;
	cutToFollowedTrain(followAvailabilityOf(trainIndex));
	return true;
}

void MainWindow::signalsApplied() {
	updateSignalCues();
}

void MainWindow::trainsApplied(bool anyAdded, bool cameraMoved) {
	if (anyAdded)
		updateNetworkLegend();
	// A move of the camera does not refresh the overlays, so the station of the followed train is refreshed here.
	if (cameraMoved)
		updateViewportOverlays();
	updateFollowAvailability();
}

// Choose a reference from routes that were actually used in this run.
int chooseRouteDiagramReference(QWidget* parent, const SceneModel& model, const CompletedRunData& run, const QString& purpose) {
	const QVector<RouteReferenceChoice> choices = buildRouteReferenceChoices(model, usedRouteReferencesFrom(run));
	if (choices.isEmpty()) return -1;
	if (e2eDialogsSuppressed()) return choices.first().runtimeIndex;
	RouteReferenceDialog dialog(choices, purpose, parent);
	return dialog.exec() == QDialog::Accepted ? dialog.selectedRuntimeIndex() : -1;
}

void MainWindow::displayTrainPathDiagrams() {
	if (!hasRunResults()) return;
	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	const int reference = chooseRouteDiagramReference(this, m_document->model(), *run, "train paths");
	if (reference >= 0) buildRouteDiagram(run, false, reference);
}

// --- Zoom controls ---
void MainWindow::zoomIn() {
	if (!networkView)
		return;
	if (networkView->zoomBy(1.15)) {
		if (!m_presenter->m_previewFitBounds.isEmpty())
			networkView->centerOn(m_presenter->m_previewZoomFocus);
		m_followCamera->settle();
	}
}

void MainWindow::zoomOut() {
	if (networkView && networkView->zoomBy(1.0 / 1.15))
		m_followCamera->settle();
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

void MainWindow::showTractiveEffortDistanceDiagram() {
	buildPerTrainDiagram(3);
}

// mode 0: speed vs distance, 1: speed vs time, 2: time vs distance,
// 3: simulated tractive effort vs distance
void MainWindow::buildPerTrainDiagram(int mode) {
	// require a completed run
	if (!hasRunResults()) {
		statusBar()->showMessage("Run a simulation to open diagrams", 5000);
		return;
	}

	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	const char* titles[] = {"Speed vs Distance", "Speed vs Time", "Time vs Distance", "Simulated Tractive Effort vs Distance"};
	const QString heading = titles[mode];
	QChart* chart = new QChart();
	chart->setTitle(QString("%1 [%2]").arg(heading, completedRunContext(run->provenance)));

	// one series per contiguous segment; legend distinguishes trains
	for (const CompletedTrain& train : run->trains) {
		const CompletedTrajectory& trajectory = train.trajectory;
		for (const TrajectorySegment& segment : trajectory.segments) {
			QLineSeries* series = new QLineSeries();
			series->setName(QString::fromStdString(train.trainId));
			series->setProperty("trainId", QString::fromStdString(train.trainId));
			for (int i = segment.first; i <= segment.last; i++) {
				double tSec = trajectoryTimeSeconds(i, run->settings.timestepSeconds);
				double dist = trajectory.at(trajectory.position, i).value_or(0.0) / 1000.0;
				double spd = trajectory.at(trajectory.speed, i).value_or(0.0) * 3.6; // m/s to km/h
				if (mode == 0)
					series->append(dist, spd); // x: distance (km), y: speed (km/h)
				else if (mode == 1)
					series->append(tSec, spd); // x: time (s),      y: speed (km/h)
				else if (mode == 2)
					series->append(tSec, dist); // x: time (s),      y: distance (km)
				else if (const auto effort = trajectory.at(trajectory.effort, i))
					series->append(dist, *effort / 1000.0);
			}
			chart->addSeries(series);
		}
	}
	chart->createDefaultAxes();
	const char* xTitles[] = {"Distance (km)", "Time", "Time", "Distance (km)"};
	const char* yTitles[] = {"Speed (km/h)", "Speed (km/h)", "Distance (km)", "Simulated tractive effort (kN)"};
	if (!chart->axes(Qt::Horizontal).isEmpty())
		chart->axes(Qt::Horizontal).first()->setTitleText(xTitles[mode]);
	if (!chart->axes(Qt::Vertical).isEmpty())
		chart->axes(Qt::Vertical).first()->setTitleText(yTitles[mode]);

	DiagramWindow* win = new DiagramWindow(heading, this);
	win->setTelemetryCapture([this] { return captureTelemetryOperation(); });
	win->setChart(chart);
	win->setPresentation(heading, QString("Run: %1").arg(completedRunContext(run->provenance)), QString());
	win->setTrainColors(trainColorsForRun(run->results, m_document->model()));
	win->setCsvProvider(csvProviderFrom(run, &trajectoryCsvFrom), "trajectory.csv");
	attachRunProvenance(win, run->provenance);
	connect(win, &DiagramWindow::trainSelected, this, &MainWindow::focusTrainInScene);
	win->setTimeAxisX(mode == 1 || mode == 2, run->settings.startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

// Centre the network view on the train selected in a diagram, when it is present.
void MainWindow::focusTrainInScene(const QString& trainId) {
	const std::string id = trainId.toStdString();
	for (TrainItemGroup* item : m_presenter->allTrains) {
		if (item && item->trainDescription == id) {
			networkView->centerOn(item->sceneBoundingRect().center());
			statusBar()->showMessage(QString("Selected train %1 in the network view").arg(trainId), 5000);
			return;
		}
	}
	statusBar()->showMessage(
		QString("Train %1 is not shown in the current network view").arg(trainId), 5000);
}

void MainWindow::showTimetableTable() {
	if (!hasRunResults()) {
		statusBar()->showMessage("Run a simulation to open diagrams", 5000);
		return;
	}

	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	auto* window = new TimetableTableWindow(run->timetable, run->settings.startOffsetSeconds,
		csvProviderFrom(run, &timetableCsvFrom), this);
	window->setTelemetryCapture([this] { return captureTelemetryOperation(); });
	window->setTrainColors(trainColorsForRun(run->results, m_document->model()));
	window->setRunProvenance(run->provenance);
	window->setWindowTitle(QString("Timetable: planned vs simulated [%1]").arg(completedRunContext(run->provenance)));
	window->setAttribute(Qt::WA_DeleteOnClose);
	window->show();
}

void MainWindow::showDelayDiagram() {
	if (!hasRunResults()) {
		statusBar()->showMessage("Run a simulation to open diagrams", 5000);
		return;
	}

	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	QChart* chart = new QChart();
	const QString heading = QStringLiteral("Arrival delay along journey (minutes)");
	chart->setTitle(QString("%1 [%2]").arg(heading, completedRunContext(run->provenance)));

	for (const CompletedTrain& train : run->trains) {
		QLineSeries* series = new QLineSeries();
		const QString trainName = QString::fromStdString(train.trainId);
		series->setName(trainName + " (arrival delay)");
		series->setProperty("trainId", trainName);
		bool hasData = false;
		for (const TimetableResultRow& row : run->timetable) {
			if (row.trainId != train.trainId || !row.arrivalDelaySeconds.available)
				continue;
			appendInspectedPoint(series, row.journeyIndex, row.arrivalDelaySeconds.value / 60.0,
				QString("Station: %1 | Call: %2 | Arrival delay")
					.arg(QString::fromStdString(row.stationId))
					.arg(row.callIndex));
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
		chart->axes(Qt::Horizontal).first()->setTitleText("Journey order (1-based)");
	}
	if (!chart->axes(Qt::Vertical).isEmpty()) {
		chart->axes(Qt::Vertical).first()->setTitleText("Arrival delay (min)");
	}

	DiagramWindow* win = new DiagramWindow(heading, this);
	win->setTelemetryCapture([this] { return captureTelemetryOperation(); });
	win->setChart(chart);
	win->setPresentation(heading, QString("Run: %1").arg(completedRunContext(run->provenance)), QString());
	win->setTrainColors(trainColorsForRun(run->results, m_document->model()));
	win->setCsvProvider(csvProviderFrom(run, &timetableCsvFrom), "timetable.csv");
	attachRunProvenance(win, run->provenance);
	connect(win, &DiagramWindow::trainSelected, this, &MainWindow::focusTrainInScene);
	win->setTimeAxisX(false, run->settings.startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

void MainWindow::showTimetableGraph() {
	if (!hasRunResults()) return;
	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	const int reference = chooseRouteDiagramReference(this, m_document->model(), *run, "timetable");
	if (reference >= 0) buildRouteDiagram(run, true, reference);
}

void MainWindow::buildRouteDiagram(const std::shared_ptr<const CompletedRunData>& run, bool timetable, int referenceIndex) {
	const std::optional<RouteDiagramData> diagram = routeDiagramData(*run, timetable, referenceIndex);
	if (!diagram) return;
	const RouteDiagramPath& referencePath = diagram->referencePath;
	const double origin = referencePath.nodes.front().positionKm;
	const double end = referencePath.nodes.back().positionKm;
	const QString heading = timetable ? "Timetable graph: planned vs simulated stops"
									  : "Train paths: simulated movement";
	const QString explanation = timetable
		? "Stop arrivals/departures and dwell, not continuous movement."
		: "Recorded position samples, not planned stop times.";
	auto* chart = new QChart();
	chart->setTitle(heading);
	for (const RouteDiagramSeries& source : diagram->series) {
		const QString name = QString::fromStdString(source.trainId);
		auto* series = new QLineSeries();
		series->setProperty("trainId", name);
		if (source.kind == RouteDiagramSeries::Kind::Trajectory) {
			series->setName(name);
			series->setProperty("inspectionInterval",
				QString("Route: %1 | Simulated trajectory").arg(QString::fromStdString(source.routeId)));
			for (const RouteDiagramPoint& point : source.points) series->append(point.positionKm, point.seconds);
			series->setPointsVisible(source.points.size() == 1); // Preserve an isolated valid sample.
		} else {
			const QString event = QString::fromStdString(source.event);
			series->setName(name + " (" + event + ")");
			QPen pen(QColor::fromHsv((source.trainIndex * 71) % 360, 175, source.planned ? 170 : 235));
			if (source.planned) pen.setStyle(Qt::DashLine);
			series->setPen(pen);
			const bool dwell = source.kind == RouteDiagramSeries::Kind::Dwell;
			series->setPointsVisible(!dwell);
			for (const RouteDiagramPoint& point : source.points) {
				const QString station = QString::fromStdString(point.station);
				const QString context = dwell
					? QString("Station: %1 | Call: %2 | %3 dwell").arg(station).arg(point.call).arg(source.planned ? "Planned" : "Simulated")
					: QString("Station: %1 | Call: %2 | Journey: %3 | %4").arg(station).arg(point.call).arg(point.journey).arg(event);
				appendInspectedPoint(series, point.positionKm, point.seconds, context);
			}
		}
		chart->addSeries(series);
	}
	chart->createDefaultAxes();
	if (chart->axes(Qt::Horizontal).isEmpty()) chart->addAxis(new QValueAxis(), Qt::AlignBottom);
	if (!chart->axes(Qt::Horizontal).isEmpty()) {
		auto* axis = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
		axis->setTitleText(QString("Route X (km); origin %1, travel %2")
				.arg(origin, 0, 'f', 3)
				.arg(end >= origin ? "right" : "left"));
		const double low = std::min(origin, end), high = std::max(origin, end);
		axis->setRange(low - 0.01, std::max(low + 0.02, high + 0.01));
	}
	QValueAxis* axisY = chart->axes(Qt::Vertical).isEmpty() ? new QValueAxis() : qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first());
	if (chart->axes(Qt::Vertical).isEmpty()) chart->addAxis(axisY, Qt::AlignLeft);
	axisY->setTitleText("Elapsed simulation time (s), downward");
	axisY->setReverse(true);
	axisY->setRange(0, std::max(1.0, run->settings.horizonSeconds()));
	for (auto* series : chart->series()) {
		if (!series->attachedAxes().contains(axisY)) series->attachAxis(axisY);
		if (!series->attachedAxes().contains(chart->axes(Qt::Horizontal).first()))
			series->attachAxis(chart->axes(Qt::Horizontal).first());
	}
	// Station names come from platform-bound canonical IDs, not from map X.
	const auto stationLabels = routeDiagramStationLabels(referencePath);
	QCategoryAxis* stationAxis = nullptr;
	std::vector<StationTick> stationTicks;
	if (!stationLabels.empty()) {
		stationAxis = new QCategoryAxis();
		stationAxis->setTitleText("Reference stations");
		stationAxis->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
		auto* distanceAxis = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
		stationAxis->setStartValue(distanceAxis->min() - 1);
		stationAxis->setRange(distanceAxis->min(), distanceAxis->max());
		chart->addAxis(stationAxis, Qt::AlignTop);
		for (auto* series : chart->series()) series->attachAxis(stationAxis);
		for (const auto& station : stationLabels) {
			const QString name = QString::fromStdString(station.second);
			stationTicks.push_back({station.first, {name + QString(" (%1 km)").arg(station.first, 0, 'f', 2), name}});
		}
	}
	// Include elapsed zero explicitly, even when clock labels start at a nonzero offset.
	DiagramWindow* win = new DiagramWindow(heading, this);
	win->setTelemetryCapture([this] { return captureTelemetryOperation(); });
	win->setChart(chart);
	if (stationAxis) win->setStationAxis(stationAxis, std::move(stationTicks));
	win->setTrainColors(trainColorsForRun(run->results, m_document->model()));
	win->setPresentation(heading,
		QString("Reference: %1 | %2 | 0 s = run start; time downward")
			.arg(QString::fromStdString(referencePath.id), completedRunContext(run->provenance)),
		explanation + " Ambiguous/unmapped portions omitted; no extrapolation.");
	const auto graphCsv = [csvRows = diagram->csvRows](const QStringList& visible) {
		std::vector<std::string> ids;
		for (const QString& id : visible) ids.push_back(id.toStdString());
		return buildRouteDiagramCsv(csvRows, ids);
	};
	win->setCsvProvider(graphCsv, timetable ? "timetable_graph.csv" : "train_path_graph.csv");
	if (const auto routeDiagramBuilt = mainWindowTestHooks().routeDiagramBuilt)
		routeDiagramBuilt(graphCsv, trainIdsFrom(*run), timetable);
	attachRunProvenance(win, run->provenance);
	connect(win, &DiagramWindow::trainSelected, this, &MainWindow::focusTrainInScene);
	win->setTimeAxisY(true, run->settings.startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

void MainWindow::showBlockingTimeDiagram() {
	if (!hasRunResults()) {
		statusBar()->showMessage("Run a simulation to open diagrams", 5000);
		return;
	}

	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	BlockingTimeScope scope = defaultBlockingTimeScopeFrom(*run);
	if (!e2eDialogsSuppressed() && !chooseBlockingTimeScope(this, blockingTimeScopeChoicesFrom(*run), scope))
		return;
	const std::optional<BlockingTimeDiagramData> diagram = blockingTimeDiagramData(*run, scope);
	if (!diagram)
		return;
	const RouteDiagramPath& referencePath = diagram->referencePath;
	const std::vector<BlockingTimeDiagramSegment>& segments = diagram->segments;
	const std::vector<BlockingTimeDiagramSegment>& unmapped = diagram->unmapped;
	const std::vector<BlockSectionExtent>& extents = diagram->extents;
	const std::vector<BlockingTimePlannedReference>& plannedReferences = diagram->plannedReferences;
	const auto& plottedPlanned = diagram->plannedLines;
	const auto& trajectoryRows = diagram->trajectoryRows;
	const int omitted = diagram->omitted;

	QChart* chart = new QChart();
	QString routeScope = QStringLiteral("all routes");
	if (scope.routeIndex >= 0 && scope.routeIndex < static_cast<int>(run->routes.size())) {
		const CompletedRoute& route = run->routes[static_cast<std::size_t>(scope.routeIndex)];
		routeScope = QString::fromStdString(route.id);
		if (!route.corridor.empty())
			routeScope += QString(" / %1").arg(QString::fromStdString(route.corridor));
		if (!scope.blockIds.empty())
			routeScope += QString(" / %1 to %2").arg(QString::fromStdString(scope.blockIds.front()), QString::fromStdString(scope.blockIds.back()));
	}
	chart->setTitle("Blocking time: envelopes and trajectories");

	addBlockingTimeSeries(chart, segments, false);

	const QColor plannedColors[] = {
		QColor(36, 117, 181), QColor(205, 92, 92), QColor(46, 139, 87),
		QColor(138, 43, 226), QColor(210, 105, 30), QColor(0, 128, 128)};
	std::map<std::string, int> plannedColorsByTrain;
	for (const auto& group : plottedPlanned) {
		const std::string& trainName = group.front().trainName;
		const auto color = plannedColorsByTrain.emplace(trainName, static_cast<int>(plannedColorsByTrain.size()));
		auto* series = new QLineSeries();
		series->setName(QString::fromStdString(trainName + " (planned reference)"));
		series->setProperty("trainId", QString::fromStdString(trainName));
		QPen pen(plannedColors[color.first->second % (sizeof(plannedColors) / sizeof(plannedColors[0]))]);
		pen.setStyle(Qt::DashLine);
		pen.setWidthF(2.5);
		series->setPen(pen);
		series->setPointsVisible(true);
		for (const auto& reference : group)
			appendInspectedPoint(series, reference.positionKm, reference.time,
				reference.stationName.empty()
					? "Clipped planned interpolation (not a station event)"
					: QString("Station: %1 | Planned %2").arg(QString::fromStdString(reference.stationName), QString::fromStdString(reference.eventType)));
		chart->addSeries(series);
		if (!color.second)
			for (QLegendMarker* marker : chart->legend()->markers(series)) marker->setVisible(false);
	}

	for (const BlockingTimeTrajectory& trajectory : diagram->trajectories) {
		auto* line = new QLineSeries();
		line->setName(QString::fromStdString(trajectory.trainId) + " (recorded trajectory)");
		line->setProperty("trainId", QString::fromStdString(trajectory.trainId));
		line->setProperty("layer", "recorded trajectory");
		line->setProperty("inspectionInterval", "Recorded simulation samples; no boundary samples inferred");
		line->setPen(QPen(QColor(20, 35, 45), 2.5));
		line->setPointsVisible(trajectory.points.size() == 1);
		for (const auto& point : trajectory.points)
			line->append(point.first, point.second);
		chart->addSeries(line);
	}

	auto* axisX = new QValueAxis;
	auto* axisY = new QValueAxis;
	chart->addAxis(axisX, Qt::AlignBottom);
	chart->addAxis(axisY, Qt::AlignLeft);
	for (auto* series : chart->series()) {
		series->attachAxis(axisX);
		series->attachAxis(axisY);
	}
	const double origin = referencePath.nodes.empty() ? 0.0 : referencePath.nodes.front().positionKm;
	const double end = referencePath.nodes.empty() ? origin : referencePath.nodes.back().positionKm;

	// The chart reads axis titles as rich text, so the route id is escaped.
	axisX->setTitleText(QString("Reference %1 X (km); origin %2, travel %3")
			.arg(QString::fromStdString(referencePath.id).toHtmlEscaped())
			.arg(origin, 0, 'f', 3)
			.arg(end >= origin ? "right" : "left"));
	const auto axisRange = blockSectionAxisRange(extents, referencePath);
	axisX->setRange(axisRange.first, axisRange.second);

	axisY->setTitleText("Elapsed simulation time (s), downward");
	axisY->setReverse(true);
	axisY->setRange(diagram->timeRange.first, diagram->timeRange.second);

	DiagramWindow* win = new DiagramWindow("Blocking time: envelopes and trajectories", this);
	win->setTelemetryCapture([this] { return captureTelemetryOperation(); });
	win->setChart(chart);
	win->setBlockSections(extents, QString::fromStdString(referencePath.id));
	// The envelope colours show the block type, so only the train list takes the service colours.
	win->setTrainListColors(trainColorsForRun(run->results, m_document->model()));
	const bool nothingPlotted = segments.empty() && plottedPlanned.empty() && trajectoryRows.empty();
	const QString note = QString("Calculated envelopes, not observed occupation; movement only within scope. "
								 "Unmapped endpoints/events omitted: %1; incomplete/missing-clearance blocks omitted. No extrapolation. "
								 "Envelope colours: blue blocks/stations, yellow switches, red conflicts. Neutral lines/strip: physical sections.")
							 .arg(omitted)
		+ (nothingPlotted ? QString(" No complete envelope could be projected in this scope.") : QString())
		+ QString::fromStdString(blockSectionGeometryNote(extents));
	win->setPresentation("Blocking time: envelopes and trajectories",
		QString("%1 | %2 to %3 [%4] | Reference: %5")
			.arg(routeScope,
				QString::fromStdString(formatSimTime(static_cast<long long>(scope.startTime), run->settings.startOffsetSeconds)),
				QString::fromStdString(formatSimTime(static_cast<long long>(scope.endTime), run->settings.startOffsetSeconds)),
				completedRunContext(run->provenance), QString::fromStdString(referencePath.id)),
		note);
	const std::function<std::string(const QStringList&)> scopedCsv =
		[segments, plannedReferences, trajectoryRows, extents, unmapped](const QStringList& visibleTrainIds) {
			return buildBlockingTimeCsv(visibleTrainIds, segments, plannedReferences, trajectoryRows, extents, unmapped);
		};
	win->setCsvProvider(scopedCsv, "blocking_time.csv", blockingTimeTrainIds(segments, plannedReferences, trajectoryRows, unmapped));
	attachRunProvenance(win, run->provenance);
	connect(win, &DiagramWindow::trainSelected, this, &MainWindow::focusTrainInScene);
	win->setTimeAxisY(true, run->settings.startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

void MainWindow::showCapacityAnalysis() {
	if (!hasRunResults()) {
		statusBar()->showMessage("Run a simulation to open capacity analysis", 5000);
		return;
	}

	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	CapacityAnalysisScope scope = firstCapacityScopeWithPairFrom(*run);
	if (scope.routeIndex < 0) {
		QMessageBox::information(this, "Capacity analysis",
			"No retained native blocking-time section has two occurrences with a common entry block. "
			"Run a scene with explicit signalling data; EGTRAIN does not infer a signalling system or blocking-time parameters.");
		return;
	}
	if ((!e2eDialogsSuppressed() || mainWindowTestHooks().askCapacityScope)
		&& !chooseCapacityAnalysisScope(this, *run, scope))
		return;
	if (scope.routeIndex < 0 || scope.blockIds.empty()) {
		QMessageBox::information(this, "Capacity analysis",
			"No route section has two occurrences with a common entry block. Choose a common entry or analyze the route in separate sections.");
		return;
	}
	const std::string scopeIssue = capacityScopeIssueFrom(*run, scope);
	if (!scopeIssue.empty()) {
		QMessageBox::information(this, "Capacity scope unavailable", QString::fromStdString(scopeIssue));
		return;
	}
	const auto trains = capacityInputsFrom(*run, scope);
	if (trains.size() < 2) {
		QMessageBox::information(this, "Capacity analysis",
			"Fewer than two occurrences share the selected section entry. Choose a common entry or a narrower section.");
		return;
	}
	const QString sectionLabel = capacityScopeLabelFrom(*run, scope);
	const CapacityAnalysisResult result = analyzeCapacity(trains, scope.periodSeconds,
		scope.cycleEndOccurrenceId);
	if (!result.analyzable) {
		QMessageBox::information(this, "Capacity analysis",
			"The selected occurrences do not form a valid shared-resource chain. Choose a common entry or analyze the route in separate sections.");
		return;
	}
	if (result.cycleEndIdentity.empty() || result.cycleTime < 0.0) {
		QMessageBox::information(this, "Capacity analysis",
			"The cycle-closing occurrence must be a selected row after the first occurrence.");
		return;
	}

	const RunProvenance provenance = run->provenance;
	QDialog* dialog = new QDialog(this);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle("Capacity analysis");
	dialog->setModal(false);
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	const QString percentage = QString::fromStdString(csv::formatDouble(result.cyclePercentage));
	const QString cycle = QString::fromStdString(csv::formatDouble(result.cycleTime));
	const QString period = QString::fromStdString(csv::formatDouble(result.periodSeconds));
	const auto sourceFor = [&result](const std::string& identity) {
		for (std::size_t index = 0; index < result.trainIdentities.size(); ++index)
			if (result.trainIdentities[index] == identity && index < result.referenceSources.size())
				return QString::fromStdString(result.referenceSources[index]);
		return QString();
	};
	auto* detailsToggle = new QToolButton(body);
	detailsToggle->setText("Calculation details");
	detailsToggle->setCheckable(true);
	detailsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	detailsToggle->setArrowType(Qt::RightArrow);
	auto* details = new QLabel(QString("Cycle / occupation time: %1 s; period: %2 s; cycle / period × 100: %3%.\n"
									   "Cycle start: %4 (%5); cycle end: %6 (%7). Conflict-free compressed occupations. "
									   "Capacity critical blocks are touching constraints, distinct from overlap/conflict styling.")
								   .arg(cycle, period, percentage, QString::fromStdString(result.firstIdentity),
									   sourceFor(result.firstIdentity), QString::fromStdString(result.cycleEndIdentity),
									   sourceFor(result.cycleEndIdentity)),
		body);
	details->setObjectName("resultContextText");
	details->setTextFormat(Qt::PlainText);
	details->setWordWrap(true);
	details->setMinimumWidth(0);
	details->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	details->hide();
	connect(detailsToggle, &QToolButton::toggled, dialog, [detailsToggle, details](bool shown) {
		detailsToggle->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
		details->setVisible(shown);
	});
	layout->addWidget(detailsToggle);
	layout->addWidget(details);

	auto* tabs = new QTabWidget(body);
	const auto labelFor = [&result](const std::string& identity) {
		for (std::size_t index = 0; index < result.trainIdentities.size(); ++index)
			if (result.trainIdentities[index] == identity && index < result.referenceLabels.size())
				return QString::fromStdString(result.referenceLabels[index]);
		return QString();
	};
	const auto addTable = [tabs](const QStringList& headers, int rows) {
		auto* table = new QTableWidget(rows, headers.size(), tabs);
		table->setHorizontalHeaderLabels(headers);
		table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		table->setSelectionBehavior(QAbstractItemView::SelectRows);
		table->setAlternatingRowColors(true);
		table->horizontalHeader()->setStretchLastSection(true);
		tabs->addTab(table, QString());
		return table;
	};
	auto* pairTable = addTable({"Leader", "Follower", "Leader code", "Follower code", "Scheduled headway [s]",
								   "Minimum headway [s]", "Buffer [s]", "Governing block evidence", "Reference label/source (leader → follower)"},
		static_cast<int>(result.pairs.size()));
	for (int row = 0; row < pairTable->rowCount(); ++row) {
		const CapacityPairRow& pair = result.pairs[static_cast<std::size_t>(row)];
		const std::string evidence = capacityEvidenceText(pair.governingEvidence);
		const QString source = labelFor(pair.leaderIdentity) + " [" + sourceFor(pair.leaderIdentity) + "] → "
			+ labelFor(pair.followerIdentity) + " [" + sourceFor(pair.followerIdentity) + "]";
		const QStringList values = {QString::fromStdString(pair.leaderIdentity), QString::fromStdString(pair.followerIdentity),
			QString::fromStdString(pair.leaderOperatingCode), QString::fromStdString(pair.followerOperatingCode),
			QString::fromStdString(csv::formatDouble(pair.scheduledHeadway)), QString::fromStdString(csv::formatDouble(pair.minimumHeadway)),
			QString::fromStdString(csv::formatDouble(pair.buffer)), QString::fromStdString(evidence), source};
		for (int column = 0; column < values.size(); ++column)
			pairTable->setItem(row, column, new QTableWidgetItem(values[column]));
	}
	tabs->setTabText(0, QString("Pairs (%1)").arg(pairTable->rowCount()));

	auto* compressionTable = addTable({"Identity", "Operating code", "Original reference [s]", "Scheduled reference [s]",
										  "Compressed reference [s]", "Shift [s]", "Governing predecessor / block evidence"},
		static_cast<int>(result.compression.size()));
	for (int row = 0; row < compressionTable->rowCount(); ++row) {
		const CapacityCompressionRow& compressed = result.compression[static_cast<std::size_t>(row)];
		QStringList governing;
		for (const CapacityCompressionEvidence& predecessor : compressed.governingPredecessors)
			governing << QString("%1 (%2 s): %3")
							 .arg(QString::fromStdString(predecessor.predecessorIdentity),
								 QString::fromStdString(csv::formatDouble(predecessor.minimumHeadway)),
								 QString::fromStdString(capacityEvidenceText(predecessor.governingEvidence)));
		const QStringList values = {QString::fromStdString(compressed.identity), QString::fromStdString(compressed.operatingCode),
			QString::fromStdString(csv::formatDouble(compressed.originalReference)),
			QString::fromStdString(csv::formatDouble(compressed.scheduledReference)),
			QString::fromStdString(csv::formatDouble(compressed.compressedReference)),
			QString::fromStdString(csv::formatDouble(compressed.shift)), governing.join("; ")};
		for (int column = 0; column < values.size(); ++column)
			compressionTable->setItem(row, column, new QTableWidgetItem(values[column]));
	}
	tabs->setTabText(1, QString("Compression (%1)").arg(compressionTable->rowCount()));

	auto* criticalTable = addTable({"Leader", "Follower", "Leader block", "Follower block", "Gap [s]"},
		static_cast<int>(result.criticalBlocks.size()));
	for (int row = 0; row < criticalTable->rowCount(); ++row) {
		const CapacityCriticalBlock& critical = result.criticalBlocks[static_cast<std::size_t>(row)];
		const QStringList values = {QString::fromStdString(critical.leaderIdentity), QString::fromStdString(critical.followerIdentity),
			QString::fromStdString(critical.leaderBlockId), QString::fromStdString(critical.followerBlockId),
			QString::fromStdString(csv::formatDouble(critical.gap))};
		for (int column = 0; column < values.size(); ++column)
			criticalTable->setItem(row, column, new QTableWidgetItem(values[column]));
	}
	tabs->setTabText(2, QString("Critical blocks (%1)").arg(criticalTable->rowCount()));
	for (int index = 0; index < tabs->count(); ++index)
		qobject_cast<QTableWidget*>(tabs->widget(index))->resizeColumnsToContents();
	layout->addWidget(tabs, 1);

	auto* secondaryActions = new QHBoxLayout();
	QPushButton* exportButton = new QPushButton("Export capacity CSV...", body);
	connect(exportButton, &QPushButton::clicked, dialog, [this, result, sectionLabel, provenance]() {
		const auto operation = captureTelemetryOperation();
		saveCsvInteractive(
			this, "capacity_analysis.csv", operation,
			[result, sectionLabel] { return capacityAnalysisCsv(result, sectionLabel); },
			[provenance](const QString& path, const std::string& bytes) {
				return writeRunArtifactWithProvenance(path.toStdString(), "csv", bytes, provenance);
			});
	});
	QPushButton* diagramButton = new QPushButton("Open compressed blocking-time diagram", body);
	const CompletedRoute& compressedRoute = run->routes[static_cast<std::size_t>(scope.routeIndex)];
	const RouteDiagramPath& compressedReference = compressedRoute.path;
	const auto compressedProjections = routeProjectionsByTrain(*run, compressedReference);
	int compressedOmitted = 0;
	auto shiftedSegments = buildBlockingTimeDiagramSegments(result.compressedOccupations, result.trainIdentities);
	restoreCompressedOriginalTimes(shiftedSegments, result.compression);
	std::vector<BlockingTimeDiagramSegment> compressedUnmapped;
	auto compressedSegments = projectBlockingSegments(shiftedSegments, compressedProjections, compressedOmitted, &compressedUnmapped);
	const auto allCompressedExtents = buildBlockSectionExtents(compressedRoute.blocks);
	const auto compressedExtents = scopedBlockSectionExtents(allCompressedExtents, scope.sectionIndices);
	matchBlockingSegmentsToSections(compressedSegments, allCompressedExtents, compressedReference.id);
	const auto axisRange = blockSectionAxisRange(compressedExtents, compressedReference);
	const QString referenceId = QString::fromStdString(compressedReference.id);
	connect(diagramButton, &QPushButton::clicked, dialog,
		[this, result, sectionLabel, run, compressedSegments, compressedUnmapped, compressedExtents, axisRange, referenceId]() {
			showCompressedBlockingTimeDiagram(result, sectionLabel, run, compressedSegments,
				axisRange.first, axisRange.second, referenceId, compressedExtents, compressedUnmapped);
		});
	secondaryActions->addWidget(exportButton);
	secondaryActions->addWidget(diagramButton);
	secondaryActions->addStretch();
	layout->addLayout(secondaryActions);
	auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
	connect(closeButtons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
	DialogLayout::install(*dialog,
		QString("Selected train chain: cycle %1 s, %2% of period; not network capacity")
			.arg(QString::number(result.cycleTime, 'f', 1),
				QString::number(result.cyclePercentage, 'f', 1)),
		QString("Section: %1\nRun: %2").arg(sectionLabel, completedRunContext(provenance)),
		body, closeButtons);
	dialog->show();
}

void MainWindow::showAreaCapacity() {
	if (!hasRunResults()) {
		statusBar()->showMessage("Run a simulation to open capacity by area", 5000);
		return;
	}
	// The options dialog reads the run until it closes, so this call holds it.
	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	openAreaCapacityWindow(this, *run, [this] { return captureTelemetryOperation(); });
}

void MainWindow::showCompressedBlockingTimeDiagram(const CapacityAnalysisResult& result,
	const QString& sectionLabel, const std::shared_ptr<const CompletedRunData>& run,
	const std::vector<BlockingTimeDiagramSegment>& segments, double axisMinKm, double axisMaxKm,
	const QString& referenceId, const std::vector<BlockSectionExtent>& extents,
	const std::vector<BlockingTimeDiagramSegment>& unmapped) {
	QChart* chart = new QChart();
	chart->setTitle("Compressed blocking-time diagram");
	addBlockingTimeSeries(chart, segments, true);
	auto* axisX = new QValueAxis;
	auto* axisY = new QValueAxis;
	chart->addAxis(axisX, Qt::AlignBottom);
	chart->addAxis(axisY, Qt::AlignLeft);
	double minimumTime = 0, maximumTime = 1;
	for (const auto& segment : segments) {
		minimumTime = std::min(minimumTime, segment.startTime);
		maximumTime = std::max(maximumTime, segment.endTime);
	}
	for (auto* series : chart->series()) {
		series->attachAxis(axisX);
		series->attachAxis(axisY);
	}
	axisX->setTitleText(QString("Reference %1 X (km)").arg(referenceId.toHtmlEscaped()));
	axisX->setRange(axisMinKm, axisMaxKm);

	axisY->setTitleText("Compressed envelope time (s), downward");
	axisY->setReverse(true);
	axisY->setRange(minimumTime, maximumTime);
	DiagramWindow* window = new DiagramWindow("Compressed blocking-time diagram", this);
	window->setTelemetryCapture([this] { return captureTelemetryOperation(); });
	window->setChart(chart);
	window->setBlockSections(extents, referenceId);
	window->setTrainListColors(trainColorsForRun(run->results, m_document->model()));
	const QString note = "Shifted calculated envelopes, not recorded movement. "
						 "Unmapped/incomplete blocks omitted; no extrapolation. "
						 "Envelope colours: blue blocks/stations, yellow switches, red conflicts, amber capacity-critical touching blocks. "
						 "Neutral lines/strip: physical sections."
		+ (segments.empty() ? QString(" No complete compressed envelope could be projected in this scope.") : QString());
	window->setPresentation("Compressed blocking-time diagram",
		QString("%1 | cycle %2 to %3 [%4] | Reference: %5")
			.arg(sectionLabel, QString::fromStdString(result.firstIdentity), QString::fromStdString(result.cycleEndIdentity),
				completedRunContext(run->provenance), referenceId),
		note);
	const std::function<std::string(const QStringList&)> csvProvider =
		[segments, extents, unmapped](const QStringList& visibleTrainIds) {
			return buildBlockingTimeCsv(visibleTrainIds, segments, {}, {}, extents, unmapped);
		};
	window->setCsvProvider(csvProvider, "capacity_compressed_blocking_time.csv", blockingTimeTrainIds(segments, {}, {}, unmapped));
	attachRunProvenance(window, run->provenance);
	connect(window, &DiagramWindow::trainSelected, this, &MainWindow::focusTrainInScene);
	window->setTimeAxisY(true, run->settings.startOffsetSeconds);
	window->setAttribute(Qt::WA_DeleteOnClose);
	window->show();
}

void MainWindow::updateNetworkLegend() {
	if (!m_networkLegendWidget)
		return;
	NetworkLegendContent content;
	// Runtime retains preview fit bounds, and a discarded run may still have
	// Failed status while the authoring preview is displayed.
	const bool preview = m_showingTrackPreview;
	content.hasTracks = simulation.run().infra.numTrackLines > 0 || !m_document->model().tracks.empty();
	content.showOperationalTrackStates = !preview;
	content.hasSelectedTrack = preview && m_presenter->m_previewHasSelectedTrack;
	for (const TrainItemGroup* train : m_presenter->allTrains) {
		if (train)
			content.trains << NetworkLegendTrain{train->fillColor, train->outlineColor,
				QString::fromStdString(train->serviceId)};
	}
	for (const StationOverlayItem* station : m_presenter->m_stationOverlays) {
		if (station)
			content.stationVisuals << station->visual();
	}
	content.hasSignals = !preview && !m_presenter->allSignals.isEmpty();
	if (preview)
		content.hasSignals = m_presenter->m_previewHasSignals;
	content.hasPassengers = !preview && m_runController->passengerGui();
	m_networkLegendWidget->setCaseContent(content);
}

void MainWindow::bindStationOverlaySources() {
	if (!scene)
		return;
	QList<StationNodeItem*> stationNodes;
	for (QGraphicsItem* item : scene->items()) {
		auto* station = qgraphicsitem_cast<StationNodeItem*>(item);
		if (station && station->node)
			stationNodes.append(station);
	}
	QList<QList<StationNodeItem*>> assignments;
	assignments.reserve(m_presenter->m_stationOverlays.size());
	for (int index = 0; index < m_presenter->m_stationOverlays.size(); ++index)
		assignments.append(QList<StationNodeItem*>());
	for (auto* station : stationNodes) {
		const QString stationName = QString::fromStdString(station->node->stationName);
		const QPointF stationCenter = station->sceneBoundingRect().center();
		int nearestIndex = -1;
		qreal nearestDistance = std::numeric_limits<qreal>::max();
		for (int index = 0; index < m_presenter->m_stationOverlays.size(); ++index) {
			const auto* overlay = m_presenter->m_stationOverlays.at(index);
			if (!overlay || overlay->stationName() != stationName)
				continue;
			const QPointF delta = stationCenter - overlay->stableAnchor();
			const qreal distance = delta.x() * delta.x() + delta.y() * delta.y();
			bool nearer = nearestIndex < 0 || distance < nearestDistance;
			if (!nearer && distance == nearestDistance) {
				const QPointF currentAnchor = m_presenter->m_stationOverlays.at(nearestIndex)->stableAnchor();
				const QPointF candidateAnchor = overlay->stableAnchor();
				nearer = candidateAnchor.x() < currentAnchor.x()
					|| (candidateAnchor.x() == currentAnchor.x()
						&& candidateAnchor.y() < currentAnchor.y());
			}
			if (nearer) {
				nearestIndex = index;
				nearestDistance = distance;
			}
		}
		if (nearestIndex >= 0)
			assignments[nearestIndex].append(station);
	}

	for (int index = 0; index < m_presenter->m_stationOverlays.size(); ++index) {
		auto* overlay = m_presenter->m_stationOverlays.at(index);
		if (!overlay)
			continue;
		auto& assigned = assignments[index];
		std::sort(assigned.begin(), assigned.end(), [overlay](const auto* left, const auto* right) {
			const QPointF leftDelta = left->sceneBoundingRect().center() - overlay->stableAnchor();
			const QPointF rightDelta = right->sceneBoundingRect().center() - overlay->stableAnchor();
			const qreal leftDistance = leftDelta.x() * leftDelta.x() + leftDelta.y() * leftDelta.y();
			const qreal rightDistance = rightDelta.x() * rightDelta.x() + rightDelta.y() * rightDelta.y();
			if (leftDistance != rightDistance)
				return leftDistance < rightDistance;
			if (left->track != right->track)
				return left->track < right->track;
			return left->node->ID < right->node->ID;
		});
		QList<StationOverlayItem::SourceIdentity> identities;
		for (const auto* station : assigned)
			identities.append({station->node->ID, station->track});
		overlay->setSourceIdentities(identities);
		// The scene-sized artwork belongs to the real semantic station node.
		// It can be picked at its painted location, not at a stale screen-space
		// collision slot owned by the decorative overlay.
		if (QGraphicsPixmapItem* picture = m_presenter->m_stationPictures.value(overlay, nullptr)) {
			const QPointF scenePosition = picture->scenePos();
			StationNodeItem* target = assigned.isEmpty() ? nullptr : assigned.first();
			picture->setParentItem(target);
			picture->setPos(target ? target->mapFromScene(scenePosition) : scenePosition);
		}
		if (overlay->hasSourceIdentity()) {
			const double representativeNodeId = overlay->sourceNodeId();
			const int representativeTrack = overlay->sourceTrack();
			overlay->setDisplacedClickHandler(
				[this, representativeNodeId, representativeTrack](const QString&) {
					QTimer::singleShot(0, this, [this, representativeNodeId, representativeTrack]() {
						if (auto* current = resolveStationNodeItem(
								representativeNodeId, representativeTrack))
							m_inspector->displayStationNodeInfo(current);
					});
				});
		} else {
			overlay->setDisplacedClickHandler({});
		}
	}
}

void MainWindow::updateStationOverlayDegrees() {
	if (!scene)
		return;
	const auto atEndpoint = [](const QPointF& point, const QPointF& endpoint) {
		return QLineF(point, endpoint).length() <= 1e-4;
	};
	const auto segmentDegree = [&](QGraphicsItem* item, const QLineF& line, const QPointF& point) {
		const QPointF first = item->mapToScene(line.p1());
		const QPointF second = item->mapToScene(line.p2());
		return (atEndpoint(point, first) || atEndpoint(point, second)) ? 1 : 0;
	};
	QList<StationNodeItem*> stationNodes;
	QList<QGraphicsItem*> edges;
	for (QGraphicsItem* item : scene->items()) {
		if (!item || !item->isVisible())
			continue;
		if (auto* station = qgraphicsitem_cast<StationNodeItem*>(item)) {
			if (station->node)
				stationNodes.append(station);
		} else if (qgraphicsitem_cast<TrackLineItem*>(item)
			|| qgraphicsitem_cast<ConnectionItem*>(item)) {
			edges.append(item);
		}
	}

	for (auto* overlay : m_presenter->m_stationOverlays) {
		if (!overlay)
			continue;
		int highestDegree = 0;
		bool hasInterchange = false;
		bool hasEndpoint = false;
		for (auto* station : stationNodes) {
			const bool assigned = overlay->hasSourceIdentity()
				? overlay->matchesSourceIdentity(station->node->ID, station->track)
				: QString::fromStdString(station->node->stationName) == overlay->stationName();
			if (!assigned)
				continue;
			const QPointF point = station->sceneBoundingRect().center();
			int degree = 0;
			for (auto* edge : edges) {
				if (auto* track = qgraphicsitem_cast<TrackLineItem*>(edge)) {
					degree += segmentDegree(track, track->line(), point);
					if (auto* virtualArc = dynamic_cast<VirtualArcItem*>(track))
						degree += segmentDegree(virtualArc, virtualArc->secondPaintedLine(), point);
				} else if (auto* connection = qgraphicsitem_cast<ConnectionItem*>(edge)) {
					degree += segmentDegree(connection, connection->line(), point);
				}
			}
			highestDegree = std::max(highestDegree, degree);
			hasInterchange = hasInterchange || degree >= 3;
			hasEndpoint = hasEndpoint || degree == 1;
		}
		overlay->setNetworkDegree(highestDegree, hasInterchange, hasEndpoint);
	}
}

bool MainWindow::isTrainOverlayPromoted(int trainIndex) const {
	return trainIndex == m_inspector->selectedTrainIndex()
		|| (m_followAction && m_followAction->isChecked() && m_followTrainIndex == trainIndex);
}

void MainWindow::updateViewportOverlays() {
	if (!networkView)
		return;
	QPointF followedCenter;
	bool hasFollowedCenter = false;
	if (m_followAction && m_followAction->isChecked() && m_followTrainIndex >= 0 && followAvailabilityOf(m_followTrainIndex).canAct) {
		if (auto* train = resolveTrainItem(m_followTrainIndex)) {
			followedCenter = train->sceneBoundingRect().center();
			hasFollowedCenter = true;
		}
	}
	StationOverlayItem* nearestFollowed = nullptr;
	qreal nearestDistance = std::numeric_limits<qreal>::max();
	for (auto* overlay : m_presenter->m_stationOverlays) {
		if (!overlay)
			continue;
		overlay->setSelected(m_inspector->selectsStationOverlay(overlay));
		overlay->setVisible(m_presenter->m_stationLayerVisible);
		overlay->setFollowed(false);
		if (!hasFollowedCenter)
			continue;
		const QPointF delta = overlay->stableAnchor() - followedCenter;
		const qreal distance = QPointF::dotProduct(delta, delta);
		if (!nearestFollowed || distance < nearestDistance
			|| (distance == nearestDistance
				&& overlay->stationName() < nearestFollowed->stationName())) {
			nearestFollowed = overlay;
			nearestDistance = distance;
		}
	}
	if (nearestFollowed)
		nearestFollowed->setFollowed(true);
	const qreal viewScale = std::abs(networkView->transform().m11());
	if (m_presenter->effect)
		m_presenter->effect->setViewScale(viewScale);
	for (auto* overlay : m_presenter->m_stationOverlays)
		if (overlay)
			overlay->applyViewScale(viewScale);
	StationOverlayItem::resolveNameCollisions(m_presenter->m_stationOverlays, networkView->viewportTransform());
	for (auto* overlay : m_presenter->m_stationOverlays)
		if (overlay && overlay->nameItem())
			overlay->nameItem()->setVisible(m_presenter->m_stationLayerVisible && m_presenter->m_stationNamesVisible
				&& !overlay->isNameHiddenByCollision());
	updateSignalCues();
	const bool paxText = m_presenter->paxTextVisible();
	for (auto* platform : m_presenter->allPlatforms)
		if (platform && platform->textIcon)
			platform->textIcon->setVisible(paxText);
	for (auto it = m_presenter->m_trainBadges.cbegin(); it != m_presenter->m_trainBadges.cend(); ++it) {
		if (!it.value())
			continue;
		const bool promoted = isTrainOverlayPromoted(it.key());
		it.value()->setPromoted(promoted);
		it.value()->setPresentation(
			TrainBadgeItem::presentationForZoom(networkView->zoomRatio(), promoted));
	}
}

// Keep every authored signal at its own anchor, including opposing heads.
// The decoration list also owns the posts and bases in preview and playback.
void MainWindow::updateSignalCues() {
	if (!networkView)
		return;
	for (QGraphicsItem* item : m_presenter->m_signalDecorations) {
		if (!item)
			continue;
		const bool visible = m_presenter->m_signalLayerVisible && item->data(kSignalBaseVisibleRole).toBool();
		auto* signal = qgraphicsitem_cast<SignalItem*>(item);
		if (signal) {
			signal->setGroupedSignals({});
			if (item->data(kSignalAnchorRole).isValid())
				signal->setPos(item->data(kSignalAnchorRole).toPointF());
			signal->setScale(1.0);
			signal->setToolTip(QStringLiteral("%1: %2, %3")
					.arg(signal->inspectionIdentity(), signal->reversedDirection ? QStringLiteral("left") : QStringLiteral("right"), signalAspectLabel(signal)));
		}
		item->setVisible(visible);
	}
	m_inspector->refreshInspectedSignal();
}

void MainWindow::updateZoomStatus() {
	if (m_zoomStatusLabel && networkView)
		m_zoomStatusLabel->setText(networkView->zoomLabel());
}
