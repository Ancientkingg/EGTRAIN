#include "editors/EntranceDelayPanel.h"

#include "editors/ServicePanel.h"
#include "widgets/CompactDoubleSpinBox.h"
#include "widgets/EditorLayout.h"

#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <set>
#include <string>

namespace {

bool hasEditorFocus(QWidget* editor) {
	QWidget* focus = QApplication::focusWidget();
	return editor && (editor->hasFocus() || (focus && editor->isAncestorOf(focus)));
}

// The first stop at which a train of the service plans to depart, which is where it can be held, or null.
const SceneStop* firstDepartureStop(const SceneService& service) {
	for (const SceneStop& stop : service.stops) {
		if (stop.hasPlannedDeparture)
			return &stop;
	}
	return nullptr;
}

} // namespace

EntranceDelayPanel::EntranceDelayPanel(QWidget* parent) : SceneEditorPanel(parent) {
	setObjectName("entranceDelayPanel");
	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel("Entrance Delays", this));
	m_listWidget = new QListWidget(this);
	m_listWidget->setObjectName("entranceDelayListWidget");
	m_listWidget->setAccessibleName("Entrance delay library");
	layout->addWidget(m_listWidget, 1);
	QHBoxLayout* buttonLayout = new QHBoxLayout();
	m_addButton = new QPushButton("Add", this);
	m_addButton->setObjectName("entranceDelayAddButton");
	m_duplicateButton = new QPushButton("Duplicate", this);
	m_duplicateButton->setObjectName("entranceDelayDuplicateButton");
	m_deleteButton = new QPushButton("Delete", this);
	m_deleteButton->setObjectName("entranceDelayDeleteButton");
	buttonLayout->addWidget(m_addButton);
	buttonLayout->addWidget(m_duplicateButton);
	buttonLayout->addWidget(m_deleteButton);
	layout->addLayout(buttonLayout);
	m_emptyStateLabel = EditorLayout::emptyStateLabel(this);
	m_emptyStateLabel->setObjectName("entranceDelayEmptyStateLabel");
	layout->addWidget(m_emptyStateLabel);
	QFormLayout* detailLayout = new QFormLayout();
	m_serviceCombo = new QComboBox(this);
	m_serviceCombo->setObjectName("entranceDelayServiceCombo");
	detailLayout->addRow("Service", m_serviceCombo);
	m_occurrenceEdit = new QSpinBox(this);
	m_occurrenceEdit->setObjectName("entranceDelayOccurrenceSpin");
	m_occurrenceEdit->setRange(1, 1);
	m_occurrenceEdit->setKeyboardTracking(false);
	detailLayout->addRow("Occurrence", m_occurrenceEdit);
	m_occurrenceContextLabel = new QLabel(this);
	m_occurrenceContextLabel->setObjectName("entranceDelayOccurrenceContextLabel");
	m_occurrenceContextLabel->setWordWrap(true);
	detailLayout->addRow(QString(), m_occurrenceContextLabel);
	m_stationCombo = new QComboBox(this);
	m_stationCombo->setObjectName("entranceDelayStationCombo");
	detailLayout->addRow("Station", m_stationCombo);
	m_secondsEdit = new CompactDoubleSpinBox(this);
	m_secondsEdit->setObjectName("entranceDelaySecondsEdit");
	m_secondsEdit->setRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
	m_secondsEdit->setDecimals(std::numeric_limits<double>::max_digits10);
	m_secondsEdit->setKeyboardTracking(false);
	detailLayout->addRow("Delay (s)", m_secondsEdit);
	layout->addLayout(detailLayout);

	connect(m_listWidget, &QListWidget::currentRowChanged, this, [this](int) { updateDetail(); });
	connect(m_addButton, &QPushButton::clicked, this, &EntranceDelayPanel::addEntranceDelay);
	connect(m_duplicateButton, &QPushButton::clicked, this, &EntranceDelayPanel::duplicateEntranceDelay);
	connect(m_deleteButton, &QPushButton::clicked, this, &EntranceDelayPanel::deleteEntranceDelay);
	connect(m_serviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &EntranceDelayPanel::commitService);
	connect(m_occurrenceEdit, &QAbstractSpinBox::editingFinished, this, &EntranceDelayPanel::commitOccurrence);
	connect(m_stationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &EntranceDelayPanel::commitStation);
	connect(m_secondsEdit, &QAbstractSpinBox::editingFinished, this, &EntranceDelayPanel::commitSeconds);
	EntranceDelayPanel::refresh();
}

