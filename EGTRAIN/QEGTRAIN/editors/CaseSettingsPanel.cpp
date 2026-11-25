#include "editors/CaseSettingsPanel.h"

#include "editors/EditorHelpers.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>

#include <algorithm>
#include <string>
#include <vector>

namespace {
// The ids sorted and without repeats, so that two lists of the same services compare equal.
std::vector<std::string> serviceSet(std::vector<std::string> ids) {
	std::sort(ids.begin(), ids.end());
	ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
	return ids;
}

// The line under the list of the services that couple, for a list with no service or with some.
QString couplingNoteText(bool none) {
	return none ? QStringLiteral("No service is checked, so every train in a level 4 area can couple.")
				: QStringLiteral("Only trains of checked services couple, and only to each other.");
}
} // namespace

CaseSettingsPanel::CaseSettingsPanel(QWidget* parent) : SceneEditorPanel(parent) {
	setObjectName("caseSettingsPanel");
	QFormLayout* layout = new QFormLayout(this);
	m_overrideNotice = new QLabel(this);
	m_overrideNotice->setObjectName("caseOverrideNotice");
	m_overrideNotice->setWordWrap(true);
	m_overrideNotice->hide();
	layout->addRow(m_overrideNotice);
	m_nameEdit = new QLineEdit(this);
	m_nameEdit->setObjectName("caseNameEdit");
	m_descriptionEdit = new QLineEdit(this);
	m_descriptionEdit->setObjectName("caseDescriptionEdit");
	m_baseTimeEdit = new QLineEdit(this);
	m_baseTimeEdit->setObjectName("caseBaseTimeEdit");
	m_baseTimeEdit->setPlaceholderText("HH:MM:SS");
	layout->addRow("Name", m_nameEdit);
	layout->addRow("Description", m_descriptionEdit);
	layout->addRow("Base time", m_baseTimeEdit);
	m_durationSecondsEdit = exactNumberEdit("caseDurationSecondsEdit", this);
	m_bufferSecondsEdit = exactNumberEdit("caseBufferSecondsEdit", this);
	m_recoveryPercentEdit = exactNumberEdit("caseRecoveryPercentEdit", this);
	layout->addRow("Duration (s)", m_durationSecondsEdit);
	layout->addRow("Buffer (s)", m_bufferSecondsEdit);
	layout->addRow("Recovery (%)", m_recoveryPercentEdit);
	m_maxTransfersEdit = new QSpinBox(this);
	m_maxTransfersEdit->setObjectName("caseMaxTransfersEdit");
	m_maxTransfersEdit->setRange(0, kMaxPassengerMaxTransfers);
	m_maxTransfersEdit->setKeyboardTracking(false);
	m_maxTransfersEdit->setToolTip("The most transfers that a passenger whose journey has no legs, or whose legs cannot be "
								   "followed, plans");
	layout->addRow("Passenger transfers", m_maxTransfersEdit);
	m_walkingSecondsEdit = new QSpinBox(this);
	m_walkingSecondsEdit->setObjectName("caseWalkingSecondsEdit");
	m_walkingSecondsEdit->setRange(0, kMaxPassengerWalkingSeconds);
	m_walkingSecondsEdit->setKeyboardTracking(false);
	m_walkingSecondsEdit->setToolTip("The walking time of a passenger at a transfer, in seconds per platform number of difference "
									 "between the two platforms; none on the same platform or when a platform number is unknown");
	layout->addRow("Passenger walking (s per platform)", m_walkingSecondsEdit);
	m_lateThresholdEdit = new QSpinBox(this);
	m_lateThresholdEdit->setObjectName("caseLateThresholdEdit");
	m_lateThresholdEdit->setRange(0, kMaxPassengerLateThresholdSeconds);
	m_lateThresholdEdit->setKeyboardTracking(false);
	m_lateThresholdEdit->setToolTip("The delay in seconds up to which passengers plan on the planned times of a train; a train that is "
									"later, or that runs under a speed limit, is planned on predicted times");
	layout->addRow("Passenger late threshold (s)", m_lateThresholdEdit);

	QGroupBox* coupling = new QGroupBox("Virtual coupling", this);
	coupling->setObjectName("caseVirtualCouplingGroup");
	QFormLayout* couplingLayout = new QFormLayout(coupling);
	const auto couplingNumber = [coupling](const char* objectName, double minimum, double maximum, double step, const char* toolTip) {
		CompactDoubleSpinBox* edit = exactNumberEdit(objectName, coupling);
		edit->setRange(minimum, maximum);
		edit->setSingleStep(step);
		edit->setToolTip(toolTip);
		return edit;
	};
	m_couplingDistanceEdit = couplingNumber("caseCouplingDistanceEdit", kMinVirtualCouplingDistanceM, kMaxVirtualCouplingDistanceM, 1.0,
		"A train couples to the train ahead within this distance of the end of authority that the train ahead reports, and a coupled "
		"train leaves the following mode when it falls further behind it");
	couplingLayout->addRow("Coupling distance (m)", m_couplingDistanceEdit);
	m_couplingSpeedToleranceEdit = couplingNumber("caseCouplingSpeedToleranceEdit", kMinVirtualCouplingSpeedToleranceMs,
		kMaxVirtualCouplingSpeedToleranceMs, 0.1, "A train couples to the train ahead when their speeds differ by less than this");
	couplingLayout->addRow("Speed tolerance (m/s)", m_couplingSpeedToleranceEdit);
	m_authorityMarginEdit = couplingNumber("caseAuthorityMarginEdit", kMinEndOfAuthorityMarginM, kMaxEndOfAuthorityMarginM, 1.0,
		"At levels 3 and 4 every train reports an end of authority this far behind its tail and one this far ahead of its head");
	couplingLayout->addRow("Authority margin, levels 3 and 4 (m)", m_authorityMarginEdit);
	m_couplingServiceList = new QListWidget(coupling);
	m_couplingServiceList->setObjectName("caseCouplingServiceList");
	m_couplingServiceList->setToolTip("A train of a checked service couples only to a train of a checked service; the other trains "
									  "keep their distance as at level 3. With none checked every train can couple");
	couplingLayout->addRow("Services that couple", m_couplingServiceList);
	m_couplingServicesNote = new QLabel(coupling);
	m_couplingServicesNote->setObjectName("caseCouplingServicesNote");
	m_couplingServicesNote->setWordWrap(true);
	couplingLayout->addRow(m_couplingServicesNote);
	layout->addRow(coupling);

	connect(m_nameEdit, &QLineEdit::editingFinished, this, &CaseSettingsPanel::commit);
	connect(m_descriptionEdit, &QLineEdit::editingFinished, this, &CaseSettingsPanel::commit);
	connect(m_baseTimeEdit, &QLineEdit::editingFinished, this, &CaseSettingsPanel::commit);
	for (QDoubleSpinBox* edit : {m_durationSecondsEdit, m_bufferSecondsEdit, m_recoveryPercentEdit, m_couplingDistanceEdit,
			 m_couplingSpeedToleranceEdit, m_authorityMarginEdit})
		connect(edit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { commit(); });
	connect(m_maxTransfersEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { commit(); });
	connect(m_walkingSecondsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { commit(); });
	connect(m_lateThresholdEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { commit(); });
	connect(m_couplingServiceList, &QListWidget::itemChanged, this, [this](QListWidgetItem*) { commitCouplingServices(); });
	CaseSettingsPanel::refresh();
}

