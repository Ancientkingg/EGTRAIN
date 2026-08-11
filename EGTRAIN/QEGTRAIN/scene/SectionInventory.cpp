#include "scene/SectionInventory.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace {

constexpr double kCoordinateTolerance = 1e-8;

std::string runtimeBlockId(const std::string& id) {
	if (id.empty() || id.front() == '@' || id.find('/') != std::string::npos)
		return id;
	return "@" + id + "@";
}

struct TrackChain {
	std::vector<const SceneNode*> nodes;
	std::vector<const SceneArc*> arcs;
};

TrackChain chainForTrack(const SceneModel& scene, const SceneTrack& track,
		const std::unordered_map<std::string, const SceneNode*>& nodesById) {
	TrackChain result;
	for (const auto& node : scene.nodes)
		if (node.trackId == track.id)
			result.nodes.push_back(&node);
	for (const auto& arc : scene.arcs)
		if (arc.trackId == track.id)
			result.arcs.push_back(&arc);
	if (result.nodes.empty() || result.arcs.empty())
		return {};

	std::unordered_map<std::string, std::vector<const SceneArc*>> outgoing;
	std::unordered_map<std::string, int> incoming;
	for (const auto* node : result.nodes)
		incoming[node->id] = 0;
	for (const auto* arc : result.arcs) {
		outgoing[arc->fromNodeId].push_back(arc);
		++incoming[arc->toNodeId];
	}
	const SceneNode* start = nullptr;
	for (const auto* node : result.nodes)
		if (incoming[node->id] == 0) {
			if (start != nullptr)
				return {};
			start = node;
		}
	if (start == nullptr)
		return {};

	std::vector<const SceneNode*> nodes;
	std::vector<const SceneArc*> arcs;
	std::unordered_map<std::string, bool> visitedNodes;
	std::unordered_map<std::string, bool> visitedArcs;
	for (const SceneNode* current = start; current != nullptr;) {
		if (visitedNodes[current->id])
			return {};
		visitedNodes[current->id] = true;
		nodes.push_back(current);
		const auto next = outgoing.find(current->id);
		if (next == outgoing.end() || next->second.empty())
			break;
		if (next->second.size() != 1 || visitedArcs[next->second.front()->id])
			return {};
		const SceneArc* arc = next->second.front();
		const auto from = nodesById.find(arc->fromNodeId);
		const auto to = nodesById.find(arc->toNodeId);
		if (from == nodesById.end() || to == nodesById.end())
			return {};
		visitedArcs[arc->id] = true;
		arcs.push_back(arc);
		current = to->second;
	}
	if (nodes.size() != result.nodes.size() || arcs.size() != result.arcs.size())
		return {};
	result.nodes = std::move(nodes);
	result.arcs = std::move(arcs);
	return result;
}

std::string nodeAt(const TrackChain& chain, double coordinate) {
	for (const SceneNode* node : chain.nodes)
		if (std::fabs(node->xKm - coordinate) <= kCoordinateTolerance)
			return node->id;
	return {};
}

std::string sectionStartTrack(const SceneSectionDescriptor& section) {
	return section.firstTrackId;
}

std::string sectionEndTrack(const SceneSectionDescriptor& section) {
	return section.secondTrackId.empty() ? section.firstTrackId : section.secondTrackId;
}

std::string sectionBoundaryNode(const SceneSectionDescriptor& section, bool forward, bool exit) {
	return exit == forward ? section.endNodeId : section.startNodeId;
}

