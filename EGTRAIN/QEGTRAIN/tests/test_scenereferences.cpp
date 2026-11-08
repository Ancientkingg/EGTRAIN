#include "scene/SceneIds.h"
#include "scene/SceneModel.h"
#include "scene/SceneReferences.h"
#include "scene/SectionInventory.h"
#include "scene/SectionReference.h"

// The Qt headers come before the undef: Qt defines `signals`, and the scene model has a member of that name.
#include <QTest>
#ifdef signals
#undef signals
#endif

#include <optional>
#include <string>
#include <utility>
#include <vector>

using Names = std::vector<std::string>;

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

class TestSceneReferences : public QObject {
	Q_OBJECT

private slots:
	void parsesSectionReferencePart_data();
	void parsesSectionReferencePart();
	void findsSwitchTransitions();
	void listsBlockReferenceComponents();
	void rewritesBlockReference();
	void renamesTrack();
	void renamesNode();
	void renamesBlock();
	void renamesStation();
	void renamesAndMovesPlatform();
	void renamesSignalAndRoute();
	void renamesTrainUnit();
	void renamesComposition();
	void renamesService();
	void renamesScenario();
	void incidentHasNoConsumers();
	void listsDeleteConsumers();
	void listsBlockConsumers();
	void listsConnectionConsumers();
	void makesUniqueOperationIds();
	void makesUniquePassengerIds();
	void makesUniqueInfrastructureIds();
	void findsSignalFailureTargets();
	void passesOverSignalNamedLikeSection();
	void findsTrainUnitById();
};

void TestSceneReferences::parsesSectionReferencePart_data() {
	QTest::addColumn<QString>("part");
	QTest::addColumn<bool>("parsed");
	QTest::addColumn<QString>("blockId");
	QTest::addColumn<QString>("offset");
	QTest::addColumn<int>("kind");
	const int block = static_cast<int>(SectionReferenceKind::Block);
	const int endpoint = static_cast<int>(SectionReferenceKind::Endpoint);
	const int other = static_cast<int>(SectionReferenceKind::Other);
	QTest::newRow("bare block") << "0-B0" << true << "0-B0" << "" << block;
	QTest::newRow("wrapped block") << "@0-B0@" << true << "0-B0" << "" << block;
	// The runtime writes a coordinate after a '-', so a negative one has two.
	QTest::newRow("signed offset") << "@0-B0@-12.5" << true << "0-B0" << "-12.5" << endpoint;
	QTest::newRow("negative offset") << "@0-B0@--1.5" << true << "0-B0" << "--1.5" << endpoint;
	QTest::newRow("separated plus") << "@0-B0@-+1" << true << "0-B0" << "-+1" << endpoint;
	// The legacy importer has always read an offset without the '-'.
	QTest::newRow("unsigned offset") << "@0-B0@5" << true << "0-B0" << "5" << endpoint;
	QTest::newRow("plus offset") << "@0-B0@+5" << true << "0-B0" << "+5" << endpoint;
	QTest::newRow("leading dot") << "@0-B0@-.5" << true << "0-B0" << "-.5" << endpoint;
	QTest::newRow("no digit") << "@0-B0@-" << true << "0-B0" << "" << other;
	QTest::newRow("three signs") << "@0-B0@---5" << true << "0-B0" << "" << other;
	QTest::newRow("two dots") << "@0-B0@-1.2.3" << true << "0-B0" << "" << other;
	QTest::newRow("text") << "@0-B0@x" << true << "0-B0" << "" << other;
	// The id ends at the second '@', so what follows is not an offset.
	QTest::newRow("third at sign") << "@0-B0@x@5" << true << "0-B0" << "" << other;
	QTest::newRow("bare with at sign") << "0-B0@5" << true << "0-B0" << "" << other;
	QTest::newRow("empty") << "" << false << "" << "" << block;
	QTest::newRow("unclosed") << "@0-B0" << false << "" << "" << block;
	QTest::newRow("at sign") << "@" << false << "" << "" << block;
	QTest::newRow("empty wrapped id") << "@@-5" << false << "" << "" << block;
}

