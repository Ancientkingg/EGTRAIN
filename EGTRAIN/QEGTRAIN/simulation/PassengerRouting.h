#ifndef PassengerRouting_hpp
#define PassengerRouting_hpp

#include <cstddef>
#include <limits>
#include <vector>

/// Earliest-arrival routing of passengers over the timetable of the trains of a run.
///
/// Stations and trains are indices; the caller maps them to the IDs of the run. Times are seconds on one clock of the
/// caller's choice. A search starts at an origin station at a time and finds, for every station, the earliest arrival
/// with at most a given number of transfers, and among the earliest arrivals the one with the fewest trains, and then the one
/// whose last train leaves earlier. It works in
/// rounds: round k holds the best arrivals that use k trains (round-based public transit routing, without the grouping of
/// trains into routes).
namespace passenger_routing {

/// A time that is not reached.
inline constexpr double kUnreached = std::numeric_limits<double>::infinity();

/// Walking time in seconds per platform number of difference between two platforms of a station when the case gives no value.
inline constexpr double kDefaultWalkingSecondsPerPlatform = 30.0;

/// Walking time in seconds between two platforms of a station, from their platform numbers: secondsPerPlatform for each
/// platform number of difference, so none on the same platform. A number of 0 is an unknown platform and needs no walking.
double walkingSeconds(int fromPlatformNumber, int toPlatformNumber, double secondsPerPlatform = kDefaultWalkingSecondsPerPlatform);

/// One stop of a train: where it stops and its planned times. A time below 0 is missing: a train cannot be boarded at a
/// stop without a departure, nor left at a stop without an arrival.
struct Stop {
	int station = -1;		 ///< Index of the station; below 0 for a stop at no station of the router.
	int platformNumber = 0;	 ///< platformNumber of the platform of the stop; 0 when unknown.
	double arrival = -1.0;	 ///< Planned arrival; below 0 when the stop has none.
	double departure = -1.0; ///< Planned departure; below 0 when the stop has none.
};

/// The stops of one train in service order.
struct TrainTimetable {
	std::vector<Stop> stops; ///< The stops in service order.
};

/// What a search knows about a train now: where it can still be boarded, and the times it uses for its stops.
struct TrainState {
	/// The first stop where the train can still be boarded: the stop where it stands, or the next one. The train cannot be
	/// boarded at earlier stops; it is still left at its later stops. A value at or past the last stop leaves the train out.
	std::size_t firstStop = 0;
	/// The predicted arrival at each stop of the train, one per stop of its timetable; below 0 at a stop without an arrival.
	/// Empty when the search uses the planned times.
	std::vector<double> arrivals;
	/// The predicted departure at each stop, like arrivals; empty when arrivals is.
	std::vector<double> departures;
	/// Seconds the train is late now, never negative. The search does not read it.
	double delay = 0.0;
	/// True when delay is above the late threshold, so that the times are predicted. The search does not read it.
	bool late = false;
	/// True while a speed limit applies to the train, so that the times are predicted. The search does not read it.
	bool slowed = false;

