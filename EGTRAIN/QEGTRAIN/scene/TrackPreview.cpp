#include "scene/TrackPreview.h"

#include "scene/SceneModel.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {

void addTrackLine(const SceneModel& scene, const SceneTrack& source,
		const std::unordered_map<std::string, const SceneNode*>& nodesById,
		TrackPreviewResult& result) {
	std::vector<const SceneNode*> nodes;
	for (const auto& node : scene.nodes)
		if (node.trackId == source.id)
			nodes.push_back(&node);

	std::unordered_map<std::string, std::vector<const SceneArc*>> outgoing;
	std::unordered_map<std::string, int> incoming;
	std::size_t arcCount = 0;
	for (const SceneNode* node : nodes)
		incoming[node->id] = 0;
	for (const auto& arc : scene.arcs) {
		if (arc.trackId != source.id)
			continue;
		++arcCount;
		outgoing[arc.fromNodeId].push_back(&arc);
		++incoming[arc.toNodeId];
	}

	std::vector<const SceneNode*> orderedNodes;
	std::vector<const SceneNode*> starts;
	for (const SceneNode* node : nodes)
		if (incoming[node->id] == 0)
			starts.push_back(node);
	if (starts.size() == 1) {
		const SceneNode* current = starts.front();
		std::unordered_set<std::string> visited;
		while (current && visited.insert(current->id).second) {
			orderedNodes.push_back(current);
			const auto next = outgoing.find(current->id);
			if (next == outgoing.end() || next->second.size() != 1)
				break;
			const auto target = nodesById.find(next->second.front()->toNodeId);
			current = target == nodesById.end() ? nullptr : target->second;
		}
	}
	if (orderedNodes.size() != nodes.size() || orderedNodes.size() != arcCount + 1) {
		std::sort(nodes.begin(), nodes.end(), [](const SceneNode* left, const SceneNode* right) {
			if (left->xKm != right->xKm)
				return left->xKm < right->xKm;
			return left->id < right->id;
		});
		orderedNodes = std::move(nodes);
		result.warnings.push_back("track " + source.id + " has no unique linear arc chain; preview uses node order");
	}
	if (orderedNodes.size() < 2)
		return;

	TrackPreviewLine line;
	line.id = source.id;
	line.points.reserve(orderedNodes.size());
	for (const SceneNode* node : orderedNodes)
		line.points.push_back({node->xKm, node->yKm, node->id});
	result.lines.push_back(std::move(line));
}

} // namespace

TrackPreviewResult loadTrackPreview(const SceneModel& scene) {
	TrackPreviewResult result;
	std::unordered_map<std::string, const SceneNode*> nodesById;
	for (const auto& node : scene.nodes)
		if (!node.id.empty() && nodesById.emplace(node.id, &node).second == false)
			result.warnings.push_back("duplicate node id " + node.id + " is ambiguous in preview");

	std::vector<const SceneTrack*> tracks;
	for (const auto& track : scene.tracks)
		tracks.push_back(&track);
	std::sort(tracks.begin(), tracks.end(), [](const SceneTrack* left, const SceneTrack* right) {
		return left->id < right->id;
	});
	for (const SceneTrack* track : tracks)
		addTrackLine(scene, *track, nodesById, result);

	for (const auto& connection : scene.connections) {
		const auto first = nodesById.find(connection.fromNodeId);
		const auto second = nodesById.find(connection.toNodeId);
		if (first == nodesById.end() || second == nodesById.end()) {
			result.warnings.push_back("connection " + connection.id + " has an unresolved node anchor");
			continue;
		}
		TrackPreviewConnection preview;
		preview.firstTrackId = first->second->trackId;
		preview.firstNodeId = first->second->id;
		preview.secondTrackId = second->second->trackId;
		preview.secondNodeId = second->second->id;
		result.connections.push_back(std::move(preview));
	}

	for (const auto& station : scene.stations) {
		const std::string name = station.name.empty() ? station.id : station.name;
		if (!station.platforms.empty()) {
			for (const auto& platform : station.platforms) {
				for (const auto& nodeId : platform.nodeIds) {
					const auto node = nodesById.find(nodeId);
					if (node == nodesById.end()) {
						result.warnings.push_back("platform " + platform.id + " has an unresolved node anchor");
						continue;
					}
					result.stations.push_back({name, node->second->id, node->second->xKm});
				}
			}
		} else if (station.hasPosition && std::isfinite(station.positionKm)) {
			result.stations.push_back({name, {}, station.positionKm});
		}
	}

	if (result.lines.empty())
		result.warnings.push_back("no valid scene track preview is available");
	return result;
}
