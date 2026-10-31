#ifndef SCENARIOPANEL_H
#define SCENARIOPANEL_H

#include "editors/SceneEditorPanel.h"
#include "scene/SceneDiagnostic.h"

#include <QString>

#include <functional>
#include <vector>

class QLineEdit;
class QListWidget;
class QPushButton;

/// The scenarios of the scene: a list to pick one, the buttons that add, copy, delete, import and export them, and the
/// id, name and description of the selected one. The selected scenario is kept in the run selection, because the next
/// run uses it and the incident and entrance delay editors show its entries. Renaming a scenario rewrites what refers
/// to it, and the default scenario cannot be deleted.
class ScenarioPanel : public SceneEditorPanel {
	Q_OBJECT

public:
	/// Called when a scenario file cannot be written, with what the writer reports.
	using ExportFailureReport = std::function<void(const std::vector<SceneDiagnostic>&)>;

	/// Makes the widgets, empty until a loaded document is set.
	explicit ScenarioPanel(QWidget* parent = nullptr);

	/// The validation messages that the list shows as the state of each scenario. The vector must outlive the panel or be
	/// replaced first; null shows none. Call updatePresentation() after the diagnostics have changed.
	void setDiagnostics(const std::vector<SceneDiagnostic>* diagnostics);
	/// Tells whether the list gives the details of each scenario. Without it the list is short.
	void setAdvancedDetails(std::function<bool()> enabled);
	/// Gives the file that Import reads; an empty path cancels. Without a chooser the panel asks with a file dialog.
	void setImportFileChooser(std::function<QString()> chooser);
	/// Gives the file that Export writes, for which it gets the name that the dialog suggests; an empty path cancels.
	/// Without a chooser the panel asks with a file dialog.
	void setExportFileChooser(std::function<QString(const QString& suggestedName)> chooser);
	/// Called when an export starts, before the user names the file. The report it returns is called when the file
	/// cannot be written.
	void setExportObserver(std::function<ExportFailureReport()> begin);
	/// Lists the scenarios again and shows the selected one.
	void refresh() override;
	/// Takes the id, the name and the description that are being edited into the document.
	void commitPendingEditors() override;
	/// Writes the label of each scenario in the list again.
	void updatePresentation();
	/// Shows the fields of the selected scenario again.
	void updateDetail();
	/// One line that names the selected scenario, for a header over the editors that work on its entries, such as
	/// "Scenario: name", "Scenario: name (id)" when another scenario has the same name, or "No scenario selected".
	QString headerText() const;

	/// Selects the scenario at this position of the scene; the list follows when it is refreshed.
	void selectScenario(int row);
	/// Appends a scenario "New scenario" without incidents or delays, with a new unique id, and selects it.
	void addBlankScenario();
	/// Appends a copy of the selected scenario, named "Copy of" its name, with new unique ids for it and its incidents,
	/// and selects it.
	void duplicateScenario();
	/// Deletes the selected scenario after the user has confirmed it and selects the default one. The default scenario
	/// cannot be deleted, nor any while the scene has no valid default.
	void deleteScenario();
	/// Reads a scenario JSON file and appends it; an id that the scene already has, of the scenario or of an incident,
	/// is made unique.
	void importScenario();
	/// Writes the selected scenario to a scenario JSON file.
	void exportScenario();
	/// Takes the id from the field. An empty or used id is dropped and the field shows the old id again.
	void commitIdEdit();
	/// Takes the name from its field.
	void commitNameEdit();
	/// Takes the description from its field.
	void commitDescriptionEdit();

	/// The scenarios of the scene, one row each.
	QListWidget* listWidget() const { return m_listWidget; }
	/// The field of the id of the selected scenario.
	QLineEdit* idEdit() const { return m_idEdit; }
	/// The field of the name of the selected scenario.
	QLineEdit* nameEdit() const { return m_nameEdit; }
	/// The field of the description of the selected scenario.
	QLineEdit* descriptionEdit() const { return m_descriptionEdit; }
	/// The button of addBlankScenario().
	QPushButton* blankButton() const { return m_blankButton; }
	/// The button of duplicateScenario().
	QPushButton* duplicateButton() const { return m_duplicateButton; }
	/// The button of deleteScenario().
	QPushButton* deleteButton() const { return m_deleteButton; }
	/// The button of importScenario().
	QPushButton* importButton() const { return m_importButton; }
	/// The button of exportScenario().
	QPushButton* exportButton() const { return m_exportButton; }

Q_SIGNALS:
	/// Another scenario is selected, by the user or because an edit has added or removed one. Its incidents and entrance
	/// delays are to be shown, and what a run showed no longer applies to the selection. Emitted after the run selection
	/// is updated and, for an edit, after the document is marked changed.
	void scenarioSelected(const QString& id);
	/// The selected scenario or its id or name has changed, or the document is replaced; the argument is headerText().
	void headerTextChanged(const QString& text);
	/// A message for the status bar, shown for `timeoutMs` milliseconds.
	void statusMessage(const QString& text, int timeoutMs);

protected:
	/// Writes the labels of the list again when scenarios are edited.
	void documentChanged(SceneDocument::ChangeKinds kinds) override;

private:
	// The selected scenario. The second form is null unless the scene can be edited.
	const SceneScenario* selectedScenario() const;
	const SceneScenario* editableScenario() const;
	// The scenario of the latest run, or else the selected one, as the run results name it.
	QString scenarioContext() const;
	// Applies `mutate` to the selected scenario in one edit of the document and records that the scenario is edited. A scene
	// without a scenario gets the baseline scenario first. False when the document refuses the edit.
	bool editSelected(const QString& label, const std::function<void(SceneScenario&)>& mutate);
	// Adds `scenario` in one edit of the document, selects it, records that it is edited, and tells those who show the entries
	// of the selected scenario. False when the document refuses the edit.
	bool addScenario(const QString& label, SceneScenario scenario);

	const std::vector<SceneDiagnostic>* m_diagnostics = nullptr;
	std::function<bool()> m_advancedDetails;
	std::function<QString()> m_importFileChooser;
	std::function<QString(const QString&)> m_exportFileChooser;
	std::function<ExportFailureReport()> m_exportObserver;

	QListWidget* m_listWidget = nullptr;
	QPushButton* m_blankButton = nullptr;
	QPushButton* m_duplicateButton = nullptr;
	QPushButton* m_deleteButton = nullptr;
	QPushButton* m_importButton = nullptr;
	QPushButton* m_exportButton = nullptr;
	QLineEdit* m_idEdit = nullptr;
	QLineEdit* m_nameEdit = nullptr;
	QLineEdit* m_descriptionEdit = nullptr;
};

#endif // SCENARIOPANEL_H
