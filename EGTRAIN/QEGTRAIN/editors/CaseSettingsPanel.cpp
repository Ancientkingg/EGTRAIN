#include "editors/CaseSettingsPanel.h"

#include "editors/EditorHelpers.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSignalBlocker>

#include <string>

CaseSettingsPanel::CaseSettingsPanel(QWidget* parent) : SceneEditorPanel(parent) {
	setObjectName("caseSettingsPanel");
	QFormLayout* layout = new QFormLayout(this);
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

	connect(m_nameEdit, &QLineEdit::editingFinished, this, &CaseSettingsPanel::commit);
	connect(m_descriptionEdit, &QLineEdit::editingFinished, this, &CaseSettingsPanel::commit);
	connect(m_baseTimeEdit, &QLineEdit::editingFinished, this, &CaseSettingsPanel::commit);
	for (QDoubleSpinBox* edit : {m_durationSecondsEdit, m_bufferSecondsEdit, m_recoveryPercentEdit})
		connect(edit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { commit(); });
	CaseSettingsPanel::refresh();
}

CaseSettingsPanel::~CaseSettingsPanel() {
	// A panel that is a shown window closes in the QWidget destructor, after this one, and the field with the focus then
	// reports that editing finished.
	QObject* const fields[] = {m_nameEdit, m_descriptionEdit, m_baseTimeEdit, m_durationSecondsEdit, m_bufferSecondsEdit,
		m_recoveryPercentEdit};
	for (QObject* field : fields)
		field->disconnect(this);
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
}

void CaseSettingsPanel::commitPendingEditors() {
	// A number field commits only the text that is being typed in it, so that a value the user did not edit is kept.
	for (QDoubleSpinBox* edit : {m_durationSecondsEdit, m_bufferSecondsEdit, m_recoveryPercentEdit}) {
		if (hasEditorFocus(edit))
			edit->interpretText();
	}
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
	if (model.name == name && model.description == description && !baseTimeEdited && !durationEdited
		&& !bufferEdited && !recoveryEdited)
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
			if (baseTimeEdited)
				emit baseTimeChanged(m_baseTimeEdit->text());
		}))
		return;
	emit changed();
}
