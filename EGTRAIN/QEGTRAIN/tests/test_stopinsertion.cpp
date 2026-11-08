#include "scene/SceneModel.h"
#include "scene/SectionInventory.h"
#include "scene/StopInsertion.h"

#include <QByteArray>
#include <QStringList>
#include <QTest>

#include <string>
#include <vector>

static bool contains(const std::string& text, const std::string& part) {
	return text.find(part) != std::string::npos;
}

static bool sameStop(const SceneStop& left, const SceneStop& right) {
	return left.stationId == right.stationId && left.platformId == right.platformId
		&& left.hasPlannedArrival == right.hasPlannedArrival
		&& left.hasPlannedDeparture == right.hasPlannedDeparture
		&& left.plannedArrivalSeconds == right.plannedArrivalSeconds
		&& left.plannedDepartureSeconds == right.plannedDepartureSeconds
		&& left.dwellSeconds == right.dwellSeconds;
}

static bool sameStops(const std::vector<SceneStop>& left, const std::vector<SceneStop>& right) {
	if (left.size() != right.size())
		return false;
	for (std::size_t index = 0; index < left.size(); ++index)
		if (!sameStop(left[index], right[index]))
			return false;
	return true;
}

// Four platform stations on one track. A, B, C and D are visited in this order.
// With `loop`, the route has five nodes and visits A on platform-a1, then B, then
// A on platform-a2.
static SceneModel lineScene(bool loop) {
	SceneModel scene;
	scene.schemaVersion = 1;
	scene.name = "Stop insertion scene";
	scene.baseTime = "08:00:00";
	scene.tracks.push_back({"track-1"});
	const int nodeCount = loop ? 5 : 4;
	for (int index = 0; index < nodeCount; ++index)
		scene.nodes.push_back({"node-" + std::to_string(index + 1), "track-1",
			static_cast<double>(index), 0.0});
	for (int index = 1; index < nodeCount; ++index)
		scene.arcs.push_back({"arc-" + std::to_string(index), "track-1",
			"node-" + std::to_string(index), "node-" + std::to_string(index + 1), 0.0, 0.0, 40.0});
	for (int index = 1; index < nodeCount; ++index)
		scene.blocks.push_back({"block-" + std::to_string(index), "track-1", 1.0});

	const auto station = [&scene](const std::string& id, const std::string& name,
							 std::vector<ScenePlatform> platforms) {
		SceneStation entry;
		entry.id = id;
		entry.name = name;
		entry.platforms = std::move(platforms);
		scene.stations.push_back(entry);
	};
	if (loop) {
		station("station-a", "A", {{"platform-a1", {"node-1"}}, {"platform-a2", {"node-5"}}});
		station("station-b", "B", {{"platform-b", {"node-3"}}});
	} else {
		station("station-a", "A", {{"platform-a", {"node-1"}}});
		station("station-b", "B", {{"platform-b", {"node-2"}}});
		station("station-c", "C", {{"platform-c", {"node-3"}}});
		station("station-d", "D", {{"platform-d", {"node-4"}}});
	}
	// No platform and no position: a stop here is off-route schedule context.
	station("station-off", "Off", {});

	SceneRoute route;
	route.id = "route-1";
	for (int index = 1; index < nodeCount; ++index)
		route.blocks.push_back("block-" + std::to_string(index));
	scene.routes.push_back(route);

	SceneService service;
	service.id = "service-1";
	service.route = "route-1";
	scene.services.push_back(service);
	return scene;
}

static SceneStop stop(const std::string& stationId, const std::string& platformId, double arrival,
	double departure) {
	SceneStop result;
	result.stationId = stationId;
	result.platformId = platformId;
	result.hasPlannedArrival = true;
	result.plannedArrivalSeconds = arrival;
	result.hasPlannedDeparture = true;
	result.plannedDepartureSeconds = departure;
	result.dwellSeconds = departure - arrival;
	return result;
}

