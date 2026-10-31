#include "diagrams/PositionScale.h"
#include "diagrams/RouteCharts.h"
#include "diagrams/SeriesMeta.h"

#include <QGraphicsLayout>
#include <QPointF>
#include <QPointer>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QLegendMarker>
#include <QtCharts/QLineSeries>
#include <QtTest>

#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

// A reference route of nodes at the given positions. A node with a name is a station.
RouteDiagramPath routeOf(const std::vector<std::pair<double, std::string>>& nodes) {
	RouteDiagramPath path;
	path.id = "route0";
	for (const auto& node : nodes) path.nodes.push_back({"n", node.second, node.second, node.first});
	return path;
}

// A chart of one line, with the default axes, as a builder has it before addRouteAxes.
std::unique_ptr<QChart> chartWithLine() {
	auto chart = std::make_unique<QChart>();
	auto* series = new QLineSeries();
	series->append(1.0, 2.0);
	chart->addSeries(series);
	chart->createDefaultAxes();
	return chart;
}

QValueAxis* distanceAxis(const QChart& chart) {
	return qobject_cast<QValueAxis*>(chart.axes(Qt::Horizontal).first());
}

QValueAxis* timeAxis(const QChart& chart) {
	return qobject_cast<QValueAxis*>(chart.axes(Qt::Vertical).first());
}

QList<QPointF> pointsOf(QAbstractSeries* series) {
	return qobject_cast<QXYSeries*>(series)->points();
}

BlockingTimeDiagramSegment blockOf(const std::string& train, double left, double right,
	BlockingTimeSegmentStyle style = BlockingTimeSegmentStyle::Default) {
	BlockingTimeDiagramSegment segment;
	segment.trainName = train;
	segment.blockId = "3-B0";
	segment.startPositionKm = left;
	segment.endPositionKm = right;
	segment.startTime = 100;
	segment.endTime = 160;
	segment.originalStartTime = 100;
	segment.originalEndTime = 160;
	segment.style = style;
	return segment;
}

// The number of entries that the legend shows. The markers show once the legend has its geometry.
int shownLegendEntries(QChart& chart) {
	chart.resize(800, 600);
	chart.legend()->layout()->activate();
	int shown = 0;
	for (const QLegendMarker* marker : chart.legend()->markers()) shown += marker->isVisible() ? 1 : 0;
	return shown;
}

// One train of samples 4 to 6, whose effort lacks the last sample.
CompletedRunData runOfOneTrain() {
	CompletedRunData run;
	CompletedTrain train;
	train.trainId = "A";
	train.trajectory.firstSample = 4;
	train.trajectory.position = {1000, 2000, 3000};
	train.trajectory.speed = {10, 20, 30};
	train.trajectory.effort = {5000, 6000};
	train.trajectory.segments = {{4, 6}};
	run.trains.push_back(train);
	return run;
}

RouteDiagramSeries lineOf(RouteDiagramSeries::Kind kind, const std::string& event, bool planned,
	const std::vector<std::pair<double, double>>& points) {
	RouteDiagramSeries series;
	series.kind = kind;
	series.trainId = "A";
	series.event = event;
	series.planned = planned;
	for (const auto& point : points) {
		RouteDiagramPoint added;
		added.positionKm = point.first;
		added.seconds = point.second;
		added.station = "Delft";
		added.journey = 2;
		series.points.push_back(added);
	}
	return series;
}

RouteDiagramData routeDiagramOf(std::vector<RouteDiagramSeries> series) {
	RouteDiagramData data;
	data.referencePath = routeOf({{0.0, "Delft"}, {5.0, ""}, {10.0, "Leiden"}});
	data.series = std::move(series);
	return data;
}

} // namespace

class TestRouteCharts : public QObject {
	Q_OBJECT

private slots:
	void distanceAxisSpansTheRoute_data();
	void distanceAxisSpansTheRoute();
	void addsDistanceAxisToChartWithoutOne();
	void namesTheStationsOfTheRoute();
	void routeWithoutStationHasNoStationAxis();
	void countsCallsAtNearestStation_data();
	void countsCallsAtNearestStation();
	void listsBlockingComponents();
	void listsOneEndOfARun();
	void compressedChartTimesAreSimulated();
	void showsOneLegendEntryPerTrainAndType();
	void boxHasFillAndClosedOutline();
	void blockOfNoWidthHasNoFill();
	void chartOwnsEveryLineOfABox();
	void describesABlockInItsHover();
	void keepsWholeTimesOfMovedBlock();
	void drawsCriticalBlockOnlyInCompressedChart();
	void trajectoryChartPerKind_data();
	void trajectoryChartPerKind();
	void missingEffortAddsNoPoint();
	void routeDiagramDrawsPlannedEventsDashed();
	void routeDiagramKeysADwell();
	void routeDiagramShowsIsolatedSample();
	void routeDiagramCountsTheStopsItIsGiven();
	void routeDiagramSpansTheRun();
	void routeDiagramNamesItsReferenceRoute();
	void delayChartShowsCallsInMinutes();
	void delayChartWithoutCallsHasNoSeries();
	void blockingChartHasPlannedAndSimulatedLines();
	void blockingChartSpansItsSections();
	void listsEveryTrainOfABlockingChartOnce();
	void seriesKeepsItsDescription();
	void seriesWithoutDescriptionHasDefaults();
	void axisKeepsItsDescription();
};

