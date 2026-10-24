#include "diagrams/DelayAnalysis.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace {

using OccurrenceKey = std::pair<std::string, int>;
using CallList = std::vector<const TimetableResultRow*>;
using CallsByOccurrence = std::map<OccurrenceKey, CallList>;

OccurrenceKey occurrenceKey(const std::string& serviceId, int occurrence, const std::string& trainId) {
	return {serviceId.empty() ? trainId : serviceId, occurrence};
}

OccurrenceKey occurrenceKey(const TrainRunResult& train) {
	return occurrenceKey(train.serviceId, train.occurrence, train.trainId);
}

OccurrenceKey occurrenceKey(const TimetableResultRow& row) {
	return occurrenceKey(row.serviceId, row.occurrence, row.trainId);
}

DelayClass classify(const RunResultValue& delay) {
	if (!delay.available)
		return DelayClass::Unavailable;
	if (delay.value < 0.0)
		return DelayClass::Early;
	return delay.value > 0.0 ? DelayClass::Late : DelayClass::OnTime;
}

DelayEvent makeEvent(const TimetableResultRow& row, DelayEventKind kind, const RunResultValue& planned,
	const RunResultValue& simulated, TimetableValueStatus status) {
	DelayEvent event;
	event.trainId = row.trainId;
	event.operatingCode = row.operatingCode;
	event.serviceId = row.serviceId;
	event.occurrence = row.occurrence;
	event.stationId = row.stationId;
	event.journeyIndex = row.journeyIndex;
	event.callIndex = row.callIndex;
	event.kind = kind;
	event.plannedSeconds = planned;
	event.simulatedSeconds = simulated;
	event.status = status;
	if (status == TimetableValueStatus::Available && planned.available && simulated.available)
		event.delaySeconds = {true, simulated.value - planned.value};
	event.delayClass = classify(event.delaySeconds);
	return event;
}

bool occurrenceSelected(const DelayFilter& filter, const DelayEvent& event) {
	if (filter.occurrences.empty())
		return true;
	const OccurrenceKey key = occurrenceKey(event.serviceId, event.occurrence, event.trainId);
	return std::any_of(filter.occurrences.begin(), filter.occurrences.end(),
		[&key](const DelayOccurrence& wanted) { return wanted.serviceId == key.first && wanted.occurrence == key.second; });
}

bool stationSelected(const DelayFilter& filter, const DelayEvent& event) {
	return filter.stations.empty()
		|| std::find(filter.stations.begin(), filter.stations.end(), event.stationId) != filter.stations.end();
}

DelayStatistics statisticsOf(const std::vector<const DelayEvent*>& events, const std::vector<double>& thresholdsSeconds) {
	DelayStatistics statistics;
	std::vector<double> delays;
	for (const DelayEvent* event : events) {
		++statistics.calls;
		switch (event->status) {
			case TimetableValueStatus::NoPlan:
				++statistics.noPlan;
				continue;
			case TimetableValueStatus::NotReached:
				++statistics.notReached;
				continue;
			case TimetableValueStatus::NoPlanAndNotReached:
				++statistics.noPlanAndNotReached;
				continue;
			case TimetableValueStatus::Available:
				break;
		}
		++statistics.available;
		const double delay = event->delaySeconds.value;
		delays.push_back(delay);
		if (delay < 0.0)
			++statistics.early;
		else if (delay > 0.0)
			++statistics.late;
		else
			++statistics.onTime;
	}

	if (!delays.empty()) {
		double sum = 0.0;
		for (const double delay : delays)
			sum += delay;
		statistics.sumSeconds = {true, sum};
		statistics.meanSeconds = {true, sum / static_cast<double>(delays.size())};
		statistics.minimumSeconds = {true, *std::min_element(delays.begin(), delays.end())};
		statistics.maximumSeconds = {true, *std::max_element(delays.begin(), delays.end())};
	}
	for (const double threshold : thresholdsSeconds) {
		if (!std::isfinite(threshold))
			continue;
		DelayShare share;
		share.thresholdSeconds = threshold;
		share.denominator = statistics.available;
		share.later = static_cast<int>(std::count_if(delays.begin(), delays.end(), [threshold](double delay) { return delay > threshold; }));
		if (share.denominator > 0)
			share.percent = {true, 100.0 * share.later / share.denominator};
		statistics.shares.push_back(share);
	}
	return statistics;
}

// A number of seconds in minutes, such as "3" or "1.5".
std::string minutesText(double seconds) {
	std::ostringstream text;
	text << seconds / 60.0;
	return text.str();
}

