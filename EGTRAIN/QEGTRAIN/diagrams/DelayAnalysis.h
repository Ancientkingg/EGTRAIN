#pragma once

#include "diagrams/RunResults.h"

#include <string>
#include <vector>

// Delay analysis of completed runs, computed from the values of RunResults.h.
//
// Every time is in seconds and every share is in percent. A value that does not
// exist is an unavailable RunResultValue and never 0. The functions read no
// other state and change none.
//
// Definitions:
//  - An event is the arrival or the departure of one call of an occurrence. A
//    pass-through is not a call and has no event.
//  - The planned lateness of an event is its simulated time minus its planned
//    time, signed: negative is early, 0 is on time, positive is late. It is
//    stored without a tolerance. The planned time is the authored one: the
//    planned departure of the stop that an entrance delay moved is taken back
//    by the entrance delay, so that stop's departure shows the entrance delay.
//  - The entrance delay of an occurrence is the primary delay. It is an input
//    of the scenario, never inferred from a run.
//  - The additional delay of an event is the simulated time in the scenario run
//    minus the simulated time of the same occurrence and call in the baseline
//    run.
//  - The secondary delay of an occurrence is its additional delay at the final
//    arrival minus the entrance delay, when the stop that the entrance delay
//    moved comes before the final call. It is a difference of two numbers and
//    says nothing about a cause.
//  - Direct evidence is the recording of a direct incident effect on the
//    occurrence in the scenario run. An occurrence without evidence is not
//    shown to be delayed by propagation.

/// Which time of a call an event is.
enum class DelayEventKind {
	Arrival,  ///< The arrival at the stop
	Departure ///< The departure from the stop
};

/// The sign of the delay of an event.
enum class DelayClass {
	Unavailable, ///< The event has no delay
	Early,		 ///< Delay below 0
	OnTime,		 ///< Delay exactly 0
	Late		 ///< Delay above 0
};

/// The arrival or the departure of one call, with its delay against the authored plan.
struct DelayEvent {
	std::string trainId;						   ///< Name of the train in the run
	std::string operatingCode;					   ///< Operating code of the service
	std::string serviceId;						   ///< Scene service of the train
	int occurrence = 1;							   ///< Occurrence of the service, from 1
	std::string stationId;						   ///< TimetableResultRow::stationId
	int journeyIndex = 0;						   ///< TimetableResultRow::journeyIndex, from 1
	int callIndex = 0;							   ///< TimetableResultRow::callIndex, from 1
	DelayEventKind kind = DelayEventKind::Arrival; ///< Arrival or departure
	/// The authored planned time, in seconds of the run.
	RunResultValue plannedSeconds;
	RunResultValue simulatedSeconds; ///< In seconds of the run
	/// Simulated minus planned. Available only with TimetableValueStatus::Available.
	RunResultValue delaySeconds;
	/// Which of the planned and simulated times exist.
	TimetableValueStatus status = TimetableValueStatus::NoPlanAndNotReached;
	DelayClass delayClass = DelayClass::Unavailable; ///< The sign of delaySeconds
};

/// Two events per row, the arrival first. `run` supplies the entrance delay of each
/// occurrence; an occurrence that `run` does not list keeps the planned times of its rows.
std::vector<DelayEvent> buildDelayEvents(const std::vector<TimetableResultRow>& rows, const RunResults& run);

/// One occurrence of a service, as the filter selects it.
struct DelayOccurrence {
	/// The service id, or the train id when the run has no service id.
	std::string serviceId;
	int occurrence = 1; ///< Occurrence of the service, from 1
};

/// The events that a summary counts.
struct DelayFilter {
	/// Empty selects every occurrence.
	std::vector<DelayOccurrence> occurrences;
	/// Empty selects every station.
	std::vector<std::string> stations;
	DelayEventKind kind = DelayEventKind::Arrival; ///< Only events of this kind
};

/// The thresholds of the summary: 0, 60, 180 and 300 s. An event is later than a
/// threshold only when its delay is strictly greater.
std::vector<double> defaultDelayThresholdsSeconds();

