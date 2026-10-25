#include "scene/SceneModel.h"
#include "scene/SceneExporter.h"
#include "scene/SceneImporter.h"
#include "scene/SceneWriter.h"
#include <chrono>
#include <filesystem>
#include "scene/SceneValidator.h"
#include "scene/SectionInventory.h"
#include "simulation/Signalling.h"
#include "simulation/SimulationRun.h"
#include "util/Log.h"

// The Qt headers come before the undef: Qt defines `signals`, and the scene model has a member of that name.
#include <QObject>
#include <QScopeGuard>
#include <QString>
#include <QTest>
#ifdef signals
#undef signals
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <list>
#include <locale>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

// The builders write the run of the process. The slots run in the order of their declaration, as the groups of checks did
// before them, and a slot that reads what a build left starts with that build.
class TestSceneBuilder : public QObject {
	Q_OBJECT

private slots:
	void cleanupTestCase();
	void buildsCompleteScene();
	void regionalRouteFollowsFirstSegment();
	void derivesSwitchSectionFromStableIds();
	void rejectsDerivedRouteReversals();
	void appliesSignallingAreasBeforeRoutes();
	void explicitReverseKeepsReferenceCoordinate();
	void descendingBlockOrderReversesRoute();
	void declaredTopologyControlsDirection();
	void multiRegionRouteKeepsAuthoredOrder();
	void partialAreaLeavesOtherSectionsUnset();
	void labelsReadAsTrackAndKilometres();
	void showsRoundedKilometreOnce();
	void sharedLabelEndsWithId();
	void unknownReferenceKeepsId();
	void labelWithIdNamesIdOnce();
	void ambiguousReferenceKeepsId();
	void labelsExactlyResolvableReferences();
	void routeSectionsCarrySectionLevels();
	void areaLevelsOfLineSections_data();
	void areaLevelsOfLineSections();
	void trackScopedAreaOverridesOwnTrack();
	void reversedRouteKeepsSectionLevels();
	void levelWarningAgreesWithBuild_data();
	void levelWarningAgreesWithBuild();
	void areasSurviveExportAndImport_data();
	void areasSurviveExportAndImport();
	void buildsLongTrack();
	void buildsManySections();
	void routeHoldsExactlyItsSections();
	void routeEndsWhereLastSectionEnds();
	void copiesNode();
	void nodesDifferingInOneMemberDiffer_data();
	void nodesDifferingInOneMemberDiffer();
	void copiesArc();
	void arcsDifferingInOneMemberDiffer_data();
	void arcsDifferingInOneMemberDiffer();
	void copiesSection();
	void sectionsDifferingInOneMemberDiffer_data();
	void sectionsDifferingInOneMemberDiffer();
	void namesTrackDetectionBorders();
	void logsRouteSetup();
	void logsCutOutsideSection();
	void logsCutsAtSwitchEdges();
	void logsCrossedElementsWithoutName();
	void logsDetectionBorderWarnings();
	void logsShortSwitchNames();
	void logsSwitchConnectedToOtherBlocks();
	void logsTooManyConnectedSections();
	void occupiesSectionAndConnectedSections();
	void tailConnectsSectionsOfPreviousSection();
	void occupiesTenConnectedSections();
	void releasesSectionAndConnectedSections();
	void releasesSwitchWithItsBlocks();
	void switchWithoutBlocksChangesNoList();
	void occupiesAndReleasesDoubleSwitch();
	void sceneSectionsListTheirNodes();
	void switchSectionListsNodesOfBothBlocks();
	void cutSectionListsNodesToCut();
	void copiesShareNodeList();
	void cutSectionOwnsItsList_data();
	void cutSectionOwnsItsList();
	void routeSectionsShareNetworkLists();
	void crossoverElsewhereKeepsOwnStart();
	void crossoverAtSharedSwitchTakesOverStart();
	void lastLocationHasNoClearingTime_data();
	void lastLocationHasNoClearingTime();
	void failedBuildKeepsPreviousRuntime_data();
	void failedBuildKeepsPreviousRuntime();
	void buildsAgainAfterFailedBuilds();

private:
	// The folder of the export round trips. Its rows share it, so a later row saves and exports over the folders of the
	// earlier ones.
	std::filesystem::path m_roundtripRoot;
};

void TestSceneBuilder::cleanupTestCase() {
	std::error_code ec;
	if (!m_roundtripRoot.empty())
		std::filesystem::remove_all(m_roundtripRoot, ec);
}

static SceneModel tinyScene() {
	SceneModel scene;
	scene.name = "tiny-native";
	scene.tracks = {{"track.z"}};
	scene.nodes = {{"node.0", "track.z", 0.0, 0.0},
		{"node.1", "track.z", 1.0, 0.2},
		{"node.2", "track.z", 2.0, 0.0}};
	scene.arcs = {{"arc.0", "track.z", "node.0", "node.1", 0.0, 1.0, 20.0},
		{"arc.1", "track.z", "node.1", "node.2", 30.0, -1.0, 18.0}};
	scene.blocks = {{"block.a", "track.z", 1.0}, {"block.b", "track.z", 1.0}};
	scene.trackViews.push_back({"track.z", -1, 2});
	SceneStation station;
	station.id = "station.tiny";
	station.name = "Tiny";
	station.platforms.push_back({"platform.1", {"node.1"}});
	scene.stations.push_back(station);
	SceneStation destination;
	destination.id = "station.end";
	destination.name = "End";
	destination.platforms.push_back({"platform.2", {"node.2"}});
	scene.stations.push_back(destination);
	scene.stationViews = {
		{"station.tiny", 1.0, 0.1, {{2, 1.0}}, {"corridor.tiny"}},
		{"station.end", 1.0, 0.2, {{2, 2.0}}, {"corridor.tiny"}},
	};
	scene.signals.push_back({"block.a", ""});
	SceneRoute route;
	route.id = "route.tiny";
	route.blocks = {"block.a", "block.b"};
	route.hasCorridor = true;
	route.corridor = "corridor.tiny";
	scene.routes.push_back(route);
	scene.blockDependencies.push_back({"block.a", "block.b"});
	scene.singleTrackRestrictions.push_back({"block.a", "block.b", "block.a", "block.b"});
	scene.stationBoundaries.push_back({"block.a", true, "block.b", false});
	return scene;
}

static SceneModel stableConnectionScene() {
	SceneModel scene;
	scene.tracks = {{"alpha"}, {"beta"}};
	scene.nodes = {{"alpha.start", "alpha", 0.0, 0.0}, {"alpha.end", "alpha", 1.0, 0.0},
		{"beta.start", "beta", 2.0, 0.0}, {"beta.end", "beta", 3.0, 0.0}};
	scene.arcs = {{"alpha.arc", "alpha", "alpha.start", "alpha.end", 0.0, 0.0, 10.0},
		{"beta.arc", "beta", "beta.start", "beta.end", 0.0, 0.0, 10.0}};
	scene.blocks = {{"alpha.block", "alpha", 1.0}, {"beta.block", "beta", 1.0}};
	scene.connections.push_back({"switch.stable", "beta.start", "alpha.end", true, 7.5});
	return scene;
}

static SceneModel switchChainScene() {
	SceneModel scene;
	scene.tracks = {{"switch-a"}, {"switch-b"}, {"switch-c"}};
	scene.nodes = {{"a.0", "switch-a", 0.0, 0.0}, {"a.1", "switch-a", 1.0, 0.0},
		{"b.0", "switch-b", 2.0, 0.0}, {"b.1", "switch-b", 4.0, 0.0},
		{"c.0", "switch-c", 5.0, 0.0}, {"c.1", "switch-c", 6.0, 0.0}};
	scene.arcs = {{"a.arc", "switch-a", "a.0", "a.1", 0.0, 0.0, 20.0},
		{"b.arc", "switch-b", "b.0", "b.1", 0.0, 0.0, 20.0},
		{"c.arc", "switch-c", "c.0", "c.1", 0.0, 0.0, 20.0}};
	scene.blocks = {{"a.block", "switch-a", 1.0}, {"b.block", "switch-b", 2.0},
		{"c.block", "switch-c", 1.0}};
	scene.connections = {{"a-to-b", "a.1", "b.0", false, 0.0},
		{"b-to-c", "b.1", "c.0", false, 0.0}};
	return scene;
}

static SceneModel signallingAreasScene() {
	SceneModel scene = stableConnectionScene();
	scene.routes.push_back({"route.switch", {"@alpha.block@-1.000000/@beta.block@-2.000000"}, false, {}, false});
	scene.signallingAreas = {
		{"network-area", 0.0, 3.0, 1, {}},
		{"alpha-area", 0.0, 3.0, 3, "alpha"},
	};
	return scene;
}

static SceneModel multiRegionRouteScene() {
	SceneModel scene;
	scene.tracks = {{"region.a"}, {"region.b"}, {"region.c"}};
	scene.nodes = {{"a.0", "region.a", 0.0, 0.0}, {"a.1", "region.a", 1.0, 0.0},
		{"b.0", "region.b", 1000.0, 0.0}, {"b.1", "region.b", 1001.0, 0.0},
		{"b.2", "region.b", 1002.0, 0.0}, {"c.0", "region.c", 500.0, 0.0},
		{"c.1", "region.c", 501.0, 0.0}};
	scene.arcs = {{"a.arc", "region.a", "a.0", "a.1", 0.0, 0.0, 10.0},
		{"b.arc.0", "region.b", "b.0", "b.1", 0.0, 0.0, 10.0},
		{"b.arc.1", "region.b", "b.1", "b.2", 0.0, 0.0, 10.0},
		{"c.arc", "region.c", "c.0", "c.1", 0.0, 0.0, 10.0}};
	scene.blocks = {{"a.block", "region.a", 1.0}, {"b.block", "region.b", 2.0},
		{"c.block", "region.c", 1.0}};
	scene.connections.push_back({"region.jump", "a.1", "b.0", true, 10.0});
	scene.connections.push_back({"region.jump.2", "b.2", "c.0", true, 10.0});
	SceneRoute route;
	route.id = "route.regions";
	route.blocks = {"a.block", "b.block", "c.block"};
	scene.routes.push_back(route);
	return scene;
}

static bool hasDiagnostic(const std::vector<SceneDiagnostic>& diagnostics, SceneSeverity severity,
	const std::string& file, const std::string& codePart) {
	for (const auto& diagnostic : diagnostics)
		if (diagnostic.severity == severity && diagnostic.file == file
			&& diagnostic.code.find(codePart) != std::string::npos)
			return true;
	return false;
}

void TestSceneBuilder::buildsCompleteScene() {
	auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), tinyScene());
	QVERIFY2(!hasErrors(diagnostics), "complete scene builds without errors");
	// Track nodes and arcs retain canonical topology.
	QCOMPARE(defaultRun().infra.numTrackLines, 1);
	QCOMPARE(defaultRun().infra.blockSets[0].numNodes, 3);
	QCOMPARE(defaultRun().infra.blockSets[0].arcs, 2);
	// Authored track layout retains a valid negative display level.
	QVERIFY(defaultRun().infra.blockSets[0].hasGraphLayout);
	QCOMPARE(defaultRun().infra.blockSets[0].graphID, -1);
	QCOMPARE(defaultRun().infra.blockSets[0].region, 2);
	// Two canonical blocks become two straight runtime sections.
	QCOMPARE(defaultRun().infra.numBlocks, 2);
	// Block IDs use the runtime boundary form.
	QCOMPARE(defaultRun().infra.sections[0].ID, "@block.a@");
	QCOMPARE(defaultRun().infra.sections[1].ID, "@block.b@");
	// Station and platform counts are bound.
	QCOMPARE(defaultRun().infra.numStations, 2);
	QCOMPARE(defaultRun().numPlatforms, 2);
	// Platform node anchors a station without a separate position.
	QCOMPARE_EQ(defaultRun().infra.stations[0].X, 1.0);
	// Authored station layout reaches the runtime station model.
	QCOMPARE_EQ(defaultRun().infra.stations[0].latitude, 1.0);
	QCOMPARE_EQ(defaultRun().infra.stations[0].longitude, 0.1);
	QCOMPARE(defaultRun().infra.stations[0].regions, std::vector<int>({2}));
	QCOMPARE_EQ(defaultRun().infra.stations[0].regionX[2], 1.0);
	QCOMPARE(defaultRun().infra.stations[0].corridors, std::vector<std::string>({"corridor.tiny"}));
	if (!defaultRun().platforms.empty()) {
		const auto& platform = defaultRun().platforms.front();
		// Platform retains canonical station binding.
		QCOMPARE(platform.ID, "platform.1");
		QCOMPARE(platform.StationID, "station.tiny");
		// Platform resolves to its canonical block section.
		QCOMPARE(platform.BlockSectionID, "@block.a@");
	}
	if (defaultRun().platforms.size() == 2) {
		const auto& platform = defaultRun().platforms.back();
		// Destination platform resolves on the same authored route.
		QCOMPARE(platform.ID, "platform.2");
		QCOMPARE(platform.StationID, "station.end");
		QCOMPARE(platform.BlockSectionID, "@block.b@");
	}
	// One native route is available.
	QCOMPARE(defaultRun().infra.numRoutes, 1);
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(1));
	const Route& route = defaultRun().infra.routes.front();
	// Route retains canonical ID and corridor.
	QCOMPARE(route.ID, "route.tiny");
	QCOMPARE(route.corridor, "corridor.tiny");
	// Route retains every canonical block reference.
	QCOMPARE(route.N_Block_Sections, 2);
	QCOMPARE(route.sequence_of_block_sections[0].ID, "@block.a@");
	QCOMPARE(route.sequence_of_block_sections[1].ID, "@block.b@");
	// Explicit block dependency is applied without a hard-coded case dependency.
	QCOMPARE(defaultRun().infra.sections[0].N_ConnectedBS, 1);
	QCOMPARE(defaultRun().infra.sections[0].IDConnectedBS[0], "@block.b@");
	// Single-track references resolve to runtime block IDs.
	QCOMPARE(defaultRun().infra.singleTrackLimits.size(), std::size_t(1));
	QCOMPARE(std::get<0>(defaultRun().infra.singleTrackLimits.front()), "@block.a@");
	QCOMPARE(std::get<2>(defaultRun().infra.singleTrackLimits.front()), "@block.a@");
	// Station boundary references resolve to runtime sections.
	QCOMPARE(defaultRun().infra.stationBoundaries.size(), std::size_t(1));
	QCOMPARE(defaultRun().infra.stationBoundaries.front().entrance->ID, "@block.a@");
	QCOMPARE(defaultRun().infra.stationBoundaries.front().exit->ID, "@block.b@");

	const int blocksBeforeReservedId = defaultRun().infra.numBlocks;
	const std::string firstSectionBeforeReservedId = defaultRun().infra.sections[0].ID;
	SceneModel reservedBlockId = tinyScene();
	reservedBlockId.blocks[0].id = "Depot/1";
	reservedBlockId.routes[0].blocks[0] = "Depot/1";
	reservedBlockId.blockDependencies[0].block = "Depot/1";
	reservedBlockId.singleTrackRestrictions[0].startBlock = "Depot/1";
	reservedBlockId.singleTrackRestrictions[0].protectedStartBlock = "Depot/1";
	reservedBlockId.stationBoundaries[0].entranceBlock = "Depot/1";
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), reservedBlockId);
	// Reserved block-id delimiters are rejected before native mutation.
	QVERIFY(hasDiagnostic(diagnostics, SceneSeverity::Error, "infrastructure.json", "id.reserved"));
	QCOMPARE(defaultRun().infra.numBlocks, blocksBeforeReservedId);
	QCOMPARE(defaultRun().infra.sections[0].ID, firstSectionBeforeReservedId);
}

void TestSceneBuilder::regionalRouteFollowsFirstSegment() {
	SceneModel segmentedRegionRoute = tinyScene();
	segmentedRegionRoute.tracks.push_back({"region.track"});
	segmentedRegionRoute.nodes.push_back({"region.0", "region.track", 100.0, 0.0});
	segmentedRegionRoute.nodes.push_back({"region.1", "region.track", 101.0, 0.0});
	segmentedRegionRoute.arcs.push_back(
		{"region.arc", "region.track", "region.0", "region.1", 0.0, 0.0, 20.0});
	segmentedRegionRoute.blocks.push_back({"region.block.1", "region.track", 0.5});
	segmentedRegionRoute.blocks.push_back({"region.block.2", "region.track", 0.5});
	segmentedRegionRoute.routes[0].blocks = {
		"block.a", "block.b", "region.block.2", "region.block.1"};
	segmentedRegionRoute.importReport.push_back({"legacy_root", ""});
	const auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), segmentedRegionRoute);
	// Native route direction follows the first connected legacy regional segment.
	QVERIFY(!hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "route.direction"));
	QCOMPARE(defaultRun().infra.numRoutes, 1);
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(1));
	QVERIFY(!defaultRun().infra.routes[0].reversed_direction);
}

