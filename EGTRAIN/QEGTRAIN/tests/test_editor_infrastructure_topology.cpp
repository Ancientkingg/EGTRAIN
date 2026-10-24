#include "editors/InfrastructurePanel.h"
#include "editors/infrastructure/TopologyFacets.h"

#include "scene/SectionInventory.h"

#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTableWidget>

#include <cstddef>
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

static bool testFacets() {
	bool ok = true;
	const std::vector<std::unique_ptr<InfrastructureFacet>> facets = makeTopologyFacets();
	QStringList keys;
	for (const std::unique_ptr<InfrastructureFacet>& facet : facets)
		keys << facet->key();
	ok &= expect(keys == QStringList({"tracks", "nodes", "arcs", "connections"}), "the network has four facets");

	Fixture f;
	ok &= expect(f.panel.facet("tracks") && f.panel.facet("nodes") && f.panel.facet("arcs") && f.panel.facet("connections")
			&& !f.panel.facet("blocks"),
		"the panel has the facets of the network and no others");
	f.choose("blocks");
	ok &= expect(f.table()->columnCount() == 0 && f.table()->rowCount() == 0, "a facet that has no class lists nothing");
	f.choose("arcs");
	f.panel.commitCell(99, 0);
	f.panel.commitCell(0, 99);
	ok &= expect(f.untouched(), "a cell that is not in the table changes nothing");
	f.document.setEditable(false);
	f.panel.refresh();
	f.panel.addEntity();
	f.type(0, 4, "5");
	ok &= expect(f.untouched() && f.model().arcs.size() == 4 && f.model().arcs[0].curvatureRadiusM == 0.0,
		"nothing is edited or added while a run uses the scene");
	return ok;
}

static bool testTracks() {
	bool ok = true;
	Fixture f;
	f.choose("tracks");
	ok &= expect(f.headers() == QStringList({"ID"}) && f.firstColumn() == QStringList({"track-a", "track-b"}), "tracks are listed by their id");
	ok &= expect(f.untouched() && !f.document.isDirty(), "listing tracks edits nothing");

	// renaming a track rewrites what sits on it
	f.type(1, 0, " track-c ");
	const SceneModel& model = f.model();
	ok &= expect(model.tracks[1].id == "track-c" && model.nodes[3].trackId == "track-c" && model.nodes[4].trackId == "track-c"
			&& model.nodes[5].trackId == "track-c" && model.arcs[2].trackId == "track-c" && model.arcs[3].trackId == "track-c"
			&& model.blocks[2].trackId == "track-c" && model.blocks[3].trackId == "track-c",
		"a renamed track keeps its nodes, arcs and blocks");
	ok &= expect(model.nodes[0].trackId == "track-a" && model.arcs[0].trackId == "track-a" && model.blocks[0].trackId == "track-a"
			&& model.signallingAreas[0].trackId == "track-a",
		"a rename leaves the other tracks as they are");
	ok &= expect(f.changedOnce("Rename track") && f.panel.selectionId() == "track-c" && f.cell(1, 0) == "track-c" && f.table()->currentRow() == 1,
		"a rename marks the document once, and the renamed track stays selected");
	f.type(0, 0, " track-a ");
	ok &= expect(f.untouched() && f.model().tracks[0].id == "track-a", "an id that is the same after trimming changes nothing");

	// ids that a track cannot have
	f.type(0, 0, "track-c");
	ok &= expect(f.untouched() && f.model().tracks[0].id == "track-a" && f.cell(0, 0) == "track-a"
			&& f.messages == QStringList({"IDs must be unique and not empty"}),
		"an id that another track has is refused, with a message, and the table shows the model again");
	f.type(0, 0, "  ");
	ok &= expect(f.untouched() && f.model().tracks[0].id == "track-a" && f.cell(0, 0) == "track-a" && f.messages.size() == 2,
		"an empty id is refused");

	// add
	f.panel.addButton()->click();
	ok &= expect(f.model().tracks.size() == 3 && f.model().tracks[2].id == "track" && f.changedOnce("Add track")
			&& f.panel.selectionId() == "track" && f.table()->currentRow() == 2 && f.cell(2, 0) == "track",
		"a track is added with a new id, and selected");
	f.panel.addButton()->click();
	ok &= expect(f.model().tracks.size() == 4 && f.model().tracks[3].id == "track_2" && f.changedOnce("Add track"),
		"the id of the next track is new too");

	// delete
	f.select(0);
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.errorCount == 1 && f.prompts.confirmCount == 0 && f.model().tracks.size() == 4 && f.untouched(),
		"a track that others use cannot be deleted");
	ok &= expect(f.prompts.errorMessage
			== blocked("Track", "track-a",
				{"node 'node-a0'", "node 'node-a1'", "node 'node-a2'", "arc 'arc-a1'", "arc 'arc-a2'", "block 'block-a1'",
					"block 'block-a2'", "signalling area 'area-1'"}),
		"the message names the nodes, arcs, blocks and signalling areas that use the track");
	f.select(2);
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.confirmCount == 1 && f.prompts.confirmQuestion == "Delete track 'track'?" && f.model().tracks.size() == 4 && f.untouched(),
		"nothing is deleted when the user answers no");
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.confirmCount == 2 && f.model().tracks.size() == 3 && f.model().tracks[2].id == "track_2" && f.changedOnce("Delete track")
			&& f.panel.selectionId() == "track_2" && f.table()->currentRow() == 2,
		"a track that nothing uses is deleted when the user answers yes, and the row at its place is selected");
	return ok;
}

