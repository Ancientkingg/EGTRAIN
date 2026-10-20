#include "scene/SceneModel.h"
#include "scene/SectionInventory.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Ids = std::vector<std::string>;

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static std::string join(const Ids& ids) {
	std::string text;
	for (const std::string& id : ids)
		text += (text.empty() ? "" : ", ") + id;
	return "[" + text + "]";
}

static bool expectIds(const Ids& actual, const Ids& expected, const std::string& what) {
	const bool same = actual == expected;
	if (!same)
		std::cerr << "failed: " << what << ": expected " << join(expected) << " got " << join(actual) << "\n";
	return same;
}

static const SceneRoute* findRoute(const SceneModel& scene, const std::string& id) {
	const auto route = std::find_if(scene.routes.begin(), scene.routes.end(),
		[&id](const SceneRoute& candidate) { return candidate.id == id; });
	return route == scene.routes.end() ? nullptr : &*route;
}

static SceneRouteStations stationsOf(const SceneModel& scene, const SceneRoute& route) {
	return sceneRouteStations(scene, route, buildSceneSectionInventory(scene));
}

// True when every element of needles occurs in haystack in this order.
static bool isSubsequence(const Ids& needles, const Ids& haystack) {
	auto position = haystack.begin();
	for (const std::string& needle : needles) {
		position = std::find(position, haystack.end(), needle);
		if (position == haystack.end())
			return false;
		++position;
	}
	return true;
}

static bool loadCommitted(const fs::path& scenes, const char* name, SceneModel& scene) {
	const SceneLoadResult loaded = loadScene((scenes / name).string());
	scene = loaded.scene;
	return expect(!scene.routes.empty(), name);
}

// One track with four nodes at 0, 1, 2 and 3 and one block per kilometre.
static SceneModel lineScene() {
	SceneModel scene;
	scene.schemaVersion = 1;
	scene.tracks.push_back({"track-1"});
	for (int i = 1; i <= 4; ++i)
		scene.nodes.push_back({"node-" + std::to_string(i), "track-1", static_cast<double>(i - 1), 0.0});
	scene.arcs.push_back({"arc-1", "track-1", "node-1", "node-2", 0.0, 0.0, 40.0});
	scene.arcs.push_back({"arc-2", "track-1", "node-2", "node-3", 1000.0, 0.0, 40.0});
	scene.arcs.push_back({"arc-3", "track-1", "node-3", "node-4", 2000.0, 0.0, 40.0});
	for (int i = 1; i <= 3; ++i)
		scene.blocks.push_back({"block-" + std::to_string(i), "track-1", 1.0});
	scene.routes.push_back({"route-1", {"block-1", "block-2", "block-3"}, false, "", false});
	return scene;
}

static SceneStation station(const std::string& id, const Ids& nodeIds) {
	SceneStation result;
	result.id = id;
	result.name = id;
	int platform = 0;
	for (const std::string& nodeId : nodeIds)
		result.platforms.push_back({id + "-platform-" + std::to_string(++platform), {nodeId}});
	return result;
}

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "usage: test_routestations <scenes directory>\n";
		return 2;
	}
	const fs::path scenes = argv[1];
	bool ok = true;

	SceneModel assignment;
	if (loadCommitted(scenes, "Assignment_Gvc_Gdg_Ut", assignment)) {
		const SceneRoute* route0 = findRoute(assignment, "route0");
		const SceneRoute* route1 = findRoute(assignment, "route1");
		ok &= expect(route0 && route1, "assignment has route0 and route1");
		if (route0 && route1) {
			const SceneRouteStations forward = stationsOf(assignment, *route0);
			const SceneRouteStations back = stationsOf(assignment, *route1);
			ok &= expect(forward.resolved && back.resolved, "assignment routes resolve");
			ok &= expectIds(forward.stationIds, {"Gvc", "Gdg", "Ut"}, "assignment route0");
			ok &= expectIds(back.stationIds, {"Ut", "Gdg", "Gvc"}, "assignment route1");
		}
	}

	SceneModel netherlands;
	if (loadCommitted(scenes, "Netherlands", netherlands)) {
		const SceneRoute* route6 = findRoute(netherlands, "route6");
		ok &= expect(route6 != nullptr, "netherlands has route6");
		if (route6)
			ok &= expectIds(stationsOf(netherlands, *route6).stationIds,
				{"Ut", "Uto", "Bhv", "Dld", "Stz", "St", "Sd", "Brn"}, "netherlands route6");
		int checked = 0;
		for (const SceneService& service : netherlands.services) {
			const SceneRoute* route = findRoute(netherlands, service.route);
			if (!route)
				continue;
			Ids stops;
			for (const SceneStop& stop : service.stops)
				stops.push_back(stop.stationId);
			const SceneRouteStations stations = stationsOf(netherlands, *route);
			if (!isSubsequence(stops, stations.stationIds))
				std::cerr << "failed: stops of " << service.id << " " << join(stops)
						  << " are not in order on " << route->id << " " << join(stations.stationIds) << "\n";
			else
				++checked;
		}
		ok &= expect(checked > 0 && checked == static_cast<int>(netherlands.services.size()),
			"every netherlands service stops in route order");
		const SceneRoute* route7 = findRoute(netherlands, "route7");
		ok &= expect(route7 && stationsOf(netherlands, *route7).direction < 0,
			"netherlands route7 runs against the native direction");
	}

	// Block order decides the travel direction, so the reversed route lists the far station first.
	SceneModel reversed = lineScene();
	reversed.stations = {station("A", {"node-1"}), station("B", {"node-4"})};
	const SceneRouteStations forwardLine = stationsOf(reversed, reversed.routes[0]);
	ok &= expect(forwardLine.resolved && forwardLine.direction > 0, "line route resolves forward");
	ok &= expectIds(forwardLine.stationIds, {"A", "B"}, "forward line route");
	reversed.routes[0].blocks = {"block-3", "block-2", "block-1"};
	const SceneRouteStations reverseLine = stationsOf(reversed, reversed.routes[0]);
	ok &= expect(reverseLine.resolved && reverseLine.direction < 0, "reversed line route resolves in reverse");
	ok &= expectIds(reverseLine.stationIds, {"B", "A"}, "reversed line route");

	SceneModel bare = lineScene();
	const SceneRouteStations none = stationsOf(bare, bare.routes[0]);
	ok &= expect(none.resolved && none.stationIds.empty(), "route without stations resolves with an empty list");

	SceneModel unknown = lineScene();
	unknown.stations = {station("A", {"node-1"})};
	unknown.routes[0].blocks = {"no-such-block"};
	ok &= expect(!stationsOf(unknown, unknown.routes[0]).resolved, "route with an unknown block id is unresolved");

	// Several platforms of one station on consecutive nodes count once; a later visit counts again.
	SceneModel repeated = lineScene();
	repeated.stations = {station("A", {"node-1", "node-2", "node-4"}), station("B", {"node-3"})};
	ok &= expectIds(stationsOf(repeated, repeated.routes[0]).stationIds, {"A", "B", "A"},
		"consecutive visits collapse and a station visited again stays");

	if (!ok)
		return 1;
	std::cout << "route station tests passed\n";
	return 0;
}
