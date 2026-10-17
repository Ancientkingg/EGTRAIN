#include "simulation/RollingStock.h"
#include "scene/SignallingLevel.h"
#include "simulation/SimulationRun.h"
#include "util/Log.h"

#include <cmath>
#include <list>
#include <string>

using namespace std;

BlockingTimes& BlockingTimes::operator=(const BlockingTimes& obj2) {
	BlockID = obj2.BlockID;
	trainDescription = obj2.trainDescription;
	StartRunTime = obj2.StartRunTime;
	EndRunTime = obj2.EndRunTime;
	NextBlockID = obj2.NextBlockID;
	this->NextBlockIDPreviousTrain = obj2.NextBlockIDPreviousTrain;
	StartApproachTime = obj2.StartApproachTime;
	EndApproachTime = obj2.EndApproachTime;
	StartClearTime = obj2.StartClearTime;
	EndClearTime = obj2.EndClearTime;
	length = obj2.length;
	StartOccTime = obj2.StartOccTime;
	EndOccTime = obj2.EndOccTime;
	setupTime = obj2.setupTime;
	sightReacTime = obj2.setupTime;
	ApproachTime = obj2.ApproachTime;
	RunTime = obj2.RunTime;
	clearingTime = obj2.clearingTime;
	ReleaseTime = obj2.ReleaseTime;
	RunTimeMargin = obj2.RunTimeMargin;
	PosStart = obj2.PosStart;
	PosEnd = obj2.PosEnd;
	SignallingLevel = obj2.SignallingLevel;
	stationName = obj2.stationName;
	IsComplete = obj2.IsComplete;
	GeoPosStart = obj2.GeoPosStart;
	GeoPosEnd = obj2.GeoPosEnd;
	this->LocationWithSwitch = obj2.LocationWithSwitch;
	this->SpeedPreviousTrain = obj2.SpeedPreviousTrain;
	this->ConnectedBlockingTimeID = obj2.ConnectedBlockingTimeID;
	this->InfraElementInPositionForTrain = obj2.InfraElementInPositionForTrain;
	this->SwitchName = obj2.SwitchName;
	IsAlreadyUniformedToConnectedSwitch = obj2.IsAlreadyUniformedToConnectedSwitch;
	NamePreviousTrain = obj2.NamePreviousTrain;
	this->PosConnectedBlockingTime = obj2.PosConnectedBlockingTime;
	this->IsEndOfDivSwitchBeingStartOfADivSwitch = obj2.IsEndOfDivSwitchBeingStartOfADivSwitch;
	return *this;
}

void BlockingTimes::checkIfLastInfraElementStatusIsInLineWithTrainNeeds(string TrainName, list<InfraEvent> ListAllInfraEvents) {
	bool IsStatusInfraElementFound = false; // this variable becomes true only when we have found the status of the infrastructure element we are computing the blocking times for, given a train
	string NameOfInfraElement;
	string NeededInfraElementPosition;
	string LastInfraElementPosition;

	// Determine the needed status of infrastructure for the train

	if (this->LocationWithSwitch == 1) {
		NameOfInfraElement = this->SwitchName;					   // The name of the location is the Switch Name if the infrastructure element is a Switch
		if (this->ConnectedBlockingTimeID != "None") {			   // if the BlockTime has a connected block time then it means that it is a switch in a diverging position
			if (this->PosStart < this->PosConnectedBlockingTime) { // if this element is the starting edge of a diverging switch where the point machine is, then it needs to be diverging
				NeededInfraElementPosition = "Diverging";
			} else {													 // if instead this element is the ending edge of diverging switch beam
				if (this->IsEndOfDivSwitchBeingStartOfADivSwitch == 1) { // if this ending edge coincides with the starting edge of another diverging switch so it is needed a diverging position
					NeededInfraElementPosition = "Diverging";
				} else { // otherwise we need a straight position
					NeededInfraElementPosition = "Straight";
				}
			}
		} else {
			NeededInfraElementPosition = "Straight";
		}
	} else {
		NameOfInfraElement = this->BlockID;
		// Infra elements other than switches are always in position for the train so InfraElementInPositionForTrain shall always be equal to its default values
		NeededInfraElementPosition = "None"; // there is no needed InfraElementPosition to ask for so we put it to None, the LastInfraStatus of infra elements which are not switches are always put to None.
											 // so that will always give a true InfraElementInPositionForTrain
	}
	// In case no name has been assigned to the infrastructure element then throw an error
	if (NameOfInfraElement.empty() == 1) {
		EG_LOG(SimSetup, Error) << "ERROR 988: Name of InfraElement has not been assigned in the function to update the status of infrastructure elements for Block Section " << this->BlockID << " on Route of Train " << TrainName << "\n";
	}

	else {
		;
		//.log("Infra element : " + NameOfInfraElement);
	}

	bool ElementFound = false;

	// Now Determining the last status of the infrastructure element as resulting from the simulation
	if (ListAllInfraEvents.size() > 0) {
		for (list<InfraEvent>::iterator i = ListAllInfraEvents.begin(); i != ListAllInfraEvents.end(); i++) {
			if (i->ElementInfo.ID == NameOfInfraElement) {

				ElementFound = true;

				if (i->RecordedEvent.size() > 0) {

					for (list<TrainEvent>::iterator t = i->RecordedEvent.begin(); t != i->RecordedEvent.end(); t++) {

						if (t->trainDescription == TrainName) {
							if (t == i->RecordedEvent.begin()) { // In case TrainName is the first train of the recorded list
								this->SpeedPreviousTrain = -1;
								this->NamePreviousTrain = "None";
								this->NextBlockIDPreviousTrain = "None";

								if (this->LocationWithSwitch == 1) { // If this is a switch this will be in the Straight position at start
									LastInfraElementPosition = "Straight";
								}

								else { // for all the other elements instead it will be None
									LastInfraElementPosition = "None";
								}
							} else { // if TrainName is not the first train to cross the infrastructure element
								list<TrainEvent>::iterator u = t;
								u--;											 // iterator u points at the element before t in the list of recorded event of the infrastructure element
								this->SpeedPreviousTrain = u->TrainSpeed;		 // taking the speed of the previous train crossing the infrastructure element i
								this->NamePreviousTrain = u->trainDescription;	 // taking the trainDescription of the previous train crossing i
								this->NextBlockIDPreviousTrain = u->SuccessorID; // taking the NextBlockID of the previous train crossing i

								if (this->LocationWithSwitch == 1) {			   // Comparing the previous position of infra element i with the needed position for let the train cross it
									LastInfraElementPosition = u->InfraElemStatus; // if the element is a switch then take the last position of the element
								}

								else {
									LastInfraElementPosition = "None"; // if that is not a switch its position will be None
								}
							}
							IsStatusInfraElementFound = true; // the status of the infra element has been found so this variable turns to true
							break;							  // and break the for loop over the recorded events of the infrastructure element
						}
					}
				}
			}

			if (IsStatusInfraElementFound == 1) {
				break; // break the for loop on infrastructure element events when we have found the right element and taken the correct recorded event
			}
		}
	}

	if (ElementFound == 0) {
		EG_LOG(SimSetup, Error) << "Error: Element " << NameOfInfraElement << " Not Found\n";
	}

	if (NeededInfraElementPosition != LastInfraElementPosition) { // The Infra Element will be not in position if NeededInfraElementPosition does not coincide with the LastInfraElementPosition

		this->InfraElementInPositionForTrain = false;
	}
}

