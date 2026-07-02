#ifndef Simulation_hpp
#define Simulation_hpp

#include <QApplication>
#include <QEventLoop>
#include <QTimer>

#include "RollingStock.h"
#include "Passengers.h"
#include <vector>

// Function to draw stochastic Train Delays from a Gaussian Distribution, the St Dev of the delays Perc_Std_Dev must be expressed as a percentage of the scheduled dwell time at stations (e.g. if it is the 20% put Perc_Std_Dev=0.2)

void drawGaussianTrainDwellTimes(double Perc_Std_Dev, int index_scenario);

// Function to Generate Multiple disturbed scenarios with stochastic Dwell times
void Generate_Multiple_Train_Dwell_Times_From_Gaussian(int Number_of_Scenarios);

// Function to Load the Stochastic Dwell Times drawn from the Previous function and assign it to the StopTime Variable of the trains
void Load_And_Set_Stoc_Dwell_Times(char* FileName);

// Load Files of a given disturbed scenario identified by its index and saved in the Folder
void Load_Dwell_Time_Disturbed_Scenario(string Folder, int index);

// Function to Load Departure Delays from a station identified by the name which is given as input
void Load_Departure_Delay_At_Station(char* FileName, string stationName);

// Function to receive the entrance delays affected by an error distributed according to a Gaussian variable with standard deviation Std_Dev. Index is the Index of the scenario
void Generate_Entrance_Delays_Affected_By_Error(string Folder, double Std_Dev, int index);

// Function to Generate the Entrance Delays affected by Error for all the Scenarios
void Generate_Entrance_Delays_With_Error_For_All_Scenarios(string Folder, double Std_Dev, int Number_Of_Scenarios);

// Function to Load the Scenario of Entrance delays (from station stationName)identified by its index and saved in Folder
void Load_Entrance_Delay_Disturbed_Scenario(string Folder, string stationName, int index);

// Function to Detect the implemented Order for all the OL in the network
void Detect_Implemented_Order_For_All_OL();

// Function to Print the Implemented Order of all the OLs in a text file
void Print_Implemented_Order_For_All_OL(string FolderName);

// Function to Compute the Arrival and Departure times of at all the timetabling points along their own route
void Compute_TimetablingPoints_For_All_Trains(Train* Trains, int numTrains);

// Function to Calculate the arrival delay at each station for each train
void calculateArrivalDelayAllTrainsOldVersion();

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

void Print_Trajectories_Of_All_Trains_At_Instant(int t, string FolderName);

// Function to print the trajectory of the trains in a PNG image by using Powershell that activates a macro in the excel file TrajROMA-EGTRAIN
//  To use This script it is necessary that the ExecutionPolicy of PowerShell is put on RemoteSigned both for the 32 and the x86 (i.e. 64 bit) versions of Powershell
//  if it does not accept the function Set-ExecutionPolicy RemoteSigned, go in regedit-> HKEY LocalMAchine-> Microsoft-> PowerShell->ShellIDs and create a string variable called ExecutionPolicy whose value is RemoteSigned (do the same also for the Wow6432 version of the Powershell)
void Print_Trajectories_As_Image(string InstanceName, char* Resch_Int, char* Pred_Hor);

extern double Comp_Time_EGTRAIN, Comp_Time_ROMA; // variable to measure the computation times of EGTRAIN and ROMA

// Function to Simulate traffic within the observation period and without interactions wth the traffic management system
void trainSimulation(double v1, double v2, double v3);

////Function to simulate traffic in networks with a mixed signalling system (e.g. conventional, mixed to ETCS L1, L2 and or L3)
////Function to Simulate traffic within the observation period and without interactions wth the traffic management system
// void Train_Simulation_Mixed_Signalling(double v1, double v2, double v3);

// Function to simulate the trains in free flow to be used for computing the Headways
void TrainSimulationForComputingHW(double v1, double v2, double v3);

