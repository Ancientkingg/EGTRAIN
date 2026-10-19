#include "widgets/SettingsDialog.h"

#include "telemetry/TelemetryConsent.h"
#include "update/UpdateSettings.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>

static bool check(bool ok, const char* text) {
	if (!ok)
		std::cerr << "failed: " << text << '\n';
	return ok;
}

static QCheckBox* box(const SettingsDialog& dialog, const char* name) {
	return dialog.findChild<QCheckBox*>(QString::fromLatin1(name));
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	QTemporaryDir dir;
	if (!dir.isValid())
		return 1;
	const QString file = dir.filePath(QStringLiteral("settings.ini"));
	const TelemetryContext context{true, true, true, QStringLiteral("https://example.org/collect"), QStringLiteral("1")};
	{
		QSettings settings(file, QSettings::IniFormat);
		settings.setValue(kRecentScenesKey, QStringList{QStringLiteral("/cases/a.egscene"), QStringLiteral("/cases/b")});
		settings.sync();
	}
	bool ok = true;

	// Each setting changed in the window is read back by a new settings object.
	{
		QSettings settings(file, QSettings::IniFormat);
		TelemetryConsent consent(settings, context);
		SettingsDialog dialog(settings, consent);
		ok &= check(dialog.findChild<QListWidget*>(QStringLiteral("settingsRecentCases"))->count() == 2, "the recent cases are listed");
		dialog.findChild<QPushButton*>(QStringLiteral("settingsClearRecentCases"))->click();
		for (const char* name : {"settingsShowPassengers", "settingsAdvancedDetails", "settingsAutomaticUpdateChecks", "settingsUsage",
				 "settingsDiagnostics"})
			box(dialog, name)->setChecked(true);
		dialog.accept();
	}
	{
		QSettings settings(file, QSettings::IniFormat);
		TelemetryConsent consent(settings, context);
		ok &= check(!settings.contains(kRecentScenesKey), "Clear removes the recent cases");
		ok &= check(settings.value(kShowPassengersKey).toBool(), "show passengers is saved");
		ok &= check(settings.value(kAdvancedDetailsKey).toBool(), "advanced details are saved");
		ok &= check(readUpdateCheckState(settings) == UpdateCheckState::Enabled, "automatic update checks are saved");
		ok &= check(consent.usageEnabled() && consent.diagnosticsEnabled(), "the privacy choices are saved");
	}

	// Restore Defaults resets every setting of the window.
	{
		QSettings settings(file, QSettings::IniFormat);
		settings.setValue(kRecentScenesKey, QStringList{QStringLiteral("/cases/a.egscene")});
		TelemetryConsent consent(settings, context);
		SettingsDialog dialog(settings, consent);
		dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::RestoreDefaults)->click();
		dialog.accept();
	}
	{
		QSettings settings(file, QSettings::IniFormat);
		TelemetryConsent consent(settings, context);
		ok &= check(!settings.contains(kRecentScenesKey), "Restore Defaults clears the recent cases");
		ok &= check(!settings.value(kShowPassengersKey).toBool() && !settings.value(kAdvancedDetailsKey).toBool(),
			"Restore Defaults turns the display settings off");
		ok &= check(readUpdateCheckState(settings) == UpdateCheckState::Unknown, "Restore Defaults asks about update checks again");
		ok &= check(!consent.usageEnabled() && !consent.diagnosticsEnabled(), "Restore Defaults turns the privacy choices off");
	}
	return ok ? 0 : 1;
}
