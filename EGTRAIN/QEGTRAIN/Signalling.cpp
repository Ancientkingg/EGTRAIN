#include "Signalling.h"
extern Owl owl;


TrainEvent::TrainEvent() {
	Time = Time2 = Position = Acceleration = -10000;
	trainDescription = "None";
	CurrentStoppedStation = SuccessorID = CurrentSectionID = NextSectionID = "None";
	TrainSpeed = -1;
	InfraElemStatus = "None";
	StoppedForServiceStop = ServiceStopBehindATrain = false;
}

// Function to order Train Event List according to a chronological order
bool orderTrainEvents(TrainEvent A, TrainEvent blockSets) {
	if (A.Time <= blockSets.Time)
		return true;
	else
		return false;
}

// Function to order Trains in a list where time events are also equal
void orderListOfTrainEvents(list<TrainEvent>& OutputList) {

	list<TrainEvent> List;
	List = OutputList;
	OutputList.clear(); // deleting all the elements from Outputlist

	while (List.size() > 0) {
		double Min = 999999999;
		TrainEvent CurrentMin;
		for (list<TrainEvent>::iterator it = List.begin(); it != List.end(); it++) {
			if (it->Time < Min) {
				Min = it->Time;
			}
		}
		// Set the object TrainEvent having the minimum time
		for (list<TrainEvent>::iterator it = List.begin(); it != List.end(); it++) {
			if (it->Time == Min) {
				CurrentMin = *it;
				List.erase(it); // remove the minimum from the list and put it in the new outputList
				OutputList.push_back(CurrentMin);
				break;
			}
		}
	}
}


InfraEvent::InfraEvent() {
	ElementInfo.GeoXCoord = ElementInfo.GeoYCoord = ElementInfo.XCoordinate = ElementInfo.YCoordinate = -1;
	ElementInfo.ID = ElementInfo.SectionID = ElementInfo.stationName = "None";
	ElementInfo.IsSignal = ElementInfo.isStation = ElementInfo.IsSwitch = ElementInfo.IsTrackDetSecBorder = false;
	// The list of Recorded event is empty by default
}

list<InfraEvent> InfraElementsList;

// Function to Update Status and Events of Infrastructure Elements collected in the InfraElementsList
// the function below updates the status of an infrastructure element along a train route given the list of Infrastructure elements on the route of the train, the current time t in the simulation, the train positions at times t and t-1 along the route
// the speed of the train at time t-1 and the description (i.e. the name) of the train service

void UpdateInfrastructureElementStatus(const list<InfraElement>& RouteInfrastructureElements, int t, double Xt_1, double Xt, double Vt_1, const string& TrainName) {
	if (RouteInfrastructureElements.size() > 0) {
		string InfraElementID;
		TrainEvent P;
		double PositionLastRouteInfraElement = -1;

		for (list<InfraElement>::const_iterator i = RouteInfrastructureElements.begin(); i != RouteInfrastructureElements.end(); i++) {
			if (i->XCoordinate > Xt) { // if the Xcoordinate of the Route Infra Element is larger than Xt then you can break the for loop over the RouteInfraElement
				break;
			}

			else if ((i->XCoordinate >= Xt_1) && (i->XCoordinate < Xt)) { // if the position of the train crosses element i
				if (i->XCoordinate > PositionLastRouteInfraElement) {	  // With this condition we avoid recording events for two infraelement having the same exact coordinate as it happens when we have ending edges of diverging switches coinciding with starting edges of another diverging switch. With this condition we avoid recording events for the same infraelement at the same time

					if ((i->IsSwitch == 1) && (i->withSwitchDiv == 1)) {
						if (i->SwitchName == "None") { // throw a warning if it does not have Switch Name
							cout << "Warning: Diverging Switch: " << i->ID << "at position " << i->XCoordinate << " on Route of Train " << TrainName << " has no SwitchName. Please investigate on it\n";
						} else { // in the case it has a SwitchName, then the Switchname will be the ID of the InfraElement
							InfraElementID = i->SwitchName;
						}

					}

					else {					   // for all the other cases just consider the ID of the element
						if (i->ID == "None") { // throw a warning in case the infra element does not have a name
							cout << "Warning: Infrastructure Element at position " << i->XCoordinate << " on Route of Train " << TrainName << " has no ID. Please investigate on it\n";
						} else { // in the case it has a regular name then InfraElement ID is the ID of the element itself
							InfraElementID = i->ID;
						}
					}
					// Identify the successor infrastructure element on this route
					list<InfraElement>::const_iterator LastRouteInfraElementOnRoute = RouteInfrastructureElements.end();
					LastRouteInfraElementOnRoute--; // this one is the last infra element on the route
					// ID of the successor InfraElement after i on the RouteInfrastructureElements List
					string SuccessorInfraElement = "None";
					// SuccessorInfraElement shall not be a VirtualTDSB since VirtualTDSB are not considered in the blocking times of trains
					if (i == LastRouteInfraElementOnRoute) { // if i is the last element of Route Infra then put SuccessorInfraElement as None
						SuccessorInfraElement = "None";
					} else { // if i is not the last infra element of the route then put the ID of the successor as SuccessorInfraElement
						list<InfraElement>::const_iterator successor = i;
						successor++; // taking the infrastructure element of the route coming after element i
						SuccessorInfraElement = successor->ID;
						if ((successor->ID == "VirtualTDSB") && (successor != LastRouteInfraElementOnRoute)) { // in case the successor of i is a VirtualTDSB then put the element after successor as SuccessorInfraElement
							list<InfraElement>::const_iterator secondsuccessor = successor;
							secondsuccessor++;
							SuccessorInfraElement = secondsuccessor->ID;
						} else if ((successor->ID == "VirtualTDSB") && (successor == LastRouteInfraElementOnRoute)) { // in case the successor is a VirtualTDSB and it is also the last element of the route then put SuccessorInfraElement as "None"
							SuccessorInfraElement = "None";
						}
					}

					// Found the infrastructure element set up the event to update its status
					P.trainDescription = TrainName;
					P.Time = t - 1;
					P.TrainSpeed = Vt_1;
					P.SuccessorID = SuccessorInfraElement;
					P.Position = i->XCoordinate; // this is the position that the infrastructure element has along the route of the train TrainName
					if (i->IsSwitch == 1) {		 // In case the element i is a switch

						if (i->withSwitchDiv == 1) { // if the switch connects two different tracklines (so it is diverging)

							if (i->XConnectedPoint != -1) { // and the position of the connected point has been correctly assigned

								if (i->XCoordinate < i->XConnectedPoint) {
									// Then activate the switch only at the starting point of the beam, since at the toe (i.e. the starting point) of the beam there is the point machine while the outer edge remain fixed in straight position

									P.InfraElemStatus = "Diverging";
								}

								else { // if the infra element is the outer edge of the beam ( i.e. i->XCoordinate> PositionConnectedPoint, so if infra element i is the second point coming in the moving direction of the train) then
									   // if the end of the beam does not concide with any starting edge of diverging beam it is in the straight position since only the starting edge of the beam is moved
									P.InfraElemStatus = "Straight";

									if (i->isEndOfDivSwitchStartOfADivSwitch == 1) {
										// if the ending edge of the beam coincides with the starting edge of another diverging switch, then of course the position of the infra element is diverging
										P.InfraElemStatus = "Diverging";
									}
								}
							} else {
								cout << "Warning: Position not found for connected Point of Switch: " << i->ID << " on Route of Train" << TrainName << "\n";
							}
						} else {
							P.InfraElemStatus = "Straight";
						}
					}

					else {
						// If the InfraElement is not a Switch then its status must always be set to "None" that is the default value of the variable InfraElemStatus
					}

					for (list<InfraEvent>::iterator e = InfraElementsList.begin(); e != InfraElementsList.end(); e++) {
						if (InfraElementID == e->ElementInfo.ID) {
							e->RecordedEvent.push_back(P); // Update the status of element
							break;						   // break the for loop on the InfraElementsList after the right infrastructure element to update has been correctly
						}
					}
				}

				PositionLastRouteInfraElement = i->XCoordinate; // Recording the Position of the LastRouteInfraElement which falls between Xt_1 and Xt
			}
		}
	}
}

// Function to order all the recorded events for the infrastructure elements according to a chronological sequence
void SortRecordedEventsForAllInfrastructureElements(list<InfraEvent>& ListOfAllInfraEvents) {
	if (ListOfAllInfraEvents.size() > 0) {
		for (list<InfraEvent>::iterator it = ListOfAllInfraEvents.begin(); it != ListOfAllInfraEvents.end(); it++) {
			orderListOfTrainEvents(it->RecordedEvent);
		}
	}
}


TDS::TDS() {
	blocksection_ID = "";
	connection_node.X = -1;
	node_on_switch.X = -1;
	length = -1;
	occupied = false;
	this->TracklineID = -1;
}


int Blocks = 0; /*Number of Block Sections within the whole network*/


Section::Section() {

	nodelist_of_nodes_in_signalling_section = nullptr;
	start_node.ID = end_node.ID = 0; // Class Constructor
	length = 0;
	exit_speed = 0;
	total_nodes = total_arcs = 0;
	code = 270;
	strcpy_s(state, "green");
	withSwitchDiv = Occupied = false;
	Occup_By_Train = false;
	trackLineId = -1;
	N_ConnectedBS = 0;
	XStartSwitch = XEndSwitch = 0;
	GeoXEndNode = GeoXBegNode = 0;
	SignallingLevel = -99999999;
	N_ETCS3BrakingPoints = 0;
	FirstConnectedTrackLineID = SecondConnectedTrackLineID = -1;
	for (int i = 0; i < 40; i++) {
		ETCS3BrakingPoints[i] = -1;
		ETCS3BrakingPointsTrainID[i] = "None";
	} // All the ETCS3 Braking Points are initialized to -1
}

// Function to cut the block section
//  the variable CutPart can be "CutEnd" to eliminate the part from CuttingAbscissa to the End Node or "CutBegin" to eliminate the part from the Start Node to the CuttingAbscissa
void Section::CutBlockSection(string CutPart, double CuttingAbscissa) {
	list<Arc> listarcs;
	list<Node> listnode;
	Section CutSection;
	bool IsCutValid = false;

	if ((CuttingAbscissa > this->start_node.X) && (CuttingAbscissa < this->end_node.X)) {
		IsCutValid = true;
	} else {
		cout << "ERROR: The Block Section " << this->ID << " cannot be cut in the inserted abscissa " << CuttingAbscissa << "\n\n";
	}
	if (IsCutValid == 1) {
		if (CutPart == "CutEnd") {
			// Setting the beginning and end nodes and all the main attributes

			CutSection.ID = this->ID;
			CutSection.trackLineId = this->trackLineId;
			CutSection.FirstConnectedTrackLineID = this->FirstConnectedTrackLineID;
			CutSection.SecondConnectedTrackLineID = this->SecondConnectedTrackLineID;
			CutSection.exit_speed = this->exit_speed;
			CutSection.code = this->code;
			CutSection.SignallingLevel = this->SignallingLevel;
			strcpy(CutSection.state, this->state);
			CutSection.Occup_By_Train = this->Occup_By_Train;
			CutSection.Occupied = this->Occupied;
			CutSection.N_ConnectedBS = this->N_ConnectedBS;
			CutSection.GeoXBegNode = this->GeoXBegNode;
			CutSection.GeoXEndNode = CuttingAbscissa * 1000;

			for (int i = 0; i < N_ConnectedBS; i++) {
				CutSection.IDConnectedBS[i] = this->IDConnectedBS[i];
			}

			// Setting the switches
			if (this->withSwitchDiv == 1) {
				if (XStartSwitch <= CuttingAbscissa) {
					CutSection.withSwitchDiv = true;
					CutSection.XStartSwitch = this->XStartSwitch;
					if (this->XEndSwitch <= CuttingAbscissa) { // The XEndSwitch can be put only if there is a XStartSwitch
						CutSection.XEndSwitch = this->XEndSwitch;
						if (CuttingAbscissa == this->XEndSwitch) {
							cout << "Cutting Block Section: " << this->ID << " at the ending edge of its diverging switch\n\n";
						}
					} else {
						cout << "Warning: Block Section: " << this->ID << " has a diverging switch and it is being cut at a progressive which cuts out the XEndSwitch edge of the switch, please consider repositioning the final edge of this switch or the starting edge of the switch of the next block section in the route\n";
						// CutSection.XEndSwitch = CuttingAbscissa;
					}
				} else {
					cout << "Warning: Block Section: " << this->ID << " has a diverging switch and it is being cut at a progressive which cuts out the XEndSwitch edge of the switch, please consider repositioning the final edge of this switch or the starting edge of the switch of the next block section in the route\n";
					// CutSection.withSwitchDiv = false;
				}
			}
			// Setting The arcs
			for (int i = 0; i < this->total_arcs; i++) {
				if (this->arcs_in_signalling_block_section[i].endNode.X <= CuttingAbscissa) {
					listarcs.push_back(this->arcs_in_signalling_block_section[i]);
				} else if ((this->arcs_in_signalling_block_section[i].endNode.X > CuttingAbscissa) && (this->arcs_in_signalling_block_section[i].startNode.X < CuttingAbscissa)) {
					Arc modArc = arcs_in_signalling_block_section[i];
					Node CutEndingNode;
					CutEndingNode.X = CuttingAbscissa;
					CutEndingNode.Y = arcs_in_signalling_block_section[i].endNode.Y;
					CutEndingNode.tdsbId = "VirtualTDSB";
					modArc.endNode = CutEndingNode;
					modArc.length = modArc.endNode.X - modArc.startNode.X;
					listarcs.push_back(modArc);
				}
			}

			// Transferring all the arcs to CutSection
			if (listarcs.empty() != 1) {
				for (list<Arc>::iterator it = listarcs.begin(); it != listarcs.end(); it++) {
					CutSection.arcs_in_signalling_block_section[CutSection.total_arcs] = *it;
					CutSection.total_arcs++;
				}
			}

			// Setting beginning and end nodes and length
			CutSection.start_node = this->start_node;
			// Setting TDSB features for the end Node of the last Arc
			CutSection.arcs_in_signalling_block_section[CutSection.total_arcs - 1].endNode.tdsbGeoCoordX = CutSection.arcs_in_signalling_block_section[CutSection.total_arcs - 1].endNode.X * 1000;
			CutSection.arcs_in_signalling_block_section[CutSection.total_arcs - 1].endNode.tdsbGeoCoordY = CutSection.arcs_in_signalling_block_section[CutSection.total_arcs - 1].endNode.Y * 1000;
			// Setting Characteristics for the end Node of the block section
			CutSection.end_node = CutSection.arcs_in_signalling_block_section[CutSection.total_arcs - 1].endNode;
			// Setting length of the block Section
			CutSection.length = CutSection.end_node.X - CutSection.start_node.X;

			// Setting the nodes nodelist_of_nodes_in_signalling_section
			CutSection.total_nodes = CutSection.total_arcs + 1;
			CutSection.nodelist_of_nodes_in_signalling_section = new Node[CutSection.total_nodes];
			// Setting the first Node
			CutSection.nodelist_of_nodes_in_signalling_section[0] = CutSection.start_node;
			for (int i = 0; i < CutSection.total_arcs; i++) { // Every block has at least two nodes
				CutSection.nodelist_of_nodes_in_signalling_section[i + 1] = CutSection.arcs_in_signalling_block_section[i].endNode;
			}

		}

		else if (CutPart == "CutBegin") {
			// Setting all the main attributes
			CutSection.ID = this->ID;
			CutSection.trackLineId = this->trackLineId;
			CutSection.FirstConnectedTrackLineID = this->FirstConnectedTrackLineID;
			CutSection.SecondConnectedTrackLineID = this->SecondConnectedTrackLineID;
			CutSection.exit_speed = this->exit_speed;
			CutSection.code = this->code;
			CutSection.SignallingLevel = this->SignallingLevel;
			strcpy(CutSection.state, this->state);
			CutSection.Occup_By_Train = this->Occup_By_Train;
			CutSection.Occupied = this->Occupied;
			CutSection.N_ConnectedBS = this->N_ConnectedBS;
			CutSection.GeoXBegNode = CuttingAbscissa * 1000;
			CutSection.GeoXEndNode = this->GeoXEndNode;

			for (int i = 0; i < N_ConnectedBS; i++) {
				CutSection.IDConnectedBS[i] = this->IDConnectedBS[i];
			}

			// Setting the switches
			if (this->withSwitchDiv == 1) {
				if (XStartSwitch >= CuttingAbscissa) {
					CutSection.withSwitchDiv = true;
					CutSection.XStartSwitch = this->XStartSwitch;
					if (CuttingAbscissa == XStartSwitch) {
						cout << "Cutting Block Section: " << this->ID << "at the starting edge of its diverging switch\n\n";
					}
					if (this->XEndSwitch >= CuttingAbscissa) { // The XEndSwitch can be put only if there is a XStartSwitch
						CutSection.XEndSwitch = this->XEndSwitch;
					} else {
						cout << "ERROR: Block Section: " << ID << " has Ending Edge of diverging switch with a progressive inferior to its Starting Egde. Problems in cutting the block section. PLease correct Switch on the infrastructure\n\n";
					}
				} else {
					cout << "Warning: Block Section : " << this->ID << " has a diverging switch and it is being cut at a progressive which cuts out the XStartSwitch edge of the switch, please consider repositioning the starting edge of this switch or the final edge of the switch of the previous block section in the route\n";
					// CutSection.withSwitchDiv = false;
				}
			}
			// Setting The arcs
			for (int i = 0; i < this->total_arcs; i++) {
				if (this->arcs_in_signalling_block_section[i].startNode.X >= CuttingAbscissa) {
					// Differently from the part in CutEnd here a further condition needs to be added because in EGTRAIN tdsbId are assigned just to end nodes of arcs. So in the case the cutting abscissa coincides with the starting Node of an Arc, such a starting Node needs to have the tdsbId of the end Node of the previous Arc if there is any previous.
					// In case the Starting Node start_node coincides with the CuttingAbscissa, does not have a TDSB ID while the end Node of the previous Arc (if the Arc is not the first (i>0)) has a tdsbId not empty
					if ((arcs_in_signalling_block_section[i].startNode.X == CuttingAbscissa) && (i > 0) && (arcs_in_signalling_block_section[i].startNode.tdsbId.empty() == 1) && (arcs_in_signalling_block_section[i - 1].endNode.tdsbId.empty() != 1)) {
						arcs_in_signalling_block_section[i].startNode = arcs_in_signalling_block_section[i - 1].endNode; // Then the Starting Node of arcs_in_signalling_block_section[i] must take tdsbId and other characteristics from the end Node of the previous Arc
					}
					listarcs.push_back(this->arcs_in_signalling_block_section[i]);
				} else if ((this->arcs_in_signalling_block_section[i].endNode.X > CuttingAbscissa) && (this->arcs_in_signalling_block_section[i].startNode.X < CuttingAbscissa)) {
					Arc modArc = arcs_in_signalling_block_section[i];
					Node CutStartingNode;
					CutStartingNode.X = CuttingAbscissa;
					CutStartingNode.Y = arcs_in_signalling_block_section[i].startNode.Y;
					CutStartingNode.tdsbId = "VirtualTDSB";
					modArc.startNode = CutStartingNode;
					modArc.length = modArc.endNode.X - modArc.startNode.X;
					listarcs.push_back(modArc);
				}
			}

			// Transferring all the arcs to CutSection
			if (listarcs.empty() != 1) {
				for (list<Arc>::iterator it = listarcs.begin(); it != listarcs.end(); it++) {
					CutSection.arcs_in_signalling_block_section[CutSection.total_arcs] = *it;
					CutSection.total_arcs++;
				}
			}

			// Setting beginning and end nodes and length
			CutSection.end_node = this->end_node;

			// Setting TDSB features for the starting Node of the first Arc
			CutSection.arcs_in_signalling_block_section[0].startNode.tdsbGeoCoordX = CutSection.arcs_in_signalling_block_section[0].startNode.X * 1000;
			CutSection.arcs_in_signalling_block_section[0].startNode.tdsbGeoCoordY = CutSection.arcs_in_signalling_block_section[0].startNode.Y * 1000;
			// Setting Characteristics for the first Node of the block section
			CutSection.start_node = CutSection.arcs_in_signalling_block_section[0].startNode;
			// Setting length of the block Section
			CutSection.length = CutSection.end_node.X - CutSection.start_node.X;

			// Setting the nodes nodelist_of_nodes_in_signalling_section
			CutSection.total_nodes = CutSection.total_arcs + 1;
			CutSection.nodelist_of_nodes_in_signalling_section = new Node[CutSection.total_nodes];
			// Setting the first Node
			CutSection.nodelist_of_nodes_in_signalling_section[0] = CutSection.start_node;
			for (int i = 0; i < CutSection.total_arcs; i++) { // Every block has at least two nodes
				CutSection.nodelist_of_nodes_in_signalling_section[i + 1] = CutSection.arcs_in_signalling_block_section[i].endNode;
			}
		}
	}
	*this = CutSection;
}

// Function to reverse the block section
void Section::reverseBlockSection(Section blockSets, double RouteLength) {

	code = blockSets.code;
	exit_speed = blockSets.exit_speed;
	strcpy(state, blockSets.state);
	withSwitchDiv = blockSets.withSwitchDiv;
	if (withSwitchDiv == 1) {
		XStartSwitch = RouteLength - blockSets.XEndSwitch; // The beginning of the switch becomes the end for A
		XEndSwitch = RouteLength - blockSets.XStartSwitch; // The end of the Switch blockSets becomes the start of Switch A
	} else {
		XStartSwitch = 0; // The beginning of the switch becomes the end for A
		XEndSwitch = 0;	  // The end of the Switch blockSets becomes the start of Switch A
	}
	ID = blockSets.ID;
	length = blockSets.length;
	total_arcs = blockSets.total_arcs;
	total_nodes = blockSets.total_nodes;
	N_ConnectedBS = blockSets.N_ConnectedBS;
	// The GeoCoordinate will be reversed
	GeoXBegNode = blockSets.GeoXEndNode;
	GeoXEndNode = blockSets.GeoXBegNode;
	// The trackLineId remains the same if the Block Section does not have a diverging Switch
	trackLineId = blockSets.trackLineId;
	// If instead it has a diverging switch the First and the second Connected Tracklines swap
	FirstConnectedTrackLineID = blockSets.SecondConnectedTrackLineID;
	SecondConnectedTrackLineID = blockSets.FirstConnectedTrackLineID;

	// The signalling system will remain the same
	SignallingLevel = blockSets.SignallingLevel;

	start_node = blockSets.end_node;				   // Inverting Ending Node with starting Node
	start_node.X = RouteLength - start_node.X; // Changing the position of the Node with respect to the length of the route
	end_node = blockSets.start_node;
	end_node.X = RouteLength - end_node.X; // Changing the position of the Node with respect to the length of the route
										   // Inverting the nodes of the Arc and changing the sign to the gradient
	int k = 0;
	for (int j = blockSets.total_arcs - 1; j >= 0; j--) {
		arcs_in_signalling_block_section[k].gradient = -blockSets.arcs_in_signalling_block_section[j].gradient;
		arcs_in_signalling_block_section[k].startNode = blockSets.arcs_in_signalling_block_section[j].endNode;
		arcs_in_signalling_block_section[k].startNode.X = RouteLength - arcs_in_signalling_block_section[k].startNode.X;
		arcs_in_signalling_block_section[k].endNode = blockSets.arcs_in_signalling_block_section[j].startNode;
		arcs_in_signalling_block_section[k].endNode.X = RouteLength - arcs_in_signalling_block_section[k].endNode.X;
		arcs_in_signalling_block_section[k].ID = blockSets.arcs_in_signalling_block_section[j].ID;
		arcs_in_signalling_block_section[k].length = blockSets.arcs_in_signalling_block_section[j].length;
		arcs_in_signalling_block_section[k].curvature = blockSets.arcs_in_signalling_block_section[j].curvature;
		arcs_in_signalling_block_section[k].speedLimit = blockSets.arcs_in_signalling_block_section[j].speedLimit;
		arcs_in_signalling_block_section[k].fs = blockSets.arcs_in_signalling_block_section[j].fs;
		arcs_in_signalling_block_section[k].brakingDistance = blockSets.arcs_in_signalling_block_section[j].brakingDistance;
		arcs_in_signalling_block_section[k].speedInBraking = blockSets.arcs_in_signalling_block_section[j].speedInBraking;
		arcs_in_signalling_block_section[k].signalSpeedLimit = blockSets.arcs_in_signalling_block_section[j].signalSpeedLimit;
		k++;
	}
	// Transferring the connected signalling_block_sections
	for (int i = 0; i < N_ConnectedBS; i++) {
		IDConnectedBS[i] = blockSets.IDConnectedBS[i];
	}

	// reversing the order of the TDS in the block section
	int N_TDS = blockSets.TDS_in_block.size();
	if (N_TDS > 0) {
		list<TDS*>::iterator td = blockSets.TDS_in_block.end();
		td--; // pointer to the last element of the list of TDS in the block section
		for (int p = N_TDS - 1; p >= 0; p--) {
			this->TDS_in_block.push_back(*td);
			if (p > 0) {
				td--;
			}
		}
	}
}

// Function to reverse the block section
void Section::changeRelativeCoordinatesOfBlockSectionWithoutReversing(Section blockSets, double RouteLength) {

	code = blockSets.code;
	exit_speed = blockSets.exit_speed;
	strcpy(state, blockSets.state);
	withSwitchDiv = blockSets.withSwitchDiv;
	if (withSwitchDiv == 1) {
		XStartSwitch = RouteLength - blockSets.XStartSwitch; // The beginning of the switch becomes the end for A
		XEndSwitch = RouteLength - blockSets.XEndSwitch;	 // The end of the Switch blockSets becomes the start of Switch A
	} else {
		XStartSwitch = 0; // The beginning of the switch becomes the end for A
		XEndSwitch = 0;	  // The end of the Switch blockSets becomes the start of Switch A
	}
	ID = blockSets.ID;
	length = blockSets.length;
	total_arcs = blockSets.total_arcs;
	total_nodes = blockSets.total_nodes;
	N_ConnectedBS = blockSets.N_ConnectedBS;
	// The GeoCoordinate will be reversed
	GeoXBegNode = blockSets.GeoXEndNode;
	GeoXEndNode = blockSets.GeoXBegNode;
	// The trackLineId remains the same if the Block Section does not have a diverging Switch
	trackLineId = blockSets.trackLineId;
	// If instead it has a diverging switch the First and the second Connected Tracklines swap
	FirstConnectedTrackLineID = blockSets.FirstConnectedTrackLineID;
	SecondConnectedTrackLineID = blockSets.SecondConnectedTrackLineID;

	// The signalling system will remain the same
	SignallingLevel = blockSets.SignallingLevel;

	start_node = blockSets.start_node;				   // StartingNode remains starting Node
	start_node.X = RouteLength - start_node.X; // Changing the position of the Node with respect to the length of the route
	end_node = blockSets.end_node;
	end_node.X = RouteLength - end_node.X; // Changing the position of the Node with respect to the length of the route
										   // Changing the sign to the gradient and the coordinates of Arc nodes
	int k = 0;
	for (int j = blockSets.total_arcs - 1; j >= 0; j--) {
		arcs_in_signalling_block_section[k].gradient = -blockSets.arcs_in_signalling_block_section[j].gradient;
		arcs_in_signalling_block_section[k].startNode = blockSets.arcs_in_signalling_block_section[j].startNode;
		arcs_in_signalling_block_section[k].startNode.X = RouteLength - arcs_in_signalling_block_section[k].startNode.X;
		arcs_in_signalling_block_section[k].endNode = blockSets.arcs_in_signalling_block_section[j].endNode;
		arcs_in_signalling_block_section[k].endNode.X = RouteLength - arcs_in_signalling_block_section[k].endNode.X;
		arcs_in_signalling_block_section[k].ID = blockSets.arcs_in_signalling_block_section[j].ID;
		arcs_in_signalling_block_section[k].length = blockSets.arcs_in_signalling_block_section[j].length;
		arcs_in_signalling_block_section[k].curvature = blockSets.arcs_in_signalling_block_section[j].curvature;
		arcs_in_signalling_block_section[k].speedLimit = blockSets.arcs_in_signalling_block_section[j].speedLimit;
		arcs_in_signalling_block_section[k].fs = blockSets.arcs_in_signalling_block_section[j].fs;
		arcs_in_signalling_block_section[k].brakingDistance = blockSets.arcs_in_signalling_block_section[j].brakingDistance;
		arcs_in_signalling_block_section[k].speedInBraking = blockSets.arcs_in_signalling_block_section[j].speedInBraking;
		arcs_in_signalling_block_section[k].signalSpeedLimit = blockSets.arcs_in_signalling_block_section[j].signalSpeedLimit;
		k++;
	}

	// Transferring the connected signalling_block_sections
	for (int i = 0; i < N_ConnectedBS; i++) {
		IDConnectedBS[i] = blockSets.IDConnectedBS[i];
	}
}

// Function to set relative coordinates to the nodes of a block section. The relative reference it is the beginning Node of the block
void Section::setRelativeCoordinatesToStartNode() {

	double StartX = start_node.X; // Beginning distance;
	this->start_node.X = start_node.X - StartX;
	this->end_node.X = end_node.X - StartX;
	if (withSwitchDiv == 1) {
		XStartSwitch = XStartSwitch - StartX; // The beginning of the switch becomes the end for A
		XEndSwitch = XEndSwitch - StartX;	  // The end of the Switch blockSets becomes the start of Switch A
	}

	// Resetting the coordinates of all the nodes of the arcs of the block
	for (int i = 0; i < this->total_arcs; i++) {
		this->arcs_in_signalling_block_section[i].startNode.X = arcs_in_signalling_block_section[i].startNode.X - StartX;
		this->arcs_in_signalling_block_section[i].endNode.X = arcs_in_signalling_block_section[i].endNode.X - StartX;
	}

	// Resetting the coordinates of the  Nodes nodelist_of_nodes_in_signalling_section
	/* (int j=0;j<this->total_nodes;j++){
	this->nodelist_of_nodes_in_signalling_section[j].X=nodelist_of_nodes_in_signalling_section[j].X-StartX;
	}*/
}

// Function to reset a block section to a different point of reference, this function is used after the function setRelativeCoordinatesToStartNode() when composing two routes together
void Section::resetBlockCoordinatesToDifferentOrigin(double OriginX) {

	this->start_node.X = start_node.X + OriginX;
	this->end_node.X = end_node.X + OriginX;
	if (withSwitchDiv == 1) {
		XStartSwitch = XStartSwitch + OriginX; // The beginning of the switch becomes the end for A
		XEndSwitch = XEndSwitch + OriginX;	   // The end of the Switch blockSets becomes the start of Switch A
	}

	// Resetting the coordinates of all the nodes of the arcs of the block
	for (int i = 0; i < this->total_arcs; i++) {
		this->arcs_in_signalling_block_section[i].startNode.X = arcs_in_signalling_block_section[i].startNode.X + OriginX;
		this->arcs_in_signalling_block_section[i].endNode.X = arcs_in_signalling_block_section[i].endNode.X + OriginX;
	}

	// Resetting the coordinates of the  Nodes nodelist_of_nodes_in_signalling_section
	/*for (int j=0;j<this->total_nodes;j++){
	this->nodelist_of_nodes_in_signalling_section[j].X=nodelist_of_nodes_in_signalling_section[j].X+OriginX;
	}*/
}

std::list<TDS> list_of_TDS;
/**
 * \brief the small parts that can be occupied
 * max of 20 arcs
 */
Section signalling_block_sections[6000]; // Signalling Block Sections

// Function to dinamically change the block section configuration during the optimization cycle
void SetEquiBlock(double L, Section* BS, int& Blocks, BlockSet blockSets) {
	double NumBlockinB;
	NumBlockinB = blockSets.member[blockSets.len - 1].endNode.X / L;
	if ((NumBlockinB - (int)NumBlockinB) > 0) {
		NumBlockinB = (int)NumBlockinB + 1;
	} else {
		NumBlockinB = (int)NumBlockinB;
	}

	for (int h = Blocks; h < (int)NumBlockinB + Blocks; h++) {
		BS[h].length = L;
	}
	// Update the Total Number of Block Sections
	Blocks = Blocks + (int)NumBlockinB;
}

// Function to Create Equi-Block_Section for a single TrackLine
void createEquiBlockSections(double L, Section* BS, int& Blocks, BlockSet blockSets) {
	int N_Block_Previous = Blocks;
	SetEquiBlock(L, BS, Blocks, blockSets);
	defSection(BS, N_Block_Previous, blockSets);
}

// Function to Generate all the Block Sections in the network with the same length
void generateAllBlocksWithEquiLength(double L, Section* BS, int& Blocks) {
	for (int i = 0; i < numTrackLines; i++)
		createEquiBlockSections(L, BS, Blocks, blockSets[i]);
}

void generateAllBlocksFromInputFile(char* FolderName, Section* BS, int& Blocks) {
	std::cout << "Loading Block files ...\n";
	for (int i = 0; i < numTrackLines; i++) {
		char trackIndex[20];
		sprintf_s(trackIndex, "%d", i);
		string filename;
		filename = filename + FolderName + "/B" + trackIndex + "/BlockCumPari.txt";
		std::cout << "\rLoading Block file : " << trackIndex;

		createBlockSectionsFromInputFile(BS, Blocks, (char*)filename.c_str(), blockSets[i]);
	}
	std::cout << "\n";
}

// Function to create Block Sections with lengths taken from text files
void createBlockSectionsFromInputFile(Section* BS, int& Blocks, char* blockname, BlockSet blockSets) {
	int N_Block_Previous = Blocks; // Take as input the number of Blocks before that new blocks are created
	loadBlockSections(BS, Blocks, blockname);
	defSection(BS, N_Block_Previous, blockSets);
}

