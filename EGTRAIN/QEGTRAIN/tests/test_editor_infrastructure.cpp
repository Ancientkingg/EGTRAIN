#include "editors/InfrastructurePanel.h"
#include "editors/infrastructure/BlockRouteFacets.h"
#include "editors/infrastructure/SignalFacets.h"
#include "editors/infrastructure/StationFacets.h"
#include "editors/infrastructure/TopologyFacets.h"

#include "graphics/NetworkPresenter.h"
#include "graphics/NetworkScene.h"
#include "scene/SectionInventory.h"
#include "scene/SignallingLevel.h"

#include "support/FakePrompts.h"
#include "widgets/EditorLayout.h"

#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPushButton>
#include <QScopeGuard>
#include <QSplitter>
#include <QString>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStringList>
#include <QTableWidget>
#include <QTest>

#include <algorithm>
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

// The rows that the filters of the table show.
static std::vector<int> shownRows(const QTableWidget* table) {
	std::vector<int> rows;
	for (int row = 0; row < table->rowCount(); ++row)
		if (!table->isRowHidden(row))
			rows.push_back(row);
	return rows;
}

// The texts in `column` of the rows that the filters of the table show.
static QStringList shownTexts(const QTableWidget* table, int column = 0) {
	QStringList texts;
	for (const int row : shownRows(table))
		texts << table->item(row, column)->text();
	return texts;
}

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
	void commitsOpenCellEditor();
	void leavesOpenEditorDuringOwnEdit();
	void widensOnlyReferenceEditor();
	void saysWhatTheCanvasMarks();
	void toldSelectionIsComplete();
	void namesNoMarkWithoutPreview();
	void selectsRowOfCanvasItem_data();
	void selectsRowOfCanvasItem();
	void selectsCanvasItemInListedTable();
	void canvasItemWithoutRowChangesNothing();
	void takesOpenEditorBeforeCanvasItem();
	void filtersRowsByText();
	void filtersRowsByTrack();
	void editsAndDeletesFilteredRows();
	void clearsFiltersForAddAndInsert();
	void showsHiddenRowOfCanvasItem();
	void checksFlagWithOneClick();
	void scrollsToCanvasRowInWindow();
	void keepsWidthForEveryKind();
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

	panel.table()->setCurrentCell(0, 0);
	QApplication::processEvents();
	const QLabel* markLine = panel.findChild<QLabel*>("infrastructureMarkLabel");
	// The line that says what the canvas marks of the selected row is between the table and the buttons.
	if (markLine == nullptr)
		QFAIL("the panel has a line under the table that says what the canvas marks");
	QVERIFY(markLine->isVisible());
	QCOMPARE_LE(bottom(table), top(markLine));
	QCOMPARE_LE(bottom(markLine), top(add));
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
	// The panel is hidden before it is destroyed, also when a check fails.
	const auto hide = qScopeGuard([&panel] { panel.hide(); });
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

// The window's commit takes the text of a cell whose editor is still open, as leaving the cell does, and closes the
// editor. Without an open editor it takes nothing, it leaves the combos that facets put in cells alone, and while the
// scene is closed for edits the text changes nothing.
void TestInfrastructurePanel::commitsOpenCellEditor() {
	SceneDocument document;
	InfrastructurePanel panel;
	for (std::unique_ptr<InfrastructureFacet>& facet : makeTopologyFacets())
		panel.setFacet(std::move(facet));
	for (std::unique_ptr<InfrastructureFacet>& facet : makeSignalFacets())
		panel.setFacet(std::move(facet));
	panel.setDocument(&document);
	SceneModel scene = infrastructureScene();
	SceneSignal signal;
	signal.id = "sig1";
	sceneSignals(scene) = {signal};
	document.replace(scene, false);
	chooseFacet(panel, "tracks");
	panel.refresh();
	panel.show();
	panel.activateWindow();
	QApplication::processEvents();
	Events events(panel, document);
	// The panel is hidden before it is destroyed, also when a check fails.
	const auto hide = qScopeGuard([&panel] { panel.hide(); });
	QTableWidget* table = panel.table();
	const SceneModel& model = document.model();
	const auto openEditor = [table](int row) {
		table->editItem(table->item(row, 0));
		return qobject_cast<QLineEdit*>(table->indexWidget(table->model()->index(row, 0)));
	};
	const auto hasEditor = [table](int row) { return table->indexWidget(table->model()->index(row, 0)) != nullptr; };

	panel.commitPendingEditors();
	// Without an open editor nothing is taken.
	QVERIFY(events.log.empty());

	QLineEdit* editor = openEditor(0);
	if (editor == nullptr)
		QFAIL("the cell has no editor");
	editor->setText("T9");
	panel.commitPendingEditors();
	// The typed id renames the track and what lies on it, as one edit, and the editor is closed.
	QCOMPARE(model.tracks[0].id, "T9");
	QCOMPARE(model.nodes[0].trackId, "T9");
	QCOMPARE(model.arcs[0].trackId, "T9");
	QCOMPARE(model.blocks[0].trackId, "T9");
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Rename track");
	QCOMPARE(firstColumn(table), QStringList({"T9", "T2"}));
	QVERIFY(!hasEditor(0));
	panel.commitPendingEditors();
	QVERIFY2(events.log == edit, "committing again changes nothing");

	events.log.clear();
	if (openEditor(1) == nullptr)
		QFAIL("the cell has no editor");
	panel.commitPendingEditors();
	// An open editor that holds the stored id changes nothing, and is closed.
	QVERIFY(events.log.empty());
	QVERIFY(!hasEditor(1));

	chooseFacet(panel, "signals");
	auto* section = qobject_cast<QComboBox*>(table->cellWidget(0, 1));
	if (section == nullptr)
		QFAIL("the signal has no section combo");
	section->setCurrentIndex(section->findData("@b1@"));
	QCOMPARE(sceneSignals(model)[0].protectedSection, "@b1@");
	events.log.clear();
	table->setCurrentCell(0, 1);
	panel.commitPendingEditors();
	// The combo, which the table returns as the widget of the current cell, keeps its value, and its cell gets no text.
	QVERIFY(events.log.empty());
	QCOMPARE(sceneSignals(model)[0].protectedSection, "@b1@");
	QVERIFY(table->cellWidget(0, 1) == section);
	QCOMPARE(section->currentData().toString(), "@b1@");
	QVERIFY(table->item(0, 1) == nullptr);

	chooseFacet(panel, "tracks");
	editor = openEditor(1);
	if (editor == nullptr)
		QFAIL("the cell has no editor");
	editor->setText("T7");
	const quint64 revision = document.revision();
	document.setEditable(false);
	panel.commitPendingEditors();
	// While the scene is closed for edits the text of the editor changes nothing.
	QCOMPARE(model.tracks[1].id, "T2");
	QCOMPARE(document.revision(), revision);
	QVERIFY(events.log.empty());
}

