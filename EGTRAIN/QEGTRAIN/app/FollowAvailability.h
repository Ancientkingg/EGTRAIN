#ifndef FOLLOWAVAILABILITY_H
#define FOLLOWAVAILABILITY_H

#include "app/GuiSimulationSnapshot.h"
#include "util/TimeFormat.h"

#include <string>

// Whether the selected train can be followed, and the words that say why not.
// The decision uses only what the window knows, so it has no Qt dependency.

enum class FollowPhase {
	NoServices,	   // the case defines no services
	NoRun,		   // the case has services but has not been run yet
	NotEntered,	   // the train has not entered the network at the displayed time
	Running,	   // the train is in the network and can be drawn
	Finished,	   // the train has left the network
	HiddenByLayer, // the train is in the network but the Trains layer is off
	NotDrawable	   // the train is in the network but has no position on the map
};

struct FollowAvailabilityInput {
	bool sceneHasServices = false;
	// A run has been prepared, so its trains are known.
	bool hasRun = false;
	bool replay = false;
	// The chosen train in the displayed snapshot, or null when it is not there.
	const GuiTrainState* train = nullptr;
	// Time of the displayed snapshot, in seconds since simulation start.
	int timestep = 0;
	// Simulation start, seconds since midnight (the offset formatSimTime takes).
	long long clockOffsetSeconds = 0;
	bool trainsLayerVisible = true;
	// The train has a geometry that is drawn on the map.
	bool drawable = false;
	// Display name of the train, as in the list.
	std::string name;
};

struct FollowAvailability {
	FollowPhase phase = FollowPhase::NoServices;
	// Text of the list entry.
	std::string entryText;
	// Sentence for the status area.
	std::string statusText;
	// Follow can be switched on for this train in this state. A train that has
	// finished is refused in a live run and accepted in a replay, which can go back.
	bool canArm = false;
	// The view can move to the train now.
	bool canAct = false;
};

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

inline FollowAvailability followAvailability(const FollowAvailabilityInput& in) {
	FollowAvailability out;
	out.phase = followPhase(in);
	const std::string& name = in.name;
	switch (out.phase) {
		case FollowPhase::NoServices:
			out.entryText = "No trains to follow";
			out.statusText = "This case has no trains to follow.";
			break;
		case FollowPhase::NoRun:
			out.entryText = "Run the case to follow trains";
			out.statusText = "Run the case first, then you can follow a train.";
			break;
		case FollowPhase::NotEntered:
			out.canArm = true;
			if (in.train) {
				const std::string entry = formatSimTime(in.train->departureTime, in.clockOffsetSeconds);
				out.entryText = name + " (enters " + entry + ")";
				out.statusText = name + " has not entered the network yet. It is scheduled to enter at " + entry
					+ ". Follow starts when it enters.";
			} else {
				out.entryText = name + " (not entered yet)";
				out.statusText = name + " has not entered the network yet. Follow starts when it enters.";
			}
			break;
		case FollowPhase::Running:
			out.canArm = true;
			out.canAct = true;
			out.entryText = name + " (running)";
			out.statusText = name + " is running.";
			break;
		case FollowPhase::Finished:
			out.entryText = name + " (finished)";
			if (in.replay) {
				out.canArm = true;
				out.statusText = name + " has left the network at this time. Follow stays on and continues when you go back in the replay.";
			} else {
				out.statusText = name + " has left the network. Follow is switched off.";
			}
			break;
		case FollowPhase::HiddenByLayer:
			out.canArm = true;
			out.entryText = name + " (hidden)";
			out.statusText = name + " is running, but the Trains layer is switched off, so the view does not follow it.";
			break;
		case FollowPhase::NotDrawable:
			out.canArm = true;
			out.entryText = name + " (no position)";
			out.statusText = name + " is running, but it has no position on the map to follow.";
			break;
	}
	return out;
}

#endif
