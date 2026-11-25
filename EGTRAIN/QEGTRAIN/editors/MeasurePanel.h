#ifndef MEASUREPANEL_H
#define MEASUREPANEL_H

#include "editors/SceneEditorPanel.h"

#include <QString>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

/// The dispatching measures of the selected scenario: a list to pick one, and the fields of the selected measure. A measure
/// cancels a service, or one occurrence of it counted from 1, from a time in seconds of the run: completely, or from a station
/// at which the service calls. The scenario whose measures are shown is the one that the run selection names.
class MeasurePanel : public SceneEditorPanel {
	Q_OBJECT

public:
	/// Makes the widgets, empty until a loaded document is set.
	explicit MeasurePanel(QWidget* parent = nullptr);

	/// Lists the measures of the selected scenario again and keeps the selected row where it still exists.
	void refresh() override;
	/// Takes the occurrence and the time that are being edited into the document.
	void commitPendingEditors() override;
	/// Shows the fields of the selected measure again.
	void updateDetail();

	/// Appends a measure that cancels every occurrence of the first service completely at 0 s, with a new unique id, and
	/// selects it. A scene without a scenario gets the baseline scenario first.
	void addMeasure();
	/// Inserts a copy of the selected measure after it, with a new unique id.
	void duplicateMeasure();
	/// Deletes the selected measure and says in the status line that Undo restores it.
	void deleteMeasure();
	/// Takes the service at `index` of the service combo. Another service starts with every occurrence and no station.
	void commitService(int index);
	/// Takes "Every occurrence" from its check box: checked, the measure names no occurrence.
	void commitEveryOccurrence(bool every);
	/// Takes the occurrence from its field.
	void commitOccurrence();
	/// Takes the time in seconds from its field.
	void commitSeconds();
	/// Takes the station at `index` of the station combo; the first entry cancels the service completely.
	void commitStation(int index);

	/// The selected measure, or null when none is selected or the scene cannot be edited.
	const SceneMeasure* selectedMeasure() const;

	/// The measures of the selected scenario, one row each.
	QListWidget* listWidget() const { return m_listWidget; }
	/// The services of the scene, each with its id as the data. A service that the scene lacks is shown as missing.
	QComboBox* serviceCombo() const { return m_serviceCombo; }
	/// Checked when the measure applies to every occurrence of the service.
	QCheckBox* everyOccurrenceCheck() const { return m_everyOccurrenceCheck; }
	/// The occurrence of the service, counted from 1; disabled while every occurrence is chosen.
	QSpinBox* occurrenceEdit() const { return m_occurrenceEdit; }
	/// The time at which the measure takes effect, in seconds of the run.
	QDoubleSpinBox* secondsEdit() const { return m_secondsEdit; }
	/// "(complete)", then the stations at which the service calls, each with its id as the data.
	QComboBox* stationCombo() const { return m_stationCombo; }
	/// The message above the fields while no measure is selected; hidden otherwise.
	QLabel* emptyStateLabel() const { return m_emptyStateLabel; }
	/// The button of addMeasure().
	QPushButton* addButton() const { return m_addButton; }
	/// The button of duplicateMeasure().
	QPushButton* duplicateButton() const { return m_duplicateButton; }
	/// The button of deleteMeasure().
	QPushButton* deleteButton() const { return m_deleteButton; }

protected:
	/// Refreshes when the case settings, the infrastructure or the services change.
	void documentChanged(SceneDocument::ChangeKinds kinds) override;

private:
	// The selected scenario.
	const SceneScenario* scenario() const;
	// The measure in the selected row of the list, or null.
	const SceneMeasure* shownMeasure() const;
	// Applies `mutate` to the measures of the selected scenario in one edit of the document and records that the scenario is
	// edited. False when the document refuses the edit.
	bool editMeasures(const QString& label, const std::function<void(std::vector<SceneMeasure>&)>& mutate);
	// As editMeasures(), for the measure in the selected row of the list, which there must be.
	bool editSelected(const QString& label, const std::function<void(SceneMeasure&)>& mutate);
	// Refreshes, says that the panel changed and selects `row`.
	void showEdited(int row);

	QListWidget* m_listWidget = nullptr;
	QPushButton* m_addButton = nullptr;
	QPushButton* m_duplicateButton = nullptr;
	QPushButton* m_deleteButton = nullptr;
	QComboBox* m_serviceCombo = nullptr;
	QCheckBox* m_everyOccurrenceCheck = nullptr;
	QSpinBox* m_occurrenceEdit = nullptr;
	QDoubleSpinBox* m_secondsEdit = nullptr;
	QComboBox* m_stationCombo = nullptr;
	QLabel* m_emptyStateLabel = nullptr;
};

#endif // MEASUREPANEL_H
