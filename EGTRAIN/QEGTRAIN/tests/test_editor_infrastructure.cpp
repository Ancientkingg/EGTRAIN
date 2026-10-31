#include "editors/InfrastructurePanel.h"
#include "editors/infrastructure/BlockRouteFacets.h"
#include "editors/infrastructure/SignalFacets.h"

#include "scene/SectionInventory.h"
#include "scene/SignallingLevel.h"

#include "support/FakePrompts.h"

#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSplitter>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTest>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Two tracks with three nodes and two arcs on the first, two blocks on it, one on the second, and one on a track that
// the scene does not have. Route r1 uses the two blocks of the first track, and route r2 a block that does not exist.
static SceneModel infrastructureScene() {
	SceneModel scene;
	scene.name = "Infrastructure";
	scene.tracks = {{"T1"}, {"T2"}};
	scene.nodes = {{"n11", "T1", 0.0, 0.0}, {"n12", "T1", 1.0, 0.0}, {"n13", "T1", 2.0, 0.0}, {"n21", "T2", 0.0, 1.0}, {"n22", "T2", 1.0, 1.0}};
	scene.arcs = {{"a11", "T1", "n11", "n12", 0.0, 0.0, 20.0}, {"a12", "T1", "n12", "n13", 0.0, 0.0, 20.0},
		{"a21", "T2", "n21", "n22", 0.0, 0.0, 20.0}};
	scene.blocks = {{"b1", "T1", 1.0}, {"b2", "T1", 1.0}, {"b3", "T2", 1.0}, {"b4", "ghost", 1.0}};
	scene.routes = {{"r1", {"b1", "b2"}, false, {}, false}, {"r2", {"b1", "missing"}, false, {}, false}};
	SceneStation station;
	station.id = "S1";
	station.name = "Station";
	station.platforms = {{"P1", {"n11"}}, {"P2", {"n12"}}};
	scene.stations = {station};
	return scene;
}

// A facet whose entities are the ids of a list, and that records what the panel asks of it. The tracks of the scene
// have a consumer when the id is T1, so that deleting it is refused.
class ListFacet : public InfrastructureFacet {
public:
	enum class Kind { Tracks,
		Nodes };

	explicit ListFacet(Kind kind) : m_kind(kind) {}

	int fillCount = 0;
	int pendingCount = 0;
	std::vector<std::pair<int, int>> committed;
	int addCount = 0;
	int removeCount = 0;

	QString key() const override { return m_kind == Kind::Tracks ? "tracks" : "nodes"; }
	QStringList columns() const override {
		return m_kind == Kind::Tracks ? QStringList({"ID"}) : QStringList({"ID", "Track ID"});
	}
	void fillRows(InfrastructurePanel& panel) override {
		++fillCount;
		QTableWidget& table = *panel.table();
		if (m_kind == Kind::Tracks) {
			table.setRowCount(static_cast<int>(panel.model().tracks.size()));
			for (int row = 0; row < table.rowCount(); ++row)
				setCell(table, row, 0, QString::fromStdString(panel.model().tracks[static_cast<std::size_t>(row)].id));
		} else {
			table.setRowCount(static_cast<int>(panel.model().nodes.size()));
			for (int row = 0; row < table.rowCount(); ++row) {
				const SceneNode& node = panel.model().nodes[static_cast<std::size_t>(row)];
				setCell(table, row, 0, QString::fromStdString(node.id));
				setCell(table, row, 1, QString::fromStdString(node.trackId), false);
			}
		}
	}
	void commitCell(InfrastructurePanel& panel, int row, int column) override {
		committed.emplace_back(row, column);
		const QString text = panel.table()->item(row, column)->text().trimmed();
		if (text == "bad") {
			panel.refuseEdit("Rejected");
			return;
		}
		if (m_kind == Kind::Tracks)
			panel.setSelectionId(text);
		panel.edit("Edit entity", [this, row, &text](SceneModel& model) {
			if (m_kind == Kind::Tracks)
				model.tracks[static_cast<std::size_t>(row)].id = text.toStdString();
		});
	}
	void add(InfrastructurePanel& panel) override {
		++addCount;
		const std::string id = "T" + std::to_string(panel.model().tracks.size() + 1);
		panel.setSelectionId(QString::fromStdString(id));
		panel.edit("Add entity", [&id](SceneModel& model) { model.tracks.push_back({id}); });
	}
	void remove(InfrastructurePanel& panel) override {
		++removeCount;
		const int row = panel.table()->currentRow();
		const QString id = QString::fromStdString(panel.model().tracks[static_cast<std::size_t>(row)].id);
		if (!panel.confirmDelete(id, id == "T1" ? std::vector<std::string>({"block b1"}) : std::vector<std::string>()))
			return;
		panel.setSelectionId(QString());
		panel.edit("Delete entity", [row](SceneModel& model) { model.tracks.erase(model.tracks.begin() + row); });
	}
	void commitPendingEdits(InfrastructurePanel&) override { ++pendingCount; }

private:
	Kind m_kind;
};

// A facet whose selection is made of two cells, like the one of platforms. An edited cell changes nothing in the model.
class PairFacet : public InfrastructureFacet {
public:
	QString key() const override { return "platforms"; }
	QStringList columns() const override { return {"Station ID", "ID"}; }
	void fillRows(InfrastructurePanel& panel) override {
		QTableWidget& table = *panel.table();
		int row = 0;
		for (const SceneStation& station : panel.model().stations)
			row += static_cast<int>(station.platforms.size());
		table.setRowCount(row);
		row = 0;
		for (const SceneStation& station : panel.model().stations) {
			for (const ScenePlatform& platform : station.platforms) {
				setCell(table, row, 0, QString::fromStdString(station.id));
				setCell(table, row, 1, QString::fromStdString(platform.id));
				++row;
			}
		}
	}
	QString selectionId(const QTableWidget& table, int row) const override {
		return table.item(row, 0)->text() + "+" + table.item(row, 1)->text();
	}
	void commitCell(InfrastructurePanel& panel, int, int) override {
		panel.edit("Edit platform", [](SceneModel&) {});
	}
	void add(InfrastructurePanel&) override {}
	void remove(InfrastructurePanel&) override {}
};

// A facet of the blocks on the track that the filter names. It keeps the model index of each row.
class BlockFacet : public InfrastructureFacet {
public:
	QString key() const override { return "blocks"; }
	Capabilities capabilities() const override { return TrackFilter | InsertAndMove; }
	QStringList columns() const override { return {"ID", "Track ID"}; }
	void fillRows(InfrastructurePanel& panel) override {
		QTableWidget& table = *panel.table();
		const QString track = panel.blockTrackFilterCombo()->currentData().toString();
		table.setRowCount(0);
		for (int index = 0; index < static_cast<int>(panel.model().blocks.size()); ++index) {
			const SceneBlock& block = panel.model().blocks[static_cast<std::size_t>(index)];
			if (QString::fromStdString(block.trackId) != track)
				continue;
			const int row = table.rowCount();
			table.insertRow(row);
			panel.blockRowModelIndices().push_back(index);
			setCell(table, row, 0, QString::fromStdString(block.id));
			setCell(table, row, 1, QString::fromStdString(block.trackId));
		}
	}
	void commitCell(InfrastructurePanel&, int, int) override {}
	void add(InfrastructurePanel&) override {}
	void remove(InfrastructurePanel&) override {}
};

