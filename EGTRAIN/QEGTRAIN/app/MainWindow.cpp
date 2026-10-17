#include "app/MainWindow.h"
#include "app/CannotOpenMessage.h"
#include "app/DialogFolders.h"
#include "graphics/SignalItemRoles.h"
#include "app/MainWindowSupport.h"
#include "app/MainWindowTestHooks.h"
#include "app/RunSelection.h"
#include "app/OpenRequestRelay.h"
#include "app/ReleaseNotesText.h"
#include "app/ReplayCoverage.h"
#include "app/RuntimeGeometryFromGlobals.h"
#include "app/SceneOpenRequest.h"
#include "app/SceneOutputDirectory.h"
#include "app/SceneSavePolicy.h"
#include "app/UpdatePromptText.h"
#include "ui_MainWindow.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QAccessible>
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
#include "widgets/CompactDoubleSpinBox.h"
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
#include "diagrams/DelayAnalysisWindow.h"
#include "diagrams/TractionCurve.h"
#include "editors/CaseSettingsPanel.h"
#include "editors/CompositionPanel.h"
#include "editors/EntranceDelayPanel.h"
#include "editors/IncidentPanel.h"
#include "editors/InfrastructurePanel.h"
#include "editors/PassengerPanel.h"
#include "editors/ScenarioPanel.h"
#include "editors/ServicePanel.h"
#include "editors/TimetableStopsPanel.h"
#include "editors/TrainUnitPanel.h"
#include "editors/TrainUnitSourceLinker.h"
#include "editors/infrastructure/TopologyFacets.h"
#include "graphics/SignalGeometry.h"
#include "graphics/AnnotationPlacement.h"
#include "graphics/VisualPolish.h"
#include "scene/SceneBundle.h"
#include "scene/SceneCompatibility.h"
#include "scene/SceneMigration.h"
#include "scene/SceneWriter.h"
#include "scene/SceneExporter.h"
#include "scene/SceneHistory.h"
#include "scene/SceneIds.h"
#include "scene/SceneImporter.h"
#include "scene/SceneReferences.h"
#include "scene/SectionInventory.h"
#include "scene/SignallingLevelNames.h"
#include "scene/StopInsertion.h"
#include "scene/TrackPreview.h"
#include "simulation/Passengers.h"
#include "update/ReleaseInfo.h"
#include "update/UpdateChecker.h"
#include "update/UpdateNotices.h"
#include "update/UpdateSettings.h"
#include "telemetry/TelemetryConsent.h"
#include "telemetry/TelemetryConsentDialog.h"
#include "telemetry/TelemetrySender.h"
#include "update/SelfUpdater.h"
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
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QRadioButton>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QProgressDialog>
#include <QInputDialog>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QStandardPaths>
#include <QSysInfo>
#include <QSignalBlocker>
#include <QShortcut>
#include <QScopedValueRollback>
#include <QSettings>
#include <QIcon>
#include <QDesktopServices>
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

extern InitialParameters initial_variables;

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

// A combo box that ignores the mouse wheel, so the wheel scrolls the view behind it.
class NoWheelComboBox : public QComboBox {
public:
	using QComboBox::QComboBox;
protected:
	void wheelEvent(QWheelEvent* event) override {
		event->ignore();
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

const char* kRecentScenesKey = "recentScenes";
const int kMaxRecentScenes = 8;
constexpr int kOverlayMargin = 12;
constexpr const char kPlatformGeometryEditedProperty[] = "platformGeometryEdited";

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

QString completedRunContext(const RunProvenance& provenance) {
	const QString caseName = QString::fromStdString(provenance.caseName);
	const QString scenario = QString::fromStdString(provenance.appliedScenario);
	if (caseName.isEmpty())
		return scenario.isEmpty() ? QStringLiteral("(none)") : scenario;
	if (scenario.isEmpty())
		return caseName;
	return QStringLiteral("%1 / %2").arg(caseName, scenario);
}

std::optional<telemetry::Error> telemetryDiagnosticError(const std::vector<SceneDiagnostic>& diagnostics) {
	std::optional<telemetry::Error> selected;
	const auto priority = [](telemetry::Error error) {
		switch (error) {
			case telemetry::Error::UnsupportedFormat: return 0;
			case telemetry::Error::InvalidInput: return 1;
			case telemetry::Error::IoFailure: return 2;
			case telemetry::Error::InternalFailure: return 3;
		}
		return 4;
	};
	static const char* invalidCodes[] = {
		"scene.arc.curvature.invalid",
		"scene.arc.gradient.invalid",
		"scene.arc.speed.invalid",
		"scene.basetime.invalid",
		"scene.basetime.missing",
		"scene.block.length.invalid",
		"scene.buffer.invalid",
		"scene.capacity.runtime",
		"scene.compatibility.schema",
		"scene.composition.empty",
		"scene.connection.speed.invalid",
		"scene.delay.invalid",
		"scene.duration.invalid",
		"scene.duration.missing",
		"scene.dwell.exceeds_window",
		"scene.dwell.invalid",
		"scene.entrance.conflict",
		"scene.entrance.occurrence.out_of_horizon",
		"scene.entrance.station",
		"scene.entrance.timetable",
		"scene.field.missing",
		"scene.id.duplicate",
		"scene.id.empty",
		"scene.id.reserved",
		"scene.incident.fields",
		"scene.incident.speed",
		"scene.incident.type",
		"scene.incident.window",
		"scene.item.invalid",
		"scene.json.parse",
		"scene.name.path",
		"scene.native.block.clipped",
		"scene.node.coordinate.invalid",
		"scene.occurrence.invalid",
		"scene.passenger.continuity",
		"scene.passenger.leg.order",
		"scene.passenger.leg.stop",
		"scene.passenger.legs.empty",
		"scene.passenger.occurrence.out_of_horizon",
		"scene.passenger.window",
		"scene.performance.invalid",
		"scene.platform.capacity.invalid",
		"scene.platform.length.invalid",
		"scene.platform.node.conflict",
		"scene.platform.nodes.none",
		"scene.platform.width.invalid",
		"scene.platforms.none",
		"scene.recovery.invalid",
		"scene.ref.ambiguous",
		"scene.ref.platform",
		"scene.ref.platform.route",
		"scene.ref.stop.order",
		"scene.ref.stop.route",
		"scene.ref.unresolved",
		"scene.repeat.count.invalid",
		"scene.repeat.invalid",
		"scene.repeat.step.invalid",
		"scene.route.direction",
		"scene.route.disconnected",
		"scene.route.empty",
		"scene.route.region_jump",
		"scene.route_choice.passengers.none",
		"scene.routes.none",
		"scene.scenario.default.missing",
		"scene.scenarios.none",
		"scene.section.missing",
		"scene.services.none",
		"scene.signal.binding.missing",
		"scene.signal.binding.unresolved",
		"scene.signalling_area.conflict",
		"scene.signalling_area.level",
		"scene.signalling_area.range",
		"scene.speed.invalid",
		"scene.station.anchor.missing",
		"scene.station.position.invalid",
		"scene.stations.none",
		"scene.stop.off_route.context",
		"scene.time.departure.missing",
		"scene.time.entry.invalid",
		"scene.time.invalid",
		"scene.time.order",
		"scene.topology.ambiguous",
		"scene.topology.arcs.missing",
		"scene.topology.arcs.none",
		"scene.topology.blocks.missing",
		"scene.topology.blocks.none",
		"scene.topology.disconnected",
		"scene.topology.loop",
		"scene.topology.nodes.missing",
		"scene.topology.nodes.none",
		"scene.topology.order",
		"scene.topology.track",
		"scene.topology.tracks.none",
		"scene.train.physical.missing",
		"scene.train.traction.empty",
		"scene.train.traction.interval",
		"scene.train.traction.order",
		"scene.train.traction.overlap",
		"scene.trains.none",
		"scene.version.missing",
	};
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.severity != SceneSeverity::Error) continue;
		std::optional<telemetry::Error> error;
		if (diagnostic.code == "scene.version.unsupported" || diagnostic.code == "scene.units.unsupported")
			error = telemetry::Error::UnsupportedFormat;
		else if (std::any_of(std::begin(invalidCodes), std::end(invalidCodes),
					 [&](const char* code) { return diagnostic.code == code; }))
			error = telemetry::Error::InvalidInput;
		else if (diagnostic.code == "scene.bundle.file.read" || diagnostic.code == "scene.bundle.temp")
			error = telemetry::Error::IoFailure;
		// Ambiguous compatibility.manifest/bundle.schema/save.write/bundle.write codes are omitted.
		if (error && (!selected || priority(*error) < priority(*selected))) selected = error;
	}
	return selected;
}

void attachRunProvenance(DiagramWindow* window, RunProvenance provenance) {
	window->setProvenanceWriter([provenance = std::move(provenance)](
									const QString& path, const char* kind, const std::string& bytes) {
		return writeRunArtifactWithProvenance(path.toStdString(), kind, bytes, provenance);
	});
}

void populateSceneSectionCombo(QComboBox* combo, const SceneSectionInventory& inventory,
	const std::string& current, bool allowNone, const QString& noneLabel) {
	if (!combo)
		return;
	combo->clear();
	if (allowNone)
		combo->addItem(noneLabel, QString());
	for (const auto& section : inventory.sections)
		combo->addItem(InfrastructurePanel::sectionLabel(section), QString::fromStdString(section.id));

	int selected = combo->findData(QString::fromStdString(current));
	if (selected < 0 && !current.empty()) {
		if (const auto* resolved = inventory.resolve(current)) {
			combo->addItem(QStringLiteral("%1 (legacy: %2)")
							   .arg(InfrastructurePanel::sectionLabel(*resolved), QString::fromStdString(current)),
				QString::fromStdString(current));
		} else {
			combo->addItem(QStringLiteral("Invalid: %1").arg(QString::fromStdString(current)),
				QString::fromStdString(current));
		}
		selected = combo->count() - 1;
	} else if (selected < 0 && current.empty() && !allowNone) {
		combo->addItem(QStringLiteral("Invalid: (empty)"), QString());
		selected = combo->count() - 1;
	}
	combo->setCurrentIndex(selected >= 0 ? selected : (combo->count() > 0 ? 0 : -1));
}

bool sceneHasTrack(const SceneModel& scene, const std::string& trackId) {
	return std::any_of(scene.tracks.begin(), scene.tracks.end(),
		[&trackId](const SceneTrack& track) { return track.id == trackId; });
}

// The signalling systems in level order. A stored level that is no system gets one item on top that stands for it.
void populateSignallingSystemCombo(QComboBox* combo, int current) {
	if (!combo)
		return;
	combo->clear();
	if (!isValidSignallingLevel(current)) {
		const bool unchosen = current == kSignallingLevelUnset;
		combo->addItem(unchosen ? QStringLiteral("Choose a signalling system")
								: QString::fromStdString(signallingLevelName(current)),
			current);
		combo->setItemData(0,
			unchosen ? QStringLiteral("No system is chosen yet. Choose the system that governs the sections inside this area.")
					 : QString::fromStdString(signallingLevelDescription(current)),
			Qt::ToolTipRole);
	}
	for (int level = levelValue(SignallingLevel::Atb); level <= levelValue(SignallingLevel::Bacc); ++level) {
		combo->addItem(QString::fromStdString(signallingLevelName(level)), level);
		combo->setItemData(combo->count() - 1, QString::fromStdString(signallingLevelDescription(level)), Qt::ToolTipRole);
	}
	combo->setCurrentIndex(combo->findData(current));
}

// All tracks, then each track of the scene with the extent of its blocks. A track that is no track of the scene
// gets one item on top.
void populateSignallingAreaTrackCombo(QComboBox* combo, const SceneModel& scene,
	const SceneSectionInventory& inventory, const std::string& current) {
	if (!combo)
		return;
	combo->clear();
	if (!current.empty() && !sceneHasTrack(scene, current))
		combo->addItem(QStringLiteral("Invalid track: %1").arg(QString::fromStdString(current)),
			QString::fromStdString(current));
	combo->addItem(QStringLiteral("(all tracks)"), QString());
	for (const SceneTrack& track : scene.tracks) {
		const QString id = QString::fromStdString(track.id);
		const SceneBlockExtent extent = sceneBlockExtent(inventory, track.id);
		combo->addItem(extent.found
				? QStringLiteral("%1 (blocks %2 to %3 km)")
					  .arg(id, QString::fromStdString(formatSceneSectionCoordinate(extent.startKm)),
						  QString::fromStdString(formatSceneSectionCoordinate(extent.endKm)))
				: QStringLiteral("%1 (no blocks)").arg(id),
			id);
	}
	combo->setCurrentIndex(combo->findData(QString::fromStdString(current)));
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
		QMessageBox::warning(parent, "Export failed",
			QString("Could not export the data and provenance to:\n%1").arg(path));
	}
}

void addLoadedDataTreeItem(QTreeWidget* tree, QTreeWidgetItem* parent, const SceneLoadedData& item) {
	auto* row = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
	QString label = QString::fromStdString(item.category);
	const bool isEntityId = item.targetType == "train_unit" || item.targetType == "composition"
		|| item.targetType == "service" || item.targetType == "incident";
	if (!isEntityId && !label.contains(':')) {
		label.replace('_', ' ');
		if (!label.isEmpty())
			label[0] = label[0].toUpper();
		if (label == "Scene")
			label = "Case metadata";
		if (label == "Train units")
			label = "Rolling stock units";
	}
	row->setText(0, label);
	row->setText(1, QString::fromStdString(item.sourceFile));
	row->setText(2, QString::number(item.parsedCount));
	row->setText(3, QString::fromStdString(item.status));
	if (!item.targetType.empty()) {
		row->setData(0, kLoadedDataTargetTypeRole, QString::fromStdString(item.targetType));
		const QString tooltip = item.targetType == "network"
			? QStringLiteral("Activate this row to focus the existing network view.")
			: (item.targetType == "validation"
					  ? QStringLiteral("Activate this row to open the existing validation table.")
					  : (item.targetType == "train_unit_plot"
								? QStringLiteral("Activate this row to plot this rolling stock unit's tractive effort.")
								: QStringLiteral("Activate this row to open the existing editor.")));
		row->setToolTip(0, tooltip);
	}
	for (const auto& child : item.children)
		addLoadedDataTreeItem(tree, row, child);
}

bool sceneDropCandidate(const QMimeData* mime, QString* path, QString* rejection) {
	if (!mime || !mime->hasUrls() || mime->urls().isEmpty()) {
		*rejection = QStringLiteral("no file was dropped");
		return false;
	}
	if (mime->urls().size() != 1) {
		*rejection = QStringLiteral("drop exactly one .egscene file");
		return false;
	}
	const QUrl url = mime->urls().front();
	if (!url.isLocalFile()) {
		*rejection = QStringLiteral("only local .egscene files can be opened");
		return false;
	}
	const QFileInfo info(url.toLocalFile());
	if (!info.exists()) {
		*rejection = QStringLiteral("the dropped file does not exist");
		return false;
	}
	if (!info.isFile()) {
		*rejection = QStringLiteral("drop an .egscene file, not a directory");
		return false;
	}
	if (info.suffix().compare(QStringLiteral("egscene"), Qt::CaseInsensitive) != 0) {
		*rejection = QStringLiteral("the dropped file must use the .egscene extension");
		return false;
	}
	*path = info.absoluteFilePath();
	return true;
}

bool e2eDialogsSuppressed() {
	return startupTimingEnabled()
		|| PlaybackProfiler::enabled()
		|| mainWindowTestHooks().dialogsSuppressed;
}

void showBlockingError(QWidget* parent, const QString& title, const QString& message, bool warningIcon = false) {
	UserPrompts(e2eDialogsSuppressed).showError(parent, title, message, warningIcon);
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

std::vector<std::string> splitCommaList(const QString& text) {
	std::vector<std::string> result;
	for (const QString& part : text.split(',', Qt::KeepEmptyParts)) {
		const QString trimmed = part.trimmed();
		if (!trimmed.isEmpty())
			result.push_back(trimmed.toStdString());
	}
	return result;
}

QString joinCommaList(const std::vector<std::string>& values) {
	QStringList result;
	for (const auto& value : values)
		result << QString::fromStdString(value);
	return result.join(", ");
}

QStringList directDeleteConsumers(const SceneModel& model, const QString& facet, const std::string& id,
	const std::string& scope = {}) {
	QStringList consumers;
	for (const std::string& consumer : sceneDirectDeleteConsumers(model, facet.toStdString(), id, scope))
		consumers << QString::fromStdString(consumer);
	return consumers;
}

std::string platformSelectionKey(const std::string& stationId, const std::string& platformId) {
	return stationId + "\n" + platformId;
}

// The headers of the infrastructure table of a facet.
QStringList infrastructureColumns(const QString& facet) {
	if (facet == "blocks")
		return {"ID", "Track ID", "Length (km)", "Order", "Start km", "End km", "Coverage"};
	if (facet == "stations")
		return {"ID", "Name", "Has position", "Position km"};
	if (facet == "platforms")
		return {"Station ID", "ID", "Nodes", "Length (m)", "Width (m)"};
	if (facet == "signals")
		return {"ID", "Protected section"};
	if (facet == "signalling_areas")
		return {"ID", "Start km", "End km", "Signalling system", "Track"};
	if (facet == "routes")
		return {"ID", "Sections", "Has corridor", "Corridor", "Reversed"};
	if (facet == "block_dependencies")
		return {"Block", "Depends on"};
	if (facet == "single_track_restrictions")
		return {"Start", "End", "Protected start", "Protected end"};
	return {"Entrance", "Exit", "Direction"};
}

} // namespace

// A facet of the infrastructure panel whose entities the window still lists, edits, adds and deletes itself.
class MainWindow::UnmovedInfrastructureFacet : public InfrastructureFacet {
public:
	UnmovedInfrastructureFacet(MainWindow& window, QString key) : m_window(window), m_key(std::move(key)) {}