void TestSceneBuilder::derivesSwitchSectionFromStableIds() {
	const auto clearBlocks = qScopeGuard([] {
		defaultRun().blocksOccupied.clear();
		defaultRun().blocksConnected.clear();
	});
	auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), stableConnectionScene());
	// Stable-ID connection scene builds one switch section.
	QVERIFY(!hasErrors(diagnostics));
	QCOMPARE(defaultRun().infra.numBlocks, 3);
	const Section& source = defaultRun().infra.sections[0];
	// Switch dependency is derived without numeric track naming.
	QCOMPARE(source.ID, "@alpha.block@");
	QCOMPARE(source.N_ConnectedBS, 1);
	// Connection nodes resolve switch sections through stable runtime references.
	QVERIFY(!source.arcs_in_signalling_block_section[0].endNode.IDConnectedBlocks.empty());
	QCOMPARE(source.arcs_in_signalling_block_section[0].endNode.IDConnectedBlocks.front(),
		"@alpha.block@-1.000000/@beta.block@-2.000000");
	bool hasCanonicalSwitchSpeed = false;
	for (int index = 0; index < defaultRun().infra.sections[2].total_arcs; ++index)
		hasCanonicalSwitchSpeed = hasCanonicalSwitchSpeed
			|| defaultRun().infra.sections[2].arcs_in_signalling_block_section[index].speedLimit == 7.5;
	QVERIFY2(hasCanonicalSwitchSpeed, "connection speed is retained independently of endpoint order");
	Section creatorNamedSwitch = defaultRun().infra.sections[2];
	creatorNamedSwitch.ID = "@creator@main-block@-1.000000/@creator-yard@block@-2.000000";
	creatorNamedSwitch.FirstConnectedTrackLineID = 0;
	creatorNamedSwitch.SecondConnectedTrackLineID = 1;
	defaultRun().blocksOccupied.clear();
	defaultRun().blocksConnected.clear();
	activateBlocksWithSwitchesDivFixedBlock(defaultRun(), creatorNamedSwitch, 0, -1.0);
	// Switch occupation preserves creator section IDs containing hyphens and wrapper characters.
	QCOMPARE(creatorNamedSwitch.ID, "@creator@main-block@-1.000000/@creator-yard@block@-2.000000");
	QVERIFY(std::find(defaultRun().blocksOccupied.begin(), defaultRun().blocksOccupied.end(), "@creator@main-block@")
		!= defaultRun().blocksOccupied.end());
	QVERIFY(std::find(defaultRun().blocksOccupied.begin(), defaultRun().blocksOccupied.end(), "@creator-yard@block@")
		!= defaultRun().blocksOccupied.end());
	defaultRun().blocksOccupied = {"occupied.before"};
	defaultRun().blocksConnected = {"connected.before"};
	const auto occupiedBefore = defaultRun().blocksOccupied;
	const auto connectedBefore = defaultRun().blocksConnected;
	Section malformedSwitch;
	malformedSwitch.ID = "malformed/switch";
	Section malformedPrevious;
	malformedPrevious.ID = "also-malformed/switch";
	occupyDoubleSwitch(defaultRun(), malformedSwitch, malformedPrevious);
	// Malformed double-switch identities are rejected before occupancy mutation.
	QCOMPARE(defaultRun().blocksOccupied, occupiedBefore);
	QCOMPARE(defaultRun().blocksConnected, connectedBefore);
	releaseDoubleSwitch(defaultRun(), malformedSwitch, malformedPrevious);
	// Malformed double-switch identities are rejected before release mutation.
	QCOMPARE(defaultRun().blocksOccupied, occupiedBefore);
	QCOMPARE(defaultRun().blocksConnected, connectedBefore);
	occupyDoubleSwitch(defaultRun(), defaultRun().infra.sections[2], malformedPrevious);
	releaseDoubleSwitch(defaultRun(), defaultRun().infra.sections[2], malformedPrevious);
	// A valid first switch half cannot mutate before a malformed second half is rejected.
	QCOMPARE(defaultRun().blocksOccupied, occupiedBefore);
	QCOMPARE(defaultRun().blocksConnected, connectedBefore);
	Section unresolvedSwitch;
	unresolvedSwitch.ID = "@missing.a@-1.000000/@missing.b@-2.000000";
	unresolvedSwitch.withSwitchDiv = true;
	Section unresolvedPrevious;
	unresolvedPrevious.ID = "@missing.c@-3.000000/@missing.d@-4.000000";
	unresolvedPrevious.withSwitchDiv = true;
	occupyDoubleSwitch(defaultRun(), unresolvedSwitch, unresolvedPrevious);
	// Unresolved double-switch branches are rejected before occupancy mutation.
	QCOMPARE(defaultRun().blocksOccupied, occupiedBefore);
	QCOMPARE(defaultRun().blocksConnected, connectedBefore);
	releaseDoubleSwitch(defaultRun(), unresolvedSwitch, unresolvedPrevious);
	// Unresolved double-switch branches are rejected before release mutation.
	QCOMPARE(defaultRun().blocksOccupied, occupiedBefore);
	QCOMPARE(defaultRun().blocksConnected, connectedBefore);
	defaultRun().blocksOccupied.clear();
	defaultRun().blocksConnected.clear();

	const int blocksBeforeDisconnectedRoute = defaultRun().infra.numBlocks;
	const std::string firstSectionBeforeDisconnectedRoute = defaultRun().infra.sections[0].ID;
	SceneModel disconnectedRoute = tinyScene();
	disconnectedRoute.routes.front().blocks = {"block.a", "block.a"};
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), disconnectedRoute);
	// Direct native-builder callers reject disconnected routes before runtime mutation.
	QVERIFY(hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "route.disconnected"));
	QCOMPARE(defaultRun().infra.numBlocks, blocksBeforeDisconnectedRoute);
	QCOMPARE(defaultRun().infra.sections[0].ID, firstSectionBeforeDisconnectedRoute);
}

void TestSceneBuilder::rejectsDerivedRouteReversals() {
	SceneModel epsilonConnection = stableConnectionScene();
	epsilonConnection.nodes[2].xKm = 1.0 + 5e-9;
	epsilonConnection.nodes[3].xKm = 2.0 + 5e-9;
	const SceneSectionInventory epsilonInventory = buildSceneSectionInventory(epsilonConnection);
	auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), epsilonConnection);
	// Sub-tolerance connection spacing retains inventory and native section-ID parity.
	QVERIFY(!hasErrors(diagnostics));
	QCOMPARE(defaultRun().infra.numBlocks, 3);
	QCOMPARE(epsilonInventory.sections.size(), std::size_t(3));
	QCOMPARE(defaultRun().infra.sections[2].ID, epsilonInventory.sections[2].id);
	SceneModel derivedUTurn = switchChainScene();
	derivedUTurn.blocks = {{"a.block", "switch-a", 1.0}, {"b.left", "switch-b", 1.0},
		{"b.right", "switch-b", 1.0}, {"c.block", "switch-c", 1.0}};
	std::string aToB;
	std::string bToC;
	for (const auto& section : buildSceneSectionInventory(derivedUTurn).sections) {
		if (section.sourceConnectionId == "a-to-b")
			aToB = section.id;
		else if (section.sourceConnectionId == "b-to-c")
			bToC = section.id;
	}
	derivedUTurn.importReport.push_back({"legacy_root", ""});
	derivedUTurn.routes.push_back({"switch-u-turn", {aToB, bToC, aToB}, false, {}, false});
	const int blocksBeforeDerivedUTurn = defaultRun().infra.numBlocks;
	const std::string firstSectionBeforeDerivedUTurn = defaultRun().infra.sections[0].ID;
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), derivedUTurn);
	// Legacy provenance cannot hide a connection-derived U-turn from native preflight.
	QVERIFY(!aToB.empty());
	QVERIFY(!bToC.empty());
	QVERIFY(hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "route.direction"));
	QCOMPARE(defaultRun().infra.numBlocks, blocksBeforeDerivedUTurn);
	QCOMPARE(defaultRun().infra.sections[0].ID, firstSectionBeforeDerivedUTurn);
	SceneModel mixedDerivedUTurn = derivedUTurn;
	mixedDerivedUTurn.routes = {{"mixed-switch-u-turn", {"b.left", bToC, aToB}, false, {}, false}};
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), mixedDerivedUTurn);
	// Mixed route evidence cannot hide a legacy derived U-turn before native mutation.
	QVERIFY(hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "route.direction"));
	QCOMPARE(defaultRun().infra.numBlocks, blocksBeforeDerivedUTurn);
	QCOMPARE(defaultRun().infra.sections[0].ID, firstSectionBeforeDerivedUTurn);
	SceneModel legacyFork = switchChainScene();
	legacyFork.connections.push_back({"a-to-c", "a.1", "c.0", false, 0.0});
	legacyFork.nodes[0].xKm = 100.0;
	legacyFork.nodes[1].xKm = 101.0;
	legacyFork.nodes[2].xKm = 0.0;
	legacyFork.nodes[3].xKm = 1.0;
	legacyFork.nodes[4].xKm = 2.0;
	legacyFork.nodes[5].xKm = 3.0;
	std::string bToA;
	std::string cToA;
	for (const auto& section : buildSceneSectionInventory(legacyFork).sections) {
		if (section.sourceConnectionId == "a-to-b")
			bToA = section.id;
		else if (section.sourceConnectionId == "a-to-c")
			cToA = section.id;
	}
	legacyFork.importReport.push_back({"legacy_root", ""});
	legacyFork.routes.push_back({"legacy-fork", {bToA, cToA}, false, {}, false});
	const int blocksBeforeLegacyFork = defaultRun().infra.numBlocks;
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), legacyFork);
	// Legacy regional compatibility rejects a wrong-branch switch fork before runtime mutation.
	QVERIFY(!bToA.empty());
	QVERIFY(!cToA.empty());
	QVERIFY(hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "route.disconnected"));
	QCOMPARE(defaultRun().infra.numBlocks, blocksBeforeLegacyFork);
}

void TestSceneBuilder::appliesSignallingAreasBeforeRoutes() {
	auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), signallingAreasScene());
	// Signalling areas apply before route construction.
	QVERIFY(!hasErrors(diagnostics));
	QCOMPARE(defaultRun().infra.numBlocks, 3);
	const Section& alpha = defaultRun().infra.sections[0];
	const Section& beta = defaultRun().infra.sections[1];
	const Section& derived = defaultRun().infra.sections[2];
	// Network and track-scoped levels reach base and derived switch sections.
	QCOMPARE(alpha.SignallingLevel, 3);
	QCOMPARE(beta.SignallingLevel, 1);
	QCOMPARE(derived.SignallingLevel, 3);
	// Signalling level is copied into the derived route section.
	QVERIFY(!defaultRun().infra.routes.empty());
	QCOMPARE(defaultRun().infra.routes.front().N_Block_Sections, 1);
	QCOMPARE(defaultRun().infra.routes.front().sequence_of_block_sections[0].SignallingLevel, 3);
	const int blocksBeforeConflict = defaultRun().infra.numBlocks;
	std::vector<std::string> sectionIdsBeforeConflict;
	for (int index = 0; index < defaultRun().infra.numBlocks; ++index)
		sectionIdsBeforeConflict.push_back(defaultRun().infra.sections[index].ID);
	SceneModel conflictingAreas = signallingAreasScene();
	conflictingAreas.signallingAreas.push_back({"beta-area", 0.0, 3.0, 4, "beta"});
	conflictingAreas.tracks.push_back({"gamma"});
	conflictingAreas.nodes.push_back({"gamma.start", "gamma", 4.0, 0.0});
	conflictingAreas.nodes.push_back({"gamma.end", "gamma", 5.0, 0.0});
	conflictingAreas.arcs.push_back({"gamma.arc", "gamma", "gamma.start", "gamma.end", 0.0, 0.0, 10.0});
	conflictingAreas.blocks.push_back({"gamma.block", "gamma", 1.0});
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), conflictingAreas);
	bool sectionIdsUnchanged = defaultRun().infra.numBlocks == blocksBeforeConflict;
	for (std::size_t index = 0; sectionIdsUnchanged && index < sectionIdsBeforeConflict.size(); ++index)
		sectionIdsUnchanged = defaultRun().infra.sections[index].ID == sectionIdsBeforeConflict[index];
	// Same-tier conflicting track areas are rejected before replacing the prior runtime.
	QVERIFY(hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "signalling_area.conflict"));
	QVERIFY(sectionIdsUnchanged);
	SceneModel invalidSwitchReference = stableConnectionScene();
	invalidSwitchReference.blockDependencies.push_back(
		{"alpha.block", "@alpha.block@-9.000000/@beta.block@-10.000000"});
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), invalidSwitchReference);
	// Invalid switch references are rejected before replacing the existing runtime.
	QVERIFY(hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "ref.unresolved"));
	QCOMPARE(defaultRun().infra.numBlocks, 3);
	QCOMPARE(defaultRun().infra.sections[0].ID, "@alpha.block@");
	SceneModel duplicateSwitchSection = stableConnectionScene();
	duplicateSwitchSection.connections.push_back(
		{"switch.duplicate", "alpha.end", "beta.start", true, 7.5});
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), duplicateSwitchSection);
	// Duplicate switch sections are rejected before fixed-capacity runtime mutation.
	QVERIFY(hasDiagnostic(diagnostics, SceneSeverity::Error, "infrastructure.json", "id.duplicate"));
	QCOMPARE(defaultRun().infra.numBlocks, 3);
	QCOMPARE(defaultRun().infra.sections[0].ID, "@alpha.block@");
	SceneModel oversizedSwitchSection;
	oversizedSwitchSection.tracks = {{"long.alpha"}, {"long.beta"}};
	for (int index = 0; index <= 10; ++index) {
		oversizedSwitchSection.nodes.push_back({"alpha." + std::to_string(index), "long.alpha",
			static_cast<double>(index), 0.0});
		oversizedSwitchSection.nodes.push_back({"beta." + std::to_string(index), "long.beta",
			static_cast<double>(index + 11), 0.0});
		if (index == 0)
			continue;
		oversizedSwitchSection.arcs.push_back({"alpha.arc." + std::to_string(index), "long.alpha",
			"alpha." + std::to_string(index - 1), "alpha." + std::to_string(index),
			0.0, 0.0, 10.0});
		oversizedSwitchSection.arcs.push_back({"beta.arc." + std::to_string(index), "long.beta",
			"beta." + std::to_string(index - 1), "beta." + std::to_string(index),
			0.0, 0.0, 10.0});
	}
	oversizedSwitchSection.blocks = {{"long.alpha.block", "long.alpha", 10.0},
		{"long.beta.block", "long.beta", 10.0}};
	oversizedSwitchSection.connections.push_back(
		{"long.switch", "alpha.10", "beta.0", false, 0.0});
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), oversizedSwitchSection);
	// Derived switch arc capacity is rejected before runtime mutation.
	QVERIFY(hasDiagnostic(diagnostics, SceneSeverity::Error, "infrastructure.json", "capacity"));
	QCOMPARE(defaultRun().infra.numBlocks, 3);
	QCOMPARE(defaultRun().infra.sections[0].ID, "@alpha.block@");
	SceneModel runtimeBlockIdCollision = tinyScene();
	runtimeBlockIdCollision.blocks[1].id = "@block.a@";
	runtimeBlockIdCollision.routes.front().blocks[1] = "@block.a@";
	runtimeBlockIdCollision.blockDependencies.front().dependsOn = "@block.a@";
	runtimeBlockIdCollision.singleTrackRestrictions.front().endBlock = "@block.a@";
	runtimeBlockIdCollision.singleTrackRestrictions.front().protectedEndBlock = "@block.a@";
	runtimeBlockIdCollision.stationBoundaries.front().exitBlock = "@block.a@";
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), runtimeBlockIdCollision);
	// Canonical block IDs that collapse to one runtime ID are rejected before mutation.
	QVERIFY(hasDiagnostic(diagnostics, SceneSeverity::Error, "infrastructure.json", "id.duplicate"));
	QCOMPARE(defaultRun().infra.numBlocks, 3);
	QCOMPARE(defaultRun().infra.sections[0].ID, "@alpha.block@");
	SceneModel excessiveDependencies = tinyScene();
	for (int index = 0; index < 10; ++index) {
		const std::string suffix = std::to_string(index);
		const std::string track = "dependency.track." + suffix;
		const std::string first = "dependency.node." + suffix + ".0";
		const std::string second = "dependency.node." + suffix + ".1";
		const std::string block = "dependency.block." + suffix;
		excessiveDependencies.tracks.push_back({track});
		excessiveDependencies.nodes.push_back({first, track, 0.0, 0.0});
		excessiveDependencies.nodes.push_back({second, track, 1.0, 0.0});
		excessiveDependencies.arcs.push_back({"dependency.arc." + suffix, track, first, second, 0.0, 0.0, 10.0});
		excessiveDependencies.blocks.push_back({block, track, 1.0});
		excessiveDependencies.blockDependencies.push_back({"block.a", block});
	}
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), excessiveDependencies);
	// Dependency overflow is rejected before replacing the existing runtime.
	QVERIFY(hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "capacity"));
	QCOMPARE(defaultRun().infra.numBlocks, 3);
	QCOMPARE(defaultRun().infra.sections[0].ID, "@alpha.block@");
}

void TestSceneBuilder::explicitReverseKeepsReferenceCoordinate() {
	SceneModel explicitReversed = tinyScene();
	explicitReversed.routes.front().reversed = true;
	const auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), explicitReversed);
	// Explicit reverse metadata retains the joined-route reference coordinate.
	QVERIFY(!hasErrors(diagnostics));
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(1));
	QVERIFY(defaultRun().infra.routes.front().reversed_direction);
	QCOMPARE_EQ(defaultRun().infra.routes.front().OriginalRefReversedRoute, defaultRun().infra.routes.front().x_of_end_node * 1000.0);
}

void TestSceneBuilder::descendingBlockOrderReversesRoute() {
	SceneModel descendingRoute = tinyScene();
	descendingRoute.routes.front().blocks = {"block.b", "block.a"};
	auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), descendingRoute);
	// Descending canonical block order retains the runtime reverse direction.
	QVERIFY(!hasErrors(diagnostics));
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(1));
	QVERIFY(defaultRun().infra.routes.front().reversed_direction);
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), tinyScene());
	QVERIFY2(!hasErrors(diagnostics), "the native runtime can be rebuilt safely");
}

void TestSceneBuilder::declaredTopologyControlsDirection() {
	SceneModel regionalDirection = stableConnectionScene();
	regionalDirection.nodes[2].xKm = 0.0;
	regionalDirection.nodes[3].xKm = 1.0;
	regionalDirection.routes.push_back(
		{"route.regional", {"alpha.block", "beta.block"}, false, {}, false});
	auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), regionalDirection);
	// Declared route topology controls direction across regional coordinate references.
	QVERIFY(!hasErrors(diagnostics));
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(1));
	QVERIFY(!defaultRun().infra.routes.front().reversed_direction);
	QCOMPARE(defaultRun().infra.routes.front().sequence_of_block_sections[0].ID, "@alpha.block@");
	QCOMPARE(defaultRun().infra.routes.front().sequence_of_block_sections[1].ID, "@beta.block@");

	const int blocksBeforeInvalid = defaultRun().infra.numBlocks;
	SceneModel invalidReference = tinyScene();
	invalidReference.routes.front().blocks = {"missing.block"};
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), invalidReference);
	QVERIFY2(hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "ref.unresolved"),
		"unknown route block returns an actionable signalling diagnostic");
	// Invalid route does not mutate or silently truncate the existing runtime.
	QCOMPARE(defaultRun().infra.numBlocks, blocksBeforeInvalid);
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(1));

	const int routesBeforeMalformed = defaultRun().infra.numRoutes;
	const std::string routeIdBeforeMalformed = defaultRun().infra.routes.front().ID;
	const int routeBlocksBeforeMalformed = defaultRun().infra.routes.front().N_Block_Sections;
	SceneModel malformedReference = tinyScene();
	malformedReference.routes.front().blocks = {"@block.a@-0.500000"};
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), malformedReference);
	// Malformed decorated route references are rejected before replacing the existing runtime.
	QVERIFY(hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "ref.unresolved"));
	QCOMPARE(defaultRun().infra.numBlocks, blocksBeforeInvalid);
	QCOMPARE(defaultRun().infra.numRoutes, routesBeforeMalformed);
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(1));
	QCOMPARE(defaultRun().infra.routes.front().ID, routeIdBeforeMalformed);
	QCOMPARE(defaultRun().infra.routes.front().N_Block_Sections, routeBlocksBeforeMalformed);
}

void TestSceneBuilder::multiRegionRouteKeepsAuthoredOrder() {
	auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), multiRegionRouteScene());
	// Multi-region route builds with a multi-arc middle block.
	QVERIFY(!hasErrors(diagnostics));
	QCOMPARE(defaultRun().infra.numRoutes, 1);
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(1));
	if (defaultRun().infra.routes.front().N_Block_Sections == 3) {
		// Native route construction retains authored order across coordinate regions.
		QCOMPARE(defaultRun().infra.routes.front().sequence_of_block_sections[0].ID, "@a.block@");
		QCOMPARE(defaultRun().infra.routes.front().sequence_of_block_sections[1].ID, "@b.block@");
		QCOMPARE(defaultRun().infra.routes.front().sequence_of_block_sections[2].ID, "@c.block@");
		const Section& later = defaultRun().infra.routes.front().sequence_of_block_sections[1];
		const bool kilometerEndpoints = later.total_arcs == 2
			&& std::fabs(later.arcs_in_signalling_block_section[0].startNode.X - 1.0) < 1e-9
			&& std::fabs(later.arcs_in_signalling_block_section[0].endNode.X - 2.0) < 1e-9
			&& std::fabs(later.arcs_in_signalling_block_section[1].startNode.X - 2.0) < 1e-9
			&& std::fabs(later.arcs_in_signalling_block_section[1].endNode.X - 3.0) < 1e-9;
		QVERIFY2(kilometerEndpoints, "route normalization keeps native multi-arc lengths in kilometres");
	}

	const int blocksBeforeInvalidTopology = defaultRun().infra.numBlocks;
	SceneModel invalidTopology = tinyScene();
	invalidTopology.arcs.front().toNodeId = "missing.node";
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), invalidTopology);
	QVERIFY2(hasDiagnostic(diagnostics, SceneSeverity::Error, "infrastructure.json", "ref.unresolved"),
		"unknown arc endpoint returns an actionable infrastructure diagnostic");
	// Invalid topology is rejected before runtime mutation.
	QCOMPARE(defaultRun().infra.numBlocks, blocksBeforeInvalidTopology);
}

