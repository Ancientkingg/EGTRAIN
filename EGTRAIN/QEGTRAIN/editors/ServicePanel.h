#ifndef SERVICEPANEL_H
#define SERVICEPANEL_H

#include "editors/SceneEditorPanel.h"
// After SceneEditorPanel.h, which includes the scene model the way RunSelection.h needs it.
#include "app/RunSelection.h"
#include "widgets/UserPrompts.h"

#include <QMetaObject>
#include <QPointer>
#include <QString>

#include <functional>

class ColorChoiceButton;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QTableWidgetItem;
class QVBoxLayout;

// The services of the scene: a list to pick one, the fields of the selected service, the place for its timetable
// and a tab that lists the occurrences the services generate and lets the user leave some out of the next run.
// Renaming a service rewrites the entities that refer to it, and deleting one that is referred to is refused.
class ServicePanel : public SceneEditorPanel {
	Q_OBJECT

public:
	explicit ServicePanel(QWidget* parent = nullptr);

	// Refreshes when the document is replaced.
	void setDocument(SceneDocument* document) override;
	// The messages and questions go through these prompts. The panel uses its own until it is given others; null
	// brings its own back. The prompts must outlive the panel.
	void setPrompts(UserPrompts* prompts);
	// The occurrences that the next run leaves out are kept here. The panel uses its own until it is given another;
	// null brings its own back. The selection must outlive the panel.
	void setRunSelection(RunSelection* selection);
	// The time in seconds over which the occurrences of a service are counted. Without one the duration of the scene
	// settings is used.
	void setOccurrenceDuration(std::function<double()> duration);
	// Puts the timetable of the selected service under the fields of the service. The widget becomes a child of the
	// panel.
	void setTimetableWidget(QWidget* widget);
	void refresh() override;
	// Takes the id and the fields that are being edited into the document.
	void commitPendingEditors() override;
	// Shows the fields of the selected service again.
	void updateDetail();
	// Lists the occurrences again.
	void refreshOccurrencePreview();

	void addService();
	void duplicateService();
	void deleteService();
	// Takes the id from the field. An empty or used id is dropped and the field shows the old id again.
	void commitIdEdit();
	void commitOperatingCode();
	void commitCategory(int index);
	void commitVisualizationColor(const QString& text);
	void commitComposition(const QString& text);
	void commitRoute(int index);
	void commitHasEntryTime(bool checked);
	void commitEntryTimeSeconds();
	void commitHasRepeat(bool checked);
	void commitHeadwaySeconds();
	void commitHasRepeatCount(bool checked);
	void commitRepeatCount();
	void commitPerformancePercent(double value);
	void commitHasMaximumSpeed(bool checked);
	void commitMaximumSpeed();
	void commitHasOperatingCodeStep(bool checked);
	void commitOperatingCodeStep();
	// Includes every occurrence in the next run, or none.
	void selectAllOccurrences();
	void selectNoneOccurrences();

	// What a generated service is called in lists: its service code, its route and its scheduled entry.
	static QString generatedServiceLabel(const SceneService& service, int occurrence);

