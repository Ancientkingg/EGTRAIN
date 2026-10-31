#ifndef RUNOUTCOME_H
#define RUNOUTCOME_H

// Whether a run that has ended keeps its results, and why not. The decision uses only plain values that the run
// controller reads from the simulation and the worker, so it has no Qt dependency.

/// How a run ended, as the run controller reads it once the worker has finished.
struct RunEnd {
	bool failed = false;				///< An exception ended the run.
	bool stopRequested = false;			///< The worker was asked to stop.
	bool lastStageCompleted = false;	///< The run went through its last stage.
	bool sceneChangedDuringRun = false; ///< The scene was edited while the run went on.
	bool hasTrajectories = false;		///< At least one train of the run has a trajectory.
};

/// Why a run keeps no results.
enum class RunDiscard {
	None,		  ///< The results are kept.
	SceneChanged, ///< The scene was edited during the run.
	Failed,		  ///< An exception ended the run.
	Stopped,	  ///< The user stopped the run before its last stage.
	NoResults,	  ///< The run ended without a trajectory.
};

/// What becomes of the results of a run that has ended.
struct RunOutcome {
	RunDiscard discard = RunDiscard::None; ///< Why the results are discarded, or None when they are kept.
	/// A stop ended the run before its last stage, and no exception did. A scene change can be the reason of the stop.
	bool stopped = false;

	/// The results of the run are kept.
	bool resultsKept() const { return discard == RunDiscard::None; }
};

/// The outcome of a run that ended as `end` says. A stop that arrives after the last stage leaves a completed run. The
/// reasons to discard the results come in this order: the scene changed, the run failed, the run was stopped, the run
/// has no trajectory.
inline RunOutcome classifyRunOutcome(const RunEnd& end) {
	RunOutcome outcome;
	outcome.stopped = !end.failed && end.stopRequested && !end.lastStageCompleted;
	if (end.sceneChangedDuringRun)
		outcome.discard = RunDiscard::SceneChanged;
	else if (end.failed)
		outcome.discard = RunDiscard::Failed;
	else if (outcome.stopped)
		outcome.discard = RunDiscard::Stopped;
	else if (!end.hasTrajectories)
		outcome.discard = RunDiscard::NoResults;
	return outcome;
}

#endif // RUNOUTCOME_H
