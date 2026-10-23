#ifndef REPLAYCOVERAGE_H
#define REPLAYCOVERAGE_H

#include "app/GuiReplayHistory.h"
#include "util/TimeFormat.h"

#include <optional>
#include <string>

// The words of the replay row: the selected time, and which part of the run the replay holds when it is not all of it.
// The window reads the history into a ReplayCoverage, asks for the text and writes it into the two labels of the row and
// the tooltip of the Start button.
//
// The first text names the selected time as a clock time, when there is one. The second is empty for a whole run and
// otherwise says where the replay starts and why. It does not depend on the selected time, so the row keeps its height
// while the user seeks. The interval is the one the slider covers, and Start goes to its first time.

/// How much of the run the replay holds.
enum class ReplayCoverageKind {
	WholeRun,	///< no frame was dropped, so the replay covers the whole run
	FromLater,	///< the first part of the run was dropped to stay within the replay memory
	Unavailable ///< not one frame fits in the replay memory
};

/// What the replay history holds, as the replay row describes it.
struct ReplayCoverage {
	ReplayCoverageKind kind = ReplayCoverageKind::Unavailable; ///< How much of the run is kept.
	/// First time that is kept, in seconds since simulation start.
	int firstTime = 0;
	/// Last time that is kept, in seconds since simulation start.
	int lastTime = 0;
	/// Simulation start, seconds since midnight (the offset formatSimTime takes).
	long long clockOffsetSeconds = 0;
};

/// The texts of the replay row and whether its controls can be used.
struct ReplayBarText {
	/// For the label beside the slider: the selected time as a clock time such as "08:20:30", "Replay" while no frame is
	/// selected, or "No replay".
	std::string selected;
	/// For the label below the controls: empty for a whole run, else where the replay starts and why.
	std::string coverage;
	/// Tooltip of the Start button: the start of the run, or the start of the replay when the first part was not kept.
	std::string startTip;
	/// The slider and the Play button can be used.
	bool usable = false;
};

/// The coverage of `history`, with the clock time of simulation start in seconds since midnight. A history without
/// frames cannot be replayed, whether it was never filled or one frame was too large for it.
inline ReplayCoverage replayCoverageOf(const GuiReplayHistory& history, long long clockOffsetSeconds) {
	ReplayCoverage out;
	if (!history.empty())
		out.kind = history.truncated() ? ReplayCoverageKind::FromLater : ReplayCoverageKind::WholeRun;
	out.firstTime = history.firstTime();
	out.lastTime = history.lastTime();
	out.clockOffsetSeconds = clockOffsetSeconds;
	return out;
}

/// The texts of the replay row for `coverage`. shownTime is the time of the frame on the canvas, in seconds since
/// simulation start. It is left out while no frame has been selected yet, as when the run has just finished.
inline ReplayBarText replayBarText(const ReplayCoverage& coverage, std::optional<int> shownTime = std::nullopt) {
	ReplayBarText out;
	out.startTip = "Go to the start of the run";
	if (coverage.kind == ReplayCoverageKind::Unavailable) {
		out.selected = "No replay";
		out.coverage = "This run is too large to replay.";
		return out;
	}
	out.usable = true;
	out.selected = shownTime ? formatSimTime(*shownTime, coverage.clockOffsetSeconds) : "Replay";
	if (coverage.kind == ReplayCoverageKind::WholeRun)
		return out;
	const std::string start = formatSimTime(coverage.firstTime, coverage.clockOffsetSeconds);
	out.coverage = "Replay starts at " + start + ". Earlier times did not fit in memory.";
	out.startTip = "Go to the start of the replay (" + start + ")";
	return out;
}

#endif