/// The share of the available events that are later than one threshold.
struct DelayShare {
	double thresholdSeconds = 0.0; ///< Threshold in seconds
	/// Available events later than the threshold.
	int later = 0;
	/// Available events.
	int denominator = 0;
	/// later / denominator * 100. Unavailable when the denominator is 0.
	RunResultValue percent;
};

/// Counts and figures of a set of events.
struct DelayStatistics {
	/// Events that the filter selects. The next four counts add up to it, one count for each status.
	int calls = 0;
	int available = 0;			 ///< Events with TimetableValueStatus::Available
	int noPlan = 0;				 ///< Events with TimetableValueStatus::NoPlan
	int notReached = 0;			 ///< Events with TimetableValueStatus::NotReached
	int noPlanAndNotReached = 0; ///< Events with TimetableValueStatus::NoPlanAndNotReached
	/// Events with a delay below 0. It, onTime and late add up to `available`.
	int early = 0;
	int onTime = 0; ///< Events with a delay of exactly 0
	int late = 0;	///< Events with a delay above 0
	/// Sum of the delays in seconds. It and the next three are over the available events and unavailable when there
	/// are none.
	RunResultValue sumSeconds;
	RunResultValue meanSeconds;	   ///< sumSeconds / available
	RunResultValue minimumSeconds; ///< Smallest delay in seconds
	RunResultValue maximumSeconds; ///< Largest delay in seconds
	/// One entry for each finite threshold, in the order given.
	std::vector<DelayShare> shares;
};

/// The statistics of the events at one station.
struct StationDelaySummary {
	std::string stationId;		///< TimetableResultRow::stationId
	DelayStatistics statistics; ///< Over the selected events at the station
};

/// The statistics of the selected events, for each station and for the network.
struct DelaySummary {
	/// In the order of first appearance in the events. Every call of a repeated station counts for the same station.
	std::vector<StationDelaySummary> stations;
	/// All selected events together; it is not a mean of the station means.
	DelayStatistics network;
};

/// The statistics of the events that the filter selects, with one share for each finite threshold in seconds.
DelaySummary summarizeDelays(const std::vector<DelayEvent>& events, const DelayFilter& filter,
	const std::vector<double>& thresholdsSeconds = defaultDelayThresholdsSeconds());

/// The sign of an additional delay.
enum class DelayChange {
	NotAvailable, ///< One of the two times is missing
	Earlier,	  ///< Additional delay below 0
	Same,		  ///< Additional delay exactly 0
	Later		  ///< Additional delay above 0
};

/// The simulated time of one event in the baseline and in the scenario run.
struct DelayDifference {
	RunResultValue baselineSeconds; ///< Simulated time in the baseline run, in seconds of the run
	RunResultValue scenarioSeconds; ///< Simulated time in the scenario run, in seconds of the run
	/// Scenario minus baseline. Unavailable unless both times are.
	RunResultValue additionalSeconds;
	DelayChange change = DelayChange::NotAvailable; ///< The sign of additionalSeconds
};

/// One call of one occurrence in both runs.
struct BaselineCall {
	std::string trainId;   ///< Name of the train in the scenario run
	std::string serviceId; ///< Scene service of the train
	int occurrence = 1;	   ///< Occurrence of the service, from 1
	std::string stationId; ///< TimetableResultRow::stationId
	int journeyIndex = 0;  ///< TimetableResultRow::journeyIndex, from 1
	int callIndex = 0;	   ///< TimetableResultRow::callIndex, from 1
	/// The last call of the occurrence.
	bool finalCall = false;
	DelayDifference arrival;   ///< The simulated arrivals of the two runs
	DelayDifference departure; ///< The simulated departures of the two runs
};

