#include "editors/InfrastructurePanel.h"
#include "editors/infrastructure/SignalFacets.h"

#include "scene/SectionInventory.h"
#include "scene/SignallingLevel.h"
#include "scene/SignallingLevelNames.h"

#include "support/FakePrompts.h"

#include <QApplication>
#include <QComboBox>
#include <QPoint>
#include <QPointF>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTest>
#include <QVariant>
#include <QWheelEvent>

#include <memory>
#include <string>
#include <utility>
#include <vector>

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
	return QString("%1 '%2' is used by:\n\xE2\x80\xA2 %3\nRemove or change these first.")
		.arg(QString(kind), QString(id), consumers.join("\n\xE2\x80\xA2 "));
}

class TestSignalFacets : public QObject {
	Q_OBJECT

private slots:
	void offersSignallingFacets();
	void listsSignals();
	void editsSignals();
	void ignoresComboOfRemovedRow();
	void addsAndDeletesSignals();
	void listsAreas();
	void editsAreas();
	void editsAreaCombos();
	void showsAreaPlaceholders();
	void wheelLeavesAreaCombo_data();
	void wheelLeavesAreaCombo();
	void addsAndDeletesAreas();
};

void TestSignalFacets::offersSignallingFacets() {
	const std::vector<std::unique_ptr<InfrastructureFacet>> facets = makeSignalFacets();
	QStringList keys;
	for (const std::unique_ptr<InfrastructureFacet>& facet : facets)
		keys << facet->key();
	// The signalling has two facets.
	QCOMPARE(keys, QStringList({"signals", "signalling_areas"}));

	Fixture f;
	// The panel has the facets of the signalling and no others.
	QVERIFY(f.panel.facet("signals"));
	QVERIFY(f.panel.facet("signalling_areas"));
	QVERIFY(!f.panel.facet("blocks"));
	QVERIFY(!f.panel.facet("stations"));
	f.choose("signals");
	f.select(1);
	f.prompts.confirmAnswer = true;
	f.document.setEditable(false);
	f.panel.refresh();
	f.type(0, 0, "signal-x");
	f.panel.addEntity();
	f.panel.deleteEntity();
	f.panel.commitCell(0, 0);
	f.panel.commitPendingEditors();
	// Nothing is edited, added or deleted while a run uses the scene.
	QVERIFY(f.untouched());
	QCOMPARE(f.prompts.confirmCount, 0);
	QCOMPARE(sceneSignals(f.model()).size(), std::size_t(2));
	QCOMPARE(sceneSignals(f.model())[0].id, "signal-1");
	QCOMPARE(sceneSignals(f.model())[1].id, "signal-2");
	QVERIFY2(!f.table()->isEnabled(), "the table cannot be used while a run uses the scene");
}

void TestSignalFacets::listsSignals() {
	Fixture f;
	f.choose("signals");
	// Signals are listed with their ids.
	QCOMPARE(f.headers(), QStringList({"ID", "Protected section"}));
	QCOMPARE(f.table()->rowCount(), 2);
	QCOMPARE(f.cell(0, 0), "signal-1");
	QCOMPARE(f.cell(1, 0), "signal-2");
	QComboBox* first = f.combo(0, 1);
	QComboBox* second = f.combo(1, 1);
	// The protected section is chosen in a combo with a name for each signal.
	if (first == nullptr) QFAIL("the protected section is chosen in a combo with a name for each signal");
	if (second == nullptr) QFAIL("the protected section is chosen in a combo with a name for each signal");
	QCOMPARE(first->objectName(), "protectedSectionCombo");
	QCOMPARE(first->accessibleName(), "Protected section for signal-1");
	QCOMPARE(second->accessibleName(), "Protected section for signal-2");
	// A signal shows the section that it protects, and none as unbound.
	QCOMPARE(first->currentData().toString().toStdString(), sectionOf(f.model(), "block-a1"));
	QCOMPARE(second->currentText(), "(unbound)");
	QVERIFY(second->currentData().toString().isEmpty());
	// the sections of the scene, one for each block
	// The combo offers none and the sections of the scene.
	QCOMPARE(first->count(), 4);
	QVERIFY(first->itemData(0).toString().isEmpty());
	// A section is named by its label, with its id as tooltip.
	QCOMPARE(first->currentText(), "track-a 0.000 to 1.000 km");
	QCOMPARE(first->itemData(first->currentIndex(), Qt::ToolTipRole).toString().toStdString(), sectionOf(f.model(), "block-a1"));
	// Listing signals edits nothing.
	QVERIFY(f.untouched());
	QVERIFY(!f.document.isDirty());
}

