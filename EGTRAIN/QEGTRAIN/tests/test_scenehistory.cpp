#include "scene/SceneHistory.h"
#include "scene/SceneDiagnostic.h"
#include "scene/SceneModel.h"

#include <QString>
#include <QTest>

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

static const std::size_t kDefaultBytes = 128u * 1024u * 1024u;

static std::string nameOf(const SceneHistoryEntry* entry) {
	return entry && entry->scene ? entry->scene->name : "(none)";
}

static std::string headName(const SceneHistory& history) {
	return history.head() ? history.head()->name : "(none)";
}

static SceneModel named(const std::string& name) {
	SceneModel scene = makeNewSceneModel();
	scene.name = name;
	return scene;
}

// An edit that changes a value and no count.
static SceneModel withDuration(SceneModel scene, double seconds) {
	scene.settings.hasDuration = true;
	scene.settings.durationSeconds = seconds;
	return scene;
}

// Text that does not fit in the string object.
static std::string longText(const std::string& tag) {
	return tag + std::string(60, 'x');
}

// A scene of arcs whose ids and track ids are long text, or short enough for the string object.
static SceneModel bigScene(int arcCount, bool longIds = true) {
	SceneModel scene = named("big");
	scene.arcs.reserve(static_cast<std::size_t>(arcCount));
	for (int index = 0; index < arcCount; ++index) {
		SceneArc arc;
		arc.id = longIds ? longText("arc-" + std::to_string(index)) : "a" + std::to_string(index);
		arc.trackId = longIds ? longText("track") : "t";
		scene.arcs.push_back(arc);
	}
	return scene;
}

template <class Container>
static void release(Container& container) {
	Container().swap(container);
}

// One of every part of a scene that the estimate has to count, each with long text.
static SceneModel richScene() {
	SceneModel scene;
	scene.name = longText("name");
	scene.savedWithAppVersion = longText("version");
	scene.description = longText("description");
	scene.baseTime = longText("time");
	scene.defaultScenarioId = longText("default");

	scene.sourceFiles.insert(longText("file"));
	SceneImportReportRow report;
	report.category = longText("report");
	scene.importReport.push_back(report);

	scene.tracks.push_back({longText("track")});
	SceneTrackView view;
	view.trackId = longText("view");
	scene.trackViews.push_back(view);
	SceneNode node;
	node.id = longText("node");
	scene.nodes.push_back(node);
	SceneArc arc;
	arc.id = longText("arc");
	scene.arcs.push_back(arc);
	SceneBlock block;
	block.id = longText("block");
	scene.blocks.push_back(block);
	SceneConnection connection;
	connection.id = longText("connection");
	scene.connections.push_back(connection);

	SceneStation station;
	station.id = longText("station");
	ScenePlatform platform;
	platform.id = longText("platform");
	platform.nodeIds.push_back(longText("platform-node"));
	station.platforms.push_back(platform);
	scene.stations.push_back(station);
	SceneStationView stationView;
	stationView.stationId = longText("station-view");
	stationView.regions.push_back({1, 2.0});
	stationView.corridors.push_back(longText("corridor"));
	scene.stationViews.push_back(stationView);
	SceneSignal signal;
	signal.id = longText("signal");
	sceneSignals(scene).push_back(signal);
	SceneSignallingArea area;
	area.id = longText("area");
	scene.signallingAreas.push_back(area);
	SceneRoute route;
	route.id = longText("route");
	route.blocks.push_back(longText("route-block"));
	scene.routes.push_back(route);
	SceneBlockDependency dependency;
	dependency.block = longText("dependent");
	scene.blockDependencies.push_back(dependency);
	SceneSingleTrackRestriction restriction;
	restriction.startBlock = longText("restricted");
	scene.singleTrackRestrictions.push_back(restriction);
	SceneStationBoundary boundary;
	boundary.entranceBlock = longText("entrance");
	scene.stationBoundaries.push_back(boundary);

	SceneTrainUnit unit;
	unit.id = longText("unit");
	unit.tractionCurve.push_back({1.0, 2.0, 3.0, 4.0, 5.0});
	scene.trainUnits.push_back(unit);
	SceneComposition composition;
	composition.id = longText("composition");
	composition.units.push_back(longText("composition-unit"));
	scene.compositions.push_back(composition);
	SceneService service;
	service.id = longText("service");
	SceneStop stop;
	stop.stationId = longText("stop");
	service.stops.push_back(stop);
	scene.services.push_back(service);

	SceneScenario scenario;
	scenario.id = longText("scenario");
	SceneIncident incident;
	incident.id = longText("incident");
	scenario.incidents.push_back(incident);
	SceneEntranceDelay delay;
	delay.serviceId = longText("delay");
	scenario.entranceDelays.push_back(delay);
	scene.scenarios.push_back(scenario);

	ScenePassenger passenger;
	passenger.id = longText("passenger");
	ScenePassengerJourney journey;
	journey.id = longText("journey");
	ScenePassengerLeg leg;
	leg.id = longText("leg");
	journey.legs.push_back(leg);
	passenger.journeys.push_back(journey);
	scene.passengers.push_back(passenger);
	return scene;
}

