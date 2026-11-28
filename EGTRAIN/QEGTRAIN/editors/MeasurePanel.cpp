#include "editors/MeasurePanel.h"

#include "editors/EditorHelpers.h"
#include "scene/SceneIds.h"
#include "widgets/ChoiceComboBox.h"
#include "widgets/CompactDoubleSpinBox.h"
#include "widgets/EditorLayout.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
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

// The service of the scene with the id, or null.
const SceneService* findService(const SceneModel& model, const std::string& id) {
	const auto found = std::find_if(model.services.begin(), model.services.end(), [&id](const SceneService& service) { return service.id == id; });
	return found != model.services.end() ? &*found : nullptr;
}

// True when the measure names one occurrence; as for incidents, an occurrence other than 1 counts as named.
bool namesOccurrence(const SceneMeasure& measure) {
	return measure.hasOccurrence || measure.occurrence != 1;
}

// The row of the list: the train or service, the time and the station, such as "IC 100 #2 at 600 s from B" or
// "s1 (every occurrence) at 0 s, complete".
QString measureText(const SceneModel& model, const SceneMeasure& measure) {
	QString train = measure.serviceId.empty() ? QStringLiteral("(none)") : QString::fromStdString(measure.serviceId);
	if (namesOccurrence(measure)) {
		const SceneService* service = findService(model, measure.serviceId);
		const std::string code = service ? sceneServiceOccurrenceOperatingCode(*service, measure.occurrence) : std::string();
		train = code.empty() ? QString("%1 #%2").arg(train).arg(measure.occurrence) : QString::fromStdString(code);
	} else {
		train += QStringLiteral(" (every occurrence)");
	}
	const QString where = measure.fromStationId.empty() ? QStringLiteral(", complete")
														: QString(" from %1").arg(QString::fromStdString(measure.fromStationId));
	return QString("%1: cancel %2 at %3 s%4")
		.arg(QString::fromStdString(measure.id), train, QString::number(measure.timeSeconds, 'g', 12), where);
}

} // namespace

