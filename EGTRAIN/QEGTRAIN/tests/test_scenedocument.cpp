#include "editors/SceneDocument.h"

#include <QObject>
#include <QString>
#include <QTest>

#include <set>
#include <string>
#include <vector>

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

class TestSceneDocument : public QObject {
	Q_OBJECT

private slots:
	void newDocumentHoldsNoScene();
	void replaceTakesModel();
	void markChangedTracksEdits();
	void setEditableEmitsOncePerChange();
	void revisionGrows();
	void assignAuthoredSceneKeepsDerivedParts();
	void restoreKeepsDerivedParts();
};

// A new document holds no scene, has no edits and is open for edits.
void TestSceneDocument::newDocumentHoldsNoScene() {
	SceneDocument document;
	QVERIFY(!document.isLoaded());
	QVERIFY(!document.isDirty());
	QVERIFY(document.isEditable());
	QCOMPARE(document.revision(), quint64(0));
}

void TestSceneDocument::replaceTakesModel() {
	SceneDocument document;
	Recorder recorder(document);
	document.replace(namedScene("first"), true);
	// Replace takes the model, marks the document loaded and sets the dirty state.
	QVERIFY(document.isLoaded());
	QVERIFY(document.isDirty());
	QCOMPARE(document.revision(), quint64(1));
	QCOMPARE(document.model().name, "first");
	// Replace emits replaced once and dirtyChanged for the new state, but not changed.
	QCOMPARE(recorder.replaced, 1);
	QCOMPARE(recorder.dirty, std::vector<bool>{true});
	QVERIFY(recorder.changes.empty());
	document.replace(namedScene("second"), false);
	// Replace sets a clean state for an opened scene.
	QVERIFY(!document.isDirty());
	QCOMPARE(document.revision(), quint64(2));
	QCOMPARE(document.model().name, "second");
	// Replace emits dirtyChanged only when the state changes.
	QCOMPARE(recorder.replaced, 2);
	QCOMPARE(recorder.dirty, std::vector<bool>({true, false}));
	document.replace(namedScene("third"), false);
	// A replace that keeps the dirty state still advances the revision.
	QCOMPARE(recorder.dirty.size(), std::size_t(2));
	QCOMPARE(document.revision(), quint64(3));
}

void TestSceneDocument::markChangedTracksEdits() {
	SceneDocument document;
	document.replace(namedScene("scene"), false);
	Recorder recorder(document);
	document.mutableModel().name = "edited";
	// An edit through mutableModel is seen through model.
	QCOMPARE(document.model().name, "edited");
	// Editing the model does nothing until markChanged.
	QVERIFY(!document.isDirty());
	QCOMPARE(document.revision(), quint64(1));
	QVERIFY(recorder.changes.empty());

	document.markChanged(SceneDocument::CaseSettings | SceneDocument::Services, "Edit case");
	// markChanged sets the dirty state and advances the revision.
	QVERIFY(document.isDirty());
	QCOMPARE(document.revision(), quint64(2));
	// markChanged emits changed once with the kinds.
	QCOMPARE(recorder.changes.size(), std::size_t(1));
	QCOMPARE(recorder.changes[0], SceneDocument::CaseSettings | SceneDocument::Services);
	// The state is up to date when changed is emitted.
	QCOMPARE(recorder.revisionInChanged, quint64(2));
	QVERIFY(recorder.dirtyInChanged);
	// The first edit emits dirtyChanged and the label is kept.
	QCOMPARE(recorder.dirty, std::vector<bool>{true});
	QCOMPARE(document.lastChangeLabel(), "Edit case");

	document.markChanged(SceneDocument::Passengers);
	// A further edit emits changed but not dirtyChanged, and its label replaces the earlier one.
	QCOMPARE(recorder.dirty.size(), std::size_t(1));
	QCOMPARE(recorder.changes.size(), std::size_t(2));
	QCOMPARE(document.revision(), quint64(3));
	QVERIFY(document.lastChangeLabel().isEmpty());

	document.markSaved();
	// markSaved clears the dirty state and keeps the revision.
	QVERIFY(!document.isDirty());
	QCOMPARE(document.revision(), quint64(3));
	QCOMPARE(recorder.dirty, std::vector<bool>({true, false}));
	document.markSaved();
	// markSaved on a clean document emits nothing.
	QCOMPARE(recorder.dirty.size(), std::size_t(2));

	document.markChanged(SceneDocument::AllChanges);
	// An edit after a save makes the document dirty again.
	QVERIFY(!recorder.changes.empty());
	QCOMPARE(recorder.changes.back(), SceneDocument::ChangeKinds(SceneDocument::AllChanges));
	QVERIFY(document.isDirty());
	QCOMPARE(document.revision(), quint64(4));
	document.setDirty(false);
	document.setDirty(false);
	// setDirty changes only the dirty state and emits once per change.
	QVERIFY(!document.isDirty());
	QCOMPARE(document.revision(), quint64(4));
	QCOMPARE(recorder.dirty.size(), std::size_t(4));
}

