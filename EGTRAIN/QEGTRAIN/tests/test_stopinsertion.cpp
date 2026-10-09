#include "scene/SceneModel.h"
#include "scene/SectionInventory.h"
#include "scene/StopInsertion.h"

#include <iostream>
#include <string>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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

int main() {
	bool ok = true;
	const SceneModel line = lineScene(false);
	const SceneModel loop = lineScene(true);

	auto withStops = [](const SceneModel& base, std::vector<SceneStop> stops) {
		SceneModel scene = base;
		scene.services[0].stops = std::move(stops);
		return scene;
	};

	// Traversal helpers.
	ok &= expect(stationsOf(sceneServiceTraversal(line, line.services[0]))
			== Names({"station-a", "station-b", "station-c", "station-d"}),
		"service traversal visits the stations of the route in order");
	ok &= expect(stationsOf(sceneServiceTraversal(loop, loop.services[0]))
				== Names({"station-a", "station-b", "station-a"})
			&& platformsOf(sceneServiceTraversal(loop, loop.services[0]), "station-a")
				== Names({"platform-a1", "platform-a2"}),
		"service traversal keeps the second visit of a repeated station");
	SceneService noRoute = line.services[0];
	noRoute.route = "missing";
	ok &= expect(!sceneServiceTraversal(line, noRoute).resolved
			&& sceneServiceTraversal(line, noRoute).visits.empty(),
		"a service without a route has an unresolved traversal");

	const SceneModel bc = withStops(line, {lineStop('B', 100.0), lineStop('C', 200.0)});
	ok &= expect(stationsOf(sceneRemainingStopTraversal(bc, bc.services[0], 0))
				== Names({"station-a", "station-b", "station-c", "station-d"})
			&& stationsOf(sceneRemainingStopTraversal(bc, bc.services[0], 1))
				== Names({"station-c", "station-d"})
			&& stationsOf(sceneRemainingStopTraversal(bc, bc.services[0], 2))
				== Names({"station-d"})
			&& stationsOf(sceneRemainingStopTraversal(bc, bc.services[0], 9))
				== Names({"station-d"}),
		"remaining traversal starts after the last resolved stop before the index");
	const SceneModel offRoute = withStops(line, {lineStop('A', 100.0), stop("station-off", "", 150.0, 160.0)});
	ok &= expect(stationsOf(sceneRemainingStopTraversal(offRoute, offRoute.services[0], 2))
			== Names({"station-b", "station-c", "station-d"}),
		"remaining traversal skips a stop that is off-route context");
	const SceneModel unknownFirst = withStops(line, {stop("station-x", "", 0.0, 0.0), lineStop('C', 200.0)});
	ok &= expect(sceneRemainingStopTraversal(unknownFirst, unknownFirst.services[0], 1).visits.empty()
			&& stationsOf(sceneRemainingStopTraversal(unknownFirst, unknownFirst.services[0], 0)).size() == 4,
		"remaining traversal is empty after a stop that does not resolve");

	// a. Before the first stop.
	{
		SceneModel scene = withStops(line, {lineStop('B', 100.0), lineStop('C', 200.0)});
		const auto window = sceneStopInsertionWindow(scene, scene.services[0], 0);
		ok &= expect(window.ok && stationsOf(window.visits) == Names({"station-a"}),
			"the window before the first stop holds only the visits before it");
		const auto result = insertSceneStop(scene, scene.services[0], 0, lineStop('A', 20.0));
		ok &= expect(result.inserted && result.error.empty()
				&& stopStations(scene.services[0]) == Names({"station-a", "station-b", "station-c"}),
			"a stop is inserted before the first stop");
	}
	// b. Between two stops.
	{
		SceneModel scene = withStops(line, {lineStop('A', 20.0), lineStop('D', 300.0)});
		const auto window = sceneStopInsertionWindow(scene, scene.services[0], 1);
		ok &= expect(window.ok && stationsOf(window.visits) == Names({"station-b", "station-c"})
				&& platformsOf(window.visits, "station-c") == Names({"platform-c"}),
			"the window between two stops holds the visits between them");
		const auto result = insertSceneStop(scene, scene.services[0], 1, lineStop('C', 200.0));
		ok &= expect(result.inserted
				&& stopStations(scene.services[0]) == Names({"station-a", "station-c", "station-d"}),
			"a stop is inserted between two stops");
	}
	// c. After the last stop.
	{
		SceneModel scene = withStops(line, {lineStop('A', 20.0), lineStop('B', 100.0)});
		const auto window = sceneStopInsertionWindow(scene, scene.services[0], 2);
		ok &= expect(window.ok && stationsOf(window.visits) == Names({"station-c", "station-d"}),
			"the window after the last stop holds the rest of the route");
		const auto result = insertSceneStop(scene, scene.services[0], 2, lineStop('D', 300.0));
		ok &= expect(result.inserted
				&& stopStations(scene.services[0]) == Names({"station-a", "station-b", "station-d"}),
			"a stop is inserted after the last stop");
	}
	// d. The destination is already in the timetable.
	{
		SceneModel scene = withStops(line, {lineStop('A', 20.0), lineStop('C', 200.0), lineStop('D', 300.0)});
		const auto last = sceneStopInsertionWindow(scene, scene.services[0], 3);
		ok &= expect(last.ok && last.visits.visits.empty()
				&& sceneRemainingStopTraversal(scene, scene.services[0], 3).visits.empty(),
			"no visit remains after the last stop when the destination is present");
		ok &= expect(!insertSceneStop(scene, scene.services[0], 3, lineStop('D', 400.0)).inserted
				&& scene.services[0].stops.size() == 3,
			"a stop cannot be inserted after the destination");
		const auto middle = sceneStopInsertionWindow(scene, scene.services[0], 1);
		ok &= expect(middle.ok && stationsOf(middle.visits) == Names({"station-b"}),
			"the same timetable offers the station in the middle");
		ok &= expect(insertSceneStop(scene, scene.services[0], 1, lineStop('B', 100.0)).inserted
				&& stopStations(scene.services[0])
					== Names({"station-a", "station-b", "station-c", "station-d"}),
			"the same timetable accepts an insertion in the middle");
	}
	// e. Incompatible order.
	{
		SceneModel scene = withStops(line, {lineStop('A', 20.0), lineStop('C', 200.0)});
		const SceneService before = scene.services[0];
		const auto refused = insertSceneStop(scene, scene.services[0], 1, lineStop('D', 300.0));
		ok &= expect(!refused.inserted && contains(refused.error, "stop 2")
				&& contains(refused.error, "(C)"),
			"an insertion that strands a later stop is refused and names that stop");
		ok &= expect(scene.services[0].id == before.id && scene.services[0].route == before.route
				&& sameStops(scene.services[0].stops, before.stops),
			"a refused insertion leaves the service as it was");
		ok &= expect(insertSceneStop(scene, scene.services[0], 1, lineStop('B', 100.0)).inserted
				&& stopStations(scene.services[0]) == Names({"station-a", "station-b", "station-c"}),
			"a compatible station is inserted at the same position");
		const auto outOfRange = insertSceneStop(scene, scene.services[0], 9, lineStop('D', 300.0));
		ok &= expect(!outOfRange.inserted && scene.services[0].stops.size() == 3
				&& !sceneStopInsertionWindow(scene, scene.services[0], 9).ok,
			"a position past the end is refused");
		SceneModel unknownStation = withStops(line, {lineStop('A', 20.0), lineStop('C', 200.0)});
		const auto unknown = insertSceneStop(unknownStation, unknownStation.services[0], 1,
			stop("station-x", "", 0.0, 0.0));
		ok &= expect(!unknown.inserted && unknownStation.services[0].stops.size() == 2,
			"a station that does not exist is refused");
	}
	// f. Repeated stations and loops.
	{
		SceneModel scene = withStops(loop, {stop("station-a", "platform-a1", 20.0, 50.0)});
		const auto window = sceneStopInsertionWindow(scene, scene.services[0], 1);
		ok &= expect(window.ok && stationsOf(window.visits) == Names({"station-b", "station-a"})
				&& platformsOf(window.visits, "station-a") == Names({"platform-a2"}),
			"the window after the first visit offers B and the second visit of A");
		const auto second = insertSceneStop(scene, scene.services[0], 1,
			stop("station-a", "platform-a2", 300.0, 330.0));
		ok &= expect(second.inserted && scene.services[0].stops.size() == 2
				&& scene.services[0].stops[1].platformId == "platform-a2",
			"the second visit of A is inserted after the first");

		SceneModel both = withStops(loop, {stop("station-a", "platform-a1", 20.0, 50.0), stop("station-a", "platform-a2", 300.0, 330.0)});
		const SceneService before = both.services[0];
		const auto between = sceneStopInsertionWindow(both, both.services[0], 1);
		ok &= expect(between.ok && stationsOf(between.visits) == Names({"station-b"}),
			"A is not offered between two stops that use both of its visits");
		for (const char* platform : {"platform-a1", "platform-a2", ""}) {
			const auto refused = insertSceneStop(both, both.services[0], 1,
				stop("station-a", platform, 100.0, 130.0));
			ok &= expect(!refused.inserted && sameStops(both.services[0].stops, before.stops),
				"A is refused between two stops that use both of its visits");
		}
		ok &= expect(insertSceneStop(both, both.services[0], 1,
						 stop("station-b", "platform-b", 100.0, 130.0))
						 .inserted,
			"B is accepted between the two visits of A");

		SceneModel secondOnly = withStops(loop, {stop("station-b", "platform-b", 100.0, 130.0)});
		const SceneService beforeAmbiguous = secondOnly.services[0];
		const auto ambiguous = insertSceneStop(secondOnly, secondOnly.services[0], 0,
			stop("station-a", "", 20.0, 50.0));
		ok &= expect(!ambiguous.inserted && contains(ambiguous.error, "choose a platform")
				&& sameStops(secondOnly.services[0].stops, beforeAmbiguous.stops),
			"a stop without a platform at a station with two reachable platforms asks for a platform");
		ok &= expect(insertSceneStop(secondOnly, secondOnly.services[0], 0,
						 stop("station-a", "platform-a1", 20.0, 50.0))
						 .inserted,
			"the same stop with a platform is accepted");
	}
	// A platform that the route reaches twice: the new stop must not take the visit of an existing stop.
	{
		SceneModel samePlatform = loop;
		samePlatform.stations[0].platforms = {{"platform-a", {"node-1", "node-5"}}};
		SceneModel scene = withStops(samePlatform, {stop("station-a", "platform-a", 20.0, 50.0)});
		const SceneService before = scene.services[0];
		const auto window = sceneStopInsertionWindow(scene, scene.services[0], 0);
		ok &= expect(window.ok && window.visits.visits.empty(),
			"no visit is free before a stop that uses the first of two visits of one platform");
		const auto refused = insertSceneStop(scene, scene.services[0], 0,
			stop("station-a", "platform-a", 10.0, 15.0));
		ok &= expect(!refused.inserted && contains(refused.error, "stop 1")
				&& contains(refused.error, "(A)") && sameStops(scene.services[0].stops, before.stops),
			"a stop that would move an existing stop to another visit is refused and names it");
		ok &= expect(insertSceneStop(scene, scene.services[0], 1,
						 stop("station-a", "platform-a", 300.0, 330.0))
						 .inserted,
			"the same stop is accepted after the existing stop");
	}
	// g. An unresolved neighbour.
	{
		SceneModel scene = withStops(line, {stop("station-x", "", 0.0, 0.0), lineStop('C', 200.0)});
		const auto window = sceneStopInsertionWindow(scene, scene.services[0], 1);
		ok &= expect(!window.ok && window.blockingStop == 0 && contains(window.problem, "stop 1")
				&& contains(window.problem, "station-x") && window.visits.visits.empty(),
			"an unresolved stop before the position is reported as the obstacle");
		const auto first = sceneStopInsertionWindow(scene, scene.services[0], 0);
		ok &= expect(first.ok && stationsOf(first.visits) == Names({"station-a", "station-b"}),
			"the position before an unresolved stop still has a window");
		SceneModel noRouteScene = line;
		noRouteScene.services[0].route = "missing";
		const auto unresolved = sceneStopInsertionWindow(noRouteScene, noRouteScene.services[0], 0);
		ok &= expect(!unresolved.ok && unresolved.blockingStop == SceneStopInsertionWindow::kNoStop
				&& !unresolved.problem.empty()
				&& !insertSceneStop(noRouteScene, noRouteScene.services[0], 0, lineStop('A', 20.0)).inserted,
			"a service without a route has no window");
	}
	// h. Existing stops and their times are unchanged.
	{
		SceneStop first = lineStop('A', 20.0);
		SceneStop last = lineStop('D', 300.0);
		last.hasPlannedArrival = false;
		last.dwellSeconds = 12.5;
		SceneModel scene = withStops(line, {first, last});
		SceneStop added = stop("station-b", "platform-b", 111.0, 141.0);
		added.dwellSeconds = 7.0;
		ok &= expect(insertSceneStop(scene, scene.services[0], 1, added).inserted
				&& scene.services[0].stops.size() == 3
				&& sameStop(scene.services[0].stops[0], first)
				&& sameStop(scene.services[0].stops[1], added)
				&& sameStop(scene.services[0].stops[2], last),
			"existing stops and the planned times of the new stop are kept exactly");
		SceneStop untimed;
		untimed.stationId = "station-c";
		untimed.platformId = "platform-c";
		ok &= expect(insertSceneStop(scene, scene.services[0], 2, untimed).inserted
				&& sameStop(scene.services[0].stops[2], untimed)
				&& !scene.services[0].stops[2].hasPlannedArrival
				&& !scene.services[0].stops[2].hasPlannedDeparture,
			"no planned time is invented for the inserted stop");
	}
	// i. A window holds nothing before the previous stop or after the next stop.
	{
		const SceneModel scene = withStops(line, {lineStop('B', 100.0), lineStop('C', 200.0)});
		const Names expected[] = {{"station-a"}, {}, {"station-d"}};
		for (std::size_t index = 0; index < 3; ++index) {
			const auto window = sceneStopInsertionWindow(scene, scene.services[0], index);
			ok &= expect(window.ok && stationsOf(window.visits) == expected[index],
				"each window holds exactly the visits between its neighbours");
		}
		const SceneModel off = withStops(line, {lineStop('A', 20.0), stop("station-off", "", 1.0, 2.0), lineStop('C', 200.0)});
		ok &= expect(stationsOf(sceneStopInsertionWindow(off, off.services[0], 2).visits)
				== Names({"station-b"}),
			"a stop that is off-route context does not bound the window");
		ok &= expect(stationsOf(sceneStopInsertionWindow(off, off.services[0], 1).visits)
				== Names({"station-b"}),
			"a stop that is off-route context after the position does not bound the window");
		const SceneModel empty = withStops(line, {});
		const auto whole = sceneStopInsertionWindow(empty, empty.services[0], 0);
		ok &= expect(whole.ok && stationsOf(whole.visits) == Names({"station-a", "station-b", "station-c", "station-d"}),
			"an empty timetable offers every station of the route");
	}

	if (!ok)
		return 1;
	std::cout << "all stop insertion tests passed\n";
	return 0;
}