void TestSceneBuilder::partialAreaLeavesOtherSectionsUnset() {
	SceneModel partialArea = tinyScene();
	partialArea.signallingAreas = {{"partial-area", 0.0, 1.5, 2, {}}};
	const auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), partialArea);
	// Partial signalling area scene builds.
	QVERIFY(!hasErrors(diagnostics));
	QCOMPARE(defaultRun().infra.numBlocks, 2);
	constexpr int unsetLevel = -99999999;
	// Only sections inside an area get a level, the others keep the unset default.
	QCOMPARE(defaultRun().infra.sections[0].SignallingLevel, 2);
	QCOMPARE(defaultRun().infra.sections[1].SignallingLevel, unsetLevel);
}

constexpr int kUnsetLevel = -99999999;
constexpr int kMissingSection = -2;

// One 1 km block per kilometre on a track, named "<prefix>b.0", "<prefix>b.1", and so on,
// with a route over all of them.
static void addLine(SceneModel& scene, const std::string& track, const std::string& prefix, int blockCount) {
	scene.tracks.push_back({track});
	for (int index = 0; index <= blockCount; ++index)
		scene.nodes.push_back({prefix + "n." + std::to_string(index), track, static_cast<double>(index), 0.0});
	SceneRoute route;
	route.id = "route." + prefix + track;
	for (int index = 0; index < blockCount; ++index) {
		const std::string number = std::to_string(index);
		scene.arcs.push_back({prefix + "a." + number, track, prefix + "n." + number,
			prefix + "n." + std::to_string(index + 1), 0.0, 0.0, 20.0});
		scene.blocks.push_back({prefix + "b." + number, track, 1.0});
		route.blocks.push_back(prefix + "b." + number);
	}
	scene.routes.push_back(route);
}

static SceneModel lineScene(int blockCount, bool reversedRoute = false) {
	SceneModel scene;
	scene.name = "area-line";
	addLine(scene, "line", "", blockCount);
	if (reversedRoute)
		std::reverse(scene.routes.back().blocks.begin(), scene.routes.back().blocks.end());
	return scene;
}

static SceneModel twoLineScene(int blockCount) {
	SceneModel scene;
	scene.name = "area-two-lines";
	addLine(scene, "line.a", "a.", blockCount);
	addLine(scene, "line.b", "b.", blockCount);
	return scene;
}

static std::map<std::string, int> builtSectionLevels() {
	std::map<std::string, int> levels;
	for (int index = 0; index < defaultRun().infra.numBlocks; ++index)
		levels[defaultRun().infra.sections[index].ID] = defaultRun().infra.sections[index].SignallingLevel;
	return levels;
}

static std::map<std::string, int> builtRouteLevels(std::size_t routeIndex) {
	std::map<std::string, int> levels;
	if (routeIndex >= defaultRun().infra.routes.size())
		return levels;
	const Route& route = defaultRun().infra.routes[routeIndex];
	for (int index = 0; index < route.N_Block_Sections; ++index)
		levels[route.sequence_of_block_sections[index].ID] = route.sequence_of_block_sections[index].SignallingLevel;
	return levels;
}

// The levels of the sections "<prefix>b.0", "<prefix>b.1", ... as built, in that order.
static std::vector<int> levelsOfLine(const std::string& prefix, std::size_t count) {
	const std::map<std::string, int> levels = builtSectionLevels();
	std::vector<int> result;
	for (std::size_t index = 0; index < count; ++index) {
		const auto found = levels.find("@" + prefix + "b." + std::to_string(index) + "@");
		result.push_back(found == levels.end() ? kMissingSection : found->second);
	}
	return result;
}

// Builds the scene and returns the levels of the single track "b.<i>" sections.
static std::vector<int> buildLineLevels(const SceneModel& scene, std::size_t count, bool& built) {
	const auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), scene);
	built = !hasErrors(diagnostics) && static_cast<std::size_t>(defaultRun().infra.numBlocks) == count;
	return levelsOfLine("", count);
}

// The section names in the validator's missing-level warning, and whether it names all of them.
static std::set<std::string> namedUncoveredSections(const SceneModel& scene, bool& complete) {
	std::set<std::string> names;
	complete = true;
	for (const SceneDiagnostic& diagnostic : validateRunnableScene(scene)) {
		if (diagnostic.code != "scene.signalling.level.missing")
			continue;
		const std::string marker = "without signalling: ";
		const std::size_t begin = diagnostic.message.find(marker);
		if (begin == std::string::npos) {
			complete = false;
			continue;
		}
		std::string list = diagnostic.message.substr(begin + marker.size());
		complete = list.find(" more") == std::string::npos;
		const std::size_t end = list.rfind(" (track");
		if (end != std::string::npos)
			list.resize(end);
		// Each name is an id with its label in parentheses, and the label of a switch section has a comma of its own.
		std::string item;
		int depth = 0;
		for (std::size_t position = 0; position <= list.size(); ++position) {
			const char character = position < list.size() ? list[position] : ',';
			if (character == '(')
				++depth;
			else if (character == ')')
				--depth;
			if (character == ',' && depth == 0) {
				names.insert(item.substr(0, item.find(" (")));
				item.clear();
				++position;
				continue;
			}
			item += character;
		}
	}
	return names;
}

// The route sections that the built runtime left without a level.
static std::set<std::string> builtUncoveredRouteSections() {
	std::set<std::string> ids;
	for (std::size_t route = 0; route < defaultRun().infra.routes.size(); ++route) {
		for (const auto& [id, level] : builtRouteLevels(route))
			if (level == kUnsetLevel)
				ids.insert(id);
	}
	return ids;
}

void TestSceneBuilder::areasSurviveExportAndImport_data() {
	QTest::addColumn<bool>("reverse");
	QTest::addColumn<bool>("empty");
	QTest::newRow("forward") << false << false;
	QTest::newRow("reverse") << true << false;
	QTest::newRow("forward without areas") << false << true;
	QTest::newRow("reverse without areas") << true << true;
}

// Compare canonical and reimported native assignments, including route copies in both travel directions.
void TestSceneBuilder::areasSurviveExportAndImport() {
	QFETCH(bool, reverse);
	QFETCH(bool, empty);
	namespace fs = std::filesystem;
	if (m_roundtripRoot.empty()) {
		m_roundtripRoot = fs::temp_directory_path() / ("area_roundtrip_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		fs::create_directories(m_roundtripRoot);
	}
	const fs::path& root = m_roundtripRoot;
	SceneModel source = twoLineScene(4);
	source.schemaVersion = kCurrentSceneSchemaVersion;
	for (auto& route : source.routes)
		if (reverse) std::reverse(route.blocks.begin(), route.blocks.end());
	source.signallingAreas = {{" network area ", 0.0, 4.0, 1, {}},
		{"head", 0.0, 2.0 - 5e-9, 3, "line.a"}, {"tail", 2.5, 4.0, 4, "line.a"},
		{"other", 1.0 + 1e-7, 3.0, 5, "line.b"}};
	if (empty) source.signallingAreas.clear();
	source.routes[0].id = "route0";
	source.routes[1].id = "route1";
	SceneStation origin;
	origin.id = "Origin";
	origin.name = "Origin";
	origin.hasPosition = true;
	origin.positionKm = 0;
	source.stations.push_back(origin);
	const auto built = buildInfrastructureAndSignallingFromScene(defaultRun(), source);
	// Roundtrip source builds.
	QVERIFY(!hasErrors(built));
	QCOMPARE(defaultRun().infra.numBlocks, 8);
	const std::vector<int> base = [&] {
		auto first = levelsOfLine("a.", 4), second = levelsOfLine("b.", 4);
		first.insert(first.end(), second.begin(), second.end());
		return first;
	}();
	// Roundtrip fixture covers contained, straddling and tolerance boundary sections.
	QCOMPARE(base, empty ? std::vector<int>(8, kUnsetLevel) : std::vector<int>({3, 3, 1, 4, 1, 1, 5, 1}));
	const auto routeLevels = [] {
		std::vector<std::vector<int>> levels;
		for (const auto& route : defaultRun().infra.routes) {
			std::vector<int> row;
			for (int i = 0; i < route.N_Block_Sections; ++i) row.push_back(route.sequence_of_block_sections[i].SignallingLevel);
			levels.push_back(row);
		}
		return levels;
	};
	const auto beforeRoutes = routeLevels();
	// Source route directions match fixture.
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(2));
	QCOMPARE(defaultRun().infra.routes[0].reversed_direction, reverse);
	QCOMPARE(defaultRun().infra.routes[1].reversed_direction, reverse);
	const fs::path scene = root / (reverse ? "reverse" : "forward");
	QVERIFY2(saveScene(source, scene.string()).success(), "roundtrip canonical source saves");
	QVERIFY2(exportLegacyScene(scene.string(), (root / "exported").string()).success(), "roundtrip source exports");
	QVERIFY2(importLegacyScene((root / "exported").string(), (root / "imported").string(), "Areas").success(), "roundtrip output imports");
	const auto loaded = loadScene((root / "imported").string());
	QVERIFY2(!hasErrors(loaded.diagnostics), "roundtrip output loads");
	const auto after = buildInfrastructureAndSignallingFromScene(defaultRun(), loaded.scene);
	std::vector<int> afterBase;
	const auto mapped = builtSectionLevels();
	for (const auto& track : {"B0", "B1"})
		for (int i = 0; i < 4; ++i) {
			const auto found = mapped.find("@" + std::to_string(i) + "-" + track + "@");
			afterBase.push_back(found == mapped.end() ? kMissingSection : found->second);
		}
	// Export/import retains base and route assignments in both directions without snapping boundaries.
	QVERIFY(!hasErrors(after));
	QCOMPARE(afterBase, base);
	QCOMPARE(routeLevels(), beforeRoutes);
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(2));
	QCOMPARE(defaultRun().infra.routes[0].reversed_direction, reverse);
	QCOMPARE(defaultRun().infra.routes[1].reversed_direction, reverse);
}

// Network-wide areas and containment.
void TestSceneBuilder::routeSectionsCarrySectionLevels() {
	SceneModel scene = lineScene(4);
	scene.signallingAreas = {{"all", 0.0, 4.0, 2, {}}};
	QVERIFY2(!hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), scene)), "the network-wide area scene builds");
	// The route sections carry the levels of the same sections.
	QCOMPARE(builtRouteLevels(0), builtSectionLevels());
	QCOMPARE(builtRouteLevels(0).size(), std::size_t(4));
}

// The areas on a line of four 1 km sections and the levels the sections get, in their order on the line.
struct LineLevels {
	const char* what;
	std::vector<SceneSignallingArea> areas;
	std::vector<int> levels;
};

static const LineLevels kLineLevels[] = {
	// Network-wide areas and containment.
	{"a network-wide area gives every section its level", {{"all", 0.0, 4.0, 2, {}}}, {2, 2, 2, 2}},
	{"a section that ends at the area end is inside", {{"head", 0.0, 2.0, 3, {}}}, {3, 3, kUnsetLevel, kUnsetLevel}},
	{"a section that crosses the area end is not inside", {{"head", 0.0, 2.5, 3, {}}}, {3, 3, kUnsetLevel, kUnsetLevel}},
	{"a section that crosses the area start is not inside", {{"tail", 0.5, 4.0, 3, {}}}, {kUnsetLevel, 3, 3, 3}},
	{"an area from one section edge to another covers the sections between", {{"middle", 1.0, 3.0, 5, {}}},
		{kUnsetLevel, 5, 5, kUnsetLevel}},
	{"adjacent areas that meet on a section edge split the sections",
		{{"adjacent-a", 0.0, 2.0, 1, {}}, {"adjacent-b", 2.0, 4.0, 4, {}}}, {1, 1, 4, 4}},
	{"a section that crosses the edge between two areas stays unset",
		{{"adjacent-a", 0.0, 2.5, 1, {}}, {"adjacent-b", 2.5, 4.0, 4, {}}}, {1, 1, kUnsetLevel, 4}},
	{"a third area that contains the straddling section decides it",
		{{"adjacent-a", 0.0, 2.5, 1, {}}, {"adjacent-b", 2.5, 4.0, 4, {}}, {"bridge", 1.5, 3.5, 4, {}}}, {1, 1, 4, 4}},
	// Edge tolerance of 1e-8 km on both edges.
	{"an area end just inside the tolerance still contains the section", {{"end", 0.0, 2.0 - 5e-9, 3, {}}},
		{3, 3, kUnsetLevel, kUnsetLevel}},
	{"an area end outside the tolerance does not contain the section", {{"end", 0.0, 2.0 - 1e-7, 3, {}}},
		{3, kUnsetLevel, kUnsetLevel, kUnsetLevel}},
	{"an area start just inside the tolerance still contains the section", {{"start", 1.0 + 5e-9, 4.0, 3, {}}},
		{kUnsetLevel, 3, 3, 3}},
	{"an area start outside the tolerance does not contain the section", {{"start", 1.0 + 1e-7, 4.0, 3, {}}},
		{kUnsetLevel, kUnsetLevel, 3, 3}},
	// An area that covers no section changes nothing.
	{"an area inside one section covers no section", {{"inside-one", 1.2, 1.8, 5, {}}},
		{kUnsetLevel, kUnsetLevel, kUnsetLevel, kUnsetLevel}},
	{"an area beyond the track covers no section", {{"beyond", 10.0, 12.0, 5, {}}},
		{kUnsetLevel, kUnsetLevel, kUnsetLevel, kUnsetLevel}},
	{"areas that cover no section leave the other areas as they are",
		{{"all", 0.0, 4.0, 2, {}}, {"inside-one", 1.2, 1.8, 5, {}}, {"beyond", 10.0, 12.0, 5, {}}}, {2, 2, 2, 2}},
};

void TestSceneBuilder::areaLevelsOfLineSections_data() {
	QTest::addColumn<int>("index");
	for (int index = 0; index < static_cast<int>(std::size(kLineLevels)); ++index)
		QTest::newRow(kLineLevels[index].what) << index;
}

void TestSceneBuilder::areaLevelsOfLineSections() {
	QFETCH(int, index);
	const LineLevels& test = kLineLevels[index];
	SceneModel scene = lineScene(4);
	scene.signallingAreas = test.areas;
	bool built = false;
	const std::vector<int> levels = buildLineLevels(scene, test.levels.size(), built);
	QVERIFY(built);
	QCOMPARE(levels, test.levels);
}

// Track scope overrides the network-wide level on its own track.
void TestSceneBuilder::trackScopedAreaOverridesOwnTrack() {
	using Levels = std::vector<int>;
	const int u = kUnsetLevel;
	SceneModel tracks = twoLineScene(3);
	tracks.signallingAreas = {{"all", 0.0, 3.0, 1, {}}, {"track-a", 0.0, 3.0, 4, "line.a"}};
	// A track-scoped area overrides the network-wide area on its track only.
	QVERIFY(!hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), tracks)));
	QCOMPARE(defaultRun().infra.numBlocks, 6);
	QCOMPARE(levelsOfLine("a.", 3), Levels({4, 4, 4}));
	QCOMPARE(levelsOfLine("b.", 3), Levels({1, 1, 1}));
	tracks.signallingAreas = {{"all", 0.0, 3.0, 1, {}}, {"track-b-head", 0.0, 1.0, 5, "line.b"}};
	// A track-scoped area overrides only the sections it contains.
	QVERIFY(!hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), tracks)));
	QCOMPARE(defaultRun().infra.numBlocks, 6);
	QCOMPARE(levelsOfLine("a.", 3), Levels({1, 1, 1}));
	QCOMPARE(levelsOfLine("b.", 3), Levels({5, 1, 1}));
	tracks.signallingAreas = {{"track-a", 0.0, 3.0, 4, "line.a"}};
	// A track-scoped area leaves the other tracks without a level.
	QVERIFY(!hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), tracks)));
	QCOMPARE(defaultRun().infra.numBlocks, 6);
	QCOMPARE(levelsOfLine("a.", 3), Levels({4, 4, 4}));
	QCOMPARE(levelsOfLine("b.", 3), Levels({u, u, u}));
}

// A route used in reverse has the same levels on its sections.
void TestSceneBuilder::reversedRouteKeepsSectionLevels() {
	const int u = kUnsetLevel;
	SceneModel forward = lineScene(4);
	forward.signallingAreas = {{"low", 0.0, 2.0, 1, {}}, {"high", 2.0, 3.0, 4, {}}};
	SceneModel reverse = lineScene(4, true);
	reverse.signallingAreas = forward.signallingAreas;
	const bool forwardBuilt = !hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), forward)) && defaultRun().infra.routes.size() == 1
		&& !defaultRun().infra.routes.front().reversed_direction;
	const std::map<std::string, int> forwardLevels = builtRouteLevels(0);
	const bool reverseBuilt = !hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), reverse)) && defaultRun().infra.routes.size() == 1
		&& defaultRun().infra.routes.front().reversed_direction;
	const std::map<std::string, int> reverseLevels = builtRouteLevels(0);
	// The forward route carries the levels of its sections.
	QVERIFY(forwardBuilt);
	QVERIFY(reverseBuilt);
	QCOMPARE(forwardLevels.size(), std::size_t(4));
	QCOMPARE(forwardLevels.at("@b.0@"), 1);
	QCOMPARE(forwardLevels.at("@b.2@"), 4);
	QCOMPARE(forwardLevels.at("@b.3@"), u);
	// A route used in reverse has the same levels on its sections.
	QCOMPARE(reverseLevels, forwardLevels);
	// The reversed route sections equal the built sections by ID.
	QCOMPARE(reverseLevels, builtSectionLevels());
}

// A scene of the warning checks: one line of five sections, or two lines of three, with these areas, and the number of
// route sections that neither the warning nor the build give a level.
struct LevelWarningCase {
	const char* what;
	bool twoLines;
	bool reversedRoute;
	std::vector<SceneSignallingArea> areas;
	std::size_t uncovered;
};

