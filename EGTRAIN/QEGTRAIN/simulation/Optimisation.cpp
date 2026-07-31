#include "simulation/Optimisation.h"

// Defining parameters for defining timetabling: Running time recovery and buffer time
double recoveryTimePercentage = 0;
double bufferTime = 0;

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
