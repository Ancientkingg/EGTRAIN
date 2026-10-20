#include "editors/IncidentPanel.h"

#include "editors/ServicePanel.h"
#include "scene/RuntimeLimits.h"
#include "scene/SceneIds.h"
#include "widgets/CompactDoubleSpinBox.h"
#include "widgets/EditorLayout.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QFrame>
#include <QHash>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <set>
#include <string>

namespace {

// The signals and blocks that a signal failure can have as its target.
std::vector<std::string> signalFailureTargets(const SceneModel& sceneModel) {
	std::vector<std::string> targets;
	std::set<std::string> seen;
	const auto add = [&targets, &seen](const std::string& target) {
		if (!target.empty() && seen.insert(target).second)
			targets.push_back(target);
	};
	for (const auto& signal : sceneSignals(sceneModel))
		add(signal.id);
	for (const auto& block : sceneModel.blocks)
		add(block.id);
	return targets;
}

bool hasEditorFocus(QWidget* editor) {
	QWidget* focus = QApplication::focusWidget();
	return editor && (editor->hasFocus() || (focus && editor->isAncestorOf(focus)));
}

} // namespace

IncidentPanel::IncidentPanel(QWidget* parent) : SceneEditorPanel(parent) {
	setObjectName("incidentPanel");
	QVBoxLayout* layout = new QVBoxLayout(this);

	// the list of incidents and the buttons that manage it, in one row. The list shows about three rows, so that the fields
	// below keep the height of a short dock.
	QWidget* listPane = new QWidget(this);
	QVBoxLayout* listLayout = new QVBoxLayout(listPane);
	listLayout->addWidget(new QLabel("Incidents", listPane));
	m_listWidget = new QListWidget(listPane);
	m_listWidget->setObjectName("incidentListWidget");
	m_listWidget->setFixedHeight(m_listWidget->fontMetrics().lineSpacing() * 3 + 2 * m_listWidget->frameWidth());
	listLayout->addWidget(m_listWidget);
	QGridLayout* buttonLayout = new QGridLayout();
	m_addButton = new QPushButton("Add Incident", listPane);
	m_addButton->setObjectName("addIncidentButton");
	m_duplicateButton = new QPushButton("Duplicate", listPane);
	m_duplicateButton->setObjectName("duplicateIncidentButton");
	m_deleteButton = new QPushButton("Delete", listPane);
	m_deleteButton->setObjectName("deleteIncidentButton");
	buttonLayout->addWidget(m_addButton, 0, 0);
	buttonLayout->addWidget(m_duplicateButton, 0, 1);
	buttonLayout->addWidget(m_deleteButton, 0, 2);
	listLayout->addLayout(buttonLayout);
	layout->addWidget(listPane);

	// the fields of the selected incident; a check box that switches a field on comes before that field
	QWidget* detailPane = new QWidget(this);
	QVBoxLayout* detailOuter = new QVBoxLayout(detailPane);
	m_emptyStateLabel = EditorLayout::emptyStateLabel(detailPane);
	m_emptyStateLabel->setObjectName("incidentEmptyStateLabel");
	detailOuter->addWidget(m_emptyStateLabel);
	QFormLayout* detailLayout = new QFormLayout();
	detailLayout->setRowWrapPolicy(QFormLayout::WrapAllRows);
	m_idEdit = new QLineEdit(detailPane);
	m_idEdit->setObjectName("incidentIdEdit");
	detailLayout->addRow("Incident Id", m_idEdit);
	m_typeCombo = new QComboBox(detailPane);
	m_typeCombo->setObjectName("incidentTypeCombo");
	m_typeCombo->addItem("signal_failure");
	m_typeCombo->addItem("train_breakdown");
	detailLayout->addRow("Type", m_typeCombo);
	m_targetCombo = new QComboBox(detailPane);
	m_targetCombo->setObjectName("incidentTargetCombo");
	detailLayout->addRow("Target", m_targetCombo);
	m_startSecondsEdit = new QLineEdit(detailPane);
	m_startSecondsEdit->setObjectName("incidentStartSecondsEdit");
	m_startSecondsEdit->setValidator(new QIntValidator(0, std::numeric_limits<int>::max(), m_startSecondsEdit));
	detailLayout->addRow("Start (s)", m_startSecondsEdit);
	m_hasEndSecondsCheck = new QCheckBox("Use recovery end (s)", detailPane);
	m_hasEndSecondsCheck->setObjectName("incidentHasEndSecondsCheck");
	m_hasEndSecondsCheck->setToolTip("When disabled, a reduced-speed breakdown continues until the destination.");
	m_endSecondsEdit = new QLineEdit(detailPane);
	m_endSecondsEdit->setObjectName("incidentEndSecondsEdit");
	m_endSecondsEdit->setAccessibleName("Recovery end (s)");
	m_endSecondsEdit->setValidator(new QIntValidator(0, std::numeric_limits<int>::max(), m_endSecondsEdit));
	detailLayout->addRow(m_hasEndSecondsCheck, m_endSecondsEdit);
	m_hasReducedSpeedCheck = new QCheckBox("Reduced speed cap", detailPane);
	m_hasReducedSpeedCheck->setObjectName("incidentHasReducedSpeedCheck");
	m_reducedSpeedKmhEdit = new CompactDoubleSpinBox(detailPane);
	m_reducedSpeedKmhEdit->setObjectName("incidentReducedSpeedKmhEdit");
	m_reducedSpeedKmhEdit->setAccessibleName("Reduced speed cap");
	m_reducedSpeedKmhEdit->setRange(0.01, 1000.0);
	m_reducedSpeedKmhEdit->setDecimals(std::numeric_limits<double>::max_digits10);
	m_reducedSpeedKmhEdit->setSuffix(" km/h");
	detailLayout->addRow(m_hasReducedSpeedCheck, m_reducedSpeedKmhEdit);
	m_terminateAtDestinationCheck = new QCheckBox("Terminate at destination", detailPane);
	m_terminateAtDestinationCheck->setObjectName("incidentTerminateAtDestinationCheck");
	detailLayout->addRow(m_terminateAtDestinationCheck);
	detailOuter->addLayout(detailLayout);
	detailOuter->addStretch();

	// the detail pane may become taller than the dock, so let it scroll rather
	// than overflowing and overlapping the tab bar
	QScrollArea* detailScroll = new QScrollArea(this);
	detailScroll->setWidgetResizable(true);
	detailScroll->setFrameShape(QFrame::NoFrame);
	detailScroll->setWidget(detailPane);
	layout->addWidget(detailScroll, 2);

	connect(m_listWidget, &QListWidget::currentRowChanged, this, [this](int) { updateDetail(); });
	connect(m_idEdit, &QLineEdit::editingFinished, this, &IncidentPanel::commitIdEdit);
	connect(m_typeCombo, &QComboBox::currentTextChanged, this, &IncidentPanel::commitType);
	connect(m_targetCombo, &QComboBox::currentTextChanged, this, [this] { commitTarget(); });
	connect(m_startSecondsEdit, &QLineEdit::editingFinished, this, &IncidentPanel::commitStartSeconds);
	connect(m_endSecondsEdit, &QLineEdit::editingFinished, this, &IncidentPanel::commitEndSeconds);
	connect(m_hasReducedSpeedCheck, &QCheckBox::toggled, this, &IncidentPanel::commitHasReducedSpeed);
	connect(m_reducedSpeedKmhEdit, &QAbstractSpinBox::editingFinished, this, &IncidentPanel::commitReducedSpeed);
	connect(m_hasEndSecondsCheck, &QCheckBox::toggled, this, &IncidentPanel::commitHasEndSeconds);
	connect(m_terminateAtDestinationCheck, &QCheckBox::toggled, this, &IncidentPanel::commitTerminateAtDestination);
	connect(m_addButton, &QPushButton::clicked, this, &IncidentPanel::addIncident);
	connect(m_duplicateButton, &QPushButton::clicked, this, &IncidentPanel::duplicateIncident);
	connect(m_deleteButton, &QPushButton::clicked, this, &IncidentPanel::deleteIncident);
	IncidentPanel::refresh();
}

