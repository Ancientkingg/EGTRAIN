#include "editors/ScenarioPanel.h"

#include "app/DialogFolders.h"
#include "editors/CountText.h"
#include "editors/EditorHelpers.h"
#include "editors/EditorRules.h"
#include "scene/SceneIds.h"
#include "scene/SceneReferences.h"
#include "scene/SceneWriter.h"

#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

ScenarioPanel::ScenarioPanel(QWidget* parent) : SceneEditorPanel(parent) {
	setObjectName("scenarioPanel");
	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel("Scenarios", this));
	m_listWidget = new QListWidget(this);
	m_listWidget->setObjectName("scenarioListWidget");
	m_listWidget->setAccessibleName("Scenario library");
	m_listWidget->setTextElideMode(Qt::ElideRight);
	m_listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	layout->addWidget(m_listWidget);
	QGridLayout* buttonLayout = new QGridLayout();
	m_blankButton = new QPushButton("New", this);
	m_blankButton->setObjectName("blankScenarioButton");
	m_duplicateButton = new QPushButton("Duplicate", this);
	m_duplicateButton->setObjectName("duplicateScenarioButton");
	m_deleteButton = new QPushButton("Delete", this);
	m_deleteButton->setObjectName("deleteScenarioButton");
	m_importButton = new QPushButton("Import...", this);
	m_importButton->setObjectName("importScenarioButton");
	m_exportButton = new QPushButton("Export...", this);
	m_exportButton->setObjectName("exportScenarioButton");
	const QList<QPushButton*> buttons{m_blankButton, m_duplicateButton, m_deleteButton, m_importButton, m_exportButton};
	for (int index = 0; index < buttons.size(); ++index)
		buttonLayout->addWidget(buttons[index], index / 2, index % 2);
	layout->addLayout(buttonLayout);
	QFormLayout* detailLayout = new QFormLayout();
	m_idEdit = new QLineEdit(this);
	m_idEdit->setObjectName("scenarioIdEdit");
	m_nameEdit = new QLineEdit(this);
	m_nameEdit->setObjectName("scenarioNameEdit");
	m_descriptionEdit = new QLineEdit(this);
	m_descriptionEdit->setObjectName("scenarioDescriptionEdit");
	detailLayout->addRow("Scenario ID", m_idEdit);
	detailLayout->addRow("Name", m_nameEdit);
	detailLayout->addRow("Description", m_descriptionEdit);
	layout->addLayout(detailLayout);

	connect(m_listWidget, &QListWidget::currentRowChanged, this, &ScenarioPanel::selectScenario);
	connect(m_idEdit, &QLineEdit::editingFinished, this, &ScenarioPanel::commitIdEdit);
	connect(m_nameEdit, &QLineEdit::editingFinished, this, &ScenarioPanel::commitNameEdit);
	connect(m_descriptionEdit, &QLineEdit::editingFinished, this, &ScenarioPanel::commitDescriptionEdit);
	connect(m_blankButton, &QPushButton::clicked, this, &ScenarioPanel::addBlankScenario);
	connect(m_duplicateButton, &QPushButton::clicked, this, &ScenarioPanel::duplicateScenario);
	connect(m_deleteButton, &QPushButton::clicked, this, &ScenarioPanel::deleteScenario);
	connect(m_importButton, &QPushButton::clicked, this, &ScenarioPanel::importScenario);
	connect(m_exportButton, &QPushButton::clicked, this, &ScenarioPanel::exportScenario);
	ScenarioPanel::refresh();
}

void ScenarioPanel::documentChanged(SceneDocument::ChangeKinds kinds) {
	if (kinds.testFlag(SceneDocument::Infrastructure))
		refresh();
	else if (kinds.testFlag(SceneDocument::Scenarios))
		updatePresentation();
}

void ScenarioPanel::setDiagnostics(const std::vector<SceneDiagnostic>* diagnostics) {
	m_diagnostics = diagnostics;
}

