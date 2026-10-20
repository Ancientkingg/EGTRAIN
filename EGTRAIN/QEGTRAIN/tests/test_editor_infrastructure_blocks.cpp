#include "editors/InfrastructurePanel.h"
#include "editors/infrastructure/BlockRouteFacets.h"

#include "scene/SectionInventory.h"

#include <QApplication>
#include <QComboBox>
#include <QListWidget>
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

// A panel with the facets of the blocks and routes, over a document that holds the scene. The events are the order in
// which the panel and its document report an edit, and the messages that the panel shows in the status line.
struct Fixture {
	SceneDocument document;
	InfrastructurePanel panel;
	FakePrompts prompts;
	Log log;
	QStringList entities;
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
		QObject::connect(&panel, &InfrastructurePanel::entitiesChanged, &panel, [this](const QString& facet) {
			log.push_back("entitiesChanged");
			entities << facet;
		});
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
	const std::vector<std::unique_ptr<InfrastructureFacet>> facets = makeBlockRouteFacets();
	QStringList keys;
	for (const std::unique_ptr<InfrastructureFacet>& facet : facets)
		keys << facet->key();
	ok &= expect(keys == QStringList({"blocks", "routes", "block_dependencies", "single_track_restrictions"}),
		"the blocks and routes have four facets");

	Fixture f;
	ok &= expect(f.panel.facet("blocks") && f.panel.facet("routes") && f.panel.facet("block_dependencies")
			&& f.panel.facet("single_track_restrictions") && !f.panel.facet("tracks") && !f.panel.facet("stations"),
		"the panel has the facets of the blocks and routes and no others");
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
	ok &= expect(f.untouched() && f.model().blocks.size() == 4 && f.model().blocks[0].lengthKm == 1.0,
		"nothing is edited, added or moved while a run uses the scene");
	return ok;
}

static bool testBlockList() {
	bool ok = true;
	Fixture f;
	f.choose("blocks");
	ok &= expect(f.headers() == QStringList({"ID", "Track ID", "Length (km)", "Order", "Start km", "End km", "Coverage"}),
		"blocks are listed with their extent");
	ok &= expect(f.table()->rowCount() == 2 && f.rowTexts(0) == QStringList({"block-a1", "track-a", "1", "1", "0", "1", "complete"})
			&& f.rowTexts(1) == QStringList({"block-a2", "track-a", "1", "2", "1", "2", "complete"}),
		"the blocks of the first track are listed in the order of the model");
	ok &= expect(!f.table()->item(0, 3)->flags().testFlag(Qt::ItemIsEditable) && !f.table()->item(0, 4)->flags().testFlag(Qt::ItemIsEditable)
			&& f.table()->item(0, 2)->flags().testFlag(Qt::ItemIsEditable),
		"the order and the extent follow from the model, and cannot be edited");
	ok &= expect(f.untouched() && !f.document.isDirty(), "listing blocks edits nothing");
	f.chooseTrack("track-b");
	ok &= expect(f.firstColumn() == QStringList({"block-b1", "block-b2"}), "the filter lists the blocks of another track");

	// a block without a section has no extent
	f.edit().blocks.push_back(block("block-lost", "track-b"));
	f.edit().nodes.erase(f.edit().nodes.begin() + 3, f.edit().nodes.end());
	f.panel.refresh();
	ok &= expect(f.rowTexts(0).mid(4) == QStringList({"unavailable", "unavailable", "unavailable"}),
		"a block that has no section shows no extent");
	return ok;
}