CallsByOccurrence callsByOccurrence(const std::vector<TimetableResultRow>& rows) {
	CallsByOccurrence result;
	for (const TimetableResultRow& row : rows)
		result[occurrenceKey(row)].push_back(&row);
	for (auto& entry : result) {
		std::stable_sort(entry.second.begin(), entry.second.end(),
			[](const TimetableResultRow* a, const TimetableResultRow* b) { return a->journeyIndex < b->journeyIndex; });
	}
	return result;
}

const CallList& callsOf(const CallsByOccurrence& calls, const OccurrenceKey& key) {
	static const CallList none;
	const auto found = calls.find(key);
	return found == calls.end() ? none : found->second;
}

std::set<OccurrenceKey> occurrenceKeys(const RunResults& run) {
	std::set<OccurrenceKey> keys;
	for (const TrainRunResult& train : run.trains)
		keys.insert(occurrenceKey(train));
	return keys;
}

bool sameCalls(const CallList& a, const CallList& b) {
	if (a.size() != b.size())
		return false;
	for (std::size_t index = 0; index < a.size(); ++index) {
		if (a[index]->journeyIndex != b[index]->journeyIndex || a[index]->stationId != b[index]->stationId
			|| a[index]->callIndex != b[index]->callIndex)
			return false;
	}
	return true;
}

DelayDifference difference(const RunResultValue& baseline, const RunResultValue& scenario) {
	DelayDifference result;
	result.baselineSeconds = baseline;
	result.scenarioSeconds = scenario;
	if (!baseline.available || !scenario.available)
		return result;
	const double additional = scenario.value - baseline.value;
	if (!std::isfinite(additional))
		return result;
	result.additionalSeconds = {true, additional};
	result.change = additional > 0.0 ? DelayChange::Later : additional < 0.0 ? DelayChange::Earlier
																			 : DelayChange::Same;
	return result;
}

// The entrance delay that lies before the final call of the occurrence: the delay when the stop it moved comes
// earlier in the journey than the final call, else 0.
double inputDelayBeforeFinalCall(const TrainRunResult& train, const CallList& calls) {
	if (calls.empty() || train.entranceDelayStationId.empty() || !std::isfinite(train.entranceDelaySeconds))
		return 0.0;
	for (const TimetableResultRow* call : calls) {
		if (call->stationId == train.entranceDelayStationId && call->callIndex == 1)
			return call->journeyIndex < calls.back()->journeyIndex ? train.entranceDelaySeconds : 0.0;
	}
	return 0.0;
}

BaselineOccurrence occurrenceOf(const TrainRunResult& train, const CallList& baselineCalls, const CallList& scenarioCalls,
	std::vector<BaselineCall>& calls) {
	BaselineOccurrence occurrence;
	occurrence.trainId = train.trainId;
	occurrence.operatingCode = train.operatingCode;
	occurrence.serviceId = train.serviceId;
	occurrence.occurrence = train.occurrence;
	occurrence.inputDelaySeconds = train.entranceDelaySeconds;
	occurrence.inputDelayStationId = train.entranceDelayStationId;
	occurrence.hasDirectEvidence = !train.directIncidentIds.empty();
	occurrence.incidentIds = train.directIncidentIds;

	for (std::size_t index = 0; index < scenarioCalls.size(); ++index) {
		const TimetableResultRow& baselineRow = *baselineCalls[index];
		const TimetableResultRow& scenarioRow = *scenarioCalls[index];
		BaselineCall call;
		call.trainId = scenarioRow.trainId;
		call.serviceId = scenarioRow.serviceId;
		call.occurrence = scenarioRow.occurrence;
		call.stationId = scenarioRow.stationId;
		call.journeyIndex = scenarioRow.journeyIndex;
		call.callIndex = scenarioRow.callIndex;
		call.finalCall = index + 1 == scenarioCalls.size();
		call.arrival = difference(baselineRow.simulatedArrivalSeconds, scenarioRow.simulatedArrivalSeconds);
		call.departure = difference(baselineRow.simulatedDepartureSeconds, scenarioRow.simulatedDepartureSeconds);
		calls.push_back(call);
	}
	if (!scenarioCalls.empty()) {
		const BaselineCall& finalCall = calls.back();
		occurrence.finalStationId = finalCall.stationId;
		occurrence.finalJourneyIndex = finalCall.journeyIndex;
		occurrence.finalCallIndex = finalCall.callIndex;
		occurrence.finalArrival = finalCall.arrival;
		if (finalCall.arrival.additionalSeconds.available)
			occurrence.secondarySeconds = {true, finalCall.arrival.additionalSeconds.value - inputDelayBeforeFinalCall(train, scenarioCalls)};
	}
	return occurrence;
}

} // namespace

