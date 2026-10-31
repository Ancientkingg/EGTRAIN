#include "editors/ServicePanel.h"

#include "editors/EditorHelpers.h"
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
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
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
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

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

// One entry of the route choice: where the route goes, marked when it runs reversed, with the stations it passes as tooltip.
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
	const QString id = QString::fromStdString(route.id);
	QString text = !traversal.resolved ? QStringLiteral("%1 (cannot be resolved)").arg(id)
		: stations.isEmpty()		   ? QStringLiteral("%1 (no stations)").arg(id)
									   : QString("%1 → %2 [%3]").arg(stations.first(), stations.last(), id);
	if (traversal.resolved && !stations.isEmpty() && traversal.direction < 0)
		text += QStringLiteral(" (reversed)");
	combo->addItem(text, id);
	QString tip;
	if (!traversal.resolved)
		tip = QStringLiteral("Fix this route's sections to see its stations.");
	else if (!stations.isEmpty())
		tip = QString("Passes %1").arg(stations.join(" → "));
	combo->setItemData(combo->count() - 1, tip, Qt::ToolTipRole);
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
	form->addRow("Service ID", m_idEdit);
	m_operatingCodeEdit = new QLineEdit(detailPane);
	m_operatingCodeEdit->setObjectName("serviceOperatingCodeEdit");
	form->addRow("Operating code", m_operatingCodeEdit);
	m_categoryCombo = new ChoiceComboBox(detailPane);
	m_categoryCombo->setObjectName("serviceCategoryCombo");
	form->addRow("Category", m_categoryCombo);
	m_colorButton = new ServiceColorField(form, detailPane);
	m_colorButton->setObjectName("serviceColorButton");
	form->addRow("Colour", m_colorButton);
	m_compositionCombo = new ChoiceComboBox(detailPane);
	m_compositionCombo->setObjectName("serviceCompositionCombo");
	form->addRow("Composition", m_compositionCombo);
	// Over the closed field the tip adds the stations of the selected route to the text of the field.
	m_routeCombo = new ChoiceComboBox(detailPane);
	m_routeCombo->setObjectName("serviceRouteCombo");
	form->addRow("Route", m_routeCombo);

	m_hasEntryTimeCheck = new QCheckBox("Entry time (s)", detailPane);
	m_hasEntryTimeCheck->setObjectName("serviceHasEntryTimeCheck");
	m_entryTimeSecondsEdit = exactNumberEdit("serviceEntryTimeSecondsEdit", detailPane);
	form->addRow(m_hasEntryTimeCheck, m_entryTimeSecondsEdit);

	m_hasRepeatCheck = new QCheckBox("Repeat every (s)", detailPane);
	m_hasRepeatCheck->setObjectName("serviceHasRepeatCheck");
	m_headwaySecondsEdit = exactNumberEdit("serviceHeadwaySecondsEdit", detailPane);
	form->addRow(m_hasRepeatCheck, m_headwaySecondsEdit);

	m_hasRepeatCountCheck = new QCheckBox("Repeat count", detailPane);
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
	form->addRow("Performance (%)", m_performancePercentEdit);

	m_hasMaximumSpeedCheck = new QCheckBox("Maximum speed (km/h)", detailPane);
	m_hasMaximumSpeedCheck->setObjectName("serviceHasMaximumSpeedCheck");
	m_maximumSpeedKmhEdit = new ServiceSpeedSpinBox(detailPane);
	m_maximumSpeedKmhEdit->setObjectName("serviceMaximumSpeedKmhEdit");
	m_maximumSpeedKmhEdit->setRange(0.1, 1000.0);
	m_maximumSpeedKmhEdit->setDecimals(std::numeric_limits<double>::max_digits10);
	m_maximumSpeedKmhEdit->setValue(100.0);
	form->addRow(m_hasMaximumSpeedCheck, m_maximumSpeedKmhEdit);

	m_hasOperatingCodeStepCheck = new QCheckBox("Code step", detailPane);
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
	// Tab reaches the fields inside, and the area scrolls to show them.
	detailScroll->setFocusPolicy(Qt::NoFocus);
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
	m_occurrenceSelectionLabel = new QLabel(occurrencePane);
	m_occurrenceSelectionLabel->setObjectName("serviceOccurrenceSelectionLabel");
	occurrenceLayout->addWidget(m_occurrenceSelectionLabel);
	m_occurrenceTable = new QTableWidget(occurrencePane);
	m_occurrenceTable->setObjectName("serviceOccurrenceTable");
	m_occurrenceTable->setColumnCount(6);
	m_occurrenceTable->setHorizontalHeaderLabels({"Run", "Operating code", "Route", "Entry", "Performance (%)", "Maximum speed (km/h)"});
	m_occurrenceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_occurrenceTable->setTabKeyNavigation(false);
	m_occurrenceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_occurrenceTable->setAlternatingRowColors(true);
	m_occurrenceTable->horizontalHeader()->setStretchLastSection(true);
	occurrenceLayout->addWidget(m_occurrenceTable, 1);
	QHBoxLayout* occurrenceButtonLayout = new QHBoxLayout();
	m_selectAllButton = new QPushButton("Select All", occurrencePane);
	m_selectAllButton->setObjectName("selectAllOccurrencesButton");
	m_selectNoneButton = new QPushButton("Select None", occurrencePane);
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
	connect(m_entryTimeSecondsEdit, &QAbstractSpinBox::editingFinished, this, &ServicePanel::commitEntryTimeSeconds);
	connect(m_hasRepeatCheck, &QCheckBox::toggled, this, &ServicePanel::commitHasRepeat);
	connect(m_headwaySecondsEdit, &QAbstractSpinBox::editingFinished, this, &ServicePanel::commitHeadwaySeconds);
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

