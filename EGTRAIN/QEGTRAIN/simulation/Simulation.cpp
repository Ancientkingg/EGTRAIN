#include "simulation/Simulation.h"
#include <cstdio>
#include <vector>

double Comp_Time_EGTRAIN = 0, Comp_Time_ROMA = 0; // variable to measure the computation times of EGTRAIN and ROMA

int Resched_Interval = 300;	  // Variable that set the time to gather train information from EGTRAIN to ROMA
int Time_To_Collect_Info = 0; // Variable to measure the time passed from the last information update
double PH = 1800;			  // This is the Prediction Horizon Set in ROMA and must be used to initialize the ROMA batch call and the corresponding char
char Init_Time[20], Pred_Hor[20];
string InstanceName = "E1";	 // This is the name of the instance that must be run in both ROMA and EGTRAIN
int InstanceIndex = 1;		 // This is the index of the instance
int DelayDispatcherImpl = 0; // This is the delay in [s] with which the dispatcher implements the solution obtained from the ROMA tool*/
extern InitialParameters initial_variables;

string Name_Of_Integ_Folder = ""; // Name of the batch file of ROMA

OrderList TrainEntranceOrder;

// Function to Calculate the arrival delay at each station for each train
void calculateArrivalDelayAllTrainsOldVersion() {
	for (int j = 0; j < numRegions; j++) {
		// regional_train[j].Actual_Arrivals();
		regional_train[j].Actual_Arrivals_NewVersion();
		regional_train[j].computeArrivalDelaysAtStations();
	}
}

// Updated Function to Calculate the arrival delay at each station for each train
void calculateArrivalDelayAllTrains() {
	for (int j = 0; j < numRegions; j++) {
		// Determine the actual arrivals at the train stations
		regional_train[j].Determine_Actual_Station_Arrivals();
		// Compute the arrival delays
		regional_train[j].computeArrivalDelaysAtStations();
	}
}

// Function to calculate the train delay statistics for a single station instant_spatial_position
void calculateDelayStatsAtStation(Stations& S) {
	std::vector<double> TrainDelay;
	std::vector<double> TrainConsDelay;
	S.N_Stopped_Trains = 0;						// Number of trains that passed station instant_spatial_position during the simulation
	S.Av_Arrival_Delay = S.Std_Arrival_Delay = 0;
	S.N_Delayed_Arr = S.N_Delayed_Arr_3min = S.N_Delayed_Arr_5min = 0; // Initializing Station parameters to 0 since these values will be calculated for station instant_spatial_position
	S.Perc_Delayed_T = S.Perc_Delayed_T_3min = S.Perc_Delayed_T_5min = 0;
	S.Tot_Consec_Delay = 0;

	if (S.stationName != "Final_Station") {
		for (int j = 0; j < numRegions; j++) {															  // looping over the total number of trains
			for (int s = 0; s < regional_train[j].numStations; s++) {									  // looping over their stations
				if (regional_train[j].stationNameForArrivalStats(s) == S.stationName) {					  // if the train has a stop at station instant_spatial_position
					if (regional_train[j].StationDelay[s] != -1) {									  // and the train has actually crossed that station within the simulation time
						TrainDelay.push_back(regional_train[j].StationDelay[s]);			  // register its arrival delay
						TrainConsDelay.push_back(regional_train[j].StationConsecDelay[s]); // register its consecutive delay
						if (TrainDelay[S.N_Stopped_Trains] > 0)
							S.N_Delayed_Arr++; // increase the number of delayed trains if it is delayed
						if (TrainDelay[S.N_Stopped_Trains] > 3 * 60 / timestep)
							S.N_Delayed_Arr_3min++; // increase this number if its delay>3 min
						if (TrainDelay[S.N_Stopped_Trains] > 5 * 60 / timestep)
							S.N_Delayed_Arr_5min++; // increase this number if its delay>5 min

						S.N_Stopped_Trains++; // increase the number of trains that crossed station instant_spatial_position
					}
				}
			}
		}
	} else {																												// This part is executed only if the station to Analyze is the fittitious one: Final_Delay that describes the statics of the train at their own final station
		for (int j = 0; j < numRegions; j++) {																					// looping over the total number of trains
			if (regional_train[j].StationDelay[regional_train[j].numStations - 1] != -1) {									// and the train has actually crossed that station within the simulation time
				TrainDelay.push_back(regional_train[j].StationDelay[regional_train[j].numStations - 1]);			// register its arrival delay
				TrainConsDelay.push_back(regional_train[j].StationConsecDelay[regional_train[j].numStations - 1]); // register its consecutive delay
				if (TrainDelay[S.N_Stopped_Trains] > 0)
					S.N_Delayed_Arr++; // increase the number of delayed trains if it is delayed
				if (TrainDelay[S.N_Stopped_Trains] > 3 * 60 / timestep)
					S.N_Delayed_Arr_3min++; // increase this number if its delay>3 min
				if (TrainDelay[S.N_Stopped_Trains] > 5 * 60 / timestep)
					S.N_Delayed_Arr_5min++; // increase this number if its delay>5 min

				S.N_Stopped_Trains++; // increase the number of trains that crossed station instant_spatial_position
			}
		}
	}
	// Calculate the Total and the Average Arrival Delay at station instant_spatial_position
	for (int r = 0; r < S.N_Stopped_Trains; r++) {
		S.Av_Arrival_Delay = S.Av_Arrival_Delay + TrainDelay[r];
	}
	S.totalArrivalDelay = S.Av_Arrival_Delay;				   // Calculating the total arrival delay (sum of train delays over all trains stopping at station instant_spatial_position)
	S.Av_Arrival_Delay = S.Av_Arrival_Delay / S.N_Delayed_Arr; // Calculating the average arrival delay over the delayed trains

	// Calculate the Standard Deviation of Delay at station instant_spatial_position over the delayed trains
	for (int r = 0; r < S.N_Stopped_Trains; r++) {
		if (TrainDelay[r] != 0) {
			S.Std_Arrival_Delay = S.Std_Arrival_Delay + pow((TrainDelay[r] - S.Av_Arrival_Delay), 2);
		}
	}
	S.Std_Arrival_Delay = sqrt(S.Std_Arrival_Delay / (S.N_Delayed_Arr - 1));

	// Calculate the Percentages of Delayed trains and Total Consecutive delay at that station
	for (int r = 0; r < S.N_Stopped_Trains; r++) {
		S.Perc_Delayed_T = (double)S.N_Delayed_Arr / S.N_Stopped_Trains * 100;			 // Percentage of delayed trains
		S.Perc_Delayed_T_3min = (double)S.N_Delayed_Arr_3min / S.N_Stopped_Trains * 100; // Percentage of delayed trains more than 3 min
		S.Perc_Delayed_T_5min = (double)S.N_Delayed_Arr_5min / S.N_Stopped_Trains * 100; // Percentage of delayed trains more than 5 min
		S.Tot_Consec_Delay = S.Tot_Consec_Delay + TrainConsDelay[r];					 // Calculate Total consecutive delay
	}

	// Calculate the MaxConsecutive Delay and the Max Total Delay
	for (int r = 0; r < S.N_Stopped_Trains; r++) {
		if (TrainDelay[r] > S.Max_TotalDelay)
			S.Max_TotalDelay = TrainDelay[r];
		if (TrainConsDelay[r] > S.Max_Cons_Delay)
			S.Max_Cons_Delay = TrainConsDelay[r];
	}

}

// Function to Calculate the positive and negative delays stats at station instant_spatial_position
void calculatePosAndNegDelayStatsAtStation(Stations& S) {
	std::vector<double> TrainDelay;
	std::vector<double> TrainConsDelay;
	S.N_Stopped_Trains = 0;						// Number of trains that passed station instant_spatial_position during the simulation
	S.Av_Arrival_Delay = S.Std_Arrival_Delay = 0;
	S.N_Delayed_Arr = S.N_Delayed_Arr_3min = S.N_Delayed_Arr_5min = 0; // Initializing Station parameters to 0 since these values will be calculated for station instant_spatial_position
	S.Perc_Delayed_T = S.Perc_Delayed_T_3min = S.Perc_Delayed_T_5min = 0;
	S.Tot_Consec_Delay = 0;

	if (S.stationName != "Final_Station") {
		for (int j = 0; j < numRegions; j++) {															  // looping over the total number of trains
			for (int s = 0; s < regional_train[j].numStations; s++) {									  // looping over their stations
				if (regional_train[j].stationNameForArrivalStats(s) == S.stationName) {					  // if the train has a stop at station instant_spatial_position
					if (regional_train[j].StationDelay[s] != -1) {									  // and the train has actually crossed that station within the simulation time
						TrainDelay.push_back(regional_train[j].StationDelay[s]);			  // register its arrival delay
						TrainConsDelay.push_back(regional_train[j].StationConsecDelay[s]); // register its consecutive delay
						if (TrainDelay[S.N_Stopped_Trains] > 0)
							S.N_Delayed_Arr++; // increase the number of delayed trains if it is delayed
						if (TrainDelay[S.N_Stopped_Trains] > 3 * 60 / timestep)
							S.N_Delayed_Arr_3min++; // increase this number if its delay>3 min
						if (TrainDelay[S.N_Stopped_Trains] > 5 * 60 / timestep)
							S.N_Delayed_Arr_5min++; // increase this number if its delay>5 min

						S.N_Stopped_Trains++; // increase the number of trains that crossed station instant_spatial_position
					}
				}
			}
		}
	} else {																												// This part is executed only if the station to Analyze is the fittitious one: Final_Delay that describes the statics of the train at their own final station
		for (int j = 0; j < numRegions; j++) {																					// looping over the total number of trains
			if (regional_train[j].StationDelay[regional_train[j].numStations - 1] != -1) {									// and the train has actually crossed that station within the simulation time
				TrainDelay.push_back(regional_train[j].StationDelay[regional_train[j].numStations - 1]);			// register its arrival delay
				TrainConsDelay.push_back(regional_train[j].StationConsecDelay[regional_train[j].numStations - 1]); // register its consecutive delay
				if (TrainDelay[S.N_Stopped_Trains] > 0)
					S.N_Delayed_Arr++; // increase the number of delayed trains if it is delayed
				if (TrainDelay[S.N_Stopped_Trains] > 3 * 60 / timestep)
					S.N_Delayed_Arr_3min++; // increase this number if its delay>3 min
				if (TrainDelay[S.N_Stopped_Trains] > 5 * 60 / timestep)
					S.N_Delayed_Arr_5min++; // increase this number if its delay>5 min

				S.N_Stopped_Trains++; // increase the number of trains that crossed station instant_spatial_position
			}
		}
	}
	// Calculate the Total and the Average Arrival Delay at station instant_spatial_position
	for (int r = 0; r < S.N_Stopped_Trains; r++) {
		S.Av_Arrival_Delay = S.Av_Arrival_Delay + TrainDelay[r];
	}
	S.totalArrivalDelay = S.Av_Arrival_Delay;					  // Calculating the total arrival delay (sum of train delays over all trains stopping at station instant_spatial_position)
	S.Av_Arrival_Delay = S.Av_Arrival_Delay / S.N_Stopped_Trains; // Calculating the average arrival delay over the delayed trains

	// Calculate the Standard Deviation of Delay at station instant_spatial_position over the delayed trains
	for (int r = 0; r < S.N_Stopped_Trains; r++) {
		S.Std_Arrival_Delay = S.Std_Arrival_Delay + pow((TrainDelay[r] - S.Av_Arrival_Delay), 2);
	}
	S.Std_Arrival_Delay = sqrt(S.Std_Arrival_Delay / (S.N_Stopped_Trains - 1));

	// Calculate the Percentages of Delayed trains and Total Consecutive delay at that station
	for (int r = 0; r < S.N_Stopped_Trains; r++) {
		S.Perc_Delayed_T = (double)S.N_Delayed_Arr / S.N_Stopped_Trains * 100;			 // Percentage of delayed trains
		S.Perc_Delayed_T_3min = (double)S.N_Delayed_Arr_3min / S.N_Stopped_Trains * 100; // Percentage of delayed trains more than 3 min
		S.Perc_Delayed_T_5min = (double)S.N_Delayed_Arr_5min / S.N_Stopped_Trains * 100; // Percentage of delayed trains more than 5 min
		S.Tot_Consec_Delay = S.Tot_Consec_Delay + TrainConsDelay[r];					 // Calculate Total consecutive delay
	}

	// Calculate the MaxConsecutive Delay and the Max Total Delay
	for (int r = 0; r < S.N_Stopped_Trains; r++) {
		if (TrainDelay[r] > S.Max_TotalDelay)
			S.Max_TotalDelay = TrainDelay[r];
		if (TrainConsDelay[r] > S.Max_Cons_Delay)
			S.Max_Cons_Delay = TrainConsDelay[r];
	}

}

