#ifndef Capacity_hpp
#define Capacity_hpp

#include "simulation/Simulation.h"

// --- MacroscopicEvent: converts microscopic EGTRAIN events to macroscopic models ---
class MacroscopicEvent {
public:
	double Time;        // Time the event occurs
	int EventID;        // Event identifier
	string TrainLineID; // Train line ID this event belongs to
	string FarStation;  // For arrivals: previous station; for departures: next station
	string EventType;   // "arrival", "departure", or "through"
	string trackNo;     // Track ID
	string Station;     // Location name (station or junction)
	string SegmentNo;   // Macroscopic segment the train path is divided into
	int direction;      // Train direction (0 or 1)
	int hourlyrunNo;    // Hourly run number

	MacroscopicEvent();
};

extern list<MacroscopicEvent> ListOfAllMacroEvents; // All macroscopic events in the network

// Initialize all macroscopic events from train data
void initialiseAllMacroEvents(Train* T, int N_T, list<MacroscopicEvent>& MacroEventList);

// --- MacroscopicProcess: a macroscopic process ---
class MacroscopicProcess {
public:
	double Mindur;
	double Maxdur;
	int IDEvent1;
	double BlockTimeCritical;
	int IDEvent2;
	string TypeProcess;
	MacroscopicProcess();
};

// Compute departure headway needed to avoid conflicts between two trains
double computeEntryTimesToSolveConflicts(BlockingTimes A, BlockingTimes B, double DepTimeTrain1, double DepTimeTrain2);

// --- TrainInArea: collects all trains crossing a given network area ---
class TrainInArea {
public:
	string trainDescription;
	list<TrainEvent> TimetablePoints;
	list<TrainEvent> HeadwaysTable;            // Headway table: trainDescription=conflicting train, Time=HW, Position=Event1 ID, Time2=Event2 ID, SuccessorID=station/area name
	list<double> BlockTimeOnCriticalBlock;     // Blocking time length on critical block for each headway
	list<BlockingTimes> BlockTime;             // Blocking times in the area
	list<BlockingTimes> BlockTimeAfterStation; // Blocking times of blocks ending after the station
	list<BlockingTimes> BlockTimeBeforeStation; // Blocking times of blocks starting before the station
	int N_BlockTime;
	bool IsRouteReversed;          // True if train route is reversed
	double ShiftInArea;            // Maximum shift possible to further compress train service
	double ShiftedEntryTimeInArea; // Maximum entry time after shifting
	double EntryTime;              // Time train enters the area
	double ExitTime;               // Time train exits the area
	string CriticalBlock;          // ID of the critical block section
	string CriticalTrain;          // Train description of the critical train
	double CriticalHeadway;        // Critical arrival-arrival headway on critical block vs critical train

	TrainInArea();

