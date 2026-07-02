#include "Capacity.h"

MacroscopicEvent::MacroscopicEvent() {
	Time = -1;
	EventID = -1;
	TrainLineID = FarStation = EventType = trackNo = Station = SegmentNo = "None";
	direction = hourlyrunNo = -1;
}

list<MacroscopicEvent> ListOfAllMacroEvents; // Defining the list of all the macroscopic events in the network

MacroscopicProcess::MacroscopicProcess() {
	Mindur = -9999;
	Maxdur = -9999;
	IDEvent1 = IDEvent2 = -1;
	BlockTimeCritical = -1;
	TypeProcess = "None";
}

TrainInArea::TrainInArea() {
	trainDescription = "None";
	ShiftInArea = 99999999;
	EntryTime = 99999999;
	ExitTime = -99999999;
	N_BlockTime = 0;
	IsRouteReversed = false;
	CriticalBlock = CriticalTrain = "None";
	ShiftedEntryTimeInArea = -99999999;
	CriticalHeadway = 99999999;
}

NetworkArea::NetworkArea() {
	Name = "None";
	XStart = XEnd = -1;
	N_TrainsCrossingArea = 0;
	trackLineId = -9999;
	EarliestOccTime = 99999999;
	LatestRelTime = -99999999;
	TotalOccupationTime = -1;
	MostCriticalTrainCouple = "None";
	MostCriticalLocation = "None";
	MostCriticalTrainHeadway = 99999999;
	TrainsPerHour = -1;
	PercentageTTCapOccupation = -1;
	SignallingLevel = -99999999;
	StationPosition = -1;
}

// This is the list of all the Areas of the network
list<NetworkArea> AllNetworkAreas;
int N_NetworkAreas = 0;

// Function that determines the correct departure headway to avoid conflicts
double computeEntryTimesToSolveConflicts(BlockingTimes A, BlockingTimes B, double DepTimeTrain1, double DepTimeTrain2) {
	double ShiftedDepTime = -1;
	double overlap = -1;

	if ((A.StartOccTime > B.EndOccTime) && (A.EndOccTime > B.EndOccTime)) { // if the blocks do not overlap
		overlap = abs(A.StartOccTime - B.EndOccTime);
		ShiftedDepTime = DepTimeTrain1 - overlap; // Subtract the overlap to their arrival distance
	} else {									  // if the blocks overlap or block A is positioned totally above block B
		overlap = abs(B.EndOccTime - A.StartOccTime);
		ShiftedDepTime = DepTimeTrain1 + overlap; // Add the overlap from the arrival distance
	}

	return ShiftedDepTime;
}

// Function to compute the Capacity occupation and the TPH for all the areas in the network
void computeCapacityAndTphForAllNetworkAreas(list<NetworkArea>& NA, Train* Trains, int numTrains, string MainFolder) {
	string ResultFolderName;
	ResultFolderName = ResultFolderName + MainFolder + "/AreaResults";
	_mkdir((char*)ResultFolderName.c_str());
	if (NA.empty() != 1) {
		for (list<NetworkArea>::iterator n = NA.begin(); n != NA.end(); n++) {
			n->SetTrainsInNetworkArea(Trains, numTrains);			// Setting the trains crossing the area
			n->ComputeAreaCapacityOccupationAndTPH_For_Timetable(); // Computing Capacity in the area of the timetable withut compressing train path and leaving white space in between
			n->PrintTimetableAreaResults(ResultFolderName);			// Printing the timetable results in the area

			n->ComputeAreaCapacityOccupationAndTPH(); // Computing the capacity in the area by compressing train path with UIC Code 406
		}
	}
	// After that all the subAreas of the network have been initialised then initialise the stats for the Entire Network
	Compute_Capacity_And_TPH_For_Entire_Network(NA, Trains, numTrains);
	// Print All the results for all the network Areas
	for (list<NetworkArea>::iterator n = NA.begin(); n != NA.end(); n++) {
		n->PrintAreaResults(ResultFolderName); // Print all the results
	}
}

