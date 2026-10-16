#include "scene/SceneHistory.h"
#include "scene/SceneDiagnostic.h"
#include "scene/SceneModel.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

static const std::size_t kDefaultBytes = 128u * 1024u * 1024u;

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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

	SceneLoadedData loaded;
	loaded.category = longText("loaded");
	SceneLoadedData child;
	child.category = longText("child");
	loaded.children.push_back(child);
	scene.loadedData.push_back(loaded);
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
	scene.signals.push_back(signal);
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

static bool testEstimate() {
	bool ok = true;
	const SceneModel empty{};
	ok &= expect(estimateSceneModelBytes(empty) >= sizeof(SceneModel), "an empty scene costs at least its own size");

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
		{"loadedData", [](SceneModel& m) { release(m.loadedData); }},
		{"loadedData children", [](SceneModel& m) { release(m.loadedData[0].children); }},
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
		{"signals", [](SceneModel& m) { release(m.signals); }},
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
		if (estimateSceneModelBytes(smaller) >= full) {
			std::cerr << "failed: the estimate counts " << entry.part << "\n";
			ok = false;
		}
	}
	ok &= expect(full > estimateSceneModelBytes(empty), "a populated scene costs more than an empty one");

	const SceneModel few = bigScene(10);
	const SceneModel many = bigScene(11);
	ok &= expect(estimateSceneModelBytes(many) > estimateSceneModelBytes(few), "the estimate grows with every element");
	ok &= expect(estimateSceneModelBytes(bigScene(100)) >= estimateSceneModelBytes(empty) + 100 * sizeof(SceneArc),
		"the estimate covers at least the elements");

	ok &= expect(estimateSceneModelBytes(bigScene(50)) >= estimateSceneModelBytes(bigScene(50, false)) + 50 * 2 * 60,
		"the estimate counts the text of long strings");
	return ok;
}

static bool testRecordUndoRedo() {
	bool ok = true;
	SceneHistory history;
	ok &= expect(history.head() != nullptr, "a new history has a root");
	ok &= expect(!history.canUndo() && !history.canRedo(), "a new history has nothing to undo or redo");

	history.reset(named("root"));
	ok &= expect(!history.canUndo() && !history.canRedo(), "a reset history has nothing to undo or redo");
	ok &= expect(history.undo() == nullptr && history.redo() == nullptr, "undo and redo of nothing give no entry");
	ok &= expect(headName(history) == "root", "the root is the live scene");
	ok &= expect(history.bytes() == estimateSceneModelBytes(*history.head()), "the root counts its own estimate");

	for (const char* name : {"one", "two", "three"})
		history.record(named(name));
	ok &= expect(headName(history) == "three", "a recorded scene is the live scene");
	ok &= expect(history.undoCount() == 3 && history.redoCount() == 0, "three edits give three undo steps");

	ok &= expect(nameOf(history.undo()) == "two", "undo returns the previous scene");
	ok &= expect(headName(history) == "two", "undo moves the live scene");
	ok &= expect(nameOf(history.undo()) == "one", "undo again");
	ok &= expect(nameOf(history.undo()) == "root", "undo to the root");
	ok &= expect(!history.canUndo() && history.undo() == nullptr, "the root cannot be undone");
	ok &= expect(history.undoCount() == 0 && history.redoCount() == 3, "three undone edits give three redo steps");

	ok &= expect(nameOf(history.redo()) == "one", "redo returns the next scene");
	ok &= expect(nameOf(history.redo()) == "two", "redo again");
	ok &= expect(nameOf(history.redo()) == "three", "redo to the last edit");
	ok &= expect(!history.canRedo() && history.redo() == nullptr, "the last edit cannot be redone");

	// A snapshot is a copy: later changes to the scene that was recorded do not reach it.
	SceneModel live = named("live");
	history.record(live);
	live.name = "changed after the record";
	ok &= expect(headName(history) == "live", "the history holds a copy of the recorded scene");
	return ok;
}

