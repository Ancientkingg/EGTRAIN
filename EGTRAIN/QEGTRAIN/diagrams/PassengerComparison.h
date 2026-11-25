#pragma once

#include "diagrams/RunResults.h"

#include <string>
#include <vector>

/// Maximum number of origin-destination pairs kept in a passenger baseline comparison.
inline constexpr std::size_t kMaxComparedOriginDestinations = 20;

/// One row of the summary table comparing a baseline run and a scenario run.
struct PassengerMetricRow {
	std::string label;			///< Name of the figure shown in the table and CSV.
	std::string unit;			///< Unit of the figure, such as "journeys", "s", or "passenger-hours".
	RunResultValue baseline;	///< Value in the baseline run; unavailable when the figure is undefined.
	RunResultValue scenario;	///< Value in the compared run; unavailable when the figure is undefined.
	RunResultValue difference;	///< Scenario minus baseline; unavailable unless both sides are available.
	bool isInteger = true;		///< True for exact whole-number counts, false for fractional quantities.
};

/// One origin-destination pair compared between a baseline run and a scenario run.
struct OriginDestinationChange {
	std::string origin;					   ///< Origin station.
	std::string destination;			   ///< Destination station.
	int baselineJourneys = 0;			   ///< Journeys of the pair in the baseline run.
	int scenarioJourneys = 0;			   ///< Journeys of the pair in the compared run.
	int baselineCompleted = 0;			   ///< Completed journeys of the pair in the baseline run.
	int scenarioCompleted = 0;			   ///< Completed journeys of the pair in the compared run.
	RunResultValue baselineMeanDelay;	   ///< Mean arrival delay of completed journeys in the baseline run, in seconds.
	RunResultValue scenarioMeanDelay;	   ///< Mean arrival delay of completed journeys in the compared run, in seconds.
	RunResultValue meanDelayDifference;	   ///< Scenario minus baseline mean arrival delay, in seconds.
	double baselinePassengerHoursLost = 0; ///< Passenger-hours lost in the baseline run.
	double scenarioPassengerHoursLost = 0; ///< Passenger-hours lost in the compared run.
	double passengerHoursLostDifference = 0; ///< Scenario minus baseline passenger-hours lost.
};

/// Result of comparing the passengers of a run against a baseline run.
struct PassengerBaselineComparison {
	PassengerRunSummary baseline;					  ///< Aggregate figures of the baseline run.
	PassengerRunSummary scenario;					  ///< Aggregate figures of the compared run.
	std::vector<PassengerMetricRow> metrics;		  ///< Summary rows for baseline, this run and the difference.
	std::vector<OriginDestinationChange> topOdPairs;  ///< At most kMaxComparedOriginDestinations pairs, ranked by largest absolute change in passenger-hours lost.
};

/// Compares the passengers of `scenario` against `baseline`. Origin-destination pairs are ranked by absolute change in
/// passenger-hours lost descending, breaking ties by origin and then destination ascending, capped at
/// kMaxComparedOriginDestinations rows.
PassengerBaselineComparison comparePassengersAgainstBaseline(const DelayRunSnapshot& baseline,
	const DelayRunSnapshot& scenario);

/// CSV text of a single run's passenger summary (`passenger_summary.csv`), with one row per metric and empty fields for
/// unavailable values.
std::string passengerSummaryCsv(const PassengerRunSummary& summary);

/// CSV text of a passenger baseline comparison (`passenger_comparison.csv`), holding `summary` records for the aggregate
/// figures and `od` records for the ranked origin-destination pairs. Empty when `comparison.metrics` is empty.
std::string passengerComparisonCsv(const PassengerBaselineComparison& comparison);