// Function to compute capacity for the entire network
void Compute_Capacity_And_TPH_For_Entire_Network(list<NetworkArea>& AllNetworkAreas, Train* Trains, int numTrains) {
	NetworkArea TotalNetwork;
	TotalNetwork.Name = "EntireNetwork";
	TotalNetwork.N_TrainsCrossingArea = numTrains;
	for (int i = 0; i < numTrains; i++) {
		if (Trains[i].N_BlockTimeComplete > 0) {
			for (int j = 0; j < Trains[i].N_BlockTimeComplete; j++) {
				// finding the earliest Occupation Time
				if (Trains[i].BlockTime[j].StartOccTime < TotalNetwork.EarliestOccTime) {
					TotalNetwork.EarliestOccTime = Trains[i].BlockTime[j].StartOccTime;
				}
				// finding the latest release time
				if (Trains[i].BlockTime[j].EndOccTime > TotalNetwork.LatestRelTime) {
					TotalNetwork.LatestRelTime = Trains[i].BlockTime[j].EndOccTime;
				}
			}
		}
	}
	// Determine the TotalOccupationTime
	TotalNetwork.TotalOccupationTime = TotalNetwork.LatestRelTime - TotalNetwork.EarliestOccTime;
	// Identify most critical Train couple, headway and location/block section
	if (AllNetworkAreas.empty() != 1) {
		for (list<NetworkArea>::iterator it = AllNetworkAreas.begin(); it != AllNetworkAreas.end(); it++) {
			if (it->MostCriticalTrainHeadway < TotalNetwork.MostCriticalTrainHeadway) {
				TotalNetwork.MostCriticalTrainHeadway = it->MostCriticalTrainHeadway;
				TotalNetwork.MostCriticalLocation = it->MostCriticalLocation;
				TotalNetwork.MostCriticalTrainCouple = it->MostCriticalTrainCouple;
			}
		}
	}

	// Compute the percentage of time which trains occupy the area
	TotalNetwork.PercentageTTCapOccupation = TotalNetwork.TotalOccupationTime / initial_variables.times;
	// TPH Computation using 100% of capacity
	TotalNetwork.TrainsPerHour = TotalNetwork.N_TrainsCrossingArea / TotalNetwork.PercentageTTCapOccupation; // This is the number of trains running at max capacity during the whole simulation period "times"
																											 // The real TPH must be  relative to a period of 1 hour that is why we need to divide the TPH computed above by the times/3600
	TotalNetwork.TrainsPerHour = TotalNetwork.TrainsPerHour / (initial_variables.times / 3600);

	// Add this Entire Network element as the last element to the list of AllNetworkAreas
	AllNetworkAreas.push_back(TotalNetwork);
}

// Compute HEadways for macroscopic model for all the Network Area having a station/junction
void Compute_Headways_For_AllNetworkAreas_For_Macro_Model(list<NetworkArea>& NA, Train* Trains, int numTrains, list<MacroscopicEvent> AllMacroEvents) {

	if (NA.empty() != 1) {
		for (list<NetworkArea>::iterator n = NA.begin(); n != NA.end(); n++) {
			n->SetTrainsInNetworkArea(Trains, numTrains); // Setting the trains crossing the area
			n->ComputeHWForAllTrainsInAreaForMacroModel(AllMacroEvents);
		}
	}
}

