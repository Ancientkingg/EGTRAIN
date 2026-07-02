#ifndef Optimisation_hpp
#define Optimisation_hpp

#include "Capacity.h"

// Defining parameters for defining timetabling: Running time recovery and buffer time
extern double recoveryTimePercentage;
extern double bufferTime;

class TrainSequenceElement {
public:
	string TrainType;
	double EntryTime;
	double ServiceOffset;				// This the time headway between two consecutive trains of the same service type
	int TrainIndex;						// This is the index of the train in the regional_train Array of all the trains in the network
	int numRunsForThisService;			// This is the number of train runs we have in the simulation that belong to this service
	string RunsName;					// This is basically the trainDescription of a train run belonging to the service
	string Direction;					// This is the direction of the train that can be either Up or Down, bue is None by Default
	list<TrainEvent> ConflictingTrains; // In this list of Train Events we collect all the conflicting train services putting their Type in the trainDescription and the average HW in the Time.
										// We use then the string SuccessorID to say if the train must be put before or after this train in order to get the computed benefit of HW

	TrainSequenceElement();

	// Operator to make two TrainSequenceElement equal
	TrainSequenceElement operator=(TrainSequenceElement ob2) {
		TrainType = ob2.TrainType;
		EntryTime = ob2.EntryTime;
		ServiceOffset = ob2.ServiceOffset;
		TrainIndex = ob2.TrainIndex;
		RunsName = ob2.RunsName;
		Direction = ob2.Direction;
		numRunsForThisService = ob2.numRunsForThisService;
		if (ob2.ConflictingTrains.empty() != 1) {
			for (list<TrainEvent>::iterator k = ob2.ConflictingTrains.begin(); k != ob2.ConflictingTrains.end(); k++) {
				ConflictingTrains.push_back(*k);
			}
		}
		return *this;
	}

	// Function to compute the Headway of a service versus all the other services in the network
	void computeCriticalHeadwaysVersusAllOtherServices(list<TrainSequenceElement> ServiceList, Train* Trains, int numTrains) {
		if (ServiceList.empty() != 1) {
			for (list<TrainSequenceElement>::iterator it = ServiceList.begin(); it != ServiceList.end(); it++) {
				if (it->TrainType != this->TrainType) {
					int N_ConflictingBlocks = 0;
					bool IsTrainConflicting = false;
					TrainEvent ConflictTrain;
					ConflictTrain.trainDescription = it->TrainType;
					ConflictTrain.Time = -99999999; // The average HW has been put to -99999999
					for (int j = 0; j < Trains[this->TrainIndex].N_BlockTimeComplete; j++) {
						for (int h = 0; h < Trains[it->TrainIndex].N_BlockTimeComplete; h++) {
							bool areBlocksConnected = false;
							areBlocksConnected = Trains[this->TrainIndex].BlockTime[j].AreOnTheSameBlock(Trains[it->TrainIndex].BlockTime[h]);
							if (areBlocksConnected == 1) {
								if (IsTrainConflicting == 0) {
									IsTrainConflicting = true; // Set the IsTrainConflicting to true
									ConflictTrain.Time = 0;	   // Set the Average Headway to 0 to say that must be different from -99999999 if the trains are conflicting
								}
								N_ConflictingBlocks++; // Increase the number of blocks where the two trains conflict
								double Hw = -1;		   // defining a variable that will collect the value of the Headway on the corresponding conflicting block
								Hw = ComputeHwForLocationByShiftingBBelowA(Trains[this->TrainIndex].BlockTime[j], Trains[it->TrainIndex].BlockTime[h]);
								ConflictTrain.Time = ConflictTrain.Time + Hw; // Adding up the headway over all the conflicting block sections
							}
						}
					}
					if (IsTrainConflicting == 1) { // if the train service is conflicting then add it to the list of conflicting service of "this" service
												   // Computing the correct average Headway over the conflicting block sections, by dividing the added up Headway by the number of conflicting block sections
						ConflictTrain.Time = ConflictTrain.Time / N_ConflictingBlocks;
						ConflictTrain.SuccessorID = "PutAfter";
						// Then push back this element in the list of conflicting train services
						this->ConflictingTrains.push_back(ConflictTrain);
					}
				}
			}
		}
	}