// The window answers changed() by taking the text of every panel, also during an edit of the panel itself. The table then
// still shows the rows from before the edit, so the open editor is left alone, and filling the table again closes it.
void TestInfrastructurePanel::leavesOpenEditorDuringOwnEdit() {
	SceneDocument document;
	InfrastructurePanel panel;
	auto tracks = std::make_unique<ListFacet>(ListFacet::Kind::Tracks);
	ListFacet* trackFacet = tracks.get();
	panel.setFacet(std::move(tracks));
	panel.setDocument(&document);
	document.replace(infrastructureScene(), false);
	panel.refresh();
	panel.show();
	panel.activateWindow();
	QApplication::processEvents();
	Events events(panel, document);
	// The panel is hidden before it is destroyed, also when a check fails.
	const auto hide = qScopeGuard([&panel] { panel.hide(); });
	QObject::connect(&panel, &InfrastructurePanel::changed, &panel, [&panel] { panel.commitPendingEditors(); });
	QTableWidget* table = panel.table();
	table->editItem(table->item(0, 0));
	auto* editor = qobject_cast<QLineEdit*>(table->indexWidget(table->model()->index(0, 0)));
	if (editor == nullptr)
		QFAIL("the cell has no editor");
	editor->setText("T9");
	panel.addButton()->click();
	// The track is added as the only edit and is selected, and the first track keeps its id.
	QCOMPARE(events.log, edit);
	QCOMPARE(document.lastChangeLabel(), "Add entity");
	QVERIFY(trackFacet->committed.empty());
	QCOMPARE(firstColumn(table), QStringList({"T1", "T2", "T3"}));
	QCOMPARE(panel.selectionId(), "T3");
	QVERIFY(table->indexWidget(table->model()->index(0, 0)) == nullptr);
}

// The editor that the table opens on a cell of references is as wide as the label of its entity, over the cells to its
// right. The combos that facets put in cells stay in their columns, such as the entrance and the exit of a new station
// boundary in a scene without sections, which offer only "(none)".
void TestInfrastructurePanel::widensOnlyReferenceEditor() {
	SceneDocument document;
	InfrastructurePanel panel;
	for (auto* makeFacets : {&makeTopologyFacets, &makeStationFacets})
		for (std::unique_ptr<InfrastructureFacet>& facet : (*makeFacets)())
			panel.setFacet(std::move(facet));
	panel.setDocument(&document);
	// The tracks, nodes and arcs of the scene, without blocks, so that it has no sections yet, as a new case.
	SceneModel scene = infrastructureScene();
	scene.blocks.clear();
	scene.routes.clear();
	document.replace(scene, false);
	panel.refresh();
	panel.resize(700, 400);
	panel.show();
	// The panel is hidden before it is destroyed, also when a check fails.
	const auto hide = qScopeGuard([&panel] { panel.hide(); });
	QApplication::processEvents();
	QTableWidget* table = panel.table();
	// The last pixel of a column; the next column starts after it.
	const auto columnRight = [table](int column) { return table->columnViewportPosition(column) + table->columnWidth(column) - 1; };

	chooseFacet(panel, "arcs");
	table->editItem(table->item(0, 2));
	QApplication::processEvents();
	auto* from = qobject_cast<QComboBox*>(table->indexWidget(table->model()->index(0, 2)));
	if (from == nullptr)
		QFAIL("the start of an arc has no combo");
	QCOMPARE(from->currentText(), "n11 (0.000 km)");
	// The combo is wider than its column and covers the next one, so that the label shows in full.
	QCOMPARE_GT(from->width(), table->columnWidth(2));
	QCOMPARE_GT(from->geometry().right(), columnRight(2));
	panel.commitPendingEditors();

	chooseFacet(panel, "station_boundaries");
	panel.addButton()->click();
	// The table places the widgets of its cells again in layouts that it queues.
	for (int pass = 0; pass < 4; ++pass)
		QApplication::processEvents();
	QCOMPARE(table->rowCount(), 1);
	auto* entranceCombo = qobject_cast<QComboBox*>(table->cellWidget(0, 0));
	auto* exitCombo = qobject_cast<QComboBox*>(table->cellWidget(0, 1));
	if (entranceCombo == nullptr || exitCombo == nullptr)
		QFAIL("a station boundary has no section combos");
	QCOMPARE(entranceCombo->currentText(), "(none)");
	QCOMPARE(exitCombo->currentText(), "(none)");
	// Each combo ends in its own column: the arrow of the entrance is not under the exit, and the exit leaves the check
	// box of the direction free.
	QCOMPARE_LE(entranceCombo->geometry().right(), columnRight(0));
	QCOMPARE_LE(exitCombo->geometry().right(), columnRight(1));
}

// Gives the panel the facets of the window and the document the committed scene Paimpol, with a signal, a block
// dependency and a station boundary added, which Paimpol does not have.
static void openPaimpol(InfrastructurePanel& panel, SceneDocument& document) {
	for (auto* makeFacets : {&makeTopologyFacets, &makeBlockRouteFacets, &makeStationFacets, &makeSignalFacets})
		for (std::unique_ptr<InfrastructureFacet>& facet : (*makeFacets)())
			panel.setFacet(std::move(facet));
	SceneModel scene = loadScene(QFINDTESTDATA("../Scenes/Paimpol").toStdString()).scene;
	sceneSignals(scene).push_back({"sig.36", "36-B0"});
	scene.blockDependencies.push_back({"36-B0", "38-B0"});
	scene.stationBoundaries.push_back({"40-B0", true, "42-B0", false});
	document.replace(std::move(scene), false);
	panel.setDocument(&document);
	panel.refresh();
}

// A panel with the facets of the window on Paimpol (openPaimpol).
struct PaimpolPanel {
	SceneDocument document;
	InfrastructurePanel panel;

	PaimpolPanel() { openPaimpol(panel, document); }

