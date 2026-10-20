#include "editors/ServicePanel.h"

#include "scene/SceneIds.h"
#include "scene/SceneReferences.h"
#include "scene/SectionInventory.h"
#include "scene/RuntimeLimits.h"
#include "widgets/ChoiceComboBox.h"
#include "widgets/ColorChoiceButton.h"
#include "widgets/CompactDoubleSpinBox.h"
#include "widgets/DialogLayout.h"
#include "widgets/EditorLayout.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringList>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace {

constexpr int kMaxOccurrences = RuntimeLimits::kMaxExpandedTrains;

// A speed field that shows the stored value as it is and does not round it to the number of decimals.
class ServiceSpeedSpinBox : public CompactDoubleSpinBox {
public:
	using CompactDoubleSpinBox::CompactDoubleSpinBox;
	QString textFromValue(double value) const override {
		return QString::number(value, 'g', 6);
	}
	double valueFromText(const QString& text) const override {
		if (!lineEdit()->isModified() && text == textFromValue(value()))
			return value();
		return CompactDoubleSpinBox::valueFromText(text);
	}
};

// The route choice. Over the closed field the tip adds the stations of the selected route to the text of the field.
class RouteComboBox : public ChoiceComboBox {
public:
	using ChoiceComboBox::ChoiceComboBox;

protected:
	bool event(QEvent* event) override {
		if (event->type() == QEvent::ToolTip && !toolTip().isEmpty()) {
			QToolTip::showText(static_cast<QHelpEvent*>(event)->globalPos(), currentText() + QLatin1Char('\n') + toolTip(), this);
			return true;
		}
		return ChoiceComboBox::event(event);
	}
};

// The colour field of the form. A form that wraps long rows keeps a row beside its label while the field gets its minimum
// width, but then leaves the field that width less the spacing after the label, which cuts the Choose button. The field
// asks for that spacing as well.
class ServiceColorField : public ColorChoiceButton {
public:
	ServiceColorField(const QFormLayout* form, QWidget* parent) : ColorChoiceButton(parent), m_form(form) {}
	QSize minimumSizeHint() const override {
		int spacing = m_form->horizontalSpacing();
		if (spacing < 0)
			spacing = style()->combinedLayoutSpacing(QSizePolicy::Label, sizePolicy().controlType(), Qt::Horizontal, nullptr, parentWidget());
		return ColorChoiceButton::minimumSizeHint() + QSize(std::max(spacing, 0), 0);
	}

private:
	const QFormLayout* m_form;
};

// One entry of the route choice: where the route goes and in which direction, with the stations it traverses as tooltip.
void addRouteChoice(QComboBox* combo, const SceneModel& model, const SceneRoute& route, const SceneSectionInventory& inventory) {
	const SceneRouteStations traversal = sceneRouteStations(model, route, inventory);
	QStringList stations;
	for (const std::string& stationId : traversal.stationIds) {
		QString name = QString::fromStdString(stationId);
		for (const auto& station : model.stations)
			if (station.id == stationId && !station.name.empty()) {
				name = QString::fromStdString(station.name);
				break;
			}
		stations << name;
	}
	const QString description = !traversal.resolved ? QStringLiteral("Unresolved route")
		: stations.isEmpty()						? QStringLiteral("Route without station anchors")
													: QString("%1 → %2").arg(stations.first(), stations.last());
	const QString direction = traversal.direction > 0 ? QStringLiteral("forward")
		: traversal.direction < 0					  ? QStringLiteral("reverse")
													  : QStringLiteral("unknown direction");
	combo->addItem(QString("%1 · %2 [%3]").arg(description, direction, QString::fromStdString(route.id)), QString::fromStdString(route.id));
	combo->setItemData(combo->count() - 1,
		traversal.resolved ? QString("Traversed stations (not scheduled calls): %1")
								 .arg(stations.isEmpty() ? QStringLiteral("none") : stations.join(" → "))
						   : QStringLiteral("Station order unavailable: fix the route topology."),
		Qt::ToolTipRole);
}

} // namespace

