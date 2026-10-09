#ifndef GUISIMULATIONSNAPSHOT_H
#define GUISIMULATIONSNAPSHOT_H

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct GuiOccupiedArc {
	int trackId = -1;
	double startX = 0.0;
};

struct GuiTrainState {
	int index = -1;
	double id = 0.0;
	std::string type;
	std::string description;
	std::string operatingCode;
	std::string serviceId;
	int routeIndex = -1;
	bool reversedDirection = false;
	int wagonCount = 0;
	double length = 0.0;
	int departureTime = 0;
	bool outOfSimulation = false;
	double routeAxisPosition = -9999.0;
	double speedKmh = 0.0;
	int currentOnboardPassengers = 0;
	int maxOnboardPassengers = 0;
	std::vector<double> wagonHeadPositions;
	std::vector<double> wagonTailPositions;
	std::vector<GuiOccupiedArc> occupiedArcs;
};

inline const std::string& guiTrainDisplayIdentifier(const GuiTrainState& train) {
	return train.operatingCode.empty() ? train.description : train.operatingCode;
}

inline bool guiTrainPublishesOccupiedArcs(const GuiTrainState& train) {
	return !train.outOfSimulation;
}

inline bool guiReplayTrainHasPosition(const GuiTrainState& train, int timestep) {
	return !train.outOfSimulation && timestep >= train.departureTime
		&& train.routeAxisPosition != -9999.0
		&& !train.wagonHeadPositions.empty() && !train.wagonTailPositions.empty();
}

// Value of GuiSignalState::level for a section without a signalling level.
constexpr int kGuiSignalNoLevel = -1;

// One entry per section ID and direction (see GuiSignalStateList).
struct GuiSignalState {
	std::string sectionId;
	int code = 0;
	bool reversedDirection = false;
	// Signalling level of the section (0 to 5), or kGuiSignalNoLevel. A section
	// without a level keeps its initial code 270, which is not a clear signal.
	int level = kGuiSignalNoLevel;
	// A signal_failure incident covers the section in this step.
	bool failed = false;
};

inline bool guiSignalHasLevel(int level) {
	return level >= 0 && level <= 5;
}

// Rank of a section code, lowest is most restrictive. 0 is an occupied or failed
// section, 751 the section behind it in BACC (red_red, speed limit V_751), 75
// caution, 180 approach and 270 clear. A code that is none of these ranks last.
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

// Collects the signal states of the route copies of every section. Copies of
// the same section ID and direction merge into one entry with the most
// restrictive code, whatever the order they arrive in. A copy without a level
// carries no signalling and never replaces one that has a level. Entries keep the
// order in which their section and direction first appeared.
class GuiSignalStateList {
public:
	// Any level outside 0 to 5 counts as no level.
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

	// Marks the section as failed in both directions where it has an entry.
	void fail(std::string_view id) {
		const auto found = index_.find(std::string(id));
		if (found == index_.end())
			return;
		for (const int position : found->second)
			if (position >= 0)
				states_[static_cast<std::size_t>(position)].failed = true;
	}

	std::vector<GuiSignalState> take() {
		index_.clear();
		return std::move(states_);
	}

private:
	std::vector<GuiSignalState> states_;
	// Position in states_ per direction (forward, reversed), or -1.
	std::unordered_map<std::string, std::array<int, 2>> index_;
};

// Every nonzero copied route-section code reports the permissive-signalling
// cue. The producer value can describe an approach aspect and is not a
// reservation or a movement guarantee for the selected direction.
inline bool guiSectionReportsPermissiveSignalling(double signalCode) {
	return signalCode != 0.0;
}

struct GuiSectionState {
	std::string sectionId;
	// Legacy field name: true when any copied route section reports
	// permissive signalling, not a reservation or direction guarantee.
	bool prepared = false;
	bool blocked = false;
};

struct GuiPlatformState {
	std::string stationId;
	std::string platformId;
	int maxVolume = 0;
	std::vector<std::string> passengerIds;
};

struct GuiPassengerState {
	std::string id;
	std::string status;
	std::string waitingPlatform;
	std::string nextTrain;
	std::string nextDestination;
};

struct GuiVirtualCouplingState {
	int timestep = 0;
	std::string trainDescription;
	std::string message;
};

struct GuiSimulationSnapshot {
	int timestep = 0;
	int totalTimesteps = 0;
	std::vector<GuiTrainState> trains;
	std::vector<GuiSignalState> signalStates;
	std::vector<GuiSectionState> sectionStates;
	std::vector<GuiPlatformState> platforms;
	std::vector<GuiPassengerState> passengers;
	std::vector<GuiVirtualCouplingState> virtualCouplingMessages;
};

class GuiSimulationSnapshotMailbox {
public:
	bool publish(std::shared_ptr<const GuiSimulationSnapshot> snapshot) {
		std::lock_guard<std::mutex> lock(mutex_);
		snapshot_ = std::move(snapshot);
		if (notificationPending_)
			return false;
		notificationPending_ = true;
		return true;
	}

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