void TestSceneReferences::parsesSectionReferencePart() {
	QFETCH(QString, part);
	QFETCH(bool, parsed);
	QFETCH(QString, blockId);
	QFETCH(QString, offset);
	QFETCH(int, kind);
	const std::optional<SectionReferencePart> result = parseSectionReferencePart(part.toStdString());
	// The part parses exactly when it names a block.
	QCOMPARE(result.has_value(), parsed);
	if (!result)
		return;
	QCOMPARE(QString::fromStdString(result->blockId), blockId);
	QCOMPARE(QString::fromStdString(result->offset), offset);
	QCOMPARE(static_cast<int>(result->kind), kind);
}

void TestSceneReferences::findsSwitchTransitions() {
	// Two endpoints joined by one '/' are a switch transition, in each offset form.
	QVERIFY(isSwitchTransitionReference("@0-B0@-1.5/@1-B1@-2"));
	QVERIFY(isSwitchTransitionReference("@0-B0@--1.5/@1-B1@-2"));
	QVERIFY(isSwitchTransitionReference("@0-B0@5/@1-B1@+6"));
	// A block, one endpoint, three endpoints or a part that is not an endpoint are not.
	QVERIFY(!isSwitchTransitionReference("@0-B0@"));
	QVERIFY(!isSwitchTransitionReference("@0-B0@-1"));
	QVERIFY(!isSwitchTransitionReference("@0-B0@-1/@1-B1@-2/@2-B2@-3"));
	QVERIFY(!isSwitchTransitionReference("@0-B0@-1/1-B1"));
	QVERIFY(!isSwitchTransitionReference("@0-B0@x@-1/@1-B1@-2"));
	QVERIFY(!isSwitchTransitionReference("/@1-B1@-2"));
	// A bare block id is wrapped for the runtime; a wrapped or joined reference is kept.
	QCOMPARE(nativeRuntimeBlockId("0-B0"), "@0-B0@");
	QCOMPARE(nativeRuntimeBlockId("@0-B0@"), "@0-B0@");
	QCOMPARE(nativeRuntimeBlockId("0-B0/1-B1"), "0-B0/1-B1");
	QCOMPARE(nativeRuntimeBlockId(""), "");
}

void TestSceneReferences::listsBlockReferenceComponents() {
	// A plain reference has one component.
	QCOMPARE(sceneBlockReferenceComponents("block-a1"), Names({"block-a1"}));
	// A joined reference lists each component.
	QCOMPARE(sceneBlockReferenceComponents("block-a2/block-b1"), Names({"block-a2", "block-b1"}));
	// A wrapped component is read without its wrapper.
	QCOMPARE(sceneBlockReferenceComponents("@block-a2@-1.000000/@block-b2@-2.000000"), Names({"block-a2", "block-b2"}));
	// An empty reference has no component.
	QCOMPARE(sceneBlockReferenceComponents(""), Names());
	// A part that opens an '@' and does not close it names no block.
	QCOMPARE(sceneBlockReferenceComponents("@block-a1/block-b1"), Names({"block-b1"}));
}

void TestSceneReferences::rewritesBlockReference() {
	// A plain reference to the block is replaced.
	QCOMPARE(sceneRewriteBlockReference("block-a1", "block-a1", "renamed"), "renamed");
	// A reference to another block is kept.
	QCOMPARE(sceneRewriteBlockReference("block-a1", "block-a2", "renamed"), "block-a1");
	// One component of a joined reference is replaced.
	QCOMPARE(sceneRewriteBlockReference("block-a2/block-b1", "block-b1", "renamed"), "block-a2/renamed");
	// A wrapped component keeps its wrapper and coordinate.
	QCOMPARE(sceneRewriteBlockReference("@block-a2@-1.000000/@block-b2@-2.000000", "block-b2", "renamed"),
		"@block-a2@-1.000000/@renamed@-2.000000");
	// A longer id that starts with the old id is kept.
	QCOMPARE(sceneRewriteBlockReference("block-a1x", "block-a1", "renamed"), "block-a1x");
	// A part that names no block is kept.
	QCOMPARE(sceneRewriteBlockReference("@block-a1/block-b1", "block-a1", "renamed"), "@block-a1/block-b1");
}

