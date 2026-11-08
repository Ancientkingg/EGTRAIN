// The rows are typed by hand and the test reads no file. The numbers of the
// line fixture are copied from tests/characterization/expected: follow-level-0
// and sf-forward-level-0 as they stand, the pairs for no signalling level and
// for levels 3 and 5 likewise. Every expected figure below was worked out by
// hand from those rows. Times and delays are in seconds, shares in percent.
// kNone marks a time that does not exist.

#include "diagrams/DelayAnalysis.h"

#include <QTest>

#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kNone = -1.0;

bool near(double a, double b) {
	return std::abs(a - b) < 1e-9;
}

bool isValue(const RunResultValue& value, double expected) {
	return value.available && near(value.value, expected);
}

RunResultValue secondsOrNone(double seconds) {
	return seconds < 0.0 ? RunResultValue() : RunResultValue{true, seconds};
}

TimetableValueStatus statusOf(const RunResultValue& planned, const RunResultValue& simulated) {
	if (planned.available)
		return simulated.available ? TimetableValueStatus::Available : TimetableValueStatus::NotReached;
	return simulated.available ? TimetableValueStatus::NoPlan : TimetableValueStatus::NoPlanAndNotReached;
}

struct CallTimes {
	double plannedArrival;
	double plannedDeparture;
	double simulatedArrival;
	double simulatedDeparture;
};

// The delay fields of a row are not read by the module, so the rows leave them out.
TimetableResultRow row(const char* service, int occurrence, const char* station, int journey, int call, const CallTimes& times) {
	TimetableResultRow result;
	result.trainId = std::string(service) + "-" + std::to_string(occurrence);
	result.serviceId = service;
	result.occurrence = occurrence;
	result.stationId = station;
	result.journeyIndex = journey;
	result.callIndex = call;
	result.plannedArrivalSeconds = secondsOrNone(times.plannedArrival);
	result.plannedDepartureSeconds = secondsOrNone(times.plannedDeparture);
	result.simulatedArrivalSeconds = secondsOrNone(times.simulatedArrival);
	result.simulatedDepartureSeconds = secondsOrNone(times.simulatedDeparture);
	result.arrivalStatus = statusOf(result.plannedArrivalSeconds, result.simulatedArrivalSeconds);
	result.departureStatus = statusOf(result.plannedDepartureSeconds, result.simulatedDepartureSeconds);
	return result;
}

TrainRunResult trainResult(const char* service, int occurrence, double entranceDelay = 0.0, const char* entranceStation = "",
	std::vector<std::string> incidents = {}) {
	TrainRunResult result;
	result.trainId = std::string(service) + "-" + std::to_string(occurrence);
	result.serviceId = service;
	result.occurrence = occurrence;
	result.entranceDelaySeconds = entranceDelay;
	result.entranceDelayStationId = entranceStation;
	result.directIncidentIds = std::move(incidents);
	return result;
}

DelayRunSnapshot snapshot(const char* scenarioId, std::vector<TimetableResultRow> rows, std::vector<TrainRunResult> trains) {
	DelayRunSnapshot result;
	result.caseRevision = "rev-1";
	result.scenarioId = scenarioId;
	result.baseTimeSeconds = 0.0;
	result.durationSeconds = 1500.0;
	result.timestep = 1.0;
	result.timetable = std::move(rows);
	result.run.trains = std::move(trains);
	return result;
}

// The simulated times of the line fixture of the characterization tests: F1 and F2 call at A, B and C. The
// planned times are the same in every case, so only the simulated times are arguments. Neither train has a
// simulated time at A.
struct LineTimes {
	double f1BArrival, f1BDeparture, f1CArrival, f1CDeparture;
	double f2BArrival, f2BDeparture, f2CArrival, f2CDeparture;
};

std::vector<TimetableResultRow> lineRows(const LineTimes& t) {
	return {
		row("F1", 1, "A", 1, 1, {kNone, 120.0, kNone, kNone}),
		row("F1", 1, "B", 2, 1, {600.0, 660.0, t.f1BArrival, t.f1BDeparture}),
		row("F1", 1, "C", 3, 1, {1100.0, kNone, t.f1CArrival, t.f1CDeparture}),
		row("F2", 1, "A", 1, 1, {kNone, 180.0, kNone, kNone}),
		row("F2", 1, "B", 2, 1, {660.0, 720.0, t.f2BArrival, t.f2BDeparture}),
		row("F2", 1, "C", 3, 1, {1160.0, kNone, t.f2CArrival, t.f2CDeparture}),
	};
}

RunResults lineRun(std::vector<std::string> f1Incidents = {}, std::vector<std::string> f2Incidents = {}) {
	RunResults run;
	run.trains.push_back(trainResult("F1", 1, 0.0, "", std::move(f1Incidents)));
	run.trains.push_back(trainResult("F2", 1, 0.0, "", std::move(f2Incidents)));
	return run;
}

DelayRunSnapshot lineSnapshot(const char* scenarioId, const LineTimes& times, std::vector<std::string> f1Incidents = {},
	std::vector<std::string> f2Incidents = {}) {
	DelayRunSnapshot result = snapshot(scenarioId, lineRows(times), {});
	result.run = lineRun(std::move(f1Incidents), std::move(f2Incidents));
	result.hasIncidents = !result.run.trains[0].directIncidentIds.empty() || !result.run.trains[1].directIncidentIds.empty();
	return result;
}

struct Counts {
	int calls, available, noPlan, notReached, noPlanAndNotReached, early, onTime, late;
};

bool hasCounts(const DelayStatistics& s, const Counts& c) {
	return s.calls == c.calls && s.available == c.available && s.noPlan == c.noPlan && s.notReached == c.notReached
		&& s.noPlanAndNotReached == c.noPlanAndNotReached && s.early == c.early && s.onTime == c.onTime && s.late == c.late;
}

bool hasNoFigures(const DelayStatistics& s) {
	return !s.sumSeconds.available && !s.meanSeconds.available && !s.minimumSeconds.available && !s.maximumSeconds.available;
}

bool hasFigures(const DelayStatistics& s, double sum, double mean, double minimum, double maximum) {
	return isValue(s.sumSeconds, sum) && isValue(s.meanSeconds, mean) && isValue(s.minimumSeconds, minimum)
		&& isValue(s.maximumSeconds, maximum);
}

struct ShareExpectation {
	int later;
	double percent;
};

// The four default thresholds 0, 60, 180 and 300 s; a percent of kNone stands for unavailable.
bool hasShares(const DelayStatistics& s, int denominator, const ShareExpectation (&expected)[4]) {
	const double thresholds[4] = {0.0, 60.0, 180.0, 300.0};
	if (s.shares.size() != 4)
		return false;
	for (std::size_t index = 0; index < 4; ++index) {
		const DelayShare& share = s.shares[index];
		const bool percentOk = expected[index].percent == kNone ? !share.percent.available : isValue(share.percent, expected[index].percent);
		if (!near(share.thresholdSeconds, thresholds[index]) || share.later != expected[index].later || share.denominator != denominator
			|| !percentOk)
			return false;
	}
	return true;
}

const StationDelaySummary* stationOf(const DelaySummary& summary, const std::string& stationId) {
	for (const StationDelaySummary& station : summary.stations) {
		if (station.stationId == stationId)
			return &station;
	}
	return nullptr;
}

const BaselineOccurrence* occurrenceOf(const DelayBaselineComparison& comparison, const std::string& serviceId) {
	for (const BaselineOccurrence& occurrence : comparison.occurrences) {
		if (occurrence.serviceId == serviceId)
			return &occurrence;
	}
	return nullptr;
}

const BaselineCall* callOf(const DelayBaselineComparison& comparison, const std::string& serviceId, const std::string& stationId,
	int callIndex = 1) {
	for (const BaselineCall& call : comparison.calls) {
		if (call.serviceId == serviceId && call.stationId == stationId && call.callIndex == callIndex)
			return &call;
	}
	return nullptr;
}

