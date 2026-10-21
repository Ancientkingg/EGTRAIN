#ifndef INCIDENTPANEL_H
#define INCIDENTPANEL_H

#include "editors/SceneEditorPanel.h"
// After SceneEditorPanel.h, which includes the scene model the way RunSelection.h needs it.
#include "app/RunSelection.h"
#include "widgets/UserPrompts.h"

#include <QMetaObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QHideEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QToolButton;

/// A target that an incident gets from outside the panel, such as from the selection on the network view.
struct IncidentTarget {
	QString type;		///< "signal_failure" or "train_breakdown"; empty for no target.
	QString id;			///< The signal or block of a signal failure, or the service of a breakdown.
	int occurrence = 0; ///< The occurrence of the service of a breakdown, counted from 1; 0 for a signal failure.
};

/// The incidents of the selected scenario: a list to pick one, and the fields of the selected incident. An incident is a
/// signal failure or a train breakdown, and the targets it can have depend on that type: a signal or a block, or one
/// occurrence of a service. The choices of targets follow the infrastructure and the services of the document. The
/// scenario whose incidents are shown is the one that the run selection names. Start and end are whole seconds from the
/// start of the run, and the reduced speed is in km/h. The button next to the target combo starts picking the target on the
/// network view; the window does the picking and gives the panel the target.
class IncidentPanel : public SceneEditorPanel {
	Q_OBJECT

public:
	/// Makes the widgets, empty until a loaded document is set.
	explicit IncidentPanel(QWidget* parent = nullptr);

	/// Refreshes when the document is replaced, and shows the choices of targets again when the case settings, the
	/// infrastructure or the services change.
	void setDocument(SceneDocument* document) override;
	/// The messages and questions go through these prompts. The panel uses its own until it is given others; null
	/// brings its own back. The prompts must outlive the panel.
	void setPrompts(UserPrompts* prompts);
	/// The scenario that is shown and the scenarios that are edited are kept here. The panel uses its own until it is
	/// given another; null brings its own back. The selection must outlive the panel.
	void setRunSelection(RunSelection* selection);
	/// Gives the time in seconds over which the occurrences of a service are counted. Without one the duration of the
	/// scene settings is used.
	void setOccurrenceDuration(std::function<double()> duration);
	/// Gives the target that addIncident() gives a new incident. It is asked before the incident is added. A target that the
	/// combo does not offer, or none, leaves the new incident a signal failure without a target.
	void setDefaultTarget(std::function<IncidentTarget()> target);
	/// Lists the incidents of the selected scenario again and keeps the selected row where it still exists. Ends picking a
	/// target.
	void refresh() override;
	/// Takes the fields that are being edited into the document.
	void commitPendingEditors() override;
	/// Shows the fields of the selected incident again.
	void updateDetail();
	/// Lists the choices of targets of the selected incident again.
	void refreshTargets();

	/// Appends an incident with a new unique id and start and end at 0 seconds, and selects it. It has the target of
	/// setDefaultTarget(), and the type of that target, when the combo offers it; otherwise it is a signal failure without
	/// a target. A scene without a scenario gets the baseline scenario first.
	void addIncident();
	/// Inserts a copy of the selected incident, with a new unique id, after it.
	void duplicateIncident();
	/// Deletes the selected incident after the user has confirmed it.
	void deleteIncident();
	/// Takes the id from the field. An empty or used id is dropped and the field shows the old id again.
	void commitIdEdit();
	/// Sets the type, "signal_failure" or "train_breakdown". Another type keeps only the target that is valid for it, and
	/// a signal failure loses what only a breakdown has.
	void commitType(const QString& type);
	/// Takes the target from its combo: a signal or block, or a service with one occurrence or, for an older incident,
	/// all of them.
	void commitTarget();
	/// Gives the selected incident a target as the combos would: its type changes first when the target has another type,
	/// and the type and the target are one edit. False, and nothing changes, when no incident can be edited or the combo
	/// does not offer the target for its type.
	bool applyTarget(const IncidentTarget& target);
	/// True when the combo offers the target for its type: for a signal failure a signal or block of the document, for a
	/// breakdown an occurrence of a service from 1 to the number of its occurrences in the occurrence duration.
	bool offersTarget(const IncidentTarget& target) const;
	/// True while the pick button is checked: the window takes the next signal or train that is selected on the network
	/// view as the target.
	bool isPickingTarget() const;
	/// Unchecks the pick button. The panel ends picking itself when another incident is selected, when the incidents are
	/// listed again, when the target combo is disabled and when the panel is hidden.
	void endTargetPick();
	/// Takes the start from its field, in whole seconds; text that is no number counts as 0.
	void commitStartSeconds();
	/// Takes the end from its field, in whole seconds, and marks that the incident has one; text that is no number
	/// counts as 0.
	void commitEndSeconds();
	/// Takes the reduced speed in km/h from its field and marks that the incident has one.
	void commitReducedSpeed();
	/// Gives the incident the reduced speed of the field, or none (0 km/h) when `checked` is false.
	void commitHasReducedSpeed(bool checked);
	/// Gives the incident an end, or none (0 seconds) when `checked` is false.
	void commitHasEndSeconds(bool checked);
	/// Sets the terminate-at-destination request of a breakdown (see the scene schema).
	void commitTerminateAtDestination(bool checked);