static const LevelWarningCase kLevelWarningCases[] = {
	{"the warning names the route sections left without a level", false, false, {{"head", 0.0, 3.0, 2, {}}}, 2},
	{"the warning and the build agree on a section that crosses the area end", false, false, {{"head", 0.0, 2.5, 2, {}}}, 3},
	{"the warning and the build agree inside the edge tolerance", false, false, {{"head", 0.0, 3.0 - 5e-9, 2, {}}}, 2},
	{"the warning and the build agree outside the edge tolerance", false, false, {{"head", 0.0, 3.0 - 1e-7, 2, {}}}, 3},
	{"the warning and the build agree at the area start", false, false, {{"tail", 1.0 + 1e-7, 5.0, 2, {}}}, 2},
	{"the warning and the build agree when there is no area", false, false, {}, 5},
	{"the warning and the build agree when every section is covered", false, false, {{"all", 0.0, 5.0, 2, {}}}, 0},
	{"the warning and the build agree on a reversed route", false, true, {{"head", 0.0, 3.0, 2, {}}}, 2},
	{"the warning and the build agree on a track-scoped area", true, false, {{"track-a", 0.0, 3.0, 4, "line.a"}}, 3},
	{"the warning and the build agree when a track-scoped area adds to a network-wide one", true, false,
		{{"all", 0.0, 3.0, 1, {}}, {"track-b-head", 0.0, 2.0, 5, "line.b"}}, 0},
	{"the warning and the build agree when the track-scoped area misses a start edge", true, false,
		{{"cover", 0.0, 2.0, 1, {}}, {"track-b-tail", 1.0 + 1e-7, 3.0, 5, "line.b"}}, 1},
};

void TestSceneBuilder::levelWarningAgreesWithBuild_data() {
	QTest::addColumn<int>("index");
	for (int index = 0; index < static_cast<int>(std::size(kLevelWarningCases)); ++index)
		QTest::newRow(kLevelWarningCases[index].what) << index;
}

// The validator's missing-level warning names the route sections that the build leaves without a level.
void TestSceneBuilder::levelWarningAgreesWithBuild() {
	QFETCH(int, index);
	const LevelWarningCase& test = kLevelWarningCases[index];
	SceneModel scene = test.twoLines ? twoLineScene(3) : lineScene(5, test.reversedRoute);
	scene.signallingAreas = test.areas;
	bool complete = false;
	const std::set<std::string> named = namedUncoveredSections(scene, complete);
	const auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), scene);
	const std::set<std::string> unset = builtUncoveredRouteSections();
	QVERIFY(!hasErrors(diagnostics));
	QVERIFY(complete);
	QVERIFY2(named == unset, "the warning names the sections that the build leaves without a level");
	QCOMPARE(unset.size(), test.uncovered);
}

void TestSceneBuilder::buildsLongTrack() {
	constexpr int arcCount = 1600;
	constexpr int arcsPerBlock = 16;
	SceneModel scene;
	scene.name = "long-track";
	scene.tracks = {{"track.long"}};
	for (int index = 0; index <= arcCount; ++index)
		scene.nodes.push_back({"node." + std::to_string(index), "track.long", static_cast<double>(index), 0.0});
	for (int index = 0; index < arcCount; ++index)
		scene.arcs.push_back({"arc." + std::to_string(index), "track.long", "node." + std::to_string(index),
			"node." + std::to_string(index + 1), 0.0, 0.0, 20.0});
	for (int index = 0; index < arcCount / arcsPerBlock; ++index)
		scene.blocks.push_back({"block." + std::to_string(index), "track.long",
			static_cast<double>(arcsPerBlock)});
	const auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), scene);
	QVERIFY2(!hasErrors(diagnostics), "a track with more than 1500 nodes and arcs builds");
	// The long track keeps all nodes and arcs.
	QCOMPARE(defaultRun().infra.numTrackLines, 1);
	QCOMPARE(defaultRun().infra.blockSets[0].numNodes, arcCount + 1);
	QCOMPARE(defaultRun().infra.blockSets[0].arcs, arcCount);
	QCOMPARE(defaultRun().infra.blockSets[0].len, arcCount);
	// The track buffers are sized from the scene.
	QCOMPARE(defaultRun().infra.blockSets[0].N.size(), static_cast<std::size_t>(arcCount + 1));
	QCOMPARE(defaultRun().infra.blockSets[0].A.size(), static_cast<std::size_t>(arcCount));
	QCOMPARE(defaultRun().infra.blockSets[0].member.size(), static_cast<std::size_t>(arcCount));
	// The long track keeps its first and last coordinates.
	QCOMPARE_EQ(defaultRun().infra.blockSets[0].member.front().startNode.X, 0.0);
	QCOMPARE_EQ(defaultRun().infra.blockSets[0].member.back().endNode.X, static_cast<double>(arcCount));
	// The long track keeps its arcs past the former limit.
	QCOMPARE_EQ(defaultRun().infra.blockSets[0].A.back().endNode.X, static_cast<double>(arcCount));
	// The long track yields one section per block.
	QCOMPARE(defaultRun().infra.numBlocks, arcCount / arcsPerBlock);
	resetNativeInfrastructureState(defaultRun());
	// The runtime reset releases the track buffers.
	QCOMPARE(defaultRun().infra.blockSets[0].N.capacity(), std::size_t(0));
	QCOMPARE(defaultRun().infra.blockSets[0].A.capacity(), std::size_t(0));
	QCOMPARE(defaultRun().infra.blockSets[0].member.capacity(), std::size_t(0));
}

void TestSceneBuilder::buildsManySections() {
	constexpr int blockCount = 6001;
	SceneModel scene;
	scene.name = "many-sections";
	scene.tracks = {{"track.many"}};
	for (int index = 0; index <= blockCount; ++index)
		scene.nodes.push_back({"node." + std::to_string(index), "track.many", static_cast<double>(index), 0.0});
	for (int index = 0; index < blockCount; ++index)
		scene.arcs.push_back({"arc." + std::to_string(index), "track.many", "node." + std::to_string(index),
			"node." + std::to_string(index + 1), 0.0, 0.0, 20.0});
	for (int index = 0; index < blockCount; ++index)
		scene.blocks.push_back({"block." + std::to_string(index), "track.many", 1.0});
	const auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), scene);
	QVERIFY2(!hasErrors(diagnostics), "a scene with more than 6000 sections builds");
	// The section storage is sized from the scene.
	QCOMPARE(defaultRun().infra.numBlocks, blockCount);
	QCOMPARE(defaultRun().infra.sections.size(), static_cast<std::size_t>(blockCount));
	// The sections past the former limit are built.
	QCOMPARE(defaultRun().infra.sections.front().ID, "@block.0@");
	QCOMPARE(defaultRun().infra.sections.back().ID, "@block.6000@");
	resetNativeInfrastructureState(defaultRun());
	// The runtime reset releases the section storage.
	QCOMPARE(defaultRun().infra.numBlocks, 0);
	QCOMPARE(defaultRun().infra.sections.capacity(), std::size_t(0));
}

void TestSceneBuilder::routeHoldsExactlyItsSections() {
	auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), tinyScene());
	// A route holds exactly the sections of its route.
	QVERIFY(!hasErrors(diagnostics));
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(1));
	QVERIFY(!defaultRun().infra.routes.front().reversed_direction);
	QCOMPARE(defaultRun().infra.routes.front().N_Block_Sections, 2);
	QCOMPARE(defaultRun().infra.routes.front().sequence_of_block_sections.size(), std::size_t(2));

	SceneModel reversed = tinyScene();
	reversed.routes.front().blocks = {"block.b", "block.a"};
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), reversed);
	// A reversed route holds exactly the sections of its route.
	QVERIFY(!hasErrors(diagnostics));
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(1));
	QVERIFY(defaultRun().infra.routes.front().reversed_direction);
	QCOMPARE(defaultRun().infra.routes.front().N_Block_Sections, 2);
	QCOMPARE(defaultRun().infra.routes.front().sequence_of_block_sections.size(), std::size_t(2));

	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), multiRegionRouteScene());
	// A route over several regions holds exactly the sections of its route.
	QVERIFY(!hasErrors(diagnostics));
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(1));
	QCOMPARE(defaultRun().infra.routes.front().N_Block_Sections, 3);
	QCOMPARE(defaultRun().infra.routes.front().sequence_of_block_sections.size(), std::size_t(3));

	resetNativeInfrastructureState(defaultRun());
	// The runtime reset removes the routes.
	QCOMPARE(defaultRun().infra.numRoutes, 0);
	QVERIFY(defaultRun().infra.routes.empty());
}

// The route starts and ends where its first and last sections do, at these kilometres.
static bool spansSections(const Route& route, double start, double end) {
	return route.N_Block_Sections > 0
		&& std::fabs(route.x_of_start_node - start) < 1e-9 && std::fabs(route.x_of_end_node - end) < 1e-9
		&& route.x_of_start_node == route.sequence_of_block_sections.front().start_node.X
		&& route.x_of_end_node == route.sequence_of_block_sections.back().end_node.X;
}

// A train leaves the network when its head passes the end of its route, so the end must lie where the last section ends, also when the
// sections after a jump of the km reference were moved to one reference.
void TestSceneBuilder::routeEndsWhereLastSectionEnds() {
	const auto reset = qScopeGuard([] { resetNativeInfrastructureState(defaultRun()); });
	auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), tinyScene());
	// A route without a jump ends where its last section ends.
	QVERIFY(!hasErrors(diagnostics));
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(1));
	QCOMPARE_EQ(defaultRun().infra.routes.front().diffRegionsJumpX.first, 0.0);
	QVERIFY(spansSections(defaultRun().infra.routes.front(), 0.0, 2.0));

	SceneModel reversed = tinyScene();
	reversed.routes.front().blocks = {"block.b", "block.a"};
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), reversed);
	// A reversed route without a jump ends where its last section ends.
	QVERIFY(!hasErrors(diagnostics));
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(1));
	QVERIFY(defaultRun().infra.routes.front().reversed_direction);
	QCOMPARE_EQ(defaultRun().infra.routes.front().diffRegionsJumpX.first, 0.0);
	QVERIFY(spansSections(defaultRun().infra.routes.front(), 0.0, 2.0));

	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), multiRegionRouteScene());
	// A route across a jump ends where its last section ends after the shift.
	QVERIFY(!hasErrors(diagnostics));
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(1));
	QCOMPARE_NE(defaultRun().infra.routes.front().diffRegionsJumpX.first, 0.0);
	QVERIFY(spansSections(defaultRun().infra.routes.front(), 0.0, 4.0));

	SceneModel reversedRegions = multiRegionRouteScene();
	reversedRegions.routes.front().blocks = {"c.block", "b.block", "a.block"};
	diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), reversedRegions);
	// A reversed route across a jump ends where its last section ends after the shift.
	QVERIFY(!hasErrors(diagnostics));
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(1));
	QVERIFY(defaultRun().infra.routes.front().reversed_direction);
	QCOMPARE_NE(defaultRun().infra.routes.front().diffRegionsJumpX.first, 0.0);
	QVERIFY(spansSections(defaultRun().infra.routes.front(), 0.0, 4.0));
}

// The sample objects give every member that the copy operators copy a value of its own, different from the constructor default.
static void fillNode(Node& n, double id) {
	double* numbers[] = {&n.X, &n.Y, &n.dwellTime, &n.StopTime, &n.arcSpeedLimit, &n.tdsbGeoCoordX, &n.tdsbGeoCoordY, &n.latitude, &n.longitude, &n.graphX, &n.graphY};
	double value = 11.5;
	for (double* number : numbers)
		*number = value++;
	n.ID = id;
	n.StepStopped = 31;
	n.indexOrderList = 32;
	n.numConnections = 3;
	for (int i = 0; i < 6; i++) {
		n.connectIdBlockSet[i] = 40 + i;
		n.connectXNode[i] = 50.5 + i;
	}
	n.isSignalled = n.station = n.respectOrder = n.virtualCouplingNode = n.virtualSignal = true;
	n.sceneNodeId = "node.sample";
	n.stationName = "Sample";
	n.stationPlatformId = "platform.sample";
	n.tdsbId = "tdsb.sample";
	n.IDConnectedBlocks = {"-B1@-1.0", "-B2@-2.0"};
}

static void fillArc(Arc& a, double id, double gradient) {
	double* numbers[] = {&a.length, &a.curvature, &a.speedLimit, &a.fs, &a.brakingDistance, &a.speedInBraking, &a.signalSpeedLimit};
	double value = 61.5;
	for (double* number : numbers)
		*number = value++;
	a.ID = id;
	a.gradient = gradient;
	fillNode(a.startNode, id + 100);
	fillNode(a.endNode, id + 200);
}

using NodeList = std::shared_ptr<const std::vector<Node>>;

static void fillSection(Section& s, const NodeList& nodes, TDS* tds) {
	double* numbers[] = {&s.length, &s.exit_speed, &s.code, &s.XStartSwitch, &s.XEndSwitch, &s.GeoXBegNode, &s.GeoXEndNode};
	double value = 71.5;
	for (double* number : numbers)
		*number = value++;
	s.ID = "section.sample";
	s.trackLineId = 81;
	s.FirstConnectedTrackLineID = 82;
	s.SecondConnectedTrackLineID = 83;
	s.SignallingLevel = 3;
	fillNode(s.start_node, 91);
	fillNode(s.end_node, 92);
	fillArc(s.arcs_in_signalling_block_section[0], 1, 0);
	fillArc(s.arcs_in_signalling_block_section[1], 2, 0.02);
	s.total_arcs = 2;
	s.nodelist_of_nodes_in_signalling_section = nodes;
	s.total_nodes = 2;
	s.state = SignalState::Red;
	s.Occupied = s.Occup_By_Train = s.withSwitchDiv = true;
	s.IDConnectedBS[0] = "section.a";
	s.IDConnectedBS[1] = "section.b";
	s.N_ConnectedBS = 2;
	s.ETCS3BrakingPoints[0] = 85.5;
	s.ETCS3BrakingPointsTrainID[0] = "train.sample";
	s.N_ETCS3BrakingPoints = 1;
	s.TDS_in_block = {tds};
}

// The comparisons go member by member, so that they do not depend on the operators under test.
static bool sameNode(const Node& a, const Node& b) {
	return a.sceneNodeId == b.sceneNodeId && a.ID == b.ID && a.X == b.X && a.Y == b.Y && a.isSignalled == b.isSignalled
		&& a.station == b.station && a.respectOrder == b.respectOrder && a.virtualCouplingNode == b.virtualCouplingNode
		&& a.dwellTime == b.dwellTime && a.StopTime == b.StopTime && a.StepStopped == b.StepStopped && a.indexOrderList == b.indexOrderList
		&& a.numConnections == b.numConnections && std::equal(a.connectIdBlockSet, a.connectIdBlockSet + 6, b.connectIdBlockSet)
		&& std::equal(a.connectXNode, a.connectXNode + 6, b.connectXNode) && a.IDConnectedBlocks == b.IDConnectedBlocks
		&& a.arcSpeedLimit == b.arcSpeedLimit && a.stationName == b.stationName && a.stationPlatformId == b.stationPlatformId
		&& a.tdsbId == b.tdsbId && a.tdsbGeoCoordX == b.tdsbGeoCoordX && a.tdsbGeoCoordY == b.tdsbGeoCoordY
		&& a.latitude == b.latitude && a.longitude == b.longitude && a.graphX == b.graphX && a.graphY == b.graphY
		&& a.virtualSignal == b.virtualSignal;
}

static bool sameArc(const Arc& a, const Arc& b) {
	return a.ID == b.ID && sameNode(a.startNode, b.startNode) && sameNode(a.endNode, b.endNode) && a.length == b.length
		&& a.curvature == b.curvature && a.gradient == b.gradient && a.speedLimit == b.speedLimit && a.fs == b.fs
		&& a.brakingDistance == b.brakingDistance && a.speedInBraking == b.speedInBraking && a.signalSpeedLimit == b.signalSpeedLimit;
}

static bool sameSection(const Section& a, const Section& b) {
	bool same = a.ID == b.ID && a.trackLineId == b.trackLineId && a.FirstConnectedTrackLineID == b.FirstConnectedTrackLineID
		&& a.SecondConnectedTrackLineID == b.SecondConnectedTrackLineID && sameNode(a.start_node, b.start_node) && sameNode(a.end_node, b.end_node)
		&& a.nodelist_of_nodes_in_signalling_section == b.nodelist_of_nodes_in_signalling_section && a.total_nodes == b.total_nodes
		&& a.total_arcs == b.total_arcs && a.length == b.length && a.exit_speed == b.exit_speed && a.code == b.code
		&& a.XStartSwitch == b.XStartSwitch && a.XEndSwitch == b.XEndSwitch && a.GeoXBegNode == b.GeoXBegNode && a.GeoXEndNode == b.GeoXEndNode
		&& a.SignallingLevel == b.SignallingLevel && a.state == b.state && a.Occupied == b.Occupied
		&& a.Occup_By_Train == b.Occup_By_Train && a.withSwitchDiv == b.withSwitchDiv && a.N_ConnectedBS == b.N_ConnectedBS
		&& a.N_ETCS3BrakingPoints == b.N_ETCS3BrakingPoints && a.TDS_in_block == b.TDS_in_block;
	for (int i = 0; same && i < a.total_arcs; i++)
		same = sameArc(a.arcs_in_signalling_block_section[i], b.arcs_in_signalling_block_section[i]);
	for (int i = 0; same && i < a.N_ConnectedBS; i++)
		same = a.IDConnectedBS[i] == b.IDConnectedBS[i];
	for (int i = 0; same && i < a.N_ETCS3BrakingPoints; i++)
		same = a.ETCS3BrakingPoints[i] == b.ETCS3BrakingPoints[i] && a.ETCS3BrakingPointsTrainID[i] == b.ETCS3BrakingPointsTrainID[i];
	return same;
}

// A change to one member of a sample object.
template <typename T>
struct MemberChange {
	const char* member;
	void (*change)(T&);
};

static const MemberChange<Node> kNodeChanges[] = {
	{"ID", [](Node& n) { n.ID += 1; }},
	{"X", [](Node& n) { n.X += 1; }},
	{"Y", [](Node& n) { n.Y += 1; }},
	{"isSignalled", [](Node& n) { n.isSignalled = false; }},
	{"station", [](Node& n) { n.station = false; }},
	{"dwellTime", [](Node& n) { n.dwellTime += 1; }},
};

static const MemberChange<Arc> kArcChanges[] = {
	{"ID", [](Arc& a) { a.ID += 1; }},
	{"length", [](Arc& a) { a.length += 1; }},
	{"startNode.ID", [](Arc& a) { a.startNode.ID += 1; }},
	{"endNode.ID", [](Arc& a) { a.endNode.ID += 1; }},
	{"speedLimit", [](Arc& a) { a.speedLimit += 1; }},
	{"curvature", [](Arc& a) { a.curvature += 1; }},
	{"fs", [](Arc& a) { a.fs += 1; }},
	{"brakingDistance", [](Arc& a) { a.brakingDistance += 1; }},
	{"speedInBraking", [](Arc& a) { a.speedInBraking += 1; }},
	{"signalSpeedLimit", [](Arc& a) { a.signalSpeedLimit += 1; }},
};

// A change to one member of a sample section. The list of nodes changes to the other list.
struct SectionChange {
	const char* member;
	void (*change)(Section&, const NodeList& otherNodes);
};

static const SectionChange kSectionChanges[] = {
	{"the gradient of the second arc", [](Section& s, const NodeList&) { s.arcs_in_signalling_block_section[1].gradient = 0.03; }},
	{"ID", [](Section& s, const NodeList&) { s.ID = "section.other"; }},
	{"state", [](Section& s, const NodeList&) { s.state = SignalState::Green; }},
	{"the list of nodes", [](Section& s, const NodeList& otherNodes) { s.nodelist_of_nodes_in_signalling_section = otherNodes; }},
	{"total_nodes", [](Section& s, const NodeList&) { s.total_nodes = 1; }},
	{"total_arcs", [](Section& s, const NodeList&) { s.total_arcs = 1; }},
	{"length", [](Section& s, const NodeList&) { s.length += 1; }},
	{"exit_speed", [](Section& s, const NodeList&) { s.exit_speed += 1; }},
	{"code", [](Section& s, const NodeList&) { s.code += 1; }},
	{"start_node.ID", [](Section& s, const NodeList&) { s.start_node.ID += 1; }},
};