	// Selects the row of `facet` whose cell in `column` reads `text`, or the row with that number for `column` -1, and
	// returns the line under the table; empty while it is hidden.
	QString markFor(const char* facet, const QString& text, int column = 0) {
		chooseFacet(panel, facet);
		QTableWidget* table = panel.table();
		for (int row = 0; row < table->rowCount(); ++row) {
			const QTableWidgetItem* cell = column < 0 ? nullptr : table->item(row, column);
			if (column < 0 ? QString::number(row) == text : cell && cell->text() == text)
				table->setCurrentCell(row, 0);
		}
		return panel.markLabel()->isHidden() ? QString() : panel.markLabel()->text();
	}
};

// The line under the table says what the canvas marks of the selected row, with the ids, tracks, kilometres and section
// labels that the table shows, and is hidden without a row.
void TestInfrastructurePanel::saysWhatTheCanvasMarks() {
	PaimpolPanel f;
	QCOMPARE(f.markFor("tracks", "B5"), "B5: the whole track, 30.866 to 31.365 km");
	QCOMPARE(f.markFor("nodes", "B0.node.41"), "B0.node.41: node on B0 at 2.520 km");
	QCOMPARE(f.markFor("arcs", "B0.arc.100"), "B0.arc.100: arc on B0 from 0.000 to 0.050 km");
	f.panel.blockTrackFilterCombo()->setCurrentIndex(f.panel.blockTrackFilterCombo()->findData("B0"));
	QCOMPARE(f.markFor("blocks", "36-B0"), "36-B0: section B0 10.510 to 10.810 km");
	QCOMPARE(f.markFor("connections", "connection.5"), "connection.5: from B0 at 9.539 km to B4 at 9.549 km");
	QCOMPARE(f.markFor("stations", "Gourland"), "Gourland: station mark and platform node on B0 at 2.520 km");
	QCOMPARE(f.markFor("stations", "Tregonneau_Squiffiec"), "Tregonneau_Squiffiec: station mark and 2 platform nodes on B0 and B4");
	QCOMPARE(f.markFor("platforms", "Gourland.platform.1", 2), "Gourland.platform.1: node B0.node.41 at 2.520 km");
	QCOMPARE(f.markFor("signals", "sig.36"), "sig.36: protects B0 10.510 to 10.810 km");
	QCOMPARE(f.markFor("signalling_areas", "network-fixed-block"), "network-fixed-block: 0.000 to 38.000 km on every track");
	QCOMPARE(f.markFor("routes", "route2"), "route2: 124 sections on B3, B0, B4, B5 and B2");
	QCOMPARE(f.markFor("block_dependencies", "0", -1),
		"Block dependency: B0 10.510 to 10.810 km depends on B0 11.110 to 11.410 km");
	QCOMPARE(f.markFor("single_track_restrictions", "0", -1),
		"Single-track restriction: B0 0.310 to 9.310 km, protected at B3 0.230 to 0.250, B0 0.300 to 0.310 km and at B0 9.310 "
		"to 9.539, B4 9.549 to 9.675 km");
	QCOMPARE(f.markFor("station_boundaries", "0", -1),
		"Station boundary: entrance B0 11.710 to 12.010 km, exit B0 12.310 to 12.610 km");
	// A row of a kind without an id is selected by its number.
	QCOMPARE(f.panel.selectionId(), "0");
	f.panel.table()->setCurrentCell(-1, -1);
	QVERIFY2(f.panel.markLabel()->isHidden(), "without a row the line is hidden");
}

// When the panel tells the selection, the controls that depend on it show it: the sections of the selected route, and
// the section chosen under Route sections, which is told again when another one is chosen.
void TestInfrastructurePanel::toldSelectionIsComplete() {
	PaimpolPanel f;
	Events events(f.panel, f.document);
	QStringList firstSections;
	QStringList lines;
	QObject::connect(&f.panel, &InfrastructurePanel::selectionChanged, &f.panel, [&f, &firstSections, &lines]() {
		const QListWidgetItem* first = f.panel.routeSectionListWidget()->item(0);
		firstSections << (first ? first->toolTip() : QString());
		lines << f.panel.markLabel()->text();
	});
	f.markFor("routes", "route0");
	firstSections.clear();
	lines.clear();
	f.markFor("routes", "route2");
	QCOMPARE(firstSections, QStringList{"0-B3"});
	QCOMPARE(lines, QStringList{"route2: 124 sections on B3, B0, B4, B5 and B2"});
	events.selections.clear();
	f.panel.routeSectionListWidget()->setCurrentRow(3);
	QCOMPARE(events.selections, std::vector<Selection>({Selection("routes", "route2")}));
	QCOMPARE(f.panel.selectedRouteSection(), 3);
	chooseFacet(f.panel, "tracks");
	QCOMPARE(f.panel.selectedRouteSection(), -1);
	// An edit grows the model generation before the selection is told again, so that the window draws the preview again.
	std::vector<quint64> generations;
	QObject::connect(&f.panel, &InfrastructurePanel::selectionChanged, &f.panel,
		[&f, &generations]() { generations.push_back(f.document.modelGeneration()); });
	const quint64 before = f.document.modelGeneration();
	f.markFor("nodes", "B0.node.41");
	generations.clear();
	f.panel.table()->item(f.panel.table()->currentRow(), 3)->setText("2.53");
	QVERIFY(f.document.isDirty());
	QVERIFY(!generations.empty());
	QCOMPARE_GT(generations.front(), before);
	QCOMPARE(generations.back(), f.document.modelGeneration());
}

// The line under the table is empty while the canvas marks no row: while a run uses the document, and while the window
// hides it because the canvas shows a run or its results.
void TestInfrastructurePanel::namesNoMarkWithoutPreview() {
	PaimpolPanel f;
	const QString node = "B0.node.41: node on B0 at 2.520 km";
	QCOMPARE(f.markFor("nodes", "B0.node.41"), node);
	f.document.setEditable(false);
	f.panel.refresh();
	QVERIFY2(f.panel.markLabel()->isHidden() && f.panel.markLabel()->text().isEmpty(), "a run marks no row");
	f.document.setEditable(true);
	f.panel.refresh();
	QCOMPARE(f.panel.markLabel()->text(), node);
	QVERIFY(!f.panel.markLabel()->isHidden());

	f.panel.setMarkShown(false);
	QVERIFY2(f.panel.markLabel()->isHidden() && f.panel.markLabel()->text().isEmpty(), "the results of a run mark no row");
	QCOMPARE(f.markFor("nodes", "B0.node.42"), QString());
	QVERIFY2(f.panel.markLabel()->text().isEmpty(), "another row is not named either");
	f.panel.setMarkShown(true);
	QCOMPARE(f.panel.markLabel()->text(), "B0.node.42: node on B0 at 2.581 km");
	QVERIFY(!f.panel.markLabel()->isHidden());
}