void IncidentPanel::setDocument(SceneDocument* document) {
	QObject::disconnect(m_replacedConnection);
	QObject::disconnect(m_changedConnection);
	m_document = document;
	if (m_document) {
		m_replacedConnection = connect(m_document, &SceneDocument::replaced, this, &IncidentPanel::refresh);
		// The occurrences that a breakdown can have as its target depend on the duration of the case and on the services,
		// and the signals and blocks of a signal failure on the infrastructure.
		m_changedConnection = connect(m_document, &SceneDocument::changed, this, [this](SceneDocument::ChangeKinds kinds) {
			if (kinds & (SceneDocument::CaseSettings | SceneDocument::Infrastructure | SceneDocument::Services))
				refreshTargets();
		});
	}
	refresh();
}

void IncidentPanel::setPrompts(UserPrompts* prompts) {
	m_prompts = prompts ? prompts : &m_ownPrompts;
}

void IncidentPanel::setRunSelection(RunSelection* selection) {
	m_runSelection = selection ? selection : &m_ownRunSelection;
}

void IncidentPanel::setOccurrenceDuration(std::function<double()> duration) {
	m_occurrenceDuration = std::move(duration);
}

bool IncidentPanel::hasScene() const {
	return m_document && m_document->isLoaded();
}

bool IncidentPanel::canEdit() const {
	return hasScene() && m_document->isEditable();
}