// A facet of the routes.
class RouteFacet : public InfrastructureFacet {
public:
	QString key() const override { return "routes"; }
	Capabilities capabilities() const override { return RouteSections; }
	QStringList columns() const override { return {"ID"}; }
	void fillRows(InfrastructurePanel& panel) override {
		QTableWidget& table = *panel.table();
		table.setRowCount(static_cast<int>(panel.model().routes.size()));
		for (int row = 0; row < table.rowCount(); ++row)
			setCell(table, row, 0, QString::fromStdString(panel.model().routes[static_cast<std::size_t>(row)].id));
	}
	void commitCell(InfrastructurePanel&, int, int) override {}
	void add(InfrastructurePanel&) override {}
	void remove(InfrastructurePanel&) override {}
};

// A facet with the key and the capabilities it is given. Like the blocks it lists the blocks on the track that the filter
// names, and it counts what the buttons ask of it.
class ControlFacet : public InfrastructureFacet {
public:
	ControlFacet(const char* key, Capabilities capabilities) : m_key(key), m_capabilities(capabilities) {}

	int fillCount = 0;
	int insertCount = 0;
	std::vector<int> moves;
	int addSectionCount = 0;
	int removeSectionCount = 0;
	std::vector<int> sectionMoves;

	QString key() const override { return m_key; }
	Capabilities capabilities() const override { return m_capabilities; }
	QStringList columns() const override { return {"ID"}; }
	void fillRows(InfrastructurePanel& panel) override {
		++fillCount;
		QTableWidget& table = *panel.table();
		const QString track = panel.blockTrackFilterCombo()->currentData().toString();
		table.setRowCount(0);
		for (int index = 0; index < static_cast<int>(panel.model().blocks.size()); ++index) {
			const SceneBlock& block = panel.model().blocks[static_cast<std::size_t>(index)];
			if (QString::fromStdString(block.trackId) != track)
				continue;
			const int row = table.rowCount();
			table.insertRow(row);
			panel.blockRowModelIndices().push_back(index);
			setCell(table, row, 0, QString::fromStdString(block.id));
		}
	}
	void commitCell(InfrastructurePanel&, int, int) override {}
	void add(InfrastructurePanel&) override {}
	void remove(InfrastructurePanel&) override {}
	void insert(InfrastructurePanel&) override { ++insertCount; }
	void move(InfrastructurePanel&, int offset) override { moves.push_back(offset); }
	void addSection(InfrastructurePanel&) override { ++addSectionCount; }
	void removeSection(InfrastructurePanel&) override { ++removeSectionCount; }
	void moveSection(InfrastructurePanel&, int offset) override { sectionMoves.push_back(offset); }

private:
	QString m_key;
	Capabilities m_capabilities;
};

using Log = std::vector<std::string>;
using Selection = std::pair<QString, QString>;

// The order in which a panel and its document report an edit, and what the panel reports about its selection and status.
struct Events {
	Log log;
	SceneDocument::ChangeKinds kinds;
	std::vector<Selection> selections;
	std::vector<std::pair<QString, int>> messages;

	// The latest selection and message, or empty ones when there is none yet.
	Selection lastSelection() const { return selections.empty() ? Selection() : selections.back(); }
	std::pair<QString, int> lastMessage() const { return messages.empty() ? std::pair<QString, int>() : messages.back(); }

	Events(InfrastructurePanel& panel, SceneDocument& document) {
		QObject::connect(&document, &SceneDocument::changed, &panel, [this](SceneDocument::ChangeKinds changedKinds) {
			log.push_back("documentChanged");
			kinds = changedKinds;
		});
		QObject::connect(&panel, &InfrastructurePanel::changed, &panel, [this] { log.push_back("panelChanged"); });
		QObject::connect(&panel, &InfrastructurePanel::selectionChanged, &panel,
			[this](const QString& facet, const QString& id) { selections.emplace_back(facet, id); });
		QObject::connect(&panel, &InfrastructurePanel::statusMessage, &panel,
			[this](const QString& text, int timeoutMs) { messages.emplace_back(text, timeoutMs); });
	}
};

static QStringList columnTitles(const QTableWidget* table) {
	QStringList result;
	for (int column = 0; column < table->columnCount(); ++column)
		result << table->horizontalHeaderItem(column)->text();
	return result;
}

static QStringList firstColumn(const QTableWidget* table) {
	QStringList result;
	for (int row = 0; row < table->rowCount(); ++row)
		result << table->item(row, 0)->text();
	return result;
}

static QStringList items(const QListWidget* list) {
	QStringList result;
	for (int row = 0; row < list->count(); ++row)
		result << list->item(row)->text();
	return result;
}

static QStringList choices(const QComboBox* combo) {
	QStringList result;
	for (int index = 0; index < combo->count(); ++index)
		result << combo->itemText(index);
	return result;
}

static const Log edit({"documentChanged", "panelChanged"});

static void chooseFacet(InfrastructurePanel& panel, const char* key) {
	panel.facetCombo()->setCurrentIndex(panel.facetCombo()->findData(key));
}

class TestInfrastructurePanel : public QObject {
	Q_OBJECT

private slots:
	void offersFacetsWithoutScene();
	void showsSceneAndSelectsRows();
	void editsAddsAndDeletesEntities();
	void filtersAndMovesBlocks();
	void showsRouteSections();
	void controlsFollowCapabilities();
	void facetsWriteNothingWhileClosedForEdits();
	void laysOutControls();
	void dropsEditorsOnFacetChange();
	void returnAndAddLeaveNoEditor();
};

