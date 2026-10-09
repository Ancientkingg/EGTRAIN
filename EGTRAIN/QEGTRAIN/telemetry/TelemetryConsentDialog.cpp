#include "telemetry/TelemetryConsentDialog.h"
#include "telemetry/TelemetryConsent.h"
#include "widgets/DialogLayout.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

TelemetryConsentDialog::TelemetryConsentDialog(TelemetryConsent& consent, const QString& domain,
	bool initialPrompt, QWidget* parent)
	: QDialog(parent), m_consent(consent), m_initialPrompt(initialPrompt) {
	setObjectName(QStringLiteral("telemetryConsentDialog"));
	setWindowTitle(QStringLiteral("Help improve EGTRAIN"));
	setModal(true);
	auto* body = new QWidget;
	auto* layout = new QVBoxLayout(body);
	m_usage = new QCheckBox(QStringLiteral("Share usage statistics"), body);
	m_usage->setObjectName(QStringLiteral("telemetryUsage"));
	m_usage->setChecked(initialPrompt ? true : consent.usageEnabled());
	m_diagnostics = new QCheckBox(QStringLiteral("Share diagnostic reports"), body);
	m_diagnostics->setObjectName(QStringLiteral("telemetryDiagnostics"));
	m_diagnostics->setChecked(initialPrompt ? true : consent.diagnosticsEnabled());
	auto addText = [layout, body](const QString& text) {
		auto* label = new QLabel(text, body);
		label->setTextFormat(Qt::PlainText);
		label->setWordWrap(true);
		layout->addWidget(label);
	};
	layout->addWidget(m_usage);
	addText(QStringLiteral("Application version, platform and predefined feature usage. No project contents, file names or simulation inputs. A random pseudonymous installation ID counts participating installations, not people. It is removed when usage is disabled."));
	layout->addWidget(m_diagnostics);
	addText(QStringLiteral("Structured application errors and technical context. No native crash dumps or usage installation ID. No project data or raw error messages."));
	addText(QStringLiteral("Example only, not collected from this computer: usage: session.started, application version 1.0.0, platform macOS; diagnostics: operation.failed, operation_code export, error_code io_failure. These categories are sent separately."));
	addText(QStringLiteral("Receiving domain: %1. No privacy notice is currently supplied. The network may reveal your IP address to the receiver. Previous uploads cannot be recalled. You can change these choices later in Help > Privacy & diagnostics.").arg(domain));
	auto* status = new QLabel(body);
	status->setObjectName(QStringLiteral("telemetryConsentStatus"));
	status->setProperty("dialogStatus", "error");
	status->setWordWrap(true);
	status->hide();
	layout->addWidget(status);
	auto* actions = new QDialogButtonBox(this);
	auto* save = actions->addButton(QStringLiteral("Save choices"), QDialogButtonBox::AcceptRole);
	save->setObjectName(QStringLiteral("saveTelemetryChoices"));
	auto* dismiss = actions->addButton(QStringLiteral("Not now"), QDialogButtonBox::RejectRole);
	dismiss->setObjectName(QStringLiteral("dismissTelemetryChoices"));
	dismiss->setDefault(true);
	save->setAutoDefault(false);
	DialogLayout::install(*this, QStringLiteral("Help improve EGTRAIN"),
		initialPrompt
			? QStringLiteral("Both categories are optional. EGTRAIN works normally without them. The prechecked choices do nothing until you save them.")
			: QStringLiteral("Both categories are optional. EGTRAIN works normally without them. Your saved choices remain active until you save changes."),
		body, actions);
	connect(save, &QPushButton::clicked, this, [this, status] {
		if (!m_consent.save(m_usage->isChecked(), m_diagnostics->isChecked())) {
			status->setText(QStringLiteral("Could not save your choices. No consent is active. Check settings storage permissions and restart EGTRAIN."));
			status->show();
			return;
		}
		accept();
	});
	connect(dismiss, &QPushButton::clicked, this, &TelemetryConsentDialog::reject);
}

void TelemetryConsentDialog::reject() {
	if (m_initialPrompt) m_consent.dismiss();
	QDialog::reject();
}
