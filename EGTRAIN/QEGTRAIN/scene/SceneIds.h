#ifndef SCENE_IDS_H
#define SCENE_IDS_H

#include "scene/SceneModel.h"

#include <string>

// Each function returns `baseId` when no entity of its kind has it, and otherwise the first of `baseId_2`,
// `baseId_3`, ... that is free.

/// A free id in an infrastructure table. `facet` is the table: "tracks", "nodes", "arcs", "blocks", "connections",
/// "stations", "signals", "signalling_areas", "platforms" or "routes". Platform ids are unique within the first
/// station.
std::string sceneUniqueInfrastructureId(const SceneModel& model, const std::string& baseId,
	const std::string& facet);

/// A free passenger id. An empty `baseId` stands for "new_passenger".
std::string sceneUniquePassengerId(const SceneModel& model, const std::string& baseId);
/// A journey id that is free across all passengers. An empty `baseId` stands for "new_journey".
std::string sceneUniquePassengerJourneyId(const SceneModel& model, const std::string& baseId);
/// A leg id that is free across all journeys. An empty `baseId` stands for "new_leg".
std::string sceneUniquePassengerLegId(const SceneModel& model, const std::string& baseId);

/// A free train unit id.
std::string sceneUniqueTrainUnitId(const SceneModel& model, const std::string& baseId);
/// A free composition id.
std::string sceneUniqueCompositionId(const SceneModel& model, const std::string& baseId);
/// A free service id.
std::string sceneUniqueServiceId(const SceneModel& model, const std::string& baseId);
/// A free scenario id. An empty `baseId` stands for "scenario".
std::string sceneUniqueScenarioId(const SceneModel& model, const std::string& baseId);
/// An incident id that is free across all scenarios.
std::string sceneUniqueIncidentId(const SceneModel& model, const std::string& baseId);

#endif // SCENE_IDS_H
