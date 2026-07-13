#ifndef Rolling_Stock_hpp
#define Rolling_Stock_hpp
#include "simulation/Signalling.h"
#include "util/TrajectoryUtil.h"

// GUI - Virtual Coupling notifications
#include <vector>
#include <QList>
#include <QGraphicsItemGroup>
extern vector<int> VCmsgTimestep;
extern vector<string> VCmsgTrain;
extern vector<string> VCmsgText;
extern QList<QGraphicsItemGroup*> VCmsgItems;

// -------------


// dispatching decisions
#include <cmath> // std::fabs
#include "simulation/DispatchDecision.h"

extern int N_OrderLists; // This is the number of OrderLists that have to be respected at critical nodes
extern InitialParameters initial_variables;

class OrderList {
public:
	string ID;
	string BlockID;								   // This is the ID of the Block Section where the ordere need to be implemented
	double Node_X;								   // This is the abscissa of the Node where the departure order has to be respected
	bool Is_MergingJunction, Is_DivergingJunction; // These 2 variables indicate whether the OrderList is a Diverging or a Merging Junction. By default they are set to 0;
	TrainEvent TE[300];
	int numTeList;
	string LastEnteredTrain; // This is the trainDescription of the Last train that has departed from the Node Node_X
	list<string> Implemented_Order;
	OrderList();

	// Function to Load the features of a single OrderList object from an input file
	void Load_OrderList(char* OrderFile) {
		string* Matr;
		int N_List = 0;
		Matr = new string[1000];
		ifstream inputfile;
		inputfile.open(OrderFile, ios::binary);
		if (!inputfile) {
			cout << "\nError 72 : impossible to open OrderList file\n";
		} else
			cout << "Loading Order List files ...\n";

		while (inputfile) {
			inputfile >> Matr[N_List];
			std::cout << "\rLoading Order List file : " << N_List;
			N_List++;
		}
		std::cout << "\n";
		N_List = (N_List - 1);
		numTeList = N_List - 2;
		inputfile.close(); // Closing OrderList Data File

		// find this
		Node_X = atof(Matr[0].c_str());
		BlockID = Matr[1];
		ID = ID + Matr[1] + "_" + Matr[0];
		for (int i = 2; i < N_List; i++) {
			TE[i - 2].trainDescription = Matr[i];
		}
		// Assigning the successor for each TrainElement in the OrderedList (i.e. taking the id of the train that is the successor in the list)
		for (int i = numTeList - 1; i >= 0; i--) {
			TE[i].SuccessorID = TE[i + 1].trainDescription;
		}
		// Deleting the Matrix
		delete[] Matr;
	}

	// Upgraded Function to Load the features of a single OrderList object from an input file
	// This upgraded function is set to distinguish whether the Order List refers to a merging or a diverging junction where train reordering can occur
	// In this version of the function there is no initalisation of the BlockID of the OrderedList as that is an old version.
	// Now the OrderedList is updated with the bool Is_DivergingJunction or Is_MergingJunction. So in the initialising text files the second row should indicate whether the Ordered List
	// Refers to a "MergingJunction" or to a "DivergingJunction"

	void Load_OrderList_Upgraded(char* OrderFile) {
		string* Matr;
		int N_List = 0;
		Matr = new string[1000];
		ifstream inputfile;
		inputfile.open(OrderFile, ios::binary);
		if (!inputfile) {
			cout << "\nError 72 : impossible to open OrderList file\n";
		} else
			cout << "Loading Order List files ...\n";

		while (inputfile) {
			inputfile >> Matr[N_List];
			std::cout << "\rLoading Order List file : " << N_List;
			N_List++;
		}
		std::cout << "\n";
		N_List = (N_List - 1);
		numTeList = N_List - 2;
		inputfile.close(); // Closing OrderList Data File

		// find this
		Node_X = atof(Matr[0].c_str());
		string JunctionType;
		JunctionType = Matr[1];
		if (JunctionType == "MergingJunction") {
			Is_MergingJunction = true;
		} else if (JunctionType == "DivergingJunction") {
			Is_DivergingJunction = 1;
		}

		else {
			cout << "\n"
				 << "Error 74: OL text file to initialise Order List referring to location abscissa " << Matr[0] << " does not correctly specify whether it is a Merging or Diverging Junction. The text on the second line of the text file should be either 'MergingJunction' or 'DivergingJunction' " << "\n";
			cout << "Any other expression raises an incorrect OL specification " << "\n";
		}

		ID = ID + JunctionType + "_" + Matr[0];
		for (int i = 2; i < N_List; i++) {
			TE[i - 2].trainDescription = Matr[i];
		}
		// Assigning the successor for each TrainElement in the OrderedList (i.e. taking the id of the train that is the successor in the list)
		for (int i = numTeList - 1; i >= 0; i--) {
			TE[i].SuccessorID = TE[i + 1].trainDescription;
		}
		// Deleting the Matrix
		delete[] Matr;
	}

	// Function to detect the implemented train order at the OL
	void Detect_Implemented_Order() {
		if (Implemented_Order.empty() || (LastEnteredTrain != Implemented_Order.back())) { // if the list is empty or the LastEnteredTrain is different from the last Element of the list
			if (LastEnteredTrain != "None")												   // And LastEnteredTrain is also different from None
				Implemented_Order.push_back(LastEnteredTrain);							   // add the train to the list
		}
	}

	// Function to Print the Implemented Order in a text file
	// Print this ordered list on a text file
	void Print_Implemented_Order(string FolderName, int Index_OL) {
		string FileName;
		char Ind_OL[20];
		snprintf(Ind_OL, sizeof(Ind_OL), "%d", Index_OL); // Fixing the right name for the OutputFile
		FileName = FileName + FolderName + "/Implemented_Order_OL" + Ind_OL + ".txt";
		ofstream ImplementedOrder;
		ImplementedOrder.open((char*)FileName.c_str());
		ImplementedOrder << Node_X << "\n"; // print the location of the OL on the top of the list
		list<string>::iterator it;			// iterator of the TrainsCrossingOL

		for (it = Implemented_Order.begin(); it != Implemented_Order.end(); it++) {
			ImplementedOrder << *it << "\n";
		}
		ImplementedOrder.close();
	}

	void compareImplementedOrderWithRomaSolution(string FolderName, int IndexOL, int t) {
		string FileName;
		char Instant[50], Index_OL[20];
		snprintf(Instant, sizeof(Instant), "%d", t);
		snprintf(Index_OL, sizeof(Index_OL), "%d", IndexOL);
		FileName = FolderName + "OL" + Index_OL + "_UnfeasibleOrders_At_" + Instant + ".txt";
		cout << FileName << "\n";
		list<string> UnfeasibleOrders; // This is a list of strings to log all the trains whose order given by ROMA is unfeasible to implement

		TrainEvent ImplemOrder[100]; // Array of TrainEvents created to collect all the trainIDs of the list ImplementedOrder

		unsigned int N_ImplemOrder = 0;
		N_ImplemOrder = Implemented_Order.size();
		list<string>::iterator it = Implemented_Order.begin(); // Initializing an iterator of the list Implemented_Order pointing at the first element of the list
															   // Assigning the trainDescription to each TrainEvent
		for (unsigned int i = 0; i < N_ImplemOrder; i++) {
			ImplemOrder[i].trainDescription = *it;
			it++; // increase the iterator of one position
		}

		// Assigning the Successor to each TrainEvent of the implemented order list
		for (int i = N_ImplemOrder - 1; i >= 0; i--) {
			ImplemOrder[i].SuccessorID = ImplemOrder[i + 1].trainDescription;
		}

		// Comparing the list of implemented orders with the one given by the ROMA Solution
		for (int i = 0; i < numTeList; i++) {									 // Looping over the elements of the list of ROMA Solution
			for (unsigned int j = 0; j < N_ImplemOrder; j++) {					 // looping over the elements of the list of implemented Order
				if (TE[i].trainDescription == ImplemOrder[j].trainDescription) { // if the TrainEvents in the ImplemOrder and in the ROMASolution are the same
					if ((j == 0) && (i != 0)) {									 // if the element in common is the first element of the ImplemOrderList (i.e. j=0), if the corresponding order in the ROMASolution is different it means that this order is not implementable
						UnfeasibleOrders.push_back(ImplemOrder[j].trainDescription);
					}

					if ((ImplemOrder[j].SuccessorID != "None") && (TE[i].SuccessorID != ImplemOrder[j].SuccessorID)) { // If the element in common is not the last element in the implemented order (when it is the last element its successor is indeed None) and if the successor is different
						UnfeasibleOrders.push_back(TE[i].SuccessorID);
					}
				}
			}
		}
		// Creating a file to write all the unfeasible orders of the ROMA Solution
		ofstream UnfeasibleOrd;
		UnfeasibleOrd.open((char*)FileName.c_str());
		if (UnfeasibleOrders.empty() != 1) {
			for (list<string>::iterator it = UnfeasibleOrders.begin(); it != UnfeasibleOrders.end(); it++) {
				UnfeasibleOrd << *it << "\n";
			}
		}
		UnfeasibleOrd.close();
	}
};

extern OrderList OL[20];

// Function to Load All The OrderLists referred to Critical Nodes in the Infrastructure
void loadAllOrderLists(char* FolderName);

void loadAllOrderListsUpgraded(char* FolderName);

// Function to Reset the Lists that will be updated at each instant
void resetOlToUpdate();

// Function to Assign to OL0 the same order of OL1
void Set_OL0_as_OL1();

// Function to Set the Critical Nodes In which a special order has to be respected and Link these nodes to the respective OrderLists OL
void setRespectOrderAndIndexOrderList();

// This is the function to set the departure sequence of trains at checkpoint nodes of a block section
void Set_RespectOrder_And_Index_OrderList_Upgraded();

// This is the function to set the departure sequence of trains at checkpoint nodes of multiple block sections having the same position of the start or end signals
void Set_RespectOrder_And_Index_OrderList_Upgraded_Improved();

// This is the function to set the departure sequence of trains at checkpoint nodes coinciding with signals of block sections which are before a merging junction or after a diverging junction
// To use this function the OL Node should coincide with the very last switch of a merging junction or with the very first switch of a diverging junction
// Also the OL file text should be defined by specifying at the second line whether it is a diverging or merging junction
void Set_RespectOrder_And_Index_OrderList_Upgraded_Improved_With_JunctionType();

void compareImplementedOrderWithRomaSolutionForAllOl(string FolderName, int t);

class BlockingTimes {
public:
	string BlockID;
	string NextBlockID; // This is the ID of the location or block section after this blocking time. By default the location after this one is a VirtualTDSB the location after the VirtualTDSB will be considered. if the location after VirtualTDSB is the last infra element of the route then NextBlockID is None. NextBlockID by default is never a VirtualTDSB
	string trainDescription;
	double StartRunTime;
	double EndRunTime;
	double StartApproachTime;
	double EndApproachTime;
	double StartClearTime;
	double EndClearTime;
	double length;
	double StartOccTime;
	double EndOccTime;
	double setupTime;
	double sightReacTime;
	double ApproachTime;
	double RunTime;
	double clearingTime;
	double ReleaseTime;
	double RunTimeMargin;
	double PosStart;
	double PosEnd;
	double GeoPosStart;
	double GeoPosEnd;
	string ConnectedBlockingTimeID;				 // This is the ID of another infrastructure element which is connected to the current one. For example if this is the starting edge of a switch its blocking time will be connected to the one of the ending edge of that switch
	double PosConnectedBlockingTime;			 // This is the Position of the connected Blocking Time along the route of the train
	double SpeedPreviousTrain;					 // This variable represents the speed of the train which previously crossed the infrastructure element. This variable is needed in the case of Virtual Coupling to compute the approaching time (that in such a case is the time needed by the train to accelerate/decelerate from its current speed to the speed of the previous train)
	string NamePreviousTrain;					 // This is the name of the previous train that has crossed a given infrastructure element
	string NextBlockIDPreviousTrain;			 // This is the ID of the next block section or location for the previous train
	int SignallingLevel;						 // This is the signalling level of the block section
	bool IsEndOfDivSwitchBeingStartOfADivSwitch; // Again this is the same variable appearing in the infrastructure elements. This bool variable is true when the infrastructure element we are computing the blocking time for is the ending edge of a diverging switch coninciding with the starting edge of another diverging switch
	bool InfraElementInPositionForTrain;		 // This is to indicate whether the Switch is in the right position for the train to cross, if it already in the right position then no additional setuptime needs to be added, otherwise additional setup time needs to be added
	bool LocationWithSwitch;
	bool IsAlreadyUniformedToConnectedSwitch; // This variable when it is true means that the blocking time has been already uniformed with the Blocking Time of the connected edge of diverging switch
	string SwitchName;						  // This is to indicate whether the location or block section has a switch or not
	string stationName;
	bool IsComplete;

	BlockingTimes();

	bool AreOnTheSameBlock(BlockingTimes blockSets) {
		bool AreOnSameBlock = false; // if the block does not have a station IsCommonBlock=true is enough to turn this variable into true, else if the block has a station this variable turns to true if IsCommonBlock is true and Blocking Time blockSets has the same stationName;
		bool IsCommonBlock = false;	 // This variable is true if one of the two blocking times have at least a block in common
		string BlockNameA;
		BlockNameA = this->BlockID;
		istringstream Line(BlockNameA);
		list<string> TokenA;
		string tok;

		int N_validTokInA = 0; // Number of valid tok in TokenA
		while (getline(Line, tok, '@')) {
			if (tok.size() > 0) {
				if ((N_validTokInA == 0) || (N_validTokInA == 2)) {
					TokenA.push_back(tok);
				}
				N_validTokInA++;
			}
		}

		string BlockNameB = blockSets.BlockID;
		istringstream Line2(BlockNameB);
		list<string> TokenB;
		string tok2;

		int N_validTokInB = 0; // Number of valid tok in TokenB
		while (getline(Line2, tok2, '@')) {
			if (tok2.size() > 0) {
				if ((N_validTokInB == 0) || (N_validTokInB == 2)) {
					TokenB.push_back(tok2);
				}
				N_validTokInB++;
			}
		}

		for (list<string>::iterator h = TokenA.begin(); h != TokenA.end(); h++) {
			for (list<string>::iterator p = TokenB.begin(); p != TokenB.end(); p++) {
				if (*p == *h) {
					IsCommonBlock = true;
					break;
				}
			}
			if (IsCommonBlock == 1)
				break;
		}
		// If the blocking time has a station then AreOnSameBlock is true only if IsCommonBlock is true and the blocking times have the same stationName
		if (stationName != "None") {
			if ((stationName == blockSets.stationName) && (IsCommonBlock == 1)) {
				AreOnSameBlock = true;
			}
		} else { // Otherwise if the blocking time does not have any station then AreOnSameBlock is true when IsCommonBlock is true
			if (IsCommonBlock == 1)
				AreOnSameBlock = true;
		}

		if (AreOnSameBlock == 1)
			return true;
		else
			return false;
	}

	// Function to see if two blocking times are overlapping
	bool AreBlocksOverlapping(BlockingTimes blockSets) {
		bool AreOverlapped = false;
		bool areBlocksConnected = false;
		areBlocksConnected = this->AreOnTheSameBlock(blockSets);
		if (areBlocksConnected == 1) {
			if (this->StartOccTime <= blockSets.StartOccTime) {
				if ((int)blockSets.StartOccTime - (int)this->EndOccTime < 0) { // if the blocks are overlapped
					AreOverlapped = true;
				}
			} else if (blockSets.StartOccTime <= this->StartOccTime) {
				if ((int)this->StartOccTime - (int)blockSets.EndOccTime < 0) { // if the blocks are overlapped
					AreOverlapped = true;
				}
			}
		}
		return AreOverlapped;
	}

	BlockingTimes& operator=(const BlockingTimes& obj2) {
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

	void DetermineLastStatusOfInfraElement(string TrainName, list<InfraEvent> ListAllInfraEvents) {
		bool IsStatusInfraElementFound = false; // this variable becomes true only when we have found the status of the infrastructure element we are computing the blocking times for, given a train
		if (ListAllInfraEvents.size() > 0) {
			for (list<InfraEvent>::iterator i = ListAllInfraEvents.begin(); i != ListAllInfraEvents.end(); i++) {
				if (this->LocationWithSwitch == 1) {
					if (this->SwitchName == i->ElementInfo.ID) {
						if (i->RecordedEvent.size() > 0) {
							for (list<TrainEvent>::iterator t = i->RecordedEvent.begin(); t != i->RecordedEvent.end(); t++) {
								if (t->trainDescription == TrainName) {
									if (t == i->RecordedEvent.begin()) {
										this->SpeedPreviousTrain = -1;
										this->InfraElementInPositionForTrain = false; // the first train always will need to set up switches on the infrastructure
									} else {
										string NeededInfraElementPosition;
										string LastInfraElementPosition;
										list<TrainEvent>::iterator u = t;
										u--;									  // iterator u points at the element before t in the list of recorded event of the infrastructure element
										this->SpeedPreviousTrain = u->TrainSpeed; // taking the speed of the previous train crossing the infrastructure element i
										// Comparing the previous position of infra element i with the needed position for let the train cross it
										LastInfraElementPosition = u->InfraElemStatus;
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
										if (NeededInfraElementPosition != LastInfraElementPosition) {
											this->InfraElementInPositionForTrain = false;
										}
									}
									IsStatusInfraElementFound = true; // Stating that we have found the last Status of the Infrastructure element
									break;							  // break the for loop on the recorded events of i when the right event has been found
								}
							}
						}
					}
				}

				else { // if instead the infrastructure element we need to compute the blocking time is not a switch
					if (this->BlockID == i->ElementInfo.ID) {
						if (i->RecordedEvent.size() > 0) {
							for (list<TrainEvent>::iterator t = i->RecordedEvent.begin(); t != i->RecordedEvent.end(); t++) {
								if (t->trainDescription == TrainName) {
									if (t == i->RecordedEvent.begin()) {
										this->SpeedPreviousTrain = -1;
										// infra elements which are not switches are always in position for the train to cross so InfraElementInPosition is set to its default value of true
									} else {
										list<TrainEvent>::iterator u = t;
										u--; // iterator u points at the element before t in the list of recorded events of infrastructure element i
										this->SpeedPreviousTrain = u->TrainSpeed;
									}
									IsStatusInfraElementFound = true; // Stating that we have found the last Status of the Infrastructure element
									break;							  // break the for loop on recorded events after we have founf the right TrainName
								}
							}
						}
					}
				}
				if (IsStatusInfraElementFound == 1) { // if we have found the status of the infra element we are computing the blocking time for, then we can break the for loop over the list of infrastructure elements and their events
					break;
				}
			}
		}
	}

	// This function is a much more elegant way of expressing the function above: DetermineLastStatusOfInfraElement, so better to use this one
	void checkIfLastInfraElementStatusIsInLineWithTrainNeeds(string TrainName, list<InfraEvent> ListAllInfraEvents) {
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
			cout << "ERROR 988: Name of InfraElement has not been assigned in the function to update the status of infrastructure elements for Block Section " << this->BlockID << " on Route of Train " << TrainName << "\n";
		}

		else {
			;
			//.log("Infra element : " + NameOfInfraElement);
			// cout << NameOfInfraElement << "\n";
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
			cout << "Error: Element " << NameOfInfraElement << " Not Found\n";
		}

		if (NeededInfraElementPosition != LastInfraElementPosition) { // The Infra Element will be not in position if NeededInfraElementPosition does not coincide with the LastInfraElementPosition

			this->InfraElementInPositionForTrain = false;
		}
	}
};

// Function to order blocking times by the StartOccTime
bool OrderByStartOccTime(BlockingTimes A, BlockingTimes blockSets);

// Function to Compute the Headway with another on a block section or a location
double ComputeHwForLocation(BlockingTimes A, BlockingTimes blockSets);

double ComputeHWForLocationToDepartureTime(BlockingTimes A, BlockingTimes blockSets, double DepTimeTrain1, double DepTimeTrain2);

// Function to Compute the Headway with another on a block section or a location (in this function we move blocking time blockSets always below blocking time A)
double ComputeHwForLocationByShiftingBBelowA(BlockingTimes A, BlockingTimes blockSets);

// Function to Compute the Headway with another on a block section or a location (in this function we move blocking time blockSets always below blocking time A)
double ComputeShiftAtTimetablePointsByShiftingBBelowA(BlockingTimes A, BlockingTimes blockSets, double timeB);

// Function that determines the correct departure headway to avoid conflicts (with this function we put blockingTime A always below Blocking Time blockSets)
double ComputeDepartureTimesToSolveConflicts(BlockingTimes A, BlockingTimes blockSets, double DepTimeTrain1, double DepTimeTrain2);

/**************************************************************************************************************************
*
Declaration of Train class
*
****************************************************************************************************************************/
extern int numRegions; /*Number of Speed ranges in the characteristic Tractive effort-speed curve of trains, Number of Trains loaded for the simulation( N_Train+N_TrainD)*/

extern int N_Train, N_TrainD; /*Number of Trains with even path, Number of Trains with odd path*/

// extern const int Max_N_Reg;
#define Max_N_Reg 700

extern double VirtualQ[Max_N_Reg], VirtualQD[Max_N_Reg];

class Train {
public:
	double number_of_wagons = 0.0; /*!< number of Wagons*/
	double massFactor = 0.0;			   // mass factor
	double mass_of_traction_unit = 0.0, mass_of_a_wagon = 0.0, total_train_mass = 0.0, max_train_decelaration = 0.0, max_train_speed = 0.0, g = 0.0, frontal_wagon_area = 0.0, massPerWagonAxle = 0.0, Jerk = 0.0, train_length = 0.0;
	// Mass of the Traction Unit(Kg), Mass of a Wagon(Kg), Total Mass(Kg), Maximum Train Deceleration(m/s2), Maximum Train Speed(m/s),Gravity Acceleration, Frontal Wagon Area , Jerk(m/s3), Length (m)
	std::vector<double> instant_train_speed; // Istantaneous Train Speed Vector(m/s)
	int velocityIntervals;								 // Number of intervals of speed for the Traction Unit of the Train
	double Vlb[20] = {}, Vub[20] = {};		 // Traction Unit Characteristic Speeds (Lower Bound Speed, Upper Bound Speed)
	double C0[20] = {}, C1[20] = {}, C2[20] = {};
	double resistanceCoefficient = 0.0;							  // Traction Curve Coefficients, Resistance coefficients
	std::vector<double> instant_spatial_position; // Vector of istantaneous Spatial position of the Train
	std::vector<std::string> instant_block_section_occupied;
	std::vector<double> instant_train_power_consumption;  // Vector of istantaneous Train Power Consumption [W]
	std::vector<double> instant_train_energy_consumption; // Vector of istantaneous Train Energy Consumption [MJ]
	double TotalEnergyConsumed, TotalEnergySubstationRequest, TotalEnergyConsWithRegBrak, TotalEnergySubstRequestWithRegBrak;
	// These values represent the total energy consumed by the train with no regenerative braking, the total energy requested to the SubStation, and the TotalEnergy consumed with regenerative braking;
	double EnergyForAuxiliaries = 0.0; // this is the amount of energy needed by auxiliaries and it is assumed as the 10% of the total energy consumption
	double departure_time = 0.0;	   // departure_time
	int brakingPoint;						   // brakingPoint is the position of the braking step after the intersection point between acceleration and braking curve
	double scheduled_departure_time = 0.0; // This is the departure time in the initial timetable not the actual scheduled_departure_time
	int temp, stop, counter;		 // Temporary variables for simulating train movement(temp=instant of time in which train starts braking, stop=time instant in which train starts stopping at a station), counter: number of times in which it enters in braking condition equation
	double ID = 0.0;				 // Train ID number
	string type;					 // This specifies if it is an Intercity, a regional or a metro train
	Arc As;							 // Temporary Arc object representing the Arc occupied by the train in a determined time instant
	Section Bs;						 // Temporary Block Section Object representing the Block Section occupied by the Train in a certain time instant
	double Start_Node_X = 0.0;		 // It represents the X of the Node from which the train starts its run
	string TrainRouteID;			 // This is the ID of the Route that the train has to follow
	int indexOfRoute;				 // This is the Index (ranging from 0 and N_Routes-1) of the Route with ID=TrainRouteID
	string trainDescription;		 // This is the Description of the train which is the union between the Type and the ID: trainDescription = Type+ID
	bool direction;
	bool GradientExceptionInBraking; // This variable is true when because of a too steep gradient the braking curve computed by the function DrawBrakingCurve has a point having a speed lower than the final speed V2 (in this case the train will first decrease its speed to this value lower than V2 and then reaccelerate to V2)
	std::vector<double> BX;
	Node* Stations = nullptr;
	int numStations;					   // Station is a dynamic array contating all Station Node for the train and int numStations is the Number of Stations (i.e. the dimension of Stations Array)
	int stationBlockSection[40];		// Cached block section index for each station (avoids full-route scan every timestep)
	int stationArc[40];				   // Cached Arc index within block section for each station
	bool ServiceStopBehindATrain = false; // This variable is true only if the train is stopping at a station behind another train. We admit that in moving block operations maximum two trains can perform a service stop queueing one after each other at the same platform
	bool StoppedForServiceStop = false;   // This variable is true when the train is stopping at a station to perform a service stop
	string CurrentServiceStop;		   // This variable indicates the name of the Station the train is currently stopping at when StoppedForServiceStop=true
	string CurrentServiceStopPlatform; // Id of the platform at which the train has stopped when making a service stop at a scheduled station/stop>

	double XCurrentServiceStop;	   // This is the relative abscissa on the route of the train of a service Stop when the train is stopping behind another train at the same platform
	double StationArrivals[40];	   // This variable is the time instant in which the train actually enters a station
	string StationArrivalNames[40]; // Preserves served station names for post-run arrival analysis.
	double StationDelay[40];	   // This variable represent the arrival delay of a train at a certain station
	double StationConsecDelay[40]; // This is the Consecutive delay at stations i.e. ArrivalDelay-Entrance Delay-Sum of disturbances at previous stations
	double StationDisturbance[40]; //  This variable represents the disturbance to dwell times at stations
	int N_Station_Stopped;		   // This variable counts the number of stations at which the train has stopped
	double ScheduledArrivals[40] = {};  // This Array contains scheduled train arrival time at stations (it is necessary to calculate train deviations from timetable and therefore train delays)
	double ScheduledDepartures[40] = {};
	int delayed;						// This is a boolean parameter: if delayed =1 the train will experience a delay at a determined station, else if delayed=0 the train won't experience any delay
	double TotalInputDelays = 0.0;		// Sum of the Entrance Delays +stochastic disturbances to dwell times
	double EntranceDelay;				// This is the entrance delay of the train on the network
	bool CanEnter;						// This Variable becomes True when time is major than departure_time and if for the first Time signalling_block_sections[0] is free (i.e. its code is 270)
	bool OutOfSimulation;				// if true it means that the train is out of the simulation, i.e. it has been already simulated
	int counter2;						// When Counter2 becomes 1, CanEnter variable becomes true and the train can start the simulation
	bool IsTrainCoupling;				// This int variable counts the number of times that the train receives a MA in Signalling Level 4 where it could Virtually Couple to another train ahead having the same direction of movement and the a shared route ahead
	bool IsTrainInFollowingMode;		// This boolean is true when the train is following another one in VC mode
	bool IsInUnintentionalDecoupling;	// This variable is set to true when a train in following mode starts outdistancing from the leader because of conditions due to speed limitations, gradient or curvature radii on the tracks is crossing and not to an intentional decoupling due to a MA where the route diverges from the leader's
	bool IsTrainDecoupling;				// This boolean is true when the train is Decoupling from the VC mode
	int CounterFollowingMode;			// This variable counts the seconds the train is in following mode under Virtual Coupling
	string LeadingTrainInFollowingMode; // This is the trainDescription of the leading train when this train is in following mode
	double Xobmin, Vobmin;
	std::vector<double> Xob, Vob;
	double Sbrak[2000] = {}, Vbrak[2000] = {}; // Vector Sbrak: Vector of curviline abscissas belonging to a braking curve;  Vector Vbrak: Vector of speeds belonging to a braking curve
	int BrakStep;
	bool BrakingForEoA = false;					  // When this is true it means that the train is braking because of an ETCS End of Authority on the route
	MovementAuthority Predicted_MA_To_CoupleAt;	  // This is the Movement Authority where the train is predicted to virtually couple to a train ahead
	MovementAuthority Last_Received_MA;			  // This is the last movement authority applied to the train
	MovementAuthority Last_MA_StoppedAt;		  // this is the last movement authority the train has stopped at
	MovementAuthority Predicted_MA_To_DecoupleAt; // This is the movement authority where the train is predicted to decouple from the train ahead
	std::vector<int> Eq;						  // Number of speed interval of theTraction curve function
	int RunStartTime;							  // Represents the time instant in which the train start his run from the beginning (In normal conditions it's equal to Train headway, but when the first light is red, this variable assumes a value higher than Headway)
	BlockingTimes BlockTime[1000];				  // This represents the blocking time of the train
	int N_BlockSections;						  // This is the total Number of BlockSections and therefore Blocking Times
	int N_BlockTimeComplete;
	int numOverlaps; // This is the number of train overlaps
	double Final_Delay;
	int End_Time;						// Time instant in which the train exits simulation
	int earliestActiveTrajectoryIndex = -1;		// First simulation sample in the active run
	double HwMatrix[100][1000];			// This matrix contains on the columns the locations crossed by the train route, while on the columns the trains that have block sections in common
	list<Location> LocationNames;		// This LocationNames contain the name of block sections, or stations crossed by the train along its route
	list<string> ConflictingTrains;		// These are the trains which have common block sections.
	int N_ConflictingTrains;			// Number Of Conflicting Trains
	double ETCS3StoppingPoint;			// This is the point in km where the train is stopped because of ETCS 3 Movement Authority
	bool IsTrainStoppedForEoA;			// This boolean is true when the train is stopped because it reached the ETCS 3 End of Authority
	list<TrainEvent> TimetablePoints;	// This lists contains the arrival and departure of the train at each interlocking area of the network either those where the train stops and those where they train does not stop at
	vector<DispatchDecision> dispDecisions; //  vector containing dispatching decisions to be implemented
	int prevIntendedDepTime;			//  stores the previous intended dep time from dispatching tool
	int dispLineID = -1;				//  lineID from dispatching tool
	int arrivalPlatform = -1;			//  arrival platform from dispatching tool
	int reservedPlatform;				//  variable to book platform at destination
	// std::map<char, std::list<std::pair<int, double>>> trainCorrPosTime; // train position over time for each corridor

	float GibsonDwellTimeParameters[8]; // this is a vector of floats to store the Gibson dwell time parameters of the trains
	int MAX_OnBoard_Passengers, Current_OnBoard_Passengers;

	// Class Default Values

	Train();
	// Pre-compute the (block section, Arc) index for each station Node.
	// Called after Stations or route changes; eliminates the O(N_BS*total_arcs*numStations)
	// triple scan from the per-timestep hot path.
	void cacheStationPositions() {
		for (int s = 0; s < 40; s++) {
			stationBlockSection[s] = -1;
			stationArc[s] = -1;
		}
		for (int s = 0; s < numStations; s++) {
			for (int h = 0; h < train_route[indexOfRoute].N_Block_Sections; h++) {
				for (int k = 0; k < train_route[indexOfRoute].sequence_of_block_sections[h].total_arcs; k++) {
					if (train_route[indexOfRoute].sequence_of_block_sections[h].arcs_in_signalling_block_section[k].endNode.stationName == Stations[s].stationName) {
						stationBlockSection[s] = h;
						stationArc[s] = k;
					}
				}
			}
		}
	}


	// Calculation of Tractive Effort of the Traction Unit

	virtual double tractiveEffort(double V) {
		double Ftr = 0.0;
		for (int i = 0; i < velocityIntervals; i++) {
			if ((V >= Vlb[i]) && (V < Vub[i])) {
				Ftr = C0[i] + C1[i] * V + C2[i] * pow(V, 2);
			}
		}
		return Ftr;
	}

	//  Calculation of Braking Effort affected by Jerk

	virtual double brakingEffort(double T) {
		double FBr;
		if (Jerk * T < max_train_decelaration) {
			FBr = total_train_mass * massFactor * Jerk * T;
		} else {
			FBr = total_train_mass * massFactor * max_train_decelaration;
		}
		return FBr;
	}


	//! Calculation of Traction Unit Resistances due to air viscosity
	//!  (Modified Strahl Formulae_Italian FS Formulation)
	virtual double traction_unit_resistences(double V) {
		double FRt;
		FRt = 4.2 * (mass_of_traction_unit * g / 1000) + 0.72 * pow(((V + 4.17) * 3.6), 2);
		return FRt;
	}

	//! Calculation of Train Wagons Resistances using the Sauthoff Formula
	virtual double wagons_resistances(double V) {
		double Frwp;
		Frwp = (1.9 + resistanceCoefficient * (V * 3.6)) * (massPerWagonAxle * g / 1000) + 4.8 * (number_of_wagons + 2.7) * frontal_wagon_area * pow(((V + 4.17) * 3.6 / 10), 2); // Sauthoff Formulae for Passenger Trains
		return Frwp;
	}

	//! Calculation of Train Resistances due to Track Gradient
	virtual double gradient_resistances(double G) {
		double Frlg;
		Frlg = g * (total_train_mass)*G;
		return Frlg;
	}

	//! Calculation of Train Resistances due to Track Curvature
	virtual double curvature_resistances(double R) {
		double Frlc;
		Frlc = g * (total_train_mass) * 700 / (R * 1000);
		return Frlc;
	}

	//! Calculation of the sum of all Train Resistances
	virtual double total_train_resistances(double V, double G, double R) {
		double FR;
		FR = traction_unit_resistences(V) + wagons_resistances(V) + gradient_resistances(G) + curvature_resistances(R);
		return FR;
	}

	virtual void Initialise_Gibson_Dwell_Time_Parameters(float beta0, float beta1, float beta2, float beta3, float beta4, float beta5, float beta6, float beta7) {
		this->GibsonDwellTimeParameters[0] = beta0;
		this->GibsonDwellTimeParameters[1] = beta1;
		this->GibsonDwellTimeParameters[2] = beta2;
		this->GibsonDwellTimeParameters[3] = beta3;
		this->GibsonDwellTimeParameters[4] = beta4;
		this->GibsonDwellTimeParameters[5] = beta5;
		this->GibsonDwellTimeParameters[6] = beta6;
		this->GibsonDwellTimeParameters[7] = beta7;
	}

	//! Calculation of Train Energy Consumption
	double train_energy_consumption(int t) {
		double Pot1 = 0;
		double Pot2 = 0;
		if (instant_train_power_consumption[t - 1] >= 0)
			Pot1 = instant_train_power_consumption[t - 1];
		else
			Pot1 = 0;
		if (instant_train_power_consumption[t] >= 0)
			Pot2 = instant_train_power_consumption[t];
		else
			Pot2 = 0;
		instant_train_energy_consumption[t] = instant_train_energy_consumption[t - 1] + ((Pot1 + Pot2) / 2000000) * timestep;
		return instant_train_energy_consumption[t];
	}

	// Function to calculate Total Energy Consumed with and without regenrative braking (the inputs are the Eta of the Substation and the Eta of the regenerative braking)
	void TotalEnergyConsumptionWithAndWithoutRegBraking(double EtaSubst, double EtaRegBrak) {
		// This is the total Energy consumed by the train to move
		this->TotalEnergyConsumed = instant_train_energy_consumption[End_Time];
		this->EnergyForAuxiliaries = 0.10 * TotalEnergyConsumed;

		// Adding the EnergyForAuxiliaries to the total Energy consumed
		TotalEnergyConsumed = TotalEnergyConsumed + EnergyForAuxiliaries;

		// The total Energy requested to the Substation is TotalEnergyConsumed/EtaSubst
		this->TotalEnergySubstationRequest = TotalEnergyConsumed / EtaSubst;

		// Computing the EnergyConsumedwith the regenerative braking

		int TrainStartTime = (int)departure_time;
		TotalEnergyConsWithRegBrak = instant_train_energy_consumption[TrainStartTime]; // Initialize the first value of the totalEnergyConsumption

		for (int i = TrainStartTime + 1; i <= End_Time; i++) {

			double Pot1 = 0, Pot2 = 0;
			// Setting Pot1
			if (instant_train_power_consumption[i - 1] >= 0)
				Pot1 = instant_train_power_consumption[i - 1];
			else
				Pot1 = EtaRegBrak * instant_train_power_consumption[i - 1];
			// Setting Pot 2
			if (instant_train_power_consumption[i] >= 0)
				Pot2 = instant_train_power_consumption[i];
			else
				Pot2 = EtaRegBrak * instant_train_power_consumption[i];

			this->TotalEnergyConsWithRegBrak = TotalEnergyConsWithRegBrak + ((Pot1 + Pot2) / 2000000) * timestep;
		}
		// Adding the energy for auxiliaries to the TotalEnrgy Consuption with regenerative braking
		TotalEnergyConsWithRegBrak = TotalEnergyConsWithRegBrak + EnergyForAuxiliaries;

		this->TotalEnergySubstRequestWithRegBrak = TotalEnergyConsWithRegBrak / EtaSubst; // this is the energy requested rom the substation by the train when it uses regenerative braking
	}

	// Check the entrance possibility: This Function checks if the train can Enter the simulation (i.e. when time>=departure_time and for the first time signalling_block_sections[0] is free)
	/*void checkEntrance(int i,Section *signalling_block_sections){ if((ID==1)||(ID==111)||(ID==113)||(ID==N_Train+1)){
	if((i>=departure_time)&&(signalling_block_sections[0].code==270)){counter2++;
	if(counter2==1){CanEnter=true;}}}
	else{
	//Condition for Even Train Runs (i.e. Runs with direction =0)
	if(direction==0){
	if((i>=departure_time)&&(VirtualQ[(int)ID-2]==ID-1)&&((signalling_block_sections[0].code==180)||(signalling_block_sections[0].code==270))){counter2++;
	if(counter2==1){CanEnter=true;}}}
	//Condition for Odd Train Runs (i.e. Runs with direction =1)
	else{
	if((i>=departure_time)&&(VirtualQD[(int)ID-N_Train-2]==ID-1)&&((signalling_block_sections[0].code==180)||(signalling_block_sections[0].code==270))){counter2++;
	if(counter2==1){CanEnter=true;}}}

	}
	}*/

	//! Check the entrance possibility:
	//! This Function checks if the train can Enter the simulation
	//!  (i.e. when time>=departure_time and for the first time signalling_block_sections[0] is free)
	void checkEntrance(int i, Section* BS) {
		string PreviousTrain = "None"; // the previous Train according to the list
		int IndexOL = BS[0].arcs_in_signalling_block_section[0].startNode.indexOrderList;

		if ((IndexOL >= 0) && (IndexOL < N_OrderLists) && (IndexOL < 20) && (OL[IndexOL].numTeList > 0)) {
			cout << "this is the indexOL TRUE \n"
				 << IndexOL;
			for (int j = 1; j < OL[IndexOL].numTeList; j++) { // Determining which is the previous train in the list
				if (trainDescription == OL[IndexOL].TE[j].trainDescription)
					PreviousTrain = OL[IndexOL].TE[j - 1].trainDescription;
			}
			// if the Previous Train has already departed or if this train is the first of the list to depart
			if ((PreviousTrain == OL[IndexOL].LastEnteredTrain) || (PreviousTrain == "None") || (trainDescription == OL[IndexOL].TE[0].trainDescription)) {
				// if the time is equal or higher than the departure time and the signals are permissive set CanEnter true
				if ((BS[0].SignallingLevel == 3) || (BS[0].SignallingLevel == 4)) {
					bool IsRouteStartingPointOccupied = false; // this bool is true if start_node of signalling_block_sections[0] is present in the list of Movement Authorities ETCS_MA, i.e. if the beginning of the train route is occupied by another train
					if (ETCS_MA.size() > 0) {
						for (list<MovementAuthority>::iterator it = ETCS_MA.begin(); it != ETCS_MA.end(); it++) {
							// if the list of Movement Autority includes a Movement Autority related to the beginning Node of the Route which belongs to a train different from this, then break the for loop over the list of ETCS_MA and set the boolean IsRouteStartingPointOccupied to true
							if ((it->BSID == BS[0].ID) && (abs(it->AbsPosEoA - BS[0].GeoXBegNode) < 0.001) && (it->TrainInfo.trainDescription != this->trainDescription)) {
								IsRouteStartingPointOccupied = true;
								break;
							}
						}
					}
					if ((i >= departure_time) && (IsRouteStartingPointOccupied == 0)) {
						counter2++;
						if (counter2 == 1)
							CanEnter = true;
					}
				} else {
					if ((i >= departure_time) && ((BS[0].code == 270) || (BS[0].code == 180))) {
						counter2++;
						if (counter2 == 1)
							CanEnter = true;
					}
				}
			}
		}

		else { // In case there is no ordered sequence of trains to be respected by trains at the beginning of the route

			if ((BS[0].SignallingLevel == 3) || (BS[0].SignallingLevel == 4)) { // if the signalling system of the firt block section is ETCS Level 3 or Level 4 (Virtual Coupling)
				bool IsRouteStartingPointOccupied = false;						// this bool is true if start_node of signalling_block_sections[0] is present in the list of Movement Authorities ETCS_MA, i.e. if the beginning of the train route is occupied by another train
				if (ETCS_MA.size() > 0) {
					for (list<MovementAuthority>::iterator it = ETCS_MA.begin(); it != ETCS_MA.end(); it++) {
						// if the list of Movement Autority includes a Movement Autority related to the beginning Node of the Route which belongs to a train different from this, then break the for loop over the list of ETCS_MA and set the boolean IsRouteStartingPointOccupied to true
						if ((it->BSID == BS[0].ID) && (abs(it->AbsPosEoA - BS[0].GeoXBegNode) < 0.001) && (it->TrainInfo.trainDescription != this->trainDescription)) {
							IsRouteStartingPointOccupied = true;
							break;
						}
					}
				}
				if ((i >= departure_time) && (IsRouteStartingPointOccupied == 0)) {
					counter2++;
					if (counter2 == 1)
						CanEnter = true;
				}

			} else { // if instead the signalling system of the first block section is a fixed-block signalling system
				if ((i >= departure_time) && ((BS[0].code == 270) || (BS[0].code == 180))) {
					counter2++;
					if (counter2 == 1)
						CanEnter = true;
				}
			}
		}
	}

	// Function to compute dwell times based on the interaction with passengers
	// By default the dwell time is computed based on the microscopic dwell time model by Fernandez et al. (2007) which extends the model by Gibson et al. (1989)
	double computePaxDependentDwellTimeAtStations(int N_BoardPax, int N_AlightPax, double PlatformOccupancyRate, float beta0, float beta1, float beta2, float beta3, float beta4, float beta5, float beta6, float beta7);

	// Function to detect if a train has to stop at a Node to respect a certain order :
	/**this function returns true if the train has to stop, false otherwise*/

	bool Train_Must_Stop_For_OL_Order(bool respectOrder, int IndexOrderList) {
		bool TrainMustStop = false;
		// Check if in this Node an Order given by an OL list has to be respected
		if (respectOrder == 1) {
			int IndexOL = IndexOrderList;
			string PreviousTrain = "None";
			for (int j = 1; j < OL[IndexOL].numTeList; j++) { // Determining the Previous train that has to pass in this Node
				if (OL[IndexOL].TE[j].trainDescription == trainDescription) {
					PreviousTrain = OL[IndexOL].TE[j - 1].trainDescription;
					break;
				}
			}
			// if the previous train has not passed yet and if this train is different from the first train of the OL list or the train is not in the list at all (PreviousTrain=None), then this train has to stop
			if ((PreviousTrain != OL[IndexOL].LastEnteredTrain) && (PreviousTrain != "None") && (trainDescription != OL[IndexOL].TE[0].trainDescription))
				TrainMustStop = true;
		}
		return TrainMustStop;
	}

	// This Function tests if the conditions for the train to stop at a certain point subsist:
	/**the train has to stop at a Node with Respsect Order=1 when
   the Previous train has not passed yet and when the aspect of its block section is restrictive(red or red_red)*/
	bool Conditions_To_Stop_Train_For_OL_Order(bool respectOrder, int IndexOrderList, const Section& BS) {
		bool Condition_To_Stop_Train = false;
		// Verifying if the train must stop because the previous train of the list has not passed yet
		bool TrainMustStop = Train_Must_Stop_For_OL_Order(respectOrder, IndexOrderList);
		// if the previous train has passed and the signal is different from restrictive aspects(red for signalling levels different than BACC or red_red for BACC) Condition_To_Stop_Train is false
		if ((TrainMustStop == 1) || ((BS.SignallingLevel == 5) && (strcmp(BS.state, "red_red") == 0)) || ((BS.SignallingLevel != 5) && (strcmp(BS.state, "red") == 0)))
			Condition_To_Stop_Train = true;
		// else stop the train
		else
			Condition_To_Stop_Train = false;
		return Condition_To_Stop_Train;
	}

	// Function to make the trains respect the order given by the Order List in the nodes where a given train order must be respected
	/**The aim of this function is to set to 0 the speedInBraking of the Arc terminating with these nodes, when the previous train in the OL has not passed yet*/
	void Set_Conditions_To_Respect_OL_Orders(Route& R) {
		for (int h = 0; h < R.N_Block_Sections; h++) {
			for (int k = 0; k < R.sequence_of_block_sections[h].total_arcs; k++) {
				if (R.sequence_of_block_sections[h].arcs_in_signalling_block_section[k].endNode.respectOrder == 1) {
					int IndexOL = R.sequence_of_block_sections[h].arcs_in_signalling_block_section[k].endNode.indexOrderList;
					string PreviousTrain;
					for (int j = 1; j < OL[IndexOL].numTeList; j++) {
						if (OL[IndexOL].TE[j].trainDescription == trainDescription) {
							PreviousTrain = OL[IndexOL].TE[j - 1].trainDescription;
							break;
						}
					}
					if ((PreviousTrain != OL[IndexOL].LastEnteredTrain) && (trainDescription != OL[IndexOL].TE[0].trainDescription)) {
						R.sequence_of_block_sections[h].arcs_in_signalling_block_section[k].speedInBraking = 0;
					}
				}
			}
		}
	}

	// Function to make the trains respect the order given by the Order List in the nodes where a given train order must be respected
	/** The aim of this function is to set to 0 the speedInBraking of the Arc terminating with these nodes, when the previous train in the OL has not passed yet */
	void Set_Conditions_To_Respect_OL_Orders(Arc& A) {
		if (A.endNode.respectOrder == 1) {
			int IndexOL = A.endNode.indexOrderList;
			string PreviousTrain;
			for (int j = 1; j < OL[IndexOL].numTeList; j++) {
				if (OL[IndexOL].TE[j].trainDescription == trainDescription) {
					PreviousTrain = OL[IndexOL].TE[j - 1].trainDescription;
					break;
				}
			}
			if ((PreviousTrain != OL[IndexOL].LastEnteredTrain) && (trainDescription != OL[IndexOL].TE[0].trainDescription)) {
				A.speedInBraking = 0;
			}
		}
	}

	// Function to Reset to the initial conditionsthe speedInBraking of the Nodes connected to the OL lists.
	/**This function is needed to avoid that other trains are conditioned by the same instances that a specific train has to respect to satisfy the given orders */
	void Reset_Original_vb_For_All_OL_Nodes(Route& R) {
		for (int h = R.N_Block_Sections - 1; h >= 0; h--) {
			if (R.sequence_of_block_sections[h].end_node.respectOrder == 1) {
				if (h == R.N_Block_Sections - 1) // if the OL Node is just the last Node of the route train_route assign to this Node speedInBraking = speedLimit of the same Arc
					R.sequence_of_block_sections[h].arcs_in_signalling_block_section[R.sequence_of_block_sections[h].total_arcs - 1].speedInBraking = R.sequence_of_block_sections[h].arcs_in_signalling_block_section[R.sequence_of_block_sections[h].total_arcs - 1].speedLimit;
				else // Otherwise assign to the Node speedInBraking = to the speed limit of the first arcs_in_signalling_block_section of the next block section
					R.sequence_of_block_sections[h].arcs_in_signalling_block_section[R.sequence_of_block_sections[h].total_arcs - 1].speedInBraking = R.sequence_of_block_sections[h + 1].arcs_in_signalling_block_section[0].speedLimit;
			}
			if (R.sequence_of_block_sections[h].total_arcs >= 2) {
				for (int k = R.sequence_of_block_sections[h].total_arcs - 2; k >= 0; k--) {
					if (R.sequence_of_block_sections[h].arcs_in_signalling_block_section[k].endNode.respectOrder == 1)
						R.sequence_of_block_sections[h].arcs_in_signalling_block_section[k].speedInBraking = R.sequence_of_block_sections[h + 1].arcs_in_signalling_block_section[k].speedLimit;
				}
			}
		}
	}

	// Function to Update the LastEntered Train for the all OrderLists OL in the network
	void Update_LastEnteredTrain_For_All_OL(int i, const Section& BS) {
		// Search if in the Block Section there are Nodes connected with Ordered Lists (thereofore in which a certain Order has to be respected)
		double AbsoluteChecklistPos = -1;
		double RelativeChecklistPos = -1;
		for (int j = 0; j < BS.total_arcs; j++) {
			if ((BS.arcs_in_signalling_block_section[j].startNode.respectOrder == 1) || (BS.arcs_in_signalling_block_section[j].endNode.respectOrder == 1)) {
				if (BS.arcs_in_signalling_block_section[j].startNode.respectOrder == 1) {
					RelativeChecklistPos = BS.arcs_in_signalling_block_section[j].startNode.X * 1000;
				} else if (BS.arcs_in_signalling_block_section[j].endNode.respectOrder == 1) {
					RelativeChecklistPos = BS.arcs_in_signalling_block_section[j].endNode.X * 1000;
				}
				if (train_route[this->indexOfRoute].reversed_direction == 1) {
					AbsoluteChecklistPos = BS.GeoXBegNode - (RelativeChecklistPos - BS.start_node.X * 1000);
				} else {
					AbsoluteChecklistPos = RelativeChecklistPos;
				}
				int OL_Index = -1;
				for (int h = 0; h < N_OrderLists; h++) {
					cout << "Order list info \n"
						 << OL[h].BlockID << "\n"
						 << BS.ID << "\n"
						 << OL[h].Node_X * 1000 << "\n"
						 << AbsoluteChecklistPos << "\n";

					if ((OL[h].BlockID == BS.ID) && (abs(OL[h].Node_X * 1000 - AbsoluteChecklistPos) < 0.001))
						OL_Index = h;
				}
				// In particular to update this value it is necessary that the train has passed with all its length train_length the checklist Node to guarantee a safe train separation
				if ((instant_spatial_position[i - 1] - train_length <= RelativeChecklistPos) && (instant_spatial_position[i] - train_length > RelativeChecklistPos)) {
#pragma omp critical
					if (OL_Index < 0)
						cout << "sss";
					else {
						OL[OL_Index].LastEnteredTrain = trainDescription;
						cout << OL[OL_Index].LastEnteredTrain << "/n";
					}
				}
			}
		}
	}

	// Function to Update the LastEntered Train for the all OrderLists OL in the network
	void Update_LastEnteredTrain_For_All_OL_Improved(int i, const Section& BS) {
		// Search if in the Block Section there are Nodes connected with Ordered Lists (thereofore in which a certain Order has to be respected)
		double AbsoluteChecklistPos = -1;
		double RelativeChecklistPos = -1;
		int IndexOrderList = -1;
		for (int j = 0; j < BS.total_arcs; j++) {
			if ((BS.arcs_in_signalling_block_section[j].startNode.respectOrder == 1) || (BS.arcs_in_signalling_block_section[j].endNode.respectOrder == 1)) {

				if (BS.arcs_in_signalling_block_section[j].startNode.respectOrder == 1) {
					RelativeChecklistPos = BS.arcs_in_signalling_block_section[j].startNode.X * 1000;
					IndexOrderList = BS.arcs_in_signalling_block_section[j].startNode.indexOrderList;
				} else if (BS.arcs_in_signalling_block_section[j].endNode.respectOrder == 1) {
					RelativeChecklistPos = BS.arcs_in_signalling_block_section[j].endNode.X * 1000;
					IndexOrderList = BS.arcs_in_signalling_block_section[j].endNode.indexOrderList;
				}
				if (train_route[this->indexOfRoute].reversed_direction == 1) {
					AbsoluteChecklistPos = BS.GeoXBegNode - (RelativeChecklistPos - BS.start_node.X * 1000);
				} else {
					AbsoluteChecklistPos = RelativeChecklistPos;
				}

				// In particular to update this value it is necessary that the train has passed with all its length train_length the checklist Node to guarantee a safe train separation
				if ((instant_spatial_position[i - 1] - train_length <= RelativeChecklistPos) && (instant_spatial_position[i] - train_length > RelativeChecklistPos)) {
#pragma omp critical

					OL[IndexOrderList].LastEnteredTrain = trainDescription;
					cout << OL[IndexOrderList].LastEnteredTrain << "\n";
				}
			}
		}
	}

	void Print_Out_vb_Arc(const Section& BS, int t, int Next_Block) {
		ofstream output;
		string NameFile;
		NameFile = NameFile + InputMainFolder + "/Routes/BSvb_" + trainDescription + ".txt";
		output.open((char*)NameFile.c_str(), ios::app);
		output << t << " " << BS.arcs_in_signalling_block_section[BS.total_arcs - 1].speedInBraking << " " << Next_Block << "\n";
		output.close();
	}

	// Function to check if the End of Authority given by the RBC for ETCS 3 is still valid
	void checkIfMovementAuthorityStillValid(double V_i_1, double S_i_1, const Section& CurrentBS) {
		bool IsMAStillValid = false;
		if ((V_i_1 == 0) && ((this->Last_MA_StoppedAt.RelativePosEoA - S_i_1) < 4) && (CurrentBS.ID == Last_MA_StoppedAt.BSID) && (IsTrainStoppedForEoA == 1)) {
			if (ETCS_MA.size() > 0) {
				for (list<MovementAuthority>::iterator it = ETCS_MA.begin(); it != ETCS_MA.end(); it++) {
					if ((it->BSID == Last_MA_StoppedAt.BSID) && (abs(it->AbsPosEoA - Last_MA_StoppedAt.AbsPosEoA) < 0.001) && (it->TrainInfo.trainDescription == Last_MA_StoppedAt.TrainInfo.trainDescription)) {
						IsMAStillValid = true; // if the movement authority where the train is stopped at is still valid, then set IsMAStillValid to true
						break;				   // and break the loop over the ETCS_MA list
					}
				}
			}

			// if the Ma is no valid anymore then set the variable IsTrainStopped for Eoa to false
			if (IsMAStillValid == false) {
				BrakingForEoA = false;
				IsTrainStoppedForEoA = false;
			}
			// otherwise set it to true and let the train still be stopped at the EoA
			else {
				IsTrainStoppedForEoA = true;
			}
		}
	}

	bool checkIfTrainCanDepartAfterHavingServiceStopBehindATrain(int t) {
		bool CanTrainDepartFromServiceStop = false;
		double dep_time = 0;
		double stoptime = 0;
		double TimeStopped = 0;
		for (int s = 0; s < numStations; s++) {
			if (Stations[s].stationName == this->CurrentServiceStop) {
				Stations[s].StepStopped++;
			recordCurrentServiceStopArrival(t);
				TimeStopped = Stations[s].StepStopped;
				dep_time = ScheduledDepartures[s];
				stoptime = Stations[s].StopTime;
				if (Stations[s].StepStopped > Stations[s].StopTime) {
					stoptime = 0;
				}
			}
		}
		if ((TimeStopped <= stoptime) || (t <= dep_time)) {
			CanTrainDepartFromServiceStop = false;
		}

		else {
			CanTrainDepartFromServiceStop = true;
		}
		return CanTrainDepartFromServiceStop;
	}

	// Calculation of Dynamic Braking Distance
	virtual double BrakDist(double V1, double V2, double G, double R) {
		double BD;
		if ((V1 > V2) && (V1 > 5)) {
			BD = V1 * timestep * 2 + (max_train_decelaration + (gradient_resistances(G) + curvature_resistances(R)) / (total_train_mass * massFactor)) * (V1 + V2) / (2 * Jerk) + (pow(V1, 2) - pow(V2, 2)) / (2 * (max_train_decelaration + (gradient_resistances(G) + curvature_resistances(R)) / (total_train_mass * massFactor)));
		} else if ((V1 > V2) && (V1 <= 5)) {
			BD = V1 * timestep * 3 + (max_train_decelaration + (gradient_resistances(G) + curvature_resistances(R)) / (total_train_mass * massFactor)) * (V1 + V2) / (2 * Jerk) + (pow(V1, 2) - pow(V2, 2)) / (2 * (max_train_decelaration + (gradient_resistances(G) + curvature_resistances(R)) / (total_train_mass * massFactor))) + 2;
		} else
			BD = 0;
		return BD;
	}

	// Function to compute the braking distance in a faster way using the closed formula and average gradient and curvature values (This function has shown the best match to simulated braking curves)
	virtual double BrakingDistanceFastComputation(double V1, double V2, double X1, double X2, Section* BS, int N_BS) { // V1 and X1 are the current train speed and position while X2 and V2 are the target position and speed, signalling_block_sections and N_BS are the list of Block Sections and the number of Block Sections along the train route
		double AverageGradient = 0;
		double AverageCurvature = 0;
		int N_arcs = 0;

		for (int i = 0; i < N_BS; i++) {
			for (int j = 0; j < BS[i].total_arcs; j++) {
				if ((BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000 > X1) && (BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000 <= X2)) {
					AverageGradient = AverageGradient + BS[i].arcs_in_signalling_block_section[j].gradient;
					AverageCurvature = AverageCurvature + BS[i].arcs_in_signalling_block_section[j].curvature;
					N_arcs++;
				}
			}
		}
		// Computing Average Gradient and Average Curvature Radii
		AverageGradient = AverageGradient / N_arcs;
		AverageCurvature = AverageCurvature / N_arcs;

		// Computing the braking curve with closed formula
		double BrakingDistance;
		// BrakingDistance = V1 * timestep + (max_train_decelaration + total_train_resistances(((V1 + V2) / (2)), AverageGradient, AverageCurvature) / (total_train_mass*massFactor))*(V1 + V2) / (2 * Jerk) + (pow(V1, 2) - pow(V2, 2)) / (2 * (max_train_decelaration + total_train_resistances(((V1 + V2) / (2 * Jerk)), AverageGradient, AverageCurvature) / (total_train_mass*massFactor)));   //this formula is the full closed braking curve formula

		BrakingDistance = (pow(V1, 2) - pow(V2, 2)) / (2 * (max_train_decelaration + total_train_resistances(((V1 + V2) / 2), AverageGradient, AverageCurvature) / (total_train_mass * massFactor))); // this one resulted to be the best in terms of closeness to simulation results

		return BrakingDistance;
	}

	// Function to compute the braking distance in a faster way using the closed formula and average gradient and curvature values
	virtual double BrakingDistanceFastComputation_PieceWise(double V1, double V2, double X1, double X2, Section* BS, int N_BS) { // V1 and X1 are the current train speed and position while X2 and V2 are the target position and speed, signalling_block_sections and N_BS are the list of Block Sections and the number of Block Sections along the train route
		double AverageGradient = 0;
		double AverageCurvature = 0;
		int N_arcs = 0;

		for (int i = 0; i < N_BS; i++) {
			for (int j = 0; j < BS[i].total_arcs; j++) {
				if ((BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000 > X1) && (BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000 <= X2)) {
					AverageGradient = AverageGradient + BS[i].arcs_in_signalling_block_section[j].gradient;
					AverageCurvature = AverageCurvature + BS[i].arcs_in_signalling_block_section[j].curvature;
					N_arcs++;
				}
			}
		}
		// Computing Average Gradient and Average Curvature Radii
		AverageGradient = AverageGradient / N_arcs;
		AverageCurvature = AverageCurvature / N_arcs;

		// Computing the braking curve with a piecewise approximation formula
		double BrakingDistance = 0;
		double N_V_Steps = 100;
		double V_Step = -1;

		if (V1 > V2) {
			V_Step = (V1 - V2) / N_V_Steps;
		}

		else {
			cout << "ERROR: Train " << this->trainDescription << " is trying to compute an average braking distance with a target speed V2 higher than the starting speed V1. Please check the reason why this is happening\n";
		}

		if (V_Step > 0) {
			for (int i = 0; i <= N_V_Steps; i++) {
				double V_Current_Interval = V2 + i * V_Step;
				double V_Previous_Interval = 0;
				if (i == 0) {
					V_Previous_Interval = V2;
				} else {
					V_Previous_Interval = V2 + (i - 1) * V_Step;
				}
				// The reconstruction of the braking curve happens backward from the lower speed to the higher speed
				double StepDeceleration = 0;
				StepDeceleration = max_train_decelaration + total_train_resistances(V_Previous_Interval, AverageGradient, AverageCurvature) / (total_train_mass * massFactor);
				BrakingDistance = BrakingDistance + (pow(V_Current_Interval, 2) - pow(V_Previous_Interval, 2)) / (2 * StepDeceleration);
			}
		}

		if (BrakingDistance <= 0) {
			cout << "Warning: For Train " << this->trainDescription << " a non positive approximated braking distance has been computed. Please check the reason why this happened\n";
		}
		return BrakingDistance;
	}

	// Function to quickly compute the average acceleration distance between two speeds
	virtual double AccelerationDistanceFastComputation(double V1, double V2, double X1, double X2, Section* BS, int N_BS) { // V1 and X1 are the current train speed and position while X2 and V2 are the target position and speed, signalling_block_sections and N_BS are the list of Block Sections and the number of Block Sections along the train route
		double AverageGradient = 0;
		double AverageCurvature = 0;
		int N_arcs = 0;

		for (int i = 0; i < N_BS; i++) {

			for (int j = 0; j < BS[i].total_arcs; j++) {

				if ((BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000 > X1) && (BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000 <= X2)) {

					AverageGradient = AverageGradient + BS[i].arcs_in_signalling_block_section[j].gradient;

					AverageCurvature = AverageCurvature + BS[i].arcs_in_signalling_block_section[j].curvature;

					N_arcs++;
				}
			}
		}
		// Computing Average Gradient and Average Curvature Radii
		AverageGradient = AverageGradient / N_arcs;
		AverageCurvature = AverageCurvature / N_arcs;

		// Computing the acceleration curve of the train

		double AccelerationDistance = 0;
		double AccelerationTime = 0;

		// Splitting the speed difference into several intervals
		double V_Step = -1;
		int N_V_Steps = 200;
		if (V2 > V1) {
			V_Step = (V2 - V1) / N_V_Steps;
		} else {
			cout << "ERROR: Train " << this->trainDescription << " is trying to compute an average acceleration distance with a target speed V2 lower than the starting speed V1. A braking curve should be computed in this case. Please check the reason why this is happening\n";
		}

		if (V_Step > 0) {
			for (int i = 0; i <= N_V_Steps; i++) {
				double V_Current_Interval = V1 + i * V_Step;
				double V_Previous_Interval = 0;
				if (i == 0) {
					V_Previous_Interval = V1;
				} else {
					V_Previous_Interval = V1 + (i - 1) * V_Step;
				}

				// Computation of piecewise acceleration distance
				double StepAcceleration = 0;
				StepAcceleration = (this->tractiveEffort(V_Previous_Interval) - this->total_train_resistances(V_Previous_Interval, AverageGradient, AverageCurvature)) / (total_train_mass * massFactor);
				AccelerationDistance = AccelerationDistance + (pow(V_Current_Interval, 2) - pow(V_Previous_Interval, 2)) / (2 * StepAcceleration);
				AccelerationTime = AccelerationTime + (V_Current_Interval - V_Previous_Interval) / StepAcceleration;
			}
		}

		if (AccelerationDistance <= 0) {
			// if the Acceleration Distance was negative that probably meant that the average Gradient was too high for the train to accelerate and the locomotive was not powerful enough
			// In this case we create a rescue acceleration distance that considers the gradient flat, i.e. =0
			double RescueAccelerationDistance = 0;
			double RescueAccelerationTime = 0;

			if (V_Step > 0) {
				for (int i = 0; i <= N_V_Steps; i++) {
					double V_Current_Interval = V1 + i * V_Step;
					double V_Previous_Interval = 0;
					if (i == 0) {
						V_Previous_Interval = V1;
					} else {
						V_Previous_Interval = V1 + (i - 1) * V_Step;
					}

					// Computation of piecewise acceleration distance
					double StepAccelerationOnFlatGround = 0;
					StepAccelerationOnFlatGround = (this->tractiveEffort(V_Previous_Interval) - this->total_train_resistances(V_Previous_Interval, 0, AverageCurvature)) / (total_train_mass * massFactor);
					RescueAccelerationDistance = RescueAccelerationDistance + (pow(V_Current_Interval, 2) - pow(V_Previous_Interval, 2)) / (2 * StepAccelerationOnFlatGround);
					RescueAccelerationTime = RescueAccelerationTime + (V_Current_Interval - V_Previous_Interval) / StepAccelerationOnFlatGround;
				}
			}

			if (RescueAccelerationDistance > 0) {
				AccelerationDistance = RescueAccelerationDistance;
			} else { // In case also the rescue acceleration distance is negative that means that the train cannot accelerate on this section because resistances are larger than tractive effort
				// At this point use the cruising mode for the train and the acceleration distance is 0 therefore

				cout << "Train " << this->trainDescription << " has air resistances larger than tractive effort on this section with V1: " << V1 << " V2: " << V2 << " X1: " << X1 << " X2: " << X2 << ". The Cruising mode will be applied since trains cannot accelerate\n";
				AccelerationDistance = 0;

				// Then if it is because the speed difference is very small and lower than 0.1 m/s
				/*if ((V2 - V1) < 0.1) {   //then impose that the acceleration distance is 0, given that train 1 and train 2 travel almost at the same speed with an approximation of 0.1 m/s
					AccelerationDistance = 0;
				}
				else { //In case the speed difference is larger than 0.1 m/s and the rescueacceleration distance is negative then throw a Warning message
					cout << "Warning: For Train " << this->trainDescription << " a non positive approximated acceleration distance has been computed with V1: " << V1 << " V2: " << V2 << " X1: " << X1 << " X2: " << X2 << ". Please check the reason why this happened\n";
					printf_s("%.11f", V1); cout << " "; printf_s("%.11f", V2);
					cout << "\n The average gradient is: "; printf_s("%.7f", AverageGradient); cout << "\n";
					cout << "Probably the gradient is too steep for the power of the train locomotive to accelerate\n";
				}*/
			}
		}

		return AccelerationDistance;
	}

	// Function to quickly compute the average acceleration distance between two speeds
	virtual double AccelerationTimeFollowingMode(double V1, double V2, double X1, double X2, Section* BS, int N_BS) { // V1 and X1 are the current train speed and position while X2 and V2 are the target position and speed, signalling_block_sections and N_BS are the list of Block Sections and the number of Block Sections along the train route
		double AverageGradient = 0;
		double AverageCurvature = 0;
		int N_arcs = 0;

		for (int i = 0; i < N_BS; i++) {

			for (int j = 0; j < BS[i].total_arcs; j++) {

				if ((BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000 > X1) && (BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000 <= X2)) {

					AverageGradient = AverageGradient + BS[i].arcs_in_signalling_block_section[j].gradient;

					AverageCurvature = AverageCurvature + BS[i].arcs_in_signalling_block_section[j].curvature;

					N_arcs++;
				}
			}
		}
		// Computing Average Gradient and Average Curvature Radii
		AverageGradient = AverageGradient / N_arcs;
		AverageCurvature = AverageCurvature / N_arcs;

		// Computing the acceleration curve of the train

		double AccelerationDistance = 0;
		double AccelerationTime = 0;

		// Splitting the speed difference into several intervals
		double V_Step = -1;
		int N_V_Steps = 200;
		if (V2 > V1) {
			V_Step = (V2 - V1) / N_V_Steps;
		} else {
			cout << "ERROR: Train " << this->trainDescription << " is trying to compute an average acceleration distance with a target speed V2 lower than the starting speed V1. A braking curve should be computed in this case. Please check the reason why this is happening\n";
		}

		if (V_Step > 0) {
			for (int i = 0; i <= N_V_Steps; i++) {
				double V_Current_Interval = V1 + i * V_Step;
				double V_Previous_Interval = 0;
				if (i == 0) {
					V_Previous_Interval = V1;
				} else {
					V_Previous_Interval = V1 + (i - 1) * V_Step;
				}

				// Computation of piecewise acceleration distance
				double StepAcceleration = 0;
				StepAcceleration = (this->tractiveEffort(V_Previous_Interval) - this->total_train_resistances(V_Previous_Interval, AverageGradient, AverageCurvature)) / (total_train_mass * massFactor);
				AccelerationDistance = AccelerationDistance + (pow(V_Current_Interval, 2) - pow(V_Previous_Interval, 2)) / (2 * StepAcceleration);
				AccelerationTime = AccelerationTime + (V_Current_Interval - V_Previous_Interval) / StepAcceleration;
			}
		}

		if (AccelerationTime <= 0) {
			// if the Acceleration Distance was negative that probably meant that the average Gradient was too high for the train to accelerate and the locomotive was not powerful enough
			// In this case we create a rescue acceleration distance that considers the gradient flat, i.e. =0
			double RescueAccelerationDistance = 0;
			double RescueAccelerationTime = 0;

			if (V_Step > 0) {
				for (int i = 0; i <= N_V_Steps; i++) {
					double V_Current_Interval = V1 + i * V_Step;
					double V_Previous_Interval = 0;
					if (i == 0) {
						V_Previous_Interval = V1;
					} else {
						V_Previous_Interval = V1 + (i - 1) * V_Step;
					}

					// Computation of piecewise acceleration distance
					double StepAccelerationOnFlatGround = 0;
					StepAccelerationOnFlatGround = (this->tractiveEffort(V_Previous_Interval) - this->total_train_resistances(V_Previous_Interval, 0, AverageCurvature)) / (total_train_mass * massFactor);
					RescueAccelerationDistance = RescueAccelerationDistance + (pow(V_Current_Interval, 2) - pow(V_Previous_Interval, 2)) / (2 * StepAccelerationOnFlatGround);
					RescueAccelerationTime = RescueAccelerationTime + (V_Current_Interval - V_Previous_Interval) / StepAccelerationOnFlatGround;
				}
			}

			if (RescueAccelerationTime > 0) {
				AccelerationTime = RescueAccelerationTime;
			} else { // In case also the rescue acceleration distance is negative that means that the train cannot accelerate on this section because resistances are larger than tractive effort
					 // At this point use the cruising mode for the train and the acceleration distance is 0 therefore

				cout << "Train " << this->trainDescription << " has air resistances larger than tractive effort on this section with V1: " << V1 << " V2: " << V2 << " X1: " << X1 << " X2: " << X2 << ". The Cruising mode will be applied since trains cannot accelerate\n";
				AccelerationTime = -99999;

				// Then if it is because the speed difference is very small and lower than 0.1 m/s
				/*if ((V2 - V1) < 0.1) {   //then impose that the acceleration distance is 0, given that train 1 and train 2 travel almost at the same speed with an approximation of 0.1 m/s
				AccelerationDistance = 0;
				}
				else { //In case the speed difference is larger than 0.1 m/s and the rescueacceleration distance is negative then throw a Warning message
				cout << "Warning: For Train " << this->trainDescription << " a non positive approximated acceleration distance has been computed with V1: " << V1 << " V2: " << V2 << " X1: " << X1 << " X2: " << X2 << ". Please check the reason why this happened\n";
				printf_s("%.11f", V1); cout << " "; printf_s("%.11f", V2);
				cout << "\n The average gradient is: "; printf_s("%.7f", AverageGradient); cout << "\n";
				cout << "Probably the gradient is too steep for the power of the train locomotive to accelerate\n";
				}*/
			}
		}

		return AccelerationTime;
	}

	// Calculation of Dynamic Braking Distance with inverse integration of the real Braking Curve considering also Block Sections
	virtual double BrakDist_Block(double V1, double V2, double X0, Section* BS, int Blocks) {
		double BD;
		double BX; // Braking Distance
		int t;
		int BlockIdx;
		double U[2000];
		double X[2000]; // Definition of Temporary variables Vector U=Speed, X=Abscissa
		double DX;		// Final Abscissa deriving from inverse integration of the Braking Curve
		U[0] = V2;
		X[0] = X0 - 0.002; // The train stops at two metres from the objective point
		if (V1 > V2) {
			for (t = 1; t < 2000; t++) {
				BlockIdx = 0;
				for (int h = 0; h < Blocks; h++) {
					if ((X[t - 1] < BS[h].end_node.X * 1000) && (X[t - 1] >= BS[h].start_node.X * 1000)) { // Selection of the right Block Section
						BlockIdx = h;
						break;
					}
				}
				{
				const Section& Block = BS[BlockIdx];
				for (int j = 0; j < Block.total_arcs; j++) {
					if ((X[t - 1] < Block.arcs_in_signalling_block_section[j].endNode.X * 1000) && (X[t - 1] >= Block.arcs_in_signalling_block_section[j].startNode.X * 1000)) { // Selection of the right Arc of the Block Section
						const Arc& Ab = Block.arcs_in_signalling_block_section[j];
						// Inverse integration of the real Braking Curve
						U[t] = U[t - 1] - (-total_train_mass * massFactor * max_train_decelaration - total_train_resistances(U[t - 1], Ab.gradient, Ab.curvature)) * timestep / (total_train_mass * massFactor);
						X[t] = X[t - 1] + (total_train_mass * massFactor * U[t - 1] * (U[t] - U[t - 1])) / (-total_train_mass * massFactor * max_train_decelaration - total_train_resistances(U[t - 1], Ab.gradient, Ab.curvature));
						break;
					}
				}
				}
			}
			for (t = 0; t < 2000; t++) {
				// BUG - does not use interpolation and can lead to incorrect critical braking point
				/*if (U[t] >= V1) {
					DX = X[t]; break;
				}*/

				// bug fix using linear interpolation
				if (U[t] >= V1) {
					DX = X[t - 1] + (V1 - U[t - 1]) * (X[t] - X[t - 1]) / (U[t] - U[t - 1]);
					break;
				}
			}
			BD = X[0] - DX + (max_train_decelaration / (2 * Jerk)) * (V1) + V1 * timestep;
			BX = DX;
		} else {
			BD = 0;
			BX = X0;
		}
		return BX;
	}

	// Calculation of Dinamic Braking Distance with direct integration of the real Braking Curve considering also Block Sections
	virtual double BrakDist_Block2(double V1, double V2, double X1, double X0) {
		double BD;
		double BX; // Braking Distance, Starting Braking Abscissa
		int t;
		Section Block;
		Arc Ab;
		double U[2000];
		double X[2000]; // Definition of Temporary variables Vector U=Speed, X=Abscissa
		double DX;		// Final Abscissa deriving from the integration of the Braking Curve
		U[0] = V1;
		X[0] = X1; // The train stops at the objective point
		if (V1 > V2) {
			for (t = 1; t < 2000; t++) {
				for (int h = 0; h < Blocks; h++) {
					if ((X[t - 1] < signalling_block_sections[h].end_node.X * 1000) && (X[t - 1] >= signalling_block_sections[h].start_node.X * 1000)) { // Selection of the right Block Section
						Block = signalling_block_sections[h];
					}
				}
				for (int j = 0; j < Block.total_arcs; j++) {
					if ((X[t - 1] < Block.arcs_in_signalling_block_section[j].endNode.X * 1000) && (X[t - 1] >= Block.arcs_in_signalling_block_section[j].startNode.X * 1000)) { // Selection of the right Arc of the Block Section
						Ab = Block.arcs_in_signalling_block_section[j];
					}
				}
				// Direct integration of the real Braking Curve
				U[t] = U[t - 1] + (-brakingEffort((t - 1) * timestep) - gradient_resistances(Ab.gradient) - curvature_resistances(Ab.curvature)) * timestep / (total_train_mass * massFactor);
				X[t] = X[t - 1] + (total_train_mass * massFactor * U[t - 1] * (U[t] - U[t - 1])) / (-brakingEffort((t - 1) * timestep) - gradient_resistances(Ab.gradient) - curvature_resistances(Ab.curvature));
			}
			for (t = 0; t < 2000; t++) {
				if (U[t] <= V2) {
					DX = X[t];
					break;
				}
			}
			BD = DX - X1;
			BX = X0 - BD;
		} else {
			BD = 0;
			BX = X0;
		}
		return BX;
	}

	// This Function simulate the capacity of the train driver to react to the first most severe service limit (i.e. it calculates the braking distance and the braking point for satisfying the most severe running operation limit)
	double BraKDistComp(double S, double V, Section* BS, int Blocks) {
		int N_BrakP;
		int ArcPos = 0;
		int BlockPos = 0;
		double BrakComp;
		double MinBX = 99999999999; // Number of Braking points in the Block Section occupied by the train, Index of the Arc of the signalling_block_sections occupied by the train, Index of the signalling_block_sections occupied by the train, Matrix of all Brak Distance of the Braking point of the Block Section occupied by the train, Minimum Braking Abscissa, Braking Abscissa of the block section arcs which follows the signalling_block_sections occupied by the train
		double Xob, Vob;			// This variables represents: Xob is the vector of objective braking points abscissa;  Vob is the vector of objective braking points speed;(These are for the braking points belonging to the signalling_block_sections occupied by the Train)

		for (int h = 0; h < Blocks; h++) {
			if ((S < BS[h].end_node.X * 1000) && (S >= BS[h].start_node.X * 1000)) {
				BlockPos = h;
				break;
			}
		}
		for (int j = 0; j < BS[BlockPos].total_arcs; j++) {
			if ((S < BS[BlockPos].arcs_in_signalling_block_section[j].endNode.X * 1000) && (S >= BS[BlockPos].arcs_in_signalling_block_section[j].startNode.X * 1000)) {
				ArcPos = j;
				break;
			}
		}
		N_BrakP = BS[BlockPos].total_arcs - ArcPos; // Initializing dimensions of Braking points vectors

		// Calculating the total number of breaking points to find the minimum breaking distance (all the arcs of the block section which follows the occupied one are also considered)
		int Cons_Block = 2;
		if (BlockPos + Cons_Block > Blocks - 1) {
			Cons_Block = Blocks - BlockPos - 1;
		}
		int N_Brak_Dist = N_BrakP;
		for (int z = 0; z < Cons_Block; z++) {
			N_Brak_Dist = N_Brak_Dist + BS[z + BlockPos + 1].total_arcs;
		} // if the train is on the last block section it cannot calculate braking points on the following sections (just because they don't exist)

		if (Cons_Block == 2) {
#pragma omp parallel private(BrakComp, Xob, Vob)
			{
#pragma omp for
				for (int k = 0; k < N_Brak_Dist; k++) {
					if (k <= N_BrakP - 1) {
						bool TrainMustStop = Train_Must_Stop_For_OL_Order(BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.respectOrder, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.indexOrderList); // Check if there is an order to respect
						if (TrainMustStop == 1) {
							BrakComp = BrakDist_Block(V, 0, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000, BS, Blocks);
							Xob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000;
							Vob = 0;
						} else {
							BrakComp = BrakDist_Block(V, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].speedInBraking, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000, BS, Blocks);
							Xob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000;
							Vob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].speedInBraking;
						}
					} else if ((k > N_BrakP - 1) && (k <= N_BrakP + BS[BlockPos + 1].total_arcs - 1)) {
						int x = k - N_BrakP;
						bool TrainMustStop = Train_Must_Stop_For_OL_Order(BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.respectOrder, BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.indexOrderList);
						if (TrainMustStop == 1) {
							BrakComp = BrakDist_Block(V, 0, BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.X * 1000, BS, Blocks);
							Xob = BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.X * 1000;
							Vob = 0;
						} else {
							BrakComp = BrakDist_Block(V, BS[BlockPos + 1].arcs_in_signalling_block_section[x].speedInBraking, BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.X * 1000, BS, Blocks);
							Xob = BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.X * 1000;
							Vob = BS[BlockPos + 1].arcs_in_signalling_block_section[x].speedInBraking;
						}
					} else if (k > N_BrakP + BS[BlockPos + 1].total_arcs - 1) {
						int x = k - N_BrakP - BS[BlockPos + 1].total_arcs;
						bool TrainMustStop = Train_Must_Stop_For_OL_Order(BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.respectOrder, BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.indexOrderList);
						if (TrainMustStop == 1) {
							BrakComp = BrakDist_Block(V, 0, BS[BlockPos + 2].arcs_in_signalling_block_section[x].endNode.X * 1000, BS, Blocks);
							Xob = BS[BlockPos + 2].arcs_in_signalling_block_section[x].endNode.X * 1000;
							Vob = 0;
						} else {
							BrakComp = BrakDist_Block(V, BS[BlockPos + 2].arcs_in_signalling_block_section[x].speedInBraking, BS[BlockPos + 2].arcs_in_signalling_block_section[x].endNode.X * 1000, BS, Blocks);
							Xob = BS[BlockPos + 2].arcs_in_signalling_block_section[x].endNode.X * 1000;
							Vob = BS[BlockPos + 2].arcs_in_signalling_block_section[x].speedInBraking;
						}
					}

					if ((BrakComp <= MinBX) && (Xob > S)) {
#pragma omp critical
						MinBX = BrakComp;
						Xobmin = Xob;
						Vobmin = Vob;
					}
				}
			}
		}

		else if (Cons_Block == 1) {
#pragma omp parallel private(BrakComp, Xob, Vob)
			{
#pragma omp for
				for (int k = 0; k < N_Brak_Dist; k++) {
					if (k <= N_BrakP - 1) {
						bool TrainMustStop = Train_Must_Stop_For_OL_Order(BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.respectOrder, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.indexOrderList); // Check if there is an order to respect
						if (TrainMustStop == 1) {
							BrakComp = BrakDist_Block(V, 0, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000, BS, Blocks);
							Xob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000;
							Vob = 0;
						} else {
							BrakComp = BrakDist_Block(V, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].speedInBraking, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000, BS, Blocks);
							Xob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000;
							Vob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].speedInBraking;
						}
					} else {
						int x = k - N_BrakP;
						bool TrainMustStop = Train_Must_Stop_For_OL_Order(BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.respectOrder, BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.indexOrderList);
						if (TrainMustStop == 1) {
							BrakComp = BrakDist_Block(V, 0, BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.X * 1000, BS, Blocks);
							Xob = BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.X * 1000;
							Vob = 0;
						} else {
							BrakComp = BrakDist_Block(V, BS[BlockPos + 1].arcs_in_signalling_block_section[x].speedInBraking, BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.X * 1000, BS, Blocks);
							Xob = BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.X * 1000;
							Vob = BS[BlockPos + 1].arcs_in_signalling_block_section[x].speedInBraking;
						}
					}

					if ((BrakComp <= MinBX) && (Xob > S)) {
#pragma omp critical
						MinBX = BrakComp;
						Xobmin = Xob;
						Vobmin = Vob;
					}
				}
			}
		} else if (Cons_Block == 0) {
#pragma omp parallel private(BrakComp, Xob, Vob)
			{
#pragma omp for
				for (int k = 0; k < N_Brak_Dist; k++) {
					bool TrainMustStop = Train_Must_Stop_For_OL_Order(BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.respectOrder, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.indexOrderList); // Check if there is an order to respect
					if (TrainMustStop == 1) {
						BrakComp = BrakDist_Block(V, 0, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000, BS, Blocks);
						Xob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000;
						Vob = 0;
					} else {
						BrakComp = BrakDist_Block(V, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].speedInBraking, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000, BS, Blocks);
						Xob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000;
						Vob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].speedInBraking;
					}

					if ((BrakComp <= MinBX) && (Xob > S)) {
#pragma omp critical
						MinBX = BrakComp;
						Xobmin = Xob;
						Vobmin = Vob;
					}
				}
			}
		}
		return MinBX;
	}

	// This Function simulate the capacity of the train driver to react to the first most severe service limit (i.e. it calculates the braking distance and the braking point for satisfying the most severe running operation limit)
	double BraKDistCompImproved(double S, double V, int PreviousInstant, Section* BS, int Blocks) {
		int N_BrakP;
		int ArcPos = 0;
		int BlockPos = 0;
		double BrakComp;
		double MinBX = 99999999999; // Number of Braking points in the Block Section occupied by the train, Index of the Arc of the signalling_block_sections occupied by the train, Index of the signalling_block_sections occupied by the train, Matrix of all Brak Distance of the Braking point of the Block Section occupied by the train, Minimum Braking Abscissa, Braking Abscissa of the block section arcs which follows the signalling_block_sections occupied by the train
		bool SatisfiesCondition = false;
		bool BrakeForLoopCondtionFound = false;
		double Xob, Vob; // This variables represents: Xob is the vector of objective braking points abscissa;  Vob is the vector of objective braking points speed;(These are for the braking points belonging to the signalling_block_sections occupied by the Train)

		for (int h = 0; h < Blocks; h++) {
			if ((S < BS[h].end_node.X * 1000) && (S >= BS[h].start_node.X * 1000)) {
				BlockPos = h;
				break;
			}
		}
		for (int j = 0; j < BS[BlockPos].total_arcs; j++) {
			if ((S < BS[BlockPos].arcs_in_signalling_block_section[j].endNode.X * 1000) && (S >= BS[BlockPos].arcs_in_signalling_block_section[j].startNode.X * 1000)) {
				ArcPos = j;
				break;
			}
		}
		N_BrakP = BS[BlockPos].total_arcs - ArcPos; // Initializing dimensions of Braking points vectors

		// Calculating the total number of breaking points to find the minimum breaking distance (all the arcs of the block section which follows the occupied one are also considered)
		int Cons_Block = 2;
		if (BlockPos + Cons_Block > Blocks - 1) {
			Cons_Block = Blocks - BlockPos - 1;
		}
		int N_Brak_Dist = N_BrakP;
		for (int z = 0; z < Cons_Block; z++) {
			N_Brak_Dist = N_Brak_Dist + BS[z + BlockPos + 1].total_arcs;
		} // if the train is on the last block section it cannot calculate braking points on the following sections (just because they don't exist)

		if (Cons_Block == 2) {
#pragma omp parallel private(BrakComp, Xob, Vob, SatisfiesCondition)
			{
#pragma omp for
				for (int k = 0; k < N_Brak_Dist; k++) {
					SatisfiesCondition = false;
					if (BrakeForLoopCondtionFound == 0) {
						if (k <= N_BrakP - 1) {
							bool TrainMustStop = Train_Must_Stop_For_OL_Order(BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.respectOrder, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.indexOrderList); // Check if there is an order to respect
							if (TrainMustStop == 1) {
								BrakComp = BrakDist_Block(V, 0, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000, BS, Blocks);
								Xob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000;
								Vob = 0;
							} else {
								BrakComp = BrakDist_Block(V, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].speedInBraking, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000, BS, Blocks);
								Xob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000;
								Vob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].speedInBraking;
							}
							// If the GradientException is true and there is still the same target point as in the previous instant then
							if ((this->GradientExceptionInBraking == 1) && (Xob == this->Xob[PreviousInstant]) && (Vob == this->Vob[PreviousInstant])) {

								SatisfiesCondition = true; // break the for loop by putting satisfying condition to true;
							}
						} else if ((k > N_BrakP - 1) && (k <= N_BrakP + BS[BlockPos + 1].total_arcs - 1)) {
							int x = k - N_BrakP;
							bool TrainMustStop = Train_Must_Stop_For_OL_Order(BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.respectOrder, BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.indexOrderList);
							if (TrainMustStop == 1) {
								BrakComp = BrakDist_Block(V, 0, BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.X * 1000, BS, Blocks);
								Xob = BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.X * 1000;
								Vob = 0;
							} else {
								BrakComp = BrakDist_Block(V, BS[BlockPos + 1].arcs_in_signalling_block_section[x].speedInBraking, BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.X * 1000, BS, Blocks);
								Xob = BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.X * 1000;
								Vob = BS[BlockPos + 1].arcs_in_signalling_block_section[x].speedInBraking;
							}
							// If the GradientException is true and there is still the same target point as in the previous instant then
							if ((this->GradientExceptionInBraking == 1) && (Xob == this->Xob[PreviousInstant]) && (Vob == this->Vob[PreviousInstant])) {

								SatisfiesCondition = true; // break the for loop by putting satisfying condition to true;
							}

						} else if (k > N_BrakP + BS[BlockPos + 1].total_arcs - 1) {
							int x = k - N_BrakP - BS[BlockPos + 1].total_arcs;
							bool TrainMustStop = Train_Must_Stop_For_OL_Order(BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.respectOrder, BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.indexOrderList);
							if (TrainMustStop == 1) {
								BrakComp = BrakDist_Block(V, 0, BS[BlockPos + 2].arcs_in_signalling_block_section[x].endNode.X * 1000, BS, Blocks);
								Xob = BS[BlockPos + 2].arcs_in_signalling_block_section[x].endNode.X * 1000;
								Vob = 0;
							} else {
								BrakComp = BrakDist_Block(V, BS[BlockPos + 2].arcs_in_signalling_block_section[x].speedInBraking, BS[BlockPos + 2].arcs_in_signalling_block_section[x].endNode.X * 1000, BS, Blocks);
								Xob = BS[BlockPos + 2].arcs_in_signalling_block_section[x].endNode.X * 1000;
								Vob = BS[BlockPos + 2].arcs_in_signalling_block_section[x].speedInBraking;
							}
							// If the GradientException is true and there is still the same target point as in the previous instant then
							if ((this->GradientExceptionInBraking == 1) && (Xob == this->Xob[PreviousInstant]) && (Vob == this->Vob[PreviousInstant])) {
								SatisfiesCondition = true; // break the for loop by putting satisfying condition to true;
							}
						}

#pragma omp critical
						{
							if (SatisfiesCondition == 1) {
								MinBX = this->Sbrak[brakingPoint + counter - 1]; // return the next braking point as braking distance
								Xobmin = Xob;							// the target point does not change
								Vobmin = Vob;
								BrakeForLoopCondtionFound = true;
							} else {
								if ((BrakComp < MinBX) && (Xob > S)) {
									MinBX = BrakComp;
									Xobmin = Xob;
									Vobmin = Vob;
								}
							}
						}
					}
				}
			}
		}

		else if (Cons_Block == 1) {
#pragma omp parallel private(BrakComp, Xob, Vob, SatisfiesCondition)
			{
#pragma omp for
				for (int k = 0; k < N_Brak_Dist; k++) {
					SatisfiesCondition = false;
					if (BrakeForLoopCondtionFound == 0) {
						if (k <= N_BrakP - 1) {
							bool TrainMustStop = Train_Must_Stop_For_OL_Order(BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.respectOrder, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.indexOrderList); // Check if there is an order to respect
							if (TrainMustStop == 1) {
								BrakComp = BrakDist_Block(V, 0, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000, BS, Blocks);
								Xob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000;
								Vob = 0;
							} else {
								BrakComp = BrakDist_Block(V, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].speedInBraking, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000, BS, Blocks);
								Xob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000;
								Vob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].speedInBraking;
							}
							// If the GradientException is true and there is still the same target point as in the previous instant then
							if ((this->GradientExceptionInBraking == 1) && (Xob == this->Xob[PreviousInstant]) && (Vob == this->Vob[PreviousInstant])) {
								SatisfiesCondition = true; // break the for loop by putting satisfying condition to true;
							}

						} else {
							int x = k - N_BrakP;
							bool TrainMustStop = Train_Must_Stop_For_OL_Order(BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.respectOrder, BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.indexOrderList);
							if (TrainMustStop == 1) {
								BrakComp = BrakDist_Block(V, 0, BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.X * 1000, BS, Blocks);
								Xob = BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.X * 1000;
								Vob = 0;
							} else {
								BrakComp = BrakDist_Block(V, BS[BlockPos + 1].arcs_in_signalling_block_section[x].speedInBraking, BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.X * 1000, BS, Blocks);
								Xob = BS[BlockPos + 1].arcs_in_signalling_block_section[x].endNode.X * 1000;
								Vob = BS[BlockPos + 1].arcs_in_signalling_block_section[x].speedInBraking;
							}
							// If the GradientException is true and there is still the same target point as in the previous instant then
							if ((this->GradientExceptionInBraking == 1) && (Xob == this->Xob[PreviousInstant]) && (Vob == this->Vob[PreviousInstant])) {
								SatisfiesCondition = true; // break the for loop by putting satisfying condition to true;
							}
						}
#pragma omp critical
						{
							if (SatisfiesCondition == 1) {
								MinBX = this->Sbrak[brakingPoint + counter - 1]; // return the next braking point as braking distance
								Xobmin = Xob;							// the target point does not change
								Vobmin = Vob;
								BrakeForLoopCondtionFound = true;
							} else {
								if ((BrakComp < MinBX) && (Xob > S)) {
									MinBX = BrakComp;
									Xobmin = Xob;
									Vobmin = Vob;
								}
							}
						}
					}
				}
			}
		}

		else if (Cons_Block == 0) {
#pragma omp parallel private(BrakComp, Xob, Vob, SatisfiesCondition)
			{
#pragma omp for
				for (int k = 0; k < N_Brak_Dist; k++) {
					SatisfiesCondition = false;
					if (BrakeForLoopCondtionFound == 0) {
						bool TrainMustStop = Train_Must_Stop_For_OL_Order(BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.respectOrder, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.indexOrderList); // Check if there is an order to respect
						if (TrainMustStop == 1) {
							BrakComp = BrakDist_Block(V, 0, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000, BS, Blocks);
							Xob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000;
							Vob = 0;
						} else {
							BrakComp = BrakDist_Block(V, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].speedInBraking, BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000, BS, Blocks);
							Xob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].endNode.X * 1000;
							Vob = BS[BlockPos].arcs_in_signalling_block_section[k + ArcPos].speedInBraking;
						}
						// If the GradientException is true and there is still the same target point as in the previous instant then
						if ((this->GradientExceptionInBraking == 1) && (Xob == this->Xob[PreviousInstant]) && (Vob == this->Vob[PreviousInstant])) {
							SatisfiesCondition = true; // break the for loop by putting satisfying condition to true;
						}
#pragma omp critical
						{
							if (SatisfiesCondition == 1) {
								MinBX = this->Sbrak[brakingPoint + counter - 1]; // return the next braking point as braking distance
								Xobmin = Xob;							// the target point does not change
								Vobmin = Vob;
								BrakeForLoopCondtionFound = true;
							} else {
								if ((BrakComp < MinBX) && (Xob > S)) {
									MinBX = BrakComp;
									Xobmin = Xob;
									Vobmin = Vob;
								}
							}
						}
					}
				}
			}
		}
		return MinBX;
	}

	// This Function simulate the capacity of the train driver to react to the first most severe service limit (i.e. it calculates the braking distance and the braking point for satisfying the most severe running operation limit)
	double EuropeanVitalComputerWithLists(double S, double V, int PreviousInstant, Section* BS, int Blocks) {
		Section Block;
		Arc As;
		int N_BrakP;
		int ArcPos = 0;
		int BlockPos = 0;
		double MinBX = 99999999999; // Number of Braking points in the Block Section occupied by the train, Index of the Arc of the signalling_block_sections occupied by the train, Index of the signalling_block_sections occupied by the train, Matrix of all Brak Distance of the Braking point of the Block Section occupied by the train, Minimum Braking Abscissa, Braking Abscissa of the block section arcs which follows the signalling_block_sections occupied by the train
		Node ClosestETCSBrakingPoint;
		ClosestETCSBrakingPoint.X = 9999999999;			 // This is the closest ETCS braking point.
		int SignallingLevelClosestETCSBrakingPoint = -1; // This is the signalling level of the closest ETCS Braking Point

		Node ETCSBrakingPoint; // Node to collect all the information of the ETCS BrakingPoints

		list<Node> BrakingPoints; // This is the list of the nodes constituting braking points

		for (int h = 0; h < Blocks; h++) {
			if ((S < BS[h].end_node.X * 1000) && (S >= BS[h].start_node.X * 1000)) {
				Block = BS[h];
				BlockPos = h;
				break;
			}
		}
		for (int j = 0; j < Block.total_arcs; j++) {
			if ((S < Block.arcs_in_signalling_block_section[j].endNode.X * 1000) && (S >= Block.arcs_in_signalling_block_section[j].startNode.X * 1000)) {
				As = Block.arcs_in_signalling_block_section[j];
				ArcPos = j;
				break;
			}
		}
		N_BrakP = Block.total_arcs - ArcPos; // Initializing dimensions of Braking points vectors

		// Putting in the list of braking points the nodes of the Arc where the train currently is
		for (int p = ArcPos; p < BS[BlockPos].total_arcs; p++) {
			// Setting the current Arc speedInBraking to the final Node of Arc
			BS[BlockPos].arcs_in_signalling_block_section[p].endNode.arcSpeedLimit = BS[BlockPos].arcs_in_signalling_block_section[p].speedInBraking;
			BrakingPoints.push_back(BS[BlockPos].arcs_in_signalling_block_section[p].endNode);
		}

		// Calculating the total number of breaking points to find the minimum breaking distance (all the arcs of the block section which follows the occupied one are also considered)
		int Cons_Block = 8;
		if (BlockPos + Cons_Block > Blocks - 1) {
			Cons_Block = Blocks - BlockPos - 1;
		}

		// Putting in list of Braking Points all the nodes belonging to the other block sections
		for (int z = 0; z < Cons_Block; z++) {
			for (int p = 0; p < BS[z + BlockPos + 1].total_arcs; p++) {
				BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].endNode.arcSpeedLimit = BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].speedInBraking;
				BrakingPoints.push_back(BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].endNode);
			}
		}

		// Identification of the closest EoA where the train needs to brake at (i.e. the EoA for which the train needs to brake the earliest to meet it)
		for (int z = 0; z <= Cons_Block; z++) {
			if ((BS[z + BlockPos].SignallingLevel == 3) || (BS[z + BlockPos].SignallingLevel == 4)) {
				if (ETCS_MA.size() > 0) {
					for (list<MovementAuthority>::iterator it = ETCS_MA.begin(); it != ETCS_MA.end(); it++) {
						if ((it->BSID == BS[z + BlockPos].ID) && (it->TrainInfo.trainDescription != trainDescription)) {
							if (train_route[indexOfRoute].reversed_direction == 1) {
								ETCSBrakingPoint.X = (BS[z + BlockPos].GeoXBegNode - it->AbsPosEoA) + BS[z + BlockPos].start_node.X * 1000;
							} else {
								ETCSBrakingPoint.X = it->AbsPosEoA;
							}
							if ((ETCSBrakingPoint.X > S) && (ETCSBrakingPoint.X < ClosestETCSBrakingPoint.X)) {
								ClosestETCSBrakingPoint = ETCSBrakingPoint;
								ClosestETCSBrakingPoint.isSignalled = true; // putting isSignalled to true, gives a way to the Braking point to be recognised as an End of Authority
								SignallingLevelClosestETCSBrakingPoint = BS[z + BlockPos].SignallingLevel;
								this->Last_Received_MA = *it;
								Last_Received_MA.RelativePosEoA = ETCSBrakingPoint.X; // Setting the Relative position of the EoA on the route of the train
							}
						}
					}
				}
			}
		}
		// if there is a Closest ETCS Braking point that has been assigned, then add it to the list of braking points
		if (ClosestETCSBrakingPoint.X != 9999999999) {
			BrakingPoints.push_back(ClosestETCSBrakingPoint);
		}

		// Identifying the point with the minimum braking distance
		if (BrakingPoints.empty() != 1) {
			for (list<Node>::iterator n = BrakingPoints.begin(); n != BrakingPoints.end(); n++) {
				double BrakeDistToNode = -1;
				double Xob, Vob;
				bool TrainMustStop = Train_Must_Stop_For_OL_Order(n->respectOrder, n->indexOrderList); // Check if there is an order to respect
				// if there is an order to be respected at a given junction/station then compute the braking distance for the train to stop there
				if (TrainMustStop == 1) {
					BrakeDistToNode = BrakDist_Block(V, 0, n->X * 1000, BS, Blocks);
					Xob = n->X * 1000;
					Vob = 0;
				}
				// if instead there is no order to be respected compute the Braking distance for the braking points
				else {
					// if the braking Node is an ETCS End Of Authority
					if (n->isSignalled == 1) {
						if (SignallingLevelClosestETCSBrakingPoint == 3) { // if the MA refers to an ETCS Level 3 section then the train shall stop at the received point
							BrakeDistToNode = BrakDist_Block(V, 0, n->X, BS, Blocks);
							Xob = n->X;
							Vob = 0;
						} else if (SignallingLevelClosestETCSBrakingPoint == 4) {
							// the train needs to stop if the MA refers to the edge of an occupied switch or to the end of a train going in opposite direction
							// Maybe this bit below needs to be upgraded with an information about the next section of the train and the status of the infrastructure element, since if the trains have the same route then this train does not need to stop at the diverging switch
							if ((this->Last_Received_MA.type == "DivergingSwitchOccupied") || ((this->Last_Received_MA.type == "TrainEnd") && (this->Last_Received_MA.ReversedDirection != train_route[this->indexOfRoute].reversed_direction))) {
								BrakeDistToNode = BrakDist_Block(V, 0, n->X, BS, Blocks);
								Xob = n->X;
								Vob = 0;
							} else if ((this->Last_Received_MA.type == "TrainEnd") && (this->Last_Received_MA.ReversedDirection == train_route[this->indexOfRoute].reversed_direction)) {
								BrakeDistToNode = BrakDist_Block(V, Last_Received_MA.TrainInfo.TrainSpeed, n->X, BS, Blocks);
								Xob = n->X;
								Vob = Last_Received_MA.TrainInfo.TrainSpeed; // maybe this bit needs to be upgraded to a train following model
							}
						}
					}
					// if the Braking Point is not a MA then compute the Braking distance because of a static or signalling speed restriction
					else {
						BrakeDistToNode = BrakDist_Block(V, n->arcSpeedLimit, n->X * 1000, BS, Blocks);
						Xob = n->X * 1000;
						Vob = n->arcSpeedLimit;
					}
				}

				// If the GradientException is true and there is still the same target point as in the previous instant then
				if ((this->GradientExceptionInBraking == 1) && (Xob == this->Xob[PreviousInstant]) && (Vob == this->Vob[PreviousInstant])) {
					MinBX = this->Sbrak[brakingPoint + counter - 1]; // return the next braking point as braking distance
					Xobmin = Xob;							// the target point does not change
					Vobmin = Vob;
					break; // break the for loop over the braking points
				}

				else {
					if (BrakeDistToNode != -1) {
						if ((BrakeDistToNode < MinBX) && (Xob > S)) {
							MinBX = BrakeDistToNode;
							Xobmin = Xob;
							Vobmin = Vob;
							this->BrakingForEoA = n->isSignalled; // In case the train was braking for an ETCS Movement Authority then set BrakingForEoA of the train to true
						}
					}
				}
			}
		}

		return MinBX;
	}

	// This Function simulate the capacity of the train driver to react to the first most severe service limit (i.e. it calculates the braking distance and the braking point for satisfying the most severe running operation limit)
	double europeanVitalComputerWithListsImproved(double S, double V, int PreviousInstant, Section* BS, int Blocks) {
		int N_BrakP;
		int ArcPos = 0;
		int BlockPos = 0;
		double MinBX = 99999999999; // Number of Braking points in the Block Section occupied by the train, Index of the Arc of the signalling_block_sections occupied by the train, Index of the signalling_block_sections occupied by the train, Matrix of all Brak Distance of the Braking point of the Block Section occupied by the train, Minimum Braking Abscissa, Braking Abscissa of the block section arcs which follows the signalling_block_sections occupied by the train
		Node ClosestETCSBrakingPoint;
		ClosestETCSBrakingPoint.X = 9999999999;			 // This is the closest ETCS braking point. In this case the closestpoint is not the MA having teh closest abscissa to the current position of the train but it is the MA which requirest the train to brake the earliest to be met
		double TargetSpeedForMA = -1;					 // This is the Speed the train needs to target to meet the most restrictive MA
		double ClosestPointToBrakeForETCS = 9999999999;	 // This is the value of the closese Point to Brake For ETCS
		int SignallingLevelClosestETCSBrakingPoint = -1; // This is the signalling level of the closest ETCS Braking Point

		Node ETCSBrakingPoint; // Node to collect all the information of the ETCS BrakingPoints

		list<Node> BrakingPoints; // This is the list of the nodes constituting braking points

		for (int h = 0; h < Blocks; h++) {
			if ((S < BS[h].end_node.X * 1000) && (S >= BS[h].start_node.X * 1000)) {
				BlockPos = h;
				break;
			}
		}
		for (int j = 0; j < BS[BlockPos].total_arcs; j++) {
			if ((S < BS[BlockPos].arcs_in_signalling_block_section[j].endNode.X * 1000) && (S >= BS[BlockPos].arcs_in_signalling_block_section[j].startNode.X * 1000)) {
				ArcPos = j;
				break;
			}
		}
		N_BrakP = BS[BlockPos].total_arcs - ArcPos; // Initializing dimensions of Braking points vectors

		// Putting in the list of braking points the nodes of the Arc where the train currently is
		for (int p = ArcPos; p < BS[BlockPos].total_arcs; p++) {
			// Setting the current Arc speedInBraking to the final Node of Arc
			BS[BlockPos].arcs_in_signalling_block_section[p].endNode.arcSpeedLimit = BS[BlockPos].arcs_in_signalling_block_section[p].speedInBraking;
			BrakingPoints.push_back(BS[BlockPos].arcs_in_signalling_block_section[p].endNode);
		}

		// Calculating the total number of breaking points to find the minimum breaking distance (all the arcs of the block section which follows the occupied one are also considered)
		int Cons_Block = 8;
		if (BlockPos + Cons_Block > Blocks - 1) {
			Cons_Block = Blocks - BlockPos - 1;
		}

		// Putting in list of Braking Points all the nodes belonging to the other block sections
		for (int z = 0; z < Cons_Block; z++) {
			for (int p = 0; p < BS[z + BlockPos + 1].total_arcs; p++) {
				BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].endNode.arcSpeedLimit = BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].speedInBraking;
				BrakingPoints.push_back(BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].endNode);
			}
		}

		// Identification of the closest EoA where the train needs to brake at (i.e. the EoA for which the train needs to brake the earliest to meet it)
		for (int z = 0; z <= Cons_Block; z++) {
			if ((BS[z + BlockPos].SignallingLevel == 3) || (BS[z + BlockPos].SignallingLevel == 4)) {
				// Identifying Braking Points computed by the EVC if the train is virtually coupled to a train ahead
				if (IsTrainInFollowingMode == 1) { // if the train is in following mode
					if ((Predicted_MA_To_DecoupleAt.TrainInfo.trainDescription != "None") && (Predicted_MA_To_DecoupleAt.TrainInfo.trainDescription == LeadingTrainInFollowingMode)) {
						if (BS[z + BlockPos].ID == Predicted_MA_To_DecoupleAt.BSID) {
							Node DecouplingBrakingPoint;
							DecouplingBrakingPoint.X = Predicted_MA_To_DecoupleAt.RelativePosEoA;
							DecouplingBrakingPoint.isSignalled = true;
							DecouplingBrakingPoint.stationName = "DecoupleTrain"; // For a Decoupling Point as a convention we say that it has a station name DecoupleTrain. We did so not to increase the number of variables of the class Node
							BrakingPoints.push_back(DecouplingBrakingPoint);
						}
					}
				}
				// Now identifying braking points due to ETCS MA
				if (ETCS_MA.size() > 0) {
					for (list<MovementAuthority>::iterator it = ETCS_MA.begin(); it != ETCS_MA.end(); it++) {
						if ((it->BSID == BS[z + BlockPos].ID) && (it->TrainInfo.trainDescription != trainDescription)) {
							double AbscissaToBrakeAtForEoA = -1;	// This is the progressive the train needs to start braking at to meet the EoA
							double SpeedForEoA = -1;				// This is the speed to be met to respect the MA
							bool BrakingForEoAModified = false;		// This variable is true when we modify the BrakingPoint because of a prediction distance to allow the train to couple with the one ahead
							bool CanTrainLinkForCoupling = false;	// This variable is a local variable to state whether the train is linking to another train ahead for virtual coupling. This boolean is true only when the closest breaking point is the Train End of a train in the same direction having the next Route section in common on a Bs with Signalling Level 4
							double PredictedDistanceToCoupling = 0; // This is the Distance need by the train to Couple with the train ahead

							if (train_route[indexOfRoute].reversed_direction == 1) {
								ETCSBrakingPoint.X = (BS[z + BlockPos].GeoXBegNode - it->AbsPosEoA) + BS[z + BlockPos].start_node.X * 1000;
							} else {
								ETCSBrakingPoint.X = it->AbsPosEoA;
							}

							if (BS[z + BlockPos].SignallingLevel == 3) {
								SpeedForEoA = 0;

							} else if (BS[z + BlockPos].SignallingLevel == 4) {
								string NextBlockID;
								if (z + BlockPos == train_route[this->indexOfRoute].N_Block_Sections - 1) { // if the block is the last of the train route then the ID of the Next Block is None
									NextBlockID = "None";
								} else {
									NextBlockID = BS[z + BlockPos + 1].ID;
								}
								if (it->type == "DivergingSwitchOccupied") {
									if ((it->TrainInfo.NextSectionID != NextBlockID) || ((it->TrainInfo.CurrentSectionID != BS[z + BlockPos].ID) && (it->TrainInfo.NextSectionID == NextBlockID))) {
										SpeedForEoA = 0;
									}
								} else if (it->type == "TrainEnd") {
									// In case the MA is a Train End the Speed shall be 0 if the train ahead is going in the opposite direction
									if (it->ReversedDirection != train_route[this->indexOfRoute].reversed_direction) {
										SpeedForEoA = 0;

									}
									// Otherwise
									else {
										if ((it->TrainInfo.NextSectionID == NextBlockID) || (it->TrainInfo.NextSectionID == "None")) { // when the trains have the same next block section or the leading train is approahing the end of its route
											SpeedForEoA = it->TrainInfo.TrainSpeed;													   // the speed is the one of the train ahead if the trains are following the same direction and ahve at least one more block section in common

											if ((this->IsTrainInFollowingMode == 0) || ((this->IsInUnintentionalDecoupling == 1) && (it->TrainInfo.Acceleration >= 0))) { // if the train is not yet in following mode then
												if (V > SpeedForEoA) {
													CanTrainLinkForCoupling = true;									   // This is the only case when trains can start linking to be virtually coupled, if they are not coupled already
													double TimeToBrake = ((V - SpeedForEoA) / max_train_decelaration); // +(1 * timestep);  //This is the time needed by the train to reach the speed of the train ahead, plus 1 is needed to cover up for the integration delay of 1 step
													PredictedDistanceToCoupling = it->TrainInfo.TrainSpeed * TimeToBrake;
													// With the statement below trains will always try to couple with a train ahead if that is possible
													if ((ETCSBrakingPoint.X + PredictedDistanceToCoupling) <= train_route[this->indexOfRoute].x_of_end_node * 1000) { // if with the predicted distance the Braking Point is still within the limits of train route then
														ETCSBrakingPoint.X = ETCSBrakingPoint.X + PredictedDistanceToCoupling;											// Change the distance of the BrakingPoint. We chang it here so that the function can also detect other EoA for other train that might pop up between the real BrakingPoint.X and the prediction distance
														BrakingForEoAModified = true;
													}
												}
											}
										} else { // or if the trains do not have the next block section in common the trains need to split apart and this train will separate from the previous by means of an absolute braking distance
											SpeedForEoA = 0;
										}
									}
								}
							}

							if (SpeedForEoA > -1) { // if we have a valid MA and the SpeedForEoa is therefore different from -1
								// let us compute the approximate abscissa to Brake at for EoA
								AbscissaToBrakeAtForEoA = ETCSBrakingPoint.X - (pow(V, 2) - pow(SpeedForEoA, 2) / (2 * this->max_train_decelaration));

								if ((ETCSBrakingPoint.X > S) && (AbscissaToBrakeAtForEoA < ClosestPointToBrakeForETCS)) {
									ClosestPointToBrakeForETCS = AbscissaToBrakeAtForEoA; // if the AbscissaToBrakeAtForEoA is lower then ClosestPointToBrakeForETCS then this one becomes the ClosestPointToBrakeAt
									TargetSpeedForMA = SpeedForEoA;						  // The TargetSpeed for MA becomes the Speed ForEoA
									ClosestETCSBrakingPoint = ETCSBrakingPoint;
									ClosestETCSBrakingPoint.isSignalled = true; // putting isSignalled to true, gives a way to the Braking point to be recognised as an End of Authority
									SignallingLevelClosestETCSBrakingPoint = BS[z + BlockPos].SignallingLevel;

									// if the train can link with a train ahead
									ClosestETCSBrakingPoint.virtualCouplingNode = CanTrainLinkForCoupling;
									// Assigning the last received MA
									this->Last_Received_MA = *it;
									Last_Received_MA.RelativePosEoA = ETCSBrakingPoint.X; // Setting the Relative position of the EoA on the route of the train
									// In case the distance of ETCSBrakingPoint was modified then we need to subtract the Predicted Distance from it
									if (BrakingForEoAModified == 1) {
										Last_Received_MA.RelativePosEoA = ETCSBrakingPoint.X - PredictedDistanceToCoupling;
									}
									if (ClosestETCSBrakingPoint.virtualCouplingNode == 1) { // In case the train is not in following mode and can couple at a train ahead
										// Assigning the Predicted MA to link at
										this->Predicted_MA_To_CoupleAt = *it;
										Predicted_MA_To_CoupleAt.RelativePosEoA = ETCSBrakingPoint.X;
									}
								}
							}
						}
					}
				}
			}
		}
		// if there is a Closest ETCS Braking point that has been assigned, then add it to the list of braking points
		if (ClosestETCSBrakingPoint.X != 9999999999) {
			BrakingPoints.push_back(ClosestETCSBrakingPoint);
		}

		// Identifying the point with the minimum braking distance
		if (BrakingPoints.empty() != 1) {
			for (list<Node>::iterator n = BrakingPoints.begin(); n != BrakingPoints.end(); n++) {
				double BrakeDistToNode = -1;
				double Xob, Vob;
				bool TrainMustStop = Train_Must_Stop_For_OL_Order(n->respectOrder, n->indexOrderList); // Check if there is an order to respect
																										// if there is an order to be respected at a given junction/station then compute the braking distance for the train to stop there
				if (TrainMustStop == 1) {

					BrakeDistToNode = BrakDist_Block(V, 0, n->X * 1000, BS, Blocks);
					Xob = n->X * 1000;
					Vob = 0;
				}
				// if instead there is no order to be respected compute the Braking distance for the braking points
				else {
					// if the braking Node is an ETCS End Of Authority
					if (n->isSignalled == 1) {

						if (n->stationName == "DecoupleTrain") { // In case the Braking Point is a decouplig point for Virtual Coupling computed by the EVC
							BrakeDistToNode = BrakDist_Block(V, 0, n->X, BS, Blocks);
							Xob = n->X;
							Vob = 0;
						} else { // otherwise for all the other MAs
							BrakeDistToNode = BrakDist_Block(V, TargetSpeedForMA, n->X, BS, Blocks);
							Xob = n->X;
							Vob = TargetSpeedForMA;
						}
					}
					// if the Braking Point is not a MA then compute the Braking distance because of a static or signalling speed restriction
					else {
						BrakeDistToNode = BrakDist_Block(V, n->arcSpeedLimit, n->X * 1000, BS, Blocks);
						Xob = n->X * 1000;
						Vob = n->arcSpeedLimit;
					}
				}

				// If the GradientException is true and there is still the same target point as in the previous instant then
				if ((this->GradientExceptionInBraking == 1) && (Xob == this->Xob[PreviousInstant]) && (Vob == this->Vob[PreviousInstant])) {
					MinBX = this->Sbrak[brakingPoint + counter - 1]; // return the next braking point as braking distance
					Xobmin = Xob;							// the target point does not change
					Vobmin = Vob;
					break; // break the for loop over the braking points
				}

				else {
					if (BrakeDistToNode != -1) {
						if ((BrakeDistToNode < MinBX) && (Xob > S)) {
							MinBX = BrakeDistToNode;
							Xobmin = Xob;
							Vobmin = Vob;
							this->BrakingForEoA = n->isSignalled;				// In case the train was braking for an ETCS Movement Authority then set BrakingForEoA of the train to true
							this->IsTrainCoupling = n->virtualCouplingNode; // The train is set to IsTrainCoupling = true when it can follow the train ahead and the train is not already in the following mode
							if (this->IsTrainInFollowingMode == 1) {
								if (n->stationName == "DecoupleTrain") {
									IsTrainDecoupling = true;
								} else {
									IsTrainDecoupling = false;
								}
							}
						}
					}
				}
			}
		}
		return MinBX;
	}

	// This Function simulate the capacity of the train driver to react to the first most severe service limit (i.e. it calculates the braking distance and the braking point for satisfying the most severe running operation limit)
	double EuropeanVitalComputer(double S, double V, int PreviousInstant, Section* BS, int Blocks) {
		Section Block;
		Arc As;
		int ArcPos = 0;
		int BlockPos = 0;
		double BrakComp;
		double MinBX = 99999999999; // Number of Braking points in the Block Section occupied by the train, Index of the Arc of the signalling_block_sections occupied by the train, Index of the signalling_block_sections occupied by the train, Matrix of all Brak Distance of the Braking point of the Block Section occupied by the train, Minimum Braking Abscissa, Braking Abscissa of the block section arcs which follows the signalling_block_sections occupied by the train
		bool SatisfiesCondition = false;
		bool BrakeForLoopCondtionFound = false;
		double Xob, Vob; // This variables represents: Xob is the vector of objective braking points abscissa;  Vob is the vector of objective braking points speed;(These are for the braking points belonging to the signalling_block_sections occupied by the Train)

		int N_BrakP = 0;			// Number of braking points on the block section currently occupied by the train
		int N_BrakPFirstAfter = 0;	// Number of braking points on the first block section after the one currently occupied by the train
		int N_BrakPSecondAfter = 0; // Number of braking points on the second block section after the one currently occupied by the train

		Node BrakingPoints[200]; // These are the nodes where the train should brake at
		int N_Brak_Dist = 0;	 // This is the total number of Braking Points

		// Identifying the current position of the train over a block section
		for (int h = 0; h < Blocks; h++) {
			if ((S < BS[h].end_node.X * 1000) && (S >= BS[h].start_node.X * 1000)) {
				Block = BS[h];
				BlockPos = h;
				break;
			}
		}
		for (int j = 0; j < Block.total_arcs; j++) {
			if ((S < Block.arcs_in_signalling_block_section[j].endNode.X * 1000) && (S >= Block.arcs_in_signalling_block_section[j].startNode.X * 1000)) {
				As = Block.arcs_in_signalling_block_section[j];
				ArcPos = j;
				break;
			}
		}

		// Inserting the braking points due to infrastructure and signal aspect constraints into the BrakingPoints Node Array
		for (int p = ArcPos; p < BS[BlockPos].total_arcs; p++) {
			BrakingPoints[N_BrakP] = BS[BlockPos].arcs_in_signalling_block_section[p].endNode;
			BrakingPoints[N_BrakP].arcSpeedLimit = BS[BlockPos].arcs_in_signalling_block_section[p].speedInBraking;
			N_BrakP++;
		}
		// To N_BrakP we need to add also the number of points that the train would need to brake at because of ETCS level 3. These points do not belong to signals or infrastructure points
		if (BS[BlockPos].N_ETCS3BrakingPoints > 0) {
			for (int p = 0; p < BS[BlockPos].N_ETCS3BrakingPoints; p++) {
				if (BS[BlockPos].ETCS3BrakingPointsTrainID[p] != this->trainDescription) { // the braking point is inserted only if it does not relate to the train itself (i.e. only if it relates to another train)
					BrakingPoints[N_BrakP].X = BS[BlockPos].ETCS3BrakingPoints[p];
					BrakingPoints[N_BrakP].arcSpeedLimit = 0;
					N_BrakP++;
				}
			}
		}

		N_Brak_Dist = N_BrakP; // Putting the Number of BrakingPoints equal to N_BrakP

		// Calculating the total number of breaking points to find the minimum breaking distance (all the arcs of the block section which follows the occupied one are also considered)
		int Cons_Block = 2;
		if (BlockPos + Cons_Block > Blocks - 1) {
			Cons_Block = Blocks - BlockPos - 1;
		}

		for (int z = 0; z < Cons_Block; z++) { // Since we set Cons_Block=2, z can assume just two values that are 0 and 1

			if (z == 0) { // if we are on the first block section after the signalling_block_sections[BlockPos] z=0
				for (int p = 0; p < BS[z + BlockPos + 1].total_arcs; p++) {
					BrakingPoints[N_Brak_Dist] = BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].endNode;
					BrakingPoints[N_Brak_Dist].arcSpeedLimit = BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].speedInBraking;
					N_Brak_Dist++;		 // Increasing the number of braking points in the array
					N_BrakPFirstAfter++; // Increasing the number of braking points belonging to the first block section after the one currently occupied by the train
				}
				// Check if we have ETCS3 Braking point on this block section
				if (BS[z + BlockPos + 1].N_ETCS3BrakingPoints > 0) {
					for (int p = 0; p < BS[z + BlockPos + 1].N_ETCS3BrakingPoints; p++) {
						if (BS[z + BlockPos + 1].ETCS3BrakingPointsTrainID[p] != this->trainDescription) { // add the braking point only if this is due to a train different from this
							BrakingPoints[N_Brak_Dist].X = BS[z + BlockPos + 1].ETCS3BrakingPoints[p];
							BrakingPoints[N_Brak_Dist].arcSpeedLimit = 0; // imposing a 0 speed value for the ETCS 3 braking curve
							N_Brak_Dist++;						   // Increasing the number of braking points in the array
							N_BrakPFirstAfter++;				   // Increasing the number of braking points belonging to the first block section after the one currently occupied by the train
						}
					}
				}
			}

			else if (z == 1) { // if we are on the second block section after the signalling_block_sections[BlockPos] z=1
				for (int p = 0; p < BS[z + BlockPos + 1].total_arcs; p++) {
					BrakingPoints[N_Brak_Dist] = BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].endNode;
					BrakingPoints[N_Brak_Dist].arcSpeedLimit = BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].speedInBraking;
					N_Brak_Dist++;		  // Increasing the number of braking points in the array
					N_BrakPSecondAfter++; // Increasing the number of braking points belonging to the first block section after the one currently occupied by the train
				}
				// Check if we have ETCS3 Braking point on this block section
				if (BS[z + BlockPos + 1].N_ETCS3BrakingPoints > 0) {
					for (int p = 0; p < BS[z + BlockPos + 1].N_ETCS3BrakingPoints; p++) {
						if (BS[z + BlockPos + 1].ETCS3BrakingPointsTrainID[p] != this->trainDescription) { // add the braking point only if this is due to a train different from this
							BrakingPoints[N_Brak_Dist].X = BS[z + BlockPos + 1].ETCS3BrakingPoints[p];
							BrakingPoints[N_Brak_Dist].arcSpeedLimit = 0; // imposing a 0 speed value for the ETCS 3 braking curve
							N_Brak_Dist++;						   // Increasing the number of braking points in the array
							N_BrakPSecondAfter++;				   // Increasing the number of braking points belonging to the first block section after the one currently occupied by the train
						}
					}
				}
			}

		} // if the train is on the last block section it cannot calculate braking points on the following sections (just because they don't exist)

		// Once the array of the Braking Point Nodes has been computed, we compute the braking curves for each of the nodes
		if (Cons_Block == 2) {
#pragma omp parallel private(BrakComp, Xob, Vob, SatisfiesCondition)
			{
#pragma omp for
				for (int k = 0; k < N_Brak_Dist; k++) {
					SatisfiesCondition = false;
					if (BrakeForLoopCondtionFound == 0) {
						if (k <= N_BrakP - 1) {
							bool TrainMustStop = Train_Must_Stop_For_OL_Order(BrakingPoints[k].respectOrder, BrakingPoints[k].indexOrderList); // Check if there is an order to respect
							if (TrainMustStop == 1) {
								BrakComp = BrakDist_Block(V, 0, BrakingPoints[k].X * 1000, BS, Blocks);
								Xob = BrakingPoints[k].X * 1000;
								Vob = 0;
							} else {
								BrakComp = BrakDist_Block(V, BrakingPoints[k].arcSpeedLimit, BrakingPoints[k].X * 1000, BS, Blocks);
								Xob = BrakingPoints[k].X * 1000;
								Vob = BrakingPoints[k].arcSpeedLimit;
							}
							// If the GradientException is true and there is still the same target point as in the previous instant then
							if ((this->GradientExceptionInBraking == 1) && (Xob == this->Xob[PreviousInstant]) && (Vob == this->Vob[PreviousInstant])) {

								SatisfiesCondition = true; // break the for loop by putting satisfying condition to true;
							}
						}
						// down here are the braking point belonging to the first block section after the one currently occupied by the train
						else if ((k > N_BrakP - 1) && (k <= N_BrakP + N_BrakPFirstAfter - 1)) {
							bool TrainMustStop = Train_Must_Stop_For_OL_Order(BrakingPoints[k].respectOrder, BrakingPoints[k].indexOrderList);
							if (TrainMustStop == 1) {
								BrakComp = BrakDist_Block(V, 0, BrakingPoints[k].X * 1000, BS, Blocks);
								Xob = BrakingPoints[k].X * 1000;
								Vob = 0;
							} else {
								BrakComp = BrakDist_Block(V, BrakingPoints[k].arcSpeedLimit, BrakingPoints[k].X * 1000, BS, Blocks);
								Xob = BrakingPoints[k].X * 1000;
								Vob = BrakingPoints[k].arcSpeedLimit;
							}
							// If the GradientException is true and there is still the same target point as in the previous instant then
							if ((this->GradientExceptionInBraking == 1) && (Xob == this->Xob[PreviousInstant]) && (Vob == this->Vob[PreviousInstant])) {

								SatisfiesCondition = true; // break the for loop by putting satisfying condition to true;
							}

						}
						// down here are the braking point belonging to the second block section after the one currently occupied by the train
						else if (k > N_BrakP + N_BrakPFirstAfter - 1) {
							bool TrainMustStop = Train_Must_Stop_For_OL_Order(BrakingPoints[k].respectOrder, BrakingPoints[k].indexOrderList);
							if (TrainMustStop == 1) {
								BrakComp = BrakDist_Block(V, 0, BrakingPoints[k].X * 1000, BS, Blocks);
								Xob = BrakingPoints[k].X * 1000;
								Vob = 0;
							} else {
								BrakComp = BrakDist_Block(V, BrakingPoints[k].arcSpeedLimit, BrakingPoints[k].X * 1000, BS, Blocks);
								Xob = BrakingPoints[k].X * 1000;
								Vob = BrakingPoints[k].arcSpeedLimit;
							}
							// If the GradientException is true and there is still the same target point as in the previous instant then
							if ((this->GradientExceptionInBraking == 1) && (Xob == this->Xob[PreviousInstant]) && (Vob == this->Vob[PreviousInstant])) {
								SatisfiesCondition = true; // break the for loop by putting satisfying condition to true;
							}
						}

#pragma omp critical
						{
							if (SatisfiesCondition == 1) {
								MinBX = this->Sbrak[brakingPoint + counter - 1]; // return the next braking point as braking distance
								Xobmin = Xob;							// the target point does not change
								Vobmin = Vob;
								BrakeForLoopCondtionFound = true;
							} else {
								if ((BrakComp < MinBX) && (Xob > S)) {
									MinBX = BrakComp;
									Xobmin = Xob;
									Vobmin = Vob;
								}
							}
						}
					}
				}
			}
		}

		else if (Cons_Block == 1) {
#pragma omp parallel private(BrakComp, Xob, Vob, SatisfiesCondition)
			{
#pragma omp for
				for (int k = 0; k < N_Brak_Dist; k++) {
					SatisfiesCondition = false;
					if (BrakeForLoopCondtionFound == 0) {
						if (k <= N_BrakP - 1) {
							bool TrainMustStop = Train_Must_Stop_For_OL_Order(BrakingPoints[k].respectOrder, BrakingPoints[k].indexOrderList); // Check if there is an order to respect
							if (TrainMustStop == 1) {
								BrakComp = BrakDist_Block(V, 0, BrakingPoints[k].X * 1000, BS, Blocks);
								Xob = BrakingPoints[k].X * 1000;
								Vob = 0;
							} else {
								BrakComp = BrakDist_Block(V, BrakingPoints[k].arcSpeedLimit, BrakingPoints[k].X * 1000, BS, Blocks);
								Xob = BrakingPoints[k].X * 1000;
								Vob = BrakingPoints[k].arcSpeedLimit;
							}
							// If the GradientException is true and there is still the same target point as in the previous instant then
							if ((this->GradientExceptionInBraking == 1) && (Xob == this->Xob[PreviousInstant]) && (Vob == this->Vob[PreviousInstant])) {
								SatisfiesCondition = true; // break the for loop by putting satisfying condition to true;
							}

						}
						// Down here the braking points on the block section right after the one currently occupied by the train
						else {
							bool TrainMustStop = Train_Must_Stop_For_OL_Order(BrakingPoints[k].respectOrder, BrakingPoints[k].indexOrderList);
							if (TrainMustStop == 1) {
								BrakComp = BrakDist_Block(V, 0, BrakingPoints[k].X * 1000, BS, Blocks);
								Xob = BrakingPoints[k].X * 1000;
								Vob = 0;
							} else {
								BrakComp = BrakDist_Block(V, BrakingPoints[k].arcSpeedLimit, BrakingPoints[k].X * 1000, BS, Blocks);
								Xob = BrakingPoints[k].X * 1000;
								Vob = BrakingPoints[k].arcSpeedLimit;
							}
							// If the GradientException is true and there is still the same target point as in the previous instant then
							if ((this->GradientExceptionInBraking == 1) && (Xob == this->Xob[PreviousInstant]) && (Vob == this->Vob[PreviousInstant])) {
								SatisfiesCondition = true; // break the for loop by putting satisfying condition to true;
							}
						}
#pragma omp critical
						{
							if (SatisfiesCondition == 1) {
								MinBX = this->Sbrak[brakingPoint + counter - 1]; // return the next braking point as braking distance
								Xobmin = Xob;							// the target point does not change
								Vobmin = Vob;
								BrakeForLoopCondtionFound = true;
							} else {
								if ((BrakComp < MinBX) && (Xob > S)) {
									MinBX = BrakComp;
									Xobmin = Xob;
									Vobmin = Vob;
								}
							}
						}
					}
				}
			}
		}

		else if (Cons_Block == 0) {
#pragma omp parallel private(BrakComp, Xob, Vob, SatisfiesCondition)
			{
#pragma omp for
				for (int k = 0; k < N_Brak_Dist; k++) {
					SatisfiesCondition = false;
					if (BrakeForLoopCondtionFound == 0) {
						bool TrainMustStop = Train_Must_Stop_For_OL_Order(BrakingPoints[k].respectOrder, BrakingPoints[k].indexOrderList); // Check if there is an order to respect
						if (TrainMustStop == 1) {
							BrakComp = BrakDist_Block(V, 0, BrakingPoints[k].X * 1000, BS, Blocks);
							Xob = BrakingPoints[k].X * 1000;
							Vob = 0;
						} else {
							BrakComp = BrakDist_Block(V, BrakingPoints[k].arcSpeedLimit, BrakingPoints[k].X * 1000, BS, Blocks);
							Xob = BrakingPoints[k].X * 1000;
							Vob = BrakingPoints[k].arcSpeedLimit;
						}
						// If the GradientException is true and there is still the same target point as in the previous instant then
						if ((this->GradientExceptionInBraking == 1) && (Xob == this->Xob[PreviousInstant]) && (Vob == this->Vob[PreviousInstant])) {
							SatisfiesCondition = true; // break the for loop by putting satisfying condition to true;
						}
#pragma omp critical
						{
							if (SatisfiesCondition == 1) {
								MinBX = this->Sbrak[brakingPoint + counter - 1]; // return the next braking point as braking distance
								Xobmin = Xob;							// the target point does not change
								Vobmin = Vob;
								BrakeForLoopCondtionFound = true;
							} else {
								if ((BrakComp < MinBX) && (Xob > S)) {
									MinBX = BrakComp;
									Xobmin = Xob;
									Vobmin = Vob;
								}
							}
						}
					}
				}
			}
		}
		return MinBX;
	}

	// This Function simulate the capacity of the train driver to react to the first most severe service limit (i.e. it calculates the braking distance and the braking point for satisfying the most severe running operation limit)
	double BraKDistCompWithLists(double S, double V, int PreviousInstant, Section* BS, int Blocks) {
		Section Block;
		Arc As;
		int N_BrakP;
		int ArcPos = 0;
		int BlockPos = 0;
		double MinBX = 99999999999; // Number of Braking points in the Block Section occupied by the train, Index of the Arc of the signalling_block_sections occupied by the train, Index of the signalling_block_sections occupied by the train, Matrix of all Brak Distance of the Braking point of the Block Section occupied by the train, Minimum Braking Abscissa, Braking Abscissa of the block section arcs which follows the signalling_block_sections occupied by the train

		list<Node> BrakingPoints; // This is the list of the nodes constituting braking points

		for (int h = 0; h < Blocks; h++) {
			if ((S < BS[h].end_node.X * 1000) && (S >= BS[h].start_node.X * 1000)) {
				Block = BS[h];
				BlockPos = h;
				break;
			}
		}
		for (int j = 0; j < Block.total_arcs; j++) {
			if ((S < Block.arcs_in_signalling_block_section[j].endNode.X * 1000) && (S >= Block.arcs_in_signalling_block_section[j].startNode.X * 1000)) {
				As = Block.arcs_in_signalling_block_section[j];
				ArcPos = j;
				break;
			}
		}
		N_BrakP = Block.total_arcs - ArcPos; // Initializing dimensions of Braking points vectors

		// Putting in the list of braking points the nodes of the Arc where the train currently is
		for (int p = ArcPos; p < BS[BlockPos].total_arcs; p++) {
			// Setting the current Arc speedInBraking to the final Node of Arc
			BS[BlockPos].arcs_in_signalling_block_section[p].endNode.arcSpeedLimit = BS[BlockPos].arcs_in_signalling_block_section[p].speedInBraking;
			BrakingPoints.push_back(BS[BlockPos].arcs_in_signalling_block_section[p].endNode);
		}

		// Calculating the total number of breaking points to find the minimum breaking distance (all the arcs of the block section which follows the occupied one are also considered)
		int Cons_Block = 8;
		if (BlockPos + Cons_Block > Blocks - 1) {
			Cons_Block = Blocks - BlockPos - 1;
		}

		// Putting in list of Braking Points all the nodes belonging to the other block sections
		for (int z = 0; z < Cons_Block; z++) {
			for (int p = 0; p < BS[z + BlockPos + 1].total_arcs; p++) {
				BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].endNode.arcSpeedLimit = BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].speedInBraking;
				BrakingPoints.push_back(BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].endNode);
			}
		}

		// Identifying the point with the minimum braking distance
		if (BrakingPoints.empty() != 1) {
			for (list<Node>::iterator n = BrakingPoints.begin(); n != BrakingPoints.end(); n++) {
				double BrakeDistToNode = -1;
				double Xob, Vob;
				bool TrainMustStop = Train_Must_Stop_For_OL_Order(n->respectOrder, n->indexOrderList); // Check if there is an order to respect
				if (TrainMustStop == 1) {
					BrakeDistToNode = BrakDist_Block(V, 0, n->X * 1000, BS, Blocks);
					Xob = n->X * 1000;
					Vob = 0;
				} else {
					BrakeDistToNode = BrakDist_Block(V, n->arcSpeedLimit, n->X * 1000, BS, Blocks);
					Xob = n->X * 1000;
					Vob = n->arcSpeedLimit;
				}
				// If the GradientException is true and there is still the same target point as in the previous instant then
				if ((this->GradientExceptionInBraking == 1) && (Xob == this->Xob[PreviousInstant]) && (Vob == this->Vob[PreviousInstant])) {
					MinBX = this->Sbrak[brakingPoint + counter - 1]; // return the next braking point as braking distance
					Xobmin = Xob;							// the target point does not change
					Vobmin = Vob;
					break; // break the for loop over the braking points
				}

				else {
					if (BrakeDistToNode != -1) {
						if ((BrakeDistToNode < MinBX) && (Xob > S)) {
							MinBX = BrakeDistToNode;
							Xobmin = Xob;
							Vobmin = Vob;
						}
					}
				}
			}
		}

		return MinBX;
	}

	// Function to draw the braking curve which must be followed by the train
	virtual void DrawBrakingCurve(double V1, double V2, double X0, Section* BS, int Blocks) {
		int t;
		Section Block;
		Arc Ab;
		double U[2000];
		double X[2000]; // Definition of Temporary variables Vector U=Speed, X=Abscissa
		double DX;		// Final Abscissa deriving from inverse integration of the Braking Curve
		U[0] = V2;
		X[0] = X0 - 0.002; // The train stops at two metres from the objective point
		if (V1 > V2) {
			for (t = 1; t < 2000; t++) {
				for (int h = 0; h < Blocks; h++) {
					if ((X[t - 1] < BS[h].end_node.X * 1000) && (X[t - 1] >= BS[h].start_node.X * 1000)) { // Selection of the right Block Section
						Block = BS[h];
					}
				}
				for (int j = 0; j < Block.total_arcs; j++) {
					if ((X[t - 1] < Block.arcs_in_signalling_block_section[j].endNode.X * 1000) && (X[t - 1] >= Block.arcs_in_signalling_block_section[j].startNode.X * 1000)) { // Selection of the right Arc of the Block Section
						Ab = Block.arcs_in_signalling_block_section[j];
					}
				}
				// Inverse integration of the real Braking Curve
				U[t] = U[t - 1] - (-total_train_mass * massFactor * max_train_decelaration - total_train_resistances(U[t - 1], Ab.gradient, Ab.curvature)) * timestep / (total_train_mass * massFactor);
				X[t] = X[t - 1] + (total_train_mass * massFactor * U[t - 1] * (U[t] - U[t - 1])) / (-total_train_mass * massFactor * max_train_decelaration - total_train_resistances(U[t - 1], Ab.gradient, Ab.curvature));
			}
			for (t = 0; t < 2000; t++) {
				if (U[t] >= V1) {
					DX = X[t];
					BrakStep = t;
					break;
				}
			}

			for (int t = 0; t <= BrakStep; t++) {
				Sbrak[t] = X[BrakStep - t];
				Vbrak[t] = U[BrakStep - t];
			}
		}
	}

	// Function for printing Train Trajectory Data
	virtual void PrintTrajectory() {
		string path;
		path = path + InputMainFolder + "/TEMP/Traj_Train_" + trainDescription + ".txt";
		ofstream trainout;
		trainout.open((char*)path.c_str(), ios::binary);
		trainout << "Time[s]\tSpeed[m/s]\tPosition[m]\tTail_Position[m]\tPower_Cons[kW]\tBX[m]\tinstant_train_energy_consumption[KWh]\tBlock" << "\n";
		const auto segments = validTrajectorySegments(instant_spatial_position, earliestActiveTrajectoryIndex, End_Time);
		bool firstSegment = true;
		if (train_route[indexOfRoute].reversed_direction == 0) {
			for (const auto& segment : segments) {
				if (!firstSegment)
					trainout << "\n";
				firstSegment = false;
				for (int i = segment.first; i <= segment.last; i++) {
					trainout << i * timestep << "\t" << instant_train_speed[i] << "\t" << instant_spatial_position[i] << "\t" << instant_spatial_position[i] - train_length << "\t" << instant_train_power_consumption[i] / 1000 << "\t" <<

						BX[i] << "\t" << instant_train_energy_consumption[i] * 0.27778 << "\t" << instant_block_section_occupied[i] <<

						// Bs.
						"\n";
				}
			}
		} else { // If instead the train runs on a reversed route then subtract the instant_spatial_position[i] from the Total Length of the route
			for (const auto& segment : segments) {
				if (!firstSegment)
					trainout << "\n";
				firstSegment = false;
				for (int i = segment.first; i <= segment.last; i++) {
					trainout << i * timestep << "\t" << instant_train_speed[i] << "\t" << train_route[indexOfRoute].OriginalRefReversedRoute - instant_spatial_position[i] << "\t" << train_route[indexOfRoute].OriginalRefReversedRoute - instant_spatial_position[i] - train_length << "\t" << instant_train_power_consumption[i] / 1000 << "\t" << BX[i] << "\t" << instant_train_energy_consumption[i] * 0.27778 << "\t" << instant_block_section_occupied[i] << "\n";
				}
			}
		}
		trainout.close();
	}

	// Function to print the trajectory of the train at instant t (being any time instant of the simulation) in the Folder described by FolderName
	virtual void PrintTrainTrajectory_At_Instant(int t, string FolderName) {
		string FileName;
		FileName = FileName + FolderName + "Traj_Train_" + trainDescription + ".txt";
		ofstream OutputFile;
		OutputFile.open((char*)FileName.c_str());
		OutputFile << "Time[s] Speed[m/s] Position[m] Tail_Position[m] Power_Cons[kW] V_Obj[m/s] instant_train_energy_consumption[KWh]" << "\n";
		const int activeLast = OutOfSimulation == 1 || End_Time < t ? End_Time : t;
		const auto segments = validTrajectorySegments(instant_spatial_position, earliestActiveTrajectoryIndex, activeLast);
		bool firstSegment = true;
		for (const auto& segment : segments) {
			if (!firstSegment)
				OutputFile << "\n";
			firstSegment = false;
			for (int i = segment.first; i <= segment.last; i++) {
				OutputFile << i * timestep << " " << instant_train_speed[i] << " " << instant_spatial_position[i] << " " << instant_spatial_position[i] - train_length << " " << instant_train_power_consumption[i] / 1000 << " " << Vob[i] << " " << instant_train_energy_consumption[i] * 0.27778 << "\n";
			}
		}
		OutputFile.close();
	}

	// Class Destructor
	virtual ~Train() {}

	// This function is used to determine the ID of the TDS the train is currently occupying a block section
	virtual string DetermineOccupiedTDS(int timeinstant, bool IsRouteReversed) {
		string Occupied_TDS_ID = "";
		double BeginningNode = -1, FinalNode = -1; // This are the Beginning and End Node of the TDS
		// if the route of the train is not reversed
		if (IsRouteReversed == 0) {
			if (Bs.TDS_in_block.size() > 0) {
				for (auto i = Bs.TDS_in_block.begin(); i != Bs.TDS_in_block.end(); i++) {
					// if the TDS is straight and not on a diverging switch
					if ((*i)->node_on_switch.X == -1) {
						BeginningNode = (*i)->start_node.X * 1000;
						FinalNode = (*i)->end_node.X * 1000;
					}
					// if instead the TDS is on a diverging switch
					else {
						// Determine Beginning and final nodes
						if ((*i)->node_on_switch.X >= (*i)->connection_node.X) {
							BeginningNode = (*i)->start_node.X * 1000;
							FinalNode = (*i)->node_on_switch.X * 1000;
						}
						// Otherwise it is valid the following condition
						else {
							BeginningNode = (*i)->node_on_switch.X * 1000;
							FinalNode = (*i)->end_node.X * 1000;
						}
					}

					if ((instant_spatial_position[timeinstant] >= BeginningNode) && (instant_spatial_position[timeinstant] < FinalNode)) {
						Occupied_TDS_ID = (*i)->ID;
						break; // break the for loop over the TDS in the block section
					}
				}
			}
		}
		// if instead the route of the train is reversed
		else {
			// the the TDS nodes should be reversed as well
			if (Bs.TDS_in_block.size() > 0) {
				for (auto i = Bs.TDS_in_block.begin(); i != Bs.TDS_in_block.end(); i++) {
					// if the TDS is straight and not on a diverging switch
					if ((*i)->node_on_switch.X == -1) {
						// Beginning Node is the absolute distance between the end Node of the TDS ( as it is reversed) and the absolute position of the Beginning Node of the reversed block + the relative start Node position of the reversed block
						BeginningNode = (Bs.GeoXBegNode - (*i)->end_node.tdsbGeoCoordX) + Bs.start_node.X * 1000;
						FinalNode = (Bs.GeoXBegNode - (*i)->start_node.tdsbGeoCoordX) + Bs.start_node.X * 1000;
					}

					// if instead the TDS is on a diverging switch
					else {
						// Determine Beginning and final nodes
						if ((*i)->node_on_switch.X >= (*i)->connection_node.X) {
							BeginningNode = (Bs.GeoXBegNode - (*i)->node_on_switch.tdsbGeoCoordX) + Bs.start_node.X * 1000;
							FinalNode = (Bs.GeoXBegNode - (*i)->start_node.tdsbGeoCoordX) + Bs.start_node.X * 1000;
						}
						// Otherwise it is valid the following condition
						else {
							BeginningNode = (Bs.GeoXBegNode - (*i)->end_node.tdsbGeoCoordX) + Bs.start_node.X * 1000;
							FinalNode = (Bs.GeoXBegNode - (*i)->node_on_switch.tdsbGeoCoordX) + Bs.start_node.X * 1000;
						}
					}

					if ((instant_spatial_position[timeinstant] >= BeginningNode) && (instant_spatial_position[timeinstant] < FinalNode)) {
						Occupied_TDS_ID = (*i)->ID;
						break; // break the for loop over the TDS in the block section
					}
				}
			}
		}
		// finally return the ID of teh occupied TDS
		return Occupied_TDS_ID;
	}

	// Function to Simulate the Train Trajectory
	virtual void Trajectory_Block_Section(int i, double signalCode1, double signalCode2, double signalCode3) {
		if (OutOfSimulation == 0) {
			// A broken-down train holds its last state for the whole incident
			// window; entrance processing is also suspended, so a train whose
			// window covers its entry time simply enters after the window.
			if (i > 0 && Incident_Holds_Train(trainDescription, i)) {
				instant_train_speed[i] = 0;
				instant_spatial_position[i] = instant_spatial_position[i - 1];
				instant_train_power_consumption[i] = 0;
				train_energy_consumption(i);
				Eq[i] = 5;
				return;
			}
			// Determine the index of the Route that his train has to follow
			/*for (int k=0;k<N_Routes;k++){
			if (train_route[k].ID==TrainRouteID) {indexOfRoute=k; break;}
			}*/
			// Determine if the Train can enter the route
			checkEntrance(i, train_route[indexOfRoute].sequence_of_block_sections);
			if ((i >= departure_time) && (CanEnter == 1)) {
				double V_lim;
				int U;
				U = i;
				int Previous_Block_Index = 0;
				// Determination of the right Block Section
				for (int h = 0; h < train_route[indexOfRoute].N_Block_Sections; h++) {
					if ((instant_spatial_position[i - 1] < train_route[indexOfRoute].sequence_of_block_sections[h].end_node.X * 1000) && (instant_spatial_position[i - 1] >= train_route[indexOfRoute].sequence_of_block_sections[h].start_node.X * 1000)) {
						Bs = train_route[indexOfRoute].sequence_of_block_sections[h];
						if (h == 0)
							Previous_Block_Index = h;
						else {
							Previous_Block_Index = h - 1;
							break;
						}
					}
				}
				// Determination of the right running Arc
				for (int j = 0; j < Bs.total_arcs; j++) {
					if ((instant_spatial_position[i - 1] < Bs.arcs_in_signalling_block_section[j].endNode.X * 1000) && (instant_spatial_position[i - 1] >= Bs.arcs_in_signalling_block_section[j].startNode.X * 1000))
						As = Bs.arcs_in_signalling_block_section[j];
				}
				// Determination of the right speed limit to observe
				double Speeds[3];
				Speeds[0] = max_train_speed;
				Speeds[1] = As.speedLimit;
				Speeds[2] = As.signalSpeedLimit;
				V_lim = Speeds[0];
				for (int k = 0; k < 3; k++) {
					if (Speeds[k] <= V_lim)
						V_lim = Speeds[k];
				}
				// Imposing Station Nodes via cached indices
				for (int s = 0; s < numStations; s++) {
					int h = stationBlockSection[s];
					int k = stationArc[s];
					if (h >= 0 && k >= 0) {
						train_route[indexOfRoute].sequence_of_block_sections[h].arcs_in_signalling_block_section[k].endNode.station = 1;
						train_route[indexOfRoute].sequence_of_block_sections[h].arcs_in_signalling_block_section[k].speedInBraking = 0;
					}
				}
				// Calculating the most severe value of Train Braking Distance at time instant i
				double Braking_Distance = 0;
				Braking_Distance = BraKDistComp(instant_spatial_position[i - 1], instant_train_speed[i - 1], train_route[indexOfRoute].sequence_of_block_sections, train_route[indexOfRoute].N_Block_Sections);

				// Acceleration phase
				if ((instant_spatial_position[i - 1] < (Braking_Distance - 0.0001)) && (instant_train_speed[i - 1] < V_lim)) {
					instant_train_speed[i] = instant_train_speed[i - 1] + (tractiveEffort(instant_train_speed[i - 1]) - total_train_resistances(instant_train_speed[i - 1], As.gradient, As.curvature)) / (total_train_mass * massFactor) * timestep;
					if (instant_train_speed[i] < 0)
						instant_train_speed[i] = 0;
					if (instant_train_speed[i] > V_lim)
						instant_train_speed[i] = V_lim;
					instant_spatial_position[i] = instant_spatial_position[i - 1] + ((total_train_mass * massFactor) / (tractiveEffort(instant_train_speed[i - 1]) - total_train_resistances(instant_train_speed[i - 1], As.gradient, As.curvature))) * instant_train_speed[i - 1] * (instant_train_speed[i] - instant_train_speed[i - 1]);
					Eq[i] = 1;
					instant_train_power_consumption[i] = tractiveEffort(instant_train_speed[i - 1]) * instant_train_speed[i - 1];
					train_energy_consumption(i);
					temp = i;
					stop = i;
					BX[i] = Braking_Distance;
					Xob[i] = Xobmin;
					Vob[i] = Vobmin;
					counter = 0;
				}

				/*//Cruising Phase
				else if((instant_spatial_position[i-1]<Braking_Distance)&&(fabs((V_lim-instant_train_speed[i-1]))<2.78*timestep)){
				instant_train_speed[i]=V_lim;    if (instant_train_speed[i]<0)instant_train_speed[i]=0;
				instant_spatial_position[i]=instant_spatial_position[i-1]+instant_train_speed[i-1]*timestep;
				instant_train_power_consumption[i]=total_train_resistances(instant_train_speed[i-1],As.gradient,As.curvature)*instant_train_speed[i-1];    Eq[i]=2;
				temp=i; stop=i; BX[i]=Braking_Distance;Xob[i]=Xobmin; Vob[i]=Vobmin; counter=0;}*/

				// Braking Phase
				else if ((instant_spatial_position[i - 1] >= Braking_Distance) && (instant_spatial_position[i - 1] < train_route[indexOfRoute].x_of_end_node * 1000) && (instant_train_speed[i - 1] > 0)) {
					counter++;
					double Vobj = 0, Xobj = 0;
					if (counter == 1) {
						Vobj = Vobmin;
						Xobj = Xobmin;
						DrawBrakingCurve(instant_train_speed[i - 1], Vobj, Xobj, train_route[indexOfRoute].sequence_of_block_sections, train_route[indexOfRoute].N_Block_Sections);
						double m1, m2;
						m1 = (instant_train_speed[i - 1] - instant_train_speed[i - 2]) / (instant_spatial_position[i - 1] - instant_spatial_position[i - 2]);		  // angular coefficient of acceleration curve
						m2 = (Vbrak[1] - Vbrak[0]) / (Sbrak[1] - Sbrak[0]);																							  // angular coefficient of braking curve
						instant_spatial_position[i - 1] = (m1 * instant_spatial_position[i - 2] - instant_train_speed[i - 2] + Vbrak[1] - m2 * Sbrak[1]) / (m1 - m2); // Intersection abscissa
						instant_train_speed[i - 1] = instant_train_speed[i - 2] + m1 * (instant_spatial_position[i - 1] - instant_spatial_position[i - 2]);			  // Intersection speed
						for (int t = 0; t <= BrakStep; t++) {
							if ((Vbrak[t] < instant_train_speed[i - 1]) && (Sbrak[t] > instant_spatial_position[i - 1])) {
								brakingPoint = t;
								break;
							}
						}
						instant_train_speed[i] = Vbrak[brakingPoint + counter - 1];
						if (instant_train_speed[i] < 0)
							instant_train_speed[i] = 0;
						instant_spatial_position[i] = Sbrak[brakingPoint + counter - 1];
						Eq[i] = 55;
					} else {
						Eq[i] = 52;
						instant_train_speed[i] = Vbrak[brakingPoint + counter - 1];
						if (instant_train_speed[i] < 0)
							instant_train_speed[i] = 0;
						instant_spatial_position[i] = Sbrak[brakingPoint + counter - 1];
						if (instant_spatial_position[i] < instant_spatial_position[i - 1]) {
							counter = 1;
							Vobj = Vobmin;
							Xobj = Xobmin;
							DrawBrakingCurve(instant_train_speed[i - 1], Vobj, Xobj, train_route[indexOfRoute].sequence_of_block_sections, train_route[indexOfRoute].N_Block_Sections);
							for (int t = 0; t <= BrakStep; t++) {
								if ((Vbrak[t] < instant_train_speed[i - 1]) && (Sbrak[t] > instant_spatial_position[i - 1])) {
									brakingPoint = t;
									break;
								}
							}
							instant_train_speed[i] = Vbrak[brakingPoint + counter - 1];
							if (instant_train_speed[i] < 0)
								instant_train_speed[i] = 0;
							instant_spatial_position[i] = Sbrak[brakingPoint + counter - 1];
						}
					}
					// if two braking curves are both restrictive
					/*if ((Xobj!=Xobmin)||(Vobj!=Vobmin)||((Xobj!=Xobmin)&&(Vobj!=Vobmin))){counter=1;
					Xobj=Xobmin;   Vobj=Vobmin;   Eq[i]=53;
					DrawBrakingCurve(instant_train_speed[i-1],Vobj,Xobj,signalling_block_sections,Blocks);
					for (int t=0;t<BrakStep;t++){
					if((Vbrak[t]<instant_train_speed[i-1])&&(Sbrak[t]>instant_spatial_position[i-1])){ brakingPoint=t; break;}}
					instant_train_speed[i]=Vbrak[brakingPoint+counter-1];    if (instant_train_speed[i]<0)instant_train_speed[i]=0;
					instant_spatial_position[i]=Sbrak[brakingPoint+counter-1];}*/

					instant_train_power_consumption[i] = -(brakingEffort((i - temp - 1) * timestep) + gradient_resistances(As.gradient) + curvature_resistances(As.curvature) - total_train_resistances(instant_train_speed[i - 1], As.gradient, As.curvature)) * instant_train_speed[i - 1]; /*Eq[i]=3;*/
					train_energy_consumption(i);
					stop = i;
					BX[i] = Braking_Distance;
					Xob[i] = Xobmin;
					Vob[i] = Vobmin;
				}

				/*//Brake if the train enters next Arc with a speed higher than the Arc speed limit
				else if ((instant_train_speed[i-1]>V_lim)){
				instant_train_speed[i]=instant_train_speed[i-1]+(-brakingEffort((i-temp-1)*timestep)-(gradient_resistances(As.gradient)+curvature_resistances(As.curvature)))/(total_train_mass*massFactor)*timestep;     if (instant_train_speed[i]<0)instant_train_speed[i]=0;
				instant_spatial_position[i]=instant_spatial_position[i-1]+((total_train_mass*massFactor)/(-brakingEffort((i-temp-1)*timestep)-(gradient_resistances(As.gradient)+curvature_resistances(As.curvature))))*instant_train_speed[i-1]*(instant_train_speed[i]-instant_train_speed[i-1]);
				instant_train_power_consumption[i]=-(brakingEffort((i-temp-1)*timestep)+gradient_resistances(As.gradient)+curvature_resistances(As.curvature)-total_train_resistances(instant_train_speed[i-1],As.gradient,As.curvature))*instant_train_speed[i-1];   Eq[i]=4;
				BX[i]=Braking_Distance;Xob[i]=Xobmin; Vob[i]=Vobmin; stop=i; counter=0;}*/

				// Cruising Phase
				else if ((instant_spatial_position[i - 1] < Braking_Distance) && (instant_train_speed[i - 1] >= V_lim) && instant_train_speed[i - 1] > 0) {
					if (instant_train_speed[i - 1] > V_lim) {
						instant_train_speed[i - 1] = V_lim;
						instant_spatial_position[i - 1] = instant_spatial_position[i - 1];
						instant_train_power_consumption[i - 1] = total_train_resistances(instant_train_speed[i - 1], As.gradient, As.curvature) * instant_train_speed[i - 1];
						train_energy_consumption(i - 1);
						instant_train_speed[i] = V_lim;
						instant_spatial_position[i] = instant_spatial_position[i - 1] + instant_train_speed[i - 1] * timestep;
					} else {
						instant_train_speed[i] = V_lim;
						instant_spatial_position[i] = instant_spatial_position[i - 1] + instant_train_speed[i - 1] * timestep;
					}
					instant_train_power_consumption[i] = total_train_resistances(instant_train_speed[i - 1], As.gradient, As.curvature) * instant_train_speed[i - 1];
					Eq[i] = 12;
					train_energy_consumption(i);
					temp = i;
					stop = i;
					BX[i] = Braking_Distance;
					Xob[i] = Xobmin;
					Vob[i] = Vobmin;
					counter = 0;
				}

				/**************************************************************************************************************************/

				// Release of signalling system when the train exits simulation
				if (instant_spatial_position[i - 1] >= train_route[indexOfRoute].x_of_end_node * 1000) {
					End_Time = i - 1;
					instant_train_speed[i] = 0;
					instant_spatial_position[i] = -9999;
					instant_train_power_consumption[i] = -9999;
					Xobmin = train_route[indexOfRoute].x_of_end_node * 1000;
					Vobmin = train_route[indexOfRoute].sequence_of_block_sections[train_route[indexOfRoute].N_Block_Sections - 1].arcs_in_signalling_block_section[train_route[indexOfRoute].sequence_of_block_sections[train_route[indexOfRoute].N_Block_Sections - 1].total_arcs - 1].speedInBraking; /*This is the speedInBraking of the last Arc of the Route*/
				}
				/*else if (instant_spatial_position[i-1]<0){instant_train_speed[i]=0; instant_spatial_position[i]=-9999; instant_train_power_consumption[i]=-9999; }*/

				/**************************************************************************************************************************/

				// Standing Train Conditions
				if (instant_train_speed[i - 1] == 0) {

					// Stopping at a station for an estabilished dwell time
					if (((As.endNode.X * 1000 - instant_spatial_position[i - 1]) < 4) && (As.endNode.station == 1)) {
						Eq[i] = 5;
						double stoptime = 0;
						double dep_time = 0;
						counter = 0;
						for (int s = 0; s < numStations; s++) {
							if (Stations[s].stationName == As.endNode.stationName) {
								stoptime = Stations[s].StopTime;
								dep_time = ScheduledDepartures[s];
								Stations[s].StepStopped++;
								recordCurrentServiceStopArrival(i);
								if (Stations[s].StepStopped > Stations[s].StopTime)
									stoptime = 0;
							}
						}
						// Check if in this Station the train has also to respect a given OL Order
						if (As.endNode.respectOrder == 1) {
							bool Condition_To_Stop_Train = Conditions_To_Stop_Train_For_OL_Order(As.endNode.respectOrder, As.endNode.indexOrderList, Bs);
							// The train can leave the station only if the previous train has passed, it has satisfied the min dwell time and the signal aspect is non restrictive
							if ((Condition_To_Stop_Train == 0) && ((i - stop) > stoptime) && (i > dep_time)) {
								instant_train_speed[i] = 0.0001;
								instant_spatial_position[i] = As.endNode.X * 1000 + 0.0001;
							}
							// Otherwise the train has to remain stopped at the station
							else {
								instant_train_speed[i] = 0;
								instant_spatial_position[i] = As.endNode.X * 1000 - 0.0001;
							}
						}
						// If the station Node does not have to respect a specific train order, the train has to be stopped at the station for the dwell time
						else {
							if (((i - stop) <= stoptime) || (i <= dep_time)) {
								instant_train_speed[i] = 0;
								instant_spatial_position[i] = As.endNode.X * 1000 - 0.0001;
							} else {
								instant_train_speed[i] = 0.0001;
								instant_spatial_position[i] = As.endNode.X * 1000 + 0.0001;
							}
						}
						instant_train_power_consumption[i] = 0;
						train_energy_consumption(i);
					}

					// Stopping at a checkpoint Node where a certain train order must be respected
					else if ((As.endNode.X * 1000 - instant_spatial_position[i - 1] < 4) && (As.endNode.respectOrder == 1)) {
						counter = 0;
						bool Cond_To_Stop_Train = Conditions_To_Stop_Train_For_OL_Order(As.endNode.respectOrder, As.endNode.indexOrderList, Bs);
						if (Cond_To_Stop_Train == 0) {
							Eq[i] = 532;
							instant_train_speed[i] = 0.0001;
							instant_spatial_position[i] = As.endNode.X * 1000 + 0.0001;
						}
						// otherwise it has to be stopped
						else {
							Eq[i] = 534;
							instant_train_speed[i] = 0;
							instant_spatial_position[i] = As.endNode.X * 1000 - 0.0001;
						}

						instant_train_power_consumption[i] = 0;
						train_energy_consumption(i);
					}

					// Stopping at a Red Signal
					else if (As.endNode.X * 1000 - instant_spatial_position[i - 1] < 4) {
						Eq[i] = 6;
						if (!strcmp(Bs.state, "red")) {
							instant_spatial_position[i] = As.endNode.X * 1000 - 0.0001;
							instant_train_speed[i] = 0;
						} else {
							instant_spatial_position[i] = As.endNode.X * 1000 + 0.0001;
							instant_train_speed[i] = 0.0001;
						}
						instant_train_power_consumption[i] = 0;
						train_energy_consumption(i);
						counter = 0;
					}

					// if the speed limit on the Arc is 0: this case happens only when the BACC is implemented
					if (V_lim == 0) {
						counter = 0;
						Eq[i] = 958;
						instant_train_speed[i] = 0;
						instant_spatial_position[i] = instant_spatial_position[i - 1];
						instant_train_power_consumption[i] = 0;
						train_energy_consumption(i);
					}
				}

				// if the train has just entered the track therefore its position instant_spatial_position[i] is higher than 1 meter and lower than 3 meters VirtualQ[ID-1] must be updated to ID
				Update_LastEnteredTrain_For_All_OL(i, train_route[indexOfRoute].sequence_of_block_sections[Previous_Block_Index]);
			}
			/***********************************************************************************************************************/
			else {
				instant_train_speed[i] = 0.0;
				instant_spatial_position[i] = Start_Node_X * 1000;
				instant_train_power_consumption[i] = 0.0;
			}
		}
	}

	// Function to Replicate a train repeated regularly over the time
	void ReplicateTrainTrajectory(Train T_2_Replicate) {
		// Offset (i.e. time headway) between the two trains
		int Offset = (int)(departure_time - T_2_Replicate.departure_time);
		// Setting the End_Time
		End_Time = T_2_Replicate.End_Time + Offset;
		if (End_Time > initial_variables.times)
			End_Time = (int)initial_variables.times;

		for (int t = 0; t < initial_variables.times; t++) {
			if (t < departure_time) {
				instant_spatial_position[t] = train_route[indexOfRoute].x_of_start_node * 1000;
				instant_train_energy_consumption[t] = instant_train_speed[t] = instant_train_power_consumption[t] = 0;
			} else {
				instant_spatial_position[t] = T_2_Replicate.instant_spatial_position[t - Offset];
				instant_train_speed[t] = T_2_Replicate.instant_train_speed[t - Offset];
				instant_train_power_consumption[t] = T_2_Replicate.instant_train_power_consumption[t - Offset];
				instant_train_energy_consumption[t] = T_2_Replicate.instant_train_energy_consumption[t - Offset];
				// Updating the status of infrastructure elements
				if (t > 0) {
					UpdateInfrastructureElementStatus(train_route[indexOfRoute].InfrastructureElements, t, instant_spatial_position[t - 1], instant_spatial_position[t], instant_train_speed[t - 1], trainDescription);
				}
			}
		}
	}

	// Function to Simulate the Train Trajectory
	virtual void Trajectory_Block_Section_Free_Flow(int i, double signalCode1, double signalCode2, double signalCode3) {
		if (OutOfSimulation == 0) {
			// same breakdown hold as Trajectory_Block_Section
			if (i > 0 && Incident_Holds_Train(trainDescription, i)) {
				instant_train_speed[i] = 0;
				instant_spatial_position[i] = instant_spatial_position[i - 1];
				instant_train_power_consumption[i] = 0;
				train_energy_consumption(i);
				Eq[i] = 5;
				return;
			}

			// Determine if the Train can enter the route

			checkEntrance(i, train_route[indexOfRoute].sequence_of_block_sections);
			if (CanEnter == 1) {
				if (this->trainDescription == "A3_Wtl-Surbiton-1") {
					cout << "###################################################################" << endl
						 << "Train : " << this->trainDescription << endl
						 << " and Train instant (speed -1) " << this->instant_train_speed[i - 1] << endl
						 << "Position: " << this->instant_spatial_position[i - 1] << endl
						 << "CanEnter is " << this->CanEnter << endl
						 << "\n";
				}
			}
			if ((i >= departure_time) && (CanEnter == 1)) {
				double V_lim;
				int U;
				U = i;
				int Previous_Block_Index = 0;
				// Determination of the right Block Section
				for (int h = 0; h < train_route[indexOfRoute].N_Block_Sections; h++) {
					if ((instant_spatial_position[i - 1] < train_route[indexOfRoute].sequence_of_block_sections[h].end_node.X * 1000) && (instant_spatial_position[i - 1] >= train_route[indexOfRoute].sequence_of_block_sections[h].start_node.X * 1000)) {
						Bs = train_route[indexOfRoute].sequence_of_block_sections[h];
						if (h == 0)
							Previous_Block_Index = h;
						else {
							Previous_Block_Index = h - 1;
							break;
						}
					}
				}
				// Determination of the right running Arc
				for (int j = 0; j < Bs.total_arcs; j++) {
					if ((instant_spatial_position[i - 1] < Bs.arcs_in_signalling_block_section[j].endNode.X * 1000) && (instant_spatial_position[i - 1] >= Bs.arcs_in_signalling_block_section[j].startNode.X * 1000))
						As = Bs.arcs_in_signalling_block_section[j];
				}

				// Determination of the right speed limit to observe
				double Speeds[3];
				Speeds[0] = max_train_speed;
				Speeds[1] = As.speedLimit;
				Speeds[2] = As.signalSpeedLimit;
				V_lim = Speeds[0];
				for (int k = 0; k < 3; k++) {
					if (Speeds[k] <= V_lim)
						V_lim = Speeds[k];
				}
				cout << "Right speed limit " << to_string(V_lim * 3.6) << endl;
				// Imposing Station Nodes via cached indices
				for (int s = 0; s < numStations; s++) {
					int h = stationBlockSection[s];
					int k = stationArc[s];
					if (h >= 0 && k >= 0) {
						train_route[indexOfRoute].sequence_of_block_sections[h].arcs_in_signalling_block_section[k].endNode.station = 1;
						train_route[indexOfRoute].sequence_of_block_sections[h].arcs_in_signalling_block_section[k].speedInBraking = 0;
					}
				}

				// Determination of the Movement Authority that has been previouly respected by the train
				//(especially if it stopped at a given EoA)
				// determining Last_MA_StoppedAt for the train
				if ((instant_train_speed[i - 1] == 0) &&
					(this->BrakingForEoA == 1) &&
					(IsTrainStoppedForEoA == 0) &&
					(this->Last_Received_MA.RelativePosEoA - instant_spatial_position[i - 1] < 4)) {
					Last_MA_StoppedAt = Last_Received_MA;
					IsTrainStoppedForEoA = true;
					cout << "IsTrainStoppedForEoA" << endl;
				}

				// Calculating the most severe value of Train Braking Distance at time instant i
				double Braking_Distance = 0;
				// Braking_Distance = BraKDistCompWithLists(instant_spatial_position[i - 1], instant_train_speed[i - 1], (i - 1), train_route[indexOfRoute].sequence_of_block_sections, train_route[indexOfRoute].N_Block_Sections);
				Braking_Distance = europeanVitalComputerWithListsImproved(instant_spatial_position[i - 1], instant_train_speed[i - 1], (i - 1), train_route[indexOfRoute].sequence_of_block_sections, train_route[indexOfRoute].N_Block_Sections);
				// This condition is a fallback option when unrealistic objective points are returned by the BraKDistComp function
				/*if ((Xobmin<Xob[i-1])&&(Vobmin>Vob[i-1])){
				Xobmin=Xob[i-1]; Vobmin=Vob[i-1];
				Braking_Distance=BX[i-1];
				}*/

				// Acceleration phase
				if (((instant_spatial_position[i - 1] < (Braking_Distance - 0.0001)) &&
					 (instant_train_speed[i - 1] < V_lim))
					/*||((instant_spatial_position[i-1]>Braking_Distance)&&(instant_train_speed[i-1]<Vobmin))*/) {
					cout << "Acceleration phase of " << this->trainDescription << endl;
					instant_train_speed[i] = instant_train_speed[i - 1] + (tractiveEffort(instant_train_speed[i - 1]) - total_train_resistances(instant_train_speed[i - 1], As.gradient, As.curvature)) / (total_train_mass * massFactor) * timestep;
					cout << "Before updating the instant speed  of train " << this->trainDescription << "is calculated at " << to_string(instant_train_speed[i])
						 << "\n\t the tractive effort is " << to_string(tractiveEffort(instant_train_speed[i - 1])) << "\n\t and resistances are " << to_string(total_train_resistances(instant_train_speed[i - 1], As.gradient, As.curvature)) << "\n\t divided by " << to_string((total_train_mass * massFactor) * timestep) << endl;

					if (instant_train_speed[i] < 0)
						instant_train_speed[i] = 0;

					if (instant_train_speed[i] > V_lim)
						instant_train_speed[i] = V_lim;

					instant_spatial_position[i] = instant_spatial_position[i - 1] +
												  ((total_train_mass * massFactor) / (tractiveEffort(instant_train_speed[i - 1]) - total_train_resistances(instant_train_speed[i - 1], As.gradient, As.curvature))) * instant_train_speed[i - 1] * (instant_train_speed[i] - instant_train_speed[i - 1]);
					cout << "instant_train_speed i of " << this->trainDescription << "= " << instant_train_speed[i] << endl;
					cout << "instant_spatial_position  of " << this->trainDescription << "-->>" << instant_spatial_position[i] << endl;
					Eq[i] = 1;
					instant_train_power_consumption[i] = tractiveEffort(instant_train_speed[i - 1]) * instant_train_speed[i - 1];
					train_energy_consumption(i);
					temp = i;
					stop = i;
					BX[i] = Braking_Distance;
					Xob[i] = Xobmin;
					Vob[i] = Vobmin;
					counter = 0;
				}

				/*//Cruising Phase
				else if((instant_spatial_position[i-1]<Braking_Distance)&&(fabs((V_lim-instant_train_speed[i-1]))<2.78*timestep)){
				instant_train_speed[i]=V_lim;    if (instant_train_speed[i]<0)instant_train_speed[i]=0;
				instant_spatial_position[i]=instant_spatial_position[i-1]+instant_train_speed[i-1]*timestep;
				instant_train_power_consumption[i]=total_train_resistances(instant_train_speed[i-1],As.gradient,As.curvature)*instant_train_speed[i-1];    Eq[i]=2;
				temp=i; stop=i; BX[i]=Braking_Distance;Xob[i]=Xobmin; Vob[i]=Vobmin; counter=0;}*/

				// Braking Phase
				else if ((instant_spatial_position[i - 1] >= Braking_Distance) && (instant_spatial_position[i - 1] < train_route[indexOfRoute].x_of_end_node * 1000) && (instant_train_speed[i - 1] > 0)) {
					cout << "Braking phase of " << this->trainDescription << endl;
					counter++;
					double Vobj = 0, Xobj = 0;
					if (counter == 1) {
						Vobj = Vobmin;
						Xobj = Xobmin;
						DrawBrakingCurve(instant_train_speed[i - 1], Vobj, Xobj, train_route[indexOfRoute].sequence_of_block_sections, train_route[indexOfRoute].N_Block_Sections);
						double m1, m2;
						m1 = (instant_train_speed[i - 1] - instant_train_speed[i - 2]) / (instant_spatial_position[i - 1] - instant_spatial_position[i - 2]);		  // angular coefficient of acceleration curve
						m2 = (Vbrak[1] - Vbrak[0]) / (Sbrak[1] - Sbrak[0]);																							  // angular coefficient of braking curve
						instant_spatial_position[i - 1] = (m1 * instant_spatial_position[i - 2] - instant_train_speed[i - 2] + Vbrak[1] - m2 * Sbrak[1]) / (m1 - m2); // Intersection abscissa
						instant_train_speed[i - 1] = instant_train_speed[i - 2] + m1 * (instant_spatial_position[i - 1] - instant_spatial_position[i - 2]);			  // Intersection speed
						for (int t = 0; t <= BrakStep; t++) {
							if ((Vbrak[t] < instant_train_speed[i - 1]) && (Sbrak[t] > instant_spatial_position[i - 1])) {
								brakingPoint = t;
								break;
							}
						}
						instant_train_speed[i] = Vbrak[brakingPoint + counter - 1];
						if (instant_train_speed[i] < 0)
							instant_train_speed[i] = 0;
						instant_spatial_position[i] = Sbrak[brakingPoint + counter - 1];
						Eq[i] = 55;
					} else {
						Eq[i] = 52;
						instant_train_speed[i] = Vbrak[brakingPoint + counter - 1];
						if (instant_train_speed[i] < 0)
							instant_train_speed[i] = 0;
						instant_spatial_position[i] = Sbrak[brakingPoint + counter - 1];
						if (instant_spatial_position[i] < instant_spatial_position[i - 1]) {
							counter = 1;
							Vobj = Vobmin;
							Xobj = Xobmin;
							DrawBrakingCurve(instant_train_speed[i - 1], Vobj, Xobj, train_route[indexOfRoute].sequence_of_block_sections, train_route[indexOfRoute].N_Block_Sections);
							for (int t = 0; t <= BrakStep; t++) {
								if ((Vbrak[t] < instant_train_speed[i - 1]) && (Sbrak[t] > instant_spatial_position[i - 1])) {
									brakingPoint = t;
									break;
								}
							}
							instant_train_speed[i] = Vbrak[brakingPoint + counter - 1];
							if (instant_train_speed[i] < 0)
								instant_train_speed[i] = 0;

							instant_spatial_position[i] = Sbrak[brakingPoint + counter - 1];
						}

						// Condition to state if because of a too steep gradient during braking we reach a speed lower than Vobmin that force the train to reaccelerate afterwards (the reacelleration is part of the braking curve in this case)
						if (instant_train_speed[i] < Vobmin) {
							this->GradientExceptionInBraking = true;
						} else {
							this->GradientExceptionInBraking = false;
						}
					}

					// if two braking curves are both restrictive
					/*if ((Xobj!=Xobmin)||(Vobj!=Vobmin)||((Xobj!=Xobmin)&&(Vobj!=Vobmin))){counter=1;
					Xobj=Xobmin;   Vobj=Vobmin;   Eq[i]=53;
					DrawBrakingCurve(instant_train_speed[i-1],Vobj,Xobj,signalling_block_sections,Blocks);
					for (int t=0;t<BrakStep;t++){
					if((Vbrak[t]<instant_train_speed[i-1])&&(Sbrak[t]>instant_spatial_position[i-1])){ brakingPoint=t; break;}}
					instant_train_speed[i]=Vbrak[brakingPoint+counter-1];    if (instant_train_speed[i]<0)instant_train_speed[i]=0;
					instant_spatial_position[i]=Sbrak[brakingPoint+counter-1];}*/

					instant_train_power_consumption[i] = -(brakingEffort((i - temp - 1) * timestep) + gradient_resistances(As.gradient) + curvature_resistances(As.curvature) - total_train_resistances(instant_train_speed[i - 1], As.gradient, As.curvature)) * instant_train_speed[i - 1]; /*Eq[i]=3;*/
					train_energy_consumption(i);
					stop = i;
					BX[i] = Braking_Distance;
					Xob[i] = Xobmin;
					Vob[i] = Vobmin;
				}

				/*//Brake if the train enters next Arc with a speed higher than the Arc speed limit
				else if ((instant_train_speed[i-1]>V_lim)){
				instant_train_speed[i]=instant_train_speed[i-1]+(-brakingEffort((i-temp-1)*timestep)-(gradient_resistances(As.gradient)+curvature_resistances(As.curvature)))/(total_train_mass*massFactor)*timestep;     if (instant_train_speed[i]<0)instant_train_speed[i]=0;
				instant_spatial_position[i]=instant_spatial_position[i-1]+((total_train_mass*massFactor)/(-brakingEffort((i-temp-1)*timestep)-(gradient_resistances(As.gradient)+curvature_resistances(As.curvature))))*instant_train_speed[i-1]*(instant_train_speed[i]-instant_train_speed[i-1]);
				instant_train_power_consumption[i]=-(brakingEffort((i-temp-1)*timestep)+gradient_resistances(As.gradient)+curvature_resistances(As.curvature)-total_train_resistances(instant_train_speed[i-1],As.gradient,As.curvature))*instant_train_speed[i-1];   Eq[i]=4;
				BX[i]=Braking_Distance;Xob[i]=Xobmin; Vob[i]=Vobmin; stop=i; counter=0;}*/

				// Cruising Phase
				else if ((instant_spatial_position[i - 1] < Braking_Distance) &&
						 (instant_train_speed[i - 1] >= V_lim) &&
						 instant_train_speed[i - 1] > 0) {
					cout << "Cruising phase of " << this->trainDescription << endl;

					if (instant_train_speed[i - 1] > V_lim) {
						instant_train_speed[i - 1] = V_lim;
						instant_spatial_position[i - 1] = instant_spatial_position[i - 1];
						instant_train_power_consumption[i - 1] = total_train_resistances(instant_train_speed[i - 1], As.gradient, As.curvature) * instant_train_speed[i - 1];
						train_energy_consumption(i - 1);
						instant_train_speed[i] = V_lim;
						instant_spatial_position[i] = instant_spatial_position[i - 1] + instant_train_speed[i - 1] * timestep;
					} else {
						instant_train_speed[i] = V_lim;
						instant_spatial_position[i] = instant_spatial_position[i - 1] + instant_train_speed[i - 1] * timestep;
					}
					instant_train_power_consumption[i] = total_train_resistances(instant_train_speed[i - 1], As.gradient, As.curvature) * instant_train_speed[i - 1];
					Eq[i] = 12;
					train_energy_consumption(i);
					temp = i;
					stop = i;
					BX[i] = Braking_Distance;
					Xob[i] = Xobmin;
					Vob[i] = Vobmin;
					counter = 0;
				}

				/**************************************************************************************************************************/

				// Release of signalling system when the train exits simulation
				if (instant_spatial_position[i - 1] >= train_route[indexOfRoute].x_of_end_node * 1000) {
					End_Time = i - 1;
					instant_train_speed[i] = 0;
					instant_spatial_position[i] = -9999;
					instant_train_power_consumption[i] = -9999;
					Xobmin = train_route[indexOfRoute].x_of_end_node * 1000;
					Vobmin = train_route[indexOfRoute].sequence_of_block_sections[train_route[indexOfRoute].N_Block_Sections - 1].arcs_in_signalling_block_section[train_route[indexOfRoute].sequence_of_block_sections[train_route[indexOfRoute].N_Block_Sections - 1].total_arcs - 1].speedInBraking; /*This is the speedInBraking of the last Arc of the Route*/
					relLastSectionMixedSignalling(train_route[indexOfRoute].sequence_of_block_sections[train_route[indexOfRoute].N_Block_Sections - 1].ID);
					OutOfSimulation = true;
				}
				/*else if (instant_spatial_position[i-1]<0){instant_train_speed[i]=0; instant_spatial_position[i]=-9999; instant_train_power_consumption[i]=-9999; }*/

				/**************************************************************************************************************************/

				// Standing Train Conditions
				if (instant_train_speed[i - 1] == 0) {

					// Stopping at a station for an estabilished dwell time
					if (((As.endNode.X * 1000 - instant_spatial_position[i - 1]) < 4) && (As.endNode.station == 1)) {
						Eq[i] = 5;
						double stoptime = 0;
						double dep_time = 0;
						counter = 0;
						for (int s = 0; s < numStations; s++) {
							if (Stations[s].stationName == As.endNode.stationName) {
								stoptime = Stations[s].StopTime;
								dep_time = ScheduledDepartures[s];
								Stations[s].StepStopped++;
								recordCurrentServiceStopArrival(i);
								if (Stations[s].StepStopped > Stations[s].StopTime)
									stoptime = 0;
							}
						}

						// In case the train is also stopped close to the station because of ETCS EoA, then check if the EoA is still valid
						if (this->IsTrainStoppedForEoA == 1) {
							this->checkIfMovementAuthorityStillValid(instant_train_speed[i - 1], instant_spatial_position[i - 1], Bs);
						}

						// Check if in this Station the train has also to respect a given OL Order
						if (As.endNode.respectOrder == 1) {
							bool Condition_To_Stop_Train = Conditions_To_Stop_Train_For_OL_Order(As.endNode.respectOrder, As.endNode.indexOrderList, Bs);
							// The train can leave the station only if the previous train has passed, it has satisfied the min dwell time and the signal aspect is non restrictive and there is not condition to stop the train becasue of an ETCS 3 EoA
							if ((Condition_To_Stop_Train == 0) && ((i - stop) > stoptime) && (i > dep_time) && (IsTrainStoppedForEoA == 0)) {
								instant_train_speed[i] = 0.0001;
								instant_spatial_position[i] = As.endNode.X * 1000 + 0.0001;
							}
							// Otherwise the train has to remain stopped at the station
							else {
								instant_train_speed[i] = 0;
								instant_spatial_position[i] = As.endNode.X * 1000 - 0.0001;
							}
						}
						// If the station Node does not have to respect a specific train order, the train has to be stopped at the station for the dwell time and can depart only after that ensuring that it does not have to respect any ETCS 3 EoA at that station
						else {
							if (((i - stop) <= stoptime) || (i <= dep_time) || (this->IsTrainStoppedForEoA == 1)) {
								instant_train_speed[i] = 0;
								instant_spatial_position[i] = As.endNode.X * 1000 - 0.0001;
							} else {
								instant_train_speed[i] = 0.0001;
								instant_spatial_position[i] = As.endNode.X * 1000 + 0.0001;
								BrakingForEoA = false; // setting the variable Braking for EoA to false since the train is starting to move again
							}
						}
						instant_train_power_consumption[i] = 0;
						train_energy_consumption(i);
					}

					// Stopping at a checkpoint Node where a certain train order must be respected
					else if ((As.endNode.X * 1000 - instant_spatial_position[i - 1] < 4) && (As.endNode.respectOrder == 1)) {
						counter = 0;
						bool Cond_To_Stop_Train = Conditions_To_Stop_Train_For_OL_Order(As.endNode.respectOrder, As.endNode.indexOrderList, Bs);

						// In case the train is also stopped close to the checkpoint because of ETCS 3 EoA, then check if the EoA is still valid
						if (this->IsTrainStoppedForEoA == 1) {
							this->checkIfMovementAuthorityStillValid(instant_train_speed[i - 1], instant_spatial_position[i - 1], Bs);
						}

						if ((Cond_To_Stop_Train == 0) && (IsTrainStoppedForEoA == 0)) { // the checkpoint Node can coincide with an ETCS3 EoA, that is why the train can depart from this Node only after having verified that the checkpoint does not coincide with an ETCS3 EoA or that the EoA has been deleted
							Eq[i] = 532;
							instant_train_speed[i] = 0.0001;
							instant_spatial_position[i] = As.endNode.X * 1000 + 0.0001;
							BrakingForEoA = false; // setting back to false BrakingForEoa since the train starts to move again
						}
						// otherwise it has to be stopped
						else {
							Eq[i] = 534;
							instant_train_speed[i] = 0;
							instant_spatial_position[i] = As.endNode.X * 1000 - 0.0001;
						}

						instant_train_power_consumption[i] = 0;
						train_energy_consumption(i);
					}

					// Stopping at a Red Signal
					else if (As.endNode.X * 1000 - instant_spatial_position[i - 1] < 4) {
						Eq[i] = 6;
						// In case the train is also stopped close to the signal because of ETCS EoA, then check if the EoA is still valid
						if (this->IsTrainStoppedForEoA == 1) {
							this->checkIfMovementAuthorityStillValid(instant_train_speed[i - 1], instant_spatial_position[i - 1], Bs);
						}

						// also a signal can coincide with an ETCS3 EoA, that is why a train must stop at the signal also if this not red but and ETCS 3 EoA exists
						if ((!strcmp(Bs.state, "red")) || (this->IsTrainStoppedForEoA == 1)) {
							instant_spatial_position[i] = As.endNode.X * 1000 - 0.0001;
							instant_train_speed[i] = 0;
						} else {
							instant_spatial_position[i] = As.endNode.X * 1000 + 0.0001;
							instant_train_speed[i] = 0.0001;
							BrakingForEoA = false; // setting BrakingForEoA to false since the train starts moving again
						}
						instant_train_power_consumption[i] = 0;
						train_energy_consumption(i);
						counter = 0;
					}
					// if noone of the previous conditions is verified it means that the train is stopped at an ETCS3 EoA
					else if (this->IsTrainStoppedForEoA == 1) {
						// Check if the EoA given at the previous instant is still valid
						this->checkIfMovementAuthorityStillValid(instant_train_speed[i - 1], instant_spatial_position[i - 1], Bs);
						if (this->IsTrainStoppedForEoA == 1) {
							instant_train_speed[i] = 0;
							instant_spatial_position[i] = Last_MA_StoppedAt.RelativePosEoA - 0.0001;
						} else {
							instant_spatial_position[i] = Last_MA_StoppedAt.RelativePosEoA + 0.0001;
							instant_train_speed[i] = 0.0001;
							BrakingForEoA = false; // setting BrakingForEoA to false since the train starts moving again
						}
						instant_train_power_consumption[i] = 0;
						train_energy_consumption(i);
						counter = 0;
					}

					// if the speed limit on the Arc is 0: this case happens only when the BACC is implemented
					if (V_lim == 0) {
						counter = 0;
						Eq[i] = 958;
						instant_train_speed[i] = 0;
						instant_spatial_position[i] = instant_spatial_position[i - 1];
						instant_train_power_consumption[i] = 0;
						train_energy_consumption(i);
					}
				}

				// Update the status of Infrastructure elements on the route of the train
				if (i > 0) {
					UpdateInfrastructureElementStatus(train_route[indexOfRoute].InfrastructureElements, i, instant_spatial_position[i - 1], instant_spatial_position[i], instant_train_speed[i - 1], trainDescription);
				}

				// if the train has just entered the track therefore its position instant_spatial_position[i] is higher than 1 meter and lower than 3 meters VirtualQ[ID-1] must be updated to ID
				Update_LastEnteredTrain_For_All_OL(i, train_route[indexOfRoute].sequence_of_block_sections[Previous_Block_Index]);
			}
			/***********************************************************************************************************************/
			else {
				instant_train_speed[i] = 0.0;
				instant_spatial_position[i] = Start_Node_X * 1000;
				instant_train_power_consumption[i] = 0.0;
			}
			// cout << instant_spatial_position[i] << " " << As.gradient << " " << As.curvature << "\n";
		}
	}

	// Function to Simulate the Train Trajectory
	virtual void trajectoryComputationIncludingMovingBlock(int time_seconds, double signalCode1, double signalCode2, double signalCode3) {
		if (OutOfSimulation == 0) {
			// A broken-down train holds its last state for the whole incident
			// window; entrance processing is also suspended, so a train whose
			// window covers its entry time simply enters after the window.
			if (time_seconds > 0 && Incident_Holds_Train(trainDescription, time_seconds)) {
				instant_train_speed[time_seconds] = 0;
				instant_spatial_position[time_seconds] = instant_spatial_position[time_seconds - 1];
				instant_train_power_consumption[time_seconds] = 0;
				train_energy_consumption(time_seconds);
				Eq[time_seconds] = 5;
				return;
			}

			// Determine if the Train can enter the route
			checkEntrance(time_seconds, train_route[indexOfRoute].sequence_of_block_sections);
			if ((time_seconds >= departure_time) && (CanEnter == 1)) {
				double V_lim;
				int U;
				U = time_seconds;
				int Previous_Block_Index = 0;
				Arc As; // use local variable instead of Train class member (faster simulation)
				string Occupied_TDS;

				// Determination of the right Block Section
				for (int h = 0; h < train_route[indexOfRoute].N_Block_Sections; h++) {
					if ((instant_spatial_position[time_seconds - 1] < train_route[indexOfRoute].sequence_of_block_sections[h].end_node.X * 1000) &&
						(instant_spatial_position[time_seconds - 1] >= train_route[indexOfRoute].sequence_of_block_sections[h].start_node.X * 1000)) {
						Bs = train_route[indexOfRoute].sequence_of_block_sections[h];
						instant_block_section_occupied[time_seconds] = Bs.ID;

						if (h == 0)
							Previous_Block_Index = h;
						else {
							Previous_Block_Index = h - 1;
							break;
						}
					}
				}

				// Determine the TDS which is currently occupied in the Block section Bs by the nose of the train
				Occupied_TDS = DetermineOccupiedTDS((time_seconds - 1), train_route[indexOfRoute].reversed_direction);

				// Determination of the right running Arc
				for (int j = 0; j < Bs.total_arcs; j++) {
					if ((instant_spatial_position[time_seconds - 1] < Bs.arcs_in_signalling_block_section[j].endNode.X * 1000) &&
						(instant_spatial_position[time_seconds - 1] >= Bs.arcs_in_signalling_block_section[j].startNode.X * 1000))
						As = Bs.arcs_in_signalling_block_section[j];
					// logger.Log("\nkkkk");
					////	logger.Log(to_string(instant_spatial_position[time_seconds - 1]));
				}
				// Determination of the right speed limit to observe
				double Speeds[3];
				Speeds[0] = max_train_speed;
				Speeds[1] = As.speedLimit;
				Speeds[2] = As.signalSpeedLimit;
				V_lim = Speeds[0];
				for (int k = 0; k < 3; k++) {
					if (Speeds[k] <= V_lim)
						V_lim = Speeds[k];
				}

				// Imposing Station Nodes via cached indices
				for (int s = 0; s < numStations; s++) {
					int h = stationBlockSection[s];
					int k = stationArc[s];
					if (h >= 0 && k >= 0) {
						auto& Arc = train_route[indexOfRoute].sequence_of_block_sections[h].arcs_in_signalling_block_section[k];
						if (Arc.endNode.X * 1000 >= instant_spatial_position[time_seconds - 1]) {
							Arc.endNode.station = 1;
							Arc.speedInBraking = 0;
						}
					}
				}

				// Determination of the Movement Authority that has been previouly respected by the train (especially if it stopped at a given EoA)
				// determining Last_MA_StoppedAt for the train
				if ((instant_train_speed[time_seconds - 1] == 0) &&
					(this->BrakingForEoA == 1) &&
					(IsTrainStoppedForEoA == 0) &&
					(this->Last_Received_MA.RelativePosEoA - instant_spatial_position[time_seconds - 1] < 4)) {

					Last_MA_StoppedAt = Last_Received_MA;
					IsTrainStoppedForEoA = true;
				}

				// Calculating the most severe value of Train Braking Distance at time instant i
				double Braking_Distance = 0;
				// Braking_Distance = BraKDistCompWithLists(instant_spatial_position[i - 1], instant_train_speed[i - 1], (i - 1), train_route[indexOfRoute].sequence_of_block_sections, train_route[indexOfRoute].N_Block_Sections);
				Braking_Distance = europeanVitalComputerWithListsImproved(
					instant_spatial_position[time_seconds - 1],
					instant_train_speed[time_seconds - 1],
					(time_seconds - 1),
					train_route[indexOfRoute].sequence_of_block_sections,
					train_route[indexOfRoute].N_Block_Sections);

				// Acceleration phase
				if (((instant_spatial_position[time_seconds - 1] < (Braking_Distance - 0.0001)) &&
					 (instant_train_speed[time_seconds - 1] < V_lim)) /*||((instant_spatial_position[i-1]>Braking_Distance)&&(instant_train_speed[i-1]<Vobmin))*/) {

					// The one below in between comments is the previous/original version of the code which did not consider the train crusing when instead the Traction Surplus with respect to the resistance is equal to 0
					/*
					instant_train_speed[time_seconds] = instant_train_speed[time_seconds - 1] + (tractiveEffort(instant_train_speed[time_seconds - 1]) -
						total_train_resistances(instant_train_speed[time_seconds - 1], As.gradient, As.curvature)) / (total_train_mass*massFactor)*timestep;

					if (instant_train_speed[time_seconds] < 0)
						instant_train_speed[time_seconds] = 0;

					if (instant_train_speed[time_seconds] > V_lim)
						instant_train_speed[time_seconds] = V_lim;

					instant_spatial_position[time_seconds] =
						instant_spatial_position[time_seconds - 1] +
						((total_train_mass*massFactor) / (tractiveEffort(instant_train_speed[time_seconds - 1]) -
							total_train_resistances(instant_train_speed[time_seconds - 1], As.gradient, As.curvature)))*
						instant_train_speed[time_seconds - 1] *
						(instant_train_speed[time_seconds] -
							instant_train_speed[time_seconds - 1]); */

					// This part of the code below instead has been updated to consider the train crusing (and not accelerating like before) is the Traction Surplus with respect to the motion resistances is null. In other words the train is now considered crusing when traction and resistance forces are equal.
					double Traction_Surplus = 0;
					Traction_Surplus = tractiveEffort(instant_train_speed[time_seconds - 1]) - total_train_resistances(instant_train_speed[time_seconds - 1], As.gradient, As.curvature);

					// Compute instant speed
					instant_train_speed[time_seconds] = instant_train_speed[time_seconds - 1] + (Traction_Surplus) / (total_train_mass * massFactor) * timestep;

					if (instant_train_speed[time_seconds] < 0)
						instant_train_speed[time_seconds] = 0;

					if (instant_train_speed[time_seconds] > V_lim)
						instant_train_speed[time_seconds] = V_lim;

					double Scruis = 0;
					Scruis = instant_spatial_position[time_seconds - 1] + instant_train_speed[time_seconds] * timestep;

					// Compute spatial position of the train at current time based on Traction Surplus sign (i.e. whether it is 0, negative or positive)
					if (Traction_Surplus == 0) {
						instant_spatial_position[time_seconds] = Scruis;
					} else {
						instant_spatial_position[time_seconds] = instant_spatial_position[time_seconds - 1] + ((total_train_mass * massFactor) / (Traction_Surplus)) * instant_train_speed[time_seconds - 1] *
																												  (instant_train_speed[time_seconds] - instant_train_speed[time_seconds - 1]);

						if (instant_spatial_position[time_seconds] < Scruis) {
							instant_spatial_position[time_seconds] = instant_spatial_position[time_seconds - 1] + instant_train_speed[time_seconds - 1] * timestep + 0.5 * ((instant_train_speed[time_seconds] - instant_train_speed[time_seconds - 1]) / timestep) * pow(timestep, 2);
						}
					}

					Eq[time_seconds] = 1;
					instant_train_power_consumption[time_seconds] = tractiveEffort(instant_train_speed[time_seconds - 1]) * instant_train_speed[time_seconds - 1];
					train_energy_consumption(time_seconds);
					temp = time_seconds;
					stop = time_seconds;
					BX[time_seconds] = Braking_Distance;
					Xob[time_seconds] = Xobmin;
					Vob[time_seconds] = Vobmin;
					counter = 0;
					GradientExceptionInBraking = false;
				}

				/*//Cruising Phase
				else if((instant_spatial_position[i-1]<Braking_Distance)&&(fabs((V_lim-instant_train_speed[i-1]))<2.78*timestep)){
				instant_train_speed[i]=V_lim;    if (instant_train_speed[i]<0)instant_train_speed[i]=0;
				instant_spatial_position[i]=instant_spatial_position[i-1]+instant_train_speed[i-1]*timestep;
				instant_train_power_consumption[i]=total_train_resistances(instant_train_speed[i-1],As.gradient,As.curvature)*instant_train_speed[i-1];    Eq[i]=2;
				temp=i; stop=i; BX[i]=Braking_Distance;Xob[i]=Xobmin; Vob[i]=Vobmin; counter=0;}*/

				// Braking Phase
				else if ((instant_spatial_position[time_seconds - 1] >= Braking_Distance) &&
						 (instant_spatial_position[time_seconds - 1] < train_route[indexOfRoute].x_of_end_node * 1000) &&
						 (instant_train_speed[time_seconds - 1] > 0)) {

					counter++;
					double Vobj = 0, Xobj = 0;

					if (counter == 1) {
						Vobj = Vobmin;
						Xobj = Xobmin;
						DrawBrakingCurve(instant_train_speed[time_seconds - 1], Vobj, Xobj, train_route[indexOfRoute].sequence_of_block_sections, train_route[indexOfRoute].N_Block_Sections);
						double m1, m2;
						m1 = (instant_train_speed[time_seconds - 1] - instant_train_speed[time_seconds - 2]) / (instant_spatial_position[time_seconds - 1] - instant_spatial_position[time_seconds - 2]); // angular coefficient of acceleration curve
						m2 = (Vbrak[1] - Vbrak[0]) / (Sbrak[1] - Sbrak[0]);																																  // angular coefficient of braking curve
						instant_spatial_position[time_seconds - 1] = (m1 * instant_spatial_position[time_seconds - 2] - instant_train_speed[time_seconds - 2] + Vbrak[1] - m2 * Sbrak[1]) / (m1 - m2);	  // Intersection abscissa
						instant_train_speed[time_seconds - 1] = instant_train_speed[time_seconds - 2] + m1 * (instant_spatial_position[time_seconds - 1] - instant_spatial_position[time_seconds - 2]);	  // Intersection speed

						for (int t = 0; t <= BrakStep; t++) {
							if ((Vbrak[t] < instant_train_speed[time_seconds - 1]) && (Sbrak[t] > instant_spatial_position[time_seconds - 1])) {
								brakingPoint = t;
								break;
							}
						}
						instant_train_speed[time_seconds] = Vbrak[brakingPoint + counter - 1];
						if (instant_train_speed[time_seconds] < 0)
							instant_train_speed[time_seconds] = 0;

						instant_spatial_position[time_seconds] = Sbrak[brakingPoint + counter - 1];
						Eq[time_seconds] = 55;
					} else {
						Eq[time_seconds] = 52;
						instant_train_speed[time_seconds] = Vbrak[brakingPoint + counter - 1];
						if (instant_train_speed[time_seconds] < 0)
							instant_train_speed[time_seconds] = 0;
						instant_spatial_position[time_seconds] = Sbrak[brakingPoint + counter - 1];
						if (instant_spatial_position[time_seconds] < instant_spatial_position[time_seconds - 1]) {
							counter = 1;
							Vobj = Vobmin;
							Xobj = Xobmin;
							DrawBrakingCurve(instant_train_speed[time_seconds - 1], Vobj, Xobj, train_route[indexOfRoute].sequence_of_block_sections, train_route[indexOfRoute].N_Block_Sections);
							for (int t = 0; t <= BrakStep; t++) {
								if ((Vbrak[t] < instant_train_speed[time_seconds - 1]) && (Sbrak[t] > instant_spatial_position[time_seconds - 1])) {
									brakingPoint = t;
									break;
								}
							}
							instant_train_speed[time_seconds] = Vbrak[brakingPoint + counter - 1];
							if (instant_train_speed[time_seconds] < 0)
								instant_train_speed[time_seconds] = 0;
							instant_spatial_position[time_seconds] = Sbrak[brakingPoint + counter - 1];
							Eq[time_seconds] = 53;
						}

						// Condition to state if because of a too steep gradient during braking we reach a speed lower than Vobmin that force the train to reaccelerate afterwards (the reacelleration is part of the braking curve in this case)
						if (instant_train_speed[time_seconds] < Vobmin) {
							this->GradientExceptionInBraking = true;
						} else {
							this->GradientExceptionInBraking = false;
						}
					}

					// if two braking curves are both restrictive
					/*if ((Xobj!=Xobmin)||(Vobj!=Vobmin)||((Xobj!=Xobmin)&&(Vobj!=Vobmin))){counter=1;
					Xobj=Xobmin;   Vobj=Vobmin;   Eq[i]=53;
					DrawBrakingCurve(instant_train_speed[i-1],Vobj,Xobj,signalling_block_sections,Blocks);
					for (int t=0;t<BrakStep;t++){
					if((Vbrak[t]<instant_train_speed[i-1])&&(Sbrak[t]>instant_spatial_position[i-1])){ brakingPoint=t; break;}}
					instant_train_speed[i]=Vbrak[brakingPoint+counter-1];    if (instant_train_speed[i]<0)instant_train_speed[i]=0;
					instant_spatial_position[i]=Sbrak[brakingPoint+counter-1];}*/

					instant_train_power_consumption[time_seconds] =
						-(brakingEffort((time_seconds - temp - 1) * timestep) +
						  gradient_resistances(As.gradient) +
						  curvature_resistances(As.curvature) -
						  total_train_resistances(instant_train_speed[time_seconds - 1], As.gradient, As.curvature)) *
						instant_train_speed[time_seconds - 1]; /*Eq[i]=3;*/
					train_energy_consumption(time_seconds);
					stop = time_seconds;
					BX[time_seconds] = Braking_Distance;
					Xob[time_seconds] = Xobmin;
					Vob[time_seconds] = Vobmin;
				}

				/*//Brake if the train enters next Arc with a speed higher than the Arc speed limit
				else if ((instant_train_speed[i-1]>V_lim)){
				instant_train_speed[i]=instant_train_speed[i-1]+(-brakingEffort((i-temp-1)*timestep)-(gradient_resistances(As.gradient)+curvature_resistances(As.curvature)))/(total_train_mass*massFactor)*timestep;     if (instant_train_speed[i]<0)instant_train_speed[i]=0;
				instant_spatial_position[i]=instant_spatial_position[i-1]+((total_train_mass*massFactor)/(-brakingEffort((i-temp-1)*timestep)-(gradient_resistances(As.gradient)+curvature_resistances(As.curvature))))*instant_train_speed[i-1]*(instant_train_speed[i]-instant_train_speed[i-1]);
				instant_train_power_consumption[i]=-(brakingEffort((i-temp-1)*timestep)+gradient_resistances(As.gradient)+curvature_resistances(As.curvature)-total_train_resistances(instant_train_speed[i-1],As.gradient,As.curvature))*instant_train_speed[i-1];   Eq[i]=4;
				BX[i]=Braking_Distance;Xob[i]=Xobmin; Vob[i]=Vobmin; stop=i; counter=0;}*/

				// Cruising Phase
				else if ((instant_spatial_position[time_seconds - 1] < Braking_Distance) && (instant_train_speed[time_seconds - 1] >= V_lim) && instant_train_speed[time_seconds - 1] > 0) {
					if (instant_train_speed[time_seconds - 1] > V_lim) {
						instant_train_speed[time_seconds - 1] = V_lim;
						instant_spatial_position[time_seconds - 1] = instant_spatial_position[time_seconds - 1];
						instant_train_power_consumption[time_seconds - 1] = total_train_resistances(instant_train_speed[time_seconds - 1], As.gradient, As.curvature) * instant_train_speed[time_seconds - 1];
						train_energy_consumption(time_seconds - 1);
						instant_train_speed[time_seconds] = V_lim;
						instant_spatial_position[time_seconds] = instant_spatial_position[time_seconds - 1] + instant_train_speed[time_seconds - 1] * timestep;
					} else {
						instant_train_speed[time_seconds] = V_lim;
						instant_spatial_position[time_seconds] = instant_spatial_position[time_seconds - 1] + instant_train_speed[time_seconds - 1] * timestep;
					}
					instant_train_power_consumption[time_seconds] = total_train_resistances(instant_train_speed[time_seconds - 1], As.gradient, As.curvature) * instant_train_speed[time_seconds - 1];
					Eq[time_seconds] = 12;
					train_energy_consumption(time_seconds);
					temp = time_seconds;
					stop = time_seconds;
					BX[time_seconds] = Braking_Distance;
					Xob[time_seconds] = Xobmin;
					Vob[time_seconds] = Vobmin;
					counter = 0;
					GradientExceptionInBraking = false;
				}

				// Determination of Train Coupling phase for Virtual Coupling
				if ((IsTrainCoupling == 1) && ((IsTrainInFollowingMode == 0) || (IsInUnintentionalDecoupling == 1)) &&
					(abs(instant_train_speed[time_seconds] - this->Predicted_MA_To_CoupleAt.TrainInfo.TrainSpeed) < 0.278) &&
					(abs(this->Predicted_MA_To_CoupleAt.RelativePosEoA - instant_spatial_position[time_seconds - 1]) <= 30)) {
					// When the condition above occurs then the train is transitioning towards the following mode
					IsInUnintentionalDecoupling = false;
					IsTrainCoupling = false;														   // So we set the ending of the Coupling transition period
					IsTrainInFollowingMode = true;													   // And the activation of the following mode
					LeadingTrainInFollowingMode = Predicted_MA_To_CoupleAt.TrainInfo.trainDescription; // Setting the trainDescription of the train which is leading during Virtual Coupling
																									   // Sending a message that train is in following mode
					cout << "At time instant " << time_seconds << " train " << trainDescription << " is virtually coupled to train " << LeadingTrainInFollowingMode << "\n";

					//-------------------------------
					// GUI notification
					VCmsgTimestep.push_back(time_seconds);
					VCmsgTrain.push_back(trainDescription);
					VCmsgText.push_back("Train is now virtually\ncoupled to the leader");
					VCmsgItems.push_back(nullptr);
					//-------------------------------
				}

				// Determination of Train Uncoupling from the leader when it is in following mode and the route diverges from the leader's
				if ((IsTrainInFollowingMode == 1) && (IsTrainDecoupling == 1)) {
					// When the train is decoupling it can be considered totally uncoupled and not anymore in following mode when the tale of the leading train has fully crossed the splitting infrastructure element and the corresponding MA is communicated with type="TrainUncoupled"
					if ((Predicted_MA_To_DecoupleAt.TrainInfo.trainDescription != "None") &&
						(Predicted_MA_To_DecoupleAt.TrainInfo.trainDescription == LeadingTrainInFollowingMode) &&
						(Predicted_MA_To_DecoupleAt.type == "TrainUncoupled")) {

						cout << "At time instant " << time_seconds << " train " << trainDescription << " intentionally decoupled from train " << LeadingTrainInFollowingMode << " because of diverging routes\n";

						//-------------------------------
						// GUI notification
						VCmsgTimestep.push_back(time_seconds);
						VCmsgTrain.push_back(trainDescription);
						VCmsgText.push_back("Train is now decoupled");
						VCmsgItems.push_back(nullptr);
						//-------------------------------

						MovementAuthority ResetPredictedMAToDecoupleAt;
						IsTrainInFollowingMode = false;
						LeadingTrainInFollowingMode = "None";
						IsTrainDecoupling = false;
						Predicted_MA_To_DecoupleAt = ResetPredictedMAToDecoupleAt;
						IsInUnintentionalDecoupling = false;
					}
				}

				/*******************  Virtual Coupling Following mode equations  ***************************************************************************************************************************/

				if ((IsTrainInFollowingMode == 1) && (IsTrainDecoupling == 0) && (IsInUnintentionalDecoupling == 0)) {
					CounterFollowingMode++; // This sets the number of time steps that the train is in following mode
					// if the train has outdistanced for more than 30 meters from the EoA of the train ahead, then interrupt the following mode and let the train to go free
					if ((Last_Received_MA.TrainInfo.trainDescription == LeadingTrainInFollowingMode) &&
						(Last_Received_MA.type == "TrainEnd") &&
						(Last_Received_MA.typePart == "Tale") &&
						(Last_Received_MA.RelativePosEoA - instant_spatial_position[time_seconds - 1] > 30)) {
						IsInUnintentionalDecoupling = true; // Originally I had IsTrainInFollowingMode = false;
						CounterFollowingMode = 0;			// set also the CounterFollowingMode to 0 again

						cout << "At time instant " << time_seconds << " train " << trainDescription << " is unintentionally decoupling from train " << LeadingTrainInFollowingMode << " because it could not reach the same speeds\n";

						//-------------------------------
						// GUI notification
						VCmsgTimestep.push_back(time_seconds);
						VCmsgTrain.push_back(trainDescription);
						VCmsgText.push_back("Train is now unintentionally\ndecoupling from the leader");
						VCmsgItems.push_back(nullptr);
						//-------------------------------
					}

					if ((CounterFollowingMode > 1)) {
						// This function is entered only when the train is in Following mode from more than 1 second
						// In the istant the train has started following the train ahead its speed and distance are computed according the normal motion above equations
						double TargetAcceleration = 0;	 // This is the acceleration at the previous instant used by the leading train ahead
						double TargetFollowingSpeed = 0; // This is the target speed to be followed to couple with the train ahead
						double Acc_i = 0;				 // This is the acceleration of this strain at current instant i

						// Setting the target speed
						if (Last_Received_MA.TrainInfo.trainDescription == LeadingTrainInFollowingMode) { // When the train is in following mode the target speed to follow is the speed of the leading train that is given in the LastReceived_MA
							TargetFollowingSpeed = Last_Received_MA.TrainInfo.TrainSpeed;
							TargetAcceleration = Last_Received_MA.TrainInfo.Acceleration;
						} else {
							cout << " Warning: Train " << trainDescription << " is in Following mode with train " << LeadingTrainInFollowingMode << " but receiving most restrictive MAs from train " << Last_Received_MA.TrainInfo.trainDescription << " . Please do investigate...\n ";
						}

						// Computation Acceleration Boundaries of this train given the gradient, curvature and acceleration and deceleration capabilities
						double MaxAchievableAcc = (tractiveEffort(0) - total_train_resistances(instant_train_speed[time_seconds - 1], As.gradient, As.curvature)) / (total_train_mass * massFactor);
						double MinAchievableAcc = ((-total_train_mass * massFactor * max_train_decelaration) - total_train_resistances(instant_train_speed[time_seconds - 1], As.gradient, As.curvature)) / (total_train_mass * massFactor);

						/*double MaxAchievableAcc = (tractiveEffort(instant_train_speed[i - 1]) - total_train_resistances(instant_train_speed[i - 1], As.gradient, As.curvature)) / (total_train_mass*massFactor);
						double MinAchievableAcc = ((-total_train_mass * massFactor * max_train_decelaration) - total_train_resistances(instant_train_speed[i - 1], As.gradient, As.curvature)) / (total_train_mass*massFactor);*/
						/*
						Acc_i = TargetAcceleration;
						instant_train_speed[i] = instant_train_speed[i - 1] + Acc_i * timestep;  //Determining Speed

						if (instant_train_speed[i] < 0) instant_train_speed[i] = 0;   if (instant_train_speed[i] > V_lim) instant_train_speed[i] = V_lim; //Correcting instant_train_speed[i] in case it results lower than 0 or higher than V_lim

						instant_spatial_position[i] = instant_spatial_position[i - 1] + instant_train_speed[i - 1] * timestep;   //Determining distance
						*/

						if ((TargetAcceleration >= MinAchievableAcc) && (TargetAcceleration <= MaxAchievableAcc)) {

							Acc_i = TargetAcceleration; // if the Acceleration of the Leader is within the boundaries of acceleration achievable by this train then Acc_i is the same acceleration of the leader

						}

						else if (TargetAcceleration < MinAchievableAcc) {

							// In case the acceleration of the leader is lower than the minimum possible acceleration (i.e. deceleration)
							// then Acc_i is the minimum possible deceleration according to
							// braking capabilities and conditions of gradient and curvature radius
							Acc_i = MinAchievableAcc;
						}

						else if (TargetAcceleration > MaxAchievableAcc) {
							// in case the Acceleration of the leader is higher than the maximum achievable acceleration
							// of the train given its acceleration capabilities as well as gradient and curvature conditions,
							// then Acc_i is the Maximum achievable acceleration
							Acc_i = MaxAchievableAcc;
						}

						instant_train_speed[time_seconds] = instant_train_speed[time_seconds - 1] + Acc_i * timestep; // Determining Speed

						// Additional constraint on acceleration and speed when the train ahead is braking to ensure that the train does not collide with the train ahead when braking
						if (Acc_i < 0) {
							instant_train_speed[time_seconds] = TargetFollowingSpeed + Acc_i * timestep; // instant_train_speed[i] + MinAchievableAcc * timestep;
						}

						// in case the leader train ahead is stopped (i.e. its acceleration and speed are both equal to 0)
						if ((Acc_i == 0) && (TargetFollowingSpeed == 0)) {
							instant_train_speed[time_seconds] = 0; // Then also the follower train needs to be stopped
						}

						if (instant_train_speed[time_seconds] < 0) {
							instant_train_speed[time_seconds] = 0;
						}

						if (instant_train_speed[time_seconds] > V_lim) {
							instant_train_speed[time_seconds] = V_lim;
						} // Correcting instant_train_speed[i] in case it results lower than 0 or higher than V_lim

						instant_spatial_position[time_seconds] = instant_spatial_position[time_seconds - 1] + instant_train_speed[time_seconds - 1] * timestep; // Determining distance

						// Computing power and Energy
						instant_train_power_consumption[time_seconds] = (Acc_i * total_train_mass * massFactor) * instant_train_speed[time_seconds - 1];
						train_energy_consumption(time_seconds);
						temp = time_seconds;
						stop = time_seconds;
						BX[time_seconds] = Braking_Distance;
						Xob[time_seconds] = Xobmin;
						Vob[time_seconds] = Vobmin;
						counter = 0;
						Eq[time_seconds] = 1002;
					}
				}

				/**************************************************************************************************************************/

				// Release of signalling system when the train exits simulation
				if (instant_spatial_position[time_seconds - 1] >= train_route[indexOfRoute].x_of_end_node * 1000) {
					End_Time = time_seconds - 1;
					instant_train_speed[time_seconds] = 0;
					instant_spatial_position[time_seconds] = -9999;
					instant_train_power_consumption[time_seconds] = -9999;
					Xobmin = train_route[indexOfRoute].x_of_end_node * 1000;
					Vobmin = train_route[indexOfRoute].sequence_of_block_sections[train_route[indexOfRoute].N_Block_Sections - 1].arcs_in_signalling_block_section[train_route[indexOfRoute].sequence_of_block_sections[train_route[indexOfRoute].N_Block_Sections - 1].total_arcs - 1].speedInBraking; /*This is the speedInBraking of the last Arc of the Route*/
					relLastSectionMixedSignalling(train_route[indexOfRoute].sequence_of_block_sections[train_route[indexOfRoute].N_Block_Sections - 1].ID);
					OutOfSimulation = true;
				}
				/*else if (instant_spatial_position[i-1]<0){instant_train_speed[i]=0; instant_spatial_position[i]=-9999; instant_train_power_consumption[i]=-9999; }*/

				/**************************************************************************************************************************/

				// Standing Train Conditions
				if (instant_train_speed[time_seconds - 1] == 0) {

					// In case the train is in following mode and stops behind another train at station (this condition is needed when the train does not stop because of an EoA but because it was in following mode and the train ahead stopped)
					if (IsTrainInFollowingMode == 1) {
						// the content of this if condition is needed to set the train to StoppedForServiceStop behind a train to true
						if (this->StoppedForServiceStop == 0) { // if the train is not yet stopped for a service performed behind another train
																// if the train ahead is stopped at a station and it is the first train on the platform
							if ((Last_Received_MA.TrainInfo.StoppedForServiceStop == 1) &&
								(Last_Received_MA.TrainInfo.ServiceStopBehindATrain == 0) &&
								(Last_Received_MA.type == "TrainEnd")) // from this if statement we can remove the statement relative to TypePart=="TrainEnd" because when the train is coupled in following mode it can get within the safety margin distance from the leader and see only its Front
							{
								for (int s = 0; s < numStations; s++) {
									if (Stations[s].stationName == Last_Received_MA.TrainInfo.CurrentStoppedStation) {
										StoppedForServiceStop = true;
										ServiceStopBehindATrain = true;
										this->CurrentServiceStop = Stations[s].stationName;					// Setting the name of the station the train is currently stopping at
										this->XCurrentServiceStop = instant_spatial_position[time_seconds]; // When the train is in following mode and it is stopped at a station the LastReceived_MA.RelativePos might be a safety margin from the front of the train ahead for this reason in this case we put the current position instant_spatial_position[i] as the location where the train needs to perform the stop
										this->CurrentServiceStopPlatform = Stations[s].stationPlatformId;
										recordCurrentServiceStopArrival(time_seconds);
									}
								}
							}
						}
					}

					// Stopping at a station for an estabilished dwell time
					if (((As.endNode.X * 1000 - instant_spatial_position[time_seconds - 1]) < 4) && (As.endNode.station == 1)) {
						this->StoppedForServiceStop = true;	   // Stating that the train is stopping at a station to perform a service stop
						this->ServiceStopBehindATrain = false; // Stating that the train is the first to stop at a platform so it is not stopping behind any other train.
						Eq[time_seconds] = 5;
						double stoptime = 0;
						double dep_time = 0;
						counter = 0;
						for (int s = 0; s < numStations; s++) {
							if (Stations[s].stationName == As.endNode.stationName) {
								this->CurrentServiceStop = Stations[s].stationName; // Setting the name of the station the train is currently stopping at
								this->XCurrentServiceStop = instant_spatial_position[time_seconds];
								this->CurrentServiceStopPlatform = Stations[s].stationPlatformId;
								recordCurrentServiceStopArrival(time_seconds);

								cout << "At time " << time_seconds << " Train" << this->trainDescription << " is stopping at " << this->CurrentServiceStop << " at " << this->CurrentServiceStopPlatform << "\n";

								stoptime = Stations[s].StopTime;
								dep_time = ScheduledDepartures[s];
								recordCurrentServiceStopArrival(time_seconds);
								Stations[s].StepStopped++;
								if (Stations[s].StepStopped > Stations[s].StopTime)
									stoptime = 0;
							}
						}

						// In case the train is also stopped close to the station because of ETCS EoA, then check if the EoA is still valid
						if (this->IsTrainStoppedForEoA == 1) {
							this->checkIfMovementAuthorityStillValid(instant_train_speed[time_seconds - 1], instant_spatial_position[time_seconds - 1], Bs);
						}

						// Check if in this Station the train has also to respect a given OL Order
						if (As.endNode.respectOrder == 1) {
							bool Condition_To_Stop_Train = Conditions_To_Stop_Train_For_OL_Order(As.endNode.respectOrder, As.endNode.indexOrderList, Bs);
							// The train can leave the station only if the previous train has passed, it has satisfied the min dwell time and the signal aspect is non restrictive and there is not condition to stop the train becasue of an ETCS 3 EoA
							if ((Condition_To_Stop_Train == 0) && ((time_seconds - stop) > stoptime) && (time_seconds > dep_time) && (IsTrainStoppedForEoA == 0)) {
								instant_train_speed[time_seconds] = 0.0001;
								instant_spatial_position[time_seconds] = As.endNode.X * 1000 + 0.0001;
								this->StoppedForServiceStop = false; // Stating that the train is no longer stoppoed at a service station
								this->CurrentServiceStop = "None";	 // This states that the train is no longer stopping at the service station
							}
							// Otherwise the train has to remain stopped at the station
							else {
								instant_train_speed[time_seconds] = 0;
								instant_spatial_position[time_seconds] = As.endNode.X * 1000 - 0.0001;
							}
						}
						// If the station Node does not have to respect a specific train order, the train has to be stopped at the station for the dwell time and can depart only after that ensuring that it does not have to respect any ETCS 3 EoA at that station
						else {
							if (((time_seconds - stop) <= stoptime) || (time_seconds <= dep_time) || (this->IsTrainStoppedForEoA == 1)) {
								instant_train_speed[time_seconds] = 0;
								instant_spatial_position[time_seconds] = As.endNode.X * 1000 - 0.0001;
							} else {
								instant_train_speed[time_seconds] = 0.0001;
								instant_spatial_position[time_seconds] = As.endNode.X * 1000 + 0.0001;
								this->StoppedForServiceStop = false; // Stating that the train is no longer stoppoed at a service station
								this->CurrentServiceStop = "None";	 // This states that the train is no longer stopping at the service station
								this->CurrentServiceStopPlatform = "None";
								BrakingForEoA = false; // setting the variable Braking for EoA to false since the train is starting to move again
							}
						}
						instant_train_power_consumption[time_seconds] = 0;
						train_energy_consumption(time_seconds);
					}

					// Stopping at a checkpoint Node where a certain train order must be respected
					else if ((As.endNode.X * 1000 - instant_spatial_position[time_seconds - 1] < 4) && (As.endNode.respectOrder == 1)) {
						counter = 0;
						bool Cond_To_Stop_Train = Conditions_To_Stop_Train_For_OL_Order(As.endNode.respectOrder, As.endNode.indexOrderList, Bs);

						// In case the train is also stopped close to the checkpoint because of ETCS 3 EoA, then check if the EoA is still valid
						if (this->IsTrainStoppedForEoA == 1) {
							this->checkIfMovementAuthorityStillValid(instant_train_speed[time_seconds - 1], instant_spatial_position[time_seconds - 1], Bs);
						}

						if ((Cond_To_Stop_Train == 0) && (IsTrainStoppedForEoA == 0)) { // the checkpoint Node can coincide with an ETCS3 EoA, that is why the train can depart from this Node only after having verified that the checkpoint does not coincide with an ETCS3 EoA or that the EoA has been deleted
							Eq[time_seconds] = 532;
							instant_train_speed[time_seconds] = 0.0001;
							instant_spatial_position[time_seconds] = As.endNode.X * 1000 + 0.0001;
							BrakingForEoA = false; // setting back to false BrakingForEoa since the train starts to move again
						}
						// otherwise it has to be stopped
						else {
							Eq[time_seconds] = 534;
							instant_train_speed[time_seconds] = 0;
							instant_spatial_position[time_seconds] = As.endNode.X * 1000 - 0.0001;
						}

						instant_train_power_consumption[time_seconds] = 0;
						train_energy_consumption(time_seconds);
					}

					// Stopping at a Red Signal
					else if (As.endNode.X * 1000 - instant_spatial_position[time_seconds - 1] < 4) {
						Eq[time_seconds] = 6;
						// In case the train is also stopped close to the signal because of ETCS EoA, then check if the EoA is still valid
						if (this->IsTrainStoppedForEoA == 1) {
							this->checkIfMovementAuthorityStillValid(instant_train_speed[time_seconds - 1], instant_spatial_position[time_seconds - 1], Bs);
						}

						// also a signal can coincide with an ETCS3 EoA, that is why a train must stop at the signal also if this not red but and ETCS 3 EoA exists
						if ((!strcmp(Bs.state, "red")) || (this->IsTrainStoppedForEoA == 1)) {
							instant_spatial_position[time_seconds] = As.endNode.X * 1000 - 0.0001;
							instant_train_speed[time_seconds] = 0;
						} else {
							instant_spatial_position[time_seconds] = As.endNode.X * 1000 + 0.0001;
							instant_train_speed[time_seconds] = 0.0001;
							BrakingForEoA = false; // setting BrakingForEoA to false since the train starts moving again
						}
						instant_train_power_consumption[time_seconds] = 0;
						train_energy_consumption(time_seconds);
						counter = 0;
					}
					// if noone of the previous conditions is verified it means that the train is stopped at an ETCS3 EoA
					else if (this->IsTrainStoppedForEoA == 1) {
						bool CanDepartFromServiceStopBehindATrain = true; // This variable sets whether the train stopping at a station behind another train at the same platform can depart because it has performed already the entire stop time. This is set to true by default
						Eq[time_seconds] = 321;
						// the content of this if condition is needed to set the train to StoppedForServiceStop behind a train to true
						if (this->StoppedForServiceStop == 0) { // if the train is not yet stopped for a service performed behind another train
							// if the train ahead is stopped at a station and it is the first train on the platform
							if ((Last_Received_MA.TrainInfo.StoppedForServiceStop == 1) && (Last_Received_MA.TrainInfo.ServiceStopBehindATrain == 0) && (Last_Received_MA.type == "TrainEnd") && (Last_Received_MA.typePart == "Tale")) {
								for (int s = 0; s < numStations; s++) {
									if (Stations[s].stationName == Last_Received_MA.TrainInfo.CurrentStoppedStation) {
										StoppedForServiceStop = true;
										ServiceStopBehindATrain = true;
										this->CurrentServiceStop = Stations[s].stationName; // Setting the name of the station the train is currently stopping at
										this->XCurrentServiceStop = Last_Received_MA.RelativePosEoA;
										this->CurrentServiceStopPlatform = Stations[s].stationPlatformId;
										recordCurrentServiceStopArrival(time_seconds);

										cout << " At time " << time_seconds << " Train " << this->trainDescription << "is stopping at: " << CurrentServiceStop << " at " << CurrentServiceStopPlatform << "\n";
									}
								}
							}
						}
						// if the train is stopping behind a train at the same platform of a station
						if ((StoppedForServiceStop == 1) && (ServiceStopBehindATrain == 1)) {
							CanDepartFromServiceStopBehindATrain = checkIfTrainCanDepartAfterHavingServiceStopBehindATrain(time_seconds); // Check if the train can depart from the service stop because it has performed the entire stop time or the scheduled departure time has been reached
						}
						// Check if the EoA given at the previous instant is still valid
						this->checkIfMovementAuthorityStillValid(instant_train_speed[time_seconds - 1], instant_spatial_position[time_seconds - 1], Bs);

						if (this->IsTrainStoppedForEoA == 1) { // if the train is stopped behind a valid MA or it is stopping because of a service stop behind another train at the same platform
							instant_train_speed[time_seconds] = 0;
							instant_spatial_position[time_seconds] = Last_Received_MA.RelativePosEoA - 0.0001;
							Eq[time_seconds] = 323;
						} else {

							if ((StoppedForServiceStop == 1) && (ServiceStopBehindATrain == 1)) {
								if (CanDepartFromServiceStopBehindATrain == 0) { // if the train cannot depart because it needs to satsfy a service stop behind another train at the same platform
									instant_train_speed[time_seconds] = 0;
									instant_spatial_position[time_seconds] = XCurrentServiceStop - 0.0001;
									Eq[time_seconds] = 324;
								}

								else {
									instant_train_speed[time_seconds] = 0.0001;
									instant_spatial_position[time_seconds] = XCurrentServiceStop + 0.0001;
									Eq[time_seconds] = 325;
									StoppedForServiceStop = false;
									ServiceStopBehindATrain = false; // then reset its stopping parameters to false
									// Add the service stop the train has performed behind a leader train to Timetabling points so that after the simulation has finished we can transfer the info on where the train has stopped back to the dynamic array Stations to compute arrival delays
									TrainEvent NewServiceStop;
									NewServiceStop.SuccessorID = CurrentServiceStop;
									NewServiceStop.Position = XCurrentServiceStop;
									TimetablePoints.push_back(NewServiceStop);
									for (int s = 0; s < numStations; s++) {
										if (Stations[s].stationName == CurrentServiceStop) { // delete the station it already stopped at from the list of Stations
											Stations[s].stationName = "None";
										}
									}
									CurrentServiceStop = "None"; // reset CurrentServiceStop to None
									XCurrentServiceStop = -1;
								}
							} else {
								instant_train_speed[time_seconds] = 0.0001;
								instant_spatial_position[time_seconds] = Last_Received_MA.RelativePosEoA + 0.0001;
								BrakingForEoA = false;
								Eq[time_seconds] = 326;
							}
						}

						instant_train_power_consumption[time_seconds] = 0;
						train_energy_consumption(time_seconds);
						counter = 0;
					}

					// if the train was stopped for an EoA behind a train and was performing a service stop but did not finish its stop yet when the leading train ahead had left
					else if ((IsTrainStoppedForEoA == 0) && (StoppedForServiceStop == 1) && (ServiceStopBehindATrain == 1) && (IsTrainInFollowingMode == 0)) {
						bool CanDepartFromServiceStopBehindATrain = true;
						CanDepartFromServiceStopBehindATrain = checkIfTrainCanDepartAfterHavingServiceStopBehindATrain(time_seconds); // Check if the train can depart from the service stop because it has performed the entire stop time or the scheduled departure time has been reached
						if (CanDepartFromServiceStopBehindATrain == 0) {
							instant_train_speed[time_seconds] = 0;
							instant_spatial_position[time_seconds] = XCurrentServiceStop - 0.0001;
							Eq[time_seconds] = 327;
						} else {
							instant_spatial_position[time_seconds] = XCurrentServiceStop + 0.0001;
							instant_train_speed[time_seconds] = 0.0001;
							Eq[time_seconds] = 328;
							StoppedForServiceStop = false;
							ServiceStopBehindATrain = false; // then reset its stopping parameters to false
							// Add the service stop the train has performed behind a leader train to Timetabling points so that after the simulation has finished we can transfer the info on where the train has stopped back to the dynamic array Stations to compute arrival delays
							TrainEvent NewServiceStop;
							NewServiceStop.SuccessorID = CurrentServiceStop;
							NewServiceStop.Position = XCurrentServiceStop;
							TimetablePoints.push_back(NewServiceStop);
							for (int s = 0; s < numStations; s++) {
								if (Stations[s].stationName == CurrentServiceStop) { // delete the station it already stopped at from the list of Stations
									Stations[s].stationName = "None";
								}
							}
							CurrentServiceStop = "None"; // reset CurrentServiceStop to None
							XCurrentServiceStop = -1;
						}
						instant_train_power_consumption[time_seconds] = 0;
						train_energy_consumption(time_seconds);
						counter = 0;
					}
					// if the train was stopped for an EoA behind a train and was performing a service stop but did not finish its stop yet when the leading train ahead had left
					// if the train is in following mode then when the train ahead (leader) departs also this train will depart independently from the time it has been stopped at the station
					else if ((IsTrainStoppedForEoA == 0) && (StoppedForServiceStop == 1) && (ServiceStopBehindATrain == 1) && (IsTrainInFollowingMode == 1)) {
						// if the train ahead is already departed then let the follower train to depart together with the leader since they are coupled
						if (Last_Received_MA.TrainInfo.StoppedForServiceStop == 0) {
							instant_spatial_position[time_seconds] = XCurrentServiceStop + 0.0001;
							instant_train_speed[time_seconds] = 0.0001;
							Eq[time_seconds] = 340;
							StoppedForServiceStop = false;
							ServiceStopBehindATrain = false; // then reset its stopping parameters to false
							// Add the service stop the train has performed behind a leader train to Timetabling points so that after the simulation has finished we can transfer the info on where the train has stopped back to the dynamic array Stations to compute arrival delays
							TrainEvent NewServiceStop;
							NewServiceStop.SuccessorID = CurrentServiceStop;
							NewServiceStop.Position = XCurrentServiceStop;
							TimetablePoints.push_back(NewServiceStop);
							for (int s = 0; s < numStations; s++) {
								if (Stations[s].stationName == CurrentServiceStop) { // delete the station it already stopped at from the list of Stations
									Stations[s].stationName = "None";
								}
							}
							CurrentServiceStop = "None"; // reset CurrentServiceStop to None
							XCurrentServiceStop = -1;
						}
						// else if the train ahead is not departed yet then let the follower staying stopped at the service stop behind the leader
						else {
							instant_spatial_position[time_seconds] = XCurrentServiceStop - 0.0001;
							instant_train_speed[time_seconds] = 0;
							Eq[time_seconds] = 341;
						}
						instant_train_power_consumption[time_seconds] = 0;
						train_energy_consumption(time_seconds);
						counter = 0;
					}

					// if the speed limit on the Arc is 0: this case happens only when the BACC is implemented
					if (V_lim == 0) {
						counter = 0;
						Eq[time_seconds] = 958;
						instant_train_speed[time_seconds] = 0;
						instant_spatial_position[time_seconds] = instant_spatial_position[time_seconds - 1];
						instant_train_power_consumption[time_seconds] = 0;
						train_energy_consumption(time_seconds);
					}
					GradientExceptionInBraking = false;
				}

				// Update the status of Infrastructure elements on the route of the train
				if (time_seconds > 0) {
					UpdateInfrastructureElementStatus(
						train_route[indexOfRoute].InfrastructureElements, time_seconds,
						instant_spatial_position[time_seconds - 1],
						instant_spatial_position[time_seconds],
						instant_train_speed[time_seconds - 1],
						trainDescription);
				}

				// if the train has just entered the track therefore its position instant_spatial_position[i] is higher than 1 meter and lower than 3 meters VirtualQ[ID-1] must be updated to ID
				Update_LastEnteredTrain_For_All_OL_Improved(time_seconds, train_route[indexOfRoute].sequence_of_block_sections[Previous_Block_Index]);
			}
			/***********************************************************************************************************************/
			else {
				instant_train_speed[time_seconds] = 0.0;
				instant_spatial_position[time_seconds] = Start_Node_X * 1000;
				instant_block_section_occupied[time_seconds] = "-";
				instant_train_power_consumption[time_seconds] = 0.0;
			}
			// cout << instant_spatial_position[i] << " " << As.gradient << " " << As.curvature << "\n";
		} else {
			instant_block_section_occupied[time_seconds] = "-";
		}
	}

	// Function to compute the arrival and departure of a train from a location with a specific position expressed in [m]
	void computeArrivalAndDepartureAtLocation(double LocationPosition, TrainEvent& PassingPoint) {
		int TrainEntryTime = (int)departure_time;
		double ArrivalTime = -1, DepartureTime = -1;
		bool IsArrivalFound = false;
		bool IsDepartureFound = false;
		for (int t = TrainEntryTime; t < initial_variables.times; t++) {
			if (IsArrivalFound == 0) {
				if ((instant_spatial_position[t - 1] < LocationPosition - 0.0001) && (instant_spatial_position[t] >= LocationPosition - 0.0001)) {
					IsArrivalFound = true;
					ArrivalTime = (t - 1) * timestep;
				}
			}
			if (IsDepartureFound == 0) {
				if ((instant_spatial_position[t - 1] <= LocationPosition - 0.0001) && (instant_spatial_position[t] > LocationPosition - 0.0001)) { // Try to put just the following line to better retrieve departure if (instant_spatial_position[t]>LocationPosition - 0.0001)
					IsDepartureFound = true;
					DepartureTime = (t - 1) * timestep;
				}
			}
			// When both the arrival and departure times are found then we assign them to the TrainEvent
			if ((IsArrivalFound == 1) && (IsDepartureFound == 1)) {
				PassingPoint.Time = ArrivalTime;
				PassingPoint.Time2 = DepartureTime;
				// The position of the PassingPoint must be the absolute geographical position, so we must use a conversion if we are using a route that is reversed
				if (train_route[indexOfRoute].reversed_direction == 1) {
					PassingPoint.Position = train_route[indexOfRoute].OriginalRefReversedRoute - LocationPosition;
				} else {
					PassingPoint.Position = LocationPosition;
				}
				break; // if both arrival and depature of the train have been found we can break the for loop over the time t
			}
		}
	}

	// Function to compute the passing time of a train over each timetable point (i.e. stations, junctions and or relevant signals)
	void ComputeTimetablingPoints() {
		if (TimetablePoints.empty() == 1) { // if the timetablepoints are still empty this means that the train did not follow any other train in Virtual Coupling and that stopped at stations
			for (int i = 0; i < train_route[indexOfRoute].N_Block_Sections; i++) {
				for (int j = 0; j < train_route[indexOfRoute].sequence_of_block_sections[i].total_arcs; j++) {
					if (train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.stationName.empty() != 1) { // if the Node on the route is a station or junction area
						TrainEvent TTPoint;
						TTPoint.SuccessorID = train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.stationName; // this is the name of the location of the timetable point
						TTPoint.Position = train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.X;
						TTPoint.trainDescription = this->trainDescription; // Assigning the trainDescription to the Train Event
																		   // In this case we compute the arrival and departure of the train from the location and we assign these to the TrainEvent
						computeArrivalAndDepartureAtLocation(train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.X * 1000, TTPoint);
						// Compute the arrival and departure times of the train from the location and push it back into the TimeTablePoints List
						this->TimetablePoints.push_back(TTPoint);
					}
				}
			}
		} else { // if instead the train stopped behind another train in Virtual Coupling, then we need to transfer the Timetable points back to the Stations
			int s = 0;
			for (list<TrainEvent>::iterator q = TimetablePoints.begin(); q != TimetablePoints.end(); q++) {
				q->trainDescription = trainDescription;
				// Retransfer the information of the performed stop to the Stations Array
				Stations[s].stationName = q->SuccessorID;
				Stations[s].X = q->Position / 1000; // In the array Stations position of Stations are given in Km that is why here we divide by 1000 since the attribute position is instead in m
				// Compute the Arrival and Departure at the stations
				computeArrivalAndDepartureAtLocation(q->Position, *q);
				s++; // increase the counter of the Stations array by 1 position
			}
			// Now compute it also for the TimetablingPoints where the train did not stop but just passed through
			for (int i = 0; i < train_route[indexOfRoute].N_Block_Sections; i++) {
				for (int j = 0; j < train_route[indexOfRoute].sequence_of_block_sections[i].total_arcs; j++) {
					if (train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.stationName.empty() != 1) { // if the Node on the route is a station or junction area
						bool IsAlreadythere = false;
						for (list<TrainEvent>::iterator d = TimetablePoints.begin(); d != TimetablePoints.end(); d++) {
							if (train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.stationName == d->SuccessorID) {
								IsAlreadythere = true;
								break; // break the loop over the Timetable points so over d
							}
						}
						if (IsAlreadythere == 0) { // Only if the stationName Node was not already in the list of Timetabling Points then we add another timetabling point to the list
							TrainEvent TTPoint;
							TTPoint.SuccessorID = train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.stationName; // this is the name of the location of the timetable point
							TTPoint.Position = train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.X;
							TTPoint.trainDescription = this->trainDescription; // Assigning the trainDescription to the Train Event
																			   // In this case we compute the arrival and departure of the train from the location and we assign these to the TrainEvent
							computeArrivalAndDepartureAtLocation(train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.X * 1000, TTPoint);
							// Compute the arrival and departure times of the train from the location and push it back into the TimeTablePoints List
							this->TimetablePoints.push_back(TTPoint);
						}
					}
				}
			}
		}
	}

	// Function to Determine which Block Section is Occupied by the train
	virtual void Det_Section_Occupied_By_Train(int i, Section* BS, int Blocks) {
		if (OutOfSimulation == 0) {
			if ((i >= departure_time) && (CanEnter == 1)) {
				// collect all signalling_block_sections from head to tail
				int hTail = 0, hHead = 0;
				for (int h = 0; h < Blocks; h++) {
					if (((instant_spatial_position[i - (int)(S_delay / timestep)] - train_length) < BS[h].end_node.X * 1000) && ((instant_spatial_position[i - (int)(S_delay / timestep)] - train_length) >= BS[h].start_node.X * 1000)) {
						hTail = h;
					}
					if ((instant_spatial_position[i - (int)(S_delay / timestep)] < BS[h].end_node.X * 1000) && (instant_spatial_position[i - (int)(S_delay / timestep)] >= BS[h].start_node.X * 1000)) {
						hHead = h;
						break;
					}
				}

				// occupy single tracks (this function limits teh entrance of two trains cnsecutively on the same single track even if they have the same direction which should not be the case
				// the entrance should only be limited to trains on the dingle track if they have opposite direction
				// occupySingleTrack(BS, Blocks, hTail, hHead, i);

				// release double switch when train leaves it
				if (hTail > 0 && BS[hTail - 1].start_node.virtualSignal) {
					releaseDoubleSwitch(BS[hTail - 1], BS[hTail - 2]);
				}
				// check if train is crossing a double switch
				for (int h = hTail; h <= hHead; h++) {
					// 2nd half of double switch occupied
					if (h > 0 && BS[h].start_node.virtualSignal) {
						occupyDoubleSwitch(BS[h], BS[h - 1]);
					}
					// only 1st half of double switch occupied
					if (BS[h].end_node.virtualSignal && h < (Blocks - 1)) {
						occupyDoubleSwitch(BS[h + 1], BS[h]);
					}
				}

				// occupy all signalling_block_sections from head to tail
				for (int h = hTail; h <= hHead; h++) {
					int Prev_Block = h - 1;
					if (h == 0)
						Prev_Block = 0;
					occupyBlockAndConnected(BS[h], BS[Prev_Block], (instant_spatial_position[i - (int)(S_delay / timestep)] - train_length), (instant_spatial_position[i - (int)(S_delay / timestep) - 1] - train_length));
					// if the Block Section has a switch in diverging position

					if (BS[h].withSwitchDiv == true) {
						if ((BS[h].SignallingLevel == 3) || (BS[h].SignallingLevel == 4)) {
							activateBlocksWithSwitchesDiv(BS[h], BS[Prev_Block].trackLineId, (instant_spatial_position[i - (int)(S_delay / timestep)] - train_length));
						} else {
							activateBlocksWithSwitchesDivFixedBlock(BS[h], BS[Prev_Block].trackLineId, (instant_spatial_position[i - (int)(S_delay / timestep)] - train_length));
						}
					}
				}
			}
			// Release signalling system when train exits simulation(this operation must be done without signalling delay because the train dies as soon as it overcomes the last Node of its route)
			if ((i > 0) && (instant_spatial_position[i - 1] >= train_route[indexOfRoute].x_of_end_node * 1000)) {
				releaseLastBlockAndConnected(Bs);
				OutOfSimulation = true;
			}
		}
	}

	// Function to Report Train Position to RBC (the ETCS3 Safety Margin must be given in m)
	void ReportPositionToRBC(int i, Section* BS, int N_BS, double ETCS3SafetyMargin) {
		if (OutOfSimulation == 0) {
			if ((i >= departure_time) && (CanEnter == 1)) {
				double LastTrainAcceleration = 0;
				if ((i - (int)(S_delay / timestep)) >= 1) { // Last Acceleration is computed when i - (int)(S_delay / timestep) is at least >1 so that we can also refer to the previous value of speed instant_train_speed[i - 1 - (int)(S_delay / timestep)]
					LastTrainAcceleration = (instant_train_speed[i - (int)(S_delay / timestep)] - instant_train_speed[i - 1 - (int)(S_delay / timestep)]) / timestep;
				}

				for (int h = 0; h < Blocks; h++) {
					if ((BS[h].SignallingLevel == 3) || (BS[h].SignallingLevel == 4)) {

						// Report the position of the max safe rear end

						// Case if the train-based detection of the rear is more permissive (i.e the position of the rear & the (rear - ETCS3SafetyMargin) are in the same block)
						if (((instant_spatial_position[i - (int)(S_delay / timestep)] - train_length - ETCS3SafetyMargin) < BS[h].end_node.X * 1000) && ((instant_spatial_position[i - (int)(S_delay / timestep)] - train_length - ETCS3SafetyMargin) >= BS[h].start_node.X * 1000) && ((instant_spatial_position[i - (int)(S_delay / timestep)] - train_length) < BS[h].end_node.X * 1000)) {
							// Defining veriables for the ID of the previous, Current and Next Section of the edge of the train
							string CurrentSectionID, NextSectionID;
							CurrentSectionID = BS[h].ID;
							int Prev_Block = h - 1;
							if (h == 0)
								Prev_Block = 0;
							int Next_Block = -1;

							if (h == N_BS - 1) { // this is the pointer to the next block section of the MA
								Next_Block = h;
								NextSectionID = "None";
							} else {
								Next_Block = h + 1;
								NextSectionID = BS[h + 1].ID;
							}

							if (BS[h].withSwitchDiv == 0) {
								elaborateRbcMas((instant_spatial_position[i - (int)(S_delay / timestep)] - train_length - ETCS3SafetyMargin), instant_train_speed[i - (int)(S_delay / timestep)], LastTrainAcceleration, BS[h], CurrentSectionID, NextSectionID, trainDescription, train_route[this->indexOfRoute], train_route[this->indexOfRoute].reversed_direction, "TrainEnd", "Tale");
								lockSwitchesOnAllConnectedSections((instant_spatial_position[i - (int)(S_delay / timestep)] + ETCS3SafetyMargin), (instant_spatial_position[i - (int)(S_delay / timestep)] - train_length - ETCS3SafetyMargin), instant_train_speed[i - (int)(S_delay / timestep)], LastTrainAcceleration, BS[h], CurrentSectionID, NextSectionID, trainDescription, train_route[this->indexOfRoute].reversed_direction, "None");
							} else { // if instead the block section has a diverging switch
								elaborateMaOnBlockSectionsWithSwitchDiv((instant_spatial_position[i - (int)(S_delay / timestep)] - train_length - ETCS3SafetyMargin), instant_train_speed[i - (int)(S_delay / timestep)], LastTrainAcceleration, BS[h], trainDescription, train_route[indexOfRoute], "Tale");
								lockSwitchesWhileTrainTraverses((instant_spatial_position[i - (int)(S_delay / timestep)] + ETCS3SafetyMargin), (instant_spatial_position[i - (int)(S_delay / timestep)] - train_length - ETCS3SafetyMargin), instant_train_speed[i - (int)(S_delay / timestep)], LastTrainAcceleration, BS[h], trainDescription, train_route[indexOfRoute], "Tale");
							}
						}

						// Case if the track-based vacancy detection is more permissive than the train-based method
						else if (((instant_spatial_position[i - (int)(S_delay / timestep)] - train_length) < BS[h].end_node.X * 1000) && ((instant_spatial_position[i - (int)(S_delay / timestep)] - train_length) >= BS[h].start_node.X * 1000) && (instant_spatial_position[i - (int)(S_delay / timestep)] - train_length - ETCS3SafetyMargin) < BS[h].start_node.X * 1000) {
							// Defining veriables for the ID of the previous, Current and Next Section of the edge of the train
							string CurrentSectionID, NextSectionID;
							CurrentSectionID = BS[h].ID;
							int Prev_Block = h - 1;
							if (h == 0)
								Prev_Block = 0;
							int Next_Block = -1;

							if (h == N_BS - 1) { // this is the pointer to the next block section of the MA
								Next_Block = h;
								NextSectionID = "None";
							} else {
								Next_Block = h + 1;
								NextSectionID = BS[h + 1].ID;
							}

							if (BS[h].withSwitchDiv == 0) {
								elaborateRbcMas((BS[h].start_node.X * 1000), instant_train_speed[i - (int)(S_delay / timestep)], LastTrainAcceleration, BS[h], CurrentSectionID, NextSectionID, trainDescription, train_route[this->indexOfRoute], train_route[this->indexOfRoute].reversed_direction, "TrainEnd", "Tale");
								lockSwitchesOnAllConnectedSections((instant_spatial_position[i - (int)(S_delay / timestep)] + ETCS3SafetyMargin), (BS[h].start_node.X * 1000), instant_train_speed[i - (int)(S_delay / timestep)], LastTrainAcceleration, BS[h], CurrentSectionID, NextSectionID, trainDescription, train_route[this->indexOfRoute].reversed_direction, "None");
							} else { // if instead the block section has a diverging switch
								elaborateMaOnBlockSectionsWithSwitchDiv((BS[h].start_node.X * 1000), instant_train_speed[i - (int)(S_delay / timestep)], LastTrainAcceleration, BS[h], trainDescription, train_route[indexOfRoute], "Tale");
								lockSwitchesWhileTrainTraverses((instant_spatial_position[i - (int)(S_delay / timestep)] + ETCS3SafetyMargin), (BS[h].start_node.X * 1000), instant_train_speed[i - (int)(S_delay / timestep)], LastTrainAcceleration, BS[h], trainDescription, train_route[indexOfRoute], "Tale");
							}
						}

						// if the train is in the simulation but its tale is still outside of the network (so the position of the tale is lower than the beginning of the first block section) then the MA should refer to the beginning Node of the first block section of the route
						if ((instant_spatial_position[i - (int)(S_delay / timestep)] - train_length - ETCS3SafetyMargin) < BS[0].start_node.X) {

							elaborateRbcMas((instant_spatial_position[i - (int)(S_delay / timestep)] - train_length - ETCS3SafetyMargin), instant_train_speed[i - (int)(S_delay / timestep)], LastTrainAcceleration, BS[0], BS[0].ID, BS[1].ID, trainDescription, train_route[this->indexOfRoute], train_route[this->indexOfRoute].reversed_direction, "TrainEnd", "Tale");
						}

						// Detecting the MA related to the front of the train
						if (((instant_spatial_position[i - (int)(S_delay / timestep)] + ETCS3SafetyMargin) < BS[h].end_node.X * 1000) && ((instant_spatial_position[i - (int)(S_delay / timestep)] + ETCS3SafetyMargin) >= BS[h].start_node.X * 1000)) {
							// Defining veriables for the ID of the previous, Current and Next Section of the edge of the train
							string CurrentSectionID, NextSectionID;
							CurrentSectionID = BS[h].ID;
							int Prev_Block = h - 1;
							if (h == 0)
								Prev_Block = 0;
							int Next_Block = -1;

							if (h == N_BS - 1) { // this is the pointer to the next block section of the MA
								Next_Block = h;
								NextSectionID = "None";
							} else {
								Next_Block = h + 1;
								NextSectionID = BS[h + 1].ID;
							}

							if (BS[h].withSwitchDiv == 0) {
								elaborateRbcMas((instant_spatial_position[i - (int)(S_delay / timestep)] + ETCS3SafetyMargin), instant_train_speed[i - (int)(S_delay / timestep)], LastTrainAcceleration, BS[h], CurrentSectionID, NextSectionID, trainDescription, train_route[this->indexOfRoute], train_route[this->indexOfRoute].reversed_direction, "TrainEnd", "Front");
								lockSwitchesOnAllConnectedSections((instant_spatial_position[i - (int)(S_delay / timestep)] + ETCS3SafetyMargin), (instant_spatial_position[i - (int)(S_delay / timestep)] - train_length - ETCS3SafetyMargin), instant_train_speed[i - (int)(S_delay / timestep)], LastTrainAcceleration, BS[h], CurrentSectionID, NextSectionID, trainDescription, train_route[this->indexOfRoute].reversed_direction, "None");
							} else { // if instead the block section has a diverging switch
								elaborateMaOnBlockSectionsWithSwitchDiv((instant_spatial_position[i - (int)(S_delay / timestep)] + ETCS3SafetyMargin), instant_train_speed[i - (int)(S_delay / timestep)], LastTrainAcceleration, BS[h], trainDescription, train_route[indexOfRoute], "Front");
								lockSwitchesWhileTrainTraverses((instant_spatial_position[i - (int)(S_delay / timestep)] + ETCS3SafetyMargin), (instant_spatial_position[i - (int)(S_delay / timestep)] - train_length - ETCS3SafetyMargin), instant_train_speed[i - (int)(S_delay / timestep)], LastTrainAcceleration, BS[h], trainDescription, train_route[indexOfRoute], "Front");
							}
							break;
						}
					}
				}
				// In case the train is stopping at a station to perform a service stop communicate all this information to the corresponding Movement Authorities
				if ((instant_train_speed[i - (int)(S_delay / timestep)] == 0) && (this->StoppedForServiceStop == 1)) {
					if (ETCS_MA.size() > 0) {
						for (list<MovementAuthority>::iterator it = ETCS_MA.begin(); it != ETCS_MA.end(); it++) {
							if (it->TrainInfo.trainDescription == this->trainDescription) {
								it->TrainInfo.StoppedForServiceStop = this->StoppedForServiceStop;
								it->TrainInfo.CurrentStoppedStation = this->CurrentServiceStop;
								it->TrainInfo.ServiceStopBehindATrain = this->ServiceStopBehindATrain;
							}
						}
					}
				}
			}
		}
	}

	// Predict MA to split from the train ahead when coupled
	void Predict_And_Check_Validity_Of_MA_To_Split_At(int t, Train* T, int numTrains) {
		if (IsTrainInFollowingMode == 1) { // If the train is in following mode
			// Variables of the leading train
			double PosLeader = -1;
			double LengthLeader = -1;
			bool LeaderIsInSimulation = false;
			double SpeedLeader = -1;
			int RouteIndexLeader = -1;
			// Determining current characteristics of Leading train
			for (int h = 0; h < numTrains; h++) {
				if ((T[h].trainDescription == LeadingTrainInFollowingMode) && (T[h].OutOfSimulation == 0)) {
					LeaderIsInSimulation = true;
					PosLeader = T[h].instant_spatial_position[t - 1];
					LengthLeader = T[h].train_length;
					SpeedLeader = T[h].instant_train_speed[t - 1];
					RouteIndexLeader = T[h].indexOfRoute;
					break; // break the for loop over the trains in the simulation
				}
			}
			if ((this->indexOfRoute != RouteIndexLeader) && (LeaderIsInSimulation == 1)) { // only if the routes of the two trains are different
				if ((Predicted_MA_To_DecoupleAt.TrainInfo.trainDescription == "None") || (Predicted_MA_To_DecoupleAt.TrainInfo.trainDescription != LeadingTrainInFollowingMode) || (this->instant_spatial_position[t - 1] > Predicted_MA_To_DecoupleAt.RelativePosEoA)) {
					// Temporary Movement Authority to split coupled train at
					MovementAuthority TEMP_MA_To_SplitAt;
					string ElementToSplitAt = "None";
					double AbscissaElementToSplitAt = -1;
					double GeoAbsElementToSplitAt = -1;
					double XElementToSplitAtOnLeaderRoute = -1;
					string BSIDElementToSplitAt = "None";

					// Number of Elements in common with the route of the leader
					int CommonElements = 0;

					// Determining the route elements in common to identify the infraelement where the decoupling shall happen

					if (train_route[this->indexOfRoute].InfrastructureElements.size() > 0) {
						for (list<InfraElement>::iterator r = train_route[this->indexOfRoute].InfrastructureElements.begin(); r != train_route[this->indexOfRoute].InfrastructureElements.end(); r++) {
							if (r->XCoordinate >= this->instant_spatial_position[t - 1]) { // if the infra element is ahead of current train position
								bool IsElementFound = false;							   // This variable turns to true when element r is found in the route of the leader
								if (train_route[RouteIndexLeader].InfrastructureElements.size() > 0) {
									for (list<InfraElement>::iterator q = train_route[RouteIndexLeader].InfrastructureElements.begin(); q != train_route[RouteIndexLeader].InfrastructureElements.end(); q++) {
										if ((q->ID == r->ID) || ((r->IsSwitch == 1) && (q->SwitchName == r->SwitchName))) { // if the element q is not a switch and has the same ID of r or if r is a switch and has the same name of q then
											CommonElements++;																// increase the number of elements in common
											IsElementFound = true;															// set Is element r found to true
											// Initialise the variables of the element to split at
											ElementToSplitAt = r->ID;
											BSIDElementToSplitAt = r->SectionID;
											AbscissaElementToSplitAt = r->XCoordinate;
											GeoAbsElementToSplitAt = r->GeoXCoord;
											XElementToSplitAtOnLeaderRoute = q->XCoordinate; // Abscissa of the element to split at on Leader's route

											break; // once the r element is found then break the for loop over the elements q
										}
									}
								}
								// if the r element has instead not been found
								if ((CommonElements > 0) && (IsElementFound == 0)) { // if the routes have elements in common and we found the first r element not in common with Leader's route
									// then initalise Movement Authority TEMP_MA_ToSplitAt and break the for loop over infra elements r
									if (PosLeader != -1) { // Initialise the MA only if the leading train is stil within the simulation environment
										// if the train is out of the simulation then TEMP_MA_To_SplitAt will be set to default value with trainDescription equal to "None" and so on
										TEMP_MA_To_SplitAt.TrainInfo.trainDescription = LeadingTrainInFollowingMode;
										TEMP_MA_To_SplitAt.TrainInfo.TrainSpeed = SpeedLeader;
										TEMP_MA_To_SplitAt.AbsPosEoA = GeoAbsElementToSplitAt;
										TEMP_MA_To_SplitAt.BSID = BSIDElementToSplitAt;
										TEMP_MA_To_SplitAt.RelativePosEoA = AbscissaElementToSplitAt;
										TEMP_MA_To_SplitAt.type = "DivergingSwitchOccupied";
										TEMP_MA_To_SplitAt.TrainInfo.Position = XElementToSplitAtOnLeaderRoute; // This is the position of the infra element to split at on leader's route
									}
									break; // break the for loop over Route infra elements r
								}
							}
						}
					}

					if ((PosLeader - LengthLeader) <= TEMP_MA_To_SplitAt.TrainInfo.Position) { // Provide the Predicted MA to split At only if it is still valid and the tale of leading train is still behind the decoupling point
						this->Predicted_MA_To_DecoupleAt = TEMP_MA_To_SplitAt;
					} else {
						Predicted_MA_To_DecoupleAt = TEMP_MA_To_SplitAt;
						Predicted_MA_To_DecoupleAt.type = "TrainUncoupled"; // In case the tale of the leading train has crossed the uncoupling point this train can be considered decoupled from the leader and this is communicated in the type of MA
					}
				}
				// In case the MA to Decouple AT does not need to be recomputed then we need to check where the leader train is and provide again the same predicted MA to decouple at or that the train can be considered uncoupled when the leader has crossed the splitting point
				else {
					// When we do not need to recompute the MA where the trains will uncouple at then just check when the tale of the leader goes beyond the splitting point so to communicate to this train that it can finally uncouple
					if ((PosLeader - LengthLeader) > Predicted_MA_To_DecoupleAt.TrainInfo.Position) {
						Predicted_MA_To_DecoupleAt.type = "TrainUncoupled";
					}
				}
			}
		}
	}

	// This function is used to compute the running time of the trains in free flow for headway computation or other similar analysis
	void ComputeFreeFlowTrajectory(double SimPeriod, double v1, double v2, double v3) {
		for (int t = 0; t < SimPeriod; t++) {
			Trajectory_Block_Section(t, v1, v2, v3);
		}
	}

	// Function to Compute Blocking Times for a single Block Section. This Function allows us to compute blocking time in area with mixed signalling system
	/**signalling_block_sections is the Block Section in the route of train, IndexBS is instead the index of that Block Section in the Route*/
	void ComputeBlockingTimeForSingleLocation(int IndexCurrentBS, string SignallingType, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double AbsoluteRTSupplement, double PercentageRTSupplement) {

		BlockTime[N_BlockSections].BlockID = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].ID;
		BlockTime[N_BlockSections].length = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].length;
		BlockTime[N_BlockSections].PosStart = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].start_node.X * 1000;
		BlockTime[N_BlockSections].PosEnd = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].end_node.X * 1000;
		BlockTime[N_BlockSections].GeoPosStart = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].GeoXBegNode; // This is in meter already
		BlockTime[N_BlockSections].GeoPosEnd = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].GeoXEndNode;	 // This is in meter already

		BlockTime[N_BlockSections].trainDescription = this->trainDescription;

		bool FoundApprTimeETCS2 = false; // This variable becomes true when the StartApproachTime of the ETCS level 2 is found

		int p = EntryTime + 1;
		for (; p <= End_Time; p++) {
			if ((instant_spatial_position[p - 1] <= train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].start_node.X * 1000) && (instant_spatial_position[p] > train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].start_node.X * 1000)) {
				BlockTime[N_BlockSections].StartRunTime = p - 1;
			}

			if ((instant_spatial_position[p - 1] <= train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].end_node.X * 1000) && (instant_spatial_position[p] > train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].end_node.X * 1000)) {
				BlockTime[N_BlockSections].EndRunTime = p - 1;
			}

			if ((instant_spatial_position[p - 1] - train_length <= train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].end_node.X * 1000) && (instant_spatial_position[p] - train_length > train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].end_node.X * 1000)) {
				BlockTime[N_BlockSections].EndClearTime = p - 1;
			}

			if (IndexCurrentBS > 0) {
				if (SignallingType == "Conventional") {
					if ((instant_spatial_position[p - 1] <= train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS - 1].start_node.X * 1000) && (instant_spatial_position[p] > train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS - 1].start_node.X * 1000)) {
						BlockTime[N_BlockSections].StartApproachTime = p - 1;
					}

				} else if (SignallingType == "ETCS2") {
					if (FoundApprTimeETCS2 == 0) {																														  // if the startApproach time of ETCS level 2 has not been fund yet
						double BrakingDistance = pow(instant_train_speed[p], 2) / (2 * max_train_decelaration);															  // Computing the braking curve from the speed instant_train_speed[p]
						if (instant_spatial_position[p] + BrakingDistance > train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].start_node.X * 1000) { // if at instant p the instant_spatial_position[p]+BrakingDistance overcomes the entry signal of block section signalling_block_sections[i] then the StartApproachTime is instant p-1
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

		if (IndexCurrentBS == train_route[indexOfRoute].N_Block_Sections - 1) { // for the last block section we do not have clearing time since the train unblocks the section as soon as its head arrives at the final point of the network
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
			BlockTime[N_BlockSections].RunTimeMargin = AbsoluteRTSupplement / train_route[indexOfRoute].N_Block_Sections;
			// Shift the EndOccTime of the RunTimeMargin
			BlockTime[N_BlockSections].EndOccTime = BlockTime[N_BlockSections].EndOccTime + BlockTime[N_BlockSections].RunTimeMargin;
		} else if ((AbsoluteRTSupplement == 0) && (PercentageRTSupplement != 0)) {
			BlockTime[N_BlockSections].RunTimeMargin = BlockTime[N_BlockSections].RunTime * PercentageRTSupplement / 100;
			// Shift the EndOccTime of the RunTimeMargin
			BlockTime[N_BlockSections].EndOccTime = BlockTime[N_BlockSections].EndOccTime + BlockTime[N_BlockSections].RunTimeMargin;
		}

		else {
			eglogger << "\n\nWARNING 11: The absolute and the percentage Running Time Supplements are both different from 0 or both equal to 0\n\n"
					 << std::endl;
		}

		if ((BlockTime[N_BlockSections].StartRunTime < 0) || (BlockTime[N_BlockSections].StartOccTime < 0) || (BlockTime[N_BlockSections].EndOccTime < BlockTime[N_BlockSections].StartOccTime)) {
			BlockTime[N_BlockSections].IsComplete = false;
		}
		if (BlockTime[N_BlockSections].IsComplete == 1)
			N_BlockTimeComplete++; // Increase the number of blocking times that are complete
								   // Increase the counter of BlockSections
		this->N_BlockSections++;
	}

	double ComputeLastOccupationTime_real_time(int t, std::string CurrentBS, int EntryTime) {
		// lastOccupationTime
		// int index=0
		// find the index of the blocksection
		// int n = sizeof(train_route[indexOfRoute].sequence_of_block_sections) / sizeof(train_route[indexOfRoute].sequence_of_block_sections[0]);
		// auto itr = find(train_route[indexOfRoute].sequence_of_block_sections, train_route[indexOfRoute].sequence_of_block_sections + n, CurrentBS);
		int IndexCurrentBS = 0; // distance(train_route[indexOfRoute].sequence_of_block_sections, itr);
		for (int h = 0; h < train_route[indexOfRoute].N_Block_Sections; h++) {
			if (train_route[indexOfRoute].sequence_of_block_sections[h].ID == CurrentBS) {
				// std::cout << "FOUND: " << CurrentBS;
				IndexCurrentBS = h;
				break;
			}
		}
		// BlockTime[N_BlockSections].StartOccTime = BlockTime[N_BlockSections].StartApproachTime - BlockTime[N_BlockSections].setupTime - BlockTime[N_BlockSections].sightReacTime;
		int p = EntryTime + 1;
		double start_occupation_time = 0.0;

		for (; p <= t; p++) {
			if (instant_spatial_position[p] >= train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].start_node.X * 1000) {
				start_occupation_time = p;
				break;
			}
		}
		return start_occupation_time;
	}

	// Function to Compute the Blocking Times in ETCS level 3 (EntryTime is the time the train actually enters the network in the simulation). Such a function has been replaces by ComputBlockingTimeETCSL3ForSection
	void ComputeBlockTimeETCS3ForSingleLocation(int IndexCurrentBS, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double AbsoluteRTSupplement, double PercentRTSupplement) {

		list<BlockingTimes> LocationList, FinalLocationList; // LocationList is a temporary List where we collect all the ETCS Level 3 locations belonging to the current Block Section
		int N_LocationList = 0, N_FinalLocationList = 0;	 // Final LocationList instead collects all those locations of LocationList that are not present in the BlockTime of this train (that is why those locations that actually need to be added)

		if (train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].start_node.tdsbId.empty() != 1) {
			BlockingTimes TEMPLoc;

			TEMPLoc.BlockID = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].start_node.tdsbId;
			TEMPLoc.length = 0;
			TEMPLoc.PosStart = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].start_node.X * 1000;
			TEMPLoc.PosEnd = TEMPLoc.PosStart;
			TEMPLoc.GeoPosStart = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].start_node.tdsbGeoCoordX;
			TEMPLoc.GeoPosEnd = TEMPLoc.GeoPosStart;
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

		// Now Add TDSB at station platforms
		for (int m = 0; m < train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].total_arcs; m++) {
			// The arcs of block sections belonging to Routes that are not reversed have the stationName in the end_node of some of their Arc
			if ((train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].arcs_in_signalling_block_section[m].endNode.tdsbId.empty() != 1) && (train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].arcs_in_signalling_block_section[m].endNode.stationName.empty() != 1)) { // if the nodes is a station and the TDSB is not empty
				BlockingTimes TEMPLoc;

				TEMPLoc.BlockID = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].arcs_in_signalling_block_section[m].endNode.tdsbId;
				TEMPLoc.length = 0;
				TEMPLoc.stationName = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].arcs_in_signalling_block_section[m].endNode.stationName;
				TEMPLoc.PosStart = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].arcs_in_signalling_block_section[m].endNode.X * 1000;
				TEMPLoc.PosEnd = TEMPLoc.PosStart;
				TEMPLoc.GeoPosStart = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].arcs_in_signalling_block_section[m].endNode.tdsbGeoCoordX;
				TEMPLoc.GeoPosEnd = TEMPLoc.GeoPosStart;
				// Increase the locations of 1 element if it is not already in the list
				if (N_LocationList == 0) {
					N_LocationList++;
					LocationList.push_back(TEMPLoc);
				} else {
					bool IsAlreadyThere = false;
					for (list<BlockingTimes>::iterator l = LocationList.begin(); l != LocationList.end(); l++) {
						if ((TEMPLoc.BlockID == l->BlockID) || (TEMPLoc.GeoPosStart == l->GeoPosStart)) {
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
			// The arcs of block sections belonging to Routes that are reversed have the stationName in the start_node of some of their Arc
			if ((train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].arcs_in_signalling_block_section[m].startNode.tdsbId.empty() != 1) && (train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].arcs_in_signalling_block_section[m].startNode.stationName.empty() != 1)) { // if the Node is a station and the TDSB is not empty
				BlockingTimes TEMPLoc;

				TEMPLoc.BlockID = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].arcs_in_signalling_block_section[m].startNode.tdsbId;
				TEMPLoc.length = 0;
				TEMPLoc.stationName = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].arcs_in_signalling_block_section[m].startNode.stationName;
				TEMPLoc.PosStart = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].arcs_in_signalling_block_section[m].startNode.X * 1000;
				TEMPLoc.PosEnd = TEMPLoc.PosStart;
				TEMPLoc.GeoPosStart = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].arcs_in_signalling_block_section[m].startNode.tdsbGeoCoordX;
				TEMPLoc.GeoPosEnd = TEMPLoc.GeoPosStart;
				// Increase the locations of 1 element if it is not already in the list
				if (N_LocationList == 0) {
					N_LocationList++;
					LocationList.push_back(TEMPLoc);
				} else {
					bool IsAlreadyThere = false;
					for (list<BlockingTimes>::iterator l = LocationList.begin(); l != LocationList.end(); l++) {
						if ((TEMPLoc.BlockID == l->BlockID) || (TEMPLoc.GeoPosStart == l->GeoPosStart)) {
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

		// Do the same thing for the end TDSB
		if (train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].end_node.tdsbId.empty() != 1) {
			BlockingTimes TEMPLoc;

			TEMPLoc.BlockID = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].end_node.tdsbId;
			TEMPLoc.length = 0;
			TEMPLoc.PosStart = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].end_node.X * 1000;
			TEMPLoc.PosEnd = TEMPLoc.PosStart;
			TEMPLoc.GeoPosStart = train_route[indexOfRoute].sequence_of_block_sections[IndexCurrentBS].end_node.tdsbGeoCoordX;
			TEMPLoc.GeoPosEnd = TEMPLoc.GeoPosStart;
			// Increase the locations of 1 element if it is not already in the list
			if (N_LocationList == 0) {
				N_LocationList++;
				LocationList.push_back(TEMPLoc);
			} else {
				bool IsAlreadyThere = false;
				for (list<BlockingTimes>::iterator l = LocationList.begin(); l != LocationList.end(); l++) {
					if ((TEMPLoc.BlockID == l->BlockID) || (TEMPLoc.GeoPosStart == l->GeoPosStart)) {
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

		// Defining the FinalLocationList
		for (list<BlockingTimes>::iterator y = LocationList.begin(); y != LocationList.end(); y++) {
			bool IsAlreadyInBlockTime = false;
			if (this->N_BlockSections > 0) {
				for (int h = 0; h < N_BlockSections; h++) {
					if ((y->BlockID == BlockTime[h].BlockID) || (y->GeoPosStart == BlockTime[h].GeoPosStart)) {
						IsAlreadyInBlockTime = true;
						break; // break the for loop over the BlockTime of this train
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
			BlockTime[N_BlockSections].BlockID = it->BlockID;
			BlockTime[N_BlockSections].length = it->length;
			BlockTime[N_BlockSections].PosStart = it->PosStart;
			BlockTime[N_BlockSections].PosEnd = it->PosEnd;
			BlockTime[N_BlockSections].GeoPosStart = it->GeoPosStart;
			BlockTime[N_BlockSections].GeoPosEnd = it->GeoPosEnd;
			BlockTime[N_BlockSections].stationName = it->stationName;
			BlockTime[N_BlockSections].trainDescription = this->trainDescription;

			bool FoundStartRunTime = false;
			bool FoundApprTimeETCS3 = false;
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
					if (FoundApprTimeETCS3 == 0) {																   // if the startApproach time of ETCS level 3 has not been found yet
						double BrakingDistance = pow(instant_train_speed[p], 2) / (2 * max_train_decelaration);	   // Computing the braking curve from the speed instant_train_speed[p]
						if (instant_spatial_position[p] + BrakingDistance > BlockTime[N_BlockSections].PosStart) { // if at instant p the instant_spatial_position[p]+BrakingDistance overcomes the entry signal of block section signalling_block_sections[i] then the StartApproachTime is instant p-1
							BlockTime[N_BlockSections].StartApproachTime = p - 1;
							FoundApprTimeETCS3 = true; // The StartApproachTime has been found that is why the variable goes to true
						}
					}
				}
			}

			// In some case it can happen an exception when the station is too close to the end of the train journey (i.e. the end of its route) and the train length is longer than the distance between the station and the end of the journey
			// In these cases it is not possible to get a EndClearTime and the corresponding condition if used to determine it does not work. In this case we fix this problem assuming that the EndClearTime coincides with the time at which the train exits the simulation (i.e. End_Time)
			if (BlockTime[N_BlockSections].EndClearTime == -1) {
				BlockTime[N_BlockSections].EndClearTime = this->End_Time;
			}

			// BlockTime[N_BlockSections].EndRunTime=BlockTime[N_BlockSections].StartRunTime;
			BlockTime[N_BlockSections].EndApproachTime = BlockTime[N_BlockSections].StartRunTime;
			BlockTime[N_BlockSections].StartClearTime = BlockTime[N_BlockSections].EndRunTime;
			BlockTime[N_BlockSections].setupTime = SetupTime;
			BlockTime[N_BlockSections].sightReacTime = SightReacTime;
			BlockTime[N_BlockSections].ReleaseTime = ReleaseTime;

			if (N_BlockSections == 0) { // for the first block we do not have setup and sightReaction Time nor approach time
				BlockTime[N_BlockSections].StartApproachTime = BlockTime[N_BlockSections].StartRunTime;
				BlockTime[N_BlockSections].EndApproachTime = BlockTime[N_BlockSections].StartApproachTime;
				BlockTime[N_BlockSections].setupTime = 0;
				BlockTime[N_BlockSections].sightReacTime = 0;
			}

			if ((N_BlockSections == N_FinalLocationList - 1) && (IndexCurrentBS == train_route[indexOfRoute].N_Block_Sections)) { // for the last block section we do not have clearing time since the train unblocks the section as soon as its head arrives at the final point of the network
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

			if ((AbsoluteRTSupplement != 0) && (PercentRTSupplement == 0)) {
				BlockTime[N_BlockSections].RunTimeMargin = AbsoluteRTSupplement / (train_route[indexOfRoute].N_Block_Sections);
				if (N_BlockSections > 0) { // Add the Running time margin to the locations apart from the initial location
					BlockTime[N_BlockSections].EndOccTime = BlockTime[N_BlockSections].EndOccTime + BlockTime[N_BlockSections].RunTimeMargin;
				}
			} else if ((AbsoluteRTSupplement == 0) && (PercentRTSupplement != 0)) {
				// For ETCS3 this line must be corrected since we consider as running time the rinning time between this block section and the previous one.
				double RunTimeBetweenConsLocations = 0; // This is the running time between two consecutive locations
				if (N_BlockSections > 0) {
					RunTimeBetweenConsLocations = BlockTime[N_BlockSections].EndRunTime - BlockTime[N_BlockSections - 1].EndRunTime; // The running time is equal to the difference between the End Run Time of this locaton and the previous
				}
				// In this way the RunTimeBetweenConsecutive locations will be 0 for the first location and different from 0 for the other locations
				BlockTime[N_BlockSections].RunTimeMargin = RunTimeBetweenConsLocations * PercentRTSupplement / 100;
				// Shift the EndOccTime of the RunTimeMargin only if the block section is not the first
				BlockTime[N_BlockSections].EndOccTime = BlockTime[N_BlockSections].EndOccTime + BlockTime[N_BlockSections].RunTimeMargin;
			}

			else {
				cout << "\n\nWARNING 1: Percentage and absolute Running Time Supplements are both equal to 0 or both different from 0\n\n";
			}

			if ((BlockTime[N_BlockSections].StartRunTime < 0) || (BlockTime[N_BlockSections].StartOccTime < 0) || (BlockTime[N_BlockSections].EndOccTime < BlockTime[N_BlockSections].StartOccTime)) {
				BlockTime[N_BlockSections].IsComplete = false;
			}
			if (BlockTime[N_BlockSections].IsComplete == 1)
				N_BlockTimeComplete++; // Increase the number of blocking times that are complete

			N_BlockSections++; // Increase the BlockTime of the train
		}
	}

	// Function to Compute the Blocking Times in ETCS level 3 (EntryTime is the time the train actually enters the network in the simulation)
	void ComputeBlockTime_ETCSLevel_3_ForSection(int IndexCurrentBS, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsoluteRTSupplement, double PercentRTSupplement) {

		list<BlockingTimes> LocationList, FinalLocationList; // LocationList is a temporary List where we collect all the ETCS Level 3 locations belonging to the current Block Section
		int N_LocationList = 0, N_FinalLocationList = 0;	 // Final LocationList instead collects all those locations of LocationList that are not present in the BlockTime of this train (that is why those locations that actually need to be added)

		if (train_route[this->indexOfRoute].InfrastructureElements.size() > 0) {

			for (list<InfraElement>::iterator k = train_route[this->indexOfRoute].InfrastructureElements.begin(); k != train_route[this->indexOfRoute].InfrastructureElements.end(); k++) {
				BlockingTimes TEMPLoc; // defining temporary Blocking Time variable

				if ((k->SectionID == train_route[this->indexOfRoute].sequence_of_block_sections[IndexCurrentBS].ID) && (k->ID != "VirtualTDSB")) { // if the Infrastructure Element belongs to the block section and it is not a VirtualTDSB
					TEMPLoc.BlockID = k->ID;
					TEMPLoc.length = 0;
					TEMPLoc.trainDescription = this->trainDescription;
					TEMPLoc.SignallingLevel = train_route[this->indexOfRoute].sequence_of_block_sections[IndexCurrentBS].SignallingLevel; // setting the signalling level of the infrastructure element we are computing the blocking time for
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
					TEMPLoc.checkIfLastInfraElementStatusIsInLineWithTrainNeeds(this->trainDescription, InfraElementsList);

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
						if (FoundApprTimeETCS3 == 0) {																																																																	 // if the startApproach time of ETCS level 3 has not been found yet
							double BrakingDistance = this->BrakingDistanceFastComputation(instant_train_speed[p], 0, instant_spatial_position[p], BlockTime[N_BlockSections].PosStart, train_route[this->indexOfRoute].sequence_of_block_sections, train_route[this->indexOfRoute].N_Block_Sections) + SafetyMargin; // Computing the braking curve from the speed instant_train_speed[p]
							if (instant_spatial_position[p] + BrakingDistance > BlockTime[N_BlockSections].PosStart) {																																																	 // if at instant p the instant_spatial_position[p]+BrakingDistance overcomes the entry signal of block section signalling_block_sections[i] then the StartApproachTime is instant p-1
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
					BlockTime[N_BlockSections].RunTimeMargin = AbsoluteRTSupplement / (train_route[indexOfRoute].N_Block_Sections);
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
						cout << "Warning 987: Train " << this->trainDescription << " has a negative running time between infrastructure element " << BlockTime->ConnectedBlockingTimeID << " and the previous infrastructure element on the route. Please check what happened\n";
					}
					// In this way the RunTimeBetweenConsecutive locations will be 0 for the first location and different from 0 for the other locations
					BlockTime[N_BlockSections].RunTimeMargin = ceil(RunTimeBetweenConsLocations * PercentRTSupplement / 100);
					// Shift the EndOccTime of the RunTimeMargin only if the block section is not the first
					BlockTime[N_BlockSections].EndRunTime = BlockTime[N_BlockSections].EndRunTime + BlockTime[N_BlockSections].RunTimeMargin;
				}

				else {

					eglogger << "\n\nWARNING 2: Percentage and absolute Running Time Supplements are both equal to 0 or both different from 0\n\n";
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

				if ((N_BlockSections == N_FinalLocationList - 1) && (IndexCurrentBS == train_route[indexOfRoute].N_Block_Sections)) { // for the last block section we do not have clearing time since the train unblocks the section as soon as its head arrives at the final point of the network
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

	// Function to Compute the Blocking Times in Virtual Coupling (EntryTime is the time the train actually enters the network in the simulation)
	/**This function uses a principle of max safety where trains are spaced out of an absolute braking distance at any switch is approached*/
	void ComputeBlockTime_ETCSLevel4_ForSection_MaxSafety(int IndexCurrentBS, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsoluteRTSupplement, double PercentRTSupplement) {

		list<BlockingTimes> LocationList, FinalLocationList; // LocationList is a temporary List where we collect all the ETCS Level 3 locations belonging to the current Block Section
		int N_LocationList = 0, N_FinalLocationList = 0;	 // Final LocationList instead collects all those locations of LocationList that are not present in the BlockTime of this train (that is why those locations that actually need to be added)

		if (train_route[this->indexOfRoute].InfrastructureElements.size() > 0) {

			for (list<InfraElement>::iterator k = train_route[this->indexOfRoute].InfrastructureElements.begin(); k != train_route[this->indexOfRoute].InfrastructureElements.end(); k++) {
				BlockingTimes TEMPLoc; // defining temporary Blocking Time variable

				if ((k->SectionID == train_route[this->indexOfRoute].sequence_of_block_sections[IndexCurrentBS].ID) && (k->ID != "VirtualTDSB")) { // if the Infrastructure Element belongs to the block section and it is not a VirtualTDSB
					TEMPLoc.BlockID = k->ID;
					TEMPLoc.length = 0;
					TEMPLoc.trainDescription = this->trainDescription;
					TEMPLoc.SignallingLevel = train_route[this->indexOfRoute].sequence_of_block_sections[IndexCurrentBS].SignallingLevel; // setting the signalling level of the infrastructure element we are computing the blocking time for
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
					TEMPLoc.checkIfLastInfraElementStatusIsInLineWithTrainNeeds(this->trainDescription, InfraElementsList);

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
							if ((BlockTime[N_BlockSections].NamePreviousTrain == "None") || (BlockTime[N_BlockSections].LocationWithSwitch == 1)) {																																										  // if the train is the first train to run over the section, that means that it is the leader and will travel under ETCS Level 3 with an absolute braking distance
								ApproachingDistance = this->BrakingDistanceFastComputation(instant_train_speed[p], 0, instant_spatial_position[p], BlockTime[N_BlockSections].PosStart, train_route[this->indexOfRoute].sequence_of_block_sections, train_route[this->indexOfRoute].N_Block_Sections) + SafetyMargin; // Computing the braking curve from the speed instant_train_speed[p]
							}

							else { // if the train has instead other trains in front of it, it is a follower and will need to coordinate its speed with the one of the train leader
								if (V_At_SafetyMargin_From_Location < BlockTime[N_BlockSections].SpeedPreviousTrain) {
									ApproachingDistance = this->AccelerationDistanceFastComputation(V_At_SafetyMargin_From_Location, BlockTime[N_BlockSections].SpeedPreviousTrain, S_At_SafetyMargin_FromLocation, BlockTime[N_BlockSections].PosStart, train_route[this->indexOfRoute].sequence_of_block_sections, train_route[this->indexOfRoute].N_Block_Sections) + SafetyMargin;
								} else if (V_At_SafetyMargin_From_Location > BlockTime[N_BlockSections].SpeedPreviousTrain) {
									ApproachingDistance = this->BrakingDistanceFastComputation(V_At_SafetyMargin_From_Location, BlockTime[N_BlockSections].SpeedPreviousTrain, S_At_SafetyMargin_FromLocation, BlockTime[N_BlockSections].PosStart, train_route[this->indexOfRoute].sequence_of_block_sections, train_route[this->indexOfRoute].N_Block_Sections) + SafetyMargin;
								} else if (V_At_SafetyMargin_From_Location <= BlockTime[N_BlockSections].SpeedPreviousTrain) {
									ApproachingDistance = SafetyMargin;
									// In this case Sight and Reaction time must be 0 since the train does not need ot react (i.e. braking or accelerrating) to the preceding train since they are at the same speed
									SightReacTime = 0;
								}
							}

							if (instant_spatial_position[p] + ApproachingDistance > BlockTime[N_BlockSections].PosStart) { // if at instant p the instant_spatial_position[p]+BrakingDistance overcomes the entry signal of block section signalling_block_sections[i] then the StartApproachTime is instant p-1
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
					BlockTime[N_BlockSections].RunTimeMargin = AbsoluteRTSupplement / (train_route[indexOfRoute].N_Block_Sections);
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
						cout << "Warning: Train " << this->trainDescription << " has a negative running time between infrastructure element " << BlockTime->ConnectedBlockingTimeID << " and the previous infrastructure element on the route. Please check what happened\n";
					}
					// In this way the RunTimeBetweenConsecutive locations will be 0 for the first location and different from 0 for the other locations
					BlockTime[N_BlockSections].RunTimeMargin = ceil(RunTimeBetweenConsLocations * PercentRTSupplement / 100);
					// Shift the EndOccTime of the RunTimeMargin only if the block section is not the first
					BlockTime[N_BlockSections].EndRunTime = BlockTime[N_BlockSections].EndRunTime + BlockTime[N_BlockSections].RunTimeMargin;
				}

				else {
					cout << "\n\nWARNING 3: Percentage and absolute Running Time Supplements are both equal to 0 or both different from 0\n\n";
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

				if ((N_BlockSections == N_FinalLocationList - 1) && (IndexCurrentBS == train_route[indexOfRoute].N_Block_Sections)) { // for the last block section we do not have clearing time since the train unblocks the section as soon as its head arrives at the final point of the network
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

	// Function to Compute the Blocking Times in Virtual Coupling (EntryTime is the time the train actually enters the network in the simulation)
	/**This function uses a principle of max capacity where trains travel at a minimum safety distance from each other over the entire shared route, splitting apart by an absolute braking distance only at those junctions where they diverge or converge*/
	void ComputeBlockTime_ETCSLevel4_ForSection_MaxCapacity(int IndexCurrentBS, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsoluteRTSupplement, double PercentRTSupplement) {

		list<BlockingTimes> LocationList, FinalLocationList; // LocationList is a temporary List where we collect all the ETCS Level 3 locations belonging to the current Block Section
		int N_LocationList = 0, N_FinalLocationList = 0;	 // Final LocationList instead collects all those locations of LocationList that are not present in the BlockTime of this train (that is why those locations that actually need to be added)

		if (train_route[this->indexOfRoute].InfrastructureElements.size() > 0) {

			for (list<InfraElement>::iterator k = train_route[this->indexOfRoute].InfrastructureElements.begin(); k != train_route[this->indexOfRoute].InfrastructureElements.end(); k++) {
				BlockingTimes TEMPLoc; // defining temporary Blocking Time variable
				// Identifying the InfraElement on the route after K
				list<InfraElement>::iterator LastInfraElementOnRoute = train_route[this->indexOfRoute].InfrastructureElements.end();
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

				if ((k->SectionID == train_route[this->indexOfRoute].sequence_of_block_sections[IndexCurrentBS].ID) && (k->ID != "VirtualTDSB")) { // if the Infrastructure Element belongs to the block section and it is not a VirtualTDSB
					TEMPLoc.BlockID = k->ID;
					TEMPLoc.NextBlockID = SuccessorInfraElement;
					TEMPLoc.length = 0;
					TEMPLoc.trainDescription = this->trainDescription;
					TEMPLoc.SignallingLevel = train_route[this->indexOfRoute].sequence_of_block_sections[IndexCurrentBS].SignallingLevel; // setting the signalling level of the infrastructure element we are computing the blocking time for
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
					TEMPLoc.checkIfLastInfraElementStatusIsInLineWithTrainNeeds(this->trainDescription, InfraElementsList);

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
							// The if condition below means that an absolute braking distance is used if teh trains is the first train of a platoon or at junctions where the route diverges or converge with the route of the train ahead.
							// This means that at switches that the train and the train ahead take in the same position a relative braking distance is used to compute the approaching time
							if ((BlockTime[N_BlockSections].NamePreviousTrain == "None") || ((BlockTime[N_BlockSections].LocationWithSwitch == 1) && (BlockTime[N_BlockSections].InfraElementInPositionForTrain == 0))) {																								  // if the train is the first train to run over the section, that means that it is the leader and will travel under ETCS Level 3 with an absolute braking distance. It will also travel with an absolute braking distance over switches that are not in the right position for the train, because the previous train was using a different route.
								ApproachingDistance = this->BrakingDistanceFastComputation(instant_train_speed[p], 0, instant_spatial_position[p], BlockTime[N_BlockSections].PosStart, train_route[this->indexOfRoute].sequence_of_block_sections, train_route[this->indexOfRoute].N_Block_Sections) + SafetyMargin; // Computing the braking curve from the speed instant_train_speed[p]
							}

							else { // if the train has instead other trains in front of it, it is a follower and will need to coordinate its speed with the one of the train leader
								if (V_At_SafetyMargin_From_Location < (BlockTime[N_BlockSections].SpeedPreviousTrain - 0.278)) {
									// The train will accelerate to coordinate its speed with the train ahead only if also the next location is on the route shared by the two trains
									if (BlockTime[N_BlockSections].NextBlockID == BlockTime[N_BlockSections].NextBlockIDPreviousTrain) {
										ApproachingDistance = this->AccelerationDistanceFastComputation(V_At_SafetyMargin_From_Location, BlockTime[N_BlockSections].SpeedPreviousTrain, S_At_SafetyMargin_FromLocation, BlockTime[N_BlockSections].PosStart, train_route[this->indexOfRoute].sequence_of_block_sections, train_route[this->indexOfRoute].N_Block_Sections) + SafetyMargin;
									} else { // In case this train is going slower than the train ahead and the train ahead will take another route at the next location, it does not make sense for this train to catch up with the train ahead and the train can stay at its current speed (cruising mode). In this case the approaching distance is simply the safety margin
										ApproachingDistance = SafetyMargin;
									}
								} else if (V_At_SafetyMargin_From_Location > (BlockTime[N_BlockSections].SpeedPreviousTrain + 0.278)) {
									ApproachingDistance = this->BrakingDistanceFastComputation(V_At_SafetyMargin_From_Location, BlockTime[N_BlockSections].SpeedPreviousTrain, S_At_SafetyMargin_FromLocation, BlockTime[N_BlockSections].PosStart, train_route[this->indexOfRoute].sequence_of_block_sections, train_route[this->indexOfRoute].N_Block_Sections) + SafetyMargin;
								} else if ((V_At_SafetyMargin_From_Location >= BlockTime[N_BlockSections].SpeedPreviousTrain - 0.278) && (V_At_SafetyMargin_From_Location <= BlockTime[N_BlockSections].SpeedPreviousTrain + 0.278)) {
									ApproachingDistance = SafetyMargin;
									// In this case Sight and Reaction time must be 0 since the train does not need ot react (i.e. braking or accelerrating) to the preceding train since they are at the same speed
									SightReacTime = 0;
								}
							}

							if (instant_spatial_position[p] + ApproachingDistance > BlockTime[N_BlockSections].PosStart) { // if at instant p the instant_spatial_position[p]+BrakingDistance overcomes the entry signal of block section signalling_block_sections[i] then the StartApproachTime is instant p-1
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
					BlockTime[N_BlockSections].RunTimeMargin = AbsoluteRTSupplement / (train_route[indexOfRoute].N_Block_Sections);
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
						cout << "Warning: Train " << this->trainDescription << " has a negative running time between infrastructure element " << BlockTime->ConnectedBlockingTimeID << " and the previous infrastructure element on the route. Please check what happened\n";
					}
					// In this way the RunTimeBetweenConsecutive locations will be 0 for the first location and different from 0 for the other locations
					BlockTime[N_BlockSections].RunTimeMargin = ceil(RunTimeBetweenConsLocations * PercentRTSupplement / 100);
					// Shift the EndOccTime of the RunTimeMargin only if the block section is not the first
					BlockTime[N_BlockSections].EndRunTime = BlockTime[N_BlockSections].EndRunTime + BlockTime[N_BlockSections].RunTimeMargin;
				}

				else {
					cout << "\n\nWARNING 4: Percentage and absolute Running Time Supplements are both equal to 0 or both different from 0\n\n";
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

				if ((N_BlockSections == N_FinalLocationList - 1) && (IndexCurrentBS == train_route[indexOfRoute].N_Block_Sections)) { // for the last block section we do not have clearing time since the train unblocks the section as soon as its head arrives at the final point of the network
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

	void ComputeBlockTime_ETCSLevel4_ForSection_MaxCapacity_Improved(int IndexCurrentBS, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsoluteRTSupplement, double PercentRTSupplement) {

		list<BlockingTimes> LocationList, FinalLocationList; // LocationList is a temporary List where we collect all the ETCS Level 3 locations belonging to the current Block Section
		int N_LocationList = 0, N_FinalLocationList = 0;	 // Final LocationList instead collects all those locations of LocationList that are not present in the BlockTime of this train (that is why those locations that actually need to be added)

		if (train_route[this->indexOfRoute].InfrastructureElements.size() > 0) {

			for (list<InfraElement>::iterator k = train_route[this->indexOfRoute].InfrastructureElements.begin(); k != train_route[this->indexOfRoute].InfrastructureElements.end(); k++) {
				BlockingTimes TEMPLoc; // defining temporary Blocking Time variable
									   // Identifying the InfraElement on the route after K
				list<InfraElement>::iterator LastInfraElementOnRoute = train_route[this->indexOfRoute].InfrastructureElements.end();
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

				if ((k->SectionID == train_route[this->indexOfRoute].sequence_of_block_sections[IndexCurrentBS].ID) && (k->ID != "VirtualTDSB")) { // if the Infrastructure Element belongs to the block section and it is not a VirtualTDSB
					TEMPLoc.BlockID = k->ID;
					TEMPLoc.NextBlockID = SuccessorInfraElement;
					TEMPLoc.length = 0;
					TEMPLoc.trainDescription = this->trainDescription;
					TEMPLoc.SignallingLevel = train_route[this->indexOfRoute].sequence_of_block_sections[IndexCurrentBS].SignallingLevel; // setting the signalling level of the infrastructure element we are computing the blocking time for
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
					TEMPLoc.checkIfLastInfraElementStatusIsInLineWithTrainNeeds(this->trainDescription, InfraElementsList);

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
							if ((BlockTime[N_BlockSections].NamePreviousTrain == "None") || ((BlockTime[N_BlockSections].LocationWithSwitch == 1) && (BlockTime[N_BlockSections].InfraElementInPositionForTrain == 0))) {																								  // if the train is the first train to run over the section, that means that it is the leader and will travel under ETCS Level 3 with an absolute braking distance. It will also travel with an absolute braking distance over switches that are not in the right position for the train, because the previous train was using a different route.
								ApproachingDistance = this->BrakingDistanceFastComputation(instant_train_speed[p], 0, instant_spatial_position[p], BlockTime[N_BlockSections].PosStart, train_route[this->indexOfRoute].sequence_of_block_sections, train_route[this->indexOfRoute].N_Block_Sections) + SafetyMargin; // Computing the braking curve from the speed instant_train_speed[p]
							}

							else { // if the train has instead other trains in front of it, it is a follower and will need to coordinate its speed with the one of the train leader
								if (V_At_SafetyMargin_From_Location < (BlockTime[N_BlockSections].SpeedPreviousTrain - 0.278)) {
									// The train will accelerate to coordinate its speed with the train ahead only if also the next location is on the route shared by the two trains
									if (BlockTime[N_BlockSections].NextBlockID == BlockTime[N_BlockSections].NextBlockIDPreviousTrain) {
										double SpeedCoordinationTime = 0;
										IsInFollowingMode = true; // The train is in following mode
										// Compute the time to accelerate the train to the speed of the train ahead
										SpeedCoordinationTime = this->AccelerationTimeFollowingMode(V_At_SafetyMargin_From_Location, BlockTime[N_BlockSections].SpeedPreviousTrain, S_At_SafetyMargin_FromLocation, BlockTime[N_BlockSections].PosStart, train_route[this->indexOfRoute].sequence_of_block_sections, train_route[this->indexOfRoute].N_Block_Sections);
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
									ApproachingDistance = this->BrakingDistanceFastComputation(V_At_SafetyMargin_From_Location, BlockTime[N_BlockSections].SpeedPreviousTrain, S_At_SafetyMargin_FromLocation, BlockTime[N_BlockSections].PosStart, train_route[this->indexOfRoute].sequence_of_block_sections, train_route[this->indexOfRoute].N_Block_Sections) + SafetyMargin;
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
					BlockTime[N_BlockSections].RunTimeMargin = AbsoluteRTSupplement / (train_route[indexOfRoute].N_Block_Sections);
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
						cout << "Warning: Train " << this->trainDescription << " has a negative running time between infrastructure element " << BlockTime->ConnectedBlockingTimeID << " and the previous infrastructure element on the route. Please check what happened\n";
					}
					// In this way the RunTimeBetweenConsecutive locations will be 0 for the first location and different from 0 for the other locations
					BlockTime[N_BlockSections].RunTimeMargin = ceil(RunTimeBetweenConsLocations * PercentRTSupplement / 100);
					// Shift the EndOccTime of the RunTimeMargin only if the block section is not the first
					BlockTime[N_BlockSections].EndRunTime = BlockTime[N_BlockSections].EndRunTime + BlockTime[N_BlockSections].RunTimeMargin;
				}

				else {
					cout << "\n\nWARNING 5: Percentage and absolute Running Time Supplements are both equal to 0 or both different from 0\n\n";
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

				if ((N_BlockSections == N_FinalLocationList - 1) && (IndexCurrentBS == train_route[indexOfRoute].N_Block_Sections)) { // for the last block section we do not have clearing time since the train unblocks the section as soon as its head arrives at the final point of the network
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

	// Function to Connect the Blocking Times of edges of diverging switches for ETCS Level 3 and Virtual Coupling. In this case the diverging switch needs to be considered as a single block section, that is why blocking times of connected edges need to be accordingly modified
	void ConnectBlockTimesOfSwitchesForTrainCentricSignalling() {
		if (this->N_BlockTimeComplete > 0) {
			for (int i = 0; i < this->N_BlockTimeComplete; i++) {
				if ((BlockTime[i].SignallingLevel >= 3) && (BlockTime[i].IsComplete == 1) && (BlockTime[i].LocationWithSwitch == 1) && (BlockTime[i].ConnectedBlockingTimeID != "None")) { // if the blocktime has a signalling level higher then 3, it is complete, it is a switch and it has a connected point (so it is a diverging switch)
					bool ConnectedBlockingTimeFound = false;

					if (BlockTime[i].IsAlreadyUniformedToConnectedSwitch == 0) { // if this blocking time has not already been uniformed to the one of the connected diverging switch edge
						for (int j = 0; j < this->N_BlockTimeComplete; j++) {

							// If there is a diverging switch having the same SwitchName and position but different ID, tthis means that this is a switch linked to two other switches and it has for this reason a left side that is a Point-End connected to a switch and the right sight that is a Point-Start connected to a different switch (the situation described in the figure here at the right) o-----oo----o
							// Point-Start-----Point-End Point-Start-----Point-End
							if ((BlockTime[j].SignallingLevel >= 3) && (BlockTime[j].SwitchName == BlockTime[i].SwitchName) && (BlockTime[j].GeoPosStart = BlockTime[i].GeoPosStart) && (BlockTime[j].BlockID != BlockTime[i].BlockID)) {
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
							if ((BlockTime[j].SignallingLevel >= 3) && (BlockTime[j].IsComplete == 1) && (BlockTime[j].LocationWithSwitch == 1) && (BlockTime[j].BlockID == BlockTime[i].ConnectedBlockingTimeID)) { // if the blocking time has a signalling level higher than 3, it is complete, it is a switch and has the switch name that is the same of the connected switch for BlockTime[i]
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

	// Function to Compute the Blocking times in mixed signalling areas
	void ComputeBlockingTimesInMixedSignallingAreas(double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsRTSupp, double PercRTSupp) {
		// Compute the time the train Enters the network in the simulation: EntryTime
		int EntryTime = (int)Det_Run_Start_Time();

		if (train_route[indexOfRoute].N_Block_Sections > 0) { // if the route is composed of at least one block section
			for (int i = 0; i < train_route[indexOfRoute].N_Block_Sections; i++) {
				if (train_route[indexOfRoute].sequence_of_block_sections[i].SignallingLevel == 0) { // if block sections have Signalling level 0 then we must apply the conventional signalling rules
					this->ComputeBlockingTimeForSingleLocation(i, "Conventional", EntryTime, SetupTime, ReleaseTime, SightReacTime, AbsRTSupp, PercRTSupp);
				} else if (train_route[indexOfRoute].sequence_of_block_sections[i].SignallingLevel == 2) { // if Signalling level is 2 apply ETCS Level 2 rules
					this->ComputeBlockingTimeForSingleLocation(i, "ETCS2", EntryTime, SetupTime, ReleaseTime, SightReacTime, AbsRTSupp, PercRTSupp);
				} else if (train_route[indexOfRoute].sequence_of_block_sections[i].SignallingLevel == 3) { // if Signalling Level is 3 Apply ETCS Level 3 rules
					// ComputeBlockTimeETCS3ForSingleLocation(i, EntryTime, SetupTime, ReleaseTime, SightReacTime, AbsRTSupp, PercRTSupp);
					// the function below now replaces the function ComputeBlockingTimeETCS3ForSingleLocation
					this->ComputeBlockTime_ETCSLevel_3_ForSection(i, EntryTime, SetupTime, ReleaseTime, SightReacTime, SafetyMargin, AbsRTSupp, PercRTSupp);
				} else if (train_route[indexOfRoute].sequence_of_block_sections[i].SignallingLevel == 4) {
					this->ComputeBlockTime_ETCSLevel4_ForSection_MaxCapacity_Improved(i, EntryTime, SetupTime, ReleaseTime, SightReacTime, SafetyMargin, AbsRTSupp, PercRTSupp);
				}
			}
			// Connecting blocking times of edges of switches in area where ETCS Level 3 or higher levels of signalling (e.g. ETCS Level 4- Virtual Coupling) are installed
			this->ConnectBlockTimesOfSwitchesForTrainCentricSignalling();
		}
	}

	// Function to Calculate the Blocking Time Diagram for the Train, the two variables are the supplement of running time that can be given in absolute terms (e.g. 2 minutes) or in percentage (e.g. 5%)
	void ComputeBlockingTimes(string SignallingType, double SetupTime, double ReleaseTime, double SightReacTime, double AbsoluteRTSupplement, double PercentageRTSupplement) {
		int EntryTime = (int)Det_Run_Start_Time();
		int N_Blocks = train_route[indexOfRoute].N_Block_Sections;

		N_BlockSections = N_Blocks;

		for (int i = 0; i < N_Blocks; i++) {
			BlockTime[i].BlockID = train_route[indexOfRoute].sequence_of_block_sections[i].ID;
			BlockTime[i].length = train_route[indexOfRoute].sequence_of_block_sections[i].length;
			BlockTime[i].PosStart = train_route[indexOfRoute].sequence_of_block_sections[i].start_node.X * 1000;
			BlockTime[i].PosEnd = train_route[indexOfRoute].sequence_of_block_sections[i].end_node.X * 1000;
			BlockTime[i].GeoPosStart = train_route[indexOfRoute].sequence_of_block_sections[i].GeoXBegNode; // This is in meter already
			BlockTime[i].GeoPosEnd = train_route[indexOfRoute].sequence_of_block_sections[i].GeoXEndNode;	  // This is in meter already

			BlockTime[i].trainDescription = this->trainDescription;

			bool FoundApprTimeETCS2 = false; // This variable becomes true when the StartApproachTime of the ETCS level 2 is found

			int p = EntryTime + 1;
			for (; p <= End_Time; p++) {
				if ((instant_spatial_position[p - 1] <= train_route[indexOfRoute].sequence_of_block_sections[i].start_node.X * 1000) && (instant_spatial_position[p] > train_route[indexOfRoute].sequence_of_block_sections[i].start_node.X * 1000)) {
					BlockTime[i].StartRunTime = p - 1;
				}

				if ((instant_spatial_position[p - 1] <= train_route[indexOfRoute].sequence_of_block_sections[i].end_node.X * 1000) && (instant_spatial_position[p] > train_route[indexOfRoute].sequence_of_block_sections[i].end_node.X * 1000)) {
					BlockTime[i].EndRunTime = p - 1;
				}

				if ((instant_spatial_position[p - 1] - train_length <= train_route[indexOfRoute].sequence_of_block_sections[i].end_node.X * 1000) && (instant_spatial_position[p] - train_length > train_route[indexOfRoute].sequence_of_block_sections[i].end_node.X * 1000)) {
					BlockTime[i].EndClearTime = p - 1;
				}

				if (i > 0) {
					if (SignallingType == "Conventional") {
						if ((instant_spatial_position[p - 1] <= train_route[indexOfRoute].sequence_of_block_sections[i - 1].start_node.X * 1000) && (instant_spatial_position[p] > train_route[indexOfRoute].sequence_of_block_sections[i - 1].start_node.X * 1000)) {
							BlockTime[i].StartApproachTime = p - 1;
						}

					} else if (SignallingType == "ETCS2") {
						if (FoundApprTimeETCS2 == 0) {																											 // if the startApproach time of ETCS level 2 has not been fund yet
							double BrakingDistance = pow(instant_train_speed[p], 2) / (2 * max_train_decelaration);												 // Computing the braking curve from the speed instant_train_speed[p]
							if (instant_spatial_position[p] + BrakingDistance > train_route[indexOfRoute].sequence_of_block_sections[i].start_node.X * 1000) { // if at instant p the instant_spatial_position[p]+BrakingDistance overcomes the entry signal of block section signalling_block_sections[i] then the StartApproachTime is instant p-1
								BlockTime[i].StartApproachTime = p - 1;
								FoundApprTimeETCS2 = true; // The StartApproachTime has been found that is why the variable goes to true
							}
						}
					}
				}
			}

			BlockTime[i].EndApproachTime = BlockTime[i].StartRunTime;
			BlockTime[i].StartClearTime = BlockTime[i].EndRunTime;
			BlockTime[i].setupTime = SetupTime;
			BlockTime[i].sightReacTime = SightReacTime;
			BlockTime[i].ReleaseTime = ReleaseTime;

			if (i == 0) { // for the first block we do not have setup and sightReaction Time nor approach time
				BlockTime[i].StartApproachTime = BlockTime[i].StartRunTime;
				BlockTime[i].EndApproachTime = BlockTime[i].StartApproachTime;
				BlockTime[i].setupTime = 0;
				BlockTime[i].sightReacTime = 0;
			}

			if (i == N_Blocks - 1) { // for the last block section we do not have clearing time since the train unblocks the section as soon as its head arrives at the final point of the network
				BlockTime[i].EndClearTime = BlockTime[i].EndRunTime;
				BlockTime[i].StartClearTime = BlockTime[i].EndClearTime;
			}

			// Defining the StartOccupation and EndOccupationTime and all the other occupation times
			BlockTime[i].ApproachTime = BlockTime[i].EndApproachTime - BlockTime[i].StartApproachTime;
			BlockTime[i].RunTime = BlockTime[i].EndRunTime - BlockTime[i].StartRunTime;
			BlockTime[i].clearingTime = BlockTime[i].EndClearTime - BlockTime[i].StartClearTime;
			BlockTime[i].StartOccTime = BlockTime[i].StartApproachTime - BlockTime[i].setupTime - BlockTime[i].sightReacTime;
			BlockTime[i].EndOccTime = BlockTime[i].EndClearTime + BlockTime[i].ReleaseTime;

			// Putting the running time supplements to the blocking time
			if ((AbsoluteRTSupplement != 0) && (PercentageRTSupplement == 0)) {
				BlockTime[i].RunTimeMargin = AbsoluteRTSupplement / N_Blocks;
				// Shift the EndOccTime of the RunTimeMargin
				BlockTime[i].EndOccTime = BlockTime[i].EndOccTime + BlockTime[i].RunTimeMargin;
			} else if ((AbsoluteRTSupplement == 0) && (PercentageRTSupplement != 0)) {
				BlockTime[i].RunTimeMargin = BlockTime[i].RunTime * PercentageRTSupplement / 100;
				// Shift the EndOccTime of the RunTimeMargin
				BlockTime[i].EndOccTime = BlockTime[i].EndOccTime + BlockTime[i].RunTimeMargin;
			}

			else {
				eglogger << "\n\nWARNING 1234: The absolute and the percentage Running Time Supplements are both different from 0 or both equal to 0\n\n";
			}

			if ((BlockTime[i].StartOccTime < 0) || (BlockTime[i].EndOccTime < BlockTime[i].StartOccTime)) {
				BlockTime[i].IsComplete = false;
			}
			if (BlockTime[i].IsComplete == 1)
				N_BlockTimeComplete++; // Increase the number of blocking times that are complete
		}
	}

	// Function to Compute the Blocking Times in ETCS level 3
	void ComputeBlockTimeETCS3(double SetupTime, double ReleaseTime, double SightReacTime, double AbsoluteRTSupplement, double PercentRTSupplement) {
		int EntryTime = (int)Det_Run_Start_Time();

		list<BlockingTimes> LocationList;
		int N_LocationList = 0;

		for (int i = 0; i < train_route[indexOfRoute].N_Block_Sections; i++) {

			if (train_route[indexOfRoute].sequence_of_block_sections[i].start_node.tdsbId.empty() != 1) {
				BlockingTimes TEMPLoc;

				TEMPLoc.BlockID = train_route[indexOfRoute].sequence_of_block_sections[i].start_node.tdsbId;
				TEMPLoc.length = 0;
				TEMPLoc.PosStart = train_route[indexOfRoute].sequence_of_block_sections[i].start_node.X * 1000;
				TEMPLoc.PosEnd = TEMPLoc.PosStart;
				TEMPLoc.GeoPosStart = train_route[indexOfRoute].sequence_of_block_sections[i].start_node.tdsbGeoCoordX;
				TEMPLoc.GeoPosEnd = TEMPLoc.GeoPosStart;
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

			// Now Add TDSB at station platform
			for (int m = 0; m < train_route[indexOfRoute].sequence_of_block_sections[i].total_arcs; m++) {
				// The arcs of block sections belonging to Routes that are not reversed have the stationName in the end_node of some of their Arc
				if ((train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[m].endNode.tdsbId.empty() != 1) && (train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[m].endNode.stationName.empty() != 1)) { // if the nodes is a station and the TDSB is not empty
					BlockingTimes TEMPLoc;

					TEMPLoc.BlockID = train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[m].endNode.tdsbId;
					TEMPLoc.length = 0;
					TEMPLoc.stationName = train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[m].endNode.stationName;
					TEMPLoc.PosStart = train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[m].endNode.X * 1000;
					TEMPLoc.PosEnd = TEMPLoc.PosStart;
					TEMPLoc.GeoPosStart = train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[m].endNode.tdsbGeoCoordX;
					TEMPLoc.GeoPosEnd = TEMPLoc.GeoPosStart;
					// Increase the locations of 1 element if it is not already in the list
					if (N_LocationList == 0) {
						N_LocationList++;
						LocationList.push_back(TEMPLoc);
					} else {
						bool IsAlreadyThere = false;
						for (list<BlockingTimes>::iterator l = LocationList.begin(); l != LocationList.end(); l++) {
							if ((TEMPLoc.BlockID == l->BlockID) || (TEMPLoc.GeoPosStart == l->GeoPosStart)) {
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
				// The arcs of block sections belonging to Routes that are reversed have the stationName in the start_node of some of their Arc
				if ((train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[m].startNode.tdsbId.empty() != 1) && (train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[m].startNode.stationName.empty() != 1)) { // if the Node is a station and the TDSB is not empty
					BlockingTimes TEMPLoc;

					TEMPLoc.BlockID = train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[m].startNode.tdsbId;
					TEMPLoc.length = 0;
					TEMPLoc.stationName = train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[m].startNode.stationName;
					TEMPLoc.PosStart = train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[m].startNode.X * 1000;
					TEMPLoc.PosEnd = TEMPLoc.PosStart;
					TEMPLoc.GeoPosStart = train_route[indexOfRoute].sequence_of_block_sections[i].arcs_in_signalling_block_section[m].startNode.tdsbGeoCoordX;
					TEMPLoc.GeoPosEnd = TEMPLoc.GeoPosStart;
					// Increase the locations of 1 element if it is not already in the list
					if (N_LocationList == 0) {
						N_LocationList++;
						LocationList.push_back(TEMPLoc);
					} else {
						bool IsAlreadyThere = false;
						for (list<BlockingTimes>::iterator l = LocationList.begin(); l != LocationList.end(); l++) {
							if ((TEMPLoc.BlockID == l->BlockID) || (TEMPLoc.GeoPosStart == l->GeoPosStart)) {
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

			// Do the same thing for the end TDSB
			if (train_route[indexOfRoute].sequence_of_block_sections[i].end_node.tdsbId.empty() != 1) {
				BlockingTimes TEMPLoc;

				TEMPLoc.BlockID = train_route[indexOfRoute].sequence_of_block_sections[i].end_node.tdsbId;
				TEMPLoc.length = 0;
				TEMPLoc.PosStart = train_route[indexOfRoute].sequence_of_block_sections[i].end_node.X * 1000;
				TEMPLoc.PosEnd = TEMPLoc.PosStart;
				TEMPLoc.GeoPosStart = train_route[indexOfRoute].sequence_of_block_sections[i].end_node.tdsbGeoCoordX;
				TEMPLoc.GeoPosEnd = TEMPLoc.GeoPosStart;
				// Increase the locations of 1 element if it is not already in the list
				if (N_LocationList == 0) {
					N_LocationList++;
					LocationList.push_back(TEMPLoc);
				} else {
					bool IsAlreadyThere = false;
					for (list<BlockingTimes>::iterator l = LocationList.begin(); l != LocationList.end(); l++) {
						if ((TEMPLoc.BlockID == l->BlockID) || (TEMPLoc.GeoPosStart == l->GeoPosStart)) {
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

		// Set the number of block sections of the train
		for (list<BlockingTimes>::iterator it = LocationList.begin(); it != LocationList.end(); it++) {
			BlockTime[N_BlockSections].BlockID = it->BlockID;
			BlockTime[N_BlockSections].length = it->length;
			BlockTime[N_BlockSections].PosStart = it->PosStart;
			BlockTime[N_BlockSections].PosEnd = it->PosEnd;
			BlockTime[N_BlockSections].GeoPosStart = it->GeoPosStart;
			BlockTime[N_BlockSections].GeoPosEnd = it->GeoPosEnd;
			BlockTime[N_BlockSections].stationName = it->stationName;
			BlockTime[N_BlockSections].trainDescription = this->trainDescription;

			bool FoundStartRunTime = false;
			bool FoundApprTimeETCS3 = false;
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
					if (FoundApprTimeETCS3 == 0) {																   // if the startApproach time of ETCS level 3 has not been found yet
						double BrakingDistance = pow(instant_train_speed[p], 2) / (2 * max_train_decelaration);	   // Computing the braking curve from the speed instant_train_speed[p]
						if (instant_spatial_position[p] + BrakingDistance > BlockTime[N_BlockSections].PosStart) { // if at instant p the instant_spatial_position[p]+BrakingDistance overcomes the entry signal of block section signalling_block_sections[i] then the StartApproachTime is instant p-1
							BlockTime[N_BlockSections].StartApproachTime = p - 1;
							FoundApprTimeETCS3 = true; // The StartApproachTime has been found that is why the variable goes to true
						}
					}
				}
			}

			// In some case it can happen an exception when the station is too close to the end of the train journey (i.e. the end of its route) and the train length is longer than the distance between the station and the end of the journey
			// In these cases it is not possible to get a EndClearTime and the corresponding condition if used to determine it does not work. In this case we fix this problem assuming that the EndClearTime coincides with the time at which the train exits the simulation (i.e. End_Time)
			if (BlockTime[N_BlockSections].EndClearTime == -1) {
				BlockTime[N_BlockSections].EndClearTime = this->End_Time;
			}

			// BlockTime[N_BlockSections].EndRunTime=BlockTime[N_BlockSections].StartRunTime;
			BlockTime[N_BlockSections].EndApproachTime = BlockTime[N_BlockSections].StartRunTime;
			BlockTime[N_BlockSections].StartClearTime = BlockTime[N_BlockSections].EndRunTime;
			BlockTime[N_BlockSections].setupTime = SetupTime;
			BlockTime[N_BlockSections].sightReacTime = SightReacTime;
			BlockTime[N_BlockSections].ReleaseTime = ReleaseTime;

			if (N_BlockSections == 0) { // for the first block we do not have setup and sightReaction Time nor approach time
				BlockTime[N_BlockSections].StartApproachTime = BlockTime[N_BlockSections].StartRunTime;
				BlockTime[N_BlockSections].EndApproachTime = BlockTime[N_BlockSections].StartApproachTime;
				BlockTime[N_BlockSections].setupTime = 0;
				BlockTime[N_BlockSections].sightReacTime = 0;
			}

			if (N_BlockSections == N_LocationList - 1) { // for the last block section we do not have clearing time since the train unblocks the section as soon as its head arrives at the final point of the network
				BlockTime[N_BlockSections].EndClearTime = BlockTime[N_BlockSections].EndRunTime;
				BlockTime[N_BlockSections].StartClearTime = BlockTime[N_BlockSections].EndClearTime;
			}

			// Defining the StartOccupation and EndOccupationTime and all the other occupation times
			BlockTime[N_BlockSections].ApproachTime = BlockTime[N_BlockSections].EndApproachTime - BlockTime[N_BlockSections].StartApproachTime;
			BlockTime[N_BlockSections].RunTime = BlockTime[N_BlockSections].EndRunTime - BlockTime[N_BlockSections].StartRunTime;
			BlockTime[N_BlockSections].clearingTime = BlockTime[N_BlockSections].EndClearTime - BlockTime[N_BlockSections].StartClearTime;
			BlockTime[N_BlockSections].StartOccTime = BlockTime[N_BlockSections].StartApproachTime - BlockTime[N_BlockSections].setupTime - BlockTime[N_BlockSections].sightReacTime;
			BlockTime[N_BlockSections].EndOccTime = BlockTime[N_BlockSections].EndClearTime + BlockTime[N_BlockSections].ReleaseTime;

			if ((AbsoluteRTSupplement != 0) && (PercentRTSupplement == 0)) {
				BlockTime[N_BlockSections].RunTimeMargin = AbsoluteRTSupplement / (train_route[indexOfRoute].N_Block_Sections);
				if (N_BlockSections > 0) { // Add the Running time margin to the locations apart from the initial location
					BlockTime[N_BlockSections].EndOccTime = BlockTime[N_BlockSections].EndOccTime + BlockTime[N_BlockSections].RunTimeMargin;
				}
			} else if ((AbsoluteRTSupplement == 0) && (PercentRTSupplement != 0)) {
				// For ETCS3 this line must be corrected since we consider as running time the rinning time between this block section and the previous one.
				double RunTimeBetweenConsLocations = 0; // This is the running time between two consecutive locations
				if (N_BlockSections > 0) {
					RunTimeBetweenConsLocations = BlockTime[N_BlockSections].EndRunTime - BlockTime[N_BlockSections - 1].EndRunTime; // The running time is equal to the difference between the End Run Time of this locaton and the previous
				}
				// In this way the RunTimeBetweenConsecutive locations will be 0 for the first location and different from 0 for the other locations
				BlockTime[N_BlockSections].RunTimeMargin = RunTimeBetweenConsLocations * PercentRTSupplement / 100;
				// Shift the EndOccTime of the RunTimeMargin only if the block section is not the first
				BlockTime[N_BlockSections].EndOccTime = BlockTime[N_BlockSections].EndOccTime + BlockTime[N_BlockSections].RunTimeMargin;
			}

			else {
				cout << "\n\nWARNING 6: Percentage and absolute Running Time Supplements are both equal to 0 or both different from 0\n\n";
			}

			if ((BlockTime[N_BlockSections].StartOccTime < 0) || (BlockTime[N_BlockSections].EndOccTime < BlockTime[N_BlockSections].StartOccTime)) {
				BlockTime[N_BlockSections].IsComplete = false;
			}
			if (BlockTime[N_BlockSections].IsComplete == 1)
				N_BlockTimeComplete++; // Increase the number of blocking times that are complete

			N_BlockSections++; // Increase the BlockTime of the train
		}
	}

	// Function to identify if two Trains share the same route or part of it and may be conflicting
	bool AreTrainsConflicting(Train blockSets) {
		bool IsTrainConflicting = false;
		for (int i = 0; i < N_BlockTimeComplete; i++) {
			// Checking if the Train blockSets is conflicting with the train on a given block section
			for (int j = 0; j < blockSets.N_BlockTimeComplete; j++) {
				bool areBlocksConnected = false;
				areBlocksConnected = BlockTime[i].AreOnTheSameBlock(blockSets.BlockTime[j]);
				if (areBlocksConnected == 1) {
					IsTrainConflicting = true;
					break; // break the for loop over j
				}
				if (IsTrainConflicting == 1)
					break; // break the for loop over i
			}
		}
		if (IsTrainConflicting == 1)
			return true;
		else
			return false;
	}

	// Function to Reset HWMatrix and Conflicting trains for a train
	void ResetConflictingTrainsAndHwMatrix() {
		// Resetting the HwMatrix
		for (int i = 0; i < 100; i++) {
			for (int j = 0; j < 1000; j++) {
				HwMatrix[i][j] = -999999;
			}
		}
		// Resetting the list and the number of conflicting trains
		if (this->ConflictingTrains.empty() != 1) {
			this->ConflictingTrains.clear();
			this->N_ConflictingTrains = 0;
		}

		// Resetting the Locations
		if (this->LocationNames.empty() != 1) {
			this->LocationNames.clear();
		}

		// Resetting the number of overlaps
		this->numOverlaps = 0;
	}

	// Function to Set the Location Name of the Train
	void SetLocationNames() {
		for (int i = 0; i < N_BlockTimeComplete; i++) {
			string BlockName = BlockTime[i].BlockID;
			istringstream Line(BlockName);
			list<string> TOKS;
			string tok, RealBlockName;
			while (getline(Line, tok, '@')) {
				if (tok.size() > 0) {
					TOKS.push_back(tok);
				}
			}
			int k = 0;
			list<string> NameCollector;
			for (list<string>::iterator it = TOKS.begin(); it != TOKS.end(); it++) {
				if ((k == 0) || (k == 2)) {
					NameCollector.push_back(*it);
				}
				k++;
			}
			RealBlockName = *NameCollector.begin();
			if (NameCollector.size() > 1) {
				list<string>::iterator p = NameCollector.begin();
				p++;
				for (; p != NameCollector.end(); p++) {
					RealBlockName = RealBlockName + "/" + *p;
				}
			}

			if (BlockTime[i].stationName != "None") { // if the block contains a station put also the name of the station in the location
				RealBlockName = BlockTime[i].stationName + "/" + RealBlockName;
			}

			string LocationName = RealBlockName;
			Location TEMPLoc; // Creating the temporary location object where to store the Location Name
			TEMPLoc.Name = RealBlockName;
			LocationNames.push_back(TEMPLoc);
		}
	}

	// Function to compute the Headways with other trains over all locations (or block sections) along its route
	void ComputeHwOverLocations(Train blockSets) {
		bool IsTrainConflicting = false;
		for (int i = 0; i < N_BlockTimeComplete; i++) {
			// Checking if the Train blockSets is conflicting with the train on a given block section
			for (int j = 0; j < blockSets.N_BlockTimeComplete; j++) {
				bool areBlocksConnected = false;
				areBlocksConnected = BlockTime[i].AreOnTheSameBlock(blockSets.BlockTime[j]);
				if (areBlocksConnected == 1) {
					IsTrainConflicting = true;
					break; // break the for loop over j
				}
				if (IsTrainConflicting == 1)
					break; // break the for loop over i
			}
		}
		if (IsTrainConflicting == 1) {
			ConflictingTrains.push_back(blockSets.trainDescription);
			N_ConflictingTrains++;
			for (int i = 0; i < N_BlockTimeComplete; i++) {
				// Computing the Headways on the potentially conflicting block sections
				for (int j = 0; j < blockSets.N_BlockTimeComplete; j++) {
					bool areBlocksConnected = false;
					areBlocksConnected = BlockTime[i].AreOnTheSameBlock(blockSets.BlockTime[j]);
					if (areBlocksConnected == 1) {
						HwMatrix[N_ConflictingTrains - 1][i] = ComputeHwForLocation(BlockTime[i], blockSets.BlockTime[j]);
					}
				}
			}
		}
	}

	// Function to compute a matrix of departure times that allow conflict free operations with all potentially conflicting trains over all of the locations (or block sections) along its route
	void ComputeConflictFreeDepartureTimes(Train blockSets) {
		bool IsTrainConflicting = false;
		for (int i = 0; i < N_BlockTimeComplete; i++) {
			// Checking if the Train blockSets is conflicting with the train on a given block section
			for (int j = 0; j < blockSets.N_BlockTimeComplete; j++) {
				bool areBlocksConnected = false;
				areBlocksConnected = BlockTime[i].AreOnTheSameBlock(blockSets.BlockTime[j]);
				if (areBlocksConnected == 1) {
					IsTrainConflicting = true;
					break; // break the for loop over j
				}
				if (IsTrainConflicting == 1)
					break; // break the for loop over i
			}
		}
		if (IsTrainConflicting == 1) {
			ConflictingTrains.push_back(blockSets.trainDescription);
			N_ConflictingTrains++;
			for (int i = 0; i < N_BlockTimeComplete; i++) {
				// Computing the Headways on the potentially conflicting block sections
				for (int j = 0; j < blockSets.N_BlockTimeComplete; j++) {
					bool areBlocksConnected = false;
					areBlocksConnected = BlockTime[i].AreOnTheSameBlock(blockSets.BlockTime[j]);
					if (areBlocksConnected == 1) {
						// The critical HW must be computed only if the blocking time of this train starts after the one of train blockSets, or if it starts earlier but ends later
						HwMatrix[N_ConflictingTrains - 1][i] = ComputeDepartureTimesToSolveConflicts(BlockTime[i], blockSets.BlockTime[j], this->departure_time, blockSets.departure_time);
					}
				}
			}
		}
	}

	// Function to compute the HW to produce the timetable
	void ComputeDepartureTimesForConflictFreeTimetable(Train blockSets) {
		bool IsTrainConflicting = false;
		for (int i = 0; i < N_BlockTimeComplete; i++) {
			// Checking if the Train blockSets is conflicting with the train on a given block section
			for (int j = 0; j < blockSets.N_BlockTimeComplete; j++) {
				bool areBlocksConnected = false;
				areBlocksConnected = BlockTime[i].AreOnTheSameBlock(blockSets.BlockTime[j]);
				if (areBlocksConnected == 1) {
					IsTrainConflicting = true;
					break; // break the for loop over j
				}
				if (IsTrainConflicting == 1)
					break; // break the for loop over i
			}
		}
		if (IsTrainConflicting == 1) {
			ConflictingTrains.push_back(blockSets.trainDescription);
			N_ConflictingTrains++;
			for (int i = 0; i < N_BlockTimeComplete; i++) {
				// Computing the Headways on the potentially conflicting block sections
				for (int j = 0; j < blockSets.N_BlockTimeComplete; j++) {
					bool areBlocksConnected = false;
					areBlocksConnected = BlockTime[i].AreOnTheSameBlock(blockSets.BlockTime[j]);
					if (areBlocksConnected == 1) {
						// The critical HW must be computed only if the blocking time of this train starts after the one of train blockSets, or if it starts earlier but ends later
						bool AreBlocksOverlapped = BlockTime[i].AreBlocksOverlapping(blockSets.BlockTime[j]);
						if ((BlockTime[i].StartOccTime >= blockSets.BlockTime[j].StartOccTime) || (BlockTime[i].EndOccTime > blockSets.BlockTime[j].StartOccTime) || (AreBlocksOverlapped == 1)) {
							HwMatrix[N_ConflictingTrains - 1][i] = ComputeDepartureTimesToSolveConflicts(BlockTime[i], blockSets.BlockTime[j], this->departure_time, blockSets.departure_time);
						}
					}
				}
			}
		}
	}

	// Function to compute the HW to produce the timetable
	void ComputeDepartureTimesForConflictFreeTimetableImproved(Train blockSets) {
#pragma omp parallel
		{
#pragma omp for
			for (int i = 0; i < N_BlockTimeComplete; i++) {
				// Computing the Headways on the potentially conflicting block sections
				for (int j = 0; j < blockSets.N_BlockTimeComplete; j++) {
					bool areBlocksConnected = false;
					areBlocksConnected = BlockTime[i].AreOnTheSameBlock(blockSets.BlockTime[j]);
					if (areBlocksConnected == 1) {
						// The critical HW must be computed only if the blocking time of this train starts after the one of train blockSets, or if it starts earlier but ends later
						bool AreBlocksOverlapped = BlockTime[i].AreBlocksOverlapping(blockSets.BlockTime[j]);
						if ((BlockTime[i].StartOccTime >= blockSets.BlockTime[j].StartOccTime) || (BlockTime[i].EndOccTime > blockSets.BlockTime[j].StartOccTime) || (AreBlocksOverlapped == 1)) {
							HwMatrix[N_ConflictingTrains - 1][i] = ComputeDepartureTimesToSolveConflicts(BlockTime[i], blockSets.BlockTime[j], this->departure_time, blockSets.departure_time);
						}
					}
				}
			}
		}
	}

	// Function to compute the HW to produce the timetable
	void ComputeDepartureTimesForConflictFreeTimetableImproved2(Train blockSets, int IndexConflictingTrain) {
		for (int i = 0; i < N_BlockTimeComplete; i++) {
			// Computing the Headways on the potentially conflicting block sections
			for (int j = 0; j < blockSets.N_BlockTimeComplete; j++) {
				bool areBlocksConnected = false;
				areBlocksConnected = BlockTime[i].AreOnTheSameBlock(blockSets.BlockTime[j]);
				if (areBlocksConnected == 1) {
					// The critical HW must be computed only if the blocking time of this train starts after the one of train blockSets, or if it starts earlier but ends later
					bool AreBlocksOverlapped = BlockTime[i].AreBlocksOverlapping(blockSets.BlockTime[j]);
					if ((BlockTime[i].StartOccTime >= blockSets.BlockTime[j].StartOccTime) || (BlockTime[i].EndOccTime > blockSets.BlockTime[j].StartOccTime) || (AreBlocksOverlapped == 1)) {
						HwMatrix[IndexConflictingTrain][i] = ComputeDepartureTimesToSolveConflicts(BlockTime[i], blockSets.BlockTime[j], this->departure_time, blockSets.departure_time);
					}
				}
			}
		}
	}

	// Function to determine the critical Headway per Location and the couple of trains which are critical
	void DetermineCriticalHWForAllLocations() {
		if (this->LocationNames.size() > 0) {
			int loc = 0;
			for (list<Location>::iterator it = LocationNames.begin(); it != LocationNames.end(); it++) {
				double MAXLocHW = -1;
				string CritCouple = "None";
				double MINLocHW = 9999999;
				string MinCouple = "None";

				if (this->N_ConflictingTrains > 0) {
					int tr = 0; // pointer on the conflicting trains
					for (list<string>::iterator ct = ConflictingTrains.begin(); ct != ConflictingTrains.end(); ct++) {
						if (this->HwMatrix[tr][loc] > MAXLocHW) {
							MAXLocHW = HwMatrix[tr][loc];
							CritCouple = *ct;
						}
						if ((this->HwMatrix[tr][loc] < MINLocHW) && (this->HwMatrix[tr][loc] > 0)) {
							MINLocHW = HwMatrix[tr][loc];
							MinCouple = *ct;
						}
						tr++; // advance the train pointer
					}
				}
				// Assigning the max HW and the critical couple for the section as well as the MinHW and the minimum train couple
				it->MaxHW = MAXLocHW;
				it->CriticalTrainCouple = this->trainDescription + "-" + CritCouple;
				it->MinHW = MINLocHW;
				it->MinimumTrainCouple = this->trainDescription + "-" + MinCouple;

				loc++; // advance the location pointer
			}
		}
	}

	// Function to Compute the Headway Matrix
	void ComputeHwMatrix(Train* T, int numTrains) {
		for (int i = 0; i < numTrains; i++) {
			if (T[i].trainDescription != trainDescription) {
				this->ComputeHwOverLocations(T[i]);
			}
		}
	}

	// Function to Compute the Headway Matrix for a given order
	void ComputeHwMatrixForGivenOrder(Train* T, int numTrains, OrderList OrderedTrainArray) {
		int IndexOfThisTrain = -1;
		bool HWwithNextTrainComputed = false;
		for (int j = 0; j < OrderedTrainArray.numTeList; j++) {
			if (this->trainDescription == OrderedTrainArray.TE[j].trainDescription) {
				IndexOfThisTrain = j;
				break; // break the for loop
			}
		}
		// Now we need to compute the headway matrix against the train that follows THIS train on its route or part of it
		for (int j = 0; j < OrderedTrainArray.numTeList; j++) {
			if (j > IndexOfThisTrain) {
				for (int i = 0; i < numTrains; i++) {
					bool PotentialTrainConflicting = false;
					if (T[i].trainDescription == OrderedTrainArray.TE[j].trainDescription) {
						PotentialTrainConflicting = this->AreTrainsConflicting(T[i]);
					}
					// if the trains have a partial or a total part of the network in common then compute the relative headway Matrix
					if (PotentialTrainConflicting == 1) {
						this->ComputeHwOverLocations(T[i]);
						HWwithNextTrainComputed = true;
						break; // break the for loop over N_trains
					}
				}
			}
			// if the HWwith the NExt Train has been computed then break the loop over the ordereTrainArray
			if (HWwithNextTrainComputed == 1)
				break;
		}
	}

	// Function to Compute the Headway Matrix for a given order
	void DepartureMatrixToSolveConflictsForGivenOrder(Train* T, int numTrains, int IndexOfTrain, int* ListOfTrainIndices) {

		int k = IndexOfTrain - 1; // Initialize k from the train that departed just before the current train
		while (k >= 0) {
			if (this->AreTrainsConflicting(T[ListOfTrainIndices[k]]) == 1) {
				this->ComputeDepartureTimesForConflictFreeTimetable(T[ListOfTrainIndices[k]]);
				// this->ComputeConflictFreeDepartureTimes(regional_train[ListOfTrainIndices[k]]);
			}

			k--;
		}
	}

	// Function to Compute the Headway Matrix for a given order
	void DepartureMatrixToSolveConflictsForGivenOrderImproved(Train* T, int numTrains, int IndexOfTrain, int* ListOfTrainIndices) {

		int k = IndexOfTrain - 1; // Initialize k from the train that departed just before the current train
		while (k >= 0) {
			if (this->AreTrainsConflicting(T[ListOfTrainIndices[k]]) == 1) {
				ConflictingTrains.push_back(T[ListOfTrainIndices[k]].trainDescription); // pushing the conflicting train in the list of conflicting trains
				N_ConflictingTrains++;													// increasing the number of conflicting trains
				this->ComputeDepartureTimesForConflictFreeTimetableImproved(T[ListOfTrainIndices[k]]);
				// this->ComputeConflictFreeDepartureTimes(regional_train[ListOfTrainIndices[k]]);
			}

			k--;
		}
	}

	// Function to Compute the Headway Matrix for a given order
	void DepartureMatrixToSolveConflictsForGivenOrderImproved2(Train* T, int numTrains, int IndexOfTrain, int* ListOfTrainIndices) {

		int k = IndexOfTrain - 1; // Initialize k from the train that departed just before the current train
		int IndicesConflictingTrains[200];
		int N_IndicesConflictingTrains = 0;
		while (k >= 0) {
			if (this->AreTrainsConflicting(T[ListOfTrainIndices[k]]) == 1) {
				ConflictingTrains.push_back(T[ListOfTrainIndices[k]].trainDescription); // pushing the conflicting train in the list of conflicting trains
				N_ConflictingTrains++;													// increasing the number of conflicting trains
				IndicesConflictingTrains[N_IndicesConflictingTrains] = ListOfTrainIndices[k];
				N_IndicesConflictingTrains++;
			}
			k--;
		}

#pragma omp parallel
		{
#pragma omp for
			for (int i = 0; i < N_IndicesConflictingTrains; i++) {
				this->ComputeDepartureTimesForConflictFreeTimetableImproved2(T[IndicesConflictingTrains[i]], i);
				// this->ComputeConflictFreeDepartureTimes(regional_train[ListOfTrainIndices[k]]);
			}
		}
	}

	// Function to Compute the Headway Matrix for a given order (Parallel execution)
	void DepartureMatrixToSolveConflictsForGivenOrderImproved3(Train* T, int numTrains, int IndexOfTrain, int* ListOfTrainIndices) {

		bool AreTrainsInConflict = false;
		int IndicesConflictingTrains[200];
		int N_IndicesConflictingTrains = 0;
#pragma omp parallel private(AreTrainsInConflict)
		{
#pragma omp for
			for (int k = IndexOfTrain - 1; k >= 0; k--) {
				AreTrainsInConflict = this->AreTrainsConflicting(T[ListOfTrainIndices[k]]);
				if (AreTrainsInConflict == 1) {
#pragma omp critical
					{
						ConflictingTrains.push_back(T[ListOfTrainIndices[k]].trainDescription); // pushing the conflicting train in the list of conflicting trains
						N_ConflictingTrains++;													// increasing the number of conflicting trains
						IndicesConflictingTrains[N_IndicesConflictingTrains] = ListOfTrainIndices[k];
						N_IndicesConflictingTrains++;
					}
				}
			}
		}

#pragma omp parallel
		{
#pragma omp for
			for (int i = 0; i < N_IndicesConflictingTrains; i++) {
				this->ComputeDepartureTimesForConflictFreeTimetableImproved2(T[IndicesConflictingTrains[i]], i);
				// this->ComputeConflictFreeDepartureTimes(regional_train[ListOfTrainIndices[k]]);
			}
		}
	}

	// Detecting the number of conflicts in a parallel fashion
	void DetectNumberOfConflicts(Train blockSets) {
		int numConflicts = 0;
		for (int i = 0; i < N_BlockTimeComplete; i++) {
			for (int j = 0; j < blockSets.N_BlockTimeComplete; j++) {
				bool areBlocksConnected = false;
				areBlocksConnected = BlockTime[i].AreOnTheSameBlock(blockSets.BlockTime[j]);
				if (areBlocksConnected == 1) {
					if (BlockTime[i].StartOccTime <= blockSets.BlockTime[j].StartOccTime) {
						if ((int)blockSets.BlockTime[j].StartOccTime - (int)BlockTime[i].EndOccTime < 0) { // if the blocks are overlapped
							this->numOverlaps++;													   // then incease the number of conflicts
							numConflicts++;
							cout << "Train " << this->trainDescription << "overlaps with Train " << blockSets.trainDescription << " in section " << this->BlockTime[i].BlockID << "\n\n";
						}
					} else if (blockSets.BlockTime[j].StartOccTime <= BlockTime[i].StartOccTime) {
						if ((int)BlockTime[i].StartOccTime - (int)blockSets.BlockTime[j].EndOccTime < 0) { // if the blocks are overlapped
							this->numOverlaps++;													   // then increase the number of conflicts
							numConflicts++;
							cout << "Train " << this->trainDescription << "overlaps with Train " << blockSets.trainDescription << " in section " << this->BlockTime[i].BlockID << "\n\n";
						}
					}
				}
			}
		}

		cout << "Train " << this->trainDescription << " has " << numConflicts << " overlaps with train " << blockSets.trainDescription << "\n";
	}

	void DetectConflictsWithPreviousDepartingTrains(Train* Trains, int numTrains) {
		if (this->ConflictingTrains.empty() != 1) {
			string TrainsConflicting[200];
			int N_TrainsConflicting = 0;
			for (list<string>::iterator it = ConflictingTrains.begin(); it != ConflictingTrains.end(); it++) {
				TrainsConflicting[N_TrainsConflicting] = *it;
				N_TrainsConflicting++;
			}
#pragma omp parallel
			{
#pragma omp for
				for (int j = 0; j < N_TrainsConflicting; j++) {
					for (int i = 0; i < numTrains; i++) {
						if (Trains[i].trainDescription == TrainsConflicting[j]) {
							this->DetectNumberOfConflicts(Trains[i]);
							break; // break the loop over trains and pass to the next conflicting train
						}
					}
				}
			}
		}
	}

	// Function to shift the blocking times of the train
	void ShiftBlockingTimes(double Shift) {
		// Assign the new shifted departure time and the new shifted End_time to the train
		departure_time = departure_time + Shift;
		// Defining the End Time that cannot be later than the simulation time
		this->End_Time = int(End_Time + Shift);
		if (End_Time > initial_variables.times)
			End_Time = (int)initial_variables.times;

		// Shift the TimetablingPoints
		if (this->TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator it = TimetablePoints.begin(); it != TimetablePoints.end(); it++) {
				it->Time = it->Time + Shift;
				it->Time2 = it->Time2 + Shift;
			}
		}

		// Need to shift also blocking times
		for (int i = 0; i < this->N_BlockTimeComplete; i++) {
			this->BlockTime[i].StartRunTime = BlockTime[i].StartRunTime + Shift;
			this->BlockTime[i].EndRunTime = BlockTime[i].EndRunTime + Shift;
			this->BlockTime[i].StartApproachTime = BlockTime[i].StartApproachTime + Shift;
			this->BlockTime[i].EndApproachTime = BlockTime[i].EndApproachTime + Shift;
			this->BlockTime[i].StartClearTime = BlockTime[i].StartClearTime + Shift;
			this->BlockTime[i].EndClearTime = BlockTime[i].EndClearTime + Shift;
			this->BlockTime[i].StartOccTime = BlockTime[i].StartOccTime + Shift;
			this->BlockTime[i].EndOccTime = BlockTime[i].EndOccTime + Shift;
		}
	}

	// Function to shift the train in order to compress the Timetable and determine the max capacity
	void ShiftTrainToCompressTT(Train* T, int numTrains, string TEMPFolderForTrajectory, string MainFolderName) {

		int k = 0; // this is the counter on the number of conflicting trains
				   /*for (list<string>::iterator it=this->ConflictingTrains.begin();it!=this->ConflictingTrains.end();it++){
				   double DepTimeConflTrain=-1;     //This is the Departure time of the it conflicting train
				   for (int i=0;i<numTrains;i++){
				   if (regional_train[i].trainDescription==*it){
				   DepTimeConflTrain=regional_train[i].departure_time;   //Assigning the departure time of the train
				   break;
				   }
				   }
				   for (int j=0;j<this->N_BlockTimeComplete;j++){
				   if (this->HwMatrix[k][j]!=-999999){
				   //The HwMatrix is converted in to a Matrix of Departures
				   HwMatrix[k][j]=DepTimeConflTrain+this->HwMatrix[k][j];}
		   
				   }
				   k++;  //increase the k iterator
				   }*/
				   // Once the Matrix of departure is realized then we must consider the latest conflict free departure of the train
		double LatestDepTime = -999999;
		string ConstrainingTrain, ConstrainingBS;					 // These strings are to visualize with which train there are conflicting problems and in which block section
		list<string>::iterator tr = this->ConflictingTrains.begin(); // Pointer to the list of conflicting trains
		for (int i = 0; i < this->N_ConflictingTrains; i++) {
			for (int j = 0; j < this->N_BlockTimeComplete; j++) {
				if (HwMatrix[i][j] > LatestDepTime) {
					LatestDepTime = HwMatrix[i][j];
					ConstrainingTrain = *tr;
					ConstrainingBS = BlockTime[j].BlockID;
				}
			}
			tr++; // advance of 1 position the pointer to the conflicting trains
		}

		ofstream ConflictOutput;
		string ConflOutputName;
		ConflOutputName = ConflOutputName + MainFolderName + "/" + "AllTrainConflicts.txt";
		ConflictOutput.open((char*)ConflOutputName.c_str(), ios::app);

		// Defining the Shift of the train for the compression
		int Shift = 0;

		if (LatestDepTime != -999999) {
			cout << "\nTrain " << this->trainDescription << " conflicts with Train " << ConstrainingTrain << " in location " << ConstrainingBS << "\n";
			// writing the same string in the ConflictOutput file
			ConflictOutput << "\nTrain " << this->trainDescription << " conflicts with Train " << ConstrainingTrain << " in location " << ConstrainingBS << "\n";
			// Now we need to shift the train by the maximum departure time, this means that the train can be shifted both earlier or later than its current departure time
			Shift = (int)(LatestDepTime - this->departure_time);
		}

		else {
			// if the train is not conflicting with any other train so we shift it back of its departure time, in order that it starts from 0
			cout << "\nTrain " << this->trainDescription << " does not have conflicts\n";
			// writing the same string to the ConflictOutput file
			ConflictOutput << "\nTrain " << this->trainDescription << " does not have conflicts\n";
			Shift = int(0 - this->departure_time);
		}

		// Close the ConflictOutput file
		ConflictOutput.close();

		// Assign the new shifted departure time and the new shifted End_time to the train
		departure_time = departure_time + Shift;
		// Defining the End Time that cannot be later than the simulation time
		this->End_Time = End_Time + Shift;
		if (End_Time > initial_variables.times)
			End_Time = (int)initial_variables.times;

		// Shift the TimetablingPoints
		if (this->TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator it = TimetablePoints.begin(); it != TimetablePoints.end(); it++) {
				it->Time = it->Time + Shift;
				it->Time2 = it->Time2 + Shift;
			}
		}
		/* By commenting this part we are not shifting the trajectory of the trains but just the blocking times, the departure_time and the End_Time
		//Writing a txt file with the shifted trajectory
		string TrajectoryFileName;
		TrajectoryFileName=TEMPFolderForTrajectory + "/TEMPTraj" + this->trainDescription+ ".txt";
		ofstream TEMPFileTraj;
		TEMPFileTraj.open((char*)TrajectoryFileName.c_str(), ios::binary);

		for (int t=0;t<times;t++){
		double TEMP_S=0, TEMP_V=0, TEMP_Pw=0, TEMP_E=0;
		if ((t>departure_time)&&(t<=End_Time)){   //The shift can be made only if the t+Shift is equal or higher thna 0, so the part of the train existing before time 0 is not considered
		if (t-Shift>=0){
		TEMP_S=instant_spatial_position[t-Shift]; TEMP_V=instant_train_speed[t-Shift];  TEMP_Pw=instant_train_power_consumption[t-Shift];  TEMP_E=instant_train_energy_consumption[t-Shift];
		}
		}
		else{
		TEMP_S=0; TEMP_V=0;  TEMP_Pw=0;  TEMP_E=0;
		//if the train ends its run before the simulation time then we put -9999 at instant_spatial_position [End_Time] to indicate that the simulation is finished for that train
		if (t==End_Time+1) TEMP_S=-9999;
		}

		//Write the trajectory in the file
		TEMPFileTraj<< t <<" "<<TEMP_S<<" "<<TEMP_V<<" "<<TEMP_Pw<<" "<<TEMP_E<<"\n";
		}
		TEMPFileTraj.close();

		//Clear all the original trajectory before the shift
		for (int i=0;i<times;i++){
		instant_spatial_position[i]=0;  instant_train_speed[i]=0; instant_train_energy_consumption[i]=0; instant_train_power_consumption[i]=0;
		}

		//Assign the shifted trajectory to the original train properties instant_spatial_position[t], instant_train_speed[t], instant_train_energy_consumption[t] and instant_train_power_consumption[t]
		//Take the trajectory from the file
		ifstream ShiftedTraj;
		ShiftedTraj.open((char*)TrajectoryFileName.c_str(),ios::binary);   //opening the file just published
		if (!ShiftedTraj) {cout<<"ERROR: Impossible to Open Shifted Trajectory of train "<<this->trainDescription<<"\n\n";}
		else{
		string FileLine;
		int linecounter=0;
		while(getline(ShiftedTraj,FileLine)){
		istringstream LineStream(FileLine);
		string Element;
		int ElementCounter=0;
		while (LineStream>> Element){
		if (ElementCounter==1) 	this->instant_spatial_position[linecounter]=atof((char*)Element.c_str());
		else if (ElementCounter==2) this->instant_train_speed[linecounter]=atof((char*)Element.c_str());
		else if (ElementCounter==3) this->instant_train_power_consumption[linecounter]=atof((char*)Element.c_str());
		else if (ElementCounter==4) this->instant_train_energy_consumption[linecounter]=atof((char*)Element.c_str());
		ElementCounter++;
		}
		linecounter++;

		}
		}
		ShiftedTraj.close();  //close the text file containing the shifted trajectory

		this->PrintTrajectory();
		*/
		// Need to shift also blocking times
		for (int i = 0; i < this->N_BlockTimeComplete; i++) {
			this->BlockTime[i].StartRunTime = BlockTime[i].StartRunTime + Shift;
			this->BlockTime[i].EndRunTime = BlockTime[i].EndRunTime + Shift;
			this->BlockTime[i].StartApproachTime = BlockTime[i].StartApproachTime + Shift;
			this->BlockTime[i].EndApproachTime = BlockTime[i].EndApproachTime + Shift;
			this->BlockTime[i].StartClearTime = BlockTime[i].StartClearTime + Shift;
			this->BlockTime[i].EndClearTime = BlockTime[i].EndClearTime + Shift;
			this->BlockTime[i].StartOccTime = BlockTime[i].StartOccTime + Shift;
			this->BlockTime[i].EndOccTime = BlockTime[i].EndOccTime + Shift;
		}
	}

	// Shift Train in attempt
	void ShiftTrain() {

		double LatestDepTime = -999999;
		string ConstrainingTrain, ConstrainingBS;					 // These strings are to visualize with which train there are conflicting problems and in which block section
		list<string>::iterator tr = this->ConflictingTrains.begin(); // Pointer to the list of conflicting trains
		for (int i = 0; i < this->N_ConflictingTrains; i++) {
			for (int j = 0; j < this->N_BlockTimeComplete; j++) {
				if (HwMatrix[i][j] > LatestDepTime) {
					LatestDepTime = HwMatrix[i][j];
					ConstrainingTrain = *tr;
					ConstrainingBS = BlockTime[j].BlockID;
				}
			}
			tr++; // advance of 1 position the pointer to the conflicting trains
		}

		// Defining the Shift of the train for the compression
		int Shift = 0;

		if (LatestDepTime != -999999) {
			cout << "\nTrain " << this->trainDescription << " conflicts with Train " << ConstrainingTrain << " in location " << ConstrainingBS << "\n";
			// Now we need to shift the train by the maximum departure time, this means that the train can be shifted both earlier or later than its current departure time
			Shift = (int)(LatestDepTime - this->departure_time);
			this->ShiftBlockingTimes(Shift); // shift the train
		} else {
			// if the train is not conflicting with any other train so we shift it back of its departure time, in order that it starts from 0
			cout << "\nTrain " << this->trainDescription << " does not have conflicts\n";
			// if the train does not have conflicts then shift it to 0
			Shift = int(0 - this->departure_time);
			this->ShiftBlockingTimes(Shift); // shift the train
		}

		cout << "Train " << this->trainDescription << " will now depart at " << departure_time;
	}

	// Function to shift the train in order to compress the Timetable and determine the max capacity
	void ShiftTrainToCompressTTTrial(Train* T, int numTrains) {

		int k = 0; // this is the counter on the number of conflicting trains
		for (list<string>::iterator it = this->ConflictingTrains.begin(); it != this->ConflictingTrains.end(); it++) {
			double DepTimeConflTrain = -1; // This is the Departure time of the it conflicting train
			for (int i = 0; i < numTrains; i++) {
				if (T[i].trainDescription == *it) {
					DepTimeConflTrain = T[i].departure_time; // Assigning the departure time of the train
					break;
				}
			}
			for (int j = 0; j < this->N_BlockTimeComplete; j++) {
				if (this->HwMatrix[k][j] != -999999) {
					// The HwMatrix is converted in to a Matrix of Departures
					HwMatrix[k][j] = DepTimeConflTrain + this->HwMatrix[k][j];
				}
			}
			k++; // increase the k iterator
		}
		// Once the Matrix of departure is realized then we must consider the latest conflict free departure of the train
		double LatestDepTime = -999999;
		string ConstrainingTrain, ConstrainingBS;					 // These strings are to visualize with which train there are conflicting problems and in which block section
		list<string>::iterator tr = this->ConflictingTrains.begin(); // Pointer to the list of conflicting trains
		for (int i = 0; i < this->N_ConflictingTrains; i++) {
			for (int j = 0; j < this->N_BlockTimeComplete; j++) {
				if (HwMatrix[i][j] > LatestDepTime) {
					LatestDepTime = HwMatrix[i][j];
					ConstrainingTrain = *tr;
					ConstrainingBS = BlockTime[j].BlockID;
				}
			}
			tr++; // advance of 1 position the pointer to the conflicting trains
		}
		if (LatestDepTime != -999999) {
			cout << "\nTrain " << this->trainDescription << " conflicts with Train " << ConstrainingTrain << " in location" << ConstrainingBS << "\n";
		} else
			cout << "\nTrain " << this->trainDescription << " does not have conflicts\n";

		// Now we need to shift the train by the maximum departure time, this means that the train can be shifted both earlier or later than its current departure time
		int Shift = (int)(LatestDepTime - this->departure_time);

		// Assign the new shifted departure time and the new shifted End_time to the train
		departure_time = LatestDepTime;
		this->End_Time = End_Time + Shift;

		// double TEMP_S[9500], TEMP_V[9500], TEMP_Pw[9500], TEMP_E[9500];    //These are temporary vectors to shift all the variables of the train in time

		if (Shift > 0) {
			for (int t = initial_variables.times; t >= 0; t--) {
				if (t + Shift <= initial_variables.times) {
					instant_spatial_position[t + Shift] = instant_spatial_position[t];
				}

				if (t < this->departure_time) { // Put all Train parameters to 0 for all instants prior to shifted departure time
					instant_spatial_position[t] = 0;
					instant_train_speed[t] = 0;
					instant_train_power_consumption[t] = 0;
					instant_train_energy_consumption[t] = 0;
				}
				// Setting -99999 to instant_spatial_position[End_Time+1], to indicate that the train run finishes after the shifted End_Time
				if ((t + Shift) == End_Time + 1)
					instant_spatial_position[t + Shift] = -9999;
			}
		} else {
			for (int t = 0; t < initial_variables.times; t++) {
				if (t + Shift >= 0) {
					instant_spatial_position[t + Shift] = instant_spatial_position[t];
				}
				if (t < this->departure_time) { // Put all Train parameters to 0 for all instants prior to shifted departure time
					instant_spatial_position[t] = 0;
					instant_train_speed[t] = 0;
					instant_train_power_consumption[t] = 0;
					instant_train_energy_consumption[t] = 0;
				}
				// Setting -99999 to instant_spatial_position[End_Time+1], to indicate that the train run finishes after the shifted End_Time
				if ((t + Shift) == End_Time + 1)
					instant_spatial_position[t + Shift] = -9999;
			}
		}

		// Need to shift also blocking times
		for (int i = 0; i < this->N_BlockTimeComplete; i++) {
			this->BlockTime[i].StartRunTime = BlockTime[i].StartRunTime + Shift;
			this->BlockTime[i].EndRunTime = BlockTime[i].EndRunTime + Shift;
			this->BlockTime[i].StartApproachTime = BlockTime[i].StartApproachTime + Shift;
			this->BlockTime[i].EndApproachTime = BlockTime[i].EndApproachTime + Shift;
			this->BlockTime[i].StartClearTime = BlockTime[i].StartClearTime + Shift;
			this->BlockTime[i].EndClearTime = BlockTime[i].EndClearTime + Shift;
			this->BlockTime[i].StartOccTime = BlockTime[i].StartOccTime + Shift;
			this->BlockTime[i].EndOccTime = BlockTime[i].EndOccTime + Shift;
		}
	}


	// This function search for a train that when follows this train reduces capacity consumption
	void SearchNextTrainThatMaximizeCapacity(list<string>& OptimalTrainSequenceList) {
		bool TrainIsAlreadyThere = false;
		string TrainOptimisingSequence = "Unidentified"; // This is the train description of the train optimising the sequence of trains
		double MinAverageHW = 99999999;					 // This variable is the minimum average headway computed over all the shared block sections
		int IndexTrain = 0;								 // This indicates the index of the train to investigate in the list of trains
		if (this->N_ConflictingTrains > 0) {
			for (list<string>::iterator y = ConflictingTrains.begin(); y != ConflictingTrains.end(); y++) {
				int N_SharedLocations = 0;	   // This is the number of locations shared with the following trains
				double AverageHWwithTrain = 0; // This is instead the average HW with respect to the following train to investigate
				for (int i = 0; i < this->N_BlockTimeComplete; i++) {
					if (this->HwMatrix[IndexTrain][i] != -1) {
						N_SharedLocations++;											   // Increasing the number of shared block sections
						AverageHWwithTrain = AverageHWwithTrain + HwMatrix[IndexTrain][i]; // Adding up all teh headways on the shared block sections
					}
				}
				AverageHWwithTrain = AverageHWwithTrain / N_SharedLocations; // Computing the average headway over all the shared block sections
				if (AverageHWwithTrain < MinAverageHW) {
					MinAverageHW = AverageHWwithTrain;
					TrainOptimisingSequence = *y; // This is the name of the train minimisig the average headway in the sequence
				}
				IndexTrain++; // Increase the Index of the train
			}
		}
		if (TrainOptimisingSequence != "Unidentified") { // If a train is identified then add it to the TrainOptimisingSequence List
			for (list<string>::iterator p = OptimalTrainSequenceList.begin(); p != OptimalTrainSequenceList.end(); p++) {
				if (*p == TrainOptimisingSequence) {
					TrainIsAlreadyThere = true;
					break;
				}
			}
			if (TrainIsAlreadyThere == 0) // Only if the Train is not already there insert the optimal train in the list of optimal trains
				OptimalTrainSequenceList.push_back(TrainOptimisingSequence);
			else { // do nothing
			}
		}
	}


	// Print Headway Matrix of a single Train
	void PrintHeadwayMatrix(string MainFolder) {
		string FileName;
		FileName = FileName + MainFolder + "/HwMatrix_" + trainDescription + ".txt";
		ofstream OutputFile;
		OutputFile.open((char*)FileName.c_str(), ios::binary);
		int N_Locations = LocationNames.size();
		OutputFile << "Trains/Locations ";
		for (list<Location>::iterator k = LocationNames.begin(); k != LocationNames.end(); k++) {
			OutputFile << k->Name << " ";
		}

		OutputFile << "\n";

		list<string>::iterator q = ConflictingTrains.begin();
		for (int i = 0; i < N_ConflictingTrains; i++) {
			OutputFile << *q << " "; // Printing the name of the conflicting train
			for (int j = 0; j < N_Locations; j++) {
				OutputFile << HwMatrix[i][j] << " ";
			}
			q++;
			OutputFile << "\n";
		}

		OutputFile.close();
	}

	// Function to calculate Train Running Time without Taking into account its dwell times at Stations (No Stop Running Time)[in seconds]
	double No_Stop_Running_Time() {
		int StopAtStat = 0; // This variable represents the sum of the whole dwell time the train stops at
		for (int s = 0; s < numStations; s++) {
			StopAtStat = StopAtStat + (int)Stations[s].StopTime;
		}
		double NoStopRunningTime = 0;
		NoStopRunningTime = End_Time + 1 - (int)departure_time - StopAtStat;
		return NoStopRunningTime * timestep;
	}

	// Function to evaluate the time instant in which for the first time the train Starts to accelerate (It's the time corresponding to the first non-zero speed value)
	double Det_Run_Start_Time() {
		for (int t = 0; t < End_Time; t++) {
			if (instant_train_speed[t] > 0) {
				RunStartTime = t - 1;
				break;
			}
		}
		return RunStartTime * timestep;
	}

	// Function to pick up time instant in which the train actually enters a specific station "instant_spatial_position" during simulation
	double Arrival_At_Station(Node ST) {
		int T = -1;
		for (int t = 0; t <= End_Time; t++) {
			if ((instant_spatial_position[t] == ST.X * 1000 - 0.002) && (instant_train_speed[t] == 0)) {
				T = t;
				break;
			}
		}
		return (double)(T * timestep);
	}

	// Function to pick up time instant in which the train actually enters a specific station "instant_spatial_position" during simulation
	double Arrival_At_Station_NewVersion(Node ST) {
		int T = -1;
		for (int t = 1; t <= End_Time; t++) {
			if (((instant_spatial_position[t - 1] < ST.X * 1000 - 3) && (instant_spatial_position[t] > ST.X * 1000 - 0.005)) && (instant_train_speed[t] == 0)) {
				T = t;
				break;
			}
		}
		return (double)(T * timestep);
	}

	// Function to calculate actual train arrival istants at each station
	void Actual_Arrivals() {
		for (int s = 0; s < numStations; s++) {	   // looping among the number of stations of the train
			for (int h = 0; h < numStations; h++) { // looping among the total number of StationArray
				if (Stations[s].stationName == StationArray[h].stationName)
					StationArrivals[s] = Arrival_At_Station_NewVersion(StationArray[h]);
			}
		}
	}

	// Function to calculate actual train arrival istants at each station
	void Actual_Arrivals_NewVersion() {
		for (int s = 0; s < numStations; s++) { // looping among the number of stations of the train
			StationArrivals[s] = Arrival_At_Station_NewVersion(Stations[s]);
		}
	}

	// Function to setup the station arrivals of the trains based on the computed arrivals/departures at the timetabling points
	// This function replaces those functions above "Actual_Arrivals_NewVersion" and "Actual_Arrivals"
	void Determine_Actual_Station_Arrivals() {
		if (!this->TimetablePoints.empty()) {
			for (int s = 0; s < numStations; s++) {
				for (list<TrainEvent>::iterator TT = TimetablePoints.begin(); TT != TimetablePoints.end(); TT++) {
					// Live service-stop captures are authoritative; timetable points fill only missing values.
					if (StationArrivals[s] < 0 && TT->SuccessorID == stationNameForArrivalStats(s) && TT->Time >= 0) {
						StationArrivals[s] = TT->Time;
						StationArrivalNames[s] = TT->SuccessorID;
						break;
					}
				}
			}
		}

		const double toleranceMeters = 5.0;
		const int sampleCount = trajectorySize();
		for (int s = 0; s < numStations; s++) {
			if (StationArrivals[s] >= 0)
				continue;

			const double stationMeters = stationRoutePositionMeters(s);
			if (stationMeters < 0)
				continue;
			for (int t = 1; t < sampleCount; t++) {
				if (instant_spatial_position[t - 1] == -9999 || instant_spatial_position[t] == -9999)
					continue;

				const double previous = trainXPosition(t - 1) * 1000.0;
				const double current = trainXPosition(t) * 1000.0;

				const double low = std::min(previous, current) - toleranceMeters;
				const double high = std::max(previous, current) + toleranceMeters;
				if (stationMeters >= low && stationMeters <= high) {
					StationArrivals[s] = t * timestep;
					if (StationArrivalNames[s] == "None")
						StationArrivalNames[s] = Stations[s].stationName;
					break;
				}
			}
		}
	}

	// Function compute Arrival delays at stations for a train
	void computeArrivalDelaysAtStations() {
		TotalInputDelays = EntranceDelay;
		for (int s = 0; s < numStations; s++) {
			// Calculate Total Delay in input
			TotalInputDelays = TotalInputDelays + StationDisturbance[s];
			// if the train is arrived at station s
			if (StationArrivals[s] < 0 && ScheduledArrivals[s] >= 0)
				StationArrivals[s] = ScheduledArrivals[s];
			if (StationArrivals[s] != -1) {
				// Compute StationDelay
				StationDelay[s] = StationArrivals[s] - ScheduledArrivals[s];
				// Compute ConsecutiveDelay
				double Cum_Disturbance = 0; // Cumulated disturbances to dwell times of the previous stations
				for (int k = 0; k < s; k++) {
					Cum_Disturbance = Cum_Disturbance + StationDisturbance[k];
				}
				StationConsecDelay[s] = StationDelay[s] - EntranceDelay - Cum_Disturbance;
				// The threshold is 30 sec, that is why if the train has a delay or a consecutive delay less or equal than 30 s is not considered delayed
				if (StationDelay[s] <= 0)
					StationDelay[s] = 0;
				if (StationConsecDelay[s] <= 0)
					StationConsecDelay[s] = 0;
			}
		}
	}

	// Function to calculate train delays taking into account both the positive and negative (arrival ahead the schedule) delays
	void Compute_Pos_And_Neg_Arrival_Delays_At_Stations() {
		for (int s = 0; s < numStations; s++) {
			// if the train is arrived at station s
			if (StationArrivals[s] != -1) {
				// Compute StationDelay
				StationDelay[s] = StationArrivals[s] - ScheduledArrivals[s];
				// Compute ConsecutiveDelay
				double Cum_Disturbance = 0; // Cumulated disturbances to dwell times of the previous stations
				for (int k = 0; k < s; k++) {
					Cum_Disturbance = Cum_Disturbance + StationDisturbance[k];
				}
				StationConsecDelay[s] = StationDelay[s] - EntranceDelay - Cum_Disturbance;
			}
		}
	}

	// Function to set scheduled train arrival times at stations according to timetable which has a regular headway between consecutive trains
	void Set_Scheduled_Arrivals(double* P_Arrivals) {
		for (int s = 0; s < numStations; s++) {
			ScheduledArrivals[s] = P_Arrivals[s] + departure_time;
		}
	}

	void recordCurrentServiceStopArrival(int time_seconds) {
		if (time_seconds < 0 || CurrentServiceStop == "None")
			return;

		for (int s = 0; s < numStations; s++) {
			if (Stations[s].stationName == CurrentServiceStop && StationArrivals[s] < 0) {
				StationArrivals[s] = time_seconds * timestep;
				StationArrivalNames[s] = CurrentServiceStop;
				break;
			}
		}
	}

	double stationRoutePositionMeters(int stationIndex) const {
		double stationX = Stations[stationIndex].X;
		if (stationX == 0) {
			const string stationName = stationNameForArrivalStats(stationIndex);
			for (int h = 0; h < ::numStations; h++) {
				if (StationArray[h].stationName == stationName) {
					stationX = StationArray[h].X;
					break;
				}
			}
		}
		if (stationX == 0)
			return -1;

		const double stationMeters = stationX * 1000.0;
		for (int h = 0; h < train_route[indexOfRoute].N_Block_Sections; h++) {
			const Section& section = train_route[indexOfRoute].sequence_of_block_sections[h];
			const double low = std::min(section.GeoXBegNode, section.GeoXEndNode) - 5.0;
			const double high = std::max(section.GeoXBegNode, section.GeoXEndNode) + 5.0;
			if (stationMeters >= low && stationMeters <= high) {
				const double portion = train_route[indexOfRoute].reversed_direction
					? section.GeoXBegNode - stationMeters
					: stationMeters - section.GeoXBegNode;
				return section.start_node.X * 1000.0 + portion;
			}
		}

		return stationMeters;
	}

	void recordStationPassagesAtTime(int time_seconds) {
		if (time_seconds <= 0 || time_seconds >= trajectorySize())
			return;

		const double previous = instant_spatial_position[time_seconds - 1];
		const double current = instant_spatial_position[time_seconds];
		if (previous == -9999 || current == -9999)
			return;

		const double toleranceMeters = 5.0;
		const double low = std::min(previous, current) - toleranceMeters;
		const double high = std::max(previous, current) + toleranceMeters;
		for (int s = 0; s < numStations; s++) {
			if (StationArrivals[s] >= 0)
				continue;

			const double stationMeters = stationRoutePositionMeters(s);
			if (stationMeters < 0)
				continue;
			if (stationMeters >= low && stationMeters <= high) {
				StationArrivals[s] = time_seconds * timestep;
				if (StationArrivalNames[s] == "None")
					StationArrivalNames[s] = Stations[s].stationName;
			}
		}
	}

	string stationNameForArrivalStats(int stationIndex) const {
		if (stationIndex < 0 || stationIndex >= numStations)
			return "None";
		if (StationArrivalNames[stationIndex] != "None")
			return StationArrivalNames[stationIndex];
		return Stations[stationIndex].stationName;
	}

	// Function to calculate Train actual Arrivals at each Station and print these time instants on a text file.
	void printActualArrivals(BlockSet blockSets) {
		for (int s = 0; s < numStations; s++) {
			StationArrivals[s] = Arrival_At_Station(Stations[s]);
		}
		ofstream Arrivalsfile;
		string ArrivalName;
		ArrivalName = ArrivalName + InputMainFolder + "/TEMP/TrainStationArrivals.txt";
		Arrivalsfile.open((char*)ArrivalName.c_str(), ios::app);
		Arrivalsfile << "Train_" << ID << "\n";
		for (int s = 0; s < numStations; s++) {
			Arrivalsfile << StationArrivals[s] << "\n";
		}
		Arrivalsfile << End_Time << "\n";
		Arrivalsfile.close();
	}

	// Function to Print out the blocking times of all the Trains
	void PrintTrainBlockingTimesForThisTrain(string FileName) {
		ofstream OutputFile;
		OutputFile.open((char*)FileName.c_str(), ios::app);

		OutputFile << this->trainDescription << "\n";

		for (int j = 0; j < this->N_BlockTimeComplete; j++) {
			OutputFile << this->BlockTime[j].BlockID << " ";
		}
		OutputFile << "\n";

		for (int j = 0; j < this->N_BlockTimeComplete; j++) {
			OutputFile << this->BlockTime[j].GeoPosStart << " ";
		}

		OutputFile << "\n";

		for (int j = 0; j < this->N_BlockTimeComplete; j++) {
			/*if (train_route[regional_train[i].indexOfRoute].reversed_direction==0)
			OutputFile<<regional_train[i].BlockTime[j].PosEnd<<" ";
			else
			OutputFile<<train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute-regional_train[i].BlockTime[j].PosStart<<" ";*/
			OutputFile << this->BlockTime[j].GeoPosEnd << " ";
		}

		OutputFile << "\n";

		for (int j = 0; j < this->N_BlockTimeComplete; j++) {
			OutputFile << this->BlockTime[j].StartOccTime << " ";
		}
		OutputFile << "\n";

		for (int j = 0; j < this->N_BlockTimeComplete; j++) {
			OutputFile << this->BlockTime[j].EndOccTime << " ";
		}
		OutputFile << "\n";
	}

	// Function to print timetable points for this train
	void PrintTimetablePointsForThisTrain(string FileName) {
		ofstream OutputFile;
		OutputFile.open((char*)FileName.c_str(), ios::app);

		OutputFile << this->trainDescription << "\n";

		if (this->TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = this->TimetablePoints.begin(); j != this->TimetablePoints.end(); j++) {
				OutputFile << j->SuccessorID << " ";
			}
		}
		OutputFile << "\n";

		if (this->TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = this->TimetablePoints.begin(); j != this->TimetablePoints.end(); j++) {
				OutputFile << j->Position << " ";
			}
		}

		OutputFile << "\n";

		if (this->TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = this->TimetablePoints.begin(); j != this->TimetablePoints.end(); j++) {
				OutputFile << j->Position << " ";
			}
		}

		OutputFile << "\n";

		if (this->TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = this->TimetablePoints.begin(); j != this->TimetablePoints.end(); j++) {
				OutputFile << j->Time << " ";
			}
		}
		OutputFile << "\n";

		if (this->TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = this->TimetablePoints.begin(); j != this->TimetablePoints.end(); j++) {
				OutputFile << j->Time2 << " ";
			}
		}
		OutputFile << "\n";
	}

	// check train arrival/departure at/from destination/origin
	void checkTrainArrDep(int trainIdx, int t);

	// check and execute dispatching decisions
	void executeDispDecisions(int t);

	// implement init message from dispatching tool
	void implementInitMsg(DispatchDecision decision);

	// implement disp message from dispatching tool
	void implementDisp(DispatchDecision decision);

	// implement dwell message from dispatching tool
	void implementDwell(DispatchDecision decision);

	// print train service path diagram to file
	void printTrainServicePathDiagram(std::string FolderName, int nextServiceRouteID);

	// print train arrival at terminal station (append to file)
	void printTrainArrDepMsg(std::string stationName, std::string msgType, int trainIdx, int t, std::string FolderName);

	// computes train X position (works with routes acrossing different regions)
	/** wagon = 0 by default to return head position*/
	double trainXPosition(int t, int wagon = 0);

	// instantaneous speed in km/h at GUI time t, mirroring trainXPosition index mapping
	double speedKmhAt(int t) const {
		if (t < 0 || t >= static_cast<int>(instant_train_speed.size()))
			return 0.0;
		return instant_train_speed[t] * 3.6; // m/s to km/h
	}

	// position in km at GUI time t, mirroring trainXPosition index mapping
	double positionKmAt(int t) const {
		if (t < 0 || t >= static_cast<int>(instant_spatial_position.size()))
			return 0.0;
		return instant_spatial_position[t] / 1000.0;
	}

	// number of trajectory samples available
	int trajectorySize() const {
		return static_cast<int>(instant_train_speed.size());
	}

	bool isTrajectorySampleValid(int index) const {
		if (earliestActiveTrajectoryIndex < 0 || index < 0 || index >= static_cast<int>(instant_spatial_position.size()))
			return false;
		return ::isValidTrajectorySample(index, earliestActiveTrajectoryIndex, End_Time,
									static_cast<int>(instant_spatial_position.size()), instant_spatial_position[index]);
	}

	void recordEarliestActiveTrajectoryIndex(int index) {
		if (earliestActiveTrajectoryIndex < 0 && CanEnter)
			earliestActiveTrajectoryIndex = index;
	}

	// signalling functions that need train info to occupy specific signalling_block_sections
	/** function to occupy entire single track*/
	void occupySingleTrack(Section* BS, int Blocks, int hTail, int hHead, int t);

	// unlock single track (unlock signalling_block_sections for a train passing a single track)
	void unlockSingleTrack(Section* BS, int Blocks, int t);

	// set vector sizes with length of simulation from user input
	void setTrainVectorSizesFromInput(int vec_size);
};

class Regional : public Train {

public:
	// Regional class Constructor
	Regional();

	// Regional Destructor
	~Regional() {
		/*PrintTrajectory();*/
		// cout << "\nTrain " << this->ID << " Destroyed";
	}

	// moved PrintTrajectory to simulation code because here destructor automatically deletes vectors before print function

	// Overloading = Operator  (Useful to create more trains belonging to the same line)
	Regional& operator=(const Regional& ob2) {
		number_of_wagons = ob2.number_of_wagons;
		massFactor = ob2.massFactor;
		mass_of_traction_unit = ob2.mass_of_traction_unit;
		mass_of_a_wagon = ob2.mass_of_a_wagon;
		total_train_mass = ob2.total_train_mass;
		max_train_decelaration = ob2.max_train_decelaration;
		max_train_speed = ob2.max_train_speed;
		g = ob2.g;
		frontal_wagon_area = ob2.frontal_wagon_area;
		massPerWagonAxle = ob2.massPerWagonAxle;
		Jerk = ob2.Jerk;
		train_length = ob2.train_length;
		return *this;
	}
};

extern Regional regional_train[Max_N_Reg];
extern Regional P, PD; /*This train is a Proof Train to measure the simulated Headway for each different Signalling Layout
				P is the Proof Train on the Even Track, while PD is the proof Train on the Odd track*/

// Fuction to load data of a train Type. Train Type involve the same category (Intercity, Regional), the same Route, and the same stops on that Route
void loadTrainType(char* Train_File, int& numRegions);

// Function to Determine for each Route the Block Sections that are occupied by trains (This Function Fill in the list BlocksOccupied)
void Occupy_Block_Sections_Of_Route(int i);

// Function to report the position of the trains in ETCS Level 3
void ReportAllTrainPositionsToRBC(int i, double ETCS3SafetyMargin);

// Function to Identify the point where following train under Virtual Coupling shall decpouple from the leading train
void Predict_And_Check_Decoupling_MA_For_All_Train_in_Following_Mode(int t);

// Function to determine the scheduled sequence of train departure from a certain Node and print it on a text file
void Print_Scheduled_Dep_Order_In_Node(double Node_X);

// Function to Rename trains according to the ROMA denomination: the trains in the ROMA file must be ordered according to the timetable departure
// the train renamed must depart or pass from the Node with abscissa Node_X
void nameTrainDescriptionAsRomaTool(char* FileNamesROMA, double Node_X);

// Debugging Function
void Function_To_Debug_Occupation_Train(Train T, Section* BS, int Blocks);

// This Function is to Print out the LastEnteredTrain of the list OL
void Print_Out_OL_LastEntry(OrderList OL, int t);

// Function to Print out the blocking times of all the Trains
void PrintTrainBlockingTimes(string MainFolder);

// Function to Print out the blocking times of all the Trains
void PrintTimetablePoints(string MainFolder);

// Print EventTimes in the Format for TU Delft Timetabling tool
void Print_Timetabling_Point_TUDelft_format(string MainFolder);

// Function to compute Blocking Times of trains in mixed signalling areas
void ComputeBlockingTimesInMixedSignallingForAllTrains(double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, string OutputFolder, double AbsRTSupplement, double PercRTSupplement);

// Function to compute blocking times of trains in ETCS L2 or conventional signalling
void ComputeBlockingTimesForAllTrains(string SignallingType, double SetupTime, double ReleaseTime, double SightReacTime, string OutputFolder, double AbsRTSupplement, double PercRTSupplement);

// Function to compute the Blocking Times in ETCS level 3 for all trains
void ComputeBlockingTimesETCS3ForAllTrains(double SetupTime, double ReleaseTime, double SightReacTime, string OutputFolder, double AbsRTSupplement, double PercentRTSupplement);

// Debug Activate Signalling Function
void Debug_Activate_Signalling();

void LoadAllTrainFiles(string MainFolder);

void DetectConflictsForAllTrains(Train* Trains, int numTrains);

// function to protect all station areas
void protectStationAreas(int i);

#endif
