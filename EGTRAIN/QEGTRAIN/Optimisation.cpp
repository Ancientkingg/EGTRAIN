#include "Optimisation.h"

// Defining parameters for defining timetabling: Running time recovery and buffer time
double recoveryTimePercentage = 0;
double bufferTime = 0;

TrainSequenceElement::TrainSequenceElement() {
	TrainType = "None";
	EntryTime = -99999999;
	ServiceOffset = -1;
	TrainIndex = -1;
	Direction = "None";
	numRunsForThisService = 0;
	RunsName = "None";
}

list<TrainSequenceElement> ServiceSequence;

// Function to change the departure time of the trains in order to fit them all in one hour of timetable
void changeTrainDepartureTimesForHourlyTimetabling(Train* Trains, int numTrains) {
	list<TrainEvent> ListTrainsUP, ListTrainsDOWN;
	int N_ListTrainsUP = 0, N_ListTrainsDOWN = 0;
	// Fill in the list of trains departing in the UP and DOWN direction
	for (int i = 0; i < numTrains; i++) {
		TrainEvent TEMPTrain;
		TEMPTrain.Time = Trains[i].departure_time;
		TEMPTrain.trainDescription = Trains[i].trainDescription;
		if (train_route[Trains[i].indexOfRoute].reversed_direction == 0) {
			ListTrainsUP.push_back(TEMPTrain);
			N_ListTrainsUP++; // increase the number of trains in the list UP
		} else {
			ListTrainsDOWN.push_back(TEMPTrain);
			N_ListTrainsDOWN++; // increase the number of trains in the list DOWN
		}
	}
	// Reorder trains in the two lists according to their departure times
	orderListOfTrainEvents(ListTrainsUP);
	orderListOfTrainEvents(ListTrainsDOWN);
	// Redefining the  Departure times based on the number of trains to fit in one hour
	int HeadwayUP = 0, HeadwayDOWN = 0;
	if (N_ListTrainsUP > 0)
		HeadwayUP = (int)(3600 / N_ListTrainsUP);
	if (N_ListTrainsDOWN > 0)
		HeadwayDOWN = (int)(3600 / N_ListTrainsDOWN);

	// Setting the redefined departure times to the elements of the lists
	if (ListTrainsUP.empty() != 1) {
		int traincounter = 0; // this is a counter for the number of trains
		for (list<TrainEvent>::iterator u = ListTrainsUP.begin(); u != ListTrainsUP.end(); u++) {
			if (traincounter == 0) { // if this is the first train of the list
				u->Time = 1;		 // Set that it departs at instant 1
			} else {
				list<TrainEvent>::iterator PreviousTrain = u;
				PreviousTrain--;								  // Iterator pointing at the element before u
				u->Time = (int)(PreviousTrain->Time + HeadwayUP); // The train enters the network after the headwayUP from the PreviousTrain
			}
			// Once we assigned the new departure time then look up for the train and assign it the redefied departure time
			for (int t = 0; t < numTrains; t++) {
				if (Trains[t].trainDescription == u->trainDescription) {
					Trains[t].departure_time = u->Time;
					break; // after having assigned the correct departure time, break the for loop
				}
			}
			traincounter++;
		}
	}

	// Do The same thing for the ListTrainsDOWN
	if (ListTrainsDOWN.empty() != 1) {
		int traincounter = 0; // this is a counter for the number of trains
		for (list<TrainEvent>::iterator u = ListTrainsDOWN.begin(); u != ListTrainsDOWN.end(); u++) {
			if (traincounter == 0) { // if this is the first train of the list
				u->Time = 1;		 // Set that it departs at instant 1
			} else {
				list<TrainEvent>::iterator PreviousTrain = u;
				PreviousTrain--;									// Iterator pointing at the element before u
				u->Time = (int)(PreviousTrain->Time + HeadwayDOWN); // The train enters the network after the headwayUP from the PreviousTrain
			}
			// Once we assigned the new departure time then look up for the train and assign it the redefied departure time
			for (int t = 0; t < numTrains; t++) {
				if (Trains[t].trainDescription == u->trainDescription) {
					Trains[t].departure_time = u->Time;
					break; // after having assigned the correct departure time, break the for loop
				}
			}
			traincounter++;
		}
	}
}