void TestRouteCharts::distanceAxisSpansTheRoute_data() {
	QTest::addColumn<double>("origin");
	QTest::addColumn<double>("end");
	QTest::addColumn<double>("low");
	QTest::addColumn<double>("high");
	QTest::newRow("left to right") << 2.5 << 12.25 << 2.5 << 12.25;
	QTest::newRow("right to left") << 12.25 << 2.5 << 2.5 << 12.25;
	QTest::newRow("no length") << 3.0 << 3.0 << 3.0 << 3.02;
}

void TestRouteCharts::distanceAxisSpansTheRoute() {
	QFETCH(double, origin);
	QFETCH(double, end);
	QFETCH(double, low);
	QFETCH(double, high);
	const auto chart = chartWithLine();
	std::vector<StationTick> ticks;
	QCategoryAxis* stations = addRouteAxes(chart.get(), routeOf({{origin, "Delft"}, {end, "Leiden"}}), ticks, {});
	QValueAxis* distance = distanceAxis(*chart);
	QCOMPARE(distance->min(), low);
	QCOMPARE(distance->max(), high);
	QCOMPARE(distance->titleText(), QStringLiteral("Distance (km)"));
	// The window keeps the range as it is.
	QVERIFY(axisMeta(distance).exactRange);
	// The station axis runs along the distance axis, and every series is on both.
	if (stations == nullptr) QFAIL("no station axis");
	QCOMPARE(stations->min(), low);
	QCOMPARE(stations->max(), high);
	QCOMPARE(stations->alignment(), Qt::AlignTop);
	const QList<QAbstractAxis*> attached = chart->series().first()->attachedAxes();
	QVERIFY(attached.contains(distance));
	QVERIFY(attached.contains(stations));
}

void TestRouteCharts::addsDistanceAxisToChartWithoutOne() {
	QChart chart;
	std::vector<StationTick> ticks;
	addRouteAxes(&chart, routeOf({{1.0, ""}, {4.0, ""}}), ticks, {});
	QCOMPARE(chart.axes(Qt::Horizontal).size(), 1);
	QCOMPARE(distanceAxis(chart)->max(), 4.0);
}

void TestRouteCharts::namesTheStationsOfTheRoute() {
	const auto chart = chartWithLine();
	std::vector<StationTick> ticks;
	addRouteAxes(chart.get(), routeOf({{0.0, "Delft"}, {5.0, ""}, {12.5, "Leiden"}}), ticks, {});
	QCOMPARE(ticks.size(), std::size_t(2));
	QCOMPARE(ticks[0].position, 0.0);
	QCOMPARE(ticks[0].spellings, QStringList({"Delft (0.00 km)", "Delft"}));
	QCOMPARE(ticks[1].position, 12.5);
	QCOMPARE(ticks[1].spellings, QStringList({"Leiden (12.50 km)", "Leiden"}));
}

void TestRouteCharts::routeWithoutStationHasNoStationAxis() {
	const auto chart = chartWithLine();
	std::vector<StationTick> ticks;
	QCOMPARE(addRouteAxes(chart.get(), routeOf({{0.0, ""}, {5.0, ""}}), ticks, {7.0}), static_cast<QCategoryAxis*>(nullptr));
	QVERIFY(ticks.empty());
	QCOMPARE(chart->axes(Qt::Horizontal).size(), 1);
}