void EntranceDelayPanel::setDocument(SceneDocument* document) {
	QObject::disconnect(m_replacedConnection);
	QObject::disconnect(m_changedConnection);
	m_document = document;
	if (m_document) {
		m_replacedConnection = connect(m_document, &SceneDocument::replaced, this, &EntranceDelayPanel::refresh);
		// The case duration sets the occurrences of a service, and the timetable sets the stations where a train can be held.
		m_changedConnection = connect(m_document, &SceneDocument::changed, this, [this](SceneDocument::ChangeKinds kinds) {
			if (kinds & (SceneDocument::CaseSettings | SceneDocument::Infrastructure | SceneDocument::Services))
				refresh();
		});
	}
	refresh();
}

void EntranceDelayPanel::setPrompts(UserPrompts* prompts) {
	m_prompts = prompts ? prompts : &m_ownPrompts;
}

void EntranceDelayPanel::setRunSelection(RunSelection* selection) {
	m_runSelection = selection ? selection : &m_ownRunSelection;
}

void EntranceDelayPanel::setOccurrenceDuration(std::function<double()> duration) {
	m_occurrenceDuration = std::move(duration);
}

bool EntranceDelayPanel::hasScene() const {
	return m_document && m_document->isLoaded();
}

bool EntranceDelayPanel::canEdit() const {
	return hasScene() && m_document->isEditable();
}

double EntranceDelayPanel::occurrenceDuration() const {
	return m_occurrenceDuration ? m_occurrenceDuration() : m_document->model().settings.durationSeconds;
}

const SceneScenario* EntranceDelayPanel::scenario() const {
	return hasScene() ? m_runSelection->selectedScenario(m_document->model()) : nullptr;
}

SceneScenario* EntranceDelayPanel::scenarioForEdit() {
	return canEdit() ? m_runSelection->selectedScenario(m_document->mutableModel()) : nullptr;
}

const SceneEntranceDelay* EntranceDelayPanel::shownDelay() const {
	const SceneScenario* selected = scenario();
	const int row = m_listWidget->currentRow();
	return selected && row >= 0 && row < static_cast<int>(selected->entranceDelays.size())
		? &selected->entranceDelays[static_cast<std::size_t>(row)]
		: nullptr;
}

SceneEntranceDelay* EntranceDelayPanel::selectedEntranceDelay() {
	SceneScenario* selected = scenarioForEdit();
	const int row = m_listWidget->currentRow();
	return selected && row >= 0 && row < static_cast<int>(selected->entranceDelays.size())
		? &selected->entranceDelays[static_cast<std::size_t>(row)]
		: nullptr;
}

void EntranceDelayPanel::markEdited(const QString& label) {
	if (const SceneScenario* selected = scenario())
		m_runSelection->markScenarioModified(selected->id);
	m_document->markChanged(SceneDocument::Scenarios, label);
}

void EntranceDelayPanel::refresh() {
	const bool loaded = hasScene();
	const SceneScenario* selected = scenario();
	{
		const int previousRow = m_listWidget->currentRow();
		const QSignalBlocker blocker(m_listWidget);
		m_listWidget->clear();
		if (loaded && selected) {
			const SceneModel& model = m_document->model();
			for (const SceneEntranceDelay& delay : selected->entranceDelays) {
				QString label = QString("%1 / #%2 @ %3: %4 s")
									.arg(QString::fromStdString(delay.serviceId.empty() ? std::string("(empty)") : delay.serviceId))
									.arg(delay.occurrence)
									.arg(QString::fromStdString(delay.stationId.empty() ? std::string("(empty)") : delay.stationId))
									.arg(QString::number(delay.delaySeconds, 'g', 12));
				const auto service = std::find_if(model.services.begin(), model.services.end(),
					[&delay](const SceneService& candidate) { return candidate.id == delay.serviceId; });
				if (service != model.services.end()) {
					const std::string code = sceneServiceOccurrenceOperatingCode(*service, delay.occurrence);
					label += QString(" | code %1").arg(code.empty() ? QStringLiteral("(unavailable)") : QString::fromStdString(code));
				}
				m_listWidget->addItem(label);
			}
		}
		const int rowCount = m_listWidget->count();
		const int rowToSelect = rowCount <= 0 ? -1 : (previousRow < 0 ? 0 : std::min(previousRow, rowCount - 1));
		m_listWidget->setCurrentRow(rowToSelect);
		m_listWidget->setEnabled(canEdit());
	}
	m_addButton->setEnabled(canEdit() && selected != nullptr);
	updateDetail();
}