static SceneStop lineStop(char letter, double arrival) {
	const std::string lower(1, static_cast<char>(letter + ('a' - 'A')));
	return stop("station-" + lower, "platform-" + lower, arrival, arrival + 30.0);
}

static std::vector<std::string> stationsOf(const SceneRouteTraversal& traversal) {
	std::vector<std::string> result;
	for (const auto& visit : traversal.visits)
		result.push_back(visit.stationId);
	return result;
}

static std::vector<std::string> platformsOf(const SceneRouteTraversal& traversal,
	const std::string& stationId) {
	std::vector<std::string> result;
	for (const auto& visit : traversal.visits)
		if (visit.stationId == stationId)
			result.push_back(visit.platformId);
	return result;
}

static std::vector<std::string> stopStations(const SceneService& service) {
	std::vector<std::string> result;
	for (const auto& entry : service.stops)
		result.push_back(entry.stationId);
	return result;
}

using Names = std::vector<std::string>;

// The scene with `stops` as the timetable of its service.
static SceneModel withStops(const SceneModel& base, std::vector<SceneStop> stops) {
	SceneModel scene = base;
	scene.services[0].stops = std::move(stops);
	return scene;
}

// A stop is inserted into a timetable only where the route of the service passes the station between the
// neighbouring stops, and the other stops are kept exactly.
class TestStopInsertion : public QObject {
	Q_OBJECT

private slots:
	void serviceTraversal();
	void remainingTraversal();
	void beforeFirstStop();
	void betweenTwoStops();
	void afterLastStop();
	void destinationAlreadyPresent();
	void incompatibleOrder();
	void repeatedStationsAndLoops();
	void platformReachedTwice();
	void unresolvedNeighbour();
	void existingStopsUnchanged();
	void windowHoldsVisitsBetweenNeighbours_data();
	void windowHoldsVisitsBetweenNeighbours();
	void windowIgnoresOffRouteStops();

private:
	const SceneModel m_line = lineScene(false);
	const SceneModel m_loop = lineScene(true);
};

void TestStopInsertion::serviceTraversal() {
	// The service traversal visits the stations of the route in order.
	QCOMPARE(stationsOf(sceneServiceTraversal(m_line, m_line.services[0])), Names({"station-a", "station-b", "station-c", "station-d"}));
	// The service traversal keeps the second visit of a repeated station.
	QCOMPARE(stationsOf(sceneServiceTraversal(m_loop, m_loop.services[0])), Names({"station-a", "station-b", "station-a"}));
	QCOMPARE(platformsOf(sceneServiceTraversal(m_loop, m_loop.services[0]), "station-a"), Names({"platform-a1", "platform-a2"}));
	SceneService noRoute = m_line.services[0];
	noRoute.route = "missing";
	// A service without a route has an unresolved traversal.
	QVERIFY(!sceneServiceTraversal(m_line, noRoute).resolved);
	QVERIFY(sceneServiceTraversal(m_line, noRoute).visits.empty());
}

void TestStopInsertion::remainingTraversal() {
	const SceneModel bc = withStops(m_line, {lineStop('B', 100.0), lineStop('C', 200.0)});
	// The remaining traversal starts after the last resolved stop before the index.
	QCOMPARE(stationsOf(sceneRemainingStopTraversal(bc, bc.services[0], 0)), Names({"station-a", "station-b", "station-c", "station-d"}));
	QCOMPARE(stationsOf(sceneRemainingStopTraversal(bc, bc.services[0], 1)), Names({"station-c", "station-d"}));
	QCOMPARE(stationsOf(sceneRemainingStopTraversal(bc, bc.services[0], 2)), Names({"station-d"}));
	QCOMPARE(stationsOf(sceneRemainingStopTraversal(bc, bc.services[0], 9)), Names({"station-d"}));
	const SceneModel offRoute = withStops(m_line, {lineStop('A', 100.0), stop("station-off", "", 150.0, 160.0)});
	// The remaining traversal skips a stop that is off-route context.
	QCOMPARE(stationsOf(sceneRemainingStopTraversal(offRoute, offRoute.services[0], 2)), Names({"station-b", "station-c", "station-d"}));
	const SceneModel unknownFirst = withStops(m_line, {stop("station-x", "", 0.0, 0.0), lineStop('C', 200.0)});
	// The remaining traversal is empty after a stop that does not resolve.
	QVERIFY(sceneRemainingStopTraversal(unknownFirst, unknownFirst.services[0], 1).visits.empty());
	QCOMPARE(stationsOf(sceneRemainingStopTraversal(unknownFirst, unknownFirst.services[0], 0)).size(), std::size_t(4));
}