void ServicePanel::setTimetableWidget(QWidget* widget) {
	m_timetableLayout->addWidget(widget);
}

int ServicePanel::selectedRow() const {
	const int row = m_listWidget->currentRow();
	if (!hasScene() || row < 0 || row >= static_cast<int>(m_document->model().services.size()))
		return -1;
	return row;
}

void ServicePanel::documentChanged(SceneDocument::ChangeKinds kinds) {
	// The panel edits neither the infrastructure nor the compositions, so these are edits of other panels. The preview
	// leaves the fields as they are.
	if (kinds.testFlag(SceneDocument::Infrastructure)) {
		takePendingText();
		refresh();
		return;
	}
	if (kinds.testFlag(SceneDocument::Compositions)) {
		takePendingText();
		updateDetail();
	}
	if (kinds.testFlag(SceneDocument::CaseSettings))
		refreshOccurrencePreview();
}

const SceneService* ServicePanel::selectedService() const {
	const int row = selectedRow();
	return row < 0 ? nullptr : &m_document->model().services[static_cast<std::size_t>(row)];
}

bool ServicePanel::editService(const QString& label, const std::function<void(SceneService&)>& mutate) {
	const auto row = static_cast<std::size_t>(selectedRow());
	return m_document->edit(SceneDocument::Services, label, [row, &mutate](SceneModel& model) { mutate(model.services[row]); });
}