// Function to Initialize all the Areas identified over the network
void InitializeAllNetworkAreas(string FileWithAreas, Section* BS, int N_BS) {

	ifstream InputFile;

	InputFile.open((char*)FileWithAreas.c_str(), ios::binary);

	if (!InputFile) {
		cout << "\n\nERROR:Cannot Read file with characteristics of Network Areas\n\n";
	}

	else {
		cout << "\nLoading Network Areas to investigate\n\n";
		string FileLine;
		int linecounter = 0;
		while (getline(InputFile, FileLine)) {
			NetworkArea A;
			istringstream LineStream(FileLine);
			string Element;
			int ElementCounter = 0;
			while (LineStream >> Element) {
				if (ElementCounter == 0)
					A.Name = Element;
				else if (ElementCounter == 1)
					A.XStart = atof((char*)Element.c_str());
				else if (ElementCounter == 2)
					A.XEnd = atof((char*)Element.c_str());
				else if (ElementCounter == 3)
					A.SignallingLevel = atoi((char*)Element.c_str());
				else if (ElementCounter == 4)
					A.trackLineId = atoi((char*)Element.c_str());
				ElementCounter++;
			}
			linecounter++;
			A.SetNetworkAreaBlocksAndSignalLevel(A.Name, A.XStart, A.XEnd, A.SignallingLevel, A.trackLineId, BS, N_BS);
			AllNetworkAreas.push_back(A);
		}
	}
	N_NetworkAreas = AllNetworkAreas.size();
}

void initialiseAllMacroEvents(Train* T, int N_T, list<MacroscopicEvent>& MacroEventList) {
	int EventCounter = 0; // This is the Event Counter

	// Filling in the events
	for (int i = 0; i < N_T; i++) {
		int TrainDirection; // This variable is 0 is the train route is normal and 1 if instead the train route is reversed
		if (train_route[T[i].indexOfRoute].reversed_direction == 1)
			TrainDirection = 1;
		else
			TrainDirection = 0;

		// Setting the MacroEvents for each timetabling point of the train (there are two macroscopic points for each timetabling point)
		if (T[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator t = T[i].TimetablePoints.begin(); t != T[i].TimetablePoints.end(); t++) {

				string StationAbbr, FarStationArrivalEvent, FarStationDepartureEvent, Event_TypeArrival, Event_TypeDeparture;
				int TimeArrival, TimeDeparture;

				// specifying Station Abbr
				StationAbbr = t->SuccessorID;
				list<TrainEvent>::iterator BEG = T[i].TimetablePoints.begin(); // Defining the position of the first station
				list<TrainEvent>::iterator END = T[i].TimetablePoints.end();
				END--; // Defining the position of the last station
					   // Defining the FarStation
				if ((t != BEG) && (t != END)) {
					list<TrainEvent>::iterator StationBefore = t;
					StationBefore--; // Setting Station before t
					FarStationArrivalEvent = StationBefore->SuccessorID;

					list<TrainEvent>::iterator StationAfter = t;
					StationAfter++; // Setting the station after t
					FarStationDepartureEvent = StationAfter->SuccessorID;
				}

				else if (t == BEG) {
					FarStationArrivalEvent = "START";
					list<TrainEvent>::iterator StationAfter = t;
					if (t != END) {

						StationAfter++;										  // Increase the pointer only if current station is not the last one
						FarStationDepartureEvent = StationAfter->SuccessorID; // In this case the Far Station Departure Event becomes the next station
					}

					else { // else put END as Far Station since the train departs from the last station to go to END
						FarStationDepartureEvent = "END";
					}

				}

				else if (t == END) {
					FarStationDepartureEvent = "END";
					list<TrainEvent>::iterator StationBefore = t;
					if (t != BEG) {
						StationBefore--; // Take the Station Before t if t is not also the beginning
						FarStationArrivalEvent = StationBefore->SuccessorID;
					}

					else { // The train will depart from Start if t is also the first Station
						FarStationArrivalEvent = "START";
					}
				}

				// Assigning the type of event
				if (t->Time == t->Time2) {
					Event_TypeArrival = "2-arrthru";
					Event_TypeDeparture = "1-depthru";
				} else {
					Event_TypeArrival = "3-arr";
					Event_TypeDeparture = "0-dep";
				}

				// Assigning the times
				TimeArrival = (int)t->Time;
				TimeDeparture = (int)t->Time2;

				// Writing down the arrival event
				if (FarStationArrivalEvent != "START") {
					MacroscopicEvent ArrivalEvent;
					EventCounter++; // Increase the event counter of one unit

					ArrivalEvent.EventID = EventCounter;
					ArrivalEvent.TrainLineID = t->trainDescription;
					ArrivalEvent.direction = TrainDirection;
					ArrivalEvent.hourlyrunNo = (int)T[i].ID;
					ArrivalEvent.SegmentNo = "1";
					ArrivalEvent.Station = StationAbbr;
					ArrivalEvent.trackNo = " ";
					ArrivalEvent.FarStation = FarStationArrivalEvent;
					ArrivalEvent.EventType = Event_TypeArrival;
					ArrivalEvent.Time = TimeArrival;

					MacroEventList.push_back(ArrivalEvent);
				}

				// Writing down the departure event
				if (FarStationDepartureEvent != "END") {
					MacroscopicEvent DepartureEvent;
					EventCounter++; // Increase the event counter of one unit

					DepartureEvent.EventID = EventCounter;
					DepartureEvent.TrainLineID = t->trainDescription;
					DepartureEvent.direction = TrainDirection;
					DepartureEvent.hourlyrunNo = (int)T[i].ID;
					DepartureEvent.SegmentNo = "1";
					DepartureEvent.Station = StationAbbr;
					DepartureEvent.trackNo = " ";
					DepartureEvent.FarStation = FarStationDepartureEvent;
					DepartureEvent.EventType = Event_TypeDeparture;
					DepartureEvent.Time = TimeDeparture;

					MacroEventList.push_back(DepartureEvent);
				}
			}
		}
	}
}

