#pragma once

#ifndef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
#error "Telemetry smoke support is test-only"
#endif
#include "telemetry/TelemetrySender.h"
#include <atomic>
#include <memory>
#include <string>

/// Helpers of the isolated telemetry smoke test, which drives the window with a scripted telemetry endpoint.
namespace telemetry_smoke {
/// What the scripted sender and the driver have seen.
struct State {
	std::atomic<int> polls{0};		  ///< Polls of the sender.
	std::atomic<int> posts{0};		  ///< Posts that the sender made.
	std::atomic<int> startedPosts{0}; ///< Posts that hold a simulation.started event.
	/// The step of the run observation that failObservationInstallation() makes fail, or -1 for none. GUI-thread test
	/// driver only.
	int installFailurePoint = -1;
	int installFailures = 0;	///< Failures that failObservationInstallation() made.
	int completedProbeRuns = 0; ///< Runs of the installation probe that have completed.
};
/// Sender options for the smoke test: settings in `root`/consent.ini, a clock that runs 100 times as fast, and posts
/// that are saved under `root`/payloads and answered as `mode` says ("offline" fails, "retired" answers 410, any other
/// mode 202).
telemetry::TelemetrySender::TestOptions options(const QString& root, const QString& mode,
	std::shared_ptr<State> state);
/// Answers the next modal file dialog with `path`, or cancels it when `path` is empty. Invoked only by the isolated
/// scripted driver. Never changes chooser behavior in an ordinary build. A false readiness predicate keeps the modal
/// chooser open. `selected` is called when the dialog selects the file.
void chooseFile(const QString& path, std::function<bool()> ready = {},
	std::function<void()> selected = {});
/// Answers the next modal folder dialog with the title `title` with `path`, once `ready` returns true.
void chooseDirectory(const QString& path, const QString& title, std::function<bool()> ready = {});
/// Throws std::bad_alloc when `point` is the installFailurePoint of `state`, and counts the failure.
void failObservationInstallation(State* state, int point);
/// Writes `csv` to install_run_N.csv in `root` and copies the energy file next to it, for an `ordinal` N from 0 to
/// 4. Returns whether both were written.
bool recordRun(const QString& root, int ordinal, const std::string& csv, const QString& energyPath);
/// Answers every message box that opens from now on with Yes, Save or Ok, or accepts it.
void dismissMessages();
}
