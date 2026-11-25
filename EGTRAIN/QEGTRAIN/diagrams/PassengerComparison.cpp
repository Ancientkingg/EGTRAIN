#include "diagrams/PassengerComparison.h"

#include "diagrams/RunCsv.h"
#include "util/CsvWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <map>
#include <utility>

namespace {

RunResultValue countValue(int value) {
	return {true, static_cast<double>(value)};
}

RunResultValue availableDouble(double value) {
	return std::isfinite(value) ? RunResultValue{true, value} : RunResultValue{};
}

RunResultValue differenceOf(const RunResultValue& baseline, const RunResultValue& scenario) {
	if (!baseline.available || !scenario.available)
		return {};
	return availableDouble(scenario.value - baseline.value);
}

bool sameValue(const RunResultValue& a, const RunResultValue& b) {
	return a.available == b.available && (!a.available || a.value == b.value);
}

std::string csvMetricValue(const RunResultValue& value, bool isInteger) {
	if (!value.available)
		return std::string(csv::kMissingValue);
	return isInteger ? std::to_string(static_cast<long long>(std::llround(value.value))) : csv::formatDouble(value.value);
}

// The mean delay of the completed journeys of a pair; unavailable when none completed.
RunResultValue meanDelayOf(const OriginDestinationSummary& pair) {
	return pair.completed > 0 ? availableDouble(pair.meanDelay) : RunResultValue{};
}

// True when anything that the comparison shows of the pair differs between the runs.
bool hasChanged(const OriginDestinationChange& pair) {
	return pair.baselineJourneys != pair.scenarioJourneys || pair.baselineCompleted != pair.scenarioCompleted
		|| pair.baselineNotReached != pair.scenarioNotReached || !sameValue(pair.baselineMeanDelay, pair.scenarioMeanDelay)
		|| pair.baselinePassengerHoursLost != pair.scenarioPassengerHoursLost;
}

struct OdPairData {
	const OriginDestinationSummary* baseline = nullptr;
	const OriginDestinationSummary* scenario = nullptr;
};

} // namespace

std::vector<PassengerFigure> passengerFigures(const PassengerRunSummary& summary) {
	const bool completed = summary.completedJourneys > 0;
	const RunResultValue totalDelay = completed ? availableDouble(summary.totalDelaySeconds) : RunResultValue{};
	const RunResultValue meanDelay = completed ? availableDouble(summary.meanDelaySeconds) : RunResultValue{};
	return {
		{"Journeys", "journeys", countValue(summary.journeys), true},
		{"Completed journeys", "journeys", countValue(summary.completedJourneys), true},
		{"Not reached (total)", "journeys", countValue(summary.notReachedJourneys()), true},
		{"Not reached: no route", "journeys", countValue(summary.noRouteJourneys), true},
		{"Not reached: in progress", "journeys", countValue(summary.inProgressJourneys), true},
		{"Total arrival delay of completed journeys", "s", totalDelay, false},
		{"Mean arrival delay of completed journeys", "s", meanDelay, false},
		{"Passenger-hours lost by completed journeys", "passenger-hours", availableDouble(summary.passengerHoursLost), false},
		{"Journeys with a missed connection", "journeys", countValue(summary.missedConnectionJourneys), true},
		{"Journeys planned again", "journeys", countValue(summary.replannedJourneys), true},
		{"Left behind (refusals)", "refusals", countValue(summary.leftBehindEvents), true},
		{"Left behind (passengers)", "passengers", countValue(summary.refusedPassengers), true},
	};
}