void TestRouteCharts::countsCallsAtNearestStation_data() {
	QTest::addColumn<QList<double>>("calls");
	QTest::addColumn<QList<int>>("counts");
	QTest::newRow("at a station") << QList<double>{10.0} << QList<int>{0, 1, 0};
	QTest::newRow("nearer the station before") << QList<double>{4.0} << QList<int>{1, 0, 0};
	QTest::newRow("nearer the station after") << QList<double>{6.0} << QList<int>{0, 1, 0};
	// Halfway counts at the station further along the axis.
	QTest::newRow("halfway") << QList<double>{5.0} << QList<int>{0, 1, 0};
	QTest::newRow("before the first") << QList<double>{-3.0} << QList<int>{1, 0, 0};
	QTest::newRow("past the last") << QList<double>{99.0} << QList<int>{0, 0, 1};
	QTest::newRow("several") << QList<double>{0.0, 9.0, 11.0, 29.0, 40.0} << QList<int>{1, 2, 2};
}

void TestRouteCharts::countsCallsAtNearestStation() {
	QFETCH(QList<double>, calls);
	QFETCH(QList<int>, counts);
	const auto chart = chartWithLine();
	std::vector<StationTick> ticks;
	addRouteAxes(chart.get(), routeOf({{0.0, "Delft"}, {10.0, "Leiden"}, {30.0, "Gouda"}}), ticks,
		std::vector<double>(calls.begin(), calls.end()));
	QList<int> counted;
	for (const StationTick& tick : ticks) counted.append(tick.calls);
	QCOMPARE(counted, counts);
}

void TestRouteCharts::listsBlockingComponents() {
	BlockingTimeDiagramSegment segment;
	// A part that is not available has no line.
	QCOMPARE(blockingComponentLines(segment, false), QStringList());
	segment.startApproachTime = 10;
	segment.startRunTime = 12;
	segment.endRunTime = 30;
	segment.endClearTime = 33;
	segment.setupTime = 2;
	segment.sightReactionTime = 9;
	segment.releaseTime = 1;
	segment.runTimeMargin = 0;
	QCOMPARE(blockingComponentLines(segment, false),
		QStringList({"Approach from 10 s", "Run 12 to 30 s", "Clearance to 33 s", "Setup 2 s", "Sight and reaction 9 s",
			"Release 1 s", "Run margin 0 s"}));
}

void TestRouteCharts::listsOneEndOfARun() {
	BlockingTimeDiagramSegment segment;
	segment.startRunTime = 12;
	QCOMPARE(blockingComponentLines(segment, false), QStringList({"Run from 12 s"}));
	segment.startRunTime = -1;
	segment.endRunTime = 30;
	QCOMPARE(blockingComponentLines(segment, false), QStringList({"Run to 30 s"}));
}

void TestRouteCharts::compressedChartTimesAreSimulated() {
	BlockingTimeDiagramSegment segment;
	segment.startApproachTime = 10;
	segment.startRunTime = 12;
	segment.endRunTime = 30;
	segment.endClearTime = 33;
	segment.setupTime = 2;
	QCOMPARE(blockingComponentLines(segment, true),
		QStringList({"Simulated approach from 10 s", "Simulated run 12 to 30 s", "Simulated clearance to 33 s", "Setup 2 s"}));
}

void TestRouteCharts::showsOneLegendEntryPerTrainAndType() {
	QChart chart;
	addBlockingTimeSeries(&chart,
		{blockOf("A", 0, 1), blockOf("A", 1, 2), blockOf("A", 2, 3, BlockingTimeSegmentStyle::Critical), blockOf("B", 0, 1),
			blockOf("B", 1, 1)},
		false, {}, PositionScale());
	// A, A with a conflict, and B. The fills have no entry of their own.
	QCOMPARE(shownLegendEntries(chart), 3);
}

void TestRouteCharts::boxHasFillAndClosedOutline() {
	QChart chart;
	addBlockingTimeSeries(&chart, {blockOf("A", 4, 1)}, false, {}, PositionScale());
	QCOMPARE(chart.series().size(), 2);
	auto* fill = qobject_cast<QAreaSeries*>(chart.series()[0]);
	if (fill == nullptr) QFAIL("the first series is not the fill");
	QCOMPARE(fill->name(), QStringLiteral("A"));
	QCOMPARE(seriesMeta(fill).trainId, QStringLiteral("A"));
	// The ends of the block in either order, from its start time to its end time.
	QCOMPARE(fill->lowerSeries()->points(), QList<QPointF>({{1, 100}, {4, 100}}));
	QCOMPARE(fill->upperSeries()->points(), QList<QPointF>({{1, 160}, {4, 160}}));
	QAbstractSeries* outline = chart.series()[1];
	QCOMPARE(pointsOf(outline), QList<QPointF>({{1, 100}, {4, 100}, {4, 160}, {1, 160}, {1, 100}}));
	QCOMPARE(outline->name(), QStringLiteral("A"));
	QCOMPARE(seriesMeta(outline).trainId, QStringLiteral("A"));
	QCOMPARE(seriesMeta(outline).layer, QStringLiteral("blocking time"));
	QVERIFY(seriesMeta(outline).inspectionFilled);
}