void Train::ComputeBlockingTimeForSingleLocation(int IndexCurrentBS, string SignallingType, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double AbsoluteRTSupplement, double PercentageRTSupplement) {

	BlockTime[N_BlockSections].BlockID = run_->infra.routes[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].ID;
	BlockTime[N_BlockSections].length = run_->infra.routes[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].length;
	BlockTime[N_BlockSections].PosStart = run_->infra.routes[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].start_node.X * 1000;
	BlockTime[N_BlockSections].PosEnd = run_->infra.routes[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].end_node.X * 1000;
	BlockTime[N_BlockSections].GeoPosStart = run_->infra.routes[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].GeoXBegNode; // This is in meter already
	BlockTime[N_BlockSections].GeoPosEnd = run_->infra.routes[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].GeoXEndNode;	  // This is in meter already

	BlockTime[N_BlockSections].trainDescription = this->trainDescription;

	bool FoundApprTimeETCS2 = false; // This variable becomes true when the StartApproachTime of the ETCS level 2 is found

	int p = EntryTime + 1;
	for (; p <= End_Time; p++) {
		if ((instant_spatial_position[p - 1] <= run_->infra.routes[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].start_node.X * 1000) && (instant_spatial_position[p] > run_->infra.routes[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].start_node.X * 1000)) {
			BlockTime[N_BlockSections].StartRunTime = p - 1;
		}

		if ((instant_spatial_position[p - 1] <= run_->infra.routes[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].end_node.X * 1000) && (instant_spatial_position[p] > run_->infra.routes[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].end_node.X * 1000)) {
			BlockTime[N_BlockSections].EndRunTime = p - 1;
		}

		if ((instant_spatial_position[p - 1] - train_length <= run_->infra.routes[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].end_node.X * 1000) && (instant_spatial_position[p] - train_length > run_->infra.routes[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].end_node.X * 1000)) {
			BlockTime[N_BlockSections].EndClearTime = p - 1;
		}

		if (IndexCurrentBS > 0) {
			if (SignallingType == "Conventional") {
				if ((instant_spatial_position[p - 1] <= run_->infra.routes[indexOfRoute].sequence_of_block_sections[IndexCurrentBS - 1].start_node.X * 1000) && (instant_spatial_position[p] > run_->infra.routes[indexOfRoute].sequence_of_block_sections[IndexCurrentBS - 1].start_node.X * 1000)) {
					BlockTime[N_BlockSections].StartApproachTime = p - 1;
				}

			} else if (SignallingType == "ETCS2") {
				if (FoundApprTimeETCS2 == 0) {																															   // if the startApproach time of ETCS level 2 has not been fund yet
					double BrakingDistance = pow(instant_train_speed[p], 2) / (2 * max_train_decelaration);																   // Computing the braking curve from the speed instant_train_speed[p]
					if (instant_spatial_position[p] + BrakingDistance > run_->infra.routes[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].start_node.X * 1000) { // if at instant p the instant_spatial_position[p]+BrakingDistance overcomes the entry signal of block section signalling_block_sections[i] then the StartApproachTime is instant p-1
						BlockTime[N_BlockSections].StartApproachTime = p - 1;
						FoundApprTimeETCS2 = true; // The StartApproachTime has been found that is why the variable goes to true
					}
				}
			}
		}
	}

	BlockTime[N_BlockSections].EndApproachTime = BlockTime[N_BlockSections].StartRunTime;
	BlockTime[N_BlockSections].StartClearTime = BlockTime[N_BlockSections].EndRunTime;
	BlockTime[N_BlockSections].setupTime = SetupTime;
	BlockTime[N_BlockSections].sightReacTime = SightReacTime;
	BlockTime[N_BlockSections].ReleaseTime = ReleaseTime;

	if (IndexCurrentBS == 0) { // for the first block we do not have setup and sightReaction Time nor approach time
		BlockTime[N_BlockSections].StartApproachTime = BlockTime[N_BlockSections].StartRunTime;
		BlockTime[N_BlockSections].EndApproachTime = BlockTime[N_BlockSections].StartApproachTime;
		BlockTime[N_BlockSections].setupTime = 0;
		BlockTime[N_BlockSections].sightReacTime = 0;
	}

	if (IndexCurrentBS == run_->infra.routes[indexOfRoute].N_Block_Sections - 1) { // for the last block section we do not have clearing time since the train unblocks the section as soon as its head arrives at the final point of the network
		BlockTime[N_BlockSections].EndClearTime = BlockTime[N_BlockSections].EndRunTime;
		BlockTime[N_BlockSections].StartClearTime = BlockTime[N_BlockSections].EndClearTime;
	}

	// In case the end of the block section has an ending Node whose distance from the exit point of the train is shorter than the length of the train, then the block section will never be cleared, that is why we must put this condition
	if ((BlockTime[N_BlockSections].StartClearTime != -1) && (BlockTime[N_BlockSections].EndClearTime == -1)) {
		if (BlockTime[N_BlockSections].PosEnd >= (instant_spatial_position[End_Time] - train_length)) { // this says that if the train head has crossed the end of the block section but its tail cannot because of vicinity problems to the exit point of the train
			BlockTime[N_BlockSections].EndClearTime = End_Time;											// Then the EndClearTime is equal to the exit time of the train from the network
		}
	}

	// Defining the StartOccupation and EndOccupationTime and all the other occupation times
	BlockTime[N_BlockSections].ApproachTime = BlockTime[N_BlockSections].EndApproachTime - BlockTime[N_BlockSections].StartApproachTime;
	BlockTime[N_BlockSections].RunTime = BlockTime[N_BlockSections].EndRunTime - BlockTime[N_BlockSections].StartRunTime;
	BlockTime[N_BlockSections].clearingTime = BlockTime[N_BlockSections].EndClearTime - BlockTime[N_BlockSections].StartClearTime;
	BlockTime[N_BlockSections].StartOccTime = BlockTime[N_BlockSections].StartApproachTime - BlockTime[N_BlockSections].setupTime - BlockTime[N_BlockSections].sightReacTime;
	BlockTime[N_BlockSections].EndOccTime = BlockTime[N_BlockSections].EndClearTime + BlockTime[N_BlockSections].ReleaseTime;

	// Putting the running time supplements to the blocking time
	if ((AbsoluteRTSupplement != 0) && (PercentageRTSupplement == 0)) {
		BlockTime[N_BlockSections].RunTimeMargin = AbsoluteRTSupplement / run_->infra.routes[indexOfRoute].N_Block_Sections;
		// Shift the EndOccTime of the RunTimeMargin
		BlockTime[N_BlockSections].EndOccTime = BlockTime[N_BlockSections].EndOccTime + BlockTime[N_BlockSections].RunTimeMargin;
	} else if ((AbsoluteRTSupplement == 0) && (PercentageRTSupplement != 0)) {
		BlockTime[N_BlockSections].RunTimeMargin = BlockTime[N_BlockSections].RunTime * PercentageRTSupplement / 100;
		// Shift the EndOccTime of the RunTimeMargin
		BlockTime[N_BlockSections].EndOccTime = BlockTime[N_BlockSections].EndOccTime + BlockTime[N_BlockSections].RunTimeMargin;
	}

	if ((BlockTime[N_BlockSections].StartRunTime < 0) || (BlockTime[N_BlockSections].StartOccTime < 0) || (BlockTime[N_BlockSections].EndOccTime < BlockTime[N_BlockSections].StartOccTime)) {
		BlockTime[N_BlockSections].IsComplete = false;
	}
	if (BlockTime[N_BlockSections].IsComplete == 1)
		N_BlockTimeComplete++; // Increase the number of blocking times that are complete
							   // Increase the counter of BlockSections
	this->N_BlockSections++;
}

double Train::ComputeLastOccupationTime_real_time(int t, std::string CurrentBS, int EntryTime) {
	// lastOccupationTime
	// int index=0
	// find the index of the blocksection
	// int n = sizeof(train_route[indexOfRoute].sequence_of_block_sections) / sizeof(train_route[indexOfRoute].sequence_of_block_sections[0]);
	// auto itr = find(train_route[indexOfRoute].sequence_of_block_sections, train_route[indexOfRoute].sequence_of_block_sections + n, CurrentBS);
	int IndexCurrentBS = 0; // distance(train_route[indexOfRoute].sequence_of_block_sections, itr);
	for (int h = 0; h < run_->infra.routes[indexOfRoute].N_Block_Sections; h++) {
		if (run_->infra.routes[indexOfRoute].sequence_of_block_sections[h].ID == CurrentBS) {
			IndexCurrentBS = h;
			break;
		}
	}
	// BlockTime[N_BlockSections].StartOccTime = BlockTime[N_BlockSections].StartApproachTime - BlockTime[N_BlockSections].setupTime - BlockTime[N_BlockSections].sightReacTime;
	int p = EntryTime + 1;
	double start_occupation_time = 0.0;

	for (; p <= t; p++) {
		if (instant_spatial_position[p] >= run_->infra.routes[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].start_node.X * 1000) {
			start_occupation_time = p;
			break;
		}
	}
	return start_occupation_time;
}

