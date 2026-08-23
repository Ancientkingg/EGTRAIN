#include "scene/TrackPreview.h"

#include "scene/SectionInventory.h"
#include "scene/SceneModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {

constexpr double kVirtualSwitchSpanKm = 0.030;
constexpr double kVirtualSwitchToleranceKm = 0.0001;
constexpr double kGeographicTrackSeparation = 0.0006;
constexpr double kPi = 3.14159265358979323846;

bool mapPreviewX(double rawX, const std::vector<std::pair<double, double>>& anchors,
		double& displayX) {
	if (anchors.size() < 2)
		return false;

	const auto right = std::upper_bound(anchors.begin(), anchors.end(), rawX,
			[](double value, const auto& anchor) { return value < anchor.first; });
	const auto left = right == anchors.begin() ? anchors.begin()
			: (right == anchors.end() ? right - 2 : right - 1);
	const auto next = right == anchors.begin() ? right + 1
			: (right == anchors.end() ? right - 1 : right);
	const double span = next->first - left->first;
	if (!(span > 0.0))
		return false;
	displayX = left->second + (rawX - left->first)
			* (next->second - left->second) / span;
	return std::isfinite(displayX);
}

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
		line.points.push_back({node->xKm, node->yKm, node->id, node->xKm});
	result.lines.push_back(std::move(line));
}

bool legacyDoubleSwitchTrack(const SceneModel& scene, const std::string& trackId,
		double& firstX, double& lastX) {
	std::size_t arcCount = 0;
	bool hasNode = false;
	firstX = std::numeric_limits<double>::infinity();
	lastX = -std::numeric_limits<double>::infinity();
	for (const auto& arc : scene.arcs)
		if (arc.trackId == trackId)
			++arcCount;
	for (const auto& node : scene.nodes) {
		if (node.trackId != trackId)
			continue;
		hasNode = true;
		firstX = std::min(firstX, node.xKm);
		lastX = std::max(lastX, node.xKm);
	}
	return arcCount == 3 && hasNode
			&& std::fabs(lastX - firstX - kVirtualSwitchSpanKm) < kVirtualSwitchToleranceKm;
}

} // namespace

