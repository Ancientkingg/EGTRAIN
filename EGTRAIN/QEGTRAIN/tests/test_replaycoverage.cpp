#include "app/ReplayCoverage.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {
void require(bool condition, const char* message) {
	if (!condition) {
		std::cerr << message << '\n';
		std::exit(1);
	}
}

ReplayCoverage coverage(ReplayCoverageKind kind, int first, int last) {
	ReplayCoverage out;
	out.kind = kind;
	out.firstTime = first;
	out.lastTime = last;
	out.clockOffsetSeconds = 8 * 3600;
	return out;
}

// A frame with a few hundred values that change, so that a few of them fill a small budget.
std::shared_ptr<const GuiSimulationSnapshot> frameAt(int time, int total) {
	auto frame = std::make_shared<GuiSimulationSnapshot>();
	frame->timestep = time;
	frame->totalTimesteps = total;
	frame->signalStates.resize(200);
	for (GuiSignalState& signal : frame->signalStates)
		signal.code = time % 3;
	return frame;
}

bool mentionsInternals(const std::string& text) {
	return text.find("evict") != std::string::npos || text.find("payload") != std::string::npos || text.find("budget") != std::string::npos;
}
}

int main() {
	// A run that is kept whole: no text under the controls, since nothing is missing. The selected time is a clock time
	// after the start of the run, 08:00:00.
	{
		const ReplayCoverage whole = coverage(ReplayCoverageKind::WholeRun, 0, 7999);
		const ReplayBarText idle = replayBarText(whole);
		require(idle.selected == "Replay" && idle.coverage.empty() && idle.startTip == "Go to the start of the run" && idle.usable,
			"the row of a whole run was not worded as such");
		const ReplayBarText shown = replayBarText(whole, 1234);
		require(shown.selected == "08:20:34" && shown.coverage.empty() && shown.startTip == idle.startTip && shown.usable,
			"the row of a whole run did not say the selected time");
		require(replayBarText(whole, 0).selected == "08:00:00", "the first second was not taken for a selected time");
	}

	// A run whose first part was dropped: where the replay starts as a clock time, why, and a tooltip for Start. 7220 s
	// after 08:00:00 is 10:00:20.
	{
		const ReplayCoverage later = coverage(ReplayCoverageKind::FromLater, 7220, 7999);
		const ReplayBarText idle = replayBarText(later);
		require(idle.selected == "Replay" && idle.coverage == "Replay starts at 10:00:20. Earlier times did not fit in memory." && idle.usable,
			"the row of a run with a dropped first part was not worded as such");
		const ReplayBarText shown = replayBarText(later, 7500);
		require(shown.selected == "10:05:00" && shown.coverage == idle.coverage,
			"the row of a run with a dropped first part did not say the selected time");
		require(idle.startTip == "Go to the start of the replay (10:00:20)" && shown.startTip == idle.startTip,
			"Start did not say that it goes to the start of the replay");
		// The second text is the same for every selected time, so the height of the row does not change while the user seeks.
		for (const int time : {7220, 7225, 7999})
			require(replayBarText(later, time).coverage == idle.coverage, "the coverage depends on the selected time");
	}

	// Unavailable: the reason, and nothing to use.
	{
		const ReplayBarText none = replayBarText(coverage(ReplayCoverageKind::Unavailable, 0, 0));
		require(none.selected == "No replay" && none.coverage == "This run is too large to replay."
				&& none.startTip == "Go to the start of the run" && !none.usable,
			"a run that cannot be replayed was not worded as such");
		const ReplayBarText asked = replayBarText(coverage(ReplayCoverageKind::Unavailable, 0, 0), 500);
		require(asked.selected == none.selected && asked.coverage == none.coverage, "an unavailable replay said a selected time");
	}

	// Boundaries: a run of one frame, the smallest dropped part, and a clock that wraps over midnight.
	{
		const ReplayBarText one = replayBarText(coverage(ReplayCoverageKind::WholeRun, 0, 0));
		require(one.coverage.empty() && one.usable, "a whole run of one frame was not worded as such");
		require(replayBarText(coverage(ReplayCoverageKind::WholeRun, 0, 0), 0).selected == "08:00:00",
			"a whole run of one frame did not say the selected time");
		const ReplayBarText last = replayBarText(coverage(ReplayCoverageKind::FromLater, 7999, 7999));
		require(last.coverage == "Replay starts at 10:13:19. Earlier times did not fit in memory."
				&& last.startTip == "Go to the start of the replay (10:13:19)" && last.usable,
			"a replay of only the last frame was not worded as such");
		const ReplayBarText one5 = replayBarText(coverage(ReplayCoverageKind::FromLater, 5, 7999));
		require(one5.coverage.find("Replay starts at 08:00:05.") != std::string::npos, "a dropped part of one frame was not worded as such");
		ReplayCoverage wrap = coverage(ReplayCoverageKind::FromLater, 1200, 4000);
		wrap.clockOffsetSeconds = 23 * 3600 + 50 * 60;
		require(replayBarText(wrap).coverage.find("Replay starts at 00:10:00.") != std::string::npos
				&& replayBarText(wrap).startTip.find("(00:10:00)") != std::string::npos
				&& replayBarText(wrap, 1200).selected == "00:10:00",
			"the clock did not wrap over midnight");
		ReplayCoverage start = coverage(ReplayCoverageKind::FromLater, 600, 4000);
		start.clockOffsetSeconds = 0;
		require(replayBarText(start).coverage.find("Replay starts at 00:10:00.") != std::string::npos, "a run that starts at midnight was not worded as such");
	}

	// The sentences use plain words for the memory.
	{
		for (const ReplayCoverageKind kind : {ReplayCoverageKind::WholeRun, ReplayCoverageKind::FromLater, ReplayCoverageKind::Unavailable}) {
			const ReplayBarText text = replayBarText(coverage(kind, 100, 900), 300);
			require(!mentionsInternals(text.selected) && !mentionsInternals(text.coverage) && !mentionsInternals(text.startTip),
				"a sentence used a word of the implementation");
		}
	}

	// The coverage of a real history: whole, with its first part dropped, too large and empty.
	{
		GuiReplayHistory whole;
		for (int time = 0; time <= 40; time += 5)
			whole.record(frameAt(time, 41));
		const ReplayCoverage kept = replayCoverageOf(whole, 3600);
		require(kept.kind == ReplayCoverageKind::WholeRun && kept.firstTime == 0 && kept.lastTime == 40 && kept.clockOffsetSeconds == 3600,
			"the coverage of a whole history was not read from it");
		require(replayBarText(kept).coverage.empty(), "the row of a whole history was not worded as such");

		GuiReplayHistory reference;
		for (int time = 0; time <= 10; time += 5)
			reference.record(frameAt(time, 41));
		GuiReplayHistory small(reference.payloadBytes());
		for (int time = 0; time <= 40; time += 5)
			small.record(frameAt(time, 41));
		require(small.truncated() && small.firstTime() > 0, "the small history did not drop its first part");
		const ReplayCoverage dropped = replayCoverageOf(small, 3600);
		require(dropped.kind == ReplayCoverageKind::FromLater && dropped.firstTime == small.firstTime() && dropped.lastTime == 40,
			"the coverage of a history with a dropped first part was not read from it");
		const ReplayBarText row = replayBarText(dropped, 40);
		const std::string start = formatSimTime(small.firstTime(), 3600);
		require(row.selected == "01:00:40" && row.coverage == "Replay starts at " + start + ". Earlier times did not fit in memory."
				&& row.startTip == "Go to the start of the replay (" + start + ")",
			"the row of a history with a dropped first part did not say where it starts");

		GuiReplayHistory tiny(1u << 20);
		auto large = std::make_shared<GuiSimulationSnapshot>();
		large->passengers.resize(1);
		large->passengers.front().id = std::string(2u << 20, 'x');
		tiny.record(large);
		const ReplayCoverage refused = replayCoverageOf(tiny, 3600);
		require(tiny.oversize() && refused.kind == ReplayCoverageKind::Unavailable,
			"the coverage of a history that refused a frame was not read from it");
		const ReplayBarText refusedText = replayBarText(refused);
		require(refusedText.selected == "No replay" && refusedText.coverage == "This run is too large to replay.",
			"the row of a history that refused a frame was not worded as such");

		const GuiReplayHistory empty;
		require(replayCoverageOf(empty, 0).kind == ReplayCoverageKind::Unavailable, "a history without frames was taken for a replay");
	}
	return 0;
}