// Compute Network Capacity for a specific Timetable by compressing it with the UIC code 406 method
void ComputeNetworkCapacityForTimetable(OrderList Timetable, Train* Trains, int numTrains, list<NetworkArea>& AllNetworkAreas, string FolderName) {
	// Creating the folder where all the outputs will be saved
	_mkdir((char*)FolderName.c_str());

	// Printing the timetable
	Print_TimeTableOrder(Timetable, FolderName);

	cout << "Resetting Train properties...\n\n";
	// First of All reset trains to the Timetable, their HwMatrix and the list of ConflictingTrains
	ResetTrainsAccordingToTimetable(Timetable, Trains, numTrains);

	cout << "Compressing Timetable According to UIC code 406 approach...\n\n";
	// Once the trains have been reset to the timetable and HwMatrix and Conflicting Trains have been cleared, then compress the timetable
	ComputeHwMatrixWithGivenOrderSolvingAllConflicts(regional_train, numRegions, FolderName, TrainEntranceOrder);

	cout << "Printing compressed blocking Times...\n\n";
	// Printing all the compressed blocking Times and Timetable in one single txt file.
	// PrintTrainPathDiagram(Trains, numTrains, FolderName);
	PrintCompressedTrainPathDiagramTrial(Trains, numTrains, FolderName);
	PrintTrainBlockingTimes(FolderName);
	PrintTimetablePoints(FolderName);

	// Resetting the properties of all network Areas
	if (AllNetworkAreas.empty() != 1) {
		for (list<NetworkArea>::iterator it = AllNetworkAreas.begin(); it != AllNetworkAreas.end(); it++) {
			it->ResetNetworkArea();
		}
		// Remove the EntireNetwork Network Area in case the capacity for "EntireNetwork" has already been computed before
		for (list<NetworkArea>::iterator it = AllNetworkAreas.begin(); it != AllNetworkAreas.end(); it++) {
			if (it->Name == "EntireNetwork") {
				AllNetworkAreas.erase(it);
				break;
			}
		}
	}

	cout << "Computing capacity for all network areas...\n\n";

	// Compute Capacity and Trains Per Hour for all network areas
	computeCapacityAndTphForAllNetworkAreas(AllNetworkAreas, Trains, numTrains, FolderName);
}

