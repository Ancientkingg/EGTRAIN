#include "editors/ScenarioPanel.h"

#include "app/DialogFolders.h"
#include "scene/SceneIds.h"
#include "scene/SceneReferences.h"
#include "scene/SceneValidator.h"
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

namespace {

QString firstDiagnosticMessage(const std::vector<SceneDiagnostic>& diagnostics) {
	for (const auto& diagnostic : diagnostics) {
		if (diagnostic.severity == SceneSeverity::Error)
			return QString::fromStdString(diagnostic.message);
	}
	if (!diagnostics.empty())
		return QString::fromStdString(diagnostics.front().message);
	return QString();
}

} // namespace

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
	m_blankButton = new QPushButton("Blank", this);
	m_blankButton->setObjectName("blankScenarioButton");
	m_duplicateButton = new QPushButton("Duplicate", this);
	m_duplicateButton->setObjectName("duplicateScenarioButton");
	m_deleteButton = new QPushButton("Delete", this);
	m_deleteButton->setObjectName("deleteScenarioButton");
	m_importButton = new QPushButton("Import JSON...", this);
	m_importButton->setObjectName("importScenarioButton");
	m_exportButton = new QPushButton("Export JSON...", this);
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
	refresh();
}

void ScenarioPanel::setDocument(SceneDocument* document) {
	QObject::disconnect(m_replacedConnection);
	QObject::disconnect(m_changedConnection);
	m_document = document;
	if (m_document) {
		m_replacedConnection = connect(m_document, &SceneDocument::replaced, this, &ScenarioPanel::refresh);
		m_changedConnection = connect(m_document, &SceneDocument::changed, this, [this](SceneDocument::ChangeKinds kinds) {
			if (kinds.testFlag(SceneDocument::Scenarios))
				updatePresentation();
		});
	}
	refresh();
}

void ScenarioPanel::setPrompts(UserPrompts* prompts) {
	m_prompts = prompts ? prompts : &m_ownPrompts;
}

void ScenarioPanel::setRunSelection(RunSelection* selection) {
	m_runSelection = selection ? selection : &m_ownRunSelection;
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

bool ScenarioPanel::hasScene() const {
	return m_document && m_document->isLoaded();
}

bool ScenarioPanel::canEdit() const {
	return hasScene() && m_document->isEditable();
}

const SceneScenario* ScenarioPanel::selectedScenario() const {
	return hasScene() ? m_runSelection->selectedScenario(m_document->model()) : nullptr;
}

SceneScenario* ScenarioPanel::selectedScenarioForEdit() {
	return hasScene() ? m_runSelection->selectedScenario(m_document->mutableModel()) : nullptr;
}

QString ScenarioPanel::scenarioContext() const {
	if (!m_runSelection->appliedScenarioId().empty())
		return QString::fromStdString(m_runSelection->appliedScenarioId());
	const SceneScenario* scenario = selectedScenario();
	return scenario ? QString::fromStdString(scenario->id) : QStringLiteral("(none)");
}

void ScenarioPanel::markEdited(const QString& label) {
	if (const SceneScenario* scenario = selectedScenario())
		m_runSelection->markScenarioModified(scenario->id);
	m_document->markChanged(SceneDocument::Scenarios, label);
}

void ScenarioPanel::finishAddition(const QString& label) {
	markEdited(label);
	refresh();
	emit scenarioSelected(QString::fromStdString(m_runSelection->scenarioId()));
	emit changed();
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
		return errors > 0 ? QStringLiteral("Invalid")
						  : (warnings > 0 ? QStringLiteral("Warning") : QStringLiteral("Ready"));
	};
	for (std::size_t index = 0; index < model.scenarios.size() && index < static_cast<std::size_t>(m_listWidget->count()); ++index) {
		const SceneScenario& scenario = model.scenarios[index];
		const QString description = scenario.description.empty()
			? QStringLiteral("(no description)")
			: QString::fromStdString(scenario.description);
		QString label = QStringLiteral("%1 \u2014 %2").arg(QString::fromStdString(scenario.id), QString::fromStdString(scenario.name));
		if (details) {
			label += QString(" | %1 | %2 incident(s) | %3 delay(s) | %4")
						 .arg(description)
						 .arg(static_cast<int>(scenario.incidents.size()))
						 .arg(static_cast<int>(scenario.entranceDelays.size()))
						 .arg(validationStatus(index));
		} else {
			label += QString(" | %1 incident(s) | %2 delay(s)")
						 .arg(static_cast<int>(scenario.incidents.size()))
						 .arg(static_cast<int>(scenario.entranceDelays.size()));
			if (!scenario.description.empty())
				label += QString(" | %1").arg(description);
			if (validationStatus(index) == QStringLiteral("Invalid"))
				label += QStringLiteral(" | Invalid");
		}
		if (scenario.id == model.defaultScenarioId)
			label += QStringLiteral(" | default");
		if (m_runSelection->isScenarioModified(scenario.id))
			label += QStringLiteral(" | modified");
		if (auto* item = m_listWidget->item(static_cast<int>(index))) {
			item->setText(label);
			item->setToolTip(label);
		}
	}
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
	if (!hasScene())
		return;
	SceneModel& model = m_document->mutableModel();
	SceneScenario scenario;
	scenario.id = sceneUniqueScenarioId(model, "new_scenario");
	scenario.name = "New scenario";
	model.scenarios.push_back(std::move(scenario));
	m_runSelection->selectScenario(model.scenarios.back().id);
	finishAddition("Add scenario");
}

