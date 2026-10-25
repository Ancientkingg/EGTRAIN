#ifndef FOLLOWAVAILABILITY_H
#define FOLLOWAVAILABILITY_H

#include "simulation/GuiSimulationSnapshot.h"
#include "util/TimeFormat.h"

#include <string>

// Whether the selected train can be followed, and the words that say why not.
// The decision uses only what the window knows, so it has no Qt dependency.
//
// The window asks this unit whether the control is offered (controlEnabled), whether Follow
// can be switched on for the train (canArm), whether it has to be switched off (switchOff),
// whether the view and the station emphasis follow the train (canAct), whether the view
// moves with the glide of the train or cuts to it (glide) and which text is shown (entryText,
// statusClause, statusText). The status bar shows the one clause; the tooltips hold the whole
// explanation, which is true whether Follow is on or off.

/// Where the selected train stands, as far as Follow is concerned.
enum class FollowPhase {
	NoServices,	   ///< the case defines no services
	NoRun,		   ///< the case has services but has not been run yet
	NotEntered,	   ///< the train has not entered the network at the displayed time
	Running,	   ///< the train is in the network and can be drawn
	Finished,	   ///< the train has left the network
	HiddenByLayer, ///< the train is in the network but the Trains layer is off
	NotDrawable	   ///< the train is in the network but has no position on the map
};

/// What the window knows about the selected train and the displayed frame.
struct FollowAvailabilityInput {
	/// The case defines at least one service.
	bool sceneHasServices = false;
	/// A run has been prepared, so its trains are known.
	bool hasRun = false;
	/// The displayed frame comes from a completed run, so the user can go back in time.
	bool replay = false;
	/// Follow is switched on for this train.
	bool followOn = false;
	/// Last time of the completed run that is displayed, in seconds since simulation start, or -1 when there is none.
	int replayEndTime = -1;
	/// The chosen train in the displayed snapshot, or null when it is not there.
	const GuiTrainState* train = nullptr;
	/// Time of the displayed snapshot, in seconds since simulation start.
	int timestep = 0;
	/// Simulation start, seconds since midnight (the offset formatSimTime takes).
	long long clockOffsetSeconds = 0;
	/// The Trains layer of the canvas is switched on.
	bool trainsLayerVisible = true;
	/// The train has a geometry that is drawn on the map.
	bool drawable = false;
	/// Display name of the train, as in the list.
	std::string name;
};

/// What the window offers and shows for Follow of the selected train.
struct FollowAvailability {
	FollowPhase phase = FollowPhase::NoServices; ///< Where the train stands.
	/// Text of the list entry. It does not depend on the state of Follow.
	std::string entryText;
	/// One clause for the status bar: the state of the train, or that the view follows it.
	std::string statusClause;
	/// The whole explanation, worded for the state of Follow, for the tooltips.
	std::string statusText;
	/// The Follow control is offered. It is not when there is nothing to follow yet.
	bool controlEnabled = false;
	/// Follow can be switched on for this train in this state. A train that has
	/// finished is refused in a live run and accepted in a replay, which can go back.
	bool canArm = false;
	/// Follow is on and cannot stay on for this train.
	bool switchOff = false;
	/// Follow is on and the view moves to the train now.
	bool canAct = false;
	/// The view moves with the train as it glides between the steps of a live run. A replay holds a
	/// frame every few seconds and its trains do not glide, so the view cuts to the train.
	bool glide = false;
};

/// Where the train of `in` stands. A train that is in the network but has no position at the displayed time counts as
/// not entered.
inline FollowPhase followPhase(const FollowAvailabilityInput& in) {
	if (!in.sceneHasServices)
		return FollowPhase::NoServices;
	if (!in.hasRun)
		return FollowPhase::NoRun;
	if (!in.train)
		return FollowPhase::NotEntered;
	if (in.train->outOfSimulation)
		return FollowPhase::Finished;
	if (!guiReplayTrainHasPosition(*in.train, in.timestep))
		return FollowPhase::NotEntered;
	if (!in.trainsLayerVisible)
		return FollowPhase::HiddenByLayer;
	return in.drawable ? FollowPhase::Running : FollowPhase::NotDrawable;
}

/// What the window offers and shows for Follow of the train of `in`.
inline FollowAvailability followAvailability(const FollowAvailabilityInput& in) {
	FollowAvailability out;
	out.phase = followPhase(in);
	const std::string& name = in.name;
	switch (out.phase) {
		case FollowPhase::NoServices:
			out.entryText = "No trains to follow";
			out.statusClause = "No trains to follow";
			out.statusText = "This case has no trains to follow.";
			break;
		case FollowPhase::NoRun:
			out.entryText = "Run the case first";
			out.statusClause = "Run the case first";
			out.statusText = "Run the case first, then you can follow a train.";
			break;
		case FollowPhase::NotEntered: {
			out.canArm = true;
			// A train that is scheduled after the end of a completed run has no position at any time of its replay, so the
			// sentence does not say that Follow starts.
			const bool afterRun = in.train && in.replayEndTime >= 0 && in.train->departureTime > in.replayEndTime;
			const std::string tail = in.followOn ? " Follow starts when it enters." : "";
			if (in.train) {
				const std::string entry = formatSimTime(in.train->departureTime, in.clockOffsetSeconds);
				out.entryText = name + " (scheduled " + entry + ")";
				out.statusClause = name + (afterRun ? " enters after this run" : " enters at " + entry);
				out.statusText = name + " is scheduled to enter at " + entry
					+ (afterRun ? std::string(", after the end of this run, so the view cannot follow it.") : "." + tail);
			} else {
				out.entryText = name + " (not entered yet)";
				out.statusClause = name + " has not entered yet";
				out.statusText = name + " has not entered the network yet." + tail;
			}
			break;
		}
		case FollowPhase::Running:
			out.canArm = true;
			out.entryText = name + " (running)";
			out.statusClause = in.followOn ? "Following " + name : name + " is running";
			out.statusText = name + " is running." + (in.followOn ? " The view follows it." : "");
			break;
		case FollowPhase::Finished:
			out.entryText = name + " (finished)";
			out.statusClause = name + " has left the network";
			if (in.replay) {
				out.canArm = true;
				out.statusText = name + " has left the network at this time."
					+ (in.followOn ? " Follow stays on and continues when you go back in the replay." : "");
			} else {
				out.statusText = name + " has left the network. " + (in.followOn ? "Follow is switched off." : "Follow cannot be switched on for it.");
			}
			break;
		case FollowPhase::HiddenByLayer:
			out.canArm = true;
			out.entryText = name + " (hidden)";
			out.statusClause = name + " is hidden (Trains layer off)";
			out.statusText = name + " is running, but the Trains layer is switched off"
				+ (in.followOn ? ", so the view does not follow it."
							   : ". Turn on the Trains layer to follow it.");
			break;
		case FollowPhase::NotDrawable:
			out.canArm = true;
			out.entryText = name + " (no position)";
			out.statusClause = name + " has no position on the map";
			out.statusText = name + " is running, but it has no position on the map to follow.";
			break;
	}
	out.controlEnabled = out.phase != FollowPhase::NoServices && out.phase != FollowPhase::NoRun;
	out.switchOff = in.followOn && !out.canArm;
	out.canAct = in.followOn && out.phase == FollowPhase::Running;
	out.glide = out.canAct && !in.replay;
	return out;
}

#endif
