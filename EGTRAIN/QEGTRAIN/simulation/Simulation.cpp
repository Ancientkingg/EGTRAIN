#include "simulation/Simulation.h"
#include "diagrams/RunResults.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <numeric>
#include <vector>

double Comp_Time_EGTRAIN = 0, Comp_Time_ROMA = 0; // variable to measure the computation times of EGTRAIN and ROMA

extern InitialParameters initial_variables;

namespace {
bool energySeriesCovers(const std::vector<double>& values, int first, int last) {
	if (first < 0 || last < first || last >= static_cast<int>(values.size()))
		return false;
	for (int index = first; index <= last; ++index) {
		if (!std::isfinite(values[static_cast<std::size_t>(index)]))
			return false;
	}
	return true;
}

bool canComputeTrainEnergy(Train& train) {
	if (train.earliestActiveTrajectoryIndex < 0 || train.End_Time < train.earliestActiveTrajectoryIndex
		|| train.End_Time >= static_cast<int>(train.instant_spatial_position.size())
		|| !std::isfinite(train.departure_time))
		return false;
	// Completed runs can leave a non-finite terminal power sample; the shared
	// energy calculation treats that boundary sample as zero.
	train.sanitizeTerminalPowerSample();
	const int energyStart = static_cast<int>(train.departure_time);
	return energyStart >= 0 && energyStart <= train.End_Time
		&& energySeriesCovers(train.instant_train_power_consumption, energyStart, train.End_Time)
		&& energySeriesCovers(train.instant_train_energy_consumption, energyStart, train.End_Time)
		&& !validTrajectorySegments(train.instant_spatial_position,
			train.earliestActiveTrajectoryIndex, train.End_Time)
				.empty();
}

void calculateDelayStatistics(Stations& result, const std::vector<double>& delays,
	const std::vector<double>& consecutive, bool includeNonPositive) {
	result.N_Stopped_Trains = static_cast<int>(delays.size());
	result.N_Delayed_Arr = result.N_Delayed_Arr_3min = result.N_Delayed_Arr_5min = 0;
	result.Av_Arrival_Delay = result.Std_Arrival_Delay = 0;
	result.Perc_Delayed_T = result.Perc_Delayed_T_3min = result.Perc_Delayed_T_5min = 0;
	result.totalArrivalDelay = result.Tot_Consec_Delay = 0;
	result.Max_TotalDelay = result.Max_Cons_Delay = -1;
	if (delays.empty()) {
		result.Av_Arrival_Delay = result.Std_Arrival_Delay = result.Tot_Consec_Delay = -1;
		result.Perc_Delayed_T = result.Perc_Delayed_T_3min = result.Perc_Delayed_T_5min = -1;
		return;
	}

	std::vector<double> population;
	population.reserve(delays.size());
	for (double delay : delays) {
		if (delay > 0) {
			++result.N_Delayed_Arr;
			if (delay > 3 * 60 / timestep)
				++result.N_Delayed_Arr_3min;
			if (delay > 5 * 60 / timestep)
				++result.N_Delayed_Arr_5min;
		}
		if (includeNonPositive || delay > 0)
			population.push_back(delay);
	}

	result.totalArrivalDelay = std::accumulate(delays.begin(), delays.end(), 0.0);
	if (!population.empty()) {
		result.Av_Arrival_Delay =
			std::accumulate(population.begin(), population.end(), 0.0) / population.size();
		if (population.size() > 1) {
			double squaredDifference = 0;
			for (double delay : population)
				squaredDifference += std::pow(delay - result.Av_Arrival_Delay, 2);
			result.Std_Arrival_Delay = std::sqrt(squaredDifference / (population.size() - 1));
		}
	}

	result.Perc_Delayed_T = static_cast<double>(result.N_Delayed_Arr) / delays.size() * 100;
	result.Perc_Delayed_T_3min = static_cast<double>(result.N_Delayed_Arr_3min) / delays.size() * 100;
	result.Perc_Delayed_T_5min = static_cast<double>(result.N_Delayed_Arr_5min) / delays.size() * 100;
	result.Max_TotalDelay = *std::max_element(delays.begin(), delays.end());
	if (!consecutive.empty()) {
		result.Tot_Consec_Delay = std::accumulate(consecutive.begin(), consecutive.end(), 0.0);
		result.Max_Cons_Delay = *std::max_element(consecutive.begin(), consecutive.end());
	}
}

void calculateStationDelayStatistics(Stations& station, bool includeNonPositive) {
	std::vector<double> arrivals;
	std::vector<double> consecutive;
	const auto append = [&arrivals, &consecutive, includeNonPositive](const Train& train, int index) {
		const bool recorded = includeNonPositive
			? train.StationArrivals[index] >= 0 && train.ScheduledArrivals[index] >= 0
			: train.StationDelay[index] != -1;
		if (!recorded)
			return;
		arrivals.push_back(train.StationDelay[index]);
		consecutive.push_back(train.StationConsecDelay[index]);
	};

	for (int trainIndex = 0; trainIndex < numRegions; ++trainIndex) {
		const Train& train = regional_train[trainIndex];
		if (station.stationName == "Final_Station") {
			if (train.numStations > 0)
				append(train, train.numStations - 1);
			continue;
		}
		for (int stationIndex = 0; stationIndex < train.numStations; ++stationIndex) {
			if (train.stationNameForArrivalStats(stationIndex) == station.stationName)
				append(train, stationIndex);
		}
	}
	calculateDelayStatistics(station, arrivals, consecutive, includeNonPositive);
}
} // namespace

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
	calculateStationDelayStatistics(S, false);
}