bool sectionBoundaryJoins(const SceneModel& scene, const SceneSectionDescriptor& left,
		const SceneSectionDescriptor& right, bool forward) {
	const double leftCoordinate = forward ? left.endKm : left.startKm;
	const double rightCoordinate = forward ? right.startKm : right.endKm;
	const std::string leftTrack = forward ? sectionEndTrack(left) : sectionStartTrack(left);
	const std::string rightTrack = forward ? sectionStartTrack(right) : sectionEndTrack(right);
	if (leftTrack == rightTrack && std::fabs(leftCoordinate - rightCoordinate) <= kCoordinateTolerance)
		return true;
	const std::string leftNode = sectionBoundaryNode(left, forward, true);
	const std::string rightNode = sectionBoundaryNode(right, forward, false);
	if (leftTrack == rightTrack || leftNode.empty() || rightNode.empty())
		return false;
	for (const auto& connection : scene.connections) {
		if (connection.id == left.sourceConnectionId || connection.id == right.sourceConnectionId)
			continue;
		if ((connection.fromNodeId == leftNode && connection.toNodeId == rightNode)
				|| (connection.toNodeId == leftNode && connection.fromNodeId == rightNode))
			return true;
	}
	return false;
}

bool sectionBoundaryIsRegionJump(const SceneSectionDescriptor& left,
		const SceneSectionDescriptor& right, bool forward) {
	const double leftCoordinate = forward ? left.endKm : left.startKm;
	const double rightCoordinate = forward ? right.startKm : right.endKm;
	const std::string leftTrack = forward ? sectionEndTrack(left) : sectionStartTrack(left);
	const std::string rightTrack = forward ? sectionStartTrack(right) : sectionEndTrack(right);
	return leftTrack != rightTrack
			&& std::fabs(leftCoordinate - rightCoordinate) > kCoordinateTolerance;
}

} // namespace

std::string formatSceneSectionCoordinate(double coordinate) {
	const int length = std::snprintf(nullptr, 0, "%f", coordinate);
	if (length <= 0)
		return {};
	std::string result(static_cast<std::size_t>(length) + 1, '\0');
	std::snprintf(result.data(), result.size(), "%f", coordinate);
	result.resize(static_cast<std::size_t>(length));
	return result;
}

const SceneSectionDescriptor* SceneSectionInventory::exact(const std::string& runtimeId) const {
	const SceneSectionDescriptor* result = nullptr;
	for (const auto& section : sections) {
		if (section.id != runtimeId)
			continue;
		if (result != nullptr)
			return nullptr;
		result = &section;
	}
	return result;
}

bool SceneSectionInventory::ambiguous(const std::string& reference) const {
	std::size_t count = 0;
	const std::string wrapped = runtimeBlockId(reference);
	for (const auto& section : sections) {
		const bool exactMatch = section.id == reference;
		const bool baseAlias = reference.find('/') == std::string::npos
				&& (section.sourceBlockId == reference || section.id == wrapped);
		if (exactMatch || baseAlias)
			++count;
	}
	return count > 1;
}

const SceneSectionDescriptor* SceneSectionInventory::resolve(const std::string& reference) const {
	if (reference.empty() || ambiguous(reference))
		return nullptr;
	if (const auto* section = exact(reference))
		return section;
	if (reference.find('/') != std::string::npos)
		return nullptr;
	const std::string wrapped = runtimeBlockId(reference);
	const SceneSectionDescriptor* result = nullptr;
	for (const auto& section : sections) {
		if (section.sourceBlockId == reference || section.id == wrapped) {
			if (result != nullptr)
				return nullptr;
			result = &section;
		}
	}
	return result;
}

SceneSectionTransition classifySceneSectionTransition(const SceneModel& scene,
		const SceneSectionDescriptor& left, const SceneSectionDescriptor& right) {
	SceneSectionTransition transition;
	transition.joinsForward = sectionBoundaryJoins(scene, left, right, true);
	transition.joinsReverse = sectionBoundaryJoins(scene, left, right, false);
	if (left.connectionDerived && right.connectionDerived) {
		const bool switchForward = left.secondBlockId == right.firstBlockId
						&& left.secondTrackId == right.firstTrackId
						&& left.secondConnectionKm <= right.firstConnectionKm + kCoordinateTolerance;
		const bool switchReverse = left.firstBlockId == right.secondBlockId
						&& left.firstTrackId == right.secondTrackId
						&& right.secondConnectionKm <= left.firstConnectionKm + kCoordinateTolerance;
		transition.joinsForward = transition.joinsForward || switchForward;
		transition.joinsReverse = transition.joinsReverse || switchReverse;
	}
	const auto rightUsesBlock = [&right](const std::string& id) {
		return !id.empty() && (right.sourceBlockId == id
				|| right.firstBlockId == id || right.secondBlockId == id);
	};
	const bool sharesSourceBlock = rightUsesBlock(left.sourceBlockId)
			|| rightUsesBlock(left.firstBlockId) || rightUsesBlock(left.secondBlockId);
	transition.regionJump = !sharesSourceBlock
			&& (sectionBoundaryIsRegionJump(left, right, true)
			|| sectionBoundaryIsRegionJump(left, right, false));
	return transition;
}

