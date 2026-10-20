#ifndef EGTRAIN_UPDATE_SETTINGS_H
#define EGTRAIN_UPDATE_SETTINGS_H

#include "util/Version.h"

#include <optional>

class QSettings;

enum class UpdateCheckState {
	Unknown,
	Enabled,
	Disabled
};

UpdateCheckState readUpdateCheckState(const QSettings& settings);
void writeUpdateCheckState(QSettings& settings, UpdateCheckState state);
bool shouldCheckForUpdates(UpdateCheckState state, bool manual);
// The version whose update was rolled back, once the application has told the user.
std::optional<SemanticVersion> readFailedUpdateVersion(const QSettings& settings);
void writeFailedUpdateVersion(QSettings& settings, const SemanticVersion& version);
// An automatic check does not offer the release that failed to start; a manual check offers every release.
bool shouldOfferRelease(const SemanticVersion& release, const std::optional<SemanticVersion>& failedVersion, bool manual);
bool updatesSuppressedByEnvironment();

#endif
