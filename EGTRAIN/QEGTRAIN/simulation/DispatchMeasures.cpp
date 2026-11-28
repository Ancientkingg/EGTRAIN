#include "simulation/DispatchMeasures.h"

#include "scene/SceneIds.h"
#include "simulation/PassengerJourneys.h"
#include "simulation/SimulationRun.h"
#include "util/Log.h"
#include "util/TimeFormat.h"

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

bool measureChangedTrain(const MeasureEffect& effect) {
	switch (effect.outcome) {
		case MeasureOutcome::NotEntered:
		case MeasureOutcome::EndsAtStop:
		case MeasureOutcome::EndsAtNextStop:
		case MeasureOutcome::StationBehind:
		case MeasureOutcome::CannotEndThere:
			return true;
		case MeasureOutcome::NoStopLeft:
		case MeasureOutcome::AlreadyLeft:
		case MeasureOutcome::AlreadyEndsEarlier:
			return false;
	}
	return false;
}

void MeasureQueue::open() {
	const std::lock_guard<std::mutex> lock(mutex_);
	measures_.clear();
	open_ = true;
}

bool MeasureQueue::submit(DispatchMeasure measure) {
	const std::lock_guard<std::mutex> lock(mutex_);
	if (!open_)
		return false;
	measures_.push_back(std::move(measure));
	return true;
}

std::vector<DispatchMeasure> MeasureQueue::take(bool close) {
	const std::lock_guard<std::mutex> lock(mutex_);
	std::vector<DispatchMeasure> taken;
	taken.swap(measures_);
	if (close)
		open_ = false;
	return taken;
}

void applyQueuedMeasures(SimulationRun& run, MeasureQueue& queue, int t, bool lastStep) {
	for (DispatchMeasure& measure : queue.take(lastStep)) {
		measure.source = MeasureSource::ByHand;
		measure.timeSeconds = t;
		applyMeasure(run, measure, t);
	}
}

std::vector<RunStop> runStops(const SimulationRun& run, const Train& train) {
	std::vector<RunStop> stops;
	const int count = std::min(train.numStations, static_cast<int>(train.Stations.size()));
	for (int stop = 0; stop < count; ++stop) {
		const Node& node = train.Stations[static_cast<std::size_t>(stop)];
		RunStop result{node.stationName, node.X * 1000.0};
		// Train::checkTrainArrDep gives the first and the last stop the position of their station when the run starts.
		if (node.X == 0 && (stop == 0 || stop == count - 1))
			for (const Stations& station : run.infra.stations)
				if (station.stationName == node.stationName) {
					result.routeMetres = station.X * 1000.0;
					break;
				}
		stops.push_back(std::move(result));
	}
	return stops;
}

std::vector<std::string> stopsAhead(const std::vector<RunStop>& stops, double headMetres) {
	// A head that stands at a stop is a little short of it.
	constexpr double kStandingToleranceMetres = 1.0;
	std::vector<std::string> ahead;
	for (const RunStop& stop : stops)
		if (headMetres < 0.0 || stop.routeMetres + kStandingToleranceMetres >= headMetres)
			ahead.push_back(stop.station);
	return ahead;
}

std::vector<SceneMeasure> byHandSceneMeasures(const SceneModel& model, const std::vector<AppliedMeasure>& applied) {
	std::vector<SceneMeasure> measures;
	SceneModel withMeasures;
	withMeasures.scenarios = model.scenarios;
	withMeasures.scenarios.emplace_back();
	for (const AppliedMeasure& record : applied) {
		const DispatchMeasure& measure = record.measure;
		if (measure.source != MeasureSource::ByHand)
			continue;
		SceneMeasure scene;
		scene.id = sceneUniqueMeasureId(withMeasures, "by_hand");
		scene.type = measure.type;
		scene.serviceId = measure.serviceId;
		scene.hasOccurrence = measure.occurrence > 0;
		scene.occurrence = measure.occurrence > 0 ? measure.occurrence : 1;
		scene.timeSeconds = record.step;
		// The run names a station by its label (sceneStationLabel); the label of a station that the scene lacks is its id. Of the
		// stations with that label, one at which the service calls comes first, so that the saved measure validates.
		scene.fromStationId = measure.fromStation;
		if (!measure.fromStation.empty()) {
			const auto service = std::find_if(model.services.begin(), model.services.end(),
				[&measure](const SceneService& candidate) { return candidate.id == measure.serviceId; });
			const auto servedBy = [&service, &model](const std::string& stationId) {
				return service != model.services.end()
					&& std::any_of(service->stops.begin(), service->stops.end(),
						[&stationId](const SceneStop& stop) { return stop.stationId == stationId; });
			};
			bool found = false;
			for (const SceneStation& station : model.stations)
				if (sceneStationLabel(station) == measure.fromStation && (!found || servedBy(station.id))) {
					const bool served = servedBy(station.id);
					scene.fromStationId = station.id;
					found = true;
					if (served)
						break;
				}
		}
		withMeasures.scenarios.back().measures.push_back(scene);
		measures.push_back(std::move(scene));
	}
	return measures;
}

std::string appliedMeasureText(const AppliedMeasure& applied, long long startOffsetSeconds) {
	const DispatchMeasure& measure = applied.measure;
	std::string text = formatSimTime(applied.step, startOffsetSeconds) + " " + measure.id
		+ (measure.source == MeasureSource::ByHand ? " (by hand): " : " (scenario): ");
	if (applied.effects.empty())
		return text + "no train of service " + measure.serviceId + " in the run";
	for (std::size_t n = 0; n < applied.effects.size(); ++n)
		text += (n > 0 ? "; " : "") + applied.effects[n].trainDescription + " " + measureEffectText(applied.effects[n]);
	return text;
}
