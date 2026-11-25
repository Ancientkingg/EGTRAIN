#ifndef PASSENGERRESULTS_H
#define PASSENGERRESULTS_H

#include "simulation/GuiSimulationSnapshot.h"
#include "simulation/PassengerSummary.h"
#include "simulation/Passengers.h"
#include <list>
#include <string>
#include <vector>

struct SimulationRun;

/// What the passenger step counted during one call of a train at a stop.
struct StopCallCounts {
	int boarded = 0;	///< Passengers who boarded.
	int alighted = 0;	///< Passengers who alighted.
	int leftBehind = 0; ///< Passengers who were refused because the train was full.
	int loadAfter = 0;	///< Passengers on board after the latest step of the call; the load at departure.
};

/// The counts of every call that trains made at their stops, by train index in the run and stop index in the train. It grows
/// as the passenger step meets a call, and SimulationRun empties it with the passenger flow.
class StopCallLog {
public:
	/// Empties the log.
	void clear() { calls_.clear(); }
	/// The counts of the call of this train at this stop, created with zeros when the passenger step has not met it yet.
	StopCallCounts& at(int train, int stop);
	/// The counts of the call, or null when the passenger step never met it.
	const StopCallCounts* find(int train, int stop) const;

private:
	/// The counts of one call and whether the passenger step met it. Meeting a later call of the train creates the
	/// calls before it unmet.
	struct Entry {
		StopCallCounts counts; ///< The counts of the call.
		bool met = false;	   ///< True once at() returned this call.
	};
	std::vector<std::vector<Entry>> calls_;
};

/// Every call that trains made at a station so far, ordered by arrival. The counts are those that the passenger step has made.
std::vector<GuiStationCall> stationCalls(const SimulationRun& run);

/// The latest kGuiStationCallsListed calls of every station that began at or before `step`, newest first, with the counts
/// as they are now. It reads the run, so it must run on the thread that steps it.
std::vector<GuiStationCall> latestStationCalls(const SimulationRun& run, int step);

/// A ratio or mean as the result files write it: three decimals.
std::string formatRatio(double value);

/// The value at nearest rank p percent of the values: sorted ascending, the one at rank ceil(p * n / 100). The values must not be
/// empty and p is from 1 to 100.
double nearestRankPercentile(std::vector<double> values, int p);

/// The summary of every origin-destination pair that has journeys, sorted by origin and then destination.
std::vector<OriginDestinationSummary> summariseOriginDestinations(const std::list<Passenger>& passengers);

/// Aggregate passenger figures for the run, computed from the same journey fields and stop-call counts that
/// PassengerJourneys.txt, StationStops.txt, ODSummary.txt and JourneyDelays.txt write.
PassengerRunSummary summarisePassengerRun(const SimulationRun& run);

/// The text of PassengerJourneys.txt: a header row and a tab-separated row for every journey of every passenger.
std::string passengerJourneysText(const std::list<Passenger>& passengers);

/// The text of StationStops.txt: a header row and a tab-separated row for every stop where a train of the run stood. Arrival and
/// departure are the first and the last step of the train at the stand, in seconds since midnight.
std::string stationStopsText(const SimulationRun& run);

/// The text of ODSummary.txt: a header row and a tab-separated row for every origin-destination pair with journeys.
std::string originDestinationSummaryText(const std::list<Passenger>& passengers);

/// Writes MainFolder/PassengerJourneys.txt, MainFolder/StationStops.txt and MainFolder/ODSummary.txt, next to JourneyDelays.txt.
/// Throws std::runtime_error when a file cannot be written (simulation/RunOutputFile.h).
void printPassengerResultFiles(const SimulationRun& run, const std::string& mainFolder);

#endif // PASSENGERRESULTS_H
