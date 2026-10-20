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

enum class DelayEventKind {
	Arrival,
	Departure
};

enum class DelayClass {
	Unavailable,
	Early,
	OnTime,
	Late
};

struct DelayEvent {
	std::string trainId;
	std::string operatingCode;
	std::string serviceId;
	int occurrence = 1;
	std::string stationId;
	int journeyIndex = 0;
	int callIndex = 0;
	DelayEventKind kind = DelayEventKind::Arrival;
	// The authored planned time.
	RunResultValue plannedSeconds;
	RunResultValue simulatedSeconds;
	// Simulated minus planned. Available only with TimetableValueStatus::Available.
	RunResultValue delaySeconds;
	TimetableValueStatus status = TimetableValueStatus::NoPlanAndNotReached;
	DelayClass delayClass = DelayClass::Unavailable;
};

// Two events per row, the arrival first. `run` supplies the entrance delay of each
// occurrence; an occurrence that `run` does not list keeps the planned times of its rows.
std::vector<DelayEvent> buildDelayEvents(const std::vector<TimetableResultRow>& rows, const RunResults& run);

struct DelayOccurrence {
	// The service id, or the train id when the run has no service id.
	std::string serviceId;
	int occurrence = 1;
};

struct DelayFilter {
	// Empty selects every occurrence.
	std::vector<DelayOccurrence> occurrences;
	// Empty selects every station.
	std::vector<std::string> stations;
	DelayEventKind kind = DelayEventKind::Arrival;
};

// The thresholds of the summary: 0, 60, 180 and 300 s. An event is later than a
// threshold only when its delay is strictly greater.
std::vector<double> defaultDelayThresholdsSeconds();

struct DelayShare {
	double thresholdSeconds = 0.0;
	// Available events later than the threshold.
	int later = 0;
	// Available events.
	int denominator = 0;
	// later / denominator * 100. Unavailable when the denominator is 0.
	RunResultValue percent;
};

struct DelayStatistics {
	// Events that the filter selects. The next four counts add up to it, one count for each status.
	int calls = 0;
	int available = 0;
	int noPlan = 0;
	int notReached = 0;
	int noPlanAndNotReached = 0;
	// The next three counts add up to `available`.
	int early = 0;
	int onTime = 0;
	int late = 0;
	// All four are over the available events and unavailable when there are none.
	RunResultValue sumSeconds;
	RunResultValue meanSeconds;
	RunResultValue minimumSeconds;
	RunResultValue maximumSeconds;
	// One entry for each finite threshold, in the order given.
	std::vector<DelayShare> shares;
};

struct StationDelaySummary {
	std::string stationId;
	DelayStatistics statistics;
};

struct DelaySummary {
	// In the order of first appearance in the events. Every call of a repeated station counts for the same station.
	std::vector<StationDelaySummary> stations;
	// All selected events together; it is not a mean of the station means.
	DelayStatistics network;
};

DelaySummary summarizeDelays(const std::vector<DelayEvent>& events, const DelayFilter& filter,
	const std::vector<double>& thresholdsSeconds = defaultDelayThresholdsSeconds());

enum class DelayChange {
	NotAvailable,
	Earlier,
	Same,
	Later
};

struct DelayDifference {
	RunResultValue baselineSeconds;
	RunResultValue scenarioSeconds;
	// Scenario minus baseline. Unavailable unless both times are.
	RunResultValue additionalSeconds;
	DelayChange change = DelayChange::NotAvailable;
};

// One call of one occurrence in both runs.
struct BaselineCall {
	std::string trainId;
	std::string serviceId;
	int occurrence = 1;
	std::string stationId;
	int journeyIndex = 0;
	int callIndex = 0;
	// The last call of the occurrence.
	bool finalCall = false;
	DelayDifference arrival;
	DelayDifference departure;
};

struct BaselineOccurrence {
	std::string trainId;
	std::string operatingCode;
	std::string serviceId;
	int occurrence = 1;
	// The entrance delay of the scenario run: 0 when it applied none. A known input, not a measurement.
	double inputDelaySeconds = 0.0;
	// Empty when no entrance delay applied.
	std::string inputDelayStationId;
	bool hasDirectEvidence = false;
	std::vector<std::string> incidentIds;
	// The final call. Empty and 0 when the occurrence has no call in the runs.
	std::string finalStationId;
	int finalJourneyIndex = 0;
	int finalCallIndex = 0;
	// The arrival difference at the final call; NotAvailable without a call.
	DelayDifference finalArrival;
	// See "secondary delay" above. Unavailable when finalArrival is.
	RunResultValue secondarySeconds;
};

// The totals are over the final arrival of each occurrence.
struct BaselineTotals {
	int occurrences = 0;
	int later = 0;
	int same = 0;
	int earlier = 0;
	int notAvailable = 0;
	// Sum of the positive additional delays and of all additional delays. Unavailable when no
	// occurrence has a difference. They leave out the occurrences that have none.
	RunResultValue positiveSeconds;
	RunResultValue netSeconds;
	// True when there is an occurrence and every one has a difference.
	bool complete = false;
};

// The totals over the final arrival of each of the occurrences.
BaselineTotals baselineTotals(const std::vector<BaselineOccurrence>& occurrences);

enum class DelayDiagnosticKind {
	// A scenario id is empty.
	ScenarioIdMissing,
	ScenarioIdsEqual,
	SceneRevisionDiffers,
	BaseTimeDiffers,
	DurationDiffers,
	TimestepDiffers,
	BaselineHasIncidents,
	BaselineHasEntranceDelays,
	// The occurrence is in the baseline run only, or in the scenario run only.
	OccurrenceOnlyInBaseline,
	OccurrenceOnlyInScenario,
	// The occurrence has other calls (station, journey order or call number) in the two runs.
	CallsDiffer
};

struct DelayDiagnostic {
	DelayDiagnosticKind kind = DelayDiagnosticKind::ScenarioIdMissing;
	// Set for the occurrence kinds, else empty and 0.
	std::string serviceId;
	int occurrence = 0;
};

struct DelayBaselineComparison {
	// True when `diagnostics` is empty. Otherwise `occurrences` and `calls` are empty and the totals are unavailable.
	bool compatible = false;
	// Every incompatibility, in a fixed order.
	std::vector<DelayDiagnostic> diagnostics;
	// In the order of the scenario run.
	std::vector<BaselineOccurrence> occurrences;
	// An occurrence after another, each in journey order.
	std::vector<BaselineCall> calls;
	BaselineTotals totals;
};

// Compares two completed runs of the same case. The baseline has no incident and no entrance delay.
// The scenario may have either.
DelayBaselineComparison compareAgainstBaseline(const DelayRunSnapshot& baseline, const DelayRunSnapshot& scenario);

// A fixed identifier of the kind, such as "scene_revision_differs".
const char* delayDiagnosticCode(DelayDiagnosticKind kind);