	QString key() const override { return m_key; }
	QStringList columns() const override { return infrastructureColumns(m_key); }
	void fillRows(InfrastructurePanel&) override { m_window.fillInfrastructureRows(m_key); }
	QString selectionId(const QTableWidget& table, int row) const override {
		const QTableWidgetItem* station = table.item(row, 0);
		const QTableWidgetItem* platform = table.item(row, 1);
		if (m_key == QStringLiteral("platforms") && station && platform)
			return QString::fromStdString(platformSelectionKey(station->text().toStdString(), platform->text().toStdString()));
		return InfrastructureFacet::selectionId(table, row);
	}
	void commitCell(InfrastructurePanel&, int row, int column) override { m_window.commitInfrastructureCell(row, column); }
	void add(InfrastructurePanel&) override { m_window.addInfrastructureEntity(); }
	void remove(InfrastructurePanel&) override { m_window.deleteInfrastructureEntity(); }
	void commitPendingEdits(InfrastructurePanel&) override {
		if (m_key == QStringLiteral("platforms"))
			m_window.commitPendingPlatformGeometry();
	}

private:
	MainWindow& m_window;
	QString m_key;
};

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
										  ui(new Ui::MainWindow),
										  m_worker(nullptr),
										  m_workerThread(nullptr) {
	QFile themeFile(":/styles/egtrain.qss");
	if (themeFile.open(QIODevice::ReadOnly | QIODevice::Text))
		qApp->setStyleSheet(QString::fromUtf8(themeFile.readAll()));

	ui->setupUi(this);
	m_userPrompts = std::make_unique<UserPrompts>(e2eDialogsSuppressed);
	m_history = std::make_unique<SceneHistory>();
	connect(m_document, &SceneDocument::changed, this, &MainWindow::sceneDocumentChanged);
	setupEditMenu();
	setupUpdateActions();
	if (ui->menuHelp) {
		auto* privacy = ui->menuHelp->addAction(QStringLiteral("Privacy && diagnostics..."));
		privacy->setObjectName(QStringLiteral("actionPrivacyDiagnostics"));
		privacy->setShortcut(QKeySequence::Preferences);
		connect(privacy, &QAction::triggered, this, [this] { showPrivacySettings(false); });
	}
	m_startOffsetSeconds = initial_variables.startingSimulationTime;

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

	// setup info dock widget
	setupInfoDockWidget();
	setupRunResultsDock();

	// create container of items to show on display area
	scene = new NetworkScene(networkView);
	m_presenter = std::make_unique<NetworkPresenter>(scene.data(), networkView);
	m_presenter->setGeometrySource(makeRuntimeGeometryFromRun(simulation));
	m_presenter->setFrameHost(this);
	networkView->setScene(scene);

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
			if (PlaybackProfiler::instance().freeze(currentTimestep) && m_worker)
				m_worker->requestStop();
		});
	});
	networkView->setMouseTracking(true);

	// connect elements from UI
	connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::handleHelpAbout);
	connect(ui->actionSimulationStart, &QAction::triggered, this, &MainWindow::runCurrent);
	connect(ui->actionSimulationStop, &QAction::triggered, this, [this]() {
		if (m_worker) {
			m_worker->requestStop();
			// Stop is a lifecycle boundary for transient playback overlays. The
			// worker may still publish one queued snapshot while it exits; the
			// delivery guard below drops it instead of repainting these states.
			m_presenter->clearOperationalTrackStates();
			m_followCamera->stop();
		}
	});
	connect(infoDockWidget, &InfoDockWidget::closed, this, &MainWindow::handleCloseInfoDockWidget);

	connect(ui->displayTrainPathDiagrams, &QAction::triggered, this, &MainWindow::displayTrainPathDiagrams);

	connect(ui->actionLoad_Network, &QAction::triggered, this, &MainWindow::actionLoad_Network);

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
	m_recentScenesMenu = new QMenu("Recent Scenes", this);
	connect(m_newSceneAction, &QAction::triggered, this, &MainWindow::newScene);
	connect(openSceneAction, &QAction::triggered, this, &MainWindow::openSceneDialog);
	connect(openSceneFolderAction, &QAction::triggered, this, &MainWindow::openSceneFolderDialog);
	connect(m_saveSceneAction, &QAction::triggered, this, &MainWindow::saveScene);
	connect(m_saveSceneAsAction, &QAction::triggered, this, &MainWindow::saveSceneAs);
	connect(m_saveSceneAsFolderAction, &QAction::triggered, this, [this]() { saveSceneAsToDirectory(); });
	connect(m_runSceneAction, &QAction::triggered, this, &MainWindow::runScene);
	if (ui->menuFile) {
		QAction* beforeAction = ui->actionLoad_Network;
		ui->menuFile->insertAction(beforeAction, m_newSceneAction);
		ui->menuFile->insertAction(beforeAction, openSceneAction);
		ui->menuFile->insertAction(beforeAction, openSceneFolderAction);
		ui->menuFile->insertAction(beforeAction, m_saveSceneAction);
		ui->menuFile->insertAction(beforeAction, m_saveSceneAsAction);
		ui->menuFile->insertAction(beforeAction, m_saveSceneAsFolderAction);
		ui->menuFile->insertAction(beforeAction, m_runSceneAction);
		ui->menuFile->insertMenu(beforeAction, m_recentScenesMenu);
		ui->menuFile->insertSeparator(beforeAction);
	}
	rebuildRecentScenesMenu();
	updateSceneActions();

	// connect scene signals
	connect(scene, &NetworkScene::MousePressedOnPreview, this, &MainWindow::displayPreviewInfo);
	connect(scene, &NetworkScene::MousePressedOnNode, this, &MainWindow::displayNodeInfo);
	connect(scene, &NetworkScene::MousePressedOnStationNode, this, &MainWindow::displayStationNodeInfo);
	connect(scene, &NetworkScene::MousePressedOnArc, this, &MainWindow::displayArcInfo);
	connect(scene, &NetworkScene::MousePressedOnConnection, this, &MainWindow::displayConnectionInfo);
	connect(scene, &NetworkScene::MousePressedOnSignal, this, &MainWindow::displaySignallingInfo);
	connect(scene, &NetworkScene::MousePressedOnTrain, this, &MainWindow::displayTrainInfo);
	connect(scene, &NetworkScene::MousePressedOnPassenger, this, &MainWindow::displayPassengerInfo);
	connect(scene, &NetworkScene::ContextMenuRequested, this, &MainWindow::showSceneContextMenu);
	connect(scene, &NetworkScene::DisableHighlight, this, &MainWindow::handleDisableHighlight);

	// connect signals from EGTRAIN simulation
	connect(&simulation, &DispatchController::snapshotAvailable, this, &MainWindow::waitForUpdates,
		Qt::QueuedConnection);

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
		const int delayMs = stepDelayForSlider(value);
		updateSpeedModeDisplay(delayMs);
		if (m_worker)
			m_worker->setDelayMs(delayMs);
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

	connect(ui->actionStartTime, &QAction::triggered, this, &MainWindow::setStartTime);
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
		connect(m_outputFolderAction, &QAction::triggered, this, &MainWindow::chooseOutputFolder);
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
	m_validationDock = new QDockWidget("Scene Validation", this);
	m_validationDock->setObjectName("sceneValidationDock");
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
	m_validationDock->hide();
	tabifyDockWidget(m_logPane, m_validationDock);
	m_validationDock->toggleViewAction()->setShortcut(QKeySequence("Ctrl+Shift+V"));
	if (ui->menuView)
		ui->menuView->addAction(m_validationDock->toggleViewAction());

	m_loadedDataDock = new QDockWidget("Loaded Data", this);
	m_loadedDataDock->setObjectName("loadedDataDock");
	m_loadedDataTree = new QTreeWidget(m_loadedDataDock);
	m_loadedDataTree->setColumnCount(4);
	m_loadedDataTree->setHeaderLabels(QStringList() << "Category" << "Source" << "Count" << "Status");
	m_loadedDataTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_loadedDataTree->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_loadedDataTree->setUniformRowHeights(true);
	m_loadedDataTree->setToolTip("Activate rows to open existing editors.");
	m_loadedDataTree->setAccessibleDescription("Loaded case data. Activatable rows open existing editors.");
	m_loadedDataTree->header()->setStretchLastSection(true);
	m_loadedDataDock->setWidget(m_loadedDataTree);
	addDockWidget(Qt::BottomDockWidgetArea, m_loadedDataDock);
	m_loadedDataDock->hide();
	tabifyDockWidget(m_validationDock, m_loadedDataDock);
	tabifyDockWidget(m_loadedDataDock, m_runResultsDock);
	if (ui->menuView)
		ui->menuView->addAction(m_loadedDataDock->toggleViewAction());
	connect(m_loadedDataTree, &QTreeWidget::itemActivated, this,
		[this](QTreeWidgetItem* item, int) { activateLoadedDataItem(item); });

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
		updateDiagnosticPresentation();
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
	for (const QString& key : InfrastructurePanel::facetKeys())
		if (!m_infrastructurePanel->facet(key))
			m_infrastructurePanel->setFacet(std::make_unique<UnmovedInfrastructureFacet>(*this, key));
	m_infrastructurePanel->setDocument(m_document);
	connect(m_infrastructurePanel, &InfrastructurePanel::selectionChanged, this, [this]() {
		if (m_sceneLoaded && !m_worker && !m_resultsAvailable)
			renderTrackPreview(m_sceneModel);
	});
	connect(m_infrastructurePanel, &InfrastructurePanel::changed, this, &MainWindow::refreshValidationPanel);
	connect(m_infrastructurePanel, &InfrastructurePanel::statusMessage, this,
		[this](const QString& text, int timeoutMs) { statusBar()->showMessage(text, timeoutMs); });
	connect(m_infrastructureDock, &QDockWidget::visibilityChanged, this,
		[this](bool visible) {
			if (visible)
				m_infrastructurePanel->updateSelection();
		});
	connect(m_insertBlockButton, &QPushButton::clicked, this, &MainWindow::insertBlock);
	connect(m_moveBlockUpButton, &QPushButton::clicked, this, [this]() { moveBlock(-1); });
	connect(m_moveBlockDownButton, &QPushButton::clicked, this, [this]() { moveBlock(1); });
	connect(m_addRouteSectionButton, &QPushButton::clicked, this, &MainWindow::addRouteSection);
	connect(m_removeRouteSectionButton, &QPushButton::clicked, this, &MainWindow::removeRouteSection);
	connect(m_moveRouteSectionUpButton, &QPushButton::clicked, this, &MainWindow::moveRouteSectionUp);
	connect(m_moveRouteSectionDownButton, &QPushButton::clicked, this, &MainWindow::moveRouteSectionDown);
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
		const SceneTrainUnit* unit = sceneTrainUnitById(m_sceneModel, newId.toStdString());
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
		if (const SceneTrainUnit* unit = sceneTrainUnitById(m_sceneModel, unitId.toStdString()))
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
	incidentLayout->addWidget(m_scenarioPanel, 0);

	QTabWidget* scenarioEditorTabs = new QTabWidget(incidentWidget);
	scenarioEditorTabs->setObjectName("scenarioEditorTabs");

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
	scenarioEditorTabs->addTab(m_entranceDelayPanel, "Entrance Delays");
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
	m_scenarioPanel->setDiagnostics(&m_sceneDiagnostics);
	m_scenarioPanel->setAdvancedDetails([this] { return advancedDetailsEnabled(); });
	m_scenarioPanel->setExportObserver([this] {
		const auto operation = captureTelemetryOperation();
		return [operation](const std::vector<SceneDiagnostic>& diagnostics) {
			operation.failure(telemetry::Operation::Export, telemetryDiagnosticError(diagnostics));
		};
	});
	m_scenarioPanel->setDocument(m_document);
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
	}
	addDockWidget(Qt::RightDockWidgetArea, m_passengerDock);
	m_passengerDock->hide();
	tabifyDockWidget(m_serviceDock, m_passengerDock);
	registerEditorDock(m_passengerDock);

	m_passengerPanel->setPrompts(m_userPrompts.get());
	m_passengerPanel->setDiagnostics(&m_sceneDiagnostics);
	m_passengerPanel->setOccurrenceDuration([this] { return serviceOccurrenceDuration(); });
	m_passengerPanel->setImportFolderChooser([this]() -> QString {
		if (const auto& passengerSource = mainWindowTestHooks().passengerSource) {
			const QString sourcePath = passengerSource();
			if (!sourcePath.isEmpty())
				return sourcePath;
		}
		const QString startDir = m_sceneDir.isEmpty() ? QDir::homePath() : QFileInfo(m_sceneDir).absolutePath();
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

	// permanent status-bar field for the scene validation summary, so it does
	// not clobber transient load/save messages
	m_validationStatusLabel = new QLabel(this);
	statusBar()->addPermanentWidget(m_validationStatusLabel);
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
	statusBar()->showMessage(QString("Ready - %1 (%2 tracks, %3 routes)")
			.arg(QString::fromStdString(initial_variables.name))
			.arg(initial_variables.numTrackLines)
			.arg(initial_variables.N_Routes));

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
	if (m_telemetrySender) m_telemetrySender->stop();
	clearSimulationWorker(true);
	delete ui;
	if (const auto windowDestroyed = mainWindowTestHooks().windowDestroyed)
		windowDestroyed();
}

void MainWindow::closeEvent(QCloseEvent* event) {
	const bool confirmed = m_closeConfirmed;
	m_closeConfirmed = false;
	if (!confirmed && !maybeSaveScene()) {
		event->ignore();
		return;
	}
	// A running simulation is stopped first. The window closes when its worker has finished.
	if (deferUntilRunStopped([this] { m_closeConfirmed = true; close(); })) {
		event->ignore();
		return;
	}
	clearSimulationWorker(true);
	QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
	QString path;
	QString rejection;
	if (sceneDropCandidate(event->mimeData(), &path, &rejection)
		&& event->possibleActions().testFlag(Qt::CopyAction)) {
		event->setDropAction(Qt::CopyAction);
		event->accept();
	} else {
		if (rejection.isEmpty())
			rejection = QStringLiteral("the source does not permit copying");
		statusBar()->showMessage(QStringLiteral("Drop rejected: %1. Scene unchanged.").arg(rejection), 8000);
		event->ignore();
	}
}

void MainWindow::dropEvent(QDropEvent* event) {
	QString path;
	QString rejection;
	if (!sceneDropCandidate(event->mimeData(), &path, &rejection)
		|| !event->possibleActions().testFlag(Qt::CopyAction)) {
		if (rejection.isEmpty())
			rejection = QStringLiteral("the source does not permit copying");
		statusBar()->showMessage(QStringLiteral("Drop rejected: %1. Scene unchanged.").arg(rejection), 8000);
		event->ignore();
		return;
	}

	if (!requestOpenScene(path)) {
		event->ignore();
		return;
	}
	event->setDropAction(Qt::CopyAction);
	event->accept();
}

namespace {
// The next run writes to the folder of the scene that the window holds. A folder from Choose
// Output Folder stays in force: the controller applies it to every run it prepares.
void selectSceneOutputFolder(const std::string& sceneName) {
	if (simulation.nextRunOutputFolder().empty())
		initial_variables.OutputMainFolder = sceneOutputDirectory(sceneName).toStdString();
}
} // namespace

void MainWindow::newScene() {
	const auto operation = captureTelemetryOperation();
	if (!maybeSaveScene())
		return;
	if (deferUntilRunStopped([this, operation] { createNewScene(operation); }))
		return;
	createNewScene(operation);
}

void MainWindow::createNewScene(const telemetry::OperationObservation& operation) {
	m_runSelection.includeAll();
	m_lastRunSelectedOccurrences = 0;
	m_lastRunTotalOccurrences = 0;
	teardownGUI();
	simulation.resetState();
	m_sceneDir.clear();
	m_savedSceneSha256.clear();
	m_document->replace(makeNewSceneModel(), true);
	m_history->reset(m_sceneModel);
	selectSceneOutputFolder(m_sceneModel.name);
	if (m_delayBaseline)
		m_delayBaselineStatus = QStringLiteral(
			"Delay baseline cleared: a new case study was created. Complete a run with no incidents or entrance delays to set a new baseline.");
	else
		m_delayBaselineStatus.clear();
	m_delayBaseline.reset();
	m_sceneIsBundle = false;
	m_sceneBundleVersion.reset();
	m_runSelection.selectScenario(m_sceneModel.defaultScenarioId);
	m_runSelection.clearModifiedScenarios();
	m_sceneDiagnostics.clear();
	m_passengerPanel->clearImportResult();
	m_startOffsetSeconds = baseTimeToSeconds(m_sceneModel.baseTime);
	invalidateRunResults();
	updateSceneWindowTitle();
	updateCaseLayersPanel();
	updateSceneActions();
	statusBar()->showMessage("New case study created; save it to choose a location");
	refreshIncidentPanel();
	refreshInfrastructurePanel();
	refreshValidationPanel();
	renderTrackPreview(m_sceneModel);
	telemetry::revealEditor(m_caseSettingsDock, operation);
	telemetry::revealEditor(m_infrastructureDock, operation);
}

void MainWindow::openSceneDialog() {
	const auto operation = captureTelemetryOperation();
	const QString startDir = m_sceneDir.isEmpty() ? QDir::homePath() : QFileInfo(m_sceneDir).absolutePath();
	const QString path = QFileDialog::getOpenFileName(this, "Open Case Study", startDir,
		"EGTRAIN Case Study (*.egscene)");
	if (path.isEmpty())
		return;

	requestOpenScene(path, operation);
}

void MainWindow::openSceneFolderDialog() {
	const auto operation = captureTelemetryOperation();
	const QString startDir = m_sceneDir.isEmpty() ? QDir::homePath() : QFileInfo(m_sceneDir).absolutePath();
	const QString dir = QFileDialog::getExistingDirectory(this, "Open Scene Folder", startDir);
	if (!dir.isEmpty())
		requestOpenScene(dir, operation);
}

telemetry::OperationObservation MainWindow::captureTelemetryOperation() const noexcept {
	return telemetry::OperationObservation(m_telemetrySender.get());
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

bool MainWindow::requestOpenScene(const QString& path) {
	return requestOpenScene(path, captureTelemetryOperation());
}

bool MainWindow::requestOpenScene(const QString& path, const telemetry::OperationObservation& operation) {
	if (path.isEmpty())
		return false;
	if (!maybeSaveScene()) {
		statusBar()->showMessage("Open canceled. Current scene retained.", 8000);
		return false;
	}
	// The request is accepted here. The scene opens when the running simulation has stopped.
	if (deferUntilRunStopped([this, path, operation] { openRequestedScene(path, operation); }))
		return true;
	return openRequestedScene(path, operation);
}

bool MainWindow::openRequestedScene(const QString& path, const telemetry::OperationObservation& operation) {
	if (openSceneDirectory(path, operation))
		return true;
	statusBar()->showMessage(QString("Scene unchanged: %1 could not be opened.")
								 .arg(QFileInfo(path).fileName()),
		8000);
	return false;
}

bool MainWindow::openSceneDirectory(const QString& dir) {
	return openSceneDirectory(dir, captureTelemetryOperation());
}

bool MainWindow::openSceneDirectory(const QString& dir, const telemetry::OperationObservation& operation) {
	const QString scenePath = QFileInfo(dir).absoluteFilePath();
	const SceneCompatibilityProbeResult compatibility = probeSceneCompatibility(scenePath.toStdString());
	const auto compatibilityMessage = [&compatibility]() {
		if (!compatibility.diagnostics.empty())
			return QString::fromStdString(toDisplayText(compatibility.diagnostics.front()));
		return QStringLiteral("The scene format could not be identified.");
	};
	// Scene compatibility prompts are suppressed only for unattended app/E2E
	// runs. The update-only disable setting must not hide scene safety prompts.
	const bool noCompatibilityDialogs = e2eDialogsSuppressed();
	if (compatibility.classification == SceneCompatibilityClass::OlderMigratable) {
		if (noCompatibilityDialogs) {
			statusBar()->showMessage("Older scene requires an interactive upgrade copy");
			return false;
		}
		QMessageBox dialog(this);
		dialog.setIcon(QMessageBox::Question);
		dialog.setWindowTitle("Older Scene");
		QString ageDetails;
		if (compatibility.schemaVersion < kCurrentSceneSchemaVersion)
			ageDetails = QString("schema %1 (this application uses schema %2)")
							 .arg(compatibility.schemaVersion)
							 .arg(kCurrentSceneSchemaVersion);
		if (compatibility.sourceKind == SceneSourceKind::Bundle && compatibility.bundleVersion
			&& *compatibility.bundleVersion < kCurrentSceneBundleVersion) {
			if (!ageDetails.isEmpty())
				ageDetails += "; ";
			ageDetails += QString("bundle layout %1 (this application uses bundle layout %2)")
							  .arg(*compatibility.bundleVersion)
							  .arg(kCurrentSceneBundleVersion);
		}
		if (ageDetails.isEmpty())
			ageDetails = QStringLiteral("an older scene layout");
		dialog.setText(QString("%1 uses %2.")
				.arg(QFileInfo(scenePath).fileName(), ageDetails));
		dialog.setInformativeText("Upgrade a copy; the original scene will remain unchanged.");
		QPushButton* upgrade = dialog.addButton("Upgrade a Copy...", QMessageBox::AcceptRole);
		dialog.addButton("Cancel", QMessageBox::RejectRole);
		dialog.exec();
		if (dialog.clickedButton() != upgrade)
			return false;
		const QFileInfo sourceInfo(scenePath);
		QString destination;
		if (compatibility.sourceKind == SceneSourceKind::Bundle) {
			const QString base = sourceInfo.completeBaseName() + "-upgraded.egscene";
			destination = QFileDialog::getSaveFileName(this, "Upgrade Scene Copy",
				QDir(sourceInfo.absolutePath()).filePath(base), "EGTRAIN Case Study (*.egscene)");
		} else {
			// A migration destination must not already exist. A save-style chooser
			// lets the user name a sibling directory without selecting an existing one.
			destination = QFileDialog::getSaveFileName(this, "Upgrade Scene Copy",
				QDir(sourceInfo.absolutePath()).filePath(sourceInfo.fileName() + "-upgraded"),
				"EGTRAIN Scene Directory (*)");
		}
		if (destination.isEmpty())
			return false;
		const SceneMigrationResult migrated = migrateSceneCopy(scenePath.toStdString(),
			destination.toStdString());
		if (!migrated.success()) {
			operation.failure(telemetry::Operation::SceneOpen, telemetryDiagnosticError(migrated.diagnostics));
			QString message = firstDiagnosticMessage(migrated.diagnostics);
			if (message.isEmpty())
				message = compatibilityMessage();
			showBlockingError(this, "Cannot Upgrade Scene", message);
			return false;
		}
		return openSceneDirectory(destination, operation);
	}
	if (compatibility.classification == SceneCompatibilityClass::OlderUnsupported) {
		operation.failure(telemetry::Operation::SceneOpen, telemetry::Error::UnsupportedFormat);
		if (!noCompatibilityDialogs)
			showBlockingError(this, "Older Scene Not Supported",
				"This scene uses an older schema or bundle layout with no registered migration path.\n\n"
					+ compatibilityMessage());
		return false;
	}
	if (compatibility.classification == SceneCompatibilityClass::Newer) {
		if (noCompatibilityDialogs) {
			operation.failure(telemetry::Operation::SceneOpen, telemetry::Error::UnsupportedFormat);
			statusBar()->showMessage("Newer scene format requires a newer EGTRAIN version");
			return false;
		}
		QMessageBox dialog(this);
		dialog.setIcon(QMessageBox::Question);
		dialog.setWindowTitle("Newer Scene");
		dialog.setText("This scene was saved with a newer scene format.");
		QString details = QString("Supported schema: %1; scene schema: %2")
							  .arg(kCurrentSceneSchemaVersion)
							  .arg(compatibility.schemaVersion);
		if (compatibility.bundleVersion)
			details += QString("; supported bundle: %1; scene bundle: %2")
						   .arg(kCurrentSceneBundleVersion)
						   .arg(*compatibility.bundleVersion);
		dialog.setInformativeText(details);
		QPushButton* updates = dialog.addButton("Check for Updates...", QMessageBox::AcceptRole);
		dialog.addButton("Cancel", QMessageBox::RejectRole);
		dialog.exec();
		if (dialog.clickedButton() == updates) {
			operation.failure(telemetry::Operation::SceneOpen, telemetry::Error::UnsupportedFormat);
			startUpdateCheck(true);
		}
		return false;
	}
	if (compatibility.classification == SceneCompatibilityClass::Malformed) {
		operation.failure(telemetry::Operation::SceneOpen, telemetryDiagnosticError(compatibility.diagnostics));
		if (!noCompatibilityDialogs)
			showBlockingError(this, cannot_open::title(), cannot_open::text(scenePath, compatibilityMessage()));
		return false;
	}
	const bool sceneIsBundle = compatibility.sourceKind == SceneSourceKind::Bundle;
	const bool reloadingSameScene = m_sceneLoaded
		&& QFileInfo(m_sceneDir).absoluteFilePath() == scenePath;
	const qint64 loadStarted = startupTimingEnabled() ? startupTimingNowNanoseconds() : 0;
	auto result = loadScenePath(scenePath.toStdString());
	const bool timingIdentityOk = !startupTimingEnabled()
		|| (sameStartupIdentity(g_startupIdentity,
				startupIdentity(scenePath, result.scene, result.inputSnapshot))
			&& g_startupIdentity.inputSnapshot == result.inputSnapshot);
	if (startupTimingEnabled())
		recordStartupTiming(QStringLiteral("canonical_load"), m_startupTimingIteration, -1,
			startupTimingNowNanoseconds() - loadStarted, QStringLiteral("scene_open"),
			QStringLiteral("MainWindow::openSceneDirectory"), timingIdentityOk);
	int errorCount = errorDiagnosticCount(result.diagnostics);
	if (errorCount > 0) {
		operation.failure(telemetry::Operation::SceneOpen, telemetryDiagnosticError(result.diagnostics));
		QString message = firstDiagnosticMessage(result.diagnostics);
		if (message.isEmpty())
			message = "Scene could not be opened.";
		message += QString("\n\nError count: %1").arg(errorCount);
		showBlockingError(this, cannot_open::title(), cannot_open::text(scenePath, message));
		return false;
	}

	teardownGUI();
	simulation.resetState();

	m_runSelection.includeAll();
	m_lastRunSelectedOccurrences = 0;
	m_lastRunTotalOccurrences = 0;
	m_sceneDir = scenePath;
	m_document->replace(result.scene, false);
	selectSceneOutputFolder(m_sceneModel.name);
	if (m_delayBaseline)
		m_delayBaselineStatus = QStringLiteral(
			"Delay baseline cleared: another case study was opened. Complete a run with no incidents or entrance delays to set a new baseline.");
	else
		m_delayBaselineStatus.clear();
	m_delayBaseline.reset();
	m_sceneIsBundle = sceneIsBundle;
	m_sceneBundleVersion = result.bundleVersion;
	m_savedSceneSha256 = hashSceneInputSnapshot(result.inputSnapshot);
	m_runSelection.selectScenario(m_sceneModel.defaultScenarioId);
	if (m_runSelection.scenarioId().empty() && !m_sceneModel.scenarios.empty()) {
		m_runSelection.selectScenario(m_sceneModel.scenarios.front().id);
		m_sceneModel.defaultScenarioId = m_runSelection.scenarioId();
	}
	m_history->reset(m_sceneModel, true);
	m_runSelection.clearModifiedScenarios();
	m_sceneDiagnostics.clear();
	m_passengerPanel->clearImportResult();
	invalidateRunResults();
	m_startOffsetSeconds = baseTimeToSeconds(m_sceneModel.baseTime);
	updateSceneWindowTitle();
	updateCaseLayersPanel();
	updateSceneActions();
	addRecentScene(scenePath);
	refreshIncidentPanel();
	refreshInfrastructurePanel(!reloadingSameScene);
	refreshValidationPanel();
	renderTrackPreview(m_sceneModel);
	statusBar()->showMessage(QString("%1: %2 (%3 services, %4 routes)")
			.arg(reloadingSameScene ? "Scene reloaded" : "Scene loaded")
			.arg(QString::fromStdString(m_sceneModel.name))
			.arg(static_cast<int>(m_sceneModel.services.size()))
			.arg(static_cast<int>(m_sceneModel.routes.size())));
	if (startupTimingEnabled()) {
		if (!timingIdentityOk) {
			failStartupTiming(QStringLiteral("scene identity, counts, or canonical input snapshot changed"));
			return false;
		}
		if (m_startupTimingScenePath.isEmpty())
			m_startupTimingScenePath = scenePath;
		networkView->armTimingPaint(QStringLiteral("preview"), m_startupTimingIteration);
	}
	if (m_loadedDataDock && advancedDetailsEnabled()) {
		m_loadedDataDock->show();
		m_loadedDataDock->raise();
	}
	operation.sceneOpened(sceneIsBundle ? telemetry::SceneKind::Bundled : telemetry::SceneKind::Local);
	return true;
}

void MainWindow::renderTrackPreview(const SceneModel& sceneModel) {
	clearPreviewInspection();
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
	options.platformBars = initial_variables.PAX_GUI;
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

void MainWindow::saveScene() {
	saveSceneToCurrentDir();
}

bool MainWindow::finishSceneSave(const SceneSaveResult& result,
	const telemetry::OperationObservation& operation, std::optional<telemetry::ExportKind> kind) {
	auto error = telemetryDiagnosticError(result.diagnostics);
	// The bundle save boundary checks this typed value before any filesystem attempt.
	if (kind == telemetry::ExportKind::SceneBundle && !result.success() && !result.writeAttempted
		&& m_sceneModel.schemaVersion != kCurrentSceneSchemaVersion)
		error = telemetry::Error::UnsupportedFormat;
	operation.exportFinished(kind, result.success(), result.writeAttempted, error);
	if (!result.success()) {
		QString message = firstDiagnosticMessage(result.diagnostics);
		if (message.isEmpty())
			message = "Scene could not be saved.";
		showBlockingError(this, "Cannot Save Scene", message);
		return false;
	}

	refreshSavedSceneMetadata(m_sceneModel);
	m_sceneModel.savedWithAppVersion = EGTRAIN_APP_VERSION;
	m_savedSceneSha256 = hashSceneInputSnapshot(result.inputSnapshot);
	m_history->markSaved();
	m_document->markSaved();
	m_runSelection.clearModifiedScenarios();
	updateSceneWindowTitle();
	updateSceneActions();
	statusBar()->showMessage("Scene saved");
	refreshValidationPanel();
	m_compositionPanel->refresh();
	m_trainUnitPanel->refresh();
	m_servicePanel->refresh();
	refreshIncidentPanel();
	refreshInfrastructurePanel();
	m_passengerPanel->refresh();
	return true;
}

bool MainWindow::saveSceneToCurrentDir() {
	const auto operation = captureTelemetryOperation();
	if (!m_sceneLoaded)
		return false;
	commitPendingEditorValues();
	if (m_sceneDir.isEmpty() || isInstalledScenePath(m_sceneDir, QCoreApplication::applicationDirPath()))
		return saveSceneAsToBundle(operation);

	auto result = m_sceneIsBundle
		? saveSceneBundle(m_sceneModel, m_sceneDir.toStdString())
		: ::saveScene(m_sceneModel, m_sceneDir.toStdString());
	return finishSceneSave(result, operation, m_sceneIsBundle ? std::optional<telemetry::ExportKind>(telemetry::ExportKind::SceneBundle) : std::nullopt);
}

void MainWindow::saveSceneAs() {
	saveSceneAsToBundle();
}

bool MainWindow::saveSceneAsToBundle() {
	return saveSceneAsToBundle(captureTelemetryOperation());
}

bool MainWindow::saveSceneAsToBundle(const telemetry::OperationObservation& operation) {
	if (!m_sceneLoaded)
		return false;
	commitPendingEditorValues();

	const QString startPath = dialog_folders::suggestedPath(m_sceneIsBundle ? QFileInfo(m_sceneDir).fileName() : QString(), m_sceneDir);
	QString targetPath = QFileDialog::getSaveFileName(this, "Save Case Study As", startPath,
		"EGTRAIN Case Study (*.egscene)");
	if (targetPath.isEmpty())
		return false;
	if (!targetPath.endsWith(".egscene", Qt::CaseInsensitive))
		targetPath += ".egscene";
	targetPath = QDir::cleanPath(QFileInfo(targetPath).absoluteFilePath());
	if (isInstalledScenePath(targetPath, QCoreApplication::applicationDirPath())) {
		operation.exportFinished(telemetry::ExportKind::SceneBundle, false, false, telemetry::Error::InvalidInput);
		showBlockingError(this, "Cannot Save Scene", "Installed example scenes cannot be overwritten. Choose a location outside the EGTRAIN installation.");
		return false;
	}

	auto result = saveSceneBundle(m_sceneModel, targetPath.toStdString());
	if (!result.success())
		return finishSceneSave(result, operation, telemetry::ExportKind::SceneBundle);

	m_sceneDir = targetPath;
	m_sceneIsBundle = true;
	m_sceneBundleVersion = kCurrentSceneBundleVersion;
	addRecentScene(targetPath);
	return finishSceneSave(result, operation, telemetry::ExportKind::SceneBundle);
}

bool MainWindow::saveSceneAsToDirectory() {
	const auto operation = captureTelemetryOperation();
	if (!m_sceneLoaded)
		return false;
	commitPendingEditorValues();

	const QString startDir = dialog_folders::startFolder(m_sceneDir);
	QString dir = QFileDialog::getExistingDirectory(this, "Save Scene As Folder", startDir);
	if (dir.isEmpty())
		return false;

	QString targetPath = QDir(dir).absolutePath();
	if (isInstalledScenePath(targetPath, QCoreApplication::applicationDirPath())) {
		operation.exportFinished(std::nullopt, false, false, telemetry::Error::InvalidInput);
		showBlockingError(this, "Cannot Save Scene", "Installed example scenes cannot be overwritten. Choose a location outside the EGTRAIN installation.");
		return false;
	}
	QDir targetDir(targetPath);
	if (targetDir.exists()) {
		bool isNonEmpty = !targetDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty();
		bool hasSceneJson = QFileInfo(targetDir.filePath("scene.json")).exists();
		if (isNonEmpty && !hasSceneJson) {
			auto response = QMessageBox::question(this,
				"Save Scene As Folder",
				"The selected folder is not empty and does not contain scene.json. Save scene files there?",
				QMessageBox::Yes | QMessageBox::No,
				QMessageBox::No);
			if (response != QMessageBox::Yes)
				return false;
		}
	}

	if (!copyScenePassthroughFiles(targetPath, operation))
		return false;

	auto result = ::saveScene(m_sceneModel, targetPath.toStdString());
	if (!result.success())
		return finishSceneSave(result, operation, std::nullopt);

	m_sceneDir = targetPath;
	m_sceneIsBundle = false;
	m_sceneBundleVersion.reset();
	addRecentScene(targetPath);
	return finishSceneSave(result, operation, std::nullopt);
}

bool MainWindow::copyScenePassthroughFiles(const QString& targetDir, const telemetry::OperationObservation& operation) {
	if (m_sceneDir.isEmpty() || m_sceneIsBundle || !QFileInfo(m_sceneDir).isDir())
		return true;

	QString sourcePath = QDir(m_sceneDir).absolutePath();
	QString targetPath = QDir(targetDir).absolutePath();

	QDir target(targetPath);
	if (!target.exists() && !QDir().mkpath(targetPath)) {
		operation.failure(telemetry::Operation::Export, telemetry::Error::IoFailure);
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

	QString sourceViews = sourceDir.filePath("views.json");
	if (QFileInfo(sourceViews).exists()) {
		QString targetViews = target.filePath("views.json");
		QFile::remove(targetViews);
		if (!QFile::copy(sourceViews, targetViews)) {
			operation.failure(telemetry::Operation::Export, telemetry::Error::IoFailure);
			QMessageBox::critical(this, "Cannot Save Scene", "Cannot copy views.json.");
			return false;
		}
	}

	return true;
}

bool MainWindow::maybeSaveScene() {
	commitPendingEditorValues();
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
		m_saveSceneAction->setEnabled(m_sceneLoaded);
	if (m_saveSceneAsAction)
		m_saveSceneAsAction->setEnabled(m_sceneLoaded);
	if (m_saveSceneAsFolderAction)
		m_saveSceneAsFolderAction->setEnabled(m_sceneLoaded);
	if (m_runSceneAction)
		m_runSceneAction->setEnabled(m_sceneLoaded && !m_worker && !hasErrors(m_sceneDiagnostics));
	if (ui->actionSimulationStart) {
		const bool sceneRunnable = m_sceneLoaded && !m_worker && !hasErrors(m_sceneDiagnostics);
		ui->actionSimulationStart->setEnabled(sceneRunnable);
		ui->actionSimulationStart->setToolTip(QString("Run simulation (Ctrl+R)"));
	}
	if (m_serviceDock)
		m_serviceDock->setEnabled(m_sceneLoaded && !m_worker);
	if (m_passengerDock)
		m_passengerDock->setEnabled(m_sceneLoaded && !m_worker);
	updateUndoRedoActions();
	updateDiagramActions();
	if (m_recentScenesMenu) {
		QSettings settings;
		m_recentScenesMenu->setEnabled(!settings.value(kRecentScenesKey).toStringList().isEmpty());
	}
}

void MainWindow::setupUpdateActions() {
	if (!ui->menuHelp || updatesSuppressedByEnvironment())
		return;

	m_checkForUpdatesAction = new QAction(QStringLiteral("Check for Updates..."), this);
	m_checkForUpdatesAction->setObjectName(QStringLiteral("actionCheckForUpdates"));
	m_automaticUpdateChecksAction = new QAction(
		QStringLiteral("Automatically Check for Updates"), this);
	m_automaticUpdateChecksAction->setObjectName(QStringLiteral("actionAutomaticallyCheckForUpdates"));
	m_automaticUpdateChecksAction->setCheckable(true);
	QSettings settings;
	{
		const QSignalBlocker blocker(m_automaticUpdateChecksAction);
		m_automaticUpdateChecksAction->setChecked(
			readUpdateCheckState(settings) == UpdateCheckState::Enabled);
	}
	ui->menuHelp->addSeparator();
	ui->menuHelp->addAction(m_checkForUpdatesAction);
	ui->menuHelp->addAction(m_automaticUpdateChecksAction);

	m_updateChecker = new UpdateChecker(this);
	m_selfUpdater = new SelfUpdater(this);
	connect(m_checkForUpdatesAction, &QAction::triggered, this,
		[this]() { startUpdateCheck(true); });
	connect(m_automaticUpdateChecksAction, &QAction::toggled, this, [this](bool checked) {
		QSettings preferences;
		writeUpdateCheckState(preferences, checked ? UpdateCheckState::Enabled : UpdateCheckState::Disabled);
		preferences.sync();
	});
	connect(m_updateChecker, &UpdateChecker::finished, this, &MainWindow::handleUpdateCheckFinished);
	connect(m_selfUpdater, &SelfUpdater::progress, this, [this](qint64 received, qint64 total) {
		if (!m_updateProgress)
			return;
		if (total > 0) {
			const qint64 maxValue = std::numeric_limits<int>::max();
			m_updateProgress->setRange(0, total > maxValue ? static_cast<int>(maxValue) : static_cast<int>(total));
			m_updateProgress->setValue(received > maxValue ? static_cast<int>(maxValue) : static_cast<int>(received));
		} else {
			m_updateProgress->setRange(0, 0);
		}
	});
	connect(m_selfUpdater, &SelfUpdater::finished, this, &MainWindow::handleSelfUpdateFinished);
	connect(m_selfUpdater, &SelfUpdater::preparing, this, [this](const QString& version) {
		if (!m_updateProgress)
			return;
		m_updateProgress->setLabelText(QStringLiteral("Verifying and preparing EGTRAIN %1...")
				.arg(version));
		m_updateProgress->setCancelButton(nullptr);
		m_updateProgress->setRange(0, 0);
	});
}

void MainWindow::startTelemetryAfterInitialConsent() {
	if (m_telemetrySender) return;
	try {
		if (const auto telemetryStart = mainWindowTestHooks().telemetryStart) {
			if (!telemetryStart(*this)) return;
		} else {
			const TelemetryContext context = applicationTelemetryContext();
			if (!context.capable()) return;
			m_telemetrySettings.reset(new QSettings);
			m_telemetryConsent.reset(new TelemetryConsent(*m_telemetrySettings, context));
			telemetry::Application metadata;
			metadata.version = QCoreApplication::applicationVersion();
			m_telemetrySender.reset(new telemetry::TelemetrySender(context, metadata,
				QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
					.filePath(QStringLiteral("Telemetry"))));
		}
		m_telemetrySender->requestInteractiveSession();
		connect(m_telemetryConsent.get(), &TelemetryConsent::revoked, this, [this](bool usage, bool diagnostics) {
				if (m_telemetrySender) m_telemetrySender->invalidateConsent(usage, diagnostics); }, Qt::DirectConnection);
		connect(
			m_telemetryConsent.get(), &TelemetryConsent::receiverChanged, this,
			[this] { if (m_telemetrySender) m_telemetrySender->invalidateReceiver(); }, Qt::DirectConnection);
	} catch (...) {
		m_telemetrySender.reset();
		m_telemetryConsent.reset();
		m_telemetrySettings.reset();
	}
}

void MainWindow::showPrivacySettings(bool initialPrompt) {
	QSettings previewSettings;
	TelemetryContext context = applicationTelemetryContext();
	const auto& privacyPreview = mainWindowTestHooks().privacyPreview;
	const bool preview = privacyPreview && privacyPreview(context, initialPrompt);
	TelemetryConsent previewConsent(previewSettings, context);
	TelemetryConsent& consent = preview || !m_telemetryConsent ? previewConsent : *m_telemetryConsent;
	if (!consent.available()) {
		if (!initialPrompt)
			QMessageBox::information(this, QStringLiteral("Privacy & diagnostics"),
				QStringLiteral("Usage statistics and diagnostics are unavailable in this build or run. Nothing is collected or sent."));
		return;
	}
	if (initialPrompt && !consent.promptRequired()) return;
	TelemetryConsentDialog dialog(consent, context.domain(), initialPrompt, this);
	if (const auto& dialogShown = mainWindowTestHooks().dialogShown)
		dialogShown(dialog, "privacySettings");
	dialog.exec();
	if (!preview && m_telemetrySender) m_telemetrySender->requestConsentRefresh();
}

void MainWindow::maybePromptForUpdateChecks() {
	if (!m_updateChecker || updatesSuppressedByEnvironment())
		return;
	QSettings settings;
	const UpdateCheckState state = readUpdateCheckState(settings);
	if (state == UpdateCheckState::Unknown) {
		QMessageBox dialog(QMessageBox::Question,
			QStringLiteral("Automatically Check for EGTRAIN Updates?"),
			update_prompt::automaticChecksQuestion(),
			QMessageBox::NoButton, this);
		dialog.setInformativeText(update_prompt::automaticChecksDetail());
		QPushButton* enableButton = dialog.addButton(QStringLiteral("Check Automatically"),
			QMessageBox::AcceptRole);
		dialog.addButton(QStringLiteral("Don't Check Automatically"), QMessageBox::RejectRole);
		dialog.setDefaultButton(enableButton);
		dialog.exec();
		const UpdateCheckState chosen = dialog.clickedButton() == enableButton
			? UpdateCheckState::Enabled
			: UpdateCheckState::Disabled;
		writeUpdateCheckState(settings, chosen);
		settings.sync();
		if (m_automaticUpdateChecksAction) {
			const QSignalBlocker blocker(m_automaticUpdateChecksAction);
			m_automaticUpdateChecksAction->setChecked(chosen == UpdateCheckState::Enabled);
		}
		if (chosen == UpdateCheckState::Enabled)
			startUpdateCheck(false);
		return;
	}
	if (state == UpdateCheckState::Enabled)
		startUpdateCheck(false);
}

void MainWindow::startUpdateCheck(bool manual) {
	if (!m_updateChecker || updatesSuppressedByEnvironment())
		return;
	QSettings settings;
	if (!shouldCheckForUpdates(readUpdateCheckState(settings), manual))
		return;
	if (!manual && m_updateChecker->isChecking())
		return;
	m_manualUpdateCheck = manual;
	m_updateChecker->check();
}

void MainWindow::handleUpdateCheckFinished(const UpdateCheckResult& result) {
	const bool manual = m_manualUpdateCheck;
	m_manualUpdateCheck = false;
	if (!result.success || !result.release) {
		if (manual) {
			QMessageBox::warning(this, QStringLiteral("Check for Updates"),
				result.error.isEmpty() ? QStringLiteral("Could not check for updates.") : result.error);
		} else {
			qWarning().noquote() << "Automatic update check failed:"
								 << (result.error.isEmpty() ? QStringLiteral("unknown error") : result.error);
		}
		return;
	}

	const std::optional<SemanticVersion> current =
		parseStableVersion(QCoreApplication::applicationVersion().toStdString());
	if (!current) {
		const QString error = QStringLiteral("The installed application version is invalid.");
		if (manual)
			QMessageBox::warning(this, QStringLiteral("Check for Updates"), error);
		else
			qWarning().noquote() << "Automatic update check failed:" << error;
		return;
	}
	if (!isUpdateAvailable(*current, *result.release)) {
		if (manual)
			QMessageBox::information(this, QStringLiteral("Check for Updates"),
				QStringLiteral("EGTRAIN %1 is up to date.").arg(formatSemanticVersion(*current)));
		return;
	}
	{
		QSettings settings;
		if (!shouldOfferRelease(result.release->version, readFailedUpdateVersion(settings), manual))
			return;
	}

	const StableRelease& release = *result.release;
	QString availability;
	if (m_selfUpdater && !m_selfUpdater->canSelfUpdate(release)) {
		const SelfUpdateCapability capability = m_selfUpdater->capability();
		availability = capability.supported
			? QStringLiteral("\n\nNo automatic update package is available for this platform. Use the release page to update manually.")
			: QStringLiteral("\n\n%1 Use the release page to update manually.").arg(capability.reason);
	} else if (m_selfUpdater && m_selfUpdater->capability().usesInstaller) {
		availability = QStringLiteral("\n\nUpdate and Restart runs the EGTRAIN installer. EGTRAIN closes while the installer "
									  "updates this installation and starts again afterwards.");
	}
	QMessageBox dialog(QMessageBox::Information,
		QStringLiteral("EGTRAIN %1 is Available").arg(formatSemanticVersion(release.version)),
		update_prompt::availableText(formatSemanticVersion(release.version), formatSemanticVersion(*current)),
		QMessageBox::NoButton, this);
	setMarkdownInformativeText(dialog,
		(release.notes.isEmpty() ? QStringLiteral("No release notes were provided.") : releaseNotesMarkdown(release.notes))
			+ availability);
	QPushButton* updateButton = nullptr;
	if (m_selfUpdater && m_selfUpdater->canSelfUpdate(release))
		updateButton = dialog.addButton(QStringLiteral("Update and Restart"), QMessageBox::AcceptRole);
	QPushButton* openButton = dialog.addButton(QStringLiteral("Open Release Page"),
		QMessageBox::AcceptRole);
	dialog.addButton(QStringLiteral("Later"), QMessageBox::RejectRole);
	QPushButton* stopButton = dialog.addButton(QStringLiteral("Stop Checking"),
		QMessageBox::DestructiveRole);
	dialog.exec();
	QAbstractButton* clickedButton = dialog.clickedButton();
	if (updateButton && clickedButton == updateButton) {
		startSelfUpdate(release);
	} else if (clickedButton == stopButton) {
		QSettings settings;
		writeUpdateCheckState(settings, UpdateCheckState::Disabled);
		settings.sync();
		if (m_automaticUpdateChecksAction) {
			const QSignalBlocker blocker(m_automaticUpdateChecksAction);
			m_automaticUpdateChecksAction->setChecked(false);
		}
	} else if (clickedButton == openButton
		&& !QDesktopServices::openUrl(release.releasePage)) {
		QMessageBox::warning(this, QStringLiteral("Open Release Page"),
			QStringLiteral("Could not open the release page:\n%1").arg(release.releasePage.toString()));
	}
}

void MainWindow::startSelfUpdate(const StableRelease& release) {
	if (!m_selfUpdater || m_selfUpdater->isBusy() || !maybeSaveScene())
		return;
	m_updateProgress = new QProgressDialog(QStringLiteral("Downloading EGTRAIN %1...")
											   .arg(formatSemanticVersion(release.version)),
		QStringLiteral("Cancel"), 0, 0, this);
	m_updateProgress->setWindowTitle(QStringLiteral("EGTRAIN Update"));
	m_updateProgress->setWindowModality(Qt::WindowModal);
	m_updateProgress->setAutoClose(false);
	m_updateProgress->setAutoReset(false);
	m_updateProgress->setMinimumDuration(0);
	connect(m_updateProgress, &QProgressDialog::canceled, this, [this]() {
		if (!m_selfUpdater)
			return;
		if (m_selfUpdater->isPreparing()) {
			// Preparation cannot be interrupted safely; Esc or a stray cancel
			// must not hide the dialog while the update is still progressing.
			if (m_updateProgress && !m_updateProgress->isVisible())
				m_updateProgress->show();
			return;
		}
		m_selfUpdater->cancel();
	});
	m_updateProgress->show();
	m_selfUpdater->start(release);
}

void MainWindow::handleSelfUpdateFinished(bool success, const QString& error) {
	if (m_updateProgress) {
		m_updateProgress->close();
		m_updateProgress->deleteLater();
		m_updateProgress = nullptr;
	}
	if (!success) {
		QMessageBox::warning(this, QStringLiteral("EGTRAIN Update"),
			error.isEmpty() ? QStringLiteral("The update was not installed. EGTRAIN is unchanged.") : error);
		return;
	}
	const bool installer = m_selfUpdater && m_selfUpdater->capability().usesInstaller;
	QMessageBox::information(this, QStringLiteral("EGTRAIN Update"),
		installer ? QStringLiteral("The installer is ready. EGTRAIN will close now, run the installer and start again.")
				  : QStringLiteral("The update is ready. EGTRAIN will restart now."));
	if (!m_selfUpdater || !m_selfUpdater->restart()) {
		QMessageBox::warning(this, QStringLiteral("EGTRAIN Update"),
			QStringLiteral("Could not start the update installer. EGTRAIN is unchanged."));
		return;
	}
	QCoreApplication::quit();
}

bool MainWindow::advancedDetailsEnabled() const {
	if (m_advancedDetailsAction)
		return m_advancedDetailsAction->isChecked();
	QSettings settings;
	return settings.value(kAdvancedDetailsKey, false).toBool();
}

void MainWindow::updateDiagnosticPresentation() {
	const SceneDiagnosticCounts counts = countDiagnostics(m_sceneDiagnostics);
	if (m_validationStatusLabel) {
		if (advancedDetailsEnabled()) {
			QString message = QStringLiteral("Validation: %1 error(s), %2 warning(s)")
								  .arg(counts.errors)
								  .arg(counts.warnings);
			if (counts.infos > 0)
				message += QStringLiteral(", %1 info").arg(counts.infos);
			m_validationStatusLabel->setText(message);
		} else if (counts.errors > 0) {
			m_validationStatusLabel->setText(
				QStringLiteral("Not ready: %1 validation error(s) must be fixed").arg(counts.errors));
		} else {
			m_validationStatusLabel->clear();
		}
	}
	updateCaseLayersPanel();
}

void MainWindow::refreshValidationPanel() {
	if (m_committingPendingEditorValues)
		return;
	commitPendingEditorValues();
	if (!m_sceneLoaded) {
		m_sceneDiagnostics.clear();
		if (m_validationTable)
			m_validationTable->setRowCount(0);
		if (m_validationStatusLabel)
			m_validationStatusLabel->clear();
		refreshLoadedDataTree();
		m_scenarioPanel->refresh();
		m_passengerPanel->refresh();
		updateSceneActions();
		updateCaseLayersPanel();
		return;
	}

	const std::optional<double> effectiveDurationOverride = initial_variables.durationOverride
		? std::optional<double>(initial_variables.times)
		: std::nullopt;
	m_sceneDiagnostics = validateRunnableScene(m_sceneModel, {}, effectiveDurationOverride);

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

	updateDiagnosticPresentation();
	refreshLoadedDataTree();
	m_scenarioPanel->refresh();
	m_passengerPanel->refresh();
	updateSceneActions();
}

void MainWindow::refreshLoadedDataTree() {
	if (!m_loadedDataTree)
		return;

	m_loadedDataTree->clear();
	if (!m_sceneLoaded)
		return;

	refreshLoadedDataSummary(m_sceneModel);
	refreshLoadedDataDiagnostics(m_sceneModel, m_sceneDiagnostics);
	auto addRow = [](QTreeWidgetItem* parent, const QString& category, const QString& source,
					  const QString& count, const QString& status) {
		return new QTreeWidgetItem(parent, QStringList{category, source, count, status});
	};
	const SceneDiagnosticCounts validationCounts = countDiagnostics(m_sceneDiagnostics);
	const SceneDiagnosticCounts runtimeCounts = countDiagnostics(m_runtimeDiagnostics);
	const bool dataInvalid = std::any_of(m_sceneModel.loadedData.begin(), m_sceneModel.loadedData.end(),
		[](const SceneLoadedData& row) { return row.status == "Invalid"; });
	const bool dataWarning = std::any_of(m_sceneModel.loadedData.begin(), m_sceneModel.loadedData.end(),
		[](const SceneLoadedData& row) { return row.status == "Warning"; });
	const QString caseStatus = validationCounts.errors > 0 || runtimeCounts.errors > 0 || dataInvalid
		? QStringLiteral("Invalid")
		: (validationCounts.warnings > 0 || runtimeCounts.warnings > 0 || dataWarning
				  ? QStringLiteral("Warning")
				  : QStringLiteral("Ready"));
	auto* caseRoot = new QTreeWidgetItem(m_loadedDataTree);
	caseRoot->setText(0, "Case Study");
	caseRoot->setText(1, m_sceneDir);
	caseRoot->setText(2, "1");
	caseRoot->setText(3, caseStatus);
	caseRoot->setToolTip(0, "Loaded case-study review.");
	addRow(caseRoot, "Name", QString::fromStdString(m_sceneModel.name), "1", "Parsed");
	addRow(caseRoot, "Description",
		m_sceneModel.description.empty() ? QStringLiteral("(none)") : QString::fromStdString(m_sceneModel.description),
		"1", "Parsed");
	addRow(caseRoot, "Source path", m_sceneDir, "1", "Loaded");
	addRow(caseRoot, "Canonical schema version", QString::number(m_sceneModel.schemaVersion),
		QString::number(kCurrentSceneSchemaVersion), "Parsed");
	addRow(caseRoot, "Saved with app version",
		m_sceneModel.savedWithAppVersion.empty()
			? QStringLiteral("(not recorded)")
			: QString::fromStdString(m_sceneModel.savedWithAppVersion),
		"1", m_sceneModel.savedWithAppVersion.empty() ? QStringLiteral("Missing optional") : QStringLiteral("Loaded"));
	if (m_sceneIsBundle)
		addRow(caseRoot, "Bundle format version", m_sceneBundleVersion ? QString::number(*m_sceneBundleVersion) : QStringLiteral("(not recorded)"),
			QString::number(kCurrentSceneBundleVersion),
			m_sceneBundleVersion ? QStringLiteral("Loaded") : QStringLiteral("Missing"));

	auto* sourceFiles = addRow(caseRoot, "Source files discovered", QString(),
		QString::number(static_cast<int>(m_sceneModel.sourceFiles.size())), "Loaded");
	for (const auto& source : m_sceneModel.sourceFiles) {
		addRow(sourceFiles, QString::fromStdString(source), QString::fromStdString(source), "1",
			QStringLiteral("Loaded"));
	}
	for (const auto& item : m_sceneModel.loadedData) {
		addLoadedDataTreeItem(m_loadedDataTree, caseRoot, item);
	}
	auto* runtimeAndResults = addRow(caseRoot, "Runtime and results", QString(), QString(),
		m_runtimeStatus);
	const bool runtimeReady = m_runtimeStatus == "Ready" || m_runtimeStatus == "Running"
		|| m_runtimeStatus == "Completed";
	const bool runAttempted = m_runtimeStatus != "Not built";
	addRow(runtimeAndResults, "Runtime infrastructure", QString(), "1",
		runtimeReady ? QStringLiteral("Ready") : QStringLiteral("Not built"));
	addRow(runtimeAndResults, "Runtime rolling stock", QString(), "1",
		runtimeReady ? QStringLiteral("Ready") : QStringLiteral("Not built"));
	addRow(runtimeAndResults, "Applied scenario", m_runSelection.appliedScenarioId().empty() ? QStringLiteral("(none)") : QString::fromStdString(m_runSelection.appliedScenarioId()), "1",
		runAttempted ? m_runtimeStatus : QStringLiteral("Not built"));
	addRow(runtimeAndResults, "Current run", QString(), "1", m_runtimeStatus);
	auto* runtime = addRow(runtimeAndResults, "Runtime", QString(),
		QString::number(static_cast<int>(m_runtimeDiagnostics.size())), m_runtimeStatus);
	for (const auto& diagnostic : m_runtimeDiagnostics) {
		const QString severity = diagnostic.severity == SceneSeverity::Error ? QStringLiteral("Failed")
																			 : (diagnostic.severity == SceneSeverity::Warning ? QStringLiteral("Warning") : QStringLiteral("Ready"));
		const QString detail = QString::fromStdString(diagnostic.code) + ": "
			+ QString::fromStdString(diagnostic.message);
		addRow(runtime, detail, QString::fromStdString(diagnostic.file), "1", severity);
	}
	addRow(runtimeAndResults, "Results", QString(), m_resultsAvailable ? "1" : "0",
		m_resultsAvailable ? QStringLiteral("Ready") : QStringLiteral("Not built"));
	caseRoot->setExpanded(true);
	m_loadedDataTree->resizeColumnToContents(0);
	m_loadedDataTree->resizeColumnToContents(1);
	m_loadedDataTree->setColumnWidth(1, std::min(m_loadedDataTree->columnWidth(1), 360));
}

void MainWindow::activateLoadedDataItem(QTreeWidgetItem* item) {
	const auto operation = captureTelemetryOperation();
	if (!item)
		return;
	const QString targetType = item->data(0, kLoadedDataTargetTypeRole).toString();
	const QString targetId = item->text(0);
	if (targetType.isEmpty())
		return;
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
		if (m_validationTable) {
			for (int row = 0; row < m_validationTable->rowCount(); ++row) {
				QTableWidgetItem* file = m_validationTable->item(row, 3);
				if (file && file->text() == item->text(1)) {
					m_validationTable->selectRow(row);
					break;
				}
			}
		}
		return;
	}
	if (targetType == "train_unit_plot") {
		const QString unitId = item->parent() && item->parent()->parent()
			? item->parent()->parent()->text(0)
			: QString();
		const SceneTrainUnit* unit = sceneTrainUnitById(m_sceneModel, unitId.toStdString());
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
				raiseDock(m_incidentDock);
				return;
			}
		}
	}
}

void MainWindow::markSceneDirty() {
	m_document->markChanged(SceneDocument::AllChanges);
}

void MainWindow::sceneDocumentChanged() {
	m_history->record(m_sceneModel, modifiedScenarioIds());
	invalidateAfterSceneChange();
}

void MainWindow::invalidateAfterSceneChange() {
	if (m_delayBaseline)
		m_delayBaselineStatus = QStringLiteral(
			"Delay baseline cleared after a case edit. Complete a run with no incidents or entrance delays to set a new baseline.");
	m_delayBaseline.reset();
	if (m_worker) {
		m_sceneChangedDuringRun = true;
		m_resultsAvailable = false;
		refreshLoadedDataTree();
		updateDiagramActions();
	} else {
		invalidateRunResults();
	}
	updateSceneWindowTitle();
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
	const bool editable = m_sceneLoaded && !m_worker;
	const bool canUndo = editable && m_history->canUndo();
	const bool canRedo = editable && m_history->canRedo();
	m_undoAction->setEnabled(canUndo);
	m_redoAction->setEnabled(canRedo);
	m_undoAction->setText(historyActionText(QStringLiteral("Undo"), canUndo ? m_history->undoLabel() : std::string()));
	m_redoAction->setText(historyActionText(QStringLiteral("Redo"), canRedo ? m_history->redoLabel() : std::string()));
}

std::set<std::string> MainWindow::modifiedScenarioIds() const {
	std::set<std::string> ids;
	for (const SceneScenario& scenario : m_sceneModel.scenarios) {
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
	if (!m_sceneLoaded || m_worker)
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
	for (const SceneTrainUnit& unit : m_sceneModel.trainUnits)
		previousUnitIds.push_back(unit.id);

	m_document->restore(*entry.scene, !m_history->isAtSavedPoint());

	// What the window keeps about the scene by id follows the restored scene. A rolling stock link stays with a unit that
	// still has its id, and is dropped for one that is gone.
	m_runSelection.clearModifiedScenarios();
	for (const std::string& id : entry.modifiedScenarioIds)
		m_runSelection.markScenarioModified(id);
	const bool scenarioKept = std::any_of(m_sceneModel.scenarios.begin(), m_sceneModel.scenarios.end(),
		[this](const SceneScenario& scenario) { return scenario.id == m_runSelection.scenarioId(); });
	if (!scenarioKept) {
		const SceneScenario* fallback = defaultScenario(static_cast<const SceneModel&>(m_sceneModel));
		m_runSelection.selectScenario(fallback ? fallback->id : std::string());
	}
	for (const std::string& id : previousUnitIds) {
		if (m_trainUnitSourceLinker->linkOf(id) && !sceneTrainUnitById(m_sceneModel, id))
			m_trainUnitSourceLinker->forgetUnit(id);
	}
	setStartOffset(baseTimeToSeconds(m_sceneModel.baseTime));
	m_runSelection.pruneExcluded(m_sceneModel, serviceOccurrenceDuration());

	invalidateAfterSceneChange();
	refreshAllEditorPanels();
	updateCaseLayersPanel();
}

// Shows the scene in every editor, in the order in which openSceneDirectory refreshes them: validation last.
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
	if (!m_worker) {
		if (m_replayActive && m_sceneLoaded && scene) {
			setFollowTrain(-1);
			handleCloseInfoDockWidget();
			renderTrackPreview(m_sceneModel); // Teardown clears history and graphics together.
		}
		clearReplay();
	}
	m_runtimeStatus = QStringLiteral("Not built");
	m_runtimeDiagnostics.clear();
	m_resultsAvailable = false;
	m_sceneChangedDuringRun = false;
	m_runSelection.clearAppliedScenario();
	m_completedRun.reset();
	m_pendingRunProvenance = RunProvenance();
	if (m_runResultsTable)
		m_runResultsTable->setRowCount(0);
	if (m_runResultsSummaryLabel)
		m_runResultsSummaryLabel->setText(QString("No completed run | Scenario: %1").arg(scenarioContext()));
	if (m_runResultsDock)
		m_runResultsDock->setWindowTitle(QString("Run Results — %1 (not built)").arg(scenarioContext()));
	if (m_runResultsDock)
		m_runResultsDock->hide();
	refreshLoadedDataTree();
	updateDiagramActions();
	refreshFollowTrainChoices();
}

void MainWindow::commitPendingEditorValues() {
	// The editors hold the scene before a restored one until they are refreshed, and that is not an edit.
	if (m_committingPendingEditorValues || m_restoringHistory)
		return;
	m_committingPendingEditorValues = true;

	m_caseSettingsPanel->commitPendingEditors();
	m_trainUnitPanel->commitPendingEditors();
	m_infrastructurePanel->commitPendingEditors();
	m_passengerPanel->commitPendingEditors();
	m_compositionPanel->commitPendingEditors();
	m_servicePanel->commitPendingEditors();
	// Scenario and incident controls are disabled and cleared during a run.
	// Their placeholder values are not pending edits.
	if (m_worker) {
		m_committingPendingEditorValues = false;
		return;
	}
	m_scenarioPanel->commitPendingEditors();
	m_incidentPanel->commitPendingEditors();
	m_entranceDelayPanel->commitPendingEditors();

	m_committingPendingEditorValues = false;
}

void MainWindow::commitPendingPlatformGeometry() {
	const auto hasEditorFocus = [](QWidget* editor) {
		QWidget* focus = QApplication::focusWidget();
		return editor && (editor->hasFocus() || (focus && editor->isAncestorOf(focus)));
	};
	std::vector<std::tuple<int, int, double>> pendingGeometry;
	for (int row = 0; row < m_infrastructureTable->rowCount(); ++row) {
		for (int column : {3, 4}) {
			auto* edit = qobject_cast<QDoubleSpinBox*>(m_infrastructureTable->cellWidget(row, column));
			if (!hasEditorFocus(edit)
				|| !edit->property(kPlatformGeometryEditedProperty).toBool())
				continue;
			edit->setProperty(kPlatformGeometryEditedProperty, false);
			edit->interpretText();
			pendingGeometry.emplace_back(row, column, edit->value());
		}
	}
	for (const auto& pending : pendingGeometry)
		commitPlatformGeometryCell(std::get<0>(pending), std::get<1>(pending), std::get<2>(pending));
}

void MainWindow::commitPendingCaseSettings() {
	m_caseSettingsPanel->commitPendingEditors();
}

void MainWindow::commitCaseSettings() {
	m_caseSettingsPanel->commit();
}

void MainWindow::refreshInfrastructurePanel(bool resetSelection) {
	m_infrastructureDock->setEnabled(m_sceneLoaded && !m_worker);
	m_infrastructurePanel->refresh(resetSelection);
}

void MainWindow::fillInfrastructureRows(const QString& facet) {
	const auto setCell = [this](int row, int column, const QString& text, bool editable = true) {
		auto* item = new QTableWidgetItem(text);
		if (!editable)
			item->setFlags(item->flags() & ~Qt::ItemIsEditable);
		m_infrastructureTable->setItem(row, column, item);
	};
	const int precision = std::numeric_limits<double>::max_digits10;
	const SceneSectionInventory sectionInventory = buildSceneSectionInventory(m_sceneModel);
	const auto makeSectionCombo = [this, &sectionInventory](const std::string& current,
									  bool allowNone, const QString& noneLabel, const QString& objectName) {
		auto* combo = new QComboBox(m_infrastructureTable);
		combo->setObjectName(objectName);
		const QSignalBlocker blocker(combo);
		populateSceneSectionCombo(combo, sectionInventory, current, allowNone, noneLabel);
		return combo;
	};
	if (facet == "blocks") {
		const QString selectedTrack = m_blockTrackFilterCombo
			? m_blockTrackFilterCombo->currentData().toString()
			: QString();
		m_infrastructureTable->setRowCount(0);
		for (int modelIndex = 0; modelIndex < static_cast<int>(m_sceneModel.blocks.size()); ++modelIndex) {
			const SceneBlock& block = m_sceneModel.blocks[static_cast<std::size_t>(modelIndex)];
			if (QString::fromStdString(block.trackId) != selectedTrack)
				continue;
			const int row = m_infrastructureTable->rowCount();
			m_infrastructureTable->insertRow(row);
			m_infrastructurePanel->blockRowModelIndices().push_back(modelIndex);
			setCell(row, 0, QString::fromStdString(block.id));
			setCell(row, 1, QString::fromStdString(block.trackId));
			setCell(row, 2, QString::number(block.lengthKm, 'g', precision));
			const SceneSectionDescriptor* descriptor = nullptr;
			for (const auto& section : sectionInventory.sections) {
				if (section.sourceBlockId == block.id) {
					descriptor = &section;
					break;
				}
			}
			setCell(row, 3, QString::number(row + 1), false);
			if (!descriptor) {
				setCell(row, 4, QStringLiteral("unavailable"), false);
				setCell(row, 5, QStringLiteral("unavailable"), false);
				setCell(row, 6, QStringLiteral("unavailable"), false);
			} else {
				setCell(row, 4, QString::number(descriptor->startKm, 'g', precision), false);
				setCell(row, 5, QString::number(descriptor->endKm, 'g', precision), false);
				QStringList coverage;
				if (descriptor->trackCoverageGap)
					coverage << QStringLiteral("gap");
				if (descriptor->layoutOverflow)
					coverage << QStringLiteral("overflow");
				if (descriptor->clippedToTrackEnd)
					coverage << QStringLiteral("clipped");
				if (coverage.isEmpty())
					coverage << QStringLiteral("complete");
				setCell(row, 6, coverage.join(QStringLiteral(", ")), false);
			}
		}
	} else if (facet == "stations") {
		m_infrastructureTable->setRowCount(static_cast<int>(m_sceneModel.stations.size()));
		for (int row = 0; row < m_infrastructureTable->rowCount(); ++row) {
			const SceneStation& station = m_sceneModel.stations[static_cast<std::size_t>(row)];
			setCell(row, 0, QString::fromStdString(station.id));
			setCell(row, 1, QString::fromStdString(station.name));
			setCell(row, 2, station.hasPosition ? QStringLiteral("true") : QStringLiteral("false"));
			setCell(row, 3, QString::number(station.positionKm, 'g', precision));
		}
	} else if (facet == "platforms") {
		int row = 0;
		for (const auto& station : m_sceneModel.stations)
			row += static_cast<int>(station.platforms.size());
		m_infrastructureTable->setRowCount(row);
		row = 0;
		for (const auto& station : m_sceneModel.stations) {
			for (const auto& platform : station.platforms) {
				setCell(row, 0, QString::fromStdString(station.id));
				setCell(row, 1, QString::fromStdString(platform.id));
				setCell(row, 2, joinCommaList(platform.nodeIds));
				const auto makeGeometrySpin = [this, row](double value, const char* objectName,
												  int column, const QString& tooltip) {
					auto* edit = new CompactDoubleSpinBox(m_infrastructureTable);
					edit->setObjectName(objectName);
					edit->setAccessibleName(tooltip);
					edit->setToolTip(tooltip);
					edit->setRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
					edit->setDecimals(std::numeric_limits<double>::max_digits10);
					edit->setSingleStep(1.0);
					edit->setKeyboardTracking(false);
					{
						const QSignalBlocker spinBlocker(edit);
						edit->setValue(value);
					}
					connect(edit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
						[this, row, column](double next) {
							commitPlatformGeometryCell(row, column, next);
						});
					if (QLineEdit* textEdit = edit->findChild<QLineEdit*>()) {
						connect(textEdit, &QLineEdit::textEdited, edit, [edit]() {
							edit->setProperty(kPlatformGeometryEditedProperty, edit->hasAcceptableInput());
						});
					}
					connect(edit, &QAbstractSpinBox::editingFinished, this, [this, edit, row, column]() {
						if (!edit->property(kPlatformGeometryEditedProperty).toBool())
							return;
						edit->setProperty(kPlatformGeometryEditedProperty, false);
						commitPlatformGeometryCell(row, column, edit->value());
					});
					return edit;
				};
				auto* lengthEdit = makeGeometrySpin(platform.hasLength ? platform.lengthM : 100.0,
					"platformLengthEdit", 3,
					QStringLiteral("Platform length in metres; default is 100 m when absent. Capacity derives from platform geometry."));
				auto* widthEdit = makeGeometrySpin(platform.hasWidth ? platform.widthM : 2.5,
					"platformWidthEdit", 4,
					QStringLiteral("Platform width in metres; default is 2.5 m when absent. Capacity derives from platform geometry."));
				m_infrastructureTable->setCellWidget(row, 3, lengthEdit);
				m_infrastructureTable->setCellWidget(row, 4, widthEdit);
				++row;
			}
		}
	} else if (facet == "signals") {
		const auto& signalsList = sceneSignals(m_sceneModel);
		m_infrastructureTable->setRowCount(static_cast<int>(signalsList.size()));
		for (int row = 0; row < m_infrastructureTable->rowCount(); ++row) {
			const SceneSignal& signal = signalsList[static_cast<std::size_t>(row)];
			setCell(row, 0, QString::fromStdString(signal.id));
			QComboBox* combo = new QComboBox(m_infrastructureTable);
			combo->setObjectName(QStringLiteral("protectedSectionCombo"));
			combo->setAccessibleName(QStringLiteral("Protected section for %1")
					.arg(QString::fromStdString(signal.id)));
			combo->setToolTip(QStringLiteral("Choose the block or switch section protected by this signal"));
			const QSignalBlocker comboBlocker(combo);
			populateSceneSectionCombo(combo, sectionInventory, signal.protectedSection, true,
				QStringLiteral("(unbound)"));
			connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
				[this, row, combo](int) {
					const auto& currentSignals = sceneSignals(m_sceneModel);
					if (row < 0 || row >= static_cast<int>(currentSignals.size()))
						return;
					auto& editableSignals = sceneSignals(m_sceneModel);
					const std::string protectedSection = combo->currentData().toString().toStdString();
					if (editableSignals[static_cast<std::size_t>(row)].protectedSection == protectedSection)
						return;
					editableSignals[static_cast<std::size_t>(row)].protectedSection = protectedSection;
					markSceneDirty();
					refreshValidationPanel();
				});
			m_infrastructureTable->setCellWidget(row, 1, combo);
		}
	} else if (facet == "signalling_areas") {
		m_infrastructureTable->setRowCount(static_cast<int>(m_sceneModel.signallingAreas.size()));
		for (int row = 0; row < m_infrastructureTable->rowCount(); ++row) {
			const SceneSignallingArea& area = m_sceneModel.signallingAreas[static_cast<std::size_t>(row)];
			setCell(row, 0, QString::fromStdString(area.id));
			setCell(row, 1, QString::number(area.startKm, 'g', precision));
			setCell(row, 2, QString::number(area.endKm, 'g', precision));
			const QString areaId = QString::fromStdString(area.id);
			auto* systemCombo = new NoWheelComboBox(m_infrastructureTable);
			systemCombo->setObjectName(QStringLiteral("signallingAreaSystemCombo"));
			systemCombo->setAccessibleName(QStringLiteral("Signalling system for %1").arg(areaId));
			systemCombo->setToolTip(QStringLiteral("Choose the signalling system of the sections inside this area"));
			systemCombo->setFocusPolicy(Qt::StrongFocus);
			populateSignallingSystemCombo(systemCombo, area.level);
			connect(systemCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
				[this, row, systemCombo](int) {
					if (row < 0 || row >= static_cast<int>(m_sceneModel.signallingAreas.size()))
						return;
					const int level = systemCombo->currentData().toInt();
					SceneSignallingArea& editableArea = m_sceneModel.signallingAreas[static_cast<std::size_t>(row)];
					if (editableArea.level == level)
						return;
					editableArea.level = level;
					{
						const QSignalBlocker systemBlocker(systemCombo);
						for (int index = systemCombo->count() - 1; index >= 0; --index)
							if (!isValidSignallingLevel(systemCombo->itemData(index).toInt()))
								systemCombo->removeItem(index);
						systemCombo->setCurrentIndex(systemCombo->findData(level));
					}
					markSceneDirty();
					refreshValidationPanel();
				});
			m_infrastructureTable->setCellWidget(row, 3, systemCombo);
			auto* trackCombo = new NoWheelComboBox(m_infrastructureTable);
			trackCombo->setObjectName(QStringLiteral("signallingAreaTrackCombo"));
			trackCombo->setAccessibleName(QStringLiteral("Track for %1").arg(areaId));
			trackCombo->setToolTip(QStringLiteral("Choose the track this area applies to, or all tracks"));
			trackCombo->setFocusPolicy(Qt::StrongFocus);
			populateSignallingAreaTrackCombo(trackCombo, m_sceneModel, sectionInventory, area.trackId);
			connect(trackCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
				[this, row, trackCombo](int) {
					if (row < 0 || row >= static_cast<int>(m_sceneModel.signallingAreas.size()))
						return;
					const std::string trackId = trackCombo->currentData().toString().toStdString();
					SceneSignallingArea& editableArea = m_sceneModel.signallingAreas[static_cast<std::size_t>(row)];
					if (editableArea.trackId == trackId)
						return;
					editableArea.trackId = trackId;
					{
						const QSignalBlocker trackBlocker(trackCombo);
						for (int index = trackCombo->count() - 1; index >= 0; --index) {
							const std::string itemTrack = trackCombo->itemData(index).toString().toStdString();
							if (!itemTrack.empty() && !sceneHasTrack(m_sceneModel, itemTrack))
								trackCombo->removeItem(index);
						}
						trackCombo->setCurrentIndex(trackCombo->findData(QString::fromStdString(trackId)));
					}
					markSceneDirty();
					refreshValidationPanel();
				});
			m_infrastructureTable->setCellWidget(row, 4, trackCombo);
		}
	} else if (facet == "routes") {
		m_infrastructureTable->setRowCount(static_cast<int>(m_sceneModel.routes.size()));
		for (int row = 0; row < m_infrastructureTable->rowCount(); ++row) {
			const SceneRoute& route = m_sceneModel.routes[static_cast<std::size_t>(row)];
			setCell(row, 0, QString::fromStdString(route.id));
			setCell(row, 1,
				route.blocks.empty()
					? QStringLiteral("0 sections")
					: QStringLiteral("%1 section(s)").arg(static_cast<int>(route.blocks.size())),
				false);
			setCell(row, 2, route.hasCorridor ? QStringLiteral("true") : QStringLiteral("false"));
			setCell(row, 3, QString::fromStdString(route.corridor));
			setCell(row, 4, route.reversed ? QStringLiteral("true") : QStringLiteral("false"));
		}
	} else if (facet == "block_dependencies") {
		m_infrastructureTable->setRowCount(static_cast<int>(m_sceneModel.blockDependencies.size()));
		for (int row = 0; row < m_infrastructureTable->rowCount(); ++row) {
			const auto& dependency = m_sceneModel.blockDependencies[static_cast<std::size_t>(row)];
			auto* blockCombo = makeSectionCombo(dependency.block, false, QString(),
				QStringLiteral("dependencyBlockCombo"));
			auto* dependsOnCombo = makeSectionCombo(dependency.dependsOn, false, QString(),
				QStringLiteral("dependencyDependsOnCombo"));
			blockCombo->setToolTip(QStringLiteral("Dependency block; catalog data is the exact section ID"));
			dependsOnCombo->setToolTip(QStringLiteral("Depends-on section; catalog data is the exact section ID"));
			connect(blockCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
				[this, row, blockCombo](int) {
					if (row < 0 || row >= static_cast<int>(m_sceneModel.blockDependencies.size()))
						return;
					const std::string value = blockCombo->currentData().toString().toStdString();
					if (m_sceneModel.blockDependencies[static_cast<std::size_t>(row)].block == value)
						return;
					m_sceneModel.blockDependencies[static_cast<std::size_t>(row)].block = value;
					markSceneDirty();
					refreshValidationPanel();
				});
			connect(dependsOnCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
				[this, row, dependsOnCombo](int) {
					if (row < 0 || row >= static_cast<int>(m_sceneModel.blockDependencies.size()))
						return;
					const std::string value = dependsOnCombo->currentData().toString().toStdString();
					if (m_sceneModel.blockDependencies[static_cast<std::size_t>(row)].dependsOn == value)
						return;
					m_sceneModel.blockDependencies[static_cast<std::size_t>(row)].dependsOn = value;
					markSceneDirty();
					refreshValidationPanel();
				});
			m_infrastructureTable->setCellWidget(row, 0, blockCombo);
			m_infrastructureTable->setCellWidget(row, 1, dependsOnCombo);
		}
	} else if (facet == "single_track_restrictions") {
		m_infrastructureTable->setRowCount(static_cast<int>(m_sceneModel.singleTrackRestrictions.size()));
		for (int row = 0; row < m_infrastructureTable->rowCount(); ++row) {
			const auto& restriction = m_sceneModel.singleTrackRestrictions[static_cast<std::size_t>(row)];
			const std::array<std::string, 4> current = {restriction.startBlock, restriction.endBlock,
				restriction.protectedStartBlock, restriction.protectedEndBlock};
			const std::array<QString, 4> names = {QStringLiteral("restrictionStartCombo"),
				QStringLiteral("restrictionEndCombo"), QStringLiteral("restrictionProtectedStartCombo"),
				QStringLiteral("restrictionProtectedEndCombo")};
			for (int column = 0; column < 4; ++column) {
				auto* combo = makeSectionCombo(current[static_cast<std::size_t>(column)], false,
					QString(), names[static_cast<std::size_t>(column)]);
				connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
					[this, row, column, combo](int) {
						if (row < 0 || row >= static_cast<int>(m_sceneModel.singleTrackRestrictions.size()))
							return;
						SceneSingleTrackRestriction& restriction =
							m_sceneModel.singleTrackRestrictions[static_cast<std::size_t>(row)];
						std::string* target = column == 0 ? &restriction.startBlock
														  : (column == 1 ? &restriction.endBlock
																		 : (column == 2 ? &restriction.protectedStartBlock
																						: &restriction.protectedEndBlock));
						const std::string value = combo->currentData().toString().toStdString();
						if (*target == value)
							return;
						*target = value;
						markSceneDirty();
						refreshValidationPanel();
					});
				m_infrastructureTable->setCellWidget(row, column, combo);
			}
		}
	} else {
		m_infrastructureTable->setRowCount(static_cast<int>(m_sceneModel.stationBoundaries.size()));
		for (int row = 0; row < m_infrastructureTable->rowCount(); ++row) {
			const auto& boundary = m_sceneModel.stationBoundaries[static_cast<std::size_t>(row)];
			auto* entranceCombo = makeSectionCombo(boundary.entranceBlock, false, QString(),
				QStringLiteral("boundaryEntranceCombo"));
			auto* exitCombo = makeSectionCombo(boundary.hasExitBlock ? boundary.exitBlock : std::string(),
				true, QStringLiteral("(none)"), QStringLiteral("boundaryExitCombo"));
			entranceCombo->setToolTip(QStringLiteral("Required entrance section; none is not permitted"));
			exitCombo->setToolTip(QStringLiteral("Exit section; (none) clears the exit atomically"));
			connect(entranceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
				[this, row, entranceCombo](int) {
					if (row < 0 || row >= static_cast<int>(m_sceneModel.stationBoundaries.size()))
						return;
					const std::string value = entranceCombo->currentData().toString().toStdString();
					if (m_sceneModel.stationBoundaries[static_cast<std::size_t>(row)].entranceBlock == value)
						return;
					m_sceneModel.stationBoundaries[static_cast<std::size_t>(row)].entranceBlock = value;
					markSceneDirty();
					refreshValidationPanel();
				});
			connect(exitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
				[this, row, exitCombo](int) {
					if (row < 0 || row >= static_cast<int>(m_sceneModel.stationBoundaries.size()))
						return;
					SceneStationBoundary& boundary = m_sceneModel.stationBoundaries[static_cast<std::size_t>(row)];
					const std::string value = exitCombo->currentData().toString().toStdString();
					const bool hasExit = !value.empty();
					if (boundary.hasExitBlock == hasExit && (!hasExit || boundary.exitBlock == value))
						return;
					boundary.hasExitBlock = hasExit;
					boundary.exitBlock = hasExit ? value : std::string();
					markSceneDirty();
					refreshValidationPanel();
				});
			m_infrastructureTable->setCellWidget(row, 0, entranceCombo);
			m_infrastructureTable->setCellWidget(row, 1, exitCombo);
			setCell(row, 2, boundary.direction ? QStringLiteral("true") : QStringLiteral("false"));
		}
	}
}