static bool testRedoBranch() {
	bool ok = true;
	SceneHistory history;
	history.reset(named("root"));
	for (const char* name : {"a", "b", "c"})
		history.record(named(name));
	history.undo();
	history.undo();
	ok &= expect(headName(history) == "a" && history.redoCount() == 2, "two undone edits can be redone");

	history.record(named("d"));
	ok &= expect(!history.canRedo() && history.redoCount() == 0, "an edit after undo drops the redo branch");
	ok &= expect(history.undoCount() == 2, "the undo steps are the kept ones and the new edit");
	ok &= expect(nameOf(history.undo()) == "a", "undo of the new edit returns to the branch point");
	ok &= expect(nameOf(history.redo()) == "d", "redo repeats the new edit, not the abandoned one");
	return ok;
}

static bool testSavedPoint() {
	bool ok = true;
	SceneHistory history;
	history.reset(named("root"));
	ok &= expect(!history.isAtSavedPoint(), "a new scene is not saved");

	history.reset(named("root"), true);
	ok &= expect(history.isAtSavedPoint(), "an opened scene is saved");
	history.record(named("one"));
	ok &= expect(!history.isAtSavedPoint(), "an edit makes the scene dirty");
	history.undo();
	ok &= expect(history.isAtSavedPoint(), "undo to the saved scene makes it clean");
	history.redo();
	ok &= expect(!history.isAtSavedPoint(), "redo away from the saved scene makes it dirty");

	history.markSaved();
	ok &= expect(history.isAtSavedPoint(), "saving makes the scene clean");
	ok &= expect(history.canUndo() && history.undoCount() == 1, "saving keeps the history");
	history.record(named("two"));
	ok &= expect(!history.isAtSavedPoint(), "an edit after saving makes the scene dirty");
	history.undo();
	ok &= expect(history.isAtSavedPoint(), "undo to the new saved scene makes it clean");
	history.undo();
	ok &= expect(!history.isAtSavedPoint(), "undo past the saved scene makes it dirty");
	history.redo();
	ok &= expect(history.isAtSavedPoint(), "redo back to the saved scene makes it clean");

	// An edit after undoing past the saved scene drops it with the redo branch.
	history.undo();
	history.record(named("branch"));
	ok &= expect(!history.isAtSavedPoint(), "the branch is dirty");
	history.undo();
	ok &= expect(!history.isAtSavedPoint(), "the saved scene is gone after the branch point");
	history.redo();
	ok &= expect(!history.isAtSavedPoint(), "the branch stays dirty until the next save");
	history.markSaved();
	ok &= expect(history.isAtSavedPoint(), "saving the branch makes it clean");

	// An edit after undoing to the saved scene keeps it.
	history.reset(named("root"), true);
	history.record(named("one"));
	history.undo();
	history.record(named("other"));
	history.undo();
	ok &= expect(history.isAtSavedPoint(), "the saved root stays saved after a new branch");

	// The modified scenarios belong to the entry and a save clears those of the live one.
	history.reset(named("root"), true);
	history.record(named("one"), {"s1"});
	history.record(named("two"), {"s1", "s2"});
	history.markSaved();
	const SceneHistoryEntry* entry = history.undo();
	ok &= expect(entry && entry->modifiedScenarioIds == std::set<std::string>{"s1"}, "an entry keeps its modified scenarios");
	entry = history.redo();
	ok &= expect(entry && entry->modifiedScenarioIds.empty(), "saving clears the modified scenarios of the live entry");
	return ok;
}