static bool testBlockEdit() {
	bool ok = true;
	Fixture f;
	f.choose("blocks");
	f.type(0, 2, "2.5");
	ok &= expect(f.model().blocks[0].lengthKm == 2.5 && f.cell(0, 2) == "2.5" && f.changedOnce("Edit block", "blocks"),
		"the length of a block is edited");
	for (const char* refused : {"abc", "", "inf", "nan"}) {
		f.type(0, 2, refused);
		ok &= expect(f.untouched() && f.model().blocks[0].lengthKm == 2.5 && f.cell(0, 2) == "2.5" && f.messages.empty(),
			"a length that is no finite number is refused, without a message, and the table shows the model again");
	}
	f.type(0, 2, " 2.5 ");
	ok &= expect(f.untouched(), "a length that is the same number changes nothing");

	// the track
	f.type(1, 1, "track-b");
	ok &= expect(f.model().blocks[1].trackId == "track-b" && f.changedOnce("Edit block", "blocks"), "the track of a block is edited");
	ok &= expect(f.panel.blockTrackFilterCombo()->currentData().toString() == "track-b" && f.firstColumn() == QStringList({"block-a2", "block-b1", "block-b2"})
			&& f.table()->rowCount() == 3,
		"the filter follows the block to its track");
	f.chooseTrack("track-a");
	f.untouched();
	ok &= expect(f.firstColumn() == QStringList({"block-a1"}), "the block has left the first track");
	f.type(0, 3, "9");
	ok &= expect(f.untouched(), "the order cannot be edited");

	// rename
	f.type(0, 0, " block-x ");
	const SceneModel& model = f.model();
	const std::string renamed = sectionOf(model, "block-x");
	ok &= expect(model.blocks[0].id == "block-x" && !renamed.empty(), "the block is renamed");
	ok &= expect(model.routes[0].blocks[0] == "@block-x@" && model.routes[0].blocks[1] == sectionOf(model, "block-a2")
			&& model.blockDependencies[0].block == "@block-x@" && model.singleTrackRestrictions[0].startBlock == "@block-x@"
			&& model.stationBoundaries[0].entranceBlock == "@block-x@" && sceneSignals(f.edit())[0].protectedSection == "@block-x@",
		"a renamed block keeps its routes, dependencies, restrictions, boundaries and signals");
	ok &= expect(model.blockDependencies[0].dependsOn == sectionOf(model, "block-a2") && model.singleTrackRestrictions[0].endBlock == sectionOf(model, "block-a2")
			&& model.singleTrackRestrictions[0].protectedStartBlock == sectionOf(model, "block-b1")
			&& model.stationBoundaries[0].exitBlock == sectionOf(model, "block-a2"),
		"a rename leaves the other blocks as they are");
	ok &= expect(f.changedOnce("Rename block", "blocks") && f.panel.selectionId() == "block-x" && f.table()->currentRow() == 0,
		"a rename marks the document once, and the renamed block stays selected");

	// ids that a block cannot have
	f.type(0, 0, "block-b1");
	ok &= expect(f.untouched() && f.model().blocks[0].id == "block-x" && f.cell(0, 0) == "block-x"
			&& f.messages == QStringList({"Infrastructure IDs must be non-empty and unique"}),
		"an id that another block has is refused, with a message, and the table shows the model again");
	f.type(0, 0, "  ");
	ok &= expect(f.untouched() && f.model().blocks[0].id == "block-x" && f.messages.size() == 2, "an empty id is refused");
	return ok;
}

static bool testBlockAdd() {
	bool ok = true;
	Fixture f;
	f.choose("blocks");
	f.panel.addButton()->click();
	const SceneBlock& added = f.model().blocks.back();
	ok &= expect(f.model().blocks.size() == 5 && added.id == "block" && added.trackId == "track-a" && added.lengthKm == 1.0
			&& f.changedOnce("Add block", "blocks") && f.panel.selectionId() == "block" && f.table()->currentRow() == 2
			&& f.firstColumn() == QStringList({"block-a1", "block-a2", "block"}),
		"a block is added to the track of the filter, with a new id, and selected");
	f.panel.addButton()->click();
	ok &= expect(f.model().blocks.size() == 6 && f.model().blocks.back().id == "block_2" && f.changedOnce("Add block", "blocks"),
		"the id of the next block is new too");

	// a track that the scene does not have
	f.edit().blocks.push_back(block("block-ghost", "ghost"));
	f.panel.refresh();
	f.chooseTrack("ghost");
	f.untouched();
	ok &= expect(f.firstColumn() == QStringList({"block-ghost"}) && !f.panel.addButton()->isEnabled(), "a track that the scene lacks has its blocks, but no Add");
	f.panel.addEntity();
	ok &= expect(f.untouched() && f.model().blocks.size() == 7 && f.messages == QStringList({"Select a valid track before adding a block"}),
		"a block is not added to a track that the scene does not have, and the user is told");
	return ok;
}

