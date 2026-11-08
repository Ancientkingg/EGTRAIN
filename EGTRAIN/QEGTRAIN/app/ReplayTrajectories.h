#ifndef REPLAYTRAJECTORIES_H
#define REPLAYTRAJECTORIES_H

#include "app/GuiReplayHistory.h"
#include "diagrams/CompletedRunData.h"

#include <memory>
#include <vector>

/// The position and speed samples of every train of `run`, shared with the completed run: the vectors stay alive as long
/// as the history holds them and cannot change, because the run is const. One entry per train, in the order of the run.
/// Empty for a null run, in which case makeGuiReplayTrains() copies the samples it needs.
inline std::vector<GuiReplayTrajectory> replayTrajectoriesOf(const std::shared_ptr<const CompletedRunData>& run) {
	std::vector<GuiReplayTrajectory> result;
	if (!run)
		return result;
	result.reserve(run->trains.size());
	for (const CompletedTrain& train : run->trains) {
		GuiReplayTrajectory entry;
		entry.firstSample = train.trajectory.firstSample;
		entry.position = std::shared_ptr<const std::vector<double>>(run, &train.trajectory.position);
		entry.speed = std::shared_ptr<const std::vector<double>>(run, &train.trajectory.speed);
		result.push_back(std::move(entry));
	}
	return result;
}

#endif