PassengerBaselineComparison comparePassengersAgainstBaseline(const DelayRunSnapshot& baseline,
	const DelayRunSnapshot& scenario) {
	PassengerBaselineComparison result;
	result.baseline = baseline.passengerSummary;
	result.scenario = scenario.passengerSummary;

	const std::vector<PassengerFigure> base = passengerFigures(result.baseline);
	const std::vector<PassengerFigure> cur = passengerFigures(result.scenario);
	for (std::size_t i = 0; i < base.size(); ++i)
		result.metrics.push_back({base[i].label, base[i].unit, base[i].value, cur[i].value, differenceOf(base[i].value, cur[i].value), base[i].isInteger});

	std::map<std::pair<std::string, std::string>, OdPairData> pairs;
	for (const OriginDestinationSummary& od : baseline.originDestinations)
		pairs[{od.origin, od.destination}].baseline = &od;
	for (const OriginDestinationSummary& od : scenario.originDestinations)
		pairs[{od.origin, od.destination}].scenario = &od;

	for (const auto& entry : pairs) {
		OriginDestinationChange change;
		change.origin = entry.first.first;
		change.destination = entry.first.second;
		if (const OriginDestinationSummary* baseOd = entry.second.baseline) {
			change.baselineJourneys = baseOd->journeys;
			change.baselineCompleted = baseOd->completed;
			change.baselineNotReached = baseOd->notReached();
			change.baselineMeanDelay = meanDelayOf(*baseOd);
			change.baselinePassengerHoursLost = baseOd->passengerHoursLost;
		}
		if (const OriginDestinationSummary* curOd = entry.second.scenario) {
			change.scenarioJourneys = curOd->journeys;
			change.scenarioCompleted = curOd->completed;
			change.scenarioNotReached = curOd->notReached();
			change.scenarioMeanDelay = meanDelayOf(*curOd);
			change.scenarioPassengerHoursLost = curOd->passengerHoursLost;
		}
		if (!hasChanged(change))
			continue;
		change.meanDelayDifference = differenceOf(change.baselineMeanDelay, change.scenarioMeanDelay);
		change.passengerHoursLostDifference = change.scenarioPassengerHoursLost - change.baselinePassengerHoursLost;
		result.changedPairs.push_back(std::move(change));
	}

	std::sort(result.changedPairs.begin(), result.changedPairs.end(),
		[](const OriginDestinationChange& a, const OriginDestinationChange& b) {
			const double lostA = std::fabs(a.passengerHoursLostDifference);
			const double lostB = std::fabs(b.passengerHoursLostDifference);
			if (lostA != lostB)
				return lostA > lostB;
			const int notReachedA = std::abs(a.scenarioNotReached - a.baselineNotReached);
			const int notReachedB = std::abs(b.scenarioNotReached - b.baselineNotReached);
			if (notReachedA != notReachedB)
				return notReachedA > notReachedB;
			if (a.origin != b.origin)
				return a.origin < b.origin;
			return a.destination < b.destination;
		});
	if (result.changedPairs.size() > kMaxComparedOriginDestinations)
		result.changedPairs.resize(kMaxComparedOriginDestinations);

	return result;
}

std::string passengerSummaryCsv(const PassengerRunSummary& summary, const std::vector<OriginDestinationSummary>& pairs) {
	enum Column : std::size_t {
		kRecordType,
		kMetric,
		kUnit,
		kValue,
		kOrigin,
		kDestination,
		kJourneys,
		kCompleted,
		kNotReached,
		kMeanDelay,
		kLost,
		kColumnCount
	};
	const char* const names[] = {"Record type", "Metric", "Unit", "Value", "Origin", "Destination", "Journeys", "Completed",
		"Not reached", "Mean delay of completed journeys[s]", "Passenger-hours lost by completed journeys[pax-h]"};
	static_assert(std::size(names) == kColumnCount, "one header name for each column");
	const std::vector<std::string> header(std::begin(names), std::end(names));
	std::vector<std::vector<std::string>> rows;
	for (const PassengerFigure& figure : passengerFigures(summary)) {
		std::vector<std::string> row(kColumnCount);
		row[kRecordType] = "summary";
		row[kMetric] = figure.label;
		row[kUnit] = figure.unit;
		row[kValue] = csvMetricValue(figure.value, figure.isInteger);
		rows.push_back(std::move(row));
	}
	for (const OriginDestinationSummary& pair : pairs) {
		std::vector<std::string> row(kColumnCount);
		row[kRecordType] = "od";
		row[kOrigin] = pair.origin;
		row[kDestination] = pair.destination;
		row[kJourneys] = std::to_string(pair.journeys);
		row[kCompleted] = std::to_string(pair.completed);
		row[kNotReached] = std::to_string(pair.notReached());
		row[kMeanDelay] = csvValue(meanDelayOf(pair));
		row[kLost] = csv::formatDouble(pair.passengerHoursLost);
		rows.push_back(std::move(row));
	}
	return csv::makeDocument(header, rows);
}