bool isDifference(const DelayDifference& difference, DelayChange change, double additional) {
	return difference.change == change && isValue(difference.additionalSeconds, additional);
}

bool isUnavailableDifference(const DelayDifference& difference) {
	return difference.change == DelayChange::NotAvailable && !difference.additionalSeconds.available;
}

bool onlyDiagnostics(const DelayBaselineComparison& comparison, std::initializer_list<DelayDiagnosticKind> kinds) {
	if (comparison.diagnostics.size() != kinds.size())
		return false;
	std::size_t index = 0;
	for (const DelayDiagnosticKind kind : kinds) {
		if (comparison.diagnostics[index++].kind != kind)
			return false;
	}
	return true;
}

struct ClassExpectation {
	const char* label;
	double from; // kNone for no lower bound
	double to;	 // kNone for no upper bound
	int events;
	double percent; // kNone for unavailable
};

bool isValueOrNone(const RunResultValue& value, double expected) {
	return expected == kNone ? !value.available : isValue(value, expected);
}

bool hasClasses(const DelayHistogram& histogram, const ClassExpectation (&expected)[6]) {
	if (histogram.classes.size() != 6)
		return false;
	for (std::size_t index = 0; index < 6; ++index) {
		const DelayHistogramClass& delayClass = histogram.classes[index];
		if (delayClass.label != expected[index].label || !isValueOrNone(delayClass.fromSeconds, expected[index].from)
			|| !isValueOrNone(delayClass.toSeconds, expected[index].to) || delayClass.events != expected[index].events
			|| !isValueOrNone(delayClass.percent, expected[index].percent))
			return false;
	}
	return true;
}

std::vector<DelayEvent> ofKind(const std::vector<DelayEvent>& events, DelayEventKind kind) {
	std::vector<DelayEvent> result;
	for (const DelayEvent& event : events) {
		if (event.kind == kind)
			result.push_back(event);
	}
	return result;
}

// Every share of the summary counts the classes above its threshold: those whose lower bound is at least the threshold
// and whose upper bound, when they have one, lies above it.
bool agreesWithSummary(const DelayHistogram& histogram, const DelayStatistics& statistics) {
	if (histogram.available != statistics.available || statistics.shares.size() != 4)
		return false;
	for (const DelayShare& share : statistics.shares) {
		int later = 0;
		double percent = 0.0;
		for (const DelayHistogramClass& delayClass : histogram.classes) {
			if (delayClass.fromSeconds.available && delayClass.fromSeconds.value >= share.thresholdSeconds
				&& (!delayClass.toSeconds.available || delayClass.toSeconds.value > share.thresholdSeconds)) {
				later += delayClass.events;
				percent += delayClass.percent.value;
			}
		}
		if (later != share.later || share.percent.available != (histogram.available > 0)
			|| (share.percent.available && !near(percent, share.percent.value)))
			return false;
	}
	return true;
}

} // namespace

class TestDelayAnalysis : public QObject {
	Q_OBJECT

private slots:
	void buildsEventsOfFollowLevel0();
	void summarizesArrivalsOfFollowLevel0();
	void summarizesDeparturesOfFollowLevel0();
	void filtersOccurrencesAndStations();
	void countsStrictlyLaterThanThresholds();
	void classifiesDelays();
	void classifiesFollowLevel0();
	void countsEarlyZeroMissingAndRepeatedCalls();
	void measuresEntranceDelayAgainstAuthoredPlan();
	void comparesPairWithoutSignalling();
	void comparesPairAtLevel3();
	void comparesPairAtLevel0();
	void comparesPairWithMissingCall();
	void separatesInputDelayFromSecondaryDelay();
	void reportsEveryIncompatibility();
	void countsLaterSameAndEarlier();
	void comparesEmptyRuns();
	void identifiesOccurrenceWithoutServiceId();
};

void TestDelayAnalysis::buildsEventsOfFollowLevel0() {
	const std::vector<DelayEvent> events = buildDelayEvents(lineRows({331, 660, 933, 934, 817, 877, 1150, 1151}), lineRun());
	// events: two for each row
	QCOMPARE(events.size(), std::size_t(12));
	const DelayEvent& aArrival = events[0];
	const DelayEvent& aDeparture = events[1];
	const DelayEvent& bArrival = events[2];
	const DelayEvent& bDeparture = events[3];
	const DelayEvent& cDeparture = events[5];
	const DelayEvent& f2BArrival = events[8];
	// events: no plan and no arrival
	QCOMPARE(aArrival.kind, DelayEventKind::Arrival);
	QCOMPARE(aArrival.trainId, "F1-1");
	QCOMPARE(aArrival.serviceId, "F1");
	QCOMPARE(aArrival.stationId, "A");
	QCOMPARE(aArrival.journeyIndex, 1);
	QCOMPARE(aArrival.callIndex, 1);
	QCOMPARE(aArrival.status, TimetableValueStatus::NoPlanAndNotReached);
	QCOMPARE(aArrival.delayClass, DelayClass::Unavailable);
	QVERIFY(!aArrival.plannedSeconds.available);
	QVERIFY(!aArrival.simulatedSeconds.available);
	QVERIFY(!aArrival.delaySeconds.available);
	// events: a train that never left has no delay, not 0
	QCOMPARE(aDeparture.kind, DelayEventKind::Departure);
	QCOMPARE(aDeparture.status, TimetableValueStatus::NotReached);
	QVERIFY(isValue(aDeparture.plannedSeconds, 120.0));
	QVERIFY(!aDeparture.simulatedSeconds.available);
	QVERIFY(!aDeparture.delaySeconds.available);
	QCOMPARE(aDeparture.delayClass, DelayClass::Unavailable);
	// events: early arrival
	QCOMPARE(bArrival.status, TimetableValueStatus::Available);
	QVERIFY(isValue(bArrival.plannedSeconds, 600.0));
	QVERIFY(isValue(bArrival.simulatedSeconds, 331.0));
	QVERIFY(isValue(bArrival.delaySeconds, -269.0));
	QCOMPARE(bArrival.delayClass, DelayClass::Early);
	// events: zero delay is on time
	QVERIFY(isValue(bDeparture.delaySeconds, 0.0));
	QCOMPARE(bDeparture.delayClass, DelayClass::OnTime);
	// events: a departure without a plan has no delay
	QCOMPARE(cDeparture.status, TimetableValueStatus::NoPlan);
	QVERIFY(isValue(cDeparture.simulatedSeconds, 934.0));
	QVERIFY(!cDeparture.plannedSeconds.available);
	QVERIFY(!cDeparture.delaySeconds.available);
	QCOMPARE(cDeparture.delayClass, DelayClass::Unavailable);
	// events: late arrival
	QCOMPARE(f2BArrival.trainId, "F2-1");
	QVERIFY(isValue(f2BArrival.delaySeconds, 157.0));
	QCOMPARE(f2BArrival.delayClass, DelayClass::Late);
}

