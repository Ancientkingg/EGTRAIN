#ifndef INFRASTRUCTUREPANEL_H
#define INFRASTRUCTUREPANEL_H

#include "editors/SceneEditorPanel.h"
#include "editors/infrastructure/InfrastructureFacet.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
struct SceneSectionDescriptor;
struct SceneSectionInventory;
struct SceneSectionLabels;

/// The infrastructure of the scene: a combo on top to pick the kind of entity, a row with a field that filters the rows by
/// text (filterEdit()) and a line that counts them (countLabel()), a sentence about the kind (infrastructureGuidanceText),
/// a table with one row for each entity of that kind, under the table a line that says what the canvas marks of the
/// selected row (markLabel()), and under that the buttons that add and delete one. The kinds are the facets: tracks, nodes,
/// arcs, blocks, connections, stations, platforms, signals, signalling areas, routes, block dependencies, single-track
/// restrictions and station boundaries. What a kind lists and how it is edited is up to its InfrastructureFacet; a kind
/// without one lists nothing. The facet also says which other controls it uses (InfrastructureFacet::capabilities): a
/// filter on the track under the combo that lists the blocks of one track, which blocks use, a filter on the track at the
/// start of the row of the text filter that hides the rows of other tracks, which nodes, arcs, connections and platforms
/// use, buttons that insert and move an entity, which blocks use too, and the heading "Route sections" with the list of
/// the sections of the selected route and the buttons that change it, below the table in a splitter, which routes use. The
/// buttons call the facet that is chosen, only when it has their capability and only while the scene can be edited.
///
/// The filters hide rows and keep them in the table, so that a row keeps its number. They stay when another kind is
/// chosen, and are applied again whenever the table is filled. Add and Insert clear them first, so
/// that the new row is shown, and Move Up and Move Down are off while they hide a row. A row that they hide is never
/// selected.
class InfrastructurePanel : public SceneEditorPanel {
	Q_OBJECT

public:
	/// Makes the widgets, without facets and empty until a loaded document is set.
	explicit InfrastructurePanel(QWidget* parent = nullptr);

	/// The panel takes the facet, which takes the place of the one with the same key. The key must be one of
	/// facetKeys(). Null is ignored.
	void setFacet(std::unique_ptr<InfrastructureFacet> facet);
	/// The facet with this key, or null. The panel keeps it.
	InfrastructureFacet* facet(const QString& key) const;
	/// The keys of the facets in the order of the combo, such as "tracks" and "station_boundaries".
	static QStringList facetKeys();

	/// Lists the entities of the chosen facet again, and keeps the selected row where it still exists.
	void refresh() override;
	/// The same; no row is selected afterwards when `resetSelection` is true, as when another scene has been opened.
	void refresh(bool resetSelection);
	/// Takes the cells that are being edited into the document: first the text of a cell whose editor is open, as leaving
	/// the cell does, then the fields that the chosen facet puts in cells (InfrastructureFacet::commitPendingEdits). The
	/// editor is left open while the panel tells an edit of its own, whose rows can differ from the model until the table
	/// is filled again.
	void commitPendingEditors() override;
	/// The same as refresh(bool) without the state of the combo and the add button. The facets call it after a change.
	void refreshTable(bool resetSelection = false);
	/// Takes the selected row as the selection, tells selectionChanged, and shows the controls that depend on it.
	void updateSelection();
	/// Selects the row of the entity that an item of the track preview stands for and scrolls the table to it, choosing its
	/// kind in the combo first and clearing a filter that hides the row. `kind` and `id` are the kind and the id of the item
	/// (PreviewGraphics::Kind and Id), and `platform` is the platform of a station node (PreviewGraphics::PlatformId). A
	/// node, a track, an arc, a connection and a station are rows of their own kind. A station node is the row of its
	/// platform under Platforms, or its row under Nodes when it has no platform. A signal head names a section, and is the
	/// row of the block of that section under Blocks, whose track the filter then shows. The cells that are being edited
	/// are taken first (commitPendingEditors()). selectionChanged is told once, or not at all when the row was selected
	/// already. Returns false and changes nothing while the scene cannot be edited, and for an item of another kind or
	/// whose entity the scene does not have.
	bool selectCanvasItem(const QString& kind, const QString& id, const QString& platform = QString());
	/// Selects the row of the entity that a diagnostic names (SceneDiagnostic::itemType and itemId), as selectCanvasItem()
	/// does for an item of the preview. `kind` is a node, a track, an arc, a connection, a station, a block (its track
	/// filter then shows the track of the block), a signal by its id, a signalling area or a route. Returns false and
	/// changes nothing while the scene cannot be edited, and for another kind or an entity that the scene does not have.
	bool selectEntity(const QString& kind, const QString& id);
	/// The row that selectCanvasItem() and selectEntity() choose: the key of its facet, its selection id and, for a block,
	/// the track that the filter shows. The facet is empty for an entity without a row.
	struct RowTarget {
		/// The key of the facet that lists the row, empty for an entity without a row.
		QString facet;
		/// The selection id of the row.
		QString id;
		/// For a block, the track that the filter of the blocks shows; empty otherwise.
		QString track;
	};
	/// Lists the tracks that blocks can be filtered on again and keeps the chosen track where it still exists.
	void refreshBlockTrackFilter();