	// Initialize from train data, area boundaries, and station position (-1 if none)
	void InitializeTrainInArea(Train T, double XStart, double XEnd, double StationPos) {
		trainDescription = T.trainDescription;

		this->IsRouteReversed = train_route[T.indexOfRoute].reversed_direction;

		if (T.N_BlockTimeComplete > 0) {
			for (int j = 0; j < T.N_BlockTimeComplete; j++) {
				if (((T.BlockTime[j].GeoPosStart >= XStart * 1000) && (T.BlockTime[j].GeoPosStart <= XEnd * 1000)) || (((T.BlockTime[j].GeoPosEnd >= XStart * 1000) && (T.BlockTime[j].GeoPosEnd <= XEnd * 1000)))) {
					this->BlockTime.push_back(T.BlockTime[j]);
					this->N_BlockTime++;
					if (StationPos != -1) { // Divide blocking times only if area has a station
						if (train_route[T.indexOfRoute].reversed_direction == 0) {
							if (T.BlockTime[j].GeoPosStart < StationPos * 1000)
								this->BlockTimeBeforeStation.push_back(T.BlockTime[j]);
							if (T.BlockTime[j].GeoPosEnd > StationPos * 1000)
								this->BlockTimeAfterStation.push_back(T.BlockTime[j]);
						} else { // Invert geo positions for reversed routes
							if (T.BlockTime[j].GeoPosEnd < StationPos * 1000)
								this->BlockTimeBeforeStation.push_back(T.BlockTime[j]);
							if (T.BlockTime[j].GeoPosStart > StationPos * 1000)
								this->BlockTimeAfterStation.push_back(T.BlockTime[j]);
						}
					}
				}
			}
		}
		if (T.TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = T.TimetablePoints.begin(); j != T.TimetablePoints.end(); j++) {
				if ((j->Position >= StationPos * 1000 - 0.0002) && (j->Position <= StationPos * 1000 + 0.0002)) { // Within 20cm of station position
					this->TimetablePoints.push_back(*j);
				}
			}
		}
	}

	// Set entry and exit times from blocking time data
	void Set_Entry_Exit_Time() {
		this->EntryTime = 99999999;
		this->ExitTime = -99999999;

		if (this->N_BlockTime > 0) {
			for (list<BlockingTimes>::iterator p = BlockTime.begin(); p != BlockTime.end(); p++) {
				if (p->StartOccTime < this->EntryTime) {
					EntryTime = p->StartOccTime;
				}
				if (p->EndOccTime > this->ExitTime) {
					ExitTime = p->EndOccTime;
				}
			}
		}
	}

	// Compute maximum shift possible in the area vs another train
	void computeShiftInArea(TrainInArea B) {
		if (this->N_BlockTime > 0) {
			for (list<BlockingTimes>::iterator it = BlockTime.begin(); it != BlockTime.end(); it++) {
				if (B.N_BlockTime > 0) {
					for (list<BlockingTimes>::iterator u = B.BlockTime.begin(); u != B.BlockTime.end(); u++) {
						bool areBlocksConnected = false;
						areBlocksConnected = it->AreOnTheSameBlock(*u);
						if (areBlocksConnected == 1) {
							double ShiftedEntryTime = computeEntryTimesToSolveConflicts(*it, *u, this->EntryTime, B.EntryTime);
							if (ShiftedEntryTime > this->ShiftedEntryTimeInArea) {
								this->ShiftedEntryTimeInArea = ShiftedEntryTime;
								this->CriticalBlock = it->BlockID;
								this->CriticalTrain = B.trainDescription;
								this->ShiftInArea = ShiftedEntryTimeInArea - EntryTime;
								this->CriticalHeadway = abs((it->StartRunTime + ShiftInArea) - u->StartRunTime);
							}
						}
					}
				}
			}
		}
	}

	// Compute headway vs another train at a timetable point
	void ComputeHwVsTrainInTimetablePoint(double TimeTrainThis, double TimeTrainB, list<BlockingTimes> This_BlockTime, list<BlockingTimes> B_BlockTime, double& Hw, double& ThisCriticalBlockTimeLength) {
		list<double> ShiftedTimesTrainB;
		list<double> ThisBlockTimeOnCriticalSection;
		Hw = -99999;
		ThisCriticalBlockTimeLength = -99999;
		if (This_BlockTime.size() >= 1) {
			for (list<BlockingTimes>::iterator it = This_BlockTime.begin(); it != This_BlockTime.end(); it++) {
				if (B_BlockTime.size() >= 1) {
					for (list<BlockingTimes>::iterator u = B_BlockTime.begin(); u != B_BlockTime.end(); u++) {
						bool areBlocksConnected = false;
						areBlocksConnected = it->AreOnTheSameBlock(*u);
						if (areBlocksConnected == 1) {
							double ShiftedEntryTimeB = ComputeShiftAtTimetablePointsByShiftingBBelowA(*it, *u, TimeTrainB);
							double BlockTimeOfThis = it->EndOccTime - it->StartOccTime;
							ShiftedTimesTrainB.push_back(ShiftedEntryTimeB);
							ThisBlockTimeOnCriticalSection.push_back(BlockTimeOfThis);
						}
					}
				}
			}
		}
		double MaxShiftedTimeTrainB = -99999999999;
		if (ShiftedTimesTrainB.empty() != 1) { // Both lists have same length
			list<double>::iterator b = ThisBlockTimeOnCriticalSection.begin();
			for (list<double>::iterator k = ShiftedTimesTrainB.begin(); k != ShiftedTimesTrainB.end(); k++) {
				if (*k > MaxShiftedTimeTrainB) {
					MaxShiftedTimeTrainB = *k;
					ThisCriticalBlockTimeLength = *b;
				}
				b++;
			}
		}

		if (MaxShiftedTimeTrainB != -99999999999) {
			Hw = MaxShiftedTimeTrainB - TimeTrainThis;
		}
	}

	// Compute headway table vs train B using macroscopic events
	void computeHeadwayVsTrainBReferToMacroEvents(list<MacroscopicEvent> AllMacroEvents, string TrainDescriptionB, double TimeTrainThis, double TimeTrainB, list<BlockingTimes> This_BlockTime, list<BlockingTimes> B_BlockTime, string EventTypeThis, string EventTypeB) {
		string stationName = "None";
		int IDEvent1 = -1, IDEvent2 = -1;
		string ThisEventType1, ThisEventType2;
		string BEventType1, BEventType2;
		double HW = -1;
		double BlockTimeLenghtOnCriticalSec = -1;
		TrainEvent HW_Element;

		// Determine event types to look for
		if (EventTypeThis == "Arrival") {
			ThisEventType1 = "3-arr";
			ThisEventType2 = "2-arrthru";
		} else if (EventTypeThis == "Departure") {
			ThisEventType1 = "0-dep";
			ThisEventType2 = "1-depthru";
		}

		if (EventTypeB == "Arrival") {
			BEventType1 = "3-arr";
			BEventType2 = "2-arrthru";
		} else if (EventTypeB == "Departure") {
			BEventType1 = "0-dep";
			BEventType2 = "1-depthru";
		}

		if (this->TimetablePoints.size() == 1) {
			list<TrainEvent>::iterator it = this->TimetablePoints.begin();
			stationName = it->SuccessorID;
		}

		if (stationName != "None") {
			bool Event1Found = false, Event2Found = false;
			if (AllMacroEvents.size() >= 1) {
				for (list<MacroscopicEvent>::iterator u = AllMacroEvents.begin(); u != AllMacroEvents.end(); u++) {
					if ((u->Station == stationName) && (u->TrainLineID == this->trainDescription) && (u->Time == TimeTrainThis) && ((u->EventType == ThisEventType1) || (u->EventType == ThisEventType2))) {
						IDEvent1 = u->EventID;
						Event1Found = true;
					}

					if ((u->Station == stationName) && (u->TrainLineID == TrainDescriptionB) && (u->Time == TimeTrainB) && ((u->EventType == BEventType1) || (u->EventType == BEventType2))) {
						IDEvent2 = u->EventID;
						Event2Found = true;
					}

					if ((Event1Found == 1) && (Event2Found == 1)) {
						break;
					}
				}
			}

			if ((IDEvent1 != -1) && (IDEvent2 != -1)) { // Only if both events found in macroscopic events list
				ComputeHwVsTrainInTimetablePoint(TimeTrainThis, TimeTrainB, This_BlockTime, B_BlockTime, HW, BlockTimeLenghtOnCriticalSec);
				HW_Element.trainDescription = TrainDescriptionB;
				HW_Element.Time = HW;
				HW_Element.Position = IDEvent1;
				HW_Element.Time2 = IDEvent2;
				HW_Element.SuccessorID = stationName;

				if (this->HeadwaysTable.empty() == 1) {
					this->HeadwaysTable.push_back(HW_Element);
					this->BlockTimeOnCriticalBlock.push_back(BlockTimeLenghtOnCriticalSec);
				} else {
					bool IsAlreadyThere = false;
					for (list<TrainEvent>::iterator p = this->HeadwaysTable.begin(); p != this->HeadwaysTable.end(); p++) {
						if ((HW_Element.Position == p->Position) && (HW_Element.Time2 == p->Time2)) {
							IsAlreadyThere = true;
							break;
						}
					}
					if (IsAlreadyThere == 0) {
						this->HeadwaysTable.push_back(HW_Element);
						this->BlockTimeOnCriticalBlock.push_back(BlockTimeLenghtOnCriticalSec);
					}
				}
			}
		}
	}

	// Compute shift vs all other trains in the ordered list and apply it
	void ComputeMaxShiftAndShiftTrainsInArea(list<TrainInArea> OrderedListOfTrains, double EarliestAreaEntryTime) {
		if (OrderedListOfTrains.empty() != 1) {
			list<TrainInArea>::iterator k = OrderedListOfTrains.begin();
			while (k->trainDescription != this->trainDescription) {
				this->computeShiftInArea(*k);
				k++;
			}
		}

		if (this->ShiftInArea != 99999999) { // ShiftInArea was calculated: shift all blocking times
			EntryTime = EntryTime + ShiftInArea;
			ExitTime = ExitTime + ShiftInArea;
			if (this->N_BlockTime > 0) {
				for (list<BlockingTimes>::iterator b = BlockTime.begin(); b != BlockTime.end(); b++) {
					b->StartApproachTime = b->StartApproachTime + ShiftInArea;
					b->StartRunTime = b->StartRunTime + ShiftInArea;
					b->StartClearTime = b->StartClearTime + ShiftInArea;
					b->EndApproachTime = b->EndApproachTime + ShiftInArea;
					b->EndRunTime = b->EndRunTime + ShiftInArea;
					b->EndClearTime = b->EndClearTime + ShiftInArea;
					b->StartOccTime = b->StartOccTime + ShiftInArea;
					b->EndOccTime = b->EndOccTime + ShiftInArea;
				}
			}
		} else {
			cout << "Train " << this->trainDescription << " does not conflict with any train in network area\n\n ";
			// Shift to reach minimum entry time and squeeze all trains together
			double ShiftToReachMinEntryTime = 0;
			ShiftToReachMinEntryTime = EarliestAreaEntryTime - this->EntryTime;
			EntryTime = EntryTime + ShiftToReachMinEntryTime;
			ExitTime = ExitTime + ShiftToReachMinEntryTime;
			if (this->N_BlockTime > 0) {
				for (list<BlockingTimes>::iterator b = BlockTime.begin(); b != BlockTime.end(); b++) {
					b->StartApproachTime = b->StartApproachTime + ShiftToReachMinEntryTime;
					b->StartRunTime = b->StartRunTime + ShiftToReachMinEntryTime;
					b->StartClearTime = b->StartClearTime + ShiftToReachMinEntryTime;
					b->EndApproachTime = b->EndApproachTime + ShiftToReachMinEntryTime;
					b->EndRunTime = b->EndRunTime + ShiftToReachMinEntryTime;
					b->EndClearTime = b->EndClearTime + ShiftToReachMinEntryTime;
					b->StartOccTime = b->StartOccTime + ShiftToReachMinEntryTime;
					b->EndOccTime = b->EndOccTime + ShiftToReachMinEntryTime;
				}
			}
		}
	}

	// Compute 4 headways between two trains for macroscopic model (2 before station/junction, 2 after)
	void computeSetOfHwVsTrainForMacroscopicModel(TrainInArea B, list<MacroscopicEvent> AllMacroEvents) {
		if ((this->TimetablePoints.empty() != 1) && (B.TimetablePoints.empty() != 1)) {
			list<TrainEvent>::iterator TTPointTrainThis = this->TimetablePoints.begin();
			list<TrainEvent>::iterator TTPointTrainB = B.TimetablePoints.begin();

			if ((this->IsRouteReversed == 1) && (B.IsRouteReversed == 1)) {
				computeHeadwayVsTrainBReferToMacroEvents(AllMacroEvents, B.trainDescription, TTPointTrainThis->Time, TTPointTrainB->Time, this->BlockTimeAfterStation, B.BlockTimeAfterStation, "Arrival", "Arrival");
				computeHeadwayVsTrainBReferToMacroEvents(AllMacroEvents, B.trainDescription, TTPointTrainThis->Time2, TTPointTrainB->Time2, this->BlockTimeBeforeStation, B.BlockTimeBeforeStation, "Departure", "Departure");
			}
			else if ((this->IsRouteReversed == 1) && (B.IsRouteReversed == 0)) {
				computeHeadwayVsTrainBReferToMacroEvents(AllMacroEvents, B.trainDescription, TTPointTrainThis->Time, TTPointTrainB->Time2, this->BlockTimeAfterStation, B.BlockTimeAfterStation, "Arrival", "Departure");
				computeHeadwayVsTrainBReferToMacroEvents(AllMacroEvents, B.trainDescription, TTPointTrainThis->Time2, TTPointTrainB->Time, this->BlockTimeBeforeStation, B.BlockTimeBeforeStation, "Departure", "Arrival");
			}
			else if ((this->IsRouteReversed == 0) && (B.IsRouteReversed == 0)) {
				computeHeadwayVsTrainBReferToMacroEvents(AllMacroEvents, B.trainDescription, TTPointTrainThis->Time2, TTPointTrainB->Time2, this->BlockTimeAfterStation, B.BlockTimeAfterStation, "Departure", "Departure");
				computeHeadwayVsTrainBReferToMacroEvents(AllMacroEvents, B.trainDescription, TTPointTrainThis->Time, TTPointTrainB->Time, this->BlockTimeBeforeStation, B.BlockTimeBeforeStation, "Arrival", "Arrival");
			}
			else if ((this->IsRouteReversed == 0) && (B.IsRouteReversed == 1)) {
				computeHeadwayVsTrainBReferToMacroEvents(AllMacroEvents, B.trainDescription, TTPointTrainThis->Time2, TTPointTrainB->Time, this->BlockTimeAfterStation, B.BlockTimeAfterStation, "Departure", "Arrival");
				computeHeadwayVsTrainBReferToMacroEvents(AllMacroEvents, B.trainDescription, TTPointTrainThis->Time, TTPointTrainB->Time2, this->BlockTimeBeforeStation, B.BlockTimeBeforeStation, "Arrival", "Departure");
			}
		}
	}
};

