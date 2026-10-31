#include "editors/PassengerPanel.h"

#include "editors/CountText.h"
#include "editors/EditorHelpers.h"
#include "editors/EditorRules.h"
#include "scene/SceneDiagnostic.h"
#include "scene/SceneIds.h"
#include "scene/SceneImporter.h"
#include "widgets/ChoiceComboBox.h"
#include "widgets/CompactDoubleSpinBox.h"
#include "widgets/DialogLayout.h"
#include "widgets/DisplayPath.h"
#include "widgets/EditorLayout.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <set>
#include <string>
#include <utility>

namespace {

// Adds the current value of a combo that is not among its choices as an entry marked with `state`, such as "missing",
// with the reason as its tooltip, and returns its index.
int addInvalidEntry(QComboBox* combo, const std::string& current, const QString& reason,
	const QString& state = QStringLiteral("missing")) {
	combo->addItem(current.empty() ? QStringLiteral("(none)")
								   : QStringLiteral("%1 (%2)").arg(QString::fromStdString(current), state),
		QString::fromStdString(current));
	const int index = combo->count() - 1;
	combo->setItemData(index, reason, Qt::ToolTipRole);
	return index;
}

bool hasStation(const SceneModel& sceneModel, const std::string& stationId) {
	return std::any_of(sceneModel.stations.begin(), sceneModel.stations.end(),
		[&stationId](const SceneStation& station) { return station.id == stationId; });
}

// Why a station that is not among the choices of a combo is invalid: the scene has no such station, or the service of
// the leg does not stop there.
QString invalidStationReason(const SceneModel& sceneModel, const std::string& stationId) {
	if (stationId.empty())
		return QStringLiteral("No station is set.");
	const QString id = QString::fromStdString(stationId);
	return hasStation(sceneModel, stationId) ? QStringLiteral("Station %1 is not a stop of the service of this leg.").arg(id)
											 : QStringLiteral("Station %1 is not in this case.").arg(id);
}

// The stations to choose from. `allowed` limits them to a set of station ids; null allows all. A current id that is not
// among them stays selected as an entry marked invalid.
void populateStationCombo(QComboBox* combo, const SceneModel& sceneModel, const std::string& current,
	const std::set<std::string>* allowed, bool hasSelection) {
	combo->clear();
	if (!hasSelection) {
		combo->addItem("Select a journey or leg", QString());
		return;
	}
	for (const auto& station : sceneModel.stations) {
		if (allowed && allowed->find(station.id) == allowed->end())
			continue;
		const QString label = station.name.empty()
			? QString::fromStdString(station.id)
			: QStringLiteral("%1 (%2)").arg(QString::fromStdString(station.name), QString::fromStdString(station.id));
		combo->addItem(label, QString::fromStdString(station.id));
	}
	int selected = combo->findData(QString::fromStdString(current));
	if (selected < 0)
		selected = addInvalidEntry(combo, current, invalidStationReason(sceneModel, current),
			hasStation(sceneModel, current) ? QStringLiteral("not a stop") : QStringLiteral("missing"));
	combo->setCurrentIndex(selected);
}

void populateServiceCombo(QComboBox* combo, const SceneModel& sceneModel, const std::string& current,
	bool hasSelection) {
	combo->clear();
	if (!hasSelection) {
		combo->addItem("Select a leg", QString());
		return;
	}
	for (const auto& service : sceneModel.services)
		combo->addItem(QString::fromStdString(service.id), QString::fromStdString(service.id));
	int selected = combo->findData(QString::fromStdString(current));
	if (selected < 0) {
		selected = addInvalidEntry(combo, current,
			current.empty() ? QStringLiteral("No service is set.")
							: QStringLiteral("Service %1 is not in this case.").arg(QString::fromStdString(current)));
	}
	combo->setCurrentIndex(selected);
}

// The most lines of one callout. The rest is counted, and the tooltip of the callout lists all of them.
constexpr int MaxDiagnosticLines = 5;

// The rows of the import results that the table shows without scrolling.
constexpr int MinImportResultRows = 8;

QString cappedLines(const QStringList& lines) {
	if (lines.size() <= MaxDiagnosticLines)
		return lines.join(QLatin1Char('\n'));
	return lines.mid(0, MaxDiagnosticLines).join(QLatin1Char('\n'))
		+ QStringLiteral("\nand %1 more").arg(lines.size() - MaxDiagnosticLines);
}

// True when the diagnostic is about the passenger, one of its journeys or one of its legs: its path starts with the
// path of the passenger, or it names one of their ids as an item of the matching kind.
bool belongsToPassenger(const SceneDiagnostic& diagnostic, const QString& passengerPrefix, const std::string& passengerId,
	const std::set<std::string>& journeyIds, const std::set<std::string>& legIds) {
	if (QString::fromStdString(diagnostic.path).startsWith(passengerPrefix))
		return true;
	if (diagnostic.itemId.empty())
		return false;
	if (diagnostic.itemType == "passenger")
		return diagnostic.itemId == passengerId;
	if (diagnostic.itemType == "journey")
		return journeyIds.count(diagnostic.itemId) > 0;
	if (diagnostic.itemType == "leg")
		return legIds.count(diagnostic.itemId) > 0;
	return false;
}

} // namespace

