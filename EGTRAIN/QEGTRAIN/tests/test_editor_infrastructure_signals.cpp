#include "editors/InfrastructurePanel.h"
#include "editors/infrastructure/SignalFacets.h"

#include "scene/SectionInventory.h"
#include "scene/SignallingLevel.h"
#include "scene/SignallingLevelNames.h"

#include <QApplication>
#include <QComboBox>
#include <QPoint>
#include <QPointF>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QVariant>
#include <QWheelEvent>

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static SceneNode node(const char* id, const char* trackId, double xKm) {
	SceneNode result;
	result.id = id;
	result.trackId = trackId;
	result.xKm = xKm;
	return result;
}

static SceneArc arc(const char* id, const char* trackId, const char* from, const char* to) {
	SceneArc result;
	result.id = id;
	result.trackId = trackId;
	result.fromNodeId = from;
	result.toNodeId = to;
	result.speedLimitMs = 20.0;
	return result;
}

static SceneBlock block(const char* id, const char* trackId, double lengthKm) {
	SceneBlock result;
	result.id = id;
	result.trackId = trackId;
	result.lengthKm = lengthKm;
	return result;
}

static SceneSignallingArea area(const char* id, double startKm, double endKm, int level, const char* trackId) {
	SceneSignallingArea result;
	result.id = id;
	result.startKm = startKm;
	result.endKm = endKm;
	result.level = level;
	result.trackId = trackId;
	return result;
}

static SceneIncident incident(const char* id, const char* type, const char* target) {
	SceneIncident result;
	result.id = id;
	result.type = type;
	result.target = target;
	return result;
}

// The runtime id of the section that a block gives, which is what the combos of sections store.
static std::string sectionOf(const SceneModel& scene, const char* blockId) {
	for (const SceneSectionDescriptor& section : buildSceneSectionInventory(scene).sections)
		if (section.sourceBlockId == blockId)
			return section.id;
	return {};
}

// Two tracks, the first with two blocks of one kilometre and the second with one of half a kilometre. Two signals protect the
// blocks of the first track, and the second protects none. A signal-failure incident targets the first signal, and a train
// breakdown targets a service that has the same id, so that a rewrite can be told to reach the first and not the second. Two
// signalling areas cover a block each: the first on the first track and the second on all tracks.
static SceneModel signalScene() {
	SceneModel scene;
	scene.name = "Signals";
	scene.tracks = {{"track-a"}, {"track-b"}};
	scene.nodes = {node("node-a0", "track-a", 0.0), node("node-a1", "track-a", 1.0), node("node-a2", "track-a", 2.0), node("node-b0", "track-b", 0.0),
		node("node-b1", "track-b", 0.5)};
	scene.arcs = {arc("arc-a1", "track-a", "node-a0", "node-a1"), arc("arc-a2", "track-a", "node-a1", "node-a2"),
		arc("arc-b1", "track-b", "node-b0", "node-b1")};
	scene.blocks = {block("block-a1", "track-a", 1.0), block("block-a2", "track-a", 1.0), block("block-b1", "track-b", 0.5)};
	sceneSignals(scene) = {{"signal-1", sectionOf(scene, "block-a1")}, {"signal-2", ""}};
	scene.signallingAreas = {area("area-1", 0.0, 1.0, 0, "track-a"), area("area-2", 1.0, 2.0, 2, "")};

	SceneScenario scenario;
	scenario.id = "scenario-1";
	scenario.incidents = {incident("incident-1", "signal_failure", "signal-1"), incident("incident-2", "train_breakdown", "signal-1")};
	scene.scenarios.push_back(scenario);
	return scene;
}

// Answers the questions of the panel and records the messages, without a dialog.
class FakePrompts : public UserPrompts {
public:
	int errorCount = 0;
	QString errorMessage;
	int confirmCount = 0;
	QString confirmQuestion;
	bool confirmAnswer = false;

	void showError(QWidget*, const QString&, const QString& message, bool = false) override {
		++errorCount;
		errorMessage = message;
	}
	bool confirm(QWidget*, const QString&, const QString& question) override {
		++confirmCount;
		confirmQuestion = question;
		return confirmAnswer;
	}
};

using Log = std::vector<std::string>;