static bool testBlockInsertAndMove() {
	bool ok = true;
	Fixture f;
	f.choose("blocks");
	ok &= expect(!f.panel.insertBlockButton()->isEnabled() && !f.panel.moveBlockUpButton()->isEnabled() && !f.panel.moveBlockDownButton()->isEnabled(),
		"the buttons wait for a selected block");
	f.panel.insertEntity();
	f.panel.moveEntity(1);
	ok &= expect(f.untouched() && f.model().blocks.size() == 4, "nothing is inserted or moved without a selected block");

	// insert
	f.select(1);
	f.panel.insertBlockButton()->click();
	const std::vector<SceneBlock>& blocks = f.model().blocks;
	ok &= expect(blocks.size() == 5 && blocks[1].id == "block" && blocks[1].trackId == "track-a" && blocks[1].lengthKm == 1.0 && blocks[2].id == "block-a2",
		"a block is inserted before the selected one, on its track");
	ok &= expect(f.changedOnce("Insert block", "blocks") && f.panel.selectionId() == "block" && f.table()->currentRow() == 1
			&& f.firstColumn() == QStringList({"block-a1", "block", "block-a2"}),
		"an insert marks the document once, and the new block is selected");

	// move: the inserted block is second
	ok &= expect(f.panel.moveBlockUpButton()->isEnabled() && f.panel.moveBlockDownButton()->isEnabled(), "a block in the middle can move both ways");
	f.panel.moveBlockDownButton()->click();
	ok &= expect(blocks[1].id == "block-a2" && blocks[2].id == "block" && f.changedOnce("Move block", "blocks") && f.panel.selectionId() == "block"
			&& f.table()->currentRow() == 2 && f.firstColumn() == QStringList({"block-a1", "block-a2", "block"}),
		"a block moves down, and stays selected");
	ok &= expect(!f.panel.moveBlockDownButton()->isEnabled(), "the last block cannot move down");
	f.panel.moveEntity(1);
	ok &= expect(f.untouched() && blocks[2].id == "block", "a move past the end changes nothing");
	f.panel.moveBlockUpButton()->click();
	f.panel.moveBlockUpButton()->click();
	ok &= expect(blocks[0].id == "block" && blocks[1].id == "block-a1" && blocks[2].id == "block-a2" && f.document.lastChangeLabel() == "Move block"
			&& f.table()->currentRow() == 0,
		"a block moves up");
	f.log.clear();
	f.entities.clear();
	ok &= expect(!f.panel.moveBlockUpButton()->isEnabled(), "the first block cannot move up");
	f.panel.moveEntity(-1);
	ok &= expect(f.untouched() && blocks[0].id == "block", "a move past the start changes nothing");

	// the other track is not touched
	ok &= expect(blocks[3].id == "block-b1" && blocks[4].id == "block-b2", "the blocks of the other track keep their places");
	return ok;
}