void TestSceneReferences::renamesTrack() {
	SceneModel scene = referenceScene();
	sceneRewriteTrackReferences(scene, "track-a", "track-x");
	// A track rename reaches the nodes of that track only.
	QCOMPARE(scene.nodes[0].trackId, "track-x");
	QCOMPARE(scene.nodes[2].trackId, "track-x");
	QCOMPARE(scene.nodes[3].trackId, "track-b");
	// A track rename reaches the arcs of that track only.
	QCOMPARE(scene.arcs[0].trackId, "track-x");
	QCOMPARE(scene.arcs[2].trackId, "track-b");
	// A track rename reaches the blocks of that track only.
	QCOMPARE(scene.blocks[0].trackId, "track-x");
	QCOMPARE(scene.blocks[1].trackId, "track-x");
	QCOMPARE(scene.blocks[2].trackId, "track-b");
	// A track rename reaches the signalling areas.
	QCOMPARE(scene.signallingAreas[0].trackId, "track-x");
	// A reference rewrite leaves the track itself to the caller.
	QCOMPARE(scene.tracks[0].id, "track-a");
}

void TestSceneReferences::renamesNode() {
	SceneModel scene = referenceScene();
	sceneRewriteNodeReferences(scene, "node-a1", "node-x");
	// A node rename reaches both ends of the arcs.
	QCOMPARE(scene.arcs[0].toNodeId, "node-x");
	QCOMPARE(scene.arcs[1].fromNodeId, "node-x");
	QCOMPARE(scene.arcs[0].fromNodeId, "node-a0");
	// A node rename reaches the connection end.
	QCOMPARE(scene.connections[0].fromNodeId, "node-x");
	QCOMPARE(scene.connections[0].toNodeId, "node-b2");
	// A node rename reaches the nodes of the platforms.
	QCOMPARE(scene.stations[0].platforms[0].nodeIds, Names({"node-x"}));
	QCOMPARE(scene.stations[0].platforms[1].nodeIds, Names({"node-a2"}));
}

void TestSceneReferences::renamesBlock() {
	SceneModel scene = referenceScene();
	sceneRewriteBlockReferences(scene, "block-a2", "block-x");
	// A block rename reaches the sections of routes.
	QCOMPARE(scene.routes[0].blocks, Names({"block-a1", "block-x/block-b1", "@block-b2@-2.000000"}));
	// A block rename reaches block dependencies.
	QCOMPARE(scene.blockDependencies[0].block, "block-x");
	QCOMPARE(scene.blockDependencies[0].dependsOn, "block-b1");
	// A block rename reaches single-track restrictions.
	QCOMPARE(scene.singleTrackRestrictions[0].endBlock, "block-x");
	QCOMPARE(scene.singleTrackRestrictions[0].startBlock, "block-a1");
	// A block rename reaches the exit block.
	QCOMPARE(scene.stationBoundaries[0].exitBlock, "block-x");
	// A block rename reaches the target of a signal failure.
	QCOMPARE(scene.scenarios[1].incidents[0].target, "block-x");
	// A block rename leaves breakdown targets alone.
	QCOMPARE(scene.scenarios[0].incidents[1].target, "service-1");
	SceneModel protectedScene = referenceScene();
	sceneRewriteBlockReferences(protectedScene, "block-a1", "block-y");
	// A block rename reaches the protected section of signals.
	QCOMPARE(protectedScene.signals[0].protectedSection, "block-y");
	QCOMPARE(protectedScene.routes[0].blocks[0], "block-y");
	SceneModel noExit = referenceScene();
	noExit.stationBoundaries[0].hasExitBlock = false;
	sceneRewriteBlockReferences(noExit, "block-a2", "block-x");
	// A boundary without an exit block keeps the text of the unused exit.
	QCOMPARE(noExit.stationBoundaries[0].exitBlock, "block-a2");
}

void TestSceneReferences::renamesStation() {
	SceneModel scene = referenceScene();
	sceneRewriteStationReferences(scene, "station-1", "station-x");
	// A station rename reaches the stops at that station only.
	QCOMPARE(scene.services[0].stops[0].stationId, "station-x");
	QCOMPARE(scene.services[0].stops[1].stationId, "station-x");
	QCOMPARE(scene.services[0].stops[2].stationId, "station-2");
	// A station rename reaches entrance delays.
	QCOMPARE(scene.scenarios[0].entranceDelays[0].stationId, "station-x");
	const ScenePassengerJourney& journey = scene.passengers[0].journeys[0];
	// A station rename reaches passenger journeys and legs.
	QCOMPARE(journey.originStationId, "station-x");
	QCOMPARE(journey.destinationStationId, "station-2");
	QCOMPARE(journey.legs[0].originStationId, "station-x");
	QCOMPARE(journey.legs[0].destinationStationId, "station-2");
}

