#include "scene/SceneIds.h"
#include "scene/SceneModel.h"
#include "scene/SceneReferences.h"
#include "scene/SectionInventory.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

using Names = std::vector<std::string>;

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static std::string join(const Names& values) {
	std::string text;
	for (const std::string& value : values)
		text += (text.empty() ? "" : " | ") + value;
	return "[" + text + "]";
}

static bool expectNames(const Names& actual, const Names& expected, const char* message) {
	const bool same = actual == expected;
	if (!same)
		std::cerr << "failed: " << message << ": expected " << join(expected) << " got " << join(actual) << "\n";
	return same;
}

static SceneNode node(const std::string& id, const std::string& trackId, double xKm) {
	SceneNode result;
	result.id = id;
	result.trackId = trackId;
	result.xKm = xKm;
	return result;
}

static SceneArc arc(const std::string& id, const std::string& trackId, const std::string& from,
	const std::string& to) {
	SceneArc result;
	result.id = id;
	result.trackId = trackId;
	result.fromNodeId = from;
	result.toNodeId = to;
	return result;
}

static SceneBlock block(const std::string& id, const std::string& trackId) {
	SceneBlock result;
	result.id = id;
	result.trackId = trackId;
	result.lengthKm = 1.0;
	return result;
}

static SceneStop stop(const std::string& stationId, const std::string& platformId) {
	SceneStop result;
	result.stationId = stationId;
	result.platformId = platformId;
	return result;
}

static SceneIncident incident(const std::string& id, const std::string& type, const std::string& target) {
	SceneIncident result;
	result.id = id;
	result.type = type;
	result.target = target;
	return result;
}

static SceneEntranceDelay delay(const std::string& serviceId, const std::string& stationId) {
	SceneEntranceDelay result;
	result.serviceId = serviceId;
	result.stationId = stationId;
	result.delaySeconds = 60.0;
	return result;
}

static ScenePassengerLeg leg(const std::string& id, const std::string& origin, const std::string& destination,
	const std::string& serviceId) {
	ScenePassengerLeg result;
	result.id = id;
	result.originStationId = origin;
	result.destinationStationId = destination;
	result.serviceId = serviceId;
	return result;
}

