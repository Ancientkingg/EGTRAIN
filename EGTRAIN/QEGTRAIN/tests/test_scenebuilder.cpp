#include "scene/SceneModel.h"
#include "scene/SceneValidator.h"
#include "scene/SectionInventory.h"
#include "simulation/Signalling.h"
#include "util/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

Logger owl;

static bool expect(bool condition, const std::string& message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
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
	scene.signals.push_back({"block.a"});
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

static bool runTinyBuilderChecks() {
	bool ok = true;
	SceneModel scene = tinyScene();
	auto diagnostics = buildInfrastructureAndSignallingFromScene(scene);
	ok &= expect(!hasErrors(diagnostics), "complete scene builds without errors");
	ok &= expect(numTrackLines == 1 && blockSets[0].numNodes == 3 && blockSets[0].arcs == 2,
		"track nodes and arcs retain canonical topology");
	ok &= expect(blockSets[0].hasGraphLayout && blockSets[0].graphID == -1
			&& blockSets[0].region == 2,
		"authored track layout retains a valid negative display level");
	ok &= expect(Blocks == 2, "two canonical blocks become two straight runtime sections");
	ok &= expect(signalling_block_sections[0].ID == "@block.a@"
			&& signalling_block_sections[1].ID == "@block.b@",
		"block IDs use the runtime boundary form");
	ok &= expect(numStations == 2 && numAllStationPlatforms == 2, "station and platform counts are bound");
	ok &= expect(StationArray[0].X == 1.0, "platform node anchors a station without a separate position");
	ok &= expect(StationArray[0].latitude == 1.0 && StationArray[0].longitude == 0.1
			&& StationArray[0].regions == std::vector<int>({2})
			&& StationArray[0].regionX[2] == 1.0
			&& StationArray[0].corridors == std::vector<std::string>({"corridor.tiny"}),
		"authored station layout reaches the runtime station model");
	if (!AllStationPlatforms.empty()) {
		const auto& platform = AllStationPlatforms.front();
		ok &= expect(platform.ID == "platform.1" && platform.StationID == "station.tiny",
			"platform retains canonical station binding");
		ok &= expect(platform.BlockSectionID == "@block.a@", "platform resolves to its canonical block section");
	}
	if (AllStationPlatforms.size() == 2) {
		const auto& platform = AllStationPlatforms.back();
		ok &= expect(platform.ID == "platform.2" && platform.StationID == "station.end"
				&& platform.BlockSectionID == "@block.b@",
			"destination platform resolves on the same authored route");
	}
	ok &= expect(N_Routes == 1 && train_route.size() == 1, "one native route is available");
	if (!train_route.empty()) {
		const Route& route = train_route.front();
		ok &= expect(route.ID == "route.tiny" && route.corridor == "corridor.tiny",
			"route retains canonical ID and corridor");
		ok &= expect(route.N_Block_Sections == 2
				&& route.sequence_of_block_sections[0].ID == "@block.a@"
				&& route.sequence_of_block_sections[1].ID == "@block.b@",
			"route retains every canonical block reference");
	}
	ok &= expect(signalling_block_sections[0].N_ConnectedBS == 1
			&& signalling_block_sections[0].IDConnectedBS[0] == "@block.b@",
		"explicit block dependency is applied without a hard-coded case dependency");
	ok &= expect(singleTrackLimits.size() == 1
			&& std::get<0>(singleTrackLimits.front()) == "@block.a@"
			&& std::get<2>(singleTrackLimits.front()) == "@block.a@",
		"single-track references resolve to runtime block IDs");
	ok &= expect(stationBoundarySections.size() == 1
			&& stationBoundarySections.front().entrance->ID == "@block.a@"
			&& stationBoundarySections.front().exit->ID == "@block.b@",
		"station boundary references resolve to runtime sections");
	const int blocksBeforeReservedId = Blocks;
	const std::string firstSectionBeforeReservedId = signalling_block_sections[0].ID;
	SceneModel reservedBlockId = tinyScene();
	reservedBlockId.blocks[0].id = "Depot/1";
	reservedBlockId.routes[0].blocks[0] = "Depot/1";
	reservedBlockId.blockDependencies[0].block = "Depot/1";
	reservedBlockId.singleTrackRestrictions[0].startBlock = "Depot/1";
	reservedBlockId.singleTrackRestrictions[0].protectedStartBlock = "Depot/1";
	reservedBlockId.stationBoundaries[0].entranceBlock = "Depot/1";
	diagnostics = buildInfrastructureAndSignallingFromScene(reservedBlockId);
	ok &= expect(hasDiagnostic(diagnostics, SceneSeverity::Error, "infrastructure.json", "id.reserved")
			&& Blocks == blocksBeforeReservedId
			&& signalling_block_sections[0].ID == firstSectionBeforeReservedId,
		"reserved block-id delimiters are rejected before native mutation");

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
	segmentedRegionRoute.importReport.push_back({"legacy_root"});
	diagnostics = buildInfrastructureAndSignallingFromScene(segmentedRegionRoute);
	ok &= expect(!hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "route.direction")
			&& N_Routes == 1 && train_route.size() == 1 && !train_route[0].reversed_direction,
		"native route direction follows the first connected legacy regional segment");

	diagnostics = buildInfrastructureAndSignallingFromScene(stableConnectionScene());
	ok &= expect(!hasErrors(diagnostics) && Blocks == 3, "stable-ID connection scene builds one switch section");
	if (!hasErrors(diagnostics) && Blocks == 3) {
		const Section& source = signalling_block_sections[0];
		ok &= expect(source.ID == "@alpha.block@" && source.N_ConnectedBS == 1,
			"switch dependency is derived without numeric track naming");
		ok &= expect(!source.arcs_in_signalling_block_section[0].endNode.IDConnectedBlocks.empty()
				&& source.arcs_in_signalling_block_section[0].endNode.IDConnectedBlocks.front()
					== "@alpha.block@-1.000000/@beta.block@-2.000000",
			"connection nodes resolve switch sections through stable runtime references");
		bool hasCanonicalSwitchSpeed = false;
		for (int index = 0; index < signalling_block_sections[2].total_arcs; ++index)
			hasCanonicalSwitchSpeed = hasCanonicalSwitchSpeed
				|| signalling_block_sections[2].arcs_in_signalling_block_section[index].speedLimit == 7.5;
		ok &= expect(hasCanonicalSwitchSpeed, "connection speed is retained independently of endpoint order");
		Section creatorNamedSwitch = signalling_block_sections[2];
		creatorNamedSwitch.ID = "@creator@main-block@-1.000000/@creator-yard@block@-2.000000";
		creatorNamedSwitch.FirstConnectedTrackLineID = 0;
		creatorNamedSwitch.SecondConnectedTrackLineID = 1;
		BlocksOccupied.clear();
		BlocksConnected.clear();
		activateBlocksWithSwitchesDivFixedBlock(creatorNamedSwitch, 0, -1.0);
		ok &= expect(creatorNamedSwitch.ID
					== "@creator@main-block@-1.000000/@creator-yard@block@-2.000000"
				&& std::find(BlocksOccupied.begin(), BlocksOccupied.end(), "@creator@main-block@")
					!= BlocksOccupied.end()
				&& std::find(BlocksOccupied.begin(), BlocksOccupied.end(), "@creator-yard@block@")
					!= BlocksOccupied.end(),
			"switch occupation preserves creator section IDs containing hyphens and wrapper characters");
		BlocksOccupied = {"occupied.before"};
		BlocksConnected = {"connected.before"};
		const auto occupiedBefore = BlocksOccupied;
		const auto connectedBefore = BlocksConnected;
		Section malformedSwitch;
		malformedSwitch.ID = "malformed/switch";
		Section malformedPrevious;
		malformedPrevious.ID = "also-malformed/switch";
		occupyDoubleSwitch(malformedSwitch, malformedPrevious);
		ok &= expect(BlocksOccupied == occupiedBefore && BlocksConnected == connectedBefore,
			"malformed double-switch identities are rejected before occupancy mutation");
		releaseDoubleSwitch(malformedSwitch, malformedPrevious);
		ok &= expect(BlocksOccupied == occupiedBefore && BlocksConnected == connectedBefore,
			"malformed double-switch identities are rejected before release mutation");
		occupyDoubleSwitch(signalling_block_sections[2], malformedPrevious);
		releaseDoubleSwitch(signalling_block_sections[2], malformedPrevious);
		ok &= expect(BlocksOccupied == occupiedBefore && BlocksConnected == connectedBefore,
			"a valid first switch half cannot mutate before a malformed second half is rejected");
		Section unresolvedSwitch;
		unresolvedSwitch.ID = "@missing.a@-1.000000/@missing.b@-2.000000";
		unresolvedSwitch.withSwitchDiv = true;
		Section unresolvedPrevious;
		unresolvedPrevious.ID = "@missing.c@-3.000000/@missing.d@-4.000000";
		unresolvedPrevious.withSwitchDiv = true;
		occupyDoubleSwitch(unresolvedSwitch, unresolvedPrevious);
		ok &= expect(BlocksOccupied == occupiedBefore && BlocksConnected == connectedBefore,
			"unresolved double-switch branches are rejected before occupancy mutation");
		releaseDoubleSwitch(unresolvedSwitch, unresolvedPrevious);
		ok &= expect(BlocksOccupied == occupiedBefore && BlocksConnected == connectedBefore,
			"unresolved double-switch branches are rejected before release mutation");
		BlocksOccupied.clear();
		BlocksConnected.clear();
	}
	const int blocksBeforeDisconnectedRoute = Blocks;
	const std::string firstSectionBeforeDisconnectedRoute = signalling_block_sections[0].ID;
	SceneModel disconnectedRoute = tinyScene();
	disconnectedRoute.routes.front().blocks = {"block.a", "block.a"};
	diagnostics = buildInfrastructureAndSignallingFromScene(disconnectedRoute);
	ok &= expect(hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "route.disconnected")
			&& Blocks == blocksBeforeDisconnectedRoute
			&& signalling_block_sections[0].ID == firstSectionBeforeDisconnectedRoute,
		"direct native-builder callers reject disconnected routes before runtime mutation");
	SceneModel epsilonConnection = stableConnectionScene();
	epsilonConnection.nodes[2].xKm = 1.0 + 5e-9;
	epsilonConnection.nodes[3].xKm = 2.0 + 5e-9;
	const SceneSectionInventory epsilonInventory = buildSceneSectionInventory(epsilonConnection);
	diagnostics = buildInfrastructureAndSignallingFromScene(epsilonConnection);
	ok &= expect(!hasErrors(diagnostics) && Blocks == 3
			&& epsilonInventory.sections.size() == 3
			&& signalling_block_sections[2].ID == epsilonInventory.sections[2].id,
		"sub-tolerance connection spacing retains inventory and native section-ID parity");
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
	derivedUTurn.importReport.push_back({"legacy_root"});
	derivedUTurn.routes.push_back({"switch-u-turn", {aToB, bToC, aToB}, false, {}, false});
	const int blocksBeforeDerivedUTurn = Blocks;
	const std::string firstSectionBeforeDerivedUTurn = signalling_block_sections[0].ID;
	diagnostics = buildInfrastructureAndSignallingFromScene(derivedUTurn);
	ok &= expect(!aToB.empty() && !bToC.empty()
			&& hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "route.direction")
			&& Blocks == blocksBeforeDerivedUTurn
			&& signalling_block_sections[0].ID == firstSectionBeforeDerivedUTurn,
		"legacy provenance cannot hide a connection-derived U-turn from native preflight");
	SceneModel mixedDerivedUTurn = derivedUTurn;
	mixedDerivedUTurn.routes = {{"mixed-switch-u-turn", {"b.left", bToC, aToB}, false, {}, false}};
	diagnostics = buildInfrastructureAndSignallingFromScene(mixedDerivedUTurn);
	ok &= expect(hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "route.direction")
			&& Blocks == blocksBeforeDerivedUTurn
			&& signalling_block_sections[0].ID == firstSectionBeforeDerivedUTurn,
		"mixed route evidence cannot hide a legacy derived U-turn before native mutation");
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
	legacyFork.importReport.push_back({"legacy_root"});
	legacyFork.routes.push_back({"legacy-fork", {bToA, cToA}, false, {}, false});
	const int blocksBeforeLegacyFork = Blocks;
	diagnostics = buildInfrastructureAndSignallingFromScene(legacyFork);
	ok &= expect(!bToA.empty() && !cToA.empty()
			&& hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "route.disconnected")
			&& Blocks == blocksBeforeLegacyFork,
		"legacy regional compatibility rejects a wrong-branch switch fork before runtime mutation");
	diagnostics = buildInfrastructureAndSignallingFromScene(signallingAreasScene());
	ok &= expect(!hasErrors(diagnostics) && Blocks == 3,
		"signalling areas apply before route construction");
	if (!hasErrors(diagnostics) && Blocks == 3) {
		const Section& alpha = signalling_block_sections[0];
		const Section& beta = signalling_block_sections[1];
		const Section& derived = signalling_block_sections[2];
		ok &= expect(alpha.SignallingLevel == 3 && beta.SignallingLevel == 1
				&& derived.SignallingLevel == 3,
			"network and track-scoped levels reach base and derived switch sections");
		ok &= expect(!train_route.empty() && train_route.front().N_Block_Sections == 1
				&& train_route.front().sequence_of_block_sections[0].SignallingLevel == 3,
			"signalling level is copied into the derived route section");
	}
	const int blocksBeforeConflict = Blocks;
	std::vector<std::string> sectionIdsBeforeConflict;
	for (int index = 0; index < Blocks; ++index)
		sectionIdsBeforeConflict.push_back(signalling_block_sections[index].ID);
	SceneModel conflictingAreas = signallingAreasScene();
	conflictingAreas.signallingAreas.push_back({"beta-area", 0.0, 3.0, 4, "beta"});
	conflictingAreas.tracks.push_back({"gamma"});
	conflictingAreas.nodes.push_back({"gamma.start", "gamma", 4.0, 0.0});
	conflictingAreas.nodes.push_back({"gamma.end", "gamma", 5.0, 0.0});
	conflictingAreas.arcs.push_back({"gamma.arc", "gamma", "gamma.start", "gamma.end", 0.0, 0.0, 10.0});
	conflictingAreas.blocks.push_back({"gamma.block", "gamma", 1.0});
	diagnostics = buildInfrastructureAndSignallingFromScene(conflictingAreas);
	bool sectionIdsUnchanged = Blocks == blocksBeforeConflict;
	for (std::size_t index = 0; sectionIdsUnchanged && index < sectionIdsBeforeConflict.size(); ++index)
		sectionIdsUnchanged = signalling_block_sections[index].ID == sectionIdsBeforeConflict[index];
	ok &= expect(hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "signalling_area.conflict")
			&& sectionIdsUnchanged,
		"same-tier conflicting track areas are rejected before replacing the prior runtime");
	SceneModel invalidSwitchReference = stableConnectionScene();
	invalidSwitchReference.blockDependencies.push_back(
		{"alpha.block", "@alpha.block@-9.000000/@beta.block@-10.000000"});
	diagnostics = buildInfrastructureAndSignallingFromScene(invalidSwitchReference);
	ok &= expect(hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "ref.unresolved")
			&& Blocks == 3 && signalling_block_sections[0].ID == "@alpha.block@",
		"invalid switch references are rejected before replacing the existing runtime");
	SceneModel duplicateSwitchSection = stableConnectionScene();
	duplicateSwitchSection.connections.push_back(
		{"switch.duplicate", "alpha.end", "beta.start", true, 7.5});
	diagnostics = buildInfrastructureAndSignallingFromScene(duplicateSwitchSection);
	ok &= expect(hasDiagnostic(diagnostics, SceneSeverity::Error, "infrastructure.json", "id.duplicate")
			&& Blocks == 3 && signalling_block_sections[0].ID == "@alpha.block@",
		"duplicate switch sections are rejected before fixed-capacity runtime mutation");
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
	diagnostics = buildInfrastructureAndSignallingFromScene(oversizedSwitchSection);
	ok &= expect(hasDiagnostic(diagnostics, SceneSeverity::Error, "infrastructure.json", "capacity")
			&& Blocks == 3 && signalling_block_sections[0].ID == "@alpha.block@",
		"derived switch arc capacity is rejected before runtime mutation");
	SceneModel runtimeBlockIdCollision = scene;
	runtimeBlockIdCollision.blocks[1].id = "@block.a@";
	runtimeBlockIdCollision.routes.front().blocks[1] = "@block.a@";
	runtimeBlockIdCollision.blockDependencies.front().dependsOn = "@block.a@";
	runtimeBlockIdCollision.singleTrackRestrictions.front().endBlock = "@block.a@";
	runtimeBlockIdCollision.singleTrackRestrictions.front().protectedEndBlock = "@block.a@";
	runtimeBlockIdCollision.stationBoundaries.front().exitBlock = "@block.a@";
	diagnostics = buildInfrastructureAndSignallingFromScene(runtimeBlockIdCollision);
	ok &= expect(hasDiagnostic(diagnostics, SceneSeverity::Error, "infrastructure.json", "id.duplicate")
			&& Blocks == 3 && signalling_block_sections[0].ID == "@alpha.block@",
		"canonical block IDs that collapse to one runtime ID are rejected before mutation");
	SceneModel excessiveDependencies = scene;
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
	diagnostics = buildInfrastructureAndSignallingFromScene(excessiveDependencies);
	ok &= expect(hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "capacity")
			&& Blocks == 3 && signalling_block_sections[0].ID == "@alpha.block@",
		"dependency overflow is rejected before replacing the existing runtime");

	SceneModel explicitReversed = scene;
	explicitReversed.routes.front().reversed = true;
	diagnostics = buildInfrastructureAndSignallingFromScene(explicitReversed);
	ok &= expect(!hasErrors(diagnostics) && train_route.size() == 1
			&& train_route.front().reversed_direction
			&& train_route.front().OriginalRefReversedRoute == train_route.front().x_of_end_node * 1000.0,
		"explicit reverse metadata retains the joined-route reference coordinate");

	SceneModel descendingRoute = scene;
	descendingRoute.routes.front().blocks = {"block.b", "block.a"};
	diagnostics = buildInfrastructureAndSignallingFromScene(descendingRoute);
	ok &= expect(!hasErrors(diagnostics) && train_route.size() == 1
			&& train_route.front().reversed_direction,
		"descending canonical block order retains the runtime reverse direction");
	diagnostics = buildInfrastructureAndSignallingFromScene(scene);
	ok &= expect(!hasErrors(diagnostics), "the native runtime can be rebuilt safely");
	SceneModel regionalDirection = stableConnectionScene();
	regionalDirection.nodes[2].xKm = 0.0;
	regionalDirection.nodes[3].xKm = 1.0;
	regionalDirection.routes.push_back(
		{"route.regional", {"alpha.block", "beta.block"}, false, {}, false});
	diagnostics = buildInfrastructureAndSignallingFromScene(regionalDirection);
	ok &= expect(!hasErrors(diagnostics) && train_route.size() == 1
			&& !train_route.front().reversed_direction
			&& train_route.front().sequence_of_block_sections[0].ID == "@alpha.block@"
			&& train_route.front().sequence_of_block_sections[1].ID == "@beta.block@",
		"declared route topology controls direction across regional coordinate references");

	const int blocksBeforeInvalid = Blocks;
	SceneModel invalidReference = scene;
	invalidReference.routes.front().blocks = {"missing.block"};
	diagnostics = buildInfrastructureAndSignallingFromScene(invalidReference);
	ok &= expect(hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "ref.unresolved"),
		"unknown route block returns an actionable signalling diagnostic");
	ok &= expect(Blocks == blocksBeforeInvalid && train_route.size() == 1,
		"invalid route does not mutate or silently truncate the existing runtime");

	const int routesBeforeMalformed = N_Routes;
	const std::string routeIdBeforeMalformed = train_route.front().ID;
	const int routeBlocksBeforeMalformed = train_route.front().N_Block_Sections;
	SceneModel malformedReference = scene;
	malformedReference.routes.front().blocks = {"@block.a@-0.500000"};
	diagnostics = buildInfrastructureAndSignallingFromScene(malformedReference);
	ok &= expect(hasDiagnostic(diagnostics, SceneSeverity::Error, "signalling.json", "ref.unresolved")
			&& Blocks == blocksBeforeInvalid && N_Routes == routesBeforeMalformed
			&& train_route.size() == 1 && train_route.front().ID == routeIdBeforeMalformed
			&& train_route.front().N_Block_Sections == routeBlocksBeforeMalformed,
		"malformed decorated route references are rejected before replacing the existing runtime");

	diagnostics = buildInfrastructureAndSignallingFromScene(multiRegionRouteScene());
	ok &= expect(!hasErrors(diagnostics) && N_Routes == 1 && train_route.size() == 1,
		"multi-region route builds with a multi-arc middle block");
	if (!hasErrors(diagnostics) && train_route.size() == 1 && train_route.front().N_Block_Sections == 3) {
		ok &= expect(train_route.front().sequence_of_block_sections[0].ID == "@a.block@"
				&& train_route.front().sequence_of_block_sections[1].ID == "@b.block@"
				&& train_route.front().sequence_of_block_sections[2].ID == "@c.block@",
			"native route construction retains authored order across coordinate regions");
		const Section& later = train_route.front().sequence_of_block_sections[1];
		const bool kilometerEndpoints = later.total_arcs == 2
			&& std::fabs(later.arcs_in_signalling_block_section[0].startNode.X - 1.0) < 1e-9
			&& std::fabs(later.arcs_in_signalling_block_section[0].endNode.X - 2.0) < 1e-9
			&& std::fabs(later.arcs_in_signalling_block_section[1].startNode.X - 2.0) < 1e-9
			&& std::fabs(later.arcs_in_signalling_block_section[1].endNode.X - 3.0) < 1e-9;
		ok &= expect(kilometerEndpoints,
			"route normalization keeps native multi-arc lengths in kilometres");
	}

	const int blocksBeforeInvalidTopology = Blocks;
	SceneModel invalidTopology = scene;
	invalidTopology.arcs.front().toNodeId = "missing.node";
	diagnostics = buildInfrastructureAndSignallingFromScene(invalidTopology);
	ok &= expect(hasDiagnostic(diagnostics, SceneSeverity::Error, "infrastructure.json", "ref.unresolved"),
		"unknown arc endpoint returns an actionable infrastructure diagnostic");
	ok &= expect(Blocks == blocksBeforeInvalidTopology, "invalid topology is rejected before runtime mutation");
	SceneModel partialArea = tinyScene();
	partialArea.signallingAreas = {{"partial-area", 0.0, 1.5, 2, {}}};
	diagnostics = buildInfrastructureAndSignallingFromScene(partialArea);
	ok &= expect(!hasErrors(diagnostics) && Blocks == 2, "partial signalling area scene builds");
	if (!hasErrors(diagnostics) && Blocks == 2) {
		constexpr int unsetLevel = -99999999;
		ok &= expect(signalling_block_sections[0].SignallingLevel == 2
				&& signalling_block_sections[1].SignallingLevel == unsetLevel,
			"only sections inside an area get a level, the others keep the unset default");
	}
	return ok;
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
	for (int index = 0; index < Blocks; ++index)
		levels[signalling_block_sections[index].ID] = signalling_block_sections[index].SignallingLevel;
	return levels;
}

