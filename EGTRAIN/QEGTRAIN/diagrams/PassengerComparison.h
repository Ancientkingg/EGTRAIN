#pragma once

#include "diagrams/RunResults.h"

#include <cstddef>
#include <string>
#include <vector>

/// Maximum number of origin-destination pairs kept in a passenger baseline comparison.
inline constexpr std::size_t kMaxComparedOriginDestinations = 20;

/// One figure of the passenger summary of a run, named as the tables and the CSV files name it.
struct PassengerFigure {
	std::string label;	   ///< Name of the figure. The names of the delays say that they count the completed journeys only.
	std::string unit;	   ///< Unit of the figure: "journeys", "s", "passenger-hours", "refusals" or "passengers".
	RunResultValue value;  ///< Value in the run; unavailable when the figure is undefined, such as a delay without completed journeys.
	bool isInteger = true; ///< True for whole counts, false for fractional quantities.
};

/// The figures of the passenger summary of a run, in the order of the comparison table and the CSV files: the journeys, the
/// completed and not reached journeys, the total and mean arrival delay and the passenger-hours lost of the completed
/// journeys, the journeys with a missed connection, the journeys planned again and the boarding refusals.
std::vector<PassengerFigure> passengerFigures(const PassengerRunSummary& summary);

/// One row of the summary table comparing a baseline run and a scenario run.
struct PassengerMetricRow {
	std::string label;		   ///< Name of the figure shown in the table and CSV (PassengerFigure::label).
	std::string unit;		   ///< Unit of the figure (PassengerFigure::unit).
	RunResultValue baseline;   ///< Value in the baseline run; unavailable when the figure is undefined.
	RunResultValue scenario;   ///< Value in the compared run; unavailable when the figure is undefined.
	RunResultValue difference; ///< Scenario minus baseline; unavailable unless both sides are available.
	bool isInteger = true;	   ///< True for exact whole-number counts, false for fractional quantities.
};

/// One origin-destination pair compared between a baseline run and a scenario run. A pair that only one run has counts as a
/// pair without journeys in the other.
struct OriginDestinationChange {
	std::string origin;						 ///< Origin station.
	std::string destination;				 ///< Destination station.
	int baselineJourneys = 0;				 ///< Journeys of the pair in the baseline run.
	int scenarioJourneys = 0;				 ///< Journeys of the pair in the compared run.
	int baselineCompleted = 0;				 ///< Completed journeys of the pair in the baseline run.
	int scenarioCompleted = 0;				 ///< Completed journeys of the pair in the compared run.
	int baselineNotReached = 0;				 ///< Journeys of the pair in the baseline run that did not reach the destination.
	int scenarioNotReached = 0;				 ///< Journeys of the pair in the compared run that did not reach the destination.
	RunResultValue baselineMeanDelay;		 ///< Mean arrival delay of the completed journeys in the baseline run, in seconds.
	RunResultValue scenarioMeanDelay;		 ///< Mean arrival delay of the completed journeys in the compared run, in seconds.
	RunResultValue meanDelayDifference;		 ///< Scenario minus baseline mean arrival delay, in seconds.
	double baselinePassengerHoursLost = 0;	 ///< Passenger-hours lost by the completed journeys in the baseline run.
	double scenarioPassengerHoursLost = 0;	 ///< Passenger-hours lost by the completed journeys in the compared run.
	double passengerHoursLostDifference = 0; ///< Scenario minus baseline passenger-hours lost.
};

/// Result of comparing the passengers of a run against a baseline run.
struct PassengerBaselineComparison {
	PassengerRunSummary baseline;					   ///< Aggregate figures of the baseline run.
	PassengerRunSummary scenario;					   ///< Aggregate figures of the compared run.
	std::vector<PassengerMetricRow> metrics;		   ///< Summary rows for baseline, this run and the difference.
	std::vector<OriginDestinationChange> changedPairs; ///< The pairs that changed most; see comparePassengersAgainstBaseline.
};

/// Compares the passengers of `scenario` against `baseline`. The pairs are those whose journeys, completed journeys, journeys
/// not reached, mean delay or passenger-hours lost differ between the runs. They are ranked by the absolute change in
/// passenger-hours lost, then by the absolute change in journeys not reached, both descending, then by origin and destination,
/// and at most kMaxComparedOriginDestinations are kept. Empty when no pair changed.
PassengerBaselineComparison comparePassengersAgainstBaseline(const DelayRunSnapshot& baseline,
	const DelayRunSnapshot& scenario);

/// CSV text of the passengers of one run (`passenger_summary.csv`): a `summary` record for each of passengerFigures, then an
/// `od` record for each origin-destination pair of `pairs` with its journeys, completed and not reached journeys, and the mean
/// delay and passenger-hours lost of its completed journeys. Unavailable values are empty fields.
std::string passengerSummaryCsv(const PassengerRunSummary& summary, const std::vector<OriginDestinationSummary>& pairs);

/// CSV text of a passenger baseline comparison (`passenger_comparison.csv`), holding `summary` records for the aggregate
/// figures and `od` records for the changed origin-destination pairs. Empty when `comparison.metrics` is empty.
std::string passengerComparisonCsv(const PassengerBaselineComparison& comparison);