// Function to Compute the amount of Disturbances set as input: Entrance delays, Cumulative disturbances to dwell times and Total delays (sum of entrance delays and disturbances to dwell times)
void Compute_Input_Delays() {
	std::vector<double> TrainDelay(numRegions);
	std::vector<double> TrainEntDelay(numRegions);
	std::vector<double> Disturb(numRegions);
										 // Initializing parameters for Total input delays
	TotalInputDelays.N_Stopped_Trains = numRegions; // Initializing the number of trains to consider: in this case they are all the trains considered in the network
	TotalInputDelays.Av_Arrival_Delay = TotalInputDelays.Std_Arrival_Delay = 0;
	TotalInputDelays.N_Delayed_Arr = TotalInputDelays.N_Delayed_Arr_3min = TotalInputDelays.N_Delayed_Arr_5min = 0; // Initializing Station parameters to 0 since these values will be calculated for station instant_spatial_position
	TotalInputDelays.Perc_Delayed_T = TotalInputDelays.Perc_Delayed_T_3min = TotalInputDelays.Perc_Delayed_T_5min = 0;
	TotalInputDelays.Tot_Consec_Delay = 0;

	// Initializing parameters for Entrance input delays
	EntranceInputDelays.N_Stopped_Trains = numRegions; // Initializing the number of trains to consider: in this case they are all the trains considered in the network
	EntranceInputDelays.Av_Arrival_Delay = EntranceInputDelays.Std_Arrival_Delay = 0;
	EntranceInputDelays.N_Delayed_Arr = EntranceInputDelays.N_Delayed_Arr_3min = EntranceInputDelays.N_Delayed_Arr_5min = 0; // Initializing Station parameters to 0 since these values will be calculated for station instant_spatial_position
	EntranceInputDelays.Perc_Delayed_T = EntranceInputDelays.Perc_Delayed_T_3min = EntranceInputDelays.Perc_Delayed_T_5min = 0;
	EntranceInputDelays.Tot_Consec_Delay = 0;

	// Initializing parameters for Disturbances to dwell times input delays
	DisturbanceInput.N_Stopped_Trains = numRegions; // Initializing the number of trains to consider: in this case they are all the trains considered in the network
	DisturbanceInput.Av_Arrival_Delay = DisturbanceInput.Std_Arrival_Delay = 0;
	DisturbanceInput.N_Delayed_Arr = DisturbanceInput.N_Delayed_Arr_3min = DisturbanceInput.N_Delayed_Arr_5min = 0; // Initializing Station parameters to 0 since these values will be calculated for station instant_spatial_position
	DisturbanceInput.Perc_Delayed_T = DisturbanceInput.Perc_Delayed_T_3min = DisturbanceInput.Perc_Delayed_T_5min = 0;
	DisturbanceInput.Tot_Consec_Delay = 0;

	for (int j = 0; j < numRegions; j++) {													   // looping over the total number of trains
		TrainDelay[j] = regional_train[j].TotalInputDelays;								   // recording the total delay in input: Entrance + Disturbances to dwell times
		TrainEntDelay[j] = regional_train[j].EntranceDelay;								   // recording the delay at the entrance set in input
		Disturb[j] = regional_train[j].TotalInputDelays - regional_train[j].EntranceDelay; // recording the disturbances to dwell times cumulated over all stations

		// Setting the Number of trains that are delayed because they have total input delay positive:Entrance delays+disturbances to dwell times
		if (TrainDelay[j] > 0)
			TotalInputDelays.N_Delayed_Arr++; // increase the number of delayed trains if it is delayed
		if (TrainDelay[j] > 3 * 60 / timestep)
			TotalInputDelays.N_Delayed_Arr_3min++; // increase this number if its delay>3 min
		if (TrainDelay[j] > 5 * 60 / timestep)
			TotalInputDelays.N_Delayed_Arr_5min++; // increase this number if its delay>5 min

		// Setting the Number of trains that are delayed because they have entrance input delay positive
		if (TrainEntDelay[j] > 0)
			EntranceInputDelays.N_Delayed_Arr++; // increase the number of delayed trains if it is delayed
		if (TrainEntDelay[j] > 3 * 60 / timestep)
			EntranceInputDelays.N_Delayed_Arr_3min++; // increase this number if its delay>3 min
		if (TrainEntDelay[j] > 5 * 60 / timestep)
			EntranceInputDelays.N_Delayed_Arr_5min++; // increase this number if its delay>5 min

		// Setting the Number of trains that are delayed because they have disturbances to dwell times positive
		if (Disturb[j] > 0)
			DisturbanceInput.N_Delayed_Arr++; // increase the number of delayed trains if it is delayed
		if (Disturb[j] > 3 * 60 / timestep)
			DisturbanceInput.N_Delayed_Arr_3min++; // increase this number if its delay>3 min
		if (Disturb[j] > 5 * 60 / timestep)
			DisturbanceInput.N_Delayed_Arr_5min++; // increase this number if its delay>5 min
	}

	// Calculate the Total and the Average Input Delays
	for (int r = 0; r < numRegions; r++) {
		TotalInputDelays.Av_Arrival_Delay = TotalInputDelays.Av_Arrival_Delay + TrainDelay[r];			// Calculating the sum of total input delays
		EntranceInputDelays.Av_Arrival_Delay = EntranceInputDelays.Av_Arrival_Delay + TrainEntDelay[r]; // Calculating the sum of entrance input delays
		DisturbanceInput.Av_Arrival_Delay = DisturbanceInput.Av_Arrival_Delay + Disturb[r];				// Calculating the sum of disturbances to dwell times
	}
	TotalInputDelays.totalArrivalDelay = TotalInputDelays.Av_Arrival_Delay;		// Calculating the total input delay
	EntranceInputDelays.totalArrivalDelay = EntranceInputDelays.Av_Arrival_Delay; // Calculating the total entrance delay in input
	DisturbanceInput.totalArrivalDelay = DisturbanceInput.Av_Arrival_Delay;		// Calculating the total entrance delay in input

	TotalInputDelays.Av_Arrival_Delay = TotalInputDelays.Av_Arrival_Delay / TotalInputDelays.N_Delayed_Arr;			 // Calculating the average total delay in input
	EntranceInputDelays.Av_Arrival_Delay = EntranceInputDelays.Av_Arrival_Delay / EntranceInputDelays.N_Delayed_Arr; // Calculating the average entrance delay in input
	DisturbanceInput.Av_Arrival_Delay = DisturbanceInput.Av_Arrival_Delay / DisturbanceInput.N_Delayed_Arr;			 // Calculating the average disturbance in input

	// Calculate the Standard Deviation of Entrance delays and disturbances in input
	for (int r = 0; r < numRegions; r++) {
		if (TrainDelay[r] != 0) { // Std for Total delays in input
			TotalInputDelays.Std_Arrival_Delay = TotalInputDelays.Std_Arrival_Delay + pow((TrainDelay[r] - TotalInputDelays.Av_Arrival_Delay), 2);
		}
		if (TrainEntDelay[r] != 0) { // Std for entrance delays in input
			EntranceInputDelays.Std_Arrival_Delay = EntranceInputDelays.Std_Arrival_Delay + pow((TrainEntDelay[r] - EntranceInputDelays.Av_Arrival_Delay), 2);
		}
		if (Disturb[r] != 0) { // Std for disturbances to dwell time in input
			DisturbanceInput.Std_Arrival_Delay = DisturbanceInput.Std_Arrival_Delay + pow((Disturb[r] - DisturbanceInput.Av_Arrival_Delay), 2);
		}
	}
	TotalInputDelays.Std_Arrival_Delay = sqrt(TotalInputDelays.Std_Arrival_Delay / (TotalInputDelays.N_Delayed_Arr - 1));
	EntranceInputDelays.Std_Arrival_Delay = sqrt(EntranceInputDelays.Std_Arrival_Delay / (EntranceInputDelays.N_Delayed_Arr - 1));
	DisturbanceInput.Std_Arrival_Delay = sqrt(DisturbanceInput.Std_Arrival_Delay / (DisturbanceInput.N_Delayed_Arr - 1));

	// Calculate the Percentages of Delayed trains in input
	for (int r = 0; r < numRegions; r++) {
		// For Total Delay in input
		TotalInputDelays.Perc_Delayed_T = (double)TotalInputDelays.N_Delayed_Arr / TotalInputDelays.N_Stopped_Trains * 100;			  // Percentage of delayed trains
		TotalInputDelays.Perc_Delayed_T_3min = (double)TotalInputDelays.N_Delayed_Arr_3min / TotalInputDelays.N_Stopped_Trains * 100; // Percentage of delayed trains more than 3 min
		TotalInputDelays.Perc_Delayed_T_5min = (double)TotalInputDelays.N_Delayed_Arr_5min / TotalInputDelays.N_Stopped_Trains * 100; // Percentage of delayed trains more than 5 min
																																	  // For Entrance Delay in input
		EntranceInputDelays.Perc_Delayed_T = (double)EntranceInputDelays.N_Delayed_Arr / EntranceInputDelays.N_Stopped_Trains * 100;		   // Percentage of delayed trains
		EntranceInputDelays.Perc_Delayed_T_3min = (double)EntranceInputDelays.N_Delayed_Arr_3min / EntranceInputDelays.N_Stopped_Trains * 100; // Percentage of delayed trains more than 3 min
		EntranceInputDelays.Perc_Delayed_T_5min = (double)EntranceInputDelays.N_Delayed_Arr_5min / EntranceInputDelays.N_Stopped_Trains * 100; // Percentage of delayed trains more than 5 min
																																			   // For Disturbances in input
		DisturbanceInput.Perc_Delayed_T = (double)DisturbanceInput.N_Delayed_Arr / DisturbanceInput.N_Stopped_Trains * 100;			  // Percentage of delayed trains
		DisturbanceInput.Perc_Delayed_T_3min = (double)DisturbanceInput.N_Delayed_Arr_3min / DisturbanceInput.N_Stopped_Trains * 100; // Percentage of delayed trains more than 3 min
		DisturbanceInput.Perc_Delayed_T_5min = (double)DisturbanceInput.N_Delayed_Arr_5min / DisturbanceInput.N_Stopped_Trains * 100; // Percentage of delayed trains more than 5 min
	}

	// Calculate the Max Total, Entrance and Disturbance Delay in input
	for (int r = 0; r < numRegions; r++) {
		if (TrainDelay[r] > TotalInputDelays.Max_TotalDelay)
			TotalInputDelays.Max_TotalDelay = TrainDelay[r]; // Max Total Delay in input
		if (TrainEntDelay[r] > EntranceInputDelays.Max_TotalDelay)
			EntranceInputDelays.Max_TotalDelay = TrainEntDelay[r]; // Max Entrance Delay in input
		if (Disturb[r] > DisturbanceInput.Max_TotalDelay)
			DisturbanceInput.Max_TotalDelay = Disturb[r]; // Max Disturbance in input
	}

}

