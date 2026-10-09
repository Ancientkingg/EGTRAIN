#ifndef REPLAYCOVERAGE_H
#define REPLAYCOVERAGE_H

#include "app/GuiReplayHistory.h"
#include "util/TimeFormat.h"

#include <cstddef>
#include <optional>
#include <string>

// The words of the replay row: which part of the run the replay holds, and why it is not all of
// it. The window reads the history into a ReplayCoverage, asks for the text and writes it into
// the label of the row and the tooltip of the Start button.
//
// The sentence starts with the selected time, when there is one, then says which interval is
// kept and how often, and last why the first part of the run is missing. The interval is the one
// the slider covers, and Start goes to its first time.

enum class ReplayCoverageKind {
	WholeRun,	// every frame of the run is kept
	FromLater,	// the first part of the run was dropped to stay within the replay memory
	Unavailable // not one frame fits in the replay memory
};

struct ReplayCoverage {
	ReplayCoverageKind kind = ReplayCoverageKind::Unavailable;
	// First and last time that is kept, in seconds since simulation start.
	int firstTime = 0;
	int lastTime = 0;
	int cadenceSeconds = GuiReplayHistory::cadenceSeconds;
	// Size of the replay memory in bytes.
	std::size_t budgetBytes = GuiReplayHistory::payloadLimit;
	// Simulation start, seconds since midnight (the offset formatSimTime takes).
	long long clockOffsetSeconds = 0;
};

struct ReplayBarText {
	// Sentence for the label of the row.
	std::string label;
	// Tooltip of the Start button. It is empty while Start goes to the start of the run.
	std::string startTip;
	// The slider and the Play button can be used.
	bool usable = false;
};

// A history without frames cannot be replayed, whether it was never filled or one frame was too
// large for it.
inline ReplayCoverage replayCoverageOf(const GuiReplayHistory& history, long long clockOffsetSeconds) {
	ReplayCoverage out;
	if (!history.empty())
		out.kind = history.truncated() ? ReplayCoverageKind::FromLater : ReplayCoverageKind::WholeRun;
	out.firstTime = history.firstTime();
	out.lastTime = history.lastTime();
	out.cadenceSeconds = GuiReplayHistory::cadenceSeconds;
	out.budgetBytes = history.budgetBytes();
	out.clockOffsetSeconds = clockOffsetSeconds;
	return out;
}

// The size of the replay memory: whole mebibytes as "128 MiB", otherwise one decimal as "2.5 MiB".
inline std::string replayMemoryText(std::size_t bytes) {
	const std::size_t mebibyte = 1024u * 1024u;
	if (bytes % mebibyte == 0)
		return std::to_string(bytes / mebibyte) + " MiB";
	const std::size_t tenths = (bytes * 10 + mebibyte / 2) / mebibyte;
	return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " MiB";
}

// shownTime is the time of the frame on the canvas. It is left out while no frame has been
// selected yet, as when the run has just finished.
inline ReplayBarText replayBarText(const ReplayCoverage& coverage, std::optional<int> shownTime = std::nullopt) {
	ReplayBarText out;
	const std::string memory = replayMemoryText(coverage.budgetBytes);
	if (coverage.kind == ReplayCoverageKind::Unavailable) {
		out.label = "No replay: one moment of this run needs more than the replay memory (" + memory + ").";
		return out;
	}
	out.usable = true;
	const std::string head = shownTime ? "Replay at " + std::to_string(*shownTime) + " s: " : "Replay: ";
	const bool oneFrame = coverage.firstTime >= coverage.lastTime;
	const std::string first = std::to_string(coverage.firstTime);
	const std::string last = std::to_string(coverage.lastTime);
	const std::string cadence = "a frame every " + std::to_string(coverage.cadenceSeconds) + " s";
	if (coverage.kind == ReplayCoverageKind::WholeRun) {
		out.label = head + "whole run, " + (oneFrame ? "one frame at " + first + " s" : first + " to " + last + " s, " + cadence) + ".";
		return out;
	}
	const std::string start = formatSimTime(coverage.firstTime, coverage.clockOffsetSeconds) + " (" + first + " s)";
	out.label = head + (oneFrame ? "one frame at " + start : "starts at " + start + ", ends at " + last + " s, " + cadence)
		+ ". The earlier part was not kept because the run is larger than the replay memory (" + memory + ").";
	out.startTip = "Go to the first kept time, " + start + ". The earlier part of the run was not kept.";
	return out;
}

#endif