std::vector<DelayEvent> buildDelayEvents(const std::vector<TimetableResultRow>& rows, const RunResults& run) {
	std::map<OccurrenceKey, const TrainRunResult*> trains;
	for (const TrainRunResult& train : run.trains)
		trains.emplace(occurrenceKey(train), &train);

	std::vector<DelayEvent> events;
	events.reserve(rows.size() * 2);
	for (const TimetableResultRow& row : rows) {
		RunResultValue plannedDeparture = row.plannedDepartureSeconds;
		const auto train = trains.find(occurrenceKey(row));
		if (train != trains.end() && plannedDeparture.available && row.callIndex == 1
			&& !train->second->entranceDelayStationId.empty() && train->second->entranceDelayStationId == row.stationId
			&& std::isfinite(train->second->entranceDelaySeconds))
			plannedDeparture.value -= train->second->entranceDelaySeconds;
		events.push_back(makeEvent(row, DelayEventKind::Arrival, row.plannedArrivalSeconds, row.simulatedArrivalSeconds, row.arrivalStatus));
		events.push_back(makeEvent(row, DelayEventKind::Departure, plannedDeparture, row.simulatedDepartureSeconds, row.departureStatus));
	}
	return events;
}

std::vector<double> defaultDelayThresholdsSeconds() {
	return {0.0, 60.0, 180.0, 300.0};
}

DelaySummary summarizeDelays(const std::vector<DelayEvent>& events, const DelayFilter& filter,
	const std::vector<double>& thresholdsSeconds) {
	std::vector<const DelayEvent*> selected;
	std::vector<std::string> stationOrder;
	std::map<std::string, std::vector<const DelayEvent*>> byStation;
	for (const DelayEvent& event : events) {
		if (event.kind != filter.kind || !occurrenceSelected(filter, event) || !stationSelected(filter, event))
			continue;
		selected.push_back(&event);
		auto station = byStation.find(event.stationId);
		if (station == byStation.end()) {
			stationOrder.push_back(event.stationId);
			station = byStation.emplace(event.stationId, std::vector<const DelayEvent*>()).first;
		}
		station->second.push_back(&event);
	}

	DelaySummary summary;
	summary.network = statisticsOf(selected, thresholdsSeconds);
	for (const std::string& stationId : stationOrder)
		summary.stations.push_back({stationId, statisticsOf(byStation[stationId], thresholdsSeconds)});
	return summary;
}

DelayHistogram delayHistogram(const std::vector<DelayEvent>& events) {
	const std::vector<double> thresholds = defaultDelayThresholdsSeconds();
	DelayHistogram histogram;
	const auto addClass = [&histogram](const std::string& label, const RunResultValue& from, const RunResultValue& to) {
		DelayHistogramClass delayClass;
		delayClass.label = label;
		delayClass.fromSeconds = from;
		delayClass.toSeconds = to;
		histogram.classes.push_back(delayClass);
	};
	addClass("Early", {}, {true, 0.0});
	addClass("On time", {true, 0.0}, {true, 0.0});
	for (std::size_t index = 1; index < thresholds.size(); ++index) {
		const double from = thresholds[index - 1];
		const double to = thresholds[index];
		addClass(from == 0.0 ? "Up to " + minutesText(to) + " min late" : minutesText(from) + " to " + minutesText(to) + " min late",
			{true, from}, {true, to});
	}
	addClass("Over " + minutesText(thresholds.back()) + " min late", {true, thresholds.back()}, {});

	for (const DelayEvent& event : events) {
		switch (event.status) {
			case TimetableValueStatus::NoPlan:
				++histogram.noPlan;
				continue;
			case TimetableValueStatus::NotReached:
				++histogram.notReached;
				continue;
			case TimetableValueStatus::NoPlanAndNotReached:
				++histogram.noPlanAndNotReached;
				continue;
			case TimetableValueStatus::Available:
				break;
		}
		++histogram.available;
		// Early below 0, else On time and one class further for each threshold that the delay is strictly later than.
		const double delay = event.delaySeconds.value;
		const auto later = std::count_if(thresholds.begin(), thresholds.end(), [delay](double threshold) { return delay > threshold; });
		++histogram.classes[delay < 0.0 ? 0 : 1 + static_cast<std::size_t>(later)].events;
	}
	if (histogram.available > 0) {
		for (DelayHistogramClass& delayClass : histogram.classes)
			delayClass.percent = {true, 100.0 * delayClass.events / histogram.available};
	}
	return histogram;
}

