#include "editors/CaseSettingsPanel.h"

#include "editors/EditorHelpers.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>

#include <algorithm>
#include <string>

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

	connect(m_nameEdit, &QLineEdit::editingFinished, this, &CaseSettingsPanel::commit);
	connect(m_descriptionEdit, &QLineEdit::editingFinished, this, &CaseSettingsPanel::commit);
	connect(m_baseTimeEdit, &QLineEdit::editingFinished, this, &CaseSettingsPanel::commit);
	for (QDoubleSpinBox* edit : {m_durationSecondsEdit, m_bufferSecondsEdit, m_recoveryPercentEdit})
		connect(edit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { commit(); });
	connect(m_maxTransfersEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { commit(); });
	connect(m_walkingSecondsEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { commit(); });
	connect(m_lateThresholdEdit, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { commit(); });
	CaseSettingsPanel::refresh();
}

CaseSettingsPanel::~CaseSettingsPanel() {
	// A panel that is a shown window closes in the QWidget destructor, after this one, and the field with the focus then
	// reports that editing finished.
	QObject* const fields[] = {m_nameEdit, m_descriptionEdit, m_baseTimeEdit, m_durationSecondsEdit, m_bufferSecondsEdit,
		m_recoveryPercentEdit, m_maxTransfersEdit, m_walkingSecondsEdit, m_lateThresholdEdit};
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
}

void CaseSettingsPanel::commitPendingEditors() {
	// A number field commits only the text that is being typed in it, so that a value the user did not edit is kept.
	for (QDoubleSpinBox* edit : {m_durationSecondsEdit, m_bufferSecondsEdit, m_recoveryPercentEdit}) {
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
	if (model.name == name && model.description == description && !baseTimeEdited && !durationEdited
		&& !bufferEdited && !recoveryEdited && !transfersEdited && !walkingEdited && !lateThresholdEdited)
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
			if (baseTimeEdited)
				emit baseTimeChanged(m_baseTimeEdit->text());
		}))
		return;
	emit changed();
}