ServicePanel::ServicePanel(QWidget* parent) : SceneEditorPanel(parent) {
	setObjectName("servicePanel");
	// The list sits above the tabs, so that a narrow dock keeps the fields, the tab names and the stop table whole.
	QVBoxLayout* serviceLayout = new QVBoxLayout(this);

	// top: the list of services and the buttons that manage it, in one row. The title of the dock names the list, which
	// shows about three rows, so that the tabs keep the height of a short dock, also when the Timetable tab is shown.
	QWidget* listPane = new QWidget(this);
	QVBoxLayout* listLayout = new QVBoxLayout(listPane);
	listLayout->setContentsMargins(0, 0, 0, 0);
	m_listWidget = new QListWidget(listPane);
	m_listWidget->setObjectName("serviceList");
	m_listWidget->setAccessibleName("Services");
	m_listWidget->setFixedHeight(m_listWidget->fontMetrics().lineSpacing() * 3 + 2 * m_listWidget->frameWidth());
	listLayout->addWidget(m_listWidget);
	QGridLayout* buttonLayout = new QGridLayout();
	m_addButton = new QPushButton("Add Service", listPane);
	m_addButton->setObjectName("addServiceButton");
	m_duplicateButton = new QPushButton("Duplicate", listPane);
	m_duplicateButton->setObjectName("duplicateServiceButton");
	m_deleteButton = new QPushButton("Delete", listPane);
	m_deleteButton->setObjectName("deleteServiceButton");
	buttonLayout->addWidget(m_addButton, 0, 0);
	buttonLayout->addWidget(m_duplicateButton, 0, 1);
	buttonLayout->addWidget(m_deleteButton, 0, 2);
	listLayout->addLayout(buttonLayout);
	serviceLayout->addWidget(listPane);

	// below: the fields of the selected service, its timetable and its occurrences on three tabs
	QWidget* detailPane = new QWidget(this);
	QVBoxLayout* detailLayout = new QVBoxLayout(detailPane);
	m_emptyLabel = EditorLayout::emptyStateLabel(detailPane);
	m_emptyLabel->setObjectName("serviceEmptyStateLabel");
	detailLayout->addWidget(m_emptyLabel);
	QFormLayout* form = new QFormLayout();
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	detailLayout->addLayout(form);
	detailLayout->addStretch();

	m_idEdit = new QLineEdit(detailPane);
	m_idEdit->setObjectName("serviceIdEdit");
	form->addRow("Service Id", m_idEdit);
	m_operatingCodeEdit = new QLineEdit(detailPane);
	m_operatingCodeEdit->setObjectName("serviceOperatingCodeEdit");
	form->addRow("Service code (number)", m_operatingCodeEdit);
	m_categoryCombo = new ChoiceComboBox(detailPane);
	m_categoryCombo->setObjectName("serviceCategoryCombo");
	form->addRow("Category", m_categoryCombo);
	m_colorButton = new ServiceColorField(form, detailPane);
	m_colorButton->setObjectName("serviceColorButton");
	form->addRow("Service visualization colour", m_colorButton);
	m_compositionCombo = new ChoiceComboBox(detailPane);
	m_compositionCombo->setObjectName("serviceCompositionCombo");
	form->addRow("Composition", m_compositionCombo);
	m_routeCombo = new RouteComboBox(detailPane);
	m_routeCombo->setObjectName("serviceRouteCombo");
	form->addRow("Route", m_routeCombo);

	m_hasEntryTimeCheck = new QCheckBox("Entry Time (s)", detailPane);
	m_hasEntryTimeCheck->setObjectName("serviceHasEntryTimeCheck");
	m_entryTimeSecondsEdit = new QLineEdit(detailPane);
	m_entryTimeSecondsEdit->setObjectName("serviceEntryTimeSecondsEdit");
	m_entryTimeSecondsEdit->setValidator(new QIntValidator(0, std::numeric_limits<int>::max(), m_entryTimeSecondsEdit));
	form->addRow(m_hasEntryTimeCheck, m_entryTimeSecondsEdit);

	m_hasRepeatCheck = new QCheckBox("Repeat Headway (s)", detailPane);
	m_hasRepeatCheck->setObjectName("serviceHasRepeatCheck");
	m_headwaySecondsEdit = new QLineEdit(detailPane);
	m_headwaySecondsEdit->setObjectName("serviceHeadwaySecondsEdit");
	m_headwaySecondsEdit->setValidator(new QIntValidator(0, std::numeric_limits<int>::max(), m_headwaySecondsEdit));
	form->addRow(m_hasRepeatCheck, m_headwaySecondsEdit);

	m_hasRepeatCountCheck = new QCheckBox("Configured total", detailPane);
	m_hasRepeatCountCheck->setObjectName("serviceHasRepeatCountCheck");
	m_repeatCountEdit = new QLineEdit(detailPane);
	m_repeatCountEdit->setObjectName("serviceRepeatCountEdit");
	m_repeatCountEdit->setValidator(new QIntValidator(1, std::numeric_limits<int>::max(), m_repeatCountEdit));
	form->addRow(m_hasRepeatCountCheck, m_repeatCountEdit);

	m_performancePercentEdit = new CompactDoubleSpinBox(detailPane);
	m_performancePercentEdit->setObjectName("servicePerformancePercentEdit");
	m_performancePercentEdit->setRange(1.0, 100.0);
	m_performancePercentEdit->setDecimals(std::numeric_limits<double>::max_digits10);
	m_performancePercentEdit->setValue(100);
	m_performancePercentEdit->setKeyboardTracking(false);
	form->addRow("Running performance (parameter) %", m_performancePercentEdit);

	m_hasMaximumSpeedCheck = new QCheckBox("Maximum speed restriction km/h", detailPane);
	m_hasMaximumSpeedCheck->setObjectName("serviceHasMaximumSpeedCheck");
	m_maximumSpeedKmhEdit = new ServiceSpeedSpinBox(detailPane);
	m_maximumSpeedKmhEdit->setObjectName("serviceMaximumSpeedKmhEdit");
	m_maximumSpeedKmhEdit->setRange(0.1, 1000.0);
	m_maximumSpeedKmhEdit->setDecimals(std::numeric_limits<double>::max_digits10);
	m_maximumSpeedKmhEdit->setValue(100.0);
	form->addRow(m_hasMaximumSpeedCheck, m_maximumSpeedKmhEdit);

	m_hasOperatingCodeStepCheck = new QCheckBox("Service code (number) step", detailPane);
	m_hasOperatingCodeStepCheck->setObjectName("serviceHasOperatingCodeStepCheck");
	m_operatingCodeStepEdit = new QLineEdit(detailPane);
	m_operatingCodeStepEdit->setObjectName("serviceOperatingCodeStepEdit");
	m_operatingCodeStepEdit->setValidator(
		new QIntValidator(-std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), m_operatingCodeStepEdit));
	form->addRow(m_hasOperatingCodeStepCheck, m_operatingCodeStepEdit);

	// the fields are tall, so let them scroll instead of overflowing the dock and overlapping the tab bar
	QScrollArea* detailScroll = new QScrollArea(this);
	detailScroll->setWidgetResizable(true);
	detailScroll->setFrameShape(QFrame::NoFrame);
	detailScroll->setWidget(detailPane);
	m_pages = new QTabWidget(this);
	m_pages->setObjectName("serviceEditorPages");
	m_pages->setMinimumSize(0, 0);
	// Tabs that do not fit keep their whole names and scroll.
	m_pages->setUsesScrollButtons(true);
	m_pages->setElideMode(Qt::ElideNone);
	m_pages->addTab(detailScroll, "Service");

	QWidget* timetablePane = new QWidget(this);
	m_timetableLayout = new QVBoxLayout(timetablePane);
	m_pages->addTab(timetablePane, "Timetable");

	QWidget* occurrencePane = new QWidget(this);
	QVBoxLayout* occurrenceLayout = new QVBoxLayout(occurrencePane);
	occurrenceLayout->addWidget(new QLabel("Run occurrences", occurrencePane));
	m_occurrenceSelectionLabel = new QLabel(occurrencePane);
	m_occurrenceSelectionLabel->setObjectName("serviceOccurrenceSelectionLabel");
	occurrenceLayout->addWidget(m_occurrenceSelectionLabel);
	m_occurrenceTable = new QTableWidget(occurrencePane);
	m_occurrenceTable->setObjectName("serviceOccurrenceTable");
	m_occurrenceTable->setColumnCount(6);
	m_occurrenceTable->setHorizontalHeaderLabels({"Include", "Service code (number)", "Generated service", "Scheduled entry",
		"Running performance (parameter) %", "Maximum speed restriction (km/h)"});
	m_occurrenceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_occurrenceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_occurrenceTable->setAlternatingRowColors(true);
	m_occurrenceTable->horizontalHeader()->setStretchLastSection(true);
	occurrenceLayout->addWidget(m_occurrenceTable, 1);
	QHBoxLayout* occurrenceButtonLayout = new QHBoxLayout();
	m_selectAllButton = new QPushButton("Select all", occurrencePane);
	m_selectAllButton->setObjectName("selectAllOccurrencesButton");
	m_selectNoneButton = new QPushButton("Select none", occurrencePane);
	m_selectNoneButton->setObjectName("selectNoneOccurrencesButton");
	occurrenceButtonLayout->addWidget(m_selectAllButton);
	occurrenceButtonLayout->addWidget(m_selectNoneButton);
	occurrenceLayout->addLayout(occurrenceButtonLayout);
	m_pages->addTab(occurrencePane, "Run occurrences");
	serviceLayout->addWidget(m_pages, 1);

	connect(m_listWidget, &QListWidget::currentRowChanged, this, [this](int) { updateDetail(); });
	connect(m_idEdit, &QLineEdit::editingFinished, this, &ServicePanel::commitIdEdit);
	connect(m_operatingCodeEdit, &QLineEdit::editingFinished, this, &ServicePanel::commitOperatingCode);
	connect(m_categoryCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &ServicePanel::commitCategory);
	connect(m_colorButton, &ColorChoiceButton::colorChanged, this, &ServicePanel::commitVisualizationColor);
	connect(m_compositionCombo, &QComboBox::currentTextChanged, this, &ServicePanel::commitComposition);
	connect(m_routeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &ServicePanel::commitRoute);
	connect(m_hasEntryTimeCheck, &QCheckBox::toggled, this, &ServicePanel::commitHasEntryTime);
	connect(m_entryTimeSecondsEdit, &QLineEdit::editingFinished, this, &ServicePanel::commitEntryTimeSeconds);
	connect(m_hasRepeatCheck, &QCheckBox::toggled, this, &ServicePanel::commitHasRepeat);
	connect(m_headwaySecondsEdit, &QLineEdit::editingFinished, this, &ServicePanel::commitHeadwaySeconds);
	connect(m_hasRepeatCountCheck, &QCheckBox::toggled, this, &ServicePanel::commitHasRepeatCount);
	connect(m_repeatCountEdit, &QLineEdit::editingFinished, this, &ServicePanel::commitRepeatCount);
	connect(m_performancePercentEdit, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &ServicePanel::commitPerformancePercent);
	connect(m_hasMaximumSpeedCheck, &QCheckBox::toggled, this, &ServicePanel::commitHasMaximumSpeed);
	connect(m_maximumSpeedKmhEdit, &QAbstractSpinBox::editingFinished, this, &ServicePanel::commitMaximumSpeed);
	connect(m_hasOperatingCodeStepCheck, &QCheckBox::toggled, this, &ServicePanel::commitHasOperatingCodeStep);
	connect(m_operatingCodeStepEdit, &QLineEdit::editingFinished, this, &ServicePanel::commitOperatingCodeStep);
	connect(m_addButton, &QPushButton::clicked, this, &ServicePanel::addService);
	connect(m_duplicateButton, &QPushButton::clicked, this, &ServicePanel::duplicateService);
	connect(m_deleteButton, &QPushButton::clicked, this, &ServicePanel::deleteService);
	connect(m_occurrenceTable, &QTableWidget::itemChanged, this, &ServicePanel::updateOccurrenceSelection);
	connect(m_selectAllButton, &QPushButton::clicked, this, &ServicePanel::selectAllOccurrences);
	connect(m_selectNoneButton, &QPushButton::clicked, this, &ServicePanel::selectNoneOccurrences);
	ServicePanel::refresh();
}

