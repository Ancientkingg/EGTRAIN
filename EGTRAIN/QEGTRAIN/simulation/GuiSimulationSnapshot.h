#ifndef GUISIMULATIONSNAPSHOT_H
#define GUISIMULATIONSNAPSHOT_H

#include "scene/SignallingLevel.h"

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/// An arc of the block section that a train occupies.
struct GuiOccupiedArc {
	int trackId = -1;	 ///< The track line of the section, or -1.
	double startX = 0.0; ///< The X coordinate of the start node of the arc, in kilometres.
};

/// One train at one step, as the window, the replay and the live stream show it. Steps and times are seconds from the
/// start of the run.
struct GuiTrainState {
	int index = -1;	  ///< Position of the train in the trains of the run, from 0.
	double id = 0.0;  ///< The ID number of the train in the simulation.
	std::string type; ///< The type of the train. A run built from a scene uses the service ID.
	/// The name of the train in the simulation, or "None" when unset. A run built from a scene uses the service ID and
	/// the occurrence, as "S1-2".
	std::string description;
	std::string operatingCode; ///< The operating code of the occurrence, or empty when it has none.
	std::string serviceId;	   ///< The ID of the service in the scene.
	/// Occurrence of the service that this train runs, counted from 1.
	int occurrence = 1;
	int routeIndex = -1;			///< Index of the route of the run that the train drives, or -1.
	bool reversedDirection = false; ///< The train drives its route against the direction in which it is defined.
	int wagonCount = 0;				///< Wagons behind the traction unit, which is not counted.
	double length = 0.0;			///< Length of the train, in metres.
	int departureTime = 0;			///< Step at which the train may enter, in seconds.
	bool outOfSimulation = false;	///< The train has passed the end of its route and left the simulation.
	/// Position of the head along the route, in metres. -9999 at the step at which the train leaves the route; later
	/// steps are not written and hold 0, so test outOfSimulation instead. -9999 when there is no route or the step lies
	/// outside the trajectory.
	double routeAxisPosition = -9999.0;
	double speedKmh = 0.0;			  ///< Speed, in km/h.
	int currentOnboardPassengers = 0; ///< Passengers on board.
	int maxOnboardPassengers = 0;	  ///< Passengers that the train can carry.
	/// The X coordinate of the front of each vehicle, traction unit first, in kilometres. -1 for a front that is on no
	/// section of the route. Empty when there is no route or the step lies outside the trajectory. At the step at which
	/// the train leaves, every entry is -1.
	std::vector<double> wagonHeadPositions;
	/// The X coordinate of the rear of each vehicle, in the order and units of wagonHeadPositions.
	std::vector<double> wagonTailPositions;
	/// The arcs of the block section that the train occupies. Empty once the train has left the simulation.
	std::vector<GuiOccupiedArc> occupiedArcs;
};

/// The name of a train in lists and messages: its operating code, or its description when it has none.
inline const std::string& guiTrainDisplayIdentifier(const GuiTrainState& train) {
	return train.operatingCode.empty() ? train.description : train.operatingCode;
}

/// The name that the train list and the Follow status give a train: its description, or its place in the list
/// (counted from 1) when it has none or it is "None". `trainIndex` counts from 0.
inline std::string followTrainName(const std::string& description, int trainIndex) {
	return description.empty() || description == "None" ? "Train " + std::to_string(trainIndex + 1) : description;
}

/// A train in the simulation reports the arcs that it occupies; one that has left does not.
inline bool guiTrainPublishesOccupiedArcs(const GuiTrainState& train) {
	return !train.outOfSimulation;
}

/// The train has a place to draw at `timestep` (seconds from the start of the run): it is in the simulation, due, and
/// has a head position and vehicle positions.
inline bool guiReplayTrainHasPosition(const GuiTrainState& train, int timestep) {
	return !train.outOfSimulation && timestep >= train.departureTime
		&& train.routeAxisPosition != -9999.0
		&& !train.wagonHeadPositions.empty() && !train.wagonTailPositions.empty();
}