// This funtion computes an optimised timetable for a direction that can be "UP", "DOWN" or "BOTH"
void ComputeOptimisedTimetable(string TTDirection, OrderList InitialTimetable, OrderList& OptimisedTimetable, Train* Trains, int numTrains) {
	list<TrainSequenceElement> ServiceSequenceUP;	// this is the Initial list of service sequence in the UP direction
	list<TrainSequenceElement> ServiceSequenceDOWN; // this is the Initial list of service sequence in the DOWN direction

	list<TrainSequenceElement> OptServiceSequenceUP;   // this is the optimised list of service sequence in the UP direction
	list<TrainSequenceElement> OptServiceSequenceDOWN; // this is the optimised list of service sequence in the DOWN direction
	list<TrainEvent> OptServiceSequenceFinal;		   // This is the final optimised list of service sequence in terms of train Events

	// if the Direction is UP or Both compute the Up Direction of the TT
	if ((TTDirection == "UP") || (TTDirection == "BOTH")) {
		// Take all the services from the TrainEntranceOrder list
		SetServiceSequence(ServiceSequenceUP, Trains, numTrains, "UP", InitialTimetable);
		// Reorder the services in order to minimise the average headway between consecutive trains and store the new sequence in the Optimised Service Sequence List
		cout << "Generating Optimised Train Service Sequence for trains in the UP direction...\n\n";
		Generate_Optimised_Train_Service_Sequence(ServiceSequenceUP, OptServiceSequenceUP);
		// Change the EntryTimes of the services in the optimised list according to the ordered departure times of the initial ServiceSequence List
		SetOrderedDepartureTimesToOptimisedServiceList(ServiceSequenceUP, OptServiceSequenceUP);
		// Generate all services in the timetable following the new order
		GenerateAllServiceRunsInOptimisedList(OptServiceSequenceUP);
		// Put the train runs in the new timetable
		if (OptServiceSequenceUP.empty() != 1) { // give the trainDescription and the Entry time of p to the element of the OptimisedTimetable
			for (list<TrainSequenceElement>::iterator p = OptServiceSequenceUP.begin(); p != OptServiceSequenceUP.end(); p++) {
				TrainEvent Element;
				Element.trainDescription = p->RunsName;		// Giving the trainDescription
				Element.Time = p->EntryTime;				// Giving the EntryTime
				OptServiceSequenceFinal.push_back(Element); // pushing back in the list the Element object
			}
		}
	}
	// if the direction is DOWN or BOTH compute the DOWN direction of the TT
	if ((TTDirection == "DOWN") || (TTDirection == "BOTH")) {
		// Take all the services from the TrainEntranceOrder list
		SetServiceSequence(ServiceSequenceDOWN, Trains, numTrains, "DOWN", InitialTimetable);
		// Reorder the services in order to minimise the average headway between consecutive trains and store the new sequence in the Optimised Service Sequence List
		cout << "Generating Optimised Train Service Sequence for trains in the DOWN direction...\n\n";
		Generate_Optimised_Train_Service_Sequence(ServiceSequenceDOWN, OptServiceSequenceDOWN);
		// Change the EntryTimes of the services in the optimised list according to the ordered departure times of the initial ServiceSequence List
		SetOrderedDepartureTimesToOptimisedServiceList(ServiceSequenceDOWN, OptServiceSequenceDOWN);
		// Generate all services in the timetable following the new order
		GenerateAllServiceRunsInOptimisedList(OptServiceSequenceDOWN);
		// Put the train runs in the new timetable
		if (OptServiceSequenceDOWN.empty() != 1) { // give the trainDescription and the Entry time of p to the element of the OptimisedTimetable
			for (list<TrainSequenceElement>::iterator p = OptServiceSequenceDOWN.begin(); p != OptServiceSequenceDOWN.end(); p++) {
				TrainEvent Element;
				Element.trainDescription = p->RunsName;		// Giving the trainDescription
				Element.Time = p->EntryTime;				// Giving the EntryTime
				OptServiceSequenceFinal.push_back(Element); // pushing back in the list the Element object
			}
		}
	}

	// Setting the Optimised timetable specification and order the elements by the EntryTime in the simulation
	if (OptServiceSequenceFinal.empty() != 1) {
		// Order the List by the entry time of train runs
		orderListOfTrainEvents(OptServiceSequenceFinal);
		for (list<TrainEvent>::iterator p = OptServiceSequenceFinal.begin(); p != OptServiceSequenceFinal.end(); p++) {
			OptimisedTimetable.TE[OptimisedTimetable.numTeList].trainDescription = p->trainDescription;
			OptimisedTimetable.TE[OptimisedTimetable.numTeList].Time = p->Time;
			OptimisedTimetable.numTeList++; // Increase the number of elements in the optimised timetable specification
		}
	}
}

// Generate Runs for each service in the OptimisedServiceSequence to pass to the Timetable
void GenerateAllServiceRunsInOptimisedList(list<TrainSequenceElement>& OptimisedServiceSequence) {
	// Create a temporary list that collects all the additional runs for each train service
	list<TrainSequenceElement> AdditionalTrainRuns;

	if (OptimisedServiceSequence.empty() != 1) {
		for (list<TrainSequenceElement>::iterator it = OptimisedServiceSequence.begin(); it != OptimisedServiceSequence.end(); it++) {
			// First of All Attribute a RunName to the first run of the service
			it->RunsName = it->TrainType + "-" + "1";
			if (it->numRunsForThisService >= 2) {					  // if there are at least two runs for this service
				for (int i = 2; i <= it->numRunsForThisService; i++) { // Initialize the runs
					TrainSequenceElement AdditionalRun;
					AdditionalRun.Direction = it->Direction;
					AdditionalRun.TrainType = it->TrainType;
					AdditionalRun.EntryTime = it->EntryTime + (i - 1) * it->ServiceOffset; // Offsetting the entry time of the train run
					char indexoftrain[20];
					snprintf(indexoftrain, sizeof(indexoftrain), "%d", i);
					AdditionalRun.RunsName = AdditionalRun.TrainType + "-" + indexoftrain;
					// Once the additional train run has been created we add it to the list of additional train runs
					AdditionalTrainRuns.push_back(AdditionalRun);
				}
			}
		}

		// Now we can add the elements in the AdditionalTrainRuns to the OptimisedServiceSequenceList
		if (AdditionalTrainRuns.empty() != 1) {
			for (list<TrainSequenceElement>::iterator p = AdditionalTrainRuns.begin(); p != AdditionalTrainRuns.end(); p++) {
				OptimisedServiceSequence.push_back(*p);
			}
		}
	}
}

