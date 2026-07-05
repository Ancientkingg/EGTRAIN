
#include "simulation/Passengers.h"


list<Passenger> AllDailyPassengers; // This list ocntains all the passengers which appear in the network throughout the whole day
int numAllDailyPassengers = 0;		// This the overall number of passengers who will use the rail service across the whole day ( size of list AllDailyPassengers)


void initialiseAllDailyPassengersFromDailyActivitySchedule(string DASFileName, list<Passenger>& All_Pax, int& N_All_Pax) {
	ifstream InputFile;

	InputFile.open((char*)DASFileName.c_str(), ios::binary);

	if (!InputFile) {
		cout << "\n\nERROR:Cannot Read file with Passenger Daily Activity Schedule\n\n";
	}

	else {
		cout << "\nLoading Passenger's Daily Activity Schedule\n\n";
		string FileLine;
		int linecounter = 0;

		// Person ID of the previous and the current lines of the file
		string personID_currentline, person_ID_previousline;

		// Iterating through the lines of the file
		while (getline(InputFile, FileLine)) {

			if (linecounter > 0) {

				// Streaming file line into string LineStream
				istringstream LineStream(FileLine);

				// Tokenize the LineStream to initialise the strings and attributes of the journey and the passenger
				list<string> TokenA;
				string tok;
				// Splitting LineStream in tokens separated by the charachter ","

				while (getline(LineStream, tok, ',')) {
					TokenA.push_back(tok);
				}

				if (linecounter == 1) { // if we are at the very first line of the DAS file after the column title line
					// Create a passenger object as well as Journey object for the passenger
					list<string>::iterator it = TokenA.begin();
					it++;						// Shifting the iterator to the second element of the Token which is the person ID in the DAS
					personID_currentline = *it; // Assigning the current_PersonID

					Passenger PAX;
					Journey J;
					PAX.ID = personID_currentline;
					advance(it, 3); // advancing iterator to stop_no which is the ID of the journey
					J.ID = *it;
					it++; // advance iterator to stop_type which is the type of activity of the journey
					J.Journey_Activity_Type = *it;
					it++; // Advancing to final journey destination
					J.Arr_Station_ID = *it;
					advance(it, 4); // Advance it up to arrival time at final destination
					string PlannedArrivalTime = *it;
					J.Planned_Arrival_Time = atof((char*)PlannedArrivalTime.c_str());
					advance(it, 2); // Point to planned departure station
					// Randomly generate Planned arrival time in the 30 minutes time window indicated by the DAS
					// Identify time window in the hour
					double TimeWindowArrival = J.Planned_Arrival_Time - (int)J.Planned_Arrival_Time;
					int ActualArrivalTimeSecondsinWindow = 0;
					if (TimeWindowArrival == 0.25) {
						NumberGenerator N;
						ActualArrivalTimeSecondsinWindow = (int)N.generateRandomNumberInRange(0, 1799);
					} else if (TimeWindowArrival == 0.75) {
						NumberGenerator N;
						ActualArrivalTimeSecondsinWindow = (int)N.generateRandomNumberInRange(1800, 3599);
					}
					J.Actual_Planned_Arrival_Time = (int)J.Planned_Arrival_Time * 3600 + ActualArrivalTimeSecondsinWindow;

					J.Dep_Station_ID = *it;
					advance(it, 2); // Pointing to planned departure time

					string PlannedDepartureTime = *it;
					J.Planned_Departure_Time = atof((char*)PlannedDepartureTime.c_str());

					// Randomly generate Planned departure time in the 30 minutes time window indicated by the DAS
					// Identify time window in the hour
					double TimeWindowDeparture = J.Planned_Departure_Time - (int)J.Planned_Departure_Time;
					int ActualDepartureTimeSecondsinWindow = 0;
					if (TimeWindowDeparture == 0.25) {
						NumberGenerator N;
						ActualDepartureTimeSecondsinWindow = (int)N.generateRandomNumberInRange(0, 1799);
					} else if (TimeWindowDeparture == 0.75) {
						NumberGenerator N;
						ActualDepartureTimeSecondsinWindow = (int)N.generateRandomNumberInRange(1800, 3599);
					}
					J.Actual_Planned_Departure_Time = (int)J.Planned_Departure_Time * 3600 + ActualDepartureTimeSecondsinWindow;

					// Adding the Journey to the list of PAX journeys
					PAX.Journeys.push_back(J);
					All_Pax.push_back(PAX); // Pushing the just created passenger in the list of all passengers
					N_All_Pax++;			// increase number of passengers in the ALL_PAX list

					// Make the current person ID as the ID of the previous line
					person_ID_previousline = personID_currentline;
				} else { // for lines after the first line of the file
					// Get the PAssenger ID of the current line in the read file
					list<string>::iterator it = TokenA.begin();
					it++;						// Shifting the iterator to the second element of the Token which is the person ID in the DAS
					personID_currentline = *it; // Assigning the current_PersonID

					// Compare the current and previou ID of passenger
					if (personID_currentline == person_ID_previousline) { // if the line reports a journey for the same person as before

						list<Passenger>::iterator Previous_PAX = All_Pax.end();
						Previous_PAX--; // Pointing at the last passenger in the list of All_Passengers
						// Then only create a new journey
						Journey J;

						advance(it, 3); // advancing iterator to stop_no which is the ID of the journey
						J.ID = *it;
						it++; // advance iterator to stop_type which is the type of activity of the journey
						J.Journey_Activity_Type = *it;
						it++; // Advancing to final journey destination
						J.Arr_Station_ID = *it;
						advance(it, 4); // Advance it up to arrival time at final destination
						string PlannedArrivalTime = *it;
						J.Planned_Arrival_Time = atof((char*)PlannedArrivalTime.c_str());
						advance(it, 2); // Point to planned departure station

						// Randomly generate Planned arrival time in the 30 minutes time window indicated by the DAS
						// Identify time window in the hour
						double TimeWindowArrival = J.Planned_Arrival_Time - (int)J.Planned_Arrival_Time;
						int ActualArrivalTimeSecondsinWindow = 0;
						if (TimeWindowArrival == 0.25) {
							NumberGenerator N;
							ActualArrivalTimeSecondsinWindow = (int)N.generateRandomNumberInRange(0, 1799);
						} else if (TimeWindowArrival == 0.75) {
							NumberGenerator N;
							ActualArrivalTimeSecondsinWindow = (int)N.generateRandomNumberInRange(1800, 3599);
						}
						J.Actual_Planned_Arrival_Time = (int)J.Planned_Arrival_Time * 3600 + ActualArrivalTimeSecondsinWindow;

						J.Dep_Station_ID = *it;
						advance(it, 2); // Pointing to planned departure time

						string PlannedDepartureTime = *it;
						J.Planned_Departure_Time = atof((char*)PlannedDepartureTime.c_str());
						// Randomly generate Planned departure time in the 30 minutes time window indicated by the DAS
						// Identify time window in the hour
						double TimeWindowDeparture = J.Planned_Departure_Time - (int)J.Planned_Departure_Time;
						int ActualDepartureTimeSecondsinWindow = 0;
						if (TimeWindowDeparture == 0.25) {
							NumberGenerator N;
							ActualDepartureTimeSecondsinWindow = (int)N.generateRandomNumberInRange(0, 1799);
						} else if (TimeWindowDeparture == 0.75) {
							NumberGenerator N;
							ActualDepartureTimeSecondsinWindow = (int)N.generateRandomNumberInRange(1800, 3599);
						}
						J.Actual_Planned_Departure_Time = (int)J.Planned_Departure_Time * 3600 + ActualDepartureTimeSecondsinWindow;

						// Adding the Journey J to the list of Journeys of the previous passenger
						Previous_PAX->Journeys.push_back(J);

						// Updating the person ID of the previous line
						person_ID_previousline = personID_currentline;
					}

					else { // if instead the ID of the passenger at the previous and current lines are different then a new passenger and a new journey need to be added
						// Create a passenger object as well as Journey object for the passenger
						list<string>::iterator it = TokenA.begin();
						it++;						// Shifting the iterator to the second element of the Token which is the person ID in the DAS
						personID_currentline = *it; // Assigning the current_PersonID

						Passenger PAX;
						Journey J;
						PAX.ID = personID_currentline;
						advance(it, 3); // advancing iterator to stop_no which is the ID of the journey
						J.ID = *it;
						it++; // advance iterator to stop_type which is the type of activity of the journey
						J.Journey_Activity_Type = *it;
						it++; // Advancing to final journey destination
						J.Arr_Station_ID = *it;
						advance(it, 4); // Advance it up to arrival time at final destination
						string PlannedArrivalTime = *it;
						J.Planned_Arrival_Time = atof((char*)PlannedArrivalTime.c_str());
						advance(it, 2); // Point to planned departure station
						// Randomly generate Planned arrival time in the 30 minutes time window indicated by the DAS
						// Identify time window in the hour
						double TimeWindowArrival = J.Planned_Arrival_Time - (int)J.Planned_Arrival_Time;
						int ActualArrivalTimeSecondsinWindow = 0;
						if (TimeWindowArrival == 0.25) {
							NumberGenerator N;
							ActualArrivalTimeSecondsinWindow = (int)N.generateRandomNumberInRange(0, 1799);
						} else if (TimeWindowArrival == 0.75) {
							NumberGenerator N;
							ActualArrivalTimeSecondsinWindow = (int)N.generateRandomNumberInRange(1800, 3599);
						}
						J.Actual_Planned_Arrival_Time = (int)J.Planned_Arrival_Time * 3600 + ActualArrivalTimeSecondsinWindow;

						J.Dep_Station_ID = *it;
						advance(it, 2); // Pointing to planned departure time

						string PlannedDepartureTime = *it;
						J.Planned_Departure_Time = atof((char*)PlannedDepartureTime.c_str());

						// Randomly generate Planned departure time in the 30 minutes time window indicated by the DAS
						// Identify time window in the hour
						double TimeWindowDeparture = J.Planned_Departure_Time - (int)J.Planned_Departure_Time;
						int ActualDepartureTimeSecondsinWindow = 0;
						if (TimeWindowDeparture == 0.25) {
							NumberGenerator N;
							ActualDepartureTimeSecondsinWindow = (int)N.generateRandomNumberInRange(0, 1799);
						} else if (TimeWindowDeparture == 0.75) {
							NumberGenerator N;
							ActualDepartureTimeSecondsinWindow = (int)N.generateRandomNumberInRange(1800, 3599);
						}
						J.Actual_Planned_Departure_Time = (int)J.Planned_Departure_Time * 3600 + ActualDepartureTimeSecondsinWindow;

						// Adding the Jounrey to the list of PAX journeys
						PAX.Journeys.push_back(J);
						All_Pax.push_back(PAX); // Pushing the just created passenger in the list of all passengers
						N_All_Pax++;			// increase number of passengers in the ALL_PAX list

						// Make the current person ID as the ID of the previous line
						person_ID_previousline = personID_currentline;
					}
				}
			}
			linecounter++;
		}
	}
}