// Function to Load Block Section Length from External file
void loadBlockSections(Section* BS, int& Blocks, char* blockname) {
	double** Matr;
	int BLOCKS = 1000;					/*char blockname[100];*/
	int i = 0, j = 0, NumBlocksinB = 0; // NumBlocksinB is the number of block section in Path blockSets

	Matr = new double*[BLOCKS];
	for (int h = 0; h < BLOCKS; h++) {
		Matr[h] = new double[2];
	}

	/*cout<<"Insert the path of text file with Block Section Characteristics [Position, Length(m)]: ";  cin.get(blockname,99);*/

	ifstream blockinput;
	blockinput.open(blockname, ios::binary);
	if (!blockinput) {
		cout << "Error 59 :impossible to open the BlockCumPari file :\n"
			 << blockname << "\n";
	}

	while (blockinput) {
		// put first column in [i][0] (index of BlockSection)
		// and length of blocksection in [i][1]
		blockinput >> Matr[i][j];
		j++;

		NumBlocksinB++;
		if (j > 1) {
			i++;
			j = 0;
		}
	}

	NumBlocksinB = (NumBlocksinB - 1) / 2;

	blockinput.close(); // Closing Block Section Data File
						// cin.ignore();

	// Fixing Block section lengths parameter values
	for (int i = 0; i < NumBlocksinB; i++) {
		BS[i + Blocks].length = Matr[i][1];
	}

	// Update the total number of Block Section (Blocks)
	Blocks = Blocks + NumBlocksinB;

	for (int i = 0; i < BLOCKS; i++)
		delete Matr[i]; // Deleting Temporary Matrix
	delete Matr;
}

// Function to define Block Section characteristics
void defSection(Section* BS, int N_Block_Previous, BlockSet blockSets) {

	// Initializing the first block section
	BS[N_Block_Previous].start_node = blockSets.member[0].startNode;
	BS[N_Block_Previous].end_node.X = BS[N_Block_Previous].start_node.X + BS[N_Block_Previous].length;
	BS[N_Block_Previous].end_node.ID = BS[N_Block_Previous].start_node.ID + 0.31;
	// if the end Node of the block section coincides with a Node of the TrackLine blockSets, the former has to take all the features of the latter including the connections with other nodes
	for (int i = 0; i < blockSets.len; i++) {
		if (fabs(blockSets.member[i].endNode.X - BS[N_Block_Previous].end_node.X) < 0.0001)
			BS[N_Block_Previous].end_node = blockSets.member[i].endNode;
	}
	// Imposing the first Block Section
	char BID[20], BSID[20];
	sprintf_s(BID, "%d", blockSets.ID);
	sprintf_s(BSID, "%d", N_Block_Previous - N_Block_Previous);
	BS[N_Block_Previous].trackLineId = blockSets.ID; // setting the ID of the TrackLine
	BS[N_Block_Previous].ID = BS[N_Block_Previous].ID + "@" + BSID + "-B" + BID + "@";
	// Setting the Track Detection Section Border for the first block section
	BS[N_Block_Previous].start_node.tdsbId = BS[N_Block_Previous].start_node.tdsbId + "@Beg-" + BSID + "-B" + BID + "@"; // ID of the first TDSB

	BS[N_Block_Previous].start_node.tdsbGeoCoordX = BS[N_Block_Previous].start_node.X * 1000; // Setting GeoCoord X
	BS[N_Block_Previous].start_node.tdsbGeoCoordY = BS[N_Block_Previous].start_node.Y * 1000; // Setting GeoCoord Y
																							  // Setting the TDSB at the end of the first Block Section
	BS[N_Block_Previous].end_node.tdsbId = BS[N_Block_Previous].end_node.tdsbId + "@End-" + BSID + "-B" + BID + "@"; // ID of the first TDSB
	// std::cout << BS[N_Block_Previous].start_node.tdsbId << '\n';
	// std::cout << BS[N_Block_Previous].end_node.tdsbId << '\n';
	BS[N_Block_Previous].end_node.tdsbGeoCoordX = BS[N_Block_Previous].end_node.X * 1000; // Setting GeoCoord X
	BS[N_Block_Previous].end_node.tdsbGeoCoordY = BS[N_Block_Previous].end_node.Y * 1000; // Setting GeoCoord Y

	// Setting All the Block Sections of the TrackLine blockSets
	for (int h = N_Block_Previous + 1; h < Blocks; h++) {
		BS[h].start_node = BS[h - 1].end_node;
		BS[h].end_node.X = BS[h].start_node.X + BS[h].length;
		BS[h].end_node.ID = BS[h].start_node.ID + 0.31;

		// if the end Node of the block section coincides with a Node of the TrackLine blockSets, the former has to take all the features of the latter including the connections with other nodes
		for (int i = 0; i < blockSets.len; i++) {
			if (fabs(blockSets.member[i].endNode.X - BS[h].end_node.X) < 0.0001)
				BS[h].end_node = blockSets.member[i].endNode;
		}
		sprintf_s(BSID, "%d", h - N_Block_Previous);
		BS[h].ID = BS[h].ID + "@" + BSID + "-B" + BID + "@";
		BS[h].trackLineId = blockSets.ID;
		/*for (int i=0;i<blockSets.len-1;i++){
		if (fabs(signalling_block_sections[h].end_node.X-blockSets.member[i].end_node.X)<=0.001) signalling_block_sections[h].end_node.X=blockSets.member[i].end_node.X+0.002;}*/
		// This if move nodes of the block sections in order that they do not overlap with nodes of the TrackList blockSets: you have to activate it when optimizing block section lengths
		// Setting the characteristics of the the TDSB at the end of the block
		BS[h].end_node.tdsbId = BS[h].end_node.tdsbId + "@End-" + BSID + "-B" + BID + "@"; // ID of the first TDSB
		// std::cout << BS[h].end_node.tdsbId << '\n';
		BS[h].end_node.tdsbGeoCoordX = BS[h].end_node.X * 1000; // Setting GeoCoord X
		BS[h].end_node.tdsbGeoCoordY = BS[h].end_node.Y * 1000; // Setting GeoCoord Y
	}
	// Adjusting the last Block Section in case its length exceeds the length of the track
	/*signalling_block_sections[Blocks-1].end_node.ID=blockSets.member[blockSets.len-1].end_node.ID;*/
	if (BS[Blocks - 1].end_node.X * 1000 > blockSets.member[blockSets.len - 1].endNode.X * 1000) {
		BS[Blocks - 1].end_node.X = blockSets.member[blockSets.len - 1].endNode.X;
		BS[Blocks - 1].length = BS[Blocks - 1].end_node.X - BS[Blocks - 1].start_node.X;
		BS[Blocks - 1].end_node.ID = blockSets.member[blockSets.len - 1].endNode.ID;
		// Changing of course also the coordinate of the TDSB at the end of the block
		BS[Blocks - 1].end_node.tdsbGeoCoordX = BS[Blocks - 1].end_node.X * 1000;
	}

	for (int h = N_Block_Previous; h < Blocks; h++) {
		int counter = 0;
		int k = 0;
		for (int j = 0; j < blockSets.len; j++) {
			if ((blockSets.member[j].endNode.X < BS[h].end_node.X) && (blockSets.member[j].endNode.X > BS[h].start_node.X)) {
				counter++;
			}								 // Defining for each Block Section the number of Track Nodes in which is divided
			BS[h].total_nodes = counter + 2; // Determining total number of nodes
			BS[h].total_arcs = BS[h].total_nodes - 1;
		} // Determining total number of arcs

		BS[h].nodelist_of_nodes_in_signalling_section = new Node[BS[h].total_nodes]; // Defining Matrix of Block Section Nodes
		for (int j = 0; j < blockSets.len; j++) {
			if ((blockSets.member[j].endNode.X < BS[h].end_node.X) && (blockSets.member[j].endNode.X > BS[h].start_node.X)) {
				k++;
				BS[h].nodelist_of_nodes_in_signalling_section[k] = blockSets.member[j].endNode;
			}
		}
		BS[h].nodelist_of_nodes_in_signalling_section[0] = BS[h].start_node;
		BS[h].nodelist_of_nodes_in_signalling_section[BS[h].total_nodes - 1] = BS[h].end_node;
		// Defining Matrix of Block Section arcs

		// Determining characteristics of Block Section Arcs
		BS[h].arcs_in_signalling_block_section[0].startNode = BS[h].nodelist_of_nodes_in_signalling_section[0];
		BS[h].arcs_in_signalling_block_section[0].endNode = BS[h].nodelist_of_nodes_in_signalling_section[1];
		BS[h].arcs_in_signalling_block_section[0].length = BS[h].arcs_in_signalling_block_section[0].endNode.X - BS[h].arcs_in_signalling_block_section[0].startNode.X; // Imposing the first Block Section Arc data
		for (int i = 1; i < BS[h].total_arcs; i++) {
			if (i == 19) {
				cout << "the " << h << " Arc of blocks of block " << blockSets.ID << " reached 20 which is max\n";
				cout << BS[h].arcs_in_signalling_block_section[i - 1].endNode.ID;
			}
			BS[h].arcs_in_signalling_block_section[i].startNode = BS[h].arcs_in_signalling_block_section[i - 1].endNode;
			for (int j = 0; j < BS[h].total_nodes; j++) {
				if (BS[h].nodelist_of_nodes_in_signalling_section[j] == BS[h].arcs_in_signalling_block_section[i].startNode)
					BS[h].arcs_in_signalling_block_section[i].endNode = BS[h].nodelist_of_nodes_in_signalling_section[j + 1];
			}
			BS[h].arcs_in_signalling_block_section[i].length = BS[h].arcs_in_signalling_block_section[i].endNode.X - BS[h].arcs_in_signalling_block_section[i].startNode.X;
		}
		for (int i = 0; i < BS[h].total_arcs; i++) {
			BS[h].arcs_in_signalling_block_section[i].ID = i;
			for (int k = 0; k < blockSets.len; k++) {
				if ((blockSets.member[k].startNode.X <= BS[h].arcs_in_signalling_block_section[i].startNode.X) && (blockSets.member[k].endNode.X >= BS[h].arcs_in_signalling_block_section[i].endNode.X)) {
					BS[h].arcs_in_signalling_block_section[i].curvature = blockSets.member[k].curvature;
					BS[h].arcs_in_signalling_block_section[i].gradient = blockSets.member[k].gradient;
					BS[h].arcs_in_signalling_block_section[i].speedLimit = blockSets.member[k].speedLimit;
					BS[h].arcs_in_signalling_block_section[i].fs = BS[h].arcs_in_signalling_block_section[i].endNode.X * 1000;
				}
			}
		}
		// Setting TDSB at Station platforms
		sprintf_s(BSID, "%d", h - N_Block_Previous);
		for (int k = 0; k < BS[h].total_arcs; k++) {
			if (BS[h].arcs_in_signalling_block_section[k].endNode.stationName.empty() != 1) {
				BS[h].arcs_in_signalling_block_section[k].endNode.tdsbId = BS[h].arcs_in_signalling_block_section[k].endNode.tdsbId + "@" + BS[h].arcs_in_signalling_block_section[k].endNode.stationName + "-" + BSID + "-B" + BID + "@"; // ID of the TDSB at station platform
				BS[h].arcs_in_signalling_block_section[k].endNode.tdsbGeoCoordX = BS[h].arcs_in_signalling_block_section[k].endNode.X * 1000;																							  // Geocoordinate X in m
				BS[h].arcs_in_signalling_block_section[k].endNode.tdsbGeoCoordY = BS[h].arcs_in_signalling_block_section[k].endNode.Y * 1000;																							  // GeoCoordinate Y in m
			}
		}
	}
}

// Function to Generate Block Sections connected by switches (it must be used in createBlockConn)
void generateConnectBlockOldVersion(Section BS1, Section BS2, Node N1, Node N2, Section& BS3) {
	if (N1.X < N2.X) {
		list<Node> listnode;
		list<Arc> listarc;
		// Setting initial and final nodes
		BS3.start_node = BS1.start_node;
		BS3.end_node = BS2.end_node;
		// Setting the connected TrackLines
		BS3.FirstConnectedTrackLineID = BS1.trackLineId;
		BS3.SecondConnectedTrackLineID = BS2.trackLineId;
		// Setting the first part of Block arcs
		for (int i = 0; i < BS1.total_arcs; i++) {
			if (BS1.arcs_in_signalling_block_section[i].endNode.X <= N1.X) {
				BS3.total_arcs++;
				// filling in the list of arcs belonging to BS3
				listarc.push_back(BS1.arcs_in_signalling_block_section[i]);
			}
		}
		// Setting the first part of Block nodes
		for (int i = 0; i < BS1.total_nodes; i++) {
			if (BS1.nodelist_of_nodes_in_signalling_section[i].X <= N1.X) {
				BS3.total_nodes++;
				// filling in the list of nodes belonging to BS3
				listnode.push_back(BS1.nodelist_of_nodes_in_signalling_section[i]);
			}
		}

		// Creating the connection
		Arc Connection;
		BS3.total_arcs++;
		Connection.startNode = N1;
		Connection.endNode = N2;
		list<Arc>::iterator it;
		if (listarc.empty() != 1) { // if the Node N1 is not the first Node of the Block Section BS1
			it = listarc.end();
			it--;
			// Defining connection properties
			Connection.gradient = it->gradient;
			Connection.curvature = it->curvature;
			Connection.speedLimit = 16.67;
			Connection.length = N2.X - N1.X;
			Connection.ID = N2.X + N1.X;
		} else { // else if the Node N1 is the first Node of the Block Section BS1
			Connection.gradient = BS1.arcs_in_signalling_block_section[0].gradient;
			Connection.curvature = BS1.arcs_in_signalling_block_section[0].curvature;
			Connection.speedLimit = 16.67;
			Connection.length = N2.X - N1.X;
			Connection.ID = N2.X + N1.X;
		}

		// Looking if there is any station Node of BS1 which falls in the middle between N1 and N2 which would risk not to be considered
		for (int k = 0; k < BS1.total_arcs; k++) {
			if (BS1.arcs_in_signalling_block_section[k].endNode.stationName.empty() != 1) { // if among the nodes of BS1 there is a station falling within N1.X and N2.X
				if ((BS1.arcs_in_signalling_block_section[k].endNode.X > N1.X) && (BS1.arcs_in_signalling_block_section[k].endNode.X <= N2.X)) {
					if (N2.stationName.empty() == 1) {											 // then if Node N2 is not already a station
						N2.stationName = BS1.arcs_in_signalling_block_section[k].endNode.stationName; // Push the station Node to Node N2 so that we do not lose the station point
					}
				}
			}
		}

		// adding connection to the listarc
		listarc.push_back(Connection);
		Node NConn;
		BS3.total_nodes++;
		NConn = N2;
		listnode.push_back(NConn);

		// Setting the Second part of arcs of BS3
		for (int i = 0; i < BS2.total_arcs; i++) {
			if (BS2.arcs_in_signalling_block_section[i].endNode.X > N2.X) {
				BS3.total_arcs++;
				listarc.push_back(BS2.arcs_in_signalling_block_section[i]);
			}
		}
		// Setting the second part of nodes of BS3
		for (int i = 0; i < BS2.total_nodes; i++) {
			if (BS2.nodelist_of_nodes_in_signalling_section[i].X > N2.X) {
				BS3.total_nodes++;
				// filling in the list of nodes belonging to BS3
				listnode.push_back(BS2.nodelist_of_nodes_in_signalling_section[i]);
			}
		}

		// Defining BS3 features
		BS3.nodelist_of_nodes_in_signalling_section = new Node[BS3.total_nodes];
		list<Node>::iterator itn = listnode.begin();
		for (int i = 0; i < BS3.total_nodes; i++) {
			BS3.nodelist_of_nodes_in_signalling_section[i] = *itn;
			itn++;
		}

		it = listarc.begin();
		for (int i = 0; i < BS3.total_arcs; i++) {
			BS3.arcs_in_signalling_block_section[i] = *it;
			it++;
		}
		char N1X[20], N2X[20];
		sprintf_s(N1X, "%f", N1.X);
		sprintf_s(N2X, "%f", N2.X);
		BS3.withSwitchDiv = true; // This block contains a switch in a diverged position
		BS3.XStartSwitch = N1.X;  // The begin of the switch is the abscissa N1.X
		BS3.XEndSwitch = N2.X;	  // The end of the switch is in the abscissa N2.X
		BS3.ID = BS3.ID + BS1.ID + "-" + N1X + "/" + BS2.ID + "-" + N2X;
		BS3.length = BS3.end_node.X - BS3.start_node.X;
	}

	// Setting tdsbId for stations which are in BS3
	// Taking the name of Block Section BS1
	istringstream Line(BS1.ID);
	istringstream Line2(BS2.ID);
	list<string> TokenA, TokenB;
	string tok1, tok2;
	// Splitting its name in tokens separated by the charachter "@"
	int N_validTokInA = 0;
	int N_validTokInB = 0; // Number of valid tok in TokenA
	while (getline(Line, tok1, '@')) {
		if (tok1.size() > 0) {
			if ((N_validTokInA == 0) || (N_validTokInA == 2)) {
				TokenA.push_back(tok1);
			}
			N_validTokInA++; // Block Section names in list A
		}
	}

	while (getline(Line2, tok2, '@')) {
		if (tok2.size() > 0) {
			if ((N_validTokInB == 0) || (N_validTokInB == 2)) {
				TokenB.push_back(tok2);
			}
			N_validTokInB++; // Block Section names in list blockSets
		}
	}

	list<string>::iterator it = TokenA.begin(); /*pointer at the name of BS1*/
	list<string>::iterator u = TokenB.begin();	// pointer at the name of BS2
	if (BS3.total_arcs > 0) {
		for (int k = 0; k < BS3.total_arcs; k++) {
			if (BS3.arcs_in_signalling_block_section[k].endNode.stationName.empty() != 1) { // if the Node is a station so the stationName is not empty
				// Assign GeoCoordinates
				BS3.arcs_in_signalling_block_section[k].endNode.tdsbGeoCoordX = BS3.arcs_in_signalling_block_section[k].endNode.X * 1000;
				BS3.arcs_in_signalling_block_section[k].endNode.tdsbGeoCoordY = BS3.arcs_in_signalling_block_section[k].endNode.Y * 1000;
				// Assign TDSB ID, in case it does not have one already
				if (BS3.arcs_in_signalling_block_section[k].endNode.X <= N1.X) { // if the station is before or at N1, so this one will belong to BS1
					if (BS3.arcs_in_signalling_block_section[k].endNode.tdsbId.empty() == 1) {
						BS3.arcs_in_signalling_block_section[k].endNode.tdsbId = BS3.arcs_in_signalling_block_section[k].endNode.tdsbId + "@" + BS3.arcs_in_signalling_block_section[k].endNode.stationName + "-" + *it + "@";
					} else { // if the tdsbId is not empty then leave it as it is
					}
					if (BS3.arcs_in_signalling_block_section[k].endNode.X == BS3.end_node.X) { // if such a station Node coincides with the last Node of BS3 then change also the tdsbId to last Node of BS3
						BS3.end_node.tdsbId = BS3.arcs_in_signalling_block_section[k].endNode.tdsbId;
					}
				} else {																   // if instead the Station Node has a progressive higher than N1.X then it belongs to BS2
					if (BS3.arcs_in_signalling_block_section[k].endNode.tdsbId.empty() == 1) { // if the tdsbId is empty then assign it as a TDSB on BS2
						BS3.arcs_in_signalling_block_section[k].endNode.tdsbId = BS3.arcs_in_signalling_block_section[k].endNode.tdsbId + "@" + BS3.arcs_in_signalling_block_section[k].endNode.stationName + "-" + *u + "@";
					} else { // if the tdsbId is not empty then leave as it is
					}

					if (BS3.arcs_in_signalling_block_section[k].endNode.X == BS3.end_node.X) { // if such a station Node coincides with the last Node of BS3 then change also the tdsbId to last Node of BS3
						BS3.end_node.tdsbId = BS3.arcs_in_signalling_block_section[k].endNode.tdsbId;
					}
				}
			}
		}
	}
}

// Function to Generate Block Sections connected by switches (it must be used in createBlockConn)
void generateConnectBlock(Connections* AllConnections, Section BS1, Section BS2, Node N1, Node N2, Section& BS3) {
	if (N1.X < N2.X) {
		list<Node> listnode;
		list<Arc> listarc;
		// Setting initial and final nodes
		BS3.start_node = BS1.start_node;
		BS3.end_node = BS2.end_node;
		// Setting the connected TrackLines
		BS3.FirstConnectedTrackLineID = BS1.trackLineId;
		BS3.SecondConnectedTrackLineID = BS2.trackLineId;

		//
		double speed_lim_on_switch_arc = 0;

		// Identifying the speed limit to be set on the switch connection in the block section BS3
		for (int c = 0; c < numConnections; c++) {
			if ((AllConnections[c].idFirstTrackLine == BS3.FirstConnectedTrackLineID) && (AllConnections[c].idSecondTrackLine == BS3.SecondConnectedTrackLineID) && (AllConnections[c].xFirstNode == N1.X) && (AllConnections[c].xSecondNode == N2.X)) {
				speed_lim_on_switch_arc = AllConnections[c].speedlimit;
				break;
			}
		}

		// Setting the first part of Block arcs
		for (int i = 0; i < BS1.total_arcs; i++) {
			if (BS1.arcs_in_signalling_block_section[i].endNode.X <= N1.X) {
				BS3.total_arcs++;
				// filling in the list of arcs belonging to BS3
				listarc.push_back(BS1.arcs_in_signalling_block_section[i]);
			}
		}
		// Setting the first part of Block nodes
		for (int i = 0; i < BS1.total_nodes; i++) {
			if (BS1.nodelist_of_nodes_in_signalling_section[i].X <= N1.X) {
				BS3.total_nodes++;
				// filling in the list of nodes belonging to BS3
				listnode.push_back(BS1.nodelist_of_nodes_in_signalling_section[i]);
			}
		}

		// Creating the connection
		Arc Connection;
		BS3.total_arcs++;
		Connection.startNode = N1;
		Connection.endNode = N2;
		list<Arc>::iterator it;
		if (listarc.empty() != 1) { // if the Node N1 is not the first Node of the Block Section BS1
			it = listarc.end();
			it--;
			// Defining connection properties
			Connection.gradient = it->gradient;
			Connection.curvature = it->curvature;
			Connection.speedLimit = speed_lim_on_switch_arc;
			Connection.length = N2.X - N1.X;
			Connection.ID = N2.X + N1.X;
		} else { // else if the Node N1 is the first Node of the Block Section BS1
			Connection.gradient = BS1.arcs_in_signalling_block_section[0].gradient;
			Connection.curvature = BS1.arcs_in_signalling_block_section[0].curvature;
			Connection.speedLimit = speed_lim_on_switch_arc;
			Connection.length = N2.X - N1.X;
			Connection.ID = N2.X + N1.X;
		}

		// Looking if there is any station Node of BS1 which falls in the middle between N1 and N2 which would risk not to be considered
		for (int k = 0; k < BS1.total_arcs; k++) {
			if (BS1.arcs_in_signalling_block_section[k].endNode.stationName.empty() != 1) { // if among the nodes of BS1 there is a station falling within N1.X and N2.X
				if ((BS1.arcs_in_signalling_block_section[k].endNode.X > N1.X) && (BS1.arcs_in_signalling_block_section[k].endNode.X <= N2.X)) {
					if (N2.stationName.empty() == 1) {											 // then if Node N2 is not already a station
						N2.stationName = BS1.arcs_in_signalling_block_section[k].endNode.stationName; // Push the station Node to Node N2 so that we do not lose the station point
					}
				}
			}
		}

		// adding connection to the listarc
		listarc.push_back(Connection);
		Node NConn;
		BS3.total_nodes++;
		NConn = N2;
		listnode.push_back(NConn);

		// Setting the Second part of arcs of BS3
		for (int i = 0; i < BS2.total_arcs; i++) {
			if (BS2.arcs_in_signalling_block_section[i].endNode.X > N2.X) {
				BS3.total_arcs++;
				listarc.push_back(BS2.arcs_in_signalling_block_section[i]);
			}
		}
		// Setting the second part of nodes of BS3
		for (int i = 0; i < BS2.total_nodes; i++) {
			if (BS2.nodelist_of_nodes_in_signalling_section[i].X > N2.X) {
				BS3.total_nodes++;
				// filling in the list of nodes belonging to BS3
				listnode.push_back(BS2.nodelist_of_nodes_in_signalling_section[i]);
			}
		}

		// Defining BS3 features
		BS3.nodelist_of_nodes_in_signalling_section = new Node[BS3.total_nodes];
		list<Node>::iterator itn = listnode.begin();
		for (int i = 0; i < BS3.total_nodes; i++) {
			BS3.nodelist_of_nodes_in_signalling_section[i] = *itn;
			itn++;
		}

		it = listarc.begin();
		for (int i = 0; i < BS3.total_arcs; i++) {
			BS3.arcs_in_signalling_block_section[i] = *it;
			it++;
		}
		char N1X[20], N2X[20];
		sprintf_s(N1X, "%f", N1.X);
		sprintf_s(N2X, "%f", N2.X);
		BS3.withSwitchDiv = true; // This block contains a switch in a diverged position
		BS3.XStartSwitch = N1.X;  // The begin of the switch is the abscissa N1.X
		BS3.XEndSwitch = N2.X;	  // The end of the switch is in the abscissa N2.X
		BS3.ID = BS3.ID + BS1.ID + "-" + N1X + "/" + BS2.ID + "-" + N2X;
		BS3.length = BS3.end_node.X - BS3.start_node.X;
	}

	// Setting tdsbId for stations which are in BS3
	// Taking the name of Block Section BS1
	istringstream Line(BS1.ID);
	istringstream Line2(BS2.ID);
	list<string> TokenA, TokenB;
	string tok1, tok2;
	// Splitting its name in tokens separated by the charachter "@"
	int N_validTokInA = 0;
	int N_validTokInB = 0; // Number of valid tok in TokenA
	while (getline(Line, tok1, '@')) {
		if (tok1.size() > 0) {
			if ((N_validTokInA == 0) || (N_validTokInA == 2)) {
				TokenA.push_back(tok1);
			}
			N_validTokInA++; // Block Section names in list A
		}
	}

	while (getline(Line2, tok2, '@')) {
		if (tok2.size() > 0) {
			if ((N_validTokInB == 0) || (N_validTokInB == 2)) {
				TokenB.push_back(tok2);
			}
			N_validTokInB++; // Block Section names in list blockSets
		}
	}

	list<string>::iterator it = TokenA.begin(); /*pointer at the name of BS1*/
	list<string>::iterator u = TokenB.begin();	// pointer at the name of BS2
	if (BS3.total_arcs > 0) {
		for (int k = 0; k < BS3.total_arcs; k++) {
			if (BS3.arcs_in_signalling_block_section[k].endNode.stationName.empty() != 1) { // if the Node is a station so the stationName is not empty
				// Assign GeoCoordinates
				BS3.arcs_in_signalling_block_section[k].endNode.tdsbGeoCoordX = BS3.arcs_in_signalling_block_section[k].endNode.X * 1000;
				BS3.arcs_in_signalling_block_section[k].endNode.tdsbGeoCoordY = BS3.arcs_in_signalling_block_section[k].endNode.Y * 1000;
				// Assign TDSB ID, in case it does not have one already
				if (BS3.arcs_in_signalling_block_section[k].endNode.X <= N1.X) { // if the station is before or at N1, so this one will belong to BS1
					if (BS3.arcs_in_signalling_block_section[k].endNode.tdsbId.empty() == 1) {
						BS3.arcs_in_signalling_block_section[k].endNode.tdsbId = BS3.arcs_in_signalling_block_section[k].endNode.tdsbId + "@" + BS3.arcs_in_signalling_block_section[k].endNode.stationName + "-" + *it + "@";
					} else { // if the tdsbId is not empty then leave it as it is
					}
					if (BS3.arcs_in_signalling_block_section[k].endNode.X == BS3.end_node.X) { // if such a station Node coincides with the last Node of BS3 then change also the tdsbId to last Node of BS3
						BS3.end_node.tdsbId = BS3.arcs_in_signalling_block_section[k].endNode.tdsbId;
					}
				} else {																   // if instead the Station Node has a progressive higher than N1.X then it belongs to BS2
					if (BS3.arcs_in_signalling_block_section[k].endNode.tdsbId.empty() == 1) { // if the tdsbId is empty then assign it as a TDSB on BS2
						BS3.arcs_in_signalling_block_section[k].endNode.tdsbId = BS3.arcs_in_signalling_block_section[k].endNode.tdsbId + "@" + BS3.arcs_in_signalling_block_section[k].endNode.stationName + "-" + *u + "@";
					} else { // if the tdsbId is not empty then leave as it is
					}

					if (BS3.arcs_in_signalling_block_section[k].endNode.X == BS3.end_node.X) { // if such a station Node coincides with the last Node of BS3 then change also the tdsbId to last Node of BS3
						BS3.end_node.tdsbId = BS3.arcs_in_signalling_block_section[k].endNode.tdsbId;
					}
				}
			}
		}
	}
}

// Function to Create connected block sections each time that a connection Node comes out (it must be used in setAllConnections)
void createBlockConn(Node Nb, Section BS1, int Temp_Blocks, int n_conn) {
	int CheckBlockPresence = 0;

	if (Nb.X < Nb.connectXNode[n_conn]) {		// if the connection Node has a X higher than its X
		for (int i = 0; i < Temp_Blocks; i++) { // Search for the connected Node in the Block Section belonging to the indicated connectIdBlockSet
			if (signalling_block_sections[i].trackLineId == Nb.connectIdBlockSet[n_conn]) {
				for (int j = 0; j < signalling_block_sections[i].total_nodes; j++) { // Search for the connected Node
					if (signalling_block_sections[i].nodelist_of_nodes_in_signalling_section[j].X == Nb.connectXNode[n_conn]) {
						string BSID, OppBSID;
						char N1ID[20], N2ID[20];
						sprintf_s(N1ID, "%f", Nb.X);
						sprintf_s(N2ID, "%f", signalling_block_sections[i].nodelist_of_nodes_in_signalling_section[j].X);
						BSID = BSID + BS1.ID + "-" + N1ID + "/" + signalling_block_sections[i].ID + "-" + N2ID;
						OppBSID = OppBSID + signalling_block_sections[i].ID + "-" + N2ID + "/" + BS1.ID + "-" + N1ID;

						for (int k = 0; k < Temp_Blocks; k++) {																 // search if a block section with the same ID or opposite ID is already present in the database of Block Sections
							if ((signalling_block_sections[k].ID == BSID) || (signalling_block_sections[k].ID == OppBSID)) { // if it is present CheckBlockPresence increases
								CheckBlockPresence++;
							}
						}
						if (CheckBlockPresence == 0) // if there is not a similar block section the new connecting block section is created
						{
							Blocks++;
							generateConnectBlock(connections, BS1, signalling_block_sections[i], Nb, signalling_block_sections[i].nodelist_of_nodes_in_signalling_section[j], signalling_block_sections[Blocks - 1]);
						}
					}
				}
			}
		}
	}
}

// Function to automatically create block sections connected by switches
void setAllConnections() {

	int Temp_Blocks = Blocks; // Temporary variable which takes the total number of Block without connections
	for (int i = 0; i < Temp_Blocks; i++) {
		for (int j = 0; j < signalling_block_sections[i].total_nodes; j++) {
			if (signalling_block_sections[i].nodelist_of_nodes_in_signalling_section[j].numConnections > 0) {
				// std::cout << "=============================\n";
				/*std::cout << signalling_block_sections[i].ID << "-" << signalling_block_sections[i].FirstConnectedTrackLineID <<
					"+" << signalling_block_sections[i].SecondConnectedTrackLineID << "-" << signalling_block_sections[i].nodelist_of_nodes_in_signalling_section[j].numConnections <<"="<<j<< std::endl;*/
				for (int n = 0; n < signalling_block_sections[i].nodelist_of_nodes_in_signalling_section[j].numConnections; n++) {
					createBlockConn(signalling_block_sections[i].nodelist_of_nodes_in_signalling_section[j], signalling_block_sections[i], Temp_Blocks, n);
				}
			}
		}
	}
}

// This function create a list of section which have the string "input" in their ID
void setList(string input, list<Section>& BSConnected) {
	for (int i = 0; i < Blocks; i++) {
		if (signalling_block_sections[i].ID.find(input, 0) != -1) {
			BSConnected.push_back(signalling_block_sections[i]);
		}
	}
}

// This Function is used to set the dependencies between two block section connected by means of switches
void setDependenciesBetweenBlocks() {
	for (int i = 0; i < Blocks; i++) {
		list<Section> BSConn_i;
		setList(signalling_block_sections[i].ID, BSConn_i);
		list<Section>::iterator it;
		for (it = BSConn_i.begin(); it != BSConn_i.end(); it++) {
			if (it->ID != signalling_block_sections[i].ID) {
				signalling_block_sections[i].N_ConnectedBS++;
				signalling_block_sections[i].IDConnectedBS[signalling_block_sections[i].N_ConnectedBS - 1] = it->ID;
			}
			// test for Copenhagen
			if (signalling_block_sections[i].ID == "@5-B6@") {
				signalling_block_sections[i].N_ConnectedBS++;
				signalling_block_sections[i].IDConnectedBS[signalling_block_sections[i].N_ConnectedBS - 1] = "@1-B30@-4.592000/@5-B7@-4.620000";
			}
		}

		//
		// Now transferring the right Block Connections to the nodes with numConnections >0
		for (int h = 0; h < signalling_block_sections[i].total_arcs; h++) {
			if (signalling_block_sections[i].arcs_in_signalling_block_section[h].endNode.numConnections > 0) {
				signalling_block_sections[i].arcs_in_signalling_block_section[h].endNode.initialiseIdConnectedBlocks(signalling_block_sections[i].IDConnectedBS, signalling_block_sections[i].N_ConnectedBS);
			}
		}
	}
}

