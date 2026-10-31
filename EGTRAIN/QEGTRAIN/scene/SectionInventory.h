#ifndef SCENE_SECTION_INVENTORY_H
#define SCENE_SECTION_INVENTORY_H

#include "scene/SceneModel.h"
#include "scene/SectionLabels.h"
#include "scene/SignallingLevelNames.h"

#include <cstddef>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

/// A transient view of one section identity produced by the native builder: a block section, or a switch section
/// that a connection derives from two blocks. It is derived from SceneModel and is never serialized.
struct SceneSectionDescriptor {
	/// Runtime id: "@block@" for a block section, "@first@-x1/@second@-x2" for a switch section, where x1 and x2 are
	/// the chainages of the two connection nodes as formatSceneSectionCoordinate writes them.
	std::string id;
	std::string sourceBlockId;		  ///< The block of a block section; empty for a switch section.
	std::string sourceConnectionId;	  ///< The connection of a switch section; empty for a block section.
	std::string firstBlockId;		  ///< Block at the lower connection node of a switch section; empty otherwise.
	std::string secondBlockId;		  ///< Block at the higher connection node of a switch section; empty otherwise.
	std::string firstTrackId;		  ///< Track of the block, or of the first block of a switch section.
	std::string secondTrackId;		  ///< Track of the second block of a switch section; empty otherwise.
	std::string startNodeId;		  ///< Node at startKm; empty when no node lies there.
	std::string endNodeId;			  ///< Node at endKm; empty when no node lies there.
	double startKm = 0.0;			  ///< Chainage of the start in kilometres, on the first track.
	double endKm = 0.0;				  ///< Chainage of the end in kilometres, on the second track for a switch section.
	double firstConnectionKm = 0.0;	  ///< Chainage in kilometres of the lower connection node of a switch section.
	double secondConnectionKm = 0.0;  ///< Chainage in kilometres of the higher connection node of a switch section.
	std::vector<std::string> nodeIds; ///< The nodes of the section in order along it.
	std::size_t arcCount = 0;		  ///< Number of arcs in the section.
	/// True when the block runs past the end of its track and is not the last block starting before that end, an
	/// error for the builder. endKm is still capped at the track end.
	bool layoutOverflow = false;
	/// True when the last block ends before the end of its track and is extended to it.
	bool trackCoverageGap = false;
	bool clippedToTrackEnd = false; ///< True when the last block runs past the end of its track and is cut there.
	bool connectionDerived = false; ///< True for a switch section.
};

/// The sections of an inventory in their order, read-only. The list indexes them by runtime id and by source block id when it
/// is made, and since the sections cannot change afterwards, the index always describes them. A copy or an assignment takes
/// the index with the sections.
class SceneSectionList {
public:
	/// An empty list.
	SceneSectionList() = default;
	/// Takes the sections and indexes them.
	explicit SceneSectionList(std::vector<SceneSectionDescriptor> sections);

	/// The first section.
	std::vector<SceneSectionDescriptor>::const_iterator begin() const { return m_sections.begin(); }
	/// Past the last section.
	std::vector<SceneSectionDescriptor>::const_iterator end() const { return m_sections.end(); }
	/// Number of sections.
	std::size_t size() const { return m_sections.size(); }
	/// True when there is no section.
	bool empty() const { return m_sections.empty(); }
	/// The section at a position.
	const SceneSectionDescriptor& operator[](std::size_t index) const { return m_sections[index]; }

	/// The positions of the sections with this runtime id, in order; null when there is none.
	const std::vector<std::size_t>* withId(const std::string& id) const;
	/// The positions of the sections with this source block id, in order; null when there is none. The empty id gives the
	/// sections without a source block, the switch sections among them.
	const std::vector<std::size_t>* withSourceBlock(const std::string& blockId) const;

private:
	std::vector<SceneSectionDescriptor> m_sections;
	std::unordered_map<std::string, std::vector<std::size_t>> m_byId;
	std::unordered_map<std::string, std::vector<std::size_t>> m_bySourceBlock;
};

/// The sections the native builder makes from a scene.
struct SceneSectionInventory {
	SceneSectionList sections; ///< Block sections by track, then switch sections.

	/// Resolve one exact runtime ID or an unwrapped base-block alias. Compound
	/// references are accepted only when they are exact catalog IDs. Null when nothing or more than one
	/// section matches. The pointer points into sections.
	const SceneSectionDescriptor* resolve(const std::string& reference) const;
	/// The one section with this runtime id; null when none or several have it.
	const SceneSectionDescriptor* exact(const std::string& runtimeId) const;
	/// True when more than one section matches the reference as a runtime id or base-block alias.
	bool ambiguous(const std::string& reference) const;
};