// A panel with the facets of the signalling, over a document that holds the scene. The events are the order in which the
// panel and its document report an edit, and the messages that the panel shows in the status line.
struct Fixture {
	SceneDocument document;
	InfrastructurePanel panel;
	FakePrompts prompts;
	Log log;
	QStringList entities;
	SceneDocument::ChangeKinds kinds;
	QStringList messages;

	explicit Fixture(SceneModel scene = signalScene()) {
		panel.setPrompts(&prompts);
		for (std::unique_ptr<InfrastructureFacet>& facet : makeSignalFacets())
			panel.setFacet(std::move(facet));
		panel.setDocument(&document);
		document.replace(std::move(scene), false);
		panel.refresh();
		QObject::connect(&document, &SceneDocument::changed, &panel, [this](SceneDocument::ChangeKinds changedKinds) {
			log.push_back("documentChanged");
			kinds = changedKinds;
		});
		QObject::connect(&panel, &InfrastructurePanel::changed, &panel, [this] { log.push_back("panelChanged"); });
		QObject::connect(&panel, &InfrastructurePanel::entitiesChanged, &panel, [this](const QString& facet) {
			log.push_back("entitiesChanged");
			entities << facet;
		});
		QObject::connect(&panel, &InfrastructurePanel::statusMessage, &panel, [this](const QString& text, int) { messages << text; });
	}

	void choose(const char* key) { panel.facetCombo()->setCurrentIndex(panel.facetCombo()->findData(key)); }
	QTableWidget* table() { return panel.table(); }
	const SceneModel& model() const { return document.model(); }
	SceneModel& edit() { return document.mutableModel(); }
	// Selects the row and lists the entities again, so that the buttons follow the selected row.
	void select(int row) {
		table()->setCurrentCell(row, 0);
		panel.refresh();
	}
	void type(int row, int column, const char* text) { table()->item(row, column)->setText(text); }
	QString cell(int row, int column) { return table()->item(row, column)->text(); }
	QStringList headers() {
		QStringList result;
		for (int column = 0; column < table()->columnCount(); ++column)
			result << table()->horizontalHeaderItem(column)->text();
		return result;
	}
	QComboBox* combo(int row, int column) { return qobject_cast<QComboBox*>(table()->cellWidget(row, column)); }
	// Chooses the section that the block gives in the combo.
	void chooseBlock(int row, const char* blockId) {
		QComboBox* box = combo(row, 1);
		box->setCurrentIndex(box->findData(QString::fromStdString(sectionOf(model(), blockId))));
	}
	// Chooses the item of the combo that has this data.
	void choose(QComboBox* box, const QVariant& data) { box->setCurrentIndex(box->findData(data)); }
	// True when the document has been changed once, under this label, and the panel has said so, and the table was listed
	// again for the entities of this facet.
	bool changedOnce(const char* label, const char* facet) {
		const bool once = log == Log({"documentChanged", "panelChanged", "entitiesChanged"}) && entities == QStringList({facet})
			&& kinds == SceneDocument::Infrastructure && document.lastChangeLabel() == label && document.isDirty();
		log.clear();
		entities.clear();
		return once;
	}
	// True when the document has been changed once under this label by a widget of the table, so that the facet is not told
	// that its entities changed.
	bool editedOnce(const char* label) {
		const bool once = log == Log({"documentChanged", "panelChanged"}) && entities.isEmpty() && kinds == SceneDocument::Infrastructure
			&& document.lastChangeLabel() == label && document.isDirty();
		log.clear();
		return once;
	}
	// True when no change has been reported since the last check.
	bool untouched() {
		const bool still = log.empty();
		log.clear();
		return still;
	}
};

static QString blocked(const char* kind, const char* id, const QStringList& consumers) {
	return QString("Cannot delete %1 '%2' because it is still referenced by:\n\xE2\x80\xA2 %3\nRemove or rebind those consumers first.")
		.arg(QString(kind), QString(id), consumers.join("\n\xE2\x80\xA2 "));
}