static bool testStepLimit() {
	bool ok = true;
	const SceneHistoryLimits defaults{};
	ok &= expect(defaults.maxSteps == 100 && defaults.maxBytes == kDefaultBytes, "the default limits are 100 steps and 128 MiB");
	ok &= expect(SceneHistory().limits().maxSteps == 100, "a history uses the default limits");
	const SceneHistory injected({7, 4096});
	ok &= expect(injected.limits().maxSteps == 7 && injected.limits().maxBytes == 4096, "the limits can be injected");

	SceneHistory history;
	history.reset(named("0"), true);
	for (int step = 1; step <= 150; ++step)
		history.record(named(std::to_string(step)));
	ok &= expect(history.undoCount() == 100, "the history keeps 100 undo steps");
	ok &= expect(headName(history) == "150", "the newest edit is kept");
	std::string oldest;
	while (const SceneHistoryEntry* entry = history.undo())
		oldest = nameOf(entry);
	ok &= expect(oldest == "50", "the oldest edits are dropped first");
	ok &= expect(!history.isAtSavedPoint(), "dropping the saved scene leaves the scene dirty");
	history.markSaved();
	ok &= expect(history.isAtSavedPoint(), "the next save is clean again");

	// The saved scene "4" stays saved while older entries are dropped before it.
	SceneHistory small({3, kDefaultBytes});
	small.reset(named("0"));
	for (int step = 1; step <= 4; ++step)
		small.record(named(std::to_string(step)));
	small.markSaved();
	small.record(named("5"));
	small.record(named("6"));
	ok &= expect(small.undoCount() == 3, "a history of 3 steps keeps 3 steps");
	small.undo();
	ok &= expect(headName(small) == "5" && !small.isAtSavedPoint(), "an entry after the saved scene is dirty");
	small.undo();
	ok &= expect(headName(small) == "4" && small.isAtSavedPoint(), "the saved position follows the entries that are dropped before it");
	small.undo();
	ok &= expect(headName(small) == "3" && !small.canUndo(), "the oldest retained step follows the limit");

	SceneHistory none({0, kDefaultBytes});
	none.reset(named("0"));
	none.record(named("1"));
	ok &= expect(!none.canUndo() && headName(none) == "1", "no steps keeps only the live scene");
	return ok;
}

static bool testByteLimit() {
	bool ok = true;
	const SceneModel scene = bigScene(500);
	SceneHistory probe;
	probe.reset(scene);
	const std::size_t one = probe.bytes();
	ok &= expect(one > 500 * sizeof(SceneArc), "the test scene is large enough");

	SceneHistory history({100, 3 * one + one / 2});
	history.reset(scene, true);
	for (int step = 1; step <= 10; ++step)
		history.record(withDuration(scene, step));
	ok &= expect(history.undoCount() == 2, "the memory limit keeps three snapshots");
	ok &= expect(history.bytes() == 3 * one && history.bytes() <= history.limits().maxBytes, "the retained estimate stays within the limit");
	ok &= expect(history.head()->settings.durationSeconds == 10, "the newest edit is kept");
	ok &= expect(!history.isAtSavedPoint(), "dropping the saved scene leaves the scene dirty");

	const std::shared_ptr<const SceneModel> kept = history.head();
	for (int step = 11; step <= 13; ++step)
		history.record(withDuration(scene, step));
	ok &= expect(history.undoCount() == 2, "the memory limit still holds");
	history.undo();
	history.undo();
	ok &= expect(!history.canUndo() && history.head()->settings.durationSeconds == 11, "the retained entries are the newest ones");
	ok &= expect(kept && kept->settings.durationSeconds == 10, "a scene that was handed out stays valid after it is dropped");

	// One snapshot larger than the limit is kept, because it is the live scene.
	SceneHistory tight({100, one / 2});
	tight.reset(scene);
	tight.record(withDuration(scene, 1));
	ok &= expect(!tight.canUndo() && tight.head()->settings.durationSeconds == 1, "the live scene is never dropped");
	tight.record(withDuration(scene, 2));
	ok &= expect(tight.undoCount() == 0 && tight.bytes() == one && tight.head()->settings.durationSeconds == 2,
		"an oversized history keeps only the newest scene");

	// The redo branch counts toward the memory only until the next edit.
	SceneHistory branch({100, 3 * one + one / 2});
	branch.reset(scene);
	branch.record(withDuration(scene, 1));
	branch.record(withDuration(scene, 2));
	branch.undo();
	branch.undo();
	ok &= expect(branch.bytes() == 3 * one && branch.redoCount() == 2, "undo does not drop entries");
	branch.record(withDuration(scene, 3));
	ok &= expect(branch.bytes() == 2 * one && branch.redoCount() == 0, "an edit frees the redo branch");
	return ok;
}

