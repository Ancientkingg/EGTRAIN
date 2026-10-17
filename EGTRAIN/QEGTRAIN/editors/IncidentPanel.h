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
class QLineEdit;
class QListWidget;
class QPushButton;

// The incidents of the selected scenario: a list to pick one, and the fields of the selected incident. An incident is a
// signal failure or a train breakdown, and the targets it can have depend on that type: a signal or a block, or one
// occurrence of a service. The choices of targets follow the infrastructure and the services of the document. The
// scenario whose incidents are shown is the one that the run selection names.
class IncidentPanel : public SceneEditorPanel {
	Q_OBJECT

public:
	explicit IncidentPanel(QWidget* parent = nullptr);

	// Refreshes when the document is replaced, and shows the choices of targets again when the case settings, the
	// infrastructure or the services change.
	void setDocument(SceneDocument* document) override;
	// The messages and questions go through these prompts. The panel uses its own until it is given others; null
	// brings its own back. The prompts must outlive the panel.
	void setPrompts(UserPrompts* prompts);
	// The scenario that is shown and the scenarios that are edited are kept here. The panel uses its own until it is
	// given another; null brings its own back. The selection must outlive the panel.
	void setRunSelection(RunSelection* selection);
	// The time in seconds over which the occurrences of a service are counted. Without one the duration of the scene
	// settings is used.
	void setOccurrenceDuration(std::function<double()> duration);
	// Lists the incidents of the selected scenario again and keeps the selected row where it still exists.
	void refresh() override;
	// Takes the fields that are being edited into the document.
	void commitPendingEditors() override;
	// Shows the fields of the selected incident again.
	void updateDetail();
	// Lists the choices of targets of the selected incident again.
	void refreshTargets();

	void addIncident();
	void duplicateIncident();
	void deleteIncident();
	// Takes the id from the field. An empty or used id is dropped and the field shows the old id again.
	void commitIdEdit();
	// Another type keeps only the target that is valid for it, and a signal failure loses what only a breakdown has.
	void commitType(const QString& type);
	void commitTarget();
	void commitStartSeconds();
	void commitEndSeconds();
	void commitReducedSpeed();
	void commitHasReducedSpeed(bool checked);
	void commitHasEndSeconds(bool checked);
	void commitTerminateAtDestination(bool checked);

	// The selected incident, or null.
	SceneIncident* selectedIncident();

	QListWidget* listWidget() const { return m_listWidget; }
	QLineEdit* idEdit() const { return m_idEdit; }
	QComboBox* typeCombo() const { return m_typeCombo; }
	QComboBox* targetCombo() const { return m_targetCombo; }
	QLineEdit* startSecondsEdit() const { return m_startSecondsEdit; }
	QLineEdit* endSecondsEdit() const { return m_endSecondsEdit; }
	QCheckBox* hasEndSecondsCheck() const { return m_hasEndSecondsCheck; }
	QCheckBox* hasReducedSpeedCheck() const { return m_hasReducedSpeedCheck; }
	QDoubleSpinBox* reducedSpeedKmhEdit() const { return m_reducedSpeedKmhEdit; }
	QCheckBox* terminateAtDestinationCheck() const { return m_terminateAtDestinationCheck; }
	QPushButton* addButton() const { return m_addButton; }
	QPushButton* duplicateButton() const { return m_duplicateButton; }
	QPushButton* deleteButton() const { return m_deleteButton; }

private:
	bool hasScene() const;
	// False while a run uses the scene.
	bool canEdit() const;
	double occurrenceDuration() const;
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

	QListWidget* m_listWidget = nullptr;
	QPushButton* m_addButton = nullptr;
	QPushButton* m_duplicateButton = nullptr;
	QPushButton* m_deleteButton = nullptr;
	QLineEdit* m_idEdit = nullptr;
	QComboBox* m_typeCombo = nullptr;
	QComboBox* m_targetCombo = nullptr;
	QLineEdit* m_startSecondsEdit = nullptr;
	QLineEdit* m_endSecondsEdit = nullptr;
	QCheckBox* m_hasEndSecondsCheck = nullptr;
	QCheckBox* m_hasReducedSpeedCheck = nullptr;
	QDoubleSpinBox* m_reducedSpeedKmhEdit = nullptr;
	QCheckBox* m_terminateAtDestinationCheck = nullptr;
};

#endif // INCIDENTPANEL_H
