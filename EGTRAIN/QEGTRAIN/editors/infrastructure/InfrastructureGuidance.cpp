#include "editors/infrastructure/InfrastructureGuidance.h"

namespace {

struct Guidance {
	const char* facet;
	// Shown when the table lists nothing.
	const char* whenEmpty;
	// Shown when the table lists entities; empty when the facet has no hint.
	const char* whenListed;
};

// One entry for each facet of the panel, in the order of its combo. The text for an empty table names what has to exist
// before Add gives a usable entity.
constexpr Guidance kGuidance[] = {
	{"tracks", "No tracks yet. Add a track first: nodes, arcs and blocks are placed on tracks.", ""},
	{"nodes", "No nodes yet. Add a track first, then add nodes on it.", ""},
	{"arcs", "No arcs yet. An arc joins two nodes of a track, so add at least two nodes first.", ""},
	{"blocks", "No blocks on this track. Choose another track or add a block.",
		"Blocks are ordered on the chosen track; select a row to insert or move it."},
	{"connections", "No connections yet. A connection joins two nodes, so add the nodes first.", ""},
	{"stations", "No stations yet. Add a station, then add its platforms.", ""},
	{"platforms", "No platforms yet. Add a station first; a new platform goes to the first station.", ""},
	{"signals", "No signals yet. Add a signal, then choose the section it protects.", ""},
	{"signalling_areas", "No signalling areas yet. Add an area, then choose its signalling system.", ""},
	{"routes", "No routes yet. Add a route, then choose sections from the catalog.",
		"Select a route to edit its ordered sections below."},
	{"block_dependencies", "No block dependencies yet. Add one, then choose a block and the section it depends on.", ""},
	{"single_track_restrictions",
		"No single-track restrictions yet. Add one, then choose its start and end sections and the protected start and end.", ""},
	{"station_boundaries", "No station boundaries yet. Add one, then choose its entrance section and, if it has one, its exit.", ""},
};

} // namespace

QString infrastructureGuidanceText(const QString& facet, bool empty, bool loaded) {
	if (!loaded)
		return QStringLiteral("Open a case to edit infrastructure.");
	for (const Guidance& guidance : kGuidance) {
		if (facet == QLatin1String(guidance.facet))
			return QString::fromLatin1(empty ? guidance.whenEmpty : guidance.whenListed);
	}
	return QString();
}
