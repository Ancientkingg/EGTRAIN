#include "editors/SceneDocument.h"

#include <QObject>
#include <QString>

#include <iostream>
#include <set>
#include <string>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static SceneModel namedScene(const char* name) {
	SceneModel scene;
	scene.name = name;
	return scene;
}

// What a document emitted, in order of the signals.
struct Recorder {
	int replaced = 0;
	std::vector<bool> dirty;
	std::vector<bool> editable;
	std::vector<SceneDocument::ChangeKinds> changes;
	// The state seen from inside the changed signal.
	quint64 revisionInChanged = 0;
	bool dirtyInChanged = false;

	explicit Recorder(SceneDocument& document) {
		QObject::connect(&document, &SceneDocument::replaced, &document, [this] { ++replaced; });
		QObject::connect(&document, &SceneDocument::dirtyChanged, &document, [this](bool value) { dirty.push_back(value); });
		QObject::connect(&document, &SceneDocument::editableChanged, &document,
			[this](bool value) { editable.push_back(value); });
		QObject::connect(&document, &SceneDocument::changed, &document, [this, &document](SceneDocument::ChangeKinds kinds) {
			changes.push_back(kinds);
			revisionInChanged = document.revision();
			dirtyInChanged = document.isDirty();
		});
	}
};

int main() {
	bool ok = true;

	{
		SceneDocument document;
		ok &= expect(!document.isLoaded() && !document.isDirty() && document.isEditable() && document.revision() == 0,
			"a new document holds no scene, has no edits and is open for edits");
	}
	{
		SceneDocument document;
		Recorder recorder(document);
		document.replace(namedScene("first"), true);
		ok &= expect(document.isLoaded() && document.isDirty() && document.revision() == 1
				&& document.model().name == "first",
			"replace takes the model, marks the document loaded and sets the dirty state");
		ok &= expect(recorder.replaced == 1 && recorder.dirty == std::vector<bool>{true} && recorder.changes.empty(),
			"replace emits replaced once and dirtyChanged for the new state, but not changed");
		document.replace(namedScene("second"), false);
		ok &= expect(!document.isDirty() && document.revision() == 2 && document.model().name == "second",
			"replace sets a clean state for an opened scene");
		ok &= expect(recorder.replaced == 2 && recorder.dirty == std::vector<bool>({true, false}),
			"replace emits dirtyChanged only when the state changes");
		document.replace(namedScene("third"), false);
		ok &= expect(recorder.dirty.size() == 2 && document.revision() == 3,
			"a replace that keeps the dirty state still advances the revision");
	}
	{
		SceneDocument document;
		document.replace(namedScene("scene"), false);
		Recorder recorder(document);
		document.mutableModel().name = "edited";
		ok &= expect(document.model().name == "edited", "an edit through mutableModel is seen through model");
		ok &= expect(!document.isDirty() && document.revision() == 1 && recorder.changes.empty(),
			"editing the model does nothing until markChanged");

		document.markChanged(SceneDocument::CaseSettings | SceneDocument::Services, "Edit case");
		ok &= expect(document.isDirty() && document.revision() == 2, "markChanged sets the dirty state and advances the revision");
		ok &= expect(recorder.changes.size() == 1
				&& recorder.changes[0] == (SceneDocument::CaseSettings | SceneDocument::Services),
			"markChanged emits changed once with the kinds");
		ok &= expect(recorder.revisionInChanged == 2 && recorder.dirtyInChanged,
			"the state is up to date when changed is emitted");
		ok &= expect(recorder.dirty == std::vector<bool>{true} && document.lastChangeLabel() == "Edit case",
			"the first edit emits dirtyChanged and the label is kept");

		document.markChanged(SceneDocument::Passengers);
		ok &= expect(recorder.dirty.size() == 1 && recorder.changes.size() == 2 && document.revision() == 3
				&& document.lastChangeLabel().isEmpty(),
			"a further edit emits changed but not dirtyChanged, and its label replaces the earlier one");

		document.markSaved();
		ok &= expect(!document.isDirty() && document.revision() == 3 && recorder.dirty == std::vector<bool>({true, false}),
			"markSaved clears the dirty state and keeps the revision");
		document.markSaved();
		ok &= expect(recorder.dirty.size() == 2, "markSaved on a clean document emits nothing");

		document.markChanged(SceneDocument::AllChanges);
		ok &= expect(!recorder.changes.empty() && recorder.changes.back() == SceneDocument::AllChanges && document.isDirty()
				&& document.revision() == 4,
			"an edit after a save makes the document dirty again");
		document.setDirty(false);
		document.setDirty(false);
		ok &= expect(!document.isDirty() && document.revision() == 4 && recorder.dirty.size() == 4,
			"setDirty changes only the dirty state and emits once per change");
	}
	{
		SceneDocument document;
		Recorder recorder(document);
		document.setEditable(true);
		ok &= expect(recorder.editable.empty(), "setEditable with the current state emits nothing");
		document.setEditable(false);
		ok &= expect(!document.isEditable() && recorder.editable == std::vector<bool>{false},
			"setEditable(false) closes the document for edits and emits editableChanged");
		document.setEditable(false);
		document.setEditable(true);
		ok &= expect(document.isEditable() && recorder.editable == std::vector<bool>({false, true}),
			"setEditable emits once per change");
		ok &= expect(document.revision() == 0 && !document.isDirty(), "editability is not an edit");
	}
	{
		SceneDocument document;
		quint64 previous = document.revision();
		bool increasing = true;
		const auto step = [&] {
			increasing = increasing && document.revision() > previous;
			previous = document.revision();
		};
		document.replace(namedScene("a"), false);
		step();
		document.markChanged(SceneDocument::Infrastructure);
		step();
		document.markSaved();
		document.markChanged(SceneDocument::Scenarios);
		step();
		document.replace(namedScene("b"), true);
		step();
		ok &= expect(increasing, "the revision grows with every replace and every markChanged");
	}

	{
		SceneModel live = namedScene("live");
		live.description = "live description";
		live.savedWithAppVersion = "live version";
		live.sourceFiles = {"live.json"};
		SceneLoadedData liveData;
		liveData.category = "live data";
		live.loadedData = {liveData};
		live.services.resize(1);
		live.services[0].id = "live service";

		SceneModel snapshot = namedScene("snapshot");
		snapshot.savedWithAppVersion = "snapshot version";
		snapshot.sourceFiles = {"snapshot.json", "other.json"};
		SceneLoadedData snapshotData;
		snapshotData.category = "snapshot data";
		snapshot.loadedData = {snapshotData, snapshotData};
		snapshot.baseTime = "08:00:00";
		snapshot.defaultScenarioId = "scenario";
		snapshot.scenarios.resize(1);
		snapshot.scenarios[0].id = "scenario";
		snapshot.passengers.resize(2);
		snapshot.passengers[1].id = "passenger";
		snapshot.importReport.resize(1);
		snapshot.importReport[0].category = "report";

		assignAuthoredScene(live, snapshot);
		ok &= expect(live.name == "snapshot" && live.description.empty() && live.baseTime == "08:00:00"
				&& live.defaultScenarioId == "scenario" && live.scenarios.size() == 1 && live.scenarios[0].id == "scenario"
				&& live.services.empty() && live.passengers.size() == 2 && live.passengers[1].id == "passenger"
				&& live.importReport.size() == 1 && live.importReport[0].category == "report",
			"assignAuthoredScene takes the authored parts of the snapshot, also where the snapshot has less than the scene");
		ok &= expect(live.savedWithAppVersion == "live version" && live.sourceFiles == std::set<std::string>{"live.json"}
				&& live.loadedData.size() == 1 && live.loadedData[0].category == "live data",
			"assignAuthoredScene keeps the loaded data, the source files and the saving version of the scene");
	}
	{
		SceneDocument document;
		document.replace(namedScene("opened"), false);
		Recorder recorder(document);
		document.mutableModel().sourceFiles = {"opened.json"};
		const quint64 revision = document.revision();

		document.restore(namedScene("earlier"), true);
		ok &= expect(document.model().name == "earlier" && document.model().sourceFiles == std::set<std::string>{"opened.json"},
			"restore takes the authored parts of the snapshot and keeps the derived ones");
		ok &= expect(document.isLoaded() && document.isEditable() && document.isDirty() && document.revision() == revision + 1,
			"restore advances the revision and sets the dirty state");
		ok &= expect(recorder.dirty == std::vector<bool>{true} && recorder.changes.empty() && recorder.replaced == 0,
			"restore emits dirtyChanged for a new dirty state, but neither changed nor replaced");

		document.restore(namedScene("opened"), false);
		ok &= expect(!document.isDirty() && document.revision() == revision + 2 && recorder.dirty == std::vector<bool>({true, false}),
			"restore to the saved state clears the dirty state");
		document.restore(namedScene("opened"), false);
		ok &= expect(recorder.dirty.size() == 2 && document.revision() == revision + 3,
			"a restore that keeps the dirty state still advances the revision");
	}

	if (!ok)
		return 1;
	std::cout << "all scene document tests passed\n";
	return 0;
}