// Two tracks of two blocks each, a connection between them, two stations that both have a platform "platform-1",
// one signal, one route, two services and one scenario with a signal failure, a breakdown and an entrance delay.
// Every id has a name of its own, so a test can tell which reference a rewrite reached.
static SceneModel referenceScene() {
	SceneModel scene;
	scene.tracks.push_back({"track-a"});
	scene.tracks.push_back({"track-b"});
	scene.nodes = {node("node-a0", "track-a", 0.0), node("node-a1", "track-a", 1.0), node("node-a2", "track-a", 2.0),
		node("node-b0", "track-b", 0.0), node("node-b1", "track-b", 1.0), node("node-b2", "track-b", 2.0)};
	scene.arcs = {arc("arc-a1", "track-a", "node-a0", "node-a1"), arc("arc-a2", "track-a", "node-a1", "node-a2"),
		arc("arc-b1", "track-b", "node-b0", "node-b1"), arc("arc-b2", "track-b", "node-b1", "node-b2")};
	scene.blocks = {block("block-a1", "track-a"), block("block-a2", "track-a"), block("block-b1", "track-b"),
		block("block-b2", "track-b")};
	SceneConnection connection;
	connection.id = "connection-1";
	connection.fromNodeId = "node-a1";
	connection.toNodeId = "node-b2";
	scene.connections.push_back(connection);
	SceneSignallingArea area;
	area.id = "area-1";
	area.trackId = "track-a";
	scene.signallingAreas.push_back(area);

	SceneStation first;
	first.id = "station-1";
	ScenePlatform platform;
	platform.id = "platform-1";
	platform.nodeIds = {"node-a1"};
	first.platforms.push_back(platform);
	platform.id = "platform-2";
	platform.nodeIds = {"node-a2"};
	first.platforms.push_back(platform);
	scene.stations.push_back(first);
	SceneStation second;
	second.id = "station-2";
	platform.id = "platform-1";
	platform.nodeIds = {"node-b1", "node-b2"};
	second.platforms.push_back(platform);
	scene.stations.push_back(second);

	SceneSignal signal;
	signal.id = "signal-1";
	signal.protectedSection = "block-a1";
	scene.signals.push_back(signal);

	SceneRoute route;
	route.id = "route-1";
	route.blocks = {"block-a1", "block-a2/block-b1", "@block-b2@-2.000000"};
	scene.routes.push_back(route);

	SceneBlockDependency dependency;
	dependency.block = "block-a2";
	dependency.dependsOn = "block-b1";
	scene.blockDependencies.push_back(dependency);
	SceneSingleTrackRestriction restriction;
	restriction.startBlock = "block-a1";
	restriction.endBlock = "block-a2";
	restriction.protectedStartBlock = "block-b1";
	restriction.protectedEndBlock = "block-b2";
	scene.singleTrackRestrictions.push_back(restriction);
	SceneStationBoundary boundary;
	boundary.entranceBlock = "block-a1";
	boundary.hasExitBlock = true;
	boundary.exitBlock = "block-a2";
	scene.stationBoundaries.push_back(boundary);

	SceneTrainUnit unitOne;
	unitOne.id = "unit-1";
	SceneTrainUnit unitTwo;
	unitTwo.id = "unit-2";
	scene.trainUnits = {unitOne, unitTwo};
	SceneComposition pair;
	pair.id = "composition-1";
	pair.units = {"unit-1", "unit-2", "unit-1"};
	SceneComposition single;
	single.id = "composition-2";
	single.units = {"unit-2"};
	scene.compositions = {pair, single};

	SceneService local;
	local.id = "service-1";
	local.composition = "composition-1";
	local.route = "route-1";
	local.stops = {stop("station-1", "platform-1"), stop("station-1", "platform-2"), stop("station-2", "platform-1")};
	SceneService express;
	express.id = "service-2";
	express.composition = "composition-2";
	express.route = "route-1";
	express.stops = {stop("station-2", "platform-1")};
	scene.services = {local, express};

	SceneScenario base;
	base.id = "scenario-1";
	base.incidents = {incident("incident-1", "signal_failure", "signal-1"),
		incident("incident-2", "train_breakdown", "service-1")};
	base.entranceDelays = {delay("service-1", "station-1")};
	SceneScenario other;
	other.id = "scenario-2";
	other.incidents = {incident("incident-3", "signal_failure", "block-a2")};
	scene.scenarios = {base, other};
	scene.defaultScenarioId = "scenario-1";

	ScenePassengerJourney journey;
	journey.id = "journey-1";
	journey.originStationId = "station-1";
	journey.destinationStationId = "station-2";
	journey.legs = {leg("leg-1", "station-1", "station-2", "service-1")};
	ScenePassenger passenger;
	passenger.id = "passenger-1";
	passenger.journeys = {journey};
	scene.passengers = {passenger};
	return scene;
}

static Names consumers(const SceneModel& scene, const std::string& facet, const std::string& id,
	const std::string& scope = {}) {
	return sceneDirectDeleteConsumers(scene, facet, id, scope);
}