static std::map<std::string, int> builtRouteLevels(std::size_t routeIndex) {
	std::map<std::string, int> levels;
	if (routeIndex >= train_route.size())
		return levels;
	const Route& route = train_route[routeIndex];
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
	const auto diagnostics = buildInfrastructureAndSignallingFromScene(scene);
	built = !hasErrors(diagnostics) && static_cast<std::size_t>(Blocks) == count;
	return levelsOfLine("", count);
}

static bool lineLevelsAre(const SceneModel& scene, const std::vector<int>& expected, const std::string& message) {
	bool built = false;
	const std::vector<int> levels = buildLineLevels(scene, expected.size(), built);
	return expect(built && levels == expected, message);
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
		const std::size_t end = list.find(" (track");
		if (end != std::string::npos)
			list.resize(end);
		for (std::size_t position = 0; position < list.size();) {
			std::size_t comma = list.find(", ", position);
			if (comma == std::string::npos)
				comma = list.size();
			names.insert(list.substr(position, comma - position));
			position = comma + 2;
		}
	}
	return names;
}

// The route sections that the built runtime left without a level.
static std::set<std::string> builtUncoveredRouteSections() {
	std::set<std::string> ids;
	for (std::size_t route = 0; route < train_route.size(); ++route) {
		for (const auto& [id, level] : builtRouteLevels(route))
			if (level == kUnsetLevel)
				ids.insert(id);
	}
	return ids;
}