// a. Before the first stop.
void TestStopInsertion::beforeFirstStop() {
	SceneModel scene = withStops(m_line, {lineStop('B', 100.0), lineStop('C', 200.0)});
	const auto window = sceneStopInsertionWindow(scene, scene.services[0], 0);
	// The window before the first stop holds only the visits before it.
	QVERIFY(window.ok);
	QCOMPARE(stationsOf(window.visits), Names({"station-a"}));
	const auto result = insertSceneStop(scene, scene.services[0], 0, lineStop('A', 20.0));
	// A stop is inserted before the first stop.
	QVERIFY(result.inserted);
	QCOMPARE(result.error, std::string());
	QCOMPARE(stopStations(scene.services[0]), Names({"station-a", "station-b", "station-c"}));
}

// b. Between two stops.
void TestStopInsertion::betweenTwoStops() {
	SceneModel scene = withStops(m_line, {lineStop('A', 20.0), lineStop('D', 300.0)});
	const auto window = sceneStopInsertionWindow(scene, scene.services[0], 1);
	// The window between two stops holds the visits between them.
	QVERIFY(window.ok);
	QCOMPARE(stationsOf(window.visits), Names({"station-b", "station-c"}));
	QCOMPARE(platformsOf(window.visits, "station-c"), Names({"platform-c"}));
	const auto result = insertSceneStop(scene, scene.services[0], 1, lineStop('C', 200.0));
	// A stop is inserted between two stops.
	QVERIFY(result.inserted);
	QCOMPARE(stopStations(scene.services[0]), Names({"station-a", "station-c", "station-d"}));
}

// c. After the last stop.
void TestStopInsertion::afterLastStop() {
	SceneModel scene = withStops(m_line, {lineStop('A', 20.0), lineStop('B', 100.0)});
	const auto window = sceneStopInsertionWindow(scene, scene.services[0], 2);
	// The window after the last stop holds the rest of the route.
	QVERIFY(window.ok);
	QCOMPARE(stationsOf(window.visits), Names({"station-c", "station-d"}));
	const auto result = insertSceneStop(scene, scene.services[0], 2, lineStop('D', 300.0));
	// A stop is inserted after the last stop.
	QVERIFY(result.inserted);
	QCOMPARE(stopStations(scene.services[0]), Names({"station-a", "station-b", "station-d"}));
}

// d. The destination is already in the timetable.
void TestStopInsertion::destinationAlreadyPresent() {
	SceneModel scene = withStops(m_line, {lineStop('A', 20.0), lineStop('C', 200.0), lineStop('D', 300.0)});
	const auto last = sceneStopInsertionWindow(scene, scene.services[0], 3);
	// No visit remains after the last stop when the destination is present.
	QVERIFY(last.ok);
	QVERIFY(last.visits.visits.empty());
	QVERIFY(sceneRemainingStopTraversal(scene, scene.services[0], 3).visits.empty());
	// A stop cannot be inserted after the destination.
	QVERIFY(!insertSceneStop(scene, scene.services[0], 3, lineStop('D', 400.0)).inserted);
	QCOMPARE(scene.services[0].stops.size(), std::size_t(3));
	const auto middle = sceneStopInsertionWindow(scene, scene.services[0], 1);
	// The same timetable offers the station in the middle.
	QVERIFY(middle.ok);
	QCOMPARE(stationsOf(middle.visits), Names({"station-b"}));
	// The same timetable accepts an insertion in the middle.
	QVERIFY(insertSceneStop(scene, scene.services[0], 1, lineStop('B', 100.0)).inserted);
	QCOMPARE(stopStations(scene.services[0]), Names({"station-a", "station-b", "station-c", "station-d"}));
}