// Function to Generate the Optimised Train Service Sequence
void Generate_Optimised_Train_Service_Sequence(list<TrainSequenceElement> InputServiceSequence, list<TrainSequenceElement>& OptimisedServiceSequence) {
	if (OptimisedServiceSequence.empty() == 1) {
		TrainSequenceElement CurrentElement, BestBefore, BestAfter;
		list<TrainSequenceElement>::iterator it = InputServiceSequence.begin();
		CurrentElement = *it;
		if (CurrentElement.ConflictingTrains.empty() != 1) {
			// Identifying BestBefore
			for (list<TrainEvent>::iterator p = CurrentElement.ConflictingTrains.begin(); p != CurrentElement.ConflictingTrains.end(); p++) {
				if (p->SuccessorID == "PutBefore") {
					BestBefore.TrainType = p->trainDescription;
					break;
				}
			}

			// Identifying BestAfter
			for (list<TrainEvent>::iterator p = CurrentElement.ConflictingTrains.begin(); p != CurrentElement.ConflictingTrains.end(); p++) {
				if (p->SuccessorID == "PutAfter") {
					BestAfter.TrainType = p->trainDescription;
					break;
				}
			}
		}

		if (BestBefore.TrainType != "None") { // if Best Before has been found then push it into the optimised list

			// And then delete this element from the InputServiceSequence
			if (InputServiceSequence.empty() != 1) {
				for (list<TrainSequenceElement>::iterator q = InputServiceSequence.begin(); q != InputServiceSequence.end(); q++) {
					if (q->TrainType == BestBefore.TrainType) {
						OptimisedServiceSequence.push_back(*q);
						InputServiceSequence.erase(q);
						break;
					}
				}
			}
		}
		// Add the Current Element to the OptimisedList
		OptimisedServiceSequence.push_back(CurrentElement);
		// And delete this element from the InputList
		if (InputServiceSequence.empty() != 1) {
			for (list<TrainSequenceElement>::iterator q = InputServiceSequence.begin(); q != InputServiceSequence.end(); q++) {
				if (q->TrainType == CurrentElement.TrainType) {
					InputServiceSequence.erase(q);
					break;
				}
			}
		}
		// Add the last Element if we find it
		if (BestAfter.TrainType != "None") {
			// Delete this element from the Input List
			if (InputServiceSequence.empty() != 1) {
				for (list<TrainSequenceElement>::iterator q = InputServiceSequence.begin(); q != InputServiceSequence.end(); q++) {
					if (q->TrainType == BestAfter.TrainType) {
						// Add this element to the OptimisedServiceSequence
						OptimisedServiceSequence.push_back(*q);
						InputServiceSequence.erase(q);
						break;
					}
				}
			}
		} else { // if do not find any feasible BestAfter then take the first element from the remaining Elements in InputServiceSequence
			if (InputServiceSequence.empty() != 1) {
				list<TrainSequenceElement>::iterator q = InputServiceSequence.begin();
				OptimisedServiceSequence.push_back(*q);
				// and again remove it from the InputServiceSequence
				InputServiceSequence.erase(q);
			}
		}
	}
	// Once we initialised the first elements in the Optimised then we must proceed with inserting the other elements

	while (InputServiceSequence.size() > 0) { // Do this while loop until the InputServiceSequence list is empty of all of its elements
		if (OptimisedServiceSequence.empty() != 1) {
			// Point at the last Element of the optimised list of service
			list<TrainSequenceElement>::iterator LastElement = OptimisedServiceSequence.end();
			LastElement--;					// now last element points at the last element of OptimisedServiceSequence
			TrainSequenceElement BestAfter; // This is the element to insert after the LastElement in the OptimisedList
			bool IsBestAfterFound = false;	// a bool variable to tell us if a Best After has been found to insert in the OptimisedList
			list<TrainEvent>::iterator p = LastElement->ConflictingTrains.begin();
			while (p != LastElement->ConflictingTrains.end()) { // Until the pointer p reaches the last element in the list or untiil the BestAfter is found do the following loop
				bool IsAlreadyThere = false;
				BestAfter.TrainType = "None";
				if (p->SuccessorID == "PutAfter") {
					BestAfter.TrainType = p->trainDescription;
					if (InputServiceSequence.empty() != 1) {
						for (list<TrainSequenceElement>::iterator k = OptimisedServiceSequence.begin(); k != OptimisedServiceSequence.end(); k++) {
							if (BestAfter.TrainType == k->TrainType) {
								IsAlreadyThere = true; // Put this to true if the element p is already in the Optimised Service List
								break;
							}
						}
					}
				}
				if ((IsAlreadyThere == 0) && (BestAfter.TrainType != "None")) {
					IsBestAfterFound = true; // We have found the BestAfter
					break;					 // we can break the while loop as soon as we find the BestAfter that can be inserted in the list
				}

				p++; // Increase the p iterator
			}

			// Now move the BestAfter element from the InputServiceSequence to the OptimisedServiceSequence if IsBestAfterFound=true
			if (IsBestAfterFound == 1) {
				if (InputServiceSequence.empty() != 1) {
					for (list<TrainSequenceElement>::iterator q = InputServiceSequence.begin(); q != InputServiceSequence.end(); q++) {
						if (q->TrainType == BestAfter.TrainType) {
							// Add this element to the OptimisedServiceSequence
							OptimisedServiceSequence.push_back(*q);
							InputServiceSequence.erase(q);
							break;
						}
					}
				}
			}
			// if instead no valid BestAfter has been found then take the first element in the InputServiceSequence
			else {
				if (InputServiceSequence.empty() != 1) {
					list<TrainSequenceElement>::iterator q = InputServiceSequence.begin();
					OptimisedServiceSequence.push_back(*q);
					// and again remove it from the InputServiceSequence
					InputServiceSequence.erase(q);
				}
			}
		}
	}
}