double IncidentPanel::occurrenceDuration() const {
	return m_occurrenceDuration ? m_occurrenceDuration() : m_document->model().settings.durationSeconds;
}

const std::vector<SceneIncident>& IncidentPanel::scenarioIncidents() const {
	static const std::vector<SceneIncident> none;
	const SceneScenario* scenario = hasScene() ? m_runSelection->selectedScenario(m_document->model()) : nullptr;
	return scenario ? scenario->incidents : none;
}

std::vector<SceneIncident>* IncidentPanel::scenarioIncidentsForEdit() {
	SceneScenario* scenario = canEdit() ? m_runSelection->selectedScenario(m_document->mutableModel()) : nullptr;
	return scenario ? &scenario->incidents : nullptr;
}

const SceneIncident* IncidentPanel::shownIncident() const {
	const auto& incidents = scenarioIncidents();
	const int row = m_listWidget->currentRow();
	return row >= 0 && row < static_cast<int>(incidents.size()) ? &incidents[static_cast<std::size_t>(row)] : nullptr;
}

SceneIncident* IncidentPanel::selectedIncident() {
	std::vector<SceneIncident>* incidents = scenarioIncidentsForEdit();
	const int row = m_listWidget->currentRow();
	return incidents && row >= 0 && row < static_cast<int>(incidents->size()) ? &(*incidents)[static_cast<std::size_t>(row)] : nullptr;
}

void IncidentPanel::markEdited(const QString& label) {
	if (const SceneScenario* scenario = m_runSelection->selectedScenario(m_document->model()))
		m_runSelection->markScenarioModified(scenario->id);
	m_document->markChanged(SceneDocument::Scenarios, label);
}

void IncidentPanel::refresh() {
	const bool loaded = hasScene();
	const auto& incidents = scenarioIncidents();
	{
		const int previousRow = m_listWidget->currentRow();
		const QSignalBlocker blocker(m_listWidget);
		m_listWidget->clear();
		if (loaded) {
			for (const auto& incident : incidents) {
				QString label = QString::fromStdString(incident.id) + " (" + QString::fromStdString(incident.type) + ")";
				if (incident.hasOccurrence || incident.occurrence != 1)
					label += QString(" #%1").arg(incident.occurrence);
				if (incident.hasReducedSpeed || incident.reducedSpeedKmh != 0.0)
					label += QString(" / %1 km/h").arg(incident.reducedSpeedKmh);
				m_listWidget->addItem(label);
			}
		}
		const int rowCount = m_listWidget->count();
		int rowToSelect = -1;
		if (rowCount > 0) {
			if (previousRow < 0)
				rowToSelect = 0;
			else if (previousRow >= rowCount)
				rowToSelect = rowCount - 1; // keep selection near a deleted last row
			else
				rowToSelect = previousRow;
		}
		m_listWidget->setCurrentRow(rowToSelect);
		m_listWidget->setEnabled(canEdit());
	}
	m_addButton->setEnabled(canEdit());
	updateDetail();
}