PassengerPanel::PassengerPanel(QWidget* parent) : SceneEditorPanel(parent) {
	setObjectName("passengerPanel");
	QHBoxLayout* passengerLayout = new QHBoxLayout(this);
	QWidget* passengerListPane = new QWidget(this);
	QVBoxLayout* passengerListLayout = new QVBoxLayout(passengerListPane);
	passengerListLayout->addWidget(new QLabel("Passengers", passengerListPane));
	m_passengerList = new QListWidget(passengerListPane);
	m_passengerList->setObjectName("passengerListWidget");
	m_passengerList->setAccessibleName("Passengers");
	m_passengerList->setMaximumHeight(fontMetrics().lineSpacing() * 12);
	passengerListLayout->addWidget(m_passengerList, 1);
	QHBoxLayout* passengerButtonLayout = new QHBoxLayout();
	m_addPassengerButton = new QPushButton("Add", passengerListPane);
	m_addPassengerButton->setObjectName("passengerAddButton");
	m_deletePassengerButton = new QPushButton("Delete", passengerListPane);
	m_deletePassengerButton->setObjectName("passengerDeleteButton");
	passengerButtonLayout->addWidget(m_addPassengerButton);
	passengerButtonLayout->addWidget(m_deletePassengerButton);
	passengerListLayout->addLayout(passengerButtonLayout);
	m_importButton = new QPushButton("Import...", passengerListPane);
	m_importButton->setObjectName("passengerImportButton");
	m_importButton->setToolTip("Add passengers from a legacy case's Passengers folder.");
	passengerListLayout->addWidget(m_importButton);
	passengerListLayout->addStretch();
	passengerListPane->setMaximumWidth(passengerListPane->fontMetrics().averageCharWidth() * 30);
	passengerLayout->addWidget(passengerListPane);

	QWidget* passengerDetailPane = new QWidget(this);
	QVBoxLayout* passengerDetailLayout = new QVBoxLayout(passengerDetailPane);
	QFormLayout* passengerForm = new QFormLayout();
	m_passengerIdEdit = new QLineEdit(passengerDetailPane);
	m_passengerIdEdit->setObjectName("passengerIdEdit");
	passengerForm->addRow("Passenger ID", m_passengerIdEdit);
	passengerDetailLayout->addLayout(passengerForm);
	m_diagnosticLabel = new QLabel(passengerDetailPane);
	m_diagnosticLabel->setObjectName("passengerDiagnosticLabel");
	passengerDetailLayout->addWidget(m_diagnosticLabel);
	m_warningLabel = new QLabel(passengerDetailPane);
	m_warningLabel->setObjectName("passengerWarningLabel");
	passengerDetailLayout->addWidget(m_warningLabel);

	m_tabs = new QTabWidget(passengerDetailPane);
	m_tabs->setObjectName("passengerEditorTabs");

	QWidget* journeysPane = new QWidget(m_tabs);
	QVBoxLayout* journeysLayout = new QVBoxLayout(journeysPane);
	QWidget* journeyListPane = new QWidget(journeysPane);
	QVBoxLayout* journeyListLayout = new QVBoxLayout(journeyListPane);
	journeyListLayout->addWidget(new QLabel("Journeys", journeyListPane));
	m_journeyList = new QListWidget(journeyListPane);
	m_journeyList->setObjectName("passengerJourneyListWidget");
	journeyListLayout->addWidget(m_journeyList, 1);
	QHBoxLayout* journeyButtons = new QHBoxLayout();
	m_addJourneyButton = new QPushButton("Add", journeyListPane);
	m_addJourneyButton->setObjectName("passengerJourneyAddButton");
	m_deleteJourneyButton = new QPushButton("Delete", journeyListPane);
	m_deleteJourneyButton->setObjectName("passengerJourneyDeleteButton");
	journeyButtons->addWidget(m_addJourneyButton);
	journeyButtons->addWidget(m_deleteJourneyButton);
	journeyListLayout->addLayout(journeyButtons);
	m_journeyList->setMaximumHeight(fontMetrics().lineSpacing() * 6);
	journeysLayout->addWidget(journeyListPane);

	QWidget* journeyDetailPane = new QWidget(journeysPane);
	QFormLayout* journeyForm = new QFormLayout(journeyDetailPane);
	journeyForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_journeyIdEdit = new QLineEdit(journeyDetailPane);
	m_journeyIdEdit->setObjectName("passengerJourneyIdEdit");
	journeyForm->addRow("Journey ID", m_journeyIdEdit);
	m_journeyActivityEdit = new QLineEdit(journeyDetailPane);
	m_journeyActivityEdit->setObjectName("passengerJourneyActivityEdit");
	journeyForm->addRow("Activity", m_journeyActivityEdit);
	m_journeyOriginCombo = new ChoiceComboBox(journeyDetailPane);
	m_journeyOriginCombo->setObjectName("passengerJourneyOriginCombo");
	journeyForm->addRow("Origin station", m_journeyOriginCombo);
	m_journeyDestinationCombo = new ChoiceComboBox(journeyDetailPane);
	m_journeyDestinationCombo->setObjectName("passengerJourneyDestinationCombo");
	journeyForm->addRow("Destination station", m_journeyDestinationCombo);
	const std::array<std::pair<const char*, const char*>, 4> windowFields = {
		std::make_pair("Departure start (s)", "passengerDepartureStartSecondsEdit"),
		std::make_pair("Departure end (s)", "passengerDepartureEndSecondsEdit"),
		std::make_pair("Arrival start (s)", "passengerArrivalStartSecondsEdit"),
		std::make_pair("Arrival end (s)", "passengerArrivalEndSecondsEdit")};
	for (std::size_t index = 0; index < windowFields.size(); ++index) {
		auto* edit = new CompactDoubleSpinBox(journeyDetailPane);
		edit->setObjectName(windowFields[index].second);
		edit->setRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
		edit->setDecimals(std::numeric_limits<double>::max_digits10);
		edit->setSingleStep(1.0);
		edit->setKeyboardTracking(false);
		m_journeyWindowEdits[index] = edit;
		journeyForm->addRow(windowFields[index].first, edit);
	}
	journeysLayout->addWidget(journeyDetailPane, 1);
	m_tabs->addTab(journeysPane, "Journeys");

	QWidget* legsPane = new QWidget(m_tabs);
	QVBoxLayout* legsLayout = new QVBoxLayout(legsPane);
	QWidget* legListPane = new QWidget(legsPane);
	QVBoxLayout* legListLayout = new QVBoxLayout(legListPane);
	legListLayout->addWidget(new QLabel("Legs", legListPane));
	m_legList = new QListWidget(legListPane);
	m_legList->setObjectName("passengerLegListWidget");
	legListLayout->addWidget(m_legList, 1);
	QGridLayout* legButtons = new QGridLayout();
	m_addLegButton = new QPushButton("Add", legListPane);
	m_addLegButton->setObjectName("passengerLegAddButton");
	m_deleteLegButton = new QPushButton("Delete", legListPane);
	m_deleteLegButton->setObjectName("passengerLegDeleteButton");
	m_moveLegUpButton = new QPushButton("Move Up", legListPane);
	m_moveLegUpButton->setObjectName("passengerLegMoveUpButton");
	m_moveLegDownButton = new QPushButton("Move Down", legListPane);
	m_moveLegDownButton->setObjectName("passengerLegMoveDownButton");
	legButtons->addWidget(m_addLegButton, 0, 0);
	legButtons->addWidget(m_deleteLegButton, 0, 1);
	legButtons->addWidget(m_moveLegUpButton, 1, 0);
	legButtons->addWidget(m_moveLegDownButton, 1, 1);
	legListLayout->addLayout(legButtons);
	m_legList->setMaximumHeight(fontMetrics().lineSpacing() * 6);
	legsLayout->addWidget(legListPane);

	QWidget* legDetailPane = new QWidget(legsPane);
	QFormLayout* legForm = new QFormLayout(legDetailPane);
	legForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_legIdEdit = new QLineEdit(legDetailPane);
	m_legIdEdit->setObjectName("passengerLegIdEdit");
	legForm->addRow("Leg ID", m_legIdEdit);
	m_legOriginCombo = new ChoiceComboBox(legDetailPane);
	m_legOriginCombo->setObjectName("passengerLegOriginCombo");
	legForm->addRow("Origin station", m_legOriginCombo);
	m_legDestinationCombo = new ChoiceComboBox(legDetailPane);
	m_legDestinationCombo->setObjectName("passengerLegDestinationCombo");
	legForm->addRow("Destination station", m_legDestinationCombo);
	m_legServiceCombo = new ChoiceComboBox(legDetailPane);
	m_legServiceCombo->setObjectName("passengerLegServiceCombo");
	legForm->addRow("Service", m_legServiceCombo);
	m_legOccurrenceEdit = new QSpinBox(legDetailPane);
	m_legOccurrenceEdit->setObjectName("passengerLegOccurrenceSpin");
	m_legOccurrenceEdit->setKeyboardTracking(false);
	legForm->addRow("Occurrence", m_legOccurrenceEdit);
	legsLayout->addWidget(legDetailPane, 1);
	m_tabs->addTab(legsPane, "Legs");
	passengerDetailLayout->addWidget(m_tabs, 1);

	m_importResultHeading = EditorLayout::sectionHeading("Import results", passengerDetailPane);
	m_importResultHeading->setObjectName("passengerImportResultHeading");
	passengerDetailLayout->addWidget(m_importResultHeading);
	m_importResultTable = new QTableWidget(0, 5, passengerDetailPane);
	m_importResultTable->setObjectName("passengerImportResultTable");
	m_importResultTable->setHorizontalHeaderLabels({"Source", "Row", "Passenger", "Status", "Detail"});
	m_importResultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_importResultTable->setTabKeyNavigation(false);
	m_importResultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_importResultTable->horizontalHeader()->setStretchLastSection(true);
	passengerDetailLayout->addWidget(m_importResultTable);
	m_importResultHeading->hide();
	m_importResultTable->hide();
	const auto updateImportVisibility = [this]() {
		const bool hasRows = m_importResultTable->rowCount() > 0;
		m_importResultTable->setMinimumHeight(m_importResultTable->horizontalHeader()->sizeHint().height()
			+ MinImportResultRows * m_importResultTable->verticalHeader()->defaultSectionSize()
			+ m_importResultTable->horizontalScrollBar()->sizeHint().height() + 2 * m_importResultTable->frameWidth());
		m_importResultHeading->setVisible(hasRows);
		m_importResultTable->setVisible(hasRows);
	};
	connect(m_importResultTable->model(), &QAbstractItemModel::rowsInserted, m_importResultTable, updateImportVisibility);
	connect(m_importResultTable->model(), &QAbstractItemModel::rowsRemoved, m_importResultTable, updateImportVisibility);
	passengerLayout->addWidget(passengerDetailPane, 2);

	connect(m_passengerList, &QListWidget::currentRowChanged, this, [this](int) { updatePassengerDetail(); });
	connect(m_passengerList, &QListWidget::itemSelectionChanged, this, [this] { updatePassengerDetail(); });
	connect(m_journeyList, &QListWidget::currentRowChanged, this, [this](int) { updateJourneyDetail(); });
	connect(m_legList, &QListWidget::currentRowChanged, this, [this](int) { updateLegDetail(); });
	connect(m_addPassengerButton, &QPushButton::clicked, this, &PassengerPanel::addPassenger);
	connect(m_deletePassengerButton, &QPushButton::clicked, this, &PassengerPanel::deletePassenger);
	connect(m_importButton, &QPushButton::clicked, this, &PassengerPanel::importPassengers);
	connect(m_addJourneyButton, &QPushButton::clicked, this, &PassengerPanel::addJourney);
	connect(m_deleteJourneyButton, &QPushButton::clicked, this, &PassengerPanel::deleteJourney);
	connect(m_addLegButton, &QPushButton::clicked, this, &PassengerPanel::addLeg);
	connect(m_deleteLegButton, &QPushButton::clicked, this, &PassengerPanel::deleteLeg);
	connect(m_moveLegUpButton, &QPushButton::clicked, this, [this]() { moveLeg(-1); });
	connect(m_moveLegDownButton, &QPushButton::clicked, this, [this]() { moveLeg(1); });
	connect(m_passengerIdEdit, &QLineEdit::editingFinished, this, &PassengerPanel::commitPassengerId);
	connect(m_journeyIdEdit, &QLineEdit::editingFinished, this, &PassengerPanel::commitJourneyId);
	connect(m_journeyActivityEdit, &QLineEdit::editingFinished, this, &PassengerPanel::commitJourneyActivity);
	connect(m_journeyOriginCombo, &QComboBox::currentTextChanged, this, [this](const QString&) { commitJourneyStation(true); });
	connect(m_journeyDestinationCombo, &QComboBox::currentTextChanged, this,
		[this](const QString&) { commitJourneyStation(false); });
	for (std::size_t index = 0; index < m_journeyWindowEdits.size(); ++index) {
		connect(m_journeyWindowEdits[index], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
			[this, index](double) { commitJourneyWindow(static_cast<int>(index)); });
	}
	connect(m_legIdEdit, &QLineEdit::editingFinished, this, &PassengerPanel::commitLegId);
	connect(m_legOriginCombo, &QComboBox::currentTextChanged, this, [this](const QString&) { commitLegStation(true); });
	connect(m_legDestinationCombo, &QComboBox::currentTextChanged, this, [this](const QString&) { commitLegStation(false); });
	connect(m_legServiceCombo, &QComboBox::currentTextChanged, this, [this](const QString&) { commitLegService(); });
	connect(m_legOccurrenceEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { commitLegOccurrence(); });
	PassengerPanel::refresh();
}

