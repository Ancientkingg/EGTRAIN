#include "scene/StopInsertion.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {

struct LowerBound {
	std::size_t cursor = 0;
	std::size_t blockingStop = SceneStopInsertionWindow::kNoStop;
};

// The first visit that is still free after the first `count` resolutions.
LowerBound lowerBound(const std::vector<SceneStopResolution>& resolutions, std::size_t count) {
	LowerBound bound;
	for (std::size_t index = 0; index < count && index < resolutions.size(); ++index) {
		const SceneStopResolution& resolution = resolutions[index];
		if (resolution.status == SceneStopResolutionStatus::Resolved)
			bound.cursor = resolution.visitIndex + 1;
		else if (resolution.status != SceneStopResolutionStatus::OffRouteContext) {
			bound.blockingStop = index;
			return bound;
		}
	}
	return bound;
}

std::string stationLabel(const SceneModel& model, const std::string& stationId) {
	for (const SceneStation& station : model.stations) {
		if (station.id == stationId)
			return station.name.empty() ? station.id : station.name;
	}
	return stationId.empty() ? "(missing station)" : stationId;
}

std::string stopLabel(const SceneModel& model, const SceneService& service, std::size_t index) {
	return "stop " + std::to_string(index + 1) + " ("
			+ stationLabel(model, service.stops[index].stationId) + ")";
}

const char* statusProblem(SceneStopResolutionStatus status) {
	switch (status) {
	case SceneStopResolutionStatus::Resolved: return "";
	case SceneStopResolutionStatus::AmbiguousPlatform:
		return "has more than one reachable platform; choose a platform";
	case SceneStopResolutionStatus::OffRouteContext: return "is not on the route";
	case SceneStopResolutionStatus::OutOfOrder: return "is before or at an already used route visit";
	case SceneStopResolutionStatus::InvalidPlatform: return "uses a platform the route does not reach";
	case SceneStopResolutionStatus::UnknownStation: return "uses a station that does not exist";
	case SceneStopResolutionStatus::UnresolvedRoute: return "cannot be matched because the route is unresolved";
	}
	return "";
}

SceneStopInsertionWindow failedWindow(std::size_t blockingStop, std::string problem) {
	SceneStopInsertionWindow window;
	window.blockingStop = blockingStop;
	window.problem = std::move(problem);
	return window;
}

} // namespace

SceneRouteTraversal sceneServiceTraversal(const SceneModel& model, const SceneService& service) {
	for (const auto& route : model.routes)
		if (route.id == service.route)
			return buildSceneRouteTraversal(model, route);
	return {};
}

SceneRouteTraversal sceneRemainingStopTraversal(const SceneModel& model, const SceneService& service,
		std::size_t stopIndex) {
	auto traversal = sceneServiceTraversal(model, service);
	SceneService prefix = service;
	prefix.stops.resize(std::min(stopIndex, prefix.stops.size()));
	const LowerBound bound = lowerBound(resolveSceneServiceStops(model, prefix, traversal),
			prefix.stops.size());
	if (bound.blockingStop != SceneStopInsertionWindow::kNoStop) {
		traversal.visits.clear();
		return traversal;
	}
	traversal.visits.erase(traversal.visits.begin(), traversal.visits.begin() + bound.cursor);
	return traversal;
}

SceneStopInsertionWindow sceneStopInsertionWindow(const SceneModel& model,
		const SceneService& service, std::size_t insertIndex) {
	if (insertIndex > service.stops.size())
		return failedWindow(SceneStopInsertionWindow::kNoStop,
				"The position is beyond the end of the timetable.");
	SceneRouteTraversal traversal = sceneServiceTraversal(model, service);
	if (!traversal.resolved)
		return failedWindow(SceneStopInsertionWindow::kNoStop,
				"The route of this service cannot be resolved; choose a valid route first.");
	const auto resolutions = resolveSceneServiceStops(model, service, traversal);
	const LowerBound lower = lowerBound(resolutions, insertIndex);
	if (lower.blockingStop != SceneStopInsertionWindow::kNoStop) {
		const SceneStopResolution& resolution = resolutions[lower.blockingStop];
		return failedWindow(lower.blockingStop,
				"Cannot insert after " + stopLabel(model, service, lower.blockingStop) + ": it "
				+ statusProblem(resolution.status) + ". Fix that stop first.");
	}
	std::size_t upper = traversal.visits.size();
	for (std::size_t index = insertIndex; index < resolutions.size(); ++index) {
		if (resolutions[index].status == SceneStopResolutionStatus::Resolved) {
			upper = std::max(resolutions[index].visitIndex, lower.cursor);
			break;
		}
	}
	traversal.visits.erase(traversal.visits.begin() + upper, traversal.visits.end());
	traversal.visits.erase(traversal.visits.begin(), traversal.visits.begin() + lower.cursor);
	SceneStopInsertionWindow window;
	window.ok = true;
	window.visits = std::move(traversal);
	return window;
}

SceneStopInsertionResult insertSceneStop(const SceneModel& model, SceneService& service,
		std::size_t insertIndex, const SceneStop& stop) {
	SceneStopInsertionResult result;
	if (insertIndex > service.stops.size()) {
		result.error = "The position is beyond the end of the timetable.";
		return result;
	}
	const SceneRouteTraversal traversal = sceneServiceTraversal(model, service);
	if (!traversal.resolved) {
		result.error = "The route of this service cannot be resolved; choose a valid route first.";
		return result;
	}
	const auto before = resolveSceneServiceStops(model, service, traversal);
	SceneService candidate = service;
	candidate.stops.insert(candidate.stops.begin() + insertIndex, stop);
	const auto after = resolveSceneServiceStops(model, candidate, traversal);

	const SceneStopResolutionStatus newStatus = after[insertIndex].status;
	if (newStatus != SceneStopResolutionStatus::Resolved) {
		result.error = "The new stop (" + stationLabel(model, stop.stationId) + ") "
				+ statusProblem(newStatus) + ".";
		if (newStatus != SceneStopResolutionStatus::AmbiguousPlatform)
			result.error += " Choose another position, station or platform.";
		return result;
	}
	std::string damaged;
	for (std::size_t index = 0; index < before.size(); ++index) {
		if (before[index].status != SceneStopResolutionStatus::Resolved)
			continue;
		const std::size_t newIndex = index < insertIndex ? index : index + 1;
		if (after[newIndex].status == SceneStopResolutionStatus::Resolved
				&& after[newIndex].visitIndex == before[index].visitIndex)
			continue;
		damaged += (damaged.empty() ? "" : ", ") + stopLabel(model, service, index);
	}
	if (!damaged.empty()) {
		result.error = "Inserting " + stationLabel(model, stop.stationId)
				+ " here would move these stops to another route visit or leave them without one: "
				+ damaged + ". Choose an earlier position or another station.";
		return result;
	}
	service.stops = std::move(candidate.stops);
	result.inserted = true;
	return result;
}
