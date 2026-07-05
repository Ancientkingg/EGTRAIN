#ifndef Passengers_hpp
#define Passengers_hpp

#include "simulation/Infrastructure.h"
#include "simulation/RollingStock.h"

using namespace std;

// this is a class to specify a trip with no transfers from a given origin to destination in a given time frame of a passenger or freight item ( e.g. pallet, parcel, container)
class Trip {
public:
	string TripID, Dep_Station_ID, Arr_Station_ID, Trip_Activity_Type; // TripID is the ID of the Trip, DepStationID is the ID of the departing station, Arr_StationID is the ID of the arrival station while Trip_Activity_Type represents the motivation of the trip (e.g. Work, leisure, etc.)
	string JourneyID;												   // this is the ID of the journey the trip belongs to
	string Dep_Station_Platform_ID, Arr_Station_Platform_ID;		   // ID of the platforms used respectively at the departure and arrival stations of the trip
	string TrainServiceDescription;
	int Planned_Departure_Time, Planned_Arrival_Time, Actual_Departure_Time, Actual_Arrival_Time; // Planned times of departure and arrivals of the trip and Actual times of departure and arrival of the trip
	int totalArrivalDelay;
	bool IsTripStarted, IsTripCompleted; // These variables are true if the trip has started or ended respectively.

	// Class constructor
	Trip() {
		this->TripID = Dep_Station_ID = Arr_Station_ID = Trip_Activity_Type = TrainServiceDescription = "None";
		this->Dep_Station_Platform_ID = Arr_Station_Platform_ID = "None";
		this->Planned_Arrival_Time = Planned_Departure_Time = Actual_Departure_Time = Actual_Arrival_Time = totalArrivalDelay = -9999;
		IsTripCompleted = IsTripStarted = false;
	}
};

// this class is to specify a Journey which is composed of a collection of trips, hence might include multiple intermediate transfers to move from an origin to a destination
class Journey {
public:
	string ID, Dep_Station_ID, Arr_Station_ID, Journey_Activity_Type;												 // ID is the Identification of the Journey, DepStationID is the ID of the departing station, Arr_StationID is the ID of the arrival station while Journey_Activity_Type represents the motivation of the Journey(e.g. Work, leisure, etc.)
	string Dep_Station_Platform_ID, Arr_Station_Platform_ID;														 // ID of the platforms used respectively at the departure and arrival stations of the Journey
	double Planned_Departure_Time, Planned_Arrival_Time, Actual_Planned_Departure_Time, Actual_Planned_Arrival_Time; // Planned times of departure and arrivals of the trip and Actual times of departure and arrival of the journey
	double Actual_Departure_Time, Actual_Arrival_Time;																 // Actual Arrival Time and Actual Departure time are the simulated arrival and departure times of a journey for a passenger ( these are different from the Actual_Planned_Arrival_Times and the Actual_Planned_Departure_Times which instead randomise the Planned time window given by the DAS)
	int N_Trips;																									 // total number of Trips (or also called legs) which compose the Journey. The number of transfers is therefore obtained as N_Trips-1
	list<Trip> Trips;																								 // this is the list of Trips composing teh Journey
	bool IsJourneyStarted, IsJourneyCompleted;																		 // Variables are true respectively when the Journey has started or completed.
	double Walkingtime;																								 // this is the total walking time in the active journey
	double Waitingtime;																								 // this variables is the total waiting time spent by the passenger at the platform across theentire journey
	int totalJourneyArrivalDelay;
	// class constructor
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

class Passenger {
public:
	string ID, passenger_category;								   // The ID of the passenger and the category (which can be for instance, elder, minor, adult, or even reduced mobility, normal mobility, minor, or whatever other attribute relevant to the trip)
	string current_location_ID, current_TripID, current_JourneyID; // ID of the current location and the current train service where the passenger is at current time in simulation for a given Trip (TripID) in a Journey (JourneyID)
	list<Journey> Journeys;										   // This is the list of Journeys made by the passenger in one day (or other reference period of time)
	string CurrentStatus;										   // This variable described whether the passenger is onboard of a train or waiting at a platform after that it enterd the simulation.
						  // Current Status can assume value:
						  //"None" if it is not initialised and the passenger is out of the simulation (Not entered yet or exited)
						  //  "Onboard" if the passenger is onboard of a train
						  //  "OnPlatform" if the passenger is waiting at a station platform to board a train

	string Current_Train_To_Wait, Current_Train_Boarded, Current_Arrival_Station, Current_Arrival_Platform; // This is the name of the train that the passenger is boarding (id the CurrentStatus is "Onboard")
	string Current_WaitingStationPlatformID, Current_WaitingStationID;										// This is the ID of the station platform and the station ID where the passenger is waiting at (when the Currentstatus is "OnPlatform")
	bool IsIntheNetwork;																					// This variable is true when the passenger enters the simulation (i.e. simulation time >= Journey start time) and when the passenger has exited the simulation as it reached its Journey destination
	double TimeExitedTheNetwork;																			// Actual time the passenger left the network. Used by the Passenger GUI to display the passenger icon a few seconds after leaving the network
	string StationExitedTheNetworkID;																		// Station where the passenger left the network. Used by the Passenger GUI to display the passenger icon a few seconds after leaving the network
	string PlatformExitedTheNetworkID;																		// Platform where the passenger left the network. Used by the Passenger GUI to display the passenger icon a few seconds after leaving the network

	// Class constructor
	Passenger() {
		ID = passenger_category = current_location_ID = current_TripID = current_JourneyID = StationExitedTheNetworkID = PlatformExitedTheNetworkID = "None";
		CurrentStatus = "None";
		Current_Train_To_Wait = Current_Train_Boarded = Current_Arrival_Station = Current_Arrival_Platform = "None";
		Current_WaitingStationPlatformID = Current_WaitingStationID = "None";
		IsIntheNetwork = false;
		TimeExitedTheNetwork = -9999;
	}

	void checkJourneyStart(int t);
};

// Define a global list of passengers
extern list<Passenger> AllDailyPassengers; // This list ocntains all the passengers which appear in the network throughout the whole day
extern int numAllDailyPassengers;		   // This the overall number of passengers who will use the rail service across the whole day ( size of list AllDailyPassengers)


void initialiseAllDailyPassengersFromDailyActivitySchedule(string DASFileName, list<Passenger>& All_Pax, int& N_All_Pax);

void updatePassengerRouteChoice(string RCFileName, list<Passenger>& All_Pax, int& N_All_Pax);

void assignPlatformsToTripsInJourney(list<Passenger>& ALLPAXlist, list<StationPlatform> ALLPLATFORMS);

void printCurrentPassengerStatus(int t, int StartSimulationTime, list<Passenger> ALLPAX, string MainFolder);

void printPassengerTotalJourneyDelay(list<Passenger> ALLPAX, string MainFolder);

#endif