// Function to recognize if two block sections are overlapping
bool areBlocksConnected(Section A, Section blockSets) {
	bool AreOnSameBlock = false; // if the block does not have a station IsCommonBlock=true is enough to turn this variable into true, else if the block has a station this variable turns to true if IsCommonBlock is true and Blocking Time blockSets has the same stationName;
	bool IsCommonBlock = false;	 // This variable is true if one of the two blocking times have at least a block in common
	string BlockNameA;
	BlockNameA = A.ID;
	istringstream Line(BlockNameA);
	list<string> TokenA;
	string tok;

	while (getline(Line, tok, '@')) {
		if (tok.size() > 0)
			TokenA.push_back(tok);
	}

	string BlockNameB = blockSets.ID;
	istringstream Line2(BlockNameB);
	list<string> TokenB;
	string tok2;

	while (getline(Line2, tok2, '@')) {
		if (tok2.size() > 0)
			TokenB.push_back(tok2);
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
	if ((((blockSets.start_node.X >= A.start_node.X) && (blockSets.start_node.X < A.end_node.X)) || ((A.start_node.X >= blockSets.start_node.X) && (A.start_node.X < blockSets.end_node.X))) && (IsCommonBlock == 1)) { // if the beginning of A or the beginning of blockSets is in between the beginning and Ending Node of the other and they have a common part of the name
		AreOnSameBlock = true;
	}

	if (AreOnSameBlock == 1)
		return true;
	else
		return false;
}

// Function to set all Track Detection Section Boundaries and Geo Coordinates
void setTrackDetectionSectionBoundariesAndGeoCoordAtSwitchesAndStations(Section* BS, int N_BlockSections) {
	std::string name_start, name_end;
	if (N_BlockSections > 0) {
		for (int i = 0; i < N_BlockSections; i++) {
			// Taking the name of the Block Section
			istringstream Line(BS[i].ID);
			list<string> TokenA;
			string tok;
			// Splitting its name in tokens separated by the charachter "@"
			int N_validTokInA = 0; // Number of valid tok in TokenA
			while (getline(Line, tok, '@')) {
				if (tok.size() > 0) {
					if ((N_validTokInA == 0) || (N_validTokInA == 2)) {
						TokenA.push_back(tok);
					}
					N_validTokInA++; // Block Sections with A diverging Switch should have two elements in the list tok in A
				}
			}
			// if Block Section has WithDivSwitch = 0, so if it is a Block Section with no diverging switch
			if (BS[i].withSwitchDiv == 0) {
				list<string>::iterator it = TokenA.begin();

				for (int j = 0; j < BS[i].total_arcs; j++) {
					// See if the first member of the block has the initial Node to be a switch
					if (j == 0) {
						if (BS[i].arcs_in_signalling_block_section[j].startNode.numConnections > 0) {
							if (BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId.empty() != 1)
								BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId = BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId + "Point"; // Allocating the name to start_node of arcs_in_signalling_block_section
							else {
								eglogger << "Warning: Block " << BS[i].ID << " has Beginning Node with no name. A name will be automatically assigned to it and need to manually check congruency in case of timetable compression\n";
								BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId = BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId + "@Beg-" + *it + "@Point";
							}
							// Then change also the ID of the first point of the block section
							BS[i].start_node.tdsbId = BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId; // changing the name of start_node of signalling_block_sections[i]
							// Setting absolute coordinates to Node start_node of arcs_in_signalling_block_section
							BS[i].arcs_in_signalling_block_section[j].startNode.tdsbGeoCoordX = BS[i].arcs_in_signalling_block_section[j].startNode.X * 1000;
							BS[i].arcs_in_signalling_block_section[j].startNode.tdsbGeoCoordY = BS[i].arcs_in_signalling_block_section[j].startNode.Y * 1000;
							name_start = BS[i].start_node.tdsbId;
						}
						if (BS[i].arcs_in_signalling_block_section[j].endNode.numConnections > 0) {
							char pointcoord[20];
							string NameTDSB; // Define name variables
							// In case this Node coincides with the last Node of the Block Section, then change also the ID of the last Node of the block section itself
							if (BS[i].arcs_in_signalling_block_section[j].endNode.X == BS[i].end_node.X) {
								if (BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId.empty() != 1)
									BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId + "Point"; // Changing the name of Node end_node of member
								else {
									eglogger << "Warning: Block " << BS[i].ID << " has End Node with no name, a name will be automatically assigned to it and need to manually check congruency in case of timetable compression\n";
									BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = BS[i].end_node.tdsbId + "@End-" + *it + "@Point";
								}
								BS[i].end_node.tdsbId = BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId; // Changing also the name of Node end_node of signalling_block_sections[i]
							} else {
								sprintf_s(pointcoord, "%f", BS[i].arcs_in_signalling_block_section[j].endNode.X);
								if (BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId.empty() != 1)
									BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId + "Point"; // Keep the same name if the Node already has a name and add that is a Point
								else {
									NameTDSB = NameTDSB + "@" + pointcoord + "-" + *it + "@Point";
									BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = NameTDSB;
								}
								name_end = BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId;
							}
							// Setting Absolute Coordinates of the member Node
							BS[i].arcs_in_signalling_block_section[j].endNode.tdsbGeoCoordX = BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000;
							BS[i].arcs_in_signalling_block_section[j].endNode.tdsbGeoCoordY = BS[i].arcs_in_signalling_block_section[j].endNode.Y * 1000;
						}

					} else { // if it is not the initial member of the block section
						if (BS[i].arcs_in_signalling_block_section[j].endNode.numConnections > 0) {
							char pointcoord[20];
							string NameTDSB; // Define name variables
							// In case this Node coincides with the last Node of the Block Section, then change also the ID of the last Node of the block section itself
							if (BS[i].arcs_in_signalling_block_section[j].endNode.X == BS[i].end_node.X) {
								if (BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId.empty() != 1)
									BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId + "Point"; // Changing the name of Node end_node of member
								else {
									cout << "Warning: Block " << BS[i].ID << " has End Node with no name, a name will be automatically assigned to it and need to manually check congruency in case of timetable compression\n";
									BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId + "@End-" + *it + "@Point";
								}
								BS[i].end_node.tdsbId = BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId; // Changing also the name of Node end_node of signalling_block_sections[i]
							} else {
								sprintf_s(pointcoord, "%f", BS[i].arcs_in_signalling_block_section[j].endNode.X);
								if (BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId.empty() != 1)
									BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId + "Point";
								else {
									NameTDSB = NameTDSB + "@" + pointcoord + "-" + *it + "@Point";
									BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = NameTDSB;
								}
							}
							// Setting Absolute Coordinates of the member Node
							BS[i].arcs_in_signalling_block_section[j].endNode.tdsbGeoCoordX = BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000;
							BS[i].arcs_in_signalling_block_section[j].endNode.tdsbGeoCoordY = BS[i].arcs_in_signalling_block_section[j].endNode.Y * 1000;
						}
					}
					if (BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId.empty() != 1)
						owl << "BS: " << BS[i].ID << " startNode ID: " << BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId << std::endl;
					if (BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId.empty() != 1)
						owl << "BS: " << BS[i].ID << " endNode ID: " << BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId << std::endl;
				}
			}

			else { // if instead the block section has withSwitchDiv = true, i.e. if the block section has a diverging switch we need to set 2 TDSBs: one at the start and the other at the end of the switch
				list<string>::iterator it = TokenA.begin();
				list<string>::iterator u = TokenA.end();
				advance(u, -1);
				for (int j = 0; j < BS[i].total_arcs; j++) {
					if (j == 0) { // if the start of switch is the first Node of the Arc
						if (BS[i].arcs_in_signalling_block_section[j].startNode.X == BS[i].XStartSwitch) {
							char pointcoord1[20];
							string NameTDSB;
							char pointcoord2[20];
							sprintf_s(pointcoord1, "%f", BS[i].XStartSwitch);
							sprintf_s(pointcoord2, "%f", BS[i].XEndSwitch);
							// Setting ID for start_node of arcs_in_signalling_block_section[j]
							if (BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId.empty() != 1) {
								NameTDSB = NameTDSB + BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId + "/@" + pointcoord2 + "-" + *u + "@Point-Start";
								BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId = NameTDSB;
							} else {
								cout << "Warning: Block " << BS[i].ID << " has Beginning Node with no name. A name will be automatically assigned to it and need to manually check congruency in case of timetable compression\n";
								NameTDSB = NameTDSB + "@Beg-" + *it + "@/@" + pointcoord2 + "-" + *u + "@Point-Start";
								BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId = NameTDSB;
							}
							// Changing the name of signalling_block_sections[i].start_node
							BS[i].start_node.tdsbId = BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId;

							// Setting Coordinates for start_node of arcs_in_signalling_block_section [j]
							BS[i].arcs_in_signalling_block_section[j].startNode.tdsbGeoCoordX = BS[i].arcs_in_signalling_block_section[j].startNode.X * 1000;
							BS[i].arcs_in_signalling_block_section[j].startNode.tdsbGeoCoordY = BS[i].arcs_in_signalling_block_section[j].startNode.Y * 1000;
						}
						if (BS[i].arcs_in_signalling_block_section[j].startNode.numConnections > 0) {
							if ((BS[i].arcs_in_signalling_block_section[j].startNode.X != BS[i].XStartSwitch) && (BS[i].arcs_in_signalling_block_section[j].startNode.X != BS[i].XEndSwitch)) {

								if (BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId.empty() != 1) {
									cout << "Warning: The initial Node of Block Section: " << BS[i].ID << "is also a switch: Changing its TDSB ID accordingly in: ";
									BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId = BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId + "Point"; // Allocating the name to start_node of arcs_in_signalling_block_section
									cout << BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId << "\n";
								} else {
									cout << "Warning: Block " << BS[i].ID << " has Beginning Node with no name. A name will be automatically assigned to it and need to manually check congruency in case of timetable compression\n";
									if (BS[i].arcs_in_signalling_block_section[j].startNode.X < BS[i].XStartSwitch) {
										BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId = BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId + "@Beg-" + *it + "@Point"; // for nodes different from the starting one this one should have as a name @end_node.X-BlockSectionID(of *it)@Point
									} else if (BS[i].arcs_in_signalling_block_section[j].startNode.X > BS[i].XEndSwitch) {
										cout << "ERROR: in Block Section" << BS[i].ID << " the starting Node has a position that goes after the end of the Switch\n"; // for nodes different from the starting one this one should have as a name @end_node.X-BlockSectionID(of *u)@Point

									} else if ((BS[i].arcs_in_signalling_block_section[j].startNode.X > BS[i].XStartSwitch) && (BS[i].arcs_in_signalling_block_section[j].startNode.X < BS[i].XEndSwitch)) {
										cout << "ERROR: in Block Section" << BS[i].ID << " the starting Node has a position in between the start and the end of the switch\n"; // for nodes different from the starting one this one should have as a name @end_node.X-BlockSectionID(of *u)@Point
									}
								}
								// Then change also the ID of the first point of the block section
								BS[i].start_node.tdsbId = BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId; // changing the name of start_node of signalling_block_sections[i]
																												 // Setting absolute coordinates to Node start_node of arcs_in_signalling_block_section
								BS[i].arcs_in_signalling_block_section[j].startNode.tdsbGeoCoordX = BS[i].arcs_in_signalling_block_section[j].startNode.X * 1000;
								BS[i].arcs_in_signalling_block_section[j].startNode.tdsbGeoCoordY = BS[i].arcs_in_signalling_block_section[j].startNode.Y * 1000;
							}
						}
					}
					// Now look just at the end nodes of the arcs
					if (BS[i].arcs_in_signalling_block_section[j].endNode.X == BS[i].XStartSwitch) {
						char pointcoord1[20];
						string NameTDSB;
						bool NoNameOfNode = false; // NoNameOfNode becomes true when the Node has not already a name
						char pointcoord2[20];
						sprintf_s(pointcoord1, "%f", BS[i].XStartSwitch);
						sprintf_s(pointcoord2, "%f", BS[i].XEndSwitch);
						// Setting ID for end_node of arcs_in_signalling_block_section[j]
						if (BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId.empty() != 1)
							NameTDSB = NameTDSB + BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId + "/@" + pointcoord2 + "-" + *u + "@Point-Start";
						else {
							NoNameOfNode = true;
							NameTDSB = NameTDSB + "@" + pointcoord1 + "-" + *it + "@/@" + pointcoord2 + "-" + *u + "@Point-Start";
						}
						BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = NameTDSB;
						// if this corresponds to the last Node of signalling_block_sections[i]
						if (BS[i].arcs_in_signalling_block_section[j].endNode.X == BS[i].end_node.X) { // change also the name of the last Node
							cout << "Warning: Check correctness of Block Section:" << BS[i].ID << " its ending Node is the start of the switch on the same block section\n\n";
							if (NoNameOfNode == 1) {
								eglogger << "Warning: Block " << BS[i].ID << " has Ending Node with no name. A name will be automatically assigned to it and need to manually check congruency in case of timetable compression\n";
								NameTDSB = NameTDSB + "@End-" + *it + "@/@" + pointcoord2 + "-" + *u + "@Point-Start";
								BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = NameTDSB;
							} else { // the name of the block section is one of the names given in the else condition just above this
							}

							BS[i].end_node.tdsbId = BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId; // Change the name of the TDSB of the last Node end_node of signalling_block_sections[i]: signalling_block_sections[i].end_node.tdsbId
						}

						// Setting Coordinates
						BS[i].arcs_in_signalling_block_section[j].endNode.tdsbGeoCoordX = BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000;
						BS[i].arcs_in_signalling_block_section[j].endNode.tdsbGeoCoordY = BS[i].arcs_in_signalling_block_section[j].endNode.Y * 1000;

					}

					else if (BS[i].arcs_in_signalling_block_section[j].endNode.X == BS[i].XEndSwitch) {
						char pointcoord1[20];
						string NameTDSB;
						bool NoNameOfNode = false; // NoNameOfNode becomes true when the Node has not already a name
						char pointcoord2[20];
						sprintf_s(pointcoord1, "%f", BS[i].XStartSwitch);
						sprintf_s(pointcoord2, "%f", BS[i].XEndSwitch);
						// Setting ID for end_node of arcs_in_signalling_block_section[j]
						if (BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId.empty() != 1) {
							NameTDSB = NameTDSB + "@" + pointcoord1 + "-" + *it + "@/" + BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId + "Point-End";
						} else {
							NoNameOfNode = true;
							NameTDSB = NameTDSB + "@" + pointcoord1 + "-" + *it + "@/@" + pointcoord2 + "-" + *u + "@Point-End";
						}
						BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = NameTDSB;
						// if this corresponds to the last Node of signalling_block_sections[i]
						if (BS[i].arcs_in_signalling_block_section[j].endNode.X == BS[i].end_node.X) { // change also the name of the last Node
							if (NoNameOfNode == 1) {
								eglogger << "Warning: Block " << BS[i].ID << " has Ending Node with no name. A name will be automatically assigned to it and need to manually check congruency in case of timetable compression\n";
								NameTDSB = NameTDSB + "@" + pointcoord1 + "-" + *it + "@/@End-" + *u + "@Point-End";
								BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = NameTDSB;
							} else { // the name of the block section is one of the names given in the else condition just above this
							}

							BS[i].end_node.tdsbId = BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId;
						}

						// Setting Coordinates
						BS[i].arcs_in_signalling_block_section[j].endNode.tdsbGeoCoordX = BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000;
						BS[i].arcs_in_signalling_block_section[j].endNode.tdsbGeoCoordY = BS[i].arcs_in_signalling_block_section[j].endNode.Y * 1000;
					}

					if (BS[i].arcs_in_signalling_block_section[j].endNode.numConnections > 0) {
						if ((BS[i].arcs_in_signalling_block_section[j].endNode.X != BS[i].XStartSwitch) && (BS[i].arcs_in_signalling_block_section[j].endNode.X != BS[i].XEndSwitch)) {
							char pointcoord[20];
							string NameTDSB;
							bool NoNameOfNode = false; // NoNameOfNode becomes true when the Node has not already a name

							sprintf_s(pointcoord, "%f", BS[i].arcs_in_signalling_block_section[j].endNode.X);

							if (BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId.empty() != 1)
								BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId + "Point"; // Allocating the name to start_node of arcs_in_signalling_block_section
							else {
								NoNameOfNode = true; // Setting NoName to true since the Node does not have any name of the TDSB ID

								if (BS[i].arcs_in_signalling_block_section[j].endNode.X < BS[i].XStartSwitch) {
									BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId + "@" + pointcoord + "-" + *it + "@Point"; // Assigning TDSB ID as @end_node.X-BlockSectionID(of *it)@Point
								} else if (BS[i].arcs_in_signalling_block_section[j].endNode.X > BS[i].XEndSwitch) {
									BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId + "@" + pointcoord + "-" + *u + "@Point"; // Assigning TDSB ID as @end_node.X-BlockSectionID(of *u)@Point

								} else if ((BS[i].arcs_in_signalling_block_section[j].endNode.X > BS[i].XStartSwitch) && (BS[i].arcs_in_signalling_block_section[j].endNode.X < BS[i].XEndSwitch)) {
									cout << "WARNING: in Block Section" << BS[i].ID << " Node at position: " << BS[i].arcs_in_signalling_block_section[j].endNode.X << " is a switch defined in between the start (XStartSwitch) and the end (XEndSwitch) of the diverging switch. A name will be automatically assigned to it\n"; // for nodes different from the starting one this one should have as a name @end_node.X-BlockSectionID(of *u)@Point
									BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId + "@" + pointcoord + "-" + *u + "@Point";																																	  // here it is assumed that the Node is part of the second block section *u connected by the diverging switch of block Section signalling_block_sections[i]
								}
							}
							// if this corresponds to the last Node of signalling_block_sections[i]
							if (BS[i].arcs_in_signalling_block_section[j].endNode.X == BS[i].end_node.X) { // change also the name of the last Node
								if (NoNameOfNode == 1) {
									eglogger << "Warning: Block " << BS[i].ID << " has Ending Node with no name. A name will be automatically assigned to it and need to manually check congruency in case of timetable compression\n";
									NameTDSB = "@End-" + *u + "@Point";
									BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId = NameTDSB;
								} else { // the name of the block section is one of the names given in the else condition just above this
								}

								BS[i].end_node.tdsbId = BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId;
							}
							// Setting Coordinates
							BS[i].arcs_in_signalling_block_section[j].endNode.tdsbGeoCoordX = BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000;
							BS[i].arcs_in_signalling_block_section[j].endNode.tdsbGeoCoordY = BS[i].arcs_in_signalling_block_section[j].endNode.Y * 1000;
						}
					}

					if (BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId.empty() != 1)
						owl << "2BS: " << BS[i].ID << " startNode ID: " << BS[i].arcs_in_signalling_block_section[j].startNode.tdsbId << std::endl;
					if (BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId.empty() != 1)
						owl << "2BS: " << BS[i].ID << " endNode ID: " << BS[i].arcs_in_signalling_block_section[j].endNode.tdsbId << std::endl;
				}
			}
		}
	}
}

// Function to create TDSs
// the function takes as input the list of block sections, their numbers, the number of TDS in which the straight blocks (i.e. those not connected to any diverging switch) will be split into and the list of TDS in the infrastructure
void createTds(Section* Blocks, int N_Blocks, int N_TDS_On_Straight_Blocks, list<TDS>& ALL_TDS) {
	for (int i = 0; i < N_Blocks; i++) {
		// Defining TDS first for all the block sections having connection with a Diverging switch. Those block sections will by default split into two parts
		// with a TDS joint set in the middle of the switch
		if (Blocks[i].withSwitchDiv == 1) {
			// Tokenize the name of the block section
			// You should use teh name of teh first block section to give the name to TDS
			string BlockName;
			BlockName = Blocks[i].ID;
			istringstream Line(BlockName);
			list<string> TokenA;
			string tok;

			string IDFirstBlock, IDSecondBlock;

			while (getline(Line, tok, '@')) {
				if (tok.size() > 0)
					TokenA.push_back(tok);
			}

			list<string>::iterator t = TokenA.begin();
			IDFirstBlock = *t;
			// Introducing TDS in the Block sections with switches
			TDS TDS1, TDS2;
			list<TDS>::iterator TDS1Pointer, TDS2Pointer;

			TDS1.ID = "TDS_1@" + IDFirstBlock;
			// Defining the name of second TDS for the second part of the block section with the diverging switch
			advance(t, 2); // Advancing the iterator t to get the ID of teh second block section connected
			IDSecondBlock = *t;
			TDS2.ID = "TDS_2@" + IDSecondBlock;

			// Defining nodes for TDS1 on the first part of the block section
			TDS1.start_node = Blocks[i].start_node;
			for (int k = 0; k < Blocks[i].total_arcs; k++) {
				if (Blocks[i].arcs_in_signalling_block_section[k].endNode.X == Blocks[i].XStartSwitch) {
					TDS1.connection_node = Blocks[i].arcs_in_signalling_block_section[k].endNode;
					break;
				}
			}
			// Defining end of first TDS on the mid point of the switch
			TDS1.node_on_switch.X = Blocks[i].XStartSwitch + (Blocks[i].XEndSwitch - Blocks[i].XStartSwitch) / 2;
			TDS1.node_on_switch.tdsbId = "Mid_Switch_" + TDS1.ID + "/" + TDS2.ID;
			TDS1.node_on_switch.tdsbGeoCoordX = TDS1.node_on_switch.X * 1000;

			// Defining the nodes for the second TDS on the second part of the block with diverging switch
			TDS2.end_node = Blocks[i].end_node;
			// Putting as connection Node of the TDS the end of the switch
			for (int k = 0; k < Blocks[i].total_arcs; k++) {
				if (Blocks[i].arcs_in_signalling_block_section[k].endNode.X == Blocks[i].XEndSwitch) {
					TDS2.connection_node = Blocks[i].arcs_in_signalling_block_section[k].endNode;
					break;
				}
			}
			// Defining Node on switch for the second TDS on the mid point of the switch
			// That Node is equal to the Node on switch of TDS1
			TDS2.node_on_switch = TDS1.node_on_switch;

			// Defining other two TD sections on the straight blocks which are connected in this block with diverging switch
			TDS TDS3, TDS4;
			list<TDS>::iterator TDS3Pointer, TDS4Pointer;

			bool IsFirstBlockFound = false, IsSecondBlockFound = false;

			// Redefining IDFirstBlock and IDSecondBlock to add "@" at teh beginning and the end of the name
			string FullIDFirstBlock, FullIDSecondBlock;
			FullIDFirstBlock = "@" + IDFirstBlock + "@";
			FullIDSecondBlock = "@" + IDSecondBlock + "@";

			// Defining the Node of the TDS on the straight block sections which are connected by the diverging switch
			for (int z = 0; z < N_Blocks; z++) {
				// Defining the second TDS on the first connected straight block section
				if (Blocks[z].ID == FullIDFirstBlock) {
					TDS1.end_node.X = TDS1.node_on_switch.X;

					TDS1.end_node.tdsbId = "End_" + TDS1.ID;
					TDS1.end_node.tdsbGeoCoordX = TDS1.end_node.X * 1000;
					TDS1.TracklineID = Blocks[z].trackLineId;
					TDS1.length = TDS1.end_node.X - TDS1.start_node.X;

					// Defining TDS3 which is the end TDS of the first connected straight block section
					TDS3.ID = "TDS_2@" + IDFirstBlock;
					TDS3.start_node = TDS1.end_node;
					TDS3.end_node = Blocks[z].end_node;

					TDS3.TracklineID = Blocks[z].trackLineId;
					TDS3.length = TDS3.end_node.X - TDS3.start_node.X;

					// Allocate the produced TDS in the list of ALL_TDS
					ALL_TDS.push_back(TDS1);

					TDS1Pointer = ALL_TDS.end();
					TDS1Pointer--;
					Blocks[z].TDS_in_block.push_back(&(*TDS1Pointer));

					ALL_TDS.push_back(TDS3);

					TDS3Pointer = ALL_TDS.end();
					TDS3Pointer--;

					// Specifying belonging of TDS to first connected straight block section

					Blocks[z].TDS_in_block.push_back(&(*TDS3Pointer));

					IsFirstBlockFound = true;
				}

				if (Blocks[z].ID == FullIDSecondBlock) {
					// Defining TDS4 which is the first TDS of the second connected straight block section
					TDS4.ID = "TDS_1@" + IDSecondBlock;
					TDS4.start_node = Blocks[z].start_node;
					TDS4.end_node.X = TDS2.node_on_switch.X;
					TDS4.end_node.tdsbId = "End_" + TDS4.ID;
					TDS4.end_node.tdsbGeoCoordX = TDS4.end_node.X * 1000;

					TDS4.TracklineID = Blocks[z].trackLineId;
					TDS4.length = TDS4.end_node.X - TDS4.start_node.X;

					// Defining the start Node of the second TDS on the second cnnected straight block section
					TDS2.start_node = TDS4.end_node;

					TDS2.TracklineID = Blocks[z].trackLineId;
					TDS2.length = TDS2.end_node.X - TDS2.start_node.X;

					// Allocate the produced TDS in the list of ALL_TDS
					ALL_TDS.push_back(TDS4);

					TDS4Pointer = ALL_TDS.end();
					TDS4Pointer--;
					Blocks[z].TDS_in_block.push_back(&(*TDS4Pointer));

					ALL_TDS.push_back(TDS2);

					TDS2Pointer = ALL_TDS.end();
					TDS2Pointer--;

					// Specifying belonging of TDS to first connected straight block section
					Blocks[z].TDS_in_block.push_back(&(*TDS2Pointer));

					IsSecondBlockFound = true;
				}

				if ((IsFirstBlockFound == 1) && (IsSecondBlockFound == 1)) {
					break; // if both the firdt and the second straight block sections are found then break the for loop on the block sections
				}
			}

			// Specifying belonging of TDS to block section with diverging switch
			if (IsFirstBlockFound)
				Blocks[i].TDS_in_block.push_back(&(*TDS1Pointer));
			if (IsSecondBlockFound)
				Blocks[i].TDS_in_block.push_back(&(*TDS2Pointer));
		}
	}
	// Now setting the TDS for all the block sections which are left, i.e. those which are not connected to any diverging switch

	for (int i = 0; i < N_Blocks; i++) {
		// If the block section has not a diverging switch or not one of those sections connected by a switch (which in this part of the code means those sections for which a list of TDS is still empty)
		// Then we can divide the section in as many TDS that are desired / necessary
		// The number of TDS in which we would like to split the block section should be given as an input to the function
		// The edge/border of a TDS should not overlap with switches or stopping boards at stations or other infrastructure elements different than simple edges of arcs denoting changes in speed limit or gradient or curvature radii
		if ((Blocks[i].withSwitchDiv == 0) && (Blocks[i].TDS_in_block.empty() == 1)) {

			// Getting the name of the block section
			string BlockName;
			BlockName = Blocks[i].ID;
			istringstream Line(BlockName);
			list<string> TokenA;
			string tok;

			string IDBlock;

			while (getline(Line, tok, '@')) {
				if (tok.size() > 0)
					TokenA.push_back(tok);
			}

			list<string>::iterator t = TokenA.begin();
			IDBlock = *t;

			TDS TDSset[20]; // Creating an array of 20 TDS. The number 20 should suffice for any case study as a block sction could at max have 4 to 5 TDS sections

			// Defining an equal length for each TDS in the block
			double TDS_Equi_Length = Blocks[i].length / N_TDS_On_Straight_Blocks;

			// defining the first TDS on the block
			TDSset[0].ID = "TDS_1@" + IDBlock;
			TDSset[0].start_node = Blocks[i].start_node;
			TDSset[0].end_node.X = TDSset[0].start_node.X + TDS_Equi_Length;
			TDSset[0].end_node.tdsbGeoCoordX = TDSset[0].end_node.X * 1000;
			TDSset[0].end_node.tdsbId = "End_" + TDSset[0].ID;
			TDSset[0].TracklineID = Blocks[i].trackLineId;
			TDSset[0].length = TDSset[0].end_node.X - TDSset[0].start_node.X;

			// if N_TDS_On_Straight_Blocks =1 then we need to put that the last Node of the TDS is actually the last Node of the block section
			if (N_TDS_On_Straight_Blocks == 1) {
				TDSset[0].end_node = Blocks[i].end_node;
				TDSset[0].length = TDSset[0].end_node.X - TDSset[0].start_node.X;

			}

			// If there are more than 1 TDS in a block then initialise all the TDS
			else if (N_TDS_On_Straight_Blocks > 1) {

				for (int d = 1; d < N_TDS_On_Straight_Blocks; d++) {
					TDSset[d].ID = "TDS_" + to_string((d + 1)) + "@" + IDBlock;

					TDSset[d].start_node = TDSset[d - 1].end_node;

					TDSset[d].end_node.X = TDSset[d].start_node.X + TDS_Equi_Length;
					TDSset[d].end_node.tdsbGeoCoordX = TDSset[d].end_node.X * 1000;
					TDSset[d].end_node.tdsbId = "End_" + TDSset[d].ID;

					// in case the TDS is the last of the block section then its end Node should coincide with the end Node of the block section.
					if (d == N_TDS_On_Straight_Blocks - 1) {
						TDSset[d].end_node = Blocks[i].end_node;
					}

					TDSset[d].TracklineID = Blocks[i].trackLineId;
					TDSset[d].length = TDSset[d].end_node.X - TDSset[d].start_node.X;
				}
			}

			// Allocating the TDS in the block section and in the list of all TDS
			for (int k = 0; k < N_TDS_On_Straight_Blocks; k++) {

				ALL_TDS.push_back(TDSset[k]);

				list<TDS>::iterator P = ALL_TDS.end();
				P--;

				Blocks[i].TDS_in_block.push_back(&(*P));
			}
		}
	}
}

// Function to set GeoCoordinates to all the Block Sections
void setGeoCoordinates(Section* BS, int N_BS) {
	for (int i = 0; i < N_BS; i++) {
		BS[i].GeoXBegNode = BS[i].start_node.X * 1000;
		BS[i].GeoXEndNode = BS[i].end_node.X * 1000;
	}
}

// Print all the ID of Block Sections in a text file (This is useful to create Routes easily)
void printAllBlocksId() {
	ofstream BlocksID;
	string BlocksIDName;
	BlocksIDName = BlocksIDName + InputMainFolder + "/Routes/List_of_Blocks_IDs.txt";
	BlocksID.open((char*)BlocksIDName.c_str());
	BlocksID << "BlockID StartX[Km] EndX[Km]\n";
	for (int i = 0; i < Blocks; i++) {
		BlocksID << signalling_block_sections[i].ID << " " << signalling_block_sections[i].start_node.X << " " << signalling_block_sections[i].end_node.X << "\n";
	}
	BlocksID.close();
}

// Print all TDS and Blocks for RECIFE-MILP input
void printAllBlocksForRecife() {
	ofstream RecifeBlocksID;
	string BlocksIDName;
	BlocksIDName = BlocksIDName + InputMainFolder + "/Tracklines/RECIFE_input.txt";
	RecifeBlocksID.open((char*)BlocksIDName.c_str());
	RecifeBlocksID << "Trackline \t BlockID  \t EntrySignal \t ExitSignal \t BlockStartX[Km] \tBlockEndX[Km] \t topologypartID \t TopoPartStartX[Km] \tTopoPartEndX[Km] \t length \t gradient \t curve \t speedlimit\n";

	for (int i = 0; i < Blocks; i++) {
		std::string line1 = "";
		std::string line2 = "";
		std::string track = std::to_string(signalling_block_sections[i].trackLineId);
		line1 += track + "\t" + signalling_block_sections[i].ID + "\t";

		std::string X_signal1 = std::to_string(signalling_block_sections[i].start_node.X);
		std::string X_signal2 = std::to_string(signalling_block_sections[i].end_node.X);

		if (std::stoi(track) >= 0) {
			line1 += "Sign_" + X_signal1.substr(0, X_signal1.size() - 3) + "_tr_" + track + "\t" +
					 "Sign_" + X_signal2.substr(0, X_signal2.size() - 3) + "_tr_" + track;
		} else {
			std::string left = signalling_block_sections[i].ID.substr(0, signalling_block_sections[i].ID.find("/"));
			left = left.substr(0, left.find("@-"));
			left = left.substr(left.find("-B") + 2); // we got track 1
			std::string right = signalling_block_sections[i].ID.substr(signalling_block_sections[i].ID.find("/"));
			// std::cout << right;
			right = right.substr(0, right.find("@-"));

			right = right.substr(right.find("-B") + 2); // we got track 2

			line1 += "Sign_" + X_signal1.substr(0, X_signal1.size() - 3) + "_tr_" + left + "\t" +
					 "Sign_" + X_signal2.substr(0, X_signal2.size() - 3) + "_tr_" + right;
		}

		line1 += "\t" + std::to_string(signalling_block_sections[i].start_node.X) + "\t" +
				 std::to_string(signalling_block_sections[i].end_node.X) + "\t";

		for (int j = 0; j < signalling_block_sections[i].total_arcs; j++) {

			if (signalling_block_sections[i].ID.find("/") != std::string::npos) {
				// if it is a block that connects two lines
				std::string left = signalling_block_sections[i].ID.substr(0, signalling_block_sections[i].ID.find("/"));
				std::string right = signalling_block_sections[i].ID.substr(signalling_block_sections[i].ID.find("/"));
				// line2 += "left=" + left + "right=" + right;
				line2 += left.substr(0, left.find("@-") + 1) + "-";
				line2 += right.substr(1, right.find("@-")) + "-" + std::to_string(j) + "\t";
			} else
				line2 += signalling_block_sections[i].ID + "-" + std::to_string(j) + "\t";

			line2 +=
				std::to_string(signalling_block_sections[i].arcs_in_signalling_block_section[j].startNode.X) + "\t" +
				std::to_string(signalling_block_sections[i].arcs_in_signalling_block_section[j].endNode.X) + "\t" +
				std::to_string(signalling_block_sections[i].arcs_in_signalling_block_section[j].length) + "\t" +
				std::to_string(signalling_block_sections[i].arcs_in_signalling_block_section[j].gradient) + "\t" +
				std::to_string(signalling_block_sections[i].arcs_in_signalling_block_section[j].curvature) + "\t" +
				std::to_string(signalling_block_sections[i].arcs_in_signalling_block_section[j].speedLimit);
			RecifeBlocksID << line1 << line2 << "\n";
			line2 = "";
		}
	}
}

// Functions to Set block Sections in consecutive order: this function returns true if the second block section has the start Node coinciding with the previous one
bool orderBlocks(Section BS1, Section BS2) {
	if (BS1.end_node.X <= BS2.start_node.X)
		return true;
	else
		return false;
}

// Function to order the block section in the reverse order
bool reverseOrderBlocks(Section BS1, Section BS2) {
	if (BS1.start_node.X >= BS2.end_node.X)
		return true;
	else
		return false;
}

int N_Routes = 0; // Total number of routes: it is given by the total number of input file in the folder Routes

Route::Route() {
	ID = "None";
	N_Block_Sections = 0;
	x_of_start_node = 0;
	x_of_end_node = 0;
	reversed_direction = false;
	OriginalRefReversedRoute = -1;
	// The list of Infrastructure Elements is empty by default.

	corridor = 'A'; // default corridor
	diffRegionsJumpX = {0, 0};
}

//  Here I need to define the function to initialise the list of InfraElements the function will be called Set_ListOFInfrastructureElements(Section signalling_block_sections)
// It is important that in such a function any infrastructure element has also the name of the block section ID the element belongs to, this is useful then when computing train blocking times block by block
void Route::setListInfrastructureElementsForRoute(Section BS) {
	list<InfraElement> TEMP_ElementList; // TEMP_ElementList is a temporary List where we collect all the infrastructure elements within a given block section
	int N_TEMP_ElementList = 0;

	if (BS.start_node.tdsbId.empty() != 1) { // if the TDSB of the starting Node of signalling_block_sections is not empty then use it as infra element
		InfraElement TEMP_Elem;				  // Temporary infrastructure element to be intialised

		TEMP_Elem.ID = BS.start_node.tdsbId;
		TEMP_Elem.SectionID = BS.ID;
		TEMP_Elem.IsTrackDetSecBorder = true;
		TEMP_Elem.XCoordinate = BS.start_node.X * 1000;
		TEMP_Elem.YCoordinate = BS.start_node.Y * 1000;
		TEMP_Elem.GeoXCoord = BS.start_node.tdsbGeoCoordX;
		TEMP_Elem.GeoYCoord = BS.start_node.tdsbGeoCoordY;
		// Is the location a station or a switch?
		if (BS.start_node.stationName.empty() != 1) {
			TEMP_Elem.isStation = true;
			TEMP_Elem.stationName = BS.start_node.stationName;
		}
		if (BS.start_node.numConnections > 0) {
			TEMP_Elem.IsSwitch = true;
			TEMP_Elem.SwitchName = BS.start_node.tdsbId;
			if (BS.withSwitchDiv == 1) {
				if ((BS.start_node.X == BS.XStartSwitch) || (BS.start_node.X == BS.XEndSwitch)) {
					TEMP_Elem.withSwitchDiv = true;

					// Setting the name of the Switch
					istringstream Line(BS.start_node.tdsbId);
					list<string> TokenA;
					string tok;
					// Splitting its name in tokens separated by the charachter "@"
					int N_validTokInA = 0; // Number of valid tok in TokenA
					while (getline(Line, tok, '@')) {
						if (tok.size() > 0) {
							TokenA.push_back(tok);
							N_validTokInA++; // Block Sections with A diverging Switch should have two elements in the list tok in A
						}
					}
					if (TokenA.size() == 4) { // if the name of the deviating switch has at least for characters divided by the symbol @
						list<string>::iterator FirstSwitchName, SecondSwitchName, SwitchPosition;
						SwitchPosition = TokenA.end();
						SwitchPosition--; // SwitchPosition identifies if the edge of the diverging switch is a Point-Start or a Poiunt-End
						SecondSwitchName = TokenA.end();
						advance(SecondSwitchName, -2);	  // This one represents the name of the second edge of the diverging switch
						FirstSwitchName = TokenA.begin(); // This is the name of the first edge of the diverging switch
						if (*SwitchPosition == "Point-End") {
							string SWName;
							SWName = SWName + "@" + *SecondSwitchName + "@Point";
							TEMP_Elem.SwitchName = SWName;
						} else if (*SwitchPosition == "Point-Start") {
							string SWName;
							SWName = SWName + "@" + *FirstSwitchName + "@Point";
							TEMP_Elem.SwitchName = SWName;
						}
					} else {
						cout << "Warning: Switch at starting Node: " << BS.start_node.tdsbId << " on Block Section " << BS.ID << "for Route " << this->ID << " that has reversed = " << this->reversed_direction << " is a diverging switch with less than 4 characters in its name. Please check why...\n";
					}
				} else {
					TEMP_Elem.SwitchName = TEMP_Elem.ID; // if the switch is on a Block Section with a Diverging Switch but theis switch is different from the diverging switch then assign its ID to its SwitchName
				}
			} else {
				TEMP_Elem.SwitchName = TEMP_Elem.ID; // if the switch is not diverging its ID coincides with the SwitchName
			}
		}

		// Increase the locations of 1 element if it is not already in the list
		if (N_TEMP_ElementList == 0) {
			N_TEMP_ElementList++;
			TEMP_ElementList.push_back(TEMP_Elem);
			list<InfraElement>::iterator it = TEMP_ElementList.begin();

		} else {
			bool IsAlreadyThere = false;
			for (list<InfraElement>::iterator l = TEMP_ElementList.begin(); l != TEMP_ElementList.end(); l++) {
				if ((TEMP_Elem.ID == l->ID) || (TEMP_Elem.GeoXCoord == l->GeoXCoord)) { // if they have the same ID or the same position
					IsAlreadyThere = true;
					break;
				}
			}
			if (IsAlreadyThere == 0) {
				N_TEMP_ElementList++;
				TEMP_ElementList.push_back(TEMP_Elem);
			}
		}
	}

	// Now Add other TDSB at station platforms and switches
	for (int m = 0; m < BS.total_arcs; m++) {
		if (this->reversed_direction == 0) {
			// The arcs of block sections belonging to Routes that are not reversed have the stationName and points in the end_node of some of their Arc
			if ((BS.arcs_in_signalling_block_section[m].endNode.tdsbId.empty() != 1) && (BS.arcs_in_signalling_block_section[m].endNode.tdsbGeoCoordX != -1)) {

				InfraElement TEMP_Elem;
				TEMP_Elem.ID = BS.arcs_in_signalling_block_section[m].endNode.tdsbId;

				TEMP_Elem.SectionID = BS.ID;
				TEMP_Elem.XCoordinate = BS.arcs_in_signalling_block_section[m].endNode.X * 1000;
				TEMP_Elem.YCoordinate = BS.arcs_in_signalling_block_section[m].endNode.Y * 1000;
				TEMP_Elem.GeoXCoord = BS.arcs_in_signalling_block_section[m].endNode.tdsbGeoCoordX;
				TEMP_Elem.GeoYCoord = BS.arcs_in_signalling_block_section[m].endNode.tdsbGeoCoordY;

				// Setting the infra Element as a Track Detection Section Border is this if the last Node of the Block Section
				if (BS.arcs_in_signalling_block_section[m].endNode.X == BS.end_node.X) {
					TEMP_Elem.IsTrackDetSecBorder = true;
				}

				if (BS.arcs_in_signalling_block_section[m].endNode.stationName.empty() != 1) {
					TEMP_Elem.isStation = true;
					TEMP_Elem.stationName = BS.arcs_in_signalling_block_section[m].endNode.stationName;
				}

				if (BS.arcs_in_signalling_block_section[m].endNode.numConnections > 0) {
					TEMP_Elem.IsSwitch = true;
					if (BS.withSwitchDiv == 1) {
						if ((BS.arcs_in_signalling_block_section[m].endNode.X == BS.XStartSwitch) || (BS.arcs_in_signalling_block_section[m].endNode.X == BS.XEndSwitch)) {
							TEMP_Elem.withSwitchDiv = true;
							// Setting the name of the Switch
							istringstream Line(BS.arcs_in_signalling_block_section[m].endNode.tdsbId);
							list<string> TokenA;
							string tok;
							// Splitting its name in tokens separated by the charachter "@"
							int N_validTokInA = 0; // Number of valid tok in TokenA
							while (getline(Line, tok, '@')) {
								if (tok.size() > 0) {
									TokenA.push_back(tok);
									N_validTokInA++; // Block Sections with A diverging Switch should have two elements in the list tok in A
								}
							}
							if (TokenA.size() == 4) { // if the name of the deviating switch has at least for characters divided by the symbol @
								list<string>::iterator FirstSwitchName, SecondSwitchName, SwitchPosition;
								SwitchPosition = TokenA.end();
								SwitchPosition--; // SwitchPosition identifies if the edge of the diverging switch is a Point-Start or a Poiunt-End
								SecondSwitchName = TokenA.end();
								advance(SecondSwitchName, -2);	  // This one represents the name of the second edge of the diverging switch
								FirstSwitchName = TokenA.begin(); // This is the name of the first edge of the diverging switch
								if (*SwitchPosition == "Point-End") {
									string SWName;
									SWName = SWName + "@" + *SecondSwitchName + "@Point";
									TEMP_Elem.SwitchName = SWName;
								} else if (*SwitchPosition == "Point-Start") {
									string SWName;
									SWName = SWName + "@" + *FirstSwitchName + "@Point";
									TEMP_Elem.SwitchName = SWName;
								}
							} else {
								std::cout << "Warning: Switch at end of one of the arcs: " << BS.arcs_in_signalling_block_section[m].endNode.tdsbId << " on Block Section " << BS.ID << "for Route" << this->ID << "that has reversed = " << this->reversed_direction << " is a diverging switch with less than 4 characters in its name. Please check why...\n"
																																																																	 " is a diverging switch with less than 4 characters in its name. Please check why...\n";
							}

						} else {
							TEMP_Elem.SwitchName = TEMP_Elem.ID; // if the switch is on a Block Section with a Diverging Switch but theis switch is different from the diverging switch then assign its ID to its SwitchName
						}

					} else {
						TEMP_Elem.SwitchName = TEMP_Elem.ID; // if the switch is not diverging its ID coincides with the SwitchName
					}
				}

				// Increase the locations of 1 element if it is not already in the list
				if (N_TEMP_ElementList == 0) {
					N_TEMP_ElementList++;
					TEMP_ElementList.push_back(TEMP_Elem);
				} else {
					bool IsAlreadyThere = false;
					for (list<InfraElement>::iterator l = TEMP_ElementList.begin(); l != TEMP_ElementList.end(); l++) {
						if ((TEMP_Elem.ID == l->ID) || (TEMP_Elem.GeoXCoord == l->GeoXCoord)) {
							IsAlreadyThere = true;
							break;
						}
					}
					if (IsAlreadyThere == 0) {
						N_TEMP_ElementList++;
						TEMP_ElementList.push_back(TEMP_Elem);
					}
				}
			}
		}

		if (this->reversed_direction == 1) {
			// The arcs of block sections belonging to Routes that are reversed have the stationName in the start_node of some of their Arc
			if ((BS.arcs_in_signalling_block_section[m].startNode.tdsbId.empty() != 1) && (BS.arcs_in_signalling_block_section[m].startNode.tdsbGeoCoordX != -1)) {

				InfraElement TEMP_Elem;

				TEMP_Elem.ID = BS.arcs_in_signalling_block_section[m].startNode.tdsbId;

				TEMP_Elem.SectionID = BS.ID;
				TEMP_Elem.XCoordinate = BS.arcs_in_signalling_block_section[m].startNode.X * 1000;
				TEMP_Elem.YCoordinate = BS.arcs_in_signalling_block_section[m].startNode.Y * 1000;
				TEMP_Elem.GeoXCoord = BS.arcs_in_signalling_block_section[m].startNode.tdsbGeoCoordX;
				TEMP_Elem.GeoYCoord = BS.arcs_in_signalling_block_section[m].startNode.tdsbGeoCoordY;

				// Setting the infra Element as a Track Detection Section Border is this if the last Node of the Block Section
				if (BS.arcs_in_signalling_block_section[m].startNode.X == BS.start_node.X) {
					TEMP_Elem.IsTrackDetSecBorder = true;
				}

				if (BS.arcs_in_signalling_block_section[m].startNode.stationName.empty() != 1) {
					TEMP_Elem.isStation = true;
					TEMP_Elem.stationName = BS.arcs_in_signalling_block_section[m].startNode.stationName;
				}

				if (BS.arcs_in_signalling_block_section[m].startNode.numConnections > 0) {
					TEMP_Elem.IsSwitch = true;
					if (BS.withSwitchDiv == 1) {
						if ((BS.arcs_in_signalling_block_section[m].startNode.X == BS.XStartSwitch) || (BS.arcs_in_signalling_block_section[m].startNode.X == BS.XEndSwitch)) {
							TEMP_Elem.withSwitchDiv = true;
							// Setting the name of the Switch
							istringstream Line(BS.arcs_in_signalling_block_section[m].startNode.tdsbId);
							list<string> TokenA;
							string tok;
							// Splitting its name in tokens separated by the charachter "@"
							int N_validTokInA = 0; // Number of valid tok in TokenA
							while (getline(Line, tok, '@')) {
								if (tok.size() > 0) {
									TokenA.push_back(tok);
									N_validTokInA++; // Block Sections with A diverging Switch should have two elements in the list tok in A
								}
							}
							if (TokenA.size() == 4) { // if the name of the deviating switch has at least for characters divided by the symbol @
								list<string>::iterator FirstSwitchName, SecondSwitchName, SwitchPosition;
								SwitchPosition = TokenA.end();
								SwitchPosition--; // SwitchPosition identifies if the edge of the diverging switch is a Point-Start or a Poiunt-End
								SecondSwitchName = TokenA.end();
								advance(SecondSwitchName, -2);	  // This one represents the name of the second edge of the diverging switch
								FirstSwitchName = TokenA.begin(); // This is the name of the first edge of the diverging switch
								if (*SwitchPosition == "Point-End") {
									string SWName;
									SWName = SWName + "@" + *SecondSwitchName + "@Point";
									TEMP_Elem.SwitchName = SWName;
								} else if (*SwitchPosition == "Point-Start") {
									string SWName;
									SWName = SWName + "@" + *FirstSwitchName + "@Point";
									TEMP_Elem.SwitchName = SWName;
								}
							} else {
								cout << "Warning: Switch at one of the beginning nodes: " << BS.arcs_in_signalling_block_section[m].startNode.tdsbId << " on Block Section " << BS.ID << "for Route" << this->ID << "that has reversed = " << this->reversed_direction << " is a diverging switch with less than 4 characters in its name. Please check why...\n"
																																																																	" is a diverging switch with less than 4 characters in its name. Please check why...\n";
							}
						}

						else {
							TEMP_Elem.SwitchName = TEMP_Elem.ID; // if the switch is on a Block Section with a Diverging Switch but theis switch is different from the diverging switch then assign its ID to its SwitchName
						}

					} else {
						TEMP_Elem.SwitchName = TEMP_Elem.ID; // if the switch is not diverging its ID coincides with the SwitchName
					}
				}

				// Increase the locations of 1 element if it is not already in the list
				if (N_TEMP_ElementList == 0) {
					N_TEMP_ElementList++;
					TEMP_ElementList.push_back(TEMP_Elem);
				} else {
					bool IsAlreadyThere = false;
					for (list<InfraElement>::iterator l = TEMP_ElementList.begin(); l != TEMP_ElementList.end(); l++) {
						if ((TEMP_Elem.ID == l->ID) || (TEMP_Elem.GeoXCoord == l->GeoXCoord)) {
							IsAlreadyThere = true;
							break;
						}
					}
					if (IsAlreadyThere == 0) {
						N_TEMP_ElementList++;
						TEMP_ElementList.push_back(TEMP_Elem);
					}
				}
			}
		}
	}

	// Do the same thing for the end TDSB
	if (BS.end_node.tdsbId.empty() != 1) {
		InfraElement TEMP_Elem;

		TEMP_Elem.ID = BS.end_node.tdsbId;
		TEMP_Elem.SectionID = BS.ID;
		TEMP_Elem.IsTrackDetSecBorder = true;
		TEMP_Elem.XCoordinate = BS.end_node.X * 1000;
		TEMP_Elem.YCoordinate = BS.end_node.Y * 1000;
		TEMP_Elem.GeoXCoord = BS.end_node.tdsbGeoCoordX;
		TEMP_Elem.GeoYCoord = BS.end_node.tdsbGeoCoordY;

		// Is the location a station or a switch?
		if (BS.end_node.stationName.empty() != 1) {
			TEMP_Elem.isStation = true;
			TEMP_Elem.stationName = BS.end_node.stationName;
		}
		if (BS.end_node.numConnections > 0) {
			TEMP_Elem.IsSwitch = true;
			if (BS.withSwitchDiv == 1) {
				if ((BS.end_node.X == BS.XStartSwitch) || (BS.end_node.X == BS.XEndSwitch)) {
					TEMP_Elem.withSwitchDiv = true;
					// Setting the name of the Switch
					istringstream Line(BS.end_node.tdsbId);
					list<string> TokenA;
					string tok;
					// Splitting its name in tokens separated by the charachter "@"
					int N_validTokInA = 0; // Number of valid tok in TokenA
					while (getline(Line, tok, '@')) {
						if (tok.size() > 0) {
							TokenA.push_back(tok);
							N_validTokInA++; // Block Sections with A diverging Switch should have two elements in the list tok in A
						}
					}
					if (TokenA.size() == 4) { // if the name of the deviating switch has at least for characters divided by the symbol @
						list<string>::iterator FirstSwitchName, SecondSwitchName, SwitchPosition;
						SwitchPosition = TokenA.end();
						SwitchPosition--; // SwitchPosition identifies if the edge of the diverging switch is a Point-Start or a Poiunt-End
						SecondSwitchName = TokenA.end();
						advance(SecondSwitchName, -2);
						;								  // This one represents the name of the second edge of the diverging switch
						FirstSwitchName = TokenA.begin(); // This is the name of the first edge of the diverging switch
						if (*SwitchPosition == "Point-End") {
							string SWName;
							SWName = SWName + "@" + *SecondSwitchName + "@Point";
							TEMP_Elem.SwitchName = SWName;
						} else if (*SwitchPosition == "Point-Start") {
							string SWName;
							SWName = SWName + "@" + *FirstSwitchName + "@Point";
							TEMP_Elem.SwitchName = SWName;
						}
					} else {
						cout << "Warning: Switch at ending Node: " << BS.end_node.tdsbId << " on Block Section " << BS.ID << "for Route" << this->ID << "that has reversed = " << this->reversed_direction << " is a diverging switch with less than 4 characters in its name. Please check why...\n"
																																																			   " is a diverging switch with less than 4 characters in its name. Please check why...\n";
					}

				}

				else {
					TEMP_Elem.SwitchName = TEMP_Elem.ID; // if the switch is on a Block Section with a Diverging Switch but theis switch is different from the diverging switch then assign its ID to its SwitchName
				}
			} else {
				TEMP_Elem.SwitchName = TEMP_Elem.ID; // if the switch is not diverging its ID coincides with the SwitchName
			}
		}

		// Increase the locations of 1 element if it is not already in the list
		if (N_TEMP_ElementList == 0) {
			N_TEMP_ElementList++;
			TEMP_ElementList.push_back(TEMP_Elem);
		} else {
			bool IsAlreadyThere = false;
			for (list<InfraElement>::iterator l = TEMP_ElementList.begin(); l != TEMP_ElementList.end(); l++) {
				if ((TEMP_Elem.ID == l->ID) || (TEMP_Elem.GeoXCoord == l->GeoXCoord)) {
					IsAlreadyThere = true;
					break;
				}
			}
			if (IsAlreadyThere == 0) {
				N_TEMP_ElementList++;
				TEMP_ElementList.push_back(TEMP_Elem);
			}
		}
	}

	// Defining connected points to InfraElement which are the start and End Point of the same diverging switch
	if (BS.withSwitchDiv == 1) {		   // if block Section signalling_block_sections has a diverging switch
		if (TEMP_ElementList.size() > 0) { // If the temporary list of infra Element of Block Section signalling_block_sections has at least one element then look for the two edges of the same diverging switch to assign the connectpoint variable
			for (list<InfraElement>::iterator t = TEMP_ElementList.begin(); t != TEMP_ElementList.end(); t++) {
				if (t->withSwitchDiv == 1) { // if this is an edge of a diverging switch find the other one and connect their name reciprocally
					for (list<InfraElement>::iterator u = TEMP_ElementList.begin(); u != TEMP_ElementList.end(); u++) {
						if ((u->withSwitchDiv == 1) && (u->ID != t->ID)) { // if u is the edge of a diverging switch and it is different from the other edge t, then assign the connect point of t to u and of u to t
							// u->ConnectedPoint = t->ID;
							// u->XConnectedPoint = t->XCoordinate;
							t->ConnectedPoint = u->ID;
							t->XConnectedPoint = u->XCoordinate;
						}
					}
				}
			}
		}
	}

	// Defining the FinalLocationList
	for (list<InfraElement>::iterator y = TEMP_ElementList.begin(); y != TEMP_ElementList.end(); y++) {
		bool IsAlreadyInRouteInfaElem = false;
		if (this->InfrastructureElements.size() > 0) {
			for (list<InfraElement>::iterator h = this->InfrastructureElements.begin(); h != this->InfrastructureElements.end(); h++) {
				if ((y->IsSwitch == 1) && (y->withSwitchDiv == 1)) { // if y in the list of TEMP Elem is a diverging switch
																	 // IsAlready There becomes true only if two switches have the same ID, switch name, geopositionX and if they are connected to the same switch edge. In case even one of this conditions is not true then the Node shall be included
					if ((y->ID == h->ID) && (y->SwitchName == h->SwitchName) && (y->ConnectedPoint == h->ConnectedPoint) && (y->GeoXCoord == y->GeoXCoord)) {
						IsAlreadyInRouteInfaElem = true;
						break;
					}
				} else { // For all the other elements which are not diverging switches IsAlreadyInRouteInfraElement becoems true when the elements have the same ID or the same position
					if ((y->ID == h->ID) || (y->GeoXCoord == h->GeoXCoord)) {
						IsAlreadyInRouteInfaElem = true;
						break; // break the for loop over the infrastructure elements
					}
				}
			}
		} else { // if the Infrastructure elements is empty then leave the bool IsAlreadyInBlockTime to its false value
		}

		// Only if the element y is not already in the Infrastructure Elements List of this Route then add it to the InfrastructureElements list
		if (IsAlreadyInRouteInfaElem == 0) {
			this->InfrastructureElements.push_back(*y);
		}
	}
}

// Function to identify ending edges of diverging switches which concide with the beginning of other diverging switches on the same route, so if it happens something like in the picture  o-----oo----o (where dotted lines represent diverging switches)
void Route::identifyEndingEdgeOfDivSwitchesWhichAreStartingOfADivSwitch(list<InfraElement>& RouteInfrastructureElements) {
	for (list<InfraElement>::iterator it = RouteInfrastructureElements.begin(); it != RouteInfrastructureElements.end(); it++) {
		if ((it->IsSwitch == 1) && (it->withSwitchDiv == 1)) {
			for (list<InfraElement>::iterator u = RouteInfrastructureElements.begin(); u != RouteInfrastructureElements.end(); u++) {
				if ((u->IsSwitch == 1) && (u->withSwitchDiv == 1)) {
					if ((u->XCoordinate == it->XCoordinate) && (u->SwitchName == it->SwitchName) && (u->ConnectedPoint != it->ConnectedPoint)) { // if it and u are the same Diverging Switch Point but they are connected to two different Points in the infrastructure, then this means that the End of Switch "it" is the beginning of another diverging switch
						it->isEndOfDivSwitchStartOfADivSwitch = true;
						break; // you can then break the for loop of u over the RouteInfrastructureElements
					}
				}
			}
		}
	}
}

// This Function Load the File of the Route and create the Route
void Route::createRoute(char* FileName) {
	int N_BLOCKSECTIONS = 1000;
	N_Block_Sections = 0; // NumBlocksinB is the number of block section in Path blockSets
	string* Matr;		  // Defining the matrix which will collect the data from the file FileName

	Matr = new string[N_BLOCKSECTIONS]; // Allocating Matr dynamically

	/*cout<<"Insert the path of text file with Block Section Characteristics [Position, Length(m)]: ";  cin.get(blockname,99);*/
	ifstream blockinput;
	blockinput.open(FileName, ios::binary);
	if (!blockinput) {
		cout << "Error[createRoute]:impossible to open the file: " << FileName << endl;
	}

	while (blockinput) {
		blockinput >> Matr[N_Block_Sections];
		N_Block_Sections++;
	}
	N_Block_Sections = N_Block_Sections - 1;

	blockinput.close(); // Closing Block Section Data File
						// cin.ignore();

	list<Section> BlockList;

	for (int j = 0; j < N_Block_Sections; j++) {
		for (int i = 0; i < Blocks; i++) {
			if (signalling_block_sections[i].ID == Matr[j]) {
				BlockList.push_back(signalling_block_sections[i]);
				break;
			} // push back Block Section element in the list
		}
	}

	// Some route tokens may not resolve to a generated block section (e.g. a
	// switch-transition entry with no matching merged block). Keep the route
	// length in step with the blocks actually collected so the copy loops below
	// never advance the iterator past the end of the list.
	if ((int)BlockList.size() < N_Block_Sections)
		N_Block_Sections = (int)BlockList.size();

	// Checking if the Blocks must be ordered progressively or in the reverse order. This will depend of course on which direction has been defined the route with respect to the definition of the TrackLine
	list<Section>::iterator start, final; // Two pointers pointing at the beginning and the end of the BlockList
	start = BlockList.begin();
	final = BlockList.end();
	final--;

	if (start->start_node.X < final->start_node.X) { // if the start point of the Route is lower than the final point of the route
		// Order the list in progressive order
		BlockList.sort(orderBlocks);

		list<Section>::iterator it = BlockList.begin();

		if (BlockList.empty() != 1) { // if the list is not empty fill in the vector sequence_of_block_sections
			for (int i = 0; i < N_Block_Sections; i++) {

				// Check if the block sections are overlapping
				if (i < N_Block_Sections - 1) {
					list<Section>::iterator p = it;
					p++; // this iterator points at the element after it in the list BlockList
					bool AreBlocksOverlapped = areBlocksConnected(*it, *p);
					if ((AreBlocksOverlapped == 1) && (it->withSwitchDiv == 1) && (p->withSwitchDiv == 1)) { // if the two blocks are somewhat overlapped then cut the end of it and the begin of p if they have switches
						double CuttingPosition = 0;
						CuttingPosition = (p->XStartSwitch - it->XEndSwitch) / 2 + it->XEndSwitch; // The cutting position is in the middle between the end of the switch in it and the beginning of the switch in p
																								   // Now cut the blocks
						it->CutBlockSection("CutEnd", CuttingPosition);	 // Cutting the end of it
						p->CutBlockSection("CutBegin", CuttingPosition); // Cutting the beginning of p
					}
				}
				sequence_of_block_sections[i] = *it;
				// Set the list of infrastructure elements of the route
				this->setListInfrastructureElementsForRoute(sequence_of_block_sections[i]);

				it++;
			}
		}
	}

	else {						   // Otherwise if the start point is higher than the ending point the route must be reversed;
		reversed_direction = true; // Imposing to true the Reverse variable
		BlockList.sort(reverseOrderBlocks);
		list<Section> TEMPBlockList;
		list<Section>::iterator q = BlockList.begin();
		OriginalRefReversedRoute = q->end_node.X * 1000; // Original reference for offsetting train/block info on this route
		list<Section>::iterator LastElement = BlockList.end();
		LastElement--;							 // This pointer points at the last element of the BlockList
		double TotalRouteLength = q->end_node.X; // This is the total length of the Route

		while (q != BlockList.end()) {
			if (q != LastElement) { // until q is different from the last element
				list<Section>::iterator p = q;
				p++; // this iterator points at the element after it in the list BlockList
				bool AreBlocksOverlapped = areBlocksConnected(*q, *p);
				if ((AreBlocksOverlapped == 1) && (q->withSwitchDiv == 1) && (p->withSwitchDiv == 1)) { // if the two blocks are somewhat overlapped then cut the begin of q and the end of p if they have switches
					double CuttingPosition = 0;
					CuttingPosition = (q->XStartSwitch - p->XEndSwitch) / 2 + p->XEndSwitch; // The cutting position is in the middle between the end of the witch in it and the beginning of the switch in p
																							 // Now cut the blocks
					q->CutBlockSection("CutBegin", CuttingPosition); // Cutting the begin of q
					p->CutBlockSection("CutEnd", CuttingPosition);	 // Cutting the end of p
				}
			}

			Section ReversedBlock;
			ReversedBlock.reverseBlockSection(*q, TotalRouteLength); // Reversing the blocks in BlockList
			TEMPBlockList.push_back(ReversedBlock);					 // Finally pushing the modified block in the TEMPBlockList
			q++;													 // Advance the pointer
		}
		BlockList.clear(); // Deleting the previous blocklist and creating the new one with all the reversed block sections
		BlockList = TEMPBlockList;

		// Assigning the Blocks to the Route
		list<Section>::iterator it = BlockList.begin();

		if (BlockList.empty() != 1) { // if the list is not empty fill in the vector sequence_of_block_sections
			for (int i = 0; i < N_Block_Sections; i++) {
				sequence_of_block_sections[i] = *it;
				// Set the list of infrastructure elements of the route
				this->setListInfrastructureElementsForRoute(sequence_of_block_sections[i]);
				it++;
			}
		}
	}

	// Setting diverging switched whose ending edge is the starting edge of another diverging switch o-----oo-----o (basically it is used in the situation depicted in the picure where dashed lines represent the beam of diverging switches)
	identifyEndingEdgeOfDivSwitchesWhichAreStartingOfADivSwitch(this->InfrastructureElements);

	// Setting the ID of the Route (the ID is given by the ID of the initial Block Section followed by the ID of the last block section)
	ID = sequence_of_block_sections[0].ID + "->" + sequence_of_block_sections[N_Block_Sections - 1].ID;
	// Setting the x_of_start_node
	x_of_start_node = sequence_of_block_sections[0].start_node.X;
	// Setting the x_of_end_node
	x_of_end_node = sequence_of_block_sections[N_Block_Sections - 1].end_node.X;
	// Deleting Temporary Matrix
	list<Section>::iterator p = BlockList.begin();
	// while (p != BlockList.end()) { cout << p->ID << "\n";  p++; }

	delete[] Matr;
}

// function to adjust route across different regions (different km references)
void Route::adjustRouteAcrossDiffRegions() {
	list<Section> tempSections;

	for (int i = 0; i < N_Block_Sections; i++) {
		tempSections.push_back(sequence_of_block_sections[i]);

		// transition to different region
		if (i > 0 && sequence_of_block_sections[i].start_node.X != sequence_of_block_sections[i - 1].end_node.X) {
			// save jump on X (only once) - assuming one jump only
			if (diffRegionsJumpX.first == 0) {
				diffRegionsJumpX.first = sequence_of_block_sections[i].start_node.X - sequence_of_block_sections[i - 1].end_node.X; // jump length

				if (!reversed_direction) {
					diffRegionsJumpX.second = sequence_of_block_sections[i - 1].end_node.X; // jump lower bound location
				} else {
					diffRegionsJumpX.second = (OriginalRefReversedRoute / 1000) - sequence_of_block_sections[i].start_node.X; // jump lower bound location
				}
			}

			// adjust section to common km reference
			sequence_of_block_sections[i].start_node.X = sequence_of_block_sections[i - 1].end_node.X;
			sequence_of_block_sections[i].end_node.X = sequence_of_block_sections[i].start_node.X + sequence_of_block_sections[i].length;
			sequence_of_block_sections[i].arcs_in_signalling_block_section[0].startNode.X = sequence_of_block_sections[i].start_node.X;
			sequence_of_block_sections[i].arcs_in_signalling_block_section[0].endNode.X = sequence_of_block_sections[i].start_node.X + sequence_of_block_sections[i].arcs_in_signalling_block_section[0].length;

			if (sequence_of_block_sections[i].total_arcs > 1) {
				for (int j = 1; j < sequence_of_block_sections[i].total_arcs; j++) { // j = 0 already changed
					sequence_of_block_sections[i].arcs_in_signalling_block_section[j].startNode.X = sequence_of_block_sections[i].arcs_in_signalling_block_section[j - 1].endNode.X;
					sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.X = sequence_of_block_sections[i].arcs_in_signalling_block_section[j].startNode.X + sequence_of_block_sections[i].arcs_in_signalling_block_section[j].length;
				}
			}
		}
	}

	// std::cout << "JumpX: " << diffRegionsJumpX.first << " and " << diffRegionsJumpX.second << std::endl;
}

std::vector<Route> train_route;

void writeRouteFilesFromTracks(string FileName, Section* BS, int N_BS, string RouteFolder) {
	string RouteName;
	string Direction; // the direction can be direct or reverse. in case it is direct then the elements are ordered in the same order otherwise in case of reverse a reverse ordering of the elements will be done
	list<TrainEvent> TrackList;

	ifstream InputFile;
	InputFile.open((char*)FileName.c_str(), ios::binary);
	if (!InputFile) {
		cout << "ERROR: Impossible to open File " << FileName << "\n\n";
	} else {
		cout << "\nLoading Route from " << FileName << "\n\n";
		string FileLine;
		int linecounter = 0;
		while (getline(InputFile, FileLine)) {
			if (linecounter == 0) {
				RouteName = FileLine;
			} else if (linecounter == 1) {
				Direction = FileLine;
			} else if (linecounter > 1) {
				TrainEvent A;
				istringstream LineStream(FileLine);
				string Element;
				int ElementCounter = 0;
				while (LineStream >> Element) {
					if (ElementCounter == 0)
						A.Position = atoi((char*)Element.c_str()); // This one is the ID of the Trackline
					else if (ElementCounter == 1)
						A.Time = atof((char*)Element.c_str()); // This is the starting point (abcsissa in Km) where the train start traversing TrackLine on the Route
					else if (ElementCounter == 2)
						A.Time2 = atof((char*)Element.c_str()); // This is the ending point (abcsissa in Km) where the train finishes traversing TrackLine on the Route
					ElementCounter++;
				}
				TrackList.push_back(A);
			}
			linecounter++;
		}
	}
	// Once the main data have been taken we start searching for the block sections to add in the route
	list<string> RouteElements;

	if (TrackList.empty() != 1) {
		list<TrainEvent>::iterator LastElement = TrackList.end();
		LastElement--; // this iterator points at the last element of TrackList

		for (list<TrainEvent>::iterator it = TrackList.begin(); it != TrackList.end(); it++) {
			TrainEvent Connection;
			if (it != LastElement) {
				list<TrainEvent>::iterator NextOfIt = it;
				NextOfIt++;
				char itTrackID[50], NextOfItTrackID[50];
				snprintf(itTrackID, sizeof(itTrackID), "%d", (int)it->Position);
				snprintf(NextOfItTrackID, sizeof(NextOfItTrackID), "%d", (int)NextOfIt->Position);

				string track1, track2;
				track1 = track1 + "B" + itTrackID;
				track2 = track2 + "B" + NextOfItTrackID;

				Connection.trainDescription = track1;
				Connection.SuccessorID = track2;
				Connection.Time = it->Time2;	   // this is the end where train leaves track it
				Connection.Time2 = NextOfIt->Time; // the beginning of when the train crosses track NextOfIT
			}

			for (int i = 0; i < N_BS; i++) {
				if (BS[i].trackLineId == (int)it->Position) {
					// if the list route elements is still empty the take the first block section on the track line containing the first point where the train enters the network
					if (RouteElements.size() == 0) {
						if ((BS[i].start_node.X <= it->Time) && (BS[i].end_node.X >= it->Time)) {
							RouteElements.push_back(BS[i].ID);
						}

					} else {
						if ((BS[i].start_node.X >= it->Time) && (BS[i].end_node.X <= it->Time2)) {
							RouteElements.push_back(BS[i].ID);
						}
					}
				}
				if (Connection.trainDescription != "None") { // if a valid connection exists
					if (BS[i].withSwitchDiv == 1) {			 // Checking all the block sections with switches
						string FirstBlock, SecondBlock;		 // These are the Tracks connected by the switch
						double FirstPoint, SecondPoint;		 // these are the points the switch connects on the tracks
						string BlockID, Token;
						int N_Token = 0;
						BlockID = BS[i].ID;
						istringstream BlockIDStream(BlockID);
						// Dividing the name of the block section to see if this connects the two tracks at the specified point
						while (getline(BlockIDStream, Token, '@')) {
							if (Token.size() > 0) {
								if (N_Token == 0) {
									int N_tok1 = 0;
									string tok1[10];
									char* trackOne = strtok((char*)Token.c_str(), "/-");
									// breaking up token to get the ID of the track
									while (trackOne != NULL) {
										tok1[N_tok1] = trackOne;
										trackOne = strtok(NULL, "/-");
										if (N_tok1 == 1) {
											FirstBlock = tok1[N_tok1]; // the first track is tok1[1]
											break;
										}
										N_tok1++;
									}

								} else if (N_Token == 2) {
									int N_tok1 = 0;
									string tok1[10];
									char* trackOne = strtok((char*)Token.c_str(), "/-");
									// breaking up token to get the ID of the track
									while (trackOne != NULL) {
										tok1[N_tok1] = trackOne;
										trackOne = strtok(NULL, "/-");
										if (N_tok1 == 1) {
											SecondBlock = tok1[N_tok1]; // the first track is tok1[1]
											break;
										}
										N_tok1++;
									}

								} else if (N_Token == 1) {
									char* pointOne = strtok((char*)Token.c_str(), "/-");
									FirstPoint = atof(pointOne);
								} else if (N_Token == 3) {
									char* pointTwo = strtok((char*)Token.c_str(), "/-");
									SecondPoint = atof(pointTwo);
								}
								N_Token++; // Increase the N_Token
							}
						}
						// Checking if the right Connection has been found
						if (((Connection.trainDescription == FirstBlock) && (Connection.SuccessorID == SecondBlock)) || ((Connection.trainDescription == SecondBlock) && (Connection.SuccessorID == FirstBlock))) {
							if (((Connection.Time == FirstPoint) && (Connection.Time2 == SecondPoint)) || ((Connection.Time == SecondPoint) && (Connection.Time2 == FirstPoint))) {
								// if also this second condition is true then we have found the correct block section and we can break the search and pass to the next track element
								RouteElements.push_back(BS[i].ID);
								break;
							}
						}
					}
				}
			}
		}
	}

	if (RouteElements.empty() != 1) {
		// Cutting the last element from direction and RouteName because of spaces characters loaded whn reading from the file
		// Cutting the last element from direction and RouteName because of spaces characters loaded whn reading from the file
		string tok1[20];
		int N_tok1 = 0;
		char* p = strtok((char*)Direction.c_str(), "@");
		while (p != NULL) {
			tok1[N_tok1] = p;
			p = strtok(NULL, "@");
			N_tok1++;
		}
		// Getting rid of extra characters from the txt file
		Direction = tok1[0];

		string tok2[20];
		int N_tok2 = 0;
		char* q = strtok((char*)RouteName.c_str(), "@");
		while (q != NULL) {
			tok2[N_tok2] = q;
			q = strtok(NULL, "@");
			N_tok2++;
		}

		RouteName = tok2[0];

		string OutputName;
		OutputName = RouteFolder + "/" + RouteName + ".txt";

		if (Direction == "Reverse") {
			RouteElements.reverse(); // reversing the order of the route if the direction is reverse
		}
		// if the direction is direct instead the elements will be listed in the correct order already
		ofstream OutputFile;
		OutputFile.open((char*)OutputName.c_str(), ios::binary);
		for (list<string>::iterator it = RouteElements.begin(); it != RouteElements.end(); it++) {
			OutputFile << *it << "\n";
		}
		OutputFile.close();
	}

	else {
		cout << "Route " << RouteName << " is empty...\n\n";
	}
}

// Function to write all routes
void writeAllRoutes(string InputFolderName, string OutputFolderName, Section* BS, int N_BS) {
	string FileName;
	FileName = FileName + InputFolderName + "/AllRoutesToWrite.txt";

	ifstream InputFile;
	InputFile.open((char*)FileName.c_str(), ios::binary);
	if (!InputFile) {
		cout << "ERROR: Impossible to open File " << FileName << "\n\n";
	} else {
		cout << "\nLoading All Route Names To Write" << "\n\n";
		string FileLine;
		int linecounter = 0;
		while (getline(InputFile, FileLine)) {
			// getting rid of the last element of the string FileLine that is a space

			FileLine.erase(FileLine.end() - 1);
			string PathTxtToRead;
			PathTxtToRead = PathTxtToRead + InputFolderName + "/" + FileLine;
			writeRouteFilesFromTracks(PathTxtToRead, BS, N_BS, OutputFolderName);
			linecounter++;
		}
	}
}

// Function to join two routes together
void joinRoutes(int IndexRoute1, int IndexRoute2, bool IsReversed, vector<Route>& AllRoutes, int& numAllRoutes) {
	list<Section> BSList;
	double RouteLength = 0;
	// Remodel Route 1
	for (int i = 0; i < AllRoutes[IndexRoute1].N_Block_Sections; i++) {
		Section ResetBlock = AllRoutes[IndexRoute1].sequence_of_block_sections[i];
		// Resetting coordinate of block sections to their beginning
		ResetBlock.setRelativeCoordinatesToStartNode();
		// resetting block coordinates to a different origin
		ResetBlock.resetBlockCoordinatesToDifferentOrigin(RouteLength);
		// push the block section in the list
		BSList.push_back(ResetBlock);
		// increase the total route length
		RouteLength = RouteLength + ResetBlock.length;
	}

	// Remodel Route 2
	for (int i = 0; i < AllRoutes[IndexRoute2].N_Block_Sections; i++) {
		Section ResetBlock = AllRoutes[IndexRoute2].sequence_of_block_sections[i];
		// Resetting coordinate of block sections to their beginning
		ResetBlock.setRelativeCoordinatesToStartNode();
		// resetting block coordinates to a different origin
		ResetBlock.resetBlockCoordinatesToDifferentOrigin(RouteLength);
		// push the block section in the list
		BSList.push_back(ResetBlock);
		// increase the total route length
		RouteLength = RouteLength + ResetBlock.length;
	}

	// Generate a new Route
	if (BSList.empty() != 1) {
		AllRoutes[numAllRoutes].N_Block_Sections = 0;
		for (list<Section>::iterator it = BSList.begin(); it != BSList.end(); it++) {
			AllRoutes[numAllRoutes].sequence_of_block_sections[AllRoutes[numAllRoutes].N_Block_Sections] = *it;
			AllRoutes[numAllRoutes].N_Block_Sections++;
		}

		// Assigning an ID to the route
		string RouteID;
		RouteID = RouteID + "JoinedRoute_" + AllRoutes[numAllRoutes].sequence_of_block_sections[0].ID + "->" + AllRoutes[numAllRoutes].sequence_of_block_sections[AllRoutes[numAllRoutes].N_Block_Sections - 1].ID;
		AllRoutes[numAllRoutes].ID = RouteID;
	}

	// Setting if the route is reversed or not
	if (IsReversed == 1) {
		AllRoutes[numAllRoutes].reversed_direction = true;
	}

	// Setting the beginning and ending nodes of the Joined route
	AllRoutes[numAllRoutes].x_of_start_node = AllRoutes[numAllRoutes].sequence_of_block_sections[0].start_node.X;
	AllRoutes[numAllRoutes].x_of_end_node = AllRoutes[numAllRoutes].sequence_of_block_sections[AllRoutes[numAllRoutes].N_Block_Sections - 1].end_node.X;
	AllRoutes[numAllRoutes].OriginalRefReversedRoute = AllRoutes[numAllRoutes].x_of_end_node * 1000;

	// Add one more route to the vector AllRoutes
	numAllRoutes++;
}

// Function to load all the join routes to generate
void loadAllJoinedRoutes(string FolderName, vector<Route>& AllRoutes, int& numAllRoutes) {
	string InputFileName;
	InputFileName = InputFileName + FolderName + "/RoutesToJoin.txt";
	ifstream InputFile;
	InputFile.open((char*)InputFileName.c_str(), ios::binary);

	if (!InputFile) {
		cout << "ERROR 549: Impossible to open File " << InputFileName << "\n\n";
	}

	else {
		cout << "\nLoading Joined Routes from " << InputFileName << "\n\n";
		string FileLine;
		int linecounter = 0;
		while (getline(InputFile, FileLine)) {
			istringstream LineStream(FileLine);
			string LineElement;
			int Route1Index = -1, Route2Index = -1;
			string Direction;
			bool IsReversed = false; // boolean that it is true if the route must be reversed
			int LineElementCounter = 0;
			while (LineStream >> LineElement) {
				if (LineElementCounter == 0) {
					char CharsToRemove[] = "Route";
					for (unsigned int i = 0; i < strlen(CharsToRemove); ++i) {
						LineElement.erase(std::remove(LineElement.begin(), LineElement.end(), CharsToRemove[i]), LineElement.end());
					}
					Route1Index = atoi((char*)LineElement.c_str());
				} else if (LineElementCounter == 1) {
					char CharsToRemove[] = "Route";
					for (unsigned int i = 0; i < strlen(CharsToRemove); ++i) {
						LineElement.erase(std::remove(LineElement.begin(), LineElement.end(), CharsToRemove[i]), LineElement.end());
					}
					Route2Index = atoi((char*)LineElement.c_str());
				}

				else if (LineElementCounter == 2) {
					Direction = LineElement;
					if (Direction == "Reverse") {
						IsReversed = true;
					}
				}
				LineElementCounter++;
			}
			linecounter++;
			if (Route1Index >= 0 && Route2Index >= 0)
				joinRoutes(Route1Index, Route2Index, IsReversed, AllRoutes, numAllRoutes);
		}
	}
}

// function to set up all the routes
void setUpAllRoutes() {
	string RouteFolder;
	RouteFolder = RouteFolder + InputMainFolder + "/Routes/";

	for (int i = 0; i < N_Routes; i++) {
		string FileName;
		char RouteID[20];
		sprintf_s(RouteID, "%d", i);
		FileName = RouteFolder + "Route" + RouteID + ".txt";
		std::cout << "\rCreating Route : " << i << " " << FileName;
		train_route[i].createRoute((char*)FileName.c_str());

		// adjust route if it crosses regions
		train_route[i].adjustRouteAcrossDiffRegions();
	}
	std::cout << "\n";
}

// Function to Print out all the block sections of a certain route with the respective initial and ending nodes. It can be used to verify the congruency of the block sections built up
void printRoutesBlocks(const Route& R, string FolderName, int IndexOfRoute) {
	ofstream OUTFile;
	string OutFileName;
	char IndexOfRouteChar[50];
	sprintf_s(IndexOfRouteChar, "%d", IndexOfRoute);
	OutFileName = OutFileName + FolderName + "/RoutesGenerated/" + "Route" + IndexOfRouteChar + ".txt";
	OUTFile.open((char*)OutFileName.c_str(), ios::binary);
	for (int i = 0; i < R.N_Block_Sections; i++) {
		OUTFile << R.sequence_of_block_sections[i].ID << " " << R.sequence_of_block_sections[i].start_node.X << " " << R.sequence_of_block_sections[i].end_node.X << "\n";
	}
	OUTFile.close();
}

void printAllRoutes(string FolderName) {
	for (int i = 0; i < N_Routes; i++) {
		printRoutesBlocks(train_route[i], FolderName, i);
	}
}

void printRoutesStations(const Route& R) {
	ofstream OUTFile;
	string OutFileName;
	OutFileName = OutFileName + InputMainFolder + "/Routes/Stations_OF_Route.txt";
	OUTFile.open((char*)OutFileName.c_str(), ios::binary);
	for (int i = 0; i < R.N_Block_Sections; i++) {
		for (int j = 0; j < R.sequence_of_block_sections[i].total_arcs; j++) {
			if (R.sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.stationName.empty() != 1)
				OUTFile << R.sequence_of_block_sections[i].arcs_in_signalling_block_section[j].endNode.stationName << " ";
		}
	}
	OUTFile.close();
}

// Function to Verify validity of a route
void verifyRouteValidity(const Route& R, int IndexRoute) {
	if (R.N_Block_Sections > 0) {
		for (int i = 0; i < R.N_Block_Sections; i++) {
			int NextTrackLineID = -1;
			int PreviousTrackLineID = -1; // These are the trackLineId of the block sections next and previous to signalling_block_sections[i]

			// if the block section has a diverging switch, then see which is the block section before and the one after
			if (i == 0) { // if this is the first block section of the route

				if (R.sequence_of_block_sections[i + 1].withSwitchDiv == 1) {
					NextTrackLineID = R.sequence_of_block_sections[i + 1].FirstConnectedTrackLineID;
				} else {
					NextTrackLineID = R.sequence_of_block_sections[i + 1].trackLineId;
				}

				if (R.sequence_of_block_sections[i].withSwitchDiv == 1) {
					if (R.sequence_of_block_sections[i].SecondConnectedTrackLineID != NextTrackLineID) {
						cout << "\nERROR1 in Route " << R.ID << " with Route Index: " << IndexRoute << " at Block Section " << R.sequence_of_block_sections[i].ID << " does not correctly connect with the trackline of next block section " << R.sequence_of_block_sections[i + 1].ID << "\n";
					}
				} else { // if instead the block section does not have a diverging switch
					if (R.sequence_of_block_sections[i].trackLineId != NextTrackLineID) {
						cout << "\nERROR2 in Route " << R.ID << " with Route Index: " << IndexRoute << " at Block Section " << R.sequence_of_block_sections[i].ID << " does not correctly connect with the trackline of next block section " << R.sequence_of_block_sections[i + 1].ID << "\n";
					}
				}

			} else if ((i > 0) && (i < R.N_Block_Sections - 1)) {

				if (R.sequence_of_block_sections[i - 1].withSwitchDiv == 1) {
					PreviousTrackLineID = R.sequence_of_block_sections[i - 1].SecondConnectedTrackLineID;
				} else {
					PreviousTrackLineID = R.sequence_of_block_sections[i - 1].trackLineId;
				}
				if (R.sequence_of_block_sections[i + 1].withSwitchDiv == 1) {
					NextTrackLineID = R.sequence_of_block_sections[i + 1].FirstConnectedTrackLineID;
				} else {
					NextTrackLineID = R.sequence_of_block_sections[i + 1].trackLineId;
				}

				if (R.sequence_of_block_sections[i].withSwitchDiv == 1) { // if signalling_block_sections[i] has a diverging switch
					if (R.sequence_of_block_sections[i].FirstConnectedTrackLineID != PreviousTrackLineID) {
						cout << "\nERROR3 in Route " << R.ID << " with Route Index: " << IndexRoute << " at Block Section " << R.sequence_of_block_sections[i].ID << " does not correctly connect with the trackline of previous block section " << R.sequence_of_block_sections[i - 1].ID << "\n";
					}
					if (R.sequence_of_block_sections[i].SecondConnectedTrackLineID != NextTrackLineID) {
						cout << "\nERROR4 in Route " << R.ID << " with Route Index: " << IndexRoute << " at Block Section " << R.sequence_of_block_sections[i].ID << " does not correctly connect with the trackline of next block section " << R.sequence_of_block_sections[i + 1].ID << "\n";
					}
				}

				else { // if instead signalling_block_sections[i] does not have a diverging switch
					if (R.sequence_of_block_sections[i].trackLineId != PreviousTrackLineID) {
						cout << "\nERROR5 in Route " << R.ID << " with Route Index: " << IndexRoute << " at Block Section " << R.sequence_of_block_sections[i].ID << " does not correctly connect with the trackline of previous block section " << R.sequence_of_block_sections[i - 1].ID << "\n";
					}
					if (R.sequence_of_block_sections[i].trackLineId != NextTrackLineID) {
						cout << "\nERROR6 in Route " << R.ID << "- " << R.sequence_of_block_sections[i].ID << " with Route Index: " << IndexRoute << " at Block Section " << R.sequence_of_block_sections[i].ID << " does not correctly connect with the trackline of next block section " << R.sequence_of_block_sections[i + 1].ID << "-" << NextTrackLineID << "\n";
					}
				}

			}

			else if (i == R.N_Block_Sections - 1) { // if signalling_block_sections[i] is the last block section of the route
				if (R.sequence_of_block_sections[i - 1].withSwitchDiv == 1) {
					PreviousTrackLineID = R.sequence_of_block_sections[i - 1].SecondConnectedTrackLineID;
				} else {
					PreviousTrackLineID = R.sequence_of_block_sections[i - 1].trackLineId;
				}

				if (R.sequence_of_block_sections[i].withSwitchDiv == 1) { // if signalling_block_sections[i] has a diverging switch
					if (R.sequence_of_block_sections[i].FirstConnectedTrackLineID != PreviousTrackLineID) {
						cout << "\nERROR7 in Route " << R.ID << " with Route Index: " << IndexRoute << " at Block Section " << R.sequence_of_block_sections[i].ID << " does not correctly connect with the trackline of previous block section " << R.sequence_of_block_sections[i - 1].ID << "\n";
					}
				} else { // if signalling_block_sections[i] does not have any diverging switch instead
					if (R.sequence_of_block_sections[i].trackLineId != PreviousTrackLineID) {
						cout << "\nERROR8 in Route " << R.ID << " with Route Index: " << IndexRoute << " at Block Section " << R.sequence_of_block_sections[i].ID << " does not correctly connect with the trackline of previous block section " << R.sequence_of_block_sections[i - 1].ID << "\n";
					}
				}
			}

			if (R.sequence_of_block_sections[i].start_node.tdsbId.empty() == 1) {
				eglogger << "Warning: Block Section: " << R.sequence_of_block_sections[i].ID << " on Route " << IndexRoute << " has starting Node without any tdsbId\n";
			}
			if (R.sequence_of_block_sections[i].end_node.tdsbId.empty() == 1) {
				eglogger << "Warning: Block Section: " << R.sequence_of_block_sections[i].ID << " on Route " << IndexRoute << " has ending Node without any tdsbId\n";
			}

			if (R.sequence_of_block_sections[i].start_node.tdsbId == "VirtualTDSB") {
				eglogger << "Block Section: " << R.sequence_of_block_sections[i].ID << " on Route " << IndexRoute << " has starting Node as a Virtual tdsbId\n";
			}

			if (R.sequence_of_block_sections[i].end_node.tdsbId == "VirtualTDSB") {
				eglogger << "Block Section: " << R.sequence_of_block_sections[i].ID << " on Route " << IndexRoute << " has ending Node as a Virtual tdsbId\n"
						 << std::endl;
			}
		}
	}
}

void setListAllInfrastructureElementsFromRoutes(vector<Route>& R, int N_Routes) {
	if (N_Routes > 0) {
		for (int i = 0; i < N_Routes; i++) {
			if (R[i].InfrastructureElements.size() > 0) {
				for (list<InfraElement>::iterator b = R[i].InfrastructureElements.begin(); b != R[i].InfrastructureElements.end(); b++) {
					if (b->ID != "VirtualTDSB") { // The element can be inserted only if it is not a Virtual TDSB (which has been created to separate two adjacent diverging switches) but a real infrastructure element
						InfraEvent TEMP_InfraEvent;
						TEMP_InfraEvent.ElementInfo = *b;
						// Changing the ID of the of TEMP_InfraEvent with the SwitchName of b in case b is a Diverging Switch
						if ((b->IsSwitch == 1) && (b->withSwitchDiv == 1)) {
							TEMP_InfraEvent.ElementInfo.ID = b->SwitchName;
						}

						// if the list of infraEvents is empty then insert the TEMP_InfraEvent
						if (InfraElementsList.size() == 0) {
							InfraElementsList.push_back(TEMP_InfraEvent);
						}
						// If it has instead already elements, check if the Infrastructure Element b is already present in the list
						else {
							bool IsAlreadyThere = false;
							for (list<InfraEvent>::iterator e = InfraElementsList.begin(); e != InfraElementsList.end(); e++) {
								if (TEMP_InfraEvent.ElementInfo.ID == e->ElementInfo.ID) {
									IsAlreadyThere = true;
									break; // break the for loop on InfraElements list
								}
							}
							if (IsAlreadyThere == 0) {
								InfraElementsList.push_back(TEMP_InfraEvent);
							}
						}
					}
				}
			}
		}
	}
}

// Class constructor
MovementAuthority::MovementAuthority() {
	BSID = "None";
	ReversedDirection = false;
	AbsPosEoA = RelativePosEoA = EoA_Dist_From_BSID_Beg = -1;
	type = typePart = "None";
}

double S_delay = 0; // Variable representing the delay of Signalling Systems (in seconds): To put a delay of x seconds this variable must be set to x-1 seconds (because you have to consider also the timestep delay of the integration

list<string> BlocksOccupied;	 // This is the list of the All Blocks Occupied by trains: i.e. Blocks where train are on and blocks which are connected to occupied blocks
list<string> BlocksConnected;	 // This is the list of all the Blocks Connected with the Blocks occupied by trains. This list is needed in order to release the blocks contained in there when the train on the connected block has left it.
list<MovementAuthority> ETCS_MA; // This is the list of all the movement authorities provided by the RBC where ETCS is active
extern Owl owl;
// Function to Occupy a Block Section and all the Block sections connected with it
void occupyBlockAndConnected(const Section& BLS, const Section& BLSPrev, double S_i, double S_i_1) {
	owl << ">>>>>>>>>>>>>BLS.ID " << BLS.ID << " BLS.IDPrev " << BLSPrev.ID << std::endl;
	int NumBlocksToFind = BLS.N_ConnectedBS + 1;
	string* IDToAdd = new string[NumBlocksToFind]; // These contain the ID of Block Sections, the test to see if that ID is present in the BlocksOccupied list and the test to check if the ID is present in the BlocksConnected
	bool* IsThere = new bool[NumBlocksToFind];
	string IDToFree[20];
	bool IsInBlocksConnected[20]; // These variables are relative to the signalling_block_sections connected to the BLSPrev that have to be freed
	IDToAdd[0] = BLS.ID;
	IsThere[0] = false;
	for (int i = 1; i < NumBlocksToFind; i++) {
		IDToAdd[i] = BLS.IDConnectedBS[i - 1];
		IsThere[i] = false;
	}
	// Filling in the BlocksOccupied list
	for (int i = 0; i < NumBlocksToFind; i++) {
		for (list<string>::iterator it = BlocksOccupied.begin(); it != BlocksOccupied.end(); it++) {
			if (*it == IDToAdd[i])
				IsThere[i] = true;
		}
		if (IsThere[i] == 0) {
			BlocksOccupied.push_back(IDToAdd[i]);
			owl << "BLS.ID " << BLS.ID << " BLS.IDPrev " << BLSPrev.ID << " add " << IDToAdd[i] << std::endl;
			int counter = 0;
			for (list<string>::iterator it2 = BlocksOccupied.begin(); it2 != BlocksOccupied.end(); it2++) {
				owl << "Occupied " << counter << " blcok " << *it2 << std::endl;
			}
		}
	}

	// Clear all the connected block sections when the tale of the train exits the previous block section
	// Filling in the BlocksConnected list
	if (BLSPrev.N_ConnectedBS > 0) {
		if ((S_i_1 <= BLSPrev.end_node.X * 1000) && (S_i > BLSPrev.end_node.X * 1000)) {
			int N_BLSPrev_Conn = BLSPrev.N_ConnectedBS;

			// add previous signalling_block_sections
			////IDToFree[0] = BLSPrev.ID; IsInBlocksConnected[0] = false;

			for (int h = 0; h < N_BLSPrev_Conn; h++) {
				IDToFree[h] = BLSPrev.IDConnectedBS[h];
				IsInBlocksConnected[h] = false;
			}

			// Load BSConnected to free to the BlocksConnected list
			for (int i = 0; i <= N_BLSPrev_Conn; i++) {
				for (list<string>::iterator it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
					if (*it == IDToFree[i]) {
						IsInBlocksConnected[i] = true;
						break;
					}
				}
				if (IsInBlocksConnected[i] == 0)
					BlocksConnected.push_back(IDToFree[i]);
			}
		}
		// only for the Copenhagen case where double switches are used for the rail crossings X

		if (BLS.ID == "@5-B6@")
			; // BlocksOccupied.push_back("@1-B30@-4.592000/@5-B7@-4.620000");
			  // if (BLS.ID == "@1-B30@-4.592000/@5-B7@-4.620000")
		//	BlocksOccupied.push_back("@5-B6@");
	}
	// fill only with previous signalling_block_sections
	/*else {
		IDToFree[0] = BLSPrev.ID; IsInBlocksConnected[0] = false;
		for (list<string>::iterator it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
			if (*it == IDToFree[0]) { IsInBlocksConnected[0] = true; break; }
		}
		if (IsInBlocksConnected[0] == 0) BlocksConnected.push_back(IDToFree[0]);
	}*/

	// release previously occupied signalling_block_sections TESTE!!!
	releaseLastBlockAndConnected(BLSPrev);

	delete[] IDToAdd;
	delete[] IsThere;
}

// Function to occupy a double switch
void occupyDoubleSwitch(const Section& BLS, const Section& BLSPrev) {
	// prevent cases not crossing the full double switch (e.g. region jumps)
	if (BLS.ID.find('/') == std::string::npos || BLSPrev.ID.find('/') == std::string::npos) {
		return;
	}

	std::list<const Section*> doubleSwitchBS = {&BLS, &BLSPrev};
	for (auto itBS = doubleSwitchBS.begin(); itBS != doubleSwitchBS.end(); ++itBS) {
		const auto& BLS = **itBS;

		// occupy compound signalling_block_sections
		{
			int NumBlocksToFind = BLS.N_ConnectedBS + 1;
			string* IDToAdd = new string[NumBlocksToFind];
			bool* IsThere = new bool[NumBlocksToFind]; // These contain the ID of Block Sections, the test to see if that ID is present in the BlocksOccupied list and the test to check if the ID is present in the BlocksConnected
			string IDToFree[20];
			bool IsInBlocksConnected[20]; // These variables are relative to the signalling_block_sections connected to the BLSPrev that have to be freed
			IDToAdd[0] = BLS.ID;
			IsThere[0] = false;
			for (int i = 1; i < NumBlocksToFind; i++) {
				IDToAdd[i] = BLS.IDConnectedBS[i - 1];
				IsThere[i] = false;
			}
			// Filling in the BlocksOccupied list
			for (int i = 0; i < NumBlocksToFind; i++) {
				for (list<string>::iterator it = BlocksOccupied.begin(); it != BlocksOccupied.end(); it++) {
					if (*it == IDToAdd[i])
						IsThere[i] = true;
				}
				if (IsThere[i] == 0)
					BlocksOccupied.push_back(IDToAdd[i]);
			}

			delete[] IDToAdd;
			delete[] IsThere;
		}

		{
			// occupy connected single signalling_block_sections

			// Defining the token of this connected Block the first token is the ID of the first block section, the second token is instead the ID of the second block section
			string tok1[10];
			int N_tok1 = 0;
			string BlockID1, BlockID2;
			char* p = strtok((char*)BLS.ID.c_str(), "/-");
			while (p != NULL) {
				tok1[N_tok1] = p;
				p = strtok(NULL, "/-");
				N_tok1++;
			}
			BlockID1 = tok1[0] + "-" + tok1[1];
			BlockID2 = tok1[3] + "-" + tok1[4];
			// Identify the TrackLine ID of BlockID1 and BlockID2;
			int TrackLineIDBlock1 = -1, TrackLineIDBlock2 = -1;
			TrackLineIDBlock1 = atoi(strtok((char*)tok1[1].c_str(), "B@"));
			TrackLineIDBlock2 = atoi(strtok((char*)tok1[4].c_str(), "B@"));

			/*//if TrackLineIDPrevBS is -1 or is equal to the trackLineId of the previous block section leave everything as it is otherwise
			if (TrackLineIDBlock2 == TrackLineIDPrevBS) {//if the trackLineId of the second block section is equal to the one of the previous block section then invert BlockID1 and BlockID2
				string TEMPBlockID;
				TEMPBlockID = BlockID2;
				BlockID2 = BlockID1;
				BlockID1 = TEMPBlockID;
			}*/

			// Adding ID of Connected Block Sections to the lists BlocksOccupied and BlocksConnected
			string IDToAdd[2];
			IDToAdd[0] = BlockID1;
			IDToAdd[1] = BlockID2;
			bool IsInBlocksOccupied[2], IsInBlocksConnected[2];
			for (int i = 0; i < 2; i++) {
				IsInBlocksOccupied[i] = IsInBlocksConnected[i] = false;
			}

			// Filling in the BlocksOccupied list
			for (int h = 0; h < 2; h++) {
				for (list<string>::iterator it = BlocksOccupied.begin(); it != BlocksOccupied.end(); it++) {
					if (*it == IDToAdd[h])
						IsInBlocksOccupied[h] = true;
				}
				if (IsInBlocksOccupied[h] == 0)
					BlocksOccupied.push_back(IDToAdd[h]);
			}

			// Filling in the BlocksConnected list
			for (int h = 0; h < 2; h++) {
				for (list<string>::iterator it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
					if (*it == IDToAdd[h])
						IsInBlocksConnected[h] = true;
				}
				if (IsInBlocksConnected[h] == 0)
					BlocksConnected.push_back(IDToAdd[h]);
			}

			// find signalling_block_sections from other branches
			Section *branchBS1 = nullptr, *branchBS2 = nullptr;
			bool branch1 = false, branch2 = false;
			for (int b = 0; b < Blocks; b++) {
				if (::signalling_block_sections[b].ID == BlockID1) {
					branchBS1 = &::signalling_block_sections[b];
					branch1 = true;
				} else if (::signalling_block_sections[b].ID == BlockID2) {
					branchBS2 = &::signalling_block_sections[b];
					branch2 = true;
				}
				// found both signalling_block_sections
				if (branch1 && branch2) {
					break;
				}
			}

			// occupy compound signalling_block_sections connected to single signalling_block_sections (other branches of double switch)
			std::list<Section*> branchBS = {branchBS1, branchBS2};
			for (auto itb = branchBS.begin(); itb != branchBS.end(); ++itb) {
				const auto& BLS = **itb;

				// occupy compound signalling_block_sections
				int NumBlocksToFind = BLS.N_ConnectedBS + 1;
				string* IDToAdd = new string[NumBlocksToFind];
				bool* IsThere = new bool[NumBlocksToFind]; // These contain the ID of Block Sections, the test to see if that ID is present in the BlocksOccupied list and the test to check if the ID is present in the BlocksConnected
				string IDToFree[20];
				bool IsInBlocksConnected[20]; // These variables are relative to the signalling_block_sections connected to the BLSPrev that have to be freed
				IDToAdd[0] = BLS.ID;
				IsThere[0] = false;
				for (int i = 1; i < NumBlocksToFind; i++) {
					IDToAdd[i] = BLS.IDConnectedBS[i - 1];
					IsThere[i] = false;
				}
				// Filling in the BlocksOccupied list
				for (int i = 0; i < NumBlocksToFind; i++) {
					for (list<string>::iterator it = BlocksOccupied.begin(); it != BlocksOccupied.end(); it++) {
						if (*it == IDToAdd[i])
							IsThere[i] = true;
					}
					if (IsThere[i] == 0)
						BlocksOccupied.push_back(IDToAdd[i]);
				}

				delete[] IDToAdd;
				delete[] IsThere;
			}
		}
	}
}

// Function to release a double switch
void releaseDoubleSwitch(const Section& BLS, const Section& BLSPrev) {
	// prevent cases not crossing the full double switch (e.g. region jumps)
	if (BLS.ID.find('/') == std::string::npos || BLSPrev.ID.find('/') == std::string::npos) {
		return;
	}

	std::list<const Section*> doubleSwitchBS = {&BLS, &BLSPrev};
	for (auto it = doubleSwitchBS.begin(); it != doubleSwitchBS.end(); ++it) {
		const auto& BLS = **it;

		string* IDToAdd;
		// Load the BLS ID and the ones of the connected Blocks in the BlocksConnected list when the former has not a diverging switch
		if (BLS.withSwitchDiv == 0) {
			int NumBlocksToFind = BLS.N_ConnectedBS + 1;
			IDToAdd = new string[NumBlocksToFind];
			bool IsInBlocksConnected[40]; // These contain the ID of Block Sections, the test to see if that ID is present in the BlocksConnected
			IDToAdd[0] = BLS.ID;
			IsInBlocksConnected[0] = false;
			for (int i = 0; i < BLS.N_ConnectedBS; i++) {
				IDToAdd[i + 1] = BLS.IDConnectedBS[i];
				IsInBlocksConnected[i + 1] = false;
			}
			// Filling in the BlocksConnected
			for (int i = 0; i < NumBlocksToFind; i++) {
				for (list<string>::iterator it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
					if (*it == IDToAdd[i])
						IsInBlocksConnected[i] = true;
				}
				if (IsInBlocksConnected[i] == 0)
					BlocksConnected.push_back(IDToAdd[i]);
			}

		}
		// if instead BLS has a diverging switch not only its ID and the one of the connected blocks must be put, but also the ones of the BlockID1 and BlockID2(see ActivateSwitch function above) in the BlocksConnected list
		else {
			int NumBlocksToFind = BLS.N_ConnectedBS + 3;
			IDToAdd = new string[NumBlocksToFind];
			bool IsInBlocksConnected[40]; // These contain the ID of Block Sections, the test to see if that ID is present in the BlocksConnected
			IDToAdd[0] = BLS.ID;
			IsInBlocksConnected[0] = false; // The first element of the array is the ID of BLS
											// Defining the token of this connected Block the first token is the ID of the first block section, the second token is instead the ID of the second block section
			string tok1[10];
			int N_tok1 = 0;
			string BlockID1, BlockID2;
			char* p = strtok((char*)BLS.ID.c_str(), "/-");
			while (p != NULL) {
				tok1[N_tok1] = p;
				p = strtok(NULL, "/-");
				N_tok1++;
			}
			BlockID1 = tok1[0] + "-" + tok1[1];
			BlockID2 = tok1[3] + "-" + tok1[4];
			// Adding ID of Connected Block Sections to the lists BlocksOccupied and BlocksConnected
			IDToAdd[1] = BlockID1;
			IsInBlocksConnected[1] = false;
			IDToAdd[2] = BlockID2;
			IsInBlocksConnected[2] = false;
			for (int i = 0; i < BLS.N_ConnectedBS; i++) {
				IDToAdd[i + 3] = BLS.IDConnectedBS[i];
				IsInBlocksConnected[i + 3] = false;
			}
			// Filling in the BlocksConnected
			for (int i = 0; i < NumBlocksToFind; i++) {
				for (list<string>::iterator it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
					if (*it == IDToAdd[i])
						IsInBlocksConnected[i] = true;
				}
				if (IsInBlocksConnected[i] == 0)
					BlocksConnected.push_back(IDToAdd[i]);
			}

			// find signalling_block_sections from other branches
			Section branchBS1, branchBS2;
			bool branch1 = false, branch2 = false;
			for (int b = 0; b < Blocks; b++) {
				if (::signalling_block_sections[b].ID == BlockID1) {
					branchBS1 = ::signalling_block_sections[b];
					branch1 = true;
				} else if (::signalling_block_sections[b].ID == BlockID2) {
					branchBS2 = ::signalling_block_sections[b];
					branch2 = true;
				}
				// found both signalling_block_sections
				if (branch1 && branch2) {
					break;
				}
			}

			// release compound signalling_block_sections connected to single signalling_block_sections (other branches of double switch)
			releaseLastBlockAndConnected(branchBS1);
			releaseLastBlockAndConnected(branchBS2);
		}
		delete[] IDToAdd;
	}
}

// unlock double switches (otherwise train gets stuck in the middle)
void unlockDoubleSwitches() {
	for (int r = 0; r < N_Routes; r++) {
		for (int b = train_route[r].N_Block_Sections - 1; b >= 0; b--) {
			// double switch locked
			if (train_route[r].sequence_of_block_sections[b].start_node.virtualSignal && train_route[r].sequence_of_block_sections[b].code == 0) {
				// changes on previous signalling_block_sections
				if (b > 0) {
					strcpy_s(train_route[r].sequence_of_block_sections[b - 1].state, "green");
					for (int k = 0; k < train_route[r].sequence_of_block_sections[b - 1].total_arcs; k++) {
						train_route[r].sequence_of_block_sections[b - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}

					// revert changes to speeds
					double MinSpeedLim = train_route[r].sequence_of_block_sections[b].arcs_in_signalling_block_section[0].speedLimit;
					if (train_route[r].sequence_of_block_sections[b].arcs_in_signalling_block_section[0].signalSpeedLimit < MinSpeedLim) {
						MinSpeedLim = train_route[r].sequence_of_block_sections[b].arcs_in_signalling_block_section[0].signalSpeedLimit;
					}

					// the speed limit at the end of block section BLS[h] is MinSpeedLim
					train_route[r].sequence_of_block_sections[b - 1].arcs_in_signalling_block_section[train_route[r].sequence_of_block_sections[b - 1].total_arcs - 1].speedInBraking = MinSpeedLim;
				}

				// changes on signalling_block_sections itself
				train_route[r].sequence_of_block_sections[b].code = 270;
				train_route[r].sequence_of_block_sections[b].exit_speed = 0;
			}
		}
	}
}

// Function to Activate blocks connected with block sections having switches in diverging position
void activateBlocksWithSwitchesDiv(const Section& BS, int TrackLineIDPrevBS, double S) {
	// Defining the token of this connected Block the first token is the ID of the first block section, the second token is instead the ID of the second block section
	string tok1[10];
	int N_tok1 = 0;
	string BlockID1, BlockID2;
	char* p = strtok((char*)BS.ID.c_str(), "/-");
	while (p != NULL) {
		tok1[N_tok1] = p;
		p = strtok(NULL, "/-");
		N_tok1++;
	}
	BlockID1 = tok1[0] + "-" + tok1[1];
	BlockID2 = tok1[3] + "-" + tok1[4];
	// Identify the TrackLine ID of BlockID1 and BlockID2;
	int TrackLineIDBlock1 = -1, TrackLineIDBlock2 = -1;
	// A simple (non switch-transition) block ID has only two tokens, so tok1[4]
	// is empty and strtok returns NULL; guard against atoi(NULL) dereferencing.
	{
		char* trackTok1 = strtok((char*)tok1[1].c_str(), "B@");
		char* trackTok2 = strtok((char*)tok1[4].c_str(), "B@");
		TrackLineIDBlock1 = trackTok1 ? atoi(trackTok1) : -1;
		TrackLineIDBlock2 = trackTok2 ? atoi(trackTok2) : -1;
	}

	// if TrackLineIDPrevBS is -1 or is equal to the trackLineId of the previous block section leave everything as it is otherwise
	if (TrackLineIDBlock2 == TrackLineIDPrevBS) { // if the trackLineId of the second block section is equal to the one of the previous block section then invert BlockID1 and BlockID2
		string TEMPBlockID;
		TEMPBlockID = BlockID2;
		BlockID2 = BlockID1;
		BlockID1 = TEMPBlockID;
	}

	// Adding ID of Connected Block Sections to the lists BlocksOccupied and BlocksConnected
	string IDToAdd[2];
	IDToAdd[0] = BlockID1;
	IDToAdd[1] = BlockID2;
	bool IsInBlocksOccupied[2], IsInBlocksConnected[2];
	for (int i = 0; i < 2; i++) {
		IsInBlocksOccupied[i] = IsInBlocksConnected[i] = false;
	}
	// if the S of the train has not passed the initial point of the diverging switch only BlockID1 will result as occupied while BlocksID2 is still free
	if (S < BS.XStartSwitch * 1000) {
		for (list<string>::iterator it = BlocksOccupied.begin(); it != BlocksOccupied.end(); it++) {
			if (*it == IDToAdd[0])
				IsInBlocksOccupied[0] = true;
		}
		if (IsInBlocksOccupied[0] == 0)
			BlocksOccupied.push_back(IDToAdd[0]);

	}
	// if the S of the Train is before the XEndSwitch and after its XStartSwitch all the Block Sections are occupied: Block1 and Block2 and of course the one with the Switch diverging
	else if ((S >= BS.XStartSwitch * 1000) && (S < BS.XEndSwitch * 1000)) {
		for (int h = 0; h < 2; h++) {
			for (list<string>::iterator it = BlocksOccupied.begin(); it != BlocksOccupied.end(); it++) {
				if (*it == IDToAdd[h])
					IsInBlocksOccupied[h] = true;
			}
			if (IsInBlocksOccupied[h] == 0)
				BlocksOccupied.push_back(IDToAdd[h]);
		}
	}
	// When instead the train has passed the XEndSwitch
	else {
		for (list<string>::iterator it = BlocksOccupied.begin(); it != BlocksOccupied.end(); it++) {
			if (*it == IDToAdd[1])
				IsInBlocksOccupied[1] = true;
		}
		if (IsInBlocksOccupied[1] == 0)
			BlocksOccupied.push_back(IDToAdd[1]);

		// Filling in the BlocksConnected list
		for (int h = 0; h < 2; h++) {
			for (list<string>::iterator it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
				if (*it == IDToAdd[h])
					IsInBlocksConnected[h] = true;
			}
			if (IsInBlocksConnected[h] == 0)
				BlocksConnected.push_back(IDToAdd[h]);
		}
	}
}

// Function to Activate blocks connected with block sections having switches in diverging position
void activateBlocksWithSwitchesDivFixedBlock(const Section& BS, int TrackLineIDPrevBS, double S) {
	// Defining the token of this connected Block the first token is the ID of the first block section, the second token is instead the ID of the second block section
	string tok1[10];
	int N_tok1 = 0;
	string BlockID1, BlockID2;
	char* p = strtok((char*)BS.ID.c_str(), "/-");
	while (p != NULL) {
		tok1[N_tok1] = p;
		p = strtok(NULL, "/-");
		N_tok1++;
	}
	BlockID1 = tok1[0] + "-" + tok1[1];
	BlockID2 = tok1[3] + "-" + tok1[4];
	// Identify the TrackLine ID of BlockID1 and BlockID2;
	int TrackLineIDBlock1 = -1, TrackLineIDBlock2 = -1;
	// A simple (non switch-transition) block ID has only two tokens, so tok1[4]
	// is empty and strtok returns NULL; guard against atoi(NULL) dereferencing.
	{
		char* trackTok1 = strtok((char*)tok1[1].c_str(), "B@");
		char* trackTok2 = strtok((char*)tok1[4].c_str(), "B@");
		TrackLineIDBlock1 = trackTok1 ? atoi(trackTok1) : -1;
		TrackLineIDBlock2 = trackTok2 ? atoi(trackTok2) : -1;
	}

	// if TrackLineIDPrevBS is -1 or is equal to the trackLineId of the previous block section leave everything as it is otherwise
	if (TrackLineIDBlock2 == TrackLineIDPrevBS) { // if the trackLineId of the second block section is equal to the one of the previous block section then invert BlockID1 and BlockID2
		string TEMPBlockID;
		TEMPBlockID = BlockID2;
		BlockID2 = BlockID1;
		BlockID1 = TEMPBlockID;
	}

	// Adding ID of Connected Block Sections to the lists BlocksOccupied and BlocksConnected
	string IDToAdd[2];
	IDToAdd[0] = BlockID1;
	IDToAdd[1] = BlockID2;
	bool IsInBlocksOccupied[2], IsInBlocksConnected[2];
	for (int i = 0; i < 2; i++) {
		IsInBlocksOccupied[i] = IsInBlocksConnected[i] = false;
	}

	// Filling in the BlocksOccupied list
	for (int h = 0; h < 2; h++) {
		for (list<string>::iterator it = BlocksOccupied.begin(); it != BlocksOccupied.end(); it++) {
			if (*it == IDToAdd[h])
				IsInBlocksOccupied[h] = true;
		}
		if (IsInBlocksOccupied[h] == 0)
			BlocksOccupied.push_back(IDToAdd[h]);
	}

	// Filling in the BlocksConnected list
	for (int h = 0; h < 2; h++) {
		for (list<string>::iterator it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
			if (*it == IDToAdd[h])
				IsInBlocksConnected[h] = true;
		}
		if (IsInBlocksConnected[h] == 0)
			BlocksConnected.push_back(IDToAdd[h]);
	}
}

// Function to Release the Last Block Section and the other connected Blocks when the train exits simulation
void releaseLastBlockAndConnected(const Section& BLS) {
	string* IDToAdd;
	// Load the BLS ID and the ones of the connected Blocks in the BlocksConnected list when the former has not a diverging switch
	if (BLS.withSwitchDiv == 0) {
		int NumBlocksToFind = BLS.N_ConnectedBS + 1;
		IDToAdd = new string[NumBlocksToFind];
		bool IsInBlocksConnected[40]; // These contain the ID of Block Sections, the test to see if that ID is present in the BlocksConnected
		IDToAdd[0] = BLS.ID;
		IsInBlocksConnected[0] = false;
		for (int i = 0; i < BLS.N_ConnectedBS; i++) {
			IDToAdd[i + 1] = BLS.IDConnectedBS[i];
			IsInBlocksConnected[i + 1] = false;
		}
		// Filling in the BlocksConnected
		for (int i = 0; i < NumBlocksToFind; i++) {
			for (list<string>::iterator it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
				if (*it == IDToAdd[i])
					IsInBlocksConnected[i] = true;
			}
			if (IsInBlocksConnected[i] == 0)
				BlocksConnected.push_back(IDToAdd[i]);
		}

	}
	// if instead BLS has a diverging switch not only its ID and the one of the connected blocks must be put, but also the ones of the BlockID1 and BlockID2(see ActivateSwitch function above) in the BlocksConnected list
	else {
		int NumBlocksToFind = BLS.N_ConnectedBS + 3;
		IDToAdd = new string[NumBlocksToFind];
		bool IsInBlocksConnected[40]; // These contain the ID of Block Sections, the test to see if that ID is present in the BlocksConnected
		IDToAdd[0] = BLS.ID;
		IsInBlocksConnected[0] = false; // The first element of the array is the ID of BLS
										// Defining the token of this connected Block the first token is the ID of the first block section, the second token is instead the ID of the second block section
		string tok1[10];
		int N_tok1 = 0;
		string BlockID1, BlockID2;
		char* p = strtok((char*)BLS.ID.c_str(), "/-");
		while (p != NULL) {
			tok1[N_tok1] = p;
			p = strtok(NULL, "/-");
			N_tok1++;
		}
		BlockID1 = tok1[0] + "-" + tok1[1];
		BlockID2 = tok1[3] + "-" + tok1[4];
		// Adding ID of Connected Block Sections to the lists BlocksOccupied and BlocksConnected
		IDToAdd[1] = BlockID1;
		IsInBlocksConnected[1] = false;
		IDToAdd[2] = BlockID2;
		IsInBlocksConnected[2] = false;
		for (int i = 0; i < BLS.N_ConnectedBS; i++) {
			IDToAdd[i + 3] = BLS.IDConnectedBS[i];
			IsInBlocksConnected[i + 3] = false;
		}
		// Filling in the BlocksConnected
		for (int i = 0; i < NumBlocksToFind; i++) {
			for (list<string>::iterator it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
				if (*it == IDToAdd[i])
					IsInBlocksConnected[i] = true;
			}
			if (IsInBlocksConnected[i] == 0)
				BlocksConnected.push_back(IDToAdd[i]);
		}
	}
	delete[] IDToAdd;
}

// Function to generate MA on Connected Block Sections when a train crosses a connected Node on a block section,
void lockSwitchesOnAllConnectedSections(double FrontEndPos, double BackEndPos, double V_i, double Acc_i, const Section& BS, const string& CurrentSectionID, const string& NextSectionID, const string& trainDescription, bool IsRouteReversed, const string& IDSectionToExclude) {
	// the parameter IDSectionToExclude is something that we put to exclude the connection with a particular block section. If we put IDSectionToExclude = "None", that means that we consider all connected block sections with signalling_block_sections. If IDSectionToExclude  is different from "None" then we will take all the connected blocks of signalling_block_sections apart from the one that we have excluded. In this way we limit listing redundant MAs if MAs relative to IDSectionToExclude have been already provided and/or listed somewhere else
	for (int i = 0; i < BS.total_arcs; i++) {

		list<string> ListBlocksConnected;

		if (IsRouteReversed == 0) {
			if (BS.arcs_in_signalling_block_section[i].endNode.numConnections > 0) {
				if ((FrontEndPos >= BS.arcs_in_signalling_block_section[i].endNode.X * 1000) && (BackEndPos <= BS.arcs_in_signalling_block_section[i].endNode.X * 1000)) { // if a train is crossing a Node corresponding to a switch edge
					// then create MA which blocks the accessto that switch on all connected block sections
					if (BS.arcs_in_signalling_block_section[i].endNode.IDConnectedBlocks.size() > 0) {
						for (list<string>::const_iterator j = BS.arcs_in_signalling_block_section[i].endNode.IDConnectedBlocks.begin(); j != BS.arcs_in_signalling_block_section[i].endNode.IDConnectedBlocks.end(); j++) {
							if (*j != IDSectionToExclude) {
								ListBlocksConnected.push_back(*j);
							}
						}
					}
				}
			}

		}

		else {
			if (BS.arcs_in_signalling_block_section[i].startNode.numConnections > 0) {
				if ((FrontEndPos >= BS.arcs_in_signalling_block_section[i].startNode.X * 1000) && (BackEndPos <= BS.arcs_in_signalling_block_section[i].startNode.X * 1000)) { // if a train is crossing a Node corresponding to a switch edge
					// then create MA which blocks the access to that switch on all connected block sections
					if (BS.arcs_in_signalling_block_section[i].startNode.IDConnectedBlocks.size() > 0) {
						for (list<string>::const_iterator j = BS.arcs_in_signalling_block_section[i].startNode.IDConnectedBlocks.begin(); j != BS.arcs_in_signalling_block_section[i].startNode.IDConnectedBlocks.end(); j++) {
							if (*j != IDSectionToExclude) {
								ListBlocksConnected.push_back(*j);
							}
						}
					}
				}
			}
		}

		if (ListBlocksConnected.size() > 0) {
			for (list<string>::iterator h = ListBlocksConnected.begin(); h != ListBlocksConnected.end(); h++) {
				MovementAuthority MA1, MA2, MA3;
				string tok1[10];
				int N_tok1 = 0;
				string BlockID1, BlockID2; // The ID of the first and the second blocks connected by the switch
				double SwitchStartPos, SwitchEndPos;
				string ConnectedBlockName = *h;
				char* p = strtok((char*)ConnectedBlockName.c_str(), "/-");
				while (p != NULL) {
					tok1[N_tok1] = p;
					p = strtok(NULL, "/-");
					N_tok1++;
				}
				BlockID1 = tok1[0] + "-" + tok1[1];
				BlockID2 = tok1[3] + "-" + tok1[4];
				SwitchStartPos = atof((char*)tok1[2].c_str());
				SwitchEndPos = atof((char*)tok1[5].c_str());
				// We do not need to block the switch edge connected to a non-diverging block connected with Block signalling_block_sections, but just the switch edge on Block signalling_block_sections and the switch edges on the connected Blocks with Switch Div=true. In this way we allow trains on non-diverging block connected with signalling_block_sections to go ahead flawlessly
				string RightSectionToConsider;
				double RightSwitchEdgeToConsider = 0.0;
				if (BlockID1 == BS.ID) {
					RightSectionToConsider = BlockID1;
					RightSwitchEdgeToConsider = SwitchStartPos;
				} else if (BlockID2 == BS.ID) {
					RightSectionToConsider = BlockID2;
					RightSwitchEdgeToConsider = SwitchEndPos;
				} else {
					cout << "\n\nError: there is not Block ahving the same ID of Block signalling_block_sections in Function: lockSwitchesOnAllConnectedSections...Please investigate on that\n\n";
				}
				// Defining MA1 on BlockID1
				MA1.BSID = RightSectionToConsider;
				MA1.AbsPosEoA = RightSwitchEdgeToConsider * 1000;
				MA1.ReversedDirection = false;
				MA1.TrainInfo.trainDescription = trainDescription;
				MA1.TrainInfo.TrainSpeed = V_i;
				MA1.TrainInfo.CurrentSectionID = CurrentSectionID;
				MA1.TrainInfo.NextSectionID = NextSectionID;
				MA1.type = "DivergingSwitchOccupied";
				MA1.TrainInfo.Acceleration = Acc_i;
				// Defining MA2 on BlockID2
				// MA2.BSID = BlockID2; MA2.AbsPosEoA = SwitchEndPos * 1000; MA2.ReversedDirection = false; MA2.TrainInfo.trainDescription = trainDescription; MA2.TrainInfo.TrainSpeed = V_i;
				// Defining MA3 on Connected Block With Switch Div
				MA2.BSID = *h;
				MA2.AbsPosEoA = SwitchStartPos * 1000;
				MA2.ReversedDirection = false;
				MA2.TrainInfo.trainDescription = trainDescription;
				MA2.TrainInfo.TrainSpeed = V_i;
				MA2.TrainInfo.CurrentSectionID = CurrentSectionID;
				MA2.TrainInfo.NextSectionID = NextSectionID;
				MA2.type = "DivergingSwitchOccupied";
				MA2.TrainInfo.Acceleration = Acc_i;
				// Defining MA4 on Connected Block With Switch Div
				MA3.BSID = *h;
				MA3.AbsPosEoA = SwitchEndPos * 1000;
				MA3.ReversedDirection = false;
				MA3.TrainInfo.trainDescription = trainDescription;
				MA3.TrainInfo.TrainSpeed = V_i;
				MA3.TrainInfo.CurrentSectionID = CurrentSectionID;
				MA3.TrainInfo.NextSectionID = NextSectionID;
				MA3.type = "DivergingSwitchOccupied";
				MA3.TrainInfo.Acceleration = Acc_i;
				// Send these MAs to the ETCS List of MAs
				list<MovementAuthority> ListofMA;
				ListofMA.push_back(MA1);
				ListofMA.push_back(MA2);
				ListofMA.push_back(MA3);
				for (list<MovementAuthority>::iterator k = ListofMA.begin(); k != ListofMA.end(); k++) {
					if (ETCS_MA.empty() == 1) {
						// Create a movement authority if the list is empty
						ETCS_MA.push_back(*k); // add the MA to the list
					} else {				   // if the list of ETCS MAs is not empty then check is this MA has been already reported
						bool IsMA_BSID_AlreadyThere = false;
						for (list<MovementAuthority>::iterator it = ETCS_MA.begin(); it != ETCS_MA.end(); it++) {
							if ((it->BSID == k->BSID) && (abs(it->AbsPosEoA - k->AbsPosEoA) < 0.001) && (it->TrainInfo.trainDescription == k->TrainInfo.trainDescription)) { // Because of rounding issues we consider that two MAs are the same if they belong to the same block section, they relate to the same train and their absolute position differs for less than 1 mm (i.e. 0.001 m)
								IsMA_BSID_AlreadyThere = true;
								break; // break the for loop over the EoAs
							}
						}
						// if the MA is already in ETCS MA then do not add it otherwise add it
						if (IsMA_BSID_AlreadyThere == 0) { // if the Movement authority was not there yet then add it to the ETCS_MA list
							ETCS_MA.push_back(*k);		   // add the MA to the list
						}
					}
				}
			}
		}
	}
}

// Function to Process Train Position and compute Movement Authorities
void elaborateRbcMas(double S_i, double V_i, double Acc_i, const Section& BLS, const string& CurrentSectionID, const string& NextSectionID, const string& trainDescription, const Route& TrainRoute, bool IsRouteReversed, const string& type, const string& typePart) {
	MovementAuthority MA;
	if (IsRouteReversed == 0) { // if the train route is not reversed
		MA.ReversedDirection = false;
		if (S_i < TrainRoute.sequence_of_block_sections[0].start_node.X) { // With this we ensure that if the tale of the train is still out of the simulation the first Node of the block section is protected
			MA.BSID = TrainRoute.sequence_of_block_sections[0].ID;
			MA.AbsPosEoA = TrainRoute.sequence_of_block_sections[0].GeoXBegNode;
			MA.EoA_Dist_From_BSID_Beg = 0;
		}

		else {
			MA.BSID = BLS.ID;
			MA.AbsPosEoA = S_i;
			MA.EoA_Dist_From_BSID_Beg = S_i - BLS.start_node.X * 1000;
		}
		MA.TrainInfo.trainDescription = trainDescription;
		MA.TrainInfo.Position = MA.AbsPosEoA;
		MA.TrainInfo.TrainSpeed = V_i;
		MA.TrainInfo.Acceleration = Acc_i;

	} else { // if instead the route is reversed
		MA.ReversedDirection = true;
		if (S_i < TrainRoute.sequence_of_block_sections[0].start_node.X) { // With this we ensure that if the tale of the train is still out of the simulation the first Node of the block section is protected
			MA.BSID = TrainRoute.sequence_of_block_sections[0].ID;
			MA.AbsPosEoA = TrainRoute.sequence_of_block_sections[0].GeoXBegNode;
			MA.EoA_Dist_From_BSID_Beg = 0;
		}

		else {
			MA.BSID = BLS.ID;
			MA.EoA_Dist_From_BSID_Beg = S_i - BLS.start_node.X * 1000;
			MA.AbsPosEoA = BLS.GeoXBegNode - MA.EoA_Dist_From_BSID_Beg;
		}
		MA.TrainInfo.trainDescription = trainDescription;
		MA.TrainInfo.Position = MA.AbsPosEoA;
		MA.TrainInfo.TrainSpeed = V_i;
		MA.TrainInfo.Acceleration = Acc_i;
	}

	// assigning the type of MA
	MA.type = type;
	MA.typePart = typePart;
	MA.TrainInfo.CurrentSectionID = CurrentSectionID;
	MA.TrainInfo.NextSectionID = NextSectionID;

	// Reporting the train position to the list of movement authorities
	if (ETCS_MA.empty() == 1) {
		// Create a movement authority if the list is empty
		ETCS_MA.push_back(MA); // add the MA to the list
	} else {				   // if the list of ETCS MAs is not empty then check is this MA has been already reported
		bool IsMA_BSID_AlreadyThere = false;
		for (list<MovementAuthority>::iterator it = ETCS_MA.begin(); it != ETCS_MA.end(); it++) {
			if ((it->BSID == MA.BSID) && (abs(it->AbsPosEoA - MA.AbsPosEoA) < 0.001) && (it->TrainInfo.trainDescription == MA.TrainInfo.trainDescription)) { // Because of rounding issues we consider that two MAs are the same if they belong to the same block section, they relate to the same train and their absolute position differs for less than 1 mm (i.e. 0.001 m)
				IsMA_BSID_AlreadyThere = true;
				break; // break the for loop over the EoAs
			}
		}
		// if the MA is already in ETCS MA then do not add it otherwise add it
		if (IsMA_BSID_AlreadyThere == 0) { // if the Movement authority was not there yet then add it to the ETCS_MA list
			ETCS_MA.push_back(MA);		   // add the MA to the list
		}
	}
}

// Function to Elaborate the MA on block sections with switches in diverging position
void elaborateMaOnBlockSectionsWithSwitchDiv(double S_i, double V_i, double Acc_i, const Section& BS, const string& trainDescription, const Route& TrainRoute, const string& typePart) {
	// the string of the next block section ID
	string NextSectionID;
	// Defining the token of this connected Block the first token is the ID of the first block section, the second token is instead the ID of the second block section
	string tok1[10];
	int N_tok1 = 0;
	string BlockID1, BlockID2; // The ID of the first and the second blocks connected by the switch
	string BSID = BS.ID;
	char* p = strtok((char*)BSID.c_str(), "/-");
	while (p != NULL) {
		tok1[N_tok1] = p;
		p = strtok(NULL, "/-");
		N_tok1++;
	}
	BlockID1 = tok1[0] + "-" + tok1[1];
	BlockID2 = tok1[3] + "-" + tok1[4];
	// Identify the TrackLine ID of BlockID1 and BlockID2;
	int TrackLineIDBlock1 = -1, TrackLineIDBlock2 = -1;
	// A simple (non switch-transition) block ID has only two tokens, so tok1[4]
	// is empty and strtok returns NULL; guard against atoi(NULL) dereferencing.
	{
		char* trackTok1 = strtok((char*)tok1[1].c_str(), "B@");
		char* trackTok2 = strtok((char*)tok1[4].c_str(), "B@");
		TrackLineIDBlock1 = trackTok1 ? atoi(trackTok1) : -1;
		TrackLineIDBlock2 = trackTok2 ? atoi(trackTok2) : -1;
	}

	// Identifying the next section ID
	for (int b = 0; b < TrainRoute.N_Block_Sections; b++) {
		if (TrainRoute.sequence_of_block_sections[b].ID == BS.ID) {
			if (b == TrainRoute.N_Block_Sections - 1) {
				NextSectionID = "None";
			} else {
				NextSectionID = TrainRoute.sequence_of_block_sections[b + 1].ID;
			}
			break; // When you have found the block section then you can break the for loop over the block sections
		}
	}

	// if TrackLineIDPrevBS is -1 or is equal to the trackLineId of the previous block section leave everything as it is otherwise
	if (TrainRoute.reversed_direction == 1) { // if the trackLineId of the second block section is equal to the one of the previous block section then invert BlockID1 and BlockID2, because this means that the Route of the train is reversed and the train is traversing the block section in the opposite direction to how the block section has been originally specified
		string TEMPBlockID;
		TEMPBlockID = BlockID2;
		BlockID2 = BlockID1;
		BlockID1 = TEMPBlockID;
	}

	// Conditions to occupy the blocks
	if (S_i < BS.XStartSwitch * 1000) { // if the abscissa S_i is on the first block section before entrying the switch then point out the first block section as occupied
		Section Block1;
		Block1 = BS;
		Block1.ID = BlockID1;
		elaborateRbcMas(S_i, V_i, Acc_i, Block1, BS.ID, NextSectionID, trainDescription, TrainRoute, TrainRoute.reversed_direction, "TrainEnd", typePart); // Occupying the first block section
		elaborateRbcMas(S_i, V_i, Acc_i, BS, BS.ID, NextSectionID, trainDescription, TrainRoute, TrainRoute.reversed_direction, "TrainEnd", typePart);	 // Occupying the block section with the diverging switch
	}
	// if the abscissa S_i is on the diverging switch then occupy all of the three block sections
	else if ((S_i >= BS.XStartSwitch * 1000) && (S_i < BS.XEndSwitch * 1000)) {
		// the first block section to occupy
		Section Block1;
		Block1 = BS;
		Block1.ID = BlockID1;
		// the second block section to occupy
		Section Block2;
		Block2 = BS;
		Block2.ID = BlockID2;

		elaborateRbcMas(S_i, V_i, Acc_i, Block1, BS.ID, NextSectionID, trainDescription, TrainRoute, TrainRoute.reversed_direction, "TrainEnd", typePart); // Occupying the first block section
		elaborateRbcMas(S_i, V_i, Acc_i, BS, BS.ID, NextSectionID, trainDescription, TrainRoute, TrainRoute.reversed_direction, "TrainEnd", typePart);	 // Occupying the block section with the diverging switch
		elaborateRbcMas(S_i, V_i, Acc_i, Block2, BS.ID, NextSectionID, trainDescription, TrainRoute, TrainRoute.reversed_direction, "TrainEnd", typePart); // Occupying the second block section

	}
	// else if the abscissa S is after the XEnd of the switch occupy the section with the diverging switch and the second block section
	else {
		Section Block2;
		Block2 = BS;
		Block2.ID = BlockID2;
		elaborateRbcMas(S_i, V_i, Acc_i, Block2, BS.ID, NextSectionID, trainDescription, TrainRoute, TrainRoute.reversed_direction, "TrainEnd", typePart); // Occupying the Second block section
		elaborateRbcMas(S_i, V_i, Acc_i, BS, BS.ID, NextSectionID, trainDescription, TrainRoute, TrainRoute.reversed_direction, "TrainEnd", typePart);	 // Occupying the block section with the diverging switch
	}
}

// Function to Block Switches when trains traverse them in the diverging position
void lockSwitchesWhileTrainTraverses(double FrontEndPos, double BackEndPos, double V_i, double Acc_i, const Section& BLS, const string& trainDescription, const Route& TrainRoute, const string& typePart) {
	// Defining the next block section ID on the route of the train after BLS
	string NextSectionID;

	string tok1[10];
	int N_tok1 = 0;
	string BlockID1, BlockID2; // The ID of the first and the second blocks connected by the switch
	string BSID = BLS.ID;
	char* p = strtok((char*)BSID.c_str(), "/-");
	while (p != NULL) {
		tok1[N_tok1] = p;
		p = strtok(NULL, "/-");
		N_tok1++;
	}
	BlockID1 = tok1[0] + "-" + tok1[1];
	BlockID2 = tok1[3] + "-" + tok1[4];
	// Identify the TrackLine ID of BlockID1 and BlockID2;
	int TrackLineIDBlock1 = -1, TrackLineIDBlock2 = -1;
	// A simple (non switch-transition) block ID has only two tokens, so tok1[4]
	// is empty and strtok returns NULL; guard against atoi(NULL) dereferencing.
	{
		char* trackTok1 = strtok((char*)tok1[1].c_str(), "B@");
		char* trackTok2 = strtok((char*)tok1[4].c_str(), "B@");
		TrackLineIDBlock1 = trackTok1 ? atoi(trackTok1) : -1;
		TrackLineIDBlock2 = trackTok2 ? atoi(trackTok2) : -1;
	}

	// Identifying the next section ID
	for (int b = 0; b < TrainRoute.N_Block_Sections; b++) {
		if (TrainRoute.sequence_of_block_sections[b].ID == BLS.ID) {
			if (b == TrainRoute.N_Block_Sections - 1) {
				NextSectionID = "None";
			} else {
				NextSectionID = TrainRoute.sequence_of_block_sections[b + 1].ID;
			}
			break; // When you have found the block section then you can break the for loop over the block sections
		}
	}

	// if TrackLineIDPrevBS is -1 or is equal to the trackLineId of the previous block section leave everything as it is otherwise
	if (TrainRoute.reversed_direction == 1) { // if the trackLineId of the second block section is equal to the one of the previous block section then invert BlockID1 and BlockID2, because this means that the Route of the train is reversed and the train is traversing the block section in the opposite direction to how the block section has been originally specified
		string TEMPBlockID;
		TEMPBlockID = BlockID2;
		BlockID2 = BlockID1;
		BlockID1 = TEMPBlockID;
	}

	// Condition to lock the Switch and make it unaccessible to other trains by putting a MA in correspondence of the beginning edge and the ending edge of the Switch
	if ((FrontEndPos >= BLS.XStartSwitch * 1000) && (BackEndPos <= BLS.XEndSwitch * 1000)) {

		double AbsStartSwitchPos = -1, AbsEndSwitchPos = -1;
		if (TrainRoute.reversed_direction == 0) {
			AbsStartSwitchPos = BLS.XStartSwitch;
			AbsEndSwitchPos = BLS.XEndSwitch;
		}

		else {
			double StartSwitchPosFromBeg = -1, EndSwitchPosFromBeg = -1;
			StartSwitchPosFromBeg = BLS.XStartSwitch - BLS.start_node.X;
			AbsStartSwitchPos = BLS.GeoXBegNode - StartSwitchPosFromBeg * 1000; // Absolute coordinate of the Start edge of the Switch
			EndSwitchPosFromBeg = BLS.XEndSwitch - BLS.start_node.X;
			AbsEndSwitchPos = BLS.GeoXBegNode - EndSwitchPosFromBeg * 1000; // Absolute coordinate of the End edge fo the Switch
		}

		Section Block1, Block2;
		bool Block1Found = false;
		bool Block2Found = false;
		for (int i = 0; i < Blocks; i++) { // signalling_block_sections is the set of all block section in the network model
			if (signalling_block_sections[i].ID == BlockID1) {
				Block1 = signalling_block_sections[i];
				Block1Found = true;
			}
			if (signalling_block_sections[i].ID == BlockID2) {
				Block2 = signalling_block_sections[i];
				Block2Found = true;
			}
			if ((Block1Found == 1) && (Block2Found == 1)) {
				break; // Found both block sections
			}
		}

		// locking start and end of the switch on the signalling_block_sections with SwitchDiv
		elaborateRbcMas(BLS.XStartSwitch * 1000, V_i, Acc_i, BLS, BLS.ID, NextSectionID, trainDescription, TrainRoute, TrainRoute.reversed_direction, "DivergingSwitchOccupied", typePart);
		elaborateRbcMas(BLS.XEndSwitch * 1000, V_i, Acc_i, BLS, BLS.ID, NextSectionID, trainDescription, TrainRoute, TrainRoute.reversed_direction, "DivergingSwitchOccupied", typePart);
		// Blocking the access to the beginning of the switch on Block 1 and connected to Block 1 apart from the BLS itself which has already been considered. We use the convention that the FrontEnd is 10m after and the BackEnd is 10m before the Switching Node
		elaborateRbcMas(AbsStartSwitchPos, V_i, Acc_i, Block1, BLS.ID, NextSectionID, trainDescription, TrainRoute, 0, "DivergingSwitchOccupied", typePart); // IsRouteReversed=0: blocks from AllBlocks list are not reversed
		lockSwitchesOnAllConnectedSections((AbsStartSwitchPos + 10), (AbsStartSwitchPos - 10), V_i, Acc_i, Block1, BLS.ID, NextSectionID, trainDescription, 0, BLS.ID); // The 0 here for IsRouteReversed means that Block1 is not reversed since we are taking it directly from the AllBlockSections
		// Blocking the access to the beginning of the switch on Block 2 and connected to Block 2 apart from the BLS itself which has already been considered. We use the convention that the FrontEnd is 10m after and the BackEnd is 10m before the Switching Node
		elaborateRbcMas(AbsEndSwitchPos, V_i, Acc_i, Block2, BLS.ID, NextSectionID, trainDescription, TrainRoute, 0, "DivergingSwitchOccupied", typePart);
		lockSwitchesOnAllConnectedSections((AbsEndSwitchPos + 10), (AbsEndSwitchPos - 10), V_i, Acc_i, Block2, BLS.ID, NextSectionID, trainDescription, 0, BLS.ID); // The 0 here for IsRouteReversed means that Block2 is not reversed since we are taking it directly from the AllBlockSections
	}
}

/************************************************************************************************************************************/

// Multithreaded version of BACC with coded track circuits for the cab-signalling (ETCS Level 0: 1 block section free behind a train)

void mTrackCircuitSs1(Section* BS, int Blocks) {
	for (int h = Blocks - 1; h >= 0; h--) {
		bool IsOccupied = false;
		list<string>::iterator it;
		// Determine if the Block Section is Occupied (i.e. check if its ID is present within the BlocksOccupied list)
		if (BlocksOccupied.empty() != 1) {
			for (it = BlocksOccupied.begin(); it != BlocksOccupied.end(); it++) {
				if (*it == BS[h].ID) {
					IsOccupied = true;
					break;
				}
			}
		}
		if (IsOccupied == 1) {
			if (h == 0) {
				BS[h].code = 0;
				BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
			} else if (h == 1) {
				BS[h].code = 0;
				BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;

				BS[h - 1].code = 751;
				strcpy_s(BS[h - 1].state, "red_red");
			} else if (h == 2) {
				BS[h].code = 0;
				BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;

				BS[h - 1].code = 751;
				strcpy_s(BS[h - 1].state, "red_red");

				BS[h - 2].code = 75;
				strcpy_s(BS[h - 2].state, "red");
			} else if (h == 3) {
				BS[h].code = 0;
				BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;

				BS[h - 1].code = 751;
				strcpy_s(BS[h - 1].state, "red_red");

				BS[h - 2].code = 75;
				strcpy_s(BS[h - 2].state, "red");

				BS[h - 3].code = 180;
				strcpy_s(BS[h - 3].state, "yellow");
			} else if (h >= 4) {

				BS[h].code = 0;
				BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;

				BS[h - 1].code = 751;
				strcpy_s(BS[h - 1].state, "red_red");

				BS[h - 2].code = 75;
				strcpy_s(BS[h - 2].state, "red");

				BS[h - 3].code = 180;
				strcpy_s(BS[h - 3].state, "yellow");

				BS[h - 4].code = 270;
				strcpy_s(BS[h - 4].state, "green");
			}
		}
	}
}

// Function to determine permitted speeds at the end of nodes of a Block Section (blockSets.A.connections.connections. with 1 free Block Section behind )
void setBlockSpeed1(double V_75, double V_751, double V_0, Section* BLS, int Blocks) {
	for (int h = Blocks - 2; h >= 0; h--) {
		BLS[h].arcs_in_signalling_block_section[BLS[h].total_arcs - 1].speedInBraking = BLS[h + 1].arcs_in_signalling_block_section[0].speedLimit;
		if (BLS[h].total_arcs >= 2) {
			for (int j = (BLS[h].total_arcs - 2); j >= 0; j--) {
				BLS[h].arcs_in_signalling_block_section[j].speedInBraking = BLS[h].arcs_in_signalling_block_section[j + 1].speedLimit;
			}
		}
	}
	BLS[Blocks - 1].arcs_in_signalling_block_section[BLS[Blocks - 1].total_arcs - 1].speedInBraking = BLS[Blocks - 1].arcs_in_signalling_block_section[BLS[Blocks - 1].total_arcs - 1].speedLimit;
	if (BLS[Blocks - 1].total_arcs >= 2) {
		for (int j = (BLS[Blocks - 1].total_arcs - 2); j >= 0; j--) {
			BLS[Blocks - 1].arcs_in_signalling_block_section[j].speedInBraking = BLS[Blocks - 1].arcs_in_signalling_block_section[j + 1].speedLimit;
		}
	}
	// Setting signal speeds and signal limits
	for (int h = Blocks - 2; h >= 0; h--) {

		if (BLS[h + 1].code == 270) {
			BLS[h].exit_speed = BLS[h].arcs_in_signalling_block_section[BLS[h].total_arcs - 1].speedInBraking;
			strcpy_s(BLS[h].state, "green");
			for (int k = 0; k < BLS[h].total_arcs; k++)
				BLS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
		}

		else if (BLS[h + 1].code == 751) {
			BLS[h].arcs_in_signalling_block_section[BLS[h].total_arcs - 1].speedInBraking = V_751;
			for (int k = 0; k < BLS[h].total_arcs; k++)
				BLS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = V_75;
		}

		else if (BLS[h + 1].code == 75) {
			BLS[h].arcs_in_signalling_block_section[BLS[h].total_arcs - 1].speedInBraking = V_75;
			for (int k = 0; k < BLS[h].total_arcs; k++)
				BLS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
		}

		else if (BLS[h + 1].code == 180) {
			BLS[h].exit_speed = BLS[h].arcs_in_signalling_block_section[BLS[h].total_arcs - 1].speedInBraking;
			for (int k = 0; k < BLS[h].total_arcs; k++)
				BLS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
		}

		else if (BLS[h + 1].code == 0) {
			BLS[h].arcs_in_signalling_block_section[BLS[h].total_arcs - 1].speedInBraking = V_0;
			for (int k = 0; k < BLS[h].total_arcs; k++)
				BLS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = V_751;
		}
	}
}

// Function to Release Block Sections when a train has passed over. This function must be applied especially for the block connected which are contained in the list BlocksConnected and when a train exits simulation
void releaseBlocksBacc(Section* BS, int Blocks) {
	list<string>::iterator it;
	if (BlocksConnected.empty() != 1) {
		for (it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
			for (int h = 0; h < Blocks; h++) {
				if (BS[h].ID == *it) {
					if (h == 0) {
						BS[h].code = 270;
					} else if (h == 1) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
					} else if (h == 2) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
					} else if (h == 3) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
						BS[h - 3].code = 270;
						strcpy_s(BS[h - 3].state, "green");
					} else if (h >= 4) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
						BS[h - 3].code = 270;
						strcpy_s(BS[h - 3].state, "green");
						BS[h - 4].code = 270;
						strcpy_s(BS[h - 4].state, "green");
					}
				}
			}
		}
	}
}

