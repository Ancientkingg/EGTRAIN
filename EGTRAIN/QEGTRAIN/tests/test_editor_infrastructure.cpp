#include "editors/InfrastructurePanel.h"

#include "scene/SectionInventory.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSplitter>
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
		if (m_kind == Kind::Tracks) {
			panel.mutableModel().tracks[static_cast<std::size_t>(row)].id = text.toStdString();
			panel.setSelectionId(text);
		}
		panel.entityChanged("Edit entity");
	}
	void add(InfrastructurePanel& panel) override {
		++addCount;
		panel.mutableModel().tracks.push_back({"T" + std::to_string(panel.model().tracks.size() + 1)});
		panel.setSelectionId(QString::fromStdString(panel.model().tracks.back().id));
		panel.entityChanged("Add entity");
	}
	void remove(InfrastructurePanel& panel) override {
		++removeCount;
		const int row = panel.table()->currentRow();
		const QString id = QString::fromStdString(panel.model().tracks[static_cast<std::size_t>(row)].id);
		if (!panel.confirmDelete(id, id == "T1" ? std::vector<std::string>({"block b1"}) : std::vector<std::string>()))
			return;
		panel.mutableModel().tracks.erase(panel.mutableModel().tracks.begin() + row);
		panel.setSelectionId(QString());
		panel.entityChanged("Delete entity");
	}
	void commitPendingEdits(InfrastructurePanel&) override { ++pendingCount; }

private:
	Kind m_kind;
};

// A facet whose selection is made of two cells, like the one of platforms.
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
	void commitCell(InfrastructurePanel&, int, int) override {}
	void add(InfrastructurePanel&) override {}
	void remove(InfrastructurePanel&) override {}
};