void TestSignalFacets::editsSignals() {
	Fixture f;
	f.choose("signals");

	// protected section
	QComboBox* second = f.combo(1, 1);
	f.chooseBlock(1, "block-a2");
	// A section is chosen, which marks the document once and leaves the combo in the table.
	QCOMPARE(sceneSignals(f.model())[1].protectedSection, sectionOf(f.model(), "block-a2"));
	QVERIFY(f.editedOnce("Edit signal"));
	QCOMPARE(f.combo(1, 1), second);
	f.choose(second, QString());
	// Choosing none unbinds the signal.
	QVERIFY(sceneSignals(f.model())[1].protectedSection.empty());
	QVERIFY(f.editedOnce("Edit signal"));
	// the combo shows none while the model already has the section that is chosen
	sceneSignals(f.edit())[1].protectedSection = sectionOf(f.model(), "block-a2");
	f.chooseBlock(1, "block-a2");
	QVERIFY2(f.untouched(), "choosing the section that the signal has changes nothing");

	// rename: only the signal-failure incidents that target the signal follow
	f.type(0, 0, " signal-x ");
	const SceneModel& model = f.model();
	// A renamed signal keeps its signal-failure incidents, and leaves a breakdown that has the old id as it is.
	QCOMPARE(sceneSignals(model)[0].id, "signal-x");
	QCOMPARE(sceneSignals(model)[1].id, "signal-2");
	QCOMPARE(model.scenarios[0].incidents[0].target, "signal-x");
	QCOMPARE(model.scenarios[0].incidents[1].target, "signal-1");
	// A renamed signal keeps its section.
	QCOMPARE(sceneSignals(model)[0].protectedSection, sectionOf(model, "block-a1"));
	// A rename marks the document once, and the renamed signal stays selected.
	QVERIFY(f.changedOnce("Rename signal", "signals"));
	QCOMPARE(f.panel.selectionId(), "signal-x");
	QCOMPARE(f.table()->currentRow(), 0);
	f.type(0, 0, " signal-x ");
	// The same id changes nothing.
	QVERIFY(f.untouched());
	QVERIFY(f.messages.empty());
	f.type(0, 0, "signal-2");
	// An id that another signal has is refused, and the table shows the model again.
	QVERIFY(f.untouched());
	QCOMPARE(sceneSignals(model)[0].id, "signal-x");
	QCOMPARE(f.cell(0, 0), "signal-x");
	QCOMPARE(f.messages, QStringList({"IDs must be unique and not empty"}));
	f.type(0, 0, "");
	// An empty id is refused.
	QVERIFY(f.untouched());
	QCOMPARE(sceneSignals(model)[0].id, "signal-x");
	QCOMPARE(f.messages.size(), qsizetype(2));
}

void TestSignalFacets::ignoresComboOfRemovedRow() {
	// A combo that belongs to a row the model no longer has does nothing.
	Fixture f;
	f.choose("signals");
	QComboBox* second = f.combo(1, 1);
	sceneSignals(f.edit()).pop_back();
	f.chooseBlock(1, "block-a1");
	// A combo of a row that is gone changes nothing.
	QVERIFY(second);
	QVERIFY(f.untouched());
	QCOMPARE(sceneSignals(f.model()).size(), std::size_t(1));
	QCOMPARE(sceneSignals(f.model())[0].protectedSection, sectionOf(f.model(), "block-a1"));
}