void TestSceneReferences::renamesAndMovesPlatform() {
	SceneModel scene = referenceScene();
	sceneRewritePlatformReferences(scene, "station-1", "platform-1", "platform-x");
	// A platform rename reaches the stops at that platform of that station only.
	QCOMPARE(scene.services[0].stops[0].platformId, "platform-x");
	QCOMPARE(scene.services[0].stops[1].platformId, "platform-2");
	QCOMPARE(scene.services[0].stops[2].platformId, "platform-1");
	SceneModel moved = referenceScene();
	sceneMovePlatformReferences(moved, "platform-1", "station-1", "station-2");
	// Moving a platform moves the stops at that platform to the new station.
	QCOMPARE(moved.services[0].stops[0].stationId, "station-2");
	QCOMPARE(moved.services[0].stops[1].stationId, "station-1");
	QCOMPARE(moved.services[1].stops[0].stationId, "station-2");
}

void TestSceneReferences::renamesSignalAndRoute() {
	SceneModel scene = referenceScene();
	sceneRewriteSignalReferences(scene, "signal-1", "signal-x");
	// A signal rename reaches signal failures only.
	QCOMPARE(scene.scenarios[0].incidents[0].target, "signal-x");
	QCOMPARE(scene.scenarios[1].incidents[0].target, "block-a2");
	sceneRewriteRouteReferences(scene, "route-1", "route-x");
	// A route rename reaches the services on it.
	QCOMPARE(scene.services[0].route, "route-x");
	QCOMPARE(scene.services[1].route, "route-x");
}

void TestSceneReferences::renamesTrainUnit() {
	SceneModel scene = referenceScene();
	sceneRewriteTrainUnitReferences(scene, "unit-1", "unit-x");
	// A train unit rename reaches every place of the unit in compositions.
	QCOMPARE(scene.compositions[0].units, Names({"unit-x", "unit-2", "unit-x"}));
	QCOMPARE(scene.compositions[1].units, Names({"unit-2"}));
}

void TestSceneReferences::renamesComposition() {
	SceneModel scene = referenceScene();
	sceneRewriteCompositionReferences(scene, "composition-1", "composition-x");
	// A composition rename reaches the services that run it.
	QCOMPARE(scene.services[0].composition, "composition-x");
	QCOMPARE(scene.services[1].composition, "composition-2");
}

void TestSceneReferences::renamesService() {
	SceneModel scene = referenceScene();
	sceneRewriteServiceReferences(scene, "service-1", "service-x");
	// A service rename reaches entrance delays.
	QCOMPARE(scene.scenarios[0].entranceDelays[0].serviceId, "service-x");
	// A service rename reaches the target of a breakdown.
	QCOMPARE(scene.scenarios[0].incidents[1].target, "service-x");
	// A service rename reaches passenger legs.
	QCOMPARE(scene.passengers[0].journeys[0].legs[0].serviceId, "service-x");
	SceneModel signalTarget = referenceScene();
	signalTarget.scenarios[1].incidents[0].target = "service-1";
	sceneRewriteServiceReferences(signalTarget, "service-1", "service-x");
	// A service rename leaves a signal failure with the same text alone.
	QCOMPARE(signalTarget.scenarios[1].incidents[0].target, "service-1");
	// A reference rewrite leaves the service itself to the caller.
	QCOMPARE(signalTarget.services[0].id, "service-1");
}

void TestSceneReferences::renamesScenario() {
	SceneModel scene = referenceScene();
	sceneRewriteScenarioReferences(scene, "scenario-2", "scenario-x");
	// A rename of another scenario keeps the default.
	QCOMPARE(scene.defaultScenarioId, "scenario-1");
	sceneRewriteScenarioReferences(scene, "scenario-1", "scenario-x");
	// A scenario rename reaches the default scenario.
	QCOMPARE(scene.defaultScenarioId, "scenario-x");
}

// No entity refers to an incident, so its rename has nothing to rewrite.
void TestSceneReferences::incidentHasNoConsumers() {
	const SceneModel scene = referenceScene();
	QVERIFY2(consumers(scene, "incident", "incident-1").empty(), "an incident has no consumers");
}