// H1: follow-level-0, arrivals. The arrival delays are F1 -269 at B and -167 at C, F2 +157 at B and -10 at C.
// A has no arrival to plan or to simulate.
// B: 2 available, sum -112, mean -56, minimum -269, maximum 157, 1 early and 1 late, 1 of 2 later than 0 and than 60 s (50 %),
// none later than 180 or 300 s.
// C: sum -177, mean -88.5, minimum -167, maximum -10, 2 early, none later than 0 s.
// Network: 4 available and 2 without a plan or an arrival, sum -289, mean -72.25, minimum -269, maximum 157, 3 early, 1 late,
// 1 of 4 later than 0 and than 60 s (25 %). The signed statistics file of the golden shows the same sums, means and shares.
void TestDelayAnalysis::summarizesArrivalsOfFollowLevel0() {
	const std::vector<DelayEvent> events = buildDelayEvents(lineRows({331, 660, 933, 934, 817, 877, 1150, 1151}), lineRun());
	DelayFilter filter;
	filter.kind = DelayEventKind::Arrival;
	const DelaySummary summary = summarizeDelays(events, filter);

	// arrival summary: two events for each row, stations in order of appearance
	QCOMPARE(events.size(), std::size_t(12));
	QCOMPARE(summary.stations.size(), std::size_t(3));
	QCOMPARE(summary.stations[0].stationId, "A");
	QCOMPARE(summary.stations[1].stationId, "B");
	QCOMPARE(summary.stations[2].stationId, "C");
	const DelayStatistics& a = summary.stations[0].statistics;
	const DelayStatistics& b = summary.stations[1].statistics;
	const DelayStatistics& c = summary.stations[2].statistics;
	// arrival summary: A has no figure and no share, not 0
	QVERIFY(hasCounts(a, {2, 0, 0, 0, 2, 0, 0, 0}));
	QVERIFY(hasNoFigures(a));
	QVERIFY(hasShares(a, 0, {{0, kNone}, {0, kNone}, {0, kNone}, {0, kNone}}));
	// arrival summary: station B
	QVERIFY(hasCounts(b, {2, 2, 0, 0, 0, 1, 0, 1}));
	QVERIFY(hasFigures(b, -112.0, -56.0, -269.0, 157.0));
	QVERIFY(hasShares(b, 2, {{1, 50.0}, {1, 50.0}, {0, 0.0}, {0, 0.0}}));
	// arrival summary: station C
	QVERIFY(hasCounts(c, {2, 2, 0, 0, 0, 2, 0, 0}));
	QVERIFY(hasFigures(c, -177.0, -88.5, -167.0, -10.0));
	QVERIFY(hasShares(c, 2, {{0, 0.0}, {0, 0.0}, {0, 0.0}, {0, 0.0}}));
	// arrival summary: network
	QVERIFY(hasCounts(summary.network, {6, 4, 0, 0, 2, 3, 0, 1}));
	QVERIFY(hasFigures(summary.network, -289.0, -72.25, -269.0, 157.0));
	QVERIFY(hasShares(summary.network, 4, {{1, 25.0}, {1, 25.0}, {0, 0.0}, {0, 0.0}}));
}

// H1 for the departures of follow-level-0. The departure delays are F1 0 at B and F2 +157 at B. At A the plan has a
// departure and the trains have none (not reached); at C there is a departure and no plan.
// B: sum 157, mean 78.5, minimum 0, maximum 157, 1 on time and 1 late, 1 of 2 later than 0 and than 60 s.
// Network: 2 available, 2 not reached, 2 without a plan.
void TestDelayAnalysis::summarizesDeparturesOfFollowLevel0() {
	const std::vector<DelayEvent> events = buildDelayEvents(lineRows({331, 660, 933, 934, 817, 877, 1150, 1151}), lineRun());
	DelayFilter filter;
	filter.kind = DelayEventKind::Departure;
	const DelaySummary summary = summarizeDelays(events, filter);
	const StationDelaySummary* a = stationOf(summary, "A");
	const StationDelaySummary* b = stationOf(summary, "B");
	const StationDelaySummary* c = stationOf(summary, "C");
	if (a == nullptr || b == nullptr || c == nullptr)
		QFAIL("departure summary: three stations");
	// departure summary: A is not reached
	QVERIFY(hasCounts(a->statistics, {2, 0, 0, 2, 0, 0, 0, 0}));
	QVERIFY(hasNoFigures(a->statistics));
	// departure summary: B has a zero delay and a late one
	QVERIFY(hasCounts(b->statistics, {2, 2, 0, 0, 0, 0, 1, 1}));
	QVERIFY(hasFigures(b->statistics, 157.0, 78.5, 0.0, 157.0));
	QVERIFY(hasShares(b->statistics, 2, {{1, 50.0}, {1, 50.0}, {0, 0.0}, {0, 0.0}}));
	// departure summary: C has no plan
	QVERIFY(hasCounts(c->statistics, {2, 0, 2, 0, 0, 0, 0, 0}));
	QVERIFY(hasNoFigures(c->statistics));
	// departure summary: network
	QVERIFY(hasCounts(summary.network, {6, 2, 2, 2, 0, 0, 1, 1}));
	QVERIFY(hasFigures(summary.network, 157.0, 78.5, 0.0, 157.0));
}

// The filter picks occurrences and stations. F2 at B and C: delays +157 and -10, sum 147, mean 73.5, 1 of 2 later than 0 and than 60 s.
// F1 at A: one call with no figure at all, so the mean does not exist and the share has no denominator.
void TestDelayAnalysis::filtersOccurrencesAndStations() {
	const std::vector<DelayEvent> events = buildDelayEvents(lineRows({331, 660, 933, 934, 817, 877, 1150, 1151}), lineRun());
	DelayFilter f2;
	f2.occurrences = {{"F2", 1}};
	f2.stations = {"B", "C"};
	const DelaySummary summary = summarizeDelays(events, f2);
	// filter: two stations
	QCOMPARE(summary.stations.size(), std::size_t(2));
	QCOMPARE(summary.stations[0].stationId, "B");
	QCOMPARE(summary.stations[1].stationId, "C");
	// filter: F2 at B and C
	QVERIFY(hasCounts(summary.network, {2, 2, 0, 0, 0, 1, 0, 1}));
	QVERIFY(hasFigures(summary.network, 147.0, 73.5, -10.0, 157.0));
	QVERIFY(hasShares(summary.network, 2, {{1, 50.0}, {1, 50.0}, {0, 0.0}, {0, 0.0}}));

	DelayFilter f1AtA;
	f1AtA.occurrences = {{"F1", 1}};
	f1AtA.stations = {"A"};
	const DelaySummary unavailable = summarizeDelays(events, f1AtA);
	// filter: nothing available gives no mean and no share
	QVERIFY(hasCounts(unavailable.network, {1, 0, 0, 0, 1, 0, 0, 0}));
	QVERIFY(hasNoFigures(unavailable.network));
	QVERIFY(hasShares(unavailable.network, 0, {{0, kNone}, {0, kNone}, {0, kNone}, {0, kNone}}));

	DelayFilter nobody;
	nobody.occurrences = {{"F9", 1}};
	const DelaySummary empty = summarizeDelays(events, nobody);
	// filter: no event selected
	QVERIFY(empty.stations.empty());
	QVERIFY(hasCounts(empty.network, {0, 0, 0, 0, 0, 0, 0, 0}));
	QVERIFY(hasNoFigures(empty.network));
	const DelaySummary noEvents = summarizeDelays({}, DelayFilter());
	// filter: no events
	QVERIFY(noEvents.stations.empty());
	QCOMPARE(noEvents.network.calls, 0);
	QVERIFY(hasNoFigures(noEvents.network));
}

