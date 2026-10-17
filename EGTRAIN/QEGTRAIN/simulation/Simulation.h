#ifndef Simulation_hpp
#define Simulation_hpp

#include "simulation/RollingStock.h"
#include "simulation/Passengers.h"
#include <vector>

// Function to Compute the Arrival and Departure times of at all the timetabling points along their own route
void Compute_TimetablingPoints_For_All_Trains(Regional* Trains, int numTrains);

// Updated Function to Calculate the arrival delay at each station for each train
void calculateArrivalDelayAllTrains(SimulationRun& run);

// Function to calculate positive an negative delays of trains for all the trains considered in the simulation
void calculatePosAndNegArrivalDelayAllTrains(SimulationRun& run);

// Function to calculate the train delay statistics for a single station instant_spatial_position
void calculateDelayStatsAtStation(SimulationRun& run, Stations& S);

// Function to Calculate the positive and negative delays stats at station instant_spatial_position
void calculatePosAndNegDelayStatsAtStation(SimulationRun& run, Stations& S);

// Function to Compute the amount of Disturbances set as input: Entrance delays, Cumulative disturbances to dwell times and Total delays (sum of entrance delays and disturbances to dwell times)
void Compute_Input_Delays(SimulationRun& run);

// Function to calculate the train delay statistics for all the station considered in the network
void calculateDelayStatsForAllStations(SimulationRun& run);

// Function to calculate positive and negative train delay statistics for all stations
void calculatePosAndNegDelayStatsForAllStations(SimulationRun& run);

// Computation times of EGTRAIN and ROMA
extern double& Comp_Time_EGTRAIN;
extern double& Comp_Time_ROMA;

// Function to print the files with the computing time of ROMA and EGTRAIN for each combination RI-PH
void Print_Computing_Times(const SimulationRun& run, std::string FolderName);


// Function to Print all the trajectories
void PrintTrainPathDiagram(const SimulationRun& run, Regional* S, int N_S, std::string FolderName);

// Function to compute Energy consumption for all the trains in the network
void ComputeEnergyConsumptionForAllTrains(Train* Trains, int numTrains);

// Function to Compute the Energy Consumption for the Timetable
void ComputeTimetableEnergyConsumption(Regional* Trains, int numTrains, std::string OutputFolder);

void checkJourneyStartForAllPassengers(int t, int StartingSimulationTime, std::list<Passenger>& SIMUL_PAX);

// The passengers waiting at one platform: how many they are and, for each of them, a pair of the passenger ID and the actual
// departure time of the trip the passenger waits for. The pairs are in chronological order of the departure time, and in the
// order of the passenger list for equal times.
struct WaitingAtPlatform {
	int count = 0;
	std::list<std::pair<std::string, double>> passengers;
};

// The passengers waiting at the platform with these IDs. No platform is modified.
WaitingAtPlatform passengersWaitingAtPlatform(const std::string& stationId, const std::string& platformId, const std::list<Passenger>& ALL_PAX);

// Sets the number and the list of the waiting passengers of every platform, in one pass over the passengers.
void Update_List_Passengers_Waiting_At_ALL_Platforms(std::list<StationPlatform>& ALL_PLAT, const std::list<Passenger>& ALL_PAX);

void Simulate_Train_Passenger_Interactions(SimulationRun& run, int t, int SimulationStartingTime, Train& T, std::list<Passenger>& ALLPAX, const std::list<StationPlatform>& ALLPLATFORMS);

#endif