// Function to Load the output of the Route Choice model and update travel choices and trips of passengers

void updatePassengerRouteChoice(string RCFileName, list<Passenger>& All_Pax, int& N_All_Pax) {
	ifstream InputFile;

	InputFile.open((char*)RCFileName.c_str(), ios::binary);

	if (!InputFile) {
		cout << "\n\nERROR:Cannot Read file with Updated Passenger Route Choices\n\n";
	}

	else {
		cout << "\nLoading Updated Passenger's Route Choices\n\n";
		string FileLine;
		int linecounter = 0;

		int CounterColumnsTransferStations = 0;									 // This is a counter of the number of columns in the Route Choice file denoting transfer stations
		int CounterColumnsTrainServiceLegs = CounterColumnsTransferStations + 1; // This is the number of columns in the Route Choice File indicating the train services taken by the passengers to move from origin to destination including transfers
		// Iterating through the lines of the file
		while (getline(InputFile, FileLine)) {

			if (linecounter == 0) {

				// Streaming file line into string LineStream
				istringstream LineStream(FileLine);

				// Tokenize the LineStream to initialise the strings and attributes of the journey and the passenger
				list<string> TokenA;
				string tok;
				// Splitting LineStream in tokens separated by the charachter ","

				while (getline(LineStream, tok, ',')) {
					TokenA.push_back(tok);
					if (tok.find("Transfer_N") != string::npos) {
						CounterColumnsTransferStations++;
						CounterColumnsTrainServiceLegs++;
					}
				}
			} else { // Reading the very first line of the file to count the number of columns dedicated to passenger transfers in the Route Choice File

				// Streaming file line into string LineStream
				istringstream LineStream(FileLine);

				// Tokenize the LineStream to initialise the strings and attributes of the journey and the passenger
				list<string> TokenA;
				string tok;
				// Splitting LineStream in tokens separated by the charachter ","

				while (getline(LineStream, tok, ',')) {
					TokenA.push_back(tok);
				}

				string Pax_ID, FinalDestinationID, NumberTransfers;
				list<string>::iterator it = TokenA.begin();
				it++; // move iterator by one element such to point the Passenger ID
				Pax_ID = *it;
				advance(it, 2); // Pointing to the final destination column in the route Choice
				FinalDestinationID = *it;
				int N_Transfers = 0;

				// Advance iterator to total number of transfers
				advance(it, 5);
				NumberTransfers = *it;
				N_Transfers = atoi((char*)NumberTransfers.c_str());

				// Iterating across the list of all passengers to set route choices details on train service and transfer stations
				for (list<Passenger>::iterator p = All_Pax.begin(); p != All_Pax.end(); p++) {
					if (p->ID == Pax_ID) {
						// iterate over the planned Journeys of the passengers to identify journey with selected Destination
						for (list<Journey>::iterator j = p->Journeys.begin(); j != p->Journeys.end(); j++) {
							if (j->Arr_Station_ID == FinalDestinationID) {
								if (N_Transfers == 0) { // i there are no transfers in the journey
									// Create only one trip for the Journey
									Trip T;
									T.JourneyID = j->ID;
									T.Trip_Activity_Type = j->Journey_Activity_Type;

									T.Planned_Departure_Time = j->Actual_Planned_Departure_Time;
									T.Planned_Arrival_Time = j->Actual_Planned_Arrival_Time;
									T.Dep_Station_ID = j->Dep_Station_ID;
									T.Arr_Station_ID = j->Arr_Station_ID;
									T.TripID = to_string(1);
									// if there are no transfers in this journey then the iterator is advanced to point directly at the fiest column of the used train service line
									int NumberOfpositionsToAdvanceTokenIterator = CounterColumnsTransferStations + 1;
									advance(it, NumberOfpositionsToAdvanceTokenIterator);

									T.TrainServiceDescription = *it; // Assigning the train service line of the trip

									j->Trips.push_back(T); // Push the trip in the Journey of the passenger
									j->N_Trips++;		   // Increase number of trips for that Journey

								} else {							 // if there are transfers then more trips shall be created
									list<string> TransferStations;	 // string of the station where the transfer will occur
									list<string> TransferTrainLines; // string of the train service lines taken in each trip in between transfers

									for (int t = 0; t < N_Transfers; t++) {
										it++; // advance iterator of Token A
										TransferStations.push_back(*it);
									}
									// This variable indicates of how many positions the iterator will need to be moved to reach the column where the train service lines are provided
									int NumberOfpositionsToAdvanceTokenIterator = CounterColumnsTransferStations - N_Transfers;
									// advance iterator to point at the first column where the train service lines are given
									advance(it, NumberOfpositionsToAdvanceTokenIterator);
									for (int r = 0; r < N_Transfers + 1; r++) {
										it++;
										TransferTrainLines.push_back(*it);
									}

									// Defining the trips in the journeys
									for (int trip = 0; trip < N_Transfers + 1; trip++) {

										list<string>::iterator TransfStat;
										list<string>::iterator TransLine;
										if (trip == 0) {
											TransfStat = TransferStations.begin();
											TransLine = TransferTrainLines.begin();
											Trip T;
											T.Dep_Station_ID = j->Dep_Station_ID;
											T.Arr_Station_ID = *TransfStat;
											T.Trip_Activity_Type = j->Journey_Activity_Type;
											T.TrainServiceDescription = *TransLine;
											T.JourneyID = j->ID;
											T.Planned_Departure_Time = j->Actual_Planned_Departure_Time;
											T.TripID = to_string(trip + 1);
											j->Trips.push_back(T);
											j->N_Trips++;
										} else if (trip == N_Transfers) { // else if this is the last trip
											TransfStat = TransferStations.end();
											TransfStat--; // pointing to the last element of the transfer stations
											TransLine = TransferTrainLines.end();
											TransLine--; // pointing to the last element of the transfer lines
											Trip T;

											T.Dep_Station_ID = *TransfStat;
											T.Arr_Station_ID = j->Arr_Station_ID;
											T.Planned_Arrival_Time = j->Actual_Planned_Arrival_Time; // setting as planned arrival time of the last trip of the journey the Planned arrival time deriving from the DAS ( already randomised within the defined 30 min time window)
											T.Trip_Activity_Type = j->Journey_Activity_Type;
											T.TrainServiceDescription = *TransLine;
											T.JourneyID = j->ID;

											T.TripID = to_string(trip + 1);
											j->Trips.push_back(T);
											j->N_Trips++;

										} else { // for all the other cases in between the first and last trip of the journey
											TransfStat = TransferStations.begin();
											TransLine = TransferTrainLines.begin();

											// advancing the iterators to indicate the destination and line of the trip
											advance(TransfStat, trip);
											advance(TransLine, trip);
											Trip T;

											T.Arr_Station_ID = *TransfStat;
											TransfStat--; // shifting to the previous transfer station in the list of transfer stations as that will be the departure of the trip
											T.Dep_Station_ID = *TransfStat;
											T.Trip_Activity_Type = j->Journey_Activity_Type;
											T.TrainServiceDescription = *TransLine;
											T.JourneyID = j->ID;

											T.TripID = to_string(trip + 1);
											j->Trips.push_back(T);
											j->N_Trips++;
										}
									}
								}
							}
						}
					}
				}
			}

			linecounter++;
		}
	}
}

