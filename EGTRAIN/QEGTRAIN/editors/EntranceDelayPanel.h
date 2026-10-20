#ifndef ENTRANCEDELAYPANEL_H
#define ENTRANCEDELAYPANEL_H

#include "editors/SceneEditorPanel.h"
// After SceneEditorPanel.h, which includes the scene model the way RunSelection.h needs it.
#include "app/RunSelection.h"
#include "widgets/UserPrompts.h"

#include <QMetaObject>
#include <QPointer>
#include <QString>

#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

// The entrance delays of the selected scenario: a list to pick one, and the fields of the selected delay, which holds a
// train back before it enters the network. A delay names one occurrence of a service and the station at which the
// train is held. The choices of occurrences and stations follow the services of the document. The scenario whose delays
// are shown is the one that the run selection names.
class EntranceDelayPanel : public SceneEditorPanel {
	Q_OBJECT

public:
	explicit EntranceDelayPanel(QWidget* parent = nullptr);

	// Refreshes when the document is replaced, and when the case settings, the infrastructure or the services change.
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
	// Lists the delays of the selected scenario again and keeps the selected row where it still exists.
	void refresh() override;
	// Takes the occurrence and the delay that are being edited into the document.
	void commitPendingEditors() override;
	// Shows the fields of the selected delay again.
	void updateDetail();

	void addEntranceDelay();
	void duplicateEntranceDelay();
	void deleteEntranceDelay();
	// Another service starts at its first occurrence, with the first station at which it can be held.
	void commitService(int index);
	void commitOccurrence();
	void commitStation(int index);
	void commitSeconds();

	// The selected delay, or null.
	SceneEntranceDelay* selectedEntranceDelay();

	QListWidget* listWidget() const { return m_listWidget; }
	QComboBox* serviceCombo() const { return m_serviceCombo; }
	QSpinBox* occurrenceEdit() const { return m_occurrenceEdit; }
	QLabel* occurrenceContextLabel() const { return m_occurrenceContextLabel; }
	// The message above the fields while no entrance delay is selected; hidden otherwise.
	QLabel* emptyStateLabel() const { return m_emptyStateLabel; }
	QComboBox* stationCombo() const { return m_stationCombo; }
	QDoubleSpinBox* secondsEdit() const { return m_secondsEdit; }
	QPushButton* addButton() const { return m_addButton; }
	QPushButton* duplicateButton() const { return m_duplicateButton; }
	QPushButton* deleteButton() const { return m_deleteButton; }

private:
	bool hasScene() const;
	// False while a run uses the scene.
	bool canEdit() const;
	double occurrenceDuration() const;
	// The selected scenario. The second form adds the baseline scenario to a scene that has none, and is null unless the
	// scene can be edited.
	const SceneScenario* scenario() const;
	SceneScenario* scenarioForEdit();
	// The delay in the selected row of the list, or null.
	const SceneEntranceDelay* shownDelay() const;
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
	QComboBox* m_serviceCombo = nullptr;
	QSpinBox* m_occurrenceEdit = nullptr;
	QLabel* m_occurrenceContextLabel = nullptr;
	QLabel* m_emptyStateLabel = nullptr;
	QComboBox* m_stationCombo = nullptr;
	QDoubleSpinBox* m_secondsEdit = nullptr;
};

#endif // ENTRANCEDELAYPANEL_H
