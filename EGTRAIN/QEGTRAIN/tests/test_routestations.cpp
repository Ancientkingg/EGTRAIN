#include "scene/SceneModel.h"
#include "scene/SectionInventory.h"

#include <QTest>

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Ids = std::vector<std::string>;

static std::string join(const Ids& ids) {
	std::string text;
	for (const std::string& id : ids)
		text += (text.empty() ? "" : ", ") + id;
	return "[" + text + "]";
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

// The committed scene `name`.
static SceneModel loadCommitted(const char* name) {
	return loadScene((fs::path(EGTRAIN_SCENES_DIR) / name).string()).scene;
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

class TestRouteStations : public QObject {
	Q_OBJECT

private slots:
	void listsAssignmentRouteStations();
	void listsNetherlandsRouteStations();
	void labelsEverySection_data();
	void labelsEverySection();
	void listsSectionsInTravelOrder();
	void blockOrderDecidesDirection();
	void routeWithoutStationsHasNone();
	void unknownBlockIsUnresolved();
	void consecutiveVisitsCollapse();
};

void TestRouteStations::listsAssignmentRouteStations() {
	const SceneModel assignment = loadCommitted("Assignment_Gvc_Gdg_Ut");
	QVERIFY2(!assignment.routes.empty(), "Assignment_Gvc_Gdg_Ut");
	const SceneRoute* route0 = findRoute(assignment, "route0");
	const SceneRoute* route1 = findRoute(assignment, "route1");
	// assignment has route0 and route1
	if (route0 == nullptr)
		QFAIL("assignment has route0");
	if (route1 == nullptr)
		QFAIL("assignment has route1");
	const SceneRouteStations forward = stationsOf(assignment, *route0);
	const SceneRouteStations back = stationsOf(assignment, *route1);
	// assignment routes resolve
	QVERIFY(forward.resolved);
	QVERIFY(back.resolved);
	// assignment route0
	QCOMPARE(forward.stationIds, Ids({"Gvc", "Gdg", "Ut"}));
	// assignment route1
	QCOMPARE(back.stationIds, Ids({"Ut", "Gdg", "Gvc"}));
}

void TestRouteStations::listsNetherlandsRouteStations() {
	const SceneModel netherlands = loadCommitted("Netherlands");
	QVERIFY2(!netherlands.routes.empty(), "Netherlands");
	const SceneRoute* route6 = findRoute(netherlands, "route6");
	if (route6 == nullptr)
		QFAIL("netherlands has route6");
	// netherlands route6
	QCOMPARE(stationsOf(netherlands, *route6).stationIds, Ids({"Ut", "Uto", "Bhv", "Dld", "Stz", "St", "Sd", "Brn"}));
	int checked = 0;
	for (const SceneService& service : netherlands.services) {
		const SceneRoute* route = findRoute(netherlands, service.route);
		if (!route)
			continue;
		Ids stops;
		for (const SceneStop& stop : service.stops)
			stops.push_back(stop.stationId);
		const SceneRouteStations stations = stationsOf(netherlands, *route);
		QVERIFY2(isSubsequence(stops, stations.stationIds),
			("stops of " + service.id + " " + join(stops) + " are not in order on " + route->id + " " + join(stations.stationIds))
				.c_str());
		++checked;
	}
	// every netherlands service stops in route order
	QVERIFY(checked > 0);
	QCOMPARE(checked, static_cast<int>(netherlands.services.size()));
	const SceneRoute* route7 = findRoute(netherlands, "route7");
	// netherlands route7 runs against the native direction
	if (route7 == nullptr)
		QFAIL("netherlands has route7");
	QVERIFY(stationsOf(netherlands, *route7).direction < 0);
}

void TestRouteStations::labelsEverySection_data() {
	QTest::addColumn<QString>("name");
	for (const char* name : {"Paimpol", "Netherlands", "Copenhagen", "Milano_Brescia"})
		QTest::newRow(name) << QString(name);
}

// Every section of the converted case studies has a label of its own.
void TestRouteStations::labelsEverySection() {
	QFETCH(QString, name);
	const SceneModel scene = loadCommitted(qPrintable(name));
	QVERIFY2(!scene.routes.empty(), qPrintable(name));
	const SceneSectionInventory inventory = buildSceneSectionInventory(scene);
	const SceneSectionLabels labels = sceneSectionLabels(inventory);
	std::set<std::string> distinct;
	for (const SceneSectionDescriptor& section : inventory.sections)
		distinct.insert(sceneSectionLabel(section));
	QVERIFY(!inventory.sections.empty());
	QCOMPARE(distinct.size(), inventory.sections.size());
	QVERIFY(labels.byReference.size() >= inventory.sections.size());
	if (name == "Paimpol") {
		// paimpol labels
		QCOMPARE(labels.label("@0-B0@"), "B0 0.000 to 0.230 km");
		QCOMPARE(labels.label("70-B0"), "B0 20.710 to 21.010 km");
		QCOMPARE(labels.label("@71-B0@-21.235000/@0-B1@-21.252000"), "B0 21.010 to 21.235, B1 21.252 to 21.450 km");
	}
	if (name == "Netherlands") {
		// netherlands label with a piece of one kilometre
		QCOMPARE(labels.label("@15-B104@-58.099000/@16-B100@-58.152000"), "B104 58.000 to 58.099, B100 58.152 km");
	}
}

// Every route of the committed scenes lists its sections in the order of travel: each one joins the next in the
// direction of the route. A route may jump once between two kilometre references, as the Netherlands routes do
// between their regions; the runtime moves the sections after one such jump onto the reference before it.
void TestRouteStations::listsSectionsInTravelOrder() {
	std::vector<fs::path> committed;
	for (const fs::directory_entry& entry : fs::directory_iterator(fs::path(EGTRAIN_SCENES_DIR)))
		if (fs::exists(entry.path() / "scene.json"))
			committed.push_back(entry.path());
	std::sort(committed.begin(), committed.end());
	QVERIFY2(committed.size() >= 6, "the committed scenes are found");
	for (const fs::path& directory : committed) {
		const SceneModel scene = loadScene(directory.string()).scene;
		const SceneSectionInventory inventory = buildSceneSectionInventory(scene);
		const std::string name = directory.filename().string();
		QVERIFY2(!scene.routes.empty(), name.c_str());
		for (const SceneRoute& route : scene.routes) {
			std::vector<const SceneSectionDescriptor*> sections;
			for (const std::string& reference : route.blocks)
				if (const SceneSectionDescriptor* section = inventory.resolve(reference))
					sections.push_back(section);
			const int direction = sceneRouteDirection(scene, sections);
			const std::string unresolved = name + " " + route.id + " does not resolve to one direction";
			QVERIFY2(sections.size() == route.blocks.size(), unresolved.c_str());
			QVERIFY2(direction != 0, unresolved.c_str());
			int jumps = 0;
			for (std::size_t index = 1; index < sections.size(); ++index) {
				const SceneSectionTransition transition = classifySceneSectionTransition(scene, *sections[index - 1], *sections[index]);
				if (direction > 0 ? transition.joinsForward : transition.joinsReverse)
					continue;
				if (transition.regionJump && ++jumps == 1)
					continue;
				QFAIL((name + " " + route.id + " entries " + std::to_string(index) + " and " + std::to_string(index + 1)
					+ " are not in the order of travel: " + route.blocks[index - 1] + " -> " + route.blocks[index])
						.c_str());
			}
		}
	}
}

// Block order decides the travel direction, so the reversed route lists the far station first.
void TestRouteStations::blockOrderDecidesDirection() {
	SceneModel reversed = lineScene();
	reversed.stations = {station("A", {"node-1"}), station("B", {"node-4"})};
	const SceneRouteStations forwardLine = stationsOf(reversed, reversed.routes[0]);
	// line route resolves forward
	QVERIFY(forwardLine.resolved);
	QVERIFY(forwardLine.direction > 0);
	// forward line route
	QCOMPARE(forwardLine.stationIds, Ids({"A", "B"}));
	reversed.routes[0].blocks = {"block-3", "block-2", "block-1"};
	const SceneRouteStations reverseLine = stationsOf(reversed, reversed.routes[0]);
	// reversed line route resolves in reverse
	QVERIFY(reverseLine.resolved);
	QVERIFY(reverseLine.direction < 0);
	// reversed line route
	QCOMPARE(reverseLine.stationIds, Ids({"B", "A"}));
}

void TestRouteStations::routeWithoutStationsHasNone() {
	SceneModel bare = lineScene();
	const SceneRouteStations none = stationsOf(bare, bare.routes[0]);
	// route without stations resolves with an empty list
	QVERIFY(none.resolved);
	QVERIFY(none.stationIds.empty());
}

void TestRouteStations::unknownBlockIsUnresolved() {
	SceneModel unknown = lineScene();
	unknown.stations = {station("A", {"node-1"})};
	unknown.routes[0].blocks = {"no-such-block"};
	QVERIFY2(!stationsOf(unknown, unknown.routes[0]).resolved, "route with an unknown block id is unresolved");
}

// Several platforms of one station on consecutive nodes count once; a later visit counts again.
void TestRouteStations::consecutiveVisitsCollapse() {
	SceneModel repeated = lineScene();
	repeated.stations = {station("A", {"node-1", "node-2", "node-4"}), station("B", {"node-3"})};
	// consecutive visits collapse and a station visited again stays
	QCOMPARE(stationsOf(repeated, repeated.routes[0]).stationIds, Ids({"A", "B", "A"}));
}

QTEST_APPLESS_MAIN(TestRouteStations)
#include "test_routestations.moc"