void TestRouteCharts::blockOfNoWidthHasNoFill() {
	QChart chart;
	addBlockingTimeSeries(&chart, {blockOf("A", 2, 2)}, false, {}, PositionScale());
	QCOMPARE(chart.series().size(), 1);
	QAbstractSeries* outline = chart.series()[0];
	QVERIFY(qobject_cast<QLineSeries*>(outline) != nullptr);
	// The upright line of a block without length is no closed box.
	QCOMPARE(pointsOf(outline), QList<QPointF>({{2, 100}, {2, 100}, {2, 160}, {2, 160}}));
	QVERIFY(!seriesMeta(outline).inspectionFilled);
}

void TestRouteCharts::chartOwnsEveryLineOfABox() {
	auto* chart = new QChart();
	addBlockingTimeSeries(chart, {blockOf("A", 0, 1)}, false, {}, PositionScale());
	auto* fill = qobject_cast<QAreaSeries*>(chart->series()[0]);
	if (fill == nullptr) QFAIL("the first series is not the fill");
	const QPointer<QLineSeries> lower = fill->lowerSeries();
	const QPointer<QLineSeries> upper = fill->upperSeries();
	const QPointer<QAbstractSeries> outline = chart->series()[1];
	QVERIFY(!lower.isNull() && !upper.isNull() && !outline.isNull());
	delete chart;
	QVERIFY(lower.isNull());
	QVERIFY(upper.isNull());
	QVERIFY(outline.isNull());
}

void TestRouteCharts::describesABlockInItsHover() {
	QChart chart;
	BlockingTimeDiagramSegment segment = blockOf("A", 1, 2, BlockingTimeSegmentStyle::CriticalStation);
	segment.setupTime = 2;
	segment.referenceNote = "Part of section 4\nof route0";
	BlockSectionExtent extent;
	extent.id = "3-B0";
	extent.startKm = 0;
	extent.endKm = 4;
	extent.geometryValid = true;
	extent.geoStartKm = 10;
	extent.geoEndKm = 14;
	SceneSectionLabels labels;
	labels.byReference["3-B0"] = "Delft yard";
	addBlockingTimeSeries(&chart, {segment}, false, labels, PositionScale({extent}));
	QAbstractSeries* outline = chart.series().last();
	QCOMPARE(seriesMeta(outline).inspectionTitle, QStringLiteral("A on 3-B0"));
	QCOMPARE(seriesMeta(outline).inspectionInterval.split('\n'),
		QStringList({"Conflict at a station", "Delft yard", "Position 11.000 to 12.000 km", "Blocked 100 to 160 s", "Setup 2 s",
			"Part of section 4", "of route0"}));
}

void TestRouteCharts::keepsWholeTimesOfMovedBlock() {
	BlockingTimeDiagramSegment segment = blockOf("A", 1, 2);
	segment.originalStartTime = 40;
	segment.originalEndTime = 90;
	QChart normal, compressed;
	addBlockingTimeSeries(&normal, {segment}, false, {}, PositionScale());
	addBlockingTimeSeries(&compressed, {segment}, true, {}, PositionScale());
	QVERIFY(seriesMeta(normal.series().last()).inspectionInterval.contains("Whole 40 to 90 s"));
	QVERIFY(seriesMeta(compressed.series().last()).inspectionInterval.contains("Simulated 40 to 90 s"));
}

void TestRouteCharts::drawsCriticalBlockOnlyInCompressedChart() {
	BlockingTimeDiagramSegment segment = blockOf("A", 1, 2);
	segment.capacityCritical = true;
	QChart normal, compressed;
	addBlockingTimeSeries(&normal, {segment}, false, {}, PositionScale());
	addBlockingTimeSeries(&compressed, {segment}, true, {}, PositionScale());
	QCOMPARE(seriesMeta(normal.series().last()).keyKind.value_or(QString()), QStringLiteral("Blocking time"));
	QCOMPARE(seriesMeta(compressed.series().last()).keyKind.value_or(QString()), QStringLiteral("Critical block"));
	QCOMPARE(seriesMeta(compressed.series().last()).inspectionInterval.split('\n').first(), QStringLiteral("Critical block"));
}