SceneSectionInventory buildSceneSectionInventory(const SceneModel& scene) {
	SceneSectionInventory inventory;
	std::unordered_map<std::string, const SceneNode*> nodesById;
	for (const auto& node : scene.nodes)
		nodesById.emplace(node.id, &node);
	std::vector<const SceneTrack*> orderedTracks;
	orderedTracks.reserve(scene.tracks.size());
	for (const auto& track : scene.tracks)
		orderedTracks.push_back(&track);
	std::sort(orderedTracks.begin(), orderedTracks.end(), [](const SceneTrack* left,
			const SceneTrack* right) { return left->id < right->id; });
	std::unordered_map<std::string, TrackChain> chains;
	for (const SceneTrack* track : orderedTracks)
		chains.emplace(track->id, chainForTrack(scene, *track, nodesById));

	struct BlockPlan {
		const SceneBlock* source = nullptr;
		const TrackChain* chain = nullptr;
		double startKm = 0.0;
		double endKm = 0.0;
		bool layoutOverflow = false;
		bool trackCoverageGap = false;
		bool clippedToTrackEnd = false;
	};
	std::vector<BlockPlan> plans;
	for (const SceneTrack* track : orderedTracks) {
		const auto chainIt = chains.find(track->id);
		if (chainIt == chains.end() || chainIt->second.nodes.empty())
			continue;
		std::vector<const SceneBlock*> blocks;
		for (const auto& block : scene.blocks)
			if (block.trackId == track->id)
				blocks.push_back(&block);
		if (blocks.empty())
			continue;
		double cursor = chainIt->second.nodes.front()->xKm;
		const double trackEnd = chainIt->second.nodes.back()->xKm;
		for (const SceneBlock* block : blocks) {
			double end = cursor + block->lengthKm;
			const bool overshootsTrack = end > trackEnd + kCoordinateTolerance;
			const bool clippedToTrackEnd = overshootsTrack && block == blocks.back()
					&& cursor < trackEnd - kCoordinateTolerance;
			if (clippedToTrackEnd)
				end = trackEnd;
			plans.push_back({block, &chainIt->second, cursor, std::min(end, trackEnd)});
			cursor += block->lengthKm;
			if (overshootsTrack && !clippedToTrackEnd)
				plans.back().layoutOverflow = true;
			plans.back().clippedToTrackEnd = clippedToTrackEnd;
		}
		if (cursor < trackEnd - kCoordinateTolerance && !plans.empty()
				&& plans.back().chain == &chainIt->second) {
			plans.back().endKm = trackEnd;
			plans.back().trackCoverageGap = true;
		}
	}

	for (const BlockPlan& plan : plans) {
		SceneSectionDescriptor section;
		section.id = runtimeBlockId(plan.source->id);
		section.sourceBlockId = plan.source->id;
		section.firstTrackId = plan.source->trackId;
		section.startKm = plan.startKm;
		section.endKm = plan.endKm;
		section.layoutOverflow = plan.layoutOverflow;
		section.trackCoverageGap = plan.trackCoverageGap;
		section.clippedToTrackEnd = plan.clippedToTrackEnd;
		section.startNodeId = nodeAt(*plan.chain, plan.startKm);
		section.endNodeId = nodeAt(*plan.chain, plan.endKm);
		for (const SceneNode* node : plan.chain->nodes)
			if (node->xKm >= plan.startKm - kCoordinateTolerance
					&& node->xKm <= plan.endKm + kCoordinateTolerance)
				section.nodeIds.push_back(node->id);
		for (const SceneArc* arc : plan.chain->arcs)
			if (arc->toNodeId.empty() == false && arc->fromNodeId.empty() == false
					&& nodesById.count(arc->fromNodeId) != 0 && nodesById.count(arc->toNodeId) != 0
					&& nodesById.at(arc->toNodeId)->xKm > plan.startKm + kCoordinateTolerance
					&& nodesById.at(arc->fromNodeId)->xKm < plan.endKm - kCoordinateTolerance)
				++section.arcCount;
		inventory.sections.push_back(std::move(section));
	}

	for (const auto& connection : scene.connections) {
		const auto from = nodesById.find(connection.fromNodeId);
		const auto to = nodesById.find(connection.toNodeId);
		if (from == nodesById.end() || to == nodesById.end())
			continue;
		const SceneNode* first = from->second;
		const SceneNode* second = to->second;
		if (first->xKm > second->xKm)
			std::swap(first, second);
		if (first->xKm >= second->xKm)
			continue;
		const std::string firstX = formatSceneSectionCoordinate(first->xKm);
		const std::string secondX = formatSceneSectionCoordinate(second->xKm);
		for (const BlockPlan& firstPlan : plans) {
			if (firstPlan.source->trackId != first->trackId
					|| first->xKm < firstPlan.startKm - kCoordinateTolerance
					|| first->xKm > firstPlan.endKm + kCoordinateTolerance)
				continue;
			for (const BlockPlan& secondPlan : plans) {
				if (secondPlan.source->trackId != second->trackId
						|| second->xKm < secondPlan.startKm - kCoordinateTolerance
						|| second->xKm > secondPlan.endKm + kCoordinateTolerance)
					continue;
				SceneSectionDescriptor section;
				section.id = runtimeBlockId(firstPlan.source->id) + "-" + firstX + "/"
						+ runtimeBlockId(secondPlan.source->id) + "-" + secondX;
				section.sourceConnectionId = connection.id;
				section.firstBlockId = firstPlan.source->id;
				section.secondBlockId = secondPlan.source->id;
				section.firstTrackId = firstPlan.source->trackId;
				section.secondTrackId = secondPlan.source->trackId;
				section.startKm = firstPlan.startKm;
				section.endKm = secondPlan.endKm;
				section.firstConnectionKm = first->xKm;
				section.secondConnectionKm = second->xKm;
				section.startNodeId = nodeAt(*firstPlan.chain, firstPlan.startKm);
				section.endNodeId = nodeAt(*secondPlan.chain, secondPlan.endKm);
				for (const SceneNode* node : firstPlan.chain->nodes)
					if (node->xKm <= first->xKm + kCoordinateTolerance
							&& node->xKm >= firstPlan.startKm - kCoordinateTolerance)
						section.nodeIds.push_back(node->id);
				section.nodeIds.push_back(second->id);
				for (const SceneNode* node : secondPlan.chain->nodes)
					if (node->xKm > second->xKm + kCoordinateTolerance
							&& node->xKm <= secondPlan.endKm + kCoordinateTolerance)
						section.nodeIds.push_back(node->id);
				section.connectionDerived = true;
				section.arcCount = 1;
				for (const SceneArc* arc : firstPlan.chain->arcs) {
					const double begin = std::max(firstPlan.startKm, nodesById.at(arc->fromNodeId)->xKm);
					const double end = std::min(firstPlan.endKm, nodesById.at(arc->toNodeId)->xKm);
					if (end > begin + kCoordinateTolerance
							&& end <= first->xKm + kCoordinateTolerance)
						++section.arcCount;
				}
				for (const SceneArc* arc : secondPlan.chain->arcs) {
					const double begin = std::max(secondPlan.startKm, nodesById.at(arc->fromNodeId)->xKm);
					const double end = std::min(secondPlan.endKm, nodesById.at(arc->toNodeId)->xKm);
					if (end > begin + kCoordinateTolerance
							&& end > second->xKm + kCoordinateTolerance)
						++section.arcCount;
				}
				inventory.sections.push_back(std::move(section));
			}
		}
	}
	return inventory;
}
