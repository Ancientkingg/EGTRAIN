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

// The compositions of the scene: a list to pick one, its id and its ordered rolling stock units. Renaming a
// composition rewrites the services that use it, and deleting one that a service uses is refused.
class CompositionPanel : public SceneEditorPanel {
	Q_OBJECT

public:
	explicit CompositionPanel(QWidget* parent = nullptr);

	// Refreshes when the document is replaced.
	void setDocument(SceneDocument* document) override;
	// The messages and questions go through these prompts. The panel uses its own until it is given others; null
	// brings its own back. The prompts must outlive the panel.
	void setPrompts(UserPrompts* prompts);
	void refresh() override;
	void commitPendingEditors() override { commitIdEdit(); }
	// Shows the sources and the warning of the selected unit again, for example after its sources have changed.
	void refreshUnitDetails();

	void addComposition();
	void duplicateComposition();
	void deleteComposition();
	// Takes the id from the field. An empty or used id is dropped and the field shows the old id again.
	void commitIdEdit();
	void addUnit();
	void removeUnit();
	void moveUnitUp();
	void moveUnitDown();
	// Asks for the plot of the selected unit, which has to have a traction curve.
	void plotSelectedUnitTraction();

	QListWidget* listWidget() const { return m_listWidget; }
	QLineEdit* idEdit() const { return m_idEdit; }
	QListWidget* unitsListWidget() const { return m_unitsListWidget; }
	QPushButton* addButton() const { return m_addButton; }
	QPushButton* duplicateButton() const { return m_duplicateButton; }
	QPushButton* deleteButton() const { return m_deleteButton; }
	QPushButton* addUnitButton() const { return m_addUnitButton; }
	QPushButton* removeUnitButton() const { return m_removeUnitButton; }
	QPushButton* moveUnitUpButton() const { return m_moveUnitUpButton; }
	QPushButton* moveUnitDownButton() const { return m_moveUnitDownButton; }
	QLabel* unitSourceDataLabel() const { return m_unitSourceDataLabel; }
	QLabel* unitSourceTractionLabel() const { return m_unitSourceTractionLabel; }
	QLabel* unitWarningLabel() const { return m_unitWarningLabel; }
	QPushButton* plotTractionButton() const { return m_plotTractionButton; }

Q_SIGNALS:
	// A composition has been added, duplicated, deleted or renamed. Other lists of composition ids are out of date.
	// Emitted after the document is marked changed and before changed().
	void compositionIdsChanged();
	// The plot of the traction curve of this train unit is wanted.
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
	QLabel* m_unitSourceDataLabel = nullptr;
	QLabel* m_unitSourceTractionLabel = nullptr;
	QLabel* m_unitWarningLabel = nullptr;
	QPushButton* m_plotTractionButton = nullptr;
};

#endif // COMPOSITIONPANEL_H