static bool testFacets() {
	bool ok = true;
	const std::vector<std::unique_ptr<InfrastructureFacet>> facets = makeSignalFacets();
	QStringList keys;
	for (const std::unique_ptr<InfrastructureFacet>& facet : facets)
		keys << facet->key();
	ok &= expect(keys == QStringList({"signals", "signalling_areas"}), "the signalling has two facets");

	Fixture f;
	ok &= expect(f.panel.facet("signals") && f.panel.facet("signalling_areas") && !f.panel.facet("blocks") && !f.panel.facet("stations"),
		"the panel has the facets of the signalling and no others");
	f.choose("signals");
	f.document.setEditable(false);
	f.panel.refresh();
	f.panel.addEntity();
	f.panel.deleteEntity();
	f.panel.commitCell(0, 0);
	f.panel.commitPendingEditors();
	ok &= expect(f.untouched() && sceneSignals(f.model()).size() == 2 && sceneSignals(f.model())[0].id == "signal-1",
		"nothing is edited, added or deleted while a run uses the scene");
	ok &= expect(!f.table()->isEnabled(), "the table cannot be used while a run uses the scene");
	return ok;
}

static bool testSignalList() {
	bool ok = true;
	Fixture f;
	f.choose("signals");
	ok &= expect(f.headers() == QStringList({"ID", "Protected section"}) && f.table()->rowCount() == 2 && f.cell(0, 0) == "signal-1" && f.cell(1, 0) == "signal-2",
		"signals are listed with their ids");
	QComboBox* first = f.combo(0, 1);
	QComboBox* second = f.combo(1, 1);
	ok &= expect(first && second && first->objectName() == "protectedSectionCombo" && first->accessibleName() == "Protected section for signal-1"
			&& second->accessibleName() == "Protected section for signal-2",
		"the protected section is chosen in a combo with a name for each signal");
	ok &= expect(first->currentData().toString().toStdString() == sectionOf(f.model(), "block-a1") && second->currentText() == "(unbound)"
			&& second->currentData().toString().isEmpty(),
		"a signal shows the section that it protects, and none as unbound");
	// the sections of the scene, one for each block
	ok &= expect(first->count() == 4 && first->itemData(0).toString().isEmpty(), "the combo offers none and the sections of the scene");
	ok &= expect(f.untouched() && !f.document.isDirty(), "listing signals edits nothing");
	return ok;
}

static bool testSignalEdit() {
	bool ok = true;
	Fixture f;
	f.choose("signals");

	// protected section
	QComboBox* second = f.combo(1, 1);
	f.chooseBlock(1, "block-a2");
	ok &= expect(sceneSignals(f.model())[1].protectedSection == sectionOf(f.model(), "block-a2") && f.editedOnce("Edit signal") && f.combo(1, 1) == second,
		"a section is chosen, which marks the document once and leaves the combo in the table");
	f.chooseBlock(1, "block-a2");
	ok &= expect(f.untouched(), "choosing the section that is chosen changes nothing");
	f.choose(second, QString());
	ok &= expect(sceneSignals(f.model())[1].protectedSection.empty() && f.editedOnce("Edit signal"), "choosing none unbinds the signal");

	// rename: only the signal-failure incidents that target the signal follow
	f.type(0, 0, " signal-x ");
	const SceneModel& model = f.model();
	ok &= expect(sceneSignals(model)[0].id == "signal-x" && sceneSignals(model)[1].id == "signal-2" && model.scenarios[0].incidents[0].target == "signal-x"
			&& model.scenarios[0].incidents[1].target == "signal-1",
		"a renamed signal keeps its signal-failure incidents, and leaves a breakdown that has the old id as it is");
	ok &= expect(sceneSignals(model)[0].protectedSection == sectionOf(model, "block-a1"), "a renamed signal keeps its section");
	ok &= expect(f.changedOnce("Rename signal", "signals") && f.panel.selectionId() == "signal-x" && f.table()->currentRow() == 0,
		"a rename marks the document once, and the renamed signal stays selected");
	f.type(0, 0, " signal-x ");
	ok &= expect(f.untouched() && f.messages.empty(), "the same id changes nothing");
	f.type(0, 0, "signal-2");
	ok &= expect(f.untouched() && sceneSignals(model)[0].id == "signal-x" && f.cell(0, 0) == "signal-x"
			&& f.messages == QStringList({"Infrastructure IDs must be non-empty and unique"}),
		"an id that another signal has is refused, and the table shows the model again");
	f.type(0, 0, "");
	ok &= expect(f.untouched() && sceneSignals(model)[0].id == "signal-x" && f.messages.size() == 2, "an empty id is refused");
	return ok;
}

