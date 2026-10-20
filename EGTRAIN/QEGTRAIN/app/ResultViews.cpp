// RunController.h brings in SceneModel.h in the way that the member named signals needs, before the other Qt headers do.
#include "app/RunController.h"
#include "app/ResultViews.h"
#include "app/DialogFolders.h"
#include "app/MainWindowSupport.h"
#include "app/MainWindowTestHooks.h"
#include "app/PathMessages.h"
#include "diagrams/AreaCapacityWindow.h"
#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/BlockingTimeScope.h"
#include "diagrams/CapacityAnalysis.h"
#include "diagrams/CapacityScope.h"
#include "diagrams/DiagramWindow.h"
#include "diagrams/RouteDiagramCoordinates.h"
#include "diagrams/RouteReferenceChoice.h"
#include "diagrams/RunExports.h"
#include "diagrams/RunResults.h"
#include "diagrams/TimetableTableWindow.h"
#include "diagrams/TrainColors.h"
#include "util/CsvWriter.h"
#include "util/TimeFormat.h"
#include "util/TrajectoryUtil.h"
#include "widgets/DialogLayout.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QCategoryAxis>
#include <QtCharts/QChart>
#include <QtCharts/QLegendMarker>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

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

} // namespace

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

ResultViews::ResultViews(QWidget* window, RunController* runController, Host& host)
	: QObject(window), m_window(window), m_runController(runController), m_host(host) {}

// Choose a reference from routes that were actually used in this run.
int ResultViews::chooseRouteDiagramReference(const CompletedRunData& run, const QString& purpose) const {
	const QVector<RouteReferenceChoice> choices = buildRouteReferenceChoices(m_host.sceneModel(), usedRouteReferencesFrom(run));
	if (choices.isEmpty()) return -1;
	if (m_host.dialogsSuppressed()) return choices.first().runtimeIndex;
	RouteReferenceDialog dialog(choices, purpose, m_window);
	return dialog.exec() == QDialog::Accepted ? dialog.selectedRuntimeIndex() : -1;
}

void ResultViews::displayTrainPathDiagrams() {
	if (!m_runController->hasRunResults()) return;
	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	const int reference = chooseRouteDiagramReference(*run, "train paths");
	if (reference >= 0) buildRouteDiagram(run, false, reference);
}

// --- Per-train diagram slots ---
void ResultViews::showSpeedDistanceDiagram() {
	buildPerTrainDiagram(0);
}
void ResultViews::showSpeedTimeDiagram() {
	buildPerTrainDiagram(1);
}
void ResultViews::showTimeDistanceDiagram() {
	buildPerTrainDiagram(2);
}

void ResultViews::showTractiveEffortDistanceDiagram() {
	buildPerTrainDiagram(3);
}