void ServicePanel::setDocument(SceneDocument* document) {
	QObject::disconnect(m_replacedConnection);
	m_document = document;
	if (m_document)
		m_replacedConnection = connect(m_document, &SceneDocument::replaced, this, &ServicePanel::refresh);
	refresh();
}

void ServicePanel::setPrompts(UserPrompts* prompts) {
	m_prompts = prompts ? prompts : &m_ownPrompts;
}

void ServicePanel::setRunSelection(RunSelection* selection) {
	m_runSelection = selection ? selection : &m_ownRunSelection;
}

void ServicePanel::setOccurrenceDuration(std::function<double()> duration) {
	m_occurrenceDuration = std::move(duration);
}

void ServicePanel::setTimetableWidget(QWidget* widget) {
	m_timetableLayout->addWidget(widget);
}

bool ServicePanel::hasScene() const {
	return m_document && m_document->isLoaded();
}

bool ServicePanel::canEdit() const {
	return hasScene() && m_document->isEditable();
}

int ServicePanel::selectedRow() const {
	const int row = m_listWidget->currentRow();
	if (!hasScene() || row < 0 || row >= static_cast<int>(m_document->model().services.size()))
		return -1;
	return row;
}

double ServicePanel::occurrenceDuration() const {
	return m_occurrenceDuration ? m_occurrenceDuration() : m_document->model().settings.durationSeconds;
}