// Release of final Block Section when the train exits simulation
void relTrackCircuit1(Section* BS, int Blocks) {
	BS[Blocks - 1].code = 270;
	BS[Blocks - 2].code = 270;
	BS[Blocks - 3].code = 270;
	BS[Blocks - 4].code = 270;
}

/******************************************************************************************************************************************************/

// Multithreaded Version of ETCS Level 1 Signalling System - Italian Version (SCMT)

void mEtcsLev1(Section* BS, int Blocks) {

	for (int h = Blocks - 1; h >= 0; h--) {
		bool IsOccupied = false;
		list<string>::iterator it;
		// Determine if the Block Section is Occupied (i.e. check if its ID is present within the BlocksOccupied list)
		if (BlocksOccupied.empty() != 1) {
			for (it = BlocksOccupied.begin(); it != BlocksOccupied.end(); it++) {
				if (*it == BS[h].ID) {
					IsOccupied = true;
					break;
				}
			}
		}
		if (IsOccupied == 1) {
			if (h == 0) {
				BS[h].code = 0;
				BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
			}

			else if (h == 1) {
				BS[h].code = 0;
				BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;

				BS[h - 1].code = 75;
				strcpy_s(BS[h - 1].state, "red");
			} else if (h == 2) {
				BS[h].code = 0;
				BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;

				BS[h - 1].code = 75;
				strcpy_s(BS[h - 1].state, "red");

				BS[h - 2].code = 180;
				strcpy_s(BS[h - 2].state, "yellow");
			}

			else if (h >= 3) {
				BS[h].code = 0;
				BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;

				BS[h - 1].code = 75;
				strcpy_s(BS[h - 1].state, "red");

				BS[h - 2].code = 180;
				strcpy_s(BS[h - 2].state, "yellow");

				BS[h - 3].code = 270;
				strcpy_s(BS[h - 3].state, "green");
			}
		}
	}
}

