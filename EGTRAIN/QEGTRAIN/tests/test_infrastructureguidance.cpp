#include "editors/InfrastructurePanel.h"
#include "editors/infrastructure/InfrastructureGuidance.h"

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>

#include <iterator>

namespace {

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

// The sentence of an empty facet in the scene of the other tests with these counts.
QString emptyText(const char* facet, int tracks, int nodes, int stations) {
	InfrastructureGuidanceState state = sceneState();
	state.tracks = tracks;
	state.nodes = nodes;
	state.stations = stations;
	return infrastructureGuidanceText(QLatin1String(facet), true, state);
}

// A row for each facet of the panel.
void addFacetRows() {
	QTest::addColumn<QString>("facet");
	for (const QString& facet : InfrastructurePanel::facetKeys())
		QTest::newRow(qPrintable(facet)) << facet;
}

const QString kNoCase = QStringLiteral("Open a case to edit infrastructure.");

} // namespace

class TestInfrastructureGuidance : public QObject {
	Q_OBJECT

private slots:
	void panelHasThirteenFacets();
	void emptyFacetSaysWhatToDo_data();
	void emptyFacetSaysWhatToDo();
	void eachEmptyFacetHasOwnSentence();
	void onlyBlocksAndRoutesExplainRows_data();
	void onlyBlocksAndRoutesExplainRows();
	void unknownKeyHasNoSentence();
	void namesMissingPrerequisite();
	void runAsksToStopInsteadOfAdding_data();
	void runAsksToStopInsteadOfAdding();
};

void TestInfrastructureGuidance::panelHasThirteenFacets() {
	const QStringList facets = InfrastructurePanel::facetKeys();
	// The panel has thirteen facets.
	QCOMPARE(facets.size(), qsizetype(13));
	// The test names a hint for each facet.
	QCOMPARE(qsizetype(std::size(kEmptyHints)), facets.size());
}

void TestInfrastructureGuidance::emptyFacetSaysWhatToDo_data() {
	QTest::addColumn<QString>("facet");
	QTest::addColumn<QString>("mentions");
	for (const EmptyHint& hint : kEmptyHints)
		QTest::newRow(hint.facet) << QString::fromLatin1(hint.facet) << QString::fromLatin1(hint.mentions);
}

void TestInfrastructureGuidance::emptyFacetSaysWhatToDo() {
	QFETCH(QString, facet);
	QFETCH(QString, mentions);
	QVERIFY2(InfrastructurePanel::facetKeys().contains(facet), qPrintable(facet + " is a facet of the panel"));

	const InfrastructureGuidanceState noScene;
	// No scene: every facet asks for a case, whether the table is empty or not.
	QCOMPARE(infrastructureGuidanceText(facet, true, noScene), kNoCase);
	QCOMPARE(infrastructureGuidanceText(facet, false, noScene), kNoCase);

	// A scene and no rows: a sentence of its own that says what to do.
	const QString empty = infrastructureGuidanceText(facet, true, sceneState());
	QVERIFY2(empty.startsWith(QLatin1String("No ")), qPrintable(facet + " empty: " + empty));
	QVERIFY2(empty.endsWith(QLatin1Char('.')), qPrintable(facet + " empty: " + empty));
	QVERIFY2(empty.contains(mentions), qPrintable(facet + " empty does not mention '" + mentions + "': " + empty));
	QVERIFY2(!empty.contains(QLatin1String("Use Add to create one")), qPrintable(facet + " empty has the generic sentence"));
}

void TestInfrastructureGuidance::eachEmptyFacetHasOwnSentence() {
	QSet<QString> emptyTexts;
	for (const EmptyHint& hint : kEmptyHints)
		emptyTexts.insert(infrastructureGuidanceText(QString::fromLatin1(hint.facet), true, sceneState()));
	// Each empty facet has a sentence of its own.
	QCOMPARE(emptyTexts.size(), InfrastructurePanel::facetKeys().size());
}

void TestInfrastructureGuidance::onlyBlocksAndRoutesExplainRows_data() {
	addFacetRows();
}