void EntranceDelayPanel::updateDetail() {
	const SceneEntranceDelay* delay = shownDelay();
	const bool enabled = canEdit() && delay != nullptr;
	const SceneModel noScene;
	const SceneModel& model = m_document ? m_document->model() : noScene;
	const SceneService* service = nullptr;
	if (delay) {
		for (const SceneService& candidate : model.services) {
			if (candidate.id == delay->serviceId) {
				service = &candidate;
				break;
			}
		}
	}

	{
		const QSignalBlocker blocker(m_serviceCombo);
		m_serviceCombo->clear();
		for (const SceneService& candidate : model.services)
			m_serviceCombo->addItem(ServicePanel::generatedServiceLabel(candidate, 1), QString::fromStdString(candidate.id));
		if (delay) {
			int index = m_serviceCombo->findData(QString::fromStdString(delay->serviceId));
			if (index < 0) {
				const QString invalid = delay->serviceId.empty() ? QStringLiteral("Invalid: (empty)")
																 : QString("Invalid: %1").arg(QString::fromStdString(delay->serviceId));
				m_serviceCombo->addItem(invalid, QString::fromStdString(delay->serviceId));
				index = m_serviceCombo->count() - 1;
			}
			m_serviceCombo->setCurrentIndex(index);
		}
		m_serviceCombo->setEnabled(enabled);
	}

	const int occurrenceCount = service ? std::max(1, sceneServiceOccurrenceCount(*service, occurrenceDuration())) : 1;
	{
		const QSignalBlocker blocker(m_occurrenceEdit);
		const int currentOccurrence = delay ? delay->occurrence : 1;
		m_occurrenceEdit->setRange(std::min(1, currentOccurrence), std::max(occurrenceCount, currentOccurrence));
		m_occurrenceEdit->setValue(currentOccurrence);
		m_occurrenceEdit->setEnabled(enabled);
	}
	QString emptyState;
	if (canEdit() && scenario() && !delay) {
		emptyState = m_listWidget->count() == 0 ? QStringLiteral("This scenario has no entrance delays. Use Add.")
												: QStringLiteral("Select an entrance delay or use Add.");
	}
	DialogLayout::setHelp(m_emptyStateLabel, emptyState);
	if (!delay) {
		m_occurrenceContextLabel->clear();
	} else {
		QString context = QString("Valid occurrence range: 1..%1").arg(occurrenceCount);
		if (service)
			context += QStringLiteral(" | ") + ServicePanel::generatedServiceLabel(*service, delay->occurrence);
		else
			context += QStringLiteral(" | Generated operating code: (unavailable)");
		m_occurrenceContextLabel->setText(context);
	}

	{
		const QSignalBlocker blocker(m_stationCombo);
		m_stationCombo->clear();
		std::set<std::string> eligibleStations;
		if (service) {
			for (const SceneStop& stop : service->stops) {
				if (eligibleStations.insert(stop.stationId).second && stop.hasPlannedDeparture)
					m_stationCombo->addItem(QString::fromStdString(stop.stationId), QString::fromStdString(stop.stationId));
			}
		}
		if (delay) {
			int index = m_stationCombo->findData(QString::fromStdString(delay->stationId));
			if (index < 0) {
				const QString invalid = delay->stationId.empty() ? QStringLiteral("Invalid: (empty)")
																 : QString("Invalid: %1").arg(QString::fromStdString(delay->stationId));
				m_stationCombo->addItem(invalid, QString::fromStdString(delay->stationId));
				index = m_stationCombo->count() - 1;
			}
			m_stationCombo->setCurrentIndex(index);
		}
		m_stationCombo->setEnabled(enabled);
	}

	{
		const QSignalBlocker blocker(m_secondsEdit);
		m_secondsEdit->setValue(delay ? delay->delaySeconds : 0.0);
		m_secondsEdit->setEnabled(enabled);
	}
	m_duplicateButton->setEnabled(enabled);
	m_deleteButton->setEnabled(enabled);
}

