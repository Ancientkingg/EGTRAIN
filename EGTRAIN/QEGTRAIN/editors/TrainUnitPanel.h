#ifndef TRAINUNITPANEL_H
#define TRAINUNITPANEL_H

#include "editors/SceneEditorPanel.h"
#include "widgets/UserPrompts.h"

#include <QMetaObject>
#include <QPointer>
#include <QString>

#include <array>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;

// The rolling stock units of the scene: a list to pick one, its id, its traction curve, its nine physical values and
// the references to the files it came from. The number fields keep text that is not a number out of the model.
class TrainUnitPanel : public SceneEditorPanel {
	Q_OBJECT

public:
	static constexpr int PhysicalFieldCount = 9;

	explicit TrainUnitPanel(QWidget* parent = nullptr);

	// Refreshes when the document is replaced.
	void setDocument(SceneDocument* document) override;
	// The messages and questions go through these prompts. The panel uses its own until it is given others; null
	// brings its own back. The prompts must outlive the panel.
	void setPrompts(UserPrompts* prompts);
	void refresh() override;
	// Takes the id, the source references and the number fields that have focus into the document.
	void commitPendingEditors() override;

	void addUnit();
	void duplicateUnit();
	void deleteUnit();
	// Takes the id from the field. An empty or used id is dropped and the field shows the old id again.
	void commitIdEdit();
	void commitSources();
	// Takes the value of the physical field with this index from its editor.
	void commitPhysical(int fieldIndex);
	void addTractionRow();
	void removeTractionRow();
	void commitTractionCell(int row, int column, double value);

	// Selects the unit with this id. False when there is none.
	bool selectUnit(const QString& id);
	// The shown unit, or null.
	const SceneTrainUnit* selectedUnit() const;
	// Shows the state of the source link of the shown unit. `linkedPath` is empty when the source is not linked, and
	// `status` is empty when the link has no status of its own.
	void setSourceState(bool traction, const QString& linkedPath, const QString& status);

	QListWidget* listWidget() const { return m_listWidget; }
	QLineEdit* idEdit() const { return m_idEdit; }
	const std::array<QDoubleSpinBox*, PhysicalFieldCount>& physicalEdits() const { return m_physicalEdits; }
	QLineEdit* sourceDataEdit() const { return m_sourceDataEdit; }
	QLineEdit* sourceTractionEdit() const { return m_sourceTractionEdit; }
	QTableWidget* tractionTable() const { return m_tractionTable; }
	QPushButton* addButton() const { return m_addButton; }
	QPushButton* duplicateButton() const { return m_duplicateButton; }
	QPushButton* deleteButton() const { return m_deleteButton; }
	QPushButton* addTractionRowButton() const { return m_addTractionRowButton; }
	QPushButton* removeTractionRowButton() const { return m_removeTractionRowButton; }
	QPushButton* plotButton() const { return m_plotButton; }
	QPushButton* linkButton(bool traction) const { return traction ? m_linkTractionButton : m_linkDataButton; }
	QPushButton* unlinkButton(bool traction) const { return traction ? m_unlinkTractionButton : m_unlinkDataButton; }
	QPushButton* retryButton(bool traction) const { return traction ? m_retryTractionButton : m_retryDataButton; }
	QLabel* sourceStatusLabel(bool traction) const { return traction ? m_sourceTractionStatusLabel : m_sourceDataStatusLabel; }

Q_SIGNALS:
	// A unit has been added, duplicated, deleted or renamed. The compositions list units by id.
	// Emitted after the document is marked changed and before changed().
	void trainUnitsChanged();
	// The references to the source files of the shown unit have changed.
	void sourceReferencesChanged();
	// The unit has got another id. Emitted after the document is marked changed.
	void unitRenamed(const QString& oldId, const QString& newId);
	// The unit is gone from the model. Emitted after the document is marked changed.
	void unitDeleted(const QString& id);
	// The details of the shown unit are shown again, so the state of its source links has to be shown again too.
	void detailsShown();
	// The plot of the traction curve of this unit is wanted.
	void tractionPlotRequested(const QString& unitId);
	// The user asks to link, unlink or retry the source of the shown unit. `traction` tells which of the two.
	void sourceLinkRequested(bool traction);
	void sourceUnlinkRequested(bool traction);
	void sourceRetryRequested(bool traction);

private:
	bool hasScene() const;
	// The row of the shown unit, or -1.
	int selectedRow() const;
	void updateDetail();
	void refreshTractionTable();

	QPointer<SceneDocument> m_document;
	QMetaObject::Connection m_replacedConnection;
	UserPrompts m_ownPrompts;
	UserPrompts* m_prompts = &m_ownPrompts;
	QListWidget* m_listWidget = nullptr;
	QLineEdit* m_idEdit = nullptr;
	std::array<QDoubleSpinBox*, PhysicalFieldCount> m_physicalEdits{};
	QLineEdit* m_sourceDataEdit = nullptr;
	QLineEdit* m_sourceTractionEdit = nullptr;
	QPushButton* m_linkDataButton = nullptr;
	QPushButton* m_unlinkDataButton = nullptr;
	QPushButton* m_retryDataButton = nullptr;
	QLabel* m_sourceDataStatusLabel = nullptr;
	QPushButton* m_linkTractionButton = nullptr;
	QPushButton* m_unlinkTractionButton = nullptr;
	QPushButton* m_retryTractionButton = nullptr;
	QLabel* m_sourceTractionStatusLabel = nullptr;
	QTableWidget* m_tractionTable = nullptr;
	QPushButton* m_addButton = nullptr;
	QPushButton* m_duplicateButton = nullptr;
	QPushButton* m_deleteButton = nullptr;
	QPushButton* m_addTractionRowButton = nullptr;
	QPushButton* m_removeTractionRowButton = nullptr;
	QPushButton* m_plotButton = nullptr;
};

#endif // TRAINUNITPANEL_H