// A scene and rows: only blocks and routes have something to explain.
void TestInfrastructureGuidance::onlyBlocksAndRoutesExplainRows() {
	QFETCH(QString, facet);
	const QString listed = infrastructureGuidanceText(facet, false, sceneState());
	if (facet == QLatin1String("blocks"))
		QCOMPARE(listed, QLatin1String("Select a block to insert a new one in its place or to move it."));
	else if (facet == QLatin1String("routes"))
		QCOMPARE(listed, QLatin1String("Select a route to edit its ordered sections below."));
	else
		QVERIFY2(listed.isEmpty(), qPrintable(facet + " with rows has no sentence: " + listed));
}

// A key that is no facet has nothing to say once a scene is open.
void TestInfrastructureGuidance::unknownKeyHasNoSentence() {
	QVERIFY2(infrastructureGuidanceText(QStringLiteral("gantries"), true, sceneState()).isEmpty(), "an unknown key, empty");
	QVERIFY2(infrastructureGuidanceText(QStringLiteral("gantries"), false, sceneState()).isEmpty(), "an unknown key, with rows");
	QVERIFY2(infrastructureGuidanceText(QString(), true, sceneState()).isEmpty(), "no key");
	// An unknown key without a scene.
	QCOMPARE(infrastructureGuidanceText(QStringLiteral("gantries"), true, InfrastructureGuidanceState()), kNoCase);
	QVERIFY2(infrastructureGuidanceText(QStringLiteral("gantries"), true, InfrastructureGuidanceState{true, false, 0, 0, 0}).isEmpty(),
		"an unknown key during a run");
}

// A prerequisite is named only while the scene lacks it.
void TestInfrastructureGuidance::namesMissingPrerequisite() {
	QVERIFY2(emptyText("nodes", 0, 0, 0).contains(QLatin1String("Add a track first")), "nodes without a track");
	QVERIFY2(!emptyText("nodes", 1, 0, 0).contains(QLatin1String("Add a track first")), "nodes with a track");
	QVERIFY2(emptyText("arcs", 1, 1, 1).contains(QLatin1String("at least two nodes")), "arcs with one node");
	QVERIFY2(!emptyText("arcs", 1, 2, 1).contains(QLatin1String("add at least two nodes")), "arcs with two nodes");
	QVERIFY2(emptyText("blocks", 0, 0, 0).contains(QLatin1String("Add a track first")), "blocks without a track");
	QVERIFY2(emptyText("blocks", 1, 0, 0).contains(QLatin1String("No blocks on this track")), "blocks with a track");
	QVERIFY2(emptyText("connections", 1, 1, 1).contains(QLatin1String("add the nodes first")), "connections with one node");
	QVERIFY2(!emptyText("connections", 1, 2, 1).contains(QLatin1String("first")), "connections with two nodes");
	QVERIFY2(emptyText("platforms", 1, 2, 0).contains(QLatin1String("Add a station first")), "platforms without a station");
	QVERIFY2(!emptyText("platforms", 1, 2, 1).contains(QLatin1String("Add a station first")), "platforms with a station");
}

void TestInfrastructureGuidance::runAsksToStopInsteadOfAdding_data() {
	addFacetRows();
}

// While a run holds the scene Add is off: no sentence asks to add, and the empty ones say why.
void TestInfrastructureGuidance::runAsksToStopInsteadOfAdding() {
	QFETCH(QString, facet);
	InfrastructureGuidanceState busy = sceneState();
	busy.editable = false;
	const QString empty = infrastructureGuidanceText(facet, true, busy);
	QVERIFY2(empty.startsWith(QLatin1String("No ")), qPrintable(facet + " empty during a run: " + empty));
	QVERIFY2(empty.endsWith(QLatin1String("Stop the run to edit.")), qPrintable(facet + " empty during a run: " + empty));
	QVERIFY2(!empty.contains(QLatin1String("Add ")), qPrintable(facet + " empty during a run asks to add: " + empty));
	const QString listed = infrastructureGuidanceText(facet, false, busy);
	QVERIFY2(listed.isEmpty() || listed == QLatin1String("Stop the run to edit."), qPrintable(facet + " with rows during a run: " + listed));
	// A missing scene wins over a missing edit right.
	busy.loaded = false;
	QCOMPARE(infrastructureGuidanceText(facet, true, busy), kNoCase);
}

QTEST_APPLESS_MAIN(TestInfrastructureGuidance)
#include "test_infrastructureguidance.moc"