void IncidentPanel::updateDetail() {
	const SceneIncident* incident = canEdit() ? shownIncident() : nullptr;
	const bool hasSelection = incident != nullptr;
	const bool isBreakdown = hasSelection && incident->type == "train_breakdown";
	QString emptyState;
	if (canEdit() && m_runSelection->selectedScenario(m_document->model()) && !hasSelection) {
		emptyState = scenarioIncidents().empty() ? QStringLiteral("This scenario has no incidents. Use Add Incident.")
												 : QStringLiteral("Select an incident or use Add Incident.");
	}
	DialogLayout::setHelp(m_emptyStateLabel, emptyState);

	{
		const QSignalBlocker blocker(m_idEdit);
		m_idEdit->setText(hasSelection ? QString::fromStdString(incident->id) : QString());
		m_idEdit->setEnabled(hasSelection);
	}

	{
		const QSignalBlocker blocker(m_typeCombo);
		if (hasSelection) {
			const int typeIndex = m_typeCombo->findText(QString::fromStdString(incident->type));
			// unknown type: default to signal_failure
			m_typeCombo->setCurrentIndex(typeIndex >= 0 ? typeIndex : 0);
		} else {
			m_typeCombo->setCurrentIndex(0);
		}
		m_typeCombo->setEnabled(hasSelection);
	}

	// the target choices depend on the type; refresh the target combo after the
	// type combo is already showing the correct value
	refreshTargets();

	{
		const QSignalBlocker blocker(m_startSecondsEdit);
		m_startSecondsEdit->setText(QString::number(hasSelection ? static_cast<int>(incident->startSeconds) : 0));
		m_startSecondsEdit->setEnabled(hasSelection);
	}

	{
		const QSignalBlocker blocker(m_endSecondsEdit);
		m_endSecondsEdit->setText(QString::number(hasSelection ? static_cast<int>(incident->endSeconds) : 0));
		m_endSecondsEdit->setEnabled(hasSelection);
	}
	{
		const QSignalBlocker blocker(m_hasEndSecondsCheck);
		m_hasEndSecondsCheck->setChecked(hasSelection && (incident->hasEndSeconds || incident->endSeconds != 0.0));
		m_hasEndSecondsCheck->setEnabled(hasSelection);
	}
	{
		const QSignalBlocker blocker(m_hasReducedSpeedCheck);
		m_hasReducedSpeedCheck->setChecked(isBreakdown && (incident->hasReducedSpeed || incident->reducedSpeedKmh != 0.0));
		m_hasReducedSpeedCheck->setEnabled(isBreakdown);
	}
	{
		const QSignalBlocker blocker(m_reducedSpeedKmhEdit);
		const double cap = hasSelection && std::isfinite(incident->reducedSpeedKmh) && incident->reducedSpeedKmh > 0.0
			? incident->reducedSpeedKmh
			: 40.0;
		m_reducedSpeedKmhEdit->setValue(cap);
		m_reducedSpeedKmhEdit->setEnabled(isBreakdown && m_hasReducedSpeedCheck->isChecked());
	}
	{
		const QSignalBlocker blocker(m_terminateAtDestinationCheck);
		m_terminateAtDestinationCheck->setChecked(isBreakdown && incident->terminateAtDestination);
		m_terminateAtDestinationCheck->setEnabled(isBreakdown);
	}

	m_duplicateButton->setEnabled(hasSelection);
	m_deleteButton->setEnabled(hasSelection);
}

