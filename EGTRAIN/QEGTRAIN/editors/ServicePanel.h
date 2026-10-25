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

/// The services of the scene: a list to pick one and three tabs. The first holds the fields of the selected service,
/// the second the place for its timetable, and the third lists the occurrences the services generate and lets the
/// user leave some out of the next run. Renaming a service rewrites the entities that refer to it, and deleting one
/// that is referred to is refused. The entry time and the headway are whole seconds, the entry time counted from the
/// base time; the performance is in percent and the maximum speed in km/h.
class ServicePanel : public SceneEditorPanel {
	Q_OBJECT

public:
	/// Makes the widgets, empty until a loaded document is set.
	explicit ServicePanel(QWidget* parent = nullptr);

	/// Refreshes when the document is replaced.
	void setDocument(SceneDocument* document) override;
	/// The messages and questions go through these prompts. The panel uses its own until it is given others; null
	/// brings its own back. The prompts must outlive the panel.
	void setPrompts(UserPrompts* prompts);
	/// The occurrences that the next run leaves out are kept here. The panel uses its own until it is given another;
	/// null brings its own back. The selection must outlive the panel.
	void setRunSelection(RunSelection* selection);
	/// Gives the time in seconds over which the occurrences of a service are counted. Without one the duration of the
	/// scene settings is used.
	void setOccurrenceDuration(std::function<double()> duration);
	/// Puts the timetable of the selected service on the Timetable tab. The widget becomes a child of the panel.
	void setTimetableWidget(QWidget* widget);
	/// Lists the services again and shows the selected one.
	void refresh() override;
	/// Takes the id and the fields that are being edited into the document.
	void commitPendingEditors() override;
	/// Shows the fields of the selected service again.
	void updateDetail();
	/// Lists the occurrences again.
	void refreshOccurrencePreview();

	/// Appends a service with a new unique id and default values, and selects it.
	void addService();
	/// Inserts a copy of the selected service, with a new unique id, after it.
	void duplicateService();
	/// Deletes the selected service after the user has confirmed it; refused while something refers to it.
	void deleteService();
	/// Takes the id from the field. An empty or used id is dropped and the field shows the old id again.
	void commitIdEdit();
	/// Takes the service code (the operating code) from its field, trimmed.
	void commitOperatingCode();
	/// Takes the category at `index` of its combo; the empty category is none.
	void commitCategory(int index);
	/// Takes the display colour, "#rrggbb" or empty for the default.
	void commitVisualizationColor(const QString& text);
	/// Takes the composition id.
	void commitComposition(const QString& text);
	/// Takes the route at `index` of the route combo.
	void commitRoute(int index);
	/// Sets whether the service has an entry time.
	void commitHasEntryTime(bool checked);
	/// Takes the entry time from its field, in whole seconds from the base time; text that is no number counts as 0.
	void commitEntryTimeSeconds();
	/// Sets whether the service repeats. A service that stops repeating loses its repeat count and code step.
	void commitHasRepeat(bool checked);
	/// Takes the headway between occurrences from its field, in whole seconds; text that is no number counts as 0.
	void commitHeadwaySeconds();
	/// Sets whether the number of occurrences is given; it is at least 1.
	void commitHasRepeatCount(bool checked);
	/// Takes the number of occurrences, including the first, from its field; text that is no number counts as 1.
	void commitRepeatCount();
	/// Takes the running performance in percent, held between 1 and 100.
	void commitPerformancePercent(double value);
	/// Sets whether the service has a maximum speed; one that has none gets 100 km/h.
	void commitHasMaximumSpeed(bool checked);
	/// Takes the maximum speed in km/h from its field.
	void commitMaximumSpeed();
	/// Sets whether the service code grows by a step with each occurrence; a step of 0 becomes 1.
	void commitHasOperatingCodeStep(bool checked);
	/// Takes the step of the service code from its field; text that is no number, and 0, become 1.
	void commitOperatingCodeStep();
	/// Includes every occurrence in the next run.
	void selectAllOccurrences();
	/// Leaves every occurrence out of the next run. Nothing happens when there are more occurrences than the table lists.
	void selectNoneOccurrences();

	/// What a generated service is called in lists: its operating code, or the service id when it has none, its route and
	/// its scheduled entry in seconds from the base time, such as "1723 (route r1, +600 s)". `occurrence` counts from 1.
	static QString generatedServiceLabel(const SceneService& service, int occurrence);
	/// The summary above the occurrence table, such as "3 of 5 selected, 2 in the run period". The run period part is left
	/// out when every selected occurrence is in the period. When the table shows only the first `shown` of the `total`
	/// occurrences, a second line says so.
	static QString occurrenceSummaryText(int total, int selected, int selectedInPeriod, int shown);