// Function to calculate the train delay statistics for all the station considered in the network
void calculateDelayStatsForAllStations() {
	for (int s = 0; s < numStations; s++) {
		calculateDelayStatsAtStation(StationArray[s]);
	}
}

// Function to calculate positive and negative train delay statistics for all stations
void calculatePosAndNegDelayStatsForAllStations() {
	for (int s = 0; s < numStations; s++) {
		calculatePosAndNegDelayStatsAtStation(StationArray[s]);
	}
}

// Function to calculate positive an negative delays of trains for all the trains considered in the simulation
void calculatePosAndNegArrivalDelayAllTrains() {
	for (int j = 0; j < numRegions; j++) {
		regional_train[j].Actual_Arrivals();
		regional_train[j].Compute_Pos_And_Neg_Arrival_Delays_At_Stations();
	}
}

// Function to call and launch ROMA for computing a new rescheduling plan
void callRoma(string instancename, double inittime, double PH) {
	// Name of the ROMA Batch file
	string Name_Of_Batch_Plus_Data = Name_Of_Integ_Folder + "batch_ROMA.bat";
	char endOfpred[20]; // This value is the sum of the current time instant t and the PH (e.g. if we are at t=150 and PH=1800 This variable is t+PH=1950) and the corresponding char
						// Convert all parameters in char
	if (inittime < 0)
		snprintf(Init_Time, sizeof(Init_Time), "%d", 0); // with this if and else conditions it is assured that the initial time of ROMA is never lower than 0 (i.e. negative)
	else
		snprintf(Init_Time, sizeof(Init_Time), "%d", (int)inittime);

	char DelayDisp[20]; // variable for storing the value of the Dispatching Delay
	char Resch_Int[20];
	snprintf(Resch_Int, sizeof(Resch_Int), "%d", Resched_Interval);
	snprintf(Pred_Hor, sizeof(Pred_Hor), "%d", (int)PH);
	snprintf(endOfpred, sizeof(endOfpred), "%d", (int)(inittime + PH));
	snprintf(DelayDisp, sizeof(DelayDisp), "%d", DelayDispatcherImpl);
	Name_Of_Batch_Plus_Data = Name_Of_Batch_Plus_Data + " " + InstanceName + " " + Init_Time + " " + Resch_Int + " " + Pred_Hor + " " + endOfpred + " " + DelayDisp;

	// Launch the batch file to activate ROMA and receive a new train order from it
	system((char*)Name_Of_Batch_Plus_Data.c_str());
}

// Function to compute the critical headways for all the trains
void ComputeCriticalHeadwaysForLocationsForAllTrains(Train* T, int numTrains) {
	for (int i = 0; i < numTrains; i++) {
		T[i].DetermineCriticalHWForAllLocations();
	}
}

// Function to compute Energy consumption for all the trains in the network
void ComputeEnergyConsumptionForAllTrains(Train* Trains, int numTrains) {
	for (int i = 0; i < numTrains; i++) {
		Trains[i].TotalEnergyConsumed = 0;
		Trains[i].TotalEnergyConsWithRegBrak = 0;
		Trains[i].TotalEnergySubstationRequest = 0;
		Trains[i].TotalEnergySubstRequestWithRegBrak = 0;
		// Compute the Energy Consumption for all the trains in the network
		Trains[i].TotalEnergyConsumptionWithAndWithoutRegBraking(0.8, 0.7); // we are using as default an efficiency of 0.8 for the substation and 0.7 for regenerative braking (but these values are actually a feature of the substation and the train respectively)
	}
}

// Function to compute the headways of all the trains
void ComputeHwMatrixForAllTrains(Train* T, int numTrains, string MainFolder) {
#pragma omp parallel
	{
#pragma omp for
		for (int i = 0; i < numTrains; i++) {
			T[i].SetLocationNames();
			T[i].ComputeHwMatrix(T, numTrains);
			T[i].PrintHeadwayMatrix(MainFolder);
		}
	}
}

// Function to compute the headways of all the trains with the following according to a train order given
void ComputeHwMatrixForAllTrainsWithGivenOrder(Train* T, int numTrains, string MainFolder, OrderList OrderedTrainArray) {

	// Once the TrainArray has been sort out compute the Headway Matrix
#pragma omp parallel
	{
#pragma omp for
		for (int i = 0; i < numTrains; i++) {
			T[i].SetLocationNames();
			T[i].ComputeHwMatrixForGivenOrder(T, numTrains, OrderedTrainArray);
			T[i].PrintHeadwayMatrix(MainFolder);
		}
	}
}

// Function to compute the HW matrix of all the trains solving the conflicts among all the trains
void ComputeHwMatrixWithGivenOrderSolvingAllConflicts(Train* T, int numTrains, string MainFolder, OrderList OrderedTrainArray) {
	int IndexOfTrains[1000]; // this vector connects the vector of OrderedTrainArray with the Array of Trains, since they are orderedi n a different way
	int N_IndexOfTrains = 0; // The size of the vector
							 // Define the array containing the indices of all the trains
	for (int q = 0; q < OrderedTrainArray.numTeList; q++) {
		for (int i = 0; i < numTrains; i++) {
			if (T[i].trainDescription == OrderedTrainArray.TE[q].trainDescription) {
				IndexOfTrains[q] = i;
				N_IndexOfTrains++; // Increase the number of trains
				break;
			}
		}
	}
	// Create a temporary folder for printing out the shifted train trajectories
	string TrajectorySubFolder;
	TrajectorySubFolder = TrajectorySubFolder + MainFolder + "/" + "TEMP_Shifted_Trajectories";
	_mkdir((char*)TrajectorySubFolder.c_str());

	// Create a temporary folder for printing out the ongoing timetable assignments
	string TTAssignmentSubFolder;
	TTAssignmentSubFolder = TTAssignmentSubFolder + MainFolder + "/OnGoing_TT_Assignment";
	_mkdir((char*)TTAssignmentSubFolder.c_str());

	// Create a file for plotting all the conflicts detected in the Timetable
	ofstream ConflictOutput;
	string ConflOutputName;
	ConflOutputName = ConflOutputName + MainFolder + "/" + "AllTrainConflicts.txt";
	ConflictOutput.open((char*)ConflOutputName.c_str(), ios::app);
	ConflictOutput << "\n\n*****************New Conflict Detection*******************\n";

	// Create a File for plotting the blocking times and timetable points when the train has been assigned to a path in the timetable
	ofstream OnGoingBlockingTimeFile;
	string BlockingTimeFileName;
	BlockingTimeFileName = BlockingTimeFileName + TTAssignmentSubFolder + "/BlockingTimes.txt";
	OnGoingBlockingTimeFile.open((char*)BlockingTimeFileName.c_str(), ios::app);

	// Create a File to plot the timetable points
	ofstream OnGoingTTPoints;
	string TTPointsFileName;
	TTPointsFileName = TTPointsFileName + TTAssignmentSubFolder + "/TimetablePoints.txt";
	OnGoingTTPoints.open((char*)TTPointsFileName.c_str(), ios::app);

	for (int i = 0; i < N_IndexOfTrains; i++) {
		T[i].SetLocationNames();
		int Index = IndexOfTrains[i]; // temporary variable equal to IndexOfTrains[i]

		if (i > 0) { // Compute the headways and the shift only for trains different from the first one
			// Put a dummy overlap to activate the while loop
			T[Index].numOverlaps = 1;
			while (T[Index].numOverlaps > 0) {
				cout << "Resetting Train " << T[Index].trainDescription << "\n";
				T[Index].ResetConflictingTrainsAndHwMatrix();
				cout << "Computing HW for train " << T[Index].trainDescription << "\n";
				// regional_train[Index].SetLocationNames();
				T[Index].DepartureMatrixToSolveConflictsForGivenOrderImproved3(T, numTrains, i, IndexOfTrains); // Compute the MAtrix of the departure times that solve conflicts among all trains

				cout << "Shifting Train " << T[Index].trainDescription << "\n";
				T[Index].ShiftTrain();
				cout << "Detecting Conflicts for Train " << T[Index].trainDescription << "\n";
				T[Index].DetectConflictsWithPreviousDepartingTrains(T, numTrains);
			}
			// regional_train[Index].DepartureMatrixToSolveConflictsForGivenOrder(regional_train, numTrains,i,IndexOfTrains);
			// regional_train[Index].ShiftTrainToCompressTT(regional_train,numTrains,TrajectorySubFolder,MainFolder);    //Shift the train to compress the timetable according the UIC code 406
		} else { // For the first train of the timetable which has i==0 then we need to shift it to 0 and we must simply apply the ShiftTrainCompressTT
			T[Index].ShiftTrainToCompressTT(T, numTrains, TrajectorySubFolder, MainFolder);
		}

		if (T[Index].numOverlaps == 0) {
			T[Index].PrintTrainBlockingTimesForThisTrain(BlockingTimeFileName);
			OnGoingBlockingTimeFile.close();
			// Now must print the Timetable points as well
			T[Index].PrintTimetablePointsForThisTrain(TTPointsFileName);
			OnGoingTTPoints.close();
		}
	}

	// Close the ConflictOutput file
	ConflictOutput.close();
	// Printing train Hw Matrices
	for (int i = 0; i < numTrains; i++) {
		T[i].PrintHeadwayMatrix(MainFolder);
	}
}

// Function to Compute the Energy Consumption for the Timetable
void ComputeTimetableEnergyConsumption(Train* Trains, int numTrains, string OutputFolder) {
	double TotalEnergyConsumed = 0, TotalEnergyConsWithRegBraking = 0, TotalEnergySubstationRequest = 0, TotalEnergySubstRequestWithRegBraking = 0;

	ofstream OutputPerTrain;
	string OutputPerTrainFileName;
	OutputPerTrainFileName = OutputPerTrainFileName + OutputFolder + "/EnergyConsumptionPerTrain.txt";
	OutputPerTrain.open((char*)OutputPerTrainFileName.c_str(), ios::binary);
	OutputPerTrain << "TrainID TotEnergyConsumed[KWh] TotEnergyConsumedWithRegen[KWh] TotEnergyRequestAtSubst[KWh] TotEnergyRequestAtSubstWithRegen[KWh]\n";

	for (int i = 0; i < numTrains; i++) {
		// Printing out the Energy consumed Measure of Performance by Train
		OutputPerTrain << Trains[i].trainDescription << " " << Trains[i].TotalEnergyConsumed * 0.27778 << " " << Trains[i].TotalEnergyConsWithRegBrak * 0.27778 << " " << Trains[i].TotalEnergySubstationRequest * 0.27778 << " " << Trains[i].TotalEnergySubstRequestWithRegBrak * 0.27778 << "\n";

		TotalEnergyConsumed = TotalEnergyConsumed + Trains[i].TotalEnergyConsumed;
		TotalEnergyConsWithRegBraking = TotalEnergyConsWithRegBraking + Trains[i].TotalEnergyConsWithRegBrak;
		TotalEnergySubstationRequest = TotalEnergySubstationRequest + Trains[i].TotalEnergySubstationRequest;
		TotalEnergySubstRequestWithRegBraking = TotalEnergySubstRequestWithRegBraking + Trains[i].TotalEnergySubstRequestWithRegBrak;
	}
	// close the output file
	OutputPerTrain.close();
	// Printing the results aggregated over the entire network
	ofstream Output;
	string OutputFileName;
	OutputFileName = OutputFileName + OutputFolder + "/TotalEnergyConsumption.txt";
	Output.open((char*)OutputFileName.c_str(), ios::binary);

	Output << "TotalEnergyConsumed[KWh] TotalEnergyConsumedWithRegenerativeBraking[KWh] TotalEnergyRequestAtSubst[KWh] TotalEnergyRequestAtSubstWithRegBraking\n";
	Output << TotalEnergyConsumed * 0.27778 << " " << TotalEnergyConsWithRegBraking * 0.27778 << " " << TotalEnergySubstationRequest * 0.27778 << " " << TotalEnergySubstRequestWithRegBraking * 0.27778 << "\n";
	Output.close();
}

