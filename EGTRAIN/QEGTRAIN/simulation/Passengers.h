#ifndef Passengers_hpp
#define Passengers_hpp

#include "simulation/Infrastructure.h"
#include "simulation/RollingStock.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <vector>


/// One leg of a journey: a ride on one train from one stop to another, without a transfer. Times are seconds since
/// midnight. Station "IDs" hold the station name, or the ID of a station without a name. Strings are "None" and numbers
/// -9999 until set.
class Trip {
public:
	/// ID of the leg in the scene.
	std::string TripID, Dep_Station_ID, Arr_Station_ID, Trip_Activity_Type; ///< Activity of the journey, such as work or leisure.
	/// \var Trip::Dep_Station_ID
	/// Station where the passenger boards.
	/// \var Trip::Arr_Station_ID
	/// Station where the passenger alights.

	std::string JourneyID; ///< this is the ID of the journey the trip belongs to
	/// ID of the platform where the passenger boards.
	std::string Dep_Station_Platform_ID, Arr_Station_Platform_ID; ///< ID of the platform where the passenger alights.
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

	int totalArrivalDelay; ///< Actual_Arrival_Time minus Planned_Arrival_Time in seconds.
	/// True once the passenger waits for this leg.
	bool IsTripStarted, IsTripCompleted; ///< True once the passenger has alighted at the arrival station.

	/// Sets the strings to "None", the times to -9999 and the flags to false.
	Trip() {
		this->TripID = Dep_Station_ID = Arr_Station_ID = Trip_Activity_Type = TrainServiceDescription = "None";
		this->Dep_Station_Platform_ID = Arr_Station_Platform_ID = "None";
		this->Planned_Arrival_Time = Planned_Departure_Time = Actual_Departure_Time = Actual_Arrival_Time = totalArrivalDelay = -9999;
		IsTripCompleted = IsTripStarted = false;
	}
};

/// A journey of a passenger from an origin to a destination: a list of legs (trips) with transfers between them. Times
/// and station IDs as in Trip; durations are seconds. Strings are "None" and times -9999 until set.
class Journey {
public:
	/// ID of the journey in the scene.
	std::string ID, Dep_Station_ID, Arr_Station_ID, Journey_Activity_Type; ///< Activity of the journey, such as work or leisure.
	/// \var Journey::Dep_Station_ID
	/// Origin station.
	/// \var Journey::Arr_Station_ID
	/// Destination station.

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
	double Walkingtime;						   ///< Total walking time between platforms at transfers in seconds.
	double Waitingtime;						   ///< Total waiting time on platforms in seconds.
	int totalJourneyArrivalDelay;			   ///< Arrival delay of the last leg in seconds; 0 until the journey is completed.

	/// Sets the strings to "None", the times to -9999, the totals to 0 and the flags to false.
	Journey() {
		ID = Dep_Station_ID = Arr_Station_ID = Journey_Activity_Type = "None";
		Dep_Station_Platform_ID = Arr_Station_Platform_ID = "None";
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
	std::string ID, passenger_category; ///< Not set by the operations builder, so "None".
	/// Station where the passenger is or was last.
	std::string current_location_ID, current_TripID, current_JourneyID; ///< ID of the journey in progress.
	/// \var Passenger::current_TripID
	/// ID of the leg in progress.

	std::list<Journey> Journeys; ///< The journeys of the passenger in the scene.
	/// Where the passenger is: "None" outside the network (not entered yet, or arrived), "OnPlatform" while waiting on a
	/// platform, "OnBoard" on a train.
	std::string CurrentStatus;

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
	/// Actual time the passenger left the network; -9999 before that. Used by the Passenger GUI to display the
	/// passenger icon a few seconds after leaving the network
	double TimeExitedTheNetwork;
	/// Station where the passenger left the network. Used by the Passenger GUI to display the passenger icon a few
	/// seconds after leaving the network
	std::string StationExitedTheNetworkID;
	/// Platform where the passenger left the network. Used by the Passenger GUI to display the passenger icon a few
	/// seconds after leaving the network
	std::string PlatformExitedTheNetworkID;

	/// Sets the strings to "None", IsIntheNetwork to false and TimeExitedTheNetwork to -9999.
	Passenger() {
		ID = passenger_category = current_location_ID = current_TripID = current_JourneyID = StationExitedTheNetworkID = PlatformExitedTheNetworkID = "None";
		CurrentStatus = "None";
		Current_Train_To_Wait = Current_Train_Boarded = Current_Arrival_Station = Current_Arrival_Platform = "None";
		Current_WaitingStationPlatformID = Current_WaitingStationID = "None";
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

void printCurrentPassengerStatus(int t, int StartSimulationTime, const std::list<Passenger>& ALLPAX, std::string MainFolder);

void printPassengerTotalJourneyDelay(const std::list<Passenger>& ALLPAX, std::string MainFolder);

nlohmann::json routeChoicePayload(const std::list<Passenger>& passengers, int timestep);

#endif