	/// The cell in `row` and `column` has been edited; the chosen facet takes it.
	void commitCell(int row, int column);
	/// Adds an entity of the chosen kind, after clearing the filters.
	void addEntity();
	/// Deletes the entity in the selected row.
	void deleteEntity();
	/// Inserts an entity before the selected one, after clearing the text filter.
	void insertEntity();
	/// Moves the selected entity down by `offset` rows, up when it is negative. Does nothing while a filter hides a row.
	void moveEntity(int offset);
	/// Adds the section of the catalog that is chosen to the selected route.
	void addSection();
	/// Removes the selected section of the route.
	void removeSection();
	/// Moves the selected section of the route down by `offset` places, up when it is negative.
	void moveSection(int offset);

	/// The key of the chosen facet, one of facetKeys().
	QString facetKey() const;
	/// The name that the combo shows for the chosen facet.
	QString facetTitle() const;
	/// The id of the selected entity, or an empty string. It is what the track preview marks. A block dependency, a
	/// single-track restriction and a station boundary have no id of their own and are named by their row number.
	const QString& selectionId() const { return m_selectionId; }
	/// For a route, the index into SceneRoute::blocks of the section selected under Route sections; -1 for another kind of
	/// entity and without one.
	int selectedRouteSection() const;
	/// Sets the selection without showing it.
	void setSelectionId(const QString& id) { m_selectionId = id; }
	/// For each row of the table of blocks, the index of its block in the model. The facet that lists the blocks sets it.
	std::vector<int>& blockRowModelIndices() { return m_blockRowModelIndices; }

	/// The table that the facets fill; the panel owns it.
	QTableWidget* table() const { return m_table; }
	/// The model of the document; an empty scene without one.
	const SceneModel& model() const;
	/// Shows a message in the status line of the window for `timeoutMs` milliseconds, through statusMessage().
	void showStatus(const QString& text, int timeoutMs = 4000);
	/// Changes the infrastructure of the document with `apply` as one edit under `label` (SceneDocument::edit), tells
	/// changed() and lists the entities again. While the document is not open for edits nothing changes, nothing is told
	/// and the result is false. A facet decides before the call whether there is a change, so that an edit that changes
	/// nothing adds no undo step.
	bool edit(const QString& label, const std::function<void(SceneModel&)>& apply);
	/// The same for a widget in the table that has changed, without listing the entities again, which would replace the
	/// widget while it tells of the change. The facet lists the entities again when it needs to.
	bool editInPlace(const QString& label, const std::function<void(SceneModel&)>& apply);
	/// A facet has refused an edit and left the model as it was. Shows `message` when there is one, and lists the
	/// entities again so that the table shows the model.
	void refuseEdit(const QString& message = {});
	/// Deletes the entity `id` of the chosen facet with `apply`, as one edit under `label` (see edit()), selects the row
	/// that takes its place, or else the one before it, and says in the status line that Undo restores the entity. An
	/// empty `id` stands for a row that is not complete yet. `consumers` describes the items that still use the entity
	/// (see sceneDirectDeleteConsumers). An entity that is used is not deleted: the user is told which items use it
	/// instead. Returns true when the entity was deleted.
	bool deleteRow(const QString& id, const std::vector<std::string>& consumers, const QString& label,
		const std::function<void(SceneModel&)>& apply);