// Function to Compute the Arrival and Departure times of at all the timetabling points along their own route
void Compute_TimetablingPoints_For_All_Trains(Train* Trains, int numTrains) {
	for (int i = 0; i < numTrains; i++) {
		Trains[i].ComputeTimetablingPoints();
	}
}

// Function to Detect the implemented Order for all the OL in the network
void Detect_Implemented_Order_For_All_OL() {
	for (int i = 0; i < N_OrderLists; i++) {
		OL[i].Detect_Implemented_Order();
	}
}

// Function to Determine the Max and the min Headway for all locations
void DetermineMaxHW_MinHWForAllLocations(Train* T, int N_Train) {
	int index = 0;
	for (list<Location>::iterator p = AllLocations.begin(); p != AllLocations.end(); p++) {

		cout << "MaxHW & MinHW For Location " << index << "\n";
		index++;
		double MAX_HW = -1;
		string CRITCOUPLE = "None";
		double MIN_HW = 9999999;
		string MINCOUPLE = "None";

		for (int i = 0; i < N_Train; i++) {
			bool LocationFound = false;
			if (T[i].LocationNames.size() > 0) {
				for (list<Location>::iterator h = T[i].LocationNames.begin(); h != T[i].LocationNames.end(); h++) {
					LocationFound = h->areLocationsEqual(*p);
					if (LocationFound == 1) {	 // if the location has been found then take its Max HW value and the critical Couple and break the for loop and pass to another train
						if (h->MaxHW > MAX_HW) { // if the max value is higher then the MAX_HW then this is the MAX_HW
							MAX_HW = h->MaxHW;
							CRITCOUPLE = h->CriticalTrainCouple;
						}
						if (h->MinHW < MIN_HW) {
							MIN_HW = h->MinHW;
							MINCOUPLE = h->MinimumTrainCouple;
						}
						break;
					}
				}
			}
		}
		// Now Set the MaxHW and the critical couple of train for the location, as well as the minimum HW and the Minimum Couple sequence
		p->MaxHW = MAX_HW;
		p->CriticalTrainCouple = CRITCOUPLE;
		p->MinHW = MIN_HW;
		p->MinimumTrainCouple = MINCOUPLE;
	}
}

// Function to draw stochastic Train Delays from a Gaussian Distribution, the St Dev of the delays Perc_Std_Dev must be expressed as a percentage of the scheduled dwell time at stations (e.g. if it is the 20% put Perc_Std_Dev=0.2)

void drawGaussianTrainDwellTimes(double Perc_Std_Dev, int index_scenario) {
	string OutputName;
	char INDEX_SCENARIO[20];
	// Define the name of the scenario to produce
	snprintf(INDEX_SCENARIO, sizeof(INDEX_SCENARIO), "%d", index_scenario);
	OutputName = InputMainFolder + "/TimeTable/Scenarios_DW/";
	OutputName = OutputName + "Scenario_DW_Times_" + INDEX_SCENARIO + ".txt";
	ofstream Output;
	Output.open(OutputName.c_str(), ios::app);
	for (int i = 0; i < numRegions; i++) {
		int N_Dwell_Times = regional_train[i].numStations + 1;
		double* DwellTimes = new double[N_Dwell_Times];
		NumberGenerator GenDelay; // Generator of Stochastic Delays
								  // Drawing the Entrance Delay of the train in the network. The Default is set with a Mean of 1 minute and a StDev of 25 sec
		/*DwellTimes[0]=GenDelay.getGaussianFloat(60,25);*/
		// When the Entrance Delay is loaded from other external files the DwellTimes[0] can be simply imposed to 0
		DwellTimes[0] = 0;
		// Drawing Stochastic delays to the dwell time at the stations
		for (int s = 0; s < regional_train[i].numStations; s++) {
			DwellTimes[s + 1] = GenDelay.getGaussianFloat(regional_train[i].Stations[s].dwellTime, regional_train[i].Stations[s].dwellTime * Perc_Std_Dev);
			if (DwellTimes[s + 1] < regional_train[i].Stations[s].dwellTime)
				DwellTimes[s + 1] = regional_train[i].Stations[s].dwellTime;
			// Additional condition that impose all the departure times from the first station to the scheduled one(this is valid only for the particular application that I am doing but in general it has to be removed from the code)
			if (s == 0)
				DwellTimes[s + 1] = regional_train[i].Stations[s].dwellTime;
		}
		// Printing the stochastic Dwell Times for the ith train
		for (int j = 0; j < N_Dwell_Times; j++) {
			Output << DwellTimes[j] << " ";
		}
		Output << "\n";
		delete[] DwellTimes;
	}
	Output.close();
}

// Function to receive the entrance delays affected by an error distributed according to a Gaussian variable with standard deviation Std_Dev. Index is the Index of the scenario
void Generate_Entrance_Delays_Affected_By_Error(string Folder, double Std_Dev, int index) { // Folder is the path of the Folder where the input files are saved
	double *Matr, *MatrWithError;
	string FileName = "Rollout_";
	char Scen_Index[20];
	snprintf(Scen_Index, sizeof(Scen_Index), "%d", index);
	FileName = Folder + FileName + Scen_Index + ".txt";
	ifstream InputFile;
	InputFile.open((char*)FileName.c_str(), ios::binary);
	if (!InputFile) {
		cout << "Error: Impossible to open file\n";
	}
	int i = 0; // i is the index of the trains
	Matr = new double[numRegions];
	MatrWithError = new double[numRegions]; // Initializing Matr Array
	while (InputFile) {
		InputFile >> Matr[i];
		NumberGenerator Er;
		MatrWithError[i] = Er.getGaussianFloat(Matr[i], Std_Dev * Matr[i]);
		if (MatrWithError[i] < 0)
			MatrWithError[i] = 0; // Do not allow that MatrWithError is less than 0
		i++;					  // increasing the index i
	}
	InputFile.close(); // Closing the InputFile
	ofstream OutputFile;
	string OutputFileName = InputMainFolder;
	OutputFileName = OutputFileName + "/TimeTable/Scenarios_Entr_Delays_With_Error/Rollout_" + Scen_Index + ".txt";
	OutputFile.open((char*)OutputFileName.c_str());
	for (int i = 0; i < numRegions; i++) {
		OutputFile << MatrWithError[i] << "\n";
	}
	OutputFile.close();
}

// Function to Generate the Entrance Delays affected by Error for all the Scenarios
void Generate_Entrance_Delays_With_Error_For_All_Scenarios(string Folder, double Std_Dev, int Number_Of_Scenarios) {
	for (int i = 1; i <= Number_Of_Scenarios; i++) {
		Generate_Entrance_Delays_Affected_By_Error(Folder, Std_Dev, i);
	}
}

// Function to Generate Multiple disturbed scenarios with stochastic Dwell times
void Generate_Multiple_Train_Dwell_Times_From_Gaussian(int Number_of_Scenarios) {
	for (int i = 1; i <= Number_of_Scenarios; i++) {
		drawGaussianTrainDwellTimes(0.8, i);
	}
}

// Function to Implement the ROMA Solution in EGTRAIN
void Implement_ROMA_Solution(string InstanceName, int Instant_Sol_Returned, double PH) {
	char Init_Time[20], Pred_Hor[20], Resch_Int[20];
	snprintf(Init_Time, sizeof(Init_Time), "%d", Instant_Sol_Returned);
	snprintf(Pred_Hor, sizeof(Pred_Hor), "%d", (int)PH);
	snprintf(Resch_Int, sizeof(Resch_Int), "%d", Resched_Interval);
	string NameFolderOutputROMA = Name_Of_Integ_Folder + "Output_ROMA/instance_" + InstanceName + "/" + Resch_Int + "-" + Pred_Hor + "/" + Init_Time + "-" + Pred_Hor; // This is the name of the folder where ROMA saves its outputs
																																									   // Reset all the OL to update
	resetOlToUpdate();
	// Update all OL
	loadAllOrderLists((char*)NameFolderOutputROMA.c_str());
	// Set OL0 as OL1
	Set_OL0_as_OL1();
}

// Fast way of computing free-flow trajectories for HW and capacity computation
void ImprovedTrainSimulationForComputingHW(double v1, double v2, double v3) {
	for (int i = 0; i < numRegions; i++) {
		if (regional_train[i].ID == 1) {
			activateSignallingSystem(); // Activate the signalling system
			for (int t = 0; t < initial_variables.times; t++) {
				regional_train[i].Trajectory_Block_Section_Free_Flow(t, v1, v2, v3);
				regional_train[i].recordEarliestActiveTrajectoryIndex(t);
			}
		} else {
			for (int j = 0; j < numRegions; j++) {
				if ((regional_train[j].type == regional_train[i].type) && (regional_train[j].ID == 1)) { // if the train selected is the first one of that type
					regional_train[i].ReplicateTrainTrajectory(regional_train[j]);						 // Than replicate this train
					break;																				 // after having replicated it break the for loop
				}
			}
		}
	}
}

// Function to Initialize all the Locations in the Network, i.e. the Location list AllLocations
void InitializeAndComputeMaxHwForAllLocations(Train* T, int N_Train, string MainFolder) {
	SetAllLocations(T, N_Train);					 // Set the all the location in the network and their names
	DetermineMaxHW_MinHWForAllLocations(T, N_Train); // Compute their maximum headway
	PrintLocationHeadways(MainFolder);				 // Print out the results in a txt file
}

// Function to Load the Stochastic Dwell Times drawn from the Previous function and assign it to the StopTime Variable of the trains
void Load_And_Set_Stoc_Dwell_Times(char* FileName) {
	ifstream TrainDwellTimes;
	TrainDwellTimes.open(FileName, ios::binary);
	double* Matr;
	if (!TrainDwellTimes) {
		cout << "Error: Impossible to open file\n";
	} else {
		cout << "File Stochastic Train Delays loaded correctly\n";
	}
	int i = 0, j = 0;									// j is the index of the file read and i is the index of the trains
	Matr = new double[regional_train[i].numStations + 1]; // Initializing Matr Array for the first Train regional_train[0]
	while (TrainDwellTimes) {
		TrainDwellTimes >> Matr[j];
		if (j == 0) {
			regional_train[i].departure_time = regional_train[i].departure_time + Matr[j];
		} else {
			regional_train[i].Stations[j - 1].StopTime = Matr[j] / timestep;
			regional_train[i].StationDisturbance[j - 1] = regional_train[i].Stations[j - 1].StopTime - regional_train[i].Stations[j - 1].dwellTime;
		} // Define the new StopTime and calculate the disturbance to dwell time for Station [j-1]
		j++;
		if (j > regional_train[i].numStations) {
			i++;
			j = 0;
			Matr = new double[regional_train[i].numStations + 1];
		} // Reinitializing the dynamic array for the other trains
	}
	cout << "Stochastic Dwell times loaded for " << i - 1 << " trains\n";
	TrainDwellTimes.close(); // Closing Block Section Data File
	delete[] Matr;
}