bool orderTrainsByEntryTime(TrainInArea A, TrainInArea B);

// Order trains by entry time (avoids C++ debug ordering issues)
void orderTrainsByEntryTime(list<TrainInArea>& List);

// --- NetworkArea: all block sections belonging to a given area ---
class NetworkArea {
public:
	string Name;
	double XStart, XEnd;
	list<string> BlockSections; // Block section IDs (or locations for ETCS L3)
	int trackLineId;            // Track line ID in this area. Default -9999.
	// To assign a different signalling level to a specific track line within an area, add a row in the Areas input
	// file with: Name, Start, End, SignallingLevel, TrackLineID. This creates a new area named "AreaName_TrackLineID".

	list<TrainInArea> TrainsCrossingArea;
	string MostCriticalTrainCouple;  // Couple of trains with the most critical headway
	string MostCriticalLocation;     // Location of the most critical headway
	double MostCriticalTrainHeadway; // Headway of the most critical couple at the most critical location
	int SignallingLevel;             // Signalling level in this area
	int N_TrainsCrossingArea;
	double StationPosition;          // Coordinate of station in area, or -1 if none
	double EarliestOccTime, LatestRelTime;
	double TotalOccupationTime;
	double TrainsPerHour;
	double PercentageTTCapOccupation; // Percentage of capacity consumed by timetable

