#include "editors/PassengerPanel.h"

#include "scene/SceneDiagnostic.h"
#include "scene/SceneIds.h"
#include "scene/SceneImporter.h"
#include "widgets/CompactDoubleSpinBox.h"

#include <QApplication>
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
			: QStringLiteral("%1 | %2").arg(QString::fromStdString(station.id),
				  QString::fromStdString(station.name));
		combo->addItem(label, QString::fromStdString(station.id));
	}
	int selected = combo->findData(QString::fromStdString(current));
	if (selected < 0) {
		combo->addItem(current.empty() ? QStringLiteral("Invalid: (empty)")
									   : QStringLiteral("Invalid: %1").arg(QString::fromStdString(current)),
			QString::fromStdString(current));
		selected = combo->count() - 1;
	}
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
		combo->addItem(current.empty() ? QStringLiteral("Invalid: (empty)")
									   : QStringLiteral("Invalid: %1").arg(QString::fromStdString(current)),
			QString::fromStdString(current));
		selected = combo->count() - 1;
	}
	combo->setCurrentIndex(selected);
}

QString firstDiagnosticMessage(const std::vector<SceneDiagnostic>& diagnostics) {
	for (const auto& d : diagnostics) {
		if (d.severity == SceneSeverity::Error)
			return QString::fromStdString(d.message);
	}
	if (!diagnostics.empty())
		return QString::fromStdString(diagnostics.front().message);
	return QString();
}

// The row that stays selected when a list is rebuilt with `count` entries.
int rowAfterRebuild(int previousRow, int count) {
	return count == 0 ? -1 : (previousRow < 0 ? 0 : std::min(previousRow, count - 1));
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
	m_importButton->setToolTip("Import the exact legacy DAS and RouteChoice passenger pair; imported records are appended.");
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
	m_diagnosticLabel->setWordWrap(true);
	m_diagnosticLabel->setStyleSheet("color: #5b6871;");
	passengerDetailLayout->addWidget(m_diagnosticLabel);

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
	m_journeyOriginCombo = new QComboBox(journeyDetailPane);
	m_journeyOriginCombo->setObjectName("passengerJourneyOriginCombo");
	journeyForm->addRow("Origin station", m_journeyOriginCombo);
	m_journeyDestinationCombo = new QComboBox(journeyDetailPane);
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
	m_legOriginCombo = new QComboBox(legDetailPane);
	m_legOriginCombo->setObjectName("passengerLegOriginCombo");
	legForm->addRow("Origin station", m_legOriginCombo);
	m_legDestinationCombo = new QComboBox(legDetailPane);
	m_legDestinationCombo->setObjectName("passengerLegDestinationCombo");
	legForm->addRow("Destination station", m_legDestinationCombo);
	m_legServiceCombo = new QComboBox(legDetailPane);
	m_legServiceCombo->setObjectName("passengerLegServiceCombo");
	legForm->addRow("Service", m_legServiceCombo);
	m_legOccurrenceEdit = new QSpinBox(legDetailPane);
	m_legOccurrenceEdit->setObjectName("passengerLegOccurrenceSpin");
	m_legOccurrenceEdit->setKeyboardTracking(false);
	legForm->addRow("Occurrence", m_legOccurrenceEdit);
	legsLayout->addWidget(legDetailPane, 1);
	m_tabs->addTab(legsPane, "Legs");
	passengerDetailLayout->addWidget(m_tabs, 1);

	m_importResultTable = new QTableWidget(0, 4, passengerDetailPane);
	m_importResultTable->setObjectName("passengerImportResultTable");
	m_importResultTable->setHorizontalHeaderLabels({"Source", "Row", "Status", "Detail"});
	m_importResultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_importResultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_importResultTable->setMaximumHeight(130);
	m_importResultTable->horizontalHeader()->setStretchLastSection(true);
	m_importResultTable->setToolTip("Transient results from the most recent passenger import; not persisted.");
	passengerDetailLayout->addWidget(m_importResultTable);
	m_importResultTable->hide();
	const auto updateImportVisibility = [this]() {
		m_importResultTable->setVisible(m_importResultTable->rowCount() > 0);
	};
	connect(m_importResultTable->model(), &QAbstractItemModel::rowsInserted, m_importResultTable, updateImportVisibility);
	connect(m_importResultTable->model(), &QAbstractItemModel::rowsRemoved, m_importResultTable, updateImportVisibility);
	passengerLayout->addWidget(passengerDetailPane, 2);

	connect(m_passengerList, &QListWidget::currentRowChanged, this, [this](int) { updatePassengerDetail(); });
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
	refresh();
}

