#include "editors/IncidentPanel.h"

#include "editors/EditorHelpers.h"
#include "editors/EditorRules.h"
#include "scene/SceneIds.h"
#include "widgets/ChoiceComboBox.h"
#include "widgets/CompactDoubleSpinBox.h"
#include "widgets/EditorLayout.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

namespace {

// The speed cap that the field shows: the stored one, or 40 km/h to start from when none is stored.
double shownReducedSpeed(const QDoubleSpinBox* edit, const SceneIncident* incident) {
	const bool stored = incident && std::isfinite(incident->reducedSpeedKmh) && incident->reducedSpeedKmh > 0.0;
	return shownValue(edit, stored ? incident->reducedSpeedKmh : 40.0);
}

// The name of an incident type on screen; a type that is none of the two shows as it is stored.
QString typeText(const std::string& type) {
	if (type == "signal_failure")
		return QStringLiteral("Signal failure");
	if (type == "train_breakdown")
		return QStringLiteral("Train breakdown");
	return QString::fromStdString(type);
}

// The text of the row of an incident in the list.
QString rowLabel(const SceneIncident& incident) {
	QString label = QString::fromStdString(incident.id) + ": " + typeText(incident.type);
	if (incident.hasOccurrence || incident.occurrence != 1)
		label += QString(" #%1").arg(incident.occurrence);
	if (incident.hasReducedSpeed || incident.reducedSpeedKmh != 0.0)
		label += QString(", %1 km/h").arg(incident.reducedSpeedKmh);
	return label;
}

// Gives the incident the target with the occurrence as the target combo holds it: 0 for none, or for all occurrences of a
// service. False when the incident has that target already.
bool setTarget(SceneIncident& incident, const std::string& target, int occurrence) {
	const bool singular = incident.type != "signal_failure" && occurrence > 0;
	occurrence = std::max(1, occurrence);
	if (incident.target == target && incident.hasOccurrence == singular && incident.occurrence == occurrence)
		return false;
	incident.target = target;
	incident.hasOccurrence = singular;
	incident.occurrence = occurrence;
	return true;
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
	// The fields take the width of the form also on macOS, where they stay at their size hint by default.
	detailLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_idEdit = new QLineEdit(detailPane);
	m_idEdit->setObjectName("incidentIdEdit");
	detailLayout->addRow("Incident ID", m_idEdit);
	// The data of an item is the type as the scene stores it.
	m_typeCombo = new QComboBox(detailPane);
	m_typeCombo->setObjectName("incidentTypeCombo");
	m_typeCombo->addItem(typeText("signal_failure"), QStringLiteral("signal_failure"));
	m_typeCombo->addItem(typeText("train_breakdown"), QStringLiteral("train_breakdown"));
	detailLayout->addRow("Type", m_typeCombo);
	// A long train label is cut in the middle.
	m_targetCombo = new ChoiceComboBox(detailPane);
	m_targetCombo->setObjectName("incidentTargetCombo");
	m_pickTargetButton = new QToolButton(detailPane);
	m_pickTargetButton->setObjectName("incidentPickTargetButton");
	m_pickTargetButton->setCheckable(true);
	m_pickTargetButton->setIcon(QIcon(":/icons/pick-target.svg"));
	m_pickTargetButton->setText("Pick");
	m_pickTargetButton->setToolTip("Pick the target on the network view (Esc cancels)");
	m_pickTargetButton->setAccessibleName("Pick target on network view");
	// The label names the combo, and the pick button follows the combo on its row.
	QLabel* targetLabel = new QLabel("Target", detailPane);
	targetLabel->setBuddy(m_targetCombo);
	QHBoxLayout* targetLayout = new QHBoxLayout();
	targetLayout->addWidget(m_targetCombo, 1);
	targetLayout->addWidget(m_pickTargetButton);
	detailLayout->addRow(targetLabel, targetLayout);
	m_startSecondsEdit = exactNumberEdit("incidentStartSecondsEdit", detailPane);
	detailLayout->addRow("Start (s)", m_startSecondsEdit);
	m_hasEndSecondsCheck = new QCheckBox("End (s)", detailPane);
	m_hasEndSecondsCheck->setObjectName("incidentHasEndSecondsCheck");
	m_hasEndSecondsCheck->setToolTip("Leave off to keep a slowed train slow until its destination.");
	m_endSecondsEdit = exactNumberEdit("incidentEndSecondsEdit", detailPane);
	m_endSecondsEdit->setAccessibleName("End (s)");
	detailLayout->addRow(m_hasEndSecondsCheck, m_endSecondsEdit);
	m_hasReducedSpeedCheck = new QCheckBox("Reduced speed", detailPane);
	m_hasReducedSpeedCheck->setObjectName("incidentHasReducedSpeedCheck");
	m_reducedSpeedKmhEdit = new CompactDoubleSpinBox(detailPane);
	m_reducedSpeedKmhEdit->setObjectName("incidentReducedSpeedKmhEdit");
	m_reducedSpeedKmhEdit->setAccessibleName("Reduced speed");
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
	// Tab reaches the fields inside, and the area scrolls to show them.
	detailScroll->setFocusPolicy(Qt::NoFocus);
	detailScroll->setWidget(detailPane);
	layout->addWidget(detailScroll, 2);

	connect(m_listWidget, &QListWidget::currentRowChanged, this, [this](int) {
		endTargetPick();
		updateDetail();
	});
	connect(m_idEdit, &QLineEdit::editingFinished, this, &IncidentPanel::commitIdEdit);
	connect(m_typeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
		if (index >= 0)
			commitType(m_typeCombo->itemData(index).toString());
	});
	connect(m_targetCombo, &QComboBox::currentTextChanged, this, [this] { commitTarget(); });
	connect(m_startSecondsEdit, &QAbstractSpinBox::editingFinished, this, &IncidentPanel::commitStartSeconds);
	connect(m_endSecondsEdit, &QAbstractSpinBox::editingFinished, this, &IncidentPanel::commitEndSeconds);
	connect(m_hasReducedSpeedCheck, &QCheckBox::toggled, this, &IncidentPanel::commitHasReducedSpeed);
	connect(m_reducedSpeedKmhEdit, &QAbstractSpinBox::editingFinished, this, &IncidentPanel::commitReducedSpeed);
	connect(m_hasEndSecondsCheck, &QCheckBox::toggled, this, &IncidentPanel::commitHasEndSeconds);
	connect(m_terminateAtDestinationCheck, &QCheckBox::toggled, this, &IncidentPanel::commitTerminateAtDestination);
	connect(m_addButton, &QPushButton::clicked, this, qOverload<>(&IncidentPanel::addIncident));
	connect(m_duplicateButton, &QPushButton::clicked, this, &IncidentPanel::duplicateIncident);
	connect(m_deleteButton, &QPushButton::clicked, this, &IncidentPanel::deleteIncident);
	connect(m_pickTargetButton, &QToolButton::toggled, this, &IncidentPanel::targetPickToggled);
	IncidentPanel::refresh();
}

