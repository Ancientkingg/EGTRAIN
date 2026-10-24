#include "scene/RuntimeLimits.h"
#include "scene/SceneModel.h"
#include "scene/SectionInventory.h"
#include "simulation/Passengers.h"
#include "simulation/Signalling.h"
#include "simulation/SimulationRun.h"
#include "simulation/builders/InfrastructureBuilderInternal.h"
#include "util/Log.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

void setUpRoutesFromScene(SimulationRun& run, const SceneModel& scene, const std::vector<int>& routeDirections) {
	run.infra.numRoutes = (int)scene.routes.size();
	run.infra.routes.clear();
	run.infra.routes.resize(run.infra.numRoutes);
	resetSingleTrackLocks(run);

	for (int i = 0; i < run.infra.numRoutes; i++) {
		EG_LOG(SimSetup, Info) << "\rCreating Scene Route : " << i << " " << scene.routes[i].id;
		const int direction = i < static_cast<int>(routeDirections.size()) ? routeDirections[i] : 0;
		run.infra.routes[i].createRouteFromBlockIds(run, scene.routes[i].blocks, direction);
		run.infra.routes[i].adjustRouteAcrossDiffRegions();
		run.infra.routes[i].ID = scene.routes[i].id;
		if (scene.routes[i].hasCorridor)
			run.infra.routes[i].corridor = scene.routes[i].corridor;
		if (scene.routes[i].reversed) {
			run.infra.routes[i].reversed_direction = true;
			run.infra.routes[i].OriginalRefReversedRoute = run.infra.routes[i].x_of_end_node * 1000.0;
		}
	}
	EG_LOG(SimSetup, Info) << "\n";
}

namespace {

using RuntimeLimits::kNativeCoordinateTolerance;
using RuntimeLimits::kNativeMaxRouteBlocks;
using RuntimeLimits::kNativeMaxSectionArcs;

struct NativeTrack {
	std::string id;
	std::vector<const SceneNode*> chainNodes;
	std::vector<const SceneArc*> chainArcs;
};

struct NativeBlockPlan {
	const SceneBlock* source = nullptr;
	int trackSlot = -1;
	double startX = 0.0;
	double endX = 0.0;
};

struct NativeSectionPlan {
	std::string id;
	double startX = 0.0;
	double endX = 0.0;
	std::string firstTrackId;
	std::string secondTrackId;
};

struct NativeStationBinding {
	const SceneStation* station = nullptr;
	const ScenePlatform* platform = nullptr;
	std::string nodeId;
};

// What the validation of a scene decides and the commit builds from. It points into the scene and is valid as long as the scene is.
struct InfrastructurePlan {
	std::vector<NativeTrack> tracks;
	std::vector<NativeBlockPlan> blocks;
	SceneSectionInventory sectionInventory;
	std::unordered_set<std::string> sectionIds;
	std::unordered_map<std::string, int> signallingLevels;
	std::vector<int> routeDirections;
};

// Appends diagnostics to a list.
struct NativeDiagnosticAdder {
	std::vector<SceneDiagnostic>& diagnostics;

	void operator()(SceneSeverity severity, const std::string& code, const std::string& message,
		const std::string& file, const std::string& type = "", const std::string& id = "",
		const std::string& path = "", const std::string& related = "", const std::string& fix = "") const {
		SceneDiagnostic diagnostic;
		diagnostic.severity = severity;
		diagnostic.code = code;
		diagnostic.message = message;
		diagnostic.file = file;
		diagnostic.itemType = type;
		diagnostic.itemId = id;
		diagnostic.path = path;
		diagnostic.relatedId = related;
		diagnostic.suggestedFix = fix;
		diagnostics.push_back(diagnostic);
	}
};

std::string nativeRuntimeBlockId(const std::string& id) {
	if (id.empty() || id.front() == '@' || id.find('/') != std::string::npos)
		return id;
	return "@" + id + "@";
}

std::vector<std::string> nativeBlockReferenceComponents(const std::string& reference) {
	std::vector<std::string> result;
	std::size_t begin = 0;
	while (begin <= reference.size()) {
		const std::size_t slash = reference.find('/', begin);
		const std::string part = reference.substr(begin,
			slash == std::string::npos ? std::string::npos : slash - begin);
		if (!part.empty()) {
			std::string id = part;
			if (part.front() == '@') {
				const std::size_t end = part.find('@', 1);
				if (end != std::string::npos)
					id = part.substr(1, end - 1);
			} else {
				const std::size_t at = part.find('@');
				if (at != std::string::npos)
					id = part.substr(0, at);
			}
			if (!id.empty())
				result.push_back(id);
		}
		if (slash == std::string::npos)
			break;
		begin = slash + 1;
	}
	return result;
}

void nativeResetRuntime(SimulationRun& run) {
	run.resetInfrastructure();
	run.numPlatforms = 0;
	run.platforms.clear();
	run.platformIndex.clear();
	// The fictitious station of the delay statistics at the final stop of every train
	run.finalStation = Stations();
	run.finalStation.stationName = "Final_Station";
}

bool nativeHasErrors(const std::vector<SceneDiagnostic>& diagnostics) {
	for (const auto& diagnostic : diagnostics)
		if (diagnostic.severity == SceneSeverity::Error)
			return true;
	return false;
}

// The infrastructure of the run as it was before a build. The members are swapped, not copied: the station boundaries point
// into the sections and the sections point into the detection sections, and a swap leaves both where they are.
class RuntimeInfrastructureBackup {
public:
	// Takes the infrastructure out of the run, which is left with an empty one.
	explicit RuntimeInfrastructureBackup(SimulationRun& run) : run_(run) {
		exchange();
	}
	RuntimeInfrastructureBackup(const RuntimeInfrastructureBackup&) = delete;
	RuntimeInfrastructureBackup& operator=(const RuntimeInfrastructureBackup&) = delete;

	// Puts the infrastructure back. What the run holds instead is dropped with the backup.
	void restore() {
		exchange();
	}

private:
	void exchange() {
		std::swap(run_.infra, infra_);
		std::swap(run_.platforms, platforms_);
		std::swap(run_.numPlatforms, numPlatforms_);
		std::swap(run_.finalStation, finalStation_);
	}