// Function to determine permitted speeds at the end of nodes of a Block Section (ETCS level 1_SCMT)

void setBlockSpeedEtcsLev1(double V_0, Section* BS, int Blocks) {
	for (int h = Blocks - 2; h >= 0; h--) {
		BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking = BS[h + 1].arcs_in_signalling_block_section[0].speedLimit;
		if (BS[h].total_arcs >= 2) {
			for (int j = (BS[h].total_arcs - 2); j >= 0; j--) {
				BS[h].arcs_in_signalling_block_section[j].speedInBraking = BS[h].arcs_in_signalling_block_section[j + 1].speedLimit;
			}
		}
	}
	BS[Blocks - 1].arcs_in_signalling_block_section[BS[Blocks - 1].total_arcs - 1].speedInBraking = BS[Blocks - 1].arcs_in_signalling_block_section[BS[Blocks - 1].total_arcs - 1].speedLimit;
	if (BS[Blocks - 1].total_arcs >= 2) {
		for (int j = (BS[Blocks - 1].total_arcs - 2); j >= 0; j--) {
			BS[Blocks - 1].arcs_in_signalling_block_section[j].speedInBraking = BS[Blocks - 1].arcs_in_signalling_block_section[j + 1].speedLimit;
		}
	}
	// Setting signal speeds and signal limits
	for (int h = Blocks - 2; h >= 0; h--) {

		if (BS[h + 1].code == 270) {
			BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
			strcpy_s(BS[h].state, "green");
			for (int k = 0; k < BS[h].total_arcs; k++)
				BS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
		}

		else if (BS[h + 1].code == 75) {
			BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
			for (int k = 0; k < BS[h].total_arcs; k++)
				BS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
		}

		else if (BS[h + 1].code == 180) {
			for (int k = 0; k < BS[h].total_arcs; k++)
				BS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
		}

		else if (BS[h + 1].code == 0) {
			BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking = V_0;
			for (int k = 0; k < BS[h].total_arcs; k++)
				BS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
		}
	}
}