static bool driftGuard(const SceneModel& scene, std::size_t uncovered, const std::string& what) {
	bool complete = false;
	const std::set<std::string> named = namedUncoveredSections(scene, complete);
	const auto diagnostics = buildInfrastructureAndSignallingFromScene(scene);
	const std::set<std::string> unset = builtUncoveredRouteSections();
	return expect(!hasErrors(diagnostics) && complete && named == unset && unset.size() == uncovered, what);
}

static bool runAreaMappingChecks() {
	bool ok = true;
	using Levels = std::vector<int>;
	const int u = kUnsetLevel;

	// Network-wide areas and containment.
	SceneModel scene = lineScene(4);
	scene.signallingAreas = {{"all", 0.0, 4.0, 2, {}}};
	ok &= lineLevelsAre(scene, {2, 2, 2, 2}, "a network-wide area gives every section its level");
	if (!hasErrors(buildInfrastructureAndSignallingFromScene(scene)))
		ok &= expect(builtRouteLevels(0) == builtSectionLevels() && builtRouteLevels(0).size() == 4,
			"the route sections carry the levels of the same sections");
	scene.signallingAreas = {{"head", 0.0, 2.0, 3, {}}};
	ok &= lineLevelsAre(scene, {3, 3, u, u}, "a section that ends at the area end is inside");
	scene.signallingAreas = {{"head", 0.0, 2.5, 3, {}}};
	ok &= lineLevelsAre(scene, {3, 3, u, u}, "a section that crosses the area end is not inside");
	scene.signallingAreas = {{"tail", 0.5, 4.0, 3, {}}};
	ok &= lineLevelsAre(scene, {u, 3, 3, 3}, "a section that crosses the area start is not inside");
	scene.signallingAreas = {{"middle", 1.0, 3.0, 5, {}}};
	ok &= lineLevelsAre(scene, {u, 5, 5, u}, "an area from one section edge to another covers the sections between");
	scene.signallingAreas = {{"adjacent-a", 0.0, 2.0, 1, {}}, {"adjacent-b", 2.0, 4.0, 4, {}}};
	ok &= lineLevelsAre(scene, {1, 1, 4, 4}, "adjacent areas that meet on a section edge split the sections");
	scene.signallingAreas = {{"adjacent-a", 0.0, 2.5, 1, {}}, {"adjacent-b", 2.5, 4.0, 4, {}}};
	ok &= lineLevelsAre(scene, {1, 1, u, 4}, "a section that crosses the edge between two areas stays unset");
	scene.signallingAreas.push_back({"bridge", 1.5, 3.5, 4, {}});
	ok &= lineLevelsAre(scene, {1, 1, 4, 4}, "a third area that contains the straddling section decides it");

	// Edge tolerance of 1e-8 km on both edges.
	scene.signallingAreas = {{"end", 0.0, 2.0 - 5e-9, 3, {}}};
	ok &= lineLevelsAre(scene, {3, 3, u, u}, "an area end just inside the tolerance still contains the section");
	scene.signallingAreas = {{"end", 0.0, 2.0 - 1e-7, 3, {}}};
	ok &= lineLevelsAre(scene, {3, u, u, u}, "an area end outside the tolerance does not contain the section");
	scene.signallingAreas = {{"start", 1.0 + 5e-9, 4.0, 3, {}}};
	ok &= lineLevelsAre(scene, {u, 3, 3, 3}, "an area start just inside the tolerance still contains the section");
	scene.signallingAreas = {{"start", 1.0 + 1e-7, 4.0, 3, {}}};
	ok &= lineLevelsAre(scene, {u, u, 3, 3}, "an area start outside the tolerance does not contain the section");

	// An area that covers no section changes nothing.
	scene.signallingAreas = {{"inside-one", 1.2, 1.8, 5, {}}};
	ok &= lineLevelsAre(scene, {u, u, u, u}, "an area inside one section covers no section");
	scene.signallingAreas = {{"beyond", 10.0, 12.0, 5, {}}};
	ok &= lineLevelsAre(scene, {u, u, u, u}, "an area beyond the track covers no section");
	scene.signallingAreas = {{"all", 0.0, 4.0, 2, {}}, {"inside-one", 1.2, 1.8, 5, {}},
		{"beyond", 10.0, 12.0, 5, {}}};
	ok &= lineLevelsAre(scene, {2, 2, 2, 2}, "areas that cover no section leave the other areas as they are");

	// Track scope overrides the network-wide level on its own track.
	SceneModel tracks = twoLineScene(3);
	tracks.signallingAreas = {{"all", 0.0, 3.0, 1, {}}, {"track-a", 0.0, 3.0, 4, "line.a"}};
	bool built = !hasErrors(buildInfrastructureAndSignallingFromScene(tracks)) && Blocks == 6;
	ok &= expect(built && levelsOfLine("a.", 3) == Levels({4, 4, 4}) && levelsOfLine("b.", 3) == Levels({1, 1, 1}),
		"a track-scoped area overrides the network-wide area on its track only");
	tracks.signallingAreas = {{"all", 0.0, 3.0, 1, {}}, {"track-b-head", 0.0, 1.0, 5, "line.b"}};
	built = !hasErrors(buildInfrastructureAndSignallingFromScene(tracks)) && Blocks == 6;
	ok &= expect(built && levelsOfLine("a.", 3) == Levels({1, 1, 1}) && levelsOfLine("b.", 3) == Levels({5, 1, 1}),
		"a track-scoped area overrides only the sections it contains");
	tracks.signallingAreas = {{"track-a", 0.0, 3.0, 4, "line.a"}};
	built = !hasErrors(buildInfrastructureAndSignallingFromScene(tracks)) && Blocks == 6;
	ok &= expect(built && levelsOfLine("a.", 3) == Levels({4, 4, 4}) && levelsOfLine("b.", 3) == Levels({u, u, u}),
		"a track-scoped area leaves the other tracks without a level");

	// A route used in reverse has the same levels on its sections.
	SceneModel forward = lineScene(4);
	forward.signallingAreas = {{"low", 0.0, 2.0, 1, {}}, {"high", 2.0, 3.0, 4, {}}};
	SceneModel reverse = lineScene(4, true);
	reverse.signallingAreas = forward.signallingAreas;
	bool forwardBuilt = !hasErrors(buildInfrastructureAndSignallingFromScene(forward)) && train_route.size() == 1
		&& !train_route.front().reversed_direction;
	const std::map<std::string, int> forwardLevels = builtRouteLevels(0);
	bool reverseBuilt = !hasErrors(buildInfrastructureAndSignallingFromScene(reverse)) && train_route.size() == 1
		&& train_route.front().reversed_direction;
	const std::map<std::string, int> reverseLevels = builtRouteLevels(0);
	ok &= expect(forwardBuilt && reverseBuilt && forwardLevels.size() == 4
			&& forwardLevels.at("@b.0@") == 1 && forwardLevels.at("@b.2@") == 4
			&& forwardLevels.at("@b.3@") == u,
		"the forward route carries the levels of its sections");
	ok &= expect(reverseBuilt && reverseLevels == forwardLevels,
		"a route used in reverse has the same levels on its sections");
	ok &= expect(reverseBuilt && reverseLevels == builtSectionLevels(),
		"the reversed route sections equal the built sections by ID");

	// The validator's missing-level warning names the route sections that the build leaves without a level.
	SceneModel drift = lineScene(5);
	drift.signallingAreas = {{"head", 0.0, 3.0, 2, {}}};
	ok &= driftGuard(drift, 2, "the warning names the route sections left without a level");
	drift.signallingAreas = {{"head", 0.0, 2.5, 2, {}}};
	ok &= driftGuard(drift, 3, "the warning and the build agree on a section that crosses the area end");
	drift.signallingAreas = {{"head", 0.0, 3.0 - 5e-9, 2, {}}};
	ok &= driftGuard(drift, 2, "the warning and the build agree inside the edge tolerance");
	drift.signallingAreas = {{"head", 0.0, 3.0 - 1e-7, 2, {}}};
	ok &= driftGuard(drift, 3, "the warning and the build agree outside the edge tolerance");
	drift.signallingAreas = {{"tail", 1.0 + 1e-7, 5.0, 2, {}}};
	ok &= driftGuard(drift, 2, "the warning and the build agree at the area start");
	drift.signallingAreas.clear();
	ok &= driftGuard(drift, 5, "the warning and the build agree when there is no area");
	drift.signallingAreas = {{"all", 0.0, 5.0, 2, {}}};
	ok &= driftGuard(drift, 0, "the warning and the build agree when every section is covered");
	drift.signallingAreas = {{"head", 0.0, 3.0, 2, {}}};
	std::reverse(drift.routes.front().blocks.begin(), drift.routes.front().blocks.end());
	ok &= driftGuard(drift, 2, "the warning and the build agree on a reversed route");
	SceneModel scoped = twoLineScene(3);
	scoped.signallingAreas = {{"track-a", 0.0, 3.0, 4, "line.a"}};
	ok &= driftGuard(scoped, 3, "the warning and the build agree on a track-scoped area");
	scoped.signallingAreas = {{"all", 0.0, 3.0, 1, {}}, {"track-b-head", 0.0, 2.0, 5, "line.b"}};
	ok &= driftGuard(scoped, 0, "the warning and the build agree when a track-scoped area adds to a network-wide one");
	scoped.signallingAreas = {{"cover", 0.0, 2.0, 1, {}}, {"track-b-tail", 1.0 + 1e-7, 3.0, 5, "line.b"}};
	ok &= driftGuard(scoped, 1, "the warning and the build agree when the track-scoped area misses a start edge");
	return ok;
}

