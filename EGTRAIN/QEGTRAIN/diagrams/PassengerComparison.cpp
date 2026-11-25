#include "diagrams/PassengerComparison.h"

#include "diagrams/RunCsv.h"
#include "util/CsvWriter.h"

#include <algorithm>
#include <cmath>
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

PassengerMetricRow countRow(const char* label, const char* unit, int baseline, int scenario) {
	return {label, unit, countValue(baseline), countValue(scenario), countValue(scenario - baseline), true};
}

PassengerMetricRow doubleRow(const char* label, const char* unit, const RunResultValue& baseline, const RunResultValue& scenario) {
	return {label, unit, baseline, scenario, differenceOf(baseline, scenario), false};
}

std::string csvMetricValue(const RunResultValue& value, bool isInteger) {
	if (!value.available)
		return std::string(csv::kMissingValue);
	return isInteger ? std::to_string(static_cast<long long>(std::llround(value.value))) : csv::formatDouble(value.value);
}

struct OdPairData {
	const OriginDestinationSummary* baseline = nullptr;
	const OriginDestinationSummary* scenario = nullptr;
};

} // namespace

PassengerBaselineComparison comparePassengersAgainstBaseline(const DelayRunSnapshot& baseline,
	const DelayRunSnapshot& scenario) {
	PassengerBaselineComparison result;
	result.baseline = baseline.passengerSummary;
	result.scenario = scenario.passengerSummary;

	const PassengerRunSummary& base = baseline.passengerSummary;
	const PassengerRunSummary& cur = scenario.passengerSummary;

	const RunResultValue baseTotalDelay = base.completedJourneys > 0 ? availableDouble(base.totalDelaySeconds) : RunResultValue{};
	const RunResultValue curTotalDelay = cur.completedJourneys > 0 ? availableDouble(cur.totalDelaySeconds) : RunResultValue{};
	const RunResultValue baseMeanDelay = base.completedJourneys > 0 ? availableDouble(base.meanDelaySeconds) : RunResultValue{};
	const RunResultValue curMeanDelay = cur.completedJourneys > 0 ? availableDouble(cur.meanDelaySeconds) : RunResultValue{};

	result.metrics = {
		countRow("Journeys", "journeys", base.journeys, cur.journeys),
		countRow("Completed journeys", "journeys", base.completedJourneys, cur.completedJourneys),
		countRow("Not reached (total)", "journeys", base.notReachedJourneys(), cur.notReachedJourneys()),
		countRow("Not reached: no route", "journeys", base.noRouteJourneys, cur.noRouteJourneys),
		countRow("Not reached: in progress", "journeys", base.inProgressJourneys, cur.inProgressJourneys),
		doubleRow("Total arrival delay", "s", baseTotalDelay, curTotalDelay),
		doubleRow("Mean arrival delay", "s", baseMeanDelay, curMeanDelay),
		doubleRow("Passenger-hours lost", "passenger-hours", availableDouble(base.passengerHoursLost), availableDouble(cur.passengerHoursLost)),
		countRow("Missed connections", "journeys", base.missedConnections, cur.missedConnections),
		countRow("Journeys planned again", "journeys", base.replannedJourneys, cur.replannedJourneys),
		countRow("Left behind (refusals)", "refusals", base.leftBehindEvents, cur.leftBehindEvents),
		countRow("Left behind (passengers)", "passengers", base.refusedPassengers, cur.refusedPassengers),
	};

	std::map<std::pair<std::string, std::string>, OdPairData> pairs;
	for (const OriginDestinationSummary& od : baseline.originDestinations)
		pairs[{od.origin, od.destination}].baseline = &od;
	for (const OriginDestinationSummary& od : scenario.originDestinations)
		pairs[{od.origin, od.destination}].scenario = &od;

	result.topOdPairs.reserve(pairs.size());
	for (const auto& entry : pairs) {
		OriginDestinationChange change;
		change.origin = entry.first.first;
		change.destination = entry.first.second;
		if (const OriginDestinationSummary* baseOd = entry.second.baseline) {
			change.baselineJourneys = baseOd->journeys;
			change.baselineCompleted = baseOd->completed;
			if (baseOd->completed > 0)
				change.baselineMeanDelay = availableDouble(baseOd->meanDelay);
			change.baselinePassengerHoursLost = baseOd->passengerHoursLost;
		}
		if (const OriginDestinationSummary* curOd = entry.second.scenario) {
			change.scenarioJourneys = curOd->journeys;
			change.scenarioCompleted = curOd->completed;
			if (curOd->completed > 0)
				change.scenarioMeanDelay = availableDouble(curOd->meanDelay);
			change.scenarioPassengerHoursLost = curOd->passengerHoursLost;
		}
		change.meanDelayDifference = differenceOf(change.baselineMeanDelay, change.scenarioMeanDelay);
		change.passengerHoursLostDifference = change.scenarioPassengerHoursLost - change.baselinePassengerHoursLost;
		result.topOdPairs.push_back(std::move(change));
	}

	std::sort(result.topOdPairs.begin(), result.topOdPairs.end(),
		[](const OriginDestinationChange& a, const OriginDestinationChange& b) {
			const double absA = std::fabs(a.passengerHoursLostDifference);
			const double absB = std::fabs(b.passengerHoursLostDifference);
			if (absA != absB)
				return absA > absB;
			if (a.origin != b.origin)
				return a.origin < b.origin;
			return a.destination < b.destination;
		});
	if (result.topOdPairs.size() > kMaxComparedOriginDestinations)
		result.topOdPairs.resize(kMaxComparedOriginDestinations);

	return result;
}