// The facets, the combo and a panel without a scene.
void TestInfrastructurePanel::offersFacetsWithoutScene() {
	InfrastructurePanel panel;
	const QStringList keys = {"tracks", "nodes", "arcs", "blocks", "connections", "stations", "platforms", "signals",
		"signalling_areas", "routes", "block_dependencies", "single_track_restrictions", "station_boundaries"};
	QStringList comboKeys;
	for (int index = 0; index < panel.facetCombo()->count(); ++index)
		comboKeys << panel.facetCombo()->itemData(index).toString();
	// The combo offers the thirteen facets in order.
	QCOMPARE(InfrastructurePanel::facetKeys(), keys);
	QCOMPARE(comboKeys, keys);
	// The combo names the facets.
	QCOMPARE(panel.facetCombo()->itemText(0), "Tracks");
	QCOMPARE(panel.facetCombo()->itemText(8), "Signalling areas");
	QCOMPARE(panel.facetCombo()->itemText(11), "Single-track restrictions");
	// The first facet is chosen, and there is no facet class yet.
	QCOMPARE(panel.facetKey(), "tracks");
	QCOMPARE(panel.facetTitle(), "Tracks");
	QCOMPARE(panel.facet("tracks"), nullptr);
	// A panel without a scene lists nothing and asks for a case.
	QCOMPARE(panel.table()->rowCount(), 0);
	QCOMPARE(panel.table()->columnCount(), 0);
	QVERIFY(!panel.table()->isEnabled());
	QVERIFY(!panel.facetCombo()->isEnabled());
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY(!panel.deleteButton()->isEnabled());
	QVERIFY(!panel.guidanceLabel()->isHidden());
	QCOMPARE(panel.guidanceLabel()->text(), "Open a case to edit infrastructure.");
	// The controls of blocks and routes are hidden.
	QVERIFY(panel.blockTrackFilterCombo()->isHidden());
	QVERIFY(panel.insertBlockButton()->isHidden());
	QVERIFY(panel.moveBlockUpButton()->isHidden());
	QVERIFY(panel.moveBlockDownButton()->isHidden());
	QVERIFY(panel.routeSectionDetailWidget()->isHidden());
	panel.addEntity();
	panel.deleteEntity();
	panel.commitCell(0, 0);
	panel.commitPendingEditors();
	QVERIFY2(panel.selectionId().isEmpty(), "the actions of a panel without a scene do nothing");

	auto tracks = std::make_unique<ListFacet>(ListFacet::Kind::Tracks);
	ListFacet* trackFacet = tracks.get();
	panel.setFacet(std::move(tracks));
	// A facet is found by its key.
	QCOMPARE(panel.facet("tracks"), trackFacet);
	QCOMPARE(panel.facet("nodes"), nullptr);
	panel.setFacet(nullptr);
	// No facet is not a facet.
	QCOMPARE(panel.facet("tracks"), trackFacet);
	auto replacement = std::make_unique<ListFacet>(ListFacet::Kind::Tracks);
	ListFacet* replacementFacet = replacement.get();
	panel.setFacet(std::move(replacement));
	// A facet takes the place of the one with its key.
	QCOMPARE(panel.facet("tracks"), replacementFacet);
}

// Showing a scene, choosing facets and selecting rows.
void TestInfrastructurePanel::showsSceneAndSelectsRows() {
	SceneDocument document;
	InfrastructurePanel panel;
	auto tracks = std::make_unique<ListFacet>(ListFacet::Kind::Tracks);
	auto nodes = std::make_unique<ListFacet>(ListFacet::Kind::Nodes);
	ListFacet* trackFacet = tracks.get();
	ListFacet* nodeFacet = nodes.get();
	panel.setFacet(std::move(tracks));
	panel.setFacet(std::move(nodes));
	panel.setFacet(std::make_unique<PairFacet>());
	panel.setDocument(&document);
	Events events(panel, document);

	// A document without a scene lists no tracks.
	QCOMPARE(panel.table()->rowCount(), 0);
	QCOMPARE(columnTitles(panel.table()), QStringList({"ID"}));
	QCOMPARE(panel.guidanceLabel()->text(), "Open a case to edit infrastructure.");
	QVERIFY(!panel.addButton()->isEnabled());

	document.replace(infrastructureScene(), false);
	panel.refresh();
	// The tracks are listed in the columns of their facet, and the table can be used.
	QCOMPARE(columnTitles(panel.table()), QStringList({"ID"}));
	QCOMPARE(firstColumn(panel.table()), QStringList({"T1", "T2"}));
	QVERIFY(panel.table()->isEnabled());
	QVERIFY(panel.facetCombo()->isEnabled());
	QVERIFY(panel.addButton()->isEnabled());
	QVERIFY(!panel.deleteButton()->isEnabled());
	QVERIFY(panel.guidanceLabel()->isHidden());
	// No row is selected until the user selects one.
	QCOMPARE(panel.table()->currentRow(), -1);
	QVERIFY(panel.selectionId().isEmpty());
	QCOMPARE(events.lastSelection(), Selection("tracks", QString()));
	// Showing a scene edits nothing.
	QVERIFY(events.log.empty());
	QVERIFY(!document.isDirty());
	QVERIFY(trackFacet->committed.empty());

	events.selections.clear();
	panel.table()->setCurrentCell(1, 0);
	// Selecting a row tells which entity of which facet is selected.
	QCOMPARE(events.selections, std::vector<Selection>({Selection("tracks", "T2")}));
	QCOMPARE(panel.selectionId(), "T2");

	// facet switching: the selection is dropped, the row at the same position is selected again
	events.selections.clear();
	chooseFacet(panel, "nodes");
	// Another facet lists its own columns and rows.
	QCOMPARE(panel.facetKey(), "nodes");
	QCOMPARE(columnTitles(panel.table()), QStringList({"ID", "Track ID"}));
	QCOMPARE(firstColumn(panel.table()), QStringList({"n11", "n12", "n13", "n21", "n22"}));
	// Another facet selects the row at the position that was selected.
	QCOMPARE(panel.table()->currentRow(), 1);
	QCOMPARE(panel.selectionId(), "n12");
	QCOMPARE(events.lastSelection(), Selection("nodes", "n12"));
	// A facet can show a cell that cannot be edited.
	QVERIFY(!panel.table()->item(1, 1)->flags().testFlag(Qt::ItemIsEditable));
	QVERIFY(panel.table()->item(1, 0)->flags().testFlag(Qt::ItemIsEditable));
	panel.table()->setCurrentCell(4, 0);
	chooseFacet(panel, "tracks");
	// A position beyond the rows selects the last row.
	QCOMPARE(panel.table()->currentRow(), 1);
	QCOMPARE(panel.selectionId(), "T2");
	// Choosing facets and rows edits nothing.
	QVERIFY(events.log.empty());
	QVERIFY(!document.isDirty());

	// a facet that moves with its entity
	document.mutableModel().tracks = {{"T3"}, {"T2"}, {"T1"}};
	panel.refresh();
	// A refresh keeps the entity that is selected.
	QCOMPARE(firstColumn(panel.table()), QStringList({"T3", "T2", "T1"}));
	QCOMPARE(panel.table()->currentRow(), 1);
	QCOMPARE(panel.selectionId(), "T2");
	document.mutableModel().tracks = {{"T3"}, {"T1"}, {"T2"}};
	panel.refresh();
	// A refresh selects the entity again where it has moved to.
	QCOMPARE(panel.table()->currentRow(), 2);
	QCOMPARE(panel.selectionId(), "T2");
	document.mutableModel().tracks = {{"T3"}, {"T1"}};
	panel.refresh();
	// A refresh selects the row at the same position when the entity is gone.
	QCOMPARE(panel.table()->currentRow(), 1);
	QCOMPARE(panel.selectionId(), "T1");
	panel.refresh(true);
	// A refresh that resets the selection selects no row.
	QCOMPARE(panel.table()->currentRow(), -1);
	QVERIFY(panel.selectionId().isEmpty());
	QCOMPARE(events.lastSelection(), Selection("tracks", QString()));
	document.mutableModel().tracks = {{"T1"}, {"T2"}};
	panel.refresh();

	// a facet that names its entities by two cells
	chooseFacet(panel, "platforms");
	panel.table()->setCurrentCell(1, 0);
	// A facet decides how an entity is named in the selection.
	QCOMPARE(firstColumn(panel.table()), QStringList({"S1", "S1"}));
	QCOMPARE(panel.selectionId(), "S1+P2");
	QCOMPARE(events.lastSelection(), Selection("platforms", "S1+P2"));
	document.mutableModel().stations[0].platforms = {{"P0", {}}, {"P1", {"n11"}}, {"P2", {"n12"}}};
	panel.refresh();
	// The selection of a platform follows its station and its id.
	QCOMPARE(panel.table()->currentRow(), 2);
	QCOMPARE(panel.selectionId(), "S1+P2");

	// a facet without a class
	chooseFacet(panel, "stations");
	// A facet without a class lists nothing.
	QCOMPARE(panel.table()->rowCount(), 0);
	QCOMPARE(panel.table()->columnCount(), 0);
	QVERIFY(panel.selectionId().isEmpty());
	QCOMPARE(panel.guidanceLabel()->text(), "No stations yet. Add a station, then add its platforms.");
	QVERIFY(!panel.guidanceLabel()->isHidden());
	panel.addEntity();
	panel.deleteEntity();
	// A facet without a class changes nothing.
	QVERIFY(events.log.empty());
	QVERIFY(!document.isDirty());

	// the sentence follows what the scene has: the panel hands over its tracks, nodes and stations and its editability
	document.mutableModel().nodes = {{"n1", "T1", 0.0, 0.0}};
	chooseFacet(panel, "arcs");
	// An empty arcs table asks for nodes while the scene has fewer than two.
	QCOMPARE(panel.table()->rowCount(), 0);
	QVERIFY(panel.guidanceLabel()->text().contains("at least two nodes"));
	document.mutableModel().nodes = {{"n1", "T1", 0.0, 0.0}, {"n2", "T1", 1.0, 0.0}};
	panel.refresh();
	QVERIFY2(panel.guidanceLabel()->text().startsWith("No arcs yet. Add one, then"), "an empty arcs table does not ask for nodes that exist");
	document.setEditable(false);
	panel.refresh();
	// An empty table says why Add is off while a run uses the scene.
	QCOMPARE(panel.guidanceLabel()->text(), "No arcs yet. Stop the run to edit.");
	document.setEditable(true);
	document.mutableModel().nodes.clear();
	panel.refresh();

	// pending edits go to the facet that is chosen
	chooseFacet(panel, "tracks");
	const int pendingBefore = trackFacet->pendingCount;
	panel.commitPendingEditors();
	// Pending edits are committed by the chosen facet.
	QCOMPARE(trackFacet->pendingCount, pendingBefore + 1);
	QCOMPARE(nodeFacet->pendingCount, 0);

	// the scene cannot be edited while a run uses it
	trackFacet->committed.clear();
	const int addsBefore = trackFacet->addCount;
	document.setEditable(false);
	panel.refresh();
	// The controls are disabled while a run uses the scene.
	QVERIFY(!panel.table()->isEnabled());
	QVERIFY(!panel.facetCombo()->isEnabled());
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY(!panel.deleteButton()->isEnabled());
	panel.table()->item(0, 0)->setText("during-run");
	panel.addEntity();
	panel.deleteEntity();
	// A facet is not asked to edit while a run uses the scene.
	QVERIFY(trackFacet->committed.empty());
	QCOMPARE(trackFacet->addCount, addsBefore);
	QCOMPARE(trackFacet->removeCount, 0);
	QCOMPARE(document.model().tracks[0].id, "T1");
	QVERIFY(events.log.empty());
	document.setEditable(true);
	panel.refresh();
	// The controls work again when the run is over.
	QVERIFY(panel.table()->isEnabled());
	QVERIFY(panel.facetCombo()->isEnabled());
	QVERIFY(panel.addButton()->isEnabled());
}