void PassengerPanel::setDocument(SceneDocument* document) {
	QObject::disconnect(m_replacedConnection);
	m_document = document;
	if (m_document)
		m_replacedConnection = connect(m_document, &SceneDocument::replaced, this, &PassengerPanel::refresh);
	refresh();
}

void PassengerPanel::setPrompts(UserPrompts* prompts) {
	m_prompts = prompts ? prompts : &m_defaultPrompts;
}

void PassengerPanel::setDiagnostics(const std::vector<SceneDiagnostic>* diagnostics) {
	m_diagnostics = diagnostics;
}

void PassengerPanel::setOccurrenceDuration(std::function<double()> duration) {
	m_occurrenceDuration = std::move(duration);
}

void PassengerPanel::setImportFolderChooser(std::function<QString()> chooser) {
	m_importFolderChooser = std::move(chooser);
}

void PassengerPanel::clearImportResult() {
	m_importResultTable->setRowCount(0);
}

bool PassengerPanel::canEdit() const {
	return m_document && m_document->isLoaded() && m_document->isEditable();
}

double PassengerPanel::occurrenceDuration() const {
	return m_occurrenceDuration ? m_occurrenceDuration() : m_document->model().settings.durationSeconds;
}

ScenePassenger* PassengerPanel::selectedPassenger() {
	if (!m_document || !m_document->isLoaded())
		return nullptr;
	auto& passengers = m_document->mutableModel().passengers;
	const int row = m_passengerList->currentRow();
	return row >= 0 && row < static_cast<int>(passengers.size()) ? &passengers[static_cast<std::size_t>(row)] : nullptr;
}

ScenePassengerJourney* PassengerPanel::selectedPassengerJourney() {
	ScenePassenger* passenger = selectedPassenger();
	const int row = m_journeyList->currentRow();
	return passenger && row >= 0 && row < static_cast<int>(passenger->journeys.size())
		? &passenger->journeys[static_cast<std::size_t>(row)]
		: nullptr;
}

ScenePassengerLeg* PassengerPanel::selectedPassengerLeg() {
	ScenePassengerJourney* journey = selectedPassengerJourney();
	const int row = m_legList->currentRow();
	return journey && row >= 0 && row < static_cast<int>(journey->legs.size())
		? &journey->legs[static_cast<std::size_t>(row)]
		: nullptr;
}

