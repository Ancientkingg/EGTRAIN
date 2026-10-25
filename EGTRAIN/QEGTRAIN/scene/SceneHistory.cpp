#include "scene/SceneHistory.h"

#include <array>
#include <cstddef>
#include <iterator>
#include <utility>

namespace {

// The memory a string holds outside its own object.
std::size_t textBytes(const std::string& text) {
	static const std::size_t inlineCapacity = std::string().capacity();
	return text.capacity() > inlineCapacity ? text.capacity() + 1 : 0;
}

template <class... Texts>
std::size_t texts(const Texts&... values) {
	return (std::size_t{0} + ... + textBytes(values));
}

// Per element of a set: the node links of a red-black tree, as three pointers and a colour.
constexpr std::size_t kSetNodeBytes = 4 * sizeof(void*);

// `heap` gives the memory that a value holds beyond its own object. `block` adds the array of
// a vector, or the nodes of a set, to the heap of its elements. A value type without a `heap`
// overload does not compile, so a new type in SceneModel cannot be left out unnoticed.
struct SceneBytes {
	template <class T>
	static std::size_t block(const std::vector<T>& values) {
		std::size_t total = values.capacity() * sizeof(T);
		for (const T& value : values)
			total += heap(value);
		return total;
	}
	static std::size_t block(const std::set<std::string>& values) {
		std::size_t total = values.size() * (sizeof(std::string) + kSetNodeBytes);
		for (const std::string& value : values)
			total += textBytes(value);
		return total;
	}

	static std::size_t heap(const std::string& text) { return textBytes(text); }
	static std::size_t heap(const std::array<double, 5>&) { return 0; }
	static std::size_t heap(const std::pair<int, double>&) { return 0; }
	static std::size_t heap(const SceneImportReportRow& value) { return texts(value.category, value.sourceFile); }
	static std::size_t heap(const SceneTrack& value) { return texts(value.id); }
	static std::size_t heap(const SceneTrackView& value) { return texts(value.trackId); }
	static std::size_t heap(const SceneNode& value) { return texts(value.id, value.trackId); }
	static std::size_t heap(const SceneArc& value) { return texts(value.id, value.trackId, value.fromNodeId, value.toNodeId); }
	static std::size_t heap(const SceneBlock& value) { return texts(value.id, value.trackId); }
	static std::size_t heap(const SceneConnection& value) { return texts(value.id, value.fromNodeId, value.toNodeId); }
	static std::size_t heap(const ScenePlatform& value) { return texts(value.id) + block(value.nodeIds); }
	static std::size_t heap(const SceneStation& value) { return texts(value.id, value.name) + block(value.platforms); }
	static std::size_t heap(const SceneStationView& value) { return texts(value.stationId) + block(value.regions) + block(value.corridors); }
	static std::size_t heap(const SceneSignal& value) { return texts(value.id, value.protectedSection); }
	static std::size_t heap(const SceneSignallingArea& value) { return texts(value.id, value.trackId); }
	static std::size_t heap(const SceneRoute& value) { return texts(value.id, value.corridor) + block(value.blocks); }
	static std::size_t heap(const SceneBlockDependency& value) { return texts(value.block, value.dependsOn); }
	static std::size_t heap(const SceneSingleTrackRestriction& value) {
		return texts(value.startBlock, value.endBlock, value.protectedStartBlock, value.protectedEndBlock);
	}
	static std::size_t heap(const SceneStationBoundary& value) { return texts(value.entranceBlock, value.exitBlock); }
	static std::size_t heap(const SceneTrainUnit& value) {
		return texts(value.id, value.sourceDataFile, value.sourceTractionFile) + block(value.tractionCurve);
	}
	static std::size_t heap(const SceneComposition& value) { return texts(value.id) + block(value.units); }
	static std::size_t heap(const SceneStop& value) { return texts(value.stationId, value.platformId); }
	static std::size_t heap(const SceneService& value) {
		return texts(value.id, value.operatingCode, value.composition, value.route, value.category, value.visualizationColor) + block(value.stops);
	}
	static std::size_t heap(const SceneIncident& value) { return texts(value.id, value.type, value.target); }
	static std::size_t heap(const SceneEntranceDelay& value) { return texts(value.serviceId, value.stationId); }
	static std::size_t heap(const SceneScenario& value) {
		return texts(value.id, value.name, value.description) + block(value.incidents) + block(value.entranceDelays);
	}
	static std::size_t heap(const ScenePassengerLeg& value) {
		return texts(value.id, value.originStationId, value.destinationStationId, value.serviceId);
	}
	static std::size_t heap(const ScenePassengerJourney& value) {
		return texts(value.id, value.activity, value.originStationId, value.destinationStationId) + block(value.legs);
	}
	static std::size_t heap(const ScenePassenger& value) { return texts(value.id) + block(value.journeys); }