void ServicePanel::editSelectedService(const QString& label, const std::function<void(SceneService&)>& mutate) {
	if (!editService(label, mutate))
		return;
	refreshOccurrencePreview();
	emit changed();
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
		restoreListSelection(m_listWidget, previousId, previousRow);
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
				m_categoryCombo->addItem(QString("%1 (unknown)").arg(currentCategory), currentCategory);
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
				m_routeCombo->addItem(currentRoute.isEmpty() ? QStringLiteral("(none)") : QString("%1 (missing)").arg(currentRoute),
					currentRoute);
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
		m_entryTimeSecondsEdit->setValue(service ? service->entryTimeSeconds : 0.0);
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
		m_headwaySecondsEdit->setValue(service ? service->headwaySeconds : 0.0);
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

QString ServicePanel::occurrenceSummaryText(int total, int selected, int selectedInPeriod, int shown) {
	QString text = QString("%1 of %2 selected").arg(selected).arg(total);
	if (selectedInPeriod != selected)
		text += QString(", %1 in the run period").arg(selectedInPeriod);
	if (shown < total)
		text += QString("\nShowing the first %1. All of them run. Lower the repeat count to choose which.").arg(shown);
	return text;
}

void ServicePanel::refreshOccurrencePreview() {
	if (!hasScene()) {
		m_updatingOccurrencePreview = true;
		m_occurrenceTable->clearContents();
		m_occurrenceTable->setRowCount(0);
		m_updatingOccurrencePreview = false;
		m_occurrenceSelectionLabel->clear();
		m_occurrenceSelectionLabel->setToolTip(QString());
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
				new QTableWidgetItem(code.empty() ? QStringLiteral("-") : QString::fromStdString(code)));
			m_occurrenceTable->setItem(row, 2,
				new QTableWidgetItem(service.route.empty() ? QStringLiteral("-") : QString::fromStdString(service.route)));
			const double entry = sceneServiceScheduledEntry(service, occurrence);
			m_occurrenceTable->setItem(row, 3,
				new QTableWidgetItem(std::isfinite(entry) ? QString("+%1 s").arg(QString::number(entry, 'g', 12)) : QStringLiteral("-")));
			m_occurrenceTable->setItem(row, 4,
				new QTableWidgetItem(QString::number(static_cast<double>(service.performancePercent), 'g', 6)));
			m_occurrenceTable->setItem(row, 5,
				new QTableWidgetItem(service.hasMaximumSpeed ? QString::number(service.maximumSpeedKmh, 'g', 6) : QStringLiteral("-")));
			for (int column = 0; column < m_occurrenceTable->columnCount(); ++column)
				m_occurrenceTable->item(row, column)
					->setToolTip(generatedServiceLabel(service, occurrence)
						+ QString("\nService %1, run %2").arg(QString::fromStdString(service.id)).arg(occurrence));
		}
		if (row >= displayedOccurrences)
			break;
	}
	m_occurrenceTable->resizeColumnsToContents();
	m_updatingOccurrencePreview = false;
	m_occurrenceSelectionLabel->setText(occurrenceSummaryText(totalOccurrences, m_runSelection->selectedCount(model, durationSeconds),
		m_runSelection->selectedInPeriodCount(model, durationSeconds), displayedOccurrences));
	m_occurrenceSelectionLabel->setToolTip(
		QString("An occurrence is in the run period when it enters from 0 up to %1 s.").arg(QString::number(durationSeconds, 'g', 12)));
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
	SceneService service;
	service.id = sceneUniqueServiceId(m_document->model(), "new_service");
	if (!m_document->edit(SceneDocument::Services, "Add service", [&service](SceneModel& model) { model.services.push_back(service); }))
		return;

	refresh();
	emit changed();

	m_listWidget->setCurrentRow(static_cast<int>(m_document->model().services.size()) - 1);
}

void ServicePanel::duplicateService() {
	const int row = selectedRow();
	if (row < 0)
		return;
	SceneService duplicate = m_document->model().services[static_cast<std::size_t>(row)];
	duplicate.id = sceneUniqueServiceId(m_document->model(), duplicate.id + "_copy");
	if (!m_document->edit(SceneDocument::Services, "Duplicate service",
			[row, &duplicate](SceneModel& model) { model.services.insert(model.services.begin() + row + 1, duplicate); }))
		return;

	refresh();
	emit changed();

	m_listWidget->setCurrentRow(row + 1);
}

void ServicePanel::deleteService() {
	const int row = selectedRow();
	if (row < 0)
		return;
	const SceneModel& model = m_document->model();
	const std::string id = model.services[static_cast<std::size_t>(row)].id;
	QStringList consumers;
	for (const std::string& consumer : sceneDirectDeleteConsumers(model, "service", id))
		consumers << QString::fromStdString(consumer);
	if (!consumers.isEmpty()) {
		m_prompts->showError(this, "Cannot Delete Service",
			QString("Service '%1' is used by:\n• %2\nRemove those references first.")
				.arg(QString::fromStdString(id), consumers.join("\n• ")),
			true);
		return;
	}
	if (!m_prompts->confirm(this, "Delete Service", QString("Delete service '%1'?").arg(QString::fromStdString(id))))
		return;

	if (!m_document->edit(SceneDocument::Services, "Delete service", [this, row](SceneModel& edited) {
			edited.services.erase(edited.services.begin() + row);
			m_runSelection->pruneExcluded(edited, occurrenceDuration());
		}))
		return;

	refresh();
	emit changed();
}

void ServicePanel::commitIdEdit() {
	const int row = selectedRow();
	if (row < 0)
		return;
	const SceneModel& model = m_document->model();
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
		m_prompts->showError(this, "Duplicate Service ID", "Choose a unique service ID.", true);
		return;
	}
	if (!m_document->edit(SceneDocument::Services | SceneDocument::Scenarios | SceneDocument::Passengers, "Rename service", [&](SceneModel& edited) {
			sceneRewriteServiceReferences(edited, oldId, newId);
			m_runSelection->serviceRenamed(oldId, newId);
			edited.services[static_cast<std::size_t>(row)].id = newId;

			// update the list row label in place instead of rebuilding the panel, so a
			// focus-out that lands on another control keeps that pending click intact
			if (QListWidgetItem* item = m_listWidget->item(row)) {
				const QSignalBlocker blocker(m_listWidget);
				item->setText(QString::fromStdString(newId));
			}
		}))
		return;

	refreshOccurrencePreview();
	emit selectedServiceChanged(QString::fromStdString(newId));
	emit serviceRenamed(QString::fromStdString(oldId), QString::fromStdString(newId));
	emit changed();
}