void PassengerPanel::refresh() {
	const bool loaded = m_document && m_document->isLoaded();
	const bool editable = canEdit();
	{
		const int previousRow = m_passengerList->currentRow();
		const QSignalBlocker blocker(m_passengerList);
		m_passengerList->clear();
		if (loaded) {
			for (const auto& passenger : m_document->model().passengers)
				m_passengerList->addItem(QString::fromStdString(passenger.id));
		}
		m_passengerList->setCurrentRow(rowAfterRebuild(previousRow, m_passengerList->count()));
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
	ScenePassenger* passenger = selectedPassenger();
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
					label += QStringLiteral(" | ") + QString::fromStdString(journey.activity);
				m_journeyList->addItem(label);
			}
		}
		m_journeyList->setCurrentRow(rowAfterRebuild(previousRow, m_journeyList->count()));
		m_journeyList->setEnabled(enabled);
	}
	m_addJourneyButton->setEnabled(enabled);
	m_deletePassengerButton->setEnabled(enabled);
	m_deleteJourneyButton->setEnabled(enabled && !m_journeyList->selectedItems().isEmpty());
	updateJourneyDetail();

	QStringList messages;
	if (passenger && m_diagnostics) {
		const QString passengerPrefix = QStringLiteral("passengers[%1]").arg(passengerRow);
		const int journeyRow = m_journeyList->currentRow();
		const int legRow = m_legList->currentRow();
		const ScenePassengerJourney* journey = selectedPassengerJourney();
		const ScenePassengerLeg* leg = selectedPassengerLeg();
		const QString journeyPrefix = journeyRow >= 0
			? passengerPrefix + QStringLiteral(".journeys[%1]").arg(journeyRow)
			: QString();
		const QString legPrefix = journey && legRow >= 0
			? journeyPrefix + QStringLiteral(".legs[%1]").arg(legRow)
			: QString();
		const QString passengerId = QString::fromStdString(passenger->id);
		const QString journeyId = journey ? QString::fromStdString(journey->id) : QString();
		const QString legId = leg ? QString::fromStdString(leg->id) : QString();
		for (const auto& diagnostic : *m_diagnostics) {
			const QString path = QString::fromStdString(diagnostic.path);
			const QString itemId = QString::fromStdString(diagnostic.itemId);
			const QString relatedId = QString::fromStdString(diagnostic.relatedId);
			const bool match = (!legPrefix.isEmpty() && path.startsWith(legPrefix))
				|| (!journeyPrefix.isEmpty() && path.startsWith(journeyPrefix))
				|| path.startsWith(passengerPrefix)
				|| itemId == passengerId || itemId == journeyId || itemId == legId
				|| relatedId == passengerId || relatedId == journeyId || relatedId == legId;
			if (match)
				messages << QString::fromStdString(severityLabel(diagnostic.severity)).toUpper()
						+ QStringLiteral(": ") + QString::fromStdString(diagnostic.message);
		}
	}
	m_diagnosticLabel->setText(messages.isEmpty()
			? (passenger ? QStringLiteral("No validation messages for this passenger selection.")
						 : QStringLiteral("Select a passenger or use Add to create one."))
			: messages.join(QStringLiteral("\n")));
	m_diagnosticLabel->setStyleSheet(messages.isEmpty() ? "color: #5b6871;"
			: std::any_of(messages.begin(), messages.end(), [](const QString& message) {
				  return message.startsWith("ERROR:");
			  })
			? "color: #81352d;"
			: "color: #965c22;");
}