void TestSceneDocument::setEditableEmitsOncePerChange() {
	SceneDocument document;
	Recorder recorder(document);
	document.setEditable(true);
	QVERIFY2(recorder.editable.empty(), "setEditable with the current state emits nothing");
	document.setEditable(false);
	// setEditable(false) closes the document for edits and emits editableChanged.
	QVERIFY(!document.isEditable());
	QCOMPARE(recorder.editable, std::vector<bool>{false});
	document.setEditable(false);
	document.setEditable(true);
	// setEditable emits once per change.
	QVERIFY(document.isEditable());
	QCOMPARE(recorder.editable, std::vector<bool>({false, true}));
	// Editability is not an edit.
	QCOMPARE(document.revision(), quint64(0));
	QVERIFY(!document.isDirty());
}

void TestSceneDocument::revisionGrows() {
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
	QVERIFY2(increasing, "the revision grows with every replace and every markChanged");
}

void TestSceneDocument::assignAuthoredSceneKeepsDerivedParts() {
	SceneModel live = namedScene("live");
	live.description = "live description";
	live.savedWithAppVersion = "live version";
	live.sourceFiles = {"live.json"};
	live.services.resize(1);
	live.services[0].id = "live service";

	SceneModel snapshot = namedScene("snapshot");
	snapshot.savedWithAppVersion = "snapshot version";
	snapshot.sourceFiles = {"snapshot.json", "other.json"};
	snapshot.baseTime = "08:00:00";
	snapshot.defaultScenarioId = "scenario";
	snapshot.scenarios.resize(1);
	snapshot.scenarios[0].id = "scenario";
	snapshot.passengers.resize(2);
	snapshot.passengers[1].id = "passenger";
	snapshot.importReport.resize(1);
	snapshot.importReport[0].category = "report";

	assignAuthoredScene(live, snapshot);
	// assignAuthoredScene takes the authored parts of the snapshot, also where the snapshot has less than the scene.
	QCOMPARE(live.name, "snapshot");
	QVERIFY(live.description.empty());
	QCOMPARE(live.baseTime, "08:00:00");
	QCOMPARE(live.defaultScenarioId, "scenario");
	QCOMPARE(live.scenarios.size(), std::size_t(1));
	QCOMPARE(live.scenarios[0].id, "scenario");
	QVERIFY(live.services.empty());
	QCOMPARE(live.passengers.size(), std::size_t(2));
	QCOMPARE(live.passengers[1].id, "passenger");
	QCOMPARE(live.importReport.size(), std::size_t(1));
	QCOMPARE(live.importReport[0].category, "report");
	// assignAuthoredScene keeps the source files and the saving version of the scene.
	QCOMPARE(live.savedWithAppVersion, "live version");
	QCOMPARE(live.sourceFiles, std::set<std::string>{"live.json"});
}

void TestSceneDocument::restoreKeepsDerivedParts() {
	SceneDocument document;
	document.replace(namedScene("opened"), false);
	Recorder recorder(document);
	document.mutableModel().sourceFiles = {"opened.json"};
	const quint64 revision = document.revision();

	document.restore(namedScene("earlier"), true);
	// Restore takes the authored parts of the snapshot and keeps the derived ones.
	QCOMPARE(document.model().name, "earlier");
	QCOMPARE(document.model().sourceFiles, std::set<std::string>{"opened.json"});
	// Restore advances the revision and sets the dirty state.
	QVERIFY(document.isLoaded());
	QVERIFY(document.isEditable());
	QVERIFY(document.isDirty());
	QCOMPARE(document.revision(), revision + 1);
	// Restore emits dirtyChanged for a new dirty state, but neither changed nor replaced.
	QCOMPARE(recorder.dirty, std::vector<bool>{true});
	QVERIFY(recorder.changes.empty());
	QCOMPARE(recorder.replaced, 0);

	document.restore(namedScene("opened"), false);
	// Restore to the saved state clears the dirty state.
	QVERIFY(!document.isDirty());
	QCOMPARE(document.revision(), revision + 2);
	QCOMPARE(recorder.dirty, std::vector<bool>({true, false}));
	document.restore(namedScene("opened"), false);
	// A restore that keeps the dirty state still advances the revision.
	QCOMPARE(recorder.dirty.size(), std::size_t(2));
	QCOMPARE(document.revision(), revision + 3);
}

QTEST_APPLESS_MAIN(TestSceneDocument)
#include "test_scenedocument.moc"