static bool testNodes() {
	bool ok = true;
	Fixture f;
	f.choose("nodes");
	ok &= expect(f.headers() == QStringList({"ID", "Track ID", "X (km)", "Y (km)"}) && f.table()->rowCount() == 6
			&& f.rowTexts(1) == QStringList({"node-a1", "track-a", "1", "0"}),
		"nodes are listed with their track and position");
	ok &= expect(f.untouched() && !f.document.isDirty(), "listing nodes edits nothing");

	// edit
	f.type(1, 2, "2.5");
	ok &= expect(f.model().nodes[1].xKm == 2.5 && f.cell(1, 2) == "2.5" && f.changedOnce("Edit node"), "a position is edited");
	f.type(1, 3, "-0.125");
	ok &= expect(f.model().nodes[1].yKm == -0.125 && f.changedOnce("Edit node"), "the other coordinate is edited");
	f.type(1, 1, "track-b");
	ok &= expect(f.model().nodes[1].trackId == "track-b" && f.changedOnce("Edit node"), "the track of a node is edited");
	for (const char* refused : {"abc", "", "inf", "nan"}) {
		f.type(1, 2, refused);
		ok &= expect(f.untouched() && f.model().nodes[1].xKm == 2.5 && f.cell(1, 2) == "2.5" && f.messages.empty(),
			"a position that is no finite number is refused, without a message, and the table shows the model again");
	}
	f.type(1, 2, " 2.5 ");
	ok &= expect(f.untouched(), "a position that is the same number changes nothing");

	// rename
	f.type(1, 0, "node-x");
	const SceneModel& model = f.model();
	ok &= expect(model.nodes[1].id == "node-x" && model.arcs[0].toNodeId == "node-x" && model.arcs[1].fromNodeId == "node-x"
			&& model.connections[0].fromNodeId == "node-x" && model.stations[0].platforms[0].nodeIds[0] == "node-x",
		"a renamed node keeps its arcs, connections and platforms");
	ok &= expect(model.arcs[0].fromNodeId == "node-a0" && model.arcs[1].toNodeId == "node-a2" && model.connections[0].toNodeId == "node-b2",
		"a rename leaves the other ends as they are");
	ok &= expect(f.changedOnce("Rename node") && f.panel.selectionId() == "node-x" && f.table()->currentRow() == 1,
		"a rename marks the document once, and the renamed node stays selected");
	f.type(0, 0, "node-x");
	ok &= expect(f.untouched() && f.model().nodes[0].id == "node-a0" && f.messages == QStringList({"IDs must be unique and not empty"}),
		"an id that another node has is refused");

	// add
	f.panel.addButton()->click();
	const SceneNode& added = f.model().nodes.back();
	ok &= expect(f.model().nodes.size() == 7 && added.id == "node" && added.trackId == "track-a" && added.xKm == 3.0 && added.yKm == 0.0
			&& f.changedOnce("Add node") && f.panel.selectionId() == "node" && f.table()->currentRow() == 6 && f.rowTexts(6) == QStringList({"node", "track-a", "3", "0"}),
		"a node is added on the first track, one kilometre after its last node, and selected");

	// delete
	f.select(1);
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.errorCount == 1 && f.prompts.confirmCount == 0 && f.model().nodes.size() == 7 && f.untouched(),
		"a node that others use cannot be deleted");
	ok &= expect(f.prompts.errorMessage
			== blocked("Node", "node-x",
				{"arc 'arc-a1' endpoint", "arc 'arc-a2' endpoint", "connection 'connection-1' endpoint",
					"platform 'platform-1' at station 'station-1'"}),
		"the message names the arcs, connections and platforms that use the node");
	f.select(6);
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.confirmQuestion == "Delete node 'node'?" && f.model().nodes.size() == 6 && f.changedOnce("Delete node")
			&& f.panel.selectionId() == "node-b2",
		"a node that nothing uses is deleted when the user answers yes");
	return ok;
}