	static std::size_t of(const SceneModel& model) {
		return sizeof(SceneModel)
			+ texts(model.savedWithAppVersion, model.name, model.description, model.baseTime, model.defaultScenarioId)
			+ block(model.sourceFiles) + block(model.importReport) + block(model.tracks)
			+ block(model.trackViews) + block(model.nodes) + block(model.arcs) + block(model.blocks)
			+ block(model.connections) + block(model.stations) + block(model.stationViews) + block(model.signals)
			+ block(model.signallingAreas) + block(model.routes) + block(model.blockDependencies)
			+ block(model.singleTrackRestrictions) + block(model.stationBoundaries) + block(model.trainUnits)
			+ block(model.compositions) + block(model.services) + block(model.scenarios) + block(model.passengers);
	}
};

// The parts of a scene that an edit can add or remove one of. A part inside another is listed
// after the one that holds it.
enum Section : std::size_t {
	TrackSection,
	TrackViewSection,
	NodeSection,
	ArcSection,
	BlockSection,
	ConnectionSection,
	StationSection,
	PlatformSection,
	StationViewSection,
	SignalSection,
	SignallingAreaSection,
	RouteSection,
	RouteBlockSection,
	BlockDependencySection,
	SingleTrackRestrictionSection,
	StationBoundarySection,
	TrainUnitSection,
	TractionRowSection,
	CompositionSection,
	CompositionUnitSection,
	ServiceSection,
	StopSection,
	ScenarioSection,
	IncidentSection,
	EntranceDelaySection,
	PassengerSection,
	JourneySection,
	LegSection,
	SectionCount
};

struct SectionInfo {
	const char* name;
	std::size_t parent; // SectionCount for a part of the scene itself.
};

const SectionInfo kSections[SectionCount] = {
	{"track", SectionCount},
	{"track view", SectionCount},
	{"node", SectionCount},
	{"arc", SectionCount},
	{"block", SectionCount},
	{"connection", SectionCount},
	{"station", SectionCount},
	{"platform", StationSection},
	{"station view", SectionCount},
	{"signal", SectionCount},
	{"signalling area", SectionCount},
	{"route", SectionCount},
	{"route block", RouteSection},
	{"block dependency", SectionCount},
	{"single-track restriction", SectionCount},
	{"station boundary", SectionCount},
	{"rolling stock unit", SectionCount},
	{"traction curve row", TrainUnitSection},
	{"composition", SectionCount},
	{"composition unit", CompositionSection},
	{"service", SectionCount},
	{"stop", ServiceSection},
	{"scenario", SectionCount},
	{"incident", ScenarioSection},
	{"entrance delay", ScenarioSection},
	{"passenger", SectionCount},
	{"journey", PassengerSection},
	{"passenger leg", JourneySection},
};

using SectionCounts = std::array<std::size_t, SectionCount>;

SectionCounts countSections(const SceneModel& scene) {
	SectionCounts counts{};
	counts[TrackSection] = scene.tracks.size();
	counts[TrackViewSection] = scene.trackViews.size();
	counts[NodeSection] = scene.nodes.size();
	counts[ArcSection] = scene.arcs.size();
	counts[BlockSection] = scene.blocks.size();
	counts[ConnectionSection] = scene.connections.size();
	counts[StationSection] = scene.stations.size();
	counts[StationViewSection] = scene.stationViews.size();
	counts[SignalSection] = scene.signals.size();
	counts[SignallingAreaSection] = scene.signallingAreas.size();
	counts[RouteSection] = scene.routes.size();
	counts[BlockDependencySection] = scene.blockDependencies.size();
	counts[SingleTrackRestrictionSection] = scene.singleTrackRestrictions.size();
	counts[StationBoundarySection] = scene.stationBoundaries.size();
	counts[TrainUnitSection] = scene.trainUnits.size();
	counts[CompositionSection] = scene.compositions.size();
	counts[ServiceSection] = scene.services.size();
	counts[ScenarioSection] = scene.scenarios.size();
	counts[PassengerSection] = scene.passengers.size();
	for (const SceneStation& station : scene.stations)
		counts[PlatformSection] += station.platforms.size();
	for (const SceneRoute& route : scene.routes)
		counts[RouteBlockSection] += route.blocks.size();
	for (const SceneTrainUnit& unit : scene.trainUnits)
		counts[TractionRowSection] += unit.tractionCurve.size();
	for (const SceneComposition& composition : scene.compositions)
		counts[CompositionUnitSection] += composition.units.size();
	for (const SceneService& service : scene.services)
		counts[StopSection] += service.stops.size();
	for (const SceneScenario& scenario : scene.scenarios) {
		counts[IncidentSection] += scenario.incidents.size();
		counts[EntranceDelaySection] += scenario.entranceDelays.size();
	}
	for (const ScenePassenger& passenger : scene.passengers) {
		counts[JourneySection] += passenger.journeys.size();
		for (const ScenePassengerJourney& journey : passenger.journeys)
			counts[LegSection] += journey.legs.size();
	}
	return counts;
}

// "Add service" or "Delete stop" when exactly one kind of part gained or lost exactly one
// element, and "Edit" for every other change. The elements that a part holds change with it,
// so a service that comes or goes with its stops is still one service.
std::string describeEdit(const SceneModel& before, const SceneModel& after) {
	const SectionCounts oldCounts = countSections(before);
	const SectionCounts newCounts = countSections(after);
	std::size_t changed = SectionCount;
	for (std::size_t section = 0; section < SectionCount; ++section) {
		if (oldCounts[section] == newCounts[section])
			continue;
		const std::size_t parent = kSections[section].parent;
		if (parent != SectionCount && oldCounts[parent] != newCounts[parent])
			continue;
		if (changed != SectionCount)
			return "Edit";
		changed = section;
	}
	if (changed == SectionCount)
		return "Edit";
	if (newCounts[changed] == oldCounts[changed] + 1)
		return std::string("Add ") + kSections[changed].name;
	if (oldCounts[changed] == newCounts[changed] + 1)
		return std::string("Delete ") + kSections[changed].name;
	return "Edit";
}

} // namespace