static bool testBlockReferences() {
	bool ok = true;
	ok &= expectNames(sceneBlockReferenceComponents("block-a1"), {"block-a1"}, "a plain reference has one component");
	ok &= expectNames(sceneBlockReferenceComponents("block-a2/block-b1"), {"block-a2", "block-b1"},
		"a joined reference lists each component");
	ok &= expectNames(sceneBlockReferenceComponents("@block-a2@-1.000000/@block-b2@-2.000000"),
		{"block-a2", "block-b2"}, "a wrapped component is read without its wrapper");
	ok &= expectNames(sceneBlockReferenceComponents(""), {}, "an empty reference has no component");

	ok &= expect(sceneRewriteBlockReference("block-a1", "block-a1", "renamed") == "renamed",
		"a plain reference to the block is replaced");
	ok &= expect(sceneRewriteBlockReference("block-a1", "block-a2", "renamed") == "block-a1",
		"a reference to another block is kept");
	ok &= expect(sceneRewriteBlockReference("block-a2/block-b1", "block-b1", "renamed") == "block-a2/renamed",
		"one component of a joined reference is replaced");
	ok &= expect(sceneRewriteBlockReference("@block-a2@-1.000000/@block-b2@-2.000000", "block-b2", "renamed")
			== "@block-a2@-1.000000/@renamed@-2.000000",
		"a wrapped component keeps its wrapper and coordinate");
	ok &= expect(sceneRewriteBlockReference("block-a1x", "block-a1", "renamed") == "block-a1x",
		"a longer id that starts with the old id is kept");
	return ok;
}

static bool testInfrastructureRenames() {
	bool ok = true;
	{
		SceneModel scene = referenceScene();
		sceneRewriteTrackReferences(scene, "track-a", "track-x");
		ok &= expect(scene.nodes[0].trackId == "track-x" && scene.nodes[2].trackId == "track-x"
				&& scene.nodes[3].trackId == "track-b",
			"a track rename reaches the nodes of that track only");
		ok &= expect(scene.arcs[0].trackId == "track-x" && scene.arcs[2].trackId == "track-b",
			"a track rename reaches the arcs of that track only");
		ok &= expect(scene.blocks[0].trackId == "track-x" && scene.blocks[1].trackId == "track-x"
				&& scene.blocks[2].trackId == "track-b",
			"a track rename reaches the blocks of that track only");
		ok &= expect(scene.signallingAreas[0].trackId == "track-x", "a track rename reaches the signalling areas");
		ok &= expect(scene.tracks[0].id == "track-a", "a reference rewrite leaves the track itself to the caller");
	}
	{
		SceneModel scene = referenceScene();
		sceneRewriteNodeReferences(scene, "node-a1", "node-x");
		ok &= expect(scene.arcs[0].toNodeId == "node-x" && scene.arcs[1].fromNodeId == "node-x"
				&& scene.arcs[0].fromNodeId == "node-a0",
			"a node rename reaches both ends of the arcs");
		ok &= expect(scene.connections[0].fromNodeId == "node-x" && scene.connections[0].toNodeId == "node-b2",
			"a node rename reaches the connection end");
		ok &= expect(scene.stations[0].platforms[0].nodeIds == Names({"node-x"})
				&& scene.stations[0].platforms[1].nodeIds == Names({"node-a2"}),
			"a node rename reaches the nodes of the platforms");
	}
	{
		SceneModel scene = referenceScene();
		sceneRewriteBlockReferences(scene, "block-a2", "block-x");
		ok &= expect(scene.routes[0].blocks == Names({"block-a1", "block-x/block-b1", "@block-b2@-2.000000"}),
			"a block rename reaches the sections of routes");
		ok &= expect(scene.blockDependencies[0].block == "block-x" && scene.blockDependencies[0].dependsOn == "block-b1",
			"a block rename reaches block dependencies");
		ok &= expect(scene.singleTrackRestrictions[0].endBlock == "block-x"
				&& scene.singleTrackRestrictions[0].startBlock == "block-a1",
			"a block rename reaches single-track restrictions");
		ok &= expect(scene.stationBoundaries[0].exitBlock == "block-x", "a block rename reaches the exit block");
		ok &= expect(scene.scenarios[1].incidents[0].target == "block-x",
			"a block rename reaches the target of a signal failure");
		ok &= expect(scene.scenarios[0].incidents[1].target == "service-1",
			"a block rename leaves breakdown targets alone");
		SceneModel protectedScene = referenceScene();
		sceneRewriteBlockReferences(protectedScene, "block-a1", "block-y");
		ok &= expect(protectedScene.signals[0].protectedSection == "block-y"
				&& protectedScene.routes[0].blocks[0] == "block-y",
			"a block rename reaches the protected section of signals");
		SceneModel noExit = referenceScene();
		noExit.stationBoundaries[0].hasExitBlock = false;
		sceneRewriteBlockReferences(noExit, "block-a2", "block-x");
		ok &= expect(noExit.stationBoundaries[0].exitBlock == "block-a2",
			"a boundary without an exit block keeps the text of the unused exit");
	}
	{
		SceneModel scene = referenceScene();
		sceneRewriteStationReferences(scene, "station-1", "station-x");
		ok &= expect(scene.services[0].stops[0].stationId == "station-x"
				&& scene.services[0].stops[1].stationId == "station-x"
				&& scene.services[0].stops[2].stationId == "station-2",
			"a station rename reaches the stops at that station only");
		ok &= expect(scene.scenarios[0].entranceDelays[0].stationId == "station-x",
			"a station rename reaches entrance delays");
		const ScenePassengerJourney& journey = scene.passengers[0].journeys[0];
		ok &= expect(journey.originStationId == "station-x" && journey.destinationStationId == "station-2"
				&& journey.legs[0].originStationId == "station-x" && journey.legs[0].destinationStationId == "station-2",
			"a station rename reaches passenger journeys and legs");
	}
	{
		SceneModel scene = referenceScene();
		sceneRewritePlatformReferences(scene, "station-1", "platform-1", "platform-x");
		ok &= expect(scene.services[0].stops[0].platformId == "platform-x"
				&& scene.services[0].stops[1].platformId == "platform-2"
				&& scene.services[0].stops[2].platformId == "platform-1",
			"a platform rename reaches the stops at that platform of that station only");
		SceneModel moved = referenceScene();
		sceneMovePlatformReferences(moved, "platform-1", "station-1", "station-2");
		ok &= expect(moved.services[0].stops[0].stationId == "station-2" && moved.services[0].stops[1].stationId == "station-1"
				&& moved.services[1].stops[0].stationId == "station-2",
			"moving a platform moves the stops at that platform to the new station");
	}
	{
		SceneModel scene = referenceScene();
		sceneRewriteSignalReferences(scene, "signal-1", "signal-x");
		ok &= expect(scene.scenarios[0].incidents[0].target == "signal-x" && scene.scenarios[1].incidents[0].target == "block-a2",
			"a signal rename reaches signal failures only");
		sceneRewriteRouteReferences(scene, "route-1", "route-x");
		ok &= expect(scene.services[0].route == "route-x" && scene.services[1].route == "route-x",
			"a route rename reaches the services on it");
	}
	return ok;
}

