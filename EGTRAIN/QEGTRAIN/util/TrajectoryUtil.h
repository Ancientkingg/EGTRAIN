#ifndef TRAJECTORYUTIL_H
#define TRAJECTORYUTIL_H

#include <vector>

struct TrajectorySegment {
	int first;
	int last;
};

// Simulation time in seconds for a trajectory sample index.
double trajectoryTimeSeconds(int index, double timestep);

bool isValidTrajectorySample(int index, int activeFirst, int activeLast,
							 int sampleCount, double positionMeters);

std::vector<TrajectorySegment> validTrajectorySegments(
		const std::vector<double>& positionsMeters, int activeFirst, int activeLast);

#endif // TRAJECTORYUTIL_H