// The Paimpol panel, shown, with the track preview of its document drawn on a scene as the network view of the window draws
// it.
struct PaimpolCanvas : PaimpolPanel {
	QGraphicsScene scene;
	NetworkPresenter presenter{&scene, nullptr};

	PaimpolCanvas() {
		presenter.showPreview(document.model());
		panel.resize(700, 400);
		panel.show();
		QApplication::processEvents();
	}
	PaimpolCanvas(const PaimpolCanvas&) = delete;
	PaimpolCanvas& operator=(const PaimpolCanvas&) = delete;
	~PaimpolCanvas() { panel.hide(); }

	// The item of the preview with this kind and id, or null.
	QGraphicsItem* item(const QString& kind, const QString& id) const {
		for (QGraphicsItem* candidate : scene.items())
			if (candidate->data(PreviewGraphics::Kind).toString() == kind && candidate->data(PreviewGraphics::Id).toString() == id)
				return candidate;
		return nullptr;
	}
	// Selects the row of the item as the window does for a click on it.
	bool click(const QGraphicsItem& clicked) {
		return panel.selectCanvasItem(clicked.data(PreviewGraphics::Kind).toString(), clicked.data(PreviewGraphics::Id).toString(),
			clicked.data(PreviewGraphics::PlatformId).toString());
	}
	// The texts of the first cells of the selected row, as many as `count`.
	QStringList selectedCells(qsizetype count) const {
		QStringList cells;
		const QTableWidget* table = panel.table();
		for (int column = 0; column < count && table->currentRow() >= 0; ++column)
			cells << (table->item(table->currentRow(), column) ? table->item(table->currentRow(), column)->text() : QString());
		return cells;
	}
	// The selected row is in the visible part of the table.
	bool selectionInView() const {
		const QTableWidget* table = panel.table();
		const QTableWidgetItem* cell = table->currentRow() < 0 ? nullptr : table->item(table->currentRow(), 0);
		return cell && table->viewport()->rect().intersects(table->visualItemRect(cell));
	}
};

void TestInfrastructurePanel::selectsRowOfCanvasItem_data() {
	QTest::addColumn<QString>("kind");
	QTest::addColumn<QString>("id");
	// Whether the preview of Paimpol draws the item. A drawn item gives its own platform.
	QTest::addColumn<bool>("drawn");
	QTest::addColumn<QString>("facet");
	// The first cells of the row that is selected.
	QTest::addColumn<QStringList>("cells");
	QTest::newRow("node") << QStringLiteral("node") << QStringLiteral("B0.node.42") << true << QStringLiteral("nodes")
						  << QStringList{"B0.node.42", "", "B0"};
	QTest::newRow("platform node") << QStringLiteral("station-node") << QStringLiteral("B0.node.41") << true
								   << QStringLiteral("platforms") << QStringList{"Gourland", "Gourland", "Gourland.platform.1"};
	QTest::newRow("station node without platform") << QStringLiteral("station-node") << QStringLiteral("B0.node.42") << false
												   << QStringLiteral("nodes") << QStringList{"B0.node.42", "", "B0"};
	QTest::newRow("track") << QStringLiteral("track") << QStringLiteral("B5") << false << QStringLiteral("tracks")
						   << QStringList{"B5"};
	QTest::newRow("arc") << QStringLiteral("arc") << QStringLiteral("B5.arc.101") << true << QStringLiteral("arcs")
						 << QStringList{"B5.arc.101", "B5"};
	QTest::newRow("connection") << QStringLiteral("connection") << QStringLiteral("connection.5") << true
								<< QStringLiteral("connections") << QStringList{"connection.5", "B0.node.101", "B4.node.3"};
	QTest::newRow("station") << QStringLiteral("station") << QStringLiteral("Frynaudour") << true << QStringLiteral("stations")
							 << QStringList{"Frynaudour"};
	QTest::newRow("signal head") << QStringLiteral("signal") << QStringLiteral("@2-B4@") << true << QStringLiteral("blocks")
								 << QStringList{"2-B4", "B4"};
}

// An item of the preview of Paimpol selects its row: the combo shows its kind, the row is selected and in view, and the
// selection is told once. A signal head names the section of a block, whose track the filter of the blocks then shows.
void TestInfrastructurePanel::selectsRowOfCanvasItem() {
	QFETCH(QString, kind);
	QFETCH(QString, id);
	QFETCH(bool, drawn);
	QFETCH(QString, facet);
	QFETCH(QStringList, cells);
	PaimpolCanvas f;
	// Another kind is listed, with a row selected.
	f.markFor("routes", "route2");
	QGraphicsItem* clicked = drawn ? f.item(kind, id) : nullptr;
	if (drawn && clicked == nullptr)
		QFAIL("the preview of Paimpol does not draw the item");
	Events events(f.panel, f.document);
	QVERIFY(clicked ? f.click(*clicked) : f.panel.selectCanvasItem(kind, id));
	QCOMPARE(f.panel.facetKey(), facet);
	QCOMPARE(f.panel.facetCombo()->currentData().toString(), facet);
	QCOMPARE(f.selectedCells(cells.size()), cells);
	QCOMPARE(events.selections, std::vector<Selection>({Selection(facet, f.panel.selectionId())}));
	QVERIFY2(f.selectionInView(), "the row is in view");
	QVERIFY2(f.panel.table()->indexWidget(f.panel.table()->currentIndex()) == nullptr, "no cell editor opens");
	QVERIFY(events.log.empty());
	QVERIFY(!f.document.isDirty());
	if (facet == QLatin1String("blocks"))
		QCOMPARE(f.panel.blockTrackFilterCombo()->currentData().toString(), cells.value(1));
}

