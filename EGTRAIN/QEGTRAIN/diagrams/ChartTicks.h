#ifndef CHARTTICKS_H
#define CHARTTICKS_H

#include "diagrams/BlockingTimeDiagram.h"

#include <QRectF>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

/// \file
/// Where the ticks and labels of a chart go. The rules work on numbers, so they need no chart and no fonts: the caller
/// reads the axis, measures the text and applies the result.

/// A station along the distance axis of a route chart: its position on that axis, the plain-text labels it can
/// have, from the longest to the shortest, and the number of calls that the chart shows there.
struct StationTick {
	double position = 0.0; ///< On the distance axis, in the unit of that axis
	QStringList spellings; ///< Plain-text labels, longest first
	int calls = 0;		   ///< Calls of the trains of the chart at the station
};

/// The pixels that a text on a chart takes along the axis that places it: its width on a horizontal axis and its height
/// on a vertical one.
using ChartTextWidth = std::function<double(const QString& text)>;

/// The step of the ticks of an axis over `span`: the smallest of 1, 2 or 5 times a power of ten that divides `span` into
/// no more than `steps` steps. `span` and `steps` are greater than 0.
double roundTickStep(double span, int steps);

/// The decimals of a tick label on the multiples of `step`, a power of ten times 1, 2 or 5 and greater than 0.
int roundTickDecimals(double step);

/// The ticks of a numeric axis on round values.
struct RoundTickPlan {
	double interval = 0.0; ///< From one tick to the next
	double anchor = 0.0;   ///< A value that has a tick
	int decimals = 0;	   ///< Of the tick labels, as many as the step has
};

/// Round ticks for the range `low` to `high`: the multiples of 1, 2 or 5 times a power of ten that divide it into at most
/// eight steps. None for a range that is empty, reversed or not finite.
std::optional<RoundTickPlan> roundTickPlan(double low, double high);

/// The range `low` to `high` widened to round ticks past its ends, so that both ends carry a label and a line at an end
/// keeps clear of the plot edge. Each end moves out to the first tick beyond it; an end within a thousandth of the range of
/// a tick counts as on it. A range that starts at zero keeps zero as its start, also when it starts a little below zero, as
/// a chart builder's margin of 0.01 km does: up to a thousandth of the range or a tenth of the step. The step is the
/// smallest one that keeps the widened range to eight steps, which is the step that roundTickPlan gives the widened range.
/// None for a range that is empty, reversed or not finite.
std::optional<std::pair<double, double>> widenedTickRange(double low, double high);

/// The label each station gets above the plot, as (position, plain text), on an axis from `low` to `high` that runs right
/// to left with `reverse`. Every label is centred on its station, must lie within the chart and may not touch its
/// neighbour. The chart hides a label whose text starts left of `labelsLeft`, the left edge of its axes. The chart shortens
/// a label that is wider than the share of the plot width that it gives each of the labels, so a label is only used when
/// it is narrower than that share. Of the stations in the range, every first, second, third and so on is tried until the
/// labels fit. That gives the number of labels. When stations have calls, that many labels go to the stations with the
/// most calls, as long as the labels keep apart; between choices with as many calls the stations of the stride win. A
/// station gets the longest spelling that fits. When no label fits whole, the middle station keeps its shortest spelling,
/// shortened by `elide` to half the plot width less a margin.
std::vector<std::pair<double, QString>> labelledStations(const std::vector<StationTick>& stations, double low, double high, bool reverse,
	const QRectF& plot, double labelsLeft, double chartWidth, const ChartTextWidth& textWidth,
	const std::function<QString(const QString& text, double width)>& elide);

/// The seconds between the ticks of a clock axis from `low` to `high`: the shortest of the round clock steps that gives at
/// most eight steps, or a round step of about a sixth of a longer range.
double clockTickInterval(double low, double high);

/// The ticks of a clock axis from `low` to `high`, as (elapsed seconds, label) in order, `interval` apart (clockTickInterval)
/// with `startOffset` the clock time of elapsed zero in seconds since midnight. Ticks fall on round clock times. Elapsed
/// zero, the run start, keeps a tick of its own while it is in range, and a round tick less than a step from it is left
/// out where `labelRoom` of its label is more than its distance at `pixelsPerSecond`, so where the two labels would
/// overlap, or while the plot has no size. A tick on a whole minute reads HH:MM while the ticks are a minute apart or more;
/// every other tick reads HH:MM:SS.
std::vector<std::pair<double, QString>> clockTicks(double low, double high, double interval, long long startOffset, double pixelsPerSecond,
	const ChartTextWidth& labelRoom);

/// An id that the section strip shows.
struct StripLabel {
	std::size_t section = 0; ///< Index of the section
	QRectF rect;			 ///< Where the id goes, in pixels of the chart
	bool strong = false;	 ///< A critical or conflict section, drawn in bold
};

/// The ids that the strip shows across `plot`, in `rows` rows of `rowHeight` from `top`. `marks` has one entry per section,
/// `xAt` gives the pixel of a distance along the route, and `idWidth` the width of the id of a section, in bold when
/// `strong`. A plot of 8 px or less shows none. Each id is centred in the part of its cell that lies in the plot and kept
/// inside the plot, and goes in the first row where it keeps clear of the ids placed there. The ids are placed in route
/// order when that names every section. Otherwise the ids of critical and conflict sections are placed first, then those
/// of the first and last section, of switch sections and of station sections, then the others in route order, and an id
/// that keeps clear in no row is left out. The result has the order of placing. `candidates` gets the number of ids that
/// could be shown.
std::vector<StripLabel> placedStripLabels(const std::vector<BlockSectionExtent>& sections, const std::vector<BlockSectionMarks>& marks,
	const QRectF& plot, double top, double rowHeight, int rows, const std::function<double(double km)>& xAt,
	const std::function<double(const BlockSectionExtent& section, bool strong)>& idWidth, std::size_t* candidates = nullptr);

#endif // CHARTTICKS_H
