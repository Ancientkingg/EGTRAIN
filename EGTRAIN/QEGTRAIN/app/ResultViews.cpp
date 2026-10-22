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
#include "diagrams/DelayAnalysisWindow.h"
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
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
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

// The layer of the outlines of the blocks; the legend shows the colour key in its place.
constexpr char kBlockingTimeLayer[] = "blocking time";
const QColor kBlockingBlockColor(100, 160, 240, 150);
const QColor kBlockingSwitchColor(240, 210, 40, 180);
const QColor kBlockingCriticalColor(220, 50, 50, 200);
const QColor kBlockingCapacityColor(235, 175, 20, 230);
constexpr int kBlockingFillAlpha = 65; // the fill of a block; its outline is its colour, darker

// The colour key entry of a block colour, a box drawn as the chart draws the block.
ColourKeyEntry blockingKeyEntry(const char* name, const QColor& color) {
	QColor fill = color;
	fill.setAlpha(kBlockingFillAlpha);
	return {name, fill, color.darker(135)};
}

// "Distance along route0 (km)", with ", right to left" when the route runs that way. The chart reads axis titles as rich
// text, so the route id is escaped.
QString distanceAxisTitle(const std::string& routeId, bool leftward) {
	return QString("Distance along %1 (km)%2").arg(QString::fromStdString(routeId).toHtmlEscaped(), leftward ? ", right to left" : "");
}

void appendInspectedPoint(QLineSeries* series, double x, double y, const QString& context) {
	series->append(x, y);
	QStringList points = series->property("inspectionPoints").toStringList();
	points.append(context);
	series->setProperty("inspectionPoints", points);
}

// "Approach from 10 s, run 12 to 30 s, clearance to 33 s" and "Setup 2 s, sight and reaction 9 s, release 1 s, run
// margin 0 s" for the hover of a block, without the parts that are not available. The times line starts with
// timesPrefix, for the blocks of a compressed chart whose times are not moved with the block.
QStringList blockingComponentLines(const BlockingTimeDiagramSegment& segment, const QString& timesPrefix) {
	const auto known = [](double value) { return value >= 0; };
	const auto seconds = [](double value) { return QString::number(value); };
	QStringList times;
	if (known(segment.startApproachTime)) times << "approach from " + seconds(segment.startApproachTime) + " s";
	if (known(segment.startRunTime) && known(segment.endRunTime))
		times << QString("run %1 to %2 s").arg(seconds(segment.startRunTime), seconds(segment.endRunTime));
	else if (known(segment.startRunTime))
		times << "run from " + seconds(segment.startRunTime) + " s";
	else if (known(segment.endRunTime))
		times << "run to " + seconds(segment.endRunTime) + " s";
	if (known(segment.endClearTime)) times << "clearance to " + seconds(segment.endClearTime) + " s";
	QStringList durations;
	const std::pair<double, const char*> parts[] = {{segment.setupTime, "setup"}, {segment.sightReactionTime, "sight and reaction"},
		{segment.releaseTime, "release"}, {segment.runTimeMargin, "run margin"}};
	for (const auto& part : parts)
		if (known(part.first)) durations << QString("%1 %2 s").arg(part.second, seconds(part.first));
	QStringList lines;
	if (!times.isEmpty()) lines << timesPrefix + times.join(", ");
	if (!durations.isEmpty()) lines << durations.join(", ");
	for (QString& line : lines) line[0] = line[0].toUpper();
	return lines;
}

