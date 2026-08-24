#include "app/MainWindow.h"
#include "ui_MainWindow.h"
#include <QTableWidget>
#include <QHeaderView>
#include "util/TimeFormat.h"
#include "util/SpeedFormat.h"
#include "widgets/ConsoleWidget.h"
#include "diagrams/DiagramWindow.h"
#include "diagrams/RunResults.h"
#include "diagrams/TimetableTableWindow.h"
#include "util/TrajectoryUtil.h"
#include "util/CsvWriter.h"
#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/CapacityAnalysis.h"
#include "diagrams/TractionCurve.h"
#include "graphics/VisualPolish.h"
#include "scene/SceneBundle.h"
#include "scene/SceneCompatibility.h"
#include "scene/SceneMigration.h"
#include "scene/SceneWriter.h"
#include "scene/SceneExporter.h"
#include "scene/SceneImporter.h"
#include "scene/SectionInventory.h"
#include "scene/TrackPreview.h"
#include "simulation/Passengers.h"
#include "update/ReleaseInfo.h"
#include "update/UpdateChecker.h"
#include "update/UpdateSettings.h"
#include "update/SelfUpdater.h"
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
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
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QRadioButton>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QProgressDialog>
#include <QInputDialog>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QSettings>
#include <QIcon>
#include <QDesktopServices>
#include <QToolButton>
#include <QTreeWidgetItemIterator>
#include <QStringList>
#include <QTabWidget>
#include <QRegularExpression>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <functional>
#include <memory>

// utilization of GUI
// bool GUI = true; // GUI used by default
extern InitialParameters initial_variables;

namespace {
const char* kRecentScenesKey = "recentScenes";
const int kMaxRecentScenes = 8;
constexpr qreal kDenseDetailZoom = 3.0;
constexpr qreal kSignalDetailZoom = 8.0;
constexpr int kOverlayMargin = 12;
constexpr int kSignalDecorationRole = 0;
constexpr int kSignalTrackRole = 1;
constexpr int kSignalBaseVisibleRole = 2;
constexpr int kSignalAnchorRole = 3;
constexpr int kSignalNormalRole = 4;
constexpr int kSignalDirectionRole = 5;
constexpr qreal kPreviewSignalOffsetPixels = 6.0;
constexpr int kLoadedDataTargetTypeRole = Qt::UserRole;
constexpr const char kPlatformGeometryEditedProperty[] = "platformGeometryEdited";

// The speed slider reads left-to-right as slow-to-fast; the worker wants a
// per-step delay, so the delay is the distance from the fast end.
constexpr int kMaxStepDelayMs = 500;
int stepDelayForSlider(int sliderValue) {
	return kMaxStepDelayMs - sliderValue;
}

// Keep enough significant digits for a displayed value to round-trip to the
// same double when pending editor fields are committed.
class CompactDoubleSpinBox : public QDoubleSpinBox {
public:
	using QDoubleSpinBox::QDoubleSpinBox;
	QString textFromValue(double value) const override {
		return QString::number(value, 'g', std::numeric_limits<double>::max_digits10);
	}
};

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

void populatePassengerStationCombo(QComboBox* combo, const SceneModel& sceneModel,
		const std::string& current, const std::set<std::string>* allowed = nullptr) {
	if (!combo)
		return;
	combo->clear();
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
		const std::string& current) {
	if (!combo)
		return;
	combo->clear();
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

std::vector<const Train*> runResultTrainPointers() {
	std::vector<const Train*> trains;
	trains.reserve(static_cast<std::size_t>(std::max(0, numRegions)));
	for (int i = 0; i < numRegions; ++i)
		trains.push_back(&regional_train[i]);
	return trains;
}

// A train exports when it is in the visible set. An empty set means the user has
// hidden every train, so nothing is exported.
bool trainInVisibleSet(const QStringList& visibleTrainIds, const std::string& trainId) {
	return visibleTrainIds.contains(QString::fromStdString(trainId));
}

std::string csvValue(const RunResultValue& value) {
	return value.available ? csv::formatDouble(value.value) : std::string(csv::kMissingValue);
}

// Per-train trajectory: identity, time, position, speed, power, wheel effort,
// cumulative energy, block. Reads raw simulation samples over the valid
// trajectory ranges so gaps never produce a false row.
std::string buildTrajectoryCsv(const QStringList& visibleTrainIds) {
	std::vector<std::vector<std::string>> rows;
	for (int tr = 0; tr < numRegions; ++tr) {
		const Train& train = regional_train[tr];
		if (!trainInVisibleSet(visibleTrainIds, train.trainDescription))
			continue;
		if (train.earliestActiveTrajectoryIndex < 0)
			continue;
		const auto sampleAt = [](const std::vector<double>& series, int i) {
			return i >= 0 && i < static_cast<int>(series.size())
				? csv::formatDouble(series[static_cast<std::size_t>(i)])
				: std::string(csv::kMissingValue);
		};
		for (const auto& segment : validTrajectorySegments(train.instant_spatial_position,
														   train.earliestActiveTrajectoryIndex,
														   train.End_Time)) {
			for (int i = segment.first; i <= segment.last; ++i) {
				std::string block = i >= 0 && i < static_cast<int>(train.instant_block_section_occupied.size())
					? train.instant_block_section_occupied[static_cast<std::size_t>(i)]
					: std::string(csv::kMissingValue);
				std::vector<std::string> row;
				row.push_back(train.trainDescription);
				row.push_back(train.operatingCode);
				row.push_back(train.serviceId);
				row.push_back(std::to_string(train.serviceOccurrence));
				row.push_back(csv::formatDouble(i * timestep));
				row.push_back(sampleAt(train.instant_spatial_position, i));
				row.push_back(sampleAt(train.instant_train_speed, i));
				std::string power = i >= 0 && i < static_cast<int>(train.instant_train_power_consumption.size())
					? csv::formatDouble(train.instant_train_power_consumption[static_cast<std::size_t>(i)] / 1000.0)
					: std::string(csv::kMissingValue);
				std::string energy = i >= 0 && i < static_cast<int>(train.instant_train_energy_consumption.size())
					? csv::formatDouble(train.instant_train_energy_consumption[static_cast<std::size_t>(i)] * kEnergyMJToKWh)
					: std::string(csv::kMissingValue);
				std::string effort = i >= 0 && i < static_cast<int>(train.instant_train_tractive_effort.size())
					? csv::formatDouble(train.instant_train_tractive_effort[static_cast<std::size_t>(i)] / 1000.0)
					: std::string(csv::kMissingValue);
				row.push_back(power);
				row.push_back(effort);
				row.push_back(energy);
				row.push_back(block);
				rows.push_back(std::move(row));
			}
		}
	}
	if (rows.empty())
		return std::string();
	return csv::makeDocument(
		{"Train", "Operating code", "Service ID", "Occurrence", "Time[s]", "Position[m]", "Speed[m/s]", "Power[kW]", "Tractive effort[kN]", "Energy[kWh]", "Block"}, rows);
}

// Timetable: train, station, planned and simulated arrival and departure, delay.
std::string buildTimetableCsv(const QStringList& visibleTrainIds) {
	const auto results = buildTimetableResults(runResultTrainPointers());
	std::vector<std::vector<std::string>> rows;
	for (const TimetableResultRow& r : results) {
		if (!trainInVisibleSet(visibleTrainIds, r.trainId))
			continue;
		rows.push_back({
			r.trainId,
			r.stationId,
			std::to_string(r.journeyIndex),
			r.operatingCode,
			csvValue(r.plannedArrivalSeconds),
			csvValue(r.plannedDepartureSeconds),
			csvValue(r.simulatedArrivalSeconds),
			csvValue(r.simulatedDepartureSeconds),
			csvValue(r.arrivalDelaySeconds),
			csvValue(r.departureDelaySeconds)});
	}
	if (rows.empty())
		return std::string();
	return csv::makeDocument(
		{"Train", "Station", "Journey order", "Operating code", "Planned arrival[s]", "Planned departure[s]",
			"Simulated arrival[s]", "Simulated departure[s]", "Arrival delay[s]", "Departure delay[s]"},
		rows);
}

const char* blockingSegmentTypeName(BlockingTimeSegmentStyle style) {
	switch (style) {
		case BlockingTimeSegmentStyle::Station:
			return "station";
		case BlockingTimeSegmentStyle::Switch:
			return "switch";
		case BlockingTimeSegmentStyle::SwitchStation:
			return "switch/station";
		case BlockingTimeSegmentStyle::Critical:
			return "conflict";
		case BlockingTimeSegmentStyle::CriticalStation:
			return "conflict/station";
		case BlockingTimeSegmentStyle::Default:
		default:
			return "block";
	}
}

const char* blockingSegmentTypeName(const BlockingTimeDiagramSegment& segment) {
	return segment.capacityCritical ? "capacity critical block" : blockingSegmentTypeName(segment.style);
}

const QColor kBlockingSwitchColor(240, 210, 40, 180);
const QColor kBlockingCriticalColor(220, 50, 50, 200);
const QColor kBlockingCapacityColor(235, 175, 20, 230);

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
			case BlockingTimeSegmentStyle::Default: default: return QColor(100, 160, 240, 150);
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
		auto* series = new QLineSeries();
		const std::string legendKey = segment.trainName + suffixFor(segment);
		series->setName(QString::fromStdString(legendKey));
		series->setProperty("trainId", QString::fromStdString(segment.trainName));
		const bool firstLegendEntry = legendEntries.emplace(legendKey, true).second;
		QPen pen(colorFor(segment));
		pen.setWidthF(segment.penWidth);
		series->setPen(pen);
		series->append(segment.startTime, segment.midPositionKm);
		series->append(segment.endTime, segment.midPositionKm);
		chart->addSeries(series);
		if (!firstLegendEntry)
			for (QLegendMarker* marker : chart->legend()->markers(series))
				marker->setVisible(false);
	}
}

struct BlockingTimeScope {
	std::vector<std::string> trainIds;
	std::vector<std::string> blockIds;
	double startTime = 0.0;
	double endTime = 0.0;
	int routeIndex = -1;
};

BlockingTimeScope defaultBlockingTimeScope() {
	BlockingTimeScope scope;
	scope.endTime = std::max(0.0, initial_variables.times * timestep);
	return scope;
}

std::vector<BlockingTimeDiagramSegment> buildAllBlockingTimeSegments() {
	std::vector<std::vector<BlockingTimeDiagramInput>> trains;
	std::vector<std::string> trainNames;
	trains.reserve(static_cast<std::size_t>(std::max(0, numRegions)));
	trainNames.reserve(static_cast<std::size_t>(std::max(0, numRegions)));
	for (int i = 0; i < numRegions; ++i) {
		const Train& t = regional_train[i];
		std::vector<BlockingTimeDiagramInput> blocks;
		blocks.reserve(static_cast<std::size_t>(std::max(0, t.N_BlockTimeComplete)));
		for (int j = 0; j < t.N_BlockTimeComplete; ++j) {
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
	return buildBlockingTimeDiagramSegments(trains, trainNames);
}

std::vector<BlockingTimePlannedReference> buildBlockingTimePlannedReferences(
	const BlockingTimeScope& scope) {
	std::vector<BlockingTimePlannedReference> references;
	if (scope.routeIndex >= 0 && scope.trainIds.empty())
		return references;
	for (int i = 0; i < numRegions; ++i) {
		const Train& train = regional_train[i];
		if (!scope.trainIds.empty() &&
			std::find(scope.trainIds.begin(), scope.trainIds.end(), train.trainDescription) == scope.trainIds.end())
			continue;
		if (!train.Stations)
			continue;
		const int stationCount = std::min(train.numStations, static_cast<int>(Train::kMaxTimetableStations));
		for (int stationIndex = 0; stationIndex < stationCount; ++stationIndex) {
			if (!train.stationIsOnRoute(stationIndex, scope.blockIds))
				continue;
			const double positionMeters = train.stationRoutePositionMeters(stationIndex);
			if (!std::isfinite(positionMeters) || positionMeters < 0.0)
				continue;
			const double positionKm = positionMeters / 1000.0;
			const auto append = [&](const char* eventType, double time) {
				if (!std::isfinite(time) || time < 0.0)
					return;
				references.push_back({train.trainDescription, train.stationNameForArrivalStats(stationIndex),
					eventType, time, positionKm});
			};
			append("arrival", train.ScheduledArrivals[stationIndex]);
			append("departure", train.ScheduledDepartures[stationIndex]);
		}
	}
	return references;
}

// Blocking-time: keep the occupation-column prefix and append planned
// reference rows so the visible dashed layer is exportable too.
std::string buildBlockingTimeCsv(const QStringList& visibleTrainIds,
	const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::vector<BlockingTimePlannedReference>& plannedReferences) {
	std::vector<std::vector<std::string>> rows;
	for (const BlockingTimeDiagramSegment& s : segments) {
		if (!trainInVisibleSet(visibleTrainIds, s.trainName))
			continue;
		rows.push_back({
			s.trainName,
			s.blockId,
			csv::formatDouble(s.startTime),
			csv::formatDouble(s.endTime),
			csv::formatDouble(s.midPositionKm),
			blockingSegmentTypeName(s),
			std::string(),
			std::string(),
			std::string()});
	}
	for (const BlockingTimePlannedReference& reference : plannedReferences) {
		if (!trainInVisibleSet(visibleTrainIds, reference.trainName))
			continue;
		rows.push_back({
			reference.trainName,
			std::string(),
			std::string(),
			std::string(),
			csv::formatDouble(reference.positionKm),
			"planned reference",
			reference.eventType,
			reference.stationName,
			csv::formatDouble(reference.time)});
	}
	if (rows.empty())
		return std::string();
	return csv::makeDocument(
		{"Train", "Block", "Occupation start[s]", "Occupation end[s]", "Position[km]", "Segment type",
			"Planned reference", "Station", "Planned time[s]"},
		rows);
}

std::string buildBlockingTimeCsv(const QStringList& visibleTrainIds) {
	const BlockingTimeScope scope = defaultBlockingTimeScope();
	return buildBlockingTimeCsv(visibleTrainIds,
		filterBlockingTimeDiagramSegments(buildAllBlockingTimeSegments(), scope.trainIds, scope.blockIds,
			scope.startTime, scope.endTime),
		filterBlockingTimePlannedReferences(buildBlockingTimePlannedReferences(scope),
			scope.startTime, scope.endTime));
}

bool chooseBlockingTimeScope(QWidget* parent, BlockingTimeScope& scope) {
	QDialog dialog(parent);
	dialog.setWindowTitle("Blocking-time scope");
	auto* form = new QFormLayout(&dialog);
	auto* routeCombo = new QComboBox(&dialog);
	routeCombo->addItem("All routes / all corridors", -1);
	for (std::size_t routeIndex = 0; routeIndex < train_route.size(); ++routeIndex) {
		const Route& route = train_route[routeIndex];
		const QString corridor = route.corridor.empty()
			? QStringLiteral("no corridor") : QString::fromStdString(route.corridor);
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
			firstBlock->addItem(blockId);
			lastBlock->addItem(blockId);
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
	form->addRow(buttons);
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
	const int routeIndex = routeCombo->currentData().toInt();
	scope.routeIndex = routeIndex >= 0 && routeIndex < static_cast<int>(train_route.size())
		? routeIndex : -1;
	if (scope.routeIndex < 0)
		return true;

	const Route& route = train_route[static_cast<std::size_t>(scope.routeIndex)];
	const int first = std::min(firstBlock->currentIndex(), lastBlock->currentIndex());
	const int last = std::max(firstBlock->currentIndex(), lastBlock->currentIndex());
	for (int blockIndex = first; blockIndex >= 0 && blockIndex <= last && blockIndex < route.N_Block_Sections; ++blockIndex)
		scope.blockIds.push_back(route.sequence_of_block_sections[blockIndex].ID);
	for (int trainIndex = 0; trainIndex < numRegions; ++trainIndex) {
		const Train& train = regional_train[trainIndex];
		if (train.indexOfRoute == scope.routeIndex || train.TrainRouteID == route.ID)
			scope.trainIds.push_back(train.trainDescription);
	}
	return true;
}

std::string joinIncidentIds(const std::vector<std::string>& ids) {
	std::string result;
	for (const std::string& id : ids) {
		if (!result.empty())
			result += ";";
		result += id;
	}
	return result;
}

std::string terminationOutcome(bool requested, bool terminated) {
	if (!requested)
		return "not requested";
	return terminated ? "terminated at destination" : "not terminated at destination";
}

// Run summary: per-train start, end, travel time, and energy totals, plus a
// network totals row.
std::string buildRunSummaryCsv(const RunResults& results) {
	std::vector<std::vector<std::string>> rows;
	for (const TrainRunResult& t : results.trains) {
		rows.push_back({
			t.trainId,
			t.operatingCode,
			csv::formatDouble(t.performancePercent),
			csv::formatDouble(t.appliedMaximumSpeedKmh),
			csvValue(t.startSeconds),
			csvValue(t.endSeconds),
			csvValue(t.travelSeconds),
			csvValue(t.energyConsumedKWh),
			csvValue(t.energyWithRegenKWh),
			csvValue(t.substationKWh),
			csvValue(t.substationWithRegenKWh),
			joinIncidentIds(t.directIncidentIds),
			csvValue(t.firstDirectIncidentTime),
			csvValue(t.firstDirectIncidentLocation),
			terminationOutcome(t.destinationTerminationRequested, t.destinationTerminated)});
	}
	rows.push_back({
		"Network total",
		std::string(csv::kMissingValue),
		std::string(csv::kMissingValue),
		std::string(csv::kMissingValue),
		csvValue(results.networkStartSeconds),
		csvValue(results.networkEndSeconds),
		csvValue(results.networkTravelSeconds),
		csvValue(results.energyConsumedKWh),
		csvValue(results.energyWithRegenKWh),
		csvValue(results.substationKWh),
		csvValue(results.substationWithRegenKWh),
		std::string(csv::kMissingValue),
		std::string(csv::kMissingValue),
		std::string(csv::kMissingValue),
		std::string(csv::kMissingValue)});
	if (results.trains.empty())
		return std::string();
	return csv::makeDocument(
		{"Train", "Operating code", "Performance [%]", "Applied maximum speed [km/h]", "Start[s]", "End[s]", "Travel time[s]", "Energy consumed[kWh]",
			"Energy with regen[kWh]", "Substation[kWh]", "Substation with regen[kWh]", "Incident IDs",
			"First direct time[s]", "First direct location[m]", "Termination outcome"},
		rows);
}
std::string delayComparisonCsv(const DelayRunSnapshot& baseline,
		const DelayRunSnapshot& scenario, const DelayComparisonResult& comparison) {
	std::vector<std::vector<std::string>> rows;
	for (const DelayComparisonRow& row : comparison.rows) {
		rows.push_back({
			baseline.scenarioId,
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

struct CapacityAnalysisScope {
	int routeIndex = -1;
	std::vector<std::string> blockIds;
	std::vector<std::string> occurrenceIds;
	std::string cycleEndOccurrenceId;
	double periodSeconds = 3600.0;
};

BlockingTimeDiagramInput capacityOccupation(const BlockingTimes& source) {
	BlockingTimeDiagramInput occupation;
	occupation.blockId = source.BlockID;
	occupation.startOccTime = source.StartOccTime;
	occupation.endOccTime = source.EndOccTime;
	occupation.posStart = source.PosStart;
	occupation.posEnd = source.PosEnd;
	occupation.switchName = source.SwitchName;
	occupation.stationName = source.stationName;
	occupation.isComplete = source.IsComplete;
	return occupation;
}

CapacityAnalysisTrain capacityTrainForScope(const Train& train, const CapacityAnalysisScope& scope) {
	CapacityAnalysisTrain result;
	result.runtimeId = train.trainDescription;
	result.operatingCode = train.operatingCode;
	if (scope.blockIds.empty())
		return result;

	const BlockingTimes* reference = nullptr;
	for (int blockIndex = 0; blockIndex < train.N_BlockTimeComplete; ++blockIndex) {
		const BlockingTimes& source = train.BlockTime[blockIndex];
		const BlockingTimeDiagramInput occupation = capacityOccupation(source);
		if (!validBlockingTimeDiagramInput(occupation))
			continue;
		if (!reference && shareBlockingTimeResource(occupation.blockId, scope.blockIds.front()))
			reference = &source;
		for (const std::string& selectedBlock : scope.blockIds) {
			if (shareBlockingTimeResource(occupation.blockId, selectedBlock)) {
				result.occupations.push_back(occupation);
				break;
			}
		}
	}
	if (!reference || !std::isfinite(reference->StartRunTime) || reference->StartRunTime < 0.0) {
		result.occupations.clear();
		return result;
	}
	result.profileReferenceTime = reference->StartRunTime;
	result.referenceLabel = "block " + scope.blockIds.front() + " entry";
	result.referenceSource = "selected section StartRunTime";

	// A boundary stop is the authored reference when its route position matches
	// the selected section entry. The 5 m tolerance is the native route tolerance.
	if (train.Stations && reference->PosStart >= 0.0) {
		const int stationCount = std::min(train.numStations, static_cast<int>(Train::kMaxTimetableStations));
		for (int stationIndex = 0; stationIndex < stationCount; ++stationIndex) {
			const double stationPosition = train.stationRoutePositionMeters(stationIndex);
			if (!std::isfinite(stationPosition) || std::abs(stationPosition - reference->PosStart) > 5.0)
				continue;
			const double departure = train.ScheduledDepartures[stationIndex];
			const double arrival = train.ScheduledArrivals[stationIndex];
			const double authored = departure >= 0.0 ? departure : arrival;
			if (!std::isfinite(authored) || authored < 0.0)
				continue;
			const std::string station = train.stationNameForArrivalStats(stationIndex);
			result.scheduledReferenceTime = authored;
			result.referenceLabel = station + " boundary";
			result.referenceSource = departure >= 0.0
				? "authored ScheduledDeparture at boundary station"
				: "authored ScheduledArrival at boundary station (departure absent)";
			return result;
		}
	}

	if (std::isfinite(train.scheduled_departure_time) && train.scheduled_departure_time >= 0.0
		&& train.RunStartTime >= 0) {
		result.scheduledReferenceTime = train.scheduled_departure_time
			+ (reference->StartRunTime - static_cast<double>(train.RunStartTime));
		result.referenceSource = "canonical scheduled entry + elapsed from RunStartTime";
	} else {
		result.scheduledReferenceTime = result.profileReferenceTime;
		result.referenceSource = "profile reference fallback (scheduled entry unavailable)";
	}
	return result;
}

std::vector<CapacityAnalysisTrain> capacityTrainsForScope(const CapacityAnalysisScope& scope) {
	std::vector<CapacityAnalysisTrain> candidates;
	for (int trainIndex = 0; trainIndex < numRegions; ++trainIndex) {
		const CapacityAnalysisTrain candidate = capacityTrainForScope(regional_train[trainIndex], scope);
		if (candidate.occupations.empty())
			continue;
		if (!scope.occurrenceIds.empty() && std::find(scope.occurrenceIds.begin(), scope.occurrenceIds.end(),
			candidate.runtimeId) == scope.occurrenceIds.end())
			continue;
		candidates.push_back(candidate);
	}
	if (scope.occurrenceIds.empty())
		return candidates;
	std::vector<CapacityAnalysisTrain> trains;
	trains.reserve(scope.occurrenceIds.size());
	for (const std::string& occurrenceId : scope.occurrenceIds) {
		const auto it = std::find_if(candidates.begin(), candidates.end(),
			[&occurrenceId](const CapacityAnalysisTrain& candidate) {
				return candidate.runtimeId == occurrenceId;
			});
		if (it != candidates.end())
			trains.push_back(*it);
	}
	return trains;
}

CapacityAnalysisScope firstCapacityScopeWithPair() {
	CapacityAnalysisScope empty;
	for (std::size_t routeIndex = 0; routeIndex < train_route.size(); ++routeIndex) {
		const Route& route = train_route[routeIndex];
		for (int blockIndex = 0; blockIndex < route.N_Block_Sections; ++blockIndex) {
			CapacityAnalysisScope candidate;
			candidate.routeIndex = static_cast<int>(routeIndex);
			candidate.blockIds.push_back(route.sequence_of_block_sections[blockIndex].ID);
			const auto trains = capacityTrainsForScope(candidate);
			if (trains.size() < 2)
				continue;
			for (const auto& train : trains)
				candidate.occurrenceIds.push_back(train.runtimeId);
			return candidate;
		}
	}
	return empty;
}

QString capacityScopeLabel(const CapacityAnalysisScope& scope) {
	if (scope.routeIndex < 0 || scope.routeIndex >= static_cast<int>(train_route.size()))
		return QStringLiteral("no selected route");
	const Route& route = train_route[static_cast<std::size_t>(scope.routeIndex)];
	QString label = QString::fromStdString(route.ID);
	if (!route.corridor.empty())
		label += QString(" / %1").arg(QString::fromStdString(route.corridor));
	if (!scope.blockIds.empty())
		label += QString(" / %1 to %2").arg(QString::fromStdString(scope.blockIds.front()),
			QString::fromStdString(scope.blockIds.back()));
	return label;
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
	auto* layout = new QVBoxLayout(&dialog);
	auto* form = new QFormLayout();
	auto* routeCombo = new QComboBox(&dialog);
	for (std::size_t routeIndex = 0; routeIndex < train_route.size(); ++routeIndex) {
		const Route& route = train_route[routeIndex];
		const QString corridor = route.corridor.empty()
			? QStringLiteral("no corridor") : QString::fromStdString(route.corridor);
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
		"period that closes the capacity cycle; EGTRAIN does not infer it from the last row.", &dialog);
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
			firstBlock->addItem(blockId);
			lastBlock->addItem(blockId);
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
		const int first = std::min(firstBlock->currentIndex(), lastBlock->currentIndex());
		const int last = std::max(firstBlock->currentIndex(), lastBlock->currentIndex());
		for (int blockIndex = first; blockIndex <= last && blockIndex < route.N_Block_Sections; ++blockIndex)
			if (blockIndex >= 0)
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
	layout->addWidget(buttons);
	return dialog.exec() == QDialog::Accepted;
}

// Ids of every loaded train, used when a whole result table is exported.
QStringList allTrainIds() {
	QStringList ids;
	for (int i = 0; i < numRegions; ++i)
		ids.append(QString::fromStdString(regional_train[i].trainDescription));
	return ids;
}

// Result windows are non-modal and may outlive the run that created them.
// Capture one document per train now so later filtering never reads a newer run.
std::function<std::string(const QStringList&)> snapshotCsv(
		const std::function<std::string(const QStringList&)>& provider) {
	std::vector<std::pair<QString, std::string>> documents;
	for (const QString& trainId : allTrainIds()) {
		const std::string document = provider(QStringList{trainId});
		if (!document.empty())
			documents.push_back({trainId, document});
	}
	return [documents = std::move(documents)](const QStringList& visibleTrainIds) {
		std::string combined;
		for (const auto& entry : documents) {
			if (!visibleTrainIds.contains(entry.first))
				continue;
			if (combined.empty()) {
				combined = entry.second;
				continue;
			}
			const std::size_t firstRow = entry.second.find('\n');
			if (firstRow != std::string::npos)
				combined.append(entry.second, firstRow + 1, std::string::npos);
		}
		return combined;
	};
}

// Write CSV text atomically to a fixed path. Used by the headless export check.
bool writeCsvFile(const QString& path, const std::string& content) {
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
		return false;
	const QByteArray bytes = QByteArray::fromStdString(content);
	return file.write(bytes) == bytes.size() && file.commit();
}

bool writeCsvFileWithProvenance(const QString& path, const std::string& content,
		const RunProvenance& provenance) {
	return writeRunArtifactWithProvenance(path.toStdString(), "csv", content, provenance);
}

// Prompt for a path and write CSV text atomically. A cancelled dialog or a write
// failure never leaves a partial file behind.
void saveCsvInteractive(QWidget* parent, const QString& suggestedName, const std::string& content,
		const std::function<bool(const QString&, const std::string&)>& artifactWriter) {
	if (content.empty()) {
		QMessageBox::information(parent, "Nothing to export", "There is no data to export.");
		return;
	}
	QString path = QFileDialog::getSaveFileName(parent, "Export Data", suggestedName, "CSV File (*.csv)");
	if (path.isEmpty())
		return;
	if (QFileInfo(path).suffix().compare("csv", Qt::CaseInsensitive) != 0)
		path += ".csv";
	if (!artifactWriter(path, content)) {
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
					? QStringLiteral("Activate this row to plot this train unit's tractive effort.")
					: QStringLiteral("Activate this row to open the existing editor.")));
		row->setToolTip(0, tooltip);
	}
	for (const auto& child : item.children)
		addLoadedDataTreeItem(tree, row, child);
}

bool e2eDialogsSuppressed() {
	return qEnvironmentVariableIsSet("QEGTRAIN_AUTOSTART")
		|| qEnvironmentVariableIsSet("QEGTRAIN_E2E_VISUAL_POLISH")
		|| qEnvironmentVariableIsSet("QEGTRAIN_E2E_STATION_OVERLAYS")
		|| qEnvironmentVariableIsSet("QEGTRAIN_E2E_SCENE_RUN")
		|| qEnvironmentVariableIsSet("QEGTRAIN_E2E_EDITOR_SMOKE")
		|| qEnvironmentVariableIsSet("QEGTRAIN_E2E_CREATOR_ACCEPTANCE")
		|| qEnvironmentVariableIsSet("QEGTRAIN_E2E_TRACK_PREVIEW")
		|| qEnvironmentVariableIsSet("QEGTRAIN_E2E_LEGACY_IMPORT")
		|| qEnvironmentVariableIsSet("QEGTRAIN_E2E_EXPORT_DIR");
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

bool previewPointAtNode(const TrackPreviewLine& line, const std::string& nodeId, qreal offset, QPointF& point) {
	TrackPreviewPoint candidate;
	if (!trackPreviewPointAtNode(line, nodeId, candidate))
		return false;
	point = QPointF(candidate.x, candidate.y + offset);
	return true;
}

bool previewPointAtX(const TrackPreviewLine& line, double x, qreal offset, QPointF& point) {
	TrackPreviewPoint candidate;
	if (!trackPreviewPointAtX(line, x, candidate))
		return false;
	point = QPointF(candidate.x, candidate.y + offset);
	return true;
}

bool previewSignalNormal(const TrackPreviewLine& line, double rawX, QPointF& normal) {
	for (std::size_t index = 1; index < line.points.size(); ++index) {
		const auto& first = line.points[index - 1];
		const auto& second = line.points[index];
		if (rawX < std::min(first.rawX, second.rawX)
				|| rawX > std::max(first.rawX, second.rawX))
			continue;
		const QPointF tangent(second.x - first.x, second.y - first.y);
		const qreal length = std::hypot(tangent.x(), tangent.y());
		if (length <= 0.0)
			continue;
		normal = QPointF(-tangent.y() / length, tangent.x() / length);
		return true;
	}
	return false;
}

std::string rewriteBlockReference(const std::string& reference,
		const std::string& oldId, const std::string& newId) {
	if (reference == oldId)
		return newId;

	std::string rewritten;
	std::size_t begin = 0;
	while (begin <= reference.size()) {
		const std::size_t slash = reference.find('/', begin);
		std::string part = reference.substr(begin,
				slash == std::string::npos ? std::string::npos : slash - begin);
		std::size_t idBegin = 0;
		std::size_t idEnd = part.find('@');
		if (!part.empty() && part.front() == '@') {
			idBegin = 1;
			idEnd = part.find('@', 1);
		}
		if (idEnd == std::string::npos)
			idEnd = part.size();
		if (part.substr(idBegin, idEnd - idBegin) == oldId)
			part.replace(idBegin, idEnd - idBegin, newId);
		if (!rewritten.empty())
			rewritten += '/';
		rewritten += part;
		if (slash == std::string::npos)
			break;
		begin = slash + 1;
	}
	return rewritten;
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

std::string platformSelectionKey(const std::string& stationId, const std::string& platformId) {
	return stationId + "\n" + platformId;
}

std::vector<std::string> previewRouteComponents(const std::string& token) {
	std::vector<std::string> components;
	std::size_t begin = 0;
	while (begin <= token.size()) {
		const std::size_t slash = token.find('/', begin);
		std::string part = token.substr(begin,
										slash == std::string::npos ? std::string::npos : slash - begin);
		std::size_t idBegin = 0;
		std::size_t idEnd = part.find('@');
		if (!part.empty() && part.front() == '@') {
			idBegin = 1;
			idEnd = part.find('@', 1);
		}
		if (idEnd == std::string::npos)
			idEnd = part.size();
		if (idEnd > idBegin)
			components.push_back(part.substr(idBegin, idEnd - idBegin));
		if (slash == std::string::npos)
			break;
		begin = slash + 1;
	}
	return components;
}

const std::vector<SceneSignal>& sceneSignals(const SceneModel& sceneModel) {
#ifdef signals
#define EGTRAIN_RESTORE_SIGNALS_HELPER
#undef signals
#endif
	const auto& result = sceneModel.signals;
#ifdef EGTRAIN_RESTORE_SIGNALS_HELPER
#define signals Q_SIGNALS
#undef EGTRAIN_RESTORE_SIGNALS_HELPER
#endif
	return result;
}

std::vector<SceneSignal>& sceneSignals(SceneModel& sceneModel) {
#ifdef signals
#define EGTRAIN_RESTORE_SIGNALS_HELPER
#undef signals
#endif
	auto& result = sceneModel.signals;
#ifdef EGTRAIN_RESTORE_SIGNALS_HELPER
#define signals Q_SIGNALS
#undef EGTRAIN_RESTORE_SIGNALS_HELPER
#endif
	return result;
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

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent),
										  ui(new Ui::MainWindow),
										  m_worker(nullptr),
										  m_workerThread(nullptr) {
	QFile themeFile(":/styles/egtrain.qss");
	if (themeFile.open(QIODevice::ReadOnly | QIODevice::Text))
		qApp->setStyleSheet(QString::fromUtf8(themeFile.readAll()));

	ui->setupUi(this);
	setupUpdateActions();
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
	pax_pixmap = QPixmap(":/icons/passenger.svg");
	pax_pixmap_scaled = pax_pixmap.scaled(QSize(14, 14), Qt::KeepAspectRatio, Qt::SmoothTransformation);

	// create container of items to show on display area
	scene = new NetworkScene(networkView);
	networkView->setScene(scene);
	connect(networkView, &NetworkView::viewportChanged, this, [this]() {
		updateViewportOverlays();
		updateZoomStatus();
	});
	connect(networkView, &NetworkView::interactionFinished,
		this, &MainWindow::updateViewportOverlays);
	networkView->setMouseTracking(true);

	// connect elements from UI
	connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::handleHelpAbout);
	connect(ui->actionSimulationStart, &QAction::triggered, this, &MainWindow::runCurrent);
	connect(ui->actionSimulationStop, &QAction::triggered, this, [this]() {
		if (m_worker)
			m_worker->requestStop();
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
	m_caseLayersDock->setWidget(caseLayersWidget);
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
		// updateViewportOverlays owns station name visibility
		updateViewportOverlays();
	});
	connect(m_stationNamesCheck, &QCheckBox::toggled, this, [this](bool checked) {
		m_stationNamesVisible = checked;
		for (auto* overlay : m_stationOverlays)
			if (overlay)
				overlay->setNameVisible(checked);
		updateViewportOverlays();
	});
	connect(m_trainLayerCheck, &QCheckBox::toggled, this, [this](bool checked) {
		m_trainLayerVisible = checked;
		for (auto* train : allTrains)
			if (train)
				train->setVisible(checked && !train->outOfSimulation);
		for (auto it = m_trainBadges.cbegin(); it != m_trainBadges.cend(); ++it) {
			if (!it.value())
				continue;
			auto trainIt = std::find_if(allTrains.cbegin(), allTrains.cend(),
				[it](const TrainItemGroup* train) { return train && train->index == it.key(); });
			it.value()->setVisible(checked && trainIt != allTrains.cend() && !(*trainIt)->outOfSimulation);
		}
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
	m_followTrainCombo->setToolTip("Select a train to keep centered while the simulation runs");
	m_followAction = new QAction("Follow", this);
	m_followAction->setObjectName("actionFollow");
	m_followAction->setCheckable(true);
	m_followAction->setToolTip("Center the network view on the selected train");
	connect(m_followTrainCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
		if (m_updatingFollowCombo || index < 0)
			return;
		if (m_followAction && m_followAction->isChecked())
			setFollowTrain(m_followTrainCombo->itemData(index).toInt());
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
	m_followTrainCombo->setAccessibleDescription("Select the train to center in the network view");
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

	// file menu: output folder chooser
	if (ui->menuFile) {
		QAction* outAction = new QAction("Set Output Folder...", this);
		connect(outAction, &QAction::triggered, this, &MainWindow::chooseOutputFolder);
		ui->menuFile->addAction(outAction);
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
	if (ui->menuView)
		ui->menuView->addAction(m_loadedDataDock->toggleViewAction());
	connect(m_loadedDataTree, &QTreeWidget::itemActivated, this,
			[this](QTreeWidgetItem* item, int) { activateLoadedDataItem(item); });

	// Case settings: the six scene-level values stay in one small editor so
	// opening the panel never changes the model and every edit uses one commit path.
	m_caseSettingsDock = new QDockWidget("Case Settings", this);
	m_caseSettingsDock->setObjectName("caseSettingsDock");
	QWidget* caseSettingsWidget = new QWidget(m_caseSettingsDock);
	QFormLayout* caseSettingsLayout = new QFormLayout(caseSettingsWidget);
	m_caseNameEdit = new QLineEdit(caseSettingsWidget);
	m_caseNameEdit->setObjectName("caseNameEdit");
	m_caseDescriptionEdit = new QLineEdit(caseSettingsWidget);
	m_caseDescriptionEdit->setObjectName("caseDescriptionEdit");
	m_caseBaseTimeEdit = new QLineEdit(caseSettingsWidget);
	m_caseBaseTimeEdit->setObjectName("caseBaseTimeEdit");
	caseSettingsLayout->addRow("Name", m_caseNameEdit);
	caseSettingsLayout->addRow("Description", m_caseDescriptionEdit);
	caseSettingsLayout->addRow("Base time", m_caseBaseTimeEdit);
	const auto makeCaseSettingSpinBox = [caseSettingsWidget](const char* objectName) {
		auto* edit = new CompactDoubleSpinBox(caseSettingsWidget);
		edit->setObjectName(objectName);
		edit->setRange(0.0, std::numeric_limits<double>::max());
		edit->setDecimals(std::numeric_limits<double>::max_digits10);
		edit->setSingleStep(1.0);
		edit->setKeyboardTracking(false);
		return edit;
	};
	m_caseDurationSecondsEdit = makeCaseSettingSpinBox("caseDurationSecondsEdit");
	m_caseBufferSecondsEdit = makeCaseSettingSpinBox("caseBufferSecondsEdit");
	m_caseRecoveryPercentEdit = makeCaseSettingSpinBox("caseRecoveryPercentEdit");
	caseSettingsLayout->addRow("Duration / horizon (s)", m_caseDurationSecondsEdit);
	caseSettingsLayout->addRow("Buffer (s)", m_caseBufferSecondsEdit);
	caseSettingsLayout->addRow("Recovery (%)", m_caseRecoveryPercentEdit);
	caseSettingsLayout->addRow(new QLabel("Changes apply when editing finishes.", caseSettingsWidget));
	m_caseSettingsDock->setWidget(caseSettingsWidget);
	addDockWidget(Qt::RightDockWidgetArea, m_caseSettingsDock);
	m_caseSettingsDock->hide();
	editorsMenu()->addAction(m_caseSettingsDock->toggleViewAction());
	connect(m_caseNameEdit, &QLineEdit::editingFinished, this, &MainWindow::commitCaseSettings);
	connect(m_caseDescriptionEdit, &QLineEdit::editingFinished, this, &MainWindow::commitCaseSettings);
	connect(m_caseBaseTimeEdit, &QLineEdit::editingFinished, this, &MainWindow::commitCaseSettings);
	connect(m_caseDurationSecondsEdit, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
			this, [this](double) { commitCaseSettings(); });
	connect(m_caseBufferSecondsEdit, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
			this, [this](double) { commitCaseSettings(); });
	connect(m_caseRecoveryPercentEdit, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
			this, [this](double) { commitCaseSettings(); });
	refreshCaseSettingsPanel();

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
	m_blockTrackFilterCombo = new QComboBox(infrastructureWidget);
	m_blockTrackFilterCombo->setObjectName("blockTrackFilterCombo");
	m_blockTrackFilterCombo->setAccessibleName("Block track filter");
	m_blockTrackFilterCombo->setToolTip("Show blocks on one track; Add appends to this track");
	infrastructureToolbar->addWidget(m_blockTrackFilterCombo);
	m_insertBlockButton = new QPushButton("Insert", infrastructureWidget);
	m_insertBlockButton->setObjectName("blockInsertButton");
	m_moveBlockUpButton = new QPushButton("Move Up", infrastructureWidget);
	m_moveBlockUpButton->setObjectName("blockMoveUpButton");
	m_moveBlockDownButton = new QPushButton("Move Down", infrastructureWidget);
	m_moveBlockDownButton->setObjectName("blockMoveDownButton");
	infrastructureToolbar->addWidget(m_insertBlockButton);
	infrastructureToolbar->addWidget(m_moveBlockUpButton);
	infrastructureToolbar->addWidget(m_moveBlockDownButton);
	infrastructureLayout->addLayout(infrastructureToolbar);
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
	editorsMenu()->addAction(m_infrastructureDock->toggleViewAction());
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
	m_trainUnitDock = new QDockWidget("Train Units", this);
	m_trainUnitDock->setObjectName("trainUnitDock");
	QWidget* trainUnitWidget = new QWidget(m_trainUnitDock);
	QHBoxLayout* trainUnitLayout = new QHBoxLayout(trainUnitWidget);
	QWidget* trainUnitListPane = new QWidget(trainUnitWidget);
	QVBoxLayout* trainUnitListLayout = new QVBoxLayout(trainUnitListPane);
	trainUnitListLayout->addWidget(new QLabel("Train Units", trainUnitListPane));
	m_trainUnitListWidget = new QListWidget(trainUnitListPane);
	trainUnitListLayout->addWidget(m_trainUnitListWidget);
	QHBoxLayout* trainUnitButtonLayout = new QHBoxLayout();
	m_addTrainUnitButton = new QPushButton("Add Unit", trainUnitListPane);
	m_duplicateTrainUnitButton = new QPushButton("Duplicate", trainUnitListPane);
	m_deleteTrainUnitButton = new QPushButton("Delete", trainUnitListPane);
	trainUnitButtonLayout->addWidget(m_addTrainUnitButton);
	trainUnitButtonLayout->addWidget(m_duplicateTrainUnitButton);
	trainUnitButtonLayout->addWidget(m_deleteTrainUnitButton);
	trainUnitListLayout->addLayout(trainUnitButtonLayout);
	trainUnitLayout->addWidget(trainUnitListPane);

	QWidget* trainUnitDetailPane = new QWidget(trainUnitWidget);
	QVBoxLayout* trainUnitDetailLayout = new QVBoxLayout(trainUnitDetailPane);
	trainUnitDetailLayout->addWidget(new QLabel("Train Unit Id", trainUnitDetailPane));
	m_trainUnitIdEdit = new QLineEdit(trainUnitDetailPane);
	trainUnitDetailLayout->addWidget(m_trainUnitIdEdit);

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
	trainUnitDetailLayout->addWidget(new QLabel("Tractive-effort source reference", trainUnitDetailPane));
	m_trainUnitSourceTractionEdit = new QLineEdit(trainUnitDetailPane);
	m_trainUnitSourceTractionEdit->setPlaceholderText("Optional");
	trainUnitDetailLayout->addWidget(m_trainUnitSourceTractionEdit);
	m_plotTrainUnitTractionButton = new QPushButton("Plot input traction characteristic", trainUnitDetailPane);
	trainUnitDetailLayout->addWidget(m_plotTrainUnitTractionButton);

	trainUnitDetailLayout->addWidget(new QLabel("Traction curve", trainUnitDetailPane));
	m_trainUnitTractionTable = new QTableWidget(0, 5, trainUnitDetailPane);
	m_trainUnitTractionTable->setHorizontalHeaderLabels(QStringList()
		<< "Lower speed (m/s)" << "Upper speed (m/s)" << "C0" << "C1" << "C2");
	m_trainUnitTractionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_trainUnitTractionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	// size columns to their headers so "Lower speed (m/s)" is never clipped
	m_trainUnitTractionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	m_trainUnitTractionTable->horizontalHeader()->setStretchLastSection(true);
	trainUnitDetailLayout->addWidget(m_trainUnitTractionTable);
	QHBoxLayout* tractionButtonLayout = new QHBoxLayout();
	m_addTrainUnitTractionButton = new QPushButton("Add Traction Row", trainUnitDetailPane);
	m_removeTrainUnitTractionButton = new QPushButton("Delete Traction Row", trainUnitDetailPane);
	tractionButtonLayout->addWidget(m_addTrainUnitTractionButton);
	tractionButtonLayout->addWidget(m_removeTrainUnitTractionButton);
	trainUnitDetailLayout->addLayout(tractionButtonLayout);
	trainUnitDetailLayout->addStretch();

	QScrollArea* trainUnitDetailScroll = new QScrollArea(trainUnitWidget);
	trainUnitDetailScroll->setWidgetResizable(true);
	trainUnitDetailScroll->setFrameShape(QFrame::NoFrame);
	trainUnitDetailScroll->setWidget(trainUnitDetailPane);
	trainUnitLayout->addWidget(trainUnitDetailScroll);

	m_trainUnitDock->setWidget(trainUnitWidget);
	addDockWidget(Qt::RightDockWidgetArea, m_trainUnitDock);
	editorsMenu()->addAction(m_trainUnitDock->toggleViewAction());
	connect(m_trainUnitListWidget, &QListWidget::currentRowChanged, this, [this](int) {
		updateTrainUnitDetailPanel();
	});
	connect(m_trainUnitIdEdit, &QLineEdit::editingFinished, this, &MainWindow::commitTrainUnitIdEdit);
	connect(m_trainUnitSourceDataEdit, &QLineEdit::editingFinished, this, &MainWindow::commitTrainUnitSources);
	connect(m_trainUnitSourceTractionEdit, &QLineEdit::editingFinished, this, &MainWindow::commitTrainUnitSources);
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
	editorsMenu()->addAction(m_compositionDock->toggleViewAction());

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
	m_serviceIdEdit->setObjectName("serviceIdEdit");
	serviceDetailLayout->addWidget(m_serviceIdEdit);
	serviceDetailLayout->addWidget(new QLabel("Operating code", serviceDetailPane));
	m_serviceOperatingCodeEdit = new QLineEdit(serviceDetailPane);
	m_serviceOperatingCodeEdit->setObjectName("serviceOperatingCodeEdit");
	serviceDetailLayout->addWidget(m_serviceOperatingCodeEdit);
	serviceDetailLayout->addWidget(new QLabel("Composition", serviceDetailPane));
	m_serviceCompositionCombo = new QComboBox(serviceDetailPane);
	serviceDetailLayout->addWidget(m_serviceCompositionCombo);
	serviceDetailLayout->addWidget(new QLabel("Route", serviceDetailPane));
	m_serviceRouteCombo = new QComboBox(serviceDetailPane);
	serviceDetailLayout->addWidget(m_serviceRouteCombo);
	m_serviceThroughCheck = new QCheckBox("Through service", serviceDetailPane);
	m_serviceThroughCheck->setObjectName("serviceThroughCheck");
	serviceDetailLayout->addWidget(m_serviceThroughCheck);

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
	m_serviceHasRepeatCountCheck = new QCheckBox("Occurrence count", serviceDetailPane);
	m_serviceHasRepeatCountCheck->setObjectName("serviceHasRepeatCountCheck");
	m_serviceRepeatCountEdit = new QLineEdit(serviceDetailPane);
	m_serviceRepeatCountEdit->setObjectName("serviceRepeatCountEdit");
	m_serviceRepeatCountEdit->setValidator(
		new QIntValidator(1, std::numeric_limits<int>::max(), m_serviceRepeatCountEdit));
	repeatCountLayout->addWidget(m_serviceHasRepeatCountCheck);
	repeatCountLayout->addWidget(m_serviceRepeatCountEdit);
	serviceDetailLayout->addLayout(repeatCountLayout);

	QHBoxLayout* performanceLayout = new QHBoxLayout();
	performanceLayout->addWidget(new QLabel("Performance (%)", serviceDetailPane));
	m_servicePerformancePercentEdit = new CompactDoubleSpinBox(serviceDetailPane);
	m_servicePerformancePercentEdit->setObjectName("servicePerformancePercentEdit");
	m_servicePerformancePercentEdit->setRange(1.0, 100.0);
	m_servicePerformancePercentEdit->setDecimals(std::numeric_limits<double>::max_digits10);
	m_servicePerformancePercentEdit->setValue(100);
	performanceLayout->addWidget(m_servicePerformancePercentEdit);
	serviceDetailLayout->addLayout(performanceLayout);

	QHBoxLayout* maximumSpeedLayout = new QHBoxLayout();
	m_serviceHasMaximumSpeedCheck = new QCheckBox("Maximum speed (km/h)", serviceDetailPane);
	m_serviceHasMaximumSpeedCheck->setObjectName("serviceHasMaximumSpeedCheck");
	m_serviceMaximumSpeedKmhEdit = new CompactDoubleSpinBox(serviceDetailPane);
	m_serviceMaximumSpeedKmhEdit->setObjectName("serviceMaximumSpeedKmhEdit");
	m_serviceMaximumSpeedKmhEdit->setRange(0.1, 1000.0);
	m_serviceMaximumSpeedKmhEdit->setDecimals(std::numeric_limits<double>::max_digits10);
	m_serviceMaximumSpeedKmhEdit->setValue(100.0);
	maximumSpeedLayout->addWidget(m_serviceHasMaximumSpeedCheck);
	maximumSpeedLayout->addWidget(m_serviceMaximumSpeedKmhEdit);
	serviceDetailLayout->addLayout(maximumSpeedLayout);

	QHBoxLayout* operatingCodeStepLayout = new QHBoxLayout();
	m_serviceHasOperatingCodeStepCheck = new QCheckBox("Operating-code step", serviceDetailPane);
	m_serviceHasOperatingCodeStepCheck->setObjectName("serviceHasOperatingCodeStepCheck");
	m_serviceOperatingCodeStepEdit = new QLineEdit(serviceDetailPane);
	m_serviceOperatingCodeStepEdit->setObjectName("serviceOperatingCodeStepEdit");
	m_serviceOperatingCodeStepEdit->setValidator(
		new QIntValidator(-std::numeric_limits<int>::max(), std::numeric_limits<int>::max(),
			m_serviceOperatingCodeStepEdit));
	operatingCodeStepLayout->addWidget(m_serviceHasOperatingCodeStepCheck);
	operatingCodeStepLayout->addWidget(m_serviceOperatingCodeStepEdit);
	serviceDetailLayout->addLayout(operatingCodeStepLayout);

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
	m_stopHasArrivalCheck = new QCheckBox("Planned arrival (s)", serviceDetailPane);
	m_stopArrivalSecondsEdit = new QLineEdit(serviceDetailPane);
	m_stopArrivalSecondsEdit->setValidator(
		new QIntValidator(0, std::numeric_limits<int>::max(), m_stopArrivalSecondsEdit));
	stopArrivalLayout->addWidget(m_stopHasArrivalCheck);
	stopArrivalLayout->addWidget(m_stopArrivalSecondsEdit);
	serviceDetailLayout->addLayout(stopArrivalLayout);

	QHBoxLayout* stopDepartureLayout = new QHBoxLayout();
	m_stopHasDepartureCheck = new QCheckBox("Planned departure (s)", serviceDetailPane);
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

	QWidget* occurrencePane = new QWidget(serviceWidget);
	QVBoxLayout* occurrenceLayout = new QVBoxLayout(occurrencePane);
	occurrenceLayout->addWidget(new QLabel("Run occurrences", occurrencePane));
	m_serviceOccurrenceSelectionLabel = new QLabel(occurrencePane);
	m_serviceOccurrenceSelectionLabel->setObjectName("serviceOccurrenceSelectionLabel");
	occurrenceLayout->addWidget(m_serviceOccurrenceSelectionLabel);
	m_serviceOccurrenceTable = new QTableWidget(occurrencePane);
	m_serviceOccurrenceTable->setObjectName("serviceOccurrenceTable");
	m_serviceOccurrenceTable->setColumnCount(6);
	m_serviceOccurrenceTable->setHorizontalHeaderLabels({
		"Include", "Operating code", "Service / occurrence", "Offset / departure", "Performance (%)", "Maximum speed (km/h)"});
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
	serviceLayout->addWidget(occurrencePane);

	m_serviceDock->setWidget(serviceWidget);
	addDockWidget(Qt::RightDockWidgetArea, m_serviceDock);
	m_serviceDock->hide();
	tabifyDockWidget(m_compositionDock, m_serviceDock);
	editorsMenu()->addAction(m_serviceDock->toggleViewAction());

	connect(m_serviceListWidget, &QListWidget::currentRowChanged, this, [this](int) {
		updateServiceDetailPanel();
	});
	connect(m_serviceIdEdit, &QLineEdit::editingFinished, this, &MainWindow::commitServiceIdEdit);
	connect(m_serviceOperatingCodeEdit, &QLineEdit::editingFinished, this, &MainWindow::commitServiceOperatingCode);
	connect(m_serviceCompositionCombo, &QComboBox::currentTextChanged, this, &MainWindow::commitServiceComposition);
	connect(m_serviceRouteCombo, &QComboBox::currentTextChanged, this, &MainWindow::commitServiceRoute);
	connect(m_serviceThroughCheck, &QCheckBox::toggled, this, &MainWindow::commitServiceThrough);
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
	m_incidentHasOccurrenceCheck = new QCheckBox("Occurrence (1-based)", incidentDetailPane);
	m_incidentHasOccurrenceCheck->setObjectName("incidentHasOccurrenceCheck");
	m_incidentOccurrenceEdit = new QLineEdit(incidentDetailPane);
	m_incidentOccurrenceEdit->setObjectName("incidentOccurrenceEdit");
	m_incidentOccurrenceEdit->setValidator(new QIntValidator(1, std::numeric_limits<int>::max(), m_incidentOccurrenceEdit));
	incidentDetailLayout->addWidget(m_incidentHasOccurrenceCheck);
	incidentDetailLayout->addWidget(m_incidentOccurrenceEdit);
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
	incidentLayout->addWidget(scenarioEditorTabs, 2);

	m_incidentDock->setWidget(incidentWidget);
	addDockWidget(Qt::RightDockWidgetArea, m_incidentDock);
	m_incidentDock->hide();
	m_trainUnitDock->hide();
	tabifyDockWidget(m_serviceDock, m_incidentDock);
	editorsMenu()->addAction(m_incidentDock->toggleViewAction());

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
	connect(m_incidentHasOccurrenceCheck, &QCheckBox::toggled, this, &MainWindow::commitIncidentHasOccurrence);
	connect(m_incidentOccurrenceEdit, &QLineEdit::editingFinished, this, &MainWindow::commitIncidentOccurrence);
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
	m_passengerDiagnosticLabel->setStyleSheet("color: #d9822b;");
	passengerDetailLayout->addWidget(m_passengerDiagnosticLabel);

	m_passengerTabs = new QTabWidget(passengerDetailPane);
	m_passengerTabs->setObjectName("passengerEditorTabs");

	QWidget* journeysPane = new QWidget(m_passengerTabs);
	QHBoxLayout* journeysLayout = new QHBoxLayout(journeysPane);
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
	journeysLayout->addWidget(journeyListPane);

	QWidget* journeyDetailPane = new QWidget(journeysPane);
	QFormLayout* journeyForm = new QFormLayout(journeyDetailPane);
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
	QHBoxLayout* legsLayout = new QHBoxLayout(legsPane);
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
	legsLayout->addWidget(legListPane);

	QWidget* legDetailPane = new QWidget(legsPane);
	QFormLayout* legForm = new QFormLayout(legDetailPane);
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
	passengerLayout->addWidget(passengerDetailPane, 2);
	m_passengerDock->setWidget(passengerWidget);
	addDockWidget(Qt::RightDockWidgetArea, m_passengerDock);
	m_passengerDock->hide();
	tabifyDockWidget(m_serviceDock, m_passengerDock);
	editorsMenu()->addAction(m_passengerDock->toggleViewAction());

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
	connect(ui->actionQuit, &QAction::triggered, this, &QApplication::quit);

	// Diagrams menu
	m_diagramsMenu = menuBar()->addMenu("Diagrams");
	m_diagramsMenu->addAction("Speed / Distance (per train)...", this, &MainWindow::showSpeedDistanceDiagram);
	m_diagramsMenu->addAction("Speed / Time (per train)...", this, &MainWindow::showSpeedTimeDiagram);
	m_diagramsMenu->addAction("Time / Distance (per train)...", this, &MainWindow::showTimeDistanceDiagram);
	m_diagramsMenu->addAction("Simulated tractive effort / Distance (per train)...", this, &MainWindow::showTractiveEffortDistanceDiagram);
	m_diagramsMenu->addSeparator();
	m_diagramsMenu->addAction("Timetable graph (train graph)...", this, &MainWindow::showTimetableGraph);
	m_diagramsMenu->addAction("Blocking-time overlay...", this, &MainWindow::showBlockingTimeDiagram);
	m_diagramsMenu->addAction("Capacity analysis...", this, &MainWindow::showCapacityAnalysis);
	m_diagramsMenu->addAction("Timetable table (planned vs simulated)...", this, &MainWindow::showTimetableTable);
	m_diagramsMenu->addAction("Train delays...", this, &MainWindow::showDelayDiagram);
	// Train paths belongs with the other charts; retire the one-entry Tools menu.
	m_diagramsMenu->addSeparator();
	ui->displayTrainPathDiagrams->setText("Train paths (per corridor)...");
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
}

MainWindow::~MainWindow() {
	clearSimulationWorker(true);
	delete ui;
}

void MainWindow::closeEvent(QCloseEvent* event) {
	if (maybeSaveScene()) {
		clearSimulationWorker(true);
		QMainWindow::closeEvent(event);
	} else {
		event->ignore();
	}
}

void MainWindow::newScene() {
	if (!maybeSaveScene())
		return;

	m_excludedSceneOccurrences.clear();
	m_lastRunSelectedOccurrences = 0;
	m_lastRunTotalOccurrences = 0;
	teardownGUI();
	simulation.resetState();
	m_sceneDir.clear();
	m_savedSceneSha256.clear();
	m_sceneModel = makeNewSceneModel();
	++m_sceneRevision;
	m_delayBaseline.reset();
	m_sceneLoaded = true;
	m_sceneIsBundle = false;
	m_sceneBundleVersion.reset();
	m_sceneDirty = true;
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
	refreshCaseSettingsPanel();
	refreshCompositionPanel();
	refreshTrainUnitPanel();
	refreshServicePanel();
	refreshIncidentPanel();
	refreshInfrastructurePanel();
	refreshPassengerPanel();
	refreshValidationPanel();
	renderTrackPreview(m_sceneModel);
	if (m_caseSettingsDock) {
		m_caseSettingsDock->show();
		m_caseSettingsDock->raise();
	}
	if (m_infrastructureDock) {
		m_infrastructureDock->show();
		m_infrastructureDock->raise();
	}
}

void MainWindow::openSceneDialog() {
	if (!maybeSaveScene())
		return;

	const QString startDir = m_sceneDir.isEmpty() ? QDir::homePath() : QFileInfo(m_sceneDir).absolutePath();
	const QString path = QFileDialog::getOpenFileName(this, "Open Case Study", startDir,
		"EGTRAIN Case Study (*.egscene)");
	if (path.isEmpty())
		return;

	openSceneDirectory(path);
}

void MainWindow::openSceneFolderDialog() {
	if (!maybeSaveScene())
		return;

	const QString startDir = m_sceneDir.isEmpty() ? QDir::homePath() : QFileInfo(m_sceneDir).absolutePath();
	const QString dir = QFileDialog::getExistingDirectory(this, "Open Scene Folder", startDir);
	if (!dir.isEmpty())
		openSceneDirectory(dir);
}

bool MainWindow::openSceneDirectory(const QString& dir) {
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
				.arg(compatibility.schemaVersion).arg(kCurrentSceneSchemaVersion);
		if (compatibility.sourceKind == SceneSourceKind::Bundle && compatibility.bundleVersion
				&& *compatibility.bundleVersion < kCurrentSceneBundleVersion) {
			if (!ageDetails.isEmpty())
				ageDetails += "; ";
			ageDetails += QString("bundle layout %1 (this application uses bundle layout %2)")
				.arg(*compatibility.bundleVersion).arg(kCurrentSceneBundleVersion);
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
			QString message = firstDiagnosticMessage(migrated.diagnostics);
			if (message.isEmpty())
				message = compatibilityMessage();
			showBlockingError(this, "Cannot Upgrade Scene", message);
			return false;
		}
		return openSceneDirectory(destination);
	}
	if (compatibility.classification == SceneCompatibilityClass::OlderUnsupported) {
		if (!noCompatibilityDialogs)
			showBlockingError(this, "Older Scene Not Supported",
				"This scene uses an older schema or bundle layout with no registered migration path.\n\n"
				+ compatibilityMessage());
		return false;
	}
	if (compatibility.classification == SceneCompatibilityClass::Newer) {
		if (noCompatibilityDialogs) {
			statusBar()->showMessage("Newer scene format requires a newer EGTRAIN version");
			return false;
		}
		QMessageBox dialog(this);
		dialog.setIcon(QMessageBox::Question);
		dialog.setWindowTitle("Newer Scene");
		dialog.setText("This scene was saved with a newer scene format.");
		QString details = QString("Supported schema: %1; scene schema: %2")
			.arg(kCurrentSceneSchemaVersion).arg(compatibility.schemaVersion);
		if (compatibility.bundleVersion)
			details += QString("; supported bundle: %1; scene bundle: %2")
				.arg(kCurrentSceneBundleVersion).arg(*compatibility.bundleVersion);
		dialog.setInformativeText(details);
		QPushButton* updates = dialog.addButton("Check for Updates...", QMessageBox::AcceptRole);
		dialog.addButton("Cancel", QMessageBox::RejectRole);
		dialog.exec();
		if (dialog.clickedButton() == updates)
			startUpdateCheck(true);
		return false;
	}
	if (compatibility.classification == SceneCompatibilityClass::Malformed) {
		if (!noCompatibilityDialogs)
			showBlockingError(this, "Cannot Open Scene", compatibilityMessage());
		return false;
	}
	const bool sceneIsBundle = compatibility.sourceKind == SceneSourceKind::Bundle;
	const bool reloadingSameScene = m_sceneLoaded
		&& QFileInfo(m_sceneDir).absoluteFilePath() == scenePath;
	auto result = loadScenePath(scenePath.toStdString());
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

	m_excludedSceneOccurrences.clear();
	m_lastRunSelectedOccurrences = 0;
	m_lastRunTotalOccurrences = 0;
	m_sceneDir = scenePath;
	m_sceneModel = result.scene;
	++m_sceneRevision;
	m_delayBaseline.reset();
	m_sceneLoaded = true;
	m_sceneIsBundle = sceneIsBundle;
	m_sceneBundleVersion = result.bundleVersion;
	m_savedSceneSha256 = hashSceneInputSnapshot(result.inputSnapshot);
	m_sceneDirty = false;
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
	refreshCaseSettingsPanel();
	updateSceneWindowTitle();
	updateCaseLayersPanel();
	updateSceneActions();
	addRecentScene(scenePath);
	statusBar()->showMessage(QString("%1: %2 (%3 services, %4 routes)")
								 .arg(reloadingSameScene ? "Scene reloaded" : "Scene loaded")
								 .arg(QString::fromStdString(m_sceneModel.name))
								 .arg(static_cast<int>(m_sceneModel.services.size()))
								 .arg(static_cast<int>(m_sceneModel.routes.size())));
	refreshCompositionPanel();
	refreshTrainUnitPanel();
	refreshServicePanel();
	refreshIncidentPanel();
	refreshInfrastructurePanel();
	refreshPassengerPanel();
	refreshValidationPanel();
	renderTrackPreview(m_sceneModel);
	if (m_loadedDataDock) {
		m_loadedDataDock->show();
		m_loadedDataDock->raise();
	}
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

void MainWindow::renderTrackPreview(const SceneModel& sceneModel) {
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
		scene->clear();
	}
	effect = nullptr;
	const QString selectedFacet = m_infrastructureFacetCombo
									  ? m_infrastructureFacetCombo->currentData().toString()
									  : QString();
	const std::string selectedId = m_infrastructureSelectionId.toStdString();
	std::set<std::string> selectedTrackIds;
	std::set<std::string> selectedNodeIds;
	const auto selectTrack = [&selectedTrackIds](const std::string& id) {
		if (!id.empty())
			selectedTrackIds.insert(id);
	};
	const auto selectNode = [&selectedNodeIds, &selectTrack, &sceneModel](const std::string& id) {
		if (id.empty())
			return;
		selectedNodeIds.insert(id);
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
				for (const auto& component : previewRouteComponents(token)) {
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
	for (std::size_t index = 0; index < preview.lines.size(); ++index) {
		const auto& line = preview.lines[index];
		if (line.points.size() < 2 || std::any_of(line.points.begin(), line.points.end(), [](const auto& point) {
				return !std::isfinite(point.x) || !std::isfinite(point.y);
			}))
			continue;
		const qreal offset = static_cast<qreal>(line.displayOffset);
		tracks[line.id] = {&line, offset};

		QPen pen(selectedTrackIds.count(line.id) > 0 ? QColor(242, 170, 70) : QColor(185, 190, 198));
		pen.setWidthF(selectedTrackIds.count(line.id) > 0 ? 3.0 : 1.25);
		pen.setCosmetic(true);
		QPainterPath path(QPointF(line.points.front().x, line.points.front().y + offset));
		for (std::size_t point = 1; point < line.points.size(); ++point)
			path.lineTo(line.points[point].x, line.points[point].y + offset);
		auto* item = scene->addPath(path, pen);
		item->setAcceptedMouseButtons(Qt::NoButton);
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

		QPainterPath path(start);
		path.lineTo(end);
		QPen pen(QColor(210, 215, 222));
		pen.setWidthF(2.25);
		pen.setCosmetic(true);
		pen.setCapStyle(Qt::RoundCap);
		auto* item = scene->addPath(path, pen);
		item->setZValue(0.5);
		item->setAcceptedMouseButtons(Qt::NoButton);
		includePreviewPoint(start);
		includePreviewPoint(end);
	}

	// Infrastructure selections use a few lightweight overlays rather than a
	// second graphics-item model. Invalid intermediate references simply do not
	// produce an overlay; the canonical model remains untouched.
	if (m_infrastructureDock && m_infrastructureDock->isVisible()) {
		for (const auto& node : sceneModel.nodes) {
			const auto track = tracks.find(node.trackId);
			if (track == tracks.end())
				continue;
			QPointF point;
			if (!previewPointAtNode(*track->second.first, node.id, track->second.second, point))
				continue;
			const bool highlighted = selectedNodeIds.count(node.id) > 0 || selectedTrackIds.count(node.trackId) > 0;
			const qreal radius = highlighted ? 4.5 : 2.5;
			auto* marker = scene->addEllipse(QRectF(-radius, -radius, radius * 2.0, radius * 2.0),
				QPen(highlighted ? QColor(255, 215, 105) : QColor(130, 155, 185)),
				QBrush(highlighted ? QColor(220, 115, 45) : QColor(85, 105, 130)));
			marker->setPos(point);
			marker->setFlag(QGraphicsItem::ItemIgnoresTransformations);
			marker->setAcceptedMouseButtons(Qt::NoButton);
		}

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
		for (const auto& track : tracks) {
			const auto& points = track.second.first->points;
			QPointF anchor;
			if (!station.nodeId.empty()) {
				if (!previewPointAtNode(*track.second.first, station.nodeId, track.second.second, anchor))
					continue;
			} else {
				const double minX = std::min(points.front().rawX, points.back().rawX);
				const double maxX = std::max(points.front().rawX, points.back().rawX);
				if (station.x < minX || station.x > maxX)
					continue;
				if (!previewPointAtX(*track.second.first, station.x, track.second.second, anchor))
					break;
			}
			paintStationOverlay(anchor, classifyStation(), station.name, 0.75);
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

		for (const bool reversed : {true, false}) {
			auto* glyph = new SignalItem(QRectF(-4.0, -4.0, 8.0, 8.0));
			glyph->setZValue(3.0);
			glyph->setPos(center);
			glyph->setPen(QPen(QColor("#0D131A"), 1.0));
			glyph->setAspectCode(180);
			glyph->setReversedDirection(reversed);
			glyph->setData(kSignalDecorationRole, true);
			glyph->setData(kSignalTrackRole, -1);
			glyph->setData(kSignalBaseVisibleRole, true);
			glyph->setData(kSignalAnchorRole, center);
			glyph->setData(kSignalNormalRole, normal);
			glyph->setData(kSignalDirectionRole, reversed ? -1.0 : 1.0);
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
					? qBound<qreal>(0.0, QPointF::dotProduct(target - first, segment)
						/ lengthSquared, 1.0)
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

bool MainWindow::finishSceneSave(const SceneSaveResult& result) {
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
	m_sceneDirty = false;
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
	if (!m_sceneLoaded)
		return false;
	commitPendingEditorValues();
	if (m_sceneDir.isEmpty())
		return saveSceneAsToBundle();

	auto result = m_sceneIsBundle
		? saveSceneBundle(m_sceneModel, m_sceneDir.toStdString())
		: ::saveScene(m_sceneModel, m_sceneDir.toStdString());
	return finishSceneSave(result);
}

void MainWindow::saveSceneAs() {
	saveSceneAsToBundle();
}

bool MainWindow::saveSceneAsToBundle() {
	if (!m_sceneLoaded)
		return false;
	commitPendingEditorValues();

	const QString startPath = m_sceneDir.isEmpty() ? QDir::homePath()
		: (QFileInfo(m_sceneDir).isDir() ? m_sceneDir : QFileInfo(m_sceneDir).absoluteFilePath());
	QString targetPath = QFileDialog::getSaveFileName(this, "Save Case Study As", startPath,
		"EGTRAIN Case Study (*.egscene)");
	if (targetPath.isEmpty())
		return false;
	if (!targetPath.endsWith(".egscene", Qt::CaseInsensitive))
		targetPath += ".egscene";
	targetPath = QFileInfo(targetPath).absoluteFilePath();

	auto result = saveSceneBundle(m_sceneModel, targetPath.toStdString());
	if (!result.success())
		return finishSceneSave(result);

	m_sceneDir = targetPath;
	m_sceneIsBundle = true;
	m_sceneBundleVersion = kCurrentSceneBundleVersion;
	addRecentScene(targetPath);
	return finishSceneSave(result);
}

bool MainWindow::saveSceneAsToDirectory() {
	if (!m_sceneLoaded)
		return false;
	commitPendingEditorValues();

	const QString startDir = m_sceneDir.isEmpty() ? QDir::homePath()
		: (QFileInfo(m_sceneDir).isDir() ? m_sceneDir : QFileInfo(m_sceneDir).absolutePath());
	QString dir = QFileDialog::getExistingDirectory(this, "Save Scene As Folder", startDir);
	if (dir.isEmpty())
		return false;

	QString targetPath = QDir(dir).absolutePath();
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

	if (!copyScenePassthroughFiles(targetPath))
		return false;

	auto result = ::saveScene(m_sceneModel, targetPath.toStdString());
	if (!result.success())
		return finishSceneSave(result);

	m_sceneDir = targetPath;
	m_sceneIsBundle = false;
	m_sceneBundleVersion.reset();
	addRecentScene(targetPath);
	return finishSceneSave(result);
}

bool MainWindow::copyScenePassthroughFiles(const QString& targetDir) {
	if (m_sceneDir.isEmpty() || m_sceneIsBundle || !QFileInfo(m_sceneDir).isDir())
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
		writeUpdateCheckState(preferences, checked ? UpdateCheckState::Enabled
			: UpdateCheckState::Disabled);
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

void MainWindow::maybePromptForUpdateChecks() {
	if (!m_updateChecker || updatesSuppressedByEnvironment())
		return;
	QSettings settings;
	const UpdateCheckState state = readUpdateCheckState(settings);
	if (state == UpdateCheckState::Unknown) {
		QMessageBox dialog(QMessageBox::Question,
			QStringLiteral("Automatically Check for EGTRAIN Updates?"),
			QStringLiteral("EGTRAIN can check GitHub Releases when the application starts and notify you "
				"when a newer stable version is available."), QMessageBox::NoButton, this);
		dialog.setInformativeText(QStringLiteral("You can change this later from Help."));
		QPushButton* enableButton = dialog.addButton(QStringLiteral("Check Automatically"),
			QMessageBox::AcceptRole);
		dialog.addButton(QStringLiteral("Don't Check Automatically"), QMessageBox::RejectRole);
		dialog.setDefaultButton(enableButton);
		dialog.exec();
		const UpdateCheckState chosen = dialog.clickedButton() == enableButton
			? UpdateCheckState::Enabled : UpdateCheckState::Disabled;
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

	const StableRelease& release = *result.release;
	QString availability;
	if (m_selfUpdater && !m_selfUpdater->canSelfUpdate(release)) {
		const SelfUpdateCapability capability = m_selfUpdater->capability();
		availability = capability.supported
			? QStringLiteral("\n\nNo automatic update package is available for this platform. Use the release page to update manually.")
			: QStringLiteral("\n\n%1 Use the release page to update manually.").arg(capability.reason);
	}
	QMessageBox dialog(QMessageBox::Information,
		QStringLiteral("EGTRAIN %1 is Available").arg(formatSemanticVersion(release.version)),
		QStringLiteral("You are currently using %1.").arg(formatSemanticVersion(*current)),
		QMessageBox::NoButton, this);
	dialog.setInformativeText((release.notes.isEmpty()
		? QStringLiteral("No release notes were provided.") : release.notes) + availability);
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
	QMessageBox::information(this, QStringLiteral("EGTRAIN Update"),
		QStringLiteral("The update is ready. EGTRAIN will restart now."));
	if (!m_selfUpdater || !m_selfUpdater->restart()) {
		QMessageBox::warning(this, QStringLiteral("EGTRAIN Update"),
			QStringLiteral("Could not start the update installer. EGTRAIN is unchanged."));
		return;
	}
	QCoreApplication::quit();
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
			? std::optional<double>(initial_variables.times) : std::nullopt;
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

	SceneDiagnosticCounts counts = countDiagnostics(m_sceneDiagnostics);
	QString message = QString("Validation: %1 error(s), %2 warning(s)").arg(counts.errors).arg(counts.warnings);
	if (counts.infos > 0)
		message += QString(", %1 info").arg(counts.infos);
	if (m_validationStatusLabel)
		m_validationStatusLabel->setText(message);
	refreshLoadedDataTree();
	refreshScenarioList();
	refreshPassengerPanel();
	updateSceneActions();
	updateCaseLayersPanel();
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
			? QStringLiteral("Warning") : QStringLiteral("Ready"));
	auto* caseRoot = new QTreeWidgetItem(m_loadedDataTree);
	caseRoot->setText(0, "Case Study");
	caseRoot->setText(1, m_sceneDir);
	caseRoot->setText(2, "1");
	caseRoot->setText(3, caseStatus);
	caseRoot->setToolTip(0, "Loaded case-study review.");
	addRow(caseRoot, "Name", QString::fromStdString(m_sceneModel.name), "1", "Parsed");
	addRow(caseRoot, "Description", m_sceneModel.description.empty()
		? QStringLiteral("(none)") : QString::fromStdString(m_sceneModel.description), "1", "Parsed");
	addRow(caseRoot, "Source path", m_sceneDir, "1", "Loaded");
	addRow(caseRoot, "Canonical schema version", QString::number(m_sceneModel.schemaVersion),
		QString::number(kCurrentSceneSchemaVersion), "Parsed");
	addRow(caseRoot, "Saved with app version", m_sceneModel.savedWithAppVersion.empty()
		? QStringLiteral("(not recorded)") : QString::fromStdString(m_sceneModel.savedWithAppVersion), "1",
		m_sceneModel.savedWithAppVersion.empty() ? QStringLiteral("Missing optional") : QStringLiteral("Loaded"));
	if (m_sceneIsBundle)
		addRow(caseRoot, "Bundle format version", m_sceneBundleVersion
			? QString::number(*m_sceneBundleVersion) : QStringLiteral("(not recorded)"),
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
	addRow(runtimeAndResults, "Applied scenario", m_appliedScenarioId.empty()
		? QStringLiteral("(none)") : QString::fromStdString(m_appliedScenarioId), "1",
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
	if (!item)
		return;
	const QString targetType = item->data(0, kLoadedDataTargetTypeRole).toString();
	const QString targetId = item->text(0);
	if (targetType.isEmpty())
		return;
	auto raiseDock = [](QDockWidget* dock) {
		if (dock) {
			dock->show();
			dock->raise();
		}
	};
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
			? item->parent()->parent()->text(0) : QString();
		const SceneTrainUnit* unit = trainUnitById(unitId.toStdString());
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
	++m_sceneRevision;
	m_delayBaseline.reset();
	m_sceneDirty = true;
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
}

void MainWindow::refreshCaseSettingsPanel() {
	const bool hasScene = m_sceneLoaded;
	if (m_caseSettingsDock)
		m_caseSettingsDock->setEnabled(hasScene);
	if (m_caseNameEdit) {
		const QSignalBlocker blocker(m_caseNameEdit);
		m_caseNameEdit->setText(hasScene ? QString::fromStdString(m_sceneModel.name) : QString());
		m_caseNameEdit->setEnabled(hasScene);
	}
	if (m_caseDescriptionEdit) {
		const QSignalBlocker blocker(m_caseDescriptionEdit);
		m_caseDescriptionEdit->setText(hasScene ? QString::fromStdString(m_sceneModel.description) : QString());
		m_caseDescriptionEdit->setEnabled(hasScene);
	}
	if (m_caseBaseTimeEdit) {
		const QSignalBlocker blocker(m_caseBaseTimeEdit);
		m_caseBaseTimeEdit->setText(hasScene ? QString::fromStdString(m_sceneModel.baseTime) : QString());
		m_caseBaseTimeEdit->setEnabled(hasScene);
	}
	if (m_caseDurationSecondsEdit) {
		const QSignalBlocker blocker(m_caseDurationSecondsEdit);
		m_caseDurationSecondsEdit->setValue(hasScene ? m_sceneModel.settings.durationSeconds : 0.0);
		m_caseDurationSecondsEdit->setEnabled(hasScene);
	}
	if (m_caseBufferSecondsEdit) {
		const QSignalBlocker blocker(m_caseBufferSecondsEdit);
		m_caseBufferSecondsEdit->setValue(hasScene ? m_sceneModel.settings.bufferTimeSeconds : 0.0);
		m_caseBufferSecondsEdit->setEnabled(hasScene);
	}
	if (m_caseRecoveryPercentEdit) {
		const QSignalBlocker blocker(m_caseRecoveryPercentEdit);
		m_caseRecoveryPercentEdit->setValue(hasScene ? m_sceneModel.settings.recoveryTimePercent : 0.0);
		m_caseRecoveryPercentEdit->setEnabled(hasScene);
	}
}

void MainWindow::commitPendingEditorValues() {
	if (m_committingPendingEditorValues)
		return;
	m_committingPendingEditorValues = true;
	const auto hasEditorFocus = [](QWidget* editor) {
		QWidget* focus = QApplication::focusWidget();
		return editor && (editor->hasFocus() || (focus && editor->isAncestorOf(focus)));
	};

	commitPendingCaseSettings();
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
	if (m_stopArrivalSecondsEdit && m_stopArrivalSecondsEdit->isEnabled())
		commitStopArrivalSeconds();
	if (m_stopDepartureSecondsEdit && m_stopDepartureSecondsEdit->isEnabled())
		commitStopDepartureSeconds();
	if (m_stopDwellSecondsEdit && m_stopDwellSecondsEdit->isEnabled())
		commitStopDwellSeconds();
	commitScenarioIdEdit();
	commitScenarioNameEdit();
	commitScenarioDescriptionEdit();
	commitIncidentIdEdit();
	commitIncidentStartSeconds();
	if (m_incidentEndSecondsEdit && ((m_incidentHasEndSecondsCheck
			&& m_incidentHasEndSecondsCheck->isChecked())
			|| hasEditorFocus(m_incidentEndSecondsEdit)))
		commitIncidentEndSeconds();
	if (m_incidentHasOccurrenceCheck && m_incidentHasOccurrenceCheck->isChecked())
		commitIncidentOccurrence();
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
	if (m_caseDurationSecondsEdit)
		m_caseDurationSecondsEdit->interpretText();
	if (m_caseBufferSecondsEdit)
		m_caseBufferSecondsEdit->interpretText();
	if (m_caseRecoveryPercentEdit)
		m_caseRecoveryPercentEdit->interpretText();
	commitCaseSettings();
}

void MainWindow::commitCaseSettings() {
	if (!m_sceneLoaded || !m_caseNameEdit || !m_caseDescriptionEdit || !m_caseBaseTimeEdit
		|| !m_caseDurationSecondsEdit || !m_caseBufferSecondsEdit || !m_caseRecoveryPercentEdit)
		return;

	const std::string name = m_caseNameEdit->text().toStdString();
	const std::string description = m_caseDescriptionEdit->text().toStdString();
	const std::string baseTime = m_caseBaseTimeEdit->text().toStdString();
	const double durationSeconds = m_caseDurationSecondsEdit->value();
	const double bufferSeconds = m_caseBufferSecondsEdit->value();
	const double recoveryPercent = m_caseRecoveryPercentEdit->value();
	const bool durationChanged = m_sceneModel.settings.durationSeconds != durationSeconds;
	const bool bufferChanged = m_sceneModel.settings.bufferTimeSeconds != bufferSeconds;
	const bool recoveryChanged = m_sceneModel.settings.recoveryTimePercent != recoveryPercent;
	const bool changed = m_sceneModel.name != name
		|| m_sceneModel.description != description
		|| m_sceneModel.baseTime != baseTime
		|| durationChanged || bufferChanged || recoveryChanged;
	if (!changed)
		return;

	m_sceneModel.name = name;
	m_sceneModel.description = description;
	m_sceneModel.baseTime = baseTime;
	m_sceneModel.settings.hasDuration = m_sceneModel.settings.hasDuration || durationChanged;
	m_sceneModel.settings.durationSeconds = durationSeconds;
	m_sceneModel.settings.hasBufferTime = m_sceneModel.settings.hasBufferTime || bufferChanged;
	m_sceneModel.settings.bufferTimeSeconds = bufferSeconds;
	m_sceneModel.settings.hasRecoveryTime = m_sceneModel.settings.hasRecoveryTime || recoveryChanged;
	m_sceneModel.settings.recoveryTimePercent = recoveryPercent;
	m_startOffsetSeconds = baseTimeToSeconds(m_sceneModel.baseTime);
	markSceneDirty();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
}

std::string MainWindow::uniqueInfrastructureId(const std::string& baseId, const QString& facet) const {
	const auto exists = [this, &facet](const std::string& id) {
		if (facet == "tracks")
			return std::any_of(m_sceneModel.tracks.begin(), m_sceneModel.tracks.end(),
				[&id](const SceneTrack& item) { return item.id == id; });
		if (facet == "nodes")
			return std::any_of(m_sceneModel.nodes.begin(), m_sceneModel.nodes.end(),
				[&id](const SceneNode& item) { return item.id == id; });
		if (facet == "arcs")
			return std::any_of(m_sceneModel.arcs.begin(), m_sceneModel.arcs.end(),
				[&id](const SceneArc& item) { return item.id == id; });
		if (facet == "blocks")
			return std::any_of(m_sceneModel.blocks.begin(), m_sceneModel.blocks.end(),
				[&id](const SceneBlock& item) { return item.id == id; });
		if (facet == "connections")
		return std::any_of(m_sceneModel.connections.begin(), m_sceneModel.connections.end(),
			[&id](const SceneConnection& item) { return item.id == id; });
		if (facet == "stations")
			return std::any_of(m_sceneModel.stations.begin(), m_sceneModel.stations.end(),
							   [&id](const SceneStation& item) { return item.id == id; });
		if (facet == "signals")
			return std::any_of(sceneSignals(m_sceneModel).begin(), sceneSignals(m_sceneModel).end(),
							   [&id](const SceneSignal& item) { return item.id == id; });
		if (facet == "signalling_areas")
			return std::any_of(m_sceneModel.signallingAreas.begin(), m_sceneModel.signallingAreas.end(),
							   [&id](const SceneSignallingArea& item) { return item.id == id; });
		return std::any_of(m_sceneModel.routes.begin(), m_sceneModel.routes.end(),
						   [&id](const SceneRoute& item) { return item.id == id; });
	};
	if (facet == "platforms" && !m_sceneModel.stations.empty()) {
		const auto& platforms = m_sceneModel.stations.front().platforms;
		std::string candidate = baseId;
		int suffix = 2;
		while (std::any_of(platforms.begin(), platforms.end(), [&candidate](const ScenePlatform& item) {
			return item.id == candidate;
		}))
			candidate = baseId + "_" + std::to_string(suffix++);
		return candidate;
	}
	std::string candidate = baseId;
	int suffix = 2;
	while (exists(candidate))
		candidate = baseId + "_" + std::to_string(suffix++);
	return candidate;
}

QStringList MainWindow::directDeleteConsumers(const QString& facet, const std::string& id,
		const std::string& scope) const {
	QStringList consumers;
	const auto add = [&consumers](const QString& description) {
		if (!description.isEmpty() && !consumers.contains(description))
			consumers << description;
	};
	const auto sectionUsesBlock = [&id](const SceneSectionInventory& inventory,
			const std::string& reference) {
		const std::vector<std::string> components = previewRouteComponents(reference);
		if (std::find(components.begin(), components.end(), id) != components.end())
			return true;
		const SceneSectionDescriptor* section = inventory.resolve(reference);
		return section && (section->sourceBlockId == id || section->firstBlockId == id
			|| section->secondBlockId == id);
	};
	const auto sectionUsesConnection = [](const SceneSectionInventory& inventory,
			const std::string& connectionId, const std::string& reference) {
		const SceneSectionDescriptor* section = inventory.resolve(reference);
		return section && section->sourceConnectionId == connectionId;
	};

	if (facet == "tracks") {
		for (const auto& node : m_sceneModel.nodes)
			if (node.trackId == id)
				add(QString("node '%1'").arg(QString::fromStdString(node.id)));
		for (const auto& arc : m_sceneModel.arcs)
			if (arc.trackId == id)
				add(QString("arc '%1'").arg(QString::fromStdString(arc.id)));
		for (const auto& block : m_sceneModel.blocks)
			if (block.trackId == id)
				add(QString("block '%1'").arg(QString::fromStdString(block.id)));
		for (const auto& area : m_sceneModel.signallingAreas)
			if (area.trackId == id)
				add(QString("signalling area '%1'").arg(QString::fromStdString(area.id)));
	} else if (facet == "nodes") {
		for (const auto& arc : m_sceneModel.arcs)
			if (arc.fromNodeId == id || arc.toNodeId == id)
				add(QString("arc '%1' endpoint").arg(QString::fromStdString(arc.id)));
		for (const auto& connection : m_sceneModel.connections)
			if (connection.fromNodeId == id || connection.toNodeId == id)
				add(QString("connection '%1' endpoint").arg(QString::fromStdString(connection.id)));
		for (const auto& station : m_sceneModel.stations)
			for (const auto& platform : station.platforms)
				if (std::find(platform.nodeIds.begin(), platform.nodeIds.end(), id) != platform.nodeIds.end())
					add(QString("platform '%1' at station '%2'")
						.arg(QString::fromStdString(platform.id), QString::fromStdString(station.id)));
	} else if (facet == "blocks" || facet == "connections") {
		const SceneSectionInventory inventory = buildSceneSectionInventory(m_sceneModel);
		const auto usesSection = [&](const std::string& reference) {
			return facet == "blocks" ? sectionUsesBlock(inventory, reference)
				: sectionUsesConnection(inventory, id, reference);
		};
		for (const auto& route : m_sceneModel.routes)
			for (const auto& token : route.blocks)
				if (usesSection(token)) {
					add(QString("route '%1' section '%2'")
						.arg(QString::fromStdString(route.id), QString::fromStdString(token)));
					break;
				}
		for (const auto& signal : sceneSignals(m_sceneModel))
			if (usesSection(signal.protectedSection))
				add(QString("signal '%1' protected section '%2'")
					.arg(QString::fromStdString(signal.id), QString::fromStdString(signal.protectedSection)));
		for (const auto& scenario : m_sceneModel.scenarios)
			for (const auto& incident : scenario.incidents)
				if (incident.type == "signal_failure" && usesSection(incident.target))
					add(QString("signal-failure incident '%1' in scenario '%2'")
						.arg(QString::fromStdString(incident.id), QString::fromStdString(scenario.id)));
		for (const auto& dependency : m_sceneModel.blockDependencies)
			if (usesSection(dependency.block) || usesSection(dependency.dependsOn))
				add(QString("block dependency '%1' -> '%2'")
					.arg(QString::fromStdString(dependency.block), QString::fromStdString(dependency.dependsOn)));
		for (const auto& restriction : m_sceneModel.singleTrackRestrictions)
			if (usesSection(restriction.startBlock) || usesSection(restriction.endBlock)
				|| usesSection(restriction.protectedStartBlock) || usesSection(restriction.protectedEndBlock))
				add(QString("single-track restriction '%1' -> '%2'")
					.arg(QString::fromStdString(restriction.startBlock), QString::fromStdString(restriction.endBlock)));
		for (const auto& boundary : m_sceneModel.stationBoundaries)
			if (usesSection(boundary.entranceBlock)
				|| (boundary.hasExitBlock && usesSection(boundary.exitBlock)))
				add(QString("station boundary '%1' -> '%2'")
					.arg(QString::fromStdString(boundary.entranceBlock), QString::fromStdString(boundary.exitBlock)));
	} else if (facet == "stations") {
		for (const auto& service : m_sceneModel.services)
			for (const auto& stop : service.stops)
				if (stop.stationId == id)
					add(QString("service '%1' stop").arg(QString::fromStdString(service.id)));
		for (const auto& scenario : m_sceneModel.scenarios)
			for (const auto& delay : scenario.entranceDelays)
				if (delay.stationId == id)
					add(QString("entrance delay for service '%1' in scenario '%2'")
						.arg(QString::fromStdString(delay.serviceId), QString::fromStdString(scenario.id)));
		for (const auto& passenger : m_sceneModel.passengers)
			for (const auto& journey : passenger.journeys) {
				if (journey.originStationId == id || journey.destinationStationId == id)
					add(QString("passenger '%1' journey '%2'")
						.arg(QString::fromStdString(passenger.id), QString::fromStdString(journey.id)));
				for (const auto& leg : journey.legs)
					if (leg.originStationId == id || leg.destinationStationId == id)
						add(QString("passenger '%1' leg '%2'")
							.arg(QString::fromStdString(passenger.id), QString::fromStdString(leg.id)));
			}
	} else if (facet == "platforms") {
		for (const auto& service : m_sceneModel.services)
			for (const auto& stop : service.stops)
				if (stop.stationId == scope && stop.platformId == id)
					add(QString("service '%1' stop at station '%2'")
						.arg(QString::fromStdString(service.id), QString::fromStdString(scope)));
	} else if (facet == "signals") {
		for (const auto& scenario : m_sceneModel.scenarios)
			for (const auto& incident : scenario.incidents)
				if (incident.type == "signal_failure" && incident.target == id)
					add(QString("signal-failure incident '%1' in scenario '%2'")
						.arg(QString::fromStdString(incident.id), QString::fromStdString(scenario.id)));
	} else if (facet == "routes") {
		for (const auto& service : m_sceneModel.services)
			if (service.route == id)
				add(QString("service '%1'").arg(QString::fromStdString(service.id)));
	} else if (facet == "train_unit") {
		for (const auto& composition : m_sceneModel.compositions)
			if (std::find(composition.units.begin(), composition.units.end(), id) != composition.units.end())
				add(QString("composition '%1'").arg(QString::fromStdString(composition.id)));
	} else if (facet == "composition") {
		for (const auto& service : m_sceneModel.services)
			if (service.composition == id)
				add(QString("service '%1'").arg(QString::fromStdString(service.id)));
	} else if (facet == "service") {
		for (const auto& scenario : m_sceneModel.scenarios) {
			for (const auto& incident : scenario.incidents)
				if (incident.type == "train_breakdown" && incident.target == id)
					add(QString("breakdown incident '%1' in scenario '%2'")
						.arg(QString::fromStdString(incident.id), QString::fromStdString(scenario.id)));
			for (const auto& delay : scenario.entranceDelays)
				if (delay.serviceId == id)
					add(QString("entrance delay in scenario '%1'").arg(QString::fromStdString(scenario.id)));
		}
		for (const auto& passenger : m_sceneModel.passengers)
			for (const auto& journey : passenger.journeys)
				for (const auto& leg : journey.legs)
					if (leg.serviceId == id)
						add(QString("passenger '%1' leg '%2'")
							.arg(QString::fromStdString(passenger.id), QString::fromStdString(leg.id)));
	}
	return consumers;
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
			: QStringLiteral("Invalid track: %1").arg(value), value);
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

void MainWindow::refreshInfrastructurePanel() {
	const bool editable = m_sceneLoaded && !m_worker;
	if (m_infrastructureDock)
		m_infrastructureDock->setEnabled(editable);
	if (m_infrastructureFacetCombo) {
		const QSignalBlocker blocker(m_infrastructureFacetCombo);
		m_infrastructureFacetCombo->setEnabled(editable);
	}
	if (m_addInfrastructureButton)
		m_addInfrastructureButton->setEnabled(editable);
	refreshInfrastructureTable();
}

void MainWindow::refreshInfrastructureTable() {
	if (!m_infrastructureTable || !m_infrastructureFacetCombo)
		return;

	const QString facet = m_infrastructureFacetCombo->currentData().toString();
	const int previousRow = m_infrastructureTable->currentRow();
	const QString previousId = m_infrastructureSelectionId;
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
			<< "Curvature radius (m)" << "Gradient (%)" << "Speed limit (m/s)";
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
		headers << "ID" << "Start km" << "End km" << "Level" << "Track ID";
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
			? m_blockTrackFilterCombo->currentData().toString() : QString();
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
			setCell(row, 3, QString::number(area.level));
			setCell(row, 4, QString::fromStdString(area.trackId));
		}
	} else if (facet == "routes") {
		m_infrastructureTable->setRowCount(static_cast<int>(m_sceneModel.routes.size()));
		for (int row = 0; row < m_infrastructureTable->rowCount(); ++row) {
			const SceneRoute& route = m_sceneModel.routes[static_cast<std::size_t>(row)];
			setCell(row, 0, QString::fromStdString(route.id));
			setCell(row, 1, route.blocks.empty()
				? QStringLiteral("0 sections")
				: QStringLiteral("%1 section(s)").arg(static_cast<int>(route.blocks.size())), false);
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
	if (rowToSelect < 0 && m_infrastructureTable->rowCount() > 0)
		rowToSelect = previousRow < 0 ? 0 : std::min(previousRow, m_infrastructureTable->rowCount() - 1);
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
			? m_blockTrackFilterCombo->currentData().toString().toStdString() : std::string();
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
			for (auto& node : m_sceneModel.nodes)
				if (node.trackId == oldId)
					node.trackId = newId;
			for (auto& arc : m_sceneModel.arcs)
				if (arc.trackId == oldId)
					arc.trackId = newId;
			for (auto& block : m_sceneModel.blocks)
				if (block.trackId == oldId)
					block.trackId = newId;
			for (auto& area : m_sceneModel.signallingAreas)
				if (area.trackId == oldId)
					area.trackId = newId;
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
				for (auto& arc : m_sceneModel.arcs) {
					if (arc.fromNodeId == oldId)
						arc.fromNodeId = newId;
					if (arc.toNodeId == oldId)
						arc.toNodeId = newId;
				}
				for (auto& connection : m_sceneModel.connections) {
					if (connection.fromNodeId == oldId)
						connection.fromNodeId = newId;
					if (connection.toNodeId == oldId)
						connection.toNodeId = newId;
				}
				for (auto& station : m_sceneModel.stations)
					for (auto& platform : station.platforms)
						for (auto& nodeId : platform.nodeIds)
							if (nodeId == oldId)
								nodeId = newId;
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
			const auto rewrite = [&oldId, &newId](std::string& reference) {
				reference = rewriteBlockReference(reference, oldId, newId);
			};
			for (auto& route : m_sceneModel.routes)
				for (auto& reference : route.blocks)
					rewrite(reference);
			for (auto& dependency : m_sceneModel.blockDependencies) {
				rewrite(dependency.block);
				rewrite(dependency.dependsOn);
			}
			for (auto& restriction : m_sceneModel.singleTrackRestrictions) {
				rewrite(restriction.startBlock);
				rewrite(restriction.endBlock);
				rewrite(restriction.protectedStartBlock);
				rewrite(restriction.protectedEndBlock);
			}
			for (auto& boundary : m_sceneModel.stationBoundaries) {
				rewrite(boundary.entranceBlock);
				if (boundary.hasExitBlock)
					rewrite(boundary.exitBlock);
			}
			for (auto& scenario : m_sceneModel.scenarios)
				for (auto& incident : scenario.incidents)
					if (incident.type == "signal_failure")
						rewrite(incident.target);
			for (auto& signal : sceneSignals(m_sceneModel))
				if (!signal.protectedSection.empty())
					rewrite(signal.protectedSection);
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
				for (auto& service : m_sceneModel.services)
					for (auto& stop : service.stops)
						if (stop.stationId == oldId)
							stop.stationId = newId;
				for (auto& scenario : m_sceneModel.scenarios)
					for (auto& delay : scenario.entranceDelays)
						if (delay.stationId == oldId)
							delay.stationId = newId;
				for (auto& passenger : m_sceneModel.passengers)
					for (auto& journey : passenger.journeys) {
						if (journey.originStationId == oldId)
							journey.originStationId = newId;
						if (journey.destinationStationId == oldId)
							journey.destinationStationId = newId;
						for (auto& leg : journey.legs) {
							if (leg.originStationId == oldId)
								leg.originStationId = newId;
							if (leg.destinationStationId == oldId)
								leg.destinationStationId = newId;
						}
					}
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
				for (auto& service : m_sceneModel.services)
					for (auto& stop : service.stops)
						if (stop.stationId == sourceStationId && stop.platformId == platformId)
							stop.stationId = targetStationId;
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
				for (auto& service : m_sceneModel.services)
					for (auto& stop : service.stops)
						if (stop.stationId == station.id && stop.platformId == oldId)
							stop.platformId = newId;
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
			for (auto& scenario : m_sceneModel.scenarios)
				for (auto& incident : scenario.incidents)
					if (incident.type == "signal_failure" && incident.target == oldId)
						incident.target = newId;
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
		} else if (column == 3) {
			bool parsed = false;
			const int level = value.toInt(&parsed);
			if (!parsed) {
				refreshInfrastructureTable();
				return;
			}
			if (area.level != level) {
				area.level = level;
				changed = true;
			}
		} else if (column == 4 && area.trackId != value.toStdString()) {
			area.trackId = value.toStdString();
			changed = true;
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
				for (auto& service : m_sceneModel.services)
					if (service.route == oldId)
						service.route = newId;
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
		? &m_sceneModel.passengers[static_cast<std::size_t>(row)] : nullptr;
}

ScenePassengerJourney* MainWindow::selectedPassengerJourney() {
	ScenePassenger* passenger = selectedPassenger();
	const int row = m_passengerJourneyListWidget ? m_passengerJourneyListWidget->currentRow() : -1;
	return passenger && row >= 0 && row < static_cast<int>(passenger->journeys.size())
		? &passenger->journeys[static_cast<std::size_t>(row)] : nullptr;
}

ScenePassengerLeg* MainWindow::selectedPassengerLeg() {
	ScenePassengerJourney* journey = selectedPassengerJourney();
	const int row = m_passengerLegListWidget ? m_passengerLegListWidget->currentRow() : -1;
	return journey && row >= 0 && row < static_cast<int>(journey->legs.size())
		? &journey->legs[static_cast<std::size_t>(row)] : nullptr;
}

std::string MainWindow::uniquePassengerId(const std::string& baseId) const {
	const std::string base = baseId.empty() ? "new_passenger" : baseId;
	std::string candidate = base;
	int suffix = 2;
	while (std::any_of(m_sceneModel.passengers.begin(), m_sceneModel.passengers.end(),
			[&candidate](const ScenePassenger& value) { return value.id == candidate; }))
		candidate = base + "_" + std::to_string(suffix++);
	return candidate;
}

std::string MainWindow::uniquePassengerJourneyId(const std::string& baseId) const {
	const std::string base = baseId.empty() ? "new_journey" : baseId;
	std::string candidate = base;
	int suffix = 2;
	for (;;) {
		bool exists = false;
		for (const auto& passenger : m_sceneModel.passengers)
			for (const auto& journey : passenger.journeys)
				if (journey.id == candidate)
					exists = true;
		if (!exists)
			return candidate;
		candidate = base + "_" + std::to_string(suffix++);
	}
}

std::string MainWindow::uniquePassengerLegId(const std::string& baseId) const {
	const std::string base = baseId.empty() ? "new_leg" : baseId;
	std::string candidate = base;
	int suffix = 2;
	for (;;) {
		bool exists = false;
		for (const auto& passenger : m_sceneModel.passengers)
			for (const auto& journey : passenger.journeys)
				for (const auto& leg : journey.legs)
					if (leg.id == candidate)
						exists = true;
		if (!exists)
			return candidate;
		candidate = base + "_" + std::to_string(suffix++);
	}
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
			? passengerPrefix + QStringLiteral(".journeys[%1]").arg(journeyRow) : QString();
		const QString legPrefix = journey && legRow >= 0
			? journeyPrefix + QStringLiteral(".legs[%1]").arg(legRow) : QString();
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
	if (m_passengerDiagnosticLabel)
		m_passengerDiagnosticLabel->setText(messages.isEmpty()
			? QStringLiteral("No validation messages for this passenger selection.")
			: messages.join(QStringLiteral("\n")));
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
			journey ? journey->originStationId : std::string());
		m_passengerJourneyOriginCombo->setEnabled(enabled);
	}
	if (m_passengerJourneyDestinationCombo) {
		const QSignalBlocker blocker(m_passengerJourneyDestinationCombo);
		populatePassengerStationCombo(m_passengerJourneyDestinationCombo, m_sceneModel,
			journey ? journey->destinationStationId : std::string());
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
			leg ? leg->serviceId : std::string());
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
			leg ? leg->originStationId : std::string(), allowed);
		m_passengerLegOriginCombo->setEnabled(enabled);
	}
	if (m_passengerLegDestinationCombo) {
		const QSignalBlocker blocker(m_passengerLegDestinationCombo);
		populatePassengerStationCombo(m_passengerLegDestinationCombo, m_sceneModel,
			leg ? leg->destinationStationId : std::string(), allowed);
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
	passenger.id = uniquePassengerId("new_passenger");
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
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
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
	journey.id = uniquePassengerJourneyId(passenger->id + ".journey");
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
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
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
	leg.id = uniquePassengerLegId(journey->id + ".leg");
	if (!m_sceneModel.services.empty()) {
		const SceneService& service = m_sceneModel.services.front();
		leg.serviceId = service.id;
		if (!service.stops.empty()) {
			leg.originStationId = service.stops.front().stationId;
			leg.destinationStationId = service.stops.size() > 1
				? service.stops[1].stationId : service.stops.front().stationId;
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
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
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
	QString sourcePath = qEnvironmentVariable("QEGTRAIN_E2E_PASSENGER_SOURCE");
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
		.arg(acceptedPassengers).arg(rejectedPassengers), 7000);
}

void MainWindow::addInfrastructureEntity() {
	if (!m_sceneLoaded || m_worker || !m_infrastructureFacetCombo)
		return;
	const QString facet = m_infrastructureFacetCombo->currentData().toString();
	std::string id;
	int newRow = -1;
	if (facet == "tracks") {
		id = uniqueInfrastructureId("track", facet);
		m_sceneModel.tracks.push_back({id});
		newRow = static_cast<int>(m_sceneModel.tracks.size()) - 1;
	} else if (facet == "nodes") {
		id = uniqueInfrastructureId("node", facet);
		const std::string trackId = m_sceneModel.tracks.empty() ? std::string() : m_sceneModel.tracks.front().id;
		double x = 0.0;
		for (const auto& node : m_sceneModel.nodes)
			if (node.trackId == trackId && std::isfinite(node.xKm))
				x = std::max(x, node.xKm + 1.0);
		m_sceneModel.nodes.push_back({id, trackId, x, 0.0});
		newRow = static_cast<int>(m_sceneModel.nodes.size()) - 1;
	} else if (facet == "arcs") {
		id = uniqueInfrastructureId("arc", facet);
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
		id = uniqueInfrastructureId("block", facet);
		const std::string selectedTrack = m_blockTrackFilterCombo
			? m_blockTrackFilterCombo->currentData().toString().toStdString() : std::string();
		const auto track = std::find_if(m_sceneModel.tracks.begin(), m_sceneModel.tracks.end(),
			[&selectedTrack](const SceneTrack& candidate) { return candidate.id == selectedTrack; });
		if (selectedTrack.empty() || track == m_sceneModel.tracks.end()) {
			statusBar()->showMessage("Select a valid track before adding a block", 4000);
			return;
		}
		m_sceneModel.blocks.push_back({id, track->id, 1.0});
		newRow = static_cast<int>(m_sceneModel.blocks.size()) - 1;
	} else if (facet == "connections") {
		id = uniqueInfrastructureId("connection", facet);
		std::string from;
		std::string to;
		if (m_sceneModel.nodes.size() >= 2) {
			from = m_sceneModel.nodes[0].id;
			to = m_sceneModel.nodes[1].id;
		}
		m_sceneModel.connections.push_back({id, from, to, false, 0.0});
		newRow = static_cast<int>(m_sceneModel.connections.size()) - 1;
	} else if (facet == "stations") {
		id = uniqueInfrastructureId("station", facet);
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
		id = uniqueInfrastructureId("platform", facet);
		m_sceneModel.stations.front().platforms.push_back({id, {}});
		m_infrastructureSelectionId = QString::fromStdString(platformSelectionKey(stationId, id));
	} else if (facet == "signals") {
		id = uniqueInfrastructureId("signal", facet);
		sceneSignals(m_sceneModel).push_back({id});
		newRow = static_cast<int>(sceneSignals(m_sceneModel).size()) - 1;
	} else if (facet == "signalling_areas") {
		id = uniqueInfrastructureId("signalling-area", facet);
		m_sceneModel.signallingAreas.push_back({id, 0.0, 0.0, 0, {}});
		newRow = static_cast<int>(m_sceneModel.signallingAreas.size()) - 1;
	} else if (facet == "routes") {
		id = uniqueInfrastructureId("route", facet);
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
	const std::string id = uniqueInfrastructureId("block", QStringLiteral("blocks"));
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
			.platforms[static_cast<std::size_t>(platformIndex)].id);
	else if (facet == "signals" && row < static_cast<int>(sceneSignals(m_sceneModel).size()))
		id = QString::fromStdString(sceneSignals(m_sceneModel)[static_cast<std::size_t>(row)].id);
	else if (facet == "signalling_areas" && row < static_cast<int>(m_sceneModel.signallingAreas.size()))
		id = QString::fromStdString(m_sceneModel.signallingAreas[static_cast<std::size_t>(row)].id);
	else if (facet == "routes" && row < static_cast<int>(m_sceneModel.routes.size()))
		id = QString::fromStdString(m_sceneModel.routes[static_cast<std::size_t>(row)].id);
	displayId = id.isEmpty() ? QStringLiteral("incomplete row") : id;
	const std::string platformScope = stationIndex >= 0
		? m_sceneModel.stations[static_cast<std::size_t>(stationIndex)].id : std::string();
	const QStringList consumers = directDeleteConsumers(facet, id.toStdString(), platformScope);
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
		m_trainUnitSourceDataEdit->setText(unit ? QString::fromStdString(unit->sourceDataFile) : QString());
		m_trainUnitSourceDataEdit->setEnabled(hasSelection);
	}
	if (m_trainUnitSourceTractionEdit) {
		m_trainUnitSourceTractionEdit->setText(unit ? QString::fromStdString(unit->sourceTractionFile) : QString());
		m_trainUnitSourceTractionEdit->setEnabled(hasSelection);
	}
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
				static_cast<int>(curve.size()) - 1), 0);
	if (m_removeTrainUnitTractionButton)
		m_removeTrainUnitTractionButton->setEnabled(m_trainUnitTractionTable->currentRow() >= 0);
}

std::string MainWindow::uniqueTrainUnitId(const std::string& baseId) const {
	auto idExists = [this](const std::string& id) {
		for (const auto& unit : m_sceneModel.trainUnits) {
			if (unit.id == id)
				return true;
		}
		return false;
	};
	std::string candidate = baseId;
	int suffix = 2;
	while (idExists(candidate))
		candidate = baseId + "_" + std::to_string(suffix++);
	return candidate;
}

void MainWindow::addTrainUnit() {
	if (!m_sceneLoaded)
		return;
	SceneTrainUnit unit;
	unit.id = uniqueTrainUnitId("new_train_unit");
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
	duplicate.id = uniqueTrainUnitId(duplicate.id + "_copy");
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
	const QStringList consumers = directDeleteConsumers(QStringLiteral("train_unit"), id);
	if (!consumers.isEmpty()) {
		showBlockingError(this, "Cannot Delete Train Unit",
			QString("Cannot delete train unit '%1' because it is still referenced by:\n• %2\nRemove it from those compositions first.")
				.arg(QString::fromStdString(id), consumers.join("\n• ")), true);
		return;
	}
	if (QMessageBox::question(this, "Delete Train Unit",
			QString("Delete train unit '%1'?").arg(QString::fromStdString(id)),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
		return;
	m_sceneModel.trainUnits.erase(m_sceneModel.trainUnits.begin() + row);
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
	m_sceneModel.trainUnits[row].id = newId;
	for (auto& composition : m_sceneModel.compositions) {
		for (auto& unitId : composition.units) {
			if (unitId == oldId)
				unitId = newId;
		}
	}
	if (QListWidgetItem* item = m_trainUnitListWidget->item(row)) {
		const QSignalBlocker blocker(m_trainUnitListWidget);
		item->setText(QString::fromStdString(newId));
	}
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
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

const SceneTrainUnit* MainWindow::trainUnitById(const std::string& id) const {
	for (const SceneTrainUnit& unit : m_sceneModel.trainUnits) {
		if (unit.id == id)
			return &unit;
	}
	return nullptr;
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
		unit = trainUnitById(m_compositionUnitsListWidget->item(unitRow)->text().toStdString());

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
	const SceneTrainUnit* unit = trainUnitById(unitId);
	if (!unit || unit->tractionCurve.empty()) {
		QMessageBox::information(this, "No Traction Data",
								 "The selected unit has no traction curve to plot.");
		return;
	}
	plotTrainUnitTraction(*unit);
}

void MainWindow::plotTrainUnitTraction(const SceneTrainUnit& unit) {
	const auto samples = sampleTractionCurve(unit.tractionCurve);
	QChart* chart = new QChart();
	chart->setTitle(QString("Input traction characteristic: %1").arg(QString::fromStdString(unit.id)));
	QLineSeries* series = new QLineSeries();
	series->setName(QString::fromStdString(unit.id));
	series->setProperty("trainId", QString::fromStdString(unit.id));
	for (const auto& point : samples)
		series->append(point.first * 3.6, point.second / 1000.0);
	chart->addSeries(series);
	chart->createDefaultAxes();
	if (!chart->axes(Qt::Horizontal).isEmpty())
		chart->axes(Qt::Horizontal).first()->setTitleText("Speed (km/h)");
	if (!chart->axes(Qt::Vertical).isEmpty())
		chart->axes(Qt::Vertical).first()->setTitleText("Tractive effort (kN)");

	QString title = QString("Input traction characteristic: %1").arg(QString::fromStdString(unit.id));
	if (!unit.sourceTractionFile.empty())
		title += QString("  (%1)").arg(QString::fromStdString(unit.sourceTractionFile));
	DiagramWindow* win = new DiagramWindow(title, this);
	win->setChart(chart);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
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
	duplicate.id = uniqueCompositionId(duplicate.id + "_copy");
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
	const QStringList consumers = directDeleteConsumers(QStringLiteral("composition"), id);
	if (!consumers.isEmpty()) {
		showBlockingError(this, "Cannot Delete Composition",
			QString("Cannot delete composition '%1' because it is still referenced by:\n• %2\nReassign those services first.")
				.arg(QString::fromStdString(id), consumers.join("\n• ")), true);
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
	const bool duplicate = newId.empty() || uniqueCompositionId(newId) != newId;
	if (duplicate) {
		m_compositionIdEdit->setText(QString::fromStdString(oldId));
		return;
	}

	m_sceneModel.compositions[row].id = newId;
	for (auto& service : m_sceneModel.services) {
		if (service.composition == oldId)
			service.composition = newId;
	}
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
		for (const auto& route : m_sceneModel.routes)
			m_serviceRouteCombo->addItem(QString::fromStdString(route.id));
		if (hasSelection) {
			QString currentRoute = QString::fromStdString(m_sceneModel.services[row].route);
			if (m_serviceRouteCombo->findText(currentRoute) < 0)
				m_serviceRouteCombo->addItem(currentRoute); // dangling reference, still shown/selectable
			m_serviceRouteCombo->setCurrentText(currentRoute);
		}
		m_serviceRouteCombo->setEnabled(editorAvailable);
	}

	if (m_serviceThroughCheck) {
		const QSignalBlocker blocker(m_serviceThroughCheck);
		m_serviceThroughCheck->setChecked(hasSelection && m_sceneModel.services[row].through);
		m_serviceThroughCheck->setEnabled(editorAvailable);
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
			? std::clamp(m_sceneModel.services[row].performancePercent, 1.0, 100.0) : 100.0);
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
			? std::max(0.1, m_sceneModel.services[row].maximumSpeedKmh) : 100.0);
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

double MainWindow::serviceOccurrenceDuration() const {
	if (initial_variables.durationOverride)
		return initial_variables.times;
	return m_sceneModel.settings.hasDuration
		? m_sceneModel.settings.durationSeconds : computeHorizon(m_sceneModel);
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
			m_serviceOccurrenceTable->setItem(row, 1,
				new QTableWidgetItem(QString::fromStdString(sceneServiceOccurrenceOperatingCode(service, occurrence))));
			m_serviceOccurrenceTable->setItem(row, 2,
				new QTableWidgetItem(QString("%1 / %2").arg(QString::fromStdString(service.id)).arg(occurrence)));
			double offset = 0.0;
			if (service.hasRepeat && service.headwaySeconds > 0.0)
				offset = service.headwaySeconds * static_cast<double>(occurrence - 1);
			QString context = QString("+%1 s").arg(QString::number(offset, 'f', 0));
			if (!service.stops.empty() && service.stops.front().hasPlannedDeparture) {
				context = QString("Departure %1 (+%2 s)")
					.arg(QString::fromStdString(formatSimTime(
						static_cast<long long>(service.stops.front().plannedDepartureSeconds + offset),
						m_startOffsetSeconds)))
					.arg(QString::number(offset, 'f', 0));
			} else if (service.hasEntryTime) {
				context = QString("Entry %1 (+%2 s)")
					.arg(QString::fromStdString(formatSimTime(
						static_cast<long long>(service.entryTimeSeconds + offset), m_startOffsetSeconds)))
					.arg(QString::number(offset, 'f', 0));
			}
			m_serviceOccurrenceTable->setItem(row, 3, new QTableWidgetItem(context));
			m_serviceOccurrenceTable->setItem(row, 4,
				new QTableWidgetItem(QString::number(static_cast<double>(service.performancePercent), 'g', 6)));
			m_serviceOccurrenceTable->setItem(row, 5, new QTableWidgetItem(service.hasMaximumSpeed
				? QString::number(service.maximumSpeedKmh, 'g', 6) : QStringLiteral("-")));
		}
		if (row >= displayedOccurrences)
			break;
	}
	m_serviceOccurrenceTable->resizeColumnsToContents();
	m_updatingServiceOccurrencePreview = false;
	if (m_serviceOccurrenceSelectionLabel) {
		if (totalOccurrences > Max_N_Reg)
			m_serviceOccurrenceSelectionLabel->setText(QString("%1 occurrences; first %2 shown. Reduce the pattern to select a subset.")
				.arg(totalOccurrences).arg(Max_N_Reg));
		else
			m_serviceOccurrenceSelectionLabel->setText(QString("%1/%2 occurrences selected")
				.arg(selectedServiceOccurrences()).arg(totalOccurrences));
	}
	const bool controlsEnabled = m_sceneLoaded && !m_worker && totalOccurrences <= Max_N_Reg;
	if (m_serviceOccurrenceTable)
		m_serviceOccurrenceTable->setEnabled(controlsEnabled);
	if (m_selectAllOccurrencesButton)
		m_selectAllOccurrencesButton->setEnabled(controlsEnabled);
	if (m_selectNoneOccurrencesButton)
		m_selectNoneOccurrencesButton->setEnabled(controlsEnabled);
	refreshEntranceDelayPanel();
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
	if (m_serviceOccurrenceSelectionLabel)
		m_serviceOccurrenceSelectionLabel->setText(QString("%1/%2 occurrences selected")
			.arg(selectedServiceOccurrences()).arg(totalServiceOccurrences()));
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
	service.id = uniqueServiceId("new_service");
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
	duplicate.id = uniqueServiceId(duplicate.id + "_copy");
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
	const QStringList consumers = directDeleteConsumers(QStringLiteral("service"), id);
	if (!consumers.isEmpty()) {
		showBlockingError(this, "Cannot Delete Service",
			QString("Cannot delete service '%1' because it is still referenced by:\n• %2\nRemove those operation references first.")
				.arg(QString::fromStdString(id), consumers.join("\n• ")), true);
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
	if (uniqueServiceId(newId) != newId) {
		const QSignalBlocker blocker(m_serviceIdEdit);
		m_serviceIdEdit->setText(QString::fromStdString(oldId));
		showBlockingError(this, "Service ID already exists", "Choose a unique service ID.", true);
		return;
	}
	for (auto& scenario : m_sceneModel.scenarios) {
		for (auto& delay : scenario.entranceDelays)
			if (delay.serviceId == oldId)
				delay.serviceId = newId;
		for (auto& incident : scenario.incidents)
			if (incident.type == "train_breakdown" && incident.target == oldId)
				incident.target = newId;
	}
	for (auto& passenger : m_sceneModel.passengers)
		for (auto& journey : passenger.journeys)
			for (auto& leg : journey.legs)
				if (leg.serviceId == oldId)
					leg.serviceId = newId;
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
	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
}

void MainWindow::commitServiceThrough(bool checked) {
	if (!m_sceneLoaded || !m_serviceListWidget)
		return;
	const int row = m_serviceListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(m_sceneModel.services.size()))
		return;
	if (checked == m_sceneModel.services[row].through)
		return;
	m_sceneModel.services[row].through = checked;
	markSceneDirty();
	refreshValidationPanel();
	refreshServiceOccurrencePreview();
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
	refreshEntranceDelayPanel();
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

	bool hasPlannedArrival = hasSelection && stop.hasPlannedArrival;
	if (m_stopHasArrivalCheck) {
		const QSignalBlocker blocker(m_stopHasArrivalCheck);
		m_stopHasArrivalCheck->setChecked(hasPlannedArrival);
		m_stopHasArrivalCheck->setEnabled(hasSelection);
	}
	if (m_stopArrivalSecondsEdit) {
		const QSignalBlocker blocker(m_stopArrivalSecondsEdit);
		int seconds = hasSelection ? static_cast<int>(stop.plannedArrivalSeconds) : 0;
		m_stopArrivalSecondsEdit->setText(QString::number(seconds));
		m_stopArrivalSecondsEdit->setEnabled(hasPlannedArrival);
	}

	bool hasPlannedDeparture = hasSelection && stop.hasPlannedDeparture;
	if (m_stopHasDepartureCheck) {
		const QSignalBlocker blocker(m_stopHasDepartureCheck);
		m_stopHasDepartureCheck->setChecked(hasPlannedDeparture);
		m_stopHasDepartureCheck->setEnabled(hasSelection);
	}
	if (m_stopDepartureSecondsEdit) {
		const QSignalBlocker blocker(m_stopDepartureSecondsEdit);
		int seconds = hasSelection ? static_cast<int>(stop.plannedDepartureSeconds) : 0;
		m_stopDepartureSecondsEdit->setText(QString::number(seconds));
		m_stopDepartureSecondsEdit->setEnabled(hasPlannedDeparture);
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

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshStopList();

	if (m_stopListWidget)
		m_stopListWidget->setCurrentRow(static_cast<int>(m_sceneModel.services[serviceRow].stops.size()) - 1);
	refreshValidationPanel();
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

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshStopList();

	if (m_stopListWidget) {
		int remaining = m_stopListWidget->count();
		if (remaining > 0)
			m_stopListWidget->setCurrentRow(stopRow < remaining ? stopRow : remaining - 1);
	}
	refreshValidationPanel();
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

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshStopList();

	if (m_stopListWidget)
		m_stopListWidget->setCurrentRow(stopRow - 1);
	refreshValidationPanel();
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

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshStopList();

	if (m_stopListWidget)
		m_stopListWidget->setCurrentRow(stopRow + 1);
	refreshValidationPanel();
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

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();

	// the station changed, so rebuild only its platform combo; rebuilding the
	// station combo here would mean clearing it from inside its own signal
	refreshStopPlatformCombo();
	refreshEntranceDelayPanel();
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

	markSceneDirty();
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
	if (checked == stops[stopRow].hasPlannedArrival)
		return;

	stops[stopRow].hasPlannedArrival = checked;
	if (m_stopArrivalSecondsEdit)
		m_stopArrivalSecondsEdit->setEnabled(checked);

	markSceneDirty();
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
	if (checked == stops[stopRow].hasPlannedDeparture)
		return;

	stops[stopRow].hasPlannedDeparture = checked;
	if (m_stopDepartureSecondsEdit)
		m_stopDepartureSecondsEdit->setEnabled(checked);

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
	refreshValidationPanel();
	refreshEntranceDelayPanel();
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
	if (newValue == stops[stopRow].plannedArrivalSeconds)
		return;

	stops[stopRow].plannedArrivalSeconds = newValue;

	markSceneDirty();
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
	if (newValue == stops[stopRow].plannedDepartureSeconds)
		return;

	stops[stopRow].plannedDepartureSeconds = newValue;

	markSceneDirty();
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

	markSceneDirty();
	updateSceneWindowTitle();
	updateSceneActions();
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
		? &scenario->entranceDelays[static_cast<std::size_t>(row)] : nullptr;
}

QString MainWindow::scenarioContext() const {
	if (!m_appliedScenarioId.empty())
		return QString::fromStdString(m_appliedScenarioId);
	const SceneScenario* scenario = selectedScenario();
	return scenario ? QString::fromStdString(scenario->id) : QStringLiteral("(none)");
}

std::string MainWindow::uniqueScenarioId(const std::string& baseId) const {
	const std::string base = baseId.empty() ? "scenario" : baseId;
	std::string candidate = base;
	int suffix = 2;
	for (;;) {
		bool used = false;
		for (const auto& scenario : m_sceneModel.scenarios) {
			if (scenario.id == candidate) {
				used = true;
				break;
			}
		}
		if (!used)
			return candidate;
		candidate = base + "_" + std::to_string(suffix++);
	}
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
		for (std::size_t index = 0; hasScene && index < m_sceneModel.scenarios.size(); ++index) {
			const SceneScenario& scenario = m_sceneModel.scenarios[index];
			const QString description = scenario.description.empty()
				? QStringLiteral("(no description)") : QString::fromStdString(scenario.description);
			QString label = QString("%1 — %2 | %3 | %4 incident(s) | %5 delay(s) | %6")
				.arg(QString::fromStdString(scenario.id))
				.arg(QString::fromStdString(scenario.name))
				.arg(description)
				.arg(static_cast<int>(scenario.incidents.size()))
				.arg(static_cast<int>(scenario.entranceDelays.size()))
				.arg(validationStatus(index));
			if (scenario.id == m_sceneModel.defaultScenarioId)
				label += " | default";
			if (m_modifiedScenarioIds.count(scenario.id) > 0)
				label += " | modified";
			auto* item = new QListWidgetItem(label, m_scenarioListWidget);
			item->setToolTip(label);
			if (scenario.id == m_selectedScenarioId)
				rowToSelect = static_cast<int>(index);
		}
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
					label += QString(" | code %1").arg(code.empty()
						? QStringLiteral("(unavailable)") : QString::fromStdString(code));
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
			m_entranceDelayServiceCombo->addItem(QString::fromStdString(candidate.id),
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
		? std::max(1, sceneServiceOccurrenceCount(*service, serviceOccurrenceDuration())) : 1;
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
				const std::string code = sceneServiceOccurrenceOperatingCode(*service, delay->occurrence);
				context += QString(" | Generated operating code: %1")
					.arg(code.empty() ? QStringLiteral("(unavailable)") : QString::fromStdString(code));
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
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
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
	scenario.id = uniqueScenarioId("new_scenario");
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
	copy.id = uniqueScenarioId(copy.id + "_copy");
	copy.name = "Copy of " + copy.name;
	for (auto& incident : copy.incidents)
		incident.id = uniqueIncidentId(incident.id + "_copy");
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
	const std::string uniqueId = uniqueScenarioId(imported.id);
	const bool scenarioConflict = uniqueId != imported.id;
	if (scenarioConflict)
		imported.id = uniqueId;
	int adjustedIncidentCount = 0;
	for (auto& incident : imported.incidents) {
		const std::string uniqueIncident = uniqueIncidentId(incident.id);
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
	if (uniqueScenarioId(newId) != newId) {
		showBlockingError(this, "Scenario ID already exists", "Choose a unique scenario ID.", true);
		updateScenarioDetailPanel();
		return;
	}
	const std::string oldId = scenario->id;
	scenario->id = newId;
	if (m_sceneModel.defaultScenarioId == oldId)
		m_sceneModel.defaultScenarioId = newId;
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
		const bool hasEnd = hasSelection && (incidents[static_cast<std::size_t>(row)].hasEndSeconds
			|| incidents[static_cast<std::size_t>(row)].endSeconds != 0.0);
		m_incidentHasEndSecondsCheck->setChecked(hasEnd);
		m_incidentHasEndSecondsCheck->setEnabled(hasSelection);
	}
	if (m_incidentHasOccurrenceCheck) {
		const QSignalBlocker blocker(m_incidentHasOccurrenceCheck);
		const bool hasOccurrence = isBreakdown && (incidents[static_cast<std::size_t>(row)].hasOccurrence
			|| incidents[static_cast<std::size_t>(row)].occurrence != 1);
		m_incidentHasOccurrenceCheck->setChecked(hasOccurrence);
		m_incidentHasOccurrenceCheck->setEnabled(isBreakdown);
	}
	if (m_incidentOccurrenceEdit) {
		const QSignalBlocker blocker(m_incidentOccurrenceEdit);
		const int occurrence = hasSelection ? std::max(1, incidents[static_cast<std::size_t>(row)].occurrence) : 1;
		m_incidentOccurrenceEdit->setText(QString::number(occurrence));
		m_incidentOccurrenceEdit->setEnabled(isBreakdown
			&& m_incidentHasOccurrenceCheck && m_incidentHasOccurrenceCheck->isChecked());
	}
	if (m_incidentHasReducedSpeedCheck) {
		const QSignalBlocker blocker(m_incidentHasReducedSpeedCheck);
		const bool hasCap = isBreakdown && (incidents[static_cast<std::size_t>(row)].hasReducedSpeed
			|| incidents[static_cast<std::size_t>(row)].reducedSpeedKmh != 0.0);
		m_incidentHasReducedSpeedCheck->setChecked(hasCap);
		m_incidentHasReducedSpeedCheck->setEnabled(isBreakdown);
	}
	if (m_incidentReducedSpeedKmhEdit) {
		const QSignalBlocker blocker(m_incidentReducedSpeedKmhEdit);
		const double cap = hasSelection && std::isfinite(incidents[static_cast<std::size_t>(row)].reducedSpeedKmh)
			&& incidents[static_cast<std::size_t>(row)].reducedSpeedKmh > 0.0
			? incidents[static_cast<std::size_t>(row)].reducedSpeedKmh : 40.0;
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

	const auto& incidents = selectedScenarioIncidents();
	int row = m_incidentListWidget ? m_incidentListWidget->currentRow() : -1;
	bool hasSelection = m_sceneLoaded && !m_worker && row >= 0
		&& row < static_cast<int>(incidents.size());

	const QSignalBlocker blocker(m_incidentTargetCombo);
	m_incidentTargetCombo->clear();
	m_incidentTargetCombo->addItem(QString()); // blank choice: no target

	if (hasSelection) {
		const SceneIncident& incident = incidents[row];
		// which pool of ids to offer depends on the current type
		QString typeText = m_incidentTypeCombo ? m_incidentTypeCombo->currentText() : QString();
		if (typeText == "signal_failure") {
			for (const auto& target : signalFailureTargets(m_sceneModel))
				m_incidentTargetCombo->addItem(QString::fromStdString(target));
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
		for (const auto& scenario : m_sceneModel.scenarios) {
			for (const auto& incident : scenario.incidents) {
				if (incident.id == id)
					return true;
			}
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

	auto& incidents = selectedScenarioIncidents();
	SceneIncident incident;
	incident.id = uniqueIncidentId("new_incident");
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
	duplicate.id = uniqueIncidentId(duplicate.id + "_copy");
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
	if (uniqueIncidentId(newId) != newId) {
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

void MainWindow::commitIncidentTarget(const QString& text) {
	if (!m_sceneLoaded || !m_incidentListWidget)
		return;
	auto& incidents = selectedScenarioIncidents();
	int row = m_incidentListWidget->currentRow();
	if (row < 0 || row >= static_cast<int>(incidents.size()))
		return;

	std::string newTarget = text.toStdString();
	if (newTarget == incidents[row].target)
		return;

	incidents[row].target = newTarget;

	// the combo already shows the chosen value; no panel rebuild needed
	markScenarioModified();
	refreshValidationPanel();
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

void MainWindow::commitIncidentOccurrence() {
	if (!m_sceneLoaded || !m_incidentOccurrenceEdit)
		return;
	SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	bool ok = false;
	int occurrence = m_incidentOccurrenceEdit->text().toInt(&ok);
	if (!ok || occurrence < 1)
		occurrence = 1;
	{
		const QSignalBlocker blocker(m_incidentOccurrenceEdit);
		m_incidentOccurrenceEdit->setText(QString::number(occurrence));
	}
	if (incident->occurrence == occurrence && incident->hasOccurrence)
		return;
	incident->occurrence = occurrence;
	incident->hasOccurrence = true;
	markScenarioModified();
	refreshValidationPanel();
	refreshIncidentPanel();
}

void MainWindow::commitIncidentHasOccurrence(bool checked) {
	if (!m_sceneLoaded)
		return;
	SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	if (incident->hasOccurrence == checked)
		return;
	incident->hasOccurrence = checked;
	if (!checked)
		incident->occurrence = 1;
	markScenarioModified();
	refreshValidationPanel();
	updateIncidentDetailPanel();
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
	QStringList recent = settings.value(kRecentScenesKey).toStringList();
	QStringList cleanedRecent;
	bool pruned = false;
	for (const QString& path : recent) {
		const QFileInfo info(path);
		const bool exists = info.isFile()
			? info.exists() && info.suffix().compare("egscene", Qt::CaseInsensitive) == 0
			: QFileInfo(QDir(path).filePath("scene.json")).exists();
		if (!exists) {
			pruned = true;
		} else {
			cleanedRecent.append(path);
		}
	}
	if (pruned) {
		settings.setValue(kRecentScenesKey, cleanedRecent);
		recent = cleanedRecent;
	}
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
		QAction* action = m_recentScenesMenu->addAction(label);
		action->setData(path);
		action->setStatusTip(path);
		action->setToolTip(path);
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
	if (m_followTrainCombo->count() == 0)
		m_followTrainCombo->addItem("No trains to follow", -1);

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
	updateViewportOverlays();
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

void MainWindow::setFollowTrain(int trainIndex) {
	if (trainIndex < 0) {
		if (m_followAction) {
			const QSignalBlocker blocker(m_followAction);
			m_followAction->setChecked(false);
		}
		m_followTrainIndex = -1;
		updateViewportOverlays();
		return;
	}
	if (!resolveTrainItem(trainIndex))
		return;
	if (m_followTrainCombo) {
		const int comboIndex = m_followTrainCombo->findData(trainIndex);
		if (comboIndex >= 0) {
			const QSignalBlocker blocker(m_followTrainCombo);
			m_followTrainCombo->setCurrentIndex(comboIndex);
		}
	}
	if (m_followAction) {
		const QSignalBlocker blocker(m_followAction);
		m_followAction->setChecked(true);
	}
	m_followTrainIndex = trainIndex;
	updateViewportOverlays();
}

void MainWindow::showSceneContextMenu(QGraphicsItem* item, const QPointF& scenePos, const QPoint& screenPos, bool keyboard) {
	Q_UNUSED(scenePos);
	Q_UNUSED(keyboard);
	if (m_sceneContextMenu) {
		m_sceneContextMenu->close();
		m_sceneContextMenu.clear();
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
		details->setIcon(QIcon(classifyStation().iconResource));
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
		details->setIcon(QIcon(classifySignalAspect(signal->aspectCode()).iconResource));
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
					.arg(track).arg(position, 0, 'g', std::numeric_limits<double>::max_digits10)
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

void MainWindow::runStationOverlayE2E() {
	if (m_e2eFinished)
		return;
	const bool waitingForAutostart = qEnvironmentVariableIsSet("QEGTRAIN_AUTOSTART") && !m_snapshot;
	if ((!m_sceneLoaded || !networkView || m_stationOverlays.isEmpty() || waitingForAutostart)
			&& m_e2eAttempts < 120) {
		++m_e2eAttempts;
		QTimer::singleShot(500, this, &MainWindow::runStationOverlayE2E);
		return;
	}
	m_e2eFinished = true;

	bool ok = true;
	QStringList failures;
	const auto marker = [](const QString& value) {
		std::fprintf(stdout, "%s\n", value.toStdString().c_str());
		std::fflush(stdout);
	};
	const auto fail = [&](const QString& value) {
		ok = false;
		failures << value;
	};
	const auto finish = [this](int code) {
		if (m_worker && m_workerThread && m_workerThread->isRunning()) {
			connect(m_workerThread, &QThread::finished, qApp, [code]() {
				QCoreApplication::exit(code);
			});
			m_worker->requestStop();
		} else {
			QCoreApplication::exit(code);
		}
	};
	const QString caseName = QString::fromStdString(m_sceneModel.name);
	const QString stationScreenshotBase = qEnvironmentVariable("QEGTRAIN_E2E_STATION_SCREENSHOT_BASE");
	if (!networkView || m_stationOverlays.isEmpty()) {
		fail("station overlay scene is empty");
	} else {
		resize(1200, 800);
		QApplication::processEvents();
		const QRectF topologyBounds = networkView->topologyBounds();
		const auto checkZoom = [&](qreal ratio, const char* label) {
			fitView();
			if (ratio > 1.0)
				networkView->zoomBy(ratio);
			updateViewportOverlays();
			QApplication::processEvents();
			if (qAbs(networkView->zoomRatio() - ratio) > 1e-5)
				fail(QString("%1 zoom did not settle").arg(label));
			if (networkView->topologyBounds() != topologyBounds)
				fail(QString("%1 changed topology-only fit bounds").arg(label));
			if (ratio <= 1.0 && m_stationOverlays.first()->labelScale() > 1.01)
				fail(QString("%1 station labels grew at overview zoom").arg(label));
			if (ratio >= 12.0 && m_stationOverlays.first()->labelScale() < 1.9)
				fail(QString("%1 station labels did not grow at detail zoom").arg(label));
			const QRectF inset = networkView->viewport()->rect().adjusted(kOverlayMargin, kOverlayMargin,
				-kOverlayMargin, -kOverlayMargin);
			QList<QRectF> symbols;
			int visibleSymbols = 0;
			for (auto* overlay : m_stationOverlays) {
				if (!overlay || !overlay->isVisible())
					continue;
				const QPointF anchor = networkView->viewportTransform().map(overlay->stableAnchor())
					+ overlay->viewportOffset();
				const QRectF symbolRect = overlay->symbolRect().translated(anchor);
				symbols.append(symbolRect);
				if (inset.intersects(symbolRect))
					++visibleSymbols;
			}
			QList<QRectF> labels;
			int importantLabels = 0;
			int collisionBlockedLabels = 0;
			int visibleImportantLabels = 0;
			for (auto* overlay : m_stationOverlays) {
				if (!overlay || !overlay->isVisible())
					continue;
				if (overlay->isInterchange() || overlay->isEndpoint())
					++importantLabels;
				if (overlay->isCollisionBlocked())
					++collisionBlockedLabels;
				if (!overlay->isLabelVisible())
					continue;
				if (overlay->isInterchange() || overlay->isEndpoint())
					++visibleImportantLabels;
				const QPointF anchor = networkView->viewportTransform().map(overlay->stableAnchor())
					+ overlay->viewportOffset();
				const QRectF labelRect = overlay->labelRect().translated(anchor);
				if (!inset.adjusted(-1.0, -1.0, 1.0, 1.0).contains(labelRect))
					fail(QString("%1 label clipped: %2").arg(label).arg(overlay->stationName()));
				for (const QRectF& otherSymbol : symbols)
					if (labelRect.intersects(otherSymbol))
						fail(QString("%1 label intersects symbol: %2").arg(label).arg(overlay->stationName()));
				for (const QRectF& other : labels)
					if (labelRect.intersects(other))
						fail(QString("%1 labels intersect: %2").arg(label).arg(overlay->stationName()));
				labels.append(labelRect);
			}
			if (labels.isEmpty() && visibleSymbols > 0)
				fail(QString("%1 has no visible station label (%2 collision-blocked)")
					.arg(QString::fromLatin1(label)).arg(collisionBlockedLabels));
			if (QString::fromLatin1(label) == QLatin1String("FIT")
				&& importantLabels > 0 && visibleImportantLabels == 0)
				fail(QString("FIT has no visible priority label (%1 important, %2 collision-blocked)")
					.arg(importantLabels).arg(collisionBlockedLabels));
			if (!stationScreenshotBase.isEmpty()) {
				const QString path = QString("%1-%2.png")
					.arg(stationScreenshotBase, QString::fromLatin1(label).toLower());
				if (!networkView->grab().save(path))
					fail(QString("%1 station screenshot save failed").arg(label));
				else
					marker(QString("E2E_STATION_OVERLAY_SCREENSHOT_%1").arg(path));
			}
			const auto signalItem = std::find_if(m_signalDecorations.cbegin(),
				m_signalDecorations.cend(), [](QGraphicsItem* item) {
					return item && qgraphicsitem_cast<SignalItem*>(item);
				});
			const auto hasVisibleSignal = [this]() {
				return std::any_of(m_signalDecorations.cbegin(), m_signalDecorations.cend(),
					[](QGraphicsItem* item) {
						return item && qgraphicsitem_cast<SignalItem*>(item) && item->isVisible();
					});
			};
			const QPointF previousCenter = networkView->mapToScene(
				networkView->viewport()->rect().center());
			bool centeredOnSignal = false;
			if (signalItem == m_signalDecorations.cend()) {
				fail(QString("%1 has no SignalItem").arg(label));
			} else if (!hasVisibleSignal() && ratio > 1.0) {
				networkView->centerOn((*signalItem)->scenePos());
				updateViewportOverlays();
				QApplication::processEvents();
				centeredOnSignal = true;
			}
			if (!hasVisibleSignal())
				fail(QString("%1 has no visible SignalItem").arg(label));
			if (centeredOnSignal) {
				networkView->centerOn(previousCenter);
				updateViewportOverlays();
				QApplication::processEvents();
			}
			marker(QString("E2E_STATION_OVERLAY_%1_%2_OK").arg(caseName, label));
		};

		checkZoom(1.0, "FIT");
		const bool hasTopologyPriority = std::any_of(m_stationOverlays.cbegin(), m_stationOverlays.cend(),
			[](const auto* overlay) { return overlay && (overlay->isInterchange() || overlay->isEndpoint()); });
		if (!hasTopologyPriority && m_stationOverlays.size() > 1) {
			const QString previousSelectedStationName = m_selectedStationName;
			StationOverlayItem* selectedOverlay = nullptr;
			for (auto* overlay : m_stationOverlays) {
				if (overlay && overlay->isVisible()) {
					selectedOverlay = overlay;
					break;
				}
			}
			if (selectedOverlay) {
				m_selectedStationName = selectedOverlay->stationName();
				updateViewportOverlays();
				const bool ordinaryLabelVisible = std::any_of(m_stationOverlays.cbegin(), m_stationOverlays.cend(),
					[selectedOverlay](const auto* overlay) {
						return overlay && overlay != selectedOverlay && overlay->isLabelVisible()
							&& !overlay->isInterchange() && !overlay->isEndpoint();
					});
				if (!ordinaryLabelVisible)
					fail("FIT selected station suppressed every ordinary fallback label");
				m_selectedStationName = previousSelectedStationName;
				updateViewportOverlays();
			}
		}
		StationOverlayItem* hovered = nullptr;
		for (auto* overlay : m_stationOverlays) {
			if (overlay && overlay->isVisible()) {
				hovered = overlay;
				break;
			}
		}
		if (!hovered) {
			fail("no overlay available for hover dispatch");
		} else {
			hovered->setLayoutVisible(false);
			QGraphicsSceneHoverEvent enter(QEvent::GraphicsSceneHoverEnter);
			enter.setPos(QPointF());
			enter.setScenePos(hovered->stableAnchor());
			scene->sendEvent(hovered, &enter);
			if (!hovered->isLabelVisible())
				fail("hover did not reveal culled station label");
			QGraphicsSceneHoverEvent leave(QEvent::GraphicsSceneHoverLeave);
			leave.setPos(QPointF());
			leave.setScenePos(hovered->stableAnchor());
			scene->sendEvent(hovered, &leave);
			if (hovered->isLabelVisible())
				fail("hover leave did not hide culled station label");
		}

		checkZoom(3.0, "3X");
		checkZoom(12.0, "12X");
		const qreal devicePixelRatio = windowHandle() ? windowHandle()->devicePixelRatio() : 1.0;
		marker(QString("E2E_STATION_OVERLAY_DPR_%1").arg(devicePixelRatio, 0, 'f', 1));

		const QString previousSelectedStationName = m_selectedStationName;
		const QList<StationOverlayItem*> previousStationOverlays = m_stationOverlays;
		const QList<QGraphicsItem*> previousStationDecorations = m_stationDecorations;
		QList<QGraphicsItem*> previouslyVisibleItems;
		for (auto* item : scene->items()) {
			if (item && item->isVisible()) {
				previouslyVisibleItems.append(item);
				if (item->data(kSignalDecorationRole).toBool())
					item->setData(kSignalBaseVisibleRole, false);
				item->setVisible(false);
			}
		}

		Node semanticFixtureNode;
		semanticFixtureNode.ID = -241.0;
		semanticFixtureNode.station = true;
		semanticFixtureNode.stationName = "E2ESemanticStation";
		semanticFixtureNode.stationPlatformId = "E2EPlatform";
		const QPointF semanticCenter = networkView->mapToScene(networkView->viewport()->rect().center());
		const StationVisual semanticVisual = classifyStation();
		auto* stationTarget = new StationNodeItem(QRectF(-12.0, -12.0, 24.0, 24.0));
		stationTarget->track = -1;
		stationTarget->node = &semanticFixtureNode;
		stationTarget->setPos(semanticCenter);
		QPen stationPen(semanticVisual.outline);
		stationPen.setWidth(0);
		stationPen.setCosmetic(true);
		stationTarget->setPen(stationPen);
		stationTarget->setBrush(semanticVisual.fill);
		auto* overlappingOverlay = new StationOverlayItem(
			QString::fromStdString(semanticFixtureNode.stationName), semanticCenter, semanticVisual);
		scene->addItem(stationTarget);
		scene->addItem(overlappingOverlay);
		m_stationOverlays.clear();
		m_stationDecorations.clear();
		m_stationOverlays.append(overlappingOverlay);
		m_stationDecorations.append(overlappingOverlay);
		if (stationTarget->sceneBoundingRect().center() != overlappingOverlay->stableAnchor())
			fail("semantic fixture station and overlay anchors diverged");

		overlappingOverlay->setLayoutVisible(false);
		overlappingOverlay->setCollisionBlocked(false);
		if (overlappingOverlay->isLabelVisible())
			fail("semantic dispatch overlay was not culled before click");

		QPointF semanticPoint = stationTarget->sceneBoundingRect().center();
		bool foundSemanticPoint = false;
		const QRectF stationBounds = stationTarget->sceneBoundingRect();
		for (int y = 0; y <= 4 && !foundSemanticPoint; ++y) {
			for (int x = 0; x <= 4 && !foundSemanticPoint; ++x) {
				const QPointF point(stationBounds.left() + stationBounds.width() * x / 4.0,
					stationBounds.top() + stationBounds.height() * y / 4.0);
				if (!overlappingOverlay->contains(overlappingOverlay->mapFromScene(point)))
					continue;
				QGraphicsItem* semantic = nullptr;
				for (auto* item : scene->items(point, Qt::IntersectsItemShape, Qt::DescendingOrder,
					networkView->viewportTransform())) {
					for (QGraphicsItem* candidate = item; candidate; candidate = candidate->parentItem()) {
						if (qgraphicsitem_cast<NodeItem*>(candidate)
							|| qgraphicsitem_cast<StationNodeItem*>(candidate)
							|| qgraphicsitem_cast<TrackLineItem*>(candidate)
							|| qgraphicsitem_cast<ConnectionItem*>(candidate)
							|| qgraphicsitem_cast<SignalItem*>(candidate)
							|| qgraphicsitem_cast<TrainBodyItem*>(candidate)
							|| qgraphicsitem_cast<PassengerItem*>(candidate)) {
							semantic = candidate;
							break;
						}
					}
					if (semantic)
						break;
				}
				if (semantic == stationTarget) {
					semanticPoint = point;
					foundSemanticPoint = true;
				}
			}
		}
		if (!foundSemanticPoint)
			fail("no clear station semantic hit point under overlay");

		const QPoint clickPos = networkView->mapFromScene(semanticPoint);
		const QPoint screenPos = networkView->viewport()->mapToGlobal(clickPos);
		QMouseEvent press(QEvent::MouseButtonPress, QPointF(clickPos), QPointF(screenPos),
			Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(networkView->viewport(), &press);
		QMouseEvent release(QEvent::MouseButtonRelease, QPointF(clickPos), QPointF(screenPos),
			Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(networkView->viewport(), &release);
		QApplication::processEvents();
		if (m_selectedStationName != QString::fromStdString(semanticFixtureNode.stationName))
			fail("station selection did not track station name");
		if (!overlappingOverlay->isSelected())
			fail(QString("station click did not select the overlapping overlay (%1/%2/%3)")
				.arg(overlappingOverlay->stationName(), m_selectedStationName)
				.arg(m_stationOverlays.contains(overlappingOverlay)));
		if (!overlappingOverlay->isLabelVisible())
			fail(QString("selected station click did not reveal its culled label (layout=%1 blocked=%2 selected=%3)")
				.arg(overlappingOverlay->isLayoutVisible())
				.arg(overlappingOverlay->isLabelLayoutVisible() && !overlappingOverlay->isLabelVisible())
				.arg(overlappingOverlay->isSelected()));

		QGraphicsItem* contextTarget = nullptr;
		const QMetaObject::Connection contextConnection = connect(scene, &NetworkScene::ContextMenuRequested,
			this, [&](QGraphicsItem* item, const QPointF&, const QPoint&, bool) { contextTarget = item; });
		QGraphicsSceneContextMenuEvent context(QEvent::GraphicsSceneContextMenu);
		context.setReason(QGraphicsSceneContextMenuEvent::Mouse);
		context.setScenePos(semanticPoint);
		context.setScreenPos(networkView->viewport()->mapToGlobal(
			networkView->mapFromScene(semanticPoint)));
		context.setWidget(networkView->viewport());
		scene->contextMenuEvent(&context);
		disconnect(contextConnection);
		if (contextTarget != stationTarget)
			fail(QString("context menu did not preserve station semantic target (got=%1 expected=%2 gotType=%3 expectedType=%4)")
				.arg(reinterpret_cast<quintptr>(contextTarget), 0, 16)
				.arg(reinterpret_cast<quintptr>(stationTarget), 0, 16)
				.arg(contextTarget ? contextTarget->type() : -1)
				.arg(stationTarget ? stationTarget->type() : -1));

		if (m_sceneContextMenu)
			m_sceneContextMenu->close();
		QApplication::processEvents();
		if (stationTarget->graphicsEffect() == effect)
			handleCloseInfoDockWidget();
		overlappingOverlay->hide();
		stationTarget->hide();
		m_stationOverlays = previousStationOverlays;
		m_stationDecorations = previousStationDecorations;
		m_selectedStationName = previousSelectedStationName;
		for (auto* item : previouslyVisibleItems)
			if (item && item->scene() == scene) {
				if (item->data(kSignalDecorationRole).toBool())
					item->setData(kSignalBaseVisibleRole, true);
				item->setVisible(true);
			}
		updateViewportOverlays();
	}

	if (ok) {
		marker("E2E_STATION_OVERLAY_OK");
		finish(0);
		return;
	}
	std::fprintf(stderr, "E2E_STATION_OVERLAY_FAIL: %s\n", failures.join(", ").toStdString().c_str());
	std::fflush(stderr);
	finish(2);
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
	const QString timelineSentinel = QStringLiteral("E2E timeline status sentinel");
	statusBar()->showMessage(timelineSentinel, 10000);
	if (m_snapshot)
		updateTimeline(m_snapshot->timestep, m_snapshot->totalTimesteps);
	if (statusBar()->currentMessage() != timelineSentinel) {
		ok = false;
		failures << "timeline update overwrote transient status guidance";
	}
	TrainItemGroup* selectedTrain = nullptr;
	TrainBodyItem* selectedTrainBody = nullptr;
	QPen selectedTrainPen;
	QBrush selectedTrainBrush;

	if (!m_speedSlider || !m_speedLabel) {
		ok = false;
		failures << "missing speed controls";
	} else {
		m_speedSlider->setValue(250);
		QApplication::processEvents();
		if (!m_speedLabel->text().contains("4.0x")) {
			ok = false;
			failures << "speed label did not update";
		}
		m_speedSlider->setValue(m_speedSlider->maximum());
	}

	if (!m_followAction || !m_followTrainCombo || m_followTrainCombo->count() == 0) {
		ok = false;
		failures << "missing follow controls";
	} else {
		m_followTrainCombo->setCurrentIndex(0);
	}

	if (allTrains.isEmpty()) {
		ok = false;
		failures << "no train items rendered";
	}
	const QVector<NetworkLegendEntry> mapKeyEntriesBeforeFit = m_networkLegendWidget
		? m_networkLegendWidget->entries() : QVector<NetworkLegendEntry>();
	if (!std::any_of(mapKeyEntriesBeforeFit.cbegin(), mapKeyEntriesBeforeFit.cend(),
		[](const NetworkLegendEntry& entry) { return entry.kind == NetworkLegendEntryKind::Train; })) {
		ok = false;
		failures << "map key did not refresh when trains entered the network";
	}

	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_VISUAL_POLISH") && scene && !allTrains.isEmpty()) {
		TrainItemGroup* testTrain = allTrains.first();
		if (!testTrain || !testTrain->trainPolygonItemList || testTrain->trainPolygonItemList->isEmpty()
			|| !testTrain->trainPolygonItemList->first()
			|| testTrain->trainPolygonItemList->first()->polygon().isEmpty()) {
			ok = false;
			failures << "cannot inject virtual coupling overlay";
		} else {
			paintVCouplingMsg(testTrain, "E2E virtual coupling overlay");
			const int afterFirst = scene->items().size();
			paintVCouplingMsg(testTrain, "E2E virtual coupling overlay");
			const int afterSecond = scene->items().size();
			if (afterSecond != afterFirst) {
				ok = false;
				failures << "virtual coupling overlay recreated between updates";
			}
			if (auto* overlay = m_vcMessageItems.value(testTrain->index, nullptr))
				overlay->setVisible(false);
		}
	}

	// Shell contract: the visual smoke must cover the controls users see before
	// opening any secondary panel, not only the network scene.
	if (!m_toolBar || m_toolBar->toolButtonStyle() != Qt::ToolButtonTextBesideIcon) {
		ok = false;
		failures << "toolbar is not compact text-beside-icon style";
	}
	const auto standardIconPresent = [](QAction* action) {
		return action && !action->icon().isNull();
	};
	if (!standardIconPresent(ui->actionSimulationStart)
		|| !standardIconPresent(ui->actionSimulationPause)
		|| !standardIconPresent(ui->actionSimulationStop)) {
		ok = false;
		failures << "simulation toolbar icons are missing or not standard media icons";
	}

	// Command-bar contract: names and order are stable so the toolbar remains
	// testable without relying on translated display text.
	const auto actionNamed = [this](const char* objectName) {
		return findChild<QAction*>(objectName);
	};
	QAction* openCaseAction = actionNamed("actionOpenCase");
	QAction* runAction = actionNamed("actionRun");
	QAction* pauseAction = actionNamed("actionPause");
	QAction* stopAction = actionNamed("actionStop");
	QAction* zoomInAction = actionNamed("actionZoomIn");
	QAction* zoomOutAction = actionNamed("actionZoomOut");
	QAction* fitAction = actionNamed("actionFit");
	const QList<QAction*> requiredActions{
		openCaseAction, runAction, pauseAction, stopAction, zoomInAction, zoomOutAction, fitAction};
	if (std::any_of(requiredActions.cbegin(), requiredActions.cend(), [](QAction* action) { return !action; })) {
		ok = false;
		failures << "command-bar actions do not have the required stable object names";
	}
	if (!openCaseAction || openCaseAction->property("chooserEntryPoint").toString() != "showStartupChooser") {
		ok = false;
		failures << "Open Case is not marked as the shared startup chooser entry point";
	}
	if (m_toolBar) {
		const QStringList expectedToolbarObjects{
			"openCaseButton", "separatorCasePlayback", "actionRunButton", "actionPauseButton", "actionStopButton",
			"speedSlowerLabel", "speedSlider", "speedFasterLabel", "separatorPlaybackView", "actionFollowButton",
			"followTrainCombo", "actionZoomInButton", "actionZoomOutButton", "actionFitButton"};
		QStringList toolbarObjects;
		for (QAction* action : m_toolBar->actions()) {
			if (!action)
				continue;
			if (QWidget* widget = m_toolBar->widgetForAction(action)) {
				if (!widget->objectName().isEmpty())
					toolbarObjects << widget->objectName();
				else if (!action->objectName().isEmpty())
					toolbarObjects << action->objectName();
			} else if (!action->objectName().isEmpty()) {
				toolbarObjects << action->objectName();
			}
		}
		int lastIndex = -1;
		for (const QString& objectName : expectedToolbarObjects) {
			const int index = toolbarObjects.indexOf(objectName);
			if (index <= lastIndex) {
				ok = false;
				failures << QString("command-bar order is missing or out of order: %1").arg(objectName);
				break;
			}
			lastIndex = index;
		}
		const auto boundaryIndex = [&toolbarObjects](const char* objectName) {
			return toolbarObjects.indexOf(QString::fromLatin1(objectName));
		};
		const int casePlaybackBoundary = boundaryIndex("separatorCasePlayback");
		const int playbackViewBoundary = boundaryIndex("separatorPlaybackView");
		const int openCaseIndex = boundaryIndex("openCaseButton");
		const int runIndex = boundaryIndex("actionRunButton");
		const int fasterIndex = boundaryIndex("speedFasterLabel");
		const int followIndex = boundaryIndex("actionFollowButton");
		if (casePlaybackBoundary <= openCaseIndex || playbackViewBoundary <= casePlaybackBoundary
			|| runIndex <= casePlaybackBoundary || fasterIndex >= playbackViewBoundary || followIndex <= playbackViewBoundary) {
			ok = false;
			failures << "command-bar Case, Playback, and View ranges are not separated in order";
		}
		const auto checkBoundary = [this, &ok, &failures](const char* objectName) {
			QAction* boundary = findChild<QAction*>(objectName);
			if (!boundary || !boundary->isSeparator() || !boundary->property("toolbarGroupBoundary").toBool()) {
				ok = false;
				failures << QString("command-bar group boundary is not named: %1").arg(objectName);
			}
		};
		checkBoundary("separatorCasePlayback");
		checkBoundary("separatorPlaybackView");
		if (pauseAction && stopAction) {
			const int pauseIndex = toolbarObjects.indexOf("actionPauseButton");
			const int stopIndex = toolbarObjects.indexOf("actionStopButton");
			if (pauseIndex < 0 || stopIndex != pauseIndex + 1) {
				ok = false;
				failures << "Pause is not immediately followed by Stop in the command bar";
			}
		}
	}

	const auto toolbarWidget = [this](QAction* action) -> QWidget* {
		return m_toolBar && action ? m_toolBar->widgetForAction(action) : nullptr;
	};
	if (!toolbarWidget(openCaseAction)) {
		ok = false;
		failures << "Open Case has no toolbar button";
	} else if (toolbarWidget(openCaseAction)->objectName() != "openCaseButton") {
		ok = false;
		failures << "Open Case toolbar button has no stable object name";
	}
	const auto checkTransportIcon = [&ok, &failures, &actionNamed](const char* actionName, const char* resource) {
		QAction* action = actionNamed(actionName);
		QFile iconFile(QString::fromLatin1(resource));
		if (!action || !iconFile.exists() || action->icon().isNull()
			|| action->property("iconResource").toString() != QString::fromLatin1(resource)) {
			ok = false;
			failures << QString("transport icon is missing or not assigned: %1").arg(resource);
		}
	};
	checkTransportIcon("actionRun", ":/icons/run.svg");
	checkTransportIcon("actionPause", ":/icons/pause.svg");
	checkTransportIcon("actionStop", ":/icons/stop.svg");

	if (findChild<QWidget*>("toolbarCaseLabel") || findChild<QWidget*>("simulationClockLabel")) {
		ok = false;
		failures << "toolbar still contains the case badge or simulation clock";
	}
	QAction* startTimeAction = actionNamed("actionStartTime");
	if (!startTimeAction || !ui->menuSimulation || !ui->menuSimulation->actions().contains(startTimeAction)
		|| (m_toolBar && m_toolBar->actions().contains(startTimeAction))) {
		ok = false;
		failures << "Start Time is not exclusively in the Simulation menu";
	}
	QLabel* slowerLabel = findChild<QLabel*>("speedSlowerLabel");
	QLabel* fasterLabel = findChild<QLabel*>("speedFasterLabel");
	if (!m_speedSlider || !slowerLabel || !fasterLabel || slowerLabel->text() != "Slower" || fasterLabel->text() != "Faster") {
		ok = false;
		failures << "speed controls do not expose Slower, slider, and Faster";
	} else {
		m_speedSlider->setValue(m_speedSlider->minimum());
		const int slowDelay = stepDelayForSlider(m_speedSlider->value());
		m_speedSlider->setValue(m_speedSlider->maximum());
		const int fastDelay = stepDelayForSlider(m_speedSlider->value());
		if (fastDelay >= slowDelay) {
			ok = false;
			failures << "speed slider right-is-faster mapping changed";
		}
	}
	if (!m_followAction || !m_followTrainCombo || m_followTrainCombo->minimumWidth() != 180
			|| m_followTrainCombo->maximumWidth() > 200) {
		ok = false;
		failures << "Follow or train selector width contract is missing";
	}

	const auto checkTabTraversal = [&ok, &failures, this, &toolbarWidget](QAction* firstAction, const QStringList& expected) {
		QWidget* first = toolbarWidget(firstAction);
		if (!first) {
			ok = false;
			failures << "cannot start command-bar Tab traversal";
			return;
		}
		first->setFocusPolicy(Qt::StrongFocus);
		first->setFocus(Qt::TabFocusReason);
		QStringList actual;
		QApplication::processEvents();
		if (QApplication::focusWidget() != first) {
			ok = false;
			failures << QString("command-bar focus did not start on %1").arg(first->objectName());
			return;
		}
		actual << first->objectName();
		const auto namedAncestor = [](QWidget* widget) {
			for (QWidget* current = widget; current; current = current->parentWidget()) {
				if (!current->objectName().isEmpty())
					return current;
			}
			return widget;
		};
		for (int i = 1; i < expected.size(); ++i) {
			QKeyEvent press(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
			QApplication::sendEvent(first, &press);
			QApplication::processEvents();
			QWidget* focused = QApplication::focusWidget();
			QWidget* named = namedAncestor(focused);
			actual << (named ? named->objectName() : QString());
			first = focused;
			if (!first)
				break;
		}
		if (actual.size() != expected.size()
			|| !std::equal(actual.begin(), actual.end(), expected.begin())) {
			ok = false;
			failures << QString("command-bar Tab traversal order mismatch: %1").arg(actual.join(", "));
		}
	};
	showNormal();
	resize(1024, 720);
	QApplication::processEvents();
	QStringList expectedTabTraversal{"openCaseButton"};
	if (runAction && runAction->isEnabled())
		expectedTabTraversal << "actionRunButton";
	expectedTabTraversal << "actionPauseButton" << "actionStopButton" << "speedSlider"
		<< "actionFollowButton" << "followTrainCombo" << "actionZoomInButton"
		<< "actionZoomOutButton" << "actionFitButton";
	checkTabTraversal(openCaseAction, expectedTabTraversal);

	const auto caseDock = findChild<QDockWidget*>("caseLayersDock");
	if (!caseDock || !caseDock->isVisible()) {
		ok = false;
		failures << "case and layers dock is not visible by default";
	}
	const auto caseLabel = findChild<QLabel*>("caseNameLabel");
	if (!caseLabel || caseLabel->text().trimmed().isEmpty()) {
		ok = false;
		failures << "case state is empty";
	}
	if (findChild<QLabel*>("simulationClockLabel")) {
		ok = false;
		failures << "toolbar still owns the simulation clock";
	}

	const auto secondaryDockHidden = [this](QDockWidget* dock) {
		return dock && !dock->isVisible();
	};
	if (!secondaryDockHidden(m_logPane) || !secondaryDockHidden(m_validationDock)
		|| !secondaryDockHidden(m_compositionDock) || !secondaryDockHidden(m_serviceDock)
		|| !secondaryDockHidden(m_incidentDock)) {
		ok = false;
		failures << "secondary diagnostics docks are visible by default";
	}
	if (!m_loadedDataDock || !m_loadedDataDock->isVisible()) {
		ok = false;
		failures << "loaded data review is not visible after scene open";
	}

	showNormal();
	resize(1200, 800);
	QApplication::processEvents();
	const auto checkCommandBarAtSize = [this, &ok, &failures](int width, int height, const char* screenshotVariable) {
		resize(width, height);
		QApplication::processEvents();
		if (!m_toolBar) {
			ok = false;
			failures << QString("command bar is missing at %1x%2").arg(width).arg(height);
			return;
		}
		const QStringList requiredWidgetNames{
			"openCaseButton", "actionRunButton", "actionPauseButton", "actionStopButton", "speedSlowerLabel", "speedSlider",
			"speedFasterLabel", "actionFollowButton", "followTrainCombo",
			"actionZoomInButton", "actionZoomOutButton", "actionFitButton"};
		if (m_toolBar->height() < 44) {
			ok = false;
			failures << QString("command bar is too short at %1x%2: %3px").arg(width).arg(height).arg(m_toolBar->height());
		}
		const int toolbarCenterY = m_toolBar->rect().center().y();
		QList<QWidget*> controls;
		for (const QString& name : requiredWidgetNames) {
			QWidget* widget = findChild<QWidget*>(name);
			if (!widget || !widget->isVisibleTo(m_toolBar)) {
				ok = false;
				failures << QString("command-bar control %1 is hidden at %2x%3").arg(name).arg(width).arg(height);
				continue;
			}
			const QRect geometry = widget->geometry();
			const QRect toolbarArea = m_toolBar->rect().adjusted(0, 0, 0, 8);
			if (!toolbarArea.contains(geometry) || geometry.height() > 36) {
				ok = false;
				failures << QString("command-bar control %1 escapes toolbar bounds at %2x%3 (%4x%5+%6+%7)")
						.arg(name).arg(width).arg(height).arg(geometry.width()).arg(geometry.height())
						.arg(geometry.x()).arg(geometry.y());
			}
			if (qAbs(geometry.center().y() - toolbarCenterY) > 1) {
				ok = false;
				failures << QString("command-bar control %1 is not vertically centered at %2x%3")
					.arg(name).arg(width).arg(height);
			}
			if (name == "followTrainCombo" && geometry.width() != 180) {
				ok = false;
				failures << QString("train selector rendered width changed at %1x%2: expected 180px, has %3px")
					.arg(width).arg(height).arg(geometry.width());
			}
			if (auto* button = qobject_cast<QToolButton*>(widget)) {
				if (button->toolButtonStyle() != Qt::ToolButtonIconOnly
					&& !button->text().isEmpty() && button->sizeHint().width() > geometry.width()) {
					ok = false;
					failures << QString("command-bar text is clipped at %1x%2: %3 needs %4px, has %5px")
						.arg(width).arg(height).arg(name).arg(button->sizeHint().width()).arg(geometry.width());
				}
			}
			controls << widget;
		}
		for (int i = 0; i < controls.size(); ++i) {
			for (int j = i + 1; j < controls.size(); ++j) {
				if (controls[i]->geometry().intersects(controls[j]->geometry())) {
					ok = false;
					failures << QString("command-bar controls overlap at %1x%2: %3/%4")
						.arg(width).arg(height).arg(controls[i]->objectName()).arg(controls[j]->objectName());
				}
			}
		}
		const QString screenshotPath = qEnvironmentVariable(screenshotVariable);
		if (!screenshotPath.isEmpty() && !grab().save(screenshotPath)) {
			ok = false;
			failures << QString("command-bar screenshot save failed at %1x%2").arg(width).arg(height);
		}
	};
	checkCommandBarAtSize(1024, 720, "QEGTRAIN_E2E_COMMAND_BAR_1024_SCREENSHOT");
	checkCommandBarAtSize(1200, 800, "QEGTRAIN_E2E_COMMAND_BAR_1200_SCREENSHOT");
	checkCommandBarAtSize(1440, 900, "QEGTRAIN_E2E_COMMAND_BAR_1440_SCREENSHOT");
	resize(1200, 800);
	QApplication::processEvents();
	const int defaultNetworkWidth = networkView ? networkView->width() : 0;
	const int caseDockWidth = caseDock ? caseDock->width() : 0;
	if (!networkView || !caseDock || defaultNetworkWidth <= caseDockWidth) {
		ok = false;
		failures << QString("network viewport (%1px) is not wider than case/layers dock (%2px)")
						.arg(defaultNetworkWidth)
						.arg(caseDockWidth);
	}
	if (!scene || !networkView) {
		ok = false;
		failures << "cannot click a train without a scene and viewport";
	} else {
		networkView->zoomBy(4.0);
		QApplication::processEvents();
		for (auto* candidate : allTrains) {
			if (!candidate || !candidate->isVisible() || !candidate->trainPolygonItemList)
				continue;
			for (auto* body : *candidate->trainPolygonItemList) {
				if (body && body->isVisible() && !body->polygon().isEmpty()) {
					selectedTrain = candidate;
					selectedTrainBody = body;
					break;
				}
			}
			if (selectedTrainBody)
				break;
		}
		if (!selectedTrain || !selectedTrainBody) {
			ok = false;
			failures << "no visible train body available for scene click";
		} else {
			selectedTrainPen = selectedTrainBody->pen();
			selectedTrainBrush = selectedTrainBody->brush();
			QGraphicsSceneMouseEvent event(QEvent::GraphicsSceneMousePress);
			event.setButton(Qt::LeftButton);
			event.setButtons(Qt::LeftButton);
			event.setScenePos(selectedTrainBody->sceneBoundingRect().center());
			event.setWidget(networkView->viewport());
			scene->mousePressEvent(&event);
			QApplication::processEvents();
			const int comboIndex = m_followTrainCombo ? m_followTrainCombo->findData(selectedTrain->index) : -1;
			if (!m_followTrainCombo || comboIndex < 0 || m_followTrainCombo->currentIndex() != comboIndex) {
				ok = false;
				failures << "train scene click did not select its combo row";
			}
			if (!m_followAction || !m_followAction->isChecked() || m_followTrainIndex != selectedTrain->index) {
				ok = false;
				failures << "train scene click did not activate follow";
			}
			if (!trainInfoWidget || !trainInfoWidget->isVisible() || trainIDText->text().trimmed().isEmpty()) {
				ok = false;
				failures << "train scene click did not populate train info";
			}
			if (!selectedTrain->graphicsEffect()) {
				ok = false;
				failures << "selected train has no halo effect";
			}
			if (selectedTrainBody->pen() != selectedTrainPen || selectedTrainBody->brush() != selectedTrainBrush) {
				ok = false;
				failures << "train source paint changed under halo effect";
			}
			const QPointF viewportCenter = networkView->mapToScene(networkView->viewport()->rect().center());
			const QRectF visibleScene = networkView->mapToScene(networkView->viewport()->rect()).boundingRect();
			const QRectF sceneBounds = networkView->sceneRect();
			const QPointF requestedCenter = selectedTrain->sceneBoundingRect().center();
			const auto clampedCenter = [](qreal value, qreal low, qreal high, qreal halfSpan) {
				if (high - low <= halfSpan * 2.0)
					return (low + high) / 2.0;
				return qBound(low + halfSpan, value, high - halfSpan);
			};
			const QPointF expectedCenter(
				clampedCenter(requestedCenter.x(), sceneBounds.left(), sceneBounds.right(), visibleScene.width() / 2.0),
				clampedCenter(requestedCenter.y(), sceneBounds.top(), sceneBounds.bottom(), visibleScene.height() / 2.0));
			const qreal centerTolerance = 2.0;
			const qreal centerDistance = QLineF(networkView->mapFromScene(viewportCenter),
				networkView->mapFromScene(expectedCenter)).length();
			if (centerDistance > centerTolerance) {
				ok = false;
				failures << QString("train scene click did not center the view (distance=%1px tolerance=%2px view=%3,%4 expected=%5,%6)")
						.arg(centerDistance, 0, 'f', 1)
						.arg(centerTolerance, 0, 'f', 1)
						.arg(viewportCenter.x(), 0, 'f', 1)
						.arg(viewportCenter.y(), 0, 'f', 1)
						.arg(expectedCenter.x(), 0, 'f', 1)
						.arg(expectedCenter.y(), 0, 'f', 1);
			}
		}
	}
	if (!allArcs.isEmpty() && allArcs.first() && allArcs.first()->arc) {
		displayArcInfo(allArcs.first());
		if (!arcOperationalStateText || arcOperationalStateText->text().trimmed().isEmpty()
			|| !arcConnectedSignalsText || arcConnectedSignalsText->text().trimmed().isEmpty()) {
			ok = false;
			failures << "arc inspector fields are empty";
		}
	}
	StationNodeItem* stationItem = nullptr;
	if (scene) {
		for (auto* item : scene->items()) {
			StationNodeItem* candidate = qgraphicsitem_cast<StationNodeItem*>(item);
			if (candidate && candidate->node) {
				stationItem = candidate;
				break;
			}
		}
	}
	if (stationItem && stationItem->node && !stationItem->node->stationName.empty()) {
		displayStationNodeInfo(stationItem);
		if (!nodeStationNameText || nodeStationNameText->text().trimmed().isEmpty()
			|| !nodeRegionText || nodeRegionText->text().trimmed().isEmpty()
			|| !nodeConnectedTracksText || nodeConnectedTracksText->text().trimmed().isEmpty()
			|| !nodeSignalledText || nodeSignalledText->text().trimmed().isEmpty()) {
			ok = false;
			failures << "station inspector fields are empty";
		}
	}
	SignalItem* inspectorSignal = nullptr;
	for (auto* candidate : allSignals) {
		if (candidate && candidate->isVisible()) {
			inspectorSignal = candidate;
			break;
		}
	}
	if (inspectorSignal) {
		displaySignallingInfo(inspectorSignal);
		if (!signallingAspectText || signallingAspectText->text().trimmed().isEmpty()
			|| !signallingProtectedSectionText || signallingProtectedSectionText->text().trimmed().isEmpty()
			|| !signallingNextTrackText || signallingNextTrackText->text().trimmed().isEmpty()) {
			ok = false;
			failures << "signal inspector fields are empty";
		}
	}
	handleCloseInfoDockWidget();
	infoDockWidget->hide();

	if (!networkView || !m_incidentDock) {
		ok = false;
		failures << "incident geometry controls are missing";
	} else {
		m_incidentDock->show();
		m_incidentDock->raise();
		QApplication::processEvents();
		const int incidentNetworkWidth = networkView->width();
		const int incidentDockWidth = m_incidentDock->width();
		if (incidentNetworkWidth < 560 || incidentNetworkWidth <= incidentDockWidth || incidentDockWidth > 360) {
			ok = false;
			failures << QString("incident geometry network=%1px, dock=%2px violates 560px/wider/360px limits")
						.arg(incidentNetworkWidth)
						.arg(incidentDockWidth);
		}
		m_incidentDock->hide();
		QApplication::processEvents();
		const int restoredNetworkWidth = networkView->width();
		if (qAbs(restoredNetworkWidth - defaultNetworkWidth) > 8) {
			ok = false;
			failures << QString("network viewport did not restore: before=%1px, after=%2px")
						.arg(defaultNetworkWidth)
						.arg(restoredNetworkWidth);
		}
	}

	// Run the layer toggles above the dense-detail threshold and put the
	// overview back afterwards.
	const auto hasVisibleSignalStructure = [this]() {
		return std::any_of(m_signalDecorations.cbegin(), m_signalDecorations.cend(),
			[](QGraphicsItem* item) {
				return item && !qgraphicsitem_cast<SignalItem*>(item) && item->isVisible();
				});
	};
	setFollowTrain(-1);
	handleCloseInfoDockWidget();
	if (networkView) {
		networkView->fitToTopology();
		updateViewportOverlays();
		QApplication::processEvents();
	}
	if (hasVisibleSignalStructure()) {
		ok = false;
		failures << "signal posts or bases remain visible at overview zoom";
	}
	if (networkView) {
		const qreal currentRatio = networkView->zoomRatio();
		if (currentRatio < kSignalDetailZoom)
			networkView->zoomBy(kSignalDetailZoom / currentRatio);
		updateViewportOverlays();
		QApplication::processEvents();
		if (!hasVisibleSignalStructure()) {
			ok = false;
			failures << "signal posts and bases did not return at detailed zoom";
		}
	}

	const auto layerToggle = [this](const char* objectName) {
		return findChild<QCheckBox*>(objectName);
	};
	QCheckBox* stationLayer = layerToggle("layerStationsPlatforms");
	QCheckBox* stationNames = layerToggle("layerStationNames");
	QCheckBox* trainLayer = layerToggle("layerTrains");
	QCheckBox* trainSpeedLabels = layerToggle("layerTrainSpeedLabels");
	QCheckBox* signalLayer = layerToggle("layerSignals");
	QCheckBox* passengerLayer = layerToggle("layerPassengers");
	const QStringList mapKeyEntries = m_networkLegendWidget
		? m_networkLegendWidget->entryLabels() : QStringList();
	if (!stationLayer || !stationNames || !trainLayer || !trainSpeedLabels
		|| !signalLayer || !passengerLayer) {
		ok = false;
		failures << "required layer controls are missing";
	} else {
		const int initialItems = scene ? scene->items().size() : 0;
		const bool initialTrainVisible = !allTrains.isEmpty() && allTrains.first()->isVisible();
		trainLayer->setChecked(!trainLayer->isChecked());
		QApplication::processEvents();
		if (allTrains.isEmpty() || allTrains.first()->isVisible() == initialTrainVisible) {
			ok = false;
			failures << "train layer toggle is not functional";
		}
		trainLayer->setChecked(!trainLayer->isChecked());
		QApplication::processEvents();
		if (scene && scene->items().size() != initialItems) {
			ok = false;
			failures << "train layer toggle changed scene ownership";
		}
		const auto checkItemLayer = [&](QCheckBox* layer, const auto& items, const char* name) {
			if (items.isEmpty())
				return;
			const bool initiallyChecked = layer->isChecked();
			QVector<bool> initiallyVisible;
			initiallyVisible.reserve(items.size());
			for (auto* item : items)
				initiallyVisible.push_back(item && item->isVisible());
			layer->setChecked(false);
			QApplication::processEvents();
			if (std::any_of(items.cbegin(), items.cend(), [](auto* item) { return item && item->isVisible(); })) {
				ok = false;
				failures << QString("%1 layer did not hide its items").arg(name);
			}
			layer->setChecked(initiallyChecked);
			QApplication::processEvents();
			bool restored = true;
			int index = 0;
			for (auto* item : items) {
				if ((item && item->isVisible()) != initiallyVisible.at(index++)) {
					restored = false;
					break;
				}
			}
			if (!restored) {
				ok = false;
				failures << QString("%1 layer did not restore its items").arg(name);
			}
		};
		checkItemLayer(stationLayer, m_stationDecorations, "station decorations");
		if (m_stationOverlays.isEmpty()) {
			ok = false;
			failures << "station-name layer has no station overlay to verify";
		} else {
			StationOverlayItem* station = m_stationOverlays.first();
			const bool stationVisible = station && station->isVisible();
			stationNames->setChecked(false);
			QApplication::processEvents();
			if (!station || station->isLabelVisible() || station->isVisible() != stationVisible) {
				ok = false;
				failures << "station-name toggle changed the station object";
			}
			stationNames->setChecked(true);
		}
		if (m_trainBadges.isEmpty()) {
			ok = false;
			failures << "train speed-label layer has no badge to verify";
		} else {
			TrainBadgeItem* badge = m_trainBadges.first();
			const bool badgeVisible = badge && badge->isVisible();
			trainSpeedLabels->setChecked(false);
			QApplication::processEvents();
			if (!badge || badge->isSpeedVisible() || badge->isVisible() != badgeVisible) {
				ok = false;
				failures << "train speed-label toggle changed the train object";
			}
			trainSpeedLabels->setChecked(true);
		}
		checkItemLayer(signalLayer, m_signalDecorations, "signal groups");
		QList<QGraphicsItem*> passengerItems;
		for (auto* platform : allPlatforms) {
			if (!platform)
				continue;
			if (platform->textIcon)
				passengerItems.append(platform->textIcon);
			for (auto* icon : platform->passengerIcons)
				if (icon)
					passengerItems.append(icon);
		}
		if (initial_variables.PAX_GUI && (passengerItems.isEmpty()
			|| std::none_of(passengerItems.cbegin(), passengerItems.cend(), [](auto* item) {
				return item && item->isVisible();
			}))) {
			ok = false;
			failures << "PAX enabled but no visible passenger-owned graphics rendered";
		}
		checkItemLayer(passengerLayer, passengerItems, "passenger load");
		const QStringList currentMapKeyEntries = m_networkLegendWidget
			? m_networkLegendWidget->entryLabels() : QStringList();
		if (!m_networkLegendWidget
			|| currentMapKeyEntries.size() != mapKeyEntries.size()
			|| !std::equal(currentMapKeyEntries.cbegin(), currentMapKeyEntries.cend(), mapKeyEntries.cbegin())) {
			ok = false;
			failures << "layer toggles changed or reordered map key entries";
		}
	}

	if (networkView) {
		networkView->fitToTopology();
		updateViewportOverlays();
		QApplication::processEvents();
	}

	const auto captureScreenshot = [this, &ok, &failures](const char* variable, const char* label) {
		const QString path = qEnvironmentVariable(variable);
		if (path.isEmpty())
			return;
		QApplication::processEvents();
		if (!grab().save(path)) {
			ok = false;
			failures << QString("%1 screenshot save failed").arg(label);
		}
	};

	if (!m_networkLegendWidget || !m_networkLegendWidget->isVisible()
		|| !networkView || networkView->isAncestorOf(m_networkLegendWidget)) {
		ok = false;
		failures << "map key is missing from the left rail";
	} else {
		m_networkLegendWidget->setExpanded(false);
		QApplication::processEvents();
		if (m_networkLegendWidget->isExpanded() || !m_showMapKeyAction) {
			ok = false;
			failures << "map key did not collapse or lacks its View action";
		} else {
			m_showMapKeyAction->trigger();
			QApplication::processEvents();
			if (!m_networkLegendWidget->isExpanded() || !m_caseLayersDock->isVisible()) {
				ok = false;
				failures << "View did not restore the map key";
			}
		}
	}
	if (!networkView) {
		ok = false;
		failures << "network viewport is missing before default capture";
	} else {
		const QRectF visibleViewport = networkView->mapToScene(networkView->viewport()->rect()).boundingRect();
		const bool visibleBadge = std::any_of(m_trainBadges.cbegin(), m_trainBadges.cend(),
			[](const auto& entry) { return entry && entry->isVisible(); });
		const QRectF viewportRect(networkView->viewport()->rect());
		const bool badgeInViewport = std::any_of(m_trainBadges.cbegin(), m_trainBadges.cend(),
			[this, &viewportRect](const auto& entry) {
				return entry && entry->isVisible() && viewportRect.contains(
					QPointF(networkView->mapFromScene(entry->scenePos())) + entry->boundingRect().center());
			});
		if (!visibleBadge) {
			ok = false;
			failures << "no visible train badge rendered";
		} else if (!badgeInViewport) {
			ok = false;
			failures << "visible train badge center is outside default viewport";
		}

		const bool operationalTrack = std::any_of(allArcs.cbegin(), allArcs.cend(),
			[](const TrackLineItem* item) {
				return item && item->operationalState() != TrackOperationalState::Free;
			});
		const bool operationalTrackInViewport = std::any_of(allArcs.cbegin(), allArcs.cend(),
			[&visibleViewport](const TrackLineItem* item) {
				return item && item->operationalState() != TrackOperationalState::Free
					&& visibleViewport.intersects(item->sceneBoundingRect());
			});
		if (!operationalTrack) {
			ok = false;
			failures << "playback did not render an operational track state";
		} else if (!operationalTrackInViewport) {
			ok = false;
			failures << "no non-free track intersects default viewport";
		}
	}
	if (m_worker)
		m_worker->requestPause();

	// Capture the readable default view before changing follow or zoom state.
	if (networkView) {
		for (int i = 0; i < 100; ++i)
			ui->actionZoomIn->trigger();
		if (networkView->zoomRatio() < NetworkView::maximumZoomRatio() - 1e-5) {
			ok = false;
			failures << "toolbar zoom-in did not reach the station-detail clamp";
		}
		for (int i = 0; i < 100; ++i)
			ui->actionZoomOut->trigger();
		if (networkView->zoomRatio() > 1.0 + 1e-5) {
			ok = false;
			failures << "toolbar zoom-out did not reach the Fit clamp";
		}
		QKeyEvent fitPress(QEvent::KeyPress, Qt::Key_0, Qt::ControlModifier);
		QKeyEvent fitRelease(QEvent::KeyRelease, Qt::Key_0, Qt::ControlModifier);
		QApplication::sendEvent(this, &fitPress);
		QApplication::sendEvent(this, &fitRelease);
		if (networkView->zoomLabel() != QStringLiteral("Fit")) {
			ok = false;
			failures << "Ctrl+0 did not trigger the Fit action";
		}
	}
	captureScreenshot("QEGTRAIN_E2E_SCREENSHOT", "default");
	if (networkView) {
		const bool fitMarkers = std::all_of(m_trainBadges.cbegin(), m_trainBadges.cend(),
			[](const TrainBadgeItem* badge) {
				return !badge || badge->presentation() == TrainBadgeItem::Presentation::Overview
					&& !badge->showsIdentifier() && !badge->showsSpeed();
			});
		if (!fitMarkers) {
			ok = false;
			failures << "Fit did not reduce ordinary train overlays to markers";
		}
		networkView->zoomBy(TrainBadgeItem::identityZoomThreshold());
		updateViewportOverlays();
		QApplication::processEvents();
		const bool identityChips = std::all_of(m_trainBadges.cbegin(), m_trainBadges.cend(),
			[](const TrainBadgeItem* badge) {
				return !badge || badge->presentation() == TrainBadgeItem::Presentation::Identity
					&& badge->showsIdentifier() && !badge->showsSpeed();
			});
		if (!identityChips) {
			ok = false;
			failures << "1.8x did not show identity chips without speed";
		}
	}
	captureScreenshot("QEGTRAIN_E2E_MEDIUM_SCREENSHOT", "medium");
	if (networkView && !allTrains.isEmpty()) {
		networkView->fitToTopology();
		networkView->centerOn(allTrains.first()->sceneBoundingRect().center());
		networkView->zoomBy(3.0);
		if (qAbs(networkView->zoomRatio() - 3.0) > 1e-5) {
			ok = false;
			failures << "dense capture did not restore the 3x zoom state";
		}
		QApplication::processEvents();
		const bool detailedLabels = std::all_of(m_trainBadges.cbegin(), m_trainBadges.cend(),
			[](const TrainBadgeItem* badge) {
				return !badge || badge->presentation() == TrainBadgeItem::Presentation::Detailed
					&& badge->showsIdentifier() && badge->showsSpeed();
			});
		if (!detailedLabels) {
			ok = false;
			failures << "3x did not show detailed train labels with speed";
		}
		m_trainSpeedLabelsCheck->setChecked(false);
		QApplication::processEvents();
		if (std::any_of(m_trainBadges.cbegin(), m_trainBadges.cend(),
				[](const TrainBadgeItem* badge) { return badge && badge->showsSpeed(); })) {
			ok = false;
			failures << "disabled train speed labels left detailed speed text visible";
		}
		m_trainSpeedLabelsCheck->setChecked(true);
		QApplication::processEvents();
	} else {
		ok = false;
		failures << "cannot create dense view without a train";
	}
	captureScreenshot("QEGTRAIN_E2E_DENSE_SCREENSHOT", "dense");
	if (networkView && selectedTrain && selectedTrainBody) {
		networkView->fitToTopology();
		displayTrainDetails(selectedTrainBody, false);
		QApplication::processEvents();
		TrainBadgeItem* selectedBadge = m_trainBadges.value(selectedTrain->index, nullptr);
		const bool ordinaryMarkers = std::all_of(m_trainBadges.cbegin(), m_trainBadges.cend(),
			[this](const TrainBadgeItem* badge) {
				return !badge || badge->isPromoted()
					|| badge->presentation() == TrainBadgeItem::Presentation::Overview;
			});
		if (!selectedBadge || !selectedBadge->isPromoted()
				|| selectedBadge->presentation() != TrainBadgeItem::Presentation::Detailed
				|| !ordinaryMarkers) {
			ok = false;
			failures << "selected train was not promoted over Fit markers";
		}
	} else {
		ok = false;
		failures << "cannot create selected-train Fit capture";
	}
	captureScreenshot("QEGTRAIN_E2E_SELECTED_SCREENSHOT", "selected Fit");
	if (networkView) {
		networkView->fitToTopology();
		networkView->zoomBy(TrainBadgeItem::detailedZoomThreshold());
		updateViewportOverlays();
		QApplication::processEvents();
	}

	SignalItem* visibleSignal = nullptr;
	QGraphicsItem* signalHit = nullptr;
	bool anyVisibleSignal = false;
	for (auto* signal : allSignals) {
		if (!signal || !signal->isVisible())
			continue;
		anyVisibleSignal = true;
		QGraphicsItem* hit = scene->itemAt(signal->sceneBoundingRect().center(), networkView->viewportTransform());
		if (qgraphicsitem_cast<SignalItem*>(hit) == signal) {
			visibleSignal = signal;
			signalHit = hit;
			break;
		}
	}
	if (!anyVisibleSignal) {
		ok = false;
		failures << "no visible signal rendered";
	} else if (!visibleSignal) {
		ok = false;
		failures << "signal scene hit-test did not resolve any visible glyph";
	} else {
		if (qgraphicsitem_cast<SignalItem*>(signalHit) != visibleSignal) {
			ok = false;
			failures << "signal scene hit-test did not resolve the glyph";
		}
	}

	const auto requestContextMenu = [this](const QPointF& scenePos, bool keyboard) -> QMenu* {
		if (!scene || !networkView)
			return nullptr;
		QGraphicsSceneContextMenuEvent event(QEvent::GraphicsSceneContextMenu);
		event.setReason(keyboard ? QGraphicsSceneContextMenuEvent::Keyboard : QGraphicsSceneContextMenuEvent::Mouse);
		event.setScenePos(scenePos);
		event.setScreenPos(networkView->viewport()->mapToGlobal(networkView->mapFromScene(scenePos)));
		event.setWidget(networkView->viewport());
		scene->contextMenuEvent(&event);
		QApplication::processEvents();
		return m_sceneContextMenu.data();
	};
	const auto closeContextMenu = [this]() {
		if (m_sceneContextMenu) {
			m_sceneContextMenu->close();
			QApplication::processEvents();
		}
	};
	const auto findMenuAction = [&ok, &failures](QMenu* menu, const QString& text) -> QAction* {
		if (!menu) {
			ok = false;
			failures << QString("context menu missing while looking for %1").arg(text);
			return nullptr;
		}
		for (auto* action : menu->actions()) {
			if (action && action->text() == text)
				return action;
		}
		ok = false;
		failures << QString("context menu missing action %1").arg(text);
		return nullptr;
	};
	const auto checkDeferredAction = [&findMenuAction, &ok, &failures](QMenu* menu, const QString& text,
		const QString& explanation) {
		QAction* action = findMenuAction(menu, text);
		if (!action)
			return;
		if (action->isEnabled() || action->toolTip() != explanation || action->statusTip() != explanation) {
			ok = false;
			failures << QString("deferred context action explanation mismatch: %1").arg(text);
		}
	};
	const QString trainRouteExplanation = QStringLiteral(
		"Requires the train-to-route association and an infrastructure query.");
	const QString stationTimetableExplanation = QStringLiteral(
		"Requires the station timetable association and an arrivals/departures query.");
	const QString trackOccupancyExplanation = QStringLiteral(
		"Requires the track-occupancy association and an active-train query.");
	const QString trackIncidentExplanation = QStringLiteral(
		"Requires the track-incident association and an incident query.");
	const QString signalApproachExplanation = QStringLiteral(
		"Requires the signal route association and an approaching-train query.");

	selectedTrain = nullptr;
	selectedTrainBody = nullptr;
	for (auto* candidate : allTrains) {
		if (!candidate || !candidate->isVisible() || !candidate->trainPolygonItemList)
			continue;
		for (auto* body : *candidate->trainPolygonItemList) {
			if (body && body->isVisible() && !body->polygon().isEmpty()) {
				selectedTrain = candidate;
				selectedTrainBody = body;
				break;
			}
		}
		if (selectedTrainBody)
			break;
	}
	if (!selectedTrainBody) {
		ok = false;
		failures << "no visible train body available after pausing visual checks";
	}
	if (selectedTrainBody) {
		setFollowTrain(-1);
		QMenu* menu = requestContextMenu(selectedTrainBody->sceneBoundingRect().center(), true);
		QAction* details = findMenuAction(menu, "Show details");
		if (!details || details->icon().isNull()) {
			ok = false;
			failures << "train details action is missing refreshed icon";
		} else {
			details->trigger();
			QApplication::processEvents();
			if (!trainInfoWidget || !trainInfoWidget->isVisible() || (m_followAction && m_followAction->isChecked()) || m_followTrainIndex != -1) {
				ok = false;
				failures << "train context details unexpectedly changed follow mode";
			}
		}
		QAction* center = findMenuAction(menu, "Center in view");
		if (center)
			center->trigger();
		QAction* copy = findMenuAction(menu, "Copy train index");
		if (copy) {
			copy->trigger();
			if (!QApplication::clipboard() || QApplication::clipboard()->text() != QString::number(selectedTrain->index)) {
				ok = false;
				failures << "train context copy action did not copy the index";
			}
		}
		QAction* follow = findMenuAction(menu, "Follow train");
		if (follow) {
			follow->trigger();
			QApplication::processEvents();
			if (!m_followAction || !m_followAction->isChecked() || m_followTrainIndex != selectedTrain->index) {
				ok = false;
				failures << "train context follow action did not activate follow mode";
			}
		}
		checkDeferredAction(menu, "Planned route and related infrastructure", trainRouteExplanation);
		const QString contextPath = qEnvironmentVariable("QEGTRAIN_E2E_CONTEXT_SCREENSHOT");
		if (contextPath.isEmpty() || !menu || !menu->grab().save(contextPath)) {
			ok = false;
			failures << "context menu screenshot save failed";
		}
		closeContextMenu();
	} else {
		ok = false;
		failures << "cannot open train context menu without a train body";
	}

	if (scene && networkView) {
		const int staleTrainIndex = 2147483001;
		auto* staleGroup = new TrainItemGroup();
		staleGroup->index = staleTrainIndex;
		staleGroup->trainDescription = "__e2e_stale_train__";
		staleGroup->trainType = "E2E";
		staleGroup->trainId = 1.0;
		auto* staleBody = new TrainBodyItem(QPolygonF({QPointF(0.0, 0.0), QPointF(20.0, 0.0),
			QPointF(20.0, 10.0), QPointF(0.0, 10.0)}));
		staleBody->index = staleTrainIndex;
		staleGroup->addToGroup(staleBody);
		staleGroup->trainPolygonItemList = new QList<TrainBodyItem*>();
		staleGroup->trainPolygonItemList->append(staleBody);
		staleGroup->setPos(scene->itemsBoundingRect().bottomRight() + QPointF(100.0, 100.0));
		scene->addItem(staleGroup);
		QMenu* menu = requestContextMenu(staleBody->sceneBoundingRect().center(), false);
		QAction* details = findMenuAction(menu, "Show details");
		const bool infoVisibleBefore = infoDockWidget && infoDockWidget->isVisible();
		const bool trainInfoVisibleBefore = trainInfoWidget && trainInfoWidget->isVisible();
		const QString infoTitleBefore = infoDockWidget ? infoDockWidget->windowTitle() : QString();
		const bool effectPresentBefore = effect != nullptr;
		staleGroup->removeFromGroup(staleBody);
		delete staleBody;
		if (details)
			details->trigger();
		QApplication::processEvents();
		if ((infoDockWidget && infoDockWidget->isVisible()) != infoVisibleBefore
			|| (trainInfoWidget && trainInfoWidget->isVisible()) != trainInfoVisibleBefore
			|| (infoDockWidget && infoDockWidget->windowTitle() != infoTitleBefore)
			|| (effect != nullptr) != effectPresentBefore) {
			ok = false;
			failures << "stale train context action changed application state";
		}
		closeContextMenu();
		if (staleGroup->trainPolygonItemList) {
			staleGroup->trainPolygonItemList->clear();
			delete staleGroup->trainPolygonItemList;
			staleGroup->trainPolygonItemList = nullptr;
		}
		scene->removeItem(staleGroup);
		delete staleGroup;
	}

	if (stationItem) {
		QMenu* menu = nullptr;
		if (networkView) {
			const QPointF stationCenter = stationItem->sceneBoundingRect().center();
			const QPoint screenPos = networkView->viewport()->mapToGlobal(networkView->mapFromScene(stationCenter));
			showSceneContextMenu(stationItem, stationCenter, screenPos, false);
			QApplication::processEvents();
			menu = m_sceneContextMenu.data();
		}
		QAction* details = findMenuAction(menu, "Show details");
		if (!details || details->icon().isNull()) {
			ok = false;
			failures << "station details action is missing refreshed icon";
		} else {
			details->trigger();
			QApplication::processEvents();
			if (!nodeInfoWidget || !nodeInfoWidget->isVisible()) {
				ok = false;
				failures << "station context details did not open station info";
			}
		}
		QAction* copy = findMenuAction(menu, "Copy node ID");
		if (copy) {
			copy->trigger();
			const QString expectedNodeId = QString::number(stationItem->node->ID, 'g',
				std::numeric_limits<double>::max_digits10);
			if (!QApplication::clipboard() || QApplication::clipboard()->text() != expectedNodeId) {
				ok = false;
				failures << "station context copy action did not preserve node precision";
			}
		}
		checkDeferredAction(menu, "Filtered station arrivals and departures", stationTimetableExplanation);
		closeContextMenu();
	} else {
		ok = false;
		failures << "cannot open station context menu without a station";
	}

	TrackLineItem* contextArc = nullptr;
	QMenu* trackMenu = nullptr;
	for (auto* candidate : allArcs) {
		if (candidate && candidate->scene() == scene && candidate->arc) {
			contextArc = candidate;
			break;
		}
	}
	if (contextArc && networkView) {
		const QPointF trackCenter = contextArc->sceneBoundingRect().center();
		const QPoint screenPos = networkView->viewport()->mapToGlobal(networkView->mapFromScene(trackCenter));
		showSceneContextMenu(contextArc, trackCenter, screenPos, false);
		QApplication::processEvents();
		trackMenu = m_sceneContextMenu.data();
	}
	if (!contextArc) {
		ok = false;
		failures << "cannot open track context menu";
	} else {
		QAction* details = findMenuAction(trackMenu, "Show details");
		if (details) {
			details->trigger();
			QApplication::processEvents();
			if (!arcInfoWidget || !arcInfoWidget->isVisible()) {
				ok = false;
				failures << "track context details did not open arc info";
			}
		}
		QAction* copy = findMenuAction(trackMenu, "Copy arc ID");
		if (copy) {
			copy->trigger();
			const QString expectedArcId = QString::number(contextArc->arc->ID, 'g',
				std::numeric_limits<double>::max_digits10);
			if (!QApplication::clipboard() || QApplication::clipboard()->text() != expectedArcId) {
				ok = false;
				failures << "track context copy action did not preserve arc precision";
			}
		}
		checkDeferredAction(trackMenu, "Trains currently using this track", trackOccupancyExplanation);
		checkDeferredAction(trackMenu, "Incidents affecting this track", trackIncidentExplanation);
		closeContextMenu();
	}

	visibleSignal = nullptr;
	for (auto* signal : allSignals) {
		if (!signal || !signal->isVisible())
			continue;
		if (qgraphicsitem_cast<SignalItem*>(scene->itemAt(signal->sceneBoundingRect().center(),
				networkView->viewportTransform())) == signal) {
			visibleSignal = signal;
			break;
		}
	}
	if (visibleSignal) {
		QMenu* menu = requestContextMenu(visibleSignal->sceneBoundingRect().center(), false);
		QAction* details = findMenuAction(menu, "Show details");
		if (!details || details->icon().isNull()) {
			ok = false;
			failures << "signal details action is missing refreshed icon";
		} else {
			details->trigger();
			QApplication::processEvents();
			if (!signallingInfoWidget || !signallingInfoWidget->isVisible()) {
				ok = false;
				failures << "signal context details did not open signal info";
			}
		}
		QAction* copy = findMenuAction(menu, "Copy signal location");
		if (copy) {
			copy->trigger();
			const QString expectedSignalLocation = QString("track %1 @ %2 (%3)")
				.arg(visibleSignal->trackID)
				.arg(visibleSignal->X, 0, 'g', std::numeric_limits<double>::max_digits10)
				.arg(visibleSignal->reversedDirection ? "reverse" : "forward");
			if (!QApplication::clipboard() || QApplication::clipboard()->text() != expectedSignalLocation) {
				ok = false;
				failures << "signal context copy action did not preserve position precision";
			}
		}
		checkDeferredAction(menu, "Next train approaching this signal", signalApproachExplanation);
		closeContextMenu();
	} else {
		ok = false;
		failures << "cannot open signal context menu without a signal";
	}

	if (scene && networkView) {
		const int temporarySignalTrack = 2147483000;
		const double temporarySignalPosition = 0.12345678901234566;
		const bool temporarySignalReversed = true;
		auto* temporarySignal = new SignalItem(QRectF(-6.0, -8.0, 12.0, 16.0));
		temporarySignal->trackID = temporarySignalTrack;
		temporarySignal->X = temporarySignalPosition;
		temporarySignal->setReversedDirection(temporarySignalReversed);
		temporarySignal->setPos(scene->itemsBoundingRect().bottomRight() + QPointF(100.0, 100.0));
		scene->addItem(temporarySignal);
		allSignals.push_back(temporarySignal);
		QMenu* menu = requestContextMenu(temporarySignal->sceneBoundingRect().center(), false);
		QAction* copy = findMenuAction(menu, "Copy signal location");
		const QString expectedSignalLocation = QStringLiteral(
			"track 2147483000 @ 0.12345678901234566 (reverse)");
		if (copy) {
			copy->trigger();
			if (!QApplication::clipboard() || QApplication::clipboard()->text() != expectedSignalLocation) {
				ok = false;
				failures << "temporary signal context copy action did not preserve exact precision";
			}
		}
		const QString clipboardBeforeStaleTrigger = QApplication::clipboard()
			? QApplication::clipboard()->text() : QString();
		SignalItem* staleSignal = temporarySignal;
		scene->removeItem(temporarySignal);
		delete temporarySignal;
		if (copy)
			copy->trigger();
		QApplication::processEvents();
		if (QApplication::clipboard() && QApplication::clipboard()->text() != clipboardBeforeStaleTrigger) {
			ok = false;
			failures << "stale signal context action changed the clipboard";
		}
		allSignals.removeOne(staleSignal);
		closeContextMenu();
	}

	PassengerItem* contextPassenger = nullptr;
	bool temporaryContextPassenger = false;
	if (scene) {
		for (auto* candidate : scene->items()) {
			auto* passenger = qgraphicsitem_cast<PassengerItem*>(candidate);
			if (passenger && passenger->isVisible()) {
				contextPassenger = passenger;
				break;
			}
		}
	}
	if (!contextPassenger) {
		contextPassenger = new PassengerItem(pax_pixmap_scaled);
		contextPassenger->passengerId = "__e2e_context_passenger__";
		contextPassenger->setPos(scene->itemsBoundingRect().bottomRight() + QPointF(100.0, 100.0));
		scene->addItem(contextPassenger);
		temporaryContextPassenger = true;
	}
	if (contextPassenger) {
		const QString passengerId = QString::fromStdString(contextPassenger->passengerId);
		QMenu* menu = requestContextMenu(contextPassenger->sceneBoundingRect().center(), false);
		QAction* details = findMenuAction(menu, "Show details");
		if (!details || details->icon().isNull()) {
			ok = false;
			failures << "passenger details action is missing refreshed icon";
		} else {
			details->trigger();
			QApplication::processEvents();
		}
		QAction* copy = findMenuAction(menu, "Copy passenger ID");
		if (copy) {
			copy->trigger();
			QApplication::processEvents();
			if (!QApplication::clipboard() || QApplication::clipboard()->text() != passengerId) {
				ok = false;
				failures << "passenger context copy action did not copy passenger ID";
			}
		}
		closeContextMenu();
		if (temporaryContextPassenger) {
			scene->removeItem(contextPassenger);
			delete contextPassenger;
		}
		removePaxInfoIcon();
		paxIconItem = nullptr;
	}

	if (scene && networkView) {
		const QPointF emptyPos = scene->itemsBoundingRect().bottomRight() + QPointF(1000.0, 1000.0);
		auto* temporarySelectionItem = new QGraphicsRectItem(QRectF(0.0, 0.0, 10.0, 10.0));
		temporarySelectionItem->setFlag(QGraphicsItem::ItemIsSelectable, true);
		temporarySelectionItem->setPos(emptyPos);
		scene->addItem(temporarySelectionItem);
		if (!selectedTrainBody) {
			ok = false;
			failures << "cannot establish selection for empty-space context menu";
		} else {
			QGraphicsSceneMouseEvent selectionEvent(QEvent::GraphicsSceneMousePress);
			selectionEvent.setButton(Qt::LeftButton);
			selectionEvent.setButtons(Qt::LeftButton);
			selectionEvent.setScenePos(selectedTrainBody->sceneBoundingRect().center());
			selectionEvent.setWidget(networkView->viewport());
			scene->mousePressEvent(&selectionEvent);
			QApplication::processEvents();
			if (!effect || !infoDockWidget || !infoDockWidget->isVisible()
				|| !m_followAction || !m_followAction->isChecked() || m_followTrainIndex != selectedTrain->index) {
				ok = false;
				failures << "empty-space context menu could not establish selection and follow mode";
			}
		}
		temporarySelectionItem->setSelected(true);
		QMenu* menu = requestContextMenu(emptyPos, false);
		QAction* fit = findMenuAction(menu, "Fit whole network");
		QAction* clear = findMenuAction(menu, "Clear selection");
		QAction* stop = findMenuAction(menu, "Stop following train");
		if (!menu || !fit || !clear || !stop) {
			ok = false;
			failures << "empty-space context menu is incomplete";
		} else {
			fit->trigger();
			QApplication::processEvents();
			clear->trigger();
			QApplication::processEvents();
			stop->trigger();
			QApplication::processEvents();
			if ((infoDockWidget && infoDockWidget->isVisible()) || effect
				|| (m_followAction && m_followAction->isChecked()) || m_followTrainIndex != -1) {
				ok = false;
				failures << "empty-space context actions did not clear selection and follow mode";
			}
			if (temporarySelectionItem->isSelected()) {
				ok = false;
				failures << "empty-space context action left a temporary scene item selected";
			}
		}
		closeContextMenu();
		scene->removeItem(temporarySelectionItem);
		delete temporarySelectionItem;
	} else {
		ok = false;
		failures << "cannot open empty-space context menu without a scene and viewport";
	}

	if (scene && networkView) {
		const std::string stalePassengerId = "__e2e_stale_passenger__";
		auto* temporaryPassenger = new PassengerItem(pax_pixmap_scaled);
		temporaryPassenger->passengerId = stalePassengerId;
		temporaryPassenger->setPos(scene->itemsBoundingRect().bottomRight() + QPointF(100.0, 100.0));
		scene->addItem(temporaryPassenger);
		QMenu* menu = requestContextMenu(temporaryPassenger->sceneBoundingRect().center(), false);
		QAction* copy = findMenuAction(menu, "Copy passenger ID");
		const QString clipboardBefore = QStringLiteral("e2e-stale-sentinel");
		if (QApplication::clipboard())
			QApplication::clipboard()->setText(clipboardBefore);
		const bool infoVisibleBefore = infoDockWidget && infoDockWidget->isVisible();
		const QString infoTitleBefore = infoDockWidget ? infoDockWidget->windowTitle() : QString();
		scene->removeItem(temporaryPassenger);
		delete temporaryPassenger;
		if (copy)
			copy->trigger();
		QApplication::processEvents();
		if (QApplication::clipboard() && QApplication::clipboard()->text() != clipboardBefore) {
			ok = false;
			failures << "stale passenger context action changed the clipboard";
		}
		if ((infoDockWidget && infoDockWidget->isVisible()) != infoVisibleBefore
			|| (infoDockWidget && infoDockWidget->windowTitle() != infoTitleBefore)) {
			ok = false;
			failures << "stale passenger context action changed application state";
		}
		closeContextMenu();
	}

	if (m_followAction && m_followTrainCombo && m_followTrainCombo->count() > 0) {
		m_followAction->setChecked(false);
		if (std::any_of(m_stationOverlays.cbegin(), m_stationOverlays.cend(),
			[](const auto* overlay) { return overlay && overlay->isFollowed(); })) {
			ok = false;
			failures << "follow deactivation left a stale station priority";
		}
		m_followAction->setChecked(true);
		const int followedOverlays = static_cast<int>(std::count_if(m_stationOverlays.cbegin(),
			m_stationOverlays.cend(), [](const auto* overlay) { return overlay && overlay->isFollowed(); }));
		if (!m_stationOverlays.isEmpty() && followedOverlays != 1) {
			ok = false;
			failures << QString("follow activation updated %1 station priorities instead of one")
				.arg(followedOverlays);
		}
		QApplication::processEvents();
		if (!m_followAction->isChecked() || m_followTrainIndex < 0) {
			ok = false;
			failures << "follow mode did not activate";
		} else {
			for (auto* train : allTrains) {
				if (train && train->index == m_followTrainIndex) {
					networkView->centerOn(train->sceneBoundingRect().center());
					break;
				}
			}
		}
	} else {
		ok = false;
		failures << "follow mode controls disappeared";
	}
	if (networkView) {
		networkView->fitToTopology();
		updateViewportOverlays();
		QApplication::processEvents();
		TrainBadgeItem* followedBadge = m_trainBadges.value(m_followTrainIndex, nullptr);
		const bool ordinaryMarkers = std::all_of(m_trainBadges.cbegin(), m_trainBadges.cend(),
			[](const TrainBadgeItem* badge) {
				return !badge || badge->isPromoted()
					|| badge->presentation() == TrainBadgeItem::Presentation::Overview;
			});
		if (!followedBadge || !followedBadge->isPromoted()
				|| followedBadge->presentation() != TrainBadgeItem::Presentation::Detailed
				|| !ordinaryMarkers) {
			ok = false;
			failures << "followed train was not promoted over Fit markers";
		}
	}
	captureScreenshot("QEGTRAIN_E2E_FOLLOW_SCREENSHOT", "follow");
	if (scene && networkView && selectedTrainBody) {
		QGraphicsSceneMouseEvent reselectionEvent(QEvent::GraphicsSceneMousePress);
		reselectionEvent.setButton(Qt::LeftButton);
		reselectionEvent.setButtons(Qt::LeftButton);
		reselectionEvent.setScenePos(selectedTrainBody->sceneBoundingRect().center());
		reselectionEvent.setWidget(networkView->viewport());
		scene->mousePressEvent(&reselectionEvent);
		QApplication::processEvents();
		if (!effect || !infoDockWidget || !infoDockWidget->isVisible() || !trainInfoWidget->isVisible()) {
			ok = false;
			failures << "reselection did not restore the train selection state";
		}
	}
	if (!scene || !networkView) {
		ok = false;
		failures << "cannot clear selection without a scene and viewport";
	} else {
		const QRectF contentBounds = scene->itemsBoundingRect();
		QGraphicsSceneMouseEvent emptyEvent(QEvent::GraphicsSceneMousePress);
		emptyEvent.setButton(Qt::LeftButton);
		emptyEvent.setButtons(Qt::LeftButton);
		emptyEvent.setScenePos(contentBounds.bottomRight() + QPointF(1000.0, 1000.0));
		emptyEvent.setWidget(networkView->viewport());
		scene->mousePressEvent(&emptyEvent);
		QApplication::processEvents();
		if (effect || (infoDockWidget && infoDockWidget->isVisible())) {
			ok = false;
			failures << "empty scene click did not clear selection";
		}
		if (arcInfoWidget->isVisible() || nodeInfoWidget->isVisible() || connectionInfoWidget->isVisible()
			|| signallingInfoWidget->isVisible() || trainInfoWidget->isVisible()) {
			ok = false;
			failures << "empty scene click left an entity pane visible";
		}
		if (trainPaxInfoItem || paxIconInfoItem) {
			ok = false;
			failures << "empty scene click left a temporary passenger overlay";
		}
	}
	if (m_followAction) {
		m_followAction->setChecked(false);
		if (std::any_of(m_stationOverlays.cbegin(), m_stationOverlays.cend(),
			[](const auto* overlay) { return overlay && overlay->isFollowed(); })) {
			ok = false;
			failures << "follow toggle left a stale station priority";
		}
		QApplication::processEvents();
	}
	if (m_followTrainIndex != -1) {
		ok = false;
		failures << "follow toggle did not clear its train index";
	}

	if (ok) {
		const qreal devicePixelRatio = windowHandle() ? windowHandle()->devicePixelRatio() : 1.0;
		std::fprintf(stdout, "E2E_VISUAL_POLISH_DPR_%0.1f\n", devicePixelRatio);
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
	}
	if (!m_worker && allTrains.isEmpty()) {
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
	std::vector<SceneTrainUnit> expectedTrainUnits;
	std::vector<SceneComposition> expectedCompositions;
	std::vector<SceneService> expectedServices;
	std::vector<SceneIncident> expectedIncidents;
	std::vector<SceneTrack> expectedTracks;
	std::vector<SceneNode> expectedNodes;
	std::vector<SceneArc> expectedArcs;
	std::vector<SceneBlock> expectedBlocks;
	std::vector<SceneConnection> expectedConnections;
	std::vector<SceneRoute> expectedRoutes;
	std::vector<SceneBlockDependency> expectedBlockDependencies;
	std::vector<SceneSingleTrackRestriction> expectedSingleTrackRestrictions;
	std::vector<SceneStationBoundary> expectedStationBoundaries;
	std::vector<SceneSignallingArea> expectedSignallingAreas;
	std::vector<SceneStation> expectedStations;
	std::vector<SceneSignal> expectedSignals;
	std::vector<ScenePassenger> expectedPassengers;
	std::string e2ePassengerId;
	std::string e2eJourneyId;
	std::string e2eLegId;
	std::vector<SceneService> expectedNewCaseServices;
	std::vector<SceneIncident> expectedNewCaseIncidents;
	std::vector<SceneEntranceDelay> expectedEntranceDelays;

	auto facetFailure = [&](bool& facetOk, const char* facet, const QString& message) {
		facetOk = false;
		ok = false;
		failures << QString("%1: %2").arg(facet).arg(message);
	};
	auto acceptConfirmation = [this]() {
		QTimer::singleShot(25, this, []() {
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				auto* dialog = qobject_cast<QMessageBox*>(widget);
				if (!dialog || !dialog->isVisible())
					continue;
				if (auto* button = dialog->button(QMessageBox::Yes))
					button->click();
				else
					dialog->done(QMessageBox::Yes);
				break;
			}
		});
	};
	auto cancelConfirmation = [this]() {
		QTimer::singleShot(25, this, []() {
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				auto* dialog = qobject_cast<QMessageBox*>(widget);
				if (!dialog || !dialog->isVisible())
					continue;
				if (auto* button = dialog->button(QMessageBox::Cancel))
					button->click();
				else
					dialog->done(QMessageBox::Cancel);
				break;
			}
		});
	};
	auto discardConfirmation = [this]() {
		QTimer::singleShot(25, this, []() {
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				auto* dialog = qobject_cast<QMessageBox*>(widget);
				if (!dialog || !dialog->isVisible())
					continue;
				if (auto* button = dialog->button(QMessageBox::Discard))
					button->click();
				else
					dialog->done(QMessageBox::Discard);
				break;
			}
		});
	};
	auto acceptUnitChoice = [this](const QString& unitId) {
		QTimer::singleShot(25, this, [unitId]() {
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				auto* dialog = qobject_cast<QInputDialog*>(widget);
				if (!dialog || !dialog->isVisible())
					continue;
				dialog->setTextValue(unitId);
				QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
				break;
			}
		});
	};
	auto sameStop = [](const SceneStop& left, const SceneStop& right) {
		return left.stationId == right.stationId && left.platformId == right.platformId && left.hasPlannedArrival == right.hasPlannedArrival && left.hasPlannedDeparture == right.hasPlannedDeparture && left.plannedArrivalSeconds == right.plannedArrivalSeconds && left.plannedDepartureSeconds == right.plannedDepartureSeconds && left.dwellSeconds == right.dwellSeconds;
	};
	auto sameCompositions = [](const std::vector<SceneComposition>& left, const std::vector<SceneComposition>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [](const SceneComposition& a, const SceneComposition& b) {
			return a.id == b.id && a.units == b.units;
		});
	};
	auto sameTrainUnits = [](const std::vector<SceneTrainUnit>& left, const std::vector<SceneTrainUnit>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [](const SceneTrainUnit& a, const SceneTrainUnit& b) {
			return a.id == b.id && a.hasPhysical == b.hasPhysical && a.physical.mass_of_traction_unit_kg == b.physical.mass_of_traction_unit_kg && a.physical.mass_of_a_wagon_kg == b.physical.mass_of_a_wagon_kg && a.physical.number_of_wagons == b.physical.number_of_wagons && a.physical.max_speed_ms == b.physical.max_speed_ms && a.physical.max_deceleration_ms2 == b.physical.max_deceleration_ms2 && a.physical.frontal_area_m2 == b.physical.frontal_area_m2 && a.physical.resistance_coefficient == b.physical.resistance_coefficient && a.physical.jerk_ms3 == b.physical.jerk_ms3 && a.physical.length_m == b.physical.length_m && a.tractionCurve == b.tractionCurve && a.sourceDataFile == b.sourceDataFile && a.sourceTractionFile == b.sourceTractionFile;
		});
	};
	auto sameServices = [&](const std::vector<SceneService>& left, const std::vector<SceneService>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [&](const SceneService& a, const SceneService& b) {
			if (a.id != b.id || a.operatingCode != b.operatingCode || a.composition != b.composition || a.route != b.route
					|| a.performancePercent != b.performancePercent || a.hasMaximumSpeed != b.hasMaximumSpeed
					|| a.maximumSpeedKmh != b.maximumSpeedKmh || a.through != b.through
					|| a.hasEntryTime != b.hasEntryTime || a.entryTimeSeconds != b.entryTimeSeconds
					|| a.hasRepeat != b.hasRepeat || a.headwaySeconds != b.headwaySeconds
					|| a.hasRepeatCount != b.hasRepeatCount || a.repeatCount != b.repeatCount
					|| a.hasOperatingCodeStep != b.hasOperatingCodeStep
					|| a.operatingCodeStep != b.operatingCodeStep || a.stops.size() != b.stops.size())
				return false;
			return std::equal(a.stops.begin(), a.stops.end(), b.stops.begin(), sameStop);
		});
	};
	auto sameIncidents = [](const std::vector<SceneIncident>& left, const std::vector<SceneIncident>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [](const SceneIncident& a, const SceneIncident& b) {
			return a.id == b.id && a.type == b.type && a.target == b.target
				&& a.startSeconds == b.startSeconds && a.endSeconds == b.endSeconds
				&& a.hasOccurrence == b.hasOccurrence && a.occurrence == b.occurrence
				&& a.hasReducedSpeed == b.hasReducedSpeed
				&& a.reducedSpeedKmh == b.reducedSpeedKmh
				&& a.terminateAtDestination == b.terminateAtDestination
				&& a.hasEndSeconds == b.hasEndSeconds;
		});
	};
	auto sameEntranceDelays = [](const std::vector<SceneEntranceDelay>& left,
			const std::vector<SceneEntranceDelay>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [](const SceneEntranceDelay& a,
				const SceneEntranceDelay& b) {
			return a.serviceId == b.serviceId && a.occurrence == b.occurrence
				&& a.stationId == b.stationId && a.delaySeconds == b.delaySeconds;
		});
	};
	auto sameStations = [](const std::vector<SceneStation>& left, const std::vector<SceneStation>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [](const SceneStation& a, const SceneStation& b) {
			if (a.id != b.id || a.name != b.name || a.hasPosition != b.hasPosition || a.positionKm != b.positionKm || a.platforms.size() != b.platforms.size())
				return false;
			return std::equal(a.platforms.begin(), a.platforms.end(), b.platforms.begin(),
							  [](const ScenePlatform& x, const ScenePlatform& y) {
								  return x.id == y.id && x.nodeIds == y.nodeIds
									  && x.hasLength == y.hasLength && x.lengthM == y.lengthM
									  && x.hasWidth == y.hasWidth && x.widthM == y.widthM;
							  });
		});
	};
	auto samePassengers = [](const std::vector<ScenePassenger>& left,
			const std::vector<ScenePassenger>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [](const ScenePassenger& a,
				const ScenePassenger& b) {
			if (a.id != b.id || a.journeys.size() != b.journeys.size())
				return false;
			return std::equal(a.journeys.begin(), a.journeys.end(), b.journeys.begin(),
				[](const ScenePassengerJourney& x, const ScenePassengerJourney& y) {
				if (x.id != y.id || x.activity != y.activity || x.originStationId != y.originStationId
						|| x.destinationStationId != y.destinationStationId
						|| x.plannedDepartureStartSeconds != y.plannedDepartureStartSeconds
						|| x.plannedDepartureEndSeconds != y.plannedDepartureEndSeconds
						|| x.plannedArrivalStartSeconds != y.plannedArrivalStartSeconds
						|| x.plannedArrivalEndSeconds != y.plannedArrivalEndSeconds
						|| x.legs.size() != y.legs.size())
					return false;
				return std::equal(x.legs.begin(), x.legs.end(), y.legs.begin(),
					[](const ScenePassengerLeg& u, const ScenePassengerLeg& v) {
						return u.id == v.id && u.originStationId == v.originStationId
							&& u.destinationStationId == v.destinationStationId
							&& u.serviceId == v.serviceId && u.occurrence == v.occurrence;
					});
				});
		});
	};
	auto sameSignals = [](const std::vector<SceneSignal>& left, const std::vector<SceneSignal>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [](const SceneSignal& a, const SceneSignal& b) {
			return a.id == b.id && a.protectedSection == b.protectedSection;
		});
	};
	auto sameSignallingAreas = [](const std::vector<SceneSignallingArea>& left,
			const std::vector<SceneSignallingArea>& right) {
		if (left.size() != right.size())
			return false;
		return std::equal(left.begin(), left.end(), right.begin(), [](const SceneSignallingArea& a,
				const SceneSignallingArea& b) {
			return a.id == b.id && a.startKm == b.startKm && a.endKm == b.endKm
				&& a.level == b.level && a.trackId == b.trackId;
		});
	};
	auto sameInfrastructure = [](const SceneModel& left, const std::vector<SceneTrack>& tracks,
			const std::vector<SceneNode>& nodes, const std::vector<SceneArc>& arcs,
			const std::vector<SceneBlock>& blocks, const std::vector<SceneConnection>& connections) {
		if (left.tracks.size() != tracks.size() || left.nodes.size() != nodes.size() || left.arcs.size() != arcs.size() || left.blocks.size() != blocks.size() || left.connections.size() != connections.size())
			return false;
		for (std::size_t i = 0; i < tracks.size(); ++i)
			if (left.tracks[i].id != tracks[i].id)
				return false;
		for (std::size_t i = 0; i < nodes.size(); ++i) {
			if (left.nodes[i].id != nodes[i].id || left.nodes[i].trackId != nodes[i].trackId || left.nodes[i].xKm != nodes[i].xKm || left.nodes[i].yKm != nodes[i].yKm)
				return false;
		}
		for (std::size_t i = 0; i < arcs.size(); ++i) {
			if (left.arcs[i].id != arcs[i].id || left.arcs[i].trackId != arcs[i].trackId || left.arcs[i].fromNodeId != arcs[i].fromNodeId || left.arcs[i].toNodeId != arcs[i].toNodeId || left.arcs[i].curvatureRadiusM != arcs[i].curvatureRadiusM || left.arcs[i].gradientPercent != arcs[i].gradientPercent || left.arcs[i].speedLimitMs != arcs[i].speedLimitMs)
				return false;
		}
		for (std::size_t i = 0; i < blocks.size(); ++i)
			if (left.blocks[i].id != blocks[i].id || left.blocks[i].trackId != blocks[i].trackId || left.blocks[i].lengthKm != blocks[i].lengthKm)
				return false;
		for (std::size_t i = 0; i < connections.size(); ++i)
			if (left.connections[i].id != connections[i].id || left.connections[i].fromNodeId != connections[i].fromNodeId || left.connections[i].toNodeId != connections[i].toNodeId || left.connections[i].hasSpeedLimit != connections[i].hasSpeedLimit || left.connections[i].speedLimitMs != connections[i].speedLimitMs)
				return false;
		return true;
	};
	auto sameBlockReferences = [](const SceneModel& left, const std::vector<SceneRoute>& routes,
			const std::vector<SceneBlockDependency>& dependencies,
			const std::vector<SceneSingleTrackRestriction>& restrictions,
			const std::vector<SceneStationBoundary>& boundaries) {
		if (left.routes.size() != routes.size() || left.blockDependencies.size() != dependencies.size() || left.singleTrackRestrictions.size() != restrictions.size() || left.stationBoundaries.size() != boundaries.size())
			return false;
		for (std::size_t index = 0; index < routes.size(); ++index) {
			if (left.routes[index].id != routes[index].id || left.routes[index].blocks != routes[index].blocks || left.routes[index].hasCorridor != routes[index].hasCorridor || left.routes[index].corridor != routes[index].corridor || left.routes[index].reversed != routes[index].reversed)
				return false;
		}
		for (std::size_t index = 0; index < dependencies.size(); ++index) {
			if (left.blockDependencies[index].block != dependencies[index].block || left.blockDependencies[index].dependsOn != dependencies[index].dependsOn)
				return false;
		}
		for (std::size_t index = 0; index < restrictions.size(); ++index) {
			if (left.singleTrackRestrictions[index].startBlock != restrictions[index].startBlock || left.singleTrackRestrictions[index].endBlock != restrictions[index].endBlock || left.singleTrackRestrictions[index].protectedStartBlock != restrictions[index].protectedStartBlock || left.singleTrackRestrictions[index].protectedEndBlock != restrictions[index].protectedEndBlock)
				return false;
		}
		for (std::size_t index = 0; index < boundaries.size(); ++index) {
			if (left.stationBoundaries[index].entranceBlock != boundaries[index].entranceBlock || left.stationBoundaries[index].hasExitBlock != boundaries[index].hasExitBlock || left.stationBoundaries[index].exitBlock != boundaries[index].exitBlock || left.stationBoundaries[index].direction != boundaries[index].direction)
				return false;
		}
		return true;
	};

	// step a: load scene from env
	QString scenePath = qEnvironmentVariable("QEGTRAIN_E2E_SCENE");
	if (scenePath.isEmpty()) {
		ok = false;
		failures << "scene: no scene path";
	} else {
		bool opened = openSceneDirectory(scenePath);
		if (!opened || !m_sceneLoaded) {
			ok = false;
			failures << "scene: scene did not open";
		} else {
			// simulate a clicked-item highlight; the scene owns the effect, so a
			// reopen must also reset the cached pointer or the next click reuses
			// a deleted effect
			if (scene && !effect) {
				QGraphicsItem* highlightCarrier = scene->addRect(QRectF(0, 0, 1, 1));
				effect = new HighlightEffect(Qt::blue, 1);
				highlightCarrier->setGraphicsEffect(effect);
			}
			bool reopened = openSceneDirectory(scenePath);
			if (!reopened || !m_sceneLoaded || QDir(m_sceneDir).absolutePath() != QDir(scenePath).absolutePath()) {
				ok = false;
				failures << "scene: scene did not reopen";
			}
			if (effect != nullptr) {
				ok = false;
				failures << "scene: highlight effect not reset on reopen";
			}
			if (!allTrains.isEmpty() || !allArcs.isEmpty()) {
				ok = false;
				failures << "scene: legacy scene state not cleared";
			}
			QString alternateScenePath = qEnvironmentVariable("QEGTRAIN_E2E_SCENE_ALT");
			if (!alternateScenePath.isEmpty() && QDir(alternateScenePath).absolutePath() != QDir(scenePath).absolutePath()) {
				bool switched = openSceneDirectory(alternateScenePath);
				if (!switched || !m_sceneLoaded || QDir(m_sceneDir).absolutePath() != QDir(alternateScenePath).absolutePath()) {
					ok = false;
					failures << "scene: alternate scene did not open";
				}
				if (!allTrains.isEmpty() || !allArcs.isEmpty()) {
					ok = false;
					failures << "scene: legacy scene state not cleared on alternate";
				}
				bool restored = openSceneDirectory(scenePath);
				if (!restored || !m_sceneLoaded) {
					ok = false;
					failures << "scene: scene did not reopen after alternate";
				}
			}
			if (QDir(m_sceneDir).absolutePath() != QDir(scenePath).absolutePath()) {
				ok = false;
				failures << "scene: primary scene not restored";
			}
		}
	}

	// step b: create, edit, save, and reopen a blank canonical case before the
	// existing loaded-scene editor facets run.
	{
		bool facetOk = true;
		const QString originalScenePath = scenePath;
		QTemporaryDir newCaseTempDir;
		QString outBase = qEnvironmentVariable("QEGTRAIN_E2E_OUT");
		if (outBase.isEmpty())
			outBase = newCaseTempDir.path();
		if (!m_sceneLoaded || originalScenePath.isEmpty()) {
			facetFailure(facetOk, "new case", "the original scene was not available to restore");
		} else {
			discardConfirmation();
			newScene();
			QApplication::processEvents();
			if (!m_sceneLoaded || !m_sceneDirty || !m_sceneDir.isEmpty() || m_sceneIsBundle || !m_saveSceneAction || !m_saveSceneAction->isEnabled() || !m_saveSceneAsAction || !m_saveSceneAsAction->isEnabled() || !m_saveSceneAsFolderAction || !m_saveSceneAsFolderAction->isEnabled())
				facetFailure(facetOk, "new case", "new case did not start unsaved and dirty with save actions enabled");
			if (!m_runSceneAction || m_runSceneAction->isEnabled() || !ui->actionSimulationStart || ui->actionSimulationStart->isEnabled())
				facetFailure(facetOk, "new case", "Run actions were enabled for an empty case");

			auto* nameEdit = findChild<QLineEdit*>("caseNameEdit");
			auto* descriptionEdit = findChild<QLineEdit*>("caseDescriptionEdit");
			auto* baseTimeEdit = findChild<QLineEdit*>("caseBaseTimeEdit");
			auto* durationEdit = findChild<QDoubleSpinBox*>("caseDurationSecondsEdit");
			auto* bufferEdit = findChild<QDoubleSpinBox*>("caseBufferSecondsEdit");
			auto* recoveryEdit = findChild<QDoubleSpinBox*>("caseRecoveryPercentEdit");
			if (!nameEdit || !descriptionEdit || !baseTimeEdit || !durationEdit || !bufferEdit || !recoveryEdit || !m_caseSettingsDock || !m_caseSettingsDock->isVisible()) {
				facetFailure(facetOk, "new case", "case settings controls or dock are unavailable");
			} else {
				nameEdit->setText("E2E New Case");
				descriptionEdit->setText("new case editor smoke");
				baseTimeEdit->setText("09:15:30");
				durationEdit->setValue(7200.0);
				bufferEdit->setValue(30.0);
				recoveryEdit->setValue(7.5);
				commitPendingCaseSettings();
				if (m_sceneModel.name != "E2E New Case" || m_sceneModel.description != "new case editor smoke" || m_sceneModel.baseTime != "09:15:30" || !m_sceneModel.settings.hasDuration || m_sceneModel.settings.durationSeconds != 7200.0 || !m_sceneModel.settings.hasBufferTime || m_sceneModel.settings.bufferTimeSeconds != 30.0 || !m_sceneModel.settings.hasRecoveryTime || m_sceneModel.settings.recoveryTimePercent != 7.5 || m_startOffsetSeconds != 9 * 3600 + 15 * 60 + 30)
					facetFailure(facetOk, "new case", "case settings edits did not commit all six values");
			}

			auto* infrastructureDock = findChild<QDockWidget*>("infrastructureDock");
			auto* infrastructureFacet = findChild<QComboBox*>("infrastructureFacetCombo");
			auto* infrastructureTable = findChild<QTableWidget*>("infrastructureTable");
			auto* infrastructureAdd = findChild<QPushButton*>("infrastructureAddButton");
				auto* infrastructureDelete = findChild<QPushButton*>("infrastructureDeleteButton");
				auto* routeSectionDetail = findChild<QWidget*>("routeSectionDetailWidget");
				auto* blockTrackFilter = findChild<QComboBox*>("blockTrackFilterCombo");
			auto* blockInsert = findChild<QPushButton*>("blockInsertButton");
			auto* blockMoveUp = findChild<QPushButton*>("blockMoveUpButton");
			auto* blockMoveDown = findChild<QPushButton*>("blockMoveDownButton");
			if (!infrastructureDock || !infrastructureFacet || !infrastructureTable
					|| !infrastructureAdd || !infrastructureDelete || !routeSectionDetail
				|| !blockTrackFilter || !blockInsert || !blockMoveUp || !blockMoveDown) {
				facetFailure(facetOk, "infrastructure", "infrastructure controls or dock are unavailable");
			} else {
				infrastructureDock->show();
				infrastructureDock->raise();
				if (routeSectionDetail->isVisible())
					facetFailure(facetOk, "infrastructure", "route section editor was visible outside Routes");
				auto chooseInfrastructureFacet = [&](const char* facet) {
					const int index = infrastructureFacet->findData(QString::fromLatin1(facet));
					if (index < 0)
						return false;
					infrastructureFacet->setCurrentIndex(index);
					QApplication::processEvents();
					return true;
				};
				auto chooseBlockTrack = [&](const char* trackId) {
					if (!chooseInfrastructureFacet("blocks"))
						return false;
					const int index = blockTrackFilter->findData(QString::fromLatin1(trackId));
					if (index < 0)
						return false;
					blockTrackFilter->setCurrentIndex(index);
					QApplication::processEvents();
					return true;
				};
				auto setInfrastructureCell = [&](const char* facet, int row, int column, const QString& value) {
					if (!chooseInfrastructureFacet(facet) || row < 0 || row >= infrastructureTable->rowCount() || !infrastructureTable->item(row, column))
						return false;
					infrastructureTable->item(row, column)->setText(value);
					QApplication::processEvents();
					return true;
				};
				auto addInfrastructureRow = [&](const char* facet) {
					if (!chooseInfrastructureFacet(facet))
						return false;
					infrastructureAdd->click();
					QApplication::processEvents();
					return true;
				};
				if (!addInfrastructureRow("tracks") || !setInfrastructureCell("tracks", 0, 0, "e2e-main") || !addInfrastructureRow("tracks") || !setInfrastructureCell("tracks", 1, 0, "e2e-yard"))
					facetFailure(facetOk, "infrastructure", "track creation or ID editing did not apply");
				if (!setInfrastructureCell("tracks", 0, 0, "e2e-yard") || m_sceneModel.tracks.size() != 2 || m_sceneModel.tracks[0].id != "e2e-main")
					facetFailure(facetOk, "infrastructure", "duplicate track ID was not rejected safely");
				for (int row = 0; row < 3; ++row) {
					if (!addInfrastructureRow("nodes") || !setInfrastructureCell("nodes", row, 0, QString("e2e-main-node-%1").arg(row)) || !setInfrastructureCell("nodes", row, 1, "e2e-main") || !setInfrastructureCell("nodes", row, 2, QString::number(row)) || !setInfrastructureCell("nodes", row, 3, QString::number(row == 1 ? 0.25 : 0.0)))
						facetFailure(facetOk, "infrastructure", "main-track node field edit did not apply");
				}
				for (int row = 3; row < 6; ++row) {
					const double x = row == 4 ? 1.5 : static_cast<double>(row - 3);
					if (!addInfrastructureRow("nodes") || !setInfrastructureCell("nodes", row, 0, QString("e2e-yard-node-%1").arg(row - 3)) || !setInfrastructureCell("nodes", row, 1, "e2e-yard") || !setInfrastructureCell("nodes", row, 2, QString::number(x)) || !setInfrastructureCell("nodes", row, 3, "1.0"))
						facetFailure(facetOk, "infrastructure", "yard-track node field edit did not apply");
				}
				const QStringList mainNodeIds = {"e2e-main-node-0", "e2e-main-node-1", "e2e-main-node-2"};
				const QStringList yardNodeIds = {"e2e-yard-node-0", "e2e-yard-node-1", "e2e-yard-node-2"};
				for (int row = 0; row < 2; ++row) {
					if (!addInfrastructureRow("arcs") || !setInfrastructureCell("arcs", row, 0, QString("e2e-main-arc-%1").arg(row)) || !setInfrastructureCell("arcs", row, 1, "e2e-main") || !setInfrastructureCell("arcs", row, 2, mainNodeIds[row]) || !setInfrastructureCell("arcs", row, 3, mainNodeIds[row + 1]) || !setInfrastructureCell("arcs", row, 4, row == 0 ? "0" : "1250.5") || !setInfrastructureCell("arcs", row, 5, row == 0 ? "-1.5" : "2.25") || !setInfrastructureCell("arcs", row, 6, row == 0 ? "22.5" : "18.75"))
						facetFailure(facetOk, "infrastructure", "main-track arc field edit did not apply");
				}
				for (int row = 0; row < 2; ++row)
					if (!addInfrastructureRow("arcs")
							|| !setInfrastructureCell("arcs", row + 2, 0, QString("e2e-yard-arc-%1").arg(row))
							|| !setInfrastructureCell("arcs", row + 2, 1, "e2e-yard")
							|| !setInfrastructureCell("arcs", row + 2, 2, yardNodeIds[row])
							|| !setInfrastructureCell("arcs", row + 2, 3, yardNodeIds[row + 1])
							|| !setInfrastructureCell("arcs", row + 2, 4, "0")
							|| !setInfrastructureCell("arcs", row + 2, 5, "0")
							|| !setInfrastructureCell("arcs", row + 2, 6, "16.5"))
						facetFailure(facetOk, "infrastructure", "yard-track arc field edit did not apply");
				if (!chooseBlockTrack("e2e-main"))
					facetFailure(facetOk, "infrastructure", "main block track filter was unavailable");
				for (int row = 0; row < 2; ++row) {
					if (!addInfrastructureRow("blocks") || !setInfrastructureCell("blocks", row, 0, QString("e2e-main-block-%1").arg(row)) || !setInfrastructureCell("blocks", row, 1, "e2e-main") || !setInfrastructureCell("blocks", row, 2, "0.75"))
						facetFailure(facetOk, "infrastructure", "main-track block field edit did not apply");
				}
				if (!chooseBlockTrack("e2e-yard")) {
					facetFailure(facetOk, "infrastructure", "yard block track filter was unavailable");
				} else {
					const std::array<double, 3> yardBlockLengths = {0.5, 1.25, 0.25};
					for (int row = 0; row < 3; ++row)
						if (!addInfrastructureRow("blocks")
								|| !setInfrastructureCell("blocks", row, 0,
									QString("e2e-yard-block-%1").arg(row))
								|| !setInfrastructureCell("blocks", row, 1, "e2e-yard")
								|| !setInfrastructureCell("blocks", row, 2,
									QString::number(yardBlockLengths[static_cast<std::size_t>(row)])))
							facetFailure(facetOk, "infrastructure",
								"yard-track block field edit did not apply");
				}
				if (!chooseBlockTrack("e2e-main")) {
					facetFailure(facetOk, "infrastructure", "main block filter could not be restored");
				} else {
					infrastructureTable->setCurrentCell(1, 0);
					blockInsert->click();
					QApplication::processEvents();
					const int insertedRow = infrastructureTable->currentRow();
					if (insertedRow < 0 || infrastructureTable->rowCount() != 3
						|| !infrastructureTable->item(insertedRow, 0)) {
						facetFailure(facetOk, "infrastructure", "block insert did not create a selected row");
					} else {
						infrastructureTable->item(insertedRow, 0)->setText("e2e-main-block-inserted");
						infrastructureTable->item(insertedRow, 2)->setText("0.5");
						QApplication::processEvents();
						blockMoveUp->click();
						QApplication::processEvents();
						blockMoveDown->click();
						QApplication::processEvents();
						const QStringList expectedOrder = {"e2e-main-block-0",
							"e2e-main-block-inserted", "e2e-main-block-1"};
						const std::array<double, 3> expectedStarts = {0.0, 0.75, 1.25};
						const std::array<double, 3> expectedEnds = {0.75, 1.25, 2.0};
						bool placementColumnsVisible = infrastructureTable->columnCount() == 7
							&& infrastructureTable->rowCount() == 3;
						for (int visibleRow = 0; visibleRow < infrastructureTable->rowCount(); ++visibleRow)
							placementColumnsVisible = placementColumnsVisible
								&& infrastructureTable->item(visibleRow, 0)
								&& infrastructureTable->item(visibleRow, 0)->text() == expectedOrder[visibleRow]
								&& infrastructureTable->item(visibleRow, 3)
								&& infrastructureTable->item(visibleRow, 3)->text() == QString::number(visibleRow + 1)
								&& infrastructureTable->item(visibleRow, 4)
								&& qFuzzyCompare(infrastructureTable->item(visibleRow, 4)->text().toDouble() + 1.0,
									expectedStarts[static_cast<std::size_t>(visibleRow)] + 1.0)
								&& infrastructureTable->item(visibleRow, 5)
								&& qFuzzyCompare(infrastructureTable->item(visibleRow, 5)->text().toDouble() + 1.0,
									expectedEnds[static_cast<std::size_t>(visibleRow)] + 1.0)
								&& infrastructureTable->item(visibleRow, 6)
								&& infrastructureTable->item(visibleRow, 6)->text() == "complete";
						if (!placementColumnsVisible)
							facetFailure(facetOk, "infrastructure", "block placement columns were not visible after reorder");
					}
				}
				const std::size_t blocksBeforeInvalidAdd = m_sceneModel.blocks.size();
				const bool orphanTrackAddDisabled = setInfrastructureCell("blocks", 1, 1, QString())
					&& !infrastructureAdd->isEnabled();
				infrastructureAdd->click();
				addInfrastructureEntity(); // also exercise the handler's defensive check
				QApplication::processEvents();
				if (!orphanTrackAddDisabled || m_sceneModel.blocks.size() != blocksBeforeInvalidAdd)
					facetFailure(facetOk, "infrastructure", "Add accepted an invalid selected block track");
				if (!setInfrastructureCell("blocks", 0, 1, "e2e-main") || !infrastructureAdd->isEnabled())
					facetFailure(facetOk, "infrastructure", "orphan block could not be restored to a valid track");
				if (!addInfrastructureRow("stations") || !setInfrastructureCell("stations", 0, 0, "e2e-station-a") || !setInfrastructureCell("stations", 0, 1, "E2E A") || !setInfrastructureCell("stations", 0, 2, "true") || !setInfrastructureCell("stations", 0, 3, "0.5") || !addInfrastructureRow("stations") || !setInfrastructureCell("stations", 1, 0, "e2e-station-b") || !setInfrastructureCell("stations", 1, 1, "E2E B") || !setInfrastructureCell("stations", 1, 2, "true") || !setInfrastructureCell("stations", 1, 3, "1.5"))
					facetFailure(facetOk, "stations/signalling", "station table authoring did not apply");
				if (!addInfrastructureRow("platforms") || !setInfrastructureCell("platforms", 0, 0, "e2e-station-a") || !setInfrastructureCell("platforms", 0, 1, "e2e-platform-a") || !setInfrastructureCell("platforms", 0, 2, mainNodeIds[0]) || !addInfrastructureRow("platforms") || !setInfrastructureCell("platforms", 1, 0, "e2e-station-b") || !setInfrastructureCell("platforms", 1, 1, "e2e-platform-b") || !setInfrastructureCell("platforms", 1, 2, mainNodeIds[2]))
					facetFailure(facetOk, "stations/signalling", "platform table authoring or station move did not apply");
				if (!addInfrastructureRow("signals") || !setInfrastructureCell("signals", 0, 0, "e2e-signal"))
					facetFailure(facetOk, "stations/signalling", "signal table authoring did not apply");
				if (!chooseInfrastructureFacet("signals")) {
					facetFailure(facetOk, "stations/signalling", "signal section choices were unavailable");
				} else {
					auto* protectedSection = qobject_cast<QComboBox*>(infrastructureTable->cellWidget(0, 1));
					const int blockChoice = protectedSection ? protectedSection->findText(
							QStringLiteral("base block e2e-main-block-inserted / track e2e-main")) : -1;
					if (blockChoice < 0) {
						facetFailure(facetOk, "stations/signalling", "creator-facing signal section choice was missing");
					} else {
						protectedSection->setCurrentIndex(blockChoice);
						QApplication::processEvents();
						if (sceneSignals(m_sceneModel).front().protectedSection
								!= protectedSection->itemData(blockChoice).toString().toStdString())
							facetFailure(facetOk, "stations/signalling", "signal section choice did not commit");
					}
				}
				if (!addInfrastructureRow("signalling_areas"))
					facetFailure(facetOk, "stations/signalling", "signalling area row could not be added");
				const bool signallingAreaStartsInvalid = m_sceneModel.signallingAreas.size() == 1
						&& m_sceneModel.signallingAreas.front().startKm == 0.0
						&& m_sceneModel.signallingAreas.front().endKm == 0.0
						&& m_sceneModel.signallingAreas.front().level == 0
						&& m_sceneModel.signallingAreas.front().trackId.empty();
				if (!signallingAreaStartsInvalid)
					facetFailure(facetOk, "stations/signalling", "Add did not create an inert signalling area row");
				if (!setInfrastructureCell("signalling_areas", 0, 0, "e2e-signalling-area")
						|| !setInfrastructureCell("signalling_areas", 0, 1, "0.25")
						|| !setInfrastructureCell("signalling_areas", 0, 2, "1.75")
						|| !setInfrastructureCell("signalling_areas", 0, 3, "4"))
					facetFailure(facetOk, "stations/signalling", "network-wide signalling area authoring did not apply");
				const bool networkAreaAuthored = m_sceneModel.signallingAreas.size() == 1
						&& m_sceneModel.signallingAreas.front().id == "e2e-signalling-area"
						&& m_sceneModel.signallingAreas.front().level == 4
						&& m_sceneModel.signallingAreas.front().trackId.empty();
				if (!networkAreaAuthored)
					facetFailure(facetOk, "stations/signalling", "network-wide signalling area was not canonical");
				if (!setInfrastructureCell("signalling_areas", 0, 4, "e2e-main")
						|| m_sceneModel.signallingAreas.front().trackId != "e2e-main")
					facetFailure(facetOk, "stations/signalling", "signalling area track-scope edit did not apply");
				const bool areaTrackRenameUpdated = setInfrastructureCell("tracks", 0, 0, "e2e-main-renamed")
						&& m_sceneModel.signallingAreas.front().trackId == "e2e-main-renamed"
						&& setInfrastructureCell("tracks", 0, 0, "e2e-main")
						&& m_sceneModel.signallingAreas.front().trackId == "e2e-main";
				if (!areaTrackRenameUpdated)
					facetFailure(facetOk, "stations/signalling", "track rename did not preserve signalling-area scope");
				if (!addInfrastructureRow("connections") || !setInfrastructureCell("connections", 0, 0, "e2e-switch") || !setInfrastructureCell("connections", 0, 1, mainNodeIds[1]) || !setInfrastructureCell("connections", 0, 2, yardNodeIds[1]) || !setInfrastructureCell("connections", 0, 3, "true") || !setInfrastructureCell("connections", 0, 4, "9.25"))
					facetFailure(facetOk, "stations/signalling", "connection field edit did not apply");
				std::array<std::string, 3> expectedNativeRoute;
				if (!addInfrastructureRow("routes") || !setInfrastructureCell("routes", 0, 0, "e2e-block-route") || !setInfrastructureCell("routes", 0, 2, "true") || !setInfrastructureCell("routes", 0, 3, "e2e-corridor") || !setInfrastructureCell("routes", 0, 4, "false")) {
					facetFailure(facetOk, "stations/signalling", "route table authoring did not apply");
				} else {
					auto* routeCatalog = findChild<QComboBox*>("routeSectionCatalogCombo");
					auto* routeAddSection = findChild<QPushButton*>("routeAddSectionButton");
					auto* routeList = findChild<QListWidget*>("routeSectionList");
					auto* routeRemoveSection = findChild<QPushButton*>("routeRemoveSectionButton");
					auto* routeMoveUp = findChild<QPushButton*>("routeMoveUpButton");
					auto* routeMoveDown = findChild<QPushButton*>("routeMoveDownButton");
					const int mainChoice = routeCatalog ? routeCatalog->findText(
						QStringLiteral("base block e2e-main-block-0 / track e2e-main")) : -1;
					const int yardChoice = routeCatalog ? routeCatalog->findText(
						QStringLiteral("base block e2e-yard-block-2 / track e2e-yard")) : -1;
					int connectionChoice = -1;
					if (routeCatalog)
						for (int index = 0; index < routeCatalog->count(); ++index)
							if (routeCatalog->itemText(index).startsWith("connection e2e-switch /")) {
								connectionChoice = index;
								break;
							}
					if (!routeCatalog || !routeAddSection || !routeList || !routeRemoveSection
							|| !routeMoveUp || !routeMoveDown || mainChoice < 0 || connectionChoice < 0
							|| yardChoice < 0) {
						facetFailure(facetOk, "stations/signalling",
							"route section catalog did not expose the switched path");
					} else {
						const std::array<int, 3> choices = {mainChoice, connectionChoice, yardChoice};
						for (std::size_t index = 0; index < choices.size(); ++index) {
							expectedNativeRoute[index] = routeCatalog->itemData(choices[index]).toString().toStdString();
							routeCatalog->setCurrentIndex(choices[index]);
							routeAddSection->click();
							QApplication::processEvents();
						}
						routeList->setCurrentRow(1);
						routeMoveUp->click();
						QApplication::processEvents();
						routeMoveDown->click();
						QApplication::processEvents();
						routeCatalog->setCurrentIndex(mainChoice);
						routeAddSection->click();
						QApplication::processEvents();
						routeRemoveSection->click();
						QApplication::processEvents();
						if (m_sceneModel.routes.empty()
								|| m_sceneModel.routes.front().blocks
									!= std::vector<std::string>(expectedNativeRoute.begin(), expectedNativeRoute.end()))
							facetFailure(facetOk, "stations/signalling",
								"ordered route section controls did not retain the switched path");
						if (!routeSectionDetail->isVisible() || !chooseInfrastructureFacet("blocks")
								|| routeSectionDetail->isVisible())
							facetFailure(facetOk, "stations/signalling",
								"route section editor did not follow the selected infrastructure facet");
					}
				}
				const auto selectSectionCell = [&](const char* facet, int row, int column,
						const QString& preferredPrefix = QString()) {
					if (!chooseInfrastructureFacet(facet))
						return QString();
					auto* combo = qobject_cast<QComboBox*>(infrastructureTable->cellWidget(row, column));
					if (!combo)
						return QString();
					int choice = -1;
					for (int index = 0; index < combo->count(); ++index) {
						if (!combo->itemData(index).toString().isEmpty()
							&& (preferredPrefix.isEmpty() || combo->itemText(index).startsWith(preferredPrefix))) {
							choice = index;
							break;
						}
					}
					if (choice < 0)
						return QString();
					combo->setCurrentIndex(choice);
					QApplication::processEvents();
					return combo->itemData(choice).toString();
				};
				const auto recreateIncompleteRow = [&](const char* facet, const auto& rows) {
					if (!addInfrastructureRow(facet))
						return false;
					acceptConfirmation();
					infrastructureDelete->click();
					QApplication::processEvents();
					return rows.empty() && addInfrastructureRow(facet);
				};
				if (!recreateIncompleteRow("block_dependencies", m_sceneModel.blockDependencies)) {
					facetFailure(facetOk, "stations/signalling", "dependency row add/delete cycle failed");
				}
				const QString dependencyBlock = selectSectionCell("block_dependencies", 0, 0,
					"connection e2e-switch /");
				const QString dependencyDependsOn = selectSectionCell("block_dependencies", 0, 1,
					"base block e2e-main-block-inserted / track e2e-main");
				if (dependencyBlock.isEmpty() || dependencyDependsOn.isEmpty())
					facetFailure(facetOk, "stations/signalling", "dependency selectors did not expose catalog choices");
				if (!recreateIncompleteRow("single_track_restrictions",
						m_sceneModel.singleTrackRestrictions)) {
					facetFailure(facetOk, "stations/signalling", "restriction row add/delete cycle failed");
				}
				const QString restrictionStart = selectSectionCell("single_track_restrictions", 0, 0,
					"base block e2e-main-block-inserted / track e2e-main");
				const QString restrictionEnd = selectSectionCell("single_track_restrictions", 0, 1,
					"base block e2e-yard-block-2 / track e2e-yard");
				const QString restrictionProtectedStart = selectSectionCell("single_track_restrictions", 0, 2,
					"connection e2e-switch /");
				const QString restrictionProtectedEnd = selectSectionCell("single_track_restrictions", 0, 3,
					"base block e2e-yard-block-2 / track e2e-yard");
				if (restrictionStart.isEmpty() || restrictionEnd.isEmpty() || restrictionProtectedStart.isEmpty() || restrictionProtectedEnd.isEmpty())
					facetFailure(facetOk, "stations/signalling", "restriction selectors did not expose catalog choices");
				if (!recreateIncompleteRow("station_boundaries", m_sceneModel.stationBoundaries)) {
					facetFailure(facetOk, "stations/signalling", "boundary row add/delete cycle failed");
				}
				const QString boundaryEntrance = selectSectionCell("station_boundaries", 0, 0,
					"base block e2e-main-block-inserted / track e2e-main");
				const QString boundaryExit = selectSectionCell("station_boundaries", 0, 1,
					"base block e2e-yard-block-2 / track e2e-yard");
				if (!setInfrastructureCell("station_boundaries", 0, 2, "false")
					|| boundaryEntrance.isEmpty() || boundaryExit.isEmpty())
					facetFailure(facetOk, "stations/signalling", "boundary selectors did not expose entrance and exit choices");
				if (chooseInfrastructureFacet("station_boundaries")) {
					auto* exitCombo = qobject_cast<QComboBox*>(infrastructureTable->cellWidget(0, 1));
					if (exitCombo) {
						const int noneChoice = exitCombo->findData(QString());
						if (noneChoice >= 0)
							exitCombo->setCurrentIndex(noneChoice);
						QApplication::processEvents();
						if (m_sceneModel.stationBoundaries.empty()
							|| m_sceneModel.stationBoundaries.front().hasExitBlock
							|| !m_sceneModel.stationBoundaries.front().exitBlock.empty())
							facetFailure(facetOk, "stations/signalling", "boundary (none) did not clear exit atomically");
					}
				}
		const bool m3TablesAuthored = m_sceneModel.stations.size() == 2 && m_sceneModel.stations[0].platforms.size() == 1 && m_sceneModel.stations[1].platforms.size() == 1 && sceneSignals(m_sceneModel).size() == 1 && m_sceneModel.routes.size() == 1 && m_sceneModel.blockDependencies.size() == 1 && m_sceneModel.singleTrackRestrictions.size() == 1 && m_sceneModel.stationBoundaries.size() == 1;
				if (!m3TablesAuthored)
					facetFailure(facetOk, "stations/signalling", "canonical M3 rows were not created through the table controls");
				if (chooseInfrastructureFacet("platforms")) {
					auto* lengthEdit = qobject_cast<QDoubleSpinBox*>(infrastructureTable->cellWidget(0, 3));
					auto* widthEdit = qobject_cast<QDoubleSpinBox*>(infrastructureTable->cellWidget(0, 4));
					if (!lengthEdit || !widthEdit) {
						facetFailure(facetOk, "platform geometry", "length/width editors were unavailable");
					} else {
						lengthEdit->setValue(123.75);
						widthEdit->setValue(4.25);
						QApplication::processEvents();
						const ScenePlatform& platform = m_sceneModel.stations.front().platforms.front();
						if (!platform.hasLength || platform.lengthM != 123.75
								|| !platform.hasWidth || platform.widthM != 4.25)
							facetFailure(facetOk, "platform geometry", "public geometry edits did not set presence/value");
					}
				} else {
					facetFailure(facetOk, "platform geometry", "platform facet was unavailable");
				}
				const std::vector<SceneDiagnostic> nativeDiagnostics =
					buildInfrastructureAndSignallingFromScene(m_sceneModel);
				bool nativeRouteMatches = !hasErrors(nativeDiagnostics) && N_Routes == 1
					&& train_route.size() == 1 && train_route.front().N_Block_Sections == 3;
				if (nativeRouteMatches)
					for (std::size_t index = 0; index < expectedNativeRoute.size(); ++index)
						nativeRouteMatches = nativeRouteMatches
							&& train_route.front().sequence_of_block_sections[index].ID
								== expectedNativeRoute[index];
				if (!nativeRouteMatches)
					facetFailure(facetOk, "stations/signalling",
						"publicly authored switched route did not retain its order in native staging");
				refreshValidationPanel();
				const std::string renamedBlockId = "e2e-main-block-renamed";
				const bool blockRenamed = setInfrastructureCell("blocks", 1, 0,
												QString::fromStdString(renamedBlockId));
				const SceneSectionInventory renamedInventory = buildSceneSectionInventory(m_sceneModel);
				const auto resolvesThroughRenamedBlock = [&](const std::string& reference) {
					const SceneSectionDescriptor* section = renamedInventory.resolve(reference);
					return section && (section->sourceBlockId == renamedBlockId
						|| section->firstBlockId == renamedBlockId || section->secondBlockId == renamedBlockId);
				};
				const bool blockReferencesUpdated = blockRenamed
					&& std::any_of(m_sceneModel.blocks.begin(), m_sceneModel.blocks.end(),
						[&](const SceneBlock& block) { return block.id == renamedBlockId; })
					&& m_sceneModel.routes.size() == 1 && m_sceneModel.routes[0].blocks.size() == 3
					&& renamedInventory.resolve(m_sceneModel.routes[0].blocks[0])
					&& resolvesThroughRenamedBlock(m_sceneModel.routes[0].blocks[1])
					&& renamedInventory.resolve(m_sceneModel.routes[0].blocks[2])
					&& m_sceneModel.blockDependencies.size() == 1
					&& resolvesThroughRenamedBlock(m_sceneModel.blockDependencies[0].block)
					&& resolvesThroughRenamedBlock(m_sceneModel.blockDependencies[0].dependsOn)
					&& m_sceneModel.singleTrackRestrictions.size() == 1
					&& resolvesThroughRenamedBlock(m_sceneModel.singleTrackRestrictions[0].startBlock)
					&& resolvesThroughRenamedBlock(m_sceneModel.singleTrackRestrictions[0].protectedStartBlock)
					&& m_sceneModel.stationBoundaries.size() == 1
					&& resolvesThroughRenamedBlock(m_sceneModel.stationBoundaries[0].entranceBlock)
					&& !sceneSignals(m_sceneModel).empty()
					&& resolvesThroughRenamedBlock(sceneSignals(m_sceneModel).front().protectedSection);
				if (!blockReferencesUpdated)
					facetFailure(facetOk, "infrastructure", "block ID rename did not update decorated/composite references");
				if (m_addServiceButton && m_serviceListWidget && m_serviceRouteCombo && m_addStopButton && m_stopStationCombo && m_stopPlatformCombo) {
					m_addServiceButton->click();
					QApplication::processEvents();
					// Composition authoring is covered later in this smoke. Keep this
					// deliberately incomplete service structurally reloadable here.
					if (!m_sceneModel.services.empty())
						m_sceneModel.services.front().composition = "e2e-unresolved-composition";
					if (m_serviceListWidget->count() != 1 || m_serviceRouteCombo->findText("e2e-block-route") < 0)
						facetFailure(facetOk, "stations/signalling", "service route choices did not refresh immediately");
					else {
						m_serviceRouteCombo->setCurrentText("e2e-block-route");
						m_addStopButton->click();
						QApplication::processEvents();
						const bool firstStopChoices = m_stopStationCombo->findText("e2e-station-a") >= 0 && m_stopPlatformCombo->findText("e2e-platform-a") >= 0;
						if (!firstStopChoices)
							facetFailure(facetOk, "stations/signalling", "service station/platform choices did not refresh immediately");
						else {
							m_stopStationCombo->setCurrentText("e2e-station-a");
							m_stopPlatformCombo->setCurrentText("e2e-platform-a");
							m_addStopButton->click();
							QApplication::processEvents();
							m_stopStationCombo->setCurrentText("e2e-station-b");
							QApplication::processEvents();
							if (m_stopPlatformCombo->findText("e2e-platform-b") < 0)
								facetFailure(facetOk, "stations/signalling", "moved service stop did not refresh platform choices");
							else {
								m_stopPlatformCombo->setCurrentText("e2e-platform-b");
								QApplication::processEvents();
								const bool platformMoveUpdatedStop = !m_sceneModel.services.empty() && !m_sceneModel.services.front().stops.empty() && setInfrastructureCell("platforms", 1, 0, "e2e-station-a") && m_sceneModel.services.front().stops.back().stationId == "e2e-station-a" && setInfrastructureCell("platforms", 1, 0, "e2e-station-b") && m_sceneModel.services.front().stops.back().stationId == "e2e-station-b";
								if (!platformMoveUpdatedStop)
									facetFailure(facetOk, "stations/signalling",
												 "moving a referenced platform did not keep its service stop usable");
							}
						}
					}
				} else {
					facetFailure(facetOk, "stations/signalling", "service controls unavailable for M3 assignment coverage");
				}
				if (m_addIncidentButton && m_incidentListWidget && m_incidentTargetCombo) {
					m_addIncidentButton->click();
					QApplication::processEvents();
					if (m_incidentTargetCombo->findText("e2e-signal") < 0)
						facetFailure(facetOk, "stations/signalling", "signal incident target choices did not refresh immediately");
					else {
						m_incidentTargetCombo->setCurrentText("e2e-signal");
						if (m_incidentStartSecondsEdit && m_incidentEndSecondsEdit) {
							m_incidentStartSecondsEdit->setText("10");
							m_incidentEndSecondsEdit->setText("20");
							commitIncidentStartSeconds();
							commitIncidentEndSeconds();
						}
						const SceneIncident* signalIncident = selectedIncident();
						if (!signalIncident || signalIncident->target != "e2e-signal")
							facetFailure(facetOk, "stations/signalling",
									"bound signal_failure target did not persist canonically");
					}
					m_addIncidentButton->click();
					QApplication::processEvents();
					const bool blockTargetOffered = m_incidentTargetCombo
						&& m_incidentTargetCombo->findText(QString::fromStdString(renamedBlockId)) >= 0;
					if (!blockTargetOffered) {
						facetFailure(facetOk, "stations/signalling", "block signal_failure target was not offered");
					} else {
						m_incidentTargetCombo->setCurrentText(QString::fromStdString(renamedBlockId));
						commitIncidentTarget(QString::fromStdString(renamedBlockId));
						const SceneIncident* blockIncident = selectedIncident();
						if (!blockIncident || blockIncident->target != renamedBlockId)
							facetFailure(facetOk, "stations/signalling", "block signal_failure target did not persist canonically");
					}
					refreshValidationPanel();
				} else {
					facetFailure(facetOk, "stations/signalling", "incident controls unavailable for signal coverage");
				}
					auto refuseInfrastructureDelete = [&](const char* facet, int row, int column,
						std::size_t expectedSize, const char* label) {
					if (!chooseInfrastructureFacet(facet) || row < 0 || row >= infrastructureTable->rowCount()) {
						facetFailure(facetOk, "delete integrity", QString("%1 facet row unavailable").arg(label));
						return;
					}
					infrastructureTable->setCurrentCell(row, column);
					QApplication::processEvents();
					infrastructureDelete->click();
					QApplication::processEvents();
					const QString facetName = QString::fromLatin1(facet);
					std::size_t actualSize = expectedSize;
					if (facetName == "tracks") actualSize = m_sceneModel.tracks.size();
					else if (facetName == "nodes") actualSize = m_sceneModel.nodes.size();
					else if (facetName == "arcs") actualSize = m_sceneModel.arcs.size();
					else if (facetName == "blocks") actualSize = m_sceneModel.blocks.size();
					else if (facetName == "connections") actualSize = m_sceneModel.connections.size();
					else if (facetName == "stations") actualSize = m_sceneModel.stations.size();
					else if (facetName == "platforms") {
						actualSize = 0;
						for (const auto& station : m_sceneModel.stations)
							actualSize += station.platforms.size();
					} else if (facetName == "signals") actualSize = sceneSignals(m_sceneModel).size();
					else if (facetName == "routes") actualSize = m_sceneModel.routes.size();
					if (actualSize != expectedSize)
						facetFailure(facetOk, "delete integrity", QString("referenced %1 delete was not refused").arg(label));
				};
				refuseInfrastructureDelete("tracks", 0, 0, m_sceneModel.tracks.size(), "track");
				refuseInfrastructureDelete("nodes", 0, 0, m_sceneModel.nodes.size(), "node");
				if (chooseBlockTrack("e2e-main"))
					refuseInfrastructureDelete("blocks", 0, 0, m_sceneModel.blocks.size(), "block");
				else
					facetFailure(facetOk, "delete integrity", "block facet filter unavailable");
				refuseInfrastructureDelete("connections", 0, 0, m_sceneModel.connections.size(), "connection");
				refuseInfrastructureDelete("stations", 0, 0, m_sceneModel.stations.size(), "station");
				std::size_t platformCount = 0;
				for (const auto& station : m_sceneModel.stations)
					platformCount += station.platforms.size();
				refuseInfrastructureDelete("platforms", 0, 1, platformCount, "platform");
				refuseInfrastructureDelete("signals", 0, 0, sceneSignals(m_sceneModel).size(), "signal");
				refuseInfrastructureDelete("routes", 0, 0, m_sceneModel.routes.size(), "route");
				expectedTracks = m_sceneModel.tracks;
				expectedNodes = m_sceneModel.nodes;
				expectedArcs = m_sceneModel.arcs;
				expectedBlocks = m_sceneModel.blocks;
				expectedConnections = m_sceneModel.connections;
				expectedRoutes = m_sceneModel.routes;
				expectedBlockDependencies = m_sceneModel.blockDependencies;
				expectedSingleTrackRestrictions = m_sceneModel.singleTrackRestrictions;
				expectedStationBoundaries = m_sceneModel.stationBoundaries;
				expectedSignallingAreas = m_sceneModel.signallingAreas;
				expectedStations = m_sceneModel.stations;
				expectedSignals = sceneSignals(m_sceneModel);
				expectedNewCaseServices = m_sceneModel.services;
				expectedNewCaseIncidents = defaultScenario(m_sceneModel)
											   ? defaultScenario(m_sceneModel)->incidents
											   : std::vector<SceneIncident>();
				if (loadTrackPreview(m_sceneModel).lines.size() < 2)
					facetFailure(facetOk, "infrastructure", "authored topology did not reach the existing preview");
				if (facetOk)
					std::fprintf(stdout, "E2E_EDITOR_INFRASTRUCTURE_OK\n");
				if (facetOk)
					std::fprintf(stdout, "E2E_EDITOR_STATIONS_SIGNALLING_OK\n");
			}

			const QString newCaseFolder = QDir(outBase).filePath("editor_smoke_new_case");
			const QString newCaseBundle = QDir(outBase).filePath("editor_smoke_new_case.egscene");
			const QString absentSettingsFolder = QDir(outBase).filePath("editor_smoke_new_case_absent_settings");
			if (!QDir().mkpath(outBase))
				facetFailure(facetOk, "new case", "output directory could not be created");
			QDir(newCaseFolder).removeRecursively();
			QDir(absentSettingsFolder).removeRecursively();
			QFile::remove(newCaseBundle);
			const SceneSaveResult folderSave = ::saveScene(m_sceneModel, newCaseFolder.toStdString());
			const SceneSaveResult bundleSave = saveSceneBundle(m_sceneModel, newCaseBundle.toStdString());
			if (!folderSave.success() || !bundleSave.success())
				facetFailure(facetOk, "new case", "folder or bundle save failed");
			std::string expectedName = "E2E New Case";
			std::string expectedDescription = "new case editor smoke";
			const auto verifyNewCase = [&]() {
				const SceneScenario* scenario = defaultScenario(static_cast<const SceneModel&>(m_sceneModel));
				return m_sceneLoaded && !m_sceneDirty && m_sceneModel.schemaVersion == 1 && m_sceneModel.name == expectedName && m_sceneModel.description == expectedDescription && m_sceneModel.baseTime == "09:15:30" && m_sceneModel.settings.hasDuration && m_sceneModel.settings.durationSeconds == 7200.0 && m_sceneModel.settings.hasBufferTime && m_sceneModel.settings.bufferTimeSeconds == 30.0 && m_sceneModel.settings.hasRecoveryTime && m_sceneModel.settings.recoveryTimePercent == 7.5 && m_sceneModel.defaultScenarioId == "baseline" && scenario && scenario->id == "baseline" && scenario->name == "Baseline" && sameIncidents(scenario->incidents, expectedNewCaseIncidents) && scenario->entranceDelays.empty() && (expectedTracks.empty() || sameInfrastructure(m_sceneModel, expectedTracks, expectedNodes, expectedArcs, expectedBlocks, expectedConnections)) && (expectedRoutes.empty() || sameBlockReferences(m_sceneModel, expectedRoutes, expectedBlockDependencies, expectedSingleTrackRestrictions, expectedStationBoundaries)) && (expectedSignallingAreas.empty() || sameSignallingAreas(m_sceneModel.signallingAreas, expectedSignallingAreas)) && (expectedStations.empty() || sameStations(m_sceneModel.stations, expectedStations)) && (expectedSignals.empty() || sameSignals(sceneSignals(m_sceneModel), expectedSignals)) && (expectedNewCaseServices.empty() || sameServices(m_sceneModel.services, expectedNewCaseServices));
			};
			if (!openSceneDirectory(newCaseFolder) || !verifyNewCase())
				facetFailure(facetOk, "new case", "folder save did not reopen with the edited settings");

			const QString savedName = QStringLiteral("E2E New Case Saved");
			const QString savedDescription = QStringLiteral("new case editor smoke saved");
			if (nameEdit && descriptionEdit && m_caseSettingsDock) {
				activateWindow();
				m_caseSettingsDock->show();
				m_caseSettingsDock->raise();
				descriptionEdit->setText(savedDescription);
				commitCaseSettings();
				nameEdit->setFocus();
				nameEdit->setText(savedName);
				QApplication::processEvents();
				if (!m_saveSceneAction || !m_saveSceneAction->isEnabled() || QApplication::focusWidget() != nameEdit)
					facetFailure(facetOk, "new case", "Save action was not ready for focused pending case text");
				else
					m_saveSceneAction->trigger();
				expectedName = savedName.toStdString();
				expectedDescription = savedDescription.toStdString();
				if (!openSceneDirectory(newCaseFolder) || !verifyNewCase()
						|| !nameEdit || nameEdit->text() != savedName)
					facetFailure(facetOk, "new case", "Save action did not persist focused pending case text");
			} else {
				facetFailure(facetOk, "new case", "case settings controls or dock are unavailable for Save action");
			}

			QFile::remove(newCaseBundle);
			const SceneSaveResult updatedBundleSave = saveSceneBundle(m_sceneModel, newCaseBundle.toStdString());
			if (!updatedBundleSave.success() || !openSceneDirectory(newCaseBundle) || !verifyNewCase())
				facetFailure(facetOk, "new case", "bundle save did not reopen with the edited settings");
			if (!openSceneDirectory(newCaseFolder) || !verifyNewCase())
				facetFailure(facetOk, "new case", "saved folder could not be restored for New action check");

			const QString pendingNewName = QStringLiteral("E2E Pending New Case");
			if (nameEdit && m_caseSettingsDock && m_newSceneAction) {
				m_caseSettingsDock->show();
				m_caseSettingsDock->raise();
				nameEdit->setFocus();
				nameEdit->setText(pendingNewName);
				QApplication::processEvents();
				const bool cleanBeforeNew = !m_sceneDirty
					&& m_sceneModel.name == expectedName
					&& nameEdit->text() == pendingNewName
					&& QApplication::focusWidget() == nameEdit;
				if (!cleanBeforeNew)
					facetFailure(facetOk, "new case", "New action precondition was not a clean scene with focused pending text");
				else {
					cancelConfirmation();
					m_newSceneAction->trigger();
					QApplication::processEvents();
					if (m_sceneDir != QFileInfo(newCaseFolder).absoluteFilePath()
							|| !m_sceneDirty || m_sceneModel.name != pendingNewName.toStdString()
							|| nameEdit->text() != pendingNewName)
						facetFailure(facetOk, "new case", "Cancel did not preserve the dirty scene after pending New action text");
				}
			} else {
				facetFailure(facetOk, "new case", "New action or case settings controls are unavailable");
			}

			SceneModel absentSettings = makeNewSceneModel();
			absentSettings.name = "E2E Absent Settings";
			absentSettings.description = "metadata-only baseline";
			absentSettings.settings.hasDuration = false;
			absentSettings.settings.durationSeconds = 0.0;
			absentSettings.settings.hasBufferTime = false;
			absentSettings.settings.bufferTimeSeconds = 0.0;
			absentSettings.settings.hasRecoveryTime = false;
			absentSettings.settings.recoveryTimePercent = 0.0;
			const SceneSaveResult absentSave = ::saveScene(absentSettings, absentSettingsFolder.toStdString());
			if (!absentSave.success() || !openSceneDirectory(absentSettingsFolder)
					|| m_sceneModel.settings.hasDuration || m_sceneModel.settings.hasBufferTime
					|| m_sceneModel.settings.hasRecoveryTime)
				facetFailure(facetOk, "new case", "canonical scene with absent optional settings did not load");
			else if (descriptionEdit && m_caseSettingsDock) {
				m_caseSettingsDock->show();
				m_caseSettingsDock->raise();
				descriptionEdit->setText("metadata-only edit");
				commitCaseSettings();
				const bool flagsRemainAbsent = !m_sceneModel.settings.hasDuration
					&& !m_sceneModel.settings.hasBufferTime && !m_sceneModel.settings.hasRecoveryTime;
				if (!flagsRemainAbsent || !m_saveSceneAction || !m_saveSceneAction->isEnabled())
					facetFailure(facetOk, "new case", "metadata-only edit did not keep optional settings absent before Save");
				else {
					m_saveSceneAction->trigger();
					if (m_sceneDirty || m_sceneModel.settings.hasDuration || m_sceneModel.settings.hasBufferTime
							|| m_sceneModel.settings.hasRecoveryTime
							|| !openSceneDirectory(absentSettingsFolder)
							|| m_sceneModel.settings.hasDuration || m_sceneModel.settings.hasBufferTime
							|| m_sceneModel.settings.hasRecoveryTime)
						facetFailure(facetOk, "new case", "metadata-only Save created optional settings");
				}
			} else {
				facetFailure(facetOk, "new case", "case settings controls or dock are unavailable for metadata-only Save");
			}
			if (!openSceneDirectory(originalScenePath))
				facetFailure(facetOk, "new case", "original scene could not be restored");
		}
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_NEW_CASE_OK\n");
	}

	// step c: exercise the passenger authoring surface, legacy import seam, and
	// native passenger/platform staging entirely through public controls.
	if (m_sceneLoaded) {
		bool facetOk = true;
		auto* passengerDock = findChild<QDockWidget*>("passengerDock");
		auto* passengerList = findChild<QListWidget*>("passengerListWidget");
		auto* passengerAdd = findChild<QPushButton*>("passengerAddButton");
		auto* passengerDelete = findChild<QPushButton*>("passengerDeleteButton");
		auto* passengerImport = findChild<QPushButton*>("passengerImportButton");
		auto* passengerId = findChild<QLineEdit*>("passengerIdEdit");
		auto* passengerTabs = findChild<QTabWidget*>("passengerEditorTabs");
		auto* journeyList = findChild<QListWidget*>("passengerJourneyListWidget");
		auto* journeyAdd = findChild<QPushButton*>("passengerJourneyAddButton");
		auto* journeyDelete = findChild<QPushButton*>("passengerJourneyDeleteButton");
		auto* journeyId = findChild<QLineEdit*>("passengerJourneyIdEdit");
		auto* journeyActivity = findChild<QLineEdit*>("passengerJourneyActivityEdit");
		auto* journeyOrigin = findChild<QComboBox*>("passengerJourneyOriginCombo");
		auto* journeyDestination = findChild<QComboBox*>("passengerJourneyDestinationCombo");
		auto* legList = findChild<QListWidget*>("passengerLegListWidget");
		auto* legAdd = findChild<QPushButton*>("passengerLegAddButton");
		auto* legDelete = findChild<QPushButton*>("passengerLegDeleteButton");
		auto* legMoveUp = findChild<QPushButton*>("passengerLegMoveUpButton");
		auto* legMoveDown = findChild<QPushButton*>("passengerLegMoveDownButton");
		auto* legId = findChild<QLineEdit*>("passengerLegIdEdit");
		auto* legOrigin = findChild<QComboBox*>("passengerLegOriginCombo");
		auto* legDestination = findChild<QComboBox*>("passengerLegDestinationCombo");
		auto* legService = findChild<QComboBox*>("passengerLegServiceCombo");
		auto* legOccurrence = findChild<QSpinBox*>("passengerLegOccurrenceSpin");
		auto* importResults = findChild<QTableWidget*>("passengerImportResultTable");
		const auto commitLineEdit = [](QLineEdit* edit) {
			return edit && QMetaObject::invokeMethod(edit, "editingFinished", Qt::DirectConnection);
		};
		const auto selectData = [](QComboBox* combo, const QString& value) {
			if (!combo)
				return false;
			const int index = combo->findData(value);
			if (index < 0)
				return false;
			combo->setCurrentIndex(index);
			QApplication::processEvents();
			return combo->currentData().toString() == value;
		};
		if (!passengerDock || !passengerList || !passengerAdd || !passengerDelete
				|| !passengerImport || !passengerId || !passengerTabs || !journeyList
				|| !journeyAdd || !journeyDelete || !journeyId || !journeyActivity
				|| !journeyOrigin || !journeyDestination || !legList || !legAdd
				|| !legDelete || !legMoveUp || !legMoveDown || !legId || !legOrigin
				|| !legDestination || !legService || !legOccurrence || !importResults) {
			facetFailure(facetOk, "passenger", "public passenger controls were unavailable");
		} else {
			passengerDock->show();
			passengerDock->raise();
			QApplication::processEvents();
			const SceneService* passengerService = nullptr;
			std::string originStationId;
			std::string destinationStationId;
			for (const auto& service : m_sceneModel.services) {
				for (std::size_t first = 0; first < service.stops.size() && !passengerService; ++first) {
					for (std::size_t last = first + 1; last < service.stops.size(); ++last) {
						if (service.stops[first].stationId == service.stops[last].stationId)
							continue;
						const bool knownOrigin = std::any_of(m_sceneModel.stations.begin(),
							m_sceneModel.stations.end(), [&](const SceneStation& station) {
								return station.id == service.stops[first].stationId;
							});
						const bool knownDestination = std::any_of(m_sceneModel.stations.begin(),
							m_sceneModel.stations.end(), [&](const SceneStation& station) {
								return station.id == service.stops[last].stationId;
							});
						if (!knownOrigin || !knownDestination)
							continue;
						passengerService = &service;
						originStationId = service.stops[first].stationId;
						destinationStationId = service.stops[last].stationId;
						break;
					}
				}
				if (passengerService)
					break;
			}
			if (!passengerService) {
				facetFailure(facetOk, "passenger", "no service with two canonical station stops was available");
			} else {
				e2ePassengerId = uniquePassengerId("e2e_passenger");
				const int passengerCountBefore = passengerList->count();
				passengerAdd->click();
				QApplication::processEvents();
				if (passengerList->count() != passengerCountBefore + 1) {
					facetFailure(facetOk, "passenger", "Add did not create a passenger row");
				} else {
					passengerId->setText(QString::fromStdString(e2ePassengerId));
					if (!commitLineEdit(passengerId))
						facetFailure(facetOk, "passenger", "passenger ID edit did not commit");
					e2eJourneyId = uniquePassengerJourneyId("e2e_journey");
					journeyAdd->click();
					QApplication::processEvents();
					ScenePassenger* authoredPassenger = selectedPassenger();
					if (!authoredPassenger || authoredPassenger->journeys.empty()) {
						facetFailure(facetOk, "passenger", "Journey Add did not create a selected journey");
					} else {
						journeyId->setText(QString::fromStdString(e2eJourneyId));
						journeyActivity->setText("e2e commute");
						if (!commitLineEdit(journeyId) || !commitLineEdit(journeyActivity)
								|| !selectData(journeyOrigin, QString::fromStdString(originStationId))
								|| !selectData(journeyDestination, QString::fromStdString(destinationStationId)))
							facetFailure(facetOk, "passenger", "journey fields or typed station references did not commit");
						const std::array<double, 4> windows = {100.0, 200.0, 300.0, 400.0};
						for (std::size_t index = 0; index < windows.size(); ++index)
							if (m_passengerJourneyWindowEdits[index])
								m_passengerJourneyWindowEdits[index]->setValue(windows[index]);
						passengerTabs->setCurrentIndex(1);
						legAdd->click();
						QApplication::processEvents();
						e2eLegId = uniquePassengerLegId("e2e_leg");
						const auto configureLeg = [&](const std::string& id) {
							legId->setText(QString::fromStdString(id));
							const bool idOk = commitLineEdit(legId);
							const bool serviceOk = selectData(legService,
								QString::fromStdString(passengerService->id));
							const bool originOk = selectData(legOrigin,
								QString::fromStdString(originStationId));
							const bool destinationOk = selectData(legDestination,
								QString::fromStdString(destinationStationId));
							legOccurrence->setValue(1);
							QApplication::processEvents();
							return idOk && serviceOk && originOk && destinationOk
								&& selectedPassengerLeg() && selectedPassengerLeg()->id == id
								&& selectedPassengerLeg()->serviceId == passengerService->id
								&& selectedPassengerLeg()->occurrence == 1;
						};
						if (!configureLeg(e2eLegId))
							facetFailure(facetOk, "passenger", "leg ID, typed stops, service, or occurrence did not commit");
						const std::string secondLegId = uniquePassengerLegId("e2e_leg_second");
						legAdd->click();
						QApplication::processEvents();
						if (!configureLeg(secondLegId))
							facetFailure(facetOk, "passenger", "second leg authoring did not commit");
						legMoveUp->click();
						QApplication::processEvents();
						const bool movedUp = selectedPassengerJourney() && selectedPassengerJourney()->legs.size() >= 2
							&& selectedPassengerJourney()->legs[0].id == secondLegId;
						legMoveDown->click();
						QApplication::processEvents();
						const bool movedDown = selectedPassengerJourney() && selectedPassengerJourney()->legs.size() >= 2
							&& selectedPassengerJourney()->legs[0].id == e2eLegId;
						if (!movedUp || !movedDown)
							facetFailure(facetOk, "passenger", "leg Move Up/Down did not retain order");
						acceptConfirmation();
						legDelete->click();
						QApplication::processEvents();
						if (!selectedPassengerJourney() || selectedPassengerJourney()->legs.size() != 1
								|| selectedPassengerJourney()->legs.front().id != e2eLegId)
							facetFailure(facetOk, "passenger", "leg Delete did not remove only the temporary leg");

						const std::size_t journeyCountBefore = selectedPassenger()
							? selectedPassenger()->journeys.size() : 0;
						journeyAdd->click();
						QApplication::processEvents();
						const std::string temporaryJourneyId = uniquePassengerJourneyId("e2e_temp_journey");
						journeyId->setText(QString::fromStdString(temporaryJourneyId));
						commitLineEdit(journeyId);
						acceptConfirmation();
						journeyDelete->click();
						QApplication::processEvents();
						if (!selectedPassenger() || selectedPassenger()->journeys.size() != journeyCountBefore)
							facetFailure(facetOk, "passenger", "Journey Delete did not remove the temporary journey");
						const int passengerCountWithAuthor = passengerList->count();
						passengerAdd->click();
						QApplication::processEvents();
						const std::string temporaryPassengerId = uniquePassengerId("e2e_temp_passenger");
						passengerId->setText(QString::fromStdString(temporaryPassengerId));
						commitLineEdit(passengerId);
						acceptConfirmation();
						passengerDelete->click();
						QApplication::processEvents();
						if (passengerList->count() != passengerCountWithAuthor)
							facetFailure(facetOk, "passenger", "Passenger Delete did not remove the temporary passenger");

						QTemporaryDir passengerFixture;
						const QString passengerDir = passengerFixture.isValid()
							? QDir(passengerFixture.path()).filePath("Passengers") : QString();
						const bool fixtureReady = !passengerDir.isEmpty() && QDir().mkpath(passengerDir)
							&& writeCsvFile(QDir(passengerDir).filePath("DAS_FrenchCaseStudy.csv"),
								"c0,person_id,c2,c3,journey_no,activity,destination,c7,c8,c9,arrival,c11,origin,c13,departure\n"
								+ std::string("x,e2e_imported,x,x,1,import,") + destinationStationId
								+ ",x,x,x,8.25,x," + originStationId + ",x,7.25\n"
								+ "x," + e2ePassengerId + ",x,x,1,collision," + destinationStationId
								+ ",x,x,x,8.25,x," + originStationId + ",x,7.25\n"
								+ "x,e2e_unresolved,x,x,1,unresolved," + destinationStationId
								+ ",x,x,x,8.25,x,missing-origin,x,7.25\n"
								+ "malformed,row\n")
							&& writeCsvFile(QDir(passengerDir).filePath("RouteChoiceFC_EQ1.csv"),
								"person_id,destination,nb_transfers,r_service_lines_id_1\n"
								+ std::string("e2e_imported,") + destinationStationId + ",0,"
								+ (passengerService->operatingCode.empty() ? passengerService->id
									: passengerService->operatingCode) + "-1\n"
								+ e2ePassengerId + "," + destinationStationId + ",0,"
								+ (passengerService->operatingCode.empty() ? passengerService->id
									: passengerService->operatingCode) + "-1\n"
								+ "e2e_unresolved," + destinationStationId + ",0,missing-service-1\n"
								+ "bad\n");
						if (!fixtureReady) {
							facetFailure(facetOk, "passenger import", "could not create the exact legacy passenger fixture");
						} else {
							qputenv("QEGTRAIN_E2E_PASSENGER_SOURCE", passengerFixture.path().toUtf8());
							passengerImport->click();
							QApplication::processEvents();
							qunsetenv("QEGTRAIN_E2E_PASSENGER_SOURCE");
							bool sawAccepted = false;
							bool sawRejected = false;
							bool sawUnresolved = false;
							int rejectedCollisionRows = 0;
							for (int row = 0; row < importResults->rowCount(); ++row) {
								const QTableWidgetItem* statusItem = importResults->item(row, 2);
								if (!statusItem)
									continue;
								sawAccepted = sawAccepted || statusItem->text() == "Accepted";
								sawRejected = sawRejected || statusItem->text() == "Rejected";
								sawUnresolved = sawUnresolved || statusItem->text() == "Unresolved";
								if (statusItem->text() == "Rejected" && importResults->item(row, 1)
										&& importResults->item(row, 1)->text() == "2")
									++rejectedCollisionRows;
							}
							if (!sawAccepted || !sawRejected || !sawUnresolved || rejectedCollisionRows < 2)
								facetFailure(facetOk, "passenger import", "result table did not expose accepted, rejected, and unresolved rows");
							const auto findPassengerRow = [&](const std::string& id) {
								const auto matches = passengerList->findItems(QString::fromStdString(id), Qt::MatchExactly);
								return matches.isEmpty() ? -1 : passengerList->row(matches.front());
							};
							const int unresolvedRow = findPassengerRow("e2e_unresolved");
							if (unresolvedRow < 0) {
								facetFailure(facetOk, "passenger import", "unresolved passenger was not appended for correction");
							} else {
								passengerList->setCurrentRow(unresolvedRow);
								passengerTabs->setCurrentIndex(1);
								QApplication::processEvents();
								const bool invalidServiceVisible = legService->currentText().startsWith("Invalid:")
									&& legOrigin->currentText().startsWith("Invalid:");
								if (!invalidServiceVisible)
									facetFailure(facetOk, "passenger import", "unresolved service/station values were not visible as invalid");
								passengerTabs->setCurrentIndex(0);
								acceptConfirmation();
								passengerDelete->click();
								QApplication::processEvents();
								if (findPassengerRow("e2e_unresolved") >= 0)
									facetFailure(facetOk, "passenger import", "invalid imported passenger was not deletable through the public control");
							}
							const auto passengersBeforeFailedImport = m_sceneModel.passengers;
							QTemporaryDir partialFixture;
							const QString partialPassengerDir = QDir(partialFixture.path()).filePath("Passengers");
							const bool partialReady = partialFixture.isValid()
								&& QDir().mkpath(partialPassengerDir)
								&& writeCsvFile(QDir(partialPassengerDir).filePath("DAS_FrenchCaseStudy.csv"),
									"c0,person_id,c2,c3,journey_no,activity,destination,c7,c8,c9,arrival,c11,origin,c13,departure\n");
							if (!partialReady) {
								facetFailure(facetOk, "passenger import", "could not create the partial import fixture");
							} else {
								qputenv("QEGTRAIN_E2E_PASSENGER_SOURCE", partialFixture.path().toUtf8());
								passengerImport->click();
								QApplication::processEvents();
								qunsetenv("QEGTRAIN_E2E_PASSENGER_SOURCE");
								if (!samePassengers(passengersBeforeFailedImport, m_sceneModel.passengers)
										|| importResults->rowCount() != 1 || !importResults->item(0, 2)
										|| importResults->item(0, 2)->text() != "Rejected")
									facetFailure(facetOk, "passenger import", "a failed import changed the scene or hid its rejection");
							}
						}

						const auto infrastructureDiagnostics = buildInfrastructureAndSignallingFromScene(m_sceneModel);
						const auto operationsDiagnostics = buildOperationsFromScene(
							m_sceneModel, m_selectedScenarioId, SceneRunSelection());
						const auto stagedPassenger = std::find_if(AllDailyPassengers.begin(), AllDailyPassengers.end(),
							[&](const Passenger& passenger) { return passenger.ID == e2ePassengerId; });
						bool nativePassengerOk = !hasErrors(infrastructureDiagnostics)
							&& !hasErrors(operationsDiagnostics) && stagedPassenger != AllDailyPassengers.end();
						if (nativePassengerOk) {
							const auto stagedJourney = std::find_if(stagedPassenger->Journeys.begin(),
								stagedPassenger->Journeys.end(), [&](const Journey& journey) {
									return journey.ID == e2eJourneyId;
								});
							nativePassengerOk = stagedJourney != stagedPassenger->Journeys.end();
							if (nativePassengerOk) {
								const auto stagedLeg = std::find_if(stagedJourney->Trips.begin(),
									stagedJourney->Trips.end(), [&](const Trip& trip) {
										return trip.TripID == e2eLegId;
									});
								nativePassengerOk = stagedLeg != stagedJourney->Trips.end();
							}
						}
						bool nativePlatformOk = false;
						if (!m_sceneModel.stations.empty() && !m_sceneModel.stations.front().platforms.empty()) {
							const ScenePlatform& sourcePlatform = m_sceneModel.stations.front().platforms.front();
							const double expectedLength = sourcePlatform.hasLength ? sourcePlatform.lengthM : 100.0;
							const double expectedWidth = sourcePlatform.hasWidth ? sourcePlatform.widthM : 2.5;
							const int expectedCapacity = static_cast<int>((expectedLength * expectedWidth)
								/ (3.14159 * std::pow(0.8, 2)) * 0.8);
							const auto stagedPlatform = std::find_if(AllStationPlatforms.begin(), AllStationPlatforms.end(),
								[&](const StationPlatform& platform) { return platform.ID == sourcePlatform.id; });
							if (stagedPlatform != AllStationPlatforms.end())
								nativePlatformOk = stagedPlatform->length == expectedLength
									&& stagedPlatform->width == expectedWidth
									&& stagedPlatform->Max_Passenger_Volume == expectedCapacity;
					}
						if (!nativePassengerOk || !nativePlatformOk)
							facetFailure(facetOk, "passenger native staging", "passenger leg or platform geometry did not reach native staging");
					}
				}
			}
		}
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_PASSENGER_OK\n");
	}

	// step b: validation assertions
	if (m_sceneLoaded) {
		if (!m_loadedDataTree || m_loadedDataTree->topLevelItemCount() <= 0) {
			ok = false;
			failures << "validation: loaded data tree empty";
		} else if (m_loadedDataTree->topLevelItem(0)->childCount() <= 0) {
			ok = false;
			failures << "validation: loaded data tree lacks drilldown rows";
		}
		if (hasErrors(m_sceneDiagnostics)) {
			ok = false;
			failures << "validation: errors on open";
		}
		if (!m_compositionListWidget || m_compositionListWidget->count() <= 0) {
			ok = false;
			failures << "composition: pane empty";
		}
		if (!m_serviceListWidget || m_serviceListWidget->count() <= 0) {
			ok = false;
			failures << "service: pane empty";
		}
		bool scenarioOk = m_scenarioListWidget && m_scenarioListWidget->count() > 0
			&& m_selectedScenarioId == m_sceneModel.defaultScenarioId;
		if (!scenarioOk) {
			ok = false;
			failures << "scenario: canonical default was not initially selected";
		} else {
			const int originalScenarioCount = m_scenarioListWidget->count();
			if (!m_blankScenarioButton || !m_duplicateScenarioButton || !m_deleteScenarioButton) {
				ok = false;
				failures << "scenario: scenario action buttons unavailable";
			} else {
				m_blankScenarioButton->click();
				QApplication::processEvents();
				m_duplicateScenarioButton->click();
				QApplication::processEvents();
			}
			if (m_scenarioListWidget->count() != originalScenarioCount + 2) {
				ok = false;
				failures << "scenario: blank or duplicate did not add an isolated scenario";
			}
			m_scenarioListWidget->setCurrentRow(0);
			QApplication::processEvents();
			if (m_selectedScenarioId != m_sceneModel.defaultScenarioId) {
				ok = false;
				failures << "scenario: selection did not return to canonical default";
			}
			const int countBeforeDefaultDelete = m_scenarioListWidget->count();
			m_deleteScenarioButton->click();
			deleteScenario();
			QApplication::processEvents();
			if (m_scenarioListWidget->count() != countBeforeDefaultDelete
					|| m_selectedScenarioId != m_sceneModel.defaultScenarioId
					|| m_deleteScenarioButton->isEnabled()) {
				ok = false;
				failures << "scenario: default deletion was not refused or button stayed enabled";
			}
			const int nonDefaultRow = m_scenarioListWidget->count() - 1;
			m_scenarioListWidget->setCurrentRow(nonDefaultRow);
			QApplication::processEvents();
			acceptConfirmation();
			m_deleteScenarioButton->click();
			QApplication::processEvents();
			if (m_scenarioListWidget->count() != originalScenarioCount + 1
					|| m_selectedScenarioId != m_sceneModel.defaultScenarioId
					|| !defaultScenario(static_cast<const SceneModel&>(m_sceneModel))) {
				ok = false;
				failures << "scenario: non-default deletion did not select a valid default";
			}
			if (m_scenarioListWidget->count() > originalScenarioCount) {
				for (int row = 0; row < m_scenarioListWidget->count(); ++row) {
					if (m_sceneModel.scenarios[static_cast<std::size_t>(row)].id == m_sceneModel.defaultScenarioId)
						continue;
					m_scenarioListWidget->setCurrentRow(row);
					acceptConfirmation();
					m_deleteScenarioButton->click();
					QApplication::processEvents();
					break;
				}
			}
		}
		bool explorerOk = m_loadedDataTree && m_loadedDataTree->topLevelItemCount() > 0;
		QTreeWidgetItem* caseRoot = explorerOk ? m_loadedDataTree->topLevelItem(0) : nullptr;
		explorerOk = explorerOk && caseRoot->text(0) == "Case Study"
			&& caseRoot->text(3) != "Invalid" && caseRoot->text(3) != "Failed";
		for (const QString& category : {QStringLiteral("Source path"), QStringLiteral("Canonical schema version"),
				QStringLiteral("Runtime"), QStringLiteral("Results")}) {
			const QList<QTreeWidgetItem*> rows = m_loadedDataTree->findItems(
				category, Qt::MatchExactly | Qt::MatchRecursive, 0);
			QTreeWidgetItem* row = rows.isEmpty() ? nullptr : rows.front();
			if (!row || row->text(3).isEmpty())
				explorerOk = false;
		}
		if (m_sceneIsBundle) {
			const QList<QTreeWidgetItem*> bundleRows = m_loadedDataTree->findItems(
				"Bundle format version", Qt::MatchExactly | Qt::MatchRecursive, 0);
			QTreeWidgetItem* bundleRow = bundleRows.isEmpty() ? nullptr : bundleRows.front();
			if (!bundleRow || bundleRow->text(1) != "1")
				explorerOk = false;
		}
		QTreeWidgetItem* trainTarget = nullptr;
		for (QTreeWidgetItemIterator it(m_loadedDataTree); *it && !trainTarget; ++it) {
			if ((*it)->data(0, kLoadedDataTargetTypeRole).toString() == "train_unit")
				trainTarget = *it;
		}
		if (!trainTarget || !m_trainUnitListWidget) {
			explorerOk = false;
		} else {
			activateLoadedDataItem(trainTarget);
			explorerOk = explorerOk && m_trainUnitListWidget->currentItem()
				&& m_trainUnitListWidget->currentItem()->text() == trainTarget->text(0)
				&& m_trainUnitDock && m_trainUnitDock->isVisible();
		}
		bool hasParameterSource = false;
		bool hasTractionSource = false;
		for (QLabel* label : findChildren<QLabel*>()) {
			hasParameterSource = hasParameterSource || label->text() == "Parameter source reference";
			hasTractionSource = hasTractionSource || label->text() == "Tractive-effort source reference";
		}
		bool hasPlotButton = false;
		for (QPushButton* button : findChildren<QPushButton*>())
				hasPlotButton = hasPlotButton || button->text() == "Plot input traction characteristic";
		const bool hasEditableTrainSources = m_trainUnitSourceDataEdit && m_trainUnitSourceTractionEdit;
		bool hasPlannedArrival = false;
		bool hasPlannedDeparture = false;
		for (QCheckBox* check : findChildren<QCheckBox*>()) {
			hasPlannedArrival = hasPlannedArrival || check->text() == "Planned arrival (s)";
			hasPlannedDeparture = hasPlannedDeparture || check->text() == "Planned departure (s)";
		}
		bool axesOk = false;
		if (m_trainUnitListWidget && m_trainUnitListWidget->count() > 0) {
			m_trainUnitListWidget->setCurrentRow(0);
			if (m_plotTrainUnitTractionButton && m_plotTrainUnitTractionButton->isEnabled()) {
				m_plotTrainUnitTractionButton->click();
				QApplication::processEvents();
				for (QChartView* view : findChildren<QChartView*>()) {
					QChart* chart = view->chart();
					if (!chart)
						continue;
					bool hasSpeedAxis = false;
					bool hasEffortAxis = false;
					for (QAbstractAxis* axis : chart->axes()) {
						hasSpeedAxis = hasSpeedAxis || axis->titleText() == "Speed (km/h)";
						hasEffortAxis = hasEffortAxis || axis->titleText() == "Tractive effort (kN)";
					}
					axesOk = hasSpeedAxis && hasEffortAxis;
					view->window()->close();
				}
			}
		}
		if (!explorerOk || !hasParameterSource || !hasTractionSource || !hasPlotButton || !hasEditableTrainSources
				|| !hasPlannedArrival || !hasPlannedDeparture || !axesOk) {
			ok = false;
			failures << "explorer: load review, activation, generic provenance, plot axes, or timetable labels missing";
		} else {
			std::fprintf(stdout, "E2E_EDITOR_EXPLORER_OK\n");
		}
		expectedCompositions = m_sceneModel.compositions;
		expectedTrainUnits = m_sceneModel.trainUnits;
		expectedServices = m_sceneModel.services;
		expectedIncidents = selectedScenarioIncidents();
	}

	std::string editedTrainUnitId;
	if (!m_sceneLoaded || !m_trainUnitListWidget) {
		bool facetOk = false;
		facetFailure(facetOk, "train unit", "scene or train-unit list unavailable");
	} else {
		bool facetOk = true;
		const int originalCount = m_trainUnitListWidget->count();
		m_trainUnitListWidget->setCurrentRow(0);
		addTrainUnit();
		if (m_trainUnitListWidget->count() != originalCount + 1) {
			facetFailure(facetOk, "train unit", "add did not apply");
		} else {
			m_trainUnitListWidget->setCurrentRow(originalCount);
			const std::string initialTrainUnitId = m_sceneModel.trainUnits[originalCount].id;
			m_compositionListWidget->setCurrentRow(0);
			acceptUnitChoice(QString::fromStdString(initialTrainUnitId));
			addUnitToComposition();
			QApplication::processEvents();
			bool assignedInitialId = false;
			if (m_compositionListWidget->currentRow() >= 0
					&& m_compositionListWidget->currentRow() < static_cast<int>(m_sceneModel.compositions.size())) {
				const auto& units = m_sceneModel.compositions[m_compositionListWidget->currentRow()].units;
				assignedInitialId = std::find(units.begin(), units.end(), initialTrainUnitId) != units.end();
			}
			if (!assignedInitialId)
				facetFailure(facetOk, "train unit", "composition assignment did not apply under the initial id");

			const std::string initialIdBeforeDuplicateRename = m_sceneModel.trainUnits[originalCount].id;
			const std::vector<SceneComposition> compositionsBeforeDuplicateRename = m_sceneModel.compositions;
			if (originalCount <= 0) {
				facetFailure(facetOk, "train unit", "scene lacks another train unit id for duplicate rename coverage");
			} else if (m_trainUnitIdEdit) {
				const std::string conflictingId = m_sceneModel.trainUnits.front().id;
				m_trainUnitIdEdit->setText(QString::fromStdString(conflictingId));
				commitTrainUnitIdEdit();
				const bool selectedIdUnchanged = m_sceneModel.trainUnits[originalCount].id == initialIdBeforeDuplicateRename;
				const bool editorIdRestored = m_trainUnitIdEdit->text().trimmed().toStdString() == initialIdBeforeDuplicateRename;
				const bool referencesUnchanged = sameCompositions(compositionsBeforeDuplicateRename, m_sceneModel.compositions);
				if (!selectedIdUnchanged || !editorIdRestored || !referencesUnchanged)
					facetFailure(facetOk, "train unit", "duplicate-id rename changed the selected unit or composition references");
			}

			editedTrainUnitId = uniqueTrainUnitId("e2e_train_unit");
			if (!m_trainUnitIdEdit) {
				facetFailure(facetOk, "train unit", "id editor unavailable");
			} else {
				m_trainUnitIdEdit->setText(QString::fromStdString(editedTrainUnitId));
				commitTrainUnitIdEdit();
			}
			bool oldReferencePresent = false;
			bool newReferencePresent = false;
			for (const auto& composition : m_sceneModel.compositions) {
				for (const auto& unitId : composition.units) {
					oldReferencePresent = oldReferencePresent || unitId == initialTrainUnitId;
					newReferencePresent = newReferencePresent || unitId == editedTrainUnitId;
				}
			}
			if (oldReferencePresent || !newReferencePresent)
				facetFailure(facetOk, "train unit", "rename did not update composition references");

			m_trainUnitListWidget->setCurrentRow(originalCount);
			const QString initialSourceData = QStringLiteral("e2e/source-data-initial.txt");
			const QString initialSourceTraction = QStringLiteral("e2e/source-traction-initial.txt");
			const QString editedSourceData = QStringLiteral("e2e/source-data-final.txt");
			const QString editedSourceTraction = QStringLiteral("e2e/source-traction-final.txt");
			if (!m_trainUnitSourceDataEdit || !m_trainUnitSourceTractionEdit) {
				facetFailure(facetOk, "train unit", "source reference editors unavailable");
			} else {
				m_trainUnitSourceDataEdit->setText(initialSourceData);
				m_trainUnitSourceTractionEdit->setText(initialSourceTraction);
				QMetaObject::invokeMethod(m_trainUnitSourceDataEdit, "editingFinished", Qt::DirectConnection);
				m_trainUnitSourceDataEdit->setText(editedSourceData);
				m_trainUnitSourceTractionEdit->setText(editedSourceTraction);
				QMetaObject::invokeMethod(m_trainUnitSourceTractionEdit, "editingFinished", Qt::DirectConnection);
				const SceneTrainUnit& editedUnit = m_sceneModel.trainUnits[originalCount];
				if (editedUnit.sourceDataFile != editedSourceData.toStdString()
						|| editedUnit.sourceTractionFile != editedSourceTraction.toStdString())
					facetFailure(facetOk, "train unit", "source reference edits did not commit both fields");
			}

			const double physicalValues[] = {
				101.1234567890123, 51.0000000012345, 2.0, 40.1234567890123,
				1.2000000012345, 9.5000000012345, 0.0300000012345, 0.4000000012345,
				84.1234567890123};
			for (int field = 0; field < 9; ++field) {
				if (m_trainUnitPhysicalEdits[static_cast<size_t>(field)])
					m_trainUnitPhysicalEdits[static_cast<size_t>(field)]->setValue(physicalValues[field]);
				commitTrainUnitPhysical(field);
			}
			const SceneTrainPhysical& committedPhysical = m_sceneModel.trainUnits[originalCount].physical;
			if (committedPhysical.mass_of_traction_unit_kg != physicalValues[0])
				facetFailure(facetOk, "train unit", "traction-unit mass did not commit the requested value");
			if (committedPhysical.mass_of_a_wagon_kg != physicalValues[1])
				facetFailure(facetOk, "train unit", "wagon mass did not commit the requested value");
			if (committedPhysical.number_of_wagons != physicalValues[2])
				facetFailure(facetOk, "train unit", "wagon count did not commit the requested value");
			if (committedPhysical.max_speed_ms != physicalValues[3])
				facetFailure(facetOk, "train unit", "maximum speed did not commit the requested value");
			if (committedPhysical.max_deceleration_ms2 != physicalValues[4])
				facetFailure(facetOk, "train unit", "maximum deceleration did not commit the requested value");
			if (committedPhysical.frontal_area_m2 != physicalValues[5])
				facetFailure(facetOk, "train unit", "frontal area did not commit the requested value");
			if (committedPhysical.resistance_coefficient != physicalValues[6])
				facetFailure(facetOk, "train unit", "resistance coefficient did not commit the requested value");
			if (committedPhysical.jerk_ms3 != physicalValues[7])
				facetFailure(facetOk, "train unit", "jerk did not commit the requested value");
			if (committedPhysical.length_m != physicalValues[8])
				facetFailure(facetOk, "train unit", "length did not commit the requested value");
			const int initialCurveRows = static_cast<int>(m_sceneModel.trainUnits[originalCount].tractionCurve.size());
			addTrainUnitTractionRow();
			if (static_cast<int>(m_sceneModel.trainUnits[originalCount].tractionCurve.size()) != initialCurveRows + 1) {
				facetFailure(facetOk, "train unit", "traction row add did not apply");
			} else if (m_trainUnitTractionTable) {
				const int row = m_trainUnitTractionTable->rowCount() - 1;
				m_trainUnitTractionTable->setCurrentCell(row, 0);
				const std::array<double, 5> tractionValues = {{
					0.123456789012345, 10.1234567890123, 100.1234567890123,
					2.1234567890123, 0.5000000012345}};
				for (int column = 0; column < 5; ++column) {
					auto* edit = qobject_cast<QDoubleSpinBox*>(m_trainUnitTractionTable->cellWidget(row, column));
					if (!edit) {
						facetFailure(facetOk, "train unit", "traction numeric editor unavailable");
						continue;
					}
					edit->setValue(tractionValues[column]);
				}
				addTrainUnitTractionRow();
				if (static_cast<int>(m_sceneModel.trainUnits[originalCount].tractionCurve.size()) != initialCurveRows + 2)
					facetFailure(facetOk, "train unit", "second traction row add did not apply");
				else {
					const int secondRow = m_trainUnitTractionTable->rowCount() - 1;
					auto* secondRowEdit = qobject_cast<QDoubleSpinBox*>(m_trainUnitTractionTable->cellWidget(secondRow, 0));
					if (!secondRowEdit) {
						facetFailure(facetOk, "train unit", "second traction numeric editor unavailable");
					} else {
						secondRowEdit->setValue(10.9876543210987);
						m_trainUnitTractionTable->setCurrentCell(row, 0);
						secondRowEdit->setFocus();
						QMetaObject::invokeMethod(secondRowEdit, "editingFinished", Qt::DirectConnection);
					}
					removeTrainUnitTractionRow();
					if (static_cast<int>(m_sceneModel.trainUnits[originalCount].tractionCurve.size()) != initialCurveRows + 1)
						facetFailure(facetOk, "train unit", "traction row delete did not apply");
					else if (m_sceneModel.trainUnits[originalCount].tractionCurve[initialCurveRows] != tractionValues)
						facetFailure(facetOk, "train unit", "editing a non-current traction row deleted the wrong row");
				}
			}

			m_trainUnitListWidget->setCurrentRow(originalCount);
			duplicateTrainUnit();
			if (m_trainUnitListWidget->count() != originalCount + 2) {
				facetFailure(facetOk, "train unit", "duplicate did not apply");
			} else {
				const int duplicateRow = originalCount + 1;
				m_trainUnitListWidget->setCurrentRow(duplicateRow);
				const std::string duplicateId = m_sceneModel.trainUnits[duplicateRow].id;
				m_compositionListWidget->setCurrentRow(0);
				acceptUnitChoice(QString::fromStdString(duplicateId));
				addUnitToComposition();
				QApplication::processEvents();
				const int referencedCount = m_trainUnitListWidget->count();
				deleteTrainUnit();
				const bool duplicateStillPresent = std::any_of(m_sceneModel.trainUnits.begin(),
					m_sceneModel.trainUnits.end(), [&](const SceneTrainUnit& unit) { return unit.id == duplicateId; });
				if (m_trainUnitListWidget->count() != referencedCount || !duplicateStillPresent)
					facetFailure(facetOk, "train unit", "referenced-unit delete was not refused");
				m_compositionListWidget->setCurrentRow(0);
				for (int unitRow = 0; unitRow < m_compositionUnitsListWidget->count(); ++unitRow) {
					if (m_compositionUnitsListWidget->item(unitRow)->text().toStdString() == duplicateId) {
						m_compositionUnitsListWidget->setCurrentRow(unitRow);
						removeUnitFromComposition();
						break;
					}
				}
				acceptConfirmation();
				deleteTrainUnit();
				if (m_trainUnitListWidget->count() != originalCount + 1
						|| std::any_of(m_sceneModel.trainUnits.begin(), m_sceneModel.trainUnits.end(),
							[&](const SceneTrainUnit& unit) { return unit.id == duplicateId; }))
					facetFailure(facetOk, "train unit", "unreferenced-unit delete did not apply");
			}
		}
		if (!m_sceneModel.trainUnits.empty()) {
			m_trainUnitListWidget->setCurrentRow(0);
			if (!m_trainUnitSourceDataEdit || m_trainUnitSourceDataEdit->text().toStdString()
					!= m_sceneModel.trainUnits.front().sourceDataFile
				|| !m_trainUnitSourceTractionEdit || m_trainUnitSourceTractionEdit->text().toStdString()
					!= m_sceneModel.trainUnits.front().sourceTractionFile)
				facetFailure(facetOk, "train unit", "source references are not visible and unchanged");
		}
		expectedCompositions = m_sceneModel.compositions;
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_TRAIN_UNIT_OK\n");
	}

	std::string editedCompositionId;
	if (!m_sceneLoaded || !m_compositionListWidget || expectedCompositions.empty()) {
		bool facetOk = false;
		facetFailure(facetOk, "composition", "scene or composition list unavailable");
	} else {
		bool facetOk = true;
		const int originalCount = m_compositionListWidget->count();
		int assignedCompositionRow = -1;
		for (int row = 0; row < static_cast<int>(m_sceneModel.compositions.size()); ++row) {
			const std::string& compositionId = m_sceneModel.compositions[static_cast<std::size_t>(row)].id;
			if (std::any_of(m_sceneModel.services.begin(), m_sceneModel.services.end(),
					[&compositionId](const SceneService& service) { return service.composition == compositionId; })) {
				assignedCompositionRow = row;
				break;
			}
		}
		if (assignedCompositionRow < 0) {
			facetFailure(facetOk, "composition", "no service-assigned composition available for rename coverage");
		} else {
			m_compositionListWidget->setCurrentRow(assignedCompositionRow);
			// the selected unit surfaces its source files and a plottable traction curve
			if (m_compositionUnitsListWidget && m_compositionUnitsListWidget->count() > 0 &&
				m_compositionUnitsListWidget->item(0)) {
				m_compositionUnitsListWidget->setCurrentRow(0);
				const SceneTrainUnit* tractionUnit =
					trainUnitById(m_compositionUnitsListWidget->item(0)->text().toStdString());
				if (tractionUnit && !tractionUnit->tractionCurve.empty()) {
					if (!m_plotTractionButton || !m_plotTractionButton->isEnabled())
						facetFailure(facetOk, "composition", "traction plot disabled for a unit with a curve");
					if (sampleTractionCurve(tractionUnit->tractionCurve).empty())
						facetFailure(facetOk, "composition", "traction curve produced no plot samples");
					if (m_compositionUnitSourceDataLabel && m_compositionUnitSourceDataLabel->text().isEmpty())
						facetFailure(facetOk, "composition", "unit source data label empty");
				}
			}
			duplicateComposition();
			if (m_compositionListWidget->count() != originalCount + 1) {
				facetFailure(facetOk, "composition", "duplicate did not apply");
			} else {
				const std::string originalAssignedCompositionId =
					m_sceneModel.compositions[static_cast<std::size_t>(assignedCompositionRow)].id;
				const std::string duplicateCompositionId =
					m_sceneModel.compositions[static_cast<std::size_t>(assignedCompositionRow + 1)].id;
				m_compositionListWidget->setCurrentRow(assignedCompositionRow);
				if (!m_compositionIdEdit) {
					facetFailure(facetOk, "composition", "id editor unavailable");
				} else {
					m_compositionIdEdit->setText(QString());
					commitCompositionIdEdit();
					const bool emptyRejected = m_sceneModel.compositions[static_cast<std::size_t>(assignedCompositionRow)].id
							== originalAssignedCompositionId
						&& m_compositionIdEdit->text() == QString::fromStdString(originalAssignedCompositionId)
						&& m_compositionListWidget->currentRow() == assignedCompositionRow;
					m_compositionIdEdit->setText(QString::fromStdString(duplicateCompositionId));
					commitCompositionIdEdit();
					const bool duplicateRejected = m_sceneModel.compositions[static_cast<std::size_t>(assignedCompositionRow)].id
							== originalAssignedCompositionId
						&& m_compositionIdEdit->text() == QString::fromStdString(originalAssignedCompositionId)
						&& m_compositionListWidget->currentRow() == assignedCompositionRow;
					if (!emptyRejected || !duplicateRejected)
						facetFailure(facetOk, "composition", "empty or duplicate ID was not rejected with the prior text restored");

					editedCompositionId = uniqueCompositionId("e2e_composition");
					m_compositionIdEdit->setText(QString::fromStdString(editedCompositionId));
					commitCompositionIdEdit();
					int migratedServiceCount = 0;
					int staleServiceCount = 0;
					for (const auto& service : m_sceneModel.services) {
						if (service.composition == editedCompositionId)
							++migratedServiceCount;
						if (service.composition == originalAssignedCompositionId)
							++staleServiceCount;
					}
					if (m_sceneModel.compositions[static_cast<std::size_t>(assignedCompositionRow)].id != editedCompositionId
							|| migratedServiceCount <= 0 || staleServiceCount != 0
							|| m_compositionListWidget->currentRow() != assignedCompositionRow
							|| !m_serviceCompositionCombo
							|| m_serviceCompositionCombo->findText(QString::fromStdString(editedCompositionId)) < 0)
						facetFailure(facetOk, "composition", "id edit did not apply");
				}
			}
			const int referencedCompositionCount = m_compositionListWidget->count();
			m_compositionListWidget->setCurrentRow(assignedCompositionRow);
			deleteComposition();
			if (m_compositionListWidget->count() != referencedCompositionCount
					|| !std::any_of(m_sceneModel.compositions.begin(), m_sceneModel.compositions.end(),
						[&](const SceneComposition& composition) {
							return composition.id == editedCompositionId;
						}))
				facetFailure(facetOk, "composition", "service-referenced composition delete was not refused");
			addComposition();
			const std::string temporaryCompositionId = m_sceneModel.compositions.back().id;
			if (m_compositionListWidget->count() != originalCount + 2
					|| !m_serviceCompositionCombo
					|| m_serviceCompositionCombo->findText(
						QString::fromStdString(temporaryCompositionId)) < 0) {
				facetFailure(facetOk, "composition", "add did not refresh the service selector");
			} else {
				acceptConfirmation();
				deleteComposition();
				if (m_compositionListWidget->count() != originalCount + 1
						|| m_serviceCompositionCombo->findText(
							QString::fromStdString(temporaryCompositionId)) >= 0)
					facetFailure(facetOk, "composition", "delete did not refresh the service selector");
			}
			int editedRow = -1;
			for (int row = 0; row < static_cast<int>(m_sceneModel.compositions.size()); ++row) {
				if (m_sceneModel.compositions[row].id == editedCompositionId) {
					editedRow = row;
					break;
				}
			}
			if (editedRow < 0 || m_sceneModel.compositions[editedRow].units
					!= expectedCompositions[static_cast<std::size_t>(assignedCompositionRow)].units)
				facetFailure(facetOk, "composition", "edited composition was not retained");
			if (facetOk)
				std::fprintf(stdout, "E2E_EDITOR_COMPOSITION_OK\n");
		}
	}

	std::string editedServiceId;
	if (!m_sceneLoaded || !m_serviceListWidget || expectedServices.empty() || editedCompositionId.empty()) {
		bool facetOk = false;
		facetFailure(facetOk, "service", "scene or service setup unavailable");
	} else {
		bool facetOk = true;
		const double precisePerformancePercent = 99.12345678901234;
		const double preciseMaximumSpeedKmh = 876.5432109876543;
		const int originalCount = m_serviceListWidget->count();
		m_serviceListWidget->setCurrentRow(0);
		duplicateService();
		if (m_serviceListWidget->count() != originalCount + 1) {
			facetFailure(facetOk, "service", "duplicate did not apply");
		} else {
			m_serviceListWidget->setCurrentRow(1);
			const std::string oldServiceId = m_sceneModel.services[1].id;
			int referenceScenarioRow = -1;
			std::size_t temporaryDelayCount = 0;
			int temporaryDelayRow = -1;
			std::size_t temporaryIncidentCount = 0;
			for (int scenarioRow = 0; scenarioRow < static_cast<int>(m_sceneModel.scenarios.size()); ++scenarioRow) {
				if (m_sceneModel.scenarios[static_cast<std::size_t>(scenarioRow)].id == m_selectedScenarioId) {
					referenceScenarioRow = scenarioRow;
					break;
				}
			}
			if (referenceScenarioRow >= 0) {
				SceneScenario& referenceScenario = m_sceneModel.scenarios[static_cast<std::size_t>(referenceScenarioRow)];
				if (m_addEntranceDelayButton && m_entranceDelayListWidget && m_entranceDelayServiceCombo) {
					m_addEntranceDelayButton->click();
					QApplication::processEvents();
					temporaryDelayRow = m_entranceDelayListWidget->count() - 1;
					const int serviceIndex = m_entranceDelayServiceCombo->findData(QString::fromStdString(oldServiceId));
					if (temporaryDelayRow < 0 || serviceIndex < 0) {
						facetFailure(facetOk, "service", "public entrance-delay controls did not expose the duplicated service");
					} else {
						m_entranceDelayListWidget->setCurrentRow(temporaryDelayRow);
						m_entranceDelayServiceCombo->setCurrentIndex(serviceIndex);
						QApplication::processEvents();
						if (!referenceScenario.entranceDelays.empty()
								&& referenceScenario.entranceDelays.back().serviceId == oldServiceId)
							++temporaryDelayCount;
					}
				} else {
					facetFailure(facetOk, "service", "public entrance-delay controls unavailable for rename coverage");
				}
				SceneIncident incident;
				incident.id = uniqueIncidentId("e2e_service_rename_incident");
				incident.type = "train_breakdown";
				incident.target = oldServiceId;
				incident.endSeconds = 1.0;
				referenceScenario.incidents.push_back(incident);
				++temporaryIncidentCount;
			}
			m_excludedSceneOccurrences.insert(SceneServiceOccurrence{oldServiceId, 1});
			editedServiceId = uniqueServiceId("e2e_service");
			if (!m_serviceIdEdit) {
				facetFailure(facetOk, "service", "id editor unavailable");
			} else {
				const std::string duplicateServiceId = m_sceneModel.services.front().id;
				const auto referencesStillOld = [&]() {
					if (referenceScenarioRow < 0)
						return true;
					const SceneScenario& scenario = m_sceneModel.scenarios[static_cast<std::size_t>(referenceScenarioRow)];
					const bool delayStillOld = temporaryDelayCount == 0 || (!scenario.entranceDelays.empty()
						&& scenario.entranceDelays.back().serviceId == oldServiceId);
					const bool incidentStillOld = temporaryIncidentCount == 0 || (!scenario.incidents.empty()
						&& scenario.incidents.back().target == oldServiceId);
					return delayStillOld && incidentStillOld
						&& m_excludedSceneOccurrences.find(SceneServiceOccurrence{oldServiceId, 1})
							!= m_excludedSceneOccurrences.end();
				};
				m_serviceIdEdit->setText(QString());
				commitServiceIdEdit();
				const bool emptyRejected = m_sceneModel.services[1].id == oldServiceId
					&& m_serviceIdEdit->text() == QString::fromStdString(oldServiceId)
					&& referencesStillOld();
				m_serviceIdEdit->setText(QString::fromStdString(duplicateServiceId));
				commitServiceIdEdit();
				const bool duplicateRejected = m_sceneModel.services[1].id == oldServiceId
					&& m_serviceIdEdit->text() == QString::fromStdString(oldServiceId)
					&& referencesStillOld();
				if (!emptyRejected || !duplicateRejected)
					facetFailure(facetOk, "service", "empty or duplicate ID rename changed canonical references");
				m_serviceIdEdit->setText(QString::fromStdString(editedServiceId));
				commitServiceIdEdit();
				if (m_sceneModel.services.size() <= 1 || m_sceneModel.services[1].id != editedServiceId)
					facetFailure(facetOk, "service", "id edit did not apply");
			}
			bool referencesUpdated = true;
			if (referenceScenarioRow >= 0) {
				const SceneScenario& referenceScenario = m_sceneModel.scenarios[static_cast<std::size_t>(referenceScenarioRow)];
				if (temporaryDelayCount > 0)
					referencesUpdated = referencesUpdated && !referenceScenario.entranceDelays.empty()
						&& referenceScenario.entranceDelays.back().serviceId == editedServiceId;
				if (temporaryIncidentCount > 0)
					referencesUpdated = referencesUpdated && !referenceScenario.incidents.empty()
						&& referenceScenario.incidents.back().target == editedServiceId;
			}
			if (m_excludedSceneOccurrences.find(SceneServiceOccurrence{editedServiceId, 1})
					== m_excludedSceneOccurrences.end())
				referencesUpdated = false;
			if (!referencesUpdated)
				facetFailure(facetOk, "service", "service ID rename did not update canonical references or occurrence exclusions");
			m_serviceListWidget->setCurrentRow(1);
			const int referencedServiceCount = m_serviceListWidget->count();
			deleteService();
			if (m_serviceListWidget->count() != referencedServiceCount
					|| !std::any_of(m_sceneModel.services.begin(), m_sceneModel.services.end(),
						[&](const SceneService& service) { return service.id == editedServiceId; }))
				facetFailure(facetOk, "service", "operation-referenced service delete was not refused");
			if (referenceScenarioRow >= 0) {
				SceneScenario& referenceScenario = m_sceneModel.scenarios[static_cast<std::size_t>(referenceScenarioRow)];
				if (temporaryDelayCount > 0 && m_entranceDelayListWidget && m_deleteEntranceDelayButton
						&& temporaryDelayRow >= 0 && temporaryDelayRow < m_entranceDelayListWidget->count()) {
					m_entranceDelayListWidget->setCurrentRow(temporaryDelayRow);
					acceptConfirmation();
					m_deleteEntranceDelayButton->click();
					QApplication::processEvents();
				}
				if (temporaryIncidentCount > 0 && !referenceScenario.incidents.empty())
					referenceScenario.incidents.pop_back();
			}
			selectAllServiceOccurrences();
			commitServiceComposition(QString::fromStdString(editedCompositionId));
			commitServiceRoute(QString::fromStdString(expectedServices[0].route));
			if (m_serviceOperatingCodeEdit) {
				m_serviceOperatingCodeEdit->setText("1723");
				QMetaObject::invokeMethod(m_serviceOperatingCodeEdit, "editingFinished", Qt::DirectConnection);
			}
			if (m_serviceThroughCheck)
				m_serviceThroughCheck->setChecked(true);
			commitServiceHasEntryTime(false);
			commitServiceHasEntryTime(true);
			if (m_serviceEntryTimeSecondsEdit)
				m_serviceEntryTimeSecondsEdit->setText("600");
			commitServiceEntryTimeSeconds();
			commitServiceHasRepeat(true);
			if (m_serviceHeadwaySecondsEdit)
				m_serviceHeadwaySecondsEdit->setText("1800");
			commitServiceHeadwaySeconds();
			if (m_serviceHasRepeatCountCheck)
				m_serviceHasRepeatCountCheck->setChecked(true);
			if (m_serviceRepeatCountEdit) {
				m_serviceRepeatCountEdit->setText("3");
				QMetaObject::invokeMethod(m_serviceRepeatCountEdit, "editingFinished", Qt::DirectConnection);
			}
			if (m_servicePerformancePercentEdit)
				m_servicePerformancePercentEdit->setValue(95.5);
			if (m_serviceHasMaximumSpeedCheck)
				m_serviceHasMaximumSpeedCheck->setChecked(true);
			if (m_serviceMaximumSpeedKmhEdit) {
				m_serviceMaximumSpeedKmhEdit->setValue(100.0);
				QMetaObject::invokeMethod(m_serviceMaximumSpeedKmhEdit, "editingFinished", Qt::DirectConnection);
			}
			if (m_serviceHasOperatingCodeStepCheck)
				m_serviceHasOperatingCodeStepCheck->setChecked(true);
			if (m_serviceOperatingCodeStepEdit) {
				m_serviceOperatingCodeStepEdit->setText("2");
				QMetaObject::invokeMethod(m_serviceOperatingCodeStepEdit, "editingFinished", Qt::DirectConnection);
			}
			refreshServiceOccurrencePreview();
			bool occurrencePreviewOk = m_serviceOccurrenceTable != nullptr;
			int serviceOccurrences = 0;
			QStringList previewCodes;
			if (m_serviceOccurrenceTable) {
				for (int previewRow = 0; previewRow < m_serviceOccurrenceTable->rowCount(); ++previewRow) {
					QTableWidgetItem* include = m_serviceOccurrenceTable->item(previewRow, 0);
					if (!include || include->data(Qt::UserRole).toString().toStdString() != editedServiceId)
						continue;
					++serviceOccurrences;
					previewCodes << m_serviceOccurrenceTable->item(previewRow, 1)->text();
					if (include->checkState() != Qt::Checked)
						occurrencePreviewOk = false;
				}
				const QStringList expectedPreviewCodes({"1723", "1725", "1727"});
				if (previewCodes.size() != expectedPreviewCodes.size()
					|| !std::equal(previewCodes.cbegin(), previewCodes.cend(), expectedPreviewCodes.cbegin()))
					occurrencePreviewOk = false;
			}
			occurrencePreviewOk = occurrencePreviewOk && serviceOccurrences == 3
				&& m_sceneModel.services[1].operatingCode == "1723"
				&& m_sceneModel.services[1].performancePercent == 95.5
				&& m_sceneModel.services[1].hasRepeat && m_sceneModel.services[1].headwaySeconds == 1800.0
				&& m_sceneModel.services[1].hasRepeatCount && m_sceneModel.services[1].repeatCount == 3
				&& m_sceneModel.services[1].hasOperatingCodeStep && m_sceneModel.services[1].operatingCodeStep == 2
				&& m_sceneModel.services[1].hasMaximumSpeed && m_sceneModel.services[1].maximumSpeedKmh == 100.0;
			if (!occurrencePreviewOk)
				facetFailure(facetOk, "service", "occurrence preview did not derive the requested codes or fields");
			m_sceneModel.services[1].performancePercent = precisePerformancePercent;
			m_sceneModel.services[1].hasMaximumSpeed = true;
			m_sceneModel.services[1].maximumSpeedKmh = preciseMaximumSpeedKmh;
			updateServiceDetailPanel();
			commitPendingServiceSettings();
			if (m_sceneModel.services[1].performancePercent != precisePerformancePercent
					|| m_sceneModel.services[1].maximumSpeedKmh != preciseMaximumSpeedKmh)
				facetFailure(facetOk, "service", "untouched precise service settings changed during pending commit");
			const SceneService serviceBeforeSelection = m_sceneModel.services[1];
			const bool dirtyBeforeSelection = m_sceneDirty;
			selectNoneServiceOccurrences();
			if (m_serviceOccurrenceTable) {
				for (int previewRow = 0; previewRow < m_serviceOccurrenceTable->rowCount(); ++previewRow) {
					QTableWidgetItem* include = m_serviceOccurrenceTable->item(previewRow, 0);
					if (include && include->data(Qt::UserRole).toString().toStdString() == editedServiceId
							&& include->data(Qt::UserRole + 1).toInt() == 1)
						include->setCheckState(Qt::Checked);
				}
			}
			const bool oneOccurrenceSelected = selectedServiceOccurrences() == 1
				&& selectedSceneOccurrences().count(SceneServiceOccurrence{editedServiceId, 1}) == 1;
			const bool selectionDidNotDirty = m_sceneDirty == dirtyBeforeSelection
				&& sameServices({serviceBeforeSelection}, {m_sceneModel.services[1]});
			if (!oneOccurrenceSelected || !selectionDidNotDirty)
				facetFailure(facetOk, "service", "occurrence selection changed canonical fields or dirty state");
		}
		addService();
		if (m_serviceListWidget->count() != originalCount + 2) {
			facetFailure(facetOk, "service", "add did not apply");
		} else {
			const std::string unreferencedServiceId = m_sceneModel.services.back().id;
			if (m_selectNoneOccurrencesButton) {
				m_selectNoneOccurrencesButton->click();
				QApplication::processEvents();
			}
			const bool occurrenceExclusionCreated = m_excludedSceneOccurrences.find(
				SceneServiceOccurrence{unreferencedServiceId, 1}) != m_excludedSceneOccurrences.end();
			acceptConfirmation();
			deleteService();
			if (m_serviceListWidget->count() != originalCount + 1
					|| std::any_of(m_sceneModel.services.begin(), m_sceneModel.services.end(),
						[&](const SceneService& service) { return service.id == unreferencedServiceId; })
					|| (occurrenceExclusionCreated && m_excludedSceneOccurrences.find(
						SceneServiceOccurrence{unreferencedServiceId, 1}) != m_excludedSceneOccurrences.end()))
				facetFailure(facetOk, "service", "unreferenced service delete did not prune occurrence exclusions");
		}
		int editedRow = -1;
		for (int row = 0; row < static_cast<int>(m_sceneModel.services.size()); ++row) {
			if (m_sceneModel.services[row].id == editedServiceId) {
				editedRow = row;
				break;
			}
		}
		if (editedRow < 0) {
			facetFailure(facetOk, "service", "edited service was not retained");
		} else {
			const SceneService& edited = m_sceneModel.services[editedRow];
			if (edited.composition != editedCompositionId || edited.route != expectedServices[0].route
					|| !edited.hasEntryTime || edited.entryTimeSeconds != 600.0
					|| !edited.hasRepeat || edited.headwaySeconds != 1800.0
					|| !edited.hasRepeatCount || edited.repeatCount != 3
					|| edited.operatingCode != "1723" || edited.performancePercent != precisePerformancePercent
					|| !edited.hasMaximumSpeed || edited.maximumSpeedKmh != preciseMaximumSpeedKmh
					|| !edited.hasOperatingCodeStep || edited.operatingCodeStep != 2 || !edited.through
					|| edited.stops.size() != expectedServices[0].stops.size())
				facetFailure(facetOk, "service", "edited fields did not persist in memory");
		}
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_SERVICE_OK\n");
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_SERVICE_OCCURRENCES_OK\n");
	}

	if (!m_sceneLoaded || !m_serviceListWidget || !m_stopListWidget || editedServiceId.empty()) {
		bool facetOk = false;
		facetFailure(facetOk, "timetable", "scene or edited service unavailable");
	} else {
		bool facetOk = true;
		int serviceRow = -1;
		for (int row = 0; row < static_cast<int>(m_sceneModel.services.size()); ++row) {
			if (m_sceneModel.services[row].id == editedServiceId) {
				serviceRow = row;
				break;
			}
		}
		if (serviceRow < 0) {
			facetFailure(facetOk, "timetable", "edited service row missing");
		} else {
			m_serviceListWidget->setCurrentRow(serviceRow);
			QApplication::processEvents();
			const int originalStopCount = static_cast<int>(m_sceneModel.services[serviceRow].stops.size());
			if (originalStopCount <= 0)
				facetFailure(facetOk, "timetable", "no baseline stop available for move coverage");
			addStop();
			if (static_cast<int>(m_sceneModel.services[serviceRow].stops.size()) != originalStopCount + 1) {
				facetFailure(facetOk, "timetable", "add stop did not apply");
			} else {
				std::string stationId;
				if (!m_sceneModel.stations.empty())
					stationId = m_sceneModel.stations.back().id;
				if (stationId.empty()) {
					facetFailure(facetOk, "timetable", "no station available for edited stop");
				} else {
					commitStopStation(QString::fromStdString(stationId));
					std::string platformId;
					for (const auto& station : m_sceneModel.stations) {
						if (station.id == stationId && !station.platforms.empty()) {
							platformId = station.platforms.front().id;
							break;
						}
					}
					if (!platformId.empty())
						commitStopPlatform(QString::fromStdString(platformId));
					double lastTime = 0.0;
					for (const auto& stop : m_sceneModel.services[serviceRow].stops) {
						if (stop.hasPlannedArrival)
							lastTime = std::max(lastTime, stop.plannedArrivalSeconds);
						if (stop.hasPlannedDeparture)
							lastTime = std::max(lastTime, stop.plannedDepartureSeconds);
					}
					const int arrivalSeconds = static_cast<int>(lastTime) + 600;
					const int departureSeconds = arrivalSeconds + 60;
					commitStopHasArrival(true);
					if (m_stopArrivalSecondsEdit)
						m_stopArrivalSecondsEdit->setText(QString::number(arrivalSeconds));
					commitStopArrivalSeconds();
					commitStopHasDeparture(true);
					if (m_stopDepartureSecondsEdit)
						m_stopDepartureSecondsEdit->setText(QString::number(departureSeconds));
					commitStopDepartureSeconds();
					if (m_stopDwellSecondsEdit)
						m_stopDwellSecondsEdit->setText("60");
					commitStopDwellSeconds();
					const SceneStop& committedStop = m_sceneModel.services[serviceRow].stops.back();
					if (committedStop.stationId != stationId)
						facetFailure(facetOk, "timetable", "station edit did not apply the chosen station");
					if (!platformId.empty() && committedStop.platformId != platformId)
						facetFailure(facetOk, "timetable", "platform edit did not apply the chosen platform");
					if (!committedStop.hasPlannedArrival)
						facetFailure(facetOk, "timetable", "arrival flag did not apply");
					if (committedStop.plannedArrivalSeconds != static_cast<double>(arrivalSeconds))
						facetFailure(facetOk, "timetable", "arrival seconds did not apply the requested value");
					if (!committedStop.hasPlannedDeparture)
						facetFailure(facetOk, "timetable", "departure flag did not apply");
					if (committedStop.plannedDepartureSeconds != static_cast<double>(departureSeconds))
						facetFailure(facetOk, "timetable", "departure seconds did not apply the requested value");
					if (committedStop.dwellSeconds != 60.0)
						facetFailure(facetOk, "timetable", "dwell did not apply the requested 60 seconds");
				}
			}
			SceneStop editedStop;
			if (static_cast<int>(m_sceneModel.services[serviceRow].stops.size()) > originalStopCount)
				editedStop = m_sceneModel.services[serviceRow].stops.back();
			if (facetOk) {
				m_stopListWidget->setCurrentRow(originalStopCount);
				moveStopUp();
				if (m_sceneModel.services[serviceRow].stops[originalStopCount - 1].stationId != editedStop.stationId)
					facetFailure(facetOk, "timetable", "move up did not apply");
				moveStopDown();
				if (!sameStop(m_sceneModel.services[serviceRow].stops.back(), editedStop))
					facetFailure(facetOk, "timetable", "move down did not restore edited stop");
				// The source scene's final stop intentionally omits a planned departure;
				// leave the edited stop before it so the model remains valid for save/reload.
				m_stopListWidget->setCurrentRow(originalStopCount);
				moveStopUp();
			}
			addStop();
			if (static_cast<int>(m_sceneModel.services[serviceRow].stops.size()) != originalStopCount + 2) {
				facetFailure(facetOk, "timetable", "temporary stop add did not apply");
			} else {
				removeStop();
				if (static_cast<int>(m_sceneModel.services[serviceRow].stops.size()) != originalStopCount + 1)
					facetFailure(facetOk, "timetable", "stop delete did not apply");
			}
			if (facetOk && !sameStop(m_sceneModel.services[serviceRow].stops[originalStopCount - 1], editedStop))
				facetFailure(facetOk, "timetable", "edited stop was not retained");
		}
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_TIMETABLE_OK\n");
	}

	std::string entranceDelayServiceId = editedServiceId;
	std::string entranceDelayStationId;
	double entranceDelaySeconds = 12.75;
	if (!m_sceneLoaded || editedServiceId.empty() || !m_entranceDelayListWidget
			|| !m_addEntranceDelayButton || !m_duplicateEntranceDelayButton
			|| !m_deleteEntranceDelayButton || !m_entranceDelayServiceCombo
			|| !m_entranceDelayOccurrenceEdit || !m_entranceDelayStationCombo
			|| !m_entranceDelaySecondsEdit) {
		bool facetOk = false;
		facetFailure(facetOk, "entrance delay", "public delay controls unavailable");
	} else {
		bool facetOk = true;
		if (m_incidentDock) {
			m_incidentDock->show();
			m_incidentDock->raise();
		}
		if (auto* tabs = findChild<QTabWidget*>("scenarioEditorTabs"))
			tabs->setCurrentIndex(1);
		const int originalDelayCount = m_entranceDelayListWidget->count();
		m_addEntranceDelayButton->click();
		QApplication::processEvents();
		if (m_entranceDelayListWidget->count() != originalDelayCount + 1) {
			facetFailure(facetOk, "entrance delay", "add did not apply through the public button");
		} else {
			const int delayRow = m_entranceDelayListWidget->count() - 1;
			m_entranceDelayListWidget->setCurrentRow(delayRow);
			const int serviceIndex = m_entranceDelayServiceCombo->findData(
				QString::fromStdString(entranceDelayServiceId));
			if (serviceIndex < 0) {
				facetFailure(facetOk, "entrance delay", "repeated service was missing from the typed selector");
			} else {
				m_entranceDelayServiceCombo->setCurrentIndex(serviceIndex);
				QApplication::processEvents();
				const auto editedService = std::find_if(m_sceneModel.services.begin(), m_sceneModel.services.end(),
					[&](const SceneService& candidate) { return candidate.id == entranceDelayServiceId; });
				int repeatedDepartureRow = -1;
				std::string repeatedStationId;
				if (editedService != m_sceneModel.services.end()) {
					const auto repeated = std::adjacent_find(editedService->stops.begin(), editedService->stops.end(),
						[](const SceneStop& first, const SceneStop& second) {
							return first.stationId == second.stationId && first.hasPlannedDeparture
								&& !second.hasPlannedDeparture;
						});
					if (repeated != editedService->stops.end()) {
						repeatedDepartureRow = static_cast<int>(std::distance(editedService->stops.begin(), repeated));
						repeatedStationId = repeated->stationId;
					}
				}
				if (repeatedDepartureRow < 0 || !m_serviceListWidget || !m_stopListWidget
						|| !m_moveStopDownButton || !m_moveStopUpButton) {
					facetFailure(facetOk, "entrance delay", "repeated-stop selector fixture unavailable");
				} else {
					if (m_serviceDock) {
						m_serviceDock->show();
						m_serviceDock->raise();
					}
					m_serviceListWidget->setCurrentRow(static_cast<int>(
						std::distance(m_sceneModel.services.begin(), editedService)));
					m_stopListWidget->setCurrentRow(repeatedDepartureRow);
					m_moveStopDownButton->click();
					QApplication::processEvents();
					const bool laterDepartureExcluded = m_entranceDelayStationCombo->findData(
						QString::fromStdString(repeatedStationId)) < 0;
					m_moveStopUpButton->click();
					QApplication::processEvents();
					if (!laterDepartureExcluded || m_entranceDelayStationCombo->findData(
							QString::fromStdString(repeatedStationId)) < 0)
						facetFailure(facetOk, "entrance delay",
							"station choices did not follow the first repeated stop");
					if (m_incidentDock) {
						m_incidentDock->show();
						m_incidentDock->raise();
					}
				}
				int stationIndex = -1;
				for (int index = 0; index < m_entranceDelayStationCombo->count(); ++index) {
					if (!m_entranceDelayStationCombo->itemData(index).toString().isEmpty()) {
						stationIndex = index;
						break;
					}
				}
				if (stationIndex < 0) {
					facetFailure(facetOk, "entrance delay", "repeated service had no eligible planned-departure stop");
				} else {
					entranceDelayStationId = m_entranceDelayStationCombo->itemData(stationIndex).toString().toStdString();
					m_entranceDelayStationCombo->setCurrentIndex(stationIndex);
					m_entranceDelayOccurrenceEdit->setValue(2);
					QMetaObject::invokeMethod(m_entranceDelayOccurrenceEdit, "editingFinished", Qt::DirectConnection);
					m_entranceDelaySecondsEdit->setValue(entranceDelaySeconds);
					QMetaObject::invokeMethod(m_entranceDelaySecondsEdit, "editingFinished", Qt::DirectConnection);
					const SceneEntranceDelay* delay = selectedEntranceDelay();
					const bool editedDelay = delay && delay->serviceId == entranceDelayServiceId
						&& delay->occurrence == 2 && delay->stationId == entranceDelayStationId
						&& delay->delaySeconds == entranceDelaySeconds && editedService != m_sceneModel.services.end()
						&& editedService->hasRepeat
						&& sceneServiceOccurrenceCount(*editedService, serviceOccurrenceDuration()) >= 2
						&& m_entranceDelayOccurrenceContextLabel
						&& m_entranceDelayOccurrenceContextLabel->text().contains("1725");
					if (!editedDelay)
						facetFailure(facetOk, "entrance delay", "service, occurrence, station, or seconds edit did not persist");
					const SceneRunSelection delayedOccurrence{{entranceDelayServiceId, 2}};
					const auto infrastructureDiagnostics = buildInfrastructureAndSignallingFromScene(m_sceneModel);
					const auto operationsDiagnostics = buildOperationsFromScene(
						m_sceneModel, m_selectedScenarioId, delayedOccurrence);
					if (hasErrors(infrastructureDiagnostics) || hasErrors(operationsDiagnostics)
							|| numRegions != 1
							|| regional_train[0].trainDescription != entranceDelayServiceId + "-2"
							|| regional_train[0].EntranceDelay != entranceDelaySeconds)
						facetFailure(facetOk, "entrance delay",
							"publicly authored delay did not reach the selected native occurrence");
				}
			}
		}
		m_duplicateEntranceDelayButton->click();
		QApplication::processEvents();
		if (m_entranceDelayListWidget->count() != originalDelayCount + 2) {
			facetFailure(facetOk, "entrance delay", "duplicate did not apply through the public button");
		} else {
			acceptConfirmation();
			m_deleteEntranceDelayButton->click();
			QApplication::processEvents();
			if (m_entranceDelayListWidget->count() != originalDelayCount + 1)
				facetFailure(facetOk, "entrance delay", "delete did not apply through the public button");
		}
		if (const SceneScenario* scenario = selectedScenario())
			expectedEntranceDelays = scenario->entranceDelays;
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_ENTRANCE_DELAY_OK\n");
	}

	std::string editedIncidentId;
	if (!m_sceneLoaded || !m_incidentListWidget || editedServiceId.empty()) {
		bool facetOk = false;
		facetFailure(facetOk, "incident", "scene or edited service unavailable");
	} else {
		bool facetOk = true;
		const double preciseBreakdownSpeed = 40.125;
		const int originalCount = m_incidentListWidget->count();
		editedIncidentId = uniqueIncidentId("e2e_incident");
		addIncident();
		if (m_incidentListWidget->count() != originalCount + 1) {
			facetFailure(facetOk, "incident", "add did not apply");
		} else {
			if (m_incidentIdEdit)
				m_incidentIdEdit->setText(QString::fromStdString(editedIncidentId));
			commitIncidentIdEdit();
			if (m_incidentTypeCombo)
				m_incidentTypeCombo->setCurrentText("train_breakdown");
			if (!m_incidentHasReducedSpeedCheck || !m_incidentHasReducedSpeedCheck->isEnabled())
				facetFailure(facetOk, "incident", "breakdown controls stayed disabled after the type change");
			commitIncidentTarget(QString::fromStdString(editedServiceId));
			if (m_incidentStartSecondsEdit)
				m_incidentStartSecondsEdit->setText("100");
			commitIncidentStartSeconds();
			if (m_incidentEndSecondsEdit)
				m_incidentEndSecondsEdit->setText("200");
			commitIncidentEndSeconds();
			if (m_incidentHasOccurrenceCheck)
				m_incidentHasOccurrenceCheck->setChecked(true);
			if (m_incidentOccurrenceEdit)
				m_incidentOccurrenceEdit->setText("2");
			commitIncidentOccurrence();
			if (m_incidentHasReducedSpeedCheck)
				m_incidentHasReducedSpeedCheck->setChecked(true);
			if (SceneIncident* incident = selectedIncident(); !incident
					|| !incident->hasReducedSpeed || !m_incidentReducedSpeedKmhEdit
					|| incident->reducedSpeedKmh != m_incidentReducedSpeedKmhEdit->value())
				facetFailure(facetOk, "incident", "enabling the speed cap did not commit its displayed value");
			if (m_incidentReducedSpeedKmhEdit)
				m_incidentReducedSpeedKmhEdit->setValue(preciseBreakdownSpeed);
			commitIncidentReducedSpeed();
			if (m_incidentHasEndSecondsCheck)
				m_incidentHasEndSecondsCheck->setChecked(false);
			if (m_incidentTerminateAtDestinationCheck)
				m_incidentTerminateAtDestinationCheck->setChecked(true);
		}
		duplicateIncident();
		if (m_incidentListWidget->count() != originalCount + 2) {
			facetFailure(facetOk, "incident", "duplicate did not apply");
		} else {
			acceptConfirmation();
			deleteIncident();
			if (m_incidentListWidget->count() != originalCount + 1)
				facetFailure(facetOk, "incident", "delete did not apply");
		}
		int editedRow = -1;
		const auto& incidents = selectedScenarioIncidents();
		for (int row = 0; row < static_cast<int>(incidents.size()); ++row) {
			if (incidents[row].id == editedIncidentId) {
				editedRow = row;
				break;
			}
		}
		if (editedRow < 0 || incidents[editedRow].type != "train_breakdown"
				|| incidents[editedRow].target != editedServiceId
				|| incidents[editedRow].startSeconds != 100.0
				|| incidents[editedRow].endSeconds != 0.0
				|| !incidents[editedRow].hasOccurrence || incidents[editedRow].occurrence != 2
				|| !incidents[editedRow].hasReducedSpeed
				|| incidents[editedRow].reducedSpeedKmh != preciseBreakdownSpeed
				|| incidents[editedRow].hasEndSeconds
				|| !incidents[editedRow].terminateAtDestination)
			facetFailure(facetOk, "incident", "edited incident was not retained");
		if (!m_serviceListWidget || !m_incidentTargetCombo) {
			facetFailure(facetOk, "incident", "service target controls unavailable");
		} else {
			const int serviceCount = m_serviceListWidget->count();
			addService();
			const std::string temporaryServiceId = m_sceneModel.services.back().id;
			if (m_incidentTargetCombo->findText(QString::fromStdString(temporaryServiceId)) < 0)
				facetFailure(facetOk, "incident", "service add did not refresh breakdown targets");
			acceptConfirmation();
			deleteService();
			if (m_serviceListWidget->count() != serviceCount
					|| m_incidentTargetCombo->findText(QString::fromStdString(temporaryServiceId)) >= 0)
				facetFailure(facetOk, "incident", "service delete did not refresh breakdown targets");
		}
		const QString review = runReviewText();
		const bool reviewOk = review.contains(QString("Scenario: %1").arg(scenarioContext()))
			&& review.contains(QString("id=%1 type=train_breakdown target=%2 start=100")
				.arg(QString::fromStdString(editedIncidentId), QString::fromStdString(editedServiceId)))
			&& review.contains("window=until-destination occurrence=2 options=")
			&& review.contains(QString("Entrance delay configuration: service=%1 occurrence=2 operating_code=1725 station=%2 seconds=%3")
				.arg(QString::fromStdString(entranceDelayServiceId), QString::fromStdString(entranceDelayStationId),
					QString::number(entranceDelaySeconds, 'g', 12)));
		if (!reviewOk)
			facetFailure(facetOk, "incident", "run review omitted complete incident or entrance-delay configuration");
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_INCIDENT_OK\n");
	}

	if (m_sceneLoaded) {
		bool facetOk = true;
		refreshValidationPanel();
		QApplication::processEvents();
		SceneDiagnosticCounts counts = countDiagnostics(m_sceneDiagnostics);
		if (!m_validationTable || !m_validationStatusLabel || counts.errors != 0
				|| m_validationTable->rowCount() != static_cast<int>(m_sceneDiagnostics.size())
				|| !m_validationStatusLabel->text().startsWith("Validation:"))
			facetFailure(facetOk, "validation", "refresh did not report a clean, complete result");
		if (facetOk)
			std::fprintf(stdout, "E2E_EDITOR_VALIDATION_OK\n");
	}

	if (m_sceneLoaded) {
		bool facetOk = true;
		QString outBase = qEnvironmentVariable("QEGTRAIN_E2E_OUT");
		QTemporaryDir tmpDir;
		if (outBase.isEmpty())
			outBase = tmpDir.path();
		QString outScenePath = QDir(outBase).filePath("editor_smoke_scene");
		QDir outputDir(outScenePath);
		if (outputDir.exists() && !outputDir.removeRecursively()) {
			facetFailure(facetOk, "save/reload", "output path could not be cleaned");
		} else {
			for (int row = 0; row < static_cast<int>(m_sceneModel.services.size()); ++row) {
				if (m_sceneModel.services[static_cast<std::size_t>(row)].id != editedServiceId)
					continue;
				m_serviceListWidget->setCurrentRow(row);
				m_serviceOperatingCodeEdit->setText("1731");
				commitPendingServiceSettings();
				if (m_sceneModel.services[static_cast<std::size_t>(row)].operatingCode != "1731")
					facetFailure(facetOk, "save/reload", "pending service text did not commit before persistence");
				break;
			}
			expectedTrainUnits = m_sceneModel.trainUnits;
			expectedCompositions = m_sceneModel.compositions;
			expectedServices = m_sceneModel.services;
			expectedStations = m_sceneModel.stations;
			expectedPassengers = m_sceneModel.passengers;
			expectedIncidents = selectedScenarioIncidents();
			if (const SceneScenario* scenario = selectedScenario())
				expectedEntranceDelays = scenario->entranceDelays;
			auto result = ::saveScene(m_sceneModel, outScenePath.toStdString());
			if (!result.success()) {
				facetFailure(facetOk, "save/reload", "save failed");
			} else if (!openSceneDirectory(outScenePath)) {
				facetFailure(facetOk, "save/reload", "saved scene did not reload");
			} else if (!sameTrainUnits(expectedTrainUnits, m_sceneModel.trainUnits)) {
				facetFailure(facetOk, "save/reload", "train-unit facet changed after reload");
			} else if (!sameCompositions(expectedCompositions, m_sceneModel.compositions)) {
				facetFailure(facetOk, "save/reload", "composition facet changed after reload");
				} else if (!sameServices(expectedServices, m_sceneModel.services)) {
					facetFailure(facetOk, "save/reload", "service/timetable facet changed after reload");
				} else if (!sameStations(expectedStations, m_sceneModel.stations)) {
					facetFailure(facetOk, "save/reload", "platform geometry changed after reload");
				} else if (!samePassengers(expectedPassengers, m_sceneModel.passengers)) {
					facetFailure(facetOk, "save/reload", "passenger hierarchy changed after reload");
				} else if (!sameIncidents(expectedIncidents, selectedScenarioIncidents())) {
				facetFailure(facetOk, "save/reload", "incident facet changed after reload");
			} else if (!selectedScenario()
					|| !sameEntranceDelays(expectedEntranceDelays, selectedScenario()->entranceDelays)) {
				facetFailure(facetOk, "save/reload", "entrance-delay facet changed after reload");
			} else if (m_sceneDirty || hasErrors(m_sceneDiagnostics)) {
				facetFailure(facetOk, "save/reload", "reloaded scene is dirty or invalid");
			} else {
				const auto triggerPendingSave = [&]() {
					if (!m_saveSceneAction || !m_saveSceneAction->isEnabled())
						return false;
					m_saveSceneAction->trigger();
					QApplication::processEvents();
					return !m_sceneDirty;
				};
				const auto spinTextEdit = [](QAbstractSpinBox* spin) {
					return spin ? spin->findChild<QLineEdit*>() : nullptr;
				};
				const auto modelRowFor = [](const auto& values, const std::string& id) {
					for (int row = 0; row < static_cast<int>(values.size()); ++row)
						if (values[static_cast<std::size_t>(row)].id == id)
							return row;
					return -1;
				};
				const int trainUnitRow = modelRowFor(m_sceneModel.trainUnits, editedTrainUnitId);
				const int compositionRow = modelRowFor(m_sceneModel.compositions, editedCompositionId);
				const int serviceRow = modelRowFor(m_sceneModel.services, editedServiceId);
				const int passengerRow = modelRowFor(m_sceneModel.passengers, e2ePassengerId);
				if (passengerRow < 0 || e2eJourneyId.empty() || e2eLegId.empty()
						|| !m_passengerDock || !m_passengerListWidget || !m_passengerIdEdit
						|| !m_passengerJourneyListWidget || !m_passengerJourneyIdEdit
						|| !m_passengerJourneyActivityEdit || !m_passengerLegListWidget
						|| !m_passengerLegIdEdit || !m_passengerLegOccurrenceEdit) {
					facetFailure(facetOk, "save/reload", "passenger pending-editor controls were unavailable after reload");
				} else {
					activateWindow();
					m_passengerDock->show();
					m_passengerDock->raise();
					m_passengerListWidget->setCurrentRow(passengerRow);
					if (m_passengerTabs)
						m_passengerTabs->setCurrentIndex(0);
					QApplication::processEvents();
					const std::string pendingPassengerId = e2ePassengerId + "_focused";
					m_passengerIdEdit->setText(QString::fromStdString(pendingPassengerId));
					m_passengerIdEdit->setFocus();
					if (!triggerPendingSave() || m_sceneModel.passengers[static_cast<std::size_t>(passengerRow)].id != pendingPassengerId)
						facetFailure(facetOk, "save/reload", "Save did not commit the still-focused passenger ID");
					else
						e2ePassengerId = pendingPassengerId;
					m_passengerJourneyListWidget->setCurrentRow(0);
					QApplication::processEvents();
					const std::string pendingJourneyId = e2eJourneyId + "_focused";
					m_passengerJourneyIdEdit->setText(QString::fromStdString(pendingJourneyId));
					m_passengerJourneyIdEdit->setFocus();
					if (!triggerPendingSave() || !selectedPassengerJourney()
							|| selectedPassengerJourney()->id != pendingJourneyId)
						facetFailure(facetOk, "save/reload", "Save did not commit the still-focused passenger journey ID");
					else
						e2eJourneyId = pendingJourneyId;
					const std::string pendingActivity = "e2e focused activity";
					m_passengerJourneyActivityEdit->setText(QString::fromStdString(pendingActivity));
					m_passengerJourneyActivityEdit->setFocus();
					if (!triggerPendingSave() || !selectedPassengerJourney()
							|| selectedPassengerJourney()->activity != pendingActivity)
						facetFailure(facetOk, "save/reload", "Save did not commit the still-focused passenger activity");
					if (m_passengerJourneyWindowEdits[0]) {
						QLineEdit* pendingWindow = spinTextEdit(m_passengerJourneyWindowEdits[0]);
						const double pendingWindowValue = 111.25;
						if (!pendingWindow) {
							facetFailure(facetOk, "save/reload", "passenger planned-window editor was unavailable");
						} else {
							pendingWindow->setText(QString::number(pendingWindowValue, 'g', 16));
							pendingWindow->setFocus();
							if (!triggerPendingSave() || !selectedPassengerJourney()
									|| selectedPassengerJourney()->plannedDepartureStartSeconds != pendingWindowValue)
								facetFailure(facetOk, "save/reload", "Save did not commit the still-focused passenger planned window");
						}
					}
					if (m_passengerTabs)
						m_passengerTabs->setCurrentIndex(1);
					m_passengerLegListWidget->setCurrentRow(0);
					QApplication::processEvents();
					const std::string pendingLegId = e2eLegId + "_focused";
					m_passengerLegIdEdit->setText(QString::fromStdString(pendingLegId));
					m_passengerLegIdEdit->setFocus();
					if (!triggerPendingSave() || !selectedPassengerLeg()
							|| selectedPassengerLeg()->id != pendingLegId)
						facetFailure(facetOk, "save/reload", "Save did not commit the still-focused passenger leg ID");
					else
						e2eLegId = pendingLegId;
					QLineEdit* pendingOccurrence = spinTextEdit(m_passengerLegOccurrenceEdit);
					const int repeatedServiceIndex = m_passengerLegServiceCombo
						? m_passengerLegServiceCombo->findData(QString::fromStdString(editedServiceId)) : -1;
					if (!pendingOccurrence || repeatedServiceIndex < 0) {
						facetFailure(facetOk, "save/reload", "passenger occurrence editor was unavailable");
					} else {
						activateWindow();
						m_passengerDock->show();
						m_passengerDock->raise();
						m_passengerLegServiceCombo->setCurrentIndex(repeatedServiceIndex);
						QApplication::processEvents();
						pendingOccurrence->setText("2");
						pendingOccurrence->setFocus();
						QApplication::processEvents();
						const bool savedOccurrence = triggerPendingSave();
						if (!savedOccurrence || !selectedPassengerLeg()
								|| selectedPassengerLeg()->occurrence != 2) {
							facetFailure(facetOk, "save/reload",
								QString("Save did not commit the still-focused passenger occurrence "
									"(range %1..%2, editor %3, model %4, saved %5)")
									.arg(m_passengerLegOccurrenceEdit->minimum())
									.arg(m_passengerLegOccurrenceEdit->maximum())
									.arg(m_passengerLegOccurrenceEdit->value())
									.arg(selectedPassengerLeg() ? selectedPassengerLeg()->occurrence : -1)
									.arg(savedOccurrence));
						}
					}
					if (!m_infrastructureFacetCombo || !m_infrastructureTable
							|| m_infrastructureFacetCombo->findData("platforms") < 0) {
						facetFailure(facetOk, "save/reload", "platform geometry controls were unavailable for focused Save");
					} else {
						activateWindow();
						if (m_infrastructureDock) {
							m_infrastructureDock->show();
							m_infrastructureDock->raise();
						}
						m_infrastructureFacetCombo->setCurrentIndex(
							m_infrastructureFacetCombo->findData("platforms"));
						QApplication::processEvents();
						const auto enterSpinText = [](QLineEdit* editor, const QString& text) {
							editor->selectAll();
							for (const QChar character : text) {
								QKeyEvent keyPress(QEvent::KeyPress, character.unicode(), Qt::NoModifier,
									QString(character));
								QApplication::sendEvent(editor, &keyPress);
							}
						};
						int implicitPlatformRow = -1;
						ScenePlatform* implicitPlatform = nullptr;
						int platformRow = 0;
						for (auto& station : m_sceneModel.stations) {
							for (auto& platform : station.platforms) {
								if (!implicitPlatform && !platform.hasLength && !platform.hasWidth) {
									implicitPlatformRow = platformRow;
									implicitPlatform = &platform;
								}
								++platformRow;
							}
						}
						auto* implicitLengthEdit = implicitPlatformRow >= 0
							? qobject_cast<QDoubleSpinBox*>(m_infrastructureTable->cellWidget(implicitPlatformRow, 3))
							: nullptr;
						auto* implicitWidthEdit = implicitPlatformRow >= 0
							? qobject_cast<QDoubleSpinBox*>(m_infrastructureTable->cellWidget(implicitPlatformRow, 4))
							: nullptr;
						QLineEdit* implicitLength = spinTextEdit(implicitLengthEdit);
						QLineEdit* implicitWidth = spinTextEdit(implicitWidthEdit);
						if (!implicitPlatform || !implicitLength || !implicitWidth) {
							facetFailure(facetOk, "save/reload", "implicit platform geometry was unavailable");
						} else {
							implicitWidth->setFocus();
							QApplication::processEvents();
							if (!implicitWidth->hasFocus())
								facetFailure(facetOk, "save/reload", "implicit platform width could not receive focus");
							enterSpinText(implicitWidth, QStringLiteral("2.5"));
							implicitLength->setFocus();
							QApplication::processEvents();
							if (!implicitLength->hasFocus()) {
								facetFailure(facetOk, "save/reload", "implicit platform length could not receive focus");
							} else if (!implicitPlatform->hasWidth || implicitPlatform->widthM != 2.5) {
								facetFailure(facetOk, "save/reload",
									"same-value platform geometry was not committed on focus loss");
							} else if (!triggerPendingSave()
									|| implicitPlatform->hasLength)
								facetFailure(facetOk, "save/reload",
									"Save materialized an untouched compatibility platform length");
						}
						auto* lengthEdit = qobject_cast<QDoubleSpinBox*>(m_infrastructureTable->cellWidget(0, 3));
						QLineEdit* pendingLength = spinTextEdit(lengthEdit);
						if (!pendingLength) {
							facetFailure(facetOk, "save/reload", "focused platform length editor was unavailable");
						} else {
							const double pendingLengthValue = 234.5;
							pendingLength->setFocus();
							enterSpinText(pendingLength, QString::number(pendingLengthValue, 'g', 16));
							if (!triggerPendingSave() || m_sceneModel.stations.empty()
									|| m_sceneModel.stations.front().platforms.empty()
									|| !m_sceneModel.stations.front().platforms.front().hasLength
									|| m_sceneModel.stations.front().platforms.front().lengthM != pendingLengthValue)
								facetFailure(facetOk, "save/reload", "Save did not commit the still-focused platform length");
						}
					}
				}
				int entranceDelayRow = -1;
				if (const SceneScenario* scenario = selectedScenario()) {
					for (int row = 0; row < static_cast<int>(scenario->entranceDelays.size()); ++row) {
						const SceneEntranceDelay& delay = scenario->entranceDelays[static_cast<std::size_t>(row)];
						if (delay.serviceId == entranceDelayServiceId && delay.occurrence == 2
								&& delay.stationId == entranceDelayStationId) {
							entranceDelayRow = row;
							break;
						}
					}
				}
				if (entranceDelayRow < 0 || !m_entranceDelayListWidget || !m_entranceDelaySecondsEdit
						|| !spinTextEdit(m_entranceDelaySecondsEdit)) {
					facetFailure(facetOk, "save/reload", "focused entrance-delay controls were unavailable after reload");
				} else {
					activateWindow();
					if (m_incidentDock) {
						m_incidentDock->show();
						m_incidentDock->raise();
					}
					if (auto* tabs = findChild<QTabWidget*>("scenarioEditorTabs"))
						tabs->setCurrentIndex(1);
					m_entranceDelayListWidget->setCurrentRow(entranceDelayRow);
					QApplication::processEvents();
					const double pendingEntranceDelay = entranceDelaySeconds + 0.5;
					QLineEdit* pendingDelayEdit = spinTextEdit(m_entranceDelaySecondsEdit);
					pendingDelayEdit->setText(QString::number(pendingEntranceDelay, 'g', 16));
					pendingDelayEdit->setFocus();
					QApplication::processEvents();
					if (!selectedEntranceDelay() || selectedEntranceDelay()->delaySeconds == pendingEntranceDelay
							|| !triggerPendingSave() || !selectedEntranceDelay()
							|| selectedEntranceDelay()->delaySeconds != pendingEntranceDelay) {
						facetFailure(facetOk, "save/reload",
							"Save did not commit the still-focused entrance-delay value");
					} else {
						entranceDelaySeconds = pendingEntranceDelay;
						if (const SceneScenario* scenario = selectedScenario())
							expectedEntranceDelays = scenario->entranceDelays;
					}
				}
				if (trainUnitRow < 0 || compositionRow < 0 || serviceRow < 0
						|| !m_compositionIdEdit || !m_trainUnitSourceDataEdit
						|| !m_trainUnitPhysicalEdits[0]
						|| !spinTextEdit(m_trainUnitPhysicalEdits[0])) {
					facetFailure(facetOk, "save/reload", "pending editor controls were unavailable after reload");
				} else {
					activateWindow();
					if (m_trainUnitDock) {
						m_trainUnitDock->show();
						m_trainUnitDock->raise();
					}
					QApplication::processEvents();
					m_trainUnitListWidget->setCurrentRow(trainUnitRow);
					m_serviceListWidget->setCurrentRow(serviceRow);
					const double pendingPhysicalMass = std::max(
						123.0, m_trainUnitPhysicalEdits[0]->value() + 1.25);
					QLineEdit* pendingPhysicalEdit = spinTextEdit(m_trainUnitPhysicalEdits[0]);
					pendingPhysicalEdit->setText(QString::number(pendingPhysicalMass, 'g', 16));
					pendingPhysicalEdit->setFocus();
					QApplication::processEvents();
					if (!triggerPendingSave()) {
						facetFailure(facetOk, "save/reload", "Save action did not flush focused train-unit text");
					} else {
						const int committedTrainRow = modelRowFor(m_sceneModel.trainUnits, editedTrainUnitId);
						if (committedTrainRow < 0
								|| !m_sceneModel.trainUnits[static_cast<std::size_t>(committedTrainRow)].hasPhysical
								|| m_sceneModel.trainUnits[static_cast<std::size_t>(committedTrainRow)]
									   .physical.mass_of_traction_unit_kg != pendingPhysicalMass)
							facetFailure(facetOk, "save/reload", "focused train-unit text was not committed before Save");
					}

					const std::string pendingCompositionId = uniqueCompositionId("e2e_pending_composition");
					if (m_compositionDock) {
						m_compositionDock->show();
						m_compositionDock->raise();
					}
					m_compositionListWidget->setCurrentRow(compositionRow);
					m_serviceListWidget->setCurrentRow(serviceRow);
					m_compositionIdEdit->setText(QString::fromStdString(pendingCompositionId));
					m_compositionIdEdit->setFocus();
					if (!triggerPendingSave()) {
						facetFailure(facetOk, "save/reload", "Save action did not flush focused composition text");
					} else {
						const int committedCompositionRow = modelRowFor(m_sceneModel.compositions, pendingCompositionId);
						if (committedCompositionRow < 0
								|| !std::any_of(m_sceneModel.services.begin(), m_sceneModel.services.end(),
									[&](const SceneService& service) {
										return service.composition == pendingCompositionId;
									}))
							facetFailure(facetOk, "save/reload", "focused composition text was not committed before Save");
						else
							editedCompositionId = pendingCompositionId;
					}

					const int pendingServiceRow = modelRowFor(m_sceneModel.services, editedServiceId);
					int pendingStopRow = -1;
					if (pendingServiceRow >= 0) {
						const auto& stops = m_sceneModel.services[static_cast<std::size_t>(pendingServiceRow)].stops;
						for (int row = 0; row < static_cast<int>(stops.size()); ++row) {
							if (stops[static_cast<std::size_t>(row)].hasPlannedArrival && stops[static_cast<std::size_t>(row)].hasPlannedDeparture) {
								pendingStopRow = row;
								break;
							}
						}
						if (pendingStopRow < 0 && !stops.empty())
							pendingStopRow = 0;
					}
					const int scenarioRow = [&]() {
						for (int row = 0; row < static_cast<int>(m_sceneModel.scenarios.size()); ++row)
							if (m_sceneModel.scenarios[static_cast<std::size_t>(row)].id == m_selectedScenarioId)
								return row;
						return -1;
					}();
					if (pendingServiceRow < 0 || pendingStopRow < 0 || scenarioRow < 0
							|| !m_stopListWidget || !m_stopDwellSecondsEdit
							|| !m_scenarioListWidget || !m_scenarioDescriptionEdit
							|| !m_incidentListWidget || !m_incidentEndSecondsEdit
							|| !m_incidentHasEndSecondsCheck) {
						facetFailure(facetOk, "save/reload", "pending stop, scenario, or incident controls were unavailable");
					} else {
						m_serviceListWidget->setCurrentRow(pendingServiceRow);
						m_stopListWidget->setCurrentRow(pendingStopRow);
						QApplication::processEvents();
						const int pendingDwell = static_cast<int>(m_sceneModel.services[
							static_cast<std::size_t>(pendingServiceRow)]
							.stops[static_cast<std::size_t>(pendingStopRow)].dwellSeconds) + 1;
						m_stopDwellSecondsEdit->setText(QString::number(pendingDwell));
						m_stopDwellSecondsEdit->setFocus();
						if (!triggerPendingSave() || m_sceneModel.services[static_cast<std::size_t>(pendingServiceRow)]
															 .stops[static_cast<std::size_t>(pendingStopRow)]
															 .dwellSeconds != pendingDwell)
							facetFailure(facetOk, "save/reload", "focused stop dwell text was not committed before Save");

						if (m_incidentDock) {
							m_incidentDock->show();
							m_incidentDock->raise();
						}
						m_scenarioListWidget->setCurrentRow(scenarioRow);
						QApplication::processEvents();
						m_scenarioDescriptionEdit->setText("pending scenario values");
						m_scenarioDescriptionEdit->setFocus();
						QApplication::processEvents();
						if (!triggerPendingSave() || !selectedScenario() || selectedScenario()->description != "pending scenario values")
							facetFailure(facetOk, "save/reload", "focused scenario description was not committed before Save");

						const auto& incidents = selectedScenarioIncidents();
						const auto pendingIncident = std::find_if(incidents.begin(), incidents.end(),
							[&](const SceneIncident& incident) { return incident.id == editedIncidentId; });
						if (pendingIncident == incidents.end()) {
							facetFailure(facetOk, "save/reload", "edited incident was unavailable for pending Save coverage");
						} else {
							const int incidentRow = static_cast<int>(std::distance(incidents.begin(), pendingIncident));
							m_incidentListWidget->setCurrentRow(incidentRow);
							QApplication::processEvents();
							if (m_incidentHasEndSecondsCheck->isChecked())
								m_incidentHasEndSecondsCheck->setChecked(false);
							const int pendingIncidentEnd = static_cast<int>(pendingIncident->endSeconds) + 200;
							m_incidentEndSecondsEdit->setText(QString::number(pendingIncidentEnd));
							m_incidentEndSecondsEdit->setFocus();
							QApplication::processEvents();
							if (m_sceneModel.routes.empty()) {
								facetFailure(facetOk, "save/reload", "route unavailable for blocked Run coverage");
							} else {
								const std::vector<std::string> validRoute = m_sceneModel.routes.front().blocks;
								m_sceneModel.routes.front().blocks.clear();
								m_runSceneAction->trigger();
								QApplication::processEvents();
								if (m_worker || !selectedIncident()
										|| !selectedIncident()->hasEndSeconds
										|| selectedIncident()->endSeconds != pendingIncidentEnd)
									facetFailure(facetOk, "save/reload", "Run did not flush focused incident text before validation");
								m_sceneModel.routes.front().blocks = validRoute;
								refreshValidationPanel();
							}
							}
						}
						const std::string pendingSource = "e2e/source-data-pending-save.txt";
						m_trainUnitListWidget->setCurrentRow(trainUnitRow);
						m_trainUnitSourceDataEdit->setText(QString::fromStdString(pendingSource));
						m_trainUnitSourceDataEdit->setFocus();
						if (m_sceneModel.trainUnits[static_cast<std::size_t>(trainUnitRow)].sourceDataFile
								== pendingSource) {
							facetFailure(facetOk, "save/reload", "pending source edit committed before Save");
						} else if (!saveSceneToCurrentDir()) {
							facetFailure(facetOk, "save/reload", "pending editor values were not saved");
						} else {
						expectedTrainUnits = m_sceneModel.trainUnits;
						expectedCompositions = m_sceneModel.compositions;
						expectedServices = m_sceneModel.services;
						expectedStations = m_sceneModel.stations;
						expectedPassengers = m_sceneModel.passengers;
						expectedIncidents = selectedScenarioIncidents();
							if (const SceneScenario* scenario = selectedScenario())
								expectedEntranceDelays = scenario->entranceDelays;
							if (!openSceneDirectory(outScenePath)
									|| !sameTrainUnits(expectedTrainUnits, m_sceneModel.trainUnits)
								|| !sameCompositions(expectedCompositions, m_sceneModel.compositions)
								|| !sameServices(expectedServices, m_sceneModel.services)
								|| !sameStations(expectedStations, m_sceneModel.stations)
								|| !samePassengers(expectedPassengers, m_sceneModel.passengers)
								|| !sameIncidents(expectedIncidents, selectedScenarioIncidents())
									|| !selectedScenario()
									|| !sameEntranceDelays(expectedEntranceDelays,
										selectedScenario()->entranceDelays))
								facetFailure(facetOk, "save/reload", "pending editor values did not survive Save and reopen");
						}
						bool selectionRoundtripOk = m_excludedSceneOccurrences.empty();
					if (m_serviceOccurrenceTable) {
						for (int row = 0; row < m_serviceOccurrenceTable->rowCount(); ++row) {
							QTableWidgetItem* include = m_serviceOccurrenceTable->item(row, 0);
							selectionRoundtripOk = selectionRoundtripOk && include && include->checkState() == Qt::Checked;
						}
					}
					if (!selectionRoundtripOk)
						facetFailure(facetOk, "save/reload", "temporary occurrence selection was serialized or not reset on reopen");
					const QString bundlePath = outScenePath + ".egscene";
					QFile::remove(bundlePath);
					const SceneSaveResult bundleSave = saveSceneBundle(m_sceneModel, bundlePath.toStdString());
					if (!bundleSave.success() || !openSceneDirectory(bundlePath) || !selectedScenario()
							|| !sameStations(expectedStations, m_sceneModel.stations)
							|| !samePassengers(expectedPassengers, m_sceneModel.passengers)
							|| !sameEntranceDelays(expectedEntranceDelays,
								selectedScenario()->entranceDelays))
						facetFailure(facetOk, "save/reload", "entrance delays changed after bundle save and reopen");
					}
			}
			if (facetOk)
				std::fprintf(stdout, "E2E_EDITOR_SAVE_RELOAD_OK\n");
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

void MainWindow::runTrackPreviewE2E() {
	if (m_e2eFinished)
		return;
	m_e2eFinished = true;

	bool ok = true;
	QStringList failures;
	auto fail = [&](const QString& facet, const QString& message) {
		ok = false;
		failures << QString("%1: %2").arg(facet, message);
	};
	auto marker = [](const char* name) {
		std::fprintf(stdout, "%s\n", name);
		std::fflush(stdout);
	};
	auto hasDiagnosticCode = [](const std::vector<SceneDiagnostic>& diagnostics, const char* code) {
		for (const auto& diagnostic : diagnostics) {
			if (diagnostic.code == code)
				return true;
		}
		return false;
	};
#ifdef signals
#define EGTRAIN_TRACK_PREVIEW_RESTORE_SIGNALS
#undef signals
#endif
	auto sameSceneModel = [](const SceneModel& left, const SceneModel& right) {
		const auto& leftIncidents = defaultScenarioIncidents(left);
		const auto& rightIncidents = defaultScenarioIncidents(right);
		if (left.schemaVersion != right.schemaVersion || left.name != right.name
			|| left.description != right.description || left.baseTime != right.baseTime
			|| left.stations.size() != right.stations.size() || left.signals.size() != right.signals.size()
			|| left.routes.size() != right.routes.size() || left.trainUnits.size() != right.trainUnits.size()
			|| left.compositions.size() != right.compositions.size() || left.services.size() != right.services.size()
			|| leftIncidents.size() != rightIncidents.size())
			return false;
		if (!std::equal(left.stations.begin(), left.stations.end(), right.stations.begin(), [](const auto& a, const auto& b) {
			return a.id == b.id && a.name == b.name && a.platforms.size() == b.platforms.size()
				&& std::equal(a.platforms.begin(), a.platforms.end(), b.platforms.begin(),
				[](const auto& x, const auto& y) { return x.id == y.id; });
		}))
			return false;
		if (!std::equal(left.signals.begin(), left.signals.end(), right.signals.begin(),
			[](const auto& a, const auto& b) {
				return a.id == b.id && a.protectedSection == b.protectedSection;
			}))
			return false;
		if (!std::equal(left.routes.begin(), left.routes.end(), right.routes.begin(), [](const auto& a, const auto& b) {
			return a.id == b.id && a.blocks == b.blocks;
		}))
			return false;
		if (!std::equal(left.trainUnits.begin(), left.trainUnits.end(), right.trainUnits.begin(), [](const auto& a, const auto& b) {
			return a.id == b.id && a.hasPhysical == b.hasPhysical && a.physical.mass_of_traction_unit_kg == b.physical.mass_of_traction_unit_kg
				&& a.physical.mass_of_a_wagon_kg == b.physical.mass_of_a_wagon_kg && a.physical.number_of_wagons == b.physical.number_of_wagons
				&& a.physical.max_speed_ms == b.physical.max_speed_ms && a.physical.max_deceleration_ms2 == b.physical.max_deceleration_ms2
				&& a.physical.frontal_area_m2 == b.physical.frontal_area_m2 && a.physical.resistance_coefficient == b.physical.resistance_coefficient
				&& a.physical.jerk_ms3 == b.physical.jerk_ms3 && a.physical.length_m == b.physical.length_m
				&& a.tractionCurve == b.tractionCurve && a.sourceDataFile == b.sourceDataFile
				&& a.sourceTractionFile == b.sourceTractionFile;
		}))
			return false;
		if (!std::equal(left.compositions.begin(), left.compositions.end(), right.compositions.begin(),
			[](const auto& a, const auto& b) { return a.id == b.id && a.units == b.units; }))
			return false;
		if (!std::equal(left.services.begin(), left.services.end(), right.services.begin(), [](const auto& a, const auto& b) {
			if (a.id != b.id || a.operatingCode != b.operatingCode
				|| a.composition != b.composition || a.route != b.route || a.through != b.through
				|| a.hasEntryTime != b.hasEntryTime || a.entryTimeSeconds != b.entryTimeSeconds
				|| a.hasRepeat != b.hasRepeat || a.headwaySeconds != b.headwaySeconds || a.stops.size() != b.stops.size())
				return false;
			return std::equal(a.stops.begin(), a.stops.end(), b.stops.begin(), [](const auto& x, const auto& y) {
				return x.stationId == y.stationId && x.platformId == y.platformId && x.hasPlannedArrival == y.hasPlannedArrival
					&& x.hasPlannedDeparture == y.hasPlannedDeparture && x.plannedArrivalSeconds == y.plannedArrivalSeconds
					&& x.plannedDepartureSeconds == y.plannedDepartureSeconds && x.dwellSeconds == y.dwellSeconds;
			});
		}))
			return false;
		return std::equal(leftIncidents.begin(), leftIncidents.end(), rightIncidents.begin(),
			[](const auto& a, const auto& b) {
				return a.id == b.id && a.type == b.type && a.target == b.target
					&& a.startSeconds == b.startSeconds && a.endSeconds == b.endSeconds;
			});
	};
#ifdef EGTRAIN_TRACK_PREVIEW_RESTORE_SIGNALS
#define signals Q_SIGNALS
#undef EGTRAIN_TRACK_PREVIEW_RESTORE_SIGNALS
#endif
	auto validationTableHasCode = [&](const char* code) {
		if (!m_validationTable)
			return false;
		for (int row = 0; row < m_validationTable->rowCount(); ++row) {
			QTableWidgetItem* item = m_validationTable->item(row, 1);
			if (item && item->text() == code)
				return true;
		}
		return false;
	};
	struct PreviewContentSnapshot {
		QList<QGraphicsItem*> items;
		QList<QRectF> itemBounds;
		QRectF bounds;
	};
	auto snapshotPreviewContent = [&]() {
		PreviewContentSnapshot snapshot;
		if (!scene)
			return snapshot;
		bool hasBounds = false;
		for (auto* item : scene->items()) {
			if (!item)
				continue;
			snapshot.items.push_back(item);
			const QRectF itemBounds = item->sceneBoundingRect();
			snapshot.itemBounds.push_back(itemBounds);
			if (itemBounds.isEmpty())
				continue;
			snapshot.bounds = hasBounds ? snapshot.bounds.united(itemBounds) : itemBounds;
			hasBounds = true;
		}
		return snapshot;
	};
	auto samePreviewContent = [](const PreviewContentSnapshot& left, const PreviewContentSnapshot& right) {
		if (left.items.size() != right.items.size() || left.itemBounds.size() != right.itemBounds.size())
			return false;
		for (int index = 0; index < left.items.size(); ++index) {
			if (left.items.at(index) != right.items.at(index)
				|| left.itemBounds.at(index) != right.itemBounds.at(index))
				return false;
		}
		if (left.bounds != right.bounds)
			return false;
		return true;
	};

	const QString scenePath = qEnvironmentVariable("QEGTRAIN_E2E_SCENE");
	const bool opened = !scenePath.isEmpty() && openSceneDirectory(scenePath);
	if (!opened || !m_sceneLoaded) {
		fail("open", "incomplete scene did not open");
	} else {
		const PreviewContentSnapshot preview = snapshotPreviewContent();
		const int itemCount = preview.items.size();
		const QRectF bounds = preview.bounds;
		const QRectF visible = networkView->mapToScene(networkView->viewport()->rect()).boundingRect();
		const bool diagnosticsOk = hasDiagnosticCode(m_sceneDiagnostics, "scene.trains.none")
			&& hasDiagnosticCode(m_sceneDiagnostics, "scene.services.none")
			&& validationTableHasCode("scene.trains.none") && validationTableHasCode("scene.services.none");
		if (!diagnosticsOk)
			fail("open", "semantic diagnostics are missing from the validation panel");
		if (itemCount < 2)
			fail("open", "too few preview items");
		if (bounds.width() < 10.0)
			fail("open", "preview geometry did not spread horizontally");
		if (!visible.intersects(bounds))
			fail("open", "preview is outside the viewport");
		if (!m_followTrainCombo || m_followTrainCombo->currentText() != "No trains to follow")
			fail("follow", "empty train selector does not explain that no trains are available");
		if (diagnosticsOk && itemCount >= 2 && bounds.width() >= 10.0 && visible.intersects(bounds))
			marker("E2E_TRACK_PREVIEW_OPEN_OK");
		const bool previewHasSignalGlyph = std::any_of(m_signalDecorations.cbegin(),
			m_signalDecorations.cend(), [](QGraphicsItem* item) {
				return item && qgraphicsitem_cast<SignalItem*>(item) != nullptr;
			});
		const QVector<NetworkLegendEntry> legendEntries = m_networkLegendWidget
			? m_networkLegendWidget->entries() : QVector<NetworkLegendEntry>();
		const bool legendHasSignals = std::any_of(legendEntries.cbegin(),
			legendEntries.cend(), [](const NetworkLegendEntry& entry) {
					return entry.kind == NetworkLegendEntryKind::Signal;
				});
		if (previewHasSignalGlyph != legendHasSignals)
			fail("signals", "preview glyphs and map-key signal entries are out of sync");
		if (previewHasSignalGlyph && networkView) {
			updateViewportOverlays();
			const QTransform transform = networkView->viewportTransform();
			for (QGraphicsItem* item : m_signalDecorations) {
				if (!item || !qgraphicsitem_cast<SignalItem*>(item)
						|| !item->data(kSignalAnchorRole).isValid())
					continue;
				const QPointF anchor = transform.map(item->data(kSignalAnchorRole).toPointF());
				const QPointF marker = transform.map(item->scenePos());
				if (qAbs(QLineF(anchor, marker).length() - kPreviewSignalOffsetPixels) > 0.5)
					fail("signals", "preview signal drifted away from its fixed trackside offset");
			}
		}
		if (previewHasSignalGlyph && m_signalLayerCheck) {
			const bool layerWasChecked = m_signalLayerCheck->isChecked();
			m_signalLayerCheck->setChecked(false);
			QApplication::processEvents();
			const bool hidden = std::none_of(m_signalDecorations.cbegin(),
				m_signalDecorations.cend(), [](QGraphicsItem* item) {
					return item && qgraphicsitem_cast<SignalItem*>(item) != nullptr && item->isVisible();
				});
			m_signalLayerCheck->setChecked(layerWasChecked);
			QApplication::processEvents();
			if (!hidden)
				fail("signals", "Signals layer toggle did not hide preview glyphs");
		}
		if (!networkView) {
			fail("viewport", "preview viewport is missing");
		} else {
			const QRectF previewFitBounds = networkView->topologyBounds();
			const qreal previewBaseline = networkView->fittedScale();
			const bool zoomApplied = networkView->zoomBy(1.15);
			if (!zoomApplied || qAbs(networkView->zoomRatio() - 1.15) > 1e-5
				|| qAbs(qAbs(networkView->transform().m11()) - previewBaseline * 1.15) > 1e-5)
				fail("viewport", "preview zoom is not relative to the fitted baseline");
			fitView();
			if (networkView->zoomLabel() != QStringLiteral("Fit")
				|| networkView->topologyBounds() != previewFitBounds)
				fail("viewport", "Fit did not restore the preview baseline");
		}
	}

	bool runFacetOk = m_runSceneAction && !m_runSceneAction->isEnabled()
		&& ui->actionSimulationStart && !ui->actionSimulationStart->isEnabled();
	if (!runFacetOk)
		fail("run gating", "Run action is enabled while semantic errors are present");
	if (m_sceneLoaded) {
		const SceneModel incomplete = m_sceneModel;
		SceneModel fixed = incomplete;
		fixed.baseTime = "08:00:00";
		fixed.settings.hasDuration = true;
		fixed.settings.durationSeconds = 3600.0;
		fixed.tracks = {{"e2e_track"}};
		fixed.nodes = {
			{"e2e_node_1", "e2e_track", 0.0, 0.0},
			{"e2e_node_2", "e2e_track", 1.0, 0.0},
		};
		fixed.arcs = {{"e2e_arc", "e2e_track", "e2e_node_1", "e2e_node_2", 0.0, 0.0, 1.0}};
		fixed.blocks = {{"e2e_block", "e2e_track", 1.0}};
		fixed.connections.clear();
		fixed.stations.clear();
		SceneStation station;
		station.id = "e2e_station";
		station.name = "E2E station";
		station.platforms.push_back({"e2e_platform", {"e2e_node_1"}});
		fixed.stations.push_back(station);
		fixed.trainUnits.clear();
		SceneTrainUnit unit;
		unit.id = "e2e_unit";
		unit.hasPhysical = true;
		unit.tractionCurve.push_back({{0.0, 1.0, 1.0, 0.0, 0.0}});
		fixed.trainUnits.push_back(unit);
		fixed.compositions.clear();
		SceneComposition composition;
		composition.id = "e2e_composition";
		composition.units.push_back(unit.id);
		fixed.compositions.push_back(composition);
		fixed.routes.clear();
		SceneRoute route;
		route.id = "e2e_route";
		route.blocks.push_back("e2e_block");
		fixed.routes.push_back(route);
		fixed.services.clear();
		SceneService service;
		service.id = "e2e_service";
		service.composition = composition.id;
		service.route = route.id;
		service.through = true;
		fixed.services.push_back(service);
		defaultScenarioIncidents(fixed).clear();
		m_sceneModel = fixed;
		refreshValidationPanel();
		if (hasErrors(m_sceneDiagnostics) || !m_runSceneAction || !m_runSceneAction->isEnabled()
			|| !ui->actionSimulationStart || !ui->actionSimulationStart->isEnabled())
			runFacetOk = false;
		m_sceneModel = incomplete;
		refreshValidationPanel();
		if (!hasErrors(m_sceneDiagnostics) || !m_runSceneAction || m_runSceneAction->isEnabled()
			|| !ui->actionSimulationStart || ui->actionSimulationStart->isEnabled())
			runFacetOk = false;
	}
	const PreviewContentSnapshot previewBeforeRun = snapshotPreviewContent();
	runScene();
	const bool workerStarted = m_worker != nullptr;
	QApplication::processEvents();
	if (workerStarted || m_worker) {
		runFacetOk = false;
		fail("run gating", "invalid scene started simulation");
	}
	if (!samePreviewContent(previewBeforeRun, snapshotPreviewContent())) {
		runFacetOk = false;
		fail("run gating", "blocked run removed the preview");
	}
	if (runFacetOk)
		marker("E2E_TRACK_PREVIEW_RUN_GATED_OK");

	QTemporaryDir e2eRoot;
	QString persistedScenePath;
	SceneModel expectedPartial;
	bool saveReloadOk = true;
	if (!e2eRoot.isValid() || !m_sceneLoaded) {
		saveReloadOk = false;
		fail("save/reload", "temporary scene directory unavailable");
	} else {
		expectedPartial = m_sceneModel;
		persistedScenePath = QDir(e2eRoot.path()).filePath("partial");
		const SceneSaveResult saveResult = ::saveScene(expectedPartial, persistedScenePath.toStdString());
		if (!saveResult.success()) {
			saveReloadOk = false;
			fail("save/reload", "partial scene save failed");
		} else if (!openSceneDirectory(persistedScenePath)) {
			saveReloadOk = false;
			fail("save/reload", "saved partial scene did not reload");
		} else if (!sameSceneModel(expectedPartial, m_sceneModel)) {
			saveReloadOk = false;
			fail("save/reload", "partial model changed after reload");
		} else if (!hasDiagnosticCode(m_sceneDiagnostics, "scene.trains.none")
			|| !hasDiagnosticCode(m_sceneDiagnostics, "scene.services.none")
			|| !m_runSceneAction || m_runSceneAction->isEnabled()
			|| !ui->actionSimulationStart || ui->actionSimulationStart->isEnabled()) {
			saveReloadOk = false;
			fail("save/reload", "reloaded diagnostics or Run gating changed");
		}
	}
	if (saveReloadOk)
		marker("E2E_TRACK_PREVIEW_SAVE_RELOAD_OK");

	bool structuralOk = saveReloadOk;
	if (structuralOk) {
		const SceneModel expectedCurrent = m_sceneModel;
		const QString expectedDir = QDir(m_sceneDir).absolutePath();
		const PreviewContentSnapshot expectedContent = snapshotPreviewContent();
		auto rejectedWithoutReplacement = [&](const QString& candidate, const char* label) {
			if (openSceneDirectory(candidate)) {
				fail("structural rejection", QString("%1 scene unexpectedly opened").arg(label));
				return false;
			}
			const bool preserved = QDir(m_sceneDir).absolutePath() == expectedDir
				&& sameSceneModel(m_sceneModel, expectedCurrent)
				&& samePreviewContent(expectedContent, snapshotPreviewContent());
			if (!preserved)
				fail("structural rejection", QString("%1 failure replaced current state").arg(label));
			return preserved;
		};
		auto copyCandidate = [&](const QString& name) {
			const QString candidate = QDir(e2eRoot.path()).filePath(name);
			if (!copyDirectoryRecursively(persistedScenePath, candidate)) {
				fail("structural rejection", QString("could not create %1 candidate").arg(name));
				return QString();
			}
			return candidate;
		};

		QString malformed = copyCandidate("malformed");
		if (malformed.isEmpty()) {
			structuralOk = false;
		} else {
			QFile file(QDir(malformed).filePath("scene.json"));
			const bool wroteMalformed = file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write("{") == 1;
			file.close();
			if (!wroteMalformed) {
				structuralOk = false;
				fail("structural rejection", "could not write malformed candidate");
			} else if (!rejectedWithoutReplacement(malformed, "malformed")) {
				structuralOk = false;
			}
		}

		QString missing = copyCandidate("missing");
		if (missing.isEmpty()) {
			structuralOk = false;
		} else {
			if (!QFile::remove(QDir(missing).filePath("services.json"))) {
				structuralOk = false;
				fail("structural rejection", "could not remove required file from candidate");
			} else if (!rejectedWithoutReplacement(missing, "missing-file")) {
				structuralOk = false;
			}
		}

		QString unsupported = copyCandidate("unsupported");
		if (unsupported.isEmpty()) {
			structuralOk = false;
		} else {
			QFile file(QDir(unsupported).filePath("scene.json"));
			if (!file.open(QIODevice::ReadOnly)) {
				structuralOk = false;
				fail("structural rejection", "could not read schema candidate");
			} else {
				QByteArray contents = file.readAll();
				file.close();
				const QByteArray currentSchemaMarker = "\"schema_version\": "
					+ QByteArray::number(kCurrentSceneSchemaVersion);
				const QByteArray newerSchemaMarker = "\"schema_version\": "
					+ QByteArray::number(kCurrentSceneSchemaVersion + 1);
				if (!contents.contains(currentSchemaMarker)) {
					structuralOk = false;
					fail("structural rejection", "saved scene schema marker not found");
				} else {
					contents.replace(currentSchemaMarker, newerSchemaMarker);
					const bool wroteUnsupported = file.open(QIODevice::WriteOnly | QIODevice::Truncate)
						&& file.write(contents) == contents.size();
					file.close();
					if (!wroteUnsupported) {
						structuralOk = false;
						fail("structural rejection", "could not write unsupported-schema candidate");
					} else if (!rejectedWithoutReplacement(unsupported, "unsupported-schema")) {
						structuralOk = false;
					}
				}
			}
		}
	}
	if (structuralOk)
		marker("E2E_TRACK_PREVIEW_STRUCTURAL_REJECTION_OK");

	if (ok) {
		std::fprintf(stdout, "E2E_TRACK_PREVIEW_OK\n");
		std::fflush(stdout);
		QCoreApplication::exit(0);
		return;
	}

	std::fprintf(stderr, "E2E_TRACK_PREVIEW_FAIL: %s\n", failures.join(", ").toStdString().c_str());
	std::fflush(stderr);
	QCoreApplication::exit(2);
}

void MainWindow::runLegacyImportE2E() {
	if (m_e2eFinished)
		return;
	m_e2eFinished = true;

	bool ok = true;
	QStringList failures;
	auto marker = [](const char* name) {
		std::fprintf(stdout, "%s\n", name);
		std::fflush(stdout);
	};
	auto fail = [&](const QString& message) {
		ok = false;
		failures << message;
	};

	const QString destinationInput = qEnvironmentVariable("QEGTRAIN_E2E_LEGACY_DEST");
	const QString destinationCanonical = QFileInfo(destinationInput).canonicalFilePath();
	const QString destination = destinationCanonical.isEmpty() ? QDir(destinationInput).absolutePath() : destinationCanonical;
	actionLoad_Network();
	if (m_sceneLoaded && QDir(m_sceneDir).absolutePath() == destination && !m_sceneModel.stations.empty()
		&& scene && !scene->items().isEmpty())
		marker("E2E_LEGACY_IMPORT_SUCCESS");
	else
		fail("successful import did not open the destination scene");

	const QString previousDir = m_sceneDir;
	const std::string previousName = m_sceneModel.name;
	const int previousItemCount = scene ? scene->items().size() : -1;
	const QString badSource = qEnvironmentVariable("QEGTRAIN_E2E_LEGACY_BAD_SOURCE");
	const QString badDestination = qEnvironmentVariable("QEGTRAIN_E2E_LEGACY_BAD_DEST");
	qputenv("QEGTRAIN_E2E_LEGACY_SOURCE", badSource.toUtf8());
	qputenv("QEGTRAIN_E2E_LEGACY_DEST", badDestination.toUtf8());
	actionLoad_Network();

	const bool failedImport = !QFileInfo(QDir(badDestination).filePath("scene.json")).exists();
	if (failedImport)
		marker("E2E_LEGACY_IMPORT_FAILURE");
	else
		fail("malformed import unexpectedly wrote a scene");
	const bool preserved = m_sceneDir == previousDir && m_sceneModel.name == previousName
		&& (!scene || scene->items().size() == previousItemCount);
	if (preserved)
		marker("E2E_LEGACY_IMPORT_STATE_PRESERVED");
	else
		fail("failed import replaced the current scene or preview");

	if (ok) {
		marker("E2E_LEGACY_IMPORT_OK");
		QCoreApplication::exit(0);
		return;
	}

	std::fprintf(stderr, "E2E_LEGACY_IMPORT_FAIL: %s\n", failures.join(", ").toStdString().c_str());
	std::fflush(stderr);
	QCoreApplication::exit(2);
}

void MainWindow::runCreatorAcceptanceE2E() {
	if (m_creatorAcceptanceFinished)
		return;

	auto marker = [](const char* name) {
		std::fprintf(stdout, "%s\n", name);
		std::fflush(stdout);
	};
	auto fail = [this](const QString& message) {
		if (m_creatorAcceptanceFinished)
			return;
		m_creatorAcceptanceFinished = true;
		std::fprintf(stderr, "E2E_CREATOR_ACCEPTANCE_FAIL: %s\n", message.toStdString().c_str());
		std::fflush(stderr);
		QCoreApplication::exit(2);
	};
	auto next = [this]() {
		++m_creatorAcceptancePhase;
		QTimer::singleShot(75, this, &MainWindow::runCreatorAcceptanceE2E);
	};
	auto process = []() { QApplication::processEvents(); };
	auto editLine = [process](QLineEdit* edit, const QString& value) {
		if (!edit)
			return false;
		edit->setFocus(Qt::OtherFocusReason);
		edit->setText(value);
		QMetaObject::invokeMethod(edit, "editingFinished", Qt::DirectConnection);
		edit->clearFocus();
		process();
		return edit->text() == value;
	};
	auto choose = [process](QComboBox* combo, const QString& value) {
		if (!combo)
			return false;
		int index = combo->findData(value);
		if (index < 0)
			index = combo->findText(value);
		if (index < 0)
			return false;
		combo->setCurrentIndex(index);
		process();
		return combo->currentIndex() == index;
	};
	auto acceptFileDialog = [this, fail](const QString& path, bool directory) {
		auto poll = std::make_shared<std::function<void()>>();
		auto attempts = std::make_shared<int>(0);
		auto seen = std::make_shared<bool>(false);
		*poll = [this, path, directory, poll, attempts, seen, fail]() {
			if (++*attempts > 200) {
				fail(QStringLiteral("file dialog did not appear for ") + path);
				return;
			}
			QFileDialog* fileDialog = nullptr;
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				auto* dialog = qobject_cast<QFileDialog*>(widget);
				if (!dialog || !dialog->isVisible())
					continue;
				fileDialog = dialog;
				break;
			}
			if (!fileDialog) {
				if (*seen)
					return;
				QTimer::singleShot(25, this, [poll]() { (*poll)(); });
				return;
			}
			*seen = true;
			if (directory) {
				fileDialog->setDirectory(path);
				static_cast<QDialog*>(fileDialog)->done(QDialog::Accepted);
			} else {
				const QFileInfo target(path);
				fileDialog->setDirectory(target.absolutePath());
				fileDialog->selectFile(target.fileName());
				if (QLineEdit* fileName = fileDialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit")))
					fileName->setText(target.fileName());
				QMetaObject::invokeMethod(fileDialog, "accept", Qt::DirectConnection);
			}
			QTimer::singleShot(25, this, [poll]() { (*poll)(); });
		};
		QTimer::singleShot(0, this, [poll]() { (*poll)(); });
	};
	auto acceptInputDialog = [this, fail](const QString& value) {
		auto poll = std::make_shared<std::function<void()>>();
		auto attempts = std::make_shared<int>(0);
		*poll = [this, value, poll, attempts, fail]() {
			if (++*attempts > 200) {
				fail(QStringLiteral("input dialog did not appear"));
				return;
			}
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				auto* dialog = qobject_cast<QInputDialog*>(widget);
				if (!dialog || !dialog->isVisible())
					continue;
				dialog->setTextValue(value);
				dialog->accept();
				return;
			}
			QTimer::singleShot(25, this, [poll]() { (*poll)(); });
		};
		QTimer::singleShot(0, this, [poll]() { (*poll)(); });
	};
	auto acceptMessageBox = [this, fail](QMessageBox::StandardButton desired = QMessageBox::Ok) {
		auto poll = std::make_shared<std::function<void()>>();
		auto attempts = std::make_shared<int>(0);
		*poll = [this, desired, poll, attempts, fail]() {
			if (++*attempts > 200) {
				fail(QStringLiteral("message box did not appear"));
				return;
			}
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				auto* dialog = qobject_cast<QMessageBox*>(widget);
				if (!dialog || !dialog->isVisible())
					continue;
				if (QAbstractButton* button = dialog->button(desired))
					button->click();
				else
					dialog->accept();
				return;
			}
			QTimer::singleShot(25, this, [poll]() { (*poll)(); });
		};
		QTimer::singleShot(0, this, [poll]() { (*poll)(); });
	};
	auto acceptCapacityScope = [this, fail]() {
		auto poll = std::make_shared<std::function<void()>>();
		auto attempts = std::make_shared<int>(0);
		*poll = [this, poll, attempts, fail]() {
			if (++*attempts > 200) {
				fail(QStringLiteral("capacity scope dialog did not appear"));
				return;
			}
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				if (auto* message = qobject_cast<QMessageBox*>(widget); message && message->isVisible()) {
					const QString detail = message->text();
					message->accept();
					fail(QStringLiteral("capacity analysis rejected the creator run: ") + detail);
					return;
				}
				auto* dialog = qobject_cast<QDialog*>(widget);
				if (!dialog || !dialog->isVisible() || !dialog->isModal()
						|| dialog->windowTitle() != QStringLiteral("Capacity analysis"))
					continue;
				QComboBox* cycleEnd = nullptr;
				for (QComboBox* combo : dialog->findChildren<QComboBox*>())
					if (combo->count() >= 3
							&& combo->itemText(0).startsWith(QStringLiteral("Select the first train")))
						cycleEnd = combo;
				QDialogButtonBox* buttons = dialog->findChild<QDialogButtonBox*>();
				if (!cycleEnd || !buttons || !buttons->button(QDialogButtonBox::Ok)) {
					fail(QStringLiteral("capacity scope controls are incomplete"));
					return;
				}
				cycleEnd->setCurrentIndex(2);
				buttons->button(QDialogButtonBox::Ok)->click();
				return;
			}
			QTimer::singleShot(25, this, [poll]() { (*poll)(); });
		};
		QTimer::singleShot(0, this, [poll]() { (*poll)(); });
	};
	auto emitPath = [](const QString& value) { return QFileInfo(value).absoluteFilePath(); };
	auto facet = [&](const char* name) {
		const int index = m_infrastructureFacetCombo->findData(QString::fromLatin1(name));
		if (index < 0)
			return false;
		m_infrastructureFacetCombo->setCurrentIndex(index);
		process();
		return m_infrastructureFacetCombo->currentData().toString() == QString::fromLatin1(name);
	};
	auto addRow = [&](const char* name) {
		if (!facet(name) || !m_addInfrastructureButton->isEnabled())
			return false;
		m_addInfrastructureButton->click();
		process();
		return m_infrastructureTable->rowCount() > 0;
	};
	auto rowFor = [&](const QString& id) {
		for (int row = 0; row < m_infrastructureTable->rowCount(); ++row)
			if (m_infrastructureTable->item(row, 0)
					&& m_infrastructureTable->item(row, 0)->text() == id)
				return row;
		return -1;
	};
	auto setCell = [&](const char* name, const QString& id, int column, const QString& value) {
		if (!facet(name))
			return false;
		const int row = rowFor(id);
		if (row < 0 || !m_infrastructureTable->item(row, column))
			return false;
		m_infrastructureTable->setCurrentCell(row, column);
		m_infrastructureTable->item(row, column)->setText(value);
		process();
		return true;
	};
	auto setSection = [&](const char* name, int row, int column, const QString& prefix) {
		if (!facet(name))
			return QString();
		auto* combo = qobject_cast<QComboBox*>(m_infrastructureTable->cellWidget(row, column));
		if (!combo)
			return QString();
		for (int index = 0; index < combo->count(); ++index) {
			if (combo->itemData(index).toString().isEmpty()
					|| (!prefix.isEmpty() && !combo->itemText(index).startsWith(prefix)))
				continue;
			combo->setCurrentIndex(index);
			process();
			return combo->itemData(index).toString();
		}
		return QString();
	};

	if (m_creatorAcceptancePhase == 0) {
		// The first observable operation in this mode is the public New action.
		if (!m_newSceneAction) {
			fail(QStringLiteral("actionNewCaseStudy is unavailable"));
			return;
		}
		m_newSceneAction->trigger();
		marker("E2E_CREATOR_NEW_CASE_OK");
		next();
		return;
	}

	if (m_creatorAcceptancePhase == 1) {
		if (!m_sceneLoaded || !m_infrastructureFacetCombo || !m_infrastructureTable
				|| !m_addInfrastructureButton || !m_caseSettingsDock) {
			fail(QStringLiteral("blank case or creator controls did not load"));
			return;
		}
		m_caseSettingsDock->show();
		m_caseSettingsDock->raise();
		if (!editLine(m_caseNameEdit, QStringLiteral("creator_acceptance_case"))
			|| !editLine(m_caseDescriptionEdit, QStringLiteral("synthetic creator acceptance"))
			|| !editLine(m_caseBaseTimeEdit, QStringLiteral("09:15:30"))) {
			fail(QStringLiteral("case metadata editing did not commit"));
			return;
		}
		m_caseDurationSecondsEdit->setValue(900.0);
		m_caseBufferSecondsEdit->setValue(30.0);
		m_caseRecoveryPercentEdit->setValue(5.0);
		process();

		if (!addRow("tracks")) {
			fail(QStringLiteral("track add 1 failed"));
			return;
		}
		if (!facet("tracks") || m_infrastructureTable->rowCount() < 1
				|| !m_infrastructureTable->item(0, 0)) {
			fail(QStringLiteral("track row 1 unavailable"));
			return;
		}
		m_infrastructureTable->item(0, 0)->setText("creator-main");
		process();
		if (m_infrastructureTable->item(0, 0)->text() != "creator-main") {
			fail(QStringLiteral("track rename 1 failed"));
			return;
		}
		if (!addRow("tracks")) {
			fail(QStringLiteral("track add 2 failed"));
			return;
		}
		if (!facet("tracks") || m_infrastructureTable->rowCount() < 2
				|| !m_infrastructureTable->item(1, 0)) {
			fail(QStringLiteral("track row 2 unavailable"));
			return;
		}
		m_infrastructureTable->item(1, 0)->setText("creator-yard");
		process();
		if (m_infrastructureTable->item(1, 0)->text() != "creator-yard") {
			fail(QStringLiteral("track rename 2 failed"));
			return;
		}
		const std::array<QString, 6> nodeIds = {QStringLiteral("creator-main-node-0"), QStringLiteral("creator-main-node-1"), QStringLiteral("creator-main-node-2"), QStringLiteral("creator-yard-node-0"), QStringLiteral("creator-yard-node-1"), QStringLiteral("creator-yard-node-2")};
		const std::array<double, 6> nodeX = {0.0, 1.0, 2.0, 0.0, 1.5, 2.0};
		const std::array<double, 6> nodeY = {0.0, 0.25, 0.0, 1.0, 1.0, 1.0};
		for (int row = 0; row < 6; ++row) {
			if (!addRow("nodes")) {
				fail(QStringLiteral("node row add failed"));
				return;
			}
			const int createdRow = m_infrastructureTable->rowCount() - 1;
			const QString generated = m_infrastructureTable->item(createdRow, 0)
				? m_infrastructureTable->item(createdRow, 0)->text() : QString();
			const QString track = row < 3 ? QStringLiteral("creator-main") : QStringLiteral("creator-yard");
			if (!setCell("nodes", generated, 0, nodeIds[static_cast<std::size_t>(row)])
				|| !setCell("nodes", nodeIds[static_cast<std::size_t>(row)], 1, track)
				|| !setCell("nodes", nodeIds[static_cast<std::size_t>(row)], 2, QString::number(nodeX[static_cast<std::size_t>(row)]))
				|| !setCell("nodes", nodeIds[static_cast<std::size_t>(row)], 3, QString::number(nodeY[static_cast<std::size_t>(row)]))) {
				fail(QStringLiteral("node geometry editing failed"));
				return;
			}
		}
		const std::array<QString, 4> arcIds = {QStringLiteral("creator-main-arc-0"), QStringLiteral("creator-main-arc-1"), QStringLiteral("creator-yard-arc-0"), QStringLiteral("creator-yard-arc-1")};
		for (int row = 0; row < 4; ++row) {
			if (!addRow("arcs")) {
				fail(QStringLiteral("arc row add failed"));
				return;
			}
			const int createdRow = m_infrastructureTable->rowCount() - 1;
			const QString generated = m_infrastructureTable->item(createdRow, 0)
				? m_infrastructureTable->item(createdRow, 0)->text() : QString();
			const QString track = row < 2 ? QStringLiteral("creator-main") : QStringLiteral("creator-yard");
			const int nodeOffset = row < 2 ? 0 : 3;
			const int localRow = row % 2;
			const QString from = nodeIds[static_cast<std::size_t>(nodeOffset + localRow)];
			const QString to = nodeIds[static_cast<std::size_t>(nodeOffset + localRow + 1)];
			QString arcFailure;
			if (!setCell("arcs", generated, 0, arcIds[static_cast<std::size_t>(row)]))
				arcFailure = QStringLiteral("id");
			else if (!setCell("arcs", arcIds[static_cast<std::size_t>(row)], 1, track))
				arcFailure = QStringLiteral("track");
			else if (!setCell("arcs", arcIds[static_cast<std::size_t>(row)], 2, from))
				arcFailure = QStringLiteral("from");
			else if (!setCell("arcs", arcIds[static_cast<std::size_t>(row)], 3, to))
				arcFailure = QStringLiteral("to");
			else if (!setCell("arcs", arcIds[static_cast<std::size_t>(row)], 4, row == 1 ? QStringLiteral("1250.5") : QStringLiteral("0")))
				arcFailure = QStringLiteral("curvature");
			else if (!setCell("arcs", arcIds[static_cast<std::size_t>(row)], 5, row == 0 ? QStringLiteral("-0.001953125") : (row == 1 ? QStringLiteral("0.00390625") : QStringLiteral("0.0009765625"))))
				arcFailure = QStringLiteral("gradient");
			else if (!setCell("arcs", arcIds[static_cast<std::size_t>(row)], 6, row < 2 ? QStringLiteral("22.5") : QStringLiteral("18")))
				arcFailure = QStringLiteral("speed");
			if (!arcFailure.isEmpty()) {
				fail(QStringLiteral("arc geometry editing failed (%1 row %2)").arg(arcFailure).arg(row));
				return;
			}
		}
		if (!facet("blocks")) {
			fail(QStringLiteral("block facet unavailable"));
			return;
		}
		for (const QString& track : {QStringLiteral("creator-main"), QStringLiteral("creator-yard")}) {
			const int filterIndex = m_blockTrackFilterCombo->findData(track);
			if (filterIndex < 0) {
				fail(QStringLiteral("block track selector unavailable"));
				return;
			}
			m_blockTrackFilterCombo->setCurrentIndex(filterIndex);
			process();
			for (int count = 0; count < 2; ++count) {
				m_addInfrastructureButton->click();
				process();
			}
			if (m_infrastructureTable->rowCount() != 2) {
				fail(QStringLiteral("base block rows did not add"));
				return;
			}
			const QString firstGenerated = m_infrastructureTable->item(0, 0)
				? m_infrastructureTable->item(0, 0)->text() : QString();
			const QString secondGenerated = m_infrastructureTable->item(1, 0)
				? m_infrastructureTable->item(1, 0)->text() : QString();
			const bool yard = track == QStringLiteral("creator-yard");
			QString blockFailure;
			if (!setCell("blocks", firstGenerated, 0, track + QStringLiteral("-block-0")))
				blockFailure = QStringLiteral("first id");
			else if (!setCell("blocks", secondGenerated, 0, track + QStringLiteral("-block-2")))
				blockFailure = QStringLiteral("second id");
			else if (!setCell("blocks", track + QStringLiteral("-block-0"), 2,
					yard ? QStringLiteral("0.5") : QStringLiteral("0.75")))
				blockFailure = QStringLiteral("first length");
			else if (!setCell("blocks", track + QStringLiteral("-block-2"), 2,
					yard ? QStringLiteral("0.25") : QStringLiteral("0.75")))
				blockFailure = QStringLiteral("second length");
			if (!blockFailure.isEmpty()) {
				fail(QStringLiteral("base block fields did not commit (%1, %2)")
					.arg(track, blockFailure));
				return;
			}
			m_infrastructureTable->setCurrentCell(1, 0);
			m_insertBlockButton->click();
			process();
			if (m_infrastructureTable->rowCount() != 3) {
				fail(QStringLiteral("block insert did not create the third visible block"));
				return;
			}
			const QString insertedGenerated = m_infrastructureTable->item(1, 0)
				? m_infrastructureTable->item(1, 0)->text() : QString();
			if (!setCell("blocks", insertedGenerated, 0, track + QStringLiteral("-block-1"))
				|| !setCell("blocks", track + QStringLiteral("-block-1"), 2,
					yard ? QStringLiteral("1.25") : QStringLiteral("0.5"))) {
				fail(QStringLiteral("inserted block fields did not commit"));
				return;
			}
			m_infrastructureTable->setCurrentCell(1, 0);
			m_moveBlockDownButton->click();
			process();
			m_moveBlockUpButton->click();
			process();
		}
		marker("E2E_CREATOR_INFRASTRUCTURE_OK");
		next();
		return;
	}

	if (m_creatorAcceptancePhase == 2) {
		if (!m_sceneLoaded || !m_infrastructureFacetCombo || !m_infrastructureTable
				|| !m_addInfrastructureButton || !m_deleteInfrastructureButton) {
			fail(QStringLiteral("infrastructure controls disappeared before signalling authoring"));
			return;
		}
		if (!addRow("stations") || !setCell("stations", "station", 0, "creator-station-a")
			|| !setCell("stations", "creator-station-a", 1, "Creator A")
			|| !setCell("stations", "creator-station-a", 2, "true")
			|| !setCell("stations", "creator-station-a", 3, "1.0")
			|| !addRow("stations") || !setCell("stations", "station", 0, "creator-station-b")
			|| !setCell("stations", "creator-station-b", 1, "Creator B")
			|| !setCell("stations", "creator-station-b", 2, "true")
			|| !setCell("stations", "creator-station-b", 3, "1.5")) {
			fail(QStringLiteral("station geometry authoring failed"));
			return;
		}
		if (!addRow("platforms") || !addRow("platforms")
				|| m_infrastructureTable->rowCount() != 2) {
			fail(QStringLiteral("platform rows did not add"));
			return;
		}
		auto setPlatformCell = [&](int row, int column, const QString& value) {
			if (!facet("platforms") || row < 0 || row >= m_infrastructureTable->rowCount()
					|| !m_infrastructureTable->item(row, column))
				return false;
			m_infrastructureTable->setCurrentCell(row, column);
			m_infrastructureTable->item(row, column)->setText(value);
			process();
			return true;
		};
		if (!setPlatformCell(0, 0, "creator-station-a")
				|| !setPlatformCell(0, 1, "creator-platform-a")
				|| !setPlatformCell(0, 2, "creator-main-node-1")
				|| !setPlatformCell(1, 0, "creator-station-b")
				|| !setPlatformCell(1, 1, "creator-platform-b")
				|| !setPlatformCell(1, 2, "creator-yard-node-1")) {
			fail(QStringLiteral("platform station/node references did not commit"));
			return;
		}
		if (!facet("platforms")) {
			fail(QStringLiteral("platform facet unavailable for geometry"));
			return;
		}
		for (int row = 0; row < m_infrastructureTable->rowCount(); ++row) {
			if (auto* length = qobject_cast<QDoubleSpinBox*>(m_infrastructureTable->cellWidget(row, 3)))
				length->setValue(123.75);
			if (auto* width = qobject_cast<QDoubleSpinBox*>(m_infrastructureTable->cellWidget(row, 4)))
				width->setValue(4.25);
			process();
		}
		if (!addRow("connections")
				|| !setCell("connections", "connection", 0, "creator-switch")
				|| !setCell("connections", "creator-switch", 1, "creator-main-node-1")
				|| !setCell("connections", "creator-switch", 2, "creator-yard-node-1")
				|| !setCell("connections", "creator-switch", 3, "true")
				|| !setCell("connections", "creator-switch", 4, "9.25")) {
			fail(QStringLiteral("connection authoring failed"));
			return;
		}
		if (!addRow("signals") || !setCell("signals", "signal", 0, "creator-signal")) {
			fail(QStringLiteral("signal row did not add"));
			return;
		}
		const QString mainSection = setSection("signals", 0, 1,
			QStringLiteral("base block creator-main-block-0 /"));
		if (mainSection.isEmpty()) {
			fail(QStringLiteral("signal protected-section selector lacked the authored block"));
			return;
		}
		if (!addRow("signalling_areas")
				|| !setCell("signalling_areas", "signalling-area", 0, "creator-signalling-area")
				|| !setCell("signalling_areas", "creator-signalling-area", 1, "0")
				|| !setCell("signalling_areas", "creator-signalling-area", 2, "2")
				|| !setCell("signalling_areas", "creator-signalling-area", 3, "0")) {
			fail(QStringLiteral("signalling area authoring failed"));
			return;
		}
		if (!addRow("routes")
				|| !setCell("routes", "route", 0, "creator-route")
				|| !setCell("routes", "creator-route", 2, "true")
				|| !setCell("routes", "creator-route", 3, "creator-corridor")
				|| !setCell("routes", "creator-route", 4, "false")) {
			fail(QStringLiteral("route metadata authoring failed"));
			return;
		}
		if (!facet("routes") || !m_routeSectionCatalogCombo || !m_addRouteSectionButton
				|| !m_routeSectionListWidget) {
			fail(QStringLiteral("route section controls unavailable"));
			return;
		}
		const std::array<QString, 3> routePrefixes = {
			QStringLiteral("base block creator-main-block-0 /"),
			QStringLiteral("connection creator-switch /"),
			QStringLiteral("base block creator-yard-block-2 /")};
		for (const QString& prefix : routePrefixes) {
			QString raw;
			for (int index = 0; index < m_routeSectionCatalogCombo->count(); ++index) {
				if (m_routeSectionCatalogCombo->itemText(index).startsWith(prefix)) {
					m_routeSectionCatalogCombo->setCurrentIndex(index);
					raw = m_routeSectionCatalogCombo->itemData(index).toString();
					break;
				}
			}
			if (raw.isEmpty()) {
				fail(QStringLiteral("route catalog lacked creator-facing section: ") + prefix);
				return;
			}
			m_addRouteSectionButton->click();
			process();
		}
		if (m_routeSectionListWidget->count() != 3) {
			fail(QStringLiteral("route section order was not authored"));
			return;
		}
		if (!addRow("block_dependencies")) {
			fail(QStringLiteral("dependency row did not add"));
			return;
		}
		if (setSection("block_dependencies", 0, 0,
				QStringLiteral("base block creator-main-block-0 /")).isEmpty()
			|| setSection("block_dependencies", 0, 1,
				QStringLiteral("base block creator-yard-block-2 /")).isEmpty()) {
			fail(QStringLiteral("dependency typed selectors lacked safe sections"));
			return;
		}
		if (!addRow("single_track_restrictions")) {
			fail(QStringLiteral("restriction row did not add"));
			return;
		}
		const std::array<QString, 4> restrictionPrefixes = {
			QStringLiteral("base block creator-main-block-0 /"),
			QStringLiteral("base block creator-yard-block-2 /"),
			QStringLiteral("connection creator-switch /"),
			QStringLiteral("base block creator-yard-block-2 /")};
		for (int column = 0; column < 4; ++column)
			if (setSection("single_track_restrictions", 0, column,
					restrictionPrefixes[static_cast<std::size_t>(column)]).isEmpty()) {
				fail(QStringLiteral("restriction typed selector lacked a safe section"));
				return;
			}
		if (!addRow("station_boundaries")
				|| setSection("station_boundaries", 0, 0,
					QStringLiteral("base block creator-main-block-0 /")).isEmpty()
				|| setSection("station_boundaries", 0, 1,
					QStringLiteral("base block creator-yard-block-2 /")).isEmpty()) {
			fail(QStringLiteral("station boundary typed selectors lacked safe sections"));
			return;
		}
		const QString renamedBlock = QStringLiteral("creator-main-block-renamed");
		if (!facet("blocks")) {
			fail(QStringLiteral("block facet unavailable for reference rename"));
			return;
		}
		const int mainTrackIndex = m_blockTrackFilterCombo->findData("creator-main");
		if (mainTrackIndex < 0) {
			fail(QStringLiteral("main block filter disappeared"));
			return;
		}
		m_blockTrackFilterCombo->setCurrentIndex(mainTrackIndex);
		process();
		const int mainBlockRow = rowFor(QStringLiteral("creator-main-block-0"));
		if (mainBlockRow < 0 || !m_infrastructureTable->item(mainBlockRow, 0)) {
			fail(QStringLiteral("referenced block row was not visible"));
			return;
		}
		m_infrastructureTable->item(mainBlockRow, 0)->setText(renamedBlock);
		process();
		bool referenceStillValid = false;
		if (m_sceneModel.routes.size() == 1 && m_sceneModel.routes.front().blocks.size() == 3
				&& m_sceneModel.blockDependencies.size() == 1
				&& m_sceneModel.singleTrackRestrictions.size() == 1
				&& m_sceneModel.stationBoundaries.size() == 1
					&& !sceneSignals(m_sceneModel).empty()) {
			const auto mentions = [&renamedBlock](const std::string& value) {
				return value.find(renamedBlock.toStdString()) != std::string::npos;
			};
			const auto& route = m_sceneModel.routes.front();
			const auto& dependency = m_sceneModel.blockDependencies.front();
			const auto& restriction = m_sceneModel.singleTrackRestrictions.front();
			const auto& boundary = m_sceneModel.stationBoundaries.front();
			referenceStillValid = mentions(route.blocks.front()) && mentions(dependency.block)
				&& mentions(restriction.startBlock) && mentions(boundary.entranceBlock)
				&& mentions(sceneSignals(m_sceneModel).front().protectedSection);
		}
		if (!referenceStillValid) {
			fail(QStringLiteral("public block rename did not retain all references"));
			return;
		}
		const std::size_t blockCount = m_sceneModel.blocks.size();
		const int renamedRow = rowFor(renamedBlock);
		if (renamedRow < 0) {
			fail(QStringLiteral("renamed block could not be selected for delete check"));
			return;
		}
		m_infrastructureTable->setCurrentCell(renamedRow, 0);
		m_deleteInfrastructureButton->click();
		process();
		if (m_sceneModel.blocks.size() != blockCount) {
			fail(QStringLiteral("referenced delete changed canonical data"));
			return;
		}
		marker("E2E_CREATOR_STATIONS_SIGNALLING_OK");
		next();
		return;
	}

	if (m_creatorAcceptancePhase == 3) {
		if (!m_sceneLoaded || !m_trainUnitDock || !m_trainUnitListWidget
				|| !m_addTrainUnitButton || !m_trainUnitIdEdit || !m_compositionDock
				|| !m_addCompositionButton || !m_addUnitButton || !m_addTrainUnitTractionButton) {
			fail(QStringLiteral("rolling-stock creator controls are unavailable"));
			return;
		}
		m_trainUnitDock->show();
		m_trainUnitDock->raise();
		auto setUnit = [&](int row, const QString& id, double seed) {
			m_trainUnitListWidget->setCurrentRow(row);
			process();
			if (!editLine(m_trainUnitIdEdit, id))
				return false;
			const std::array<double, 9> values = {90000.0 + seed, 22000.0 + seed,
				2.0, 30.0, 0.8, 9.0, 0.002, 0.4, 80.0};
			for (std::size_t index = 0; index < values.size(); ++index) {
				if (!m_trainUnitPhysicalEdits[index])
					return false;
				m_trainUnitPhysicalEdits[index]->setValue(values[index]);
			}
			if (!editLine(m_trainUnitSourceDataEdit, QStringLiteral("creator-parameters.csv"))
				|| !editLine(m_trainUnitSourceTractionEdit, QStringLiteral("creator-traction.csv")))
				return false;
			m_addTrainUnitTractionButton->click();
			process();
			if (!m_trainUnitTractionTable || m_trainUnitTractionTable->rowCount() < 1)
				return false;
			const std::array<double, 5> traction = {0.0, 30.0, 220000.0, -3500.0, 0.0};
			for (int column = 0; column < 5; ++column)
				if (auto* edit = qobject_cast<QDoubleSpinBox*>(m_trainUnitTractionTable->cellWidget(0, column)))
					edit->setValue(traction[static_cast<std::size_t>(column)]);
				else
					return false;
			process();
			return true;
		};
		m_addTrainUnitButton->click();
		process();
		if (m_trainUnitListWidget->count() != 1 || !setUnit(0, QStringLiteral("creator-unit-a"), 0.0)) {
			fail(QStringLiteral("first train unit did not author through controls"));
			return;
		}
		m_addTrainUnitButton->click();
		process();
		if (m_trainUnitListWidget->count() != 2 || !setUnit(1, QStringLiteral("creator-unit-b"), 5000.0)) {
			fail(QStringLiteral("second train unit did not author through controls"));
			return;
		}
		m_compositionDock->show();
		m_compositionDock->raise();
		m_addCompositionButton->click();
		process();
		if (m_compositionListWidget->count() != 1
				|| !editLine(m_compositionIdEdit, QStringLiteral("creator-composition"))) {
			fail(QStringLiteral("composition row did not author through controls"));
			return;
		}
		acceptInputDialog(QStringLiteral("creator-unit-a"));
		m_addUnitButton->click();
		process();
		acceptInputDialog(QStringLiteral("creator-unit-b"));
		m_addUnitButton->click();
		process();
		if (m_compositionUnitsListWidget->count() != 2
				|| m_sceneModel.compositions.empty()
				|| m_sceneModel.compositions.front().units.size() != 2
				|| m_sceneModel.compositions.front().units.front() != "creator-unit-a"
				|| m_sceneModel.compositions.front().units.back() != "creator-unit-b") {
			fail(QStringLiteral("composition membership did not retain both units in order"));
			return;
		}
		marker("E2E_CREATOR_ROLLING_STOCK_OK");
		next();
		return;
	}

	if (m_creatorAcceptancePhase == 4) {
		if (!m_sceneLoaded || !m_serviceDock || !m_serviceListWidget || !m_addServiceButton
				|| !m_serviceIdEdit || !m_serviceOperatingCodeEdit || !m_serviceCompositionCombo
				|| !m_serviceRouteCombo || !m_addStopButton || !m_stopListWidget
				|| !m_stopStationCombo || !m_stopPlatformCombo) {
			fail(QStringLiteral("service creator controls are unavailable"));
			return;
		}
		m_serviceDock->show();
		m_serviceDock->raise();
		m_addServiceButton->click();
		process();
		if (m_serviceListWidget->count() != 1
				|| !editLine(m_serviceIdEdit, QStringLiteral("creator-service"))
				|| !editLine(m_serviceOperatingCodeEdit, QStringLiteral("1701"))
				|| !choose(m_serviceCompositionCombo, QStringLiteral("creator-composition"))
				|| !choose(m_serviceRouteCombo, QStringLiteral("creator-route"))) {
			fail(QStringLiteral("service identity or typed references did not commit"));
			return;
		}
		m_serviceThroughCheck->setChecked(false);
		m_serviceHasEntryTimeCheck->setChecked(true);
		if (!editLine(m_serviceEntryTimeSecondsEdit, QStringLiteral("60"))) {
			fail(QStringLiteral("service entry time did not commit"));
			return;
		}
		m_serviceHasRepeatCheck->setChecked(true);
		if (!editLine(m_serviceHeadwaySecondsEdit, QStringLiteral("300"))) {
			fail(QStringLiteral("service headway did not commit"));
			return;
		}
		m_serviceHasRepeatCountCheck->setChecked(true);
		if (!editLine(m_serviceRepeatCountEdit, QStringLiteral("3"))) {
			fail(QStringLiteral("service repeat count did not commit"));
			return;
		}
		m_servicePerformancePercentEdit->setValue(92.0);
		m_serviceHasMaximumSpeedCheck->setChecked(true);
		m_serviceMaximumSpeedKmhEdit->setValue(100.0);
		m_serviceHasOperatingCodeStepCheck->setChecked(true);
		if (!editLine(m_serviceOperatingCodeStepEdit, QStringLiteral("1"))) {
			fail(QStringLiteral("service operating-code increment did not commit"));
			return;
		}
		const auto configureStop = [&](const QString& station, const QString& platform,
				double arrival, double departure, double dwell) {
			if (!choose(m_stopStationCombo, station) || !choose(m_stopPlatformCombo, platform))
				return false;
			m_stopHasArrivalCheck->setChecked(true);
			m_stopHasDepartureCheck->setChecked(true);
			if (!editLine(m_stopArrivalSecondsEdit, QString::number(arrival))
					|| !editLine(m_stopDepartureSecondsEdit, QString::number(departure))
					|| !editLine(m_stopDwellSecondsEdit, QString::number(dwell)))
				return false;
			QMetaObject::invokeMethod(m_stopArrivalSecondsEdit, "editingFinished", Qt::DirectConnection);
			QMetaObject::invokeMethod(m_stopDepartureSecondsEdit, "editingFinished", Qt::DirectConnection);
			QMetaObject::invokeMethod(m_stopDwellSecondsEdit, "editingFinished", Qt::DirectConnection);
			process();
			return true;
		};
		m_addStopButton->click();
		process();
		if (!configureStop(QStringLiteral("creator-station-a"), QStringLiteral("creator-platform-a"),
				180.0, 240.0, 60.0)) {
			fail(QStringLiteral("first service stop did not commit station/platform timetable"));
			return;
		}
		m_addStopButton->click();
		process();
		if (!configureStop(QStringLiteral("creator-station-b"), QStringLiteral("creator-platform-b"),
				480.0, 540.0, 60.0)) {
			fail(QStringLiteral("second service stop did not commit station/platform timetable"));
			return;
		}
		if (m_sceneModel.services.empty() || m_sceneModel.services.front().stops.size() != 2
				|| !m_sceneModel.services.front().hasRepeat
				|| !m_sceneModel.services.front().hasRepeatCount
				|| m_sceneModel.services.front().repeatCount != 3
				|| m_sceneModel.services.front().headwaySeconds != 300.0
				|| m_sceneModel.services.front().stops.front().stationId != "creator-station-a"
				|| m_sceneModel.services.front().stops.back().stationId != "creator-station-b"
				|| !m_sceneModel.services.front().stops.front().hasPlannedArrival
				|| !m_sceneModel.services.front().stops.front().hasPlannedDeparture
				|| m_sceneModel.services.front().stops.front().plannedArrivalSeconds != 180.0
				|| m_sceneModel.services.front().stops.front().plannedDepartureSeconds != 240.0
				|| m_sceneModel.services.front().stops.front().dwellSeconds != 60.0
				|| m_sceneModel.services.front().stops.back().plannedArrivalSeconds != 480.0
				|| m_sceneModel.services.front().stops.back().plannedDepartureSeconds != 540.0
				|| m_sceneModel.services.front().stops.back().dwellSeconds != 60.0) {
			const SceneService* service = m_sceneModel.services.empty()
				? nullptr : &m_sceneModel.services.front();
			fail(QStringLiteral("service repeat or ordered stop values were not retained: stops=%1 repeat=%2 count-flag=%3 count=%4 headway=%5 first=%6 last=%7 arrival=%8 departure=%9 dwell=%10")
				.arg(service ? static_cast<int>(service->stops.size()) : -1)
				.arg(service && service->hasRepeat)
				.arg(service && service->hasRepeatCount)
				.arg(service ? service->repeatCount : -1)
				.arg(service ? service->headwaySeconds : -1.0)
				.arg(service && !service->stops.empty() ? QString::fromStdString(service->stops.front().stationId) : QStringLiteral("-"))
				.arg(service && !service->stops.empty() ? QString::fromStdString(service->stops.back().stationId) : QStringLiteral("-"))
				.arg(service && !service->stops.empty() && service->stops.front().hasPlannedArrival)
				.arg(service && !service->stops.empty() && service->stops.front().hasPlannedDeparture)
				.arg(service && !service->stops.empty() ? service->stops.front().dwellSeconds : -1.0));
			return;
		}
		if (m_serviceOccurrenceTable->rowCount() != 3 || hasErrors(m_sceneDiagnostics)) {
			QString detail;
			for (const SceneDiagnostic& diagnostic : m_sceneDiagnostics)
				if (diagnostic.severity == SceneSeverity::Error) {
					detail = QString::fromStdString(toDisplayText(diagnostic));
					break;
				}
			fail(QStringLiteral("service occurrence preview or validation is not runnable: rows=%1 %2")
				.arg(m_serviceOccurrenceTable->rowCount()).arg(detail));
			return;
		}
		marker("E2E_CREATOR_SERVICE_OK");
		next();
		return;
	}

	if (m_creatorAcceptancePhase == 5) {
		if (!m_sceneLoaded || !m_incidentDock || !m_scenarioListWidget
				|| !m_blankScenarioButton || !m_addIncidentButton || !m_addEntranceDelayButton
				|| !m_incidentHasReducedSpeedCheck || !m_incidentReducedSpeedKmhEdit) {
			fail(QStringLiteral("scenario creator controls are unavailable"));
			return;
		}
		m_incidentDock->show();
		m_incidentDock->raise();
		m_blankScenarioButton->click();
		process();
		if (!editLine(m_scenarioIdEdit, QStringLiteral("incident"))
				|| !editLine(m_scenarioNameEdit, QStringLiteral("Incident scenario"))
				|| !editLine(m_scenarioDescriptionEdit, QStringLiteral("signal and breakdown evidence"))) {
			fail(QStringLiteral("incident scenario metadata did not commit"));
			return;
		}
		m_addIncidentButton->click();
		process();
		if (!editLine(m_incidentIdEdit, QStringLiteral("creator-signal-failure"))
				|| !choose(m_incidentTargetCombo, QStringLiteral("creator-signal"))
				|| !editLine(m_incidentStartSecondsEdit, QStringLiteral("350"))
				|| !editLine(m_incidentEndSecondsEdit, QStringLiteral("500"))) {
			fail(QStringLiteral("signal failure incident did not bind through controls"));
			return;
		}
		m_addIncidentButton->click();
		process();
		m_incidentTypeCombo->setCurrentText(QStringLiteral("train_breakdown"));
		process();
		if (!editLine(m_incidentIdEdit, QStringLiteral("creator-breakdown"))
				|| !choose(m_incidentTargetCombo, QStringLiteral("creator-service"))
				|| !editLine(m_incidentStartSecondsEdit, QStringLiteral("60"))
				|| !editLine(m_incidentEndSecondsEdit, QStringLiteral("100"))) {
			fail(QStringLiteral("breakdown incident target or finite interval did not commit"));
			return;
		}
		m_incidentHasOccurrenceCheck->setChecked(true);
		if (!editLine(m_incidentOccurrenceEdit, QStringLiteral("1"))) {
			fail(QStringLiteral("breakdown occurrence did not commit"));
			return;
		}
		m_incidentHasReducedSpeedCheck->setChecked(true);
		m_incidentReducedSpeedKmhEdit->setValue(10.0);
		QMetaObject::invokeMethod(m_incidentReducedSpeedKmhEdit, "editingFinished", Qt::DirectConnection);
		process();
		m_blankScenarioButton->click();
		process();
		if (!editLine(m_scenarioIdEdit, QStringLiteral("entrance"))
				|| !editLine(m_scenarioNameEdit, QStringLiteral("Entrance delay"))
				|| !editLine(m_scenarioDescriptionEdit, QStringLiteral("positive entrance delay at station A"))) {
			fail(QStringLiteral("entrance scenario metadata did not commit"));
			return;
		}
		m_addEntranceDelayButton->click();
		process();
		if (!choose(m_entranceDelayServiceCombo, QStringLiteral("creator-service"))) {
			fail(QStringLiteral("entrance delay service selector lacked authored service"));
			return;
		}
		m_entranceDelayOccurrenceEdit->setValue(2);
		QMetaObject::invokeMethod(m_entranceDelayOccurrenceEdit, "editingFinished", Qt::DirectConnection);
		process();
		if (!choose(m_entranceDelayStationCombo, QStringLiteral("creator-station-a"))) {
			fail(QStringLiteral("entrance delay station selector lacked station A"));
			return;
		}
		m_entranceDelaySecondsEdit->setValue(90.0);
		QMetaObject::invokeMethod(m_entranceDelaySecondsEdit, "editingFinished", Qt::DirectConnection);
		process();
		if (m_sceneModel.scenarios.size() != 3
				|| m_sceneModel.scenarios[1].incidents.size() != 2
				|| m_sceneModel.scenarios[2].entranceDelays.size() != 1
				|| m_sceneModel.scenarios[2].entranceDelays.front().occurrence != 2
				|| m_sceneModel.scenarios[2].entranceDelays.front().delaySeconds <= 0.0
				|| hasErrors(m_sceneDiagnostics)) {
			QString detail;
			for (const SceneDiagnostic& diagnostic : m_sceneDiagnostics)
				if (diagnostic.severity == SceneSeverity::Error) {
					detail = QString::fromStdString(toDisplayText(diagnostic));
					break;
				}
			fail(QStringLiteral("scenario library or validation did not retain all three scenarios: scenarios=%1 incident-count=%2 delay-count=%3 occurrence=%4 seconds=%5 %6")
				.arg(static_cast<int>(m_sceneModel.scenarios.size()))
				.arg(m_sceneModel.scenarios.size() > 1 ? static_cast<int>(m_sceneModel.scenarios[1].incidents.size()) : -1)
				.arg(m_sceneModel.scenarios.size() > 2 ? static_cast<int>(m_sceneModel.scenarios[2].entranceDelays.size()) : -1)
				.arg(m_sceneModel.scenarios.size() > 2 && !m_sceneModel.scenarios[2].entranceDelays.empty() ? m_sceneModel.scenarios[2].entranceDelays.front().occurrence : -1)
				.arg(m_sceneModel.scenarios.size() > 2 && !m_sceneModel.scenarios[2].entranceDelays.empty() ? m_sceneModel.scenarios[2].entranceDelays.front().delaySeconds : -1.0)
				.arg(detail));
			return;
		}
		marker("E2E_CREATOR_SCENARIOS_OK");
		next();
		return;
	}

	if (m_creatorAcceptancePhase == 6) {
		if (!m_sceneLoaded || !m_passengerDock || !m_passengerListWidget
				|| !m_addPassengerButton || !m_addPassengerJourneyButton
				|| !m_addPassengerLegButton || !m_passengerTabs) {
			fail(QStringLiteral("passenger creator controls are unavailable"));
			return;
		}
		m_passengerDock->show();
		m_passengerDock->raise();
		m_addPassengerButton->click();
		process();
		if (m_passengerListWidget->count() != 1
				|| !editLine(m_passengerIdEdit, QStringLiteral("creator-passenger"))) {
			fail(QStringLiteral("passenger ID did not commit"));
			return;
		}
		m_addPassengerJourneyButton->click();
		process();
		if (!editLine(m_passengerJourneyIdEdit, QStringLiteral("creator-journey"))
				|| !editLine(m_passengerJourneyActivityEdit, QStringLiteral("creator commute"))
				|| !choose(m_passengerJourneyOriginCombo, QStringLiteral("creator-station-a"))
				|| !choose(m_passengerJourneyDestinationCombo, QStringLiteral("creator-station-b"))) {
			fail(QStringLiteral("journey station or metadata controls did not commit"));
			return;
		}
		const std::array<double, 4> windows = {120.0, 420.0, 300.0, 720.0};
		for (std::size_t index = 0; index < windows.size(); ++index)
			m_passengerJourneyWindowEdits[index]->setValue(windows[index]);
		process();
		m_passengerTabs->setCurrentIndex(1);
		m_addPassengerLegButton->click();
		process();
		if (!editLine(m_passengerLegIdEdit, QStringLiteral("creator-leg"))
				|| !choose(m_passengerLegOriginCombo, QStringLiteral("creator-station-a"))
				|| !choose(m_passengerLegDestinationCombo, QStringLiteral("creator-station-b"))
				|| !choose(m_passengerLegServiceCombo, QStringLiteral("creator-service"))) {
			fail(QStringLiteral("passenger leg public CRUD did not commit references"));
			return;
		}
		m_passengerLegOccurrenceEdit->setFocus(Qt::OtherFocusReason);
		m_passengerLegOccurrenceEdit->setValue(1);
		m_passengerLegOccurrenceEdit->clearFocus();
		process();
		if (m_sceneModel.passengers.size() != 1
				|| m_sceneModel.passengers.front().journeys.size() != 1
				|| m_sceneModel.passengers.front().journeys.front().legs.size() != 1
				|| m_sceneModel.passengers.front().journeys.front().legs.front().occurrence != 1
				|| hasErrors(m_sceneDiagnostics)) {
			fail(QStringLiteral("passenger journey/leg or validation did not retain authored values"));
			return;
		}
		marker("E2E_CREATOR_PASSENGER_OK");
		next();
		return;
	}

	if (m_creatorAcceptancePhase == 7) {
		const QString folder = emitPath(qEnvironmentVariable("QEGTRAIN_E2E_CREATOR_FOLDER"));
		if (!m_sceneLoaded || folder.isEmpty() || !m_saveSceneAsFolderAction
				|| !m_newSceneAction || !m_trainUnitListWidget) {
			fail(QStringLiteral("folder persistence controls or target are unavailable"));
			return;
		}
		activateWindow();
		m_trainUnitDock->show();
		m_trainUnitDock->raise();
		process();
		m_trainUnitListWidget->setCurrentRow(0);
		process();
		QLineEdit* pendingMass = m_trainUnitPhysicalEdits[0]
			? m_trainUnitPhysicalEdits[0]->findChild<QLineEdit*>() : nullptr;
		if (!pendingMass) {
			fail(QStringLiteral("focused train-unit mass editor is unavailable"));
			return;
		}
		pendingMass->setFocus(Qt::OtherFocusReason);
		pendingMass->setText(QStringLiteral("91000"));
		process();
		if (!pendingMass->hasFocus()) {
			fail(QStringLiteral("focused train-unit mass editor did not retain focus before Save As"));
			return;
		}
		acceptFileDialog(folder, true);
		m_saveSceneAsFolderAction->trigger();
		process();
		if (QFileInfo(m_sceneDir).absoluteFilePath() != folder || m_sceneIsBundle
				|| m_sceneDirty || !QFileInfo(QDir(folder).filePath("scene.json")).exists()) {
			fail(QStringLiteral("public Save Scene As Folder did not persist the focused physical value"));
			return;
		}
		m_newSceneAction->trigger();
		process();
		QAction* openFolder = findChild<QAction*>("actionOpenSceneFolder");
		if (!openFolder) {
			fail(QStringLiteral("public Open Scene Folder action is unavailable"));
			return;
		}
		acceptFileDialog(folder, true);
		acceptMessageBox(QMessageBox::Discard);
		openFolder->trigger();
		process();
		if (QFileInfo(m_sceneDir).absoluteFilePath() != folder || m_sceneIsBundle
				|| m_sceneModel.name != "creator_acceptance_case"
				|| m_sceneModel.tracks.size() != 2 || m_sceneModel.nodes.size() != 6
				|| m_sceneModel.arcs.size() != 4 || m_sceneModel.blocks.size() != 6
				|| m_sceneModel.connections.size() != 1 || m_sceneModel.stations.size() != 2
				|| m_sceneModel.trainUnits.size() != 2 || m_sceneModel.compositions.size() != 1
				|| m_sceneModel.services.size() != 1 || m_sceneModel.scenarios.size() != 3
				|| m_sceneModel.passengers.size() != 1
				|| m_sceneModel.trainUnits.front().physical.mass_of_traction_unit_kg != 91000.0
				|| m_sceneModel.passengers.front().id != "creator-passenger"
				|| m_sceneModel.passengers.front().journeys.size() != 1
				|| m_sceneModel.passengers.front().journeys.front().id != "creator-journey"
				|| hasErrors(m_sceneDiagnostics)) {
			fail(QStringLiteral("folder reopen did not retain complete creator rows"));
			return;
		}
		marker("E2E_CREATOR_FOLDER_ROUNDTRIP_OK");
		next();
		return;
	}

	if (m_creatorAcceptancePhase == 8) {
		const QString bundle = emitPath(qEnvironmentVariable("QEGTRAIN_E2E_CREATOR_BUNDLE"));
		if (!m_sceneLoaded || bundle.isEmpty() || !m_saveSceneAsAction || !m_newSceneAction) {
			fail(QStringLiteral("bundle persistence controls or target are unavailable"));
			return;
		}
		acceptFileDialog(bundle, false);
		m_saveSceneAsAction->trigger();
		process();
		if (QFileInfo(m_sceneDir).absoluteFilePath() != bundle || !m_sceneIsBundle
				|| m_sceneDirty || !QFileInfo(bundle).exists()) {
			fail(QStringLiteral("public Save Case Study As bundle did not complete"));
			return;
		}
		m_newSceneAction->trigger();
		process();
		QAction* openBundle = findChild<QAction*>("actionOpenCaseStudyBundle");
		if (!openBundle) {
			fail(QStringLiteral("public Open Case Study bundle action is unavailable"));
			return;
		}
		acceptFileDialog(bundle, false);
		acceptMessageBox(QMessageBox::Discard);
		openBundle->trigger();
		process();
		if (QFileInfo(m_sceneDir).absoluteFilePath() != bundle || !m_sceneIsBundle
				|| m_sceneModel.name != "creator_acceptance_case"
				|| m_sceneModel.blocks.size() != 6 || m_sceneModel.trainUnits.size() != 2
				|| m_sceneModel.compositions.size() != 1 || m_sceneModel.services.size() != 1
				|| m_sceneModel.scenarios.size() != 3 || m_sceneModel.passengers.size() != 1
				|| m_sceneModel.services.front().repeatCount != 3
				|| m_sceneModel.services.front().stops.size() != 2 || hasErrors(m_sceneDiagnostics)) {
			fail(QStringLiteral("bundle reopen did not retain complete creator IDs and values: path=%1 bundle=%2 name=%3 blocks=%4 units=%5 compositions=%6 services=%7 scenarios=%8 passengers=%9 repeat=%10 stops=%11 errors=%12")
				.arg(m_sceneDir).arg(m_sceneIsBundle).arg(QString::fromStdString(m_sceneModel.name))
				.arg(static_cast<int>(m_sceneModel.blocks.size()))
				.arg(static_cast<int>(m_sceneModel.trainUnits.size()))
				.arg(static_cast<int>(m_sceneModel.compositions.size()))
				.arg(static_cast<int>(m_sceneModel.services.size()))
				.arg(static_cast<int>(m_sceneModel.scenarios.size()))
				.arg(static_cast<int>(m_sceneModel.passengers.size()))
				.arg(m_sceneModel.services.empty() ? -1 : m_sceneModel.services.front().repeatCount)
				.arg(m_sceneModel.services.empty() ? -1 : static_cast<int>(m_sceneModel.services.front().stops.size()))
				.arg(hasErrors(m_sceneDiagnostics)));
			return;
		}
		if (!m_loadedDataDock || !m_loadedDataTree) {
			fail(QStringLiteral("Loaded Data review is unavailable after bundle reopen"));
			return;
		}
		if (!m_loadedDataDock->isVisible())
			m_loadedDataDock->toggleViewAction()->trigger();
		m_loadedDataDock->raise();
		process();
		QTreeWidgetItem* caseRoot = m_loadedDataTree->topLevelItemCount() > 0
			? m_loadedDataTree->topLevelItem(0) : nullptr;
		bool loadedDataOk = caseRoot && caseRoot->text(0) == "Case Study"
			&& QFileInfo(caseRoot->text(1)).absoluteFilePath() == bundle;
		for (const QString& label : {QStringLiteral("Infrastructure"), QStringLiteral("Scenarios"),
				QStringLiteral("Passengers"), QStringLiteral("Canonical schema version"),
				QStringLiteral("Bundle format version")}) {
			const QList<QTreeWidgetItem*> rows = m_loadedDataTree->findItems(
				label, Qt::MatchExactly | Qt::MatchRecursive, 0);
			loadedDataOk = loadedDataOk && !rows.isEmpty()
				&& rows.front()->text(3) != "Invalid" && rows.front()->text(3) != "Failed";
		}
		if (!m_loadedDataDock->isVisible() || !loadedDataOk) {
			fail(QStringLiteral("Loaded Data did not expose the reopened bundle inputs"));
			return;
		}
		marker("E2E_CREATOR_BUNDLE_ROUNDTRIP_OK");
		next();
		return;
	}

	if (m_creatorAcceptancePhase == 9) {
		if (!m_sceneLoaded || !m_runSceneAction || !m_serviceOccurrenceTable
				|| !m_scenarioListWidget || !m_setDelayBaselineButton) {
			fail(QStringLiteral("run and occurrence controls are unavailable"));
			return;
		}
		int baselineRow = -1;
		for (int row = 0; row < m_sceneModel.scenarios.size(); ++row)
			if (m_sceneModel.scenarios[static_cast<std::size_t>(row)].id == "baseline")
				baselineRow = row;
		if (baselineRow < 0) {
			fail(QStringLiteral("baseline scenario was not retained after bundle reopen"));
			return;
		}
		m_scenarioListWidget->setCurrentRow(baselineRow);
		process();
		if (m_serviceOccurrenceTable->rowCount() != 3) {
			fail(QStringLiteral("three service occurrences were not exposed after reopen"));
			return;
		}
		for (int row = 0; row < m_serviceOccurrenceTable->rowCount(); ++row) {
			if (auto* item = m_serviceOccurrenceTable->item(row, 0))
				item->setCheckState(row == 2 ? Qt::Unchecked : Qt::Checked);
		}
		process();
		m_creatorAcceptancePolls = 0;
		m_runSceneAction->trigger();
		marker("E2E_CREATOR_BASELINE_RUN_STARTED");
		next();
		return;
	}

	if (m_creatorAcceptancePhase == 10) {
		if (m_worker || !m_resultsAvailable) {
			if (++m_creatorAcceptancePolls > 600) {
				fail(QStringLiteral("baseline simulation did not complete"));
				return;
			}
			QTimer::singleShot(100, this, &MainWindow::runCreatorAcceptanceE2E);
			return;
		}
		m_creatorAcceptancePolls = 0;
		if (m_completedRunProvenance.appliedScenario != "baseline"
				|| m_completedRunProvenance.selectedOccurrences.size() != 2) {
			fail(QStringLiteral("baseline provenance did not retain scenario and selected occurrences 1+2"));
			return;
		}
		QPushButton* timeDistance = nullptr;
		for (QPushButton* button : findChildren<QPushButton*>())
			if (button->text() == QStringLiteral("Time / distance"))
				timeDistance = button;
		if (!timeDistance) {
			fail(QStringLiteral("public Time / distance result control is unavailable"));
			return;
		}
		timeDistance->click();
		process();
		for (QWidget* widget : QApplication::topLevelWidgets()) {
			if (auto* diagram = qobject_cast<DiagramWindow*>(widget))
				if (diagram->windowTitle().contains(QStringLiteral("Time vs Distance")))
					m_creatorBaselineDiagram = diagram;
		}
		if (!m_creatorBaselineDiagram) {
			fail(QStringLiteral("baseline result diagram did not open"));
			return;
		}
		m_setDelayBaselineButton->click();
		if (!m_delayBaseline) {
			fail(QStringLiteral("public delay baseline control did not freeze baseline"));
			return;
		}
		marker("E2E_CREATOR_BASELINE_RUN_OK");
		next();
		return;
	}

	if (m_creatorAcceptancePhase == 11) {
		int entranceRow = -1;
		for (int row = 0; row < m_sceneModel.scenarios.size(); ++row)
			if (m_sceneModel.scenarios[static_cast<std::size_t>(row)].id == "entrance")
				entranceRow = row;
		if (entranceRow < 0 || !m_scenarioListWidget || !m_runSceneAction) {
			fail(QStringLiteral("entrance scenario was not available for public run"));
			return;
		}
		m_scenarioListWidget->setCurrentRow(entranceRow);
		process();
		m_creatorAcceptancePolls = 0;
		m_runSceneAction->trigger();
		next();
		return;
	}

	if (m_creatorAcceptancePhase == 12) {
		if (m_worker || !m_resultsAvailable) {
			if (++m_creatorAcceptancePolls > 600) {
				fail(QStringLiteral("entrance-delay simulation did not complete"));
				return;
			}
			QTimer::singleShot(100, this, &MainWindow::runCreatorAcceptanceE2E);
			return;
		}
		m_creatorAcceptancePolls = 0;
		if (m_completedRunProvenance.appliedScenario != "entrance") {
			fail(QStringLiteral("entrance run did not stage the entrance scenario"));
			return;
		}
		const auto stationADeparture = [](const std::vector<TimetableResultRow>& rows)
				-> const TimetableResultRow* {
			for (const TimetableResultRow& row : rows)
				if (row.serviceId == "creator-service" && row.occurrence == 2
						&& row.stationId == "Creator A")
					return &row;
			return nullptr;
		};
		const TimetableResultRow* baselineDeparture = m_delayBaseline
			? stationADeparture(m_delayBaseline->timetable) : nullptr;
		const TimetableResultRow* delayedDeparture = stationADeparture(m_completedTimetableResults);
		if (!baselineDeparture || !delayedDeparture
				|| !baselineDeparture->plannedDepartureSeconds.available
				|| !delayedDeparture->plannedDepartureSeconds.available
				|| !baselineDeparture->departureDelaySeconds.available
				|| !delayedDeparture->departureDelaySeconds.available
				|| delayedDeparture->plannedDepartureSeconds.value
					- baselineDeparture->plannedDepartureSeconds.value < 89.0
				|| std::abs(delayedDeparture->departureDelaySeconds.value
					- baselineDeparture->departureDelaySeconds.value) < 1.0) {
			fail(QStringLiteral("entrance delay did not shift occurrence 2 station-A plan by 90 seconds and change its reported delay: baseline=%1/%2 delayed=%3/%4 rows=%5/%6")
				.arg(baselineDeparture && baselineDeparture->plannedDepartureSeconds.available
					? baselineDeparture->plannedDepartureSeconds.value : -1.0)
				.arg(baselineDeparture && baselineDeparture->departureDelaySeconds.available
					? baselineDeparture->departureDelaySeconds.value : -1.0)
				.arg(delayedDeparture && delayedDeparture->plannedDepartureSeconds.available
					? delayedDeparture->plannedDepartureSeconds.value : -1.0)
				.arg(delayedDeparture && delayedDeparture->departureDelaySeconds.available
					? delayedDeparture->departureDelaySeconds.value : -1.0)
				.arg(m_delayBaseline ? static_cast<int>(m_delayBaseline->timetable.size()) : -1)
				.arg(static_cast<int>(m_completedTimetableResults.size())));
			return;
		}
		marker("E2E_CREATOR_ENTRANCE_RUN_OK");
		int incidentRow = -1;
		for (int row = 0; row < m_sceneModel.scenarios.size(); ++row)
			if (m_sceneModel.scenarios[static_cast<std::size_t>(row)].id == "incident")
				incidentRow = row;
		if (incidentRow < 0 || !m_scenarioListWidget || !m_runSceneAction) {
			fail(QStringLiteral("incident scenario was not available for final public run"));
			return;
		}
		m_scenarioListWidget->setCurrentRow(incidentRow);
		process();
		m_creatorAcceptancePolls = 0;
		m_runSceneAction->trigger();
		next();
		return;
	}

	if (m_creatorAcceptancePhase == 13) {
		if (m_worker || !m_resultsAvailable) {
			if (++m_creatorAcceptancePolls > 600) {
				fail(QStringLiteral("incident simulation did not complete"));
				return;
			}
			QTimer::singleShot(100, this, &MainWindow::runCreatorAcceptanceE2E);
			return;
		}
		m_creatorAcceptancePolls = 0;
		bool directEvidence = false;
		for (const auto& train : m_completedRunResults.trains)
			if (!train.directIncidentIds.empty())
				directEvidence = true;
		if (m_completedRunProvenance.appliedScenario != "incident" || !directEvidence) {
			fail(QStringLiteral("final incident run lacked direct incident evidence"));
			return;
		}
		marker("E2E_CREATOR_INCIDENT_RUN_OK");
		next();
		return;
	}

	if (m_creatorAcceptancePhase == 14) {
		const QString exportDir = emitPath(qEnvironmentVariable("QEGTRAIN_E2E_CREATOR_EXPORT_DIR"));
		if (!m_resultsAvailable || exportDir.isEmpty() || !m_runResultsDock) {
			fail(QStringLiteral("public result controls or export target are unavailable"));
			return;
		}
		QDir().mkpath(exportDir);
		auto path = [&](const char* name) { return QDir(exportDir).filePath(QString::fromLatin1(name)); };
		auto exportButton = [&](QWidget* window, const QString& text, const QString& target) {
			if (!window)
				return false;
			QPushButton* button = nullptr;
			for (QPushButton* candidate : window->findChildren<QPushButton*>())
				if (candidate->text() == text) {
					button = candidate;
					break;
				}
			if (!button)
				return false;
			acceptFileDialog(target, false);
			button->click();
			process();
			return true;
		};
		auto findDiagram = [&](const QString& titlePart) -> DiagramWindow* {
			for (QWidget* widget : QApplication::topLevelWidgets())
				if (auto* diagram = qobject_cast<DiagramWindow*>(widget))
					if (diagram->windowTitle().contains(titlePart))
						return diagram;
			return nullptr;
		};
		auto findResultButton = [&](const QString& text) -> QPushButton* {
			for (QPushButton* button : findChildren<QPushButton*>())
				if (button->text() == text)
					return button;
			return nullptr;
		};

		if (!m_creatorBaselineDiagram
				|| !exportButton(m_creatorBaselineDiagram, QStringLiteral("Export CSV..."),
					path("baseline_time_distance.csv"))) {
			fail(QStringLiteral("baseline diagram CSV export was not driven through its public button"));
			return;
		}
		QPushButton* speedButton = findResultButton(QStringLiteral("Speed / distance"));
		if (!speedButton) {
			fail(QStringLiteral("speed/distance result control is unavailable"));
			return;
		}
		speedButton->click();
		process();
		DiagramWindow* speedDiagram = findDiagram(QStringLiteral("Speed vs Distance"));
		if (!speedDiagram
				|| !exportButton(speedDiagram, QStringLiteral("Export CSV..."), path("trajectory.csv"))
				|| !exportButton(speedDiagram, QStringLiteral("Export PNG..."), path("trajectory.png"))) {
			fail(QStringLiteral("current trajectory/speed exports were not driven through the diagram"));
			return;
		}
		QPushButton* tractiveButton = findResultButton(QStringLiteral("Tractive effort / distance"));
		if (!tractiveButton) {
			fail(QStringLiteral("tractive-effort result control is unavailable"));
			return;
		}
		tractiveButton->click();
		process();
		DiagramWindow* tractiveDiagram = findDiagram(QStringLiteral("Simulated Tractive Effort vs Distance"));
		if (!tractiveDiagram
				|| !exportButton(tractiveDiagram, QStringLiteral("Export CSV..."), path("tractive_effort.csv"))
				|| !exportButton(tractiveDiagram, QStringLiteral("Export PNG..."), path("tractive_effort.png"))) {
			fail(QStringLiteral("simulated tractive-effort exports were not driven through the diagram"));
			return;
		}
		QPushButton* timetableButton = findResultButton(QStringLiteral("Timetable"));
		if (!timetableButton) {
			fail(QStringLiteral("timetable result control is unavailable"));
			return;
		}
		timetableButton->click();
		process();
		TimetableTableWindow* timetable = nullptr;
		for (QWidget* widget : QApplication::topLevelWidgets())
			if (auto* candidate = qobject_cast<TimetableTableWindow*>(widget))
				timetable = candidate;
		if (!timetable
				|| !exportButton(timetable, QStringLiteral("Export CSV..."), path("timetable.csv"))
				|| !exportButton(timetable, QStringLiteral("Export PNG..."), path("timetable.png"))) {
			fail(QStringLiteral("timetable exports were not driven through the public table"));
			return;
		}
		QPushButton* blockingButton = findResultButton(QStringLiteral("Blocking time"));
		if (!blockingButton) {
			fail(QStringLiteral("blocking-time result control is unavailable"));
			return;
		}
		acceptMessageBox();
		blockingButton->click();
		process();
		DiagramWindow* blocking = findDiagram(QStringLiteral("Blocking time:"));
		if (!blocking
				|| !exportButton(blocking, QStringLiteral("Export CSV..."), path("blocking_time.csv"))
				|| !exportButton(blocking, QStringLiteral("Export PNG..."), path("blocking_time.png"))) {
			fail(QStringLiteral("blocking-time exports were not driven through the public diagram"));
			return;
		}
		QPushButton* summaryCsv = findChild<QPushButton*>("resultView_ExportCSV");
		QPushButton* summaryPng = findChild<QPushButton*>("resultView_ExportPNG");
		if (!summaryCsv || !summaryPng) {
			fail(QStringLiteral("run-summary result controls are unavailable"));
			return;
		}
		acceptFileDialog(path("run_summary.csv"), false);
		summaryCsv->click();
		process();
		acceptFileDialog(path("run_summary.png"), false);
		summaryPng->click();
		process();

		QPushButton* capacityButton = findResultButton(QStringLiteral("Capacity"));
		if (!capacityButton) {
			fail(QStringLiteral("capacity result control is unavailable"));
			return;
		}
		acceptCapacityScope();
		capacityButton->click();
		process();
		QDialog* capacity = nullptr;
		for (QWidget* widget : QApplication::topLevelWidgets())
			if (auto* dialog = qobject_cast<QDialog*>(widget))
				if (!dialog->isModal() && dialog->windowTitle().contains(QStringLiteral("Capacity analysis")))
					capacity = dialog;
		if (!capacity
				|| !exportButton(capacity, QStringLiteral("Export capacity CSV..."), path("capacity_analysis.csv"))) {
			fail(QStringLiteral("capacity analysis did not open and export through public controls"));
			return;
		}
		QPushButton* compressedButton = nullptr;
		for (QPushButton* candidate : capacity->findChildren<QPushButton*>())
			if (candidate->text().contains(QStringLiteral("compressed blocking-time")))
				compressedButton = candidate;
		if (!compressedButton) {
			fail(QStringLiteral("compressed blocking-time control is unavailable"));
			return;
		}
		compressedButton->click();
		process();
		DiagramWindow* compressed = findDiagram(QStringLiteral("Compressed blocking-time diagram"));
		if (!compressed
				|| !exportButton(compressed, QStringLiteral("Export CSV..."), path("capacity_compressed_blocking_time.csv"))
				|| !exportButton(compressed, QStringLiteral("Export PNG..."), path("capacity_compressed_blocking_time.png"))) {
			fail(QStringLiteral("compressed capacity diagram did not export"));
			return;
		}
		capacity->close();
		QPushButton* compareButton = findChild<QPushButton*>("compareDelayButton");
		if (!compareButton) {
			fail(QStringLiteral("delay comparison result control is unavailable"));
			return;
		}
		acceptFileDialog(path("delay_comparison.csv"), false);
		QTimer::singleShot(75, this, [this]() {
			for (QWidget* widget : QApplication::topLevelWidgets()) {
				auto* dialog = qobject_cast<QDialog*>(widget);
				if (!dialog || dialog->windowTitle() != QStringLiteral("Incident delay comparison"))
					continue;
				for (QPushButton* button : dialog->findChildren<QPushButton*>())
					if (button->text() == QStringLiteral("Export CSV...")) {
						button->click();
						dialog->accept();
						return;
					}
			}
		});
		compareButton->click();
		process();
		for (QWidget* widget : QApplication::topLevelWidgets())
			if (auto* dialog = qobject_cast<QDialog*>(widget))
				if (dialog->windowTitle() == QStringLiteral("Incident delay comparison"))
					dialog->close();
		marker("E2E_CREATOR_EXPORTS_OK");
		next();
		return;
	}

	if (m_creatorAcceptancePhase == 15) {
		if (!m_caseSettingsDock || !m_caseDescriptionEdit || !m_runResultsDock) {
			fail(QStringLiteral("stale-result invalidation controls are unavailable"));
			return;
		}
		m_caseSettingsDock->show();
		m_caseSettingsDock->raise();
		if (!editLine(m_caseDescriptionEdit,
				QStringLiteral("creator acceptance edited after incident exports"))) {
			fail(QStringLiteral("description editing did not commit through editingFinished"));
			return;
		}
		process();
		bool resultButtonsDisabled = true;
		for (QPushButton* button : findChildren<QPushButton*>())
			if (button->objectName().startsWith(QStringLiteral("resultView_")))
				resultButtonsDisabled = resultButtonsDisabled && !button->isEnabled();
		if (m_resultsAvailable || m_runResultsDock->isVisible() || !resultButtonsDisabled) {
			fail(QStringLiteral("editing case description did not invalidate stale result views"));
			return;
		}
		m_creatorAcceptanceFinished = true;
		marker("E2E_CREATOR_ACCEPTANCE_OK");
		QCoreApplication::exit(0);
		return;
	}
}

void MainWindow::clearSimulationWorker(bool requestStop) {
	if (m_worker && requestStop)
		m_worker->requestStop();
	if (m_workerThread && m_workerThread->isRunning()) {
		m_workerThread->quit();
		m_workerThread->wait();
	}
	m_worker = nullptr;
	m_workerThread = nullptr;
	// Pause and Stop only mean something while a worker exists.
	if (ui->actionSimulationPause)
		ui->actionSimulationPause->setEnabled(false);
	if (ui->actionSimulationStop)
		ui->actionSimulationStop->setEnabled(false);
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
				StationArray[i].stationName, 0.75);
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

		paintStationOverlay(pt,
			classifyStation(),
			StationArray[i].stationName);
	}

	// draw connections
	QPointF station1, station2;
	int st_index;
	QPointF ptc1, ptc2;
	for (int c = 0; c < numConnections; c++) {
		if (hasSharedPreview && (!cachedTrackLine(connections[c].idFirstTrackLine)
				|| !cachedTrackLine(connections[c].idSecondTrackLine)))
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
				paintStationNode(pt, station_node_size, line_width, track, &blockSets[track].member[i - 1].endNode);
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
					paintStationNode(pte, station_node_size, line_width, track, &blockSets[track].member[i].endNode);
					if (initial_variables.PAX_GUI) {
						paintStationPlatform(pte, station_node_size, line_width, &blockSets[track].member[i].endNode);
					}
				}
				else {
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
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_CREATOR_ACCEPTANCE")) {
		QTimer::singleShot(0, this, &MainWindow::runCreatorAcceptanceE2E);
		return;
	}
	// Keep update consent behind the startup chooser. Scripted launches have no
	// chooser, so the same queued callback runs after the initial window shows.
	QTimer::singleShot(0, this, [this]() {
		if (!initial_variables.nArgProvided)
			showStartupChooser();
		maybePromptForUpdateChecks();
	});

	// verification hook: auto-start the simulation when QEGTRAIN_AUTOSTART is set
	if (qEnvironmentVariableIsSet("QEGTRAIN_AUTOSTART"))
		QTimer::singleShot(1500, this, &MainWindow::runCurrent);
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_VISUAL_POLISH"))
		QTimer::singleShot(2600, this, &MainWindow::runVisualPolishE2E);
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_STATION_OVERLAYS"))
		QTimer::singleShot(1000, this, &MainWindow::runStationOverlayE2E);
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_EDITOR_SMOKE"))
		// let the startup case load and the docks settle before the smoke runs
		QTimer::singleShot(1000, this, &MainWindow::runEditorSmokeE2E);
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_SCENE_RUN"))
		QTimer::singleShot(1000, this, &MainWindow::runSceneRenderE2E);
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_TRACK_PREVIEW"))
		QTimer::singleShot(1000, this, &MainWindow::runTrackPreviewE2E);
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_LEGACY_IMPORT"))
		QTimer::singleShot(1000, this, &MainWindow::runLegacyImportE2E);
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
		for (QPushButton* button : m_runResultsDock->findChildren<QPushButton*>()) {
			if (button->objectName().startsWith("resultView_"))
				button->setEnabled(available);
		}
	}
	const DelayRunSnapshot current = completedDelaySnapshot();
	const bool canSetBaseline = available && !current.scenarioId.empty()
		&& !current.hasIncidents && !current.hasEntranceDelays;
	if (m_setDelayBaselineButton)
		m_setDelayBaselineButton->setEnabled(canSetBaseline);
	if (m_compareDelayButton)
		m_compareDelayButton->setEnabled(available && m_delayBaseline.has_value()
			&& current.hasIncidents && !current.hasEntranceDelays);
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
	QDialog dialog(this);
	dialog.setWindowTitle("Open a Case");
	dialog.resize(560, 460);
	QVBoxLayout* layout = new QVBoxLayout(&dialog);

	QLabel* heading = new QLabel("Choose a case study to open:", &dialog);
	layout->addWidget(heading);

	QListWidget* list = new QListWidget(&dialog);
	QSet<QString> seen;
	const auto addSceneItem = [&](const QString& path, const QString& badge) {
		const QString canonical = QFileInfo(path).canonicalFilePath();
		if (canonical.isEmpty() || seen.contains(canonical))
			return;
		seen.insert(canonical);
		auto* item = new QListWidgetItem(QString("%1%2").arg(QFileInfo(path).fileName(), badge), list);
		item->setData(Qt::UserRole, path);
		item->setToolTip(path);
	};
	// bundled scenes travel next to the binary or in the working directory
	const QStringList sceneRoots = {
		QDir::currentPath() + "/Scenes",
		QCoreApplication::applicationDirPath() + "/../Resources/Scenes"};
	QSet<QString> bundledNames;
	for (const QString& root : sceneRoots) {
		QDir dir(root);
		if (!dir.exists())
			continue;
		const auto entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
		for (const QFileInfo& entry : entries) {
			if ((entry.isDir() && QFileInfo(QDir(entry.absoluteFilePath()).filePath("scene.json")).exists())
				|| (entry.isFile() && entry.suffix().compare("egscene", Qt::CaseInsensitive) == 0)) {
				if (bundledNames.contains(entry.fileName().toCaseFolded())) {
					seen.insert(entry.canonicalFilePath());
					continue;
				}
				bundledNames.insert(entry.fileName().toCaseFolded());
				addSceneItem(entry.absoluteFilePath(), QString());
			}
		}
	}
	QSettings settings;
	const QStringList recent = settings.value(kRecentScenesKey).toStringList();
	for (const QString& path : recent) {
		const QFileInfo info(path);
		if ((info.isDir() && QFileInfo(QDir(path).filePath("scene.json")).exists())
			|| (info.isFile() && info.suffix().compare("egscene", Qt::CaseInsensitive) == 0))
			addSceneItem(path, "  (recent)");
	}
	layout->addWidget(list, 1);

	QLabel* legacyHint = new QLabel(
		"A legacy case is a folder with the original text inputs. Importing one asks "
		"for that folder first and then for a folder to save the converted scene.", &dialog);
	legacyHint->setWordWrap(true);
	legacyHint->setStyleSheet("color: gray;");
	layout->addWidget(legacyHint);

	QHBoxLayout* buttons = new QHBoxLayout();
	QPushButton* newCaseBtn = new QPushButton("New Case Study...", &dialog);
	QPushButton* legacyBtn = new QPushButton("Import Legacy Case...", &dialog);
	QPushButton* browseBtn = new QPushButton("Open Scene Folder...", &dialog);
	QPushButton* skipBtn = new QPushButton("Skip", &dialog);
	QPushButton* openBtn = new QPushButton("Open", &dialog);
	openBtn->setDefault(true);
	openBtn->setEnabled(false);
	buttons->addWidget(newCaseBtn);
	buttons->addWidget(legacyBtn);
	buttons->addWidget(browseBtn);
	buttons->addStretch();
	buttons->addWidget(skipBtn);
	buttons->addWidget(openBtn);
	layout->addLayout(buttons);

	enum { Skipped, OpenSelected, BrowseScene, ImportLegacy, NewCase };
	int choice = Skipped;
	connect(list, &QListWidget::itemSelectionChanged, &dialog, [&]() {
		openBtn->setEnabled(list->currentItem() != nullptr);
	});
	connect(list, &QListWidget::itemDoubleClicked, &dialog, [&](QListWidgetItem*) {
		choice = OpenSelected;
		dialog.accept();
	});
	connect(openBtn, &QPushButton::clicked, &dialog, [&]() { choice = OpenSelected; dialog.accept(); });
	connect(browseBtn, &QPushButton::clicked, &dialog, [&]() { choice = BrowseScene; dialog.accept(); });
	connect(legacyBtn, &QPushButton::clicked, &dialog, [&]() { choice = ImportLegacy; dialog.accept(); });
	connect(newCaseBtn, &QPushButton::clicked, &dialog, [&]() { choice = NewCase; dialog.accept(); });
	connect(skipBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

	if (list->count() > 0)
		list->setCurrentRow(0);
	dialog.exec();

	switch (choice) {
		case OpenSelected:
			if (QListWidgetItem* item = list->currentItem())
				openSceneDirectory(item->data(Qt::UserRole).toString());
			break;
		case BrowseScene:
			openSceneFolderDialog();
			break;
		case ImportLegacy:
			actionLoad_Network();
			break;
		case NewCase:
			newScene();
			break;
		default:
		statusBar()->showMessage("No case chosen; use File > Open Case Study when ready", 8000);
			break;
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
			? QString::number(incident.occurrence) : QStringLiteral("all");
		const QString window = incident.hasEndSeconds || incident.endSeconds != 0.0
			? QString::number(incident.endSeconds, 'g', 12) : QStringLiteral("until-destination");
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
	QString summary = QString("Case study: %1\nScenario: %2\nServices: %3\nOccurrences: %4/%5 selected\nCompositions: %6\nIncidents: %7\nValidation: %8 error(s), %9 warning(s)")
		.arg(QString::fromStdString(m_sceneModel.name))
		.arg(scenarioContext())
		.arg(static_cast<int>(m_sceneModel.services.size()))
		.arg(selectedServiceOccurrences())
		.arg(totalServiceOccurrences())
		.arg(static_cast<int>(m_sceneModel.compositions.size()))
		.arg(static_cast<int>(selectedScenarioIncidents().size()))
		.arg(counts.errors)
		.arg(counts.warnings);
	if (!incidentDetails.isEmpty())
		summary += "\nIncident configuration: " + incidentDetails;
	if (!entranceDelayDetails.isEmpty())
		summary += "\nEntrance delay configuration: " + entranceDelayDetails;
	return summary;
}

bool MainWindow::showRunReview() {
	if (e2eDialogsSuppressed())
		return true;

	QDialog review(this);
	review.setObjectName("runReviewDialog");
	review.setWindowTitle("Run simulation");
	review.setMinimumWidth(520);
	auto* layout = new QVBoxLayout(&review);
	layout->setContentsMargins(24, 22, 24, 20);
	layout->setSpacing(14);

	auto* heading = new QLabel("Run simulation", &review);
	heading->setObjectName("runReviewHeading");
	layout->addWidget(heading);
	auto* context = new QLabel(QString("%1  /  %2")
		.arg(QString::fromStdString(m_sceneModel.name), scenarioContext()), &review);
	context->setObjectName("runReviewContext");
	layout->addWidget(context);

	const SceneDiagnosticCounts counts = countDiagnostics(m_sceneDiagnostics);
	auto* facts = new QGridLayout();
	facts->setHorizontalSpacing(24);
	facts->setVerticalSpacing(8);
	const auto addFact = [&](int row, const QString& label, const QString& value) {
		auto* name = new QLabel(label, &review);
		name->setObjectName("runReviewFactLabel");
		auto* fact = new QLabel(value, &review);
		fact->setObjectName("runReviewFactValue");
		facts->addWidget(name, row, 0);
		facts->addWidget(fact, row, 1);
	};
	addFact(0, "Services", QString::number(static_cast<int>(m_sceneModel.services.size())));
	addFact(1, "Occurrences", QString("%1 of %2 selected")
		.arg(selectedServiceOccurrences()).arg(totalServiceOccurrences()));
	addFact(2, "Compositions", QString::number(static_cast<int>(m_sceneModel.compositions.size())));
	addFact(3, "Incidents", QString::number(static_cast<int>(selectedScenarioIncidents().size())));
	addFact(4, "Validation", QString("%1 errors, %2 warnings").arg(counts.errors).arg(counts.warnings));
	layout->addLayout(facts);

	auto* status = new QLabel(counts.warnings == 0
		? QStringLiteral("Ready to run. No validation issues were found.")
		: QString("Ready to run. Review %1 validation %2 if needed.")
			.arg(counts.warnings).arg(counts.warnings == 1 ? "warning" : "warnings"), &review);
	status->setObjectName("runReviewStatus");
	status->setProperty("warning", counts.warnings > 0);
	status->setWordWrap(true);
	layout->addWidget(status);

	const QString summary = runReviewText();
	const int detailsStart = summary.indexOf("\nIncident configuration:");
	const int delaysStart = summary.indexOf("\nEntrance delay configuration:");
	const int firstDetail = detailsStart < 0 ? delaysStart
		: delaysStart < 0 ? detailsStart : std::min(detailsStart, delaysStart);
	if (firstDetail >= 0) {
		auto* detailsToggle = new QToolButton(&review);
		detailsToggle->setText("Configuration details");
		detailsToggle->setCheckable(true);
		detailsToggle->setArrowType(Qt::RightArrow);
		detailsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
		auto* details = new QLabel(summary.mid(firstDetail + 1), &review);
		details->setObjectName("runReviewDetails");
		details->setWordWrap(true);
		details->setTextInteractionFlags(Qt::TextSelectableByMouse);
		details->hide();
		connect(detailsToggle, &QToolButton::toggled, &review, [detailsToggle, details](bool shown) {
			detailsToggle->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
			details->setVisible(shown);
		});
		layout->addWidget(detailsToggle);
		layout->addWidget(details);
	}

	auto* buttons = new QHBoxLayout();
	auto* loadedButton = new QPushButton("Loaded Data", &review);
	auto* validationButton = new QPushButton("Validation", &review);
	auto* cancelButton = new QPushButton("Cancel", &review);
	auto* runButton = new QPushButton("Run simulation", &review);
	runButton->setObjectName("runReviewRunButton");
	runButton->setDefault(true);
	buttons->addWidget(loadedButton);
	buttons->addWidget(validationButton);
	buttons->addStretch();
	buttons->addWidget(cancelButton);
	buttons->addWidget(runButton);
	layout->addLayout(buttons);

	enum ReviewChoice { CancelRun, StartRun, ShowLoadedData, ShowValidation };
	ReviewChoice choice = CancelRun;
	connect(runButton, &QPushButton::clicked, &review, [&]() { choice = StartRun; review.accept(); });
	connect(loadedButton, &QPushButton::clicked, &review, [&]() { choice = ShowLoadedData; review.accept(); });
	connect(validationButton, &QPushButton::clicked, &review, [&]() { choice = ShowValidation; review.accept(); });
	connect(cancelButton, &QPushButton::clicked, &review, &QDialog::reject);
	review.exec();
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
		? initial_variables.times : 0.0;
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
		QMessageBox::warning(this, "Delay baseline unavailable", "Complete an incident-free run first.");
		return;
	}
	if (snapshot.hasIncidents || snapshot.hasEntranceDelays) {
		QMessageBox::warning(this, "Delay baseline rejected",
			"The delay baseline must have no incidents and no entrance delays.");
		return;
	}
	m_delayBaseline = snapshot;
	statusBar()->showMessage(QString("Delay baseline set to scenario %1").arg(QString::fromStdString(snapshot.scenarioId)), 5000);
	refreshRunResults();
	updateDiagramActions();
}

void MainWindow::showDelayComparison() {
	if (!m_delayBaseline) {
		QMessageBox::warning(this, "Delay comparison unavailable", "Set an incident-free delay baseline first.");
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
	dialog.resize(1180, 520);
	QVBoxLayout* layout = new QVBoxLayout(&dialog);
	QLabel* context = new QLabel(QString("Baseline: %1 | Scenario: %2 | Total positive arrival delay: %3 s")
		.arg(completedRunContext(m_delayBaseline->provenance), completedRunContext(scenario.provenance))
		.arg(comparison.totalArrivalDelay.available ? QString::number(comparison.totalArrivalDelay.value, 'g', 12) : QStringLiteral("-")), &dialog);
	context->setWordWrap(true);
	layout->addWidget(context);
	QTableWidget* table = new QTableWidget(&dialog);
	table->setObjectName("delayComparisonTable");
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setColumnCount(13);
	table->setHorizontalHeaderLabels({
		"Service", "Occurrence", "Operating code", "Baseline final arrival", "Scenario final arrival",
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
		table->setItem(index, 12, new QTableWidgetItem(QString("%1 / %2")
			.arg(QString::fromStdString(m_delayBaseline->scenarioId), QString::fromStdString(scenario.scenarioId))));
	}
	table->resizeColumnsToContents();
	layout->addWidget(table, 1);
	QHBoxLayout* actions = new QHBoxLayout();
	QPushButton* exportButton = new QPushButton("Export CSV...", &dialog);
	exportButton->setObjectName("delayComparisonExportCsvButton");
	const RunProvenance baselineProvenance = m_delayBaseline->provenance;
	const RunProvenance scenarioProvenance = scenario.provenance;
	connect(exportButton, &QPushButton::clicked, &dialog, [this, scenario, comparison,
			baselineProvenance, scenarioProvenance]() {
		saveCsvInteractive(this, "delay_comparison.csv", delayComparisonCsv(*m_delayBaseline, scenario, comparison),
			[baselineProvenance, scenarioProvenance](const QString& path, const std::string& bytes) {
				return writeDelayArtifactWithProvenance(path.toStdString(), "csv", bytes,
					baselineProvenance, scenarioProvenance);
			});
	});
	actions->addWidget(exportButton);
	actions->addStretch();
	QDialogButtonBox* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	connect(closeButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	actions->addWidget(closeButtons);
	layout->addLayout(actions);
	dialog.exec();
}

// starts EGTRAIN simulation on a worker thread
void MainWindow::startSimulation() {
	if (m_worker)
		return; // already running

	// show progress bar
	progressBar->show();
	statusBar()->showMessage("Simulation running...");

	// create worker and thread; sync slider before run() starts
	m_worker = new SimulationWorker();
	refreshInfrastructurePanel();
	m_worker->setDelayMs(stepDelayForSlider(m_speedSlider->value()));
	refreshIncidentPanel();
	updateSceneActions();
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
	ui->actionSimulationPause->setEnabled(true);
	ui->actionSimulationStop->setEnabled(true);
}

// handle simulation completion on the main thread
void MainWindow::onSimulationFinished() {
	const bool sceneChangedDuringRun = m_sceneChangedDuringRun;
	m_sceneChangedDuringRun = false;
	m_resultsAvailable = !sceneChangedDuringRun && hasRawRunResults();
	m_runtimeStatus = m_resultsAvailable ? QStringLiteral("Completed") : QStringLiteral("Failed");
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
			m_runResultsSummaryLabel->setText(QString("No results | Case: %1 | Scenario: %2 | Status: Failed%3")
				.arg(QString::fromStdString(m_sceneModel.name), scenarioContext(),
					sceneChangedDuringRun ? QStringLiteral(" (scene changed during run)") : QString()));
		if (m_runResultsDock) {
			m_runResultsDock->setWindowTitle(QString("Run Results — %1 (failed)").arg(scenarioContext()));
			m_runResultsDock->hide();
		}
	}
	m_pendingRunProvenance = RunProvenance();
	refreshLoadedDataTree();

	// verification hook: write every CSV export from the completed run, then exit
	if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_EXPORT_DIR")) {
		const QString dir = qEnvironmentVariable("QEGTRAIN_E2E_EXPORT_DIR");
		const QStringList ids = allTrainIds();
		// Skip an export with no rows, matching the interactive "nothing to export".
		const auto dump = [&dir, this](const QString& file, const std::string& content) {
			if (content.empty())
				return true;
			const QString path = dir + "/" + file;
			if (writeCsvFileWithProvenance(path, content, m_completedRunProvenance))
				return true;
			std::fprintf(stderr, "E2E_PROVENANCE_EXPORT_WARNING: %s\n", path.toStdString().c_str());
			return false;
		};
		bool ok = m_resultsAvailable;
		if (ok) {
			ok &= dump("trajectory.csv", buildTrajectoryCsv(ids));
			ok &= dump("timetable.csv", buildTimetableCsv(ids));
			ok &= dump("blocking_time.csv", buildBlockingTimeCsv(ids));
			ok &= dump("run_summary.csv", buildRunSummaryCsv(m_completedRunResults));
			const CapacityAnalysisScope capacityScope = firstCapacityScopeWithPair();
			std::string capacityCsv;
			if (capacityScope.routeIndex >= 0) {
				const auto capacityTrains = capacityTrainsForScope(capacityScope);
				capacityCsv = buildCapacityAnalysisCsv(
					analyzeCapacity(capacityTrains, capacityScope.periodSeconds,
						capacityScope.cycleEndOccurrenceId), capacityScopeLabel(capacityScope));
			}
			if (capacityCsv.empty()) {
				std::fprintf(stdout, "E2E_CAPACITY_EXPORT_UNAVAILABLE\n");
			} else {
				ok &= dump("capacity_analysis.csv", capacityCsv);
			}
		}
		std::fprintf(ok ? stdout : stderr, ok ? "E2E_CSV_EXPORT_OK\n" : "E2E_CSV_EXPORT_FAIL\n");
		std::fflush(stdout);
		std::fflush(stderr);
		clearSimulationWorker(false);
		refreshInfrastructurePanel();
		QCoreApplication::exit(ok ? 0 : 2);
		return;
	}

	// print last services
	simulation.printLastTrainServicePathDiagram();

	// hide progress bar
	progressBar->hide();
	statusBar()->showMessage(sceneChangedDuringRun
		? QStringLiteral("Simulation finished; results discarded because the scene changed during the run")
		: QStringLiteral("Simulation complete - open the Diagrams menu for results"));
	ui->actionSimulationPause->setText("Pause");
	ui->actionSimulationPause->setChecked(false);

	// The Run Results dock raised by refreshRunResults is the completion notice;
	// diagram entries switch on here instead of a modal prompt chain.
	// cleanup thread
	clearSimulationWorker(false);
	refreshInfrastructurePanel();
	refreshIncidentPanel();
	updateSceneActions();
}

void MainWindow::teardownGUI() {
	// Stop any running simulation before clearing scene objects it may reference.
	clearSimulationWorker(true);

	stopTrainAnimations();

	// Clearing the scene deletes all owned QGraphicsItems.
	scene->clear();

	// Clear list pointers - the items were owned by the scene and are now deleted.
	m_trainBadges.clear(); // items deleted by scene->clear() above
	m_prevTrainPositions.clear();
	allTrains.clear();
	allSignals.clear();
	m_signalDecorations.clear();
	m_stationOverlays.clear();
	m_selectedStationName.clear();
	m_vcMessageItems.clear();
	m_stationDecorations.clear();
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
	m_followTrainIndex = -1;
	m_selectedTrainIndex = -1;
	m_e2eAttempts = 0;
	m_e2eFinished = false;
	if (m_followAction)
		m_followAction->setChecked(false);
	if (m_followTrainCombo) {
		m_followTrainCombo->clear();
		m_followTrainCombo->addItem("No trains to follow", -1);
	}

	m_snapshot.reset();
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
	updateCaseLayersPanel();
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
	pruneExcludedServiceOccurrences();
	m_lastRunTotalOccurrences = totalServiceOccurrences();
	m_lastRunSelectedOccurrences = selectedServiceOccurrences();
	if (m_lastRunSelectedOccurrences <= 0) {
		showBlockingError(this, "Cannot Run Scene", "Select at least one generated service occurrence before running.", true);
		return;
	}
	if (!showRunReview())
		return;

	statusBar()->showMessage("Preparing scene simulation...");
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
	const std::vector<SceneDiagnostic> diagnostics = simulation.prepareScene(m_sceneModel, m_selectedScenarioId, selection);
	m_runtimeDiagnostics = diagnostics;
	if (hasErrors(diagnostics)) {
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

	// scene stays loaded; only the case-study load path clears it
	updateSceneWindowTitle();
	updateCaseLayersPanel();
	updateSceneActions();

	m_runtimeStatus = QStringLiteral("Running");
	refreshLoadedDataTree();
	startSimulation();
	statusBar()->showMessage(QString("Running scene: %1").arg(QString::fromStdString(m_sceneModel.name)));
}

void MainWindow::actionLoad_Network() {
	if (!maybeSaveScene())
		return;
	const bool e2e = qEnvironmentVariableIsSet("QEGTRAIN_E2E_LEGACY_IMPORT");
	QString sourceDir;
	QString destinationDir;
	if (e2e) {
		sourceDir = qEnvironmentVariable("QEGTRAIN_E2E_LEGACY_SOURCE");
		destinationDir = qEnvironmentVariable("QEGTRAIN_E2E_LEGACY_DEST");
	} else {
		sourceDir = QFileDialog::getExistingDirectory(this, "Select Legacy Case Folder", QDir::homePath());
		if (sourceDir.isEmpty())
			return;
		destinationDir = QFileDialog::getExistingDirectory(this, "Select Scene Destination", QDir::homePath());
	}
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
		? QString("Imported Legacy Scene") : QFileInfo(sourcePath).fileName();
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
	openSceneDirectory(destinationPath);
}

// draws a Node
void MainWindow::paintNode(QPointF coord, int size, int pen_width, int track, Node* Node) {
	QPen pen = QPen(Qt::lightGray);
	pen.setWidth(0);
	pen.setCosmetic(true);

	// center the station marker on its network coordinate
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
	StationVisual visual = classifyStation();
	QPen pen = QPen(visual.outline);
	pen.setWidth(0);
	pen.setCosmetic(true);

	// draws using rectangle with center on top-left corner (center_x,center_y,width,height)
	QRectF rect = QRectF(0, 0, size, size);
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

// Draw the fixed-size station symbol and label as one scene-owned item.
void MainWindow::paintStationOverlay(QPointF coord, const StationVisual& visual, const string& sname,
		qreal scale) {
	if (!scene)
		return;
	auto* overlay = new StationOverlayItem(QString::fromStdString(sname), coord, visual);
	overlay->setScale(scale);
	scene->addItem(overlay);
	m_stationOverlays.push_back(overlay);
	m_stationDecorations.push_back(overlay);
	overlay->setNameVisible(m_stationNamesVisible);
	overlay->setVisible(m_stationLayerVisible);
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
	textItem->setPos(QPointF(platformItem->sceneBoundingRect().center().x() - textItem->boundingRect().width() / 2, platformItem->sceneBoundingRect().top() - textItem->boundingRect().height()));
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
	ss << "Current onboard pax: " << trainItem->currentOnboardPassengers
		<< "\nMax onboard pax: " << trainItem->maxOnboardPassengers;

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


// draws an Arc
void MainWindow::paintArc(QPointF start, QPointF end, int pen_width, int track, Arc* Arc, int track_separation) {
	arcDrawing(start, end, pen_width, track, Arc);
}

// Arc drawing
void MainWindow::arcDrawing(QPointF start, QPointF end, int pen_width, int track, Arc* Arc) {
	TrackVisual visual = freeTrackVisual();
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
	const TrackVisual visual = freeTrackVisual();
	QPen pen(visual.color);
	pen.setWidth(std::max(pen_width, visual.width));
	pen.setCosmetic(true);

	// draws a line from start to end with a given line width
	ConnectionItem* line = new ConnectionItem(QLineF(start.x(), start.y(), end.x(), end.y()));
	line->setPen(pen);

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
	double signalDeltaX = 0.0;
	double signalDeltaY = 0.0;
	if (previewLine) {
		QPointF normal;
		if (!previewSignalNormal(*previewLine, X, normal))
			return;
		signalDeltaX = normal.x();
		signalDeltaY = normal.y();
	} else {
		int stationIdx[2];
		neighbourStations(X, track, stationIdx);
		const int index = stationIdx[1];
		signalDeltaX = StationArray[index].signalDeltaX[blockSets[track].region];
		signalDeltaY = StationArray[index].signalDeltaY[blockSets[track].region];
	}

	basisStartX -= signalDeltaX * 0.10 * track_separation;
	basisStartY -= signalDeltaY * 0.10 * track_separation;
	basisEndX = basisStartX - signalDeltaX * 0.20 * track_separation;
	basisEndY = basisStartY - signalDeltaY * 0.20 * track_separation;

	postStartX -= signalDeltaX * 0.20 * track_separation;
	postStartY -= signalDeltaY * 0.20 * track_separation;
	postEndX -= signalDeltaX * 0.20 * track_separation;
	postEndY -= signalDeltaY * 0.20 * track_separation;

	// post/basis
	QPen penPost = QPen(Qt::white);
	penPost.setWidth(pen_width);
	penPost.setCosmetic(true);

	// plate
	QPen penPlate = QPen();
	penPlate.setWidth(0);
	// draws using rectangle with center on top-left corner (center_x,center_y,width,height)
	const qreal markerSize = static_cast<qreal>(size) * 0.6;
	QRectF rect = QRectF(0, 0, markerSize, markerSize);
	rect.moveCenter(QPointF(0.0, 0.0));

	// post #1
	QGraphicsLineItem* post1 = new QGraphicsLineItem(QLineF(postStartX, postStartY, postEndX, postEndY));
	post1->setPen(penPost);

	// plate #1
	plateCenterX = postEndX;
	plateCenterY = postEndY;

	SignalItem* plate1 = new SignalItem(rect);
	plate1->setZValue(3);
	plate1->setPos(QPointF(plateCenterX, plateCenterY));
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

	// basis #1
	QGraphicsLineItem* basis1 = new QGraphicsLineItem(QLineF(basisStartX, basisStartY, basisEndX, basisEndY));
	basis1->setPen(penPost);

	// basis #2
	basisStartX += 2 * signalDeltaX * 0.10 * track_separation;
	basisStartY += 2 * signalDeltaY * 0.10 * track_separation;
	basisEndX = basisStartX + signalDeltaX * 0.20 * track_separation;
	basisEndY = basisStartY + signalDeltaY * 0.20 * track_separation;

	// post #2
	egtrainPoint2Screen(X + 0.008, track, track_separation, postEndX, postEndY);
	postStartX += 2 * signalDeltaX * 0.20 * track_separation;
	postStartY += 2 * signalDeltaY * 0.20 * track_separation;
	postEndX += signalDeltaX * 0.20 * track_separation;
	postEndY += signalDeltaY * 0.20 * track_separation;

	QGraphicsLineItem* post2 = new QGraphicsLineItem(QLineF(postStartX, postStartY, postEndX, postEndY));
	post2->setPen(penPost);

	// plate #2
	plateCenterX = postEndX;
	plateCenterY = postEndY;

	SignalItem* plate2 = new SignalItem(rect);
	plate2->setZValue(3);
	plate2->setPos(QPointF(plateCenterX, plateCenterY));
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

	QGraphicsLineItem* basis2 = new QGraphicsLineItem(QLineF(basisStartX, basisStartY, basisEndX, basisEndY));
	basis2->setPen(penPost);

	const bool detailedSignals = networkView && networkView->zoomRatio() >= kSignalDetailZoom;
	const auto addSignalDecoration = [this, track, detailedSignals](QGraphicsItem* item) {
		item->setData(kSignalDecorationRole, true);
		item->setData(kSignalTrackRole, track);
		item->setData(kSignalBaseVisibleRole, true);
		scene->addItem(item);
		m_signalDecorations.push_back(item);
		item->setVisible(m_signalLayerVisible
			&& (qgraphicsitem_cast<SignalItem*>(item) || detailedSignals));
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
	TrainVisual visual = classifyTrainType(train.type, train.description);
	QPen pen = QPen(visual.outline);
	pen.setWidthF(3);

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
	m_runResultsTable->setHorizontalHeaderLabels({
		"Train", "Operating code", "Performance (%)", "Maximum speed (km/h)", "Start", "End", "Travel time",
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
	const auto addResultViewButton = [this, resultViews](const QString& text, auto slot) {
		auto* button = new QPushButton(text, m_runResultsDock);
		button->setObjectName(QString("resultView_%1").arg(text).remove(' '));
		connect(button, &QPushButton::clicked, this, slot);
		resultViews->addWidget(button);
	};
	addResultViewButton("Timetable", &MainWindow::showTimetableTable);
	addResultViewButton("Timetable graph", &MainWindow::showTimetableGraph);
	addResultViewButton("Delays", &MainWindow::showDelayDiagram);
	addResultViewButton("Speed / distance", &MainWindow::showSpeedDistanceDiagram);
	addResultViewButton("Speed / time", &MainWindow::showSpeedTimeDiagram);
	addResultViewButton("Time / distance", &MainWindow::showTimeDistanceDiagram);
	addResultViewButton("Tractive effort / distance", &MainWindow::showTractiveEffortDistanceDiagram);
	addResultViewButton("Train paths", &MainWindow::displayTrainPathDiagrams);
	addResultViewButton("Blocking time", &MainWindow::showBlockingTimeDiagram);
	addResultViewButton("Capacity", &MainWindow::showCapacityAnalysis);
	containerLayout->addLayout(resultViews);
	QPushButton* exportCsvBtn = new QPushButton("Export CSV...", container);
	exportCsvBtn->setObjectName("resultView_ExportCSV");
	exportCsvBtn->setToolTip("Write travel time and energy per train to a CSV file");
	connect(exportCsvBtn, &QPushButton::clicked, this, [this]() {
		const RunProvenance provenance = m_completedRunProvenance;
		saveCsvInteractive(this, "run_summary.csv", buildRunSummaryCsv(m_completedRunResults),
			[provenance](const QString& path, const std::string& bytes) {
				return writeRunArtifactWithProvenance(path.toStdString(), "csv", bytes, provenance);
			});
	});
	QPushButton* exportPngBtn = new QPushButton("Export PNG...", container);
	exportPngBtn->setObjectName("resultView_ExportPNG");
	exportPngBtn->setToolTip("Save the table as an image");
	connect(exportPngBtn, &QPushButton::clicked, this, [this]() {
		QString path = QFileDialog::getSaveFileName(this, "Export Table", "run_summary.png", "PNG Image (*.png)");
		if (path.isEmpty())
			return;
		if (QFileInfo(path).suffix().compare("png", Qt::CaseInsensitive) != 0)
			path += ".png";
		QByteArray data;
		QBuffer buffer(&data);
		if (!buffer.open(QIODevice::WriteOnly) || !m_runResultsTable->grab().save(&buffer, "PNG")) {
			QMessageBox::warning(this, "Export failed", QString("Could not write the image to:\n%1").arg(path));
			return;
		}
		const std::string bytes(data.constData(), static_cast<std::size_t>(data.size()));
		if (!writeRunArtifactWithProvenance(path.toStdString(), "png", bytes, m_completedRunProvenance))
			QMessageBox::warning(this, "Export failed",
				QString("Could not export the image and provenance to:\n%1").arg(path));
		});
	QHBoxLayout* toolRow = new QHBoxLayout();
	toolRow->addWidget(exportCsvBtn);
	toolRow->addWidget(exportPngBtn);
	m_setDelayBaselineButton = new QPushButton("Set delay baseline", container);
	m_setDelayBaselineButton->setObjectName("setDelayBaselineButton");
	m_setDelayBaselineButton->setToolTip("Freeze this completed incident-free run as the delay baseline");
	connect(m_setDelayBaselineButton, &QPushButton::clicked, this, &MainWindow::setDelayBaseline);
	toolRow->addWidget(m_setDelayBaselineButton);
	m_compareDelayButton = new QPushButton("Compare delays", container);
	m_compareDelayButton->setObjectName("compareDelayButton");
	m_compareDelayButton->setToolTip("Compare the completed incident run with the frozen delay baseline");
	connect(m_compareDelayButton, &QPushButton::clicked, this, &MainWindow::showDelayComparison);
	toolRow->addWidget(m_compareDelayButton);
	toolRow->addStretch();
	containerLayout->addLayout(toolRow);
	containerLayout->addWidget(m_runResultsTable);
	m_runResultsDock->setWidget(container);
	addDockWidget(Qt::BottomDockWidgetArea, m_runResultsDock);
	m_runResultsDock->hide();
}

void MainWindow::refreshRunResults() {
	if (!m_runResultsDock || !m_runResultsTable || m_completedRunResults.trains.empty())
		return;
	int directEvidenceCount = 0;
	int destinationTerminationCount = 0;
	for (const TrainRunResult& result : m_completedRunResults.trains) {
		if (!result.directIncidentIds.empty())
			++directEvidenceCount;
		if (result.destinationTerminated)
			++destinationTerminationCount;
	}
	if (m_runResultsSummaryLabel) {
		QString summary = QString("Run: %1 | Occurrences: %2/%3 selected | Status: Completed | Direct incident evidence: %4 | Destination terminations: %5")
			.arg(completedRunContext(m_completedRunProvenance))
			.arg(m_lastRunSelectedOccurrences).arg(m_lastRunTotalOccurrences)
			.arg(directEvidenceCount).arg(destinationTerminationCount);
		if (m_delayBaseline)
			summary += QString(" | Delay baseline: %1").arg(completedRunContext(m_delayBaseline->provenance));
		m_runResultsSummaryLabel->setText(summary);
	}
	m_runResultsDock->setWindowTitle(QString("Run Results — %1").arg(completedRunContext(m_completedRunProvenance)));

	const RunResults& results = m_completedRunResults;
	const int totalColumns = 11;
	m_runResultsTable->clear();
	m_runResultsTable->setColumnCount(totalColumns);
	m_runResultsTable->setHorizontalHeaderLabels({
		"Train", "Operating code", "Performance (%)", "Maximum speed (km/h)", "Start time (s)", "End time (s)", "Travel time (s)",
		"Energy consumed (kWh)", "Energy consumed with regenerative braking (kWh)",
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
	m_runResultsDock->show();
	m_runResultsDock->raise();
	if (qEnvironmentVariableIsSet("QEGTRAIN_AUTOSTART")) {
		extern InitialParameters initial_variables;
		std::fprintf(stdout, "\nE2E_GUI_RUN_RESULTS rows=%d trains=%d dock_visible=%d output_dir=%s\n",
			m_runResultsTable->rowCount(), static_cast<int>(results.trains.size()),
			m_runResultsDock->isVisible() ? 1 : 0, initial_variables.OutputMainFolder.c_str());
		std::fflush(stdout);
	}
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
	arcFormLayout->addRow("Gradient", arcGradientText);
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
	signallingFormLayout = new QFormLayout();
	signallingFormLayout->addRow("Track ID", signallingTrackIDText);
	signallingFormLayout->addRow("X (m)", signallingXText);
	signallingFormLayout->addRow("ID of Block Section", signallingIDSectionAheadText);
	signallingFormLayout->addRow("Length of Block Section (m)", signallingLengthSectionAheadText);
	signallingFormLayout->addRow("Aspect", signallingAspectText);
	signallingFormLayout->addRow("Protected section", signallingProtectedSectionText);
	signallingFormLayout->addRow("Next track", signallingNextTrackText);
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
	for (auto* edit : infoWidget->findChildren<QLineEdit*>())
		edit->setReadOnly(true);

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
		tr("EGTRAIN %1\n\nMade at TU Delft").arg(QCoreApplication::applicationVersion()));
}

// hides all widgets from the dock widget
// removes highlight from last clicked item
void MainWindow::handleCloseInfoDockWidget() {
	m_selectedStationName.clear();
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
	removePaxInfoIcon();

	// remove highlight
	if (effect) {
		delete effect;
		effect = nullptr;
	}
	updateViewportOverlays();
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
	for (auto* overlay : m_stationOverlays)
		if (overlay)
			overlay->setSelected(overlay->stationName() == m_selectedStationName);
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
				return QStringLiteral("Prepared");
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

	// update signalling info displayed on widget
	signallingTrackIDText->setText(QString::fromStdString(to_string_precision(signal->trackID, 0)));
	signallingXText->setText(QString::fromStdString(to_string_precision(1000 * signal->X, 2))); // km to m
	const auto aspectName = [](int code) {
		switch (classifySignalAspect(code).cue) {
			case SignalCueKind::Stop:
				return QStringLiteral("Stop");
			case SignalCueKind::Caution:
				return QStringLiteral("Caution");
			case SignalCueKind::Proceed:
				return QStringLiteral("Proceed");
			case SignalCueKind::Neutral:
			default:
				return QStringLiteral("Neutral");
		}
	};
	signallingAspectText->setText(aspectName(signal->aspectCode()));
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

	// show pax info
	if (initial_variables.PAX_GUI) {
		paintTrainPassengerInfo(groupItem);
	}

	// show widget
	infoDockWidget->setWindowTitle("Train Info");
	infoDockWidget->show();
	trainInfoWidget->show();
	if (changeFollowMode) {
		setFollowTrain(trainItem->index);
		centerSceneItem(groupItem);
	}
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
	if (progressBar)
		progressBar->setProgress(timestep, totalTimesteps, m_startOffsetSeconds);
}

void MainWindow::waitForUpdates() {
	const auto snapshot = simulation.takeSimulationSnapshot();
	if (!snapshot)
		return;
	m_snapshot = snapshot;
	const int timestep = snapshot->timestep;
	static bool autostartProgressReported = false;
	if (!autostartProgressReported && qEnvironmentVariableIsSet("QEGTRAIN_AUTOSTART")) {
		// fprintf so the marker reaches the real stdout; std::cout is captured
		// by the console pane once the window exists
		std::fprintf(stdout, "E2E_GUI_AUTOSTART_RUNNING timestep=%d\n", timestep);
		std::fflush(stdout);
		autostartProgressReported = true;
	}
	updateTimeline(timestep, snapshot->totalTimesteps);

	qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (now - m_lastRenderMs >= 33 || timestep >= snapshot->totalTimesteps - 1) {
		updateSignalling();
		updateTrainPosition(timestep);

		// pax info
		if (initial_variables.PAX_GUI) {
			updatePlatforms(timestep);
			updatePaxIconInfo();
			updateTrainPaxInfo();
		}
		if (qEnvironmentVariableIsSet("QEGTRAIN_E2E_VISUAL_POLISH") && !m_e2eFinished && !allTrains.isEmpty())
			runVisualPolishE2E();

		m_lastRenderMs = now;
	}
}

// slot to update signal aspects
void MainWindow::updateSignalling() {
	if (!m_snapshot)
		return;
	for (const GuiSignalState& signal : m_snapshot->signalStates)
		updateSignalAspect(signal.sectionId, signal.code, signal.reversedDirection);
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
		for (auto* icon : platformIcon->passengerIcons) {
			// if icon had a message, store its pax ID
			if (icon == paxIconItem) {
				currentlyHighlightedPaxID = icon->passengerId;
				paxIconItem = nullptr;
			}

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
		auto delta = originalCenter - newCenter;
		platformIcon->textIcon->moveBy(delta.x(), delta.y());

		// add icons
		if (nPaxIcons > 0) {
			qreal iconSpacing = platformIcon->sceneBoundingRect().width() / nPaxIcons;
			qreal iconX = platformIcon->sceneBoundingRect().left() + iconSpacing / 2;

			for (const std::string& paxID : passengerIds) {
				const int iconSize = pax_pixmap_scaled.width();
				auto* item = new PassengerItem(pax_pixmap_scaled);
				item->setFlag(QGraphicsItem::ItemIgnoresTransformations);
				item->setPos(QPointF(iconX - iconSize / 2, platformIcon->sceneBoundingRect().center().y() - iconSize / 2));
				item->setTransformationMode(Qt::SmoothTransformation);

				item->passengerId = paxID;

				// update current highlighted pax icon
				if (!currentlyHighlightedPaxID.empty() && item->passengerId == currentlyHighlightedPaxID) {
					paxIconItem = item;
				}

				scene->addItem(item);
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

void MainWindow::releaseBlockOccupationStatus() {
	for (auto* track : m_activeTrackItems)
		if (track)
			track->setOperationalState(TrackOperationalState::Free);
	m_activeTrackItems.clear();
}

void MainWindow::updateSignalAspect(const std::string& ID, double code, bool reversed) {
	if (m_signalsByAheadId.find(ID) == m_signalsByAheadId.end())
		return;
	for (auto* signal : m_signalsByAheadId.at(ID)) {
		if (signal->reversedDirection == reversed) {
			signal->setAspectCode(static_cast<int>(code));
		}
	}
}

// updates train positions given a specific timestep
void MainWindow::updateTrainPosition(int t) {
	if (!m_snapshot)
		return;
	bool legendNeedsUpdate = false;
	for (auto* group : m_vcMessageItems)
		if (group)
			group->setVisible(false);
	// update every train
	releaseBlockOccupationStatus();
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
		updateBlockOccupationStatus(state);
	for (const GuiTrainState& state : m_snapshot->trains) {
		const int train = state.index;
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
			trainItem->setVisible(m_trainLayerVisible && !state.outOfSimulation);
			// update train position
			if (!state.outOfSimulation) {
				// capture previous scene center for interpolation
				QPointF oldCenter;
				auto prevIt = m_prevTrainPositions.find(train);
				if (prevIt != m_prevTrainPositions.end())
					oldCenter = prevIt.value();
				else
					oldCenter = trainItem->sceneBoundingRect().center();

				const bool hasVisibleGeometry = getTrainPolygonItemList(
					trainItem->trainPolygonItemList, state);
				trainItem->setVisible(m_trainLayerVisible && hasVisibleGeometry);
				checkVCouplingMsg(trainItem, state, t);

				// animate smooth transition from old position to new position
				QPointF newCenter = trainItem->sceneBoundingRect().center();

				TrainBadgeItem* badge = m_trainBadges.value(train, nullptr);
				if (badge) {
					badge->setIdentifier(QString::fromStdString(guiTrainDisplayIdentifier(state)));
					badge->setTooltipDetails(QString::fromStdString(state.description),
						QString::fromStdString(state.operatingCode), QString::fromStdString(state.type));
					badge->setSpeedText(QString::fromStdString(formatSpeedLabel(state.speedKmh)));
					badge->setSpeedVisible(m_trainSpeedLabelsVisible);
					badge->setTrainVisual(classifyTrainType(state.type, state.description));
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
				if (delta.manhattanLength() > 0.5) {
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
				if (hasVisibleGeometry && m_followAction && m_followAction->isChecked()
						&& m_followTrainIndex == train)
					networkView->centerOn(newCenter);
			}
			// hide train whose simulation is finished
			else {
				stopTrainAnimation(train);
				trainItem->hide();
				if (m_trainBadges.contains(train))
					m_trainBadges[train]->setVisible(false);
			}
		}
		// new train starting; >= not == because the frame throttle may drop
		// the exact departure frame, and the recorded trajectory (not the live
		// train state) is what says whether the train is on the network at t
		else if (t >= state.departureTime && state.routeAxisPosition != -9999) {
			const bool firstTrain = allTrains.isEmpty();
			paintTrain(state, node_size, line_width);
			legendNeedsUpdate = true;
			if (firstTrain && !allTrains.isEmpty() && allTrains.last()->isVisible())
				networkView->centerOn(allTrains.last()->sceneBoundingRect().center());
			if (m_followAction && m_followAction->isChecked() && m_followTrainIndex == train
					&& !allTrains.isEmpty() && allTrains.last()->isVisible())
				networkView->centerOn(allTrains.last());
		}
	}
	for (const GuiSectionState& state : m_snapshot->sectionStates)
		if (state.blocked)
			applySectionState(state, TrackOperationalState::Blocked);
	if (legendNeedsUpdate)
		updateNetworkLegend();

}

// returns the full train shape (list of polygons)
bool MainWindow::getTrainPolygonItemList(QList<TrainBodyItem*>* trainPolygonItemList, const GuiTrainState& train) {
	if (!trainPolygonItemList)
		return false;
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
	int revStIndex;												  // used to add station points to the polygon (needed for reversed routes)
	double total_nW = train.wagonCount + 1;
	const auto addPreviewPoint = [&](const TrackPreviewLine* line, double rawX,
			const QPointF& point) {
		QPointF normal;
		if (!line || !previewSignalNormal(*line, rawX, normal))
			return false;
		const QPointF offset = normal * (0.10 * track_separation);
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
	if (wagon >= static_cast<int>(train.wagonHeadPositions.size()) ||
		wagon >= static_cast<int>(train.wagonTailPositions.size()))
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
					if ((hasPreview && (!previewLine1 || !previewLine2))
							|| (!hasPreview && (!hasTrackGeometry(BS1->trackLineId)
									|| !hasTrackGeometry(BS2->trackLineId))))
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
								trainPointsUp.push_back(QPointF(ptConnection2.x() - connectionEndShiftX * 0.10 * track_separation, ptConnection2.y() - connectionEndShiftY * 0.10 * track_separation));
								trainPointsDown.push_front(QPointF(ptConnection2.x() + connectionEndShiftX * 0.10 * track_separation, ptConnection2.y() + connectionEndShiftY * 0.10 * track_separation));
								trainPointsStIndex.push_back(revPrevIndexConnection2);
								trainPointsStRegion.push_back(blockSets[BS2->trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS2->trackLineId].graphID);
							}
							if (headX > connectionX1 && tailX < connectionX1) {
								trainPointsUp.push_back(QPointF(ptConnection1.x() - connectionStartShiftX * 0.10 * track_separation, ptConnection1.y() - connectionStartShiftY * 0.10 * track_separation));
								trainPointsDown.push_front(QPointF(ptConnection1.x() + connectionStartShiftX * 0.10 * track_separation, ptConnection1.y() + connectionStartShiftY * 0.10 * track_separation));
								trainPointsStIndex.push_back(revPrevIndexConnection1);
								trainPointsStRegion.push_back(blockSets[BS1->trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS1->trackLineId].graphID);
							}
						}
						// reversed route (add first connection 1)
						else {
							if (tailX > connectionX1 && headX < connectionX1) {
								trainPointsUp.push_back(QPointF(ptConnection1.x() - connectionStartShiftX * 0.10 * track_separation, ptConnection1.y() - connectionStartShiftY * 0.10 * track_separation));
								trainPointsDown.push_front(QPointF(ptConnection1.x() + connectionStartShiftX * 0.10 * track_separation, ptConnection1.y() + connectionStartShiftY * 0.10 * track_separation));
								trainPointsStIndex.push_back(revPrevIndexConnection1);
								trainPointsStRegion.push_back(blockSets[BS1->trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS1->trackLineId].graphID);
							}
							if (tailX > connectionX2 && headX < connectionX2) {
								trainPointsUp.push_back(QPointF(ptConnection2.x() - connectionEndShiftX * 0.10 * track_separation, ptConnection2.y() - connectionEndShiftY * 0.10 * track_separation));
								trainPointsDown.push_front(QPointF(ptConnection2.x() + connectionEndShiftX * 0.10 * track_separation, ptConnection2.y() + connectionEndShiftY * 0.10 * track_separation));
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
						trainPointsUp.push_back(QPointF(ptTrainEdge.x() - connectionDeltaX * 0.10 * track_separation, ptTrainEdge.y() - connectionDeltaY * 0.10 * track_separation));
						trainPointsDown.push_front(QPointF(ptTrainEdge.x() + connectionDeltaX * 0.10 * track_separation, ptTrainEdge.y() + connectionDeltaY * 0.10 * track_separation));
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
								trainPointsUp.push_back(QPointF(ptConnection2.x() - connectionEndShiftX * 0.10 * track_separation, ptConnection2.y() - connectionEndShiftY * 0.10 * track_separation));
								trainPointsDown.push_front(QPointF(ptConnection2.x() + connectionEndShiftX * 0.10 * track_separation, ptConnection2.y() + connectionEndShiftY * 0.10 * track_separation));
								trainPointsStIndex.push_back(revPrevIndexConnection2);
								trainPointsStRegion.push_back(blockSets[BS2->trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS2->trackLineId].graphID);
							}
							if (headX > connectionX1 && tailX < connectionX1) {
								trainPointsUp.push_back(QPointF(ptConnection1.x() - connectionStartShiftX * 0.10 * track_separation, ptConnection1.y() - connectionStartShiftY * 0.10 * track_separation));
								trainPointsDown.push_front(QPointF(ptConnection1.x() + connectionStartShiftX * 0.10 * track_separation, ptConnection1.y() + connectionStartShiftY * 0.10 * track_separation));
								trainPointsStIndex.push_back(revPrevIndexConnection1);
								trainPointsStRegion.push_back(blockSets[BS1->trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS1->trackLineId].graphID);
							}
						}
						// reversed route (add first connection 1)
						else {
							if (tailX > connectionX1 && headX < connectionX1) {
								trainPointsUp.push_back(QPointF(ptConnection1.x() - connectionStartShiftX * 0.10 * track_separation, ptConnection1.y() - connectionStartShiftY * 0.10 * track_separation));
								trainPointsDown.push_front(QPointF(ptConnection1.x() + connectionStartShiftX * 0.10 * track_separation, ptConnection1.y() + connectionStartShiftY * 0.10 * track_separation));
								trainPointsStIndex.push_back(revPrevIndexConnection1);
								trainPointsStRegion.push_back(blockSets[BS1->trackLineId].region);
								trainPointsGraphID.push_back(blockSets[BS1->trackLineId].graphID);
							}
							if (tailX > connectionX2 && headX < connectionX2) {
								trainPointsUp.push_back(QPointF(ptConnection2.x() - connectionEndShiftX * 0.10 * track_separation, ptConnection2.y() - connectionEndShiftY * 0.10 * track_separation));
								trainPointsDown.push_front(QPointF(ptConnection2.x() + connectionEndShiftX * 0.10 * track_separation, ptConnection2.y() + connectionEndShiftY * 0.10 * track_separation));
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

	QString title = "Train paths (time vs distance), corridor ";
	title.append(QString::fromStdString(corridor));
	title += QString(" [%1]").arg(completedRunContext(m_completedRunProvenance));
	chart->setTitle(title);

	for (int i = 0; i < numRegions; i++) {
		// check route corridor
		if (train_route[regional_train[i].indexOfRoute].corridor == corridor &&
			regional_train[i].earliestActiveTrajectoryIndex >= 0) {
			const auto segments = validTrajectorySegments(regional_train[i].instant_spatial_position,
														 regional_train[i].earliestActiveTrajectoryIndex,
														 regional_train[i].End_Time);
			for (const auto& segment : segments) {
				QLineSeries* trainSeries = new QLineSeries();
				for (int t = segment.first; t <= segment.last; ++t) {
					double position = regional_train[i].instant_spatial_position[t];
					if (!train_route[regional_train[i].indexOfRoute].reversed_direction) {
						position /= 1000;
					} else {
						position = (train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute - position) / 1000;
						if (train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.first != 0) {
							position -= train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.first;
							corridorJumpX.first = train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.first;
							corridorJumpX.second = train_route[regional_train[i].indexOfRoute].diffRegionsJumpX.second;
						}
					}
					*trainSeries << QPointF(t * timestep, position);
					minRangeX = std::min(minRangeX, position);
					maxRangeX = std::max(maxRangeX, position);
				}
				trainSeries->setName(QString::fromStdString(regional_train[i].trainDescription));
				trainSeries->setProperty("trainId", QString::fromStdString(regional_train[i].trainDescription));
				seriesToAdd.append(trainSeries);
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

	// show the chart in the shared diagram window, which adds the train filter
	// panel, hover and click identification, and PNG and CSV export
	DiagramWindow* win = new DiagramWindow(title, this);
	win->setChart(chart);
	win->setCsvProvider(snapshotCsv(&buildTrajectoryCsv), "train_path.csv");
	attachRunProvenance(win, m_completedRunProvenance);
	connect(win, &DiagramWindow::trainSelected, this, &MainWindow::focusTrainInScene);
	win->setTimeAxisX(true, m_startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
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
	QPointF end = start + QPointF(0, -2 * track_separation);

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

	textBox->setRect(QRectF(0, 0, 1.25 * text->boundingRect().width(), 1.25 * text->boundingRect().height()));
	textBox->setBrush(QColor(242, 161, 106));
	textBox->setPos(end.x() - (textBox->boundingRect().width() / 2), end.y() - textBox->boundingRect().height());

	QPointF textPos = textBox->pos();
	textPos.rx() += 0.5 * (textBox->boundingRect().width() - text->boundingRect().width());
	textPos.ry() += 0.5 * (textBox->boundingRect().height() - text->boundingRect().height());
	text->setPos(textPos);
	msgGroup->setVisible(true);
}

// --- Zoom controls ---
void MainWindow::zoomIn() {
	if (!networkView)
		return;
	if (networkView->zoomBy(1.15) && !m_previewFitBounds.isEmpty())
		networkView->centerOn(m_previewZoomFocus);
}

void MainWindow::zoomOut() {
	if (networkView)
		networkView->zoomBy(1.0 / 1.15);
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
	win->setChart(chart);
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
			series->append(row.journeyIndex, row.arrivalDelaySeconds.value / 60.0);
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
	win->setChart(chart);
	win->setCsvProvider(snapshotCsv(&buildTimetableCsv), "timetable.csv");
	attachRunProvenance(win, m_completedRunProvenance);
	connect(win, &DiagramWindow::trainSelected, this, &MainWindow::focusTrainInScene);
	win->setTimeAxisX(false, m_startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

void MainWindow::showTimetableGraph() {
	if (!hasRunResults()) {
		statusBar()->showMessage("Run a simulation to open diagrams", 5000);
		return;
	}

	QChart* chart = new QChart();
	const QString title = QString("Train graph: planned vs simulated arrival/departure [%1]")
		.arg(completedRunContext(m_completedRunProvenance));
	chart->setTitle(title);
	const auto rows = buildTimetableResults(runResultTrainPointers());

	for (int i = 0; i < numRegions; i++) {
		const std::string trainId = regional_train[i].trainDescription;
		const QString trainName = QString::fromStdString(trainId);
		QLineSeries* simulatedArrival = new QLineSeries();
		simulatedArrival->setName(trainName + " (simulated arrival)");
		simulatedArrival->setProperty("trainId", trainName);
		QLineSeries* plannedArrival = new QLineSeries();
		plannedArrival->setName(trainName + " (planned arrival)");
		plannedArrival->setProperty("trainId", trainName);
		QLineSeries* simulatedDeparture = new QLineSeries();
		simulatedDeparture->setName(trainName + " (simulated departure)");
		simulatedDeparture->setProperty("trainId", trainName);
		QLineSeries* plannedDeparture = new QLineSeries();
		plannedDeparture->setName(trainName + " (planned departure)");
		plannedDeparture->setProperty("trainId", trainName);

		for (const TimetableResultRow& row : rows) {
			if (row.trainId != trainId)
				continue;
			if (row.simulatedArrivalSeconds.available)
				simulatedArrival->append(row.simulatedArrivalSeconds.value, row.journeyIndex);
			if (row.plannedArrivalSeconds.available)
				plannedArrival->append(row.plannedArrivalSeconds.value, row.journeyIndex);
			if (row.simulatedDepartureSeconds.available)
				simulatedDeparture->append(row.simulatedDepartureSeconds.value, row.journeyIndex);
			if (row.plannedDepartureSeconds.available)
				plannedDeparture->append(row.plannedDepartureSeconds.value, row.journeyIndex);
		}

		auto addSeriesPair = [chart](QLineSeries* simulated, QLineSeries* planned) {
			if (simulated->count() > 0) {
				chart->addSeries(simulated);
				if (planned->count() > 0) {
					QPen plannedPen = planned->pen();
					plannedPen.setStyle(Qt::DashLine);
					plannedPen.setColor(simulated->pen().color());
					planned->setPen(plannedPen);
					chart->addSeries(planned);
				} else {
					delete planned;
				}
			} else if (planned->count() > 0) {
				QPen plannedPen = planned->pen();
				plannedPen.setStyle(Qt::DashLine);
				planned->setPen(plannedPen);
				chart->addSeries(planned);
				delete simulated;
			} else {
				delete simulated;
				delete planned;
			}
		};
		addSeriesPair(simulatedArrival, plannedArrival);
		addSeriesPair(simulatedDeparture, plannedDeparture);
	}
	chart->createDefaultAxes();
	if (!chart->axes(Qt::Horizontal).isEmpty())
		chart->axes(Qt::Horizontal).first()->setTitleText("Time (simulation seconds)");
	if (!chart->axes(Qt::Vertical).isEmpty())
		chart->axes(Qt::Vertical).first()->setTitleText("Journey order (1-based)");

	DiagramWindow* win = new DiagramWindow(title, this);
	win->setChart(chart);
	win->setCsvProvider(snapshotCsv(&buildTimetableCsv), "timetable.csv");
	attachRunProvenance(win, m_completedRunProvenance);
	connect(win, &DiagramWindow::trainSelected, this, &MainWindow::focusTrainInScene);
	win->setTimeAxisX(true, m_startOffsetSeconds);
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
	const std::vector<BlockingTimeDiagramSegment> segments =
		scope.routeIndex >= 0 && scope.trainIds.empty()
			? std::vector<BlockingTimeDiagramSegment>()
			: filterBlockingTimeDiagramSegments(allSegments, scope.trainIds, scope.blockIds,
				scope.startTime, scope.endTime);
	const std::vector<BlockingTimePlannedReference> plannedReferences =
		filterBlockingTimePlannedReferences(buildBlockingTimePlannedReferences(scope),
			scope.startTime, scope.endTime);
	if (segments.empty() && plannedReferences.empty()) {
		QMessageBox::information(this, "No Data", "No complete blocking-time data is available for this simulation.");
		return;
	}

	QChart* chart = new QChart();
	QString routeScope = QStringLiteral("all routes");
	if (scope.routeIndex >= 0 && scope.routeIndex < static_cast<int>(train_route.size())) {
		const Route& route = train_route[static_cast<std::size_t>(scope.routeIndex)];
		routeScope = QString::fromStdString(route.ID);
		if (!route.corridor.empty())
			routeScope += QString(" / %1").arg(QString::fromStdString(route.corridor));
		if (!scope.blockIds.empty())
			routeScope += QString(" / %1 to %2").arg(QString::fromStdString(scope.blockIds.front()),
				QString::fromStdString(scope.blockIds.back()));
	}
	const QString title = QString("Blocking time: actual occupations and dashed planned timetable | %1 | %2 to %3 [%4]")
		.arg(routeScope,
			QString::fromStdString(formatSimTime(static_cast<long long>(scope.startTime), m_startOffsetSeconds)),
			QString::fromStdString(formatSimTime(static_cast<long long>(scope.endTime), m_startOffsetSeconds)),
			completedRunContext(m_completedRunProvenance));
	chart->setTitle(title);

	addBlockingTimeSeries(chart, segments, false);

	const QColor plannedColors[] = {
		QColor(36, 117, 181), QColor(205, 92, 92), QColor(46, 139, 87),
		QColor(138, 43, 226), QColor(210, 105, 30), QColor(0, 128, 128)};
	std::map<std::string, QLineSeries*> plannedSeries;
	int plannedColorIndex = 0;
	for (const BlockingTimePlannedReference& reference : plannedReferences) {
		auto it = plannedSeries.find(reference.trainName);
		if (it == plannedSeries.end()) {
			auto* series = new QLineSeries();
			series->setName(QString::fromStdString(reference.trainName + " (planned reference)"));
			series->setProperty("trainId", QString::fromStdString(reference.trainName));
			QPen pen(plannedColors[plannedColorIndex % (sizeof(plannedColors) / sizeof(plannedColors[0]))]);
			pen.setStyle(Qt::DashLine);
			pen.setWidthF(2.5);
			series->setPen(pen);
			series->setPointsVisible(true);
			chart->addSeries(series);
			it = plannedSeries.emplace(reference.trainName, series).first;
			++plannedColorIndex;
		}
		it->second->append(reference.time, reference.positionKm);
	}

	QLineSeries* dummySwitch = new QLineSeries();
	dummySwitch->setName("Key: Switch");
	QPen dummySwitchPen(kBlockingSwitchColor);
	dummySwitchPen.setWidthF(4.0);
	dummySwitch->setPen(dummySwitchPen);
	chart->addSeries(dummySwitch);

	QLineSeries* dummyCritical = new QLineSeries();
	dummyCritical->setName("Key: Conflict");
	QPen dummyCriticalPen(kBlockingCriticalColor);
	dummyCriticalPen.setWidthF(4.0);
	dummyCritical->setPen(dummyCriticalPen);
	chart->addSeries(dummyCritical);

	chart->createDefaultAxes();
	if (!chart->axes(Qt::Horizontal).isEmpty()) {
		chart->axes(Qt::Horizontal).first()->setTitleText("Time");
		if (auto* axis = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first()))
			axis->setRange(scope.startTime, scope.endTime);
	}
	if (!chart->axes(Qt::Vertical).isEmpty()) {
		chart->axes(Qt::Vertical).first()->setTitleText("Position (km)");
	}

	DiagramWindow* win = new DiagramWindow(title, this);
	win->setChart(chart);
	const std::function<std::string(const QStringList&)> scopedCsv =
		[segments, plannedReferences](const QStringList& visibleTrainIds) {
			return buildBlockingTimeCsv(visibleTrainIds, segments, plannedReferences);
		};
	win->setCsvProvider(snapshotCsv(scopedCsv), "blocking_time.csv");
	attachRunProvenance(win, m_completedRunProvenance);
	connect(win, &DiagramWindow::trainSelected, this, &MainWindow::focusTrainInScene);
	win->setTimeAxisX(true, m_startOffsetSeconds);
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
	if ((!e2eDialogsSuppressed() || qEnvironmentVariableIsSet("QEGTRAIN_E2E_CREATOR_ACCEPTANCE"))
			&& !chooseCapacityAnalysisScope(this, scope))
		return;
	if (scope.routeIndex < 0 || scope.blockIds.empty()) {
		QMessageBox::information(this, "Capacity analysis",
			"No route section has two occurrences with a common entry block. Choose a common entry or split the analysis into pre-/post-Gdg sections.");
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
	dialog->setWindowTitle(QString("Capacity analysis | %1 [%2]").arg(sectionLabel,
		completedRunContext(provenance)));
	dialog->setModal(false);
	dialog->resize(1050, 650);
	auto* layout = new QVBoxLayout(dialog);
	auto* summary = new QLabel(dialog);
	summary->setWordWrap(true);
	const QString percentage = QString::fromStdString(csv::formatDouble(result.cyclePercentage));
	const QString cycle = QString::fromStdString(csv::formatDouble(result.cycleTime));
	const QString period = QString::fromStdString(csv::formatDouble(result.periodSeconds));
	const auto sourceFor = [&result](const std::string& identity) {
		for (std::size_t index = 0; index < result.trainIdentities.size(); ++index)
			if (result.trainIdentities[index] == identity && index < result.referenceSources.size())
				return QString::fromStdString(result.referenceSources[index]);
		return QString();
	};
	summary->setText(QString("Section: %1 | cycle/occupation time = %2 s | period = %3 s | cycle/period*100 = %4%% | "
		"cycle start = %5 (%6) | cycle end = %7 (%8) | conflict-free compressed occupations. "
		"Capacity critical blocks are touching constraints, distinct from overlap/conflict styling.")
		.arg(sectionLabel, cycle, period, percentage, QString::fromStdString(result.firstIdentity),
			sourceFor(result.firstIdentity), QString::fromStdString(result.cycleEndIdentity),
			sourceFor(result.cycleEndIdentity)));
	layout->addWidget(summary);

	auto* tabs = new QTabWidget(dialog);
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
			governing << QString("%1 (%2 s): %3").arg(QString::fromStdString(predecessor.predecessorIdentity),
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

	auto* buttons = new QHBoxLayout();
	QPushButton* exportButton = new QPushButton("Export capacity CSV...", dialog);
	connect(exportButton, &QPushButton::clicked, dialog, [this, result, sectionLabel, provenance]() {
		saveCsvInteractive(this, "capacity_analysis.csv", buildCapacityAnalysisCsv(result, sectionLabel),
			[provenance](const QString& path, const std::string& bytes) {
				return writeRunArtifactWithProvenance(path.toStdString(), "csv", bytes, provenance);
			});
	});
	QPushButton* diagramButton = new QPushButton("Open compressed blocking-time diagram", dialog);
	connect(diagramButton, &QPushButton::clicked, dialog, [this, result, sectionLabel, provenance]() {
		showCompressedBlockingTimeDiagram(result, sectionLabel, provenance);
	});
	QPushButton* closeButton = new QPushButton("Close", dialog);
	connect(closeButton, &QPushButton::clicked, dialog, &QDialog::close);
	buttons->addWidget(exportButton);
	buttons->addWidget(diagramButton);
	buttons->addStretch();
	buttons->addWidget(closeButton);
	layout->addLayout(buttons);
	dialog->show();
}

void MainWindow::showCompressedBlockingTimeDiagram(const CapacityAnalysisResult& result,
	const QString& sectionLabel, RunProvenance provenance) {
	const std::vector<BlockingTimeDiagramSegment> segments = buildBlockingTimeDiagramSegments(
		result.compressedOccupations, result.trainIdentities);
	if (segments.empty()) {
		QMessageBox::information(this, "Capacity diagram", "No complete compressed occupation data is available.");
		return;
	}
	QChart* chart = new QChart();
	chart->setTitle(QString("Compressed blocking-time diagram | %1 | cycle %2 to %3 [%4]")
		.arg(sectionLabel, QString::fromStdString(result.firstIdentity),
			QString::fromStdString(result.cycleEndIdentity), completedRunContext(provenance)));
	addBlockingTimeSeries(chart, segments, true);
	auto addKey = [chart](const QString& name, const QColor& color) {
		auto* series = new QLineSeries();
		series->setName(name);
		QPen pen(color);
		pen.setWidthF(4.0);
		series->setPen(pen);
		chart->addSeries(series);
	};
	addKey("Key: Conflict", kBlockingCriticalColor);
	addKey("Key: Capacity critical block (touching)", kBlockingCapacityColor);
	chart->createDefaultAxes();
	if (!chart->axes(Qt::Horizontal).isEmpty())
		chart->axes(Qt::Horizontal).first()->setTitleText("Time");
	if (!chart->axes(Qt::Vertical).isEmpty())
		chart->axes(Qt::Vertical).first()->setTitleText("Position (km)");
	DiagramWindow* window = new DiagramWindow(chart->title(), this);
	window->setChart(chart);
	const std::function<std::string(const QStringList&)> csvProvider =
		[segments](const QStringList& visibleTrainIds) {
			return buildBlockingTimeCsv(visibleTrainIds, segments, {});
	};
	window->setCsvProvider(csvProvider, "capacity_compressed_blocking_time.csv");
	attachRunProvenance(window, std::move(provenance));
	connect(window, &DiagramWindow::trainSelected, this, &MainWindow::focusTrainInScene);
	window->setTimeAxisX(true, m_startOffsetSeconds);
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
	const bool preview = !m_previewFitBounds.isEmpty();
	content.hasTracks = numTrackLines > 0 || !m_sceneModel.tracks.empty();
	content.showOperationalTrackStates = !preview;
	content.hasSelectedTrack = preview && m_previewHasSelectedTrack;
	for (const TrainItemGroup* train : allTrains) {
		if (train)
			content.trainVisuals << classifyTrainType(train->trainType, train->trainDescription);
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

	for (auto* overlay : m_stationOverlays) {
		if (!overlay)
			continue;
		int highestDegree = 0;
		bool hasInterchange = false;
		bool hasEndpoint = false;
		for (auto* item : scene->items()) {
			auto* station = qgraphicsitem_cast<StationNodeItem*>(item);
			if (!station || !station->node || !station->isVisible()
				|| QString::fromStdString(station->node->stationName) != overlay->stationName())
				continue;
			const QPointF point = station->sceneBoundingRect().center();
			int degree = 0;
			for (auto* edge : scene->items()) {
				if (!edge || !edge->isVisible())
					continue;
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
	const bool dense = networkView->zoomRatio() >= kDenseDetailZoom;
	const bool signalDetail = networkView->zoomRatio() >= kSignalDetailZoom;
	const qreal stationLabelScale = qMin<qreal>(3.0,
		std::sqrt(qMax<qreal>(1.0, networkView->zoomRatio() / kDenseDetailZoom)));

	const QTransform toDevice = networkView->viewportTransform();
	const QRectF viewport = networkView->viewport()->rect();
	const QRectF inset = viewport.adjusted(kOverlayMargin, kOverlayMargin,
		-kOverlayMargin, -kOverlayMargin);
	struct CandidatePlacement {
		StationOverlayItem* overlay = nullptr;
		StationOverlayItem::ViewportPlacement right;
		StationOverlayItem::ViewportPlacement left;
		StationOverlayItem::ViewportPlacement above;
		StationOverlayItem::ViewportPlacement below;
		StationOverlayItem::ViewportPlacement current;
		int symbolIndex = -1;
	};

	QList<QRectF> symbolRects;
	QList<StationOverlayItem*> candidates;
	QList<CandidatePlacement> placements;
	for (auto* overlay : m_stationOverlays) {
		if (!overlay)
			continue;
		overlay->setLabelScale(stationLabelScale);
		overlay->setViewportOffset(QPointF());
		overlay->setCollisionBlocked(false);
		overlay->setSelected(!m_selectedStationName.isEmpty()
			&& overlay->stationName() == m_selectedStationName);
		overlay->setVisible(m_stationLayerVisible);
		if (!m_stationLayerVisible)
			continue;

		const QPointF anchor = toDevice.map(overlay->stableAnchor());
		CandidatePlacement placement;
		placement.overlay = overlay;
		placement.right = overlay->placementForSide(StationOverlayItem::LabelSide::Right, anchor, inset);
		placement.left = overlay->placementForSide(StationOverlayItem::LabelSide::Left, anchor, inset);
		placement.above = overlay->placementForSide(StationOverlayItem::LabelSide::Above, anchor, inset);
		placement.below = overlay->placementForSide(StationOverlayItem::LabelSide::Below, anchor, inset);
		placement.current = overlay->preferredViewportPlacement(anchor, inset);
		overlay->setLabelSide(placement.current.side);
		overlay->setViewportOffset(placement.current.offset);
		placement.symbolIndex = symbolRects.size();
		symbolRects.append(placement.current.symbolRect);
		placements.append(placement);
		candidates.append(overlay);
	}
	const auto placementFor = [&placements](StationOverlayItem* overlay) -> CandidatePlacement* {
		for (auto& placement : placements)
			if (placement.overlay == overlay)
				return &placement;
		return nullptr;
	};

	QPointF followedCenter;
	bool hasFollowedCenter = false;
	if (m_followAction && m_followAction->isChecked() && m_followTrainIndex >= 0) {
		if (auto* train = resolveTrainItem(m_followTrainIndex)) {
			followedCenter = train->sceneBoundingRect().center();
			hasFollowedCenter = true;
		}
	}
	StationOverlayItem* nearestFollowed = nullptr;
	qreal nearestDistance = std::numeric_limits<qreal>::max();
	for (auto* overlay : candidates) {
		overlay->setFollowed(false);
		if (!hasFollowedCenter)
			continue;
		const QPointF delta = overlay->stableAnchor() - followedCenter;
		const qreal distance = delta.x() * delta.x() + delta.y() * delta.y();
		bool nearer = !nearestFollowed || distance < nearestDistance;
		if (!nearer && distance == nearestDistance) {
			const int nameOrder = QString::compare(overlay->stationName(), nearestFollowed->stationName(),
				Qt::CaseSensitive);
			nearer = nameOrder < 0
				|| (nameOrder == 0 && (overlay->stableAnchor().x() < nearestFollowed->stableAnchor().x()
					|| (overlay->stableAnchor().x() == nearestFollowed->stableAnchor().x()
						&& overlay->stableAnchor().y() < nearestFollowed->stableAnchor().y())));
		}
		if (nearer) {
			nearestFollowed = overlay;
			nearestDistance = distance;
		}
	}
	if (nearestFollowed)
		nearestFollowed->setFollowed(true);

	std::sort(candidates.begin(), candidates.end(), [&](const auto* left, const auto* right) {
		return StationOverlayItem::priorityLess(*left, *right,
			toDevice.inverted().map(viewport.center()));
	});
	const bool hasTopologyPriority = std::any_of(candidates.cbegin(), candidates.cend(), [](const auto* overlay) {
		return overlay->isInterchange() || overlay->isEndpoint();
	});
	const bool showOrdinaryOverviewLabels = !hasTopologyPriority;
	QList<QRectF> placedLabels;
	for (auto* overlay : candidates) {
		const bool forced = overlay->isSelected() || overlay->isFollowed() || overlay->isHovered();
		const bool eligible = networkView->zoomRatio() >= 2.0
			|| forced || overlay->isInterchange() || overlay->isEndpoint()
			|| showOrdinaryOverviewLabels;
		bool visible = false;
		bool collisionBlocked = false;
		CandidatePlacement* placement = placementFor(overlay);
		if (placement) {
			const StationOverlayItem::ViewportPlacement* choices[4] = {
				&placement->right, &placement->left, &placement->above, &placement->below};
			if (placement->current.side == StationOverlayItem::LabelSide::Left) {
				choices[0] = &placement->left;
				choices[1] = &placement->right;
			}
			const StationOverlayItem::ViewportPlacement* chosen = nullptr;
			const StationOverlayItem::ViewportPlacement* hoverFallback = nullptr;
			for (const auto* choice : choices) {
				if (!choice->fits)
					continue;
				const QRectF inflated = choice->labelRect.adjusted(-4.0, -4.0, 4.0, 4.0);
				bool symbolCollision = false;
				for (const QRectF& symbol : symbolRects) {
					if (inflated.intersects(symbol)) {
						symbolCollision = true;
						break;
					}
				}
				if (symbolCollision)
					continue;
				bool symbolMovedIntoLabel = false;
				for (const QRectF& other : placedLabels) {
					if (other.intersects(choice->symbolRect)) {
						symbolMovedIntoLabel = true;
						break;
					}
				}
				if (symbolMovedIntoLabel)
					continue;
				if (!hoverFallback)
					hoverFallback = choice;
				if (!eligible)
					continue;
				bool labelCollision = false;
				for (const QRectF& other : placedLabels) {
					if (inflated.intersects(other)) {
						labelCollision = true;
						break;
					}
				}
				if (labelCollision)
					continue;
				chosen = choice;
				break;
			}
			if (!chosen && hoverFallback) {
				placement->current = *hoverFallback;
				overlay->setLabelSide(hoverFallback->side);
				overlay->setViewportOffset(hoverFallback->offset);
				symbolRects[placement->symbolIndex] = hoverFallback->symbolRect;
			}
			if (!chosen)
				collisionBlocked = !hoverFallback;
			if (chosen) {
				placement->current = *chosen;
				overlay->setLabelSide(chosen->side);
				overlay->setViewportOffset(chosen->offset);
				symbolRects[placement->symbolIndex] = chosen->symbolRect;
				visible = true;
				placedLabels.append(chosen->labelRect.adjusted(-4.0, -4.0, 4.0, 4.0));
			}
		}
		overlay->setCollisionBlocked(collisionBlocked);
		overlay->setLayoutVisible(visible);
	}

	// Keep markers readable without painting dense section boundaries on top of
	// one another. Higher zoom progressively reveals closer signals.
	QList<QPointF> signalCenters;
	const qreal minimumSignalDistanceSquared = signalDetail ? 144.0 : 576.0;
	for (auto* item : m_signalDecorations) {
		if (!item)
			continue;
		const bool baseVisible = item->data(kSignalBaseVisibleRole).toBool();
		auto* signal = qgraphicsitem_cast<SignalItem*>(item);
		if (signal && item->data(kSignalAnchorRole).isValid()) {
			const qreal viewScale = std::hypot(toDevice.m11(), toDevice.m12());
			if (viewScale > 0.0) {
				const QPointF anchor = item->data(kSignalAnchorRole).toPointF();
				const QPointF normal = item->data(kSignalNormalRole).toPointF();
				const qreal direction = item->data(kSignalDirectionRole).toReal();
				item->setPos(anchor + direction * kPreviewSignalOffsetPixels / viewScale * normal);
			}
		}
		if (signal)
			signal->setScale(signalDetail ? 1.0 : 0.7);
		bool visible = m_signalLayerVisible && baseVisible && (signal || signalDetail);
		if (visible && signal) {
			const QPointF center = toDevice.map(signal->scenePos());
			visible = inset.contains(center)
				&& std::none_of(signalCenters.cbegin(), signalCenters.cend(),
					[&center, minimumSignalDistanceSquared](const QPointF& existing) {
						const QPointF delta = center - existing;
						return delta.x() * delta.x() + delta.y() * delta.y()
							< minimumSignalDistanceSquared;
					});
			if (visible)
				signalCenters.append(center);
		}
		item->setVisible(visible);
	}

	const bool paxText = paxTextVisible();
	for (auto* platform : allPlatforms) {
		if (platform && platform->textIcon)
			platform->textIcon->setVisible(paxText);
	}

	for (auto it = m_trainBadges.cbegin(); it != m_trainBadges.cend(); ++it) {
		if (!it.value())
			continue;
		const bool promoted = isTrainOverlayPromoted(it.key());
		it.value()->setPromoted(promoted);
		it.value()->setPresentation(
			TrainBadgeItem::presentationForZoom(networkView->zoomRatio(), promoted));
	}
}

void MainWindow::updateZoomStatus() {
	if (m_zoomStatusLabel && networkView)
		m_zoomStatusLabel->setText(networkView->zoomLabel());
}