void MainWindow::commitInfrastructureCell(int row, int column) {
	if (!m_sceneLoaded || m_worker || !m_infrastructureFacetCombo || !m_infrastructureTable || row < 0 || row >= m_infrastructureTable->rowCount())
		return;
	const QString facet = m_infrastructureFacetCombo->currentData().toString();
	const QTableWidgetItem* item = m_infrastructureTable->item(row, column);
	if (!item)
		return;
	const QString value = item->text().trimmed();
	bool changed = false;
	const auto idCanReplaceRow = [](int rowIndex, const auto& items, const std::string& id) {
		if (id.empty())
			return false;
		for (int index = 0; index < static_cast<int>(items.size()); ++index)
			if (index != rowIndex && items[static_cast<std::size_t>(index)].id == id)
				return false;
		return true;
	};
	const auto rejectUnsafeId = [this]() {
		statusBar()->showMessage("Infrastructure IDs must be non-empty and unique", 4000);
		m_infrastructurePanel->refreshTable();
	};
	const auto parseNumber = [&](double& target) {
		bool parsed = false;
		const double number = value.toDouble(&parsed);
		if (!parsed || !std::isfinite(number)) {
			m_infrastructurePanel->refreshTable();
			return false;
		}
		if (target != number) {
			target = number;
			changed = true;
		}
		return true;
	};
	const auto parseBool = [&](bool& target) {
		const QString lower = value.toLower();
		if (lower != "true" && lower != "false") {
			m_infrastructurePanel->refreshTable();
			return false;
		}
		const bool flag = lower == "true";
		if (target != flag) {
			target = flag;
			changed = true;
		}
		return true;
	};
	const auto stationIndexForId = [this](const std::string& id) {
		for (int index = 0; index < static_cast<int>(m_sceneModel.stations.size()); ++index)
			if (m_sceneModel.stations[static_cast<std::size_t>(index)].id == id)
				return index;
		return -1;
	};
	const auto locatePlatform = [this](int flattenedRow, int& stationIndex, int& platformIndex) {
		int rowIndex = 0;
		for (int station = 0; station < static_cast<int>(m_sceneModel.stations.size()); ++station) {
			const auto& platforms = m_sceneModel.stations[static_cast<std::size_t>(station)].platforms;
			if (flattenedRow >= rowIndex && flattenedRow < rowIndex + static_cast<int>(platforms.size())) {
				stationIndex = station;
				platformIndex = flattenedRow - rowIndex;
				return true;
			}
			rowIndex += static_cast<int>(platforms.size());
		}
		return false;
	};
	if (facet == "blocks" && row < static_cast<int>(m_sceneModel.blocks.size())) {
		const std::vector<int>& blockRows = m_infrastructurePanel->blockRowModelIndices();
		if (row < 0 || row >= static_cast<int>(blockRows.size()))
			return;
		const int modelIndex = blockRows[static_cast<std::size_t>(row)];
		if (modelIndex < 0 || modelIndex >= static_cast<int>(m_sceneModel.blocks.size()))
			return;
		SceneBlock& block = m_sceneModel.blocks[static_cast<std::size_t>(modelIndex)];
		if (column == 0) {
			const std::string oldId = block.id;
			const std::string newId = value.toStdString();
			if (oldId == newId)
				return;
			if (!idCanReplaceRow(modelIndex, m_sceneModel.blocks, newId)) {
				rejectUnsafeId();
				return;
			}
			block.id = newId;
			sceneRewriteBlockReferences(m_sceneModel, oldId, newId);
			m_infrastructurePanel->setSelectionId(value);
			changed = true;
		} else if (column == 1 && block.trackId != value.toStdString()) {
			block.trackId = value.toStdString();
			m_infrastructurePanel->refreshBlockTrackFilter();
			if (m_blockTrackFilterCombo) {
				const QSignalBlocker blocker(m_blockTrackFilterCombo);
				m_blockTrackFilterCombo->setCurrentIndex(m_blockTrackFilterCombo->findData(value));
			}
			changed = true;
		} else if (column == 2) {
			parseNumber(block.lengthKm);
		}
	} else if (facet == "stations" && row < static_cast<int>(m_sceneModel.stations.size())) {
		SceneStation& station = m_sceneModel.stations[static_cast<std::size_t>(row)];
		if (column == 0) {
			const std::string oldId = station.id;
			const std::string newId = value.toStdString();
			if (oldId != newId) {
				if (!idCanReplaceRow(row, m_sceneModel.stations, newId)) {
					rejectUnsafeId();
					return;
				}
				station.id = newId;
				sceneRewriteStationReferences(m_sceneModel, oldId, newId);
				m_infrastructurePanel->setSelectionId(value);
				changed = true;
			}
		} else if (column == 1 && station.name != value.toStdString()) {
			station.name = value.toStdString();
			changed = true;
		} else if (column == 2) {
			parseBool(station.hasPosition);
		} else if (column == 3) {
			parseNumber(station.positionKm);
		}
	} else if (facet == "platforms") {
		int stationIndex = -1;
		int platformIndex = -1;
		if (!locatePlatform(row, stationIndex, platformIndex))
			return;
		SceneStation& station = m_sceneModel.stations[static_cast<std::size_t>(stationIndex)];
		ScenePlatform& platform = station.platforms[static_cast<std::size_t>(platformIndex)];
		if (column == 0) {
			const std::string targetStationId = value.toStdString();
			const int targetStationIndex = stationIndexForId(targetStationId);
			if (targetStationIndex < 0) {
				statusBar()->showMessage("Platform Station ID must name an existing station", 4000);
				m_infrastructurePanel->refreshTable();
				return;
			}
			if (targetStationIndex != stationIndex) {
				const auto& targetPlatforms = m_sceneModel.stations[static_cast<std::size_t>(targetStationIndex)].platforms;
				if (std::any_of(targetPlatforms.begin(), targetPlatforms.end(), [&platform](const ScenePlatform& item) {
						return item.id == platform.id;
					})) {
					statusBar()->showMessage("Platform ID already exists on the target station", 4000);
					m_infrastructurePanel->refreshTable();
					return;
				}
				const std::string platformId = platform.id;
				const std::string sourceStationId = station.id;
				ScenePlatform moved = platform;
				station.platforms.erase(station.platforms.begin() + platformIndex);
				m_sceneModel.stations[static_cast<std::size_t>(targetStationIndex)].platforms.push_back(std::move(moved));
				sceneMovePlatformReferences(m_sceneModel, platformId, sourceStationId, targetStationId);
				m_infrastructurePanel->setSelectionId(QString::fromStdString(platformSelectionKey(targetStationId, platformId)));
				changed = true;
			}
		} else if (column == 1) {
			const std::string oldId = platform.id;
			const std::string newId = value.toStdString();
			if (oldId != newId) {
				const bool duplicate = std::any_of(station.platforms.begin(), station.platforms.end(),
					[platformIndex, &station, &newId](const ScenePlatform& item) {
						return &item != &station.platforms[static_cast<std::size_t>(platformIndex)] && item.id == newId;
					});
				if (newId.empty() || duplicate) {
					rejectUnsafeId();
					return;
				}
				platform.id = newId;
				sceneRewritePlatformReferences(m_sceneModel, station.id, oldId, newId);
				m_infrastructurePanel->setSelectionId(QString::fromStdString(platformSelectionKey(station.id, newId)));
				changed = true;
			}
		} else if (column == 2) {
			const std::vector<std::string> nodes = splitCommaList(value);
			if (platform.nodeIds != nodes) {
				platform.nodeIds = nodes;
				changed = true;
			}
		}
	} else if (facet == "signals") {
		auto& signalsList = sceneSignals(m_sceneModel);
		if (row >= static_cast<int>(signalsList.size()) || column != 0)
			return;
		SceneSignal& signal = signalsList[static_cast<std::size_t>(row)];
		const std::string oldId = signal.id;
		const std::string newId = value.toStdString();
		if (oldId != newId) {
			if (!idCanReplaceRow(row, signalsList, newId)) {
				rejectUnsafeId();
				return;
			}
			signal.id = newId;
			sceneRewriteSignalReferences(m_sceneModel, oldId, newId);
			m_infrastructurePanel->setSelectionId(value);
			changed = true;
		}
	} else if (facet == "signalling_areas" && row < static_cast<int>(m_sceneModel.signallingAreas.size())) {
		SceneSignallingArea& area = m_sceneModel.signallingAreas[static_cast<std::size_t>(row)];
		if (column == 0) {
			const std::string oldId = area.id;
			const std::string newId = value.toStdString();
			if (oldId != newId) {
				if (!idCanReplaceRow(row, m_sceneModel.signallingAreas, newId)) {
					rejectUnsafeId();
					return;
				}
				area.id = newId;
				m_infrastructurePanel->setSelectionId(value);
				changed = true;
			}
		} else if (column == 1) {
			parseNumber(area.startKm);
		} else if (column == 2) {
			parseNumber(area.endKm);
		}
	} else if (facet == "routes" && row < static_cast<int>(m_sceneModel.routes.size())) {
		SceneRoute& route = m_sceneModel.routes[static_cast<std::size_t>(row)];
		if (column == 0) {
			const std::string oldId = route.id;
			const std::string newId = value.toStdString();
			if (oldId != newId) {
				if (!idCanReplaceRow(row, m_sceneModel.routes, newId)) {
					rejectUnsafeId();
					return;
				}
				route.id = newId;
				sceneRewriteRouteReferences(m_sceneModel, oldId, newId);
				m_infrastructurePanel->setSelectionId(value);
				changed = true;
			}
		} else if (column == 2) {
			parseBool(route.hasCorridor);
		} else if (column == 3 && route.corridor != value.toStdString()) {
			route.corridor = value.toStdString();
			changed = true;
		} else if (column == 4) {
			parseBool(route.reversed);
		}
	} else if (facet == "station_boundaries" && row < static_cast<int>(m_sceneModel.stationBoundaries.size())) {
		SceneStationBoundary& boundary = m_sceneModel.stationBoundaries[static_cast<std::size_t>(row)];
		if (column == 2) {
			parseBool(boundary.direction);
		}
	}
	if (!changed)
		return;
	markSceneDirty();
	refreshValidationPanel();
	if (facet == "stations" || facet == "platforms" || facet == "routes")
		m_servicePanel->refresh();
	if (facet == "signals" || facet == "blocks")
		refreshIncidentPanel();
	m_infrastructurePanel->refreshTable();
}