// Arrivals 0, 60, 180, 300 and 301 s late at five stations. A call is later than a threshold only when its delay is
// strictly greater: later than 0 s: 4 of 5 (80 %); than 60 s: 3 (60 %); than 180 s: 2 (40 %); than 300 s: 1 (20 %).
// The station with 60 s is later than 0 s (1 of 1) and not later than 60 s.
void TestDelayAnalysis::countsStrictlyLaterThanThresholds() {
	std::vector<TimetableResultRow> rows;
	const char* stations[5] = {"S0", "S60", "S180", "S300", "S301"};
	const double delays[5] = {0.0, 60.0, 180.0, 300.0, 301.0};
	for (int index = 0; index < 5; ++index)
		rows.push_back(row("Z", 1, stations[index], index + 1, 1, {1000.0, kNone, 1000.0 + delays[index], kNone}));
	const std::vector<DelayEvent> events = buildDelayEvents(rows, RunResults());
	const DelaySummary summary = summarizeDelays(events, DelayFilter());
	// thresholds: strictly later
	QVERIFY(hasCounts(summary.network, {5, 5, 0, 0, 0, 0, 1, 4}));
	QVERIFY(hasFigures(summary.network, 841.0, 168.2, 0.0, 301.0));
	QVERIFY(hasShares(summary.network, 5, {{4, 80.0}, {3, 60.0}, {2, 40.0}, {1, 20.0}}));
	const StationDelaySummary* s60 = stationOf(summary, "S60");
	// thresholds: 60 s is not later than 60 s
	if (s60 == nullptr)
		QFAIL("thresholds: 60 s is not later than 60 s");
	QVERIFY(hasShares(s60->statistics, 1, {{1, 100.0}, {0, 0.0}, {0, 0.0}, {0, 0.0}}));

	const DelaySummary custom = summarizeDelays(events, DelayFilter(), {std::numeric_limits<double>::quiet_NaN(), 100.0, -1.0});
	// thresholds: a threshold that is not finite is left out, the others keep their order
	QCOMPARE(custom.network.shares.size(), std::size_t(2));
	QVERIFY(near(custom.network.shares[0].thresholdSeconds, 100.0));
	QCOMPARE(custom.network.shares[0].later, 3);
	QVERIFY(near(custom.network.shares[1].thresholdSeconds, -1.0));
	QCOMPARE(custom.network.shares[1].later, 5);
}

// Arrival delays -30, 0, 0, 1, 60, 61, 180, 300, 300.5 and 1000 s. A delay on a threshold is in the class below it:
// 60 is up to 1 min late, 180 is 1 to 3 min late and 300 is 3 to 5 min late, as it is not later than that threshold.
// Early 1 (10 %), on time 2 (20 %), up to 1 min 2 (20 %), 1 to 3 min 2 (20 %), 3 to 5 min 1 (10 %), more than 5 min
// 2 (20 %). One arrival has no plan, two are not reached and three have neither; they are in no class.
void TestDelayAnalysis::classifiesDelays() {
	std::vector<TimetableResultRow> rows;
	const double delays[10] = {-30.0, 0.0, 0.0, 1.0, 60.0, 61.0, 180.0, 300.0, 300.5, 1000.0};
	for (int index = 0; index < 10; ++index)
		rows.push_back(row("Z", 1, "S", index + 1, index + 1, {1000.0, kNone, 1000.0 + delays[index], kNone}));
	rows.push_back(row("Z", 1, "S", 11, 11, {kNone, kNone, 1000.0, kNone}));
	for (int index = 12; index < 14; ++index)
		rows.push_back(row("Z", 1, "S", index, index, {1000.0, kNone, kNone, kNone}));
	for (int index = 14; index < 17; ++index)
		rows.push_back(row("Z", 1, "S", index, index, {kNone, kNone, kNone, kNone}));
	const std::vector<DelayEvent> arrivals = ofKind(buildDelayEvents(rows, RunResults()), DelayEventKind::Arrival);
	const DelayHistogram histogram = delayHistogram(arrivals);
	QVERIFY2(hasClasses(histogram,
				 {{"Early", kNone, 0.0, 1, 10.0}, {"On time", 0.0, 0.0, 2, 20.0}, {"Up to 1 min late", 0.0, 60.0, 2, 20.0},
					 {"1 to 3 min late", 60.0, 180.0, 2, 20.0}, {"3 to 5 min late", 180.0, 300.0, 1, 10.0},
					 {"Over 5 min late", 300.0, kNone, 2, 20.0}}),
		"histogram: the classes, their bounds, counts and shares, with a delay on a threshold in the class below it");
	// histogram: the events without a delay are counted by status and are in no class
	QCOMPARE(histogram.available, 10);
	QCOMPARE(histogram.noPlan, 1);
	QCOMPARE(histogram.notReached, 2);
	QCOMPARE(histogram.noPlanAndNotReached, 3);
	DelayFilter filter;
	QVERIFY2(agreesWithSummary(histogram, summarizeDelays(arrivals, filter).network),
		"histogram: the classes above each threshold are the events later than it in the summary");
}

// The arrivals of follow-level-0: -269, -167, +157 and -10, and two without a plan or an arrival at A. Early 3 (75 %) and
// 1 to 3 min late 1 (25 %). The departures: 0 and +157 at B, two not reached at A and two without a plan at C. Without an
// available event every share is unavailable, not 0.
void TestDelayAnalysis::classifiesFollowLevel0() {
	const std::vector<DelayEvent> events = buildDelayEvents(lineRows({331, 660, 933, 934, 817, 877, 1150, 1151}), lineRun());
	const DelayHistogram arrivals = delayHistogram(ofKind(events, DelayEventKind::Arrival));
	// histogram: the arrivals of follow-level-0
	QVERIFY(hasClasses(arrivals,
		{{"Early", kNone, 0.0, 3, 75.0}, {"On time", 0.0, 0.0, 0, 0.0}, {"Up to 1 min late", 0.0, 60.0, 0, 0.0},
			{"1 to 3 min late", 60.0, 180.0, 1, 25.0}, {"3 to 5 min late", 180.0, 300.0, 0, 0.0},
			{"Over 5 min late", 300.0, kNone, 0, 0.0}}));
	QCOMPARE(arrivals.available, 4);
	QCOMPARE(arrivals.noPlan, 0);
	QCOMPARE(arrivals.notReached, 0);
	QCOMPARE(arrivals.noPlanAndNotReached, 2);
	DelayFilter arrivalFilter;
	QVERIFY2(agreesWithSummary(arrivals, summarizeDelays(events, arrivalFilter).network), "histogram: the arrivals agree with the summary");

	const DelayHistogram departures = delayHistogram(ofKind(events, DelayEventKind::Departure));
	// histogram: the departures of follow-level-0
	QVERIFY(hasClasses(departures,
		{{"Early", kNone, 0.0, 0, 0.0}, {"On time", 0.0, 0.0, 1, 50.0}, {"Up to 1 min late", 0.0, 60.0, 0, 0.0},
			{"1 to 3 min late", 60.0, 180.0, 1, 50.0}, {"3 to 5 min late", 180.0, 300.0, 0, 0.0},
			{"Over 5 min late", 300.0, kNone, 0, 0.0}}));
	QCOMPARE(departures.available, 2);
	QCOMPARE(departures.noPlan, 2);
	QCOMPARE(departures.notReached, 2);
	QCOMPARE(departures.noPlanAndNotReached, 0);
	DelayFilter departureFilter;
	departureFilter.kind = DelayEventKind::Departure;
	QVERIFY2(agreesWithSummary(departures, summarizeDelays(events, departureFilter).network),
		"histogram: the departures agree with the summary");

	DelayFilter atA;
	atA.stations = {"A"};
	std::vector<DelayEvent> eventsAtA;
	for (const DelayEvent& event : ofKind(events, DelayEventKind::Arrival)) {
		if (event.stationId == "A")
			eventsAtA.push_back(event);
	}
	const DelayHistogram unavailable = delayHistogram(eventsAtA);
	// histogram: without an available event the classes are empty and their shares unavailable
	QVERIFY(hasClasses(unavailable,
		{{"Early", kNone, 0.0, 0, kNone}, {"On time", 0.0, 0.0, 0, kNone}, {"Up to 1 min late", 0.0, 60.0, 0, kNone},
			{"1 to 3 min late", 60.0, 180.0, 0, kNone}, {"3 to 5 min late", 180.0, 300.0, 0, kNone},
			{"Over 5 min late", 300.0, kNone, 0, kNone}}));
	QCOMPARE(unavailable.available, 0);
	QCOMPARE(unavailable.noPlanAndNotReached, 2);
	QVERIFY(agreesWithSummary(unavailable, summarizeDelays(events, atA).network));

	const DelayHistogram empty = delayHistogram({});
	// histogram: no event gives the six classes without a count or a share
	QVERIFY(hasClasses(empty,
		{{"Early", kNone, 0.0, 0, kNone}, {"On time", 0.0, 0.0, 0, kNone}, {"Up to 1 min late", 0.0, 60.0, 0, kNone},
			{"1 to 3 min late", 60.0, 180.0, 0, kNone}, {"3 to 5 min late", 180.0, 300.0, 0, kNone},
			{"Over 5 min late", 300.0, kNone, 0, kNone}}));
	QCOMPARE(empty.available, 0);
	QCOMPARE(empty.noPlan, 0);
	QCOMPARE(empty.notReached, 0);
	QCOMPARE(empty.noPlanAndNotReached, 0);
}