static bool testBlockDelete() {
	bool ok = true;
	Fixture f;
	f.choose("blocks");
	f.select(0);
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.errorCount == 1 && f.prompts.confirmCount == 0 && f.model().blocks.size() == 4 && f.untouched(),
		"a block that others use cannot be deleted");
	const std::string a1 = sectionOf(f.model(), "block-a1");
	ok &= expect(f.prompts.errorMessage
			== blocked("blocks", "block-a1",
				{QString::fromStdString("route 'route-1' section '" + a1 + "'"),
					QString::fromStdString("signal 'signal-1' protected section '" + a1 + "'"),
					QString::fromStdString("block dependency '" + a1 + "' -> '" + sectionOf(f.model(), "block-a2") + "'"),
					QString::fromStdString("single-track restriction '" + a1 + "' -> '" + sectionOf(f.model(), "block-a2") + "'"),
					QString::fromStdString("station boundary '" + a1 + "' -> '" + sectionOf(f.model(), "block-a2") + "'")}),
		"the message names the routes, signals, dependencies, restrictions and boundaries that use the block");

	// a block that nothing uses
	f.panel.addButton()->click();
	f.log.clear();
	f.entities.clear();
	ok &= expect(f.panel.selectionId() == "block", "the new block is selected");
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.confirmCount == 1 && f.prompts.confirmQuestion == "Delete blocks 'block'?" && f.model().blocks.size() == 5 && f.untouched(),
		"nothing is deleted when the user answers no");
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.confirmCount == 2 && f.model().blocks.size() == 4 && f.changedOnce("Delete block", "blocks") && f.panel.selectionId() == "block-a2",
		"a block that nothing uses is deleted when the user answers yes");
	return ok;
}

static bool testRoutes() {
	bool ok = true;
	Fixture f;
	f.choose("routes");
	ok &= expect(f.headers() == QStringList({"ID", "Sections", "Has corridor", "Corridor", "Reversed"}) && f.table()->rowCount() == 2
			&& f.rowTexts(0) == QStringList({"route-1", "2 section(s)", "false", "", "false"}),
		"routes are listed with the number of their sections");
	ok &= expect(!f.table()->item(0, 1)->flags().testFlag(Qt::ItemIsEditable), "the number of sections cannot be edited");
	ok &= expect(f.untouched() && !f.document.isDirty(), "listing routes edits nothing");

	// edit
	f.type(0, 2, "True");
	ok &= expect(f.model().routes[0].hasCorridor && f.cell(0, 2) == "true" && f.changedOnce("Edit route", "routes"), "the flag of a route is edited");
	f.type(0, 3, "north");
	ok &= expect(f.model().routes[0].corridor == "north" && f.changedOnce("Edit route", "routes"), "the corridor of a route is edited");
	f.type(0, 4, "true");
	ok &= expect(f.model().routes[0].reversed && f.changedOnce("Edit route", "routes"), "the direction of a route is edited");
	f.type(0, 4, "maybe");
	ok &= expect(f.untouched() && f.model().routes[0].reversed && f.cell(0, 4) == "true" && f.messages.empty(),
		"a flag that is neither true nor false is refused and the table shows the model again");
	f.type(0, 1, "7 sections");
	ok &= expect(f.untouched() && f.model().routes[0].blocks.size() == 2, "the number of sections is no value of the route");

	// rename: services refer to a route by its id
	f.type(0, 0, " route-x ");
	ok &= expect(f.model().routes[0].id == "route-x" && f.model().services[0].route == "route-x" && f.model().routes[1].id == "route-2"
			&& f.changedOnce("Rename route", "routes") && f.panel.selectionId() == "route-x" && f.table()->currentRow() == 0,
		"a renamed route keeps its services, and stays selected");
	f.type(0, 0, "route-2");
	ok &= expect(f.untouched() && f.model().routes[0].id == "route-x" && f.messages == QStringList({"Infrastructure IDs must be non-empty and unique"}),
		"an id that another route has is refused");

	// add
	f.panel.addButton()->click();
	const SceneRoute& added = f.model().routes.back();
	ok &= expect(f.model().routes.size() == 3 && added.id == "route" && added.blocks.empty() && !added.hasCorridor && added.corridor.empty()
			&& !added.reversed && f.changedOnce("Add route", "routes") && f.panel.selectionId() == "route" && f.table()->currentRow() == 2
			&& f.cell(2, 1) == "0 sections",
		"a route is added with a new id and no sections, and selected");

	// delete
	f.select(0);
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.errorCount == 1 && f.model().routes.size() == 3 && f.untouched()
			&& f.prompts.errorMessage == blocked("routes", "route-x", {"service 'service-1'"}),
		"a route that a service uses cannot be deleted");
	f.select(2);
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.confirmQuestion == "Delete routes 'route'?" && f.model().routes.size() == 2 && f.changedOnce("Delete route", "routes"),
		"a route that nothing uses is deleted when the user answers yes");
	return ok;
}