void ScenarioPanel::duplicateScenario() {
	const SceneScenario* selected = selectedScenarioForEdit();
	if (!selected)
		return;
	SceneModel& model = m_document->mutableModel();
	SceneScenario copy = *selected;
	copy.id = sceneUniqueScenarioId(model, copy.id + "_copy");
	copy.name = "Copy of " + copy.name;
	for (auto& incident : copy.incidents)
		incident.id = sceneUniqueIncidentId(model, incident.id + "_copy");
	model.scenarios.push_back(std::move(copy));
	m_runSelection->selectScenario(model.scenarios.back().id);
	finishAddition("Duplicate scenario");
}

void ScenarioPanel::deleteScenario() {
	if (!canEdit())
		return;
	SceneModel& model = m_document->mutableModel();
	const SceneScenario* selected = selectedScenarioForEdit();
	const auto persistedDefault = std::find_if(model.scenarios.begin(), model.scenarios.end(),
		[&model](const SceneScenario& scenario) { return scenario.id == model.defaultScenarioId; });
	if (!selected || model.defaultScenarioId.empty() || persistedDefault == model.scenarios.end()) {
		m_prompts->showError(this, "Cannot Delete Scenario",
			"The scene has no valid persisted default scenario; correct validation before deleting scenarios.", true);
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

	auto it = std::find_if(model.scenarios.begin(), model.scenarios.end(),
		[&deletedId](const SceneScenario& scenario) { return scenario.id == deletedId; });
	if (it == model.scenarios.end())
		return;
	model.scenarios.erase(it);
	m_runSelection->scenarioDeleted(deletedId, defaultId);
	m_document->markChanged(SceneDocument::Scenarios, "Delete scenario");
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
		m_prompts->showError(this, "Scenario export failed", firstDiagnosticMessage(result.diagnostics), true);
		return;
	}
	emit statusMessage(QString("Exported scenario %1").arg(scenarioContext()), 4000);
}