// Early, on time, no plan, not reached, repeated station. Train T calls at S, U, S again and W.
// S first call: arrival 90 for 100 (-10), departure 200 for 200 (0). U: arrival 300 with no plan, departure 420 for 400 (+20).
// S second call: planned 500 and 600, the train never got there. W: no plan, no time.
// Arrivals at S: 2 calls, 1 available (-10, early), 1 not reached. The mean is over 1 and not over 2.
void TestDelayAnalysis::countsEarlyZeroMissingAndRepeatedCalls() {
	const std::vector<TimetableResultRow> rows = {
		row("T", 1, "S", 1, 1, {100.0, 200.0, 90.0, 200.0}),
		row("T", 1, "U", 2, 1, {kNone, 400.0, 300.0, 420.0}),
		row("T", 1, "S", 3, 2, {500.0, 600.0, kNone, kNone}),
		row("T", 1, "W", 4, 1, {kNone, kNone, kNone, kNone}),
	};
	const std::vector<DelayEvent> events = buildDelayEvents(rows, RunResults());
	DelayFilter arrivals;
	const DelaySummary arrival = summarizeDelays(events, arrivals);
	const StationDelaySummary* s = stationOf(arrival, "S");
	// repeated station: both calls are the same station and the mean has the available calls as denominator
	QCOMPARE(arrival.stations.size(), std::size_t(3));
	if (s == nullptr)
		QFAIL("repeated station: both calls are the same station and the mean has the available calls as denominator");
	QVERIFY(hasCounts(s->statistics, {2, 1, 0, 1, 0, 1, 0, 0}));
	QVERIFY(hasFigures(s->statistics, -10.0, -10.0, -10.0, -10.0));
	QVERIFY(hasShares(s->statistics, 1, {{0, 0.0}, {0, 0.0}, {0, 0.0}, {0, 0.0}}));
	QVERIFY2(hasCounts(arrival.network, {4, 1, 1, 1, 1, 1, 0, 0}), "repeated station: one event of each status");
	// repeated station: the second call keeps its call index and is not reached
	QCOMPARE(events[4].callIndex, 2);
	QCOMPARE(events[4].journeyIndex, 3);
	QCOMPARE(events[4].status, TimetableValueStatus::NotReached);
	QCOMPARE(events[4].delayClass, DelayClass::Unavailable);
	// early, on time and late events
	QCOMPARE(events[0].delayClass, DelayClass::Early);
	QCOMPARE(events[1].delayClass, DelayClass::OnTime);
	QCOMPARE(events[3].delayClass, DelayClass::Late);
	QVERIFY(isValue(events[3].delaySeconds, 20.0));
	DelayFilter departures;
	departures.kind = DelayEventKind::Departure;
	const DelaySummary departure = summarizeDelays(events, departures);
	// repeated station: departures
	QVERIFY(hasCounts(departure.network, {4, 2, 0, 1, 1, 0, 1, 1}));
	QVERIFY(hasFigures(departure.network, 20.0, 10.0, 0.0, 20.0));
}

// P, Q and R of train X. An entrance delay of 120 s at P moves the planned departure of P from 100 to 220. The
// events measure against the authored 100, so the departure at P is +120. Q: arrival 570 for 500 (+70), departure
// 680 for 560 (+120). R: arrival 1090 for 1000 (+90).
void TestDelayAnalysis::measuresEntranceDelayAgainstAuthoredPlan() {
	const std::vector<TimetableResultRow> rows = {
		row("X", 1, "P", 1, 1, {kNone, 220.0, kNone, 220.0}),
		row("X", 1, "Q", 2, 1, {500.0, 560.0, 570.0, 680.0}),
		row("X", 1, "R", 3, 1, {1000.0, kNone, 1090.0, 1091.0}),
	};
	RunResults run;
	run.trains.push_back(trainResult("X", 1, 120.0, "P"));
	const std::vector<DelayEvent> events = buildDelayEvents(rows, run);
	// entrance delay: the departure at the moved stop is measured against the authored plan
	QCOMPARE(events.size(), std::size_t(6));
	QVERIFY(isValue(events[1].plannedSeconds, 100.0));
	QVERIFY(isValue(events[1].delaySeconds, 120.0));
	QCOMPARE(events[1].delayClass, DelayClass::Late);
	// entrance delay: the other events are unchanged
	QVERIFY(isValue(events[2].delaySeconds, 70.0));
	QVERIFY(isValue(events[3].delaySeconds, 120.0));
	QVERIFY(isValue(events[4].delaySeconds, 90.0));
	QVERIFY(isValue(events[3].plannedSeconds, 560.0));

	const std::vector<DelayEvent> unknown = buildDelayEvents(rows, RunResults());
	// entrance delay: a run that does not list the occurrence leaves the planned time as it is
	QVERIFY(isValue(unknown[1].plannedSeconds, 220.0));
	QVERIFY(isValue(unknown[1].delaySeconds, 0.0));

	// The delay is taken back at the first call of the station only.
	const std::vector<TimetableResultRow> repeated = {
		row("X", 1, "P", 1, 1, {kNone, 220.0, kNone, 220.0}),
		row("X", 1, "Q", 2, 1, {500.0, 560.0, 500.0, 560.0}),
		row("X", 1, "P", 3, 2, {700.0, 760.0, 700.0, 760.0}),
	};
	const std::vector<DelayEvent> second = buildDelayEvents(repeated, run);
	// entrance delay: only the first call of the station is moved back
	QVERIFY(isValue(second[1].plannedSeconds, 100.0));
	QVERIFY(isValue(second[5].plannedSeconds, 760.0));
	QVERIFY(isValue(second[5].delaySeconds, 0.0));
}