void EntranceDelayPanel::addEntranceDelay() {
	SceneScenario* selected = scenarioForEdit();
	if (!selected)
		return;
	const SceneModel& model = m_document->model();
	SceneEntranceDelay delay;
	for (const SceneService& service : model.services) {
		if (const SceneStop* stop = firstDepartureStop(service)) {
			delay.serviceId = service.id;
			delay.stationId = stop->stationId;
		}
		if (!delay.serviceId.empty())
			break;
	}
	if (delay.serviceId.empty() && !model.services.empty())
		delay.serviceId = model.services.front().id;
	selected->entranceDelays.push_back(std::move(delay));

	markEdited("Add entrance delay");
	refresh();
	emit changed();

	m_listWidget->setCurrentRow(static_cast<int>(selected->entranceDelays.size()) - 1);
}

void EntranceDelayPanel::duplicateEntranceDelay() {
	SceneScenario* selected = scenarioForEdit();
	const int row = m_listWidget->currentRow();
	if (!selected || row < 0 || row >= static_cast<int>(selected->entranceDelays.size()))
		return;
	selected->entranceDelays.insert(selected->entranceDelays.begin() + row + 1, selected->entranceDelays[static_cast<std::size_t>(row)]);

	markEdited("Duplicate entrance delay");
	refresh();
	emit changed();

	m_listWidget->setCurrentRow(row + 1);
}

void EntranceDelayPanel::deleteEntranceDelay() {
	SceneScenario* selected = scenarioForEdit();
	const int row = m_listWidget->currentRow();
	if (!selected || row < 0 || row >= static_cast<int>(selected->entranceDelays.size()))
		return;
	const QString description = QString::fromStdString(selected->entranceDelays[static_cast<std::size_t>(row)].serviceId);
	if (!m_prompts->confirm(this, "Delete Entrance Delay", QString("Delete entrance delay for service '%1'?").arg(description)))
		return;
	selected->entranceDelays.erase(selected->entranceDelays.begin() + row);

	markEdited("Delete entrance delay");
	refresh();
	emit changed();
}

void EntranceDelayPanel::commitService(int index) {
	if (index < 0)
		return;
	SceneEntranceDelay* delay = selectedEntranceDelay();
	if (!delay)
		return;
	const std::string serviceId = m_serviceCombo->itemData(index).toString().toStdString();
	if (serviceId == delay->serviceId)
		return;
	delay->serviceId = serviceId;
	delay->occurrence = 1;
	delay->stationId.clear();
	for (const SceneService& service : m_document->model().services) {
		if (service.id != serviceId)
			continue;
		if (const SceneStop* stop = firstDepartureStop(service))
			delay->stationId = stop->stationId;
		break;
	}
	markEdited("Change entrance delay service");
	refresh();
	emit changed();
}

void EntranceDelayPanel::commitOccurrence() {
	SceneEntranceDelay* delay = selectedEntranceDelay();
	if (!delay)
		return;
	const int occurrence = m_occurrenceEdit->value();
	if (delay->occurrence == occurrence)
		return;
	delay->occurrence = occurrence;
	markEdited("Change entrance delay occurrence");
	refresh();
	emit changed();
}

void EntranceDelayPanel::commitStation(int index) {
	if (index < 0)
		return;
	SceneEntranceDelay* delay = selectedEntranceDelay();
	if (!delay)
		return;
	const std::string stationId = m_stationCombo->itemData(index).toString().toStdString();
	if (delay->stationId == stationId)
		return;
	delay->stationId = stationId;
	markEdited("Change entrance delay station");
	refresh();
	emit changed();
}

void EntranceDelayPanel::commitSeconds() {
	SceneEntranceDelay* delay = selectedEntranceDelay();
	if (!delay)
		return;
	const double seconds = m_secondsEdit->value();
	if (delay->delaySeconds == seconds)
		return;
	delay->delaySeconds = seconds;
	markEdited("Change entrance delay");
	refresh();
	emit changed();
}

void EntranceDelayPanel::commitPendingEditors() {
	if (!canEdit())
		return;
	if (hasEditorFocus(m_occurrenceEdit)) {
		m_occurrenceEdit->interpretText();
		commitOccurrence();
	}
	if (hasEditorFocus(m_secondsEdit)) {
		m_secondsEdit->interpretText();
		commitSeconds();
	}
}