TrackPreviewResult loadTrackPreview(const SceneModel& scene) {
	TrackPreviewResult result;
	std::unordered_map<std::string, const SceneNode*> nodesById;
	for (const auto& node : scene.nodes)
		if (!node.id.empty() && nodesById.emplace(node.id, &node).second == false)
			result.warnings.push_back("duplicate node id " + node.id + " is ambiguous in preview");

	for (const auto& track : scene.tracks) {
		const auto view = std::find_if(scene.trackViews.begin(), scene.trackViews.end(),
				[&track](const SceneTrackView& candidate) { return candidate.trackId == track.id; });
		if (view == scene.trackViews.end() || view->visible)
			addTrackLine(scene, track, nodesById, result);
	}

	if (result.lines.size() > 1) {
		double minX = std::numeric_limits<double>::infinity();
		double maxX = -std::numeric_limits<double>::infinity();
		double minY = std::numeric_limits<double>::infinity();
		double maxY = -std::numeric_limits<double>::infinity();
		for (const auto& line : result.lines) {
			for (const auto& point : line.points) {
				minX = std::min(minX, point.x);
				maxX = std::max(maxX, point.x);
				minY = std::min(minY, point.y);
				maxY = std::max(maxY, point.y);
			}
		}
		if (std::isfinite(minX) && std::fabs(maxY - minY) <= 1e-9) {
			const double step = (maxX - minX) * 0.2
					/ static_cast<double>(result.lines.size() - 1);
			const double middle = static_cast<double>(result.lines.size() - 1) / 2.0;
			for (std::size_t index = 0; index < result.lines.size(); ++index)
				result.lines[index].displayOffset = (static_cast<double>(index) - middle) * step;
		}
	}
	for (auto& line : result.lines) {
		const auto view = std::find_if(scene.trackViews.begin(), scene.trackViews.end(),
				[&line](const SceneTrackView& candidate) { return candidate.trackId == line.id; });
		if (view != scene.trackViews.end()) {
			line.displayOffset = static_cast<double>(view->level) * 0.015;

			std::vector<std::pair<double, double>> xAnchors;
			std::vector<std::pair<double, double>> yAnchors;
			bool valid = true;
			for (const auto& station : scene.stationViews) {
				for (const auto& region : station.regions) {
					if (region.first != view->region)
						continue;
					if (!std::isfinite(station.latitude) || station.latitude <= -90.0
							|| station.latitude >= 90.0 || !std::isfinite(station.longitude)
							|| !std::isfinite(region.second)) {
						valid = false;
						break;
					}
					const double displayY = -std::log(std::tan(kPi / 4.0
							+ station.latitude * kPi / 360.0)) * 180.0 / kPi;
					if (!std::isfinite(displayY)) {
						valid = false;
						break;
					}
					xAnchors.emplace_back(region.second, station.longitude);
					yAnchors.emplace_back(region.second, displayY);
				}
				if (!valid)
					break;
			}
			std::sort(xAnchors.begin(), xAnchors.end());
			std::sort(yAnchors.begin(), yAnchors.end());
			std::vector<std::pair<double, double>> uniqueXAnchors;
			for (const auto& anchor : xAnchors) {
				if (!uniqueXAnchors.empty() && anchor.first == uniqueXAnchors.back().first) {
					if (anchor.second != uniqueXAnchors.back().second)
						valid = false;
					continue;
				}
				uniqueXAnchors.push_back(anchor);
			}
			std::vector<std::pair<double, double>> uniqueYAnchors;
			for (const auto& anchor : yAnchors) {
				if (!uniqueYAnchors.empty() && anchor.first == uniqueYAnchors.back().first) {
					if (anchor.second != uniqueYAnchors.back().second)
						valid = false;
					continue;
				}
				uniqueYAnchors.push_back(anchor);
			}
			const bool schematicLayout = valid && uniqueYAnchors.size() >= 2
					&& std::all_of(uniqueYAnchors.begin() + 1, uniqueYAnchors.end(),
							[&uniqueYAnchors](const auto& anchor) {
								return std::fabs(anchor.second - uniqueYAnchors.front().second) <= 1e-9;
							});
			if (schematicLayout && uniqueXAnchors.size() >= 2) {
				std::vector<double> displayXs;
				displayXs.reserve(line.points.size());
				for (const auto& point : line.points) {
					double displayX = 0.0;
					if (!std::isfinite(point.rawX)
							|| !mapPreviewX(point.rawX, uniqueXAnchors, displayX)) {
						valid = false;
						break;
					}
					displayXs.push_back(displayX);
				}
				if (valid)
					for (std::size_t index = 0; index < line.points.size(); ++index)
						line.points[index].x = displayXs[index];
				continue;
			}
			if (valid && uniqueXAnchors.size() >= 2 && uniqueYAnchors.size() >= 2) {
				const double separation = static_cast<double>(view->level)
						* kGeographicTrackSeparation;
				for (std::size_t index = 0; index < uniqueXAnchors.size(); ++index) {
					const std::size_t previous = index == 0 ? 0 : index - 1;
					const std::size_t next = index + 1 == uniqueXAnchors.size() ? index : index + 1;
					const double dx = uniqueXAnchors[next].second - uniqueXAnchors[previous].second;
					const double dy = uniqueYAnchors[next].second - uniqueYAnchors[previous].second;
					const double length = std::hypot(dx, dy);
					if (!(length > 0.0)) {
						valid = false;
						break;
					}
					uniqueXAnchors[index].second += dy / length * separation;
					uniqueYAnchors[index].second -= dx / length * separation;
				}
				if (!valid)
					continue;
				std::vector<std::pair<double, double>> displayPoints;
				displayPoints.reserve(line.points.size());
				for (auto& point : line.points) {
					double displayX = 0.0;
					double displayY = 0.0;
					if (!std::isfinite(point.rawX)
							|| !mapPreviewX(point.rawX, uniqueXAnchors, displayX)
							|| !mapPreviewX(point.rawX, uniqueYAnchors, displayY)) {
						valid = false;
						break;
					}
					displayPoints.emplace_back(displayX, displayY);
				}
				if (valid) {
					line.displayOffset = 0.0;
					for (std::size_t index = 0; index < line.points.size(); ++index) {
						line.points[index].x = displayPoints[index].first;
						line.points[index].y = displayPoints[index].second;
					}
				}
			}
		}
	}

	const auto visibleTrack = [&result](const std::string& id) {
		return std::any_of(result.lines.begin(), result.lines.end(),
				[&id](const TrackPreviewLine& line) { return line.id == id; });
	};
	for (const auto& connection : scene.connections) {
		const auto first = nodesById.find(connection.fromNodeId);
		const auto second = nodesById.find(connection.toNodeId);
		if (first == nodesById.end() || second == nodesById.end()) {
			result.warnings.push_back("connection " + connection.id + " has an unresolved node anchor");
			continue;
		}
		if (!visibleTrack(first->second->trackId) || !visibleTrack(second->second->trackId))
			continue;
		TrackPreviewConnection preview;
		preview.firstTrackId = first->second->trackId;
		preview.firstNodeId = first->second->id;
		preview.secondTrackId = second->second->trackId;
		preview.secondNodeId = second->second->id;
		result.connections.push_back(std::move(preview));
	}

	for (const auto& station : scene.stations) {
		const std::string name = station.name.empty() ? station.id : station.name;
		const SceneNode* anchor = nullptr;
		for (const auto& platform : station.platforms) {
			for (const auto& nodeId : platform.nodeIds) {
				const auto node = nodesById.find(nodeId);
				if (node == nodesById.end()) {
					result.warnings.push_back("platform " + platform.id + " has an unresolved node anchor");
					continue;
				}
				if (anchor == nullptr && visibleTrack(node->second->trackId))
					anchor = node->second;
			}
		}
		if (anchor != nullptr)
			result.stations.push_back({name, anchor->id, anchor->xKm, true});
		else if (station.hasPosition && std::isfinite(station.positionKm)) {
			result.stations.push_back({name, {}, station.positionKm, false});
		}
	}

	const SceneSectionInventory sectionInventory = buildSceneSectionInventory(scene);
	std::unordered_set<std::string> seenTracks;
	std::unordered_map<std::string, std::pair<double, double>> virtualSwitches;
	for (const auto& section : sectionInventory.sections) {
		if (section.connectionDerived || section.firstTrackId.empty()
				|| !visibleTrack(section.firstTrackId)
				|| seenTracks.insert(section.firstTrackId).second)
			continue; // The first base section has no runtime signal.

		double firstX = 0.0;
		double lastX = 0.0;
		const auto virtualSwitch = virtualSwitches.emplace(section.firstTrackId,
				std::make_pair(firstX, lastX));
		if (virtualSwitch.second
				&& !legacyDoubleSwitchTrack(scene, section.firstTrackId, firstX, lastX))
			virtualSwitches.erase(virtualSwitch.first);
		else if (virtualSwitch.second)
			virtualSwitch.first->second = {firstX, lastX};

		if (virtualSwitches.count(section.firstTrackId) != 0
				&& std::fabs(section.startKm
						- (virtualSwitches.at(section.firstTrackId).first
							+ virtualSwitches.at(section.firstTrackId).second) / 2.0)
					< kVirtualSwitchToleranceKm)
			continue;
		result.previewSignals.push_back({section.firstTrackId, section.id,
			section.startNodeId, section.startKm});
	}

	if (result.lines.empty())
		result.warnings.push_back("no valid scene track preview is available");
	return result;
}