// The texts of the sections in the list of the panel.
static QStringList sectionTexts(Fixture& f) {
	QStringList result;
	for (int row = 0; row < f.panel.routeSectionListWidget()->count(); ++row)
		result << f.panel.routeSectionListWidget()->item(row)->text();
	return result;
}

static bool testRouteSections() {
	bool ok = true;
	Fixture f;
	f.choose("routes");
	f.select(0);
	QListWidget* list = f.panel.routeSectionListWidget();
	QComboBox* catalog = f.panel.routeSectionCatalogCombo();
	const std::string a1 = sectionOf(f.model(), "block-a1");
	const std::string a2 = sectionOf(f.model(), "block-a2");
	const std::string b1 = sectionOf(f.model(), "block-b1");
	ok &= expect(!f.panel.routeSectionDetailWidget()->isHidden() && list->count() == 2 && catalog->count() == 4 && f.panel.addRouteSectionButton()->isEnabled(),
		"the sections of the selected route and the catalog are shown");

	// add the section that the catalog shows, at the end
	catalog->setCurrentIndex(catalog->findData(QString::fromStdString(b1)));
	f.panel.addRouteSectionButton()->click();
	ok &= expect(f.model().routes[0].blocks == std::vector<std::string>({a1, a2, b1}) && list->count() == 3 && list->currentRow() == 2
			&& f.cell(0, 1) == "3 section(s)",
		"a section is added at the end of the route, and selected");
	ok &= expect(f.log == Log({"documentChanged", "panelChanged"}) && f.entities.isEmpty() && f.kinds == SceneDocument::Infrastructure
			&& f.document.lastChangeLabel() == "Add route section" && f.panel.selectionId() == "route-1" && f.table()->currentRow() == 0,
		"a section marks the document once, and tells no more than that the route has changed");
	f.log.clear();

	// move
	ok &= expect(f.panel.moveRouteSectionUpButton()->isEnabled() && !f.panel.moveRouteSectionDownButton()->isEnabled(), "the last section can move up only");
	f.panel.moveRouteSectionUpButton()->click();
	ok &= expect(f.model().routes[0].blocks == std::vector<std::string>({a1, b1, a2}) && list->currentRow() == 1
			&& f.document.lastChangeLabel() == "Move route section" && f.log.size() == 2,
		"a section moves up, and stays selected");
	f.log.clear();
	f.panel.moveRouteSectionDownButton()->click();
	ok &= expect(f.model().routes[0].blocks == std::vector<std::string>({a1, a2, b1}) && list->currentRow() == 2 && f.log.size() == 2,
		"a section moves down, and stays selected");
	f.log.clear();
	f.panel.moveSection(1);
	list->setCurrentRow(0);
	f.panel.moveSection(-1);
	ok &= expect(f.untouched() && f.model().routes[0].blocks == std::vector<std::string>({a1, a2, b1}), "a move past either end changes nothing");

	// remove: the section at the start
	f.panel.removeRouteSectionButton()->click();
	ok &= expect(f.model().routes[0].blocks == std::vector<std::string>({a2, b1}) && list->count() == 2 && list->currentRow() == 0
			&& f.document.lastChangeLabel() == "Remove route section" && f.cell(0, 1) == "2 section(s)",
		"a section is removed, and the one at its place is selected");
	list->setCurrentRow(1);
	f.panel.removeRouteSectionButton()->click();
	ok &= expect(f.model().routes[0].blocks == std::vector<std::string>({a2}) && list->currentRow() == 0, "the last section is removed, and the one before it is selected");
	f.panel.removeRouteSectionButton()->click();
	ok &= expect(f.model().routes[0].blocks.empty() && list->count() == 0 && !f.panel.removeRouteSectionButton()->isEnabled() && f.cell(0, 1) == "0 sections",
		"a route without sections lists none");
	f.log.clear();
	f.panel.removeSection();
	ok &= expect(f.untouched(), "there is nothing to remove from a route without sections");

	// the catalog keeps the section that was chosen
	f.panel.addRouteSectionButton()->click();
	ok &= expect(f.model().routes[0].blocks == std::vector<std::string>({b1}) && sectionTexts(f) == QStringList({"base block block-b1 / track track-b"}),
		"the catalog keeps the section that was chosen, and the list names it by its block and track");

	// the buttons wait for a route
	f.edit().routes.clear();
	f.panel.refresh();
	f.log.clear();
	f.panel.addSection();
	ok &= expect(f.untouched() && !f.panel.addRouteSectionButton()->isEnabled() && !f.panel.removeRouteSectionButton()->isEnabled(), "sections cannot be added without a route");
	return ok;
}