static bool testSignalRow() {
	bool ok = true;
	// A combo that belongs to a row the model no longer has does nothing.
	Fixture f;
	f.choose("signals");
	QComboBox* second = f.combo(1, 1);
	sceneSignals(f.edit()).pop_back();
	f.chooseBlock(1, "block-a1");
	ok &= expect(second && f.untouched() && sceneSignals(f.model()).size() == 1 && sceneSignals(f.model())[0].protectedSection == sectionOf(f.model(), "block-a1"),
		"a combo of a row that is gone changes nothing");
	return ok;
}

static bool testSignalAddAndDelete() {
	bool ok = true;
	Fixture f;
	f.choose("signals");
	f.panel.addButton()->click();
	ok &= expect(sceneSignals(f.model()).size() == 3 && sceneSignals(f.model())[2].id == "signal" && sceneSignals(f.model())[2].protectedSection.empty()
			&& f.changedOnce("Add signal", "signals") && f.panel.selectionId() == "signal" && f.table()->currentRow() == 2,
		"a signal is added unbound with a new id, and selected");
	ok &= expect(f.combo(2, 1)->currentText() == "(unbound)", "the new signal is listed unbound");
	f.panel.addButton()->click();
	ok &= expect(sceneSignals(f.model()).size() == 4 && sceneSignals(f.model())[3].id == "signal_2" && f.changedOnce("Add signal", "signals"),
		"another signal gets the next free id");

	// delete
	f.select(0);
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.errorCount == 1 && f.prompts.confirmCount == 0 && sceneSignals(f.model()).size() == 4 && f.untouched(),
		"a signal that an incident targets cannot be deleted");
	ok &= expect(f.prompts.errorMessage == blocked("signals", "signal-1", {"signal-failure incident 'incident-1' in scenario 'scenario-1'"}),
		"the message names the signal-failure incident that targets the signal");
	f.select(1);
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.confirmCount == 1 && f.prompts.confirmQuestion == "Delete signals 'signal-2'?" && sceneSignals(f.model()).size() == 4 && f.untouched(),
		"nothing is deleted when the user answers no");
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	ok &= expect(sceneSignals(f.model()).size() == 3 && sceneSignals(f.model())[1].id == "signal" && f.changedOnce("Delete signal", "signals")
			&& f.panel.selectionId() == "signal",
		"a signal that no incident targets is deleted when the user answers yes, and the next row is selected");
	return ok;
}

static bool testAreaList() {
	bool ok = true;
	Fixture f;
	f.choose("signalling_areas");
	ok &= expect(f.headers() == QStringList({"ID", "Start km", "End km", "Signalling system", "Track"}) && f.table()->rowCount() == 2,
		"signalling areas are listed with their range, system and track");
	ok &= expect(f.cell(0, 0) == "area-1" && f.cell(0, 1) == "0" && f.cell(0, 2) == "1" && f.cell(1, 0) == "area-2" && f.cell(1, 1) == "1" && f.cell(1, 2) == "2",
		"each row has its id, start and end");
	QComboBox* system = f.combo(0, 3);
	QComboBox* track = f.combo(0, 4);
	ok &= expect(system && track && system->objectName() == "signallingAreaSystemCombo" && track->objectName() == "signallingAreaTrackCombo"
			&& system->accessibleName() == "Signalling system for area-1" && track->accessibleName() == "Track for area-1",
		"the system and the track are chosen in combos with a name for each area");
	ok &= expect(system->count() == 6 && system->currentData().toInt() == 0 && f.combo(1, 3)->currentData().toInt() == 2,
		"a system combo offers the six signalling systems and shows the one of the area");
	ok &= expect(track->count() == 3 && track->itemData(0).toString().isEmpty() && track->currentData().toString() == "track-a"
			&& f.combo(1, 4)->currentData().toString().isEmpty() && f.combo(1, 4)->currentText() == "(all tracks)",
		"a track combo offers all tracks and each track, and shows the track of the area");
	ok &= expect(track->itemText(1) == "track-a (blocks 0.000000 to 2.000000 km)" && track->itemText(2) == "track-b (blocks 0.000000 to 0.500000 km)",
		"a track is shown with the extent of its blocks");
	ok &= expect(f.untouched() && !f.document.isDirty(), "listing signalling areas edits nothing");
	return ok;
}