// Print Timetable Order
void Print_TimeTableOrder(OrderList Timetable, string MainFolder) {
	ofstream OutputFile;
	string OutputFileName;
	OutputFileName = OutputFileName + MainFolder + "/Service_Timetable_Order.txt";
	OutputFile.open((char*)OutputFileName.c_str(), ios::binary);
	if (Timetable.numTeList > 0) {
		for (int i = 0; i < Timetable.numTeList; i++) {
			OutputFile << Timetable.TE[i].trainDescription << "\n";
		}
	}
	OutputFile.close();
}

// Function to reset all train blocking times, departure and exit times according to the timetable. This is achieved by shifting the blocking times, departure and End_Time
void ResetTrainsAccordingToTimetable(OrderList Timetable, Train* Trains, int numTrains) {
	if (Timetable.numTeList > 0) {
		for (int i = 0; i < Timetable.numTeList; i++) {
			for (int j = 0; j < numTrains; j++) {
				if (Trains[j].trainDescription == Timetable.TE[i].trainDescription) {
					// Once the right train has been found then shift its departure, ending and blocking times with the proper function
					double Shift = 0;
					Shift = Timetable.TE[i].Time - Trains[j].departure_time;
					if (Shift != 0) { // Shift train blocking times only if the shift is different from 0
						Trains[j].ShiftBlockingTimes(Shift);
					}
					// In any case also if the train will not be shifted we reset the HwMatrix and the list of Conflicting Trains for the train
					Trains[j].ResetConflictingTrainsAndHwMatrix();
					// at this point we can break the for loop over the trains
					break;
				}
			}
		}
	}
}

// Function to transfer the ordered departure times of the train services in the list InputServiceSequence to the OptimisedServiceSequence
void SetOrderedDepartureTimesToOptimisedServiceList(list<TrainSequenceElement> InputServiceSequence, list<TrainSequenceElement>& OptimisedServiceSequence) {
	if (InputServiceSequence.size() == OptimisedServiceSequence.size()) {
		list<TrainSequenceElement>::iterator it = InputServiceSequence.begin();
		list<TrainSequenceElement>::iterator q = OptimisedServiceSequence.begin();
		while (it != InputServiceSequence.end()) {
			q->EntryTime = it->EntryTime;
			q++;
			it++;
		}
	}
}