std::size_t estimateSceneModelBytes(const SceneModel& scene) {
	return SceneBytes::of(scene);
}

SceneHistory::SceneHistory(SceneHistoryLimits limits)
	: m_limits(limits) {
	reset(SceneModel{});
}

SceneHistoryEntry SceneHistory::makeEntry(const SceneModel& scene, std::string label) {
	SceneHistoryEntry entry;
	entry.scene = std::make_shared<const SceneModel>(scene);
	entry.label = std::move(label);
	entry.bytes = estimateSceneModelBytes(*entry.scene);
	return entry;
}

void SceneHistory::reset(const SceneModel& root, bool saved) {
	SceneHistoryEntry entry = makeEntry(root, std::string());
	m_bytes = entry.bytes;
	m_entries.clear();
	m_entries.push_back(std::move(entry));
	m_position = 0;
	m_saved = saved ? std::optional<std::size_t>(0) : std::nullopt;
}

void SceneHistory::record(const SceneModel& after, std::set<std::string> modifiedScenarioIds) {
	SceneHistoryEntry entry = makeEntry(after, describeEdit(*m_entries[m_position].scene, after));
	entry.modifiedScenarioIds = std::move(modifiedScenarioIds);

	const auto firstDropped = std::next(m_entries.begin(), static_cast<std::ptrdiff_t>(m_position + 1));
	for (auto dropped = firstDropped; dropped != m_entries.end(); ++dropped)
		m_bytes -= dropped->bytes;
	m_entries.erase(firstDropped, m_entries.end());
	if (m_saved && *m_saved > m_position)
		m_saved.reset();

	m_bytes += entry.bytes;
	m_entries.push_back(std::move(entry));
	++m_position;
	dropOldest();
}

void SceneHistory::dropOldest() {
	while (m_position > 0 && (m_position > m_limits.maxSteps || m_bytes > m_limits.maxBytes)) {
		m_bytes -= m_entries.front().bytes;
		m_entries.erase(m_entries.begin());
		--m_position;
		if (m_saved) {
			if (*m_saved == 0)
				m_saved.reset();
			else
				--*m_saved;
		}
	}
}

bool SceneHistory::canUndo() const {
	return m_position > 0;
}

bool SceneHistory::canRedo() const {
	return m_position + 1 < m_entries.size();
}

std::string SceneHistory::undoLabel() const {
	return canUndo() ? m_entries[m_position].label : std::string();
}

std::string SceneHistory::redoLabel() const {
	return canRedo() ? m_entries[m_position + 1].label : std::string();
}

const SceneHistoryEntry* SceneHistory::undo() {
	if (!canUndo())
		return nullptr;
	--m_position;
	return &m_entries[m_position];
}

const SceneHistoryEntry* SceneHistory::redo() {
	if (!canRedo())
		return nullptr;
	++m_position;
	return &m_entries[m_position];
}

std::shared_ptr<const SceneModel> SceneHistory::head() const {
	return m_entries[m_position].scene;
}

void SceneHistory::markSaved() {
	m_saved = m_position;
	m_entries[m_position].modifiedScenarioIds.clear();
}

bool SceneHistory::isAtSavedPoint() const {
	return m_saved && *m_saved == m_position;
}

std::size_t SceneHistory::undoCount() const {
	return m_position;
}

std::size_t SceneHistory::redoCount() const {
	return m_entries.size() - 1 - m_position;
}

std::size_t SceneHistory::bytes() const {
	return m_bytes;
}