	/// The arrival at stop that a search uses: the predicted one, or the planned one of train when there are none.
	double arrival(const TrainTimetable& train, std::size_t stop) const { return arrivals.empty() ? train.stops[stop].arrival : arrivals[stop]; }
	/// The departure at stop that a search uses: the predicted one, or the planned one of train when there are none.
	double departure(const TrainTimetable& train, std::size_t stop) const {
		return departures.empty() ? train.stops[stop].departure : departures[stop];
	}
};

/// The late threshold in seconds when the case gives no value: passengers plan on the planned times of a train that is late
/// by this or less.
inline constexpr double kDefaultLateThresholdSeconds = 300.0;

/// Seconds to run lengthMetres at the lower of speedMs, the speed of the train on that stretch without the limit, and limitMs,
/// the speed limit (both m/s). A speed of 0 or less, or one that is not finite, is unknown, and the stretch runs at the limit.
/// Acceleration and braking are not counted.
double limitedStretchSeconds(double lengthMetres, double speedMs, double limitMs);

/// What a prediction of the times of a train starts from. Times are seconds on the clock of the router.
struct TrainProgress {
	std::size_t firstStop = 0; ///< TrainState::firstStop.
	double delay = 0.0;		   ///< Seconds the train is late now, never negative.
	double now = 0.0;		   ///< The time now.
	bool standing = false;	   ///< True while the train stands at firstStop.
	/// True while a speed limit applies to the train; nextStop, nextArrival, runs and limitEnds then say what it costs.
	bool limited = false;
	/// The stop the train runs to: firstStop while it stands there, otherwise the first stop ahead of it. At or past the last
	/// stop when there is none.
	std::size_t nextStop = 0;
	/// The earliest arrival at nextStop under the limit from where the train is now; below 0 when it is not known.
	double nextArrival = -1.0;
	/// runs[s]: seconds the train needs under the limit from stop s to stop s + 1; below 0 when not known. Read from nextStop on.
	std::vector<double> runs;
	/// When the limit ends; kUnreached when it lasts to the end of the route. A run that starts at this time or later is not
	/// limited.
	double limitEnds = kUnreached;
};

/// The state that a search uses for a train with this timetable and progress. A train that is late by lateThresholdSeconds or
/// less and has no speed limit keeps its planned times. Otherwise each time is predicted: the planned time plus the delay, and
/// under a limit each run still to make takes the longer of its planned running time and its running time under the limit,
/// with the time lost carried to the later stops like a delay. A train that stands at firstStop can be boarded there until now,
/// whatever its delay.
TrainState predictTrainState(const TrainTimetable& timetable, const TrainProgress& progress, double lateThresholdSeconds = kDefaultLateThresholdSeconds);

/// One ride of a plan: a train from the stop where the passenger boards to the stop where the passenger leaves it.
struct Leg {
	std::size_t train = 0;		   ///< Index of the train.
	std::size_t boardStop = 0;	   ///< Index in the stops of the train of the stop where the passenger boards.
	std::size_t alightStop = 0;	   ///< Index in the stops of the train of the stop where the passenger leaves.
	double departure = kUnreached; ///< At boardStop, as the TrainState of the train gives it.
	double arrival = kUnreached;   ///< At alightStop, as the TrainState of the train gives it.
};

/// The rides from the origin to one destination, in travel order; empty when the destination is not reached or is the
/// origin.
struct Plan {
	std::vector<Leg> legs;		 ///< The rides in travel order.
	double arrival = kUnreached; ///< The arrival at the destination; kUnreached when it is not reached.
	/// True when the destination is reached, also when it is the origin and the plan has no legs.
	bool reached() const { return arrival != kUnreached; }
};

/// The result of one search from an origin: the best arrival at every station in every round.
class Search {
public:
	/// The earliest arrival at station with at most maxTransfers transfers; kUnreached when none.
	double arrival(std::size_t station) const;
	/// The rides of the earliest arrival at station, with the fewest trains among the earliest.
	Plan plan(std::size_t station) const;

private:
	friend class Router;
	struct Label {
		double arrival = kUnreached;
		int platformNumber = 0;
		std::size_t train = 0;
		std::size_t boardStop = 0;
		std::size_t alightStop = 0;
	};
	const std::vector<TrainTimetable>* trains_ = nullptr;
	const std::vector<TrainState>* states_ = nullptr;
	std::vector<std::vector<Label>> rounds_; ///< rounds_[k][station]: the best arrival with exactly k trains; round 0 is the origin.
	std::vector<double> best_;				 ///< The best arrival at each station over all rounds.
};

/// The trains of a run, with an index from stations to the trains that stop there.
class Router {
public:
	/// Keeps the timetables. A stop with a station outside stationCount is ignored. A transfer walks secondsPerPlatform per
	/// platform number of difference (see walkingSeconds).
	Router(std::vector<TrainTimetable> trains, std::size_t stationCount, double walkingSecondsPerPlatform = kDefaultWalkingSecondsPerPlatform);

	/// The number of trains.
	std::size_t trainCount() const { return trains_.size(); }
	/// The number of stations.
	std::size_t stationCount() const { return stationCount_; }
	/// The timetables of the trains, in the order they were given.
	const std::vector<TrainTimetable>& trains() const { return trains_; }

	/// Searches from origin, where the passenger is ready at readyTime, with at most maxTransfers transfers. states holds
	/// one TrainState per train, whose predicted times are empty or one per stop, and must outlive the result, like the
	/// router; other states reach nothing. A transfer needs walkingSeconds between the
	/// platform of the arrival and that of the departure. The first boarding needs walkingSeconds from originPlatformNumber,
	/// the platform where the passenger stands; 0, the default, for a passenger who just entered the station.
	Search search(std::size_t origin, double readyTime, int maxTransfers, const std::vector<TrainState>& states,
		int originPlatformNumber = 0) const;

private:
	std::vector<TrainTimetable> trains_;
	std::size_t stationCount_;
	double walkingSecondsPerPlatform_;
	/// For each station, the pairs of train and stop index where a train departs from it.
	std::vector<std::vector<std::pair<std::size_t, std::size_t>>> departures_;
};

} // namespace passenger_routing

#endif