void TestSceneReferences::listsDeleteConsumers() {
	const SceneModel scene = referenceScene();
	// A track is used by its nodes, arcs, blocks and signalling areas.
	QCOMPARE(consumers(scene, "tracks", "track-a"),
		Names({"node 'node-a0'", "node 'node-a1'", "node 'node-a2'", "arc 'arc-a1'", "arc 'arc-a2'", "block 'block-a1'",
			"block 'block-a2'", "signalling area 'area-1'"}));
	// A node is used by arc and connection ends and by platforms.
	QCOMPARE(consumers(scene, "nodes", "node-a1"),
		Names({"arc 'arc-a1' endpoint", "arc 'arc-a2' endpoint", "connection 'connection-1' endpoint",
			"platform 'platform-1' at station 'station-1'"}));
	// A station is used by stops, entrance delays and passenger journeys and legs, one line for each.
	QCOMPARE(consumers(scene, "stations", "station-1"),
		Names({"service 'service-1' stop", "entrance delay for service 'service-1' in scenario 'scenario-1'",
			"passenger 'passenger-1' journey 'journey-1'", "passenger 'passenger-1' leg 'leg-1'"}));
	// A platform is used by the stops at that platform of its own station.
	QCOMPARE(consumers(scene, "platforms", "platform-1", "station-1"), Names({"service 'service-1' stop at station 'station-1'"}));
	// A platform id used in two stations is told apart by the station.
	QCOMPARE(consumers(scene, "platforms", "platform-1", "station-2"),
		Names({"service 'service-1' stop at station 'station-2'", "service 'service-2' stop at station 'station-2'"}));
	// A signal is used by the signal failures that target it.
	QCOMPARE(consumers(scene, "signals", "signal-1"), Names({"signal-failure incident 'incident-1' in scenario 'scenario-1'"}));
	// A route is used by its services.
	QCOMPARE(consumers(scene, "routes", "route-1"), Names({"service 'service-1'", "service 'service-2'"}));
	// A train unit used twice by one composition is named once.
	QCOMPARE(consumers(scene, "train_unit", "unit-1"), Names({"composition 'composition-1'"}));
	// A train unit is used by every composition that holds it.
	QCOMPARE(consumers(scene, "train_unit", "unit-2"), Names({"composition 'composition-1'", "composition 'composition-2'"}));
	// A composition is used by the services that run it.
	QCOMPARE(consumers(scene, "composition", "composition-2"), Names({"service 'service-2'"}));
	// A service is used by breakdowns, entrance delays and passenger legs.
	QCOMPARE(consumers(scene, "service", "service-1"),
		Names({"breakdown incident 'incident-2' in scenario 'scenario-1'", "entrance delay in scenario 'scenario-1'",
			"passenger 'passenger-1' leg 'leg-1'"}));
	QVERIFY2(consumers(scene, "service", "service-2").empty(), "a service that nothing refers to has no consumers");
	QVERIFY2(consumers(scene, "arcs", "arc-a1").empty(), "an arc has no consumers");
	QVERIFY2(consumers(scene, "tracks", "track-unknown").empty(), "an unknown id has no consumers");
}

// Blocks are used through route sections, whether the section names the block or a section built from it.
void TestSceneReferences::listsBlockConsumers() {
	const SceneModel scene = referenceScene();
	// A block is used by routes, signal failures, dependencies, restrictions and station boundaries.
	QCOMPARE(consumers(scene, "blocks", "block-a2"),
		Names({"route 'route-1' section 'block-a2/block-b1'", "signal-failure incident 'incident-3' in scenario 'scenario-2'",
			"block dependency 'block-a2' -> 'block-b1'", "single-track restriction 'block-a1' -> 'block-a2'",
			"station boundary 'block-a1' -> 'block-a2'"}));
	// A block is used by the protected section of a signal.
	QCOMPARE(consumers(scene, "blocks", "block-a1"),
		Names({"route 'route-1' section 'block-a1'", "signal 'signal-1' protected section 'block-a1'",
			"single-track restriction 'block-a1' -> 'block-a2'", "station boundary 'block-a1' -> 'block-a2'"}));
	SceneModel withoutExit = scene;
	withoutExit.stationBoundaries[0].hasExitBlock = false;
	// An exit block that a boundary does not use is not a consumer.
	QCOMPARE(consumers(withoutExit, "blocks", "block-a2").size(), std::size_t(4));
}

