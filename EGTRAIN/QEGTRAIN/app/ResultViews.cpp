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
#include "diagrams/CapacityWindow.h"
#include "diagrams/DelayAnalysisWindow.h"
#include "diagrams/DiagramWindow.h"
#include "diagrams/PositionScale.h"
#include "diagrams/ResultFrame.h"
#include "diagrams/RouteDiagramCoordinates.h"
#include "diagrams/RouteReferenceChoice.h"
#include "diagrams/RunExports.h"
#include "diagrams/RunResults.h"
#include "diagrams/TimetableTableWindow.h"
#include "diagrams/TrainColors.h"
#include "util/TimeFormat.h"
#include "util/TrajectoryUtil.h"
#include "widgets/UserPrompts.h"
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QCategoryAxis>
#include <QtCharts/QChart>
#include <QtCharts/QLegendMarker>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

void attachRunProvenance(DiagramWindow* window, RunProvenance provenance) {
	window->setProvenanceWriter([provenance = std::move(provenance)](
									const QString& path, const char* kind, const std::string& bytes) {
		return writeRunArtifactWithProvenance(path.toStdString(), kind, bytes, provenance);
	});
}

// The layer of the outlines of the blocks.
constexpr char kBlockingTimeLayer[] = "blocking time";

// The reference route in the header of a route chart: "route0", with ", right to left" when the route runs that way.
QString referenceRouteText(const std::string& routeId, bool leftward) {
	return QString::fromStdString(routeId) + (leftward ? QStringLiteral(", right to left") : QString());
}

// Asks setChart for the range of the axis as it is, with round ticks inside it.
void keepExactRange(QValueAxis* axis) {
	axis->setProperty("exactRange", true);
}

// The axes of a chart along a reference route: the distance along the bottom, spanning the route exactly, and the
// stations above the plot, both attached to every series. Gives the station axis and its ticks for
// DiagramWindow::setStationAxis, or null when the route has no station. Each of `callPositions`, the route distances of
// the calls that the chart shows, counts as a call at the station nearest to it.
QCategoryAxis* addRouteAxes(QChart* chart, const RouteDiagramPath& referencePath, std::vector<StationTick>& stationTicks,
	const std::vector<double>& callPositions) {
	if (chart->axes(Qt::Horizontal).isEmpty()) chart->addAxis(new QValueAxis(), Qt::AlignBottom);
	auto* distanceAxis = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
	distanceAxis->setTitleText(QStringLiteral("Distance (km)"));
	// The range of the route exactly, with round ticks inside it.
	const double origin = referencePath.nodes.front().positionKm;
	const double end = referencePath.nodes.back().positionKm;
	const double low = std::min(origin, end), high = std::max(origin, end);
	distanceAxis->setRange(low, std::max(low + 0.02, high));
	keepExactRange(distanceAxis);
	for (auto* series : chart->series())
		if (!series->attachedAxes().contains(distanceAxis)) series->attachAxis(distanceAxis);
	// Station names come from platform-bound canonical IDs, not from map X.
	const auto stationLabels = routeDiagramStationLabels(referencePath);
	if (stationLabels.empty()) return nullptr;
	// The station names say what the axis is, so it has no title.
	auto* stationAxis = new QCategoryAxis();
	stationAxis->setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
	stationAxis->setStartValue(distanceAxis->min() - 1);
	stationAxis->setRange(distanceAxis->min(), distanceAxis->max());
	chart->addAxis(stationAxis, Qt::AlignTop);
	for (auto* series : chart->series()) series->attachAxis(stationAxis);
	for (const auto& station : stationLabels) {
		const QString name = QString::fromStdString(station.second);
		stationTicks.push_back({station.first, {name + QString(" (%1 km)").arg(station.first, 0, 'f', 2), name}});
	}
	for (const double call : callPositions) {
		auto nearest = std::lower_bound(stationTicks.begin(), stationTicks.end(), call,
			[](const StationTick& tick, double position) { return tick.position < position; });
		if (nearest == stationTicks.end() || (nearest != stationTicks.begin() && call - std::prev(nearest)->position < nearest->position - call))
			nearest = std::prev(nearest);
		++nearest->calls;
	}
	return stationAxis;
}