// A row for each change of the table, named by the member it changes.
template <typename Change, std::size_t Count>
static void addChangeRows(const Change (&changes)[Count]) {
	QTest::addColumn<int>("index");
	for (int index = 0; index < static_cast<int>(Count); ++index)
		QTest::newRow(changes[index].member) << index;
}

void TestSceneBuilder::copiesNode() {
	Node source, reference;
	fillNode(source, 7);
	fillNode(reference, 7);

	Node copied(source);
	QVERIFY2(sameNode(copied, source), "a copied node holds the members and the block list of its source");
	QVERIFY2(copied.virtualSignal, "a copied node keeps the virtual signal flag");

	Node target;
	target.IDConnectedBlocks = {"old.1", "old.2", "old.3"};
	target = source;
	QVERIFY2(sameNode(target, source), "an assigned node holds the members and the block list of its source");
	QVERIFY2(target.virtualSignal, "an assigned node takes the virtual signal flag of its source");
	QVERIFY2(sameNode(source, reference), "assigning a node leaves its source unchanged");

	Node stale, plain;
	stale.virtualSignal = true;
	stale = plain;
	QVERIFY2(!stale.virtualSignal, "an assigned node loses its virtual signal flag when its source has none");

	Node& alias = source;
	source = alias;
	QVERIFY2(sameNode(source, reference), "a node assigned to itself keeps its members and its block list");

	// Nodes with the same members compare equal in both directions.
	QVERIFY(source == reference);
	QVERIFY(reference == source);
}

void TestSceneBuilder::nodesDifferingInOneMemberDiffer_data() {
	addChangeRows(kNodeChanges);
}

// A node that differs from the sample in one member compares unequal to it, in both directions.
void TestSceneBuilder::nodesDifferingInOneMemberDiffer() {
	QFETCH(int, index);
	Node sample, changed;
	fillNode(sample, 7);
	fillNode(changed, 7);
	kNodeChanges[index].change(changed);
	QVERIFY(!(sample == changed));
	QVERIFY(!(changed == sample));
}

void TestSceneBuilder::copiesArc() {
	Arc source, reference;
	fillArc(source, 1, 0.01);
	fillArc(reference, 1, 0.01);

	Arc copied(source);
	QVERIFY2(sameArc(copied, source), "a copied arc holds the members of its source");

	Arc target;
	target = source;
	QVERIFY2(sameArc(target, source), "an assigned arc holds the members of its source");
	QVERIFY2(sameArc(source, reference), "assigning an arc leaves its source unchanged");

	Arc& alias = source;
	source = alias;
	QVERIFY2(sameArc(source, reference), "an arc assigned to itself keeps its members");

	Arc flat, otherFlat;
	QVERIFY2(flat == otherFlat, "two default arcs compare equal");
	// Arcs with the same members compare equal in both directions.
	QVERIFY(source == reference);
	QVERIFY(reference == source);
	// Comparing equal arcs leaves their gradients unchanged.
	QCOMPARE_EQ(source.gradient, 0.01);
	QCOMPARE_EQ(reference.gradient, 0.01);
	Arc steeper;
	fillArc(steeper, 1, 0.02);
	// Arcs that differ only in gradient compare unequal in both directions.
	QVERIFY(!(source == steeper));
	QVERIFY(!(steeper == source));
	// Comparing unequal arcs leaves their gradients unchanged.
	QCOMPARE_EQ(source.gradient, 0.01);
	QCOMPARE_EQ(steeper.gradient, 0.02);
}

void TestSceneBuilder::arcsDifferingInOneMemberDiffer_data() {
	addChangeRows(kArcChanges);
}

// An arc that differs from the sample in one member compares unequal to it, in both directions.
void TestSceneBuilder::arcsDifferingInOneMemberDiffer() {
	QFETCH(int, index);
	Arc sample, changed;
	fillArc(sample, 1, 0.01);
	fillArc(changed, 1, 0.01);
	kArcChanges[index].change(changed);
	QVERIFY(!(sample == changed));
	QVERIFY(!(changed == sample));
}

void TestSceneBuilder::copiesSection() {
	const NodeList nodes = std::make_shared<const std::vector<Node>>(2);
	TDS tds, firstOtherTds, secondOtherTds;
	Section source, reference;
	fillSection(source, nodes, &tds);
	fillSection(reference, nodes, &tds);

	Section copied(source);
	QVERIFY2(sameSection(copied, source), "a copied section holds the members, arcs and detection sections of its source");

	Section target;
	target.TDS_in_block = {&firstOtherTds, &secondOtherTds};
	target = source;
	QVERIFY2(sameSection(target, source), "an assigned section holds the members, arcs and detection sections of its source");
	QVERIFY2(sameSection(source, reference), "assigning a section leaves its source unchanged");

	Section& alias = source;
	source = alias;
	QVERIFY2(sameSection(source, reference), "a section assigned to itself keeps its members, arcs and detection sections");

	// A section with the flag at its end node only, as the first half of a double switch has it in a route that runs forward.
	Section firstHalf, assignedHalf;
	fillSection(firstHalf, nodes, &tds);
	firstHalf.start_node.virtualSignal = false;
	assignedHalf = firstHalf;
	// An assigned section keeps which of its end nodes carries the virtual signal.
	QVERIFY(!assignedHalf.start_node.virtualSignal);
	QVERIFY(assignedHalf.end_node.virtualSignal);

	// Sections with the same members compare equal in both directions.
	QVERIFY(source == reference);
	QVERIFY(reference == source);
	QVERIFY2(sameSection(source, reference), "comparing equal sections leaves their arcs unchanged");
	Section steeper;
	fillSection(steeper, nodes, &tds);
	steeper.arcs_in_signalling_block_section[0].gradient = 0.05;
	// Sections that differ only in the gradient of the first arc compare unequal.
	QVERIFY(!(source == steeper));
	QVERIFY(!(steeper == source));
	QVERIFY2(sameSection(source, reference), "comparing unequal sections leaves the arcs of the left section unchanged");
}

void TestSceneBuilder::sectionsDifferingInOneMemberDiffer_data() {
	addChangeRows(kSectionChanges);
}

// A section that differs from the sample in one member compares unequal to it, in both directions.
void TestSceneBuilder::sectionsDifferingInOneMemberDiffer() {
	QFETCH(int, index);
	const NodeList nodes = std::make_shared<const std::vector<Node>>(2);
	const NodeList otherNodes = std::make_shared<const std::vector<Node>>(2);
	TDS tds;
	Section sample, changed;
	fillSection(sample, nodes, &tds);
	fillSection(changed, nodes, &tds);
	kSectionChanges[index].change(changed, otherNodes);
	QVERIFY(!(sample == changed));
	QVERIFY(!(changed == sample));
}

// The texts of the track detection section border ids, for plain sections and for sections with a diverging switch,
// and the text that a node searches its connected blocks with.
void TestSceneBuilder::namesTrackDetectionBorders() {
	const auto addArc = [](Section& target, double startX, double endX, int connections = 0, const std::string& name = std::string()) {
		Arc& added = target.arcs_in_signalling_block_section[target.total_arcs++];
		added.startNode.X = startX;
		added.endNode.X = endX;
		added.endNode.numConnections = connections;
		added.endNode.tdsbId = name;
	};
	const auto setSwitch = [](Section& target, const std::string& id, double xStart, double xEnd, double endX) {
		target.ID = id;
		target.withSwitchDiv = true;
		target.XStartSwitch = xStart;
		target.XEndSwitch = xEnd;
		target.end_node.X = endX;
	};
	std::vector<Section> sections(4);
	Section& plain = sections[0];
	plain.ID = "@P1@";
	plain.end_node.X = 20.0;
	addArc(plain, -9.0, -7.2345678, 1);
	addArc(plain, -7.2345678, 12.3456789, 1);
	Section& whole = sections[1];
	setSwitch(whole, "@Ba@-1.000000/@Bb@-2.000000", 20.1234567, 20.7654321, 20.7654321);
	addArc(whole, whole.XStartSwitch, whole.XEndSwitch);
	Section& ahead = sections[2];
	setSwitch(ahead, "@Bc@-1.000000/@Bd@-2.000000", 30.5555556, 31.4444444, 30.5555556);
	addArc(ahead, 29.0, 29.3333333, 1);
	addArc(ahead, 29.3333333, ahead.XStartSwitch);
	Section& named = sections[3];
	setSwitch(named, "@Be@-1.000000/@Bf@-2.000000", 40.2222222, 40.8888888, 50.0);
	addArc(named, 40.0, named.XStartSwitch, 0, "@Q@");
	addArc(named, named.XStartSwitch, named.XEndSwitch, 0, "@R@");
	addArc(named, named.XEndSwitch, 45.5555556, 1);
	setTrackDetectionSectionBoundariesAndGeoCoordAtSwitchesAndStations(sections.data(), static_cast<int>(sections.size()));
	const auto endId = [](const Section& target, int index) -> const std::string& {
		return target.arcs_in_signalling_block_section[index].endNode.tdsbId;
	};
	// The border of a first arc end without a switch.
	QCOMPARE(endId(plain, 0), "@-7.234568-P1@Point");
	// The border of a later arc end without a switch.
	QCOMPARE(endId(plain, 1), "@12.345679-P1@Point");
	// The border at the start of a switch at the section start.
	QCOMPARE(whole.arcs_in_signalling_block_section[0].startNode.tdsbId, "@Beg-Ba@/@20.765432-Bb@Point-Start");
	// The border at the end of a switch at the section end.
	QCOMPARE(endId(whole, 0), "@20.123457-Ba@/@20.765432-Bb@Point-End@20.123457-Ba@/@End-Bb@Point-End");
	// The border before a switch.
	QCOMPARE(endId(ahead, 0), "@29.333333-Bc@Point");
	// The border at the start of a switch at the section end.
	QCOMPARE(endId(ahead, 1), "@30.555556-Bc@/@31.444444-Bd@Point-Start@End-Bc@/@31.444444-Bd@Point-Start");
	// The named border at the start of a switch.
	QCOMPARE(endId(named, 0), "@Q@/@40.888889-Bf@Point-Start");
	// The named border at the end of a switch.
	QCOMPARE(endId(named, 1), "@40.222222-Be@/@R@Point-End");
	// The border after a switch.
	QCOMPARE(endId(named, 2), "@45.555556-Bf@Point");
}

// The messages of the infrastructure builder are sim.setup messages of the logging core. A sink collects them, because the console
// shows neither the category nor the level, and each text is compared with the exact wording, quirks included (a missing space,
// a tail that is written twice, "ahving").
struct Logged {
	eglog::Category category;
	eglog::Level level;
	std::string text;
};

struct Expected {
	eglog::Level level;
	std::string text;
};

// Collects the messages and puts the default sink and the default rules back on every path out of the slot.
struct Capture {
	std::vector<Logged> messages;
	Capture() {
		eglog::setSink([this](eglog::Category category, eglog::Level level, const std::string& text) { this->messages.push_back(Logged{category, level, text}); });
	}
	~Capture() {
		eglog::setSink({});
		eglog::configure("");
	}
	std::vector<Logged> take() {
		std::vector<Logged> taken;
		taken.swap(messages);
		return taken;
	}
};

constexpr eglog::Level infoLevel = eglog::Level::Info;
constexpr eglog::Level warningLevel = eglog::Level::Warning;
constexpr eglog::Level errorLevel = eglog::Level::Error;

static std::string visible(std::string text) {
	for (std::size_t at = 0; (at = text.find_first_of("\r\n", at)) != std::string::npos; at += 2)
		text.replace(at, 1, text[at] == '\r' ? "\\r" : "\\n");
	return text;
}

// Nothing when the messages found are the sim.setup messages wanted, and otherwise what with both lists.
static std::string loggedProblem(const std::vector<Logged>& found, const std::vector<Expected>& wanted, const std::string& what) {
	bool same = found.size() == wanted.size();
	for (std::size_t index = 0; same && index < found.size(); ++index)
		same = found[index].category == eglog::Category::SimSetup && found[index].level == wanted[index].level
			&& found[index].text == wanted[index].text;
	if (same)
		return {};
	std::string described = what + "\n  found:";
	for (const Logged& message : found)
		described += std::string("\n    ") + eglog::categoryName(message.category) + " " + eglog::levelName(message.level)
			+ " '" + visible(message.text) + "'";
	described += "\n  wanted:";
	for (const Expected& message : wanted)
		described += std::string("\n    sim.setup ") + eglog::levelName(message.level) + " '" + visible(message.text) + "'";
	return described;
}

// The messages of a cut of a section of length 10 with a diverging switch. The failed cut replaces the section by a default one, so
// the id is a constant.
static std::vector<Logged> cutMessages(Capture& capture, const std::string& part, double abscissa, double xStartSwitch, double xEndSwitch) {
	Section section;
	section.ID = "@Cut@";
	section.withSwitchDiv = true;
	section.XStartSwitch = xStartSwitch;
	section.XEndSwitch = xEndSwitch;
	section.end_node.X = 10.0;
	section.total_arcs = 1;
	section.arcs_in_signalling_block_section[0].endNode.X = 10.0;
	section.CutBlockSection(part, abscissa);
	return capture.take();
}

// The route setup of a build: the progress text of each route, then the line end, also without routes.
void TestSceneBuilder::logsRouteSetup() {
	const auto reset = qScopeGuard([] { resetNativeInfrastructureState(defaultRun()); });
	Capture capture;
	QVERIFY2(!hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), tinyScene())), "the scene with one route builds");
	std::string problem = loggedProblem(capture.take(), {{infoLevel, "\rCreating Scene Route : 0 route.tiny"}, {infoLevel, "\n"}},
		"the route setup of a scene with one route");
	QVERIFY2(problem.empty(), problem.c_str());
	QVERIFY2(!hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), stableConnectionScene())), "the scene without routes builds");
	problem = loggedProblem(capture.take(), {{infoLevel, "\n"}}, "the route setup of a scene without routes");
	QVERIFY2(problem.empty(), problem.c_str());
}

// A cut abscissa outside the section is an error.
void TestSceneBuilder::logsCutOutsideSection() {
	Capture capture;
	std::string problem = loggedProblem(cutMessages(capture, "CutEnd", 12.5, 2.0, 4.0),
		{{errorLevel, "ERROR: The Block Section @Cut@ cannot be cut in the inserted abscissa 12.5\n\n"}},
		"a cut outside the section");
	QVERIFY2(problem.empty(), problem.c_str());
	QVERIFY2(eglog::configure("sim.setup=false"), "the rule that switches sim.setup off is well formed");
	problem = loggedProblem(cutMessages(capture, "CutEnd", 12.5, 2.0, 4.0), {}, "a cut outside the section with sim.setup off");
	QVERIFY2(problem.empty(), problem.c_str());
}

// Valid cuts of a section of length 10 with a diverging switch from 2 to 4, where the cut touches or removes a switch edge.
void TestSceneBuilder::logsCutsAtSwitchEdges() {
	Capture capture;
	const std::string endEdgeCut = " has a diverging switch and it is being cut at a progressive which cuts out the XEndSwitch edge of the switch, "
								   "please consider repositioning the final edge of this switch or the starting edge of the switch "
								   "of the next block section in the route\n";
	const std::string startEdgeCut = " has a diverging switch and it is being cut at a progressive which cuts out the XStartSwitch edge of the switch, "
									 "please consider repositioning the starting edge of this switch or the final edge of the switch "
									 "of the previous block section in the route\n";
	std::string problem = loggedProblem(cutMessages(capture, "CutEnd", 4.0, 2.0, 4.0),
		{{infoLevel, "Cutting Block Section: @Cut@ at the ending edge of its diverging switch\n\n"}},
		"a cut at the end of the switch");
	QVERIFY2(problem.empty(), problem.c_str());
	problem = loggedProblem(cutMessages(capture, "CutEnd", 3.0, 2.0, 4.0), {{warningLevel, "Warning: Block Section: @Cut@" + endEdgeCut}},
		"a cut inside the switch");
	QVERIFY2(problem.empty(), problem.c_str());
	problem = loggedProblem(cutMessages(capture, "CutEnd", 1.0, 2.0, 4.0), {{warningLevel, "Warning: Block Section: @Cut@" + endEdgeCut}},
		"a cut before the switch");
	QVERIFY2(problem.empty(), problem.c_str());
	problem = loggedProblem(cutMessages(capture, "CutBegin", 2.0, 2.0, 4.0),
		{{infoLevel, "Cutting Block Section: @Cut@at the starting edge of its diverging switch\n\n"}},
		"a cut at the start of the switch");
	QVERIFY2(problem.empty(), problem.c_str());
	problem = loggedProblem(cutMessages(capture, "CutBegin", 3.0, 2.0, 4.0), {{warningLevel, "Warning: Block Section : @Cut@" + startEdgeCut}},
		"a cut inside the switch from the start");
	QVERIFY2(problem.empty(), problem.c_str());
	problem = loggedProblem(cutMessages(capture, "CutBegin", 3.0, 4.0, 2.0),
		{{errorLevel, "ERROR: Block Section: @Cut@ has Ending Edge of diverging switch with a progressive inferior to its Starting Egde. "
					  "Problems in cutting the block section. PLease correct Switch on the infrastructure\n\n"}},
		"a cut of a switch that ends before it starts");
	QVERIFY2(problem.empty(), problem.c_str());
}

// The elements that a train crosses between 4 and 6 and that miss a name.
void TestSceneBuilder::logsCrossedElementsWithoutName() {
	Capture capture;
	const auto crossing = [&](const InfraElement& element) {
		std::list<InfraElement> elements(1);
		elements.front() = element;
		UpdateInfrastructureElementStatus(defaultRun(), elements, 10, 4.0, 6.0, 0.0, "T1");
		return capture.take();
	};
	InfraElement divergingSwitch;
	divergingSwitch.XCoordinate = 5.0;
	divergingSwitch.IsSwitch = true;
	divergingSwitch.withSwitchDiv = true;
	std::string problem = loggedProblem(crossing(divergingSwitch),
		{{warningLevel, "Warning: Diverging Switch: Noneat position 5 on Route of Train T1 has no SwitchName. Please investigate on it\n"},
			{warningLevel, "Warning: Position not found for connected Point of Switch: None on Route of TrainT1\n"}},
		"a diverging switch with no name and no connected point");
	QVERIFY2(problem.empty(), problem.c_str());
	divergingSwitch.ID = "sw1";
	divergingSwitch.XConnectedPoint = 7.0;
	problem = loggedProblem(crossing(divergingSwitch),
		{{warningLevel, "Warning: Diverging Switch: sw1at position 5 on Route of Train T1 has no SwitchName. Please investigate on it\n"}},
		"a diverging switch with no switch name");
	QVERIFY2(problem.empty(), problem.c_str());
	divergingSwitch.SwitchName = "@s@Point";
	divergingSwitch.XConnectedPoint = -1;
	problem = loggedProblem(crossing(divergingSwitch),
		{{warningLevel, "Warning: Position not found for connected Point of Switch: sw1 on Route of TrainT1\n"}},
		"a diverging switch with no connected point");
	QVERIFY2(problem.empty(), problem.c_str());
	InfraElement unnamed;
	unnamed.XCoordinate = 5.0;
	problem = loggedProblem(crossing(unnamed),
		{{warningLevel, "Warning: Infrastructure Element at position 5 on Route of Train T1 has no ID. Please investigate on it\n"}},
		"an element with no id");
	QVERIFY2(problem.empty(), problem.c_str());
}