void IncidentPanel::documentChanged(SceneDocument::ChangeKinds kinds) {
	// The occurrences that a breakdown can have as its target depend on the duration of the case and on the services, and
	// the signals and blocks of a signal failure on the infrastructure. The panel does not edit the infrastructure, and the
	// targets leave the other fields as they are. The pick ends before the text is taken, while the window still hears
	// of it: a taken speed shows the incidents again, which ends the pick with the signals of the panel blocked.
	if (kinds.testFlag(SceneDocument::Infrastructure)) {
		endTargetPick();
		takePendingText();
		refresh();
	} else if (kinds & (SceneDocument::CaseSettings | SceneDocument::Services)) {
		refreshTargets();
	}
}

void IncidentPanel::setDefaultTarget(std::function<IncidentTarget()> target) {
	m_defaultTarget = std::move(target);
}

bool IncidentPanel::offersTarget(const IncidentTarget& target) const {
	if (!hasScene())
		return false;
	const SceneModel& model = m_document->model();
	const std::string id = target.id.toStdString();
	if (target.type == "signal_failure") {
		const auto targets = signalFailureTargets(model);
		return std::find(targets.begin(), targets.end(), id) != targets.end();
	}
	return target.type == "train_breakdown" && target.occurrence >= 1
		&& std::any_of(model.services.begin(), model.services.end(), [this, &id, &target](const SceneService& service) {
			   return service.id == id && target.occurrence <= sceneServiceOccurrenceCount(service, occurrenceDuration());
		   });
}