// Edits, additions and deletions through the panel.
void TestInfrastructurePanel::editsAddsAndDeletesEntities() {
	SceneDocument document;
	InfrastructurePanel panel;
	FakePrompts prompts;
	auto tracks = std::make_unique<ListFacet>(ListFacet::Kind::Tracks);
	ListFacet* trackFacet = tracks.get();
	panel.setPrompts(&prompts);
	panel.setFacet(std::move(tracks));
	panel.setDocument(&document);
	document.replace(infrastructureScene(), false);
	panel.refresh();
	Events events(panel, document);

	panel.table()->item(1, 0)->setText(" T9 ");
	// An edited cell is given to the facet, which marks the document once.
	QCOMPARE(trackFacet->committed, (std::vector<std::pair<int, int>>({{1, 0}})));
	QCOMPARE(document.model().tracks[1].id, "T9");
	QCOMPARE(events.log, edit);
	QCOMPARE(events.kinds, SceneDocument::ChangeKinds(SceneDocument::Infrastructure));
	QCOMPARE(document.lastChangeLabel(), "Edit entity");
	QVERIFY(document.isDirty());
	// The table shows the model again, and the selection follows a rename.
	QCOMPARE(firstColumn(panel.table()), QStringList({"T1", "T9"}));
	QCOMPARE(panel.table()->currentRow(), 1);
	QCOMPARE(panel.selectionId(), "T9");
	QCOMPARE(events.lastSelection(), Selection("tracks", "T9"));

	events.log.clear();
	events.messages.clear();
	panel.table()->item(1, 0)->setText("bad");
	// A refused edit is shown in the status line, and the table shows the model again.
	QCOMPARE(document.model().tracks[1].id, "T9");
	QVERIFY(events.log.empty());
	QCOMPARE(panel.table()->item(1, 0)->text(), "T9");
	QCOMPARE(events.messages, (std::vector<std::pair<QString, int>>({{"Rejected", 4000}})));

	panel.refuseEdit();
	// A refused edit without a message shows none.
	QCOMPARE(events.messages.size(), std::size_t(1));
	QVERIFY(events.log.empty());

	// add
	document.markSaved();
	events.log.clear();
	panel.addButton()->click();
	// The add button asks the facet to add, and the new entity is selected.
	QCOMPARE(trackFacet->addCount, 1);
	QCOMPARE(document.model().tracks.size(), std::size_t(3));
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Add entity");
	QVERIFY(document.isDirty());
	QCOMPARE(panel.selectionId(), "T3");
	QCOMPARE(panel.table()->currentRow(), 2);

	// delete
	prompts.confirmCount = 0;
	events.log.clear();
	panel.table()->setCurrentCell(0, 0);
	panel.deleteButton()->click();
	// An entity that is used cannot be deleted, and the user is told which items use it.
	QCOMPARE(trackFacet->removeCount, 1);
	QCOMPARE(prompts.errorCount, 1);
	QCOMPARE(prompts.confirmCount, 0);
	QCOMPARE(prompts.errorMessage, "Track 'T1' is used by:\n\xE2\x80\xA2 block b1\nRemove or change these first.");
	QCOMPARE(document.model().tracks.size(), std::size_t(3));
	QVERIFY(events.log.empty());
	// a long list of consumers is cut short
	{
		std::vector<std::string> consumers;
		for (int index = 1; index <= 25; ++index)
			consumers.push_back("node n" + std::to_string(index));
		prompts.errorMessage.clear();
		QVERIFY2(!panel.confirmDelete("T1", consumers), "an entity with many consumers cannot be deleted");
		// The message lists the first ten consumers and counts the others.
		QVERIFY(prompts.errorMessage.contains("\xE2\x80\xA2 node n10\n"));
		QVERIFY(!prompts.errorMessage.contains("node n11"));
		QVERIFY(prompts.errorMessage.contains("\nand 15 more\n"));
		consumers.resize(10);
		prompts.errorMessage.clear();
		panel.confirmDelete("T1", consumers);
		// Ten consumers are all listed.
		QVERIFY(prompts.errorMessage.contains("node n10\n"));
		QVERIFY(!prompts.errorMessage.contains("more"));
	}
	panel.table()->setCurrentCell(1, 0);
	panel.deleteButton()->click();
	// Nothing is deleted when the user answers no.
	QCOMPARE(prompts.confirmCount, 1);
	QCOMPARE(prompts.confirmQuestion, "Delete track 'T9'?");
	QCOMPARE(document.model().tracks.size(), std::size_t(3));
	QVERIFY(events.log.empty());
	QCOMPARE(panel.selectionId(), "T9");
	prompts.confirmAnswer = true;
	panel.deleteButton()->click();
	// An entity is deleted when the user answers yes, and the row at its position is selected.
	QCOMPARE(prompts.confirmCount, 2);
	QCOMPARE(document.model().tracks.size(), std::size_t(2));
	QCOMPARE(document.model().tracks[1].id, "T3");
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Delete entity");
	QCOMPARE(panel.table()->currentRow(), 1);
	QCOMPARE(panel.selectionId(), "T3");
	const int removesBefore = trackFacet->removeCount;
	panel.table()->setCurrentCell(-1, -1);
	panel.deleteButton()->click();
	panel.deleteEntity();
	// Deleting without a selected row asks nothing of the facet.
	QCOMPARE(trackFacet->removeCount, removesBefore);
	// The delete button says why it is off while no row is selected.
	QVERIFY(!panel.deleteButton()->isEnabled());
	QCOMPARE(panel.deleteButton()->toolTip(), "Select a row to delete.");
	panel.table()->setCurrentCell(0, 0);
	// Choosing a row turns the delete button on.
	QVERIFY(panel.deleteButton()->isEnabled());
	QCOMPARE(panel.deleteButton()->toolTip(), "Delete the selected row.");
	panel.table()->setCurrentCell(-1, -1);
	document.setEditable(false);
	panel.refresh();
	// The delete button says why it is off while a run uses the scene.
	QVERIFY(!panel.deleteButton()->isEnabled());
	QCOMPARE(panel.deleteButton()->toolTip(), "Stop the run to edit.");
	document.setEditable(true);
	panel.refresh();
	panel.showStatus("Hello", 1500);
	// A facet can show a message in the status line.
	QCOMPARE(events.lastMessage(), (std::pair<QString, int>("Hello", 1500)));
}

