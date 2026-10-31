#ifndef ENTRANCEDELAYPANEL_H
#define ENTRANCEDELAYPANEL_H

#include "editors/SceneEditorPanel.h"

#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

/// The entrance delays of the selected scenario: a list to pick one, and the fields of the selected delay, which holds a
/// train back before it enters the network. A delay names one occurrence of a service (counted from 1), the station at
/// which the train is held and the delay in seconds. The choices of occurrences and stations follow the services of the
/// document: a station is offered when the first stop of the service there has a planned departure. The scenario whose delays
/// are shown is the one that the run selection names.
class EntranceDelayPanel : public SceneEditorPanel {
	Q_OBJECT

public:
	/// Makes the widgets, empty until a loaded document is set.
	explicit EntranceDelayPanel(QWidget* parent = nullptr);

	/// Lists the delays of the selected scenario again and keeps the selected row where it still exists.
	void refresh() override;
	/// Takes the occurrence and the delay that are being edited into the document.
	void commitPendingEditors() override;
	/// Shows the fields of the selected delay again.
	void updateDetail();

	/// Appends a delay of 0 seconds to the selected scenario and selects it. It names the first service with a planned
	/// departure and that station, or else the first service and no station. A scene without a scenario gets the
	/// baseline scenario first.
	void addEntranceDelay();
	/// Inserts a copy of the selected delay after it.
	void duplicateEntranceDelay();
	/// Deletes the selected delay after the user has confirmed it.
	void deleteEntranceDelay();
	/// Takes the service at `index` of the service combo. Another service starts at its first occurrence, with the first
	/// station at which it can be held.
	void commitService(int index);
	/// Takes the occurrence from its field.
	void commitOccurrence();
	/// Takes the station at `index` of the station combo.
	void commitStation(int index);
	/// Takes the delay in seconds from its field.
	void commitSeconds();

	/// The selected delay for an edit, or null when none is selected or the scene cannot be edited. A scene without a
	/// scenario gets the baseline scenario first.
	SceneEntranceDelay* selectedEntranceDelay();

	/// The delays of the selected scenario, one row each.
	QListWidget* listWidget() const { return m_listWidget; }
	/// The services of the scene, each with its id as the data. A service that the scene lacks is shown as missing.
	QComboBox* serviceCombo() const { return m_serviceCombo; }
	/// The occurrence of the service, counted from 1, up to the number of occurrences in the counted time, which its suffix
	/// gives.
	QSpinBox* occurrenceEdit() const { return m_occurrenceEdit; }
	/// The label of the chosen occurrence; empty when the delay names no service of the scene.
	QLabel* occurrenceContextLabel() const { return m_occurrenceContextLabel; }
	/// The message above the fields while no entrance delay is selected; hidden otherwise.
	QLabel* emptyStateLabel() const { return m_emptyStateLabel; }
	/// The stations whose first stop of the service has a planned departure, each with its id as the data.
	QComboBox* stationCombo() const { return m_stationCombo; }
	/// The delay in seconds.
	QDoubleSpinBox* secondsEdit() const { return m_secondsEdit; }
	/// The button of addEntranceDelay().
	QPushButton* addButton() const { return m_addButton; }
	/// The button of duplicateEntranceDelay().
	QPushButton* duplicateButton() const { return m_duplicateButton; }
	/// The button of deleteEntranceDelay().
	QPushButton* deleteButton() const { return m_deleteButton; }

protected:
	/// Refreshes when the case settings, the infrastructure or the services change.
	void documentChanged(SceneDocument::ChangeKinds kinds) override;

private:
	// The selected scenario. The second form adds the baseline scenario to a scene that has none, and is null unless the
	// scene can be edited.
	const SceneScenario* scenario() const;
	SceneScenario* scenarioForEdit();
	// The delay in the selected row of the list, or null.
	const SceneEntranceDelay* shownDelay() const;
	// Records that the selected scenario is edited and tells the document.
	void markEdited(const QString& label);

	QListWidget* m_listWidget = nullptr;
	QPushButton* m_addButton = nullptr;
	QPushButton* m_duplicateButton = nullptr;
	QPushButton* m_deleteButton = nullptr;
	QComboBox* m_serviceCombo = nullptr;
	QSpinBox* m_occurrenceEdit = nullptr;
	QLabel* m_occurrenceContextLabel = nullptr;
	QLabel* m_emptyStateLabel = nullptr;
	QComboBox* m_stationCombo = nullptr;
	QDoubleSpinBox* m_secondsEdit = nullptr;
};

#endif // ENTRANCEDELAYPANEL_H