static bool testDependencies() {
	bool ok = true;
	Fixture f;
	f.choose("block_dependencies");
	const std::string a1 = sectionOf(f.model(), "block-a1");
	const std::string a2 = sectionOf(f.model(), "block-a2");
	ok &= expect(f.headers() == QStringList({"Block", "Depends on"}) && f.table()->rowCount() == 1, "dependencies are listed with their two blocks");
	ok &= expect(f.combo(0, 0) && f.combo(0, 1) && f.combo(0, 0)->objectName() == "dependencyBlockCombo" && f.combo(0, 1)->objectName() == "dependencyDependsOnCombo"
			&& f.chosen(0, 0).toStdString() == a1 && f.chosen(0, 1).toStdString() == a2,
		"the blocks of a dependency are chosen in combos of the sections");
	ok &= expect(f.untouched() && !f.document.isDirty(), "listing dependencies edits nothing");

	// edit: the combo is the widget that is edited, so the table keeps it
	QComboBox* first = f.combo(0, 0);
	f.choose(0, 0, "block-b1");
	ok &= expect(f.model().blockDependencies[0].block == sectionOf(f.model(), "block-b1") && f.model().blockDependencies[0].dependsOn == a2
			&& f.editedOnce("Edit block dependency") && f.combo(0, 0) == first,
		"another block is chosen, which marks the document once and leaves the combo in the table");
	f.choose(0, 1, "block-a1");
	ok &= expect(f.model().blockDependencies[0].dependsOn == a1 && f.editedOnce("Edit block dependency"), "the block it depends on is chosen");
	f.choose(0, 1, "block-a1");
	ok &= expect(f.untouched(), "choosing the block that is chosen changes nothing");

	// add
	f.panel.addButton()->click();
	ok &= expect(f.model().blockDependencies.size() == 2 && f.model().blockDependencies[1].block.empty() && f.model().blockDependencies[1].dependsOn.empty()
			&& f.changedOnce("Add block dependency", "block_dependencies") && f.table()->currentRow() == 1 && f.panel.selectionId().isEmpty(),
		"a dependency is added empty, and its row is selected");
	ok &= expect(f.combo(1, 0)->currentText() == "Invalid: (empty)" && f.chosen(1, 0).isEmpty(), "an empty block shows as invalid");

	// delete: nothing refers to a dependency
	f.prompts.confirmAnswer = false;
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.errorCount == 0 && f.prompts.confirmQuestion == "Delete block dependencies 'incomplete row'?" && f.model().blockDependencies.size() == 2 && f.untouched(),
		"a dependency that has no block yet is named an incomplete row, and stays when the user answers no");
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	ok &= expect(f.model().blockDependencies.size() == 1 && f.changedOnce("Delete block dependency", "block_dependencies"), "a dependency is deleted when the user answers yes");
	f.select(0);
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.confirmQuestion == QString::fromStdString("Delete block dependencies '" + sectionOf(f.model(), "block-b1") + "'?")
			&& f.model().blockDependencies.empty(),
		"a dependency is named by its block");
	return ok;
}