void ScenarioPanel::setAdvancedDetails(std::function<bool()> enabled) {
	m_advancedDetails = std::move(enabled);
}

void ScenarioPanel::setImportFileChooser(std::function<QString()> chooser) {
	m_importFileChooser = std::move(chooser);
}

void ScenarioPanel::setExportFileChooser(std::function<QString(const QString& suggestedName)> chooser) {
	m_exportFileChooser = std::move(chooser);
}

void ScenarioPanel::setExportObserver(std::function<ExportFailureReport()> begin) {
	m_exportObserver = std::move(begin);
}

const SceneScenario* ScenarioPanel::selectedScenario() const {
	return hasScene() ? m_runSelection->selectedScenario(m_document->model()) : nullptr;
}

const SceneScenario* ScenarioPanel::editableScenario() const {
	return canEdit() ? selectedScenario() : nullptr;
}

QString ScenarioPanel::scenarioContext() const {
	if (!m_runSelection->appliedScenarioId().empty())
		return QString::fromStdString(m_runSelection->appliedScenarioId());
	const SceneScenario* scenario = selectedScenario();
	return scenario ? QString::fromStdString(scenario->id) : QStringLiteral("(none)");
}

bool ScenarioPanel::editSelected(const QString& label, const std::function<void(SceneScenario&)>& mutate) {
	return m_document->edit(SceneDocument::Scenarios, label, [this, &mutate](SceneModel& model) {
		SceneScenario* scenario = m_runSelection->selectedScenario(model);
		mutate(*scenario);
		m_runSelection->markScenarioModified(scenario->id);
	});
}

bool ScenarioPanel::addScenario(const QString& label, SceneScenario scenario) {
	const std::string id = scenario.id;
	if (!m_document->edit(SceneDocument::Scenarios, label, [this, &id, &scenario](SceneModel& model) {
			model.scenarios.push_back(std::move(scenario));
			m_runSelection->selectScenario(id);
			m_runSelection->markScenarioModified(id);
		}))
		return false;
	refresh();
	emit scenarioSelected(QString::fromStdString(id));
	emit changed();
	return true;
}

void ScenarioPanel::refresh() {
	const bool loaded = hasScene();
	if (loaded && m_runSelection->scenarioId().empty()) {
		if (const SceneScenario* scenario = defaultScenario(m_document->model()))
			m_runSelection->selectScenario(scenario->id);
	}

	{
		const QSignalBlocker blocker(m_listWidget);
		m_listWidget->clear();
		int rowToSelect = -1;
		if (loaded) {
			const auto& scenarios = m_document->model().scenarios;
			for (std::size_t index = 0; index < scenarios.size(); ++index) {
				new QListWidgetItem(m_listWidget);
				if (scenarios[index].id == m_runSelection->scenarioId())
					rowToSelect = static_cast<int>(index);
			}
		}
		updatePresentation();
		m_listWidget->setCurrentRow(rowToSelect);
		m_listWidget->setEnabled(canEdit());
	}

	updateDetail();
}