void TestSignalFacets::addsAndDeletesSignals() {
	Fixture f;
	f.choose("signals");
	f.panel.addButton()->click();
	// A signal is added unbound with a new id, and selected.
	QCOMPARE(sceneSignals(f.model()).size(), std::size_t(3));
	QCOMPARE(sceneSignals(f.model())[2].id, "signal");
	QVERIFY(sceneSignals(f.model())[2].protectedSection.empty());
	QVERIFY(f.changedOnce("Add signal", "signals"));
	QCOMPARE(f.panel.selectionId(), "signal");
	QCOMPARE(f.table()->currentRow(), 2);
	// The new signal is listed unbound.
	QCOMPARE(f.combo(2, 1)->currentText(), "(unbound)");
	f.panel.addButton()->click();
	// Another signal gets the next free id.
	QCOMPARE(sceneSignals(f.model()).size(), std::size_t(4));
	QCOMPARE(sceneSignals(f.model())[3].id, "signal_2");
	QVERIFY(f.changedOnce("Add signal", "signals"));

	// delete
	f.select(0);
	f.panel.deleteButton()->click();
	// A signal that an incident targets cannot be deleted.
	QCOMPARE(f.prompts.errorCount, 1);
	QCOMPARE(f.prompts.confirmCount, 0);
	QCOMPARE(sceneSignals(f.model()).size(), std::size_t(4));
	QVERIFY(f.untouched());
	// The message names the signal-failure incident that targets the signal.
	QCOMPARE(f.prompts.errorMessage, blocked("Signal", "signal-1", {"signal-failure incident 'incident-1' in scenario 'scenario-1'"}));
	f.select(1);
	f.panel.deleteButton()->click();
	// Nothing is deleted when the user answers no.
	QCOMPARE(f.prompts.confirmCount, 1);
	QCOMPARE(f.prompts.confirmQuestion, "Delete signal 'signal-2'?");
	QCOMPARE(sceneSignals(f.model()).size(), std::size_t(4));
	QVERIFY(f.untouched());
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	// A signal that no incident targets is deleted when the user answers yes, and the next row is selected.
	QCOMPARE(sceneSignals(f.model()).size(), std::size_t(3));
	QCOMPARE(sceneSignals(f.model())[1].id, "signal");
	QVERIFY(f.changedOnce("Delete signal", "signals"));
	QCOMPARE(f.panel.selectionId(), "signal");
}

void TestSignalFacets::listsAreas() {
	Fixture f;
	f.choose("signalling_areas");
	// Signalling areas are listed with their range, system and track.
	QCOMPARE(f.headers(), QStringList({"ID", "Start km", "End km", "Signalling system", "Track"}));
	QCOMPARE(f.table()->rowCount(), 2);
	// Each row has its id, start and end.
	QCOMPARE(f.cell(0, 0), "area-1");
	QCOMPARE(f.cell(0, 1), "0");
	QCOMPARE(f.cell(0, 2), "1");
	QCOMPARE(f.cell(1, 0), "area-2");
	QCOMPARE(f.cell(1, 1), "1");
	QCOMPARE(f.cell(1, 2), "2");
	QComboBox* system = f.combo(0, 3);
	QComboBox* track = f.combo(0, 4);
	// The system and the track are chosen in combos with a name for each area.
	if (system == nullptr) QFAIL("the system and the track are chosen in combos with a name for each area");
	if (track == nullptr) QFAIL("the system and the track are chosen in combos with a name for each area");
	QCOMPARE(system->objectName(), "signallingAreaSystemCombo");
	QCOMPARE(track->objectName(), "signallingAreaTrackCombo");
	QCOMPARE(system->accessibleName(), "Signalling system for area-1");
	QCOMPARE(track->accessibleName(), "Track for area-1");
	// A system combo offers the six signalling systems and shows the one of the area.
	QCOMPARE(system->count(), 6);
	QCOMPARE(system->currentData().toInt(), 0);
	QCOMPARE(f.combo(1, 3)->currentData().toInt(), 2);
	// A track combo offers all tracks and each track, and shows the track of the area.
	QCOMPARE(track->count(), 3);
	QVERIFY(track->itemData(0).toString().isEmpty());
	QCOMPARE(track->currentData().toString(), "track-a");
	QVERIFY(f.combo(1, 4)->currentData().toString().isEmpty());
	QCOMPARE(f.combo(1, 4)->currentText(), "(all tracks)");
	// A track is shown with the extent of its blocks.
	QCOMPARE(track->itemText(1), "track-a (blocks 0.000000 to 2.000000 km)");
	QCOMPARE(track->itemText(2), "track-b (blocks 0.000000 to 0.500000 km)");
	// Listing signalling areas edits nothing.
	QVERIFY(f.untouched());
	QVERIFY(!f.document.isDirty());
}