void addBlockingTimeSeries(QChart* chart, const std::vector<BlockingTimeDiagramSegment>& segments,
	bool useCapacityCriticalStyle, const SceneSectionLabels& labels) {
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
			default: return kBlockingBlockColor;
		}
	};
	// The type of a block that is not an ordinary one, as the colour shows it.
	const auto typeFor = [useCapacityCriticalStyle](const BlockingTimeDiagramSegment& segment) {
		if (useCapacityCriticalStyle && segment.capacityCritical)
			return std::string("capacity critical block");
		if (segment.style == BlockingTimeSegmentStyle::Default)
			return std::string();
		return std::string(blockingSegmentTypeName(segment.style));
	};
	std::map<std::string, bool> legendEntries;
	for (const BlockingTimeDiagramSegment& segment : segments) {
		const double left = std::min(segment.startPositionKm, segment.endPositionKm);
		const double right = std::max(segment.startPositionKm, segment.endPositionKm);
		const std::string type = typeFor(segment);
		const std::string legendKey = segment.trainName + "\n" + type;
		const QString name = QString::fromStdString(segment.trainName);
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
			fill->setProperty("trainId", name);
			QColor translucent = color;
			translucent.setAlpha(kBlockingFillAlpha);
			fill->setBrush(translucent);
			fill->setPen(Qt::NoPen);
			chart->addSeries(fill);
			for (QLegendMarker* marker : chart->legend()->markers(fill)) marker->setVisible(false);
		}
		auto* outline = new QLineSeries();
		outline->setName(name);
		outline->setProperty("trainId", name);
		outline->setProperty("layer", kBlockingTimeLayer);
		outline->setProperty("inspectionFilled", left != right);
		QPen pen(color.darker(135));
		pen.setWidthF(2.0);
		outline->setPen(pen);
		// The block, when it was blocked, how that time is made up and the section of the reference route, one per line.
		QStringList lines = {"Block: " + QString::fromStdString(labels.labelWithId(segment.blockId))
			+ (type.empty() ? QString() : ", " + QString::fromStdString(type))};
		QString blocked = QString("Blocked %1 to %2 s").arg(QString::number(segment.startTime), QString::number(segment.endTime));
		if (std::abs(segment.originalStartTime - segment.startTime) > kBlockingTimeToleranceSeconds
			|| std::abs(segment.originalEndTime - segment.endTime) > kBlockingTimeToleranceSeconds)
			blocked += QString(useCapacityCriticalStyle ? " (as simulated: %1 to %2 s)" : " (whole: %1 to %2 s)")
						   .arg(QString::number(segment.originalStartTime), QString::number(segment.originalEndTime));
		lines << blocked << blockingComponentLines(segment, useCapacityCriticalStyle ? QStringLiteral("as simulated: ") : QString());
		if (!segment.referenceNote.empty()) lines << QString::fromStdString(segment.referenceNote);
		outline->setProperty("inspectionInterval", lines.join('\n'));
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
		QMessageBox::warning(parent, "Export failed", path_message::writeFailed("save the data to", path));
	}
}

ResultViews::ResultViews(QWidget* window, RunController* runController, Host& host)
	: QObject(window), m_window(window), m_runController(runController), m_host(host) {}

void ResultViews::showWindow(QWidget* window) {
	m_windows.removeAll(QPointer<QWidget>());
	if (!m_windows.isEmpty()) {
		const QWidget* last = m_windows.last();
		const QRect available = last->screen()->availableGeometry();
		QRect frame(last->pos() + QPoint(32, 32), window->size() + last->frameSize() - last->size());
		if (!available.contains(frame))
			frame.moveTopLeft(available.topLeft());
		window->move(frame.topLeft());
	}
	window->show();
	m_windows.append(window);
}

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
	win->setTrainColors(trainColorsForRun(run->results, m_host.sceneModel()));
	win->setCsvProvider(csvProviderFrom(run, &trajectoryCsvFrom), "trajectory.csv");
	attachRunProvenance(win, run->provenance);
	connect(win, &DiagramWindow::trainSelected, this, &ResultViews::trainSelected);
	win->setTimeAxisX(mode == 1 || mode == 2, run->settings.startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	showWindow(win);
}

void ResultViews::showDelayAnalysis() {
	if (QWidget* window = m_runController->openDelayAnalysis())
		showWindow(window);
}