	NetworkArea();

	// Reset all properties except name, boundaries, signalling level, and block sections
	void ResetNetworkArea() {
		N_TrainsCrossingArea = 0;
		EarliestOccTime = 99999999;
		LatestRelTime = -99999999;
		TotalOccupationTime = -1;
		MostCriticalTrainCouple = "None";
		MostCriticalLocation = "None";
		MostCriticalTrainHeadway = 99999999;
		TrainsPerHour = -1;
		PercentageTTCapOccupation = -1;
		TrainsCrossingArea.clear();
	}

	// Compute earliest occupation, latest release, and total occupation time of the area
	void ComputeTotalOccupationOfNetworkArea() {
		this->EarliestOccTime = 99999999;
		this->LatestRelTime = -99999999;
		this->TotalOccupationTime = -1;
		if (TrainsCrossingArea.empty() != 1) {
			for (list<TrainInArea>::iterator it = TrainsCrossingArea.begin(); it != TrainsCrossingArea.end(); it++) {
				if (it->EntryTime < this->EarliestOccTime) {
					EarliestOccTime = it->EntryTime;
				}
				if (it->ExitTime > this->LatestRelTime) {
					LatestRelTime = it->ExitTime;
				}
			}
		}

		if ((LatestRelTime != -99999999) && (EarliestOccTime != 99999999)) {
			this->TotalOccupationTime = LatestRelTime - EarliestOccTime;
		}
	}