CaseSettingsPanel::~CaseSettingsPanel() {
	// A panel that is a shown window closes in the QWidget destructor, after this one, and the field with the focus then
	// reports that editing finished.
	QObject* const fields[] = {m_nameEdit, m_descriptionEdit, m_baseTimeEdit, m_durationSecondsEdit, m_bufferSecondsEdit,
		m_recoveryPercentEdit, m_maxTransfersEdit, m_walkingSecondsEdit, m_lateThresholdEdit, m_couplingDistanceEdit,
		m_couplingSpeedToleranceEdit, m_authorityMarginEdit, m_couplingServiceList};
	for (QObject* field : fields)
		field->disconnect(this);
}

void CaseSettingsPanel::setCommandLineOverrides(std::optional<double> durationSeconds, std::optional<double> bufferSeconds,
	std::optional<double> recoveryPercent) {
	QStringList options;
	const auto add = [&options](const char* option, const std::optional<double>& value) {
		if (value)
			options << QStringLiteral("%1 %2").arg(option).arg(*value, 0, 'g', 12);
	};
	add("-h", durationSeconds);
	add("-b", bufferSeconds);
	add("-c", recoveryPercent);
	const QString last = options.isEmpty() ? QString() : options.takeLast();
	const QString listed = options.isEmpty() ? last : options.join(QStringLiteral(", ")) + QStringLiteral(" and ") + last;
	m_overrideNotice->setText(listed.isEmpty() ? QString()
											   : QStringLiteral("This session uses %1 from the command line instead of the values of "
																"the case, for every run and every case it opens.")
													 .arg(listed));
	m_overrideNotice->setVisible(!listed.isEmpty());
}

