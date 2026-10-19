#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QListWidget;
class QSettings;
class TelemetryConsent;
enum class UpdateCheckState;

// The keys of the application settings that the Settings window and the menu entries share. The automatic update
// checks are in update/UpdateSettings.h and the privacy choices in telemetry/TelemetryConsent.h.
/// The setting that lists the cases of File > Recent Scenes and of the Recent cases group in Open a Case.
constexpr const char* kRecentScenesKey = "recentScenes";
/// The setting behind the Advanced Details switch.
constexpr const char* kAdvancedDetailsKey = "advancedDetails";
/// The setting that remembers View > Show Passengers.
constexpr const char* kShowPassengersKey = "showPassengers";

/// The Settings window: every application setting in one place. Settings that belong to a case are in Case Settings.
/// The window reads the settings when it opens and writes them when OK is pressed; the caller then updates the menu
/// entries. Restore Defaults puts every control at its default and writes nothing until OK.
class SettingsDialog : public QDialog {
public:
	/// Builds the window from the current values in `settings` and `consent`. Both must outlive the window.
	SettingsDialog(QSettings& settings, TelemetryConsent& consent, QWidget* parent = nullptr);

	/// Saves the privacy choices first; when that fails, shows the error and keeps the window open. Otherwise writes
	/// every other setting and closes the window.
	void accept() override;
	/// Puts every control at its default: no recent cases, every switch off and the update check choice unset.
	void restoreDefaults();

private:
	QSettings& m_settings;
	TelemetryConsent& m_consent;
	QListWidget* m_recentCases = nullptr;
	QCheckBox* m_showPassengers = nullptr;
	QCheckBox* m_advancedDetails = nullptr;
	QCheckBox* m_updateChecks = nullptr;
	QCheckBox* m_usage = nullptr;
	QCheckBox* m_diagnostics = nullptr;
	QLabel* m_status = nullptr;
	// Restore Defaults removes the choice about automatic checks, so EGTRAIN asks again at the next start.
	UpdateCheckState m_updateCheckState{};
	// The state when the window opened. OK writes the state only when it differs, so it keeps a choice that the update
	// prompt made while the window was open.
	UpdateCheckState m_openedUpdateCheckState{};
};