struct Release {
	const char* part;
	void (*apply)(SceneModel&);
};

// The label of the edit from `before` to `after`, read back through the history.
static std::string labelOf(const SceneModel& before, const SceneModel& after) {
	SceneHistory history;
	history.reset(before);
	history.record(after);
	return history.undoLabel();
}

// The scene directories of the Scenes directory, in name order.
static std::vector<fs::path> committedSceneDirectories() {
	std::vector<fs::path> directories;
	for (const auto& entry : fs::directory_iterator(fs::path(EGTRAIN_SCENES_DIR))) {
		if (entry.is_directory())
			directories.push_back(entry.path());
	}
	std::sort(directories.begin(), directories.end());
	return directories;
}

class TestSceneHistory : public QObject {
	Q_OBJECT

private slots:
	void estimate();
	void recordUndoRedo();
	void redoBranch();
	void savedPoint();
	void stepLimit();
	void byteLimit();
	void reset();
	void labels();
	void scenesFolderHoldsScenes();
	void committedScenes_data();
	void committedScenes();
};

void TestSceneHistory::estimate() {
	const SceneModel empty{};
	// An empty scene costs at least its own size.
	QCOMPARE_GE(estimateSceneModelBytes(empty), sizeof(SceneModel));

	// A copy holds no spare capacity, so dropping any one part of it must lower the estimate. A part
	// that the estimate leaves out does not.
	const SceneModel rich = richScene();
	const SceneModel copy = rich;
	const std::size_t full = estimateSceneModelBytes(copy);
	const Release releases[] = {
		{"name", [](SceneModel& m) { release(m.name); }},
		{"description", [](SceneModel& m) { release(m.description); }},
		{"savedWithAppVersion", [](SceneModel& m) { release(m.savedWithAppVersion); }},
		{"baseTime", [](SceneModel& m) { release(m.baseTime); }},
		{"defaultScenarioId", [](SceneModel& m) { release(m.defaultScenarioId); }},
		{"sourceFiles", [](SceneModel& m) { release(m.sourceFiles); }},
		{"importReport", [](SceneModel& m) { release(m.importReport); }},
		{"tracks", [](SceneModel& m) { release(m.tracks); }},
		{"trackViews", [](SceneModel& m) { release(m.trackViews); }},
		{"nodes", [](SceneModel& m) { release(m.nodes); }},
		{"arcs", [](SceneModel& m) { release(m.arcs); }},
		{"blocks", [](SceneModel& m) { release(m.blocks); }},
		{"connections", [](SceneModel& m) { release(m.connections); }},
		{"stations", [](SceneModel& m) { release(m.stations); }},
		{"platforms", [](SceneModel& m) { release(m.stations[0].platforms); }},
		{"platform nodes", [](SceneModel& m) { release(m.stations[0].platforms[0].nodeIds); }},
		{"stationViews", [](SceneModel& m) { release(m.stationViews); }},
		{"station view regions", [](SceneModel& m) { release(m.stationViews[0].regions); }},
		{"station view corridors", [](SceneModel& m) { release(m.stationViews[0].corridors); }},
		{"signals", [](SceneModel& m) { release(sceneSignals(m)); }},
		{"signallingAreas", [](SceneModel& m) { release(m.signallingAreas); }},
		{"routes", [](SceneModel& m) { release(m.routes); }},
		{"route blocks", [](SceneModel& m) { release(m.routes[0].blocks); }},
		{"blockDependencies", [](SceneModel& m) { release(m.blockDependencies); }},
		{"singleTrackRestrictions", [](SceneModel& m) { release(m.singleTrackRestrictions); }},
		{"stationBoundaries", [](SceneModel& m) { release(m.stationBoundaries); }},
		{"trainUnits", [](SceneModel& m) { release(m.trainUnits); }},
		{"traction curve", [](SceneModel& m) { release(m.trainUnits[0].tractionCurve); }},
		{"compositions", [](SceneModel& m) { release(m.compositions); }},
		{"composition units", [](SceneModel& m) { release(m.compositions[0].units); }},
		{"services", [](SceneModel& m) { release(m.services); }},
		{"stops", [](SceneModel& m) { release(m.services[0].stops); }},
		{"scenarios", [](SceneModel& m) { release(m.scenarios); }},
		{"incidents", [](SceneModel& m) { release(m.scenarios[0].incidents); }},
		{"entranceDelays", [](SceneModel& m) { release(m.scenarios[0].entranceDelays); }},
		{"passengers", [](SceneModel& m) { release(m.passengers); }},
		{"journeys", [](SceneModel& m) { release(m.passengers[0].journeys); }},
		{"legs", [](SceneModel& m) { release(m.passengers[0].journeys[0].legs); }},
	};
	for (const Release& entry : releases) {
		SceneModel smaller = copy;
		entry.apply(smaller);
		QVERIFY2(estimateSceneModelBytes(smaller) < full, (std::string("the estimate counts ") + entry.part).c_str());
	}
	// A populated scene costs more than an empty one.
	QCOMPARE_GT(full, estimateSceneModelBytes(empty));

	const SceneModel few = bigScene(10);
	const SceneModel many = bigScene(11);
	// The estimate grows with every element.
	QCOMPARE_GT(estimateSceneModelBytes(many), estimateSceneModelBytes(few));
	// The estimate covers at least the elements.
	QCOMPARE_GE(estimateSceneModelBytes(bigScene(100)), estimateSceneModelBytes(empty) + 100 * sizeof(SceneArc));

	// The estimate counts the text of long strings.
	QCOMPARE_GE(estimateSceneModelBytes(bigScene(50)), estimateSceneModelBytes(bigScene(50, false)) + 50 * 2 * 60);
}