	/// The services, by id.
	QListWidget* listWidget() const { return m_listWidget; }
	/// The field of the id of the selected service.
	QLineEdit* idEdit() const { return m_idEdit; }
	/// The field of the service code (the operating code).
	QLineEdit* operatingCodeEdit() const { return m_operatingCodeEdit; }
	/// The categories, with the category text as the data and "No category" for none.
	QComboBox* categoryCombo() const { return m_categoryCombo; }
	/// The display colour of the service.
	ColorChoiceButton* colorButton() const { return m_colorButton; }
	/// The compositions of the scene, by id. A composition that the scene lacks is still shown.
	QComboBox* compositionCombo() const { return m_compositionCombo; }
	/// The routes of the scene, with the route id as the data.
	QComboBox* routeCombo() const { return m_routeCombo; }
	/// Whether the service has an entry time.
	QCheckBox* hasEntryTimeCheck() const { return m_hasEntryTimeCheck; }
	/// The entry time in seconds from the base time.
	QDoubleSpinBox* entryTimeSecondsEdit() const { return m_entryTimeSecondsEdit; }
	/// Whether the service repeats.
	QCheckBox* hasRepeatCheck() const { return m_hasRepeatCheck; }
	/// The headway between occurrences in seconds.
	QDoubleSpinBox* headwaySecondsEdit() const { return m_headwaySecondsEdit; }
	/// Whether the number of occurrences is given.
	QCheckBox* hasRepeatCountCheck() const { return m_hasRepeatCountCheck; }
	/// The number of occurrences, including the first.
	QLineEdit* repeatCountEdit() const { return m_repeatCountEdit; }
	/// The running performance in percent, 1 to 100.
	QDoubleSpinBox* performancePercentEdit() const { return m_performancePercentEdit; }
	/// Whether the service has a maximum speed.
	QCheckBox* hasMaximumSpeedCheck() const { return m_hasMaximumSpeedCheck; }
	/// The maximum speed of the service in km/h, 0.1 to 1000.
	QDoubleSpinBox* maximumSpeedKmhEdit() const { return m_maximumSpeedKmhEdit; }
	/// Whether the service code grows by a step with each occurrence.
	QCheckBox* hasOperatingCodeStepCheck() const { return m_hasOperatingCodeStepCheck; }
	/// The step of the service code, a whole number other than 0.
	QLineEdit* operatingCodeStepEdit() const { return m_operatingCodeStepEdit; }
	/// The button of addService().
	QPushButton* addButton() const { return m_addButton; }
	/// The button of duplicateService().
	QPushButton* duplicateButton() const { return m_duplicateButton; }
	/// The button of deleteService().
	QPushButton* deleteButton() const { return m_deleteButton; }
	/// The Service, Timetable and Run occurrences tabs.
	QTabWidget* pages() const { return m_pages; }
	/// The occurrences of all services, one row each, with a check box that includes the occurrence in the next run.
	QTableWidget* occurrenceTable() const { return m_occurrenceTable; }
	/// The button of selectAllOccurrences().
	QPushButton* selectAllButton() const { return m_selectAllButton; }
	/// The button of selectNoneOccurrences().
	QPushButton* selectNoneButton() const { return m_selectNoneButton; }
	/// The counts of the occurrences and of those that the next run includes.
	QLabel* occurrenceSelectionLabel() const { return m_occurrenceSelectionLabel; }
	/// The message above the fields when no service can be shown, hidden otherwise.
	QLabel* emptyStateLabel() const { return m_emptyLabel; }

Q_SIGNALS:
	/// The service has got another id and the entities that referred to it have followed. Emitted after the document
	/// is marked changed.
	void serviceRenamed(const QString& oldId, const QString& newId);
	/// The occurrences are listed again, so what is built from them, the entrance delays and the targets of the
	/// incidents, is out of date.
	void occurrencesChanged();
	/// The timetable of this service, or of none when the id is empty, has to be shown again: another service is
	/// selected, or the id or the route of the selected one has changed.
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
	QDoubleSpinBox* m_entryTimeSecondsEdit = nullptr;
	QCheckBox* m_hasRepeatCheck = nullptr;
	QDoubleSpinBox* m_headwaySecondsEdit = nullptr;
	QCheckBox* m_hasRepeatCountCheck = nullptr;
	QLineEdit* m_repeatCountEdit = nullptr;
	QDoubleSpinBox* m_performancePercentEdit = nullptr;
	QCheckBox* m_hasMaximumSpeedCheck = nullptr;
	QDoubleSpinBox* m_maximumSpeedKmhEdit = nullptr;
	QCheckBox* m_hasOperatingCodeStepCheck = nullptr;
	QLineEdit* m_operatingCodeStepEdit = nullptr;
	QLabel* m_emptyLabel = nullptr;
	QVBoxLayout* m_timetableLayout = nullptr;
	QTabWidget* m_pages = nullptr;
	QTableWidget* m_occurrenceTable = nullptr;
	QPushButton* m_selectAllButton = nullptr;
	QPushButton* m_selectNoneButton = nullptr;
	QLabel* m_occurrenceSelectionLabel = nullptr;
};

#endif // SERVICEPANEL_H