void ResultViews::showDelayComparison() {
	if (DelayAnalysisWindow* window = m_runController->openDelayAnalysis()) {
		showWindow(window);
		window->showBaselineTab();
	}
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
	window->setAttribute(Qt::WA_DeleteOnClose);
	showWindow(window);
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
		series->setName(trainName);
		series->setProperty("trainId", trainName);
		bool hasData = false;
		for (const TimetableResultRow& row : run->timetable) {
			if (row.trainId != train.trainId || !row.arrivalDelaySeconds.available)
				continue;
			appendInspectedPoint(series, row.journeyIndex, row.arrivalDelaySeconds.value / 60.0,
				QString("Station: %1").arg(QString::fromStdString(row.stationId)));
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
		chart->axes(Qt::Horizontal).first()->setTitleText("Stop number");
	}
	if (!chart->axes(Qt::Vertical).isEmpty()) {
		chart->axes(Qt::Vertical).first()->setTitleText("Arrival delay (min)");
	}

	DiagramWindow* win = new DiagramWindow(heading, m_window);
	win->setProperty("manualSection", QStringLiteral("timetable-results"));
	win->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	win->setChart(chart);
	win->setTrainColors(trainColorsForRun(run->results, m_host.sceneModel()));
	win->setCsvProvider(csvProviderFrom(run, &timetableCsvFrom), "timetable.csv");
	attachRunProvenance(win, run->provenance);
	connect(win, &DiagramWindow::trainSelected, this, &ResultViews::trainSelected);
	win->setTimeAxisX(false, run->settings.startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	showWindow(win);
}

void ResultViews::showTimetableGraph() {
	if (!m_runController->hasRunResults()) return;
	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	const int reference = chooseRouteDiagramReference(*run, "timetable graph");
	if (reference >= 0) buildRouteDiagram(run, true, reference);
}

void ResultViews::buildRouteDiagram(const std::shared_ptr<const CompletedRunData>& run, bool timetable, int referenceIndex) {
	const std::optional<RouteDiagramData> diagram = routeDiagramData(*run, timetable, referenceIndex);
	if (!diagram) return;
	const RouteDiagramPath& referencePath = diagram->referencePath;
	const double origin = referencePath.nodes.front().positionKm;
	const double end = referencePath.nodes.back().positionKm;
	const QString heading = timetable ? ResultViewName::kTimetableGraph : ResultViewName::kTrainPaths;
	auto* chart = new QChart();
	chart->setTitle(QString("%1 [%2]").arg(heading, completedRunContext(run->provenance)));
	for (const RouteDiagramSeries& source : diagram->series) {
		const QString name = QString::fromStdString(source.trainId);
		auto* series = new QLineSeries();
		series->setProperty("trainId", name);
		if (source.kind == RouteDiagramSeries::Kind::Trajectory) {
			series->setName(name);
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
					? QString("%1, %2 dwell").arg(station, source.planned ? "planned" : "simulated")
					: QString("%1, stop %2, %3").arg(station).arg(point.journey).arg(event);
				appendInspectedPoint(series, point.positionKm, point.seconds, context);
			}
		}
		chart->addSeries(series);
	}
	chart->createDefaultAxes();
	if (chart->axes(Qt::Horizontal).isEmpty()) chart->addAxis(new QValueAxis(), Qt::AlignBottom);
	if (!chart->axes(Qt::Horizontal).isEmpty()) {
		auto* axis = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
		axis->setTitleText(distanceAxisTitle(referencePath.id, end < origin));
		const double low = std::min(origin, end), high = std::max(origin, end);
		axis->setRange(low - 0.01, std::max(low + 0.02, high + 0.01));
	}
	QValueAxis* axisY = chart->axes(Qt::Vertical).isEmpty() ? new QValueAxis() : qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first());
	if (chart->axes(Qt::Vertical).isEmpty()) chart->addAxis(axisY, Qt::AlignLeft);
	axisY->setTitleText("Time (s)");
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
		stationAxis->setTitleText("Stations");
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
	showWindow(win);
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
	// The route and blocks as the Capacity window names its section: "route0 (A), blocks 36 to 72".
	CapacityAnalysisScope blocks;
	blocks.routeIndex = scope.routeIndex;
	blocks.blockIds = scope.blockIds;
	blocks.sectionIndices = scope.sectionIndices;
	const QString route = run->hasRoute(scope.routeIndex) ? capacityScopeTextFrom(*run, blocks) : QStringLiteral("All routes");
	showBlockingTimeWindow(run, *diagram, false,
		QString("%1, %2 to %3")
			.arg(route, QString::fromStdString(formatSimTime(static_cast<long long>(scope.startTime), run->settings.startOffsetSeconds)),
				QString::fromStdString(formatSimTime(static_cast<long long>(scope.endTime), run->settings.startOffsetSeconds))),
		capacityScopeBlocksText(*run, blocks));
}

