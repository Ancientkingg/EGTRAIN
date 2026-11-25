#pragma once

#include <string>

/// The statistics of the completed journeys between one origin and one destination, in seconds. The means and the percentile
/// are -1 when no journey completed; a mean delay can also be negative for a journey that arrived early.
struct OriginDestinationSummary {
	std::string origin;				 ///< Origin station.
	std::string destination;		 ///< Destination station.
	int journeys = 0;				 ///< Journeys of the pair, finished or not.
	int completed = 0;				 ///< Journeys that reached the destination.
	int noRoute = 0;				 ///< Journeys that found no route.
	double meanWait = -1;			 ///< Mean waiting time of the completed journeys.
	double p95Wait = -1;			 ///< Waiting time below which 95 percent of the completed journeys fall, by nearest rank.
	double meanTravel = -1;			 ///< Mean of arrival minus appearance of the completed journeys.
	double meanDelay = -1;			 ///< Mean arrival delay of the completed journeys.
	double passengerHoursLost = 0.0; ///< Sum of the positive arrival delays of the completed journeys, divided by 3600.

	/// Journeys of the pair that did not reach the destination: no route, or still on the way when the run ended.
	int notReached() const { return journeys - completed; }
};

/// Aggregate passenger figures of one run. All but the missed connections are computed from the same values that
/// PassengerJourneys.txt, StationStops.txt, ODSummary.txt and JourneyDelays.txt write.
///
/// The delays and the passenger-hours lost count the completed journeys only. A journey that did not reach its destination
/// adds nothing to them, so a run that strands passengers can show less delay than one that brings them home late; the
/// journeys not reached say how many were left out.
struct PassengerRunSummary {
	/// Total journeys in the run (rows in PassengerJourneys.txt and JourneyDelays.txt).
	int journeys = 0;
	/// Journeys that reached their destination (Outcome "completed" in PassengerJourneys.txt; numeric Delay in JourneyDelays.txt).
	int completedJourneys = 0;
	/// Journeys that left the network after finding no route (Outcome "no_route" in PassengerJourneys.txt).
	int noRouteJourneys = 0;
	/// Journeys still in progress or not started at the end of the run (Outcome "in_progress" in PassengerJourneys.txt).
	int inProgressJourneys = 0;
	/// Sum of the signed arrival delays of the completed journeys, in seconds (sum of the Delay column of PassengerJourneys.txt
	/// and of the numeric rows of JourneyDelays.txt). Zero when completedJourneys is 0.
	double totalDelaySeconds = 0.0;
	/// Mean arrival delay of the completed journeys, in seconds (totalDelaySeconds / completedJourneys). Zero when
	/// completedJourneys is 0; callers check completedJourneys > 0 because a real mean delay can be negative.
	double meanDelaySeconds = 0.0;
	/// Passenger-hours lost by the completed journeys against their planned arrival: the sum of max(0, Delay) over the
	/// completed journeys, divided by 3600.
	double passengerHoursLost = 0.0;
	/// Journeys whose legs were planned again after their first plan (Replanned = 1 in PassengerJourneys.txt).
	int replannedJourneys = 0;
	/// Journeys with at least one missed connection (Journey::MissedConnections above 0). No result file has this figure;
	/// the passenger step counts the missed connections during the run.
	int missedConnectionJourneys = 0;
	/// Total boarding refusals by full trains across all calls where a train stood (sum of LeftBehind in StationStops.txt).
	int leftBehindEvents = 0;
	/// Distinct passengers refused boarding at least once because a train was full (from Passenger::TrainThatRefusedBoarding,
	/// Passenger::StopThatRefusedBoarding and Journey::RefusedBy).
	int refusedPassengers = 0;

	/// Journeys that did not reach their destination (noRouteJourneys + inProgressJourneys).
	int notReachedJourneys() const { return noRouteJourneys + inProgressJourneys; }
};