static bool testArcs() {
	bool ok = true;
	Fixture f;
	f.choose("arcs");
	ok &= expect(f.headers()
				== QStringList({"ID", "Track ID", "From node", "To node", "Curvature radius (m)", "Gradient (rise/length)", "Speed limit (m/s)"})
			&& f.table()->rowCount() == 4 && f.rowTexts(0) == QStringList({"arc-a1", "track-a", "node-a0", "node-a1", "0", "0", "20"}),
		"arcs are listed with their nodes and limits");
	ok &= expect(f.untouched() && !f.document.isDirty(), "listing arcs edits nothing");

	// edit
	f.type(0, 2, "node-a2");
	f.type(0, 3, "node-a0");
	ok &= expect(f.model().arcs[0].fromNodeId == "node-a2" && f.model().arcs[0].toNodeId == "node-a0" && f.cell(0, 3) == "node-a0",
		"the ends of an arc are edited");
	f.log.clear();
	f.type(0, 1, "track-b");
	ok &= expect(f.model().arcs[0].trackId == "track-b" && f.changedOnce("Edit arc"), "the track of an arc is edited");
	f.type(0, 4, "150.5");
	f.type(0, 5, "-0.01");
	f.type(0, 6, "30");
	ok &= expect(f.model().arcs[0].curvatureRadiusM == 150.5 && f.model().arcs[0].gradientPercent == -0.01 && f.model().arcs[0].speedLimitMs == 30.0,
		"the curvature, the gradient and the speed limit are edited");
	f.log.clear();
	f.type(0, 6, "fast");
	ok &= expect(f.untouched() && f.model().arcs[0].speedLimitMs == 30.0 && f.cell(0, 6) == "30" && f.messages.empty(),
		"a limit that is no number is refused and the table shows the model again");

	// rename: nothing refers to an arc
	const std::vector<SceneArc> before = f.model().arcs;
	f.type(1, 0, "arc-x");
	ok &= expect(f.model().arcs[1].id == "arc-x" && f.model().arcs[0].id == "arc-a1" && f.model().arcs[2].id == "arc-b1"
			&& f.model().arcs[1].trackId == before[1].trackId && f.changedOnce("Rename arc") && f.panel.selectionId() == "arc-x",
		"an arc is renamed, and the other arcs are as they were");
	f.type(2, 0, "arc-x");
	ok &= expect(f.untouched() && f.model().arcs[2].id == "arc-b1" && f.messages == QStringList({"IDs must be unique and not empty"}),
		"an id that another arc has is refused");

	// add: after the arcs that the first track has, between its next two nodes
	f.document.mutableModel().arcs.erase(f.document.mutableModel().arcs.begin() + 1);
	f.document.mutableModel().arcs[0].trackId = "track-a";
	f.panel.refresh();
	f.panel.addButton()->click();
	const SceneArc& added = f.model().arcs.back();
	ok &= expect(added.id == "arc" && added.trackId == "track-a" && added.fromNodeId == "node-a1" && added.toNodeId == "node-a2"
			&& added.curvatureRadiusM == 0.0 && added.gradientPercent == 0.0 && added.speedLimitMs == 20.0 && f.changedOnce("Add arc")
			&& f.panel.selectionId() == "arc" && f.table()->currentRow() == f.table()->rowCount() - 1,
		"an arc is added on the first track from the node after the end of its arcs, and selected");

	// delete: nothing refers to an arc
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.errorCount == 0 && f.prompts.confirmQuestion == "Delete arc 'arc'?" && f.model().arcs.size() == 3
			&& f.changedOnce("Delete arc"),
		"an arc is deleted when the user answers yes, as nothing uses it");
	return ok;
}

