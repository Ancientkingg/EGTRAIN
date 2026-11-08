#include "editors/InfrastructurePanel.h"
#include "editors/infrastructure/TopologyFacets.h"

#include "scene/SectionInventory.h"

#include "scene/RuntimeLimits.h"
#include "scene/SceneValidator.h"
#include "support/EditHistory.h"
#include "support/FakePrompts.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCompleter>
#include <QLineEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTest>

#include <cmath>
#include <cstddef>
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

static SceneBlock block(const char* id, const char* trackId) {
	SceneBlock result;
	result.id = id;
	result.trackId = trackId;
	result.lengthKm = 1.0;
	return result;
}

// Two tracks of three nodes, two arcs and two blocks each, a connection between the second node of the first track
// and the last of the second, a signalling area on the first track, and a station with a platform at the second node of
// the first track. Every id has a name of its own, so a test can tell which reference a rewrite reached.
static SceneModel topologyScene() {
	SceneModel scene;
	scene.name = "Topology";
	scene.tracks = {{"track-a"}, {"track-b"}};
	scene.nodes = {node("node-a0", "track-a", 0.0), node("node-a1", "track-a", 1.0), node("node-a2", "track-a", 2.0),
		node("node-b0", "track-b", 0.0), node("node-b1", "track-b", 1.0), node("node-b2", "track-b", 2.0)};
	scene.arcs = {arc("arc-a1", "track-a", "node-a0", "node-a1"), arc("arc-a2", "track-a", "node-a1", "node-a2"),
		arc("arc-b1", "track-b", "node-b0", "node-b1"), arc("arc-b2", "track-b", "node-b1", "node-b2")};
	scene.blocks = {block("block-a1", "track-a"), block("block-a2", "track-a"), block("block-b1", "track-b"),
		block("block-b2", "track-b")};
	SceneConnection connection;
	connection.id = "connection-1";
	connection.fromNodeId = "node-a1";
	connection.toNodeId = "node-b2";
	scene.connections.push_back(connection);
	SceneSignallingArea area;
	area.id = "area-1";
	area.trackId = "track-a";
	scene.signallingAreas.push_back(area);
	SceneStation station;
	station.id = "station-1";
	ScenePlatform platform;
	platform.id = "platform-1";
	platform.nodeIds = {"node-a1"};
	station.platforms.push_back(platform);
	scene.stations.push_back(station);
	return scene;
}

using Log = std::vector<std::string>;

// A panel with the facets of the network, over a document that holds the scene. The events are the order in which the
// panel and its document report an edit, and the messages that the panel shows in the status line.
struct Fixture {
	SceneDocument document;
	InfrastructurePanel panel;
	FakePrompts prompts;
	Log log;
	SceneDocument::ChangeKinds kinds;
	QStringList messages;

	explicit Fixture(SceneModel scene = topologyScene()) {
		panel.setPrompts(&prompts);
		for (std::unique_ptr<InfrastructureFacet>& facet : makeTopologyFacets())
			panel.setFacet(std::move(facet));
		panel.setDocument(&document);
		document.replace(std::move(scene), false);
		panel.refresh();
		QObject::connect(&document, &SceneDocument::changed, &panel, [this](SceneDocument::ChangeKinds changedKinds) {
			log.push_back("documentChanged");
			kinds = changedKinds;
		});
		QObject::connect(&panel, &InfrastructurePanel::changed, &panel, [this] { log.push_back("panelChanged"); });
		QObject::connect(&panel, &InfrastructurePanel::statusMessage, &panel,
			[this](const QString& text, int) { messages << text; });
	}

