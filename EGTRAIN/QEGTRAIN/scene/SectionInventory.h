#ifndef SCENE_SECTION_INVENTORY_H
#define SCENE_SECTION_INVENTORY_H

#include "scene/SceneModel.h"
#include "scene/SignallingLevelNames.h"

#include <cstddef>
#include <limits>
#include <string>
#include <unordered_map>
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

// The stations a route passes in travel order. Consecutive visits of the same
// station count once; a station visited again later is listed again.
struct SceneRouteStations {
	bool resolved = false;
	int direction = 0; // as SceneRouteTraversal::direction
	std::vector<std::string> stationIds;
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

// Two signalling areas of one scope that give one section different levels.
struct SceneSignallingAreaConflict {
	std::size_t firstArea = 0; // The area of that scope that decides, as an index into SceneModel::signallingAreas.
	std::size_t area = 0;	   // The area whose level differs from it.
	bool trackScoped = false;
};

// What the signalling areas make of one runtime section.
struct SceneSectionSignalling {
	static constexpr std::size_t kNoArea = std::numeric_limits<std::size_t>::max();

	std::string sectionId;
	int level = kSignallingLevelUnset;
	std::size_t decidingArea = kNoArea; // Index into SceneModel::signallingAreas, kNoArea when none covers it.
	bool onRoute = false;
	std::vector<SceneSignallingAreaConflict> conflicts; // Network-wide scope first.
};

// What one signalling area makes of the runtime sections in its scope.
struct SceneAreaSignalling {
	std::size_t sectionCount = 0;				   // Sections that lie completely inside the area.
	std::size_t routeSectionCount = 0;			   // Of those, the sections on a route.
	std::vector<std::string> sectionsSplitByStart; // Sections the start edge cuts through.
	std::vector<std::string> sectionsSplitByEnd;   // Sections the end edge cuts through.
};

struct SceneSignallingAnalysis {
	std::vector<SceneSectionSignalling> sections; // In inventory order, one per section ID.
	std::vector<SceneAreaSignalling> areas;		  // One per SceneModel::signallingAreas entry.

	std::unordered_map<std::string, std::size_t> sectionIndex; // Section ID to its place in sections.

	const SceneSectionSignalling* section(const std::string& sectionId) const;
};

SceneSectionInventory buildSceneSectionInventory(const SceneModel& scene);

// The extent of the blocks of one track, or of the whole network when the track ID is empty.
// Sections that a connection derives are left out; found is false when there is no block.
struct SceneBlockExtent {
	bool found = false;
	double startKm = 0.0;
	double endKm = 0.0;
};
SceneBlockExtent sceneBlockExtent(const SceneSectionInventory& inventory, const std::string& trackId);

// Which signalling area decides the level of each section. A section belongs to an area when it lies
// completely inside the area's range on its own track chainage; a track-scoped area applies to the
// sections of its track and overrides the network-wide areas. When areas of one scope disagree, the
// first of them decides and the others are listed as conflicts. Areas with a non-finite or empty range
// or a level outside 0 to 5 take no part.
SceneSignallingAnalysis analyzeSignallingAreas(const SceneModel& scene, const SceneSectionInventory& inventory);
SceneSectionTransition classifySceneSectionTransition(const SceneModel& scene,
	const SceneSectionDescriptor& left, const SceneSectionDescriptor& right);
int sceneRouteDirection(const SceneModel& scene,
	const std::vector<const SceneSectionDescriptor*>& sections);
bool sceneSectionsOverlap(const std::string& leftId, double leftStart, double leftEnd,
	const std::string& rightId, double rightStart, double rightEnd);
SceneRouteTraversal buildSceneRouteTraversal(const SceneModel& scene, const SceneRoute& route);
SceneRouteTraversal buildSceneRouteTraversal(const SceneModel& scene, const SceneRoute& route,
	const SceneSectionInventory& inventory);
SceneRouteStations sceneRouteStations(const SceneModel& scene, const SceneRoute& route,
	const SceneSectionInventory& inventory);
std::vector<SceneStopResolution> resolveSceneServiceStops(const SceneModel& scene,
	const SceneService& service, const SceneRouteTraversal& traversal);
std::string formatSceneSectionCoordinate(double coordinate);

#endif // SCENE_SECTION_INVENTORY_H
