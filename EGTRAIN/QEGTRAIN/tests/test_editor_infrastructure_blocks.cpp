#include "editors/InfrastructurePanel.h"
#include "editors/infrastructure/BlockRouteFacets.h"

#include "scene/SectionInventory.h"

#include "support/FakePrompts.h"

#include <QApplication>
#include <QComboBox>
#include <QListWidget>
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

static SceneBlock block(const char* id, const char* trackId, double lengthKm = 1.0) {
	SceneBlock result;
	result.id = id;
	result.trackId = trackId;
	result.lengthKm = lengthKm;
	return result;
}

// The runtime id of the section that a block gives, which is what the combos of sections store.
static std::string sectionOf(const SceneModel& scene, const char* blockId) {
	for (const SceneSectionDescriptor& section : buildSceneSectionInventory(scene).sections)
		if (section.sourceBlockId == blockId)
			return section.id;
	return {};
}

// Two tracks of three nodes, two arcs and two blocks each. A route, a service on it, a signal, a dependency, a
// restriction and a station boundary use blocks of the first track by the id of their section. Every id has a name of its
// own, so a test can tell which reference a rewrite reached.
static SceneModel blockScene() {
	SceneModel scene;
	scene.name = "Blocks";
	scene.tracks = {{"track-a"}, {"track-b"}};
	scene.nodes = {node("node-a0", "track-a", 0.0), node("node-a1", "track-a", 1.0), node("node-a2", "track-a", 2.0),
		node("node-b0", "track-b", 0.0), node("node-b1", "track-b", 1.0), node("node-b2", "track-b", 2.0)};
	scene.arcs = {arc("arc-a1", "track-a", "node-a0", "node-a1"), arc("arc-a2", "track-a", "node-a1", "node-a2"),
		arc("arc-b1", "track-b", "node-b0", "node-b1"), arc("arc-b2", "track-b", "node-b1", "node-b2")};
	scene.blocks = {block("block-a1", "track-a"), block("block-a2", "track-a"), block("block-b1", "track-b"),
		block("block-b2", "track-b")};
	const std::string a1 = sectionOf(scene, "block-a1");
	const std::string a2 = sectionOf(scene, "block-a2");
	const std::string b1 = sectionOf(scene, "block-b1");
	const std::string b2 = sectionOf(scene, "block-b2");

	SceneRoute route;
	route.id = "route-1";
	route.blocks = {a1, a2};
	scene.routes.push_back(route);
	route.id = "route-2";
	route.blocks = {b1};
	scene.routes.push_back(route);
	SceneService service;
	service.id = "service-1";
	service.route = "route-1";
	scene.services.push_back(service);
	sceneSignals(scene).push_back({"signal-1", a1});
	scene.blockDependencies.push_back({a1, a2});
	scene.singleTrackRestrictions.push_back({a1, a2, b1, b2});
	SceneStationBoundary boundary;
	boundary.entranceBlock = a1;
	boundary.hasExitBlock = true;
	boundary.exitBlock = a2;
	scene.stationBoundaries.push_back(boundary);
	return scene;
}

using Log = std::vector<std::string>;

// A panel with the facets of the blocks and routes, over a document that holds the scene. The events are the order in
// which the panel and its document report an edit, and the messages that the panel shows in the status line.
struct Fixture {
	SceneDocument document;
	InfrastructurePanel panel;
	FakePrompts prompts;
	Log log;
	SceneDocument::ChangeKinds kinds;
	QStringList messages;

