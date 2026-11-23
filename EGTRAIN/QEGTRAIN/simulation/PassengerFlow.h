#ifndef PASSENGERFLOW_H
#define PASSENGERFLOW_H

#include "simulation/Infrastructure.h"
#include "simulation/Passengers.h"
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct SimulationRun;
class Train;

/// The passengers waiting at one platform.
struct WaitingAtPlatform {
	int count = 0; ///< Number of waiting passengers.
	/// For each waiting passenger, the passenger ID and the Trip::Actual_Departure_Time of the trip the passenger waits for, which
	/// is when the passenger started to wait, in seconds since midnight. Sorted by that time, and in the order of the passenger
	/// list for equal times.
	std::list<std::pair<std::string, double>> passengers;
};

/// Where the passengers of one list are, kept up to date while the passengers move, so that a step looks only at the passengers
/// that it concerns instead of scanning the whole list:
/// - the journeys that are about to start, by start time;
/// - the passengers on board, by train and arrival station;
/// - the passengers waiting at each platform, by train, in the order in which they board: by the time from which they wait and
///   then by their place in the passenger list.
/// Passengers are named by their place in the list. The flow reads the state of the passengers when it is built and follows the
/// changes that the passenger step makes. A change made by other code needs build() again. A train only meets the passengers
/// who wait for it, each at its own place in the queue.
class PassengerFlow {
public:
	/// Registers the passengers of the list by their current state, and schedules the passengers who are outside the network and
	/// have a journey left.
	void build(std::list<Passenger>& passengers);
	/// Empties the flow.
	void clear();
	/// True when the flow was built for this list and the list has the size it had then.
	bool covers(const std::list<Passenger>& passengers) const;
	/// The time of the latest startDueJourneys call; the smallest int before the first.
	int lastTime() const { return lastTime_; }

	/// Starts the journey of every scheduled passenger whose start time is at or before now (seconds since midnight) and puts
	/// the passenger on the platform of the first leg. onStarted, when given, is called with the place of each passenger
	/// after the journey started and before the flow registers the passenger, so that it can plan the legs of a live journey.
	/// It changes the passenger only, not the flow.
	void startDueJourneys(int now, const std::function<void(int position)>& onStarted = {});

	/// Sets the number and the list of the waiting passengers of every platform that changed since the last call, and of every
	/// platform at the first call for this platform list.
	void refreshPlatformLists(std::list<StationPlatform>& platforms);

	/// The passengers waiting at the platform with these IDs, in boarding order.
	WaitingAtPlatform waitingAt(const std::string& stationId, const std::string& platformId) const;

	/// A passenger who waits for a train: from when, and who.
	struct Waiter {
		double since; ///< Trip::Actual_Departure_Time of the trip the passenger waits for.
		int position; ///< Place of the passenger in the list.
		int leg;	  ///< Order among the entries of one passenger, which only an odd journey list makes more than one.
		/// Boarding order: by the time from which the passenger waits, then place in the list, then leg.
		bool operator<(const Waiter& other) const;
	};