// H2, no signalling level. Baseline follow-level-none, incident run sf-forward-level-none; both trains have direct evidence.
// F1 arrives at C at 933 and 1218: +285. F2 at 993 and 1218: +225. At B both trains are unchanged (331 and 391).
// Total 510 for both the positive and the net sum.
void TestDelayAnalysis::comparesPairWithoutSignalling() {
	const DelayRunSnapshot baseline = lineSnapshot("baseline", {331, 660, 933, 934, 391, 720, 993, 994});
	const DelayRunSnapshot incident = lineSnapshot("signal-failure-forward", {331, 660, 1218, 1219, 391, 720, 1218, 1219}, {"inc.fwd"}, {"inc.fwd"});
	const DelayBaselineComparison result = compareAgainstBaseline(baseline, incident);
	// pair none: compatible, two occurrences, six calls
	QVERIFY(result.compatible);
	QVERIFY(result.diagnostics.empty());
	QCOMPARE(result.occurrences.size(), std::size_t(2));
	QCOMPARE(result.calls.size(), std::size_t(6));
	const BaselineOccurrence* f1 = occurrenceOf(result, "F1");
	const BaselineOccurrence* f2 = occurrenceOf(result, "F2");
	if (f1 == nullptr || f2 == nullptr)
		QFAIL("pair none: both occurrences");
	// pair none: evidence and final arrivals
	QVERIFY(f1->hasDirectEvidence);
	QCOMPARE(f1->incidentIds, std::vector<std::string>{"inc.fwd"});
	QVERIFY(f2->hasDirectEvidence);
	QCOMPARE(f1->finalStationId, "C");
	QCOMPARE(f1->finalJourneyIndex, 3);
	QCOMPARE(f1->finalCallIndex, 1);
	QVERIFY(isDifference(f1->finalArrival, DelayChange::Later, 285.0));
	QVERIFY(isDifference(f2->finalArrival, DelayChange::Later, 225.0));
	// pair none: no input delay, so the secondary delay is the additional delay
	QVERIFY(isValue(f1->finalArrival.baselineSeconds, 933.0));
	QVERIFY(isValue(f1->finalArrival.scenarioSeconds, 1218.0));
	QVERIFY(near(f1->inputDelaySeconds, 0.0));
	QVERIFY(f1->inputDelayStationId.empty());
	QVERIFY(isValue(f1->secondarySeconds, 285.0));
	const BaselineCall* f1B = callOf(result, "F1", "B");
	const BaselineCall* f2B = callOf(result, "F2", "B");
	const BaselineCall* f2C = callOf(result, "F2", "C");
	const BaselineCall* f1A = callOf(result, "F1", "A");
	if (f1B == nullptr || f2B == nullptr || f2C == nullptr || f1A == nullptr)
		QFAIL("pair none: calls");
	// pair none: unchanged calls are Same, not unavailable
	QVERIFY(isDifference(f1B->arrival, DelayChange::Same, 0.0));
	QVERIFY(isDifference(f1B->departure, DelayChange::Same, 0.0));
	QVERIFY(isDifference(f2B->arrival, DelayChange::Same, 0.0));
	QVERIFY(!f1B->finalCall);
	QVERIFY(f2C->finalCall);
	// pair none: F2 at C
	QVERIFY(isDifference(f2C->arrival, DelayChange::Later, 225.0));
	QVERIFY(isDifference(f2C->departure, DelayChange::Later, 225.0));
	// pair none: A has no time in either run
	QVERIFY(isUnavailableDifference(f1A->arrival));
	QVERIFY(isUnavailableDifference(f1A->departure));
	// pair none: totals
	QCOMPARE(result.totals.occurrences, 2);
	QCOMPARE(result.totals.later, 2);
	QCOMPARE(result.totals.same, 0);
	QCOMPARE(result.totals.earlier, 0);
	QCOMPARE(result.totals.notAvailable, 0);
	QVERIFY(isValue(result.totals.positiveSeconds, 510.0));
	QVERIFY(isValue(result.totals.netSeconds, 510.0));
	QVERIFY(result.totals.complete);
}

// H2, level 3. follow-level-3 against sf-forward-level-3: only F1 has direct evidence.
// F1 at C: 933 to 1218, +285. F2 at C: 1021 to 1249, +228 (departure 1022 to 1250), without evidence. At B both are unchanged
// (331, 688). Positive and net total 513.
void TestDelayAnalysis::comparesPairAtLevel3() {
	const DelayRunSnapshot baseline = lineSnapshot("baseline", {331, 660, 933, 934, 688, 748, 1021, 1022});
	const DelayRunSnapshot incident = lineSnapshot("signal-failure-forward", {331, 660, 1218, 1219, 688, 748, 1249, 1250}, {"inc.fwd"});
	const DelayBaselineComparison result = compareAgainstBaseline(baseline, incident);
	const BaselineOccurrence* f1 = occurrenceOf(result, "F1");
	const BaselineOccurrence* f2 = occurrenceOf(result, "F2");
	if (!result.compatible || f1 == nullptr || f2 == nullptr)
		QFAIL("pair 3: compatible with two occurrences");
	// pair 3: only F1 has evidence
	QVERIFY(f1->hasDirectEvidence);
	QVERIFY(!f2->hasDirectEvidence);
	QVERIFY(f2->incidentIds.empty());
	// pair 3: both trains are later at C, with or without evidence
	QVERIFY(isDifference(f1->finalArrival, DelayChange::Later, 285.0));
	QVERIFY(isDifference(f2->finalArrival, DelayChange::Later, 228.0));
	const BaselineCall* f2B = callOf(result, "F2", "B");
	// pair 3: F2 at B is unchanged
	if (f2B == nullptr)
		QFAIL("pair 3: F2 at B is unchanged");
	QVERIFY(isDifference(f2B->arrival, DelayChange::Same, 0.0));
	QVERIFY(isDifference(f2B->departure, DelayChange::Same, 0.0));
	// pair 3: totals
	QVERIFY(isValue(result.totals.positiveSeconds, 513.0));
	QVERIFY(isValue(result.totals.netSeconds, 513.0));
	QVERIFY(result.totals.complete);
}

// H2, level 0. follow-level-0 against sf-forward-level-0: F1 has direct evidence, F2 has none.
// F1 at C: 933 to 1218, +285. F2 at B: arrival 817 to 866 (+49), departure 877 to 1013 (+136); at C 1150 to 1373 (+223).
// Positive and net total 508.
void TestDelayAnalysis::comparesPairAtLevel0() {
	const DelayRunSnapshot baseline = lineSnapshot("baseline", {331, 660, 933, 934, 817, 877, 1150, 1151});
	const DelayRunSnapshot incident = lineSnapshot("signal-failure-forward", {331, 660, 1218, 1219, 866, 1013, 1373, 1374}, {"inc.fwd"});
	const DelayBaselineComparison result = compareAgainstBaseline(baseline, incident);
	const BaselineOccurrence* f1 = occurrenceOf(result, "F1");
	const BaselineOccurrence* f2 = occurrenceOf(result, "F2");
	if (!result.compatible || f1 == nullptr || f2 == nullptr)
		QFAIL("pair 0: compatible with two occurrences");
	const BaselineCall* f2B = callOf(result, "F2", "B");
	// pair 0: final arrivals and evidence
	QVERIFY(f1->hasDirectEvidence);
	QVERIFY(!f2->hasDirectEvidence);
	QVERIFY(isDifference(f1->finalArrival, DelayChange::Later, 285.0));
	QVERIFY(isDifference(f2->finalArrival, DelayChange::Later, 223.0));
	// pair 0: F2 at B
	if (f2B == nullptr)
		QFAIL("pair 0: F2 at B");
	QVERIFY(isDifference(f2B->arrival, DelayChange::Later, 49.0));
	QVERIFY(isDifference(f2B->departure, DelayChange::Later, 136.0));
	// pair 0: totals
	QVERIFY(isValue(result.totals.positiveSeconds, 508.0));
	QVERIFY(isValue(result.totals.netSeconds, 508.0));
	QCOMPARE(result.totals.later, 2);
}

// H3, level 5: F2 never arrives at C in the incident run (sf-forward-level-5). Baseline follow-level-5.
// F1: arrival at C 933 to 1274 (+341), departure at B 660 to 1001 (+341). F2: arrival at B 897 to 1238 (+341), departure at C
// not available in the incident run. F2's final arrival has no difference. Of the totals only F1 counts: 341 for both sums,
// and the total is flagged incomplete.
void TestDelayAnalysis::comparesPairWithMissingCall() {
	const DelayRunSnapshot baseline = lineSnapshot("baseline", {331, 660, 933, 934, 897, 957, 1230, 1231});
	const DelayRunSnapshot incident = lineSnapshot("signal-failure-forward", {331, 1001, 1274, 1275, 1238, 1298, kNone, kNone});
	const DelayBaselineComparison result = compareAgainstBaseline(baseline, incident);
	const BaselineOccurrence* f1 = occurrenceOf(result, "F1");
	const BaselineOccurrence* f2 = occurrenceOf(result, "F2");
	if (!result.compatible || f1 == nullptr || f2 == nullptr)
		QFAIL("pair 5: compatible with two occurrences");
	const BaselineCall* f1B = callOf(result, "F1", "B");
	const BaselineCall* f2C = callOf(result, "F2", "C");
	// pair 5: F1
	if (f1B == nullptr)
		QFAIL("pair 5: F1");
	QVERIFY(isDifference(f1B->arrival, DelayChange::Same, 0.0));
	QVERIFY(isDifference(f1B->departure, DelayChange::Later, 341.0));
	QVERIFY(isDifference(f1->finalArrival, DelayChange::Later, 341.0));
	// pair 5: a missing arrival is not available and not 0
	if (f2C == nullptr)
		QFAIL("pair 5: a missing arrival is not available and not 0");
	QVERIFY(f2C->finalCall);
	QVERIFY(isUnavailableDifference(f2C->arrival));
	QVERIFY(isUnavailableDifference(f2C->departure));
	QVERIFY(isValue(f2C->arrival.baselineSeconds, 1230.0));
	QVERIFY(!f2C->arrival.scenarioSeconds.available);
	// pair 5: no secondary delay without a final arrival
	QVERIFY(isUnavailableDifference(f2->finalArrival));
	QVERIFY(!f2->secondarySeconds.available);
	// pair 5: the total is incomplete
	QCOMPARE(result.totals.occurrences, 2);
	QCOMPARE(result.totals.later, 1);
	QCOMPARE(result.totals.notAvailable, 1);
	QVERIFY(!result.totals.complete);
	QVERIFY(isValue(result.totals.positiveSeconds, 341.0));
	QVERIFY(isValue(result.totals.netSeconds, 341.0));
}