// Blocks: the filter on the track and the buttons that move a block.
void TestInfrastructurePanel::filtersAndMovesBlocks() {
	SceneDocument document;
	InfrastructurePanel panel;
	panel.setFacet(std::make_unique<BlockFacet>());
	panel.setFacet(std::make_unique<ListFacet>(ListFacet::Kind::Tracks));
	panel.setDocument(&document);
	document.replace(infrastructureScene(), false);
	Events events(panel, document);
	ListFacet* trackFacet = static_cast<ListFacet*>(panel.facet("tracks"));
	panel.refresh();
	// The filter on the track is hidden until blocks are chosen.
	QVERIFY(panel.blockTrackFilterCombo()->isHidden());
	QVERIFY(panel.insertBlockButton()->isHidden());

	chooseFacet(panel, "blocks");
	// The filter on the track and the buttons of blocks are shown with blocks.
	QVERIFY(!panel.blockTrackFilterCombo()->isHidden());
	QVERIFY(!panel.insertBlockButton()->isHidden());
	QVERIFY(!panel.moveBlockUpButton()->isHidden());
	QVERIFY(!panel.moveBlockDownButton()->isHidden());
	// The filter offers the tracks, and a track that the scene does not have if a block names it.
	QCOMPARE(choices(panel.blockTrackFilterCombo()), QStringList({"T1", "T2", "ghost (missing)"}));
	QCOMPARE(panel.blockTrackFilterCombo()->currentIndex(), 0);
	QVERIFY(panel.blockTrackFilterCombo()->isEnabled());
	// The blocks of the chosen track are listed, and the buttons wait for a selected block.
	QCOMPARE(firstColumn(panel.table()), QStringList({"b1", "b2"}));
	QCOMPARE(panel.blockRowModelIndices(), std::vector<int>({0, 1}));
	QVERIFY(panel.addButton()->isEnabled());
	QVERIFY(!panel.insertBlockButton()->isEnabled());
	QVERIFY(!panel.moveBlockUpButton()->isEnabled());
	QVERIFY(!panel.moveBlockDownButton()->isEnabled());
	QVERIFY(!panel.deleteButton()->isEnabled());
	// Blocks come with their guidance.
	QVERIFY(!panel.guidanceLabel()->isHidden());
	QCOMPARE(panel.guidanceLabel()->text(), "Select a block to insert a new one in its place or to move it.");

	panel.table()->setCurrentCell(0, 0);
	// The first block cannot move up.
	QVERIFY(!panel.moveBlockUpButton()->isEnabled());
	QVERIFY(panel.moveBlockDownButton()->isEnabled());
	panel.table()->setCurrentCell(1, 0);
	// The last block cannot move down.
	QVERIFY(panel.moveBlockUpButton()->isEnabled());
	QVERIFY(!panel.moveBlockDownButton()->isEnabled());
	// The selection names the block.
	QCOMPARE(events.lastSelection(), Selection("blocks", "b2"));
	panel.refresh();
	// A block can be inserted and deleted when the table has been refreshed with a selected block.
	QVERIFY(panel.insertBlockButton()->isEnabled());
	QVERIFY(panel.deleteButton()->isEnabled());
	QCOMPARE(panel.table()->currentRow(), 1);

	// another track
	panel.blockTrackFilterCombo()->setCurrentIndex(1);
	// Choosing a track lists its blocks, and selects the row at the position that was selected.
	QCOMPARE(firstColumn(panel.table()), QStringList({"b3"}));
	QCOMPARE(panel.blockRowModelIndices(), std::vector<int>({2}));
	QCOMPARE(panel.table()->currentRow(), 0);
	QCOMPARE(panel.selectionId(), "b3");
	QVERIFY(panel.addButton()->isEnabled());
	panel.blockTrackFilterCombo()->setCurrentIndex(2);
	// Blocks cannot be added to a track that the scene does not have.
	QCOMPARE(firstColumn(panel.table()), QStringList({"b4"}));
	QVERIFY(!panel.addButton()->isEnabled());
	panel.blockTrackFilterCombo()->setCurrentIndex(0);

	// no blocks on the track
	document.mutableModel().tracks.push_back({"T3"});
	panel.refreshBlockTrackFilter();
	panel.blockTrackFilterCombo()->setCurrentIndex(panel.blockTrackFilterCombo()->findData("T3"));
	// A track without blocks is explained.
	QCOMPARE(panel.table()->rowCount(), 0);
	QCOMPARE(panel.guidanceLabel()->text(), "No blocks on this track. Choose another track or add a block.");
	QVERIFY(panel.addButton()->isEnabled());
	document.mutableModel().tracks.pop_back();

	// the filter belongs to blocks
	chooseFacet(panel, "tracks");
	const int fillsBefore = trackFacet->fillCount;
	// The filter on the track and the buttons of blocks go with blocks.
	QVERIFY(panel.blockTrackFilterCombo()->isHidden());
	QVERIFY(panel.insertBlockButton()->isHidden());
	QVERIFY(panel.moveBlockUpButton()->isHidden());
	QVERIFY(panel.moveBlockDownButton()->isHidden());
	panel.blockTrackFilterCombo()->setCurrentIndex(1);
	// Choosing a track does not refresh the table of another facet.
	QCOMPARE(trackFacet->fillCount, fillsBefore);

	// the scene cannot be edited while a run uses it
	chooseFacet(panel, "blocks");
	document.setEditable(false);
	panel.refresh();
	// The filter and the buttons of blocks are disabled while a run uses the scene.
	QVERIFY(!panel.blockTrackFilterCombo()->isEnabled());
	QVERIFY(!panel.addButton()->isEnabled());
	QVERIFY(!panel.insertBlockButton()->isEnabled());
	QVERIFY(!panel.moveBlockUpButton()->isEnabled());
	QVERIFY(!panel.moveBlockDownButton()->isEnabled());
}