void TestSceneReferences::listsConnectionConsumers() {
	const SceneModel scene = referenceScene();
	std::string derived;
	for (const SceneSectionDescriptor& section : buildSceneSectionInventory(scene).sections)
		if (section.sourceConnectionId == "connection-1")
			derived = section.id;
	QVERIFY2(!derived.empty(), "the connection yields a section");
	SceneModel connected = scene;
	connected.routes[0].blocks.push_back(derived);
	// A connection is used by the routes that contain a section built from it.
	QCOMPARE(consumers(connected, "connections", "connection-1"), Names({"route 'route-1' section '" + derived + "'"}));
	QVERIFY2(consumers(scene, "connections", "connection-1").empty(), "an unused connection has no consumers");
}

void TestSceneReferences::makesUniqueOperationIds() {
	const SceneModel scene = referenceScene();
	// A free id is kept.
	QCOMPARE(sceneUniqueTrainUnitId(scene, "unit-new"), "unit-new");
	// A taken id gets the suffix _2.
	QCOMPARE(sceneUniqueTrainUnitId(scene, "unit-1"), "unit-1_2");
	SceneModel crowded = scene;
	SceneTrainUnit third;
	third.id = "unit-1_2";
	crowded.trainUnits.push_back(third);
	// The suffix counts up to the first free one.
	QCOMPARE(sceneUniqueTrainUnitId(crowded, "unit-1"), "unit-1_3");
	// Compositions are unique.
	QCOMPARE(sceneUniqueCompositionId(scene, "composition-1"), "composition-1_2");
	// Services are unique.
	QCOMPARE(sceneUniqueServiceId(scene, "service-2"), "service-2_2");
	// Scenarios are unique.
	QCOMPARE(sceneUniqueScenarioId(scene, "scenario-1"), "scenario-1_2");
	// An empty scenario id becomes scenario.
	QCOMPARE(sceneUniqueScenarioId(scene, ""), "scenario");
	// Incident ids are unique across the scenarios.
	QCOMPARE(sceneUniqueIncidentId(scene, "incident-3"), "incident-3_2");
	// A free incident id is kept.
	QCOMPARE(sceneUniqueIncidentId(scene, "incident-4"), "incident-4");
}

void TestSceneReferences::makesUniquePassengerIds() {
	const SceneModel scene = referenceScene();
	// Passengers are unique.
	QCOMPARE(sceneUniquePassengerId(scene, "passenger-1"), "passenger-1_2");
	// An empty passenger id becomes new_passenger.
	QCOMPARE(sceneUniquePassengerId(scene, ""), "new_passenger");
	// Journeys are unique.
	QCOMPARE(sceneUniquePassengerJourneyId(scene, "journey-1"), "journey-1_2");
	// An empty journey id becomes new_journey.
	QCOMPARE(sceneUniquePassengerJourneyId(scene, ""), "new_journey");
	// Legs are unique.
	QCOMPARE(sceneUniquePassengerLegId(scene, "leg-1"), "leg-1_2");
	// An empty leg id becomes new_leg.
	QCOMPARE(sceneUniquePassengerLegId(scene, ""), "new_leg");
	SceneModel twoPassengers = scene;
	ScenePassenger second;
	second.id = "passenger-2";
	ScenePassengerJourney journey;
	journey.id = "journey-2";
	journey.legs = {leg("leg-2", "", "", "")};
	second.journeys = {journey};
	twoPassengers.passengers.push_back(second);
	// Journey and leg ids are unique across all passengers.
	QCOMPARE(sceneUniquePassengerJourneyId(twoPassengers, "journey-2"), "journey-2_2");
	QCOMPARE(sceneUniquePassengerLegId(twoPassengers, "leg-2"), "leg-2_2");
}