void TestSceneHistory::recordUndoRedo() {
	SceneHistory history;
	QVERIFY2(history.head() != nullptr, "a new history has a root");
	// A new history has nothing to undo or redo.
	QVERIFY(!history.canUndo());
	QVERIFY(!history.canRedo());

	history.reset(named("root"));
	// A reset history has nothing to undo or redo.
	QVERIFY(!history.canUndo());
	QVERIFY(!history.canRedo());
	// Undo and redo of nothing give no entry.
	QVERIFY(history.undo() == nullptr);
	QVERIFY(history.redo() == nullptr);
	// The root is the live scene.
	QCOMPARE(headName(history), "root");
	// The root counts its own estimate.
	QCOMPARE(history.bytes(), estimateSceneModelBytes(*history.head()));

	for (const char* name : {"one", "two", "three"})
		history.record(named(name));
	// A recorded scene is the live scene.
	QCOMPARE(headName(history), "three");
	// Three edits give three undo steps.
	QCOMPARE(history.undoCount(), std::size_t(3));
	QCOMPARE(history.redoCount(), std::size_t(0));

	// Undo returns the previous scene.
	QCOMPARE(nameOf(history.undo()), "two");
	// Undo moves the live scene.
	QCOMPARE(headName(history), "two");
	// Undo again.
	QCOMPARE(nameOf(history.undo()), "one");
	// Undo to the root.
	QCOMPARE(nameOf(history.undo()), "root");
	// The root cannot be undone.
	QVERIFY(!history.canUndo());
	QVERIFY(history.undo() == nullptr);
	// Three undone edits give three redo steps.
	QCOMPARE(history.undoCount(), std::size_t(0));
	QCOMPARE(history.redoCount(), std::size_t(3));

	// Redo returns the next scene.
	QCOMPARE(nameOf(history.redo()), "one");
	// Redo again.
	QCOMPARE(nameOf(history.redo()), "two");
	// Redo to the last edit.
	QCOMPARE(nameOf(history.redo()), "three");
	// The last edit cannot be redone.
	QVERIFY(!history.canRedo());
	QVERIFY(history.redo() == nullptr);

	// A snapshot is a copy: later changes to the scene that was recorded do not reach it.
	SceneModel live = named("live");
	history.record(live);
	live.name = "changed after the record";
	// The history holds a copy of the recorded scene.
	QCOMPARE(headName(history), "live");
}