// Routes: the sections of the selected route.
void TestInfrastructurePanel::showsRouteSections() {
	SceneDocument document;
	InfrastructurePanel panel;
	panel.setFacet(std::make_unique<RouteFacet>());
	panel.setFacet(std::make_unique<ListFacet>(ListFacet::Kind::Tracks));
	panel.setDocument(&document);
	document.replace(infrastructureScene(), false);
	panel.refresh();
	QVERIFY2(panel.routeSectionDetailWidget()->isHidden(), "the sections of a route are hidden until routes are chosen");

	chooseFacet(panel, "routes");
	// Routes are listed with their guidance, and the sections are shown.
	QVERIFY(!panel.routeSectionDetailWidget()->isHidden());
	QCOMPARE(firstColumn(panel.table()), QStringList({"r1", "r2"}));
	QCOMPARE(panel.guidanceLabel()->text(), "Select a route to edit its ordered sections below.");
	const QStringList catalog = choices(panel.routeSectionCatalogCombo());
	// The catalog names every section of the scene by its label, with its id as data and tooltip.
	QCOMPARE(catalog.size(), qsizetype(3));
	QCOMPARE(catalog[0], "T1 0.000 to 1.000 km");
	QCOMPARE(catalog[2], "T2 0.000 to 1.000 km");
	QCOMPARE(panel.routeSectionCatalogCombo()->itemData(0).toString(), "@b1@");
	QCOMPARE(panel.routeSectionCatalogCombo()->itemData(0, Qt::ToolTipRole).toString(), "@b1@");
	// No section can be edited until a route is selected.
	QCOMPARE(panel.routeSectionListWidget()->count(), 0);
	QVERIFY(!panel.routeSectionListWidget()->isEnabled());
	QVERIFY(!panel.addRouteSectionButton()->isEnabled());
	QVERIFY(!panel.removeRouteSectionButton()->isEnabled());
	QVERIFY(!panel.routeSectionCatalogCombo()->isEnabled());

	panel.table()->setCurrentCell(0, 0);
	// The sections of the selected route are listed, and the first is selected.
	QCOMPARE(items(panel.routeSectionListWidget()), QStringList({"T1 0.000 to 1.000 km (legacy: b1)", "T1 1.000 to 2.000 km (legacy: b2)"}));
	QCOMPARE(panel.routeSectionListWidget()->item(1)->toolTip(), "b2");
	QCOMPARE(panel.routeSectionListWidget()->currentRow(), 0);
	QVERIFY(panel.routeSectionListWidget()->isEnabled());
	QVERIFY(panel.routeSectionCatalogCombo()->isEnabled());
	QVERIFY(panel.addRouteSectionButton()->isEnabled());
	QVERIFY(panel.removeRouteSectionButton()->isEnabled());
	QVERIFY(!panel.moveRouteSectionUpButton()->isEnabled());
	QVERIFY(panel.moveRouteSectionDownButton()->isEnabled());
	panel.routeSectionListWidget()->setCurrentRow(1);
	// The buttons follow the selected section.
	QVERIFY(panel.moveRouteSectionUpButton()->isEnabled());
	QVERIFY(!panel.moveRouteSectionDownButton()->isEnabled());

	panel.table()->setCurrentCell(1, 0);
	// A section that the scene does not have is listed as invalid.
	QCOMPARE(items(panel.routeSectionListWidget()), QStringList({"T1 0.000 to 1.000 km (legacy: b1)", "missing (missing)"}));
	QCOMPARE(panel.routeSectionListWidget()->item(1)->toolTip(), "missing");

	chooseFacet(panel, "tracks");
	QVERIFY2(panel.routeSectionDetailWidget()->isHidden(), "the sections of a route go with routes");

	SceneSectionDescriptor connection;
	connection.id = "@b1@-1.000000/@b3@-0.000000";
	connection.connectionDerived = true;
	SceneSectionLabels labels;
	labels.byReference[connection.id] = "T1 1.000, T2 0.000 km";
	// A section of a connection is named by its label.
	QCOMPARE(InfrastructurePanel::sectionLabel(labels, connection), "T1 1.000, T2 0.000 km");
}

// The facet decides the controls by its capabilities, not by its key: tracks with every capability get the filter, the
// buttons of blocks and the sections of routes, and blocks without one get none of them.
void TestInfrastructurePanel::controlsFollowCapabilities() {
	SceneDocument document;
	InfrastructurePanel panel;
	panel.setFacet(std::make_unique<ControlFacet>("tracks",
		InfrastructureFacet::TrackFilter | InfrastructureFacet::InsertAndMove | InfrastructureFacet::RouteSections));
	panel.setFacet(std::make_unique<ControlFacet>("blocks", InfrastructureFacet::Capabilities()));
	auto* capable = static_cast<ControlFacet*>(panel.facet("tracks"));
	auto* bare = static_cast<ControlFacet*>(panel.facet("blocks"));
	panel.setDocument(&document);
	document.replace(infrastructureScene(), false);
	panel.refresh();
	QCOMPARE(panel.facetKey(), "tracks");
	QVERIFY(!panel.blockTrackFilterCombo()->isHidden());
	QVERIFY(!panel.insertBlockButton()->isHidden());
	QVERIFY(!panel.moveBlockUpButton()->isHidden());
	QVERIFY(!panel.moveBlockDownButton()->isHidden());
	QVERIFY(!panel.routeSectionDetailWidget()->isHidden());
	QCOMPARE(choices(panel.blockTrackFilterCombo()), QStringList({"T1", "T2", "ghost (missing)"}));
	QCOMPARE(firstColumn(panel.table()), QStringList({"b1", "b2"}));

	panel.table()->setCurrentCell(0, 0);
	QVERIFY(panel.insertBlockButton()->isEnabled());
	QVERIFY(panel.moveBlockDownButton()->isEnabled());
	panel.insertBlockButton()->click();
	panel.moveBlockDownButton()->click();
	QCOMPARE(capable->insertCount, 1);
	QCOMPARE(capable->moves, std::vector<int>({1}));
	// The table row is the route whose sections are shown.
	QCOMPARE(panel.routeSectionListWidget()->count(), 2);
	QVERIFY(panel.addRouteSectionButton()->isEnabled());
	panel.addRouteSectionButton()->click();
	panel.removeRouteSectionButton()->click();
	panel.moveRouteSectionDownButton()->click();
	QCOMPARE(capable->addSectionCount, 1);
	QCOMPARE(capable->removeSectionCount, 1);
	QCOMPARE(capable->sectionMoves, std::vector<int>({1}));
	const int fills = capable->fillCount;
	panel.blockTrackFilterCombo()->setCurrentIndex(1);
	QVERIFY2(capable->fillCount > fills, "choosing a track lists the rows of a facet with the filter again");
	QCOMPARE(firstColumn(panel.table()), QStringList({"b3"}));

	chooseFacet(panel, "blocks");
	QVERIFY(panel.blockTrackFilterCombo()->isHidden());
	QVERIFY(panel.insertBlockButton()->isHidden());
	QVERIFY(panel.moveBlockUpButton()->isHidden());
	QVERIFY(panel.moveBlockDownButton()->isHidden());
	QVERIFY(panel.routeSectionDetailWidget()->isHidden());
	const int bareFills = bare->fillCount;
	panel.blockTrackFilterCombo()->setCurrentIndex(0);
	QCOMPARE(bare->fillCount, bareFills);
	panel.table()->setCurrentCell(0, 0);
	panel.insertEntity();
	panel.moveEntity(1);
	panel.addSection();
	panel.removeSection();
	panel.moveSection(1);
	QCOMPARE(bare->insertCount, 0);
	QVERIFY(bare->moves.empty());
	QCOMPARE(bare->addSectionCount, 0);
	QCOMPARE(bare->removeSectionCount, 0);
	QVERIFY(bare->sectionMoves.empty());
}