void IncidentPanel::changeType(SceneIncident& incident, const std::string& type) const {
	incident.type = type;
	if (type == "signal_failure") {
		incident.hasOccurrence = false;
		incident.occurrence = 1;
		incident.hasReducedSpeed = false;
		incident.reducedSpeedKmh = 0.0;
		incident.terminateAtDestination = false;
	}

	// when the type changes the valid target pool also changes; drop a target
	// that is no longer valid for the new type
	bool targetValid = incident.target.empty();
	if (!targetValid) {
		const SceneModel& model = m_document->model();
		if (type == "signal_failure") {
			const auto targets = signalFailureTargets(model);
			targetValid = std::find(targets.begin(), targets.end(), incident.target) != targets.end();
		} else {
			targetValid = std::any_of(model.services.begin(), model.services.end(),
				[&incident](const SceneService& service) { return service.id == incident.target; });
		}
	}
	if (!targetValid)
		incident.target.clear();
}

const std::vector<SceneIncident>& IncidentPanel::scenarioIncidents() const {
	static const std::vector<SceneIncident> none;
	const SceneScenario* scenario = hasScene() ? m_runSelection->selectedScenario(m_document->model()) : nullptr;
	return scenario ? scenario->incidents : none;
}

const SceneIncident* IncidentPanel::shownIncident() const {
	const auto& incidents = scenarioIncidents();
	const int row = m_listWidget->currentRow();
	return row >= 0 && row < static_cast<int>(incidents.size()) ? &incidents[static_cast<std::size_t>(row)] : nullptr;
}

const SceneIncident* IncidentPanel::selectedIncident() const {
	return canEdit() ? shownIncident() : nullptr;
}

bool IncidentPanel::editIncidents(const QString& label, const std::function<void(std::vector<SceneIncident>&)>& mutate) {
	return m_document->edit(SceneDocument::Scenarios, label, [this, &mutate](SceneModel& model) {
		SceneScenario* scenario = m_runSelection->selectedScenario(model);
		mutate(scenario->incidents);
		m_runSelection->markScenarioModified(scenario->id);
	});
}

bool IncidentPanel::editSelected(const QString& label, const std::function<void(SceneIncident&)>& mutate) {
	const auto row = static_cast<std::size_t>(m_listWidget->currentRow());
	return editIncidents(label, [row, &mutate](std::vector<SceneIncident>& incidents) { mutate(incidents[row]); });
}