// Function to Release Block Sections when a train has passed over. This function must be applied especially for the block connected which are contained in the list BlocksConnected and when a train exits simulation
void releaseBlocksEtcsLev1(Section* BS, int Blocks) {
	list<string>::iterator it;
	if (BlocksConnected.empty() != 1) {
		for (it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
			for (int h = 0; h < Blocks; h++) {
				if (BS[h].ID == *it) {
					if (h == 0) {
						BS[h].code = 270;
					} else if (h == 1) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
					} else if (h == 2) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
					} else if (h >= 3) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
						BS[h - 3].code = 270;
						strcpy_s(BS[h - 3].state, "green");
					}
				}
			}
		}
	}
}

// Release of final Block Section when the train exits simulation
void relEtcsLev1(Section* BS, int Blocks) {
	BS[Blocks - 1].code = 270;
	BS[Blocks - 2].code = 270;
	BS[Blocks - 3].code = 270;
}


// Multithreaded Version of ATB Signalling system - National Dutch System. To select this system is necessary to set the variable Signalling_Level=2

void mAtb(Section* BS, int Blocks) {

	for (int h = Blocks - 1; h >= 0; h--) {
		bool IsOccupied = false;
		list<string>::iterator it;
		// Determine if the Block Section is Occupied (i.e. check if its ID is present within the BlocksOccupied list)
		if (BlocksOccupied.empty() != 1) {
			for (it = BlocksOccupied.begin(); it != BlocksOccupied.end(); it++) {
				if (*it == BS[h].ID) {
					IsOccupied = true;
					break;
				}
			}
		}
		if (IsOccupied == 1) {
			if (h == 0) {
				BS[h].code = 0;
				BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
			}

			else if (h == 1) {
				BS[h].code = 0;
				BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;

				BS[h - 1].code = 75;
				strcpy_s(BS[h - 1].state, "red");
			} else if (h == 2) {
				BS[h].code = 0;
				BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;

				BS[h - 1].code = 75;
				strcpy_s(BS[h - 1].state, "red");

				BS[h - 2].code = 180;
				strcpy_s(BS[h - 2].state, "yellow");
			}

			else if (h >= 3) {
				BS[h].code = 0;
				BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;

				BS[h - 1].code = 75;
				strcpy_s(BS[h - 1].state, "red");

				BS[h - 2].code = 180;
				strcpy_s(BS[h - 2].state, "yellow");

				BS[h - 3].code = 270;
				strcpy_s(BS[h - 3].state, "green");
			}
		}
	}
}