// The combos in the table and the route sections of the real facets write nothing while the scene is closed for edits,
// even when they are reached past the panel's checks: the model, the revision and the undo history stay as they were.
void TestInfrastructurePanel::facetsWriteNothingWhileClosedForEdits() {
	SceneDocument document;
	InfrastructurePanel panel;
	for (std::unique_ptr<InfrastructureFacet>& facet : makeBlockRouteFacets())
		panel.setFacet(std::move(facet));
	for (std::unique_ptr<InfrastructureFacet>& facet : makeSignalFacets())
		panel.setFacet(std::move(facet));
	panel.setDocument(&document);
	SceneModel scene = infrastructureScene();
	scene.blockDependencies = {{"@b1@", "@b2@"}};
	SceneSignal signal;
	signal.id = "sig1";
	sceneSignals(scene) = {signal};
	scene.signallingAreas = {{"area1", 0.0, 2.0, kSignallingLevelUnset, {}}};
	document.replace(scene, false);
	Events events(panel, document);
	const quint64 revision = document.revision();
	document.setEditable(false);
	const auto combo = [&panel](const char* facet, int column) {
		chooseFacet(panel, facet);
		panel.refresh();
		return qobject_cast<QComboBox*>(panel.table()->cellWidget(0, column));
	};

	QComboBox* dependency = combo("block_dependencies", 0);
	QVERIFY(dependency);
	QCOMPARE(dependency->currentData().toString(), "@b1@");
	dependency->setCurrentIndex(dependency->findData("@b2@"));
	QComboBox* protectedSection = combo("signals", 1);
	QVERIFY(protectedSection);
	protectedSection->setCurrentIndex(protectedSection->findData("@b1@"));
	QComboBox* system = combo("signalling_areas", 3);
	QVERIFY(system);
	system->setCurrentIndex(1);
	QComboBox* track = combo("signalling_areas", 4);
	QVERIFY(track);
	track->setCurrentIndex(track->findData("T1"));
	chooseFacet(panel, "routes");
	panel.refresh();
	panel.table()->setCurrentCell(0, 0);
	InfrastructureFacet* routes = panel.facet("routes");
	QVERIFY(routes);
	QVERIFY(panel.routeSectionCatalogCombo()->currentIndex() >= 0);
	routes->addSection(panel);
	panel.routeSectionListWidget()->setCurrentRow(0);
	routes->removeSection(panel);
	routes->moveSection(panel, 1);

	const SceneModel& model = document.model();
	QCOMPARE(model.blockDependencies[0].block, "@b1@");
	QCOMPARE(sceneSignals(model)[0].protectedSection, std::string());
	QCOMPARE(model.signallingAreas[0].level, kSignallingLevelUnset);
	QCOMPARE(model.signallingAreas[0].trackId, std::string());
	QCOMPARE(model.routes[0].blocks, std::vector<std::string>({"b1", "b2"}));
	QCOMPARE(document.revision(), revision);
	QVERIFY(!document.isDirty());
	QVERIFY2(events.log.empty(), "no edit is told, so the window records no undo step");

	document.setEditable(true);
	panel.refresh();
	panel.table()->setCurrentCell(0, 0);
	routes->addSection(panel);
	QVERIFY2(model.routes[0].blocks.size() == 3, "the same call changes the route once the scene is open for edits");
	QCOMPARE(events.log, edit);
}