// While the table lists the row already, selecting it scrolls to it and tells the selection once, and selecting it again
// tells nothing.
void TestInfrastructurePanel::selectsCanvasItemInListedTable() {
	PaimpolCanvas f;
	f.markFor("nodes", "B0.node.1");
	Events events(f.panel, f.document);
	QVERIFY(f.panel.selectCanvasItem("node", "B0.node.300"));
	QCOMPARE(f.selectedCells(1), QStringList{"B0.node.300"});
	QCOMPARE(events.selections, std::vector<Selection>({Selection("nodes", "B0.node.300")}));
	QVERIFY2(f.selectionInView(), "the table scrolls to the row");
	QVERIFY2(f.panel.table()->indexWidget(f.panel.table()->currentIndex()) == nullptr, "no cell editor opens");
	events.selections.clear();
	QVERIFY(f.panel.selectCanvasItem("node", "B0.node.300"));
	QVERIFY2(events.selections.empty(), "a row that is selected already is not told again");

	// A block on the track of the filter is selected in the listed table.
	f.panel.blockTrackFilterCombo()->setCurrentIndex(f.panel.blockTrackFilterCombo()->findData("B0"));
	f.markFor("blocks", "0-B0");
	events.selections.clear();
	QVERIFY(f.panel.selectCanvasItem("signal", "@36-B0@"));
	QCOMPARE(f.selectedCells(2), QStringList({"36-B0", "B0"}));
	QCOMPARE(events.selections, std::vector<Selection>({Selection("blocks", "36-B0")}));
}

// An item without a row changes nothing and tells nothing: an id that the scene does not have, a kind without rows and a
// switch section, which no block has. Nor does any item while the scene cannot be edited, as during a run.
void TestInfrastructurePanel::canvasItemWithoutRowChangesNothing() {
	PaimpolCanvas f;
	f.markFor("nodes", "B0.node.41");
	const SceneSectionInventory inventory = buildSceneSectionInventory(f.document.model());
	const auto switchSection = std::find_if(inventory.sections.begin(), inventory.sections.end(),
		[](const SceneSectionDescriptor& section) { return section.connectionDerived; });
	if (switchSection == inventory.sections.end())
		QFAIL("Paimpol has no switch section");
	Events events(f.panel, f.document);
	const std::vector<std::pair<QString, QString>> withoutRow = {{"node", "no-such-node"}, {"arc", "B0.node.42"},
		{"train", "B0.node.42"}, {"station-node", "no-such-node"}, {"signal", QString::fromStdString(switchSection->id)},
		{"signal", "no-such-section"}};
	for (const auto& [kind, id] : withoutRow) {
		QVERIFY2(!f.panel.selectCanvasItem(kind, id, "Gourland.platform.1"), qPrintable(kind + " " + id));
		QCOMPARE(f.panel.facetKey(), "nodes");
		QCOMPARE(f.panel.selectionId(), "B0.node.41");
	}
	QVERIFY(events.selections.empty());

	f.document.setEditable(false);
	f.panel.refresh();
	events.selections.clear();
	QVERIFY(!f.panel.selectCanvasItem("arc", "B5.arc.101"));
	QVERIFY(!f.panel.selectCanvasItem("node", "B0.node.42"));
	QCOMPARE(f.panel.facetKey(), "nodes");
	QCOMPARE(f.panel.selectionId(), "B0.node.41");
	QVERIFY(events.selections.empty());
	QVERIFY(events.log.empty());
}

// The text of a cell that is still being edited is taken into the model before the row of the item is selected, as
// choosing another row takes it.
void TestInfrastructurePanel::takesOpenEditorBeforeCanvasItem() {
	PaimpolCanvas f;
	f.panel.activateWindow();
	f.markFor("nodes", "B0.node.41");
	// Choosing the row opened an editor on its id, which is taken unchanged.
	f.panel.commitPendingEditors();
	QTableWidget* table = f.panel.table();
	const int row = table->currentRow();
	table->editItem(table->item(row, 3));
	auto* editor = qobject_cast<QLineEdit*>(table->indexWidget(table->model()->index(row, 3)));
	if (editor == nullptr)
		QFAIL("the cell has no editor");
	editor->setText("2.53");
	Events events(f.panel, f.document);
	QVERIFY(f.panel.selectCanvasItem("arc", "B5.arc.101"));
	QCOMPARE(f.document.model().nodes[40].id, "B0.node.41");
	QCOMPARE(f.document.model().nodes[40].xKm, 2.53);
	QCOMPARE(f.document.lastChangeLabel(), "Edit node");
	QCOMPARE(f.panel.facetKey(), "arcs");
	QCOMPARE(f.panel.selectionId(), "B5.arc.101");
	QCOMPARE(events.selections.back(), Selection("arcs", "B5.arc.101"));
}

// The filter above the table shows the rows that hold every word of it in their cells, ignoring case, and the line
// above the table counts them. A row keeps its place, so it stays the entity at that index. The filter stays when the
// kind changes and when the table is filled again.
void TestInfrastructurePanel::filtersRowsByText() {
	PaimpolPanel f;
	chooseFacet(f.panel, "nodes");
	const QTableWidget* table = f.panel.table();
	QLineEdit* filter = f.panel.filterEdit();
	const QLabel* count = f.panel.countLabel();
	// Every node is listed and counted.
	QVERIFY(!filter->isHidden());
	QCOMPARE(shownRows(table).size(), std::size_t(399));
	QVERIFY(!count->isHidden());
	QCOMPARE(count->text(), "399 nodes");

	filter->setText("NODE.1 b4");
	// The words can be in different cells and in another case.
	QCOMPARE(shownTexts(table), QStringList({"B4.node.1", "B4.node.10"}));
	QCOMPARE(count->text(), "2 of 399 nodes");
	for (const int row : shownRows(table))
		QCOMPARE(QString::fromStdString(f.document.model().nodes[static_cast<std::size_t>(row)].id), table->item(row, 0)->text());
	filter->setText("gourland");
	// The name of the station of a node is one of its cells.
	QCOMPARE(shownTexts(table), QStringList({"B0.node.41"}));
	QCOMPARE(count->text(), "1 of 399 nodes");
	filter->setText("nowhere");
	// A filter that no row matches shows none, and no row is selected.
	QVERIFY(shownRows(table).empty());
	QCOMPARE(count->text(), "0 of 399 nodes");
	QCOMPARE(table->currentRow(), -1);
	filter->setText("   ");
	// A filter without words shows every row.
	QCOMPARE(count->text(), "399 nodes");

	filter->setText("b4 node.1");
	chooseFacet(f.panel, "arcs");
	QStringList arcs;
	for (const SceneArc& arc : f.document.model().arcs) {
		const QStringList cells = {QString::fromStdString(arc.id), QString::fromStdString(arc.trackId),
			QString::fromStdString(arc.fromNodeId), QString::fromStdString(arc.toNodeId)};
		const auto holds = [&cells](const char* word) {
			return std::any_of(cells.begin(), cells.end(), [word](const QString& cell) { return cell.contains(word, Qt::CaseInsensitive); });
		};
		if (holds("b4") && holds("node.1"))
			arcs << cells.front();
	}
	// The filter stays when the kind changes, and filters the arcs.
	QCOMPARE(filter->text(), "b4 node.1");
	QVERIFY(!arcs.isEmpty());
	QCOMPARE(shownTexts(table), arcs);
	QCOMPARE(count->text(), QStringLiteral("%1 of 393 arcs").arg(arcs.size()));
	f.panel.refresh();
	// Filling the table again filters it again.
	QCOMPARE(shownTexts(table), arcs);
	chooseFacet(f.panel, "routes");
	filter->clear();
	// Another kind counts its rows the same way.
	QCOMPARE(count->text(), QStringLiteral("%1 routes").arg(f.document.model().routes.size()));
}

