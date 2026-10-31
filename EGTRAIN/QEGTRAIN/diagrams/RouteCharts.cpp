#include "diagrams/RouteCharts.h"
#include "diagrams/PositionScale.h"
#include "diagrams/SeriesMeta.h"
#include "diagrams/TrainColors.h"
#include "scene/SectionLabels.h"
#include "util/TrajectoryUtil.h"
#include <QPen>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QLegendMarker>
#include <QtCharts/QLineSeries>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace {

// The titles of the axes and whether time runs along the horizontal axis of each chart of the trajectories, in the order
// of TrajectoryChart.
struct TrajectoryChartSpec {
	const char* xTitle;
	const char* yTitle;
	bool timeAxis;
};
constexpr TrajectoryChartSpec kTrajectoryCharts[] = {
	{"Distance (km)", "Speed (km/h)", false},
	{"Time", "Speed (km/h)", true},
	{"Time", "Distance (km)", true},
	{"Distance (km)", "Simulated tractive effort (kN)", false},
};

// The layer of the outlines of the blocks.
constexpr char kBlockingTimeLayer[] = "blocking time";

// The reference route in the header of a route chart: "route0", with ", right to left" when the route runs that way.
QString referenceRouteText(const std::string& routeId, bool leftward) {
	return QString::fromStdString(routeId) + (leftward ? QStringLiteral(", right to left") : QString());
}

// Asks setChart for the range of the axis as it is, with round ticks inside it.
void keepExactRange(QValueAxis* axis) {
	AxisMeta meta = axisMeta(axis);
	meta.exactRange = true;
	describeAxis(axis, meta);
}

// The description of a series of a train.
SeriesMeta metaOfTrain(const QString& trainId) {
	SeriesMeta meta;
	meta.trainId = trainId;
	return meta;
}

void appendInspectedPoint(QLineSeries* series, SeriesMeta& meta, double x, double y, const QString& context) {
	series->append(x, y);
	meta.inspectionPoints.append(context);
}

} // namespace

TrajectoryChartResult buildTrajectoryChart(const CompletedRunData& run, TrajectoryChart kind) {
	const TrajectoryChartSpec& spec = kTrajectoryCharts[static_cast<int>(kind)];
	QChart* chart = new QChart();

	// one series per contiguous segment; legend distinguishes trains
	for (const CompletedTrain& train : run.trains) {
		const CompletedTrajectory& trajectory = train.trajectory;
		for (const TrajectorySegment& segment : trajectory.segments) {
			QLineSeries* series = new QLineSeries();
			series->setName(QString::fromStdString(train.trainId));
			describeSeries(series, metaOfTrain(QString::fromStdString(train.trainId)));
			for (int i = segment.first; i <= segment.last; i++) {
				double tSec = trajectoryTimeSeconds(i, run.settings.timestepSeconds);
				double dist = trajectory.at(trajectory.position, i).value_or(0.0) / 1000.0;
				double spd = trajectory.at(trajectory.speed, i).value_or(0.0) * 3.6; // m/s to km/h
				switch (kind) {
					case TrajectoryChart::SpeedDistance: series->append(dist, spd); break;
					case TrajectoryChart::SpeedTime: series->append(tSec, spd); break;
					case TrajectoryChart::TimeDistance: series->append(tSec, dist); break;
					case TrajectoryChart::TractiveEffortDistance:
						if (const auto effort = trajectory.at(trajectory.effort, i))
							series->append(dist, *effort / 1000.0);
						break;
				}
			}
			chart->addSeries(series);
		}
	}
	chart->createDefaultAxes();
	if (!chart->axes(Qt::Horizontal).isEmpty())
		chart->axes(Qt::Horizontal).first()->setTitleText(spec.xTitle);
	if (!chart->axes(Qt::Vertical).isEmpty())
		chart->axes(Qt::Vertical).first()->setTitleText(spec.yTitle);
	return {chart, spec.timeAxis};
}