static bool testOperationRenames() {
	bool ok = true;
	{
		SceneModel scene = referenceScene();
		sceneRewriteTrainUnitReferences(scene, "unit-1", "unit-x");
		ok &= expect(scene.compositions[0].units == Names({"unit-x", "unit-2", "unit-x"})
				&& scene.compositions[1].units == Names({"unit-2"}),
			"a train unit rename reaches every place of the unit in compositions");
	}
	{
		SceneModel scene = referenceScene();
		sceneRewriteCompositionReferences(scene, "composition-1", "composition-x");
		ok &= expect(scene.services[0].composition == "composition-x" && scene.services[1].composition == "composition-2",
			"a composition rename reaches the services that run it");
	}
	{
		SceneModel scene = referenceScene();
		sceneRewriteServiceReferences(scene, "service-1", "service-x");
		ok &= expect(scene.scenarios[0].entranceDelays[0].serviceId == "service-x",
			"a service rename reaches entrance delays");
		ok &= expect(scene.scenarios[0].incidents[1].target == "service-x",
			"a service rename reaches the target of a breakdown");
		ok &= expect(scene.passengers[0].journeys[0].legs[0].serviceId == "service-x",
			"a service rename reaches passenger legs");
		SceneModel signalTarget = referenceScene();
		signalTarget.scenarios[1].incidents[0].target = "service-1";
		sceneRewriteServiceReferences(signalTarget, "service-1", "service-x");
		ok &= expect(signalTarget.scenarios[1].incidents[0].target == "service-1",
			"a service rename leaves a signal failure with the same text alone");
		ok &= expect(signalTarget.services[0].id == "service-1", "a reference rewrite leaves the service itself to the caller");
	}
	{
		SceneModel scene = referenceScene();
		sceneRewriteScenarioReferences(scene, "scenario-2", "scenario-x");
		ok &= expect(scene.defaultScenarioId == "scenario-1", "a rename of another scenario keeps the default");
		sceneRewriteScenarioReferences(scene, "scenario-1", "scenario-x");
		ok &= expect(scene.defaultScenarioId == "scenario-x", "a scenario rename reaches the default scenario");
	}
	{
		// No entity refers to an incident, so its rename has nothing to rewrite.
		const SceneModel scene = referenceScene();
		ok &= expect(consumers(scene, "incident", "incident-1").empty(), "an incident has no consumers");
	}
	return ok;
}

