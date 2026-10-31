#ifndef ROUTECHARTS_H
#define ROUTECHARTS_H

#include "diagrams/DiagramWindow.h"
#include "diagrams/RunExports.h"

#include <QString>
#include <QStringList>

#include <vector>

// The charts of a finished run: each function turns run data into a QChart with its series, their properties and its
// axes. A chart that a function returns belongs to the caller until DiagramWindow::setChart takes it. The functions read
// nothing but their arguments and open no window.

/// The charts of the trajectories of the trains.
enum class TrajectoryChart {
	SpeedDistance,		   ///< Speed against distance.
	SpeedTime,			   ///< Speed against time.
	TimeDistance,		   ///< Distance against time.
	TractiveEffortDistance ///< Simulated tractive effort against distance.
};

/// A chart of the trajectories and what its window needs next.
struct TrajectoryChartResult {
	QChart* chart = nullptr; ///< The chart.
	bool timeAxis = false;	 ///< Time runs along the horizontal axis, for DiagramWindow::setTimeAxisX.
};

/// A chart along a reference route and what its window needs next.
struct RouteChart {
	QChart* chart = nullptr;			   ///< The chart.
	QCategoryAxis* stationAxis = nullptr;  ///< For DiagramWindow::setStationAxis; null when the route has no station.
	std::vector<StationTick> stationTicks; ///< The stations of `stationAxis`.
	/// The reference route for the header of the window: "route0", with ", right to left" when the route runs that way.
	QString referenceRoute;
};

/// One series per contiguous segment of every train of the run, along the axes of `kind`. A sample without a value leaves
/// out the point of the tractive effort chart, and counts as 0 in the others.
TrajectoryChartResult buildTrajectoryChart(const CompletedRunData& run, TrajectoryChart kind);

/// The Delays chart: a line with a marker per call for each train that has a call at a station of the reference route.
RouteChart buildDelayChart(const DelayDiagramData& diagram);

/// The route diagram: the lines of `diagram` over the distance along its reference route and the time of the run, from 0
/// to `horizonSeconds`. `calls` gives the stops of the trains that count at each station, so that the busiest stations are
/// named first; a train path chart passes the timetable graph of its reference route, a timetable graph itself.
RouteChart buildRouteDiagramChart(const RouteDiagramData& diagram, const RouteDiagramData& calls, double horizonSeconds);

/// The blocking-time chart: the blocks, the planned lines and the simulated lines of `diagram`, over the route distance
/// exactly over the sections of the scope and the time range of the diagram. `compressed` is for the compressed diagram of
/// a capacity result. `scale` gives the geo X in the hover of a block.
QChart* buildBlockingTimeChart(const BlockingTimeDiagramData& diagram, bool compressed, const SceneSectionLabels& labels,
	const PositionScale& scale);

/// The axes of a chart along a reference route: the distance along the bottom, spanning the route exactly, and the
/// stations above the plot, both attached to every series. Gives the station axis and its ticks for
/// DiagramWindow::setStationAxis, or null when the route has no station. Each of `callPositions`, the route distances of
/// the calls that the chart shows, counts as a call at the station nearest to it.
QCategoryAxis* addRouteAxes(QChart* chart, const RouteDiagramPath& referencePath, std::vector<StationTick>& stationTicks,
	const std::vector<double>& callPositions);

/// "Approach from 10 s", "Run 12 to 30 s", "Clearance to 33 s", "Setup 2 s", "Sight and reaction 9 s", "Release 1 s" and
/// "Run margin 0 s" for the hover of a block, one per line, without the parts that are not available. In a compressed
/// chart, whose blocks move and whose times do not, each time starts with "Simulated".
QStringList blockingComponentLines(const BlockingTimeDiagramSegment& segment, bool compressed);

/// Adds a filled box and its outline for each of `segments`, and only the outline of a block of no width. The Trains list
/// gets one legend entry per train and type of block. `useCapacityCriticalStyle` draws the capacity-critical blocks of a
/// compressed chart.
void addBlockingTimeSeries(QChart* chart, const std::vector<BlockingTimeDiagramSegment>& segments,
	bool useCapacityCriticalStyle, const SceneSectionLabels& labels, const PositionScale& scale);

/// Real identities retained in the immutable export, including trains with no projectable series.
QStringList blockingTimeTrainIds(const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::vector<BlockingTimePlannedReference>& references,
	const std::vector<std::vector<std::string>>& trajectories,
	const std::vector<BlockingTimeDiagramSegment>& unmapped);

#endif // ROUTECHARTS_H
