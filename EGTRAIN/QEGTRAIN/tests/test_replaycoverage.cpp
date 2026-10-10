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
	// A run that is kept whole: the interval, the cadence and nothing about a missing part.
	{
		const ReplayCoverage whole = coverage(ReplayCoverageKind::WholeRun, 0, 7999);
		const ReplayBarText idle = replayBarText(whole);
		require(idle.selected == "Replay" && idle.coverage == "Whole run, 0 to 7999 s, a frame every 5 s." && idle.startTip.empty() && idle.usable,
			"the row of a whole run was not worded as such");
		const ReplayBarText shown = replayBarText(whole, 1234);
		require(shown.selected == "Replay at 1234 s" && shown.coverage == idle.coverage && shown.startTip.empty() && shown.usable,
			"the row of a whole run did not say the selected time");
		require(replayBarText(whole, 0).selected == "Replay at 0 s", "the first second was not taken for a selected time");
		require(idle.coverage.find("not kept") == std::string::npos, "a whole run said that a part was not kept");
	}

	// A run whose first part was dropped: the start as a clock time and in seconds, the end, the
	// reason, and a tooltip for Start. 7220 s after 08:00:00 is 10:00:20.
	{
		const ReplayCoverage later = coverage(ReplayCoverageKind::FromLater, 7220, 7999);
		const std::string reason = "The earlier part was not kept because the run is larger than the replay memory (128 MiB).";
		const ReplayBarText idle = replayBarText(later);
		require(idle.selected == "Replay" && idle.coverage == "Starts at 10:00:20 (7220 s), ends at 7999 s, a frame every 5 s. " + reason && idle.usable,
			"the row of a run with a dropped first part was not worded as such");
		const ReplayBarText shown = replayBarText(later, 7500);
		require(shown.selected == "Replay at 7500 s" && shown.coverage == idle.coverage,
			"the row of a run with a dropped first part did not say the selected time");
		require(idle.startTip == "Go to the first kept time, 10:00:20 (7220 s). The earlier part of the run was not kept."
				&& shown.startTip == idle.startTip,
			"Start did not say that it goes to the first kept time");
		// The start comes before the reason, so a narrow row shows it first.
		require(idle.coverage.find("10:00:20") < idle.coverage.find("not kept"), "the start does not come before the reason");
		// The second text is the same for every selected time, so the height of the row does not change while the user seeks.
		for (const int time : {7220, 7225, 7999})
			require(replayBarText(later, time).coverage == idle.coverage, "the coverage depends on the selected time");
	}

	// Unavailable: the reason and the size of the memory, and nothing to use.
	{
		const ReplayBarText none = replayBarText(coverage(ReplayCoverageKind::Unavailable, 0, 0));
		require(none.selected == "No replay" && none.coverage == "One moment of this run needs more than the replay memory (128 MiB)."
				&& none.startTip.empty() && !none.usable,
			"a run that cannot be replayed was not worded as such");
		const ReplayBarText asked = replayBarText(coverage(ReplayCoverageKind::Unavailable, 0, 0), 500);
		require(asked.selected == none.selected && asked.coverage == none.coverage, "an unavailable replay said a selected time");
		ReplayCoverage small = coverage(ReplayCoverageKind::Unavailable, 0, 0);
		small.budgetBytes = 4u * 1024u * 1024u;
		require(replayBarText(small).coverage == "One moment of this run needs more than the replay memory (4 MiB).",
			"the size of the replay memory was not taken from the coverage");
	}

	// Boundaries: a run of one frame, the smallest dropped part, and a clock that wraps over midnight.
	{
		const ReplayBarText one = replayBarText(coverage(ReplayCoverageKind::WholeRun, 0, 0));
		require(one.coverage == "Whole run, one frame at 0 s." && one.usable, "a whole run of one frame was not worded as such");
		require(replayBarText(coverage(ReplayCoverageKind::WholeRun, 0, 0), 0).selected == "Replay at 0 s",
			"a whole run of one frame did not say the selected time");
		const ReplayBarText last = replayBarText(coverage(ReplayCoverageKind::FromLater, 7999, 7999));
		require(last.coverage
					== "One frame at 10:13:19 (7999 s). The earlier part was not kept because the run is larger than the replay memory (128 MiB)."
				&& last.startTip == "Go to the first kept time, 10:13:19 (7999 s). The earlier part of the run was not kept." && last.usable,
			"a replay of only the last frame was not worded as such");
		const ReplayBarText one5 = replayBarText(coverage(ReplayCoverageKind::FromLater, 5, 7999));
		require(one5.coverage.find("Starts at 08:00:05 (5 s), ends at 7999 s") != std::string::npos, "a dropped part of one frame was not worded as such");
		ReplayCoverage wrap = coverage(ReplayCoverageKind::FromLater, 1200, 4000);
		wrap.clockOffsetSeconds = 23 * 3600 + 50 * 60;
		require(replayBarText(wrap).coverage.find("Starts at 00:10:00 (1200 s)") != std::string::npos
				&& replayBarText(wrap).startTip.find("00:10:00 (1200 s)") != std::string::npos,
			"the clock did not wrap over midnight");
		ReplayCoverage start = coverage(ReplayCoverageKind::FromLater, 600, 4000);
		start.clockOffsetSeconds = 0;
		require(replayBarText(start).coverage.find("Starts at 00:10:00 (600 s)") != std::string::npos, "a run that starts at midnight was not worded as such");
		ReplayCoverage cadence = coverage(ReplayCoverageKind::WholeRun, 0, 599);
		cadence.cadenceSeconds = 1;
		require(replayBarText(cadence).coverage == "Whole run, 0 to 599 s, a frame every 1 s.", "the cadence was not taken from the coverage");
	}

	// The size of the memory.
	{
		require(replayMemoryText(128u * 1024u * 1024u) == "128 MiB" && replayMemoryText(1024u * 1024u) == "1 MiB"
				&& replayMemoryText(2u * 1024u * 1024u * 1024u) == "2048 MiB",
			"whole mebibytes were not written as such");
		require(replayMemoryText(2621440) == "2.5 MiB" && replayMemoryText(300000) == "0.3 MiB" && replayMemoryText(1048577) == "1.0 MiB",
			"a size that is not whole mebibytes was not written with one decimal");
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
		require(kept.kind == ReplayCoverageKind::WholeRun && kept.firstTime == 0 && kept.lastTime == 40 && kept.cadenceSeconds == 5
				&& kept.budgetBytes == GuiReplayHistory::payloadLimit && kept.clockOffsetSeconds == 3600,
			"the coverage of a whole history was not read from it");
		require(replayBarText(kept).coverage == "Whole run, 0 to 40 s, a frame every 5 s.", "the row of a whole history was not worded as such");

		GuiReplayHistory reference;
		for (int time = 0; time <= 10; time += 5)
			reference.record(frameAt(time, 41));
		GuiReplayHistory small(reference.payloadBytes());
		for (int time = 0; time <= 40; time += 5)
			small.record(frameAt(time, 41));
		require(small.truncated() && small.firstTime() > 0, "the small history did not drop its first part");
		const ReplayCoverage dropped = replayCoverageOf(small, 3600);
		require(dropped.kind == ReplayCoverageKind::FromLater && dropped.firstTime == small.firstTime() && dropped.lastTime == 40
				&& dropped.budgetBytes == small.budgetBytes(),
			"the coverage of a history with a dropped first part was not read from it");
		const ReplayBarText row = replayBarText(dropped, 40);
		const std::string start = formatSimTime(small.firstTime(), 3600) + " (" + std::to_string(small.firstTime()) + " s)";
		require(row.selected == "Replay at 40 s" && row.coverage.find("Starts at " + start + ", ends at 40 s") != std::string::npos
				&& row.coverage.find("replay memory (" + replayMemoryText(small.budgetBytes()) + ")") != std::string::npos
				&& row.startTip == "Go to the first kept time, " + start + ". The earlier part of the run was not kept.",
			"the row of a history with a dropped first part did not say where it starts");

		GuiReplayHistory tiny(1u << 20);
		auto large = std::make_shared<GuiSimulationSnapshot>();
		large->passengers.resize(1);
		large->passengers.front().id = std::string(2u << 20, 'x');
		tiny.record(large);
		const ReplayCoverage refused = replayCoverageOf(tiny, 3600);
		require(tiny.oversize() && refused.kind == ReplayCoverageKind::Unavailable && refused.budgetBytes == (1u << 20),
			"the coverage of a history that refused a frame was not read from it");
		const ReplayBarText refusedText = replayBarText(refused);
		require(refusedText.selected == "No replay" && refusedText.coverage == "One moment of this run needs more than the replay memory (1 MiB).",
			"the row of a history that refused a frame was not worded as such");

		const GuiReplayHistory empty;
		require(replayCoverageOf(empty, 0).kind == ReplayCoverageKind::Unavailable, "a history without frames was taken for a replay");
	}
	return 0;
}