// The track filter of nodes, arcs, connections and platforms shows the rows on the chosen track: a node on it, an arc on
// it, a connection with an end on it, and a platform with a node on it. Blocks keep their own filter, and tracks have none.
void TestInfrastructurePanel::filtersRowsByTrack() {
	PaimpolPanel f;
	const SceneModel& model = f.document.model();
	QComboBox* tracks = f.panel.trackFilterCombo();
	const QTableWidget* table = f.panel.table();
	const QLabel* count = f.panel.countLabel();
	const auto onB4 = [&model](const std::string& nodeId) {
		return std::any_of(model.nodes.begin(), model.nodes.end(),
			[&nodeId](const SceneNode& node) { return node.id == nodeId && node.trackId == "B4"; });
	};

	chooseFacet(f.panel, "nodes");
	// The filter offers every track, and shows all of them at first.
	QVERIFY(!tracks->isHidden());
	QCOMPARE(choices(tracks), QStringList({"All tracks", "B0", "B1", "B2", "B3", "B4", "B5"}));
	QCOMPARE(tracks->currentIndex(), 0);
	tracks->setCurrentIndex(tracks->findData("B4"));
	std::vector<int> nodes;
	for (std::size_t index = 0; index < model.nodes.size(); ++index)
		if (model.nodes[index].trackId == "B4")
			nodes.push_back(static_cast<int>(index));
	// The nodes on the track are shown.
	QCOMPARE(shownRows(table), nodes);
	QCOMPARE(count->text(), "10 of 399 nodes");

	chooseFacet(f.panel, "arcs");
	std::vector<int> arcs;
	for (std::size_t index = 0; index < model.arcs.size(); ++index)
		if (model.arcs[index].trackId == "B4")
			arcs.push_back(static_cast<int>(index));
	// The filter stays when the kind changes, and shows the arcs on the track.
	QVERIFY(!tracks->isHidden());
	QCOMPARE(tracks->currentData().toString(), "B4");
	QCOMPARE(shownRows(table), arcs);
	QCOMPARE(count->text(), "9 of 393 arcs");

	chooseFacet(f.panel, "connections");
	// A connection with one end on the track is shown.
	QCOMPARE(shownTexts(table), QStringList({"connection.4", "connection.5"}));
	QVERIFY(onB4(model.connections[4].fromNodeId) && !onB4(model.connections[4].toNodeId));
	QVERIFY(!onB4(model.connections[5].fromNodeId) && onB4(model.connections[5].toNodeId));
	QCOMPARE(count->text(), "2 of 8 connections");

	chooseFacet(f.panel, "platforms");
	// A platform with a node on the track is shown.
	QCOMPARE(shownTexts(table, 2), QStringList({"Tregonneau_Squiffiec.platform.2"}));
	QCOMPARE(count->text(), "1 of 15 platforms");

	chooseFacet(f.panel, "tracks");
	// Tracks have no track filter, and every track is shown.
	QVERIFY(tracks->isHidden());
	QCOMPARE(count->text(), "6 tracks");
	chooseFacet(f.panel, "blocks");
	// Blocks keep the filter of their own.
	QVERIFY(tracks->isHidden());
	QVERIFY(!f.panel.blockTrackFilterCombo()->isHidden());
	QCOMPARE(shownRows(table).size(), static_cast<std::size_t>(table->rowCount()));

	chooseFacet(f.panel, "nodes");
	f.panel.filterEdit()->setText("node.1");
	// The text filter and the track filter both apply.
	QCOMPARE(shownTexts(table), QStringList({"B4.node.1", "B4.node.10"}));
	tracks->setCurrentIndex(0);
	// All tracks leaves the text filter.
	QCOMPARE_GT(shownRows(table).size(), std::size_t(2));
	QVERIFY(shownTexts(table).contains("B0.node.1"));
}

