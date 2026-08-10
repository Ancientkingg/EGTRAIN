#include "scene/SceneValidator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <regex>
#include <unordered_map>
#include <unordered_set>

namespace {

struct DiagnosticBuilder {
	std::vector<SceneDiagnostic>& diagnostics;

	void add(SceneSeverity severity, const std::string& code, const std::string& message,
			const std::string& file, const std::string& itemType = "",
			const std::string& itemId = "", const std::string& path = "",
			const std::string& relatedId = "", const std::string& suggestedFix = "") {
		SceneDiagnostic diagnostic;
		diagnostic.severity = severity;
		diagnostic.code = code;
		diagnostic.message = message;
		diagnostic.file = file;
		diagnostic.itemType = itemType;
		diagnostic.itemId = itemId;
		diagnostic.path = path;
		diagnostic.relatedId = relatedId;
		diagnostic.suggestedFix = suggestedFix;
		diagnostics.push_back(diagnostic);
	}

	void error(const std::string& code, const std::string& message, const std::string& file,
			const std::string& itemType = "", const std::string& itemId = "",
			const std::string& path = "", const std::string& relatedId = "",
			const std::string& suggestedFix = "") {
		add(SceneSeverity::Error, code, message, file, itemType, itemId, path, relatedId, suggestedFix);
	}