// The track detection section borders of one hand-made section per call. A switch section has the tokens of two blocks in its id.
void TestSceneBuilder::logsDetectionBorderWarnings() {
	Capture capture;
	const auto oneSection = [](const std::string& id, bool withSwitch, double xStartSwitch, double xEndSwitch, double endX) {
		std::vector<Section> sections(1);
		sections[0].ID = id;
		sections[0].withSwitchDiv = withSwitch;
		sections[0].XStartSwitch = xStartSwitch;
		sections[0].XEndSwitch = xEndSwitch;
		sections[0].end_node.X = endX;
		return sections;
	};
	const auto addArc = [](Section& target, double startX, double endX, int startConnections, const std::string& startName, int endConnections) {
		Arc& added = target.arcs_in_signalling_block_section[target.total_arcs++];
		added.startNode.X = startX;
		added.startNode.numConnections = startConnections;
		added.startNode.tdsbId = startName;
		added.endNode.X = endX;
		added.endNode.numConnections = endConnections;
	};
	const auto borders = [&](std::vector<Section>& sections) {
		setTrackDetectionSectionBoundariesAndGeoCoordAtSwitchesAndStations(sections.data(), static_cast<int>(sections.size()));
		return capture.take();
	};
	const std::string switchId = "@Be@-1.000000/@Bf@-2.000000";
	const std::string noEndName = " has End Node with no name, a name will be automatically assigned to it "
								  "and need to manually check congruency in case of timetable compression\n";
	const std::string noBeginningName = "Warning: Block " + switchId + " has Beginning Node with no name. A name will be automatically assigned to it "
																	   "and need to manually check congruency in case of timetable compression\n";
	const std::string startingNode = "ERROR: in Block Section" + switchId + " the starting Node has a position ";
	std::vector<Section> plain = oneSection("@P1@", false, 0.0, 0.0, 20.0);
	addArc(plain[0], -9.0, -7.2345678, 0, "", 0);
	addArc(plain[0], -7.2345678, 20.0, 0, "", 1);
	std::string problem = loggedProblem(borders(plain), {{warningLevel, "Warning: Block @P1@" + noEndName}},
		"the last node of a section without a switch has no name");
	QVERIFY2(problem.empty(), problem.c_str());
	const double wholeStart = 20.1234567;
	const double wholeEnd = 20.7654321;
	std::vector<Section> whole = oneSection(switchId, true, wholeStart, wholeEnd, wholeEnd);
	addArc(whole[0], wholeStart, wholeEnd, 0, "", 0);
	problem = loggedProblem(borders(whole), {{warningLevel, noBeginningName}},
		"the first node of the first arc is the start of the switch and has no name");
	QVERIFY2(problem.empty(), problem.c_str());
	const double xStart = 40.2222222;
	const double xEnd = 40.8888888;
	std::vector<Section> beforeSwitch = oneSection(switchId, true, xStart, xEnd, 50.0);
	addArc(beforeSwitch[0], 40.0, 41.0, 1, "", 0);
	problem = loggedProblem(borders(beforeSwitch),
		{{warningLevel, noBeginningName}},
		"the first node of the first arc is a switch before the switch of the section and has no name");
	QVERIFY2(problem.empty(), problem.c_str());
	std::vector<Section> named = oneSection(switchId, true, xStart, xEnd, 50.0);
	addArc(named[0], 40.0, 41.0, 1, "@Q@", 0);
	problem = loggedProblem(borders(named),
		{{warningLevel, "Warning: The initial Node of Block Section: " + switchId + "is also a switch: Changing its TDSB ID accordingly in: @Q@Point\n"}},
		"the first node of the first arc is a switch and has a name");
	QVERIFY2(problem.empty(), problem.c_str());
	// The name in the message is the name of the node.
	QCOMPARE(named[0].arcs_in_signalling_block_section[0].startNode.tdsbId, "@Q@Point");
	std::vector<Section> afterSwitch = oneSection(switchId, true, xStart, xEnd, 50.0);
	addArc(afterSwitch[0], 41.5, 45.0, 1, "", 0);
	problem = loggedProblem(borders(afterSwitch),
		{{warningLevel, noBeginningName}, {errorLevel, startingNode + "that goes after the end of the Switch\n"}},
		"the first node of the first arc is a switch after the switch of the section");
	QVERIFY2(problem.empty(), problem.c_str());
	std::vector<Section> insideSwitch = oneSection(switchId, true, xStart, xEnd, 50.0);
	addArc(insideSwitch[0], 40.5, 41.0, 1, "", 0);
	problem = loggedProblem(borders(insideSwitch),
		{{warningLevel, noBeginningName}, {errorLevel, startingNode + "in between the start and the end of the switch\n"}},
		"the first node of the first arc is a switch inside the switch of the section");
	QVERIFY2(problem.empty(), problem.c_str());
	const std::string aheadId = "@Bc@-1.000000/@Bd@-2.000000";
	const double aheadStart = 30.5555556;
	std::vector<Section> ahead = oneSection(aheadId, true, aheadStart, 31.4444444, aheadStart);
	addArc(ahead[0], 29.0, aheadStart, 0, "", 0);
	problem = loggedProblem(borders(ahead),
		{{warningLevel, "Warning: Check correctness of Block Section:" + aheadId + " its ending Node is the start of the switch "
																				   "on the same block section\n\n"}},
		"the last node of the section is the start of its switch");
	QVERIFY2(problem.empty(), problem.c_str());
	std::vector<Section> between = oneSection(switchId, true, xStart, xEnd, 50.0);
	addArc(between[0], 40.0, 40.5, 0, "", 1);
	const std::string betweenSwitchEdges = " Node at position: 40.5 is a switch defined in between the start (XStartSwitch) "
										   "and the end (XEndSwitch) of the diverging switch. A name will be automatically assigned to it\n";
	problem = loggedProblem(borders(between),
		{{warningLevel, "WARNING: in Block Section" + switchId + betweenSwitchEdges}},
		"the end node of an arc is a switch inside the switch of the section");
	QVERIFY2(problem.empty(), problem.c_str());
}

// A switch edge with a one-token name cannot be read as a diverging switch. Three of the four messages write the tail twice.
void TestSceneBuilder::logsShortSwitchNames() {
	Capture capture;
	const auto edge = [] {
		Section made;
		made.ID = "@S1@";
		made.withSwitchDiv = true;
		return made;
	};
	const auto switchName = [&](bool reversed, const Section& made) {
		Route route;
		route.ID = "R1";
		route.reversed_direction = reversed;
		route.setListInfrastructureElementsForRoute(made);
		return capture.take();
	};
	const std::string tail = " is a diverging switch with less than 4 characters in its name. Please check why...\n";
	Section startEdge = edge();
	startEdge.start_node.tdsbId = "AB";
	startEdge.start_node.numConnections = 1;
	std::string problem = loggedProblem(switchName(false, startEdge),
		{{warningLevel, "Warning: Switch at starting Node: AB on Block Section @S1@for Route R1 that has reversed = 0" + tail}},
		"a switch at the start of a section");
	QVERIFY2(problem.empty(), problem.c_str());
	Section arcEdge = edge();
	arcEdge.total_arcs = 1;
	arcEdge.arcs_in_signalling_block_section[0].endNode.tdsbId = "AB";
	arcEdge.arcs_in_signalling_block_section[0].endNode.tdsbGeoCoordX = 0.0;
	arcEdge.arcs_in_signalling_block_section[0].endNode.numConnections = 1;
	problem = loggedProblem(switchName(false, arcEdge),
		{{warningLevel, "Warning: Switch at end of one of the arcs: AB on Block Section @S1@for RouteR1that has reversed = 0" + tail + tail}},
		"a switch at the end of an arc");
	QVERIFY2(problem.empty(), problem.c_str());
	Section reversedEdge = edge();
	reversedEdge.total_arcs = 1;
	reversedEdge.arcs_in_signalling_block_section[0].startNode.tdsbId = "AB";
	reversedEdge.arcs_in_signalling_block_section[0].startNode.tdsbGeoCoordX = 0.0;
	reversedEdge.arcs_in_signalling_block_section[0].startNode.numConnections = 1;
	problem = loggedProblem(switchName(true, reversedEdge),
		{{warningLevel, "Warning: Switch at one of the beginning nodes: AB on Block Section @S1@for RouteR1that has reversed = 1" + tail + tail}},
		"a switch at the start of an arc of a reversed route");
	QVERIFY2(problem.empty(), problem.c_str());
	Section endEdge = edge();
	endEdge.end_node.tdsbId = "AB";
	endEdge.end_node.numConnections = 1;
	problem = loggedProblem(switchName(false, endEdge),
		{{warningLevel, "Warning: Switch at ending Node: AB on Block Section @S1@for RouteR1that has reversed = 0" + tail + tail}},
		"a switch at the end of a section");
	QVERIFY2(problem.empty(), problem.c_str());
}

// The switch of a section is connected to blocks that are not the section.
void TestSceneBuilder::logsSwitchConnectedToOtherBlocks() {
	Capture capture;
	Section unknownBlock;
	unknownBlock.ID = "@C@";
	unknownBlock.total_arcs = 1;
	unknownBlock.arcs_in_signalling_block_section[0].endNode.X = 1.0;
	unknownBlock.arcs_in_signalling_block_section[0].endNode.numConnections = 1;
	unknownBlock.arcs_in_signalling_block_section[0].endNode.IDConnectedBlocks.push_back("@A@-1.000000/@B@-2.000000");
	lockSwitchesOnAllConnectedSections(defaultRun(), 1010.0, 990.0, 10.0, 0.0, unknownBlock, "now", "next", "T1", false, "None");
	defaultRun().etcsMa.clear();
	const std::string problem = loggedProblem(capture.take(),
		{{errorLevel, "\n\nError: there is not Block ahving the same ID of Block signalling_block_sections in Function: "
					  "lockSwitchesOnAllConnectedSections...Please investigate on that\n\n"}},
		"a switch connected to blocks that are not the section");
	QVERIFY2(problem.empty(), problem.c_str());
}

// A section whose id is part of the ids of eleven others has more connected sections than it can hold.
void TestSceneBuilder::logsTooManyConnectedSections() {
	Capture capture;
	defaultRun().infra.sections = std::vector<Section>(12);
	defaultRun().infra.numBlocks = static_cast<int>(defaultRun().infra.sections.size());
	defaultRun().infra.sections[0].ID = "@Cap@";
	for (std::size_t index = 1; index < defaultRun().infra.sections.size(); ++index)
		defaultRun().infra.sections[index].ID = "@Cap@-" + std::to_string(index);
	setDependenciesBetweenBlocks(defaultRun());
	resetNativeInfrastructureState(defaultRun());
	const std::string problem = loggedProblem(capture.take(),
		{{errorLevel, "ERROR: Block section @Cap@ has more than 10 connected block sections\n"}},
		"a section with more connected sections than it holds");
	QVERIFY2(problem.empty(), problem.c_str());
}

// The ids that occupyBlockAndConnected, occupyDoubleSwitch, releaseDoubleSwitch and releaseLastBlockAndConnected add to blocksOccupied
// and blocksConnected of the run: the section, its connected sections and, for a diverging switch, the two block ids of its name, in this order
// and each id once.
using IdList = std::list<std::string>;

static void startWith(const IdList& occupied, const IdList& connected) {
	defaultRun().blocksOccupied = occupied;
	defaultRun().blocksConnected = connected;
}

static void describe(Section& target, const std::string& id, const std::vector<std::string>& linked, bool diverging = false) {
	target.ID = id;
	target.withSwitchDiv = diverging;
	target.N_ConnectedBS = static_cast<int>(linked.size());
	for (std::size_t index = 0; index < linked.size(); ++index)
		target.IDConnectedBS[index] = linked[index];
}

// A section is added to blocksOccupied after the connected sections that are listed already, and the previous section is released.
void TestSceneBuilder::occupiesSectionAndConnectedSections() {
	std::vector<Section> halves(2);
	Section& current = halves[0];
	Section& previous = halves[1];
	describe(current, "occ.main", {"occ.c1", "occ.c2"});
	describe(previous, "occ.prev", {});
	startWith({"occ.c1"}, {});
	occupyBlockAndConnected(defaultRun(), current, previous, 5000.0, 5000.0);
	// An occupied section adds itself and the connected sections that are missing.
	QCOMPARE(defaultRun().blocksOccupied, (IdList{"occ.c1", "occ.main", "occ.c2"}));
	QCOMPARE(defaultRun().blocksConnected, (IdList{"occ.prev"}));
	occupyBlockAndConnected(defaultRun(), current, previous, 5000.0, 5000.0);
	// A second occupation of the same sections adds no id twice.
	QCOMPARE(defaultRun().blocksOccupied, (IdList{"occ.c1", "occ.main", "occ.c2"}));
	QCOMPARE(defaultRun().blocksConnected, (IdList{"occ.prev"}));
}

// The connected sections of the previous section are added to blocksConnected when the train tail crosses its end node, before the
// previous section itself.
void TestSceneBuilder::tailConnectsSectionsOfPreviousSection() {
	std::vector<Section> halves(2);
	Section& current = halves[0];
	Section& previous = halves[1];
	describe(current, "occ.main", {"occ.c1", "occ.c2"});
	describe(previous, "occ.prev", {"prev.c1"});
	previous.end_node.X = 2.0;
	startWith({}, {});
	occupyBlockAndConnected(defaultRun(), current, previous, 2001.0, 1999.0);
	// The tail crossing the end of the previous section connects its sections first.
	QCOMPARE(defaultRun().blocksOccupied, (IdList{"occ.main", "occ.c1", "occ.c2"}));
	QCOMPARE(defaultRun().blocksConnected, (IdList{"prev.c1", "occ.prev"}));
	startWith({}, {});
	occupyBlockAndConnected(defaultRun(), current, previous, 2002.0, 2001.0);
	// The tail past the end of the previous section connects its sections after the section.
	QCOMPARE(defaultRun().blocksOccupied, (IdList{"occ.main", "occ.c1", "occ.c2"}));
	QCOMPARE(defaultRun().blocksConnected, (IdList{"occ.prev", "prev.c1"}));
}

// A section with the largest number of connected sections.
void TestSceneBuilder::occupiesTenConnectedSections() {
	std::vector<Section> halves(2);
	Section& current = halves[0];
	Section& previous = halves[1];
	describe(current, "big.main", {"big.c0", "big.c1", "big.c2", "big.c3", "big.c4", "big.c5", "big.c6", "big.c7", "big.c8", "big.c9"});
	describe(previous, "occ.prev", {});
	startWith({}, {});
	occupyBlockAndConnected(defaultRun(), current, previous, 5000.0, 5000.0);
	// A section with ten connected sections adds all eleven ids.
	QCOMPARE(defaultRun().blocksOccupied,
		(IdList{"big.main", "big.c0", "big.c1", "big.c2", "big.c3", "big.c4", "big.c5", "big.c6", "big.c7", "big.c8", "big.c9"}));
	QCOMPARE(defaultRun().blocksConnected, (IdList{"occ.prev"}));
	startWith({"big.c9"}, {});
	occupyBlockAndConnected(defaultRun(), current, previous, 5000.0, 5000.0);
	// The last connected section of ten is skipped when it is listed already.
	QCOMPARE(defaultRun().blocksOccupied,
		(IdList{"big.c9", "big.main", "big.c0", "big.c1", "big.c2", "big.c3", "big.c4", "big.c5", "big.c6", "big.c7", "big.c8"}));
	QCOMPARE(defaultRun().blocksConnected, (IdList{"occ.prev"}));
}

// A released section without a switch adds itself and its connected sections to blocksConnected only.
void TestSceneBuilder::releasesSectionAndConnectedSections() {
	Section current;
	describe(current, "rel.main", {"rel.c1", "rel.c2"});
	startWith({}, {"rel.c2"});
	releaseLastBlockAndConnected(defaultRun(), current);
	// A released section adds itself and the connected sections that are missing.
	QCOMPARE(defaultRun().blocksOccupied, IdList());
	QCOMPARE(defaultRun().blocksConnected, (IdList{"rel.c2", "rel.main", "rel.c1"}));
}

// A released section with a diverging switch also adds the two block ids of its name, before the connected sections.
void TestSceneBuilder::releasesSwitchWithItsBlocks() {
	Section current;
	const std::string switchId = "@sw.a@-1.000000/@sw.b@-2.000000";
	describe(current, switchId, {"sw.c1", "sw.c2"}, true);
	startWith({}, {"sw.c1"});
	releaseLastBlockAndConnected(defaultRun(), current);
	// A released section with a switch adds the two blocks of its name.
	QCOMPARE(defaultRun().blocksOccupied, IdList());
	QCOMPARE(defaultRun().blocksConnected, (IdList{"sw.c1", switchId, "@sw.a@", "@sw.b@", "sw.c2"}));
	describe(current, switchId, {"sw.d0", "sw.d1", "sw.d2", "sw.d3", "sw.d4", "sw.d5", "sw.d6", "sw.d7", "sw.d8", "sw.d9"}, true);
	startWith({}, {});
	releaseLastBlockAndConnected(defaultRun(), current);
	// A released section with a switch and ten connected sections adds thirteen ids.
	QCOMPARE(defaultRun().blocksOccupied, IdList());
	QCOMPARE(defaultRun().blocksConnected,
		(IdList{switchId, "@sw.a@", "@sw.b@", "sw.d0", "sw.d1", "sw.d2", "sw.d3", "sw.d4", "sw.d5", "sw.d6", "sw.d7", "sw.d8", "sw.d9"}));
}

// A switch whose name holds no two block ids changes nothing.
void TestSceneBuilder::switchWithoutBlocksChangesNoList() {
	Section current;
	describe(current, "sw.without.blocks", {"sw.c1"}, true);
	startWith({"kept.occupied"}, {"kept.connected"});
	releaseLastBlockAndConnected(defaultRun(), current);
	// A section with a switch and a name without two blocks changes neither list.
	QCOMPARE(defaultRun().blocksOccupied, (IdList{"kept.occupied"}));
	QCOMPARE(defaultRun().blocksConnected, (IdList{"kept.connected"}));
}

