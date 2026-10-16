#ifndef SCENE_IDS_H
#define SCENE_IDS_H

#include "scene/SceneModel.h"

#include <string>

// Each function returns `baseId` when no entity of its kind has it, and otherwise the first of `baseId_2`,
// `baseId_3`, ... that is free.

// `facet` is the infrastructure table: "tracks", "nodes", "arcs", "blocks", "connections", "stations",
// "signals", "signalling_areas", "platforms" or "routes". Platform ids are unique within the first
// station.
std::string sceneUniqueInfrastructureId(const SceneModel& model, const std::string& baseId,
	const std::string& facet);

// An empty `baseId` stands for "new_passenger", "new_journey" and "new_leg". Journey ids are unique across all
// passengers and leg ids across all journeys.
std::string sceneUniquePassengerId(const SceneModel& model, const std::string& baseId);
std::string sceneUniquePassengerJourneyId(const SceneModel& model, const std::string& baseId);
std::string sceneUniquePassengerLegId(const SceneModel& model, const std::string& baseId);

std::string sceneUniqueTrainUnitId(const SceneModel& model, const std::string& baseId);
std::string sceneUniqueCompositionId(const SceneModel& model, const std::string& baseId);
std::string sceneUniqueServiceId(const SceneModel& model, const std::string& baseId);
// An empty `baseId` stands for "scenario".
std::string sceneUniqueScenarioId(const SceneModel& model, const std::string& baseId);
// Incident ids are unique across all scenarios.
std::string sceneUniqueIncidentId(const SceneModel& model, const std::string& baseId);

#endif // SCENE_IDS_H