// Function to determine permitted speeds at the end of nodes of a Block Section (ATB - Dutch Signalling system)

void setBlockSpeedAtb(double V_75, double V_0, Section* BS, int Blocks) {
	for (int h = Blocks - 2; h >= 0; h--) {
		BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking = BS[h + 1].arcs_in_signalling_block_section[0].speedLimit;
		if (BS[h].total_arcs >= 2) {
			for (int j = (BS[h].total_arcs - 2); j >= 0; j--) {
				BS[h].arcs_in_signalling_block_section[j].speedInBraking = BS[h].arcs_in_signalling_block_section[j + 1].speedLimit;
			}
		}
	}
	BS[Blocks - 1].arcs_in_signalling_block_section[BS[Blocks - 1].total_arcs - 1].speedInBraking = BS[Blocks - 1].arcs_in_signalling_block_section[BS[Blocks - 1].total_arcs - 1].speedLimit;
	if (BS[Blocks - 1].total_arcs >= 2) {
		for (int j = (BS[Blocks - 1].total_arcs - 2); j >= 0; j--) {
			BS[Blocks - 1].arcs_in_signalling_block_section[j].speedInBraking = BS[Blocks - 1].arcs_in_signalling_block_section[j + 1].speedLimit;
		}
	}
	// Setting signal speeds and signal limits
	for (int h = Blocks - 2; h >= 0; h--) {

		if (BS[h + 1].code == 270) {
			BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
			strcpy_s(BS[h].state, "green");
			for (int k = 0; k < BS[h].total_arcs; k++)
				BS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
		}

		else if (BS[h + 1].code == 75) {
			BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking = V_75;
			for (int k = 0; k < BS[h].total_arcs; k++)
				BS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
		}

		else if (BS[h + 1].code == 180) {
			for (int k = 0; k < BS[h].total_arcs; k++)
				BS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
		}

		else if (BS[h + 1].code == 0) {
			BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking = V_0;
			for (int k = 0; k < BS[h].total_arcs; k++)
				BS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = V_75;
		}
	}
}

// Function to Release Block Sections when a train has passed over. This function must be applied especially for the block connected which are contained in the list BlocksConnected and when a train exits simulation
void releaseBlocksAtb(Section* BS, int Blocks) {
	list<string>::iterator it;
	if (BlocksConnected.empty() != 1) {
		for (it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
			for (int h = 0; h < Blocks; h++) {
				if (BS[h].ID == *it) {
					if (h == 0) {
						BS[h].code = 270;
					} else if (h == 1) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
					} else if (h == 2) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
					} else if (h >= 3) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
						BS[h - 3].code = 270;
						strcpy_s(BS[h - 3].state, "green");
					}
				}
			}
		}
	}
}

// Release of final Block Section when the train exits simulation
void relAtb(Section* BS, int Blocks) {
	BS[Blocks - 1].code = 270;
	BS[Blocks - 2].code = 270;
	BS[Blocks - 3].code = 270;
}

/************************************************************************************************************************************/

// Function to Simulate the provision of MAs from RBC to Train routes, the third parameter (IsROuteReversed) is the boolean reversed_direction of the route
void rbcSendsMasToRoute(Route& R) {
}

void setInfraSpeedInEtcs3(Section* BS, int Blocks) {
	for (int h = Blocks - 2; h >= 0; h--) {
		BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking = BS[h + 1].arcs_in_signalling_block_section[0].speedLimit;
		if (BS[h].total_arcs >= 2) {
			for (int j = (BS[h].total_arcs - 2); j >= 0; j--) {
				BS[h].arcs_in_signalling_block_section[j].speedInBraking = BS[h].arcs_in_signalling_block_section[j + 1].speedLimit;
			}
		}
	}
	BS[Blocks - 1].arcs_in_signalling_block_section[BS[Blocks - 1].total_arcs - 1].speedInBraking = BS[Blocks - 1].arcs_in_signalling_block_section[BS[Blocks - 1].total_arcs - 1].speedLimit;
	if (BS[Blocks - 1].total_arcs >= 2) {
		for (int j = (BS[Blocks - 1].total_arcs - 2); j >= 0; j--) {
			BS[Blocks - 1].arcs_in_signalling_block_section[j].speedInBraking = BS[Blocks - 1].arcs_in_signalling_block_section[j + 1].speedLimit;
		}
	}
}

// This function is basically the relase of the block sections used for the other signalling systems
void clearPreviousRbcMasForNewUpdating(Route& R) {
	for (int i = 0; i < R.N_Block_Sections; i++) {
		if (R.sequence_of_block_sections[i].N_ETCS3BrakingPoints > 0) {						 // if a block section had ETCS3BrakingPoints
			for (int j = 0; j < R.sequence_of_block_sections[i].N_ETCS3BrakingPoints; j++) { // then reset the state of the block section
				R.sequence_of_block_sections[i].ETCS3BrakingPoints[j] = -1;
				R.sequence_of_block_sections[i].ETCS3BrakingPointsTrainID[j] = "None";
			}
			R.sequence_of_block_sections[i].N_ETCS3BrakingPoints = 0; // Reset the number of ETCS3 braking points to 0
		}
	}
}

/****************************************************************************************************************************************/
// Function to Activate the signalling system according to the position of trains at a given time instant t
void activateSignallingSystem() {
	for (int i = 0; i < N_Routes; i++) {
		if (Signalling_Level == 0) {
			mTrackCircuitSs1(train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);
			setBlockSpeed1(signalCode1, signalCode2, signalCode3, train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);
		} else if (Signalling_Level == 1) {
			mEtcsLev1(train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);
			setBlockSpeedEtcsLev1(signalCode3, train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);
		} else if (Signalling_Level == 2) {
			mAtb(train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);
			setBlockSpeedAtb(signalCode1, signalCode3, train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);
		} else if (Signalling_Level == 3) {
			rbcSendsMasToRoute(train_route[i]);
			setInfraSpeedInEtcs3(train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);
		}
	}
}

// Function to release all the Block Sections which are occupied not because of the real presence of train above but just because they are connected with the block section that is really occupied
void releaseBlockConnections() {
	for (int t = 0; t < N_Routes; t++) {
		if (Signalling_Level == 0)
			releaseBlocksBacc(train_route[t].sequence_of_block_sections, train_route[t].N_Block_Sections);
		else if (Signalling_Level == 1)
			releaseBlocksEtcsLev1(train_route[t].sequence_of_block_sections, train_route[t].N_Block_Sections);
		else if (Signalling_Level == 2)
			releaseBlocksAtb(train_route[t].sequence_of_block_sections, train_route[t].N_Block_Sections);
		else if (Signalling_Level == 3)
			clearPreviousRbcMasForNewUpdating(train_route[t]);
	}
}

// Function to Set the infrastructure speed limits (i.e. the speed limits of the Arc that derive from the infrastructure and not from the signalling system)
void setInfraSpeedLimits(Section* BS, int Blocks) {
	for (int h = Blocks - 2; h >= 0; h--) {
		BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking = BS[h + 1].arcs_in_signalling_block_section[0].speedLimit;
		if (BS[h].total_arcs >= 2) {
			for (int j = (BS[h].total_arcs - 2); j >= 0; j--) {
				BS[h].arcs_in_signalling_block_section[j].speedInBraking = BS[h].arcs_in_signalling_block_section[j + 1].speedLimit;
			}
		}
	}
	BS[Blocks - 1].arcs_in_signalling_block_section[BS[Blocks - 1].total_arcs - 1].speedInBraking = BS[Blocks - 1].arcs_in_signalling_block_section[BS[Blocks - 1].total_arcs - 1].speedLimit;
	if (BS[Blocks - 1].total_arcs >= 2) {
		for (int j = (BS[Blocks - 1].total_arcs - 2); j >= 0; j--) {
			BS[Blocks - 1].arcs_in_signalling_block_section[j].speedInBraking = BS[Blocks - 1].arcs_in_signalling_block_section[j + 1].speedLimit;
		}
	}
}

