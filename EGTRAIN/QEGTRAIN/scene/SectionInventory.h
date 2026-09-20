#ifndef SCENE_SECTION_INVENTORY_H
#define SCENE_SECTION_INVENTORY_H

#include "scene/SceneModel.h"

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

// A transient view of the section identities produced by the native builder.
// It is derived from SceneModel and is never serialized.
struct SceneSectionDescriptor {
	std::string id;
	std::string sourceBlockId;
	std::string sourceConnectionId;
	std::string firstBlockId;
	std::string secondBlockId;
	std::string firstTrackId;
	std::string secondTrackId;
	std::string startNodeId;
	std::string endNodeId;
	double startKm = 0.0;
	double endKm = 0.0;
	double firstConnectionKm = 0.0;
	double secondConnectionKm = 0.0;
	std::vector<std::string> nodeIds;
	std::size_t arcCount = 0;
	bool layoutOverflow = false;
	bool trackCoverageGap = false;
	bool clippedToTrackEnd = false;
	bool connectionDerived = false;
};

struct SceneSectionInventory {
	std::vector<SceneSectionDescriptor> sections;

	// Resolve one exact runtime ID or an unwrapped base-block alias. Compound
	// references are accepted only when they are exact catalog IDs.
	const SceneSectionDescriptor* resolve(const std::string& reference) const;
	const SceneSectionDescriptor* exact(const std::string& runtimeId) const;
	bool ambiguous(const std::string& reference) const;
};

struct SceneSectionTransition {
	bool joinsForward = false;
	bool joinsReverse = false;
	bool regionJump = false;
};

struct SceneRouteVisit {
	std::string sectionId;
	std::string nodeId;
	std::string stationId;
	std::string platformId;
	std::size_t sectionIndex = std::numeric_limits<std::size_t>::max();
};

struct SceneRouteTraversal {
	int direction = 0; // 1 = native forward, -1 = native reverse, 0 = unresolved.
	bool resolved = false;
	std::vector<SceneRouteVisit> visits;
};

enum class SceneStopResolutionStatus {
	Resolved,
	AmbiguousPlatform,
	OffRouteContext,
	OutOfOrder,
	InvalidPlatform,
	UnknownStation,
	UnresolvedRoute,
};

struct SceneStopResolution {
	SceneStopResolutionStatus status = SceneStopResolutionStatus::UnresolvedRoute;
	std::size_t visitIndex = std::numeric_limits<std::size_t>::max();
	std::size_t sectionIndex = std::numeric_limits<std::size_t>::max();
	std::vector<std::string> candidatePlatformIds;
	std::string nodeId;
	std::string sectionId;
};

SceneSectionInventory buildSceneSectionInventory(const SceneModel& scene);
SceneSectionTransition classifySceneSectionTransition(const SceneModel& scene,
		const SceneSectionDescriptor& left, const SceneSectionDescriptor& right);
int sceneRouteDirection(const SceneModel& scene,
		const std::vector<const SceneSectionDescriptor*>& sections);
bool sceneSectionsOverlap(const std::string& leftId, double leftStart, double leftEnd,
		const std::string& rightId, double rightStart, double rightEnd);
SceneRouteTraversal buildSceneRouteTraversal(const SceneModel& scene, const SceneRoute& route);
std::vector<SceneStopResolution> resolveSceneServiceStops(const SceneModel& scene,
		const SceneService& service, const SceneRouteTraversal& traversal);
std::string formatSceneSectionCoordinate(double coordinate);

#endif // SCENE_SECTION_INVENTORY_H