/// How two consecutive sections of a route join.
struct SceneSectionTransition {
	bool joinsForward = false; ///< The end of the first section joins the start of the second.
	bool joinsReverse = false; ///< The start of the first section joins the end of the second.
	/// They do not join, share no block, and lie on different tracks at different chainages.
	bool regionJump = false;
};

/// A node of a route at which a station lies.
struct SceneRouteVisit {
	std::string sectionId;	///< Runtime id of the section that holds the node.
	std::string nodeId;		///< The node.
	std::string stationId;	///< The station.
	std::string platformId; ///< The platform at the node; empty for a station bound by its position.
	/// Index into SceneRoute::blocks of the section; the maximum value when not set.
	std::size_t sectionIndex = std::numeric_limits<std::size_t>::max();
};

/// The station visits of a route in travel order.
struct SceneRouteTraversal {
	int direction = 0;					 ///< 1 = native forward, -1 = native reverse, 0 = unresolved.
	bool resolved = false;				 ///< True when every section resolves and the direction is known.
	std::vector<SceneRouteVisit> visits; ///< The visits in travel order.
};

/// The stations a route passes in travel order. Consecutive visits of the same
/// station count once; a station visited again later is listed again.
struct SceneRouteStations {
	bool resolved = false;				 ///< As SceneRouteTraversal::resolved.
	int direction = 0;					 ///< As SceneRouteTraversal::direction.
	std::vector<std::string> stationIds; ///< The stations in travel order.
};

/// How a stop of a service binds to the visits of its route.
enum class SceneStopResolutionStatus {
	Resolved,		   ///< Bound to a visit after the previous bound stop.
	AmbiguousPlatform, ///< No platform given, and the station has several platforms on the rest of the route.
	OffRouteContext,   ///< No platform given, and the station is nowhere on the route.
	OutOfOrder,		   ///< The station or platform lies on the route only before the previous bound stop.
	InvalidPlatform,   ///< The given platform is not on the rest of the route.
	UnknownStation,	   ///< The station id is unknown.
	UnresolvedRoute,   ///< The route traversal is not resolved.
};

/// Where a stop of a service binds on its route.
struct SceneStopResolution {
	SceneStopResolutionStatus status = SceneStopResolutionStatus::UnresolvedRoute; ///< The outcome.
	/// Index into SceneRouteTraversal::visits; the maximum value unless Resolved.
	std::size_t visitIndex = std::numeric_limits<std::size_t>::max();
	/// Index into SceneRoute::blocks of the section; the maximum value unless Resolved.
	std::size_t sectionIndex = std::numeric_limits<std::size_t>::max();
	std::vector<std::string> candidatePlatformIds; ///< Platforms of the station on the rest of the route.
	std::string nodeId;							   ///< The node of the visit; empty unless Resolved.
	std::string sectionId;						   ///< Runtime id of the section of the visit; empty unless Resolved.
};

/// Two signalling areas of one scope that give one section different levels.
struct SceneSignallingAreaConflict {
	std::size_t firstArea = 0; ///< The area of that scope that decides, as an index into SceneModel::signallingAreas.
	std::size_t area = 0;	   ///< The area whose level differs from it.
	bool trackScoped = false;  ///< True for track-scoped areas, false for network-wide ones.
};

/// What the signalling areas make of one runtime section.
struct SceneSectionSignalling {
	static constexpr std::size_t kNoArea = std::numeric_limits<std::size_t>::max(); ///< No area.

	std::string sectionId;								///< Runtime id of the section.
	int level = kSignallingLevelUnset;					///< The level the deciding area gives; kSignallingLevelUnset when none.
	std::size_t decidingArea = kNoArea;					///< Index into SceneModel::signallingAreas, kNoArea when none covers it.
	bool onRoute = false;								///< True when a route contains the section.
	std::vector<SceneSignallingAreaConflict> conflicts; ///< Network-wide scope first.
};

/// What one signalling area makes of the runtime sections in its scope.
struct SceneAreaSignalling {
	std::size_t sectionCount = 0;				   ///< Sections that lie completely inside the area.
	std::size_t routeSectionCount = 0;			   ///< Of those, the sections on a route.
	std::vector<std::string> sectionsSplitByStart; ///< Sections the start edge cuts through.
	std::vector<std::string> sectionsSplitByEnd;   ///< Sections the end edge cuts through.
};

/// What the signalling areas of a scene make of its runtime sections.
struct SceneSignallingAnalysis {
	std::vector<SceneSectionSignalling> sections; ///< In inventory order, one per section ID.
	std::vector<SceneAreaSignalling> areas;		  ///< One per SceneModel::signallingAreas entry.