// e. Incompatible order.
void TestStopInsertion::incompatibleOrder() {
	SceneModel scene = withStops(m_line, {lineStop('A', 20.0), lineStop('C', 200.0)});
	const SceneService before = scene.services[0];
	const auto refused = insertSceneStop(scene, scene.services[0], 1, lineStop('D', 300.0));
	// An insertion that strands a later stop is refused and names that stop.
	QVERIFY(!refused.inserted);
	QVERIFY(contains(refused.error, "stop 2"));
	QVERIFY(contains(refused.error, "(C)"));
	// A refused insertion leaves the service as it was.
	QCOMPARE(scene.services[0].id, before.id);
	QCOMPARE(scene.services[0].route, before.route);
	QVERIFY(sameStops(scene.services[0].stops, before.stops));
	// A compatible station is inserted at the same position.
	QVERIFY(insertSceneStop(scene, scene.services[0], 1, lineStop('B', 100.0)).inserted);
	QCOMPARE(stopStations(scene.services[0]), Names({"station-a", "station-b", "station-c"}));
	const auto outOfRange = insertSceneStop(scene, scene.services[0], 9, lineStop('D', 300.0));
	// A position past the end is refused.
	QVERIFY(!outOfRange.inserted);
	QCOMPARE(scene.services[0].stops.size(), std::size_t(3));
	QVERIFY(!sceneStopInsertionWindow(scene, scene.services[0], 9).ok);
	SceneModel unknownStation = withStops(m_line, {lineStop('A', 20.0), lineStop('C', 200.0)});
	const auto unknown = insertSceneStop(unknownStation, unknownStation.services[0], 1, stop("station-x", "", 0.0, 0.0));
	// A station that does not exist is refused.
	QVERIFY(!unknown.inserted);
	QCOMPARE(unknownStation.services[0].stops.size(), std::size_t(2));
}

// f. Repeated stations and loops.
void TestStopInsertion::repeatedStationsAndLoops() {
	SceneModel scene = withStops(m_loop, {stop("station-a", "platform-a1", 20.0, 50.0)});
	const auto window = sceneStopInsertionWindow(scene, scene.services[0], 1);
	// The window after the first visit offers B and the second visit of A.
	QVERIFY(window.ok);
	QCOMPARE(stationsOf(window.visits), Names({"station-b", "station-a"}));
	QCOMPARE(platformsOf(window.visits, "station-a"), Names({"platform-a2"}));
	const auto second = insertSceneStop(scene, scene.services[0], 1, stop("station-a", "platform-a2", 300.0, 330.0));
	// The second visit of A is inserted after the first.
	QVERIFY(second.inserted);
	QCOMPARE(scene.services[0].stops.size(), std::size_t(2));
	QCOMPARE(scene.services[0].stops[1].platformId, std::string("platform-a2"));

	SceneModel both = withStops(m_loop, {stop("station-a", "platform-a1", 20.0, 50.0), stop("station-a", "platform-a2", 300.0, 330.0)});
	const SceneService before = both.services[0];
	const auto between = sceneStopInsertionWindow(both, both.services[0], 1);
	// A is not offered between two stops that use both of its visits.
	QVERIFY(between.ok);
	QCOMPARE(stationsOf(between.visits), Names({"station-b"}));
	for (const char* platform : {"platform-a1", "platform-a2", ""}) {
		const auto refused = insertSceneStop(both, both.services[0], 1, stop("station-a", platform, 100.0, 130.0));
		// A is refused between two stops that use both of its visits.
		const QByteArray message = "A is refused between two stops that use both of its visits, platform '" + QByteArray(platform) + "'";
		QVERIFY2(!refused.inserted, message.constData());
		QVERIFY2(sameStops(both.services[0].stops, before.stops), message.constData());
	}
	QVERIFY2(insertSceneStop(both, both.services[0], 1, stop("station-b", "platform-b", 100.0, 130.0)).inserted,
		"B is accepted between the two visits of A");

	SceneModel secondOnly = withStops(m_loop, {stop("station-b", "platform-b", 100.0, 130.0)});
	const SceneService beforeAmbiguous = secondOnly.services[0];
	const auto ambiguous = insertSceneStop(secondOnly, secondOnly.services[0], 0, stop("station-a", "", 20.0, 50.0));
	// A stop without a platform at a station with two reachable platforms asks for a platform.
	QVERIFY(!ambiguous.inserted);
	QCOMPARE(ambiguous.error, std::string("The new stop (A) has no platform and the route reaches more than one here. Choose a platform."));
	QVERIFY(sameStops(secondOnly.services[0].stops, beforeAmbiguous.stops));
	QVERIFY2(insertSceneStop(secondOnly, secondOnly.services[0], 0, stop("station-a", "platform-a1", 20.0, 50.0)).inserted,
		"the same stop with a platform is accepted");
}