void TestSceneReferences::makesUniqueInfrastructureIds() {
	const SceneModel scene = referenceScene();
	// Track ids are unique.
	QCOMPARE(sceneUniqueInfrastructureId(scene, "track-a", "tracks"), "track-a_2");
	// Node ids are unique.
	QCOMPARE(sceneUniqueInfrastructureId(scene, "node-a0", "nodes"), "node-a0_2");
	// Arc ids are unique.
	QCOMPARE(sceneUniqueInfrastructureId(scene, "arc-a1", "arcs"), "arc-a1_2");
	// Block ids are unique.
	QCOMPARE(sceneUniqueInfrastructureId(scene, "block-a1", "blocks"), "block-a1_2");
	// Connection ids are unique.
	QCOMPARE(sceneUniqueInfrastructureId(scene, "connection-1", "connections"), "connection-1_2");
	// Station ids are unique.
	QCOMPARE(sceneUniqueInfrastructureId(scene, "station-1", "stations"), "station-1_2");
	// Signal ids are unique.
	QCOMPARE(sceneUniqueInfrastructureId(scene, "signal-1", "signals"), "signal-1_2");
	// Signalling area ids are unique.
	QCOMPARE(sceneUniqueInfrastructureId(scene, "area-1", "signalling_areas"), "area-1_2");
	// Route ids are unique.
	QCOMPARE(sceneUniqueInfrastructureId(scene, "route-1", "routes"), "route-1_2");
	// Platform ids are unique within the first station.
	QCOMPARE(sceneUniqueInfrastructureId(scene, "platform-1", "platforms"), "platform-1_2");
	// Platform ids are checked in the first station.
	QCOMPARE(sceneUniqueInfrastructureId(scene, "platform-2", "platforms"), "platform-2_2");
	SceneModel otherFirst = scene;
	std::swap(otherFirst.stations[0], otherFirst.stations[1]);
	// Platform ids of a later station are not taken into account.
	QCOMPARE(sceneUniqueInfrastructureId(otherFirst, "platform-2", "platforms"), "platform-2");
	// An id is only compared within its own table.
	QCOMPARE(sceneUniqueInfrastructureId(scene, "track-a", "nodes"), "track-a");
}

void TestSceneReferences::findsSignalFailureTargets() {
	SceneModel scene = referenceScene();
	SceneSectionInventory inventory = buildSceneSectionInventory(scene);
	std::string switchSection;
	for (const SceneSectionDescriptor& section : inventory.sections)
		if (section.connectionDerived && switchSection.empty())
			switchSection = section.id;
	// A section that one signal protects gives that signal.
	QCOMPARE(sceneSignalFailureTarget(scene, inventory, "@block-a1@"), "signal-1");
	// The block id names the same section.
	QCOMPARE(sceneSignalFailureTarget(scene, inventory, "block-a1"), "signal-1");
	// A section that no signal protects gives its block.
	QCOMPARE(sceneSignalFailureTarget(scene, inventory, "@block-a2@"), "block-a2");
	// A switch section that no signal protects gives no target.
	QVERIFY(!switchSection.empty());
	QVERIFY(sceneSignalFailureTarget(scene, inventory, switchSection).empty());
	// An id that names no section gives no target.
	QVERIFY(sceneSignalFailureTarget(scene, inventory, "@block-x@").empty());
	QVERIFY(sceneSignalFailureTarget(scene, inventory, "").empty());

	SceneSignal second;
	second.id = "signal-2";
	second.protectedSection = "@block-a1@";
	scene.signals.push_back(second);
	SceneSignal atSwitch;
	atSwitch.id = "signal-3";
	atSwitch.protectedSection = switchSection;
	scene.signals.push_back(atSwitch);
	inventory = buildSceneSectionInventory(scene);
	// A section that two signals protect gives its block.
	QCOMPARE(sceneSignalFailureTarget(scene, inventory, "@block-a1@"), "block-a1");
	// A switch section that one signal protects gives that signal.
	QVERIFY(!switchSection.empty());
	QCOMPARE(sceneSignalFailureTarget(scene, inventory, switchSection), "signal-3");
}

void TestSceneReferences::passesOverSignalNamedLikeSection() {
	SceneModel scene = referenceScene();
	SceneSignal named;
	named.id = "block-b2";
	named.protectedSection = "block-b1";
	scene.signals.push_back(named);
	const SceneSectionInventory inventory = buildSceneSectionInventory(scene);
	// A signal whose id also names a section is passed over for the block.
	QCOMPARE(sceneSignalFailureTarget(scene, inventory, "@block-b1@"), "block-b1");
	QVERIFY2(sceneSignalFailureTarget(scene, inventory, "@block-b2@").empty(),
		"a block whose id is also the id of a signal gives no target");
}

void TestSceneReferences::findsTrainUnitById() {
	const SceneModel scene = referenceScene();
	// A train unit is found by id.
	QCOMPARE(sceneTrainUnitById(scene, "unit-2"), &scene.trainUnits[1]);
	// An unknown train unit is not found.
	QCOMPARE(sceneTrainUnitById(scene, "unit-9"), nullptr);
}

QTEST_APPLESS_MAIN(TestSceneReferences)
#include "test_scenereferences.moc"
