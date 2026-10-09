#ifndef Simulation_hpp
#define Simulation_hpp

#include "simulation/RollingStock.h"
#include "simulation/Passengers.h"
#include <vector>

// Function to Detect the implemented Order for all the OL in the network
void Detect_Implemented_Order_For_All_OL();

// Function to Print the Implemented Order of all the OLs in a text file
void Print_Implemented_Order_For_All_OL(string FolderName);

// Function to Compute the Arrival and Departure times of at all the timetabling points along their own route
void Compute_TimetablingPoints_For_All_Trains(Regional* Trains, int numTrains);

// Updated Function to Calculate the arrival delay at each station for each train
void calculateArrivalDelayAllTrains();

// Function to calculate positive an negative delays of trains for all the trains considered in the simulation
void calculatePosAndNegArrivalDelayAllTrains();

// Function to calculate the train delay statistics for a single station instant_spatial_position
void calculateDelayStatsAtStation(Stations& S);

// Function to Calculate the positive and negative delays stats at station instant_spatial_position
void calculatePosAndNegDelayStatsAtStation(Stations& S);

// Function to Compute the amount of Disturbances set as input: Entrance delays, Cumulative disturbances to dwell times and Total delays (sum of entrance delays and disturbances to dwell times)
void Compute_Input_Delays();

// Function to calculate the train delay statistics for all the station considered in the network
void calculateDelayStatsForAllStations();

// Function to calculate positive and negative train delay statistics for all stations
void calculatePosAndNegDelayStatsForAllStations();

extern double Comp_Time_EGTRAIN, Comp_Time_ROMA; // variable to measure the computation times of EGTRAIN and ROMA

// Function to print the files with the computing time of ROMA and EGTRAIN for each combination RI-PH
void Print_Computing_Times(string FolderName);


// Function to Print all the trajectories
void PrintTrainPathDiagram(Regional* S, int N_S, string FolderName);

// Function to compute Energy consumption for all the trains in the network
void ComputeEnergyConsumptionForAllTrains(Train* Trains, int numTrains);

// Function to Compute the Energy Consumption for the Timetable
void ComputeTimetableEnergyConsumption(Regional* Trains, int numTrains, string OutputFolder);

void checkJourneyStartForAllPassengers(int t, int StartingSimulationTime, list<Passenger>& SIMUL_PAX);

void Update_List_Passengers_Waiting_At_Platform(StationPlatform& PLAT, list<Passenger> ALL_PAX);

void Update_List_Passengers_Waiting_At_ALL_Platforms(list<StationPlatform>& ALL_PLAT, list<Passenger> ALL_PAX);

void Simulate_Train_Passenger_Interactions(int t, int SimulationStartingTime, Train& T, list<Passenger>& ALLPAX, const list<StationPlatform>& ALLPLATFORMS);

#endif
