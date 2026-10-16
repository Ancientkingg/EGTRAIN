#include "editors/SceneDocument.h"

#include <QObject>
#include <QString>

#include <iostream>
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

	if (!ok)
		return 1;
	std::cout << "all scene document tests passed\n";
	return 0;
}