void IncidentPanel::refreshTargets() {
	const SceneIncident* incident = shownIncident();
	const QSignalBlocker blocker(m_targetCombo);
	m_targetCombo->clear();
	m_targetCombo->addItem(QStringLiteral("Choose target"), QString());
	int current = 0;
	if (incident) {
		const SceneModel& model = m_document->model();
		const QString target = QString::fromStdString(incident->target);
		bool found = false;
		if (incident->type == "signal_failure") {
			for (const std::string& value : signalFailureTargets(model))
				m_targetCombo->addItem(QString::fromStdString(value), QString::fromStdString(value));
			current = m_targetCombo->findData(target);
			if (current < 0 && !target.isEmpty()) {
				m_targetCombo->addItem(QStringLiteral("Invalid removed target: ") + target, target);
				current = m_targetCombo->count() - 1;
			}
		} else {
			const bool singular = incident->hasOccurrence || incident->occurrence != 1;
			if (!singular && !incident->target.empty()) {
				const bool exists = std::any_of(model.services.begin(), model.services.end(),
					[&](const SceneService& service) { return service.id == incident->target; });
				m_targetCombo->addItem(
					(exists ? QStringLiteral("Historical scope: all generated services [")
							: QStringLiteral("Invalid removed target; historical all scope ["))
						+ target + QStringLiteral("]"),
					target);
				const int index = m_targetCombo->count() - 1;
				m_targetCombo->setItemData(index, 0, Qt::UserRole + 1);
				current = index;
			}
			int shown = 0;
			for (const SceneService& service : model.services) {
				const int count = std::max(0, sceneServiceOccurrenceCount(service, occurrenceDuration()));
				for (int occurrence = 1; occurrence <= count && shown < RuntimeLimits::kMaxExpandedTrains; ++occurrence, ++shown) {
					m_targetCombo->addItem(ServicePanel::generatedServiceLabel(service, occurrence), QString::fromStdString(service.id));
					const int index = m_targetCombo->count() - 1;
					m_targetCombo->setItemData(index, occurrence, Qt::UserRole + 1);
					if (singular && service.id == incident->target && occurrence == incident->occurrence) {
						current = index;
						found = true;
					}
				}
				if (shown >= RuntimeLimits::kMaxExpandedTrains)
					break;
			}
			if (singular && !found && !incident->target.empty()) {
				for (const SceneService& service : model.services) {
					if (service.id != incident->target)
						continue;
					const int count = std::max(0, sceneServiceOccurrenceCount(service, occurrenceDuration()));
					if (incident->occurrence < 1 || incident->occurrence > count)
						break;
					m_targetCombo->addItem(ServicePanel::generatedServiceLabel(service, incident->occurrence),
						QString::fromStdString(service.id));
					current = m_targetCombo->count() - 1;
					m_targetCombo->setItemData(current, incident->occurrence, Qt::UserRole + 1);
					found = true;
					break;
				}
			}
		}
		if (incident->type != "signal_failure" && !incident->target.empty()
			&& ((incident->hasOccurrence || incident->occurrence != 1) ? !found : current == 0)) {
			m_targetCombo->addItem(
				QStringLiteral("Invalid removed target [") + target + QStringLiteral(", ") + QString::number(incident->occurrence)
					+ QStringLiteral("]"),
				target);
			current = m_targetCombo->count() - 1;
			m_targetCombo->setItemData(current, incident->hasOccurrence || incident->occurrence != 1 ? incident->occurrence : 0,
				Qt::UserRole + 1);
		}
	}
	QHash<QString, int> labelCounts;
	for (int index = 0; index < m_targetCombo->count(); ++index)
		++labelCounts[m_targetCombo->itemText(index)];
	for (int index = 0; index < m_targetCombo->count(); ++index) {
		const QString label = m_targetCombo->itemText(index);
		const QString identity = QString(" [%1, #%2]")
									 .arg(m_targetCombo->itemData(index).toString())
									 .arg(m_targetCombo->itemData(index, Qt::UserRole + 1).toInt());
		if (labelCounts.value(label) > 1)
			m_targetCombo->setItemText(index, label + identity);
		m_targetCombo->setItemData(index, label + identity, Qt::ToolTipRole);
	}
	m_targetCombo->setCurrentIndex(std::max(0, current));
	m_targetCombo->setEnabled(incident != nullptr && canEdit());
	m_targetCombo->setToolTip(QStringLiteral("Service code with route, elapsed scheduled entry and stable identity."));
}