// Function to Load Departure Delays from a station identified by the name which is given as input
void Load_Departure_Delay_At_Station(char* FileName, string stationName) {
	double* Matr;
	ifstream InputFile;
	InputFile.open(FileName, ios::binary);
	if (!InputFile) {
		cout << "Error: Impossible to open file\n";
	} else {
		cout << "File Train Departure Delays at " << stationName << " loaded correctly\n";
	}
	int i = 0;				  // i is the index of the trains
	Matr = new double[numRegions]; // Initializing Matr Array
	while (InputFile) {
		InputFile >> Matr[i];
		regional_train[i].EntranceDelay = Matr[i];
		for (int j = 0; j < regional_train[i].numStations; j++) {
			if (regional_train[i].Stations[j].stationName == stationName) {
				regional_train[i].ScheduledDepartures[j] = regional_train[i].ScheduledDepartures[j] + regional_train[i].EntranceDelay;
				break;
			}
		}
		i++; // increase index i
	}
	cout << "Departure Delay at Station " << stationName << " loaded for " << i - 1 << " trains\n";
	InputFile.close(); // Closing Block Section Data File
	delete[] Matr;
}

// Load Files of a given disturbed scenario identified by its index and saved in the Folder
void Load_Dwell_Time_Disturbed_Scenario(string Folder, int index) {
	string Name_Of_File = "Scenario_DW_Times_";
	char Scenario_Index[20];
	snprintf(Scenario_Index, sizeof(Scenario_Index), "%d", index);
	Name_Of_File = Folder + Name_Of_File + Scenario_Index + ".txt";
	Load_And_Set_Stoc_Dwell_Times((char*)Name_Of_File.c_str());
}

// Function to Load the Scenario of Entrance delays (from station stationName)identified by its index and saved in Folder
void Load_Entrance_Delay_Disturbed_Scenario(string Folder, string stationName, int index) {
	string Name_Of_File = "Rollout_";
	char Scenario_Index[20];
	snprintf(Scenario_Index, sizeof(Scenario_Index), "%d", index);
	Name_Of_File = Folder + Name_Of_File + Scenario_Index + ".txt";
	Load_Departure_Delay_At_Station((char*)Name_Of_File.c_str(), stationName);
}

void PrintCompressedTrainPathDiagramTrial(Train* S, int N_S, string FolderName) {

	int StartPrintingTime = 999999999; // this is the time the first train departs

	for (int i = 0; i < N_S; i++) {
		if (S[i].departure_time < StartPrintingTime) {
			StartPrintingTime = S[i].departure_time; // finding in this way the minimum train departing time as result from the blocking time compression process
		}
	}

	string FileName;
	string FileSpeedsName;
	FileName = FolderName + "/CompressedTrainPathDiagram.txt";
	FileSpeedsName = FolderName + "/CompressedSpeedsDiagram.txt";
	ofstream FileOutput;
	ofstream FileSpeedsOutput;
	// Opening the file of the speeds
	FileSpeedsOutput.open((char*)FileSpeedsName.c_str(), ios::binary);
	// Opening the file of the train positions
	FileOutput.open((char*)FileName.c_str(), ios::binary);
	// witing on the file of the speeds
	FileSpeedsOutput << "Train/Time ";
	// writing on the file of the train positions
	FileOutput << "Train/Time ";

	for (int t = StartPrintingTime; t <= StartPrintingTime + initial_variables.times; t++) {
		FileOutput << t * timestep << " ";
		// writing on the file of the speeds
		FileSpeedsOutput << t * timestep << " ";
	}
	FileOutput << "\n";
	FileSpeedsOutput << "\n";

	// Determining the actual EndTime of the train run
	for (int i = 0; i < N_S; i++) {
		int ActualEndTime = 0;

		for (int t = 0; t < initial_variables.times; t++) {
			if (S[i].instant_spatial_position[t] == -9999) {
				ActualEndTime = t - 1;
				break;
			}

			if (t > initial_variables.times) {
				ActualEndTime = initial_variables.times;
				break;
			}
		}

		// Printing out the names of trains
		// cout << instant_spatial_position[i].trainDescription << " ";
		FileOutput << S[i].trainDescription << " ";
		// writing on the file of the speeds
		FileSpeedsOutput << S[i].trainDescription << " ";

		int TrainShift = (int)S[i].departure_time - (int)S[i].RunStartTime;
		list<double> TrainSpeeds;
		list<double> TrainPositions;
		for (int l = S[i].RunStartTime; l <= ActualEndTime; l++) {
			double speed = S[i].instant_train_speed[l];
			double position = S[i].instant_spatial_position[l];
			TrainSpeeds.push_back(speed);
			TrainPositions.push_back(position);
			list<double>::iterator h = TrainSpeeds.end();
			h--;
			list<double>::iterator r = TrainPositions.end();
			r--;

			//	cout << *h<< " " << *r << "\n";
		}

		int ShiftedEndTime = ActualEndTime + TrainShift;
		bool StopPrinting = false;
		list<double>::iterator sp = TrainSpeeds.begin();
		list<double>::iterator pos = TrainPositions.begin();
		if (TrainPositions.size() > 0) {
			for (int k = StartPrintingTime; k <= StartPrintingTime + initial_variables.times; k++) {
				if ((k >= S[i].departure_time) && (k <= ShiftedEndTime)) {
					// cout << *pos << " ";
					if (train_route[S[i].indexOfRoute].reversed_direction == 0) {
						FileOutput << *pos << " ";
						// printing speed on the file of the speeds
						FileSpeedsOutput << *sp << " ";
					} else {
						FileOutput << train_route[S[i].indexOfRoute].OriginalRefReversedRoute - *pos << " ";
						// printing speed on the file of the speeds
						FileSpeedsOutput << *sp << " ";
					}
					// advancing iterators on positions and speeds
					pos++;
					sp++;
				} else if (k < S[i].departure_time) {
					// cout << " ";
					if (train_route[S[i].indexOfRoute].reversed_direction == 0) {
						FileOutput << 0 << " ";
						// writing on the file of the speeds
						FileSpeedsOutput << 0 << " ";
					} else {
						FileOutput << train_route[S[i].indexOfRoute].OriginalRefReversedRoute << " ";
						// writing on the file of the speeds
						FileSpeedsOutput << 0 << " ";
					}
				} else if (k > ShiftedEndTime) {
					StopPrinting = true; // break the for loop when the cicle reaches the end of the list
					break;
				}
			}
		}
		// cout << "\n";
		FileOutput << "\n";
		// writing on the file of the speeds
		FileSpeedsOutput << "\n";
	}
	// Close the output file	of positions
	FileOutput.close();
	// Close the output file	of speeds
	FileSpeedsOutput.close();
}

// Function to print out all the Results of a network Location
void PrintLocationHeadways(string MainFolder) {
	string FileName;
	FileName = FileName + MainFolder + "/AllHWForAllLocations.txt";
	ofstream OutputFile;
	OutputFile.open((char*)FileName.c_str(), ios::binary);

	if (AllLocations.size() > 0) {
		OutputFile << "LocationID CriticalHW[s] CriticalTrainCouple MinimumHW[s] MinimumTrainCouple\n";
		for (list<Location>::iterator p = AllLocations.begin(); p != AllLocations.end(); p++) {
			OutputFile << p->Name << " " << p->MaxHW << " " << p->CriticalTrainCouple << " " << p->MinHW << " " << p->MinimumTrainCouple << "\n";
		}
	}
	OutputFile.close(); // Close the file
}

// Function to Print all the trajectories
void PrintTrainPathDiagram(Train* S, int N_S, string FolderName) {
	string FileName;
	FileName = FolderName + "/TrainPathDiagram.txt";
	ofstream FileOutput;
	FileOutput.open((char*)FileName.c_str(), ios::binary);
	FileOutput << "Train/Time ";
	for (int t = 0; t < initial_variables.times; t++) {
		FileOutput << t * timestep << " ";
	}
	FileOutput << "\n";
	for (int i = 0; i < N_S; i++) {
		FileOutput << S[i].trainDescription << " ";
		const auto exportCells = trajectoryExportCells(S[i].instant_spatial_position,
													 S[i].earliestActiveTrajectoryIndex, S[i].End_Time);
		for (int t = 0; t < initial_variables.times; t++) {
			const double position = t < static_cast<int>(exportCells.size()) ? exportCells[t] : -9999;
			if (position == -9999) {
				FileOutput << -9999 << " ";
			} else if (train_route[S[i].indexOfRoute].reversed_direction == 0) {
				FileOutput << position << " ";
			} else {
				FileOutput << train_route[S[i].indexOfRoute].OriginalRefReversedRoute - position << " ";
			}
		}
		FileOutput << "\n";
	}

	FileOutput.close();
}

// Function to Print all the trajectories
void PrintTrainPathDiagramToDebug(Train* S, int N_S, string FolderName) {
	bool NoPrint[300]; // This array has for each train the boolean variable that becomes 1 only if the train has finished its run (i.e. the value -9999 to the instant_spatial_position[i] has been reached)
	for (int k = 0; k < 300; k++) {
		NoPrint[k] = false;
	} // Initializing the NoPrint
	string FileName;
	FileName = FolderName + "/TrainPathDiagram.txt";
	ofstream FileOutput;
	FileOutput.open((char*)FileName.c_str(), ios::binary);
	FileOutput << "Train/Time ";
	for (int t = 0; t < initial_variables.times; t++) {
		FileOutput << t * timestep << " ";
	}
	FileOutput << "\n";
	for (int i = 0; i < N_S; i++) {
		FileOutput << S[i].trainDescription << " ";
		for (int t = 0; t < initial_variables.times; t++) {
			if ((S[i].instant_spatial_position[t] != -9999) && (NoPrint[i] == 0)) {
				if (train_route[S[i].indexOfRoute].reversed_direction == 0) {
					FileOutput << S[i].instant_spatial_position[t] << " ";
				} else {
					FileOutput << train_route[S[i].indexOfRoute].OriginalRefReversedRoute - S[i].instant_spatial_position[t] << " ";
				}
			} else
				break;
		}
		FileOutput << "\n";
	}

	FileOutput.close();
}

// Function to print the files with the computing time of ROMA and EGTRAIN for each combination RI-PH
void Print_Computing_Times(string FolderName) {
	string FileName;
	FileName = FileName + FolderName + "/Computing_Times.txt";
	ofstream Comp_Times;
	Comp_Times.open((char*)FileName.c_str());
	Comp_Times << "TOT_Comp_Time_EGTRAIN[s]" << " " << "TOT_Comp_Time_ROMA" << " " << "TOT_COMP_TIME" << "\n";
	Comp_Times << Comp_Time_EGTRAIN << " " << Comp_Time_ROMA << " " << Comp_Time_EGTRAIN + Comp_Time_ROMA << "\n";
	Comp_Times.close();
}

// Function to Print the Implemented Order of all the OLs in a text file
void Print_Implemented_Order_For_All_OL(string FolderName) {
	for (int i = 0; i < N_OrderLists; i++)
		OL[i].Print_Implemented_Order(FolderName, i);
}

