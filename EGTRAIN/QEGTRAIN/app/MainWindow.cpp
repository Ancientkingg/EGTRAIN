#include "app/MainWindow.h"
#include "app/SignalItemRoles.h"
#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
#include "app/TelemetrySmokeSupport.h"
#endif
#include "app/MainWindowSupport.h"
#include "app/MainWindowTestHooks.h"
#include "app/OpenRequestRelay.h"
#include "app/ReplayCoverage.h"
#include "app/SceneOpenRequest.h"
#include "app/SceneSavePolicy.h"
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
#include "widgets/UserPrompts.h"
#include "widgets/AboutDialog.h"
#include "widgets/CaseChooserDialog.h"
#include "widgets/CompactDoubleSpinBox.h"
#include <QScrollBar>
#include "diagrams/DiagramWindow.h"
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
#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/BlockingTimeScope.h"
#include "diagrams/CapacityAnalysis.h"
#include "diagrams/CapacityScope.h"
#include "diagrams/TractionCurve.h"
#include "editors/CaseSettingsPanel.h"
#include "graphics/SignalGeometry.h"
#include "graphics/AnnotationPlacement.h"
#include "graphics/VisualPolish.h"
#include "scene/SceneBundle.h"
#include "scene/SceneCompatibility.h"
#include "scene/SceneMigration.h"
#include "scene/SceneWriter.h"
#include "scene/SceneExporter.h"
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
#include <QFileSystemWatcher>
#include <QSaveFile>
#include <QCryptographicHash>
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

// utilization of GUI
// bool GUI = true; // GUI used by default
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
constexpr qreal kDenseDetailZoom = 3.0;
constexpr int kOverlayMargin = 12;
struct PreviewStationMembership {
	std::string stationId;
	std::string name;
	std::string platformId;
};

std::map<std::string, PreviewStationMembership> previewStationMemberships(const SceneModel& model) {
	std::map<std::string, PreviewStationMembership> result;
	std::set<std::tuple<std::string, std::string, std::string>> assigned;
	for (const auto& station : model.stations) {
		const auto assign = [&](const std::string& nodeId) -> PreviewStationMembership& {
			auto& membership = result[nodeId];
			membership.stationId = station.id;
			membership.name = station.name.empty() ? station.id : station.name;
			return membership;
		};
		if (!station.platforms.empty()) {
			for (const auto& platform : station.platforms)
				for (const auto& nodeId : platform.nodeIds)
					if (assigned.emplace(nodeId, station.id, platform.id).second)
						assign(nodeId).platformId = platform.id;
		} else if (station.hasPosition) {
			for (const auto& node : model.nodes)
				if (std::fabs(node.xKm - station.positionKm) <= 1e-8)
					assign(node.id); // Native position binding retains any earlier platform.
		}
	}
	return result;
}

void tagPreviewItem(QGraphicsItem* item, const QString& kind, const std::string& id) {
	item->setData(PreviewGraphics::Kind, kind);
	item->setData(PreviewGraphics::Id, QString::fromStdString(id));
}

// The pen of a connection, shared by the preview before Run and a run.
QPen connectionPen(int width) {
	QPen pen(Qt::white);
	pen.setWidth(width);
	pen.setCosmetic(true);
	return pen;
}

// The outline of a signal head, a hairline at every zoom, shared by the preview before Run and a run.
QPen signalHeadPen() {
	QPen pen;
	pen.setWidth(0);
	return pen;
}

constexpr const char kPlatformGeometryEditedProperty[] = "platformGeometryEdited";

class ServiceSpeedSpinBox : public CompactDoubleSpinBox {
public:
	using CompactDoubleSpinBox::CompactDoubleSpinBox;
	QString textFromValue(double value) const override {
		return QString::number(value, 'g', 6);
	}
	double valueFromText(const QString& text) const override {
		if (!lineEdit()->isModified() && text == textFromValue(value()))
			return value();
		return CompactDoubleSpinBox::valueFromText(text);
	}
};

QString sourceFileSignature(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return QStringLiteral("<missing>");
	return QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
}

QString absoluteSourcePath(const QString& path) {
	return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool sameTrainPhysical(const SceneTrainPhysical& left, const SceneTrainPhysical& right) {
	return left.mass_of_traction_unit_kg == right.mass_of_traction_unit_kg
		&& left.mass_of_a_wagon_kg == right.mass_of_a_wagon_kg
		&& left.number_of_wagons == right.number_of_wagons
		&& left.max_speed_ms == right.max_speed_ms
		&& left.max_deceleration_ms2 == right.max_deceleration_ms2
		&& left.frontal_area_m2 == right.frontal_area_m2
		&& left.resistance_coefficient == right.resistance_coefficient
		&& left.jerk_ms3 == right.jerk_ms3
		&& left.length_m == right.length_m;
}

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

QString sceneSectionDisplayLabel(const SceneSectionDescriptor& section) {
	if (section.connectionDerived) {
		return QStringLiteral("connection %1 / %2 -> %3")
			.arg(QString::fromStdString(section.sourceConnectionId),
				QString::fromStdString(section.firstBlockId),
				QString::fromStdString(section.secondBlockId));
	}
	return QStringLiteral("base block %1 / track %2")
		.arg(QString::fromStdString(section.sourceBlockId),
			QString::fromStdString(section.firstTrackId));
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
		combo->addItem(sceneSectionDisplayLabel(section), QString::fromStdString(section.id));

	int selected = combo->findData(QString::fromStdString(current));
	if (selected < 0 && !current.empty()) {
		if (const auto* resolved = inventory.resolve(current)) {
			combo->addItem(QStringLiteral("%1 (legacy: %2)")
							   .arg(sceneSectionDisplayLabel(*resolved), QString::fromStdString(current)),
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

void populatePassengerStationCombo(QComboBox* combo, const SceneModel& sceneModel,
	const std::string& current, const std::set<std::string>* allowed = nullptr,
	bool hasSelection = true) {
	if (!combo)
		return;
	combo->clear();
	if (!hasSelection) {
		combo->addItem("Select a journey or leg", QString());
		return;
	}
	for (const auto& station : sceneModel.stations) {
		if (allowed && allowed->find(station.id) == allowed->end())
			continue;
		const QString label = station.name.empty()
			? QString::fromStdString(station.id)
			: QStringLiteral("%1 | %2").arg(QString::fromStdString(station.id),
				  QString::fromStdString(station.name));
		combo->addItem(label, QString::fromStdString(station.id));
	}
	int selected = combo->findData(QString::fromStdString(current));
	if (selected < 0) {
		combo->addItem(current.empty() ? QStringLiteral("Invalid: (empty)")
									   : QStringLiteral("Invalid: %1").arg(QString::fromStdString(current)),
			QString::fromStdString(current));
		selected = combo->count() - 1;
	}
	combo->setCurrentIndex(selected);
}

void populatePassengerServiceCombo(QComboBox* combo, const SceneModel& sceneModel,
	const std::string& current, bool hasSelection = true) {
	if (!combo)
		return;
	combo->clear();
	if (!hasSelection) {
		combo->addItem("Select a leg", QString());
		return;
	}
	for (const auto& service : sceneModel.services)
		combo->addItem(QString::fromStdString(service.id), QString::fromStdString(service.id));
	int selected = combo->findData(QString::fromStdString(current));
	if (selected < 0) {
		combo->addItem(current.empty() ? QStringLiteral("Invalid: (empty)")
									   : QStringLiteral("Invalid: %1").arg(QString::fromStdString(current)),
			QString::fromStdString(current));
		selected = combo->count() - 1;
	}
	combo->setCurrentIndex(selected);
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

bool chooseBlockingTimeScope(QWidget* parent, BlockingTimeScope& scope) {
	QDialog dialog(parent);
	dialog.setWindowTitle("Blocking-time scope");
	auto* body = new QWidget;
	auto* form = new QFormLayout(body);
	auto* routeCombo = new QComboBox(&dialog);
	routeCombo->addItem("All routes / all corridors", -1);
	for (std::size_t routeIndex = 0; routeIndex < train_route.size(); ++routeIndex) {
		const Route& route = train_route[routeIndex];
		const QString corridor = route.corridor.empty()
			? QStringLiteral("no corridor")
			: QString::fromStdString(route.corridor);
		routeCombo->addItem(QString("%1  (%2)").arg(QString::fromStdString(route.ID), corridor),
			static_cast<int>(routeIndex));
	}
	auto* firstBlock = new QComboBox(&dialog);
	auto* lastBlock = new QComboBox(&dialog);
	auto* startTime = new QDoubleSpinBox(&dialog);
	auto* endTime = new QDoubleSpinBox(&dialog);
	const double caseEnd = std::max(0.0, initial_variables.times * timestep);
	for (QDoubleSpinBox* spinBox : {startTime, endTime}) {
		spinBox->setRange(0.0, caseEnd);
		spinBox->setDecimals(3);
		spinBox->setSingleStep(timestep > 0.0 ? timestep : 1.0);
		spinBox->setSuffix(" s");
	}
	startTime->setValue(scope.startTime);
	endTime->setValue(scope.endTime);

	const auto refillBlocks = [routeCombo, firstBlock, lastBlock]() {
		firstBlock->clear();
		lastBlock->clear();
		const int routeIndex = routeCombo->currentData().toInt();
		if (routeIndex < 0 || routeIndex >= static_cast<int>(train_route.size())) {
			firstBlock->addItem("All blocks");
			lastBlock->addItem("All blocks");
			firstBlock->setEnabled(false);
			lastBlock->setEnabled(false);
			return;
		}
		const Route& route = train_route[static_cast<std::size_t>(routeIndex)];
		for (int blockIndex = 0; blockIndex < route.N_Block_Sections; ++blockIndex) {
			const QString blockId = QString::fromStdString(route.sequence_of_block_sections[blockIndex].ID);
			firstBlock->addItem(QString("%1:%2").arg(blockIndex + 1).arg(blockId));
			lastBlock->addItem(QString("%1:%2").arg(blockIndex + 1).arg(blockId));
		}
		const bool hasBlocks = firstBlock->count() > 0;
		firstBlock->setEnabled(hasBlocks);
		lastBlock->setEnabled(hasBlocks);
		if (hasBlocks)
			lastBlock->setCurrentIndex(lastBlock->count() - 1);
	};
	QObject::connect(routeCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
		[refillBlocks](int) { refillBlocks(); });
	refillBlocks();

	form->addRow("Route / corridor:", routeCombo);
	form->addRow("First block:", firstBlock);
	form->addRow("Last block:", lastBlock);
	form->addRow("Start after case base:", startTime);
	form->addRow("End after case base:", endTime);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	DialogLayout::install(dialog, "Blocking-time scope",
		"Choose the route, blocks and elapsed interval to inspect.", body, buttons);
	if (dialog.exec() != QDialog::Accepted)
		return false;

	scope.startTime = std::min(startTime->value(), endTime->value());
	scope.endTime = std::max(startTime->value(), endTime->value());
	if (scope.endTime <= scope.startTime) {
		const double step = timestep > 0.0 ? timestep : 1.0;
		if (scope.startTime + step <= caseEnd)
			scope.endTime = scope.startTime + step;
		else
			scope.startTime = std::max(0.0, scope.endTime - step);
	}
	if (scope.endTime <= scope.startTime)
		return false;
	scope.trainIds.clear();
	scope.blockIds.clear();
	scope.sectionIndices.clear();
	const int routeIndex = routeCombo->currentData().toInt();
	scope.routeIndex = routeIndex >= 0 && routeIndex < static_cast<int>(train_route.size())
		? routeIndex
		: -1;
	if (scope.routeIndex < 0)
		return true;

	const Route& route = train_route[static_cast<std::size_t>(scope.routeIndex)];
	scope.sectionIndices = blockSectionRangeIndices(firstBlock->currentIndex(), lastBlock->currentIndex(), route.N_Block_Sections);
	for (int blockIndex : scope.sectionIndices)
		scope.blockIds.push_back(route.sequence_of_block_sections[blockIndex].ID);
	for (int trainIndex = 0; trainIndex < numRegions; ++trainIndex) {
		const Train& train = regional_train[trainIndex];
		if (train.indexOfRoute == scope.routeIndex || train.TrainRouteID == route.ID)
			scope.trainIds.push_back(train.trainDescription);
	}
	return true;
}

std::string delayComparisonCsv(const DelayRunSnapshot& baseline,
	const DelayRunSnapshot& scenario, const DelayComparisonResult& comparison) {
	std::vector<std::vector<std::string>> rows;
	for (const DelayComparisonRow& row : comparison.rows) {
		rows.push_back({baseline.scenarioId,
			scenario.scenarioId,
			baseline.caseRevision,
			row.serviceId,
			std::to_string(row.occurrence),
			row.operatingCode,
			csvValue(row.baselineFinalArrival),
			csvValue(row.scenarioFinalArrival),
			csvValue(row.positiveContribution),
			row.attribution,
			joinIncidentIds(row.incidentIds),
			csvValue(row.firstDirectTime),
			csvValue(row.firstDirectLocation),
			terminationOutcome(row.destinationTerminationRequested, row.destinationTerminated)});
	}
	return csv::makeDocument(
		{"Baseline scenario", "Scenario", "Case revision", "Service", "Occurrence", "Operating code",
			"Baseline final arrival[s]", "Scenario final arrival[s]", "Positive contribution[s]",
			"Attribution", "Incident IDs", "First direct time[s]", "First direct location[m]", "Termination outcome"},
		rows);
}

std::string capacityEvidenceText(const std::vector<CapacityHeadwayEvidence>& evidence) {
	QStringList values;
	for (const auto& item : evidence)
		values << QString("%1/%2 (leader=%3 s; follower=%4 s; candidate=%5 s)")
					  .arg(QString::fromStdString(item.leaderBlockId), QString::fromStdString(item.followerBlockId),
						  QString::fromStdString(csv::formatDouble(item.leaderOffset)),
						  QString::fromStdString(csv::formatDouble(item.followerOffset)),
						  QString::fromStdString(csv::formatDouble(item.candidateHeadway)));
	return values.join("; ").toStdString();
}

std::string buildCapacityAnalysisCsv(const CapacityAnalysisResult& result, const QString& sectionLabel) {
	if (!result.analyzable || result.cycleEndIdentity.empty() || !std::isfinite(result.cycleTime)
		|| result.cycleTime < 0.0 || !std::isfinite(result.cyclePercentage))
		return std::string();
	const std::vector<std::string> header = {
		"Record type", "Leader", "Follower", "Identity", "Operating code", "Original reference[s]",
		"Scheduled reference[s]", "Compressed reference[s]", "Shift[s]", "Scheduled headway[s]",
		"Minimum headway[s]", "Buffer[s]", "Block", "Leader offset[s]", "Follower offset[s]",
		"Candidate headway[s]", "Gap[s]", "Cycle time[s]", "Period[s]", "Cycle / period [%]",
		"Section", "Reference label", "Reference source", "Notes"};
	constexpr std::size_t kColumnCount = 24;
	std::vector<std::vector<std::string>> rows;
	const auto referenceSource = [&result](const std::string& identity) {
		for (std::size_t index = 0; index < result.trainIdentities.size(); ++index)
			if (result.trainIdentities[index] == identity && index < result.referenceSources.size())
				return result.referenceSources[index];
		return std::string();
	};
	const auto referenceLabel = [&result](const std::string& identity) {
		for (std::size_t index = 0; index < result.trainIdentities.size(); ++index)
			if (result.trainIdentities[index] == identity && index < result.referenceLabels.size())
				return result.referenceLabels[index];
		return std::string();
	};
	std::vector<std::string> summary(kColumnCount);
	summary[0] = "summary";
	summary[1] = result.firstIdentity;
	summary[2] = result.cycleEndIdentity;
	summary[17] = csv::formatDouble(result.cycleTime);
	summary[18] = csv::formatDouble(result.periodSeconds);
	summary[19] = csv::formatDouble(result.cyclePercentage);
	summary[20] = sectionLabel.toStdString();
	summary[21] = referenceLabel(result.firstIdentity) + " -> " + referenceLabel(result.cycleEndIdentity);
	summary[22] = referenceSource(result.firstIdentity) + " -> " + referenceSource(result.cycleEndIdentity);
	summary[23] = "explicit cycle start and next-period closing occurrence";
	rows.push_back(std::move(summary));
	for (const auto& pair : result.allPairs) {
		if (pair.governingEvidence.empty()) {
			std::vector<std::string> record(kColumnCount);
			record[0] = "pair";
			record[1] = pair.leaderIdentity;
			record[2] = pair.followerIdentity;
			record[9] = csv::formatDouble(pair.scheduledHeadway);
			record[10] = csv::formatDouble(pair.minimumHeadway);
			record[11] = csv::formatDouble(pair.buffer);
			record[20] = sectionLabel.toStdString();
			record[21] = referenceLabel(pair.leaderIdentity) + " -> " + referenceLabel(pair.followerIdentity);
			record[22] = referenceSource(pair.leaderIdentity) + " -> " + referenceSource(pair.followerIdentity);
			record[23] = pair.adjacent ? "no shared constraint" : "nonadjacent; no shared constraint";
			rows.push_back(std::move(record));
			continue;
		}
		for (const auto& evidence : pair.governingEvidence) {
			std::vector<std::string> record(kColumnCount);
			record[0] = "pair";
			record[1] = pair.leaderIdentity;
			record[2] = pair.followerIdentity;
			record[9] = csv::formatDouble(pair.scheduledHeadway);
			record[10] = csv::formatDouble(pair.minimumHeadway);
			record[11] = csv::formatDouble(pair.buffer);
			record[12] = evidence.leaderBlockId + " / " + evidence.followerBlockId;
			record[13] = csv::formatDouble(evidence.leaderOffset);
			record[14] = csv::formatDouble(evidence.followerOffset);
			record[15] = csv::formatDouble(evidence.candidateHeadway);
			record[20] = sectionLabel.toStdString();
			record[21] = referenceLabel(pair.leaderIdentity) + " -> " + referenceLabel(pair.followerIdentity);
			record[22] = referenceSource(pair.leaderIdentity) + " -> " + referenceSource(pair.followerIdentity);
			record[23] = pair.adjacent ? "governing resource" : "nonadjacent ordered constraint; governing resource";
			rows.push_back(std::move(record));
		}
	}
	for (const auto& row : result.compression) {
		const auto appendCompressionRecord = [&](const CapacityCompressionEvidence* governing) {
			const std::string predecessor = governing ? governing->predecessorIdentity : std::string();
			std::vector<std::string> record(kColumnCount);
			record[0] = "compression";
			record[1] = predecessor;
			record[3] = row.identity;
			record[4] = row.operatingCode;
			record[5] = csv::formatDouble(row.originalReference);
			record[6] = csv::formatDouble(row.scheduledReference);
			record[7] = csv::formatDouble(row.compressedReference);
			record[8] = csv::formatDouble(row.shift);
			record[12] = governing ? capacityEvidenceText(governing->governingEvidence) : std::string();
			record[20] = sectionLabel.toStdString();
			record[21] = referenceLabel(row.identity);
			record[22] = referenceSource(row.identity);
			record[23] = "governing predecessor=" + predecessor;
			rows.push_back(std::move(record));
		};
		if (row.governingPredecessors.empty())
			appendCompressionRecord(nullptr);
		else
			for (const auto& governing : row.governingPredecessors)
				appendCompressionRecord(&governing);
	}
	for (const auto& critical : result.criticalBlocks) {
		std::vector<std::string> record(kColumnCount);
		record[0] = "critical";
		record[1] = critical.leaderIdentity;
		record[2] = critical.followerIdentity;
		record[12] = critical.leaderBlockId + " / " + critical.followerBlockId;
		record[16] = csv::formatDouble(critical.gap);
		record[20] = sectionLabel.toStdString();
		record[21] = referenceLabel(critical.leaderIdentity) + " -> " + referenceLabel(critical.followerIdentity);
		record[22] = referenceSource(critical.leaderIdentity) + " -> " + referenceSource(critical.followerIdentity);
		record[23] = "capacity critical block";
		rows.push_back(std::move(record));
	}
	return csv::makeDocument(header, rows);
}

bool chooseCapacityAnalysisScope(QWidget* parent, CapacityAnalysisScope& scope) {
	if (train_route.empty())
		return false;
	QDialog dialog(parent);
	dialog.setWindowTitle("Capacity analysis");
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	auto* form = new QFormLayout();
	auto* routeCombo = new QComboBox(&dialog);
	for (std::size_t routeIndex = 0; routeIndex < train_route.size(); ++routeIndex) {
		const Route& route = train_route[routeIndex];
		const QString corridor = route.corridor.empty()
			? QStringLiteral("no corridor")
			: QString::fromStdString(route.corridor);
		routeCombo->addItem(QString("%1  (%2)").arg(QString::fromStdString(route.ID), corridor),
			static_cast<int>(routeIndex));
	}
	auto* firstBlock = new QComboBox(&dialog);
	auto* lastBlock = new QComboBox(&dialog);
	auto* period = new QDoubleSpinBox(&dialog);
	period->setRange(0.001, std::numeric_limits<double>::max());
	period->setDecimals(3);
	period->setSingleStep(60.0);
	period->setSuffix(" s");
	period->setValue(3600.0);
	auto* cycleEnd = new QComboBox(&dialog);
	cycleEnd->addItem("Select the first train in the next period...");
	auto* occurrences = new QListWidget(&dialog);
	occurrences->setSelectionMode(QAbstractItemView::SingleSelection);
	occurrences->setDragDropMode(QAbstractItemView::InternalMove);
	occurrences->setDefaultDropAction(Qt::MoveAction);
	occurrences->setMinimumHeight(180);
	form->addRow("Route / corridor:", routeCombo);
	form->addRow("First block:", firstBlock);
	form->addRow("Last block:", lastBlock);
	form->addRow("Analysis period:", period);
	form->addRow("Cycle-closing occurrence:", cycleEnd);
	layout->addLayout(form);
	auto* note = new QLabel(
		"Checked occurrences are analyzed in the displayed order (A → B); selecting exactly two enables the pair workflow. "
		"Pre-/post-Gdg order changes are separate explicit sections. Select the occurrence of the first service in the next "
		"period that closes the capacity cycle; EGTRAIN does not infer it from the last row.",
		&dialog);
	note->setWordWrap(true);
	layout->addWidget(note);
	layout->addWidget(new QLabel("Occurrences with complete occupations on the selected section:", &dialog));
	layout->addWidget(occurrences);

	const auto refillBlocks = [routeCombo, firstBlock, lastBlock]() {
		firstBlock->clear();
		lastBlock->clear();
		const int routeIndex = routeCombo->currentData().toInt();
		if (routeIndex < 0 || routeIndex >= static_cast<int>(train_route.size()))
			return;
		const Route& route = train_route[static_cast<std::size_t>(routeIndex)];
		for (int blockIndex = 0; blockIndex < route.N_Block_Sections; ++blockIndex) {
			const QString blockId = QString::fromStdString(route.sequence_of_block_sections[blockIndex].ID);
			firstBlock->addItem(QString("%1:%2").arg(blockIndex + 1).arg(blockId));
			lastBlock->addItem(QString("%1:%2").arg(blockIndex + 1).arg(blockId));
		}
		if (firstBlock->count() > 0)
			lastBlock->setCurrentIndex(lastBlock->count() - 1);
	};
	const auto currentScope = [routeCombo, firstBlock, lastBlock, period]() {
		CapacityAnalysisScope candidate;
		candidate.routeIndex = routeCombo->currentData().toInt();
		candidate.periodSeconds = period->value();
		if (candidate.routeIndex < 0 || candidate.routeIndex >= static_cast<int>(train_route.size()))
			return candidate;
		const Route& route = train_route[static_cast<std::size_t>(candidate.routeIndex)];
		candidate.sectionIndices = blockSectionRangeIndices(firstBlock->currentIndex(), lastBlock->currentIndex(), route.N_Block_Sections);
		for (int blockIndex : candidate.sectionIndices)
			candidate.blockIds.push_back(route.sequence_of_block_sections[blockIndex].ID);
		return candidate;
	};
	const auto refillOccurrences = [occurrences, cycleEnd, currentScope]() {
		occurrences->clear();
		cycleEnd->clear();
		cycleEnd->addItem("Select the first train in the next period...");
		const auto trains = capacityTrainsForScope(currentScope());
		for (const CapacityAnalysisTrain& train : trains) {
			const QString identity = QString::fromStdString(train.runtimeId);
			const QString code = QString::fromStdString(train.operatingCode);
			auto* item = new QListWidgetItem(
				QString("%1 | %2 | %3").arg(identity, code, QString::fromStdString(train.referenceSource)), occurrences);
			item->setData(Qt::UserRole, identity);
			item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
			item->setCheckState(Qt::Checked);
			cycleEnd->addItem(QString("%1 | %2").arg(identity, code), identity);
		}
	};
	QObject::connect(routeCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
		[refillBlocks, refillOccurrences](int) { refillBlocks(); refillOccurrences(); });
	QObject::connect(firstBlock, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
		[refillOccurrences](int) { refillOccurrences(); });
	QObject::connect(lastBlock, qOverload<int>(&QComboBox::currentIndexChanged), &dialog,
		[refillOccurrences](int) { refillOccurrences(); });
	if (routeCombo->count() > 0)
		routeCombo->setCurrentIndex(0);
	refillBlocks();
	refillOccurrences();

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
		int selected = 0;
		for (int row = 0; row < occurrences->count(); ++row)
			selected += occurrences->item(row)->checkState() == Qt::Checked ? 1 : 0;
		if (selected < 2) {
			QMessageBox::information(&dialog, "Capacity analysis", "Select at least two ordered occurrences with a common entry block.");
			return;
		}
		scope = currentScope();
		scope.occurrenceIds.clear();
		for (int row = 0; row < occurrences->count(); ++row) {
			const QListWidgetItem* item = occurrences->item(row);
			if (item->checkState() == Qt::Checked)
				scope.occurrenceIds.push_back(item->data(Qt::UserRole).toString().toStdString());
		}
		scope.cycleEndOccurrenceId = cycleEnd->currentData().toString().toStdString();
		const auto selectedCycleEnd = std::find(scope.occurrenceIds.begin(), scope.occurrenceIds.end(),
			scope.cycleEndOccurrenceId);
		if (selectedCycleEnd == scope.occurrenceIds.end() || selectedCycleEnd == scope.occurrenceIds.begin()) {
			QMessageBox::information(&dialog, "Capacity analysis",
				"Choose a checked cycle-closing occurrence after the first row, normally the first train in the next period.");
			return;
		}
		dialog.accept();
	});
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	DialogLayout::install(dialog, "Capacity analysis",
		"Choose a route section and ordered service occurrences.", body, buttons);
	return dialog.exec() == QDialog::Accepted;
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
	QString path = QFileDialog::getSaveFileName(parent, "Export Data", suggestedName, "CSV File (*.csv)");
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

QString stationDisplayName(const SceneModel& model, const std::string& stationId) {
	for (const auto& station : model.stations) {
		if (station.id == stationId)
			return station.name.empty()
				? QString::fromStdString(station.id)
				: QString::fromStdString(station.name);
	}
	return stationId.empty() ? QStringLiteral("(missing station)") : QString::fromStdString(stationId);
}

QString stopResolutionText(SceneStopResolutionStatus status) {
	switch (status) {
		case SceneStopResolutionStatus::Resolved: return "Reachable ordered route visit";
		case SceneStopResolutionStatus::AmbiguousPlatform: return "Choose an explicit reachable platform";
		case SceneStopResolutionStatus::OffRouteContext: return "Off-route schedule context; no simulated stop";
		case SceneStopResolutionStatus::OutOfOrder: return "Invalid: stop is before or at an already used route visit";
		case SceneStopResolutionStatus::InvalidPlatform: return "Invalid: platform is not reachable on this route";
		case SceneStopResolutionStatus::UnknownStation: return "Invalid: station is missing";
		case SceneStopResolutionStatus::UnresolvedRoute: return "Invalid: route traversal is unresolved";
	}
	return {};
}

void addServiceRouteChoice(QComboBox* combo, const SceneModel& model, const SceneRoute& route,
	const SceneSectionInventory& inventory) {
	const SceneRouteStations traversal = sceneRouteStations(model, route, inventory);
	QStringList stations;
	for (const std::string& stationId : traversal.stationIds) {
		QString name = QString::fromStdString(stationId);
		for (const auto& station : model.stations)
			if (station.id == stationId && !station.name.empty()) {
				name = QString::fromStdString(station.name);
				break;
			}
		stations << name;
	}
	const QString description = !traversal.resolved ? QStringLiteral("Unresolved route")
		: stations.isEmpty()						? QStringLiteral("Route without station anchors")
													: QString("%1 → %2").arg(stations.first(), stations.last());
	const QString direction = traversal.direction > 0 ? QStringLiteral("forward")
		: traversal.direction < 0					  ? QStringLiteral("reverse")
													  : QStringLiteral("unknown direction");
	combo->addItem(QString("%1 · %2 [%3]").arg(description, direction, QString::fromStdString(route.id)), QString::fromStdString(route.id));
	combo->setItemData(combo->count() - 1,
		traversal.resolved ? QString("Traversed stations (not scheduled calls): %1")
								 .arg(stations.isEmpty() ? QStringLiteral("none") : stations.join(" → "))
						   : QStringLiteral("Station order unavailable: fix the route topology."),
		Qt::ToolTipRole);
}

bool previewPointAtNode(const TrackPreviewLine& line, const std::string& nodeId, qreal offset, QPointF& point) {
	TrackPreviewPoint candidate;
	if (!trackPreviewPointAtNode(line, nodeId, candidate))
		return false;
	point = QPointF(candidate.x, candidate.y + offset);
	return true;
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

std::vector<std::string> signalFailureTargets(const SceneModel& sceneModel) {
	std::vector<std::string> targets;
	std::set<std::string> seen;
	const auto add = [&targets, &seen](const std::string& target) {
		if (!target.empty() && seen.insert(target).second)
			targets.push_back(target);
	};
	for (const auto& signal : sceneSignals(sceneModel))
		add(signal.id);
	for (const auto& block : sceneModel.blocks)
		add(block.id);
	return targets;
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
										  ui(new Ui::MainWindow),
										  m_worker(nullptr),
										  m_workerThread(nullptr) {
	QFile themeFile(":/styles/egtrain.qss");
	if (themeFile.open(QIODevice::ReadOnly | QIODevice::Text))
		qApp->setStyleSheet(QString::fromUtf8(themeFile.readAll()));

	ui->setupUi(this);
	connect(m_document, &SceneDocument::changed, this, &MainWindow::sceneDocumentChanged);
	m_trainUnitSourceWatcher = new QFileSystemWatcher(this);
	m_trainUnitSourceDebounceTimer = new QTimer(this);
	m_trainUnitSourceDebounceTimer->setSingleShot(true);
	m_trainUnitSourceDebounceTimer->setInterval(150);
	connect(m_trainUnitSourceWatcher, &QFileSystemWatcher::fileChanged, this,
		[this](const QString& path) { scheduleTrainUnitSourceChange(path); });
	connect(m_trainUnitSourceWatcher, &QFileSystemWatcher::directoryChanged, this,
		[this](const QString& path) { scheduleTrainUnitSourceChange(path); });
	connect(m_trainUnitSourceDebounceTimer, &QTimer::timeout, this,
		&MainWindow::processTrainUnitSourceChanges);
	setupUpdateActions();
	if (ui->menuHelp) {
		auto* privacy = ui->menuHelp->addAction(QStringLiteral("Privacy & diagnostics..."));
		privacy->setObjectName(QStringLiteral("actionPrivacyDiagnostics"));
		privacy->setShortcut(QKeySequence::Preferences);
		connect(privacy, &QAction::triggered, this, [this] { showPrivacySettings(false); });
	}
	m_startOffsetSeconds = initial_variables.startingSimulationTime;

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

	// load image for passenger icon
	pax_pixmap = QPixmap(":/icons/pax_icon.png");
	pax_pixmap_scaled = pax_pixmap.scaled(QSize(station_size / 10, station_size / 10), Qt::KeepAspectRatio, Qt::SmoothTransformation);

	// create container of items to show on display area
	scene = new NetworkScene(networkView);
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
			clearOperationalTrackStates();
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

	// effect on (last) clicked item
	effect = nullptr;

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
	m_caseNameLabel = new QLabel(caseLayersWidget);
	m_caseNameLabel->setObjectName("caseNameLabel");
	m_caseNameLabel->setWordWrap(true);
	caseLayersLayout->addWidget(m_caseNameLabel);
	m_caseReadinessLabel = new QLabel(caseLayersWidget);
	m_caseReadinessLabel->setObjectName("caseReadinessLabel");
	m_caseReadinessLabel->setWordWrap(true);
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
	auto* caseLayersScroll = new QScrollArea(m_caseLayersDock);
	caseLayersScroll->setObjectName("caseLayersScroll");
	caseLayersScroll->setWidgetResizable(true);
	caseLayersScroll->setFrameShape(QFrame::NoFrame);
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
		m_stationLayerVisible = checked;
		for (auto* item : m_stationDecorations)
			if (item)
				item->setVisible(checked);
		for (auto* label : m_stationLabels)
			if (label)
				label->setVisible(checked && m_stationNamesVisible);
		// updateViewportOverlays owns station name visibility
		updateViewportOverlays();
	});
	connect(m_stationNamesCheck, &QCheckBox::toggled, this, [this](bool checked) {
		m_stationNamesVisible = checked;
		for (auto* overlay : m_stationOverlays)
			if (overlay)
				overlay->setNameVisible(checked);
		for (auto* label : m_stationLabels)
			if (label)
				label->setVisible(checked && m_stationLayerVisible);
		updateViewportOverlays();
	});
	connect(m_trainLayerCheck, &QCheckBox::toggled, this, [this](bool checked) {
		m_trainLayerVisible = checked;
		for (auto* train : allTrains)
			if (train)
				train->setVisible(checked && (m_replayActive ? replayTrainHasPosition(train->index) : !train->outOfSimulation));
		for (auto it = m_trainBadges.cbegin(); it != m_trainBadges.cend(); ++it) {
			if (!it.value())
				continue;
			auto trainIt = std::find_if(allTrains.cbegin(), allTrains.cend(),
				[it](const TrainItemGroup* train) { return train && train->index == it.key(); });
			it.value()->setVisible(checked && trainIt != allTrains.cend()
				&& (m_replayActive ? replayTrainHasPosition(it.key()) : !(*trainIt)->outOfSimulation));
		}
		updateViewportOverlays();
		cutToFollowedTrain(updateFollowAvailability());
	});
	connect(m_trainSpeedLabelsCheck, &QCheckBox::toggled, this, [this](bool checked) {
		m_trainSpeedLabelsVisible = checked;
		for (auto* badge : m_trainBadges)
			if (badge)
				badge->setSpeedVisible(checked);
	});
	connect(m_signalLayerCheck, &QCheckBox::toggled, this, [this](bool checked) {
		m_signalLayerVisible = checked;
		updateViewportOverlays();
	});
	connect(m_passengerLayerCheck, &QCheckBox::toggled, this, [this](bool checked) {
		m_passengerLayerVisible = checked;
		for (auto* platform : allPlatforms) {
			if (!platform)
				continue;
			if (platform->textIcon)
				platform->textIcon->setVisible(paxTextVisible());
			for (auto* icon : platform->passengerIcons)
				if (icon)
					icon->setVisible(checked);
		}
		if (trainPaxInfoItem)
			trainPaxInfoItem->setVisible(checked);
		if (paxIconInfoItem)
			paxIconInfoItem->setVisible(checked);
		if (checked) {
			updateTrainPaxInfo();
			updatePaxIconInfo();
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
		updateScenarioPresentation();
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
		refreshServiceOccurrencePreview();
		refreshStopList();
	});

	// Infrastructure editor: one small facet selector and a table keep all
	// canonical infrastructure fields visible without introducing a second model.
	m_infrastructureDock = new QDockWidget("Infrastructure", this);
	m_infrastructureDock->setObjectName("infrastructureDock");
	QWidget* infrastructureWidget = new QWidget(m_infrastructureDock);
	QVBoxLayout* infrastructureLayout = new QVBoxLayout(infrastructureWidget);
	QHBoxLayout* infrastructureToolbar = new QHBoxLayout();
	infrastructureToolbar->addWidget(new QLabel("Entity", infrastructureWidget));
	m_infrastructureFacetCombo = new QComboBox(infrastructureWidget);
	m_infrastructureFacetCombo->setObjectName("infrastructureFacetCombo");
	m_infrastructureFacetCombo->setAccessibleName("Infrastructure entity");
	m_infrastructureFacetCombo->addItem("Tracks", "tracks");
	m_infrastructureFacetCombo->addItem("Nodes", "nodes");
	m_infrastructureFacetCombo->addItem("Arcs", "arcs");
	m_infrastructureFacetCombo->addItem("Blocks", "blocks");
	m_infrastructureFacetCombo->addItem("Connections", "connections");
	m_infrastructureFacetCombo->addItem("Stations", "stations");
	m_infrastructureFacetCombo->addItem("Platforms", "platforms");
	m_infrastructureFacetCombo->addItem("Signals", "signals");
	m_infrastructureFacetCombo->addItem("Signalling area", "signalling_areas");
	m_infrastructureFacetCombo->addItem("Routes", "routes");
	m_infrastructureFacetCombo->addItem("Block dependencies", "block_dependencies");
	m_infrastructureFacetCombo->addItem("Single-track restrictions", "single_track_restrictions");
	m_infrastructureFacetCombo->addItem("Station boundaries", "station_boundaries");
	infrastructureToolbar->addWidget(m_infrastructureFacetCombo, 1);
	m_addInfrastructureButton = new QPushButton("Add", infrastructureWidget);
	m_addInfrastructureButton->setObjectName("infrastructureAddButton");
	m_deleteInfrastructureButton = new QPushButton("Delete", infrastructureWidget);
	m_deleteInfrastructureButton->setObjectName("infrastructureDeleteButton");
	infrastructureToolbar->addWidget(m_addInfrastructureButton);
	infrastructureToolbar->addWidget(m_deleteInfrastructureButton);
	infrastructureLayout->addLayout(infrastructureToolbar);
	QHBoxLayout* blockToolbar = new QHBoxLayout();
	m_blockTrackFilterCombo = new QComboBox(infrastructureWidget);
	m_blockTrackFilterCombo->setObjectName("blockTrackFilterCombo");
	m_blockTrackFilterCombo->setAccessibleName("Block track filter");
	m_blockTrackFilterCombo->setToolTip("Show blocks on one track; Add appends to this track");
	blockToolbar->addWidget(m_blockTrackFilterCombo, 1);
	m_insertBlockButton = new QPushButton("Insert", infrastructureWidget);
	m_insertBlockButton->setObjectName("blockInsertButton");
	m_moveBlockUpButton = new QPushButton("Move Up", infrastructureWidget);
	m_moveBlockUpButton->setObjectName("blockMoveUpButton");
	m_moveBlockDownButton = new QPushButton("Move Down", infrastructureWidget);
	m_moveBlockDownButton->setObjectName("blockMoveDownButton");
	blockToolbar->addWidget(m_insertBlockButton);
	blockToolbar->addWidget(m_moveBlockUpButton);
	blockToolbar->addWidget(m_moveBlockDownButton);
	infrastructureLayout->addLayout(blockToolbar);
	auto* infrastructureGuidance = new QLabel(infrastructureWidget);
	infrastructureGuidance->setObjectName("infrastructureGuidance");
	infrastructureGuidance->setWordWrap(true);
	infrastructureGuidance->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	infrastructureLayout->addWidget(infrastructureGuidance);
	m_infrastructureTable = new QTableWidget(infrastructureWidget);
	m_infrastructureTable->setObjectName("infrastructureTable");
	m_infrastructureTable->setAccessibleName("Infrastructure fields");
	m_infrastructureTable->setEditTriggers(QAbstractItemView::AllEditTriggers);
	m_infrastructureTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_infrastructureTable->setSelectionMode(QAbstractItemView::SingleSelection);
	m_infrastructureTable->horizontalHeader()->setStretchLastSection(true);
	infrastructureLayout->addWidget(m_infrastructureTable);
	m_routeSectionDetailWidget = new QWidget(infrastructureWidget);
	m_routeSectionDetailWidget->setObjectName("routeSectionDetailWidget");
	QVBoxLayout* routeSectionDetailLayout = new QVBoxLayout(m_routeSectionDetailWidget);
	QHBoxLayout* routeSectionCatalogLayout = new QHBoxLayout();
	routeSectionCatalogLayout->addWidget(new QLabel("Section catalog", m_routeSectionDetailWidget));
	m_routeSectionCatalogCombo = new QComboBox(m_routeSectionDetailWidget);
	m_routeSectionCatalogCombo->setObjectName("routeSectionCatalogCombo");
	m_routeSectionCatalogCombo->setAccessibleName("Route section catalog");
	routeSectionCatalogLayout->addWidget(m_routeSectionCatalogCombo, 1);
	m_addRouteSectionButton = new QPushButton("Add Section", m_routeSectionDetailWidget);
	m_addRouteSectionButton->setObjectName("routeAddSectionButton");
	routeSectionCatalogLayout->addWidget(m_addRouteSectionButton);
	routeSectionDetailLayout->addLayout(routeSectionCatalogLayout);
	m_routeSectionListWidget = new QListWidget(m_routeSectionDetailWidget);
	m_routeSectionListWidget->setObjectName("routeSectionList");
	m_routeSectionListWidget->setAccessibleName("Ordered route sections");
	routeSectionDetailLayout->addWidget(m_routeSectionListWidget);
	QHBoxLayout* routeSectionActions = new QHBoxLayout();
	m_removeRouteSectionButton = new QPushButton("Remove", m_routeSectionDetailWidget);
	m_removeRouteSectionButton->setObjectName("routeRemoveSectionButton");
	m_moveRouteSectionUpButton = new QPushButton("Move Up", m_routeSectionDetailWidget);
	m_moveRouteSectionUpButton->setObjectName("routeMoveUpButton");
	m_moveRouteSectionDownButton = new QPushButton("Move Down", m_routeSectionDetailWidget);
	m_moveRouteSectionDownButton->setObjectName("routeMoveDownButton");
	routeSectionActions->addWidget(m_removeRouteSectionButton);
	routeSectionActions->addWidget(m_moveRouteSectionUpButton);
	routeSectionActions->addWidget(m_moveRouteSectionDownButton);
	routeSectionActions->addStretch();
	routeSectionDetailLayout->addLayout(routeSectionActions);
	infrastructureLayout->addWidget(m_routeSectionDetailWidget);
	m_infrastructureDock->setWidget(infrastructureWidget);
	addDockWidget(Qt::RightDockWidgetArea, m_infrastructureDock);
	m_infrastructureDock->hide();
	registerEditorDock(m_infrastructureDock);
	connect(m_infrastructureFacetCombo, &QComboBox::currentTextChanged, this,
		[this](const QString&) {
			m_infrastructureSelectionId.clear();
			refreshInfrastructureTable();
		});
	connect(m_blockTrackFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		[this](int) {
			if (m_infrastructureFacetCombo
				&& m_infrastructureFacetCombo->currentData().toString() == QStringLiteral("blocks")) {
				m_infrastructureSelectionId.clear();
				refreshInfrastructureTable();
			}
		});
	connect(m_infrastructureTable, &QTableWidget::itemChanged, this,
		[this](QTableWidgetItem* item) {
			if (item)
				commitInfrastructureCell(item->row(), item->column());
		});
	connect(m_infrastructureTable, &QTableWidget::itemSelectionChanged, this,
		&MainWindow::updateInfrastructureSelection);
	connect(m_infrastructureDock, &QDockWidget::visibilityChanged, this,
		[this](bool visible) {
			if (visible)
				updateInfrastructureSelection();
		});
	connect(m_addInfrastructureButton, &QPushButton::clicked, this,
		&MainWindow::addInfrastructureEntity);
	connect(m_deleteInfrastructureButton, &QPushButton::clicked, this,
		&MainWindow::deleteInfrastructureEntity);
	connect(m_insertBlockButton, &QPushButton::clicked, this, &MainWindow::insertBlock);
	connect(m_moveBlockUpButton, &QPushButton::clicked, this, [this]() { moveBlock(-1); });
	connect(m_moveBlockDownButton, &QPushButton::clicked, this, [this]() { moveBlock(1); });
	connect(m_routeSectionListWidget, &QListWidget::itemSelectionChanged, this,
		[this]() { refreshRouteSectionPanel(); });
	connect(m_addRouteSectionButton, &QPushButton::clicked, this, &MainWindow::addRouteSection);
	connect(m_removeRouteSectionButton, &QPushButton::clicked, this, &MainWindow::removeRouteSection);
	connect(m_moveRouteSectionUpButton, &QPushButton::clicked, this, &MainWindow::moveRouteSectionUp);
	connect(m_moveRouteSectionDownButton, &QPushButton::clicked, this, &MainWindow::moveRouteSectionDown);
	refreshInfrastructurePanel();

	// train-unit editor: one list/detail dock for physical values and traction
	// rows. Numeric widgets keep incomplete or nonnumeric input out of the model.
	m_trainUnitDock = new QDockWidget("Rolling stock units", this);
	m_trainUnitDock->setObjectName("trainUnitDock");
	QWidget* trainUnitWidget = new QWidget(m_trainUnitDock);
	QHBoxLayout* trainUnitLayout = new QHBoxLayout(trainUnitWidget);
	QWidget* trainUnitListPane = new QWidget(trainUnitWidget);
	QVBoxLayout* trainUnitListLayout = new QVBoxLayout(trainUnitListPane);
	trainUnitListLayout->addWidget(new QLabel("Rolling stock units", trainUnitListPane));
	m_trainUnitListWidget = new QListWidget(trainUnitListPane);
	trainUnitListLayout->addWidget(m_trainUnitListWidget);
	QGridLayout* trainUnitButtonLayout = new QGridLayout();
	m_addTrainUnitButton = new QPushButton("Add Unit", trainUnitListPane);
	m_duplicateTrainUnitButton = new QPushButton("Duplicate", trainUnitListPane);
	m_deleteTrainUnitButton = new QPushButton("Delete", trainUnitListPane);
	trainUnitButtonLayout->addWidget(m_addTrainUnitButton, 0, 0);
	trainUnitButtonLayout->addWidget(m_duplicateTrainUnitButton, 0, 1);
	trainUnitButtonLayout->addWidget(m_deleteTrainUnitButton, 1, 0);
	trainUnitListLayout->addLayout(trainUnitButtonLayout);
	trainUnitListPane->setMaximumWidth(trainUnitListPane->fontMetrics().averageCharWidth() * 32);
	trainUnitLayout->addWidget(trainUnitListPane);

	QWidget* trainUnitDetailPane = new QWidget(trainUnitWidget);
	QVBoxLayout* trainUnitDetailLayout = new QVBoxLayout(trainUnitDetailPane);
	auto addUnitHeading = [trainUnitDetailPane, trainUnitDetailLayout](const QString& title) {
		auto* heading = new QLabel(title, trainUnitDetailPane);
		heading->setObjectName("rollingStockSectionHeading");
		QFont font = heading->font();
		font.setBold(true);
		font.setPointSizeF(font.pointSizeF() + 2.0);
		heading->setFont(font);
		heading->setWordWrap(true);
		trainUnitDetailLayout->addWidget(heading);
	};
	addUnitHeading("Rolling stock unit ID");
	m_trainUnitIdEdit = new QLineEdit(trainUnitDetailPane);
	trainUnitDetailLayout->addWidget(m_trainUnitIdEdit);
	addUnitHeading("Rolling stock engine");
	trainUnitDetailLayout->addWidget(new QLabel("Tractive-effort source reference", trainUnitDetailPane));
	m_trainUnitSourceTractionEdit = new QLineEdit(trainUnitDetailPane);
	m_trainUnitSourceTractionEdit->setPlaceholderText("Optional");
	trainUnitDetailLayout->addWidget(m_trainUnitSourceTractionEdit);
	QHBoxLayout* tractionSourceActions = new QHBoxLayout();
	m_linkTrainUnitSourceTractionButton = new QPushButton("Link traction file...", trainUnitDetailPane);
	m_linkTrainUnitSourceTractionButton->setObjectName("linkTrainUnitSourceTractionButton");
	m_unlinkTrainUnitSourceTractionButton = new QPushButton("Unlink", trainUnitDetailPane);
	m_unlinkTrainUnitSourceTractionButton->setObjectName("unlinkTrainUnitSourceTractionButton");
	m_retryTrainUnitSourceTractionButton = new QPushButton("Retry", trainUnitDetailPane);
	m_retryTrainUnitSourceTractionButton->setObjectName("retryTrainUnitSourceTractionButton");
	tractionSourceActions->addWidget(m_linkTrainUnitSourceTractionButton);
	tractionSourceActions->addWidget(m_unlinkTrainUnitSourceTractionButton);
	tractionSourceActions->addWidget(m_retryTrainUnitSourceTractionButton);
	trainUnitDetailLayout->addLayout(tractionSourceActions);
	m_trainUnitSourceTractionStatusLabel = new QLabel("Unlinked", trainUnitDetailPane);
	m_trainUnitSourceTractionStatusLabel->setObjectName("trainUnitSourceTractionStatusLabel");
	m_trainUnitSourceTractionStatusLabel->setWordWrap(true);
	trainUnitDetailLayout->addWidget(m_trainUnitSourceTractionStatusLabel);

	trainUnitDetailLayout->addWidget(new QLabel("Traction curve", trainUnitDetailPane));
	m_trainUnitTractionTable = new QTableWidget(0, 5, trainUnitDetailPane);
	m_trainUnitTractionTable->setHorizontalHeaderLabels(QStringList()
		<< "Lower speed (m/s)" << "Upper speed (m/s)" << "C0" << "C1" << "C2");
	m_trainUnitTractionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_trainUnitTractionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	// size columns to their headers so "Lower speed (m/s)" is never clipped
	m_trainUnitTractionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_trainUnitTractionTable->horizontalHeader()->setStretchLastSection(true);
	m_trainUnitTractionTable->setMinimumHeight(m_trainUnitTractionTable->fontMetrics().lineSpacing() * 8);
	trainUnitDetailLayout->addWidget(m_trainUnitTractionTable, 1);
	QHBoxLayout* tractionButtonLayout = new QHBoxLayout();
	m_addTrainUnitTractionButton = new QPushButton("Add Traction Row", trainUnitDetailPane);
	m_removeTrainUnitTractionButton = new QPushButton("Delete Traction Row", trainUnitDetailPane);
	tractionButtonLayout->addWidget(m_addTrainUnitTractionButton);
	tractionButtonLayout->addWidget(m_removeTrainUnitTractionButton);
	trainUnitDetailLayout->addLayout(tractionButtonLayout);
	m_plotTrainUnitTractionButton = new QPushButton("Plot input traction characteristic", trainUnitDetailPane);
	trainUnitDetailLayout->addWidget(m_plotTrainUnitTractionButton);

	addUnitHeading("Rolling stock unit characteristics");
	QFormLayout* trainPhysicalLayout = new QFormLayout();
	const char* physicalLabels[] = {
		"Traction-unit mass (kg)", "Wagon mass (kg)", "Wagon count",
		"Maximum speed (m/s)", "Maximum deceleration (m/s²)", "Frontal area (m²)",
		"Resistance coefficient", "Jerk (m/s³)", "Length (m)"};
	for (int i = 0; i < 9; ++i) {
		auto* edit = new CompactDoubleSpinBox(trainUnitDetailPane);
		edit->setRange(std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max());
		edit->setDecimals(std::numeric_limits<double>::max_digits10);
		edit->setSingleStep(i == 2 ? 1.0 : 0.1);
		edit->setKeyboardTracking(false);
		m_trainUnitPhysicalEdits[static_cast<size_t>(i)] = edit;
		trainPhysicalLayout->addRow(new QLabel(physicalLabels[i], trainUnitDetailPane), edit);
	}
	trainUnitDetailLayout->addLayout(trainPhysicalLayout);
	trainUnitDetailLayout->addWidget(new QLabel("Parameter source reference", trainUnitDetailPane));
	m_trainUnitSourceDataEdit = new QLineEdit(trainUnitDetailPane);
	m_trainUnitSourceDataEdit->setPlaceholderText("Optional");
	trainUnitDetailLayout->addWidget(m_trainUnitSourceDataEdit);
	QHBoxLayout* dataSourceActions = new QHBoxLayout();
	m_linkTrainUnitSourceDataButton = new QPushButton("Link parameter file...", trainUnitDetailPane);
	m_linkTrainUnitSourceDataButton->setObjectName("linkTrainUnitSourceDataButton");
	m_unlinkTrainUnitSourceDataButton = new QPushButton("Unlink", trainUnitDetailPane);
	m_unlinkTrainUnitSourceDataButton->setObjectName("unlinkTrainUnitSourceDataButton");
	m_retryTrainUnitSourceDataButton = new QPushButton("Retry", trainUnitDetailPane);
	m_retryTrainUnitSourceDataButton->setObjectName("retryTrainUnitSourceDataButton");
	dataSourceActions->addWidget(m_linkTrainUnitSourceDataButton);
	dataSourceActions->addWidget(m_unlinkTrainUnitSourceDataButton);
	dataSourceActions->addWidget(m_retryTrainUnitSourceDataButton);
	trainUnitDetailLayout->addLayout(dataSourceActions);
	m_trainUnitSourceDataStatusLabel = new QLabel("Unlinked", trainUnitDetailPane);
	m_trainUnitSourceDataStatusLabel->setObjectName("trainUnitSourceDataStatusLabel");
	m_trainUnitSourceDataStatusLabel->setWordWrap(true);
	trainUnitDetailLayout->addWidget(m_trainUnitSourceDataStatusLabel);

	QScrollArea* trainUnitDetailScroll = new QScrollArea(trainUnitWidget);
	trainUnitDetailScroll->setWidgetResizable(true);
	trainUnitDetailScroll->setFrameShape(QFrame::NoFrame);
	trainUnitDetailScroll->setWidget(trainUnitDetailPane);
	trainUnitLayout->addWidget(trainUnitDetailScroll, 1);

	m_trainUnitDock->setWidget(trainUnitWidget);
	addDockWidget(Qt::RightDockWidgetArea, m_trainUnitDock);
	registerEditorDock(m_trainUnitDock);
	connect(m_trainUnitListWidget, &QListWidget::currentRowChanged, this, [this](int) {
		updateTrainUnitDetailPanel();
	});
	connect(m_trainUnitIdEdit, &QLineEdit::editingFinished, this, &MainWindow::commitTrainUnitIdEdit);
	connect(m_trainUnitSourceDataEdit, &QLineEdit::editingFinished, this, &MainWindow::commitTrainUnitSources);
	connect(m_trainUnitSourceTractionEdit, &QLineEdit::editingFinished, this, &MainWindow::commitTrainUnitSources);
	connect(m_linkTrainUnitSourceDataButton, &QPushButton::clicked, this, [this]() {
		linkTrainUnitSource(false);
	});
	connect(m_unlinkTrainUnitSourceDataButton, &QPushButton::clicked, this, [this]() {
		unlinkTrainUnitSource(false);
	});
	connect(m_retryTrainUnitSourceDataButton, &QPushButton::clicked, this, [this]() {
		retryTrainUnitSource(false);
	});
	connect(m_linkTrainUnitSourceTractionButton, &QPushButton::clicked, this, [this]() {
		linkTrainUnitSource(true);
	});
	connect(m_unlinkTrainUnitSourceTractionButton, &QPushButton::clicked, this, [this]() {
		unlinkTrainUnitSource(true);
	});
	connect(m_retryTrainUnitSourceTractionButton, &QPushButton::clicked, this, [this]() {
		retryTrainUnitSource(true);
	});
	for (int i = 0; i < 9; ++i) {
		connect(m_trainUnitPhysicalEdits[static_cast<size_t>(i)], QOverload<double>::of(&QDoubleSpinBox::valueChanged),
			this, [this, i](double) { commitTrainUnitPhysical(i); });
	}
	connect(m_addTrainUnitButton, &QPushButton::clicked, this, &MainWindow::addTrainUnit);
	connect(m_duplicateTrainUnitButton, &QPushButton::clicked, this, &MainWindow::duplicateTrainUnit);
	connect(m_deleteTrainUnitButton, &QPushButton::clicked, this, &MainWindow::deleteTrainUnit);
	connect(m_addTrainUnitTractionButton, &QPushButton::clicked, this, &MainWindow::addTrainUnitTractionRow);
	connect(m_removeTrainUnitTractionButton, &QPushButton::clicked, this, &MainWindow::removeTrainUnitTractionRow);
	connect(m_plotTrainUnitTractionButton, &QPushButton::clicked, this, [this]() {
		const int row = m_trainUnitListWidget ? m_trainUnitListWidget->currentRow() : -1;
		if (row >= 0 && row < static_cast<int>(m_sceneModel.trainUnits.size()))
			plotTrainUnitTraction(m_sceneModel.trainUnits[static_cast<std::size_t>(row)]);
	});
	connect(m_trainUnitTractionTable, &QTableWidget::currentCellChanged, this, [this](int, int, int, int) {
		if (m_removeTrainUnitTractionButton)
			m_removeTrainUnitTractionButton->setEnabled(m_trainUnitTractionTable->currentRow() >= 0);
	});

	// composition editor: dockable panel to view and edit train compositions.
	// this is the first editable scene panel, so it sets the pattern later
	// panes follow: a read-only list picks the record, a details area with
	// explicit fields/buttons edits it, no inline table editing.
	m_compositionDock = new QDockWidget("Compositions", this);
	m_compositionDock->setObjectName("compositionDock");
	QWidget* compositionWidget = new QWidget(m_compositionDock);
	QHBoxLayout* compositionLayout = new QHBoxLayout(compositionWidget);

	// left: the list of compositions and the buttons that manage it
	QWidget* compositionListPane = new QWidget(compositionWidget);
	QVBoxLayout* compositionListLayout = new QVBoxLayout(compositionListPane);
	compositionListLayout->addWidget(new QLabel("Compositions", compositionListPane));
	m_compositionListWidget = new QListWidget(compositionListPane);
	compositionListLayout->addWidget(m_compositionListWidget);
	QGridLayout* compositionButtonLayout = new QGridLayout();
	m_addCompositionButton = new QPushButton("Add Composition", compositionListPane);
	m_duplicateCompositionButton = new QPushButton("Duplicate", compositionListPane);
	m_deleteCompositionButton = new QPushButton("Delete", compositionListPane);
	compositionButtonLayout->addWidget(m_addCompositionButton, 0, 0);
	compositionButtonLayout->addWidget(m_duplicateCompositionButton, 0, 1);
	compositionButtonLayout->addWidget(m_deleteCompositionButton, 1, 0);
	compositionListLayout->addLayout(compositionButtonLayout);
	compositionListPane->setMaximumWidth(compositionListPane->fontMetrics().averageCharWidth() * 32);
	compositionLayout->addWidget(compositionListPane);

	// right: the selected composition's id and its ordered unit membership
	QWidget* compositionDetailPane = new QWidget(compositionWidget);
	QVBoxLayout* compositionDetailLayout = new QVBoxLayout(compositionDetailPane);
	compositionDetailLayout->addWidget(new QLabel("Composition Id", compositionDetailPane));
	m_compositionIdEdit = new QLineEdit(compositionDetailPane);
	compositionDetailLayout->addWidget(m_compositionIdEdit);
	compositionDetailLayout->addWidget(new QLabel("Rolling stock units (in order)", compositionDetailPane));
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

	// selected unit: its original sources and the tractive-effort plot
	compositionDetailLayout->addWidget(new QLabel("Original parameter source", compositionDetailPane));
	m_compositionUnitSourceDataLabel = new QLabel(compositionDetailPane);
	m_compositionUnitSourceDataLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_compositionUnitSourceDataLabel->setWordWrap(true);
	compositionDetailLayout->addWidget(m_compositionUnitSourceDataLabel);
	compositionDetailLayout->addWidget(new QLabel("Original tractive-effort source", compositionDetailPane));
	m_compositionUnitSourceTractionLabel = new QLabel(compositionDetailPane);
	m_compositionUnitSourceTractionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_compositionUnitSourceTractionLabel->setWordWrap(true);
	compositionDetailLayout->addWidget(m_compositionUnitSourceTractionLabel);
	m_plotTractionButton = new QPushButton("Plot input traction characteristic", compositionDetailPane);
	compositionDetailLayout->addWidget(m_plotTractionButton);
	m_compositionUnitWarningLabel = new QLabel(compositionDetailPane);
	m_compositionUnitWarningLabel->setWordWrap(true);
	m_compositionUnitWarningLabel->setStyleSheet("color: #b00020;");
	compositionDetailLayout->addWidget(m_compositionUnitWarningLabel);
	connect(m_plotTractionButton, &QPushButton::clicked, this, &MainWindow::plotSelectedCompositionUnitTraction);

	compositionLayout->addWidget(compositionDetailPane);

	m_compositionDock->setWidget(compositionWidget);
	addDockWidget(Qt::RightDockWidgetArea, m_compositionDock);
	m_compositionDock->hide();
	tabifyDockWidget(m_trainUnitDock, m_compositionDock);
	registerEditorDock(m_compositionDock);

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
	m_serviceDock->setObjectName("serviceDock");
	QWidget* serviceWidget = new QWidget(m_serviceDock);
	QHBoxLayout* serviceLayout = new QHBoxLayout(serviceWidget);

	// left: the list of services and the buttons that manage it
	QWidget* serviceListPane = new QWidget(serviceWidget);
	QVBoxLayout* serviceListLayout = new QVBoxLayout(serviceListPane);
	serviceListLayout->addWidget(new QLabel("Services", serviceListPane));
	m_serviceListWidget = new QListWidget(serviceListPane);
	serviceListLayout->addWidget(m_serviceListWidget);
	QGridLayout* serviceButtonLayout = new QGridLayout();
	m_addServiceButton = new QPushButton("Add Service", serviceListPane);
	m_duplicateServiceButton = new QPushButton("Duplicate", serviceListPane);
	m_deleteServiceButton = new QPushButton("Delete", serviceListPane);
	serviceButtonLayout->addWidget(m_addServiceButton, 0, 0);
	serviceButtonLayout->addWidget(m_duplicateServiceButton, 0, 1);
	serviceButtonLayout->addWidget(m_deleteServiceButton, 1, 0);
	serviceListLayout->addLayout(serviceButtonLayout);
	serviceListPane->setMaximumWidth(serviceListPane->fontMetrics().averageCharWidth() * 32);
	serviceLayout->addWidget(serviceListPane);

	// right: the selected service's service-level fields
	QWidget* serviceDetailPane = new QWidget(serviceWidget);
	QVBoxLayout* serviceDetailLayout = new QVBoxLayout(serviceDetailPane);
	serviceDetailLayout->addWidget(new QLabel("Service Id", serviceDetailPane));
	m_serviceIdEdit = new QLineEdit(serviceDetailPane);
	m_serviceIdEdit->setObjectName("serviceIdEdit");
	serviceDetailLayout->addWidget(m_serviceIdEdit);
	serviceDetailLayout->addWidget(new QLabel("Service code (number)", serviceDetailPane));
	m_serviceOperatingCodeEdit = new QLineEdit(serviceDetailPane);
	m_serviceOperatingCodeEdit->setObjectName("serviceOperatingCodeEdit");
	serviceDetailLayout->addWidget(m_serviceOperatingCodeEdit);
	serviceDetailLayout->addWidget(new QLabel("Category", serviceDetailPane));
	m_serviceCategoryCombo = new QComboBox(serviceDetailPane);
	m_serviceCategoryCombo->setObjectName("serviceCategoryCombo");
	serviceDetailLayout->addWidget(m_serviceCategoryCombo);
	serviceDetailLayout->addWidget(new QLabel("Service visualization colour", serviceDetailPane));
	m_serviceColorButton = new ColorChoiceButton(serviceDetailPane);
	serviceDetailLayout->addWidget(m_serviceColorButton);
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

	QHBoxLayout* repeatCountLayout = new QHBoxLayout();
	m_serviceHasRepeatCountCheck = new QCheckBox("Configured total", serviceDetailPane);
	m_serviceHasRepeatCountCheck->setObjectName("serviceHasRepeatCountCheck");
	m_serviceRepeatCountEdit = new QLineEdit(serviceDetailPane);
	m_serviceRepeatCountEdit->setObjectName("serviceRepeatCountEdit");
	m_serviceRepeatCountEdit->setValidator(
		new QIntValidator(1, std::numeric_limits<int>::max(), m_serviceRepeatCountEdit));
	repeatCountLayout->addWidget(m_serviceHasRepeatCountCheck);
	repeatCountLayout->addWidget(m_serviceRepeatCountEdit);
	serviceDetailLayout->addLayout(repeatCountLayout);

	QHBoxLayout* performanceLayout = new QHBoxLayout();
	performanceLayout->addWidget(new QLabel("Running performance (parameter) %", serviceDetailPane));
	m_servicePerformancePercentEdit = new CompactDoubleSpinBox(serviceDetailPane);
	m_servicePerformancePercentEdit->setObjectName("servicePerformancePercentEdit");
	m_servicePerformancePercentEdit->setRange(1.0, 100.0);
	m_servicePerformancePercentEdit->setDecimals(std::numeric_limits<double>::max_digits10);
	m_servicePerformancePercentEdit->setValue(100);
	m_servicePerformancePercentEdit->setKeyboardTracking(false);
	performanceLayout->addWidget(m_servicePerformancePercentEdit);
	serviceDetailLayout->addLayout(performanceLayout);

	QHBoxLayout* maximumSpeedLayout = new QHBoxLayout();
	m_serviceHasMaximumSpeedCheck = new QCheckBox("Maximum speed restriction km/h", serviceDetailPane);
	m_serviceHasMaximumSpeedCheck->setObjectName("serviceHasMaximumSpeedCheck");
	m_serviceMaximumSpeedKmhEdit = new ServiceSpeedSpinBox(serviceDetailPane);
	m_serviceMaximumSpeedKmhEdit->setObjectName("serviceMaximumSpeedKmhEdit");
	m_serviceMaximumSpeedKmhEdit->setRange(0.1, 1000.0);
	m_serviceMaximumSpeedKmhEdit->setDecimals(std::numeric_limits<double>::max_digits10);
	m_serviceMaximumSpeedKmhEdit->setValue(100.0);
	maximumSpeedLayout->addWidget(m_serviceHasMaximumSpeedCheck);
	maximumSpeedLayout->addWidget(m_serviceMaximumSpeedKmhEdit);
	serviceDetailLayout->addLayout(maximumSpeedLayout);

	QHBoxLayout* operatingCodeStepLayout = new QHBoxLayout();
	m_serviceHasOperatingCodeStepCheck = new QCheckBox("Service code (number) step", serviceDetailPane);
	m_serviceHasOperatingCodeStepCheck->setObjectName("serviceHasOperatingCodeStepCheck");
	m_serviceOperatingCodeStepEdit = new QLineEdit(serviceDetailPane);
	m_serviceOperatingCodeStepEdit->setObjectName("serviceOperatingCodeStepEdit");
	m_serviceOperatingCodeStepEdit->setValidator(
		new QIntValidator(-std::numeric_limits<int>::max(), std::numeric_limits<int>::max(),
			m_serviceOperatingCodeStepEdit));
	operatingCodeStepLayout->addWidget(m_serviceHasOperatingCodeStepCheck);
	operatingCodeStepLayout->addWidget(m_serviceOperatingCodeStepEdit);
	serviceDetailLayout->addLayout(operatingCodeStepLayout);

	serviceDetailLayout->addWidget(new QLabel("Timetable stops", serviceDetailPane));
	QHBoxLayout* stopTimeLayout = new QHBoxLayout();
	stopTimeLayout->addWidget(new QLabel("Planned time display", serviceDetailPane));
	m_stopTimeModeCombo = new QComboBox(serviceDetailPane);
	m_stopTimeModeCombo->setObjectName("timetableTimeModeCombo");
	m_stopTimeModeCombo->addItem("Elapsed offsets (s)", false);
	m_stopTimeModeCombo->addItem("Clock time", true);
	stopTimeLayout->addWidget(m_stopTimeModeCombo);
	m_stopTimeBaseLabel = new QLabel(serviceDetailPane);
	m_stopTimeBaseLabel->setObjectName("timetableTimeBaseLabel");
	m_stopTimeBaseLabel->setWordWrap(true);
	serviceDetailLayout->addLayout(stopTimeLayout);
	serviceDetailLayout->addWidget(m_stopTimeBaseLabel);
	m_stopTableWidget = new QTableWidget(serviceDetailPane);
	m_stopTableWidget->setObjectName("timetableStopTable");
	m_stopTableWidget->setColumnCount(5);
	m_stopTableWidget->setHorizontalHeaderLabels({"Timetable stops (station)", "Stop platform", "Minimum dwell (s)",
		"Planned arrival", "Planned departure"});
	m_stopTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_stopTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_stopTableWidget->setSelectionMode(QAbstractItemView::SingleSelection);
	m_stopTableWidget->setAlternatingRowColors(true);
	m_stopTableWidget->setTabKeyNavigation(true);
	m_stopTableWidget->verticalHeader()->setVisible(false);
	m_stopTableWidget->horizontalHeader()->setStretchLastSection(true);
	serviceDetailLayout->addWidget(m_stopTableWidget);
	QGridLayout* stopButtonLayout = new QGridLayout();
	m_addStopButton = new QPushButton("Add Stop", serviceDetailPane);
	m_addStopButton->setObjectName("addTimetableStopButton");
	m_insertStopAfterButton = new QPushButton("Insert After Selected", serviceDetailPane);
	m_insertStopAfterButton->setObjectName("insertTimetableStopAfterButton");
	m_removeStopButton = new QPushButton("Remove Stop", serviceDetailPane);
	m_removeStopButton->setObjectName("removeTimetableStopButton");
	m_moveStopUpButton = new QPushButton("Move Up", serviceDetailPane);
	m_moveStopUpButton->setObjectName("moveTimetableStopUpButton");
	m_moveStopDownButton = new QPushButton("Move Down", serviceDetailPane);
	m_moveStopDownButton->setObjectName("moveTimetableStopDownButton");
	stopButtonLayout->addWidget(m_addStopButton, 0, 0);
	stopButtonLayout->addWidget(m_insertStopAfterButton, 0, 1);
	stopButtonLayout->addWidget(m_removeStopButton, 1, 0);
	stopButtonLayout->addWidget(m_moveStopUpButton, 1, 1);
	stopButtonLayout->addWidget(m_moveStopDownButton, 1, 2);
	serviceDetailLayout->addLayout(stopButtonLayout);

	serviceDetailLayout->addStretch();

	// the service detail pane plus the timetable editor is tall, so let it scroll
	// instead of overflowing the dock and overlapping the tab bar
	QScrollArea* serviceDetailScroll = new QScrollArea(serviceWidget);
	serviceDetailScroll->setWidgetResizable(true);
	serviceDetailScroll->setFrameShape(QFrame::NoFrame);
	serviceDetailScroll->setWidget(serviceDetailPane);
	auto* servicePages = new QTabWidget(serviceWidget);
	servicePages->setObjectName("serviceEditorPages");
	servicePages->setMinimumSize(0, 0);
	servicePages->addTab(serviceDetailScroll, "Service and timetable");

	QWidget* occurrencePane = new QWidget(serviceWidget);
	QVBoxLayout* occurrenceLayout = new QVBoxLayout(occurrencePane);
	occurrenceLayout->addWidget(new QLabel("Run occurrences", occurrencePane));
	m_serviceOccurrenceSelectionLabel = new QLabel(occurrencePane);
	m_serviceOccurrenceSelectionLabel->setObjectName("serviceOccurrenceSelectionLabel");
	occurrenceLayout->addWidget(m_serviceOccurrenceSelectionLabel);
	m_serviceOccurrenceTable = new QTableWidget(occurrencePane);
	m_serviceOccurrenceTable->setObjectName("serviceOccurrenceTable");
	m_serviceOccurrenceTable->setColumnCount(6);
	m_serviceOccurrenceTable->setHorizontalHeaderLabels(
		{"Include", "Service code (number)", "Generated service", "Scheduled entry",
			"Running performance (parameter) %", "Maximum speed restriction (km/h)"});
	m_serviceOccurrenceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_serviceOccurrenceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_serviceOccurrenceTable->setAlternatingRowColors(true);
	m_serviceOccurrenceTable->horizontalHeader()->setStretchLastSection(true);
	occurrenceLayout->addWidget(m_serviceOccurrenceTable, 1);
	QHBoxLayout* occurrenceButtonLayout = new QHBoxLayout();
	m_selectAllOccurrencesButton = new QPushButton("Select all", occurrencePane);
	m_selectAllOccurrencesButton->setObjectName("selectAllOccurrencesButton");
	m_selectNoneOccurrencesButton = new QPushButton("Select none", occurrencePane);
	m_selectNoneOccurrencesButton->setObjectName("selectNoneOccurrencesButton");
	occurrenceButtonLayout->addWidget(m_selectAllOccurrencesButton);
	occurrenceButtonLayout->addWidget(m_selectNoneOccurrencesButton);
	occurrenceLayout->addLayout(occurrenceButtonLayout);
	servicePages->addTab(occurrencePane, "Run occurrences");
	serviceLayout->addWidget(servicePages, 1);

	m_serviceDock->setWidget(serviceWidget);
	addDockWidget(Qt::RightDockWidgetArea, m_serviceDock);
	m_serviceDock->hide();
	tabifyDockWidget(m_compositionDock, m_serviceDock);
	registerEditorDock(m_serviceDock);

	connect(m_serviceListWidget, &QListWidget::currentRowChanged, this, [this](int) {
		updateServiceDetailPanel();
	});
	connect(m_serviceIdEdit, &QLineEdit::editingFinished, this, &MainWindow::commitServiceIdEdit);
	connect(m_serviceOperatingCodeEdit, &QLineEdit::editingFinished, this, &MainWindow::commitServiceOperatingCode);
	connect(m_serviceCategoryCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
		&MainWindow::commitServiceCategory);
	connect(m_serviceColorButton, &ColorChoiceButton::colorChanged, this,
		&MainWindow::commitServiceVisualizationColor);
	connect(m_serviceCompositionCombo, &QComboBox::currentTextChanged, this, &MainWindow::commitServiceComposition);
	connect(m_serviceRouteCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::commitServiceRoute);
	connect(m_serviceHasEntryTimeCheck, &QCheckBox::toggled, this, &MainWindow::commitServiceHasEntryTime);
	connect(m_serviceEntryTimeSecondsEdit, &QLineEdit::editingFinished, this, &MainWindow::commitServiceEntryTimeSeconds);
	connect(m_serviceHasRepeatCheck, &QCheckBox::toggled, this, &MainWindow::commitServiceHasRepeat);
	connect(m_serviceHeadwaySecondsEdit, &QLineEdit::editingFinished, this, &MainWindow::commitServiceHeadwaySeconds);
	connect(m_serviceHasRepeatCountCheck, &QCheckBox::toggled, this, &MainWindow::commitServiceHasRepeatCount);
	connect(m_serviceRepeatCountEdit, &QLineEdit::editingFinished, this, &MainWindow::commitServiceRepeatCount);
	connect(m_servicePerformancePercentEdit, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &MainWindow::commitServicePerformancePercent);
	connect(m_serviceHasMaximumSpeedCheck, &QCheckBox::toggled, this, &MainWindow::commitServiceHasMaximumSpeed);
	connect(m_serviceMaximumSpeedKmhEdit, &QAbstractSpinBox::editingFinished, this, &MainWindow::commitServiceMaximumSpeed);
	connect(m_serviceHasOperatingCodeStepCheck, &QCheckBox::toggled, this, &MainWindow::commitServiceHasOperatingCodeStep);
	connect(m_serviceOperatingCodeStepEdit, &QLineEdit::editingFinished, this, &MainWindow::commitServiceOperatingCodeStep);
	connect(m_addServiceButton, &QPushButton::clicked, this, &MainWindow::addService);
	connect(m_duplicateServiceButton, &QPushButton::clicked, this, &MainWindow::duplicateService);
	connect(m_deleteServiceButton, &QPushButton::clicked, this, &MainWindow::deleteService);
	connect(m_serviceOccurrenceTable, &QTableWidget::itemChanged, this, &MainWindow::updateServiceOccurrenceSelection);
	connect(m_selectAllOccurrencesButton, &QPushButton::clicked, this, &MainWindow::selectAllServiceOccurrences);
	connect(m_selectNoneOccurrencesButton, &QPushButton::clicked, this, &MainWindow::selectNoneServiceOccurrences);

	connect(m_stopTableWidget, &QTableWidget::cellClicked, this,
		[this](int row, int) { editStop(row); });
	for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
		auto* shortcut = new QShortcut(QKeySequence(key), m_stopTableWidget);
		shortcut->setContext(Qt::WidgetShortcut);
		connect(shortcut, &QShortcut::activated, this, [this]() {
			if (m_stopTableWidget->currentRow() >= 0) editStop(m_stopTableWidget->currentRow());
		});
	}
	connect(m_stopTableWidget, &QTableWidget::currentCellChanged, this, &MainWindow::updateStopActions);
	connect(m_addStopButton, &QPushButton::clicked, this, &MainWindow::addStop);
	connect(m_insertStopAfterButton, &QPushButton::clicked, this, &MainWindow::insertStopAfterSelected);
	connect(m_removeStopButton, &QPushButton::clicked, this, &MainWindow::removeStop);
	connect(m_moveStopUpButton, &QPushButton::clicked, this, &MainWindow::moveStopUp);
	connect(m_moveStopDownButton, &QPushButton::clicked, this, &MainWindow::moveStopDown);
	connect(m_stopTimeModeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
		if (index < 0)
			return;
		m_stopClockMode = m_stopTimeModeCombo->itemData(index).toBool();
		refreshStopList();
	});

	// scenario library and incident editor. each
	// incident has a type (signal_failure or train_breakdown) and a target whose
	// valid choices depend on that type, analogous to a stop's platform choices
	// depending on the selected station.
	m_incidentDock = new QDockWidget("Incidents", this);
	m_incidentDock->setObjectName("incidentDock");
	QWidget* incidentWidget = new QWidget(m_incidentDock);
	QVBoxLayout* incidentLayout = new QVBoxLayout(incidentWidget);

	QWidget* scenarioPane = new QWidget(incidentWidget);
	QVBoxLayout* scenarioLayout = new QVBoxLayout(scenarioPane);
	scenarioLayout->addWidget(new QLabel("Scenarios", scenarioPane));
	m_scenarioListWidget = new QListWidget(scenarioPane);
	m_scenarioListWidget->setObjectName("scenarioListWidget");
	m_scenarioListWidget->setAccessibleName("Scenario library");
	m_scenarioListWidget->setTextElideMode(Qt::ElideRight);
	m_scenarioListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scenarioLayout->addWidget(m_scenarioListWidget);
	QGridLayout* scenarioButtonLayout = new QGridLayout();
	m_blankScenarioButton = new QPushButton("Blank", scenarioPane);
	m_blankScenarioButton->setObjectName("blankScenarioButton");
	m_duplicateScenarioButton = new QPushButton("Duplicate", scenarioPane);
	m_duplicateScenarioButton->setObjectName("duplicateScenarioButton");
	m_deleteScenarioButton = new QPushButton("Delete", scenarioPane);
	m_deleteScenarioButton->setObjectName("deleteScenarioButton");
	m_importScenarioButton = new QPushButton("Import JSON...", scenarioPane);
	m_importScenarioButton->setObjectName("importScenarioButton");
	m_exportScenarioButton = new QPushButton("Export JSON...", scenarioPane);
	m_exportScenarioButton->setObjectName("exportScenarioButton");
	const QList<QPushButton*> scenarioButtons{m_blankScenarioButton, m_duplicateScenarioButton,
		m_deleteScenarioButton, m_importScenarioButton, m_exportScenarioButton};
	for (int index = 0; index < scenarioButtons.size(); ++index)
		scenarioButtonLayout->addWidget(scenarioButtons[index], index / 2, index % 2);
	scenarioLayout->addLayout(scenarioButtonLayout);
	QFormLayout* scenarioDetailLayout = new QFormLayout();
	m_scenarioIdEdit = new QLineEdit(scenarioPane);
	m_scenarioIdEdit->setObjectName("scenarioIdEdit");
	m_scenarioNameEdit = new QLineEdit(scenarioPane);
	m_scenarioNameEdit->setObjectName("scenarioNameEdit");
	m_scenarioDescriptionEdit = new QLineEdit(scenarioPane);
	m_scenarioDescriptionEdit->setObjectName("scenarioDescriptionEdit");
	scenarioDetailLayout->addRow("Scenario ID", m_scenarioIdEdit);
	scenarioDetailLayout->addRow("Name", m_scenarioNameEdit);
	scenarioDetailLayout->addRow("Description", m_scenarioDescriptionEdit);
	scenarioLayout->addLayout(scenarioDetailLayout);

	incidentLayout->addWidget(scenarioPane, 0);

	QTabWidget* scenarioEditorTabs = new QTabWidget(incidentWidget);
	scenarioEditorTabs->setObjectName("scenarioEditorTabs");

	// incident list and the buttons that manage it
	QWidget* incidentEditorPane = new QWidget(scenarioEditorTabs);
	QVBoxLayout* incidentEditorLayout = new QVBoxLayout(incidentEditorPane);
	QWidget* incidentListPane = new QWidget(incidentEditorPane);
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

	incidentEditorLayout->addWidget(incidentListPane, 1);

	// selected incident's fields
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
	incidentDetailLayout->addWidget(new QLabel("Recovery end (s)", incidentDetailPane));
	m_incidentEndSecondsEdit = new QLineEdit(incidentDetailPane);
	m_incidentEndSecondsEdit->setObjectName("incidentEndSecondsEdit");
	m_incidentEndSecondsEdit->setValidator(
		new QIntValidator(0, std::numeric_limits<int>::max(), m_incidentEndSecondsEdit));
	incidentDetailLayout->addWidget(m_incidentEndSecondsEdit);
	m_incidentHasEndSecondsCheck = new QCheckBox("Use recovery end", incidentDetailPane);
	m_incidentHasEndSecondsCheck->setObjectName("incidentHasEndSecondsCheck");
	m_incidentHasEndSecondsCheck->setToolTip("When disabled, a reduced-speed breakdown continues until the destination.");
	incidentDetailLayout->addWidget(m_incidentHasEndSecondsCheck);
	m_incidentHasReducedSpeedCheck = new QCheckBox("Reduced speed cap", incidentDetailPane);
	m_incidentHasReducedSpeedCheck->setObjectName("incidentHasReducedSpeedCheck");
	m_incidentReducedSpeedKmhEdit = new CompactDoubleSpinBox(incidentDetailPane);
	m_incidentReducedSpeedKmhEdit->setObjectName("incidentReducedSpeedKmhEdit");
	m_incidentReducedSpeedKmhEdit->setRange(0.01, 1000.0);
	m_incidentReducedSpeedKmhEdit->setDecimals(std::numeric_limits<double>::max_digits10);
	m_incidentReducedSpeedKmhEdit->setSuffix(" km/h");
	incidentDetailLayout->addWidget(m_incidentHasReducedSpeedCheck);
	incidentDetailLayout->addWidget(m_incidentReducedSpeedKmhEdit);
	m_incidentTerminateAtDestinationCheck = new QCheckBox("Terminate at destination", incidentDetailPane);
	m_incidentTerminateAtDestinationCheck->setObjectName("incidentTerminateAtDestinationCheck");
	incidentDetailLayout->addWidget(m_incidentTerminateAtDestinationCheck);
	incidentDetailLayout->addStretch();

	// the detail pane may become taller than the dock, so let it scroll rather
	// than overflowing and overlapping the tab bar
	QScrollArea* incidentDetailScroll = new QScrollArea(incidentEditorPane);
	incidentDetailScroll->setWidgetResizable(true);
	incidentDetailScroll->setFrameShape(QFrame::NoFrame);
	incidentDetailScroll->setWidget(incidentDetailPane);
	incidentEditorLayout->addWidget(incidentDetailScroll, 2);
	scenarioEditorTabs->addTab(incidentEditorPane, "Incidents");

	QWidget* entranceDelayPane = new QWidget(scenarioEditorTabs);
	QVBoxLayout* entranceDelayLayout = new QVBoxLayout(entranceDelayPane);
	entranceDelayLayout->addWidget(new QLabel("Entrance Delays", entranceDelayPane));
	m_entranceDelayListWidget = new QListWidget(entranceDelayPane);
	m_entranceDelayListWidget->setObjectName("entranceDelayListWidget");
	m_entranceDelayListWidget->setAccessibleName("Entrance delay library");
	entranceDelayLayout->addWidget(m_entranceDelayListWidget, 1);
	QHBoxLayout* entranceDelayButtonLayout = new QHBoxLayout();
	m_addEntranceDelayButton = new QPushButton("Add", entranceDelayPane);
	m_addEntranceDelayButton->setObjectName("entranceDelayAddButton");
	m_duplicateEntranceDelayButton = new QPushButton("Duplicate", entranceDelayPane);
	m_duplicateEntranceDelayButton->setObjectName("entranceDelayDuplicateButton");
	m_deleteEntranceDelayButton = new QPushButton("Delete", entranceDelayPane);
	m_deleteEntranceDelayButton->setObjectName("entranceDelayDeleteButton");
	entranceDelayButtonLayout->addWidget(m_addEntranceDelayButton);
	entranceDelayButtonLayout->addWidget(m_duplicateEntranceDelayButton);
	entranceDelayButtonLayout->addWidget(m_deleteEntranceDelayButton);
	entranceDelayLayout->addLayout(entranceDelayButtonLayout);
	QFormLayout* entranceDelayDetailLayout = new QFormLayout();
	m_entranceDelayServiceCombo = new QComboBox(entranceDelayPane);
	m_entranceDelayServiceCombo->setObjectName("entranceDelayServiceCombo");
	entranceDelayDetailLayout->addRow("Service", m_entranceDelayServiceCombo);
	m_entranceDelayOccurrenceEdit = new QSpinBox(entranceDelayPane);
	m_entranceDelayOccurrenceEdit->setObjectName("entranceDelayOccurrenceSpin");
	m_entranceDelayOccurrenceEdit->setRange(1, 1);
	m_entranceDelayOccurrenceEdit->setKeyboardTracking(false);
	entranceDelayDetailLayout->addRow("Occurrence", m_entranceDelayOccurrenceEdit);
	m_entranceDelayOccurrenceContextLabel = new QLabel(entranceDelayPane);
	m_entranceDelayOccurrenceContextLabel->setObjectName("entranceDelayOccurrenceContextLabel");
	m_entranceDelayOccurrenceContextLabel->setWordWrap(true);
	entranceDelayDetailLayout->addRow(QString(), m_entranceDelayOccurrenceContextLabel);
	m_entranceDelayStationCombo = new QComboBox(entranceDelayPane);
	m_entranceDelayStationCombo->setObjectName("entranceDelayStationCombo");
	entranceDelayDetailLayout->addRow("Station", m_entranceDelayStationCombo);
	m_entranceDelaySecondsEdit = new CompactDoubleSpinBox(entranceDelayPane);
	m_entranceDelaySecondsEdit->setObjectName("entranceDelaySecondsEdit");
	m_entranceDelaySecondsEdit->setRange(-std::numeric_limits<double>::max(),
		std::numeric_limits<double>::max());
	m_entranceDelaySecondsEdit->setDecimals(std::numeric_limits<double>::max_digits10);
	m_entranceDelaySecondsEdit->setKeyboardTracking(false);
	entranceDelayDetailLayout->addRow("Delay (s)", m_entranceDelaySecondsEdit);
	entranceDelayLayout->addLayout(entranceDelayDetailLayout);
	scenarioEditorTabs->addTab(entranceDelayPane, "Entrance Delays");
	scenarioEditorTabs->setMinimumSize(0, 0);
	incidentLayout->addWidget(scenarioEditorTabs, 1);

	m_incidentDock->setWidget(incidentWidget);
	addDockWidget(Qt::RightDockWidgetArea, m_incidentDock);
	m_incidentDock->hide();
	m_trainUnitDock->hide();
	tabifyDockWidget(m_serviceDock, m_incidentDock);
	registerEditorDock(m_incidentDock);

	connect(m_incidentListWidget, &QListWidget::currentRowChanged, this, [this](int) {
		updateIncidentDetailPanel();
	});
	connect(m_scenarioListWidget, &QListWidget::currentRowChanged, this, &MainWindow::selectScenario);
	connect(m_scenarioIdEdit, &QLineEdit::editingFinished, this, &MainWindow::commitScenarioIdEdit);
	connect(m_scenarioNameEdit, &QLineEdit::editingFinished, this, &MainWindow::commitScenarioNameEdit);
	connect(m_scenarioDescriptionEdit, &QLineEdit::editingFinished, this, &MainWindow::commitScenarioDescriptionEdit);
	connect(m_blankScenarioButton, &QPushButton::clicked, this, &MainWindow::addBlankScenario);
	connect(m_duplicateScenarioButton, &QPushButton::clicked, this, &MainWindow::duplicateScenario);
	connect(m_deleteScenarioButton, &QPushButton::clicked, this, &MainWindow::deleteScenario);
	connect(m_importScenarioButton, &QPushButton::clicked, this, &MainWindow::importScenario);
	connect(m_exportScenarioButton, &QPushButton::clicked, this, &MainWindow::exportScenario);
	connect(m_incidentIdEdit, &QLineEdit::editingFinished, this, &MainWindow::commitIncidentIdEdit);
	connect(m_incidentTypeCombo, &QComboBox::currentTextChanged, this, &MainWindow::commitIncidentType);
	connect(m_incidentTargetCombo, &QComboBox::currentTextChanged, this, &MainWindow::commitIncidentTarget);
	connect(m_incidentStartSecondsEdit, &QLineEdit::editingFinished, this, &MainWindow::commitIncidentStartSeconds);
	connect(m_incidentEndSecondsEdit, &QLineEdit::editingFinished, this, &MainWindow::commitIncidentEndSeconds);
	connect(m_incidentHasReducedSpeedCheck, &QCheckBox::toggled, this, &MainWindow::commitIncidentHasReducedSpeed);
	connect(m_incidentReducedSpeedKmhEdit, &QAbstractSpinBox::editingFinished, this, &MainWindow::commitIncidentReducedSpeed);
	connect(m_incidentHasEndSecondsCheck, &QCheckBox::toggled, this, &MainWindow::commitIncidentHasEndSeconds);
	connect(m_incidentTerminateAtDestinationCheck, &QCheckBox::toggled, this, &MainWindow::commitIncidentTerminateAtDestination);
	connect(m_addIncidentButton, &QPushButton::clicked, this, &MainWindow::addIncident);
	connect(m_duplicateIncidentButton, &QPushButton::clicked, this, &MainWindow::duplicateIncident);
	connect(m_deleteIncidentButton, &QPushButton::clicked, this, &MainWindow::deleteIncident);
	connect(m_entranceDelayListWidget, &QListWidget::currentRowChanged, this,
		[this](int) { updateEntranceDelayDetailPanel(); });
	connect(m_addEntranceDelayButton, &QPushButton::clicked, this, &MainWindow::addEntranceDelay);
	connect(m_duplicateEntranceDelayButton, &QPushButton::clicked, this, &MainWindow::duplicateEntranceDelay);
	connect(m_deleteEntranceDelayButton, &QPushButton::clicked, this, &MainWindow::deleteEntranceDelay);
	connect(m_entranceDelayServiceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&MainWindow::commitEntranceDelayService);
	connect(m_entranceDelayOccurrenceEdit, &QAbstractSpinBox::editingFinished, this,
		&MainWindow::commitEntranceDelayOccurrence);
	connect(m_entranceDelayStationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&MainWindow::commitEntranceDelayStation);
	connect(m_entranceDelaySecondsEdit, &QAbstractSpinBox::editingFinished, this,
		&MainWindow::commitEntranceDelaySeconds);

	// Passenger authoring: a deliberately direct list/detail editor over the
	// canonical SceneModel.  Journeys and legs remain nested under the selected
	// passenger; no second tree model or transient copy is needed.
	m_passengerDock = new QDockWidget("Passengers", this);
	m_passengerDock->setObjectName("passengerDock");
	QWidget* passengerWidget = new QWidget(m_passengerDock);
	QHBoxLayout* passengerLayout = new QHBoxLayout(passengerWidget);
	QWidget* passengerListPane = new QWidget(passengerWidget);
	QVBoxLayout* passengerListLayout = new QVBoxLayout(passengerListPane);
	passengerListLayout->addWidget(new QLabel("Passengers", passengerListPane));
	m_passengerListWidget = new QListWidget(passengerListPane);
	m_passengerListWidget->setObjectName("passengerListWidget");
	m_passengerListWidget->setAccessibleName("Passengers");
	m_passengerListWidget->setMaximumHeight(fontMetrics().lineSpacing() * 12);
	passengerListLayout->addWidget(m_passengerListWidget, 1);
	QHBoxLayout* passengerButtonLayout = new QHBoxLayout();
	m_addPassengerButton = new QPushButton("Add", passengerListPane);
	m_addPassengerButton->setObjectName("passengerAddButton");
	m_deletePassengerButton = new QPushButton("Delete", passengerListPane);
	m_deletePassengerButton->setObjectName("passengerDeleteButton");
	passengerButtonLayout->addWidget(m_addPassengerButton);
	passengerButtonLayout->addWidget(m_deletePassengerButton);
	passengerListLayout->addLayout(passengerButtonLayout);
	m_importPassengerButton = new QPushButton("Import...", passengerListPane);
	m_importPassengerButton->setObjectName("passengerImportButton");
	m_importPassengerButton->setToolTip("Import the exact legacy DAS and RouteChoice passenger pair; imported records are appended.");
	passengerListLayout->addWidget(m_importPassengerButton);
	passengerListLayout->addStretch();
	passengerListPane->setMaximumWidth(passengerListPane->fontMetrics().averageCharWidth() * 30);
	passengerLayout->addWidget(passengerListPane);

	QWidget* passengerDetailPane = new QWidget(passengerWidget);
	QVBoxLayout* passengerDetailLayout = new QVBoxLayout(passengerDetailPane);
	QFormLayout* passengerForm = new QFormLayout();
	m_passengerIdEdit = new QLineEdit(passengerDetailPane);
	m_passengerIdEdit->setObjectName("passengerIdEdit");
	passengerForm->addRow("Passenger ID", m_passengerIdEdit);
	passengerDetailLayout->addLayout(passengerForm);
	m_passengerDiagnosticLabel = new QLabel(passengerDetailPane);
	m_passengerDiagnosticLabel->setObjectName("passengerDiagnosticLabel");
	m_passengerDiagnosticLabel->setWordWrap(true);
	m_passengerDiagnosticLabel->setStyleSheet("color: #5b6871;");
	passengerDetailLayout->addWidget(m_passengerDiagnosticLabel);

	m_passengerTabs = new QTabWidget(passengerDetailPane);
	m_passengerTabs->setObjectName("passengerEditorTabs");

	QWidget* journeysPane = new QWidget(m_passengerTabs);
	QVBoxLayout* journeysLayout = new QVBoxLayout(journeysPane);
	QWidget* journeyListPane = new QWidget(journeysPane);
	QVBoxLayout* journeyListLayout = new QVBoxLayout(journeyListPane);
	journeyListLayout->addWidget(new QLabel("Journeys", journeyListPane));
	m_passengerJourneyListWidget = new QListWidget(journeyListPane);
	m_passengerJourneyListWidget->setObjectName("passengerJourneyListWidget");
	journeyListLayout->addWidget(m_passengerJourneyListWidget, 1);
	QHBoxLayout* journeyButtons = new QHBoxLayout();
	m_addPassengerJourneyButton = new QPushButton("Add", journeyListPane);
	m_addPassengerJourneyButton->setObjectName("passengerJourneyAddButton");
	m_deletePassengerJourneyButton = new QPushButton("Delete", journeyListPane);
	m_deletePassengerJourneyButton->setObjectName("passengerJourneyDeleteButton");
	journeyButtons->addWidget(m_addPassengerJourneyButton);
	journeyButtons->addWidget(m_deletePassengerJourneyButton);
	journeyListLayout->addLayout(journeyButtons);
	m_passengerJourneyListWidget->setMaximumHeight(fontMetrics().lineSpacing() * 6);
	journeysLayout->addWidget(journeyListPane);

	QWidget* journeyDetailPane = new QWidget(journeysPane);
	QFormLayout* journeyForm = new QFormLayout(journeyDetailPane);
	journeyForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_passengerJourneyIdEdit = new QLineEdit(journeyDetailPane);
	m_passengerJourneyIdEdit->setObjectName("passengerJourneyIdEdit");
	journeyForm->addRow("Journey ID", m_passengerJourneyIdEdit);
	m_passengerJourneyActivityEdit = new QLineEdit(journeyDetailPane);
	m_passengerJourneyActivityEdit->setObjectName("passengerJourneyActivityEdit");
	journeyForm->addRow("Activity", m_passengerJourneyActivityEdit);
	m_passengerJourneyOriginCombo = new QComboBox(journeyDetailPane);
	m_passengerJourneyOriginCombo->setObjectName("passengerJourneyOriginCombo");
	journeyForm->addRow("Origin station", m_passengerJourneyOriginCombo);
	m_passengerJourneyDestinationCombo = new QComboBox(journeyDetailPane);
	m_passengerJourneyDestinationCombo->setObjectName("passengerJourneyDestinationCombo");
	journeyForm->addRow("Destination station", m_passengerJourneyDestinationCombo);
	const std::array<std::pair<const char*, const char*>, 4> passengerWindowFields = {
		std::make_pair("Departure start (s)", "passengerDepartureStartSecondsEdit"),
		std::make_pair("Departure end (s)", "passengerDepartureEndSecondsEdit"),
		std::make_pair("Arrival start (s)", "passengerArrivalStartSecondsEdit"),
		std::make_pair("Arrival end (s)", "passengerArrivalEndSecondsEdit")};
	for (std::size_t index = 0; index < passengerWindowFields.size(); ++index) {
		auto* edit = new CompactDoubleSpinBox(journeyDetailPane);
		edit->setObjectName(passengerWindowFields[index].second);
		edit->setRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
		edit->setDecimals(std::numeric_limits<double>::max_digits10);
		edit->setSingleStep(1.0);
		edit->setKeyboardTracking(false);
		m_passengerJourneyWindowEdits[index] = edit;
		journeyForm->addRow(passengerWindowFields[index].first, edit);
	}
	journeysLayout->addWidget(journeyDetailPane, 1);
	m_passengerTabs->addTab(journeysPane, "Journeys");

	QWidget* legsPane = new QWidget(m_passengerTabs);
	QVBoxLayout* legsLayout = new QVBoxLayout(legsPane);
	QWidget* legListPane = new QWidget(legsPane);
	QVBoxLayout* legListLayout = new QVBoxLayout(legListPane);
	legListLayout->addWidget(new QLabel("Legs", legListPane));
	m_passengerLegListWidget = new QListWidget(legListPane);
	m_passengerLegListWidget->setObjectName("passengerLegListWidget");
	legListLayout->addWidget(m_passengerLegListWidget, 1);
	QGridLayout* legButtons = new QGridLayout();
	m_addPassengerLegButton = new QPushButton("Add", legListPane);
	m_addPassengerLegButton->setObjectName("passengerLegAddButton");
	m_deletePassengerLegButton = new QPushButton("Delete", legListPane);
	m_deletePassengerLegButton->setObjectName("passengerLegDeleteButton");
	m_movePassengerLegUpButton = new QPushButton("Move Up", legListPane);
	m_movePassengerLegUpButton->setObjectName("passengerLegMoveUpButton");
	m_movePassengerLegDownButton = new QPushButton("Move Down", legListPane);
	m_movePassengerLegDownButton->setObjectName("passengerLegMoveDownButton");
	legButtons->addWidget(m_addPassengerLegButton, 0, 0);
	legButtons->addWidget(m_deletePassengerLegButton, 0, 1);
	legButtons->addWidget(m_movePassengerLegUpButton, 1, 0);
	legButtons->addWidget(m_movePassengerLegDownButton, 1, 1);
	legListLayout->addLayout(legButtons);
	m_passengerLegListWidget->setMaximumHeight(fontMetrics().lineSpacing() * 6);
	legsLayout->addWidget(legListPane);

	QWidget* legDetailPane = new QWidget(legsPane);
	QFormLayout* legForm = new QFormLayout(legDetailPane);
	legForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_passengerLegIdEdit = new QLineEdit(legDetailPane);
	m_passengerLegIdEdit->setObjectName("passengerLegIdEdit");
	legForm->addRow("Leg ID", m_passengerLegIdEdit);
	m_passengerLegOriginCombo = new QComboBox(legDetailPane);
	m_passengerLegOriginCombo->setObjectName("passengerLegOriginCombo");
	legForm->addRow("Origin station", m_passengerLegOriginCombo);
	m_passengerLegDestinationCombo = new QComboBox(legDetailPane);
	m_passengerLegDestinationCombo->setObjectName("passengerLegDestinationCombo");
	legForm->addRow("Destination station", m_passengerLegDestinationCombo);
	m_passengerLegServiceCombo = new QComboBox(legDetailPane);
	m_passengerLegServiceCombo->setObjectName("passengerLegServiceCombo");
	legForm->addRow("Service", m_passengerLegServiceCombo);
	m_passengerLegOccurrenceEdit = new QSpinBox(legDetailPane);
	m_passengerLegOccurrenceEdit->setObjectName("passengerLegOccurrenceSpin");
	m_passengerLegOccurrenceEdit->setKeyboardTracking(false);
	legForm->addRow("Occurrence", m_passengerLegOccurrenceEdit);
	legsLayout->addWidget(legDetailPane, 1);
	m_passengerTabs->addTab(legsPane, "Legs");
	passengerDetailLayout->addWidget(m_passengerTabs, 1);

	m_passengerImportResultTable = new QTableWidget(0, 4, passengerDetailPane);
	m_passengerImportResultTable->setObjectName("passengerImportResultTable");
	m_passengerImportResultTable->setHorizontalHeaderLabels({"Source", "Row", "Status", "Detail"});
	m_passengerImportResultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_passengerImportResultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_passengerImportResultTable->setMaximumHeight(130);
	m_passengerImportResultTable->horizontalHeader()->setStretchLastSection(true);
	m_passengerImportResultTable->setToolTip("Transient results from the most recent passenger import; not persisted.");
	passengerDetailLayout->addWidget(m_passengerImportResultTable);
	m_passengerImportResultTable->hide();
	const auto updateImportVisibility = [this]() {
		m_passengerImportResultTable->setVisible(m_passengerImportResultTable->rowCount() > 0);
	};
	connect(m_passengerImportResultTable->model(), &QAbstractItemModel::rowsInserted,
		m_passengerImportResultTable, updateImportVisibility);
	connect(m_passengerImportResultTable->model(), &QAbstractItemModel::rowsRemoved,
		m_passengerImportResultTable, updateImportVisibility);
	passengerLayout->addWidget(passengerDetailPane, 2);
	m_passengerDock->setWidget(passengerWidget);
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

	connect(m_passengerListWidget, &QListWidget::currentRowChanged, this,
		[this](int) { updatePassengerDetailPanel(); });
	connect(m_passengerJourneyListWidget, &QListWidget::currentRowChanged, this,
		[this](int) { updatePassengerJourneyPanel(); });
	connect(m_passengerLegListWidget, &QListWidget::currentRowChanged, this,
		[this](int) { updatePassengerLegPanel(); });
	connect(m_addPassengerButton, &QPushButton::clicked, this, &MainWindow::addPassenger);
	connect(m_deletePassengerButton, &QPushButton::clicked, this, &MainWindow::deletePassenger);
	connect(m_importPassengerButton, &QPushButton::clicked, this, &MainWindow::importPassengers);
	connect(m_addPassengerJourneyButton, &QPushButton::clicked, this, &MainWindow::addPassengerJourney);
	connect(m_deletePassengerJourneyButton, &QPushButton::clicked, this, &MainWindow::deletePassengerJourney);
	connect(m_addPassengerLegButton, &QPushButton::clicked, this, &MainWindow::addPassengerLeg);
	connect(m_deletePassengerLegButton, &QPushButton::clicked, this, &MainWindow::deletePassengerLeg);
	connect(m_movePassengerLegUpButton, &QPushButton::clicked, this, [this]() { movePassengerLeg(-1); });
	connect(m_movePassengerLegDownButton, &QPushButton::clicked, this, [this]() { movePassengerLeg(1); });
	connect(m_passengerIdEdit, &QLineEdit::editingFinished, this, &MainWindow::commitPassengerIdEdit);
	connect(m_passengerJourneyIdEdit, &QLineEdit::editingFinished, this, &MainWindow::commitPassengerJourneyIdEdit);
	connect(m_passengerJourneyActivityEdit, &QLineEdit::editingFinished, this, &MainWindow::commitPassengerJourneyActivity);
	connect(m_passengerJourneyOriginCombo, &QComboBox::currentTextChanged, this,
		[this](const QString& text) { commitPassengerJourneyStation(true, text); });
	connect(m_passengerJourneyDestinationCombo, &QComboBox::currentTextChanged, this,
		[this](const QString& text) { commitPassengerJourneyStation(false, text); });
	for (std::size_t index = 0; index < m_passengerJourneyWindowEdits.size(); ++index) {
		connect(m_passengerJourneyWindowEdits[index], QOverload<double>::of(&QDoubleSpinBox::valueChanged),
			this, [this, index](double) { commitPassengerJourneyWindow(static_cast<int>(index)); });
	}
	connect(m_passengerLegIdEdit, &QLineEdit::editingFinished, this, &MainWindow::commitPassengerLegIdEdit);
	connect(m_passengerLegOriginCombo, &QComboBox::currentTextChanged, this,
		[this](const QString& text) { commitPassengerLegStation(true, text); });
	connect(m_passengerLegDestinationCombo, &QComboBox::currentTextChanged, this,
		[this](const QString& text) { commitPassengerLegStation(false, text); });
	connect(m_passengerLegServiceCombo, &QComboBox::currentTextChanged, this, &MainWindow::commitPassengerLegService);
	connect(m_passengerLegOccurrenceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this,
		[this](int) { commitPassengerLegOccurrence(); });

	// permanent status-bar field for the scene validation summary, so it does
	// not clobber transient load/save messages
	m_validationStatusLabel = new QLabel(this);
	statusBar()->addPermanentWidget(m_validationStatusLabel);
	m_zoomStatusLabel = new QLabel(this);
	m_zoomStatusLabel->setObjectName("zoomStatusLabel");
	m_zoomStatusLabel->setToolTip("Network zoom");
	statusBar()->addPermanentWidget(m_zoomStatusLabel);
	updateZoomStatus();

	// route Qt log messages to std::cout so they flow through ConsoleWidget's streambuf
	qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& msg) {
		std::cout << msg.toStdString() << "\n";
	});

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
	m_diagramsMenu->addAction("Timetable table (planned vs simulated)...", this, &MainWindow::showTimetableTable);
	m_diagramsMenu->addAction("Train delays...", this, &MainWindow::showDelayDiagram);
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

	refreshCompositionPanel();
	refreshTrainUnitPanel();
	refreshServicePanel();
	refreshIncidentPanel();
	refreshInfrastructurePanel();
	refreshPassengerPanel();
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
#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_TELEMETRY_SMOKE")) {
		std::fprintf(stdout, "E2E_TELEMETRY_SHUTDOWN\n");
		std::fflush(stdout);
	}
#endif
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

void MainWindow::newScene() {
	const auto operation = captureTelemetryOperation();
	if (!maybeSaveScene())
		return;
	if (deferUntilRunStopped([this, operation] { createNewScene(operation); }))
		return;
	createNewScene(operation);
}

void MainWindow::createNewScene(const telemetry::OperationObservation& operation) {
	m_excludedSceneOccurrences.clear();
	m_lastRunSelectedOccurrences = 0;
	m_lastRunTotalOccurrences = 0;
	clearTrainUnitSourceLinks();
	teardownGUI();
	simulation.resetState();
	m_sceneDir.clear();
	m_savedSceneSha256.clear();
	m_document->replace(makeNewSceneModel(), true);
	if (m_delayBaseline)
		m_delayBaselineStatus = QStringLiteral(
			"Delay baseline cleared: a new case study was created. Complete a run with no incidents or entrance delays to set a new baseline.");
	else
		m_delayBaselineStatus.clear();
	m_delayBaseline.reset();
	m_sceneIsBundle = false;
	m_sceneBundleVersion.reset();
	m_selectedScenarioId = m_sceneModel.defaultScenarioId;
	m_modifiedScenarioIds.clear();
	m_sceneDiagnostics.clear();
	if (m_passengerImportResultTable)
		m_passengerImportResultTable->setRowCount(0);
	m_startOffsetSeconds = baseTimeToSeconds(m_sceneModel.baseTime);
	invalidateRunResults();
	updateSceneWindowTitle();
	updateCaseLayersPanel();
	updateSceneActions();
	statusBar()->showMessage("New case study created; save it to choose a location");
	refreshCompositionPanel();
	refreshTrainUnitPanel();
	refreshServicePanel();
	refreshIncidentPanel();
	refreshInfrastructurePanel();
	refreshPassengerPanel();
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
			showBlockingError(this, "Cannot Open Scene", compatibilityMessage());
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
		showBlockingError(this, "Cannot Open Scene", message);
		return false;
	}

	clearTrainUnitSourceLinks();
	teardownGUI();
	simulation.resetState();

	m_excludedSceneOccurrences.clear();
	m_lastRunSelectedOccurrences = 0;
	m_lastRunTotalOccurrences = 0;
	m_sceneDir = scenePath;
	m_document->replace(result.scene, false);
	if (m_delayBaseline)
		m_delayBaselineStatus = QStringLiteral(
			"Delay baseline cleared: another case study was opened. Complete a run with no incidents or entrance delays to set a new baseline.");
	else
		m_delayBaselineStatus.clear();
	m_delayBaseline.reset();
	m_sceneIsBundle = sceneIsBundle;
	m_sceneBundleVersion = result.bundleVersion;
	m_savedSceneSha256 = hashSceneInputSnapshot(result.inputSnapshot);
	m_selectedScenarioId = m_sceneModel.defaultScenarioId;
	if (m_selectedScenarioId.empty() && !m_sceneModel.scenarios.empty()) {
		m_selectedScenarioId = m_sceneModel.scenarios.front().id;
		m_sceneModel.defaultScenarioId = m_selectedScenarioId;
	}
	m_modifiedScenarioIds.clear();
	m_sceneDiagnostics.clear();
	if (m_passengerImportResultTable)
		m_passengerImportResultTable->setRowCount(0);
	invalidateRunResults();
	m_startOffsetSeconds = baseTimeToSeconds(m_sceneModel.baseTime);
	updateSceneWindowTitle();
	updateCaseLayersPanel();
	updateSceneActions();
	addRecentScene(scenePath);
	refreshCompositionPanel();
	refreshTrainUnitPanel();
	refreshServicePanel();
	refreshIncidentPanel();
	refreshInfrastructurePanel(!reloadingSameScene);
	refreshPassengerPanel();
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

const TrackPreviewLine* MainWindow::cachedTrackLine(int track) const {
	if (track < 0 || track >= numTrackLines || blockSets[track].sceneTrackId.empty())
		return nullptr;
	const auto line = std::find_if(m_cachedTrackPreview.lines.begin(),
		m_cachedTrackPreview.lines.end(), [track](const TrackPreviewLine& candidate) {
			return candidate.id == blockSets[track].sceneTrackId;
		});
	return line == m_cachedTrackPreview.lines.end() ? nullptr : &*line;
}

qreal MainWindow::presentationScale() const {
	return m_cachedTrackPreview.presentationScale;
}

void MainWindow::renderTrackPreview(const SceneModel& sceneModel) {
	clearPreviewInspection();
	m_previewFitBounds = QRectF();
	m_previewHasSelectedTrack = false;
	m_previewHasSignals = false;
	const QString selectedInfrastructureId = m_infrastructureSelectionId;
	const bool hasRuntimeGraphics = static_cast<bool>(m_snapshot) || !allTrains.isEmpty() || !allArcs.isEmpty() || !allSignals.isEmpty() || !allPlatforms.isEmpty() || !m_stationOverlays.isEmpty() || !m_stationDecorations.isEmpty() || !m_signalDecorations.isEmpty() || !m_vcMessageItems.isEmpty() || !m_trainAnimations.isEmpty() || !m_tracksBySectionId.empty() || !m_tracksByOccupiedArc.empty() || !m_activeTrackItems.empty();
	if (hasRuntimeGraphics) {
		// A dirty edit invalidates run results; clear the runtime-owned item
		// observers before rebuilding the authoring preview so no stale pointers
		// remain after scene->clear().
		teardownGUI();
		m_infrastructureSelectionId = selectedInfrastructureId;
	} else if (scene) {
		m_inspectedSignal = nullptr;
		scene->clear();
	}
	m_showingTrackPreview = true;
	effect = nullptr;
	const QString selectedFacet = m_infrastructureFacetCombo
		? m_infrastructureFacetCombo->currentData().toString()
		: QString();
	const std::string selectedId = m_infrastructureSelectionId.toStdString();
	std::set<std::string> selectedTrackIds;
	const auto selectTrack = [&selectedTrackIds](const std::string& id) {
		if (!id.empty())
			selectedTrackIds.insert(id);
	};
	const auto selectNode = [&selectTrack, &sceneModel](const std::string& id) {
		if (id.empty())
			return;
		for (const auto& node : sceneModel.nodes)
			if (node.id == id)
				selectTrack(node.trackId);
	};
	if (selectedFacet == "tracks") {
		selectTrack(selectedId);
	} else if (selectedFacet == "nodes") {
		selectNode(selectedId);
	} else if (selectedFacet == "arcs") {
		for (const auto& arc : sceneModel.arcs)
			if (arc.id == selectedId)
				selectTrack(arc.trackId);
	} else if (selectedFacet == "blocks") {
		for (const auto& block : sceneModel.blocks)
			if (block.id == selectedId)
				selectTrack(block.trackId);
	} else if (selectedFacet == "connections") {
		for (const auto& connection : sceneModel.connections) {
			if (connection.id != selectedId)
				continue;
			for (const auto& node : sceneModel.nodes) {
				if (node.id == connection.fromNodeId || node.id == connection.toNodeId)
					selectTrack(node.trackId);
			}
		}
	} else if (selectedFacet == "stations") {
		for (const auto& station : sceneModel.stations) {
			if (station.id != selectedId)
				continue;
			for (const auto& platform : station.platforms)
				for (const auto& nodeId : platform.nodeIds)
					selectNode(nodeId);
		}
	} else if (selectedFacet == "platforms") {
		const std::size_t separator = selectedId.find('\n');
		if (separator != std::string::npos) {
			const std::string stationId = selectedId.substr(0, separator);
			const std::string platformId = selectedId.substr(separator + 1);
			for (const auto& station : sceneModel.stations) {
				if (station.id != stationId)
					continue;
				for (const auto& platform : station.platforms)
					if (platform.id == platformId)
						for (const auto& nodeId : platform.nodeIds)
							selectNode(nodeId);
			}
		}
	} else if (selectedFacet == "routes") {
		for (const auto& route : sceneModel.routes) {
			if (route.id != selectedId)
				continue;
			for (const auto& token : route.blocks) {
				for (const auto& component : sceneBlockReferenceComponents(token)) {
					for (const auto& block : sceneModel.blocks)
						if (block.id == component)
							selectTrack(block.trackId);
				}
			}
		}
	}
	m_previewHasSelectedTrack = !selectedTrackIds.empty();
	m_cachedTrackPreview = normalizeTrackPreview(loadTrackPreview(sceneModel));
	const TrackPreviewResult& preview = m_cachedTrackPreview;
	if (preview.lines.empty()) {
		if (scene)
			scene->setSceneRect(QRectF());
		statusBar()->showMessage("Scene loaded - no track preview available; simulation not running");
		return;
	}

	QRectF previewBounds;
	bool hasPreviewBounds = false;
	const auto includePreviewPoint = [&previewBounds, &hasPreviewBounds](const QPointF& point) {
		if (!hasPreviewBounds) {
			previewBounds = QRectF(point, point);
			hasPreviewBounds = true;
			return;
		}
		previewBounds.setLeft(qMin(previewBounds.left(), point.x()));
		previewBounds.setRight(qMax(previewBounds.right(), point.x()));
		previewBounds.setTop(qMin(previewBounds.top(), point.y()));
		previewBounds.setBottom(qMax(previewBounds.bottom(), point.y()));
	};
	std::map<std::string, std::pair<const TrackPreviewLine*, qreal>> tracks;
	std::vector<const TrackPreviewLine*> drawnLines;
	for (std::size_t index = 0; index < preview.lines.size(); ++index) {
		const auto& line = preview.lines[index];
		if (line.points.size() < 2 || std::any_of(line.points.begin(), line.points.end(), [](const auto& point) {
				return !std::isfinite(point.x) || !std::isfinite(point.y);
			}))
			continue;
		const qreal offset = static_cast<qreal>(line.displayOffset);
		tracks[line.id] = {&line, offset};
		drawnLines.push_back(&line);
		for (const auto& point : line.points)
			includePreviewPoint(QPointF(point.x, point.y + offset));
	}

	for (const auto& connection : preview.connections) {
		const auto first = tracks.find(connection.firstTrackId);
		const auto second = tracks.find(connection.secondTrackId);
		if (first == tracks.end() || second == tracks.end())
			continue;

		QPointF start;
		QPointF end;
		if (!previewPointAtNode(*first->second.first, connection.firstNodeId, first->second.second, start) || !previewPointAtNode(*second->second.first, connection.secondNodeId, second->second.second, end))
			continue;

		auto* item = new ConnectionItem(QLineF(start, end));
		item->setPen(connectionPen(line_width));
		tagPreviewItem(item, "connection", connection.id);
		scene->addItem(item);
		includePreviewPoint(start);
		includePreviewPoint(end);
	}

	// A run adds the connections first and the track lines after them, so a track is drawn over a connection.
	for (const TrackPreviewLine* drawn : drawnLines) {
		const auto& line = *drawn;
		const qreal offset = static_cast<qreal>(line.displayOffset);
		for (std::size_t point = 1; point < line.points.size(); ++point) {
			const auto arc = std::find_if(sceneModel.arcs.begin(), sceneModel.arcs.end(), [&](const SceneArc& candidate) {
				return candidate.trackId == line.id
					&& ((candidate.fromNodeId == line.points[point - 1].nodeId
							&& candidate.toNodeId == line.points[point].nodeId)
						|| (candidate.toNodeId == line.points[point - 1].nodeId
							&& candidate.fromNodeId == line.points[point].nodeId));
			});
			const TrackVisual visual = classifyTrackSpeed(arc == sceneModel.arcs.end() ? 0.0 : arc->speedLimitMs);
			QPen pen(selectedTrackIds.count(line.id) > 0 ? kSelectionCueColor : visual.color);
			pen.setWidth(selectedTrackIds.count(line.id) > 0 ? 4 : visual.width);
			pen.setCosmetic(true);
			auto* item = new TrackLineItem(QLineF(line.points[point - 1].x, line.points[point - 1].y + offset,
				line.points[point].x, line.points[point].y + offset));
			item->setPen(pen);
			tagPreviewItem(item, arc == sceneModel.arcs.end() ? "track" : "arc",
				arc == sceneModel.arcs.end() ? line.id : arc->id);
			scene->addItem(item);
		}
	}

	const auto memberships = previewStationMemberships(sceneModel);
	std::map<std::string, int> connectionDegrees;
	for (const auto& connection : sceneModel.connections) {
		++connectionDegrees[connection.fromNodeId];
		++connectionDegrees[connection.toNodeId];
	}
	std::map<std::string, QGraphicsItem*> nodeItems;
	for (const auto& line : preview.lines) {
		if (!tracks.count(line.id))
			continue;
		for (std::size_t index = 0; index < line.points.size(); ++index) {
			const auto& point = line.points[index];
			const auto membership = memberships.find(point.nodeId);
			// Current runnable-chain presentation: first point is ordinary;
			// four-node ordinary starts paint zero size, but the terminal dot remains.
			const bool station = index > 0 && membership != memberships.end();
			const qreal size = (station ? station_node_size
										: (line.points.size() == 4 && index + 1 < line.points.size() ? 0 : node_size))
				* presentationScale();
			QRectF rect(0, 0, size, size);
			rect.moveCenter(QPointF(point.x, point.y + line.displayOffset));
			QAbstractGraphicsShapeItem* item;
			if (station) {
				auto* square = new StationNodeItem(rect);
				const auto& value = membership->second;
				// A platform darkens the square only at the end of the line; an interior one is the ordinary grey.
				const bool lineEnd = index + 1 == line.points.size();
				const StationVisual visual = classifyStation(lineEnd && !value.platformId.empty()
						&& value.platformId != "None",
					connectionDegrees[point.nodeId]);
				square->setPen(QPen(visual.outline, 0));
				square->setBrush(visual.fill);
				item = square;
				tagPreviewItem(item, "station-node", point.nodeId);
				item->setData(PreviewGraphics::StationId, QString::fromStdString(value.stationId));
				item->setData(PreviewGraphics::PlatformId, QString::fromStdString(value.platformId));
				if (initial_variables.PAX_GUI) {
					QRectF barRect(0, 0, 5 * size, 0.9 * size);
					barRect.moveCenter(rect.center() - QPointF(0, 1.15 * size));
					auto* bar = new PlatformItem(barRect, square);
					bar->setPen(QPen(Qt::white, 0));
					bar->setBrush(Qt::white);
					m_stationDecorations.push_back(bar);
					bar->setVisible(m_stationLayerVisible);
				}
			} else {
				item = new NodeItem(rect);
				item->setPen(QPen(Qt::lightGray, 0));
				item->setBrush(Qt::lightGray);
				tagPreviewItem(item, "node", point.nodeId);
			}
			QPen pen = item->pen();
			pen.setCosmetic(true);
			item->setPen(pen);
			scene->addItem(item);
			nodeItems[point.nodeId] = item;
		}
	}

	// Selected-only editor emphasis is separate from structural boundary marks.
	if (m_infrastructureDock && m_infrastructureDock->isVisible()) {
		if (selectedFacet == "arcs") {
			for (const auto& arc : sceneModel.arcs) {
				if (arc.id != selectedId)
					continue;
				const auto track = tracks.find(arc.trackId);
				if (track == tracks.end())
					break;
				QPointF start;
				QPointF end;
				if (!previewPointAtNode(*track->second.first, arc.fromNodeId, track->second.second, start) || !previewPointAtNode(*track->second.first, arc.toNodeId, track->second.second, end))
					break;
				QPainterPath path(start);
				path.lineTo(end);
				QPen highlight(QColor(255, 110, 90));
				highlight.setWidthF(6.0);
				highlight.setCosmetic(true);
				auto* item = scene->addPath(path, highlight);
				item->setAcceptedMouseButtons(Qt::NoButton);
				break;
			}
		}

		if (selectedFacet == "connections") {
			for (const auto& connection : sceneModel.connections) {
				if (connection.id != selectedId)
					continue;
				const auto fromNode = std::find_if(sceneModel.nodes.begin(), sceneModel.nodes.end(),
					[&connection](const SceneNode& node) { return node.id == connection.fromNodeId; });
				const auto toNode = std::find_if(sceneModel.nodes.begin(), sceneModel.nodes.end(),
					[&connection](const SceneNode& node) { return node.id == connection.toNodeId; });
				if (fromNode == sceneModel.nodes.end() || toNode == sceneModel.nodes.end())
					break;
				const auto first = tracks.find(fromNode->trackId);
				const auto second = tracks.find(toNode->trackId);
				if (first == tracks.end() || second == tracks.end())
					break;
				QPointF start;
				QPointF end;
				if (!previewPointAtNode(*first->second.first, fromNode->id, first->second.second, start)
					|| !previewPointAtNode(*second->second.first, toNode->id, second->second.second, end))
					break;
				QPainterPath path(start);
				path.lineTo(end);
				QPen highlight(QColor(255, 110, 90));
				highlight.setWidthF(6.0);
				highlight.setCosmetic(true);
				auto* item = scene->addPath(path, highlight);
				item->setAcceptedMouseButtons(Qt::NoButton);
				break;
			}
		}
	}

	// Station anchors: one fixed-size marker and name per canonical station.
	for (const auto& station : preview.stations) {
		if (station.name.find("virtual") != std::string::npos)
			continue;
		for (const auto& line : preview.lines) {
			if (!tracks.count(line.id))
				continue;
			const auto& points = line.points;
			QPointF anchor;
			if (!station.nodeId.empty()) {
				if (!previewPointAtNode(line, station.nodeId, line.displayOffset, anchor))
					continue;
			} else {
				const double minX = std::min(points.front().rawX, points.back().rawX);
				const double maxX = std::max(points.front().rawX, points.back().rawX);
				if (station.x < minX || station.x > maxX)
					continue;
				if (!previewPointAtX(line, station.x, line.displayOffset, anchor))
					break;
			}
			const auto sceneStation = std::find_if(sceneModel.stations.cbegin(), sceneModel.stations.cend(),
				[&station](const SceneStation& candidate) { return candidate.id == station.id; });
			paintStationOverlay(anchor, classifyStation(), station.name, presentationScale(),
				stationDecorationOffset(sceneModel.stationViews, station.id, station.name) * presentationScale(),
				sceneStation == sceneModel.stations.cend() ? 0 : static_cast<int>(sceneStation->platforms.size()));
			auto* overlay = m_stationOverlays.back();
			tagPreviewItem(overlay, "station", station.id);
			const auto bind = [&](QGraphicsItem* decoration, bool artwork) {
				if (artwork) tagPreviewItem(decoration, "station", station.id);
				const auto node = nodeItems.find(station.nodeId);
				if (node != nodeItems.end()) {
					const QPointF position = decoration->scenePos();
					decoration->setParentItem(node->second);
					decoration->setPos(node->second->mapFromScene(position));
				}
			};
			if (auto* picture = m_stationPictures.value(overlay, nullptr))
				bind(picture, true);
			bind(m_stationLabels.back(), false);
			break;
		}
	}

	// Preview signals are derived from canonical block boundaries.  They stay
	// in the existing decoration list so the layer toggle and teardown own them.
	for (const auto& signal : preview.previewSignals) {
		const auto track = tracks.find(signal.trackId);
		if (track == tracks.end())
			continue;
		QPointF center;
		if ((!signal.nodeId.empty()
				&& !previewPointAtNode(*track->second.first, signal.nodeId,
					track->second.second, center))
			|| (signal.nodeId.empty()
				&& !previewPointAtX(*track->second.first, signal.rawX,
					track->second.second, center)))
			continue;
		QPointF normal;
		if (!previewSignalNormal(*track->second.first, signal.rawX, normal))
			continue;

		QStringList canonicalIds;
		for (const SceneSignal& source : sceneSignals(sceneModel))
			if (source.protectedSection == signal.sectionId)
				canonicalIds.append(QString::fromStdString(source.id));
		QPointF before, after;
		if (!previewPointAtX(*track->second.first, signal.rawX - 0.008,
				track->second.second, before)
			|| !previewPointAtX(*track->second.first, signal.rawX + 0.008,
				track->second.second, after))
			continue;
		const SignalGeometry geometry = signalGeometry(center, before, after, normal, track_separation * presentationScale());
		for (const bool reversed : {true, false}) {
			const QLineF base = reversed ? geometry.reversedBase : geometry.forwardBase;
			const QLineF post = reversed ? geometry.reversedPost : geometry.forwardPost;
			const QPointF head = reversed ? geometry.reversedHead : geometry.forwardHead;
			QPen white(Qt::white, line_width);
			white.setCosmetic(true);
			for (const QLineF& stroke : {base, post}) {
				auto* line = scene->addLine(stroke, white);
				line->setData(kSignalDecorationRole, true);
				line->setData(kSignalTrackRole, -1);
				line->setData(kSignalBaseVisibleRole, true);
				line->setAcceptedMouseButtons(Qt::NoButton);
				m_signalDecorations.push_back(line);
				line->setVisible(m_signalLayerVisible);
			}
			auto* glyph = new SignalItem(QRectF(-node_size * presentationScale() / 2.0, -node_size * presentationScale() / 2.0,
				node_size * presentationScale(), node_size * presentationScale()));
			tagPreviewItem(glyph, "signal", signal.sectionId);
			glyph->setData(PreviewGraphics::TrackId, QString::fromStdString(signal.trackId));
			glyph->setData(PreviewGraphics::Reversed, reversed);
			glyph->X = signal.rawX;
			glyph->sectionAheadId = signal.sectionId;
			glyph->setInspectionIdentity(QStringLiteral("Track %1 at %2 km; section %3%4")
					.arg(QString::fromStdString(signal.trackId))
					.arg(signal.rawX, 0, 'g', 10)
					.arg(QString::fromStdString(signal.sectionId))
					.arg(canonicalIds.isEmpty() ? QString() : QStringLiteral("; signal ID %1").arg(canonicalIds.join(", "))));
			glyph->setPen(signalHeadPen());
			glyph->setZValue(3.0);
			glyph->setPos(head);
			glyph->setAspectCode(180);
			glyph->setReversedDirection(reversed);
			glyph->setData(kSignalDecorationRole, true);
			glyph->setData(kSignalTrackRole, -1);
			glyph->setData(kSignalBaseVisibleRole, true);
			glyph->setData(kSignalAnchorRole, head);
			glyph->setAcceptedMouseButtons(Qt::NoButton);
			scene->addItem(glyph);
			m_signalDecorations.push_back(glyph);
			glyph->setVisible(m_signalLayerVisible);
		}
	}
	m_previewHasSignals = !preview.previewSignals.empty()
		&& std::any_of(m_signalDecorations.cbegin(), m_signalDecorations.cend(),
			[](QGraphicsItem* item) { return qgraphicsitem_cast<SignalItem*>(item) != nullptr; });

	// Fit the drawn geometry directly; the generic fitView minimum-scale clamp
	// is tuned for case-study coordinates and crops these kilometre-scale paths.
	if (hasPreviewBounds) {
		const QPointF target = previewBounds.center();
		qreal bestDistance = std::numeric_limits<qreal>::max();
		for (const auto& track : tracks) {
			const auto& line = *track.second.first;
			const qreal offset = track.second.second;
			for (std::size_t index = 1; index < line.points.size(); ++index) {
				const QPointF first(line.points[index - 1].x, line.points[index - 1].y + offset);
				const QPointF second(line.points[index].x, line.points[index].y + offset);
				const QPointF segment = second - first;
				const qreal lengthSquared = QPointF::dotProduct(segment, segment);
				const qreal ratio = lengthSquared > 0.0
					? qBound<qreal>(0.0, QPointF::dotProduct(target - first, segment) / lengthSquared, 1.0)
					: 0.0;
				const QPointF candidate = first + ratio * segment;
				const QPointF delta = candidate - target;
				const qreal distance = QPointF::dotProduct(delta, delta);
				if (distance < bestDistance) {
					bestDistance = distance;
					m_previewZoomFocus = candidate;
				}
			}
		}
		if (previewBounds.height() < previewBounds.width() * 0.2) {
			const qreal grow = previewBounds.width() * 0.2 - previewBounds.height();
			previewBounds.adjust(0, -grow / 2.0, 0, grow / 2.0);
		}
		const qreal marginX = previewBounds.width() * 0.05;
		const qreal marginY = previewBounds.height() * 0.1;
		previewBounds.adjust(-marginX, -marginY, marginX, marginY);
		m_previewFitBounds = previewBounds;
		scene->setSceneRect(previewBounds);
		networkView->fitToBounds(previewBounds);
		updateViewportOverlays();
	}
	updateNetworkLegend();
	statusBar()->showMessage(QString("Previewing %1 trackline(s); simulation not running")
			.arg(static_cast<int>(tracks.size())));
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
	m_document->markSaved();
	m_modifiedScenarioIds.clear();
	updateSceneWindowTitle();
	updateSceneActions();
	statusBar()->showMessage("Scene saved");
	refreshValidationPanel();
	refreshCompositionPanel();
	refreshTrainUnitPanel();
	refreshServicePanel();
	refreshIncidentPanel();
	refreshInfrastructurePanel();
	refreshPassengerPanel();
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

	const QString startPath = (m_sceneDir.isEmpty() || isInstalledScenePath(m_sceneDir, QCoreApplication::applicationDirPath())) ? QDir::homePath()
																																 : (QFileInfo(m_sceneDir).isDir() ? m_sceneDir : QFileInfo(m_sceneDir).absoluteFilePath());
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

	const QString startDir = (m_sceneDir.isEmpty() || isInstalledScenePath(m_sceneDir, QCoreApplication::applicationDirPath())) ? QDir::homePath()
																																: (QFileInfo(m_sceneDir).isDir() ? m_sceneDir : QFileInfo(m_sceneDir).absolutePath());
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

bool MainWindow::privacyDialogTestHook() const {
#if EGTRAIN_BUILD_TESTS
	const QString mode = qEnvironmentVariable("QEGTRAIN_E2E_PRIVACY_DIALOG");
	return (mode == QStringLiteral("1") || mode == QStringLiteral("settings"))
		&& !qEnvironmentVariable("QEGTRAIN_E2E_SETTINGS_DIR").isEmpty();
#else
	return false;
#endif
}

#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
void MainWindow::runTelemetrySmoke(int stage) {
	if (!m_telemetrySmokeState || !m_telemetrySender) return;
	const QString root = QDir(qEnvironmentVariable("QEGTRAIN_E2E_TELEMETRY_SMOKE")).canonicalPath();
	const QString mode = qEnvironmentVariable("QEGTRAIN_E2E_TELEMETRY_MODE", "mixed");
	const auto later = [this](int next, int delay = 500) {
		QTimer::singleShot(delay, this, [this, next] { runTelemetrySmoke(next); });
	};
	if (stage == 0) {
		if ((mode != "retired_restart" && m_telemetrySmokeState->polls.load() < 1)
			|| (mode == "retired" && m_telemetrySmokeState->posts.load() < 1)) {
			later(0, 20);
			return;
		}
		telemetry_smoke::dismissMessages();
		if (mode == "import_late" || mode == "import_revoke") {
			if (mode == "import_revoke" && m_telemetrySmokeState->posts.load() < 1) {
				later(0, 20);
				return;
			}
			const int poll = m_telemetrySmokeState->polls.load();
			telemetry_smoke::chooseDirectory(root + "/private-legacy", "Select Legacy Case Folder",
				[this, mode, poll, changed = false]() mutable {
					if (!changed) {
						changed = true;
						if (mode == "import_revoke") m_telemetryConsent->save(false, false);
						m_telemetryConsent->save(true, true);
						m_telemetrySender->requestConsentRefresh();
					}
					return m_telemetrySmokeState->polls.load() > poll;
				});
			telemetry_smoke::chooseDirectory(root + "/imported", "Select Scene Destination");
			actionLoad_Network();
			if (m_sceneDir != QFileInfo(root + "/imported").canonicalFilePath()) {
				std::fprintf(stderr, "E2E_TELEMETRY_IMPORT_FAILED\n");
				QCoreApplication::exit(2);
				return;
			}
			std::fprintf(stdout, "E2E_TELEMETRY_IMPORTED\n");
		} else if (mode == "late") {
			// Real actions while disabled must not be reconstructed after enable.
			openSceneDirectory(m_sceneDir);
			m_caseSettingsDock->hide();
			revealEditorDock(m_caseSettingsDock);
			const int poll = m_telemetrySmokeState->polls.load();
			saveSceneBundle(m_sceneModel, (root + "/historical.egscene").toStdString());
			telemetry_smoke::chooseFile(root + "/historical.egscene", [this, poll, changed = false]() mutable {
				if (!changed) {
					changed = true;
					m_telemetryConsent->save(true, true);
					m_telemetrySender->requestConsentRefresh();
				}
				return m_telemetrySmokeState->polls.load() > poll;
			});
			openSceneDialog();
		} else if (mode == "revoke") {
			if (m_telemetrySmokeState->posts.load() < 1) {
				later(0, 20);
				return;
			}
			DiagramWindow diagram("Private in-flight chart", this);
			diagram.setTelemetryCapture([this] { return captureTelemetryOperation(); });
			diagram.setCsvProvider([](const QStringList&) { return std::string("private,content\n"); }, "stale.csv");
			attachRunProvenance(&diagram, captureRunProvenance());
			QDir().mkpath(root + "/stale.csv.provenance.json");
			const int poll = m_telemetrySmokeState->polls.load();
			telemetry_smoke::chooseFile(root + "/stale.csv", [this, poll, changed = false]() mutable {
				if (!changed) {
					changed = true;
					m_telemetryConsent->save(false, false);
					m_telemetryConsent->save(true, true);
					m_telemetrySender->requestConsentRefresh();
				}
				return m_telemetrySmokeState->polls.load() > poll;
			});
			QMetaObject::invokeMethod(&diagram, "exportCsv", Qt::DirectConnection);
		}
		later(1);
		return;
	}
	if (stage == 1) {
		const QString scenePath = root + "/case";
		openSceneDirectory(scenePath);
		openSceneDirectory(scenePath); // Successful reload is a separate occurrence.
		openSceneDirectory(root + "/invalid");
		openSceneDirectory(root + "/ambiguous");
		if (mode == "unsupported") openSceneDirectory(root + "/newer");
		if (mode == "malformed_bundle") {
			openSceneDirectory(root + "/missing-schema.egscene");
			openSceneDirectory(root + "/noninteger-schema.egscene");
		}
		telemetry_smoke::chooseFile(QString());
		openSceneDialog();
		for (auto it = m_editorActions.cbegin(); it != m_editorActions.cend(); ++it) {
			it.key()->hide();
			it.value()->trigger();		// Built-in QAction ordering must not hide the prior state.
			revealEditorDock(it.key()); // Focus/tab activation is not another open.
		}
		hide();
		show(); // Parent visibility never counts editor opens.
		DiagramWindow diagram("Private chart name", this);
		diagram.setTelemetryCapture([this] { return captureTelemetryOperation(); });
		diagram.setCsvProvider([](const QStringList&) { return std::string("private,content\n1,2\n"); }, "chart.csv");
		attachRunProvenance(&diagram, captureRunProvenance());
		for (int repeat = 0; repeat < 2; ++repeat) {
			telemetry_smoke::chooseFile(root + "/chart.csv");
			QMetaObject::invokeMethod(&diagram, "exportCsv", Qt::DirectConnection);
		}
		telemetry_smoke::chooseFile(QString());
		QMetaObject::invokeMethod(&diagram, "exportCsv", Qt::DirectConnection);
		QDir().mkpath(root + "/blocked.csv.provenance.json");
		telemetry_smoke::chooseFile(root + "/blocked.csv");
		QMetaObject::invokeMethod(&diagram, "exportCsv", Qt::DirectConnection);
		telemetry_smoke::chooseFile(root + "/chart.png");
		QMetaObject::invokeMethod(&diagram, "exportPng", Qt::DirectConnection);
		TimetableTableWindow table({}, m_startOffsetSeconds, [](const QStringList&) { return std::string("private,table\n"); }, this);
		table.setTelemetryCapture([this] { return captureTelemetryOperation(); });
		table.setRunProvenance(captureRunProvenance());
		telemetry_smoke::chooseFile(root + "/table.csv");
		QMetaObject::invokeMethod(&table, "exportCsv", Qt::DirectConnection);
		telemetry_smoke::chooseFile(root + "/table.png");
		QMetaObject::invokeMethod(&table, "exportPng", Qt::DirectConnection);
		telemetry_smoke::chooseFile(QString());
		QMetaObject::invokeMethod(&table, "exportCsv", Qt::DirectConnection);
		telemetry_smoke::chooseFile(QString());
		QMetaObject::invokeMethod(&table, "exportPng", Qt::DirectConnection);
		if (mode == "table_failure") {
			QDir().mkpath(root + "/blocked_table.png.provenance.json");
			telemetry_smoke::chooseFile(root + "/blocked_table.png");
			QMetaObject::invokeMethod(&table, "exportPng", Qt::DirectConnection);
		}
		saveSceneToCurrentDir(); // Directory save has no usage kind.
		QDir(root + "/failed.egscene").removeRecursively();
		telemetry_smoke::chooseFile(root + "/failed.egscene", {}, [root] {
			QDir().mkpath(root + "/failed.egscene");
		});
		saveSceneAsToBundle(); // Failed Save As still requests scene_bundle, not current directory kind.
		telemetry_smoke::chooseFile(root + "/saved.egscene");
		saveSceneAsToBundle();
		telemetry_smoke::chooseFile(QString());
		saveSceneAsToBundle();
		++m_sceneModel.schemaVersion;
		telemetry_smoke::chooseFile(root + "/schema.egscene");
		saveSceneAsToBundle(); // Unsupported schema before staging: diagnostics only.
		--m_sceneModel.schemaVersion;
		if (mode == "bundle") openSceneDirectory(root + "/saved.egscene");
		openSceneDirectory(scenePath);
		const auto services = m_sceneModel.services;
		m_sceneModel.services.clear();
		runScene(); // Handled preflight failure: no usage run pair.
		m_sceneModel.services = services;
		later(2);
		return;
	}
	if (stage == 2) {
		if (mode == "install_failure") m_telemetrySmokeState->installFailurePoint = 0;
		if (mode == "run_revoke" || mode == "discard") {
			m_speedSlider->setValue(kMaxStepDelayMs - 10);
			auto* timer = new QTimer(this);
			timer->setInterval(20);
			connect(timer, &QTimer::timeout, this, [this, timer, mode] {
				if (!m_telemetrySmokeState->startedPosts.load()) return;
				timer->stop();
				timer->deleteLater();
				if (mode == "discard") {
					markSceneDirty();
					return;
				}
				m_telemetryConsent->save(false, false);
				m_telemetryConsent->save(true, true);
				m_telemetrySender->requestConsentRefresh();
			});
			timer->start();
		}
		if (mode == "zero") {
			teardownGUI();
			simulation.resetState();
			startSimulation(captureTelemetryOperation());
		} else runCurrent();
		if (mode == "teardown" || mode == "replacement") clearSimulationWorker(true);
		if (mode == "replacement") {
			openSceneDirectory(root + "/case");
			runCurrent(); // A queued GUI completion for the joined worker is stale.
		}
		later(3, 50);
		return;
	}
	if (stage == 3 || stage == 5) {
		if (m_worker) {
			later(stage, 50);
			return;
		}
		if (mode == "install_failure") {
			extern InitialParameters initial_variables;
			const int ordinal = m_telemetrySmokeState->completedProbeRuns;
			if (!m_resultsAvailable
				|| !telemetry_smoke::recordRun(root, ordinal, buildRunSummaryCsv(m_completedRunResults),
					QString::fromStdString(initial_variables.OutputMainFolder) + "/EnergyConsumptionPerTrain.txt")) {
				std::fprintf(stderr, "E2E_TELEMETRY_INSTALL_RUN_FAILED\n");
				QCoreApplication::exit(2);
				return;
			}
			++m_telemetrySmokeState->completedProbeRuns;
			if (ordinal < 4) {
				teardownGUI();
				simulation.resetState();
				simulation.runSimulation(); // Zero-train guard probes any orphaned rejection observer.
				m_telemetrySmokeState->installFailurePoint = ordinal < 3 ? ordinal + 1 : -1;
				runCurrent();
				later(3, 50);
				return;
			}
			std::fprintf(stdout, "E2E_TELEMETRY_INSTALL_PROBES runs=%d failures=%d\n",
				m_telemetrySmokeState->completedProbeRuns, m_telemetrySmokeState->installFailures);
		}
		if (stage == 3 && mode == "repeat") {
			runCurrent();
			later(5, 50);
			return;
		}
		if (m_resultsAvailable && mode != "teardown") {
			telemetry_smoke::chooseFile(root + "/summary.csv");
			if (auto* button = findChild<QPushButton*>("resultView_ExportCSV")) button->click();
			telemetry_smoke::chooseFile(root + "/summary.png");
			if (auto* button = findChild<QPushButton*>("resultView_ExportPNG")) button->click();
		}
		later(4, 2000);
		return;
	}
	std::fprintf(stdout, "E2E_TELEMETRY_ACTIONS_DONE mode=%s posts=%d\n",
		mode.toUtf8().constData(), m_telemetrySmokeState->posts.load());
	std::fflush(stdout);
	QCoreApplication::quit();
}
#endif

void MainWindow::startTelemetryAfterInitialConsent() {
	if (m_telemetrySender) return;
	try {
		if (privacyDialogTestHook()) return;
		TelemetryContext context = applicationTelemetryContext();
#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
		const QString requestedDir = qEnvironmentVariable("QEGTRAIN_E2E_TELEMETRY_SMOKE");
		const QString isolatedDir = QDir(requestedDir).canonicalPath();
		const QString temporaryRoot = QDir(QDir::tempPath()).canonicalPath();
		// Compare resolved paths: Python and Qt may use different spellings of macOS /var.
		const bool isolated = !requestedDir.isEmpty() && QDir(requestedDir).isAbsolute()
			&& QDir(requestedDir).exists() && !temporaryRoot.isEmpty()
			&& isolatedDir.startsWith(temporaryRoot + QLatin1Char('/'));
		if (!requestedDir.isEmpty() && !isolated) {
			std::fprintf(stderr, "E2E_TELEMETRY_MOCK_REFUSED reason=invalid_isolation_root\n");
			return;
		}
		if (isolated)
			context = {true, true, true, QStringLiteral("https://127.0.0.1:19497/collect"), QStringLiteral("1")};
#endif
		if (!context.capable()) return;
#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
		if (isolated) {
			m_telemetrySettings.reset(new QSettings(QDir(isolatedDir).filePath("consent.ini"), QSettings::IniFormat));
		} else
#endif
			m_telemetrySettings.reset(new QSettings);
		m_telemetryConsent.reset(new TelemetryConsent(*m_telemetrySettings, context));
#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
		const QString smokeMode = qEnvironmentVariable("QEGTRAIN_E2E_TELEMETRY_MODE", "mixed");
		if (isolated
			&& !m_telemetryConsent->save(smokeMode != "diagnostics" && smokeMode != "late" && smokeMode != "import_late",
				smokeMode != "usage" && smokeMode != "late" && smokeMode != "import_late")) {
			std::fprintf(stderr, "E2E_TELEMETRY_MOCK_REFUSED reason=consent_storage_failure\n");
			return;
		}
#endif
		telemetry::Application metadata;
		metadata.version = QCoreApplication::applicationVersion();
#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
		if (isolated) {
			m_telemetrySmokeState = std::make_shared<telemetry_smoke::State>();
			m_telemetrySender.reset(new telemetry::TelemetrySender(context, metadata,
				QDir(isolatedDir).filePath("queue"), telemetry_smoke::options(isolatedDir, smokeMode, m_telemetrySmokeState)));
			QTimer::singleShot(0, this, [this] { runTelemetrySmoke(0); });
		} else
#endif
			m_telemetrySender.reset(new telemetry::TelemetrySender(context, metadata,
				QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
					.filePath(QStringLiteral("Telemetry"))));
		m_telemetrySender->requestInteractiveSession();
		connect(m_telemetryConsent.get(), &TelemetryConsent::revoked, this, [this](bool usage, bool diagnostics) {
				if (m_telemetrySender) m_telemetrySender->invalidateConsent(usage, diagnostics); }, Qt::DirectConnection);
		connect(
			m_telemetryConsent.get(), &TelemetryConsent::receiverChanged, this,
			[this] { if (m_telemetrySender) m_telemetrySender->invalidateReceiver(); }, Qt::DirectConnection);
	} catch (...) {
#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
		if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_TELEMETRY_SMOKE"))
			std::fprintf(stderr, "E2E_TELEMETRY_MOCK_REFUSED reason=initialization_exception\n");
#endif
		m_telemetrySender.reset();
		m_telemetryConsent.reset();
		m_telemetrySettings.reset();
	}
}

void MainWindow::showPrivacySettings(bool initialPrompt) {
	QSettings previewSettings;
	const bool preview = privacyDialogTestHook();
	TelemetryContext context = applicationTelemetryContext();
	if (preview) {
		// A visual-test-only fake context: no sender exists and the URL is never contacted.
		context = {true, true, true, QStringLiteral("https://127.0.0.1/consent-preview"), QStringLiteral("1")};
	}
	TelemetryConsent previewConsent(previewSettings, context);
	TelemetryConsent& consent = preview || !m_telemetryConsent ? previewConsent : *m_telemetryConsent;
	if (!consent.available()) {
		if (!initialPrompt)
			QMessageBox::information(this, QStringLiteral("Privacy & diagnostics"),
				QStringLiteral("Usage statistics and diagnostics are unavailable in this build or run. Nothing is collected or sent."));
		return;
	}
	if (privacyDialogTestHook()
		&& qEnvironmentVariable("QEGTRAIN_E2E_PRIVACY_DIALOG") == QStringLiteral("settings"))
		initialPrompt = false;
	if (initialPrompt && !consent.promptRequired()) return;
	TelemetryConsentDialog dialog(consent, context.domain(), initialPrompt, this);
	if (privacyDialogTestHook() && !qEnvironmentVariable("QEGTRAIN_E2E_SCREENSHOT").isEmpty()) {
		QTimer::singleShot(200, &dialog, [&dialog] {
			dialog.grab().save(qEnvironmentVariable("QEGTRAIN_E2E_SCREENSHOT"));
		});
	}
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
			QStringLiteral("EGTRAIN can check GitHub Releases when the application starts and notify you "
						   "when a newer stable version is available."),
			QMessageBox::NoButton, this);
		dialog.setInformativeText(QStringLiteral("You can change this later from Help."));
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
		QStringLiteral("You are currently using %1.").arg(formatSemanticVersion(*current)),
		QMessageBox::NoButton, this);
	dialog.setInformativeText((release.notes.isEmpty()
									  ? QStringLiteral("No release notes were provided.")
									  : release.notes)
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

void MainWindow::updateScenarioPresentation() {
	if (!m_scenarioListWidget)
		return;
	const bool details = advancedDetailsEnabled();
	const auto validationStatus = [this](std::size_t index) {
		const std::string prefix = "scenarios[" + std::to_string(index) + "]";
		int errors = 0;
		int warnings = 0;
		for (const auto& diagnostic : m_sceneDiagnostics) {
			if (diagnostic.path.rfind(prefix, 0) != 0)
				continue;
			if (diagnostic.severity == SceneSeverity::Error)
				++errors;
			else if (diagnostic.severity == SceneSeverity::Warning)
				++warnings;
		}
		return errors > 0 ? QStringLiteral("Invalid")
						  : (warnings > 0 ? QStringLiteral("Warning") : QStringLiteral("Ready"));
	};
	for (std::size_t index = 0; index < m_sceneModel.scenarios.size()
		&& index < static_cast<std::size_t>(m_scenarioListWidget->count());
		++index) {
		const SceneScenario& scenario = m_sceneModel.scenarios[index];
		const QString description = scenario.description.empty()
			? QStringLiteral("(no description)")
			: QString::fromStdString(scenario.description);
		QString label = QString("%1 — %2")
							.arg(QString::fromStdString(scenario.id), QString::fromStdString(scenario.name));
		if (details) {
			label += QString(" | %1 | %2 incident(s) | %3 delay(s) | %4")
						 .arg(description)
						 .arg(static_cast<int>(scenario.incidents.size()))
						 .arg(static_cast<int>(scenario.entranceDelays.size()))
						 .arg(validationStatus(index));
		} else {
			label += QString(" | %1 incident(s) | %2 delay(s)")
						 .arg(static_cast<int>(scenario.incidents.size()))
						 .arg(static_cast<int>(scenario.entranceDelays.size()));
			if (!scenario.description.empty())
				label += QString(" | %1").arg(description);
			if (validationStatus(index) == QStringLiteral("Invalid"))
				label += QStringLiteral(" | Invalid");
		}
		if (scenario.id == m_sceneModel.defaultScenarioId)
			label += QStringLiteral(" | default");
		if (m_modifiedScenarioIds.count(scenario.id) > 0)
			label += QStringLiteral(" | modified");
		if (auto* item = m_scenarioListWidget->item(static_cast<int>(index))) {
			item->setText(label);
			item->setToolTip(label);
		}
	}
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
		refreshScenarioList();
		refreshPassengerPanel();
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
	refreshScenarioList();
	refreshPassengerPanel();
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
	addRow(runtimeAndResults, "Applied scenario", m_appliedScenarioId.empty() ? QStringLiteral("(none)") : QString::fromStdString(m_appliedScenarioId), "1",
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

	QListWidget* targetList = nullptr;
	QDockWidget* targetDock = nullptr;
	if (targetType == "train_unit") {
		targetList = m_trainUnitListWidget;
		targetDock = m_trainUnitDock;
	} else if (targetType == "composition") {
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
	m_appliedScenarioId.clear();
	m_completedRunResults = RunResults();
	m_completedTimetableResults.clear();
	m_pendingRunProvenance = RunProvenance();
	m_completedRunProvenance = RunProvenance();
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
	if (m_committingPendingEditorValues)
		return;
	m_committingPendingEditorValues = true;
	const auto hasEditorFocus = [](QWidget* editor) {
		QWidget* focus = QApplication::focusWidget();
		return editor && (editor->hasFocus() || (focus && editor->isAncestorOf(focus)));
	};

	m_caseSettingsPanel->commitPendingEditors();
	commitTrainUnitIdEdit();
	for (std::size_t index = 0; index < m_trainUnitPhysicalEdits.size(); ++index) {
		QDoubleSpinBox* edit = m_trainUnitPhysicalEdits[index];
		if (hasEditorFocus(edit)) {
			edit->interpretText();
			commitTrainUnitPhysical(static_cast<int>(index));
		}
	}
	if (m_trainUnitTractionTable) {
		for (int row = 0; row < m_trainUnitTractionTable->rowCount(); ++row) {
			for (int column = 0; column < m_trainUnitTractionTable->columnCount(); ++column) {
				auto* edit = qobject_cast<QDoubleSpinBox*>(m_trainUnitTractionTable->cellWidget(row, column));
				if (hasEditorFocus(edit)) {
					edit->interpretText();
					commitTrainUnitTractionCell(row, column, edit->value());
				}
			}
		}
	}
	if (m_infrastructureFacetCombo && m_infrastructureTable
		&& m_infrastructureFacetCombo->currentData().toString() == QStringLiteral("platforms")) {
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
	commitPassengerIdEdit();
	commitPassengerJourneyIdEdit();
	commitPassengerJourneyActivity();
	for (std::size_t index = 0; index < m_passengerJourneyWindowEdits.size(); ++index) {
		QDoubleSpinBox* edit = m_passengerJourneyWindowEdits[index];
		if (hasEditorFocus(edit)) {
			edit->interpretText();
			commitPassengerJourneyWindow(static_cast<int>(index));
		}
	}
	commitPassengerLegIdEdit();
	if (m_passengerLegOccurrenceEdit && hasEditorFocus(m_passengerLegOccurrenceEdit)) {
		m_passengerLegOccurrenceEdit->interpretText();
		commitPassengerLegOccurrence();
	}
	commitTrainUnitSources();
	commitCompositionIdEdit();
	commitPendingServiceSettings();
	// Scenario and incident controls are disabled and cleared during a run.
	// Their placeholder values are not pending edits.
	if (m_worker) {
		m_committingPendingEditorValues = false;
		return;
	}
	commitScenarioIdEdit();
	commitScenarioNameEdit();
	commitScenarioDescriptionEdit();
	commitIncidentIdEdit();
	commitIncidentStartSeconds();
	if (m_incidentEndSecondsEdit && ((m_incidentHasEndSecondsCheck && m_incidentHasEndSecondsCheck->isChecked()) || hasEditorFocus(m_incidentEndSecondsEdit)))
		commitIncidentEndSeconds();
	if (m_incidentHasReducedSpeedCheck && m_incidentHasReducedSpeedCheck->isChecked()
		&& m_incidentReducedSpeedKmhEdit) {
		m_incidentReducedSpeedKmhEdit->interpretText();
		commitIncidentReducedSpeed();
	}
	if (m_entranceDelayOccurrenceEdit && hasEditorFocus(m_entranceDelayOccurrenceEdit)) {
		m_entranceDelayOccurrenceEdit->interpretText();
		commitEntranceDelayOccurrence();
	}
	if (m_entranceDelaySecondsEdit && hasEditorFocus(m_entranceDelaySecondsEdit)) {
		m_entranceDelaySecondsEdit->interpretText();
		commitEntranceDelaySeconds();
	}

	m_committingPendingEditorValues = false;
}

void MainWindow::commitPendingCaseSettings() {
	m_caseSettingsPanel->commitPendingEditors();
}

void MainWindow::commitCaseSettings() {
	m_caseSettingsPanel->commit();
}

void MainWindow::refreshBlockTrackFilter() {
	if (!m_blockTrackFilterCombo)
		return;
	const QString previousTrack = m_blockTrackFilterCombo->currentData().toString();
	const QSignalBlocker blocker(m_blockTrackFilterCombo);
	m_blockTrackFilterCombo->clear();
	std::set<std::string> knownTracks;
	for (const auto& track : m_sceneModel.tracks) {
		m_blockTrackFilterCombo->addItem(QString::fromStdString(track.id),
			QString::fromStdString(track.id));
		knownTracks.insert(track.id);
	}
	std::set<std::string> invalidTracks;
	for (const auto& block : m_sceneModel.blocks)
		if (knownTracks.count(block.trackId) == 0)
			invalidTracks.insert(block.trackId);
	for (const auto& trackId : invalidTracks) {
		const QString value = QString::fromStdString(trackId);
		m_blockTrackFilterCombo->addItem(trackId.empty()
				? QStringLiteral("Invalid track: (empty)")
				: QStringLiteral("Invalid track: %1").arg(value),
			value);
	}
	int selected = m_blockTrackFilterCombo->findData(previousTrack);
	if (selected < 0 && m_blockTrackFilterCombo->count() > 0)
		selected = 0;
	m_blockTrackFilterCombo->setCurrentIndex(selected);
	m_blockTrackFilterCombo->setVisible(m_infrastructureFacetCombo
		&& m_infrastructureFacetCombo->currentData().toString() == QStringLiteral("blocks"));
	m_blockTrackFilterCombo->setEnabled(m_sceneLoaded && !m_worker
		&& m_blockTrackFilterCombo->count() > 0);
}

void MainWindow::refreshRouteSectionPanel() {
	if (!m_routeSectionDetailWidget || !m_routeSectionCatalogCombo || !m_routeSectionListWidget
		|| !m_infrastructureFacetCombo || m_infrastructureFacetCombo->currentData().toString() != QStringLiteral("routes")) {
		if (m_routeSectionDetailWidget)
			m_routeSectionDetailWidget->setVisible(false);
		return;
	}
	m_routeSectionDetailWidget->setVisible(true);
	const bool editable = m_sceneLoaded && !m_worker;
	const int routeRow = m_infrastructureTable ? m_infrastructureTable->currentRow() : -1;
	const bool hasRoute = routeRow >= 0 && routeRow < static_cast<int>(m_sceneModel.routes.size());
	const SceneSectionInventory inventory = buildSceneSectionInventory(m_sceneModel);
	const QString previousCatalog = m_routeSectionCatalogCombo->currentData().toString();
	const int previousListRow = m_routeSectionListWidget->currentRow();
	{
		const QSignalBlocker blocker(m_routeSectionCatalogCombo);
		m_routeSectionCatalogCombo->clear();
		for (const auto& section : inventory.sections)
			m_routeSectionCatalogCombo->addItem(sceneSectionDisplayLabel(section),
				QString::fromStdString(section.id));
		int selected = m_routeSectionCatalogCombo->findData(previousCatalog);
		if (selected < 0 && m_routeSectionCatalogCombo->count() > 0)
			selected = 0;
		m_routeSectionCatalogCombo->setCurrentIndex(selected);
		m_routeSectionCatalogCombo->setEnabled(editable && hasRoute && selected >= 0);
	}
	{
		const QSignalBlocker blocker(m_routeSectionListWidget);
		m_routeSectionListWidget->clear();
		if (hasRoute) {
			const SceneRoute& route = m_sceneModel.routes[static_cast<std::size_t>(routeRow)];
			for (const std::string& raw : route.blocks) {
				auto* item = new QListWidgetItem(m_routeSectionListWidget);
				item->setData(Qt::UserRole, QString::fromStdString(raw));
				if (const auto* exact = inventory.exact(raw)) {
					item->setText(sceneSectionDisplayLabel(*exact));
				} else if (const auto* resolved = inventory.resolve(raw)) {
					item->setText(QStringLiteral("%1 (legacy: %2)")
							.arg(sceneSectionDisplayLabel(*resolved), QString::fromStdString(raw)));
				} else {
					item->setText(QStringLiteral("Invalid: %1").arg(QString::fromStdString(raw)));
				}
			}
		}
		const int row = m_routeSectionListWidget->count() == 0 ? -1
															   : std::min(std::max(previousListRow, 0), m_routeSectionListWidget->count() - 1);
		m_routeSectionListWidget->setCurrentRow(row);
	}
	m_routeSectionListWidget->setEnabled(editable && hasRoute);
	const int selectedRow = m_routeSectionListWidget->currentRow();
	if (m_addRouteSectionButton)
		m_addRouteSectionButton->setEnabled(editable && hasRoute
			&& m_routeSectionCatalogCombo->currentIndex() >= 0);
	if (m_removeRouteSectionButton)
		m_removeRouteSectionButton->setEnabled(editable && hasRoute && selectedRow >= 0);
	if (m_moveRouteSectionUpButton)
		m_moveRouteSectionUpButton->setEnabled(editable && hasRoute && selectedRow > 0);
	if (m_moveRouteSectionDownButton)
		m_moveRouteSectionDownButton->setEnabled(editable && hasRoute
			&& selectedRow >= 0 && selectedRow + 1 < m_routeSectionListWidget->count());
}

void MainWindow::refreshInfrastructurePanel(bool resetSelection) {
	const bool editable = m_sceneLoaded && !m_worker;
	if (m_infrastructureDock)
		m_infrastructureDock->setEnabled(editable);
	if (m_infrastructureFacetCombo) {
		const QSignalBlocker blocker(m_infrastructureFacetCombo);
		m_infrastructureFacetCombo->setEnabled(editable);
	}
	if (m_addInfrastructureButton)
		m_addInfrastructureButton->setEnabled(editable);
	refreshInfrastructureTable(resetSelection);
}

void MainWindow::refreshInfrastructureTable(bool resetSelection) {
	if (!m_infrastructureTable || !m_infrastructureFacetCombo)
		return;

	const QString facet = m_infrastructureFacetCombo->currentData().toString();
	const int previousRow = resetSelection ? -1 : m_infrastructureTable->currentRow();
	const QString previousId = resetSelection ? QString() : m_infrastructureSelectionId;
	if (facet == QStringLiteral("blocks"))
		refreshBlockTrackFilter();
	m_blockRowModelIndices.clear();
	QStringList headers;
	if (facet == "tracks")
		headers << "ID";
	else if (facet == "nodes")
		headers << "ID" << "Track ID" << "X (km)" << "Y (km)";
	else if (facet == "arcs")
		headers << "ID" << "Track ID" << "From node" << "To node"
				<< "Curvature radius (m)" << "Gradient (rise/length)" << "Speed limit (m/s)";
	else if (facet == "blocks")
		headers << "ID" << "Track ID" << "Length (km)" << "Order"
				<< "Start km" << "End km" << "Coverage";
	else if (facet == "connections")
		headers << "ID" << "From node" << "To node" << "Has speed limit"
				<< "Speed limit (m/s)";
	else if (facet == "stations")
		headers << "ID" << "Name" << "Has position" << "Position km";
	else if (facet == "platforms")
		headers << "Station ID" << "ID" << "Nodes" << "Length (m)" << "Width (m)";
	else if (facet == "signals")
		headers << "ID" << "Protected section";
	else if (facet == "signalling_areas")
		headers << "ID" << "Start km" << "End km" << "Signalling system" << "Track";
	else if (facet == "routes")
		headers << "ID" << "Sections" << "Has corridor" << "Corridor" << "Reversed";
	else if (facet == "block_dependencies")
		headers << "Block" << "Depends on";
	else if (facet == "single_track_restrictions")
		headers << "Start" << "End" << "Protected start" << "Protected end";
	else
		headers << "Entrance" << "Exit" << "Direction";

	QSignalBlocker blocker(m_infrastructureTable);
	m_infrastructureTable->clearContents();
	m_infrastructureTable->setColumnCount(headers.size());
	m_infrastructureTable->setHorizontalHeaderLabels(headers);
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
	if (facet == "tracks") {
		m_infrastructureTable->setRowCount(static_cast<int>(m_sceneModel.tracks.size()));
		for (int row = 0; row < m_infrastructureTable->rowCount(); ++row)
			setCell(row, 0, QString::fromStdString(m_sceneModel.tracks[static_cast<std::size_t>(row)].id));
	} else if (facet == "nodes") {
		m_infrastructureTable->setRowCount(static_cast<int>(m_sceneModel.nodes.size()));
		for (int row = 0; row < m_infrastructureTable->rowCount(); ++row) {
			const SceneNode& node = m_sceneModel.nodes[static_cast<std::size_t>(row)];
			setCell(row, 0, QString::fromStdString(node.id));
			setCell(row, 1, QString::fromStdString(node.trackId));
			setCell(row, 2, QString::number(node.xKm, 'g', precision));
			setCell(row, 3, QString::number(node.yKm, 'g', precision));
		}
	} else if (facet == "arcs") {
		m_infrastructureTable->setRowCount(static_cast<int>(m_sceneModel.arcs.size()));
		for (int row = 0; row < m_infrastructureTable->rowCount(); ++row) {
			const SceneArc& arc = m_sceneModel.arcs[static_cast<std::size_t>(row)];
			setCell(row, 0, QString::fromStdString(arc.id));
			setCell(row, 1, QString::fromStdString(arc.trackId));
			setCell(row, 2, QString::fromStdString(arc.fromNodeId));
			setCell(row, 3, QString::fromStdString(arc.toNodeId));
			setCell(row, 4, QString::number(arc.curvatureRadiusM, 'g', precision));
			setCell(row, 5, QString::number(arc.gradientPercent, 'g', precision));
			setCell(row, 6, QString::number(arc.speedLimitMs, 'g', precision));
		}
	} else if (facet == "blocks") {
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
			m_blockRowModelIndices.push_back(modelIndex);
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
	} else if (facet == "connections") {
		m_infrastructureTable->setRowCount(static_cast<int>(m_sceneModel.connections.size()));
		for (int row = 0; row < m_infrastructureTable->rowCount(); ++row) {
			const SceneConnection& connection = m_sceneModel.connections[static_cast<std::size_t>(row)];
			setCell(row, 0, QString::fromStdString(connection.id));
			setCell(row, 1, QString::fromStdString(connection.fromNodeId));
			setCell(row, 2, QString::fromStdString(connection.toNodeId));
			setCell(row, 3, connection.hasSpeedLimit ? QStringLiteral("true") : QStringLiteral("false"));
			setCell(row, 4, QString::number(connection.speedLimitMs, 'g', precision));
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
	m_infrastructureTable->setEnabled(m_sceneLoaded && !m_worker);
	if (auto* guidance = m_infrastructureDock->findChild<QLabel*>("infrastructureGuidance")) {
		const bool empty = m_infrastructureTable->rowCount() == 0;
		guidance->setText(!m_sceneLoaded	 ? "Open a case to edit infrastructure."
				: facet == "blocks" && empty ? "No blocks on this track. Choose another track or add a block."
				: facet == "routes" && empty ? "No routes yet. Add a route, then choose sections from the catalog."
				: empty						 ? QString("No %1 yet. Use Add to create one.").arg(m_infrastructureFacetCombo->currentText().toLower())
				: facet == "routes"			 ? "Select a route to edit its ordered sections below."
				: facet == "blocks"			 ? "Blocks are ordered on the chosen track; select a row to insert or move it."
											 : QString());
		guidance->setVisible(!guidance->text().isEmpty());
	}
	m_infrastructureTable->resizeColumnsToContents();
	int rowToSelect = -1;
	for (int row = 0; row < m_infrastructureTable->rowCount(); ++row) {
		QString rowId;
		if (facet == "platforms") {
			if (m_infrastructureTable->item(row, 0) && m_infrastructureTable->item(row, 1))
				rowId = QString::fromStdString(platformSelectionKey(
					m_infrastructureTable->item(row, 0)->text().toStdString(),
					m_infrastructureTable->item(row, 1)->text().toStdString()));
		} else if (m_infrastructureTable->item(row, 0)) {
			rowId = m_infrastructureTable->item(row, 0)->text();
		}
		if (rowId == previousId) {
			rowToSelect = row;
			break;
		}
	}
	if (!resetSelection && rowToSelect < 0 && previousRow >= 0 && m_infrastructureTable->rowCount() > 0)
		rowToSelect = std::min(previousRow, m_infrastructureTable->rowCount() - 1);
	m_infrastructureTable->setCurrentCell(rowToSelect, rowToSelect < 0 ? -1 : 0);
	updateInfrastructureSelection();
	const bool isBlocks = facet == QStringLiteral("blocks");
	if (m_blockTrackFilterCombo)
		m_blockTrackFilterCombo->setVisible(isBlocks);
	if (m_insertBlockButton)
		m_insertBlockButton->setVisible(isBlocks);
	if (m_moveBlockUpButton)
		m_moveBlockUpButton->setVisible(isBlocks);
	if (m_moveBlockDownButton)
		m_moveBlockDownButton->setVisible(isBlocks);
	bool canAdd = m_sceneLoaded && !m_worker;
	if (isBlocks) {
		const std::string selectedTrack = m_blockTrackFilterCombo
			? m_blockTrackFilterCombo->currentData().toString().toStdString()
			: std::string();
		canAdd = canAdd && !selectedTrack.empty()
			&& std::any_of(m_sceneModel.tracks.begin(), m_sceneModel.tracks.end(),
				[&selectedTrack](const SceneTrack& track) { return track.id == selectedTrack; });
	}
	if (m_addInfrastructureButton)
		m_addInfrastructureButton->setEnabled(canAdd);
	if (m_deleteInfrastructureButton)
		m_deleteInfrastructureButton->setEnabled(m_sceneLoaded && !m_worker && rowToSelect >= 0);
	if (m_insertBlockButton)
		m_insertBlockButton->setEnabled(m_sceneLoaded && !m_worker && isBlocks && rowToSelect >= 0);
}

void MainWindow::updateInfrastructureSelection() {
	if (!m_infrastructureTable)
		return;
	const int row = m_infrastructureTable->currentRow();
	const QString facet = m_infrastructureFacetCombo
		? m_infrastructureFacetCombo->currentData().toString()
		: QString();
	if (row >= 0 && facet == "platforms" && m_infrastructureTable->item(row, 0) && m_infrastructureTable->item(row, 1)) {
		m_infrastructureSelectionId = QString::fromStdString(platformSelectionKey(
			m_infrastructureTable->item(row, 0)->text().toStdString(),
			m_infrastructureTable->item(row, 1)->text().toStdString()));
	} else {
		const QTableWidgetItem* item = row >= 0 ? m_infrastructureTable->item(row, 0) : nullptr;
		m_infrastructureSelectionId = item ? item->text() : QString();
	}
	if (m_sceneLoaded && !m_worker && !m_resultsAvailable)
		renderTrackPreview(m_sceneModel);
	if (facet == QStringLiteral("blocks")) {
		const bool hasBlock = row >= 0 && row < static_cast<int>(m_blockRowModelIndices.size());
		if (m_moveBlockUpButton)
			m_moveBlockUpButton->setEnabled(m_sceneLoaded && !m_worker && hasBlock && row > 0);
		if (m_moveBlockDownButton)
			m_moveBlockDownButton->setEnabled(m_sceneLoaded && !m_worker && hasBlock
				&& row + 1 < static_cast<int>(m_blockRowModelIndices.size()));
	}
	refreshRouteSectionPanel();
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
		refreshInfrastructureTable();
	};
	const auto parseNumber = [&](double& target) {
		bool parsed = false;
		const double number = value.toDouble(&parsed);
		if (!parsed || !std::isfinite(number)) {
			refreshInfrastructureTable();
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
			refreshInfrastructureTable();
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
	if (facet == "tracks" && row < static_cast<int>(m_sceneModel.tracks.size()) && column == 0) {
		SceneTrack& track = m_sceneModel.tracks[static_cast<std::size_t>(row)];
		const std::string oldId = track.id;
		const std::string newId = value.toStdString();
		if (oldId != newId) {
			if (!idCanReplaceRow(row, m_sceneModel.tracks, newId)) {
				rejectUnsafeId();
				return;
			}
			track.id = newId;
			sceneRewriteTrackReferences(m_sceneModel, oldId, newId);
			m_infrastructureSelectionId = value;
			changed = true;
		}
	} else if (facet == "nodes" && row < static_cast<int>(m_sceneModel.nodes.size())) {
		SceneNode& node = m_sceneModel.nodes[static_cast<std::size_t>(row)];
		if (column == 0) {
			const std::string oldId = node.id;
			const std::string newId = value.toStdString();
			if (oldId != newId) {
				if (!idCanReplaceRow(row, m_sceneModel.nodes, newId)) {
					rejectUnsafeId();
					return;
				}
				node.id = newId;
				sceneRewriteNodeReferences(m_sceneModel, oldId, newId);
				m_infrastructureSelectionId = value;
				changed = true;
			}
		} else if (column == 1 && node.trackId != value.toStdString()) {
			node.trackId = value.toStdString();
			changed = true;
		} else if (column == 2) {
			parseNumber(node.xKm);
		} else if (column == 3) {
			parseNumber(node.yKm);
		}
	} else if (facet == "arcs" && row < static_cast<int>(m_sceneModel.arcs.size())) {
		SceneArc& arc = m_sceneModel.arcs[static_cast<std::size_t>(row)];
		if (column == 0 && arc.id != value.toStdString()) {
			if (!idCanReplaceRow(row, m_sceneModel.arcs, value.toStdString())) {
				rejectUnsafeId();
				return;
			}
			arc.id = value.toStdString();
			m_infrastructureSelectionId = value;
			changed = true;
		} else if (column == 1 && arc.trackId != value.toStdString()) {
			arc.trackId = value.toStdString();
			changed = true;
		} else if (column == 2 && arc.fromNodeId != value.toStdString()) {
			arc.fromNodeId = value.toStdString();
			changed = true;
		} else if (column == 3 && arc.toNodeId != value.toStdString()) {
			arc.toNodeId = value.toStdString();
			changed = true;
		} else if (column == 4) {
			parseNumber(arc.curvatureRadiusM);
		} else if (column == 5) {
			parseNumber(arc.gradientPercent);
		} else if (column == 6) {
			parseNumber(arc.speedLimitMs);
		}
	} else if (facet == "blocks" && row < static_cast<int>(m_sceneModel.blocks.size())) {
		if (row < 0 || row >= static_cast<int>(m_blockRowModelIndices.size()))
			return;
		const int modelIndex = m_blockRowModelIndices[static_cast<std::size_t>(row)];
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
			m_infrastructureSelectionId = value;
			changed = true;
		} else if (column == 1 && block.trackId != value.toStdString()) {
			block.trackId = value.toStdString();
			refreshBlockTrackFilter();
			if (m_blockTrackFilterCombo) {
				const QSignalBlocker blocker(m_blockTrackFilterCombo);
				m_blockTrackFilterCombo->setCurrentIndex(m_blockTrackFilterCombo->findData(value));
			}
			changed = true;
		} else if (column == 2) {
			parseNumber(block.lengthKm);
		}
	} else if (facet == "connections" && row < static_cast<int>(m_sceneModel.connections.size())) {
		SceneConnection& connection = m_sceneModel.connections[static_cast<std::size_t>(row)];
		if (column == 0 && connection.id != value.toStdString()) {
			if (!idCanReplaceRow(row, m_sceneModel.connections, value.toStdString())) {
				rejectUnsafeId();
				return;
			}
			connection.id = value.toStdString();
			m_infrastructureSelectionId = value;
			changed = true;
		} else if (column == 1 && connection.fromNodeId != value.toStdString()) {
			connection.fromNodeId = value.toStdString();
			changed = true;
		} else if (column == 2 && connection.toNodeId != value.toStdString()) {
			connection.toNodeId = value.toStdString();
			changed = true;
		} else if (column == 3) {
			parseBool(connection.hasSpeedLimit);
		} else if (column == 4) {
			parseNumber(connection.speedLimitMs);
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
				m_infrastructureSelectionId = value;
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
				refreshInfrastructureTable();
				return;
			}
			if (targetStationIndex != stationIndex) {
				const auto& targetPlatforms = m_sceneModel.stations[static_cast<std::size_t>(targetStationIndex)].platforms;
				if (std::any_of(targetPlatforms.begin(), targetPlatforms.end(), [&platform](const ScenePlatform& item) {
						return item.id == platform.id;
					})) {
					statusBar()->showMessage("Platform ID already exists on the target station", 4000);
					refreshInfrastructureTable();
					return;
				}
				const std::string platformId = platform.id;
				const std::string sourceStationId = station.id;
				ScenePlatform moved = platform;
				station.platforms.erase(station.platforms.begin() + platformIndex);
				m_sceneModel.stations[static_cast<std::size_t>(targetStationIndex)].platforms.push_back(std::move(moved));
				sceneMovePlatformReferences(m_sceneModel, platformId, sourceStationId, targetStationId);
				m_infrastructureSelectionId = QString::fromStdString(platformSelectionKey(targetStationId, platformId));
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
				m_infrastructureSelectionId = QString::fromStdString(platformSelectionKey(station.id, newId));
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
			m_infrastructureSelectionId = value;
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
				m_infrastructureSelectionId = value;
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
				m_infrastructureSelectionId = value;
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
		refreshServicePanel();
	if (facet == "signals" || facet == "blocks")
		refreshIncidentPanel();
	refreshInfrastructureTable();
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

ScenePassenger* MainWindow::selectedPassenger() {
	const int row = m_passengerListWidget ? m_passengerListWidget->currentRow() : -1;
	return m_sceneLoaded && row >= 0 && row < static_cast<int>(m_sceneModel.passengers.size())
		? &m_sceneModel.passengers[static_cast<std::size_t>(row)]
		: nullptr;
}

ScenePassengerJourney* MainWindow::selectedPassengerJourney() {
	ScenePassenger* passenger = selectedPassenger();
	const int row = m_passengerJourneyListWidget ? m_passengerJourneyListWidget->currentRow() : -1;
	return passenger && row >= 0 && row < static_cast<int>(passenger->journeys.size())
		? &passenger->journeys[static_cast<std::size_t>(row)]
		: nullptr;
}

ScenePassengerLeg* MainWindow::selectedPassengerLeg() {
	ScenePassengerJourney* journey = selectedPassengerJourney();
	const int row = m_passengerLegListWidget ? m_passengerLegListWidget->currentRow() : -1;
	return journey && row >= 0 && row < static_cast<int>(journey->legs.size())
		? &journey->legs[static_cast<std::size_t>(row)]
		: nullptr;
}

void MainWindow::refreshPassengerPanel() {
	const bool editable = m_sceneLoaded && !m_worker;
	if (m_passengerDock)
		m_passengerDock->setEnabled(editable);
	if (m_passengerListWidget) {
		const int previousRow = m_passengerListWidget->currentRow();
		const QSignalBlocker blocker(m_passengerListWidget);
		m_passengerListWidget->clear();
		if (m_sceneLoaded) {
			for (const auto& passenger : m_sceneModel.passengers)
				m_passengerListWidget->addItem(QString::fromStdString(passenger.id));
		}
		const int count = m_passengerListWidget->count();
		const int selected = count == 0 ? -1
										: (previousRow < 0 ? 0 : std::min(previousRow, count - 1));
		m_passengerListWidget->setCurrentRow(selected);
		m_passengerListWidget->setEnabled(editable);
	}
	if (m_passengerImportResultTable && !m_sceneLoaded)
		m_passengerImportResultTable->setRowCount(0);
	if (m_addPassengerButton)
		m_addPassengerButton->setEnabled(editable);
	if (m_importPassengerButton)
		m_importPassengerButton->setEnabled(editable);
	updatePassengerDetailPanel();
}

void MainWindow::updatePassengerDetailPanel() {
	const int passengerRow = m_passengerListWidget ? m_passengerListWidget->currentRow() : -1;
	ScenePassenger* passenger = selectedPassenger();
	const bool enabled = passenger && m_sceneLoaded && !m_worker;
	if (m_passengerTabs)
		m_passengerTabs->setVisible(passenger != nullptr);
	if (m_passengerIdEdit) {
		const QSignalBlocker blocker(m_passengerIdEdit);
		m_passengerIdEdit->setText(passenger ? QString::fromStdString(passenger->id) : QString());
		m_passengerIdEdit->setEnabled(enabled);
	}
	if (m_passengerJourneyListWidget) {
		const int previousRow = m_passengerJourneyListWidget->currentRow();
		const QSignalBlocker blocker(m_passengerJourneyListWidget);
		m_passengerJourneyListWidget->clear();
		if (passenger) {
			for (const auto& journey : passenger->journeys) {
				QString label = QString::fromStdString(journey.id);
				if (!journey.activity.empty())
					label += QStringLiteral(" | ") + QString::fromStdString(journey.activity);
				m_passengerJourneyListWidget->addItem(label);
			}
		}
		const int count = m_passengerJourneyListWidget->count();
		m_passengerJourneyListWidget->setCurrentRow(count == 0 ? -1
															   : (previousRow < 0 ? 0 : std::min(previousRow, count - 1)));
		m_passengerJourneyListWidget->setEnabled(enabled);
	}
	if (m_addPassengerJourneyButton)
		m_addPassengerJourneyButton->setEnabled(enabled);
	if (m_deletePassengerButton)
		m_deletePassengerButton->setEnabled(enabled);
	if (m_deletePassengerJourneyButton)
		m_deletePassengerJourneyButton->setEnabled(enabled && !m_passengerJourneyListWidget->selectedItems().isEmpty());
	updatePassengerJourneyPanel();

	QStringList messages;
	if (passenger) {
		const QString passengerPrefix = QStringLiteral("passengers[%1]").arg(passengerRow);
		const int journeyRow = m_passengerJourneyListWidget ? m_passengerJourneyListWidget->currentRow() : -1;
		const int legRow = m_passengerLegListWidget ? m_passengerLegListWidget->currentRow() : -1;
		const ScenePassengerJourney* journey = selectedPassengerJourney();
		const ScenePassengerLeg* leg = selectedPassengerLeg();
		const QString journeyPrefix = journeyRow >= 0
			? passengerPrefix + QStringLiteral(".journeys[%1]").arg(journeyRow)
			: QString();
		const QString legPrefix = journey && legRow >= 0
			? journeyPrefix + QStringLiteral(".legs[%1]").arg(legRow)
			: QString();
		const QString passengerId = QString::fromStdString(passenger->id);
		const QString journeyId = journey ? QString::fromStdString(journey->id) : QString();
		const QString legId = leg ? QString::fromStdString(leg->id) : QString();
		for (const auto& diagnostic : m_sceneDiagnostics) {
			const QString path = QString::fromStdString(diagnostic.path);
			const QString itemId = QString::fromStdString(diagnostic.itemId);
			const QString relatedId = QString::fromStdString(diagnostic.relatedId);
			const bool match = (!legPrefix.isEmpty() && path.startsWith(legPrefix))
				|| (!journeyPrefix.isEmpty() && path.startsWith(journeyPrefix))
				|| path.startsWith(passengerPrefix)
				|| itemId == passengerId || itemId == journeyId || itemId == legId
				|| relatedId == passengerId || relatedId == journeyId || relatedId == legId;
			if (match)
				messages << QString::fromStdString(severityLabel(diagnostic.severity)).toUpper()
						+ QStringLiteral(": ") + QString::fromStdString(diagnostic.message);
		}
	}
	if (m_passengerDiagnosticLabel) {
		m_passengerDiagnosticLabel->setText(messages.isEmpty()
				? (passenger ? QStringLiteral("No validation messages for this passenger selection.")
							 : QStringLiteral("Select a passenger or use Add to create one."))
				: messages.join(QStringLiteral("\n")));
		m_passengerDiagnosticLabel->setStyleSheet(messages.isEmpty() ? "color: #5b6871;"
				: std::any_of(messages.begin(), messages.end(), [](const QString& message) {
					  return message.startsWith("ERROR:");
				  })
				? "color: #81352d;"
				: "color: #965c22;");
	}
}

void MainWindow::updatePassengerJourneyPanel() {
	ScenePassengerJourney* journey = selectedPassengerJourney();
	const bool enabled = journey && m_sceneLoaded && !m_worker;
	if (m_passengerLegListWidget) {
		const int previousRow = m_passengerLegListWidget->currentRow();
		const QSignalBlocker blocker(m_passengerLegListWidget);
		m_passengerLegListWidget->clear();
		if (journey) {
			for (const auto& leg : journey->legs)
				m_passengerLegListWidget->addItem(QString::fromStdString(leg.id));
		}
		const int count = m_passengerLegListWidget->count();
		m_passengerLegListWidget->setCurrentRow(count == 0 ? -1
														   : (previousRow < 0 ? 0 : std::min(previousRow, count - 1)));
		m_passengerLegListWidget->setEnabled(enabled);
	}
	if (m_passengerJourneyIdEdit) {
		const QSignalBlocker blocker(m_passengerJourneyIdEdit);
		m_passengerJourneyIdEdit->setText(journey ? QString::fromStdString(journey->id) : QString());
		m_passengerJourneyIdEdit->setEnabled(enabled);
	}
	if (m_passengerJourneyActivityEdit) {
		const QSignalBlocker blocker(m_passengerJourneyActivityEdit);
		m_passengerJourneyActivityEdit->setText(journey ? QString::fromStdString(journey->activity) : QString());
		m_passengerJourneyActivityEdit->setEnabled(enabled);
	}
	if (m_passengerJourneyOriginCombo) {
		const QSignalBlocker blocker(m_passengerJourneyOriginCombo);
		populatePassengerStationCombo(m_passengerJourneyOriginCombo, m_sceneModel,
			journey ? journey->originStationId : std::string(), nullptr, journey != nullptr);
		m_passengerJourneyOriginCombo->setEnabled(enabled);
	}
	if (m_passengerJourneyDestinationCombo) {
		const QSignalBlocker blocker(m_passengerJourneyDestinationCombo);
		populatePassengerStationCombo(m_passengerJourneyDestinationCombo, m_sceneModel,
			journey ? journey->destinationStationId : std::string(), nullptr, journey != nullptr);
		m_passengerJourneyDestinationCombo->setEnabled(enabled);
	}
	const std::array<double, 4> values = journey
		? std::array<double, 4>{journey->plannedDepartureStartSeconds, journey->plannedDepartureEndSeconds,
			  journey->plannedArrivalStartSeconds, journey->plannedArrivalEndSeconds}
		: std::array<double, 4>{0.0, 0.0, 0.0, 0.0};
	for (std::size_t index = 0; index < m_passengerJourneyWindowEdits.size(); ++index) {
		if (!m_passengerJourneyWindowEdits[index])
			continue;
		const QSignalBlocker blocker(m_passengerJourneyWindowEdits[index]);
		m_passengerJourneyWindowEdits[index]->setValue(values[index]);
		m_passengerJourneyWindowEdits[index]->setEnabled(enabled);
	}
	if (m_deletePassengerJourneyButton)
		m_deletePassengerJourneyButton->setEnabled(enabled);
	updatePassengerLegPanel();
}

void MainWindow::updatePassengerLegPanel() {
	ScenePassengerLeg* leg = selectedPassengerLeg();
	const bool enabled = leg && m_sceneLoaded && !m_worker;
	if (m_passengerLegIdEdit) {
		const QSignalBlocker blocker(m_passengerLegIdEdit);
		m_passengerLegIdEdit->setText(leg ? QString::fromStdString(leg->id) : QString());
		m_passengerLegIdEdit->setEnabled(enabled);
	}
	if (m_passengerLegServiceCombo) {
		const QSignalBlocker blocker(m_passengerLegServiceCombo);
		populatePassengerServiceCombo(m_passengerLegServiceCombo, m_sceneModel,
			leg ? leg->serviceId : std::string(), leg != nullptr);
		m_passengerLegServiceCombo->setEnabled(enabled);
	}
	std::set<std::string> stopStations;
	if (leg) {
		const auto service = std::find_if(m_sceneModel.services.begin(), m_sceneModel.services.end(),
			[leg](const SceneService& value) { return value.id == leg->serviceId; });
		if (service != m_sceneModel.services.end())
			for (const auto& stop : service->stops)
				stopStations.insert(stop.stationId);
	}
	const std::set<std::string>* allowed = &stopStations;
	if (m_passengerLegOriginCombo) {
		const QSignalBlocker blocker(m_passengerLegOriginCombo);
		populatePassengerStationCombo(m_passengerLegOriginCombo, m_sceneModel,
			leg ? leg->originStationId : std::string(), allowed, leg != nullptr);
		m_passengerLegOriginCombo->setEnabled(enabled);
	}
	if (m_passengerLegDestinationCombo) {
		const QSignalBlocker blocker(m_passengerLegDestinationCombo);
		populatePassengerStationCombo(m_passengerLegDestinationCombo, m_sceneModel,
			leg ? leg->destinationStationId : std::string(), allowed, leg != nullptr);
		m_passengerLegDestinationCombo->setEnabled(enabled);
	}
	if (m_passengerLegOccurrenceEdit) {
		const int current = leg ? leg->occurrence : 1;
		int count = 1;
		if (leg) {
			const auto service = std::find_if(m_sceneModel.services.begin(), m_sceneModel.services.end(),
				[leg](const SceneService& value) { return value.id == leg->serviceId; });
			if (service != m_sceneModel.services.end())
				count = std::max(1, sceneServiceOccurrenceCount(*service, serviceOccurrenceDuration()));
		}
		const QSignalBlocker blocker(m_passengerLegOccurrenceEdit);
		m_passengerLegOccurrenceEdit->setRange(std::min(0, current), std::max({1, current, count}));
		m_passengerLegOccurrenceEdit->setValue(current);
		m_passengerLegOccurrenceEdit->setEnabled(enabled);
	}
	if (m_deletePassengerLegButton)
		m_deletePassengerLegButton->setEnabled(enabled);
	if (m_movePassengerLegUpButton)
		m_movePassengerLegUpButton->setEnabled(enabled && m_passengerLegListWidget->currentRow() > 0);
	if (m_movePassengerLegDownButton)
		m_movePassengerLegDownButton->setEnabled(enabled
			&& m_passengerLegListWidget->currentRow() + 1 < m_passengerLegListWidget->count());
	if (m_addPassengerLegButton)
		m_addPassengerLegButton->setEnabled(selectedPassengerJourney() && m_sceneLoaded && !m_worker);
}

void MainWindow::addPassenger() {
	if (!m_sceneLoaded || m_worker)
		return;
	ScenePassenger passenger;
	passenger.id = sceneUniquePassengerId(m_sceneModel, "new_passenger");
	m_sceneModel.passengers.push_back(std::move(passenger));
	markSceneDirty();
	refreshPassengerPanel();
	if (m_passengerListWidget)
		m_passengerListWidget->setCurrentRow(static_cast<int>(m_sceneModel.passengers.size()) - 1);
	refreshValidationPanel();
}

void MainWindow::deletePassenger() {
	if (!m_sceneLoaded || m_worker || !m_passengerListWidget)
		return;
	const int row = m_passengerListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.passengers.size()))
		return;
	const std::string id = m_sceneModel.passengers[static_cast<std::size_t>(row)].id;
	if (QMessageBox::question(this, "Delete Passenger",
			QString("Delete passenger '%1' and its journeys?").arg(QString::fromStdString(id)),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
		!= QMessageBox::Yes)
		return;
	m_sceneModel.passengers.erase(m_sceneModel.passengers.begin() + row);
	markSceneDirty();
	// Refresh widgets before validation so a shifted row cannot write stale
	// detail values back into the newly selected passenger.
	refreshPassengerPanel();
	refreshValidationPanel();
}

void MainWindow::addPassengerJourney() {
	ScenePassenger* passenger = selectedPassenger();
	if (!passenger || m_worker)
		return;
	ScenePassengerJourney journey;
	journey.id = sceneUniquePassengerJourneyId(m_sceneModel, passenger->id + ".journey");
	if (!m_sceneModel.stations.empty()) {
		journey.originStationId = m_sceneModel.stations.front().id;
		journey.destinationStationId = m_sceneModel.stations.front().id;
	}
	passenger->journeys.push_back(std::move(journey));
	markSceneDirty();
	refreshPassengerPanel();
	if (m_passengerJourneyListWidget)
		m_passengerJourneyListWidget->setCurrentRow(static_cast<int>(passenger->journeys.size()) - 1);
	refreshValidationPanel();
}

void MainWindow::deletePassengerJourney() {
	ScenePassenger* passenger = selectedPassenger();
	if (!passenger || m_worker || !m_passengerJourneyListWidget)
		return;
	const int row = m_passengerJourneyListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(passenger->journeys.size()))
		return;
	const std::string id = passenger->journeys[static_cast<std::size_t>(row)].id;
	if (QMessageBox::question(this, "Delete Journey",
			QString("Delete journey '%1' and its legs?").arg(QString::fromStdString(id)),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
		!= QMessageBox::Yes)
		return;
	passenger->journeys.erase(passenger->journeys.begin() + row);
	markSceneDirty();
	refreshPassengerPanel();
	refreshValidationPanel();
}

void MainWindow::addPassengerLeg() {
	ScenePassengerJourney* journey = selectedPassengerJourney();
	if (!journey || m_worker)
		return;
	ScenePassengerLeg leg;
	leg.id = sceneUniquePassengerLegId(m_sceneModel, journey->id + ".leg");
	if (!m_sceneModel.services.empty()) {
		const SceneService& service = m_sceneModel.services.front();
		leg.serviceId = service.id;
		if (!service.stops.empty()) {
			leg.originStationId = service.stops.front().stationId;
			leg.destinationStationId = service.stops.size() > 1
				? service.stops[1].stationId
				: service.stops.front().stationId;
		}
	}
	if (leg.originStationId.empty() && !m_sceneModel.stations.empty()) {
		leg.originStationId = m_sceneModel.stations.front().id;
		leg.destinationStationId = m_sceneModel.stations.front().id;
	}
	journey->legs.push_back(std::move(leg));
	markSceneDirty();
	refreshPassengerPanel();
	if (m_passengerLegListWidget)
		m_passengerLegListWidget->setCurrentRow(static_cast<int>(journey->legs.size()) - 1);
	if (m_passengerTabs)
		m_passengerTabs->setCurrentIndex(1);
	refreshValidationPanel();
}

void MainWindow::deletePassengerLeg() {
	ScenePassengerJourney* journey = selectedPassengerJourney();
	if (!journey || m_worker || !m_passengerLegListWidget)
		return;
	const int row = m_passengerLegListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(journey->legs.size()))
		return;
	const std::string id = journey->legs[static_cast<std::size_t>(row)].id;
	if (QMessageBox::question(this, "Delete Leg",
			QString("Delete passenger leg '%1'?").arg(QString::fromStdString(id)),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
		!= QMessageBox::Yes)
		return;
	journey->legs.erase(journey->legs.begin() + row);
	markSceneDirty();
	refreshPassengerPanel();
	refreshValidationPanel();
}

void MainWindow::movePassengerLeg(int offset) {
	ScenePassengerJourney* journey = selectedPassengerJourney();
	if (!journey || m_worker || !m_passengerLegListWidget)
		return;
	const int row = m_passengerLegListWidget->currentRow();
	const int target = row + offset;
	if (row < 0 || target < 0 || target >= static_cast<int>(journey->legs.size()))
		return;
	std::swap(journey->legs[static_cast<std::size_t>(row)], journey->legs[static_cast<std::size_t>(target)]);
	markSceneDirty();
	refreshPassengerPanel();
	if (m_passengerLegListWidget)
		m_passengerLegListWidget->setCurrentRow(target);
	refreshValidationPanel();
}

void MainWindow::commitPassengerIdEdit() {
	if (!m_sceneLoaded || m_worker || !m_passengerListWidget || !m_passengerIdEdit)
		return;
	const int row = m_passengerListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.passengers.size()))
		return;
	const std::string oldId = m_sceneModel.passengers[static_cast<std::size_t>(row)].id;
	const std::string newId = m_passengerIdEdit->text().trimmed().toStdString();
	bool duplicate = false;
	for (int index = 0; index < static_cast<int>(m_sceneModel.passengers.size()); ++index)
		if (index != row && m_sceneModel.passengers[static_cast<std::size_t>(index)].id == newId)
			duplicate = true;
	if (newId.empty() || (newId != oldId && duplicate)) {
		const QSignalBlocker blocker(m_passengerIdEdit);
		m_passengerIdEdit->setText(QString::fromStdString(oldId));
		showBlockingError(this, newId.empty() ? "Invalid Passenger ID" : "Passenger ID already exists",
			newId.empty() ? "Passenger IDs must not be empty." : "Choose a unique passenger ID.", true);
		return;
	}
	if (newId == oldId)
		return;
	m_sceneModel.passengers[static_cast<std::size_t>(row)].id = newId;
	if (auto* item = m_passengerListWidget->item(row)) {
		const QSignalBlocker blocker(m_passengerListWidget);
		item->setText(QString::fromStdString(newId));
	}
	markSceneDirty();
	refreshValidationPanel();
}

void MainWindow::commitPassengerJourneyIdEdit() {
	ScenePassenger* passenger = selectedPassenger();
	ScenePassengerJourney* journey = selectedPassengerJourney();
	if (!passenger || !journey || m_worker || !m_passengerJourneyIdEdit)
		return;
	const std::string oldId = journey->id;
	const std::string newId = m_passengerJourneyIdEdit->text().trimmed().toStdString();
	bool duplicate = false;
	for (const auto& owner : m_sceneModel.passengers)
		for (const auto& candidate : owner.journeys)
			if (&candidate != journey && candidate.id == newId)
				duplicate = true;
	if (newId.empty() || (newId != oldId && duplicate)) {
		const QSignalBlocker blocker(m_passengerJourneyIdEdit);
		m_passengerJourneyIdEdit->setText(QString::fromStdString(oldId));
		showBlockingError(this, newId.empty() ? "Invalid Journey ID" : "Journey ID already exists",
			newId.empty() ? "Journey IDs must not be empty." : "Choose a unique journey ID.", true);
		return;
	}
	if (newId == oldId)
		return;
	journey->id = newId;
	markSceneDirty();
	refreshPassengerPanel();
	refreshValidationPanel();
}

void MainWindow::commitPassengerJourneyActivity() {
	ScenePassengerJourney* journey = selectedPassengerJourney();
	if (!journey || m_worker || !m_passengerJourneyActivityEdit)
		return;
	const std::string value = m_passengerJourneyActivityEdit->text().toStdString();
	if (value == journey->activity)
		return;
	journey->activity = value;
	markSceneDirty();
	refreshPassengerPanel();
	refreshValidationPanel();
}

void MainWindow::commitPassengerJourneyStation(bool origin, const QString& text) {
	Q_UNUSED(text);
	ScenePassengerJourney* journey = selectedPassengerJourney();
	QComboBox* combo = origin ? m_passengerJourneyOriginCombo : m_passengerJourneyDestinationCombo;
	if (!journey || m_worker || !combo)
		return;
	const std::string value = combo->currentData().toString().toStdString();
	std::string& target = origin ? journey->originStationId : journey->destinationStationId;
	if (target == value)
		return;
	target = value;
	markSceneDirty();
	refreshPassengerPanel();
	refreshValidationPanel();
}

void MainWindow::commitPassengerJourneyWindow(int index) {
	ScenePassengerJourney* journey = selectedPassengerJourney();
	if (!journey || m_worker || index < 0 || index >= static_cast<int>(m_passengerJourneyWindowEdits.size())
		|| !m_passengerJourneyWindowEdits[static_cast<std::size_t>(index)])
		return;
	const double value = m_passengerJourneyWindowEdits[static_cast<std::size_t>(index)]->value();
	double* target = index == 0 ? &journey->plannedDepartureStartSeconds
								: (index == 1 ? &journey->plannedDepartureEndSeconds
											  : (index == 2 ? &journey->plannedArrivalStartSeconds : &journey->plannedArrivalEndSeconds));
	if (*target == value)
		return;
	*target = value;
	markSceneDirty();
	refreshValidationPanel();
}

void MainWindow::commitPassengerLegIdEdit() {
	ScenePassengerJourney* journey = selectedPassengerJourney();
	ScenePassengerLeg* leg = selectedPassengerLeg();
	if (!journey || !leg || m_worker || !m_passengerLegIdEdit)
		return;
	const std::string oldId = leg->id;
	const std::string newId = m_passengerLegIdEdit->text().trimmed().toStdString();
	bool duplicate = false;
	for (const auto& passenger : m_sceneModel.passengers)
		for (const auto& owner : passenger.journeys)
			for (const auto& candidate : owner.legs)
				if (&candidate != leg && candidate.id == newId)
					duplicate = true;
	if (newId.empty() || (newId != oldId && duplicate)) {
		const QSignalBlocker blocker(m_passengerLegIdEdit);
		m_passengerLegIdEdit->setText(QString::fromStdString(oldId));
		showBlockingError(this, newId.empty() ? "Invalid Leg ID" : "Leg ID already exists",
			newId.empty() ? "Leg IDs must not be empty." : "Choose a unique leg ID.", true);
		return;
	}
	if (newId == oldId)
		return;
	leg->id = newId;
	markSceneDirty();
	refreshPassengerPanel();
	refreshValidationPanel();
}

void MainWindow::commitPassengerLegStation(bool origin, const QString& text) {
	Q_UNUSED(text);
	ScenePassengerLeg* leg = selectedPassengerLeg();
	QComboBox* combo = origin ? m_passengerLegOriginCombo : m_passengerLegDestinationCombo;
	if (!leg || m_worker || !combo)
		return;
	const std::string value = combo->currentData().toString().toStdString();
	std::string& target = origin ? leg->originStationId : leg->destinationStationId;
	if (target == value)
		return;
	target = value;
	markSceneDirty();
	refreshPassengerPanel();
	refreshValidationPanel();
}

void MainWindow::commitPassengerLegService(const QString& text) {
	Q_UNUSED(text);
	ScenePassengerLeg* leg = selectedPassengerLeg();
	if (!leg || m_worker || !m_passengerLegServiceCombo)
		return;
	const std::string value = m_passengerLegServiceCombo->currentData().toString().toStdString();
	if (leg->serviceId == value)
		return;
	leg->serviceId = value;
	markSceneDirty();
	refreshPassengerPanel();
	refreshValidationPanel();
}

void MainWindow::commitPassengerLegOccurrence() {
	ScenePassengerLeg* leg = selectedPassengerLeg();
	if (!leg || m_worker || !m_passengerLegOccurrenceEdit)
		return;
	m_passengerLegOccurrenceEdit->interpretText();
	const int value = m_passengerLegOccurrenceEdit->value();
	if (leg->occurrence == value)
		return;
	leg->occurrence = value;
	markSceneDirty();
	refreshValidationPanel();
}

void MainWindow::importPassengers() {
	if (!m_sceneLoaded || m_worker)
		return;
	QString sourcePath;
	if (const auto& passengerSource = mainWindowTestHooks().passengerSource)
		sourcePath = passengerSource();
	if (sourcePath.isEmpty()) {
		const QString startDir = m_sceneDir.isEmpty() ? QDir::homePath() : QFileInfo(m_sceneDir).absolutePath();
		sourcePath = QFileDialog::getExistingDirectory(this, "Select Legacy Passenger Folder", startDir);
	}
	if (sourcePath.isEmpty())
		return;
	const ScenePassengerImportResult imported = importLegacyPassengers(sourcePath.toStdString(), m_sceneModel);
	if (m_passengerImportResultTable)
		m_passengerImportResultTable->setRowCount(0);
	const auto addResultRow = [this](const QString& source, int row, const QString& status, const QString& detail) {
		if (!m_passengerImportResultTable)
			return;
		const int target = m_passengerImportResultTable->rowCount();
		m_passengerImportResultTable->insertRow(target);
		m_passengerImportResultTable->setItem(target, 0, new QTableWidgetItem(source));
		m_passengerImportResultTable->setItem(target, 1, new QTableWidgetItem(row > 0 ? QString::number(row) : QStringLiteral("-")));
		m_passengerImportResultTable->setItem(target, 2, new QTableWidgetItem(status));
		m_passengerImportResultTable->setItem(target, 3, new QTableWidgetItem(detail));
	};
	if (!imported.success()) {
		for (const auto& row : imported.rows)
			addResultRow(QFileInfo(QString::fromStdString(row.sourceFile)).fileName(), row.row,
				QStringLiteral("Rejected"), QString::fromStdString(row.context));
		if (imported.rows.empty())
			addResultRow(QFileInfo(sourcePath).fileName(), 0, QStringLiteral("Rejected"),
				QStringLiteral("The exact DAS and RouteChoice passenger pair could not be read"));
		QString message = firstDiagnosticMessage(imported.diagnostics);
		if (message.isEmpty())
			message = QStringLiteral("Passenger import reported errors.");
		showBlockingError(this, "Passenger Import Diagnostics", message, true);
		if (m_passengerImportResultTable)
			m_passengerImportResultTable->resizeColumnsToContents();
		statusBar()->showMessage(QStringLiteral("Passenger import rejected; the scene was not changed"), 7000);
		return;
	}

	std::set<std::string> passengerIds;
	std::set<std::string> journeyIds;
	std::set<std::string> legIds;
	for (const auto& passenger : m_sceneModel.passengers) {
		passengerIds.insert(passenger.id);
		for (const auto& journey : passenger.journeys) {
			journeyIds.insert(journey.id);
			for (const auto& leg : journey.legs)
				legIds.insert(leg.id);
		}
	}
	int acceptedPassengers = 0;
	int rejectedPassengers = 0;
	std::set<std::string> rejectedPassengerIds;
	for (const auto& passenger : imported.passengers) {
		bool collision = passenger.id.empty() || passengerIds.count(passenger.id) > 0;
		std::set<std::string> candidateJourneyIds;
		std::set<std::string> candidateLegIds;
		for (const auto& journey : passenger.journeys) {
			if (journey.id.empty() || journeyIds.count(journey.id) > 0
				|| !candidateJourneyIds.insert(journey.id).second)
				collision = true;
			for (const auto& leg : journey.legs) {
				if (leg.id.empty() || legIds.count(leg.id) > 0
					|| !candidateLegIds.insert(leg.id).second)
					collision = true;
			}
		}
		if (collision) {
			++rejectedPassengers;
			if (!passenger.id.empty())
				rejectedPassengerIds.insert(passenger.id);
			addResultRow(QStringLiteral("%1 (batch)").arg(QFileInfo(sourcePath).fileName()), 0,
				QStringLiteral("Rejected"),
				QStringLiteral("Passenger, journey, or leg ID collides with existing or earlier imported data"));
			continue;
		}
		passengerIds.insert(passenger.id);
		journeyIds.insert(candidateJourneyIds.begin(), candidateJourneyIds.end());
		legIds.insert(candidateLegIds.begin(), candidateLegIds.end());
		m_sceneModel.passengers.push_back(passenger);
		++acceptedPassengers;
	}
	for (const auto& row : imported.rows) {
		const QString source = QFileInfo(QString::fromStdString(row.sourceFile)).fileName();
		const bool rejectedByBatch = rejectedPassengerIds.count(row.passengerId) > 0;
		const QString status = !row.accepted || rejectedByBatch ? QStringLiteral("Rejected")
																: (row.unresolvedReferences ? QStringLiteral("Unresolved") : QStringLiteral("Accepted"));
		const QString detail = rejectedByBatch
			? QStringLiteral("Passenger ID collides with existing or earlier imported data")
			: QString::fromStdString(row.context);
		addResultRow(source, row.row, status, detail);
	}
	if (imported.rows.empty() && imported.diagnostics.empty())
		addResultRow(QFileInfo(sourcePath).fileName(), 0, QStringLiteral("Rejected"),
			QStringLiteral("No passenger source rows were found"));
	for (const auto& report : imported.report)
		m_sceneModel.importReport.push_back(report);
	if (rejectedPassengers > 0) {
		m_sceneModel.importReport.push_back({"passengers.editor", sourcePath.toStdString(),
			acceptedPassengers + rejectedPassengers, acceptedPassengers, rejectedPassengers, 0});
	}
	if (acceptedPassengers > 0 || rejectedPassengers > 0 || !imported.report.empty()) {
		markSceneDirty();
		refreshPassengerPanel();
		refreshValidationPanel();
	}
	if (m_passengerImportResultTable)
		m_passengerImportResultTable->resizeColumnsToContents();
	statusBar()->showMessage(QString("Passenger import: %1 accepted, %2 rejected; unresolved rows remain visible")
								 .arg(acceptedPassengers)
								 .arg(rejectedPassengers),
		7000);
}

void MainWindow::addInfrastructureEntity() {
	if (!m_sceneLoaded || m_worker || !m_infrastructureFacetCombo)
		return;
	const QString facet = m_infrastructureFacetCombo->currentData().toString();
	std::string id;
	int newRow = -1;
	if (facet == "tracks") {
		id = sceneUniqueInfrastructureId(m_sceneModel, "track", facet.toStdString());
		m_sceneModel.tracks.push_back({id});
		newRow = static_cast<int>(m_sceneModel.tracks.size()) - 1;
	} else if (facet == "nodes") {
		id = sceneUniqueInfrastructureId(m_sceneModel, "node", facet.toStdString());
		const std::string trackId = m_sceneModel.tracks.empty() ? std::string() : m_sceneModel.tracks.front().id;
		double x = 0.0;
		for (const auto& node : m_sceneModel.nodes)
			if (node.trackId == trackId && std::isfinite(node.xKm))
				x = std::max(x, node.xKm + 1.0);
		m_sceneModel.nodes.push_back({id, trackId, x, 0.0});
		newRow = static_cast<int>(m_sceneModel.nodes.size()) - 1;
	} else if (facet == "arcs") {
		id = sceneUniqueInfrastructureId(m_sceneModel, "arc", facet.toStdString());
		const std::string trackId = m_sceneModel.tracks.empty() ? std::string() : m_sceneModel.tracks.front().id;
		std::vector<std::string> nodeIds;
		for (const auto& node : m_sceneModel.nodes)
			if (node.trackId == trackId)
				nodeIds.push_back(node.id);
		std::string from;
		std::string to;
		if (nodeIds.size() >= 2) {
			std::size_t index = 0;
			for (const auto& arc : m_sceneModel.arcs)
				if (arc.trackId == trackId && index + 1 < nodeIds.size())
					++index;
			from = nodeIds[index];
			to = nodeIds[std::min(index + 1, nodeIds.size() - 1)];
		}
		m_sceneModel.arcs.push_back({id, trackId, from, to, 0.0, 0.0, 20.0});
		newRow = static_cast<int>(m_sceneModel.arcs.size()) - 1;
	} else if (facet == "blocks") {
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
	} else if (facet == "connections") {
		id = sceneUniqueInfrastructureId(m_sceneModel, "connection", facet.toStdString());
		std::string from;
		std::string to;
		if (m_sceneModel.nodes.size() >= 2) {
			from = m_sceneModel.nodes[0].id;
			to = m_sceneModel.nodes[1].id;
		}
		m_sceneModel.connections.push_back({id, from, to, false, 0.0});
		newRow = static_cast<int>(m_sceneModel.connections.size()) - 1;
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
		m_infrastructureSelectionId = QString::fromStdString(platformSelectionKey(stationId, id));
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
		m_infrastructureSelectionId = QString::fromStdString(id);
	markSceneDirty();
	refreshValidationPanel();
	if (facet == "stations" || facet == "platforms" || facet == "routes")
		refreshServicePanel();
	if (facet == "signals" || facet == "blocks")
		refreshIncidentPanel();
	refreshInfrastructureTable();
	if (facet != QStringLiteral("blocks") && m_infrastructureTable
		&& newRow >= 0 && newRow < m_infrastructureTable->rowCount())
		m_infrastructureTable->setCurrentCell(newRow, 0);
}

void MainWindow::insertBlock() {
	if (!m_sceneLoaded || m_worker || !m_infrastructureTable || !m_infrastructureFacetCombo
		|| m_infrastructureFacetCombo->currentData().toString() != QStringLiteral("blocks"))
		return;
	const int row = m_infrastructureTable->currentRow();
	if (row < 0 || row >= static_cast<int>(m_blockRowModelIndices.size()))
		return;
	const int modelIndex = m_blockRowModelIndices[static_cast<std::size_t>(row)];
	if (modelIndex < 0 || modelIndex >= static_cast<int>(m_sceneModel.blocks.size()))
		return;
	const std::string trackId = m_sceneModel.blocks[static_cast<std::size_t>(modelIndex)].trackId;
	const std::string id = sceneUniqueInfrastructureId(m_sceneModel, "block", "blocks");
	m_sceneModel.blocks.insert(m_sceneModel.blocks.begin() + modelIndex, {id, trackId, 1.0});
	m_infrastructureSelectionId = QString::fromStdString(id);
	markSceneDirty();
	refreshValidationPanel();
	refreshIncidentPanel();
	refreshInfrastructureTable();
}

void MainWindow::moveBlock(int offset) {
	if (!m_sceneLoaded || m_worker || !m_infrastructureTable
		|| !m_infrastructureFacetCombo
		|| m_infrastructureFacetCombo->currentData().toString() != QStringLiteral("blocks"))
		return;
	const int row = m_infrastructureTable->currentRow();
	const int neighborRow = row + offset;
	if (row < 0 || row >= static_cast<int>(m_blockRowModelIndices.size())
		|| neighborRow < 0 || neighborRow >= static_cast<int>(m_blockRowModelIndices.size()))
		return;
	const int modelIndex = m_blockRowModelIndices[static_cast<std::size_t>(row)];
	const int neighbor = m_blockRowModelIndices[static_cast<std::size_t>(neighborRow)];
	if (modelIndex < 0 || modelIndex >= static_cast<int>(m_sceneModel.blocks.size())
		|| neighbor < 0 || neighbor >= static_cast<int>(m_sceneModel.blocks.size()))
		return;
	std::swap(m_sceneModel.blocks[static_cast<std::size_t>(modelIndex)],
		m_sceneModel.blocks[static_cast<std::size_t>(neighbor)]);
	m_infrastructureSelectionId = QString::fromStdString(
		m_sceneModel.blocks[static_cast<std::size_t>(neighbor)].id);
	markSceneDirty();
	refreshValidationPanel();
	refreshIncidentPanel();
	refreshInfrastructureTable();
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
	m_infrastructureSelectionId = QString::fromStdString(m_sceneModel.routes[static_cast<std::size_t>(row)].id);
	markSceneDirty();
	refreshValidationPanel();
	refreshInfrastructureTable();
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
	refreshInfrastructureTable();
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
	refreshInfrastructureTable();
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
	refreshInfrastructureTable();
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
		if (row >= static_cast<int>(m_blockRowModelIndices.size()))
			return;
		modelRow = m_blockRowModelIndices[static_cast<std::size_t>(row)];
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
	if (facet == "tracks" && row < static_cast<int>(m_sceneModel.tracks.size()))
		id = QString::fromStdString(m_sceneModel.tracks[static_cast<std::size_t>(row)].id);
	else if (facet == "nodes" && row < static_cast<int>(m_sceneModel.nodes.size()))
		id = QString::fromStdString(m_sceneModel.nodes[static_cast<std::size_t>(row)].id);
	else if (facet == "arcs" && row < static_cast<int>(m_sceneModel.arcs.size()))
		id = QString::fromStdString(m_sceneModel.arcs[static_cast<std::size_t>(row)].id);
	else if (facet == "blocks" && modelRow < static_cast<int>(m_sceneModel.blocks.size()))
		id = QString::fromStdString(m_sceneModel.blocks[static_cast<std::size_t>(modelRow)].id);
	else if (facet == "connections" && row < static_cast<int>(m_sceneModel.connections.size()))
		id = QString::fromStdString(m_sceneModel.connections[static_cast<std::size_t>(row)].id);
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
	if (facet == "tracks" && row < static_cast<int>(m_sceneModel.tracks.size()))
		m_sceneModel.tracks.erase(m_sceneModel.tracks.begin() + row);
	else if (facet == "nodes" && row < static_cast<int>(m_sceneModel.nodes.size()))
		m_sceneModel.nodes.erase(m_sceneModel.nodes.begin() + row);
	else if (facet == "arcs" && row < static_cast<int>(m_sceneModel.arcs.size()))
		m_sceneModel.arcs.erase(m_sceneModel.arcs.begin() + row);
	else if (facet == "blocks" && modelRow < static_cast<int>(m_sceneModel.blocks.size()))
		m_sceneModel.blocks.erase(m_sceneModel.blocks.begin() + modelRow);
	else if (facet == "connections" && row < static_cast<int>(m_sceneModel.connections.size()))
		m_sceneModel.connections.erase(m_sceneModel.connections.begin() + row);
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
	m_infrastructureSelectionId.clear();
	markSceneDirty();
	refreshValidationPanel();
	if (facet == "stations" || facet == "platforms" || facet == "routes")
		refreshServicePanel();
	if (facet == "signals" || facet == "blocks")
		refreshIncidentPanel();
	refreshInfrastructureTable();
}

void MainWindow::refreshTrainUnitPanel() {
	const bool hasScene = m_sceneLoaded;
	if (m_trainUnitListWidget) {
		const int previousRow = m_trainUnitListWidget->currentRow();
		const QSignalBlocker blocker(m_trainUnitListWidget);
		m_trainUnitListWidget->clear();
		if (hasScene) {
			for (const auto& unit : m_sceneModel.trainUnits)
				m_trainUnitListWidget->addItem(QString::fromStdString(unit.id));
		}
		const int rowCount = m_trainUnitListWidget->count();
		int rowToSelect = -1;
		if (rowCount > 0)
			rowToSelect = previousRow < 0 ? 0 : std::min(previousRow, rowCount - 1);
		m_trainUnitListWidget->setCurrentRow(rowToSelect);
		m_trainUnitListWidget->setEnabled(hasScene);
	}
	if (m_addTrainUnitButton)
		m_addTrainUnitButton->setEnabled(hasScene);
	updateTrainUnitDetailPanel();
}

void MainWindow::updateTrainUnitDetailPanel() {
	const int row = m_trainUnitListWidget ? m_trainUnitListWidget->currentRow() : -1;
	const bool hasSelection = m_sceneLoaded && row >= 0 && row < static_cast<int>(m_sceneModel.trainUnits.size());
	const SceneTrainUnit* unit = hasSelection ? &m_sceneModel.trainUnits[row] : nullptr;

	if (m_trainUnitIdEdit) {
		const QSignalBlocker blocker(m_trainUnitIdEdit);
		m_trainUnitIdEdit->setText(unit ? QString::fromStdString(unit->id) : QString());
		m_trainUnitIdEdit->setEnabled(hasSelection);
	}
	const double values[] = {
		unit ? unit->physical.mass_of_traction_unit_kg : 0.0,
		unit ? unit->physical.mass_of_a_wagon_kg : 0.0,
		unit ? unit->physical.number_of_wagons : 0.0,
		unit ? unit->physical.max_speed_ms : 0.0,
		unit ? unit->physical.max_deceleration_ms2 : 0.0,
		unit ? unit->physical.frontal_area_m2 : 0.0,
		unit ? unit->physical.resistance_coefficient : 0.0,
		unit ? unit->physical.jerk_ms3 : 0.0,
		unit ? unit->physical.length_m : 0.0};
	for (size_t index = 0; index < m_trainUnitPhysicalEdits.size(); ++index) {
		if (!m_trainUnitPhysicalEdits[index])
			continue;
		const QSignalBlocker blocker(m_trainUnitPhysicalEdits[index]);
		m_trainUnitPhysicalEdits[index]->setValue(values[index]);
		m_trainUnitPhysicalEdits[index]->setEnabled(hasSelection);
	}
	if (m_trainUnitSourceDataEdit) {
		const QSignalBlocker blocker(m_trainUnitSourceDataEdit);
		m_trainUnitSourceDataEdit->setText(unit ? QString::fromStdString(unit->sourceDataFile) : QString());
		m_trainUnitSourceDataEdit->setEnabled(hasSelection);
	}
	if (m_trainUnitSourceTractionEdit) {
		const QSignalBlocker blocker(m_trainUnitSourceTractionEdit);
		m_trainUnitSourceTractionEdit->setText(unit ? QString::fromStdString(unit->sourceTractionFile) : QString());
		m_trainUnitSourceTractionEdit->setEnabled(hasSelection);
	}
	updateTrainUnitSourceStatus();
	if (m_plotTrainUnitTractionButton)
		m_plotTrainUnitTractionButton->setEnabled(unit && !unit->tractionCurve.empty());
	if (m_duplicateTrainUnitButton)
		m_duplicateTrainUnitButton->setEnabled(hasSelection);
	if (m_deleteTrainUnitButton)
		m_deleteTrainUnitButton->setEnabled(hasSelection);
	if (m_addTrainUnitTractionButton)
		m_addTrainUnitTractionButton->setEnabled(hasSelection);
	if (m_removeTrainUnitTractionButton)
		m_removeTrainUnitTractionButton->setEnabled(hasSelection
			&& m_trainUnitTractionTable && m_trainUnitTractionTable->currentRow() >= 0);
	refreshTrainUnitTractionTable();
}

void MainWindow::refreshTrainUnitTractionTable() {
	if (!m_trainUnitTractionTable)
		return;
	const int unitRow = m_trainUnitListWidget ? m_trainUnitListWidget->currentRow() : -1;
	const bool hasSelection = m_sceneLoaded && unitRow >= 0 && unitRow < static_cast<int>(m_sceneModel.trainUnits.size());
	const int previousRow = m_trainUnitTractionTable->currentRow();
	const QSignalBlocker tableBlocker(m_trainUnitTractionTable);
	m_trainUnitTractionTable->setRowCount(0);
	if (!hasSelection) {
		m_trainUnitTractionTable->setEnabled(false);
		return;
	}
	const auto& curve = m_sceneModel.trainUnits[unitRow].tractionCurve;
	for (int row = 0; row < static_cast<int>(curve.size()); ++row) {
		m_trainUnitTractionTable->insertRow(row);
		for (int column = 0; column < 5; ++column) {
			auto* edit = new CompactDoubleSpinBox(m_trainUnitTractionTable);
			edit->setRange(std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max());
			edit->setDecimals(std::numeric_limits<double>::max_digits10);
			edit->setSingleStep(0.1);
			edit->setKeyboardTracking(false);
			edit->setValue(curve[row][column]);
			connect(edit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
				[this, row, column](double value) { commitTrainUnitTractionCell(row, column, value); });
			connect(edit, &QDoubleSpinBox::editingFinished, this, [this, row, column, edit]() {
				if (m_trainUnitTractionTable
					&& (m_trainUnitTractionTable->currentRow() != row
						|| m_trainUnitTractionTable->currentColumn() != column)) {
					const QSignalBlocker blocker(edit);
					m_trainUnitTractionTable->setCurrentCell(row, column);
				}
			});
			m_trainUnitTractionTable->setCellWidget(row, column, edit);
		}
	}
	m_trainUnitTractionTable->setEnabled(true);
	if (!curve.empty())
		m_trainUnitTractionTable->setCurrentCell(std::min(previousRow < 0 ? 0 : previousRow,
													 static_cast<int>(curve.size()) - 1),
			0);
	if (m_removeTrainUnitTractionButton)
		m_removeTrainUnitTractionButton->setEnabled(m_trainUnitTractionTable->currentRow() >= 0);
}

void MainWindow::addTrainUnit() {
	if (!m_sceneLoaded)
		return;
	SceneTrainUnit unit;
	unit.id = sceneUniqueTrainUnitId(m_sceneModel, "new_train_unit");
	m_sceneModel.trainUnits.push_back(unit);
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshTrainUnitPanel();
	refreshCompositionPanel();
	if (m_trainUnitListWidget)
		m_trainUnitListWidget->setCurrentRow(static_cast<int>(m_sceneModel.trainUnits.size()) - 1);
}

void MainWindow::duplicateTrainUnit() {
	if (!m_sceneLoaded || !m_trainUnitListWidget)
		return;
	const int row = m_trainUnitListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.trainUnits.size()))
		return;
	SceneTrainUnit duplicate = m_sceneModel.trainUnits[row];
	duplicate.id = sceneUniqueTrainUnitId(m_sceneModel, duplicate.id + "_copy");
	m_sceneModel.trainUnits.insert(m_sceneModel.trainUnits.begin() + row + 1, duplicate);
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshTrainUnitPanel();
	refreshCompositionPanel();
	if (m_trainUnitListWidget)
		m_trainUnitListWidget->setCurrentRow(row + 1);
}

void MainWindow::deleteTrainUnit() {
	if (!m_sceneLoaded || !m_trainUnitListWidget)
		return;
	const int row = m_trainUnitListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.trainUnits.size()))
		return;
	const std::string id = m_sceneModel.trainUnits[row].id;
	const QStringList consumers = directDeleteConsumers(m_sceneModel, QStringLiteral("train_unit"), id);
	if (!consumers.isEmpty()) {
		showBlockingError(this, "Cannot Delete Rolling Stock Unit",
			QString("Cannot delete rolling stock unit '%1' because it is still referenced by:\n• %2\nRemove it from those compositions first.")
				.arg(QString::fromStdString(id), consumers.join("\n• ")),
			true);
		return;
	}
	if (QMessageBox::question(this, "Delete Rolling Stock Unit",
			QString("Delete rolling stock unit '%1'?").arg(QString::fromStdString(id)),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
		!= QMessageBox::Yes)
		return;
	m_trainUnitSourceLinks.erase(id);
	m_sceneModel.trainUnits.erase(m_sceneModel.trainUnits.begin() + row);
	refreshInputTractionDiagrams(id);
	refreshTrainUnitSourceWatches();
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshTrainUnitPanel();
	refreshCompositionPanel();
	refreshValidationPanel();
}

void MainWindow::commitTrainUnitIdEdit() {
	if (!m_sceneLoaded || !m_trainUnitListWidget || !m_trainUnitIdEdit)
		return;
	const int row = m_trainUnitListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.trainUnits.size()))
		return;
	const std::string oldId = m_sceneModel.trainUnits[row].id;
	const std::string newId = m_trainUnitIdEdit->text().trimmed().toStdString();
	if (newId.empty()) {
		const QSignalBlocker blocker(m_trainUnitIdEdit);
		m_trainUnitIdEdit->setText(QString::fromStdString(oldId));
		return;
	}
	if (newId == oldId)
		return;
	for (int index = 0; index < static_cast<int>(m_sceneModel.trainUnits.size()); ++index) {
		if (index != row && m_sceneModel.trainUnits[index].id == newId) {
			const QSignalBlocker blocker(m_trainUnitIdEdit);
			m_trainUnitIdEdit->setText(QString::fromStdString(oldId));
			return;
		}
	}
	const auto sourceLink = m_trainUnitSourceLinks.find(oldId);
	if (sourceLink != m_trainUnitSourceLinks.end()) {
		TrainUnitSourceLink state = std::move(sourceLink->second);
		m_trainUnitSourceLinks.erase(sourceLink);
		m_trainUnitSourceLinks.emplace(newId, std::move(state));
	}
	for (DiagramWindow* window : findChildren<DiagramWindow*>()) {
		if (window && window->property("inputTrainUnitId").toString() == QString::fromStdString(oldId)) {
			window->setProperty("inputTrainUnitId", QString::fromStdString(newId));
			window->setPresentation("Input traction characteristic",
				inputTractionContext(m_sceneModel.trainUnits[row], QString::fromStdString(newId)),
				inputTractionWarning(m_sceneModel.trainUnits[row]));
		}
	}
	m_sceneModel.trainUnits[row].id = newId;
	sceneRewriteTrainUnitReferences(m_sceneModel, oldId, newId);
	if (QListWidgetItem* item = m_trainUnitListWidget->item(row)) {
		const QSignalBlocker blocker(m_trainUnitListWidget);
		item->setText(QString::fromStdString(newId));
	}
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshTrainUnitSourceWatches();
	updateTrainUnitSourceStatus();
	refreshCompositionPanel();
	refreshValidationPanel();
}

void MainWindow::commitTrainUnitSources() {
	if (!m_sceneLoaded || !m_trainUnitListWidget || !m_trainUnitSourceDataEdit
		|| !m_trainUnitSourceTractionEdit)
		return;
	const int row = m_trainUnitListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.trainUnits.size()))
		return;
	SceneTrainUnit& unit = m_sceneModel.trainUnits[static_cast<std::size_t>(row)];
	const std::string sourceDataFile = m_trainUnitSourceDataEdit->text().toStdString();
	const std::string sourceTractionFile = m_trainUnitSourceTractionEdit->text().toStdString();
	if (unit.sourceDataFile == sourceDataFile && unit.sourceTractionFile == sourceTractionFile)
		return;
	unit.sourceDataFile = sourceDataFile;
	unit.sourceTractionFile = sourceTractionFile;
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	updateCompositionUnitButtons();
}

void MainWindow::updateTrainUnitSourceStatus() {
	const int row = m_trainUnitListWidget ? m_trainUnitListWidget->currentRow() : -1;
	const SceneTrainUnit* unit = m_sceneLoaded && row >= 0
			&& row < static_cast<int>(m_sceneModel.trainUnits.size())
		? &m_sceneModel.trainUnits[static_cast<std::size_t>(row)]
		: nullptr;
	const auto update = [unit](QPushButton* linkButton, QPushButton* unlinkButton,
							QPushButton* retryButton, QLabel* statusLabel, const QString& path,
							const QString& status) {
		const bool hasSelection = unit != nullptr;
		const bool linked = hasSelection && !path.isEmpty();
		if (linkButton)
			linkButton->setEnabled(hasSelection);
		if (unlinkButton)
			unlinkButton->setEnabled(linked);
		if (retryButton)
			retryButton->setEnabled(linked);
		if (!statusLabel)
			return;
		if (!hasSelection) {
			statusLabel->setText(QStringLiteral("No unit selected"));
			statusLabel->setToolTip(QString());
		} else if (!linked) {
			statusLabel->setText(QStringLiteral("Unlinked"));
			statusLabel->setToolTip(QString());
		} else {
			QString display = status;
			if (display.isEmpty())
				display = QFileInfo(path).exists()
					? QStringLiteral("Linked: %1").arg(QFileInfo(path).fileName())
					: QStringLiteral("Linked file missing; Retry");
			statusLabel->setText(display);
			statusLabel->setToolTip(path);
		}
	};
	TrainUnitSourceLink* state = nullptr;
	if (unit) {
		const auto it = m_trainUnitSourceLinks.find(unit->id);
		if (it != m_trainUnitSourceLinks.end())
			state = &it->second;
	}
	update(m_linkTrainUnitSourceDataButton, m_unlinkTrainUnitSourceDataButton,
		m_retryTrainUnitSourceDataButton, m_trainUnitSourceDataStatusLabel,
		state ? state->dataPath : QString(), state ? state->dataStatus : QString());
	update(m_linkTrainUnitSourceTractionButton, m_unlinkTrainUnitSourceTractionButton,
		m_retryTrainUnitSourceTractionButton, m_trainUnitSourceTractionStatusLabel,
		state ? state->tractionPath : QString(), state ? state->tractionStatus : QString());
}

void MainWindow::refreshTrainUnitSourceWatches() {
	if (!m_trainUnitSourceWatcher)
		return;
	QSet<QString> files;
	QSet<QString> directories;
	for (const auto& entry : m_trainUnitSourceLinks) {
		const TrainUnitSourceLink& state = entry.second;
		for (const QString& path : {state.dataPath, state.tractionPath}) {
			if (path.isEmpty())
				continue;
			if (QFileInfo(path).isFile())
				files.insert(path);
			const QString parent = QFileInfo(path).absolutePath();
			if (!parent.isEmpty())
				directories.insert(parent);
		}
	}
	const auto reconcile = [this](const QStringList& watched, const QSet<QString>& wanted) {
		for (const QString& path : watched)
			if (!wanted.contains(path)) m_trainUnitSourceWatcher->removePath(path);
		for (const QString& path : wanted)
			if (!watched.contains(path)) m_trainUnitSourceWatcher->addPath(path);
	};
	reconcile(m_trainUnitSourceWatcher->files(), files);
	reconcile(m_trainUnitSourceWatcher->directories(), directories);
}

void MainWindow::clearTrainUnitSourceLinks() {
	if (m_trainUnitSourceDebounceTimer)
		m_trainUnitSourceDebounceTimer->stop();
	m_pendingTrainUnitSourcePaths.clear();
	m_trainUnitSourceLinks.clear();
	if (m_trainUnitSourceWatcher) {
		const QStringList watched = m_trainUnitSourceWatcher->files()
			+ m_trainUnitSourceWatcher->directories();
		if (!watched.isEmpty())
			m_trainUnitSourceWatcher->removePaths(watched);
	}
	for (DiagramWindow* window : findChildren<DiagramWindow*>()) {
		if (window && window->property("inputTrainUnitId").isValid())
			window->close();
	}
	updateTrainUnitSourceStatus();
}

void MainWindow::linkTrainUnitSource(bool traction) {
	if (!m_sceneLoaded || !m_trainUnitListWidget)
		return;
	const int row = m_trainUnitListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.trainUnits.size()))
		return;
	commitPendingEditorValues();
	const SceneTrainUnit unit = m_sceneModel.trainUnits[static_cast<std::size_t>(row)];
	const quint64 sceneRevision = m_sceneRevision;
	const std::string unitId = unit.id;
	const auto it = m_trainUnitSourceLinks.find(unitId);
	const QString oldPath = it == m_trainUnitSourceLinks.end() ? QString()
															   : (traction ? it->second.tractionPath : it->second.dataPath);
	const QString selected = QFileDialog::getOpenFileName(this,
		traction ? QStringLiteral("Link traction source") : QStringLiteral("Link parameter source"),
		oldPath.isEmpty() ? QDir::homePath() : QFileInfo(oldPath).absolutePath());
	if (selected.isEmpty() || !m_sceneLoaded || m_sceneRevision != sceneRevision)
		return;
	const QString path = absoluteSourcePath(selected);
	TrainUnitSourceLink& state = m_trainUnitSourceLinks[unitId];
	++state.generation;
	QString* linkedPath = traction ? &state.tractionPath : &state.dataPath;
	QString* signature = traction ? &state.tractionSignature : &state.dataSignature;
	QString* status = traction ? &state.tractionStatus : &state.dataStatus;
	if (*linkedPath != path) {
		signature->clear();
		status->clear();
	}
	if (traction)
		state.acceptedTraction = unit.tractionCurve;
	else
		state.acceptedPhysical = unit.physical;
	*linkedPath = path;
	if (traction) {
		state.tractionDeferred = m_worker != nullptr;
		*status = state.tractionDeferred
			? QStringLiteral("Linked; pending reload after run")
			: QStringLiteral("Linked; waiting for reload");
	} else {
		state.dataDeferred = m_worker != nullptr;
		*status = state.dataDeferred
			? QStringLiteral("Linked; pending reload after run")
			: QStringLiteral("Linked; waiting for reload");
	}
	refreshTrainUnitSourceWatches();
	m_pendingTrainUnitSourcePaths.insert(path);
	if (m_trainUnitSourceDebounceTimer)
		m_trainUnitSourceDebounceTimer->start();
	updateTrainUnitSourceStatus();
}

void MainWindow::unlinkTrainUnitSource(bool traction) {
	if (!m_sceneLoaded || !m_trainUnitListWidget)
		return;
	const int row = m_trainUnitListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.trainUnits.size()))
		return;
	const std::string unitId = m_sceneModel.trainUnits[static_cast<std::size_t>(row)].id;
	const auto it = m_trainUnitSourceLinks.find(unitId);
	if (it == m_trainUnitSourceLinks.end())
		return;
	if (traction) {
		++it->second.generation;
		it->second.tractionPath.clear();
		it->second.tractionSignature.clear();
		it->second.acceptedTraction.clear();
		it->second.tractionStatus.clear();
		it->second.tractionDeferred = false;
	} else {
		++it->second.generation;
		it->second.dataPath.clear();
		it->second.dataSignature.clear();
		it->second.acceptedPhysical.reset();
		it->second.dataStatus.clear();
		it->second.dataDeferred = false;
	}
	if (it->second.dataPath.isEmpty() && it->second.tractionPath.isEmpty())
		m_trainUnitSourceLinks.erase(it);
	refreshTrainUnitSourceWatches();
	updateTrainUnitSourceStatus();
}

void MainWindow::retryTrainUnitSource(bool traction) {
	if (!m_sceneLoaded || !m_trainUnitListWidget)
		return;
	const int row = m_trainUnitListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.trainUnits.size()))
		return;
	const std::string unitId = m_sceneModel.trainUnits[static_cast<std::size_t>(row)].id;
	const auto it = m_trainUnitSourceLinks.find(unitId);
	if (it == m_trainUnitSourceLinks.end())
		return;
	const QString path = traction ? it->second.tractionPath : it->second.dataPath;
	if (path.isEmpty())
		return;
	if (traction) {
		it->second.tractionSignature.clear();
		it->second.tractionDeferred = m_worker != nullptr;
		it->second.tractionStatus = it->second.tractionDeferred
			? QStringLiteral("Retry pending after run")
			: QStringLiteral("Retrying");
	} else {
		it->second.dataSignature.clear();
		it->second.dataDeferred = m_worker != nullptr;
		it->second.dataStatus = it->second.dataDeferred
			? QStringLiteral("Retry pending after run")
			: QStringLiteral("Retrying");
	}
	refreshTrainUnitSourceWatches();
	m_pendingTrainUnitSourcePaths.insert(path);
	if (m_trainUnitSourceDebounceTimer)
		m_trainUnitSourceDebounceTimer->start();
	updateTrainUnitSourceStatus();
}

void MainWindow::scheduleTrainUnitSourceChange(const QString& changedPath) {
	const QString path = absoluteSourcePath(changedPath);
	for (auto& entry : m_trainUnitSourceLinks) {
		TrainUnitSourceLink& state = entry.second;
		const auto consider = [this, &path](const QString& linkedPath, bool traction,
								  TrainUnitSourceLink& current) {
			if (linkedPath.isEmpty())
				return;
			const QString parent = QFileInfo(linkedPath).absolutePath();
			if (path != linkedPath && path != parent)
				return;
			m_pendingTrainUnitSourcePaths.insert(linkedPath);
			if (m_worker && sourceFileSignature(linkedPath) != (traction ? current.tractionSignature : current.dataSignature)) {
				if (traction) {
					current.tractionDeferred = true;
					current.tractionStatus = QStringLiteral("Changed during run; pending reload");
				} else {
					current.dataDeferred = true;
					current.dataStatus = QStringLiteral("Changed during run; pending reload");
				}
			}
		};
		consider(state.dataPath, false, state);
		consider(state.tractionPath, true, state);
	}
	if (m_pendingTrainUnitSourcePaths.isEmpty())
		return;
	if (m_worker)
		updateTrainUnitSourceStatus();
	if (m_trainUnitSourceDebounceTimer)
		m_trainUnitSourceDebounceTimer->start();
}

void MainWindow::processTrainUnitSourceChanges() {
	if (m_processingTrainUnitSourceChanges || m_pendingTrainUnitSourcePaths.isEmpty() || m_worker)
		return;
	if (m_trainUnitSourceDebounceTimer)
		m_trainUnitSourceDebounceTimer->stop();
	QScopedValueRollback<bool> processing(m_processingTrainUnitSourceChanges, true);
	const QSet<QString> pending = m_pendingTrainUnitSourcePaths;
	m_pendingTrainUnitSourcePaths.clear();
	std::vector<std::pair<QString, bool>> associations;
	for (const auto& entry : m_trainUnitSourceLinks) {
		for (const auto& association : std::array<std::pair<QString, bool>, 2>{
				 std::make_pair(entry.second.dataPath, false), std::make_pair(entry.second.tractionPath, true)}) {
			if (!association.first.isEmpty() && pending.contains(association.first))
				associations.push_back(association);
		}
	}
	QSet<QString> processed;
	for (const auto& association : associations) {
		const QString key = QString(association.second ? "traction\n" : "physical\n") + association.first;
		if (processed.contains(key))
			continue;
		processed.insert(key);
		processTrainUnitSourceFile(association.first, association.second);
	}
	if (!m_pendingTrainUnitSourcePaths.isEmpty() && m_trainUnitSourceDebounceTimer)
		m_trainUnitSourceDebounceTimer->start();
}

void MainWindow::processTrainUnitSourceFile(const QString& path, bool traction) {
	if (!m_sceneLoaded || m_worker)
		return;
	commitPendingEditorValues();
	std::vector<std::string> affected;
	for (const auto& entry : m_trainUnitSourceLinks) {
		const QString linkedPath = traction ? entry.second.tractionPath : entry.second.dataPath;
		if (linkedPath == path)
			affected.push_back(entry.first);
	}
	if (affected.empty()) {
		refreshTrainUnitSourceWatches();
		return;
	}
	const QString signature = sourceFileSignature(path);
	bool needsProcessing = false;
	for (const std::string& unitId : affected) {
		const auto it = m_trainUnitSourceLinks.find(unitId);
		if (it == m_trainUnitSourceLinks.end())
			continue;
		const QString handled = traction ? it->second.tractionSignature : it->second.dataSignature;
		needsProcessing = needsProcessing || handled != signature;
	}
	if (!needsProcessing) {
		for (const std::string& unitId : affected) {
			auto& link = m_trainUnitSourceLinks.at(unitId);
			bool& deferred = traction ? link.tractionDeferred : link.dataDeferred;
			if (deferred) {
				(traction ? link.tractionStatus : link.dataStatus) = QStringLiteral("Up to date");
				deferred = false;
			}
		}
		refreshTrainUnitSourceWatches();
		updateTrainUnitSourceStatus();
		return;
	}

	SceneTrainPhysicalSourceResult physicalResult;
	SceneTrainTractionSourceResult tractionResult;
	if (traction)
		tractionResult = parseTrainTractionSourceFile(path.toStdString());
	else
		physicalResult = parseTrainPhysicalSourceFile(path.toStdString());
	const bool parsed = traction ? tractionResult.success() : physicalResult.success();
	const QString error = traction ? QString::fromStdString(tractionResult.error)
								   : QString::fromStdString(physicalResult.error);
	if (!parsed) {
		for (const std::string& unitId : affected) {
			auto it = m_trainUnitSourceLinks.find(unitId);
			if (it == m_trainUnitSourceLinks.end()
				|| (traction ? it->second.tractionPath : it->second.dataPath) != path)
				continue;
			if (traction) {
				it->second.tractionSignature = signature;
				it->second.tractionStatus = QStringLiteral("Rejected: %1; Retry available").arg(error);
				it->second.tractionDeferred = false;
			} else {
				it->second.dataSignature = signature;
				it->second.dataStatus = QStringLiteral("Rejected: %1; Retry available").arg(error);
				it->second.dataDeferred = false;
			}
		}
		refreshTrainUnitSourceWatches();
		updateTrainUnitSourceStatus();
		statusBar()->showMessage(QString("Source reload rejected: %1").arg(error), 8000);
		return;
	}

	QStringList conflicts;
	QStringList affectedNames;
	std::map<std::string, quint64> generations;
	bool deferredChange = false;
	for (const std::string& unitId : affected) {
		const auto it = m_trainUnitSourceLinks.find(unitId);
		if (it == m_trainUnitSourceLinks.end()
			|| (traction ? it->second.tractionPath : it->second.dataPath) != path)
			continue;
		affectedNames << QString::fromStdString(unitId);
		generations.emplace(unitId, it->second.generation);
		deferredChange = deferredChange || (traction ? it->second.tractionDeferred : it->second.dataDeferred);
		const SceneTrainUnit* unit = sceneTrainUnitById(m_sceneModel, unitId);
		if (!unit)
			continue;
		const bool localChanged = traction
			? (unit->tractionCurve != it->second.acceptedTraction)
			: (it->second.acceptedPhysical && !sameTrainPhysical(unit->physical, *it->second.acceptedPhysical));
		const bool candidateChanged = traction
			? unit->tractionCurve != tractionResult.tractionCurve
			: !sameTrainPhysical(unit->physical, physicalResult.physical);
		if (localChanged && candidateChanged)
			conflicts << QString::fromStdString(unitId);
	}
	if (affectedNames.isEmpty()) {
		refreshTrainUnitSourceWatches();
		return;
	}
	const quint64 sceneRevisionAtDecision = m_sceneRevision;
	bool reload = true;
	if (!conflicts.isEmpty() || deferredChange) {
		QMessageBox dialog(this);
		dialog.setIcon(QMessageBox::Question);
		dialog.setWindowTitle("Linked source changed");
		dialog.setText(QString("%1 changed for %2.")
				.arg(traction ? QStringLiteral("The traction source") : QStringLiteral("The parameter source"),
					affectedNames.join(", ")));
		dialog.setInformativeText(conflicts.isEmpty()
				? QStringLiteral("The source changed during a run. Reload the external values or keep local edits?")
				: QStringLiteral("Local edits conflict in %1. Reload the external values or keep local edits?")
					  .arg(conflicts.join(", ")));
		QPushButton* reloadButton = dialog.addButton("Reload", QMessageBox::AcceptRole);
		QPushButton* keepButton = dialog.addButton("Keep local", QMessageBox::RejectRole);
		dialog.setDefaultButton(keepButton);
		dialog.exec();
		reload = dialog.clickedButton() == reloadButton;
	}
	const auto requeueCurrentAssociations = [this, &path, &affected, traction]() {
		for (const auto& entry : m_trainUnitSourceLinks) {
			const QString currentPath = traction ? entry.second.tractionPath : entry.second.dataPath;
			if (currentPath == path
				|| std::find(affected.begin(), affected.end(), entry.first) != affected.end()) {
				if (!currentPath.isEmpty())
					m_pendingTrainUnitSourcePaths.insert(currentPath);
			}
		}
		refreshTrainUnitSourceWatches();
	};
	bool staleDecision = !m_sceneLoaded || m_worker || m_sceneRevision != sceneRevisionAtDecision;
	if (!staleDecision) {
		for (const std::string& unitId : affected) {
			const auto it = m_trainUnitSourceLinks.find(unitId);
			const auto generation = generations.find(unitId);
			if (it == m_trainUnitSourceLinks.end() || generation == generations.end()
				|| (traction ? it->second.tractionPath : it->second.dataPath) != path
				|| it->second.generation != generation->second) {
				staleDecision = true;
				break;
			}
		}
	}
	if (staleDecision) {
		requeueCurrentAssociations();
		updateTrainUnitSourceStatus();
		return;
	}
	if (!reload) {
		for (const std::string& unitId : affected) {
			auto it = m_trainUnitSourceLinks.find(unitId);
			if (it == m_trainUnitSourceLinks.end()
				|| (traction ? it->second.tractionPath : it->second.dataPath) != path)
				continue;
			if (traction) {
				it->second.tractionSignature = signature;
				it->second.tractionStatus = QStringLiteral("Kept local; Retry available");
				it->second.tractionDeferred = false;
			} else {
				it->second.dataSignature = signature;
				it->second.dataStatus = QStringLiteral("Kept local; Retry available");
				it->second.dataDeferred = false;
			}
		}
		refreshTrainUnitSourceWatches();
		updateTrainUnitSourceStatus();
		return;
	}

	bool changed = false;
	std::set<std::string> changedTractionUnits;
	for (const std::string& unitId : affected) {
		auto linkIt = m_trainUnitSourceLinks.find(unitId);
		if (linkIt == m_trainUnitSourceLinks.end())
			continue;
		SceneTrainUnit* unit = nullptr;
		for (auto& candidate : m_sceneModel.trainUnits) {
			if (candidate.id == unitId) {
				unit = &candidate;
				break;
			}
		}
		if (!unit)
			continue;
		if (traction) {
			const bool unitChanged = unit->tractionCurve != tractionResult.tractionCurve;
			if (unitChanged) {
				unit->tractionCurve = tractionResult.tractionCurve;
				changed = true;
				changedTractionUnits.insert(unitId);
			}
			linkIt->second.acceptedTraction = tractionResult.tractionCurve;
			linkIt->second.tractionSignature = signature;
			linkIt->second.tractionStatus = unitChanged ? QStringLiteral("Reloaded") : QStringLiteral("Up to date");
			linkIt->second.tractionDeferred = false;
		} else {
			const bool unitChanged = !sameTrainPhysical(unit->physical, physicalResult.physical);
			if (unitChanged) {
				unit->physical = physicalResult.physical;
				unit->hasPhysical = true;
				changed = true;
			}
			linkIt->second.acceptedPhysical = physicalResult.physical;
			linkIt->second.dataSignature = signature;
			linkIt->second.dataStatus = unitChanged ? QStringLiteral("Reloaded") : QStringLiteral("Up to date");
			linkIt->second.dataDeferred = false;
		}
	}
	if (changed)
		markSceneDirty();
	refreshTrainUnitPanel();
	refreshCompositionPanel();
	refreshValidationPanel();
	for (const std::string& unitId : changedTractionUnits)
		refreshInputTractionDiagrams(unitId);
	refreshTrainUnitSourceWatches();
	updateTrainUnitSourceStatus();
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

void MainWindow::commitTrainUnitPhysical(int fieldIndex) {
	if (!m_sceneLoaded || !m_trainUnitListWidget || fieldIndex < 0 || fieldIndex >= 9)
		return;
	const int row = m_trainUnitListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.trainUnits.size())
		|| !m_trainUnitPhysicalEdits[static_cast<size_t>(fieldIndex)])
		return;
	SceneTrainPhysical& physical = m_sceneModel.trainUnits[row].physical;
	double* values[] = {
		&physical.mass_of_traction_unit_kg, &physical.mass_of_a_wagon_kg, &physical.number_of_wagons,
		&physical.max_speed_ms, &physical.max_deceleration_ms2, &physical.frontal_area_m2,
		&physical.resistance_coefficient, &physical.jerk_ms3, &physical.length_m};
	const double value = m_trainUnitPhysicalEdits[static_cast<size_t>(fieldIndex)]->value();
	if (*values[fieldIndex] == value && m_sceneModel.trainUnits[row].hasPhysical)
		return;
	*values[fieldIndex] = value;
	m_sceneModel.trainUnits[row].hasPhysical = true;
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::addTrainUnitTractionRow() {
	if (!m_sceneLoaded || !m_trainUnitListWidget)
		return;
	const int unitRow = m_trainUnitListWidget->currentRow();
	if (unitRow < 0 || unitRow >= static_cast<int>(m_sceneModel.trainUnits.size()))
		return;
	std::array<double, 5> row = {{0.0, 1.0, 0.0, 0.0, 0.0}};
	const auto& curve = m_sceneModel.trainUnits[unitRow].tractionCurve;
	if (!curve.empty()) {
		row[0] = curve.back()[1];
		row[1] = row[0] + 1.0;
	}
	m_sceneModel.trainUnits[unitRow].tractionCurve.push_back(row);
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshTrainUnitTractionTable();
	if (m_trainUnitTractionTable)
		m_trainUnitTractionTable->setCurrentCell(m_trainUnitTractionTable->rowCount() - 1, 0);
}

void MainWindow::removeTrainUnitTractionRow() {
	if (!m_sceneLoaded || !m_trainUnitListWidget || !m_trainUnitTractionTable)
		return;
	const int unitRow = m_trainUnitListWidget->currentRow();
	const int curveRow = m_trainUnitTractionTable->currentRow();
	if (unitRow < 0 || unitRow >= static_cast<int>(m_sceneModel.trainUnits.size())
		|| curveRow < 0 || curveRow >= static_cast<int>(m_sceneModel.trainUnits[unitRow].tractionCurve.size()))
		return;
	m_sceneModel.trainUnits[unitRow].tractionCurve.erase(
		m_sceneModel.trainUnits[unitRow].tractionCurve.begin() + curveRow);
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	updateTrainUnitDetailPanel();
}

void MainWindow::commitTrainUnitTractionCell(int row, int column, double value) {
	if (!m_sceneLoaded || !m_trainUnitListWidget || row < 0)
		return;
	const int unitRow = m_trainUnitListWidget->currentRow();
	if (unitRow < 0 || unitRow >= static_cast<int>(m_sceneModel.trainUnits.size()) || column < 0 || column >= 5)
		return;
	std::vector<std::array<double, 5>>& curve = m_sceneModel.trainUnits[unitRow].tractionCurve;
	if (row >= static_cast<int>(curve.size()) || curve[row][column] == value)
		return;
	curve[row][column] = value;
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
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

	// resolve the selected unit and surface its source files, association state,
	// and whether a traction curve can be plotted
	const SceneTrainUnit* unit = nullptr;
	if (hasUnitSelection && m_compositionUnitsListWidget->item(unitRow))
		unit = sceneTrainUnitById(m_sceneModel, m_compositionUnitsListWidget->item(unitRow)->text().toStdString());

	const auto sourceText = [](const std::string& value) {
		return value.empty() ? QStringLiteral("(none)") : QString::fromStdString(value);
	};
	if (m_compositionUnitSourceDataLabel)
		m_compositionUnitSourceDataLabel->setText(unit ? sourceText(unit->sourceDataFile) : QStringLiteral("(none)"));
	if (m_compositionUnitSourceTractionLabel)
		m_compositionUnitSourceTractionLabel->setText(unit ? sourceText(unit->sourceTractionFile) : QStringLiteral("(none)"));

	const bool canPlot = unit && !unit->tractionCurve.empty();
	if (m_plotTractionButton)
		m_plotTractionButton->setEnabled(canPlot);
	if (m_compositionUnitWarningLabel) {
		QString warning;
		if (hasUnitSelection) {
			warning = QString::fromStdString(tractionAssociationWarning(
				unit != nullptr,
				unit && !unit->tractionCurve.empty()));
		}
		m_compositionUnitWarningLabel->setText(warning);
	}
}

void MainWindow::plotSelectedCompositionUnitTraction() {
	int unitRow = m_compositionUnitsListWidget ? m_compositionUnitsListWidget->currentRow() : -1;
	if (unitRow < 0 || !m_compositionUnitsListWidget->item(unitRow))
		return;
	const std::string unitId = m_compositionUnitsListWidget->item(unitRow)->text().toStdString();
	const SceneTrainUnit* unit = sceneTrainUnitById(m_sceneModel, unitId);
	if (!unit || unit->tractionCurve.empty()) {
		QMessageBox::information(this, "No Traction Data",
			"The selected unit has no traction curve to plot.");
		return;
	}
	plotTrainUnitTraction(*unit);
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

void MainWindow::addComposition() {
	if (!m_sceneLoaded)
		return;

	SceneComposition composition;
	composition.id = sceneUniqueCompositionId(m_sceneModel, "new_composition");
	m_sceneModel.compositions.push_back(composition);

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshCompositionPanel();
	updateServiceDetailPanel();
	refreshValidationPanel();

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
	duplicate.id = sceneUniqueCompositionId(m_sceneModel, duplicate.id + "_copy");
	m_sceneModel.compositions.insert(m_sceneModel.compositions.begin() + row + 1, duplicate);

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshCompositionPanel();
	updateServiceDetailPanel();
	refreshValidationPanel();

	if (m_compositionListWidget)
		m_compositionListWidget->setCurrentRow(row + 1);
}

void MainWindow::deleteComposition() {
	if (!m_sceneLoaded || !m_compositionListWidget)
		return;
	int row = m_compositionListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.compositions.size()))
		return;

	const std::string id = m_sceneModel.compositions[row].id;
	const QStringList consumers = directDeleteConsumers(m_sceneModel, QStringLiteral("composition"), id);
	if (!consumers.isEmpty()) {
		showBlockingError(this, "Cannot Delete Composition",
			QString("Cannot delete composition '%1' because it is still referenced by:\n• %2\nReassign those services first.")
				.arg(QString::fromStdString(id), consumers.join("\n• ")),
			true);
		return;
	}
	auto response = QMessageBox::question(this,
		"Delete Composition",
		QString("Delete composition '%1'?").arg(QString::fromStdString(id)),
		QMessageBox::Yes | QMessageBox::No,
		QMessageBox::No);
	if (response != QMessageBox::Yes)
		return;

	m_sceneModel.compositions.erase(m_sceneModel.compositions.begin() + row);

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshCompositionPanel();
	updateServiceDetailPanel();
	refreshValidationPanel();
}

void MainWindow::commitCompositionIdEdit() {
	if (!m_sceneLoaded || !m_compositionListWidget || !m_compositionIdEdit)
		return;
	const int row = m_compositionListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.compositions.size()))
		return;

	const std::string oldId = m_sceneModel.compositions[row].id;
	const std::string newId = m_compositionIdEdit->text().trimmed().toStdString();
	if (newId == oldId)
		return;
	const bool duplicate = newId.empty() || sceneUniqueCompositionId(m_sceneModel, newId) != newId;
	if (duplicate) {
		m_compositionIdEdit->setText(QString::fromStdString(oldId));
		return;
	}

	m_sceneModel.compositions[row].id = newId;
	sceneRewriteCompositionReferences(m_sceneModel, oldId, newId);
	updateServiceDetailPanel();

	// update the list row label in place instead of rebuilding the panel, so a
	// focus-out that lands on another control keeps that pending click intact
	if (QListWidgetItem* item = m_compositionListWidget->item(row)) {
		const QSignalBlocker blocker(m_compositionListWidget);
		item->setText(QString::fromStdString(newId));
	}

	markSceneDirty();
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
		QMessageBox::information(this, "Add Rolling Stock Unit", "This scene has no rolling stock units defined.");
		return;
	}

	QStringList unitIds;
	for (const auto& unit : m_sceneModel.trainUnits)
		unitIds << QString::fromStdString(unit.id);

	bool ok = false;
	QString chosen = QInputDialog::getItem(this, "Add Rolling Stock Unit", "Rolling stock unit:", unitIds, 0, false, &ok);
	if (!ok || chosen.isEmpty())
		return;

	m_sceneModel.compositions[row].units.push_back(chosen.toStdString());

	markSceneDirty();
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

	markSceneDirty();
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

	markSceneDirty();
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

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshCompositionPanel();

	if (m_compositionUnitsListWidget)
		m_compositionUnitsListWidget->setCurrentRow(unitRow + 1);
}

void MainWindow::refreshServicePanel() {
	bool hasScene = m_sceneLoaded;
	if (m_serviceDock)
		m_serviceDock->setEnabled(hasScene && !m_worker);

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
		m_serviceListWidget->setEnabled(hasScene && !m_worker);
	}

	if (m_addServiceButton)
		m_addServiceButton->setEnabled(hasScene && !m_worker);

	updateServiceDetailPanel();
	refreshServiceOccurrencePreview();
}

void MainWindow::updateServiceDetailPanel() {
	int row = m_serviceListWidget ? m_serviceListWidget->currentRow() : -1;
	bool hasSelection = m_sceneLoaded && row >= 0 && row < static_cast<int>(m_sceneModel.services.size());
	const bool editorAvailable = hasSelection && !m_worker;

	if (m_serviceIdEdit) {
		const QSignalBlocker blocker(m_serviceIdEdit);
		m_serviceIdEdit->setText(hasSelection ? QString::fromStdString(m_sceneModel.services[row].id) : QString());
		m_serviceIdEdit->setEnabled(editorAvailable);
	}

	if (m_serviceOperatingCodeEdit) {
		const QSignalBlocker blocker(m_serviceOperatingCodeEdit);
		m_serviceOperatingCodeEdit->setText(hasSelection ? QString::fromStdString(m_sceneModel.services[row].operatingCode) : QString());
		m_serviceOperatingCodeEdit->setEnabled(editorAvailable);
	}

	if (m_serviceCategoryCombo) {
		const QSignalBlocker blocker(m_serviceCategoryCombo);
		m_serviceCategoryCombo->clear();
		m_serviceCategoryCombo->addItem("No category", QString());
		for (const QString& category : {QStringLiteral("Intercity"), QStringLiteral("Regional"),
				 QStringLiteral("High speed/international"), QStringLiteral("Freight"),
				 QStringLiteral("Metro/urban"), QStringLiteral("Suburban")})
			m_serviceCategoryCombo->addItem(category, category);
		int categoryIndex = 0;
		if (hasSelection) {
			const QString currentCategory = QString::fromStdString(m_sceneModel.services[row].category);
			categoryIndex = m_serviceCategoryCombo->findData(currentCategory);
			if (categoryIndex < 0 && !currentCategory.isEmpty()) {
				m_serviceCategoryCombo->addItem(QString("Unknown: %1").arg(currentCategory), currentCategory);
				categoryIndex = m_serviceCategoryCombo->count() - 1;
			}
			if (categoryIndex < 0)
				categoryIndex = 0;
		}
		m_serviceCategoryCombo->setCurrentIndex(categoryIndex);
		m_serviceCategoryCombo->setEnabled(editorAvailable);
	}

	if (m_serviceColorButton) {
		m_serviceColorButton->setColor(hasSelection ? m_sceneModel.services[row].visualizationColor : std::string());
		m_serviceColorButton->setEnabled(editorAvailable);
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
		m_serviceCompositionCombo->setEnabled(editorAvailable);
	}

	if (m_serviceRouteCombo) {
		const QSignalBlocker blocker(m_serviceRouteCombo);
		m_serviceRouteCombo->clear();
		const auto inventory = buildSceneSectionInventory(m_sceneModel);
		for (const auto& route : m_sceneModel.routes)
			addServiceRouteChoice(m_serviceRouteCombo, m_sceneModel, route, inventory);
		if (hasSelection) {
			QString currentRoute = QString::fromStdString(m_sceneModel.services[row].route);
			if (m_serviceRouteCombo->findData(currentRoute) < 0)
				m_serviceRouteCombo->addItem(QString("Missing route [%1]").arg(currentRoute), currentRoute);
			m_serviceRouteCombo->setCurrentIndex(m_serviceRouteCombo->findData(currentRoute));
		}
		m_serviceRouteCombo->setToolTip(m_serviceRouteCombo->currentData(Qt::ToolTipRole).toString());
		m_serviceRouteCombo->setEnabled(editorAvailable);
	}

	bool hasEntryTime = hasSelection && m_sceneModel.services[row].hasEntryTime;
	if (m_serviceHasEntryTimeCheck) {
		const QSignalBlocker blocker(m_serviceHasEntryTimeCheck);
		m_serviceHasEntryTimeCheck->setChecked(hasEntryTime);
		m_serviceHasEntryTimeCheck->setEnabled(editorAvailable);
	}
	if (m_serviceEntryTimeSecondsEdit) {
		const QSignalBlocker blocker(m_serviceEntryTimeSecondsEdit);
		int seconds = hasSelection ? static_cast<int>(m_sceneModel.services[row].entryTimeSeconds) : 0;
		m_serviceEntryTimeSecondsEdit->setText(QString::number(seconds));
		m_serviceEntryTimeSecondsEdit->setEnabled(editorAvailable && hasEntryTime);
	}

	bool hasRepeat = hasSelection && m_sceneModel.services[row].hasRepeat;
	if (m_serviceHasRepeatCheck) {
		const QSignalBlocker blocker(m_serviceHasRepeatCheck);
		m_serviceHasRepeatCheck->setChecked(hasRepeat);
		m_serviceHasRepeatCheck->setEnabled(editorAvailable);
	}
	if (m_serviceHeadwaySecondsEdit) {
		const QSignalBlocker blocker(m_serviceHeadwaySecondsEdit);
		int seconds = hasSelection ? static_cast<int>(m_sceneModel.services[row].headwaySeconds) : 0;
		m_serviceHeadwaySecondsEdit->setText(QString::number(seconds));
		m_serviceHeadwaySecondsEdit->setEnabled(editorAvailable && hasRepeat);
	}

	bool hasRepeatCount = hasSelection && m_sceneModel.services[row].hasRepeatCount;
	if (m_serviceHasRepeatCountCheck) {
		const QSignalBlocker blocker(m_serviceHasRepeatCountCheck);
		m_serviceHasRepeatCountCheck->setChecked(hasRepeatCount);
		m_serviceHasRepeatCountCheck->setEnabled(editorAvailable && hasRepeat);
	}
	if (m_serviceRepeatCountEdit) {
		const QSignalBlocker blocker(m_serviceRepeatCountEdit);
		int count = hasSelection ? m_sceneModel.services[row].repeatCount : 1;
		m_serviceRepeatCountEdit->setText(QString::number(std::max(1, count)));
		m_serviceRepeatCountEdit->setEnabled(editorAvailable && hasRepeat && hasRepeatCount);
	}

	if (m_servicePerformancePercentEdit) {
		const QSignalBlocker blocker(m_servicePerformancePercentEdit);
		m_servicePerformancePercentEdit->setValue(hasSelection
				? std::clamp(m_sceneModel.services[row].performancePercent, 1.0, 100.0)
				: 100.0);
		m_servicePerformancePercentEdit->setEnabled(editorAvailable);
	}

	const bool hasMaximumSpeed = hasSelection && m_sceneModel.services[row].hasMaximumSpeed;
	if (m_serviceHasMaximumSpeedCheck) {
		const QSignalBlocker blocker(m_serviceHasMaximumSpeedCheck);
		m_serviceHasMaximumSpeedCheck->setChecked(hasMaximumSpeed);
		m_serviceHasMaximumSpeedCheck->setEnabled(editorAvailable);
	}
	if (m_serviceMaximumSpeedKmhEdit) {
		const QSignalBlocker blocker(m_serviceMaximumSpeedKmhEdit);
		m_serviceMaximumSpeedKmhEdit->setValue(hasSelection
				? std::max(0.1, m_sceneModel.services[row].maximumSpeedKmh)
				: 100.0);
		m_serviceMaximumSpeedKmhEdit->setEnabled(editorAvailable && hasMaximumSpeed);
	}

	const bool hasOperatingCodeStep = hasSelection && m_sceneModel.services[row].hasOperatingCodeStep;
	if (m_serviceHasOperatingCodeStepCheck) {
		const QSignalBlocker blocker(m_serviceHasOperatingCodeStepCheck);
		m_serviceHasOperatingCodeStepCheck->setChecked(hasOperatingCodeStep);
		m_serviceHasOperatingCodeStepCheck->setEnabled(editorAvailable && hasRepeat);
	}
	if (m_serviceOperatingCodeStepEdit) {
		const QSignalBlocker blocker(m_serviceOperatingCodeStepEdit);
		int step = hasSelection ? m_sceneModel.services[row].operatingCodeStep : 1;
		m_serviceOperatingCodeStepEdit->setText(QString::number(step == 0 ? 1 : step));
		m_serviceOperatingCodeStepEdit->setEnabled(editorAvailable && hasRepeat && hasOperatingCodeStep);
	}

	// the stop list is part of the service detail, so it refreshes whenever the
	// selected service changes
	refreshStopList();

	if (m_duplicateServiceButton)
		m_duplicateServiceButton->setEnabled(editorAvailable);
	if (m_deleteServiceButton)
		m_deleteServiceButton->setEnabled(editorAvailable);
}

QString MainWindow::generatedServiceLabel(const SceneService& service, int occurrence) const {
	const std::string code = sceneServiceOccurrenceOperatingCode(service, occurrence);
	const double entry = sceneServiceScheduledEntry(service, occurrence);
	const QString entryText = std::isfinite(entry)
		? QString("+%1 s").arg(QString::number(entry, 'g', 12))
		: QStringLiteral("(invalid scheduled entry)");
	return QString("%1 | route %2 | entry %3")
		.arg(code.empty() ? QStringLiteral("(unavailable)") : QString::fromStdString(code),
			QString::fromStdString(service.route), entryText);
}

double MainWindow::serviceOccurrenceDuration() const {
	if (initial_variables.durationOverride)
		return initial_variables.times;
	return m_sceneModel.settings.hasDuration
		? m_sceneModel.settings.durationSeconds
		: computeHorizon(m_sceneModel);
}

void MainWindow::pruneExcludedServiceOccurrences() {
	if (!m_sceneLoaded)
		return;
	const double durationSeconds = serviceOccurrenceDuration();
	for (auto it = m_excludedSceneOccurrences.begin(); it != m_excludedSceneOccurrences.end();) {
		const auto service = std::find_if(m_sceneModel.services.begin(), m_sceneModel.services.end(),
			[&](const SceneService& candidate) { return candidate.id == it->serviceId; });
		if (service == m_sceneModel.services.end() || it->occurrence < 1
			|| it->occurrence > sceneServiceOccurrenceCount(*service, durationSeconds))
			it = m_excludedSceneOccurrences.erase(it);
		else
			++it;
	}
}

void MainWindow::migrateExcludedServiceOccurrences(const std::string& oldId, const std::string& newId) {
	if (oldId == newId)
		return;
	SceneRunSelection migrated;
	for (const SceneServiceOccurrence& value : m_excludedSceneOccurrences) {
		SceneServiceOccurrence replacement = value;
		if (replacement.serviceId == oldId)
			replacement.serviceId = newId;
		migrated.insert(replacement);
	}
	m_excludedSceneOccurrences.swap(migrated);
}

int MainWindow::totalServiceOccurrences() const {
	if (!m_sceneLoaded)
		return 0;
	const double durationSeconds = serviceOccurrenceDuration();
	int total = 0;
	for (const SceneService& service : m_sceneModel.services) {
		const int count = std::max(0, sceneServiceOccurrenceCount(service, durationSeconds));
		if (count > std::numeric_limits<int>::max() - total)
			return std::numeric_limits<int>::max();
		total += count;
	}
	return total;
}

int MainWindow::inPeriodServiceOccurrences() const {
	if (!m_sceneLoaded)
		return 0;
	long long total = 0;
	for (const SceneService& service : m_sceneModel.services) {
		total += sceneServiceInWindowCount(service, serviceOccurrenceDuration());
		if (total >= std::numeric_limits<int>::max())
			return std::numeric_limits<int>::max();
	}
	return static_cast<int>(total);
}

int MainWindow::selectedServiceOccurrences() const {
	if (!m_sceneLoaded)
		return 0;
	const double durationSeconds = serviceOccurrenceDuration();
	int excluded = 0;
	for (const SceneServiceOccurrence& value : m_excludedSceneOccurrences) {
		const auto service = std::find_if(m_sceneModel.services.begin(), m_sceneModel.services.end(),
			[&](const SceneService& candidate) { return candidate.id == value.serviceId; });
		if (service != m_sceneModel.services.end() && value.occurrence >= 1
			&& value.occurrence <= sceneServiceOccurrenceCount(*service, durationSeconds))
			++excluded;
	}
	return std::max(0, totalServiceOccurrences() - excluded);
}

int MainWindow::selectedServiceOccurrencesInPeriod() const {
	if (!m_sceneLoaded)
		return 0;
	const double durationSeconds = serviceOccurrenceDuration();
	const int configured = totalServiceOccurrences();
	if (m_excludedSceneOccurrences.empty() || configured > Max_N_Reg)
		return inPeriodServiceOccurrences();
	int selected = 0;
	for (const SceneService& service : m_sceneModel.services) {
		const int count = std::max(0, sceneServiceOccurrenceCount(service, durationSeconds));
		for (int occurrence = 1; occurrence <= count; ++occurrence) {
			const SceneServiceOccurrence value{service.id, occurrence};
			const double entry = sceneServiceScheduledEntry(service, occurrence);
			if (m_excludedSceneOccurrences.find(value) == m_excludedSceneOccurrences.end()
				&& std::isfinite(entry) && entry >= 0.0 && entry < durationSeconds)
				++selected;
		}
	}
	return selected;
}

SceneRunSelection MainWindow::selectedSceneOccurrences() const {
	SceneRunSelection selection;
	if (!m_sceneLoaded || m_excludedSceneOccurrences.empty())
		return selection;
	if (totalServiceOccurrences() > Max_N_Reg)
		return selection;
	const double durationSeconds = serviceOccurrenceDuration();
	for (const SceneService& service : m_sceneModel.services) {
		const int count = std::max(0, sceneServiceOccurrenceCount(service, durationSeconds));
		for (int occurrence = 1; occurrence <= count; ++occurrence) {
			SceneServiceOccurrence value;
			value.serviceId = service.id;
			value.occurrence = occurrence;
			if (m_excludedSceneOccurrences.find(value) == m_excludedSceneOccurrences.end())
				selection.insert(value);
		}
	}
	return selection;
}

void MainWindow::refreshServiceOccurrencePreview() {
	if (!m_serviceOccurrenceTable)
		return;
	pruneExcludedServiceOccurrences();
	m_updatingServiceOccurrencePreview = true;
	m_serviceOccurrenceTable->clearContents();
	if (!m_sceneLoaded) {
		m_serviceOccurrenceTable->setRowCount(0);
		m_updatingServiceOccurrencePreview = false;
		if (m_serviceOccurrenceSelectionLabel)
			m_serviceOccurrenceSelectionLabel->setText(QStringLiteral("No scene loaded"));
		if (m_selectAllOccurrencesButton)
			m_selectAllOccurrencesButton->setEnabled(false);
		if (m_selectNoneOccurrencesButton)
			m_selectNoneOccurrencesButton->setEnabled(false);
		return;
	}

	const double durationSeconds = serviceOccurrenceDuration();
	const int totalOccurrences = totalServiceOccurrences();
	const int displayedOccurrences = std::min(totalOccurrences, Max_N_Reg);
	if (totalOccurrences > Max_N_Reg)
		m_excludedSceneOccurrences.clear();
	m_serviceOccurrenceTable->setRowCount(displayedOccurrences);
	int row = 0;
	for (const SceneService& service : m_sceneModel.services) {
		const int count = std::min(std::max(0, sceneServiceOccurrenceCount(service, durationSeconds)),
			displayedOccurrences - row);
		for (int occurrence = 1; occurrence <= count; ++occurrence, ++row) {
			SceneServiceOccurrence value;
			value.serviceId = service.id;
			value.occurrence = occurrence;
			const bool checked = m_excludedSceneOccurrences.find(value) == m_excludedSceneOccurrences.end();
			auto* include = new QTableWidgetItem();
			include->setFlags(include->flags() | Qt::ItemIsUserCheckable);
			include->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
			include->setData(Qt::UserRole, QString::fromStdString(service.id));
			include->setData(Qt::UserRole + 1, occurrence);
			m_serviceOccurrenceTable->setItem(row, 0, include);
			const std::string code = sceneServiceOccurrenceOperatingCode(service, occurrence);
			m_serviceOccurrenceTable->setItem(row, 1,
				new QTableWidgetItem(code.empty() ? QStringLiteral("(unavailable)") : QString::fromStdString(code)));
			m_serviceOccurrenceTable->setItem(row, 2,
				new QTableWidgetItem(QString("route %1").arg(QString::fromStdString(service.route))));
			const double entry = sceneServiceScheduledEntry(service, occurrence);
			m_serviceOccurrenceTable->setItem(row, 3,
				new QTableWidgetItem(
					std::isfinite(entry)
						? QString("+%1 s").arg(QString::number(entry, 'g', 12))
						: QStringLiteral("(invalid scheduled entry)")));
			m_serviceOccurrenceTable->setItem(row, 4,
				new QTableWidgetItem(QString::number(static_cast<double>(service.performancePercent), 'g', 6)));
			m_serviceOccurrenceTable->setItem(row, 5,
				new QTableWidgetItem(service.hasMaximumSpeed ? QString::number(service.maximumSpeedKmh, 'g', 6) : QStringLiteral("-")));
			for (int column = 0; column < m_serviceOccurrenceTable->columnCount(); ++column)
				m_serviceOccurrenceTable->item(row, column)
					->setToolTip(generatedServiceLabel(service, occurrence)
						+ QString(" [%1, #%2]").arg(QString::fromStdString(service.id)).arg(occurrence));
		}
		if (row >= displayedOccurrences)
			break;
	}
	m_serviceOccurrenceTable->resizeColumnsToContents();
	m_updatingServiceOccurrencePreview = false;
	if (m_serviceOccurrenceSelectionLabel) {
		QString text = QString("Configured total: %1; Number of services in sim.: %2; Selected: %3; Selected in period: %4.\n"
							   "Counting rule: scheduled entry ≥ 0 and < %5 s; these are configured identities, not observed trains.")
						   .arg(totalOccurrences)
						   .arg(inPeriodServiceOccurrences())
						   .arg(selectedServiceOccurrences())
						   .arg(selectedServiceOccurrencesInPeriod())
						   .arg(QString::number(durationSeconds, 'g', 12));
		if (totalOccurrences > displayedOccurrences)
			text += QString("\nPreview limited to %1 rows; generation is not truncated. Reduce the configured pattern before Run.")
						.arg(displayedOccurrences);
		m_serviceOccurrenceSelectionLabel->setText(text);
		m_serviceOccurrenceSelectionLabel->setWordWrap(true);
	}
	const bool controlsEnabled = m_sceneLoaded && !m_worker && totalOccurrences <= Max_N_Reg;
	if (m_serviceOccurrenceTable)
		m_serviceOccurrenceTable->setEnabled(controlsEnabled);
	if (m_selectAllOccurrencesButton)
		m_selectAllOccurrencesButton->setEnabled(controlsEnabled);
	if (m_selectNoneOccurrencesButton)
		m_selectNoneOccurrencesButton->setEnabled(controlsEnabled);
	refreshEntranceDelayPanel();
	refreshIncidentTargetCombo();
}

void MainWindow::updateServiceOccurrenceSelection(QTableWidgetItem* item) {
	if (m_updatingServiceOccurrencePreview || !item || item->column() != 0)
		return;
	SceneServiceOccurrence value;
	value.serviceId = item->data(Qt::UserRole).toString().toStdString();
	value.occurrence = item->data(Qt::UserRole + 1).toInt();
	if (item->checkState() == Qt::Checked)
		m_excludedSceneOccurrences.erase(value);
	else
		m_excludedSceneOccurrences.insert(value);
	refreshServiceOccurrencePreview();
}

void MainWindow::selectAllServiceOccurrences() {
	if (m_worker)
		return;
	m_excludedSceneOccurrences.clear();
	refreshServiceOccurrencePreview();
}

void MainWindow::selectNoneServiceOccurrences() {
	if (m_worker || totalServiceOccurrences() > Max_N_Reg)
		return;
	const double durationSeconds = serviceOccurrenceDuration();
	m_excludedSceneOccurrences.clear();
	for (const SceneService& service : m_sceneModel.services) {
		const int count = std::max(0, sceneServiceOccurrenceCount(service, durationSeconds));
		for (int occurrence = 1; occurrence <= count; ++occurrence) {
			SceneServiceOccurrence value;
			value.serviceId = service.id;
			value.occurrence = occurrence;
			m_excludedSceneOccurrences.insert(value);
		}
	}
	refreshServiceOccurrencePreview();
}

void MainWindow::addService() {
	if (!m_sceneLoaded)
		return;

	SceneService service;
	service.id = sceneUniqueServiceId(m_sceneModel, "new_service");
	m_sceneModel.services.push_back(service);

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshServicePanel();
	refreshIncidentTargetCombo();
	refreshValidationPanel();

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
	duplicate.id = sceneUniqueServiceId(m_sceneModel, duplicate.id + "_copy");
	m_sceneModel.services.insert(m_sceneModel.services.begin() + row + 1, duplicate);

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshServicePanel();
	refreshIncidentTargetCombo();
	refreshValidationPanel();

	if (m_serviceListWidget)
		m_serviceListWidget->setCurrentRow(row + 1);
}

void MainWindow::deleteService() {
	if (!m_sceneLoaded || !m_serviceListWidget)
		return;
	int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;

	const std::string id = m_sceneModel.services[row].id;
	const QStringList consumers = directDeleteConsumers(m_sceneModel, QStringLiteral("service"), id);
	if (!consumers.isEmpty()) {
		showBlockingError(this, "Cannot Delete Service",
			QString("Cannot delete service '%1' because it is still referenced by:\n• %2\nRemove those operation references first.")
				.arg(QString::fromStdString(id), consumers.join("\n• ")),
			true);
		return;
	}
	auto response = QMessageBox::question(this,
		"Delete Service",
		QString("Delete service '%1'?").arg(QString::fromStdString(id)),
		QMessageBox::Yes | QMessageBox::No,
		QMessageBox::No);
	if (response != QMessageBox::Yes)
		return;

	m_sceneModel.services.erase(m_sceneModel.services.begin() + row);
	pruneExcludedServiceOccurrences();

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshServicePanel();
	refreshIncidentTargetCombo();
	refreshValidationPanel();
}

void MainWindow::commitServiceIdEdit() {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_serviceIdEdit)
		return;
	int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;

	const std::string oldId = m_sceneModel.services[row].id;
	const std::string newId = m_serviceIdEdit->text().trimmed().toStdString();
	if (newId.empty()) {
		const QSignalBlocker blocker(m_serviceIdEdit);
		m_serviceIdEdit->setText(QString::fromStdString(oldId));
		showBlockingError(this, "Invalid Service ID", "Service IDs must not be empty.", true);
		return;
	}
	if (newId == oldId)
		return;
	if (sceneUniqueServiceId(m_sceneModel, newId) != newId) {
		const QSignalBlocker blocker(m_serviceIdEdit);
		m_serviceIdEdit->setText(QString::fromStdString(oldId));
		showBlockingError(this, "Service ID already exists", "Choose a unique service ID.", true);
		return;
	}
	sceneRewriteServiceReferences(m_sceneModel, oldId, newId);
	migrateExcludedServiceOccurrences(oldId, newId);
	m_sceneModel.services[row].id = newId;

	// update the list row label in place instead of rebuilding the panel, so a
	// focus-out that lands on another control keeps that pending click intact
	if (QListWidgetItem* item = m_serviceListWidget->item(row)) {
		const QSignalBlocker blocker(m_serviceListWidget);
		item->setText(QString::fromStdString(newId));
	}

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
	updateIncidentDetailPanel();
}

void MainWindow::commitServiceOperatingCode() {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_serviceOperatingCodeEdit)
		return;
	const int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;
	const std::string value = m_serviceOperatingCodeEdit->text().trimmed().toStdString();
	if (value == m_sceneModel.services[row].operatingCode)
		return;
	m_sceneModel.services[row].operatingCode = value;
	markSceneDirty();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
}

void MainWindow::commitServiceCategory(int index) {
	if (!m_sceneLoaded || m_worker || !m_serviceListWidget || !m_serviceCategoryCombo || index < 0)
		return;
	const int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;
	const std::string value = m_serviceCategoryCombo->itemData(index).toString().toStdString();
	if (value == m_sceneModel.services[row].category)
		return;
	m_sceneModel.services[row].category = value;
	markSceneDirty();
}

void MainWindow::commitServiceVisualizationColor(const QString& text) {
	if (!m_sceneLoaded || m_worker || !m_serviceListWidget)
		return;
	const int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;
	const std::string value = text.toStdString();
	if (value == m_sceneModel.services[row].visualizationColor)
		return;
	m_sceneModel.services[row].visualizationColor = value;
	markSceneDirty();
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
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
}

void MainWindow::commitServiceRoute(int index) {
	if (!m_sceneLoaded || m_worker || !m_serviceListWidget || !m_serviceRouteCombo || index < 0)
		return;
	int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;

	std::string newRoute = m_serviceRouteCombo->itemData(index).toString().toStdString();
	if (newRoute == m_sceneModel.services[row].route)
		return;

	m_sceneModel.services[row].route = newRoute;
	m_serviceRouteCombo->setToolTip(m_serviceRouteCombo->itemData(index, Qt::ToolTipRole).toString());

	// the combo already shows the chosen value and the service list labels are
	// unchanged, so do not rebuild the panel here (that would close the popup)
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
	refreshStopList();
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

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
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

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
}

void MainWindow::commitServiceHasRepeat(bool checked) {
	if (!m_sceneLoaded || !m_serviceListWidget)
		return;
	int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;
	if (checked == m_sceneModel.services[row].hasRepeat)
		return;

	SceneService& service = m_sceneModel.services[row];
	service.hasRepeat = checked;
	if (!checked) {
		service.hasRepeatCount = false;
		service.hasOperatingCodeStep = false;
	}
	if (m_serviceHeadwaySecondsEdit)
		m_serviceHeadwaySecondsEdit->setEnabled(checked);
	if (m_serviceHasRepeatCountCheck) {
		const QSignalBlocker blocker(m_serviceHasRepeatCountCheck);
		m_serviceHasRepeatCountCheck->setChecked(checked && service.hasRepeatCount);
		m_serviceHasRepeatCountCheck->setEnabled(checked && !m_worker);
	}
	if (m_serviceRepeatCountEdit)
		m_serviceRepeatCountEdit->setEnabled(checked && service.hasRepeatCount && !m_worker);
	if (m_serviceHasOperatingCodeStepCheck) {
		const QSignalBlocker blocker(m_serviceHasOperatingCodeStepCheck);
		m_serviceHasOperatingCodeStepCheck->setChecked(checked && service.hasOperatingCodeStep);
		m_serviceHasOperatingCodeStepCheck->setEnabled(checked && !m_worker);
	}
	if (m_serviceOperatingCodeStepEdit)
		m_serviceOperatingCodeStepEdit->setEnabled(checked && service.hasOperatingCodeStep && !m_worker);

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
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

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
}

void MainWindow::commitServiceHasRepeatCount(bool checked) {
	if (!m_sceneLoaded || !m_serviceListWidget)
		return;
	const int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;
	if (checked == m_sceneModel.services[row].hasRepeatCount)
		return;
	m_sceneModel.services[row].hasRepeatCount = checked;
	if (checked && m_sceneModel.services[row].repeatCount < 1)
		m_sceneModel.services[row].repeatCount = 1;
	if (m_serviceRepeatCountEdit) {
		const QSignalBlocker blocker(m_serviceRepeatCountEdit);
		m_serviceRepeatCountEdit->setText(QString::number(std::max(1, m_sceneModel.services[row].repeatCount)));
		m_serviceRepeatCountEdit->setEnabled(checked && !m_worker);
	}
	markSceneDirty();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
}

void MainWindow::commitServiceRepeatCount() {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_serviceRepeatCountEdit)
		return;
	const int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;
	bool ok = false;
	int count = m_serviceRepeatCountEdit->text().toInt(&ok);
	if (!ok || count < 1)
		count = 1;
	{
		const QSignalBlocker blocker(m_serviceRepeatCountEdit);
		m_serviceRepeatCountEdit->setText(QString::number(count));
	}
	if (count == m_sceneModel.services[row].repeatCount)
		return;
	m_sceneModel.services[row].repeatCount = count;
	markSceneDirty();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
}

void MainWindow::commitServicePerformancePercent(double value) {
	if (!m_sceneLoaded || !m_serviceListWidget)
		return;
	const int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;
	value = std::clamp(value, 1.0, 100.0);
	if (value == m_sceneModel.services[row].performancePercent)
		return;
	m_sceneModel.services[row].performancePercent = value;
	markSceneDirty();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
}

void MainWindow::commitServiceHasMaximumSpeed(bool checked) {
	if (!m_sceneLoaded || !m_serviceListWidget)
		return;
	const int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;
	if (checked == m_sceneModel.services[row].hasMaximumSpeed)
		return;
	m_sceneModel.services[row].hasMaximumSpeed = checked;
	if (checked && m_sceneModel.services[row].maximumSpeedKmh <= 0.0)
		m_sceneModel.services[row].maximumSpeedKmh = 100.0;
	if (m_serviceMaximumSpeedKmhEdit) {
		const QSignalBlocker blocker(m_serviceMaximumSpeedKmhEdit);
		m_serviceMaximumSpeedKmhEdit->setValue(std::max(0.1, m_sceneModel.services[row].maximumSpeedKmh));
		m_serviceMaximumSpeedKmhEdit->setEnabled(checked && !m_worker);
	}
	markSceneDirty();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
}

void MainWindow::commitServiceMaximumSpeed() {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_serviceMaximumSpeedKmhEdit)
		return;
	const int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;
	const double value = m_serviceMaximumSpeedKmhEdit->value();
	if (value == m_sceneModel.services[row].maximumSpeedKmh)
		return;
	m_sceneModel.services[row].maximumSpeedKmh = value;
	markSceneDirty();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
}

void MainWindow::commitServiceHasOperatingCodeStep(bool checked) {
	if (!m_sceneLoaded || !m_serviceListWidget)
		return;
	const int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;
	if (checked == m_sceneModel.services[row].hasOperatingCodeStep)
		return;
	m_sceneModel.services[row].hasOperatingCodeStep = checked;
	if (checked && m_sceneModel.services[row].operatingCodeStep == 0)
		m_sceneModel.services[row].operatingCodeStep = 1;
	if (m_serviceOperatingCodeStepEdit) {
		const QSignalBlocker blocker(m_serviceOperatingCodeStepEdit);
		m_serviceOperatingCodeStepEdit->setText(QString::number(
			m_sceneModel.services[row].operatingCodeStep == 0 ? 1 : m_sceneModel.services[row].operatingCodeStep));
		m_serviceOperatingCodeStepEdit->setEnabled(checked && !m_worker);
	}
	markSceneDirty();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
}

void MainWindow::commitServiceOperatingCodeStep() {
	if (!m_sceneLoaded || !m_serviceListWidget || !m_serviceOperatingCodeStepEdit)
		return;
	const int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;
	bool ok = false;
	int value = m_serviceOperatingCodeStepEdit->text().toInt(&ok);
	if (!ok || value == 0)
		value = 1;
	{
		const QSignalBlocker blocker(m_serviceOperatingCodeStepEdit);
		m_serviceOperatingCodeStepEdit->setText(QString::number(value));
	}
	if (value == m_sceneModel.services[row].operatingCodeStep)
		return;
	m_sceneModel.services[row].operatingCodeStep = value;
	m_sceneModel.services[row].hasOperatingCodeStep = true;
	markSceneDirty();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
}

void MainWindow::commitPendingServiceSettings() {
	if (!m_sceneLoaded || m_worker)
		return;
	commitServiceIdEdit();
	commitServiceOperatingCode();
	if (m_serviceHasEntryTimeCheck && m_serviceHasEntryTimeCheck->isChecked())
		commitServiceEntryTimeSeconds();
	if (m_serviceHasRepeatCheck && m_serviceHasRepeatCheck->isChecked()) {
		commitServiceHeadwaySeconds();
		if (m_serviceHasRepeatCountCheck && m_serviceHasRepeatCountCheck->isChecked())
			commitServiceRepeatCount();
		if (m_serviceHasOperatingCodeStepCheck && m_serviceHasOperatingCodeStepCheck->isChecked())
			commitServiceOperatingCodeStep();
	}
	if (m_servicePerformancePercentEdit) {
		m_servicePerformancePercentEdit->interpretText();
		commitServicePerformancePercent(m_servicePerformancePercentEdit->value());
	}
	if (m_serviceHasMaximumSpeedCheck && m_serviceHasMaximumSpeedCheck->isChecked()
		&& m_serviceMaximumSpeedKmhEdit) {
		m_serviceMaximumSpeedKmhEdit->interpretText();
		commitServiceMaximumSpeed();
	}
}

void MainWindow::refreshStopList() {
	const int serviceRow = m_serviceListWidget ? m_serviceListWidget->currentRow() : -1;
	const bool hasService = m_sceneLoaded && serviceRow >= 0
		&& serviceRow < static_cast<int>(m_sceneModel.services.size());
	const bool editorAvailable = hasService && !m_worker;
	const int previousRow = m_stopTableWidget ? m_stopTableWidget->currentRow() : -1;
	const long long baseOffsetSeconds = baseTimeToSeconds(m_sceneModel.baseTime);
	if (m_stopTimeBaseLabel) {
		m_stopTimeBaseLabel->setText(hasService
				? QString("Case base %1; %2")
					  .arg(QString::fromStdString(m_sceneModel.baseTime.empty()
							  ? std::string("00:00:00")
							  : m_sceneModel.baseTime))
					  .arg(m_stopClockMode ? QStringLiteral("clock times include explicit day offsets")
										   : QStringLiteral("planned times are elapsed seconds from this base"))
				: QString());
	}
	if (m_stopTimeModeCombo)
		m_stopTimeModeCombo->setEnabled(editorAvailable);
	if (m_stopTableWidget) {
		const QSignalBlocker blocker(m_stopTableWidget);
		m_stopTableWidget->clearContents();
		m_stopTableWidget->setRowCount(hasService
				? static_cast<int>(m_sceneModel.services[serviceRow].stops.size())
				: 0);
		std::vector<SceneStopResolution> resolutions;
		if (hasService)
			resolutions = resolveSceneServiceStops(m_sceneModel, m_sceneModel.services[serviceRow],
				sceneServiceTraversal(m_sceneModel, m_sceneModel.services[serviceRow]));
		if (hasService) {
			const auto& stops = m_sceneModel.services[serviceRow].stops;
			for (int row = 0; row < static_cast<int>(stops.size()); ++row) {
				const SceneStop& stop = stops[static_cast<std::size_t>(row)];
				const QString station = stationDisplayName(m_sceneModel, stop.stationId);
				const QString platform = stop.platformId.empty()
					? QStringLiteral("—")
					: QString::fromStdString(stop.platformId);
				const QString arrival = stop.hasPlannedArrival
					? QString::fromStdString(formatPlannedTime(stop.plannedArrivalSeconds,
						  m_stopClockMode, baseOffsetSeconds))
					: QStringLiteral("—");
				const QString departure = stop.hasPlannedDeparture
					? QString::fromStdString(formatPlannedTime(stop.plannedDepartureSeconds,
						  m_stopClockMode, baseOffsetSeconds))
					: QStringLiteral("—");
				const std::array<QString, 5> values = {station, platform,
					QString::fromStdString(csv::formatDouble(stop.dwellSeconds)), arrival, departure};
				const QString reason = row < static_cast<int>(resolutions.size())
					? stopResolutionText(resolutions[static_cast<std::size_t>(row)].status)
					: QString();
				for (int column = 0; column < 5; ++column) {
					auto* item = new QTableWidgetItem(values[static_cast<std::size_t>(column)]);
					item->setData(Qt::UserRole, QString::fromStdString(stop.stationId));
					item->setData(Qt::UserRole + 1, QString::fromStdString(stop.platformId));
					item->setData(Qt::UserRole + 2, row);
					item->setToolTip(reason.isEmpty() ? QString("Station %1 [%2]")
															.arg(station, QString::fromStdString(stop.stationId))
													  : reason);
					m_stopTableWidget->setItem(row, column, item);
				}
			}
		}
		m_stopTableWidget->resizeColumnsToContents();
		m_stopTableWidget->setEnabled(editorAvailable);
		if (m_stopTableWidget->rowCount() > 0)
			m_stopTableWidget->setCurrentCell(std::clamp(previousRow < 0 ? 0 : previousRow,
												  0, m_stopTableWidget->rowCount() - 1),
				0);
		else
			m_stopTableWidget->clearSelection();
	}
	updateStopActions();
	refreshEntranceDelayPanel();
}

void MainWindow::updateStopActions() {
	const bool editorAvailable = m_sceneLoaded && !m_worker && m_serviceListWidget
		&& m_serviceListWidget->currentRow() >= 0;
	const int selectedRow = m_stopTableWidget ? m_stopTableWidget->currentRow() : -1;
	const bool hasSelection = editorAvailable && selectedRow >= 0 && m_stopTableWidget
		&& selectedRow < m_stopTableWidget->rowCount();
	if (m_addStopButton)
		m_addStopButton->setEnabled(editorAvailable);
	if (m_insertStopAfterButton)
		m_insertStopAfterButton->setEnabled(hasSelection);
	if (m_removeStopButton)
		m_removeStopButton->setEnabled(hasSelection);
	if (m_moveStopUpButton)
		m_moveStopUpButton->setEnabled(hasSelection && selectedRow > 0);
	if (m_moveStopDownButton)
		m_moveStopDownButton->setEnabled(hasSelection
			&& selectedRow + 1 < m_stopTableWidget->rowCount());
}

void MainWindow::editStop(int row, int insertIndex) {
	if (!m_sceneLoaded || m_worker || !m_serviceListWidget)
		return;
	const int serviceRow = m_serviceListWidget->currentRow();
	if (serviceRow < 0 || serviceRow >= static_cast<int>(m_sceneModel.services.size()))
		return;
	SceneService& service = m_sceneModel.services[static_cast<std::size_t>(serviceRow)];
	const bool append = row < 0;
	if (!append && row >= static_cast<int>(service.stops.size()))
		return;
	// The position of a new stop in the timetable, or the row of the stop being edited.
	std::size_t stopIndex = !append ? static_cast<std::size_t>(row)
		: insertIndex < 0			? service.stops.size()
									: std::min(static_cast<std::size_t>(insertIndex), service.stops.size());
	// The route visits offered for the stop at `stopIndex`. A new stop is offered only
	// the visits between its neighbours; `windowProblem` says why there are none.
	SceneRouteTraversal traversal;
	std::string windowProblem;
	const auto updateWindow = [&]() {
		const auto window = sceneStopInsertionWindow(m_sceneModel, service, stopIndex);
		traversal = window.ok ? window.visits : SceneRouteTraversal();
		windowProblem = window.ok ? std::string() : window.problem;
		if (window.ok && traversal.visits.empty())
			windowProblem = "No station visit is available at this position between the neighbouring stops.";
	};
	if (append)
		updateWindow();
	else
		traversal = sceneRemainingStopTraversal(m_sceneModel, service, stopIndex);

	SceneStop draft = append ? SceneStop() : service.stops[stopIndex];
	if (append && !traversal.visits.empty()) {
		draft.stationId = traversal.visits.front().stationId;
		std::vector<std::string> candidatePlatforms;
		for (const auto& visit : traversal.visits)
			if (visit.stationId == draft.stationId && !visit.platformId.empty()
				&& std::find(candidatePlatforms.begin(), candidatePlatforms.end(), visit.platformId)
					== candidatePlatforms.end())
				candidatePlatforms.push_back(visit.platformId);
		if (candidatePlatforms.size() == 1)
			draft.platformId = candidatePlatforms.front();
	}

	QDialog dialog(this);
	dialog.setObjectName("stopEditorDialog");
	dialog.setWindowTitle(append ? "Add timetable stop" : "Edit timetable stop");
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	auto* form = new QFormLayout();
	ChoiceComboBox* positionCombo = nullptr;
	if (append) {
		positionCombo = new ChoiceComboBox(&dialog);
		positionCombo->setObjectName("stopEditorPositionCombo");
		positionCombo->setAccessibleName("Timetable stop position");
		positionCombo->addItem("At the start of the timetable", 0);
		for (std::size_t index = 0; index < service.stops.size(); ++index) {
			const SceneStop& stop = service.stops[index];
			QString label = QString("After stop %1: %2").arg(index + 1).arg(stationDisplayName(m_sceneModel, stop.stationId));
			if (!stop.platformId.empty())
				label += QString(" [%1]").arg(QString::fromStdString(stop.platformId));
			positionCombo->addItem(label, static_cast<int>(index + 1));
		}
		positionCombo->setCurrentIndex(static_cast<int>(stopIndex));
		form->addRow("Insert position", positionCombo);
	}
	auto* stationCombo = new ChoiceComboBox(&dialog);
	stationCombo->setObjectName("stopEditorStationCombo");
	stationCombo->setAccessibleName("Timetable stop station");
	const auto fillStationChoices = [&]() {
		const QSignalBlocker blocker(stationCombo);
		stationCombo->clear();
		const auto addStationChoice = [&](const std::string& stationId) {
			if (stationId.empty() || stationCombo->findData(QString::fromStdString(stationId)) >= 0)
				return;
			stationCombo->addItem(stationDisplayName(m_sceneModel, stationId), QString::fromStdString(stationId));
		};
		for (const auto& visit : traversal.visits)
			addStationChoice(visit.stationId);
		if (!append)
			addStationChoice(draft.stationId);
		if (stationCombo->count() == 0)
			stationCombo->addItem(append ? "(no reachable station here)" : "(missing station)", QString());
		stationCombo->setEnabled(!append || !traversal.visits.empty());
	};
	fillStationChoices();
	form->addRow("Station", stationCombo);
	auto* platformCombo = new ChoiceComboBox(&dialog);
	platformCombo->setObjectName("stopEditorPlatformCombo");
	platformCombo->setAccessibleName("Timetable stop platform");
	form->addRow("Compatible platform", platformCombo);
	auto* eligibilityLabel = new QLabel(&dialog);
	eligibilityLabel->setObjectName("stopEditorEligibilityLabel");
	eligibilityLabel->setWordWrap(true);
	form->addRow(QString(), eligibilityLabel);

	auto* modeCombo = new ChoiceComboBox(&dialog);
	modeCombo->setObjectName("stopEditorTimeModeCombo");
	modeCombo->addItem("Elapsed offsets (s)", false);
	modeCombo->addItem("Clock time", true);
	modeCombo->setCurrentIndex(m_stopClockMode ? 1 : 0);
	form->addRow("Planned time display", modeCombo);
	auto* baseLabel = new QLabel(QString("Case base time: %1")
									 .arg(QString::fromStdString(m_sceneModel.baseTime.empty() ? std::string("00:00:00") : m_sceneModel.baseTime)),
		&dialog);
	baseLabel->setObjectName("stopEditorBaseTimeLabel");
	baseLabel->setWordWrap(true);
	form->addRow(QString(), baseLabel);

	auto* arrivalPresent = new QCheckBox("Planned arrival", &dialog);
	arrivalPresent->setObjectName("stopEditorArrivalPresent");
	auto* arrivalEdit = new QLineEdit(&dialog);
	arrivalEdit->setObjectName("stopEditorArrivalEdit");
	arrivalEdit->setPlaceholderText("blank = absent");
	arrivalEdit->setAccessibleName("Planned arrival time");
	auto* arrivalLayout = new QHBoxLayout();
	arrivalLayout->addWidget(arrivalPresent);
	arrivalLayout->addWidget(arrivalEdit, 1);
	form->addRow("Arrival", arrivalLayout);
	auto* departurePresent = new QCheckBox("Planned departure", &dialog);
	departurePresent->setObjectName("stopEditorDeparturePresent");
	auto* departureEdit = new QLineEdit(&dialog);
	departureEdit->setObjectName("stopEditorDepartureEdit");
	departureEdit->setPlaceholderText("blank = absent");
	departureEdit->setAccessibleName("Planned departure time");
	auto* departureLayout = new QHBoxLayout();
	departureLayout->addWidget(departurePresent);
	departureLayout->addWidget(departureEdit, 1);
	form->addRow("Departure", departureLayout);
	auto* dwellEdit = new QLineEdit(&dialog);
	dwellEdit->setObjectName("stopEditorDwellEdit");
	dwellEdit->setAccessibleName("Minimum dwell seconds");
	form->addRow("Minimum dwell (s)", dwellEdit);
	layout->addLayout(form);
	auto* errorLabel = new QLabel(&dialog);
	errorLabel->setObjectName("stopEditorErrorLabel");
	errorLabel->setStyleSheet("color: #b00020;");
	errorLabel->setWordWrap(true);
	layout->addWidget(errorLabel);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	buttons->setObjectName("stopEditorButtons");
	buttons->button(QDialogButtonBox::Ok)->setObjectName("stopEditorAcceptButton");
	buttons->button(QDialogButtonBox::Cancel)->setObjectName("stopEditorCancelButton");
	DialogLayout::install(dialog, append ? "Add timetable stop" : "Edit timetable stop",
		"Set the station, compatible platform and planned times.", body, buttons);

	const long long baseOffsetSeconds = baseTimeToSeconds(m_sceneModel.baseTime);
	bool dialogClockMode = m_stopClockMode;
	const auto optionalArrival = [&]() -> std::optional<double> {
		return draft.hasPlannedArrival ? std::optional<double>(draft.plannedArrivalSeconds) : std::nullopt;
	};
	const auto optionalDeparture = [&]() -> std::optional<double> {
		return draft.hasPlannedDeparture ? std::optional<double>(draft.plannedDepartureSeconds) : std::nullopt;
	};
	const auto setTimeField = [&](QCheckBox* present, QLineEdit* edit, const std::optional<double>& value) {
		const QSignalBlocker checkBlocker(present);
		present->setChecked(value.has_value());
		const QSignalBlocker editBlocker(edit);
		edit->setText(QString::fromStdString(formatPlannedTime(value, dialogClockMode, baseOffsetSeconds)));
		edit->setEnabled(value.has_value());
	};
	setTimeField(arrivalPresent, arrivalEdit, optionalArrival());
	setTimeField(departurePresent, departureEdit, optionalDeparture());
	dwellEdit->setText(QString::fromStdString(csv::formatDouble(draft.dwellSeconds)));

	int invalidPlatformIndex = -1;
	const auto stationOnRoute = [&](const std::string& stationId) {
		return std::any_of(traversal.visits.begin(), traversal.visits.end(),
			[&](const auto& visit) { return visit.stationId == stationId; });
	};
	const auto updateEligibility = [&]() {
		if (!windowProblem.empty()) {
			eligibilityLabel->setText(QString::fromStdString(windowProblem));
			return SceneStopResolutionStatus::UnresolvedRoute;
		}
		SceneService candidate = service;
		if (append)
			candidate.stops.insert(candidate.stops.begin() + stopIndex, draft);
		else
			candidate.stops[stopIndex] = draft;
		const auto resolutions = resolveSceneServiceStops(m_sceneModel, candidate,
			sceneServiceTraversal(m_sceneModel, candidate));
		const auto status = stopIndex < resolutions.size() ? resolutions[stopIndex].status
														   : SceneStopResolutionStatus::UnresolvedRoute;
		eligibilityLabel->setText(stopResolutionText(status));
		if (stationCombo->currentIndex() >= 0 && !stationOnRoute(draft.stationId))
			stationCombo->setItemData(stationCombo->currentIndex(), eligibilityLabel->text(), Qt::ToolTipRole);
		if (invalidPlatformIndex >= 0 && platformCombo->currentIndex() == invalidPlatformIndex)
			platformCombo->setItemData(invalidPlatformIndex, eligibilityLabel->text(), Qt::ToolTipRole);
		return status;
	};
	const auto refreshPlatforms = [&]() {
		const std::string stationId = stationCombo->currentData().toString().toStdString();
		const QSignalBlocker blocker(platformCombo);
		platformCombo->clear();
		invalidPlatformIndex = -1;
		platformCombo->addItem(QStringLiteral("(no platform)"), QString());
		std::vector<std::string> choices;
		for (const auto& visit : traversal.visits)
			if (visit.stationId == stationId && !visit.platformId.empty()
				&& std::find(choices.begin(), choices.end(), visit.platformId) == choices.end())
				choices.push_back(visit.platformId);
		for (const auto& platform : choices)
			platformCombo->addItem(QString::fromStdString(platform), QString::fromStdString(platform));
		if (!draft.platformId.empty() && platformCombo->findData(QString::fromStdString(draft.platformId)) < 0) {
			invalidPlatformIndex = platformCombo->count();
			platformCombo->addItem(QString("Invalid: %1").arg(QString::fromStdString(draft.platformId)),
				QString::fromStdString(draft.platformId));
		}
		int index = platformCombo->findData(QString::fromStdString(draft.platformId));
		platformCombo->setCurrentIndex(index < 0 ? 0 : index);
		platformCombo->setEnabled(!stationId.empty());
	};
	{
		const QSignalBlocker blocker(stationCombo);
		const int index = stationCombo->findData(QString::fromStdString(draft.stationId));
		if (index >= 0)
			stationCombo->setCurrentIndex(index);
	}
	refreshPlatforms();
	updateEligibility();
	DialogLayout::fitWidthToContent(dialog);
	if (positionCombo) {
		connect(positionCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
			if (index < 0)
				return;
			stopIndex = static_cast<std::size_t>(positionCombo->itemData(index).toInt());
			updateWindow();
			fillStationChoices();
			int stationIndex = stationCombo->findData(QString::fromStdString(draft.stationId));
			if (stationIndex < 0)
				stationIndex = 0;
			const std::string stationId = stationCombo->itemData(stationIndex).toString().toStdString();
			const bool platformOffered = std::any_of(traversal.visits.begin(), traversal.visits.end(),
				[&](const auto& visit) { return visit.stationId == stationId && visit.platformId == draft.platformId; });
			if (stationId != draft.stationId || !platformOffered)
				draft.platformId.clear();
			draft.stationId = stationId;
			{
				const QSignalBlocker blocker(stationCombo);
				stationCombo->setCurrentIndex(stationIndex);
			}
			refreshPlatforms();
			updateEligibility();
			errorLabel->clear();
		});
	}
	connect(stationCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
		if (index < 0)
			return;
		const std::string stationId = stationCombo->itemData(index).toString().toStdString();
		if (stationId != draft.stationId)
			draft.platformId.clear();
		draft.stationId = stationId;
		refreshPlatforms();
		updateEligibility();
	});
	connect(platformCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
		if (index >= 0)
			draft.platformId = platformCombo->itemData(index).toString().toStdString();
		updateEligibility();
	});
	connect(arrivalPresent, &QCheckBox::toggled, &dialog, [&](bool checked) {
		arrivalEdit->setEnabled(checked);
		if (!checked)
			arrivalEdit->clear();
	});
	connect(departurePresent, &QCheckBox::toggled, &dialog, [&](bool checked) {
		departureEdit->setEnabled(checked);
		if (!checked)
			departureEdit->clear();
	});
	const auto parseField = [&](QCheckBox* present, QLineEdit* edit, bool clock,
								std::optional<double>& value) {
		if (!present->isChecked() || edit->text().trimmed().isEmpty()) {
			value.reset();
			return true;
		}
		return parsePlannedTime(edit->text().toStdString(), clock, baseOffsetSeconds, value);
	};
	const auto showParseError = [&](QLineEdit* edit, const QString& field) {
		errorLabel->setText(QString("%1 is invalid. Enter a non-negative elapsed value or HH:MM:SS[.fraction] with an explicit +Nd day prefix.").arg(field));
		edit->setFocus();
	};
	connect(modeCombo, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
		if (index < 0)
			return;
		const bool nextClockMode = modeCombo->itemData(index).toBool();
		if (nextClockMode == dialogClockMode)
			return;
		std::optional<double> arrival = optionalArrival();
		std::optional<double> departure = optionalDeparture();
		if (!parseField(arrivalPresent, arrivalEdit, dialogClockMode, arrival)
			|| !parseField(departurePresent, departureEdit, dialogClockMode, departure)) {
			const QSignalBlocker blocker(modeCombo);
			modeCombo->setCurrentIndex(dialogClockMode ? 1 : 0);
			errorLabel->setText("Finish the invalid planned time before changing its display mode.");
			return;
		}
		draft.hasPlannedArrival = arrival.has_value();
		if (draft.hasPlannedArrival)
			draft.plannedArrivalSeconds = *arrival;
		draft.hasPlannedDeparture = departure.has_value();
		if (draft.hasPlannedDeparture)
			draft.plannedDepartureSeconds = *departure;
		dialogClockMode = nextClockMode;
		setTimeField(arrivalPresent, arrivalEdit, arrival);
		setTimeField(departurePresent, departureEdit, departure);
		errorLabel->clear();
	});
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
		if (!windowProblem.empty()) {
			errorLabel->setText(QString::fromStdString(windowProblem));
			if (positionCombo)
				positionCombo->setFocus();
			return;
		}
		std::optional<double> arrival = optionalArrival();
		std::optional<double> departure = optionalDeparture();
		if (!parseField(arrivalPresent, arrivalEdit, dialogClockMode, arrival)) {
			showParseError(arrivalEdit, "Planned arrival");
			return;
		}
		if (!parseField(departurePresent, departureEdit, dialogClockMode, departure)) {
			showParseError(departureEdit, "Planned departure");
			return;
		}
		bool dwellOk = false;
		const double dwell = dwellEdit->text().trimmed().toDouble(&dwellOk);
		if (!dwellOk || !std::isfinite(dwell) || dwell < 0.0) {
			errorLabel->setText("Minimum dwell must be a finite non-negative number of seconds.");
			dwellEdit->setFocus();
			return;
		}
		if (draft.stationId.empty()) {
			errorLabel->setText("Choose a station for this timetable stop.");
			stationCombo->setFocus();
			return;
		}
		const bool changedAssignment = append || draft.stationId != service.stops[stopIndex].stationId
			|| draft.platformId != service.stops[stopIndex].platformId;
		if (changedAssignment && updateEligibility() == SceneStopResolutionStatus::AmbiguousPlatform) {
			errorLabel->setText("Choose a compatible platform: more than one is reachable at this station.");
			platformCombo->setFocus();
			return;
		}
		draft.hasPlannedArrival = arrival.has_value();
		if (draft.hasPlannedArrival)
			draft.plannedArrivalSeconds = *arrival;
		draft.hasPlannedDeparture = departure.has_value();
		if (draft.hasPlannedDeparture)
			draft.plannedDepartureSeconds = *departure;
		draft.dwellSeconds = dwell;
		if (append) {
			const auto result = insertSceneStop(m_sceneModel, service, stopIndex, draft);
			if (!result.inserted) {
				errorLabel->setText(QString::fromStdString(result.error));
				return;
			}
		}
		dialog.accept();
	});

	if (dialog.exec() != QDialog::Accepted)
		return;
	if (!append)
		service.stops[stopIndex] = draft;
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshStopList();
	if (m_stopTableWidget)
		m_stopTableWidget->setCurrentCell(static_cast<int>(stopIndex), 0);
	refreshValidationPanel();
}

void MainWindow::addStop() {
	if (!m_sceneLoaded || m_worker || !m_serviceListWidget)
		return;
	const int serviceRow = m_serviceListWidget->currentRow();
	if (serviceRow < 0 || serviceRow >= static_cast<int>(m_sceneModel.services.size()))
		return;
	const SceneService& service = m_sceneModel.services[static_cast<std::size_t>(serviceRow)];
	for (std::size_t position = service.stops.size() + 1; position-- > 0;) {
		const auto window = sceneStopInsertionWindow(m_sceneModel, service, position);
		if (window.ok && !window.visits.visits.empty()) {
			editStop(-1, static_cast<int>(position));
			return;
		}
	}
	QMessageBox::information(this, "No reachable stop",
		"No station visit is available between any of the current stops. Check the route and stop order before adding a stop.");
}

void MainWindow::insertStopAfterSelected() {
	const int row = m_stopTableWidget ? m_stopTableWidget->currentRow() : -1;
	if (row >= 0 && row < m_stopTableWidget->rowCount())
		editStop(-1, row + 1);
}

void MainWindow::removeStop() {
	if (!m_sceneLoaded || m_worker || !m_serviceListWidget || !m_stopTableWidget)
		return;
	const int serviceRow = m_serviceListWidget->currentRow();
	const int stopRow = m_stopTableWidget->currentRow();
	if (serviceRow < 0 || serviceRow >= static_cast<int>(m_sceneModel.services.size())
		|| stopRow < 0 || stopRow >= static_cast<int>(m_sceneModel.services[serviceRow].stops.size()))
		return;
	auto& stops = m_sceneModel.services[serviceRow].stops;
	stops.erase(stops.begin() + stopRow);
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshStopList();
	if (m_stopTableWidget && m_stopTableWidget->rowCount() > 0)
		m_stopTableWidget->setCurrentCell(std::min(stopRow, m_stopTableWidget->rowCount() - 1), 0);
	refreshValidationPanel();
}

void MainWindow::moveStopUp() {
	if (!m_sceneLoaded || m_worker || !m_serviceListWidget || !m_stopTableWidget)
		return;
	const int serviceRow = m_serviceListWidget->currentRow();
	const int stopRow = m_stopTableWidget->currentRow();
	if (serviceRow < 0 || serviceRow >= static_cast<int>(m_sceneModel.services.size()) || stopRow <= 0
		|| stopRow >= static_cast<int>(m_sceneModel.services[serviceRow].stops.size()))
		return;
	auto& stops = m_sceneModel.services[serviceRow].stops;
	std::swap(stops[static_cast<std::size_t>(stopRow)], stops[static_cast<std::size_t>(stopRow - 1)]);
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshStopList();
	m_stopTableWidget->setCurrentCell(stopRow - 1, 0);
	refreshValidationPanel();
}

void MainWindow::moveStopDown() {
	if (!m_sceneLoaded || m_worker || !m_serviceListWidget || !m_stopTableWidget)
		return;
	const int serviceRow = m_serviceListWidget->currentRow();
	const int stopRow = m_stopTableWidget->currentRow();
	if (serviceRow < 0 || serviceRow >= static_cast<int>(m_sceneModel.services.size()) || stopRow < 0
		|| stopRow + 1 >= static_cast<int>(m_sceneModel.services[serviceRow].stops.size()))
		return;
	auto& stops = m_sceneModel.services[serviceRow].stops;
	std::swap(stops[static_cast<std::size_t>(stopRow)], stops[static_cast<std::size_t>(stopRow + 1)]);
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshStopList();
	m_stopTableWidget->setCurrentCell(stopRow + 1, 0);
	refreshValidationPanel();
}

SceneScenario* MainWindow::selectedScenario() {
	if (!m_sceneLoaded)
		return nullptr;
	for (auto& scenario : m_sceneModel.scenarios) {
		if (scenario.id == m_selectedScenarioId)
			return &scenario;
	}
	return defaultScenario(m_sceneModel);
}

const SceneScenario* MainWindow::selectedScenario() const {
	if (!m_sceneLoaded)
		return nullptr;
	for (const auto& scenario : m_sceneModel.scenarios) {
		if (scenario.id == m_selectedScenarioId)
			return &scenario;
	}
	return defaultScenario(static_cast<const SceneModel&>(m_sceneModel));
}

std::vector<SceneIncident>& MainWindow::selectedScenarioIncidents() {
	static std::vector<SceneIncident> empty;
	SceneScenario* scenario = selectedScenario();
	return scenario ? scenario->incidents : empty;
}

SceneIncident* MainWindow::selectedIncident() {
	if (!m_incidentListWidget)
		return nullptr;
	auto& incidents = selectedScenarioIncidents();
	const int row = m_incidentListWidget->currentRow();
	return row >= 0 && row < static_cast<int>(incidents.size()) ? &incidents[row] : nullptr;
}

const std::vector<SceneIncident>& MainWindow::selectedScenarioIncidents() const {
	static const std::vector<SceneIncident> empty;
	const SceneScenario* scenario = selectedScenario();
	return scenario ? scenario->incidents : empty;
}

SceneEntranceDelay* MainWindow::selectedEntranceDelay() {
	if (!m_entranceDelayListWidget)
		return nullptr;
	SceneScenario* scenario = selectedScenario();
	const int row = m_entranceDelayListWidget->currentRow();
	return scenario && row >= 0 && row < static_cast<int>(scenario->entranceDelays.size())
		? &scenario->entranceDelays[static_cast<std::size_t>(row)]
		: nullptr;
}

QString MainWindow::scenarioContext() const {
	if (!m_appliedScenarioId.empty())
		return QString::fromStdString(m_appliedScenarioId);
	const SceneScenario* scenario = selectedScenario();
	return scenario ? QString::fromStdString(scenario->id) : QStringLiteral("(none)");
}

void MainWindow::markScenarioModified() {
	if (const SceneScenario* scenario = selectedScenario())
		m_modifiedScenarioIds.insert(scenario->id);
	markSceneDirty();
}

void MainWindow::selectScenario(int row) {
	if (!m_sceneLoaded || m_worker || row < 0
		|| row >= static_cast<int>(m_sceneModel.scenarios.size()))
		return;
	const std::string id = m_sceneModel.scenarios[static_cast<std::size_t>(row)].id;
	if (id == m_selectedScenarioId) {
		updateScenarioDetailPanel();
		updateIncidentDetailPanel();
		return;
	}
	m_selectedScenarioId = id;
	invalidateRunResults();
	refreshIncidentPanel();
}

void MainWindow::updateScenarioDetailPanel() {
	const SceneScenario* scenario = selectedScenario();
	const bool enabled = m_sceneLoaded && !m_worker && scenario != nullptr;
	if (m_scenarioIdEdit) {
		const QSignalBlocker blocker(m_scenarioIdEdit);
		m_scenarioIdEdit->setText(enabled ? QString::fromStdString(scenario->id) : QString());
		m_scenarioIdEdit->setEnabled(enabled);
	}
	if (m_scenarioNameEdit) {
		const QSignalBlocker blocker(m_scenarioNameEdit);
		m_scenarioNameEdit->setText(enabled ? QString::fromStdString(scenario->name) : QString());
		m_scenarioNameEdit->setEnabled(enabled);
	}
	if (m_scenarioDescriptionEdit) {
		const QSignalBlocker blocker(m_scenarioDescriptionEdit);
		m_scenarioDescriptionEdit->setText(enabled ? QString::fromStdString(scenario->description) : QString());
		m_scenarioDescriptionEdit->setEnabled(enabled);
	}
	if (m_blankScenarioButton)
		m_blankScenarioButton->setEnabled(m_sceneLoaded && !m_worker);
	if (m_duplicateScenarioButton)
		m_duplicateScenarioButton->setEnabled(enabled);
	if (m_deleteScenarioButton) {
		const bool hasPersistedDefault = m_sceneLoaded && !m_sceneModel.defaultScenarioId.empty()
			&& std::any_of(m_sceneModel.scenarios.begin(), m_sceneModel.scenarios.end(),
				[this](const SceneScenario& candidate) {
					return candidate.id == m_sceneModel.defaultScenarioId;
				});
		m_deleteScenarioButton->setEnabled(enabled && hasPersistedDefault
			&& scenario->id != m_sceneModel.defaultScenarioId);
	}
	if (m_importScenarioButton)
		m_importScenarioButton->setEnabled(m_sceneLoaded && !m_worker);
	if (m_exportScenarioButton)
		m_exportScenarioButton->setEnabled(enabled);
}

void MainWindow::refreshScenarioList() {
	const bool hasScene = m_sceneLoaded;
	if (m_selectedScenarioId.empty() && hasScene) {
		if (const SceneScenario* scenario = defaultScenario(static_cast<const SceneModel&>(m_sceneModel)))
			m_selectedScenarioId = scenario->id;
	}

	if (m_scenarioListWidget) {
		const QSignalBlocker blocker(m_scenarioListWidget);
		m_scenarioListWidget->clear();
		int rowToSelect = -1;
		for (std::size_t index = 0; hasScene && index < m_sceneModel.scenarios.size(); ++index) {
			const SceneScenario& scenario = m_sceneModel.scenarios[index];
			new QListWidgetItem(m_scenarioListWidget);
			if (scenario.id == m_selectedScenarioId)
				rowToSelect = static_cast<int>(index);
		}
		updateScenarioPresentation();
		m_scenarioListWidget->setCurrentRow(rowToSelect);
		m_scenarioListWidget->setEnabled(hasScene && !m_worker);
	}

	updateScenarioDetailPanel();
}

void MainWindow::refreshIncidentPanel() {
	const bool hasScene = m_sceneLoaded;
	refreshScenarioList();
	const auto& incidents = selectedScenarioIncidents();

	if (m_incidentListWidget) {
		int previousRow = m_incidentListWidget->currentRow();
		const QSignalBlocker blocker(m_incidentListWidget);
		m_incidentListWidget->clear();
		if (hasScene) {
			for (const auto& incident : incidents) {
				QString label = QString::fromStdString(incident.id) + " (" + QString::fromStdString(incident.type) + ")";
				if (incident.hasOccurrence || incident.occurrence != 1)
					label += QString(" #%1").arg(incident.occurrence);
				if (incident.hasReducedSpeed || incident.reducedSpeedKmh != 0.0)
					label += QString(" / %1 km/h").arg(incident.reducedSpeedKmh);
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
		m_incidentListWidget->setEnabled(hasScene && !m_worker);
	}

	if (m_addIncidentButton)
		m_addIncidentButton->setEnabled(hasScene && !m_worker);

	updateIncidentDetailPanel();
	refreshEntranceDelayPanel();
}

void MainWindow::refreshEntranceDelayPanel() {
	if (!m_entranceDelayListWidget)
		return;
	const bool hasScene = m_sceneLoaded;
	const SceneScenario* scenario = selectedScenario();
	const int previousRow = m_entranceDelayListWidget->currentRow();
	{
		const QSignalBlocker blocker(m_entranceDelayListWidget);
		m_entranceDelayListWidget->clear();
		if (hasScene && scenario) {
			for (const SceneEntranceDelay& delay : scenario->entranceDelays) {
				QString label = QString("%1 / #%2 @ %3: %4 s")
									.arg(QString::fromStdString(delay.serviceId.empty() ? std::string("(empty)") : delay.serviceId))
									.arg(delay.occurrence)
									.arg(QString::fromStdString(delay.stationId.empty() ? std::string("(empty)") : delay.stationId))
									.arg(QString::number(delay.delaySeconds, 'g', 12));
				const auto service = std::find_if(m_sceneModel.services.begin(), m_sceneModel.services.end(),
					[&delay](const SceneService& candidate) { return candidate.id == delay.serviceId; });
				if (service != m_sceneModel.services.end()) {
					const std::string code = sceneServiceOccurrenceOperatingCode(*service, delay.occurrence);
					label += QString(" | code %1").arg(code.empty() ? QStringLiteral("(unavailable)") : QString::fromStdString(code));
				}
				m_entranceDelayListWidget->addItem(label);
			}
		}
		const int rowCount = m_entranceDelayListWidget->count();
		const int rowToSelect = rowCount <= 0 ? -1
											  : (previousRow < 0 ? 0 : std::min(previousRow, rowCount - 1));
		m_entranceDelayListWidget->setCurrentRow(rowToSelect);
		m_entranceDelayListWidget->setEnabled(hasScene && !m_worker);
	}
	if (m_addEntranceDelayButton)
		m_addEntranceDelayButton->setEnabled(hasScene && !m_worker && scenario != nullptr);
	updateEntranceDelayDetailPanel();
}

void MainWindow::updateEntranceDelayDetailPanel() {
	const SceneEntranceDelay* delay = selectedEntranceDelay();
	const bool enabled = m_sceneLoaded && !m_worker && delay != nullptr;
	const SceneService* service = nullptr;
	if (delay) {
		for (const SceneService& candidate : m_sceneModel.services) {
			if (candidate.id == delay->serviceId) {
				service = &candidate;
				break;
			}
		}
	}

	if (m_entranceDelayServiceCombo) {
		const QSignalBlocker blocker(m_entranceDelayServiceCombo);
		m_entranceDelayServiceCombo->clear();
		for (const SceneService& candidate : m_sceneModel.services)
			m_entranceDelayServiceCombo->addItem(generatedServiceLabel(candidate, 1),
				QString::fromStdString(candidate.id));
		if (delay) {
			int index = m_entranceDelayServiceCombo->findData(QString::fromStdString(delay->serviceId));
			if (index < 0) {
				const QString invalid = delay->serviceId.empty()
					? QStringLiteral("Invalid: (empty)")
					: QString("Invalid: %1").arg(QString::fromStdString(delay->serviceId));
				m_entranceDelayServiceCombo->addItem(invalid, QString::fromStdString(delay->serviceId));
				index = m_entranceDelayServiceCombo->count() - 1;
			}
			m_entranceDelayServiceCombo->setCurrentIndex(index);
		}
		m_entranceDelayServiceCombo->setEnabled(enabled);
	}

	const int occurrenceCount = service
		? std::max(1, sceneServiceOccurrenceCount(*service, serviceOccurrenceDuration()))
		: 1;
	if (m_entranceDelayOccurrenceEdit) {
		const QSignalBlocker blocker(m_entranceDelayOccurrenceEdit);
		const int currentOccurrence = delay ? delay->occurrence : 1;
		m_entranceDelayOccurrenceEdit->setRange(std::min(1, currentOccurrence),
			std::max(occurrenceCount, currentOccurrence));
		m_entranceDelayOccurrenceEdit->setValue(currentOccurrence);
		m_entranceDelayOccurrenceEdit->setEnabled(enabled);
	}
	if (m_entranceDelayOccurrenceContextLabel) {
		if (!delay) {
			m_entranceDelayOccurrenceContextLabel->setText(QStringLiteral("Select an entrance delay."));
		} else {
			QString context = QString("Valid occurrence range: 1..%1").arg(occurrenceCount);
			if (service) {
				context += QStringLiteral(" | ") + generatedServiceLabel(*service, delay->occurrence);
			} else {
				context += QStringLiteral(" | Generated operating code: (unavailable)");
			}
			m_entranceDelayOccurrenceContextLabel->setText(context);
		}
	}

	if (m_entranceDelayStationCombo) {
		const QSignalBlocker blocker(m_entranceDelayStationCombo);
		m_entranceDelayStationCombo->clear();
		std::set<std::string> eligibleStations;
		if (service) {
			for (const SceneStop& stop : service->stops) {
				if (eligibleStations.insert(stop.stationId).second && stop.hasPlannedDeparture)
					m_entranceDelayStationCombo->addItem(QString::fromStdString(stop.stationId),
						QString::fromStdString(stop.stationId));
			}
		}
		if (delay) {
			int index = m_entranceDelayStationCombo->findData(QString::fromStdString(delay->stationId));
			if (index < 0) {
				const QString invalid = delay->stationId.empty()
					? QStringLiteral("Invalid: (empty)")
					: QString("Invalid: %1").arg(QString::fromStdString(delay->stationId));
				m_entranceDelayStationCombo->addItem(invalid, QString::fromStdString(delay->stationId));
				index = m_entranceDelayStationCombo->count() - 1;
			}
			m_entranceDelayStationCombo->setCurrentIndex(index);
		}
		m_entranceDelayStationCombo->setEnabled(enabled);
	}

	if (m_entranceDelaySecondsEdit) {
		const QSignalBlocker blocker(m_entranceDelaySecondsEdit);
		m_entranceDelaySecondsEdit->setValue(delay ? delay->delaySeconds : 0.0);
		m_entranceDelaySecondsEdit->setEnabled(enabled);
	}
	if (m_duplicateEntranceDelayButton)
		m_duplicateEntranceDelayButton->setEnabled(enabled);
	if (m_deleteEntranceDelayButton)
		m_deleteEntranceDelayButton->setEnabled(enabled);
}

void MainWindow::addEntranceDelay() {
	if (!m_sceneLoaded || m_worker)
		return;
	SceneScenario* scenario = selectedScenario();
	if (!scenario)
		return;
	SceneEntranceDelay delay;
	for (const SceneService& service : m_sceneModel.services) {
		for (const SceneStop& stop : service.stops) {
			if (!stop.hasPlannedDeparture)
				continue;
			delay.serviceId = service.id;
			delay.stationId = stop.stationId;
			break;
		}
		if (!delay.serviceId.empty())
			break;
	}
	if (delay.serviceId.empty() && !m_sceneModel.services.empty())
		delay.serviceId = m_sceneModel.services.front().id;
	scenario->entranceDelays.push_back(std::move(delay));
	markScenarioModified();
	refreshValidationPanel();
	refreshEntranceDelayPanel();
	if (m_entranceDelayListWidget)
		m_entranceDelayListWidget->setCurrentRow(static_cast<int>(scenario->entranceDelays.size()) - 1);
}

void MainWindow::duplicateEntranceDelay() {
	if (!m_sceneLoaded || m_worker || !m_entranceDelayListWidget)
		return;
	SceneScenario* scenario = selectedScenario();
	const int row = m_entranceDelayListWidget->currentRow();
	if (!scenario || row < 0 || row >= static_cast<int>(scenario->entranceDelays.size()))
		return;
	scenario->entranceDelays.insert(scenario->entranceDelays.begin() + row + 1,
		scenario->entranceDelays[static_cast<std::size_t>(row)]);
	markScenarioModified();
	refreshValidationPanel();
	refreshEntranceDelayPanel();
	if (m_entranceDelayListWidget)
		m_entranceDelayListWidget->setCurrentRow(row + 1);
}

void MainWindow::deleteEntranceDelay() {
	if (!m_sceneLoaded || m_worker || !m_entranceDelayListWidget)
		return;
	SceneScenario* scenario = selectedScenario();
	const int row = m_entranceDelayListWidget->currentRow();
	if (!scenario || row < 0 || row >= static_cast<int>(scenario->entranceDelays.size()))
		return;
	const QString description = QString::fromStdString(scenario->entranceDelays[static_cast<std::size_t>(row)].serviceId);
	if (QMessageBox::question(this, "Delete Entrance Delay",
			QString("Delete entrance delay for service '%1'?").arg(description),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
		!= QMessageBox::Yes)
		return;
	scenario->entranceDelays.erase(scenario->entranceDelays.begin() + row);
	markScenarioModified();
	refreshEntranceDelayPanel();
	refreshValidationPanel();
}

void MainWindow::commitEntranceDelayService(int index) {
	if (!m_sceneLoaded || m_worker || index < 0 || !m_entranceDelayServiceCombo)
		return;
	SceneEntranceDelay* delay = selectedEntranceDelay();
	if (!delay)
		return;
	const std::string serviceId = m_entranceDelayServiceCombo->itemData(index).toString().toStdString();
	if (serviceId == delay->serviceId)
		return;
	delay->serviceId = serviceId;
	delay->occurrence = 1;
	delay->stationId.clear();
	for (const SceneService& service : m_sceneModel.services) {
		if (service.id != serviceId)
			continue;
		for (const SceneStop& stop : service.stops) {
			if (stop.hasPlannedDeparture) {
				delay->stationId = stop.stationId;
				break;
			}
		}
		break;
	}
	markScenarioModified();
	refreshValidationPanel();
	refreshEntranceDelayPanel();
}

void MainWindow::commitEntranceDelayOccurrence() {
	if (!m_sceneLoaded || m_worker || !m_entranceDelayOccurrenceEdit)
		return;
	SceneEntranceDelay* delay = selectedEntranceDelay();
	if (!delay)
		return;
	const int occurrence = m_entranceDelayOccurrenceEdit->value();
	if (delay->occurrence == occurrence)
		return;
	delay->occurrence = occurrence;
	markScenarioModified();
	refreshValidationPanel();
	refreshEntranceDelayPanel();
}

void MainWindow::commitEntranceDelayStation(int index) {
	if (!m_sceneLoaded || m_worker || index < 0 || !m_entranceDelayStationCombo)
		return;
	SceneEntranceDelay* delay = selectedEntranceDelay();
	if (!delay)
		return;
	const std::string stationId = m_entranceDelayStationCombo->itemData(index).toString().toStdString();
	if (delay->stationId == stationId)
		return;
	delay->stationId = stationId;
	markScenarioModified();
	refreshValidationPanel();
	refreshEntranceDelayPanel();
}

void MainWindow::commitEntranceDelaySeconds() {
	if (!m_sceneLoaded || m_worker || !m_entranceDelaySecondsEdit)
		return;
	SceneEntranceDelay* delay = selectedEntranceDelay();
	if (!delay)
		return;
	const double seconds = m_entranceDelaySecondsEdit->value();
	if (delay->delaySeconds == seconds)
		return;
	delay->delaySeconds = seconds;
	markScenarioModified();
	refreshValidationPanel();
	refreshEntranceDelayPanel();
}

void MainWindow::addBlankScenario() {
	if (!m_sceneLoaded)
		return;
	SceneScenario scenario;
	scenario.id = sceneUniqueScenarioId(m_sceneModel, "new_scenario");
	scenario.name = "New scenario";
	m_sceneModel.scenarios.push_back(std::move(scenario));
	m_selectedScenarioId = m_sceneModel.scenarios.back().id;
	markScenarioModified();
	refreshIncidentPanel();
	refreshValidationPanel();
}

void MainWindow::duplicateScenario() {
	const SceneScenario* selected = selectedScenario();
	if (!selected)
		return;
	SceneScenario copy = *selected;
	copy.id = sceneUniqueScenarioId(m_sceneModel, copy.id + "_copy");
	copy.name = "Copy of " + copy.name;
	for (auto& incident : copy.incidents)
		incident.id = sceneUniqueIncidentId(m_sceneModel, incident.id + "_copy");
	m_sceneModel.scenarios.push_back(std::move(copy));
	m_selectedScenarioId = m_sceneModel.scenarios.back().id;
	markScenarioModified();
	refreshIncidentPanel();
	refreshValidationPanel();
}

void MainWindow::deleteScenario() {
	if (!m_sceneLoaded || m_worker)
		return;
	SceneScenario* selected = selectedScenario();
	const auto persistedDefault = std::find_if(m_sceneModel.scenarios.begin(), m_sceneModel.scenarios.end(),
		[this](const SceneScenario& scenario) { return scenario.id == m_sceneModel.defaultScenarioId; });
	if (!selected || m_sceneModel.defaultScenarioId.empty() || persistedDefault == m_sceneModel.scenarios.end()) {
		showBlockingError(this, "Cannot Delete Scenario",
			"The scene has no valid persisted default scenario; correct validation before deleting scenarios.", true);
		return;
	}
	const std::string defaultId = m_sceneModel.defaultScenarioId;
	if (selected->id == defaultId) {
		showBlockingError(this, "Cannot Delete Scenario", "The default scenario cannot be deleted.", true);
		updateScenarioDetailPanel();
		return;
	}

	const std::string deletedId = selected->id;
	const QMessageBox::StandardButton answer = QMessageBox::question(
		this, "Delete Scenario",
		QString("Delete scenario '%1'?").arg(QString::fromStdString(deletedId)),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (answer != QMessageBox::Yes)
		return;

	auto it = std::find_if(m_sceneModel.scenarios.begin(), m_sceneModel.scenarios.end(),
		[&deletedId](const SceneScenario& scenario) { return scenario.id == deletedId; });
	if (it == m_sceneModel.scenarios.end())
		return;
	m_sceneModel.scenarios.erase(it);
	m_modifiedScenarioIds.erase(deletedId);
	m_selectedScenarioId = defaultId;
	markSceneDirty();
	refreshIncidentPanel();
	refreshValidationPanel();
}

void MainWindow::exportScenario() {
	const auto operation = captureTelemetryOperation();
	const SceneScenario* scenario = selectedScenario();
	if (!scenario)
		return;
	QString path = QFileDialog::getSaveFileName(this, "Export Scenario", scenarioContext() + ".json",
		"Scenario JSON (*.json)");
	if (path.isEmpty())
		return;
	if (!path.endsWith(".json", Qt::CaseInsensitive))
		path += ".json";
	const SceneSaveResult result = saveScenarioJson(*scenario, path.toStdString());
	if (!result.success()) {
		operation.failure(telemetry::Operation::Export, telemetryDiagnosticError(result.diagnostics));
		showBlockingError(this, "Scenario export failed", firstDiagnosticMessage(result.diagnostics), true);
		return;
	}
	statusBar()->showMessage(QString("Exported scenario %1").arg(scenarioContext()), 4000);
}

void MainWindow::importScenario() {
	if (!m_sceneLoaded)
		return;
	const QString path = QFileDialog::getOpenFileName(this, "Import Scenario", QDir::homePath(),
		"Scenario JSON (*.json)");
	if (path.isEmpty())
		return;
	const ScenarioLoadResult loaded = loadScenarioJson(path.toStdString());
	if (!loaded.success()) {
		showBlockingError(this, "Scenario import failed", firstDiagnosticMessage(loaded.diagnostics), true);
		return;
	}
	SceneScenario imported = loaded.scenario;
	const std::string requestedId = imported.id;
	const std::string uniqueId = sceneUniqueScenarioId(m_sceneModel, imported.id);
	const bool scenarioConflict = uniqueId != imported.id;
	if (scenarioConflict)
		imported.id = uniqueId;
	int adjustedIncidentCount = 0;
	for (auto& incident : imported.incidents) {
		const std::string uniqueIncident = sceneUniqueIncidentId(m_sceneModel, incident.id);
		if (uniqueIncident != incident.id) {
			incident.id = uniqueIncident;
			++adjustedIncidentCount;
		}
	}

	SceneModel candidate = m_sceneModel;
	candidate.scenarios.push_back(imported);
	const std::size_t candidateIndex = candidate.scenarios.size() - 1;
	const std::string prefix = "scenarios[" + std::to_string(candidateIndex) + "]";
	QStringList errors;
	for (const auto& diagnostic : validateScene(candidate)) {
		if (diagnostic.severity == SceneSeverity::Error && diagnostic.path.rfind(prefix, 0) == 0)
			errors << QString::fromStdString(diagnostic.message);
	}
	if (!errors.isEmpty()) {
		showBlockingError(this, "Scenario import rejected", errors.join("\n"), true);
		return;
	}
	m_sceneModel.scenarios.push_back(std::move(imported));
	m_selectedScenarioId = m_sceneModel.scenarios.back().id;
	markScenarioModified();
	refreshIncidentPanel();
	refreshValidationPanel();
	QString message = QString("Imported scenario %1").arg(QString::fromStdString(m_selectedScenarioId));
	if (scenarioConflict)
		message += QString(" (ID %1 already existed; imported as %2)")
					   .arg(QString::fromStdString(requestedId), QString::fromStdString(m_selectedScenarioId));
	if (adjustedIncidentCount > 0)
		message += QString(" %1 incident ID(s) adjusted to remain globally unique.")
					   .arg(adjustedIncidentCount);
	statusBar()->showMessage(message, 7000);
	if ((scenarioConflict || adjustedIncidentCount > 0) && !e2eDialogsSuppressed())
		QMessageBox::information(this, "Scenario ID adjusted", message);
}

void MainWindow::commitScenarioIdEdit() {
	SceneScenario* scenario = selectedScenario();
	if (!scenario || !m_scenarioIdEdit)
		return;
	const std::string newId = m_scenarioIdEdit->text().trimmed().toStdString();
	if (newId == scenario->id)
		return;
	if (newId.empty()) {
		showBlockingError(this, "Invalid Scenario ID", "Scenario IDs must not be empty.", true);
		updateScenarioDetailPanel();
		return;
	}
	if (sceneUniqueScenarioId(m_sceneModel, newId) != newId) {
		showBlockingError(this, "Scenario ID already exists", "Choose a unique scenario ID.", true);
		updateScenarioDetailPanel();
		return;
	}
	const std::string oldId = scenario->id;
	scenario->id = newId;
	sceneRewriteScenarioReferences(m_sceneModel, oldId, newId);
	m_selectedScenarioId = newId;
	m_modifiedScenarioIds.erase(oldId);
	markScenarioModified();
	refreshValidationPanel();
}

void MainWindow::commitScenarioNameEdit() {
	SceneScenario* scenario = selectedScenario();
	if (!scenario || !m_scenarioNameEdit)
		return;
	const std::string value = m_scenarioNameEdit->text().toStdString();
	if (value == scenario->name)
		return;
	scenario->name = value;
	markScenarioModified();
	refreshValidationPanel();
}

void MainWindow::commitScenarioDescriptionEdit() {
	SceneScenario* scenario = selectedScenario();
	if (!scenario || !m_scenarioDescriptionEdit)
		return;
	const std::string value = m_scenarioDescriptionEdit->text().toStdString();
	if (value == scenario->description)
		return;
	scenario->description = value;
	markScenarioModified();
	refreshValidationPanel();
}

void MainWindow::updateIncidentDetailPanel() {
	const auto& incidents = selectedScenarioIncidents();
	int row = m_incidentListWidget ? m_incidentListWidget->currentRow() : -1;
	bool hasSelection = m_sceneLoaded && !m_worker && row >= 0
		&& row < static_cast<int>(incidents.size());
	const bool isBreakdown = hasSelection && incidents[static_cast<std::size_t>(row)].type == "train_breakdown";

	if (m_incidentIdEdit) {
		const QSignalBlocker blocker(m_incidentIdEdit);
		m_incidentIdEdit->setText(hasSelection ? QString::fromStdString(incidents[row].id) : QString());
		m_incidentIdEdit->setEnabled(hasSelection);
	}

	if (m_incidentTypeCombo) {
		const QSignalBlocker blocker(m_incidentTypeCombo);
		if (hasSelection) {
			QString currentType = QString::fromStdString(incidents[row].type);
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
		int seconds = hasSelection ? static_cast<int>(incidents[row].startSeconds) : 0;
		m_incidentStartSecondsEdit->setText(QString::number(seconds));
		m_incidentStartSecondsEdit->setEnabled(hasSelection);
	}

	if (m_incidentEndSecondsEdit) {
		const QSignalBlocker blocker(m_incidentEndSecondsEdit);
		int seconds = hasSelection ? static_cast<int>(incidents[row].endSeconds) : 0;
		m_incidentEndSecondsEdit->setText(QString::number(seconds));
		m_incidentEndSecondsEdit->setEnabled(hasSelection);
	}
	if (m_incidentHasEndSecondsCheck) {
		const QSignalBlocker blocker(m_incidentHasEndSecondsCheck);
		const bool hasEnd = hasSelection
			&& (incidents[static_cast<std::size_t>(row)].hasEndSeconds || incidents[static_cast<std::size_t>(row)].endSeconds != 0.0);
		m_incidentHasEndSecondsCheck->setChecked(hasEnd);
		m_incidentHasEndSecondsCheck->setEnabled(hasSelection);
	}
	if (m_incidentHasReducedSpeedCheck) {
		const QSignalBlocker blocker(m_incidentHasReducedSpeedCheck);
		const bool hasCap = isBreakdown
			&& (incidents[static_cast<std::size_t>(row)].hasReducedSpeed || incidents[static_cast<std::size_t>(row)].reducedSpeedKmh != 0.0);
		m_incidentHasReducedSpeedCheck->setChecked(hasCap);
		m_incidentHasReducedSpeedCheck->setEnabled(isBreakdown);
	}
	if (m_incidentReducedSpeedKmhEdit) {
		const QSignalBlocker blocker(m_incidentReducedSpeedKmhEdit);
		const double cap = hasSelection && std::isfinite(incidents[static_cast<std::size_t>(row)].reducedSpeedKmh)
				&& incidents[static_cast<std::size_t>(row)].reducedSpeedKmh > 0.0
			? incidents[static_cast<std::size_t>(row)].reducedSpeedKmh
			: 40.0;
		m_incidentReducedSpeedKmhEdit->setValue(cap);
		m_incidentReducedSpeedKmhEdit->setEnabled(isBreakdown
			&& m_incidentHasReducedSpeedCheck && m_incidentHasReducedSpeedCheck->isChecked());
	}
	if (m_incidentTerminateAtDestinationCheck) {
		const QSignalBlocker blocker(m_incidentTerminateAtDestinationCheck);
		m_incidentTerminateAtDestinationCheck->setChecked(isBreakdown && hasSelection
			&& incidents[static_cast<std::size_t>(row)].terminateAtDestination);
		m_incidentTerminateAtDestinationCheck->setEnabled(isBreakdown);
	}

	if (m_duplicateIncidentButton)
		m_duplicateIncidentButton->setEnabled(hasSelection);
	if (m_deleteIncidentButton)
		m_deleteIncidentButton->setEnabled(hasSelection);
}

void MainWindow::refreshIncidentTargetCombo() {
	if (!m_incidentTargetCombo)
		return;
	const SceneIncident* incident = selectedIncident();
	const QSignalBlocker blocker(m_incidentTargetCombo);
	m_incidentTargetCombo->clear();
	m_incidentTargetCombo->addItem(QStringLiteral("Choose target"), QString());
	int current = 0;
	if (incident) {
		const QString target = QString::fromStdString(incident->target);
		bool found = false;
		if (incident->type == "signal_failure") {
			for (const std::string& value : signalFailureTargets(m_sceneModel))
				m_incidentTargetCombo->addItem(QString::fromStdString(value), QString::fromStdString(value));
			current = m_incidentTargetCombo->findData(target);
			if (current < 0 && !target.isEmpty()) {
				m_incidentTargetCombo->addItem(QStringLiteral("Invalid removed target: ") + target, target);
				current = m_incidentTargetCombo->count() - 1;
			}
		} else {
			const bool singular = incident->hasOccurrence || incident->occurrence != 1;
			if (!singular && !incident->target.empty()) {
				const bool exists = std::any_of(m_sceneModel.services.begin(), m_sceneModel.services.end(),
					[&](const SceneService& service) { return service.id == incident->target; });
				m_incidentTargetCombo->addItem(
					(exists ? QStringLiteral("Historical scope: all generated services [")
							: QStringLiteral("Invalid removed target; historical all scope ["))
						+ target + QStringLiteral("]"),
					target);
				const int index = m_incidentTargetCombo->count() - 1;
				m_incidentTargetCombo->setItemData(index, 0, Qt::UserRole + 1);
				current = index;
			}
			int shown = 0;
			for (const SceneService& service : m_sceneModel.services) {
				const int count = std::max(0,
					sceneServiceOccurrenceCount(service, serviceOccurrenceDuration()));
				for (int occurrence = 1; occurrence <= count && shown < Max_N_Reg; ++occurrence, ++shown) {
					m_incidentTargetCombo->addItem(generatedServiceLabel(service, occurrence),
						QString::fromStdString(service.id));
					const int index = m_incidentTargetCombo->count() - 1;
					m_incidentTargetCombo->setItemData(index, occurrence, Qt::UserRole + 1);
					if (singular && service.id == incident->target && occurrence == incident->occurrence) {
						current = index;
						found = true;
					}
				}
				if (shown >= Max_N_Reg)
					break;
			}
			if (singular && !found && !incident->target.empty()) {
				for (const SceneService& service : m_sceneModel.services) {
					if (service.id != incident->target)
						continue;
					const int count = std::max(0,
						sceneServiceOccurrenceCount(service, serviceOccurrenceDuration()));
					if (incident->occurrence < 1 || incident->occurrence > count)
						break;
					m_incidentTargetCombo->addItem(generatedServiceLabel(service, incident->occurrence),
						QString::fromStdString(service.id));
					current = m_incidentTargetCombo->count() - 1;
					m_incidentTargetCombo->setItemData(current, incident->occurrence, Qt::UserRole + 1);
					found = true;
					break;
				}
			}
		}
		if (incident->type != "signal_failure" && !incident->target.empty()
			&& ((incident->hasOccurrence || incident->occurrence != 1) ? !found : current == 0)) {
			m_incidentTargetCombo->addItem(
				QStringLiteral("Invalid removed target [") + target + QStringLiteral(", ")
					+ QString::number(incident->occurrence) + QStringLiteral("]"),
				target);
			current = m_incidentTargetCombo->count() - 1;
			m_incidentTargetCombo->setItemData(current,
				incident->hasOccurrence || incident->occurrence != 1 ? incident->occurrence : 0,
				Qt::UserRole + 1);
		}
	}
	QHash<QString, int> labelCounts;
	for (int index = 0; index < m_incidentTargetCombo->count(); ++index)
		++labelCounts[m_incidentTargetCombo->itemText(index)];
	for (int index = 0; index < m_incidentTargetCombo->count(); ++index) {
		const QString label = m_incidentTargetCombo->itemText(index);
		const QString identity = QString(" [%1, #%2]")
									 .arg(m_incidentTargetCombo->itemData(index).toString())
									 .arg(m_incidentTargetCombo->itemData(index, Qt::UserRole + 1).toInt());
		if (labelCounts.value(label) > 1)
			m_incidentTargetCombo->setItemText(index, label + identity);
		m_incidentTargetCombo->setItemData(index, label + identity, Qt::ToolTipRole);
	}
	m_incidentTargetCombo->setCurrentIndex(std::max(0, current));
	m_incidentTargetCombo->setEnabled(incident != nullptr && !m_worker);
	m_incidentTargetCombo->setToolTip(
		QStringLiteral("Service code with route, elapsed scheduled entry and stable identity."));
}

void MainWindow::addIncident() {
	if (!m_sceneLoaded)
		return;

	auto& incidents = selectedScenarioIncidents();
	SceneIncident incident;
	incident.id = sceneUniqueIncidentId(m_sceneModel, "new_incident");
	incident.type = "signal_failure";
	incident.hasEndSeconds = true;
	incidents.push_back(incident);

	markScenarioModified();
	refreshValidationPanel();
	refreshIncidentPanel();

	if (m_incidentListWidget)
		m_incidentListWidget->setCurrentRow(static_cast<int>(incidents.size()) - 1);
}

void MainWindow::duplicateIncident() {
	if (!m_sceneLoaded || !m_incidentListWidget)
		return;
	auto& incidents = selectedScenarioIncidents();
	int row = m_incidentListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(incidents.size()))
		return;

	SceneIncident duplicate = incidents[row];
	duplicate.id = sceneUniqueIncidentId(m_sceneModel, duplicate.id + "_copy");
	incidents.insert(incidents.begin() + row + 1, duplicate);

	markScenarioModified();
	refreshValidationPanel();
	refreshIncidentPanel();

	if (m_incidentListWidget)
		m_incidentListWidget->setCurrentRow(row + 1);
}

void MainWindow::deleteIncident() {
	if (!m_sceneLoaded || !m_incidentListWidget)
		return;
	auto& incidents = selectedScenarioIncidents();
	int row = m_incidentListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(incidents.size()))
		return;

	QString id = QString::fromStdString(incidents[row].id);
	auto response = QMessageBox::question(this,
		"Delete Incident",
		QString("Delete incident '%1'?").arg(id),
		QMessageBox::Yes | QMessageBox::No,
		QMessageBox::No);
	if (response != QMessageBox::Yes)
		return;

	incidents.erase(incidents.begin() + row);

	markScenarioModified();
	refreshIncidentPanel();
	refreshValidationPanel();
}

void MainWindow::commitIncidentIdEdit() {
	if (!m_sceneLoaded || !m_incidentListWidget || !m_incidentIdEdit)
		return;
	auto& incidents = selectedScenarioIncidents();
	int row = m_incidentListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(incidents.size()))
		return;

	std::string newId = m_incidentIdEdit->text().trimmed().toStdString();
	if (newId == incidents[row].id)
		return;
	if (newId.empty()) {
		showBlockingError(this, "Invalid Incident ID", "Incident IDs must not be empty.", true);
		updateIncidentDetailPanel();
		return;
	}
	if (sceneUniqueIncidentId(m_sceneModel, newId) != newId) {
		showBlockingError(this, "Incident ID already exists",
			"Incident IDs must be unique across all scenarios.", true);
		updateIncidentDetailPanel();
		return;
	}

	incidents[row].id = newId;

	// update the list row label in place instead of rebuilding the panel, so a
	// focus-out that lands on another control keeps that pending click intact
	if (QListWidgetItem* item = m_incidentListWidget->item(row)) {
		const QSignalBlocker blocker(m_incidentListWidget);
		QString label = QString::fromStdString(newId) + " (" + QString::fromStdString(incidents[row].type) + ")";
		item->setText(label);
	}

	markScenarioModified();
	refreshValidationPanel();
}

void MainWindow::commitIncidentType(const QString& text) {
	if (!m_sceneLoaded || !m_incidentListWidget)
		return;
	auto& incidents = selectedScenarioIncidents();
	int row = m_incidentListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(incidents.size()))
		return;

	std::string newType = text.toStdString();
	if (newType == incidents[row].type)
		return;

	incidents[row].type = newType;
	if (newType == "signal_failure") {
		incidents[row].hasOccurrence = false;
		incidents[row].occurrence = 1;
		incidents[row].hasReducedSpeed = false;
		incidents[row].reducedSpeedKmh = 0.0;
		incidents[row].terminateAtDestination = false;
	}

	// when the type changes the valid target pool also changes; drop a target
	// that is no longer valid for the new type
	bool targetValid = incidents[row].target.empty();
	if (!targetValid) {
		if (newType == "signal_failure") {
			const auto targets = signalFailureTargets(m_sceneModel);
			targetValid = std::find(targets.begin(), targets.end(), incidents[row].target) != targets.end();
		} else {
			for (const auto& service : m_sceneModel.services) {
				if (service.id == incidents[row].target) {
					targetValid = true;
					break;
				}
			}
		}
	}
	if (!targetValid)
		incidents[row].target.clear();

	// update the list row label in place to reflect the new type
	if (QListWidgetItem* item = m_incidentListWidget->item(row)) {
		const QSignalBlocker blocker(m_incidentListWidget);
		QString label = QString::fromStdString(incidents[row].id) + " (" + text + ")";
		item->setText(label);
	}

	markScenarioModified();
	refreshValidationPanel();

	// The target choices and breakdown-only controls both depend on the type.
	updateIncidentDetailPanel();
}

void MainWindow::commitIncidentTarget(const QString&) {
	if (!m_sceneLoaded || m_worker || !m_incidentTargetCombo)
		return;
	SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	const std::string newTarget = m_incidentTargetCombo->currentData().toString().toStdString();
	int occurrence = m_incidentTargetCombo->currentData(Qt::UserRole + 1).toInt();
	const bool singular = incident->type != "signal_failure" && occurrence > 0;
	occurrence = std::max(1, occurrence);
	if (incident->target == newTarget && incident->hasOccurrence == singular
		&& incident->occurrence == occurrence)
		return;
	incident->target = newTarget;
	incident->hasOccurrence = singular;
	incident->occurrence = occurrence;
	markScenarioModified();
	refreshValidationPanel();
	updateIncidentDetailPanel();
}

void MainWindow::commitIncidentStartSeconds() {
	if (!m_sceneLoaded || !m_incidentListWidget || !m_incidentStartSecondsEdit)
		return;
	auto& incidents = selectedScenarioIncidents();
	int row = m_incidentListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(incidents.size()))
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
	if (newValue == incidents[row].startSeconds)
		return;

	incidents[row].startSeconds = newValue;

	markScenarioModified();
	refreshValidationPanel();
}

void MainWindow::commitIncidentEndSeconds() {
	if (!m_sceneLoaded || !m_incidentListWidget || !m_incidentEndSecondsEdit)
		return;
	auto& incidents = selectedScenarioIncidents();
	int row = m_incidentListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(incidents.size()))
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
	if (newValue == incidents[row].endSeconds && incidents[row].hasEndSeconds)
		return;

	incidents[row].endSeconds = newValue;
	incidents[row].hasEndSeconds = true;

	markScenarioModified();
	refreshValidationPanel();
}

void MainWindow::commitIncidentReducedSpeed() {
	if (!m_sceneLoaded || !m_incidentReducedSpeedKmhEdit)
		return;
	SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	const double speed = m_incidentReducedSpeedKmhEdit->value();
	if (incident->reducedSpeedKmh == speed && incident->hasReducedSpeed)
		return;
	incident->reducedSpeedKmh = speed;
	incident->hasReducedSpeed = true;
	markScenarioModified();
	refreshValidationPanel();
	refreshIncidentPanel();
}

void MainWindow::commitIncidentHasReducedSpeed(bool checked) {
	if (!m_sceneLoaded)
		return;
	SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	if (incident->hasReducedSpeed == checked)
		return;
	incident->hasReducedSpeed = checked;
	if (checked && m_incidentReducedSpeedKmhEdit)
		incident->reducedSpeedKmh = m_incidentReducedSpeedKmhEdit->value();
	else if (!checked)
		incident->reducedSpeedKmh = 0.0;
	markScenarioModified();
	refreshValidationPanel();
	updateIncidentDetailPanel();
}

void MainWindow::commitIncidentHasEndSeconds(bool checked) {
	if (!m_sceneLoaded)
		return;
	SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	if (incident->hasEndSeconds == checked)
		return;
	incident->hasEndSeconds = checked;
	if (!checked)
		incident->endSeconds = 0.0;
	markScenarioModified();
	refreshValidationPanel();
	updateIncidentDetailPanel();
}

void MainWindow::commitIncidentTerminateAtDestination(bool checked) {
	if (!m_sceneLoaded)
		return;
	SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	if (incident->terminateAtDestination == checked)
		return;
	incident->terminateAtDestination = checked;
	markScenarioModified();
	refreshValidationPanel();
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
		m_caseNameLabel->setText(caseName);

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
	if (!m_showingTrackPreview && numRegions > 0) {
		for (int train = 0; train < numRegions; ++train) {
			QString label = QString::fromStdString(regional_train[train].trainDescription);
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
	in.trainsLayerVisible = m_trainLayerVisible;
	in.clockOffsetSeconds = m_startOffsetSeconds;
	if (m_snapshot)
		in.timestep = m_snapshot->timestep;
	const int train = m_followTrainCombo && row >= 0 ? m_followTrainCombo->itemData(row).toInt() : -1;
	in.hasRun = train >= 0 && !m_showingTrackPreview && numRegions > 0;
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
	const auto item = std::find_if(allTrains.cbegin(), allTrains.cend(),
		[trainIndex](const TrainItemGroup* candidate) { return candidate && candidate->index == trainIndex; });
	if (item == allTrains.cend() || !(*item)->trainPolygonItemList)
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
	if (trainIndex >= numRegions || (m_followTrainCombo && comboIndex < 0)) {
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
	clearOperationalTrackStates();
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
	m_showingTrackPreview = false;

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

	// Reuse drawing levels once track spans stop overlapping when the scene has
	// no authored display layout.
	struct TrackSpan {
		int track;
		double begin;
		double end;
	};
	std::vector<TrackSpan> trackSpans;
	for (int track = 0; track < numTrackLines; ++track) {
		if (blockSets[track].hasGraphLayout)
			continue;
		if (blockSets[track].len <= 0) {
			blockSets[track].graphID = 0;
			continue;
		}
		double begin = std::numeric_limits<double>::max();
		double end = std::numeric_limits<double>::lowest();
		for (int arc = 0; arc < blockSets[track].len; ++arc) {
			begin = std::min({begin, blockSets[track].member[arc].startNode.X,
				blockSets[track].member[arc].endNode.X});
			end = std::max({end, blockSets[track].member[arc].startNode.X,
				blockSets[track].member[arc].endNode.X});
		}
		trackSpans.push_back({track, begin, end});
	}
	std::sort(trackSpans.begin(), trackSpans.end(), [](const TrackSpan& left, const TrackSpan& right) {
		return left.begin < right.begin || (left.begin == right.begin && left.track < right.track);
	});
	std::vector<double> levelEnds;
	for (const TrackSpan& span : trackSpans) {
		auto available = std::find_if(levelEnds.begin(), levelEnds.end(), [&span](double end) {
			return end < span.begin;
		});
		if (available == levelEnds.end()) {
			blockSets[span.track].graphID = static_cast<int>(levelEnds.size());
			levelEnds.push_back(span.end);
		} else {
			blockSets[span.track].graphID = static_cast<int>(std::distance(levelEnds.begin(), available));
			*available = span.end;
		}
	}
	// Use a single fallback region for stations without authored view metadata.
	for (int station = 0; station < numStations; ++station) {
		if (StationArray[station].regions.empty()) {
			StationArray[station].regions.push_back(0);
			StationArray[station].regionX[0] = StationArray[station].X;
		}
	}

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
	const bool hasSharedPreview = !m_cachedTrackPreview.lines.empty();
	const auto sharedStationPoint = [this, hasSharedPreview](int stationIndex, QPointF& point) {
		if (!hasSharedPreview || stationIndex < 0
			|| stationIndex >= static_cast<int>(m_sceneModel.stations.size()))
			return false;
		const auto& source = m_sceneModel.stations[static_cast<std::size_t>(stationIndex)];
		const auto station = std::find_if(m_cachedTrackPreview.stations.begin(),
			m_cachedTrackPreview.stations.end(), [&source](const TrackPreviewStation& candidate) {
				return (!source.id.empty() && candidate.id == source.id)
					|| (source.id.empty() && candidate.name == source.name);
			});
		if (station == m_cachedTrackPreview.stations.end())
			return false;
		for (const auto& line : m_cachedTrackPreview.lines) {
			if (!station->nodeId.empty()) {
				if (previewPointAtNode(line, station->nodeId,
						static_cast<qreal>(line.displayOffset), point))
					return true;
				continue;
			}
			if (line.points.empty())
				continue;
			const double minX = std::min(line.points.front().rawX, line.points.back().rawX);
			const double maxX = std::max(line.points.front().rawX, line.points.back().rawX);
			if (station->x < minX || station->x > maxX)
				continue;
			if (previewPointAtX(line, station->x,
					static_cast<qreal>(line.displayOffset), point))
				return true;
		}
		return false;
	};

	// draw station icons and print names
	for (int i = 0; i < numStations; i++) {
		// ignore virtual stations (name contains "virtual")
		if (StationArray[i].stationName.find("virtual") != std::string::npos) {
			continue;
		}
		QPointF sharedPoint;
		if (sharedStationPoint(i, sharedPoint)) {
			paintStationOverlay(sharedPoint,
				classifyStation(),
				StationArray[i].stationName, presentationScale(),
				stationDecorationOffset(m_sceneModel.stationViews,
					m_sceneModel.stations[static_cast<std::size_t>(i)].id,
					StationArray[i].stationName)
					* presentationScale(),
				static_cast<int>(m_sceneModel.stations[static_cast<std::size_t>(i)].platforms.size()));
			continue;
		}

		// Keep the native graph position as the source anchor when no shared line resolves.
		pt = QPointF(StationArray[i].graphX, StationArray[i].graphY);
		QPointF averageShift;
		int validRegions = 0;
		for (int region : StationArray[i].regions) {
			const double x = StationArray[i].shiftX[region];
			const double y = StationArray[i].shiftY[region];
			if (std::isfinite(x) && std::isfinite(y)) {
				averageShift += QPointF(x, y);
				++validRegions;
			}
		}
		if (validRegions > 0)
			averageShift /= validRegions;
		const QPointF decorationOffset = averageShift * (station_name_graphID * track_separation)
			+ QPointF(0.0, stationNamedYOffset(StationArray[i].stationName));

		paintStationOverlay(pt,
			classifyStation(),
			StationArray[i].stationName, 1.0, decorationOffset,
			i < static_cast<int>(m_sceneModel.stations.size())
				? static_cast<int>(m_sceneModel.stations[static_cast<std::size_t>(i)].platforms.size())
				: 0);
	}

	// draw connections
	QPointF station1, station2;
	int st_index;
	QPointF ptc1, ptc2;
	for (int c = 0; c < numConnections; c++) {
		if (hasSharedPreview && (!cachedTrackLine(connections[c].idFirstTrackLine) || !cachedTrackLine(connections[c].idSecondTrackLine)))
			continue;
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
		if (hasSharedPreview && !cachedTrackLine(track))
			continue;
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
				paintStationNode(pt, station_node_size, line_width, track, &blockSets[track].member[i - 1].endNode, false);
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
				paintArc(pt_prev, pt, line_width, track, &blockSets[track].member[i - 1], track_separation);
			}

			// draw last Arc and last Node - end_node Node needed
			if (i == (blockSets[track].len - 1)) {
				// draw last Arc
				paintArc(pt, pte, line_width, track, &blockSets[track].member[i], track_separation);

				// draw last end_node Node
				if (!empty(blockSets[track].member[i].endNode.stationName)) { // station Node
					paintStationNode(pte, station_node_size, line_width, track, &blockSets[track].member[i].endNode, true);
					if (initial_variables.PAX_GUI) {
						paintStationPlatform(pte, station_node_size, line_width, &blockSets[track].member[i].endNode);
					}
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
			if (hasSharedPreview && !cachedTrackLine(signalling_block_sections[i].trackLineId))
				continue;

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
	buildTrackIndexes();

	bindStationOverlaySources();
	if (!hasSharedPreview)
		updateStationOverlayDegrees();
	updateViewportOverlays();

	refreshFollowTrainChoices();

	// hide unused menus
	// ui->menuFile->setTitle("");
	// View menu is populated from the .ui file
	// ui->menuTools->setTitle("");
	// ui->menuAbout->setTitle("");
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
	for (int i = 0; i < numRegions; ++i)
		if (regional_train[i].trajectorySize() > 0)
			return true;
	return false;
}

// Raw trajectories can survive a scene edit; the explicit availability flag is
// the ownership boundary for result actions.
bool MainWindow::hasRunResults() const {
	return m_resultsAvailable && hasRawRunResults();
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
		&& current.hasIncidents && !current.hasEntranceDelays;
	if (m_setDelayBaselineButton) {
		m_setDelayBaselineButton->setEnabled(canSetBaseline);
		m_setDelayBaselineButton->setToolTip(canSetBaseline
				? QStringLiteral("Freeze this completed run with no incidents or entrance delays as the delay baseline")
				: QStringLiteral("Complete a run with no incidents or entrance delays before setting a delay baseline"));
	}
	if (m_compareDelayButton) {
		m_compareDelayButton->setEnabled(canCompare);
		m_compareDelayButton->setToolTip(canCompare
				? QStringLiteral("Compare the completed incident run with the frozen delay baseline")
				: QStringLiteral("Run a completed incident scenario without entrance delays after setting a baseline"));
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
		feedback = QStringLiteral("Baseline: %1. Compare the completed incident scenario; the metric is positive additional final-arrival delay.")
					   .arg(completedRunContext(m_delayBaseline->provenance));
	} else if (current.hasEntranceDelays) {
		feedback = QStringLiteral("Baseline: %1. Comparison requires a completed incident scenario with no entrance delays.")
					   .arg(completedRunContext(m_delayBaseline->provenance));
	} else if (!current.hasIncidents) {
		feedback = QStringLiteral("Baseline: %1. Run an incident scenario without entrance delays, then compare.")
					   .arg(completedRunContext(m_delayBaseline->provenance));
	}
	if (!current.scenarioId.empty() && !m_selectedScenarioId.empty()
		&& current.scenarioId != m_selectedScenarioId) {
		feedback += QStringLiteral(" Selected next scenario: %1; it has not been run yet.")
						.arg(QString::fromStdString(m_selectedScenarioId));
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
	const QString sceneDirBefore = m_sceneDir;
	const quint64 sceneRevisionBefore = m_sceneRevision;

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
	QListWidget* list = dialog.list();
	QPushButton* openBtn = dialog.openButton();
	QPushButton* continueBtn = dialog.continueButton();

	const bool startupChooserE2E = qEnvironmentVariableIsSet("QEGTRAIN_E2E_STARTUP_CHOOSER");
	QVariantMap chooserContract;
	if (startupChooserE2E) {
		const auto rows = [list]() {
			QVariantList result;
			for (int row = 0; row < list->count(); ++row) {
				const auto* item = list->item(row);
				result.append(QVariantMap{{"title", item->text()}, {"path", item->data(Qt::UserRole)},
					{"enabled", bool(item->flags() & Qt::ItemIsEnabled)},
					{"selectable", bool(item->flags() & Qt::ItemIsSelectable)}, {"tooltip", item->toolTip()}});
			}
			return result;
		};
		chooserContract.insert("rows", rows());
		chooserContract.insert("initialPath", dialog.selectedPath());
		chooserContract.insert("loadedBefore", sceneDirBefore);
		chooserContract.insert("defaultOpen", openBtn->isDefault());
		QStringList menuTexts;
		for (const auto* action : dialog.otherMenu()->actions()) menuTexts.append(action->text());
		chooserContract.insert("otherActions", menuTexts);
		QVariantList recentActions;
		if (m_recentScenesMenu) {
			for (const auto* action : m_recentScenesMenu->actions())
				recentActions.append(QVariantMap{{"title", action->text()}, {"path", action->data()},
					{"enabled", action->isEnabled()}, {"tooltip", action->toolTip()}});
		}
		chooserContract.insert("recentActions", recentActions);
		QTimer::singleShot(0, &dialog, [&]() {
			const QString interaction = qEnvironmentVariable("QEGTRAIN_E2E_CHOOSER_INTERACTION");
			const QString target = qEnvironmentVariable("QEGTRAIN_E2E_CHOOSER_PATH");
			const auto key = [](QWidget* widget, int code) {
				QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier);
				QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier);
				QCoreApplication::sendEvent(widget, &press);
				QCoreApplication::sendEvent(widget, &release);
			};
			const auto mouse = [](QWidget* widget, QEvent::Type type, const QPoint& point) {
				QMouseEvent event(type, QPointF(point), Qt::LeftButton,
					type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
				QCoreApplication::sendEvent(widget, &event);
			};
			QListWidgetItem* targetItem = nullptr;
			for (int row = 0; row < list->count(); ++row)
				if (list->item(row)->data(Qt::UserRole).toString() == target && !target.isEmpty())
					targetItem = list->item(row);
			if (targetItem) {
				list->setFocus();
				if (interaction == "keyboard") {
					key(list, Qt::Key_Home);
					for (int step = 0; step < list->count() && list->currentItem() != targetItem; ++step)
						key(list, Qt::Key_Down);
				} else {
					list->scrollToItem(targetItem);
					const QPoint point = list->visualItemRect(targetItem).center();
					mouse(list->viewport(), QEvent::MouseButtonPress, point);
					mouse(list->viewport(), QEvent::MouseButtonRelease, point);
				}
			}
			chooserContract.insert("selectedPath", dialog.selectedPath());
			chooserContract.insert("openEnabled", openBtn->isEnabled());
			chooserContract.insert("interaction", interaction);
			if (interaction == "keyboard") {
				key(list, Qt::Key_Return);
			} else if (interaction == "doubleclick" && targetItem) {
				const QPoint point = list->visualItemRect(targetItem).center();
				mouse(list->viewport(), QEvent::MouseButtonDblClick, point);
				mouse(list->viewport(), QEvent::MouseButtonRelease, point);
			} else if (interaction == "open") {
				mouse(openBtn, QEvent::MouseButtonPress, openBtn->rect().center());
				mouse(openBtn, QEvent::MouseButtonRelease, openBtn->rect().center());
			} else {
				continueBtn->click();
			}
		});
		// A failed event dispatch must fail the contract rather than hang the subprocess.
		chooserContract.insert("timedOut", false);
		QTimer::singleShot(5000, &dialog, [&]() {
			chooserContract.insert("timedOut", true);
			dialog.reject();
		});
	}
	const int dialogResult = dialog.exec();

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
	if (startupChooserE2E) {
		const auto marker = [](const char* name, const QString& value) {
			std::fprintf(stdout, "%s=%s\n", name, value.toUtf8().constData());
		};
		chooserContract.insert("chosenAction", outcome == CaseChooserDialog::Outcome::OpenSelected ? "OpenSelected" : "ContinueCurrent");
		chooserContract.insert("chosenPath", dialog.chosenPath());
		chooserContract.insert("accepted", dialogResult == QDialog::Accepted);
		chooserContract.insert("loadedAfter", m_sceneDir);
		chooserContract.insert("loaded", m_sceneLoaded);
		marker("E2E_STARTUP_CHOOSER_CONTRACT", QString::fromUtf8(QJsonDocument::fromVariant(chooserContract).toJson(QJsonDocument::Compact)));
		marker("E2E_STARTUP_CHOOSER_PROMPT", dialog.findChild<QLabel*>("dialogContext")->text());
		marker("E2E_STARTUP_CHOOSER_ACTION", continueBtn->text());
		marker("E2E_STARTUP_CHOOSER_GROUP", list->item(0)->text());
		marker("E2E_STARTUP_CHOOSER_GROUP_SELECTABLE",
			list->item(0)->flags() & Qt::ItemIsEnabled ? "yes" : "no");
		marker("E2E_STARTUP_CHOOSER_FIRST", list->currentItem() ? list->currentItem()->text() : QString());
		marker("E2E_STARTUP_CHOOSER_OPEN_ENABLED", openBtn->isEnabled() ? "yes" : "no");
		marker("E2E_STARTUP_CHOOSER_MODEL", QString::fromStdString(m_sceneModel.name));
		marker("E2E_STARTUP_CHOOSER_CASE_LABEL", m_caseNameLabel ? m_caseNameLabel->text() : QString());
		marker("E2E_STARTUP_CHOOSER_READINESS", m_caseReadinessLabel ? m_caseReadinessLabel->text() : QString());
		marker("E2E_STARTUP_CHOOSER_STATUS", statusBar()->currentMessage());
		marker("E2E_STARTUP_CHOOSER_UNCHANGED",
			m_sceneLoaded && m_sceneDir == sceneDirBefore && m_sceneRevision == sceneRevisionBefore
				? QStringLiteral("yes")
				: QStringLiteral("no"));
		std::fflush(stdout);
		QCoreApplication::exit(0);
	}
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
						  .arg(selectedServiceOccurrences())
						  .arg(selectedServiceOccurrencesInPeriod())
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
	const bool reviewCancelE2E = qEnvironmentVariableIsSet("QEGTRAIN_E2E_REVIEW_CANCEL");
	if (e2eDialogsSuppressed() && !reviewCancelE2E)
		return true;

	const quint64 revisionBefore = m_sceneRevision;
	const SceneRunSelection selectionBefore = selectedSceneOccurrences();
	QDialog review(this);
	review.setObjectName("runReviewDialog");
	review.setWindowTitle("Run simulation");
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	layout->setSpacing(8);
	const bool detailsEnabled = advancedDetailsEnabled();
	const SceneDiagnosticCounts counts = countDiagnostics(m_sceneDiagnostics);
	const int selectedInPeriod = selectedServiceOccurrencesInPeriod();
	const double duration = serviceOccurrenceDuration();
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
	QJsonObject reviewDiagnostic;
	if (reviewCancelE2E) {
		const bool enlarged = qEnvironmentVariableIsSet("QEGTRAIN_E2E_REVIEW_ENLARGED");
		if (enlarged) {
			QFont font = review.font();
			font.setPointSizeF(18);
			review.setFont(font);
			// Styled widgets have already resolved their fonts before this test seam.
			for (QWidget* widget : review.findChildren<QWidget*>()) {
				font = widget->font();
				font.setPointSizeF(widget->objectName() == "dialogHeading" ? 22.5 : 18);
				widget->setFont(font);
			}
			review.resize(qMin(560, review.maximumWidth()), qMin(420, review.maximumHeight()));
		}
		QTimer::singleShot(0, &review, [&]() {
			// Modal show and disclosure changes post LayoutRequest events in Qt 5.
			const auto settle = []() {
				for (int i = 0; i < 3; ++i) {
					QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
					QApplication::processEvents();
				}
			};
			settle();
			auto* scroll = review.findChild<QScrollArea*>("dialogBodyScroll");
			auto* context = review.findChild<QLabel*>("dialogContext");
			QScreen* screen = QGuiApplication::screenAt(review.mapToGlobal(QPoint()));
			if (!screen)
				screen = QGuiApplication::primaryScreen();
			const QRect available = screen->availableGeometry();
			reviewDiagnostic.insert("availableScreen", QJsonArray{available.width(), available.height()});
			const auto rectangle = [&](QWidget* widget) {
				const QPoint origin = widget->mapTo(&review, QPoint());
				return QJsonArray{origin.x(), origin.y(), widget->width(), widget->height()};
			};
			const auto geometry = [&]() {
				settle();
				return QJsonObject{{"dialog", QJsonArray{0, 0, review.width(), review.height()}},
					{"maximum", QJsonArray{review.maximumWidth(), review.maximumHeight()}},
					{"scroll", rectangle(scroll)}, {"footer", rectangle(buttons)},
					{"run", rectangle(runButton)}, {"cancel", rectangle(buttons->button(QDialogButtonBox::Cancel))},
					{"scrollMaximum", scroll->verticalScrollBar()->maximum()},
					{"footerVisible", buttons->isVisible()}, {"fontPoints", review.font().pointSizeF()},
					{"controlFontPoints",
						QJsonArray{context->font().pointSizeF(), details->font().pointSizeF(), runButton->font().pointSizeF(),
							buttons->button(QDialogButtonBox::Cancel)->font().pointSizeF()}}};
			};
			reviewDiagnostic.insert("initialFocus", review.focusWidget() == runButton ? "run" : review.focusWidget() ? review.focusWidget()->objectName()
																													 : "none");
			reviewDiagnostic.insert("runDefault", runButton->isDefault());
			reviewDiagnostic.insert("cancelDefault", buttons->button(QDialogButtonBox::Cancel)->isDefault());
			reviewDiagnostic.insert("context", context->text());
			reviewDiagnostic.insert("plainContext", context->textFormat() == Qt::PlainText);
			reviewDiagnostic.insert("plainDetails", details->textFormat() == Qt::PlainText);
			reviewDiagnostic.insert("collapsedGeometry", geometry());
			const auto detailState = [&]() {
				return QJsonObject{{"checked", detailsToggle->isChecked()},
					{"visible", details->isVisible()},
					{"arrow", detailsToggle->arrowType() == Qt::DownArrow ? "down" : "right"}};
			};
			QJsonObject summary;
			for (int row = 0; row < facts->rowCount(); ++row)
				summary.insert(facts->itemAtPosition(row, 0)->widget()->property("text").toString(),
					facts->itemAtPosition(row, 1)->widget()->property("text").toString());
			reviewDiagnostic.insert("summary", summary);
			reviewDiagnostic.insert("status", status->text());
			reviewDiagnostic.insert("warning", status->property("warning").toBool());
			reviewDiagnostic.insert("runEnabled", runButton->isEnabled());
			reviewDiagnostic.insert("collapsed", detailState());
			detailsToggle->click();
			reviewDiagnostic.insert("expanded", detailState());
			reviewDiagnostic.insert("details", details->text());
			reviewDiagnostic.insert("expandedGeometry", geometry());
			scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
			reviewDiagnostic.insert("scrolledGeometry", geometry());

			// Exercise the real focus chain without activating Run or inspection actions.
			detailsToggle->setFocus();
			QWidget* lastBody = detailsToggle;
			QJsonArray bodyFocus;
			QWidget* firstFooter = nullptr;
			for (int i = 0; i < 12; ++i) {
				QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
				QApplication::sendEvent(review.focusWidget(), &tab);
				settle();
				QWidget* focused = review.focusWidget();
				if (focused && buttons->isAncestorOf(focused)) {
					firstFooter = focused;
					break;
				}
				if (!focused || !body->isAncestorOf(focused) || focused == detailsToggle)
					break;
				lastBody = focused;
				bodyFocus.append(focused->property("text").toString());
			}
			scroll->verticalScrollBar()->setValue(0);
			QJsonArray footerBacktab;
			if (firstFooter) {
				for (int i = 0; i <= buttons->buttons().size(); ++i) {
					QKeyEvent backtab(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
					QApplication::sendEvent(review.focusWidget(), &backtab);
					settle();
					QWidget* focused = review.focusWidget();
					if (!focused || !buttons->isAncestorOf(focused))
						break;
					footerBacktab.append(focused->property("text").toString());
				}
			}
			const QRect focusedRect(lastBody->mapTo(scroll->viewport(), QPoint()), lastBody->size());
			reviewDiagnostic.insert("keyboard",
				QJsonObject{{"bodyFocus", bodyFocus},
					{"tabReachedFooter", firstFooter != nullptr},
					{"backtabReturned", firstFooter && review.focusWidget() == lastBody},
					{"firstFooter", firstFooter == runButton ? "run" : "cancel"},
					{"footerBacktab", footerBacktab},
					{"backtabFocus",
						review.focusWidget()
							? QString::fromLatin1(review.focusWidget()->metaObject()->className()) + ":"
								+ review.focusWidget()->objectName() + ":" + review.focusWidget()->property("text").toString()
							: "none"},
					{"focusedBodyRect", QJsonArray{focusedRect.x(), focusedRect.y(), focusedRect.width(), focusedRect.height()}},
					{"viewport", QJsonArray{0, 0, scroll->viewport()->width(), scroll->viewport()->height()}},
					{"scrollValue", scroll->verticalScrollBar()->value()}});
			detailsToggle->click();
			reviewDiagnostic.insert("recollapsed", detailState());
			reviewDiagnostic.insert("recollapsedGeometry", geometry());
			const bool escape = qEnvironmentVariable("QEGTRAIN_E2E_REVIEW_CANCEL") == "escape";
			reviewDiagnostic.insert("cancellation", escape ? "escape" : "button");
			if (escape) {
				QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
				QApplication::sendEvent(&review, &key);
			} else {
				buttons->button(QDialogButtonBox::Cancel)->click();
			}
		});
	}
	review.exec();
	if (reviewCancelE2E) {
		reviewDiagnostic.insert("rejected", review.result() == QDialog::Rejected);
		reviewDiagnostic.insert("workerStarted", m_worker != nullptr);
		std::fprintf(stdout, "E2E_RUN_REVIEW_RENDERED=%s\n",
			QJsonDocument(reviewDiagnostic).toJson(QJsonDocument::Compact).constData());
		std::fprintf(stdout, "E2E_RUN_REVIEW_SUMMARY=%s\n", facts->itemAtPosition(0, 1)->widget()->property("text").toString().toUtf8().constData());
		std::fprintf(stdout, "E2E_RUN_REVIEW_START=%s\n", facts->itemAtPosition(1, 1)->widget()->property("text").toString().toUtf8().constData());
		std::fprintf(stdout, "E2E_RUN_REVIEW_DURATION=%s\n", facts->itemAtPosition(2, 1)->widget()->property("text").toString().toUtf8().constData());
		std::fprintf(stdout, "E2E_RUN_REVIEW_DETAILS=%d\n",
			details->text().contains("Service definitions:")
					&& details->text().contains("Configured total:")
					&& details->text().contains("Selected:")
					&& details->text().contains("Compositions:")
					&& details->text().contains("Counting rule:")
				? 1
				: 0);
		std::fprintf(stdout, "E2E_RUN_REVIEW_ADVANCED=%d\n", detailsEnabled ? 1 : 0);
		std::fprintf(stdout, "E2E_RUN_REVIEW_VALIDATION_VISIBLE=%d\n",
			details->text().contains("Validation:") ? 1 : 0);
		std::fprintf(stdout, "E2E_RUN_REVIEW_CANCELLED=%d\n", choice == CancelRun ? 1 : 0);
		std::fprintf(stdout, "E2E_RUN_REVIEW_UNCHANGED=%d\n",
			m_sceneRevision == revisionBefore && selectedSceneOccurrences() == selectionBefore ? 1 : 0);
		std::fflush(stdout);
		QCoreApplication::exit(0);
	}
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
	provenance.appliedScenario = m_appliedScenarioId;
	provenance.baseTimeSeconds = static_cast<double>(initial_variables.startingSimulationTime);
	provenance.durationSeconds = initial_variables.times;
	provenance.timestepSeconds = timestep;
	provenance.bufferSeconds = bufferTime;
	provenance.recoveryPercent = recoveryTimePercentage;
	provenance.paxMode = initial_variables.PAX_GUI ? 1 : 0;
	provenance.tsmMode = initial_variables.TSM;
	provenance.routeChoiceMode = initial_variables.RChoice;
	provenance.randomSeed = initial_variables.randomSeed;
	provenance.selectedOccurrences.reserve(static_cast<std::size_t>(std::max(0, numRegions)));
	for (int index = 0; index < numRegions; ++index) {
		const Train& train = regional_train[index];
		provenance.selectedOccurrences.push_back({train.serviceId, train.serviceOccurrence,
			train.operatingCode});
	}
	return provenance;
}

DelayRunSnapshot MainWindow::completedDelaySnapshot() const {
	DelayRunSnapshot snapshot;
	if (!m_resultsAvailable || m_completedRunResults.trains.empty())
		return snapshot;
	snapshot.caseRevision = std::to_string(m_sceneRevision);
	snapshot.scenarioId = m_appliedScenarioId;
	snapshot.baseTimeSeconds = static_cast<double>(baseTimeToSeconds(m_sceneModel.baseTime));
	snapshot.durationSeconds = std::isfinite(initial_variables.times)
		? initial_variables.times
		: 0.0;
	snapshot.timestep = timestep;
	for (const SceneScenario& scenario : m_sceneModel.scenarios) {
		if (scenario.id != snapshot.scenarioId)
			continue;
		snapshot.hasIncidents = !scenario.incidents.empty();
		snapshot.hasEntranceDelays = !scenario.entranceDelays.empty();
		break;
	}
	snapshot.provenance = m_completedRunProvenance;
	snapshot.run = m_completedRunResults;
	snapshot.timetable = m_completedTimetableResults;
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
	m_delayBaselineStatus = QString("Delay baseline set: %1. Compare it with a completed incident scenario without entrance delays.")
								.arg(completedRunContext(snapshot.provenance));
	statusBar()->showMessage(QString("Delay baseline set to scenario %1").arg(QString::fromStdString(snapshot.scenarioId)), 5000);
	refreshRunResults();
	updateDiagramActions();
}

void MainWindow::showDelayComparison() {
	if (!m_delayBaseline) {
		QMessageBox::warning(this, "Delay comparison unavailable",
			"Set a delay baseline from a run with no incidents or entrance delays first.");
		return;
	}
	const DelayRunSnapshot scenario = completedDelaySnapshot();
	const DelayComparisonResult comparison = compareDelayRuns(*m_delayBaseline, scenario);
	if (!comparison.valid) {
		QMessageBox::warning(this, "Delay comparison rejected", QString::fromStdString(comparison.diagnostic));
		return;
	}

	QDialog dialog(this);
	dialog.setWindowTitle("Incident delay comparison");
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	const QString resultSummary = comparison.rows.empty()
		? QStringLiteral("Success: zero positive additional final-arrival delay.")
		: QString("Positive additional final-arrival delay: %1 s")
			  .arg(comparison.totalArrivalDelay.available
					  ? QString::number(comparison.totalArrivalDelay.value, 'g', 12)
					  : QStringLiteral("-"));
	QTableWidget* table = new QTableWidget(body);
	table->setObjectName("delayComparisonTable");
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setColumnCount(13);
	table->setHorizontalHeaderLabels({"Service", "Occurrence", "Operating code", "Baseline final arrival", "Scenario final arrival",
		"Positive delay", "Attribution", "Incident IDs", "First direct time", "First direct location",
		"Termination requested", "Terminated", "Baseline / scenario"});
	table->setRowCount(static_cast<int>(comparison.rows.size()));
	const auto valueText = [](const RunResultValue& value) {
		return value.available ? QString::number(value.value, 'g', 12) : QStringLiteral("-");
	};
	for (int index = 0; index < static_cast<int>(comparison.rows.size()); ++index) {
		const DelayComparisonRow& row = comparison.rows[static_cast<std::size_t>(index)];
		table->setItem(index, 0, new QTableWidgetItem(QString::fromStdString(row.serviceId)));
		table->setItem(index, 1, new QTableWidgetItem(QString::number(row.occurrence)));
		table->setItem(index, 2, new QTableWidgetItem(QString::fromStdString(row.operatingCode)));
		table->setItem(index, 3, new QTableWidgetItem(valueText(row.baselineFinalArrival)));
		table->setItem(index, 4, new QTableWidgetItem(valueText(row.scenarioFinalArrival)));
		table->setItem(index, 5, new QTableWidgetItem(valueText(row.positiveContribution)));
		table->setItem(index, 6, new QTableWidgetItem(QString::fromStdString(row.attribution)));
		table->setItem(index, 7, new QTableWidgetItem(QString::fromStdString(joinIncidentIds(row.incidentIds))));
		table->setItem(index, 8, new QTableWidgetItem(valueText(row.firstDirectTime)));
		table->setItem(index, 9, new QTableWidgetItem(valueText(row.firstDirectLocation)));
		table->setItem(index, 10, new QTableWidgetItem(row.destinationTerminationRequested ? "yes" : "no"));
		table->setItem(index, 11, new QTableWidgetItem(row.destinationTerminated ? "yes" : "no"));
		table->setItem(index, 12,
			new QTableWidgetItem(
				QString("%1 / %2")
					.arg(QString::fromStdString(m_delayBaseline->scenarioId), QString::fromStdString(scenario.scenarioId))));
	}
	table->resizeColumnsToContents();
	layout->addWidget(table, 1);
	QHBoxLayout* actions = new QHBoxLayout();
	QPushButton* exportButton = new QPushButton("Export CSV...", body);
	exportButton->setObjectName("delayComparisonExportCsvButton");
	const RunProvenance baselineProvenance = m_delayBaseline->provenance;
	const RunProvenance scenarioProvenance = scenario.provenance;
	connect(exportButton, &QPushButton::clicked, &dialog, [this, scenario, comparison, baselineProvenance, scenarioProvenance]() {
		const auto operation = captureTelemetryOperation();
		saveCsvInteractive(
			this, "delay_comparison.csv", operation,
			[this, scenario, comparison] { return delayComparisonCsv(*m_delayBaseline, scenario, comparison); },
			[baselineProvenance, scenarioProvenance](const QString& path, const std::string& bytes) {
				return writeDelayArtifactWithProvenance(path.toStdString(), "csv", bytes,
					baselineProvenance, scenarioProvenance);
			});
	});
	actions->addWidget(exportButton);
	layout->addLayout(actions);
	QDialogButtonBox* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	connect(closeButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	DialogLayout::install(dialog, resultSummary,
		QString("Baseline: %1\nIncident run: %2")
			.arg(completedRunContext(m_delayBaseline->provenance), completedRunContext(scenario.provenance)),
		body, closeButtons);
	dialog.exec();
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
		const auto rollback = [this]() noexcept {
			for (auto& connection : m_simulationObservations) {
				QObject::disconnect(connection);
				connection = QMetaObject::Connection();
			}
		};
		// Telemetry is optional. Its allocation/connection failures cannot stop Run.
		try {
#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
			telemetry_smoke::failObservationInstallation(m_telemetrySmokeState.get(), 0);
#endif
			auto observation = std::make_shared<telemetry::SimulationObservation>(operation);
			m_simulationObservations[0] = connect(
				&simulation, &DispatchController::executionRejected, &simulation,
				[observation] { observation->reject(telemetry::Error::InvalidInput); }, Qt::DirectConnection);
#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
			telemetry_smoke::failObservationInstallation(m_telemetrySmokeState.get(), 1);
#endif
			m_simulationObservations[1] = connect(
				&simulation, &DispatchController::executionBegan, &simulation,
				[observation] { observation->begin(); }, Qt::DirectConnection);
#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
			telemetry_smoke::failObservationInstallation(m_telemetrySmokeState.get(), 2);
#endif
			m_simulationObservations[2] = connect(
				&simulation, &DispatchController::executionReturned, &simulation,
				[observation](qint64 elapsedMs, bool cancelled) {
					observation->finish(
						cancelled ? telemetry::SimulationObservation::Outcome::Cancelled : telemetry::SimulationObservation::Outcome::Completed,
						elapsedMs);
				},
				Qt::DirectConnection);
#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
			telemetry_smoke::failObservationInstallation(m_telemetrySmokeState.get(), 3);
#endif
			if (!m_simulationObservations[0] || !m_simulationObservations[1] || !m_simulationObservations[2])
				rollback();
		} catch (...) {
			rollback();
		}
	}
#ifdef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
	const QString smokeMode = qEnvironmentVariable("QEGTRAIN_E2E_TELEMETRY_MODE");
	if (m_telemetrySmokeState && smokeMode == "stop_before")
		connect(m_worker, &SimulationWorker::simulationStarted, m_worker,
			&SimulationWorker::requestStop, Qt::DirectConnection);
	if (m_telemetrySmokeState && smokeMode == "completion_stop") {
		auto* worker = m_worker.data();
		m_simulationObservations[3] = connect(
			&simulation, &DispatchController::executionReturned, &simulation,
			[worker](qint64, bool) { worker->requestStop(); }, Qt::DirectConnection);
	}
	if (m_telemetrySmokeState && smokeMode == "stop_postprocessing") {
		auto* worker = m_worker.data();
		m_simulationObservations[4] = connect(
			&simulation, &DispatchController::executionPostprocessing, &simulation,
			[worker] { worker->requestStop(); }, Qt::DirectConnection);
	}
#endif
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
		const auto trains = runResultTrainPointers();
		// Freeze result values before a subsequent run replaces the runtime trains.
		m_completedRunProvenance = m_pendingRunProvenance;
		m_completedRunResults = buildRunResults(trains, timestep);
		m_completedTimetableResults = buildTimetableResults(trains);
		refreshRunResults();
	} else {
		m_pendingRunProvenance = RunProvenance();
		m_completedRunProvenance = RunProvenance();
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
	processTrainUnitSourceChanges();
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

	stopTrainAnimations();

	// Clearing the scene deletes all owned QGraphicsItems.
	m_inspectedSignal = nullptr;
	scene->clear();

	// Clear list pointers - the items were owned by the scene and are now deleted.
	m_trainBadges.clear(); // items deleted by scene->clear() above
	m_prevTrainPositions.clear();
	allTrains.clear();
	allSignals.clear();
	m_signalDecorations.clear();
	m_stationOverlays.clear();
	m_selectedStationName.clear();
	m_hasSelectedStationIdentity = false;
	m_selectedStationNodeId = 0.0;
	m_selectedStationTrack = -1;
	m_vcMessageItems.clear();
	m_stationDecorations.clear();
	m_stationLabels.clear();
	m_stationPictures.clear();
	m_signalsByAheadId.clear();
	allArcs.clear();
	allPlatforms.clear();
	if (m_networkLegendWidget)
		m_networkLegendWidget->setCaseContent(NetworkLegendContent());
	m_tracksBySectionId.clear();
	m_tracksByOccupiedArc.clear();
	m_activeTrackItems.clear();
	m_infrastructureSelectionId.clear();
	trainPaxInfoItem = nullptr;
	trainPaxItem = nullptr;
	paxIconInfoItem = nullptr;
	paxIconItem = nullptr;
	effect = nullptr; // owned and deleted by the cleared item

	regionStations.clear();
	setFollowTrain(-1);
	m_selectedTrainIndex = -1;

	m_snapshot.reset();
	refreshFollowTrainChoices();
	m_previewFitBounds = QRectF();
	m_previewHasSelectedTrack = false;
	m_previewHasSignals = false;

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
		m_selectedScenarioId = configured->id;
	}

	refreshValidationPanel();
	if (hasErrors(m_sceneDiagnostics)) {
		operation.failure(telemetry::Operation::Simulation, telemetryDiagnosticError(m_sceneDiagnostics));
		int errorCount = countDiagnostics(m_sceneDiagnostics).errors;
		showBlockingError(this, "Cannot Run Scene",
			QString("The scene has %1 validation error(s) that must be fixed before running.").arg(errorCount), true);
		return;
	}
	pruneExcludedServiceOccurrences();
	m_lastRunTotalOccurrences = totalServiceOccurrences();
	m_lastRunSelectedOccurrences = selectedServiceOccurrences();
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

	const QRectF previewFitBounds = m_previewFitBounds;
	const QPointF previewZoomFocus = m_previewZoomFocus;
	teardownGUI();
	m_previewFitBounds = previewFitBounds;
	m_previewZoomFocus = previewZoomFocus;
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
	m_appliedScenarioId = m_selectedScenarioId;
	const SceneRunSelection selection = selectedSceneOccurrences();
	const bool identityBeforePrepare = !startupTimingEnabled()
		|| startupTimingIdentityMatches(m_sceneDir, m_sceneModel);
	const qint64 prepareStarted = startupTimingEnabled() ? startupTimingNowNanoseconds() : 0;
	const std::vector<SceneDiagnostic> diagnostics = simulation.prepareScene(m_sceneModel, m_selectedScenarioId, selection);
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
		config.scenario = m_appliedScenarioId;
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

// draws a Node
void MainWindow::paintNode(QPointF coord, int size, int pen_width, int track, Node* Node) {
	QPen pen = QPen(Qt::lightGray);
	pen.setWidth(0);
	pen.setCosmetic(true);

	// center the station marker on its network coordinate
	const qreal diameter = size * (cachedTrackLine(track) ? presentationScale() : 1.0);
	QRectF rect = QRectF(0, 0, diameter, diameter);
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

// draws a station Node; its platform id darkens the square only at the end of a line
void MainWindow::paintStationNode(QPointF coord, int size, int pen_width, int track, Node* Node, bool lineEnd) {
	const bool hasPlatform = lineEnd && Node && !Node->stationPlatformId.empty() && Node->stationPlatformId != "None";
	StationVisual visual = classifyStation(hasPlatform, Node ? Node->numConnections : 0);
	QPen pen = QPen(visual.outline);
	pen.setWidth(0);
	pen.setCosmetic(true);

	// draws using rectangle with center on top-left corner (center_x,center_y,width,height)
	const qreal diameter = size * (cachedTrackLine(track) ? presentationScale() : 1.0);
	QRectF rect = QRectF(0, 0, diameter, diameter);
	rect.moveCenter(coord);

	StationNodeItem* el = new StationNodeItem(rect);
	el->setPen(pen);
	el->setBrush(visual.fill);

	// add track and Node pointer to the station item
	el->track = track;
	el->node = Node;

	// add station Node to scene
	scene->addItem(el);
}

// Keep native font/pixmap metrics and scale their scene-space presentation. The
// overlay then enlarges them on screen to their minimum readable size.
void MainWindow::paintStationOverlay(QPointF coord, const StationVisual& visual, const string& sname,
	qreal presentationScale, QPointF decorationOffset, int platformCount) {
	if (!scene)
		return;
	auto* overlay = new StationOverlayItem(QString::fromStdString(sname), coord, visual);
	overlay->setSceneDecoration(true);
	scene->addItem(overlay);
	m_stationOverlays.push_back(overlay);
	m_stationDecorations.push_back(overlay);
	overlay->setNameVisible(m_stationNamesVisible);
	overlay->setVisible(m_stationLayerVisible);

	// Keep picking/source identity at coord; only artwork and text move.
	const QPointF artworkCoord = coord + decorationOffset;
	const int symbolSize = station_size;
	const QPixmap symbol = QIcon(visual.iconResource).pixmap(symbolSize, symbolSize);
	if (!symbol.isNull()) {
		auto* picture = scene->addPixmap(symbol);
		picture->setScale(presentationScale);
		picture->setPos(artworkCoord.x() - symbolSize * presentationScale / 2.0,
			artworkCoord.y() - (station_size + symbolSize) * presentationScale / 2.0);
		picture->setTransformationMode(Qt::SmoothTransformation);
		picture->setAcceptedMouseButtons(Qt::NoButton);
		m_stationDecorations.push_back(picture);
		m_stationPictures.insert(overlay, picture);
		picture->setVisible(m_stationLayerVisible);
	}
	QFont font;
	font.setPixelSize(station_size / 5);
	auto* label = scene->addText(StationOverlayItem::displayName(sname), font);
	label->setDefaultTextColor(Qt::white);
	label->setScale(presentationScale);
	label->setZValue(3);
	label->setPos(artworkCoord - QPointF(label->boundingRect().width() * presentationScale / 2.0, label->boundingRect().height() * presentationScale / 2.0));
	label->setAcceptedMouseButtons(Qt::NoButton);
	m_stationLabels.push_back(label);
	label->setVisible(m_stationLayerVisible && m_stationNamesVisible);
	overlay->attachArtwork(m_stationPictures.value(overlay, nullptr), label, platformCount);
}

// paint platform next to station Node (upper side)
void MainWindow::paintStationPlatform(QPointF coord, int size, int pen_width, Node* Node) {
	QPen pen = QPen(Qt::white);
	pen.setWidth(0);
	pen.setCosmetic(true);

	// draws using rectangle with center on top-left corner (center_x,center_y,width,height)
	const qreal diameter = size * presentationScale();
	QRectF rect = QRectF(0, 0, 5 * diameter, 0.9 * diameter);

	// platform on top of station Node
	coord.setY(coord.y() - 1.15 * diameter);

	rect.moveCenter(coord);

	auto* platformItem = new PlatformItem(rect);
	platformItem->setPen(pen);
	platformItem->setBrush(Qt::white);

	// find corresponding station platform
	auto platformIt = std::find_if(AllStationPlatforms.begin(), AllStationPlatforms.end(),
		[&Node](auto const& plat) { return Node->stationName == plat.StationID && Node->stationPlatformId == plat.ID; });

	// platform found
	if (platformIt != AllStationPlatforms.end()) {
		platformItem->stationId = platformIt->StationID;
		platformItem->platformId = platformIt->ID;
		platformItem->maxVolume = platformIt->Max_Passenger_Volume;
	}

	// string with pax counter
	std::stringstream ss;
	ss << "Pax on platform: " << (platformIt == AllStationPlatforms.end() ? 0 : platformIt->Current_N_Passengers)
	   << "\n Max pax volume: " << platformItem->maxVolume;

	// convert string
	QString text = QString::fromStdString(ss.str());

	// set text size
	QFont font = QFont();
	font.setPixelSize((int)station_size / 15);

	auto* textItem = new QGraphicsTextItem(text);
	textItem->setDefaultTextColor(Qt::white);
	textItem->setFont(font);
	textItem->setScale(presentationScale());
	textItem->setPos(QPointF(platformItem->sceneBoundingRect().center().x() - textItem->boundingRect().width() * presentationScale() / 2, platformItem->sceneBoundingRect().top() - textItem->boundingRect().height() * presentationScale()));
	textItem->setZValue(3); // draw on top of every item

	// add textItem pointer to rect item
	platformItem->textIcon = textItem;

	// add to scene
	scene->addItem(platformItem);
	scene->addItem(textItem);
	m_stationDecorations.push_back(platformItem);
	platformItem->setVisible(m_stationLayerVisible);

	// hide textItem at first
	textItem->setVisible(false);

	// add item to allPlatforms list
	allPlatforms.push_back(platformItem);
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
	qreal dy = -1 * track_separation * presentationScale();
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
	ss << "Current onboard pax: " << trainItem->currentOnboardPassengers
	   << "\nMax onboard pax: " << trainItem->maxOnboardPassengers;

	// paint text
	QGraphicsTextItem* text = new QGraphicsTextItem;
	text->setPlainText(QString::fromStdString(ss.str())); // text without background
	text->setDefaultTextColor(Qt::black);
	text->setFont(font);
	text->setScale(presentationScale());

	// draw box around text
	QGraphicsRectItem* textBox = new QGraphicsRectItem;
	textBox->setRect(QRectF(0, 0, 1.15 * (text->boundingRect().width() * presentationScale()), 1.15 * (text->boundingRect().height() * presentationScale()))); // box 15% bigger than text rect on both directions
	textBox->setBrush(QColor(242, 161, 106));
	QPen boxPen = textBox->pen();
	boxPen.setWidthF(presentationScale());
	textBox->setPen(boxPen);
	textBox->setPos(coord.x() - (textBox->boundingRect().width() / 2), coord.y() - (textBox->boundingRect().height()));

	// set text position (center of text box)
	QPointF textPos = textBox->pos();
	textPos.rx() += 0.5 * (textBox->boundingRect().width() - (text->boundingRect().width() * presentationScale()));
	textPos.ry() += 0.5 * (textBox->boundingRect().height() - (text->boundingRect().height() * presentationScale()));
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
	qreal dy = -0.75 * track_separation * presentationScale();
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

	GuiPassengerState passenger;
	passenger.id = paxItem->passengerId;
	if (m_snapshot) {
		auto passengerIt = std::find_if(m_snapshot->passengers.begin(), m_snapshot->passengers.end(),
			[&paxItem](const GuiPassengerState& value) { return value.id == paxItem->passengerId; });
		if (passengerIt != m_snapshot->passengers.end())
			passenger = *passengerIt;
	}

	// string to display
	std::stringstream ss;
	ss << "Pax ID: " << passenger.id << "\nStatus: " << passenger.status
	   << " (" << passenger.waitingPlatform << ")"
	   << "\nNext train: " << passenger.nextTrain;

	if (!passenger.nextDestination.empty()) {
		ss << "\nNext destination: " << passenger.nextDestination;
	}
	if (m_replayActive)
		ss << "\nReplay: journey details unavailable";

	// paint text
	QGraphicsTextItem* text = new QGraphicsTextItem;
	text->setPlainText(QString::fromStdString(ss.str())); // text without background
	text->setDefaultTextColor(Qt::black);
	text->setFont(font);
	text->setScale(presentationScale());

	// draw box around text
	QGraphicsRectItem* textBox = new QGraphicsRectItem;
	textBox->setRect(QRectF(0, 0, 1.15 * (text->boundingRect().width() * presentationScale()), 1.15 * (text->boundingRect().height() * presentationScale()))); // box 15% bigger than text rect on both directions
	textBox->setBrush(QColor(242, 161, 106));
	QPen boxPen = textBox->pen();
	boxPen.setWidthF(presentationScale());
	textBox->setPen(boxPen);
	textBox->setPos(coord.x() - (textBox->boundingRect().width() / 2), coord.y() - (textBox->boundingRect().height()));

	// set text position (center of text box)
	QPointF textPos = textBox->pos();
	textPos.rx() += 0.5 * (textBox->boundingRect().width() - (text->boundingRect().width() * presentationScale()));
	textPos.ry() += 0.5 * (textBox->boundingRect().height() - (text->boundingRect().height() * presentationScale()));
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
}

// draws a connection
void MainWindow::paintConnection(QPointF start, QPointF end, int pen_width, Connections* connection) {
	// draws a line from start to end with a given line width
	ConnectionItem* line = new ConnectionItem(QLineF(start.x(), start.y(), end.x(), end.y()));
	line->setPen(connectionPen(pen_width));

	// add connection pointer to line item
	line->connection = connection;

	// add item to scene
	scene->addItem(line);
}

// draws trackside signals (X is the beginning of the block section - except for the last signal of each trackline)
void MainWindow::paintSignal(double X, int size, int pen_width, int track, int track_separation, int sectionIndex) {
	const TrackPreviewLine* previewLine = cachedTrackLine(track);
	if (!previewLine && !hasTrackGeometry(track))
		return;

	double centerX, centerY, beforeX, beforeY, afterX, afterY;
	egtrainPoint2Screen(X, track, track_separation, centerX, centerY);
	egtrainPoint2Screen(X - 0.008, track, track_separation, beforeX, beforeY);
	egtrainPoint2Screen(X + 0.008, track, track_separation, afterX, afterY);
	QPointF normal;
	if (previewLine) {
		if (!previewSignalNormal(*previewLine, X, normal))
			return;
	} else {
		int stationIdx[2];
		neighbourStations(X, track, stationIdx);
		const int index = stationIdx[1];
		normal = QPointF(StationArray[index].signalDeltaX[blockSets[track].region],
			StationArray[index].signalDeltaY[blockSets[track].region]);
	}
	const SignalGeometry geometry = signalGeometry(QPointF(centerX, centerY),
		QPointF(beforeX, beforeY), QPointF(afterX, afterY), normal,
		track_separation * (previewLine ? presentationScale() : 1.0));

	// post/basis
	QPen penPost = QPen(Qt::white);
	penPost.setWidth(pen_width);
	penPost.setCosmetic(true);

	// plate
	const QPen penPlate = signalHeadPen();
	// draws using rectangle with center on top-left corner (center_x,center_y,width,height)
	const qreal diameter = size * (previewLine ? presentationScale() : 1.0);
	QRectF rect = QRectF(0, 0, diameter, diameter);
	rect.moveCenter(QPointF(0.0, 0.0));

	// post #1
	QGraphicsLineItem* post1 = new QGraphicsLineItem(geometry.reversedPost);
	post1->setPen(penPost);

	// plate #1
	SignalItem* plate1 = new SignalItem(rect);
	plate1->setZValue(3);
	plate1->setPos(geometry.reversedHead);
	plate1->setData(kSignalAnchorRole, plate1->pos());
	plate1->setPen(penPlate);
	plate1->setBrush(Qt::green);
	plate1->setAspectCode(180);

	// add trackID, direction and immutable section display fields to signal item
	plate1->trackID = signalling_block_sections[sectionIndex].trackLineId;
	plate1->X = X;
	plate1->setReversedDirection(true);
	// last signal of trackline
	if (X == signalling_block_sections[sectionIndex].end_node.X) {
		plate1->sectionAheadId = signalling_block_sections[sectionIndex].ID;
		plate1->sectionAheadLength = signalling_block_sections[sectionIndex].length;
		plate1->sectionAheadTrackId = signalling_block_sections[sectionIndex].trackLineId;
	}
	// remaining signals
	else {
		if (sectionIndex > 0 && signalling_block_sections[sectionIndex - 1].trackLineId == signalling_block_sections[sectionIndex].trackLineId) {
			plate1->sectionAheadId = signalling_block_sections[sectionIndex - 1].ID;
			plate1->sectionAheadLength = signalling_block_sections[sectionIndex - 1].length;
			plate1->sectionAheadTrackId = signalling_block_sections[sectionIndex - 1].trackLineId;
		}
		plate1->sectionBehindId = signalling_block_sections[sectionIndex].ID;
		plate1->sectionBehindLength = signalling_block_sections[sectionIndex].length;
		plate1->sectionBehindTrackId = signalling_block_sections[sectionIndex].trackLineId;
	}

	// basis #1, and the opposing direction's post and base
	QGraphicsLineItem* basis1 = new QGraphicsLineItem(geometry.reversedBase);
	basis1->setPen(penPost);
	QGraphicsLineItem* post2 = new QGraphicsLineItem(geometry.forwardPost);
	post2->setPen(penPost);

	SignalItem* plate2 = new SignalItem(rect);
	plate2->setZValue(3);
	plate2->setPos(geometry.forwardHead);
	plate2->setData(kSignalAnchorRole, plate2->pos());
	plate2->setPen(penPlate);
	plate2->setBrush(Qt::green);
	plate2->setAspectCode(180);

	// add trackID, direction and immutable section display fields to signal item
	plate2->trackID = signalling_block_sections[sectionIndex].trackLineId;
	plate2->X = X;
	plate2->setReversedDirection(false);
	// last signal of trackline
	if (X == signalling_block_sections[sectionIndex].end_node.X) {
		plate2->sectionBehindId = signalling_block_sections[sectionIndex].ID;
		plate2->sectionBehindLength = signalling_block_sections[sectionIndex].length;
		plate2->sectionBehindTrackId = signalling_block_sections[sectionIndex].trackLineId;
	}
	// remaining signals
	else {
		plate2->sectionAheadId = signalling_block_sections[sectionIndex].ID;
		plate2->sectionAheadLength = signalling_block_sections[sectionIndex].length;
		plate2->sectionAheadTrackId = signalling_block_sections[sectionIndex].trackLineId;
		if (sectionIndex > 0 && signalling_block_sections[sectionIndex - 1].trackLineId == signalling_block_sections[sectionIndex].trackLineId) {
			plate2->sectionBehindId = signalling_block_sections[sectionIndex - 1].ID;
			plate2->sectionBehindLength = signalling_block_sections[sectionIndex - 1].length;
			plate2->sectionBehindTrackId = signalling_block_sections[sectionIndex - 1].trackLineId;
		}
	}

	QGraphicsLineItem* basis2 = new QGraphicsLineItem(geometry.forwardBase);
	basis2->setPen(penPost);

	const auto addSignalDecoration = [this, track](QGraphicsItem* item) {
		item->setData(kSignalDecorationRole, true);
		item->setData(kSignalTrackRole, track);
		item->setData(kSignalBaseVisibleRole, true);
		scene->addItem(item);
		m_signalDecorations.push_back(item);
		item->setVisible(m_signalLayerVisible);
	};
	addSignalDecoration(post1);
	addSignalDecoration(plate1);
	addSignalDecoration(basis1);
	addSignalDecoration(post2);
	addSignalDecoration(plate2);
	addSignalDecoration(basis2);

	// add signals to allSignals list
	allSignals.push_back(plate1);
	allSignals.push_back(plate2);
}

// draws a train
void MainWindow::paintTrain(const GuiTrainState& train, int size, int pen_width) {
	// The scene in the window is the scene that was run: an edit discards the results.
	QColor serviceColor;
	int red = 0, green = 0, blue = 0;
	if (sceneParseVisualizationColor(sceneServiceVisualizationColor(m_sceneModel, train.serviceId), &red, &green, &blue))
		serviceColor = QColor(red, green, blue);
	TrainVisual visual = resolveTrainVisual(train.type, train.description, serviceColor);
	QPen pen = QPen(visual.outline);
	pen.setWidthF(3 * presentationScale());

	// create train polygon item list
	QList<TrainBodyItem*>* trainPolygonItemList = new QList<TrainBodyItem*>();

	// create group of polygons
	TrainItemGroup* trainPolygonGroup = new TrainItemGroup();

	// get polygon for each wagon
	for (int wagon = 0; wagon <= train.wagonCount; wagon++) {
		QPolygonF trainPolygon;
		getTrainPolygon(&trainPolygon, wagon, train);

		// train graphical item
		TrainBodyItem* trainItem = new TrainBodyItem(trainPolygon);
		trainItem->setPen(pen);
		trainItem->setBrush(visual.fill);
		trainItem->setOpacity(1);

		trainItem->index = train.index;

		// add to polygon group
		trainPolygonGroup->addToGroup(trainItem);

		// add item to list
		trainPolygonItemList->push_back(trainItem);
	}
	const bool hasVisibleGeometry = std::any_of(trainPolygonItemList->cbegin(),
		trainPolygonItemList->cend(), [](const TrainBodyItem* item) {
			return item && !item->polygon().isEmpty();
		});

	// add train pointer and index to the item
	trainPolygonGroup->index = train.index;
	trainPolygonGroup->trainId = train.id;
	trainPolygonGroup->trainDescription = train.description;
	trainPolygonGroup->trainType = train.type;
	trainPolygonGroup->fillColor = visual.fill;
	trainPolygonGroup->outlineColor = visual.outline;
	trainPolygonGroup->serviceId = train.serviceId;
	trainPolygonGroup->trainLength = train.length;
	trainPolygonGroup->wagonCount = train.wagonCount;
	trainPolygonGroup->currentOnboardPassengers = train.currentOnboardPassengers;
	trainPolygonGroup->maxOnboardPassengers = train.maxOnboardPassengers;
	trainPolygonGroup->outOfSimulation = train.outOfSimulation;
	trainPolygonGroup->trainPolygonItemList = trainPolygonItemList;

	// add train to scene
	scene->addItem(trainPolygonGroup);
	trainPolygonGroup->setVisible(m_trainLayerVisible && !train.outOfSimulation && hasVisibleGeometry);

	// add train to allTrains list
	allTrains.push_back(trainPolygonGroup);

	TrainBadgeItem* badge = new TrainBadgeItem();
	badge->setIdentifier(QString::fromStdString(guiTrainDisplayIdentifier(train)));
	badge->setTooltipDetails(QString::fromStdString(train.description),
		QString::fromStdString(train.operatingCode), QString::fromStdString(train.type));
	badge->setSpeedText(QString::fromStdString(formatSpeedLabel(train.speedKmh)));
	badge->setSpeedVisible(m_trainSpeedLabelsVisible);
	badge->setTrainVisual(visual);
	badge->setReversed(train.reversedDirection);
	const bool promoted = isTrainOverlayPromoted(train.index);
	badge->setPromoted(promoted);
	badge->setPresentation(TrainBadgeItem::presentationForZoom(
		networkView ? networkView->zoomRatio() : 1.0, promoted));
	scene->addItem(badge);
	badge->setVisible(m_trainLayerVisible && !train.outOfSimulation && hasVisibleGeometry);
	badge->setPos(trainPolygonGroup->sceneBoundingRect().center());
	m_trainBadges[train.index] = badge;
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
	m_runResultsSummaryLabel = new QLabel(container);
	m_runResultsSummaryLabel->setObjectName("runResultsContextLabel");
	m_runResultsSummaryLabel->setWordWrap(true);
	containerLayout->addWidget(m_runResultsSummaryLabel);
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
	resultMenu->addSeparator();
	addResultViewAction("Speed / distance", &MainWindow::showSpeedDistanceDiagram);
	addResultViewAction("Speed / time", &MainWindow::showSpeedTimeDiagram);
	addResultViewAction("Time / distance", &MainWindow::showTimeDistanceDiagram);
	addResultViewAction("Tractive effort / distance", &MainWindow::showTractiveEffortDistanceDiagram);
	resultMenu->addSeparator();
	addResultViewAction("Train paths (simulated movement)", &MainWindow::displayTrainPathDiagrams);
	addResultViewAction("Blocking time", &MainWindow::showBlockingTimeDiagram);
	addResultViewAction("Capacity", &MainWindow::showCapacityAnalysis);
	resultViews->addWidget(diagramsButton);
	QPushButton* exportCsvBtn = new QPushButton("Export CSV...", container);
	exportCsvBtn->setObjectName("resultView_ExportCSV");
	exportCsvBtn->setToolTip("Write travel time and energy per train to a CSV file");
	connect(exportCsvBtn, &QPushButton::clicked, this, [this]() {
		const auto operation = captureTelemetryOperation();
		const RunProvenance provenance = m_completedRunProvenance;
		saveCsvInteractive(
			this, "run_summary.csv", operation,
			[this] { return buildRunSummaryCsv(m_completedRunResults); },
			[provenance](const QString& path, const std::string& bytes) {
				return writeRunArtifactWithProvenance(path.toStdString(), "csv", bytes, provenance);
			});
	});
	QPushButton* exportPngBtn = new QPushButton("Export PNG...", container);
	exportPngBtn->setObjectName("resultView_ExportPNG");
	exportPngBtn->setToolTip("Save the table as an image");
	connect(exportPngBtn, &QPushButton::clicked, this, [this]() {
		const auto operation = captureTelemetryOperation();
		QString path = QFileDialog::getSaveFileName(this, "Export Table", "run_summary.png", "PNG Image (*.png)");
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
		const bool written = writeRunArtifactWithProvenance(path.toStdString(), "png", bytes, m_completedRunProvenance);
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
	m_delayFeedbackLabel = new QLabel(container);
	m_delayFeedbackLabel->setObjectName("delayFeedbackLabel");
	m_delayFeedbackLabel->setWordWrap(true);
	containerLayout->addWidget(m_delayFeedbackLabel);
	m_setDelayBaselineButton = new QPushButton("Set delay baseline", container);
	m_setDelayBaselineButton->setObjectName("setDelayBaselineButton");
	m_setDelayBaselineButton->setToolTip("Freeze this completed run with no incidents or entrance delays as the delay baseline");
	connect(m_setDelayBaselineButton, &QPushButton::clicked, this, &MainWindow::setDelayBaseline);
	delayRow->addWidget(m_setDelayBaselineButton);
	m_compareDelayButton = new QPushButton("Compare delays", container);
	m_compareDelayButton->setObjectName("compareDelayButton");
	m_compareDelayButton->setToolTip("Compare the completed incident run with the frozen delay baseline");
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
	if (!m_runResultsDock || !m_runResultsTable || m_completedRunResults.trains.empty())
		return;
	refreshRunResultsSummary();
	m_runResultsDock->setWindowTitle(QString("Run Results — %1").arg(completedRunContext(m_completedRunProvenance)));

	const RunResults& results = m_completedRunResults;
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
	const auto clockText = [this](const RunResultValue& value) {
		return value.available
			? QString::fromStdString(formatSimTime(static_cast<long long>(value.value), m_startOffsetSeconds))
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
	if (!m_runResultsSummaryLabel || m_completedRunResults.trains.empty())
		return;
	int directEvidenceCount = 0;
	int destinationTerminationCount = 0;
	for (const TrainRunResult& result : m_completedRunResults.trains) {
		if (!result.directIncidentIds.empty())
			++directEvidenceCount;
		if (result.destinationTerminated)
			++destinationTerminationCount;
	}
	QString summary = QString("Run: %1 | Occurrences: %2/%3 selected | Status: Completed")
						  .arg(completedRunContext(m_completedRunProvenance))
						  .arg(m_lastRunSelectedOccurrences)
						  .arg(m_lastRunTotalOccurrences);
	if (advancedDetailsEnabled()) {
		summary += QString(" | Direct incident evidence: %1 | Destination terminations: %2")
					   .arg(directEvidenceCount)
					   .arg(destinationTerminationCount);
	}
	if (m_delayBaseline)
		summary += QString(" | Delay baseline: %1").arg(completedRunContext(m_delayBaseline->provenance));
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
	removeTrainPaxInfoIcon();
	removePaxInfoIcon();
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
	m_inspectedSignal = nullptr;
	m_selectedStationName.clear();
	m_hasSelectedStationIdentity = false;
	m_selectedStationNodeId = 0.0;
	m_selectedStationTrack = -1;
	m_selectedTrainIndex = -1;
	for (auto* overlay : m_stationOverlays)
		if (overlay)
			overlay->setSelected(false);

	// hide all widgets
	arcInfoWidget->hide();
	nodeInfoWidget->hide();
	connectionInfoWidget->hide();
	signallingInfoWidget->hide();
	trainInfoWidget->hide();
	removeTrainPaxInfoIcon();
	trainPaxItem = nullptr; // Explicitly dismissed or replaced train inspection.
	removePaxInfoIcon();

	// remove highlight
	if (effect) {
		delete effect;
		effect = nullptr;
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
			for (const auto& anchor : m_cachedTrackPreview.stations)
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
		const auto head = std::find_if(m_cachedTrackPreview.previewSignals.begin(), m_cachedTrackPreview.previewSignals.end(),
			[&](const TrackPreviewSignal& signal) { return signal.trackId == track && signal.sectionId == id; });
		if (head == m_cachedTrackPreview.previewSignals.end())
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
	if (!effect) {
		effect = new HighlightEffect(Qt::blue, 1);
	}
	item->setGraphicsEffect(effect);
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
	if (!re || !re->node)
		return;
	handleCloseInfoDockWidget();
	m_selectedStationName = QString::fromStdString(re->node->stationName);
	m_hasSelectedStationIdentity = true;
	m_selectedStationNodeId = re->node->ID;
	m_selectedStationTrack = re->track;
	for (auto* overlay : m_stationOverlays)
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
		nodeRegionText->setText(QString::number(blockSets[re->track].region));
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
	for (auto* candidate : allSignals) {
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
	if (!signal)
		return;
	handleCloseInfoDockWidget();
	m_inspectedSignal = signal;

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
	removeTrainPaxInfoIcon();
	removePaxInfoIcon();

	// show pax info only while its layer is enabled
	if (initial_variables.PAX_GUI && m_passengerLayerVisible)
		paintTrainPassengerInfo(groupItem);

	// show widget
	infoDockWidget->setWindowTitle("Train Info");
	infoDockWidget->show();
	trainInfoWidget->show();
	if (changeFollowMode)
		setFollowTrain(trainItem->index);
	updateViewportOverlays();

	// effect on clicked item
	if (!effect) {
		effect = new HighlightEffect(Qt::blue, 1);
	}
	if (trainItem->parentItem()) {
		trainItem->parentItem()->setGraphicsEffect(effect);
	} // effect on entire train
}

// shows train info on dock widget
void MainWindow::displayTrainInfo(TrainBodyItem* trainItem) {
	displayTrainDetails(trainItem, true);
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
	removeTrainPaxInfoIcon();
	removePaxInfoIcon();

	// show pax info
	paintPassengerInfoIcon(paxItem);

	// effect on clicked item
	if (!effect) {
		effect = new HighlightEffect(Qt::blue, 1);
	}
	paxItem->setGraphicsEffect(effect);
}

void MainWindow::handleDisableHighlight() {
	handleCloseInfoDockWidget();
	if (infoDockWidget)
		infoDockWidget->hide();
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
		if (std::fabs(StationArray[i].latitude) > 1e-12 || std::fabs(StationArray[i].longitude) > 1e-12) {
			pt = Coord2ScreenPoint(StationArray[i].latitude, StationArray[i].longitude, geo_scale);
			StationArray[i].graphX = pt.x();
			StationArray[i].graphY = pt.y();
		} else {
			// Native scenes may intentionally omit legacy geographic metadata.
			StationArray[i].graphX = StationArray[i].X * 1000.0;
			StationArray[i].graphY = StationArray[i].Y * 1000.0;
		}
		if (StationArray[i].regions.empty())
			StationArray[i].regions.push_back(0);
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


// fit view
void MainWindow::fitView() {
	updateNetworkLegend();
	if (networkView) {
		if (!m_previewFitBounds.isEmpty())
			networkView->fitToBounds(m_previewFitBounds);
		else
			networkView->fitToTopology();
		m_followCamera->settle();
	}
	updateViewportOverlays();
	updateZoomStatus();
}

bool MainWindow::hasTrackGeometry(int track) const {
	if (track < 0 || track >= numTrackLines)
		return false;
	const int region = blockSets[track].region;
	return region >= 0 && region < static_cast<int>(regionStations.size()) && regionStations[region].size() >= 2;
}


// finds the indexes of the two closest stations given a point
void MainWindow::neighbourStations(double X, int tracklineID, int* stationIdx) {
	stationIdx[0] = 0;
	stationIdx[1] = 0;
	if (!hasTrackGeometry(tracklineID))
		return;
	const int region = blockSets[tracklineID].region;
	std::vector<int> trackStIdx = regionStations[region];

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
	if (const auto* line = cachedTrackLine(track)) {
		TrackPreviewPoint point;
		if (trackPreviewPointAtX(*line, X, point)) {
			graphX = point.x;
			graphY = point.y + line->displayOffset;
			return;
		}
	}
	if (!hasTrackGeometry(track)) {
		graphX = X * 1000.0;
		graphY = static_cast<double>(blockSets[track].graphID) * separation;
		return;
	}
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
	if (const auto* line = cachedTrackLine(track)) {
		TrackPreviewPoint point;
		if ((!Node->sceneNodeId.empty()
				&& trackPreviewPointAtNode(*line, Node->sceneNodeId, point))
			|| trackPreviewPointAtX(*line, Node->X, point)) {
			Node->graphX = point.x;
			Node->graphY = point.y + line->displayOffset;
			return;
		}
	}
	if (!hasTrackGeometry(track)) {
		Node->graphX = Node->X * 1000.0;
		Node->graphY = Node->Y * 1000.0 + static_cast<double>(blockSets[track].graphID) * separation;
		return;
	}
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
	if (const auto* firstLine = cachedTrackLine(track1)) {
		if (const auto* secondLine = cachedTrackLine(track2)) {
			TrackPreviewPoint first;
			TrackPreviewPoint second;
			const bool hasFirst = (!connections->sceneFirstNodeId.empty()
									  && trackPreviewPointAtNode(*firstLine, connections->sceneFirstNodeId, first))
				|| trackPreviewPointAtX(*firstLine, connections->xFirstNode, first);
			const bool hasSecond = (!connections->sceneSecondNodeId.empty()
									   && trackPreviewPointAtNode(*secondLine, connections->sceneSecondNodeId, second))
				|| trackPreviewPointAtX(*secondLine, connections->xSecondNode, second);
			if (hasFirst && hasSecond) {
				connections->graphXFirstNode = first.x;
				connections->graphYFirstNode = first.y + firstLine->displayOffset;
				connections->graphXSecondNode = second.x;
				connections->graphYSecondNode = second.y + secondLine->displayOffset;
				return;
			}
		}
	}
	if (!hasTrackGeometry(track1) || !hasTrackGeometry(track2)) {
		connections->graphXFirstNode = connections->xFirstNode * 1000.0;
		connections->graphYFirstNode = static_cast<double>(blockSets[track1].graphID) * separation;
		connections->graphXSecondNode = connections->xSecondNode * 1000.0;
		connections->graphYSecondNode = static_cast<double>(blockSets[track2].graphID) * separation;
		return;
	}
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
		for (TrainItemGroup* train : allTrains)
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
	removePaxInfoIcon();
	paxIconItem = nullptr;
	stopTrainAnimations();
	m_prevTrainPositions.clear();
	m_snapshot = frame;
	{
		const QSignalBlocker blocker(m_replaySlider);
		m_replaySlider->setValue(m_replayRequestedTime);
	}
	writeReplayText(frame->timestep);
	renderSnapshot(true);
	if (trainPaxItem && !replayTrainHasPosition(trainPaxItem->index))
		removeTrainPaxInfoIcon();
	else if (trainPaxItem && !trainPaxInfoItem && initial_variables.PAX_GUI
		&& m_passengerLayerVisible)
		paintTrainPassengerInfo(trainPaxItem);
	updateViewportOverlays();
}

void MainWindow::renderSnapshot(bool historical) {
	if (!m_snapshot) return;
	QEGTRAIN_PROFILE_SCOPE("gui/snapshot_delivery/render_frame", "gui", "gui/snapshot_delivery");

	{
		QEGTRAIN_PROFILE_SCOPE("gui/snapshot_delivery/render_frame/signalling", "gui",
			"gui/snapshot_delivery/render_frame");
		updateSignalling();
	}
	{
		QEGTRAIN_PROFILE_SCOPE("gui/snapshot_delivery/render_frame/train_position", "gui",
			"gui/snapshot_delivery/render_frame");
		updateTrainPosition(m_snapshot->timestep);
	}

	// pax info
	if (initial_variables.PAX_GUI) {
		{
			QEGTRAIN_PROFILE_SCOPE("gui/snapshot_delivery/render_frame/platforms", "gui",
				"gui/snapshot_delivery/render_frame");
			updatePlatforms(m_snapshot->timestep);
		}
		{
			QEGTRAIN_PROFILE_SCOPE("gui/snapshot_delivery/render_frame/passenger_icons", "gui",
				"gui/snapshot_delivery/render_frame");
			updatePaxIconInfo();
		}
		{
			QEGTRAIN_PROFILE_SCOPE("gui/snapshot_delivery/render_frame/train_passenger_info", "gui",
				"gui/snapshot_delivery/render_frame");
			updateTrainPaxInfo();
		}
	}

	if (const auto frameRendered = mainWindowTestHooks().frameRendered)
		frameRendered(*this, historical);
}

// slot to update signal aspects
void MainWindow::updateSignalling() {
	if (!m_snapshot)
		return;
	std::set<const SignalItem*> updated;
	for (const GuiSignalState& signal : m_snapshot->signalStates)
		updateSignalAspect(signal, updated);
	// A head without a state has no route running through its section in its
	// direction. It has nothing to show, so it must not keep an earlier aspect.
	for (SignalItem* signal : allSignals) {
		if (updated.count(signal))
			continue;
		signal->setAspectCode(kGuiSignalUnavailable);
		signal->setFailed(false);
	}
	updateSignalCues();
}

// slot to update passenger counter at platforms
void MainWindow::updatePlatforms(int t) {
	Q_UNUSED(t);
	if (!m_snapshot)
		return;
	// show text items from the beginning of the simulation
	if (m_snapshot->timestep == 0) {
		for (auto* platformIcon : allPlatforms) {
			platformIcon->textIcon->setVisible(paxTextVisible());
		}
	}

	for (auto* platformIcon : allPlatforms) {
		auto platformState = std::find_if(m_snapshot->platforms.begin(), m_snapshot->platforms.end(),
			[platformIcon](const GuiPlatformState& value) {
				return value.stationId == platformIcon->stationId && value.platformId == platformIcon->platformId;
			});
		if (platformState == m_snapshot->platforms.end())
			continue;

		// remove existing pax icons
		std::string currentlyHighlightedPaxID;
		bool selectionCueLost = false;
		for (auto* icon : platformIcon->passengerIcons) {
			// if icon had a message, store its pax ID
			if (icon == paxIconItem) {
				currentlyHighlightedPaxID = icon->passengerId;
				paxIconItem = nullptr;
			}
			// the icon deletes its highlight effect
			if (effect && icon->graphicsEffect() == effect)
				selectionCueLost = true;

			scene->removeItem(icon);
		}
		qDeleteAll(platformIcon->passengerIcons.begin(), platformIcon->passengerIcons.end());
		platformIcon->passengerIcons.clear();

		const auto& passengerIds = platformState->passengerIds;
		int nPaxIcons = static_cast<int>(passengerIds.size());

		// string with pax counter
		std::stringstream ss;
		ss << "Pax on platform: " << nPaxIcons << "\nMax pax volume: " << platformState->maxVolume;

		// convert string
		QString text = QString::fromStdString(ss.str());

		// update text item
		auto originalCenter = platformIcon->textIcon->boundingRect().center();
		platformIcon->textIcon->setPlainText(text);
		platformIcon->textIcon->setVisible(paxTextVisible());
		auto newCenter = platformIcon->textIcon->boundingRect().center();
		auto delta = (originalCenter - newCenter) * presentationScale();
		platformIcon->textIcon->moveBy(delta.x(), delta.y());

		// add icons
		if (nPaxIcons > 0) {
			qreal iconSpacing = platformIcon->sceneBoundingRect().width() / nPaxIcons;
			qreal iconX = platformIcon->sceneBoundingRect().left() + iconSpacing / 2;

			for (const std::string& paxID : passengerIds) {
				const qreal iconSize = pax_pixmap_scaled.width() * presentationScale();
				auto* item = new PassengerItem(pax_pixmap_scaled);
				item->setScale(presentationScale());
				item->setPos(QPointF(iconX - iconSize / 2, platformIcon->sceneBoundingRect().center().y() - iconSize / 2));
				item->setTransformationMode(Qt::SmoothTransformation);

				item->passengerId = paxID;

				// update current highlighted pax icon
				if (!currentlyHighlightedPaxID.empty() && item->passengerId == currentlyHighlightedPaxID) {
					paxIconItem = item;
				}

				scene->addItem(item);
				if (selectionCueLost && item->passengerId == currentlyHighlightedPaxID) {
					effect = new HighlightEffect(Qt::blue, 1);
					item->setGraphicsEffect(effect);
					selectionCueLost = false;
				}
				item->setVisible(m_passengerLayerVisible);
				platformIcon->passengerIcons.push_back(item);

				iconX += iconSpacing;
			}
		}
	}
}

// slot to update passenger counter from clicked train
void MainWindow::updateTrainPaxInfo() {
	if (!m_passengerLayerVisible) {
		if (trainPaxInfoItem)
			trainPaxInfoItem->setVisible(false);
		return;
	}
	if (trainPaxInfoItem) {
		// remove existing item
		removeTrainPaxInfoIcon();
		if (trainPaxItem)
			paintTrainPassengerInfo(trainPaxItem);
	}
}

// slot to update passenger info from clicked passenger icon
void MainWindow::updatePaxIconInfo() {
	if (!m_passengerLayerVisible) {
		if (paxIconInfoItem)
			paxIconInfoItem->setVisible(false);
		return;
	}
	if (paxIconInfoItem) {
		// remove existing item
		removePaxInfoIcon();
		if (paxIconItem)
			paintPassengerInfoIcon(paxIconItem);
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

void MainWindow::updateBlockOccupationStatus(const GuiTrainState& train) {
	for (const GuiOccupiedArc& occupiedArc : train.occupiedArcs) {
		auto it = m_tracksByOccupiedArc.find({occupiedArc.trackId, occupiedArc.startX});
		if (it == m_tracksByOccupiedArc.end() || !it->second)
			continue;
		TrackLineItem* track = it->second;
		if (trackStatePriority(TrackOperationalState::Occupied) >= trackStatePriority(track->operationalState())) {
			track->setOperationalState(TrackOperationalState::Occupied);
			m_activeTrackItems.insert(track);
		}
	}
}

void MainWindow::clearOperationalTrackStates() {
	for (auto* track : m_activeTrackItems)
		if (track)
			track->setOperationalState(TrackOperationalState::Free);
	m_activeTrackItems.clear();
}

void MainWindow::updateSignalAspect(const GuiSignalState& state, std::set<const SignalItem*>& updated) {
	const auto heads = m_signalsByAheadId.find(state.sectionId);
	if (heads == m_signalsByAheadId.end())
		return;
	for (auto* signal : heads->second) {
		if (signal->reversedDirection != state.reversedDirection)
			continue;
		signal->setAspectCode(guiSignalDisplayCode(state));
		signal->setFailed(state.failed);
		updated.insert(signal);
	}
}

// updates train positions given a specific timestep
void MainWindow::updateTrainPosition(int t) {
	if (!m_snapshot)
		return;
	bool legendNeedsUpdate = false;
	// The camera was given the position of the followed train in this update.
	bool followedTrainMoved = false;
	for (auto* group : m_vcMessageItems)
		if (group)
			group->setVisible(false);
	// update every train
	clearOperationalTrackStates();
	const auto applySectionState = [this](const GuiSectionState& state, TrackOperationalState visualState) {
		if (!state.prepared && visualState == TrackOperationalState::Prepared)
			return;
		if (!state.blocked && visualState == TrackOperationalState::Blocked)
			return;
		auto tracks = m_tracksBySectionId.find(state.sectionId);
		if (tracks == m_tracksBySectionId.end())
			return;
		for (auto* track : tracks->second) {
			if (!track || trackStatePriority(visualState) < trackStatePriority(track->operationalState()))
				continue;
			track->setOperationalState(visualState);
			m_activeTrackItems.insert(track);
		}
	};
	for (const GuiSectionState& state : m_snapshot->sectionStates)
		if (state.prepared)
			applySectionState(state, TrackOperationalState::Prepared);
	for (const GuiTrainState& state : m_snapshot->trains)
		if (!m_replayActive || guiReplayTrainHasPosition(state, t))
			updateBlockOccupationStatus(state);
	for (const GuiTrainState& state : m_snapshot->trains) {
		const int train = state.index;
		// Throttled delivery can skip the whole visible lifetime of a train.
		if (m_followTrainIndex == train) {
			const FollowAvailability following = followAvailabilityOf(train);
			if (following.switchOff) {
				setFollowTrain(-1);
				statusBar()->showMessage(QString::fromStdString(following.statusText), 5000);
			}
		}
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
			trainItem->outOfSimulation = state.outOfSimulation;
			trainItem->currentOnboardPassengers = state.currentOnboardPassengers;
			trainItem->maxOnboardPassengers = state.maxOnboardPassengers;
			const bool hasPosition = guiReplayTrainHasPosition(state, t);
			trainItem->setVisible(m_trainLayerVisible && (m_replayActive ? hasPosition : !state.outOfSimulation));
			if (m_replayActive && !hasPosition && m_trainBadges.contains(train))
				m_trainBadges[train]->hide();
			// update train position
			if (!state.outOfSimulation && (!m_replayActive || hasPosition)) {
				// Capture the currently displayed center before replacing the polygons.
				// An interrupted animation leaves a temporary group offset that must
				// not become part of the next geometry center.
				const QPointF oldCenter = trainItem->sceneBoundingRect().center();
				auto prevIt = m_prevTrainPositions.find(train);
				const QPointF oldBadgeCenter = prevIt != m_prevTrainPositions.end()
					? prevIt.value()
					: oldCenter;
				stopTrainAnimation(train);
				trainItem->setPos(QPointF(0, 0));
				TrainBadgeItem* badge = m_trainBadges.value(train, nullptr);
				if (badge)
					badge->setPos(oldBadgeCenter);

				const bool hasVisibleGeometry = getTrainPolygonItemList(
					trainItem->trainPolygonItemList, state);
				trainItem->setVisible(m_trainLayerVisible && hasVisibleGeometry);
				checkVCouplingMsg(trainItem, state, t);

				// animate smooth transition from old position to new position
				QPointF newCenter = trainItem->sceneBoundingRect().center();
				if (badge) {
					badge->setIdentifier(QString::fromStdString(guiTrainDisplayIdentifier(state)));
					badge->setTooltipDetails(QString::fromStdString(state.description),
						QString::fromStdString(state.operatingCode), QString::fromStdString(state.type));
					badge->setSpeedText(QString::fromStdString(formatSpeedLabel(state.speedKmh)));
					badge->setSpeedVisible(m_trainSpeedLabelsVisible);
					badge->setReversed(state.reversedDirection);
					const bool promoted = isTrainOverlayPromoted(train);
					badge->setPromoted(promoted);
					badge->setPresentation(TrainBadgeItem::presentationForZoom(
						networkView ? networkView->zoomRatio() : 1.0, promoted));
					badge->setVisible(m_trainLayerVisible && hasVisibleGeometry);
				}
				m_prevTrainPositions[train] = newCenter;
				QPointF delta = oldCenter - newCenter;
				const QPointF badgeCenter = newCenter;
				if (!m_replayActive && delta.manhattanLength() > 0.5) {
					stopTrainAnimation(train);
					trainItem->setPos(delta);
					if (badge)
						badge->setPos(badgeCenter + delta);
					QVariantAnimation* interp = new QVariantAnimation(this);
					m_trainAnimations[train] = interp;
					interp->setDuration(120);
					interp->setStartValue(QVariant::fromValue(delta));
					interp->setEndValue(QVariant::fromValue(QPointF(0, 0)));
					interp->setEasingCurve(QEasingCurve::Linear);
					connect(interp, &QVariantAnimation::valueChanged, this, [trainItem, badge, badgeCenter](const QVariant& val) {
						QEGTRAIN_PROFILE_SCOPE("gui/train_animation/value_changed", "gui", "");
						QPointF offset = val.toPointF();
						trainItem->setPos(offset);
						if (badge)
							badge->setPos(badgeCenter + offset);
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
					if (badge)
						badge->setPos(badgeCenter);
				}
				if (m_followTrainIndex == train) {
					const FollowAvailability following = followAvailabilityOf(train);
					if (following.canAct) {
						m_followCamera->follow(newCenter, !following.glide);
						followedTrainMoved = true;
					} else {
						m_followCamera->stop();
					}
				}
			}
			// hide train whose simulation is finished
			else {
				stopTrainAnimation(train);
				trainItem->hide();
				if (m_trainBadges.contains(train))
					m_trainBadges[train]->setVisible(false);
				if (m_followTrainIndex == train)
					m_followCamera->stop();
			}
		}
		// new train starting; >= not == because the frame throttle may drop
		// the exact departure frame, and the recorded trajectory (not the live
		// train state) is what says whether the train is on the network at t
		else if (m_replayActive ? guiReplayTrainHasPosition(state, t)
								: t >= state.departureTime && state.routeAxisPosition != -9999) {
			const bool firstTrain = allTrains.isEmpty();
			paintTrain(state, node_size, line_width);
			legendNeedsUpdate = true;
			TrainItemGroup* newTrain = resolveTrainItem(train);
			if (firstTrain && !m_replayActive && newTrain && newTrain->isVisible())
				centerSceneItem(newTrain);
			if (m_followTrainIndex == train) {
				cutToFollowedTrain(followAvailabilityOf(train));
				followedTrainMoved = true;
			}
		}
	}
	for (const GuiSectionState& state : m_snapshot->sectionStates)
		if (state.blocked)
			applySectionState(state, TrackOperationalState::Blocked);
	if (legendNeedsUpdate)
		updateNetworkLegend();
	// A move of the camera does not refresh the overlays, so the station of the followed train is refreshed here.
	if (followedTrainMoved)
		updateViewportOverlays();
	updateFollowAvailability();
}

// returns the full train shape (list of polygons)
bool MainWindow::getTrainPolygonItemList(QList<TrainBodyItem*>* trainPolygonItemList, const GuiTrainState& train) {
	if (!trainPolygonItemList)
		return false;
	for (TrainBodyItem* body : *trainPolygonItemList) {
		if (!body)
			continue;
		if (auto* group = qgraphicsitem_cast<TrainItemGroup*>(body->parentItem()))
			group->prepareForChildGeometryChange();
		break;
	}
	bool hasVisibleGeometry = false;
	// get the polygon of each wagon
	for (int wagon = 0; wagon <= train.wagonCount; wagon++) {
		if (wagon >= trainPolygonItemList->size())
			return hasVisibleGeometry;
		QPolygonF trainPolygon;
		getTrainPolygon(&trainPolygon, wagon, train);
		trainPolygonItemList->at(wagon)->setPolygon(trainPolygon);
		hasVisibleGeometry = hasVisibleGeometry || !trainPolygon.isEmpty();
	}
	return hasVisibleGeometry;
}

// returns the shape of an one-wagon train (polygon)
// used to get each wagon of a multi-wagon train
void MainWindow::getTrainPolygon(QPolygonF* trainPolygon, int wagon, const GuiTrainState& train) {
	if (!trainPolygon || train.routeIndex < 0 || train.routeIndex >= static_cast<int>(train_route.size()))
		return;
	double headX, tailX, connectionX1, connectionX2, x0, x1, x2, x3, y0, y1, y2, y3;
	double dot, det, beta, alpha, gamma;
	std::vector<double> posX, routeX;
	QPointF ptTrainEdge, ptConnection1, ptConnection2, ptBegin1, ptEnd2;
	std::string ID1, ID2, connection1, connection2;
	const Section* currBS = nullptr;
	const Section* BS1 = nullptr;
	const Section* BS2 = nullptr;
	QVector<QPointF> trainPointsUp, trainPointsDown, trainPointsComplete;
	QVector<int> trainPointsStIndex, trainPointsStRegion, trainPointsGraphID;
	int revStIndex; // used to add station points to the polygon (needed for reversed routes)
	double total_nW = train.wagonCount + 1;
	const auto addPreviewPoint = [&](const TrackPreviewLine* line, double rawX,
									 const QPointF& point) {
		QPointF normal;
		if (!line || !previewSignalNormal(*line, rawX, normal))
			return false;
		const QPointF offset = normal * (0.10 * track_separation * presentationScale());
		trainPointsUp.push_back(point - offset);
		trainPointsDown.push_front(point + offset);
		trainPointsStIndex.push_back(-1);
		trainPointsStRegion.push_back(-1);
		trainPointsGraphID.push_back(INT_MIN);
		return true;
	};
	const auto addTrackPoint = [&](const QPointF& point, int track, double rawX) {
		if (const auto* line = cachedTrackLine(track))
			return addPreviewPoint(line, rawX, point);
		int stationIdx[2];
		neighbourStations(rawX, track, stationIdx);
		const int prevIndex = stationIdx[0];
		const int index = stationIdx[1];
		const int revPrevIndex = index * (1 - revStIndex) + prevIndex * revStIndex;
		const int region = blockSets[track].region;
		trainPointsUp.push_back(QPointF(point.x()
				- StationArray[index].signalDeltaX[region] * 0.10 * track_separation,
			point.y() - StationArray[index].signalDeltaY[region] * 0.10 * track_separation));
		trainPointsDown.push_front(QPointF(point.x()
				+ StationArray[index].signalDeltaX[region] * 0.10 * track_separation,
			point.y() + StationArray[index].signalDeltaY[region] * 0.10 * track_separation));
		trainPointsStIndex.push_back(revPrevIndex);
		trainPointsStRegion.push_back(region);
		trainPointsGraphID.push_back(blockSets[track].graphID);
		return true;
	};

	// train position on X axis
	// reversed route
	if (wagon >= static_cast<int>(train.wagonHeadPositions.size()) || wagon >= static_cast<int>(train.wagonTailPositions.size()))
		return;
	headX = train.wagonHeadPositions[static_cast<std::size_t>(wagon)];
	tailX = train.wagonTailPositions[static_cast<std::size_t>(wagon)];
	revStIndex = train.reversedDirection ? 1 : 0;
	const Route& route = train_route[train.routeIndex];
	const double routePosition = train.routeAxisPosition;

	// get occupied signalling_block_sections (list of index on SeqBS)
	std::vector<int> occupiedBS; // we already know head signalling_block_sections but not the index on SeqBS

	// find block sections where the train is (full length)
	int headIndex = -1, tailIndex = -1;
	for (int h = route.N_Block_Sections - 1; h >= 0; h--) {
		if (((routePosition - wagon * (train.length / total_nW)) < route.sequence_of_block_sections[h].end_node.X * 1000) && ((routePosition - wagon * (train.length / total_nW)) >= route.sequence_of_block_sections[h].start_node.X * 1000)) {
			occupiedBS.push_back(h);
			headIndex = h;
		}
		if (((routePosition - (wagon + 1) * (train.length / total_nW)) < route.sequence_of_block_sections[h].end_node.X * 1000) && ((routePosition - (wagon + 1) * (train.length / total_nW)) >= route.sequence_of_block_sections[h].start_node.X * 1000)) {
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
		currBS = &route.sequence_of_block_sections[occupiedBS[i]];

		if (headIndex == tailIndex && tailIndex == occupiedBS[i]) {
			posX = {headX, tailX};
			routeX = {(routePosition / 1000) - wagon * ((train.length / 1000) / total_nW), (routePosition / 1000) - (wagon + 1) * ((train.length / 1000) / total_nW)};
		} // head and tail in this section
		else if (occupiedBS[i] == headIndex) {
			posX = {headX};
			routeX = {(routePosition / 1000) - wagon * ((train.length / 1000) / total_nW)};
		} // only head
		else if (occupiedBS[i] == tailIndex) {
			posX = {tailX};
			routeX = {(routePosition / 1000) - (wagon + 1) * ((train.length / 1000) / total_nW)};
		} // only tail
		else {
			posX = {-DBL_MAX};
			routeX = {-DBL_MAX};
		} // section in between (no head or tail)

		// in case head and tail in the same signalling_block_sections (need to get both)
		for (int j = 0; j < posX.size(); j++) {
			// single signalling_block_sections -> get head or tail
			if (currBS->ID.find('/') == std::string::npos) {
				if (occupiedBS[i] != headIndex && occupiedBS[i] != tailIndex) {
					continue;
				} // single section in between (no relevant points)

				const TrackPreviewLine* previewLine = cachedTrackLine(currBS->trackLineId);
				if ((!m_cachedTrackPreview.lines.empty() && !previewLine)
					|| (!previewLine && !hasTrackGeometry(currBS->trackLineId))) {
					continue;
				}

				// get point
				egtrainPoint2Screen(posX[j], currBS->trackLineId, track_separation, ptTrainEdge.rx(), ptTrainEdge.ry());
				if (!addTrackPoint(ptTrainEdge, currBS->trackLineId, posX[j]))
					return;
			}
			// compound signalling_block_sections -> get connection points and/or tail or head
			else {
				int index1 = currBS->ID.find("@-");
				int index2 = currBS->ID.find("/@", index1 + 1);
				int index3 = currBS->ID.find("@-", index2 + 1);
				if (index1 != std::string::npos && index2 != std::string::npos && index3 != std::string::npos) {
					ID1 = currBS->ID.substr(0, index1 + 1);
					ID2 = currBS->ID.substr(index2 + 1, index3 - index2);
					connection1 = currBS->ID.substr(index1 + 2, index2 - index1 - 2);
					connection2 = currBS->ID.substr(index3 + 2, string::npos);
					connectionX1 = atof(connection1.c_str());
					connectionX2 = atof(connection2.c_str());

					// find single block sections (needed for the graphID's)
					BS1 = nullptr;
					BS2 = nullptr;
					for (int b = 0; b < Blocks; b++) {
						if (ID1.compare(signalling_block_sections[b].ID) == 0) {
							BS1 = &signalling_block_sections[b];
						} else if (ID2.compare(signalling_block_sections[b].ID) == 0) {
							BS2 = &signalling_block_sections[b];
						}
					}
					if (!BS1 || !BS2)
						continue;
					const TrackPreviewLine* previewLine1 = cachedTrackLine(BS1->trackLineId);
					const TrackPreviewLine* previewLine2 = cachedTrackLine(BS2->trackLineId);
					const bool hasPreview = !m_cachedTrackPreview.lines.empty();
					const qreal lateralSeparation = track_separation * (hasPreview ? presentationScale() : 1.0);
					if ((hasPreview && (!previewLine1 || !previewLine2))
						|| (!hasPreview && (!hasTrackGeometry(BS1->trackLineId) || !hasTrackGeometry(BS2->trackLineId))))
						continue;

					// get beginning and end of connection to interpolate
					egtrainPoint2Screen(connectionX1, BS1->trackLineId, track_separation, ptConnection1.rx(), ptConnection1.ry());
					egtrainPoint2Screen(connectionX2, BS2->trackLineId, track_separation, ptConnection2.rx(), ptConnection2.ry());

					// get beginning of BS1 and end of BS2 to calculate shifts
					egtrainPoint2Screen(BS1->start_node.X, BS1->trackLineId, track_separation, ptBegin1.rx(), ptBegin1.ry());
					egtrainPoint2Screen(BS2->end_node.X, BS2->trackLineId, track_separation, ptEnd2.rx(), ptEnd2.ry());

					int revPrevIndexConnection1 = -1;
					int revPrevIndexConnection2 = -1;
					if (!hasPreview) {
						// get station indexes (used to add station points)
						int stationIdx[2];
						neighbourStations(connectionX1, BS1->trackLineId, stationIdx);
						revPrevIndexConnection1 = stationIdx[1] * (1 - revStIndex) + stationIdx[0] * revStIndex;
						neighbourStations(connectionX2, BS2->trackLineId, stationIdx);
						revPrevIndexConnection2 = stationIdx[1] * (1 - revStIndex) + stationIdx[0] * revStIndex;
					}

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
					if (hasPreview) {
						QPointF normal;
						if (!previewSignalNormal(*previewLine1, connectionX1, normal))
							return;
						connectionStartShiftX = normal.x();
						connectionStartShiftY = normal.y();
						if (!previewSignalNormal(*previewLine2, connectionX2, normal))
							return;
						connectionEndShiftX = normal.x();
						connectionEndShiftY = normal.y();
					}

					// calculate connection deltas (inside connection)
					beta = atan2(-(y1 - y2), (x1 - x2));
					double connectionDeltaX = sin(beta);
					double connectionDeltaY = cos(beta);

					// add connection points if it is a middle section
					// add connection points if it is the tail signalling_block_sections and head already added
					if ((posX[j] == -DBL_MAX) || (posX.size() == 1 && occupiedBS[i] == tailIndex)) { // adding connection points before adding tail of the train
						// non-reversed route (add first connection 2)
						if (!train.reversedDirection) {
							if (headX > connectionX2 && tailX < connectionX2) {
								trainPointsUp.push_back(QPointF(ptConnection2.x() - connectionEndShiftX * 0.10 * lateralSeparation, ptConnection2.y() - connectionEndShiftY * 0.10 * lateralSeparation));
								trainPointsDown.push_front(QPointF(ptConnection2.x() + connectionEndShiftX * 0.10 * lateralSeparation, ptConnection2.y() + connectionEndShiftY * 0.10 * lateralSeparation));
								trainPointsStIndex.push_back(revPrevIndexConnection2);
								trainPointsStRegion.push_back(blockSets[BS2->trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS2->trackLineId].graphID);
							}
							if (headX > connectionX1 && tailX < connectionX1) {
								trainPointsUp.push_back(QPointF(ptConnection1.x() - connectionStartShiftX * 0.10 * lateralSeparation, ptConnection1.y() - connectionStartShiftY * 0.10 * lateralSeparation));
								trainPointsDown.push_front(QPointF(ptConnection1.x() + connectionStartShiftX * 0.10 * lateralSeparation, ptConnection1.y() + connectionStartShiftY * 0.10 * lateralSeparation));
								trainPointsStIndex.push_back(revPrevIndexConnection1);
								trainPointsStRegion.push_back(blockSets[BS1->trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS1->trackLineId].graphID);
							}
						}
						// reversed route (add first connection 1)
						else {
							if (tailX > connectionX1 && headX < connectionX1) {
								trainPointsUp.push_back(QPointF(ptConnection1.x() - connectionStartShiftX * 0.10 * lateralSeparation, ptConnection1.y() - connectionStartShiftY * 0.10 * lateralSeparation));
								trainPointsDown.push_front(QPointF(ptConnection1.x() + connectionStartShiftX * 0.10 * lateralSeparation, ptConnection1.y() + connectionStartShiftY * 0.10 * lateralSeparation));
								trainPointsStIndex.push_back(revPrevIndexConnection1);
								trainPointsStRegion.push_back(blockSets[BS1->trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS1->trackLineId].graphID);
							}
							if (tailX > connectionX2 && headX < connectionX2) {
								trainPointsUp.push_back(QPointF(ptConnection2.x() - connectionEndShiftX * 0.10 * lateralSeparation, ptConnection2.y() - connectionEndShiftY * 0.10 * lateralSeparation));
								trainPointsDown.push_front(QPointF(ptConnection2.x() + connectionEndShiftX * 0.10 * lateralSeparation, ptConnection2.y() + connectionEndShiftY * 0.10 * lateralSeparation));
								trainPointsStIndex.push_back(revPrevIndexConnection2);
								trainPointsStRegion.push_back(blockSets[BS2->trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS2->trackLineId].graphID);
							}
						}
					}

					// head/tail in the 1st track and before connection
					if (routeX[j] >= currBS->start_node.X && posX[j] <= connectionX1) {
						egtrainPoint2Screen(posX[j], BS1->trackLineId, track_separation, ptTrainEdge.rx(), ptTrainEdge.ry());
						if (!addTrackPoint(ptTrainEdge, BS1->trackLineId, posX[j]))
							return;
					}
					// head/tail inside the connection
					else if (posX[j] > connectionX1 && posX[j] < connectionX2) {
						ptTrainEdge.setX(x1 + (((posX[j] - connectionX1) / (connectionX2 - connectionX1)) * (x2 - x1)));
						ptTrainEdge.setY(y1 + (((posX[j] - connectionX1) / (connectionX2 - connectionX1)) * (y2 - y1)));

						// train polygon
						trainPointsUp.push_back(QPointF(ptTrainEdge.x() - connectionDeltaX * 0.10 * lateralSeparation, ptTrainEdge.y() - connectionDeltaY * 0.10 * lateralSeparation));
						trainPointsDown.push_front(QPointF(ptTrainEdge.x() + connectionDeltaX * 0.10 * lateralSeparation, ptTrainEdge.y() + connectionDeltaY * 0.10 * lateralSeparation));
						trainPointsStIndex.push_back(-1);
						trainPointsStRegion.push_back(-1);
						trainPointsGraphID.push_back(INT_MIN);
					}
					// head/tail in the 2nd track and after connection
					else if (posX[j] >= connectionX2 && routeX[j] < currBS->end_node.X) {
						egtrainPoint2Screen(posX[j], BS2->trackLineId, track_separation, ptTrainEdge.rx(), ptTrainEdge.ry());
						if (!addTrackPoint(ptTrainEdge, BS2->trackLineId, posX[j]))
							return;
					}

					// add connection points if it is head section and tail is in another signalling_block_sections
					// add connection points if head and tail in the same section
					if ((occupiedBS[i] == headIndex && headIndex != tailIndex) || (posX.size() == 2 && j == 0)) { // adding connection points after adding head of the train
						// non-reversed route (add first connection 2)
						if (!train.reversedDirection) {
							if (headX > connectionX2 && tailX < connectionX2) {
								trainPointsUp.push_back(QPointF(ptConnection2.x() - connectionEndShiftX * 0.10 * lateralSeparation, ptConnection2.y() - connectionEndShiftY * 0.10 * lateralSeparation));
								trainPointsDown.push_front(QPointF(ptConnection2.x() + connectionEndShiftX * 0.10 * lateralSeparation, ptConnection2.y() + connectionEndShiftY * 0.10 * lateralSeparation));
								trainPointsStIndex.push_back(revPrevIndexConnection2);
								trainPointsStRegion.push_back(blockSets[BS2->trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS2->trackLineId].graphID);
							}
							if (headX > connectionX1 && tailX < connectionX1) {
								trainPointsUp.push_back(QPointF(ptConnection1.x() - connectionStartShiftX * 0.10 * lateralSeparation, ptConnection1.y() - connectionStartShiftY * 0.10 * lateralSeparation));
								trainPointsDown.push_front(QPointF(ptConnection1.x() + connectionStartShiftX * 0.10 * lateralSeparation, ptConnection1.y() + connectionStartShiftY * 0.10 * lateralSeparation));
								trainPointsStIndex.push_back(revPrevIndexConnection1);
								trainPointsStRegion.push_back(blockSets[BS1->trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS1->trackLineId].graphID);
							}
						}
						// reversed route (add first connection 1)
						else {
							if (tailX > connectionX1 && headX < connectionX1) {
								trainPointsUp.push_back(QPointF(ptConnection1.x() - connectionStartShiftX * 0.10 * lateralSeparation, ptConnection1.y() - connectionStartShiftY * 0.10 * lateralSeparation));
								trainPointsDown.push_front(QPointF(ptConnection1.x() + connectionStartShiftX * 0.10 * lateralSeparation, ptConnection1.y() + connectionStartShiftY * 0.10 * lateralSeparation));
								trainPointsStIndex.push_back(revPrevIndexConnection1);
								trainPointsStRegion.push_back(blockSets[BS1->trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS1->trackLineId].graphID);
							}
							if (tailX > connectionX2 && headX < connectionX2) {
								trainPointsUp.push_back(QPointF(ptConnection2.x() - connectionEndShiftX * 0.10 * lateralSeparation, ptConnection2.y() - connectionEndShiftY * 0.10 * lateralSeparation));
								trainPointsDown.push_front(QPointF(ptConnection2.x() + connectionEndShiftX * 0.10 * lateralSeparation, ptConnection2.y() + connectionEndShiftY * 0.10 * lateralSeparation));
								trainPointsStIndex.push_back(revPrevIndexConnection2);
								trainPointsStRegion.push_back(blockSets[BS2->trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS2->trackLineId].graphID);
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
		if ((trainPointsStIndex[i - 1] == -1 && i >= 2 && trainPointsStIndex[i] != trainPointsStIndex[i - 2]
				&& trainPointsStRegion[i] == trainPointsStRegion[i - 2])
			|| (trainPointsStIndex[i - 1] != -1 && trainPointsStIndex[i] != trainPointsStIndex[i - 1]
				&& trainPointsStRegion[i] == trainPointsStRegion[i - 1])) {

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

// Choose a reference from routes that were actually used in this run.
int chooseRouteDiagramReference(QWidget* parent, const SceneModel& model, const QString& purpose) {
	const QVector<RouteReferenceChoice> choices = buildRouteReferenceChoices(model, usedRouteReferences());
	if (choices.isEmpty()) return -1;
	if (e2eDialogsSuppressed()) return choices.first().runtimeIndex;
	RouteReferenceDialog dialog(choices, purpose, parent);
	return dialog.exec() == QDialog::Accepted ? dialog.selectedRuntimeIndex() : -1;
}

void MainWindow::displayTrainPathDiagrams() {
	if (!hasRunResults()) return;
	const int reference = chooseRouteDiagramReference(this, m_sceneModel, "train paths");
	if (reference >= 0) buildRouteDiagram(false, reference);
}

// manage VCoupling notifications
void MainWindow::checkVCouplingMsg(TrainItemGroup* trainItem, const GuiTrainState& train, int t) {
	if (!m_snapshot)
		return;
	for (auto it = m_snapshot->virtualCouplingMessages.rbegin(); it != m_snapshot->virtualCouplingMessages.rend(); ++it) {
		if (it->trainDescription == train.description && t >= it->timestep && t <= it->timestep + 40) {
			paintVCouplingMsg(trainItem, it->message);
			break;
		}
	}
}

// paint VCoupling notification
void MainWindow::paintVCouplingMsg(TrainItemGroup* trainItem, const std::string& message) {
	if (!trainItem || !trainItem->trainPolygonItemList || trainItem->trainPolygonItemList->isEmpty())
		return;
	TrainBodyItem* headPolygon = trainItem->trainPolygonItemList->at(0);
	if (!headPolygon || headPolygon->polygon().isEmpty())
		return;
	QPointF frontUp = headPolygon->polygon().first();
	QPointF frontDown = headPolygon->polygon().last();

	QPointF start = frontUp + frontDown;
	start /= 2;
	QPointF end = start + QPointF(0, -2 * track_separation * presentationScale());

	QPen pen = QPen(QColor(242, 161, 106));
	pen.setWidth(line_width);
	pen.setCosmetic(true);
	QGraphicsItemGroup* msgGroup = m_vcMessageItems.value(trainItem->index, nullptr);
	QGraphicsLineItem* line = nullptr;
	QGraphicsTextItem* text = nullptr;
	QGraphicsRectItem* textBox = nullptr;
	if (!msgGroup) {
		msgGroup = new QGraphicsItemGroup;
		line = new QGraphicsLineItem;
		textBox = new QGraphicsRectItem;
		text = new QGraphicsTextItem;
		msgGroup->addToGroup(line);
		msgGroup->addToGroup(textBox);
		msgGroup->addToGroup(text);
		msgGroup->setZValue(5);
		scene->addItem(msgGroup);
		m_vcMessageItems.insert(trainItem->index, msgGroup);
	} else {
		for (QGraphicsItem* child : msgGroup->childItems()) {
			if (auto* item = qgraphicsitem_cast<QGraphicsLineItem*>(child))
				line = item;
			else if (auto* item = qgraphicsitem_cast<QGraphicsRectItem*>(child))
				textBox = item;
			else if (auto* item = qgraphicsitem_cast<QGraphicsTextItem*>(child))
				text = item;
		}
	}
	if (!line || !text || !textBox)
		return;

	line->setLine(QLineF(start.x(), start.y(), end.x(), end.y()));
	line->setPen(pen);

	QFont font = QFont();
	font.setPixelSize((int)station_size / 6);
	text->setPlainText(QString::fromStdString(message));
	text->setDefaultTextColor(Qt::white);
	text->setFont(font);
	text->setScale(presentationScale());

	textBox->setRect(QRectF(0, 0, 1.25 * (text->boundingRect().width() * presentationScale()), 1.25 * (text->boundingRect().height() * presentationScale())));
	textBox->setBrush(QColor(242, 161, 106));
	QPen boxPen = textBox->pen();
	boxPen.setWidthF(presentationScale());
	textBox->setPen(boxPen);
	textBox->setPos(end.x() - (textBox->boundingRect().width() / 2), end.y() - textBox->boundingRect().height());

	QPointF textPos = textBox->pos();
	textPos.rx() += 0.5 * (textBox->boundingRect().width() - (text->boundingRect().width() * presentationScale()));
	textPos.ry() += 0.5 * (textBox->boundingRect().height() - (text->boundingRect().height() * presentationScale()));
	text->setPos(textPos);
	msgGroup->setVisible(true);
}

// --- Zoom controls ---
void MainWindow::zoomIn() {
	if (!networkView)
		return;
	if (networkView->zoomBy(1.15)) {
		if (!m_previewFitBounds.isEmpty())
			networkView->centerOn(m_previewZoomFocus);
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

	const char* titles[] = {"Speed vs Distance", "Speed vs Time", "Time vs Distance", "Simulated Tractive Effort vs Distance"};
	const QString title = QString("%1 [%2]").arg(titles[mode], completedRunContext(m_completedRunProvenance));
	QChart* chart = new QChart();
	chart->setTitle(title);

	// one series per contiguous segment; legend distinguishes trains
	for (int tr = 0; tr < numRegions; tr++) {
		Train& train = regional_train[tr];
		if (train.trajectorySize() == 0 || train.earliestActiveTrajectoryIndex < 0)
			continue;

		for (const auto& segment : validTrajectorySegments(train.instant_spatial_position,
				 train.earliestActiveTrajectoryIndex, train.End_Time)) {
			QLineSeries* series = new QLineSeries();
			series->setName(QString::fromStdString(train.trainDescription));
			series->setProperty("trainId", QString::fromStdString(train.trainDescription));
			for (int i = segment.first; i <= segment.last; i++) {
				double tSec = trajectoryTimeSeconds(i, timestep);
				double dist = train.positionKmAt(i);
				double spd = train.speedKmhAt(i);
				if (mode == 0)
					series->append(dist, spd); // x: distance (km), y: speed (km/h)
				else if (mode == 1)
					series->append(tSec, spd); // x: time (s),      y: speed (km/h)
				else if (mode == 2)
					series->append(tSec, dist); // x: time (s),      y: distance (km)
				else if (i < static_cast<int>(train.instant_train_tractive_effort.size()))
					series->append(dist, train.instant_train_tractive_effort[static_cast<std::size_t>(i)] / 1000.0);
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
	win->setTrainColors(trainColorsForRun(m_completedRunResults, m_sceneModel));
	win->setCsvProvider(snapshotCsv(&buildTrajectoryCsv), "trajectory.csv");
	attachRunProvenance(win, m_completedRunProvenance);
	connect(win, &DiagramWindow::trainSelected, this, &MainWindow::focusTrainInScene);
	win->setTimeAxisX(mode == 1 || mode == 2, m_startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

// Centre the network view on the train selected in a diagram, when it is present.
void MainWindow::focusTrainInScene(const QString& trainId) {
	const std::string id = trainId.toStdString();
	for (TrainItemGroup* item : allTrains) {
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

	auto* window = new TimetableTableWindow(m_completedTimetableResults,
		m_startOffsetSeconds, snapshotCsv(&buildTimetableCsv), this);
	window->setTelemetryCapture([this] { return captureTelemetryOperation(); });
	window->setTrainColors(trainColorsForRun(m_completedRunResults, m_sceneModel));
	window->setRunProvenance(m_completedRunProvenance);
	window->setWindowTitle(QString("Timetable: planned vs simulated [%1]").arg(completedRunContext(m_completedRunProvenance)));
	window->setAttribute(Qt::WA_DeleteOnClose);
	window->show();
}

void MainWindow::showDelayDiagram() {
	if (!hasRunResults()) {
		statusBar()->showMessage("Run a simulation to open diagrams", 5000);
		return;
	}

	QChart* chart = new QChart();
	const QString title = QString("Arrival delay along journey (minutes) [%1]").arg(completedRunContext(m_completedRunProvenance));
	chart->setTitle(title);
	const auto& rows = m_completedTimetableResults;

	for (int i = 0; i < numRegions; i++) {
		QLineSeries* series = new QLineSeries();
		const QString trainName = QString::fromStdString(regional_train[i].trainDescription);
		series->setName(trainName + " (arrival delay)");
		series->setProperty("trainId", trainName);
		bool hasData = false;
		for (const TimetableResultRow& row : rows) {
			if (row.trainId != regional_train[i].trainDescription || !row.arrivalDelaySeconds.available)
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
	win->setTrainColors(trainColorsForRun(m_completedRunResults, m_sceneModel));
	win->setCsvProvider(snapshotCsv(&buildTimetableCsv), "timetable.csv");
	attachRunProvenance(win, m_completedRunProvenance);
	connect(win, &DiagramWindow::trainSelected, this, &MainWindow::focusTrainInScene);
	win->setTimeAxisX(false, m_startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

void MainWindow::showTimetableGraph() {
	if (!hasRunResults()) return;
	const int reference = chooseRouteDiagramReference(this, m_sceneModel, "timetable");
	if (reference >= 0) buildRouteDiagram(true, reference);
}

void MainWindow::buildRouteDiagram(bool timetable, int referenceIndex) {
	if (!hasRunResults() || referenceIndex < 0 || referenceIndex >= static_cast<int>(train_route.size())) return;
	const Route& reference = train_route[referenceIndex];
	const RouteDiagramPath referencePath = routeDiagramPath(reference, &m_sceneModel);
	if (referencePath.nodes.empty()) return;
	std::map<int, RouteDiagramPath> paths;
	std::map<int, RouteDiagramProjection> projections;
	for (int i = 0; i < numRegions; ++i) {
		const int routeIndex = regional_train[i].indexOfRoute;
		if (routeIndex < 0 || routeIndex >= static_cast<int>(train_route.size()) || paths.count(routeIndex)) continue;
		paths.emplace(routeIndex, routeDiagramPath(train_route[routeIndex], &m_sceneModel));
		projections.emplace(routeIndex, buildRouteDiagramProjection(paths.at(routeIndex), referencePath));
	}
	const auto position = [&](const Train& train, double sourceKm) -> std::optional<double> {
		const auto projection = projections.find(train.indexOfRoute);
		return projection == projections.end() ? std::optional<double>() : projection->second.map(sourceKm);
	};
	const double origin = referencePath.nodes.front().positionKm;
	const double end = referencePath.nodes.back().positionKm;
	const QString heading = timetable ? "Timetable graph: planned vs simulated stops"
									  : "Train paths: simulated movement";
	const QString explanation = timetable
		? "Stop arrivals/departures and dwell, not continuous movement."
		: "Recorded position samples, not planned stop times.";
	auto* chart = new QChart();
	chart->setTitle(heading);
	std::vector<std::vector<std::string>> exportRows;
	const auto record = [&](const Train& train, const char* kind, const std::string& station,
							int journey, int call, const RunResultValue& time, std::optional<double> x) {
		exportRows.push_back({train.trainDescription, reference.ID, train_route[train.indexOfRoute].ID,
			kind, station, journey ? std::to_string(journey) : "", call ? std::to_string(call) : "",
			time.available ? csv::formatDouble(time.value) : "", x ? csv::formatDouble(*x) : ""});
	};
	if (timetable) {
		const auto rows = buildTimetableResults(runResultTrainPointers());
		for (int i = 0; i < numRegions; ++i) {
			const Train& train = regional_train[i];
			const auto path = paths.find(train.indexOfRoute);
			const auto projection = projections.find(train.indexOfRoute);
			if (path == paths.end() || projection == projections.end()) continue;
			const QString name = QString::fromStdString(train.trainDescription);
			// Separate series at every absent or unprojectable event. Never connect
			// across a missing schedule value or an unavailable station visit.
			for (int kind = 0; kind < 4; ++kind) {
				QLineSeries* segment = nullptr;
				for (const auto& row : rows) {
					if (row.trainId != train.trainDescription) continue;
					const int stopIndex = row.journeyIndex - 1;
					const auto x = routeDiagramStopPosition(train, stopIndex, path->second, projection->second);
					const RunResultValue time = kind == 0 ? row.plannedArrivalSeconds : kind == 1 ? row.plannedDepartureSeconds
						: kind == 2																  ? row.simulatedArrivalSeconds
																								  : row.simulatedDepartureSeconds;
					const char* event = kind == 0 ? "planned arrival" : kind == 1 ? "planned departure"
						: kind == 2												  ? "simulated arrival"
																				  : "simulated departure";
					record(train, event, row.stationId, row.journeyIndex, row.callIndex, time, x);
					if (!x || !time.available) {
						segment = nullptr;
						continue;
					}
					if (!segment) {
						segment = new QLineSeries();
						segment->setName(name + " (" + event + ")");
						segment->setProperty("trainId", name);
						segment->setPointsVisible(true);
						QPen pen(QColor::fromHsv((i * 71) % 360, 175, kind < 2 ? 170 : 235));
						if (kind < 2) pen.setStyle(Qt::DashLine);
						segment->setPen(pen);
						chart->addSeries(segment);
					}
					appendInspectedPoint(segment, *x, time.value,
						QString("Station: %1 | Call: %2 | Journey: %3 | %4")
							.arg(QString::fromStdString(row.stationId))
							.arg(row.callIndex)
							.arg(row.journeyIndex)
							.arg(event));
				}
			}
			// A dwell is vertical: fixed reference distance, advancing time.
			for (const auto& row : rows) {
				if (row.trainId != train.trainDescription) continue;
				const auto x = routeDiagramStopPosition(train, row.journeyIndex - 1, path->second, projection->second);
				for (int planned = 0; planned < 2; ++planned) {
					const auto arrival = planned ? row.plannedArrivalSeconds : row.simulatedArrivalSeconds;
					const auto departure = planned ? row.plannedDepartureSeconds : row.simulatedDepartureSeconds;
					if (!x || !arrival.available || !departure.available) continue;
					auto* dwell = new QLineSeries();
					dwell->setName(name + (planned ? " (planned dwell)" : " (simulated dwell)"));
					dwell->setProperty("trainId", name);
					QPen pen(QColor::fromHsv((i * 71) % 360, 175, planned ? 170 : 235));
					if (planned) pen.setStyle(Qt::DashLine);
					dwell->setPen(pen);
					const QString context = QString("Station: %1 | Call: %2 | %3 dwell")
												.arg(QString::fromStdString(row.stationId))
												.arg(row.callIndex)
												.arg(planned ? "Planned" : "Simulated");
					appendInspectedPoint(dwell, *x, arrival.value, context);
					appendInspectedPoint(dwell, *x, departure.value, context);
					chart->addSeries(dwell);
				}
			}
		}
	} else {
		for (int i = 0; i < numRegions; ++i) {
			const Train& train = regional_train[i];
			if (train.indexOfRoute < 0 || train.indexOfRoute >= static_cast<int>(train_route.size())) continue;
			if (train.earliestActiveTrajectoryIndex < 0) continue;
			for (const auto& valid : validTrajectorySegments(train.instant_spatial_position,
					 train.earliestActiveTrajectoryIndex, train.End_Time)) {
				QLineSeries* segment = nullptr;
				for (int t = valid.first; t <= valid.last; ++t) {
					const double sourceKm = routeDiagramTrajectoryKm(train.instant_spatial_position[t]);
					const auto x = position(train, sourceKm);
					const RunResultValue time{true, t * timestep};
					record(train, "trajectory", "", 0, 0, time, x);
					if (!x) {
						segment = nullptr;
						continue;
					}
					if (!segment) {
						segment = new QLineSeries();
						segment->setName(QString::fromStdString(train.trainDescription));
						segment->setProperty("trainId", QString::fromStdString(train.trainDescription));
						segment->setProperty("inspectionInterval",
							QString("Route: %1 | Simulated trajectory").arg(QString::fromStdString(train_route[train.indexOfRoute].ID)));
						segment->setPointsVisible(true); // Preserve an isolated valid sample.
						chart->addSeries(segment);
					}
					segment->append(*x, time.value);
					if (segment->count() == 2) segment->setPointsVisible(false);
				}
			}
		}
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
	axisY->setRange(0, std::max(1.0, initial_variables.times * timestep));
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
	win->setTrainColors(trainColorsForRun(m_completedRunResults, m_sceneModel));
	win->setPresentation(heading,
		QString("Reference: %1 | %2 | 0 s = run start; time downward")
			.arg(QString::fromStdString(reference.ID), completedRunContext(m_completedRunProvenance)),
		explanation + " Ambiguous/unmapped portions omitted; no extrapolation.");
	const auto csvRows = std::move(exportRows);
	const auto graphCsv = [csvRows](const QStringList& visible) {
		std::vector<std::string> ids;
		for (const QString& id : visible) ids.push_back(id.toStdString());
		return buildRouteDiagramCsv(csvRows, ids);
	};
	win->setCsvProvider(graphCsv, timetable ? "timetable_graph.csv" : "train_path_graph.csv");
	if (const auto routeDiagramBuilt = mainWindowTestHooks().routeDiagramBuilt)
		routeDiagramBuilt(graphCsv, timetable);
	attachRunProvenance(win, m_completedRunProvenance);
	connect(win, &DiagramWindow::trainSelected, this, &MainWindow::focusTrainInScene);
	win->setTimeAxisY(true, m_startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

void MainWindow::showBlockingTimeDiagram() {
	if (!hasRunResults()) {
		statusBar()->showMessage("Run a simulation to open diagrams", 5000);
		return;
	}

	const std::vector<BlockingTimeDiagramSegment> allSegments = buildAllBlockingTimeSegments();
	BlockingTimeScope scope = defaultBlockingTimeScope();
	if (!e2eDialogsSuppressed() && !chooseBlockingTimeScope(this, scope))
		return;
	const auto scoped = scope.routeIndex >= 0 && scope.trainIds.empty()
		? std::vector<BlockingTimeDiagramSegment>()
		: filterBlockingTimeDiagramSegments(allSegments, scope.trainIds, scope.blockIds,
			  scope.startTime, scope.endTime);
	const int referenceIndex = scope.routeIndex >= 0 ? scope.routeIndex : [&]() {
		for (int i = 0; i < numRegions; ++i)
			if ((scope.trainIds.empty() || std::find(scope.trainIds.begin(), scope.trainIds.end(), regional_train[i].trainDescription) != scope.trainIds.end())
				&& regional_train[i].indexOfRoute >= 0
				&& regional_train[i].indexOfRoute < static_cast<int>(train_route.size()))
				return regional_train[i].indexOfRoute;
		return -1;
	}();
	if (referenceIndex < 0) return;
	const RouteDiagramPath referencePath = routeDiagramPath(train_route[referenceIndex], &m_sceneModel);
	std::map<int, RouteDiagramPath> paths;
	std::map<int, RouteDiagramProjection> projections;
	std::map<std::string, RouteDiagramProjection> byTrain;
	for (int i = 0; i < numRegions; ++i) {
		const Train& train = regional_train[i];
		const int index = train.indexOfRoute;
		if (index < 0 || index >= static_cast<int>(train_route.size())) continue;
		if (!paths.count(index)) {
			paths.emplace(index, routeDiagramPath(train_route[index], &m_sceneModel));
			projections.emplace(index, buildRouteDiagramProjection(paths.at(index), referencePath));
		}
		byTrain.emplace(train.trainDescription, projections.at(index));
	}
	int omitted = 0;
	std::vector<BlockingTimeDiagramSegment> unmapped;
	auto segments = projectBlockingSegments(scoped, byTrain, omitted, &unmapped);
	const auto allExtents = buildBlockSectionExtents(blockSectionInputsForRoute(train_route[referenceIndex]));
	const auto extents = scopedBlockSectionExtents(allExtents, scope.sectionIndices);
	matchBlockingSegmentsToSections(segments, allExtents, referencePath.id);
	if (!scope.sectionIndices.empty())
		segments.erase(std::remove_if(segments.begin(), segments.end(),
						   [&extents](const auto& segment) { return !blockingSegmentInSections(segment, extents); }),
			segments.end());
	const auto allPlannedReferences = buildBlockingTimePlannedReferences(scope, paths, projections, omitted);
	const auto plannedReferences = filterBlockingTimePlannedReferences(
		allPlannedReferences, scope.startTime, scope.endTime);
	const auto plottedPlanned = clipBlockingTimePlannedReferences(
		allPlannedReferences, scope.startTime, scope.endTime);

	QChart* chart = new QChart();
	QString routeScope = QStringLiteral("all routes");
	if (scope.routeIndex >= 0 && scope.routeIndex < static_cast<int>(train_route.size())) {
		const Route& route = train_route[static_cast<std::size_t>(scope.routeIndex)];
		routeScope = QString::fromStdString(route.ID);
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

	std::vector<std::vector<std::string>> trajectoryRows;
	for (int i = 0; i < numRegions; ++i) {
		const Train& train = regional_train[i];
		if (train.earliestActiveTrajectoryIndex < 0) continue;
		const auto projection = byTrain.find(train.trainDescription);
		if (projection == byTrain.end()) continue;
		for (const auto& valid : validTrajectorySegments(train.instant_spatial_position,
				 train.earliestActiveTrajectoryIndex, train.End_Time)) {
			QLineSeries* line = nullptr;
			for (int t = valid.first; t <= valid.last; ++t) {
				const double time = t * timestep;
				const double sourceKm = routeDiagramTrajectoryKm(train.instant_spatial_position[t]);
				const auto x = projection->second.map(sourceKm);
				const bool inScope = x && time >= scope.startTime && time <= scope.endTime
					&& std::any_of(segments.begin(), segments.end(), [&](const auto& block) {
						   return block.trainName == train.trainDescription && time >= block.startTime
							   && time <= block.endTime && *x >= std::min(block.startPositionKm, block.endPositionKm)
							   && *x <= std::max(block.startPositionKm, block.endPositionKm);
					   });
				if (!inScope) {
					line = nullptr;
					continue;
				}
				if (!line) {
					line = new QLineSeries();
					line->setName(QString::fromStdString(train.trainDescription) + " (recorded trajectory)");
					line->setProperty("trainId", QString::fromStdString(train.trainDescription));
					line->setProperty("layer", "recorded trajectory");
					line->setProperty("inspectionInterval", "Recorded simulation samples; no boundary samples inferred");
					line->setPen(QPen(QColor(20, 35, 45), 2.5));
					line->setPointsVisible(true);
					chart->addSeries(line);
				}
				line->append(*x, time);
				if (line->count() == 2) line->setPointsVisible(false);
				trajectoryRows.push_back({train.trainDescription, "", "", "", csv::formatDouble(*x),
					"recorded sample", "", "", csv::formatDouble(time), "", "", "", "", "", "", "", "", "", "", "", "", "recorded trajectory"});
			}
		}
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
	axisY->setRange(0, std::max(1.0, initial_variables.times * timestep));

	DiagramWindow* win = new DiagramWindow("Blocking time: envelopes and trajectories", this);
	win->setTelemetryCapture([this] { return captureTelemetryOperation(); });
	win->setChart(chart);
	win->setBlockSections(extents, QString::fromStdString(referencePath.id));
	// The envelope colours show the block type, so only the train list takes the service colours.
	win->setTrainListColors(trainColorsForRun(m_completedRunResults, m_sceneModel));
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
				QString::fromStdString(formatSimTime(static_cast<long long>(scope.startTime), m_startOffsetSeconds)),
				QString::fromStdString(formatSimTime(static_cast<long long>(scope.endTime), m_startOffsetSeconds)),
				completedRunContext(m_completedRunProvenance), QString::fromStdString(referencePath.id)),
		note);
	const std::function<std::string(const QStringList&)> scopedCsv =
		[segments, plannedReferences, trajectoryRows, extents, unmapped](const QStringList& visibleTrainIds) {
			return buildBlockingTimeCsv(visibleTrainIds, segments, plannedReferences, trajectoryRows, extents, unmapped);
		};
	win->setCsvProvider(scopedCsv, "blocking_time.csv", blockingTimeTrainIds(segments, plannedReferences, trajectoryRows, unmapped));
	attachRunProvenance(win, m_completedRunProvenance);
	connect(win, &DiagramWindow::trainSelected, this, &MainWindow::focusTrainInScene);
	win->setTimeAxisY(true, m_startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

void MainWindow::showCapacityAnalysis() {
	if (!hasRunResults()) {
		statusBar()->showMessage("Run a simulation to open capacity analysis", 5000);
		return;
	}

	CapacityAnalysisScope scope = firstCapacityScopeWithPair();
	if (scope.routeIndex < 0) {
		QMessageBox::information(this, "Capacity analysis",
			"No retained native blocking-time section has two occurrences with a common entry block. "
			"Run a scene with explicit signalling data; EGTRAIN does not infer a signalling system or blocking-time parameters.");
		return;
	}
	if ((!e2eDialogsSuppressed() || mainWindowTestHooks().askCapacityScope)
		&& !chooseCapacityAnalysisScope(this, scope))
		return;
	if (scope.routeIndex < 0 || scope.blockIds.empty()) {
		QMessageBox::information(this, "Capacity analysis",
			"No route section has two occurrences with a common entry block. Choose a common entry or split the analysis into pre-/post-Gdg sections.");
		return;
	}
	const std::string scopeIssue = capacityScopeIssue(scope);
	if (!scopeIssue.empty()) {
		QMessageBox::information(this, "Capacity scope unavailable", QString::fromStdString(scopeIssue));
		return;
	}
	const auto trains = capacityTrainsForScope(scope);
	if (trains.size() < 2) {
		QMessageBox::information(this, "Capacity analysis",
			"Fewer than two occurrences share the selected section entry. Choose a common entry or a narrower section.");
		return;
	}
	const QString sectionLabel = capacityScopeLabel(scope);
	const CapacityAnalysisResult result = analyzeCapacity(trains, scope.periodSeconds,
		scope.cycleEndOccurrenceId);
	if (!result.analyzable) {
		QMessageBox::information(this, "Capacity analysis",
			"The selected occurrences do not form a valid shared-resource chain. Choose a common entry or split pre-/post-Gdg order into separate sections.");
		return;
	}
	if (result.cycleEndIdentity.empty() || result.cycleTime < 0.0) {
		QMessageBox::information(this, "Capacity analysis",
			"The cycle-closing occurrence must be a selected row after the first occurrence.");
		return;
	}

	const RunProvenance provenance = m_completedRunProvenance;
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
			[result, sectionLabel] { return buildCapacityAnalysisCsv(result, sectionLabel); },
			[provenance](const QString& path, const std::string& bytes) {
				return writeRunArtifactWithProvenance(path.toStdString(), "csv", bytes, provenance);
			});
	});
	QPushButton* diagramButton = new QPushButton("Open compressed blocking-time diagram", body);
	const RouteDiagramPath compressedReference = routeDiagramPath(train_route[scope.routeIndex], &m_sceneModel);
	std::map<std::string, RouteDiagramProjection> compressedProjections;
	for (int i = 0; i < numRegions; ++i) {
		const Train& train = regional_train[i];
		if (train.indexOfRoute < 0 || train.indexOfRoute >= static_cast<int>(train_route.size())) continue;
		compressedProjections.emplace(train.trainDescription,
			buildRouteDiagramProjection(routeDiagramPath(train_route[train.indexOfRoute], &m_sceneModel), compressedReference));
	}
	int compressedOmitted = 0;
	auto shiftedSegments = buildBlockingTimeDiagramSegments(result.compressedOccupations, result.trainIdentities);
	restoreCompressedOriginalTimes(shiftedSegments, result.compression);
	std::vector<BlockingTimeDiagramSegment> compressedUnmapped;
	auto compressedSegments = projectBlockingSegments(shiftedSegments, compressedProjections, compressedOmitted, &compressedUnmapped);
	const auto allCompressedExtents = buildBlockSectionExtents(blockSectionInputsForRoute(train_route[scope.routeIndex]));
	const auto compressedExtents = scopedBlockSectionExtents(allCompressedExtents, scope.sectionIndices);
	matchBlockingSegmentsToSections(compressedSegments, allCompressedExtents, compressedReference.id);
	const auto axisRange = blockSectionAxisRange(compressedExtents, compressedReference);
	const QString referenceId = QString::fromStdString(compressedReference.id);
	connect(diagramButton, &QPushButton::clicked, dialog,
		[this, result, sectionLabel, provenance, compressedSegments, compressedUnmapped, compressedExtents, axisRange, referenceId]() {
			showCompressedBlockingTimeDiagram(result, sectionLabel, provenance, compressedSegments,
				axisRange.first, axisRange.second, referenceId, compressedExtents, compressedUnmapped);
		});
	secondaryActions->addWidget(exportButton);
	secondaryActions->addWidget(diagramButton);
	secondaryActions->addStretch();
	layout->addLayout(secondaryActions);
	auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
	connect(closeButtons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
	DialogLayout::install(*dialog,
		QString("Cycle: %1 s  |  %2% of period")
			.arg(QString::number(result.cycleTime, 'f', 1),
				QString::number(result.cyclePercentage, 'f', 1)),
		QString("Section: %1\nRun: %2").arg(sectionLabel, completedRunContext(provenance)),
		body, closeButtons);
	dialog->show();
}

void MainWindow::showCompressedBlockingTimeDiagram(const CapacityAnalysisResult& result,
	const QString& sectionLabel, RunProvenance provenance,
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
	window->setTrainListColors(trainColorsForRun(m_completedRunResults, m_sceneModel));
	const QString note = "Shifted calculated envelopes, not recorded movement. "
						 "Unmapped/incomplete blocks omitted; no extrapolation. "
						 "Envelope colours: blue blocks/stations, yellow switches, red conflicts, amber capacity-critical touching blocks. "
						 "Neutral lines/strip: physical sections."
		+ (segments.empty() ? QString(" No complete compressed envelope could be projected in this scope.") : QString());
	window->setPresentation("Compressed blocking-time diagram",
		QString("%1 | cycle %2 to %3 [%4] | Reference: %5")
			.arg(sectionLabel, QString::fromStdString(result.firstIdentity), QString::fromStdString(result.cycleEndIdentity),
				completedRunContext(provenance), referenceId),
		note);
	const std::function<std::string(const QStringList&)> csvProvider =
		[segments, extents, unmapped](const QStringList& visibleTrainIds) {
			return buildBlockingTimeCsv(visibleTrainIds, segments, {}, {}, extents, unmapped);
		};
	window->setCsvProvider(csvProvider, "capacity_compressed_blocking_time.csv", blockingTimeTrainIds(segments, {}, {}, unmapped));
	attachRunProvenance(window, std::move(provenance));
	connect(window, &DiagramWindow::trainSelected, this, &MainWindow::focusTrainInScene);
	window->setTimeAxisY(true, m_startOffsetSeconds);
	window->setAttribute(Qt::WA_DeleteOnClose);
	window->show();
}

void MainWindow::buildSignalIndex() {
	m_signalsByAheadId.clear();
	for (auto* signal : allSignals) {
		if (!signal->sectionAheadId.empty()) {
			m_signalsByAheadId[signal->sectionAheadId].push_back(signal);
		}
	}
}

void MainWindow::buildTrackIndexes() {
	m_tracksBySectionId.clear();
	m_tracksByOccupiedArc.clear();

	for (auto* item : allArcs) {
		if (!item || !item->arc)
			continue;
		m_tracksByOccupiedArc[{item->track, item->arc->startNode.X}] = item;
	}

	const auto sameArc = [](const Arc& left, const Arc& right) {
		return (left.ID == right.ID && left.startNode.X == right.startNode.X && left.endNode.X == right.endNode.X)
			|| (left.startNode.X == right.startNode.X && left.endNode.X == right.endNode.X
				&& left.length == right.length);
	};
	for (int sectionIndex = 0; sectionIndex < Blocks; ++sectionIndex) {
		const Section& section = signalling_block_sections[sectionIndex];
		if (section.ID.empty())
			continue;
		const int arcCount = std::min(section.total_arcs,
			static_cast<int>(sizeof(section.arcs_in_signalling_block_section)
				/ sizeof(section.arcs_in_signalling_block_section[0])));
		auto& tracks = m_tracksBySectionId[section.ID];
		for (int arcIndex = 0; arcIndex < arcCount; ++arcIndex) {
			const Arc& sectionArc = section.arcs_in_signalling_block_section[arcIndex];
			for (auto* item : allArcs) {
				if (!item || !item->arc || !sameArc(*item->arc, sectionArc))
					continue;
				const bool trackMatches = section.trackLineId < 0
					|| item->track == section.trackLineId
					|| item->track == section.FirstConnectedTrackLineID
					|| item->track == section.SecondConnectedTrackLineID;
				if (!trackMatches || std::find(tracks.begin(), tracks.end(), item) != tracks.end())
					continue;
				tracks.push_back(item);
			}
		}
		if (tracks.empty())
			m_tracksBySectionId.erase(section.ID);
	}
}

void MainWindow::updateNetworkLegend() {
	if (!m_networkLegendWidget)
		return;
	NetworkLegendContent content;
	// Runtime retains preview fit bounds, and a discarded run may still have
	// Failed status while the authoring preview is displayed.
	const bool preview = m_showingTrackPreview;
	content.hasTracks = numTrackLines > 0 || !m_sceneModel.tracks.empty();
	content.showOperationalTrackStates = !preview;
	content.hasSelectedTrack = preview && m_previewHasSelectedTrack;
	for (const TrainItemGroup* train : allTrains) {
		if (train)
			content.trains << NetworkLegendTrain{train->fillColor, train->outlineColor,
				QString::fromStdString(train->serviceId)};
	}
	for (const StationOverlayItem* station : m_stationOverlays) {
		if (station)
			content.stationVisuals << station->visual();
	}
	content.hasSignals = !preview && !allSignals.isEmpty();
	if (preview)
		content.hasSignals = m_previewHasSignals;
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
	assignments.reserve(m_stationOverlays.size());
	for (int index = 0; index < m_stationOverlays.size(); ++index)
		assignments.append(QList<StationNodeItem*>());
	for (auto* station : stationNodes) {
		const QString stationName = QString::fromStdString(station->node->stationName);
		const QPointF stationCenter = station->sceneBoundingRect().center();
		int nearestIndex = -1;
		qreal nearestDistance = std::numeric_limits<qreal>::max();
		for (int index = 0; index < m_stationOverlays.size(); ++index) {
			const auto* overlay = m_stationOverlays.at(index);
			if (!overlay || overlay->stationName() != stationName)
				continue;
			const QPointF delta = stationCenter - overlay->stableAnchor();
			const qreal distance = delta.x() * delta.x() + delta.y() * delta.y();
			bool nearer = nearestIndex < 0 || distance < nearestDistance;
			if (!nearer && distance == nearestDistance) {
				const QPointF currentAnchor = m_stationOverlays.at(nearestIndex)->stableAnchor();
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

	for (int index = 0; index < m_stationOverlays.size(); ++index) {
		auto* overlay = m_stationOverlays.at(index);
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
		if (QGraphicsPixmapItem* picture = m_stationPictures.value(overlay, nullptr)) {
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

	for (auto* overlay : m_stationOverlays) {
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

bool MainWindow::paxTextVisible() const {
	if (!m_passengerLayerVisible || !networkView)
		return false;
	return networkView->zoomRatio() >= kDenseDetailZoom;
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
	for (auto* overlay : m_stationOverlays) {
		if (!overlay)
			continue;
		const bool selectedByIdentity = m_hasSelectedStationIdentity && overlay->hasSourceIdentity()
			&& overlay->matchesSourceIdentity(m_selectedStationNodeId, m_selectedStationTrack);
		const bool selectedByLegacyName = !m_hasSelectedStationIdentity && !overlay->hasSourceIdentity()
			&& !m_selectedStationName.isEmpty() && overlay->stationName() == m_selectedStationName;
		overlay->setSelected(m_showingTrackPreview
				? !m_previewSelectedStationId.isEmpty() && overlay->data(PreviewGraphics::Id).toString() == m_previewSelectedStationId
				: selectedByIdentity || selectedByLegacyName);
		overlay->setVisible(m_stationLayerVisible);
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
	if (effect)
		effect->setViewScale(viewScale);
	for (auto* overlay : m_stationOverlays)
		if (overlay)
			overlay->applyViewScale(viewScale);
	StationOverlayItem::resolveNameCollisions(m_stationOverlays, networkView->viewportTransform());
	for (auto* overlay : m_stationOverlays)
		if (overlay && overlay->nameItem())
			overlay->nameItem()->setVisible(m_stationLayerVisible && m_stationNamesVisible
				&& !overlay->isNameHiddenByCollision());
	updateSignalCues();
	const bool paxText = paxTextVisible();
	for (auto* platform : allPlatforms)
		if (platform && platform->textIcon)
			platform->textIcon->setVisible(paxText);
	for (auto it = m_trainBadges.cbegin(); it != m_trainBadges.cend(); ++it) {
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
	for (QGraphicsItem* item : m_signalDecorations) {
		if (!item)
			continue;
		const bool visible = m_signalLayerVisible && item->data(kSignalBaseVisibleRole).toBool();
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
		&& m_inspectedSignal && m_signalDecorations.contains(m_inspectedSignal)) {
		const QString details = m_inspectedSignal->toolTip();
		if (signallingGroupDetails->toPlainText() != details) {
			const int scroll = signallingGroupDetails->verticalScrollBar()->value();
			signallingGroupDetails->setPlainText(details);
			signallingGroupDetails->verticalScrollBar()->setValue(scroll);
		}
		signallingAspectText->setText(signalAspectLabel(m_inspectedSignal));
	}
}

void MainWindow::updateZoomStatus() {
	if (m_zoomStatusLabel && networkView)
		m_zoomStatusLabel->setText(networkView->zoomLabel());
}