void Train::ComputeBlockTime_ETCSLevel_3_ForSection(int IndexCurrentBS, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsoluteRTSupplement, double PercentRTSupplement) {

	list<BlockingTimes> LocationList, FinalLocationList; // LocationList is a temporary List where we collect all the ETCS Level 3 locations belonging to the current Block Section
	int N_LocationList = 0, N_FinalLocationList = 0;	 // Final LocationList instead collects all those locations of LocationList that are not present in the BlockTime of this train (that is why those locations that actually need to be added)

	if (run_->infra.routes[this->indexOfRoute].InfrastructureElements.size() > 0) {

		for (list<InfraElement>::iterator k = run_->infra.routes[this->indexOfRoute].InfrastructureElements.begin(); k != run_->infra.routes[this->indexOfRoute].InfrastructureElements.end(); k++) {
			BlockingTimes TEMPLoc; // defining temporary Blocking Time variable

			if ((k->SectionID == run_->infra.routes[this->indexOfRoute].sequence_of_block_sections[IndexCurrentBS].ID) && (k->ID != "VirtualTDSB")) { // if the Infrastructure Element belongs to the block section and it is not a VirtualTDSB
				TEMPLoc.BlockID = k->ID;
				TEMPLoc.length = 0;
				TEMPLoc.trainDescription = this->trainDescription;
				TEMPLoc.SignallingLevel = run_->infra.routes[this->indexOfRoute].sequence_of_block_sections[IndexCurrentBS].SignallingLevel; // setting the signalling level of the infrastructure element we are computing the blocking time for
				TEMPLoc.PosStart = k->XCoordinate;
				TEMPLoc.PosEnd = TEMPLoc.PosStart;
				TEMPLoc.GeoPosStart = k->GeoXCoord;
				TEMPLoc.GeoPosEnd = TEMPLoc.GeoPosStart;

				// Is the location a station or a switch?
				if (k->stationName.empty() != 1) {
					TEMPLoc.stationName = k->stationName;
				}
				if (k->IsSwitch == 1) {
					TEMPLoc.LocationWithSwitch = true;
					TEMPLoc.SwitchName = k->SwitchName;
					if (k->ConnectedPoint != "None") {														   // if this is a diverging switch then it is connected to another point
						TEMPLoc.ConnectedBlockingTimeID = k->ConnectedPoint;								   // Connected BlockingTimeID has the ID of the connected point of the switch
						TEMPLoc.PosConnectedBlockingTime = k->XConnectedPoint;								   // Position of the connected point
						TEMPLoc.IsEndOfDivSwitchBeingStartOfADivSwitch = k->isEndOfDivSwitchStartOfADivSwitch; // Is the end of the switch also the start of another diverging switch?
					}
				}

				// Identifying the last status of the infrastructure element we are computing the blocking time for and the speed of the previous passing train
				// TEMPLoc.DetermineLastStatusOfInfraElement(this->trainDescription, InfraElementsList);
				TEMPLoc.checkIfLastInfraElementStatusIsInLineWithTrainNeeds(this->trainDescription, run_->infra.infraEvents);

				// Increase the locations of 1 element if it is not already in the list
				if (N_LocationList == 0) {
					N_LocationList++;
					LocationList.push_back(TEMPLoc);
				} else {
					bool IsAlreadyThere = false;
					for (list<BlockingTimes>::iterator l = LocationList.begin(); l != LocationList.end(); l++) {
						if ((TEMPLoc.BlockID == l->BlockID) || (TEMPLoc.GeoPosStart == l->GeoPosStart)) { // if they have the same ID or the same position
							IsAlreadyThere = true;
							break;
						}
					}
					if (IsAlreadyThere == 0) {
						N_LocationList++;
						LocationList.push_back(TEMPLoc);
					}
				}
			}
		}

		// Defining the FinalLocationList
		for (list<BlockingTimes>::iterator y = LocationList.begin(); y != LocationList.end(); y++) {
			bool IsAlreadyInBlockTime = false;
			if (this->N_BlockSections > 0) {
				for (int h = 0; h < N_BlockSections; h++) {
					if ((y->LocationWithSwitch == 1) && (y->ConnectedBlockingTimeID != "None")) { // if the block time refers to a diverging switch
						if ((y->BlockID == BlockTime[h].BlockID) && (y->ConnectedBlockingTimeID == BlockTime[h].ConnectedBlockingTimeID) && (y->GeoPosStart == BlockTime[h].GeoPosStart)) {
							IsAlreadyInBlockTime = true;
							break;
						}
					} else { // for the block times of all other elements which are not diverging switches
						if ((y->BlockID == BlockTime[h].BlockID) || (y->GeoPosStart == BlockTime[h].GeoPosStart)) {
							IsAlreadyInBlockTime = true;
							break; // break the for loop over the BlockTime of this train
						}
					}
				}
			} else { // if the blockTime is empty then leave the bool IsAlreadyInBlockTime to its false value
			}

			// Only if the element y is not already in the BlockTime List of this train then add it to the FinalLocationList
			if (IsAlreadyInBlockTime == 0) {
				FinalLocationList.push_back(*y);
				N_FinalLocationList++;
			}
		}

		// Set the number of block sections of the train
		for (list<BlockingTimes>::iterator it = FinalLocationList.begin(); it != FinalLocationList.end(); it++) {
			BlockTime[N_BlockSections] = *it; // Setting the BlockTime equal to the BlockTime it in the FinalLocationList

			bool FoundStartRunTime = false;
			bool FoundApprTimeETCS3 = false;
			bool FoundEndRunTime = false;
			bool FoundEndClearTime = false;
			int p = EntryTime + 1;
			for (; p <= End_Time; p++) {
				// Finding the start run time instant
				if (FoundStartRunTime == 0) {
					if (((instant_spatial_position[p - 1] <= BlockTime[N_BlockSections].PosStart) && (instant_spatial_position[p] > BlockTime[N_BlockSections].PosStart)) || ((instant_spatial_position[p - 1] == BlockTime[N_BlockSections].PosStart - 0.0001))) { // The second condition after the || stands to consider the case in which the train enters a platform stop
						BlockTime[N_BlockSections].StartRunTime = p - 1;
						FoundStartRunTime = true;
					}
				}
				// Finding the End Run Time: In  the ETCS level 3 the running time is always 0 so the EndRunTime is always equal to the Start Run Time apart from the sections at platform stops where they are different
				if ((instant_spatial_position[p - 1] <= BlockTime[N_BlockSections].PosEnd) && (instant_spatial_position[p] > BlockTime[N_BlockSections].PosEnd)) {
					BlockTime[N_BlockSections].EndRunTime = p - 1;
				}

				// Finding the end run clear time instance (of course in ETCS 3 PosStart and PosEnd of the section coincides)
				if ((instant_spatial_position[p - 1] - train_length <= BlockTime[N_BlockSections].PosEnd) && (instant_spatial_position[p] - train_length > BlockTime[N_BlockSections].PosEnd)) {
					BlockTime[N_BlockSections].EndClearTime = p - 1;
				}

				if (N_BlockSections > 0) {
					if (FoundApprTimeETCS3 == 0) {																																																																					  // if the startApproach time of ETCS level 3 has not been found yet
						double BrakingDistance = this->BrakingDistanceFastComputation(instant_train_speed[p], 0, instant_spatial_position[p], BlockTime[N_BlockSections].PosStart, run_->infra.routes[this->indexOfRoute].sequence_of_block_sections.data(), run_->infra.routes[this->indexOfRoute].N_Block_Sections) + SafetyMargin; // Computing the braking curve from the speed instant_train_speed[p]
						if (instant_spatial_position[p] + BrakingDistance > BlockTime[N_BlockSections].PosStart) {																																																					  // if at instant p the instant_spatial_position[p]+BrakingDistance overcomes the entry signal of block section signalling_block_sections[i] then the StartApproachTime is instant p-1
							BlockTime[N_BlockSections].StartApproachTime = p - 1;
							FoundApprTimeETCS3 = true; // The StartApproachTime has been found that is why the variable goes to true
						}
					}
				}

				if ((FoundStartRunTime == 1) && (FoundEndRunTime == 1) && (FoundEndClearTime == 1) && (FoundApprTimeETCS3 == 1)) {
					break; // after all the time instants we were looking for have been found then we can break the for loop over the time trajectory of the train
				}
			}

			// Adding RunTimeMargins in case we have one. The RunTime Margin wil be added to the EnRunTIme of the train
			if ((AbsoluteRTSupplement != 0) && (PercentRTSupplement == 0)) {
				BlockTime[N_BlockSections].RunTimeMargin = AbsoluteRTSupplement / (run_->infra.routes[indexOfRoute].N_Block_Sections);
				if (N_BlockSections > 0) { // Add the Running time margin to the locations apart from the initial location
					BlockTime[N_BlockSections].EndRunTime = BlockTime[N_BlockSections].EndRunTime + BlockTime[N_BlockSections].RunTimeMargin;
				}
			} else if ((AbsoluteRTSupplement == 0) && (PercentRTSupplement != 0)) {
				// For ETCS3 this line must be corrected since we consider as running time the running time between this block section and the previous one.
				double RunTimeBetweenConsLocations = 0; // This is the running time between two consecutive locations
				if (N_BlockSections > 0) {
					RunTimeBetweenConsLocations = BlockTime[N_BlockSections].EndRunTime - (BlockTime[N_BlockSections - 1].EndRunTime - BlockTime[N_BlockSections - 1].RunTimeMargin); // The running time is equal to the difference between the End Run Time of this locaton and the previous
				}

				if (RunTimeBetweenConsLocations < 0) { // Throwing an exception in case of negative running time between two consecutive infrastructure elements
					EG_LOG(SimSetup, Warning) << "Warning 987: Train " << this->trainDescription << " has a negative running time between infrastructure element " << BlockTime[0].ConnectedBlockingTimeID << " and the previous infrastructure element on the route. Please check what happened\n";
				}
				// In this way the RunTimeBetweenConsecutive locations will be 0 for the first location and different from 0 for the other locations
				BlockTime[N_BlockSections].RunTimeMargin = ceil(RunTimeBetweenConsLocations * PercentRTSupplement / 100);
				// Shift the EndOccTime of the RunTimeMargin only if the block section is not the first
				BlockTime[N_BlockSections].EndRunTime = BlockTime[N_BlockSections].EndRunTime + BlockTime[N_BlockSections].RunTimeMargin;
			}

			// In some case it can happen an exception when the station is too close to the end of the train journey (i.e. the end of its route) and the train length is longer than the distance between the station and the end of the journey
			// In these cases it is not possible to get a EndClearTime and the corresponding condition if used to determine it does not work. In this case we fix this problem assuming that the EndClearTime coincides with the time at which the train exits the simulation (i.e. End_Time)
			if (BlockTime[N_BlockSections].EndClearTime == -1) {
				BlockTime[N_BlockSections].EndClearTime = this->End_Time + BlockTime[N_BlockSections].RunTimeMargin;
			}

			// BlockTime[N_BlockSections].EndRunTime=BlockTime[N_BlockSections].StartRunTime;
			BlockTime[N_BlockSections].EndApproachTime = BlockTime[N_BlockSections].StartRunTime;
			BlockTime[N_BlockSections].StartClearTime = BlockTime[N_BlockSections].EndRunTime;
			// Setting Sight and Reaction Time
			BlockTime[N_BlockSections].sightReacTime = SightReacTime;
			// Setting setuptime and release times that in ETCS L3 are applied just to switch
			// The setup time will be considered in ETCS Level 3 only if the switch is not in the right position for the train
			if (BlockTime[N_BlockSections].LocationWithSwitch == 1) {
				if (BlockTime[N_BlockSections].InfraElementInPositionForTrain == 0) { // under ETCS L3 setup time should be different from 0 only for switches that are not in the right position for the train and need to be setup
					BlockTime[N_BlockSections].setupTime = SetupTime;
				} else {
					BlockTime[N_BlockSections].setupTime = 0; // if the switch is already in the right position then setuptime is null
				}
				BlockTime[N_BlockSections].ReleaseTime = ReleaseTime; // if the element is a switch there is always a release time to release the track detection section on the switching element
			} else {												  // if the infrastructure element is not a switch, then both setuptime and Release times are 0
				BlockTime[N_BlockSections].setupTime = 0;
				BlockTime[N_BlockSections].ReleaseTime = 0;
			}

			if (N_BlockSections == 0) { // for the first block we do not have setup and sightReaction Time nor approach time
				BlockTime[N_BlockSections].StartApproachTime = BlockTime[N_BlockSections].StartRunTime;
				BlockTime[N_BlockSections].EndApproachTime = BlockTime[N_BlockSections].StartApproachTime;
				BlockTime[N_BlockSections].setupTime = 0;
				BlockTime[N_BlockSections].sightReacTime = 0;
			}

			if ((N_BlockSections == N_FinalLocationList - 1) && (IndexCurrentBS == run_->infra.routes[indexOfRoute].N_Block_Sections)) { // for the last block section we do not have clearing time since the train unblocks the section as soon as its head arrives at the final point of the network
				BlockTime[N_BlockSections].EndClearTime = BlockTime[N_BlockSections].EndRunTime;
				BlockTime[N_BlockSections].StartClearTime = BlockTime[N_BlockSections].EndClearTime;
			}

			// In case the end of the block section has an ending Node whose distance from the exit point of the train is shorter than the length of the train, then the block section will never be cleared, that is why we must put this condition
			if ((BlockTime[N_BlockSections].StartClearTime != -1) && (BlockTime[N_BlockSections].EndClearTime == -1)) {
				if (BlockTime[N_BlockSections].PosEnd >= (instant_spatial_position[End_Time] - train_length)) {	   // this says that if the train head has crossed the end of the block section but its tail cannot because of vicinity problems to the exit point of the train
					BlockTime[N_BlockSections].EndClearTime = End_Time + BlockTime[N_BlockSections].RunTimeMargin; // Then the EndClearTime is equal to the exit time of the train from the network
				}
			}

			// Defining the StartOccupation and EndOccupationTime and all the other occupation times, NOTE: All of these times include the RunTimeMargin if there is any. I need to subtract the RunTimeMargin from each of those to get the times for the minimum running time
			BlockTime[N_BlockSections].ApproachTime = BlockTime[N_BlockSections].EndApproachTime - BlockTime[N_BlockSections].StartApproachTime;
			BlockTime[N_BlockSections].RunTime = BlockTime[N_BlockSections].EndRunTime - BlockTime[N_BlockSections].StartRunTime;
			BlockTime[N_BlockSections].clearingTime = BlockTime[N_BlockSections].EndClearTime - BlockTime[N_BlockSections].StartClearTime;

			BlockTime[N_BlockSections].StartOccTime = BlockTime[N_BlockSections].StartApproachTime - BlockTime[N_BlockSections].setupTime - BlockTime[N_BlockSections].sightReacTime;
			BlockTime[N_BlockSections].EndOccTime = BlockTime[N_BlockSections].EndClearTime + BlockTime[N_BlockSections].ReleaseTime;

			if ((BlockTime[N_BlockSections].StartRunTime < 0) || (BlockTime[N_BlockSections].StartOccTime < 0) || (BlockTime[N_BlockSections].EndOccTime < BlockTime[N_BlockSections].StartOccTime)) {
				BlockTime[N_BlockSections].IsComplete = false;
			}
			if (BlockTime[N_BlockSections].IsComplete == 1)
				N_BlockTimeComplete++; // Increase the number of blocking times that are complete

			N_BlockSections++; // Increase the BlockTime of the train
		}
	}
}

