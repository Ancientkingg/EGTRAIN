#ifndef DISPATCHMEASURES_H
#define DISPATCHMEASURES_H

#include "scene/SceneModel.h"

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

#endif // DISPATCHMEASURES_H
