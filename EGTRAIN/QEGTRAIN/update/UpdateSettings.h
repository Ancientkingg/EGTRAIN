#ifndef EGTRAIN_UPDATE_SETTINGS_H
#define EGTRAIN_UPDATE_SETTINGS_H

class QSettings;

enum class UpdateCheckState {
	Unknown,
	Enabled,
	Disabled
};

UpdateCheckState readUpdateCheckState(const QSettings& settings);
void writeUpdateCheckState(QSettings& settings, UpdateCheckState state);
bool shouldCheckForUpdates(UpdateCheckState state, bool manual);
bool updatesSuppressedByEnvironment();

#endif