RouteChart buildDelayChart(const DelayDiagramData& diagram) {
	const RouteDiagramPath& referencePath = diagram.referencePath;
	QChart* chart = new QChart();

	std::vector<double> calls;
	for (const DelayDiagramSeries& source : diagram.series) {
		for (const DelayDiagramPoint& point : source.points) calls.push_back(point.positionKm);
		if (source.points.empty()) continue;
		auto* series = new QLineSeries();
		const QString trainName = QString::fromStdString(source.trainId);
		series->setName(trainName);
		SeriesMeta meta = metaOfTrain(trainName);
		// A train may call at two or three stations of the route, so each call is a marker on the line.
		series->setPointsVisible(true);
		for (const DelayDiagramPoint& point : source.points)
			appendInspectedPoint(series, meta, point.positionKm, point.delaySeconds / 60.0,
				QString("%1, stop %2").arg(QString::fromStdString(point.station)).arg(point.journey));
		describeSeries(series, meta);
		chart->addSeries(series);
	}
	chart->createDefaultAxes();
	std::vector<StationTick> stationTicks;
	QCategoryAxis* stationAxis = addRouteAxes(chart, referencePath, stationTicks, calls);
	// The readout names the station nearest the pointer.
	QAbstractAxis* distanceAxis = chart->axes(Qt::Horizontal).first();
	AxisMeta distanceMeta = axisMeta(distanceAxis);
	distanceMeta.readoutStation = true;
	describeAxis(distanceAxis, distanceMeta);
	if (chart->axes(Qt::Vertical).isEmpty()) chart->addAxis(new QValueAxis(), Qt::AlignLeft);
	chart->axes(Qt::Vertical).first()->setTitleText("Arrival delay (min)");
	const bool leftward = referencePath.nodes.back().positionKm < referencePath.nodes.front().positionKm;
	return {chart, stationAxis, std::move(stationTicks), referenceRouteText(referencePath.id, leftward)};
}

RouteChart buildRouteDiagramChart(const RouteDiagramData& diagram, const RouteDiagramData& calls, double horizonSeconds) {
	const RouteDiagramPath& referencePath = diagram.referencePath;
	const double origin = referencePath.nodes.front().positionKm;
	const double end = referencePath.nodes.back().positionKm;
	auto* chart = new QChart();
	// The stops of the trains, so that the busiest stations are named first.
	std::vector<double> callPositions;
	for (const RouteDiagramSeries& source : calls.series)
		if (source.kind == RouteDiagramSeries::Kind::Event)
			for (const RouteDiagramPoint& point : source.points) callPositions.push_back(point.positionKm);
	for (const RouteDiagramSeries& source : diagram.series) {
		const QString name = QString::fromStdString(source.trainId);
		auto* series = new QLineSeries();
		SeriesMeta meta = metaOfTrain(name);
		if (source.kind == RouteDiagramSeries::Kind::Trajectory) {
			series->setName(name);
			for (const RouteDiagramPoint& point : source.points) series->append(point.positionKm, point.seconds);
			series->setPointsVisible(source.points.size() == 1); // Preserve an isolated valid sample.
		} else {
			const QString event = QString::fromStdString(source.event);
			series->setName(name + " (" + event + ")");
			// The window paints the stops and dwells in the colour of the train; planned ones are dashed.
			meta.planned = source.planned;
			series->setPen(QPen(QColor(20, 35, 45), 1.0, source.planned ? Qt::DashLine : Qt::SolidLine));
			const bool dwell = source.kind == RouteDiagramSeries::Kind::Dwell;
			series->setPointsVisible(!dwell);
			// In the Key a dwell is a kind of its own, and the points of an event are its stops.
			if (dwell) {
				meta.keyKind = QStringLiteral("Dwell");
				meta.keyUpright = true;
			} else {
				meta.keyMarker = QStringLiteral("Stop");
			}
			for (const RouteDiagramPoint& point : source.points) {
				const QString station = QString::fromStdString(point.station);
				const QString context = dwell
					? QString("%1, %2 dwell").arg(station, source.planned ? "planned" : "simulated")
					: QString("%1, stop %2, %3").arg(station).arg(point.journey).arg(event);
				appendInspectedPoint(series, meta, point.positionKm, point.seconds, context);
			}
		}
		describeSeries(series, meta);
		chart->addSeries(series);
	}
	chart->createDefaultAxes();
	std::vector<StationTick> stationTicks;
	QCategoryAxis* stationAxis = addRouteAxes(chart, referencePath, stationTicks, callPositions);
	QValueAxis* axisY = chart->axes(Qt::Vertical).isEmpty() ? new QValueAxis() : qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first());
	if (chart->axes(Qt::Vertical).isEmpty()) chart->addAxis(axisY, Qt::AlignLeft);
	axisY->setTitleText("Time (s)");
	axisY->setReverse(true);
	axisY->setRange(0, std::max(1.0, horizonSeconds));
	for (auto* series : chart->series())
		if (!series->attachedAxes().contains(axisY)) series->attachAxis(axisY);
	// Include elapsed zero explicitly, even when clock labels start at a nonzero offset.
	return {chart, stationAxis, std::move(stationTicks), referenceRouteText(referencePath.id, end < origin)};
}