void PassengerPanel::updateJourneyDetail() {
	ScenePassengerJourney* journey = selectedPassengerJourney();
	const bool enabled = journey && canEdit();
	{
		const int previousRow = m_legList->currentRow();
		const QSignalBlocker blocker(m_legList);
		m_legList->clear();
		if (journey) {
			for (const auto& leg : journey->legs)
				m_legList->addItem(QString::fromStdString(leg.id));
		}
		m_legList->setCurrentRow(rowAfterRebuild(previousRow, m_legList->count()));
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
	ScenePassengerLeg* leg = selectedPassengerLeg();
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
	const auto hasEditorFocus = [](QWidget* editor) {
		QWidget* focus = QApplication::focusWidget();
		return editor && (editor->hasFocus() || (focus && editor->isAncestorOf(focus)));
	};
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
	m_document->mutableModel().passengers.push_back(std::move(passenger));
	m_document->markChanged(SceneDocument::Passengers, "Add passenger");
	refresh();
	m_passengerList->setCurrentRow(static_cast<int>(m_document->model().passengers.size()) - 1);
	emit changed();
}

void PassengerPanel::deletePassenger() {
	if (!canEdit())
		return;
	auto& passengers = m_document->mutableModel().passengers;
	const int row = m_passengerList->currentRow();
	if (row < 0 || row >= static_cast<int>(passengers.size()))
		return;
	const std::string id = passengers[static_cast<std::size_t>(row)].id;
	if (!m_prompts->confirm(this, "Delete Passenger",
			QString("Delete passenger '%1' and its journeys?").arg(QString::fromStdString(id))))
		return;
	passengers.erase(passengers.begin() + row);
	m_document->markChanged(SceneDocument::Passengers, "Delete passenger");
	// Refresh widgets before validation so a shifted row cannot write stale
	// detail values back into the newly selected passenger.
	refresh();
	emit changed();
}

void PassengerPanel::addJourney() {
	ScenePassenger* passenger = selectedPassenger();
	if (!passenger || !canEdit())
		return;
	const SceneModel& model = m_document->model();
	ScenePassengerJourney journey;
	journey.id = sceneUniquePassengerJourneyId(model, passenger->id + ".journey");
	if (!model.stations.empty()) {
		journey.originStationId = model.stations.front().id;
		journey.destinationStationId = model.stations.front().id;
	}
	passenger->journeys.push_back(std::move(journey));
	m_document->markChanged(SceneDocument::Passengers, "Add journey");
	refresh();
	m_journeyList->setCurrentRow(static_cast<int>(passenger->journeys.size()) - 1);
	emit changed();
}

void PassengerPanel::deleteJourney() {
	ScenePassenger* passenger = selectedPassenger();
	if (!passenger || !canEdit())
		return;
	const int row = m_journeyList->currentRow();
	if (row < 0 || row >= static_cast<int>(passenger->journeys.size()))
		return;
	const std::string id = passenger->journeys[static_cast<std::size_t>(row)].id;
	if (!m_prompts->confirm(this, "Delete Journey",
			QString("Delete journey '%1' and its legs?").arg(QString::fromStdString(id))))
		return;
	passenger->journeys.erase(passenger->journeys.begin() + row);
	m_document->markChanged(SceneDocument::Passengers, "Delete journey");
	refresh();
	emit changed();
}

void PassengerPanel::addLeg() {
	ScenePassengerJourney* journey = selectedPassengerJourney();
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
	journey->legs.push_back(std::move(leg));
	m_document->markChanged(SceneDocument::Passengers, "Add leg");
	refresh();
	m_legList->setCurrentRow(static_cast<int>(journey->legs.size()) - 1);
	m_tabs->setCurrentIndex(1);
	emit changed();
}

void PassengerPanel::deleteLeg() {
	ScenePassengerJourney* journey = selectedPassengerJourney();
	if (!journey || !canEdit())
		return;
	const int row = m_legList->currentRow();
	if (row < 0 || row >= static_cast<int>(journey->legs.size()))
		return;
	const std::string id = journey->legs[static_cast<std::size_t>(row)].id;
	if (!m_prompts->confirm(this, "Delete Leg", QString("Delete passenger leg '%1'?").arg(QString::fromStdString(id))))
		return;
	journey->legs.erase(journey->legs.begin() + row);
	m_document->markChanged(SceneDocument::Passengers, "Delete leg");
	refresh();
	emit changed();
}

void PassengerPanel::moveLeg(int offset) {
	ScenePassengerJourney* journey = selectedPassengerJourney();
	if (!journey || !canEdit())
		return;
	const int row = m_legList->currentRow();
	const int target = row + offset;
	if (row < 0 || target < 0 || target >= static_cast<int>(journey->legs.size()))
		return;
	std::swap(journey->legs[static_cast<std::size_t>(row)], journey->legs[static_cast<std::size_t>(target)]);
	m_document->markChanged(SceneDocument::Passengers, "Move leg");
	refresh();
	m_legList->setCurrentRow(target);
	emit changed();
}

void PassengerPanel::commitPassengerId() {
	if (!canEdit())
		return;
	auto& passengers = m_document->mutableModel().passengers;
	const int row = m_passengerList->currentRow();
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
		m_prompts->showError(this, newId.empty() ? "Invalid Passenger ID" : "Passenger ID already exists",
			newId.empty() ? "Passenger IDs must not be empty." : "Choose a unique passenger ID.", true);
		return;
	}
	if (newId == oldId)
		return;
	passengers[static_cast<std::size_t>(row)].id = newId;
	if (auto* item = m_passengerList->item(row)) {
		const QSignalBlocker blocker(m_passengerList);
		item->setText(QString::fromStdString(newId));
	}
	m_document->markChanged(SceneDocument::Passengers, "Rename passenger");
	emit changed();
}

void PassengerPanel::commitJourneyId() {
	ScenePassengerJourney* journey = selectedPassengerJourney();
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
		m_prompts->showError(this, newId.empty() ? "Invalid Journey ID" : "Journey ID already exists",
			newId.empty() ? "Journey IDs must not be empty." : "Choose a unique journey ID.", true);
		return;
	}
	if (newId == oldId)
		return;
	journey->id = newId;
	m_document->markChanged(SceneDocument::Passengers, "Rename journey");
	refresh();
	emit changed();
}

void PassengerPanel::commitJourneyActivity() {
	ScenePassengerJourney* journey = selectedPassengerJourney();
	if (!journey || !canEdit())
		return;
	const std::string value = m_journeyActivityEdit->text().toStdString();
	if (value == journey->activity)
		return;
	journey->activity = value;
	m_document->markChanged(SceneDocument::Passengers, "Edit journey");
	refresh();
	emit changed();
}

