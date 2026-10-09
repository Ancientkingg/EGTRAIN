#include "app/FollowAvailability.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char* message) {
	if (!condition) {
		std::cerr << message << '\n';
		std::exit(1);
	}
}

GuiTrainState trainEnteringAt(int departureTime) {
	GuiTrainState train;
	train.index = 3;
	train.departureTime = departureTime;
	train.routeAxisPosition = 0.0;
	train.wagonHeadPositions = {10.0, 5.0};
	train.wagonTailPositions = {8.0, 3.0};
	return train;
}

FollowAvailabilityInput runOf(const GuiTrainState& train, int timestep) {
	FollowAvailabilityInput in;
	in.sceneHasServices = true;
	in.hasRun = true;
	in.train = &train;
	in.timestep = timestep;
	in.clockOffsetSeconds = 8 * 3600;
	in.drawable = true;
	in.name = "Rail-1";
	return in;
}
}

int main() {
	// A case without services keeps the existing text, whatever else is known.
	{
		FollowAvailabilityInput in;
		in.hasRun = true;
		const FollowAvailability a = followAvailability(in);
		require(a.phase == FollowPhase::NoServices && a.entryText == "No trains to follow"
				&& a.statusText == "This case has no trains to follow." && !a.canArm && !a.canAct,
			"a case without services was not reported as having no trains");
	}

	// A case without services that has not run says the same, not that it has to run first.
	{
		const FollowAvailability a = followAvailability(FollowAvailabilityInput{});
		require(a.phase == FollowPhase::NoServices && a.entryText == "No trains to follow" && !a.canArm && !a.canAct,
			"a case without services that has not run was not reported as having no trains");
	}

	// Services but no run yet.
	{
		FollowAvailabilityInput in;
		in.sceneHasServices = true;
		const FollowAvailability a = followAvailability(in);
		require(a.phase == FollowPhase::NoRun && a.entryText == "Run the case to follow trains"
				&& a.statusText == "Run the case first, then you can follow a train." && !a.canArm && !a.canAct,
			"a case that has not run was not reported as such");
	}

	// A run whose displayed snapshot does not hold the train.
	{
		const GuiTrainState none;
		FollowAvailabilityInput in = runOf(none, 0);
		in.train = nullptr;
		const FollowAvailability a = followAvailability(in);
		require(a.phase == FollowPhase::NotEntered && a.entryText == "Rail-1 (not entered yet)"
				&& a.statusText == "Rail-1 has not entered the network yet. Follow starts when it enters."
				&& a.canArm && !a.canAct,
			"a train missing from the snapshot was not reported as not entered");
	}

	// Not entered: one second before the entry time, with the scheduled time on the clock.
	const GuiTrainState train = trainEnteringAt(1550);
	{
		const FollowAvailability a = followAvailability(runOf(train, 1549));
		require(a.phase == FollowPhase::NotEntered && a.entryText == "Rail-1 (enters 08:25:50)"
				&& a.statusText == "Rail-1 has not entered the network yet. It is scheduled to enter at 08:25:50. "
								   "Follow starts when it enters."
				&& a.canArm && !a.canAct,
			"a train before its entry time was not reported as not entered");
	}

	// Running: the second the train enters.
	{
		const FollowAvailability a = followAvailability(runOf(train, 1550));
		require(a.phase == FollowPhase::Running && a.entryText == "Rail-1 (running)"
				&& a.statusText == "Rail-1 is running." && a.canArm && a.canAct,
			"a train at its entry time was not reported as running");
	}

	// A train past its entry time without a position has not entered yet.
	{
		GuiTrainState noPosition = trainEnteringAt(100);
		noPosition.routeAxisPosition = -9999.0;
		require(followAvailability(runOf(noPosition, 200)).phase == FollowPhase::NotEntered,
			"a train without a position was reported as running");
		GuiTrainState noWagons = trainEnteringAt(100);
		noWagons.wagonHeadPositions.clear();
		require(followAvailability(runOf(noWagons, 200)).phase == FollowPhase::NotEntered,
			"a train without wagon positions was reported as running");
	}

	// Finished: the last second in the network and the first second out of it.
	// The stale position of a finished train does not make it run.
	GuiTrainState finished = train;
	finished.outOfSimulation = true;
	{
		require(followAvailability(runOf(train, 2999)).phase == FollowPhase::Running,
			"the last second in the network was not running");
		const FollowAvailability live = followAvailability(runOf(finished, 3000));
		require(live.phase == FollowPhase::Finished && live.entryText == "Rail-1 (finished)"
				&& live.statusText == "Rail-1 has left the network. Follow is switched off."
				&& !live.canArm && !live.canAct,
			"a finished train in a live run was not refused");
		FollowAvailabilityInput replay = runOf(finished, 3000);
		replay.replay = true;
		const FollowAvailability back = followAvailability(replay);
		require(back.phase == FollowPhase::Finished && back.entryText == "Rail-1 (finished)"
				&& back.statusText == "Rail-1 has left the network at this time. "
									  "Follow stays on and continues when you go back in the replay."
				&& back.canArm && !back.canAct,
			"a finished train in a replay did not stay armed");
	}

	// The Trains layer and a missing geometry only matter for a train in the network.
	{
		FollowAvailabilityInput hidden = runOf(train, 2000);
		hidden.trainsLayerVisible = false;
		const FollowAvailability a = followAvailability(hidden);
		require(a.phase == FollowPhase::HiddenByLayer && a.entryText == "Rail-1 (hidden)"
				&& a.statusText == "Rail-1 is running, but the Trains layer is switched off, so the view does not follow it."
				&& a.canArm && !a.canAct,
			"a train hidden by the layer switch was not reported as hidden");
		FollowAvailabilityInput blank = runOf(train, 2000);
		blank.drawable = false;
		const FollowAvailability b = followAvailability(blank);
		require(b.phase == FollowPhase::NotDrawable && b.entryText == "Rail-1 (no position)"
				&& b.statusText == "Rail-1 is running, but it has no position on the map to follow."
				&& b.canArm && !b.canAct,
			"a train without a drawn position was not reported as such");
		hidden.train = &finished;
		require(followAvailability(hidden).phase == FollowPhase::Finished,
			"the layer switch changed the phase of a finished train");
		hidden.train = &train;
		hidden.timestep = 1000;
		require(followAvailability(hidden).phase == FollowPhase::NotEntered,
			"the layer switch changed the phase of a train that has not entered");
		FollowAvailabilityInput both = runOf(train, 2000);
		both.trainsLayerVisible = false;
		both.drawable = false;
		require(followAvailability(both).phase == FollowPhase::HiddenByLayer,
			"a train hidden by the layer was reported as not drawable");
	}

	// The phase depends on the displayed frame only: stepping back across both boundaries gives the same answers.
	{
		const int times[] = {0, 1549, 1550, 2999, 1549, 0};
		const FollowPhase expected[] = {FollowPhase::NotEntered, FollowPhase::NotEntered, FollowPhase::Running,
			FollowPhase::Running, FollowPhase::NotEntered, FollowPhase::NotEntered};
		for (int i = 0; i < 6; ++i)
			require(followPhase(runOf(train, times[i])) == expected[i], "the phase depended on an earlier time");
	}

	// The clock wraps and shows the same format as the rest of the window.
	{
		const GuiTrainState late = trainEnteringAt(3600);
		FollowAvailabilityInput in = runOf(late, 0);
		in.clockOffsetSeconds = 23 * 3600;
		require(followAvailability(in).entryText == "Rail-1 (enters 00:00:00)",
			"the entry time was not formatted like other clock times");
	}
	return 0;
}