void ResultViews::showBlockingTimeWindow(const std::shared_ptr<const CompletedRunData>& run,
	const BlockingTimeDiagramData& diagram, bool compressed, const QString& context, const QString& contextToolTip) {
	const QString title = compressed ? QStringLiteral("Compressed blocking time") : QString(ResultViewName::kBlockingTime);
	const RouteDiagramPath& referencePath = diagram.referencePath;
	const std::vector<BlockingTimeDiagramSegment>& segments = diagram.segments;
	const std::vector<BlockingTimeDiagramSegment>& unmapped = diagram.unmapped;
	const std::vector<BlockSectionExtent>& extents = diagram.extents;
	const std::vector<BlockingTimePlannedReference>& plannedReferences = diagram.plannedReferences;
	const auto& plottedPlanned = diagram.plannedLines;
	const auto& trajectoryRows = diagram.trajectoryRows;

	QChart* chart = new QChart();
	chart->setTitle(QString("%1 [%2]").arg(title, completedRunContext(run->provenance)));

	addBlockingTimeSeries(chart, segments, compressed, run->sectionLabels);

	const QColor plannedColors[] = {
		QColor(36, 117, 181), QColor(205, 92, 92), QColor(46, 139, 87),
		QColor(138, 43, 226), QColor(210, 105, 30), QColor(0, 128, 128)};
	std::map<std::string, int> plannedColorsByTrain;
	for (const auto& group : plottedPlanned) {
		const std::string& trainName = group.front().trainName;
		const auto color = plannedColorsByTrain.emplace(trainName, static_cast<int>(plannedColorsByTrain.size()));
		auto* series = new QLineSeries();
		series->setName(QString::fromStdString(trainName));
		series->setProperty("trainId", QString::fromStdString(trainName));
		series->setProperty("planned", true);
		QPen pen(plannedColors[color.first->second % (sizeof(plannedColors) / sizeof(plannedColors[0]))]);
		pen.setStyle(Qt::DashLine);
		pen.setWidthF(2.5);
		series->setPen(pen);
		series->setPointsVisible(true);
		for (const auto& reference : group)
			appendInspectedPoint(series, reference.positionKm, reference.time,
				reference.stationName.empty()
					? "Planned time at the section edge"
					: QString("%1, planned %2").arg(QString::fromStdString(reference.stationName), QString::fromStdString(reference.eventType)));
		chart->addSeries(series);
		if (!color.second)
			for (QLegendMarker* marker : chart->legend()->markers(series)) marker->setVisible(false);
	}

	for (const BlockingTimeTrajectory& trajectory : diagram.trajectories) {
		auto* line = new QLineSeries();
		line->setName(QString::fromStdString(trajectory.trainId));
		line->setProperty("trainId", QString::fromStdString(trajectory.trainId));
		line->setProperty("layer", "simulated");
		if (compressed) line->setProperty("inspectionInterval", "Shifted with the train's blocking times");
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

	axisX->setTitleText(distanceAxisTitle(referencePath.id, end < origin));
	const auto axisRange = blockSectionAxisRange(extents, referencePath);
	axisX->setRange(axisRange.first, axisRange.second);

	axisY->setTitleText("Time (s)");
	axisY->setReverse(true);
	axisY->setRange(diagram.timeRange.first, diagram.timeRange.second);

	DiagramWindow* win = new DiagramWindow(title, m_window);
	win->setProperty("manualSection", compressed ? QStringLiteral("capacity") : QStringLiteral("blocking-time"));
	win->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	win->setChart(chart);
	win->setBlockSections(extents, QString::fromStdString(referencePath.id), &run->sectionLabels);
	// The colours of the blocks show their type, so only the train list takes the service colours.
	win->setTrainListColors(trainColorsForRun(run->results, m_host.sceneModel()));
	// A compressed chart has no conflict, as its trains are moved apart until their blocks only touch.
	const ColourKeyEntry lastKey = compressed ? blockingKeyEntry("Capacity critical", kBlockingCapacityColor)
											  : blockingKeyEntry("Conflict", kBlockingCriticalColor);
	const ColourKeyEntry blockKey = blockingKeyEntry("Block or station", kBlockingBlockColor);
	win->setColourKey(kBlockingTimeLayer, {blockKey, blockingKeyEntry("Switch", kBlockingSwitchColor), lastKey});
	const bool nothingPlotted = segments.empty() && plottedPlanned.empty() && trajectoryRows.empty();
	win->setPresentation(title, context,
		QString::fromStdString(blockingTimeNote(diagram.omitted, nothingPlotted, !unmapped.empty(), referencePath.id)), contextToolTip);
	const std::function<std::string(const QStringList&)> scopedCsv =
		[segments, plannedReferences, trajectoryRows, extents, unmapped](const QStringList& visibleTrainIds) {
			return buildBlockingTimeCsv(visibleTrainIds, segments, plannedReferences, trajectoryRows, extents, unmapped);
		};
	win->setCsvProvider(scopedCsv, compressed ? "capacity_compressed_blocking_time.csv" : "blocking_time.csv",
		blockingTimeTrainIds(segments, plannedReferences, trajectoryRows, unmapped));
	attachRunProvenance(win, run->provenance);
	connect(win, &DiagramWindow::trainSelected, this, &ResultViews::trainSelected);
	win->setTimeAxisY(true, run->settings.startOffsetSeconds);
	win->setAttribute(Qt::WA_DeleteOnClose);
	showWindow(win);
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
			"No block has complete blocking times for two or more trains. Run a case with signalling data.");
		return;
	}
	if ((!m_host.dialogsSuppressed() || mainWindowTestHooks().askCapacityScope)
		&& !chooseCapacityAnalysisScope(m_window, *run, scope))
		return;
	if (scope.routeIndex < 0 || scope.blockIds.empty()) {
		QMessageBox::information(m_window, ResultViewName::kCapacity,
			"No block of the route is shared by the checked trains. Choose other trains.");
		return;
	}
	const std::string scopeIssue = capacityScopeIssueFrom(*run, scope);
	if (!scopeIssue.empty()) {
		QMessageBox::information(m_window, ResultViewName::kCapacity, QString::fromStdString(scopeIssue));
		return;
	}
	const auto trains = capacityInputsFrom(*run, scope);
	if (trains.size() < 2) {
		QMessageBox::information(m_window, ResultViewName::kCapacity,
			"Fewer than two trains share the chosen blocks. Choose other trains.");
		return;
	}
	// The export names the blocks by their ids, the window by their labels.
	const QString sectionLabel = capacityScopeLabelFrom(*run, scope);
	const QString sectionText = capacityScopeTextFrom(*run, scope);
	const CapacityAnalysisResult result = analyzeCapacity(trains, scope.periodSeconds,
		scope.cycleEndOccurrenceId);
	if (!result.analyzable) {
		QMessageBox::information(m_window, ResultViewName::kCapacity,
			"These trains do not form a chain: two neighbours share no block or still overlap after compression. "
			"Choose other trains or another order.");
		return;
	}
	if (result.cycleEndIdentity.empty() || result.cycleTime < 0.0) {
		QMessageBox::information(m_window, ResultViewName::kCapacity,
			"The cycle-closing train must be a checked row below the first.");
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
	// "Reference time: planned departure at Guingamp" for the tooltip of a cell that names a train; empty without one.
	const auto referenceTip = [&result](const std::string& identity) {
		const QString reference = capacityReferenceOf(result, identity);
		return reference.isEmpty() ? QString() : "Reference time: " + reference;
	};
	auto* detailsToggle = new QToolButton(body);
	detailsToggle->setObjectName("capacityDetailsToggle");
	detailsToggle->setText("Calculation details");
	detailsToggle->setCheckable(true);
	detailsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	detailsToggle->setArrowType(Qt::RightArrow);
	// The text size of the dialog; macOS gives a tool button a smaller font.
	detailsToggle->setFont(QApplication::font());
	auto* details = new QLabel(capacityDetailsText(result), body);
	details->setObjectName("resultContextText");
	QStringList references;
	for (const std::string& identity : {result.firstIdentity, result.cycleEndIdentity})
		if (const QString reference = capacityReferenceOf(result, identity); !reference.isEmpty())
			references << QString::fromStdString(identity) + ": " + reference;
	details->setToolTip(references.join('\n'));
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
	const auto addTable = [tabs](const QStringList& headers, int rows) {
		auto* table = new QTableWidget(rows, headers.size(), tabs);
		table->setHorizontalHeaderLabels(headers);
		table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		table->setSelectionBehavior(QAbstractItemView::SelectRows);
		table->setAlternatingRowColors(true);
		// Tab leaves the table for the buttons below it; the arrow keys move between the cells.
		table->setTabKeyNavigation(false);
		table->horizontalHeader()->setStretchLastSection(true);
		tabs->addTab(table, QString());
		return table;
	};
	// Numbers are aligned to the right, as in the other analysis tables. A cell that names blocks by their ids has
	// their labels as its tooltip.
	const auto cell = [](const QString& text, bool number, const QString& tooltip = QString()) {
		auto* item = new QTableWidgetItem(text);
		if (number)
			item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
		if (!tooltip.isEmpty())
			item->setToolTip(tooltip);
		return item;
	};
	const SceneSectionLabels& labels = run->sectionLabels;
	// A tab title with its count, which it leaves out when it is 0.
	const auto countedTitle = [](const QString& title, int count) {
		return count == 0 ? title : QString("%1 (%2)").arg(title).arg(count);
	};
	auto* pairTable = addTable({"Leader", "Follower", "Leader code", "Follower code", "Scheduled headway (s)",
								   "Minimum headway (s)", "Buffer (s)", "Governing block"},
		static_cast<int>(result.pairs.size()));
	for (int row = 0; row < pairTable->rowCount(); ++row) {
		const CapacityPairRow& pair = result.pairs[static_cast<std::size_t>(row)];
		const QStringList values = {QString::fromStdString(pair.leaderIdentity), QString::fromStdString(pair.followerIdentity),
			QString::fromStdString(pair.leaderOperatingCode), QString::fromStdString(pair.followerOperatingCode),
			QString::fromStdString(csv::formatDouble(pair.scheduledHeadway)), QString::fromStdString(csv::formatDouble(pair.minimumHeadway)),
			QString::fromStdString(csv::formatDouble(pair.buffer)), QString::fromStdString(capacityEvidenceBlocks(pair.governingEvidence))};
		const QStringList tips = {referenceTip(pair.leaderIdentity), referenceTip(pair.followerIdentity), QString(), QString(), QString(),
			QString(), QString(), QString::fromStdString(capacityEvidenceText(pair.governingEvidence, &labels))};
		for (int column = 0; column < values.size(); ++column)
			pairTable->setItem(row, column, cell(values[column], column >= 4 && column <= 6, tips[column]));
	}
	tabs->setTabText(0, countedTitle("Pairs", pairTable->rowCount()));

	auto* compressionTable = addTable({"Train", "Operating code", "Original reference (s)", "Scheduled reference (s)",
										  "Compressed reference (s)", "Shift (s)", "Governing predecessor"},
		static_cast<int>(result.compression.size()));
	for (int row = 0; row < compressionTable->rowCount(); ++row) {
		const CapacityCompressionRow& compressed = result.compression[static_cast<std::size_t>(row)];
		QStringList governing;
		QStringList governingTips;
		for (const CapacityCompressionEvidence& predecessor : compressed.governingPredecessors) {
			const QString start = QString("%1 (%2 s)").arg(QString::fromStdString(predecessor.predecessorIdentity), QString::fromStdString(csv::formatDouble(predecessor.minimumHeadway)));
			governing << start + ": " + QString::fromStdString(capacityEvidenceBlocks(predecessor.governingEvidence));
			governingTips << start + "\n" + QString::fromStdString(capacityEvidenceText(predecessor.governingEvidence, &labels));
		}
		const QStringList values = {QString::fromStdString(compressed.identity), QString::fromStdString(compressed.operatingCode),
			QString::fromStdString(csv::formatDouble(compressed.originalReference)),
			QString::fromStdString(csv::formatDouble(compressed.scheduledReference)),
			QString::fromStdString(csv::formatDouble(compressed.compressedReference)),
			QString::fromStdString(csv::formatDouble(compressed.shift)), governing.join(", ")};
		const QStringList tips = {referenceTip(compressed.identity), QString(), QString(), QString(), QString(), QString(), governingTips.join('\n')};
		for (int column = 0; column < values.size(); ++column)
			compressionTable->setItem(row, column, cell(values[column], column >= 2 && column <= 5, tips[column]));
	}
	tabs->setTabText(1, countedTitle("Compression", compressionTable->rowCount()));

	auto* criticalTable = addTable({"Leader", "Follower", "Leader block", "Follower block", "Gap (s)"},
		static_cast<int>(result.criticalBlocks.size()));
	for (int row = 0; row < criticalTable->rowCount(); ++row) {
		const CapacityCriticalBlock& critical = result.criticalBlocks[static_cast<std::size_t>(row)];
		const QStringList values = {QString::fromStdString(critical.leaderIdentity), QString::fromStdString(critical.followerIdentity),
			QString::fromStdString(critical.leaderBlockId), QString::fromStdString(critical.followerBlockId),
			QString::fromStdString(csv::formatDouble(critical.gap))};
		const QStringList tips = {QString(), QString(), QString::fromStdString(labels.label(critical.leaderBlockId)),
			QString::fromStdString(labels.label(critical.followerBlockId)), QString()};
		for (int column = 0; column < values.size(); ++column)
			criticalTable->setItem(row, column, cell(values[column], column == 4, tips[column]));
	}
	tabs->setTabText(2, countedTitle("Critical blocks", criticalTable->rowCount()));
	for (int index = 0; index < tabs->count(); ++index)
		qobject_cast<QTableWidget*>(tabs->widget(index))->resizeColumnsToContents();
	layout->addWidget(tabs, 1);

	auto* secondaryActions = new QHBoxLayout();
	QPushButton* exportButton = new QPushButton("Export CSV...", body);
	// The file dialog and its messages belong to the Capacity window, so the focus returns to it.
	connect(exportButton, &QPushButton::clicked, dialog, [this, dialog, result, sectionLabel, provenance]() {
		const auto operation = m_host.captureTelemetryOperation();
		saveCsvInteractive(
			dialog, "capacity_analysis.csv", operation,
			[result, sectionLabel] { return capacityAnalysisCsv(result, sectionLabel); },
			[provenance](const QString& path, const std::string& bytes) {
				return writeRunArtifactWithProvenance(path.toStdString(), "csv", bytes, provenance);
			});
	});
	QPushButton* diagramButton = new QPushButton("Open Compressed Blocking Time", body);
	const QString sectionTip = capacityScopeBlocksText(*run, scope);
	connect(diagramButton, &QPushButton::clicked, dialog, [this, run, scope, result, sectionText, sectionTip]() {
		const std::optional<BlockingTimeDiagramData> diagram = compressedBlockingTimeDiagramData(*run, scope, result);
		if (!diagram)
			return;
		showBlockingTimeWindow(run, *diagram, true,
			QString("%1, cycle %2 to %3")
				.arg(sectionText, QString::fromStdString(result.firstIdentity), QString::fromStdString(result.cycleEndIdentity)),
			sectionTip);
	});
	secondaryActions->addWidget(exportButton);
	secondaryActions->addWidget(diagramButton);
	secondaryActions->addStretch();
	layout->addLayout(secondaryActions);
	auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
	connect(closeButtons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
	// The window title names the view; the result leads the context.
	DialogLayout::install(*dialog, ResultViewName::kCapacity,
		QString("Cycle %1 s, %2% of the %3 s period\nSection: %4\nRun: %5")
			.arg(QString::number(result.cycleTime, 'f', 1), QString::number(result.cyclePercentage, 'f', 1),
				QString::fromStdString(csv::formatDouble(result.periodSeconds)), sectionText, completedRunContext(provenance)),
		body, closeButtons);
	if (auto* context = dialog->findChild<QLabel*>(QStringLiteral("dialogContext")))
		context->setToolTip(sectionTip);
	// Room for the tables, as Capacity by area has, and for every column of the widest one, up to the width limit:
	// while the window is fitted, each table asks for the size of all its cells, and the widgets above it measure again.
	const auto askForCells = [tabs](bool cells) {
		for (QTableWidget* table : tabs->findChildren<QTableWidget*>()) {
			table->setSizeAdjustPolicy(cells ? QAbstractScrollArea::AdjustToContents : QAbstractScrollArea::AdjustIgnored);
			for (QWidget* widget = table; widget && !widget->isWindow(); widget = widget->parentWidget())
				widget->updateGeometry();
		}
	};
	askForCells(true);
	dialog->resize(qMin(dialog->maximumWidth(), 960), qMin(dialog->maximumHeight(), 640));
	DialogLayout::fitWidthToContent(*dialog);
	askForCells(false);
	dialog->resize(dialog->width(), qMin(dialog->maximumHeight(), 640));
	// No button of the window is a default button, so Return presses none.
	for (QPushButton* button : dialog->findChildren<QPushButton*>())
		button->setAutoDefault(false);
	showWindow(dialog);
}

void ResultViews::showAreaCapacity() {
	if (!m_runController->hasRunResults()) {
		m_host.resultMessage("Run a simulation to open capacity by area", 5000);
		return;
	}
	// The options dialog reads the run until it closes, so this call holds it.
	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	if (QWidget* window = openAreaCapacityWindow(m_window, *run, [this] { return m_host.captureTelemetryOperation(); }))
		showWindow(window);
}