void Train::ComputeBlockTime_ETCSLevel4_ForSection_MaxCapacity_Improved(int IndexCurrentBS, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsoluteRTSupplement, double PercentRTSupplement) {

	list<BlockingTimes> LocationList, FinalLocationList; // LocationList is a temporary List where we collect all the ETCS Level 3 locations belonging to the current Block Section
	int N_LocationList = 0, N_FinalLocationList = 0;	 // Final LocationList instead collects all those locations of LocationList that are not present in the BlockTime of this train (that is why those locations that actually need to be added)

	if (run_->infra.routes[this->indexOfRoute].InfrastructureElements.size() > 0) {

		for (list<InfraElement>::iterator k = run_->infra.routes[this->indexOfRoute].InfrastructureElements.begin(); k != run_->infra.routes[this->indexOfRoute].InfrastructureElements.end(); k++) {
			BlockingTimes TEMPLoc; // defining temporary Blocking Time variable
								   // Identifying the InfraElement on the route after K
			list<InfraElement>::iterator LastInfraElementOnRoute = run_->infra.routes[this->indexOfRoute].InfrastructureElements.end();
			LastInfraElementOnRoute--; // this points at the last infraelement on the route
			string SuccessorInfraElement = "None";
			if (k == LastInfraElementOnRoute) { // if k is the last element of the route then its successor is None
				SuccessorInfraElement = "None";
			} else { // Otherwise it is the element after k
				list<InfraElement>::iterator successor = k;
				successor++; // this iterator points at the infrastructure element after k
				SuccessorInfraElement = successor->ID;
				// In case the InfraElement after k is a VirtualTDSB then take the element after successor as SuccessorInfraElement
				if ((successor->ID == "VirtualTDSB") && (successor != LastInfraElementOnRoute)) {
					list<InfraElement>::iterator secondsuccessor = successor;
					secondsuccessor++; // this element points at the infraElement on the route coming after successor
					SuccessorInfraElement = secondsuccessor->ID;
				}
				// if successor is a VirtualTDSB and it is the last infra element of the route also, then SuccessorInfraElement is "None"
				else if ((successor->ID == "VirtualTDSB") && (successor == LastInfraElementOnRoute)) {
					SuccessorInfraElement = "None";
				}
			}

			if ((k->SectionID == run_->infra.routes[this->indexOfRoute].sequence_of_block_sections[IndexCurrentBS].ID) && (k->ID != "VirtualTDSB")) { // if the Infrastructure Element belongs to the block section and it is not a VirtualTDSB
				TEMPLoc.BlockID = k->ID;
				TEMPLoc.NextBlockID = SuccessorInfraElement;
				TEMPLoc.length = 0;
				TEMPLoc.trainDescription = this->trainDescription;
				TEMPLoc.SignallingLevel = run_->infra.routes[this->indexOfRoute].sequence_of_block_sections[IndexCurrentBS].SignallingLevel; // setting the signalling level of the infrastructure element we are computing the blocking time for
				TEMPLoc.PosStart = k->XCoordinate;
				TEMPLoc.PosEnd = TEMPLoc.PosStart;
				TEMPLoc.GeoPosStart = k->GeoXCoord;
				TEMPLoc.GeoPosEnd = TEMPLoc.GeoPosStart;

				// Is the location a station or a switch?
				if (k->stationName.empty() != 1) {
					TEMPLoc.stationName = k->stationName;
				}
				if (k->IsSwitch == 1) {
					TEMPLoc.LocationWithSwitch = true;
					TEMPLoc.SwitchName = k->SwitchName;
					if (k->ConnectedPoint != "None") {														   // if this is a diverging switch then it is connected to another point
						TEMPLoc.ConnectedBlockingTimeID = k->ConnectedPoint;								   // Connected BlockingTimeID has the ID of the connected point of the switch
						TEMPLoc.PosConnectedBlockingTime = k->XConnectedPoint;								   // Position of the connected point
						TEMPLoc.IsEndOfDivSwitchBeingStartOfADivSwitch = k->isEndOfDivSwitchStartOfADivSwitch; // Is the end of the switch also the start of another diverging switch?
					}
				}

				// Identifying the last status of the infrastructure element we are computing the blocking time for and the speed of the previous passing train
				// TEMPLoc.DetermineLastStatusOfInfraElement(this->trainDescription, InfraElementsList);
				TEMPLoc.checkIfLastInfraElementStatusIsInLineWithTrainNeeds(this->trainDescription, run_->infra.infraEvents);

				// Increase the locations of 1 element if it is not already in the list
				if (N_LocationList == 0) {
					N_LocationList++;
					LocationList.push_back(TEMPLoc);
				} else {
					bool IsAlreadyThere = false;
					for (list<BlockingTimes>::iterator l = LocationList.begin(); l != LocationList.end(); l++) {
						if ((TEMPLoc.BlockID == l->BlockID) || (TEMPLoc.GeoPosStart == l->GeoPosStart)) { // if they have the same ID or the same position
							IsAlreadyThere = true;
							break;
						}
					}
					if (IsAlreadyThere == 0) {
						N_LocationList++;
						LocationList.push_back(TEMPLoc);
					}
				}
			}
		}

		// Defining the FinalLocationList
		for (list<BlockingTimes>::iterator y = LocationList.begin(); y != LocationList.end(); y++) {
			bool IsAlreadyInBlockTime = false;
			if (this->N_BlockSections > 0) {
				for (int h = 0; h < N_BlockSections; h++) {
					if ((y->LocationWithSwitch == 1) && (y->ConnectedBlockingTimeID != "None")) { // if the block time refers to a diverging switch
						if ((y->BlockID == BlockTime[h].BlockID) && (y->ConnectedBlockingTimeID == BlockTime[h].ConnectedBlockingTimeID) && (y->GeoPosStart == BlockTime[h].GeoPosStart)) {
							IsAlreadyInBlockTime = true;
							break;
						}
					} else { // for the block times of all other elements which are not diverging switches
						if ((y->BlockID == BlockTime[h].BlockID) || (y->GeoPosStart == BlockTime[h].GeoPosStart)) {
							IsAlreadyInBlockTime = true;
							break; // break the for loop over the BlockTime of this train
						}
					}
				}
			} else { // if the blockTime is empty then leave the bool IsAlreadyInBlockTime to its false value
			}

			// Only if the element y is not already in the BlockTime List of this train then add it to the FinalLocationList
			if (IsAlreadyInBlockTime == 0) {
				FinalLocationList.push_back(*y);
				N_FinalLocationList++;
			}
		}

		// Set the number of block sections of the train
		for (list<BlockingTimes>::iterator it = FinalLocationList.begin(); it != FinalLocationList.end(); it++) {
			BlockTime[N_BlockSections] = *it; // Setting the BlockTime equal to the BlockTime it in the FinalLocationList

			bool FoundStartRunTime = false;
			bool FoundApprTimeETCS3 = false; // In ETCS Level 4 this one is not anymore an approaching time, but a coordination time (time needed by the train to run over the relative braking distance) + the approaching time (time to run over the safety margin s0)
			bool FoundEndRunTime = false;
			bool FoundEndClearTime = false;

			// Find the speed of the current train at the safety margin before the Pos Start of the infra element we are computing the blocking time for
			double V_At_SafetyMargin_From_Location = 0;
			double S_At_SafetyMargin_FromLocation = 0;
			if (N_BlockSections > 0) { // the speed of the train at the safety margin before the infra element is needed only to compute the approaching and coordination times for ETCS Level 4 in case this one it is a following train
									   // for the first block section the start approaching time coincides with the start run time
				for (int t = EntryTime + 1; t < End_Time; t++) {
					if (instant_spatial_position[t] + SafetyMargin > BlockTime[N_BlockSections].PosStart) {
						V_At_SafetyMargin_From_Location = instant_train_speed[t - 1];
						S_At_SafetyMargin_FromLocation = instant_spatial_position[t - 1];
						break; // then break the for loop over time
					}
				}
			}

			int p = EntryTime + 1;
			for (; p <= End_Time; p++) {
				// Finding the start run time instant
				if (FoundStartRunTime == 0) {
					if (((instant_spatial_position[p - 1] <= BlockTime[N_BlockSections].PosStart) && (instant_spatial_position[p] > BlockTime[N_BlockSections].PosStart)) || ((instant_spatial_position[p - 1] == BlockTime[N_BlockSections].PosStart - 0.0001))) { // The second condition after the || stands to consider the case in which the train enters a platform stop
						BlockTime[N_BlockSections].StartRunTime = p - 1;
						FoundStartRunTime = true;
					}
				}
				// Finding the End Run Time: In  the ETCS level 3 the running time is always 0 so the EndRunTime is always equal to the Start Run Time apart from the sections at platform stops where they are different
				if ((instant_spatial_position[p - 1] <= BlockTime[N_BlockSections].PosEnd) && (instant_spatial_position[p] > BlockTime[N_BlockSections].PosEnd)) {
					BlockTime[N_BlockSections].EndRunTime = p - 1;
				}

				// Finding the end run clear time instance (of course in ETCS 3 PosStart and PosEnd of the section coincides)
				if ((instant_spatial_position[p - 1] - train_length <= BlockTime[N_BlockSections].PosEnd) && (instant_spatial_position[p] - train_length > BlockTime[N_BlockSections].PosEnd)) {
					BlockTime[N_BlockSections].EndClearTime = p - 1;
				}

				// IF TRAINS ARE ALL THE SAME THEN THE CONDITION ABOVE TO FIND THE END OF CLEARING TIME FOR VIRTUAL COUPLING IT IS STILL VALID.
				// FOR MIXED TRAFFIC WITH DIFFERENT TRAIN CATEGORIES AND FOR A BLOCKING TIME COMPUTATION THAT DEPARTS FROM FREE FLOW TRAIN TRAJECTORIES THE ENDCLEAR TIME FOR VIRTUAL COUPLING SHALL BE COMPUTED ACCORDING TO THE FOLLOWING CONDITION
				// IF A TRAIN-FOLLOWING MODEL IS IMPLEMENTED INSTEAD THE ABOVE CONDITION IS VALID.
				// SO ACTIVATE THE FOLLOWING CODE ONLY IF YOU ARE COMPUTING BLOCKING TIMES FOR VIRTUAL COUPLING STARTING FROM THE FREE FLOW SPEED PROFILES AND THERE ARE DIFFERENT TRAIN RUNNING ON THE NETWORK
				/*if (BlockTime[N_BlockSections].NamePreviousTrain == "None") {
				if ((instant_spatial_position[p - 1] - train_length <= BlockTime[N_BlockSections].PosEnd) && (instant_spatial_position[p] - train_length > BlockTime[N_BlockSections].PosEnd)) {
				BlockTime[N_BlockSections].EndClearTime = p - 1;
				}
				}

				else {
				int ComputedClearTimeInFollowingMode = 0;
				ComputedClearTimeInFollowingMode = ceil(train_length / BlockTime[N_BlockSections].SpeedPreviousTrain);
				BlockTime[N_BlockSections].EndClearTime = BlockTime[N_BlockSections].EndRunTime + ComputedClearTimeInFollowingMode;
				}
				*/

				if (N_BlockSections > 0) {
					if (FoundApprTimeETCS3 == 0) { // if the startApproach time of ETCS level 3 has not been found yet
												   // If I want the Absolute braking distance to be active only when the infra element is a diverging switch and not just a switch then I need to add the condition || ((BlockTime[N_BlockSections].LocationWithSwitch==1) && BlockTime[N_BlockSections].ConnectedBlockingTimeID!="None")
						double ApproachingDistance = 0;
						bool IsInFollowingMode = false; // This variable turn to true only if the train is in following mode, i.e. it can accelerate and the train ahead is faster
						double ApproachingTimeInFollowingMode = 0;
						// The if condition below means that an absolute braking distance is used if teh trains is the first train of a platoon or at junctions where the route diverges or converge with the route of the train ahead.
						// This means that at switches that the train and the train ahead take in the same position a relative braking distance is used to compute the approaching time
						if ((BlockTime[N_BlockSections].NamePreviousTrain == "None") || ((BlockTime[N_BlockSections].LocationWithSwitch == 1) && (BlockTime[N_BlockSections].InfraElementInPositionForTrain == 0))) {																												   // if the train is the first train to run over the section, that means that it is the leader and will travel under ETCS Level 3 with an absolute braking distance. It will also travel with an absolute braking distance over switches that are not in the right position for the train, because the previous train was using a different route.
							ApproachingDistance = this->BrakingDistanceFastComputation(instant_train_speed[p], 0, instant_spatial_position[p], BlockTime[N_BlockSections].PosStart, run_->infra.routes[this->indexOfRoute].sequence_of_block_sections.data(), run_->infra.routes[this->indexOfRoute].N_Block_Sections) + SafetyMargin; // Computing the braking curve from the speed instant_train_speed[p]
						}

						else { // if the train has instead other trains in front of it, it is a follower and will need to coordinate its speed with the one of the train leader
							if (V_At_SafetyMargin_From_Location < (BlockTime[N_BlockSections].SpeedPreviousTrain - 0.278)) {
								// The train will accelerate to coordinate its speed with the train ahead only if also the next location is on the route shared by the two trains
								if (BlockTime[N_BlockSections].NextBlockID == BlockTime[N_BlockSections].NextBlockIDPreviousTrain) {
									double SpeedCoordinationTime = 0;
									IsInFollowingMode = true; // The train is in following mode
									// Compute the time to accelerate the train to the speed of the train ahead
									SpeedCoordinationTime = this->AccelerationTimeFollowingMode(V_At_SafetyMargin_From_Location, BlockTime[N_BlockSections].SpeedPreviousTrain, S_At_SafetyMargin_FromLocation, BlockTime[N_BlockSections].PosStart, run_->infra.routes[this->indexOfRoute].sequence_of_block_sections.data(), run_->infra.routes[this->indexOfRoute].N_Block_Sections);
									// if the speedcoordination time is different from -99999 that means that is a positive value, as the function AccelerationTimeFollowingMode has been designed
									// In this case the Approaching time is given by the time needed to accelerate + the time to cross the safety margin at the speed of the previous train
									if (SpeedCoordinationTime != -99999) {
										ApproachingTimeInFollowingMode = SpeedCoordinationTime + SafetyMargin / BlockTime[N_BlockSections].SpeedPreviousTrain;
									}
									// if the time returned by the Function AccelerationTimeFollowingMode is -99999 then it means that the train cannot accelerate and need to stay in cruising mode
									else { // in this case the Approaching time is just the time for the train to cross the safety margin at the speed it has
										ApproachingTimeInFollowingMode = -1;
										ApproachingDistance = SafetyMargin;
									}
								} else { // In case this train is going slower than the train ahead and the train ahead will take another route at the next location, it does not make sense for this train to catch up with the train ahead and the train can stay at its current speed (cruising mode). In this case the approaching distance is simply the safety margin
									ApproachingDistance = SafetyMargin;
								}
							} else if (V_At_SafetyMargin_From_Location > (BlockTime[N_BlockSections].SpeedPreviousTrain + 0.278)) {
								ApproachingDistance = this->BrakingDistanceFastComputation(V_At_SafetyMargin_From_Location, BlockTime[N_BlockSections].SpeedPreviousTrain, S_At_SafetyMargin_FromLocation, BlockTime[N_BlockSections].PosStart, run_->infra.routes[this->indexOfRoute].sequence_of_block_sections.data(), run_->infra.routes[this->indexOfRoute].N_Block_Sections) + SafetyMargin;
							} else if ((V_At_SafetyMargin_From_Location >= BlockTime[N_BlockSections].SpeedPreviousTrain - 0.278) && (V_At_SafetyMargin_From_Location <= BlockTime[N_BlockSections].SpeedPreviousTrain + 0.278)) {
								ApproachingDistance = SafetyMargin;
								// In this case Sight and Reaction time must be 0 since the train does not need ot react (i.e. braking or accelerrating) to the preceding train since they are at the same speed
								SightReacTime = 0;
							}
						}

						if (IsInFollowingMode == 0) {
							if (instant_spatial_position[p] + ApproachingDistance > BlockTime[N_BlockSections].PosStart) { // if at instant p the instant_spatial_position[p]+BrakingDistance overcomes the entry signal of block section signalling_block_sections[i] then the StartApproachTime is instant p-1
								BlockTime[N_BlockSections].StartApproachTime = p - 1;
								FoundApprTimeETCS3 = true; // The StartApproachTime has been found that is why the variable goes to true
							}
						}
						// In case the train is in following mode then the approaching time is computed in a different way, that is :
						else {
							// after that the startRunTime has been found, the startApproachingTime is obtained by that the StartRunTime - ApproachingtimeInFollowingMode
							if (FoundStartRunTime == 1) {
								BlockTime[N_BlockSections].StartApproachTime = floor(BlockTime[N_BlockSections].StartRunTime - ApproachingTimeInFollowingMode);
								if (BlockTime[N_BlockSections].StartApproachTime < 0) {
									BlockTime[N_BlockSections].StartApproachTime = 0;
								}
								FoundApprTimeETCS3 = true;
							}
						}
					}
				}

				if ((FoundStartRunTime == 1) && (FoundEndRunTime == 1) && (FoundEndClearTime == 1) && (FoundApprTimeETCS3 == 1)) {
					break; // after all the time instants we were looking for have been found then we can break the for loop over the time trajectory of the train
				}
			}

			// Adding RunTimeMargins in case we have one. The RunTime Margin wil be added to the EnRunTIme of the train
			if ((AbsoluteRTSupplement != 0) && (PercentRTSupplement == 0)) {
				BlockTime[N_BlockSections].RunTimeMargin = AbsoluteRTSupplement / (run_->infra.routes[indexOfRoute].N_Block_Sections);
				if (N_BlockSections > 0) { // Add the Running time margin to the locations apart from the initial location
					BlockTime[N_BlockSections].EndRunTime = BlockTime[N_BlockSections].EndRunTime + BlockTime[N_BlockSections].RunTimeMargin;
				}
			} else if ((AbsoluteRTSupplement == 0) && (PercentRTSupplement != 0)) {
				// For ETCS3 this line must be corrected since we consider as running time the running time between this block section and the previous one.
				double RunTimeBetweenConsLocations = 0; // This is the running time between two consecutive locations
				if (N_BlockSections > 0) {
					RunTimeBetweenConsLocations = BlockTime[N_BlockSections].EndRunTime - (BlockTime[N_BlockSections - 1].EndRunTime - BlockTime[N_BlockSections - 1].RunTimeMargin); // The running time is equal to the difference between the End Run Time of this locaton and the previous
				}

				if (RunTimeBetweenConsLocations < 0) { // Throwing an exception in case of negative running time between two consecutive infrastructure elements
					EG_LOG(SimSetup, Warning) << "Warning: Train " << this->trainDescription << " has a negative running time between infrastructure element " << BlockTime[0].ConnectedBlockingTimeID << " and the previous infrastructure element on the route. Please check what happened\n";
				}
				// In this way the RunTimeBetweenConsecutive locations will be 0 for the first location and different from 0 for the other locations
				BlockTime[N_BlockSections].RunTimeMargin = ceil(RunTimeBetweenConsLocations * PercentRTSupplement / 100);
				// Shift the EndOccTime of the RunTimeMargin only if the block section is not the first
				BlockTime[N_BlockSections].EndRunTime = BlockTime[N_BlockSections].EndRunTime + BlockTime[N_BlockSections].RunTimeMargin;
			}

			else {
				EG_LOG(SimSetup, Warning) << "\n\nWARNING 5: Percentage and absolute Running Time Supplements are both equal to 0 or both different from 0\n\n";
			}

			// In some case it can happen an exception when the station is too close to the end of the train journey (i.e. the end of its route) and the train length is longer than the distance between the station and the end of the journey
			// In these cases it is not possible to get a EndClearTime and the corresponding condition if used to determine it does not work. In this case we fix this problem assuming that the EndClearTime coincides with the time at which the train exits the simulation (i.e. End_Time)
			if (BlockTime[N_BlockSections].EndClearTime == -1) {
				BlockTime[N_BlockSections].EndClearTime = this->End_Time + BlockTime[N_BlockSections].RunTimeMargin;
			}

			// BlockTime[N_BlockSections].EndRunTime=BlockTime[N_BlockSections].StartRunTime;
			BlockTime[N_BlockSections].EndApproachTime = BlockTime[N_BlockSections].StartRunTime;
			BlockTime[N_BlockSections].StartClearTime = BlockTime[N_BlockSections].EndRunTime;
			// Setting Sight and Reaction Time
			BlockTime[N_BlockSections].sightReacTime = SightReacTime;
			// Setting setuptime and release times that in ETCS L3 are applied just to switch
			// The setup time will be considered in ETCS Level 3 only if the switch is not in the right position for the train
			if (BlockTime[N_BlockSections].LocationWithSwitch == 1) {
				if (BlockTime[N_BlockSections].InfraElementInPositionForTrain == 0) { // under ETCS L3 setup time should be different from 0 only for switches that are not in the right position for the train and need to be setup
					BlockTime[N_BlockSections].setupTime = SetupTime;
				} else {
					BlockTime[N_BlockSections].setupTime = 0; // if the switch is already in the right position then setuptime is null
				}
				BlockTime[N_BlockSections].ReleaseTime = ReleaseTime; // if the element is a switch there is always a release time to release the track detection section on the switching element
			} else {												  // if the infrastructure element is not a switch, then both setuptime and Release times are 0
				BlockTime[N_BlockSections].setupTime = 0;
				BlockTime[N_BlockSections].ReleaseTime = 0;
			}

			if (N_BlockSections == 0) { // for the first block we do not have setup and sightReaction Time nor approach time
				BlockTime[N_BlockSections].StartApproachTime = BlockTime[N_BlockSections].StartRunTime;
				BlockTime[N_BlockSections].EndApproachTime = BlockTime[N_BlockSections].StartApproachTime;
				BlockTime[N_BlockSections].setupTime = 0;
				BlockTime[N_BlockSections].sightReacTime = 0;
			}

			if ((N_BlockSections == N_FinalLocationList - 1) && (IndexCurrentBS == run_->infra.routes[indexOfRoute].N_Block_Sections)) { // for the last block section we do not have clearing time since the train unblocks the section as soon as its head arrives at the final point of the network
				BlockTime[N_BlockSections].EndClearTime = BlockTime[N_BlockSections].EndRunTime;
				BlockTime[N_BlockSections].StartClearTime = BlockTime[N_BlockSections].EndClearTime;
			}

			// In case the end of the block section has an ending Node whose distance from the exit point of the train is shorter than the length of the train, then the block section will never be cleared, that is why we must put this condition
			if ((BlockTime[N_BlockSections].StartClearTime != -1) && (BlockTime[N_BlockSections].EndClearTime == -1)) {
				if (BlockTime[N_BlockSections].PosEnd >= (instant_spatial_position[End_Time] - train_length)) {	   // this says that if the train head has crossed the end of the block section but its tail cannot because of vicinity problems to the exit point of the train
					BlockTime[N_BlockSections].EndClearTime = End_Time + BlockTime[N_BlockSections].RunTimeMargin; // Then the EndClearTime is equal to the exit time of the train from the network
				}
			}

			// Defining the StartOccupation and EndOccupationTime and all the other occupation times, NOTE: All of these times include the RunTimeMargin if there is any. I need to subtract the RunTimeMargin from each of those to get the times for the minimum running time
			BlockTime[N_BlockSections].ApproachTime = BlockTime[N_BlockSections].EndApproachTime - BlockTime[N_BlockSections].StartApproachTime;
			BlockTime[N_BlockSections].RunTime = BlockTime[N_BlockSections].EndRunTime - BlockTime[N_BlockSections].StartRunTime;
			BlockTime[N_BlockSections].clearingTime = BlockTime[N_BlockSections].EndClearTime - BlockTime[N_BlockSections].StartClearTime;

			BlockTime[N_BlockSections].StartOccTime = BlockTime[N_BlockSections].StartApproachTime - BlockTime[N_BlockSections].setupTime - BlockTime[N_BlockSections].sightReacTime;
			BlockTime[N_BlockSections].EndOccTime = BlockTime[N_BlockSections].EndClearTime + BlockTime[N_BlockSections].ReleaseTime;

			if ((BlockTime[N_BlockSections].StartRunTime < 0) || (BlockTime[N_BlockSections].StartOccTime < 0) || (BlockTime[N_BlockSections].EndOccTime < BlockTime[N_BlockSections].StartOccTime)) {
				BlockTime[N_BlockSections].IsComplete = false;
			}
			if (BlockTime[N_BlockSections].IsComplete == 1)
				N_BlockTimeComplete++; // Increase the number of blocking times that are complete

			N_BlockSections++; // Increase the BlockTime of the train
		}
	}
}