	/// The number of passengers waiting at the platform now, 0 for a platform nobody ever waited at.
	int waitingCount(const std::string& stationId, const std::string& platformId) const;
	/// The number that the flow gives the platform, or -1 when nobody ever waited there.
	int platformNumber(const std::string& stationId, const std::string& platformId) const { return findPlatformKey(stationId, platformId); }
	/// The number that the flow gives the train, or -1 when nobody ever waited for it or was on it.
	int trainNumber(const std::string& train) const { return findId(train); }
	/// The first passenger in boarding order after `after` (the first of all when it is empty) who waits at the platform for the
	/// train, or none. With onlyNotRefused it skips the passengers who were refused because the train was full, who stay in line
	/// but only matter when the train has room.
	std::optional<Waiter> nextWaiter(int platform, int train, const std::optional<Waiter>& after, bool onlyNotRefused) const;
	/// Notes that the passenger at this place was refused boarding because the train was full.
	void refused(int position);
	/// Notes that the train makes its call at stop `call` (an index of Train::Stations) at the platform. The passengers who were
	/// refused at an earlier call count as not refused again, so that this call refuses them in turn.
	void beginCall(int platform, int train, int call);
	/// The places of the passengers who wait at the platform for the train, in boarding order, one for each passenger.
	std::vector<int> waitingFor(int platform, int train) const;
	/// The places of the passengers who wait at the platform for any train, one for each passenger, in the order of the waiting lines.
	std::vector<int> waitingAt(int platform) const;
	/// Removes every passenger on board of the train from the flow and returns their places in list order, like
	/// takeOnBoardArrivingAt for all stations.
	std::vector<int> takeAllOnBoard(const std::string& train);
	/// Removes the passengers on board of the train who arrive at the station, in list order, from the flow and returns their
	/// places. The caller changes them and gives each back with placed().
	std::vector<int> takeOnBoardArrivingAt(const std::string& train, const std::string& station);
	/// The passenger at this place in the list.
	Passenger& passenger(int position) const { return *passengers_[static_cast<std::size_t>(position)]; }
	/// Registers the passenger at this place by its present state, after the passenger step changed it.
	void placed(int position);
	/// Like placed(), for a passenger who alighted: one who left the network is scheduled for the next journey.
	void alighted(int position);

private:
	enum class Kind : char { None,
		Waiting,
		OnBoard };
	struct Registration {
		Kind kind = Kind::None;
		int first = -1;				 // waiting: platform; on board: train
		int second = -1;			 // waiting: train; on board: arrival station
		std::vector<Waiter> waiters; // waiting only
		bool refused = false;		 // waiting only: refused boarding the train that the passenger waits for
	};
	struct TrainQueue {
		std::set<Waiter> all;
		std::set<Waiter> notRefused;
		int call = -1; // the stop of the train that the refusals of the passengers belong to; -1 before the first call
	};
	struct PlatformQueues {
		int count = 0;
		bool dirty = false;
		std::unordered_map<int, TrainQueue> byTrain;
	};
	struct JourneyStart {
		double due;
		int position;
		bool operator>(const JourneyStart& other) const { return due != other.due ? due > other.due : position > other.position; }
	};

	int platformKey(const std::string& stationId, const std::string& platformId);
	int findPlatformKey(const std::string& stationId, const std::string& platformId) const;
	int idOf(const std::string& name);
	int findId(const std::string& name) const;
	void unregister(int position);
	void registerState(int position);
	void markDirty(int platform);
	void scheduleJourneyStart(int position);
	WaitingAtPlatform listFor(int platform) const;
	void attachPlatforms(std::list<StationPlatform>& platforms);

	std::list<Passenger>* source_ = nullptr;
	std::size_t count_ = 0;
	int lastTime_ = INT32_MIN;
	std::vector<Passenger*> passengers_;
	std::vector<Registration> registrations_;

	std::vector<JourneyStart> journeyStarts_;  // a heap, the earliest start first
	std::unordered_map<std::string, int> ids_; // train descriptions and station names, numbered
	std::map<std::pair<std::string, std::string>, int> platformKeys_;
	std::vector<PlatformQueues> queues_;
	std::unordered_map<std::uint64_t, std::vector<int>> onBoard_;
	std::vector<int> dirtyPlatforms_;

	const std::list<StationPlatform>* platformsSource_ = nullptr;
	std::size_t platformsCount_ = 0;
	std::vector<std::vector<StationPlatform*>> platformsByKey_;
	std::vector<StationPlatform*> allPlatforms_;
	bool allPlatformsStale_ = true;
};

/// At the start of step t: the passengers whose journey starts enter the network and, with settings.PAX_GUI, the waiting lists of
/// the platforms are refreshed. The flow of the run is built or rebuilt here when it does not describe the passengers of the run.
void enterPassengersAndRefreshPlatforms(SimulationRun& run, int t);

/// Runs the passenger step of every train of the run in step t: alighting, boarding and the passenger-dependent dwell time.
void simulateTrainPassengerInteractions(SimulationRun& run, int t);

#endif // PASSENGERFLOW_H