void PassengerPanel::documentChanged(SceneDocument::ChangeKinds kinds) {
	// The case duration sets the occurrences of a service, and journeys and legs name stations and services. The panel
	// edits only the passengers, so these are edits of other panels.
	if (kinds & (SceneDocument::CaseSettings | SceneDocument::Infrastructure | SceneDocument::Services)) {
		takePendingText();
		refresh();
	}
}

void PassengerPanel::setDiagnostics(const std::vector<SceneDiagnostic>* diagnostics) {
	m_diagnostics = diagnostics;
}

void PassengerPanel::setImportFolderChooser(std::function<QString()> chooser) {
	m_importFolderChooser = std::move(chooser);
}

void PassengerPanel::clearImportResult() {
	m_importResultTable->setRowCount(0);
}

int PassengerPanel::selectedPassengerRow() const {
	// Ctrl+click on the selected entry clears the selection and keeps the current row.
	const QListWidgetItem* current = m_passengerList->currentItem();
	return current && current->isSelected() ? m_passengerList->currentRow() : -1;
}

const ScenePassenger* PassengerPanel::selectedPassenger() const {
	if (!hasScene())
		return nullptr;
	const auto& passengers = m_document->model().passengers;
	const int row = selectedPassengerRow();
	return row >= 0 && row < static_cast<int>(passengers.size()) ? &passengers[static_cast<std::size_t>(row)] : nullptr;
}