// An entrance delay of 120 s at P for train X (P, Q, R). Baseline: P departs 100, Q arrives 450 and departs 560, R arrives 950
// and departs 951. Scenario: P 220, Q 570 and 680, R 1090 and 1091.
// Additional delays: P departure +120, Q +120 and +120, R +140 and +140. P has no arrival.
// Input delay 120 at P. Secondary delay at the final arrival: 140 - 120 = 20.
// A second train with its delay at the final stop: the moved stop is not before the final call, so nothing is taken off.
void TestDelayAnalysis::separatesInputDelayFromSecondaryDelay() {
	const std::vector<TimetableResultRow> baselineRows = {
		row("X", 1, "P", 1, 1, {kNone, 100.0, kNone, 100.0}),
		row("X", 1, "Q", 2, 1, {500.0, 560.0, 450.0, 560.0}),
		row("X", 1, "R", 3, 1, {1000.0, kNone, 950.0, 951.0}),
	};
	const std::vector<TimetableResultRow> scenarioRows = {
		row("X", 1, "P", 1, 1, {kNone, 220.0, kNone, 220.0}),
		row("X", 1, "Q", 2, 1, {500.0, 560.0, 570.0, 680.0}),
		row("X", 1, "R", 3, 1, {1000.0, kNone, 1090.0, 1091.0}),
	};
	DelayRunSnapshot baseline = snapshot("baseline", baselineRows, {trainResult("X", 1)});
	DelayRunSnapshot scenario = snapshot("entrance-delay", scenarioRows, {trainResult("X", 1, 120.0, "P")});
	scenario.hasEntranceDelays = true;
	const DelayBaselineComparison result = compareAgainstBaseline(baseline, scenario);
	const BaselineOccurrence* x = occurrenceOf(result, "X");
	if (!result.compatible || x == nullptr)
		QFAIL("entrance delay pair: compatible");
	// entrance delay pair: the input delay is the primary delay and there is no incident evidence
	QVERIFY(near(x->inputDelaySeconds, 120.0));
	QCOMPARE(x->inputDelayStationId, "P");
	QVERIFY(!x->hasDirectEvidence);
	QVERIFY(x->incidentIds.empty());
	// entrance delay pair: secondary delay is the additional delay minus the input delay
	QVERIFY(isDifference(x->finalArrival, DelayChange::Later, 140.0));
	QVERIFY(isValue(x->secondarySeconds, 20.0));
	const BaselineCall* p = callOf(result, "X", "P");
	const BaselineCall* q = callOf(result, "X", "Q");
	// entrance delay pair: calls
	if (p == nullptr)
		QFAIL("entrance delay pair: calls");
	QVERIFY(isUnavailableDifference(p->arrival));
	QVERIFY(isDifference(p->departure, DelayChange::Later, 120.0));
	if (q == nullptr)
		QFAIL("entrance delay pair: calls");
	QVERIFY(isDifference(q->arrival, DelayChange::Later, 120.0));
	QVERIFY(isDifference(q->departure, DelayChange::Later, 120.0));
	// entrance delay pair: totals
	QVERIFY(isValue(result.totals.positiveSeconds, 140.0));
	QVERIFY(isValue(result.totals.netSeconds, 140.0));

	// Delay of 60 s at the final stop R: R arrival 450 to 460 (+10), departure 560 to 620.
	const std::vector<TimetableResultRow> lastBaseline = {
		row("Y", 1, "P", 1, 1, {kNone, 100.0, kNone, 100.0}),
		row("Y", 1, "R", 2, 1, {500.0, 560.0, 450.0, 560.0}),
	};
	const std::vector<TimetableResultRow> lastScenario = {
		row("Y", 1, "P", 1, 1, {kNone, 100.0, kNone, 100.0}),
		row("Y", 1, "R", 2, 1, {500.0, 620.0, 460.0, 620.0}),
	};
	const DelayBaselineComparison last = compareAgainstBaseline(snapshot("baseline", lastBaseline, {trainResult("Y", 1)}),
		snapshot("entrance-delay", lastScenario, {trainResult("Y", 1, 60.0, "R")}));
	const BaselineOccurrence* y = occurrenceOf(last, "Y");
	// entrance delay pair: a delay at the final stop does not explain the final arrival
	QVERIFY(last.compatible);
	if (y == nullptr)
		QFAIL("entrance delay pair: a delay at the final stop does not explain the final arrival");
	QVERIFY(isDifference(y->finalArrival, DelayChange::Later, 10.0));
	QVERIFY(isValue(y->secondarySeconds, 10.0));
}

// Every reason is reported, not only the first. The scenario id repeats; the revision, base time, duration and timestep differ;
// the baseline has incidents and entrance delays. Y is only in the baseline, W only in the scenario, Z has other stations.
// Occurrences of the baseline that the scenario lacks come first, then the scenario's in its own order.
void TestDelayAnalysis::reportsEveryIncompatibility() {
	const CallTimes departureAtP = {kNone, 100.0, kNone, 100.0};
	const CallTimes arrivalAtLast = {200.0, kNone, 200.0, kNone};
	DelayRunSnapshot baseline = snapshot("run",
		{row("X", 1, "P", 1, 1, departureAtP), row("Y", 1, "P", 1, 1, departureAtP), row("Z", 1, "P", 1, 1, departureAtP),
			row("Z", 1, "Q", 2, 1, arrivalAtLast)},
		{trainResult("X", 1), trainResult("Y", 1), trainResult("Z", 1)});
	DelayRunSnapshot scenario = snapshot("run",
		{row("X", 1, "P", 1, 1, departureAtP), row("Z", 1, "P", 1, 1, departureAtP), row("Z", 1, "R", 2, 1, arrivalAtLast),
			row("W", 1, "P", 1, 1, departureAtP)},
		{trainResult("X", 1), trainResult("Z", 1), trainResult("W", 1)});
	scenario.caseRevision = "rev-2";
	scenario.baseTimeSeconds = 60.0;
	scenario.durationSeconds = 1400.0;
	scenario.timestep = 2.0;
	baseline.hasIncidents = true;
	baseline.hasEntranceDelays = true;
	const DelayBaselineComparison result = compareAgainstBaseline(baseline, scenario);
	// incompatible: no rows
	QVERIFY(!result.compatible);
	QVERIFY(result.occurrences.empty());
	QVERIFY(result.calls.empty());
	QVERIFY2(onlyDiagnostics(result,
				 {DelayDiagnosticKind::ScenarioIdsEqual, DelayDiagnosticKind::SceneRevisionDiffers, DelayDiagnosticKind::BaseTimeDiffers,
					 DelayDiagnosticKind::DurationDiffers, DelayDiagnosticKind::TimestepDiffers, DelayDiagnosticKind::BaselineHasIncidents,
					 DelayDiagnosticKind::BaselineHasEntranceDelays, DelayDiagnosticKind::OccurrenceOnlyInBaseline,
					 DelayDiagnosticKind::CallsDiffer, DelayDiagnosticKind::OccurrenceOnlyInScenario}),
		"incompatible: all ten reasons, in a fixed order");
	// incompatible: the occurrence kinds name the occurrence
	QCOMPARE(result.diagnostics[7].serviceId, "Y");
	QCOMPARE(result.diagnostics[7].occurrence, 1);
	QCOMPARE(result.diagnostics[8].serviceId, "Z");
	QCOMPARE(result.diagnostics[9].serviceId, "W");
	QVERIFY(result.diagnostics[0].serviceId.empty());
	QCOMPARE(result.diagnostics[0].occurrence, 0);
	// incompatible: totals do not exist
	QCOMPARE(result.totals.occurrences, 0);
	QVERIFY(!result.totals.positiveSeconds.available);
	QVERIFY(!result.totals.netSeconds.available);
	QVERIFY(!result.totals.complete);

	const DelayRunSnapshot unnamed = snapshot("", {}, {});
	const DelayBaselineComparison missing = compareAgainstBaseline(unnamed, snapshot("run", {}, {}));
	// incompatible: an empty id is reported once
	QVERIFY(!missing.compatible);
	QVERIFY(onlyDiagnostics(missing, {DelayDiagnosticKind::ScenarioIdMissing}));
	// incompatible: diagnostic codes
	QCOMPARE(std::string(delayDiagnosticCode(DelayDiagnosticKind::ScenarioIdsEqual)), "scenario_ids_equal");
	QCOMPARE(std::string(delayDiagnosticCode(DelayDiagnosticKind::CallsDiffer)), "calls_differ");
}

