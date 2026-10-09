#include "diagrams/RouteDiagramCoordinates.h"
#include "scene/SceneModel.h"
#include "diagrams/SimulationHeaders.h"
#include <cmath>
#include <map>

RouteDiagramPath routeDiagramPath(const Route& route, const SceneModel* scene) {
	RouteDiagramPath path;
	path.id = route.ID;
	std::map<std::string, std::pair<std::string, std::string>> stationNodes;
	if (scene) for (const auto& station : scene->stations)
		for (const auto& platform : station.platforms)
			for (const auto& id : platform.nodeIds)
				stationNodes[id] = {station.id, station.name.empty() ? station.id : station.name};
	const auto add = [&](const Node& node) {
		if (!std::isfinite(node.X)) return;
		RouteDiagramNode point;
		point.nodeId = node.sceneNodeId;
		point.positionKm = node.X;
		const auto station = stationNodes.find(point.nodeId);
		if (station != stationNodes.end()) {
			point.stationId = station->second.first;
			point.stationName = station->second.second;
		}
		path.nodes.push_back(std::move(point));
	};
	for (int s = 0; s < route.N_Block_Sections; ++s) {
		const Section& section = route.sequence_of_block_sections[s];
		add(section.start_node);
		for (int a = 0; a < section.total_arcs; ++a)
			add(section.arcs_in_signalling_block_section[a].endNode);
		add(section.end_node);
	}
	return path;
}

// Runtime stop nodes retain the canonical node ID and route X assigned by the
// native operations builder. Never substitute the map X fallback from Train.
std::optional<double> routeDiagramStopPosition(const Train& train, int index,
	const RouteDiagramPath& source, const RouteDiagramProjection& projection) {
	if (!train.Stations || index < 0 || index >= train.numStations) return {};
	const Node& stop = train.Stations[index];
	if (stop.sceneNodeId.empty()) return {};
	for (const auto& node : source.nodes)
		if (node.nodeId == stop.sceneNodeId && std::abs(node.positionKm - stop.X) < 1e-6)
			return projection.map(node.positionKm);
	return {};
}
