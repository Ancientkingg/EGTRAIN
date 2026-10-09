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

FollowAvailabilityInput withFollow(FollowAvailabilityInput in, bool on) {
	in.followOn = on;
	return in;
}

FollowAvailabilityInput inReplay(FollowAvailabilityInput in) {
	in.replay = true;
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
				&& a.statusText == "This case has no trains to follow." && !a.controlEnabled && !a.canArm && !a.switchOff && !a.canAct,
			"a case without services was not reported as having no trains");
	}

	// A case without services that has not run says the same, not that it has to run first.
	{
		const FollowAvailability a = followAvailability(FollowAvailabilityInput{});
		require(a.phase == FollowPhase::NoServices && a.entryText == "No trains to follow" && !a.controlEnabled && !a.canArm && !a.canAct,
			"a case without services that has not run was not reported as having no trains");
	}

	// Services but no run yet. The control is not offered, and the sentence says why.
	{
		FollowAvailabilityInput in;
		in.sceneHasServices = true;
		const FollowAvailability a = followAvailability(in);
		require(a.phase == FollowPhase::NoRun && a.entryText == "Run the case to follow trains"
				&& a.statusText == "Run the case first, then you can follow a train." && !a.controlEnabled && !a.canArm && !a.switchOff && !a.canAct,
			"a case that has not run was not reported as such");
		in.followOn = true;
		const FollowAvailability on = followAvailability(in);
		require(on.statusText == a.statusText && on.switchOff && !on.canAct,
			"Follow that was on in a case that has not run was not switched off");
	}

	// A run whose displayed snapshot does not hold the train.
	{
		const GuiTrainState none;
		FollowAvailabilityInput in = runOf(none, 0);
		in.train = nullptr;
		const FollowAvailability off = followAvailability(in);
		require(off.phase == FollowPhase::NotEntered && off.entryText == "Rail-1 (not entered yet)"
				&& off.statusText == "Rail-1 has not entered the network yet. Follow can be switched on now and starts when it enters."
				&& off.controlEnabled && off.canArm && !off.switchOff && !off.canAct,
			"a train missing from the snapshot was not reported as not entered");
		const FollowAvailability on = followAvailability(withFollow(in, true));
		require(on.entryText == off.entryText
				&& on.statusText == "Rail-1 has not entered the network yet. Follow starts when it enters."
				&& on.canArm && !on.switchOff && !on.canAct,
			"Follow on for a train missing from the snapshot was not worded for the armed control");
	}

	// Not entered: one second before the entry time, with the scheduled time on the clock.
	// The list entry says that the time is the schedule, and does not change with Follow.
	const GuiTrainState train = trainEnteringAt(1550);
	{
		const FollowAvailability off = followAvailability(runOf(train, 1549));
		require(off.phase == FollowPhase::NotEntered && off.entryText == "Rail-1 (scheduled 08:25:50)"
				&& off.statusText == "Rail-1 is scheduled to enter at 08:25:50. Follow can be switched on now and starts when it enters."
				&& off.controlEnabled && off.canArm && !off.switchOff && !off.canAct,
			"a train before its entry time was not reported as not entered with Follow off");
		const FollowAvailability on = followAvailability(withFollow(runOf(train, 1549), true));
		require(on.phase == FollowPhase::NotEntered && on.entryText == off.entryText
				&& on.statusText == "Rail-1 is scheduled to enter at 08:25:50. Follow starts when it enters."
				&& on.canArm && !on.switchOff && !on.canAct,
			"a train before its entry time was not reported as not entered with Follow on");
		// The state and the scheduled time end early in the sentence, so that a long name and a narrow
		// status area still show them, and the part about Follow comes after them.
		const std::string time = "08:25:50";
		require(off.statusText.find(time) + time.size() <= 45 && on.statusText.find(time) + time.size() <= 45
				&& off.statusText.find("Follow") > off.statusText.find(time) && on.statusText.find("Follow") > on.statusText.find(time),
			"the scheduled time does not come early in the sentence, before the part about Follow");
	}

	// Running: the second the train enters. Only a Follow that is on moves the view.
	{
		const FollowAvailability off = followAvailability(runOf(train, 1550));
		require(off.phase == FollowPhase::Running && off.entryText == "Rail-1 (running)"
				&& off.statusText == "Rail-1 is running. Follow can be switched on now." && off.controlEnabled && off.canArm
				&& !off.switchOff && !off.canAct,
			"a train at its entry time was not reported as running with Follow off");
		const FollowAvailability on = followAvailability(withFollow(runOf(train, 1550), true));
		require(on.phase == FollowPhase::Running && on.entryText == off.entryText
				&& on.statusText == "Rail-1 is running. The view follows it." && on.canArm && !on.switchOff && on.canAct,
			"a train at its entry time was not reported as running with Follow on");
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
				&& live.statusText == "Rail-1 has left the network. Follow cannot be switched on for it."
				&& live.controlEnabled && !live.canArm && !live.switchOff && !live.canAct,
			"a finished train in a live run was not refused");
		const FollowAvailability liveOn = followAvailability(withFollow(runOf(finished, 3000), true));
		require(liveOn.phase == FollowPhase::Finished && liveOn.entryText == live.entryText
				&& liveOn.statusText == "Rail-1 has left the network. Follow is switched off."
				&& !liveOn.canArm && liveOn.switchOff && !liveOn.canAct,
			"Follow that was on for a train that left a live run was not switched off");
		const FollowAvailability back = followAvailability(inReplay(runOf(finished, 3000)));
		require(back.phase == FollowPhase::Finished && back.entryText == "Rail-1 (finished)"
				&& back.statusText == "Rail-1 has left the network at this time. "
									  "Follow can be switched on now and continues when you go back in the replay."
				&& back.controlEnabled && back.canArm && !back.switchOff && !back.canAct,
			"a finished train in a replay was not accepted");
		const FollowAvailability stay = followAvailability(withFollow(inReplay(runOf(finished, 3000)), true));
		require(stay.phase == FollowPhase::Finished && stay.entryText == back.entryText
				&& stay.statusText == "Rail-1 has left the network at this time. "
									  "Follow stays on and continues when you go back in the replay."
				&& stay.canArm && !stay.switchOff && !stay.canAct,
			"Follow did not stay on for a train that finished in a replay");
	}

	// The Trains layer and a missing geometry only matter for a train in the network.
	{
		FollowAvailabilityInput hidden = runOf(train, 2000);
		hidden.trainsLayerVisible = false;
		const FollowAvailability off = followAvailability(hidden);
		require(off.phase == FollowPhase::HiddenByLayer && off.entryText == "Rail-1 (hidden)"
				&& off.statusText == "Rail-1 is running, but the Trains layer is switched off. "
									 "Follow can be switched on now and moves the view when the layer is switched on."
				&& off.canArm && !off.switchOff && !off.canAct,
			"a train hidden by the layer switch was not reported as hidden with Follow off");
		hidden.followOn = true;
		const FollowAvailability on = followAvailability(hidden);
		require(on.phase == FollowPhase::HiddenByLayer && on.entryText == off.entryText
				&& on.statusText == "Rail-1 is running, but the Trains layer is switched off, so the view does not follow it."
				&& on.canArm && !on.switchOff && !on.canAct,
			"a train hidden by the layer switch was not reported as hidden with Follow on");
		FollowAvailabilityInput blank = withFollow(runOf(train, 2000), true);
		blank.drawable = false;
		const FollowAvailability b = followAvailability(blank);
		require(b.phase == FollowPhase::NotDrawable && b.entryText == "Rail-1 (no position)"
				&& b.statusText == "Rail-1 is running, but it has no position on the map to follow."
				&& b.canArm && !b.switchOff && !b.canAct,
			"a train without a drawn position was not reported as such");
		blank.followOn = false;
		require(followAvailability(blank).statusText == b.statusText,
			"the sentence for a train without a drawn position depended on Follow");
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

	// The list entry of a phase does not depend on Follow or on the replay, and no
	// sentence that mentions Follow reads the same for both states of the control.
	{
		const GuiTrainState* const trains[] = {&train, &train, &train, &finished};
		const int times[] = {1000, 2000, 2000, 3000};
		for (int i = 0; i < 4; ++i) {
			FollowAvailabilityInput in = runOf(*trains[i], times[i]);
			in.trainsLayerVisible = i != 2;
			const FollowAvailability off = followAvailability(in);
			in.followOn = true;
			in.replay = true;
			const FollowAvailability on = followAvailability(in);
			require(off.entryText == on.entryText, "the list entry changed with the state of Follow");
			require(off.statusText != on.statusText, "a sentence about Follow did not change with the state of Follow");
			require(on.statusText.find("switched on now") == std::string::npos, "a sentence for Follow on offers to switch it on");
			require(off.statusText.find("Follow stays on") == std::string::npos
					&& off.statusText.find("Follow starts") == std::string::npos
					&& off.statusText.find("The view follows") == std::string::npos,
				"a sentence for Follow off says that it is on");
		}
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
		require(followAvailability(in).entryText == "Rail-1 (scheduled 00:00:00)",
			"the entry time was not formatted like other clock times");
	}
	return 0;
}