void TestSceneHistory::redoBranch() {
	SceneHistory history;
	history.reset(named("root"));
	for (const char* name : {"a", "b", "c"})
		history.record(named(name));
	history.undo();
	history.undo();
	// Two undone edits can be redone.
	QCOMPARE(headName(history), "a");
	QCOMPARE(history.redoCount(), std::size_t(2));

	history.record(named("d"));
	// An edit after undo drops the redo branch.
	QVERIFY(!history.canRedo());
	QCOMPARE(history.redoCount(), std::size_t(0));
	// The undo steps are the kept ones and the new edit.
	QCOMPARE(history.undoCount(), std::size_t(2));
	// Undo of the new edit returns to the branch point.
	QCOMPARE(nameOf(history.undo()), "a");
	// Redo repeats the new edit, not the abandoned one.
	QCOMPARE(nameOf(history.redo()), "d");
}

void TestSceneHistory::savedPoint() {
	SceneHistory history;
	history.reset(named("root"));
	QVERIFY2(!history.isAtSavedPoint(), "a new scene is not saved");

	history.reset(named("root"), true);
	QVERIFY2(history.isAtSavedPoint(), "an opened scene is saved");
	history.record(named("one"));
	QVERIFY2(!history.isAtSavedPoint(), "an edit makes the scene dirty");
	history.undo();
	QVERIFY2(history.isAtSavedPoint(), "undo to the saved scene makes it clean");
	history.redo();
	QVERIFY2(!history.isAtSavedPoint(), "redo away from the saved scene makes it dirty");

	history.markSaved();
	QVERIFY2(history.isAtSavedPoint(), "saving makes the scene clean");
	// Saving keeps the history.
	QVERIFY(history.canUndo());
	QCOMPARE(history.undoCount(), std::size_t(1));
	history.record(named("two"));
	QVERIFY2(!history.isAtSavedPoint(), "an edit after saving makes the scene dirty");
	history.undo();
	QVERIFY2(history.isAtSavedPoint(), "undo to the new saved scene makes it clean");
	history.undo();
	QVERIFY2(!history.isAtSavedPoint(), "undo past the saved scene makes it dirty");
	history.redo();
	QVERIFY2(history.isAtSavedPoint(), "redo back to the saved scene makes it clean");

	// An edit after undoing past the saved scene drops it with the redo branch.
	history.undo();
	history.record(named("branch"));
	QVERIFY2(!history.isAtSavedPoint(), "the branch is dirty");
	history.undo();
	QVERIFY2(!history.isAtSavedPoint(), "the saved scene is gone after the branch point");
	history.redo();
	QVERIFY2(!history.isAtSavedPoint(), "the branch stays dirty until the next save");
	history.markSaved();
	QVERIFY2(history.isAtSavedPoint(), "saving the branch makes it clean");

	// An edit after undoing to the saved scene keeps it.
	history.reset(named("root"), true);
	history.record(named("one"));
	history.undo();
	history.record(named("other"));
	history.undo();
	QVERIFY2(history.isAtSavedPoint(), "the saved root stays saved after a new branch");

	// The modified scenarios belong to the entry and a save clears those of the live one.
	history.reset(named("root"), true);
	history.record(named("one"), {"s1"});
	history.record(named("two"), {"s1", "s2"});
	history.markSaved();
	const SceneHistoryEntry* entry = history.undo();
	// An entry keeps its modified scenarios.
	QVERIFY(entry);
	QVERIFY(entry->modifiedScenarioIds == std::set<std::string>{"s1"});
	entry = history.redo();
	// Saving clears the modified scenarios of the live entry.
	QVERIFY(entry);
	QVERIFY(entry->modifiedScenarioIds.empty());
}