// In a filtered table an edit, a delete and the line under the table name the entity of the row, and undo brings a
// deleted row back under the same filters.
void TestInfrastructurePanel::editsAndDeletesFilteredRows() {
	PaimpolPanel f;
	FakePrompts prompts;
	f.panel.setPrompts(&prompts);
	QTableWidget* table = f.panel.table();
	chooseFacet(f.panel, "nodes");
	f.panel.trackFilterCombo()->setCurrentIndex(f.panel.trackFilterCombo()->findData("B4"));
	f.panel.filterEdit()->setText("node.10");
	QCOMPARE(shownTexts(table), QStringList({"B4.node.10"}));
	const int nodeRow = shownRows(table).front();
	table->setCurrentCell(nodeRow, 0);
	// The line under the table names the node of the row.
	QVERIFY2(f.panel.markLabel()->text().startsWith("B4.node.10: node on B4"), qPrintable(f.panel.markLabel()->text()));

	Events events(f.panel, f.document);
	table->item(nodeRow, 4)->setText("0.75");
	const SceneNode& node = f.document.model().nodes[static_cast<std::size_t>(nodeRow)];
	// The edit changes the node of the row, once, and the row stays shown and selected.
	QCOMPARE(events.log, edit);
	QCOMPARE(f.document.lastChangeLabel(), "Edit node");
	QCOMPARE(node.id, "B4.node.10");
	QCOMPARE_EQ(node.yKm, 0.75);
	QCOMPARE(shownTexts(table), QStringList({"B4.node.10"}));
	QCOMPARE(table->currentRow(), nodeRow);
	QCOMPARE(f.panel.selectionId(), "B4.node.10");
	f.panel.deleteButton()->click();
	// Delete names the node of the row, which arcs use.
	QCOMPARE(prompts.errorCount, 1);
	QVERIFY2(prompts.errorMessage.startsWith("Node 'B4.node.10' is used by:"), qPrintable(prompts.errorMessage));

	chooseFacet(f.panel, "arcs");
	const SceneModel before = f.document.model();
	QCOMPARE(shownRows(table).size(), std::size_t(1));
	const int arcRow = shownRows(table).front();
	const QString arcId = QString::fromStdString(before.arcs[static_cast<std::size_t>(arcRow)].id);
	QVERIFY(arcRow > 0);
	table->setCurrentCell(arcRow, 0);
	QVERIFY2(f.panel.markLabel()->text().startsWith(arcId + ": arc on B4"), qPrintable(f.panel.markLabel()->text()));
	prompts.confirmAnswer = true;
	events.log.clear();
	f.panel.deleteButton()->click();
	const std::vector<SceneArc>& arcs = f.document.model().arcs;
	// Delete asks about the arc of the row and deletes that one only.
	QCOMPARE(prompts.confirmQuestion, QStringLiteral("Delete arc '%1'?").arg(arcId));
	QCOMPARE(events.log, edit);
	QCOMPARE(f.document.lastChangeLabel(), "Delete arc");
	QCOMPARE(arcs.size(), before.arcs.size() - 1);
	QCOMPARE(arcs[static_cast<std::size_t>(arcRow) - 1].id, before.arcs[static_cast<std::size_t>(arcRow) - 1].id);
	QCOMPARE(arcs[static_cast<std::size_t>(arcRow)].id, before.arcs[static_cast<std::size_t>(arcRow) + 1].id);
	QVERIFY(shownRows(table).empty());
	QCOMPARE(f.panel.countLabel()->text(), "0 of 392 arcs");
	f.document.restore(before, true);
	f.panel.refresh();
	// Undo brings the arc back in its row, under the same filters.
	QCOMPARE(f.document.model().arcs.size(), before.arcs.size());
	QCOMPARE(shownTexts(table), QStringList({arcId}));
	QCOMPARE(shownRows(table).front(), arcRow);
	QCOMPARE(f.panel.countLabel()->text(), "1 of 393 arcs");
}

// Add and Insert clear the filters of the kind, so that the new row is shown. Move Up and Move Down are off while a filter
// hides a row, and the filter of blocks on a track stays.
void TestInfrastructurePanel::clearsFiltersForAddAndInsert() {
	PaimpolPanel f;
	QTableWidget* table = f.panel.table();
	QLineEdit* filter = f.panel.filterEdit();
	QComboBox* tracks = f.panel.trackFilterCombo();
	chooseFacet(f.panel, "nodes");
	tracks->setCurrentIndex(tracks->findData("B4"));
	filter->setText("node.1");
	Events events(f.panel, f.document);
	f.panel.addButton()->click();
	// The node is added once, both filters are cleared, and its row is shown and selected.
	QCOMPARE(events.log, edit);
	QCOMPARE(f.document.lastChangeLabel(), "Add node");
	QVERIFY(filter->text().isEmpty());
	QCOMPARE(tracks->currentIndex(), 0);
	QCOMPARE(f.document.model().nodes.size(), std::size_t(400));
	QCOMPARE(table->currentRow(), 399);
	QVERIFY(!table->isRowHidden(399));
	QCOMPARE(f.panel.selectionId(), QString::fromStdString(f.document.model().nodes.back().id));
	QCOMPARE(f.panel.countLabel()->text(), "400 nodes");

	chooseFacet(f.panel, "blocks");
	QComboBox* blockTracks = f.panel.blockTrackFilterCombo();
	blockTracks->setCurrentIndex(blockTracks->findData("B0"));
	table->setCurrentCell(2, 0);
	// Without a filter a block can move.
	QVERIFY(f.panel.moveBlockUpButton()->isEnabled());
	QVERIFY(f.panel.moveBlockDownButton()->isEnabled());
	filter->setText("3");
	const std::vector<int> shown = shownRows(table);
	QVERIFY(shown.size() > 2 && shown.size() < static_cast<std::size_t>(table->rowCount()));
	table->setCurrentCell(shown[1], 0);
	const QString selected = f.panel.selectionId();
	// While the filter hides a row, the blocks cannot move, and the buttons say why.
	QVERIFY(!f.panel.moveBlockUpButton()->isEnabled());
	QVERIFY(!f.panel.moveBlockDownButton()->isEnabled());
	QCOMPARE(f.panel.moveBlockUpButton()->toolTip(), "Clear the filter to move a row.");
	events.log.clear();
	f.panel.moveEntity(-1);
	QVERIFY(events.log.empty());
	QCOMPARE(f.panel.selectionId(), selected);
	f.panel.insertBlockButton()->click();
	// Insert clears the text filter and keeps the track of the blocks, and its row is shown and selected.
	QCOMPARE(events.log, edit);
	QCOMPARE(f.document.lastChangeLabel(), "Insert block");
	QVERIFY(filter->text().isEmpty());
	QCOMPARE(blockTracks->currentData().toString(), "B0");
	QCOMPARE(shownRows(table).size(), static_cast<std::size_t>(table->rowCount()));
	QVERIFY(table->currentRow() >= 0);
	QVERIFY(f.panel.moveBlockUpButton()->isEnabled());
	QVERIFY(f.panel.moveBlockUpButton()->toolTip().isEmpty());
}

// A canvas item whose row a filter hides clears that filter, and only that one, and its row is selected and told once.
void TestInfrastructurePanel::showsHiddenRowOfCanvasItem() {
	PaimpolPanel f;
	const QTableWidget* table = f.panel.table();
	QLineEdit* filter = f.panel.filterEdit();
	QComboBox* tracks = f.panel.trackFilterCombo();
	chooseFacet(f.panel, "nodes");
	tracks->setCurrentIndex(tracks->findData("B5"));
	filter->setText("node.4");
	Events events(f.panel, f.document);
	QVERIFY(f.panel.selectCanvasItem("node", "B0.node.42"));
	// The node is listed: the track filter that hid it shows all tracks, and the text filter, which matches it, stays.
	QCOMPARE(tracks->currentIndex(), 0);
	QCOMPARE(filter->text(), "node.4");
	QCOMPARE(f.panel.selectionId(), "B0.node.42");
	QVERIFY(!table->isRowHidden(table->currentRow()));
	QCOMPARE(table->item(table->currentRow(), 0)->text(), "B0.node.42");
	QCOMPARE(events.selections, std::vector<Selection>({Selection("nodes", "B0.node.42")}));

	filter->setText("Gourland");
	events.selections.clear();
	QVERIFY(f.panel.selectCanvasItem("node", "B0.node.42"));
	// A text filter that hides the row is cleared.
	QVERIFY(filter->text().isEmpty());
	QCOMPARE(f.panel.selectionId(), "B0.node.42");
	QVERIFY(!table->isRowHidden(table->currentRow()));
	QCOMPARE(events.selections, std::vector<Selection>({Selection("nodes", "B0.node.42")}));

	chooseFacet(f.panel, "routes");
	filter->setText("route0");
	tracks->setCurrentIndex(tracks->findData("B5"));
	events.selections.clear();
	QVERIFY(f.panel.selectCanvasItem("arc", "B0.arc.100"));
	// Another kind shows the row of the item once, without the filters that would hide it.
	QCOMPARE(f.panel.facetKey(), "arcs");
	QVERIFY(filter->text().isEmpty());
	QCOMPARE(tracks->currentIndex(), 0);
	QCOMPARE(f.panel.selectionId(), "B0.arc.100");
	QVERIFY(!table->isRowHidden(table->currentRow()));
	QCOMPARE(events.selections, std::vector<Selection>({Selection("arcs", "B0.arc.100")}));
	QVERIFY(events.log.empty());
}

