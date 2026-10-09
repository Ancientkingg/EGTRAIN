#include "diagrams/RouteDiagramCoordinates.h"
#include "util/CsvWriter.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace {
std::map<std::string, std::optional<double>> anchors(const RouteDiagramPath& path) {
	std::map<std::string, std::optional<double>> result;
	for (const auto& node : path.nodes) {
		if (!std::isfinite(node.positionKm)) continue;
		for (const auto& key : {node.nodeId.empty() ? std::string() : "node:" + node.nodeId,
				 node.stationId.empty() ? std::string() : "station:" + node.stationId}) {
			if (key.empty()) continue;
			auto inserted = result.emplace(key, node.positionKm);
			if (!inserted.second && inserted.first->second
				&& std::abs(*inserted.first->second - node.positionKm) > 1e-7)
				inserted.first->second.reset();
		}
	}
	return result;
}
}

RouteDiagramProjection buildRouteDiagramProjection(const RouteDiagramPath& source,
	const RouteDiagramPath& reference) {
	if (source.nodes.empty() || reference.nodes.empty()) return {};
	if (!source.id.empty() && source.id == reference.id && source.nodes.size() == reference.nodes.size()
		&& std::equal(source.nodes.begin(), source.nodes.end(), reference.nodes.begin(),
			[](const RouteDiagramNode& a, const RouteDiagramNode& b) {
				return a.nodeId == b.nodeId && a.stationId == b.stationId
					&& std::abs(a.positionKm - b.positionKm) < 1e-7;
			})) return {true, {}};
	const auto sourceAnchors = anchors(source);
	const auto referenceAnchors = anchors(reference);
	std::vector<std::pair<double, double>> pairs;
	for (const auto& entry : sourceAnchors) {
		const auto found = referenceAnchors.find(entry.first);
		if (found == referenceAnchors.end()) continue;
		// A repeated shared identity at different coordinates cannot anchor a
		// unique projection, even if the other anchors appear monotonic.
		if (!entry.second || !found->second) return {};
		pairs.emplace_back(*entry.second, *found->second);
	}
	std::sort(pairs.begin(), pairs.end());
	pairs.erase(std::unique(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) {
		return std::abs(a.first - b.first) < 1e-7 && std::abs(a.second - b.second) < 1e-7;
	}),
		pairs.end());
	if (pairs.size() < 2) return {};
	// A source coordinate cannot identify two different reference positions.
	for (std::size_t i = 1; i < pairs.size(); ++i)
		if (std::abs(pairs[i].first - pairs[i - 1].first) < 1e-7
			&& std::abs(pairs[i].second - pairs[i - 1].second) >= 1e-7)
			return {};
	const bool reverse = pairs.back().second < pairs.front().second;
	for (std::size_t i = 1; i < pairs.size(); ++i) {
		if (pairs[i].first <= pairs[i - 1].first
			|| (reverse ? pairs[i].second >= pairs[i - 1].second : pairs[i].second <= pairs[i - 1].second))
			return {};
	}
	return {false, std::move(pairs)};
}

std::optional<double> RouteDiagramProjection::map(double positionKm) const {
	if (!std::isfinite(positionKm)) return {};
	if (identity) return positionKm;
	const auto& pairs = anchors;
	if (pairs.size() < 2 || positionKm < pairs.front().first || positionKm > pairs.back().first) return {};
	const auto upper = std::lower_bound(pairs.begin(), pairs.end(), positionKm,
		[](const auto& pair, double value) { return pair.first < value; });
	if (upper == pairs.begin() || upper->first == positionKm) return upper->second;
	const auto& lower = *(upper - 1);
	const double fraction = (positionKm - lower.first) / (upper->first - lower.first);
	return lower.second + fraction * (upper->second - lower.second);
}


double routeDiagramTrajectoryKm(double positionMeters) {
	return positionMeters / 1000.0;
}

std::string buildRouteDiagramCsv(const std::vector<std::vector<std::string>>& rows,
	const std::vector<std::string>& visibleTrainIds) {
	std::vector<std::vector<std::string>> filtered;
	for (const auto& row : rows)
		if (!row.empty() && std::find(visibleTrainIds.begin(), visibleTrainIds.end(), row.front()) != visibleTrainIds.end())
			filtered.push_back(row);
	return csv::makeDocument({"Train", "Reference route", "Source route", "Event", "Station",
								 "Journey order", "Call", "Elapsed time[s]", "Reference route X[km]"},
		filtered);
}

std::map<double, std::string> routeDiagramStationLabels(const RouteDiagramPath& path) {
	std::map<double, std::map<std::string, std::string>> grouped;
	for (const auto& node : path.nodes)
		if (!node.stationId.empty() && std::isfinite(node.positionKm))
			grouped[node.positionKm].emplace(node.stationId, node.stationName);
	std::map<double, std::string> labels;
	for (const auto& at : grouped) {
		std::string label;
		for (const auto& station : at.second) {
			if (!label.empty()) label += " / ";
			label += station.second.empty() ? station.first : station.second;
			if (!station.second.empty() && station.second != station.first)
				label += " [" + station.first + "]";
		}
		labels.emplace(at.first, std::move(label));
	}
	return labels;
}
