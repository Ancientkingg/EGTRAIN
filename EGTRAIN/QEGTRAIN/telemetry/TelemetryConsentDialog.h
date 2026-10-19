#pragma once

#include <QDialog>

class QCheckBox;
class TelemetryConsent;

/// Modal dialog with the two telemetry choices, usage statistics and diagnostic reports. Save
/// choices writes both through TelemetryConsent::save and keeps the dialog open with an error when
/// that fails.
class TelemetryConsentDialog : public QDialog {
	Q_OBJECT
public:
	/// consent is not owned and must outlive the dialog. domain is shown as the receiving domain. On
	/// the initial prompt both boxes start checked and closing the dialog saves both choices as
	/// disabled; otherwise the boxes show the saved choices and closing changes nothing.
	TelemetryConsentDialog(TelemetryConsent& consent, const QString& domain,
		bool initialPrompt, QWidget* parent = nullptr);
	/// The usage statistics check box, owned by the dialog.
	QCheckBox* usageCheckBox() const { return m_usage; }
	/// The diagnostic reports check box, owned by the dialog.
	QCheckBox* diagnosticsCheckBox() const { return m_diagnostics; }
protected:
	/// Not now, Escape and closing the window. On the initial prompt it saves both choices as disabled.
	void reject() override;
private:
	TelemetryConsent& m_consent;
	bool m_initialPrompt;
	QCheckBox* m_usage;
	QCheckBox* m_diagnostics;
};