void MainWindow::commitPlatformGeometryCell(int row, int column, double value) {
	if (!m_sceneLoaded || m_worker || !m_infrastructureFacetCombo
		|| m_infrastructureFacetCombo->currentData().toString() != QStringLiteral("platforms")
		|| !std::isfinite(value) || (column != 3 && column != 4))
		return;
	int rowIndex = 0;
	for (auto& station : m_sceneModel.stations) {
		if (row >= rowIndex && row < rowIndex + static_cast<int>(station.platforms.size())) {
			ScenePlatform& platform = station.platforms[static_cast<std::size_t>(row - rowIndex)];
			bool changed = false;
			if (column == 3) {
				changed = !platform.hasLength || platform.lengthM != value;
				platform.hasLength = true;
				platform.lengthM = value;
			} else {
				changed = !platform.hasWidth || platform.widthM != value;
				platform.hasWidth = true;
				platform.widthM = value;
			}
			if (!changed)
				return;
			markSceneDirty();
			refreshValidationPanel();
			return;
		}
		rowIndex += static_cast<int>(station.platforms.size());
	}
}

void MainWindow::addInfrastructureEntity() {
	if (!m_sceneLoaded || m_worker || !m_infrastructureFacetCombo)
		return;
	const QString facet = m_infrastructureFacetCombo->currentData().toString();
	std::string id;
	int newRow = -1;
	if (facet == "blocks") {
		id = sceneUniqueInfrastructureId(m_sceneModel, "block", facet.toStdString());
		const std::string selectedTrack = m_blockTrackFilterCombo
			? m_blockTrackFilterCombo->currentData().toString().toStdString()
			: std::string();
		const auto track = std::find_if(m_sceneModel.tracks.begin(), m_sceneModel.tracks.end(),
			[&selectedTrack](const SceneTrack& candidate) { return candidate.id == selectedTrack; });
		if (selectedTrack.empty() || track == m_sceneModel.tracks.end()) {
			statusBar()->showMessage("Select a valid track before adding a block", 4000);
			return;
		}
		m_sceneModel.blocks.push_back({id, track->id, 1.0});
		newRow = static_cast<int>(m_sceneModel.blocks.size()) - 1;
	} else if (facet == "stations") {
		id = sceneUniqueInfrastructureId(m_sceneModel, "station", facet.toStdString());
		SceneStation station;
		station.id = id;
		station.name = id;
		m_sceneModel.stations.push_back(std::move(station));
		newRow = static_cast<int>(m_sceneModel.stations.size()) - 1;
	} else if (facet == "platforms") {
		if (m_sceneModel.stations.empty()) {
			statusBar()->showMessage("Add a station before adding a platform", 4000);
			return;
		}
		const std::string stationId = m_sceneModel.stations.front().id;
		id = sceneUniqueInfrastructureId(m_sceneModel, "platform", facet.toStdString());
		m_sceneModel.stations.front().platforms.push_back({id, {}});
		m_infrastructurePanel->setSelectionId(QString::fromStdString(platformSelectionKey(stationId, id)));
	} else if (facet == "signals") {
		id = sceneUniqueInfrastructureId(m_sceneModel, "signal", facet.toStdString());
		sceneSignals(m_sceneModel).push_back({id});
		newRow = static_cast<int>(sceneSignals(m_sceneModel).size()) - 1;
	} else if (facet == "signalling_areas") {
		id = sceneUniqueInfrastructureId(m_sceneModel, "signalling-area", facet.toStdString());
		const SceneBlockExtent extent = sceneBlockExtent(buildSceneSectionInventory(m_sceneModel), "");
		m_sceneModel.signallingAreas.push_back({id, extent.startKm, extent.endKm, kSignallingLevelUnset, {}});
		newRow = static_cast<int>(m_sceneModel.signallingAreas.size()) - 1;
	} else if (facet == "routes") {
		id = sceneUniqueInfrastructureId(m_sceneModel, "route", facet.toStdString());
		m_sceneModel.routes.push_back({id, {}, false, {}, false});
		newRow = static_cast<int>(m_sceneModel.routes.size()) - 1;
	} else if (facet == "block_dependencies") {
		m_sceneModel.blockDependencies.push_back({});
		newRow = static_cast<int>(m_sceneModel.blockDependencies.size()) - 1;
	} else if (facet == "single_track_restrictions") {
		m_sceneModel.singleTrackRestrictions.push_back({});
		newRow = static_cast<int>(m_sceneModel.singleTrackRestrictions.size()) - 1;
	} else if (facet == "station_boundaries") {
		m_sceneModel.stationBoundaries.push_back({});
		newRow = static_cast<int>(m_sceneModel.stationBoundaries.size()) - 1;
	}
	if (facet != "platforms")
		m_infrastructurePanel->setSelectionId(QString::fromStdString(id));
	markSceneDirty();
	refreshValidationPanel();
	if (facet == "stations" || facet == "platforms" || facet == "routes")
		m_servicePanel->refresh();
	if (facet == "signals" || facet == "blocks")
		refreshIncidentPanel();
	m_infrastructurePanel->refreshTable();
	if (facet != QStringLiteral("blocks") && m_infrastructureTable
		&& newRow >= 0 && newRow < m_infrastructureTable->rowCount())
		m_infrastructureTable->setCurrentCell(newRow, 0);
}