const ScenePassengerJourney* PassengerPanel::selectedPassengerJourney() const {
	const ScenePassenger* passenger = selectedPassenger();
	const int row = m_journeyList->currentRow();
	return passenger && row >= 0 && row < static_cast<int>(passenger->journeys.size())
		? &passenger->journeys[static_cast<std::size_t>(row)]
		: nullptr;
}

const ScenePassengerLeg* PassengerPanel::selectedPassengerLeg() const {
	const ScenePassengerJourney* journey = selectedPassengerJourney();
	const int row = m_legList->currentRow();
	return journey && row >= 0 && row < static_cast<int>(journey->legs.size())
		? &journey->legs[static_cast<std::size_t>(row)]
		: nullptr;
}

bool PassengerPanel::editPassenger(const QString& label, const std::function<void(ScenePassenger&)>& mutate) {
	const auto row = static_cast<std::size_t>(selectedPassengerRow());
	return m_document->edit(SceneDocument::Passengers, label, [row, &mutate](SceneModel& model) { mutate(model.passengers[row]); });
}

bool PassengerPanel::editJourney(const QString& label, const std::function<void(ScenePassengerJourney&)>& mutate) {
	const auto row = static_cast<std::size_t>(m_journeyList->currentRow());
	return editPassenger(label, [row, &mutate](ScenePassenger& passenger) { mutate(passenger.journeys[row]); });
}

bool PassengerPanel::editLeg(const QString& label, const std::function<void(ScenePassengerLeg&)>& mutate) {
	const auto row = static_cast<std::size_t>(m_legList->currentRow());
	return editJourney(label, [row, &mutate](ScenePassengerJourney& journey) { mutate(journey.legs[row]); });
}

void PassengerPanel::refresh() {
	const bool loaded = hasScene();
	const bool editable = canEdit();
	{
		const int previousRow = m_passengerList->currentRow();
		const QString previousId = m_passengerList->currentItem() ? m_passengerList->currentItem()->text() : QString();
		const bool selectionCleared = m_passengerList->currentItem() && !m_passengerList->currentItem()->isSelected();
		const QSignalBlocker blocker(m_passengerList);
		m_passengerList->clear();
		if (loaded) {
			for (const auto& passenger : m_document->model().passengers)
				m_passengerList->addItem(QString::fromStdString(passenger.id));
		}
		restoreListSelection(m_passengerList, previousId, previousRow);
		if (selectionCleared)
			m_passengerList->clearSelection();
		m_passengerList->setEnabled(editable);
	}
	if (!loaded)
		clearImportResult();
	m_addPassengerButton->setEnabled(editable);
	m_importButton->setEnabled(editable);
	updatePassengerDetail();
}

