#ifndef ROUTEDIAGRAMCOORDINATES_H
#define ROUTEDIAGRAMCOORDINATES_H

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class Route;
class Train;
struct SceneModel;

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
RouteDiagramPath routeDiagramPath(const Route& route, const SceneModel* scene);
std::optional<double> routeDiagramStopPosition(const Train& train, int index,
	const RouteDiagramPath& source, const RouteDiagramProjection& projection);
// Simulation samples already use runtime route X in metres, including reversed routes.
double routeDiagramTrajectoryKm(double positionMeters);
std::map<double, std::string> routeDiagramStationLabels(const RouteDiagramPath& path);
std::string buildRouteDiagramCsv(const std::vector<std::vector<std::string>>& rows,
	const std::vector<std::string>& visibleTrainIds);

#endif