void TestSignalFacets::editsAreas() {
	Fixture f;
	f.choose("signalling_areas");

	// range
	f.type(0, 1, "0.25");
	// The start of an area is edited.
	QCOMPARE_EQ(f.model().signallingAreas[0].startKm, 0.25);
	QVERIFY(f.changedOnce("Edit signalling area", "signalling_areas"));
	f.type(0, 2, " 1.75 ");
	// The end of an area is edited.
	QCOMPARE_EQ(f.model().signallingAreas[0].endKm, 1.75);
	QVERIFY(f.changedOnce("Edit signalling area", "signalling_areas"));
	f.type(0, 1, " 0.25 ");
	QVERIFY2(f.untouched(), "a value that is the same changes nothing");
	f.type(0, 2, "far");
	// An end that is no number is refused, without a message, and the table shows the model again.
	QVERIFY(f.untouched());
	QCOMPARE_EQ(f.model().signallingAreas[0].endKm, 1.75);
	QCOMPARE(f.cell(0, 2), "1.75");
	QVERIFY(f.messages.empty());

	// rename: nothing refers to an area
	f.type(1, 0, " area-x ");
	// A rename marks the document once, and the renamed area stays selected.
	QCOMPARE(f.model().signallingAreas[1].id, "area-x");
	QCOMPARE(f.model().signallingAreas[1].level, 2);
	QVERIFY(f.changedOnce("Rename signalling area", "signalling_areas"));
	QCOMPARE(f.panel.selectionId(), "area-x");
	QCOMPARE(f.table()->currentRow(), 1);
	f.type(1, 0, "area-1");
	// An id that another area has is refused.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().signallingAreas[1].id, "area-x");
	QCOMPARE(f.messages, QStringList({"IDs must be unique and not empty"}));
	f.type(1, 0, "");
	// An empty id is refused.
	QVERIFY(f.untouched());
	QCOMPARE(f.messages.size(), qsizetype(2));
}

void TestSignalFacets::editsAreaCombos() {
	Fixture f;
	f.choose("signalling_areas");

	// system
	QComboBox* system = f.combo(0, 3);
	f.choose(system, 4);
	// Another system is chosen, which marks the document once and leaves the combo in the table.
	QCOMPARE(f.model().signallingAreas[0].level, 4);
	QVERIFY(f.editedOnce("Edit signalling area"));
	QCOMPARE(f.combo(0, 3), system);
	// the combo shows another system than the model has
	f.edit().signallingAreas[0].level = 5;
	f.choose(system, 5);
	QVERIFY2(f.untouched(), "choosing the system that the area has changes nothing");

	// track
	QComboBox* track = f.combo(0, 4);
	f.choose(track, "track-b");
	// Another track is chosen, which marks the document once and leaves the combo in the table.
	QCOMPARE(f.model().signallingAreas[0].trackId, "track-b");
	QVERIFY(f.editedOnce("Edit signalling area"));
	QCOMPARE(f.combo(0, 4), track);
	f.choose(track, QString());
	// All tracks can be chosen.
	QVERIFY(f.model().signallingAreas[0].trackId.empty());
	QVERIFY(f.editedOnce("Edit signalling area"));
	// the combo shows another track than the model has
	f.edit().signallingAreas[0].trackId = "track-b";
	f.choose(track, "track-b");
	QVERIFY2(f.untouched(), "choosing the track that the area has changes nothing");

	// a combo of a row that is gone does nothing
	QComboBox* last = f.combo(1, 3);
	f.edit().signallingAreas.pop_back();
	f.choose(last, 5);
	// A combo of a row that is gone changes nothing.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().signallingAreas.size(), std::size_t(1));
}

void TestSignalFacets::showsAreaPlaceholders() {
	SceneModel scene = signalScene();
	scene.signallingAreas = {area("unchosen", 0.0, 1.0, kSignallingLevelUnset, "track-a"), area("odd", 0.0, 1.0, 9, "nowhere")};
	Fixture f(scene);
	f.choose("signalling_areas");

	// an area without a system shows an item that stands for it, until a system is chosen
	QComboBox* unchosen = f.combo(0, 3);
	// An area without a system shows an item that asks for one.
	QCOMPARE(unchosen->count(), 7);
	QCOMPARE(unchosen->currentText(), "Choose a signalling system");
	QCOMPARE(unchosen->currentData().toInt(), kSignallingLevelUnset);
	f.choose(unchosen, 1);
	// Choosing a system takes the item away.
	QCOMPARE(f.model().signallingAreas[0].level, 1);
	QCOMPARE(unchosen->count(), 6);
	QCOMPARE(unchosen->currentData().toInt(), 1);
	QVERIFY(f.editedOnce("Edit signalling area"));

	// a level that is no system is shown as it is stored
	QComboBox* odd = f.combo(1, 3);
	// A level that is no system is shown as stored.
	QCOMPARE(odd->count(), 7);
	QCOMPARE(odd->currentData().toInt(), 9);
	QCOMPARE(odd->currentText(), QString::fromStdString(signallingLevelName(9)));
	f.choose(odd, 3);
	// Choosing a system takes that item away.
	QCOMPARE(f.model().signallingAreas[1].level, 3);
	QCOMPARE(odd->count(), 6);
	QVERIFY(f.editedOnce("Edit signalling area"));

	// a track that the scene does not have is shown on top, until a track is chosen
	QComboBox* nowhere = f.combo(1, 4);
	// A track that the scene does not have is shown as invalid.
	QCOMPARE(nowhere->count(), 4);
	QCOMPARE(nowhere->itemText(0), "nowhere (missing)");
	QCOMPARE(nowhere->currentData().toString(), "nowhere");
	f.choose(nowhere, "track-a");
	// Choosing a track takes the invalid item away.
	QCOMPARE(f.model().signallingAreas[1].trackId, "track-a");
	QCOMPARE(nowhere->count(), 3);
	QCOMPARE(nowhere->currentData().toString(), "track-a");
	QVERIFY(f.editedOnce("Edit signalling area"));
}

