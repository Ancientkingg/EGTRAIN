#ifndef Passengers_hpp
#define Passengers_hpp

#include "simulation/Infrastructure.h"
#include "simulation/RollingStock.h"
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>


/// Where a passenger is.
enum class PassengerStatus {
	Outside,	///< Outside the network: not entered yet, or arrived.
	OnPlatform, ///< Waiting on a platform for a train.
	OnBoard		///< On a train.
};

/// The name of a status as the output files, traces and the GUI write it: "None", "OnPlatform" or "OnBoard".
const char* passengerStatusName(PassengerStatus status);

/// What a passenger does at the destination of a journey, as the scene describes it.
enum class PassengerActivity {
	None,	 ///< The scene gives no activity.
	Home,	 ///< The activity text is "home" in any letter case.
	Work,	 ///< The activity text is "work" in any letter case.
	Leisure, ///< The activity text is "leisure" in any letter case.
	Other	 ///< Any other activity text.
};

/// The activity that a scene's free-text activity stands for: None for empty text, Home, Work or Leisure for those words in any
/// letter case, and Other for anything else.
PassengerActivity passengerActivityFromText(const std::string& text);

/// One leg of a journey: a ride on one train from one stop to another, without a transfer. Times are seconds since
/// midnight. Station "IDs" hold the station name, or the ID of a station without a name. Strings are "None" and numbers
/// -9999 until set.
class Trip {
public:
	/// ID of the leg in the scene.
	std::string TripID, Dep_Station_ID, Arr_Station_ID;
	/// \var Trip::Dep_Station_ID
	/// Station where the passenger boards.
	/// \var Trip::Arr_Station_ID
	/// Station where the passenger alights.

	std::string JourneyID; ///< this is the ID of the journey the trip belongs to
	/// ID of the platform where the passenger boards.
	std::string Dep_Station_Platform_ID, Arr_Station_Platform_ID; ///< ID of the platform where the passenger alights.
	int depPlatformNumber;										  ///< platformNumber of Dep_Station_Platform_ID, which the operations builder sets; 0 until then.
	std::string TrainServiceDescription;						  ///< trainDescription of the train of the leg.
	/// Planned departure; set on the first leg of a journey only, to the departure the journey drew from its window.
	int Planned_Departure_Time, Planned_Arrival_Time, Actual_Departure_Time, Actual_Arrival_Time;
	/// \var Trip::Planned_Arrival_Time
	/// Planned arrival; set on the last leg of a journey only, to the arrival the journey drew from its window.
	/// \var Trip::Actual_Departure_Time
	/// When the passenger starts to wait on the platform for the train: the start of the journey, or the arrival of the
	/// previous leg plus the walking time.
	/// \var Trip::Actual_Arrival_Time
	/// When the train with the passenger stopped at the arrival station.

	/// When the passenger boarded the train of the leg, in seconds since midnight; -9999 until the passenger boards.
	int Actual_Boarding_Time;
	int totalArrivalDelay; ///< Actual_Arrival_Time minus Planned_Arrival_Time in seconds.
	/// True once the passenger waits for this leg.
	bool IsTripStarted, IsTripCompleted; ///< True once the passenger has alighted at the arrival station.

	Trip() {
		this->TripID = Dep_Station_ID = Arr_Station_ID = TrainServiceDescription = kNoneId;
		this->Dep_Station_Platform_ID = Arr_Station_Platform_ID = kNoneId;
		this->depPlatformNumber = 0;
		this->Actual_Boarding_Time = -9999;
		this->Planned_Arrival_Time = Planned_Departure_Time = Actual_Departure_Time = Actual_Arrival_Time = totalArrivalDelay = -9999;
		IsTripCompleted = IsTripStarted = false;
	}
};

/// How a journey ended, or "InProgress" while it has not.
enum class JourneyOutcome {
	InProgress, ///< Not started, or on its way.
	Completed,	///< The passenger reached the destination.
	NoRoute		///< No train could take the passenger to the destination, so the passenger left the network where it was.
};

/// The name of an outcome as the observations and logs write it: "in_progress", "completed" or "no_route".
const char* journeyOutcomeName(JourneyOutcome outcome);

/// How the legs of a journey come about.
enum class JourneyMode {
	Assigned, ///< The scene gives the legs. They are followed until one cannot be taken, and then planned like a live journey.
	Live	  ///< The scene gives no legs: the passenger step plans them when the passenger appears.
};

/// A journey of a passenger from an origin to a destination: a list of legs (trips) with transfers between them. Times
/// and station IDs as in Trip; durations are seconds. Strings are "None" and times -9999 until set.
///
/// A journey without legs in the scene is live: when the passenger appears on the platform, the passenger step plans the
/// legs on the timetable of the moment (PassengerJourneys.h), and again whenever the passenger misses a train or another plan
/// arrives well before that of a late train. A journey with
/// legs follows them until one cannot be taken and is then planned like a live journey from the station where the passenger is.
class Journey {
public:
	/// ID of the journey in the scene.
	std::string ID, Dep_Station_ID, Arr_Station_ID;
	/// \var Journey::Dep_Station_ID
	/// Origin station.
	/// \var Journey::Arr_Station_ID
	/// Destination station.

