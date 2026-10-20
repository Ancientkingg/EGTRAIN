#pragma once

#include <QDialog>

class QCheckBox;
class TelemetryConsent;

class TelemetryConsentDialog : public QDialog {
	Q_OBJECT
public:
	TelemetryConsentDialog(TelemetryConsent& consent, const QString& domain,
		bool initialPrompt, QWidget* parent = nullptr);
	QCheckBox* usageCheckBox() const { return m_usage; }
	QCheckBox* diagnosticsCheckBox() const { return m_diagnostics; }
protected:
	void reject() override;
private:
	TelemetryConsent& m_consent;
	bool m_initialPrompt;
	QCheckBox* m_usage;
	QCheckBox* m_diagnostics;
};