void Train::ConnectBlockTimesOfSwitchesForTrainCentricSignalling() {
	if (this->N_BlockTimeComplete > 0) {
		for (int i = 0; i < this->N_BlockTimeComplete; i++) {
			if ((BlockTime[i].SignallingLevel >= levelValue(SignallingLevel::EtcsLevel3)) && (BlockTime[i].IsComplete == 1) && (BlockTime[i].LocationWithSwitch == 1) && (BlockTime[i].ConnectedBlockingTimeID != "None")) { // if the blocktime has a signalling level higher then 3, it is complete, it is a switch and it has a connected point (so it is a diverging switch)
				bool ConnectedBlockingTimeFound = false;

				if (BlockTime[i].IsAlreadyUniformedToConnectedSwitch == 0) { // if this blocking time has not already been uniformed to the one of the connected diverging switch edge
					for (int j = 0; j < this->N_BlockTimeComplete; j++) {

						// If there is a diverging switch having the same SwitchName and position but different ID, tthis means that this is a switch linked to two other switches and it has for this reason a left side that is a Point-End connected to a switch and the right sight that is a Point-Start connected to a different switch (the situation described in the figure here at the right) o-----oo----o
						// Point-Start-----Point-End Point-Start-----Point-End
						if ((BlockTime[j].SignallingLevel >= levelValue(SignallingLevel::EtcsLevel3)) && (BlockTime[j].SwitchName == BlockTime[i].SwitchName) && (BlockTime[j].GeoPosStart = BlockTime[i].GeoPosStart) && (BlockTime[j].BlockID != BlockTime[i].BlockID)) {
							BlockTime[i].StartOccTime = BlockTime[j].StartOccTime;
							BlockTime[i].EndOccTime = BlockTime[j].EndOccTime;
							BlockTime[i].StartApproachTime = BlockTime[j].StartApproachTime;
							BlockTime[i].EndApproachTime = BlockTime[j].EndApproachTime;
							BlockTime[i].StartRunTime = BlockTime[j].StartRunTime;
							BlockTime[i].EndRunTime = BlockTime[j].EndRunTime;
							BlockTime[i].StartClearTime = BlockTime[j].StartClearTime;
							BlockTime[i].EndClearTime = BlockTime[j].EndClearTime;
							BlockTime[i].sightReacTime = BlockTime[j].sightReacTime;
							BlockTime[i].ReleaseTime = BlockTime[j].ReleaseTime;
							BlockTime[i].setupTime = BlockTime[j].setupTime;
							BlockTime[i].RunTimeMargin = BlockTime[j].RunTimeMargin;
							// setting the time lags for each of the blocking time phases of BlockTime [i]
							BlockTime[i].RunTime = BlockTime[i].EndRunTime - BlockTime[i].StartRunTime;
							BlockTime[i].ApproachTime = BlockTime[i].EndApproachTime - BlockTime[i].StartApproachTime;
							BlockTime[i].clearingTime = BlockTime[i].EndClearTime - BlockTime[i].StartClearTime;
						}

						// HEre we look for the connected blocking time of the connected switch edge
						if ((BlockTime[j].SignallingLevel >= levelValue(SignallingLevel::EtcsLevel3)) && (BlockTime[j].IsComplete == 1) && (BlockTime[j].LocationWithSwitch == 1) && (BlockTime[j].BlockID == BlockTime[i].ConnectedBlockingTimeID)) { // if the blocking time has a signalling level higher than 3, it is complete, it is a switch and has the switch name that is the same of the connected switch for BlockTime[i]
							// The first part of the blocking time that goes from start occupation to startRunTime needs to be transferred from BlockTime[i] to BlockTime[j]
							BlockTime[j].StartOccTime = BlockTime[i].StartOccTime;
							BlockTime[j].setupTime = BlockTime[i].setupTime;
							BlockTime[j].sightReacTime = BlockTime[i].sightReacTime;
							BlockTime[j].StartApproachTime = BlockTime[i].StartApproachTime;
							BlockTime[j].EndApproachTime = BlockTime[i].EndApproachTime;
							BlockTime[j].StartRunTime = BlockTime[i].StartRunTime;
							// The second part of the blocking time that goes from the EndRunTime to EndOccTime needs to be transferred from BlockTime[j] to BlockTime[i]
							BlockTime[i].EndRunTime = BlockTime[j].EndRunTime;
							BlockTime[i].StartClearTime = BlockTime[j].StartClearTime;
							BlockTime[i].EndClearTime = BlockTime[j].EndClearTime;
							BlockTime[i].ReleaseTime = BlockTime[j].ReleaseTime;
							BlockTime[i].EndOccTime = BlockTime[j].EndOccTime;

							// Setting the time lags for the two blocking times BlockTIme[i]
							BlockTime[i].RunTime = BlockTime[i].EndRunTime - BlockTime[i].StartRunTime;
							BlockTime[i].ApproachTime = BlockTime[i].EndApproachTime - BlockTime[i].StartApproachTime;
							BlockTime[i].clearingTime = BlockTime[i].EndClearTime - BlockTime[i].StartClearTime;
							BlockTime[i].RunTimeMargin = BlockTime[j].RunTimeMargin;
							// and for BlockTime[j]
							BlockTime[j].RunTime = BlockTime[j].EndRunTime - BlockTime[j].StartRunTime;
							BlockTime[j].ApproachTime = BlockTime[j].EndApproachTime - BlockTime[j].StartApproachTime;
							BlockTime[j].clearingTime = BlockTime[j].EndClearTime - BlockTime[j].StartClearTime;

							// Setting now the variable IsAlreadyUniformed to connected block to true so that the external for loop on i will not remodify the blocking time j which has been instead been modified now together with blocking time i. Putting the variable IsAlreadyUniformedToConnectedBlockTime to true avoid that the same blocking time is modified two time always with respect to the same connected blocking time
							BlockTime[i].IsAlreadyUniformedToConnectedSwitch = true;
							BlockTime[j].IsAlreadyUniformedToConnectedSwitch = true;
							// Set to true the variable ConnectedBlockingTimeFound since it has been found indeed and we can therefore break the for loop over j
							ConnectedBlockingTimeFound = true;
						}
						if (ConnectedBlockingTimeFound == 1) {
							break; // break the for loop over j when the connected blocking time has been found
						}
					}
				}
			}
		}
	}
}

