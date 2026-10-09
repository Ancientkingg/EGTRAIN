#pragma once

#ifndef EGTRAIN_ISOLATED_TELEMETRY_SMOKE
#error "Telemetry smoke support is test-only"
#endif
#include "telemetry/TelemetrySender.h"
#include <atomic>
#include <memory>
#include <string>

namespace telemetry_smoke {
struct State {
	std::atomic<int> polls{0};
	std::atomic<int> posts{0};
	std::atomic<int> startedPosts{0};
	int installFailurePoint = -1; // GUI-thread test driver only.
	int installFailures = 0;
	int completedProbeRuns = 0;
};
telemetry::TelemetrySender::TestOptions options(const QString& root, const QString& mode,
	std::shared_ptr<State> state);
// Invoked only by the isolated scripted driver. Never changes chooser behavior
// in an ordinary build. A false readiness predicate keeps the modal chooser open.
void chooseFile(const QString& path, std::function<bool()> ready = {},
	std::function<void()> selected = {});
void chooseDirectory(const QString& path, const QString& title, std::function<bool()> ready = {});
void failObservationInstallation(State* state, int point);
bool recordRun(const QString& root, int ordinal, const std::string& csv, const QString& energyPath);
void dismissMessages();
}