	SimulationRun& run_;
	RunInfrastructure infra_;
	std::list<StationPlatform> platforms_;
	int numPlatforms_ = 0;
	Stations finalStation_;
};

// Checks the whole scene and decides what the runtime will hold, without reading or writing a runtime global.
// The plan is complete only when the diagnostics hold no error.
InfrastructurePlan planInfrastructure(const SceneModel& scene, std::vector<SceneDiagnostic>& diagnostics) {
	const NativeDiagnosticAdder add{diagnostics};

	std::unordered_map<std::string, const SceneTrack*> tracksById;
	std::vector<const SceneTrack*> orderedTracks;
	orderedTracks.reserve(scene.tracks.size());
	for (const auto& track : scene.tracks) {
		if (track.id.empty()) {
			add(SceneSeverity::Error, "scene.native.id.empty", "Track id is empty", "infrastructure.json", "track");
			continue;
		}
		if (!tracksById.emplace(track.id, &track).second)
			add(SceneSeverity::Error, "scene.native.id.duplicate", "Duplicate track id", "infrastructure.json", "track", track.id);
		else
			orderedTracks.push_back(&track);
	}
	std::sort(orderedTracks.begin(), orderedTracks.end(), [](const SceneTrack* left, const SceneTrack* right) {
		return left->id < right->id;
	});
	if (orderedTracks.size() > kMaxTrackLines)
		add(SceneSeverity::Error, "scene.native.capacity", "The case has more tracks than the runtime can hold",
			"infrastructure.json", "track", "", "tracks", std::to_string(kMaxTrackLines),
			"Reduce tracks or use a runtime with a larger fixed capacity");

	std::unordered_map<std::string, const SceneNode*> nodesById;
	for (const auto& node : scene.nodes) {
		if (node.id.empty()) {
			add(SceneSeverity::Error, "scene.native.id.empty", "Node id is empty", "infrastructure.json", "node");
			continue;
		}
		if (!nodesById.emplace(node.id, &node).second)
			add(SceneSeverity::Error, "scene.native.id.duplicate", "Duplicate node id", "infrastructure.json", "node", node.id);
		if (tracksById.find(node.trackId) == tracksById.end())
			add(SceneSeverity::Error, "scene.native.ref.unresolved", "Node refers to an unknown track",
				"infrastructure.json", "node", node.id, "nodes.track", node.trackId);
	}
	std::unordered_set<std::string> arcIds;
	std::unordered_set<std::string> blockIds;
	for (const auto& arc : scene.arcs) {
		if (!arcIds.insert(arc.id).second)
			add(SceneSeverity::Error, "scene.native.id.duplicate", "Duplicate arc id", "infrastructure.json", "arc", arc.id);
		const auto trackIt = tracksById.find(arc.trackId);
		const auto fromIt = nodesById.find(arc.fromNodeId);
		const auto toIt = nodesById.find(arc.toNodeId);
		if (trackIt == tracksById.end())
			add(SceneSeverity::Error, "scene.native.ref.unresolved", "Arc refers to an unknown track",
				"infrastructure.json", "arc", arc.id, "arcs.track", arc.trackId);
		if (fromIt == nodesById.end())
			add(SceneSeverity::Error, "scene.native.ref.unresolved", "Arc refers to an unknown start node",
				"infrastructure.json", "arc", arc.id, "arcs.from", arc.fromNodeId);
		if (toIt == nodesById.end())
			add(SceneSeverity::Error, "scene.native.ref.unresolved", "Arc refers to an unknown end node",
				"infrastructure.json", "arc", arc.id, "arcs.to", arc.toNodeId);
		if (fromIt != nodesById.end() && trackIt != tracksById.end()
			&& fromIt->second->trackId != arc.trackId)
			add(SceneSeverity::Error, "scene.native.topology.track", "Arc start node belongs to another track",
				"infrastructure.json", "arc", arc.id, "arcs.from", fromIt->second->trackId);
		if (toIt != nodesById.end() && trackIt != tracksById.end()
			&& toIt->second->trackId != arc.trackId)
			add(SceneSeverity::Error, "scene.native.topology.track", "Arc end node belongs to another track",
				"infrastructure.json", "arc", arc.id, "arcs.to", toIt->second->trackId);
		if (arc.fromNodeId == arc.toNodeId)
			add(SceneSeverity::Error, "scene.native.topology.loop", "Arc cannot connect a node to itself",
				"infrastructure.json", "arc", arc.id);
	}
	for (const auto& block : scene.blocks) {
		if (block.id.empty())
			add(SceneSeverity::Error, "scene.native.id.empty", "Block id is empty", "infrastructure.json", "block");
		if (block.id.find('/') != std::string::npos)
			add(SceneSeverity::Error, "scene.native.id.reserved",
				"Block id cannot contain '/' because it separates connection-derived section identities",
				"infrastructure.json", "block", block.id, "blocks.id", "/",
				"Rename the block without '/' characters");
		if (!blockIds.insert(block.id).second)
			add(SceneSeverity::Error, "scene.native.id.duplicate", "Duplicate block id", "infrastructure.json", "block", block.id);
		if (tracksById.find(block.trackId) == tracksById.end())
			add(SceneSeverity::Error, "scene.native.ref.unresolved", "Block refers to an unknown track",
				"infrastructure.json", "block", block.id, "blocks.track", block.trackId);
	}
	if (nativeHasErrors(diagnostics))
		return {};

	std::vector<NativeTrack> nativeTracks(orderedTracks.size());
	for (std::size_t slot = 0; slot < orderedTracks.size(); ++slot) {
		NativeTrack& track = nativeTracks[slot];
		track.id = orderedTracks[slot]->id;
		std::vector<const SceneNode*> nodes;
		for (const auto& node : scene.nodes)
			if (node.trackId == track.id)
				nodes.push_back(&node);
		std::vector<const SceneArc*> arcs;
		for (const auto& arc : scene.arcs)
			if (arc.trackId == track.id)
				arcs.push_back(&arc);
		if (nodes.size() < 2 || arcs.empty()) {
			add(SceneSeverity::Error, "scene.native.topology.incomplete", "Track must have at least one arc and two nodes",
				"infrastructure.json", "track", track.id);
			continue;
		}

		std::unordered_map<std::string, std::vector<const SceneArc*>> outgoing;
		std::unordered_map<std::string, int> incoming;
		for (const SceneNode* node : nodes)
			incoming[node->id] = 0;
		for (const SceneArc* arc : arcs) {
			outgoing[arc->fromNodeId].push_back(arc);
			++incoming[arc->toNodeId];
		}
		std::vector<const SceneNode*> starts;
		for (const SceneNode* node : nodes) {
			if (outgoing[node->id].size() > 1)
				add(SceneSeverity::Error, "scene.native.topology.ambiguous", "Track has multiple outgoing arcs from a node",
					"infrastructure.json", "track", track.id, "nodes", node->id);
			if (incoming[node->id] > 1)
				add(SceneSeverity::Error, "scene.native.topology.ambiguous", "Track has multiple incoming arcs to a node",
					"infrastructure.json", "track", track.id, "nodes", node->id);
			if (incoming[node->id] == 0)
				starts.push_back(node);
		}
		if (starts.size() != 1) {
			add(SceneSeverity::Error, "scene.native.topology.disconnected", "Track must have exactly one chain start",
				"infrastructure.json", "track", track.id, "tracks.nodes", std::to_string(starts.size()));
			continue;
		}
		const SceneNode* current = starts.front();
		std::unordered_set<std::string> visitedNodes;
		std::unordered_set<std::string> visitedArcs;
		while (current) {
			if (!visitedNodes.insert(current->id).second) {
				add(SceneSeverity::Error, "scene.native.topology.loop", "Track chain revisits a node",
					"infrastructure.json", "track", track.id, "nodes", current->id);
				break;
			}
			track.chainNodes.push_back(current);
			const auto next = outgoing.find(current->id);
			if (next == outgoing.end() || next->second.empty())
				break;
			const SceneArc* arc = next->second.front();
			if (!visitedArcs.insert(arc->id).second)
				break;
			track.chainArcs.push_back(arc);
			current = nodesById[arc->toNodeId];
		}
		if (track.chainArcs.size() != arcs.size() || track.chainNodes.size() != nodes.size())
			add(SceneSeverity::Error, "scene.native.topology.disconnected", "Track arcs do not form one connected linear chain",
				"infrastructure.json", "track", track.id, "tracks.arcs");
		for (std::size_t index = 1; index < track.chainNodes.size(); ++index) {
			if (track.chainNodes[index]->xKm + kNativeCoordinateTolerance < track.chainNodes[index - 1]->xKm)
				add(SceneSeverity::Error, "scene.native.topology.order", "Track node x coordinates must be nondecreasing",
					"infrastructure.json", "track", track.id, "tracks.nodes", track.chainNodes[index]->id);
		}
	}

	if (scene.connections.size() > kMaxConnections)
		add(SceneSeverity::Error, "scene.native.capacity", "The case has more connections than the runtime can hold",
			"infrastructure.json", "connection", "", "connections", std::to_string(kMaxConnections));
	if (scene.stations.size() > kMaxStations)
		add(SceneSeverity::Error, "scene.native.capacity", "The case has more stations than the runtime can hold",
			"stations.json", "station", "", "stations", std::to_string(kMaxStations));
	std::unordered_set<std::string> connectionIds;
	std::unordered_map<std::string, std::size_t> nodeConnectionCounts;
	for (const auto& connection : scene.connections) {
		if (!connectionIds.insert(connection.id).second)
			add(SceneSeverity::Error, "scene.native.id.duplicate", "Duplicate connection id",
				"infrastructure.json", "connection", connection.id);
		const auto from = nodesById.find(connection.fromNodeId);
		const auto to = nodesById.find(connection.toNodeId);
		if (from == nodesById.end() || to == nodesById.end()) {
			if (from == nodesById.end())
				add(SceneSeverity::Error, "scene.native.ref.unresolved", "Connection refers to an unknown start node",
					"infrastructure.json", "connection", connection.id, "connections.from", connection.fromNodeId);
			if (to == nodesById.end())
				add(SceneSeverity::Error, "scene.native.ref.unresolved", "Connection refers to an unknown end node",
					"infrastructure.json", "connection", connection.id, "connections.to", connection.toNodeId);
			continue;
		}
		const std::size_t fromCount = ++nodeConnectionCounts[connection.fromNodeId];
		const std::size_t toCount = ++nodeConnectionCounts[connection.toNodeId];
		if (fromCount > RuntimeLimits::kNativeMaxNodeConnections || toCount > RuntimeLimits::kNativeMaxNodeConnections)
			add(SceneSeverity::Error, "scene.native.capacity", "Node has more than six runtime connections",
				"infrastructure.json", "connection", connection.id, "connections", connection.fromNodeId,
				"Reduce connections at the node");
	}
	if (nativeHasErrors(diagnostics))
		return {};
	SceneSectionInventory sectionInventory = buildSceneSectionInventory(scene);
	std::vector<NativeBlockPlan> blockPlans;
	blockPlans.reserve(scene.blocks.size());
	std::unordered_map<std::string, int> trackSlots;
	for (std::size_t slot = 0; slot < nativeTracks.size(); ++slot)
		trackSlots[nativeTracks[slot].id] = static_cast<int>(slot);
	for (const auto& track : nativeTracks) {
		bool hasBlock = false;
		for (const auto& section : sectionInventory.sections)
			if (!section.connectionDerived && section.firstTrackId == track.id)
				hasBlock = true;
		if (!hasBlock)
			add(SceneSeverity::Error, "scene.native.topology.blocks", "Track has no block sections",
				"infrastructure.json", "track", track.id);
	}
	for (const auto& section : sectionInventory.sections) {
		if (section.connectionDerived)
			continue;
		const auto block = std::find_if(scene.blocks.begin(), scene.blocks.end(),
			[&section](const SceneBlock& candidate) { return candidate.id == section.sourceBlockId; });
		const auto slot = trackSlots.find(section.firstTrackId);
		if (block == scene.blocks.end() || slot == trackSlots.end())
			continue;
		if (section.endKm <= section.startKm + kNativeCoordinateTolerance)
			add(SceneSeverity::Error, "scene.native.topology.blocks", "Block section has no positive runtime span",
				"infrastructure.json", "block", block->id);
		if (section.layoutOverflow)
			add(SceneSeverity::Error, "scene.native.topology.blocks", "Block section extends beyond its track",
				"infrastructure.json", "block", block->id);
		if (section.clippedToTrackEnd)
			add(SceneSeverity::Warning, "scene.native.block.clipped",
				"Final block length exceeds its track and is clipped at the final node",
				"infrastructure.json", "block", block->id, "blocks.length_km", block->trackId);
		if (section.trackCoverageGap)
			add(SceneSeverity::Warning, "scene.native.block.extended",
				"Final block section is extended to cover the imported track endpoint",
				"infrastructure.json", "block", block->id, "blocks.length_km", block->trackId);
		blockPlans.push_back({&*block, slot->second, section.startKm, section.endKm});
	}
	std::unordered_set<std::string> plannedSectionIds;
	std::vector<NativeSectionPlan> signallingSectionPlans;
	signallingSectionPlans.reserve(sectionInventory.sections.size());
	for (const auto& section : sectionInventory.sections) {
		if (section.arcCount > kNativeMaxSectionArcs)
			add(SceneSeverity::Error, "scene.native.capacity", "Section exceeds the runtime arc capacity",
				"infrastructure.json", section.connectionDerived ? "connection" : "block",
				section.connectionDerived ? section.sourceConnectionId : section.sourceBlockId,
				"sections", std::to_string(kNativeMaxSectionArcs));
		if (!plannedSectionIds.insert(section.id).second) {
			add(SceneSeverity::Error, "scene.native.id.duplicate",
				"Sections produce the same runtime section ID", "infrastructure.json",
				section.connectionDerived ? "connection" : "block",
				section.connectionDerived ? section.sourceConnectionId : section.sourceBlockId,
				"sections", section.id);
			continue;
		}
		signallingSectionPlans.push_back({section.id, section.startKm, section.endKm,
			section.firstTrackId, section.secondTrackId});
	}
	auto plannedRuntimeId = [&](const std::string& reference) {
		const auto* section = sectionInventory.resolve(reference);
		return section == nullptr ? std::string() : section->id;
	};
	auto validatePlannedReference = [&](const std::string& reference, const std::string& type,
										const std::string& id, const std::string& path) {
		if (plannedRuntimeId(reference).empty())
			add(SceneSeverity::Error, "scene.native.ref.unresolved",
				"Reference does not identify a planned runtime section", "signalling.json", type, id, path, reference);
	};
	std::unordered_set<std::string> stationIds;
	std::unordered_set<std::string> platformIds;
	for (const auto& station : scene.stations) {
		if (!stationIds.insert(station.id).second)
			add(SceneSeverity::Error, "scene.native.id.duplicate", "Duplicate station id", "stations.json", "station", station.id);
		if (!station.hasPosition && station.platforms.empty())
			add(SceneSeverity::Error, "scene.native.binding.missing", "Station has neither a position nor a platform node anchor",
				"stations.json", "station", station.id);
		for (const auto& platform : station.platforms) {
			if (!platformIds.insert(station.id + "\n" + platform.id).second)
				add(SceneSeverity::Error, "scene.native.id.duplicate", "Duplicate platform id on station",
					"stations.json", "platform", platform.id, "stations.platforms", station.id);
			if (platform.nodeIds.empty())
				add(SceneSeverity::Error, "scene.native.binding.missing", "Platform has no node anchors",
					"stations.json", "platform", platform.id, "stations.platforms", station.id);
			for (const auto& nodeId : platform.nodeIds)
				if (nodesById.find(nodeId) == nodesById.end())
					add(SceneSeverity::Error, "scene.native.ref.unresolved", "Platform refers to an unknown node",
						"stations.json", "platform", platform.id, "stations.platforms.nodes", nodeId);
		}
	}

	const bool hasLegacyImport = std::any_of(scene.importReport.begin(), scene.importReport.end(),
		[](const SceneImportReportRow& row) { return row.category == "legacy_root"; });
	std::vector<int> routeDirections(scene.routes.size(), 0);
	for (std::size_t index = 0; index < scene.routes.size(); ++index) {
		const SceneRoute& route = scene.routes[index];
		std::vector<const SceneSectionDescriptor*> routeSections;
		routeSections.reserve(route.blocks.size());
		if (route.id.empty())
			add(SceneSeverity::Error, "scene.native.id.empty", "Route id is empty", "signalling.json", "route", route.id);
		if (route.blocks.empty())
			add(SceneSeverity::Error, "scene.native.route.empty", "Route has no blocks", "signalling.json", "route", route.id);
		if (route.blocks.size() > kNativeMaxRouteBlocks)
			add(SceneSeverity::Error, "scene.native.capacity", "Route exceeds the runtime block capacity",
				"signalling.json", "route", route.id, "routes.blocks", std::to_string(kNativeMaxRouteBlocks));
		for (const auto& token : route.blocks) {
			for (const auto& component : nativeBlockReferenceComponents(token))
				if (blockIds.find(component) == blockIds.end())
					add(SceneSeverity::Error, "scene.native.ref.unresolved", "Route refers to an unknown block",
						"signalling.json", "route", route.id, "routes.blocks", component);
			validatePlannedReference(token, "route", route.id, "routes.blocks");
			if (const auto* section = sectionInventory.resolve(token))
				routeSections.push_back(section);
		}
		if (routeSections.size() == route.blocks.size() && routeSections.size() > 1) {
			bool forward = false;
			bool reverse = false;
			bool directionError = false;
			for (std::size_t sectionIndex = 1; sectionIndex < routeSections.size(); ++sectionIndex) {
				const SceneSectionTransition transition = classifySceneSectionTransition(scene,
					*routeSections[sectionIndex - 1], *routeSections[sectionIndex]);
				forward = forward || transition.joinsForward;
				reverse = reverse || transition.joinsReverse;
				directionError = directionError || (forward && reverse);
				if (transition.joinsForward || transition.joinsReverse)
					continue;
				if (transition.regionJump && hasLegacyImport) {
					forward = false;
					reverse = false;
					continue;
				}
				add(SceneSeverity::Error, "scene.route.disconnected",
					"Adjacent route sections are disconnected", "signalling.json", "route", route.id,
					"routes.blocks[" + std::to_string(sectionIndex) + "]",
					route.blocks[sectionIndex - 1] + " -> " + route.blocks[sectionIndex]);
			}
			if (directionError)
				add(SceneSeverity::Error, "scene.route.direction", "Route changes direction",
					"signalling.json", "route", route.id, "routes.blocks");
			else {
				routeDirections[index] = sceneRouteDirection(scene, routeSections);
			}
		}
	}
	std::unordered_set<std::string> routeIds;
	for (const auto& route : scene.routes)
		if (!routeIds.insert(route.id).second)
			add(SceneSeverity::Error, "scene.native.id.duplicate", "Duplicate route id", "signalling.json", "route", route.id);
	for (const auto& dependency : scene.blockDependencies) {
		for (const auto& ref : {dependency.block, dependency.dependsOn}) {
			for (const auto& component : nativeBlockReferenceComponents(ref))
				if (blockIds.find(component) == blockIds.end())
					add(SceneSeverity::Error, "scene.native.ref.unresolved", "Block dependency refers to an unknown block",
						"signalling.json", "block_dependency", dependency.block, "block_dependencies", component);
			validatePlannedReference(ref, "block_dependency", dependency.block, "block_dependencies");
		}
	}
	std::unordered_map<std::string, std::unordered_set<std::string>> plannedDependencies;
	for (const auto& target : plannedSectionIds) {
		if (target.find('/') == std::string::npos)
			continue;
		for (const auto& component : nativeBlockReferenceComponents(target))
			plannedDependencies[nativeRuntimeBlockId(component)].insert(target);
	}
	for (const auto& dependency : scene.blockDependencies) {
		const std::string source = plannedRuntimeId(dependency.block);
		const std::string target = plannedRuntimeId(dependency.dependsOn);
		if (!source.empty() && !target.empty())
			plannedDependencies[source].insert(target);
	}
	for (const auto& dependency : plannedDependencies)
		if (dependency.second.size() > RuntimeLimits::kNativeMaxDependencies)
			add(SceneSeverity::Error, "scene.native.capacity", "Block dependency exceeds the runtime dependency capacity",
				"signalling.json", "block_dependency", dependency.first, "block_dependencies",
				std::to_string(RuntimeLimits::kNativeMaxDependencies));
	for (const auto& restriction : scene.singleTrackRestrictions) {
		for (const auto& ref : {restriction.startBlock, restriction.endBlock,
				 restriction.protectedStartBlock, restriction.protectedEndBlock}) {
			for (const auto& component : nativeBlockReferenceComponents(ref))
				if (blockIds.find(component) == blockIds.end())
					add(SceneSeverity::Error, "scene.native.ref.unresolved", "Single-track restriction refers to an unknown block",
						"signalling.json", "single_track_restriction", restriction.startBlock, "single_track_restrictions", component);
			validatePlannedReference(ref, "single_track_restriction", restriction.startBlock, "single_track_restrictions");
		}
	}
	for (const auto& boundary : scene.stationBoundaries) {
		for (const auto& ref : {boundary.entranceBlock, boundary.hasExitBlock ? boundary.exitBlock : std::string()}) {
			if (!ref.empty()) {
				validatePlannedReference(ref, "station_boundary", boundary.entranceBlock, "station_boundaries");
				for (const auto& component : nativeBlockReferenceComponents(ref))
					if (blockIds.find(component) == blockIds.end())
						add(SceneSeverity::Error, "scene.native.ref.unresolved", "Station boundary refers to an unknown block",
							"signalling.json", "station_boundary", boundary.entranceBlock, "station_boundaries", component);
			}
		}
	}
	std::unordered_set<std::string> signallingAreaIds;
	for (std::size_t index = 0; index < scene.signallingAreas.size(); ++index) {
		const SceneSignallingArea& area = scene.signallingAreas[index];
		const std::string path = "signalling_areas[" + std::to_string(index) + "]";
		if (area.id.empty())
			add(SceneSeverity::Error, "scene.native.id.empty", "Signalling area id is empty",
				"signalling.json", "signalling_area", area.id, path + ".id");
		else if (!signallingAreaIds.insert(area.id).second)
			add(SceneSeverity::Error, "scene.native.id.duplicate", "Duplicate signalling area id",
				"signalling.json", "signalling_area", area.id, path + ".id");
		if (!std::isfinite(area.startKm) || !std::isfinite(area.endKm)
			|| !(area.startKm < area.endKm))
			add(SceneSeverity::Error, "scene.native.signalling_area.range",
				"Signalling area coordinates must be finite with start below end",
				"signalling.json", "signalling_area", area.id, path);
		if (!isValidSignallingLevel(area.level))
			add(SceneSeverity::Error, "scene.native.signalling_area.level",
				"Signalling area level must be between 0 and 5",
				"signalling.json", "signalling_area", area.id, path + ".level");
		if (!area.trackId.empty() && tracksById.find(area.trackId) == tracksById.end())
			add(SceneSeverity::Error, "scene.native.ref.unresolved",
				"Signalling area refers to an unknown track", "signalling.json", "signalling_area",
				area.id, path + ".track", area.trackId);
	}
	auto sectionSpanInsideArea = [](const NativeSectionPlan& section, const SceneSignallingArea& area) {
		return section.startX >= area.startKm - kNativeCoordinateTolerance
			&& section.endX <= area.endKm + kNativeCoordinateTolerance;
	};
	std::unordered_map<std::string, int> plannedSignallingLevels;
	for (const auto& section : signallingSectionPlans) {
		int selectedLevel = -1;
		for (const bool trackScoped : {false, true}) {
			bool matched = false;
			bool conflict = false;
			int level = -1;
			std::string firstAreaId;
			for (const auto& area : scene.signallingAreas) {
				if (area.trackId.empty() != !trackScoped
					|| !sectionSpanInsideArea(section, area)
					|| (trackScoped && area.trackId != section.firstTrackId
						&& area.trackId != section.secondTrackId))
					continue;
				if (!matched) {
					matched = true;
					level = area.level;
					firstAreaId = area.id;
				} else if (level != area.level) {
					conflict = true;
					add(SceneSeverity::Error, "scene.native.signalling_area.conflict",
						"Multiple signalling areas assign different levels to one runtime section",
						"signalling.json", "signalling_area", area.id, "signalling_areas",
						firstAreaId + " -> " + section.id);
				}
			}
			if (matched && !conflict)
				selectedLevel = level;
		}
		if (selectedLevel >= 0)
			plannedSignallingLevels[section.id] = selectedLevel;
	}

	if (nativeHasErrors(diagnostics))
		return {};

	InfrastructurePlan plan;
	plan.tracks = std::move(nativeTracks);
	plan.blocks = std::move(blockPlans);
	plan.sectionInventory = std::move(sectionInventory);
	plan.sectionIds = std::move(plannedSectionIds);
	plan.signallingLevels = std::move(plannedSignallingLevels);
	plan.routeDirections = std::move(routeDirections);
	return plan;
}

// Replaces the runtime infrastructure by what the plan describes. A problem that only shows while the runtime is built
// is added to the diagnostics, and the runtime then holds a partial build.
void commitInfrastructure(SimulationRun& run, const SceneModel& scene, const InfrastructurePlan& plan, std::vector<SceneDiagnostic>& diagnostics) {
	const NativeDiagnosticAdder add{diagnostics};

	nativeResetRuntime(run);
	// Base sections plus derived switch sections. A successful build creates exactly this many.
	run.infra.sections = std::vector<Section>(plan.sectionIds.size());
	run.infra.numTrackLines = static_cast<int>(plan.tracks.size());
	std::unordered_map<std::string, Node> runtimeNodeById;
	std::unordered_map<std::string, int> runtimeNodeTrack;
	for (std::size_t slot = 0; slot < plan.tracks.size(); ++slot) {
		const NativeTrack& track = plan.tracks[slot];
		BlockSet& runtimeTrack = run.infra.blockSets[slot];
		runtimeTrack.ID = static_cast<int>(slot);
		runtimeTrack.sceneTrackId = track.id;
		runtimeTrack.numNodes = static_cast<int>(track.chainNodes.size());
		runtimeTrack.arcs = static_cast<int>(track.chainArcs.size());
		runtimeTrack.len = static_cast<int>(track.chainArcs.size());
		runtimeTrack.N.resize(track.chainNodes.size());
		runtimeTrack.A.resize(track.chainArcs.size());
		runtimeTrack.member.resize(track.chainArcs.size());
		const auto view = std::find_if(scene.trackViews.begin(), scene.trackViews.end(),
			[&track](const SceneTrackView& candidate) { return candidate.trackId == track.id; });
		if (view != scene.trackViews.end()) {
			runtimeTrack.graphID = view->level;
			runtimeTrack.region = view->region;
			runtimeTrack.hasGraphLayout = true;
		}
		for (std::size_t index = 0; index < track.chainNodes.size(); ++index) {
			Node node;
			node.sceneNodeId = track.chainNodes[index]->id;
			node.ID = static_cast<double>(slot * 1000000 + index + 1);
			node.X = track.chainNodes[index]->xKm;
			node.Y = track.chainNodes[index]->yKm;
			runtimeNodeTrack[track.chainNodes[index]->id] = static_cast<int>(slot);
			runtimeTrack.N[index] = node;
			runtimeNodeById[track.chainNodes[index]->id] = runtimeTrack.N[index];
		}
		for (std::size_t index = 0; index < track.chainArcs.size(); ++index) {
			const SceneArc& source = *track.chainArcs[index];
			Arc arc;
			arc.ID = static_cast<double>(index);
			arc.startNode = runtimeTrack.N[index];
			arc.endNode = runtimeTrack.N[index + 1];
			arc.curvature = source.curvatureRadiusM;
			arc.gradient = source.gradientPercent;
			arc.speedLimit = source.speedLimitMs;
			arc.fs = arc.endNode.X * 1000.0;
			arc.arcLength();
			runtimeTrack.A[index] = arc;
			runtimeTrack.member[index] = arc;
		}
	}

	auto updateRuntimeNode = [&](const std::string& nodeId, const auto& update) {
		const auto trackIt = runtimeNodeTrack.find(nodeId);
		if (trackIt == runtimeNodeTrack.end())
			return;
		const int slot = trackIt->second;
		const double targetId = runtimeNodeById[nodeId].ID;
		for (int index = 0; index < run.infra.blockSets[slot].numNodes; ++index)
			if (run.infra.blockSets[slot].N[index].ID == targetId)
				update(run.infra.blockSets[slot].N[index]);
		for (int index = 0; index < run.infra.blockSets[slot].arcs; ++index) {
			if (run.infra.blockSets[slot].A[index].startNode.ID == targetId)
				update(run.infra.blockSets[slot].A[index].startNode);
			if (run.infra.blockSets[slot].A[index].endNode.ID == targetId)
				update(run.infra.blockSets[slot].A[index].endNode);
			if (run.infra.blockSets[slot].member[index].startNode.ID == targetId)
				update(run.infra.blockSets[slot].member[index].startNode);
			if (run.infra.blockSets[slot].member[index].endNode.ID == targetId)
				update(run.infra.blockSets[slot].member[index].endNode);
		}
		for (int index = 0; index < run.infra.blockSets[slot].numNodes; ++index)
			if (run.infra.blockSets[slot].N[index].ID == targetId)
				runtimeNodeById[nodeId] = run.infra.blockSets[slot].N[index];
	};

	for (std::size_t index = 0; index < scene.connections.size(); ++index) {
		const SceneConnection& source = scene.connections[index];
		Connections& connection = run.infra.connections[index];
		const int firstSlot = runtimeNodeTrack[source.fromNodeId];
		const int secondSlot = runtimeNodeTrack[source.toNodeId];
		connection.ID = source.id;
		const Node& fromNode = runtimeNodeById[source.fromNodeId];
		const Node& toNode = runtimeNodeById[source.toNodeId];
		const bool fromFirst = fromNode.X <= toNode.X;
		connection.idFirstTrackLine = fromFirst ? firstSlot : secondSlot;
		connection.idSecondTrackLine = fromFirst ? secondSlot : firstSlot;
		connection.sceneFirstNodeId = fromFirst ? source.fromNodeId : source.toNodeId;
		connection.sceneSecondNodeId = fromFirst ? source.toNodeId : source.fromNodeId;
		connection.xFirstNode = fromFirst ? fromNode.X : toNode.X;
		connection.xSecondNode = fromFirst ? toNode.X : fromNode.X;
		connection.speedlimit = source.hasSpeedLimit ? source.speedLimitMs : 16.667;
		connection.graphXFirstNode = fromFirst ? fromNode.graphX : toNode.graphX;
		connection.graphYFirstNode = fromFirst ? fromNode.graphY : toNode.graphY;
		connection.graphXSecondNode = fromFirst ? toNode.graphX : fromNode.graphX;
		connection.graphYSecondNode = fromFirst ? toNode.graphY : fromNode.graphY;
		updateRuntimeNode(source.fromNodeId, [&](Node& node) {
			const int count = node.numConnections++;
			node.connectIdBlockSet[count] = secondSlot;
			node.connectXNode[count] = runtimeNodeById[source.toNodeId].X;
		});
		updateRuntimeNode(source.toNodeId, [&](Node& node) {
			const int count = node.numConnections++;
			node.connectIdBlockSet[count] = firstSlot;
			node.connectXNode[count] = runtimeNodeById[source.fromNodeId].X;
		});
	}
	run.infra.numConnections = static_cast<int>(scene.connections.size());

	std::vector<NativeStationBinding> stationBindings;
	std::unordered_set<std::string> stationNodeAssignments;
	for (const auto& station : scene.stations) {
		if (!station.platforms.empty()) {
			for (const auto& platform : station.platforms) {
				for (const auto& nodeId : platform.nodeIds) {
					const std::string key = nodeId + "\n" + station.id + "\n" + platform.id;
					if (!stationNodeAssignments.insert(key).second)
						continue;
					stationBindings.push_back({&station, &platform, nodeId});
					updateRuntimeNode(nodeId, [&](Node& node) {
						node.station = true;
						node.stationName = station.name.empty() ? station.id : station.name;
						node.stationPlatformId = platform.id;
					});
				}
			}
		} else if (station.hasPosition) {
			bool matched = false;
			for (const auto& node : scene.nodes) {
				if (std::fabs(node.xKm - station.positionKm) > kNativeCoordinateTolerance)
					continue;
				matched = true;
				updateRuntimeNode(node.id, [&](Node& runtimeNode) {
					runtimeNode.station = true;
					runtimeNode.stationName = station.name.empty() ? station.id : station.name;
				});
			}
			if (!matched)
				add(SceneSeverity::Warning, "scene.native.binding.position", "Station position has no exact node anchor",
					"stations.json", "station", station.id, "stations.position_km");
		}
	}


	auto nodeAtArcCoordinate = [&](const Arc& arc, double x) {
		if (std::fabs(x - arc.startNode.X) <= kNativeCoordinateTolerance)
			return arc.startNode;
		if (std::fabs(x - arc.endNode.X) <= kNativeCoordinateTolerance)
			return arc.endNode;
		Node node;
		node.ID = arc.startNode.ID + (x - arc.startNode.X) * 0.000001;
		const double span = arc.endNode.X - arc.startNode.X;
		const double ratio = span == 0.0 ? 0.0 : (x - arc.startNode.X) / span;
		node.X = x;
		node.Y = arc.startNode.Y + ratio * (arc.endNode.Y - arc.startNode.Y);
		return node;
	};

	for (const NativeBlockPlan& blockPlan : plan.blocks) {
		BlockSet& runtimeTrack = run.infra.blockSets[blockPlan.trackSlot];
		Section& section = run.infra.sections[run.infra.numBlocks++];
		section.ID = nativeRuntimeBlockId(blockPlan.source->id);
		section.trackLineId = blockPlan.trackSlot;
		section.length = blockPlan.endX - blockPlan.startX;
		section.start_node = Node();
		section.end_node = Node();
		std::vector<Node> nodes;
		std::vector<Arc> arcs;
		for (int arcIndex = 0; arcIndex < runtimeTrack.arcs; ++arcIndex) {
			const Arc& sourceArc = runtimeTrack.A[arcIndex];
			if (sourceArc.endNode.X <= blockPlan.startX + kNativeCoordinateTolerance
				|| sourceArc.startNode.X >= blockPlan.endX - kNativeCoordinateTolerance)
				continue;
			const double beginX = std::max(blockPlan.startX, sourceArc.startNode.X);
			const double endX = std::min(blockPlan.endX, sourceArc.endNode.X);
			if (endX <= beginX + kNativeCoordinateTolerance)
				continue;
			Arc piece = sourceArc;
			piece.startNode = nodeAtArcCoordinate(sourceArc, beginX);
			piece.endNode = nodeAtArcCoordinate(sourceArc, endX);
			piece.length = piece.endNode.X - piece.startNode.X;
			piece.fs = piece.endNode.X * 1000.0;
			if (nodes.empty())
				nodes.push_back(piece.startNode);
			else if (nodes.back().ID != piece.startNode.ID)
				nodes.push_back(piece.startNode);
			arcs.push_back(piece);
			nodes.push_back(piece.endNode);
		}
		if (nodes.empty()) {
			add(SceneSeverity::Error, "scene.native.topology.blocks", "Block section could not be mapped to track arcs",
				"infrastructure.json", "block", blockPlan.source->id);
			continue;
		}
		section.start_node = nodes.front();
		section.end_node = nodes.back();
		section.total_nodes = static_cast<int>(nodes.size());
		section.total_arcs = static_cast<int>(arcs.size());
		section.nodelist_of_nodes_in_signalling_section = std::make_shared<const std::vector<Node>>(std::move(nodes));
		for (std::size_t index = 0; index < arcs.size(); ++index)
			section.arcs_in_signalling_block_section[index] = arcs[index];
	}

	for (std::size_t stationIndex = 0; stationIndex < scene.stations.size(); ++stationIndex) {
		const SceneStation& source = scene.stations[stationIndex];
		Stations& station = run.infra.stations[stationIndex];
		station.stationName = source.name.empty() ? source.id : source.name;
		station.station = true;
		if (source.hasPosition)
			station.X = source.positionKm;
		else if (!source.platforms.empty() && !source.platforms.front().nodeIds.empty()) {
			const Node& anchor = runtimeNodeById[source.platforms.front().nodeIds.front()];
			station.X = anchor.X;
			station.Y = anchor.Y;
		}
		station.N_StationPlatforms = static_cast<int>(source.platforms.size());
		for (const auto& platform : source.platforms)
			station.StationPlatformIDs.push_back(platform.id);
		const auto view = std::find_if(scene.stationViews.begin(), scene.stationViews.end(),
			[&source](const SceneStationView& candidate) { return candidate.stationId == source.id; });
		if (view != scene.stationViews.end()) {
			station.latitude = view->latitude;
			station.longitude = view->longitude;
			for (const auto& region : view->regions) {
				station.regions.push_back(region.first);
				station.regionX[region.first] = region.second;
			}
			station.corridors = view->corridors;
		}
	}
	run.infra.numStations = static_cast<int>(scene.stations.size());
	for (const NativeStationBinding& binding : stationBindings) {
		const Node& node = runtimeNodeById[binding.nodeId];
		StationPlatform platform;
		platform.ID = binding.platform->id;
		platform.StationID = binding.station->id;
		platform.BlockSectionID.clear();
		platform.X = node.X;
		platform.Y = node.Y;
		for (int blockIndex = 0; blockIndex < run.infra.numBlocks; ++blockIndex) {
			const Section& section = run.infra.sections[blockIndex];
			for (int nodeIndex = 0; nodeIndex < section.total_nodes; ++nodeIndex) {
				if ((*section.nodelist_of_nodes_in_signalling_section)[nodeIndex].ID == node.ID) {
					platform.BlockSectionID = section.ID;
					break;
				}
			}
			if (!platform.BlockSectionID.empty())
				break;
		}
		if (platform.BlockSectionID.empty()) {
			add(SceneSeverity::Error, "scene.native.binding.platform", "Platform node is not part of a block section",
				"stations.json", "platform", binding.platform->id, "stations.platforms.nodes", binding.nodeId);
		} else {
			run.platforms.push_back(platform);
			++run.numPlatforms;
		}
	}

	setAllConnections(run);
	std::unordered_map<std::string, int> sectionAliases;
	for (int sectionIndex = 0; sectionIndex < run.infra.numBlocks; ++sectionIndex)
		sectionAliases[run.infra.sections[sectionIndex].ID] = sectionIndex;
	const bool sectionMismatch = run.infra.derivedSectionsExceedPlan
		|| sectionAliases.size() != static_cast<std::size_t>(run.infra.numBlocks)
		|| sectionAliases.size() != plan.sectionIds.size()
		|| std::any_of(plan.sectionIds.begin(), plan.sectionIds.end(),
			[&sectionAliases](const std::string& id) { return sectionAliases.count(id) == 0; });
	if (sectionMismatch) {
		add(SceneSeverity::Error, "scene.native.sections.mismatch",
			"Legacy connection construction produced section IDs different from the planned inventory",
			"infrastructure.json", "section", "", "sections",
			"planned=" + std::to_string(plan.sectionIds.size())
				+ ", actual=" + std::to_string(sectionAliases.size()),
			"Fix topology or block placement so native section identities match the section catalog");
		return;
	}
	for (int sectionIndex = 0; sectionIndex < run.infra.numBlocks; ++sectionIndex) {
		const auto plannedLevel = plan.signallingLevels.find(run.infra.sections[sectionIndex].ID);
		if (plannedLevel != plan.signallingLevels.end())
			run.infra.sections[sectionIndex].SignallingLevel = plannedLevel->second;
	}
	if (nativeHasErrors(diagnostics))
		return;
	setDependenciesBetweenBlocksInternal(run, false);

	auto resolveSection = [&](const std::string& reference) -> int {
		const auto* planned = plan.sectionInventory.resolve(reference);
		if (planned == nullptr)
			return -1;
		const auto section = sectionAliases.find(planned->id);
		return section == sectionAliases.end() ? -1 : section->second;
	};
	auto addSectionDependency = [&](const std::string& sourceRef, const std::string& targetRef,
									const std::string& itemId) {
		const int sourceIndex = resolveSection(sourceRef);
		const int targetIndex = resolveSection(targetRef);
		if (sourceIndex < 0 || targetIndex < 0) {
			add(SceneSeverity::Error, "scene.native.ref.unresolved", "Block dependency could not resolve a runtime section",
				"signalling.json", "block_dependency", itemId, "block_dependencies", sourceIndex < 0 ? sourceRef : targetRef);
			return;
		}
		Section& source = run.infra.sections[sourceIndex];
		const std::string target = run.infra.sections[targetIndex].ID;
		for (int index = 0; index < source.N_ConnectedBS; ++index)
			if (source.IDConnectedBS[index] == target)
				return;
		if (static_cast<std::size_t>(source.N_ConnectedBS) >= RuntimeLimits::kNativeMaxDependencies) {
			add(SceneSeverity::Error, "scene.native.capacity", "Block dependency exceeds the runtime dependency capacity",
				"signalling.json", "block_dependency", itemId, "block_dependencies", target);
			return;
		}
		source.IDConnectedBS[source.N_ConnectedBS++] = target;
	};
	for (const auto& dependency : scene.blockDependencies)
		addSectionDependency(dependency.block, dependency.dependsOn, dependency.block);
	for (int sectionIndex = 0; sectionIndex < run.infra.numBlocks; ++sectionIndex) {
		Section& section = run.infra.sections[sectionIndex];
		for (int arcIndex = 0; arcIndex < section.total_arcs; ++arcIndex) {
			Node& node = section.arcs_in_signalling_block_section[arcIndex].endNode;
			node.IDConnectedBlocks.clear();
			for (int connectionIndex = 0; connectionIndex < node.numConnections; ++connectionIndex) {
				for (int dependencyIndex = 0; dependencyIndex < section.N_ConnectedBS; ++dependencyIndex) {
					const int connectedIndex = resolveSection(section.IDConnectedBS[dependencyIndex]);
					if (connectedIndex < 0)
						continue;
					const Section& connected = run.infra.sections[connectedIndex];
					const bool fromFirst = connected.FirstConnectedTrackLineID == section.trackLineId
						&& connected.SecondConnectedTrackLineID == node.connectIdBlockSet[connectionIndex]
						&& std::fabs(connected.XStartSwitch - node.X) <= kNativeCoordinateTolerance
						&& std::fabs(connected.XEndSwitch - node.connectXNode[connectionIndex]) <= kNativeCoordinateTolerance;
					const bool fromSecond = connected.SecondConnectedTrackLineID == section.trackLineId
						&& connected.FirstConnectedTrackLineID == node.connectIdBlockSet[connectionIndex]
						&& std::fabs(connected.XEndSwitch - node.X) <= kNativeCoordinateTolerance
						&& std::fabs(connected.XStartSwitch - node.connectXNode[connectionIndex]) <= kNativeCoordinateTolerance;
					if (fromFirst || fromSecond) {
						node.IDConnectedBlocks.push_back(connected.ID);
						break;
					}
				}
			}
		}
	}

	for (const auto& restriction : scene.singleTrackRestrictions) {
		const int start = resolveSection(restriction.startBlock);
		const int end = resolveSection(restriction.endBlock);
		const int protectedStart = resolveSection(restriction.protectedStartBlock);
		const int protectedEnd = resolveSection(restriction.protectedEndBlock);
		if (start < 0 || end < 0 || protectedStart < 0 || protectedEnd < 0) {
			add(SceneSeverity::Error, "scene.native.ref.unresolved", "Single-track restriction could not resolve a runtime section",
				"signalling.json", "single_track_restriction", restriction.startBlock);
			continue;
		}
		run.infra.singleTrackLimits.emplace_back(run.infra.sections[start].ID, run.infra.sections[end].ID,
			run.infra.sections[protectedStart].ID, run.infra.sections[protectedEnd].ID);
	}
	for (const auto& boundary : scene.stationBoundaries) {
		const int entrance = resolveSection(boundary.entranceBlock);
		const int exit = boundary.hasExitBlock ? resolveSection(boundary.exitBlock) : -1;
		if (entrance < 0 || (boundary.hasExitBlock && exit < 0)) {
			add(SceneSeverity::Error, "scene.native.ref.unresolved", "Station boundary could not resolve a runtime section",
				"signalling.json", "station_boundary", boundary.entranceBlock);
			continue;
		}
		run.infra.stationBoundaries.emplace_back(&run.infra.sections[entrance], boundary.direction,
			boundary.hasExitBlock ? &run.infra.sections[exit] : nullptr);
	}

	setVirtualSignals(run);
	setTrackDetectionSectionBoundariesAndGeoCoordAtSwitchesAndStations(run.infra.sections.data(), run.infra.numBlocks);
	setGeoCoordinates(run.infra.sections.data(), run.infra.numBlocks);
	createTds(run.infra.sections.data(), run.infra.numBlocks, 3, run.infra.detectionSections);
	for (const auto& route : scene.routes)
		for (const auto& token : route.blocks)
			if (resolveSection(token) < 0)
				add(SceneSeverity::Error, "scene.native.ref.unresolved", "Route token has no derived runtime section",
					"signalling.json", "route", route.id, "routes.blocks", token);
	if (nativeHasErrors(diagnostics))
		return;
	setUpRoutesFromScene(run, scene, plan.routeDirections);
	for (std::size_t index = 0; index < scene.routes.size(); ++index) {
		if (index >= run.infra.routes.size()
			|| run.infra.routes[index].N_Block_Sections != static_cast<int>(scene.routes[index].blocks.size())) {
			add(SceneSeverity::Error, "scene.native.route.truncated", "Route did not retain every canonical block token",
				"signalling.json", "route", scene.routes[index].id, "routes.blocks");
			continue;
		}
		for (std::size_t blockIndex = 0; blockIndex < scene.routes[index].blocks.size(); ++blockIndex) {
			const auto* planned = plan.sectionInventory.resolve(scene.routes[index].blocks[blockIndex]);
			if (planned == nullptr || run.infra.routes[index].sequence_of_block_sections[blockIndex].ID != planned->id)
				add(SceneSeverity::Error, "scene.native.route.order",
					"Native route order does not retain the authored section IDs",
					"signalling.json", "route", scene.routes[index].id,
					"routes.blocks[" + std::to_string(blockIndex) + "]",
					planned == nullptr ? scene.routes[index].blocks[blockIndex] : planned->id,
					"Keep route tokens in authored order and use exact catalog IDs");
		}
	}
	setRouteVirtualSignals(run);
	setListAllInfrastructureElementsFromRoutes(run);
}

} // namespace

void resetNativeInfrastructureState(SimulationRun& run) {
	nativeResetRuntime(run);
}

std::vector<SceneDiagnostic> buildInfrastructureAndSignallingFromScene(SimulationRun& run, const SceneModel& scene) {
	std::vector<SceneDiagnostic> diagnostics;
	const InfrastructurePlan plan = planInfrastructure(scene, diagnostics);
	if (nativeHasErrors(diagnostics))
		return diagnostics;
	RuntimeInfrastructureBackup previous(run);
	commitInfrastructure(run, scene, plan, diagnostics);
	if (nativeHasErrors(diagnostics))
		previous.restore();
	return diagnostics;
}