bool orderTrainsByEntryTime(TrainInArea A, TrainInArea B) {
	if (A.EntryTime <= B.EntryTime) {
		return true;
	} else {
		return false;
	}
}

// We overrun the bool function written below to avoid problem with orderings in C++ debugging
void orderTrainsByEntryTime(list<TrainInArea>& List) {
	list<TrainInArea> TEMPList; // Creating a temporary list that collects all the trains

	// transfer all the elements in the List to TEMPList
	TEMPList = List;
	// Empty List of all its Elements
	List.clear();
	// Now order all the elements in the TEMPList according to the entrance time of trains in the area
	while (TEMPList.size() > 0) {
		double Min = 999999999;
		for (list<TrainInArea>::iterator it = TEMPList.begin(); it != TEMPList.end(); it++) {
			if (it->EntryTime < Min) {
				Min = it->EntryTime;
			}
		}
		// Set the object TrainEvent having the minimum time
		for (list<TrainInArea>::iterator it = TEMPList.begin(); it != TEMPList.end(); it++) {
			if (it->EntryTime == Min) {
				List.push_back(*it);
				TEMPList.erase(it); // remove the minimum from the TEMPlist and put it in the  original List
				break;
			}
		}
	}
}

// Function to print out all the Events and the processes of the Macroscopic model
void Print_Macroscopic_Events_And_Processes(list<MacroscopicEvent> AllMacroEvents, list<NetworkArea> NA, string MainFolder) {
	// Initialising the File of the Events
	string FileOutName;
	FileOutName = FileOutName + MainFolder + "/Events.csv";

	ofstream FILEOUTPUT;

	FILEOUTPUT.open((char*)FileOutName.c_str(), ios::binary);

	FILEOUTPUT << "event_id,line_id,hourlyrunno,direction,segmentno,station_abbr,farstation_abbr,event_type,time,track\n";

	// Print out Events
	if (AllMacroEvents.empty() != 1) {
		for (list<MacroscopicEvent>::iterator e = AllMacroEvents.begin(); e != AllMacroEvents.end(); e++) {
			FILEOUTPUT << e->EventID << "," << e->TrainLineID << "," << e->hourlyrunNo << "," << e->direction << "," << e->SegmentNo << "," << e->Station << "," << e->FarStation << "," << e->EventType << "," << e->Time << ", \n";
		}
	}

	// Print out all the processes
	// Initialising the File of the Processes
	string FileProcessOUT;
	FileProcessOUT = FileProcessOUT + MainFolder + "/Processes.csv";
	ofstream FILEPROCESS;
	FILEPROCESS.open((char*)FileProcessOUT.c_str(), ios::binary);

	FILEPROCESS << "from_event_id,to_event_id,mindur,scheddur,maxdur,process_type\n";

	// Print all the processes between consecutive events of the same train
	if (AllMacroEvents.size() > 1) {
		list<MacroscopicEvent>::iterator k = AllMacroEvents.begin();
		list<MacroscopicEvent>::iterator u = AllMacroEvents.begin();
		k++; // This points to the second element of the list of Macroscopic Events

		while (k != AllMacroEvents.end()) {
			if (k->TrainLineID == u->TrainLineID) { // if they are events of the same train line
				if (k->Station == u->Station) {		// if they are at the same station
					if (k->Time == u->Time) {		// if the times are the same then it is a thru process
						FILEPROCESS << u->EventID << "," << k->EventID << "," << k->Time - u->Time << ", , ," << "thru" << "\n";
					} else {
						FILEPROCESS << u->EventID << "," << k->EventID << "," << k->Time - u->Time << ", , ," << "dwell" << "\n";
					}
				} else { // if they are not at the same station then it is a run process
					FILEPROCESS << u->EventID << "," << k->EventID << "," << k->Time - u->Time << ", , ," << "run" << "\n";
				}
			}

			k++;
			u++; // increase the pointers to the elements of the Macroscopic events list
		}

		// Printing all the events related to the infrastructure, i.e. all the headways
		if (NA.empty() != 1) {
			list<MacroscopicProcess> Processes;
			for (list<NetworkArea>::iterator n = NA.begin(); n != NA.end(); n++) {
				if (n->StationPosition != -1) {
					if (n->TrainsCrossingArea.empty() != 1) {
						for (list<TrainInArea>::iterator t = n->TrainsCrossingArea.begin(); t != n->TrainsCrossingArea.end(); t++) {
							if (t->HeadwaysTable.empty() != 1) {
								list<double>::iterator b = t->BlockTimeOnCriticalBlock.begin();
								for (list<TrainEvent>::iterator h = t->HeadwaysTable.begin(); h != t->HeadwaysTable.end(); h++) {
									if (h->Time != -99999) {
										MacroscopicProcess Proc;
										Proc.IDEvent1 = (int)h->Position;
										Proc.IDEvent2 = (int)h->Time2;
										Proc.Mindur = h->Time;
										Proc.TypeProcess = "infra";
										Proc.BlockTimeCritical = *b;
										Processes.push_back(Proc);
										// FILEPROCESS<<(int)h->Position<<","<<(int)h->Time2<<","<<h->Time<<", , ,"<<"infra"<<"\n";
									}
									b++;
								}
							}
						}
					}
				}
			}
			// if the list of processes is not empty then postprocess it so to put  the maxdur for all negative and 0 headways and the positive related to the events
			if (Processes.empty() != 1) {
				for (list<MacroscopicProcess>::iterator p = Processes.begin(); p != Processes.end(); p++) {
					if (p->Mindur <= 0) {
						p->Maxdur = p->Mindur - p->BlockTimeCritical;
						// Compute the Maxdur also for the process between the opposite events
						for (list<MacroscopicProcess>::iterator q = Processes.begin(); q != Processes.end(); q++) {
							if ((q->IDEvent1 == p->IDEvent2) && (q->IDEvent2 == p->IDEvent1)) {
								q->Maxdur = q->Mindur - q->BlockTimeCritical;
								break; // break then the inner for loop over the processes
							}
						}
					}
				}
				for (list<MacroscopicProcess>::iterator p = Processes.begin(); p != Processes.end(); p++) {
					if (p->Maxdur != -9999)
						FILEPROCESS << p->IDEvent1 << "," << p->IDEvent2 << "," << p->Mindur << "," << " " << "," << p->Maxdur << "," << p->TypeProcess << "\n";
					else
						FILEPROCESS << p->IDEvent1 << "," << p->IDEvent2 << "," << p->Mindur << "," << " " << "," << " " << "," << p->TypeProcess << "\n";
				}
			}
		}
	}
	FILEOUTPUT.close();

	FILEPROCESS.close();
}