std::string passengerSummaryCsv(const PassengerRunSummary& summary) {
	const RunResultValue totalDelay = summary.completedJourneys > 0 ? availableDouble(summary.totalDelaySeconds) : RunResultValue{};
	const RunResultValue meanDelay = summary.completedJourneys > 0 ? availableDouble(summary.meanDelaySeconds) : RunResultValue{};
	const std::vector<std::vector<std::string>> rows = {
		{"Journeys", std::to_string(summary.journeys), "journeys"},
		{"Completed journeys", std::to_string(summary.completedJourneys), "journeys"},
		{"Not reached (total)", std::to_string(summary.notReachedJourneys()), "journeys"},
		{"Not reached: no route", std::to_string(summary.noRouteJourneys), "journeys"},
		{"Not reached: in progress", std::to_string(summary.inProgressJourneys), "journeys"},
		{"Total arrival delay", csvValue(totalDelay), "s"},
		{"Mean arrival delay", csvValue(meanDelay), "s"},
		{"Passenger-hours lost", csv::formatDouble(summary.passengerHoursLost), "passenger-hours"},
		{"Missed connections", std::to_string(summary.missedConnections), "journeys"},
		{"Journeys planned again", std::to_string(summary.replannedJourneys), "journeys"},
		{"Left behind (refusals)", std::to_string(summary.leftBehindEvents), "refusals"},
		{"Left behind (passengers)", std::to_string(summary.refusedPassengers), "passengers"},
	};
	return csv::makeDocument({"Metric", "Value", "Unit"}, rows);
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
		kBaselineMeanDelay,
		kScenarioMeanDelay,
		kMeanDelayDifference,
		kBaselineLost,
		kScenarioLost,
		kLostDifference,
		kColumnCount
	};
	const char* const names[] = {
		"Record type", "Metric", "Unit", "Baseline", "Scenario", "Difference",
		"Origin", "Destination", "Baseline journeys", "Scenario journeys",
		"Baseline completed", "Scenario completed",
		"Baseline mean delay[s]", "Scenario mean delay[s]", "Mean delay difference[s]",
		"Baseline lost[pax-h]", "Scenario lost[pax-h]", "Lost difference[pax-h]"};
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
	for (const OriginDestinationChange& od : comparison.topOdPairs) {
		std::vector<std::string> row(kColumnCount);
		row[kRecordType] = "od";
		row[kOrigin] = od.origin;
		row[kDestination] = od.destination;
		row[kBaselineJourneys] = std::to_string(od.baselineJourneys);
		row[kScenarioJourneys] = std::to_string(od.scenarioJourneys);
		row[kBaselineCompleted] = std::to_string(od.baselineCompleted);
		row[kScenarioCompleted] = std::to_string(od.scenarioCompleted);
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
