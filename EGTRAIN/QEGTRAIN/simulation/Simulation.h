#ifndef Simulation_hpp
#define Simulation_hpp

#include "simulation/RollingStock.h"
#include "simulation/Passengers.h"
#include <vector>

/// Computes the timetable points of the first numTrains trains of Trains (Train::ComputeTimetablingPoints).
void Compute_TimetablingPoints_For_All_Trains(Regional* Trains, int numTrains);

/// Sets the actual station arrivals and the arrival delays of every train (Train::Determine_Actual_Station_Arrivals and
/// Train::computeArrivalDelaysAtStations).
void calculateArrivalDelayAllTrains(SimulationRun& run);

/// Sets the arrival delays of every train again, with an early arrival as a negative delay
/// (Train::Compute_Pos_And_Neg_Arrival_Delays_At_Stations).
void calculatePosAndNegArrivalDelayAllTrains(SimulationRun& run);

/// Fills the delay statistics of station S from the trains with an arrival delay there, in seconds; the average and standard
/// deviation count only delayed trains, and the 3 and 5 minute counts compare with 180 and 300 s. The station "Final_Station" takes
/// the last stop of every train.
void calculateDelayStatsAtStation(SimulationRun& run, Stations& S);

/// The same as calculateDelayStatsAtStation with early arrivals: every train with an arrival and a planned arrival there counts, and
/// the average and standard deviation include delays of 0 and below.
void calculatePosAndNegDelayStatsAtStation(SimulationRun& run, Stations& S);

/// Fills run.totalInputDelays, run.entranceInputDelays and run.disturbanceInput with the statistics of the input delays of all trains
/// (s): the total, the entrance delay, and the dwell disturbances (the total less the entrance delay).
void Compute_Input_Delays(SimulationRun& run);

/// calculateDelayStatsAtStation for every station of run.infra.stations.
void calculateDelayStatsForAllStations(SimulationRun& run);

/// calculatePosAndNegDelayStatsAtStation for every station of run.infra.stations.
void calculatePosAndNegDelayStatsForAllStations(SimulationRun& run);

/// Writes FolderName/Computing_Times.txt with run.compTimeEgtrain (processor time of the steps, s), run.compTimeRoma and their sum.
/// Nothing outside the tests sets compTimeRoma, so its column is 0.
void Print_Computing_Times(const SimulationRun& run, std::string FolderName);


/// Writes FolderName/TrainPathDiagram.txt: a row of the step times (s), then for each of the N_S trains of S a row of its head
/// position (m, original coordinates) in every step, -9999 where it does not run.
void PrintTrainPathDiagram(const SimulationRun& run, Regional* S, int N_S, std::string FolderName);

/// Sets the energy totals (MJ) of each of the numTrains trains whose energy samples are complete, with a substation efficiency of 0.8
/// and a regeneration factor of 0.7 (Train::TotalEnergyConsumptionWithAndWithoutRegBraking). The totals of the other trains are 0.
void ComputeEnergyConsumptionForAllTrains(Train* Trains, int numTrains);

/// Writes EnergyConsumptionPerTrain.txt and TotalEnergyConsumption.txt to OutputFolder: the energy totals of each train and of all
/// trains, converted from MJ to kWh.
void ComputeTimetableEnergyConsumption(Regional* Trains, int numTrains, std::string OutputFolder);

/// Lets every passenger of SIMUL_PAX whose next journey starts by t + StartingSimulationTime (seconds since midnight) enter the
/// network (Passenger::checkJourneyStart).
void checkJourneyStartForAllPassengers(int t, int StartingSimulationTime, std::list<Passenger>& SIMUL_PAX);

/// The passengers waiting at one platform.
struct WaitingAtPlatform {
	int count = 0; ///< Number of waiting passengers.
	/// For each waiting passenger, the passenger ID and the Trip::Actual_Departure_Time of the trip the passenger waits for, which
	/// is when the passenger started to wait, in seconds since midnight. Sorted by that time, and in the order of the passenger
	/// list for equal times.
	std::list<std::pair<std::string, double>> passengers;
};

/// The passengers waiting at the platform with these IDs. No platform is modified.
WaitingAtPlatform passengersWaitingAtPlatform(const std::string& stationId, const std::string& platformId, const std::list<Passenger>& ALL_PAX);

/// Sets the number and the list of the waiting passengers of every platform, in one pass over the passengers.
void Update_List_Passengers_Waiting_At_ALL_Platforms(std::list<StationPlatform>& ALL_PLAT, const std::list<Passenger>& ALL_PAX);

/// At a service stop of train T in step t: the passengers whose trip ends there alight and transfer or leave the network, then the
/// passengers waiting for T board in the order of the platform while T has room. In the first step of the stop, the
/// passenger-dependent dwell replaces the stop time when it is longer. Passenger times are t + SimulationStartingTime (s since midnight).
void Simulate_Train_Passenger_Interactions(SimulationRun& run, int t, int SimulationStartingTime, Train& T, std::list<Passenger>& ALLPAX, const std::list<StationPlatform>& ALLPLATFORMS);

#endif