void ScenarioPanel::importScenario() {
	if (!hasScene())
		return;
	const QString path = m_importFileChooser
		? m_importFileChooser()
		: QFileDialog::getOpenFileName(this, "Import Scenario", QDir::homePath(), "Scenario JSON (*.json)");
	if (path.isEmpty())
		return;
	const ScenarioLoadResult loaded = loadScenarioJson(path.toStdString());
	if (!loaded.success()) {
		m_prompts->showError(this, "Scenario import failed", firstDiagnosticMessage(loaded.diagnostics), true);
		return;
	}
	SceneModel& model = m_document->mutableModel();
	SceneScenario imported = loaded.scenario;
	const std::string requestedId = imported.id;
	const std::string uniqueId = sceneUniqueScenarioId(model, imported.id);
	const bool scenarioConflict = uniqueId != imported.id;
	if (scenarioConflict)
		imported.id = uniqueId;
	int adjustedIncidentCount = 0;
	for (auto& incident : imported.incidents) {
		const std::string uniqueIncident = sceneUniqueIncidentId(model, incident.id);
		if (uniqueIncident != incident.id) {
			incident.id = uniqueIncident;
			++adjustedIncidentCount;
		}
	}

	SceneModel candidate = model;
	candidate.scenarios.push_back(imported);
	const std::size_t candidateIndex = candidate.scenarios.size() - 1;
	const std::string prefix = "scenarios[" + std::to_string(candidateIndex) + "]";
	QStringList errors;
	for (const auto& diagnostic : validateScene(candidate)) {
		if (diagnostic.severity == SceneSeverity::Error && diagnostic.path.rfind(prefix, 0) == 0)
			errors << QString::fromStdString(diagnostic.message);
	}
	if (!errors.isEmpty()) {
		m_prompts->showError(this, "Scenario import rejected", errors.join("\n"), true);
		return;
	}
	model.scenarios.push_back(std::move(imported));
	m_runSelection->selectScenario(model.scenarios.back().id);
	finishAddition("Import scenario");
	const QString importedId = QString::fromStdString(m_runSelection->scenarioId());
	QString message = QString("Imported scenario %1").arg(importedId);
	if (scenarioConflict)
		message += QString(" (ID %1 already existed; imported as %2)").arg(QString::fromStdString(requestedId), importedId);
	if (adjustedIncidentCount > 0)
		message += QString(" %1 incident ID(s) adjusted to remain globally unique.").arg(adjustedIncidentCount);
	emit statusMessage(message, 7000);
	if (scenarioConflict || adjustedIncidentCount > 0)
		m_prompts->showInfo(this, "Scenario ID adjusted", message);
}

void ScenarioPanel::commitIdEdit() {
	SceneScenario* scenario = selectedScenarioForEdit();
	if (!scenario)
		return;
	SceneModel& model = m_document->mutableModel();
	const std::string newId = m_idEdit->text().trimmed().toStdString();
	if (newId == scenario->id)
		return;
	if (newId.empty()) {
		m_prompts->showError(this, "Invalid Scenario ID", "Scenario IDs must not be empty.", true);
		updateDetail();
		return;
	}
	if (sceneUniqueScenarioId(model, newId) != newId) {
		m_prompts->showError(this, "Scenario ID already exists", "Choose a unique scenario ID.", true);
		updateDetail();
		return;
	}
	const std::string oldId = scenario->id;
	scenario->id = newId;
	sceneRewriteScenarioReferences(model, oldId, newId);
	m_runSelection->scenarioRenamed(oldId, newId);
	markEdited("Rename scenario");
	emit changed();
}

void ScenarioPanel::commitNameEdit() {
	SceneScenario* scenario = selectedScenarioForEdit();
	if (!scenario)
		return;
	const std::string value = m_nameEdit->text().toStdString();
	if (value == scenario->name)
		return;
	scenario->name = value;
	markEdited("Change scenario name");
	emit changed();
}

void ScenarioPanel::commitDescriptionEdit() {
	SceneScenario* scenario = selectedScenarioForEdit();
	if (!scenario)
		return;
	const std::string value = m_descriptionEdit->text().toStdString();
	if (value == scenario->description)
		return;
	scenario->description = value;
	markEdited("Change scenario description");
	emit changed();
}

void ScenarioPanel::commitPendingEditors() {
	if (!canEdit())
		return;
	commitIdEdit();
	commitNameEdit();
	commitDescriptionEdit();
}