void IncidentPanel::refresh() {
	endTargetPick();
	const bool loaded = hasScene();
	const auto& incidents = scenarioIncidents();
	{
		const int previousRow = m_listWidget->currentRow();
		const QSignalBlocker blocker(m_listWidget);
		m_listWidget->clear();
		if (loaded) {
			for (const auto& incident : incidents)
				m_listWidget->addItem(rowLabel(incident));
		}
		restoreListSelection(m_listWidget, QString(), previousRow);
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
			const int typeIndex = m_typeCombo->findData(QString::fromStdString(incident->type));
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
		m_startSecondsEdit->setValue(hasSelection ? incident->startSeconds : 0.0);
		m_startSecondsEdit->setEnabled(hasSelection);
	}

	{
		const QSignalBlocker blocker(m_endSecondsEdit);
		m_endSecondsEdit->setValue(hasSelection ? incident->endSeconds : 0.0);
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
		m_reducedSpeedKmhEdit->setValue(shownReducedSpeed(m_reducedSpeedKmhEdit, hasSelection ? incident : nullptr));
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
	const bool signalFailure = incident && incident->type == "signal_failure";
	const SceneModel noScene;
	const IncidentTargetChoices choices = incident ? incidentTargetChoices(m_document->model(), incident, occurrenceDuration())
												   : incidentTargetChoices(noScene, nullptr, 0.0);
	const QSignalBlocker blocker(m_targetCombo);
	m_targetCombo->clear();
	for (const IncidentTargetChoice& entry : choices.entries) {
		m_targetCombo->addItem(entry.text, entry.target);
		const int index = m_targetCombo->count() - 1;
		if (entry.occurrence)
			m_targetCombo->setItemData(index, *entry.occurrence, Qt::UserRole + 1);
		m_targetCombo->setItemData(index, entry.toolTip, Qt::ToolTipRole);
	}
	m_targetCombo->setCurrentIndex(choices.current);
	const bool editable = incident != nullptr && canEdit();
	if (!editable)
		endTargetPick();
	m_targetCombo->setEnabled(editable);
	m_pickTargetButton->setEnabled(editable);
	// The field shows this tip below the current target.
	if (!incident)
		m_targetCombo->setToolTip(QString());
	else if (signalFailure)
		m_targetCombo->setToolTip(QStringLiteral("The signal or block that fails."));
	else
		m_targetCombo->setToolTip(QStringLiteral("The train that breaks down."));
}

void IncidentPanel::addIncident() {
	if (!canEdit())
		return;
	// Asked before the edit, which can clear what the target comes from.
	const IncidentTarget target = m_defaultTarget ? m_defaultTarget() : IncidentTarget();
	appendIncident(offersTarget(target) ? target : IncidentTarget());
}

bool IncidentPanel::addIncident(const IncidentTarget& target) {
	if (!offersTarget(target) || !canEdit())
		return false;
	appendIncident(target);
	return true;
}

void IncidentPanel::appendIncident(const IncidentTarget& target) {
	SceneIncident incident;
	incident.id = sceneUniqueIncidentId(m_document->model(), "new_incident");
	incident.type = "signal_failure";
	incident.hasEndSeconds = true;
	if (!target.type.isEmpty()) {
		changeType(incident, target.type.toStdString());
		setTarget(incident, target.id.toStdString(), target.occurrence);
	}
	if (!editIncidents("Add incident", [&incident](std::vector<SceneIncident>& incidents) { incidents.push_back(incident); }))
		return;

	refresh();
	emit changed();

	m_listWidget->setCurrentRow(static_cast<int>(scenarioIncidents().size()) - 1);
}

void IncidentPanel::duplicateIncident() {
	const SceneIncident* selected = selectedIncident();
	if (!selected)
		return;

	const int row = m_listWidget->currentRow();
	SceneIncident duplicate = *selected;
	duplicate.id = sceneUniqueIncidentId(m_document->model(), duplicate.id + "_copy");
	if (!editIncidents("Duplicate incident", [row, &duplicate](std::vector<SceneIncident>& incidents) {
			incidents.insert(incidents.begin() + row + 1, duplicate);
		}))
		return;

	refresh();
	emit changed();

	m_listWidget->setCurrentRow(row + 1);
}

void IncidentPanel::deleteIncident() {
	const SceneIncident* selected = selectedIncident();
	if (!selected)
		return;

	const int row = m_listWidget->currentRow();
	const QString id = QString::fromStdString(selected->id);
	if (!m_prompts->confirm(this, "Delete Incident", QString("Delete incident '%1'?").arg(id)))
		return;

	if (!editIncidents("Delete incident", [row](std::vector<SceneIncident>& incidents) { incidents.erase(incidents.begin() + row); }))
		return;

	refresh();
	emit changed();
}

void IncidentPanel::commitIdEdit() {
	const SceneIncident* incident = selectedIncident();
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
		m_prompts->showError(this, "Duplicate Incident ID", "Incident IDs must be unique across all scenarios.", true);
		updateDetail();
		return;
	}

	if (!editSelected("Rename incident", [this, &newId](SceneIncident& edited) {
			edited.id = newId;

			// update the list row label in place instead of rebuilding the panel, so a
			// focus-out that lands on another control keeps that pending click intact
			if (QListWidgetItem* item = m_listWidget->currentItem()) {
				const QSignalBlocker blocker(m_listWidget);
				item->setText(rowLabel(edited));
			}
		}))
		return;

	emit changed();
}

void IncidentPanel::commitType(const QString& type) {
	const SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	const std::string newType = type.toStdString();
	if (newType == incident->type)
		return;

	if (!editSelected("Change incident type", [this, &newType](SceneIncident& edited) {
			changeType(edited, newType);

			// update the list row label in place to reflect the new type
			if (QListWidgetItem* item = m_listWidget->currentItem()) {
				const QSignalBlocker blocker(m_listWidget);
				item->setText(rowLabel(edited));
			}
		}))
		return;

	// The target choices and breakdown-only controls both depend on the type.
	updateDetail();
	emit changed();
}

void IncidentPanel::commitTarget() {
	const SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	SceneIncident updated = *incident;
	if (!setTarget(updated, m_targetCombo->currentData().toString().toStdString(),
			m_targetCombo->currentData(Qt::UserRole + 1).toInt()))
		return;
	if (!editSelected("Change incident target", [&updated](SceneIncident& edited) { edited = updated; }))
		return;
	updateDetail();
	emit changed();
}

