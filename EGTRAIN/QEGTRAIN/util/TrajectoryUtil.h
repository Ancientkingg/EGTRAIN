#ifndef TRAJECTORYUTIL_H
#define TRAJECTORYUTIL_H

#include <vector>

/// \file
/// Helpers for the position series of a train. A series has one sample per step of the run, its
/// index is the step in seconds from the start of the run, and a position is the head position
/// in metres along the route. -9999 marks a sample without a position, such as after the train
/// has left.

/// A run of consecutive valid samples, both ends included.
struct TrajectorySegment {
	int first; ///< Index of the first sample.
	int last;  ///< Index of the last sample.
};

/// Sets an end step to a temporary value for its lifetime and restores the original value when it
/// is destroyed.
class TrajectoryEndTimeOverride {
public:
	/// Sets \p endTime to \p temporaryEndTime, both steps in seconds from the start of the run.
	/// \p endTime must outlive this object.
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

/// Simulation time in seconds for a sample index: \p index times \p timestep, the length of a step
/// in seconds.
double trajectoryTimeSeconds(int index, double timestep);

/// True when \p index lies in [0, \p sampleCount) and in [\p activeFirst, \p activeLast], and
/// \p positionMeters is finite and not -9999.
bool isValidTrajectorySample(int index, int activeFirst, int activeLast,
	int sampleCount, double positionMeters);

/// The positions in metres, as long as \p positionsMeters, with -9999 in every cell outside the
/// segments of validTrajectorySegments().
std::vector<double> trajectoryExportCells(const std::vector<double>& positionsMeters,
	int activeFirst, int activeLast);

/// The cells of trajectoryExportCells() moved in time: one cell per step from \p outputFirst to
/// \p outputLast, both included, in which the sample \p activeFirst falls on \p departureTime.
/// Steps are seconds from the start of the run. A cell without a valid sample is -9999. Returns an
/// empty vector when \p outputFirst is after \p outputLast, and only -9999 when \p activeFirst
/// is negative.
std::vector<double> shiftedTrajectoryExportCells(const std::vector<double>& values,
	int activeFirst, int activeLast,
	int departureTime, int outputFirst, int outputLast);

/// The first active sample index of a train: \p candidateIndex when none is recorded yet
/// (\p currentIndex is negative) and \p canEnter is true, otherwise \p currentIndex. -1 means
/// none.
int recordEarliestTrajectoryIndex(int currentIndex, int candidateIndex, bool canEnter);

/// \p sourceIndex moved by \p offset steps, or -1 when \p sourceIndex is negative.
int replicatedEarliestTrajectoryIndex(int sourceIndex, int offset);

/// The runs of valid samples (isValidTrajectorySample()) between \p activeFirst and \p activeLast,
/// limited to the vector. Empty when the vector is empty, \p activeFirst is -1 (the train was
/// never active) or \p activeFirst is after \p activeLast.
std::vector<TrajectorySegment> validTrajectorySegments(
	const std::vector<double>& positionsMeters, int activeFirst, int activeLast);

#endif // TRAJECTORYUTIL_H