	/// The text that names a section in a list of sections: its label in `labels` (see sceneSectionLabels).
	static QString sectionLabel(const SceneSectionLabels& labels, const SceneSectionDescriptor& section);
	/// Fills `combo` with the sections of the inventory, each named by its label, with the id of the section as its
	/// data and its tooltip, and chooses `current`. A section that is not in the inventory gets an item of its own, and
	/// `allowNone` adds an item with `noneLabel` and no data on top.
	static void populateSectionCombo(QComboBox* combo, const SceneSectionInventory& inventory, const std::string& current,
		bool allowNone, const QString& noneLabel);

	/// The combo of the kinds of entity, with the key of each facet as its data.
	QComboBox* facetCombo() const { return m_facetCombo; }
	/// The button of addEntity().
	QPushButton* addButton() const { return m_addButton; }
	/// The button of deleteEntity().
	QPushButton* deleteButton() const { return m_deleteButton; }
	/// The button of the task action `index` of the chosen kind (InfrastructureFacet::taskActions); null when the kind has
	/// no such action.
	QPushButton* taskButton(int index) const {
		return index >= 0 && index < static_cast<int>(m_taskButtons.size()) ? m_taskButtons[static_cast<std::size_t>(index)] : nullptr;
	}
	/// Runs the task action `index` of the chosen kind on the selected row, as its button does. The cells that are being
	/// edited are taken first. Does nothing while the scene cannot be edited, without a selected row, and for an index
	/// that is out of range.
	void runTaskAction(int index);
	/// The sentence about the chosen kind.
	QLabel* guidanceLabel() const { return m_guidanceLabel; }
	/// The line under the table that says what the canvas marks of the selected row, such as "route2: 124 sections on B3,
	/// B0, B4, B5 and B2"; empty and hidden without a selected row, while the document cannot be edited and while
	/// setMarkShown() hides it.
	QLabel* markLabel() const { return m_markLabel; }
	/// Shows the line under the table (markLabel()) or hides it. The window hides it while the canvas shows a run or its
	/// results, which mark no row. It is shown until this is called.
	void setMarkShown(bool shown);
	/// The filter of the blocks on a track, shown while blocks are listed.
	QComboBox* blockTrackFilterCombo() const { return m_blockTrackFilterCombo; }
	/// The filter on a track of a facet with InfrastructureFacet::TrackRowFilter, shown while one is listed: "All tracks"
	/// with an empty id as its data, then the tracks of the scene with their ids. One choice serves every such facet.
	QComboBox* trackFilterCombo() const { return m_trackFilterCombo; }
	/// The field that filters the rows by text: a row is shown when every word of the field occurs in one of its cells,
	/// ignoring case, the rule of the Find dialog (FindDialog::matches). The text of a combo or a number field in a cell
	/// counts as its text.
	QLineEdit* filterEdit() const { return m_filterEdit; }
	/// The line beside the text filter that counts the rows, such as "399 nodes", or "12 of 399 nodes" while the filters
	/// hide rows. Blocks are counted on the track of their filter. Hidden while the table is empty.
	QLabel* countLabel() const { return m_countLabel; }
	/// The button of insertEntity() for blocks.
	QPushButton* insertBlockButton() const { return m_insertBlockButton; }
	/// The button of moveEntity() up for blocks.
	QPushButton* moveBlockUpButton() const { return m_moveBlockUpButton; }
	/// The button of moveEntity() down for blocks.
	QPushButton* moveBlockDownButton() const { return m_moveBlockDownButton; }
	/// The part below the table that holds the sections of the selected route, shown while routes are listed.
	QWidget* routeSectionDetailWidget() const { return m_routeSectionDetailWidget; }
	/// The catalog of the sections that addSection() can add.
	QComboBox* routeSectionCatalogCombo() const { return m_routeSectionCatalogCombo; }
	/// The sections of the selected route in order.
	QListWidget* routeSectionListWidget() const { return m_routeSectionListWidget; }
	/// The button of addSection().
	QPushButton* addRouteSectionButton() const { return m_addRouteSectionButton; }
	/// The button of removeSection().
	QPushButton* removeRouteSectionButton() const { return m_removeRouteSectionButton; }
	/// The button of moveSection() up.
	QPushButton* moveRouteSectionUpButton() const { return m_moveRouteSectionUpButton; }
	/// The button of moveSection() down.
	QPushButton* moveRouteSectionDownButton() const { return m_moveRouteSectionDownButton; }

Q_SIGNALS:
	/// The selection has been taken from the table again: after another row or facet is chosen, after another section of
	/// the selected route is chosen under Route sections, and after the table has been filled again. `facet` is the key of
	/// the chosen facet and `id` the id of the selected entity, empty when no row is selected. The controls that depend on
	/// the selection are up to date when it is told.
	void selectionChanged(const QString& facet, const QString& id);

protected:
	/// Does nothing: the window draws the track preview when the document is replaced, and calls refresh() when the scene
	/// has been set up.
	void documentReplaced() override {}

private:
	// Selects the row that `find` gives for the model, after taking the cells that are being edited; find runs again after
	// them, because the edit can rename or remove the entity.
	bool selectRow(const std::function<RowTarget(const SceneModel&)>& find);
	class CellDelegate;