void ScenarioPanel::updatePresentation() {
	if (!hasScene())
		return;
	const SceneModel& model = m_document->model();
	const bool details = m_advancedDetails && m_advancedDetails();
	const auto validationStatus = [this](std::size_t index) {
		const std::string prefix = "scenarios[" + std::to_string(index) + "]";
		int errors = 0;
		int warnings = 0;
		if (m_diagnostics) {
			for (const auto& diagnostic : *m_diagnostics) {
				if (diagnostic.path.rfind(prefix, 0) != 0)
					continue;
				if (diagnostic.severity == SceneSeverity::Error)
					++errors;
				else if (diagnostic.severity == SceneSeverity::Warning)
					++warnings;
			}
		}
		return errors > 0 ? QStringLiteral("has errors") : (warnings > 0 ? QStringLiteral("has warnings") : QString());
	};
	for (std::size_t index = 0; index < model.scenarios.size() && index < static_cast<std::size_t>(m_listWidget->count()); ++index) {
		const SceneScenario& scenario = model.scenarios[index];
		// The name, or the id of a scenario without one, and its markers; below it the counts that are not zero and, when the
		// scenario has errors, that it has. Advanced details also say when it has warnings.
		std::vector<std::string> markers;
		if (scenario.id == model.defaultScenarioId)
			markers.emplace_back("default");
		if (m_runSelection->isScenarioModified(scenario.id))
			markers.emplace_back("modified");
		QString label = QString::fromStdString(sceneScenarioLabel(model, scenario, markers));
		QStringList facts;
		if (!scenario.incidents.empty())
			facts << countText(static_cast<long long>(scenario.incidents.size()), QStringLiteral("incident"), QStringLiteral("incidents"));
		if (!scenario.entranceDelays.empty())
			facts << countText(static_cast<long long>(scenario.entranceDelays.size()), QStringLiteral("delay"), QStringLiteral("delays"));
		const QString status = validationStatus(index);
		if (status == QStringLiteral("has errors") || (details && !status.isEmpty()))
			facts << status;
		if (!facts.isEmpty())
			label += QLatin1Char('\n') + facts.join(QStringLiteral(", "));
		QString tooltip = QStringLiteral("ID: %1").arg(QString::fromStdString(scenario.id));
		if (!scenario.description.empty())
			tooltip += QLatin1Char('\n') + QString::fromStdString(scenario.description);
		if (auto* item = m_listWidget->item(static_cast<int>(index))) {
			item->setText(label);
			item->setToolTip(tooltip);
		}
	}
	emit headerTextChanged(headerText());
}

void ScenarioPanel::updateDetail() {
	const SceneScenario* scenario = selectedScenario();
	const bool editable = canEdit();
	const bool enabled = editable && scenario != nullptr;
	{
		const QSignalBlocker blocker(m_idEdit);
		m_idEdit->setText(enabled ? QString::fromStdString(scenario->id) : QString());
		m_idEdit->setEnabled(enabled);
	}
	{
		const QSignalBlocker blocker(m_nameEdit);
		m_nameEdit->setText(enabled ? QString::fromStdString(scenario->name) : QString());
		m_nameEdit->setEnabled(enabled);
	}
	{
		const QSignalBlocker blocker(m_descriptionEdit);
		m_descriptionEdit->setText(enabled ? QString::fromStdString(scenario->description) : QString());
		m_descriptionEdit->setEnabled(enabled);
	}
	m_blankButton->setEnabled(editable);
	m_duplicateButton->setEnabled(enabled);
	bool deletable = false;
	if (enabled) {
		const SceneModel& model = m_document->model();
		const bool hasPersistedDefault = !model.defaultScenarioId.empty()
			&& std::any_of(model.scenarios.begin(), model.scenarios.end(),
				[&model](const SceneScenario& candidate) { return candidate.id == model.defaultScenarioId; });
		deletable = hasPersistedDefault && scenario->id != model.defaultScenarioId;
	}
	m_deleteButton->setEnabled(deletable);
	m_importButton->setEnabled(editable);
	m_exportButton->setEnabled(enabled);
	emit headerTextChanged(headerText());
}

QString ScenarioPanel::headerText() const {
	const SceneScenario* scenario = selectedScenario();
	if (!scenario)
		return QStringLiteral("No scenario selected");
	return QStringLiteral("Scenario: %1").arg(QString::fromStdString(sceneScenarioLabel(m_document->model(), *scenario)));
}

void ScenarioPanel::selectScenario(int row) {
	if (!canEdit() || row < 0 || row >= static_cast<int>(m_document->model().scenarios.size()))
		return;
	const std::string id = m_document->model().scenarios[static_cast<std::size_t>(row)].id;
	if (id == m_runSelection->scenarioId()) {
		updateDetail();
		return;
	}
	m_runSelection->selectScenario(id);
	updateDetail();
	emit scenarioSelected(QString::fromStdString(id));
}