static bool testRestrictions() {
	bool ok = true;
	Fixture f;
	f.choose("single_track_restrictions");
	ok &= expect(f.headers() == QStringList({"Start", "End", "Protected start", "Protected end"}) && f.table()->rowCount() == 1, "restrictions are listed with their four blocks");
	const char* names[] = {"restrictionStartCombo", "restrictionEndCombo", "restrictionProtectedStartCombo", "restrictionProtectedEndCombo"};
	const char* blocks[] = {"block-a1", "block-a2", "block-b1", "block-b2"};
	for (int column = 0; column < 4; ++column)
		ok &= expect(f.combo(0, column) && f.combo(0, column)->objectName() == names[column]
				&& f.chosen(0, column).toStdString() == sectionOf(f.model(), blocks[column]),
			"each block of a restriction is chosen in a combo of the sections");
	ok &= expect(f.untouched(), "listing restrictions edits nothing");

	// edit each of the four
	const SceneSingleTrackRestriction& restriction = f.model().singleTrackRestrictions[0];
	f.choose(0, 0, "block-b2");
	ok &= expect(restriction.startBlock == sectionOf(f.model(), "block-b2") && f.editedOnce("Edit single-track restriction"), "the start is chosen");
	f.choose(0, 1, "block-b1");
	ok &= expect(restriction.endBlock == sectionOf(f.model(), "block-b1") && f.editedOnce("Edit single-track restriction"), "the end is chosen");
	f.choose(0, 2, "block-a2");
	ok &= expect(restriction.protectedStartBlock == sectionOf(f.model(), "block-a2") && f.editedOnce("Edit single-track restriction"), "the protected start is chosen");
	f.choose(0, 3, "block-a1");
	ok &= expect(restriction.protectedEndBlock == sectionOf(f.model(), "block-a1") && f.editedOnce("Edit single-track restriction"), "the protected end is chosen");
	ok &= expect(restriction.startBlock == sectionOf(f.model(), "block-b2") && restriction.endBlock == sectionOf(f.model(), "block-b1"), "each combo writes its own block");

	// add and delete
	f.panel.addButton()->click();
	ok &= expect(f.model().singleTrackRestrictions.size() == 2 && f.model().singleTrackRestrictions[1].startBlock.empty()
			&& f.changedOnce("Add single-track restriction", "single_track_restrictions") && f.table()->currentRow() == 1,
		"a restriction is added empty, and its row is selected");
	f.prompts.confirmAnswer = true;
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.confirmQuestion == "Delete single-track restrictions 'incomplete row'?" && f.model().singleTrackRestrictions.size() == 1
			&& f.changedOnce("Delete single-track restriction", "single_track_restrictions"),
		"a restriction is deleted when the user answers yes");
	f.select(0);
	f.panel.deleteButton()->click();
	ok &= expect(f.prompts.confirmQuestion == QString::fromStdString("Delete single-track restrictions '" + sectionOf(f.model(), "block-b2") + "'?")
			&& f.model().singleTrackRestrictions.empty(),
		"a restriction is named by its start");
	return ok;
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;
	ok &= testFacets();
	ok &= testBlockList();
	ok &= testBlockEdit();
	ok &= testBlockAdd();
	ok &= testBlockInsertAndMove();
	ok &= testBlockDelete();
	ok &= testRoutes();
	ok &= testRouteSections();
	ok &= testDependencies();
	ok &= testRestrictions();
	return ok ? 0 : 1;
}