static bool testDeleteConsumers() {
	bool ok = true;
	const SceneModel scene = referenceScene();
	ok &= expectNames(consumers(scene, "tracks", "track-a"),
		{"node 'node-a0'", "node 'node-a1'", "node 'node-a2'", "arc 'arc-a1'", "arc 'arc-a2'", "block 'block-a1'",
			"block 'block-a2'", "signalling area 'area-1'"},
		"a track is used by its nodes, arcs, blocks and signalling areas");
	ok &= expectNames(consumers(scene, "nodes", "node-a1"),
		{"arc 'arc-a1' endpoint", "arc 'arc-a2' endpoint", "connection 'connection-1' endpoint",
			"platform 'platform-1' at station 'station-1'"},
		"a node is used by arc and connection ends and by platforms");
	ok &= expectNames(consumers(scene, "stations", "station-1"),
		{"service 'service-1' stop", "entrance delay for service 'service-1' in scenario 'scenario-1'",
			"passenger 'passenger-1' journey 'journey-1'", "passenger 'passenger-1' leg 'leg-1'"},
		"a station is used by stops, entrance delays and passenger journeys and legs, one line for each");
	ok &= expectNames(consumers(scene, "platforms", "platform-1", "station-1"),
		{"service 'service-1' stop at station 'station-1'"},
		"a platform is used by the stops at that platform of its own station");
	ok &= expectNames(consumers(scene, "platforms", "platform-1", "station-2"),
		{"service 'service-1' stop at station 'station-2'", "service 'service-2' stop at station 'station-2'"},
		"a platform id used in two stations is told apart by the station");
	ok &= expectNames(consumers(scene, "signals", "signal-1"),
		{"signal-failure incident 'incident-1' in scenario 'scenario-1'"},
		"a signal is used by the signal failures that target it");
	ok &= expectNames(consumers(scene, "routes", "route-1"), {"service 'service-1'", "service 'service-2'"},
		"a route is used by its services");
	ok &= expectNames(consumers(scene, "train_unit", "unit-1"), {"composition 'composition-1'"},
		"a train unit used twice by one composition is named once");
	ok &= expectNames(consumers(scene, "train_unit", "unit-2"), {"composition 'composition-1'", "composition 'composition-2'"},
		"a train unit is used by every composition that holds it");
	ok &= expectNames(consumers(scene, "composition", "composition-2"), {"service 'service-2'"},
		"a composition is used by the services that run it");
	ok &= expectNames(consumers(scene, "service", "service-1"),
		{"breakdown incident 'incident-2' in scenario 'scenario-1'", "entrance delay in scenario 'scenario-1'",
			"passenger 'passenger-1' leg 'leg-1'"},
		"a service is used by breakdowns, entrance delays and passenger legs");
	ok &= expect(consumers(scene, "service", "service-2").empty(), "a service that nothing refers to has no consumers");
	ok &= expect(consumers(scene, "arcs", "arc-a1").empty(), "an arc has no consumers");
	ok &= expect(consumers(scene, "tracks", "track-unknown").empty(), "an unknown id has no consumers");

	// Blocks are used through route sections, whether the section names the block or a section built from it.
	ok &= expectNames(consumers(scene, "blocks", "block-a2"),
		{"route 'route-1' section 'block-a2/block-b1'", "signal-failure incident 'incident-3' in scenario 'scenario-2'",
			"block dependency 'block-a2' -> 'block-b1'", "single-track restriction 'block-a1' -> 'block-a2'",
			"station boundary 'block-a1' -> 'block-a2'"},
		"a block is used by routes, signal failures, dependencies, restrictions and station boundaries");
	ok &= expectNames(consumers(scene, "blocks", "block-a1"),
		{"route 'route-1' section 'block-a1'", "signal 'signal-1' protected section 'block-a1'",
			"single-track restriction 'block-a1' -> 'block-a2'", "station boundary 'block-a1' -> 'block-a2'"},
		"a block is used by the protected section of a signal");
	SceneModel withoutExit = scene;
	withoutExit.stationBoundaries[0].hasExitBlock = false;
	ok &= expect(consumers(withoutExit, "blocks", "block-a2").size() == 4,
		"an exit block that a boundary does not use is not a consumer");

	std::string derived;
	for (const SceneSectionDescriptor& section : buildSceneSectionInventory(scene).sections)
		if (section.sourceConnectionId == "connection-1")
			derived = section.id;
	ok &= expect(!derived.empty(), "the connection yields a section");
	SceneModel connected = scene;
	connected.routes[0].blocks.push_back(derived);
	ok &= expectNames(consumers(connected, "connections", "connection-1"), {"route 'route-1' section '" + derived + "'"},
		"a connection is used by the routes that contain a section built from it");
	ok &= expect(consumers(scene, "connections", "connection-1").empty(), "an unused connection has no consumers");
	return ok;
}