void Train::ComputeBlockingTimesInMixedSignallingAreas(double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsRTSupp, double PercRTSupp) {
	// Compute the time the train Enters the network in the simulation: EntryTime
	int EntryTime = (int)Det_Run_Start_Time();

	if (run_->infra.routes[indexOfRoute].N_Block_Sections > 0) { // if the route is composed of at least one block section
		for (int i = 0; i < run_->infra.routes[indexOfRoute].N_Block_Sections; i++) {
			const int level = run_->infra.routes[indexOfRoute].sequence_of_block_sections[i].SignallingLevel;
			if (level == levelValue(SignallingLevel::Atb)) { // if block sections have Signalling level 0 then we must apply the conventional signalling rules
				this->ComputeBlockingTimeForSingleLocation(i, "Conventional", EntryTime, SetupTime, ReleaseTime, SightReacTime, AbsRTSupp, PercRTSupp);
			} else if (level == levelValue(SignallingLevel::EtcsLevel2)) { // if Signalling level is 2 apply ETCS Level 2 rules
				this->ComputeBlockingTimeForSingleLocation(i, "ETCS2", EntryTime, SetupTime, ReleaseTime, SightReacTime, AbsRTSupp, PercRTSupp);
			} else if (level == levelValue(SignallingLevel::EtcsLevel3)) { // if Signalling Level is 3 Apply ETCS Level 3 rules
				// ComputeBlockTimeETCS3ForSingleLocation(i, EntryTime, SetupTime, ReleaseTime, SightReacTime, AbsRTSupp, PercRTSupp);
				// the function below now replaces the function ComputeBlockingTimeETCS3ForSingleLocation
				this->ComputeBlockTime_ETCSLevel_3_ForSection(i, EntryTime, SetupTime, ReleaseTime, SightReacTime, SafetyMargin, AbsRTSupp, PercRTSupp);
			} else if (level == levelValue(SignallingLevel::VirtualCoupling)) {
				this->ComputeBlockTime_ETCSLevel4_ForSection_MaxCapacity_Improved(i, EntryTime, SetupTime, ReleaseTime, SightReacTime, SafetyMargin, AbsRTSupp, PercRTSupp);
			}
		}
		// Connecting blocking times of edges of switches in area where ETCS Level 3 or higher levels of signalling (e.g. ETCS Level 4- Virtual Coupling) are installed
		this->ConnectBlockTimesOfSwitchesForTrainCentricSignalling();
	}
}
