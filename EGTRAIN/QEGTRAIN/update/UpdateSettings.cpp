#include "update/UpdateSettings.h"

#include <QProcessEnvironment>
#include <QSettings>

namespace {
constexpr char kAutomaticUpdateChecksKey[] = "updates/automaticCheck";
}

UpdateCheckState readUpdateCheckState(const QSettings& settings) {
	if (!settings.contains(QString::fromLatin1(kAutomaticUpdateChecksKey)))
		return UpdateCheckState::Unknown;
	return settings.value(QString::fromLatin1(kAutomaticUpdateChecksKey)).toBool()
		? UpdateCheckState::Enabled : UpdateCheckState::Disabled;
}

void writeUpdateCheckState(QSettings& settings, UpdateCheckState state) {
	const QString key = QString::fromLatin1(kAutomaticUpdateChecksKey);
	if (state == UpdateCheckState::Unknown) {
		settings.remove(key);
		return;
	}
	settings.setValue(key, state == UpdateCheckState::Enabled);
}

bool shouldCheckForUpdates(UpdateCheckState state, bool manual) {
	return manual || state == UpdateCheckState::Enabled;
}

// Scripted launches have nobody to answer the consent prompt and must not reach the network:
// the explicit switch, the autostart hook, the startup timing and playback profile modes, and
// every end-to-end hook.
bool updatesSuppressedByEnvironment() {
	const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	if (environment.contains(QStringLiteral("QEGTRAIN_DISABLE_UPDATES"))
		|| environment.contains(QStringLiteral("QEGTRAIN_AUTOSTART"))
		|| environment.value(QStringLiteral("QEGTRAIN_STARTUP_TIMING")) == QLatin1String("1")
		|| environment.value(QStringLiteral("QEGTRAIN_PLAYBACK_PROFILE")) == QLatin1String("1"))
		return true;
	for (const QString& key : environment.keys())
		if (key.startsWith(QStringLiteral("QEGTRAIN_E2E_")))
			return true;
	return false;
}