	// Set block sections and signalling level for the network area
	void SetNetworkAreaBlocksAndSignalLevel(string name, double xstart, double xend, int siglevel, int TrackLine_ID, Section* BS, int N_BS) {
		Name = name;
		XStart = xstart;
		XEnd = xend;
		SignallingLevel = siglevel;
		trackLineId = TrackLine_ID;

		if (trackLineId == -9999) { // Apply signalling level to all track lines in area
			for (int i = 0; i < N_BS; i++) {
				if (((BS[i].GeoXBegNode >= XStart * 1000) && (BS[i].GeoXBegNode <= XEnd * 1000)) || ((BS[i].GeoXEndNode >= XStart * 1000) && (BS[i].GeoXEndNode <= XEnd * 1000))) {
					if (BS[i].SignallingLevel == -99999999) { // Only assign if not already set
						BS[i].SignallingLevel = this->SignallingLevel;
					}
					if (BlockSections.empty() == 1) {
						BlockSections.push_back(BS[i].ID);
					} else {
						bool IsBlockThere = false;
						for (list<string>::iterator p = BlockSections.begin(); p != BlockSections.end(); p++) {
							if (*p == BS[i].ID) {
								IsBlockThere = true;
								break;
							}
						}
						if (IsBlockThere == 0) {
							BlockSections.push_back(BS[i].ID);
						}
					}
				}
			}
		} else { // Specific track line: create area with TrackLine_ID suffix
			Name = Name + "_" + tostring(TrackLine_ID);

			for (int i = 0; i < N_BS; i++) {
				// Match either the block's track line or the second connected track line (for diverging switches
				// that allow trains to move between track lines with different signalling levels)
				if ((BS[i].trackLineId == trackLineId) || (BS[i].SecondConnectedTrackLineID == trackLineId)) {
					if (((BS[i].GeoXBegNode >= XStart * 1000) && (BS[i].GeoXBegNode <= XEnd * 1000)) || ((BS[i].GeoXEndNode >= XStart * 1000) && (BS[i].GeoXEndNode <= XEnd * 1000))) {
						BS[i].SignallingLevel = this->SignallingLevel;

						if (BlockSections.empty() == 1) {
							BlockSections.push_back(BS[i].ID);
						} else {
							bool IsBlockThere = false;
							for (list<string>::iterator p = BlockSections.begin(); p != BlockSections.end(); p++) {
								if (*p == BS[i].ID) {
									IsBlockThere = true;
									break;
								}
							}
							if (IsBlockThere == 0) {
								BlockSections.push_back(BS[i].ID);
							}
						}
					}
				}
			}
		}

		// Set station position if area name matches a station name
		for (int s = 0; s < numStations; s++) {
			if (StationArray[s].stationName == this->Name)
				this->StationPosition = StationArray[s].X;
		}
	}