void PassengerPanel::updatePassengerDetail() {
	const int passengerRow = m_passengerList->currentRow();
	const ScenePassenger* passenger = selectedPassenger();
	const bool enabled = passenger && canEdit();
	m_tabs->setVisible(passenger != nullptr);
	{
		const QSignalBlocker blocker(m_passengerIdEdit);
		m_passengerIdEdit->setText(passenger ? QString::fromStdString(passenger->id) : QString());
		m_passengerIdEdit->setEnabled(enabled);
	}
	{
		const int previousRow = m_journeyList->currentRow();
		const QSignalBlocker blocker(m_journeyList);
		m_journeyList->clear();
		if (passenger) {
			for (const auto& journey : passenger->journeys) {
				QString label = QString::fromStdString(journey.id);
				if (!journey.activity.empty())
					label += QStringLiteral(" (%1)").arg(QString::fromStdString(journey.activity));
				m_journeyList->addItem(label);
			}
		}
		restoreListSelection(m_journeyList, QString(), previousRow);
		m_journeyList->setEnabled(enabled);
	}
	m_addJourneyButton->setEnabled(enabled);
	m_deletePassengerButton->setEnabled(enabled);
	m_deleteJourneyButton->setEnabled(enabled && !m_journeyList->selectedItems().isEmpty());
	updateJourneyDetail();

	QStringList errors;
	QStringList notes;
	bool hasWarning = false;
	if (passenger && m_diagnostics) {
		const QString passengerPrefix = QStringLiteral("passengers[%1]").arg(passengerRow);
		std::set<std::string> journeyIds;
		std::set<std::string> legIds;
		for (const auto& journey : passenger->journeys) {
			journeyIds.insert(journey.id);
			for (const auto& leg : journey.legs)
				legIds.insert(leg.id);
		}
		for (const auto& diagnostic : *m_diagnostics) {
			if (!belongsToPassenger(diagnostic, passengerPrefix, passenger->id, journeyIds, legIds))
				continue;
			// The colour of the error callout says that its lines are errors; the other callout names the severity.
			const QString message = QString::fromStdString(diagnostic.message);
			if (diagnostic.severity == SceneSeverity::Error) {
				errors << message;
			} else {
				const bool warning = diagnostic.severity == SceneSeverity::Warning;
				notes << (warning ? QStringLiteral("Warning: ") : QStringLiteral("Note: ")) + message;
				hasWarning = hasWarning || warning;
			}
		}
	}
	if (errors.isEmpty() && notes.isEmpty()) {
		DialogLayout::setHelp(m_diagnosticLabel, passenger ? QString() : QStringLiteral("Select a passenger or add one."));
	} else {
		DialogLayout::setStatus(m_diagnosticLabel, DialogLayout::Severity::Error, cappedLines(errors));
	}
	m_diagnosticLabel->setToolTip(errors.size() > MaxDiagnosticLines ? errors.join(QLatin1Char('\n')) : QString());
	DialogLayout::setStatus(m_warningLabel, hasWarning ? DialogLayout::Severity::Warning : DialogLayout::Severity::Neutral,
		cappedLines(notes));
	m_warningLabel->setToolTip(notes.size() > MaxDiagnosticLines ? notes.join(QLatin1Char('\n')) : QString());
}

void PassengerPanel::updateJourneyDetail() {
	const ScenePassengerJourney* journey = selectedPassengerJourney();
	const bool enabled = journey && canEdit();
	{
		const int previousRow = m_legList->currentRow();
		const QSignalBlocker blocker(m_legList);
		m_legList->clear();
		if (journey) {
			for (const auto& leg : journey->legs)
				m_legList->addItem(QString::fromStdString(leg.id));
		}
		restoreListSelection(m_legList, QString(), previousRow);
		m_legList->setEnabled(enabled);
	}
	{
		const QSignalBlocker blocker(m_journeyIdEdit);
		m_journeyIdEdit->setText(journey ? QString::fromStdString(journey->id) : QString());
		m_journeyIdEdit->setEnabled(enabled);
	}
	{
		const QSignalBlocker blocker(m_journeyActivityEdit);
		m_journeyActivityEdit->setText(journey ? QString::fromStdString(journey->activity) : QString());
		m_journeyActivityEdit->setEnabled(enabled);
	}
	const SceneModel empty;
	const SceneModel& model = m_document ? m_document->model() : empty;
	{
		const QSignalBlocker blocker(m_journeyOriginCombo);
		populateStationCombo(m_journeyOriginCombo, model, journey ? journey->originStationId : std::string(), nullptr,
			journey != nullptr);
		m_journeyOriginCombo->setEnabled(enabled);
	}
	{
		const QSignalBlocker blocker(m_journeyDestinationCombo);
		populateStationCombo(m_journeyDestinationCombo, model, journey ? journey->destinationStationId : std::string(),
			nullptr, journey != nullptr);
		m_journeyDestinationCombo->setEnabled(enabled);
	}
	const std::array<double, 4> values = journey
		? std::array<double, 4>{journey->plannedDepartureStartSeconds, journey->plannedDepartureEndSeconds,
			  journey->plannedArrivalStartSeconds, journey->plannedArrivalEndSeconds}
		: std::array<double, 4>{0.0, 0.0, 0.0, 0.0};
	for (std::size_t index = 0; index < m_journeyWindowEdits.size(); ++index) {
		const QSignalBlocker blocker(m_journeyWindowEdits[index]);
		m_journeyWindowEdits[index]->setValue(values[index]);
		m_journeyWindowEdits[index]->setEnabled(enabled);
	}
	m_deleteJourneyButton->setEnabled(enabled);
	updateLegDetail();
}

void PassengerPanel::updateLegDetail() {
	const ScenePassengerLeg* leg = selectedPassengerLeg();
	const bool enabled = leg && canEdit();
	const SceneModel empty;
	const SceneModel& model = m_document ? m_document->model() : empty;
	{
		const QSignalBlocker blocker(m_legIdEdit);
		m_legIdEdit->setText(leg ? QString::fromStdString(leg->id) : QString());
		m_legIdEdit->setEnabled(enabled);
	}
	{
		const QSignalBlocker blocker(m_legServiceCombo);
		populateServiceCombo(m_legServiceCombo, model, leg ? leg->serviceId : std::string(), leg != nullptr);
		m_legServiceCombo->setEnabled(enabled);
	}
	const SceneService* legService = nullptr;
	if (leg) {
		const auto service = std::find_if(model.services.begin(), model.services.end(),
			[leg](const SceneService& value) { return value.id == leg->serviceId; });
		if (service != model.services.end())
			legService = &*service;
	}
	std::set<std::string> stopStations;
	if (legService)
		for (const auto& stop : legService->stops)
			stopStations.insert(stop.stationId);
	{
		const QSignalBlocker blocker(m_legOriginCombo);
		populateStationCombo(m_legOriginCombo, model, leg ? leg->originStationId : std::string(), &stopStations,
			leg != nullptr);
		m_legOriginCombo->setEnabled(enabled);
	}
	{
		const QSignalBlocker blocker(m_legDestinationCombo);
		populateStationCombo(m_legDestinationCombo, model, leg ? leg->destinationStationId : std::string(),
			&stopStations, leg != nullptr);
		m_legDestinationCombo->setEnabled(enabled);
	}
	{
		const int current = leg ? leg->occurrence : 1;
		int count = 1;
		if (legService)
			count = std::max(1, sceneServiceOccurrenceCount(*legService, occurrenceDuration()));
		const QSignalBlocker blocker(m_legOccurrenceEdit);
		m_legOccurrenceEdit->setRange(std::min(0, current), std::max({1, current, count}));
		m_legOccurrenceEdit->setValue(current);
		m_legOccurrenceEdit->setEnabled(enabled);
	}
	m_deleteLegButton->setEnabled(enabled);
	m_moveLegUpButton->setEnabled(enabled && m_legList->currentRow() > 0);
	m_moveLegDownButton->setEnabled(enabled && m_legList->currentRow() + 1 < m_legList->count());
	m_addLegButton->setEnabled(selectedPassengerJourney() && canEdit());
}