BaselineTotals baselineTotals(const std::vector<BaselineOccurrence>& occurrences) {
	BaselineTotals totals;
	double positive = 0.0;
	double net = 0.0;
	for (const BaselineOccurrence& occurrence : occurrences) {
		++totals.occurrences;
		const DelayDifference& finalArrival = occurrence.finalArrival;
		switch (finalArrival.change) {
			case DelayChange::NotAvailable:
				++totals.notAvailable;
				continue;
			case DelayChange::Earlier:
				++totals.earlier;
				break;
			case DelayChange::Same:
				++totals.same;
				break;
			case DelayChange::Later:
				++totals.later;
				positive += finalArrival.additionalSeconds.value;
				break;
		}
		net += finalArrival.additionalSeconds.value;
	}
	if (totals.occurrences > totals.notAvailable) {
		totals.positiveSeconds = {true, positive};
		totals.netSeconds = {true, net};
	}
	totals.complete = totals.occurrences > 0 && totals.notAvailable == 0;
	return totals;
}

DelayBaselineComparison compareAgainstBaseline(const DelayRunSnapshot& baseline, const DelayRunSnapshot& scenario) {
	DelayBaselineComparison result;
	const auto report = [&result](DelayDiagnosticKind kind, const OccurrenceKey& key = {}) {
		result.diagnostics.push_back({kind, key.first, key.second});
	};
	if (baseline.scenarioId.empty() || scenario.scenarioId.empty())
		report(DelayDiagnosticKind::ScenarioIdMissing);
	else if (baseline.scenarioId == scenario.scenarioId)
		report(DelayDiagnosticKind::ScenarioIdsEqual);
	if (baseline.caseRevision != scenario.caseRevision)
		report(DelayDiagnosticKind::SceneRevisionDiffers);
	if (baseline.baseTimeSeconds != scenario.baseTimeSeconds)
		report(DelayDiagnosticKind::BaseTimeDiffers);
	if (baseline.durationSeconds != scenario.durationSeconds)
		report(DelayDiagnosticKind::DurationDiffers);
	if (baseline.timestep != scenario.timestep)
		report(DelayDiagnosticKind::TimestepDiffers);
	if (baseline.hasIncidents)
		report(DelayDiagnosticKind::BaselineHasIncidents);
	if (baseline.hasEntranceDelays)
		report(DelayDiagnosticKind::BaselineHasEntranceDelays);

	const std::set<OccurrenceKey> baselineKeys = occurrenceKeys(baseline.run);
	const std::set<OccurrenceKey> scenarioKeys = occurrenceKeys(scenario.run);
	const CallsByOccurrence baselineCalls = callsByOccurrence(baseline.timetable);
	const CallsByOccurrence scenarioCalls = callsByOccurrence(scenario.timetable);
	for (const TrainRunResult& train : baseline.run.trains) {
		if (scenarioKeys.count(occurrenceKey(train)) == 0)
			report(DelayDiagnosticKind::OccurrenceOnlyInBaseline, occurrenceKey(train));
	}
	for (const TrainRunResult& train : scenario.run.trains) {
		const OccurrenceKey key = occurrenceKey(train);
		if (baselineKeys.count(key) == 0)
			report(DelayDiagnosticKind::OccurrenceOnlyInScenario, key);
		else if (!sameCalls(callsOf(baselineCalls, key), callsOf(scenarioCalls, key)))
			report(DelayDiagnosticKind::CallsDiffer, key);
	}
	result.compatible = result.diagnostics.empty();
	if (!result.compatible)
		return result;

	for (const TrainRunResult& train : scenario.run.trains) {
		const OccurrenceKey key = occurrenceKey(train);
		result.occurrences.push_back(occurrenceOf(train, callsOf(baselineCalls, key), callsOf(scenarioCalls, key), result.calls));
	}

	result.totals = baselineTotals(result.occurrences);
	return result;
}

const char* delayDiagnosticCode(DelayDiagnosticKind kind) {
	switch (kind) {
		case DelayDiagnosticKind::ScenarioIdMissing:
			return "scenario_id_missing";
		case DelayDiagnosticKind::ScenarioIdsEqual:
			return "scenario_ids_equal";
		case DelayDiagnosticKind::SceneRevisionDiffers:
			return "scene_revision_differs";
		case DelayDiagnosticKind::BaseTimeDiffers:
			return "base_time_differs";
		case DelayDiagnosticKind::DurationDiffers:
			return "duration_differs";
		case DelayDiagnosticKind::TimestepDiffers:
			return "timestep_differs";
		case DelayDiagnosticKind::BaselineHasIncidents:
			return "baseline_has_incidents";
		case DelayDiagnosticKind::BaselineHasEntranceDelays:
			return "baseline_has_entrance_delays";
		case DelayDiagnosticKind::OccurrenceOnlyInBaseline:
			return "occurrence_only_in_baseline";
		case DelayDiagnosticKind::OccurrenceOnlyInScenario:
			return "occurrence_only_in_scenario";
		case DelayDiagnosticKind::CallsDiffer:
			return "calls_differ";
	}
	return "unknown";
}
