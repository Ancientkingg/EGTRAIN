#ifndef GUISIMULATIONSNAPSHOT_H
#define GUISIMULATIONSNAPSHOT_H

#include <memory>
#include <mutex>
#include <string>
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

struct GuiSignalState {
	std::string sectionId;
	int code = 0;
	bool reversedDirection = false;
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