// mode 0: speed vs distance, 1: speed vs time, 2: time vs distance,
// 3: simulated tractive effort vs distance
void ResultViews::buildPerTrainDiagram(int mode) {
	// require a completed run
	if (!m_runController->hasRunResults()) {
		m_host.resultMessage("Run a simulation to open diagrams", 5000);
		return;
	}

	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	const char* titles[] = {ResultViewName::kSpeedDistance, ResultViewName::kSpeedTime, ResultViewName::kTimeDistance,
		ResultViewName::kTractiveEffortDistance};
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

	DiagramWindow* win = new DiagramWindow(heading, m_window);
	win->setProperty("manualSection", QStringLiteral("train-diagram"));
	win->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	win->setChart(chart);
	win->setPresentation(heading, QString("Run: %1").arg(completedRunContext(run->provenance)), QString());
	win->setTrainColors(trainColorsForRun(run->results, m_host.sceneModel()));
	win->setCsvProvider(csvProviderFrom(run, &trajectoryCsvFrom), "trajectory.csv");
	attachRunProvenance(win, run->provenance);
	connect(win, &DiagramWindow::trainSelected, this, &ResultViews::trainSelected);
	win->setTimeAxisX(mode == 1 || mode == 2, run->settings.startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

void ResultViews::showDelayAnalysis() {
	m_runController->openDelayAnalysis();
}

void ResultViews::showTimetableTable() {
	if (!m_runController->hasRunResults()) {
		m_host.resultMessage("Run a simulation to open diagrams", 5000);
		return;
	}

	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	auto* window = new TimetableTableWindow(run->timetable, run->settings.startOffsetSeconds,
		csvProviderFrom(run, &timetableCsvFrom), m_window);
	window->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	window->setTrainColors(trainColorsForRun(run->results, m_host.sceneModel()));
	window->setRunProvenance(run->provenance);
	window->setWindowTitle(QString("%1 [%2]").arg(ResultViewName::kTimetable, completedRunContext(run->provenance)));
	window->setAttribute(Qt::WA_DeleteOnClose);
	window->show();
}

void ResultViews::showDelayDiagram() {
	if (!m_runController->hasRunResults()) {
		m_host.resultMessage("Run a simulation to open diagrams", 5000);
		return;
	}

	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	QChart* chart = new QChart();
	const QString heading = ResultViewName::kDelays;
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

	DiagramWindow* win = new DiagramWindow(heading, m_window);
	win->setProperty("manualSection", QStringLiteral("timetable-results"));
	win->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	win->setChart(chart);
	win->setPresentation(heading, QString("Run: %1").arg(completedRunContext(run->provenance)), QString());
	win->setTrainColors(trainColorsForRun(run->results, m_host.sceneModel()));
	win->setCsvProvider(csvProviderFrom(run, &timetableCsvFrom), "timetable.csv");
	attachRunProvenance(win, run->provenance);
	connect(win, &DiagramWindow::trainSelected, this, &ResultViews::trainSelected);
	win->setTimeAxisX(false, run->settings.startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

void ResultViews::showTimetableGraph() {
	if (!m_runController->hasRunResults()) return;
	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	const int reference = chooseRouteDiagramReference(*run, "timetable");
	if (reference >= 0) buildRouteDiagram(run, true, reference);
}

void ResultViews::buildRouteDiagram(const std::shared_ptr<const CompletedRunData>& run, bool timetable, int referenceIndex) {
	const std::optional<RouteDiagramData> diagram = routeDiagramData(*run, timetable, referenceIndex);
	if (!diagram) return;
	const RouteDiagramPath& referencePath = diagram->referencePath;
	const double origin = referencePath.nodes.front().positionKm;
	const double end = referencePath.nodes.back().positionKm;
	const QString heading = timetable ? ResultViewName::kTimetableGraph : ResultViewName::kTrainPaths;
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
	DiagramWindow* win = new DiagramWindow(heading, m_window);
	win->setProperty("manualSection", QStringLiteral("train-paths"));
	win->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	win->setChart(chart);
	if (stationAxis) win->setStationAxis(stationAxis, std::move(stationTicks));
	win->setTrainColors(trainColorsForRun(run->results, m_host.sceneModel()));
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
	connect(win, &DiagramWindow::trainSelected, this, &ResultViews::trainSelected);
	win->setTimeAxisY(true, run->settings.startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

void ResultViews::showBlockingTimeDiagram() {
	if (!m_runController->hasRunResults()) {
		m_host.resultMessage("Run a simulation to open diagrams", 5000);
		return;
	}

	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	BlockingTimeScope scope = defaultBlockingTimeScopeFrom(*run);
	if (!m_host.dialogsSuppressed() && !chooseBlockingTimeScope(m_window, blockingTimeScopeChoicesFrom(*run), scope))
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
	chart->setTitle(ResultViewName::kBlockingTime);

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

	DiagramWindow* win = new DiagramWindow(ResultViewName::kBlockingTime, m_window);
	win->setProperty("manualSection", QStringLiteral("blocking-time"));
	win->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	win->setChart(chart);
	win->setBlockSections(extents, QString::fromStdString(referencePath.id));
	// The envelope colours show the block type, so only the train list takes the service colours.
	win->setTrainListColors(trainColorsForRun(run->results, m_host.sceneModel()));
	const bool nothingPlotted = segments.empty() && plottedPlanned.empty() && trajectoryRows.empty();
	const QString note = QString("Calculated envelopes, not observed occupation; movement only within scope. "
								 "Unmapped endpoints/events omitted: %1; incomplete/missing-clearance blocks omitted. No extrapolation. "
								 "Envelope colours: blue blocks/stations, yellow switches, red conflicts.")
							 .arg(omitted)
		+ (nothingPlotted ? QString(" No complete envelope could be projected in this scope.") : QString())
		+ QString::fromStdString(blockSectionGeometryNote(extents));
	win->setPresentation(ResultViewName::kBlockingTime,
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
	connect(win, &DiagramWindow::trainSelected, this, &ResultViews::trainSelected);
	win->setTimeAxisY(true, run->settings.startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	win->show();
}

void ResultViews::showCapacityAnalysis() {
	if (!m_runController->hasRunResults()) {
		m_host.resultMessage("Run a simulation to open capacity analysis", 5000);
		return;
	}

	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	CapacityAnalysisScope scope = firstCapacityScopeWithPairFrom(*run);
	if (scope.routeIndex < 0) {
		QMessageBox::information(m_window, ResultViewName::kCapacity,
			"No retained native blocking-time section has two occurrences with a common entry block. "
			"Run a scene with explicit signalling data; EGTRAIN does not infer a signalling system or blocking-time parameters.");
		return;
	}
	if ((!m_host.dialogsSuppressed() || mainWindowTestHooks().askCapacityScope)
		&& !chooseCapacityAnalysisScope(m_window, *run, scope))
		return;
	if (scope.routeIndex < 0 || scope.blockIds.empty()) {
		QMessageBox::information(m_window, ResultViewName::kCapacity,
			"No route section has two occurrences with a common entry block. Choose a common entry or analyze the route in separate sections.");
		return;
	}
	const std::string scopeIssue = capacityScopeIssueFrom(*run, scope);
	if (!scopeIssue.empty()) {
		QMessageBox::information(m_window, "Capacity scope unavailable", QString::fromStdString(scopeIssue));
		return;
	}
	const auto trains = capacityInputsFrom(*run, scope);
	if (trains.size() < 2) {
		QMessageBox::information(m_window, ResultViewName::kCapacity,
			"Fewer than two occurrences share the selected section entry. Choose a common entry or a narrower section.");
		return;
	}
	const QString sectionLabel = capacityScopeLabelFrom(*run, scope);
	const CapacityAnalysisResult result = analyzeCapacity(trains, scope.periodSeconds,
		scope.cycleEndOccurrenceId);
	if (!result.analyzable) {
		QMessageBox::information(m_window, ResultViewName::kCapacity,
			"The selected occurrences do not form a valid shared-resource chain. Choose a common entry or analyze the route in separate sections.");
		return;
	}
	if (result.cycleEndIdentity.empty() || result.cycleTime < 0.0) {
		QMessageBox::information(m_window, ResultViewName::kCapacity,
			"The cycle-closing occurrence must be a selected row after the first occurrence.");
		return;
	}

	const RunProvenance provenance = run->provenance;
	QDialog* dialog = new QDialog(m_window);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(ResultViewName::kCapacity);
	dialog->setProperty("manualSection", QStringLiteral("capacity"));
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
	// Numbers are aligned to the right, as in the other analysis tables.
	const auto cell = [](const QString& text, bool number) {
		auto* item = new QTableWidgetItem(text);
		if (number)
			item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
		return item;
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
			pairTable->setItem(row, column, cell(values[column], column >= 4 && column <= 6));
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
			compressionTable->setItem(row, column, cell(values[column], column >= 2 && column <= 5));
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
			criticalTable->setItem(row, column, cell(values[column], column == 4));
	}
	tabs->setTabText(2, QString("Critical blocks (%1)").arg(criticalTable->rowCount()));
	for (int index = 0; index < tabs->count(); ++index)
		qobject_cast<QTableWidget*>(tabs->widget(index))->resizeColumnsToContents();
	layout->addWidget(tabs, 1);

	auto* secondaryActions = new QHBoxLayout();
	QPushButton* exportButton = new QPushButton("Export capacity CSV...", body);
	connect(exportButton, &QPushButton::clicked, dialog, [this, result, sectionLabel, provenance]() {
		const auto operation = m_host.captureTelemetryOperation();
		saveCsvInteractive(
			m_window, "capacity_analysis.csv", operation,
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
	// No button of the window is a default button, so Return presses none.
	for (QPushButton* button : dialog->findChildren<QPushButton*>())
		button->setAutoDefault(false);
	dialog->show();
}

void ResultViews::showAreaCapacity() {
	if (!m_runController->hasRunResults()) {
		m_host.resultMessage("Run a simulation to open capacity by area", 5000);
		return;
	}
	// The options dialog reads the run until it closes, so this call holds it.
	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	openAreaCapacityWindow(m_window, *run, [this] { return m_host.captureTelemetryOperation(); });
}

void ResultViews::showCompressedBlockingTimeDiagram(const CapacityAnalysisResult& result,
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
	DiagramWindow* window = new DiagramWindow("Compressed blocking-time diagram", m_window);
	window->setProperty("manualSection", QStringLiteral("capacity"));
	window->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	window->setChart(chart);
	window->setBlockSections(extents, referenceId);
	window->setTrainListColors(trainColorsForRun(run->results, m_host.sceneModel()));
	const QString note = "Shifted calculated envelopes, not recorded movement. "
						 "Unmapped/incomplete blocks omitted; no extrapolation. "
						 "Envelope colours: blue blocks/stations, yellow switches, red conflicts, amber capacity-critical touching blocks."
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
	connect(window, &DiagramWindow::trainSelected, this, &ResultViews::trainSelected);
	window->setTimeAxisY(true, run->settings.startOffsetSeconds);
	window->setAttribute(Qt::WA_DeleteOnClose);
	window->show();
}