void TestRouteCharts::trajectoryChartPerKind_data() {
	QTest::addColumn<int>("kind");
	QTest::addColumn<bool>("time");
	QTest::addColumn<QString>("xTitle");
	QTest::addColumn<QString>("yTitle");
	QTest::addColumn<QList<QPointF>>("points");
	QTest::newRow("speed against distance") << static_cast<int>(TrajectoryChart::SpeedDistance) << false << "Distance (km)" << "Speed (km/h)"
											<< QList<QPointF>({{1, 36}, {2, 72}, {3, 108}});
	QTest::newRow("speed against time") << static_cast<int>(TrajectoryChart::SpeedTime) << true << "Time" << "Speed (km/h)"
										<< QList<QPointF>({{4, 36}, {5, 72}, {6, 108}});
	QTest::newRow("distance against time") << static_cast<int>(TrajectoryChart::TimeDistance) << true << "Time" << "Distance (km)"
										   << QList<QPointF>({{4, 1}, {5, 2}, {6, 3}});
	QTest::newRow("tractive effort") << static_cast<int>(TrajectoryChart::TractiveEffortDistance) << false << "Distance (km)"
									 << "Simulated tractive effort (kN)" << QList<QPointF>({{1, 5}, {2, 6}});
}

void TestRouteCharts::trajectoryChartPerKind() {
	QFETCH(int, kind);
	QFETCH(bool, time);
	QFETCH(QString, xTitle);
	QFETCH(QString, yTitle);
	QFETCH(QList<QPointF>, points);
	const TrajectoryChartResult built = buildTrajectoryChart(runOfOneTrain(), static_cast<TrajectoryChart>(kind));
	const std::unique_ptr<QChart> chart(built.chart);
	QCOMPARE(built.timeAxis, time);
	QCOMPARE(chart->series().size(), 1);
	QCOMPARE(chart->series().first()->name(), QStringLiteral("A"));
	QCOMPARE(seriesMeta(chart->series().first()).trainId, QStringLiteral("A"));
	QCOMPARE(pointsOf(chart->series().first()), points);
	QCOMPARE(chart->axes(Qt::Horizontal).first()->titleText(), xTitle);
	QCOMPARE(chart->axes(Qt::Vertical).first()->titleText(), yTitle);
}

void TestRouteCharts::missingEffortAddsNoPoint() {
	CompletedRunData run = runOfOneTrain();
	run.trains[0].trajectory.effort = {};
	const std::unique_ptr<QChart> chart(buildTrajectoryChart(run, TrajectoryChart::TractiveEffortDistance).chart);
	QCOMPARE(chart->series().size(), 1);
	QVERIFY(pointsOf(chart->series().first()).isEmpty());
}

void TestRouteCharts::routeDiagramDrawsPlannedEventsDashed() {
	const RouteDiagramData data = routeDiagramOf({lineOf(RouteDiagramSeries::Kind::Event, "planned arrival", true, {{0, 10}, {10, 20}}),
		lineOf(RouteDiagramSeries::Kind::Event, "simulated arrival", false, {{0, 12}, {10, 25}})});
	const RouteChart built = buildRouteDiagramChart(data, data, 100);
	const std::unique_ptr<QChart> chart(built.chart);
	QCOMPARE(chart->series().size(), 2);
	auto* planned = qobject_cast<QLineSeries*>(chart->series()[0]);
	auto* simulated = qobject_cast<QLineSeries*>(chart->series()[1]);
	if (planned == nullptr || simulated == nullptr) QFAIL("an event is not a line");
	QCOMPARE(planned->name(), QStringLiteral("A (planned arrival)"));
	QCOMPARE(seriesMeta(planned).trainId, QStringLiteral("A"));
	QVERIFY(seriesMeta(planned).planned);
	QCOMPARE(planned->pen().style(), Qt::DashLine);
	QVERIFY(planned->pointsVisible());
	QCOMPARE(seriesMeta(planned).keyMarker, QStringLiteral("Stop"));
	QCOMPARE(seriesMeta(planned).inspectionPoints,
		QStringList({"Delft, stop 2, planned arrival", "Delft, stop 2, planned arrival"}));
	QVERIFY(!seriesMeta(simulated).planned);
	QCOMPARE(simulated->pen().style(), Qt::SolidLine);
}

void TestRouteCharts::routeDiagramKeysADwell() {
	const RouteDiagramData data = routeDiagramOf({lineOf(RouteDiagramSeries::Kind::Dwell, "planned dwell", true, {{0, 10}, {0, 20}}),
		lineOf(RouteDiagramSeries::Kind::Dwell, "simulated dwell", false, {{0, 12}, {0, 22}})});
	const RouteChart built = buildRouteDiagramChart(data, data, 100);
	const std::unique_ptr<QChart> chart(built.chart);
	auto* dwell = qobject_cast<QLineSeries*>(chart->series()[0]);
	if (dwell == nullptr) QFAIL("a dwell is not a line");
	QCOMPARE(seriesMeta(dwell).keyKind.value_or(QString()), QStringLiteral("Dwell"));
	QVERIFY(seriesMeta(dwell).keyUpright);
	QVERIFY(seriesMeta(dwell).keyMarker.isEmpty());
	QVERIFY(!dwell->pointsVisible());
	QCOMPARE(seriesMeta(dwell).inspectionPoints, QStringList({"Delft, planned dwell", "Delft, planned dwell"}));
	QCOMPARE(seriesMeta(chart->series()[1]).inspectionPoints.first(), QStringLiteral("Delft, simulated dwell"));
}