	QListWidget* listWidget() const { return m_listWidget; }
	QLineEdit* idEdit() const { return m_idEdit; }
	QLineEdit* operatingCodeEdit() const { return m_operatingCodeEdit; }
	QComboBox* categoryCombo() const { return m_categoryCombo; }
	ColorChoiceButton* colorButton() const { return m_colorButton; }
	QComboBox* compositionCombo() const { return m_compositionCombo; }
	QComboBox* routeCombo() const { return m_routeCombo; }
	QCheckBox* hasEntryTimeCheck() const { return m_hasEntryTimeCheck; }
	QLineEdit* entryTimeSecondsEdit() const { return m_entryTimeSecondsEdit; }
	QCheckBox* hasRepeatCheck() const { return m_hasRepeatCheck; }
	QLineEdit* headwaySecondsEdit() const { return m_headwaySecondsEdit; }
	QCheckBox* hasRepeatCountCheck() const { return m_hasRepeatCountCheck; }
	QLineEdit* repeatCountEdit() const { return m_repeatCountEdit; }
	QDoubleSpinBox* performancePercentEdit() const { return m_performancePercentEdit; }
	QCheckBox* hasMaximumSpeedCheck() const { return m_hasMaximumSpeedCheck; }
	QDoubleSpinBox* maximumSpeedKmhEdit() const { return m_maximumSpeedKmhEdit; }
	QCheckBox* hasOperatingCodeStepCheck() const { return m_hasOperatingCodeStepCheck; }
	QLineEdit* operatingCodeStepEdit() const { return m_operatingCodeStepEdit; }
	QPushButton* addButton() const { return m_addButton; }
	QPushButton* duplicateButton() const { return m_duplicateButton; }
	QPushButton* deleteButton() const { return m_deleteButton; }
	QTabWidget* pages() const { return m_pages; }
	QTableWidget* occurrenceTable() const { return m_occurrenceTable; }
	QPushButton* selectAllButton() const { return m_selectAllButton; }
	QPushButton* selectNoneButton() const { return m_selectNoneButton; }
	QLabel* occurrenceSelectionLabel() const { return m_occurrenceSelectionLabel; }

Q_SIGNALS:
	// The service has got another id and the entities that referred to it have followed. Emitted after the document
	// is marked changed.
	void serviceRenamed(const QString& oldId, const QString& newId);
	// The occurrences are listed again, so what is built from them, the entrance delays and the targets of the
	// incidents, is out of date.
	void occurrencesChanged();
	// The timetable of this service, or of none when the id is empty, has to be shown again: another service is
	// selected, or the id or the route of the selected one has changed.
	void selectedServiceChanged(const QString& serviceId);

private:
	bool hasScene() const;
	// False while a run uses the scene.
	bool canEdit() const;
	// The selected service, or -1.
	int selectedRow() const;
	double occurrenceDuration() const;
	void updateOccurrenceSelection(QTableWidgetItem* item);

	QPointer<SceneDocument> m_document;
	QMetaObject::Connection m_replacedConnection;
	UserPrompts m_ownPrompts;
	UserPrompts* m_prompts = &m_ownPrompts;
	RunSelection m_ownRunSelection;
	RunSelection* m_runSelection = &m_ownRunSelection;
	std::function<double()> m_occurrenceDuration;
	bool m_updatingOccurrencePreview = false;

	QListWidget* m_listWidget = nullptr;
	QPushButton* m_addButton = nullptr;
	QPushButton* m_duplicateButton = nullptr;
	QPushButton* m_deleteButton = nullptr;
	QLineEdit* m_idEdit = nullptr;
	QLineEdit* m_operatingCodeEdit = nullptr;
	QComboBox* m_categoryCombo = nullptr;
	ColorChoiceButton* m_colorButton = nullptr;
	QComboBox* m_compositionCombo = nullptr;
	QComboBox* m_routeCombo = nullptr;
	QCheckBox* m_hasEntryTimeCheck = nullptr;
	QLineEdit* m_entryTimeSecondsEdit = nullptr;
	QCheckBox* m_hasRepeatCheck = nullptr;
	QLineEdit* m_headwaySecondsEdit = nullptr;
	QCheckBox* m_hasRepeatCountCheck = nullptr;
	QLineEdit* m_repeatCountEdit = nullptr;
	QDoubleSpinBox* m_performancePercentEdit = nullptr;
	QCheckBox* m_hasMaximumSpeedCheck = nullptr;
	QDoubleSpinBox* m_maximumSpeedKmhEdit = nullptr;
	QCheckBox* m_hasOperatingCodeStepCheck = nullptr;
	QLineEdit* m_operatingCodeStepEdit = nullptr;
	QVBoxLayout* m_detailLayout = nullptr;
	QTabWidget* m_pages = nullptr;
	QTableWidget* m_occurrenceTable = nullptr;
	QPushButton* m_selectAllButton = nullptr;
	QPushButton* m_selectNoneButton = nullptr;
	QLabel* m_occurrenceSelectionLabel = nullptr;
};

#endif // SERVICEPANEL_H