void CaseSettingsPanel::refresh() {
	const bool loaded = hasScene();
	const SceneModel empty;
	const SceneModel& model = loaded ? m_document->model() : empty;
	{
		const QSignalBlocker blocker(m_nameEdit);
		m_nameEdit->setText(QString::fromStdString(model.name));
		// The field shows the start of a text that is longer than the field.
		m_nameEdit->setCursorPosition(0);
		m_nameEdit->setEnabled(loaded);
	}
	{
		const QSignalBlocker blocker(m_descriptionEdit);
		m_descriptionEdit->setText(QString::fromStdString(model.description));
		m_descriptionEdit->setCursorPosition(0);
		m_descriptionEdit->setEnabled(loaded);
	}
	{
		const QSignalBlocker blocker(m_baseTimeEdit);
		m_baseTimeEdit->setText(QString::fromStdString(model.baseTime));
		m_baseTimeEdit->setEnabled(loaded);
	}
	{
		const QSignalBlocker blocker(m_durationSecondsEdit);
		m_durationSecondsEdit->setValue(model.settings.durationSeconds);
		m_durationSecondsEdit->setEnabled(loaded);
	}
	{
		const QSignalBlocker blocker(m_bufferSecondsEdit);
		m_bufferSecondsEdit->setValue(model.settings.bufferTimeSeconds);
		m_bufferSecondsEdit->setEnabled(loaded);
	}
	{
		const QSignalBlocker blocker(m_recoveryPercentEdit);
		m_recoveryPercentEdit->setValue(model.settings.recoveryTimePercent);
		m_recoveryPercentEdit->setEnabled(loaded);
	}
	{
		const QSignalBlocker blocker(m_maxTransfersEdit);
		m_maxTransfersEdit->setValue(model.settings.passengerMaxTransfers);
		m_maxTransfersEdit->setEnabled(loaded);
	}
	{
		const QSignalBlocker blocker(m_walkingSecondsEdit);
		m_walkingSecondsEdit->setValue(model.settings.passengerWalkingSecondsPerPlatform);
		m_walkingSecondsEdit->setEnabled(loaded);
	}
	{
		const QSignalBlocker blocker(m_lateThresholdEdit);
		m_lateThresholdEdit->setValue(model.settings.passengerLateThresholdSeconds);
		m_lateThresholdEdit->setEnabled(loaded);
	}
	{
		const QSignalBlocker blocker(m_couplingDistanceEdit);
		m_couplingDistanceEdit->setValue(model.settings.virtualCouplingDistanceM);
		m_couplingDistanceEdit->setEnabled(loaded);
	}
	{
		const QSignalBlocker blocker(m_couplingSpeedToleranceEdit);
		m_couplingSpeedToleranceEdit->setValue(model.settings.virtualCouplingSpeedToleranceMs);
		m_couplingSpeedToleranceEdit->setEnabled(loaded);
	}
	{
		const QSignalBlocker blocker(m_authorityMarginEdit);
		m_authorityMarginEdit->setValue(model.settings.endOfAuthorityMarginM);
		m_authorityMarginEdit->setEnabled(loaded);
	}
	const std::vector<std::string>& listed = model.settings.virtualCouplingServices;
	{
		const QSignalBlocker blocker(m_couplingServiceList);
		m_couplingServiceList->clear();
		std::vector<std::string> ids;
		for (const SceneService& service : model.services)
			ids.push_back(service.id);
		for (const std::string& id : listed) {
			if (std::find(ids.begin(), ids.end(), id) == ids.end())
				ids.push_back(id);
		}
		for (const std::string& id : ids) {
			auto* item = new QListWidgetItem(QString::fromStdString(id), m_couplingServiceList);
			item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
			item->setCheckState(std::find(listed.begin(), listed.end(), id) != listed.end() ? Qt::Checked : Qt::Unchecked);
		}
		m_couplingServiceList->setEnabled(loaded);
	}
	m_couplingServicesNote->setText(couplingNoteText(listed.empty()));
}