void PassengerPanel::commitJourneyStation(bool origin) {
	ScenePassengerJourney* journey = selectedPassengerJourney();
	QComboBox* combo = origin ? m_journeyOriginCombo : m_journeyDestinationCombo;
	if (!journey || !canEdit())
		return;
	const std::string value = combo->currentData().toString().toStdString();
	std::string& target = origin ? journey->originStationId : journey->destinationStationId;
	if (target == value)
		return;
	target = value;
	m_document->markChanged(SceneDocument::Passengers, "Edit journey");
	refresh();
	emit changed();
}

void PassengerPanel::commitJourneyWindow(int index) {
	ScenePassengerJourney* journey = selectedPassengerJourney();
	if (!journey || !canEdit() || index < 0 || index >= static_cast<int>(m_journeyWindowEdits.size()))
		return;
	const double value = m_journeyWindowEdits[static_cast<std::size_t>(index)]->value();
	double* target = index == 0 ? &journey->plannedDepartureStartSeconds
								: (index == 1 ? &journey->plannedDepartureEndSeconds
											  : (index == 2 ? &journey->plannedArrivalStartSeconds : &journey->plannedArrivalEndSeconds));
	if (*target == value)
		return;
	*target = value;
	m_document->markChanged(SceneDocument::Passengers, "Edit journey");
	emit changed();
}

void PassengerPanel::commitLegId() {
	ScenePassengerLeg* leg = selectedPassengerLeg();
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
		m_prompts->showError(this, newId.empty() ? "Invalid Leg ID" : "Leg ID already exists",
			newId.empty() ? "Leg IDs must not be empty." : "Choose a unique leg ID.", true);
		return;
	}
	if (newId == oldId)
		return;
	leg->id = newId;
	m_document->markChanged(SceneDocument::Passengers, "Rename leg");
	refresh();
	emit changed();
}

void PassengerPanel::commitLegStation(bool origin) {
	ScenePassengerLeg* leg = selectedPassengerLeg();
	QComboBox* combo = origin ? m_legOriginCombo : m_legDestinationCombo;
	if (!leg || !canEdit())
		return;
	const std::string value = combo->currentData().toString().toStdString();
	std::string& target = origin ? leg->originStationId : leg->destinationStationId;
	if (target == value)
		return;
	target = value;
	m_document->markChanged(SceneDocument::Passengers, "Edit leg");
	refresh();
	emit changed();
}

void PassengerPanel::commitLegService() {
	ScenePassengerLeg* leg = selectedPassengerLeg();
	if (!leg || !canEdit())
		return;
	const std::string value = m_legServiceCombo->currentData().toString().toStdString();
	if (leg->serviceId == value)
		return;
	leg->serviceId = value;
	m_document->markChanged(SceneDocument::Passengers, "Edit leg");
	refresh();
	emit changed();
}