static bool testReset() {
	bool ok = true;
	SceneHistory history;
	history.reset(named("first"), true);
	history.record(named("edit"));
	const std::shared_ptr<const SceneModel> before = history.head();
	history.undo();

	history.reset(named("second"));
	ok &= expect(headName(history) == "second", "reset replaces the scene");
	ok &= expect(!history.canUndo() && !history.canRedo(), "reset drops the undo and redo steps");
	ok &= expect(!history.isAtSavedPoint(), "reset to an unsaved scene is dirty");
	ok &= expect(history.undoLabel().empty() && history.redoLabel().empty(), "reset leaves no labels");
	ok &= expect(before && before->name == "edit", "a scene from before the reset stays valid");

	history.record(named("edit"));
	history.reset(named("third"), true);
	ok &= expect(history.isAtSavedPoint() && history.bytes() == estimateSceneModelBytes(*history.head()), "reset to a saved scene is clean");
	return ok;
}

// The label of the edit from `before` to `after`, read back through the history.
static std::string labelOf(const SceneModel& before, const SceneModel& after) {
	SceneHistory history;
	history.reset(before);
	history.record(after);
	return history.undoLabel();
}

static bool testLabels() {
	bool ok = true;
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
	ok &= expect(labelOf(base, more) == "Add service", "a service added with its stops is one added service");
	ok &= expect(labelOf(more, base) == "Delete service", "a service removed with its stops is one deleted service");

	more = base;
	more.services[0].stops.push_back(SceneStop());
	ok &= expect(labelOf(base, more) == "Add stop", "a stop added to a service");
	ok &= expect(labelOf(more, base) == "Delete stop", "a stop removed from a service");

	more = base;
	more.stations[0].platforms.push_back(platform);
	ok &= expect(labelOf(base, more) == "Add platform", "a platform added to a station");

	more = base;
	more.stations.clear();
	ok &= expect(labelOf(base, more) == "Delete station", "a station removed with its platforms");

	more = base;
	more.trainUnits.push_back(SceneTrainUnit());
	ok &= expect(labelOf(base, more) == "Add rolling stock unit", "a unit added");
	more = base;
	more.compositions.push_back(SceneComposition());
	ok &= expect(labelOf(base, more) == "Add composition", "a composition added");
	more = base;
	more.passengers.push_back(ScenePassenger());
	ok &= expect(labelOf(base, more) == "Add passenger", "a passenger added");

	SceneModel withPassenger = base;
	ScenePassenger passenger;
	passenger.journeys.push_back(ScenePassengerJourney());
	passenger.journeys[0].legs.push_back(ScenePassengerLeg());
	withPassenger.passengers.push_back(passenger);
	ok &= expect(labelOf(base, withPassenger) == "Add passenger", "a passenger with its journeys and legs is one passenger");
	more = withPassenger;
	more.passengers[0].journeys[0].legs.push_back(ScenePassengerLeg());
	ok &= expect(labelOf(withPassenger, more) == "Add passenger leg", "a leg added to a journey");

	SceneModel withScenario = base;
	SceneScenario scenario;
	scenario.id = "scenario";
	scenario.incidents.push_back(SceneIncident());
	withScenario.scenarios.push_back(scenario);
	ok &= expect(labelOf(base, withScenario) == "Add scenario", "a scenario with its incidents is one scenario");
	more = withScenario;
	more.scenarios[0].incidents.push_back(SceneIncident());
	ok &= expect(labelOf(withScenario, more) == "Add incident", "an incident added to a scenario");
	more = withScenario;
	more.scenarios[0].entranceDelays.push_back(SceneEntranceDelay());
	ok &= expect(labelOf(withScenario, more) == "Add entrance delay", "an entrance delay added to a scenario");

	more = base;
	more.name = "renamed";
	ok &= expect(labelOf(base, more) == "Edit", "a changed value is an edit");
	more = base;
	std::swap(more.services[0].id, more.stations[0].id);
	ok &= expect(labelOf(base, more) == "Edit", "a moved value is an edit");

	more = base;
	more.services.push_back(added);
	more.services.push_back(added);
	ok &= expect(labelOf(base, more) == "Edit", "two services added at once are an edit");
	more = base;
	more.services.push_back(added);
	more.tracks.push_back(SceneTrack());
	ok &= expect(labelOf(base, more) == "Edit", "changes to two kinds of part are an edit");

	// The label belongs to the edit and moves with the position.
	SceneHistory history;
	history.reset(base);
	history.record(withScenario);
	history.record(base);
	ok &= expect(history.undoLabel() == "Delete scenario" && history.redoLabel().empty(), "the undo label names the last edit");
	history.undo();
	ok &= expect(history.undoLabel() == "Add scenario" && history.redoLabel() == "Delete scenario", "the labels follow the position");
	history.undo();
	ok &= expect(history.undoLabel().empty() && history.redoLabel() == "Add scenario", "the root has no label to undo");
	return ok;
}