// Function to print the trajectory of the trains in a PNG image by using Powershell that activates a macro in the excel file TrajROMA-EGTRAIN
//  To use This script it is necessary that the ExecutionPolicy of PowerShell is put on RemoteSigned both for the 32 and the x86 (i.e. 64 bit) versions of Powershell
//  if it does not accept the function Set-ExecutionPolicy RemoteSigned, go in regedit-> HKEY LocalMAchine-> Microsoft-> PowerShell->ShellIDs and create a string variable called ExecutionPolicy whose value is RemoteSigned (do the same also for the Wow6432 version of the Powershell)
void Print_Trajectories_As_Image(string InstanceName, char* Resch_Int, char* Pred_Hor) {
	// Printing the trajectories for all trains in the Folder C:/TEMP/
	for (int i = 0; i < numRegions; i++) {
		regional_train[i].PrintTrajectory();
	}

	string FolderOutputEGTRAIN = initial_variables.OutputMainFolder + "\\"; // This is written with the backslash and not with the normal slach because this address will be read by Excel data import that recognizes the backslash
	FolderOutputEGTRAIN = FolderOutputEGTRAIN + "TrainPathDiagram";
	ofstream PathofPrintingFolder; // This file will contain the path in which the image of the trajectory graph must be saved
	PathofPrintingFolder.open(initial_variables.InputMainFolder + "/FolderEGTRAIN.txt");
	PathofPrintingFolder << FolderOutputEGTRAIN; // Writing on the file the folder in which the image will be printed
	PathofPrintingFolder.close();

	// Call the script of powershell to open the Excel file TrajROMA-EGTRAIN, and execute its macro to make the graphs and save it as a PNG image
	// system(initial_variables.InputMainFolder+"/MakeTrajectFigures.ps1");
}

void Print_Trajectories_Of_All_Trains_At_Instant(int t, string FolderName) {
	string SubFolderName = "Trajectories";
	char Instant_t[50];
	snprintf(Instant_t, sizeof(Instant_t), "%d", t);
	SubFolderName = FolderName + SubFolderName + "_" + Instant_t + "/";
	// create subfolder
	_mkdir((char*)SubFolderName.c_str());
	for (int k = 0; k < numRegions; k++) {
		if ((regional_train[k].CanEnter == 1) || (regional_train[k].OutOfSimulation == 1)) {
			if (regional_train[k].instant_spatial_position[t] > 43) // This condition allows to print files only when the train is departed from Utrecht, if you do not want this just eliminate this line
				regional_train[k].PrintTrainTrajectory_At_Instant(t, SubFolderName);
		}
	}
}

// Function to Initialize all the Locations in the infrastructure in order to Identify all the HWs
void SetAllLocations(Train* T, int N_Train) {
	for (int i = 0; i < N_Train; i++) {

		cout << "SetLocations" << i << "\n";
		if (T[i].LocationNames.size() > 0) {
			for (list<Location>::iterator p = T[i].LocationNames.begin(); p != T[i].LocationNames.end(); p++) {
				bool IsLocationThere = false;
				Location TEMPLoc; // Create a temporary Location to add to the AllLocations list
				TEMPLoc.Name = p->Name;

				if (AllLocations.size() == 0) {
					AllLocations.push_back(TEMPLoc);
				} else {
					list<Location>::iterator k = AllLocations.begin();
					while (k != AllLocations.end()) {
						IsLocationThere = p->areLocationsEqual(*k);
						if (IsLocationThere == 1)
							break; // break the while if the location is already in the list

						k++;
					}
					if (IsLocationThere == 0) { // if the location is not there, add it to the AllLocations list
						AllLocations.push_back(TEMPLoc);
					} else { // do nothing
					}
				}
			}
		}
	}
}

void SortOutOrderedTrainArray(Train* T, int numTrains, OrderList& TrainEntranceOrder) {

	list<TrainEvent> OutputLista;
	for (int i = 0; i < numTrains; i++) {
		TrainEvent A;
		A.Time = T[i].departure_time;
		A.trainDescription = T[i].trainDescription;
		OutputLista.push_back(A);
	}

	orderListOfTrainEvents(OutputLista);

	list<TrainEvent>::iterator p = OutputLista.begin();

	while (p != OutputLista.end()) {
		TrainEntranceOrder.TE[TrainEntranceOrder.numTeList].Time = p->Time;
		TrainEntranceOrder.TE[TrainEntranceOrder.numTeList].trainDescription = p->trainDescription;
		TrainEntranceOrder.numTeList++;
		p++;
	}
}

// Function to simulate the trains in free flow to be used for computing the Headways
void TrainSimulationForComputingHW(double v1, double v2, double v3) {
	/*#pragma omp parallel
	{
	#pragma omp for*/
	for (int i = 0; i < numRegions; i++) {
		activateSignallingSystem();
		for (int t = 0; t < initial_variables.times; t++) {
			// if (t==2200)
			// cout<<"Pites";
			regional_train[i].Trajectory_Block_Section_Free_Flow(t, v1, v2, v3);
			regional_train[i].recordEarliestActiveTrajectoryIndex(t);
		}
	}
	//}
}

// Function to Simulate EGTRAIN within a dynamic Interaction with the ROMA tool
void Train_Simulation_Integration_With_ROMA(double v1, double v2, double v3) {
	int Instant_Sol_Returned = 0, Instant_Sol_Implemented = 0;
	for (int t = 0; t <= initial_variables.times; t++) {
		clock_t startEGTRAIN = clock(); // variable that sets the time in which EGTRAIN starts
#pragma omp parallel
		{
#pragma omp for
			// Upload for each simulation step: train characteristics
			for (int j = 0; j < numRegions; j++) {
				regional_train[j].Trajectory_Block_Section(t, v1, v2, v3);
				regional_train[j].recordEarliestActiveTrajectoryIndex(t);
			}
		}

		Occupy_Block_Sections_Of_Route(t); // Fill in the lists Blocks_Occupied and BlocksConnected

		for (const auto& inc : simulationIncidents) {
			if (inc.type == "signal_failure" && t >= inc.startSeconds && t <= inc.endSeconds) {
				for (const auto& secID : inc.resolvedSectionIDs) {
					bool found = false;
					for (const auto& occ : BlocksOccupied) {
						if (occ == secID) { found = true; break; }
					}
					if (!found) {
						BlocksOccupied.push_back(secID);
					}
				}
			}
		}
		releaseBlockConnections();	   // Release Blocks connected with the one really occupied by a train
		activateSignallingSystem();	   // Apply the rules of the signalling system for all the Blocks contained
		BlocksOccupied.clear();			   // Clear the list BlocksOccupied
		BlocksConnected.clear();		   // Clear the list BlocksConnected

		Detect_Implemented_Order_For_All_OL(); // Detect The order Implemented for all the OLs in the network
		clock_t endEGTRAIN = clock();		   // variable that sets the time in which EGTRAIN ends

		Comp_Time_EGTRAIN = Comp_Time_EGTRAIN + double(endEGTRAIN - startEGTRAIN) / CLOCKS_PER_SEC; // computing the cumulated computation time of EGTRAIN

		if (Time_To_Collect_Info == Resched_Interval) { // If the Time instant is equal to the interval to gather the information
			char Resch_Int[20];
			snprintf(Resch_Int, sizeof(Resch_Int), "%d", Resched_Interval);
			string NameOutputFolder;
			NameOutputFolder = NameOutputFolder + Name_Of_Integ_Folder + initial_variables.OutputMainFolder + "/" + "instance_" + InstanceName + "/" + Resch_Int + "-" + Pred_Hor + "/"; // This is the string in which the outputs of EGTRAIN are saved
			Print_Trajectories_Of_All_Trains_At_Instant(t, NameOutputFolder);
			Time_To_Collect_Info = 0;

			// CALL the ROMA tool and compute new rescheduling solution
			clock_t startROMA = clock();
			callRoma(InstanceName, t, PH);
			clock_t endROMA = clock();
			Comp_Time_ROMA = Comp_Time_ROMA + double(endROMA - startROMA) / CLOCKS_PER_SEC; // computing the cumulated computation time of ROMA

			if (DelayDispatcherImpl > Resched_Interval) // If the DelayDispatcherImpl is larger than the reshceduling interval then the instantSol Returned must be the one calculated at the DelayDispatcher/Resched_Interval Rescheduling intervals ago.
				Instant_Sol_Returned = t - (int)(DelayDispatcherImpl / Resched_Interval) * Resched_Interval;
			else
				Instant_Sol_Returned = t;
			Instant_Sol_Implemented = Instant_Sol_Returned + DelayDispatcherImpl;
		}

		// Implement the new rescheduling solution (list OL for each of the Checkpoints) in EGTRAIN
		if (t == Instant_Sol_Implemented) { // If you suppress these two lines you calculate the ROMA Solution but you will not implement it
			Implement_ROMA_Solution(InstanceName, Instant_Sol_Returned, PH);
			// Printing unfeasible orders on a txt file
			char Resch_Int[20];
			snprintf(Resch_Int, sizeof(Resch_Int), "%d", Resched_Interval); // Char variables to take the values of the rescheduling interval and the prediction horizon
			string NameOutputFolder;
			NameOutputFolder = NameOutputFolder + Name_Of_Integ_Folder + initial_variables.OutputMainFolder + "/" + "instance_" + InstanceName + "/" + Resch_Int + "-" + Pred_Hor + "/";
			compareImplementedOrderWithRomaSolutionForAllOl(NameOutputFolder, t);
			/*//Print OL lists on the screen
			cout<<"time"<<t<<"\n";
			for(int i=0;i<N_OrderLists;i++){
			cout<<"OL "<<i<<"\n";
			for (int j=0;j<OL[i].numTeList;j++){
			cout<<OL[i].TE[j].trainDescription<<"\n";
			}
			}
			system("pause");*/
		} // This is the parenthesis that closes the if statement above

		/*
		//Print OL lists on the screen
		cout<<"time"<<t<<"\n";
		for(int i=0;i<N_OrderLists;i++){
		cout<<"OL "<<i<<"\n";
		for (int j=0;j<OL[i].numTeList;j++){
		cout<<OL[i].TE[j].trainDescription<<"\n";
		}
		}
		system("pause");*/

		Time_To_Collect_Info++; // increase Time_To_Collect_Info;
	}
}

// Function to Simulate traffic within the observation period and without interactions wth the traffic management system
void trainSimulation(double v1, double v2, double v3) {
	for (int t = 0; t < initial_variables.times; t++) {
		std::cout << "times = " << t << std::endl;
		clock_t startEGTRAIN = clock(); // variable that sets the time in which EGTRAIN starts
#pragma omp parallel
		{
#pragma omp for
			// Upload for each simulation step: train characteristics
			for (int j = 0; j < numRegions; j++) {
				regional_train[j].Trajectory_Block_Section(t, v1, v2, v3);
				regional_train[j].recordEarliestActiveTrajectoryIndex(t);
			}
		}


		Occupy_Block_Sections_Of_Route(t); // Fill in the lists Blocks_Occupied and BlocksConnected

		for (const auto& inc : simulationIncidents) {
			if (inc.type == "signal_failure" && t >= inc.startSeconds && t <= inc.endSeconds) {
				for (const auto& secID : inc.resolvedSectionIDs) {
					bool found = false;
					for (const auto& occ : BlocksOccupied) {
						if (occ == secID) { found = true; break; }
					}
					if (!found) {
						BlocksOccupied.push_back(secID);
					}
				}
			}
		}
		releaseBlockConnections();	   // Release Blocks connected with the one really occupied by a train
		activateSignallingSystem();	   // Apply the rules of the signalling system for all the Blocks contained
		/*showElement(t,BlocksOccupied);*/
		BlocksOccupied.clear();	 // Clear the list BlocksOccupied
		BlocksConnected.clear(); // Clear the list BlocksConnected
		/*debugFunctionBlockCodes(t,"@2-B2@-1.314000/@3-B0@-1.339000",Route[0]);*/

		Detect_Implemented_Order_For_All_OL();														// Detect The order Implemented for all the OLs in the network
		clock_t endEGTRAIN = clock();																// variable that sets the time in which EGTRAIN ends
		Comp_Time_EGTRAIN = Comp_Time_EGTRAIN + double(endEGTRAIN - startEGTRAIN) / CLOCKS_PER_SEC; // computing the cumulated computation time of EGTRAIN
	}
}

