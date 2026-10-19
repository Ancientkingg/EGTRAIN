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
class QTabWidget;
class QTableWidget;

/// The rolling stock units of the scene: a list to pick one, its id, its traction curve, its nine physical values and
/// the references to the files it came from. The number fields keep text that is not a number out of the model.
///
/// The physical values, in the order of physicalEdits(): traction unit mass (kg), wagon mass (kg), wagon count, maximum
/// speed (m/s), maximum deceleration (m/s²), frontal area (m²), resistance coefficient (the factor of the speed term of
/// the wagon resistance; no unit is stated), jerk (m/s³) and length (m). A traction curve row holds the lower and upper
/// speed of its band in m/s and the coefficients c0, c1 and c2 of the tractive effort c0 + c1 v + c2 v² in N, with v in
/// m/s.
class TrainUnitPanel : public SceneEditorPanel {
	Q_OBJECT

public:
	/// The number of physical values of a unit.
	static constexpr int PhysicalFieldCount = 9;

	/// Makes the widgets, empty until a loaded document is set.
	explicit TrainUnitPanel(QWidget* parent = nullptr);

	/// Refreshes when the document is replaced.
	void setDocument(SceneDocument* document) override;
	/// The messages and questions go through these prompts. The panel uses its own until it is given others; null
	/// brings its own back. The prompts must outlive the panel.
	void setPrompts(UserPrompts* prompts);
	/// Lists the units again and shows the selected one.
	void refresh() override;
	/// Takes the id, the source references and the number fields that have focus into the document.
	void commitPendingEditors() override;

	/// Appends a unit with a new unique id and selects it.
	void addUnit();
	/// Inserts a copy of the selected unit, with a new unique id, after it.
	void duplicateUnit();
	/// Deletes the selected unit after the user has confirmed it; refused while a composition uses it.
	void deleteUnit();
	/// Takes the id from the field. An empty or used id is dropped and the field shows the old id again.
	void commitIdEdit();
	/// Takes the parameter and traction source references from their fields.
	void commitSources();
	/// Takes the value of the physical field with this index, 0 to PhysicalFieldCount - 1, from its editor. The unit then
	/// has physical values.
	void commitPhysical(int fieldIndex);
	/// Appends a traction curve row whose speed band starts at the upper speed of the last row, or at 0 m/s when the
	/// curve is empty, and is 1 m/s wide, with zero coefficients.
	void addTractionRow();
	/// Removes the selected traction curve row.
	void removeTractionRow();
	/// Stores `value` in `column` (0 to 4) of traction curve row `row`: a speed in m/s, or a coefficient of the force in N with the speed in m/s.
	void commitTractionCell(int row, int column, double value);

	/// Selects the unit with this id. False when there is none.
	bool selectUnit(const QString& id);
	/// The shown unit, or null.
	const SceneTrainUnit* selectedUnit() const;
	/// Shows the state of the source link of the shown unit. `traction` chooses the traction file over the parameter
	/// file. `linkedPath` is empty when the source is not linked, and `status` is empty when the link has no status of its
	/// own. A linked file that does not exist is shown as missing, with its path, whatever the status says.
	void setSourceState(bool traction, const QString& linkedPath, const QString& status);

	/// The units, by id.
	QListWidget* listWidget() const { return m_listWidget; }
	/// The field of the id of the shown unit.
	QLineEdit* idEdit() const { return m_idEdit; }
	/// The message shown while a scene is loaded and no unit is selected; empty otherwise.
	QLabel* emptyStateLabel() const { return m_emptyStateLabel; }
	/// The "Traction" and "Physical" pages of the shown unit.
	QTabWidget* tabs() const { return m_tabs; }
	/// The editors of the physical values, in the order and units of the class description.
	const std::array<QDoubleSpinBox*, PhysicalFieldCount>& physicalEdits() const { return m_physicalEdits; }
	/// The reference to the parameter file of the unit; it may be empty.
	QLineEdit* sourceDataEdit() const { return m_sourceDataEdit; }
	/// The reference to the traction file of the unit; it may be empty.
	QLineEdit* sourceTractionEdit() const { return m_sourceTractionEdit; }
	/// The traction curve, one row per speed band: lower and upper speed in m/s, then c0, c1 and c2.
	QTableWidget* tractionTable() const { return m_tractionTable; }
	/// The button of addUnit().
	QPushButton* addButton() const { return m_addButton; }
	/// The button of duplicateUnit().
	QPushButton* duplicateButton() const { return m_duplicateButton; }
	/// The button of deleteUnit().
	QPushButton* deleteButton() const { return m_deleteButton; }
	/// The button of addTractionRow().
	QPushButton* addTractionRowButton() const { return m_addTractionRowButton; }
	/// The button of removeTractionRow().
	QPushButton* removeTractionRowButton() const { return m_removeTractionRowButton; }
	/// The button that asks for the plot of the traction curve of the shown unit.
	QPushButton* plotButton() const { return m_plotButton; }
	/// The button that links the traction file (`traction` true) or the parameter file.
	QPushButton* linkButton(bool traction) const { return traction ? m_linkTractionButton : m_linkDataButton; }
	/// The button that unlinks the traction file (`traction` true) or the parameter file.
	QPushButton* unlinkButton(bool traction) const { return traction ? m_unlinkTractionButton : m_unlinkDataButton; }
	/// The button that takes the linked traction file (`traction` true) or parameter file again.
	QPushButton* retryButton(bool traction) const { return traction ? m_retryTractionButton : m_retryDataButton; }
	/// The state of the link of the traction file (`traction` true) or the parameter file.
	QLabel* sourceStatusLabel(bool traction) const { return traction ? m_sourceTractionStatusLabel : m_sourceDataStatusLabel; }

Q_SIGNALS:
	/// A unit has been added, duplicated, deleted or renamed. The compositions list units by id.
	/// Emitted after the document is marked changed and before changed().
	void trainUnitsChanged();
	/// The references to the source files of the shown unit have changed.
	void sourceReferencesChanged();
	/// The unit has got another id. Emitted after the document is marked changed.
	void unitRenamed(const QString& oldId, const QString& newId);
	/// The unit is gone from the model. Emitted after the document is marked changed.
	void unitDeleted(const QString& id);
	/// The details of the shown unit are shown again, so the state of its source links has to be shown again too.
	void detailsShown();
	/// The plot of the traction curve of this unit is wanted.
	void tractionPlotRequested(const QString& unitId);
	/// The user asks to link the source of the shown unit. `traction` tells whether it is the traction file or the
	/// parameter file.
	void sourceLinkRequested(bool traction);
	/// The user asks to unlink the traction file (`traction` true) or the parameter file of the shown unit.
	void sourceUnlinkRequested(bool traction);
	/// The user asks to take the linked traction file (`traction` true) or parameter file of the shown unit again.
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
	QLabel* m_emptyStateLabel = nullptr;
	QTabWidget* m_tabs = nullptr;
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