void ScenarioPanel::addBlankScenario() {
	if (!canEdit())
		return;
	SceneScenario scenario;
	scenario.id = sceneUniqueScenarioId(m_document->model(), "new_scenario");
	scenario.name = "New scenario";
	addScenario("Add scenario", std::move(scenario));
}

void ScenarioPanel::duplicateScenario() {
	const SceneScenario* selected = editableScenario();
	if (!selected)
		return;
	const SceneModel& model = m_document->model();
	SceneScenario copy = *selected;
	copy.id = sceneUniqueScenarioId(model, copy.id + "_copy");
	copy.name = "Copy of " + copy.name;
	for (auto& incident : copy.incidents)
		incident.id = sceneUniqueIncidentId(model, incident.id + "_copy");
	addScenario("Duplicate scenario", std::move(copy));
}

void ScenarioPanel::deleteScenario() {
	if (!canEdit())
		return;
	const SceneModel& model = m_document->model();
	const SceneScenario* selected = editableScenario();
	const auto persistedDefault = std::find_if(model.scenarios.begin(), model.scenarios.end(),
		[&model](const SceneScenario& scenario) { return scenario.id == model.defaultScenarioId; });
	if (!selected || model.defaultScenarioId.empty() || persistedDefault == model.scenarios.end()) {
		m_prompts->showError(this, "Cannot Delete Scenario",
			"This case has no default scenario. Fix the validation errors first.", true);
		return;
	}
	const std::string defaultId = model.defaultScenarioId;
	if (selected->id == defaultId) {
		m_prompts->showError(this, "Cannot Delete Scenario", "The default scenario cannot be deleted.", true);
		updateDetail();
		return;
	}

	const std::string deletedId = selected->id;
	if (!m_prompts->confirm(this, "Delete Scenario", QString("Delete scenario '%1'?").arg(QString::fromStdString(deletedId))))
		return;

	const auto it = std::find_if(model.scenarios.begin(), model.scenarios.end(),
		[&deletedId](const SceneScenario& scenario) { return scenario.id == deletedId; });
	if (it == model.scenarios.end())
		return;
	const auto row = std::distance(model.scenarios.begin(), it);
	if (!m_document->edit(SceneDocument::Scenarios, "Delete scenario", [this, row, &deletedId, &defaultId](SceneModel& edited) {
			edited.scenarios.erase(edited.scenarios.begin() + row);
			m_runSelection->scenarioDeleted(deletedId, defaultId);
		}))
		return;
	refresh();
	emit scenarioSelected(QString::fromStdString(defaultId));
	emit changed();
}

void ScenarioPanel::exportScenario() {
	const ExportFailureReport reportFailure = m_exportObserver ? m_exportObserver() : ExportFailureReport();
	const SceneScenario* scenario = selectedScenario();
	if (!scenario)
		return;
	const QString suggestedName = scenarioContext() + ".json";
	QString path = m_exportFileChooser
		? m_exportFileChooser(suggestedName)
		: QFileDialog::getSaveFileName(this, "Export Scenario", dialog_folders::suggestedPath(suggestedName), "Scenario JSON (*.json)");
	if (path.isEmpty())
		return;
	if (!path.endsWith(".json", Qt::CaseInsensitive))
		path += ".json";
	const SceneSaveResult result = saveScenarioJson(*scenario, path.toStdString());
	if (!result.success()) {
		if (reportFailure)
			reportFailure(result.diagnostics);
		m_prompts->showError(this, "Scenario Export Failed", firstDiagnosticDialogText(result.diagnostics), true);
		return;
	}
	emit statusMessage(QString("Exported scenario %1").arg(scenarioContext()), 4000);
}