// A double switch: two switch halves, each with its two blocks in the section list.
void TestSceneBuilder::occupiesAndReleasesDoubleSwitch() {
	const auto clear = qScopeGuard([] {
		defaultRun().blocksOccupied.clear();
		defaultRun().blocksConnected.clear();
		resetNativeInfrastructureState(defaultRun());
	});
	std::vector<Section> halves(2);
	Section& current = halves[0];
	Section& previous = halves[1];
	resetNativeInfrastructureState(defaultRun());
	defaultRun().infra.sections = std::vector<Section>(4);
	defaultRun().infra.numBlocks = static_cast<int>(defaultRun().infra.sections.size());
	describe(defaultRun().infra.sections[0], "@dsA@", {"a.c"});
	describe(defaultRun().infra.sections[1], "@dsB@", {"b.c"});
	describe(defaultRun().infra.sections[2], "@dsC@", {"c.c"});
	describe(defaultRun().infra.sections[3], "@dsD@", {"d.c"});
	const std::string firstId = "@dsA@-1.000000/@dsB@-2.000000";
	const std::string secondId = "@dsC@-3.000000/@dsD@-4.000000";
	describe(current, firstId, {"s1.c"});
	describe(previous, secondId, {});
	const IdList fullOccupied = {firstId, "s1.c", "@dsA@", "@dsB@", "a.c", "b.c", secondId, "@dsC@", "@dsD@", "c.c", "d.c"};
	startWith({}, {});
	occupyDoubleSwitch(defaultRun(), current, previous);
	// A double switch occupies both halves, their blocks and the sections connected to them.
	QCOMPARE(defaultRun().blocksOccupied, fullOccupied);
	QCOMPARE(defaultRun().blocksConnected, (IdList{"@dsA@", "@dsB@", "@dsC@", "@dsD@"}));
	startWith({firstId}, {});
	occupyDoubleSwitch(defaultRun(), current, previous);
	// A double switch whose first half is listed already still adds the sections connected to it.
	QCOMPARE(defaultRun().blocksOccupied, fullOccupied);
	QCOMPARE(defaultRun().blocksConnected, (IdList{"@dsA@", "@dsB@", "@dsC@", "@dsD@"}));

	current.withSwitchDiv = true;
	previous.withSwitchDiv = false;
	startWith({}, {});
	releaseDoubleSwitch(defaultRun(), current, previous);
	// A released double switch with a switch on its first half only.
	QCOMPARE(defaultRun().blocksOccupied, IdList());
	QCOMPARE(defaultRun().blocksConnected, (IdList{firstId, "@dsA@", "@dsB@", "s1.c", "a.c", "b.c", secondId}));
	current.withSwitchDiv = false;
	startWith({}, {});
	releaseDoubleSwitch(defaultRun(), current, previous);
	// A released double switch without a switch on either half.
	QCOMPARE(defaultRun().blocksOccupied, IdList());
	QCOMPARE(defaultRun().blocksConnected, (IdList{firstId, "s1.c", secondId}));
	current.withSwitchDiv = true;
	previous.withSwitchDiv = true;
	startWith({}, {});
	releaseDoubleSwitch(defaultRun(), current, previous);
	// A released double switch with a switch on both halves.
	QCOMPARE(defaultRun().blocksOccupied, IdList());
	QCOMPARE(defaultRun().blocksConnected,
		(IdList{firstId, "@dsA@", "@dsB@", "s1.c", "a.c", "b.c", secondId, "@dsC@", "@dsD@", "c.c", "d.c"}));
}

// The nodes that a section lists are read through this one function.
static const Node& listedNode(const Section& section, int index) {
	return (*section.nodelist_of_nodes_in_signalling_section)[index];
}

// The abscissas of the listed nodes in order; empty when the section has no list.
static std::vector<double> listedAbscissas(const Section& section) {
	std::vector<double> abscissas;
	if (section.nodelist_of_nodes_in_signalling_section != nullptr)
		for (int index = 0; index < section.total_nodes; ++index)
			abscissas.push_back(listedNode(section, index).X);
	return abscissas;
}

// The scene ids of the listed nodes in order; empty when the section has no list.
static std::vector<std::string> listedSceneIds(const Section& section) {
	std::vector<std::string> sceneIds;
	if (section.nodelist_of_nodes_in_signalling_section != nullptr)
		for (int index = 0; index < section.total_nodes; ++index)
			sceneIds.push_back(listedNode(section, index).sceneNodeId);
	return sceneIds;
}

// A section of length 10 without a switch, made of one arc from 0 to 4 and one arc from 4 to 10.
static void describeCuttableSection(Section& section) {
	section.ID = "@Cut@";
	section.start_node.X = 0.0;
	section.end_node.X = 10.0;
	section.total_arcs = 2;
	section.arcs_in_signalling_block_section[0].startNode.X = 0.0;
	section.arcs_in_signalling_block_section[0].endNode.X = 4.0;
	section.arcs_in_signalling_block_section[1].startNode.X = 4.0;
	section.arcs_in_signalling_block_section[1].endNode.X = 10.0;
}

// A section built from a scene lists the nodes of its track that it passes, from its first node to its last.
void TestSceneBuilder::sceneSectionsListTheirNodes() {
	resetNativeInfrastructureState(defaultRun());
	// The scene with two blocks builds.
	QVERIFY(!hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), tinyScene())));
	QCOMPARE(defaultRun().infra.numBlocks, 2);
	const Section& first = defaultRun().infra.sections[0];
	const Section& second = defaultRun().infra.sections[1];
	// The first block of the scene lists its two nodes.
	QCOMPARE(first.ID, "@block.a@");
	QCOMPARE(first.total_nodes, 2);
	QCOMPARE(listedAbscissas(first), std::vector<double>({0.0, 1.0}));
	QCOMPARE(listedSceneIds(first), std::vector<std::string>({"node.0", "node.1"}));
	// The second block of the scene lists its two nodes.
	QCOMPARE(second.ID, "@block.b@");
	QCOMPARE(second.total_nodes, 2);
	QCOMPARE(listedAbscissas(second), std::vector<double>({1.0, 2.0}));
	QCOMPARE(listedSceneIds(second), std::vector<std::string>({"node.1", "node.2"}));
}

// A section derived for a switch lists the nodes of the first section up to the switch node, the switch node of the second section
// and the nodes of the second section behind it.
void TestSceneBuilder::switchSectionListsNodesOfBothBlocks() {
	const auto reset = qScopeGuard([] { resetNativeInfrastructureState(defaultRun()); });
	// The scene with a switch builds.
	QVERIFY(!hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), stableConnectionScene())));
	QCOMPARE(defaultRun().infra.numBlocks, 3);
	const Section& alpha = defaultRun().infra.sections[0];
	const Section& beta = defaultRun().infra.sections[1];
	const Section& derived = defaultRun().infra.sections[2];
	// The first block of the switch scene lists its two nodes.
	QCOMPARE(alpha.ID, "@alpha.block@");
	QCOMPARE(alpha.total_nodes, 2);
	QCOMPARE(listedAbscissas(alpha), std::vector<double>({0.0, 1.0}));
	QCOMPARE(listedSceneIds(alpha), std::vector<std::string>({"alpha.start", "alpha.end"}));
	// The second block of the switch scene lists its two nodes.
	QCOMPARE(beta.ID, "@beta.block@");
	QCOMPARE(beta.total_nodes, 2);
	QCOMPARE(listedAbscissas(beta), std::vector<double>({2.0, 3.0}));
	QCOMPARE(listedSceneIds(beta), std::vector<std::string>({"beta.start", "beta.end"}));
	// The section derived for the switch lists the nodes of both blocks in order.
	QCOMPARE(derived.ID, "@alpha.block@-1.000000/@beta.block@-2.000000");
	QCOMPARE(derived.total_nodes, 4);
	QCOMPARE(listedAbscissas(derived), std::vector<double>({0.0, 1.0, 2.0, 3.0}));
	QCOMPARE(listedSceneIds(derived), std::vector<std::string>({"alpha.start", "alpha.end", "beta.start", "beta.end"}));
}

// A cut section lists its start node and the end node of each of its arcs.
void TestSceneBuilder::cutSectionListsNodesToCut() {
	Section source;
	describeCuttableSection(source);
	{
		Section cut(source);
		cut.CutBlockSection("CutEnd", 6.0);
		// A section cut at its end lists the nodes up to the cut.
		QCOMPARE(cut.total_nodes, 3);
		QCOMPARE(listedAbscissas(cut), std::vector<double>({0.0, 4.0, 6.0}));
	}
	{
		Section cut(source);
		cut.CutBlockSection("CutBegin", 6.0);
		// A section cut at its start lists the nodes from the cut.
		QCOMPARE(cut.total_nodes, 2);
		QCOMPARE(listedAbscissas(cut), std::vector<double>({6.0, 10.0}));
	}
}

using WeakList = std::weak_ptr<const std::vector<Node>>;

// A default section has an empty list. A list that a section and its copies share is freed when the last of them lets go of it.
void TestSceneBuilder::copiesShareNodeList() {
	Section owner;
	QVERIFY2(owner.nodelist_of_nodes_in_signalling_section == nullptr, "a default section has an empty list");
	owner.nodelist_of_nodes_in_signalling_section = std::make_shared<const std::vector<Node>>(2);
	{
		Section copied(owner);
		Section assigned;
		assigned = owner;
		// A copied and an assigned section share the list of their source.
		QVERIFY(copied.nodelist_of_nodes_in_signalling_section == owner.nodelist_of_nodes_in_signalling_section);
		QVERIFY(assigned.nodelist_of_nodes_in_signalling_section == owner.nodelist_of_nodes_in_signalling_section);
		QCOMPARE(owner.nodelist_of_nodes_in_signalling_section.use_count(), 3L);
	}
	// A copy that is gone no longer holds the list.
	QCOMPARE(owner.nodelist_of_nodes_in_signalling_section.use_count(), 1L);
	Section other;
	other.nodelist_of_nodes_in_signalling_section = std::make_shared<const std::vector<Node>>(3);
	const WeakList replaced = other.nodelist_of_nodes_in_signalling_section;
	other = owner;
	// A section that is assigned another list releases its own.
	QVERIFY(replaced.expired());
	QVERIFY(other.nodelist_of_nodes_in_signalling_section == owner.nodelist_of_nodes_in_signalling_section);
}

void TestSceneBuilder::cutSectionOwnsItsList_data() {
	QTest::addColumn<QString>("part");
	QTest::newRow("CutEnd") << QStringLiteral("CutEnd");
	QTest::newRow("CutBegin") << QStringLiteral("CutBegin");
}

// A cut section is the only owner of the list that the cut made, and frees it with the section.
void TestSceneBuilder::cutSectionOwnsItsList() {
	QFETCH(QString, part);
	Section source;
	describeCuttableSection(source);
	Section cut(source);
	cut.CutBlockSection(part.toStdString(), 6.0);
	const WeakList cutList = cut.nodelist_of_nodes_in_signalling_section;
	// The cut section is the only owner of its list.
	QCOMPARE(cut.nodelist_of_nodes_in_signalling_section.use_count(), 1L);
	cut = Section();
	QVERIFY2(cutList.expired(), "the list of the cut section is freed with the section");
}

// The sections of a route share the lists of the network, and a copy of a section keeps its list when the network is reset.
void TestSceneBuilder::routeSectionsShareNetworkLists() {
	const auto reset = qScopeGuard([] { resetNativeInfrastructureState(defaultRun()); });
	// The scene with one route builds.
	QVERIFY(!hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), tinyScene())));
	QCOMPARE(defaultRun().infra.numBlocks, 2);
	QCOMPARE(defaultRun().infra.routes.size(), std::size_t(1));
	QCOMPARE(defaultRun().infra.routes.front().sequence_of_block_sections.size(), std::size_t(2));
	for (int index = 0; index < 2; ++index) {
		const NodeList& routeList = defaultRun().infra.routes.front().sequence_of_block_sections[index].nodelist_of_nodes_in_signalling_section;
		// A section of the route shares the list of its section of the network.
		QVERIFY(routeList != nullptr);
		QVERIFY(routeList == defaultRun().infra.sections[index].nodelist_of_nodes_in_signalling_section);
	}
	Section kept = defaultRun().infra.sections[0];
	const WeakList keptList = defaultRun().infra.sections[0].nodelist_of_nodes_in_signalling_section;
	const WeakList droppedList = defaultRun().infra.sections[1].nodelist_of_nodes_in_signalling_section;
	resetNativeInfrastructureState(defaultRun());
	// A copy of a section keeps its list after the reset.
	QVERIFY(!keptList.expired());
	QCOMPARE(kept.nodelist_of_nodes_in_signalling_section.use_count(), 1L);
	QCOMPARE(kept.total_nodes, 2);
	QCOMPARE(listedAbscissas(kept), std::vector<double>({0.0, 1.0}));
	QVERIFY2(droppedList.expired(), "the list of a section that no copy holds is freed by the reset");
}

// The infrastructure of the run. Two snapshots are equal only when the storage is the same too, because the station
// boundaries, the sections and the items of the canvas point into it.
struct InfrastructureSnapshot {
	int blocks = 0;
	int routeCount = 0;
	int trackLines = 0;
	int connectionCount = 0;
	int stationCount = 0;
	int platformCount = 0;
	std::size_t platformListSize = 0;
	std::size_t routeListSize = 0;
	std::size_t detectionSectionCount = 0;
	std::size_t infraElementCount = 0;
	std::size_t singleTrackLimitCount = 0;
	std::vector<std::string> sectionIds;
	std::vector<std::string> routeIds;
	std::string firstTrackId;
	std::string firstStationName;
	bool firstStationVirtualSignal = false;
	std::string finalStationName;
	const Section* sectionStorage = nullptr;
	const TDS* firstDetectionSection = nullptr;
	std::vector<const Section*> boundaryEntrances;
	// The first track and the buffers of its nodes and arcs, the first connection and the first station.
	const BlockSet* trackStorage = nullptr;
	const Node* trackNodes = nullptr;
	const Arc* trackArcs = nullptr;
	const Connections* connectionStorage = nullptr;
	const Stations* stationStorage = nullptr;

	bool operator==(const InfrastructureSnapshot& other) const {
		return std::tie(blocks, routeCount, trackLines, connectionCount, stationCount, platformCount, platformListSize, routeListSize,
				   detectionSectionCount, infraElementCount, singleTrackLimitCount, sectionIds, routeIds, firstTrackId,
				   firstStationName, firstStationVirtualSignal, finalStationName, sectionStorage, firstDetectionSection, boundaryEntrances,
				   trackStorage, trackNodes, trackArcs, connectionStorage, stationStorage)
			== std::tie(other.blocks, other.routeCount, other.trackLines, other.connectionCount, other.stationCount, other.platformCount,
				other.platformListSize, other.routeListSize, other.detectionSectionCount, other.infraElementCount,
				other.singleTrackLimitCount, other.sectionIds, other.routeIds, other.firstTrackId, other.firstStationName,
				other.firstStationVirtualSignal, other.finalStationName, other.sectionStorage, other.firstDetectionSection,
				other.boundaryEntrances, other.trackStorage, other.trackNodes, other.trackArcs, other.connectionStorage, other.stationStorage);
	}
};

static InfrastructureSnapshot takeInfrastructureSnapshot() {
	InfrastructureSnapshot snapshot;
	snapshot.blocks = defaultRun().infra.numBlocks;
	snapshot.routeCount = defaultRun().infra.numRoutes;
	snapshot.trackLines = defaultRun().infra.numTrackLines;
	snapshot.connectionCount = defaultRun().infra.numConnections;
	snapshot.stationCount = defaultRun().infra.numStations;
	snapshot.platformCount = defaultRun().numPlatforms;
	snapshot.platformListSize = defaultRun().platforms.size();
	snapshot.routeListSize = defaultRun().infra.routes.size();
	snapshot.detectionSectionCount = defaultRun().infra.detectionSections.size();
	snapshot.infraElementCount = defaultRun().infra.infraEvents.size();
	snapshot.singleTrackLimitCount = defaultRun().infra.singleTrackLimits.size();
	for (int index = 0; index < defaultRun().infra.numBlocks && static_cast<std::size_t>(index) < defaultRun().infra.sections.size(); ++index)
		snapshot.sectionIds.push_back(defaultRun().infra.sections[index].ID);
	for (const Route& route : defaultRun().infra.routes)
		snapshot.routeIds.push_back(route.ID);
	snapshot.firstTrackId = defaultRun().infra.blockSets[0].sceneTrackId;
	snapshot.firstStationName = defaultRun().infra.stations[0].stationName;
	snapshot.firstStationVirtualSignal = defaultRun().infra.stations[0].virtualSignal;
	snapshot.finalStationName = defaultRun().finalStation.stationName;
	snapshot.sectionStorage = defaultRun().infra.sections.data();
	snapshot.firstDetectionSection = defaultRun().infra.detectionSections.empty() ? nullptr : &defaultRun().infra.detectionSections.front();
	for (const StationBoundarySection& boundary : defaultRun().infra.stationBoundaries)
		snapshot.boundaryEntrances.push_back(boundary.entrance);
	snapshot.trackStorage = &defaultRun().infra.blockSets[0];
	snapshot.trackNodes = defaultRun().infra.blockSets[0].N.data();
	snapshot.trackArcs = defaultRun().infra.blockSets[0].member.data();
	snapshot.connectionStorage = &defaultRun().infra.connections[0];
	snapshot.stationStorage = &defaultRun().infra.stations[0];
	return snapshot;
}

// True when every station boundary points at a section of the runtime and every section, of the runtime and of the routes,
// points at detection sections of the runtime list.
static bool runtimePointersAreValid() {
	const auto isSection = [](const Section* candidate) {
		return std::any_of(defaultRun().infra.sections.begin(), defaultRun().infra.sections.end(),
			[candidate](const Section& section) { return &section == candidate; });
	};
	std::set<const TDS*> detectionSections;
	for (const TDS& detection : defaultRun().infra.detectionSections)
		detectionSections.insert(&detection);
	const auto detectionPointersAreValid = [&detectionSections](const Section& section) {
		return std::all_of(section.TDS_in_block.begin(), section.TDS_in_block.end(),
			[&detectionSections](const TDS* detection) { return detectionSections.count(detection) == 1; });
	};
	bool valid = true;
	for (const StationBoundarySection& boundary : defaultRun().infra.stationBoundaries)
		valid = valid && isSection(boundary.entrance) && (boundary.exit == nullptr || isSection(boundary.exit));
	for (const Section& section : defaultRun().infra.sections)
		valid = valid && detectionPointersAreValid(section);
	for (const Route& route : defaultRun().infra.routes)
		for (const Section& section : route.sequence_of_block_sections)
			valid = valid && detectionPointersAreValid(section);
	return valid;
}

// A track with a node at each abscissa and an arc between each two neighbours. The nodes are "<track>.<index>".
static void addNodeTrack(SceneModel& scene, const std::string& track, const std::vector<double>& abscissas) {
	scene.tracks.push_back({track});
	for (std::size_t index = 0; index < abscissas.size(); ++index) {
		const std::string node = track + "." + std::to_string(index);
		scene.nodes.push_back({node, track, abscissas[index], 0.0});
		if (index > 0)
			scene.arcs.push_back({track + ".arc." + std::to_string(index), track,
				track + "." + std::to_string(index - 1), node, 0.0, 0.0, 20.0});
	}
}

// The builder ignores an overlap of at most 1e-8 km between a block and an arc. The block "line.b1" is 1.5e-8 km long, so it has a
// span, but each of the two arcs under it overlaps it by 0.75e-8 km, and no arc is mapped to it.
static SceneModel blockBetweenShortArcsScene() {
	SceneModel scene;
	addNodeTrack(scene, "line", {0.0, 1.0, 1.0 + 0.75e-8, 1.0 + 1.5e-8, 2.0});
	scene.blocks = {{"line.b0", "line", 1.0}, {"line.b1", "line", 1.5e-8}, {"line.b2", "line", 1.0}};
	return scene;
}

