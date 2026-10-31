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
class QListWidget;
class QPushButton;
struct SceneSectionDescriptor;
struct SceneSectionInventory;
struct SceneSectionLabels;

/// The infrastructure of the scene: a combo on top to pick the kind of entity, a sentence about the kind
/// (infrastructureGuidanceText), a table with one row for each entity of that kind, and under the table the buttons that
/// add and delete one. The kinds are the facets: tracks, nodes, arcs, blocks, connections, stations, platforms, signals,
/// signalling areas, routes, block dependencies, single-track restrictions and station boundaries. What a kind lists and
/// how it is edited is up to its InfrastructureFacet; a kind without one lists nothing. The facet also says which other
/// controls it uses (InfrastructureFacet::capabilities): a filter on the track under the combo, which blocks use, buttons
/// that insert and move an entity, which blocks use too, and the heading "Route sections" with the list of the sections
/// of the selected route and the buttons that change it, below the table in a splitter, which routes use. The buttons
/// call the facet that is chosen, only when it has their capability and only while the scene can be edited.
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
	/// Takes the cells that are being edited into the document.
	void commitPendingEditors() override;
	/// The same as refresh(bool) without the state of the combo and the add button. The facets call it after a change.
	void refreshTable(bool resetSelection = false);
	/// Takes the selected row as the selection, tells selectionChanged, and shows the controls that depend on it.
	void updateSelection();
	/// Lists the tracks that blocks can be filtered on again and keeps the chosen track where it still exists.
	void refreshBlockTrackFilter();

	/// The cell in `row` and `column` has been edited; the chosen facet takes it.
	void commitCell(int row, int column);
	/// Adds an entity of the chosen kind.
	void addEntity();
	/// Deletes the entity in the selected row after the user has confirmed it.
	void deleteEntity();
	/// Inserts an entity before the selected one.
	void insertEntity();
	/// Moves the selected entity down by `offset` rows, up when it is negative.
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
	/// The id of the selected entity, or an empty string. It is what the track preview marks.
	const QString& selectionId() const { return m_selectionId; }
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
	/// Asks whether to delete the entity `id` of the chosen facet. `consumers` describes the items that still use it
	/// (see sceneDirectDeleteConsumers). An entity that is used cannot be deleted: the user is told which items use it
	/// instead, and the answer is no.
	bool confirmDelete(const QString& id, const std::vector<std::string>& consumers);

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
	/// The sentence about the chosen kind.
	QLabel* guidanceLabel() const { return m_guidanceLabel; }
	/// The filter of the blocks on a track, shown while blocks are listed.
	QComboBox* blockTrackFilterCombo() const { return m_blockTrackFilterCombo; }
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
	/// The selection has been taken from the table again: after another row or facet is chosen, and after the table has
	/// been filled again. `facet` is the key of the chosen facet and `id` the id of the selected entity, empty when no
	/// row is selected.
	void selectionChanged(const QString& facet, const QString& id);
	/// A message for the status line, shown for `timeoutMs` milliseconds.
	void statusMessage(const QString& text, int timeoutMs);

protected:
	/// Does nothing: the window draws the track preview when the document is replaced, and calls refresh() when the scene
	/// has been set up.
	void documentReplaced() override {}

private:
	// Shows the section catalog and the sections of the selected route, while routes are listed.
	void refreshRouteSectionPanel();
	InfrastructureFacet* currentFacet() const;
	// Whether the chosen facet has the capability; false without a facet.
	bool currentFacetHas(InfrastructureFacet::Capability capability) const;

	std::map<QString, std::unique_ptr<InfrastructureFacet>> m_facets;
	QString m_selectionId;
	std::vector<int> m_blockRowModelIndices;

	QComboBox* m_facetCombo = nullptr;
	QPushButton* m_addButton = nullptr;
	QPushButton* m_deleteButton = nullptr;
	QComboBox* m_blockTrackFilterCombo = nullptr;
	QPushButton* m_insertBlockButton = nullptr;
	QPushButton* m_moveBlockUpButton = nullptr;
	QPushButton* m_moveBlockDownButton = nullptr;
	QLabel* m_guidanceLabel = nullptr;
	QTableWidget* m_table = nullptr;
	QWidget* m_routeSectionDetailWidget = nullptr;
	QComboBox* m_routeSectionCatalogCombo = nullptr;
	QListWidget* m_routeSectionListWidget = nullptr;
	QPushButton* m_addRouteSectionButton = nullptr;
	QPushButton* m_removeRouteSectionButton = nullptr;
	QPushButton* m_moveRouteSectionUpButton = nullptr;
	QPushButton* m_moveRouteSectionDownButton = nullptr;
};

#endif // INFRASTRUCTUREPANEL_H