void PassengerPanel::commitPendingEditors() {
	commitPassengerId();
	commitJourneyId();
	commitJourneyActivity();
	for (std::size_t index = 0; index < m_journeyWindowEdits.size(); ++index) {
		QDoubleSpinBox* edit = m_journeyWindowEdits[index];
		if (hasEditorFocus(edit)) {
			edit->interpretText();
			commitJourneyWindow(static_cast<int>(index));
		}
	}
	commitLegId();
	if (hasEditorFocus(m_legOccurrenceEdit)) {
		m_legOccurrenceEdit->interpretText();
		commitLegOccurrence();
	}
}

void PassengerPanel::addPassenger() {
	if (!canEdit())
		return;
	ScenePassenger passenger;
	passenger.id = sceneUniquePassengerId(m_document->model(), "new_passenger");
	if (!m_document->edit(SceneDocument::Passengers, "Add passenger",
			[&passenger](SceneModel& model) { model.passengers.push_back(std::move(passenger)); }))
		return;
	refresh();
	m_passengerList->setCurrentRow(static_cast<int>(m_document->model().passengers.size()) - 1);
	emit changed();
}

void PassengerPanel::deletePassenger() {
	if (!canEdit())
		return;
	const auto& passengers = m_document->model().passengers;
	const int row = selectedPassengerRow();
	if (row < 0 || row >= static_cast<int>(passengers.size()))
		return;
	const std::string id = passengers[static_cast<std::size_t>(row)].id;
	if (!m_prompts->confirm(this, "Delete Passenger",
			QString("Delete passenger '%1' and its journeys?").arg(QString::fromStdString(id))))
		return;
	if (!m_document->edit(SceneDocument::Passengers, "Delete passenger",
			[row](SceneModel& model) { model.passengers.erase(model.passengers.begin() + row); }))
		return;
	// Refresh widgets before validation so a shifted row cannot write stale
	// detail values back into the newly selected passenger.
	refresh();
	emit changed();
}

void PassengerPanel::addJourney() {
	const ScenePassenger* passenger = selectedPassenger();
	if (!passenger || !canEdit())
		return;
	const SceneModel& model = m_document->model();
	ScenePassengerJourney journey;
	journey.id = sceneUniquePassengerJourneyId(model, passenger->id + ".journey");
	if (!model.stations.empty()) {
		journey.originStationId = model.stations.front().id;
		journey.destinationStationId = model.stations.front().id;
	}
	if (!editPassenger("Add journey", [&journey](ScenePassenger& edited) { edited.journeys.push_back(std::move(journey)); }))
		return;
	refresh();
	m_journeyList->setCurrentRow(static_cast<int>(passenger->journeys.size()) - 1);
	emit changed();
}

void PassengerPanel::deleteJourney() {
	const ScenePassenger* passenger = selectedPassenger();
	if (!passenger || !canEdit())
		return;
	const int row = m_journeyList->currentRow();
	if (row < 0 || row >= static_cast<int>(passenger->journeys.size()))
		return;
	const std::string id = passenger->journeys[static_cast<std::size_t>(row)].id;
	if (!m_prompts->confirm(this, "Delete Journey",
			QString("Delete journey '%1' and its legs?").arg(QString::fromStdString(id))))
		return;
	if (!editPassenger("Delete journey", [row](ScenePassenger& edited) { edited.journeys.erase(edited.journeys.begin() + row); }))
		return;
	refresh();
	emit changed();
}

void PassengerPanel::addLeg() {
	const ScenePassengerJourney* journey = selectedPassengerJourney();
	if (!journey || !canEdit())
		return;
	const SceneModel& model = m_document->model();
	ScenePassengerLeg leg;
	leg.id = sceneUniquePassengerLegId(model, journey->id + ".leg");
	if (!model.services.empty()) {
		const SceneService& service = model.services.front();
		leg.serviceId = service.id;
		if (!service.stops.empty()) {
			leg.originStationId = service.stops.front().stationId;
			leg.destinationStationId = service.stops.size() > 1
				? service.stops[1].stationId
				: service.stops.front().stationId;
		}
	}
	if (leg.originStationId.empty() && !model.stations.empty()) {
		leg.originStationId = model.stations.front().id;
		leg.destinationStationId = model.stations.front().id;
	}
	if (!editJourney("Add leg", [&leg](ScenePassengerJourney& edited) { edited.legs.push_back(std::move(leg)); }))
		return;
	refresh();
	m_legList->setCurrentRow(static_cast<int>(journey->legs.size()) - 1);
	m_tabs->setCurrentIndex(1);
	emit changed();
}

void PassengerPanel::deleteLeg() {
	const ScenePassengerJourney* journey = selectedPassengerJourney();
	if (!journey || !canEdit())
		return;
	const int row = m_legList->currentRow();
	if (row < 0 || row >= static_cast<int>(journey->legs.size()))
		return;
	const std::string id = journey->legs[static_cast<std::size_t>(row)].id;
	if (!m_prompts->confirm(this, "Delete Leg", QString("Delete passenger leg '%1'?").arg(QString::fromStdString(id))))
		return;
	if (!editJourney("Delete leg", [row](ScenePassengerJourney& edited) { edited.legs.erase(edited.legs.begin() + row); }))
		return;
	refresh();
	emit changed();
}