void PassengerPanel::commitLegOccurrence() {
	ScenePassengerLeg* leg = selectedPassengerLeg();
	if (!leg || !canEdit())
		return;
	m_legOccurrenceEdit->interpretText();
	const int value = m_legOccurrenceEdit->value();
	if (leg->occurrence == value)
		return;
	leg->occurrence = value;
	m_document->markChanged(SceneDocument::Passengers, "Edit leg");
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
	SceneModel& model = m_document->mutableModel();
	const ScenePassengerImportResult imported = importLegacyPassengers(sourcePath.toStdString(), model);
	clearImportResult();
	const auto addResultRow = [this](const QString& source, int row, const QString& status, const QString& detail) {
		const int target = m_importResultTable->rowCount();
		m_importResultTable->insertRow(target);
		m_importResultTable->setItem(target, 0, new QTableWidgetItem(source));
		m_importResultTable->setItem(target, 1, new QTableWidgetItem(row > 0 ? QString::number(row) : QStringLiteral("-")));
		m_importResultTable->setItem(target, 2, new QTableWidgetItem(status));
		m_importResultTable->setItem(target, 3, new QTableWidgetItem(detail));
	};
	if (!imported.success()) {
		for (const auto& row : imported.rows)
			addResultRow(QFileInfo(QString::fromStdString(row.sourceFile)).fileName(), row.row,
				QStringLiteral("Rejected"), QString::fromStdString(row.context));
		if (imported.rows.empty())
			addResultRow(QFileInfo(sourcePath).fileName(), 0, QStringLiteral("Rejected"),
				QStringLiteral("The exact DAS and RouteChoice passenger pair could not be read"));
		QString message = firstDiagnosticMessage(imported.diagnostics);
		if (message.isEmpty())
			message = QStringLiteral("Passenger import reported errors.");
		m_prompts->showError(this, "Passenger Import Diagnostics", message, true);
		m_importResultTable->resizeColumnsToContents();
		emit statusMessage(QStringLiteral("Passenger import rejected; the scene was not changed"), 7000);
		return;
	}

	std::set<std::string> passengerIds;
	std::set<std::string> journeyIds;
	std::set<std::string> legIds;
	for (const auto& passenger : model.passengers) {
		passengerIds.insert(passenger.id);
		for (const auto& journey : passenger.journeys) {
			journeyIds.insert(journey.id);
			for (const auto& leg : journey.legs)
				legIds.insert(leg.id);
		}
	}
	int acceptedPassengers = 0;
	int rejectedPassengers = 0;
	std::set<std::string> rejectedPassengerIds;
	for (const auto& passenger : imported.passengers) {
		bool collision = passenger.id.empty() || passengerIds.count(passenger.id) > 0;
		std::set<std::string> candidateJourneyIds;
		std::set<std::string> candidateLegIds;
		for (const auto& journey : passenger.journeys) {
			if (journey.id.empty() || journeyIds.count(journey.id) > 0
				|| !candidateJourneyIds.insert(journey.id).second)
				collision = true;
			for (const auto& leg : journey.legs) {
				if (leg.id.empty() || legIds.count(leg.id) > 0
					|| !candidateLegIds.insert(leg.id).second)
					collision = true;
			}
		}
		if (collision) {
			++rejectedPassengers;
			if (!passenger.id.empty())
				rejectedPassengerIds.insert(passenger.id);
			addResultRow(QStringLiteral("%1 (batch)").arg(QFileInfo(sourcePath).fileName()), 0,
				QStringLiteral("Rejected"),
				QStringLiteral("Passenger, journey, or leg ID collides with existing or earlier imported data"));
			continue;
		}
		passengerIds.insert(passenger.id);
		journeyIds.insert(candidateJourneyIds.begin(), candidateJourneyIds.end());
		legIds.insert(candidateLegIds.begin(), candidateLegIds.end());
		model.passengers.push_back(passenger);
		++acceptedPassengers;
	}
	for (const auto& row : imported.rows) {
		const QString source = QFileInfo(QString::fromStdString(row.sourceFile)).fileName();
		const bool rejectedByBatch = rejectedPassengerIds.count(row.passengerId) > 0;
		const QString status = !row.accepted || rejectedByBatch ? QStringLiteral("Rejected")
																: (row.unresolvedReferences ? QStringLiteral("Unresolved") : QStringLiteral("Accepted"));
		const QString detail = rejectedByBatch
			? QStringLiteral("Passenger ID collides with existing or earlier imported data")
			: QString::fromStdString(row.context);
		addResultRow(source, row.row, status, detail);
	}
	if (imported.rows.empty() && imported.diagnostics.empty())
		addResultRow(QFileInfo(sourcePath).fileName(), 0, QStringLiteral("Rejected"),
			QStringLiteral("No passenger source rows were found"));
	for (const auto& report : imported.report)
		model.importReport.push_back(report);
	if (rejectedPassengers > 0) {
		model.importReport.push_back({"passengers.editor", sourcePath.toStdString(),
			acceptedPassengers + rejectedPassengers, acceptedPassengers, rejectedPassengers, 0});
	}
	if (acceptedPassengers > 0 || rejectedPassengers > 0 || !imported.report.empty()) {
		m_document->markChanged(SceneDocument::Passengers, "Import passengers");
		refresh();
		emit changed();
	}
	m_importResultTable->resizeColumnsToContents();
	emit statusMessage(QString("Passenger import: %1 accepted, %2 rejected; unresolved rows remain visible")
						   .arg(acceptedPassengers)
						   .arg(rejectedPassengers),
		7000);
}