MeasurePanel::MeasurePanel(QWidget* parent) : SceneEditorPanel(parent) {
	setObjectName("measurePanel");
	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel("Measures", this));
	// The list shows about three rows above its buttons in one row, so that the fields below keep the height of a short dock.
	m_listWidget = new QListWidget(this);
	m_listWidget->setObjectName("measureListWidget");
	m_listWidget->setAccessibleName("Measure library");
	m_listWidget->setFixedHeight(m_listWidget->fontMetrics().lineSpacing() * 3 + 2 * m_listWidget->frameWidth());
	layout->addWidget(m_listWidget);
	QGridLayout* buttonLayout = new QGridLayout();
	m_addButton = new QPushButton("Add", this);
	m_addButton->setObjectName("measureAddButton");
	m_duplicateButton = new QPushButton("Duplicate", this);
	m_duplicateButton->setObjectName("measureDuplicateButton");
	m_deleteButton = new QPushButton("Delete", this);
	m_deleteButton->setObjectName("measureDeleteButton");
	buttonLayout->addWidget(m_addButton, 0, 0);
	buttonLayout->addWidget(m_duplicateButton, 0, 1);
	buttonLayout->addWidget(m_deleteButton, 0, 2);
	layout->addLayout(buttonLayout);
	m_emptyStateLabel = EditorLayout::emptyStateLabel(this);
	m_emptyStateLabel->setObjectName("measureEmptyStateLabel");
	layout->addWidget(m_emptyStateLabel);
	QFormLayout* detailLayout = new QFormLayout();
	m_serviceCombo = new ChoiceComboBox(this);
	m_serviceCombo->setObjectName("measureServiceCombo");
	detailLayout->addRow("Cancel service", m_serviceCombo);
	m_everyOccurrenceCheck = new QCheckBox("Every occurrence", this);
	m_everyOccurrenceCheck->setObjectName("measureEveryOccurrenceCheck");
	detailLayout->addRow(QString(), m_everyOccurrenceCheck);
	m_occurrenceEdit = new QSpinBox(this);
	m_occurrenceEdit->setObjectName("measureOccurrenceSpin");
	m_occurrenceEdit->setRange(1, 1);
	m_occurrenceEdit->setKeyboardTracking(false);
	detailLayout->addRow("Occurrence", m_occurrenceEdit);
	m_secondsEdit = new CompactDoubleSpinBox(this);
	m_secondsEdit->setObjectName("measureSecondsEdit");
	m_secondsEdit->setRange(0.0, std::numeric_limits<double>::max());
	m_secondsEdit->setDecimals(std::numeric_limits<double>::max_digits10);
	m_secondsEdit->setKeyboardTracking(false);
	m_secondsEdit->setToolTip("When the measure takes effect, in seconds from the start of the run");
	detailLayout->addRow("From time (s)", m_secondsEdit);
	m_stationCombo = new ChoiceComboBox(this);
	m_stationCombo->setObjectName("measureStationCombo");
	m_stationCombo->setToolTip("Complete: the train does not enter, or ends its run at its next stop if it runs already. A station: the "
							   "train ends its run there and does not call at its later stops");
	detailLayout->addRow("From station", m_stationCombo);
	layout->addLayout(detailLayout);
	layout->addStretch();

	connect(m_listWidget, &QListWidget::currentRowChanged, this, [this](int) { updateDetail(); });
	connect(m_addButton, &QPushButton::clicked, this, &MeasurePanel::addMeasure);
	connect(m_duplicateButton, &QPushButton::clicked, this, &MeasurePanel::duplicateMeasure);
	connect(m_deleteButton, &QPushButton::clicked, this, &MeasurePanel::deleteMeasure);
	connect(m_serviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeasurePanel::commitService);
	connect(m_everyOccurrenceCheck, &QCheckBox::toggled, this, &MeasurePanel::commitEveryOccurrence);
	connect(m_occurrenceEdit, &QAbstractSpinBox::editingFinished, this, &MeasurePanel::commitOccurrence);
	connect(m_secondsEdit, &QAbstractSpinBox::editingFinished, this, &MeasurePanel::commitSeconds);
	connect(m_stationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeasurePanel::commitStation);
	MeasurePanel::refresh();
}

void MeasurePanel::documentChanged(SceneDocument::ChangeKinds kinds) {
	// The case duration sets the occurrences of a service, and the timetable the stations at which it calls. The panel edits only
	// the scenarios, so these are edits of other panels.
	if (kinds & (SceneDocument::CaseSettings | SceneDocument::Infrastructure | SceneDocument::Services)) {
		takePendingText();
		refresh();
	}
}

const SceneScenario* MeasurePanel::scenario() const {
	return hasScene() ? m_runSelection->selectedScenario(m_document->model()) : nullptr;
}

const SceneMeasure* MeasurePanel::shownMeasure() const {
	const SceneScenario* selected = scenario();
	const int row = m_listWidget->currentRow();
	return selected && row >= 0 && row < static_cast<int>(selected->measures.size()) ? &selected->measures[static_cast<std::size_t>(row)]
																					 : nullptr;
}

const SceneMeasure* MeasurePanel::selectedMeasure() const {
	return canEdit() ? shownMeasure() : nullptr;
}

bool MeasurePanel::editMeasures(const QString& label, const std::function<void(std::vector<SceneMeasure>&)>& mutate) {
	return m_document->edit(SceneDocument::Scenarios, label, [this, &mutate](SceneModel& model) {
		SceneScenario* selected = m_runSelection->selectedScenario(model);
		mutate(selected->measures);
		m_runSelection->markScenarioModified(selected->id);
	});
}

bool MeasurePanel::editSelected(const QString& label, const std::function<void(SceneMeasure&)>& mutate) {
	const auto row = static_cast<std::size_t>(m_listWidget->currentRow());
	return editMeasures(label, [row, &mutate](std::vector<SceneMeasure>& measures) { mutate(measures[row]); });
}

void MeasurePanel::showEdited(int row) {
	refresh();
	emit changed();
	m_listWidget->setCurrentRow(row);
}