// Same, earlier and later in one comparison. The final arrivals at R are baseline 1000 for every train; scenario X 1050
// (+50, later), Y 970 (-30, earlier), Z 1000 (0, same). The positive sum is 50 and the net sum 20.
// A scenario that equals the baseline gives a positive sum of 0 that exists, unlike no sum at all.
void TestDelayAnalysis::countsLaterSameAndEarlier() {
	const auto build = [](double x, double y, double z) {
		return snapshot("scenario",
			{
				row("X", 1, "R", 1, 1, {kNone, kNone, x, kNone}),
				row("Y", 1, "R", 1, 1, {kNone, kNone, y, kNone}),
				row("Z", 1, "R", 1, 1, {kNone, kNone, z, kNone}),
			},
			{trainResult("X", 1), trainResult("Y", 1), trainResult("Z", 1)});
	};
	DelayRunSnapshot baseline = build(1000.0, 1000.0, 1000.0);
	baseline.scenarioId = "baseline";
	const DelayBaselineComparison mixed = compareAgainstBaseline(baseline, build(1050.0, 970.0, 1000.0));
	const BaselineOccurrence* y = occurrenceOf(mixed, "Y");
	// mixed: totals
	QVERIFY(mixed.compatible);
	QCOMPARE(mixed.totals.later, 1);
	QCOMPARE(mixed.totals.same, 1);
	QCOMPARE(mixed.totals.earlier, 1);
	QVERIFY(isValue(mixed.totals.positiveSeconds, 50.0));
	QVERIFY(isValue(mixed.totals.netSeconds, 20.0));
	QVERIFY(mixed.totals.complete);
	// mixed: an early occurrence has a negative difference
	if (y == nullptr)
		QFAIL("mixed: an early occurrence has a negative difference");
	QVERIFY(isDifference(y->finalArrival, DelayChange::Earlier, -30.0));
	QVERIFY(isValue(y->secondarySeconds, -30.0));

	const DelayBaselineComparison equal = compareAgainstBaseline(baseline, build(1000.0, 1000.0, 1000.0));
	// equal: zero delay is a value
	QVERIFY(equal.compatible);
	QCOMPARE(equal.totals.same, 3);
	QVERIFY(isValue(equal.totals.positiveSeconds, 0.0));
	QVERIFY(isValue(equal.totals.netSeconds, 0.0));
	QVERIFY(equal.totals.complete);

	const DelayBaselineComparison earlier = compareAgainstBaseline(baseline, build(990.0, 970.0, 980.0));
	// earlier: the positive sum is 0 and the net sum is negative
	QCOMPARE(earlier.totals.earlier, 3);
	QVERIFY(isValue(earlier.totals.positiveSeconds, 0.0));
	QVERIFY(isValue(earlier.totals.netSeconds, -60.0));
}

// A run without any occurrence is compatible and has no sum. An occurrence with no call has no final arrival.
void TestDelayAnalysis::comparesEmptyRuns() {
	const DelayBaselineComparison none = compareAgainstBaseline(snapshot("baseline", {}, {}), snapshot("scenario", {}, {}));
	// empty runs: compatible and no sum
	QVERIFY(none.compatible);
	QVERIFY(none.occurrences.empty());
	QCOMPARE(none.totals.occurrences, 0);
	QVERIFY(!none.totals.positiveSeconds.available);
	QVERIFY(!none.totals.netSeconds.available);
	QVERIFY(!none.totals.complete);

	const DelayBaselineComparison noCalls = compareAgainstBaseline(snapshot("baseline", {}, {trainResult("X", 1)}),
		snapshot("scenario", {}, {trainResult("X", 1)}));
	const BaselineOccurrence* x = occurrenceOf(noCalls, "X");
	// empty runs: an occurrence without a call has no final arrival
	QVERIFY(noCalls.compatible);
	if (x == nullptr)
		QFAIL("empty runs: an occurrence without a call has no final arrival");
	QVERIFY(x->finalStationId.empty());
	QCOMPARE(x->finalJourneyIndex, 0);
	QVERIFY(isUnavailableDifference(x->finalArrival));
	QVERIFY(!x->secondarySeconds.available);
	QCOMPARE(noCalls.totals.notAvailable, 1);
	QVERIFY(!noCalls.totals.complete);
	QVERIFY(!noCalls.totals.positiveSeconds.available);
}

// An occurrence without a service id is identified by its train id, in the events, the filter and the comparison.
void TestDelayAnalysis::identifiesOccurrenceWithoutServiceId() {
	TimetableResultRow solo = row("solo", 1, "S", 1, 1, {100.0, kNone, 130.0, kNone});
	solo.serviceId.clear();
	TrainRunResult soloRun = trainResult("solo", 1);
	soloRun.serviceId.clear();
	RunResults soloResults;
	soloResults.trains.push_back(soloRun);
	const std::vector<DelayEvent> events = buildDelayEvents({solo}, soloResults);
	DelayFilter filter;
	filter.occurrences = {{"solo-1", 1}};
	const DelaySummary summary = summarizeDelays(events, filter);
	// no service id: the train id identifies the occurrence in the filter
	QVERIFY(hasCounts(summary.network, {1, 1, 0, 0, 0, 0, 0, 1}));
	QVERIFY(hasFigures(summary.network, 30.0, 30.0, 30.0, 30.0));
	TimetableResultRow later = solo;
	later.simulatedArrivalSeconds = secondsOrNone(150.0);
	DelayRunSnapshot baseline = snapshot("baseline", {solo}, {soloRun});
	DelayRunSnapshot scenario = snapshot("scenario", {later}, {soloRun});
	const DelayBaselineComparison result = compareAgainstBaseline(baseline, scenario);
	// no service id: the comparison pairs the occurrence by its train id
	QVERIFY(result.compatible);
	QCOMPARE(result.occurrences.size(), std::size_t(1));
	QVERIFY(isDifference(result.occurrences[0].finalArrival, DelayChange::Later, 20.0));
}

QTEST_APPLESS_MAIN(TestDelayAnalysis)
#include "test_delayanalysis.moc"
