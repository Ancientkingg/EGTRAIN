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
	{"tracks", "Add a track first"},
	{"nodes", "Add a track first"},
	{"arcs", "at least two nodes"},
	{"blocks", "this track"},
	{"connections", "two nodes"},
	{"stations", "its platforms"},
	{"platforms", "Add a station first"},
	{"signals", "the section it protects"},
	{"signalling_areas", "signalling system"},
	{"routes", "sections from the catalog"},
	{"block_dependencies", "the section it depends on"},
	{"single_track_restrictions", "protected start and end"},
	{"station_boundaries", "entrance section"},
};

const QString kNoCase = QStringLiteral("Open a case to edit infrastructure.");

} // namespace

int main() {
	bool ok = true;
	const QStringList facets = InfrastructurePanel::facetKeys();
	ok &= check(facets.size() == 13, "the panel has thirteen facets");
	ok &= check(static_cast<int>(std::size(kEmptyHints)) == facets.size(), "the test names a hint for each facet");

	QSet<QString> emptyTexts;
	for (const EmptyHint& hint : kEmptyHints) {
		const QString facet = QString::fromLatin1(hint.facet);
		ok &= check(facets.contains(facet), facet + " is a facet of the panel");

		// No scene: every facet asks for a case, whether the table is empty or not.
		ok &= check(infrastructureGuidanceText(facet, true, false) == kNoCase, facet + " without a scene and without rows");
		ok &= check(infrastructureGuidanceText(facet, false, false) == kNoCase, facet + " without a scene and with rows");

		// A scene and no rows: a sentence of its own that says what to do.
		const QString empty = infrastructureGuidanceText(facet, true, true);
		ok &= check(empty.startsWith(QLatin1String("No ")) && empty.endsWith(QLatin1Char('.')), facet + " empty: " + empty);
		ok &= check(empty.contains(QLatin1String(hint.mentions)), facet + " empty does not mention '" + hint.mentions + "': " + empty);
		ok &= check(!empty.contains(QLatin1String("Use Add to create one")), facet + " empty has the generic sentence");
		emptyTexts.insert(empty);
	}
	ok &= check(emptyTexts.size() == facets.size(), "each empty facet has a sentence of its own");

	// A scene and rows: only blocks and routes have something to explain.
	for (const QString& facet : facets) {
		const QString listed = infrastructureGuidanceText(facet, false, true);
		if (facet == QLatin1String("blocks"))
			ok &= check(listed == QLatin1String("Blocks are ordered on the chosen track; select a row to insert or move it."),
				"blocks with rows: " + listed);
		else if (facet == QLatin1String("routes"))
			ok &= check(listed == QLatin1String("Select a route to edit its ordered sections below."), "routes with rows: " + listed);
		else
			ok &= check(listed.isEmpty(), facet + " with rows has no sentence: " + listed);
	}

	// A key that is no facet has nothing to say once a scene is open.
	ok &= check(infrastructureGuidanceText(QStringLiteral("gantries"), true, true).isEmpty(), "an unknown key, empty");
	ok &= check(infrastructureGuidanceText(QStringLiteral("gantries"), false, true).isEmpty(), "an unknown key, with rows");
	ok &= check(infrastructureGuidanceText(QString(), true, true).isEmpty(), "no key");
	ok &= check(infrastructureGuidanceText(QStringLiteral("gantries"), true, false) == kNoCase, "an unknown key without a scene");

	return ok ? 0 : 1;
}