void TestRouteCharts::routeDiagramShowsIsolatedSample() {
	const RouteDiagramData data = routeDiagramOf({lineOf(RouteDiagramSeries::Kind::Trajectory, "", false, {{3, 30}}),
		lineOf(RouteDiagramSeries::Kind::Trajectory, "", false, {{3, 30}, {4, 40}})});
	const RouteChart built = buildRouteDiagramChart(data, data, 100);
	const std::unique_ptr<QChart> chart(built.chart);
	auto* isolated = qobject_cast<QLineSeries*>(chart->series()[0]);
	auto* line = qobject_cast<QLineSeries*>(chart->series()[1]);
	if (isolated == nullptr || line == nullptr) QFAIL("a trajectory is not a line");
	QCOMPARE(isolated->name(), QStringLiteral("A"));
	QVERIFY(isolated->pointsVisible());
	QVERIFY(!line->pointsVisible());
	QCOMPARE(line->points(), QList<QPointF>({{3, 30}, {4, 40}}));
	QVERIFY(seriesMeta(line).inspectionPoints.isEmpty());
}

void TestRouteCharts::routeDiagramCountsTheStopsItIsGiven() {
	const RouteDiagramData data = routeDiagramOf({lineOf(RouteDiagramSeries::Kind::Trajectory, "", false, {{1, 30}, {2, 40}})});
	const RouteDiagramData timetable = routeDiagramOf({lineOf(RouteDiagramSeries::Kind::Event, "planned arrival", true, {{0, 10}, {9, 20}}),
		lineOf(RouteDiagramSeries::Kind::Dwell, "planned dwell", true, {{9, 20}, {9, 30}}),
		lineOf(RouteDiagramSeries::Kind::Trajectory, "", false, {{9, 20}})});
	const RouteChart built = buildRouteDiagramChart(data, timetable, 100);
	const std::unique_ptr<QChart> chart(built.chart);
	// Only the points of the events are stops.
	QCOMPARE(built.stationTicks.size(), std::size_t(2));
	QCOMPARE(built.stationTicks[0].calls, 1);
	QCOMPARE(built.stationTicks[1].calls, 1);
	QCOMPARE(chart->series().size(), 1);
}

void TestRouteCharts::routeDiagramSpansTheRun() {
	const RouteDiagramData data = routeDiagramOf({lineOf(RouteDiagramSeries::Kind::Trajectory, "", false, {{1, 30}, {2, 40}})});
	const RouteChart built = buildRouteDiagramChart(data, data, 500);
	const std::unique_ptr<QChart> chart(built.chart);
	QValueAxis* time = timeAxis(*chart);
	QCOMPARE(time->titleText(), QStringLiteral("Time (s)"));
	QVERIFY(time->isReverse());
	QCOMPARE(time->min(), 0.0);
	QCOMPARE(time->max(), 500.0);
	QCOMPARE(distanceAxis(*chart)->max(), 10.0);
	QVERIFY(built.stationAxis != nullptr);
	// A run without a length still has a range.
	const std::unique_ptr<QChart> empty(buildRouteDiagramChart(data, data, 0).chart);
	QCOMPARE(timeAxis(*empty)->max(), 1.0);
}

void TestRouteCharts::routeDiagramNamesItsReferenceRoute() {
	RouteDiagramData data = routeDiagramOf({lineOf(RouteDiagramSeries::Kind::Trajectory, "", false, {{1, 30}, {2, 40}})});
	const RouteChart forward = buildRouteDiagramChart(data, data, 100);
	const std::unique_ptr<QChart> forwardChart(forward.chart);
	QCOMPARE(forward.referenceRoute, QStringLiteral("route0"));
	data.referencePath = routeOf({{10.0, "Leiden"}, {0.0, "Delft"}});
	const RouteChart backward = buildRouteDiagramChart(data, data, 100);
	const std::unique_ptr<QChart> backwardChart(backward.chart);
	QCOMPARE(backward.referenceRoute, QStringLiteral("route0, right to left"));
	QCOMPARE(distanceAxis(*backwardChart)->min(), 0.0);
	QCOMPARE(distanceAxis(*backwardChart)->max(), 10.0);
}