////Function to simulate traffic in networks with a mixed signalling system (e.g. conventional, mixed to ETCS L1, L2 and or L3)
////Function to Simulate traffic within the observation period and without interactions wth the traffic management system
// void Train_Simulation_Mixed_Signalling(double v1, double v2, double v3) {
//	for (int t = 0; t < times; t++) {
//		clock_t startEGTRAIN = clock();    //variable that sets the time in which EGTRAIN starts
// #pragma omp parallel
//		{
// #pragma omp for
//			//Upload for each simulation step: train characteristics
//
//			for (int j = 0; j < numRegions; j++) {
//
//				regional_train[j].trajectoryComputationIncludingMovingBlock(t, v1, v2, v3);  //originally we shall call the function Trajectory_Block_Section_Free_Flow
//			}
//		}
//
//		/**********************************************************************************************
//		*                        Update configuration of the network elements at instant t
//		*
//		************************************************************************************************/
//		ETCS_MA.clear();                        //Clear the list containing all the Movement Authorities given to the trains at the previous instant
//
//		Occupy_Block_Sections_Of_Route(t);     //Fill in the lists Blocks_Occupied and BlocksConnected
//		//ReportAllTrainPositionsToRBC(t, 50);    //Reporting the positions of the trains to the RBC considering a safety Margin of 50 metres
//		//Predict_And_Check_Decoupling_MA_For_All_Train_in_Following_Mode(t);  // Predict and check the Predict_MA_To_DecoupleAt for all trains which are in following mode
//		releaseMixedSignallingSystem();          //Release Blocks connected with the one really occupied by a train
//
//		activateMixedSignallingSystem();          //Apply the rules of the signalling system for all the Blocks contained
//
//		//showElementInEtcsMa(t);   //Printing the MAs
//												  /*showElement(t,BlocksOccupied);*/
//		BlocksOccupied.clear();                 //Clear the list BlocksOccupied
//		BlocksConnected.clear();                //Clear the list BlocksConnected
//
//												/*debugFunctionBlockCodes(t,"@2-B2@-1.314000/@3-B0@-1.339000",Route[0]);*/
//
//		Detect_Implemented_Order_For_All_OL();   //Detect The order Implemented for all the OLs in the network
//		clock_t endEGTRAIN = clock();              //variable that sets the time in which EGTRAIN ends
//		Comp_Time_EGTRAIN = Comp_Time_EGTRAIN + double(endEGTRAIN - startEGTRAIN) / CLOCKS_PER_SEC;     //computing the cumulated computation time of EGTRAIN
//
//	}
// }


// Function to initialise all StationPlatforms for the simulation of passenger flows
// The function takes as input all the Array and number of all block sections, the array and number of all trains, the array of all defined rutes, as well as the length and width of the platforms (to be expressed in meters)
// Length and width of the platforms are assumed to be the same for all platforms in this function. If different dimensions needs to be assigned to each and every platform then the function should be extended with possibility to gather such a varying design from an external data input (manual entry or file)

void Initialise_All_Station_Platforms(list<StationPlatform>& ALLPLATFORMS, int& N_ALLPLATFORMS, Section* Blocks, int N_Blocks, Train* Trains, int numTrains, vector<Route> All_Routes, double platform_length, double platform_width) {
	if (N_Blocks > 0) {
		for (int i = 0; i < N_Blocks; i++) {
			for (int j = 0; j < Blocks[i].total_arcs; j++) {
				if (Blocks[i].arcs_in_signalling_block_section[j].endNode.stationName.empty() != 1) {
					StationPlatform SP;
					SP.ID = Blocks[i].arcs_in_signalling_block_section[j].endNode.stationPlatformId;
					SP.StationID = Blocks[i].arcs_in_signalling_block_section[j].endNode.stationName;
					SP.BlockSectionID = Blocks[i].ID;
					SP.length = platform_length;
					SP.width = platform_width;
					SP.X = Blocks[i].arcs_in_signalling_block_section[j].endNode.X;
					double OccupationAreaOfaPassenger = 3.14159 * pow(0.8, 2); // This formula considers that a person occupies a circular area having a radius of 0.8 meters
					double PlatformArea = SP.length * SP.width;
					double numberofPax = PlatformArea / OccupationAreaOfaPassenger;
					SP.Max_Passenger_Volume = (int)(numberofPax * 0.8); // It is considered that the maximum volume of passengera allowed on a platform is 80% of the floor of the ration between the area of the platform and the average occupation area of a passenger

					for (int h = 0; h < numTrains; h++) {
						if (Trains[h].numStations > 0) {
							for (int k = 0; k < Trains[h].numStations; k++) {
								if (Trains[h].Stations[k].stationName == SP.StationID) {
									bool StationPlatformFound = 0;
									for (int r = 0; r < All_Routes[Trains[h].indexOfRoute].N_Block_Sections; r++) {
										if (All_Routes[Trains[h].indexOfRoute].sequence_of_block_sections[r].ID == SP.BlockSectionID) {

											// Assign information of Stopping platform to the Station Node of the train
											Trains[h].Stations[k].stationPlatformId = SP.ID;

											// Update the list of stopping train at the platform with the traindescription of Trains[h]
											SP.List_Trains_Stopping_At_Platform.push_back(Trains[h].trainDescription);
											StationPlatformFound = 1;
											break;
										}
									}
								}
							}
						}
					}

					ALLPLATFORMS.push_back(SP);
					N_ALLPLATFORMS++;
				}
			}
		}
	}
}

// Function to update the list of trains stopping at platforms after that a new RTTP has been produced ( this might for instance due to train rerouting in stations)
void Update_List_Trains_Stopping_At_Platforms(list<StationPlatform>& ALLPLATFORMS, int& N_ALLPLATFORMS, Train* Trains, int numTrains, vector<Route> All_Routes) {
	// iterating over all the station platforms in the modelled railway network
	for (list<StationPlatform>::iterator s = ALLPLATFORMS.begin(); s != ALLPLATFORMS.end(); s++) {
		// Clear the previous list of trains stopiing at the platform
		s->List_Trains_Stopping_At_Platform.clear();
		// Reset the list of trains stopping at the platform based on the new RTTP which might include train stop skipping and station rerouting
		for (int h = 0; h < numTrains; h++) {
			if (Trains[h].numStations > 0) {
				for (int k = 0; k < Trains[h].numStations; k++) {
					if (Trains[h].Stations[k].stationName == s->StationID) {
						bool StationPlatformFound = 0;
						for (int r = 0; r < All_Routes[Trains[h].indexOfRoute].N_Block_Sections; r++) {
							if (All_Routes[Trains[h].indexOfRoute].sequence_of_block_sections[r].ID == s->BlockSectionID) {

								// Assign information of Stopping platform to the Station Node of the train
								Trains[h].Stations[k].stationPlatformId = s->ID;

								// Update the list of stopping train at the platform with the traindescription of Trains[h]
								s->List_Trains_Stopping_At_Platform.push_back(Trains[h].trainDescription);
								StationPlatformFound = 1;
								break;
							}
						}
					}
				}
			}
		}
	}
}

// Function to check the start of the Journey (hence check their entrance on the network) for all simulated passengers
void checkJourneyStartForAllPassengers(int t, int StartingSimulationTime, list<Passenger>& SIMUL_PAX) {
	if (SIMUL_PAX.empty() != 1) {
		for (list<Passenger>::iterator p = SIMUL_PAX.begin(); p != SIMUL_PAX.end(); p++) {
			p->checkJourneyStart(t + StartingSimulationTime);
		}
	}
}

void Update_List_Passengers_Waiting_At_Platform(StationPlatform& PLAT, list<Passenger> ALL_PAX) {
	// Reset the previous number of people on the platform
	PLAT.Current_N_Passengers = 0;
	PLAT.Current_List_Pax_On_Platform.clear(); // Delete the previous list of passengers on the platform
	for (list<Passenger>::iterator p = ALL_PAX.begin(); p != ALL_PAX.end(); p++) {
		if ((p->IsIntheNetwork == 1) && (p->CurrentStatus == "OnPlatform") && (p->Current_WaitingStationID == PLAT.StationID) && (p->Current_WaitingStationPlatformID == PLAT.ID)) {

			PLAT.Current_N_Passengers++; // Increasing number of passengers on platform

			if (p->Journeys.empty() != 1) {
				for (list<Journey>::iterator j = p->Journeys.begin(); j != p->Journeys.end(); j++) {
					if (j->ID == p->current_JourneyID) {
						for (list<Trip>::iterator activetrip = j->Trips.begin(); activetrip != j->Trips.end(); activetrip++) {
							if (activetrip->TripID == p->current_TripID) {
								PLAT.Current_List_Pax_On_Platform.push_back(make_pair(p->ID, activetrip->Actual_Departure_Time)); // Assigning PAx ID and Actual Departure time in the passenger list of the platform
								break;																							  // break loop over trips
							}
						}
					}
				}
			}
		}
	}
	// Order the list of passengers waiting at the platform in chronological order of their actual departure time
	if (PLAT.Current_List_Pax_On_Platform.empty() != 1) {
		PLAT.Current_List_Pax_On_Platform.sort(orderPassengerListOnPlatform);
	}
}

void Update_List_Passengers_Waiting_At_ALL_Platforms(list<StationPlatform>& ALL_PLAT, list<Passenger> ALL_PAX) {
	if (ALL_PLAT.empty() != 1) {
		for (list<StationPlatform>::iterator plat = ALL_PLAT.begin(); plat != ALL_PLAT.end(); plat++) {
			Update_List_Passengers_Waiting_At_Platform(*plat, ALL_PAX);
		}
	}
}