static bool runLongTrackChecks() {
	bool ok = true;
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
	const auto diagnostics = buildInfrastructureAndSignallingFromScene(scene);
	ok &= expect(!hasErrors(diagnostics), "a track with more than 1500 nodes and arcs builds");
	ok &= expect(numTrackLines == 1 && blockSets[0].numNodes == arcCount + 1 && blockSets[0].arcs == arcCount
			&& blockSets[0].len == arcCount,
		"the long track keeps all nodes and arcs");
	ok &= expect(blockSets[0].N.size() == static_cast<std::size_t>(arcCount + 1)
			&& blockSets[0].A.size() == static_cast<std::size_t>(arcCount)
			&& blockSets[0].member.size() == static_cast<std::size_t>(arcCount),
		"the track buffers are sized from the scene");
	if (blockSets[0].member.size() == static_cast<std::size_t>(arcCount)) {
		ok &= expect(blockSets[0].member.front().startNode.X == 0.0
				&& blockSets[0].member.back().endNode.X == static_cast<double>(arcCount),
			"the long track keeps its first and last coordinates");
		ok &= expect(blockSets[0].A.back().endNode.X == static_cast<double>(arcCount),
			"the long track keeps its arcs past the former limit");
	}
	ok &= expect(Blocks == arcCount / arcsPerBlock, "the long track yields one section per block");
	resetNativeInfrastructureState();
	ok &= expect(blockSets[0].N.capacity() == 0 && blockSets[0].A.capacity() == 0
			&& blockSets[0].member.capacity() == 0,
		"the runtime reset releases the track buffers");
	return ok;
}

