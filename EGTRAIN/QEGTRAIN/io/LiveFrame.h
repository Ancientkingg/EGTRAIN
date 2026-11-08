#ifndef LIVEFRAME_H
#define LIVEFRAME_H

#include "simulation/GuiSimulationSnapshot.h"
#include "io/LiveCommand.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

/// \file
/// Frames of the live contract, built from the immutable snapshot of a step. The members of each
/// frame and their units are described in docs/live/frame-v1.schema.json, and the contract in
/// docs/architecture/external-sharing.md. Times are in seconds, positions in metres, the x coordinate
/// in kilometres and speeds in m/s. A value that the snapshot does not hold, such as the position of
/// a train that has not entered, is null in the frame and never a sentinel: the -1 that
/// Train::trainXPosition gives for a head in no block section becomes null, and so does a number that
/// is not finite. No Qt.
///
/// The frame functions only build JSON and keep no state. DispatchController calls them on the thread that
/// runs the steps. Every frame carries seq, the position of the frame in the run, counted from 0
/// across all frames; the caller assigns it.

/// Version of the contract, in the member "v" of every frame. A change that removes or
/// reinterprets a member raises it; a new member does not.
constexpr int kLiveContractVersion = 1;

/// Random lowercase hexadecimal digits: 8 for each of `words` 32-bit words from std::random_device, the high digit first.
/// The live server makes its token with it and DispatchController the id of a run.
std::string randomHex(int words);

/// What identifies a run and sets the time axis of its frames.
struct LiveRunInfo {
	std::string runId;			  ///< The id of the run in every frame. DispatchController makes 16 hexadecimal digits per run.
	std::string sceneName;		  ///< The name of the scene of the run.
	double baseTimeSeconds = 0.0; ///< Time of day of step 0, in seconds from midnight.
	int horizonSteps = 0;		  ///< Number of steps of the run.
	double stepSeconds = 1.0;	  ///< Length of one step, in seconds.
};

/// A change in the life of a run that an event frame reports.
enum class LiveEvent {
	/// "steps_finished": the step loop has ended and the positions are final. Result files may still be
	/// written.
	StepsFinished,
	/// "run_finished" with status "completed": the stages of the run have ended. Not every result file
	/// is written yet.
	RunCompleted,
	/// "run_finished" with status "cancelled": a stop was requested, and the stages after it are skipped.
	RunCancelled
};

/// The frame that opens a run: its identity, time axis, units and the trains it will report, in the
/// order of snapshot.trains. Only the identity and route index of each train are read.
nlohmann::json makeHelloFrame(const LiveRunInfo& run, std::uint64_t seq, const GuiSimulationSnapshot& snapshot);

/// The state of one step: the step, sim_time_s and clock_s in seconds, and for each train its route
/// position in metres, x in kilometres and speed in m/s, or null where the train has no position. The
/// signal list is part of the frame only when includeSignals is set.
nlohmann::json makeStateFrame(const LiveRunInfo& run, std::uint64_t seq, const GuiSimulationSnapshot& snapshot, bool includeSignals);

/// A change in the life of the run. step is the number of steps that have run.
nlohmann::json makeEventFrame(const LiveRunInfo& run, std::uint64_t seq, LiveEvent event, int step);

/// The outcome of a command that a client sent, with step as the number of steps that have run. The
/// frame is "applied" when rejectedBecause is null: the run takes the command before its next step.
/// Otherwise it is "rejected" with rejectedBecause as the reason, such as run_ended, no_active_run or
/// queue_full.
nlohmann::json makeCommandEventFrame(const LiveRunInfo& run, std::uint64_t seq, const LiveCommand& command, int step,
	const char* rejectedBecause = nullptr);

/// True when the signal lists differ in length, order or any value that a frame reports.
bool liveSignalsChanged(const std::vector<GuiSignalState>& previous, const std::vector<GuiSignalState>& current);

#endif // LIVEFRAME_H
