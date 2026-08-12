#include "scene/SceneModel.h"
#include "scene/SectionInventory.h"
#include "simulation/Signalling.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

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
	ok &= expect(Blocks == 2, "two canonical blocks become two straight runtime sections");
	ok &= expect(signalling_block_sections[0].ID == "@block.a@"
			&& signalling_block_sections[1].ID == "@block.b@", "block IDs use the runtime boundary form");
	ok &= expect(numStations == 2 && numAllStationPlatforms == 2, "station and platform counts are bound");
	ok &= expect(StationArray[0].X == 1.0, "platform node anchors a station without a separate position");
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
			&& std::get<3>(singleTrackLimits.front()) == "@block.a@",
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
		Section unresolvedSwitch;
		unresolvedSwitch.ID = "@missing.a@-1.000000/@missing.b@-2.000000";
		Section unresolvedPrevious;
		unresolvedPrevious.ID = "@missing.c@-3.000000/@missing.d@-4.000000";
		occupyDoubleSwitch(unresolvedSwitch, unresolvedPrevious);
		ok &= expect(BlocksOccupied == occupiedBefore && BlocksConnected == connectedBefore,
			"unresolved double-switch branches are rejected before occupancy mutation");
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
	return ok;
}


int main() {
	bool ok = runTinyBuilderChecks();
	return ok ? 0 : 1;
}
