#ifndef EGTRAIN_UPDATE_SETTINGS_H
#define EGTRAIN_UPDATE_SETTINGS_H

#include "util/Version.h"

#include <optional>

class QSettings;

/// Whether the application checks for updates by itself, as the user chose.
enum class UpdateCheckState {
	Unknown, ///< The user has not chosen yet, so the application asks.
	Enabled, ///< The application checks for updates by itself.
	Disabled ///< Only the user starts a check.
};

/// The choice stored under "updates/automaticCheck", or Unknown when none is stored.
UpdateCheckState readUpdateCheckState(const QSettings& settings);
/// Stores the choice under "updates/automaticCheck". Unknown removes the key.
void writeUpdateCheckState(QSettings& settings, UpdateCheckState state);
/// True for a check that the user started (manual) and when automatic checks are enabled.
bool shouldCheckForUpdates(UpdateCheckState state, bool manual);
/// The version whose update was rolled back, once the application has told the user. Stored under
/// "updates/failedVersion"; nothing when none or no stable version is stored.
std::optional<SemanticVersion> readFailedUpdateVersion(const QSettings& settings);
/// Stores version as "X.Y.Z" under "updates/failedVersion".
void writeFailedUpdateVersion(QSettings& settings, const SemanticVersion& version);
/// An automatic check does not offer the release that failed to start; a manual check offers every release.
bool shouldOfferRelease(const SemanticVersion& release, const std::optional<SemanticVersion>& failedVersion, bool manual);
/// True for a scripted launch, which neither asks about update checks nor reaches the network: when
/// QEGTRAIN_DISABLE_UPDATES or QEGTRAIN_AUTOSTART is set, QEGTRAIN_STARTUP_TIMING or
/// QEGTRAIN_PLAYBACK_PROFILE is 1, or a variable of the end-to-end tests is set.
bool updatesSuppressedByEnvironment();

#endif