static bool runManySectionsChecks() {
	bool ok = true;
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
	const auto diagnostics = buildInfrastructureAndSignallingFromScene(scene);
	ok &= expect(!hasErrors(diagnostics), "a scene with more than 6000 sections builds");
	ok &= expect(Blocks == blockCount && signalling_block_sections.size() == static_cast<std::size_t>(blockCount),
		"the section storage is sized from the scene");
	if (signalling_block_sections.size() == static_cast<std::size_t>(blockCount))
		ok &= expect(signalling_block_sections.front().ID == "@block.0@"
				&& signalling_block_sections.back().ID == "@block.6000@",
			"the sections past the former limit are built");
	resetNativeInfrastructureState();
	ok &= expect(Blocks == 0 && signalling_block_sections.capacity() == 0,
		"the runtime reset releases the section storage");
	return ok;
}

static bool runRouteStorageChecks() {
	bool ok = true;
	auto diagnostics = buildInfrastructureAndSignallingFromScene(tinyScene());
	ok &= expect(!hasErrors(diagnostics) && train_route.size() == 1 && !train_route.front().reversed_direction
			&& train_route.front().N_Block_Sections == 2
			&& train_route.front().sequence_of_block_sections.size() == 2,
		"a route holds exactly the sections of its route");

	SceneModel reversed = tinyScene();
	reversed.routes.front().blocks = {"block.b", "block.a"};
	diagnostics = buildInfrastructureAndSignallingFromScene(reversed);
	ok &= expect(!hasErrors(diagnostics) && train_route.size() == 1 && train_route.front().reversed_direction
			&& train_route.front().N_Block_Sections == 2
			&& train_route.front().sequence_of_block_sections.size() == 2,
		"a reversed route holds exactly the sections of its route");

	diagnostics = buildInfrastructureAndSignallingFromScene(multiRegionRouteScene());
	ok &= expect(!hasErrors(diagnostics) && train_route.size() == 1 && train_route.front().N_Block_Sections == 3
			&& train_route.front().sequence_of_block_sections.size() == 3,
		"a route over several regions holds exactly the sections of its route");

	resetNativeInfrastructureState();
	ok &= expect(N_Routes == 0 && train_route.empty(), "the runtime reset removes the routes");
	return ok;
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

static void fillSection(Section& s, Node* nodes, TDS* tds) {
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
	strcpy_s(s.state, "red");
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
		&& a.SignallingLevel == b.SignallingLevel && !strcmp(a.state, b.state) && a.Occupied == b.Occupied
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

// An object that differs from the sample in one member compares unequal to it, in both directions.
template <typename T, typename Fill, typename Change>
static bool unequalAfter(const char* what, const char* member, Fill fill, Change change) {
	T sample, changed;
	fill(sample);
	fill(changed);
	change(changed);
	return expect(!(sample == changed) && !(changed == sample), std::string(what) + " that differ only in " + member + " compare unequal");
}

static bool runNodeValueChecks() {
	bool ok = true;
	Node source, reference;
	fillNode(source, 7);
	fillNode(reference, 7);

	Node copied(source);
	ok &= expect(sameNode(copied, source), "a copied node holds the members and the block list of its source");
	ok &= expect(copied.virtualSignal, "a copied node keeps the virtual signal flag");

	Node target;
	target.IDConnectedBlocks = {"old.1", "old.2", "old.3"};
	target = source;
	ok &= expect(sameNode(target, source), "an assigned node holds the members and the block list of its source");
	ok &= expect(target.virtualSignal, "an assigned node takes the virtual signal flag of its source");
	ok &= expect(sameNode(source, reference), "assigning a node leaves its source unchanged");

	Node stale, plain;
	stale.virtualSignal = true;
	stale = plain;
	ok &= expect(!stale.virtualSignal, "an assigned node loses its virtual signal flag when its source has none");

	Node& alias = source;
	source = alias;
	ok &= expect(sameNode(source, reference), "a node assigned to itself keeps its members and its block list");

	ok &= expect(source == reference && reference == source, "nodes with the same members compare equal in both directions");
	auto fill = [](Node& n) { fillNode(n, 7); };
	ok &= unequalAfter<Node>("nodes", "ID", fill, [](Node& n) { n.ID += 1; });
	ok &= unequalAfter<Node>("nodes", "X", fill, [](Node& n) { n.X += 1; });
	ok &= unequalAfter<Node>("nodes", "Y", fill, [](Node& n) { n.Y += 1; });
	ok &= unequalAfter<Node>("nodes", "isSignalled", fill, [](Node& n) { n.isSignalled = false; });
	ok &= unequalAfter<Node>("nodes", "station", fill, [](Node& n) { n.station = false; });
	ok &= unequalAfter<Node>("nodes", "dwellTime", fill, [](Node& n) { n.dwellTime += 1; });
	return ok;
}

static bool runArcValueChecks() {
	bool ok = true;
	Arc source, reference;
	fillArc(source, 1, 0.01);
	fillArc(reference, 1, 0.01);

	Arc copied(source);
	ok &= expect(sameArc(copied, source), "a copied arc holds the members of its source");

	Arc target;
	target = source;
	ok &= expect(sameArc(target, source), "an assigned arc holds the members of its source");
	ok &= expect(sameArc(source, reference), "assigning an arc leaves its source unchanged");

	Arc& alias = source;
	source = alias;
	ok &= expect(sameArc(source, reference), "an arc assigned to itself keeps its members");

	Arc flat, otherFlat;
	ok &= expect(flat == otherFlat, "two default arcs compare equal");
	ok &= expect(source == reference && reference == source, "arcs with the same members compare equal in both directions");
	ok &= expect(source.gradient == 0.01 && reference.gradient == 0.01, "comparing equal arcs leaves their gradients unchanged");
	Arc steeper;
	fillArc(steeper, 1, 0.02);
	ok &= expect(!(source == steeper) && !(steeper == source), "arcs that differ only in gradient compare unequal in both directions");
	ok &= expect(source.gradient == 0.01 && steeper.gradient == 0.02, "comparing unequal arcs leaves their gradients unchanged");

	auto fill = [](Arc& a) { fillArc(a, 1, 0.01); };
	ok &= unequalAfter<Arc>("arcs", "ID", fill, [](Arc& a) { a.ID += 1; });
	ok &= unequalAfter<Arc>("arcs", "length", fill, [](Arc& a) { a.length += 1; });
	ok &= unequalAfter<Arc>("arcs", "startNode.ID", fill, [](Arc& a) { a.startNode.ID += 1; });
	ok &= unequalAfter<Arc>("arcs", "endNode.ID", fill, [](Arc& a) { a.endNode.ID += 1; });
	ok &= unequalAfter<Arc>("arcs", "speedLimit", fill, [](Arc& a) { a.speedLimit += 1; });
	ok &= unequalAfter<Arc>("arcs", "curvature", fill, [](Arc& a) { a.curvature += 1; });
	ok &= unequalAfter<Arc>("arcs", "fs", fill, [](Arc& a) { a.fs += 1; });
	ok &= unequalAfter<Arc>("arcs", "brakingDistance", fill, [](Arc& a) { a.brakingDistance += 1; });
	ok &= unequalAfter<Arc>("arcs", "speedInBraking", fill, [](Arc& a) { a.speedInBraking += 1; });
	ok &= unequalAfter<Arc>("arcs", "signalSpeedLimit", fill, [](Arc& a) { a.signalSpeedLimit += 1; });
	return ok;
}

static bool runSectionValueChecks() {
	bool ok = true;
	Node nodes[2], otherNodes[2];
	TDS tds, firstOtherTds, secondOtherTds;
	Section source, reference;
	fillSection(source, nodes, &tds);
	fillSection(reference, nodes, &tds);

	Section copied(source);
	ok &= expect(sameSection(copied, source), "a copied section holds the members, arcs and detection sections of its source");

	Section target;
	target.TDS_in_block = {&firstOtherTds, &secondOtherTds};
	target = source;
	ok &= expect(sameSection(target, source), "an assigned section holds the members, arcs and detection sections of its source");
	ok &= expect(sameSection(source, reference), "assigning a section leaves its source unchanged");

	Section& alias = source;
	source = alias;
	ok &= expect(sameSection(source, reference), "a section assigned to itself keeps its members, arcs and detection sections");

	// A section with the flag at its end node only, as the first half of a double switch has it in a route that runs forward.
	Section firstHalf, assignedHalf;
	fillSection(firstHalf, nodes, &tds);
	firstHalf.start_node.virtualSignal = false;
	assignedHalf = firstHalf;
	ok &= expect(!assignedHalf.start_node.virtualSignal && assignedHalf.end_node.virtualSignal,
		"an assigned section keeps which of its end nodes carries the virtual signal");

	ok &= expect(source == reference && reference == source, "sections with the same members compare equal in both directions");
	ok &= expect(sameSection(source, reference), "comparing equal sections leaves their arcs unchanged");
	Section steeper;
	fillSection(steeper, nodes, &tds);
	steeper.arcs_in_signalling_block_section[0].gradient = 0.05;
	ok &= expect(!(source == steeper) && !(steeper == source), "sections that differ only in the gradient of the first arc compare unequal");
	ok &= expect(sameSection(source, reference), "comparing unequal sections leaves the arcs of the left section unchanged");

	auto fill = [&](Section& s) { fillSection(s, nodes, &tds); };
	ok &= unequalAfter<Section>("sections", "the gradient of the second arc", fill, [](Section& s) { s.arcs_in_signalling_block_section[1].gradient = 0.03; });
	ok &= unequalAfter<Section>("sections", "ID", fill, [](Section& s) { s.ID = "section.other"; });
	ok &= unequalAfter<Section>("sections", "state", fill, [](Section& s) { strcpy_s(s.state, "green"); });
	ok &= unequalAfter<Section>("sections", "the list of nodes", fill, [&](Section& s) { s.nodelist_of_nodes_in_signalling_section = otherNodes; });
	ok &= unequalAfter<Section>("sections", "total_nodes", fill, [](Section& s) { s.total_nodes = 1; });
	ok &= unequalAfter<Section>("sections", "total_arcs", fill, [](Section& s) { s.total_arcs = 1; });
	ok &= unequalAfter<Section>("sections", "length", fill, [](Section& s) { s.length += 1; });
	ok &= unequalAfter<Section>("sections", "exit_speed", fill, [](Section& s) { s.exit_speed += 1; });
	ok &= unequalAfter<Section>("sections", "code", fill, [](Section& s) { s.code += 1; });
	ok &= unequalAfter<Section>("sections", "start_node.ID", fill, [](Section& s) { s.start_node.ID += 1; });
	return ok;
}

static bool runValueSemanticsChecks() {
	bool ok = runNodeValueChecks();
	ok &= runArcValueChecks();
	ok &= runSectionValueChecks();
	return ok;
}

// The texts of the track detection section border ids, for plain sections and for sections with a diverging switch,
// and the text that a node searches its connected blocks with.
static bool runTrackDetectionBorderNameChecks() {
	bool ok = true;
	const auto same = [&ok](const std::string& actual, const std::string& expected, const std::string& what) {
		ok &= expect(actual == expected, what + " is '" + actual + "'");
	};
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
	same(endId(plain, 0), "@-7.234568-P1@Point", "the border of a first arc end without a switch");
	same(endId(plain, 1), "@12.345679-P1@Point", "the border of a later arc end without a switch");
	same(whole.arcs_in_signalling_block_section[0].startNode.tdsbId, "@Beg-Ba@/@20.765432-Bb@Point-Start",
		"the border at the start of a switch at the section start");
	same(endId(whole, 0), "@20.123457-Ba@/@20.765432-Bb@Point-End@20.123457-Ba@/@End-Bb@Point-End",
		"the border at the end of a switch at the section end");
	same(endId(ahead, 0), "@29.333333-Bc@Point", "the border before a switch");
	same(endId(ahead, 1), "@30.555556-Bc@/@31.444444-Bd@Point-Start@End-Bc@/@31.444444-Bd@Point-Start",
		"the border at the start of a switch at the section end");
	same(endId(named, 0), "@Q@/@40.888889-Bf@Point-Start", "the named border at the start of a switch");
	same(endId(named, 1), "@40.222222-Be@/@R@Point-End", "the named border at the end of a switch");
	same(endId(named, 2), "@45.555556-Bf@Point", "the border after a switch");

	Node connected;
	connected.numConnections = 1;
	connected.connectIdBlockSet[0] = 30;
	connected.connectXNode[0] = 4.5923456;
	std::string candidates[] = {"@1-B3@-4.592346/@4-B5@-6.700000", "@1-B30@-4.592346/@5-B7@-4.620000"};
	connected.initialiseIdConnectedBlocks(candidates, 2);
	ok &= expect(connected.IDConnectedBlocks.size() == 1 && connected.IDConnectedBlocks.front() == candidates[1],
		"a node finds the connected block with its block set and abscissa");
	return ok;
}

// The messages of simulation/Signalling.cpp are sim.setup messages of the logging core. A sink collects them, because the console
// shows neither the category nor the level, and each text is compared with the exact wording, quirks included (a missing space,
// a tail that is written twice, "ahving").
static bool runSignallingMessageChecks() {
	bool ok = true;
	struct Logged {
		eglog::Category category;
		eglog::Level level;
		std::string text;
	};
	struct Expected {
		eglog::Level level;
		std::string text;
	};
	// Collects the messages and puts the default sink and the default rules back on every path out of the function.
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
	} capture;
	const eglog::Level infoLevel = eglog::Level::Info;
	const eglog::Level warningLevel = eglog::Level::Warning;
	const eglog::Level errorLevel = eglog::Level::Error;
	const auto visible = [](std::string text) {
		for (std::size_t at = 0; (at = text.find_first_of("\r\n", at)) != std::string::npos; at += 2)
			text.replace(at, 1, text[at] == '\r' ? "\\r" : "\\n");
		return text;
	};
	const auto shows = [&](const std::vector<Logged>& found, const std::vector<Expected>& wanted, const std::string& what) {
		bool same = found.size() == wanted.size();
		for (std::size_t index = 0; same && index < found.size(); ++index)
			same = found[index].category == eglog::Category::SimSetup && found[index].level == wanted[index].level
				&& found[index].text == wanted[index].text;
		std::string described;
		if (!same) {
			described = "\n  found:";
			for (const Logged& message : found)
				described += std::string("\n    ") + eglog::categoryName(message.category) + " " + eglog::levelName(message.level)
					+ " '" + visible(message.text) + "'";
			described += "\n  wanted:";
			for (const Expected& message : wanted)
				described += std::string("\n    sim.setup ") + eglog::levelName(message.level) + " '" + visible(message.text) + "'";
		}
		ok &= expect(same, what + described);
	};

	// The route setup of a build: the progress text of each route, then the line end, also without routes.
	ok &= expect(!hasErrors(buildInfrastructureAndSignallingFromScene(tinyScene())), "the scene with one route builds");
	shows(capture.take(), {{infoLevel, "\rCreating Scene Route : 0 route.tiny"}, {infoLevel, "\n"}}, "the route setup of a scene with one route");
	ok &= expect(!hasErrors(buildInfrastructureAndSignallingFromScene(stableConnectionScene())), "the scene without routes builds");
	shows(capture.take(), {{infoLevel, "\n"}}, "the route setup of a scene without routes");
	resetNativeInfrastructureState();
	capture.take();

	// A cut abscissa outside the section is an error. The failed cut replaces the section by a default one, so the id is a constant.
	const auto cut = [&](const std::string& part, double abscissa, double xStartSwitch, double xEndSwitch) {
		Section section;
		section.ID = "@Cut@";
		section.withSwitchDiv = true;
		section.XStartSwitch = xStartSwitch;
		section.XEndSwitch = xEndSwitch;
		section.end_node.X = 10.0;
		section.total_arcs = 1;
		section.arcs_in_signalling_block_section[0].endNode.X = 10.0;
		section.CutBlockSection(part, abscissa);
		delete[] section.nodelist_of_nodes_in_signalling_section;
		return capture.take();
	};
	shows(cut("CutEnd", 12.5, 2.0, 4.0),
		{{errorLevel, "ERROR: The Block Section @Cut@ cannot be cut in the inserted abscissa 12.5\n\n"}},
		"a cut outside the section");
	ok &= expect(eglog::configure("sim.setup=false"), "the rule that switches sim.setup off is well formed");
	shows(cut("CutEnd", 12.5, 2.0, 4.0), {}, "a cut outside the section with sim.setup off");
	eglog::configure("");

	// Valid cuts of a section of length 10 with a diverging switch from 2 to 4, where the cut touches or removes a switch edge.
	const std::string endEdgeCut = " has a diverging switch and it is being cut at a progressive which cuts out the XEndSwitch edge of the switch, "
								   "please consider repositioning the final edge of this switch or the starting edge of the switch "
								   "of the next block section in the route\n";
	const std::string startEdgeCut = " has a diverging switch and it is being cut at a progressive which cuts out the XStartSwitch edge of the switch, "
									 "please consider repositioning the starting edge of this switch or the final edge of the switch "
									 "of the previous block section in the route\n";
	shows(cut("CutEnd", 4.0, 2.0, 4.0),
		{{infoLevel, "Cutting Block Section: @Cut@ at the ending edge of its diverging switch\n\n"}},
		"a cut at the end of the switch");
	shows(cut("CutEnd", 3.0, 2.0, 4.0), {{warningLevel, "Warning: Block Section: @Cut@" + endEdgeCut}}, "a cut inside the switch");
	shows(cut("CutEnd", 1.0, 2.0, 4.0), {{warningLevel, "Warning: Block Section: @Cut@" + endEdgeCut}}, "a cut before the switch");
	shows(cut("CutBegin", 2.0, 2.0, 4.0),
		{{infoLevel, "Cutting Block Section: @Cut@at the starting edge of its diverging switch\n\n"}},
		"a cut at the start of the switch");
	shows(cut("CutBegin", 3.0, 2.0, 4.0), {{warningLevel, "Warning: Block Section : @Cut@" + startEdgeCut}}, "a cut inside the switch from the start");
	shows(cut("CutBegin", 3.0, 4.0, 2.0),
		{{errorLevel, "ERROR: Block Section: @Cut@ has Ending Edge of diverging switch with a progressive inferior to its Starting Egde. "
					  "Problems in cutting the block section. PLease correct Switch on the infrastructure\n\n"}},
		"a cut of a switch that ends before it starts");

	// The elements that a train crosses between 4 and 6 and that miss a name.
	const auto crossing = [&](const InfraElement& element) {
		std::list<InfraElement> elements(1);
		elements.front() = element;
		UpdateInfrastructureElementStatus(elements, 10, 4.0, 6.0, 0.0, "T1");
		return capture.take();
	};
	InfraElement divergingSwitch;
	divergingSwitch.XCoordinate = 5.0;
	divergingSwitch.IsSwitch = true;
	divergingSwitch.withSwitchDiv = true;
	shows(crossing(divergingSwitch),
		{{warningLevel, "Warning: Diverging Switch: Noneat position 5 on Route of Train T1 has no SwitchName. Please investigate on it\n"},
			{warningLevel, "Warning: Position not found for connected Point of Switch: None on Route of TrainT1\n"}},
		"a diverging switch with no name and no connected point");
	divergingSwitch.ID = "sw1";
	divergingSwitch.XConnectedPoint = 7.0;
	shows(crossing(divergingSwitch),
		{{warningLevel, "Warning: Diverging Switch: sw1at position 5 on Route of Train T1 has no SwitchName. Please investigate on it\n"}},
		"a diverging switch with no switch name");
	divergingSwitch.SwitchName = "@s@Point";
	divergingSwitch.XConnectedPoint = -1;
	shows(crossing(divergingSwitch),
		{{warningLevel, "Warning: Position not found for connected Point of Switch: sw1 on Route of TrainT1\n"}},
		"a diverging switch with no connected point");
	InfraElement unnamed;
	unnamed.XCoordinate = 5.0;
	shows(crossing(unnamed),
		{{warningLevel, "Warning: Infrastructure Element at position 5 on Route of Train T1 has no ID. Please investigate on it\n"}},
		"an element with no id");

	// The track detection section borders of one hand-made section per call. A switch section has the tokens of two blocks in its id.
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
	shows(borders(plain), {{warningLevel, "Warning: Block @P1@" + noEndName}}, "the last node of a section without a switch has no name");
	const double wholeStart = 20.1234567;
	const double wholeEnd = 20.7654321;
	std::vector<Section> whole = oneSection(switchId, true, wholeStart, wholeEnd, wholeEnd);
	addArc(whole[0], wholeStart, wholeEnd, 0, "", 0);
	shows(borders(whole), {{warningLevel, noBeginningName}}, "the first node of the first arc is the start of the switch and has no name");
	const double xStart = 40.2222222;
	const double xEnd = 40.8888888;
	std::vector<Section> beforeSwitch = oneSection(switchId, true, xStart, xEnd, 50.0);
	addArc(beforeSwitch[0], 40.0, 41.0, 1, "", 0);
	shows(borders(beforeSwitch),
		{{warningLevel, noBeginningName}},
		"the first node of the first arc is a switch before the switch of the section and has no name");
	std::vector<Section> named = oneSection(switchId, true, xStart, xEnd, 50.0);
	addArc(named[0], 40.0, 41.0, 1, "@Q@", 0);
	shows(borders(named),
		{{warningLevel, "Warning: The initial Node of Block Section: " + switchId + "is also a switch: Changing its TDSB ID accordingly in: @Q@Point\n"}},
		"the first node of the first arc is a switch and has a name");
	ok &= expect(named[0].arcs_in_signalling_block_section[0].startNode.tdsbId == "@Q@Point", "the name in the message is the name of the node");
	std::vector<Section> afterSwitch = oneSection(switchId, true, xStart, xEnd, 50.0);
	addArc(afterSwitch[0], 41.5, 45.0, 1, "", 0);
	shows(borders(afterSwitch),
		{{warningLevel, noBeginningName}, {errorLevel, startingNode + "that goes after the end of the Switch\n"}},
		"the first node of the first arc is a switch after the switch of the section");
	std::vector<Section> insideSwitch = oneSection(switchId, true, xStart, xEnd, 50.0);
	addArc(insideSwitch[0], 40.5, 41.0, 1, "", 0);
	shows(borders(insideSwitch),
		{{warningLevel, noBeginningName}, {errorLevel, startingNode + "in between the start and the end of the switch\n"}},
		"the first node of the first arc is a switch inside the switch of the section");
	const std::string aheadId = "@Bc@-1.000000/@Bd@-2.000000";
	const double aheadStart = 30.5555556;
	std::vector<Section> ahead = oneSection(aheadId, true, aheadStart, 31.4444444, aheadStart);
	addArc(ahead[0], 29.0, aheadStart, 0, "", 0);
	shows(borders(ahead),
		{{warningLevel, "Warning: Check correctness of Block Section:" + aheadId + " its ending Node is the start of the switch "
																				   "on the same block section\n\n"}},
		"the last node of the section is the start of its switch");
	std::vector<Section> between = oneSection(switchId, true, xStart, xEnd, 50.0);
	addArc(between[0], 40.0, 40.5, 0, "", 1);
	const std::string betweenSwitchEdges = " Node at position: 40.5 is a switch defined in between the start (XStartSwitch) "
										   "and the end (XEndSwitch) of the diverging switch. A name will be automatically assigned to it\n";
	shows(borders(between),
		{{warningLevel, "WARNING: in Block Section" + switchId + betweenSwitchEdges}},
		"the end node of an arc is a switch inside the switch of the section");

	// A switch edge with a one-token name cannot be read as a diverging switch. Three of the four messages write the tail twice.
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
	shows(switchName(false, startEdge),
		{{warningLevel, "Warning: Switch at starting Node: AB on Block Section @S1@for Route R1 that has reversed = 0" + tail}},
		"a switch at the start of a section");
	Section arcEdge = edge();
	arcEdge.total_arcs = 1;
	arcEdge.arcs_in_signalling_block_section[0].endNode.tdsbId = "AB";
	arcEdge.arcs_in_signalling_block_section[0].endNode.tdsbGeoCoordX = 0.0;
	arcEdge.arcs_in_signalling_block_section[0].endNode.numConnections = 1;
	shows(switchName(false, arcEdge),
		{{warningLevel, "Warning: Switch at end of one of the arcs: AB on Block Section @S1@for RouteR1that has reversed = 0" + tail + tail}},
		"a switch at the end of an arc");
	Section reversedEdge = edge();
	reversedEdge.total_arcs = 1;
	reversedEdge.arcs_in_signalling_block_section[0].startNode.tdsbId = "AB";
	reversedEdge.arcs_in_signalling_block_section[0].startNode.tdsbGeoCoordX = 0.0;
	reversedEdge.arcs_in_signalling_block_section[0].startNode.numConnections = 1;
	shows(switchName(true, reversedEdge),
		{{warningLevel, "Warning: Switch at one of the beginning nodes: AB on Block Section @S1@for RouteR1that has reversed = 1" + tail + tail}},
		"a switch at the start of an arc of a reversed route");
	Section endEdge = edge();
	endEdge.end_node.tdsbId = "AB";
	endEdge.end_node.numConnections = 1;
	shows(switchName(false, endEdge),
		{{warningLevel, "Warning: Switch at ending Node: AB on Block Section @S1@for RouteR1that has reversed = 0" + tail + tail}},
		"a switch at the end of a section");

	// The switch of a section is connected to blocks that are not the section.
	Section unknownBlock;
	unknownBlock.ID = "@C@";
	unknownBlock.total_arcs = 1;
	unknownBlock.arcs_in_signalling_block_section[0].endNode.X = 1.0;
	unknownBlock.arcs_in_signalling_block_section[0].endNode.numConnections = 1;
	unknownBlock.arcs_in_signalling_block_section[0].endNode.IDConnectedBlocks.push_back("@A@-1.000000/@B@-2.000000");
	lockSwitchesOnAllConnectedSections(1010.0, 990.0, 10.0, 0.0, unknownBlock, "now", "next", "T1", false, "None");
	ETCS_MA.clear();
	shows(capture.take(),
		{{errorLevel, "\n\nError: there is not Block ahving the same ID of Block signalling_block_sections in Function: "
					  "lockSwitchesOnAllConnectedSections...Please investigate on that\n\n"}},
		"a switch connected to blocks that are not the section");

	// A section whose id is part of the ids of eleven others has more connected sections than it can hold.
	signalling_block_sections = std::vector<Section>(12);
	Blocks = static_cast<int>(signalling_block_sections.size());
	signalling_block_sections[0].ID = "@Cap@";
	for (std::size_t index = 1; index < signalling_block_sections.size(); ++index)
		signalling_block_sections[index].ID = "@Cap@-" + std::to_string(index);
	setDependenciesBetweenBlocks();
	resetNativeInfrastructureState();
	shows(capture.take(),
		{{errorLevel, "ERROR: Block section @Cap@ has more than 10 connected block sections\n"}},
		"a section with more connected sections than it holds");
	return ok;
}

int main() {
	bool ok = runTinyBuilderChecks();
	ok &= runAreaMappingChecks();
	ok &= runLongTrackChecks();
	ok &= runManySectionsChecks();
	ok &= runRouteStorageChecks();
	ok &= runValueSemanticsChecks();
	ok &= runTrackDetectionBorderNameChecks();
	ok &= runSignallingMessageChecks();
	return ok ? 0 : 1;
}
