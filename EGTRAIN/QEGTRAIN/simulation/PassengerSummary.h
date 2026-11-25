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
	double passengerHoursLost = 0.0; ///< Sum of positive arrival delays of the completed journeys, divided by 3600.
};

/// Aggregate passenger figures of one run, computed from the same values written to PassengerJourneys.txt,
/// StationStops.txt, ODSummary.txt and JourneyDelays.txt.
struct PassengerRunSummary {
	/// Total journeys in the run (rows in PassengerJourneys.txt and JourneyDelays.txt).
	int journeys = 0;
	/// Journeys that reached their destination (Outcome "completed" in PassengerJourneys.txt; numeric Delay in JourneyDelays.txt).
	int completedJourneys = 0;
	/// Journeys that left the network after finding no route (Outcome "no_route" in PassengerJourneys.txt).
	int noRouteJourneys = 0;
	/// Journeys still in progress or not started at the end of the run (Outcome "in_progress" in PassengerJourneys.txt).
	int inProgressJourneys = 0;
	/// Sum of the signed arrival delays of completed journeys, in seconds (sum of the Delay column of PassengerJourneys.txt
	/// and of the numeric rows of JourneyDelays.txt). Zero when completedJourneys is 0.
	double totalDelaySeconds = 0.0;
	/// Mean arrival delay of completed journeys, in seconds (totalDelaySeconds / completedJourneys). Zero when
	/// completedJourneys is 0; callers check completedJourneys > 0 because a real mean delay can be negative.
	double meanDelaySeconds = 0.0;
	/// Passenger-hours lost against the planned journeys: sum of max(0, Delay) over completed journeys, divided by 3600.
	double passengerHoursLost = 0.0;
	/// Journeys whose legs were planned again after their first plan (Replanned = 1 in PassengerJourneys.txt).
	int replannedJourneys = 0;
	/// Replanned journeys that rode a connecting leg or were replanned after riding at least one leg: Replanned = 1 in
	/// PassengerJourneys.txt with either more than one train in Trains (contains "+") or at least one train (Trains != "-")
	/// and Outcome != "completed". Closest exact count of missed connections available from the result data without
	/// changing the passenger simulation.
	int missedConnections = 0;
	/// Total boarding refusals by full trains across all calls where a train stood (sum of LeftBehind in StationStops.txt).
	int leftBehindEvents = 0;
	/// Distinct passengers refused boarding at least once because a train was full (from Passenger::TrainThatRefusedBoarding,
	/// Passenger::StopThatRefusedBoarding and Journey::RefusedBy).
	int refusedPassengers = 0;

	/// Journeys that did not reach their destination (noRouteJourneys + inProgressJourneys).
	int notReachedJourneys() const { return noRouteJourneys + inProgressJourneys; }
};
