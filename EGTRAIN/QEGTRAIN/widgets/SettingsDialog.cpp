#include "widgets/SettingsDialog.h"

#include "telemetry/TelemetryConsent.h"
#include "update/UpdateSettings.h"
#include "widgets/DialogLayout.h"
#include "widgets/DisplayPath.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {

QVBoxLayout* addSection(QVBoxLayout* layout, const QString& title) {
	auto* group = new QGroupBox(title);
	layout->addWidget(group);
	return new QVBoxLayout(group);
}

QCheckBox* addCheckBox(QVBoxLayout* section, const QString& text, const char* name, bool checked) {
	auto* box = new QCheckBox(text);
	box->setObjectName(QString::fromLatin1(name));
	box->setChecked(checked);
	section->addWidget(box);
	return box;
}

void addHelp(QVBoxLayout* section, const QString& text) {
	auto* label = new QLabel(section->parentWidget());
	section->addWidget(label);
	DialogLayout::setHelp(label, text);
}

} // namespace

SettingsDialog::SettingsDialog(QSettings& settings, TelemetryConsent& consent, QWidget* parent)
	: QDialog(parent), m_settings(settings), m_consent(consent) {
	setObjectName(QStringLiteral("settingsDialog"));
	setWindowTitle(QStringLiteral("Settings"));
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	layout->setContentsMargins(0, 0, 0, 0);

	QVBoxLayout* general = addSection(layout, QStringLiteral("General"));
	general->addWidget(new QLabel(QStringLiteral("Recent cases")));
	m_recentCases = new QListWidget;
	m_recentCases->setObjectName(QStringLiteral("settingsRecentCases"));
	m_recentCases->setSelectionMode(QAbstractItemView::NoSelection);
	m_recentCases->setMaximumHeight(5 * fontMetrics().height());
	for (const QString& path : settings.value(kRecentScenesKey).toStringList())
		m_recentCases->addItem(display_path::format(path));
	general->addWidget(m_recentCases);
	auto* clearRecent = new QPushButton(QStringLiteral("Clear Recent Cases"));
	clearRecent->setObjectName(QStringLiteral("settingsClearRecentCases"));
	clearRecent->setAutoDefault(false);
	general->addWidget(clearRecent, 0, Qt::AlignLeft);
	addHelp(general, QStringLiteral("File > Recent Scenes and Open a Case at start offer these cases."));
	connect(clearRecent, &QPushButton::clicked, m_recentCases, &QListWidget::clear);

	QVBoxLayout* display = addSection(layout, QStringLiteral("Display"));
	m_showPassengers = addCheckBox(display, QStringLiteral("Show passengers"), "settingsShowPassengers",
		settings.value(kShowPassengersKey, false).toBool());
	addHelp(display, QStringLiteral("The same switch as View > Show Passengers. Turning it on shows passengers from the next run."));
	m_advancedDetails = addCheckBox(display, QStringLiteral("Advanced / Developer details"), "settingsAdvancedDetails",
		settings.value(kAdvancedDetailsKey, false).toBool());

	QVBoxLayout* updates = addSection(layout, QStringLiteral("Updates and privacy"));
	m_openedUpdateCheckState = m_updateCheckState = readUpdateCheckState(settings);
	m_updateChecks = addCheckBox(updates, QStringLiteral("Automatically check for updates"), "settingsAutomaticUpdateChecks",
		m_updateCheckState == UpdateCheckState::Enabled);
	connect(m_updateChecks, &QCheckBox::toggled, this,
		[this](bool on) { m_updateCheckState = on ? UpdateCheckState::Enabled : UpdateCheckState::Disabled; });
	const bool privacy = consent.available();
	m_usage = addCheckBox(updates, QStringLiteral("Share usage statistics"), "settingsUsage", privacy && consent.usageEnabled());
	m_diagnostics = addCheckBox(updates, QStringLiteral("Share diagnostic reports"), "settingsDiagnostics",
		privacy && consent.diagnosticsEnabled());
	m_usage->setEnabled(privacy);
	m_diagnostics->setEnabled(privacy);
	if (privacy)
		addHelp(updates, QStringLiteral("Help > Privacy & diagnostics... says what each choice sends."));
	else
		addHelp(updates, QStringLiteral("Usage statistics and diagnostics are unavailable in this build or run. Nothing is collected or sent."));
	m_status = new QLabel(body);
	m_status->setObjectName(QStringLiteral("settingsStatus"));
	m_status->hide();
	layout->addWidget(m_status);

	auto* actions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
	actions->button(QDialogButtonBox::Ok)->setDefault(true);
	actions->button(QDialogButtonBox::RestoreDefaults)->setAutoDefault(false);
	connect(actions, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
	connect(actions, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
	connect(actions->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, &SettingsDialog::restoreDefaults);
	DialogLayout::install(*this, QStringLiteral("Settings"),
		QStringLiteral("These settings apply to every case. The settings of the open case are in Case Settings."), body, actions);
	DialogLayout::fitWidthToContent(*this);
}

void SettingsDialog::restoreDefaults() {
	m_recentCases->clear();
	m_showPassengers->setChecked(false);
	m_advancedDetails->setChecked(false);
	m_updateChecks->setChecked(false);
	m_updateCheckState = UpdateCheckState::Unknown;
	m_usage->setChecked(false);
	m_diagnostics->setChecked(false);
}

void SettingsDialog::accept() {
	const bool usage = m_usage->isChecked();
	const bool diagnostics = m_diagnostics->isChecked();
	if (m_consent.available() && (usage != m_consent.usageEnabled() || diagnostics != m_consent.diagnosticsEnabled())
		&& !m_consent.save(usage, diagnostics)) {
		DialogLayout::setStatus(m_status, DialogLayout::Severity::Error,
			QStringLiteral("Could not save the privacy choices. No consent is active. Check settings storage permissions and "
						   "restart EGTRAIN."));
		return;
	}
	if (m_recentCases->count() == 0)
		m_settings.remove(kRecentScenesKey);
	m_settings.setValue(kShowPassengersKey, m_showPassengers->isChecked());
	m_settings.setValue(kAdvancedDetailsKey, m_advancedDetails->isChecked());
	if (m_updateCheckState != m_openedUpdateCheckState)
		writeUpdateCheckState(m_settings, m_updateCheckState);
	m_settings.sync();
	QDialog::accept();
}
