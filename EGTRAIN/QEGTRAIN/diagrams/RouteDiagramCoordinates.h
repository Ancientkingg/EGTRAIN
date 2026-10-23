#ifndef ROUTEDIAGRAMCOORDINATES_H
#define ROUTEDIAGRAMCOORDINATES_H

#include "diagrams/BlockingTimeDiagram.h"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct CompletedStop;

struct RouteDiagramNode {
	std::string nodeId;
	std::string stationId;
	std::string stationName;
	double positionKm = 0.0;
};

struct RouteDiagramPath {
	std::string id;
	std::vector<RouteDiagramNode> nodes;
};

struct RouteDiagramProjection {
	bool identity = false;
	std::vector<std::pair<double, double>> anchors;
	std::optional<double> map(double positionKm) const;
};

// Runtime route coordinates, not map X or authored chainage. Other routes are
// projected between uniquely shared nodes/stations; no extrapolation is made.
RouteDiagramProjection buildRouteDiagramProjection(const RouteDiagramPath& source,
	const RouteDiagramPath& reference);
// Distance-axis range of a chart of the reference route, exactly: the sections that have geometry, else the finite nodes
// of the reference path, else 0 to 1, and at least 0.02 km long. Envelopes are projected onto that path.
std::pair<double, double> blockSectionAxisRange(const std::vector<BlockSectionExtent>& extents, const RouteDiagramPath& reference);
// Where a stop lies on the reference route: its node is on the path of its own route at the stored abscissa. Empty when the
// stop has no node or the path does not hold it there.
std::optional<double> routeDiagramStopPosition(const CompletedStop& stop,
	const RouteDiagramPath& source, const RouteDiagramProjection& projection);
// Simulation samples already use runtime route X in metres, including reversed routes.
double routeDiagramTrajectoryKm(double positionMeters);
std::map<double, std::string> routeDiagramStationLabels(const RouteDiagramPath& path);
std::string buildRouteDiagramCsv(const std::vector<std::vector<std::string>>& rows,
	const std::vector<std::string>& visibleTrainIds);

#endif