// Function to compute the HW matrix of all the trains solving the conflicts among all the trains (The variable TrainDirection is a string that can assume value UP or DOWN to separate the trains going in a direction from those going in the opposite direction)
void SetServiceSequence(list<TrainSequenceElement>& ServiceSequence, Train* T, int numTrains, string TrainDirection, OrderList OrderedTrainArray) {

	// Define the array containing the indices of all the trains
	for (int q = 0; q < OrderedTrainArray.numTeList; q++) {
		bool IsTrainServiceThere = false;
		TrainSequenceElement TrainService;
		if (TrainDirection == "DOWN") {
			for (int i = 0; i < numTrains; i++) {
				if ((T[i].trainDescription == OrderedTrainArray.TE[q].trainDescription) && (train_route[T[i].indexOfRoute].reversed_direction == 1)) {
					TrainService.TrainType = T[i].type;
					TrainService.EntryTime = OrderedTrainArray.TE[q].Time; // give the EntryTime of the train as stated by the Timetable Spec
					TrainService.TrainIndex = i;
					TrainService.Direction = "DOWN";
					TrainService.numRunsForThisService++; // Increase of one run the number of train runs of this service that are present in the Initial Timetable
					break;
				}
			}
		} else if (TrainDirection == "UP") {
			for (int i = 0; i < numTrains; i++) {
				if ((T[i].trainDescription == OrderedTrainArray.TE[q].trainDescription) && (train_route[T[i].indexOfRoute].reversed_direction == 0)) {
					TrainService.TrainType = T[i].type;
					TrainService.EntryTime = OrderedTrainArray.TE[q].Time;
					TrainService.TrainIndex = i;
					TrainService.Direction = "UP";
					TrainService.numRunsForThisService++; // Increase of one run the number of train runs of this service that are present in the Initial Timetable
					break;
				}
			}
		}
		// else the list of Service Sequence will contain trains in both the up and down direction
		else {
			for (int i = 0; i < numTrains; i++) {
				if (T[i].trainDescription == OrderedTrainArray.TE[q].trainDescription) {
					TrainService.TrainType = T[i].type;
					TrainService.EntryTime = OrderedTrainArray.TE[q].Time;
					TrainService.TrainIndex = i;
					TrainService.Direction = "None";
					TrainService.numRunsForThisService++; // Increase of one run the number of train runs of this service that are present in the Initial Timetable
					break;
				}
			}
		}

		if (ServiceSequence.empty() == 1) {
			if (TrainService.TrainType != "None") // Get the train in the list only if the TrainType is different from "None"
				ServiceSequence.push_back(TrainService);
		} else {
			for (list<TrainSequenceElement>::iterator it = ServiceSequence.begin(); it != ServiceSequence.end(); it++) {
				if (it->TrainType == TrainService.TrainType) {
					IsTrainServiceThere = true;
					it->numRunsForThisService++; // Increase of one element the number of runs of this service that are present in the tinitial timetable
												// Put The service Offset if the TrainServiceElement does not have it yet
					if (it->ServiceOffset == -1) {
						it->ServiceOffset = TrainService.EntryTime - it->EntryTime;
					}
					break;
				}
			}

			if (IsTrainServiceThere == 0) {
				if (TrainService.TrainType != "None") // Get the train in the list only if the TrainType is different from "None"
					ServiceSequence.push_back(TrainService);
			}
		}
	}
	// Now setting the list of Conflicting Trains and the respective average HW for each of the elements of the ServiceSequence List
	if (ServiceSequence.empty() != 1) {
		for (list<TrainSequenceElement>::iterator it = ServiceSequence.begin(); it != ServiceSequence.end(); it++) {
			cout << "Computing Critical Headways versus all other services for train " << it->TrainType << "\n\n";
			it->computeCriticalHeadwaysVersusAllOtherServicesImproved(ServiceSequence, T, numTrains);
			// Order the list of conflicting trains identified for each train by the average Hw they have versus this train
			it->orderConflictingTrainsByAverageHw();
		}
	}
}
