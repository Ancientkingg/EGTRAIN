#include "util/TrajectoryUtil.h"

#include <algorithm>
#include <cmath>

double trajectoryTimeSeconds(int index, double timestep) {
	return index * timestep;
}

bool isValidTrajectorySample(int index, int activeFirst, int activeLast,
							 int sampleCount, double positionMeters) {
	return index >= 0 && index < sampleCount && index >= activeFirst && index <= activeLast &&
		   std::isfinite(positionMeters) && positionMeters != -9999;
}

std::vector<double> trajectoryExportCells(
		const std::vector<double>& positionsMeters, int activeFirst, int activeLast) {
	std::vector<double> cells(positionsMeters.size(), -9999);
	for (const auto& segment : validTrajectorySegments(positionsMeters, activeFirst, activeLast)) {
		std::copy(positionsMeters.begin() + segment.first,
				  positionsMeters.begin() + segment.last + 1,
				  cells.begin() + segment.first);
	}
	return cells;
}

int recordEarliestTrajectoryIndex(int currentIndex, int candidateIndex, bool canEnter) {
	return currentIndex < 0 && canEnter ? candidateIndex : currentIndex;
}

int replicatedEarliestTrajectoryIndex(int sourceIndex, int offset) {
	return sourceIndex < 0 ? -1 : sourceIndex + offset;
}

std::vector<TrajectorySegment> validTrajectorySegments(
		const std::vector<double>& positionsMeters, int activeFirst, int activeLast) {
	std::vector<TrajectorySegment> segments;
	// -1 is Train's "not active" marker; other out-of-range bounds still clamp.
	if (positionsMeters.empty() || activeFirst == -1 || activeFirst > activeLast)
		return segments;

	const int first = std::max(0, activeFirst);
	const int last = std::min(activeLast, static_cast<int>(positionsMeters.size()) - 1);
	if (first > last)
		return segments;

	int segmentFirst = -1;
	for (int index = first; index <= last; ++index) {
		if (isValidTrajectorySample(index, first, last,
								static_cast<int>(positionsMeters.size()), positionsMeters[index])) {
			if (segmentFirst < 0)
				segmentFirst = index;
		} else if (segmentFirst >= 0) {
			segments.push_back({segmentFirst, index - 1});
			segmentFirst = -1;
		}
	}
	if (segmentFirst >= 0)
		segments.push_back({segmentFirst, last});

	return segments;
}