void TestSceneHistory::stepLimit() {
	const SceneHistoryLimits defaults{};
	// The default limits are 100 steps and 128 MiB.
	QCOMPARE(defaults.maxSteps, std::size_t(100));
	QCOMPARE(defaults.maxBytes, kDefaultBytes);
	// A history uses the default limits.
	QCOMPARE(SceneHistory().limits().maxSteps, std::size_t(100));
	const SceneHistory injected({7, 4096});
	// The limits can be injected.
	QCOMPARE(injected.limits().maxSteps, std::size_t(7));
	QCOMPARE(injected.limits().maxBytes, std::size_t(4096));

	SceneHistory history;
	history.reset(named("0"), true);
	for (int step = 1; step <= 150; ++step)
		history.record(named(std::to_string(step)));
	// The history keeps 100 undo steps.
	QCOMPARE(history.undoCount(), std::size_t(100));
	// The newest edit is kept.
	QCOMPARE(headName(history), "150");
	std::string oldest;
	while (const SceneHistoryEntry* entry = history.undo())
		oldest = nameOf(entry);
	// The oldest edits are dropped first.
	QCOMPARE(oldest, "50");
	QVERIFY2(!history.isAtSavedPoint(), "dropping the saved scene leaves the scene dirty");
	history.markSaved();
	QVERIFY2(history.isAtSavedPoint(), "the next save is clean again");

	// The saved scene "4" stays saved while older entries are dropped before it.
	SceneHistory small({3, kDefaultBytes});
	small.reset(named("0"));
	for (int step = 1; step <= 4; ++step)
		small.record(named(std::to_string(step)));
	small.markSaved();
	small.record(named("5"));
	small.record(named("6"));
	// A history of 3 steps keeps 3 steps.
	QCOMPARE(small.undoCount(), std::size_t(3));
	small.undo();
	// An entry after the saved scene is dirty.
	QCOMPARE(headName(small), "5");
	QVERIFY(!small.isAtSavedPoint());
	small.undo();
	// The saved position follows the entries that are dropped before it.
	QCOMPARE(headName(small), "4");
	QVERIFY(small.isAtSavedPoint());
	small.undo();
	// The oldest retained step follows the limit.
	QCOMPARE(headName(small), "3");
	QVERIFY(!small.canUndo());

	SceneHistory none({0, kDefaultBytes});
	none.reset(named("0"));
	none.record(named("1"));
	// No steps keeps only the live scene.
	QVERIFY(!none.canUndo());
	QCOMPARE(headName(none), "1");
}

void TestSceneHistory::byteLimit() {
	const SceneModel scene = bigScene(500);
	SceneHistory probe;
	probe.reset(scene);
	const std::size_t one = probe.bytes();
	// The test scene is large enough.
	QCOMPARE_GT(one, 500 * sizeof(SceneArc));

	SceneHistory history({100, 3 * one + one / 2});
	history.reset(scene, true);
	for (int step = 1; step <= 10; ++step)
		history.record(withDuration(scene, step));
	// The memory limit keeps three snapshots.
	QCOMPARE(history.undoCount(), std::size_t(2));
	// The retained estimate stays within the limit.
	QCOMPARE(history.bytes(), 3 * one);
	QCOMPARE_LE(history.bytes(), history.limits().maxBytes);
	// The newest edit is kept.
	QCOMPARE_EQ(history.head()->settings.durationSeconds, 10.0);
	QVERIFY2(!history.isAtSavedPoint(), "dropping the saved scene leaves the scene dirty");

	const std::shared_ptr<const SceneModel> kept = history.head();
	for (int step = 11; step <= 13; ++step)
		history.record(withDuration(scene, step));
	// The memory limit still holds.
	QCOMPARE(history.undoCount(), std::size_t(2));
	history.undo();
	history.undo();
	// The retained entries are the newest ones.
	QVERIFY(!history.canUndo());
	QCOMPARE_EQ(history.head()->settings.durationSeconds, 11.0);
	// A scene that was handed out stays valid after it is dropped.
	QVERIFY(kept);
	QCOMPARE_EQ(kept->settings.durationSeconds, 10.0);

	// One snapshot larger than the limit is kept, because it is the live scene.
	SceneHistory tight({100, one / 2});
	tight.reset(scene);
	tight.record(withDuration(scene, 1));
	// The live scene is never dropped.
	QVERIFY(!tight.canUndo());
	QCOMPARE_EQ(tight.head()->settings.durationSeconds, 1.0);
	tight.record(withDuration(scene, 2));
	// An oversized history keeps only the newest scene.
	QCOMPARE(tight.undoCount(), std::size_t(0));
	QCOMPARE(tight.bytes(), one);
	QCOMPARE_EQ(tight.head()->settings.durationSeconds, 2.0);

	// The redo branch counts toward the memory only until the next edit.
	SceneHistory branch({100, 3 * one + one / 2});
	branch.reset(scene);
	branch.record(withDuration(scene, 1));
	branch.record(withDuration(scene, 2));
	branch.undo();
	branch.undo();
	// Undo does not drop entries.
	QCOMPARE(branch.bytes(), 3 * one);
	QCOMPARE(branch.redoCount(), std::size_t(2));
	branch.record(withDuration(scene, 3));
	// An edit frees the redo branch.
	QCOMPARE(branch.bytes(), 2 * one);
	QCOMPARE(branch.redoCount(), std::size_t(0));
}