bool IncidentPanel::applyTarget(const IncidentTarget& target) {
	const SceneIncident* incident = selectedIncident();
	if (!incident || !offersTarget(target))
		return false;
	const std::string type = target.type.toStdString();
	const bool typeChanged = incident->type != type;
	SceneIncident updated = *incident;
	if (typeChanged)
		changeType(updated, type);
	if (!setTarget(updated, target.id.toStdString(), target.occurrence) && !typeChanged)
		return true;
	if (!editSelected("Change incident target", [this, &updated](SceneIncident& edited) {
			edited = updated;
			if (QListWidgetItem* item = m_listWidget->currentItem()) {
				const QSignalBlocker blocker(m_listWidget);
				item->setText(rowLabel(edited));
			}
		}))
		return false;
	updateDetail();
	emit changed();
	return true;
}

bool IncidentPanel::isPickingTarget() const {
	return m_pickTargetButton->isChecked();
}

void IncidentPanel::endTargetPick() {
	m_pickTargetButton->setChecked(false);
}

void IncidentPanel::hideEvent(QHideEvent* event) {
	if (!event->spontaneous())
		endTargetPick();
	SceneEditorPanel::hideEvent(event);
}

void IncidentPanel::commitStartSeconds() {
	const SceneIncident* incident = selectedIncident();
	if (!incident)
		return;

	const double newValue = m_startSecondsEdit->value();
	if (newValue == shownValue(m_startSecondsEdit, incident->startSeconds))
		return;

	if (!editSelected("Change incident start", [newValue](SceneIncident& edited) { edited.startSeconds = newValue; }))
		return;

	emit changed();
}

void IncidentPanel::commitEndSeconds() {
	const SceneIncident* incident = selectedIncident();
	if (!incident)
		return;

	// The field shows the end while its box is off too. Leaving it as it is does not switch the end on.
	const double newValue = m_endSecondsEdit->value();
	if (newValue == shownValue(m_endSecondsEdit, incident->endSeconds))
		return;

	if (!editSelected("Change incident end", [newValue](SceneIncident& edited) {
			edited.endSeconds = newValue;
			edited.hasEndSeconds = true;
		}))
		return;

	emit changed();
}

void IncidentPanel::commitReducedSpeed() {
	const SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	const double speed = m_reducedSpeedKmhEdit->value();
	if (speed == shownReducedSpeed(m_reducedSpeedKmhEdit, incident))
		return;
	if (!editSelected("Change incident reduced speed", [speed](SceneIncident& edited) {
			edited.reducedSpeedKmh = speed;
			edited.hasReducedSpeed = true;
		}))
		return;
	refresh();
	emit changed();
}

void IncidentPanel::commitHasReducedSpeed(bool checked) {
	const SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	if (incident->hasReducedSpeed == checked)
		return;
	const double speed = checked ? m_reducedSpeedKmhEdit->value() : 0.0;
	if (!editSelected("Change incident reduced speed", [checked, speed](SceneIncident& edited) {
			edited.hasReducedSpeed = checked;
			edited.reducedSpeedKmh = speed;
		}))
		return;
	updateDetail();
	emit changed();
}

void IncidentPanel::commitHasEndSeconds(bool checked) {
	const SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	if (incident->hasEndSeconds == checked)
		return;
	if (!editSelected("Change incident end", [checked](SceneIncident& edited) {
			edited.hasEndSeconds = checked;
			if (!checked)
				edited.endSeconds = 0.0;
		}))
		return;
	updateDetail();
	emit changed();
}

void IncidentPanel::commitTerminateAtDestination(bool checked) {
	const SceneIncident* incident = selectedIncident();
	if (!incident)
		return;
	if (incident->terminateAtDestination == checked)
		return;
	if (!editSelected("Change incident termination", [checked](SceneIncident& edited) { edited.terminateAtDestination = checked; }))
		return;
	emit changed();
}

void IncidentPanel::commitPendingEditors() {
	if (!canEdit())
		return;
	commitIdEdit();
	// A number field commits only the text that is being typed in it, so that a value the user did not edit is kept.
	if (hasEditorFocus(m_startSecondsEdit)) {
		m_startSecondsEdit->interpretText();
		commitStartSeconds();
	}
	if (hasEditorFocus(m_endSecondsEdit)) {
		m_endSecondsEdit->interpretText();
		commitEndSeconds();
	}
	if (hasEditorFocus(m_reducedSpeedKmhEdit)) {
		m_reducedSpeedKmhEdit->interpretText();
		commitReducedSpeed();
	}
}