	PassengerActivity Activity = PassengerActivity::None; ///< Activity of the journey, such as work or leisure.
	JourneyMode Mode = JourneyMode::Assigned;			  ///< Whether the scene gives the legs or the run plans them.

	/// Not set by the operations builder, so "None".
	std::string Dep_Station_Platform_ID, Arr_Station_Platform_ID; ///< Not set by the operations builder, so "None".
	/// Start of the planned departure window from the scene.
	double Planned_Departure_Time, Planned_Arrival_Time, Actual_Planned_Departure_Time, Actual_Planned_Arrival_Time;
	/// \var Journey::Planned_Arrival_Time
	/// Start of the planned arrival window from the scene.
	/// \var Journey::Actual_Planned_Departure_Time
	/// Departure drawn at random from the planned departure window. The passenger enters the network at this time.
	/// \var Journey::Actual_Planned_Arrival_Time
	/// Arrival drawn at random from the planned arrival window.

	/// When the passenger entered the network.
	double Actual_Departure_Time, Actual_Arrival_Time; ///< When the passenger alighted at the destination.
	int N_Trips;									   ///< Number of legs; the number of transfers is N_Trips - 1.
	std::list<Trip> Trips;							   ///< The legs in travel order.
	/// True once the passenger has entered the network for this journey.
	bool IsJourneyStarted, IsJourneyCompleted; ///< True once the passenger has reached the destination.
	/// True once the legs after the first plan of the journey were replaced: the passenger missed a train, was refused by a
	/// full one, was set down early by a train that ended its service, took a standing train that reaches the destination
	/// directly in time, or left a late or slowed train for a plan that arrives earlier.
	bool IsReplanned = false;
	JourneyOutcome Outcome = JourneyOutcome::InProgress; ///< Set when the journey completes or finds no route.
	int LiveTripCount = 0;								 ///< Number of legs the passenger step planned for this journey; the IDs of its legs count it.
	/// The trains that refused the passenger because they were full, as trainDescription and station. A later plan from that
	/// station leaves out the train while it stands there.
	std::vector<std::pair<std::string, std::string>> RefusedBy;
	double Walkingtime;			  ///< Total walking time between platforms at transfers in seconds.
	double Waitingtime;			  ///< Total waiting time on platforms in seconds.
	int totalJourneyArrivalDelay; ///< Arrival delay of the journey in seconds; 0 until the journey is completed.
	/// Planned arrival of the first plan of the journey: the last given leg, or the last leg of the first plan of the run. -9999
	/// until set; it is set once and a later plan does not change it.
	int FirstPlanArrival = -9999;

	/// The arrival delay of the journey that has arrived at `actualArrival`, when its last leg was `lastLegDelay` late. A journey
	/// that was planned again is measured against the arrival of its first plan, so that a replacement train does not move the
	/// reference. Any other journey has the delay of its last leg.
	int arrivalDelay(double actualArrival, int lastLegDelay) const {
		return IsReplanned && FirstPlanArrival != -9999 ? static_cast<int>(actualArrival - FirstPlanArrival) : lastLegDelay;
	}

	/// True when the journey is over, because the passenger arrived or no route was found.
	bool isFinished() const { return IsJourneyCompleted || Outcome == JourneyOutcome::NoRoute; }

	Journey() {
		ID = Dep_Station_ID = Arr_Station_ID = kNoneId;
		Dep_Station_Platform_ID = Arr_Station_Platform_ID = kNoneId;
		Planned_Departure_Time = Planned_Arrival_Time = Actual_Planned_Departure_Time = Actual_Planned_Arrival_Time = -9999;
		Actual_Departure_Time = Actual_Arrival_Time = -9999;

		Walkingtime = Waitingtime = totalJourneyArrivalDelay = 0;
		N_Trips = 0;
		IsJourneyCompleted = IsJourneyStarted = false;
	}
};

/// A passenger with the journeys it makes during the run. Times are seconds since midnight. The current state names the
/// journey, leg, station and train of the moment; each is "None" when it does not apply.
class Passenger {
public:
	/// ID of the passenger in the scene.
	std::string ID;
	/// Station where the passenger is or was last.
	std::string current_location_ID, current_TripID, current_JourneyID; ///< ID of the journey in progress.
	/// \var Passenger::current_TripID
	/// ID of the leg in progress.

	std::list<Journey> Journeys; ///< The journeys of the passenger in the scene.
	/// Where the passenger is.
	PassengerStatus CurrentStatus = PassengerStatus::Outside;

