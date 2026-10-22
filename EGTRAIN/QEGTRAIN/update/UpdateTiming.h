#ifndef EGTRAIN_UPDATE_TIMING_H
#define EGTRAIN_UPDATE_TIMING_H

// The update helper includes this header too, so it uses no Qt.

/// How long the update helper watches the new version for an early failure, in milliseconds. A version
/// that fails within it is replaced by the backup of the previous installation. SelfUpdater passes no
/// --observe-ms, so every update uses this window.
#if defined(_WIN32)
constexpr unsigned kUpdateObserveMs = 3000;
#else
constexpr unsigned kUpdateObserveMs = 500;
#endif

/// How long after its start the application sweeps the folder of its installation a second time and
/// removes the backup of the previous installation (SelfUpdater::removeUpdateBackup). The new version
/// starts inside the observation window, and the helper needs the backup until the window ends.
constexpr unsigned kSecondSweepDelayMs = 20000;
static_assert(kSecondSweepDelayMs > kUpdateObserveMs, "the backup must outlive the observation window");

#endif