void Simulate_Train_Passenger_Interactions(int t, int SimulationStartingTime, Train& T, list<Passenger>& ALLPAX, list<StationPlatform> ALLPLATFORMS) {
	// if the train is stopped for a service stop
	if (T.StoppedForServiceStop == 1) {
		int N_AlightPax = 0;
		int N_BoardedPax = 0;					  // These variables count the number of alighted and boarded passengers from / on the train at the current service stop
		double CurrentPlatformOccupationRate = 0; // Occupation rate of the platform where the train is stopping at

		// Iterate across the passenger list to check who needs to alight from and board on the train at the selected station platform
		for (list<Passenger>::iterator p = ALLPAX.begin(); p != ALLPAX.end(); p++) {
			// select all passengers which are onboard of the train which need to get off at the current Station where the train is stopped
			if ((p->CurrentStatus == "OnBoard") && (p->Current_Train_Boarded == T.trainDescription) && (p->Current_Arrival_Station == T.CurrentServiceStop)) {
				T.Current_OnBoard_Passengers--; // reduce the number of onboard passengers by 1 unit as the passenger will alight the train.
				N_AlightPax++;					// accordingly increase the number of alighted passengers at this service stop.

				// Update travel information of the alighted passenger
				if (p->Journeys.empty() != 1) {

					for (list<Journey>::iterator j = p->Journeys.begin(); j != p->Journeys.end(); j++) {
						if ((j->ID == p->current_JourneyID) && (j->IsJourneyCompleted == 0)) { // Select the current journey of the passenger if it is not yet completed

							// Defining iterator of the current, the next trip and the last trips of the active journey

							list<Trip>::iterator currenttrip = j->Trips.begin();
							list<Trip>::iterator nexttrip = j->Trips.begin();
							nexttrip++; // if the journey is composed of one signle trip then next trip will concide with Trips.end() iterator
							list<Trip>::iterator LasttripOfJourney = j->Trips.end();
							LasttripOfJourney--; // Selecting the last trip of the active journey list

							// iterate throught the active trips
							while (currenttrip != j->Trips.end()) {
								// if the active trip is found and has the arrival stop where the train T is stopping at
								if ((currenttrip->TripID == p->current_TripID) && (currenttrip->Arr_Station_ID == T.CurrentServiceStop)) {
									// then set the trip as complete and let the passenger get on the platform
									p->current_location_ID = T.CurrentServiceStop;
									currenttrip->Actual_Arrival_Time = t + SimulationStartingTime;
									currenttrip->totalArrivalDelay = currenttrip->Actual_Arrival_Time - currenttrip->Planned_Arrival_Time;
									currenttrip->IsTripCompleted = true;

									// if the selected trip is the last trip of the active journey then set that journey j will be completed
									if ((currenttrip->TripID == LasttripOfJourney->TripID) && (currenttrip->Arr_Station_ID == j->Arr_Station_ID)) {
										j->Actual_Arrival_Time = currenttrip->Actual_Arrival_Time;
										j->totalJourneyArrivalDelay = currenttrip->totalArrivalDelay; // Could also be computed as Actual_Arrival_Time - Actual_Planned_Arrival_Time
										// however it might be that in the rescheduling process the Planned arrival time of the last trip of the journey is changed based on rerouting or replanning of the train service.

										j->IsJourneyCompleted = true;
										p->current_location_ID = T.CurrentServiceStop;
										p->IsIntheNetwork = false;											  // The passenger is selected and sent out of the simulation as the journey is ended
										p->TimeExitedTheNetwork = currenttrip->Actual_Arrival_Time;			  // Used by the Passenger GUI when the passenger leaves the network
										p->StationExitedTheNetworkID = currenttrip->Arr_Station_ID;			  // Used by the Passenger GUI when the passenger leaves the network
										p->PlatformExitedTheNetworkID = currenttrip->Arr_Station_Platform_ID; // Used by the Passenger GUI when the passenger leaves the network
										p->CurrentStatus = "None";
										p->current_TripID = "None";
										p->current_JourneyID = "None";
										p->Current_Train_Boarded = "None";
										p->Current_Arrival_Station = "None";
										p->Current_Arrival_Platform = "None";

									}

									else { // else if the selected trip is not the last trip of the journey then it means that the passenger will need to transfer and take another trip to complete its journey
										// computing the actual and planned arrival times of the current trip as well as its Total arrival delay
										currenttrip->Actual_Arrival_Time = t + SimulationStartingTime;
										currenttrip->totalArrivalDelay = currenttrip->Actual_Arrival_Time - currenttrip->Planned_Arrival_Time;
										currenttrip->IsTripCompleted = true;

										p->current_TripID = nexttrip->TripID;
										p->CurrentStatus = "OnPlatform";
										p->Current_WaitingStationID = T.CurrentServiceStop;
										p->current_location_ID = T.CurrentServiceStop;
										p->Current_WaitingStationPlatformID = T.CurrentServiceStopPlatform;
										p->Current_Train_Boarded = "None";
										p->Current_Train_To_Wait = nexttrip->TrainServiceDescription;
										p->Current_Arrival_Station = nexttrip->Arr_Station_ID;

										// check that the waiting station of the trip is the same as the one departure station of teh nexttripID
										if (p->Current_WaitingStationID != nexttrip->Dep_Station_ID) {
											cout << "Warning: Passenger " << p->ID << "will depart from a station different than the one planned for active trip " << p->current_TripID << " in journey " << p->current_JourneyID << "\n";
										} else {
											// if the arrival station of teh previous trip is the same as the departure station of the next trip then check whether the passenger is waiting at the scheduled platform
											// Set first of all that the next trip has started
											nexttrip->IsTripStarted = true;
											// Then check the difference in number of platforms between the one where the passenger got off and the one where theu will need to board the train

											int N_Platform_Difference = 0;

											// Code to remove the text "Platform_" from the string containing the Platform Number of the alighting and boarding platforms
											string CurrentPlatformNumber, PlannedDepPlatformNumber, text_to_remove;
											CurrentPlatformNumber = p->Current_WaitingStationPlatformID;
											PlannedDepPlatformNumber = nexttrip->Dep_Station_Platform_ID;
											text_to_remove = "Platform_"; // This removes the "Platform_" from the string of the station platform name
											size_t index1 = CurrentPlatformNumber.find(text_to_remove);
											size_t index2 = PlannedDepPlatformNumber.find(text_to_remove);
											if (index1 != string::npos) {
												CurrentPlatformNumber.erase(index1, text_to_remove.length());
											}
											if (index2 != string::npos) {
												PlannedDepPlatformNumber.erase(index2, text_to_remove.length());
											}

											// Code to conver the obtained platform numbers into integer and compute the difference in number of platforms between the alighting and boarding platforms at the connecting arrival station
											int CurrentPlatform = (int)atof((char*)CurrentPlatformNumber.c_str());
											int PlannedDepPlatform = (int)atof((char*)PlannedDepPlatformNumber.c_str());

											N_Platform_Difference = abs(CurrentPlatform - PlannedDepPlatform);
											double Walkingtime = 0; // this variable is the walking time for the passenger to walk between platforms at a transfer station
											if (N_Platform_Difference <= 1) {
												nexttrip->Actual_Departure_Time = t + SimulationStartingTime; // probably needs also to be added the overall simulation starting time
												j->Walkingtime = j->Walkingtime + 0;						  // No walking time is added is the passenger needs to wait at the same platform they arrived at

											} else {
												// if instead they need to walk to reach the waiting platform then add 30 seconds for each platform difference between the arrival and departing platforms
												Walkingtime = 30 * N_Platform_Difference; // if for instance N_Platform Difference is 5 then the it will be 30 * 5 = 150 seconds meaning 2.5 min and  minutes walking for crossing 5 patforms
												nexttrip->Actual_Departure_Time = t + SimulationStartingTime + Walkingtime;
												j->Walkingtime = j->Walkingtime + Walkingtime;
											}
										}
									}
									break; // break the while loop on trips
								}
								currenttrip++;
								// Advance the iterator nexttrip only if the next current trip is not the last trip of the journey, otherwise an exeption is thrown because of exceeding elements in the trip list
								if (currenttrip != LasttripOfJourney) {
									nexttrip++;
								}
							}
							break; // break the for loop over the journeys once the active one has been identified
						}
					}
				}
			}
		}
		// Specifying Boarding procedure
		// Boarding procedure will follow the order at which passengers arrived at the platform
		for (list<StationPlatform>::iterator Platform = ALLPLATFORMS.begin(); Platform != ALLPLATFORMS.end(); Platform++) {
			if ((Platform->StationID == T.CurrentServiceStop) && (Platform->ID == T.CurrentServiceStopPlatform)) {

				// Update the list of passengers currently waiting at the platform
				// it would be possible to update the list of passenger waiting at the platform only when a train approaches a stop and not at every single time instant
				// NOTE: if you want to show the list of passengers updating on the platform at every time instant then you should comment the line below and uncommnet ,hence use the function "UPdateList of Waiting PAssengers at all platforms" in the "Train_Simulation_Mixed_Signalling_With_Passengersfunction"
				if (!initial_variables.PAX_GUI) {
					Update_List_Passengers_Waiting_At_Platform(*Platform, AllDailyPassengers);
				}

				// The current platform occupation rate is given by the total number of pax waiting at the platform + those just alighted from the train
				CurrentPlatformOccupationRate = (Platform->Current_N_Passengers + N_AlightPax) / Platform->Max_Passenger_Volume;

				if (Platform->Current_List_Pax_On_Platform.empty() != 1) {
					for (list<pair<string, double>>::iterator Pax = Platform->Current_List_Pax_On_Platform.begin(); Pax != Platform->Current_List_Pax_On_Platform.end(); Pax++) {
						for (list<Passenger>::iterator p = ALLPAX.begin(); p != ALLPAX.end(); p++) {
							// iterating through the list of All passengers in the order in which passengers have appeared and arrived at the platform
							// if the ID of the passenger is the same ID of the one in the listo of passengers on the platform and the passenger p needs to board the current train then
							if ((p->ID == Pax->first) && (p->CurrentStatus == "OnPlatform") && (p->Current_Train_To_Wait == T.trainDescription) && (p->Current_WaitingStationID == T.CurrentServiceStop)) {
								// Let the passenger having as train to wait the current train T and Waiting station the currentService Stop board the train
								// passengers can board only if the number of onboard passengers is lower than the max capacity
								if (T.Current_OnBoard_Passengers < T.MAX_OnBoard_Passengers) {
									N_BoardedPax++;
									T.Current_OnBoard_Passengers++;
									p->CurrentStatus = "OnBoard";
									p->Current_Train_Boarded = T.trainDescription;
									p->Current_WaitingStationID = "None";
									p->Current_WaitingStationPlatformID = "None";
									p->Current_Train_To_Wait = "None";

									if (p->Journeys.empty() != 1) {
										for (list<Journey>::iterator j = p->Journeys.begin(); j != p->Journeys.end(); j++) {
											if ((j->ID == p->current_JourneyID) && (j->IsJourneyCompleted == 0)) {
												list<Trip>::iterator currenttrip = j->Trips.begin();
												while (currenttrip != j->Trips.end()) {
													if (currenttrip->TripID == p->current_TripID) {

														int Waiting_Time = t + SimulationStartingTime - currenttrip->Actual_Departure_Time;
														j->Waitingtime = j->Waitingtime + Waiting_Time;
														break; // when the waiting time has been set for the correct journey and trip it is possible to break the while loop over the trips in the active journey
													}
													currenttrip++; // advance the current trip iterator by one.
												}
											}
											break; // break the loop over the journey once the active one has been found
										}
									}

								}

								else { // if the train is full and the passenger cannot board the train then the Route Choice Function shall be called

									cout << "Call RouteChoice model as passenger ID: " << p->ID << " could not board train " << T.trainDescription << " as it was full\n";
									// Call Route Choice Function TO BE ADDED
								}
							}
						}
					}
				}
				break; // breaking the for loop over all platforms when the correct one has been found
			}
		}

		// Changing the dwell time of the train at the station based on the number of alighted and boarded passengers
		for (int s = 0; s < T.numStations; s++) {
			// Change the Stopping Time of the train at the platform as it arrives based on the Gibson dwell time function
			// The stopTime is changed only when the train first approaches the station (i.e. when StepStopped is = 1) and not later than that
			// This means that the stopping time is not changes if an additional passenger arrives last minute as it is assumed that the platform will be less crowded and the passenger can enter with no additional dwell time required with respect to that necessary to alight / board all passengers when the train approached the platform
			if ((T.Stations[s].stationName == T.CurrentServiceStop) && (T.Stations[s].StepStopped == 1)) {
				// Define variable to set passenger dependendent dwell time based on the Gibson dwell time equation
				double PassengerDependetDwellTime = -1;
				PassengerDependetDwellTime = T.computePaxDependentDwellTimeAtStations(N_BoardedPax, N_AlightPax, CurrentPlatformOccupationRate, T.GibsonDwellTimeParameters[0], T.GibsonDwellTimeParameters[1], T.GibsonDwellTimeParameters[2], T.GibsonDwellTimeParameters[3], T.GibsonDwellTimeParameters[4], T.GibsonDwellTimeParameters[5], T.GibsonDwellTimeParameters[6], T.GibsonDwellTimeParameters[7]);
				// Change the Stop Time of the train at the stopping station only if the Passenger dependent dwell time is larger than the pre-set minimum dwell time at that station / stop
				// In case the passenger dependent well time is lower than the pre-set minimum dwell time, then the minimum dwell time is considered.
				if (PassengerDependetDwellTime > T.Stations[s].StopTime) {

					T.Stations[s].StopTime = PassengerDependetDwellTime;
				}
			}
		}
	}
}