static bool testAreaEdit() {
	bool ok = true;
	Fixture f;
	f.choose("signalling_areas");

	// range
	f.type(0, 1, "0.25");
	ok &= expect(f.model().signallingAreas[0].startKm == 0.25 && f.changedOnce("Edit signalling area", "signalling_areas"), "the start of an area is edited");
	f.type(0, 2, " 1.75 ");
	ok &= expect(f.model().signallingAreas[0].endKm == 1.75 && f.changedOnce("Edit signalling area", "signalling_areas"), "the end of an area is edited");
	f.type(0, 1, " 0.25 ");
	ok &= expect(f.untouched(), "a value that is the same changes nothing");
	f.type(0, 2, "far");
	ok &= expect(f.untouched() && f.model().signallingAreas[0].endKm == 1.75 && f.cell(0, 2) == "1.75" && f.messages.empty(),
		"an end that is no number is refused, without a message, and the table shows the model again");

	// rename: nothing refers to an area
	f.type(1, 0, " area-x ");
	ok &= expect(f.model().signallingAreas[1].id == "area-x" && f.model().signallingAreas[1].level == 2 && f.changedOnce("Rename signalling area", "signalling_areas")
			&& f.panel.selectionId() == "area-x" && f.table()->currentRow() == 1,
		"a rename marks the document once, and the renamed area stays selected");
	f.type(1, 0, "area-1");
	ok &= expect(f.untouched() && f.model().signallingAreas[1].id == "area-x" && f.messages == QStringList({"Infrastructure IDs must be non-empty and unique"}),
		"an id that another area has is refused");
	f.type(1, 0, "");
	ok &= expect(f.untouched() && f.messages.size() == 2, "an empty id is refused");
	return ok;
}

static bool testAreaCombos() {
	bool ok = true;
	Fixture f;
	f.choose("signalling_areas");

	// system
	QComboBox* system = f.combo(0, 3);
	f.choose(system, 4);
	ok &= expect(f.model().signallingAreas[0].level == 4 && f.editedOnce("Edit signalling area") && f.combo(0, 3) == system,
		"another system is chosen, which marks the document once and leaves the combo in the table");
	f.choose(system, 4);
	ok &= expect(f.untouched(), "choosing the system that is chosen changes nothing");

	// track
	QComboBox* track = f.combo(0, 4);
	f.choose(track, "track-b");
	ok &= expect(f.model().signallingAreas[0].trackId == "track-b" && f.editedOnce("Edit signalling area") && f.combo(0, 4) == track,
		"another track is chosen, which marks the document once and leaves the combo in the table");
	f.choose(track, QString());
	ok &= expect(f.model().signallingAreas[0].trackId.empty() && f.editedOnce("Edit signalling area"), "all tracks can be chosen");
	f.choose(track, QString());
	ok &= expect(f.untouched(), "choosing the track that is chosen changes nothing");

	// a combo of a row that is gone does nothing
	QComboBox* last = f.combo(1, 3);
	f.edit().signallingAreas.pop_back();
	f.choose(last, 5);
	ok &= expect(f.untouched() && f.model().signallingAreas.size() == 1, "a combo of a row that is gone changes nothing");
	return ok;
}