void TestSignalFacets::wheelLeavesAreaCombo_data() {
	QTest::addColumn<int>("column");
	QTest::newRow("system") << 3;
	QTest::newRow("track") << 4;
}

// The wheel scrolls the table behind a combo and does not change what the combo has chosen.
void TestSignalFacets::wheelLeavesAreaCombo() {
	QFETCH(int, column);
	Fixture f;
	f.choose("signalling_areas");
	QComboBox* box = f.combo(1, column);
	if (box == nullptr) QFAIL("the column of the row has a combo");
	const int index = box->currentIndex();
	QWheelEvent wheel(QPointF(2, 2), box->mapToGlobal(QPoint(2, 2)), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QApplication::sendEvent(box, &wheel);
	// The wheel does not change a combo of an area.
	QVERIFY(!wheel.isAccepted());
	QCOMPARE(box->currentIndex(), index);
	QVERIFY(f.untouched());
}

void TestSignalFacets::addsAndDeletesAreas() {
	Fixture f;
	f.choose("signalling_areas");
	const SceneBlockExtent extent = sceneBlockExtent(buildSceneSectionInventory(f.model()), "");
	// The blocks of the scene reach from 0 to 2 km.
	QVERIFY(extent.found);
	QCOMPARE_EQ(extent.startKm, 0.0);
	QCOMPARE_EQ(extent.endKm, 2.0);

	f.panel.addButton()->click();
	const SceneSignallingArea& added = f.model().signallingAreas.back();
	// An area is added over all the blocks, with a new id and no system, and selected.
	QCOMPARE(f.model().signallingAreas.size(), std::size_t(3));
	QCOMPARE(added.id, "signalling-area");
	QCOMPARE_EQ(added.startKm, 0.0);
	QCOMPARE_EQ(added.endKm, 2.0);
	QCOMPARE(added.level, kSignallingLevelUnset);
	QVERIFY(added.trackId.empty());
	QVERIFY(f.changedOnce("Add signalling area", "signalling_areas"));
	QCOMPARE(f.panel.selectionId(), "signalling-area");
	QCOMPARE(f.table()->currentRow(), 2);
	// The new area asks for a system and applies to all tracks.
	QCOMPARE(f.combo(2, 3)->currentText(), "Choose a signalling system");
	QCOMPARE(f.combo(2, 4)->currentText(), "(all tracks)");
	f.panel.addButton()->click();
	// Another area gets the next free id.
	QCOMPARE(f.model().signallingAreas.size(), std::size_t(4));
	QCOMPARE(f.model().signallingAreas[3].id, "signalling-area_2");
	QVERIFY(f.changedOnce("Add signalling area", "signalling_areas"));

	// nothing refers to an area, so the user is only asked
	f.select(0);
	f.panel.deleteButton()->click();
	// Nothing is deleted when the user answers no.
	QCOMPARE(f.prompts.errorCount, 0);
	QCOMPARE(f.prompts.confirmCount, 1);
	QCOMPARE(f.prompts.confirmQuestion, "Delete signalling area 'area-1'?");
	QCOMPARE(f.model().signallingAreas.size(), std::size_t(4));
	QVERIFY(f.untouched());
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	// An area is deleted when the user answers yes, and the next row is selected.
	QCOMPARE(f.model().signallingAreas.size(), std::size_t(3));
	QCOMPARE(f.model().signallingAreas[0].id, "area-2");
	QVERIFY(f.changedOnce("Delete signalling area", "signalling_areas"));
	QCOMPARE(f.panel.selectionId(), "area-2");
}

QTEST_MAIN(TestSignalFacets)
#include "test_editor_infrastructure_signals.moc"