void ScenarioPanel::importScenario() {
	if (!canEdit())
		return;
	const QString path = m_importFileChooser
		? m_importFileChooser()
		: QFileDialog::getOpenFileName(this, "Import Scenario", QDir::homePath(), "Scenario JSON (*.json)");
	if (path.isEmpty())
		return;
	const ScenarioLoadResult loaded = loadScenarioJson(path.toStdString());
	if (!loaded.success()) {
		m_prompts->showError(this, "Scenario Import Failed", firstDiagnosticDialogText(loaded.diagnostics), true);
		return;
	}
	ScenarioImport prepared = prepareScenarioImport(m_document->model(), loaded.scenario);
	// One error gives its message and its fix, several give their messages, one per line.
	if (!prepared.errors.empty()) {
		QStringList messages;
		for (const auto& error : prepared.errors)
			messages << QString::fromStdString(error.message);
		m_prompts->showError(this, "Scenario Import Failed",
			prepared.errors.size() == 1 ? QString::fromStdString(toDialogText(prepared.errors.front())) : messages.join("\n"), true);
		return;
	}
	if (!addScenario("Import scenario", std::move(prepared.scenario)))
		return;
	const QString importedId = QString::fromStdString(m_runSelection->scenarioId());
	QString message = QString("Imported scenario %1.").arg(importedId);
	if (prepared.idTaken)
		message += QString(" %1 was taken, so it is now %2.").arg(QString::fromStdString(prepared.requestedId), importedId);
	if (prepared.renamedIncidents > 0)
		message += prepared.renamedIncidents == 1 ? QStringLiteral(" 1 incident ID was renamed to stay unique.")
												  : QString(" %1 incident IDs were renamed to stay unique.").arg(prepared.renamedIncidents);
	emit statusMessage(message, 7000);
	if (prepared.idTaken || prepared.renamedIncidents > 0)
		m_prompts->showInfo(this, "Scenario ID Adjusted", message);
}

void ScenarioPanel::commitIdEdit() {
	const SceneScenario* scenario = editableScenario();
	if (!scenario)
		return;
	const std::string newId = m_idEdit->text().trimmed().toStdString();
	if (newId == scenario->id)
		return;
	if (newId.empty()) {
		m_prompts->showError(this, "Invalid Scenario ID", "Scenario IDs must not be empty.", true);
		updateDetail();
		return;
	}
	if (sceneUniqueScenarioId(m_document->model(), newId) != newId) {
		m_prompts->showError(this, "Duplicate Scenario ID", "Choose a unique scenario ID.", true);
		updateDetail();
		return;
	}
	const std::string oldId = scenario->id;
	if (!m_document->edit(SceneDocument::Scenarios, "Rename scenario", [this, &oldId, &newId](SceneModel& model) {
			m_runSelection->selectedScenario(model)->id = newId;
			sceneRewriteScenarioReferences(model, oldId, newId);
			m_runSelection->scenarioRenamed(oldId, newId);
			m_runSelection->markScenarioModified(newId);
		}))
		return;
	emit changed();
}

void ScenarioPanel::commitNameEdit() {
	const SceneScenario* scenario = editableScenario();
	if (!scenario)
		return;
	const std::string value = m_nameEdit->text().toStdString();
	if (value == scenario->name)
		return;
	if (!editSelected("Change scenario name", [&value](SceneScenario& edited) { edited.name = value; }))
		return;
	emit changed();
}

void ScenarioPanel::commitDescriptionEdit() {
	const SceneScenario* scenario = editableScenario();
	if (!scenario)
		return;
	const std::string value = m_descriptionEdit->text().toStdString();
	if (value == scenario->description)
		return;
	if (!editSelected("Change scenario description", [&value](SceneScenario& edited) { edited.description = value; }))
		return;
	emit changed();
}

void ScenarioPanel::commitPendingEditors() {
	if (!canEdit())
		return;
	commitIdEdit();
	commitNameEdit();
	commitDescriptionEdit();
}