void PassengerPanel::moveLeg(int offset) {
	const ScenePassengerJourney* journey = selectedPassengerJourney();
	if (!journey || !canEdit())
		return;
	const int row = m_legList->currentRow();
	const int target = row + offset;
	if (row < 0 || target < 0 || target >= static_cast<int>(journey->legs.size()))
		return;
	if (!editJourney("Move leg", [row, target](ScenePassengerJourney& edited) {
			std::swap(edited.legs[static_cast<std::size_t>(row)], edited.legs[static_cast<std::size_t>(target)]);
		}))
		return;
	refresh();
	m_legList->setCurrentRow(target);
	emit changed();
}

void PassengerPanel::commitPassengerId() {
	if (!canEdit())
		return;
	const auto& passengers = m_document->model().passengers;
	const int row = selectedPassengerRow();
	if (row < 0 || row >= static_cast<int>(passengers.size()))
		return;
	const std::string oldId = passengers[static_cast<std::size_t>(row)].id;
	const std::string newId = m_passengerIdEdit->text().trimmed().toStdString();
	bool duplicate = false;
	for (int index = 0; index < static_cast<int>(passengers.size()); ++index)
		if (index != row && passengers[static_cast<std::size_t>(index)].id == newId)
			duplicate = true;
	if (newId.empty() || (newId != oldId && duplicate)) {
		const QSignalBlocker blocker(m_passengerIdEdit);
		m_passengerIdEdit->setText(QString::fromStdString(oldId));
		m_prompts->showError(this, newId.empty() ? "Invalid Passenger ID" : "Duplicate Passenger ID",
			newId.empty() ? "Passenger IDs must not be empty." : "Choose a unique passenger ID.", true);
		return;
	}
	if (newId == oldId)
		return;
	if (!editPassenger("Rename passenger", [this, row, &newId](ScenePassenger& edited) {
			edited.id = newId;
			if (auto* item = m_passengerList->item(row)) {
				const QSignalBlocker blocker(m_passengerList);
				item->setText(QString::fromStdString(newId));
			}
		}))
		return;
	emit changed();
}

void PassengerPanel::commitJourneyId() {
	const ScenePassengerJourney* journey = selectedPassengerJourney();
	if (!journey || !canEdit())
		return;
	const std::string oldId = journey->id;
	const std::string newId = m_journeyIdEdit->text().trimmed().toStdString();
	bool duplicate = false;
	for (const auto& owner : m_document->model().passengers)
		for (const auto& candidate : owner.journeys)
			if (&candidate != journey && candidate.id == newId)
				duplicate = true;
	if (newId.empty() || (newId != oldId && duplicate)) {
		const QSignalBlocker blocker(m_journeyIdEdit);
		m_journeyIdEdit->setText(QString::fromStdString(oldId));
		m_prompts->showError(this, newId.empty() ? "Invalid Journey ID" : "Duplicate Journey ID",
			newId.empty() ? "Journey IDs must not be empty." : "Choose a unique journey ID.", true);
		return;
	}
	if (newId == oldId)
		return;
	if (!editJourney("Rename journey", [&newId](ScenePassengerJourney& edited) { edited.id = newId; }))
		return;
	refresh();
	emit changed();
}

void PassengerPanel::commitJourneyActivity() {
	const ScenePassengerJourney* journey = selectedPassengerJourney();
	if (!journey || !canEdit())
		return;
	const std::string value = m_journeyActivityEdit->text().toStdString();
	if (value == journey->activity)
		return;
	if (!editJourney("Edit journey", [&value](ScenePassengerJourney& edited) { edited.activity = value; }))
		return;
	refresh();
	emit changed();
}

void PassengerPanel::commitJourneyStation(bool origin) {
	const ScenePassengerJourney* journey = selectedPassengerJourney();
	QComboBox* combo = origin ? m_journeyOriginCombo : m_journeyDestinationCombo;
	if (!journey || !canEdit())
		return;
	const std::string value = combo->currentData().toString().toStdString();
	if ((origin ? journey->originStationId : journey->destinationStationId) == value)
		return;
	if (!editJourney("Edit journey", [origin, &value](ScenePassengerJourney& edited) {
			(origin ? edited.originStationId : edited.destinationStationId) = value;
		}))
		return;
	refresh();
	emit changed();
}

void PassengerPanel::commitJourneyWindow(int index) {
	const ScenePassengerJourney* journey = selectedPassengerJourney();
	if (!journey || !canEdit() || index < 0 || index >= static_cast<int>(m_journeyWindowEdits.size()))
		return;
	const double value = m_journeyWindowEdits[static_cast<std::size_t>(index)]->value();
	constexpr std::array<double ScenePassengerJourney::*, 4> fields = {&ScenePassengerJourney::plannedDepartureStartSeconds,
		&ScenePassengerJourney::plannedDepartureEndSeconds, &ScenePassengerJourney::plannedArrivalStartSeconds,
		&ScenePassengerJourney::plannedArrivalEndSeconds};
	const auto field = fields[static_cast<std::size_t>(index)];
	if (journey->*field == value)
		return;
	if (!editJourney("Edit journey", [field, value](ScenePassengerJourney& edited) { edited.*field = value; }))
		return;
	emit changed();
}

void PassengerPanel::commitLegId() {
	const ScenePassengerLeg* leg = selectedPassengerLeg();
	if (!leg || !canEdit())
		return;
	const std::string oldId = leg->id;
	const std::string newId = m_legIdEdit->text().trimmed().toStdString();
	bool duplicate = false;
	for (const auto& passenger : m_document->model().passengers)
		for (const auto& owner : passenger.journeys)
			for (const auto& candidate : owner.legs)
				if (&candidate != leg && candidate.id == newId)
					duplicate = true;
	if (newId.empty() || (newId != oldId && duplicate)) {
		const QSignalBlocker blocker(m_legIdEdit);
		m_legIdEdit->setText(QString::fromStdString(oldId));
		m_prompts->showError(this, newId.empty() ? "Invalid Leg ID" : "Duplicate Leg ID",
			newId.empty() ? "Leg IDs must not be empty." : "Choose a unique leg ID.", true);
		return;
	}
	if (newId == oldId)
		return;
	if (!editLeg("Rename leg", [&newId](ScenePassengerLeg& edited) { edited.id = newId; }))
		return;
	refresh();
	emit changed();
}