void baccMixedSignalling(double V_75, double V_751, double V_0, Section* BS, int Blocks) {
	for (int h = Blocks - 1; h >= 0; h--) {
		bool IsOccupied = false;
		list<string>::iterator it;
		// Determine if the Block Section is Occupied (i.e. check if its ID is present within the BlocksOccupied list)
		if (BlocksOccupied.empty() != 1) {
			for (it = BlocksOccupied.begin(); it != BlocksOccupied.end(); it++) {
				if (*it == BS[h].ID) {
					IsOccupied = true;
					break;
				}
			}
		}
		if (IsOccupied == 1) {
			if (h == 0) {
				if (BS[h].SignallingLevel == 5) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
			} else if (h == 1) {
				if (BS[h].SignallingLevel == 5) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
				if (BS[h - 1].SignallingLevel == 5) {
					BS[h - 1].code = 751;
					strcpy_s(BS[h - 1].state, "red_red");
					for (int k = 0; k < BS[h - 1].total_arcs; k++) {
						BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = V_751;
					}
				}
			}

			else if (h == 2) {
				if (BS[h].SignallingLevel == 5) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
				if (BS[h - 1].SignallingLevel == 5) {
					BS[h - 1].code = 751;
					strcpy_s(BS[h - 1].state, "red_red");
					for (int k = 0; k < BS[h - 1].total_arcs; k++) {
						BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = V_751;
					}
				}
				if (BS[h - 2].SignallingLevel == 5) {
					BS[h - 2].code = 75;
					strcpy_s(BS[h - 2].state, "red");
					for (int k = 0; k < BS[h - 2].total_arcs; k++) {
						BS[h - 2].arcs_in_signalling_block_section[k].signalSpeedLimit = V_75;
					}
				}
			}

			else if (h == 3) {
				if (BS[h].SignallingLevel == 5) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
				if (BS[h - 1].SignallingLevel == 5) {
					BS[h - 1].code = 751;
					strcpy_s(BS[h - 1].state, "red_red");
					for (int k = 0; k < BS[h - 1].total_arcs; k++) {
						BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = V_751;
					}
				}
				if (BS[h - 2].SignallingLevel == 5) {
					BS[h - 2].code = 75;
					strcpy_s(BS[h - 2].state, "red");
					for (int k = 0; k < BS[h - 2].total_arcs; k++) {
						BS[h - 2].arcs_in_signalling_block_section[k].signalSpeedLimit = V_75;
					}
				}
				if (BS[h - 3].SignallingLevel == 5) {
					BS[h - 3].code = 180;
					strcpy_s(BS[h - 3].state, "yellow");
					for (int k = 0; k < BS[h - 3].total_arcs; k++) {
						BS[h - 3].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
			}

			else if (h >= 4) {
				if (BS[h].SignallingLevel == 5) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
				if (BS[h - 1].SignallingLevel == 5) {
					BS[h - 1].code = 751;
					strcpy_s(BS[h - 1].state, "red_red");
					for (int k = 0; k < BS[h - 1].total_arcs; k++) {
						BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = V_751;
					}
				}
				if (BS[h - 2].SignallingLevel == 5) {
					BS[h - 2].code = 75;
					strcpy_s(BS[h - 2].state, "red");
					for (int k = 0; k < BS[h - 2].total_arcs; k++) {
						BS[h - 2].arcs_in_signalling_block_section[k].signalSpeedLimit = V_75;
					}
				}
				if (BS[h - 3].SignallingLevel == 5) {
					BS[h - 3].code = 180;
					strcpy_s(BS[h - 3].state, "yellow");
					for (int k = 0; k < BS[h - 3].total_arcs; k++) {
						BS[h - 3].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
				if (BS[h - 4].SignallingLevel == 5) {
					BS[h - 4].code = 270;
					strcpy_s(BS[h - 4].state, "green");
					for (int k = 0; k < BS[h - 4].total_arcs; k++) {
						BS[h - 4].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
			}
		}
	}
}

// Function to determine permitted speeds at the end of nodes of a Block Section (blockSets.A.connections.connections. with 1 free Block Section behind )
void setBlockSpeed1MixedSignalling(Section* BLS, int Blocks) {
	// Setting signal speeds
	for (int h = Blocks - 2; h >= 0; h--) {
		if (BLS[h].SignallingLevel == 5) {
			// if the block section in front of BLS[h] is occupied by a train then the speed limit at the end of the section must be 0
			if (BLS[h + 1].code == 0) {
				BLS[h].arcs_in_signalling_block_section[BLS[h].total_arcs - 1].speedInBraking = 0;
			} else {
				double MinSpeedLim = BLS[h + 1].arcs_in_signalling_block_section[0].speedLimit;
				if (BLS[h + 1].arcs_in_signalling_block_section[0].signalSpeedLimit < MinSpeedLim)
					MinSpeedLim = BLS[h + 1].arcs_in_signalling_block_section[0].signalSpeedLimit;
				// the speed limit at the end of block section BLS[h] is MinSpeedLim
				BLS[h].arcs_in_signalling_block_section[BLS[h].total_arcs - 1].speedInBraking = MinSpeedLim;
			}
		}
	}
}

// Function to Release Block Sections when a train has passed over. This function must be applied especially for the block connected which are contained in the list BlocksConnected and when a train exits simulation
void releaseBlocksBaccMixedSignalling(Section* BS, int Blocks) {
	list<string>::iterator it;
	if (BlocksConnected.empty() != 1) {
		for (it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
			for (int h = 0; h < Blocks; h++) {
				if (BS[h].ID == *it) {
					if (h == 0) {
						BS[h].code = 270;
					} else if (h == 1) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
					} else if (h == 2) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
					} else if (h == 3) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
						BS[h - 3].code = 270;
						strcpy_s(BS[h - 3].state, "green");
					} else if (h >= 4) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
						BS[h - 3].code = 270;
						strcpy_s(BS[h - 3].state, "green");
						BS[h - 4].code = 270;
						strcpy_s(BS[h - 4].state, "green");
					}
				}
			}
		}
	}
}

// Release of final Block Section when the train exits simulation
void relTrackCircuit1MixedSignalling(Section* BS, int blockIndex) {
	BS[blockIndex].code = 270;
	if ((blockIndex - 1) >= 0) {
		BS[blockIndex - 1].code = 270;
	}
	if ((blockIndex - 2) >= 0) {
		BS[blockIndex - 2].code = 270;
	}
	if ((blockIndex - 3) >= 0) {
		BS[blockIndex - 3].code = 270;
	}
}

// Multithreaded Version of ATB Signalling system - National Dutch System.

void atbMixedSignalling(double V_75, double V_0, Section* BS, int Blocks) {

	for (int h = Blocks - 1; h >= 0; h--) {
		bool IsOccupied = false;
		list<string>::iterator it;
		// Determine if the Block Section is Occupied (i.e. check if its ID is present within the BlocksOccupied list)
		if (BlocksOccupied.empty() != 1) {
			for (it = BlocksOccupied.begin(); it != BlocksOccupied.end(); it++) {
				if (*it == BS[h].ID) {
					IsOccupied = true;
					break;
				}
			}
		}

		if (IsOccupied == 1) {
			if (h == 0) {
				if (BS[h].SignallingLevel == 0) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
			}

			else if (h == 1) {
				if (BS[h].SignallingLevel == 0) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
				if (BS[h - 1].SignallingLevel == 0) {
					BS[h - 1].code = 75;
					strcpy_s(BS[h - 1].state, "red");
					for (int k = 0; k < BS[h - 1].total_arcs; k++) {
						BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = V_75;
					}
				}
			} else if (h == 2) {
				if (BS[h].SignallingLevel == 0) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
				if (BS[h - 1].SignallingLevel == 0) {
					BS[h - 1].code = 75;
					strcpy_s(BS[h - 1].state, "red");
					for (int k = 0; k < BS[h - 1].total_arcs; k++) {
						BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = V_75;
					}
				}
				if (BS[h - 2].SignallingLevel == 0) {
					BS[h - 2].code = 180;
					strcpy_s(BS[h - 2].state, "yellow");
					for (int k = 0; k < BS[h - 2].total_arcs; k++) {
						BS[h - 2].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
			}

			else if (h >= 3) {
				if (BS[h].SignallingLevel == 0) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
				if (BS[h - 1].SignallingLevel == 0) {
					BS[h - 1].code = 75;
					strcpy_s(BS[h - 1].state, "red");
					for (int k = 0; k < BS[h - 1].total_arcs; k++) {
						BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = V_75;
					}
				}
				if (BS[h - 2].SignallingLevel == 0) {
					BS[h - 2].code = 180;
					strcpy_s(BS[h - 2].state, "yellow");
					for (int k = 0; k < BS[h - 2].total_arcs; k++) {
						BS[h - 2].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
				if (BS[h - 3].SignallingLevel == 0) {
					BS[h - 3].code = 270;
					strcpy_s(BS[h - 3].state, "green");
					for (int k = 0; k < BS[h - 3].total_arcs; k++) {
						BS[h - 3].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
			}
		}
	}
}

// Function to determine permitted speeds at the end of nodes of a Block Section (ATB - Dutch Signalling system)

void setBlockSpeedAtbMixedSignalling(Section* BS, int Blocks) {
	// Setting signal limit speeds
	for (int h = Blocks - 2; h >= 0; h--) {

		if (BS[h].SignallingLevel == 0) {
			// if the block section in front of BLS[h] is occupied by a train then the speed limit at the end of the section must be 0
			if (BS[h + 1].code == 0) {
				BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking = 0;
			} else {
				double MinSpeedLim = BS[h + 1].arcs_in_signalling_block_section[0].speedLimit;
				if (BS[h + 1].arcs_in_signalling_block_section[0].signalSpeedLimit < MinSpeedLim)
					MinSpeedLim = BS[h + 1].arcs_in_signalling_block_section[0].signalSpeedLimit;
				// the speed limit at the end of block section BLS[h] is MinSpeedLim
				BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking = MinSpeedLim;
			}
		}
	}
}

// Function to Release Block Sections when a train has passed over. This function must be applied especially for the block connected which are contained in the list BlocksConnected and when a train exits simulation
void releaseBlocksAtbMixedSignalling(Section* BS, int Blocks) {
	list<string>::iterator it;
	if (BlocksConnected.empty() != 1) {
		for (it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
			for (int h = 0; h < Blocks; h++) {
				if (BS[h].ID == *it) {
					if (h == 0) {
						BS[h].code = 270;
					} else if (h == 1) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
					} else if (h == 2) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
					} else if (h >= 3) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
						BS[h - 3].code = 270;
						strcpy_s(BS[h - 3].state, "green");
					}
				}
			}
		}
	}
}

// Release of final Block Section when the train exits simulation
void relAtbMixedSignalling(Section* BS, int blockIndex) {
	BS[blockIndex].code = 270;
	if ((blockIndex - 1) >= 0) {
		BS[blockIndex - 1].code = 270;
	}
	if ((blockIndex - 2) >= 0) {
		BS[blockIndex - 2].code = 270;
	}
}

// Multithreaded Version of ETCS Level 1 Signalling System - Italian Version (SCMT)

void etcsLev1MixedSignalling(double V_0, Section* BS, int Blocks) {

	for (int h = Blocks - 1; h >= 0; h--) {
		bool IsOccupied = false;
		list<string>::iterator it;
		// Determine if the Block Section is Occupied (i.e. check if its ID is present within the BlocksOccupied list)
		if (BlocksOccupied.empty() != 1) {
			for (it = BlocksOccupied.begin(); it != BlocksOccupied.end(); it++) {
				if (*it == BS[h].ID) {
					IsOccupied = true;
					break;
				}
			}
		}
		if (IsOccupied == 1) {
			if (h == 0) {
				if (BS[h].SignallingLevel == 1) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
			}

			else if (h == 1) {
				if (BS[h].SignallingLevel == 1) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
				if (BS[h - 1].SignallingLevel == 1) {
					BS[h - 1].code = 75;
					strcpy_s(BS[h - 1].state, "red");
					for (int k = 0; k < BS[h - 1].total_arcs; k++) {
						BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
			} else if (h == 2) {
				if (BS[h].SignallingLevel == 1) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
				if (BS[h].SignallingLevel == 1) {
					BS[h - 1].code = 75;
					strcpy_s(BS[h - 1].state, "red");
					for (int k = 0; k < BS[h - 1].total_arcs; k++) {
						BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
				if (BS[h].SignallingLevel == 1) {
					BS[h - 2].code = 180;
					strcpy_s(BS[h - 2].state, "yellow");
					for (int k = 0; k < BS[h - 2].total_arcs; k++) {
						BS[h - 2].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
			}

			else if (h >= 3) {
				if (BS[h].SignallingLevel == 1) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
				if (BS[h].SignallingLevel == 1) {
					BS[h - 1].code = 75;
					strcpy_s(BS[h - 1].state, "red");
					for (int k = 0; k < BS[h - 1].total_arcs; k++) {
						BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
				if (BS[h].SignallingLevel == 1) {
					BS[h - 2].code = 180;
					strcpy_s(BS[h - 2].state, "yellow");
					for (int k = 0; k < BS[h - 2].total_arcs; k++) {
						BS[h - 2].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
				if (BS[h].SignallingLevel == 1) {
					BS[h - 3].code = 270;
					strcpy_s(BS[h - 3].state, "green");
					for (int k = 0; k < BS[h - 2].total_arcs; k++) {
						BS[h - 2].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
			}
		}
	}
}

// Function to determine permitted speeds at the end of nodes of a Block Section (ETCS level 1_SCMT)

void setBlockSpeedEtcsLev1MixedSignalling(Section* BS, int Blocks) {
	// Setting signal limit speeds
	for (int h = Blocks - 2; h >= 0; h--) {

		if (BS[h].SignallingLevel == 1) {
			// if the block section in front of BLS[h] is occupied by a train then the speed limit at the end of the section must be 0
			if (BS[h + 1].code == 0) {
				BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking = 0;
			} else {
				double MinSpeedLim = BS[h + 1].arcs_in_signalling_block_section[0].speedLimit;
				if (BS[h + 1].arcs_in_signalling_block_section[0].signalSpeedLimit < MinSpeedLim)
					MinSpeedLim = BS[h + 1].arcs_in_signalling_block_section[0].signalSpeedLimit;
				// the speed limit at the end of block section BLS[h] is MinSpeedLim
				BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking = MinSpeedLim;
			}
		}
	}
}

// Function to Release Block Sections when a train has passed over. This function must be applied especially for the block connected which are contained in the list BlocksConnected and when a train exits simulation
void releaseBlocksEtcsLev1MixedSignalling(Section* BS, int Blocks) {
	list<string>::iterator it;
	if (BlocksConnected.empty() != 1) {
		for (it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
			for (int h = 0; h < Blocks; h++) {
				if (BS[h].ID == *it) {
					if (h == 0) {
						BS[h].code = 270;
					} else if (h == 1) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
					} else if (h == 2) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
					} else if (h >= 3) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
						BS[h - 3].code = 270;
						strcpy_s(BS[h - 3].state, "green");
					}
				}
			}
		}
	}
}

// Release of final Block Section when the train exits simulation
void relEtcsLev1MixedSignalling(Section* BS, int blockIndex) {
	BS[blockIndex].code = 270;
	if ((blockIndex - 1) >= 0) {
		BS[blockIndex - 1].code = 270;
	}
	if ((blockIndex - 2) >= 0) {
		BS[blockIndex - 2].code = 270;
	}
}

void etcsLev2MixedSignalling(double V_0, Section* BS, int Blocks) {

	for (int h = Blocks - 1; h >= 0; h--) {
		bool IsOccupied = false;
		list<string>::iterator it;
		// Determine if the Block Section is Occupied (i.e. check if its ID is present within the BlocksOccupied list)
		if (BlocksOccupied.empty() != 1) {
			for (it = BlocksOccupied.begin(); it != BlocksOccupied.end(); it++) {
				if (*it == BS[h].ID) {
					IsOccupied = true;
					break;
				}
			}
		}
		if (IsOccupied == 1) {
			if (h == 0) {
				if (BS[h].SignallingLevel == 2) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
			}

			else if (h == 1) {
				if (BS[h].SignallingLevel == 2) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
				if (BS[h - 1].SignallingLevel == 2) {
					BS[h - 1].code = 75;
					strcpy_s(BS[h - 1].state, "red");
					for (int k = 0; k < BS[h - 1].total_arcs; k++) {
						BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
			} else if (h == 2) {
				if (BS[h].SignallingLevel == 2) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
				if (BS[h].SignallingLevel == 2) {
					BS[h - 1].code = 75;
					strcpy_s(BS[h - 1].state, "red");
					for (int k = 0; k < BS[h - 1].total_arcs; k++) {
						BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
				if (BS[h].SignallingLevel == 2) {
					BS[h - 2].code = 180;
					strcpy_s(BS[h - 2].state, "yellow");
					for (int k = 0; k < BS[h - 2].total_arcs; k++) {
						BS[h - 2].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
			}

			else if (h >= 3) {
				if (BS[h].SignallingLevel == 2) {
					BS[h].code = 0;
					BS[h].exit_speed = BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking;
				}
				if (BS[h].SignallingLevel == 2) {
					BS[h - 1].code = 75;
					strcpy_s(BS[h - 1].state, "red");
					for (int k = 0; k < BS[h - 1].total_arcs; k++) {
						BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
				if (BS[h].SignallingLevel == 2) {
					BS[h - 2].code = 180;
					strcpy_s(BS[h - 2].state, "yellow");
					for (int k = 0; k < BS[h - 2].total_arcs; k++) {
						BS[h - 2].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
				if (BS[h].SignallingLevel == 2) {
					BS[h - 3].code = 270;
					strcpy_s(BS[h - 3].state, "green");
					for (int k = 0; k < BS[h - 2].total_arcs; k++) {
						BS[h - 2].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}
				}
			}
		}
	}
}

// Function to determine permitted speeds at the end of nodes of a Block Section (ETCS level 1_SCMT)

void setBlockSpeedEtcsLev2MixedSignalling(Section* BS, int Blocks) {
	// Setting signal limit speeds
	for (int h = Blocks - 2; h >= 0; h--) {

		if (BS[h].SignallingLevel == 2) {
			// if the block section in front of BLS[h] is occupied by a train then the speed limit at the end of the section must be 0
			if (BS[h + 1].code == 0) {
				BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking = 0;
			} else {
				double MinSpeedLim = BS[h + 1].arcs_in_signalling_block_section[0].speedLimit;
				if (BS[h + 1].arcs_in_signalling_block_section[0].signalSpeedLimit < MinSpeedLim)
					MinSpeedLim = BS[h + 1].arcs_in_signalling_block_section[0].signalSpeedLimit;
				// the speed limit at the end of block section BLS[h] is MinSpeedLim
				BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking = MinSpeedLim;
			}
		}
	}
}

// Function to Release Block Sections when a train has passed over. This function must be applied especially for the block connected which are contained in the list BlocksConnected and when a train exits simulation
void releaseBlocksEtcsLev2MixedSignalling(Section* BS, int Blocks) {
	list<string>::iterator it;
	if (BlocksConnected.empty() != 1) {
		for (it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
			for (int h = 0; h < Blocks; h++) {
				if (BS[h].ID == *it) {
					if (h == 0) {
						BS[h].code = 270;
					} else if (h == 1) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
					} else if (h == 2) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
					} else if (h >= 3) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
						BS[h - 3].code = 270;
						strcpy_s(BS[h - 3].state, "green");
					}
				}
			}
		}
	}
}

// Release of final Block Section when the train exits simulation
void relEtcsLev2MixedSignalling(Section* BS, int blockIndex) {
	BS[blockIndex].code = 270;
	if ((blockIndex - 1) >= 0) {
		BS[blockIndex - 1].code = 270;
	}
	if ((blockIndex - 2) >= 0) {
		BS[blockIndex - 2].code = 270;
	}
}

// Function to Simulate the provision of MAs from RBC to Train routes, the third parameter (IsROuteReversed) is the boolean reversed_direction of the route
void rbcSendsMasToRouteMixedSignalling(Route& R) {
	// Occupying the block section in order to be seen by the other signalling system as well
	for (int h = R.N_Block_Sections - 1; h >= 0; h--) {
		bool IsOccupied = false;
		list<string>::iterator it;
		// Determine if the Block Section is Occupied (i.e. check if its ID is present within the BlocksOccupied list)
		if (BlocksOccupied.empty() != 1) {
			for (it = BlocksOccupied.begin(); it != BlocksOccupied.end(); it++) {
				if (*it == R.sequence_of_block_sections[h].ID) {
					IsOccupied = true;
					break;
				}
			}
		}
		if (IsOccupied == 1) {
			if ((R.sequence_of_block_sections[h].SignallingLevel == 3) || (R.sequence_of_block_sections[h].SignallingLevel == 4)) {
				R.sequence_of_block_sections[h].code = 0;
				R.sequence_of_block_sections[h].exit_speed = R.sequence_of_block_sections[h].arcs_in_signalling_block_section[R.sequence_of_block_sections[h].total_arcs - 1].speedInBraking;
			}
		}
	}
}

void manageEtcs3TransitionsToOtherSignalling(Section* BS, int Blocks) {
	// Setting signal limit speeds
	for (int h = Blocks - 2; h >= 0; h--) {

		if ((BS[h].SignallingLevel == 3) || (BS[h].SignallingLevel == 4)) {
			// if the block section in front of BLS[h] is occupied by a train and is not equipped with ETCS 3 then the train must stop at the end of signalling_block_sections[h]
			if ((BS[h + 1].code == 0) && (BS[h + 1].SignallingLevel != 3) && (BS[h + 1].SignallingLevel != 4)) {
				BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking = 0;
			} else { // In all the other cases instead the speed at the end of the block is the minimum between the infra speed and the signal speed limit of teh next block
				double MinSpeedLim = BS[h + 1].arcs_in_signalling_block_section[0].speedLimit;
				if (BS[h + 1].arcs_in_signalling_block_section[0].signalSpeedLimit < MinSpeedLim)
					MinSpeedLim = BS[h + 1].arcs_in_signalling_block_section[0].signalSpeedLimit;
				// the speed limit at the end of block section BLS[h] is MinSpeedLim
				BS[h].arcs_in_signalling_block_section[BS[h].total_arcs - 1].speedInBraking = MinSpeedLim;
			}
		}
	}
}

// Function to Release Block Sections when a train has passed over. This function must be applied especially for the block connected which are contained in the list BlocksConnected and when a train exits simulation
void releaseBlocksMixedSignalling(Section* BS, int Blocks) {
	list<string>::iterator it;
	if (BlocksConnected.empty() != 1) {
		for (it = BlocksConnected.begin(); it != BlocksConnected.end(); it++) {
			for (int h = 0; h < Blocks; h++) {
				if (BS[h].ID == *it) {
					if (h == 0) {
						BS[h].code = 270;

						// clear signalSpeedLimit restrictions
						for (int k = 0; k < BS[h].total_arcs; k++) {
							BS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
						}
					} else if (h == 1) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");

						// clear signalSpeedLimit restrictions
						for (int k = 0; k < BS[h].total_arcs; k++) {
							BS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
						}
						for (int k = 0; k < BS[h - 1].total_arcs; k++) {
							BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
						}
					} else if (h == 2) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");

						// clear signalSpeedLimit restrictions
						for (int k = 0; k < BS[h].total_arcs; k++) {
							BS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
						}
						for (int k = 0; k < BS[h - 1].total_arcs; k++) {
							BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
						}
						for (int k = 0; k < BS[h - 2].total_arcs; k++) {
							BS[h - 2].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
						}
					} else if (h == 3) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
						BS[h - 3].code = 270;
						strcpy_s(BS[h - 3].state, "green");

						// clear signalSpeedLimit restrictions
						for (int k = 0; k < BS[h].total_arcs; k++) {
							BS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
						}
						for (int k = 0; k < BS[h - 1].total_arcs; k++) {
							BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
						}
						for (int k = 0; k < BS[h - 2].total_arcs; k++) {
							BS[h - 2].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
						}
						for (int k = 0; k < BS[h - 3].total_arcs; k++) {
							BS[h - 3].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
						}
					} else if (h >= 4) {
						BS[h].code = 270;
						BS[h - 1].code = 270;
						strcpy_s(BS[h - 1].state, "green");
						BS[h - 2].code = 270;
						strcpy_s(BS[h - 2].state, "green");
						BS[h - 3].code = 270;
						strcpy_s(BS[h - 3].state, "green");
						BS[h - 4].code = 270;
						strcpy_s(BS[h - 4].state, "green");

						// clear signalSpeedLimit restrictions
						for (int k = 0; k < BS[h].total_arcs; k++) {
							BS[h].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
						}
						for (int k = 0; k < BS[h - 1].total_arcs; k++) {
							BS[h - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
						}
						for (int k = 0; k < BS[h - 2].total_arcs; k++) {
							BS[h - 2].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
						}
						for (int k = 0; k < BS[h - 3].total_arcs; k++) {
							BS[h - 3].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
						}
						for (int k = 0; k < BS[h - 4].total_arcs; k++) {
							BS[h - 4].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
						}
					}
				}
			}
		}
	}
}

/***********************************************************************************************************************************************************************************************************/

// Function to activate mixed signalling areas
void activateMixedSignallingSystem() {
	for (int i = 0; i < N_Routes; i++) {
		// Set all the speed limits coming from the Infrastructure
		setInfraSpeedLimits(train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);

		// Set the aspect of signals and Signal Speed Limits for BACC
		baccMixedSignalling(signalCode1, signalCode2, signalCode3, train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);
		setBlockSpeed1MixedSignalling(train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);

		// Set the aspect of signals and signal speed limits for ATB
		atbMixedSignalling(signalCode1, signalCode3, train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);
		setBlockSpeedAtbMixedSignalling(train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);

		// Set MA and signalling speed limits for ETCS Level 1
		etcsLev1MixedSignalling(signalCode3, train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);
		setBlockSpeedEtcsLev1MixedSignalling(train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);

		// Set MA and signalling speed limits for ETCS Level 2
		etcsLev2MixedSignalling(signalCode3, train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);
		setBlockSpeedEtcsLev2MixedSignalling(train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);

		// Set MA and signalling speed limits for ETCS Level 3
		rbcSendsMasToRouteMixedSignalling(train_route[i]);
		manageEtcs3TransitionsToOtherSignalling(train_route[i].sequence_of_block_sections, train_route[i].N_Block_Sections);
	}
}

// Function to Release all block sections in mixed signalling areas
void releaseMixedSignallingSystem() {
	for (int t = 0; t < N_Routes; t++) {
		releaseBlocksMixedSignalling(train_route[t].sequence_of_block_sections, train_route[t].N_Block_Sections);
	}
}

// Function to Release the last block section in mixed signalling
void relLastSectionMixedSignalling(string blockID) {
	// check all routes
	for (int r = 0; r < N_Routes; r++) {
		for (int b = 0; b < train_route[r].N_Block_Sections; b++) {
			// check if route contains this signalling_block_sections
			if (train_route[r].sequence_of_block_sections[b].ID == blockID) {
				if (train_route[r].sequence_of_block_sections[b].SignallingLevel == 0) {
					relAtbMixedSignalling(train_route[r].sequence_of_block_sections, b);
				} else if (train_route[r].sequence_of_block_sections[b].SignallingLevel == 1) {
					relEtcsLev1MixedSignalling(train_route[r].sequence_of_block_sections, b);
				} else if (train_route[r].sequence_of_block_sections[b].SignallingLevel == 2) {
					relEtcsLev2MixedSignalling(train_route[r].sequence_of_block_sections, b);
				}

				// Here the function for ETCS Level 3 shall also be added if needed

				else if (train_route[r].sequence_of_block_sections[b].SignallingLevel == 5) {
					relTrackCircuit1MixedSignalling(train_route[r].sequence_of_block_sections, b);
				}

				// proceed to next route
				break;
			}
		}
	}
}

// Function to Debug
void debugFunctionBlockCodes(int t, string BSID, const Route& R) {
	ofstream output;
	string outputname;
	outputname = outputname + InputMainFolder + "/TrackLines/DebugBlockCodes.txt";
	output.open((char*)outputname.c_str(), ios::app);
	for (int i = 0; i < R.N_Block_Sections; i++) {
		if (R.sequence_of_block_sections[i].ID == BSID) {
			output << t << " " << R.sequence_of_block_sections[i].ID << " " << R.sequence_of_block_sections[i].code << " " << R.sequence_of_block_sections[i].arcs_in_signalling_block_section[R.sequence_of_block_sections[i].total_arcs - 1].speedInBraking << " " << R.sequence_of_block_sections[i - 1].ID << " " << R.sequence_of_block_sections[i - 1].code << " " << R.sequence_of_block_sections[i - 1].arcs_in_signalling_block_section[R.sequence_of_block_sections[i - 1].total_arcs - 1].speedInBraking << " " << R.sequence_of_block_sections[i - 2].ID << " " << R.sequence_of_block_sections[i - 2].code << " " << R.sequence_of_block_sections[i - 2].arcs_in_signalling_block_section[R.sequence_of_block_sections[i - 2].total_arcs - 1].speedInBraking << "\n";
		}
	}
	output.close();
}

void showElement(int t, list<string> blockSets) {
	ofstream output;
	string outputname;
	outputname = outputname + InputMainFolder + "/TrackLines/ShowElements.txt";
	output.open((char*)outputname.c_str(), ios::app);
	output << t << " ";
	if (blockSets.empty() != 1) {
		list<string>::iterator it;
		for (it = blockSets.begin(); it != blockSets.end(); it++) {
			output << *it << " ";
		}
	}
	output << "\n";
	output.close();
}

void showElementInEtcsMa(int t) {
	ofstream output;
	string outputname;
	outputname = outputname + InputMainFolder + "/TrackLines/ShowElementsETCS_MA_List.txt";
	output.open((char*)outputname.c_str(), ios::app);
	output << t << " ";
	if (ETCS_MA.empty() != 1) {

		for (list<MovementAuthority>::iterator it = ETCS_MA.begin(); it != ETCS_MA.end(); it++) {
			output << it->BSID << " " << it->AbsPosEoA << " ";
		}
	}
	output << "\n";
	output.close();
}

void printBlocksAndConnections() {
	ofstream output;
	string outputname;
	outputname = outputname + InputMainFolder + "/TrackLines/BlocksAndConnections.txt";
	output.open((char*)outputname.c_str());

	for (int i = 0; i < Blocks; i++) {
		output << signalling_block_sections[i].ID << " ";
		for (int j = 0; j < signalling_block_sections[i].N_ConnectedBS; j++) {
			output << signalling_block_sections[i].IDConnectedBS[j] << " ";
		}
		output << "\n";
	}
	output.close();
}

// function to set mid-signals of double switches as virtual signals
void setVirtualSignals() {
	for (int i = 0; i < Blocks; i++) {
		// 'virtual' signals (in the middle of double switches)
		if (signalling_block_sections[i].trackLineId != -1 && blockSets[signalling_block_sections[i].trackLineId].len == 3 && fabs(blockSets[signalling_block_sections[i].trackLineId].member[blockSets[signalling_block_sections[i].trackLineId].len - 1].endNode.X - blockSets[signalling_block_sections[i].trackLineId].member[0].startNode.X - 0.030) < 0.0001) {
			// end_node is virtual
			if (signalling_block_sections[i].start_node.X == blockSets[signalling_block_sections[i].trackLineId].member[0].startNode.X) {
				signalling_block_sections[i].end_node.virtualSignal = true;
				// do the same for its connected signalling_block_sections (compound signalling_block_sections)
				for (int c = 0; c < signalling_block_sections[i].N_ConnectedBS; c++) {
					for (int j = 0; j < Blocks; j++) {
						if (signalling_block_sections[j].ID == signalling_block_sections[i].IDConnectedBS[c]) {
							signalling_block_sections[j].end_node.virtualSignal = true;
							break;
						}
					}
				}
			}
			// start_node is virtual
			else {
				signalling_block_sections[i].start_node.virtualSignal = true;
				// do the same for its connected signalling_block_sections (compound signalling_block_sections)
				for (int c = 0; c < signalling_block_sections[i].N_ConnectedBS; c++) {
					for (int j = 0; j < Blocks; j++) {
						if (signalling_block_sections[j].ID == signalling_block_sections[i].IDConnectedBS[c]) {
							signalling_block_sections[j].start_node.virtualSignal = true;
							break;
						}
					}
				}
			}
		}
	}
}

// set route corridors from input file
void loadRouteCorridors() {
	// read corridors from file
	ifstream corridorsFile(InputMainFolder + "/GUI/caseStudyRouteCorridors.txt");
	if (corridorsFile.is_open()) {
		std::string line;
		while (getline(corridorsFile, line)) {
			// ignore empty lines (in the end of the files)
			if (line.empty())
				continue;

			std::string token;
			std::vector<std::string> tokens;
			char delimiter = '\t';
			std::istringstream tokenStream(line);
			while (std::getline(tokenStream, token, delimiter)) {
				tokens.push_back(token);
			}

			// set corridor
			int routeID = std::stoi(tokens[0]);
			train_route[routeID].corridor = tokens[1];
		}
	}
}

// set virtual signals on routes from original signalling_block_sections (information is missed when creating routes)
void setRouteVirtualSignals() {
	for (int r = 0; r < N_Routes; r++) {
		for (int b = 0; b < train_route[r].N_Block_Sections; b++) {
			for (int i = 0; i < Blocks; i++) {
				if (train_route[r].sequence_of_block_sections[b].ID == signalling_block_sections[i].ID) {
					// rev route
					if (train_route[r].reversed_direction) {
						if (signalling_block_sections[i].start_node.virtualSignal) {
							train_route[r].sequence_of_block_sections[b].end_node.virtualSignal = true;
						}
						if (signalling_block_sections[i].end_node.virtualSignal) {
							train_route[r].sequence_of_block_sections[b].start_node.virtualSignal = true;
						}
					}
					// non-rev route
					else {
						if (signalling_block_sections[i].start_node.virtualSignal) {
							train_route[r].sequence_of_block_sections[b].start_node.virtualSignal = true;
						}
						if (signalling_block_sections[i].end_node.virtualSignal) {
							train_route[r].sequence_of_block_sections[b].end_node.virtualSignal = true;
						}
					}
					break;
				}
			}
		}
	}
}

// vector containing the limits of single tracks (pair of first/last plain signalling_block_sections IDs, train occupying single track, signalling_block_sections IDs to block)
std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string>> singleTrackLimits;

// load single track limits
void loadSingleTrackLimits() {
	// read limits from file
	ifstream singleTrackFile(InputMainFolder + "/GUI/singleTrackLimits.txt");
	if (singleTrackFile.is_open()) {
		std::string line;
		while (getline(singleTrackFile, line)) {
			// ignore empty lines (in the end of the files)
			if (line.empty())
				continue;

			std::string token;
			std::vector<std::string> tokens;
			char delimiter = '\t';
			std::istringstream tokenStream(line);
			while (std::getline(tokenStream, token, delimiter)) {
				tokens.push_back(token);
			}

			// add limits (signalling_block_sections ID begin, signalling_block_sections ID end) to vector
			singleTrackLimits.push_back(std::make_tuple(tokens[0], tokens[1], "", tokens[2], tokens[3])); // empty string meaning single track is free
		}
	}
}

// load station boundary sections
void loadStationBoundarySections() {
	// read sections from file
	ifstream entranceSectionFile(InputMainFolder + "/GUI/stationBoundarySections.txt");
	if (entranceSectionFile.is_open()) {
		std::string line;
		while (getline(entranceSectionFile, line)) {
			// ignore empty lines (in the end of the files)
			if (line.empty())
				continue;

			std::string token;
			std::vector<std::string> tokens;
			char delimiter = '\t';
			std::istringstream tokenStream(line);
			while (std::getline(tokenStream, token, delimiter)) {
				tokens.push_back(token);
			}

			// only entrance section
			if (tokens[1].empty()) {
				for (int i = 0; i < Blocks; i++) {
					if (tokens[0] == signalling_block_sections[i].ID) {
						// add section to vector
						StationBoundarySection boundarySection(&signalling_block_sections[i], std::stoi(tokens[2]), nullptr);
						stationBoundarySections.push_back(boundarySection);
						break;
					}
				}
			}
			// both entrance and exit sections
			else {
				int entranceIndex = -1, exitIndex = -1;
				for (int i = 0; i < Blocks; i++) {
					if (tokens[0] == signalling_block_sections[i].ID) {
						entranceIndex = i;
					} else if (tokens[1] == signalling_block_sections[i].ID) {
						exitIndex = i;
					}
					if (entranceIndex != -1 && exitIndex != -1) {
						break;
					}
				}
				// add section to vector (skip if either ID was not found in signalling_block_sections)
				if (entranceIndex != -1 && exitIndex != -1) {
					StationBoundarySection boundarySection(&signalling_block_sections[entranceIndex], std::stoi(tokens[2]), &signalling_block_sections[exitIndex]);
					stationBoundarySections.push_back(boundarySection);
				}
			}
		}
	}
}

// constructor
StationBoundarySection::StationBoundarySection(Section* entrance, bool direction, Section* exit)
	: entrance(entrance), direction(direction), exit(exit) {}

// function to protect station interlocking area
void StationBoundarySection::protectEntrance(int sectionIndex, int routeIndex, bool platformBooked) {
	bool needToBlock = false;

	if (platformBooked == false && train_route[routeIndex].sequence_of_block_sections[sectionIndex].ID == entrance->ID && direction == train_route[routeIndex].reversed_direction) {
		std::vector<std::string> sectionsAhead;

		// collect sections ahead
		for (int i = sectionIndex + 1; i < train_route[routeIndex].N_Block_Sections; i++) {
			sectionsAhead.push_back(train_route[routeIndex].sequence_of_block_sections[i].ID);

			// reached exit of station protected area
			if (exit && exit->ID == train_route[routeIndex].sequence_of_block_sections[i].ID) {
				break;
			}
		}

		// block entrance if any signalling_block_sections ahead is occupied
		for (int i = 0; i < sectionsAhead.size(); i++) {
			for (auto occ = BlocksOccupied.begin(); occ != BlocksOccupied.end(); ++occ) {
				if (sectionsAhead[i] == *occ) {
					needToBlock = true;
					break;
				}
			}
			if (needToBlock) {
				break;
			}
		}
	}

	if (platformBooked == true || needToBlock == true) {
		// block section ahead in route of train
		int h = sectionIndex, Prev_Block = h - 1;
		if (h == 0)
			Prev_Block = 0;

		occupyBlockAndConnected(train_route[routeIndex].sequence_of_block_sections[h], train_route[routeIndex].sequence_of_block_sections[Prev_Block], -1, -1);

		return;
	}

	// release section ahead if still occupied because of protection on previous timestep
	releaseLastBlockAndConnected(train_route[routeIndex].sequence_of_block_sections[sectionIndex]);
}

// vector containing the station entrance sections
std::vector<StationBoundarySection> stationBoundarySections;

// dictionary to save signal aspect from previous timestep (used to avoid messing signalling functions)
std::map<std::string, int> signalAspects;

// print signal aspect changes over time to be used in the offline visualizer
void saveSignalAspectChanges(int t, std::string FolderName) {
	std::map<std::string, int> aspectsToPrint;
	std::string FileName = FolderName + "/SignallingOutput.txt";

	// fill dictionary (first time, all green aspects)
	if (signalAspects.empty()) {
		for (int r = 0; r < N_Routes; r++) {
			for (int i = 0; i < train_route[r].N_Block_Sections; i++) {
				// plain signalling_block_sections
				if (train_route[r].sequence_of_block_sections[i].ID.find('/') == std::string::npos) {
					signalAspects.insert({train_route[r].sequence_of_block_sections[i].ID, (int)train_route[r].sequence_of_block_sections[i].code});
				}
				// signalling_block_sections with switch
				else {
					int index1 = train_route[r].sequence_of_block_sections[i].ID.find("@-");
					int index2 = train_route[r].sequence_of_block_sections[i].ID.find("/@", index1 + 1);
					int index3 = train_route[r].sequence_of_block_sections[i].ID.find("@-", index2 + 1);
					if (index1 != std::string::npos && index2 != std::string::npos && index3 != std::string::npos) {
						std::string ID1 = train_route[r].sequence_of_block_sections[i].ID.substr(0, index1 + 1);
						signalAspects.insert({ID1, (int)train_route[r].sequence_of_block_sections[i].code});

						std::string ID2 = train_route[r].sequence_of_block_sections[i].ID.substr(index2 + 1, index3 - index2);
						signalAspects.insert({ID2, (int)train_route[r].sequence_of_block_sections[i].code});
					}
				}
			}
		}

		// erase output file (new simulation)
		std::ofstream FileOutput;
		FileOutput.open((char*)FileName.c_str(), std::ios::binary | std::ios::trunc); // overwrite
		FileOutput.close();
	}
	// check and save aspect changes
	else {
		for (int r = 0; r < N_Routes; r++) {
			for (int i = 0; i < train_route[r].N_Block_Sections; i++) {
				// plain signalling_block_sections
				if (train_route[r].sequence_of_block_sections[i].ID.find('/') == std::string::npos) {
					// aspect changed
					if (signalAspects.find(train_route[r].sequence_of_block_sections[i].ID)->second != train_route[r].sequence_of_block_sections[i].code) {
						// not yet stored - save aspect
						if (aspectsToPrint.find(train_route[r].sequence_of_block_sections[i].ID) == aspectsToPrint.end()) {
							aspectsToPrint.insert({train_route[r].sequence_of_block_sections[i].ID, (int)train_route[r].sequence_of_block_sections[i].code});
						}
						// update if more severe code
						else if (train_route[r].sequence_of_block_sections[i].code < aspectsToPrint[train_route[r].sequence_of_block_sections[i].ID]) {
							aspectsToPrint[train_route[r].sequence_of_block_sections[i].ID] = train_route[r].sequence_of_block_sections[i].code;
						}
					}
					// for the cases where aspectToPrint is filled with less severe code than previous but there are routes with previous code
					else if (aspectsToPrint.find(train_route[r].sequence_of_block_sections[i].ID) != aspectsToPrint.end() && train_route[r].sequence_of_block_sections[i].code < aspectsToPrint[train_route[r].sequence_of_block_sections[i].ID]) {
						aspectsToPrint[train_route[r].sequence_of_block_sections[i].ID] = train_route[r].sequence_of_block_sections[i].code;
					}
				}
				// signalling_block_sections with switch
				else {
					int index1 = train_route[r].sequence_of_block_sections[i].ID.find("@-");
					int index2 = train_route[r].sequence_of_block_sections[i].ID.find("/@", index1 + 1);
					int index3 = train_route[r].sequence_of_block_sections[i].ID.find("@-", index2 + 1);
					if (index1 != std::string::npos && index2 != std::string::npos && index3 != std::string::npos) {
						std::string ID1 = train_route[r].sequence_of_block_sections[i].ID.substr(0, index1 + 1);
						// aspect changed
						if (signalAspects.find(ID1)->second != train_route[r].sequence_of_block_sections[i].code) {
							// not yet stored - save aspect
							if (aspectsToPrint.find(ID1) == aspectsToPrint.end()) {
								aspectsToPrint.insert({ID1, (int)train_route[r].sequence_of_block_sections[i].code});
							}
							// update if more severe code
							else if (train_route[r].sequence_of_block_sections[i].code < aspectsToPrint[ID1]) {
								aspectsToPrint[ID1] = train_route[r].sequence_of_block_sections[i].code;
							}
						}
						// for the cases where aspectToPrint is filled with less severe code than previous but there are routes with previous code
						else if (aspectsToPrint.find(ID1) != aspectsToPrint.end() && train_route[r].sequence_of_block_sections[i].code < aspectsToPrint[ID1]) {
							aspectsToPrint[ID1] = train_route[r].sequence_of_block_sections[i].code;
						}

						std::string ID2 = train_route[r].sequence_of_block_sections[i].ID.substr(index2 + 1, index3 - index2);
						// aspect changed
						if (signalAspects.find(ID2)->second != train_route[r].sequence_of_block_sections[i].code) {
							// not yet stored - save aspect
							if (aspectsToPrint.find(ID2) == aspectsToPrint.end()) {
								aspectsToPrint.insert({ID2, (int)train_route[r].sequence_of_block_sections[i].code});
							}
							// update if more severe code
							else if (train_route[r].sequence_of_block_sections[i].code < aspectsToPrint[ID2]) {
								aspectsToPrint[ID2] = train_route[r].sequence_of_block_sections[i].code;
							}
						}
						// for the cases where aspectToPrint is filled with less severe code than previous but there are routes with previous code
						else if (aspectsToPrint.find(ID2) != aspectsToPrint.end() && train_route[r].sequence_of_block_sections[i].code < aspectsToPrint[ID2]) {
							aspectsToPrint[ID2] = train_route[r].sequence_of_block_sections[i].code;
						}
					}
				}
			}
		}
	}

	// open output file
	std::ofstream FileOutput;
	FileOutput.open((char*)FileName.c_str(), std::ios::binary | std::ios::app); // append

	if (FileOutput.is_open()) {
		for (auto it = aspectsToPrint.begin(); it != aspectsToPrint.end(); ++it) {
			if (signalAspects[it->first] != it->second) {
				// print to file
				FileOutput << t << "\t" << it->first << "\t" << it->second << "\n";

				// update dictionary
				signalAspects[it->first] = it->second;
			}
		}
		FileOutput.close();
	} else // error opening file
	{
		std::cout << "Error opening file to write signalling output\n";
	}
}

std::vector<SimulationIncident> simulationIncidents;

void Load_Incidents(const std::string& MainFolder) {
	std::string filePath = MainFolder + "/Incidents.txt";
	std::ifstream file(filePath);
	if (!file.is_open()) {
		return; // Silent if no incidents file
	}

	std::string line;
	int lineNo = 0;
	while (std::getline(file, line)) {
		++lineNo;
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty())
			continue;

		// The file is tab separated and a target (a station or signal name) may
		// contain spaces, so split on tabs rather than on any whitespace.
		std::istringstream iss(line);
		SimulationIncident inc;
		std::string startField, endField;
		if (!std::getline(iss, inc.type, '\t') || !std::getline(iss, inc.target, '\t') ||
			!std::getline(iss, startField, '\t') || !std::getline(iss, endField)) {
			std::cout << "Warning: malformed incident line " << lineNo << " skipped.\n";
			continue;
		}
		try {
			inc.startSeconds = std::stod(startField);
			inc.endSeconds = std::stod(endField);
		} catch (const std::exception&) {
			std::cout << "Warning: malformed incident line " << lineNo << " skipped.\n";
			continue;
		}

		if (inc.type == "signal_failure") {
			std::string targetToken = "@" + inc.target + "@";
			bool resolved = false;
			for (int b = 0; b < Blocks; ++b) {
				if (::signalling_block_sections[b].ID.find(targetToken) != std::string::npos) {
					inc.resolvedSectionIDs.push_back(::signalling_block_sections[b].ID);
					resolved = true;
				}
			}
			if (!resolved) {
				std::cout << "Warning: signal_failure incident target " << inc.target << " matches no section.\n";
			}
		}
		simulationIncidents.push_back(inc);
	}
}

bool Incident_Holds_Train(const std::string& trainDesc, int timestepIndex) {
	for (const auto& inc : simulationIncidents) {
		// trainDescription is "<service>-<n>", so a service target holds every repeat
		if (inc.type == "train_breakdown" && timestepIndex >= inc.startSeconds && timestepIndex < inc.endSeconds && trainDesc.rfind(inc.target + "-", 0) == 0) {
			return true;
		}
	}
	return false;
}