void ServicePanel::commitOperatingCode() {
	const SceneService* selected = selectedService();
	if (!selected)
		return;
	const std::string value = m_operatingCodeEdit->text().trimmed().toStdString();
	if (value == selected->operatingCode)
		return;
	editSelectedService("Change service code", [&value](SceneService& service) { service.operatingCode = value; });
}

void ServicePanel::commitCategory(int index) {
	const SceneService* selected = selectedService();
	if (!selected || index < 0)
		return;
	const std::string value = m_categoryCombo->itemData(index).toString().toStdString();
	if (value == selected->category)
		return;
	if (editService("Change service category", [&value](SceneService& service) { service.category = value; }))
		emit changed();
}

void ServicePanel::commitVisualizationColor(const QString& text) {
	const SceneService* selected = selectedService();
	if (!selected)
		return;
	const std::string value = text.toStdString();
	if (value == selected->visualizationColor)
		return;
	if (editService("Change service colour", [&value](SceneService& service) { service.visualizationColor = value; }))
		emit changed();
}

void ServicePanel::commitComposition(const QString& text) {
	const SceneService* selected = selectedService();
	if (!selected)
		return;
	const std::string value = text.toStdString();
	if (value == selected->composition)
		return;
	// the combo already shows the chosen value and the service list labels are
	// unchanged, so do not rebuild the panel here (that would close the popup)
	if (editService("Change service composition", [&value](SceneService& service) { service.composition = value; }))
		emit changed();
}

void ServicePanel::commitRoute(int index) {
	const SceneService* selected = selectedService();
	if (!selected || index < 0)
		return;
	const std::string value = m_routeCombo->itemData(index).toString().toStdString();
	if (value == selected->route)
		return;
	// the combo already shows the chosen value and the service list labels are
	// unchanged, so do not rebuild the panel here (that would close the popup)
	if (!editService("Change service route", [this, index, &value](SceneService& service) {
			service.route = value;
			m_routeCombo->setToolTip(m_routeCombo->itemData(index, Qt::ToolTipRole).toString());
		}))
		return;
	refreshOccurrencePreview();
	emit selectedServiceChanged(QString::fromStdString(selected->id));
	emit changed();
}

void ServicePanel::commitHasEntryTime(bool checked) {
	const SceneService* selected = selectedService();
	if (!selected || checked == selected->hasEntryTime)
		return;
	editSelectedService("Change service entry time", [this, checked](SceneService& service) {
		service.hasEntryTime = checked;
		m_entryTimeSecondsEdit->setEnabled(checked);
	});
}

void ServicePanel::commitEntryTimeSeconds() {
	const SceneService* selected = selectedService();
	if (!selected)
		return;
	const double value = m_entryTimeSecondsEdit->value();
	if (value == shownValue(m_entryTimeSecondsEdit, selected->entryTimeSeconds))
		return;
	editSelectedService("Change service entry time", [value](SceneService& service) { service.entryTimeSeconds = value; });
}

void ServicePanel::commitHasRepeat(bool checked) {
	const SceneService* selected = selectedService();
	if (!selected || checked == selected->hasRepeat)
		return;
	editSelectedService("Change service repeat", [this, checked](SceneService& service) {
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
	});
}

void ServicePanel::commitHeadwaySeconds() {
	const SceneService* selected = selectedService();
	if (!selected)
		return;
	const double value = m_headwaySecondsEdit->value();
	if (value == shownValue(m_headwaySecondsEdit, selected->headwaySeconds))
		return;
	editSelectedService("Change service headway", [value](SceneService& service) { service.headwaySeconds = value; });
}

void ServicePanel::commitHasRepeatCount(bool checked) {
	const SceneService* selected = selectedService();
	if (!selected || checked == selected->hasRepeatCount)
		return;
	editSelectedService("Change service repeat count", [this, checked](SceneService& service) {
		service.hasRepeatCount = checked;
		if (checked && service.repeatCount < 1)
			service.repeatCount = 1;
		const QSignalBlocker blocker(m_repeatCountEdit);
		m_repeatCountEdit->setText(QString::number(std::max(1, service.repeatCount)));
		m_repeatCountEdit->setEnabled(checked && canEdit());
	});
}