void MeasurePanel::refresh() {
	const SceneScenario* selected = scenario();
	{
		const int previousRow = m_listWidget->currentRow();
		const QSignalBlocker blocker(m_listWidget);
		m_listWidget->clear();
		if (selected)
			for (const SceneMeasure& measure : selected->measures)
				m_listWidget->addItem(measureText(m_document->model(), measure));
		restoreListSelection(m_listWidget, QString(), previousRow);
		m_listWidget->setEnabled(canEdit());
	}
	m_addButton->setEnabled(canEdit() && selected != nullptr);
	updateDetail();
}

void MeasurePanel::updateDetail() {
	const SceneMeasure* measure = shownMeasure();
	const bool enabled = canEdit() && measure != nullptr;
	const SceneModel noScene;
	const SceneModel& model = m_document ? m_document->model() : noScene;
	const SceneService* service = measure ? findService(model, measure->serviceId) : nullptr;

	{
		const QSignalBlocker blocker(m_serviceCombo);
		m_serviceCombo->clear();
		for (const SceneService& candidate : model.services)
			m_serviceCombo->addItem(generatedServiceLabel(candidate, 1), QString::fromStdString(candidate.id));
		if (measure) {
			int index = m_serviceCombo->findData(QString::fromStdString(measure->serviceId));
			if (index < 0) {
				const QString invalid = measure->serviceId.empty() ? QStringLiteral("(none)")
																   : QString("%1 (missing)").arg(QString::fromStdString(measure->serviceId));
				m_serviceCombo->addItem(invalid, QString::fromStdString(measure->serviceId));
				index = m_serviceCombo->count() - 1;
			}
			m_serviceCombo->setCurrentIndex(index);
		}
		m_serviceCombo->setEnabled(enabled);
	}
	{
		const QSignalBlocker blocker(m_everyOccurrenceCheck);
		m_everyOccurrenceCheck->setChecked(measure && !namesOccurrence(*measure));
		m_everyOccurrenceCheck->setEnabled(enabled);
	}
	const int occurrenceCount = service ? std::max(1, sceneServiceOccurrenceCount(*service, occurrenceDuration())) : 1;
	{
		const QSignalBlocker blocker(m_occurrenceEdit);
		const int currentOccurrence = measure ? measure->occurrence : 1;
		m_occurrenceEdit->setRange(std::min(1, currentOccurrence), std::max(occurrenceCount, currentOccurrence));
		m_occurrenceEdit->setSuffix(measure ? QString(" of %1").arg(occurrenceCount) : QString());
		m_occurrenceEdit->setValue(currentOccurrence);
		m_occurrenceEdit->setEnabled(enabled && namesOccurrence(*measure));
	}
	{
		const QSignalBlocker blocker(m_secondsEdit);
		m_secondsEdit->setValue(measure ? measure->timeSeconds : 0.0);
		m_secondsEdit->setEnabled(enabled);
	}
	{
		const QSignalBlocker blocker(m_stationCombo);
		m_stationCombo->clear();
		m_stationCombo->addItem(QStringLiteral("(complete)"), QString());
		std::set<std::string> listed;
		if (service)
			for (const SceneStop& stop : service->stops)
				if (listed.insert(stop.stationId).second)
					m_stationCombo->addItem(QString::fromStdString(stop.stationId), QString::fromStdString(stop.stationId));
		if (measure) {
			int index = m_stationCombo->findData(QString::fromStdString(measure->fromStationId));
			if (index < 0) {
				m_stationCombo->addItem(QString("%1 (not a stop)").arg(QString::fromStdString(measure->fromStationId)),
					QString::fromStdString(measure->fromStationId));
				index = m_stationCombo->count() - 1;
			}
			m_stationCombo->setCurrentIndex(index);
		}
		m_stationCombo->setEnabled(enabled);
	}
	QString emptyState;
	if (canEdit() && scenario() && !measure)
		emptyState = m_listWidget->count() == 0 ? QStringLiteral("This scenario has no measures. Use Add.")
												: QStringLiteral("Select a measure or use Add.");
	DialogLayout::setHelp(m_emptyStateLabel, emptyState);
	m_duplicateButton->setEnabled(enabled);
	m_deleteButton->setEnabled(enabled);
}