	// Shows the section catalog and the sections of the selected route, while routes are listed.
	void refreshRouteSectionPanel();
	// Lists the tracks of the filter of TrackRowFilter again, after "All tracks", and keeps the chosen track where it still
	// exists.
	void refreshTrackFilter();
	// Enables Move Up and Move Down for the selected row of a facet with InsertAndMove.
	void refreshMoveButtons();
	// Whether the row holds every word of the filter field in its cells.
	bool matchesText(int row) const;
	// Whether the row is on the track of the track filter, or the chosen facet has no such filter or "All tracks" is chosen.
	bool matchesTrack(int row) const;
	// Hides the rows that do not match the filters, and counts the rows that are shown.
	void applyFilters();
	// Applies the filters after the user has changed one, and drops the selection of a row that they hide.
	void filterRows();
	// Clears the filters that hide the row.
	void revealRow(int row);
	// Clears the text filter and, for a facet with TrackRowFilter, the track filter, before an entity is added.
	void clearFilters();
	// Says on the line under the table what the canvas marks of the selected row.
	void refreshMarkLabel();
	InfrastructureFacet* currentFacet() const;
	// Makes the buttons of the task row when the kind has changed, shows the row only when it has buttons, and enables the
	// buttons while a row is selected and the scene can be edited.
	void refreshTaskButtons();
	// Whether the chosen facet has the capability; false without a facet.
	bool currentFacetHas(InfrastructureFacet::Capability capability) const;

	std::map<QString, std::unique_ptr<InfrastructureFacet>> m_facets;
	QString m_selectionId;
	std::vector<int> m_blockRowModelIndices;

	QComboBox* m_facetCombo = nullptr;
	QPushButton* m_addButton = nullptr;
	QPushButton* m_deleteButton = nullptr;
	QWidget* m_taskActionRow = nullptr;
	std::vector<QPushButton*> m_taskButtons;
	const InfrastructureFacet* m_taskFacet = nullptr;
	QComboBox* m_blockTrackFilterCombo = nullptr;
	QComboBox* m_trackFilterCombo = nullptr;
	QLineEdit* m_filterEdit = nullptr;
	QLabel* m_countLabel = nullptr;
	// True while the filters hide a row.
	bool m_rowsHidden = false;
	// True while refreshTable() is to clear a filter that hides the selected entity, as for a row of the canvas.
	bool m_revealSelection = false;
	QPushButton* m_insertBlockButton = nullptr;
	QPushButton* m_moveBlockUpButton = nullptr;
	QPushButton* m_moveBlockDownButton = nullptr;
	QLabel* m_guidanceLabel = nullptr;
	QTableWidget* m_table = nullptr;
	QLabel* m_markLabel = nullptr;
	// False while setMarkShown() hides m_markLabel.
	bool m_markShown = true;
	// The delegate of the table, which knows its open editor.
	CellDelegate* m_cellDelegate = nullptr;
	// True while editInPlace() changes the document and tells changed().
	bool m_editing = false;
	QWidget* m_routeSectionDetailWidget = nullptr;
	QComboBox* m_routeSectionCatalogCombo = nullptr;
	QListWidget* m_routeSectionListWidget = nullptr;
	QPushButton* m_addRouteSectionButton = nullptr;
	QPushButton* m_removeRouteSectionButton = nullptr;
	QPushButton* m_moveRouteSectionUpButton = nullptr;
	QPushButton* m_moveRouteSectionDownButton = nullptr;
};

#endif // INFRASTRUCTUREPANEL_H