void ServicePanel::refresh() {
	const bool editable = canEdit();
	{
		const int previousRow = m_listWidget->currentRow();
		const QString previousId = m_listWidget->currentItem() ? m_listWidget->currentItem()->text() : QString();
		const QSignalBlocker blocker(m_listWidget);
		m_listWidget->clear();
		if (hasScene()) {
			for (const auto& service : m_document->model().services)
				m_listWidget->addItem(QString::fromStdString(service.id));
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
		if (const int sameService = rowOfEntry(m_listWidget, previousId, previousRow); sameService >= 0)
			rowToSelect = sameService;
		m_listWidget->setCurrentRow(rowToSelect);
		m_listWidget->setEnabled(editable);
	}
	m_addButton->setEnabled(editable);
	updateDetail();
	refreshOccurrencePreview();
}

void ServicePanel::updateDetail() {
	const int row = selectedRow();
	const bool hasSelection = row >= 0;
	const bool editorAvailable = hasSelection && canEdit();
	const SceneModel noScene;
	const SceneModel& model = m_document ? m_document->model() : noScene;
	const SceneService* service = hasSelection ? &model.services[static_cast<std::size_t>(row)] : nullptr;

	QString emptyText;
	if (!service) {
		if (!hasScene())
			emptyText = QStringLiteral("Open a case to edit services.");
		else if (canEdit())
			emptyText = QStringLiteral("Select a service or use Add Service.");
		else
			emptyText = QStringLiteral("No services.");
	}
	DialogLayout::setHelp(m_emptyLabel, emptyText);

	{
		const QSignalBlocker blocker(m_idEdit);
		m_idEdit->setText(service ? QString::fromStdString(service->id) : QString());
		// A long id shows its start.
		m_idEdit->setCursorPosition(0);
		m_idEdit->setEnabled(editorAvailable);
	}

	{
		const QSignalBlocker blocker(m_operatingCodeEdit);
		m_operatingCodeEdit->setText(service ? QString::fromStdString(service->operatingCode) : QString());
		m_operatingCodeEdit->setCursorPosition(0);
		m_operatingCodeEdit->setEnabled(editorAvailable);
	}

	{
		const QSignalBlocker blocker(m_categoryCombo);
		m_categoryCombo->clear();
		m_categoryCombo->addItem("No category", QString());
		for (const QString& category : {QStringLiteral("Intercity"), QStringLiteral("Regional"),
				 QStringLiteral("High speed/international"), QStringLiteral("Freight"),
				 QStringLiteral("Metro/urban"), QStringLiteral("Suburban")})
			m_categoryCombo->addItem(category, category);
		int categoryIndex = 0;
		if (service) {
			const QString currentCategory = QString::fromStdString(service->category);
			categoryIndex = m_categoryCombo->findData(currentCategory);
			if (categoryIndex < 0 && !currentCategory.isEmpty()) {
				m_categoryCombo->addItem(QString("Unknown: %1").arg(currentCategory), currentCategory);
				categoryIndex = m_categoryCombo->count() - 1;
			}
			if (categoryIndex < 0)
				categoryIndex = 0;
		}
		m_categoryCombo->setCurrentIndex(categoryIndex);
		m_categoryCombo->setEnabled(editorAvailable);
	}

	m_colorButton->setColor(service ? service->visualizationColor : std::string());
	m_colorButton->setEnabled(editorAvailable);

	{
		const QSignalBlocker blocker(m_compositionCombo);
		m_compositionCombo->clear();
		for (const auto& composition : model.compositions)
			m_compositionCombo->addItem(QString::fromStdString(composition.id));
		if (service) {
			const QString currentComposition = QString::fromStdString(service->composition);
			if (m_compositionCombo->findText(currentComposition) < 0)
				m_compositionCombo->addItem(currentComposition); // dangling reference, still shown/selectable
			m_compositionCombo->setCurrentText(currentComposition);
		}
		m_compositionCombo->setEnabled(editorAvailable);
	}

	{
		const QSignalBlocker blocker(m_routeCombo);
		m_routeCombo->clear();
		const auto inventory = buildSceneSectionInventory(model);
		for (const auto& route : model.routes)
			addRouteChoice(m_routeCombo, model, route, inventory);
		if (service) {
			const QString currentRoute = QString::fromStdString(service->route);
			if (m_routeCombo->findData(currentRoute) < 0)
				m_routeCombo->addItem(QString("Missing route [%1]").arg(currentRoute), currentRoute);
			m_routeCombo->setCurrentIndex(m_routeCombo->findData(currentRoute));
		}
		m_routeCombo->setToolTip(m_routeCombo->currentData(Qt::ToolTipRole).toString());
		m_routeCombo->setEnabled(editorAvailable);
	}

	const bool hasEntryTime = service && service->hasEntryTime;
	{
		const QSignalBlocker blocker(m_hasEntryTimeCheck);
		m_hasEntryTimeCheck->setChecked(hasEntryTime);
		m_hasEntryTimeCheck->setEnabled(editorAvailable);
	}
	{
		const QSignalBlocker blocker(m_entryTimeSecondsEdit);
		const int seconds = service ? static_cast<int>(service->entryTimeSeconds) : 0;
		m_entryTimeSecondsEdit->setText(QString::number(seconds));
		m_entryTimeSecondsEdit->setEnabled(editorAvailable && hasEntryTime);
	}

	const bool hasRepeat = service && service->hasRepeat;
	{
		const QSignalBlocker blocker(m_hasRepeatCheck);
		m_hasRepeatCheck->setChecked(hasRepeat);
		m_hasRepeatCheck->setEnabled(editorAvailable);
	}
	{
		const QSignalBlocker blocker(m_headwaySecondsEdit);
		const int seconds = service ? static_cast<int>(service->headwaySeconds) : 0;
		m_headwaySecondsEdit->setText(QString::number(seconds));
		m_headwaySecondsEdit->setEnabled(editorAvailable && hasRepeat);
	}

	const bool hasRepeatCount = service && service->hasRepeatCount;
	{
		const QSignalBlocker blocker(m_hasRepeatCountCheck);
		m_hasRepeatCountCheck->setChecked(hasRepeatCount);
		m_hasRepeatCountCheck->setEnabled(editorAvailable && hasRepeat);
	}
	{
		const QSignalBlocker blocker(m_repeatCountEdit);
		const int count = service ? service->repeatCount : 1;
		m_repeatCountEdit->setText(QString::number(std::max(1, count)));
		m_repeatCountEdit->setEnabled(editorAvailable && hasRepeat && hasRepeatCount);
	}

	{
		const QSignalBlocker blocker(m_performancePercentEdit);
		m_performancePercentEdit->setValue(service ? std::clamp(service->performancePercent, 1.0, 100.0) : 100.0);
		m_performancePercentEdit->setEnabled(editorAvailable);
	}

	const bool hasMaximumSpeed = service && service->hasMaximumSpeed;
	{
		const QSignalBlocker blocker(m_hasMaximumSpeedCheck);
		m_hasMaximumSpeedCheck->setChecked(hasMaximumSpeed);
		m_hasMaximumSpeedCheck->setEnabled(editorAvailable);
	}
	{
		const QSignalBlocker blocker(m_maximumSpeedKmhEdit);
		m_maximumSpeedKmhEdit->setValue(service ? std::max(0.1, service->maximumSpeedKmh) : 100.0);
		m_maximumSpeedKmhEdit->setEnabled(editorAvailable && hasMaximumSpeed);
	}

	const bool hasOperatingCodeStep = service && service->hasOperatingCodeStep;
	{
		const QSignalBlocker blocker(m_hasOperatingCodeStepCheck);
		m_hasOperatingCodeStepCheck->setChecked(hasOperatingCodeStep);
		m_hasOperatingCodeStepCheck->setEnabled(editorAvailable && hasRepeat);
	}
	{
		const QSignalBlocker blocker(m_operatingCodeStepEdit);
		const int step = service ? service->operatingCodeStep : 1;
		m_operatingCodeStepEdit->setText(QString::number(step == 0 ? 1 : step));
		m_operatingCodeStepEdit->setEnabled(editorAvailable && hasRepeat && hasOperatingCodeStep);
	}

	m_duplicateButton->setEnabled(editorAvailable);
	m_deleteButton->setEnabled(editorAvailable);

	// the timetable is part of the service detail, so it follows the selected service
	emit selectedServiceChanged(service ? QString::fromStdString(service->id) : QString());
}

QString ServicePanel::generatedServiceLabel(const SceneService& service, int occurrence) {
	const std::string code = sceneServiceOccurrenceOperatingCode(service, occurrence);
	const double entry = sceneServiceScheduledEntry(service, occurrence);
	const QString entryText = std::isfinite(entry)
		? QString("+%1 s").arg(QString::number(entry, 'g', 12))
		: QStringLiteral("(invalid scheduled entry)");
	return QString("%1 | route %2 | entry %3")
		.arg(code.empty() ? QStringLiteral("(unavailable)") : QString::fromStdString(code),
			QString::fromStdString(service.route), entryText);
}

void ServicePanel::refreshOccurrencePreview() {
	if (!hasScene()) {
		m_updatingOccurrencePreview = true;
		m_occurrenceTable->clearContents();
		m_occurrenceTable->setRowCount(0);
		m_updatingOccurrencePreview = false;
		m_occurrenceSelectionLabel->setText(QStringLiteral("No scene loaded"));
		m_selectAllButton->setEnabled(false);
		m_selectNoneButton->setEnabled(false);
		return;
	}

	const SceneModel& model = m_document->model();
	const double durationSeconds = occurrenceDuration();
	m_runSelection->pruneExcluded(model, durationSeconds);
	m_updatingOccurrencePreview = true;
	m_occurrenceTable->clearContents();

	const int totalOccurrences = sceneTotalOccurrences(model, durationSeconds);
	const int displayedOccurrences = std::min(totalOccurrences, kMaxOccurrences);
	if (totalOccurrences > kMaxOccurrences)
		m_runSelection->includeAll();
	m_occurrenceTable->setRowCount(displayedOccurrences);
	int row = 0;
	for (const SceneService& service : model.services) {
		const int count = std::min(std::max(0, sceneServiceOccurrenceCount(service, durationSeconds)), displayedOccurrences - row);
		for (int occurrence = 1; occurrence <= count; ++occurrence, ++row) {
			SceneServiceOccurrence value;
			value.serviceId = service.id;
			value.occurrence = occurrence;
			const bool checked = !m_runSelection->isExcluded(value);
			auto* include = new QTableWidgetItem();
			include->setFlags(include->flags() | Qt::ItemIsUserCheckable);
			include->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
			include->setData(Qt::UserRole, QString::fromStdString(service.id));
			include->setData(Qt::UserRole + 1, occurrence);
			m_occurrenceTable->setItem(row, 0, include);
			const std::string code = sceneServiceOccurrenceOperatingCode(service, occurrence);
			m_occurrenceTable->setItem(row, 1,
				new QTableWidgetItem(code.empty() ? QStringLiteral("(unavailable)") : QString::fromStdString(code)));
			m_occurrenceTable->setItem(row, 2, new QTableWidgetItem(QString("route %1").arg(QString::fromStdString(service.route))));
			const double entry = sceneServiceScheduledEntry(service, occurrence);
			m_occurrenceTable->setItem(row, 3,
				new QTableWidgetItem(std::isfinite(entry) ? QString("+%1 s").arg(QString::number(entry, 'g', 12))
														  : QStringLiteral("(invalid scheduled entry)")));
			m_occurrenceTable->setItem(row, 4,
				new QTableWidgetItem(QString::number(static_cast<double>(service.performancePercent), 'g', 6)));
			m_occurrenceTable->setItem(row, 5,
				new QTableWidgetItem(service.hasMaximumSpeed ? QString::number(service.maximumSpeedKmh, 'g', 6) : QStringLiteral("-")));
			for (int column = 0; column < m_occurrenceTable->columnCount(); ++column)
				m_occurrenceTable->item(row, column)
					->setToolTip(generatedServiceLabel(service, occurrence)
						+ QString(" [%1, #%2]").arg(QString::fromStdString(service.id)).arg(occurrence));
		}
		if (row >= displayedOccurrences)
			break;
	}
	m_occurrenceTable->resizeColumnsToContents();
	m_updatingOccurrencePreview = false;
	QString text = QString("Configured total: %1; Number of services in sim.: %2; Selected: %3; Selected in period: %4.\n"
						   "Counting rule: scheduled entry ≥ 0 and < %5 s; these are configured identities, not observed trains.")
					   .arg(totalOccurrences)
					   .arg(sceneInPeriodOccurrences(model, durationSeconds))
					   .arg(m_runSelection->selectedCount(model, durationSeconds))
					   .arg(m_runSelection->selectedInPeriodCount(model, durationSeconds))
					   .arg(QString::number(durationSeconds, 'g', 12));
	if (totalOccurrences > displayedOccurrences)
		text += QString("\nPreview limited to %1 rows; generation is not truncated. Reduce the configured pattern before Run.")
					.arg(displayedOccurrences);
	m_occurrenceSelectionLabel->setText(text);
	m_occurrenceSelectionLabel->setWordWrap(true);
	const bool controlsEnabled = canEdit() && totalOccurrences <= kMaxOccurrences;
	m_occurrenceTable->setEnabled(controlsEnabled);
	m_selectAllButton->setEnabled(controlsEnabled);
	m_selectNoneButton->setEnabled(controlsEnabled);
	emit occurrencesChanged();
}

void ServicePanel::updateOccurrenceSelection(QTableWidgetItem* item) {
	if (m_updatingOccurrencePreview || !item || item->column() != 0)
		return;
	SceneServiceOccurrence value;
	value.serviceId = item->data(Qt::UserRole).toString().toStdString();
	value.occurrence = item->data(Qt::UserRole + 1).toInt();
	m_runSelection->setExcluded(value, item->checkState() != Qt::Checked);
	refreshOccurrencePreview();
}

void ServicePanel::selectAllOccurrences() {
	if (!m_document || !m_document->isEditable())
		return;
	m_runSelection->includeAll();
	refreshOccurrencePreview();
}

void ServicePanel::selectNoneOccurrences() {
	if (!m_document || !m_document->isEditable() || sceneTotalOccurrences(m_document->model(), occurrenceDuration()) > kMaxOccurrences)
		return;
	m_runSelection->excludeAll(m_document->model(), occurrenceDuration());
	refreshOccurrencePreview();
}

void ServicePanel::addService() {
	if (!hasScene())
		return;
	SceneModel& model = m_document->mutableModel();
	SceneService service;
	service.id = sceneUniqueServiceId(model, "new_service");
	model.services.push_back(service);

	m_document->markChanged(SceneDocument::Services, "Add service");
	refresh();
	emit changed();

	m_listWidget->setCurrentRow(static_cast<int>(model.services.size()) - 1);
}

void ServicePanel::duplicateService() {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneModel& model = m_document->mutableModel();
	SceneService duplicate = model.services[static_cast<std::size_t>(row)];
	duplicate.id = sceneUniqueServiceId(model, duplicate.id + "_copy");
	model.services.insert(model.services.begin() + row + 1, duplicate);

	m_document->markChanged(SceneDocument::Services, "Duplicate service");
	refresh();
	emit changed();

	m_listWidget->setCurrentRow(row + 1);
}

void ServicePanel::deleteService() {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneModel& model = m_document->mutableModel();
	const std::string id = model.services[static_cast<std::size_t>(row)].id;
	QStringList consumers;
	for (const std::string& consumer : sceneDirectDeleteConsumers(model, "service", id))
		consumers << QString::fromStdString(consumer);
	if (!consumers.isEmpty()) {
		m_prompts->showError(this, "Cannot Delete Service",
			QString("Cannot delete service '%1' because it is still referenced by:\n• %2\nRemove those operation references first.")
				.arg(QString::fromStdString(id), consumers.join("\n• ")),
			true);
		return;
	}
	if (!m_prompts->confirm(this, "Delete Service", QString("Delete service '%1'?").arg(QString::fromStdString(id))))
		return;

	model.services.erase(model.services.begin() + row);
	m_runSelection->pruneExcluded(model, occurrenceDuration());

	m_document->markChanged(SceneDocument::Services, "Delete service");
	refresh();
	emit changed();
}

void ServicePanel::commitIdEdit() {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneModel& model = m_document->mutableModel();
	const std::string oldId = model.services[static_cast<std::size_t>(row)].id;
	const std::string newId = m_idEdit->text().trimmed().toStdString();
	if (newId.empty()) {
		const QSignalBlocker blocker(m_idEdit);
		m_idEdit->setText(QString::fromStdString(oldId));
		m_prompts->showError(this, "Invalid Service ID", "Service IDs must not be empty.", true);
		return;
	}
	if (newId == oldId)
		return;
	if (sceneUniqueServiceId(model, newId) != newId) {
		const QSignalBlocker blocker(m_idEdit);
		m_idEdit->setText(QString::fromStdString(oldId));
		m_prompts->showError(this, "Service ID already exists", "Choose a unique service ID.", true);
		return;
	}
	sceneRewriteServiceReferences(model, oldId, newId);
	m_runSelection->serviceRenamed(oldId, newId);
	model.services[static_cast<std::size_t>(row)].id = newId;

	// update the list row label in place instead of rebuilding the panel, so a
	// focus-out that lands on another control keeps that pending click intact
	if (QListWidgetItem* item = m_listWidget->item(row)) {
		const QSignalBlocker blocker(m_listWidget);
		item->setText(QString::fromStdString(newId));
	}

	m_document->markChanged(SceneDocument::Services | SceneDocument::Scenarios | SceneDocument::Passengers, "Rename service");
	refreshOccurrencePreview();
	emit selectedServiceChanged(QString::fromStdString(newId));
	emit serviceRenamed(QString::fromStdString(oldId), QString::fromStdString(newId));
	emit changed();
}

void ServicePanel::commitOperatingCode() {
	const int row = selectedRow();
	if (row < 0)
		return;
	const std::string value = m_operatingCodeEdit->text().trimmed().toStdString();
	SceneService& service = m_document->mutableModel().services[static_cast<std::size_t>(row)];
	if (value == service.operatingCode)
		return;
	service.operatingCode = value;
	m_document->markChanged(SceneDocument::Services, "Change service code");
	refreshOccurrencePreview();
	emit changed();
}

void ServicePanel::commitCategory(int index) {
	const int row = selectedRow();
	if (!canEdit() || row < 0 || index < 0)
		return;
	const std::string value = m_categoryCombo->itemData(index).toString().toStdString();
	SceneService& service = m_document->mutableModel().services[static_cast<std::size_t>(row)];
	if (value == service.category)
		return;
	service.category = value;
	m_document->markChanged(SceneDocument::Services, "Change service category");
	emit changed();
}

void ServicePanel::commitVisualizationColor(const QString& text) {
	const int row = selectedRow();
	if (!canEdit() || row < 0)
		return;
	const std::string value = text.toStdString();
	SceneService& service = m_document->mutableModel().services[static_cast<std::size_t>(row)];
	if (value == service.visualizationColor)
		return;
	service.visualizationColor = value;
	m_document->markChanged(SceneDocument::Services, "Change service colour");
	emit changed();
}

void ServicePanel::commitComposition(const QString& text) {
	const int row = selectedRow();
	if (row < 0)
		return;
	const std::string value = text.toStdString();
	SceneService& service = m_document->mutableModel().services[static_cast<std::size_t>(row)];
	if (value == service.composition)
		return;
	service.composition = value;

	// the combo already shows the chosen value and the service list labels are
	// unchanged, so do not rebuild the panel here (that would close the popup)
	m_document->markChanged(SceneDocument::Services, "Change service composition");
	emit changed();
}

void ServicePanel::commitRoute(int index) {
	const int row = selectedRow();
	if (!canEdit() || row < 0 || index < 0)
		return;
	const std::string value = m_routeCombo->itemData(index).toString().toStdString();
	SceneService& service = m_document->mutableModel().services[static_cast<std::size_t>(row)];
	if (value == service.route)
		return;
	service.route = value;
	m_routeCombo->setToolTip(m_routeCombo->itemData(index, Qt::ToolTipRole).toString());

	// the combo already shows the chosen value and the service list labels are
	// unchanged, so do not rebuild the panel here (that would close the popup)
	m_document->markChanged(SceneDocument::Services, "Change service route");
	refreshOccurrencePreview();
	emit selectedServiceChanged(QString::fromStdString(service.id));
	emit changed();
}

void ServicePanel::commitHasEntryTime(bool checked) {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneService& service = m_document->mutableModel().services[static_cast<std::size_t>(row)];
	if (checked == service.hasEntryTime)
		return;
	service.hasEntryTime = checked;
	m_entryTimeSecondsEdit->setEnabled(checked);

	m_document->markChanged(SceneDocument::Services, "Change service entry time");
	refreshOccurrencePreview();
	emit changed();
}

void ServicePanel::commitEntryTimeSeconds() {
	const int row = selectedRow();
	if (row < 0)
		return;
	bool ok = false;
	int seconds = m_entryTimeSecondsEdit->text().toInt(&ok);
	if (!ok)
		seconds = 0;

	// normalize a blank or partial entry back to a plain integer display
	{
		const QSignalBlocker blocker(m_entryTimeSecondsEdit);
		m_entryTimeSecondsEdit->setText(QString::number(seconds));
	}

	const double newValue = static_cast<double>(seconds);
	SceneService& service = m_document->mutableModel().services[static_cast<std::size_t>(row)];
	if (newValue == service.entryTimeSeconds)
		return;
	service.entryTimeSeconds = newValue;

	m_document->markChanged(SceneDocument::Services, "Change service entry time");
	refreshOccurrencePreview();
	emit changed();
}

void ServicePanel::commitHasRepeat(bool checked) {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneService& service = m_document->mutableModel().services[static_cast<std::size_t>(row)];
	if (checked == service.hasRepeat)
		return;
	service.hasRepeat = checked;
	if (!checked) {
		service.hasRepeatCount = false;
		service.hasOperatingCodeStep = false;
	}
	const bool editable = canEdit();
	m_headwaySecondsEdit->setEnabled(checked);
	{
		const QSignalBlocker blocker(m_hasRepeatCountCheck);
		m_hasRepeatCountCheck->setChecked(checked && service.hasRepeatCount);
		m_hasRepeatCountCheck->setEnabled(checked && editable);
	}
	m_repeatCountEdit->setEnabled(checked && service.hasRepeatCount && editable);
	{
		const QSignalBlocker blocker(m_hasOperatingCodeStepCheck);
		m_hasOperatingCodeStepCheck->setChecked(checked && service.hasOperatingCodeStep);
		m_hasOperatingCodeStepCheck->setEnabled(checked && editable);
	}
	m_operatingCodeStepEdit->setEnabled(checked && service.hasOperatingCodeStep && editable);

	m_document->markChanged(SceneDocument::Services, "Change service repeat");
	refreshOccurrencePreview();
	emit changed();
}

void ServicePanel::commitHeadwaySeconds() {
	const int row = selectedRow();
	if (row < 0)
		return;
	bool ok = false;
	int seconds = m_headwaySecondsEdit->text().toInt(&ok);
	if (!ok)
		seconds = 0;

	// normalize a blank or partial entry back to a plain integer display
	{
		const QSignalBlocker blocker(m_headwaySecondsEdit);
		m_headwaySecondsEdit->setText(QString::number(seconds));
	}

	const double newValue = static_cast<double>(seconds);
	SceneService& service = m_document->mutableModel().services[static_cast<std::size_t>(row)];
	if (newValue == service.headwaySeconds)
		return;
	service.headwaySeconds = newValue;

	m_document->markChanged(SceneDocument::Services, "Change service headway");
	refreshOccurrencePreview();
	emit changed();
}

void ServicePanel::commitHasRepeatCount(bool checked) {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneService& service = m_document->mutableModel().services[static_cast<std::size_t>(row)];
	if (checked == service.hasRepeatCount)
		return;
	service.hasRepeatCount = checked;
	if (checked && service.repeatCount < 1)
		service.repeatCount = 1;
	{
		const QSignalBlocker blocker(m_repeatCountEdit);
		m_repeatCountEdit->setText(QString::number(std::max(1, service.repeatCount)));
		m_repeatCountEdit->setEnabled(checked && canEdit());
	}
	m_document->markChanged(SceneDocument::Services, "Change service repeat count");
	refreshOccurrencePreview();
	emit changed();
}

void ServicePanel::commitRepeatCount() {
	const int row = selectedRow();
	if (row < 0)
		return;
	bool ok = false;
	int count = m_repeatCountEdit->text().toInt(&ok);
	if (!ok || count < 1)
		count = 1;
	{
		const QSignalBlocker blocker(m_repeatCountEdit);
		m_repeatCountEdit->setText(QString::number(count));
	}
	SceneService& service = m_document->mutableModel().services[static_cast<std::size_t>(row)];
	if (count == service.repeatCount)
		return;
	service.repeatCount = count;
	m_document->markChanged(SceneDocument::Services, "Change service repeat count");
	refreshOccurrencePreview();
	emit changed();
}

void ServicePanel::commitPerformancePercent(double value) {
	const int row = selectedRow();
	if (row < 0)
		return;
	value = std::clamp(value, 1.0, 100.0);
	SceneService& service = m_document->mutableModel().services[static_cast<std::size_t>(row)];
	if (value == service.performancePercent)
		return;
	service.performancePercent = value;
	m_document->markChanged(SceneDocument::Services, "Change service performance");
	refreshOccurrencePreview();
	emit changed();
}

void ServicePanel::commitHasMaximumSpeed(bool checked) {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneService& service = m_document->mutableModel().services[static_cast<std::size_t>(row)];
	if (checked == service.hasMaximumSpeed)
		return;
	service.hasMaximumSpeed = checked;
	if (checked && service.maximumSpeedKmh <= 0.0)
		service.maximumSpeedKmh = 100.0;
	{
		const QSignalBlocker blocker(m_maximumSpeedKmhEdit);
		m_maximumSpeedKmhEdit->setValue(std::max(0.1, service.maximumSpeedKmh));
		m_maximumSpeedKmhEdit->setEnabled(checked && canEdit());
	}
	m_document->markChanged(SceneDocument::Services, "Change service maximum speed");
	refreshOccurrencePreview();
	emit changed();
}

void ServicePanel::commitMaximumSpeed() {
	const int row = selectedRow();
	if (row < 0)
		return;
	const double value = m_maximumSpeedKmhEdit->value();
	SceneService& service = m_document->mutableModel().services[static_cast<std::size_t>(row)];
	if (value == service.maximumSpeedKmh)
		return;
	service.maximumSpeedKmh = value;
	m_document->markChanged(SceneDocument::Services, "Change service maximum speed");
	refreshOccurrencePreview();
	emit changed();
}

void ServicePanel::commitHasOperatingCodeStep(bool checked) {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneService& service = m_document->mutableModel().services[static_cast<std::size_t>(row)];
	if (checked == service.hasOperatingCodeStep)
		return;
	service.hasOperatingCodeStep = checked;
	if (checked && service.operatingCodeStep == 0)
		service.operatingCodeStep = 1;
	{
		const QSignalBlocker blocker(m_operatingCodeStepEdit);
		m_operatingCodeStepEdit->setText(QString::number(service.operatingCodeStep == 0 ? 1 : service.operatingCodeStep));
		m_operatingCodeStepEdit->setEnabled(checked && canEdit());
	}
	m_document->markChanged(SceneDocument::Services, "Change service code step");
	refreshOccurrencePreview();
	emit changed();
}

void ServicePanel::commitOperatingCodeStep() {
	const int row = selectedRow();
	if (row < 0)
		return;
	bool ok = false;
	int value = m_operatingCodeStepEdit->text().toInt(&ok);
	if (!ok || value == 0)
		value = 1;
	{
		const QSignalBlocker blocker(m_operatingCodeStepEdit);
		m_operatingCodeStepEdit->setText(QString::number(value));
	}
	SceneService& service = m_document->mutableModel().services[static_cast<std::size_t>(row)];
	if (value == service.operatingCodeStep)
		return;
	service.operatingCodeStep = value;
	service.hasOperatingCodeStep = true;
	m_document->markChanged(SceneDocument::Services, "Change service code step");
	refreshOccurrencePreview();
	emit changed();
}

void ServicePanel::commitPendingEditors() {
	if (!canEdit())
		return;
	commitIdEdit();
	commitOperatingCode();
	if (m_hasEntryTimeCheck->isChecked())
		commitEntryTimeSeconds();
	if (m_hasRepeatCheck->isChecked()) {
		commitHeadwaySeconds();
		if (m_hasRepeatCountCheck->isChecked())
			commitRepeatCount();
		if (m_hasOperatingCodeStepCheck->isChecked())
			commitOperatingCodeStep();
	}
	m_performancePercentEdit->interpretText();
	commitPerformancePercent(m_performancePercentEdit->value());
	if (m_hasMaximumSpeedCheck->isChecked()) {
		m_maximumSpeedKmhEdit->interpretText();
		commitMaximumSpeed();
	}
}