void TestSceneHistory::reset() {
	SceneHistory history;
	history.reset(named("first"), true);
	history.record(named("edit"));
	const std::shared_ptr<const SceneModel> before = history.head();
	history.undo();

	history.reset(named("second"));
	// Reset replaces the scene.
	QCOMPARE(headName(history), "second");
	// Reset drops the undo and redo steps.
	QVERIFY(!history.canUndo());
	QVERIFY(!history.canRedo());
	QVERIFY2(!history.isAtSavedPoint(), "reset to an unsaved scene is dirty");
	// Reset leaves no labels.
	QVERIFY(history.undoLabel().empty());
	QVERIFY(history.redoLabel().empty());
	// A scene from before the reset stays valid.
	QVERIFY(before);
	QCOMPARE(before->name, "edit");

	history.record(named("edit"));
	history.reset(named("third"), true);
	// Reset to a saved scene is clean.
	QVERIFY(history.isAtSavedPoint());
	QCOMPARE(history.bytes(), estimateSceneModelBytes(*history.head()));
}

void TestSceneHistory::labels() {
	SceneModel base = named("labels");
	SceneStation station;
	station.id = "station";
	ScenePlatform platform;
	platform.id = "platform";
	station.platforms.push_back(platform);
	base.stations.push_back(station);
	SceneService service;
	service.id = "service";
	service.stops.push_back(SceneStop());
	base.services.push_back(service);

	SceneModel more = base;
	SceneService added;
	added.id = "other";
	added.stops.push_back(SceneStop());
	added.stops.push_back(SceneStop());
	more.services.push_back(added);
	// A service added with its stops is one added service.
	QCOMPARE(labelOf(base, more), "Add service");
	// A service removed with its stops is one deleted service.
	QCOMPARE(labelOf(more, base), "Delete service");

	more = base;
	more.services[0].stops.push_back(SceneStop());
	// A stop added to a service.
	QCOMPARE(labelOf(base, more), "Add stop");
	// A stop removed from a service.
	QCOMPARE(labelOf(more, base), "Delete stop");

	more = base;
	more.stations[0].platforms.push_back(platform);
	// A platform added to a station.
	QCOMPARE(labelOf(base, more), "Add platform");

	more = base;
	more.stations.clear();
	// A station removed with its platforms.
	QCOMPARE(labelOf(base, more), "Delete station");

	more = base;
	more.trainUnits.push_back(SceneTrainUnit());
	// A unit added.
	QCOMPARE(labelOf(base, more), "Add rolling stock unit");
	more = base;
	more.compositions.push_back(SceneComposition());
	// A composition added.
	QCOMPARE(labelOf(base, more), "Add composition");
	more = base;
	more.passengers.push_back(ScenePassenger());
	// A passenger added.
	QCOMPARE(labelOf(base, more), "Add passenger");

	SceneModel withPassenger = base;
	ScenePassenger passenger;
	passenger.journeys.push_back(ScenePassengerJourney());
	passenger.journeys[0].legs.push_back(ScenePassengerLeg());
	withPassenger.passengers.push_back(passenger);
	// A passenger with its journeys and legs is one passenger.
	QCOMPARE(labelOf(base, withPassenger), "Add passenger");
	more = withPassenger;
	more.passengers[0].journeys[0].legs.push_back(ScenePassengerLeg());
	// A leg added to a journey.
	QCOMPARE(labelOf(withPassenger, more), "Add passenger leg");

	SceneModel withScenario = base;
	SceneScenario scenario;
	scenario.id = "scenario";
	scenario.incidents.push_back(SceneIncident());
	withScenario.scenarios.push_back(scenario);
	// A scenario with its incidents is one scenario.
	QCOMPARE(labelOf(base, withScenario), "Add scenario");
	more = withScenario;
	more.scenarios[0].incidents.push_back(SceneIncident());
	// An incident added to a scenario.
	QCOMPARE(labelOf(withScenario, more), "Add incident");
	more = withScenario;
	more.scenarios[0].entranceDelays.push_back(SceneEntranceDelay());
	// An entrance delay added to a scenario.
	QCOMPARE(labelOf(withScenario, more), "Add entrance delay");

	more = base;
	more.name = "renamed";
	// A changed value is an edit.
	QCOMPARE(labelOf(base, more), "Edit");
	more = base;
	std::swap(more.services[0].id, more.stations[0].id);
	// A moved value is an edit.
	QCOMPARE(labelOf(base, more), "Edit");

	more = base;
	more.services.push_back(added);
	more.services.push_back(added);
	// Two services added at once are an edit.
	QCOMPARE(labelOf(base, more), "Edit");
	more = base;
	more.services.push_back(added);
	more.tracks.push_back(SceneTrack());
	// Changes to two kinds of part are an edit.
	QCOMPARE(labelOf(base, more), "Edit");

	// The label belongs to the edit and moves with the position.
	SceneHistory history;
	history.reset(base);
	history.record(withScenario);
	history.record(base);
	// The undo label names the last edit.
	QCOMPARE(history.undoLabel(), "Delete scenario");
	QVERIFY(history.redoLabel().empty());
	history.undo();
	// The labels follow the position.
	QCOMPARE(history.undoLabel(), "Add scenario");
	QCOMPARE(history.redoLabel(), "Delete scenario");
	history.undo();
	// The root has no label to undo.
	QVERIFY(history.undoLabel().empty());
	QCOMPARE(history.redoLabel(), "Add scenario");
}

