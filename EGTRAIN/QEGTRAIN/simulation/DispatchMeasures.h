#ifndef DISPATCHMEASURES_H
#define DISPATCHMEASURES_H

#include "scene/SceneModel.h"

#include <mutex>
#include <string>
#include <vector>

struct SimulationRun;
class Train;

/// Where a dispatching measure comes from.
enum class MeasureSource {
	Scenario, ///< The measures of the scenario of the run.
	ByHand,	  ///< A dispatcher took it during the run.
};

/// A dispatching measure as the run applies it. The operations builder makes one of each measure of the scenario; a dispatcher, or
/// a dispatching strategy, makes one during the run and passes it to applyMeasure.
struct DispatchMeasure {
	std::string id;								   ///< The id of the measure.
	MeasureType type = MeasureType::CancelService; ///< What the measure does.
	std::string serviceId;						   ///< The scene service that it cancels.
	int occurrence = 0;							   ///< The 1-based occurrence of the service that it cancels; 0 for every occurrence.
	/// The station, by its name in the run (Node::stationName), from which the service is cancelled; empty to cancel it completely.
	std::string fromStation;
	double timeSeconds = 0.0;						///< When the measure takes effect, in seconds from the start of the run.
	MeasureSource source = MeasureSource::Scenario; ///< Where the measure comes from.
};

/// What a measure did to one train.
enum class MeasureOutcome {
	NotEntered,			///< The train had not entered the network and does not enter it.
	EndsAtStop,			///< The train ends its run at the station of the measure.
	EndsAtNextStop,		///< A complete cancellation of a train that runs: it ends its run at its next stop.
	StationBehind,		///< The station of the measure was behind the train, so it ends its run at its next stop.
	CannotEndThere,		///< The train calls at the station of the measure but cannot end its run there, so it ends at its next stop.
	NoStopLeft,			///< The train has no stop left where it could end its run, so it runs on to the end of its route.
	AlreadyLeft,		///< The train had already left the network.
	AlreadyEndsEarlier, ///< An earlier measure already ends the run of the train at or before this station.
};

/// The effect of a measure on one train.
struct MeasureEffect {
	std::string trainDescription;						  ///< The train (Train::trainDescription).
	MeasureOutcome outcome = MeasureOutcome::AlreadyLeft; ///< What the measure did to it.
	/// The station (Node::stationName) where the train ends its run; empty when it does not enter or nothing changed.
	std::string endStation;
};

/// A measure that the run applied.
struct AppliedMeasure {
	DispatchMeasure measure;			///< The measure.
	int step = 0;						///< The step in which it took effect.
	std::vector<MeasureEffect> effects; ///< What it did to each train of the service, in the order of the trains of the run.
};

/// Applies measure in step t of run, before the trains move, and records it in SimulationRun::appliedMeasures with its effect on
/// every train that it names. Every measure goes through here, whatever its source.
///
/// A cancellation ends the run of each train of the service (of the occurrence, when it names one) at a stop (Train::runEndStop):
/// the train runs there as timetabled, dwells and leaves the network, and it does not call at the later stops. A train that has
/// not entered the network does not enter when the service is cancelled completely or from its first stop. A complete
/// cancellation of a train that runs ends it at its next stop, the one where it stands included; so does a cancellation from a
/// station that is behind the train. A train that already ends its run earlier keeps that end. The record that it returns is valid
/// until the next measure is applied.
const AppliedMeasure& applyMeasure(SimulationRun& run, const DispatchMeasure& measure, int t);

/// Applies the measures of SimulationRun::measures that take effect at step t and have not been applied: those whose time is t
/// seconds or less, in the order of their times. The run calls it at the start of every step.
void applyDueMeasures(SimulationRun& run, int t);

/// The effect as the results show it, after the name of the train, such as "ends at its next stop, B".
std::string measureEffectText(const MeasureEffect& effect);

/// True when the effect changed the run of the train: it does not enter or ends at another stop than before. A train that had left,
/// already ended earlier or had no stop left to end at runs as it would have without the measure.
bool measureChangedTrain(const MeasureEffect& effect);

/// The line of the results about an applied measure: the clock time at which it took effect, with second 0 of the run at
/// startOffsetSeconds after midnight, its id, where it came from and its effect on each train, such as
/// "08:10:00 cancel-q1 (scenario): Q1-1 ends at B".
std::string appliedMeasureText(const AppliedMeasure& applied, long long startOffsetSeconds);

/// Measures that a dispatcher takes by hand while a run goes on, which the thread of the run applies at the top of its next step
/// (applyQueuedMeasures). The queue takes measures only while it is open: the run opens it before its first step and closes it
/// when it takes the measures of its last step, so a measure is either applied or refused. Every function may be called from any
/// thread.
class MeasureQueue {
public:
	/// Empties the queue and opens it.
	void open();
	/// Queues the measure for the top of the next step of the run. False, and nothing is queued, while the queue is closed.
	bool submit(DispatchMeasure measure);
	/// Takes the queued measures, in the order in which they were submitted, and leaves the queue empty. With close, the queue
	/// refuses the measures that follow.
	std::vector<DispatchMeasure> take(bool close = false);

private:
	std::mutex mutex_;						///< Guards measures_ and open_.
	std::vector<DispatchMeasure> measures_; ///< The measures that wait for the next step, in the order of their submits.
	bool open_ = false;						///< The queue takes measures.
};

/// Applies the measures of the queue in step t, in the order of their submits, as measures by hand that take effect at t
/// (MeasureSource::ByHand, timeSeconds t). The run calls it at the top of every step, before the step moves anything, with
/// lastStep at its last step, which closes the queue.
void applyQueuedMeasures(SimulationRun& run, MeasureQueue& queue, int t, bool lastStep = false);

/// A stop of a train: the station by its name in the run and its position along the route of the train.
struct RunStop {
	std::string station;	  ///< The name of the station (Node::stationName).
	double routeMetres = 0.0; ///< The position of the stop along the route, in metres.
};

/// The stops of the train in the order of its timetable. Read them before the run starts: a run fills in the position of the
/// first and the last stop when it starts, which this function does in the same way, from the station of that name.
std::vector<RunStop> runStops(const SimulationRun& run, const Train& train);

/// The stops that are not behind a head at headMetres along the route, by their names, in the order of the timetable; every stop
/// while the train has not entered (a negative head). They are the stations from which a dispatcher can still cancel the train.
std::vector<std::string> stopsAhead(const std::vector<RunStop>& stops, double headMetres);

/// The measures that the run applied by hand, as measures of a scenario of the scene, so that a run of that scenario repeats them:
/// each takes effect at the step at which it was applied, names the station by its id in the scene and gets an id that the scene
/// does not use yet. The measures of the scenario of the run are left out.
std::vector<SceneMeasure> byHandSceneMeasures(const SceneModel& model, const std::vector<AppliedMeasure>& applied);

#endif // DISPATCHMEASURES_H