	void warning(const std::string& code, const std::string& message, const std::string& file,
			const std::string& itemType = "", const std::string& itemId = "",
			const std::string& path = "", const std::string& relatedId = "",
			const std::string& suggestedFix = "") {
		add(SceneSeverity::Warning, code, message, file, itemType, itemId, path, relatedId, suggestedFix);
	}
};

std::string basicBlockId(const std::string& token) {
	if (!token.empty() && token.front() == '@') {
		const std::size_t end = token.find('@', 1);
		if (end != std::string::npos)
			return token.substr(1, end - 1);
	}
	const std::size_t position = token.find('@');
	return position == std::string::npos ? token : token.substr(0, position);
}

std::vector<std::string> routeComponents(const std::string& token) {
	std::vector<std::string> components;
	std::size_t begin = 0;
	while (begin <= token.size()) {
		const std::size_t slash = token.find('/', begin);
		const std::string part = token.substr(begin,
				slash == std::string::npos ? std::string::npos : slash - begin);
		if (!part.empty())
			components.push_back(basicBlockId(part));
		if (slash == std::string::npos)
			break;
		begin = slash + 1;
	}
	return components;
}

constexpr std::size_t kNativeMaxTracks = 268;
constexpr std::size_t kNativeMaxTrackNodes = 1500;
constexpr std::size_t kNativeMaxTrackArcs = 1500;
constexpr std::size_t kNativeMaxConnections = 708;
constexpr std::size_t kNativeMaxStations = 95;
constexpr std::size_t kNativeMaxBaseBlocks = 6000;
constexpr std::size_t kNativeMaxRouteBlocks = 600;
constexpr std::size_t kNativeMaxNodeConnections = 6;
constexpr std::size_t kNativeMaxDependencies = 10;
constexpr std::size_t kNativeMaxSectionArcs = 20;
constexpr double kNativeCoordinateTolerance = 1e-8;

std::string nativeRuntimeBlockId(const std::string& id) {
	if (id.empty() || id.front() == '@' || id.find('/') != std::string::npos)
		return id;
	return "@" + id + "@";
}

std::string nativeFormattedCoordinate(double coordinate) {
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%f", coordinate);
	return buffer;
}

bool hasId(const std::unordered_set<std::string>& ids, const std::string& id) {
	return !id.empty() && ids.find(id) != ids.end();
}

template <typename T>
bool idsAreUnique(const std::vector<T>& items) {
	std::unordered_set<std::string> ids;
	for (const auto& item : items) {
		if (item.id.empty() || !ids.insert(item.id).second)
			return false;
	}
	return true;
}

template <typename T>
void collectIds(const std::vector<T>& items, const std::string& file, const std::string& type,
		const std::string& category, DiagnosticBuilder& diagnostics,
		std::unordered_set<std::string>& ids) {
	for (std::size_t index = 0; index < items.size(); ++index) {
		const std::string& id = items[index].id;
		const std::string path = category + "[" + std::to_string(index) + "].id";
		if (id.empty()) {
			diagnostics.error("scene.id.empty", type + " id must not be empty", file, type, id, path,
				"", "Give each " + type + " a non-empty id");
			continue;
		}
		if (!ids.insert(id).second) {
			diagnostics.error("scene.id.duplicate", "Duplicate " + type + " id", file, type, id,
					path, id, "Give each " + type + " a unique id");
		}
	}
}

std::vector<SceneDiagnostic> validateCore(const SceneModel& scene, bool runnable) {
	std::vector<SceneDiagnostic> result;
	DiagnosticBuilder diagnostics{result};

	if (!scene.baseTime.empty()) {
		static const std::regex timePattern("^([01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]$");
		if (!std::regex_match(scene.baseTime, timePattern)) {
			diagnostics.error("scene.basetime.invalid", "Invalid base_time format, must be HH:MM:SS",
					"scene.json", "scene", "", "base_time", "",
					"Write the base time as HH:MM:SS, for example 08:00:00");
		}
	}
	if (scene.settings.hasDuration && (!std::isfinite(scene.settings.durationSeconds)
			|| scene.settings.durationSeconds <= 0.0)) {
		diagnostics.error("scene.duration.invalid", "Simulation duration must be positive and finite", "scene.json",
				"scene", "", "simulation_settings.duration_seconds", "",
				"Use a duration greater than 0 seconds");
	}
	if (scene.settings.hasBufferTime && scene.settings.bufferTimeSeconds < 0.0) {
		diagnostics.error("scene.buffer.invalid", "Simulation buffer time cannot be negative", "scene.json",
				"scene", "", "simulation_settings.buffer_time_seconds", "",
				"Use a buffer time of 0 or more seconds");
	}
	if (scene.settings.hasRecoveryTime && scene.settings.recoveryTimePercent < 0.0) {
		diagnostics.error("scene.recovery.invalid", "Recovery time cannot be negative", "scene.json",
				"scene", "", "simulation_settings.recovery_time_percent", "",
				"Use a recovery value of 0 or more");
	}
	if (scene.trainUnits.empty() || scene.compositions.empty())
		diagnostics.error("scene.trains.none", "No trains defined", "rolling_stock.json", "", "", "", "",
				"Add at least one train unit and one composition to rolling_stock.json");
	if (scene.services.empty())
		diagnostics.error("scene.services.none", "No services defined", "services.json", "", "", "", "",
				"Add at least one service to services.json");

	const auto errorCount = [&result]() {
		return std::count_if(result.begin(), result.end(), [](const SceneDiagnostic& diagnostic) {
			return diagnostic.severity == SceneSeverity::Error;
		});
	};
	const auto errorsBeforeInfrastructure = errorCount();
	std::unordered_set<std::string> trackIds;
	collectIds(scene.tracks, "infrastructure.json", "track", "tracks", diagnostics, trackIds);
	std::unordered_set<std::string> nodeIds;
	collectIds(scene.nodes, "infrastructure.json", "node", "nodes", diagnostics, nodeIds);
	std::unordered_set<std::string> arcIds;
	collectIds(scene.arcs, "infrastructure.json", "arc", "arcs", diagnostics, arcIds);
	std::unordered_set<std::string> blockIds;
	collectIds(scene.blocks, "infrastructure.json", "block", "blocks", diagnostics, blockIds);
	std::unordered_set<std::string> connectionIds;
	collectIds(scene.connections, "infrastructure.json", "connection", "connections", diagnostics,
			connectionIds);
	std::unordered_map<std::string, const SceneNode*> nodesById;
	for (const auto& node : scene.nodes)
		if (!node.id.empty())
			nodesById.emplace(node.id, &node);

	for (std::size_t index = 0; index < scene.nodes.size(); ++index) {
		const SceneNode& node = scene.nodes[index];
		const std::string path = "nodes[" + std::to_string(index) + "]";
		if (!std::isfinite(node.xKm))
			diagnostics.error("scene.node.coordinate.invalid", "Node x_km must be finite",
				"infrastructure.json", "node", node.id, path + ".x_km");
		if (!std::isfinite(node.yKm))
			diagnostics.error("scene.node.coordinate.invalid", "Node y_km must be finite",
				"infrastructure.json", "node", node.id, path + ".y_km");
		if (!hasId(trackIds, node.trackId)) {
			diagnostics.error("scene.ref.unresolved", "Node refers to unknown track", "infrastructure.json",
					"node", node.id, path + ".track", node.trackId,
					"Add track " + node.trackId + " or reference an existing track");
		}
	}
	for (std::size_t index = 0; index < scene.arcs.size(); ++index) {
		const SceneArc& arc = scene.arcs[index];
		const std::string path = "arcs[" + std::to_string(index) + "]";
		if (!std::isfinite(arc.curvatureRadiusM) || arc.curvatureRadiusM < 0.0)
			diagnostics.error("scene.arc.curvature.invalid",
				"Arc curvature_radius_m must be finite and non-negative", "infrastructure.json", "arc", arc.id,
				path + ".curvature_radius_m");
		if (!std::isfinite(arc.gradientPercent))
			diagnostics.error("scene.arc.gradient.invalid", "Arc gradient_percent must be finite",
				"infrastructure.json", "arc", arc.id, path + ".gradient_percent");
		if (!std::isfinite(arc.speedLimitMs) || arc.speedLimitMs <= 0.0)
			diagnostics.error("scene.arc.speed.invalid", "Arc speed_limit_ms must be positive and finite",
				"infrastructure.json", "arc", arc.id, path + ".speed_limit_ms");
		if (!hasId(trackIds, arc.trackId))
			diagnostics.error("scene.ref.unresolved", "Arc refers to unknown track", "infrastructure.json",
					"arc", arc.id, path + ".track", arc.trackId);
		if (!hasId(nodeIds, arc.fromNodeId))
			diagnostics.error("scene.ref.unresolved", "Arc refers to unknown start node", "infrastructure.json",
					"arc", arc.id, path + ".from", arc.fromNodeId);
		if (!hasId(nodeIds, arc.toNodeId))
			diagnostics.error("scene.ref.unresolved", "Arc refers to unknown end node", "infrastructure.json",
					"arc", arc.id, path + ".to", arc.toNodeId);
		if (arc.fromNodeId == arc.toNodeId)
			diagnostics.error("scene.topology.loop", "Arc cannot connect a node to itself", "infrastructure.json",
					"arc", arc.id, path + ".to", arc.fromNodeId);
		if (hasId(trackIds, arc.trackId)) {
			const auto from = nodesById.find(arc.fromNodeId);
			if (from != nodesById.end() && from->second->trackId != arc.trackId)
				diagnostics.error("scene.topology.track", "Arc start node belongs to another track",
						"infrastructure.json", "arc", arc.id, path + ".from", from->second->trackId,
						"Move the node to track " + arc.trackId + " or fix the arc track");
			const auto to = nodesById.find(arc.toNodeId);
			if (to != nodesById.end() && to->second->trackId != arc.trackId)
				diagnostics.error("scene.topology.track", "Arc end node belongs to another track",
						"infrastructure.json", "arc", arc.id, path + ".to", to->second->trackId,
						"Move the node to track " + arc.trackId + " or fix the arc track");
		}
	}
	for (std::size_t index = 0; index < scene.blocks.size(); ++index) {
		const SceneBlock& block = scene.blocks[index];
		const std::string path = "blocks[" + std::to_string(index) + "]";
		if (!std::isfinite(block.lengthKm) || block.lengthKm <= 0.0)
			diagnostics.error("scene.block.length.invalid", "Block length_km must be positive and finite",
				"infrastructure.json", "block", block.id, path + ".length_km");
		if (!hasId(trackIds, block.trackId))
			diagnostics.error("scene.ref.unresolved", "Block refers to unknown track", "infrastructure.json",
					"block", block.id, path + ".track", block.trackId);
	}
	for (std::size_t index = 0; index < scene.connections.size(); ++index) {
		const SceneConnection& connection = scene.connections[index];
		const std::string path = "connections[" + std::to_string(index) + "]";
		if (connection.hasSpeedLimit
				&& (!std::isfinite(connection.speedLimitMs) || connection.speedLimitMs <= 0.0))
			diagnostics.error("scene.connection.speed.invalid",
				"Connection speed_limit_ms must be positive and finite when specified",
				"infrastructure.json", "connection", connection.id, path + ".speed_limit_ms");
		if (!hasId(nodeIds, connection.fromNodeId))
			diagnostics.error("scene.ref.unresolved", "Connection refers to unknown start node",
					"infrastructure.json", "connection", connection.id, path + ".from",
					connection.fromNodeId);
		if (!hasId(nodeIds, connection.toNodeId))
			diagnostics.error("scene.ref.unresolved", "Connection refers to unknown end node",
					"infrastructure.json", "connection", connection.id, path + ".to",
					connection.toNodeId);
	}

	const bool topologyIdsUnique = idsAreUnique(scene.tracks) && idsAreUnique(scene.nodes)
			&& idsAreUnique(scene.arcs);
	std::unordered_map<std::string, std::string> arcPaths;
	for (std::size_t index = 0; index < scene.arcs.size(); ++index)
		if (!scene.arcs[index].id.empty())
			arcPaths.emplace(scene.arcs[index].id, "arcs[" + std::to_string(index) + "]");
	std::unordered_map<std::string, std::vector<const SceneNode*>> nativeChainNodes;
	std::unordered_map<std::string, std::vector<const SceneArc*>> nativeChainArcs;
	for (std::size_t trackIndex = 0; trackIndex < scene.tracks.size(); ++trackIndex) {
		const SceneTrack& track = scene.tracks[trackIndex];
		if (track.id.empty())
			continue;
		const std::string trackPath = "tracks[" + std::to_string(trackIndex) + "]";
		std::vector<const SceneNode*> nodes;
		std::vector<const SceneArc*> arcs;
		std::size_t blockCount = 0;
		for (const auto& node : scene.nodes)
			if (node.trackId == track.id)
				nodes.push_back(&node);
		for (const auto& arc : scene.arcs)
			if (arc.trackId == track.id)
				arcs.push_back(&arc);
		for (const auto& block : scene.blocks)
			if (block.trackId == track.id)
				++blockCount;
		if (nodes.size() < 2)
			diagnostics.error("scene.topology.nodes.missing", "Track must have at least two nodes",
					"infrastructure.json", "track", track.id, trackPath + ".nodes", track.id,
					"Add at least two nodes to the track");
		if (arcs.empty())
			diagnostics.error("scene.topology.arcs.missing", "Track must have at least one arc",
					"infrastructure.json", "track", track.id, trackPath + ".arcs", track.id,
					"Add an arc between the track nodes");
		if (blockCount == 0)
			diagnostics.error("scene.topology.blocks.missing", "Track must have at least one block",
					"infrastructure.json", "track", track.id, trackPath + ".blocks", track.id,
					"Add at least one block to the track");
		if (!topologyIdsUnique || nodes.size() < 2 || arcs.empty())
			continue;

		bool chainResolvable = true;
		for (const auto* node : nodes)
			if (!std::isfinite(node->xKm) || !std::isfinite(node->yKm))
				chainResolvable = false;
		for (const auto* arc : arcs) {
			const auto from = nodesById.find(arc->fromNodeId);
			const auto to = nodesById.find(arc->toNodeId);
			if (from == nodesById.end() || to == nodesById.end()
					|| from->second->trackId != track.id || to->second->trackId != track.id)
				chainResolvable = false;
		}
		if (!chainResolvable)
			continue;

		std::unordered_map<std::string, std::vector<const SceneArc*>> outgoing;
		std::unordered_map<std::string, int> incoming;
		for (const auto* node : nodes)
			incoming[node->id] = 0;
		for (const auto* arc : arcs) {
			outgoing[arc->fromNodeId].push_back(arc);
			++incoming[arc->toNodeId];
		}
		std::vector<const SceneNode*> starts;
		bool ambiguous = false;
		for (const auto* node : nodes) {
			if (outgoing[node->id].size() > 1) {
				diagnostics.error("scene.topology.ambiguous",
						"Track has multiple outgoing arcs from a node", "infrastructure.json", "track", track.id,
						trackPath + ".nodes", node->id, "Keep at most one outgoing arc per node");
				ambiguous = true;
			}
			if (incoming[node->id] > 1) {
				diagnostics.error("scene.topology.ambiguous",
						"Track has multiple incoming arcs to a node", "infrastructure.json", "track", track.id,
						trackPath + ".nodes", node->id, "Keep at most one incoming arc per node");
				ambiguous = true;
			}
			if (incoming[node->id] == 0)
				starts.push_back(node);
		}
		if (starts.size() != 1) {
			diagnostics.error("scene.topology.disconnected", "Track must have exactly one chain start",
					"infrastructure.json", "track", track.id, trackPath + ".nodes",
					std::to_string(starts.size()), "Connect every track node into one directed chain");
			if (starts.empty() && !ambiguous) {
				const SceneNode* cycleNode = nodes.front();
				std::unordered_set<std::string> cycleNodes;
				while (cycleNode && cycleNodes.insert(cycleNode->id).second) {
					const auto next = outgoing.find(cycleNode->id);
					if (next == outgoing.end() || next->second.empty()) {
						cycleNode = nullptr;
						break;
					}
					cycleNode = nodesById[next->second.front()->toNodeId];
				}
				if (cycleNode)
					diagnostics.error("scene.topology.loop", "Track chain revisits a node", "infrastructure.json",
							"track", track.id, trackPath + ".nodes", cycleNode->id);
			}
			continue;
		}
		if (ambiguous)
			continue;

		std::unordered_set<std::string> visitedNodes;
		std::unordered_set<std::string> visitedArcs;
		std::vector<const SceneNode*> chainNodes;
		std::vector<const SceneArc*> chainArcs;
		const SceneNode* current = starts.front();
		while (current) {
			if (!visitedNodes.insert(current->id).second) {
				diagnostics.error("scene.topology.loop", "Track chain revisits a node", "infrastructure.json",
						"track", track.id, trackPath + ".nodes", current->id);
				break;
			}
			chainNodes.push_back(current);
			const auto next = outgoing.find(current->id);
			if (next == outgoing.end() || next->second.empty())
				break;
			const SceneArc* arc = next->second.front();
			if (!visitedArcs.insert(arc->id).second) {
				diagnostics.error("scene.topology.loop", "Track chain revisits an arc", "infrastructure.json",
						"arc", arc->id, arcPaths[arc->id]);
				break;
			}
			chainArcs.push_back(arc);
			const SceneNode* following = nodesById[arc->toNodeId];
			if (visitedNodes.count(following->id) > 0) {
				diagnostics.error("scene.topology.loop", "Track chain revisits a node", "infrastructure.json",
						"arc", arc->id, arcPaths[arc->id] + ".to", following->id);
				break;
			}
			if (following->xKm + kNativeCoordinateTolerance < current->xKm)
				diagnostics.error("scene.topology.order",
						"Track node x coordinates must be nondecreasing in chain direction",
						"infrastructure.json", "arc", arc->id, arcPaths[arc->id] + ".to", current->id,
						"Order the chain nodes by nondecreasing x_km");
			current = following;
		}
		if (visitedArcs.size() != arcs.size() || visitedNodes.size() != nodes.size())
			diagnostics.error("scene.topology.disconnected",
					"Track arcs and nodes must form one connected directed chain", "infrastructure.json", "track",
					track.id, trackPath + ".arcs", track.id, "Connect every declared node and arc exactly once");
		else {
			nativeChainNodes.emplace(track.id, std::move(chainNodes));
			nativeChainArcs.emplace(track.id, std::move(chainArcs));
		}
	}
	const bool infrastructureUsableForRuntimeChecks = !scene.tracks.empty()
			&& errorCount() == errorsBeforeInfrastructure
			&& nativeChainNodes.size() == scene.tracks.size();
	std::unordered_map<std::string, std::unordered_set<std::string>> blockNodeIds;
	if (infrastructureUsableForRuntimeChecks) {
		for (const auto& track : scene.tracks) {
			const auto chain = nativeChainNodes.find(track.id);
			if (chain == nativeChainNodes.end() || chain->second.empty())
				continue;
			std::vector<const SceneBlock*> trackBlocks;
			for (const auto& block : scene.blocks)
				if (block.trackId == track.id)
					trackBlocks.push_back(&block);
			if (trackBlocks.empty())
				continue;

			struct BlockSpan {
				const SceneBlock* block = nullptr;
				double start = 0.0;
				double end = 0.0;
			};
			std::vector<BlockSpan> spans;
			const double trackEnd = chain->second.back()->xKm;
			double cursor = chain->second.front()->xKm;
			bool spansValid = true;
			for (std::size_t index = 0; index < trackBlocks.size(); ++index) {
				const SceneBlock* block = trackBlocks[index];
				double end = cursor + block->lengthKm;
				if (end > trackEnd + kNativeCoordinateTolerance) {
					if (index + 1 == trackBlocks.size()
							&& cursor < trackEnd - kNativeCoordinateTolerance)
						end = trackEnd;
					else
						spansValid = false;
				}
				spans.push_back({block, cursor, std::min(end, trackEnd)});
				cursor = end;
			}
			if (cursor < trackEnd - kNativeCoordinateTolerance && !spans.empty())
				spans.back().end = trackEnd;
			if (!spansValid)
				continue;
			for (const BlockSpan& span : spans) {
				auto& ids = blockNodeIds[span.block->id];
				for (const SceneNode* node : chain->second)
					if (node->xKm >= span.start - kNativeCoordinateTolerance
							&& node->xKm <= span.end + kNativeCoordinateTolerance)
						ids.insert(node->id);
			}
		}
	}
	std::unordered_map<std::string, std::unordered_set<std::string>> sectionNodeIds = blockNodeIds;
	for (const auto& entry : blockNodeIds)
		sectionNodeIds[nativeRuntimeBlockId(entry.first)] = entry.second;
	if (infrastructureUsableForRuntimeChecks) {
		for (const auto& connection : scene.connections) {
			const auto from = nodesById.find(connection.fromNodeId);
			const auto to = nodesById.find(connection.toNodeId);
			if (from == nodesById.end() || to == nodesById.end())
				continue;
			const SceneNode* first = from->second;
			const SceneNode* second = to->second;
			if (first->xKm > second->xKm)
				std::swap(first, second);
			if (first->xKm + kNativeCoordinateTolerance >= second->xKm)
				continue;
			for (const auto& firstBlock : blockNodeIds) {
				if (firstBlock.second.count(first->id) == 0)
					continue;
				for (const auto& secondBlock : blockNodeIds) {
					if (secondBlock.second.count(second->id) == 0)
						continue;
					const std::string id = nativeRuntimeBlockId(firstBlock.first) + "-"
							+ nativeFormattedCoordinate(first->xKm) + "/"
							+ nativeRuntimeBlockId(secondBlock.first) + "-"
							+ nativeFormattedCoordinate(second->xKm);
					auto& ids = sectionNodeIds[id];
					for (const auto& nodeId : firstBlock.second) {
						const auto node = nodesById.find(nodeId);
						if (node != nodesById.end()
								&& node->second->xKm <= first->xKm + kNativeCoordinateTolerance)
							ids.insert(nodeId);
					}
					ids.insert(second->id);
					for (const auto& nodeId : secondBlock.second) {
						const auto node = nodesById.find(nodeId);
						if (node != nodesById.end()
								&& node->second->xKm > second->xKm + kNativeCoordinateTolerance)
							ids.insert(nodeId);
					}
				}
			}
		}
	}

	std::unordered_set<std::string> stationIds;
	std::unordered_map<std::string, const SceneStation*> stations;
	std::unordered_map<std::string, std::string> platformOwnerByNode;
	for (std::size_t index = 0; index < scene.stations.size(); ++index) {
		const SceneStation& station = scene.stations[index];
		const std::string path = "stations[" + std::to_string(index) + "]";
		if (station.id.empty()) {
			diagnostics.error("scene.id.empty", "Station id must not be empty", "stations.json", "station",
					station.id, path + ".id", "", "Give each station a non-empty id");
		} else if (!stationIds.insert(station.id).second) {
			diagnostics.error("scene.id.duplicate", "Duplicate station id", "stations.json", "station",
					station.id, path + ".id", station.id);
		}
		if (!station.id.empty())
			stations[station.id] = &station;
		if (station.hasPosition && !std::isfinite(station.positionKm))
			diagnostics.error("scene.station.position.invalid", "Station position must be finite", "stations.json",
					"station", station.id, path + ".position_km", "", "Use a finite position in kilometres");
		if (!station.hasPosition && station.platforms.empty())
			diagnostics.error("scene.station.anchor.missing", "Station has neither a position nor a platform anchor",
					"stations.json", "station", station.id, path, "", "Set a position or add a bound platform");
		std::unordered_set<std::string> platformIds;
		for (std::size_t platformIndex = 0; platformIndex < station.platforms.size(); ++platformIndex) {
			const ScenePlatform& platform = station.platforms[platformIndex];
			const std::string platformPath = path + ".platforms[" + std::to_string(platformIndex) + "]";
			if (platform.id.empty())
				diagnostics.error("scene.id.empty", "Platform id must not be empty", "stations.json", "platform",
						platform.id, platformPath + ".id", "", "Give each platform a non-empty id");
			else if (!platformIds.insert(platform.id).second)
				diagnostics.error("scene.id.duplicate", "Duplicate platform id on station", "stations.json",
						"platform", platform.id, platformPath + ".id", station.id);
			if (platform.nodeIds.empty())
				diagnostics.error("scene.platform.nodes.none", "Platform has no bound nodes", "stations.json",
						"platform", platform.id, platformPath + ".nodes", "", "Bind the platform to at least one node");
			for (std::size_t nodeIndex = 0; nodeIndex < platform.nodeIds.size(); ++nodeIndex) {
				const std::string& nodeId = platform.nodeIds[nodeIndex];
				if (!hasId(nodeIds, nodeId)) {
					diagnostics.error("scene.ref.unresolved", "Platform refers to unknown node", "stations.json",
							"platform", platform.id, platformPath + ".nodes[" + std::to_string(nodeIndex) + "]",
							nodeId);
				} else if (!station.id.empty() && !platform.id.empty()) {
					const std::string owner = station.id + "\n" + platform.id;
					const auto inserted = platformOwnerByNode.emplace(nodeId, owner);
					if (!inserted.second && inserted.first->second != owner)
						diagnostics.error("scene.platform.node.conflict",
								"Node is bound to more than one station/platform", "stations.json", "platform",
								platform.id, platformPath + ".nodes[" + std::to_string(nodeIndex) + "]",
								nodeId, "Keep one station/platform assignment for each node");
				}
			}
		}
	}

	std::unordered_set<std::string> signalIds;
	collectIds(scene.signals, "signalling.json", "signal", "signals", diagnostics, signalIds);
	std::unordered_set<std::string> signallingAreaIds;
	collectIds(scene.signallingAreas, "signalling.json", "signalling area", "signalling_areas",
			diagnostics, signallingAreaIds);
	for (std::size_t index = 0; index < scene.signallingAreas.size(); ++index) {
		const SceneSignallingArea& area = scene.signallingAreas[index];
		const std::string path = "signalling_areas[" + std::to_string(index) + "]";
		if (!std::isfinite(area.startKm) || !std::isfinite(area.endKm)
				|| !(area.startKm < area.endKm))
			diagnostics.error("scene.signalling_area.range",
				"Signalling area start_km and end_km must be finite with start_km below end_km",
				"signalling.json", "signalling_area", area.id, path, area.id,
				"Use a finite increasing coordinate range");
		if (area.level < 0 || area.level > 5)
			diagnostics.error("scene.signalling_area.level", "Signalling area level must be between 0 and 5",
				"signalling.json", "signalling_area", area.id, path + ".level", area.trackId,
				"Use a signalling level from 0 through 5");
		if (!area.trackId.empty() && !hasId(trackIds, area.trackId))
			diagnostics.error("scene.ref.unresolved", "Signalling area refers to unknown track",
				"signalling.json", "signalling_area", area.id, path + ".track", area.trackId,
				"Add the track or leave track blank for a network-wide area");
	}
	std::unordered_set<std::string> routeIds;
	collectIds(scene.routes, "signalling.json", "route", "routes", diagnostics, routeIds);
	std::unordered_set<std::string> routeBlockIds;
	std::unordered_map<std::string, std::unordered_set<std::string>> routeNodeIds;
	std::unordered_set<std::string> routesWithCompleteNodeMembership;
	for (std::size_t index = 0; index < scene.routes.size(); ++index) {
		const SceneRoute& route = scene.routes[index];
		const std::string path = "routes[" + std::to_string(index) + "]";
		if (route.blocks.empty()) {
			diagnostics.error("scene.route.empty", "Route has no blocks", "signalling.json", "route", route.id,
					path + ".blocks", "", "List the block ids the route runs through");
		}
		bool nodeMembershipComplete = !route.blocks.empty() && !blockNodeIds.empty();
		for (const auto& token : route.blocks) {
			for (const auto& component : routeComponents(token)) {
				routeBlockIds.insert(component);
			}
			const auto nodes = sectionNodeIds.find(token);
			if (nodes == sectionNodeIds.end())
				nodeMembershipComplete = false;
			else
				routeNodeIds[route.id].insert(nodes->second.begin(), nodes->second.end());
		}
		if (nodeMembershipComplete)
			routesWithCompleteNodeMembership.insert(route.id);
		if (!blockIds.empty()) {
			for (std::size_t blockIndex = 0; blockIndex < route.blocks.size(); ++blockIndex) {
				const std::string blockPath = path + ".blocks[" + std::to_string(blockIndex) + "]";
				for (const auto& component : routeComponents(route.blocks[blockIndex])) {
					if (!hasId(blockIds, component)) {
						diagnostics.error("scene.ref.unresolved", "Route refers to unknown block",
								"signalling.json", "route", route.id, blockPath, component,
								"Add block " + component + " or fix the route token");
					}
				}
			}
		}
	}
	auto blockReferenceKnown = [&](const std::string& reference) {
		const std::vector<std::string> components = routeComponents(reference);
		if (components.empty())
			return false;
		for (const auto& component : components) {
			const bool found = blockIds.empty() ? hasId(routeBlockIds, component)
					: hasId(blockIds, component);
			if (!found)
				return false;
		}
		return true;
	};
	for (std::size_t index = 0; index < scene.blockDependencies.size(); ++index) {
		const SceneBlockDependency& dependency = scene.blockDependencies[index];
		const std::string path = "block_dependencies[" + std::to_string(index) + "]";
		if (!blockReferenceKnown(dependency.block))
			diagnostics.error("scene.ref.unresolved", "Block dependency refers to unknown block",
					"signalling.json", "block_dependency", dependency.block, path + ".block",
					dependency.block);
		if (!blockReferenceKnown(dependency.dependsOn))
			diagnostics.error("scene.ref.unresolved", "Block dependency refers to unknown dependency",
					"signalling.json", "block_dependency", dependency.block, path + ".depends_on",
					dependency.dependsOn);
	}
	for (std::size_t index = 0; index < scene.singleTrackRestrictions.size(); ++index) {
		const SceneSingleTrackRestriction& restriction = scene.singleTrackRestrictions[index];
		const std::string path = "single_track_restrictions[" + std::to_string(index) + "]";
		const std::array<std::pair<const char*, const std::string*>, 4> roles = {{
			{"start_block", &restriction.startBlock},
			{"end_block", &restriction.endBlock},
			{"protected_start_block", &restriction.protectedStartBlock},
			{"protected_end_block", &restriction.protectedEndBlock},
		}};
		for (const auto& role : roles) {
			if (!blockReferenceKnown(*role.second))
				diagnostics.error("scene.ref.unresolved", "Single-track restriction refers to unknown block",
						"signalling.json", "single_track_restriction", *role.second,
						path + "." + role.first, *role.second);
		}
	}
	for (std::size_t index = 0; index < scene.stationBoundaries.size(); ++index) {
		const SceneStationBoundary& boundary = scene.stationBoundaries[index];
		const std::string path = "station_boundaries[" + std::to_string(index) + "]";
		if (!blockReferenceKnown(boundary.entranceBlock))
			diagnostics.error("scene.ref.unresolved", "Station boundary refers to unknown entrance block",
					"signalling.json", "station_boundary", boundary.entranceBlock,
					path + ".entrance_block", boundary.entranceBlock);
		if (boundary.hasExitBlock && !blockReferenceKnown(boundary.exitBlock))
			diagnostics.error("scene.ref.unresolved", "Station boundary refers to unknown exit block",
					"signalling.json", "station_boundary", boundary.exitBlock,
					path + ".exit_block", boundary.exitBlock);
	}

	std::unordered_set<std::string> trainUnitIds;
	collectIds(scene.trainUnits, "rolling_stock.json", "train unit", "train_units", diagnostics,
			trainUnitIds);
	for (std::size_t index = 0; index < scene.trainUnits.size(); ++index) {
		const SceneTrainUnit& unit = scene.trainUnits[index];
		const std::string path = "train_units[" + std::to_string(index) + "]";
		if (unit.tractionCurve.empty()) {
			diagnostics.error("scene.train.traction.empty", "Train unit has no traction data",
					"rolling_stock.json", "train_unit", unit.id, path + ".traction_curve", "",
					"Add at least one traction curve row");
		}
		if (runnable && !unit.hasPhysical) {
			diagnostics.error("scene.train.physical.missing", "Train unit has no physical parameters",
					"rolling_stock.json", "train_unit", unit.id, path + ".physical", "",
					"Add the train-unit physical parameters");
		}
		for (std::size_t rowIndex = 0; rowIndex < unit.tractionCurve.size(); ++rowIndex) {
			const auto& row = unit.tractionCurve[rowIndex];
			const std::string rowPath = path + ".traction_curve[" + std::to_string(rowIndex) + "]";
			if (!(row[0] < row[1])) {
				diagnostics.error("scene.train.traction.interval",
						"Traction curve lower speed must be below upper speed", "rolling_stock.json",
						"train_unit", unit.id, rowPath, "",
						"Set the lower speed below the upper speed");
			}
			if (rowIndex > 0) {
				const auto& previous = unit.tractionCurve[rowIndex - 1];
				if (row[0] < previous[0]) {
					diagnostics.error("scene.train.traction.order",
							"Traction curve rows are not in ascending speed order", "rolling_stock.json",
							"train_unit", unit.id, rowPath, "", "Order rows by increasing lower speed");
				} else if (row[0] < previous[1]) {
					diagnostics.error("scene.train.traction.overlap", "Traction curve intervals overlap",
							"rolling_stock.json", "train_unit", unit.id, rowPath, "",
							"Adjust adjacent bounds so intervals do not overlap");
				}
			}
		}
	}

	std::unordered_set<std::string> compositionIds;
	collectIds(scene.compositions, "rolling_stock.json", "composition", "compositions", diagnostics,
			compositionIds);
	for (std::size_t index = 0; index < scene.compositions.size(); ++index) {
		const SceneComposition& composition = scene.compositions[index];
		const std::string path = "compositions[" + std::to_string(index) + "]";
		if (composition.units.empty()) {
			diagnostics.error("scene.composition.empty", "Composition has no units", "rolling_stock.json",
					"composition", composition.id, path + ".units", "",
					"List at least one train unit id");
		}
		for (std::size_t unitIndex = 0; unitIndex < composition.units.size(); ++unitIndex) {
			if (!hasId(trainUnitIds, composition.units[unitIndex]))
				diagnostics.error("scene.ref.unresolved", "Composition refers to unknown train unit",
						"rolling_stock.json", "composition", composition.id,
						path + ".units[" + std::to_string(unitIndex) + "]", composition.units[unitIndex]);
		}
	}

	std::unordered_set<std::string> serviceIds;
	std::unordered_map<std::string, int> serviceOccurrences;
	collectIds(scene.services, "services.json", "service", "services", diagnostics, serviceIds);
	for (std::size_t index = 0; index < scene.services.size(); ++index) {
		const SceneService& service = scene.services[index];
		const std::string path = "services[" + service.id + "]";
		if (!hasId(compositionIds, service.composition))
			diagnostics.error("scene.ref.unresolved", "Service refers to unknown composition", "services.json",
					"service", service.id, path + ".composition", service.composition);
		if (!hasId(routeIds, service.route))
			diagnostics.error("scene.ref.unresolved", "Service refers to unknown route", "services.json",
					"service", service.id, path + ".route", service.route);
		if (!std::isfinite(service.performancePercent) || service.performancePercent < 1.0
				|| service.performancePercent > 100.0)
			diagnostics.error("scene.performance.invalid", "Service performance_percent must be finite and between 1 and 100",
					"services.json", "service", service.id, path + ".performance_percent", "",
					"Use a performance percentage from 1 through 100");
		if (service.hasMaximumSpeed
				&& (!std::isfinite(service.maximumSpeedKmh) || service.maximumSpeedKmh <= 0.0))
			diagnostics.error("scene.speed.invalid", "Service maximum_speed_kmh must be positive and finite",
					"services.json", "service", service.id, path + ".maximum_speed_kmh", "",
					"Use a positive maximum speed in km/h");
		if (service.hasRepeat && (!std::isfinite(service.headwaySeconds) || service.headwaySeconds <= 0.0))
			diagnostics.error("scene.repeat.invalid", "Non-positive or non-finite headway", "services.json", "service",
					service.id, path + ".repeat.headway_seconds", "",
					"Use a headway greater than 0 seconds");
		if (service.hasRepeatCount && !service.hasRepeat)
			diagnostics.error("scene.repeat.count.invalid", "repeat.count requires a repeat object",
					"services.json", "service", service.id, path + ".repeat.count");
		if (service.hasRepeat && service.hasRepeatCount && service.repeatCount <= 0)
			diagnostics.error("scene.repeat.count.invalid", "repeat.count must be a positive integer",
					"services.json", "service", service.id, path + ".repeat.count", "",
					"Use a count of 1 or more");
		if (service.hasOperatingCodeStep && !service.hasRepeat)
			diagnostics.error("scene.repeat.step.invalid", "repeat.operating_code_step requires a repeat object",
					"services.json", "service", service.id, path + ".repeat.operating_code_step");
		if (service.hasRepeat && service.hasOperatingCodeStep
				&& sceneServiceOccurrenceOperatingCode(service, 1).empty())
			diagnostics.error("scene.repeat.step.invalid",
					"repeat.operating_code_step must be nonzero and use a decimal operating code base",
					"services.json", "service", service.id, path + ".repeat.operating_code_step", "",
					"Use a nonzero step with a decimal operating_code");
		const int occurrences = sceneServiceOccurrenceCount(service,
				scene.settings.hasDuration ? scene.settings.durationSeconds : 0.0);
		if (service.hasRepeat && service.hasOperatingCodeStep
				&& !sceneServiceOccurrenceOperatingCode(service, 1).empty()
				&& sceneServiceOccurrenceOperatingCode(service, occurrences).empty())
			diagnostics.error("scene.repeat.step.invalid",
					"repeat.operating_code_step progression exceeds the supported integer range",
					"services.json", "service", service.id, path + ".repeat.operating_code_step", "",
					"Use a smaller decimal base, step, or repeat count");
		serviceOccurrences[service.id] = occurrences;
		if (service.stops.empty()) {
			if (!service.through)
				diagnostics.warning("scene.service.no_stops", "Service has no stops", "services.json",
						"service", service.id, path + ".stops", "",
						"Add at least one stop or mark the service through");
		} else if (service.through) {
			diagnostics.warning("scene.service.through_stops", "Through service has stops", "services.json",
					"service", service.id, path + ".through");
		}
		bool hasPreviousDeparture = false;
		double previousDeparture = 0.0;
		for (std::size_t stopIndex = 0; stopIndex < service.stops.size(); ++stopIndex) {
			const SceneStop& stop = service.stops[stopIndex];
			const std::string stopPath = path + ".stops[" + std::to_string(stopIndex) + "]";
			auto station = stations.find(stop.stationId);
			if (station == stations.end()) {
				diagnostics.error("scene.ref.unresolved", "Stop refers to unknown station", "services.json",
						"service", service.id, stopPath + ".station", stop.stationId);
			} else if (!stop.platformId.empty()) {
				const ScenePlatform* selectedPlatform = nullptr;
				for (const auto& platform : station->second->platforms) {
					if (platform.id == stop.platformId) {
						selectedPlatform = &platform;
						break;
					}
				}
				if (selectedPlatform == nullptr) {
					diagnostics.error("scene.ref.platform", "Stop refers to platform not on station",
							"services.json", "service", service.id, stopPath + ".platform", stop.platformId);
				} else if (!selectedPlatform->nodeIds.empty()
						&& routesWithCompleteNodeMembership.count(service.route) > 0) {
					const auto routeNodes = routeNodeIds.find(service.route);
					const bool accessible = routeNodes != routeNodeIds.end()
							&& std::any_of(selectedPlatform->nodeIds.begin(), selectedPlatform->nodeIds.end(),
									[&routeNodes](const std::string& nodeId) {
										return routeNodes->second.count(nodeId) > 0;
									});
					if (!accessible)
						diagnostics.error("scene.ref.platform.route", "Stop platform is not present on the service route",
								"services.json", "service", service.id, stopPath + ".platform", stop.platformId,
								"Bind the platform to a node on the selected route or choose another platform");
				}
			}
			if (stop.hasPlannedArrival && stop.hasPlannedDeparture
					&& stop.plannedDepartureSeconds < stop.plannedArrivalSeconds) {
				diagnostics.error("scene.time.invalid", "Departure before arrival", "services.json",
						"service", service.id, stopPath + ".planned_departure_seconds");
			}
			if (stop.hasPlannedDeparture) {
				if (hasPreviousDeparture && stop.plannedDepartureSeconds < previousDeparture)
					diagnostics.warning("scene.time.order", "Non-increasing departure times", "services.json",
							"service", service.id, stopPath + ".planned_departure_seconds");
				hasPreviousDeparture = true;
				previousDeparture = stop.plannedDepartureSeconds;
			} else if (stopIndex + 1 < service.stops.size()) {
				diagnostics.warning("scene.time.departure.missing",
						"Intermediate stop has no planned departure", "services.json", "service", service.id,
						stopPath + ".planned_departure_seconds");
			}
			if (stop.dwellSeconds < 0.0)
				diagnostics.error("scene.dwell.invalid", "Negative dwell time", "services.json", "service",
						service.id, stopPath + ".dwell_seconds", "",
						"Use a dwell time of 0 or more seconds");
			if (stop.hasPlannedArrival && stop.hasPlannedDeparture
					&& stop.dwellSeconds > stop.plannedDepartureSeconds - stop.plannedArrivalSeconds) {
				diagnostics.warning("scene.dwell.exceeds_window",
						"Dwell time exceeds departure - arrival window", "services.json", "service", service.id,
						stopPath + ".dwell_seconds");
			}
		}
	}

	std::unordered_set<std::string> scenarioIds;
	std::unordered_set<std::string> incidentIds;
	for (std::size_t scenarioIndex = 0; scenarioIndex < scene.scenarios.size(); ++scenarioIndex) {
		const SceneScenario& scenario = scene.scenarios[scenarioIndex];
		const std::string scenarioPath = "scenarios[" + std::to_string(scenarioIndex) + "]";
		if (!scenarioIds.insert(scenario.id).second)
			diagnostics.error("scene.id.duplicate", "Duplicate scenario id", "scenarios.json", "scenario",
					scenario.id, scenarioPath + ".id", scenario.id);
		for (std::size_t incidentIndex = 0; incidentIndex < scenario.incidents.size(); ++incidentIndex) {
			const SceneIncident& incident = scenario.incidents[incidentIndex];
			const std::string path = scenarioPath + ".incidents[" + std::to_string(incidentIndex) + "]";
			const bool hasOccurrence = incident.hasOccurrence || incident.occurrence != 1;
			const bool hasReducedSpeed = incident.hasReducedSpeed || incident.reducedSpeedKmh != 0.0;
			const bool hasEnd = incident.hasEndSeconds || incident.endSeconds != 0.0;
			if (!incidentIds.insert(incident.id).second)
				diagnostics.error("scene.id.duplicate", "Duplicate incident id", "scenarios.json", "incident",
						incident.id, path + ".id", incident.id);
			if (incident.type == "signal_failure") {
				bool targetFound = hasId(signalIds, incident.target);
				if (!targetFound)
					targetFound = blockReferenceKnown(incident.target);
				if (!targetFound)
					diagnostics.error("scene.ref.unresolved", "Signal failure refers to unknown signal or block",
							"scenarios.json", "incident", incident.id, path + ".target", incident.target);
			} else if (incident.type == "train_breakdown") {
				if (!hasId(serviceIds, incident.target))
					diagnostics.error("scene.ref.unresolved", "Train breakdown refers to unknown service",
							"scenarios.json", "incident", incident.id, path + ".target", incident.target);
				if (hasOccurrence) {
					if (incident.occurrence <= 0)
						diagnostics.error("scene.occurrence.invalid", "Breakdown occurrence must be positive",
								"scenarios.json", "incident", incident.id, path + ".occurrence");
					else if (hasId(serviceIds, incident.target)
							&& incident.occurrence > serviceOccurrences[incident.target])
						diagnostics.error("scene.occurrence.invalid", "Breakdown occurrence is outside the configured service pattern",
								"scenarios.json", "incident", incident.id, path + ".occurrence",
								incident.target + "-" + std::to_string(incident.occurrence));
				}
				if (hasReducedSpeed
						&& (!std::isfinite(incident.reducedSpeedKmh) || incident.reducedSpeedKmh <= 0.0))
					diagnostics.error("scene.incident.speed", "Reduced breakdown speed must be positive and finite",
							"scenarios.json", "incident", incident.id, path + ".reduced_speed_kmh");
				if (!hasReducedSpeed && !hasEnd)
					diagnostics.error("scene.incident.window", "A full-hold breakdown requires end_seconds",
							"scenarios.json", "incident", incident.id, path + ".end_seconds");
			} else {
				diagnostics.error("scene.incident.type", "Unknown incident type", "scenarios.json", "incident",
						incident.id, path + ".type", incident.type,
						"Use signal_failure or train_breakdown");
			}
			if (!std::isfinite(incident.startSeconds) || incident.startSeconds < 0.0)
				diagnostics.error("scene.incident.window", "Incident start_seconds must be finite and non-negative",
						"scenarios.json", "incident", incident.id, path + ".start_seconds");
			if (incident.type == "signal_failure") {
				if (hasOccurrence || hasReducedSpeed || incident.terminateAtDestination)
					diagnostics.error("scene.incident.fields", "Signal failures do not accept breakdown-only fields",
							"scenarios.json", "incident", incident.id, path);
				if (!hasEnd || !std::isfinite(incident.endSeconds)
						|| incident.endSeconds <= incident.startSeconds)
					diagnostics.error("scene.incident.window", "Signal failure requires end_seconds after start_seconds",
							"scenarios.json", "incident", incident.id, path + ".end_seconds");
			} else if (hasEnd && (!std::isfinite(incident.endSeconds)
					|| incident.endSeconds <= incident.startSeconds)) {
				diagnostics.error("scene.incident.window", "Incident end_seconds must be after start_seconds",
						"scenarios.json", "incident", incident.id, path + ".end_seconds");
			}
		}
		for (std::size_t delayIndex = 0; delayIndex < scenario.entranceDelays.size(); ++delayIndex) {
			const SceneEntranceDelay& delay = scenario.entranceDelays[delayIndex];
			const std::string path = scenarioPath + ".entrance_delays[" + std::to_string(delayIndex) + "]";
			if (!hasId(serviceIds, delay.serviceId))
				diagnostics.error("scene.ref.unresolved", "Entrance delay refers to unknown service",
						"scenarios.json", "entrance_delay", delay.serviceId, path + ".service", delay.serviceId);
			if (!hasId(stationIds, delay.stationId))
				diagnostics.error("scene.ref.unresolved", "Entrance delay refers to unknown station",
						"scenarios.json", "entrance_delay", delay.serviceId, path + ".station", delay.stationId);
			if (delay.occurrence <= 0)
				diagnostics.error("scene.occurrence.invalid", "Entrance delay occurrence must be positive",
						"scenarios.json", "entrance_delay", delay.serviceId, path + ".occurrence");
			else if (hasId(serviceIds, delay.serviceId)
					&& delay.occurrence > serviceOccurrences[delay.serviceId])
				diagnostics.warning("scene.entrance.occurrence.out_of_horizon",
						"Entrance delay refers to a service occurrence outside the configured pattern",
						"scenarios.json", "entrance_delay", delay.serviceId, path + ".occurrence",
						delay.serviceId + "-" + std::to_string(delay.occurrence));
			if (delay.delaySeconds < 0.0)
				diagnostics.error("scene.delay.invalid", "Entrance delay cannot be negative", "scenarios.json",
						"entrance_delay", delay.serviceId, path + ".delay_seconds");
		}
	}
	if (!scene.defaultScenarioId.empty() && !hasId(scenarioIds, scene.defaultScenarioId))
		diagnostics.error("scene.ref.unresolved", "Default scenario refers to unknown scenario",
				"scenarios.json", "scene", "", "default_scenario_id", scene.defaultScenarioId);

	std::unordered_set<std::string> passengerIds;
	std::unordered_set<std::string> journeyIds;
	std::unordered_set<std::string> passengerLegIds;
	for (std::size_t passengerIndex = 0; passengerIndex < scene.passengers.size(); ++passengerIndex) {
		const ScenePassenger& passenger = scene.passengers[passengerIndex];
		const std::string passengerPath = "passengers[" + std::to_string(passengerIndex) + "]";
		if (!passengerIds.insert(passenger.id).second)
			diagnostics.error("scene.id.duplicate", "Duplicate passenger id", "passengers.json", "passenger",
					passenger.id, passengerPath + ".id", passenger.id);
		for (std::size_t journeyIndex = 0; journeyIndex < passenger.journeys.size(); ++journeyIndex) {
			const ScenePassengerJourney& journey = passenger.journeys[journeyIndex];
			const std::string journeyPath = passengerPath + ".journeys[" + std::to_string(journeyIndex) + "]";
			if (!journeyIds.insert(journey.id).second)
				diagnostics.error("scene.id.duplicate", "Duplicate passenger journey id", "passengers.json",
						"journey", journey.id, journeyPath + ".id", journey.id);
			if (!hasId(stationIds, journey.originStationId))
				diagnostics.error("scene.ref.unresolved", "Journey refers to unknown origin station",
						"passengers.json", "journey", journey.id, journeyPath + ".origin", journey.originStationId);
			if (!hasId(stationIds, journey.destinationStationId))
				diagnostics.error("scene.ref.unresolved", "Journey refers to unknown destination station",
						"passengers.json", "journey", journey.id, journeyPath + ".destination",
						journey.destinationStationId);
			if (journey.plannedDepartureStartSeconds < 0.0
					|| journey.plannedDepartureEndSeconds < journey.plannedDepartureStartSeconds)
				diagnostics.error("scene.passenger.window", "Invalid planned departure window", "passengers.json",
						"journey", journey.id, journeyPath + ".planned_departure");
			if (journey.plannedArrivalStartSeconds < 0.0
					|| journey.plannedArrivalEndSeconds < journey.plannedArrivalStartSeconds)
				diagnostics.error("scene.passenger.window", "Invalid planned arrival window", "passengers.json",
						"journey", journey.id, journeyPath + ".planned_arrival");
			if (journey.legs.empty())
				diagnostics.warning("scene.passenger.legs.empty", "Journey has no route-choice legs", "passengers.json",
						"journey", journey.id, journeyPath + ".legs");
			for (std::size_t legIndex = 0; legIndex < journey.legs.size(); ++legIndex) {
				const ScenePassengerLeg& leg = journey.legs[legIndex];
				const std::string legPath = journeyPath + ".legs[" + std::to_string(legIndex) + "]";
				if (!passengerLegIds.insert(leg.id).second)
					diagnostics.error("scene.id.duplicate", "Duplicate passenger leg id", "passengers.json",
							"leg", leg.id, legPath + ".id", leg.id);
				if (!hasId(stationIds, leg.originStationId))
					diagnostics.error("scene.ref.unresolved", "Passenger leg refers to unknown origin station",
							"passengers.json", "leg", leg.id, legPath + ".origin", leg.originStationId);
				if (!hasId(stationIds, leg.destinationStationId))
					diagnostics.error("scene.ref.unresolved", "Passenger leg refers to unknown destination station",
							"passengers.json", "leg", leg.id, legPath + ".destination",
							leg.destinationStationId);
				if (!hasId(serviceIds, leg.serviceId))
					diagnostics.error("scene.ref.unresolved", "Passenger leg refers to unknown service",
							"passengers.json", "leg", leg.id, legPath + ".service", leg.serviceId);
				if (leg.occurrence <= 0)
					diagnostics.error("scene.occurrence.invalid", "Passenger leg occurrence must be positive",
							"passengers.json", "leg", leg.id, legPath + ".occurrence");
				else if (hasId(serviceIds, leg.serviceId)
						&& leg.occurrence > serviceOccurrences[leg.serviceId])
					diagnostics.warning("scene.passenger.occurrence.out_of_horizon",
							"Passenger leg refers to a service occurrence outside the simulation horizon",
							"passengers.json", "leg", leg.id, legPath + ".occurrence",
							leg.serviceId + "-" + std::to_string(leg.occurrence));
				if (legIndex == 0 && leg.originStationId != journey.originStationId)
					diagnostics.error("scene.passenger.continuity", "First passenger leg does not start at journey origin",
							"passengers.json", "journey", journey.id, legPath + ".origin", leg.originStationId);
				if (legIndex > 0 && leg.originStationId != journey.legs[legIndex - 1].destinationStationId)
					diagnostics.error("scene.passenger.continuity", "Passenger legs are not continuous",
							"passengers.json", "journey", journey.id, legPath + ".origin", leg.originStationId);
				if (legIndex + 1 == journey.legs.size()
						&& leg.destinationStationId != journey.destinationStationId)
					diagnostics.error("scene.passenger.continuity",
							"Last passenger leg does not end at journey destination", "passengers.json", "journey",
							journey.id, legPath + ".destination", leg.destinationStationId);
			}
		}
	}

	if (runnable) {
		if (scene.baseTime.empty())
			diagnostics.error("scene.basetime.missing", "Runnable scene requires base_time", "scene.json",
					"scene", "", "base_time", "", "Set scene.json base_time to HH:MM:SS");
		if (!scene.settings.hasDuration || !std::isfinite(scene.settings.durationSeconds)
				|| scene.settings.durationSeconds <= 0.0)
			diagnostics.error("scene.duration.missing", "Runnable scene requires a positive finite duration",
					"scene.json", "scene", "", "simulation_settings.duration_seconds");
		if (scene.tracks.empty())
			diagnostics.error("scene.topology.tracks.none", "Runnable scene has no tracks",
					"infrastructure.json", "track", "", "tracks");
		if (scene.nodes.empty())
			diagnostics.error("scene.topology.nodes.none", "Runnable scene has no nodes",
					"infrastructure.json", "node", "", "nodes");
		if (scene.arcs.empty())
			diagnostics.error("scene.topology.arcs.none", "Runnable scene has no arcs",
					"infrastructure.json", "arc", "", "arcs");
		if (scene.blocks.empty())
			diagnostics.error("scene.topology.blocks.none", "Runnable scene has no blocks",
					"infrastructure.json", "block", "", "blocks");
		if (scene.stations.empty())
			diagnostics.error("scene.stations.none", "Runnable scene has no stations", "stations.json",
					"station", "", "stations");
		std::unordered_set<std::string> usedPlatforms;
		for (const auto& service : scene.services) {
			for (const auto& stop : service.stops) {
				if (!stop.platformId.empty())
					usedPlatforms.insert(stop.stationId + "\n" + stop.platformId);
			}
		}
		int boundPlatformCount = 0;
		for (std::size_t stationIndex = 0; stationIndex < scene.stations.size(); ++stationIndex) {
			const SceneStation& station = scene.stations[stationIndex];
			const std::string stationPath = "stations[" + std::to_string(stationIndex) + "]";
			for (const auto& platform : station.platforms) {
				if (!platform.nodeIds.empty())
					++boundPlatformCount;
				if (usedPlatforms.count(station.id + "\n" + platform.id) > 0 && platform.nodeIds.empty())
					diagnostics.error("scene.platform.nodes.none", "Platform has no bound nodes", "stations.json",
							"platform", platform.id, stationPath + ".platforms");
			}
		}
		if (boundPlatformCount == 0)
			diagnostics.error("scene.platforms.none", "Runnable scene has no bound platform nodes",
					"stations.json", "station", "", "stations[].platforms[].nodes");
		if (scene.routes.empty())
			diagnostics.error("scene.routes.none", "Runnable scene has no routes", "signalling.json", "route",
					"", "routes");
		if (scene.scenarios.empty())
			diagnostics.error("scene.scenarios.none", "Runnable scene has no scenarios", "scenarios.json",
					"scenario", "", "scenarios");
		if (scene.defaultScenarioId.empty())
			diagnostics.error("scene.scenario.default.missing", "Runnable scene requires a default scenario",
					"scenarios.json", "scene", "", "default_scenario_id");

		if (infrastructureUsableForRuntimeChecks) {
			auto runtimeCapacity = [&](const std::string& message, const std::string& file,
					const std::string& itemType, const std::string& itemId, const std::string& path,
					const std::string& relatedId = "", const std::string& suggestedFix = "") {
				diagnostics.error("scene.capacity.runtime", message, file, itemType, itemId, path,
						relatedId, suggestedFix);
			};

			if (scene.tracks.size() > kNativeMaxTracks)
				runtimeCapacity("Scene has more than " + std::to_string(kNativeMaxTracks)
						+ " tracks for the native runtime", "infrastructure.json", "track", "", "tracks",
						std::to_string(scene.tracks.size()), "Reduce the number of tracks");

			std::unordered_map<std::string, std::size_t> nodesPerTrack;
			std::unordered_map<std::string, std::size_t> arcsPerTrack;
			for (const auto& node : scene.nodes)
				++nodesPerTrack[node.trackId];
			for (const auto& arc : scene.arcs)
				++arcsPerTrack[arc.trackId];
			for (std::size_t trackIndex = 0; trackIndex < scene.tracks.size(); ++trackIndex) {
				const SceneTrack& track = scene.tracks[trackIndex];
				const std::string trackPath = "tracks[" + std::to_string(trackIndex) + "]";
				const std::size_t nodeCount = nodesPerTrack[track.id];
				if (nodeCount > kNativeMaxTrackNodes)
					runtimeCapacity("Track has more than " + std::to_string(kNativeMaxTrackNodes)
							+ " runtime nodes", "infrastructure.json", "track", track.id,
							trackPath + ".nodes", std::to_string(nodeCount), "Reduce nodes on this track");
				const std::size_t arcCount = arcsPerTrack[track.id];
				if (arcCount > kNativeMaxTrackArcs)
					runtimeCapacity("Track has more than " + std::to_string(kNativeMaxTrackArcs)
							+ " runtime arcs", "infrastructure.json", "track", track.id,
							trackPath + ".arcs", std::to_string(arcCount), "Reduce arcs on this track");
			}

			if (scene.connections.size() > kNativeMaxConnections)
				runtimeCapacity("Scene has more than " + std::to_string(kNativeMaxConnections)
						+ " connections for the native runtime", "infrastructure.json", "connection", "",
						"connections", std::to_string(scene.connections.size()), "Reduce the number of connections");
			if (scene.stations.size() > kNativeMaxStations)
				runtimeCapacity("Scene has more than " + std::to_string(kNativeMaxStations)
						+ " stations for the native runtime", "stations.json", "station", "", "stations",
						std::to_string(scene.stations.size()), "Reduce the number of stations");
			struct NativeBlockPlan {
				const SceneBlock* source = nullptr;
				std::string trackId;
				double startX = 0.0;
				double endX = 0.0;
			};
			struct PlannedSignallingSection {
				std::string id;
				double startX = 0.0;
				double endX = 0.0;
				std::string firstTrackId;
				std::string secondTrackId;
			};
			std::vector<NativeBlockPlan> blockPlans;
			std::vector<PlannedSignallingSection> plannedSignallingSections;
			std::vector<const SceneTrack*> orderedTracks;
			orderedTracks.reserve(scene.tracks.size());
			for (const auto& track : scene.tracks)
				orderedTracks.push_back(&track);
			std::sort(orderedTracks.begin(), orderedTracks.end(), [](const SceneTrack* left,
					const SceneTrack* right) { return left->id < right->id; });

			for (const SceneTrack* track : orderedTracks) {
				const auto chainNodes = nativeChainNodes.find(track->id);
				const auto chainArcs = nativeChainArcs.find(track->id);
				if (chainNodes == nativeChainNodes.end() || chainArcs == nativeChainArcs.end()
						|| chainNodes->second.empty())
					continue;
				std::vector<const SceneBlock*> trackBlocks;
				for (const auto& block : scene.blocks)
					if (block.trackId == track->id)
						trackBlocks.push_back(&block);
				if (trackBlocks.empty())
					continue;

				const double trackEnd = chainNodes->second.back()->xKm;
				double cursor = chainNodes->second.front()->xKm;
				for (std::size_t blockIndex = 0; blockIndex < trackBlocks.size(); ++blockIndex) {
					const SceneBlock* block = trackBlocks[blockIndex];
					double end = cursor + block->lengthKm;
					if (end > trackEnd + kNativeCoordinateTolerance) {
						if (blockIndex + 1 == trackBlocks.size()
								&& cursor < trackEnd - kNativeCoordinateTolerance) {
							end = trackEnd;
						} else {
							runtimeCapacity("Block section extends beyond its track", "infrastructure.json",
									"block", block->id,
									"blocks[" + std::to_string(
											static_cast<std::size_t>(block - scene.blocks.data())) + "].length_km",
									track->id, "Reduce block lengths to fit the track");
						}
					}
					if (end <= cursor + kNativeCoordinateTolerance)
						runtimeCapacity("Block section has no positive runtime span", "infrastructure.json",
								"block", block->id,
								"blocks[" + std::to_string(
										static_cast<std::size_t>(block - scene.blocks.data())) + "].length_km",
								track->id, "Use positive block lengths larger than the native coordinate tolerance");
					int arcCount = 0;
					for (const SceneArc* arc : chainArcs->second) {
						const auto from = nodesById.find(arc->fromNodeId);
						const auto to = nodesById.find(arc->toNodeId);
						if (from != nodesById.end() && to != nodesById.end()
								&& to->second->xKm > cursor + kNativeCoordinateTolerance
								&& from->second->xKm < end - kNativeCoordinateTolerance)
							++arcCount;
					}
					if (arcCount > static_cast<int>(kNativeMaxSectionArcs))
						runtimeCapacity("Block section exceeds the runtime arc capacity", "infrastructure.json",
								"block", block->id,
								"blocks[" + std::to_string(
										static_cast<std::size_t>(block - scene.blocks.data())) + "].arcs",
								std::to_string(kNativeMaxSectionArcs), "Split the block section");
					blockPlans.push_back({block, track->id, cursor, std::min(end, trackEnd)});
					cursor = end;
				}
				if (cursor < trackEnd - kNativeCoordinateTolerance && !blockPlans.empty()
						&& blockPlans.back().trackId == track->id) {
					blockPlans.back().endX = trackEnd;
				} else if (cursor < trackEnd - kNativeCoordinateTolerance) {
					runtimeCapacity("Block sections do not cover the complete track", "infrastructure.json",
							"track", track->id, "tracks[" + std::to_string(
							static_cast<std::size_t>(track - scene.tracks.data())) + "].blocks", track->id,
							"Extend the final block to the track endpoint");
				}
			}

			std::unordered_set<std::string> plannedSectionIds;
			for (const NativeBlockPlan& plan : blockPlans) {
				const std::string runtimeId = nativeRuntimeBlockId(plan.source->id);
				if (!plannedSectionIds.insert(runtimeId).second)
					runtimeCapacity("Canonical block IDs normalize to the same native runtime section ID",
							"infrastructure.json", "block", plan.source->id, "blocks", runtimeId,
							"Give each block a distinct native runtime section ID");
				plannedSignallingSections.push_back(
						{runtimeId, plan.startX, plan.endX, plan.trackId, {}});
			}
			auto plannedRuntimeId = [&](const std::string& reference) {
				if (plannedSectionIds.find(reference) != plannedSectionIds.end())
					return reference;
				if (reference.find('/') == std::string::npos) {
					const std::string wrapped = nativeRuntimeBlockId(reference);
					if (plannedSectionIds.find(wrapped) != plannedSectionIds.end())
						return wrapped;
				}
				return std::string();
			};

			for (const auto& connection : scene.connections) {
				const auto from = nodesById.find(connection.fromNodeId);
				const auto to = nodesById.find(connection.toNodeId);
				if (from == nodesById.end() || to == nodesById.end())
					continue;
				const SceneNode* first = from->second;
				const SceneNode* second = to->second;
				if (first->xKm > second->xKm)
					std::swap(first, second);
				if (first->xKm + kNativeCoordinateTolerance >= second->xKm)
					continue;
				const std::string firstX = nativeFormattedCoordinate(first->xKm);
				const std::string secondX = nativeFormattedCoordinate(second->xKm);
				for (const NativeBlockPlan& firstPlan : blockPlans) {
					if (firstPlan.trackId != first->trackId
							|| first->xKm < firstPlan.startX - kNativeCoordinateTolerance
							|| first->xKm > firstPlan.endX + kNativeCoordinateTolerance)
						continue;
					for (const NativeBlockPlan& secondPlan : blockPlans) {
						if (secondPlan.trackId != second->trackId
								|| second->xKm < secondPlan.startX - kNativeCoordinateTolerance
								|| second->xKm > secondPlan.endX + kNativeCoordinateTolerance)
							continue;
						const std::string derivedId = nativeRuntimeBlockId(firstPlan.source->id) + "-" + firstX
								+ "/" + nativeRuntimeBlockId(secondPlan.source->id) + "-" + secondX;
						if (!plannedSectionIds.insert(derivedId).second)
							runtimeCapacity("Connections produce the same native runtime switch section ID",
									"infrastructure.json", "connection", connection.id, "connections", derivedId,
									"Give each switch connection a distinct runtime section ID");
						plannedSignallingSections.push_back({derivedId, firstPlan.startX, secondPlan.endX,
								firstPlan.trackId, secondPlan.trackId});
						int derivedArcCount = 1;
						for (const SceneArc* arc : nativeChainArcs[firstPlan.trackId]) {
							const auto fromNode = nodesById.find(arc->fromNodeId);
							const auto toNode = nodesById.find(arc->toNodeId);
							if (fromNode == nodesById.end() || toNode == nodesById.end())
								continue;
							const double beginX = std::max(firstPlan.startX, fromNode->second->xKm);
							const double endX = std::min(firstPlan.endX, toNode->second->xKm);
							if (endX > beginX + kNativeCoordinateTolerance
									&& endX <= first->xKm + kNativeCoordinateTolerance)
								++derivedArcCount;
						}
						for (const SceneArc* arc : nativeChainArcs[secondPlan.trackId]) {
							const auto fromNode = nodesById.find(arc->fromNodeId);
							const auto toNode = nodesById.find(arc->toNodeId);
							if (fromNode == nodesById.end() || toNode == nodesById.end())
								continue;
							const double beginX = std::max(secondPlan.startX, fromNode->second->xKm);
							const double endX = std::min(secondPlan.endX, toNode->second->xKm);
							if (endX > beginX + kNativeCoordinateTolerance
									&& endX > second->xKm + kNativeCoordinateTolerance)
								++derivedArcCount;
						}
						if (derivedArcCount > static_cast<int>(kNativeMaxSectionArcs))
							runtimeCapacity("Derived switch section exceeds the runtime arc capacity",
									"infrastructure.json", "connection", connection.id, "connections",
									std::to_string(kNativeMaxSectionArcs), "Reduce the connected block spans");
					}
				}
			}
			for (const PlannedSignallingSection& section : plannedSignallingSections) {
				for (const bool trackScoped : {false, true}) {
					const SceneSignallingArea* matched = nullptr;
					for (std::size_t areaIndex = 0; areaIndex < scene.signallingAreas.size(); ++areaIndex) {
						const SceneSignallingArea& area = scene.signallingAreas[areaIndex];
						if (!std::isfinite(area.startKm) || !std::isfinite(area.endKm)
								|| !(area.startKm < area.endKm) || area.level < 0 || area.level > 5
								|| area.trackId.empty() != !trackScoped
								|| section.startX < area.startKm - kNativeCoordinateTolerance
								|| section.endX > area.endKm + kNativeCoordinateTolerance)
							continue;
						if (trackScoped && area.trackId != section.firstTrackId
								&& area.trackId != section.secondTrackId)
							continue;
						if (!matched) {
							matched = &area;
							continue;
						}
						if (matched->level != area.level) {
							diagnostics.error("scene.signalling_area.conflict",
									"Multiple signalling areas assign different levels to one runtime section",
									"signalling.json", "signalling_area", area.id,
									"signalling_areas[" + std::to_string(areaIndex) + "]",
									matched->id + " -> " + section.id,
									"Adjust area ranges, levels, or track scope");
							break;
						}
					}
				}
			}
			if (plannedSectionIds.size() > kNativeMaxBaseBlocks)
				runtimeCapacity("Base blocks and derived switch sections exceed runtime capacity",
						"infrastructure.json", "block", "", "blocks",
						std::to_string(kNativeMaxBaseBlocks), "Reduce base blocks or switch connections");

			std::unordered_map<std::string, std::size_t> endpointCounts;
			for (const auto& connection : scene.connections) {
				++endpointCounts[connection.fromNodeId];
				++endpointCounts[connection.toNodeId];
			}
			for (const auto& node : scene.nodes) {
				const std::size_t endpointCount = endpointCounts[node.id];
				if (endpointCount > kNativeMaxNodeConnections)
					runtimeCapacity("Node has more than " + std::to_string(kNativeMaxNodeConnections)
							+ " runtime connection endpoints", "infrastructure.json", "node", node.id,
							"connections", std::to_string(endpointCount), "Reduce connections at this node");
			}

			for (std::size_t routeIndex = 0; routeIndex < scene.routes.size(); ++routeIndex) {
				const SceneRoute& route = scene.routes[routeIndex];
				if (route.blocks.size() > kNativeMaxRouteBlocks)
					runtimeCapacity("Route has more than " + std::to_string(kNativeMaxRouteBlocks)
							+ " block tokens for the native runtime", "signalling.json", "route", route.id,
							"routes[" + std::to_string(routeIndex) + "].blocks",
							std::to_string(route.blocks.size()), "Shorten the route");
			}

			std::unordered_map<std::string, std::unordered_set<std::string>> dependencyTargets;
			for (const auto& target : plannedSectionIds) {
				if (target.find('/') == std::string::npos)
					continue;
				for (const auto& component : routeComponents(target))
					if (!component.empty())
						dependencyTargets[nativeRuntimeBlockId(component)].insert(target);
			}
			for (const auto& dependency : scene.blockDependencies) {
				const std::string source = plannedRuntimeId(dependency.block);
				const std::string target = plannedRuntimeId(dependency.dependsOn);
				if (!source.empty() && !target.empty())
					dependencyTargets[source].insert(target);
			}
			for (const auto& dependency : dependencyTargets) {
				if (dependency.second.size() > kNativeMaxDependencies)
					runtimeCapacity("A normalized source block has more than "
							+ std::to_string(kNativeMaxDependencies)
							+ " distinct implicit or explicit dependency targets", "signalling.json", "block_dependency",
							dependency.first, "block_dependencies", std::to_string(dependency.second.size()),
							"Reduce explicit dependencies for this source block");
			}
		}
	}

	return result;
}

} // namespace

