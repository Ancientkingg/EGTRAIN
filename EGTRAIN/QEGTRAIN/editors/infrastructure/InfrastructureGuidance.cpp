#include "editors/infrastructure/InfrastructureGuidance.h"

namespace {

struct Guidance {
	const char* facet;
	// What the facet lists, for the sentence that explains why Add is off.
	const char* noun;
	// Shown when the table lists nothing.
	const char* whenEmpty;
	// Shown when the table lists entities; empty when the facet has no hint.
	const char* whenListed;
};

// One entry for each facet of the panel, in the order of its combo. The text for an empty table says what to do next.
// Where an entity needs another one first, the sentence for a scene that lacks it is in prerequisiteText().
constexpr Guidance kGuidance[] = {
	{"tracks", "tracks", "No tracks yet. Add a track: nodes, arcs and blocks are placed on tracks.", ""},
	{"nodes", "nodes", "No nodes yet. Add one, then choose its track in the table.", ""},
	{"arcs", "arcs", "No arcs yet. Add one, then choose the two nodes it joins in the table.", ""},
	{"blocks", "blocks", "No blocks on this track. Choose another track or add a block.",
		"Select a block to insert a new one in its place or to move it."},
	{"connections", "connections", "No connections yet. Add one, then choose the two nodes it joins in the table.", ""},
	{"stations", "stations", "No stations yet. Add a station, then add its platforms.", ""},
	{"platforms", "platforms", "No platforms yet. Add one, then set its station in the table.", ""},
	{"signals", "signals", "No signals yet. Add a signal, then choose the section it protects.", ""},
	{"signalling_areas", "signalling areas", "No signalling areas yet. Add an area, then choose its signalling system.", ""},
	{"routes", "routes", "No routes yet. Add a route, then choose sections from the catalog.",
		"Select a route to edit its ordered sections below."},
	{"block_dependencies", "block dependencies", "No block dependencies yet. Add one, then choose a block and the section it depends on.", ""},
	{"single_track_restrictions", "single-track restrictions",
		"No single-track restrictions yet. Add one, then choose its start and end sections and the protected start and end.", ""},
	{"station_boundaries", "station boundaries", "No station boundaries yet. Add one, then choose its entrance section and, if it has one, its exit.", ""},
};

// The sentence for an empty table whose facet needs an entity that the scene does not have yet; null when it has it.
const char* prerequisiteText(const QString& facet, const InfrastructureGuidanceState& state) {
	if (facet == QLatin1String("nodes") && state.tracks < 1)
		return "No nodes yet. Add a track first, then add nodes on it.";
	if (facet == QLatin1String("arcs") && state.nodes < 2)
		return "No arcs yet. An arc joins two nodes, so add at least two nodes first.";
	if (facet == QLatin1String("blocks") && state.tracks < 1)
		return "No blocks yet. Add a track first, then add blocks on it.";
	if (facet == QLatin1String("connections") && state.nodes < 2)
		return "No connections yet. A connection joins two nodes, so add the nodes first.";
	if (facet == QLatin1String("platforms") && state.stations < 1)
		return "No platforms yet. Add a station first, then add its platforms.";
	return nullptr;
}

} // namespace

QString infrastructureGuidanceText(const QString& facet, bool empty, const InfrastructureGuidanceState& state) {
	if (!state.loaded)
		return QStringLiteral("Open a case to edit infrastructure.");
	for (const Guidance& guidance : kGuidance) {
		if (facet != QLatin1String(guidance.facet))
			continue;
		const char* listed = guidance.whenListed;
		if (!state.editable) {
			const QString why = QStringLiteral("Stop the run to edit.");
			if (empty)
				return QStringLiteral("No %1 yet. ").arg(QLatin1String(guidance.noun)) + why;
			return *listed ? why : QString();
		}
		if (!empty)
			return QString::fromLatin1(listed);
		const char* missing = prerequisiteText(facet, state);
		return QString::fromLatin1(missing ? missing : guidance.whenEmpty);
	}
	return QString();
}