void PassengerPanel::commitLegStation(bool origin) {
	const ScenePassengerLeg* leg = selectedPassengerLeg();
	QComboBox* combo = origin ? m_legOriginCombo : m_legDestinationCombo;
	if (!leg || !canEdit())
		return;
	const std::string value = combo->currentData().toString().toStdString();
	if ((origin ? leg->originStationId : leg->destinationStationId) == value)
		return;
	if (!editLeg("Edit leg", [origin, &value](ScenePassengerLeg& edited) {
			(origin ? edited.originStationId : edited.destinationStationId) = value;
		}))
		return;
	refresh();
	emit changed();
}

void PassengerPanel::commitLegService() {
	const ScenePassengerLeg* leg = selectedPassengerLeg();
	if (!leg || !canEdit())
		return;
	const std::string value = m_legServiceCombo->currentData().toString().toStdString();
	if (leg->serviceId == value)
		return;
	if (!editLeg("Edit leg", [&value](ScenePassengerLeg& edited) { edited.serviceId = value; }))
		return;
	refresh();
	emit changed();
}

void PassengerPanel::commitLegOccurrence() {
	const ScenePassengerLeg* leg = selectedPassengerLeg();
	if (!leg || !canEdit())
		return;
	m_legOccurrenceEdit->interpretText();
	const int value = m_legOccurrenceEdit->value();
	if (leg->occurrence == value)
		return;
	if (!editLeg("Edit leg", [value](ScenePassengerLeg& edited) { edited.occurrence = value; }))
		return;
	emit changed();
}

void PassengerPanel::importPassengers() {
	if (!canEdit())
		return;
	const QString sourcePath = m_importFolderChooser
		? m_importFolderChooser()
		: QFileDialog::getExistingDirectory(this, "Select Legacy Passenger Folder", QDir::homePath());
	if (sourcePath.isEmpty())
		return;
	const SceneModel& model = m_document->model();
	const ScenePassengerImportResult imported = importLegacyPassengers(sourcePath.toStdString(), model);
	clearImportResult();
	// The tooltip of the source is the path of the file or folder the row is about, and that of every other cell is its text.
	const auto addResultRow = [this](const QString& source, const QString& sourceFile, int row, const QString& passengerId,
								  const QString& status, const QString& detail) {
		const int target = m_importResultTable->rowCount();
		m_importResultTable->insertRow(target);
		const QStringList texts = {source, row > 0 ? QString::number(row) : QStringLiteral("-"),
			passengerId.isEmpty() ? QStringLiteral("-") : passengerId, status, detail};
		for (int column = 0; column < texts.size(); ++column) {
			auto* item = new QTableWidgetItem(texts[column]);
			item->setToolTip(column == 0 ? display_path::format(sourceFile) : texts[column]);
			m_importResultTable->setItem(target, column, item);
		}
	};
	if (!imported.success()) {
		for (const auto& row : imported.rows) {
			const QString file = QString::fromStdString(row.sourceFile);
			addResultRow(QFileInfo(file).fileName(), file, row.row, QString::fromStdString(row.passengerId),
				QStringLiteral("Rejected"), QString::fromStdString(row.context));
		}
		if (imported.rows.empty())
			addResultRow(QFileInfo(sourcePath).fileName(), sourcePath, 0, QString(), QStringLiteral("Rejected"),
				QStringLiteral("Could not read the DAS and RouteChoice files"));
		QString message = firstDiagnosticDialogText(imported.diagnostics);
		if (message.isEmpty())
			message = QStringLiteral("Passenger import reported errors.");
		m_prompts->showError(this, "Passenger Import Failed", message, true);
		m_importResultTable->resizeColumnsToContents();
		emit statusMessage(QStringLiteral("Passenger import failed. Nothing was changed"), 7000);
		return;
	}

	const PassengerImportMerge merge = mergeImportedPassengers(model, imported, sourcePath.toStdString());
	for (const std::string& passengerId : merge.rejected) {
		addResultRow(QStringLiteral("%1 (batch)").arg(QFileInfo(sourcePath).fileName()), sourcePath, 0,
			QString::fromStdString(passengerId), QStringLiteral("Rejected"),
			QStringLiteral("Passenger, journey or leg ID already exists"));
	}
	for (const auto& row : imported.rows) {
		const QString file = QString::fromStdString(row.sourceFile);
		const bool rejectedByBatch = merge.rejects(row.passengerId);
		const QString status = !row.accepted || rejectedByBatch ? QStringLiteral("Rejected")
																: (row.unresolvedReferences ? QStringLiteral("Unresolved") : QStringLiteral("Accepted"));
		const QString detail = rejectedByBatch
			? QStringLiteral("Passenger ID already exists")
			: QString::fromStdString(row.context);
		addResultRow(QFileInfo(file).fileName(), file, row.row, QString::fromStdString(row.passengerId), status, detail);
	}
	if (imported.rows.empty() && imported.diagnostics.empty())
		addResultRow(QFileInfo(sourcePath).fileName(), sourcePath, 0, QString(), QStringLiteral("Rejected"),
			QStringLiteral("No passengers found"));
	const int acceptedCount = static_cast<int>(merge.accepted.size());
	const int rejectedPassengers = static_cast<int>(merge.rejected.size());
	if (merge.changesScene()) {
		if (!m_document->edit(SceneDocument::Passengers, "Import passengers", [&merge](SceneModel& edited) {
				edited.passengers.insert(edited.passengers.end(), merge.accepted.begin(), merge.accepted.end());
				edited.importReport.insert(edited.importReport.end(), merge.report.begin(), merge.report.end());
			}))
			return;
		refresh();
		emit changed();
	}
	m_importResultTable->resizeColumnsToContents();
	QString summary = acceptedCount > 0
		? QStringLiteral("Imported ") + countText(acceptedCount, QStringLiteral("passenger"), QStringLiteral("passengers"))
		: QStringLiteral("No passengers imported");
	if (rejectedPassengers > 0)
		summary += QStringLiteral(", %1 rejected").arg(rejectedPassengers);
	emit statusMessage(summary, 7000);
}