	/// trainDescription of the train the passenger waits for while "OnPlatform".
	std::string Current_Train_To_Wait, Current_Train_Boarded, Current_Arrival_Station, Current_Arrival_Platform;
	/// \var Passenger::Current_Train_Boarded
	/// trainDescription of the train the passenger is on while "OnBoard".
	/// \var Passenger::Current_Arrival_Station
	/// Station where the current leg ends.
	/// \var Passenger::Current_Arrival_Platform
	/// Platform where the first leg ends; a transfer does not update it.

	/// ID of the platform the passenger waits on while "OnPlatform".
	std::string Current_WaitingStationPlatformID, Current_WaitingStationID; ///< Station the passenger waits at while "OnPlatform".
	/// True from the start of a journey until the passenger reaches its destination; false before and after.
	bool IsIntheNetwork;
	/// Actual time the passenger left the network; -9999 before that.
	double TimeExitedTheNetwork;
	/// Station where the passenger left the network.
	std::string StationExitedTheNetworkID;
	/// Platform where the passenger left the network.
	std::string PlatformExitedTheNetworkID;
	/// trainDescription of the last train that was full when the passenger wanted to board it; "None" before that. The
	/// refusal is logged once per call of that train, not at every step of its stop.
	std::string TrainThatRefusedBoarding = kNoneId;
	/// Index of the stop of that train (Train::Stations) at which the refusal happened; -1 before that. A later call of the
	/// same train refuses the passenger again.
	int StopThatRefusedBoarding = -1;

	Passenger() {
		ID = current_location_ID = current_TripID = current_JourneyID = StationExitedTheNetworkID = PlatformExitedTheNetworkID = kNoneId;
		Current_Train_To_Wait = Current_Train_Boarded = Current_Arrival_Station = Current_Arrival_Platform = kNoneId;
		Current_WaitingStationPlatformID = Current_WaitingStationID = kNoneId;
		IsIntheNetwork = false;
		TimeExitedTheNetwork = -9999;
	}

	/// While the passenger is outside the network, starts each uncompleted journey whose drawn departure time has come and
	/// puts the passenger on the departure platform of its first leg. t is the time in seconds since midnight.
	void checkJourneyStart(int t);
};

/// Passengers by ID, in list order, for one passenger list. The index points into the list, so it describes the list only
/// while the list keeps the same elements: a list that was cleared or replaced needs a new index.
class PassengerIndex {
public:
	/// Indexes the list, replacing what the index held.
	void build(std::list<Passenger>& passengers);
	/// Empties the index.
	void clear();
	/// True when the index was built for this list and the list has the size it had then.
	bool covers(const std::list<Passenger>& passengers) const;
	/// The passengers with this ID in list order. There are several only when an ID repeats.
	const std::vector<Passenger*>& find(const std::string& id) const;

private:
	const std::list<Passenger>* source_ = nullptr;
	size_t count_ = 0;
	std::unordered_map<std::string, std::vector<Passenger*>> byId_;
};

/// Station platforms by station ID and platform ID, for one platform list, with the same rules as PassengerIndex.
class PlatformIndex {
public:
	/// Indexes the list, replacing what the index held.
	void build(const std::list<StationPlatform>& platforms);
	/// Empties the index.
	void clear();
	/// True when the index was built for this list and the list has the size it had then.
	bool covers(const std::list<StationPlatform>& platforms) const;
	/// The first platform in list order with these IDs, or null.
	const StationPlatform* find(const std::string& stationId, const std::string& platformId) const;

private:
	const std::list<StationPlatform>* source_ = nullptr;
	size_t count_ = 0;
	std::unordered_map<std::string, std::unordered_map<std::string, const StationPlatform*>> byId_;
};

/// Writes MainFolder/PassengerStatus.txt: the passengers in the network at step t, with the time t + StartSimulationTime (s).
/// Throws std::runtime_error when the file cannot be written (simulation/RunOutputFile.h).
void printCurrentPassengerStatus(int t, int StartSimulationTime, const std::list<Passenger>& ALLPAX, const std::string& MainFolder);

/// Writes MainFolder/JourneyDelays.txt: the total arrival delay (s) of every journey of every passenger. Throws
/// std::runtime_error when the file cannot be written (simulation/RunOutputFile.h).
void printPassengerTotalJourneyDelay(const std::list<Passenger>& ALLPAX, const std::string& MainFolder);

/// The number of a platform, which the walking time of a transfer compares: the first "Platform_" in platformId is cut out and what is
/// left is read as a number with atof and truncated. "Platform_4" gives 4 and "2" gives 2. An ID that does not then start with a
/// number, such as "None", "P1" or "B.platform.1", gives 0. An ID read as NaN, infinity or a value outside int also gives 0, such as
/// "Nantes.platform.1", "INF.platform.2" or "Platform_1e30".
int platformNumber(const std::string& platformId);

#endif
