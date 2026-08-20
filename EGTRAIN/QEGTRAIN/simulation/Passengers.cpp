#include "simulation/Passengers.h"
#include <algorithm>

list<Passenger> AllDailyPassengers; // This list ocntains all the passengers which appear in the network throughout the whole day
int numAllDailyPassengers = 0;		// This the overall number of passengers who will use the rail service across the whole day ( size of list AllDailyPassengers)

nlohmann::json routeChoicePayload(const list<Passenger>& passengers, int timestep) {
	nlohmann::json payload = {{"passengers", nlohmann::json::object()}, {"time", timestep}};
	for (const Passenger& passenger : passengers) {
		if (!passenger.IsIntheNetwork)
			continue;
		const auto journey = std::find_if(passenger.Journeys.begin(), passenger.Journeys.end(),
				[&passenger](const Journey& value) { return value.ID == passenger.current_JourneyID; });
		if (journey == passenger.Journeys.end())
			continue;
		payload["passengers"][passenger.ID + "--1.0"] = {
			{"origin", journey->Dep_Station_ID},
			{"destination", journey->Arr_Station_ID},
			{"departure_time", static_cast<int>(journey->Actual_Planned_Departure_Time)}};
	}
	return payload;
}

void printCurrentPassengerStatus(int t, int StartSimulationTime, list<Passenger> ALLPAX, string MainFolder) {
	string FileName;

	FileName = FileName + MainFolder + "/PassengerStatus.txt";
	ofstream OutputFile;
	OutputFile.open((char*)FileName.c_str(), ios::binary);

	OutputFile << "Time PaxID IsInNetwork JourneyID TripID CurrentStatus CurrentWaitingStationID CurrentWaitingPlatformID CurrentWaitingTrain CurrentBoardedTrain CurrentArrivalStation\n";

	if (ALLPAX.empty() != 1) {
		for (list<Passenger>::iterator p = ALLPAX.begin(); p != ALLPAX.end(); p++) {
			if (p->IsIntheNetwork == 1) {
				OutputFile << (t + StartSimulationTime) << " " << p->ID << " " << p->IsIntheNetwork << " " << p->current_JourneyID << " " << p->current_TripID << " " << p->CurrentStatus << " " << p->Current_WaitingStationID << " " << p->Current_WaitingStationPlatformID << " " << p->Current_Train_To_Wait << " " << p->Current_Train_Boarded << " " << p->Current_Arrival_Station << "\n";
			}
		}
	}
	OutputFile.close();
}

// Function to print the delay of all completed journeys for all passengers.
void printPassengerTotalJourneyDelay(list<Passenger> ALLPAX, string MainFolder) {
	string FileName;

	FileName = FileName + MainFolder + "/JourneyDelays.txt";
	ofstream OutputFile;
	OutputFile.open((char*)FileName.c_str(), ios::binary);

	OutputFile << "PaxID JourneyID TotalArrivalDelay[s]\n";

	if (ALLPAX.empty() != 1) {
		for (list<Passenger>::iterator p = ALLPAX.begin(); p != ALLPAX.end(); p++) {
			for (list<Journey>::iterator j = p->Journeys.begin(); j != p->Journeys.end(); j++) {
				if (j->IsJourneyCompleted == 1) {
					OutputFile << p->ID << " " << j->ID << " " << j->totalJourneyArrivalDelay << "\n";
				} else {

					OutputFile << p->ID << " " << j->ID << " " << "Journey_not_yet_completed" << "\n";
				}
			}
		}
	}
	OutputFile.close();
}

// When using this function it is necessary that the list of passengers is ordered by entrance time on the network, i.e. by the starting time of the currently selected journey

void Passenger::checkJourneyStart(int t) {
	if (this->IsIntheNetwork == 0) { // if the passenger is out of the network check whether the one of its journeys is about to start
		if (Journeys.empty() != 1) {
			for (list<Journey>::iterator j = this->Journeys.begin(); j != Journeys.end(); j++) {
				if (j->IsJourneyCompleted == 0) {
					// Set the condition for the passenger to enter the network to start a journey
					if (t >= j->Actual_Planned_Departure_Time) {

						j->Actual_Departure_Time = t; // Setting the simulated departure time of the passenger for the journey
						IsIntheNetwork = true;		  // let the passenger enter the network to start its journey
						// Assign all the details of the journey to the passenger and assign the passenger to the departure platform of the the first trip
						j->IsJourneyStarted = true; // if the time of the simulation is larger than the actual start time of the journey it is considered started
						this->CurrentStatus = "OnPlatform";
						this->current_location_ID = j->Dep_Station_ID;
						this->Current_WaitingStationID = j->Dep_Station_ID;

						this->current_JourneyID = j->ID;
						if (j->N_Trips > 0) {
							for (list<Trip>::iterator trip = j->Trips.begin(); trip != j->Trips.end(); trip++) {
								// if the trip is not completed and has the same ID of the Journey as well as the same Departure station ID
								if ((trip->IsTripCompleted == 0) && (trip->JourneyID == this->current_JourneyID) && (trip->Dep_Station_ID == this->Current_WaitingStationID)) {
									trip->IsTripStarted = true;
									trip->Actual_Departure_Time = t; // assigning the actual departure time of the trip
									this->Current_WaitingStationPlatformID = trip->Dep_Station_Platform_ID;
									this->current_TripID = trip->TripID;
									// Assigning the attributes of the train to be taken, the station and the platform where the passenger will alight after the train_To_Wait will be arrived
									this->Current_Train_To_Wait = trip->TrainServiceDescription;
									this->Current_Arrival_Station = trip->Arr_Station_ID;
									Current_Arrival_Platform = trip->Arr_Station_Platform_ID;
									break; // break the loop over the trips in the journey once the correct trip ID has been identified
								}
							}
						}
					}
				}
			}
		}
	}
}