void IncidentPanel::addIncident() {
	std::vector<SceneIncident>* incidents = scenarioIncidentsForEdit();
	if (!incidents)
		return;
	SceneIncident incident;
	incident.id = sceneUniqueIncidentId(m_document->model(), "new_incident");
	incident.type = "signal_failure";
	incident.hasEndSeconds = true;
	incidents->push_back(incident);

	markEdited("Add incident");
	refresh();
	emit changed();

	m_listWidget->setCurrentRow(static_cast<int>(incidents->size()) - 1);
}

void IncidentPanel::duplicateIncident() {
	std::vector<SceneIncident>* incidents = scenarioIncidentsForEdit();
	const int row = m_listWidget->currentRow();
	if (!incidents || row < 0 || row >= static_cast<int>(incidents->size()))
		return;

	SceneIncident duplicate = (*incidents)[static_cast<std::size_t>(row)];
	duplicate.id = sceneUniqueIncidentId(m_document->model(), duplicate.id + "_copy");
	incidents->insert(incidents->begin() + row + 1, duplicate);

	markEdited("Duplicate incident");
	refresh();
	emit changed();

	m_listWidget->setCurrentRow(row + 1);
}

void IncidentPanel::deleteIncident() {
	std::vector<SceneIncident>* incidents = scenarioIncidentsForEdit();
	const int row = m_listWidget->currentRow();
	if (!incidents || row < 0 || row >= static_cast<int>(incidents->size()))
		return;

	const QString id = QString::fromStdString((*incidents)[static_cast<std::size_t>(row)].id);
	if (!m_prompts->confirm(this, "Delete Incident", QString("Delete incident '%1'?").arg(id)))
		return;

	incidents->erase(incidents->begin() + row);

	markEdited("Delete incident");
	refresh();
	emit changed();
}

void IncidentPanel::commitIdEdit() {
	SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	const std::string newId = m_idEdit->text().trimmed().toStdString();
	if (newId == incident->id)
		return;
	if (newId.empty()) {
		m_prompts->showError(this, "Invalid Incident ID", "Incident IDs must not be empty.", true);
		updateDetail();
		return;
	}
	if (sceneUniqueIncidentId(m_document->model(), newId) != newId) {
		m_prompts->showError(this, "Incident ID already exists", "Incident IDs must be unique across all scenarios.", true);
		updateDetail();
		return;
	}

	incident->id = newId;

	// update the list row label in place instead of rebuilding the panel, so a
	// focus-out that lands on another control keeps that pending click intact
	if (QListWidgetItem* item = m_listWidget->currentItem()) {
		const QSignalBlocker blocker(m_listWidget);
		item->setText(QString::fromStdString(newId) + " (" + QString::fromStdString(incident->type) + ")");
	}

	markEdited("Rename incident");
	emit changed();
}

void IncidentPanel::commitType(const QString& type) {
	SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	const std::string newType = type.toStdString();
	if (newType == incident->type)
		return;

	incident->type = newType;
	if (newType == "signal_failure") {
		incident->hasOccurrence = false;
		incident->occurrence = 1;
		incident->hasReducedSpeed = false;
		incident->reducedSpeedKmh = 0.0;
		incident->terminateAtDestination = false;
	}

	// when the type changes the valid target pool also changes; drop a target
	// that is no longer valid for the new type
	bool targetValid = incident->target.empty();
	if (!targetValid) {
		const SceneModel& model = m_document->model();
		if (newType == "signal_failure") {
			const auto targets = signalFailureTargets(model);
			targetValid = std::find(targets.begin(), targets.end(), incident->target) != targets.end();
		} else {
			targetValid = std::any_of(model.services.begin(), model.services.end(),
				[incident](const SceneService& service) { return service.id == incident->target; });
		}
	}
	if (!targetValid)
		incident->target.clear();

	// update the list row label in place to reflect the new type
	if (QListWidgetItem* item = m_listWidget->currentItem()) {
		const QSignalBlocker blocker(m_listWidget);
		item->setText(QString::fromStdString(incident->id) + " (" + type + ")");
	}

	markEdited("Change incident type");
	// The target choices and breakdown-only controls both depend on the type.
	updateDetail();
	emit changed();
}