// One click on a check box of a flag sets the flag in one edit, and opens no editor.
void TestInfrastructurePanel::checksFlagWithOneClick() {
	PaimpolCanvas f;
	f.panel.activateWindow();
	chooseFacet(f.panel, "connections");
	QApplication::processEvents();
	QTableWidget* table = f.panel.table();
	const QTableWidgetItem* box = table->item(0, 3);
	QCOMPARE(table->horizontalHeaderItem(3)->text(), "Has speed limit");
	QVERIFY(!f.document.model().connections[0].hasSpeedLimit);
	QCOMPARE(box->checkState(), Qt::Unchecked);
	QStyleOptionViewItem option;
	option.initFrom(table->viewport());
	option.rect = table->visualItemRect(box);
	option.features = QStyleOptionViewItem::HasCheckIndicator;
	option.checkState = Qt::Unchecked;
	const QRect indicator = table->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, table);
	Events events(f.panel, f.document);
	QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, indicator.center());
	QApplication::processEvents();
	// The flag is set once, the box is checked, and no editor is open.
	QCOMPARE(events.log, edit);
	QCOMPARE(f.document.lastChangeLabel(), "Edit connection");
	QVERIFY(f.document.model().connections[0].hasSpeedLimit);
	QCOMPARE(table->item(0, 3)->checkState(), Qt::Checked);
	QVERIFY(table->indexWidget(table->model()->index(0, 3)) == nullptr);
}

// The Paimpol panel in the Infrastructure dock of a window of 1280 by 800 pixels, in the scroll area that the main window
// puts it in. Above it is a dock as high as the Details dock of the main window, hidden, which a click on the canvas
// shows before the panel selects the row of the item.
struct PaimpolWindow {
	SceneDocument document;
	QMainWindow window;
	QDockWidget* details = new QDockWidget("Details", &window);
	QDockWidget* dock = new QDockWidget("Infrastructure", &window);
	InfrastructurePanel* panel = new InfrastructurePanel;

	PaimpolWindow() {
		auto* form = new QWidget(details);
		form->setMinimumHeight(340);
		details->setWidget(form);
		window.addDockWidget(Qt::RightDockWidgetArea, details);
		details->hide();
		auto* scroll = new EditorLayout::DockScrollArea(dock);
		scroll->setWidgetResizable(true);
		scroll->setWidget(panel);
		dock->setWidget(scroll);
		window.addDockWidget(Qt::RightDockWidgetArea, dock);
		window.setCentralWidget(new QWidget(&window));
		openPaimpol(*panel, document);
		window.resize(1280, 800);
		window.show();
		settle();
	}
	PaimpolWindow(const PaimpolWindow&) = delete;
	PaimpolWindow& operator=(const PaimpolWindow&) = delete;

	// Runs the events that lay out the window, which Qt passes on one level of widgets at a time.
	static void settle() {
		for (int pass = 0; pass < 8; ++pass)
			QApplication::processEvents();
	}
};

// In the window a click on the canvas shows the Details dock above the Infrastructure dock, which makes the table lower,
// and then selects the row of the item. The row of a platform near the end of the list is in view once the window is
// laid out.
void TestInfrastructurePanel::scrollsToCanvasRowInWindow() {
	PaimpolWindow f;
	// The window shows the line under the table, and blocks are listed, with controls that platforms do not have.
	f.panel->setMarkShown(true);
	chooseFacet(*f.panel, "blocks");
	PaimpolWindow::settle();
	const int fullHeight = f.panel->table()->viewport()->height();
	f.details->show();
	QVERIFY(f.panel->selectCanvasItem("station-node", "B0.node.350", "Paimpol.platform.1"));
	PaimpolWindow::settle();
	const QTableWidget* table = f.panel->table();
	QCOMPARE(f.panel->selectionId(), "Paimpol\nPaimpol.platform.1");
	QCOMPARE(table->currentRow(), 13);
	QCOMPARE(table->rowCount(), 15);
	QVERIFY2(table->viewport()->height() < std::min(fullHeight, table->rowHeight(0) * 10), "the table is lower and shows a part of its rows");
	const QRect row = table->visualItemRect(table->item(13, 0));
	QVERIFY2(table->viewport()->rect().contains(row),
		qPrintable(QStringLiteral("row at %1 to %2 of a viewport %3 high").arg(row.top()).arg(row.bottom()).arg(table->viewport()->height())));
}

// The panel needs the same width for every kind, so that the dock keeps its width when the kind changes.
void TestInfrastructurePanel::keepsWidthForEveryKind() {
	PaimpolWindow f;
	std::vector<std::pair<QString, int>> widths;
	for (const QString& key : InfrastructurePanel::facetKeys()) {
		f.panel->facetCombo()->setCurrentIndex(f.panel->facetCombo()->findData(key));
		PaimpolWindow::settle();
		widths.emplace_back(key, f.panel->minimumSizeHint().width());
	}
	QCOMPARE(widths.size(), std::size_t(13));
	for (const auto& [key, width] : widths)
		QVERIFY2(width == widths.front().second, qPrintable(QStringLiteral("%1 is %2 wide, tracks %3").arg(key).arg(width).arg(widths.front().second)));
}

QTEST_MAIN(TestInfrastructurePanel)
#include "test_editor_infrastructure.moc"