	// Initialize trains present in this network area
	void SetTrainsInNetworkArea(Train* Trains, int numTrains) {
		for (int i = 0; i < numTrains; i++) {
			bool IsTrainInArea = false;
			if (Trains[i].N_BlockTimeComplete > 0) {
				for (int j = 0; j < Trains[i].N_BlockTimeComplete; j++) {
					if (((Trains[i].BlockTime[j].GeoPosStart >= this->XStart * 1000) && (Trains[i].BlockTime[j].GeoPosStart <= this->XEnd * 1000)) || (((Trains[i].BlockTime[j].GeoPosEnd >= this->XStart * 1000) && (Trains[i].BlockTime[j].GeoPosEnd <= this->XEnd * 1000)))) {
						if (IsTrainInArea == 0) {
							IsTrainInArea = true;
							break;
						}
					}
				}
			}

			if (IsTrainInArea == 1) {
				TrainInArea TA;
				TA.InitializeTrainInArea(Trains[i], this->XStart, this->XEnd, this->StationPosition);
				TA.Set_Entry_Exit_Time();
				if (N_TrainsCrossingArea == 0) {
					this->TrainsCrossingArea.push_back(TA);
					this->N_TrainsCrossingArea++;
				} else {
					bool IsTrainThere = false;
					for (list<TrainInArea>::iterator it = TrainsCrossingArea.begin(); it != TrainsCrossingArea.end(); it++) {
						if (it->trainDescription == TA.trainDescription) {
							IsTrainThere = true;
							break;
						}
					}
					if (IsTrainThere == 0) {
						this->TrainsCrossingArea.push_back(TA);
						this->N_TrainsCrossingArea++;
					}
				}
			}
		}
		// Order trains by entry time
		if (this->N_TrainsCrossingArea > 0) {
			orderTrainsByEntryTime(this->TrainsCrossingArea);
		}
		ComputeTotalOccupationOfNetworkArea();
	}

