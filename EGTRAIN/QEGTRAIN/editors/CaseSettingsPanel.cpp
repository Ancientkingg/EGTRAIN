#include "editors/CaseSettingsPanel.h"

#include "widgets/CompactDoubleSpinBox.h"
#include "widgets/DialogLayout.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>

#include <limits>
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
	const auto makeSpinBox = [this](const char* widgetName) {
		auto* edit = new CompactDoubleSpinBox(this);
		edit->setObjectName(widgetName);
		edit->setRange(0.0, std::numeric_limits<double>::max());
		edit->setDecimals(std::numeric_limits<double>::max_digits10);
		edit->setSingleStep(1.0);
		edit->setKeyboardTracking(false);
		return edit;
	};
	m_durationSecondsEdit = makeSpinBox("caseDurationSecondsEdit");
	m_bufferSecondsEdit = makeSpinBox("caseBufferSecondsEdit");
	m_recoveryPercentEdit = makeSpinBox("caseRecoveryPercentEdit");
	layout->addRow("Duration / horizon (s)", m_durationSecondsEdit);
	layout->addRow("Buffer (s)", m_bufferSecondsEdit);
	layout->addRow("Recovery (%)", m_recoveryPercentEdit);
	auto* note = new QLabel(this);
	note->setObjectName("caseSettingsNote");
	DialogLayout::setHelp(note, "Changes apply when editing finishes.");
	layout->addRow(note);

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

void CaseSettingsPanel::setDocument(SceneDocument* document) {
	QObject::disconnect(m_replacedConnection);
	m_document = document;
	if (m_document)
		m_replacedConnection = connect(m_document, &SceneDocument::replaced, this, &CaseSettingsPanel::refresh);
	refresh();
}

void CaseSettingsPanel::refresh() {
	const bool hasScene = m_document && m_document->isLoaded();
	const SceneModel empty;
	const SceneModel& model = hasScene ? m_document->model() : empty;
	{
		const QSignalBlocker blocker(m_nameEdit);
		m_nameEdit->setText(QString::fromStdString(model.name));
		// The field shows the start of a text that is longer than the field.
		m_nameEdit->setCursorPosition(0);
		m_nameEdit->setEnabled(hasScene);
	}
	{
		const QSignalBlocker blocker(m_descriptionEdit);
		m_descriptionEdit->setText(QString::fromStdString(model.description));
		m_descriptionEdit->setCursorPosition(0);
		m_descriptionEdit->setEnabled(hasScene);
	}
	{
		const QSignalBlocker blocker(m_baseTimeEdit);
		m_baseTimeEdit->setText(QString::fromStdString(model.baseTime));
		m_baseTimeEdit->setEnabled(hasScene);
	}
	{
		const QSignalBlocker blocker(m_durationSecondsEdit);
		m_durationSecondsEdit->setValue(model.settings.durationSeconds);
		m_durationSecondsEdit->setEnabled(hasScene);
	}
	{
		const QSignalBlocker blocker(m_bufferSecondsEdit);
		m_bufferSecondsEdit->setValue(model.settings.bufferTimeSeconds);
		m_bufferSecondsEdit->setEnabled(hasScene);
	}
	{
		const QSignalBlocker blocker(m_recoveryPercentEdit);
		m_recoveryPercentEdit->setValue(model.settings.recoveryTimePercent);
		m_recoveryPercentEdit->setEnabled(hasScene);
	}
}

void CaseSettingsPanel::commitPendingEditors() {
	m_durationSecondsEdit->interpretText();
	m_bufferSecondsEdit->interpretText();
	m_recoveryPercentEdit->interpretText();
	commit();
}

void CaseSettingsPanel::commit() {
	if (!m_document || !m_document->isLoaded())
		return;
	SceneModel& model = m_document->mutableModel();
	const std::string name = m_nameEdit->text().toStdString();
	const std::string description = m_descriptionEdit->text().toStdString();
	const std::string baseTime = m_baseTimeEdit->text().toStdString();
	const double durationSeconds = m_durationSecondsEdit->value();
	const double bufferSeconds = m_bufferSecondsEdit->value();
	const double recoveryPercent = m_recoveryPercentEdit->value();
	const bool baseTimeEdited = model.baseTime != baseTime;
	const bool durationEdited = model.settings.durationSeconds != durationSeconds;
	const bool bufferEdited = model.settings.bufferTimeSeconds != bufferSeconds;
	const bool recoveryEdited = model.settings.recoveryTimePercent != recoveryPercent;
	if (model.name == name && model.description == description && !baseTimeEdited && !durationEdited
		&& !bufferEdited && !recoveryEdited)
		return;

	model.name = name;
	model.description = description;
	model.baseTime = baseTime;
	model.settings.hasDuration = model.settings.hasDuration || durationEdited;
	model.settings.durationSeconds = durationSeconds;
	model.settings.hasBufferTime = model.settings.hasBufferTime || bufferEdited;
	model.settings.bufferTimeSeconds = bufferSeconds;
	model.settings.hasRecoveryTime = model.settings.hasRecoveryTime || recoveryEdited;
	model.settings.recoveryTimePercent = recoveryPercent;
	if (baseTimeEdited)
		emit baseTimeChanged(m_baseTimeEdit->text());
	m_document->markChanged(SceneDocument::CaseSettings, "Edit case settings");
	emit changed();
}
