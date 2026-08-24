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

bool updatesSuppressedByEnvironment() {
	const QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	if (environment.contains(QStringLiteral("QEGTRAIN_DISABLE_UPDATES"))
		|| environment.contains(QStringLiteral("QEGTRAIN_AUTOSTART")))
		return true;
	for (const QString& key : environment.keys())
		if (key.startsWith(QStringLiteral("QEGTRAIN_E2E_")))
			return true;
	return false;
}
