#include "editors/InfrastructurePanel.h"
#include "editors/infrastructure/InfrastructureGuidance.h"

#include <QSet>
#include <QString>
#include <QStringList>

#include <iostream>
#include <iterator>

namespace {

bool check(bool condition, const QString& message) {
	if (!condition)
		std::cerr << "failed: " << message.toStdString() << "\n";
	return condition;
}

// What the sentence of an empty facet has to name: the entity that must exist first, or the field that is chosen next.
struct EmptyHint {
	const char* facet;
	const char* mentions;
};

constexpr EmptyHint kEmptyHints[] = {
	{"tracks", "Add a track:"},
	{"nodes", "its track in the table"},
	{"arcs", "No arcs yet. Add one"},
	{"blocks", "this track"},
	{"connections", "No connections yet. Add one"},
	{"stations", "its platforms"},
	{"platforms", "set its station"},
	{"signals", "the section it protects"},
	{"signalling_areas", "signalling system"},
	{"routes", "sections from the catalog"},
	{"block_dependencies", "the section it depends on"},
	{"single_track_restrictions", "protected start and end"},
	{"station_boundaries", "entrance section"},
};

// The scene of the other tests: it has what every facet builds on.
InfrastructureGuidanceState sceneState() {
	InfrastructureGuidanceState state;
	state.loaded = true;
	state.editable = true;
	state.tracks = 1;
	state.nodes = 2;
	state.stations = 1;
	return state;
}

const QString kNoCase = QStringLiteral("Open a case to edit infrastructure.");

} // namespace

int main() {
	bool ok = true;
	const QStringList facets = InfrastructurePanel::facetKeys();
	ok &= check(facets.size() == 13, "the panel has thirteen facets");
	ok &= check(static_cast<int>(std::size(kEmptyHints)) == facets.size(), "the test names a hint for each facet");

	const InfrastructureGuidanceState noScene;
	QSet<QString> emptyTexts;
	for (const EmptyHint& hint : kEmptyHints) {
		const QString facet = QString::fromLatin1(hint.facet);
		ok &= check(facets.contains(facet), facet + " is a facet of the panel");

		// No scene: every facet asks for a case, whether the table is empty or not.
		ok &= check(infrastructureGuidanceText(facet, true, noScene) == kNoCase, facet + " without a scene and without rows");
		ok &= check(infrastructureGuidanceText(facet, false, noScene) == kNoCase, facet + " without a scene and with rows");

		// A scene and no rows: a sentence of its own that says what to do.
		const QString empty = infrastructureGuidanceText(facet, true, sceneState());
		ok &= check(empty.startsWith(QLatin1String("No ")) && empty.endsWith(QLatin1Char('.')), facet + " empty: " + empty);
		ok &= check(empty.contains(QLatin1String(hint.mentions)), facet + " empty does not mention '" + hint.mentions + "': " + empty);
		ok &= check(!empty.contains(QLatin1String("Use Add to create one")), facet + " empty has the generic sentence");
		emptyTexts.insert(empty);
	}
	ok &= check(emptyTexts.size() == facets.size(), "each empty facet has a sentence of its own");

	// A scene and rows: only blocks and routes have something to explain.
	for (const QString& facet : facets) {
		const QString listed = infrastructureGuidanceText(facet, false, sceneState());
		if (facet == QLatin1String("blocks"))
			ok &= check(listed == QLatin1String("Select a block to insert a new one in its place or to move it."),
				"blocks with rows: " + listed);
		else if (facet == QLatin1String("routes"))
			ok &= check(listed == QLatin1String("Select a route to edit its ordered sections below."), "routes with rows: " + listed);
		else
			ok &= check(listed.isEmpty(), facet + " with rows has no sentence: " + listed);
	}

	// A key that is no facet has nothing to say once a scene is open.
	ok &= check(infrastructureGuidanceText(QStringLiteral("gantries"), true, sceneState()).isEmpty(), "an unknown key, empty");
	ok &= check(infrastructureGuidanceText(QStringLiteral("gantries"), false, sceneState()).isEmpty(), "an unknown key, with rows");
	ok &= check(infrastructureGuidanceText(QString(), true, sceneState()).isEmpty(), "no key");
	ok &= check(infrastructureGuidanceText(QStringLiteral("gantries"), true, noScene) == kNoCase, "an unknown key without a scene");

	// A prerequisite is named only while the scene lacks it.
	const auto text = [](const char* facet, int tracks, int nodes, int stations) {
		InfrastructureGuidanceState state = sceneState();
		state.tracks = tracks;
		state.nodes = nodes;
		state.stations = stations;
		return infrastructureGuidanceText(QLatin1String(facet), true, state);
	};
	ok &= check(text("nodes", 0, 0, 0).contains(QLatin1String("Add a track first")), "nodes without a track");
	ok &= check(!text("nodes", 1, 0, 0).contains(QLatin1String("Add a track first")), "nodes with a track");
	ok &= check(text("arcs", 1, 1, 1).contains(QLatin1String("at least two nodes")), "arcs with one node");
	ok &= check(!text("arcs", 1, 2, 1).contains(QLatin1String("add at least two nodes")), "arcs with two nodes");
	ok &= check(text("blocks", 0, 0, 0).contains(QLatin1String("Add a track first")), "blocks without a track");
	ok &= check(text("blocks", 1, 0, 0).contains(QLatin1String("No blocks on this track")), "blocks with a track");
	ok &= check(text("connections", 1, 1, 1).contains(QLatin1String("add the nodes first")), "connections with one node");
	ok &= check(!text("connections", 1, 2, 1).contains(QLatin1String("first")), "connections with two nodes");
	ok &= check(text("platforms", 1, 2, 0).contains(QLatin1String("Add a station first")), "platforms without a station");
	ok &= check(!text("platforms", 1, 2, 1).contains(QLatin1String("Add a station first")), "platforms with a station");

	// While a run holds the scene Add is off: no sentence asks to add, and the empty ones say why.
	for (const QString& facet : facets) {
		InfrastructureGuidanceState busy = sceneState();
		busy.editable = false;
		const QString empty = infrastructureGuidanceText(facet, true, busy);
		ok &= check(empty.startsWith(QLatin1String("No ")) && empty.endsWith(QLatin1String("Stop the run to edit.")),
			facet + " empty during a run: " + empty);
		ok &= check(!empty.contains(QLatin1String("Add ")), facet + " empty during a run asks to add: " + empty);
		const QString listed = infrastructureGuidanceText(facet, false, busy);
		ok &= check(listed.isEmpty() || listed == QLatin1String("Stop the run to edit."),
			facet + " with rows during a run: " + listed);
		// A missing scene wins over a missing edit right.
		busy.loaded = false;
		ok &= check(infrastructureGuidanceText(facet, true, busy) == kNoCase, facet + " unloaded and not editable");
	}
	ok &= check(infrastructureGuidanceText(QStringLiteral("gantries"), true, InfrastructureGuidanceState{true, false, 0, 0, 0}).isEmpty(),
		"an unknown key during a run");

	return ok ? 0 : 1;
}
