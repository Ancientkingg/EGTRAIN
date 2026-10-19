#ifndef COMPOSITIONPANEL_H
#define COMPOSITIONPANEL_H

#include "editors/SceneEditorPanel.h"
#include "widgets/UserPrompts.h"

#include <QMetaObject>
#include <QPointer>
#include <QString>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

/// The compositions of the scene: a list to pick one, its id and its ordered rolling stock units. Renaming a
/// composition rewrites the services that use it, and deleting one that a service uses is refused.
class CompositionPanel : public SceneEditorPanel {
	Q_OBJECT

public:
	/// Makes the widgets, empty until a loaded document is set.
	explicit CompositionPanel(QWidget* parent = nullptr);

	/// Refreshes when the document is replaced.
	void setDocument(SceneDocument* document) override;
	/// The messages and questions go through these prompts. The panel uses its own until it is given others; null
	/// brings its own back. The prompts must outlive the panel.
	void setPrompts(UserPrompts* prompts);
	void refresh() override;
	/// Takes the id that is being edited into the document.
	void commitPendingEditors() override { commitIdEdit(); }
	/// Shows the sources and the warning of the selected unit again, for example after its sources have changed.
	void refreshUnitDetails();

	/// Appends a composition without units, with a new unique id, and selects it.
	void addComposition();
	/// Inserts a copy of the selected composition, with a new unique id, after it.
	void duplicateComposition();
	/// Deletes the selected composition after the user has confirmed it; refused while a service uses it.
	void deleteComposition();
	/// Takes the id from the field. An empty or used id is dropped and the field shows the old id again.
	void commitIdEdit();
	/// Asks for a rolling stock unit of the scene and appends it to the units of the selected composition.
	void addUnit();
	/// Removes the selected unit from the selected composition.
	void removeUnit();
	/// Moves the selected unit one place towards the front of the composition.
	void moveUnitUp();
	/// Moves the selected unit one place towards the back of the composition.
	void moveUnitDown();
	/// Asks for the plot of the selected unit, which has to have a traction curve.
	void plotSelectedUnitTraction();

	/// The list of the compositions, by id.
	QListWidget* listWidget() const { return m_listWidget; }
	/// The field of the id of the selected composition.
	QLineEdit* idEdit() const { return m_idEdit; }
	/// The units of the selected composition in order, by id. A unit can appear more than once.
	QListWidget* unitsListWidget() const { return m_unitsListWidget; }
	/// The button of addComposition().
	QPushButton* addButton() const { return m_addButton; }
	/// The button of duplicateComposition().
	QPushButton* duplicateButton() const { return m_duplicateButton; }
	/// The button of deleteComposition().
	QPushButton* deleteButton() const { return m_deleteButton; }
	/// The button of addUnit().
	QPushButton* addUnitButton() const { return m_addUnitButton; }
	/// The button of removeUnit().
	QPushButton* removeUnitButton() const { return m_removeUnitButton; }
	/// The button of moveUnitUp().
	QPushButton* moveUnitUpButton() const { return m_moveUnitUpButton; }
	/// The button of moveUnitDown().
	QPushButton* moveUnitDownButton() const { return m_moveUnitDownButton; }
	/// The message shown while a scene is loaded and no composition is selected; empty otherwise.
	QLabel* emptyStateLabel() const { return m_emptyStateLabel; }
	/// The message shown while the selected composition has no units.
	QLabel* unitsEmptyStateLabel() const { return m_unitsEmptyStateLabel; }
	/// The parameter source file of the selected unit, or "(none)".
	QLabel* unitSourceDataLabel() const { return m_unitSourceDataLabel; }
	/// The traction source file of the selected unit, or "(none)".
	QLabel* unitSourceTractionLabel() const { return m_unitSourceTractionLabel; }
	/// The warning about the selected unit: a unit that the scene lacks, or one without a traction curve.
	QLabel* unitWarningLabel() const { return m_unitWarningLabel; }
	/// The button of plotSelectedUnitTraction(), enabled when the selected unit has a traction curve.
	QPushButton* plotTractionButton() const { return m_plotTractionButton; }

Q_SIGNALS:
	/// A composition has been added, duplicated, deleted or renamed. Other lists of composition ids are out of date.
	/// Emitted after the document is marked changed and before changed().
	void compositionIdsChanged();
	/// The plot of the traction curve of this train unit is wanted.
	void tractionPlotRequested(const QString& unitId);

private:
	bool hasScene() const;
	// The selected composition, or -1.
	int selectedRow() const;
	void updateDetail();

	QPointer<SceneDocument> m_document;
	QMetaObject::Connection m_replacedConnection;
	UserPrompts m_ownPrompts;
	UserPrompts* m_prompts = &m_ownPrompts;
	QListWidget* m_listWidget = nullptr;
	QLineEdit* m_idEdit = nullptr;
	QListWidget* m_unitsListWidget = nullptr;
	QPushButton* m_addButton = nullptr;
	QPushButton* m_duplicateButton = nullptr;
	QPushButton* m_deleteButton = nullptr;
	QPushButton* m_addUnitButton = nullptr;
	QPushButton* m_removeUnitButton = nullptr;
	QPushButton* m_moveUnitUpButton = nullptr;
	QPushButton* m_moveUnitDownButton = nullptr;
	QLabel* m_emptyStateLabel = nullptr;
	QLabel* m_unitsEmptyStateLabel = nullptr;
	QLabel* m_unitSourceDataLabel = nullptr;
	QLabel* m_unitSourceTractionLabel = nullptr;
	QLabel* m_unitWarningLabel = nullptr;
	QPushButton* m_plotTractionButton = nullptr;
};

#endif // COMPOSITIONPANEL_H
