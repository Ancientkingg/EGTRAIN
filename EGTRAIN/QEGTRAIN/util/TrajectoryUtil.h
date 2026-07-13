#ifndef TRAJECTORYUTIL_H
#define TRAJECTORYUTIL_H

#include <vector>

struct TrajectorySegment {
	int first;
	int last;
};

class TrajectoryEndTimeOverride {
public:
	TrajectoryEndTimeOverride(int& endTime, int temporaryEndTime)
		: endTime_(endTime), originalEndTime_(endTime) {
		endTime_ = temporaryEndTime;
	}

	~TrajectoryEndTimeOverride() {
		endTime_ = originalEndTime_;
	}

	TrajectoryEndTimeOverride(const TrajectoryEndTimeOverride&) = delete;
	TrajectoryEndTimeOverride& operator=(const TrajectoryEndTimeOverride&) = delete;

private:
	int& endTime_;
	int originalEndTime_;
};

// Simulation time in seconds for a trajectory sample index.
double trajectoryTimeSeconds(int index, double timestep);

bool isValidTrajectorySample(int index, int activeFirst, int activeLast,
							 int sampleCount, double positionMeters);

std::vector<double> trajectoryExportCells(const std::vector<double>& positionsMeters,
										 int activeFirst, int activeLast);

int recordEarliestTrajectoryIndex(int currentIndex, int candidateIndex, bool canEnter);

int replicatedEarliestTrajectoryIndex(int sourceIndex, int offset);

std::vector<TrajectorySegment> validTrajectorySegments(
		const std::vector<double>& positionsMeters, int activeFirst, int activeLast);

#endif // TRAJECTORYUTIL_H