// A platform that the route reaches twice: the new stop must not take the visit of an existing stop.
void TestStopInsertion::platformReachedTwice() {
	SceneModel samePlatform = m_loop;
	samePlatform.stations[0].platforms = {{"platform-a", {"node-1", "node-5"}}};
	SceneModel scene = withStops(samePlatform, {stop("station-a", "platform-a", 20.0, 50.0)});
	const SceneService before = scene.services[0];
	const auto window = sceneStopInsertionWindow(scene, scene.services[0], 0);
	// No visit is free before a stop that uses the first of two visits of one platform.
	QVERIFY(window.ok);
	QVERIFY(window.visits.visits.empty());
	const auto refused = insertSceneStop(scene, scene.services[0], 0, stop("station-a", "platform-a", 10.0, 15.0));
	// A stop that would move an existing stop to another visit is refused and names it.
	QVERIFY(!refused.inserted);
	QVERIFY(contains(refused.error, "stop 1"));
	QVERIFY(contains(refused.error, "(A)"));
	QVERIFY(sameStops(scene.services[0].stops, before.stops));
	QVERIFY2(insertSceneStop(scene, scene.services[0], 1, stop("station-a", "platform-a", 300.0, 330.0)).inserted,
		"the same stop is accepted after the existing stop");
}

// g. An unresolved neighbour.
void TestStopInsertion::unresolvedNeighbour() {
	SceneModel scene = withStops(m_line, {stop("station-x", "", 0.0, 0.0), lineStop('C', 200.0)});
	const auto window = sceneStopInsertionWindow(scene, scene.services[0], 1);
	// An unresolved stop before the position is reported as the obstacle.
	QVERIFY(!window.ok);
	QCOMPARE(window.blockingStop, std::size_t(0));
	QVERIFY(contains(window.problem, "stop 1"));
	QVERIFY(contains(window.problem, "station-x"));
	QVERIFY(window.visits.visits.empty());
	const auto first = sceneStopInsertionWindow(scene, scene.services[0], 0);
	// The position before an unresolved stop still has a window.
	QVERIFY(first.ok);
	QCOMPARE(stationsOf(first.visits), Names({"station-a", "station-b"}));
	SceneModel noStation = withStops(m_line, {stop("", "", 0.0, 0.0), lineStop('C', 200.0)});
	const auto afterNoStation = sceneStopInsertionWindow(noStation, noStation.services[0], 1);
	// A stop without a station is named without empty brackets.
	QCOMPARE(afterNoStation.problem, std::string("Cannot insert after stop 1: it has no station. Fix that stop first."));
	SceneModel empty = withStops(m_line, {});
	const auto newWithoutStation = insertSceneStop(empty, empty.services[0], 0, stop("", "", 0.0, 0.0));
	// A new stop without a station is named without empty brackets.
	QCOMPARE(newWithoutStation.error, std::string("The new stop has no station. Choose another position, station or platform."));
	SceneModel noRouteScene = m_line;
	noRouteScene.services[0].route = "missing";
	const auto unresolved = sceneStopInsertionWindow(noRouteScene, noRouteScene.services[0], 0);
	// A service without a route has no window.
	QVERIFY(!unresolved.ok);
	QCOMPARE(unresolved.blockingStop, SceneStopInsertionWindow::kNoStop);
	QVERIFY(!unresolved.problem.empty());
	QVERIFY(!insertSceneStop(noRouteScene, noRouteScene.services[0], 0, lineStop('A', 20.0)).inserted);
}