	std::unordered_map<std::string, std::size_t> sectionIndex; ///< Section ID to its place in sections.

	/// The entry of the section with this runtime id; null when there is none.
	const SceneSectionSignalling* section(const std::string& sectionId) const;
};

/// The sections the native builder makes from the scene: the blocks of each track one after the other from its
/// first node, then one switch section for each connection and each pair of blocks that hold its two nodes.
SceneSectionInventory buildSceneSectionInventory(const SceneModel& scene);
/// The target that a signal failure of the section has, as the incident editor offers it: the signal that protects the
/// section when exactly one does and its id names no section, otherwise the block of the section unless a signal has the
/// same id. Empty for a switch section that no signal protects and for a reference that names no section. The reference
/// is a runtime id or a block id.
std::string sceneSignalFailureTarget(const SceneModel& scene, const SceneSectionInventory& inventory,
	const std::string& sectionReference);

/// The extent of the blocks of one track, or of the whole network when the track ID is empty.
/// Sections that a connection derives are left out; found is false when there is no block.
struct SceneBlockExtent {
	bool found = false;	  ///< True when there is a block.
	double startKm = 0.0; ///< Lowest chainage in kilometres.
	double endKm = 0.0;	  ///< Highest chainage in kilometres.
};
/// The extent of the block sections of the track, or of all tracks when trackId is empty.
SceneBlockExtent sceneBlockExtent(const SceneSectionInventory& inventory, const std::string& trackId);

/// Which signalling area decides the level of each section. A section belongs to an area when it lies
/// completely inside the area's range on its own track chainage; a track-scoped area applies to the
/// sections of its track and overrides the network-wide areas. When areas of one scope disagree, the
/// first of them decides and the others are listed as conflicts. Areas with a non-finite or empty range
/// or a level outside 0 to 5 take no part.
SceneSignallingAnalysis analyzeSignallingAreas(const SceneModel& scene, const SceneSectionInventory& inventory);
/// How the section right, which follows left on a route, joins it: at the same chainage of one track, or through
/// a connection or a shared switch block.
SceneSectionTransition classifySceneSectionTransition(const SceneModel& scene,
	const SceneSectionDescriptor& left, const SceneSectionDescriptor& right);
/// The direction of a route through these sections in order: 1 forward, -1 reverse, 0 when the joins contradict
/// each other or a pair does not join. A region jump is allowed only in a scene imported from a legacy case. A
/// single section is forward; without joins the chainages of the first and last section decide.
int sceneRouteDirection(const SceneModel& scene,
	const std::vector<const SceneSectionDescriptor*>& sections);
/// True when the chainage ranges [start, end) in kilometres overlap and the two section ids share a block.
bool sceneSectionsOverlap(const std::string& leftId, double leftStart, double leftEnd,
	const std::string& rightId, double rightStart, double rightEnd);
/// The station visits of the route, building the section inventory first.
SceneRouteTraversal buildSceneRouteTraversal(const SceneModel& scene, const SceneRoute& route);
/// The station visits of the route: the platform nodes and the positions of stations without platforms that it
/// passes, in travel order. Unresolved when a section reference does not resolve or the direction is unknown.
SceneRouteTraversal buildSceneRouteTraversal(const SceneModel& scene, const SceneRoute& route,
	const SceneSectionInventory& inventory);
/// The stations of the route in travel order, from its traversal.
SceneRouteStations sceneRouteStations(const SceneModel& scene, const SceneRoute& route,
	const SceneSectionInventory& inventory);
/// One resolution per stop of the service, in order. A stop binds to the first matching visit after the
/// previous bound stop.
std::vector<SceneStopResolution> resolveSceneServiceStops(const SceneModel& scene,
	const SceneService& service, const SceneRouteTraversal& traversal);
/// A chainage in kilometres as printf "%f" writes it, the form used in switch section ids.
std::string formatSceneSectionCoordinate(double coordinate);

/// What a person reads for a section: "B0 21.010 to 21.310 km" for a block section, or the piece on each
/// track, "B0 21.010 to 21.235, B1 21.252 to 21.450 km", for a switch section. A piece of a switch section
/// whose two ends round to the same value shows that value once. Kilometres have three decimals and a full
/// stop in every locale.
std::string sceneSectionLabel(const SceneSectionDescriptor& section);

/// The labels of the sections of the inventory (see SceneSectionLabels).
SceneSectionLabels sceneSectionLabels(const SceneSectionInventory& inventory);

#endif // SCENE_SECTION_INVENTORY_H
