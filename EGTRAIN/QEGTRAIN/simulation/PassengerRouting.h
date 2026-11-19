#ifndef PassengerRouting_hpp
#define PassengerRouting_hpp

#include <cstddef>
#include <limits>
#include <vector>

/// Earliest-arrival routing of passengers over the timetable of the trains of a run.
///
/// Stations and trains are indices; the caller maps them to the IDs of the run. Times are seconds on one clock of the
/// caller's choice. A search starts at an origin station at a time and finds, for every station, the earliest arrival
/// with at most a given number of transfers, and among the earliest arrivals the one with the fewest trains. It works in
/// rounds: round k holds the best arrivals that use k trains (round-based public transit routing, without the grouping of
/// trains into routes).
namespace passenger_routing {

/// A time that is not reached.
inline constexpr double kUnreached = std::numeric_limits<double>::infinity();

/// Walking time in seconds per platform of difference between two platform numbers at a transfer.
inline constexpr double kWalkingSecondsPerPlatform = 30.0;

/// Walking time in seconds between two platforms of a station, from their platform numbers: none for the same or a
/// neighbouring platform, otherwise kWalkingSecondsPerPlatform per platform of difference. A number of 0 is an unknown
/// platform and needs no walking.
double walkingSeconds(int fromPlatformNumber, int toPlatformNumber);

/// One stop of a train: where it stops and its planned times. A time below 0 is missing: a train cannot be boarded at a
/// stop without a departure, nor left at a stop without an arrival.
struct Stop {
	int station = -1;
	int platformNumber = 0; ///< platformNumber of the platform of the stop; 0 when unknown.
	double arrival = -1.0;
	double departure = -1.0;
};

/// The stops of one train in service order.
struct TrainTimetable {
	std::vector<Stop> stops;
};

/// What a search knows about a train now.
struct TrainState {
	/// The first stop where the train can still be boarded: the stop where it stands, or the next one. The train cannot be
	/// boarded at earlier stops; it is still left at its later stops. A value at or past the last stop leaves the train out.
	std::size_t firstStop = 0;
	/// Seconds added to every planned time from firstStop on; a train ahead of its plan keeps its planned times.
	double delay = 0.0;
};

/// One ride of a plan: a train from the stop where the passenger boards to the stop where the passenger leaves it.
struct Leg {
	std::size_t train = 0;
	std::size_t boardStop = 0;
	std::size_t alightStop = 0;
	double departure = kUnreached; ///< At boardStop, with the delay of the train.
	double arrival = kUnreached;   ///< At alightStop, with the delay of the train.
};

/// The rides from the origin to one destination, in travel order; empty when the destination is not reached or is the
/// origin.
struct Plan {
	std::vector<Leg> legs;
	double arrival = kUnreached;
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
	/// Keeps the timetables. A stop with a station outside stationCount is ignored.
	Router(std::vector<TrainTimetable> trains, std::size_t stationCount);

	std::size_t trainCount() const { return trains_.size(); }
	std::size_t stationCount() const { return stationCount_; }
	const std::vector<TrainTimetable>& trains() const { return trains_; }

	/// Searches from origin, where the passenger is ready at readyTime, with at most maxTransfers transfers. states holds
	/// one TrainState per train and must outlive the result, like the router. A transfer needs walkingSeconds between the
	/// platform of the arrival and that of the departure; the first boarding needs none.
	Search search(std::size_t origin, double readyTime, int maxTransfers, const std::vector<TrainState>& states) const;

private:
	std::vector<TrainTimetable> trains_;
	std::size_t stationCount_;
	/// For each station, the pairs of train and stop index where a train departs from it.
	std::vector<std::vector<std::pair<std::size_t, std::size_t>>> departures_;
};

} // namespace passenger_routing

#endif