void MainWindow::insertBlock() {
	if (!m_sceneLoaded || m_worker || !m_infrastructureTable || !m_infrastructureFacetCombo
		|| m_infrastructureFacetCombo->currentData().toString() != QStringLiteral("blocks"))
		return;
	const std::vector<int>& blockRows = m_infrastructurePanel->blockRowModelIndices();
	const int row = m_infrastructureTable->currentRow();
	if (row < 0 || row >= static_cast<int>(blockRows.size()))
		return;
	const int modelIndex = blockRows[static_cast<std::size_t>(row)];
	if (modelIndex < 0 || modelIndex >= static_cast<int>(m_sceneModel.blocks.size()))
		return;
	const std::string trackId = m_sceneModel.blocks[static_cast<std::size_t>(modelIndex)].trackId;
	const std::string id = sceneUniqueInfrastructureId(m_sceneModel, "block", "blocks");
	m_sceneModel.blocks.insert(m_sceneModel.blocks.begin() + modelIndex, {id, trackId, 1.0});
	m_infrastructurePanel->setSelectionId(QString::fromStdString(id));
	markSceneDirty();
	refreshValidationPanel();
	refreshIncidentPanel();
	m_infrastructurePanel->refreshTable();
}

void MainWindow::moveBlock(int offset) {
	if (!m_sceneLoaded || m_worker || !m_infrastructureTable
		|| !m_infrastructureFacetCombo
		|| m_infrastructureFacetCombo->currentData().toString() != QStringLiteral("blocks"))
		return;
	const std::vector<int>& blockRows = m_infrastructurePanel->blockRowModelIndices();
	const int row = m_infrastructureTable->currentRow();
	const int neighborRow = row + offset;
	if (row < 0 || row >= static_cast<int>(blockRows.size())
		|| neighborRow < 0 || neighborRow >= static_cast<int>(blockRows.size()))
		return;
	const int modelIndex = blockRows[static_cast<std::size_t>(row)];
	const int neighbor = blockRows[static_cast<std::size_t>(neighborRow)];
	if (modelIndex < 0 || modelIndex >= static_cast<int>(m_sceneModel.blocks.size())
		|| neighbor < 0 || neighbor >= static_cast<int>(m_sceneModel.blocks.size()))
		return;
	std::swap(m_sceneModel.blocks[static_cast<std::size_t>(modelIndex)],
		m_sceneModel.blocks[static_cast<std::size_t>(neighbor)]);
	m_infrastructurePanel->setSelectionId(QString::fromStdString(
		m_sceneModel.blocks[static_cast<std::size_t>(neighbor)].id));
	markSceneDirty();
	refreshValidationPanel();
	refreshIncidentPanel();
	m_infrastructurePanel->refreshTable();
}

void MainWindow::addRouteSection() {
	if (!m_sceneLoaded || m_worker || !m_infrastructureTable || !m_routeSectionCatalogCombo
		|| !m_infrastructureFacetCombo
		|| m_infrastructureFacetCombo->currentData().toString() != QStringLiteral("routes"))
		return;
	const int row = m_infrastructureTable->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.routes.size())
		|| m_routeSectionCatalogCombo->currentIndex() < 0)
		return;
	const std::string section = m_routeSectionCatalogCombo->currentData().toString().toStdString();
	if (section.empty())
		return;
	m_sceneModel.routes[static_cast<std::size_t>(row)].blocks.push_back(section);
	m_infrastructurePanel->setSelectionId(QString::fromStdString(m_sceneModel.routes[static_cast<std::size_t>(row)].id));
	markSceneDirty();
	refreshValidationPanel();
	m_infrastructurePanel->refreshTable();
	m_routeSectionListWidget->setCurrentRow(m_routeSectionListWidget->count() - 1);
}

void MainWindow::removeRouteSection() {
	if (!m_sceneLoaded || m_worker || !m_infrastructureTable || !m_routeSectionListWidget
		|| !m_infrastructureFacetCombo
		|| m_infrastructureFacetCombo->currentData().toString() != QStringLiteral("routes"))
		return;
	const int routeRow = m_infrastructureTable->currentRow();
	const int sectionRow = m_routeSectionListWidget->currentRow();
	if (routeRow < 0 || routeRow >= static_cast<int>(m_sceneModel.routes.size())
		|| sectionRow < 0
		|| sectionRow >= static_cast<int>(m_sceneModel.routes[static_cast<std::size_t>(routeRow)].blocks.size()))
		return;
	m_sceneModel.routes[static_cast<std::size_t>(routeRow)].blocks.erase(
		m_sceneModel.routes[static_cast<std::size_t>(routeRow)].blocks.begin() + sectionRow);
	markSceneDirty();
	refreshValidationPanel();
	m_infrastructurePanel->refreshTable();
	m_routeSectionListWidget->setCurrentRow(std::min(sectionRow,
		m_routeSectionListWidget->count() - 1));
}

void MainWindow::moveRouteSectionUp() {
	if (!m_sceneLoaded || m_worker || !m_infrastructureTable || !m_routeSectionListWidget
		|| !m_infrastructureFacetCombo
		|| m_infrastructureFacetCombo->currentData().toString() != QStringLiteral("routes"))
		return;
	const int routeRow = m_infrastructureTable->currentRow();
	const int sectionRow = m_routeSectionListWidget->currentRow();
	if (routeRow < 0 || routeRow >= static_cast<int>(m_sceneModel.routes.size()) || sectionRow <= 0
		|| sectionRow >= static_cast<int>(m_sceneModel.routes[static_cast<std::size_t>(routeRow)].blocks.size()))
		return;
	auto& blocks = m_sceneModel.routes[static_cast<std::size_t>(routeRow)].blocks;
	std::swap(blocks[static_cast<std::size_t>(sectionRow)], blocks[static_cast<std::size_t>(sectionRow - 1)]);
	markSceneDirty();
	refreshValidationPanel();
	m_infrastructurePanel->refreshTable();
	m_routeSectionListWidget->setCurrentRow(sectionRow - 1);
}

void MainWindow::moveRouteSectionDown() {
	if (!m_sceneLoaded || m_worker || !m_infrastructureTable || !m_routeSectionListWidget
		|| !m_infrastructureFacetCombo
		|| m_infrastructureFacetCombo->currentData().toString() != QStringLiteral("routes"))
		return;
	const int routeRow = m_infrastructureTable->currentRow();
	const int sectionRow = m_routeSectionListWidget->currentRow();
	if (routeRow < 0 || routeRow >= static_cast<int>(m_sceneModel.routes.size()))
		return;
	auto& blocks = m_sceneModel.routes[static_cast<std::size_t>(routeRow)].blocks;
	if (sectionRow < 0 || sectionRow + 1 >= static_cast<int>(blocks.size()))
		return;
	std::swap(blocks[static_cast<std::size_t>(sectionRow)], blocks[static_cast<std::size_t>(sectionRow + 1)]);
	markSceneDirty();
	refreshValidationPanel();
	m_infrastructurePanel->refreshTable();
	m_routeSectionListWidget->setCurrentRow(sectionRow + 1);
}

