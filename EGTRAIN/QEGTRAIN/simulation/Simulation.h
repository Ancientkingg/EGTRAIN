#ifndef Simulation_hpp
#define Simulation_hpp

#include "simulation/RollingStock.h"
#include "simulation/Passengers.h"
#include <functional>
#include <vector>

/// Computes the timetable points of the first numTrains trains of Trains (Train::ComputeTimetablingPoints).
void Compute_TimetablingPoints_For_All_Trains(Train* Trains, int numTrains);

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
void PrintTrainPathDiagram(const SimulationRun& run, Train* S, int N_S, std::string FolderName);

/// Sets the energy totals (MJ) of each of the numTrains trains whose energy samples are complete, with a substation efficiency of 0.8
/// and a regeneration factor of 0.7 (Train::TotalEnergyConsumptionWithAndWithoutRegBraking). The totals of the other trains are 0.
void ComputeEnergyConsumptionForAllTrains(Train* Trains, int numTrains);

/// Writes EnergyConsumptionPerTrain.txt and TotalEnergyConsumption.txt to OutputFolder: the energy totals of each train and of all
/// trains, converted from MJ to kWh.
void ComputeTimetableEnergyConsumption(Train* Trains, int numTrains, std::string OutputFolder);

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

/// The order in which the trains of a run move in its steps. The loop that steps a run makes one before the first step and passes
/// it to every step.
struct TrainMovementOrder {
	/// The trains of run by departure time; trains due at the same time stay in the order of the trains of the run.
	explicit TrainMovementOrder(const SimulationRun& run);
	/// The indexes of the trains by departure time.
	std::vector<int> dueOrder;
	/// The order of the trains in the current step. It is kept between steps so that each step reuses its storage.
	std::vector<int> movementOrder;
};

/// Runs step t of run, in seconds from the start of the run. The passengers whose journey starts enter the network and, with
/// settings.PAX_GUI, the waiting lists of the platforms are refreshed. Every train then moves, in the order of the trains of the run
/// except that the trains that are due and still waiting to enter take those places in the order in which they are due, so the
/// one due first enters first. The trains and passengers interact, the signalling routines set the sections and the authorities
/// for the next step, and the processor time of the step is added to run.compTimeEgtrain. order belongs to the run and keeps its
/// due order for every step.
void runSimulationStep(SimulationRun& run, int t, TrainMovementOrder& order);

/// Runs the stages that follow the steps of run, in this order: the energy totals and their two files, the sort of the recorded
/// events of the infrastructure, the timetable points, the delays of the trains with the statistics of the stations and the input
/// delays and their files, the passenger delay file, the computing times, the blocking times, the path diagram with
/// settings.exportDetailedTrajectories, the timetable points file and, with that setting too, the file of each train. The folders
/// TrainTrajectories and PassengerStatus of the output folder must exist; the folder TEMP is made here, and a failure to make it
/// throws std::runtime_error.
///
/// stopRequested is called after the energy stage, after the sort, before the blocking times, before the files of the trains and
/// before the file of each train, and the stages end there when it returns true. The caller checks for a stop before this
/// function. Returns true when every stage ran and false when a stop ended them.
bool runSimulationPostprocessing(SimulationRun& run, const std::function<bool()>& stopRequested);

#endif
