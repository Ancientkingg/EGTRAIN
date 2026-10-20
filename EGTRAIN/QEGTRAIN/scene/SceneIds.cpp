#include "scene/SceneIds.h"

#include <algorithm>

namespace {

template <typename Exists>
std::string firstFreeId(const std::string& baseId, Exists exists) {
	std::string candidate = baseId;
	int suffix = 2;
	while (exists(candidate))
		candidate = baseId + "_" + std::to_string(suffix++);
	return candidate;
}

template <typename Items>
bool hasItemWithId(const Items& items, const std::string& id) {
	return std::any_of(items.begin(), items.end(), [&id](const auto& item) { return item.id == id; });
}

} // namespace

std::string sceneUniqueInfrastructureId(const SceneModel& model, const std::string& baseId,
	const std::string& facet) {
	if (facet == "platforms" && !model.stations.empty()) {
		const auto& platforms = model.stations.front().platforms;
		return firstFreeId(baseId, [&platforms](const std::string& id) { return hasItemWithId(platforms, id); });
	}
	return firstFreeId(baseId, [&model, &facet](const std::string& id) {
		if (facet == "tracks")
			return hasItemWithId(model.tracks, id);
		if (facet == "nodes")
			return hasItemWithId(model.nodes, id);
		if (facet == "arcs")
			return hasItemWithId(model.arcs, id);
		if (facet == "blocks")
			return hasItemWithId(model.blocks, id);
		if (facet == "connections")
			return hasItemWithId(model.connections, id);
		if (facet == "stations")
			return hasItemWithId(model.stations, id);
		if (facet == "signals")
			return hasItemWithId(model.signals, id);
		if (facet == "signalling_areas")
			return hasItemWithId(model.signallingAreas, id);
		return hasItemWithId(model.routes, id);
	});
}

std::string sceneUniquePassengerId(const SceneModel& model, const std::string& baseId) {
	return firstFreeId(baseId.empty() ? "new_passenger" : baseId,
		[&model](const std::string& id) { return hasItemWithId(model.passengers, id); });
}

std::string sceneUniquePassengerJourneyId(const SceneModel& model, const std::string& baseId) {
	return firstFreeId(baseId.empty() ? "new_journey" : baseId, [&model](const std::string& id) {
		return std::any_of(model.passengers.begin(), model.passengers.end(),
			[&id](const ScenePassenger& passenger) { return hasItemWithId(passenger.journeys, id); });
	});
}

std::string sceneUniquePassengerLegId(const SceneModel& model, const std::string& baseId) {
	return firstFreeId(baseId.empty() ? "new_leg" : baseId, [&model](const std::string& id) {
		return std::any_of(model.passengers.begin(), model.passengers.end(), [&id](const ScenePassenger& passenger) {
			return std::any_of(passenger.journeys.begin(), passenger.journeys.end(),
				[&id](const ScenePassengerJourney& journey) { return hasItemWithId(journey.legs, id); });
		});
	});
}

std::string sceneUniqueTrainUnitId(const SceneModel& model, const std::string& baseId) {
	return firstFreeId(baseId, [&model](const std::string& id) { return hasItemWithId(model.trainUnits, id); });
}

std::string sceneUniqueCompositionId(const SceneModel& model, const std::string& baseId) {
	return firstFreeId(baseId, [&model](const std::string& id) { return hasItemWithId(model.compositions, id); });
}

std::string sceneUniqueServiceId(const SceneModel& model, const std::string& baseId) {
	return firstFreeId(baseId, [&model](const std::string& id) { return hasItemWithId(model.services, id); });
}

std::string sceneUniqueScenarioId(const SceneModel& model, const std::string& baseId) {
	return firstFreeId(baseId.empty() ? "scenario" : baseId,
		[&model](const std::string& id) { return hasItemWithId(model.scenarios, id); });
}

std::string sceneUniqueIncidentId(const SceneModel& model, const std::string& baseId) {
	return firstFreeId(baseId, [&model](const std::string& id) {
		return std::any_of(model.scenarios.begin(), model.scenarios.end(),
			[&id](const SceneScenario& scenario) { return hasItemWithId(scenario.incidents, id); });
	});
}
