#include "simulation/DispatchMeasures.h"

#include "simulation/PassengerJourneys.h"
#include "simulation/SimulationRun.h"
#include "util/Log.h"

#include <algorithm>

namespace {
// The first stop from `from` on where the train can end its run: a stop at the end of an arc of its route where it halts.
// -1 when there is none.
int endableStopFrom(const Train& train, int from) {
	const int count = std::min(train.numStations, static_cast<int>(train.Stations.size()));
	for (int stop = std::max(0, from); stop < count; ++stop)
		if (train.stationBlockSection[stop] >= 0 && train.stationArc[stop] >= 0 && haltsAtStop(train, stop))
			return stop;
	return -1;
}

MeasureEffect cancelTrain(Train& train, const std::string& fromStation) {
	MeasureEffect effect;
	effect.trainDescription = train.trainDescription;
	if (train.OutOfSimulation) {
		effect.outcome = MeasureOutcome::AlreadyLeft;
		return effect;
	}
	const bool entered = train.CanEnter;
	// The first stop that the train has not left; 0 before it enters.
	const int progress = entered ? advanceFirstStop(train, 0) : 0;
	const int count = std::min(train.numStations, static_cast<int>(train.Stations.size()));
	int requested = -1;
	if (!fromStation.empty())
		for (int stop = progress; stop < count && requested < 0; ++stop)
			if (train.Stations[static_cast<std::size_t>(stop)].stationName == fromStation)
				requested = stop;
	// A train that has not entered and is cancelled completely, or from its first stop, does not enter.
	if (!entered && (fromStation.empty() || requested == 0)) {
		train.OutOfSimulation = true;
		effect.outcome = MeasureOutcome::NotEntered;
		return effect;
	}
	if (fromStation.empty())
		effect.outcome = MeasureOutcome::EndsAtNextStop;
	else if (requested < 0)
		effect.outcome = MeasureOutcome::StationBehind;
	else
		effect.outcome = MeasureOutcome::EndsAtStop;
	int end = endableStopFrom(train, requested >= 0 ? requested : progress);
	if (end < 0) {
		if (!entered) {
			train.OutOfSimulation = true;
			effect.outcome = MeasureOutcome::NotEntered;
		} else {
			effect.outcome = MeasureOutcome::NoStopLeft;
		}
		return effect;
	}
	if (end != requested && effect.outcome == MeasureOutcome::EndsAtStop)
		effect.outcome = MeasureOutcome::CannotEndThere;
	if (train.runEndStop >= 0 && train.runEndStop <= end) {
		effect.outcome = MeasureOutcome::AlreadyEndsEarlier;
		end = train.runEndStop;
	}
	train.runEndStop = end;
	effect.endStation = train.Stations[static_cast<std::size_t>(end)].stationName;
	return effect;
}
} // namespace

const AppliedMeasure& applyMeasure(SimulationRun& run, const DispatchMeasure& measure, int t) {
	AppliedMeasure applied;
	applied.measure = measure;
	applied.step = t;
	for (Train& train : run.trains) {
		if (train.serviceId != measure.serviceId || (measure.occurrence > 0 && train.serviceOccurrence != measure.occurrence))
			continue;
		switch (measure.type) {
			case MeasureType::CancelService:
				applied.effects.push_back(cancelTrain(train, measure.fromStation));
				break;
		}
		const MeasureEffect& effect = applied.effects.back();
		EG_LOG(SimEvents, Info) << "At time instant " << t << " measure " << measure.id << " cancels train " << effect.trainDescription
								<< ": it " << measureEffectText(effect) << "\n";
	}
	run.appliedMeasures.push_back(std::move(applied));
	return run.appliedMeasures.back();
}

void applyDueMeasures(SimulationRun& run, int t) {
	while (run.nextMeasure < run.measures.size() && run.measures[run.nextMeasure].timeSeconds <= t)
		applyMeasure(run, run.measures[run.nextMeasure++], t);
}

std::string measureEffectText(const MeasureEffect& effect) {
	switch (effect.outcome) {
		case MeasureOutcome::NotEntered:
			return "does not enter";
		case MeasureOutcome::EndsAtStop:
			return "ends at " + effect.endStation;
		case MeasureOutcome::EndsAtNextStop:
			return "ends at its next stop, " + effect.endStation;
		case MeasureOutcome::StationBehind:
			return "has passed the station and ends at its next stop, " + effect.endStation;
		case MeasureOutcome::CannotEndThere:
			return "cannot end its run at the station and ends at its next stop, " + effect.endStation;
		case MeasureOutcome::NoStopLeft:
			return "has no stop left to end at and runs to the end of its route";
		case MeasureOutcome::AlreadyLeft:
			return "had already left the network";
		case MeasureOutcome::AlreadyEndsEarlier:
			return "already ends at " + effect.endStation;
	}
	return {};
}