// The layout: the combo on top, the buttons under the table, and the sections of a route below it in a splitter.
void TestInfrastructurePanel::laysOutControls() {
	SceneDocument document;
	InfrastructurePanel panel;
	panel.setFacet(std::make_unique<BlockFacet>());
	panel.setFacet(std::make_unique<RouteFacet>());
	panel.setFacet(std::make_unique<ListFacet>(ListFacet::Kind::Tracks));
	panel.setDocument(&document);
	document.replace(infrastructureScene(), false);
	panel.refresh();
	panel.resize(560, 640);
	panel.show();
	QApplication::processEvents();
	const auto top = [&panel](const QWidget* widget) { return widget->mapTo(&panel, QPoint(0, 0)).y(); };
	const auto bottom = [&top](const QWidget* widget) { return top(widget) + widget->height(); };
	const auto right = [&panel](const QWidget* widget) { return widget->mapTo(&panel, QPoint(0, 0)).x() + widget->width(); };
	const auto inside = [&panel, &bottom, &right](const QWidget* widget) {
		return widget->isVisible() && bottom(widget) <= panel.height() && right(widget) <= panel.width();
	};
	const QTableWidget* table = panel.table();
	const QPushButton* add = panel.addButton();

	// The combo is above the table, and Add and Delete are in a row under it.
	QCOMPARE_LE(bottom(panel.facetCombo()), top(table));
	QCOMPARE_LE(bottom(table), top(add));
	QCOMPARE(top(add), top(panel.deleteButton()));
	QVERIFY(inside(add));
	QVERIFY(inside(panel.deleteButton()));
	// The buttons of blocks and the sections of a route are not shown with tracks.
	QVERIFY(panel.insertBlockButton()->isHidden());
	QVERIFY(panel.routeSectionDetailWidget()->isHidden());

	chooseFacet(panel, "blocks");
	QApplication::processEvents();
	// The filter on the track is between the combo and the table.
	QCOMPARE_LE(bottom(panel.facetCombo()), top(panel.blockTrackFilterCombo()));
	QCOMPARE_LE(bottom(panel.blockTrackFilterCombo()), top(table));
	QCOMPARE_LE(bottom(table), top(add));
	for (const QPushButton* button : {panel.deleteButton(), panel.insertBlockButton(), panel.moveBlockUpButton(), panel.moveBlockDownButton()}) {
		// A button of blocks is in the row of Add.
		QCOMPARE(top(button), top(add));
		QVERIFY(inside(button));
		QCOMPARE_GT(button->x(), add->x());
	}
	QVERIFY2(panel.routeSectionDetailWidget()->isHidden(), "the sections of a route are not shown with blocks");

	chooseFacet(panel, "routes");
	QApplication::processEvents();
	QWidget* detail = panel.routeSectionDetailWidget();
	auto* splitter = qobject_cast<QSplitter*>(detail->parentWidget());
	const auto* heading = detail->findChild<QLabel*>("routeSectionHeading");
	// The sections of a route are in a vertical splitter below the table.
	QVERIFY(panel.insertBlockButton()->isHidden());
	QVERIFY(detail->isVisible());
	QVERIFY(inside(detail));
	if (splitter == nullptr) QFAIL("the sections of a route are in a vertical splitter below the table");
	QCOMPARE(splitter->orientation(), Qt::Vertical);
	QCOMPARE(splitter->indexOf(detail), 1);
	QCOMPARE(splitter->indexOf(table->parentWidget()), 0);
	// The heading, the catalog, the list and the buttons of the sections follow each other under the buttons of the table.
	if (heading == nullptr)
		QFAIL("the heading, the catalog, the list and the buttons of the sections follow each other under the buttons of the table");
	QCOMPARE(heading->text(), "Route sections");
	QVERIFY(heading->isVisible());
	QCOMPARE_LE(bottom(add), top(heading));
	QCOMPARE_LT(top(heading), top(panel.routeSectionCatalogCombo()));
	QCOMPARE_LE(bottom(panel.routeSectionCatalogCombo()), top(panel.routeSectionListWidget()));
	QCOMPARE_LE(bottom(panel.routeSectionListWidget()), top(panel.removeRouteSectionButton()));
	for (const QPushButton* button : {panel.addButton(), panel.deleteButton(), panel.addRouteSectionButton(), panel.removeRouteSectionButton(),
			 panel.moveRouteSectionUpButton(), panel.moveRouteSectionDownButton()})
		QVERIFY2(inside(button), "a button of the routes can be reached without scrolling");
	// The user drags the handle up by 40 pixels; the table pane keeps its minimum height, so it may give less.
	const QList<int> before = splitter->sizes();
	QSplitterHandle* handle = splitter->handle(1);
	const QPoint start = handle->rect().center();
	const QPoint end = start - QPoint(0, 40);
	QMouseEvent press(QEvent::MouseButtonPress, start, handle->mapToGlobal(start), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent move(QEvent::MouseMove, end, handle->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
	QMouseEvent release(QEvent::MouseButtonRelease, end, handle->mapToGlobal(end), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(handle, &press);
	QApplication::sendEvent(handle, &move);
	QApplication::sendEvent(handle, &release);
	QApplication::processEvents();
	const QList<int> after = splitter->sizes();
	// Dragging the handle gives the sections of a route more room and keeps the buttons of the table in view.
	QCOMPARE(before.size(), qsizetype(2));
	QCOMPARE(after.size(), qsizetype(2));
	QCOMPARE_LT(after[0], before[0]);
	QCOMPARE_GT(after[1], before[1]);
	QCOMPARE(after[0] + after[1], before[0] + before[1]);
	QCOMPARE_LE(before[0] - after[0], 40 + splitter->handleWidth());
	QVERIFY(inside(add));

	chooseFacet(panel, "tracks");
	QApplication::processEvents();
	// The table takes the room of the sections again.
	QVERIFY(panel.routeSectionDetailWidget()->isHidden());
	QCOMPARE_LE(bottom(table), top(add));
	QVERIFY(inside(add));
}

// Another facet is chosen while a field in a cell has the focus, and the panel is closed with a cell editor open.
void TestInfrastructurePanel::dropsEditorsOnFacetChange() {
	SceneDocument document;
	InfrastructurePanel panel;
	panel.setFacet(std::make_unique<ListFacet>(ListFacet::Kind::Tracks));
	panel.setFacet(std::make_unique<ListFacet>(ListFacet::Kind::Nodes));
	panel.setDocument(&document);
	document.replace(infrastructureScene(), false);
	panel.refresh();
	panel.show();
	panel.activateWindow();
	QTableWidget* table = panel.table();
	auto* field = new QComboBox();
	table->setCellWidget(0, 0, field);
	field->setFocus();
	QApplication::processEvents();
	QVERIFY2(field->hasFocus(), "a field in a cell has the focus");

	chooseFacet(panel, "nodes");
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	const QWidget* editor = table->indexWidget(table->currentIndex());
	int strayEditors = 0;
	for (const QWidget* child : table->viewport()->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
		strayEditors += !child->isHidden() && child != editor ? 1 : 0;
	// The table of the other facet shows no editor besides the one of its current cell.
	QCOMPARE(strayEditors, 0);

	// The panel is destroyed at the end of the slot with this editor open, which must not crash.
	table->setCurrentCell(1, 0);
	QApplication::processEvents();
	QVERIFY2(table->indexWidget(table->currentIndex()) != nullptr, "the current cell has an editor");
}

// Return ends the edit of a cell and Add selects the new row, and neither opens an editor on the first cell.
void TestInfrastructurePanel::returnAndAddLeaveNoEditor() {
	SceneDocument document;
	InfrastructurePanel panel;
	panel.setFacet(std::make_unique<ListFacet>(ListFacet::Kind::Tracks));
	panel.setFacet(std::make_unique<PairFacet>());
	panel.setDocument(&document);
	document.replace(infrastructureScene(), false);
	panel.refresh();
	panel.show();
	panel.activateWindow();
	QApplication::processEvents();
	QTableWidget* table = panel.table();
	const auto shownEditors = [table] {
		int count = 0;
		for (const QWidget* child : table->viewport()->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
			count += child->isHidden() ? 0 : 1;
		return count;
	};

	chooseFacet(panel, "platforms");
	table->setCurrentCell(1, 1);
	auto* editor = qobject_cast<QLineEdit*>(table->indexWidget(table->currentIndex()));
	if (editor == nullptr) QFAIL("a cell that becomes current opens its editor");
	editor->setText("P9");
	QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
	QApplication::sendEvent(editor, &press);
	// The delegate commits the editor in a queued call.
	QApplication::processEvents();
	// Return ends the edit, and the cell stays current in the table without an editor.
	QVERIFY(document.isDirty());
	QCOMPARE(shownEditors(), 0);
	QCOMPARE(table->currentRow(), 1);
	QCOMPARE(table->currentColumn(), 1);
	QVERIFY(table->hasFocus());

	chooseFacet(panel, "tracks");
	panel.addButton()->click();
	QApplication::processEvents();
	// Add selects the new row without an editor.
	QCOMPARE(document.model().tracks.size(), std::size_t(3));
	QCOMPARE(table->currentRow(), 2);
	QCOMPARE(shownEditors(), 0);
}

QTEST_MAIN(TestInfrastructurePanel)
#include "test_editor_infrastructure.moc"