void appendInspectedPoint(QLineSeries* series, double x, double y, const QString& context) {
	series->append(x, y);
	QStringList points = series->property("inspectionPoints").toStringList();
	points.append(context);
	series->setProperty("inspectionPoints", points);
}

// "Approach from 10 s", "Run 12 to 30 s", "Clearance to 33 s", "Setup 2 s", "Sight and reaction 9 s", "Release 1 s" and
// "Run margin 0 s" for the hover of a block, one per line, without the parts that are not available. In a compressed
// chart, whose blocks move and whose times do not, each time starts with "Simulated".
QStringList blockingComponentLines(const BlockingTimeDiagramSegment& segment, bool compressed) {
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
	QStringList lines;
	for (const QString& time : times) lines << (compressed ? "Simulated " + time : time);
	const std::pair<double, const char*> parts[] = {{segment.setupTime, "setup"}, {segment.sightReactionTime, "sight and reaction"},
		{segment.releaseTime, "release"}, {segment.runTimeMargin, "run margin"}};
	for (const auto& part : parts)
		if (known(part.first)) lines << QString("%1 %2 s").arg(part.second, seconds(part.first));
	for (QString& line : lines) line[0] = line[0].toUpper();
	return lines;
}

void addBlockingTimeSeries(QChart* chart, const std::vector<BlockingTimeDiagramSegment>& segments,
	bool useCapacityCriticalStyle, const SceneSectionLabels& labels, const PositionScale& scale) {
	// The kind of a block, which gives its look: a critical block of a compressed chart, a conflict, or a blocking time.
	// A station or switch block looks like any other; its hover names its type.
	const auto kindFor = [useCapacityCriticalStyle](const BlockingTimeDiagramSegment& segment) {
		if (useCapacityCriticalStyle && segment.capacityCritical) return BlockingTimeKind::CriticalBlock;
		if (segment.style == BlockingTimeSegmentStyle::Critical || segment.style == BlockingTimeSegmentStyle::CriticalStation)
			return BlockingTimeKind::Conflict;
		return BlockingTimeKind::Ordinary;
	};
	// The type of a block that is not an ordinary one, which its hover names.
	const auto typeFor = [useCapacityCriticalStyle](const BlockingTimeDiagramSegment& segment) {
		if (useCapacityCriticalStyle && segment.capacityCritical)
			return std::string("capacity critical block");
		if (segment.style == BlockingTimeSegmentStyle::Default)
			return std::string();
		return std::string(blockingSegmentTypeName(segment.style));
	};
	// The same type in the hover, such as "Conflict at a station"; empty for an ordinary block.
	const auto kindText = [useCapacityCriticalStyle](const BlockingTimeDiagramSegment& segment) {
		if (useCapacityCriticalStyle && segment.capacityCritical) return QStringLiteral("Critical block");
		switch (segment.style) {
			case BlockingTimeSegmentStyle::Station: return QStringLiteral("Station block");
			case BlockingTimeSegmentStyle::Switch: return QStringLiteral("Switch block");
			case BlockingTimeSegmentStyle::SwitchStation: return QStringLiteral("Switch block at a station");
			case BlockingTimeSegmentStyle::Critical: return QStringLiteral("Conflict");
			case BlockingTimeSegmentStyle::CriticalStation: return QStringLiteral("Conflict at a station");
			case BlockingTimeSegmentStyle::Default: break;
		}
		return QString();
	};
	std::map<std::string, bool> legendEntries;
	for (const BlockingTimeDiagramSegment& segment : segments) {
		const double left = std::min(segment.startPositionKm, segment.endPositionKm);
		const double right = std::max(segment.startPositionKm, segment.endPositionKm);
		const std::string type = typeFor(segment);
		const std::string legendKey = segment.trainName + "\n" + type;
		const QString name = QString::fromStdString(segment.trainName);
		QAreaSeries* fill = nullptr;
		if (left != right) {
			auto* lower = new QLineSeries();
			auto* upper = new QLineSeries();
			lower->append(left, segment.startTime);
			lower->append(right, segment.startTime);
			upper->append(left, segment.endTime);
			upper->append(right, segment.endTime);
			fill = new QAreaSeries(upper, lower);
			fill->setName(name);
			fill->setProperty("trainId", name);
		}
		auto* outline = new QLineSeries();
		outline->setName(name);
		outline->setProperty("trainId", name);
		outline->setProperty("layer", kBlockingTimeLayer);
		outline->setProperty("inspectionFilled", left != right);
		styleBlockingTime(fill, outline, kindFor(segment));
		if (fill) {
			chart->addSeries(fill);
			for (QLegendMarker* marker : chart->legend()->markers(fill)) marker->setVisible(false);
		}
		// The train on the block by its short id, as the section strip names it, then one fact per line: the kind, the label
		// of the block, where and when it was blocked, how that time is made up and the section of the reference route.
		const QString blockText = QString::fromStdString(shortBlockId(segment.blockId));
		outline->setProperty("inspectionTitle", name + " on " + blockText);
		QStringList lines;
		if (const QString kind = kindText(segment); !kind.isEmpty()) lines << kind;
		if (const QString label = QString::fromStdString(readableSectionLabel(segment.blockId, labels)); label != blockText) lines << label;
		// The geo X of the block's own ends, also where they stick out of the sections of the chart.
		if (!scale.empty())
			lines << QString("Position %1 to %2 km")
						 .arg(scale.geoAt(segment.startPositionKm, segment.endPositionKm), 0, 'f', 3)
						 .arg(scale.geoAt(segment.endPositionKm, segment.startPositionKm), 0, 'f', 3);
		lines << QString("Blocked %1 to %2 s").arg(QString::number(segment.startTime), QString::number(segment.endTime));
		if (std::abs(segment.originalStartTime - segment.startTime) > kBlockingTimeToleranceSeconds
			|| std::abs(segment.originalEndTime - segment.endTime) > kBlockingTimeToleranceSeconds)
			lines << QString(useCapacityCriticalStyle ? "Simulated %1 to %2 s" : "Whole %1 to %2 s")
						 .arg(QString::number(segment.originalStartTime), QString::number(segment.originalEndTime));
		lines << blockingComponentLines(segment, useCapacityCriticalStyle);
		if (!segment.referenceNote.empty()) lines << QString::fromStdString(segment.referenceNote).split('\n');
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
	// The window comes back in front once the save dialog and its messages close.
	const KeepInFront front(parent);
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

void ResultViews::addCaseWindow(QWidget* window) {
	m_caseWindows.removeAll(QPointer<QWidget>());
	m_caseWindows.append(window);
}

void ResultViews::closeAll() {
	for (const QPointer<QWidget>& window : m_windows + m_caseWindows)
		if (window)
			window->close();
	m_windows.clear();
	m_caseWindows.clear();
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
	win->setPresentation(heading, completedRunContext(run->provenance));
	win->setProperty("manualSection", QStringLiteral("train-diagram"));
	win->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	win->setChart(chart);
	win->setTrainColors(trainColorsForRun(run->results, m_host.sceneModel()));
	win->setTrainServices(trainServicesForRun(run->results));
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
	const int reference = chooseRouteDiagramReference(*run, "delays");
	if (reference < 0) return;
	const std::optional<DelayDiagramData> diagram = delayDiagramData(*run, reference);
	if (!diagram) return;
	const RouteDiagramPath& referencePath = diagram->referencePath;
	QChart* chart = new QChart();
	const QString heading = ResultViewName::kDelays;

	std::vector<double> calls;
	for (const DelayDiagramSeries& source : diagram->series) {
		for (const DelayDiagramPoint& point : source.points) calls.push_back(point.positionKm);
		if (source.points.empty()) continue;
		auto* series = new QLineSeries();
		const QString trainName = QString::fromStdString(source.trainId);
		series->setName(trainName);
		series->setProperty("trainId", trainName);
		// A train may call at two or three stations of the route, so each call is a marker on the line.
		series->setPointsVisible(true);
		for (const DelayDiagramPoint& point : source.points)
			appendInspectedPoint(series, point.positionKm, point.delaySeconds / 60.0,
				QString("%1, stop %2").arg(QString::fromStdString(point.station)).arg(point.journey));
		chart->addSeries(series);
	}
	chart->createDefaultAxes();
	std::vector<StationTick> stationTicks;
	QCategoryAxis* stationAxis = addRouteAxes(chart, referencePath, stationTicks, calls);
	// The readout names the station nearest the pointer.
	chart->axes(Qt::Horizontal).first()->setProperty("readoutStation", true);
	if (chart->axes(Qt::Vertical).isEmpty()) chart->addAxis(new QValueAxis(), Qt::AlignLeft);
	chart->axes(Qt::Vertical).first()->setTitleText("Arrival delay (min)");

	DiagramWindow* win = new DiagramWindow(heading, m_window);
	// The header names the run and then the reference route, which the distance axis follows.
	const bool leftward = referencePath.nodes.back().positionKm < referencePath.nodes.front().positionKm;
	win->setPresentation(heading, completedRunContext(run->provenance) + QStringLiteral(" \u00b7 ") + referenceRouteText(referencePath.id, leftward));
	win->setProperty("manualSection", QStringLiteral("timetable-results"));
	win->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	win->setChart(chart);
	if (stationAxis) win->setStationAxis(stationAxis, std::move(stationTicks));
	win->setTrainColors(trainColorsForRun(run->results, m_host.sceneModel()));
	win->setTrainServices(trainServicesForRun(run->results));
	win->setCsvProvider(csvProviderFrom(run, &timetableCsvFrom), "timetable.csv");
	win->setFigureFileName(QStringLiteral("delays.png"));
	// A run whose timetable has no planned arrivals, as Netherlands, has no arrival delay to draw, nor has a route whose
	// stations no train has one at.
	win->setEmptyText(QStringLiteral("No arrival delays at stations of %1").arg(QString::fromStdString(referencePath.id)));
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
	// The stops of the trains, so that the busiest stations are named first. A train path chart takes them from the
	// timetable graph of its reference route.
	const std::optional<RouteDiagramData> timetableGraph = timetable ? std::nullopt : routeDiagramData(*run, true, referenceIndex);
	std::vector<double> calls;
	for (const RouteDiagramSeries& source : (timetableGraph ? *timetableGraph : *diagram).series)
		if (source.kind == RouteDiagramSeries::Kind::Event)
			for (const RouteDiagramPoint& point : source.points) calls.push_back(point.positionKm);
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
			// The window paints the stops and dwells in the colour of the train; planned ones are dashed.
			series->setPen(QPen(QColor(20, 35, 45), 1.0, source.planned ? Qt::DashLine : Qt::SolidLine));
			const bool dwell = source.kind == RouteDiagramSeries::Kind::Dwell;
			series->setPointsVisible(!dwell);
			// In the Key a dwell is a kind of its own, and the points of an event are its stops.
			if (dwell) {
				series->setProperty("keyKind", QStringLiteral("Dwell"));
				series->setProperty("keyUpright", true);
			} else {
				series->setProperty("keyMarker", QStringLiteral("Stop"));
			}
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
	std::vector<StationTick> stationTicks;
	QCategoryAxis* stationAxis = addRouteAxes(chart, referencePath, stationTicks, calls);
	QValueAxis* axisY = chart->axes(Qt::Vertical).isEmpty() ? new QValueAxis() : qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first());
	if (chart->axes(Qt::Vertical).isEmpty()) chart->addAxis(axisY, Qt::AlignLeft);
	axisY->setTitleText("Time (s)");
	axisY->setReverse(true);
	axisY->setRange(0, std::max(1.0, run->settings.horizonSeconds()));
	for (auto* series : chart->series())
		if (!series->attachedAxes().contains(axisY)) series->attachAxis(axisY);
	// Include elapsed zero explicitly, even when clock labels start at a nonzero offset.
	DiagramWindow* win = new DiagramWindow(heading, m_window);
	// The header names the run and then the reference route, which the distance axis follows.
	win->setPresentation(heading, completedRunContext(run->provenance) + QStringLiteral(" \u00b7 ") + referenceRouteText(referencePath.id, end < origin));
	win->setProperty("manualSection", QStringLiteral("train-paths"));
	win->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	win->setChart(chart);
	if (stationAxis) win->setStationAxis(stationAxis, std::move(stationTicks));
	win->setTrainColors(trainColorsForRun(run->results, m_host.sceneModel()));
	win->setTrainServices(trainServicesForRun(run->results));
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
	const PositionScale scale(extents);

	addBlockingTimeSeries(chart, segments, compressed, run->sectionLabels, scale);

	// The window paints the planned and the simulated lines in the colour of the train: planned dashed, simulated solid.
	std::set<std::string> plannedTrains;
	for (const auto& group : plottedPlanned) {
		const std::string& trainName = group.front().trainName;
		const bool first = plannedTrains.insert(trainName).second;
		auto* series = new QLineSeries();
		series->setName(QString::fromStdString(trainName));
		series->setProperty("trainId", QString::fromStdString(trainName));
		series->setProperty("planned", true);
		series->setPen(QPen(QColor(20, 35, 45), 2.0, Qt::DashLine));
		series->setPointsVisible(true);
		for (const auto& reference : group)
			appendInspectedPoint(series, reference.positionKm, reference.time,
				reference.stationName.empty()
					? "Planned time at the section edge"
					: QString("%1, planned %2").arg(QString::fromStdString(reference.stationName), QString::fromStdString(reference.eventType)));
		chart->addSeries(series);
		if (!first)
			for (QLegendMarker* marker : chart->legend()->markers(series)) marker->setVisible(false);
	}

	for (const BlockingTimeTrajectory& trajectory : diagram.trajectories) {
		auto* line = new QLineSeries();
		line->setName(QString::fromStdString(trajectory.trainId));
		line->setProperty("trainId", QString::fromStdString(trajectory.trainId));
		line->setProperty("layer", "simulated");
		if (compressed) line->setProperty("inspectionInterval", "Shifted with the train's blocking times");
		line->setPen(QPen(QColor(20, 35, 45), 2.0));
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
	// The plot keeps the route distance, exactly over the sections of the scope; the position axis takes the place
	// of this axis on screen.
	axisX->setTitleText(QStringLiteral("Distance (km)"));
	const auto axisRange = blockSectionAxisRange(extents, referencePath);
	axisX->setRange(axisRange.first, axisRange.second);
	keepExactRange(axisX);

	axisY->setTitleText("Time (s)");
	axisY->setReverse(true);
	axisY->setRange(diagram.timeRange.first, diagram.timeRange.second);

	DiagramWindow* win = new DiagramWindow(title, m_window);
	win->setProperty("manualSection", compressed ? QStringLiteral("capacity") : QStringLiteral("blocking-time"));
	win->setTelemetryCapture([this] { return m_host.captureTelemetryOperation(); });
	win->setChart(chart);
	// The strip marks the stations of the reference route and the critical and conflicting blocks of the chart.
	std::vector<double> stationKm;
	for (const RouteDiagramNode& node : referencePath.nodes)
		if (!node.stationId.empty()) stationKm.push_back(node.positionKm);
	win->setBlockSections(extents, QString::fromStdString(referencePath.id), &run->sectionLabels,
		blockSectionMarks(extents, segments, stationKm, compressed), diagram.routeSectionCount);
	win->setPositionAxis(scale);
	// The outlines and lines take the colours of the trains; critical blocks and conflicts keep their own. A repeated
	// train is a copy of its train, which gives it the colour of that train, and it has the service of its train; a train
	// without a service is a service of its own.
	QHash<QString, QString> services = trainServicesForRun(run->results);
	QHash<QString, QString> copies;
	for (const auto& [repeat, source] : diagram.repeatedTrains) {
		const QString repeatId = QString::fromStdString(repeat);
		const QString sourceId = QString::fromStdString(source);
		copies.insert(repeatId, sourceId);
		services.insert(sourceId, services.value(sourceId, sourceId));
		services.insert(repeatId, services.value(sourceId));
	}
	win->setTrainCopies(copies);
	win->setTrainColors(trainColorsForRun(run->results, m_host.sceneModel()));
	win->setTrainServices(services);
	const bool nothingPlotted = segments.empty() && plottedPlanned.empty() && trajectoryRows.empty();
	// The header names the run before the scope of the chart.
	win->setPresentation(title, completedRunContext(run->provenance) + QStringLiteral(" \u00b7 ") + context,
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

	// A scripted run has nobody to close the message, so it shows none.
	const auto showCapacityMessage = [this](const QString& message) {
		UserPrompts([this] { return m_host.dialogsSuppressed(); }).showInfo(m_window, ResultViewName::kCapacity, message);
	};
	const std::shared_ptr<const CompletedRunData> run = m_runController->completedRun();
	CapacityAnalysisScope scope = firstCapacityScopeWithPairFrom(*run);
	if (scope.routeIndex < 0) {
		showCapacityMessage("No route has complete blocking times for two or more trains. Run a case with signalling data.");
		return;
	}
	if ((!m_host.dialogsSuppressed() || mainWindowTestHooks().askCapacityScope)
		&& !chooseCapacityAnalysisScope(m_window, *run, scope))
		return;
	if (scope.routeIndex < 0 || scope.blockIds.empty()) {
		showCapacityMessage("The chosen route has no blocks. Choose another route.");
		return;
	}
	const std::string scopeIssue = capacityScopeIssueFrom(*run, scope);
	if (!scopeIssue.empty()) {
		showCapacityMessage(QString::fromStdString(scopeIssue));
		return;
	}
	if (capacityInputsFrom(*run, scope).size() < 2) {
		showCapacityMessage("Fewer than two trains use the chosen blocks. Choose other blocks or trains.");
		return;
	}
	// The export names the blocks by their ids, the window by their labels.
	const QString sectionLabel = capacityScopeLabelFrom(*run, scope);
	const QString sectionText = capacityScopeTextFrom(*run, scope);
	const CapacityAnalysisResult result = capacityAnalysisFrom(*run, scope);
	if (!result.analyzable) {
		showCapacityMessage("These trains still overlap after compression. Choose other trains or another order.");
		return;
	}
	if (result.cycleEndIdentity.empty()) {
		showCapacityMessage("The cycle-closing train must be a checked row below the first.");
		return;
	}
	if (result.cycleTime < 0.0) {
		showCapacityMessage(
			QString("%1 shares no block with an earlier train, so it closes no cycle. Choose another cycle-closing train.")
				.arg(QString::fromStdString(result.cycleEndIdentity)));
		return;
	}

	const RunProvenance provenance = run->provenance;
	const QString sectionTip = capacityScopeBlocksText(*run, scope);
	// The header names the run and the section; the window title names the view.
	auto* dialog = new CapacityWindow(result, completedRunContext(provenance) + QStringLiteral(" \u00b7 ") + sectionText, sectionTip,
		run->sectionLabels, m_window);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	// The file dialog and its messages belong to the Capacity window, so the focus returns to it.
	connect(dialog->exportButton(), &QPushButton::clicked, dialog, [this, dialog, result, sectionLabel, provenance]() {
		const auto operation = m_host.captureTelemetryOperation();
		saveCsvInteractive(
			dialog, "capacity_analysis.csv", operation,
			[result, sectionLabel] { return capacityAnalysisCsv(result, sectionLabel); },
			[provenance](const QString& path, const std::string& bytes) {
				return writeRunArtifactWithProvenance(path.toStdString(), "csv", bytes, provenance);
			});
	});
	connect(dialog->diagramButton(), &QPushButton::clicked, dialog, [this, run, scope, result, sectionText, sectionTip]() {
		const std::optional<BlockingTimeDiagramData> diagram = compressedBlockingTimeDiagramData(*run, scope, result);
		if (!diagram)
			return;
		// The header has the key figures of the Capacity window; the tooltip names the trains of the cycle.
		showBlockingTimeWindow(run, *diagram, true, sectionText + QStringLiteral(" \u00b7 ") + capacityCycleText(result),
			QStringList({QString("Cycle from %1 to %2").arg(QString::fromStdString(result.firstIdentity), QString::fromStdString(result.cycleEndIdentity)),
							sectionTip})
				.join('\n')
				.trimmed());
	});
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