/// Value of GuiSignalState::level for a section without a signalling level.
constexpr int kGuiSignalNoLevel = -1;

/// One entry per section ID and direction (see GuiSignalStateList).
struct GuiSignalState {
	std::string sectionId; ///< The ID of the block section.
	/// The aspect code of the section: 270 clear (only for a section with a level; see level), 180 approach,
	/// 75 caution, 751 red-red, 0 occupied or failed (see guiSignalRestriction()).
	int code = 0;
	bool reversedDirection = false; ///< The entry is for the direction against the definition of the route.
	/// Signalling level of the section (0 to 5), or kGuiSignalNoLevel. A section
	/// without a level keeps its initial code 270, which is not a clear signal.
	int level = kGuiSignalNoLevel;
	/// A signal_failure incident covers the section in this step.
	bool failed = false;
};

/// `level` is a signalling level from 0 to 5.
inline bool guiSignalHasLevel(int level) {
	return isValidSignallingLevel(level);
}

/// Display code of a head that has no signalling data to show.
constexpr int kGuiSignalUnavailable = -1;

/// The code a head shows for this state. A section without a signalling level
/// keeps its initial code 270, which is not a clear signal, so it shows as
/// unavailable.
inline int guiSignalDisplayCode(const GuiSignalState& state) {
	return guiSignalHasLevel(state.level) ? state.code : kGuiSignalUnavailable;
}

/// Rank of a section code, lowest is most restrictive. 0 is an occupied or failed
/// section, 751 the section behind it in BACC (red_red, speed limit V_751), 75
/// caution, 180 approach and 270 clear. A code that is none of these ranks last.
inline int guiSignalRestriction(int code) {
	switch (code) {
		case 0: return 0;
		case 751: return 1;
		case 75: return 2;
		case 180: return 3;
		case 270: return 4;
		default: return 5;
	}
}

/// Collects the signal states of the route copies of every section. Copies of
/// the same section ID and direction merge into one entry with the most
/// restrictive code, whatever the order they arrive in. A copy without a level
/// carries no signalling and never replaces one that has a level. Entries keep the
/// order in which their section and direction first appeared.
class GuiSignalStateList {
public:
	/// Adds the route copy of the section `id` in its direction, with its code and level. Any level outside 0 to 5
	/// counts as no level. Of two copies with a level the entry keeps the lowest level.
	void merge(std::string_view id, bool reversed, int code, int level) {
		if (!guiSignalHasLevel(level))
			level = kGuiSignalNoLevel;
		const std::string key(id);
		auto found = index_.find(key);
		if (found == index_.end())
			found = index_.emplace(key, std::array<int, 2>{-1, -1}).first;
		int& position = found->second[reversed ? 1 : 0];
		if (position < 0) {
			position = static_cast<int>(states_.size());
			states_.push_back({key, code, reversed, level, false});
			return;
		}
		GuiSignalState& into = states_[static_cast<std::size_t>(position)];
		if (!guiSignalHasLevel(level))
			return;
		if (!guiSignalHasLevel(into.level)) {
			into.code = code;
			into.level = level;
			return;
		}
		if (guiSignalRestriction(code) < guiSignalRestriction(into.code))
			into.code = code;
		into.level = std::min(into.level, level);
	}

	/// Marks the section as failed in both directions where it has an entry.
	void fail(std::string_view id) {
		const auto found = index_.find(std::string(id));
		if (found == index_.end())
			return;
		for (const int position : found->second)
			if (position >= 0)
				states_[static_cast<std::size_t>(position)].failed = true;
	}

	/// The entries in the order of their first copy. The list is empty afterwards.
	std::vector<GuiSignalState> take() {
		index_.clear();
		return std::move(states_);
	}

private:
	std::vector<GuiSignalState> states_;
	// Position in states_ per direction (forward, reversed), or -1.
	std::unordered_map<std::string, std::array<int, 2>> index_;
};