static bool testAreaPlaceholders() {
	bool ok = true;
	SceneModel scene = signalScene();
	scene.signallingAreas = {area("unchosen", 0.0, 1.0, kSignallingLevelUnset, "track-a"), area("odd", 0.0, 1.0, 9, "nowhere")};
	Fixture f(scene);
	f.choose("signalling_areas");

	// an area without a system shows an item that stands for it, until a system is chosen
	QComboBox* unchosen = f.combo(0, 3);
	ok &= expect(unchosen->count() == 7 && unchosen->currentText() == "Choose a signalling system" && unchosen->currentData().toInt() == kSignallingLevelUnset,
		"an area without a system shows an item that asks for one");
	f.choose(unchosen, 1);
	ok &= expect(f.model().signallingAreas[0].level == 1 && unchosen->count() == 6 && unchosen->currentData().toInt() == 1 && f.editedOnce("Edit signalling area"),
		"choosing a system takes the item away");

	// a level that is no system is shown as it is stored
	QComboBox* odd = f.combo(1, 3);
	ok &= expect(odd->count() == 7 && odd->currentData().toInt() == 9 && odd->currentText() == QString::fromStdString(signallingLevelName(9)),
		"a level that is no system is shown as stored");
	f.choose(odd, 3);
	ok &= expect(f.model().signallingAreas[1].level == 3 && odd->count() == 6 && f.editedOnce("Edit signalling area"), "choosing a system takes that item away");

	// a track that the scene does not have is shown on top, until a track is chosen
	QComboBox* nowhere = f.combo(1, 4);
	ok &= expect(nowhere->count() == 4 && nowhere->itemText(0) == "Invalid track: nowhere" && nowhere->currentData().toString() == "nowhere",
		"a track that the scene does not have is shown as invalid");
	f.choose(nowhere, "track-a");
	ok &= expect(f.model().signallingAreas[1].trackId == "track-a" && nowhere->count() == 3 && nowhere->currentData().toString() == "track-a"
			&& f.editedOnce("Edit signalling area"),
		"choosing a track takes the invalid item away");
	return ok;
}

static bool testAreaWheel() {
	bool ok = true;
	Fixture f;
	f.choose("signalling_areas");
	// The wheel scrolls the table behind a combo and does not change what the combo has chosen.
	for (const int column : {3, 4}) {
		QComboBox* box = f.combo(1, column);
		const int index = box->currentIndex();
		QWheelEvent wheel(QPointF(2, 2), box->mapToGlobal(QPoint(2, 2)), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
		QApplication::sendEvent(box, &wheel);
		ok &= expect(!wheel.isAccepted() && box->currentIndex() == index && f.untouched(), "the wheel does not change a combo of an area");
	}
	return ok;
}

static bool testAreaAddAndDelete() {
	bool ok = true;
	Fixture f;
	f.choose("signalling_areas");
	const SceneBlockExtent extent = sceneBlockExtent(buildSceneSectionInventory(f.model()), "");
	ok &= expect(extent.found && extent.startKm == 0.0 && extent.endKm == 2.0, "the blocks of the scene reach from 0 to 2 km");

	f.panel.addButton()->click();
	const SceneSignallingArea& added = f.model().signallingAreas.back();
	ok &= expect(f.model().signallingAreas.size() == 3 && added.id == "signalling-area" && added.startKm == 0.0 && added.endKm == 2.0
			&& added.level == kSignallingLevelUnset && added.trackId.empty()
			&& f.changedOnce("Add signalling area", "signalling_areas") && f.panel.selectionId() == "signalling-area" && f.table()->currentRow() == 2,
		"an area is added over all the blocks, with a new id and no system, and selected");
	ok &= expect(f.combo(2, 3)->currentText() == "Choose a signalling system" && f.combo(2, 4)->currentText() == "(all tracks)",
		"the new area asks for a system and applies to all tracks");
	f.panel.addButton()->click();
	ok &= expect(f.model().signallingAreas.size() == 4 && f.model().signallingAreas[3].id == "signalling-area_2" && f.changedOnce("Add signalling area", "signalling_areas"),
		"another area gets the next free id");

	// nothing refers to an area, so the user is only asked
	f.select(0);
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.errorCount == 0 && f.prompts.confirmCount == 1 && f.prompts.confirmQuestion == "Delete signalling area 'area-1'?"
			&& f.model().signallingAreas.size() == 4 && f.untouched(),
		"nothing is deleted when the user answers no");
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	ok &= expect(f.model().signallingAreas.size() == 3 && f.model().signallingAreas[0].id == "area-2" && f.changedOnce("Delete signalling area", "signalling_areas")
			&& f.panel.selectionId() == "area-2",
		"an area is deleted when the user answers yes, and the next row is selected");
	return ok;
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;
	ok &= testFacets();
	ok &= testSignalList();
	ok &= testSignalEdit();
	ok &= testSignalRow();
	ok &= testSignalAddAndDelete();
	ok &= testAreaList();
	ok &= testAreaEdit();
	ok &= testAreaCombos();
	ok &= testAreaPlaceholders();
	ok &= testAreaWheel();
	ok &= testAreaAddAndDelete();
	return ok ? 0 : 1;
}