// Function to Calculate the positive and negative delays stats at station instant_spatial_position
void calculatePosAndNegDelayStatsAtStation(Stations& S) {
	calculateStationDelayStatistics(S, true);
}

// Function to Compute the amount of Disturbances set as input: Entrance delays, Cumulative disturbances to dwell times and Total delays (sum of entrance delays and disturbances to dwell times)
void Compute_Input_Delays() {
	std::vector<double> delays(numRegions);
	for (int trainIndex = 0; trainIndex < numRegions; ++trainIndex)
		delays[trainIndex] = regional_train[trainIndex].TotalInputDelays;
	calculateDelayStatistics(TotalInputDelays, delays, {}, false);
	for (int trainIndex = 0; trainIndex < numRegions; ++trainIndex)
		delays[trainIndex] = regional_train[trainIndex].EntranceDelay;
	calculateDelayStatistics(EntranceInputDelays, delays, {}, false);
	for (int trainIndex = 0; trainIndex < numRegions; ++trainIndex)
		delays[trainIndex] = regional_train[trainIndex].TotalInputDelays
			- regional_train[trainIndex].EntranceDelay;
	calculateDelayStatistics(DisturbanceInput, delays, {}, false);
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
		regional_train[j].Compute_Pos_And_Neg_Arrival_Delays_At_Stations();
	}
}

// Function to compute Energy consumption for all the trains in the network
void ComputeEnergyConsumptionForAllTrains(Train* Trains, int numTrains) {
	for (int i = 0; i < numTrains; i++) {
		Trains[i].TotalEnergyConsumed = 0;
		Trains[i].TotalEnergyConsWithRegBrak = 0;
		Trains[i].TotalEnergySubstationRequest = 0;
		Trains[i].TotalEnergySubstRequestWithRegBrak = 0;
		if (!canComputeTrainEnergy(Trains[i]))
			continue;
		// Compute the Energy Consumption for all the trains in the network
		Trains[i].TotalEnergyConsumptionWithAndWithoutRegBraking(0.8, 0.7); // we are using as default an efficiency of 0.8 for the substation and 0.7 for regenerative braking (but these values are actually a feature of the substation and the train respectively)
	}
}