void TestRouteCharts::delayChartShowsCallsInMinutes() {
	DelayDiagramData data;
	data.referencePath = routeOf({{10.0, "Leiden"}, {0.0, "Delft"}});
	DelayDiagramPoint point;
	point.positionKm = 1;
	point.delaySeconds = 90;
	point.station = "Delft";
	point.journey = 3;
	data.series.push_back({"A", {point}});
	data.series.push_back({"B", {}});
	const RouteChart built = buildDelayChart(data);
	const std::unique_ptr<QChart> chart(built.chart);
	// A train without a delay has no line, and its calls do not count.
	QCOMPARE(chart->series().size(), 1);
	auto* series = qobject_cast<QLineSeries*>(chart->series().first());
	if (series == nullptr) QFAIL("a train is not a line");
	QCOMPARE(series->name(), QStringLiteral("A"));
	QCOMPARE(seriesMeta(series).trainId, QStringLiteral("A"));
	QVERIFY(series->pointsVisible());
	QCOMPARE(series->points(), QList<QPointF>({{1, 1.5}}));
	QCOMPARE(seriesMeta(series).inspectionPoints, QStringList({"Delft, stop 3"}));
	QCOMPARE(chart->axes(Qt::Vertical).first()->titleText(), QStringLiteral("Arrival delay (min)"));
	QVERIFY(axisMeta(distanceAxis(*chart)).readoutStation);
	QCOMPARE(built.referenceRoute, QStringLiteral("route0, right to left"));
	QCOMPARE(built.stationTicks.size(), std::size_t(2));
	QCOMPARE(built.stationTicks[0].calls, 1);
	QCOMPARE(built.stationTicks[1].calls, 0);
}

void TestRouteCharts::delayChartWithoutCallsHasNoSeries() {
	DelayDiagramData data;
	data.referencePath = routeOf({{0.0, "Delft"}, {10.0, "Leiden"}});
	const RouteChart built = buildDelayChart(data);
	const std::unique_ptr<QChart> chart(built.chart);
	QVERIFY(chart->series().isEmpty());
	QCOMPARE(chart->axes(Qt::Vertical).size(), 1);
	QCOMPARE(chart->axes(Qt::Vertical).first()->titleText(), QStringLiteral("Arrival delay (min)"));
}

void TestRouteCharts::blockingChartHasPlannedAndSimulatedLines() {
	BlockingTimeDiagramData data;
	data.referencePath = routeOf({{0.0, "Delft"}, {10.0, "Leiden"}});
	data.timeRange = {50, 250};
	BlockingTimePlannedReference edge;
	edge.trainName = "A";
	edge.positionKm = 1;
	edge.time = 60;
	BlockingTimePlannedReference stop = edge;
	stop.stationName = "Delft";
	stop.eventType = "arrival";
	data.plannedLines = {{edge, stop}, {stop}};
	data.trajectories = {{"A", {{1, 60}}}, {"B", {{1, 60}, {2, 70}}}};
	const std::unique_ptr<QChart> chart(buildBlockingTimeChart(data, true, {}, PositionScale()));
	QCOMPARE(chart->series().size(), 4);
	auto* planned = qobject_cast<QLineSeries*>(chart->series()[0]);
	auto* isolated = qobject_cast<QLineSeries*>(chart->series()[2]);
	auto* simulated = qobject_cast<QLineSeries*>(chart->series()[3]);
	if (planned == nullptr || isolated == nullptr || simulated == nullptr) QFAIL("a line is missing");
	QVERIFY(seriesMeta(planned).planned);
	QCOMPARE(planned->pen().style(), Qt::DashLine);
	QVERIFY(planned->pointsVisible());
	QCOMPARE(seriesMeta(planned).inspectionPoints,
		QStringList({"Planned time at the section edge", "Delft, planned arrival"}));
	QCOMPARE(seriesMeta(simulated).layer, QStringLiteral("simulated"));
	QCOMPARE(simulated->pen().style(), Qt::SolidLine);
	QCOMPARE(seriesMeta(simulated).inspectionInterval, QStringLiteral("Shifted with the train's blocking times"));
	QVERIFY(isolated->pointsVisible());
	QVERIFY(!simulated->pointsVisible());
	// The second line of a train has no entry of its own.
	QCOMPARE(shownLegendEntries(*chart), 3);
	const std::unique_ptr<QChart> uncompressed(buildBlockingTimeChart(data, false, {}, PositionScale()));
	QVERIFY(seriesMeta(uncompressed->series()[3]).inspectionInterval.isEmpty());
}