/// Every nonzero copied route-section code reports the permissive-signalling
/// cue. The producer value can describe an approach aspect and is not a
/// reservation or a movement guarantee for the selected direction.
inline bool guiSectionReportsPermissiveSignalling(double signalCode) {
	return signalCode != 0.0;
}

/// One block section at one step, merged over its copies in the routes.
struct GuiSectionState {
	std::string sectionId; ///< The ID of the block section.
	/// Legacy field name: true when any copied route section reports
	/// permissive signalling, not a reservation or direction guarantee.
	bool prepared = false;
	bool blocked = false; ///< A signal_failure incident covers the section in this step.
};

/// One station platform at one step.
struct GuiPlatformState {
	std::string stationId;	///< The ID of the station.
	std::string platformId; ///< The ID of the platform within the station.
	int maxVolume = 0;		///< Passengers that the platform holds at most; -1 when the run does not set it.
	/// The passengers waiting on the platform, then those who left the network there in the last 5 seconds.
	std::vector<std::string> passengerIds;
};

/// One passenger at one step.
struct GuiPassengerState {
	std::string id;				 ///< The ID of the passenger.
	std::string status;			 ///< "None", "OnPlatform" or "OnBoard".
	std::string waitingPlatform; ///< The platform on which the passenger waits, as the run records it.
	std::string nextTrain;		 ///< The train that the passenger waits for, as the run records it.
	/// The arrival station of the current trip of the passenger, or empty when the passenger has none.
	std::string nextDestination;
};

/// A message of virtual coupling, which a snapshot shows for 40 seconds after its step.
struct GuiVirtualCouplingState {
	int timestep = 0;			  ///< The step of the message, in seconds from the start of the run.
	std::string trainDescription; ///< The train that the message concerns.
	std::string message;		  ///< The text of the message.
};

/// The state of a run at one step: what the canvas, the panels, the replay and the live stream show.
struct GuiSimulationSnapshot {
	int timestep = 0;											  ///< The step, in seconds from the start of the run.
	int totalTimesteps = 0;										  ///< The number of steps of the run, which is its duration in seconds.
	std::vector<GuiTrainState> trains;							  ///< Every train of the run, in the order of the run.
	std::vector<GuiSignalState> signalStates;					  ///< One entry per section ID and direction.
	std::vector<GuiSectionState> sectionStates;					  ///< One entry per section ID, ordered by ID.
	std::vector<GuiPlatformState> platforms;					  ///< Every platform of the run.
	std::vector<GuiPassengerState> passengers;					  ///< Every passenger of the run.
	std::vector<GuiVirtualCouplingState> virtualCouplingMessages; ///< The messages of the last 40 seconds.
};

/// Hands the latest snapshot from the thread that steps the run to the window. It holds one snapshot: a newer one
/// replaces an older one that was not taken. Both functions may be called from any thread.
class GuiSimulationSnapshotMailbox {
public:
	/// Puts `snapshot` in the mailbox. Returns true when the reader has to be told, false when it was told of an earlier
	/// snapshot that it has not taken yet.
	bool publish(std::shared_ptr<const GuiSimulationSnapshot> snapshot) {
		std::lock_guard<std::mutex> lock(mutex_);
		snapshot_ = std::move(snapshot);
		if (notificationPending_)
			return false;
		notificationPending_ = true;
		return true;
	}

	/// Takes the snapshot out of the mailbox. Null when none was published since the last take.
	std::shared_ptr<const GuiSimulationSnapshot> take() {
		std::lock_guard<std::mutex> lock(mutex_);
		auto snapshot = std::move(snapshot_);
		notificationPending_ = false;
		return snapshot;
	}

private:
	std::mutex mutex_;
	std::shared_ptr<const GuiSimulationSnapshot> snapshot_;
	bool notificationPending_ = false;
};

#endif