static bool testUniqueIds() {
	bool ok = true;
	const SceneModel scene = referenceScene();
	ok &= expect(sceneUniqueTrainUnitId(scene, "unit-new") == "unit-new", "a free id is kept");
	ok &= expect(sceneUniqueTrainUnitId(scene, "unit-1") == "unit-1_2", "a taken id gets the suffix _2");
	SceneModel crowded = scene;
	SceneTrainUnit third;
	third.id = "unit-1_2";
	crowded.trainUnits.push_back(third);
	ok &= expect(sceneUniqueTrainUnitId(crowded, "unit-1") == "unit-1_3", "the suffix counts up to the first free one");
	ok &= expect(sceneUniqueCompositionId(scene, "composition-1") == "composition-1_2", "compositions are unique");
	ok &= expect(sceneUniqueServiceId(scene, "service-2") == "service-2_2", "services are unique");
	ok &= expect(sceneUniqueScenarioId(scene, "scenario-1") == "scenario-1_2", "scenarios are unique");
	ok &= expect(sceneUniqueScenarioId(scene, "") == "scenario", "an empty scenario id becomes scenario");
	ok &= expect(sceneUniqueIncidentId(scene, "incident-3") == "incident-3_2",
		"incident ids are unique across the scenarios");
	ok &= expect(sceneUniqueIncidentId(scene, "incident-4") == "incident-4", "a free incident id is kept");

	ok &= expect(sceneUniquePassengerId(scene, "passenger-1") == "passenger-1_2", "passengers are unique");
	ok &= expect(sceneUniquePassengerId(scene, "") == "new_passenger", "an empty passenger id becomes new_passenger");
	ok &= expect(sceneUniquePassengerJourneyId(scene, "journey-1") == "journey-1_2", "journeys are unique");
	ok &= expect(sceneUniquePassengerJourneyId(scene, "") == "new_journey", "an empty journey id becomes new_journey");
	ok &= expect(sceneUniquePassengerLegId(scene, "leg-1") == "leg-1_2", "legs are unique");
	ok &= expect(sceneUniquePassengerLegId(scene, "") == "new_leg", "an empty leg id becomes new_leg");
	SceneModel twoPassengers = scene;
	ScenePassenger second;
	second.id = "passenger-2";
	ScenePassengerJourney journey;
	journey.id = "journey-2";
	journey.legs = {leg("leg-2", "", "", "")};
	second.journeys = {journey};
	twoPassengers.passengers.push_back(second);
	ok &= expect(sceneUniquePassengerJourneyId(twoPassengers, "journey-2") == "journey-2_2"
			&& sceneUniquePassengerLegId(twoPassengers, "leg-2") == "leg-2_2",
		"journey and leg ids are unique across all passengers");

	ok &= expect(sceneUniqueInfrastructureId(scene, "track-a", "tracks") == "track-a_2", "track ids are unique");
	ok &= expect(sceneUniqueInfrastructureId(scene, "node-a0", "nodes") == "node-a0_2", "node ids are unique");
	ok &= expect(sceneUniqueInfrastructureId(scene, "arc-a1", "arcs") == "arc-a1_2", "arc ids are unique");
	ok &= expect(sceneUniqueInfrastructureId(scene, "block-a1", "blocks") == "block-a1_2", "block ids are unique");
	ok &= expect(sceneUniqueInfrastructureId(scene, "connection-1", "connections") == "connection-1_2",
		"connection ids are unique");
	ok &= expect(sceneUniqueInfrastructureId(scene, "station-1", "stations") == "station-1_2", "station ids are unique");
	ok &= expect(sceneUniqueInfrastructureId(scene, "signal-1", "signals") == "signal-1_2", "signal ids are unique");
	ok &= expect(sceneUniqueInfrastructureId(scene, "area-1", "signalling_areas") == "area-1_2",
		"signalling area ids are unique");
	ok &= expect(sceneUniqueInfrastructureId(scene, "route-1", "routes") == "route-1_2", "route ids are unique");
	ok &= expect(sceneUniqueInfrastructureId(scene, "platform-1", "platforms") == "platform-1_2",
		"platform ids are unique within the first station");
	ok &= expect(sceneUniqueInfrastructureId(scene, "platform-2", "platforms") == "platform-2_2",
		"platform ids are checked in the first station");
	SceneModel otherFirst = scene;
	std::swap(otherFirst.stations[0], otherFirst.stations[1]);
	ok &= expect(sceneUniqueInfrastructureId(otherFirst, "platform-2", "platforms") == "platform-2",
		"platform ids of a later station are not taken into account");
	ok &= expect(sceneUniqueInfrastructureId(scene, "track-a", "nodes") == "track-a",
		"an id is only compared within its own table");
	return ok;
}

static bool testTrainUnitLookup() {
	const SceneModel scene = referenceScene();
	bool ok = true;
	ok &= expect(sceneTrainUnitById(scene, "unit-2") == &scene.trainUnits[1], "a train unit is found by id");
	ok &= expect(sceneTrainUnitById(scene, "unit-9") == nullptr, "an unknown train unit is not found");
	return ok;
}

int main() {
	bool ok = true;
	ok &= testBlockReferences();
	ok &= testInfrastructureRenames();
	ok &= testOperationRenames();
	ok &= testDeleteConsumers();
	ok &= testUniqueIds();
	ok &= testTrainUnitLookup();
	if (!ok)
		return 1;
	std::cout << "all scene reference tests passed\n";
	return 0;
}