void assignPlatformsToTripsInJourney(list<Passenger>& ALLPAX, list<StationPlatform> ALLPLATFORMS) {
	if (ALLPAX.empty() != 1) { // if the list of all passengers is not empty
		for (list<Passenger>::iterator p = ALLPAX.begin(); p != ALLPAX.end(); p++) {
			if (p->Journeys.empty() != 1) { // if the passengers has journeys to do
				for (list<Journey>::iterator j = p->Journeys.begin(); j != p->Journeys.end(); j++) {
					list<Trip>::iterator trip = j->Trips.begin();
					while (trip != j->Trips.end()) {
						string DepartureStation, ArrivalStation, TrainServiceID;
						DepartureStation = trip->Dep_Station_ID;
						ArrivalStation = trip->Arr_Station_ID;
						TrainServiceID = trip->TrainServiceDescription;
						bool ArrivalPlatformFound = 0, DeparturePlatformFound = 0; // defining a boolean which turns to 1 when arrival and departure platforms are found
						// Iterate across StationPlatforms to identiy at which platform the train service indicated by the trip stops at for both arrival and departure stations
						for (list<StationPlatform>::iterator plat = ALLPLATFORMS.begin(); plat != ALLPLATFORMS.end(); plat++) {
							if (plat->StationID == ArrivalStation) {
								if (plat->List_Trains_Stopping_At_Platform.empty() != 1) {
									for (list<string>::iterator service = plat->List_Trains_Stopping_At_Platform.begin(); service != plat->List_Trains_Stopping_At_Platform.end(); service++) {
										if (*service == TrainServiceID) {			  // if this platform contains the train service that the passenger has assigned for the trip
											trip->Arr_Station_Platform_ID = plat->ID; // Then assign that platform ID as the Platform ID of the arrival station
											ArrivalPlatformFound = true;			  // Set the boolean to true
										}
									}
								}
							}

							if (plat->StationID == DepartureStation) {
								if (plat->List_Trains_Stopping_At_Platform.empty() != 1) {
									for (list<string>::iterator service = plat->List_Trains_Stopping_At_Platform.begin(); service != plat->List_Trains_Stopping_At_Platform.end(); service++) {
										if (*service == TrainServiceID) {			  // if this platform contains the train service that the passenger has assigned for the trip
											trip->Dep_Station_Platform_ID = plat->ID; // Then assign that platform ID as the Platform ID of the arrival station
											DeparturePlatformFound = true;			  // Set the boolean to true
										}
									}
								}
							}
							if ((ArrivalPlatformFound == 1) && (DeparturePlatformFound == 1)) { // break the for loop on platforms once both the arrival and departure station platform for that trip have been found
								break;
							}
						}

						trip++; // advance the trip iterator of one position in the trip ist in the journeys j
					}
				}
			}
		}
	}
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