// Function to Compute the Energy Consumption for the Timetable
void ComputeTimetableEnergyConsumption(Regional* Trains, int numTrains, string OutputFolder) {
	double TotalEnergyConsumed = 0, TotalEnergyConsWithRegBraking = 0, TotalEnergySubstationRequest = 0, TotalEnergySubstRequestWithRegBraking = 0;

	ofstream OutputPerTrain;
	string OutputPerTrainFileName;
	OutputPerTrainFileName = OutputPerTrainFileName + OutputFolder + "/EnergyConsumptionPerTrain.txt";
	OutputPerTrain.open((char*)OutputPerTrainFileName.c_str(), ios::binary);
	OutputPerTrain << "TrainID TotEnergyConsumed[KWh] TotEnergyConsumedWithRegen[KWh] TotEnergyRequestAtSubst[KWh] TotEnergyRequestAtSubstWithRegen[KWh]\n";

	for (int i = 0; i < numTrains; i++) {
		// Printing out the Energy consumed Measure of Performance by Train
		OutputPerTrain << Trains[i].trainDescription << " " << energyMJKWh(Trains[i].TotalEnergyConsumed) << " " << energyMJKWh(Trains[i].TotalEnergyConsWithRegBrak) << " " << energyMJKWh(Trains[i].TotalEnergySubstationRequest) << " " << energyMJKWh(Trains[i].TotalEnergySubstRequestWithRegBrak) << "\n";

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
	Output << energyMJKWh(TotalEnergyConsumed) << " " << energyMJKWh(TotalEnergyConsWithRegBraking) << " " << energyMJKWh(TotalEnergySubstationRequest) << " " << energyMJKWh(TotalEnergySubstRequestWithRegBraking) << "\n";
	Output.close();
}

// Function to Compute the Arrival and Departure times of at all the timetabling points along their own route
void Compute_TimetablingPoints_For_All_Trains(Regional* Trains, int numTrains) {
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

// Function to Print all the trajectories
void PrintTrainPathDiagram(Regional* S, int N_S, string FolderName) {
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

void Simulate_Train_Passenger_Interactions(int t, int SimulationStartingTime, Train& T, list<Passenger>& ALLPAX, const list<StationPlatform>& ALLPLATFORMS) {
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
		for (list<StationPlatform>::const_iterator PlatformIt = ALLPLATFORMS.begin(); PlatformIt != ALLPLATFORMS.end(); PlatformIt++) {
			if ((PlatformIt->StationID == T.CurrentServiceStop) && (PlatformIt->ID == T.CurrentServiceStopPlatform)) {
				// Boarding refreshes the waiting list and counts on a local copy; the shared platform list is not modified here
				StationPlatform Platform = *PlatformIt;

				// Update the list of passengers currently waiting at the platform
				// it would be possible to update the list of passenger waiting at the platform only when a train approaches a stop and not at every single time instant
				// NOTE: if you want to show the list of passengers updating on the platform at every time instant then you should comment the line below and uncommnet ,hence use the function "UPdateList of Waiting PAssengers at all platforms" in the "Train_Simulation_Mixed_Signalling_With_Passengersfunction"
				if (!initial_variables.PAX_GUI) {
					Update_List_Passengers_Waiting_At_Platform(Platform, AllDailyPassengers);
				}

				// The current platform occupation rate is given by the total number of pax waiting at the platform + those just alighted from the train
				CurrentPlatformOccupationRate = passengerOccupancyRatio(Platform.Current_N_Passengers + N_AlightPax, Platform.Max_Passenger_Volume);

				if (Platform.Current_List_Pax_On_Platform.empty() != 1) {
					for (list<pair<string, double>>::iterator Pax = Platform.Current_List_Pax_On_Platform.begin(); Pax != Platform.Current_List_Pax_On_Platform.end(); Pax++) {
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
