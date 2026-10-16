#ifndef SCENE_REFERENCES_H
#define SCENE_REFERENCES_H

#include "scene/SceneModel.h"

#include <string>
#include <vector>

// What still uses an entity, and how a rename reaches the entities that refer to it. The rename functions
// rewrite the references only; the caller renames the entity itself.

// One description per use of the entity `id`, in the order found, for the message that blocks a delete. Empty
// when nothing uses it. `facet` is an infrastructure table ("tracks", "nodes", "blocks", "connections",
// "stations", "platforms", "signals", "routes") or one of "train_unit", "composition" and "service". The other
// infrastructure tables are not used by anything. `scope` is the id of the station that holds a platform.
std::vector<std::string> sceneDirectDeleteConsumers(const SceneModel& model, const std::string& facet,
	const std::string& id, const std::string& scope = {});

// A section reference names one block or connection, optionally wrapped as `@id@...`, or several of them joined
// by '/'. This returns the ids in it.
std::vector<std::string> sceneBlockReferenceComponents(const std::string& reference);
// `reference` with every component `oldId` replaced by `newId`.
std::string sceneRewriteBlockReference(const std::string& reference, const std::string& oldId,
	const std::string& newId);

// Nodes, arcs, blocks and signalling areas that sit on the track.
void sceneRewriteTrackReferences(SceneModel& model, const std::string& oldId, const std::string& newId);
// Arc and connection endpoints and platform nodes.
void sceneRewriteNodeReferences(SceneModel& model, const std::string& oldId, const std::string& newId);
// Routes, block dependencies, single-track restrictions, station boundaries, signal-failure incident targets and
// protected sections of signals.
void sceneRewriteBlockReferences(SceneModel& model, const std::string& oldId, const std::string& newId);
// Stops, entrance delays and passenger journeys and legs.
void sceneRewriteStationReferences(SceneModel& model, const std::string& oldId, const std::string& newId);
// Stops at the platform of that station.
void sceneRewritePlatformReferences(SceneModel& model, const std::string& stationId, const std::string& oldId,
	const std::string& newId);
// Stops at the platform move with it when the platform moves to another station.
void sceneMovePlatformReferences(SceneModel& model, const std::string& platformId,
	const std::string& fromStationId, const std::string& toStationId);
// Signal-failure incident targets.
void sceneRewriteSignalReferences(SceneModel& model, const std::string& oldId, const std::string& newId);
// The route of services.
void sceneRewriteRouteReferences(SceneModel& model, const std::string& oldId, const std::string& newId);
// The units of compositions.
void sceneRewriteTrainUnitReferences(SceneModel& model, const std::string& oldId, const std::string& newId);
// The composition of services.
void sceneRewriteCompositionReferences(SceneModel& model, const std::string& oldId, const std::string& newId);
// Entrance delays, train-breakdown incident targets and passenger legs.
void sceneRewriteServiceReferences(SceneModel& model, const std::string& oldId, const std::string& newId);
// The default scenario.
void sceneRewriteScenarioReferences(SceneModel& model, const std::string& oldId, const std::string& newId);

// The train unit with this id, or null.
const SceneTrainUnit* sceneTrainUnitById(const SceneModel& model, const std::string& id);

#endif // SCENE_REFERENCES_H