void TestRouteCharts::blockingChartSpansItsSections() {
	BlockingTimeDiagramData data;
	data.referencePath = routeOf({{0.0, "Delft"}, {10.0, "Leiden"}});
	data.timeRange = {50, 250};
	BlockSectionExtent extent;
	extent.startKm = 2.5;
	extent.endKm = 6.25;
	extent.geometryValid = true;
	data.extents = {extent};
	data.segments = {blockOf("A", 3, 4)};
	const std::unique_ptr<QChart> chart(buildBlockingTimeChart(data, false, {}, PositionScale()));
	QValueAxis* distance = distanceAxis(*chart);
	QCOMPARE(distance->titleText(), QStringLiteral("Distance (km)"));
	QCOMPARE(distance->min(), 2.5);
	QCOMPARE(distance->max(), 6.25);
	QVERIFY(axisMeta(distance).exactRange);
	QValueAxis* time = timeAxis(*chart);
	QCOMPARE(time->titleText(), QStringLiteral("Time (s)"));
	QVERIFY(time->isReverse());
	QCOMPARE(time->min(), 50.0);
	QCOMPARE(time->max(), 250.0);
	for (QAbstractSeries* series : chart->series()) {
		QVERIFY(series->attachedAxes().contains(distance));
		QVERIFY(series->attachedAxes().contains(time));
	}
}

void TestRouteCharts::listsEveryTrainOfABlockingChartOnce() {
	BlockingTimePlannedReference reference;
	reference.trainName = "B";
	QCOMPARE(blockingTimeTrainIds({blockOf("A", 0, 1), blockOf("A", 1, 2)}, {reference}, {{"C", "1"}, {}, {"A"}}, {blockOf("D", 0, 1), blockOf("", 0, 1)}),
		QStringList({"A", "B", "C", "D"}));
}

void TestRouteCharts::seriesKeepsItsDescription() {
	QLineSeries series;
	SeriesMeta meta;
	meta.trainId = "A";
	meta.planned = true;
	meta.layer = "simulated";
	meta.keyKind = QString();
	meta.keyPen = QPen(Qt::red, 3.0);
	meta.keyMarker = "Stop";
	meta.keyUpright = true;
	meta.ownColour = true;
	meta.inspectionPoints = {"one", ""};
	meta.inspectionTitle = "title";
	meta.inspectionInterval = "interval";
	meta.inspectionFilled = true;
	describeSeries(&series, meta);
	const SeriesMeta read = seriesMeta(&series);
	QCOMPARE(read.trainId, QStringLiteral("A"));
	QVERIFY(read.planned);
	QCOMPARE(read.layer, QStringLiteral("simulated"));
	// An empty kind is set, and leaves the series out of the Key.
	QVERIFY(read.keyKind.has_value());
	QCOMPARE(*read.keyKind, QString());
	QVERIFY(read.keyPen.has_value());
	QCOMPARE(*read.keyPen, QPen(Qt::red, 3.0));
	QCOMPARE(read.keyMarker, QStringLiteral("Stop"));
	QVERIFY(read.keyUpright);
	QVERIFY(read.ownColour);
	QCOMPARE(read.inspectionPoints, QStringList({"one", ""}));
	QCOMPARE(read.inspectionTitle, QStringLiteral("title"));
	QCOMPARE(read.inspectionInterval, QStringLiteral("interval"));
	QVERIFY(read.inspectionFilled);
	// A new description takes the place of the old one.
	describeSeries(&series, SeriesMeta());
	QVERIFY(seriesMeta(&series).trainId.isEmpty());
}

void TestRouteCharts::seriesWithoutDescriptionHasDefaults() {
	QLineSeries series;
	const SeriesMeta read = seriesMeta(&series);
	QVERIFY(read.trainId.isEmpty());
	QVERIFY(!read.planned);
	QVERIFY(!read.keyKind.has_value());
	QVERIFY(!read.keyPen.has_value());
	QVERIFY(!read.inspectionFilled);
}

void TestRouteCharts::axisKeepsItsDescription() {
	QValueAxis axis;
	QVERIFY(!axisMeta(&axis).exactRange);
	QVERIFY(!axisMeta(&axis).readoutStation);
	AxisMeta meta;
	meta.readoutStation = true;
	describeAxis(&axis, meta);
	QVERIFY(!axisMeta(&axis).exactRange);
	QVERIFY(axisMeta(&axis).readoutStation);
}

QTEST_MAIN(TestRouteCharts)
#include "test_routecharts.moc"