static bool testConnections() {
	bool ok = true;
	Fixture f;
	f.choose("connections");
	ok &= expect(f.headers() == QStringList({"ID", "From node", "To node", "Has speed limit", "Speed limit (m/s)"})
			&& f.rowTexts(0) == QStringList({"connection-1", "node-a1", "node-b2", "false", "0"}),
		"connections are listed with their nodes and limit");
	ok &= expect(f.untouched() && !f.document.isDirty(), "listing connections edits nothing");

	// edit
	f.type(0, 3, "True");
	ok &= expect(f.model().connections[0].hasSpeedLimit && f.cell(0, 3) == "true" && f.changedOnce("Edit connection"),
		"the flag of a connection is edited, whatever the case");
	f.type(0, 3, "TRUE");
	ok &= expect(f.untouched(), "a flag that has the value changes nothing");
	f.type(0, 3, "maybe");
	ok &= expect(f.untouched() && f.model().connections[0].hasSpeedLimit && f.cell(0, 3) == "true" && f.messages.empty(),
		"a flag that is neither true nor false is refused and the table shows the model again");
	f.type(0, 4, "12.5");
	f.type(0, 1, "node-a0");
	f.type(0, 2, "node-b0");
	ok &= expect(f.model().connections[0].speedLimitMs == 12.5 && f.model().connections[0].fromNodeId == "node-a0"
			&& f.model().connections[0].toNodeId == "node-b0",
		"the limit and the nodes of a connection are edited");
	f.log.clear();

	// rename: nothing refers to a connection by its id
	f.type(0, 0, "connection-x");
	ok &= expect(f.model().connections[0].id == "connection-x" && f.model().connections[0].fromNodeId == "node-a0" && f.changedOnce("Rename connection")
			&& f.panel.selectionId() == "connection-x",
		"a connection is renamed");
	f.type(0, 0, "  ");
	ok &= expect(f.untouched() && f.model().connections[0].id == "connection-x" && f.messages == QStringList({"IDs must be unique and not empty"}),
		"an empty id is refused");

	// add
	f.panel.addButton()->click();
	const SceneConnection& added = f.model().connections.back();
	ok &= expect(f.model().connections.size() == 2 && added.id == "connection" && added.fromNodeId == "node-a0" && added.toNodeId == "node-a1"
			&& !added.hasSpeedLimit && added.speedLimitMs == 0.0 && f.changedOnce("Add connection") && f.panel.selectionId() == "connection"
			&& f.table()->currentRow() == 1,
		"a connection is added between the first two nodes, and selected");

	// delete: a route uses a section that the connection gives
	SceneModel scene = topologyScene();
	std::string derived;
	for (const SceneSectionDescriptor& section : buildSceneSectionInventory(scene).sections)
		if (section.sourceConnectionId == "connection-1")
			derived = section.id;
	ok &= expect(!derived.empty(), "the connection yields a section");
	SceneRoute route;
	route.id = "route-1";
	route.blocks = {"block-a1", derived};
	scene.routes.push_back(route);
	Fixture used(scene);
	used.choose("connections");
	used.select(0);
	used.panel.deleteButton()->click();
	ok &= expect(used.prompts.errorCount == 1 && used.prompts.confirmCount == 0 && used.model().connections.size() == 1 && used.untouched(),
		"a connection that a route uses cannot be deleted");
	ok &= expect(used.prompts.errorMessage
			== blocked("Connection", "connection-1", {QString("route 'route-1' section '%1'").arg(QString::fromStdString(derived))}),
		"the message names the routes that use a section of the connection");
	used.document.mutableModel().routes[0].blocks.pop_back();
	used.panel.refresh();
	used.prompts.confirmAnswer = true;
	used.panel.deleteButton()->click();
	ok &= expect(used.prompts.confirmQuestion == "Delete connection 'connection-1'?" && used.model().connections.empty() && used.changedOnce("Delete connection"),
		"a connection that nothing uses is deleted when the user answers yes");
	return ok;
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;
	ok &= testFacets();
	ok &= testTracks();
	ok &= testNodes();
	ok &= testArcs();
	ok &= testConnections();
	return ok ? 0 : 1;
}