void ServicePanel::commitRepeatCount() {
	const SceneService* selected = selectedService();
	if (!selected)
		return;
	bool ok = false;
	int count = m_repeatCountEdit->text().toInt(&ok);
	if (!ok || count < 1)
		count = 1;
	{
		const QSignalBlocker blocker(m_repeatCountEdit);
		m_repeatCountEdit->setText(QString::number(count));
	}
	if (count == selected->repeatCount)
		return;
	editSelectedService("Change service repeat count", [count](SceneService& service) { service.repeatCount = count; });
}

void ServicePanel::commitPerformancePercent(double value) {
	const SceneService* selected = selectedService();
	if (!selected)
		return;
	value = std::clamp(value, 1.0, 100.0);
	if (value == shownValue(m_performancePercentEdit, selected->performancePercent))
		return;
	editSelectedService("Change service performance", [value](SceneService& service) { service.performancePercent = value; });
}

void ServicePanel::commitHasMaximumSpeed(bool checked) {
	const SceneService* selected = selectedService();
	if (!selected || checked == selected->hasMaximumSpeed)
		return;
	editSelectedService("Change service maximum speed", [this, checked](SceneService& service) {
		service.hasMaximumSpeed = checked;
		if (checked && service.maximumSpeedKmh <= 0.0)
			service.maximumSpeedKmh = 100.0;
		const QSignalBlocker blocker(m_maximumSpeedKmhEdit);
		m_maximumSpeedKmhEdit->setValue(std::max(0.1, service.maximumSpeedKmh));
		m_maximumSpeedKmhEdit->setEnabled(checked && canEdit());
	});
}

void ServicePanel::commitMaximumSpeed() {
	const SceneService* selected = selectedService();
	if (!selected)
		return;
	const double value = m_maximumSpeedKmhEdit->value();
	if (value == shownValue(m_maximumSpeedKmhEdit, selected->maximumSpeedKmh))
		return;
	editSelectedService("Change service maximum speed", [value](SceneService& service) { service.maximumSpeedKmh = value; });
}

void ServicePanel::commitHasOperatingCodeStep(bool checked) {
	const SceneService* selected = selectedService();
	if (!selected || checked == selected->hasOperatingCodeStep)
		return;
	editSelectedService("Change service code step", [this, checked](SceneService& service) {
		service.hasOperatingCodeStep = checked;
		if (checked && service.operatingCodeStep == 0)
			service.operatingCodeStep = 1;
		const QSignalBlocker blocker(m_operatingCodeStepEdit);
		m_operatingCodeStepEdit->setText(QString::number(service.operatingCodeStep == 0 ? 1 : service.operatingCodeStep));
		m_operatingCodeStepEdit->setEnabled(checked && canEdit());
	});
}

void ServicePanel::commitOperatingCodeStep() {
	const SceneService* selected = selectedService();
	if (!selected)
		return;
	bool ok = false;
	int value = m_operatingCodeStepEdit->text().toInt(&ok);
	if (!ok || value == 0)
		value = 1;
	{
		const QSignalBlocker blocker(m_operatingCodeStepEdit);
		m_operatingCodeStepEdit->setText(QString::number(value));
	}
	if (value == selected->operatingCodeStep)
		return;
	editSelectedService("Change service code step", [value](SceneService& service) {
		service.operatingCodeStep = value;
		service.hasOperatingCodeStep = true;
	});
}

void ServicePanel::commitPendingEditors() {
	if (!canEdit())
		return;
	commitIdEdit();
	commitOperatingCode();
	if (m_hasRepeatCheck->isChecked()) {
		if (m_hasRepeatCountCheck->isChecked())
			commitRepeatCount();
		if (m_hasOperatingCodeStepCheck->isChecked())
			commitOperatingCodeStep();
	}
	// A number field commits only the text that is being typed in it, so that a value the user did not edit is kept. A
	// field that its box has switched off is disabled and cannot have the focus.
	if (hasEditorFocus(m_entryTimeSecondsEdit)) {
		m_entryTimeSecondsEdit->interpretText();
		commitEntryTimeSeconds();
	}
	if (hasEditorFocus(m_headwaySecondsEdit)) {
		m_headwaySecondsEdit->interpretText();
		commitHeadwaySeconds();
	}
	if (hasEditorFocus(m_performancePercentEdit)) {
		m_performancePercentEdit->interpretText();
		commitPerformancePercent(m_performancePercentEdit->value());
	}
	if (hasEditorFocus(m_maximumSpeedKmhEdit)) {
		m_maximumSpeedKmhEdit->interpretText();
		commitMaximumSpeed();
	}
}