// A facet of the blocks on the track that the filter names. It keeps the model index of each row.
class BlockFacet : public InfrastructureFacet {
public:
	QString key() const override { return "blocks"; }
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

static void chooseFacet(InfrastructurePanel& panel, const char* key) {
	panel.facetCombo()->setCurrentIndex(panel.facetCombo()->findData(key));
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	bool ok = true;
	const Log edit({"documentChanged", "panelChanged"});

	// The facets, the combo and a panel without a scene
	{
		InfrastructurePanel panel;
		const QStringList keys = {"tracks", "nodes", "arcs", "blocks", "connections", "stations", "platforms", "signals",
			"signalling_areas", "routes", "block_dependencies", "single_track_restrictions", "station_boundaries"};
		QStringList comboKeys;
		for (int index = 0; index < panel.facetCombo()->count(); ++index)
			comboKeys << panel.facetCombo()->itemData(index).toString();
		ok &= expect(InfrastructurePanel::facetKeys() == keys && comboKeys == keys, "the combo offers the thirteen facets in order");
		ok &= expect(panel.facetCombo()->itemText(0) == "Tracks" && panel.facetCombo()->itemText(8) == "Signalling area"
				&& panel.facetCombo()->itemText(11) == "Single-track restrictions",
			"the combo names the facets");
		ok &= expect(panel.facetKey() == "tracks" && panel.facetTitle() == "Tracks" && panel.facet("tracks") == nullptr,
			"the first facet is chosen, and there is no facet class yet");
		ok &= expect(panel.table()->rowCount() == 0 && panel.table()->columnCount() == 0 && !panel.table()->isEnabled()
				&& !panel.facetCombo()->isEnabled() && !panel.addButton()->isEnabled() && !panel.deleteButton()->isEnabled()
				&& !panel.guidanceLabel()->isHidden() && panel.guidanceLabel()->text() == "Open a case to edit infrastructure.",
			"a panel without a scene lists nothing and asks for a case");
		ok &= expect(panel.blockTrackFilterCombo()->isHidden() && panel.insertBlockButton()->isHidden()
				&& panel.moveBlockUpButton()->isHidden() && panel.moveBlockDownButton()->isHidden()
				&& panel.routeSectionDetailWidget()->isHidden(),
			"the controls of blocks and routes are hidden");
		panel.addEntity();
		panel.deleteEntity();
		panel.commitCell(0, 0);
		panel.commitPendingEditors();
		ok &= expect(panel.selectionId().isEmpty(), "the actions of a panel without a scene do nothing");

		auto tracks = std::make_unique<ListFacet>(ListFacet::Kind::Tracks);
		ListFacet* trackFacet = tracks.get();
		panel.setFacet(std::move(tracks));
		ok &= expect(panel.facet("tracks") == trackFacet && panel.facet("nodes") == nullptr, "a facet is found by its key");
		panel.setFacet(nullptr);
		ok &= expect(panel.facet("tracks") == trackFacet, "no facet is not a facet");
		auto replacement = std::make_unique<ListFacet>(ListFacet::Kind::Tracks);
		ListFacet* replacementFacet = replacement.get();
		panel.setFacet(std::move(replacement));
		ok &= expect(panel.facet("tracks") == replacementFacet, "a facet takes the place of the one with its key");
	}

	// Showing a scene, choosing facets and selecting rows
	{
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

		ok &= expect(panel.table()->rowCount() == 0 && columnTitles(panel.table()) == QStringList({"ID"})
				&& panel.guidanceLabel()->text() == "Open a case to edit infrastructure." && !panel.addButton()->isEnabled(),
			"a document without a scene lists no tracks");

		document.replace(infrastructureScene(), false);
		panel.refresh();
		ok &= expect(columnTitles(panel.table()) == QStringList({"ID"}) && firstColumn(panel.table()) == QStringList({"T1", "T2"})
				&& panel.table()->isEnabled() && panel.facetCombo()->isEnabled() && panel.addButton()->isEnabled()
				&& !panel.deleteButton()->isEnabled() && panel.guidanceLabel()->isHidden(),
			"the tracks are listed in the columns of their facet, and the table can be used");
		ok &= expect(panel.table()->currentRow() == -1 && panel.selectionId().isEmpty()
				&& events.lastSelection() == Selection("tracks", QString()),
			"no row is selected until the user selects one");
		ok &= expect(events.log.empty() && !document.isDirty() && trackFacet->committed.empty(),
			"showing a scene edits nothing");

		events.selections.clear();
		panel.table()->setCurrentCell(1, 0);
		ok &= expect(events.selections == std::vector<Selection>({Selection("tracks", "T2")}) && panel.selectionId() == "T2",
			"selecting a row tells which entity of which facet is selected");

		// facet switching: the selection is dropped, the row at the same position is selected again
		events.selections.clear();
		chooseFacet(panel, "nodes");
		ok &= expect(panel.facetKey() == "nodes" && columnTitles(panel.table()) == QStringList({"ID", "Track ID"})
				&& firstColumn(panel.table()) == QStringList({"n11", "n12", "n13", "n21", "n22"}),
			"another facet lists its own columns and rows");
		ok &= expect(panel.table()->currentRow() == 1 && panel.selectionId() == "n12"
				&& events.lastSelection() == Selection("nodes", "n12"),
			"another facet selects the row at the position that was selected");
		ok &= expect(!panel.table()->item(1, 1)->flags().testFlag(Qt::ItemIsEditable)
				&& panel.table()->item(1, 0)->flags().testFlag(Qt::ItemIsEditable),
			"a facet can show a cell that cannot be edited");
		panel.table()->setCurrentCell(4, 0);
		chooseFacet(panel, "tracks");
		ok &= expect(panel.table()->currentRow() == 1 && panel.selectionId() == "T2",
			"a position beyond the rows selects the last row");
		ok &= expect(events.log.empty() && !document.isDirty(), "choosing facets and rows edits nothing");

		// a facet that moves with its entity
		panel.mutableModel().tracks = {{"T3"}, {"T2"}, {"T1"}};
		panel.refresh();
		ok &= expect(firstColumn(panel.table()) == QStringList({"T3", "T2", "T1"}) && panel.table()->currentRow() == 1 && panel.selectionId() == "T2",
			"a refresh keeps the entity that is selected");
		panel.mutableModel().tracks = {{"T3"}, {"T1"}, {"T2"}};
		panel.refresh();
		ok &= expect(panel.table()->currentRow() == 2 && panel.selectionId() == "T2",
			"a refresh selects the entity again where it has moved to");
		panel.mutableModel().tracks = {{"T3"}, {"T1"}};
		panel.refresh();
		ok &= expect(panel.table()->currentRow() == 1 && panel.selectionId() == "T1",
			"a refresh selects the row at the same position when the entity is gone");
		panel.refresh(true);
		ok &= expect(panel.table()->currentRow() == -1 && panel.selectionId().isEmpty() && events.lastSelection() == Selection("tracks", QString()),
			"a refresh that resets the selection selects no row");
		panel.mutableModel().tracks = {{"T1"}, {"T2"}};
		panel.refresh();

		// a facet that names its entities by two cells
		chooseFacet(panel, "platforms");
		panel.table()->setCurrentCell(1, 0);
		ok &= expect(firstColumn(panel.table()) == QStringList({"S1", "S1"}) && panel.selectionId() == "S1+P2"
				&& events.lastSelection() == Selection("platforms", "S1+P2"),
			"a facet decides how an entity is named in the selection");
		panel.mutableModel().stations[0].platforms = {{"P0", {}}, {"P1", {"n11"}}, {"P2", {"n12"}}};
		panel.refresh();
		ok &= expect(panel.table()->currentRow() == 2 && panel.selectionId() == "S1+P2",
			"the selection of a platform follows its station and its id");

		// a facet without a class
		chooseFacet(panel, "stations");
		ok &= expect(panel.table()->rowCount() == 0 && panel.table()->columnCount() == 0 && panel.selectionId().isEmpty()
				&& panel.guidanceLabel()->text() == "No stations yet. Add a station, then add its platforms."
				&& !panel.guidanceLabel()->isHidden(),
			"a facet without a class lists nothing");
		panel.addEntity();
		panel.deleteEntity();
		ok &= expect(events.log.empty() && !document.isDirty(), "a facet without a class changes nothing");

		// the sentence follows what the scene has: the panel hands over its tracks, nodes and stations and its editability
		panel.mutableModel().nodes = {{"n1", "T1", 0.0, 0.0}};
		chooseFacet(panel, "arcs");
		ok &= expect(panel.table()->rowCount() == 0 && panel.guidanceLabel()->text().contains("at least two nodes"),
			"an empty arcs table asks for nodes while the scene has fewer than two");
		panel.mutableModel().nodes = {{"n1", "T1", 0.0, 0.0}, {"n2", "T1", 1.0, 0.0}};
		panel.refresh();
		ok &= expect(panel.guidanceLabel()->text().startsWith("No arcs yet. Add creates an arc"),
			"an empty arcs table does not ask for nodes that exist");
		document.setEditable(false);
		panel.refresh();
		ok &= expect(panel.guidanceLabel()->text() == "No arcs yet. The scene cannot be edited while a run is in progress.",
			"an empty table says why Add is off while a run uses the scene");
		document.setEditable(true);
		panel.mutableModel().nodes.clear();
		panel.refresh();

		// pending edits go to the facet that is chosen
		chooseFacet(panel, "tracks");
		const int pendingBefore = trackFacet->pendingCount;
		panel.commitPendingEditors();
		ok &= expect(trackFacet->pendingCount == pendingBefore + 1 && nodeFacet->pendingCount == 0,
			"pending edits are committed by the chosen facet");

		// the scene cannot be edited while a run uses it
		trackFacet->committed.clear();
		const int addsBefore = trackFacet->addCount;
		document.setEditable(false);
		panel.refresh();
		ok &= expect(!panel.table()->isEnabled() && !panel.facetCombo()->isEnabled() && !panel.addButton()->isEnabled()
				&& !panel.deleteButton()->isEnabled(),
			"the controls are disabled while a run uses the scene");
		panel.table()->item(0, 0)->setText("during-run");
		panel.addEntity();
		panel.deleteEntity();
		ok &= expect(trackFacet->committed.empty() && trackFacet->addCount == addsBefore && trackFacet->removeCount == 0
				&& document.model().tracks[0].id == "T1" && events.log.empty(),
			"a facet is not asked to edit while a run uses the scene");
		document.setEditable(true);
		panel.refresh();
		ok &= expect(panel.table()->isEnabled() && panel.facetCombo()->isEnabled() && panel.addButton()->isEnabled(),
			"the controls work again when the run is over");
	}

	// Edits, additions and deletions through the panel
	{
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
		ok &= expect(trackFacet->committed == std::vector<std::pair<int, int>>({{1, 0}}) && document.model().tracks[1].id == "T9"
				&& events.log == edit && events.kinds == SceneDocument::Infrastructure && document.lastChangeLabel() == "Edit entity"
				&& document.isDirty(),
			"an edited cell is given to the facet, which marks the document once");
		ok &= expect(firstColumn(panel.table()) == QStringList({"T1", "T9"}) && panel.table()->currentRow() == 1 && panel.selectionId() == "T9"
				&& events.lastSelection() == Selection("tracks", "T9"),
			"the table shows the model again, and the selection follows a rename");

		events.log.clear();
		events.messages.clear();
		panel.table()->item(1, 0)->setText("bad");
		ok &= expect(document.model().tracks[1].id == "T9" && events.log.empty() && panel.table()->item(1, 0)->text() == "T9"
				&& events.messages == std::vector<std::pair<QString, int>>({{"Rejected", 4000}}),
			"a refused edit is shown in the status line, and the table shows the model again");

		panel.refuseEdit();
		ok &= expect(events.messages.size() == 1 && events.log.empty(), "a refused edit without a message shows none");

		// add
		document.markSaved();
		events.log.clear();
		panel.addButton()->click();
		ok &= expect(trackFacet->addCount == 1 && document.model().tracks.size() == 3 && events.log == edit
				&& document.lastChangeLabel() == "Add entity" && document.isDirty() && panel.selectionId() == "T3"
				&& panel.table()->currentRow() == 2,
			"the add button asks the facet to add, and the new entity is selected");

		// delete
		prompts.confirmCount = 0;
		events.log.clear();
		panel.table()->setCurrentCell(0, 0);
		panel.deleteButton()->click();
		ok &= expect(trackFacet->removeCount == 1 && prompts.errorCount == 1 && prompts.confirmCount == 0
				&& prompts.errorMessage == "Cannot delete tracks 'T1' because it is still referenced by:\n\xE2\x80\xA2 block b1\nRemove or rebind those consumers first."
				&& document.model().tracks.size() == 3 && events.log.empty(),
			"an entity that is used cannot be deleted, and the user is told which items use it");
		// a long list of consumers is cut short
		{
			std::vector<std::string> consumers;
			for (int index = 1; index <= 25; ++index)
				consumers.push_back("node n" + std::to_string(index));
			prompts.errorMessage.clear();
			ok &= expect(!panel.confirmDelete("T1", consumers), "an entity with many consumers cannot be deleted");
			ok &= expect(prompts.errorMessage.contains("\xE2\x80\xA2 node n10\n") && !prompts.errorMessage.contains("node n11")
					&& prompts.errorMessage.contains("\nand 15 more\n"),
				"the message lists the first ten consumers and counts the others");
			consumers.resize(10);
			prompts.errorMessage.clear();
			panel.confirmDelete("T1", consumers);
			ok &= expect(prompts.errorMessage.contains("node n10\n") && !prompts.errorMessage.contains("more"),
				"ten consumers are all listed");
		}
		panel.table()->setCurrentCell(1, 0);
		panel.deleteButton()->click();
		ok &= expect(prompts.confirmCount == 1 && prompts.confirmQuestion == "Delete tracks 'T9'?" && document.model().tracks.size() == 3
				&& events.log.empty() && panel.selectionId() == "T9",
			"nothing is deleted when the user answers no");
		prompts.confirmAnswer = true;
		panel.deleteButton()->click();
		ok &= expect(prompts.confirmCount == 2 && document.model().tracks.size() == 2 && document.model().tracks[1].id == "T3" && events.log == edit
				&& document.lastChangeLabel() == "Delete entity" && panel.table()->currentRow() == 1 && panel.selectionId() == "T3",
			"an entity is deleted when the user answers yes, and the row at its position is selected");
		const int removesBefore = trackFacet->removeCount;
		panel.table()->setCurrentCell(-1, -1);
		panel.deleteButton()->click();
		panel.deleteEntity();
		ok &= expect(trackFacet->removeCount == removesBefore, "deleting without a selected row asks nothing of the facet");
		ok &= expect(!panel.deleteButton()->isEnabled() && panel.deleteButton()->toolTip() == "Select a row to delete.",
			"the delete button says why it is off while no row is selected");
		panel.table()->setCurrentCell(0, 0);
		ok &= expect(panel.deleteButton()->isEnabled() && panel.deleteButton()->toolTip().contains("cannot be deleted"),
			"choosing a row turns the delete button on");
		panel.table()->setCurrentCell(-1, -1);
		document.setEditable(false);
		panel.refresh();
		ok &= expect(!panel.deleteButton()->isEnabled() && panel.deleteButton()->toolTip().contains("while a run is in progress"),
			"the delete button says why it is off while a run uses the scene");
		document.setEditable(true);
		panel.refresh();
		panel.showStatus("Hello", 1500);
		ok &= expect(events.lastMessage() == std::pair<QString, int>("Hello", 1500), "a facet can show a message in the status line");
	}

	// Blocks: the filter on the track and the buttons that move a block
	{
		SceneDocument document;
		InfrastructurePanel panel;
		panel.setFacet(std::make_unique<BlockFacet>());
		panel.setFacet(std::make_unique<ListFacet>(ListFacet::Kind::Tracks));
		panel.setDocument(&document);
		document.replace(infrastructureScene(), false);
		Events events(panel, document);
		ListFacet* trackFacet = static_cast<ListFacet*>(panel.facet("tracks"));
		panel.refresh();
		ok &= expect(panel.blockTrackFilterCombo()->isHidden() && panel.insertBlockButton()->isHidden(),
			"the filter on the track is hidden until blocks are chosen");

		chooseFacet(panel, "blocks");
		ok &= expect(!panel.blockTrackFilterCombo()->isHidden() && !panel.insertBlockButton()->isHidden()
				&& !panel.moveBlockUpButton()->isHidden() && !panel.moveBlockDownButton()->isHidden(),
			"the filter on the track and the buttons of blocks are shown with blocks");
		ok &= expect(choices(panel.blockTrackFilterCombo()) == QStringList({"T1", "T2", "Invalid track: ghost"})
				&& panel.blockTrackFilterCombo()->currentIndex() == 0 && panel.blockTrackFilterCombo()->isEnabled(),
			"the filter offers the tracks, and a track that the scene does not have if a block names it");
		ok &= expect(firstColumn(panel.table()) == QStringList({"b1", "b2"}) && panel.blockRowModelIndices() == std::vector<int>({0, 1})
				&& panel.addButton()->isEnabled() && !panel.insertBlockButton()->isEnabled() && !panel.moveBlockUpButton()->isEnabled()
				&& !panel.moveBlockDownButton()->isEnabled() && !panel.deleteButton()->isEnabled(),
			"the blocks of the chosen track are listed, and the buttons wait for a selected block");
		ok &= expect(!panel.guidanceLabel()->isHidden()
				&& panel.guidanceLabel()->text() == "Blocks are ordered on the chosen track; select a row to insert or move it.",
			"blocks come with their guidance");

		panel.table()->setCurrentCell(0, 0);
		ok &= expect(!panel.moveBlockUpButton()->isEnabled() && panel.moveBlockDownButton()->isEnabled(), "the first block cannot move up");
		panel.table()->setCurrentCell(1, 0);
		ok &= expect(panel.moveBlockUpButton()->isEnabled() && !panel.moveBlockDownButton()->isEnabled(), "the last block cannot move down");
		ok &= expect(events.lastSelection() == Selection("blocks", "b2"), "the selection names the block");
		panel.refresh();
		ok &= expect(panel.insertBlockButton()->isEnabled() && panel.deleteButton()->isEnabled() && panel.table()->currentRow() == 1,
			"a block can be inserted and deleted when the table has been refreshed with a selected block");

		// another track
		panel.blockTrackFilterCombo()->setCurrentIndex(1);
		ok &= expect(firstColumn(panel.table()) == QStringList({"b3"}) && panel.blockRowModelIndices() == std::vector<int>({2})
				&& panel.table()->currentRow() == 0 && panel.selectionId() == "b3" && panel.addButton()->isEnabled(),
			"choosing a track lists its blocks, and selects the row at the position that was selected");
		panel.blockTrackFilterCombo()->setCurrentIndex(2);
		ok &= expect(firstColumn(panel.table()) == QStringList({"b4"}) && !panel.addButton()->isEnabled(),
			"blocks cannot be added to a track that the scene does not have");
		panel.blockTrackFilterCombo()->setCurrentIndex(0);

		// no blocks on the track
		panel.mutableModel().tracks.push_back({"T3"});
		panel.refreshBlockTrackFilter();
		panel.blockTrackFilterCombo()->setCurrentIndex(panel.blockTrackFilterCombo()->findData("T3"));
		ok &= expect(panel.table()->rowCount() == 0 && panel.guidanceLabel()->text() == "No blocks on this track. Choose another track or add a block."
				&& panel.addButton()->isEnabled(),
			"a track without blocks is explained");
		panel.mutableModel().tracks.pop_back();

		// the filter belongs to blocks
		chooseFacet(panel, "tracks");
		const int fillsBefore = trackFacet->fillCount;
		ok &= expect(panel.blockTrackFilterCombo()->isHidden() && panel.insertBlockButton()->isHidden() && panel.moveBlockUpButton()->isHidden()
				&& panel.moveBlockDownButton()->isHidden(),
			"the filter on the track and the buttons of blocks go with blocks");
		panel.blockTrackFilterCombo()->setCurrentIndex(1);
		ok &= expect(trackFacet->fillCount == fillsBefore, "choosing a track does not refresh the table of another facet");

		// the scene cannot be edited while a run uses it
		chooseFacet(panel, "blocks");
		document.setEditable(false);
		panel.refresh();
		ok &= expect(!panel.blockTrackFilterCombo()->isEnabled() && !panel.addButton()->isEnabled() && !panel.insertBlockButton()->isEnabled()
				&& !panel.moveBlockUpButton()->isEnabled() && !panel.moveBlockDownButton()->isEnabled(),
			"the filter and the buttons of blocks are disabled while a run uses the scene");
	}

	// Routes: the sections of the selected route
	{
		SceneDocument document;
		InfrastructurePanel panel;
		panel.setFacet(std::make_unique<RouteFacet>());
		panel.setFacet(std::make_unique<ListFacet>(ListFacet::Kind::Tracks));
		panel.setDocument(&document);
		document.replace(infrastructureScene(), false);
		panel.refresh();
		ok &= expect(panel.routeSectionDetailWidget()->isHidden(), "the sections of a route are hidden until routes are chosen");

		chooseFacet(panel, "routes");
		ok &= expect(!panel.routeSectionDetailWidget()->isHidden() && firstColumn(panel.table()) == QStringList({"r1", "r2"})
				&& panel.guidanceLabel()->text() == "Select a route to edit its ordered sections below.",
			"routes are listed with their guidance, and the sections are shown");
		const QStringList catalog = choices(panel.routeSectionCatalogCombo());
		ok &= expect(catalog.size() == 3 && catalog[0] == "base block b1 / track T1" && catalog[2] == "base block b3 / track T2"
				&& panel.routeSectionCatalogCombo()->itemData(0).toString() == "@b1@",
			"the catalog names every section of the scene by its block");
		ok &= expect(panel.routeSectionListWidget()->count() == 0 && !panel.routeSectionListWidget()->isEnabled()
				&& !panel.addRouteSectionButton()->isEnabled() && !panel.removeRouteSectionButton()->isEnabled()
				&& !panel.routeSectionCatalogCombo()->isEnabled(),
			"no section can be edited until a route is selected");

		panel.table()->setCurrentCell(0, 0);
		ok &= expect(items(panel.routeSectionListWidget())
					== QStringList({"base block b1 / track T1 (legacy: b1)", "base block b2 / track T1 (legacy: b2)"})
				&& panel.routeSectionListWidget()->currentRow() == 0 && panel.routeSectionListWidget()->isEnabled()
				&& panel.routeSectionCatalogCombo()->isEnabled() && panel.addRouteSectionButton()->isEnabled()
				&& panel.removeRouteSectionButton()->isEnabled() && !panel.moveRouteSectionUpButton()->isEnabled()
				&& panel.moveRouteSectionDownButton()->isEnabled(),
			"the sections of the selected route are listed, and the first is selected");
		panel.routeSectionListWidget()->setCurrentRow(1);
		ok &= expect(panel.moveRouteSectionUpButton()->isEnabled() && !panel.moveRouteSectionDownButton()->isEnabled(),
			"the buttons follow the selected section");

		panel.table()->setCurrentCell(1, 0);
		ok &= expect(items(panel.routeSectionListWidget())
				== QStringList({"base block b1 / track T1 (legacy: b1)", "Invalid: missing"}),
			"a section that the scene does not have is listed as invalid");

		chooseFacet(panel, "tracks");
		ok &= expect(panel.routeSectionDetailWidget()->isHidden(), "the sections of a route go with routes");

		SceneSectionDescriptor connection;
		connection.connectionDerived = true;
		connection.sourceConnectionId = "c1";
		connection.firstBlockId = "b1";
		connection.secondBlockId = "b3";
		ok &= expect(InfrastructurePanel::sectionLabel(connection) == "connection c1 / b1 -> b3", "a section of a connection is named by its blocks");
	}

	// The layout: the combo on top, the buttons under the table, and the sections of a route below it in a splitter
	{
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

		ok &= expect(bottom(panel.facetCombo()) <= top(table) && bottom(table) <= top(add) && top(add) == top(panel.deleteButton())
				&& inside(add) && inside(panel.deleteButton()),
			"the combo is above the table, and Add and Delete are in a row under it");
		ok &= expect(panel.insertBlockButton()->isHidden() && panel.routeSectionDetailWidget()->isHidden(),
			"the buttons of blocks and the sections of a route are not shown with tracks");

		chooseFacet(panel, "blocks");
		QApplication::processEvents();
		ok &= expect(bottom(panel.facetCombo()) <= top(panel.blockTrackFilterCombo()) && bottom(panel.blockTrackFilterCombo()) <= top(table)
				&& bottom(table) <= top(add),
			"the filter on the track is between the combo and the table");
		for (const QPushButton* button : {panel.deleteButton(), panel.insertBlockButton(), panel.moveBlockUpButton(), panel.moveBlockDownButton()})
			ok &= expect(top(button) == top(add) && inside(button) && button->x() > add->x(), "a button of blocks is in the row of Add");
		ok &= expect(panel.routeSectionDetailWidget()->isHidden(), "the sections of a route are not shown with blocks");

		chooseFacet(panel, "routes");
		QApplication::processEvents();
		QWidget* detail = panel.routeSectionDetailWidget();
		auto* splitter = qobject_cast<QSplitter*>(detail->parentWidget());
		const auto* heading = detail->findChild<QLabel*>("routeSectionHeading");
		ok &= expect(panel.insertBlockButton()->isHidden() && detail->isVisible() && inside(detail) && splitter
				&& splitter->orientation() == Qt::Vertical && splitter->indexOf(detail) == 1 && splitter->indexOf(table->parentWidget()) == 0,
			"the sections of a route are in a vertical splitter below the table");
		ok &= expect(heading && heading->text() == "Route sections" && heading->isVisible() && bottom(add) <= top(heading)
				&& top(heading) < top(panel.routeSectionCatalogCombo()) && bottom(panel.routeSectionCatalogCombo()) <= top(panel.routeSectionListWidget())
				&& bottom(panel.routeSectionListWidget()) <= top(panel.removeRouteSectionButton()),
			"the heading, the catalog, the list and the buttons of the sections follow each other under the buttons of the table");
		for (const QPushButton* button : {panel.addButton(), panel.deleteButton(), panel.addRouteSectionButton(), panel.removeRouteSectionButton(),
				 panel.moveRouteSectionUpButton(), panel.moveRouteSectionDownButton()})
			ok &= expect(inside(button), "a button of the routes can be reached without scrolling");
		if (splitter) {
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
			ok &= expect(before.size() == 2 && after.size() == 2 && after[0] < before[0] && after[1] > before[1]
					&& after[0] + after[1] == before[0] + before[1] && before[0] - after[0] <= 40 + splitter->handleWidth()
					&& inside(add),
				"dragging the handle gives the sections of a route more room and keeps the buttons of the table in view");
		}

		chooseFacet(panel, "tracks");
		QApplication::processEvents();
		ok &= expect(panel.routeSectionDetailWidget()->isHidden() && bottom(table) <= top(add) && inside(add),
			"the table takes the room of the sections again");
	}

	// Another facet is chosen while a field in a cell has the focus, and the panel is closed with a cell editor open
	{
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
		ok &= expect(field->hasFocus(), "a field in a cell has the focus");

		chooseFacet(panel, "nodes");
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		const QWidget* editor = table->indexWidget(table->currentIndex());
		int strayEditors = 0;
		for (const QWidget* child : table->viewport()->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
			strayEditors += !child->isHidden() && child != editor ? 1 : 0;
		ok &= expect(strayEditors == 0, "the table of the other facet shows no editor besides the one of its current cell");

		// The panel is destroyed at the end of the block with this editor open, which must not crash.
		table->setCurrentCell(1, 0);
		QApplication::processEvents();
		ok &= expect(table->indexWidget(table->currentIndex()) != nullptr, "the current cell has an editor");
	}

	return ok ? 0 : 1;
}
