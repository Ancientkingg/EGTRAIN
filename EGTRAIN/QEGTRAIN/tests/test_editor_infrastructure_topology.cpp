#include "editors/InfrastructurePanel.h"
#include "editors/infrastructure/TopologyFacets.h"

#include "scene/SectionInventory.h"

#include "support/FakePrompts.h"

#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTest>

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
};

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
	void editsConnections();
	void deletesConnectionThatRouteUses();
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
	f.panel.deleteButton()->click();
	// Nothing is deleted when the user answers no.
	QCOMPARE(f.prompts.confirmCount, 1);
	QCOMPARE(f.prompts.confirmQuestion, "Delete track 'track'?");
	QCOMPARE(f.model().tracks.size(), std::size_t(4));
	QVERIFY(f.untouched());
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	// A track that nothing uses is deleted when the user answers yes, and the row at its place is selected.
	QCOMPARE(f.prompts.confirmCount, 2);
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
	QCOMPARE(f.headers(), QStringList({"ID", "Track ID", "X (km)", "Y (km)"}));
	QCOMPARE(f.table()->rowCount(), 6);
	QCOMPARE(f.rowTexts(1), QStringList({"node-a1", "track-a", "1", "0"}));
	// Listing nodes edits nothing.
	QVERIFY(f.untouched());
	QVERIFY(!f.document.isDirty());

	// edit
	f.type(1, 2, "2.5");
	// A position is edited.
	QCOMPARE_EQ(f.model().nodes[1].xKm, 2.5);
	QCOMPARE(f.cell(1, 2), "2.5");
	QVERIFY(f.changedOnce("Edit node"));
	f.type(1, 3, "-0.125");
	// The other coordinate is edited.
	QCOMPARE_EQ(f.model().nodes[1].yKm, -0.125);
	QVERIFY(f.changedOnce("Edit node"));
	f.type(1, 1, "track-b");
	// The track of a node is edited.
	QCOMPARE(f.model().nodes[1].trackId, "track-b");
	QVERIFY(f.changedOnce("Edit node"));
	for (const char* refused : {"abc", "", "inf", "nan"}) {
		f.type(1, 2, refused);
		// A position that is no finite number is refused, without a message, and the table shows the model again.
		QVERIFY2(f.untouched(), refused);
		QCOMPARE_EQ(f.model().nodes[1].xKm, 2.5);
		QCOMPARE(f.cell(1, 2), "2.5");
		QVERIFY2(f.messages.empty(), refused);
	}
	f.type(1, 2, " 2.5 ");
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
	QCOMPARE(f.rowTexts(6), QStringList({"node", "track-a", "3", "0"}));

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
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	// A node that nothing uses is deleted when the user answers yes.
	QCOMPARE(f.prompts.confirmQuestion, "Delete node 'node'?");
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
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	// An arc is deleted when the user answers yes, as nothing uses it.
	QCOMPARE(f.prompts.errorCount, 0);
	QCOMPARE(f.prompts.confirmQuestion, "Delete arc 'arc'?");
	QCOMPARE(f.model().arcs.size(), std::size_t(3));
	QVERIFY(f.changedOnce("Delete arc"));
}

void TestTopologyFacets::editsConnections() {
	Fixture f;
	f.choose("connections");
	// Connections are listed with their nodes and limit.
	QCOMPARE(f.headers(), QStringList({"ID", "From node", "To node", "Has speed limit", "Speed limit (m/s)"}));
	QCOMPARE(f.rowTexts(0), QStringList({"connection-1", "node-a1", "node-b2", "false", "0"}));
	// Listing connections edits nothing.
	QVERIFY(f.untouched());
	QVERIFY(!f.document.isDirty());

	// edit
	f.type(0, 3, "True");
	// The flag of a connection is edited, whatever the case.
	QVERIFY(f.model().connections[0].hasSpeedLimit);
	QCOMPARE(f.cell(0, 3), "true");
	QVERIFY(f.changedOnce("Edit connection"));
	f.type(0, 3, "TRUE");
	QVERIFY2(f.untouched(), "a flag that has the value changes nothing");
	f.type(0, 3, "maybe");
	// A flag that is neither true nor false is refused and the table shows the model again.
	QVERIFY(f.untouched());
	QVERIFY(f.model().connections[0].hasSpeedLimit);
	QCOMPARE(f.cell(0, 3), "true");
	QVERIFY(f.messages.empty());
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
	used.prompts.confirmAnswer = true;
	used.panel.deleteButton()->click();
	// A connection that nothing uses is deleted when the user answers yes.
	QCOMPARE(used.prompts.confirmQuestion, "Delete connection 'connection-1'?");
	QVERIFY(used.model().connections.empty());
	QVERIFY(used.changedOnce("Delete connection"));
}

QTEST_MAIN(TestTopologyFacets)
#include "test_editor_infrastructure_topology.moc"