void MainWindow::deleteInfrastructureEntity() {
	if (!m_sceneLoaded || m_worker || !m_infrastructureFacetCombo || !m_infrastructureTable)
		return;
	const QString facet = m_infrastructureFacetCombo->currentData().toString();
	const int row = m_infrastructureTable->currentRow();
	if (row < 0)
		return;
	int modelRow = row;
	if (facet == QStringLiteral("blocks")) {
		const std::vector<int>& blockRows = m_infrastructurePanel->blockRowModelIndices();
		if (row >= static_cast<int>(blockRows.size()))
			return;
		modelRow = blockRows[static_cast<std::size_t>(row)];
		if (modelRow < 0 || modelRow >= static_cast<int>(m_sceneModel.blocks.size()))
			return;
	}
	const int idColumn = facet == "platforms" ? 1 : 0;
	QString id;
	if (const auto* item = m_infrastructureTable->item(row, idColumn))
		id = item->text();
	else if (auto* combo = qobject_cast<QComboBox*>(m_infrastructureTable->cellWidget(row, idColumn)))
		id = combo->currentData().toString();
	const bool anonymousRow = facet == QStringLiteral("block_dependencies")
		|| facet == QStringLiteral("single_track_restrictions")
		|| facet == QStringLiteral("station_boundaries");
	if (id.isEmpty() && !anonymousRow)
		return;
	QString displayId = id.isEmpty() ? QStringLiteral("incomplete row") : id;
	int stationIndex = -1;
	int platformIndex = -1;
	if (facet == "platforms") {
		int flattened = 0;
		for (int station = 0; station < static_cast<int>(m_sceneModel.stations.size()); ++station) {
			const auto& platforms = m_sceneModel.stations[static_cast<std::size_t>(station)].platforms;
			if (row >= flattened && row < flattened + static_cast<int>(platforms.size())) {
				stationIndex = station;
				platformIndex = row - flattened;
				break;
			}
			flattened += static_cast<int>(platforms.size());
		}
	}
	if (facet == "blocks" && modelRow < static_cast<int>(m_sceneModel.blocks.size()))
		id = QString::fromStdString(m_sceneModel.blocks[static_cast<std::size_t>(modelRow)].id);
	else if (facet == "stations" && row < static_cast<int>(m_sceneModel.stations.size()))
		id = QString::fromStdString(m_sceneModel.stations[static_cast<std::size_t>(row)].id);
	else if (facet == "platforms" && stationIndex >= 0 && platformIndex >= 0)
		id = QString::fromStdString(m_sceneModel.stations[static_cast<std::size_t>(stationIndex)]
				.platforms[static_cast<std::size_t>(platformIndex)]
				.id);
	else if (facet == "signals" && row < static_cast<int>(sceneSignals(m_sceneModel).size()))
		id = QString::fromStdString(sceneSignals(m_sceneModel)[static_cast<std::size_t>(row)].id);
	else if (facet == "signalling_areas" && row < static_cast<int>(m_sceneModel.signallingAreas.size()))
		id = QString::fromStdString(m_sceneModel.signallingAreas[static_cast<std::size_t>(row)].id);
	else if (facet == "routes" && row < static_cast<int>(m_sceneModel.routes.size()))
		id = QString::fromStdString(m_sceneModel.routes[static_cast<std::size_t>(row)].id);
	displayId = id.isEmpty() ? QStringLiteral("incomplete row") : id;
	const std::string platformScope = stationIndex >= 0
		? m_sceneModel.stations[static_cast<std::size_t>(stationIndex)].id
		: std::string();
	const QStringList consumers = directDeleteConsumers(m_sceneModel, facet, id.toStdString(), platformScope);
	if (!consumers.isEmpty()) {
		showBlockingError(this, "Cannot Delete Infrastructure Entity",
			QString("Cannot delete %1 '%2' because it is still referenced by:\n• %3\nRemove or rebind those consumers first.")
				.arg(m_infrastructureFacetCombo->currentText().toLower(), displayId, consumers.join("\n• ")),
			true);
		return;
	}
	const QMessageBox::StandardButton answer = QMessageBox::question(
		this, "Delete infrastructure entity",
		QString("Delete %1 '%2'?").arg(m_infrastructureFacetCombo->currentText().toLower(), displayId),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (answer != QMessageBox::Yes)
		return;
	if (facet == "blocks" && modelRow < static_cast<int>(m_sceneModel.blocks.size()))
		m_sceneModel.blocks.erase(m_sceneModel.blocks.begin() + modelRow);
	else if (facet == "stations" && row < static_cast<int>(m_sceneModel.stations.size()))
		m_sceneModel.stations.erase(m_sceneModel.stations.begin() + row);
	else if (facet == "platforms" && stationIndex >= 0 && platformIndex >= 0)
		m_sceneModel.stations[static_cast<std::size_t>(stationIndex)].platforms.erase(
			m_sceneModel.stations[static_cast<std::size_t>(stationIndex)].platforms.begin() + platformIndex);
	else if (facet == "signals" && row < static_cast<int>(sceneSignals(m_sceneModel).size()))
		sceneSignals(m_sceneModel).erase(sceneSignals(m_sceneModel).begin() + row);
	else if (facet == "signalling_areas" && row < static_cast<int>(m_sceneModel.signallingAreas.size()))
		m_sceneModel.signallingAreas.erase(m_sceneModel.signallingAreas.begin() + row);
	else if (facet == "routes" && row < static_cast<int>(m_sceneModel.routes.size()))
		m_sceneModel.routes.erase(m_sceneModel.routes.begin() + row);
	else if (facet == "block_dependencies" && row < static_cast<int>(m_sceneModel.blockDependencies.size()))
		m_sceneModel.blockDependencies.erase(m_sceneModel.blockDependencies.begin() + row);
	else if (facet == "single_track_restrictions" && row < static_cast<int>(m_sceneModel.singleTrackRestrictions.size()))
		m_sceneModel.singleTrackRestrictions.erase(m_sceneModel.singleTrackRestrictions.begin() + row);
	else if (facet == "station_boundaries" && row < static_cast<int>(m_sceneModel.stationBoundaries.size()))
		m_sceneModel.stationBoundaries.erase(m_sceneModel.stationBoundaries.begin() + row);
	else
		return;
	m_infrastructurePanel->setSelectionId(QString());
	markSceneDirty();
	refreshValidationPanel();
	if (facet == "stations" || facet == "platforms" || facet == "routes")
		m_servicePanel->refresh();
	if (facet == "signals" || facet == "blocks")
		refreshIncidentPanel();
	m_infrastructurePanel->refreshTable();
}

void MainWindow::updateTrainUnitSourceStatus() {
	const SceneTrainUnit* unit = m_trainUnitPanel->selectedUnit();
	const TrainUnitSourceLinker::Link* state = unit ? m_trainUnitSourceLinker->linkOf(unit->id) : nullptr;
	m_trainUnitPanel->setSourceState(false, state ? state->dataPath : QString(), state ? state->dataStatus : QString());
	m_trainUnitPanel->setSourceState(true, state ? state->tractionPath : QString(), state ? state->tractionStatus : QString());
}

void MainWindow::refreshInputTractionDiagrams(const std::string& unitId) {
	const SceneTrainUnit* unit = sceneTrainUnitById(m_sceneModel, unitId);
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
	if (initial_variables.durationOverride)
		return initial_variables.times;
	return m_sceneModel.settings.hasDuration
		? m_sceneModel.settings.durationSeconds
		: computeHorizon(m_sceneModel);
}

int MainWindow::totalServiceOccurrences() const {
	return m_sceneLoaded ? sceneTotalOccurrences(m_sceneModel, serviceOccurrenceDuration()) : 0;
}

int MainWindow::inPeriodServiceOccurrences() const {
	return m_sceneLoaded ? sceneInPeriodOccurrences(m_sceneModel, serviceOccurrenceDuration()) : 0;
}

SceneScenario* MainWindow::selectedScenario() {
	return m_sceneLoaded ? m_runSelection.selectedScenario(m_sceneModel) : nullptr;
}

const SceneScenario* MainWindow::selectedScenario() const {
	return m_sceneLoaded ? m_runSelection.selectedScenario(static_cast<const SceneModel&>(m_sceneModel)) : nullptr;
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

void MainWindow::addRecentScene(const QString& path) {
	const QString cleanPath = QFileInfo(path).absoluteFilePath();
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
	const QStringList recent = settings.value(kRecentScenesKey).toStringList();
	// Label entries by scene name; the full path stays in the status tip. When
	// two checkouts share a scene name, a shortened parent path tells them apart.
	QHash<QString, int> nameCount;
	for (const QString& path : recent)
		++nameCount[QFileInfo(path).fileName()];
	for (const QString& path : recent) {
		const QFileInfo info(path);
		const QString name = info.fileName();
		QString label = name;
		if (nameCount.value(name) > 1) {
			QString parent = info.dir().path();
			if (parent.length() > 44)
				parent = parent.left(20) + "..." + parent.right(20);
			label = QString("%1 (%2)").arg(name, parent);
		}
		const bool exists = info.isFile()
			? info.exists() && info.suffix().compare("egscene", Qt::CaseInsensitive) == 0
			: QFileInfo(QDir(path).filePath("scene.json")).exists();
		QAction* action = m_recentScenesMenu->addAction(exists ? label : QString("Unavailable: %1").arg(label));
		action->setEnabled(exists);
		action->setData(path);
		action->setStatusTip(path);
		action->setToolTip(path);
		connect(action, &QAction::triggered, this, [this, action]() {
			requestOpenScene(action->data().toString());
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

void MainWindow::updateCaseLayersPanel() {
	if (!m_caseNameLabel)
		return;

	QString caseName = m_sceneLoaded ? QString::fromStdString(m_sceneModel.name)
									 : QString::fromStdString(initial_variables.name);
	if (caseName.trimmed().isEmpty())
		caseName = QStringLiteral("Canonical scene");
	if (caseName.trimmed().isEmpty())
		caseName = QStringLiteral("Default case study");
	if (m_caseNameLabel)
		m_caseNameLabel->setText(EditorLayout::breakableName(caseName));

	QString readiness = "Ready to run";
	bool blocked = false;
	if (m_sceneLoaded) {
		for (const SceneDiagnostic& diagnostic : m_sceneDiagnostics) {
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
			QString label = QString::fromStdString(simulation.run().trains[train].trainDescription);
			if (label.isEmpty() || label == "None")
				label = QString("Train %1").arg(train + 1);
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
	in.clockOffsetSeconds = m_startOffsetSeconds;
	if (m_snapshot)
		in.timestep = m_snapshot->timestep;
	const int train = m_followTrainCombo && row >= 0 ? m_followTrainCombo->itemData(row).toInt() : -1;
	in.hasRun = train >= 0 && !m_showingTrackPreview && simulation.run().numTrains > 0;
	in.sceneHasServices = in.hasRun || !m_sceneModel.services.empty();
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
	m_startOffsetSeconds = seconds;
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

void MainWindow::showSceneContextMenu(QGraphicsItem* item, const QPointF& scenePos, const QPoint& screenPos, bool keyboard) {

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
		const quint64 revision = m_sceneRevision;
		QMenu* menu = new QMenu(this);
		menu->setAttribute(Qt::WA_DeleteOnClose);
		menu->setTitle("Preview: " + id);
		const auto resolve = [this, kind, id, track, reversed, revision]() {
			return resolvePreviewItem(kind, id, track, reversed, revision);
		};
		connect(menu->addAction("Show details"), &QAction::triggered, this, [this, resolve]() {
			if (auto* current = resolve()) displayPreviewInfo(current);
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
				displayTrainDetails(body, false);
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
				displayStationNodeInfo(current);
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
				displayArcInfo(current);
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
				displaySignallingInfo(current);
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
				displayPassengerInfo(current);
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
		handleDisableHighlight();
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
	const ReplayBarText bar = replayBarText(replayCoverageOf(m_completedReplay, m_startOffsetSeconds), shownTime);
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

void MainWindow::clearSimulationWorker(bool requestStop) {
	if (m_worker && requestStop)
		m_worker->requestStop();
	if (m_workerThread && m_workerThread->isRunning()) {
		m_workerThread->quit();
		m_workerThread->wait();
	}
	for (auto& observation : m_simulationObservations) {
		QObject::disconnect(observation);
		observation = QMetaObject::Connection();
	}
	// The mailbox can still contain the last worker publication after the
	// thread has stopped. Drop it with the other transient playback state so a
	// queued snapshotAvailable callback cannot repaint an old run.
	simulation.takeSimulationSnapshot();
	m_presenter->clearOperationalTrackStates();
	// Retain the final frame for passenger inspection until scene teardown.
	m_worker = nullptr;
	m_workerThread = nullptr;
	m_document->setEditable(true);
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
}

// setup GUI
void MainWindow::setupGUI() {
	m_showingTrackPreview = false;

	const bool hasSharedPreview = !m_presenter->m_cachedTrackPreview.lines.empty();
	m_presenter->showRuntime(m_sceneModel);

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
	const bool identityOk = startupTimingIdentityMatches(m_sceneDir, m_sceneModel);
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
		QTimer::singleShot(0, this, &MainWindow::runScene);
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
	extern InitialParameters initial_variables;
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
			setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
			raise();
			activateWindow();
			const SceneOpenChoice choice = classifyOpenPaths({request});
			if (!choice.rejection.isEmpty()) {
				statusBar()->showMessage(QStringLiteral("Open request rejected: %1. Scene unchanged.").arg(choice.rejection), 8000);
				return;
			}
			// The request names the case, so a startup chooser that has not opened yet stays closed.
			initial_variables.nArgProvided = true;
			requestOpenScene(choice.path);
		});
	}
	// Keep update consent behind the startup chooser. Scripted launches have no
	// chooser, so the same queued callback runs after the initial window shows.
	const auto startup = [this, relay]() {
		if (!initial_variables.nArgProvided && !(relay && relay->hasPendingRequest()))
			showStartupChooser();
		showPrivacySettings(true);
		startTelemetryAfterInitialConsent();
		reportInstallerUpdate(this);
		reportRolledBackUpdate(this);
		maybePromptForUpdateChecks();
	};
#ifdef Q_OS_MACOS
	// Finder hands over the file that started the program as an open event, and that event reaches
	// the application only after this callback has run. A chooser that is open by then holds it back
	// until the user closes it, so the chooser waits for the event loop to deliver the event first.
	QTimer::singleShot(0, this, [this, relay, startup]() {
		constexpr int kOpenEventWaitMs = 300;
		if (relay && !initial_variables.nArgProvided && !relay->hasPendingRequest())
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
		QTimer::singleShot(1500, this, &MainWindow::runCurrent);
	}
	if (const auto& afterFirstShow = mainWindowTestHooks().afterFirstShow)
		afterFirstShow(*this);
}

bool MainWindow::hasRawRunResults() const {
	for (int i = 0; i < simulation.run().numTrains; ++i)
		if (simulation.run().trains[i].trajectorySize() > 0)
			return true;
	return false;
}

// Raw trajectories can survive a scene edit; the explicit availability flag is
// the ownership boundary for result actions.
bool MainWindow::hasRunResults() const {
	return m_resultsAvailable && m_completedRun && hasRawRunResults();
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
	const DelayRunSnapshot current = completedDelaySnapshot();
	const bool canSetBaseline = available && !current.scenarioId.empty()
		&& !current.hasIncidents && !current.hasEntranceDelays;
	const bool canCompare = available && m_delayBaseline.has_value()
		&& (current.hasIncidents || current.hasEntranceDelays);
	if (m_setDelayBaselineButton) {
		m_setDelayBaselineButton->setEnabled(canSetBaseline);
		m_setDelayBaselineButton->setToolTip(canSetBaseline
				? QStringLiteral("Freeze this completed run with no incidents or entrance delays as the delay baseline")
				: QStringLiteral("Complete a run with no incidents or entrance delays before setting a delay baseline"));
	}
	if (m_compareDelayButton) {
		m_compareDelayButton->setEnabled(canCompare);
		m_compareDelayButton->setToolTip(canCompare
				? QStringLiteral("Compare the completed run with the frozen delay baseline")
				: QStringLiteral("Run a completed scenario with incidents or entrance delays after setting a baseline"));
	}

	QString feedback;
	if (!available) {
		if (m_delayBaseline) {
			feedback = QStringLiteral("Baseline: %1. Run the selected scenario to create completed results for comparison.")
						   .arg(completedRunContext(m_delayBaseline->provenance));
		} else if (!m_delayBaselineStatus.isEmpty()) {
			feedback = m_delayBaselineStatus;
		} else {
			feedback = QStringLiteral("No completed run. Run the selected scenario; a run with no incidents or entrance delays can become the baseline.");
		}
	} else if (!m_delayBaseline) {
		if (canSetBaseline) {
			feedback = QStringLiteral("Completed run: %1. Set this run with no incidents or entrance delays as the delay baseline.")
						   .arg(completedRunContext(current.provenance));
		} else if (current.hasEntranceDelays) {
			feedback = QStringLiteral("Completed run: %1. This run has entrance delays; run a scenario with no incidents or entrance delays to set the baseline.")
						   .arg(completedRunContext(current.provenance));
		} else {
			feedback = QStringLiteral("Completed run: %1. This run has incidents; run a scenario with no incidents or entrance delays to set the baseline.")
						   .arg(completedRunContext(current.provenance));
		}
	} else if (canCompare) {
		feedback = QStringLiteral("Baseline: %1. Compare delays shows the additional delay of each call against it.")
					   .arg(completedRunContext(m_delayBaseline->provenance));
	} else {
		feedback = QStringLiteral("Baseline: %1. Run a scenario with incidents or entrance delays, then compare.")
					   .arg(completedRunContext(m_delayBaseline->provenance));
	}
	if (!current.scenarioId.empty() && !m_runSelection.scenarioId().empty()
		&& current.scenarioId != m_runSelection.scenarioId()) {
		feedback += QStringLiteral(" Selected next scenario: %1; it has not been run yet.")
						.arg(QString::fromStdString(m_runSelection.scenarioId()));
	}
	if (m_delayFeedbackLabel)
		m_delayFeedbackLabel->setText(feedback);
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
	const QString currentCaseName = m_sceneLoaded
		? QString::fromStdString(m_sceneModel.name)
		: QString();

	// Bundled scenes: below the working directory, next to the executable (Windows
	// and Linux builds), in the macOS bundle, or in a Linux install prefix.
	const QString applicationDir = QCoreApplication::applicationDirPath();
	const QStringList sceneRoots = {
		QDir::currentPath() + "/Scenes",
		applicationDir + "/Scenes",
		applicationDir + "/../Resources/Scenes",
		applicationDir + "/../share/EGTRAIN/Scenes"};
	const QStringList recentPaths = QSettings().value(kRecentScenesKey).toStringList();
	CaseChooserDialog dialog({currentCaseName, sceneRoots, recentPaths}, this);
	if (const auto& dialogShown = mainWindowTestHooks().dialogShown)
		dialogShown(dialog, "startupChooser");
	dialog.exec();

	const CaseChooserDialog::Outcome outcome = dialog.outcome();
	switch (outcome) {
		case CaseChooserDialog::Outcome::OpenSelected:
			if (!dialog.chosenPath().isEmpty()) requestOpenScene(dialog.chosenPath());
			break;
		case CaseChooserDialog::Outcome::BrowseFolder: openSceneFolderDialog(); break;
		case CaseChooserDialog::Outcome::ImportLegacy: actionLoad_Network(); break;
		case CaseChooserDialog::Outcome::NewCase: newScene(); break;
		case CaseChooserDialog::Outcome::Continue:
			if (!currentCaseName.isEmpty())
				statusBar()->showMessage(QString("Ready - %1").arg(currentCaseName));
			break;
	}
	if (const auto& dialogClosed = mainWindowTestHooks().dialogClosed)
		dialogClosed(dialog, "startupChooser");
}

// The Run action always prepares the currently loaded canonical scene first.
void MainWindow::runCurrent() {
	if (m_worker)
		return;
	if (m_sceneLoaded)
		runScene();
	else
		statusBar()->showMessage("Open a canonical scene before running.", 5000);
}

QString MainWindow::runReviewText() const {
	const SceneDiagnosticCounts counts = countDiagnostics(m_sceneDiagnostics);
	QString incidentDetails;
	for (const SceneIncident& incident : selectedScenarioIncidents()) {
		const QString occurrence = incident.hasOccurrence || incident.occurrence != 1
			? QString::number(incident.occurrence)
			: QStringLiteral("all");
		const QString window = incident.hasEndSeconds || incident.endSeconds != 0.0
			? QString::number(incident.endSeconds, 'g', 12)
			: QStringLiteral("until-destination");
		QStringList options;
		options << (incident.hasReducedSpeed
				? QString("reduced-speed=%1 km/h").arg(QString::number(incident.reducedSpeedKmh, 'g', 12))
				: QStringLiteral("reduced-speed=none"));
		options << (incident.hasEndSeconds ? QStringLiteral("recovery-end=yes")
										   : QStringLiteral("recovery-end=no"));
		options << (incident.terminateAtDestination
				? QStringLiteral("terminate-at-destination=yes")
				: QStringLiteral("terminate-at-destination=no"));
		QString detail = QString("id=%1 type=%2 target=%3 start=%4 window=%5 occurrence=%6 options=%7")
							 .arg(QString::fromStdString(incident.id), QString::fromStdString(incident.type),
								 incident.target.empty() ? QStringLiteral("(empty)") : QString::fromStdString(incident.target))
							 .arg(QString::number(incident.startSeconds, 'g', 12), window, occurrence, options.join(","));
		if (!incidentDetails.isEmpty())
			incidentDetails += "; ";
		incidentDetails += detail;
	}
	QString entranceDelayDetails;
	const SceneScenario* scenario = selectedScenario();
	if (scenario) {
		for (const SceneEntranceDelay& delay : scenario->entranceDelays) {
			const auto service = std::find_if(m_sceneModel.services.begin(), m_sceneModel.services.end(),
				[&delay](const SceneService& candidate) { return candidate.id == delay.serviceId; });
			const QString operatingCode = service == m_sceneModel.services.end()
				? QStringLiteral("(unavailable)")
				: QString::fromStdString(sceneServiceOccurrenceOperatingCode(*service, delay.occurrence));
			QString detail = QString("service=%1 occurrence=%2 operating_code=%3 station=%4 seconds=%5")
								 .arg(delay.serviceId.empty() ? QStringLiteral("(empty)") : QString::fromStdString(delay.serviceId))
								 .arg(delay.occurrence)
								 .arg(operatingCode.isEmpty() ? QStringLiteral("(unavailable)") : operatingCode)
								 .arg(delay.stationId.empty() ? QStringLiteral("(empty)") : QString::fromStdString(delay.stationId))
								 .arg(QString::number(delay.delaySeconds, 'g', 12));
			if (!entranceDelayDetails.isEmpty())
				entranceDelayDetails += "; ";
			entranceDelayDetails += detail;
		}
	}
	QString summary = QString("Case study: %1\nScenario: %2\nService definitions: %3\nConfigured total: %4\nNumber of services in sim.: %5\nSelected: %6\nSelected in period: %7\nCompositions: %8\nIncidents: %9\nValidation: %10 error(s), %11 warning(s)")
						  .arg(QString::fromStdString(m_sceneModel.name))
						  .arg(scenarioContext())
						  .arg(static_cast<int>(m_sceneModel.services.size()))
						  .arg(totalServiceOccurrences())
						  .arg(inPeriodServiceOccurrences())
						  .arg(m_runSelection.selectedCount(m_sceneModel, serviceOccurrenceDuration()))
						  .arg(m_runSelection.selectedInPeriodCount(m_sceneModel, serviceOccurrenceDuration()))
						  .arg(static_cast<int>(m_sceneModel.compositions.size()))
						  .arg(static_cast<int>(selectedScenarioIncidents().size()))
						  .arg(counts.errors)
						  .arg(counts.warnings);
	summary += QString("\nCounting rule: scheduled entry ≥ 0 and < %1 s; these are configured identities, not observed trains.")
				   .arg(QString::number(serviceOccurrenceDuration(), 'g', 12));
	if (!incidentDetails.isEmpty())
		summary += "\nIncident configuration: " + incidentDetails;
	if (!entranceDelayDetails.isEmpty())
		summary += "\nEntrance delay configuration: " + entranceDelayDetails;
	return summary;
}

bool MainWindow::showRunReview() {
	if (e2eDialogsSuppressed() && !mainWindowTestHooks().askRunReview)
		return true;

	QDialog review(this);
	review.setObjectName("runReviewDialog");
	review.setWindowTitle("Run simulation");
	auto* body = new QWidget;
	body->setObjectName("runReviewBody");
	auto* layout = new QVBoxLayout(body);
	layout->setSpacing(8);
	const bool detailsEnabled = advancedDetailsEnabled();
	const SceneDiagnosticCounts counts = countDiagnostics(m_sceneDiagnostics);
	const double duration = serviceOccurrenceDuration();
	const int selectedInPeriod = m_runSelection.selectedInPeriodCount(m_sceneModel, duration);
	const long long baseSeconds = baseTimeToSeconds(m_sceneModel.baseTime);
	auto* facts = new QGridLayout();
	facts->setHorizontalSpacing(20);
	facts->setVerticalSpacing(8);
	const auto addFact = [&](int row, const QString& label, const QString& value) {
		auto* name = new QLabel(label, body);
		name->setObjectName("runReviewFactLabel");
		auto* fact = new QLabel(value, body);
		fact->setTextFormat(Qt::PlainText);
		fact->setWordWrap(true);
		fact->setObjectName("runReviewFactValue");
		facts->addWidget(name, row, 0);
		facts->addWidget(fact, row, 1);
	};
	addFact(0, "Selected in period", QString::number(selectedInPeriod));
	addFact(1, "Start clock", QString::fromStdString(formatSimTime(0, baseSeconds)));
	addFact(2, "Duration",
		QString("%1 s elapsed (ends near %2)")
			.arg(QString::number(duration, 'g', 12),
				QString::fromStdString(formatSimTime(static_cast<long long>(duration), baseSeconds))));
	addFact(3, "Active incidents", QString::number(static_cast<int>(selectedScenarioIncidents().size())));
	layout->addLayout(facts);

	auto* status = new QLabel(body);
	status->setObjectName("runReviewStatus");
	const bool noServicesInPeriod = selectedInPeriod == 0;
	// The validator message names the route sections and tracks without a signalling level.
	QString unsignalledSections;
	for (const SceneDiagnostic& diagnostic : m_sceneDiagnostics) {
		if (diagnostic.code == "scene.signalling.level.missing") {
			unsignalledSections = QString::fromStdString(diagnostic.message);
			unsignalledSections[0] = unsignalledSections[0].toLower();
			break;
		}
	}
	status->setText(noServicesInPeriod
			? QStringLiteral("Ready to run, but no selected services enter during this period.")
			: !unsignalledSections.isEmpty()
			? QString("Ready to run, but %1.").arg(unsignalledSections)
			: detailsEnabled && counts.warnings > 0
			? QString("Ready to run. Review %1 validation %2 if needed.")
				  .arg(counts.warnings)
				  .arg(counts.warnings == 1 ? "warning" : "warnings")
			: QStringLiteral("Ready to run."));
	status->setProperty("warning", noServicesInPeriod || !unsignalledSections.isEmpty() || (detailsEnabled && counts.warnings > 0));
	status->setWordWrap(true);
	layout->addWidget(status);

	auto* detailsToggle = new QToolButton(body);
	detailsToggle->setText("Run details");
	detailsToggle->setObjectName("runReviewDetailsToggle");
	detailsToggle->setCheckable(true);
	detailsToggle->setArrowType(Qt::RightArrow);
	detailsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	auto* details = new QLabel(body);
	details->setObjectName("runReviewDetails");
	details->setTextFormat(Qt::PlainText);
	details->setWordWrap(true);
	details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	const QString fullDetails = runReviewText();
	const int incidentStart = fullDetails.indexOf("\nIncident configuration:");
	const int delayStart = fullDetails.indexOf("\nEntrance delay configuration:");
	const int firstConfiguration = incidentStart < 0 ? delayStart
		: delayStart < 0							 ? incidentStart
													 : std::min(incidentStart, delayStart);
	QString displayedDetails = detailsEnabled || firstConfiguration < 0
		? fullDetails
		: fullDetails.left(firstConfiguration);
	if (!detailsEnabled) {
		const int validationStart = displayedDetails.indexOf("\nValidation:");
		const int countingStart = displayedDetails.indexOf("\nCounting rule:");
		if (validationStart >= 0 && countingStart > validationStart)
			displayedDetails.remove(validationStart, countingStart - validationStart);
	}
	details->setText(displayedDetails);
	details->hide();
	connect(detailsToggle, &QToolButton::toggled, &review, [detailsToggle, details](bool shown) {
		detailsToggle->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
		details->setVisible(shown);
	});
	layout->addWidget(detailsToggle);
	layout->addWidget(details);

	enum ReviewChoice { CancelRun,
		StartRun,
		ShowLoadedData,
		ShowValidation };
	ReviewChoice choice = CancelRun;
	if (detailsEnabled) {
		auto* inspection = new QHBoxLayout;
		auto* loadedButton = new QPushButton("Loaded Data", body);
		auto* validationButton = new QPushButton("Validation", body);
		inspection->addWidget(loadedButton);
		inspection->addWidget(validationButton);
		inspection->addStretch();
		layout->addLayout(inspection);
		connect(loadedButton, &QPushButton::clicked, &review, [&]() {
			choice = ShowLoadedData;
			review.accept();
		});
		connect(validationButton, &QPushButton::clicked, &review, [&]() {
			choice = ShowValidation;
			review.accept();
		});
	}
	auto* buttons = new QDialogButtonBox(&review);
	buttons->setObjectName("runReviewButtons");
	buttons->addButton(QDialogButtonBox::Cancel);
	auto* runButton = buttons->addButton("Run simulation", QDialogButtonBox::AcceptRole);
	runButton->setObjectName("runReviewRunButton");
	runButton->setDefault(true);
	connect(buttons, &QDialogButtonBox::accepted, &review, [&]() {
		choice = StartRun;
		review.accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, &review, &QDialog::reject);
	DialogLayout::install(review, "Run simulation",
		QString("%1 / %2").arg(QString::fromStdString(m_sceneModel.name), scenarioContext()),
		body, buttons);
	if (const auto& dialogShown = mainWindowTestHooks().dialogShown)
		dialogShown(review, "runReview");
	review.exec();
	if (const auto& dialogClosed = mainWindowTestHooks().dialogClosed)
		dialogClosed(review, "runReview");
	if (choice == StartRun)
		return true;
	if (choice == ShowLoadedData && m_loadedDataDock) {
		m_loadedDataDock->show();
		m_loadedDataDock->raise();
	} else if (choice == ShowValidation && m_validationDock) {
		m_validationDock->show();
		m_validationDock->raise();
	}
	return false;
}

RunProvenance MainWindow::captureRunProvenance() const {
	RunProvenance provenance;
	provenance.caseName = m_sceneModel.name;
	provenance.sceneSchemaVersion = m_sceneModel.schemaVersion;
	provenance.input = captureSavedInput(m_sceneDir.toStdString(),
		m_sceneIsBundle ? "bundle" : "directory", m_sceneDirty, m_savedSceneSha256);
	provenance.appliedScenario = m_runSelection.appliedScenarioId();
	provenance.baseTimeSeconds = static_cast<double>(initial_variables.startingSimulationTime);
	provenance.durationSeconds = initial_variables.times;
	provenance.timestepSeconds = timestep;
	provenance.bufferSeconds = initial_variables.bufferTime;
	provenance.recoveryPercent = initial_variables.recoveryTimePercentage;
	provenance.paxMode = initial_variables.PAX_GUI ? 1 : 0;
	provenance.tsmMode = initial_variables.TSM;
	provenance.routeChoiceMode = initial_variables.RChoice;
	provenance.randomSeed = initial_variables.randomSeed;
	provenance.selectedOccurrences.reserve(static_cast<std::size_t>(std::max(0, simulation.run().numTrains)));
	for (int index = 0; index < simulation.run().numTrains; ++index) {
		const Train& train = simulation.run().trains[index];
		provenance.selectedOccurrences.push_back({train.serviceId, train.serviceOccurrence,
			train.operatingCode});
	}
	return provenance;
}

DelayRunSnapshot MainWindow::completedDelaySnapshot() const {
	DelayRunSnapshot snapshot;
	if (!m_resultsAvailable || !m_completedRun || m_completedRun->results.trains.empty())
		return snapshot;
	const CompletedRunData& run = *m_completedRun;
	snapshot.caseRevision = std::to_string(m_sceneRevision);
	snapshot.scenarioId = m_runSelection.appliedScenarioId();
	snapshot.baseTimeSeconds = static_cast<double>(baseTimeToSeconds(m_sceneModel.baseTime));
	snapshot.durationSeconds = std::isfinite(run.settings.horizonSteps)
		? run.settings.horizonSteps
		: 0.0;
	snapshot.timestep = run.settings.timestepSeconds;
	for (const SceneScenario& scenario : m_sceneModel.scenarios) {
		if (scenario.id != snapshot.scenarioId)
			continue;
		snapshot.hasIncidents = !scenario.incidents.empty();
		snapshot.hasEntranceDelays = !scenario.entranceDelays.empty();
		break;
	}
	snapshot.provenance = run.provenance;
	snapshot.run = run.results;
	snapshot.timetable = run.timetable;
	return snapshot;
}

void MainWindow::setDelayBaseline() {
	const DelayRunSnapshot snapshot = completedDelaySnapshot();
	if (snapshot.scenarioId.empty() || snapshot.run.trains.empty()) {
		QMessageBox::warning(this, "Delay baseline unavailable",
			"Complete a run with no incidents or entrance delays first.");
		return;
	}
	if (snapshot.hasIncidents || snapshot.hasEntranceDelays) {
		QMessageBox::warning(this, "Delay baseline rejected",
			"The delay baseline must have no incidents and no entrance delays.");
		return;
	}
	m_delayBaseline = snapshot;
	m_delayBaselineStatus = QString("Delay baseline set: %1. Compare it with a completed scenario that has incidents or entrance delays.")
								.arg(completedRunContext(snapshot.provenance));
	statusBar()->showMessage(QString("Delay baseline set to scenario %1").arg(QString::fromStdString(snapshot.scenarioId)), 5000);
	refreshRunResults();
	updateDiagramActions();
}

DelayAnalysisWindow* MainWindow::openDelayAnalysis() {
	if (!hasRunResults()) {
		statusBar()->showMessage("Run a simulation to open the delay analysis", 5000);
		return nullptr;
	}
	auto* window = new DelayAnalysisWindow(completedDelaySnapshot(), m_delayBaseline, this);
	window->setTelemetryCapture([this] { return captureTelemetryOperation(); });
	window->setAttribute(Qt::WA_DeleteOnClose);
	window->show();
	return window;
}

void MainWindow::showDelayAnalysis() {
	openDelayAnalysis();
}

void MainWindow::showDelayComparison() {
	if (DelayAnalysisWindow* window = openDelayAnalysis())
		window->showBaselineTab();
}

// starts EGTRAIN simulation on a worker thread
void MainWindow::startSimulation(const telemetry::OperationObservation& operation) {
	if (m_worker)
		return; // already running

	clearReplay();
	// show progress bar
	progressBar->show();
	statusBar()->showMessage("Simulation running...");

	// create worker and thread; sync slider before run() starts
	m_worker = new SimulationWorker();
	m_document->setEditable(false);
	refreshInfrastructurePanel();
	m_worker->setDelayMs(stepDelayForSlider(m_speedSlider->value()));
	refreshIncidentPanel();
	updateSceneActions();
	m_workerThread = new QThread(this);
	m_worker->moveToThread(m_workerThread);

	// when the thread starts, begin the simulation
	connect(m_workerThread, &QThread::started, m_worker, &SimulationWorker::run);
	// when simulation finishes on the worker, handle results on main thread
	connect(m_worker, &SimulationWorker::simulationFinished, this, [this, worker = m_worker]() {
		// A stopped run can leave a queued completion after another case opens.
		if (worker && worker == m_worker)
			onSimulationFinished();
		runDeferredAction();
	});
	// clean up when thread finishes
	connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
	connect(m_workerThread, &QThread::finished, m_workerThread, &QObject::deleteLater);

	if (m_telemetrySender) {
		const auto observationStep = mainWindowTestHooks().observationStep;
		const auto rollback = [this]() noexcept {
			for (auto& connection : m_simulationObservations) {
				QObject::disconnect(connection);
				connection = QMetaObject::Connection();
			}
		};
		// Telemetry is optional. Its allocation/connection failures cannot stop Run.
		try {
			if (observationStep) observationStep(0);
			auto observation = std::make_shared<telemetry::SimulationObservation>(operation);
			m_simulationObservations[0] = connect(
				&simulation, &DispatchController::executionRejected, &simulation,
				[observation] { observation->reject(telemetry::Error::InvalidInput); }, Qt::DirectConnection);
			if (observationStep) observationStep(1);
			m_simulationObservations[1] = connect(
				&simulation, &DispatchController::executionBegan, &simulation,
				[observation] { observation->begin(); }, Qt::DirectConnection);
			if (observationStep) observationStep(2);
			m_simulationObservations[2] = connect(
				&simulation, &DispatchController::executionReturned, &simulation,
				[observation](qint64 elapsedMs, bool cancelled) {
					observation->finish(
						cancelled ? telemetry::SimulationObservation::Outcome::Cancelled : telemetry::SimulationObservation::Outcome::Completed,
						elapsedMs);
				},
				Qt::DirectConnection);
			if (observationStep) observationStep(3);
			if (!m_simulationObservations[0] || !m_simulationObservations[1] || !m_simulationObservations[2])
				rollback();
		} catch (...) {
			rollback();
		}
	}
	if (const auto runStarting = mainWindowTestHooks().runStarting)
		runStarting(*this);
	m_workerThread->start();
	ui->actionSimulationPause->setEnabled(true);
	ui->actionSimulationStop->setEnabled(true);
	// A run keeps the output folder and the start time it was prepared with.
	if (m_outputFolderAction)
		m_outputFolderAction->setEnabled(false);
	ui->actionStartTime->setEnabled(false);
}

// With a run active: asks it to stop, shows the stopping state and keeps the action for the
// moment its worker has finished. The last request replaces an earlier one. Returns false
// without a run; the caller then acts at once.
bool MainWindow::deferUntilRunStopped(std::function<void()> action) {
	if (!m_worker)
		return false;
	m_afterRunAction = std::move(action);
	m_worker->requestStop();
	ui->actionSimulationPause->setEnabled(false);
	ui->actionSimulationStop->setEnabled(false);
	statusBar()->showMessage(QStringLiteral("Stopping simulation..."));
	return true;
}

void MainWindow::runDeferredAction() {
	if (m_worker || !m_afterRunAction)
		return;
	const std::function<void()> action = std::move(m_afterRunAction);
	m_afterRunAction = nullptr;
	action();
}

// handle simulation completion on the main thread
void MainWindow::onSimulationFinished() {
	const bool hadFollowTarget = m_followTrainIndex >= 0
		|| (m_followAction && m_followAction->isChecked());
	setFollowTrain(-1);
	if (PlaybackProfiler::enabled() && PlaybackProfiler::instance().frozen()) {
		PlaybackProfiler::instance().emitRecords(true);
		clearSimulationWorker(false);
		QCoreApplication::exit(0);
		return;
	}
	const bool sceneChangedDuringRun = m_sceneChangedDuringRun;
	m_sceneChangedDuringRun = false;
	// A stop that arrives after the last stage leaves a completed run.
	const bool stopped = m_worker && m_worker->isStopRequested() && !simulation.lastRunCompleted();
	const bool stoppedByUser = stopped && !sceneChangedDuringRun;
	m_resultsAvailable = !sceneChangedDuringRun && !stopped && hasRawRunResults();
	m_runtimeStatus = m_resultsAvailable ? QStringLiteral("Completed")
		: stoppedByUser					 ? QStringLiteral("Stopped")
										 : QStringLiteral("Failed");
	if (m_resultsAvailable) {
		// Freeze result values before a subsequent run replaces the runtime trains.
		m_completedRun = std::make_shared<const CompletedRunData>(
			captureCompletedRun(simulation.run(), &m_sceneModel, m_startOffsetSeconds, m_pendingRunProvenance));
		refreshRunResults();
	} else {
		m_pendingRunProvenance = RunProvenance();
		m_completedRun.reset();
		if (m_runResultsTable)
			m_runResultsTable->setRowCount(0);
		if (m_runResultsSummaryLabel)
			m_runResultsSummaryLabel->setText(QString("No results | Case: %1 | Scenario: %2 | Status: %3%4")
					.arg(QString::fromStdString(m_sceneModel.name), scenarioContext(), m_runtimeStatus,
						sceneChangedDuringRun ? QStringLiteral(" (scene changed during run)") : QString()));
		if (m_runResultsDock) {
			m_runResultsDock->setWindowTitle(
				QString("Run Results — %1 (%2)")
					.arg(scenarioContext(), stoppedByUser ? QStringLiteral("stopped") : QStringLiteral("failed")));
			m_runResultsDock->hide();
		}
	}
	m_pendingRunProvenance = RunProvenance();
	refreshLoadedDataTree();

	if (const auto runFinished = mainWindowTestHooks().runFinished; runFinished && runFinished(*this))
		return;

	// print last services
	if (!stopped)
		simulation.printLastTrainServicePathDiagram();

	// hide progress bar
	progressBar->hide();
	statusBar()->showMessage(sceneChangedDuringRun
			? QStringLiteral("Simulation finished; results discarded because the scene changed during the run")
			: stopped ? QStringLiteral("Simulation stopped")
			: hadFollowTarget
			? QStringLiteral("Simulation complete - Follow disabled")
			: QStringLiteral("Simulation complete - open the Diagrams menu for results"));
	// The Run Results dock raised by refreshRunResults is the completion notice;
	// diagram entries switch on here instead of a modal prompt chain.
	// cleanup thread
	clearSimulationWorker(false);
	if (m_resultsAvailable && !stopped) {
		m_completedReplay = simulation.takeReplayCandidate();
		if (!m_completedReplay.empty() || m_completedReplay.oversize())
			showReplayBar();
	} else simulation.resetReplayCandidate();
	// A completed run can be replayed, so a train that has left can be followed in it.
	updateFollowAvailability();
	refreshInfrastructurePanel();
	refreshIncidentPanel();
	updateSceneActions();
	m_trainUnitSourceLinker->processChanges();
	// The track preview that replaces a stopped run wrote its own status message.
	if (stoppedByUser)
		statusBar()->showMessage(QStringLiteral("Simulation stopped"));

	if (const auto runSettled = mainWindowTestHooks().runSettled)
		runSettled(*this);
}

void MainWindow::teardownGUI() {
	clearPreviewInspection();
	m_showingTrackPreview = true;
	// Stop any running simulation before clearing scene objects it may reference.
	clearSimulationWorker(true);
	clearReplay();
	progressBar->hide();

	// The presenter stops the movement of the trains, deletes all items of the scene and drops the pointers to them.
	m_presenter->clear();
	m_selectedStationName.clear();
	m_hasSelectedStationIdentity = false;
	m_selectedStationNodeId = 0.0;
	m_selectedStationTrack = -1;
	if (m_networkLegendWidget)
		m_networkLegendWidget->setCaseContent(NetworkLegendContent());
	m_infrastructurePanel->setSelectionId(QString());

	setFollowTrain(-1);
	m_selectedTrainIndex = -1;

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

// The choice is stored in the controller and applied when the next run is prepared, so the
// folder of a run that is already active never changes.
void MainWindow::chooseOutputFolder() {
	extern InitialParameters initial_variables;
	const std::string& chosen = simulation.nextRunOutputFolder();
	QString dir = QFileDialog::getExistingDirectory(this, "Choose Output Folder",
		QString::fromStdString(chosen.empty() ? initial_variables.OutputMainFolder : chosen));
	if (dir.isEmpty())
		return;
	QDir().mkpath(dir);
	simulation.setNextRunOutputFolder(dir.toStdString());
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
	setStartOffset(secs);
	updateCaseLayersPanel();
	statusBar()->showMessage(QString("Start time set to %1").arg(text), 3000);
}

void MainWindow::runScene() {
	const auto operation = captureTelemetryOperation();
	extern InitialParameters initial_variables;
	if (!m_sceneLoaded)
		return;

	if (PlaybackProfiler::enabled()) {
		const std::string requested = qEnvironmentVariable(
			"QEGTRAIN_PLAYBACK_PROFILE_SCENARIO")
										  .toStdString();
		const SceneScenario* configured = requested == "default"
			? defaultScenario(static_cast<const SceneModel&>(m_sceneModel))
			: nullptr;
		if (requested != "default") {
			const auto match = std::find_if(m_sceneModel.scenarios.cbegin(), m_sceneModel.scenarios.cend(),
				[&requested](const SceneScenario& scenario) { return scenario.id == requested; });
			if (match != m_sceneModel.scenarios.cend())
				configured = &*match;
		}
		if (!configured) {
			std::fprintf(stderr, "QEGTRAIN_PLAYBACK_PROFILE {\"type\":\"error\",\"reason\":\"scenario_unavailable\"}\n");
			QCoreApplication::exit(2);
			return;
		}
		m_runSelection.selectScenario(configured->id);
	}

	refreshValidationPanel();
	if (hasErrors(m_sceneDiagnostics)) {
		operation.failure(telemetry::Operation::Simulation, telemetryDiagnosticError(m_sceneDiagnostics));
		int errorCount = countDiagnostics(m_sceneDiagnostics).errors;
		showBlockingError(this, "Cannot Run Scene",
			QString("The scene has %1 validation error(s) that must be fixed before running.").arg(errorCount), true);
		return;
	}
	m_runSelection.pruneExcluded(m_sceneModel, serviceOccurrenceDuration());
	m_lastRunTotalOccurrences = totalServiceOccurrences();
	m_lastRunSelectedOccurrences = m_runSelection.selectedCount(m_sceneModel, serviceOccurrenceDuration());
	if (m_lastRunSelectedOccurrences <= 0) {
		operation.failure(telemetry::Operation::Simulation, telemetry::Error::InvalidInput);
		showBlockingError(this, "Cannot Run Scene", "Select at least one generated service occurrence before running.", true);
		return;
	}
	if (!showRunReview())
		return;

	statusBar()->showMessage("Preparing scene simulation...");
	if (!startupTimingEnabled())
		QApplication::processEvents();

	const QRectF previewFitBounds = m_presenter->m_previewFitBounds;
	const QPointF previewZoomFocus = m_presenter->m_previewZoomFocus;
	teardownGUI();
	m_presenter->m_previewFitBounds = previewFitBounds;
	m_presenter->m_previewZoomFocus = previewZoomFocus;
	// A case that was renamed after it was opened or created runs in the folder of its present name.
	selectSceneOutputFolder(m_sceneModel.name);
	QString outputPath = QString::fromStdString(initial_variables.OutputMainFolder);
	if (outputPath.isEmpty() || QDir(outputPath).isRelative()) {
		QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
		if (base.isEmpty())
			base = QDir::homePath() + "/EGTRAIN";
		outputPath = QDir(base).absoluteFilePath(outputPath.isEmpty() ? "Output" : outputPath);
		QDir().mkpath(outputPath);
		initial_variables.OutputMainFolder = outputPath.toStdString();
	}
	initial_variables.GUI = 1;
	invalidateRunResults();
	m_runSelection.applySelectedScenario();
	const SceneRunSelection selection = m_runSelection.selectedOccurrences(m_sceneModel, serviceOccurrenceDuration());
	const bool identityBeforePrepare = !startupTimingEnabled()
		|| startupTimingIdentityMatches(m_sceneDir, m_sceneModel);
	const qint64 prepareStarted = startupTimingEnabled() ? startupTimingNowNanoseconds() : 0;
	const std::vector<SceneDiagnostic> diagnostics = simulation.prepareScene(m_sceneModel, m_runSelection.scenarioId(), selection);
	const bool timingIdentityOk = identityBeforePrepare
		&& (!startupTimingEnabled() || startupTimingIdentityMatches(m_sceneDir, m_sceneModel));
	if (startupTimingEnabled())
		recordStartupTiming(QStringLiteral("native_runtime_construction"), m_startupTimingIteration,
			-1, startupTimingNowNanoseconds() - prepareStarted,
			QStringLiteral("simulation.prepareScene"), QStringLiteral("MainWindow::runScene"),
			timingIdentityOk);
	m_runtimeDiagnostics = diagnostics;
	if (hasErrors(diagnostics)) {
		operation.failure(telemetry::Operation::Simulation, telemetryDiagnosticError(diagnostics));
		m_runtimeStatus = QStringLiteral("Failed");
		updateSceneActions();
		refreshValidationPanel();
		showBlockingError(this, "Cannot Run Scene", firstDiagnosticMessage(diagnostics), true);
		return;
	}
	m_runtimeStatus = QStringLiteral("Ready");
	refreshLoadedDataTree();
	initial_variables.startingSimulationTime = baseTimeToSeconds(m_sceneModel.baseTime);
	m_startOffsetSeconds = initial_variables.startingSimulationTime;
	m_pendingRunProvenance = captureRunProvenance();

	setupGUI();
	fitView();
	if (PlaybackProfiler::enabled()) {
		PlaybackProfiler::RunConfig config;
		config.trial = qEnvironmentVariableIntValue("QEGTRAIN_PLAYBACK_PROFILE_TRIAL");
		config.view = qEnvironmentVariable("QEGTRAIN_PLAYBACK_PROFILE_VIEW").toStdString();
#if defined(Q_OS_MACOS)
		config.platform = "macOS";
#elif defined(Q_OS_WIN)
		config.platform = "Windows";
#else
		config.platform = "Linux";
#endif
		config.architecture = QSysInfo::currentCpuArchitecture().toStdString();
		config.qtPlatform = QGuiApplication::platformName().toStdString();
		config.buildType = EGTRAIN_BUILD_TYPE;
		config.caseName = m_sceneModel.name;
		config.requestedScenario = qEnvironmentVariable(
			"QEGTRAIN_PLAYBACK_PROFILE_SCENARIO")
									   .toStdString();
		config.scenario = m_runSelection.appliedScenarioId();
		config.defaultScenario = m_sceneModel.defaultScenarioId;
		config.targetZoom = config.view == "dense" ? 3.0 : 1.0;
		config.durationMs = qEnvironmentVariableIntValue("QEGTRAIN_PLAYBACK_PROFILE_DURATION_MS");
		config.delayMs = qEnvironmentVariableIntValue("QEGTRAIN_PLAYBACK_PROFILE_DELAY_MS");
		config.passengerGui = initial_variables.PAX_GUI;
		config.tsm = initial_variables.TSM != 0;
		config.routeChoice = initial_variables.RChoice != 0;
		config.horizon = static_cast<int>(initial_variables.times);
		config.structural = qEnvironmentVariable("QEGTRAIN_PLAYBACK_PROFILE_STRUCTURAL") == QLatin1String("1");
		PlaybackProfiler::instance().configure(config);
		m_playbackProfileViewApplied = false;
	}
	if (startupTimingEnabled()) {
		if (!timingIdentityOk) {
			failStartupTiming(QStringLiteral("scene identity or counts changed during native preparation"));
			return;
		}
		networkView->armTimingPaint(QStringLiteral("runtime"), m_startupTimingIteration);
		statusBar()->showMessage(QString("Prepared scene: %1").arg(QString::fromStdString(m_sceneModel.name)));
		return;
	}

	// scene stays loaded; only the case-study load path clears it
	updateSceneWindowTitle();
	updateCaseLayersPanel();
	updateSceneActions();

	m_runtimeStatus = QStringLiteral("Running");
	refreshLoadedDataTree();
	startSimulation(operation);
	statusBar()->showMessage(QString("Running scene: %1").arg(QString::fromStdString(m_sceneModel.name)));
}

void MainWindow::actionLoad_Network() {
	const auto operation = captureTelemetryOperation();
	const QString sourceDir = QFileDialog::getExistingDirectory(this, "Select Legacy Case Folder", QDir::homePath());
	if (sourceDir.isEmpty())
		return;
	const QString destinationDir = QFileDialog::getExistingDirectory(this, "Select Scene Destination", QDir::homePath());
	importLegacyCase(sourceDir, destinationDir, operation);
}

void MainWindow::importLegacyCase(const QString& sourceDir, const QString& destinationDir,
	const telemetry::OperationObservation& operation) {
	if (sourceDir.isEmpty() || destinationDir.isEmpty())
		return;

	const QString sourceCanonical = QFileInfo(sourceDir).canonicalFilePath();
	const QString destinationCanonical = QFileInfo(destinationDir).canonicalFilePath();
	const QString sourcePath = sourceCanonical.isEmpty() ? QDir(sourceDir).absolutePath() : sourceCanonical;
	const QString destinationPath = destinationCanonical.isEmpty() ? QDir(destinationDir).absolutePath() : destinationCanonical;
	auto containsPath = [](const QString& parent, const QString& child) {
		return child == parent || child.startsWith(parent + QDir::separator());
	};
	if (containsPath(sourcePath, destinationPath) || containsPath(destinationPath, sourcePath)) {
		showBlockingError(this, "Cannot Import Legacy Case",
			"Choose a scene destination that is separate from and outside the legacy source folder.");
		return;
	}

	if (!maybeSaveScene())
		return;

	statusBar()->showMessage("Importing legacy case...");
	QApplication::processEvents();

	auto diagnosticSummary = [](const std::vector<SceneDiagnostic>& diagnostics) {
		int errors = 0;
		int warnings = 0;
		QStringList details;
		for (const auto& d : diagnostics) {
			if (d.severity == SceneSeverity::Error)
				++errors;
			else if (d.severity == SceneSeverity::Warning)
				++warnings;
			if (details.size() < 4) {
				QString detail = QString::fromStdString(d.message);
				if (!d.file.empty())
					detail += QString(" [%1]").arg(QString::fromStdString(d.file));
				details << detail;
			}
		}
		QString summary = QString("Import: %1 error(s), %2 warning(s)").arg(errors).arg(warnings);
		if (!details.isEmpty())
			summary += "\n" + details.join("\n");
		return summary;
	};

	const QString sceneName = QFileInfo(sourcePath).fileName().isEmpty()
		? QString("Imported Legacy Scene")
		: QFileInfo(sourcePath).fileName();
	SceneImportResult importResult = importLegacyScene(sourcePath.toStdString(), destinationPath.toStdString(),
		sceneName.toStdString());
	if (!importResult.success()) {
		showBlockingError(this, "Cannot Import Legacy Case", diagnosticSummary(importResult.diagnostics));
		return;
	}

	SceneLoadResult loadResult = loadScene(destinationPath.toStdString());
	std::vector<SceneDiagnostic> diagnostics = importResult.diagnostics;
	diagnostics.insert(diagnostics.end(), loadResult.diagnostics.begin(), loadResult.diagnostics.end());
	if (errorDiagnosticCount(loadResult.diagnostics) > 0) {
		showBlockingError(this, "Cannot Open Imported Scene", diagnosticSummary(diagnostics));
		return;
	}
	const auto semanticDiagnostics = validateRunnableScene(loadResult.scene);
	diagnostics.insert(diagnostics.end(), semanticDiagnostics.begin(), semanticDiagnostics.end());
	showBlockingError(this, "Legacy Import Diagnostics", diagnosticSummary(diagnostics), true);
	if (deferUntilRunStopped([this, destinationPath, operation] { openSceneDirectory(destinationPath, operation); }))
		return;
	openSceneDirectory(destinationPath, operation);
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
		const std::shared_ptr<const CompletedRunData> run = m_completedRun;
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
		if (!buffer.open(QIODevice::WriteOnly) || !m_runResultsTable->grab().save(&buffer, "PNG")) {
			operation.failure(telemetry::Operation::Export, telemetry::Error::InternalFailure);
			QMessageBox::warning(this, "Export failed", QString("Could not write the image to:\n%1").arg(path));
			return;
		}
		const std::string bytes(data.constData(), static_cast<std::size_t>(data.size()));
		const bool written = writeRunArtifactWithProvenance(path.toStdString(), "png", bytes,
			m_completedRun ? m_completedRun->provenance : RunProvenance());
		operation.exportFinished(telemetry::ExportKind::Png, written, true, telemetry::Error::IoFailure);
		if (!written)
			QMessageBox::warning(this, "Export failed",
				QString("Could not export the image and provenance to:\n%1").arg(path));
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
	connect(m_setDelayBaselineButton, &QPushButton::clicked, this, &MainWindow::setDelayBaseline);
	delayRow->addWidget(m_setDelayBaselineButton);
	m_compareDelayButton = new QPushButton("Compare delays", container);
	m_compareDelayButton->setObjectName("compareDelayButton");
	m_compareDelayButton->setToolTip("Compare the completed run with the frozen delay baseline");
	connect(m_compareDelayButton, &QPushButton::clicked, this, &MainWindow::showDelayComparison);
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
	if (!m_runResultsDock || !m_runResultsTable || !m_completedRun || m_completedRun->results.trains.empty())
		return;
	const std::shared_ptr<const CompletedRunData> run = m_completedRun;
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
	if (!m_runResultsSummaryLabel || !m_completedRun || m_completedRun->results.trains.empty())
		return;
	int destinationTerminationCount = 0;
	for (const TrainRunResult& result : m_completedRun->results.trains) {
		if (result.destinationTerminated)
			++destinationTerminationCount;
	}
	QString summary = QString("Run: %1 | Occurrences: %2/%3 selected | Status: Completed")
						  .arg(completedRunContext(m_completedRun->provenance))
						  .arg(m_lastRunSelectedOccurrences)
						  .arg(m_lastRunTotalOccurrences);
	if (advancedDetailsEnabled()) {
		summary += QString(" | %1 | Destination terminations: %2")
					   .arg(QString::fromStdString(directIncidentEvidenceText(m_completedRun->results)))
					   .arg(destinationTerminationCount);
	}
	if (m_delayBaseline)
		summary += QString("\nDelay baseline: %1").arg(completedRunContext(m_delayBaseline->provenance));
	m_runResultsSummaryLabel->setText(summary);
}

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
	arcOperationalStateText = new QLineEdit(arcInfoWidget);
	arcOperationalStateText->setObjectName("arcOperationalStateText");
	arcConnectedSignalsText = new QLineEdit(arcInfoWidget);
	arcConnectedSignalsText->setObjectName("arcConnectedSignalsText");
	arcFormLayout = new QFormLayout();
	arcFormLayout->addRow("Arc ID", arcIDText);
	arcFormLayout->addRow("First Node ID", arcFirstNodeIDText);
	arcFormLayout->addRow("Second Node ID", arcSecondNodeIDText);
	arcFormLayout->addRow("Track ID", arcTrackIDText);
	arcFormLayout->addRow("Length (m)", arcLengthText);
	arcFormLayout->addRow("Curvature (m)", arcCurvatureText);
	arcFormLayout->addRow("Gradient (rise/length)", arcGradientText);
	arcFormLayout->addRow("Speed Limit (m/s)", arcSpeedLimitText);
	arcFormLayout->addRow("Operational state", arcOperationalStateText);
	arcFormLayout->addRow("Connected signals", arcConnectedSignalsText);
	arcInfoWidget->setLayout(arcFormLayout);

	// Node info widget
	nodeInfoWidget = new QWidget();
	nodeIDText = new QLineEdit(nodeInfoWidget);
	nodeTrackIDText = new QLineEdit(nodeInfoWidget);
	nodeXText = new QLineEdit(nodeInfoWidget);
	nodeYText = new QLineEdit(nodeInfoWidget);
	nodeStationNameText = new QLineEdit(nodeInfoWidget);
	nodeStationNameText->setObjectName("nodeStationNameText");
	nodeRegionText = new QLineEdit(nodeInfoWidget);
	nodeRegionText->setObjectName("nodeRegionText");
	nodeConnectedTracksText = new QLineEdit(nodeInfoWidget);
	nodeConnectedTracksText->setObjectName("nodeConnectedTracksText");
	nodeSignalledText = new QLineEdit(nodeInfoWidget);
	nodeSignalledText->setObjectName("nodeSignalledText");
	nodeFormLayout = new QFormLayout();
	nodeFormLayout->addRow("Node ID", nodeIDText);
	nodeFormLayout->addRow("Track ID", nodeTrackIDText);
	nodeFormLayout->addRow("X (m)", nodeXText);
	nodeFormLayout->addRow("Y (m)", nodeYText);
	nodeFormLayout->addRow("Name", nodeStationNameText);
	nodeFormLayout->addRow("Region", nodeRegionText);
	nodeFormLayout->addRow("Connected tracks", nodeConnectedTracksText);
	nodeFormLayout->addRow("Signalled", nodeSignalledText);
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
	signallingAspectText = new QLineEdit(signallingInfoWidget);
	signallingAspectText->setObjectName("signallingAspectText");
	signallingProtectedSectionText = new QLineEdit(signallingInfoWidget);
	signallingProtectedSectionText->setObjectName("signallingProtectedSectionText");
	signallingNextTrackText = new QLineEdit(signallingInfoWidget);
	signallingNextTrackText->setObjectName("signallingNextTrackText");
	signallingGroupDetails = new QPlainTextEdit(signallingInfoWidget);
	signallingGroupDetails->setObjectName("signallingGroupDetails");
	signallingGroupDetails->setReadOnly(true);
	signallingGroupDetails->setMaximumHeight(120);
	signallingGroupDetails->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	signallingGroupDetails->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
	signallingFormLayout = new QFormLayout();
	signallingFormLayout->addRow("Track ID", signallingTrackIDText);
	signallingFormLayout->addRow("X (m)", signallingXText);
	signallingFormLayout->addRow("ID of Block Section", signallingIDSectionAheadText);
	signallingFormLayout->addRow("Length of Block Section (m)", signallingLengthSectionAheadText);
	signallingFormLayout->addRow("Aspect", signallingAspectText);
	signallingFormLayout->addRow("Protected section", signallingProtectedSectionText);
	signallingFormLayout->addRow("Next track", signallingNextTrackText);
	signallingFormLayout->addRow("Signals at location", signallingGroupDetails);
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
	for (QFormLayout* form : {arcFormLayout, nodeFormLayout, connectionFormLayout,
			 signallingFormLayout, trainFormLayout})
		form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	for (auto* label : infoWidget->findChildren<QLabel*>())
		label->setWordWrap(true);
	for (auto* edit : infoWidget->findChildren<QLineEdit*>()) {
		edit->setReadOnly(true);
		connect(edit, &QLineEdit::textChanged, edit, [edit](const QString& text) {
			edit->setToolTip(text.toHtmlEscaped());
			edit->setCursorPosition(0);
		});
	}

	// dock widget
	infoDockWidget = new InfoDockWidget("Simulation Info");
	auto* infoScroll = new QScrollArea(infoDockWidget);
	infoScroll->setWidgetResizable(true);
	infoScroll->setFrameShape(QFrame::NoFrame);
	infoScroll->setMinimumSize(300, 0);
	infoScroll->setWidget(infoWidget);
	infoDockWidget->setWidget(infoScroll);
	infoDockWidget->setAllowedAreas(Qt::RightDockWidgetArea);
	addDockWidget(Qt::RightDockWidgetArea, infoDockWidget);

	// hide widgets
	infoDockWidget->hide();
	arcInfoWidget->hide();
	nodeInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
}

// help from menu
void MainWindow::handleHelpAbout() {
	if (!m_aboutDialog)
		m_aboutDialog = new AboutDialog(this);
	m_aboutDialog->show();
	m_aboutDialog->raise();
	m_aboutDialog->activateWindow();
}

// hides all widgets from the dock widget
// removes highlight from last clicked item
void MainWindow::handleCloseInfoDockWidget() {
	m_previewSelectedStationId.clear();
	m_presenter->m_inspectedSignal = nullptr;
	m_selectedStationName.clear();
	m_hasSelectedStationIdentity = false;
	m_selectedStationNodeId = 0.0;
	m_selectedStationTrack = -1;
	m_selectedTrainIndex = -1;
	for (auto* overlay : m_presenter->m_stationOverlays)
		if (overlay)
			overlay->setSelected(false);

	// hide all widgets
	arcInfoWidget->hide();
	nodeInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->trainPaxItem = nullptr; // Explicitly dismissed or replaced train inspection.
	m_presenter->removePaxInfoIcon();

	// remove highlight
	if (m_presenter->effect) {
		delete m_presenter->effect;
		m_presenter->effect = nullptr;
	}
	updateViewportOverlays();
}

void MainWindow::clearPreviewInspection() {
	if (m_sceneContextMenu) {
		m_sceneContextMenu->close();
		m_sceneContextMenu.clear();
	}
	if (m_showingTrackPreview) {
		handleCloseInfoDockWidget();
		infoDockWidget->hide();
		for (auto* field : {nodeIDText, nodeXText, nodeYText, nodeTrackIDText,
				 nodeStationNameText, nodeRegionText, nodeConnectedTracksText, nodeSignalledText,
				 arcIDText, arcFirstNodeIDText, arcSecondNodeIDText, arcTrackIDText,
				 arcLengthText, arcCurvatureText, arcGradientText, arcSpeedLimitText,
				 arcOperationalStateText, arcConnectedSignalsText, connectionFirstTrackIDText,
				 connectionSecondTrackIDText, connectionXFirstNodeText, connectionXSecondNodeText,
				 signallingTrackIDText, signallingXText, signallingAspectText,
				 signallingProtectedSectionText, signallingNextTrackText,
				 signallingIDSectionAheadText, signallingLengthSectionAheadText})
			field->clear();
		signallingGroupDetails->clear();
	}
}

QGraphicsItem* MainWindow::resolvePreviewItem(const QString& kind, const QString& id,
	const QString& track, bool reversed, quint64 revision) const {
	if (!m_showingTrackPreview || !m_sceneLoaded || revision != m_sceneRevision || !scene)
		return nullptr;
	for (auto* item : scene->items()) {
		if (item->data(PreviewGraphics::Kind).toString() != kind
			|| item->data(PreviewGraphics::Id).toString() != id)
			continue;
		if (kind == "signal" && (item->data(PreviewGraphics::TrackId).toString() != track || item->data(PreviewGraphics::Reversed).toBool() != reversed))
			continue;
		return item;
	}
	return nullptr;
}

void MainWindow::displayPreviewInfo(QGraphicsItem* item) {
	if (!item || item->scene() != scene || !m_showingTrackPreview || !m_sceneLoaded)
		return;
	const QString kind = item->data(PreviewGraphics::Kind).toString();
	const std::string id = item->data(PreviewGraphics::Id).toString().toStdString();
	const auto text = [](const std::string& value) { return QString::fromStdString(value); };
	const auto number = [](double value) { return QString::number(value, 'g', 12); };
	const QString unavailable = QStringLiteral("Unavailable before Run");
	// Resolve canonical values at interaction; no pointers survive an edit/rebuild.
	clearPreviewInspection();
	QString title;
	if (kind == "node" || kind == "station-node" || kind == "station") {
		const SceneNode* node = nullptr;
		const SceneStation* station = nullptr;
		for (const auto& candidate : m_sceneModel.nodes)
			if (kind != "station" && candidate.id == id)
				node = &candidate;
		const auto memberships = previewStationMemberships(m_sceneModel);
		const auto membership = memberships.find(id);
		const std::string stationId = kind == "station" ? id
			: membership == memberships.end()			? std::string()
														: membership->second.stationId;
		for (const auto& candidate : m_sceneModel.stations)
			if (candidate.id == stationId)
				station = &candidate;
		if (kind == "station") {
			if (!station)
				return;
			for (const auto& anchor : m_presenter->m_cachedTrackPreview.stations)
				if (anchor.id == id)
					for (const auto& candidate : m_sceneModel.nodes)
						if (!anchor.nodeId.empty() && candidate.id == anchor.nodeId)
							node = &candidate;
		} else if (!node) {
			return;
		}
		nodeIDText->setText(node ? text(node->id) : QStringLiteral("No node anchor"));
		nodeXText->setText(node					  ? number(node->xKm * 1000)
				: station && station->hasPosition ? number(station->positionKm * 1000)
												  : unavailable);
		nodeYText->setText(node ? number(node->yKm * 1000) : unavailable);
		nodeTrackIDText->setText(node ? text(node->trackId) : QStringLiteral("No node anchor"));
		nodeStationNameText->setText(station
				? text(station->name.empty() ? station->id : station->name) + " [" + text(station->id) + "]"
				: QString());
		nodeRegionText->setText(unavailable);
		nodeSignalledText->setText(unavailable);
		QStringList connectedTracks;
		if (node) {
			connectedTracks << text(node->trackId);
			for (const auto& connection : m_sceneModel.connections) {
				const std::string other = connection.fromNodeId == node->id ? connection.toNodeId
					: connection.toNodeId == node->id						? connection.fromNodeId
																			: std::string();
				for (const auto& candidate : m_sceneModel.nodes)
					if (candidate.id == other && !connectedTracks.contains(text(candidate.trackId)))
						connectedTracks << text(candidate.trackId);
			}
		}
		nodeConnectedTracksText->setText(connectedTracks.join(", "));
		if (station)
			m_previewSelectedStationId = text(station->id);
		title = kind == "station" ? "Station " + text(id) : "Node " + text(id);
		if (kind != "station" && membership != memberships.end() && !membership->second.platformId.empty())
			title += " / platform " + text(membership->second.platformId);
		nodeInfoWidget->show();
	} else if (kind == "arc" || kind == "track") {
		if (kind == "arc") {
			const auto arc = std::find_if(m_sceneModel.arcs.begin(), m_sceneModel.arcs.end(),
				[&](const SceneArc& value) { return value.id == id; });
			if (arc == m_sceneModel.arcs.end())
				return;
			arcIDText->setText(text(arc->id));
			arcFirstNodeIDText->setText(text(arc->fromNodeId));
			arcSecondNodeIDText->setText(text(arc->toNodeId));
			arcTrackIDText->setText(text(arc->trackId));
			const SceneNode* from = nullptr;
			const SceneNode* to = nullptr;
			for (const auto& node : m_sceneModel.nodes) {
				if (node.id == arc->fromNodeId) from = &node;
				if (node.id == arc->toNodeId) to = &node;
			}
			arcLengthText->setText(from && to
					? number(std::hypot(to->xKm - from->xKm, to->yKm - from->yKm) * 1000)
					: unavailable);
			arcCurvatureText->setText(number(arc->curvatureRadiusM));
			arcGradientText->setText(number(arc->gradientPercent));
			arcSpeedLimitText->setText(number(arc->speedLimitMs));
		} else {
			if (std::none_of(m_sceneModel.tracks.begin(), m_sceneModel.tracks.end(),
					[&](const SceneTrack& track) { return track.id == id; }))
				return;
			arcIDText->setText("No authored arc (fallback)");
			arcTrackIDText->setText(text(id));
		}
		arcOperationalStateText->setText(unavailable);
		arcConnectedSignalsText->setText(unavailable);
		title = kind == "arc" ? "Arc " + text(id) : "Track " + text(id);
		arcInfoWidget->show();
	} else if (kind == "connection") {
		const auto connection = std::find_if(m_sceneModel.connections.begin(), m_sceneModel.connections.end(),
			[&](const SceneConnection& value) { return value.id == id; });
		if (connection == m_sceneModel.connections.end())
			return;
		for (const auto& node : m_sceneModel.nodes) {
			if (node.id == connection->fromNodeId) {
				connectionFirstTrackIDText->setText(text(node.trackId));
				connectionXFirstNodeText->setText(number(node.xKm * 1000));
			}
			if (node.id == connection->toNodeId) {
				connectionSecondTrackIDText->setText(text(node.trackId));
				connectionXSecondNodeText->setText(number(node.xKm * 1000));
			}
		}
		title = "Connection " + text(id);
		connectionInfoWidget->show();
	} else if (kind == "signal") {
		const std::string track = item->data(PreviewGraphics::TrackId).toString().toStdString();
		const auto head = std::find_if(m_presenter->m_cachedTrackPreview.previewSignals.begin(), m_presenter->m_cachedTrackPreview.previewSignals.end(),
			[&](const TrackPreviewSignal& signal) { return signal.trackId == track && signal.sectionId == id; });
		if (head == m_presenter->m_cachedTrackPreview.previewSignals.end())
			return;
		signallingTrackIDText->setText(text(track));
		signallingXText->setText(number(head->rawX * 1000));
		signallingAspectText->setText(unavailable);
		signallingProtectedSectionText->setText(text(id));
		signallingNextTrackText->setText(unavailable);
		signallingIDSectionAheadText->setText(text(id));
		signallingLengthSectionAheadText->setText(unavailable);
		QStringList ids;
		for (const auto& signal : sceneSignals(m_sceneModel))
			if (signal.protectedSection == id)
				ids << text(signal.id);
		signallingGroupDetails->setPlainText(QStringLiteral("Track %1; section %2; %3; authored signal IDs: %4")
				.arg(text(track), text(id), item->data(PreviewGraphics::Reversed).toBool() ? "reverse" : "forward",
					ids.isEmpty() ? "None" : ids.join(", ")));
		title = "Signal " + text(id);
		signallingInfoWidget->show();
	} else {
		return;
	}
	infoDockWidget->setWindowTitle("Preview: " + title + " (not running)");
	infoDockWidget->show();

	// effect on clicked item
	if (!m_presenter->effect) {
		m_presenter->effect = new HighlightEffect(Qt::blue, 1);
	}
	item->setGraphicsEffect(m_presenter->effect);
	updateViewportOverlays();
	// Do not recolor boundary dots/squares or neutral signal heads on inspection.
}

// shows Node info on dock widget
void MainWindow::displayNodeInfo(NodeItem* el) {
	if (!el || !el->node)
		return;
	handleCloseInfoDockWidget();

	// update Node info displayed on widget
	nodeIDText->setText(QString::fromStdString(to_string_precision(el->node->ID, 0)));
	nodeXText->setText(QString::fromStdString(to_string_precision(1000 * el->node->X, 2))); // km to m
	nodeYText->setText(QString::fromStdString(to_string_precision(1000 * el->node->Y, 2))); // km to m
	nodeTrackIDText->setText(QString::fromStdString(to_string_precision(el->track, 0)));
	nodeStationNameText->clear();
	nodeRegionText->clear();
	nodeConnectedTracksText->clear();
	nodeSignalledText->clear();

	// hide other widgets
	arcInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle("Node Info");
	infoDockWidget->show();
	nodeInfoWidget->show();

	// effect on clicked item
	if (!m_presenter->effect) {
		m_presenter->effect = new HighlightEffect(Qt::blue, 1);
	}
	el->setGraphicsEffect(m_presenter->effect);
}

// shows station Node info on dock widget
void MainWindow::displayStationNodeInfo(StationNodeItem* re) {
	if (!re || !re->node)
		return;
	handleCloseInfoDockWidget();
	m_selectedStationName = QString::fromStdString(re->node->stationName);
	m_hasSelectedStationIdentity = true;
	m_selectedStationNodeId = re->node->ID;
	m_selectedStationTrack = re->track;
	for (auto* overlay : m_presenter->m_stationOverlays)
		if (overlay)
			overlay->setSelected(overlay->hasSourceIdentity()
					? overlay->matchesSourceIdentity(m_selectedStationNodeId, m_selectedStationTrack)
					: !m_hasSelectedStationIdentity && overlay->stationName() == m_selectedStationName);
	updateViewportOverlays();

	// update Node info displayed on widget
	nodeIDText->setText(QString::fromStdString(to_string_precision(re->node->ID, 0)));
	nodeXText->setText(QString::fromStdString(to_string_precision(1000 * re->node->X, 2))); // km to m
	nodeYText->setText(QString::fromStdString(to_string_precision(1000 * re->node->Y, 2))); // km to m
	nodeTrackIDText->setText(QString::fromStdString(to_string_precision(re->track, 0)));
	nodeStationNameText->setText(QString::fromStdString(re->node->stationName));
	if (re->track >= 0 && re->track < 268)
		nodeRegionText->setText(QString::number(simulation.run().infra.blockSets[re->track].region));
	else
		nodeRegionText->setText("None");
	QStringList connectedTracks;
	if (re->track >= 0 && re->track < 268)
		connectedTracks << QString::number(re->track);
	const int connectionCount = std::max(0, std::min(re->node->numConnections, 6));
	for (int i = 0; i < connectionCount; ++i) {
		const int connectedTrack = re->node->connectIdBlockSet[i];
		if (connectedTrack >= 0 && connectedTrack < 268
			&& !connectedTracks.contains(QString::number(connectedTrack)))
			connectedTracks << QString::number(connectedTrack);
	}
	nodeConnectedTracksText->setText(connectedTracks.join(", "));
	nodeSignalledText->setText(re->node->isSignalled ? "Yes" : "No");

	// hide other widgets
	arcInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle("Station Node Info");
	infoDockWidget->show();
	nodeInfoWidget->show();

	// effect on clicked item
	if (!m_presenter->effect) {
		m_presenter->effect = new HighlightEffect(Qt::blue, 1);
	}
	re->setGraphicsEffect(m_presenter->effect);
}

// shows Arc info on dock widget
void MainWindow::displayArcInfo(TrackLineItem* line) {
	if (!line || !line->arc)
		return;
	handleCloseInfoDockWidget();

	// update Arc info displayed on widget
	arcIDText->setText(QString::fromStdString(to_string_precision(line->arc->ID, 0)));
	arcFirstNodeIDText->setText(QString::fromStdString(to_string_precision(line->arc->startNode.ID, 0)));
	arcSecondNodeIDText->setText(QString::fromStdString(to_string_precision(line->arc->endNode.ID, 0)));
	arcTrackIDText->setText(QString::fromStdString(to_string_precision(line->track, 0)));
	arcLengthText->setText(QString::fromStdString(to_string_precision(line->arc->length, 2)));
	arcCurvatureText->setText(QString::fromStdString(to_string_precision(line->arc->curvature, 2)));
	arcGradientText->setText(QString::fromStdString(to_string_precision(line->arc->gradient, 3)));
	arcSpeedLimitText->setText(QString::fromStdString(to_string_precision(line->arc->speedLimit, 2)));
	const auto operationalStateName = [](TrackOperationalState state) {
		switch (state) {
			case TrackOperationalState::Prepared:
				return QStringLiteral("Permissive signalling");
			case TrackOperationalState::Occupied:
				return QStringLiteral("Occupied");
			case TrackOperationalState::Blocked:
				return QStringLiteral("Blocked");
			case TrackOperationalState::Free:
			default:
				return QStringLiteral("Free");
		}
	};
	arcOperationalStateText->setText(operationalStateName(line->operationalState()));
	QStringList sectionIds;
	const double arcStart = std::min(line->arc->startNode.X, line->arc->endNode.X);
	const double arcEnd = std::max(line->arc->startNode.X, line->arc->endNode.X);
	const auto appendSection = [&sectionIds](const std::string& sectionId) {
		if (!sectionId.empty() && !sectionIds.contains(QString::fromStdString(sectionId)))
			sectionIds << QString::fromStdString(sectionId);
	};
	const std::string startSection = line->arc->startNode.tdsbId;
	const std::string endSection = line->arc->endNode.tdsbId;
	const auto sectionTouchesArc = [&startSection, &endSection](const std::string& sectionId) {
		return !sectionId.empty() && (sectionId == startSection || sectionId == endSection);
	};
	for (auto* candidate : m_presenter->allSignals) {
		if (!candidate || candidate->trackID != line->track)
			continue;
		const bool positionMatches = candidate->X >= arcStart - 1e-9 && candidate->X <= arcEnd + 1e-9;
		const bool sectionMatches = sectionTouchesArc(candidate->sectionAheadId)
			|| sectionTouchesArc(candidate->sectionBehindId);
		if (!positionMatches && !sectionMatches)
			continue;
		appendSection(candidate->sectionAheadId);
		appendSection(candidate->sectionBehindId);
	}
	arcConnectedSignalsText->setText(sectionIds.isEmpty() ? QStringLiteral("None") : sectionIds.join(", "));

	// hide other widgets
	nodeInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle("Arc Info");
	infoDockWidget->show();
	arcInfoWidget->show();

	// effect on clicked item
	if (!m_presenter->effect) {
		m_presenter->effect = new HighlightEffect(Qt::blue, 1);
	}
	line->setGraphicsEffect(m_presenter->effect);
}

// shows connection info on dock widget
void MainWindow::displayConnectionInfo(ConnectionItem* line) {
	if (!line || !line->connection)
		return;
	handleCloseInfoDockWidget();

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
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle("Connection Info");
	infoDockWidget->show();
	connectionInfoWidget->show();

	// effect on clicked item
	if (!m_presenter->effect) {
		m_presenter->effect = new HighlightEffect(Qt::blue, 1);
	}
	line->setGraphicsEffect(m_presenter->effect);
}

// shows signalling info on dock widget
void MainWindow::displaySignallingInfo(SignalItem* signal) {
	if (!signal)
		return;
	handleCloseInfoDockWidget();
	m_presenter->m_inspectedSignal = signal;

	// update signalling info displayed on widget
	signallingTrackIDText->setText(QString::fromStdString(to_string_precision(signal->trackID, 0)));
	signallingXText->setText(QString::fromStdString(to_string_precision(1000 * signal->X, 2))); // km to m
	signallingAspectText->setText(signalAspectLabel(signal));
	signallingGroupDetails->setPlainText(signal->toolTip());
	const std::string protectedSection = !signal->sectionAheadId.empty()
		? signal->sectionAheadId
		: signal->sectionBehindId;
	const int nextTrack = signal->sectionAheadTrackId >= 0
		? signal->sectionAheadTrackId
		: signal->sectionBehindTrackId;
	signallingProtectedSectionText->setText(protectedSection.empty()
			? QStringLiteral("None")
			: QString::fromStdString(protectedSection));
	signallingNextTrackText->setText(nextTrack < 0 ? QStringLiteral("None") : QString::number(nextTrack));

	// check if section ahead exists
	if (!signal->sectionAheadId.empty()) {
		signallingIDSectionAheadText->setText(QString::fromStdString(signal->sectionAheadId));
		signallingLengthSectionAheadText->setText(QString::fromStdString(to_string_precision(1000 * signal->sectionAheadLength, 2))); // km to m
	} else {
		signallingIDSectionAheadText->setText("None");
		signallingLengthSectionAheadText->setText("None");
	}

	// hide other widgets
	arcInfoWidget->hide();
	connectionInfoWidget->hide();
	nodeInfoWidget->hide();
	trainInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show widget
	infoDockWidget->setWindowTitle("Signalling Info");
	infoDockWidget->show();
	signallingInfoWidget->show();

	// effect on clicked item
	if (!m_presenter->effect) {
		m_presenter->effect = new HighlightEffect(Qt::blue, 1);
	}
	signal->setGraphicsEffect(m_presenter->effect);
	updateSignalCues();
}

void MainWindow::displayTrainDetails(TrainBodyItem* trainItem, bool changeFollowMode) {
	if (!trainItem)
		return;
	// update train info displayed on widget
	TrainItemGroup* groupItem = qgraphicsitem_cast<TrainItemGroup*>(trainItem->parentItem());
	if (!groupItem)
		return;
	handleCloseInfoDockWidget();
	m_selectedTrainIndex = trainItem->index;
	trainIDText->setText(QString::fromStdString(to_string_precision(groupItem->trainId, 0)));
	trainTypeText->setText(QString::fromStdString(groupItem->trainType));
	trainLengthText->setText(QString::fromStdString(to_string_precision(groupItem->trainLength, 0)));
	trainWagonsText->setText(QString::fromStdString(to_string_precision(groupItem->wagonCount, 0)));

	// hide other widgets
	arcInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	nodeInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show pax info only while its layer is enabled
	if (initial_variables.PAX_GUI && m_presenter->m_passengerLayerVisible)
		m_presenter->paintTrainPassengerInfo(groupItem);

	// show widget
	infoDockWidget->setWindowTitle("Train Info");
	infoDockWidget->show();
	trainInfoWidget->show();
	if (changeFollowMode)
		setFollowTrain(trainItem->index);
	updateViewportOverlays();

	// effect on clicked item
	if (!m_presenter->effect) {
		m_presenter->effect = new HighlightEffect(Qt::blue, 1);
	}
	if (trainItem->parentItem()) {
		trainItem->parentItem()->setGraphicsEffect(m_presenter->effect);
	} // effect on entire train
}

// shows train info on dock widget
void MainWindow::displayTrainInfo(TrainBodyItem* trainItem) {
	displayTrainDetails(trainItem, true);
}

// the details of a passenger in the snapshot; only the id is known when the snapshot does not list the passenger
GuiPassengerState MainWindow::passengerState(const std::string& id) const {
	return passengerStateIn(m_snapshot.get(), id);
}

// show text icon on top of passenger icon
void MainWindow::displayPassengerInfo(PassengerItem* paxItem) {
	if (!paxItem)
		return;
	handleCloseInfoDockWidget();
	// hide other widgets
	infoDockWidget->hide();
	arcInfoWidget->hide();
	nodeInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	m_presenter->removeTrainPaxInfoIcon();
	m_presenter->removePaxInfoIcon();

	// show pax info
	m_presenter->paintPassengerInfoIcon(paxItem, passengerState(paxItem->passengerId), m_replayActive);

	// effect on clicked item
	if (!m_presenter->effect) {
		m_presenter->effect = new HighlightEffect(Qt::blue, 1);
	}
	paxItem->setGraphicsEffect(m_presenter->effect);
}

void MainWindow::handleDisableHighlight() {
	handleCloseInfoDockWidget();
	if (infoDockWidget)
		infoDockWidget->hide();
}


// double to string with precision
string MainWindow::to_string_precision(double value, int precision) {
	std::ostringstream out;
	out.precision(precision);
	out << std::fixed << value;
	return out.str();
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
		progressBar->setProgress(timestep, totalTimesteps, m_startOffsetSeconds);
}

void MainWindow::waitForUpdates() {
	QEGTRAIN_PROFILE_SCOPE("gui/snapshot_delivery", "gui", "");
	// A queued notification can outlive its worker or arrive after Stop. Drain
	// the mailbox, but never apply that late publication to the current scene.
	if (!m_worker || m_runtimeStatus != QStringLiteral("Running")
		|| m_worker->isStopRequested()) {
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
				if (!m_snapshot || m_snapshot == m_lastRenderedSnapshot || m_replayActive || !m_worker
					|| m_worker != m_trailingRenderWorker || m_worker->isStopRequested()
					|| m_runtimeStatus != QStringLiteral("Running"))
					return;
				renderSnapshot(false);
				m_lastRenderedSnapshot = m_snapshot;
				m_lastRenderMs = QDateTime::currentMSecsSinceEpoch();
				if (const auto snapshotDrawn = mainWindowTestHooks().snapshotDrawn)
					snapshotDrawn(*this);
			});
		}
		m_trailingRenderWorker = m_worker;
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
	if (m_completedReplay.empty() || m_worker) return;
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
	else if (m_presenter->trainPaxItem && !m_presenter->trainPaxInfoItem && initial_variables.PAX_GUI
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
	const std::shared_ptr<const CompletedRunData> run = m_completedRun;
	const int reference = chooseRouteDiagramReference(this, m_sceneModel, *run, "train paths");
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

	const std::shared_ptr<const CompletedRunData> run = m_completedRun;
	const char* titles[] = {"Speed vs Distance", "Speed vs Time", "Time vs Distance", "Simulated Tractive Effort vs Distance"};
	const QString title = QString("%1 [%2]").arg(titles[mode], completedRunContext(run->provenance));
	QChart* chart = new QChart();
	chart->setTitle(title);

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

	DiagramWindow* win = new DiagramWindow(title, this);
	win->setTelemetryCapture([this] { return captureTelemetryOperation(); });
	win->setChart(chart);
	win->setTrainColors(trainColorsForRun(run->results, m_sceneModel));
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

	const std::shared_ptr<const CompletedRunData> run = m_completedRun;
	auto* window = new TimetableTableWindow(run->timetable, run->settings.startOffsetSeconds,
		csvProviderFrom(run, &timetableCsvFrom), this);
	window->setTelemetryCapture([this] { return captureTelemetryOperation(); });
	window->setTrainColors(trainColorsForRun(run->results, m_sceneModel));
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

	const std::shared_ptr<const CompletedRunData> run = m_completedRun;
	QChart* chart = new QChart();
	const QString title = QString("Arrival delay along journey (minutes) [%1]").arg(completedRunContext(run->provenance));
	chart->setTitle(title);

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

	DiagramWindow* win = new DiagramWindow(title, this);
	win->setTelemetryCapture([this] { return captureTelemetryOperation(); });
	win->setChart(chart);
	win->setTrainColors(trainColorsForRun(run->results, m_sceneModel));
	win->setCsvProvider(csvProviderFrom(run, &timetableCsvFrom), "timetable.csv");
	attachRunProvenance(win, run->provenance);
	connect(win, &DiagramWindow::trainSelected, this, &MainWindow::focusTrainInScene);
	win->setTimeAxisX(false, run->settings.startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

void MainWindow::showTimetableGraph() {
	if (!hasRunResults()) return;
	const std::shared_ptr<const CompletedRunData> run = m_completedRun;
	const int reference = chooseRouteDiagramReference(this, m_sceneModel, *run, "timetable");
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
	if (!stationLabels.empty()) {
		auto* stations = new QCategoryAxis();
		stations->setTitleText("Reference stations");
		stations->setLabelsAngle(-90);
		stations->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
		auto* distanceAxis = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
		stations->setStartValue(distanceAxis->min() - 1);
		for (const auto& station : stationLabels)
			stations->append(QString::fromStdString(station.second)
					+ QString(" (%1 km)").arg(station.first, 0, 'f', 2),
				station.first);
		stations->setRange(distanceAxis->min(), distanceAxis->max());
		chart->addAxis(stations, Qt::AlignTop);
		for (auto* series : chart->series()) series->attachAxis(stations);
	}
	// Include elapsed zero explicitly, even when clock labels start at a nonzero offset.
	DiagramWindow* win = new DiagramWindow(heading, this);
	win->setTelemetryCapture([this] { return captureTelemetryOperation(); });
	win->setChart(chart);
	win->setTrainColors(trainColorsForRun(run->results, m_sceneModel));
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

	const std::shared_ptr<const CompletedRunData> run = m_completedRun;
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

	axisX->setTitleText(QString("Reference %1 X (km); origin %2, travel %3")
			.arg(QString::fromStdString(referencePath.id))
			.arg(origin, 0, 'f', 3)
			.arg(end >= origin ? "right" : "left"));
	const auto axisRange = blockSectionAxisRange(extents, referencePath);
	axisX->setRange(axisRange.first, axisRange.second);

	axisY->setTitleText("Elapsed simulation time (s), downward");
	axisY->setReverse(true);
	axisY->setRange(0, std::max(1.0, run->settings.horizonSeconds()));

	DiagramWindow* win = new DiagramWindow("Blocking time: envelopes and trajectories", this);
	win->setTelemetryCapture([this] { return captureTelemetryOperation(); });
	win->setChart(chart);
	win->setBlockSections(extents, QString::fromStdString(referencePath.id));
	// The envelope colours show the block type, so only the train list takes the service colours.
	win->setTrainListColors(trainColorsForRun(run->results, m_sceneModel));
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

	const std::shared_ptr<const CompletedRunData> run = m_completedRun;
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
	const std::shared_ptr<const CompletedRunData> run = m_completedRun;
	openAreaCapacityWindow(this, *run, [this] { return captureTelemetryOperation(); });
}

void MainWindow::showCompressedBlockingTimeDiagram(const CapacityAnalysisResult& result,
	const QString& sectionLabel, const std::shared_ptr<const CompletedRunData>& run,
	std::vector<BlockingTimeDiagramSegment> segments, double axisMinKm, double axisMaxKm,
	const QString& referenceId, std::vector<BlockSectionExtent> extents,
	std::vector<BlockingTimeDiagramSegment> unmapped) {
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
	axisX->setTitleText(QString("Reference %1 X (km)").arg(referenceId));
	axisX->setRange(axisMinKm, axisMaxKm);

	axisY->setTitleText("Compressed envelope time (s), downward");
	axisY->setReverse(true);
	axisY->setRange(minimumTime, maximumTime);
	DiagramWindow* window = new DiagramWindow("Compressed blocking-time diagram", this);
	window->setTelemetryCapture([this] { return captureTelemetryOperation(); });
	window->setChart(chart);
	window->setBlockSections(extents, referenceId);
	window->setTrainListColors(trainColorsForRun(run->results, m_sceneModel));
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
	content.hasTracks = simulation.run().infra.numTrackLines > 0 || !m_sceneModel.tracks.empty();
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
	content.hasPassengers = !preview && initial_variables.PAX_GUI;
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
							displayStationNodeInfo(current);
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
	return trainIndex == m_selectedTrainIndex
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
		const bool selectedByIdentity = m_hasSelectedStationIdentity && overlay->hasSourceIdentity()
			&& overlay->matchesSourceIdentity(m_selectedStationNodeId, m_selectedStationTrack);
		const bool selectedByLegacyName = !m_hasSelectedStationIdentity && !overlay->hasSourceIdentity()
			&& !m_selectedStationName.isEmpty() && overlay->stationName() == m_selectedStationName;
		overlay->setSelected(m_showingTrackPreview
				? !m_previewSelectedStationId.isEmpty() && overlay->data(PreviewGraphics::Id).toString() == m_previewSelectedStationId
				: selectedByIdentity || selectedByLegacyName);
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
	if (signallingInfoWidget && signallingInfoWidget->isVisible() && signallingGroupDetails
		&& m_presenter->m_inspectedSignal && m_presenter->m_signalDecorations.contains(m_presenter->m_inspectedSignal)) {
		const QString details = m_presenter->m_inspectedSignal->toolTip();
		if (signallingGroupDetails->toPlainText() != details) {
			const int scroll = signallingGroupDetails->verticalScrollBar()->value();
			signallingGroupDetails->setPlainText(details);
			signallingGroupDetails->verticalScrollBar()->setValue(scroll);
		}
		signallingAspectText->setText(signalAspectLabel(m_presenter->m_inspectedSignal));
	}
}

void MainWindow::updateZoomStatus() {
	if (m_zoomStatusLabel && networkView)
		m_zoomStatusLabel->setText(networkView->zoomLabel());
}