/// One occurrence in both runs, judged at its final arrival.
struct BaselineOccurrence {
	std::string trainId;	   ///< Name of the train in the scenario run
	std::string operatingCode; ///< Operating code of the service
	std::string serviceId;	   ///< Scene service of the train
	int occurrence = 1;		   ///< Occurrence of the service, from 1
	/// The entrance delay of the scenario run: 0 when it applied none. A known input, not a measurement.
	double inputDelaySeconds = 0.0;
	/// Empty when no entrance delay applied.
	std::string inputDelayStationId;
	bool hasDirectEvidence = false;		  ///< True when the scenario run recorded a direct incident effect on it
	std::vector<std::string> incidentIds; ///< The incidents of that evidence, TrainRunResult::directIncidentIds
	/// Station of the final call. It and the two indices below are empty and 0 when the occurrence has no call in
	/// the runs.
	std::string finalStationId;
	int finalJourneyIndex = 0; ///< TimetableResultRow::journeyIndex of the final call
	int finalCallIndex = 0;	   ///< TimetableResultRow::callIndex of the final call
	/// The arrival difference at the final call; NotAvailable without a call.
	DelayDifference finalArrival;
	/// See "secondary delay" above. Unavailable when finalArrival is.
	RunResultValue secondarySeconds;
};

/// The totals are over the final arrival of each occurrence.
struct BaselineTotals {
	int occurrences = 0;  ///< Occurrences compared
	int later = 0;		  ///< Occurrences with DelayChange::Later
	int same = 0;		  ///< Occurrences with DelayChange::Same
	int earlier = 0;	  ///< Occurrences with DelayChange::Earlier
	int notAvailable = 0; ///< Occurrences with DelayChange::NotAvailable
	/// Sum of the positive additional delays in seconds. It and netSeconds are unavailable when no occurrence has a
	/// difference, and they leave out the occurrences that have none.
	RunResultValue positiveSeconds;
	RunResultValue netSeconds; ///< Sum of all additional delays, in seconds
	/// True when there is an occurrence and every one has a difference.
	bool complete = false;
};

/// The totals over the final arrival of each of the occurrences.
BaselineTotals baselineTotals(const std::vector<BaselineOccurrence>& occurrences);

/// Why two runs cannot be compared.
enum class DelayDiagnosticKind {
	/// A scenario id is empty.
	ScenarioIdMissing,
	ScenarioIdsEqual,		   ///< Both runs applied the same scenario
	SceneRevisionDiffers,	   ///< The runs used other revisions of the scene
	BaseTimeDiffers,		   ///< The runs have other base times
	DurationDiffers,		   ///< The runs have other lengths
	TimestepDiffers,		   ///< The runs have other timesteps
	BaselineHasIncidents,	   ///< The scenario of the baseline has an incident
	BaselineHasEntranceDelays, ///< The scenario of the baseline has an entrance delay
	OccurrenceOnlyInBaseline,  ///< The occurrence is in the baseline run only
	OccurrenceOnlyInScenario,  ///< The occurrence is in the scenario run only
	/// The occurrence has other calls (station, journey order or call number) in the two runs.
	CallsDiffer
};

/// One reason why two runs cannot be compared.
struct DelayDiagnostic {
	DelayDiagnosticKind kind = DelayDiagnosticKind::ScenarioIdMissing; ///< The reason
	/// Service of the occurrence for the occurrence kinds, or the train id when the run has no service id; else empty.
	std::string serviceId;
	int occurrence = 0; ///< Occurrence of the service, from 1; 0 for the other kinds
};

/// The comparison of a scenario run with a baseline run.
struct DelayBaselineComparison {
	/// True when `diagnostics` is empty. Otherwise `occurrences` and `calls` are empty and the totals are unavailable.
	bool compatible = false;
	/// Every incompatibility, in a fixed order.
	std::vector<DelayDiagnostic> diagnostics;
	/// In the order of the scenario run.
	std::vector<BaselineOccurrence> occurrences;
	/// An occurrence after another, each in journey order.
	std::vector<BaselineCall> calls;
	BaselineTotals totals; ///< Over the final arrivals of the occurrences
};

/// Compares two completed runs of the same case. The baseline has no incident and no entrance delay.
/// The scenario may have either.
DelayBaselineComparison compareAgainstBaseline(const DelayRunSnapshot& baseline, const DelayRunSnapshot& scenario);

/// A fixed identifier of the kind, such as "scene_revision_differs".
const char* delayDiagnosticCode(DelayDiagnosticKind kind);
