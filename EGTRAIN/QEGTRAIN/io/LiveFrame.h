#ifndef LIVEFRAME_H
#define LIVEFRAME_H

#include "app/GuiSimulationSnapshot.h"
#include "io/LiveCommand.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Frames of the live contract, built from the immutable snapshot of a step. The members of each
// frame and their units are described in docs/live/frame-v1.schema.json. A value that the
// snapshot does not hold, such as the position of a train that has not entered, is null in the
// frame and never a sentinel. No Qt.

// Version of the contract, in the member "v" of every frame. A change that removes or
// reinterprets a member raises it; a new member does not.
constexpr int kLiveContractVersion = 1;

// What identifies a run and sets the time axis of its frames.
struct LiveRunInfo {
	std::string runId;
	std::string sceneName;
	// Time of day of step 0, in seconds from midnight.
	double baseTimeSeconds = 0.0;
	// Number of steps of the run.
	int horizonSteps = 0;
	// Seconds per step.
	double stepSeconds = 1.0;
};

enum class LiveEvent {
	// The step loop has ended and the positions are final.
	StepsFinished,
	// The run has ended and its result files are written.
	RunCompleted,
	// The run was stopped before its last step.
	RunCancelled
};

// seq is the position of the frame in the run, counted from 0 across all frames. The caller
// assigns it.

// The frame that opens a run: its identity, time axis, units and the trains it will report.
nlohmann::json makeHelloFrame(const LiveRunInfo& run, std::uint64_t seq, const GuiSimulationSnapshot& snapshot);

// The state of one step. The signal list is part of the frame only when includeSignals is set.
nlohmann::json makeStateFrame(const LiveRunInfo& run, std::uint64_t seq, const GuiSimulationSnapshot& snapshot, bool includeSignals);

// A change in the life of the run. step is the number of steps that have run.
nlohmann::json makeEventFrame(const LiveRunInfo& run, std::uint64_t seq, LiveEvent event, int step);

// The outcome of a command that a client sent. The command is applied before the step after the
// steps that have run, or it is turned down with rejectedBecause as the reason.
nlohmann::json makeCommandEventFrame(const LiveRunInfo& run, std::uint64_t seq, const LiveCommand& command, int step,
	const char* rejectedBecause = nullptr);

// True when the signal lists differ in length, order or any value that a frame reports.
bool liveSignalsChanged(const std::vector<GuiSignalState>& previous, const std::vector<GuiSignalState>& current);

#endif // LIVEFRAME_H