// Fast way of computing free-flow trajectories for HW and capacity computation
void ImprovedTrainSimulationForComputingHW(double v1, double v2, double v3);

extern int Resched_Interval;	 // Variable that set the time to gather train information from EGTRAIN to ROMA
extern int Time_To_Collect_Info; // Variable to measure the time passed from the last information update
extern double PH;				 // This is the Prediction Horizon Set in ROMA and must be used to initialize the ROMA batch call and the corresponding char
extern char Init_Time[20], Pred_Hor[20];
extern string InstanceName;		// This is the name of the instance that must be run in both ROMA and EGTRAIN
extern int InstanceIndex;		// This is the index of the instance
extern int DelayDispatcherImpl; // This is the delay in [s] with which the dispatcher implements the solution obtained from the ROMA tool*/

extern string Name_Of_Integ_Folder; // Name of the batch file of ROMA

// Function to print the files with the computing time of ROMA and EGTRAIN for each combination RI-PH
void Print_Computing_Times(string FolderName);

// Function to call and launch ROMA for computing a new rescheduling plan
void callRoma(string instancename, double inittime, double PH);

// Function to Implement the ROMA Solution in EGTRAIN
void Implement_ROMA_Solution(string InstanceName, int Instant_Sol_Returned, double PH);

// Function to Simulate EGTRAIN within a dynamic Interaction with the ROMA tool
void Train_Simulation_Integration_With_ROMA(double v1, double v2, double v3);

extern OrderList TrainEntranceOrder;

void SortOutOrderedTrainArray(Train* T, int numTrains, OrderList& TrainEntranceOrder);

// Function to compute the headways of all the trains
void ComputeHwMatrixForAllTrains(Train* T, int numTrains, string MainFolder);

// Function to compute the headways of all the trains with the following according to a train order given
void ComputeHwMatrixForAllTrainsWithGivenOrder(Train* T, int numTrains, string MainFolder, OrderList OrderedTrainArray);

// Function to compute the HW matrix of all the trains solving the conflicts among all the trains
void ComputeHwMatrixWithGivenOrderSolvingAllConflicts(Train* T, int numTrains, string MainFolder, OrderList OrderedTrainArray);

// Function to compute the critical headways for all the trains
void ComputeCriticalHeadwaysForLocationsForAllTrains(Train* T, int numTrains);

// Function to Initialize all the Locations in the infrastructure in order to Identify all the HWs
void SetAllLocations(Train* T, int N_Train);

// Function to Determine the Max and the min Headway for all locations
void DetermineMaxHW_MinHWForAllLocations(Train* T, int N_Train);

// Function to print out all the Results of a network Location
void PrintLocationHeadways(string MainFolder);

// Function to Initialize all the Locations in the Network, i.e. the Location list AllLocations
void InitializeAndComputeMaxHwForAllLocations(Train* T, int N_Train, string MainFolder);

// Function to Print all the trajectories
void PrintTrainPathDiagram(Train* S, int N_S, string FolderName);

// Function to Print all the trajectories
void PrintTrainPathDiagramToDebug(Train* S, int N_S, string FolderName);

