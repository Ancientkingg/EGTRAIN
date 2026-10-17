#ifndef INFRASTRUCTUREPANEL_H
#define INFRASTRUCTUREPANEL_H

#include "editors/SceneEditorPanel.h"
#include "editors/infrastructure/InfrastructureFacet.h"
#include "widgets/UserPrompts.h"

#include <QPointer>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>
#include <string>
#include <vector>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
struct SceneSectionDescriptor;

// The infrastructure of the scene: a combo to pick the kind of entity, a table with one row for each entity of that
// kind, and the buttons that add and delete one. The kinds are the facets: tracks, nodes, arcs, blocks, connections,
// stations, platforms, signals, signalling areas, routes, block dependencies, single-track restrictions and station
// boundaries. What a kind lists and how it is edited is up to its InfrastructureFacet; a kind without one lists
// nothing. Blocks come with a filter on the track and buttons that insert and move a block, and routes with the list of
// their sections.
class InfrastructurePanel : public SceneEditorPanel {
	Q_OBJECT

public:
	explicit InfrastructurePanel(QWidget* parent = nullptr);

	// The panel shows the scene of this document. It is not refreshed when the document is replaced, because the window
	// draws the track preview at that time: call refresh() when the scene has been set up.
	void setDocument(SceneDocument* document) override;
	// The messages and questions go through these prompts. The panel uses its own until it is given others; null
	// brings its own back. The prompts must outlive the panel.
	void setPrompts(UserPrompts* prompts);
	// The facet takes the place of the one with the same key. The key must be one of facetKeys().
	void setFacet(std::unique_ptr<InfrastructureFacet> facet);
	// The facet with this key, or null.
	InfrastructureFacet* facet(const QString& key) const;
	// The keys of the facets in the order of the combo.
	static QStringList facetKeys();

	// Lists the entities of the chosen facet again, and keeps the selected row where it still exists.
	void refresh() override;
	// The same; no row is selected afterwards when `resetSelection` is true, as when another scene has been opened.
	void refresh(bool resetSelection);
	// Takes the cells that are being edited into the document.
	void commitPendingEditors() override;
	// The same as refresh(bool) without the state of the combo and the add button. The facets call it after a change.
	void refreshTable(bool resetSelection = false);
	// Takes the selected row as the selection, tells selectionChanged, and shows the controls that depend on it.
	void updateSelection();
	// Lists the tracks that blocks can be filtered on again and keeps the chosen track where it still exists.
	void refreshBlockTrackFilter();

	// The cell in `row` and `column` has been edited.
	void commitCell(int row, int column);
	void addEntity();
	void deleteEntity();

	// The key of the chosen facet and the name that the combo shows for it.
	QString facetKey() const;
	QString facetTitle() const;
	// The id of the selected entity, or an empty string. It is what the track preview marks.
	const QString& selectionId() const { return m_selectionId; }
	// Sets the selection without showing it.
	void setSelectionId(const QString& id) { m_selectionId = id; }
	// For each row of the table of blocks, the index of its block in the model. The facet that lists the blocks sets it.
	std::vector<int>& blockRowModelIndices() { return m_blockRowModelIndices; }

	// What the facets use.
	QTableWidget* table() const { return m_table; }
	// The model of the document; an empty scene without one.
	const SceneModel& model() const;
	SceneModel& mutableModel();
	// True when a scene is loaded.
	bool hasScene() const;
	// False while no scene is loaded or a run uses it.
	bool canEdit() const;
	// Shows a message in the status line of the window.
	void showStatus(const QString& text, int timeoutMs = 4000);
	// A facet has changed the model: marks the document changed under `label`, tells changed(), and lists the entities
	// again.
	void entityChanged(const QString& label);
	// A facet has refused an edit and left the model as it was. Shows `message` when there is one, and lists the
	// entities again so that the table shows the model.
	void refuseEdit(const QString& message = {});
	// Asks whether to delete the entity `id` of the chosen facet. `consumers` describes the items that still use it
	// (see sceneDirectDeleteConsumers). An entity that is used cannot be deleted: the user is told which items use it
	// instead, and the answer is no.
	bool confirmDelete(const QString& id, const std::vector<std::string>& consumers);

	// The text that names a section in a list of sections.
	static QString sectionLabel(const SceneSectionDescriptor& section);

	QComboBox* facetCombo() const { return m_facetCombo; }
	QPushButton* addButton() const { return m_addButton; }
	QPushButton* deleteButton() const { return m_deleteButton; }
	QLabel* guidanceLabel() const { return m_guidanceLabel; }
	QComboBox* blockTrackFilterCombo() const { return m_blockTrackFilterCombo; }
	QPushButton* insertBlockButton() const { return m_insertBlockButton; }
	QPushButton* moveBlockUpButton() const { return m_moveBlockUpButton; }
	QPushButton* moveBlockDownButton() const { return m_moveBlockDownButton; }
	QWidget* routeSectionDetailWidget() const { return m_routeSectionDetailWidget; }
	QComboBox* routeSectionCatalogCombo() const { return m_routeSectionCatalogCombo; }
	QListWidget* routeSectionListWidget() const { return m_routeSectionListWidget; }
	QPushButton* addRouteSectionButton() const { return m_addRouteSectionButton; }
	QPushButton* removeRouteSectionButton() const { return m_removeRouteSectionButton; }
	QPushButton* moveRouteSectionUpButton() const { return m_moveRouteSectionUpButton; }
	QPushButton* moveRouteSectionDownButton() const { return m_moveRouteSectionDownButton; }

Q_SIGNALS:
	// The selection has been taken from the table again: after another row or facet is chosen, and after the table has
	// been filled again. `facet` is the key of the chosen facet and `id` the id of the selected entity, empty when no
	// row is selected.
	void selectionChanged(const QString& facet, const QString& id);
	void statusMessage(const QString& text, int timeoutMs);

private:
	// Shows the section catalog and the sections of the selected route, while routes are listed.
	void refreshRouteSectionPanel();
	InfrastructureFacet* currentFacet() const;

	QPointer<SceneDocument> m_document;
	UserPrompts m_ownPrompts;
	UserPrompts* m_prompts = &m_ownPrompts;
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