	/// The selected incident for an edit, or null when none is selected or the scene cannot be edited. A scene without a
	/// scenario gets the baseline scenario first.
	SceneIncident* selectedIncident();

	/// The incidents of the selected scenario, one row each.
	QListWidget* listWidget() const { return m_listWidget; }
	/// The field of the id of the selected incident.
	QLineEdit* idEdit() const { return m_idEdit; }
	/// The type: "signal_failure" or "train_breakdown".
	QComboBox* typeCombo() const { return m_typeCombo; }
	/// The targets for the type. The data of an item is the target id, and its data in role Qt::UserRole + 1 the
	/// occurrence, 0 for all occurrences of a service.
	QComboBox* targetCombo() const { return m_targetCombo; }
	/// The checkable button next to the target combo that starts and ends picking the target on the network view. It is
	/// enabled when the combo is.
	QToolButton* pickTargetButton() const { return m_pickTargetButton; }
	/// The start in whole seconds from the start of the run.
	QLineEdit* startSecondsEdit() const { return m_startSecondsEdit; }
	/// The end (the recovery) in whole seconds from the start of the run.
	QLineEdit* endSecondsEdit() const { return m_endSecondsEdit; }
	/// Whether the incident has an end. A reduced-speed breakdown without one lasts until the train reaches its
	/// destination.
	QCheckBox* hasEndSecondsCheck() const { return m_hasEndSecondsCheck; }
	/// Whether a breakdown caps the speed of the train instead of stopping it.
	QCheckBox* hasReducedSpeedCheck() const { return m_hasReducedSpeedCheck; }
	/// The speed cap of a breakdown in km/h.
	QDoubleSpinBox* reducedSpeedKmhEdit() const { return m_reducedSpeedKmhEdit; }
	/// The terminate-at-destination request of a breakdown.
	QCheckBox* terminateAtDestinationCheck() const { return m_terminateAtDestinationCheck; }
	/// The message above the fields while no incident is selected; hidden otherwise.
	QLabel* emptyStateLabel() const { return m_emptyStateLabel; }
	/// The button of addIncident().
	QPushButton* addButton() const { return m_addButton; }
	/// The button of duplicateIncident().
	QPushButton* duplicateButton() const { return m_duplicateButton; }
	/// The button of deleteIncident().
	QPushButton* deleteButton() const { return m_deleteButton; }

Q_SIGNALS:
	/// The pick button was checked (true) or unchecked (false).
	void targetPickToggled(bool picking);

protected:
	/// Ends picking, unless the window system hid the panel, as when the window is minimized.
	void hideEvent(QHideEvent* event) override;

private:
	bool hasScene() const;
	// False while a run uses the scene.
	bool canEdit() const;
	double occurrenceDuration() const;
	// Gives the incident another type: a signal failure loses what only a breakdown has, and a target that is not valid for
	// the type is dropped.
	void changeType(SceneIncident& incident, const std::string& type) const;
	// The incidents of the selected scenario. The second form adds the baseline scenario to a scene that has none, and
	// is null unless the scene can be edited.
	const std::vector<SceneIncident>& scenarioIncidents() const;
	std::vector<SceneIncident>* scenarioIncidentsForEdit();
	// The incident in the selected row of the list, or null.
	const SceneIncident* shownIncident() const;
	// Records that the selected scenario is edited and tells the document.
	void markEdited(const QString& label);

	QPointer<SceneDocument> m_document;
	QMetaObject::Connection m_replacedConnection;
	QMetaObject::Connection m_changedConnection;
	UserPrompts m_ownPrompts;
	UserPrompts* m_prompts = &m_ownPrompts;
	RunSelection m_ownRunSelection;
	RunSelection* m_runSelection = &m_ownRunSelection;
	std::function<double()> m_occurrenceDuration;
	std::function<IncidentTarget()> m_defaultTarget;

	QListWidget* m_listWidget = nullptr;
	QPushButton* m_addButton = nullptr;
	QPushButton* m_duplicateButton = nullptr;
	QPushButton* m_deleteButton = nullptr;
	QLineEdit* m_idEdit = nullptr;
	QComboBox* m_typeCombo = nullptr;
	QComboBox* m_targetCombo = nullptr;
	QToolButton* m_pickTargetButton = nullptr;
	QLineEdit* m_startSecondsEdit = nullptr;
	QLineEdit* m_endSecondsEdit = nullptr;
	QCheckBox* m_hasEndSecondsCheck = nullptr;
	QCheckBox* m_hasReducedSpeedCheck = nullptr;
	QDoubleSpinBox* m_reducedSpeedKmhEdit = nullptr;
	QCheckBox* m_terminateAtDestinationCheck = nullptr;
	QLabel* m_emptyStateLabel = nullptr;
};

#endif // INCIDENTPANEL_H