void MeasurePanel::addMeasure() {
	if (!canEdit())
		return;
	const SceneModel& model = m_document->model();
	SceneMeasure measure;
	measure.id = sceneUniqueMeasureId(model, "cancel");
	if (!model.services.empty())
		measure.serviceId = model.services.front().id;
	if (!editMeasures("Add measure", [&measure](std::vector<SceneMeasure>& measures) { measures.push_back(measure); }))
		return;
	showEdited(static_cast<int>(scenario()->measures.size()) - 1);
}

void MeasurePanel::duplicateMeasure() {
	const SceneMeasure* selected = selectedMeasure();
	if (!selected)
		return;
	const int row = m_listWidget->currentRow();
	SceneMeasure duplicate = *selected;
	duplicate.id = sceneUniqueMeasureId(m_document->model(), selected->id + "_copy");
	if (!editMeasures("Duplicate measure",
			[row, &duplicate](std::vector<SceneMeasure>& measures) { measures.insert(measures.begin() + row + 1, duplicate); }))
		return;
	showEdited(row + 1);
}

void MeasurePanel::deleteMeasure() {
	const SceneMeasure* selected = selectedMeasure();
	if (!selected)
		return;
	const int row = m_listWidget->currentRow();
	const QString description = QString::fromStdString(selected->id);
	if (!editMeasures("Delete measure", [row](std::vector<SceneMeasure>& measures) { measures.erase(measures.begin() + row); }))
		return;
	refresh();
	emit changed();
	reportDeleted(QString("measure '%1'").arg(description));
}

void MeasurePanel::commitService(int index) {
	const SceneMeasure* measure = selectedMeasure();
	if (index < 0 || !measure)
		return;
	const std::string serviceId = m_serviceCombo->itemData(index).toString().toStdString();
	if (serviceId == measure->serviceId)
		return;
	if (!editSelected("Change measure service", [&serviceId](SceneMeasure& edited) {
			edited.serviceId = serviceId;
			edited.hasOccurrence = false;
			edited.occurrence = 1;
			edited.fromStationId.clear();
		}))
		return;
	showEdited(m_listWidget->currentRow());
}

void MeasurePanel::commitEveryOccurrence(bool every) {
	const SceneMeasure* measure = selectedMeasure();
	if (!measure || namesOccurrence(*measure) == !every)
		return;
	if (!editSelected("Change measure occurrence", [every](SceneMeasure& edited) {
			edited.hasOccurrence = !every;
			edited.occurrence = 1;
		}))
		return;
	showEdited(m_listWidget->currentRow());
}

void MeasurePanel::commitOccurrence() {
	const SceneMeasure* measure = selectedMeasure();
	if (!measure || !namesOccurrence(*measure))
		return;
	const int occurrence = m_occurrenceEdit->value();
	if (measure->occurrence == occurrence)
		return;
	if (!editSelected("Change measure occurrence", [occurrence](SceneMeasure& edited) { edited.occurrence = occurrence; }))
		return;
	showEdited(m_listWidget->currentRow());
}

void MeasurePanel::commitSeconds() {
	const SceneMeasure* measure = selectedMeasure();
	if (!measure)
		return;
	const double seconds = m_secondsEdit->value();
	if (measure->timeSeconds == seconds)
		return;
	if (!editSelected("Change measure time", [seconds](SceneMeasure& edited) { edited.timeSeconds = seconds; }))
		return;
	showEdited(m_listWidget->currentRow());
}

void MeasurePanel::commitStation(int index) {
	const SceneMeasure* measure = selectedMeasure();
	if (index < 0 || !measure)
		return;
	const std::string stationId = m_stationCombo->itemData(index).toString().toStdString();
	if (measure->fromStationId == stationId)
		return;
	if (!editSelected("Change measure station", [&stationId](SceneMeasure& edited) { edited.fromStationId = stationId; }))
		return;
	showEdited(m_listWidget->currentRow());
}

void MeasurePanel::commitPendingEditors() {
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