	// Function to compute the Headway of a service versus all the other services in the network (It runs in parallel fashion)
	void computeCriticalHeadwaysVersusAllOtherServicesImproved(list<TrainSequenceElement> ServiceList, Train* Trains, int numTrains) {
		if (ServiceList.empty() != 1) {
			for (list<TrainSequenceElement>::iterator it = ServiceList.begin(); it != ServiceList.end(); it++) {
				if (it->TrainType != this->TrainType) {
					int N_ConflictingBlocks = 0;
					bool IsTrainConflicting = false;
					TrainEvent ConflictTrain;
					ConflictTrain.trainDescription = it->TrainType;
					ConflictTrain.Time = -99999999; // The average HW has been put to -99999999

					bool areBlocksConnected = false;
#pragma omp parallel private(areBlocksConnected)
					{
						for (int j = 0; j < Trains[this->TrainIndex].N_BlockTimeComplete; j++) {
							for (int h = 0; h < Trains[it->TrainIndex].N_BlockTimeComplete; h++) {

								areBlocksConnected = Trains[this->TrainIndex].BlockTime[j].AreOnTheSameBlock(Trains[it->TrainIndex].BlockTime[h]);
								if (areBlocksConnected == 1) {
#pragma omp critical
									{
										if (IsTrainConflicting == 0) {
											IsTrainConflicting = true; // Set the IsTrainConflicting to true
											ConflictTrain.Time = 0;	   // Set the Average Headway to 0 to say that must be different from -99999999 if the trains are conflicting
										}
										N_ConflictingBlocks++; // Increase the number of blocks where the two trains conflict
										double Hw = -1;		   // defining a variable that will collect the value of the Headway on the corresponding conflicting block
										Hw = ComputeHwForLocationByShiftingBBelowA(Trains[this->TrainIndex].BlockTime[j], Trains[it->TrainIndex].BlockTime[h]);
										ConflictTrain.Time = ConflictTrain.Time + Hw; // Adding up the headway over all the conflicting block sections
									}
								}
							}
						}
					}
					if (IsTrainConflicting == 1) { // if the train service is conflicting then add it to the list of conflicting service of "this" service
												   // Computing the correct average Headway over the conflicting block sections, by dividing the added up Headway by the number of conflicting block sections
						ConflictTrain.Time = ConflictTrain.Time / N_ConflictingBlocks;
						ConflictTrain.SuccessorID = "PutAfter";
						// Then push back this element in the list of conflicting train services
						this->ConflictingTrains.push_back(ConflictTrain);
					}
				}
			}
		}
	}

	// Function to Order Conflicting Trains by their average Headway with this train, in this way the first of the list will be the one that is most convenient to take as a successor
	void orderConflictingTrainsByAverageHw() {
		list<TrainEvent> TEMPList; // Creating a temporary list that collects all the trains

		// transfer all the elements in the List to TEMPList
		TEMPList = this->ConflictingTrains;
		// Empty List of all its Elements
		ConflictingTrains.clear();
		// Now order all the elements in the TEMPList according to the entrance time of trains in the area
		while (TEMPList.size() > 0) {
			double Min = 999999999;
			for (list<TrainEvent>::iterator it = TEMPList.begin(); it != TEMPList.end(); it++) {
				if (it->Time < Min) {
					Min = it->Time;
				}
			}
			// Set the object TrainEvent having the minimum time
			for (list<TrainEvent>::iterator it = TEMPList.begin(); it != TEMPList.end(); it++) {
				if (it->Time == Min) {
					this->ConflictingTrains.push_back(*it);
					TEMPList.erase(it); // remove the minimum from the TEMPlist and put it in the  original List
					break;
				}
			}
		}
	}
};

extern list<TrainSequenceElement> ServiceSequence;

// Function to compute the HW matrix of all the trains solving the conflicts among all the trains (The variable TrainDirection is a string that can assume value UP or DOWN to separate the trains going in a direction from those going in the opposite direction)
void SetServiceSequence(list<TrainSequenceElement>& ServiceSequence, Train* T, int numTrains, string TrainDirection, OrderList OrderedTrainArray);

// Function to Generate the Optimised Train Service Sequence
void Generate_Optimised_Train_Service_Sequence(list<TrainSequenceElement> InputServiceSequence, list<TrainSequenceElement>& OptimisedServiceSequence);

// Function to transfer the ordered departure times of the train services in the list InputServiceSequence to the OptimisedServiceSequence
void SetOrderedDepartureTimesToOptimisedServiceList(list<TrainSequenceElement> InputServiceSequence, list<TrainSequenceElement>& OptimisedServiceSequence);

// Generate Runs for each service in the OptimisedServiceSequence to pass to the Timetable
void GenerateAllServiceRunsInOptimisedList(list<TrainSequenceElement>& OptimisedServiceSequence);

// This funtion computes an optimised timetable for a direction that can be "UP", "DOWN" or "BOTH"
void ComputeOptimisedTimetable(string TTDirection, OrderList InitialTimetable, OrderList& OptimisedTimetable, Train* Trains, int numTrains);

// Function to reset all train blocking times, departure and exit times according to the timetable. This is achieved by shifting the blocking times, departure and End_Time
void ResetTrainsAccordingToTimetable(OrderList Timetable, Train* Trains, int numTrains);

// Print Timetable Order
void Print_TimeTableOrder(OrderList Timetable, string MainFolder);

// Compute Network Capacity for a specific Timetable by compressing it with the UIC code 406 method
void ComputeNetworkCapacityForTimetable(OrderList Timetable, Train* Trains, int numTrains, list<NetworkArea>& AllNetworkAreas, string FolderName);

// Function to change the departure time of the trains in order to fit them all in one hour of timetable
void changeTrainDepartureTimesForHourlyTimetabling(Train* Trains, int numTrains);

#endif