// The node "line.2" lies between two arcs of 4e-9 km. No section lists it, so its platform is in no section.
static SceneModel platformBetweenShortArcsScene() {
	SceneModel scene;
	addNodeTrack(scene, "line", {0.0, 1.0, 1.0 + 4e-9, 1.0 + 8e-9, 2.0});
	scene.blocks = {{"line.b0", "line", 1.0}, {"line.b1", "line", 1.0}};
	SceneStation station;
	station.id = "station.short";
	station.name = "Short";
	station.platforms.push_back({"platform.short", {"line.2"}});
	scene.stations.push_back(station);
	return scene;
}

// A connection at the same node. The plan has a derived section for each of the two blocks that reach the node, and the builder
// derives none, because no section lists the node.
static SceneModel connectionBetweenShortArcsScene() {
	SceneModel scene;
	addNodeTrack(scene, "alpha", {0.0, 1.0, 1.0 + 4e-9, 1.0 + 8e-9, 2.0});
	addNodeTrack(scene, "beta", {3.0, 4.0});
	scene.blocks = {{"alpha.b0", "alpha", 1.0}, {"alpha.b1", "alpha", 1.0}, {"beta.b0", "beta", 1.0}};
	scene.connections.push_back({"switch.short", "alpha.2", "beta.0", false, 0.0});
	return scene;
}

// The id of the block "dependent" is part of the ids of eleven other sections, "@x0@dependent@" and so on. Setting the dependencies
// fills its ten places with sections, so the explicit dependency finds none left.
static SceneModel crowdedDependencyScene() {
	SceneModel scene;
	std::vector<double> abscissas;
	for (int index = 0; index <= 13; ++index)
		abscissas.push_back(static_cast<double>(index));
	addNodeTrack(scene, "line", abscissas);
	scene.blocks.push_back({"dependent", "line", 1.0});
	for (int index = 0; index < 11; ++index)
		scene.blocks.push_back({"x" + std::to_string(index) + "@dependent", "line", 1.0});
	scene.blocks.push_back({"target", "line", 1.0});
	scene.blockDependencies.push_back({"dependent", "target"});
	return scene;
}

// The check of the station boundaries skips an empty entrance, so the builder meets it.
static SceneModel boundaryWithoutEntranceScene() {
	SceneModel scene = tinyScene();
	scene.stationBoundaries.front().entranceBlock.clear();
	return scene;
}

// Two derived sections that meet at the end node of the block "B". The route joins them with a cut at that node, which is not inside
// the section that it cuts, so the route does not keep the section.
static SceneModel routeOverEndNodeSwitchesScene() {
	SceneModel scene;
	addNodeTrack(scene, "A", {0.0, 1.0});
	addNodeTrack(scene, "B", {2.0, 4.0});
	addNodeTrack(scene, "C", {5.0, 6.0});
	scene.blocks = {{"A", "A", 1.0}, {"B", "B", 2.0}, {"C", "C", 1.0}};
	scene.connections.push_back({"switch.ab", "A.1", "B.1", false, 0.0});
	scene.connections.push_back({"switch.bc", "B.1", "C.0", false, 0.0});
	SceneRoute route;
	route.id = "route.cut";
	route.blocks = {"@A@-1.000000/@B@-4.000000", "@B@-4.000000/@C@-5.000000"};
	scene.routes.push_back(route);
	return scene;
}

static bool hasErrorMessage(const std::vector<SceneDiagnostic>& diagnostics, const std::string& code, const std::string& message) {
	return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const SceneDiagnostic& diagnostic) {
		return diagnostic.severity == SceneSeverity::Error && diagnostic.code == code && diagnostic.message == message;
	});
}

// Keeps the messages of the failing builds out of the test output.
struct QuietLog {
	QuietLog() {
		eglog::setSink([](eglog::Category, eglog::Level, const std::string&) {});
	}
	~QuietLog() {
		eglog::setSink({});
	}
};

// A scene that passes the checks and fails while the runtime is built, with the code and the message of its error.
struct FailedBuild {
	const char* what;
	SceneModel (*scene)();
	const char* code;
	const char* message;
};

// A build that fails after it started to write the runtime leaves the infrastructure of the previous build. The builder has ten
// errors that it can add at that stage. Four cannot be reached by a scene that passes the checks, because the checks resolve the
// references of a dependency, a single-track restriction and a route as the builder does, and a route keeps all its blocks.
// The other six are below.
static const FailedBuild kFailedBuilds[] = {
	{"a block without an arc", blockBetweenShortArcsScene, "scene.native.topology.blocks",
		"Block section could not be mapped to track arcs"},
	{"a platform outside every section", platformBetweenShortArcsScene, "scene.native.binding.platform",
		"Platform node is not part of a block section"},
	{"sections that differ from the plan", connectionBetweenShortArcsScene, "scene.native.sections.mismatch",
		"Legacy connection construction produced section IDs different from the planned inventory"},
	{"a section with all its dependency places taken", crowdedDependencyScene, "scene.native.capacity",
		"Block dependency exceeds the runtime dependency capacity"},
	{"a station boundary without an entrance", boundaryWithoutEntranceScene, "scene.native.ref.unresolved",
		"Station boundary could not resolve a runtime section"},
	{"a route cut outside its section", routeOverEndNodeSwitchesScene, "scene.native.route.order",
		"Native route order does not retain the authored section IDs"},
};

void TestSceneBuilder::failedBuildKeepsPreviousRuntime_data() {
	QTest::addColumn<int>("index");
	for (int index = 0; index < static_cast<int>(std::size(kFailedBuilds)); ++index)
		QTest::newRow(kFailedBuilds[index].what) << index;
}

// Builds the scene that the previous runtime holds, then a scene that passes the checks and fails while the runtime is built.
void TestSceneBuilder::failedBuildKeepsPreviousRuntime() {
	QFETCH(int, index);
	const FailedBuild& test = kFailedBuilds[index];
	const QuietLog quiet;
	QVERIFY2(!hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), tinyScene())), "the previous scene builds");
	defaultRun().infra.stations[0].virtualSignal = true;
	const InfrastructureSnapshot before = takeInfrastructureSnapshot();
	// The previous runtime holds the scene.
	QCOMPARE(before.blocks, 2);
	QCOMPARE(before.routeCount, 1);
	QCOMPARE(before.platformCount, 2);
	QCOMPARE(before.firstStationName, "Tiny");
	QCOMPARE_GT(before.detectionSectionCount, std::size_t(0));
	QCOMPARE(before.boundaryEntrances.size(), std::size_t(1));

	const auto diagnostics = buildInfrastructureAndSignallingFromScene(defaultRun(), test.scene());
	QVERIFY2(hasErrorMessage(diagnostics, test.code, test.message),
		("the build reaches its error '" + std::string(test.message) + "'").c_str());
	QVERIFY2(before == takeInfrastructureSnapshot(), "the previous infrastructure is still in the runtime");
	QVERIFY2(runtimePointersAreValid(), "the pointers into the sections and the detection sections are valid");
}

// The failed builds again, each after the build of the previous scene, and then the build of the previous scene.
void TestSceneBuilder::buildsAgainAfterFailedBuilds() {
	const QuietLog quiet;
	for (const FailedBuild& test : kFailedBuilds) {
		buildInfrastructureAndSignallingFromScene(defaultRun(), tinyScene());
		defaultRun().infra.stations[0].virtualSignal = true;
		buildInfrastructureAndSignallingFromScene(defaultRun(), test.scene());
	}
	// The scene builds again after the failed builds.
	QVERIFY(!hasErrors(buildInfrastructureAndSignallingFromScene(defaultRun(), tinyScene())));
	QCOMPARE(defaultRun().infra.numBlocks, 2);
	QVERIFY(runtimePointersAreValid());
	resetNativeInfrastructureState(defaultRun());
}

// A locale that writes a decimal comma, as many user locales do.
struct DecimalComma : std::numpunct<char> {
	char do_decimal_point() const override { return ','; }
};

// Two blocks of 0.2 m round to the same kilometres.
static SceneModel shortBlocksScene() {
	SceneModel scene = lineScene(1);
	scene.blocks = {{"short.0", "line", 0.0002}, {"short.1", "line", 0.0002}, {"rest", "line", 0.9996}};
	return scene;
}

// Two lines whose blocks have the same id.
static SceneModel sameIdScene() {
	SceneModel scene = twoLineScene(1);
	scene.blocks.back().id = scene.blocks.front().id;
	return scene;
}

void TestSceneBuilder::labelsReadAsTrackAndKilometres() {
	const std::locale previous = std::locale::global(std::locale(std::locale::classic(), new DecimalComma));
	const auto restore = qScopeGuard([&previous] { std::locale::global(previous); });
	const SceneSectionLabels labels = sceneSectionLabels(buildSceneSectionInventory(stableConnectionScene()));
	// A block section reads as its track and kilometres, by runtime id and by block id, with a full stop in every locale.
	QCOMPARE(labels.label("@alpha.block@"), "alpha 0.000 to 1.000 km");
	QCOMPARE(labels.label("alpha.block"), "alpha 0.000 to 1.000 km");
	// A switch section reads as the piece on each track.
	QCOMPARE(labels.label("@alpha.block@-1.000000/@beta.block@-2.000000"), "alpha 0.000 to 1.000, beta 2.000 to 3.000 km");
}

// The connection ends at the end of the block of beta, so the piece on beta has one kilometre.
void TestSceneBuilder::showsRoundedKilometreOnce() {
	SceneModel atBlockEnd = stableConnectionScene();
	atBlockEnd.connections.front().fromNodeId = "beta.end";
	const SceneSectionInventory endInventory = buildSceneSectionInventory(atBlockEnd);
	const auto atEnd = std::find_if(endInventory.sections.begin(), endInventory.sections.end(),
		[](const SceneSectionDescriptor& section) { return section.connectionDerived; });
	// A piece whose ends round to the same kilometre shows it once.
	QVERIFY(atEnd != endInventory.sections.end());
	QCOMPARE(sceneSectionLabel(*atEnd), "alpha 0.000 to 1.000, beta 3.000 km");
}

void TestSceneBuilder::sharedLabelEndsWithId() {
	const SceneSectionLabels shortLabels = sceneSectionLabels(buildSceneSectionInventory(shortBlocksScene()));
	// A label that two sections share ends with the id.
	QCOMPARE(shortLabels.label("short.0"), "line 0.000 to 0.000 km (@short.0@)");
	QCOMPARE(shortLabels.label("@short.1@"), "line 0.000 to 0.000 km (@short.1@)");
	QCOMPARE(shortLabels.label("rest"), "line 0.000 to 1.000 km");
}

void TestSceneBuilder::unknownReferenceKeepsId() {
	const SceneSectionLabels labels = sceneSectionLabels(buildSceneSectionInventory(stableConnectionScene()));
	// An unknown or missing reference keeps its id.
	QCOMPARE(labels.label("no-such-block"), "no-such-block");
	QVERIFY(labels.label("").empty());
}

void TestSceneBuilder::labelWithIdNamesIdOnce() {
	const SceneSectionLabels labels = sceneSectionLabels(buildSceneSectionInventory(stableConnectionScene()));
	const SceneSectionLabels shortLabels = sceneSectionLabels(buildSceneSectionInventory(shortBlocksScene()));
	// The label with the id names the id once, and an unknown reference by its id alone.
	QCOMPARE(labels.labelWithId("@alpha.block@"), "alpha 0.000 to 1.000 km (@alpha.block@)");
	QCOMPARE(labels.labelWithId("no-such-block"), "no-such-block");
	QCOMPARE(shortLabels.labelWithId("@short.0@"), "line 0.000 to 0.000 km (@short.0@)");
}

void TestSceneBuilder::ambiguousReferenceKeepsId() {
	const SceneSectionInventory sameIdInventory = buildSceneSectionInventory(sameIdScene());
	const SceneSectionLabels sameIdLabels = sceneSectionLabels(sameIdInventory);
	// A reference that names two sections keeps its id.
	QCOMPARE(sameIdInventory.sections.size(), std::size_t(2));
	QCOMPARE(sameIdLabels.label("a.b.0"), "a.b.0");
	QCOMPARE(sameIdLabels.label("@a.b.0@"), "@a.b.0@");
}

// A label exists for exactly the references that resolve() accepts, also for a block whose id is a runtime id.
void TestSceneBuilder::labelsExactlyResolvableReferences() {
	SceneModel wrapped = twoLineScene(1);
	wrapped.blocks.back().id = "@a.b.0@";
	for (const SceneModel& scene : {stableConnectionScene(), switchChainScene(), shortBlocksScene(), sameIdScene(), wrapped}) {
		const SceneSectionInventory inventory = buildSceneSectionInventory(scene);
		const SceneSectionLabels sceneLabels = sceneSectionLabels(inventory);
		for (const SceneSectionDescriptor& section : inventory.sections)
			for (const std::string& reference : {section.id, section.sourceBlockId, section.id.substr(1, section.id.size() - 2)})
				QVERIFY2((sceneLabels.byReference.count(reference) != 0) == (inventory.resolve(reference) != nullptr),
					("the labels name the sections that resolve() finds: " + scene.name + " " + reference).c_str());
	}
}

// At level 3 the two edges of a diverging switch get one blocking time. A train runs over two crossovers, and the end of the
// first and the start of the second have the same switch name, as two switch nodes at the platform of one station on one
// block have. The start of the second crossover takes over the times of the end of the first only when it lies at the
// same position, where the end of one crossover is the start of the next.
static Train connectCrossovers(double secondStart) {
	struct Edge {
		const char* id;
		const char* switchName;
		const char* connected;
		double position;
		double startOcc;
	};
	const Edge edges[] = {
		{"first.start", "@first.start@Point", "first.end", 2145.0, 119.0},
		{"first.end", "@M-0-T2@Point", "first.start", 2159.0, 124.0},
		{"second.start", "@M-0-T2@Point", "second.end", secondStart, 120.0},
		{"second.end", "@second.end@Point", "second.start", 2210.0, 125.0},
	};
	Train train;
	for (const Edge& edge : edges) {
		BlockingTimes& time = train.BlockTime[train.N_BlockSections++];
		time.BlockID = edge.id;
		time.SwitchName = edge.switchName;
		time.ConnectedBlockingTimeID = edge.connected;
		time.SignallingLevel = levelValue(SignallingLevel::EtcsLevel3);
		time.LocationWithSwitch = true;
		time.GeoPosStart = time.GeoPosEnd = edge.position;
		time.StartOccTime = edge.startOcc;
	}
	train.N_BlockTimeComplete = train.N_BlockSections;
	train.ConnectBlockTimesOfSwitchesForTrainCentricSignalling();
	return train;
}

void TestSceneBuilder::crossoverElsewhereKeepsOwnStart() {
	const Train apart = connectCrossovers(2175.0);
	// The end of the first crossover keeps its position.
	QCOMPARE_EQ(apart.BlockTime[1].GeoPosStart, 2159.0);
	QCOMPARE_EQ(apart.BlockTime[1].GeoPosEnd, 2159.0);
	// A second crossover elsewhere keeps its own start of occupation.
	QCOMPARE_EQ(apart.BlockTime[2].GeoPosStart, 2175.0);
	QCOMPARE_EQ(apart.BlockTime[2].StartOccTime, 120.0);
	QCOMPARE_EQ(apart.BlockTime[3].StartOccTime, 120.0);
}

void TestSceneBuilder::crossoverAtSharedSwitchTakesOverStart() {
	const Train shared = connectCrossovers(2159.0);
	// The shared switch keeps its position.
	QCOMPARE_EQ(shared.BlockTime[1].GeoPosStart, 2159.0);
	QCOMPARE_EQ(shared.BlockTime[2].GeoPosStart, 2159.0);
	// A second crossover that starts where the first ends takes over its start of occupation.
	QCOMPARE_EQ(shared.BlockTime[2].StartOccTime, 119.0);
	QCOMPARE_EQ(shared.BlockTime[3].StartOccTime, 119.0);
}

void TestSceneBuilder::lastLocationHasNoClearingTime_data() {
	QTest::addColumn<int>("level");
	QTest::newRow("level 3") << levelValue(SignallingLevel::EtcsLevel3);
	QTest::newRow("level 4") << levelValue(SignallingLevel::VirtualCoupling);
}

// At levels 3 and 4 the last location of the last block section has no clearing time, because the train leaves the network
// when its head reaches it. A 100 m train runs at 20 m/s over two sections of 1 km, with one location on the first and two
// on the second. Its tail passes the location at 1200 m at 65 s and never passes the last one at 1950 m.
void TestSceneBuilder::lastLocationHasNoClearingTime() {
	QFETCH(int, level);
	struct Location {
		const char* id;
		const char* sectionId;
		double position;
	};
	const Location locations[] = {{"E1", "B1", 500.0}, {"E2", "B2", 1200.0}, {"E3", "B2", 1950.0}};
	SimulationRun& run = defaultRun();
	std::vector<Route> savedRoutes;
	savedRoutes.swap(run.infra.routes);
	std::list<InfraEvent> savedEvents;
	savedEvents.swap(run.infra.infraEvents);
	const auto restore = qScopeGuard([&] {
		run.infra.routes.swap(savedRoutes);
		run.infra.infraEvents.swap(savedEvents);
	});
	run.infra.routes.resize(1);
	Route& route = run.infra.routes[0];
	route.N_Block_Sections = 2;
	route.sequence_of_block_sections.resize(2);
	for (int s = 0; s < 2; ++s) {
		Section& section = route.sequence_of_block_sections[s];
		section.ID = "B" + std::to_string(s + 1);
		section.start_node.X = s;
		section.end_node.X = s + 1;
		section.total_arcs = 1;
		section.arcs_in_signalling_block_section[0].endNode.X = s + 1;
		section.arcs_in_signalling_block_section[0].gradient = 0.0;
		section.arcs_in_signalling_block_section[0].curvature = 0.0;
		section.SignallingLevel = level;
	}
	for (const Location& location : locations) {
		InfraElement element;
		element.ID = location.id;
		element.SectionID = location.sectionId;
		element.XCoordinate = element.GeoXCoord = location.position;
		route.InfrastructureElements.push_back(element);
		InfraEvent event;
		event.ElementInfo = element;
		event.RecordedEvent.emplace_back();
		event.RecordedEvent.back().trainDescription = "T1";
		run.infra.infraEvents.push_back(event);
	}
	Train train;
	train.run_ = &run;
	train.indexOfRoute = 0;
	train.trainDescription = "T1";
	train.train_length = 100.0;
	train.max_train_decelaration = 0.5;
	train.total_train_mass = train.massFactor = 1.0;
	train.End_Time = 100;
	for (int t = 0; t <= train.End_Time; ++t) {
		train.instant_spatial_position.push_back(20.0 * t);
		train.instant_train_speed.push_back(t == 0 ? 0.0 : 20.0);
	}
	train.ComputeBlockingTimesInMixedSignallingAreas(0, 0, 0, 0, 0, 5);
	// One blocking time per location.
	QCOMPARE(train.N_BlockSections, 3);
	// The last location of the last section has no clearing time.
	QCOMPARE_EQ(train.BlockTime[2].EndClearTime, train.BlockTime[2].EndRunTime);
	QCOMPARE_EQ(train.BlockTime[2].clearingTime, 0.0);
	// The location before it on the last section is cleared when the tail passes.
	QCOMPARE_EQ(train.BlockTime[1].EndClearTime, 65.0);
	QCOMPARE_GT(train.BlockTime[1].clearingTime, 0.0);
}

QTEST_APPLESS_MAIN(TestSceneBuilder)
#include "test_scenebuilder.moc"