	void choose(const char* key) { panel.facetCombo()->setCurrentIndex(panel.facetCombo()->findData(key)); }
	QTableWidget* table() { return panel.table(); }
	const SceneModel& model() const { return document.model(); }
	// Selects the row and lists the entities again, so that the buttons follow the selected row.
	void select(int row) {
		table()->setCurrentCell(row, 0);
		panel.refresh();
	}
	void type(int row, int column, const char* text) { table()->item(row, column)->setText(text); }
	QString cell(int row, int column) { return table()->item(row, column)->text(); }
	QStringList rowTexts(int row) {
		QStringList result;
		for (int column = 0; column < table()->columnCount(); ++column)
			result << cell(row, column);
		return result;
	}
	QStringList firstColumn() {
		QStringList result;
		for (int row = 0; row < table()->rowCount(); ++row)
			result << cell(row, 0);
		return result;
	}
	QStringList headers() {
		QStringList result;
		for (int column = 0; column < table()->columnCount(); ++column)
			result << table()->horizontalHeaderItem(column)->text();
		return result;
	}
	// True when the document has been changed once, under this label, and the panel has said so.
	bool changedOnce(const char* label) {
		const bool once = log == Log({"documentChanged", "panelChanged"}) && kinds == SceneDocument::Infrastructure
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
	// Opens the editor of the cell, as a click on it does, and returns it when it is a combo.
	QComboBox* openCombo(int row, int column) {
		table()->setCurrentCell(row, column);
		table()->editItem(table()->item(row, column));
		return qobject_cast<QComboBox*>(table()->indexWidget(table()->model()->index(row, column)));
	}
	// True when no editor is open on the cell.
	bool closed(int row, int column) { return table()->indexWidget(table()->model()->index(row, column)) == nullptr; }
};

// The labels of the items of the combo.
static QStringList labels(const QComboBox* combo) {
	QStringList result;
	for (int index = 0; index < combo->count(); ++index)
		result << combo->itemText(index);
	return result;
}

// Chooses the item of the combo as a click in its list does, and lets the queued end of the edit run.
static void choose(QComboBox* combo, int index) {
	combo->setCurrentIndex(index);
	emit combo->activated(index);
	QApplication::processEvents();
}

static QString blocked(const char* kind, const char* id, const QStringList& consumers) {
	return QString("%1 '%2' is used by:\n\xE2\x80\xA2 %3\nRemove or change these first.")
		.arg(QString(kind), QString(id), consumers.join("\n\xE2\x80\xA2 "));
}

class TestTopologyFacets : public QObject {
	Q_OBJECT

private slots:
	void offersNetworkFacets();
	void editsTracks();
	void editsNodes();
	void editsArcs();
	void showsShortestNumbers();
	void editsConnections();
	void deletesConnectionThatRouteUses();
	void namesStationOfNodes();
	void editsReferencesInCombos();
	void leavesComboWithArrowKeys();
	void addsStationAtNode();
	void undoRemovesAddedStation();
	void refusesStationAtStationNode();
	void refusesTakenOrBadStationName_data();
	void refusesTakenOrBadStationName();
	void refusesStationBeyondLimit();
	void cancelAddsNoStation();
	void taskButtonFollowsSelection();
	void reversesFallingTrack();
	void undoRestoresReversedTrack();
	void refusesBrokenTrack();
	void reverseWithoutRoutesShowsNoBox();
};

void TestTopologyFacets::offersNetworkFacets() {
	const std::vector<std::unique_ptr<InfrastructureFacet>> facets = makeTopologyFacets();
	QStringList keys;
	for (const std::unique_ptr<InfrastructureFacet>& facet : facets)
		keys << facet->key();
	// The network has four facets.
	QCOMPARE(keys, QStringList({"tracks", "nodes", "arcs", "connections"}));

	Fixture f;
	// The panel has the facets of the network and no others.
	QVERIFY(f.panel.facet("tracks"));
	QVERIFY(f.panel.facet("nodes"));
	QVERIFY(f.panel.facet("arcs"));
	QVERIFY(f.panel.facet("connections"));
	QVERIFY(!f.panel.facet("blocks"));
	f.choose("blocks");
	// A facet that has no class lists nothing.
	QCOMPARE(f.table()->columnCount(), 0);
	QCOMPARE(f.table()->rowCount(), 0);
	f.choose("arcs");
	f.panel.commitCell(99, 0);
	f.panel.commitCell(0, 99);
	QVERIFY2(f.untouched(), "a cell that is not in the table changes nothing");
	f.document.setEditable(false);
	f.panel.refresh();
	f.panel.addEntity();
	f.type(0, 4, "5");
	// Nothing is edited or added while a run uses the scene.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().arcs.size(), std::size_t(4));
	QCOMPARE_EQ(f.model().arcs[0].curvatureRadiusM, 0.0);
}

void TestTopologyFacets::editsTracks() {
	Fixture f;
	f.choose("tracks");
	// Tracks are listed by their id.
	QCOMPARE(f.headers(), QStringList({"ID"}));
	QCOMPARE(f.firstColumn(), QStringList({"track-a", "track-b"}));
	// Listing tracks edits nothing.
	QVERIFY(f.untouched());
	QVERIFY(!f.document.isDirty());

	// renaming a track rewrites what sits on it
	f.type(1, 0, " track-c ");
	const SceneModel& model = f.model();
	// A renamed track keeps its nodes, arcs and blocks.
	QCOMPARE(model.tracks[1].id, "track-c");
	QCOMPARE(model.nodes[3].trackId, "track-c");
	QCOMPARE(model.nodes[4].trackId, "track-c");
	QCOMPARE(model.nodes[5].trackId, "track-c");
	QCOMPARE(model.arcs[2].trackId, "track-c");
	QCOMPARE(model.arcs[3].trackId, "track-c");
	QCOMPARE(model.blocks[2].trackId, "track-c");
	QCOMPARE(model.blocks[3].trackId, "track-c");
	// A rename leaves the other tracks as they are.
	QCOMPARE(model.nodes[0].trackId, "track-a");
	QCOMPARE(model.arcs[0].trackId, "track-a");
	QCOMPARE(model.blocks[0].trackId, "track-a");
	QCOMPARE(model.signallingAreas[0].trackId, "track-a");
	// A rename marks the document once, and the renamed track stays selected.
	QVERIFY(f.changedOnce("Rename track"));
	QCOMPARE(f.panel.selectionId(), "track-c");
	QCOMPARE(f.cell(1, 0), "track-c");
	QCOMPARE(f.table()->currentRow(), 1);
	f.type(0, 0, " track-a ");
	// An id that is the same after trimming changes nothing.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().tracks[0].id, "track-a");

	// ids that a track cannot have
	f.type(0, 0, "track-c");
	// An id that another track has is refused, with a message, and the table shows the model again.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().tracks[0].id, "track-a");
	QCOMPARE(f.cell(0, 0), "track-a");
	QCOMPARE(f.messages, QStringList({"IDs must be unique and not empty"}));
	f.type(0, 0, "  ");
	// An empty id is refused.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().tracks[0].id, "track-a");
	QCOMPARE(f.cell(0, 0), "track-a");
	QCOMPARE(f.messages.size(), qsizetype(2));

	// add
	f.panel.addButton()->click();
	// A track is added with a new id, and selected.
	QCOMPARE(f.model().tracks.size(), std::size_t(3));
	QCOMPARE(f.model().tracks[2].id, "track");
	QVERIFY(f.changedOnce("Add track"));
	QCOMPARE(f.panel.selectionId(), "track");
	QCOMPARE(f.table()->currentRow(), 2);
	QCOMPARE(f.cell(2, 0), "track");
	f.panel.addButton()->click();
	// The id of the next track is new too.
	QCOMPARE(f.model().tracks.size(), std::size_t(4));
	QCOMPARE(f.model().tracks[3].id, "track_2");
	QVERIFY(f.changedOnce("Add track"));

	// delete
	f.select(0);
	f.panel.deleteButton()->click();
	// A track that others use cannot be deleted.
	QCOMPARE(f.prompts.errorCount, 1);
	QCOMPARE(f.prompts.confirmCount, 0);
	QCOMPARE(f.model().tracks.size(), std::size_t(4));
	QVERIFY(f.untouched());
	// The message names the nodes, arcs, blocks and signalling areas that use the track.
	QCOMPARE(f.prompts.errorMessage,
		blocked("Track", "track-a",
			{"node 'node-a0'", "node 'node-a1'", "node 'node-a2'", "arc 'arc-a1'", "arc 'arc-a2'", "block 'block-a1'",
				"block 'block-a2'", "signalling area 'area-1'"}));
	f.select(2);
	f.messages.clear();
	f.panel.deleteButton()->click();
	// A track that nothing uses is deleted without a question, the row at its place is selected, and the status line
	// says that Undo restores the track.
	QCOMPARE(f.prompts.confirmCount, 0);
	QCOMPARE(f.messages, QStringList({"Deleted track 'track'. Undo restores it."}));
	QCOMPARE(f.model().tracks.size(), std::size_t(3));
	QCOMPARE(f.model().tracks[2].id, "track_2");
	QVERIFY(f.changedOnce("Delete track"));
	QCOMPARE(f.panel.selectionId(), "track_2");
	QCOMPARE(f.table()->currentRow(), 2);
}

void TestTopologyFacets::editsNodes() {
	Fixture f;
	f.choose("nodes");
	// Nodes are listed with their track and position.
	QCOMPARE(f.headers(), QStringList({"ID", "Station", "Track ID", "X (km)", "Y (km)"}));
	QCOMPARE(f.table()->rowCount(), 6);
	QCOMPARE(f.rowTexts(1), QStringList({"node-a1", "station-1", "track-a", "1", "0"}));
	// Listing nodes edits nothing.
	QVERIFY(f.untouched());
	QVERIFY(!f.document.isDirty());

	// edit
	f.type(1, 3, "2.5");
	// A position is edited.
	QCOMPARE_EQ(f.model().nodes[1].xKm, 2.5);
	QCOMPARE(f.cell(1, 3), "2.5");
	QVERIFY(f.changedOnce("Edit node"));
	f.type(1, 4, "-0.125");
	// The other coordinate is edited.
	QCOMPARE_EQ(f.model().nodes[1].yKm, -0.125);
	QVERIFY(f.changedOnce("Edit node"));
	f.type(1, 2, "track-b");
	// The track of a node is edited.
	QCOMPARE(f.model().nodes[1].trackId, "track-b");
	QVERIFY(f.changedOnce("Edit node"));
	for (const char* refused : {"abc", "", "inf", "nan"}) {
		f.type(1, 3, refused);
		// A position that is no finite number is refused, without a message, and the table shows the model again.
		QVERIFY2(f.untouched(), refused);
		QCOMPARE_EQ(f.model().nodes[1].xKm, 2.5);
		QCOMPARE(f.cell(1, 3), "2.5");
		QVERIFY2(f.messages.empty(), refused);
	}
	f.type(1, 3, " 2.5 ");
	QVERIFY2(f.untouched(), "a position that is the same number changes nothing");

	// rename
	f.type(1, 0, "node-x");
	const SceneModel& model = f.model();
	// A renamed node keeps its arcs, connections and platforms.
	QCOMPARE(model.nodes[1].id, "node-x");
	QCOMPARE(model.arcs[0].toNodeId, "node-x");
	QCOMPARE(model.arcs[1].fromNodeId, "node-x");
	QCOMPARE(model.connections[0].fromNodeId, "node-x");
	QCOMPARE(model.stations[0].platforms[0].nodeIds[0], "node-x");
	// A rename leaves the other ends as they are.
	QCOMPARE(model.arcs[0].fromNodeId, "node-a0");
	QCOMPARE(model.arcs[1].toNodeId, "node-a2");
	QCOMPARE(model.connections[0].toNodeId, "node-b2");
	// A rename marks the document once, and the renamed node stays selected.
	QVERIFY(f.changedOnce("Rename node"));
	QCOMPARE(f.panel.selectionId(), "node-x");
	QCOMPARE(f.table()->currentRow(), 1);
	f.type(0, 0, "node-x");
	// An id that another node has is refused.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().nodes[0].id, "node-a0");
	QCOMPARE(f.messages, QStringList({"IDs must be unique and not empty"}));

	// add
	f.panel.addButton()->click();
	const SceneNode& added = f.model().nodes.back();
	// A node is added on the first track, one kilometre after its last node, and selected.
	QCOMPARE(f.model().nodes.size(), std::size_t(7));
	QCOMPARE(added.id, "node");
	QCOMPARE(added.trackId, "track-a");
	QCOMPARE_EQ(added.xKm, 3.0);
	QCOMPARE_EQ(added.yKm, 0.0);
	QVERIFY(f.changedOnce("Add node"));
	QCOMPARE(f.panel.selectionId(), "node");
	QCOMPARE(f.table()->currentRow(), 6);
	QCOMPARE(f.rowTexts(6), QStringList({"node", "", "track-a", "3", "0"}));

	// delete
	f.select(1);
	f.panel.deleteButton()->click();
	// A node that others use cannot be deleted.
	QCOMPARE(f.prompts.errorCount, 1);
	QCOMPARE(f.prompts.confirmCount, 0);
	QCOMPARE(f.model().nodes.size(), std::size_t(7));
	QVERIFY(f.untouched());
	// The message names the arcs, connections and platforms that use the node.
	QCOMPARE(f.prompts.errorMessage,
		blocked("Node", "node-x",
			{"arc 'arc-a1' endpoint", "arc 'arc-a2' endpoint", "connection 'connection-1' endpoint",
				"platform 'platform-1' at station 'station-1'"}));
	f.select(6);
	f.messages.clear();
	f.panel.deleteButton()->click();
	// A node that nothing uses is deleted at once.
	QCOMPARE(f.messages, QStringList({"Deleted node 'node'. Undo restores it."}));
	QCOMPARE(f.model().nodes.size(), std::size_t(6));
	QVERIFY(f.changedOnce("Delete node"));
	QCOMPARE(f.panel.selectionId(), "node-b2");
}

void TestTopologyFacets::editsArcs() {
	Fixture f;
	f.choose("arcs");
	// Arcs are listed with their nodes and limits.
	QCOMPARE(f.headers(),
		QStringList({"ID", "Track ID", "From node", "To node", "Curvature radius (m)", "Gradient (rise/length)", "Speed limit (m/s)"}));
	QCOMPARE(f.table()->rowCount(), 4);
	QCOMPARE(f.rowTexts(0), QStringList({"arc-a1", "track-a", "node-a0", "node-a1", "0", "0", "20"}));
	// Listing arcs edits nothing.
	QVERIFY(f.untouched());
	QVERIFY(!f.document.isDirty());

	// edit
	f.type(0, 2, "node-a2");
	f.type(0, 3, "node-a0");
	// The ends of an arc are edited.
	QCOMPARE(f.model().arcs[0].fromNodeId, "node-a2");
	QCOMPARE(f.model().arcs[0].toNodeId, "node-a0");
	QCOMPARE(f.cell(0, 3), "node-a0");
	f.log.clear();
	f.type(0, 1, "track-b");
	// The track of an arc is edited.
	QCOMPARE(f.model().arcs[0].trackId, "track-b");
	QVERIFY(f.changedOnce("Edit arc"));
	f.type(0, 4, "150.5");
	f.type(0, 5, "-0.01");
	f.type(0, 6, "30");
	// The curvature, the gradient and the speed limit are edited.
	QCOMPARE_EQ(f.model().arcs[0].curvatureRadiusM, 150.5);
	QCOMPARE_EQ(f.model().arcs[0].gradientPercent, -0.01);
	QCOMPARE_EQ(f.model().arcs[0].speedLimitMs, 30.0);
	f.log.clear();
	f.type(0, 6, "fast");
	// A limit that is no number is refused and the table shows the model again.
	QVERIFY(f.untouched());
	QCOMPARE_EQ(f.model().arcs[0].speedLimitMs, 30.0);
	QCOMPARE(f.cell(0, 6), "30");
	QVERIFY(f.messages.empty());

	// rename: nothing refers to an arc
	const std::vector<SceneArc> before = f.model().arcs;
	f.type(1, 0, "arc-x");
	// An arc is renamed, and the other arcs are as they were.
	QCOMPARE(f.model().arcs[1].id, "arc-x");
	QCOMPARE(f.model().arcs[0].id, "arc-a1");
	QCOMPARE(f.model().arcs[2].id, "arc-b1");
	QCOMPARE(f.model().arcs[1].trackId, before[1].trackId);
	QVERIFY(f.changedOnce("Rename arc"));
	QCOMPARE(f.panel.selectionId(), "arc-x");
	f.type(2, 0, "arc-x");
	// An id that another arc has is refused.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().arcs[2].id, "arc-b1");
	QCOMPARE(f.messages, QStringList({"IDs must be unique and not empty"}));

	// add: after the arcs that the first track has, between its next two nodes
	f.document.mutableModel().arcs.erase(f.document.mutableModel().arcs.begin() + 1);
	f.document.mutableModel().arcs[0].trackId = "track-a";
	f.panel.refresh();
	f.panel.addButton()->click();
	const SceneArc& added = f.model().arcs.back();
	// An arc is added on the first track from the node after the end of its arcs, and selected.
	QCOMPARE(added.id, "arc");
	QCOMPARE(added.trackId, "track-a");
	QCOMPARE(added.fromNodeId, "node-a1");
	QCOMPARE(added.toNodeId, "node-a2");
	QCOMPARE_EQ(added.curvatureRadiusM, 0.0);
	QCOMPARE_EQ(added.gradientPercent, 0.0);
	QCOMPARE_EQ(added.speedLimitMs, 20.0);
	QVERIFY(f.changedOnce("Add arc"));
	QCOMPARE(f.panel.selectionId(), "arc");
	QCOMPARE(f.table()->currentRow(), f.table()->rowCount() - 1);

	// delete: nothing refers to an arc
	f.messages.clear();
	f.panel.deleteButton()->click();
	// An arc is deleted at once, as nothing uses it.
	QCOMPARE(f.prompts.errorCount, 0);
	QCOMPARE(f.messages, QStringList({"Deleted arc 'arc'. Undo restores it."}));
	QCOMPARE(f.model().arcs.size(), std::size_t(3));
	QVERIFY(f.changedOnce("Delete arc"));
}

void TestTopologyFacets::showsShortestNumbers() {
	SceneModel scene = topologyScene();
	scene.arcs[0].speedLimitMs = 8.333333333;
	Fixture f(std::move(scene));
	f.choose("arcs");
	// A number has the fewest digits that read back as it, such as the speed limit of 30 km/h of Paimpol.
	QCOMPARE(f.cell(0, 6), "8.333333333");

	const std::vector<std::pair<double, QString>> numbers = {{0.1, "0.1"}, {1.0 / 3.0, "0.3333333333333333"},
		{8.333333333, "8.333333333"}, {1e-7, "1e-07"}, {123456789.123, "123456789.123"}, {-0.012, "-0.012"}, {0.0, "0"},
		{-5.0, "-5"}, {10000.0, "10000"}, {100000.0, "100000"}, {1e20, "1e+20"}};
	for (const auto& [number, text] : numbers) {
		f.document.mutableModel().arcs[0].gradientPercent = number;
		f.panel.refresh();
		// The cell shows the shortest text of the number, which reads back as the same number.
		QCOMPARE(f.cell(0, 5), text);
		QCOMPARE_EQ(f.cell(0, 5).toDouble(), number);
		f.panel.commitCell(0, 5);
		// A cell that is left as it is edits nothing.
		QVERIFY2(f.untouched(), qPrintable(text));
		QCOMPARE_EQ(f.model().arcs[0].gradientPercent, number);
	}
	QVERIFY(!f.document.isDirty());
}

void TestTopologyFacets::editsConnections() {
	Fixture f;
	f.choose("connections");
	// Connections are listed with their nodes and limit.
	QCOMPARE(f.headers(), QStringList({"ID", "From node", "To node", "Has speed limit", "Speed limit (m/s)"}));
	// The flag is a check box, which has no text.
	QCOMPARE(f.rowTexts(0), QStringList({"connection-1", "node-a1", "node-b2", "", "0"}));
	QCOMPARE(f.table()->item(0, 3)->checkState(), Qt::Unchecked);
	// Listing connections edits nothing.
	QVERIFY(f.untouched());
	QVERIFY(!f.document.isDirty());

	// edit
	f.table()->item(0, 3)->setCheckState(Qt::Checked);
	// Checking the box sets the flag of the connection.
	QVERIFY(f.model().connections[0].hasSpeedLimit);
	QCOMPARE(f.table()->item(0, 3)->checkState(), Qt::Checked);
	QVERIFY(f.changedOnce("Edit connection"));
	// The box takes no text.
	QVERIFY(!f.table()->item(0, 3)->flags().testFlag(Qt::ItemIsEditable));
	f.type(0, 4, "12.5");
	f.type(0, 1, "node-a0");
	f.type(0, 2, "node-b0");
	// The limit and the nodes of a connection are edited.
	QCOMPARE_EQ(f.model().connections[0].speedLimitMs, 12.5);
	QCOMPARE(f.model().connections[0].fromNodeId, "node-a0");
	QCOMPARE(f.model().connections[0].toNodeId, "node-b0");
	f.log.clear();

	// rename: nothing refers to a connection by its id
	f.type(0, 0, "connection-x");
	// A connection is renamed.
	QCOMPARE(f.model().connections[0].id, "connection-x");
	QCOMPARE(f.model().connections[0].fromNodeId, "node-a0");
	QVERIFY(f.changedOnce("Rename connection"));
	QCOMPARE(f.panel.selectionId(), "connection-x");
	f.type(0, 0, "  ");
	// An empty id is refused.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().connections[0].id, "connection-x");
	QCOMPARE(f.messages, QStringList({"IDs must be unique and not empty"}));

	// add
	f.panel.addButton()->click();
	const SceneConnection& added = f.model().connections.back();
	// A connection is added between the first two nodes, and selected.
	QCOMPARE(f.model().connections.size(), std::size_t(2));
	QCOMPARE(added.id, "connection");
	QCOMPARE(added.fromNodeId, "node-a0");
	QCOMPARE(added.toNodeId, "node-a1");
	QVERIFY(!added.hasSpeedLimit);
	QCOMPARE_EQ(added.speedLimitMs, 0.0);
	QVERIFY(f.changedOnce("Add connection"));
	QCOMPARE(f.panel.selectionId(), "connection");
	QCOMPARE(f.table()->currentRow(), 1);
}

// A route uses a section that the connection gives.
void TestTopologyFacets::deletesConnectionThatRouteUses() {
	SceneModel scene = topologyScene();
	std::string derived;
	for (const SceneSectionDescriptor& section : buildSceneSectionInventory(scene).sections)
		if (section.sourceConnectionId == "connection-1")
			derived = section.id;
	QVERIFY2(!derived.empty(), "the connection yields a section");
	SceneRoute route;
	route.id = "route-1";
	route.blocks = {"block-a1", derived};
	scene.routes.push_back(route);
	Fixture used(scene);
	used.choose("connections");
	used.select(0);
	used.panel.deleteButton()->click();
	// A connection that a route uses cannot be deleted.
	QCOMPARE(used.prompts.errorCount, 1);
	QCOMPARE(used.prompts.confirmCount, 0);
	QCOMPARE(used.model().connections.size(), std::size_t(1));
	QVERIFY(used.untouched());
	// The message names the routes that use a section of the connection.
	QCOMPARE(used.prompts.errorMessage,
		blocked("Connection", "connection-1", {QString("route 'route-1' section '%1'").arg(QString::fromStdString(derived))}));
	used.document.mutableModel().routes[0].blocks.pop_back();
	used.panel.refresh();
	used.messages.clear();
	used.panel.deleteButton()->click();
	// A connection that nothing uses is deleted at once.
	QCOMPARE(used.messages, QStringList({"Deleted connection 'connection-1'. Undo restores it."}));
	QVERIFY(used.model().connections.empty());
	QVERIFY(used.changedOnce("Delete connection"));
}

// The Station column of nodes names the station of a node as the Details dock does: the station of a platform that lists
// it, by its name or else its id, or a station without platforms at the position of the node. The column is not edited.
void TestTopologyFacets::namesStationOfNodes() {
	SceneModel scene = topologyScene();
	SceneStation yard;
	yard.id = "station-2";
	yard.name = "Yard";
	yard.hasPosition = true;
	yard.positionKm = 2.0;
	scene.stations.push_back(yard);
	Fixture f(scene);
	f.choose("nodes");
	QStringList stations;
	for (int row = 0; row < f.table()->rowCount(); ++row)
		stations << f.cell(row, 1);
	// The platform of station-1, which has no name, lists node-a1, and Yard is at 2 km.
	QCOMPARE(stations, QStringList({"", "station-1", "Yard", "", "", "Yard"}));
	QVERIFY(!f.table()->item(1, 1)->flags().testFlag(Qt::ItemIsEditable));
	f.document.mutableModel().stations[0].name = "Alpha";
	f.panel.refresh();
	// The column follows the scene.
	QCOMPARE(f.cell(1, 1), "Alpha");
}

// The track of a node, the nodes of an arc and the nodes of a connection are chosen in a combo, in which the user can also
// type, and which completes from any part of a name, ignoring case. A choice is one edit, and an id that the scene does
// not have is refused with a message that names it.
void TestTopologyFacets::editsReferencesInCombos() {
	Fixture f;
	f.panel.show();
	// The panel is hidden before the fixture destroys it, also when a check fails.
	const auto hide = qScopeGuard([&f] { f.panel.hide(); });
	f.panel.activateWindow();
	QApplication::processEvents();

	// the track of a node
	f.choose("nodes");
	QComboBox* track = f.openCombo(1, 2);
	if (track == nullptr)
		QFAIL("the track of a node has no combo");
	// The combo lists the tracks, shows the track of the node, and can be typed in.
	QCOMPARE(track->objectName(), "infrastructureReferenceEditor");
	QCOMPARE(track->accessibleName(), "Track ID");
	QCOMPARE(labels(track), QStringList({"track-a", "track-b"}));
	QCOMPARE(track->currentText(), "track-a");
	QVERIFY(track->isEditable());
	choose(track, 1);
	// Choosing a track moves the node in one edit and ends the edit.
	QCOMPARE(f.model().nodes[1].trackId, "track-b");
	QVERIFY(f.changedOnce("Edit node"));
	QCOMPARE(f.cell(1, 2), "track-b");
	QVERIFY(f.closed(1, 2));
	track = f.openCombo(1, 2);
	if (track == nullptr)
		QFAIL("the track of a node has no combo");
	track->lineEdit()->setText("track-z");
	f.panel.commitPendingEditors();
	// A track that the scene does not have is refused, and the table shows the model again.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().nodes[1].trackId, "track-b");
	QCOMPARE(f.messages, QStringList({"Track 'track-z' does not exist"}));
	QCOMPARE(f.cell(1, 2), "track-b");
	f.messages.clear();
	track = f.openCombo(1, 2);
	if (track == nullptr)
		QFAIL("the track of a node has no combo");
	track->lineEdit()->setText("TRACK-A");
	f.panel.commitPendingEditors();
	// A track typed in another case is that track.
	QCOMPARE(f.model().nodes[1].trackId, "track-a");
	QVERIFY(f.changedOnce("Edit node"));

	// the nodes of an arc
	f.choose("arcs");
	QComboBox* to = f.openCombo(0, 3);
	if (to == nullptr)
		QFAIL("the end of an arc has no combo");
	// The combo lists the nodes of the track of the arc with their positions.
	QCOMPARE(to->accessibleName(), "To node");
	QCOMPARE(labels(to), QStringList({"node-a0 (0.000 km)", "node-a1 (1.000 km)", "node-a2 (2.000 km)"}));
	QCOMPARE(to->currentText(), "node-a1 (1.000 km)");
	QCompleter* completer = to->completer();
	completer->setCompletionPrefix("A2");
	// Typing completes from any part of a label, ignoring case.
	QCOMPARE(completer->completionCount(), 1);
	QCOMPARE(completer->currentCompletion(), "node-a2 (2.000 km)");
	choose(to, 2);
	// Choosing a node is one edit, and the cell shows its id.
	QCOMPARE(f.model().arcs[0].toNodeId, "node-a2");
	QVERIFY(f.changedOnce("Edit arc"));
	QCOMPARE(f.cell(0, 3), "node-a2");
	QComboBox* from = f.openCombo(0, 2);
	if (from == nullptr)
		QFAIL("the start of an arc has no combo");
	from->lineEdit()->setText("node-z");
	f.panel.commitPendingEditors();
	// A node that the scene does not have is refused.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().arcs[0].fromNodeId, "node-a0");
	QCOMPARE(f.messages, QStringList({"Node 'node-z' does not exist"}));
	from = f.openCombo(0, 2);
	if (from == nullptr)
		QFAIL("the start of an arc has no combo");
	from->lineEdit()->setText("node-a1");
	QTest::keyClick(from->lineEdit(), Qt::Key_Return);
	QApplication::processEvents();
	// A typed id is taken with Return.
	QCOMPARE(f.model().arcs[0].fromNodeId, "node-a1");
	QVERIFY(f.changedOnce("Edit arc"));
	from = f.openCombo(0, 2);
	if (from == nullptr)
		QFAIL("the start of an arc has no combo");
	from->lineEdit()->setText("NODE-A0");
	f.panel.commitPendingEditors();
	// An id typed in another case is that node, also where the label holds more than the id.
	QCOMPARE(f.model().arcs[0].fromNodeId, "node-a0");
	QVERIFY(f.changedOnce("Edit arc"));
	f.messages.clear();
	from = f.openCombo(0, 2);
	if (from == nullptr)
		QFAIL("the start of an arc has no combo");
	from->lineEdit()->clear();
	f.panel.commitPendingEditors();
	// An empty end is refused and asks for a node.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().arcs[0].fromNodeId, "node-a0");
	QCOMPARE(f.messages, QStringList({"Choose a node"}));
	f.messages.clear();

	// the nodes of a connection
	f.choose("connections");
	QComboBox* end = f.openCombo(0, 2);
	if (end == nullptr)
		QFAIL("the end of a connection has no combo");
	// The combo lists every node with its track.
	QCOMPARE(labels(end), QStringList({"node-a0 (track-a)", "node-a1 (track-a)", "node-a2 (track-a)", "node-b0 (track-b)", "node-b1 (track-b)", "node-b2 (track-b)"}));
	QCOMPARE(end->currentText(), "node-b2 (track-b)");
	end->completer()->setCompletionPrefix("B1");
	QCOMPARE(end->completer()->completionCount(), 1);
	choose(end, 3);
	// Choosing a node is one edit.
	QCOMPARE(f.model().connections[0].toNodeId, "node-b0");
	QVERIFY(f.changedOnce("Edit connection"));
	QComboBox* start = f.openCombo(0, 1);
	if (start == nullptr)
		QFAIL("the start of a connection has no combo");
	start->lineEdit()->setText("node-q");
	f.panel.commitPendingEditors();
	// A node that the scene does not have is refused.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().connections[0].fromNodeId, "node-a1");
	QCOMPARE(f.messages, QStringList({"Node 'node-q' does not exist"}));
}

// The keys up and down move from a combo of references to another row, as from a text cell, and choose no other entity.
void TestTopologyFacets::leavesComboWithArrowKeys() {
	Fixture f;
	f.panel.show();
	// The panel is hidden before the fixture destroys it, also when a check fails.
	const auto hide = qScopeGuard([&f] { f.panel.hide(); });
	f.panel.activateWindow();
	QApplication::processEvents();
	f.choose("nodes");
	QComboBox* track = f.openCombo(1, 2);
	if (track == nullptr)
		QFAIL("the track of a node has no combo");
	QTest::keyClick(track, Qt::Key_Down);
	QApplication::processEvents();
	// The next row is current, and the node keeps its track.
	QCOMPARE(f.table()->currentRow(), 2);
	QCOMPARE(f.table()->currentColumn(), 2);
	QCOMPARE(f.model().nodes[1].trackId, "track-a");
	QVERIFY(f.untouched());
	track = qobject_cast<QComboBox*>(f.table()->indexWidget(f.table()->model()->index(2, 2)));
	if (track == nullptr)
		QFAIL("moving to the track of a node opens no combo");
	QTest::keyClick(track, Qt::Key_Up);
	QApplication::processEvents();
	// Up goes back, and no node changes its track.
	QCOMPARE(f.table()->currentRow(), 1);
	QCOMPARE(f.model().nodes[2].trackId, "track-a");
	QVERIFY(f.untouched());
}

// The station ids, names, and platform ids with their nodes, as one text per station, to compare two models.
static QStringList stationTexts(const SceneModel& model) {
	QStringList result;
	for (const SceneStation& station : model.stations) {
		QStringList platforms;
		for (const ScenePlatform& platform : station.platforms) {
			QStringList nodes;
			for (const std::string& id : platform.nodeIds)
				nodes << QString::fromStdString(id);
			platforms << QString::fromStdString(platform.id) + "=" + nodes.join("+");
		}
		result << QString::fromStdString(station.id) + "/" + QString::fromStdString(station.name) + ":" + platforms.join(",");
	}
	return result;
}

// Add Station... at node-a2, which no station has, adds the station and its platform in one edit, selects the node
// again and says that Undo removes the station.
void TestTopologyFacets::addsStationAtNode() {
	Fixture fixture;
	fixture.choose("nodes");
	fixture.select(2);
	QCOMPARE(fixture.cell(2, 0), QString("node-a2"));
	fixture.prompts.answeredText = QString("Halt");
	QPushButton* button = fixture.panel.taskButton(0);
	QVERIFY(button);
	QCOMPARE(button->objectName(), QString("addStationAtNodeButton"));
	QVERIFY(button->isEnabled());

	button->click();

	QCOMPARE(fixture.prompts.textCount, 1);
	QCOMPARE(fixture.prompts.textTitle, QString("Add Station"));
	QCOMPARE(fixture.prompts.textLabel, QString("Name of the new station at node node-a2:"));
	QVERIFY(fixture.changedOnce("Add station"));
	QCOMPARE(fixture.model().stations.size(), std::size_t(2));
	const SceneStation& station = fixture.model().stations.back();
	QCOMPARE(station.id, std::string("Halt"));
	QCOMPARE(station.name, std::string("Halt"));
	QVERIFY(!station.hasPosition);
	QCOMPARE(station.platforms.size(), std::size_t(1));
	const ScenePlatform& platform = station.platforms.front();
	QCOMPARE(platform.id, std::string("Halt.platform.1"));
	QCOMPARE(platform.nodeIds, std::vector<std::string>({"node-a2"}));
	QVERIFY(!platform.hasLength);
	QVERIFY(!platform.hasWidth);
	QCOMPARE(fixture.cell(2, 1), QString("Halt"));
	QCOMPARE(fixture.panel.selectionId(), QString("node-a2"));
	QCOMPARE(fixture.table()->currentRow(), 2);
	QCOMPARE(fixture.messages, QStringList({"Added station 'Halt' at node 'node-a2'. Undo removes it."}));
}

// One Undo takes the station and its platform away together.
void TestTopologyFacets::undoRemovesAddedStation() {
	Fixture fixture;
	RunSelection selection;
	EditHistory history(fixture.document, selection, fixture.panel);
	const QStringList before = stationTexts(fixture.model());
	fixture.choose("nodes");
	fixture.select(2);
	fixture.prompts.answeredText = QString("Halt");
	fixture.panel.taskButton(0)->click();
	QCOMPARE(fixture.model().stations.size(), std::size_t(2));

	QVERIFY(history.undo());

	QCOMPARE(stationTexts(fixture.model()), before);
	QVERIFY(!history.undo());
}

// A node that a station has gets no second station, and the user is not asked for a name.
void TestTopologyFacets::refusesStationAtStationNode() {
	Fixture fixture;
	fixture.choose("nodes");
	fixture.select(1);
	QCOMPARE(fixture.cell(1, 0), QString("node-a1"));
	const QStringList before = stationTexts(fixture.model());
	fixture.prompts.answeredText = QString("Halt");

	fixture.panel.taskButton(0)->click();

	QCOMPARE(fixture.prompts.textCount, 0);
	QVERIFY(fixture.untouched());
	QCOMPARE(stationTexts(fixture.model()), before);
	QCOMPARE(fixture.messages,
		QStringList({"Node 'node-a1' already belongs to station 'station-1'. Add a platform to that station under Platforms."}));
}

void TestTopologyFacets::refusesTakenOrBadStationName_data() {
	QTest::addColumn<QString>("name");
	QTest::addColumn<QString>("message");
	const QString taken = "Station '%1' already exists. Add a platform to it under Platforms.";
	QTest::newRow("empty") << QString() << "Give the station a name.";
	QTest::newRow("spaces") << QString("  ") << "Give the station a name.";
	QTest::newRow("id taken") << QString("station-1") << taken.arg("station-1");
	QTest::newRow("name taken") << QString("Central") << taken.arg("Central");
	QTest::newRow("name taken after trimming") << QString(" Central ") << taken.arg("Central");
	QTest::newRow("tab") << QString("A\tB") << "A station name cannot hold a tab or a line break.";
	QTest::newRow("line break") << QString("A\nB") << "A station name cannot hold a tab or a line break.";
	QTest::newRow("carriage return") << QString("A\rB") << "A station name cannot hold a tab or a line break.";
}

// A name that is empty, holds a tab or a line break, or is a station's id or name changes nothing and says why.
void TestTopologyFacets::refusesTakenOrBadStationName() {
	QFETCH(QString, name);
	QFETCH(QString, message);
	SceneModel scene = topologyScene();
	scene.stations.front().name = "Central";
	Fixture fixture(scene);
	fixture.choose("nodes");
	fixture.select(2);
	const QStringList before = stationTexts(fixture.model());
	fixture.prompts.answeredText = name;

	fixture.panel.taskButton(0)->click();

	QCOMPARE(fixture.prompts.textCount, 1);
	QVERIFY(fixture.untouched());
	QCOMPARE(stationTexts(fixture.model()), before);
	QCOMPARE(fixture.messages, QStringList({message}));
}

// A case that has the most stations that a run takes gets no more, and the user is not asked for a name.
void TestTopologyFacets::refusesStationBeyondLimit() {
	SceneModel scene = topologyScene();
	while (scene.stations.size() < static_cast<std::size_t>(RuntimeLimits::kNativeMaxStations)) {
		SceneStation station;
		station.id = "extra-" + std::to_string(scene.stations.size());
		station.name = station.id;
		station.hasPosition = true;
		station.positionKm = 50.0;
		scene.stations.push_back(station);
	}
	Fixture fixture(scene);
	fixture.choose("nodes");
	fixture.select(2);
	fixture.prompts.answeredText = QString("Halt");

	fixture.panel.taskButton(0)->click();

	QCOMPARE(fixture.prompts.textCount, 0);
	QVERIFY(fixture.untouched());
	QCOMPARE(fixture.model().stations.size(), static_cast<std::size_t>(RuntimeLimits::kNativeMaxStations));
	QCOMPARE(fixture.messages,
		QStringList({QString("The case already has %1 stations, the most a run takes.").arg(static_cast<int>(RuntimeLimits::kNativeMaxStations))}));
}

// Cancel in the name box changes nothing and says nothing.
void TestTopologyFacets::cancelAddsNoStation() {
	Fixture fixture;
	fixture.choose("nodes");
	fixture.select(2);
	fixture.prompts.answeredText = std::nullopt;

	fixture.panel.taskButton(0)->click();

	QCOMPARE(fixture.prompts.textCount, 1);
	QVERIFY(fixture.untouched());
	QCOMPARE(fixture.model().stations.size(), std::size_t(1));
	QVERIFY(fixture.messages.isEmpty());
}

// The task row shows under Nodes only, and its button is on while a row is selected and the scene can be edited.
void TestTopologyFacets::taskButtonFollowsSelection() {
	Fixture fixture;
	fixture.choose("nodes");
	QPushButton* button = fixture.panel.taskButton(0);
	QVERIFY(button);
	QVERIFY(!button->isHidden());
	QVERIFY(!fixture.panel.taskButton(1));
	QCOMPARE(button->text(), QString("Add Station..."));
	QCOMPARE(button->toolTip(), QString("Adds a station with one platform at the selected node."));
	fixture.table()->setCurrentCell(-1, -1);
	fixture.panel.updateSelection();
	QVERIFY(!button->isEnabled());
	fixture.select(0);
	QVERIFY(button->isEnabled());
	fixture.document.setEditable(false);
	fixture.panel.refresh();
	QVERIFY(!button->isEnabled());
	fixture.document.setEditable(true);
	fixture.panel.refresh();
	fixture.choose("arcs");
	QVERIFY(!fixture.panel.taskButton(0));
}

// The topology scene and a third track whose X falls along its arcs: nodes at 2, 1 and 0, a climb of 0.01 on the first
// arc and none on the second, and two blocks of 0.5 and 1.5 km that share scene.blocks with the other tracks. A route
// runs over both blocks of the track and a second route over a block of track-a.
static SceneModel fallingTrackScene() {
	SceneModel scene = topologyScene();
	scene.tracks.push_back({"track-c"});
	scene.nodes.push_back(node("node-c0", "track-c", 2.0));
	scene.nodes.push_back(node("node-c1", "track-c", 1.0));
	scene.nodes.push_back(node("node-c2", "track-c", 0.0));
	scene.arcs.push_back(arc("arc-c1", "track-c", "node-c0", "node-c1"));
	scene.arcs.push_back(arc("arc-c2", "track-c", "node-c1", "node-c2"));
	scene.arcs[4].gradientPercent = 0.01;
	SceneBlock first = block("block-c1", "track-c");
	first.lengthKm = 0.5;
	SceneBlock second = block("block-c2", "track-c");
	second.lengthKm = 1.5;
	scene.blocks = {block("block-a1", "track-a"), first, block("block-a2", "track-a"), block("block-b1", "track-b"),
		second, block("block-b2", "track-b")};
	SceneRoute over;
	over.id = "route-c";
	over.blocks = {"block-c1", "block-c2"};
	SceneRoute elsewhere;
	elsewhere.id = "route-a";
	elsewhere.blocks = {"block-a1"};
	scene.routes = {elsewhere, over};
	return scene;
}

static int orderErrors(const SceneModel& scene) {
	int count = 0;
	for (const SceneDiagnostic& diagnostic : validateScene(scene))
		count += diagnostic.code == "scene.topology.order" ? 1 : 0;
	return count;
}

static std::vector<std::string> blockIds(const SceneModel& scene) {
	std::vector<std::string> result;
	for (const SceneBlock& item : scene.blocks)
		result.push_back(item.id);
	return result;
}

// Reverse Track swaps the ends of the arcs of track-c, negates the climb but keeps 0 as +0, and reverses the blocks
// within the places that track-c holds in scene.blocks. That clears the order errors, and it names the route that runs
// over the track.
void TestTopologyFacets::reversesFallingTrack() {
	Fixture fixture(fallingTrackScene());
	QVERIFY(orderErrors(fixture.model()) > 0);
	fixture.choose("tracks");
	fixture.select(2);
	QCOMPARE(fixture.cell(2, 0), QString("track-c"));
	QPushButton* button = fixture.panel.taskButton(0);
	QVERIFY(button);
	QCOMPARE(button->objectName(), QString("reverseTrackButton"));
	QCOMPARE(button->text(), QString("Reverse Track"));
	QCOMPARE(button->toolTip(),
		QString("Swaps the ends of every arc of the selected track and reverses the order of its blocks."));
	QVERIFY(button->isEnabled());

	button->click();

	QVERIFY(fixture.changedOnce("Reverse track"));
	const SceneModel& model = fixture.model();
	QCOMPARE(orderErrors(model), 0);
	QCOMPARE(model.arcs[4].fromNodeId, std::string("node-c1"));
	QCOMPARE(model.arcs[4].toNodeId, std::string("node-c0"));
	QCOMPARE(model.arcs[5].fromNodeId, std::string("node-c2"));
	QCOMPARE(model.arcs[5].toNodeId, std::string("node-c1"));
	QCOMPARE(model.arcs[4].gradientPercent, -0.01);
	QCOMPARE(model.arcs[5].gradientPercent, 0.0);
	QVERIFY(!std::signbit(model.arcs[5].gradientPercent));
	QCOMPARE(model.arcs[0].fromNodeId, std::string("node-a0"));
	QCOMPARE(blockIds(model),
		std::vector<std::string>({"block-a1", "block-c2", "block-a2", "block-b1", "block-c1", "block-b2"}));
	QCOMPARE(model.nodes[6].xKm, 2.0);
	const SceneSectionInventory inventory = buildSceneSectionInventory(model);
	const SceneSectionDescriptor* high = inventory.exact("@block-c2@");
	const SceneSectionDescriptor* low = inventory.exact("@block-c1@");
	QVERIFY(high && low);
	QCOMPARE(high->startKm, 0.0);
	QCOMPARE(high->endKm, 1.5);
	QCOMPARE(low->startKm, 1.5);
	QCOMPARE(low->endKm, 2.0);
	for (const SceneSectionDescriptor* section : {high, low}) {
		QVERIFY(!section->layoutOverflow);
		QVERIFY(!section->clippedToTrackEnd);
		QVERIFY(!section->trackCoverageGap);
	}
	QCOMPARE(fixture.prompts.infoCount, 1);
	QCOMPARE(fixture.prompts.infoTitle, QString("Track Reversed"));
	QCOMPARE(fixture.prompts.infoMessage,
		QString("Track 'track-c' is reversed. Check these routes, which run over it:\n\xE2\x80\xA2 route-c"));
	QCOMPARE(fixture.messages, QStringList({"Reversed track 'track-c'. Undo restores it."}));
}

// One Undo puts the arcs, the gradients and the block order back.
void TestTopologyFacets::undoRestoresReversedTrack() {
	Fixture fixture(fallingTrackScene());
	RunSelection selection;
	EditHistory history(fixture.document, selection, fixture.panel);
	const SceneModel before = fixture.model();
	fixture.choose("tracks");
	fixture.select(2);
	fixture.panel.taskButton(0)->click();
	QVERIFY(fixture.model().arcs[4].fromNodeId != before.arcs[4].fromNodeId);

	QVERIFY(history.undo());

	const SceneModel& model = fixture.model();
	QCOMPARE(model.arcs.size(), before.arcs.size());
	for (std::size_t index = 0; index < model.arcs.size(); ++index) {
		QCOMPARE(model.arcs[index].fromNodeId, before.arcs[index].fromNodeId);
		QCOMPARE(model.arcs[index].toNodeId, before.arcs[index].toNodeId);
		QCOMPARE(model.arcs[index].gradientPercent, before.arcs[index].gradientPercent);
		QCOMPARE(std::signbit(model.arcs[index].gradientPercent), std::signbit(before.arcs[index].gradientPercent));
	}
	QCOMPARE(blockIds(model), blockIds(before));
	QVERIFY(!history.undo());
}

// A track whose arcs are not one line, here two arcs that leave node-c0, is not changed, and the status line says why.
void TestTopologyFacets::refusesBrokenTrack() {
	SceneModel scene = fallingTrackScene();
	scene.arcs.push_back(arc("arc-c3", "track-c", "node-c0", "node-c2"));
	Fixture fixture(scene);
	fixture.choose("tracks");
	fixture.select(2);

	fixture.panel.taskButton(0)->click();

	QVERIFY(fixture.untouched());
	QCOMPARE(fixture.prompts.infoCount, 0);
	QCOMPARE(fixture.messages,
		QStringList({"Track 'track-c' is not one line of arcs. Case Validation names what to fix."}));
	QCOMPARE(fixture.model().arcs[4].fromNodeId, std::string("node-c0"));
	QCOMPARE(blockIds(fixture.model()), blockIds(scene));
}

// A track that no route runs over is reversed without a box.
void TestTopologyFacets::reverseWithoutRoutesShowsNoBox() {
	Fixture fixture(fallingTrackScene());
	fixture.choose("tracks");
	fixture.select(1);
	QCOMPARE(fixture.cell(1, 0), QString("track-b"));

	fixture.panel.taskButton(0)->click();

	QVERIFY(fixture.changedOnce("Reverse track"));
	QCOMPARE(fixture.prompts.infoCount, 0);
	QCOMPARE(fixture.model().arcs[2].fromNodeId, std::string("node-b1"));
	QCOMPARE(fixture.messages, QStringList({"Reversed track 'track-b'. Undo restores it."}));
}

QTEST_MAIN(TestTopologyFacets)
#include "test_editor_infrastructure_topology.moc"