std::string passengerComparisonCsv(const PassengerBaselineComparison& comparison) {
	if (comparison.metrics.empty())
		return std::string();
	enum Column : std::size_t {
		kRecordType,
		kMetric,
		kUnit,
		kBaseline,
		kScenario,
		kDifference,
		kOrigin,
		kDestination,
		kBaselineJourneys,
		kScenarioJourneys,
		kBaselineCompleted,
		kScenarioCompleted,
		kBaselineNotReached,
		kScenarioNotReached,
		kBaselineMeanDelay,
		kScenarioMeanDelay,
		kMeanDelayDifference,
		kBaselineLost,
		kScenarioLost,
		kLostDifference,
		kColumnCount
	};
	const char* const names[] = {"Record type", "Metric", "Unit", "Baseline", "Scenario", "Difference", "Origin", "Destination",
		"Baseline journeys", "Scenario journeys", "Baseline completed", "Scenario completed", "Baseline not reached",
		"Scenario not reached", "Baseline mean delay of completed journeys[s]", "Scenario mean delay of completed journeys[s]",
		"Mean delay difference[s]", "Baseline passenger-hours lost by completed journeys[pax-h]",
		"Scenario passenger-hours lost by completed journeys[pax-h]", "Passenger-hours lost difference[pax-h]"};
	static_assert(std::size(names) == kColumnCount, "one header name for each column");
	const std::vector<std::string> header(std::begin(names), std::end(names));
	std::vector<std::vector<std::string>> rows;
	for (const PassengerMetricRow& metric : comparison.metrics) {
		std::vector<std::string> row(kColumnCount);
		row[kRecordType] = "summary";
		row[kMetric] = metric.label;
		row[kUnit] = metric.unit;
		row[kBaseline] = csvMetricValue(metric.baseline, metric.isInteger);
		row[kScenario] = csvMetricValue(metric.scenario, metric.isInteger);
		row[kDifference] = csvMetricValue(metric.difference, metric.isInteger);
		rows.push_back(std::move(row));
	}
	for (const OriginDestinationChange& od : comparison.changedPairs) {
		std::vector<std::string> row(kColumnCount);
		row[kRecordType] = "od";
		row[kOrigin] = od.origin;
		row[kDestination] = od.destination;
		row[kBaselineJourneys] = std::to_string(od.baselineJourneys);
		row[kScenarioJourneys] = std::to_string(od.scenarioJourneys);
		row[kBaselineCompleted] = std::to_string(od.baselineCompleted);
		row[kScenarioCompleted] = std::to_string(od.scenarioCompleted);
		row[kBaselineNotReached] = std::to_string(od.baselineNotReached);
		row[kScenarioNotReached] = std::to_string(od.scenarioNotReached);
		row[kBaselineMeanDelay] = csvValue(od.baselineMeanDelay);
		row[kScenarioMeanDelay] = csvValue(od.scenarioMeanDelay);
		row[kMeanDelayDifference] = csvValue(od.meanDelayDifference);
		row[kBaselineLost] = csv::formatDouble(od.baselinePassengerHoursLost);
		row[kScenarioLost] = csv::formatDouble(od.scenarioPassengerHoursLost);
		row[kLostDifference] = csv::formatDouble(od.passengerHoursLostDifference);
		rows.push_back(std::move(row));
	}
	return csv::makeDocument(header, rows);
}