void IncidentPanel::commitTarget() {
	SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	const std::string newTarget = m_targetCombo->currentData().toString().toStdString();
	int occurrence = m_targetCombo->currentData(Qt::UserRole + 1).toInt();
	const bool singular = incident->type != "signal_failure" && occurrence > 0;
	occurrence = std::max(1, occurrence);
	if (incident->target == newTarget && incident->hasOccurrence == singular && incident->occurrence == occurrence)
		return;
	incident->target = newTarget;
	incident->hasOccurrence = singular;
	incident->occurrence = occurrence;
	markEdited("Change incident target");
	updateDetail();
	emit changed();
}

void IncidentPanel::commitStartSeconds() {
	SceneIncident* incident = selectedIncident();
	if (!incident)
		return;

	bool ok = false;
	int seconds = m_startSecondsEdit->text().toInt(&ok);
	if (!ok)
		seconds = 0;

	// normalize a blank or partial entry back to a plain integer display
	{
		const QSignalBlocker blocker(m_startSecondsEdit);
		m_startSecondsEdit->setText(QString::number(seconds));
	}

	const double newValue = static_cast<double>(seconds);
	if (newValue == incident->startSeconds)
		return;

	incident->startSeconds = newValue;

	markEdited("Change incident start");
	emit changed();
}

void IncidentPanel::commitEndSeconds() {
	SceneIncident* incident = selectedIncident();
	if (!incident)
		return;

	bool ok = false;
	int seconds = m_endSecondsEdit->text().toInt(&ok);
	if (!ok)
		seconds = 0;

	// normalize a blank or partial entry back to a plain integer display
	{
		const QSignalBlocker blocker(m_endSecondsEdit);
		m_endSecondsEdit->setText(QString::number(seconds));
	}

	const double newValue = static_cast<double>(seconds);
	if (newValue == incident->endSeconds && incident->hasEndSeconds)
		return;

	incident->endSeconds = newValue;
	incident->hasEndSeconds = true;

	markEdited("Change incident end");
	emit changed();
}

void IncidentPanel::commitReducedSpeed() {
	SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	const double speed = m_reducedSpeedKmhEdit->value();
	if (incident->reducedSpeedKmh == speed && incident->hasReducedSpeed)
		return;
	incident->reducedSpeedKmh = speed;
	incident->hasReducedSpeed = true;
	markEdited("Change incident reduced speed");
	refresh();
	emit changed();
}

void IncidentPanel::commitHasReducedSpeed(bool checked) {
	SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	if (incident->hasReducedSpeed == checked)
		return;
	incident->hasReducedSpeed = checked;
	if (checked)
		incident->reducedSpeedKmh = m_reducedSpeedKmhEdit->value();
	else
		incident->reducedSpeedKmh = 0.0;
	markEdited("Change incident reduced speed");
	updateDetail();
	emit changed();
}

void IncidentPanel::commitHasEndSeconds(bool checked) {
	SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	if (incident->hasEndSeconds == checked)
		return;
	incident->hasEndSeconds = checked;
	if (!checked)
		incident->endSeconds = 0.0;
	markEdited("Change incident end");
	updateDetail();
	emit changed();
}

void IncidentPanel::commitTerminateAtDestination(bool checked) {
	SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	if (incident->terminateAtDestination == checked)
		return;
	incident->terminateAtDestination = checked;
	markEdited("Change incident termination");
	emit changed();
}

void IncidentPanel::commitPendingEditors() {
	if (!canEdit())
		return;
	commitIdEdit();
	commitStartSeconds();
	if (m_hasEndSecondsCheck->isChecked() || hasEditorFocus(m_endSecondsEdit))
		commitEndSeconds();
	if (m_hasReducedSpeedCheck->isChecked()) {
		m_reducedSpeedKmhEdit->interpretText();
		commitReducedSpeed();
	}
}