/*
//Function to Print all the trajectories
void PrintCompressedTrainPathDiagram(Train *instant_spatial_position, int N_S, string FolderName) {

	int StartPrintingTime = 999999999; // this is the time the first train departs
	int EndPrintingTime = -10000000;
	for (int i = 0; i < N_S; i++) {
		if (instant_spatial_position[i].departure_time < StartPrintingTime) {
			StartPrintingTime = instant_spatial_position[i].departure_time;  // finding in this way the minimum train departing time as result from the blocking time compression process
		}
		if (instant_spatial_position[i].End_Time > EndPrintingTime) {  //finding the latest train exit time from simulation as result from the blocking time compression process
			EndPrintingTime = instant_spatial_position[i].End_Time+1+instant_spatial_position[i].departure_time-instant_spatial_position[i].RunStartTime;
		}
	}

	string FileName;
	FileName = FolderName + "/CompressedTrainPathDiagram.txt";
	ofstream FileOutput;
	FileOutput.open((char*)FileName.c_str(), ios::binary);
	FileOutput << "Train/Time ";
	for (int t = StartPrintingTime; t<=EndPrintingTime; t++) {
		FileOutput << t * timestep << " ";
	}
	FileOutput << "\n";

	//Shifting train trajectories
	for (int i = 0; i < N_S; i++) {
		//Printing out the names of trains
		FileOutput << instant_spatial_position[i].trainDescription << " ";
		int TrainShift = (int) instant_spatial_position[i].departure_time - (int) instant_spatial_position[i].RunStartTime;
		list<int> ShiftedTimes;
		list<double> TrainPositions;
		for (int t = 0; t <= times; t++) {
			ShiftedTimes.push_back(t + instant_spatial_position[i].departure_time);
			TrainPositions.push_back(instant_spatial_position[i].instant_spatial_position[t]);
		}

		bool StopPrinting = false;
		list<int>::iterator shiftTime = ShiftedTimes.begin();
		list<double>::iterator pos = TrainPositions.begin();
		for (int k = StartPrintingTime; k <= EndPrintingTime; k++) {
			if (ShiftedTimes.size() > 0) {
				if ((*shiftTime == k)&&(StopPrinting==0)) {
					if (train_route[instant_spatial_position[i].indexOfRoute].reversed_direction == 0) {
						FileOutput << *pos << " ";
					}
					else {
						FileOutput << train_route[instant_spatial_position[i].indexOfRoute].OriginalRefReversedRoute - *pos << " ";
					}
					shiftTime++;  //advancing the pointers to the lists
					pos++;
				}
				else {
					FileOutput << " ";
				}
				if (shiftTime == ShiftedTimes.end()) {
					StopPrinting = true; //break the for loop when the cicle reaches the end of the list
				}

			}
		}
		FileOutput << "\n";
	}
//Close the output file
	FileOutput.close();
}
*/
void PrintCompressedTrainPathDiagramTrial(Train* S, int N_S, string FolderName);

// Function to compute Energy consumption for all the trains in the network
void ComputeEnergyConsumptionForAllTrains(Train* Trains, int numTrains);

// Function to Compute the Energy Consumption for the Timetable
void ComputeTimetableEnergyConsumption(Train* Trains, int numTrains, string OutputFolder);


// Function to initialise all StationPlatforms for the simulation of passenger flows
// The function takes as input all the Array and number of all block sections, the array and number of all trains, the array of all defined rutes, as well as the length and width of the platforms
// Length and width of the platforms are assumed to be the same for all platforms in this function. If different dimensions needs to be assigned to each and every platform then the function should be extended with possibility to gather such a varying design from an external data input (manual entry or file)

void Initialise_All_Station_Platforms(list<StationPlatform>& ALLPLATFORMS, int& N_ALLPLATFORMS, Section* Blocks, int N_Blocks, Train* Trains, int numTrains, vector<Route> All_Routes, double platform_length, double platform_width);

void Update_List_Trains_Stopping_At_Platforms(list<StationPlatform>& ALLPLATFORMS, int& N_ALLPLATFORMS, Train* Trains, int numTrains, vector<Route> All_Routes);

void checkJourneyStartForAllPassengers(int t, int StartingSimulationTime, list<Passenger>& SIMUL_PAX);

void Update_List_Passengers_Waiting_At_Platform(StationPlatform& PLAT, list<Passenger> ALL_PAX);

void Update_List_Passengers_Waiting_At_ALL_Platforms(list<StationPlatform>& ALL_PLAT, list<Passenger> ALL_PAX);

void Simulate_Train_Passenger_Interactions(int t, int SimulationStartingTime, Train& T, list<Passenger>& ALLPAX, list<StationPlatform> ALLPLATFORMS);

#endif