// h. Existing stops and their times are unchanged.
void TestStopInsertion::existingStopsUnchanged() {
	SceneStop first = lineStop('A', 20.0);
	SceneStop last = lineStop('D', 300.0);
	last.hasPlannedArrival = false;
	last.dwellSeconds = 12.5;
	SceneModel scene = withStops(m_line, {first, last});
	SceneStop added = stop("station-b", "platform-b", 111.0, 141.0);
	added.dwellSeconds = 7.0;
	// Existing stops and the planned times of the new stop are kept exactly.
	QVERIFY(insertSceneStop(scene, scene.services[0], 1, added).inserted);
	QCOMPARE(scene.services[0].stops.size(), std::size_t(3));
	QVERIFY(sameStop(scene.services[0].stops[0], first));
	QVERIFY(sameStop(scene.services[0].stops[1], added));
	QVERIFY(sameStop(scene.services[0].stops[2], last));
	SceneStop untimed;
	untimed.stationId = "station-c";
	untimed.platformId = "platform-c";
	// No planned time is invented for the inserted stop.
	QVERIFY(insertSceneStop(scene, scene.services[0], 2, untimed).inserted);
	QVERIFY(sameStop(scene.services[0].stops[2], untimed));
	QVERIFY(!scene.services[0].stops[2].hasPlannedArrival);
	QVERIFY(!scene.services[0].stops[2].hasPlannedDeparture);
}

// i. A window holds nothing before the previous stop or after the next stop.
void TestStopInsertion::windowHoldsVisitsBetweenNeighbours_data() {
	QTest::addColumn<int>("position");
	QTest::addColumn<QStringList>("stations");
	QTest::newRow("before B") << 0 << QStringList{"station-a"};
	QTest::newRow("between B and C") << 1 << QStringList{};
	QTest::newRow("after C") << 2 << QStringList{"station-d"};
}

// Each window holds exactly the visits between its neighbours.
void TestStopInsertion::windowHoldsVisitsBetweenNeighbours() {
	QFETCH(int, position);
	QFETCH(QStringList, stations);
	Names expected;
	for (const QString& station : stations)
		expected.push_back(station.toStdString());
	const SceneModel scene = withStops(m_line, {lineStop('B', 100.0), lineStop('C', 200.0)});
	const auto window = sceneStopInsertionWindow(scene, scene.services[0], static_cast<std::size_t>(position));
	QVERIFY(window.ok);
	QCOMPARE(stationsOf(window.visits), expected);
}

void TestStopInsertion::windowIgnoresOffRouteStops() {
	const SceneModel off = withStops(m_line, {lineStop('A', 20.0), stop("station-off", "", 1.0, 2.0), lineStop('C', 200.0)});
	// A stop that is off-route context does not bound the window.
	QCOMPARE(stationsOf(sceneStopInsertionWindow(off, off.services[0], 2).visits), Names({"station-b"}));
	// A stop that is off-route context after the position does not bound the window.
	QCOMPARE(stationsOf(sceneStopInsertionWindow(off, off.services[0], 1).visits), Names({"station-b"}));
	const SceneModel empty = withStops(m_line, {});
	const auto whole = sceneStopInsertionWindow(empty, empty.services[0], 0);
	// An empty timetable offers every station of the route.
	QVERIFY(whole.ok);
	QCOMPARE(stationsOf(whole.visits), Names({"station-a", "station-b", "station-c", "station-d"}));
}

QTEST_APPLESS_MAIN(TestStopInsertion)
#include "test_stopinsertion.moc"