	explicit Fixture(SceneModel scene = blockScene()) {
		panel.setPrompts(&prompts);
		for (std::unique_ptr<InfrastructureFacet>& facet : makeBlockRouteFacets())
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
	// Shows the blocks of this track.
	void chooseTrack(const char* track) {
		panel.blockTrackFilterCombo()->setCurrentIndex(panel.blockTrackFilterCombo()->findData(track));
	}
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
	QComboBox* combo(int row, int column) { return qobject_cast<QComboBox*>(table()->cellWidget(row, column)); }
	// Chooses the section that the block gives in the combo.
	void choose(int row, int column, const char* blockId) {
		QComboBox* box = combo(row, column);
		box->setCurrentIndex(box->findData(QString::fromStdString(sectionOf(model(), blockId))));
	}
	QString chosen(int row, int column) { return combo(row, column)->currentData().toString(); }
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

class TestBlockRouteFacets : public QObject {
	Q_OBJECT

private slots:
	void offersBlockAndRouteFacets();
	void listsBlocks();
	void editsBlocks();
	void addsBlocks();
	void insertsAndMovesBlocks();
	void deletesBlocks();
	void editsRoutes();
	void editsRouteSections();
	void editsDependencies();
	void editsRestrictions();
};

void TestBlockRouteFacets::offersBlockAndRouteFacets() {
	const std::vector<std::unique_ptr<InfrastructureFacet>> facets = makeBlockRouteFacets();
	QStringList keys;
	for (const std::unique_ptr<InfrastructureFacet>& facet : facets)
		keys << facet->key();
	// The blocks and routes have four facets.
	QCOMPARE(keys, QStringList({"blocks", "routes", "block_dependencies", "single_track_restrictions"}));

	Fixture f;
	// The panel has the facets of the blocks and routes and no others.
	QVERIFY(f.panel.facet("blocks"));
	QVERIFY(f.panel.facet("routes"));
	QVERIFY(f.panel.facet("block_dependencies"));
	QVERIFY(f.panel.facet("single_track_restrictions"));
	QVERIFY(!f.panel.facet("tracks"));
	QVERIFY(!f.panel.facet("stations"));
	f.choose("blocks");
	f.document.setEditable(false);
	f.panel.refresh();
	f.panel.addEntity();
	f.panel.insertEntity();
	f.panel.moveEntity(1);
	f.panel.deleteEntity();
	f.panel.addSection();
	f.panel.removeSection();
	f.panel.moveSection(1);
	f.type(0, 2, "5");
	// Nothing is edited, added or moved while a run uses the scene.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().blocks.size(), std::size_t(4));
	QCOMPARE_EQ(f.model().blocks[0].lengthKm, 1.0);
}

void TestBlockRouteFacets::listsBlocks() {
	Fixture f;
	f.choose("blocks");
	// Blocks are listed with their extent.
	QCOMPARE(f.headers(), QStringList({"ID", "Track ID", "Length (km)", "Order", "Start km", "End km", "Coverage"}));
	// The blocks of the first track are listed in the order of the model.
	QCOMPARE(f.table()->rowCount(), 2);
	QCOMPARE(f.rowTexts(0), QStringList({"block-a1", "track-a", "1", "1", "0", "1", "complete"}));
	QCOMPARE(f.rowTexts(1), QStringList({"block-a2", "track-a", "1", "2", "1", "2", "complete"}));
	// The order and the extent follow from the model, and cannot be edited.
	QVERIFY(!f.table()->item(0, 3)->flags().testFlag(Qt::ItemIsEditable));
	QVERIFY(!f.table()->item(0, 4)->flags().testFlag(Qt::ItemIsEditable));
	QVERIFY(f.table()->item(0, 2)->flags().testFlag(Qt::ItemIsEditable));
	// Listing blocks edits nothing.
	QVERIFY(f.untouched());
	QVERIFY(!f.document.isDirty());
	f.chooseTrack("track-b");
	// The filter lists the blocks of another track.
	QCOMPARE(f.firstColumn(), QStringList({"block-b1", "block-b2"}));

	// a block without a section has no extent
	f.edit().blocks.push_back(block("block-lost", "track-b"));
	f.edit().nodes.erase(f.edit().nodes.begin() + 3, f.edit().nodes.end());
	f.panel.refresh();
	// A block that has no section shows no extent.
	QCOMPARE(f.rowTexts(0).mid(4), QStringList({"unavailable", "unavailable", "unavailable"}));
}

void TestBlockRouteFacets::editsBlocks() {
	Fixture f;
	f.choose("blocks");
	f.type(0, 2, "2.5");
	// The length of a block is edited.
	QCOMPARE_EQ(f.model().blocks[0].lengthKm, 2.5);
	QCOMPARE(f.cell(0, 2), "2.5");
	QVERIFY(f.changedOnce("Edit block"));
	for (const char* refused : {"abc", "", "inf", "nan"}) {
		f.type(0, 2, refused);
		// A length that is no finite number is refused, without a message, and the table shows the model again.
		QVERIFY2(f.untouched(), refused);
		QCOMPARE_EQ(f.model().blocks[0].lengthKm, 2.5);
		QCOMPARE(f.cell(0, 2), "2.5");
		QVERIFY2(f.messages.empty(), refused);
	}
	f.type(0, 2, " 2.5 ");
	QVERIFY2(f.untouched(), "a length that is the same number changes nothing");

	// the track
	f.type(1, 1, "track-b");
	// The track of a block is edited.
	QCOMPARE(f.model().blocks[1].trackId, "track-b");
	QVERIFY(f.changedOnce("Edit block"));
	// The filter follows the block to its track.
	QCOMPARE(f.panel.blockTrackFilterCombo()->currentData().toString(), "track-b");
	QCOMPARE(f.firstColumn(), QStringList({"block-a2", "block-b1", "block-b2"}));
	QCOMPARE(f.table()->rowCount(), 3);
	f.chooseTrack("track-a");
	f.untouched();
	// The block has left the first track.
	QCOMPARE(f.firstColumn(), QStringList({"block-a1"}));
	f.type(0, 3, "9");
	QVERIFY2(f.untouched(), "the order cannot be edited");

	// rename
	f.type(0, 0, " block-x ");
	const SceneModel& model = f.model();
	const std::string renamed = sectionOf(model, "block-x");
	// The block is renamed.
	QCOMPARE(model.blocks[0].id, "block-x");
	QVERIFY(!renamed.empty());
	// A renamed block keeps its routes, dependencies, restrictions, boundaries and signals.
	QCOMPARE(model.routes[0].blocks[0], "@block-x@");
	QCOMPARE(model.routes[0].blocks[1], sectionOf(model, "block-a2"));
	QCOMPARE(model.blockDependencies[0].block, "@block-x@");
	QCOMPARE(model.singleTrackRestrictions[0].startBlock, "@block-x@");
	QCOMPARE(model.stationBoundaries[0].entranceBlock, "@block-x@");
	QCOMPARE(sceneSignals(f.edit())[0].protectedSection, "@block-x@");
	// A rename leaves the other blocks as they are.
	QCOMPARE(model.blockDependencies[0].dependsOn, sectionOf(model, "block-a2"));
	QCOMPARE(model.singleTrackRestrictions[0].endBlock, sectionOf(model, "block-a2"));
	QCOMPARE(model.singleTrackRestrictions[0].protectedStartBlock, sectionOf(model, "block-b1"));
	QCOMPARE(model.stationBoundaries[0].exitBlock, sectionOf(model, "block-a2"));
	// A rename marks the document once, and the renamed block stays selected.
	QVERIFY(f.changedOnce("Rename block"));
	QCOMPARE(f.panel.selectionId(), "block-x");
	QCOMPARE(f.table()->currentRow(), 0);

	// ids that a block cannot have
	f.type(0, 0, "block-b1");
	// An id that another block has is refused, with a message, and the table shows the model again.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().blocks[0].id, "block-x");
	QCOMPARE(f.cell(0, 0), "block-x");
	QCOMPARE(f.messages, QStringList({"IDs must be unique and not empty"}));
	f.type(0, 0, "  ");
	// An empty id is refused.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().blocks[0].id, "block-x");
	QCOMPARE(f.messages.size(), qsizetype(2));
}

void TestBlockRouteFacets::addsBlocks() {
	Fixture f;
	f.choose("blocks");
	f.panel.addButton()->click();
	const SceneBlock& added = f.model().blocks.back();
	// A block is added to the track of the filter, with a new id, and selected.
	QCOMPARE(f.model().blocks.size(), std::size_t(5));
	QCOMPARE(added.id, "block");
	QCOMPARE(added.trackId, "track-a");
	QCOMPARE_EQ(added.lengthKm, 1.0);
	QVERIFY(f.changedOnce("Add block"));
	QCOMPARE(f.panel.selectionId(), "block");
	QCOMPARE(f.table()->currentRow(), 2);
	QCOMPARE(f.firstColumn(), QStringList({"block-a1", "block-a2", "block"}));
	f.panel.addButton()->click();
	// The id of the next block is new too.
	QCOMPARE(f.model().blocks.size(), std::size_t(6));
	QCOMPARE(f.model().blocks.back().id, "block_2");
	QVERIFY(f.changedOnce("Add block"));

	// a track that the scene does not have
	f.edit().blocks.push_back(block("block-ghost", "ghost"));
	f.panel.refresh();
	f.chooseTrack("ghost");
	f.untouched();
	// A track that the scene lacks has its blocks, but no Add.
	QCOMPARE(f.firstColumn(), QStringList({"block-ghost"}));
	QVERIFY(!f.panel.addButton()->isEnabled());
	f.panel.addEntity();
	// A block is not added to a track that the scene does not have, and the user is told.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().blocks.size(), std::size_t(7));
	QCOMPARE(f.messages, QStringList({"Select a valid track before adding a block"}));
}

void TestBlockRouteFacets::insertsAndMovesBlocks() {
	Fixture f;
	f.choose("blocks");
	// The buttons wait for a selected block.
	QVERIFY(!f.panel.insertBlockButton()->isEnabled());
	QVERIFY(!f.panel.moveBlockUpButton()->isEnabled());
	QVERIFY(!f.panel.moveBlockDownButton()->isEnabled());
	f.panel.insertEntity();
	f.panel.moveEntity(1);
	// Nothing is inserted or moved without a selected block.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().blocks.size(), std::size_t(4));

	// insert
	f.select(1);
	f.panel.insertBlockButton()->click();
	const std::vector<SceneBlock>& blocks = f.model().blocks;
	// A block is inserted before the selected one, on its track.
	QCOMPARE(blocks.size(), std::size_t(5));
	QCOMPARE(blocks[1].id, "block");
	QCOMPARE(blocks[1].trackId, "track-a");
	QCOMPARE_EQ(blocks[1].lengthKm, 1.0);
	QCOMPARE(blocks[2].id, "block-a2");
	// An insert marks the document once, and the new block is selected.
	QVERIFY(f.changedOnce("Insert block"));
	QCOMPARE(f.panel.selectionId(), "block");
	QCOMPARE(f.table()->currentRow(), 1);
	QCOMPARE(f.firstColumn(), QStringList({"block-a1", "block", "block-a2"}));

	// move: the inserted block is second
	// A block in the middle can move both ways.
	QVERIFY(f.panel.moveBlockUpButton()->isEnabled());
	QVERIFY(f.panel.moveBlockDownButton()->isEnabled());
	f.panel.moveBlockDownButton()->click();
	// A block moves down, and stays selected.
	QCOMPARE(blocks[1].id, "block-a2");
	QCOMPARE(blocks[2].id, "block");
	QVERIFY(f.changedOnce("Move block"));
	QCOMPARE(f.panel.selectionId(), "block");
	QCOMPARE(f.table()->currentRow(), 2);
	QCOMPARE(f.firstColumn(), QStringList({"block-a1", "block-a2", "block"}));
	QVERIFY2(!f.panel.moveBlockDownButton()->isEnabled(), "the last block cannot move down");
	f.panel.moveEntity(1);
	// A move past the end changes nothing.
	QVERIFY(f.untouched());
	QCOMPARE(blocks[2].id, "block");
	f.panel.moveBlockUpButton()->click();
	f.panel.moveBlockUpButton()->click();
	// A block moves up.
	QCOMPARE(blocks[0].id, "block");
	QCOMPARE(blocks[1].id, "block-a1");
	QCOMPARE(blocks[2].id, "block-a2");
	QCOMPARE(f.document.lastChangeLabel(), "Move block");
	QCOMPARE(f.table()->currentRow(), 0);
	f.log.clear();
	QVERIFY2(!f.panel.moveBlockUpButton()->isEnabled(), "the first block cannot move up");
	f.panel.moveEntity(-1);
	// A move past the start changes nothing.
	QVERIFY(f.untouched());
	QCOMPARE(blocks[0].id, "block");

	// the other track is not touched
	// The blocks of the other track keep their places.
	QCOMPARE(blocks[3].id, "block-b1");
	QCOMPARE(blocks[4].id, "block-b2");
}

void TestBlockRouteFacets::deletesBlocks() {
	Fixture f;
	f.choose("blocks");
	f.select(0);
	f.panel.deleteButton()->click();
	// A block that others use cannot be deleted.
	QCOMPARE(f.prompts.errorCount, 1);
	QCOMPARE(f.prompts.confirmCount, 0);
	QCOMPARE(f.model().blocks.size(), std::size_t(4));
	QVERIFY(f.untouched());
	const std::string a1 = sectionOf(f.model(), "block-a1");
	// The message names the routes, signals, dependencies, restrictions and boundaries that use the block.
	QCOMPARE(f.prompts.errorMessage,
		blocked("Block", "block-a1",
			{QString::fromStdString("route 'route-1' section '" + a1 + "'"),
				QString::fromStdString("signal 'signal-1' protected section '" + a1 + "'"),
				QString::fromStdString("block dependency '" + a1 + "' -> '" + sectionOf(f.model(), "block-a2") + "'"),
				QString::fromStdString("single-track restriction '" + a1 + "' -> '" + sectionOf(f.model(), "block-a2") + "'"),
				QString::fromStdString("station boundary '" + a1 + "' -> '" + sectionOf(f.model(), "block-a2") + "'")}));

	// a block that nothing uses
	f.panel.addButton()->click();
	f.log.clear();
	// The new block is selected.
	QCOMPARE(f.panel.selectionId(), "block");
	f.panel.deleteButton()->click();
	// Nothing is deleted when the user answers no.
	QCOMPARE(f.prompts.confirmCount, 1);
	QCOMPARE(f.prompts.confirmQuestion, "Delete block 'block'?");
	QCOMPARE(f.model().blocks.size(), std::size_t(5));
	QVERIFY(f.untouched());
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	// A block that nothing uses is deleted when the user answers yes.
	QCOMPARE(f.prompts.confirmCount, 2);
	QCOMPARE(f.model().blocks.size(), std::size_t(4));
	QVERIFY(f.changedOnce("Delete block"));
	QCOMPARE(f.panel.selectionId(), "block-a2");
}

void TestBlockRouteFacets::editsRoutes() {
	Fixture f;
	f.choose("routes");
	// Routes are listed with the number of their sections.
	QCOMPARE(f.headers(), QStringList({"ID", "Sections", "Has corridor", "Corridor", "Reversed"}));
	QCOMPARE(f.table()->rowCount(), 2);
	QCOMPARE(f.rowTexts(0), QStringList({"route-1", "2 sections", "false", "", "false"}));
	QVERIFY2(!f.table()->item(0, 1)->flags().testFlag(Qt::ItemIsEditable), "the number of sections cannot be edited");
	// Listing routes edits nothing.
	QVERIFY(f.untouched());
	QVERIFY(!f.document.isDirty());

	// edit
	f.type(0, 2, "True");
	// The flag of a route is edited.
	QVERIFY(f.model().routes[0].hasCorridor);
	QCOMPARE(f.cell(0, 2), "true");
	QVERIFY(f.changedOnce("Edit route"));
	f.type(0, 3, "north");
	// The corridor of a route is edited.
	QCOMPARE(f.model().routes[0].corridor, "north");
	QVERIFY(f.changedOnce("Edit route"));
	f.type(0, 4, "true");
	// The direction of a route is edited.
	QVERIFY(f.model().routes[0].reversed);
	QVERIFY(f.changedOnce("Edit route"));
	f.type(0, 4, "maybe");
	// A flag that is neither true nor false is refused and the table shows the model again.
	QVERIFY(f.untouched());
	QVERIFY(f.model().routes[0].reversed);
	QCOMPARE(f.cell(0, 4), "true");
	QVERIFY(f.messages.empty());
	f.type(0, 1, "7 sections");
	// The number of sections is no value of the route.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().routes[0].blocks.size(), std::size_t(2));

	// rename: services refer to a route by its id
	f.type(0, 0, " route-x ");
	// A renamed route keeps its services, and stays selected.
	QCOMPARE(f.model().routes[0].id, "route-x");
	QCOMPARE(f.model().services[0].route, "route-x");
	QCOMPARE(f.model().routes[1].id, "route-2");
	QVERIFY(f.changedOnce("Rename route"));
	QCOMPARE(f.panel.selectionId(), "route-x");
	QCOMPARE(f.table()->currentRow(), 0);
	f.type(0, 0, "route-2");
	// An id that another route has is refused.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().routes[0].id, "route-x");
	QCOMPARE(f.messages, QStringList({"IDs must be unique and not empty"}));

	// add
	f.panel.addButton()->click();
	const SceneRoute& added = f.model().routes.back();
	// A route is added with a new id and no sections, and selected.
	QCOMPARE(f.model().routes.size(), std::size_t(3));
	QCOMPARE(added.id, "route");
	QVERIFY(added.blocks.empty());
	QVERIFY(!added.hasCorridor);
	QVERIFY(added.corridor.empty());
	QVERIFY(!added.reversed);
	QVERIFY(f.changedOnce("Add route"));
	QCOMPARE(f.panel.selectionId(), "route");
	QCOMPARE(f.table()->currentRow(), 2);
	QCOMPARE(f.cell(2, 1), "-");

	// delete
	f.select(0);
	f.panel.deleteButton()->click();
	// A route that a service uses cannot be deleted.
	QCOMPARE(f.prompts.errorCount, 1);
	QCOMPARE(f.model().routes.size(), std::size_t(3));
	QVERIFY(f.untouched());
	QCOMPARE(f.prompts.errorMessage, blocked("Route", "route-x", {"service 'service-1'"}));
	f.select(2);
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	// A route that nothing uses is deleted when the user answers yes.
	QCOMPARE(f.prompts.confirmQuestion, "Delete route 'route'?");
	QCOMPARE(f.model().routes.size(), std::size_t(2));
	QVERIFY(f.changedOnce("Delete route"));
}

// The texts of the sections in the list of the panel.
static QStringList sectionTexts(Fixture& f) {
	QStringList result;
	for (int row = 0; row < f.panel.routeSectionListWidget()->count(); ++row)
		result << f.panel.routeSectionListWidget()->item(row)->text();
	return result;
}

void TestBlockRouteFacets::editsRouteSections() {
	Fixture f;
	f.choose("routes");
	f.select(0);
	QListWidget* list = f.panel.routeSectionListWidget();
	QComboBox* catalog = f.panel.routeSectionCatalogCombo();
	const std::string a1 = sectionOf(f.model(), "block-a1");
	const std::string a2 = sectionOf(f.model(), "block-a2");
	const std::string b1 = sectionOf(f.model(), "block-b1");
	// The sections of the selected route and the catalog are shown.
	QVERIFY(!f.panel.routeSectionDetailWidget()->isHidden());
	QCOMPARE(list->count(), 2);
	QCOMPARE(catalog->count(), 4);
	QVERIFY(f.panel.addRouteSectionButton()->isEnabled());

	// add the section that the catalog shows, at the end
	catalog->setCurrentIndex(catalog->findData(QString::fromStdString(b1)));
	f.panel.addRouteSectionButton()->click();
	// A section is added at the end of the route, and selected.
	QCOMPARE(f.model().routes[0].blocks, std::vector<std::string>({a1, a2, b1}));
	QCOMPARE(list->count(), 3);
	QCOMPARE(list->currentRow(), 2);
	QCOMPARE(f.cell(0, 1), "3 sections");
	// A section marks the document once, and tells no more than that the route has changed.
	QCOMPARE(f.log, Log({"documentChanged", "panelChanged"}));
	QCOMPARE(f.kinds, SceneDocument::ChangeKinds(SceneDocument::Infrastructure));
	QCOMPARE(f.document.lastChangeLabel(), "Add route section");
	QCOMPARE(f.panel.selectionId(), "route-1");
	QCOMPARE(f.table()->currentRow(), 0);
	f.log.clear();

	// move
	// The last section can move up only.
	QVERIFY(f.panel.moveRouteSectionUpButton()->isEnabled());
	QVERIFY(!f.panel.moveRouteSectionDownButton()->isEnabled());
	f.panel.moveRouteSectionUpButton()->click();
	// A section moves up, and stays selected.
	QCOMPARE(f.model().routes[0].blocks, std::vector<std::string>({a1, b1, a2}));
	QCOMPARE(list->currentRow(), 1);
	QCOMPARE(f.document.lastChangeLabel(), "Move route section");
	QCOMPARE(f.log.size(), std::size_t(2));
	f.log.clear();
	f.panel.moveRouteSectionDownButton()->click();
	// A section moves down, and stays selected.
	QCOMPARE(f.model().routes[0].blocks, std::vector<std::string>({a1, a2, b1}));
	QCOMPARE(list->currentRow(), 2);
	QCOMPARE(f.log.size(), std::size_t(2));
	f.log.clear();
	f.panel.moveSection(1);
	list->setCurrentRow(0);
	f.panel.moveSection(-1);
	// A move past either end changes nothing.
	QVERIFY(f.untouched());
	QCOMPARE(f.model().routes[0].blocks, std::vector<std::string>({a1, a2, b1}));

	// remove: the section at the start
	f.panel.removeRouteSectionButton()->click();
	// A section is removed, and the one at its place is selected.
	QCOMPARE(f.model().routes[0].blocks, std::vector<std::string>({a2, b1}));
	QCOMPARE(list->count(), 2);
	QCOMPARE(list->currentRow(), 0);
	QCOMPARE(f.document.lastChangeLabel(), "Remove route section");
	QCOMPARE(f.cell(0, 1), "2 sections");
	list->setCurrentRow(1);
	f.panel.removeRouteSectionButton()->click();
	// The last section is removed, and the one before it is selected.
	QCOMPARE(f.model().routes[0].blocks, std::vector<std::string>({a2}));
	QCOMPARE(list->currentRow(), 0);
	f.panel.removeRouteSectionButton()->click();
	// A route without sections lists none.
	QVERIFY(f.model().routes[0].blocks.empty());
	QCOMPARE(list->count(), 0);
	QVERIFY(!f.panel.removeRouteSectionButton()->isEnabled());
	QCOMPARE(f.cell(0, 1), "-");
	f.log.clear();
	f.panel.removeSection();
	QVERIFY2(f.untouched(), "there is nothing to remove from a route without sections");

	// the catalog keeps the section that was chosen
	f.panel.addRouteSectionButton()->click();
	// The catalog keeps the section that was chosen, and the list names it by its label.
	QCOMPARE(f.model().routes[0].blocks, std::vector<std::string>({b1}));
	QCOMPARE(sectionTexts(f), QStringList({"track-b 0.000 to 1.000 km"}));

	// the buttons wait for a route
	f.edit().routes.clear();
	f.panel.refresh();
	f.log.clear();
	f.panel.addSection();
	// Sections cannot be added without a route.
	QVERIFY(f.untouched());
	QVERIFY(!f.panel.addRouteSectionButton()->isEnabled());
	QVERIFY(!f.panel.removeRouteSectionButton()->isEnabled());
}

void TestBlockRouteFacets::editsDependencies() {
	Fixture f;
	f.choose("block_dependencies");
	const std::string a1 = sectionOf(f.model(), "block-a1");
	const std::string a2 = sectionOf(f.model(), "block-a2");
	// Dependencies are listed with their two blocks.
	QCOMPARE(f.headers(), QStringList({"Block", "Depends on"}));
	QCOMPARE(f.table()->rowCount(), 1);
	// The blocks of a dependency are chosen in combos of the sections.
	QVERIFY(f.combo(0, 0));
	QVERIFY(f.combo(0, 1));
	QCOMPARE(f.combo(0, 0)->objectName(), "dependencyBlockCombo");
	QCOMPARE(f.combo(0, 1)->objectName(), "dependencyDependsOnCombo");
	QCOMPARE(f.chosen(0, 0).toStdString(), a1);
	QCOMPARE(f.chosen(0, 1).toStdString(), a2);
	// Listing dependencies edits nothing.
	QVERIFY(f.untouched());
	QVERIFY(!f.document.isDirty());

	// edit: the combo is the widget that is edited, so the table keeps it
	QComboBox* first = f.combo(0, 0);
	f.choose(0, 0, "block-b1");
	// Another block is chosen, which marks the document once and leaves the combo in the table.
	QCOMPARE(f.model().blockDependencies[0].block, sectionOf(f.model(), "block-b1"));
	QCOMPARE(f.model().blockDependencies[0].dependsOn, a2);
	QVERIFY(f.changedOnce("Edit block dependency"));
	QCOMPARE(f.combo(0, 0), first);
	f.choose(0, 1, "block-a1");
	// The block it depends on is chosen.
	QCOMPARE(f.model().blockDependencies[0].dependsOn, a1);
	QVERIFY(f.changedOnce("Edit block dependency"));
	f.choose(0, 1, "block-a1");
	QVERIFY2(f.untouched(), "choosing the block that is chosen changes nothing");

	// add
	f.panel.addButton()->click();
	// A dependency is added empty, and its row is selected.
	QCOMPARE(f.model().blockDependencies.size(), std::size_t(2));
	QVERIFY(f.model().blockDependencies[1].block.empty());
	QVERIFY(f.model().blockDependencies[1].dependsOn.empty());
	QVERIFY(f.changedOnce("Add block dependency"));
	QCOMPARE(f.table()->currentRow(), 1);
	QVERIFY(f.panel.selectionId().isEmpty());
	// An empty block shows as none.
	QCOMPARE(f.combo(1, 0)->currentText(), "(none)");
	QVERIFY(f.chosen(1, 0).isEmpty());

	// delete: nothing refers to a dependency
	f.prompts.confirmAnswer = false;
	f.panel.deleteButton()->click();
	// A dependency that has no block yet is named an incomplete row, and stays when the user answers no.
	QCOMPARE(f.prompts.errorCount, 0);
	QCOMPARE(f.prompts.confirmQuestion, "Delete block dependency 'incomplete row'?");
	QCOMPARE(f.model().blockDependencies.size(), std::size_t(2));
	QVERIFY(f.untouched());
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	// A dependency is deleted when the user answers yes.
	QCOMPARE(f.model().blockDependencies.size(), std::size_t(1));
	QVERIFY(f.changedOnce("Delete block dependency"));
	f.select(0);
	f.panel.deleteButton()->click();
	// A dependency is named by its block.
	QCOMPARE(f.prompts.confirmQuestion, QString::fromStdString("Delete block dependency '" + sectionOf(f.model(), "block-b1") + "'?"));
	QVERIFY(f.model().blockDependencies.empty());
}

void TestBlockRouteFacets::editsRestrictions() {
	Fixture f;
	f.choose("single_track_restrictions");
	// Restrictions are listed with their four blocks.
	QCOMPARE(f.headers(), QStringList({"Start", "End", "Protected start", "Protected end"}));
	QCOMPARE(f.table()->rowCount(), 1);
	const char* names[] = {"restrictionStartCombo", "restrictionEndCombo", "restrictionProtectedStartCombo", "restrictionProtectedEndCombo"};
	const char* blocks[] = {"block-a1", "block-a2", "block-b1", "block-b2"};
	for (int column = 0; column < 4; ++column) {
		// Each block of a restriction is chosen in a combo of the sections.
		QVERIFY(f.combo(0, column));
		QCOMPARE(f.combo(0, column)->objectName(), names[column]);
		QCOMPARE(f.chosen(0, column).toStdString(), sectionOf(f.model(), blocks[column]));
	}
	QVERIFY2(f.untouched(), "listing restrictions edits nothing");

	// edit each of the four
	const SceneSingleTrackRestriction& restriction = f.model().singleTrackRestrictions[0];
	f.choose(0, 0, "block-b2");
	// The start is chosen.
	QCOMPARE(restriction.startBlock, sectionOf(f.model(), "block-b2"));
	QVERIFY(f.changedOnce("Edit single-track restriction"));
	f.choose(0, 1, "block-b1");
	// The end is chosen.
	QCOMPARE(restriction.endBlock, sectionOf(f.model(), "block-b1"));
	QVERIFY(f.changedOnce("Edit single-track restriction"));
	f.choose(0, 2, "block-a2");
	// The protected start is chosen.
	QCOMPARE(restriction.protectedStartBlock, sectionOf(f.model(), "block-a2"));
	QVERIFY(f.changedOnce("Edit single-track restriction"));
	f.choose(0, 3, "block-a1");
	// The protected end is chosen.
	QCOMPARE(restriction.protectedEndBlock, sectionOf(f.model(), "block-a1"));
	QVERIFY(f.changedOnce("Edit single-track restriction"));
	// Each combo writes its own block.
	QCOMPARE(restriction.startBlock, sectionOf(f.model(), "block-b2"));
	QCOMPARE(restriction.endBlock, sectionOf(f.model(), "block-b1"));

	// add and delete
	f.panel.addButton()->click();
	// A restriction is added empty, and its row is selected.
	QCOMPARE(f.model().singleTrackRestrictions.size(), std::size_t(2));
	QVERIFY(f.model().singleTrackRestrictions[1].startBlock.empty());
	QVERIFY(f.changedOnce("Add single-track restriction"));
	QCOMPARE(f.table()->currentRow(), 1);
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	// A restriction is deleted when the user answers yes.
	QCOMPARE(f.prompts.confirmQuestion, "Delete single-track restriction 'incomplete row'?");
	QCOMPARE(f.model().singleTrackRestrictions.size(), std::size_t(1));
	QVERIFY(f.changedOnce("Delete single-track restriction"));
	f.select(0);
	f.panel.deleteButton()->click();
	// A restriction is named by its start.
	QCOMPARE(f.prompts.confirmQuestion, QString::fromStdString("Delete single-track restriction '" + sectionOf(f.model(), "block-b2") + "'?"));
	QVERIFY(f.model().singleTrackRestrictions.empty());
}

QTEST_MAIN(TestBlockRouteFacets)
#include "test_editor_infrastructure_blocks.moc"