	// Compute headways among all trains in the area (for areas with stations/junctions)
	void ComputeHWForAllTrainsInAreaForMacroModel(list<MacroscopicEvent> AllMacroEvents) {
		if (this->StationPosition != -1) {
			if (this->TrainsCrossingArea.empty() != 1) {
				for (list<TrainInArea>::iterator t = this->TrainsCrossingArea.begin(); t != this->TrainsCrossingArea.end(); t++) {
					for (list<TrainInArea>::iterator k = this->TrainsCrossingArea.begin(); k != this->TrainsCrossingArea.end(); k++) {
						if (k->trainDescription != t->trainDescription) {
							t->computeSetOfHwVsTrainForMacroscopicModel(*k, AllMacroEvents);
						}
					}
				}
			}
		}
	}

	// Compress all trains in area starting from the second one
	void compressAllTrainsInArea() {
		if (this->TrainsCrossingArea.size() > 1) {
			list<TrainInArea>::iterator t = this->TrainsCrossingArea.begin();
			t++;

			while (t != TrainsCrossingArea.end()) {
				t->ComputeMaxShiftAndShiftTrainsInArea(TrainsCrossingArea, this->EarliestOccTime);
				t++;
			}
		}
	}

	// Identify the most critical train couple and location
	void IdentifyMostCriticalTrainCouple() {
		if (TrainsCrossingArea.empty() != 1) {
			for (list<TrainInArea>::iterator t = TrainsCrossingArea.begin(); t != TrainsCrossingArea.end(); t++) {
				if (t->CriticalHeadway < this->MostCriticalTrainHeadway) {
					this->MostCriticalTrainHeadway = t->CriticalHeadway;
					this->MostCriticalLocation = t->CriticalBlock;
					this->MostCriticalTrainCouple = t->trainDescription + "/" + t->CriticalTrain;
				}
			}
		}
	}

	// Compute timetable capacity occupation (without compression, includes buffer times)
	void ComputeAreaCapacityOccupationAndTPH_For_Timetable() {
		this->ComputeTotalOccupationOfNetworkArea();
		IdentifyMostCriticalTrainCouple();

		PercentageTTCapOccupation = this->TotalOccupationTime / initial_variables.times;
		TrainsPerHour = N_TrainsCrossingArea / PercentageTTCapOccupation;
		TrainsPerHour = TrainsPerHour / (initial_variables.times / 3600);
	}

	// Compute capacity occupation in area (with compression)
	void ComputeAreaCapacityOccupationAndTPH() {
		this->compressAllTrainsInArea();
		this->ComputeTotalOccupationOfNetworkArea();
		IdentifyMostCriticalTrainCouple();

		PercentageTTCapOccupation = this->TotalOccupationTime / initial_variables.times;
		TrainsPerHour = N_TrainsCrossingArea / PercentageTTCapOccupation;
		TrainsPerHour = TrainsPerHour / (initial_variables.times / 3600);
	}