std::vector<SceneDiagnostic> validateSceneStructure(const std::string& sceneDir) {
	return loadScene(sceneDir).diagnostics;
}

std::vector<SceneDiagnostic> validateScene(const SceneModel& scene) {
	return validateCore(scene, false);
}

std::vector<SceneDiagnostic> validateRunnableScene(const SceneModel& scene) {
	return validateCore(scene, true);
}

std::vector<SceneDiagnostic> validateSceneDirectory(const std::string& sceneDir) {
	SceneLoadResult loadResult = loadScene(sceneDir);
	if (hasErrors(loadResult.diagnostics))
		return loadResult.diagnostics;
	std::vector<SceneDiagnostic> diagnostics = loadResult.diagnostics;
	const std::vector<SceneDiagnostic> semantic = validateScene(loadResult.scene);
	diagnostics.insert(diagnostics.end(), semantic.begin(), semantic.end());
	return diagnostics;
}

std::vector<SceneDiagnostic> validateRunnableSceneDirectory(const std::string& sceneDir) {
	SceneLoadResult loadResult = loadScene(sceneDir);
	if (hasErrors(loadResult.diagnostics))
		return loadResult.diagnostics;
	std::vector<SceneDiagnostic> diagnostics = loadResult.diagnostics;
	const std::vector<SceneDiagnostic> runnable = validateRunnableScene(loadResult.scene);
	diagnostics.insert(diagnostics.end(), runnable.begin(), runnable.end());
	return diagnostics;
}