static bool testCommittedScenes(const fs::path& scenes) {
	bool ok = true;
	std::vector<fs::path> directories;
	for (const auto& entry : fs::directory_iterator(scenes)) {
		if (entry.is_directory())
			directories.push_back(entry.path());
	}
	std::sort(directories.begin(), directories.end());
	ok &= expect(!directories.empty(), "the scenes folder holds scenes");

	for (const fs::path& directory : directories) {
		const std::string name = directory.filename().string();
		const SceneLoadResult loaded = loadScene(directory.string());
		if (!expect(!hasErrors(loaded.diagnostics) && !loaded.scene.arcs.empty(), name.c_str())) {
			ok = false;
			continue;
		}

		SceneHistory history;
		history.reset(loaded.scene, true);
		const std::size_t snapshot = history.bytes();
		ok &= expect(snapshot >= loaded.scene.arcs.size() * sizeof(SceneArc), (name + ": the estimate covers the arcs").c_str());

		SceneModel live = loaded.scene;
		for (int step = 1; step <= 101; ++step) {
			live.settings.hasDuration = true;
			live.settings.durationSeconds = step;
			history.record(live);
		}
		const SceneHistoryLimits& limits = history.limits();
		ok &= expect(history.undoCount() <= limits.maxSteps, (name + ": at most 100 undo steps").c_str());
		ok &= expect(history.bytes() <= limits.maxBytes, (name + ": at most 128 MiB").c_str());
		// Only a limit drops steps: either the count is full or one more snapshot would not fit.
		ok &= expect(history.undoCount() == limits.maxSteps || history.bytes() + snapshot > limits.maxBytes,
			(name + ": entries are dropped only by a limit").c_str());
		ok &= expect(history.head()->settings.durationSeconds == 101, (name + ": the newest edit is the live scene").c_str());
	}
	return ok;
}

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "usage: test_scenehistory <scenes directory>\n";
		return 2;
	}
	bool ok = true;
	ok &= testEstimate();
	ok &= testRecordUndoRedo();
	ok &= testRedoBranch();
	ok &= testSavedPoint();
	ok &= testStepLimit();
	ok &= testByteLimit();
	ok &= testReset();
	ok &= testLabels();
	ok &= testCommittedScenes(argv[1]);
	if (ok)
		std::cout << "test_scenehistory passed\n";
	return ok ? 0 : 1;
}