QChart* buildBlockingTimeChart(const BlockingTimeDiagramData& diagram, bool compressed, const SceneSectionLabels& labels,
	const PositionScale& scale) {
	const RouteDiagramPath& referencePath = diagram.referencePath;
	const auto& plottedPlanned = diagram.plannedLines;

	QChart* chart = new QChart();

	addBlockingTimeSeries(chart, diagram.segments, compressed, labels, scale);

	// The window paints the planned and the simulated lines in the colour of the train: planned dashed, simulated solid.
	std::set<std::string> plannedTrains;
	for (const auto& group : plottedPlanned) {
		const std::string& trainName = group.front().trainName;
		const bool first = plannedTrains.insert(trainName).second;
		auto* series = new QLineSeries();
		series->setName(QString::fromStdString(trainName));
		SeriesMeta meta = metaOfTrain(QString::fromStdString(trainName));
		meta.planned = true;
		series->setPen(QPen(QColor(20, 35, 45), 2.0, Qt::DashLine));
		series->setPointsVisible(true);
		for (const auto& reference : group)
			appendInspectedPoint(series, meta, reference.positionKm, reference.time,
				reference.stationName.empty()
					? "Planned time at the section edge"
					: QString("%1, planned %2").arg(QString::fromStdString(reference.stationName), QString::fromStdString(reference.eventType)));
		describeSeries(series, meta);
		chart->addSeries(series);
		if (!first)
			for (QLegendMarker* marker : chart->legend()->markers(series)) marker->setVisible(false);
	}

	for (const BlockingTimeTrajectory& trajectory : diagram.trajectories) {
		auto* line = new QLineSeries();
		line->setName(QString::fromStdString(trajectory.trainId));
		SeriesMeta meta = metaOfTrain(QString::fromStdString(trajectory.trainId));
		meta.layer = QStringLiteral("simulated");
		if (compressed) meta.inspectionInterval = QStringLiteral("Shifted with the train's blocking times");
		describeSeries(line, meta);
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
	const auto axisRange = blockSectionAxisRange(diagram.extents, referencePath);
	axisX->setRange(axisRange.first, axisRange.second);
	keepExactRange(axisX);

	axisY->setTitleText("Time (s)");
	axisY->setReverse(true);
	axisY->setRange(diagram.timeRange.first, diagram.timeRange.second);
	return chart;
}

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
			// An area series does not own its two lines, so the chart does.
			auto* lower = new QLineSeries(chart);
			auto* upper = new QLineSeries(chart);
			lower->append(left, segment.startTime);
			lower->append(right, segment.startTime);
			upper->append(left, segment.endTime);
			upper->append(right, segment.endTime);
			fill = new QAreaSeries(upper, lower);
			fill->setName(name);
			describeSeries(fill, metaOfTrain(name));
		}
		auto* outline = new QLineSeries();
		outline->setName(name);
		SeriesMeta meta = metaOfTrain(name);
		meta.layer = QString::fromLatin1(kBlockingTimeLayer);
		meta.inspectionFilled = left != right;
		// The train on the block by its short id, as the section strip names it, then one fact per line: the kind, the label
		// of the block, where and when it was blocked, how that time is made up and the section of the reference route.
		const QString blockText = QString::fromStdString(shortBlockId(segment.blockId));
		meta.inspectionTitle = name + " on " + blockText;
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
		meta.inspectionInterval = lines.join('\n');
		describeSeries(outline, meta);
		styleBlockingTime(fill, outline, kindFor(segment));
		if (fill) {
			chart->addSeries(fill);
			for (QLegendMarker* marker : chart->legend()->markers(fill)) marker->setVisible(false);
		}
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