void TestSceneHistory::scenesFolderHoldsScenes() {
	QVERIFY2(!committedSceneDirectories().empty(), "the scenes folder holds scenes");
}

void TestSceneHistory::committedScenes_data() {
	QTest::addColumn<QString>("directory");
	for (const fs::path& directory : committedSceneDirectories())
		QTest::newRow(directory.filename().string().c_str()) << QString::fromStdString(directory.u8string());
}

void TestSceneHistory::committedScenes() {
	QFETCH(QString, directory);
	const SceneLoadResult loaded = loadScene(directory.toStdString());
	// The scene loads and has arcs.
	QVERIFY(!hasErrors(loaded.diagnostics));
	QVERIFY(!loaded.scene.arcs.empty());

	SceneHistory history;
	history.reset(loaded.scene, true);
	const std::size_t snapshot = history.bytes();
	// The estimate covers the arcs.
	QCOMPARE_GE(snapshot, loaded.scene.arcs.size() * sizeof(SceneArc));

	SceneModel live = loaded.scene;
	for (int step = 1; step <= 101; ++step) {
		live.settings.hasDuration = true;
		live.settings.durationSeconds = step;
		history.record(live);
	}
	const SceneHistoryLimits& limits = history.limits();
	// At most 100 undo steps.
	QCOMPARE_LE(history.undoCount(), limits.maxSteps);
	// At most 128 MiB.
	QCOMPARE_LE(history.bytes(), limits.maxBytes);
	// Only a limit drops steps: either the count is full or one more snapshot would not fit.
	QVERIFY2(history.undoCount() == limits.maxSteps || history.bytes() + snapshot > limits.maxBytes,
		"entries are dropped only by a limit");
	// The newest edit is the live scene.
	QCOMPARE_EQ(history.head()->settings.durationSeconds, 101.0);
}

QTEST_APPLESS_MAIN(TestSceneHistory)
#include "test_scenehistory.moc"