	// Print blocking times for trains in the area
	void PrintBlockingTimesInArea(string FolderName, string FileType) {
		string FileOUTName;
		FileOUTName = FileOUTName + FolderName + "/" + FileType + "_" + this->Name + ".txt";
		ofstream OUTPUT;
		OUTPUT.open((char*)FileOUTName.c_str(), ios::binary);

		if (TrainsCrossingArea.size() > 0) {
			for (list<TrainInArea>::iterator t = TrainsCrossingArea.begin(); t != TrainsCrossingArea.end(); t++) {
				OUTPUT << t->trainDescription << "\n";
				if (t->N_BlockTime > 0) {
					for (list<BlockingTimes>::iterator b = t->BlockTime.begin(); b != t->BlockTime.end(); b++) {
						OUTPUT << b->BlockID << " ";
					}
					OUTPUT << "\n";

					for (list<BlockingTimes>::iterator b = t->BlockTime.begin(); b != t->BlockTime.end(); b++) {
						OUTPUT << b->GeoPosStart << " ";
					}
					OUTPUT << "\n";

					for (list<BlockingTimes>::iterator b = t->BlockTime.begin(); b != t->BlockTime.end(); b++) {
						OUTPUT << b->GeoPosEnd << " ";
					}
					OUTPUT << "\n";

					for (list<BlockingTimes>::iterator b = t->BlockTime.begin(); b != t->BlockTime.end(); b++) {
						OUTPUT << b->StartOccTime << " ";
					}
					OUTPUT << "\n";

					for (list<BlockingTimes>::iterator b = t->BlockTime.begin(); b != t->BlockTime.end(); b++) {
						OUTPUT << b->EndOccTime << " ";
					}
				}
				OUTPUT << "\n\n";
			}
		}
		OUTPUT.close();
	}

	void PrintAreaStatistics(string FolderName, string FileType) {
		string FileName;
		FileName = FolderName + "/" + FileType + "_" + this->Name + ".txt";
		ofstream OUTPUT;
		OUTPUT.open((char*)FileName.c_str(), ios::binary);
		OUTPUT << "N_Trains_Scheduled EarliestEntryTime[s] LatestRelTime[s] TotalOccupationTime[s] %TTCapacityConsumption CriticalTrainCouple CriticalLocation CriticalHW[s] PotentialTPH\n";
		OUTPUT << this->N_TrainsCrossingArea << " " << this->EarliestOccTime << " " << this->LatestRelTime << " " << this->TotalOccupationTime << " " << this->PercentageTTCapOccupation << " " << this->MostCriticalTrainCouple << " " << this->MostCriticalLocation << " " << this->MostCriticalTrainHeadway << " " << this->TrainsPerHour << "\n";
		OUTPUT.close();
	}

	void PrintAreaResults(string MainFolderName) {
		string NameFolder;
		NameFolder = NameFolder + MainFolderName + "/" + this->Name + "_Results";
		_mkdir((char*)NameFolder.c_str());
		PrintBlockingTimesInArea(NameFolder, "BlockTrainsInArea");
		PrintAreaStatistics(NameFolder, "Statistics_Area");
	}

	// Print timetable results for this area
	void PrintTimetableAreaResults(string MainFolderName) {
		string NameFolder;
		NameFolder = NameFolder + MainFolderName + "/" + this->Name + "_Results";
		_mkdir((char*)NameFolder.c_str());
		PrintBlockingTimesInArea(NameFolder, "TT_BlockTrainsInArea");
		PrintAreaStatistics(NameFolder, "TT_Statistics_Area");
	}
};

extern list<NetworkArea> AllNetworkAreas;
extern int N_NetworkAreas;

// Initialize all network areas from input file
void InitializeAllNetworkAreas(string FileWithAreas, Section* BS, int N_BS);

// Compute capacity and TPH for the entire network
void Compute_Capacity_And_TPH_For_Entire_Network(list<NetworkArea>& AllNetworkAreas, Train* Trains, int numTrains);

// Compute capacity occupation and TPH for all areas
void computeCapacityAndTphForAllNetworkAreas(list<NetworkArea>& NA, Train* Trains, int numTrains, string MainFolder);

// Compute headways for macroscopic model for all areas with stations/junctions
void Compute_Headways_For_AllNetworkAreas_For_Macro_Model(list<NetworkArea>& NA, Train* Trains, int numTrains, list<MacroscopicEvent> AllMacroEvents);

// Print all macroscopic events and processes
void Print_Macroscopic_Events_And_Processes(list<MacroscopicEvent> AllMacroEvents, list<NetworkArea> NA, string MainFolder);

#endif