void CaseSettingsPanel::documentChanged(SceneDocument::ChangeKinds kinds) {
	// The panel edits no service, so this is an edit of another panel. The text being typed in a field is taken first, so
	// that refresh() does not drop it; the list of services that couple is not taken, and refresh() shows it from the model.
	if (kinds.testFlag(SceneDocument::Services)) {
		takePendingText();
		refresh();
	}
}

void CaseSettingsPanel::commitPendingEditors() {
	// A number field commits only the text that is being typed in it, so that a value the user did not edit is kept.
	for (QDoubleSpinBox* edit : {m_durationSecondsEdit, m_bufferSecondsEdit, m_recoveryPercentEdit, m_couplingDistanceEdit,
			 m_couplingSpeedToleranceEdit, m_authorityMarginEdit}) {
		if (hasEditorFocus(edit))
			edit->interpretText();
	}
	if (hasEditorFocus(m_maxTransfersEdit))
		m_maxTransfersEdit->interpretText();
	if (hasEditorFocus(m_walkingSecondsEdit))
		m_walkingSecondsEdit->interpretText();
	if (hasEditorFocus(m_lateThresholdEdit))
		m_lateThresholdEdit->interpretText();
	commit();
}

void CaseSettingsPanel::commit() {
	if (!hasScene())
		return;
	const SceneModel& model = m_document->model();
	const std::string name = m_nameEdit->text().toStdString();
	const std::string description = m_descriptionEdit->text().toStdString();
	const std::string baseTime = m_baseTimeEdit->text().toStdString();
	const double durationSeconds = m_durationSecondsEdit->value();
	const double bufferSeconds = m_bufferSecondsEdit->value();
	const double recoveryPercent = m_recoveryPercentEdit->value();
	const bool baseTimeEdited = model.baseTime != baseTime;
	// A number field shows a stored value outside its range at the nearest limit, which is not an edit.
	const bool durationEdited = shownValue(m_durationSecondsEdit, model.settings.durationSeconds) != durationSeconds;
	const bool bufferEdited = shownValue(m_bufferSecondsEdit, model.settings.bufferTimeSeconds) != bufferSeconds;
	const bool recoveryEdited = shownValue(m_recoveryPercentEdit, model.settings.recoveryTimePercent) != recoveryPercent;
	const int maxTransfers = m_maxTransfersEdit->value();
	const bool transfersEdited = std::clamp(model.settings.passengerMaxTransfers, 0, kMaxPassengerMaxTransfers) != maxTransfers;
	const int walkingSeconds = m_walkingSecondsEdit->value();
	const bool walkingEdited = std::clamp(model.settings.passengerWalkingSecondsPerPlatform, 0, kMaxPassengerWalkingSeconds) != walkingSeconds;
	const int lateThreshold = m_lateThresholdEdit->value();
	const bool lateThresholdEdited = std::clamp(model.settings.passengerLateThresholdSeconds, 0, kMaxPassengerLateThresholdSeconds) != lateThreshold;
	const double couplingDistance = m_couplingDistanceEdit->value();
	const bool couplingDistanceEdited = shownValue(m_couplingDistanceEdit, model.settings.virtualCouplingDistanceM) != couplingDistance;
	const double speedTolerance = m_couplingSpeedToleranceEdit->value();
	const bool speedToleranceEdited = shownValue(m_couplingSpeedToleranceEdit, model.settings.virtualCouplingSpeedToleranceMs) != speedTolerance;
	const double authorityMargin = m_authorityMarginEdit->value();
	const bool authorityMarginEdited = shownValue(m_authorityMarginEdit, model.settings.endOfAuthorityMarginM) != authorityMargin;
	const bool couplingEdited = couplingDistanceEdited || speedToleranceEdited || authorityMarginEdited;
	if (model.name == name && model.description == description && !baseTimeEdited && !durationEdited
		&& !bufferEdited && !recoveryEdited && !transfersEdited && !walkingEdited && !lateThresholdEdited && !couplingEdited)
		return;

	if (!m_document->edit(SceneDocument::CaseSettings, "Edit case settings", [&](SceneModel& edited) {
			edited.name = name;
			edited.description = description;
			edited.baseTime = baseTime;
			if (durationEdited) {
				edited.settings.hasDuration = true;
				edited.settings.durationSeconds = durationSeconds;
			}
			if (bufferEdited) {
				edited.settings.hasBufferTime = true;
				edited.settings.bufferTimeSeconds = bufferSeconds;
			}
			if (recoveryEdited) {
				edited.settings.hasRecoveryTime = true;
				edited.settings.recoveryTimePercent = recoveryPercent;
			}
			if (transfersEdited)
				edited.settings.passengerMaxTransfers = maxTransfers;
			if (walkingEdited)
				edited.settings.passengerWalkingSecondsPerPlatform = walkingSeconds;
			if (lateThresholdEdited)
				edited.settings.passengerLateThresholdSeconds = lateThreshold;
			if (couplingDistanceEdited)
				edited.settings.virtualCouplingDistanceM = couplingDistance;
			if (speedToleranceEdited)
				edited.settings.virtualCouplingSpeedToleranceMs = speedTolerance;
			if (authorityMarginEdited)
				edited.settings.endOfAuthorityMarginM = authorityMargin;
			if (baseTimeEdited)
				emit baseTimeChanged(m_baseTimeEdit->text());
		}))
		return;
	emit changed();
}

void CaseSettingsPanel::commitCouplingServices() {
	if (!hasScene())
		return;
	std::vector<std::string> services;
	for (int row = 0; row < m_couplingServiceList->count(); ++row) {
		const QListWidgetItem* item = m_couplingServiceList->item(row);
		if (item->checkState() == Qt::Checked)
			services.push_back(item->text().toStdString());
	}
	// The order of the list in the file is not an edit.
	if (serviceSet(services) == serviceSet(m_document->model().settings.virtualCouplingServices))
		return;
	if (!m_document->edit(SceneDocument::CaseSettings, "Edit case settings",
			[&services](SceneModel& edited) { edited.settings.virtualCouplingServices = services; }))
		return;
	m_couplingServicesNote->setText(couplingNoteText(services.empty()));
	emit changed();
}
