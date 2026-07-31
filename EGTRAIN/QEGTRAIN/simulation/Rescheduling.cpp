#include "simulation/Rescheduling.h"

#include <cmath>
#include <fstream>
#include "util/Util.hpp"

/*************************************************************************************************************************************************/

// Definition of Objects to convert EGTRAIN infrastructure, timetable and rolling stock to real-time traffic rescheduling algorithms

/*************************************************************************************************************************************************/

// Definition of objects for converting EGTRAIN inputs into real-time traffic rescheduling tool RECIFE

std::list<TopologySequence> All_Topology_Sequences;

std::list<Arc> Additional_Arcs_To_Create_TDS;

// This function fills in the Unused TopoParts In TopoSequence list for the TDS which are on switches, hence having unused TopoParts when crossed in a direction rather than another
void fillinUnusedTopoPartsOfTopoSequencesForTdsOnSwitches(list<TopologySequence>& All_TopoSequences) {
	if (All_TopoSequences.size() != 0) {
		for (auto i = All_TopoSequences.begin(); i != All_TopoSequences.end(); i++) {
			for (auto j = All_TopoSequences.begin(); j != All_TopoSequences.end(); j++) {
				// if the Topology Sequences i and j are two different sequences of the same TDS
				if ((j->TDS_ID == i->TDS_ID) && (j->ID != i->ID)) {
					// then add the list of TopoParts which are not used in a sequence to the list of unused TopoParts of the other sequence
					for (auto h = j->TopologyPart_List.begin(); h != j->TopologyPart_List.end(); h++) {
						bool IsTopoPartAlreadyThere = false;
						list<TopologyPart>::iterator k = i->TopologyPart_List.begin();

						while (k != i->TopologyPart_List.end()) {
							// if the TopoPart of Topology Sequence j is already in Topology Sequence i
							if ((k->ID == h->ID) && (k->StartX == h->StartX) && (k->EndX == h->EndX)) {
								// Then set boolean value to true
								IsTopoPartAlreadyThere = true;
								// and break the for loop cycle over k
								break;
							}
							k++;
						}

						// if the Topology Part of j is not in Topology Sequence i, then add it to the list of unused TopoParts in i
						if (IsTopoPartAlreadyThere == 0) {
							i->Unused_TopoParts_In_Sequence.push_back(*h);
						}
					}
				}
			}
		}
	}
}

void initialiseTopologySequencesForRecife(Section* Blocks, int N_Blocks, list<TDS> All_TDS, list<TopologySequence>& AllTopoSequences) {
	for (auto it = All_TDS.begin(); it != All_TDS.end(); it++) {

		for (int i = 0; i < N_Blocks; i++) {
			if (Blocks[i].TDS_in_block.empty() != 1) {
				for (auto td = Blocks[i].TDS_in_block.begin(); td != Blocks[i].TDS_in_block.end(); td++) {
					string BlockTDS_ID = (*td)->ID;
					if (it->ID == BlockTDS_ID) {
						if (Blocks[i].withSwitchDiv == 0) {
							TopologySequence TS;
							TS.TDS_ID = it->ID;
							TS.ID = TS.TDS_ID + "_straight";
							TS.blocksection_ID = Blocks[i].ID;

							int counter = 0; // this counts the number of Topologyparts in the TDS
							// defining topologypart
							for (int j = 0; j < Blocks[i].total_arcs; j++) {

								if (((Blocks[i].arcs_in_signalling_block_section[j].startNode.X >= (*td)->start_node.X) && (Blocks[i].arcs_in_signalling_block_section[j].startNode.X < (*td)->end_node.X)) && ((Blocks[i].arcs_in_signalling_block_section[j].endNode.X > (*td)->start_node.X) && (Blocks[i].arcs_in_signalling_block_section[j].endNode.X <= (*td)->end_node.X))) {
									// When both the start and end Node of the arcs are within the TDS boundaries
									// we should take the whole Arc

									counter++;
									TopologyPart TDSPart;
									TDSPart.ID = (*td)->ID + "_a_" + to_string(counter);
									TDSPart.TDS_arc = &Blocks[i].arcs_in_signalling_block_section[j];

									// specifying the X and Y coordinates of the TDSPart
									TDSPart.trackLineId = Blocks[i].trackLineId;
									TDSPart.StartX = TDSPart.TDS_arc->startNode.X;
									TDSPart.StartY = TDSPart.trackLineId;
									TDSPart.EndX = TDSPart.TDS_arc->endNode.X;
									TDSPart.EndY = TDSPart.trackLineId;

									TS.TopologyPart_List.push_back(TDSPart);

								}

								// if the end start Node of the Arc is lower than the start of the TDS then take only the part after the start Node of the TDS
								else if ((Blocks[i].arcs_in_signalling_block_section[j].startNode.X < (*td)->start_node.X) && ((Blocks[i].arcs_in_signalling_block_section[j].endNode.X <= (*td)->end_node.X) && (Blocks[i].arcs_in_signalling_block_section[j].endNode.X > (*td)->start_node.X))) {

									counter++;
									TopologyPart TDSPart;
									TDSPart.ID = (*td)->ID + "_a_" + to_string(counter);

									// create a copy of the Arc
									Arc ArcCopy = Blocks[i].arcs_in_signalling_block_section[j];

									Additional_Arcs_To_Create_TDS.push_back(ArcCopy);

									list<Arc>::iterator EndElement = Additional_Arcs_To_Create_TDS.end();
									EndElement--;

									EndElement->startNode = (*td)->start_node;

									// the length should be changed accordingly as the Arc is cut at the end
									EndElement->length = EndElement->endNode.X - EndElement->startNode.X;
									TDSPart.TDS_arc = &(*EndElement);

									// specifying the X and Y coordinates of the TDSPart
									TDSPart.trackLineId = Blocks[i].trackLineId;
									TDSPart.StartX = TDSPart.TDS_arc->startNode.X;
									TDSPart.StartY = TDSPart.trackLineId;
									TDSPart.EndX = TDSPart.TDS_arc->endNode.X;
									TDSPart.EndY = TDSPart.trackLineId;

									TS.TopologyPart_List.push_back(TDSPart);

								}

								else if (((Blocks[i].arcs_in_signalling_block_section[j].startNode.X >= (*td)->start_node.X) && (Blocks[i].arcs_in_signalling_block_section[j].startNode.X < (*td)->end_node.X)) && (Blocks[i].arcs_in_signalling_block_section[j].endNode.X > (*td)->end_node.X)) {

									counter++;
									TopologyPart TDSPart;
									TDSPart.ID = (*td)->ID + "_a_" + to_string(counter);

									// create a copy of the Arc
									Arc ArcCopy = Blocks[i].arcs_in_signalling_block_section[j];

									Additional_Arcs_To_Create_TDS.push_back(ArcCopy);

									list<Arc>::iterator EndElement = Additional_Arcs_To_Create_TDS.end();
									EndElement--;

									EndElement->endNode = (*td)->end_node;

									// the length should be changed accordingly as the Arc is cut at the end
									EndElement->length = EndElement->endNode.X - EndElement->startNode.X;
									TDSPart.TDS_arc = &(*EndElement);

									// specifying the X and Y coordinates of the TDSPart
									TDSPart.trackLineId = Blocks[i].trackLineId;
									TDSPart.StartX = TDSPart.TDS_arc->startNode.X;
									TDSPart.StartY = TDSPart.trackLineId;
									TDSPart.EndX = TDSPart.TDS_arc->endNode.X;
									TDSPart.EndY = TDSPart.trackLineId;

									TS.TopologyPart_List.push_back(TDSPart);

								}

								else if ((Blocks[i].arcs_in_signalling_block_section[j].startNode.X < (*td)->start_node.X) && (Blocks[i].arcs_in_signalling_block_section[j].endNode.X > (*td)->end_node.X)) { // if instead the TDS is shorter than the Arc, i.e. if both the start and end of the Arc are outside of the start and end Node of the TDS

									counter++;
									TopologyPart TDSPart;
									TDSPart.ID = (*td)->ID + "_a_" + to_string(counter);

									// create a copy of the Arc
									Arc ArcCopy = Blocks[i].arcs_in_signalling_block_section[j];

									Additional_Arcs_To_Create_TDS.push_back(ArcCopy);

									list<Arc>::iterator EndElement = Additional_Arcs_To_Create_TDS.end();
									EndElement--;

									EndElement->startNode = (*td)->start_node; // cut the start of the Arc with the start Node of the TDS
									EndElement->endNode = (*td)->end_node;	// cut the end of the Arc with end Node of the TDS

									// the length should be changed accordingly as the Arc is cut at the end
									EndElement->length = EndElement->endNode.X - EndElement->startNode.X;
									TDSPart.TDS_arc = &(*EndElement);

									// specifying the X and Y coordinates of the TDSPart
									TDSPart.trackLineId = Blocks[i].trackLineId;
									TDSPart.StartX = TDSPart.TDS_arc->startNode.X;
									TDSPart.StartY = TDSPart.trackLineId;
									TDSPart.EndX = TDSPart.TDS_arc->endNode.X;
									TDSPart.EndY = TDSPart.trackLineId;

									TS.TopologyPart_List.push_back(TDSPart);
								}
							}

							AllTopoSequences.push_back(TS);
						}
						// From here we need to write the conditions to create TopoSequence for block sections with diverging switch
						// the conditions will be exactly the same as for those without switch apart from the fact that
						// if the trackline of the TDS is equal to the FirstTrackLine of the block section then the Arc with the switch is the last one
						// and the TopologySequence ID can be TDS_ID_Diverging
						// else if it is equal to the Secondtrackline of the Block section, then the topologySequence ID is TDS_ID Converging
						// Things to be done is to add variable x and y to the topo
						else { // if the block section has a diverging switch

							TopologySequence TS;
							TS.TDS_ID = it->ID;
							TS.blocksection_ID = Blocks[i].ID;
							double TDS_StartNodeX = -1;
							double TDS_EndNodeX = -1;
							bool TDSOnFirstTrackLine = 0;

							// check whether the TDS trackline ID is the same of the first or the second connected trackline of the block section
							if (it->TracklineID == Blocks[i].FirstConnectedTrackLineID) {
								TS.ID = TS.TDS_ID + "_diverging";
								// if the TDS ID has the ID of the FirstConnectBlock section then the start Node of the TDS is the actual start Node
								// and the end Node is the Node on the switch
								TDS_StartNodeX = it->start_node.X;
								TDS_EndNodeX = it->node_on_switch.X;
								TDSOnFirstTrackLine = 1;

							} else if (it->TracklineID == Blocks[i].SecondConnectedTrackLineID) {
								TS.ID = TS.TDS_ID + "_converging";
								// if the TDS has the TrackLine ID of the second connected TrackLine then the beginning switch is the Node of the switch and the end Node is the actual end Node of the TDS
								TDS_StartNodeX = it->node_on_switch.X;
								TDS_EndNodeX = it->end_node.X;
							}

							int counter = 0; // this counts the number of Topologyparts in the TDS
							// defining topologypart
							for (int j = 0; j < Blocks[i].total_arcs; j++) {

								bool TDSPartCreated = false; // This variable turns to 1 only if a an Arc of the block section falls in the TDS section considered,
								// i.e. only i a TDSPart is created for the TDS to be part of a Topology Sequence

								if (((Blocks[i].arcs_in_signalling_block_section[j].startNode.X >= TDS_StartNodeX) && (Blocks[i].arcs_in_signalling_block_section[j].startNode.X < TDS_EndNodeX)) && ((Blocks[i].arcs_in_signalling_block_section[j].endNode.X > TDS_StartNodeX) && (Blocks[i].arcs_in_signalling_block_section[j].endNode.X <= TDS_EndNodeX))) {
									// When both the start and end Node of the arcs are within the TDS boundaries
									// we should take the whole Arc

									counter++;

									TopologyPart TDSPart;

									TDSPartCreated = true;
									TDSPart.ID = (*td)->ID + "_a_" + to_string(counter);
									TDSPart.TDS_arc = &Blocks[i].arcs_in_signalling_block_section[j];

									// specifying the X and Y coordinates of the TDSPart
									TDSPart.trackLineId = it->TracklineID;
									TDSPart.StartX = TDSPart.TDS_arc->startNode.X;
									TDSPart.StartY = TDSPart.trackLineId;
									TDSPart.EndX = TDSPart.TDS_arc->endNode.X;
									TDSPart.EndY = TDSPart.trackLineId;

									TS.TopologyPart_List.push_back(TDSPart);

								}

								// if the end start Node of the Arc is lower than the start of the TDS then take only the part after the start Node of the TDS
								else if ((Blocks[i].arcs_in_signalling_block_section[j].startNode.X < (*td)->start_node.X) && ((Blocks[i].arcs_in_signalling_block_section[j].endNode.X <= (*td)->end_node.X) && (Blocks[i].arcs_in_signalling_block_section[j].endNode.X > (*td)->start_node.X))) {

									counter++;
									TopologyPart TDSPart;

									TDSPartCreated = true;
									TDSPart.ID = (*td)->ID + "_a_" + to_string(counter);

									// create a copy of the Arc
									Arc ArcCopy = Blocks[i].arcs_in_signalling_block_section[j];

									Additional_Arcs_To_Create_TDS.push_back(ArcCopy);

									list<Arc>::iterator EndElement = Additional_Arcs_To_Create_TDS.end();
									EndElement--;

									if (TDSOnFirstTrackLine == 1) {
										EndElement->startNode = (*td)->start_node;
									}

									else {
										EndElement->startNode = (*td)->node_on_switch;
									}

									// the length should be changed accordingly as the Arc is cut at the beginning
									EndElement->length = EndElement->endNode.X - EndElement->startNode.X;
									TDSPart.TDS_arc = &(*EndElement);

									// specifying the X and Y coordinates of the TDSPart
									TDSPart.trackLineId = it->TracklineID;
									TDSPart.StartX = TDSPart.TDS_arc->startNode.X;
									TDSPart.StartY = TDSPart.trackLineId;
									TDSPart.EndX = TDSPart.TDS_arc->endNode.X;
									TDSPart.EndY = TDSPart.trackLineId;

									TS.TopologyPart_List.push_back(TDSPart);

								}

								else if (((Blocks[i].arcs_in_signalling_block_section[j].startNode.X >= (*td)->start_node.X) && (Blocks[i].arcs_in_signalling_block_section[j].startNode.X < (*td)->end_node.X)) && (Blocks[i].arcs_in_signalling_block_section[j].endNode.X > (*td)->end_node.X)) {

									counter++;
									TopologyPart TDSPart;

									TDSPartCreated = true;
									TDSPart.ID = (*td)->ID + "_a_" + to_string(counter);

									// create a copy of the Arc
									Arc ArcCopy = Blocks[i].arcs_in_signalling_block_section[j];

									Additional_Arcs_To_Create_TDS.push_back(ArcCopy);

									list<Arc>::iterator EndElement = Additional_Arcs_To_Create_TDS.end();
									EndElement--;

									if (TDSOnFirstTrackLine == 1) {
										EndElement->endNode = (*td)->node_on_switch;
									} else {

										EndElement->endNode = (*td)->end_node;
									}

									// the length should be changed accordingly as the Arc is cut at the end
									EndElement->length = EndElement->endNode.X - EndElement->startNode.X;
									TDSPart.TDS_arc = &(*EndElement);

									// specifying the X and Y coordinates of the TDSPart
									TDSPart.trackLineId = it->TracklineID;
									TDSPart.StartX = TDSPart.TDS_arc->startNode.X;
									TDSPart.StartY = TDSPart.trackLineId;
									TDSPart.EndX = TDSPart.TDS_arc->endNode.X;
									TDSPart.EndY = TDSPart.trackLineId;

									TS.TopologyPart_List.push_back(TDSPart);
								}

								else if ((Blocks[i].arcs_in_signalling_block_section[j].startNode.X < (*td)->start_node.X) && (Blocks[i].arcs_in_signalling_block_section[j].endNode.X > (*td)->end_node.X)) {
									// if instead both the start and end edges of the Arc are outside the start and end of the TDS ( i.e. the TDS is shorter than the Arc)
									counter++;
									TopologyPart TDSPart;

									TDSPartCreated = true;
									TDSPart.ID = (*td)->ID + "_a_" + to_string(counter);

									// create a copy of the Arc
									Arc ArcCopy = Blocks[i].arcs_in_signalling_block_section[j];

									Additional_Arcs_To_Create_TDS.push_back(ArcCopy);

									list<Arc>::iterator EndElement = Additional_Arcs_To_Create_TDS.end();
									EndElement--;

									if (TDSOnFirstTrackLine == 1) {
										EndElement->startNode = (*td)->start_node;
										EndElement->endNode = (*td)->node_on_switch;
									} else {
										EndElement->startNode = (*td)->node_on_switch;
										EndElement->endNode = (*td)->end_node;
									}

									// the length should be changed accordingly as the Arc is cut at the end
									EndElement->length = EndElement->endNode.X - EndElement->startNode.X;
									TDSPart.TDS_arc = &(*EndElement);

									// specifying the X and Y coordinates of the TDSPart
									TDSPart.trackLineId = it->TracklineID;
									TDSPart.StartX = TDSPart.TDS_arc->startNode.X;
									TDSPart.StartY = TDSPart.trackLineId;
									TDSPart.EndX = TDSPart.TDS_arc->endNode.X;
									TDSPart.EndY = TDSPart.trackLineId;

									TS.TopologyPart_List.push_back(TDSPart);
								}

								// If a TDS Part has been created for the current TDS then the following condition should be checked.
								if (TDSPartCreated == 1) {
									// if the beginning Node of the TDSPart is equal to the beginning of the switch and the trackline ID of the TDS is the FirstConnected TracklineID
									// then rename the TDSPart
									list<TopologyPart>::iterator LastInsertedElement = TS.TopologyPart_List.end();
									LastInsertedElement--; // pointer to the last inserted element
									// if the beginning Node of the TDSPart is equal to the beginning of the switch and the trackline ID of the TDS is the FirstConnected TracklineID
									// then rename the TDSPart
									if (((*(*LastInsertedElement).TDS_arc).startNode.X == Blocks[i].XStartSwitch) && (it->TracklineID == Blocks[i].FirstConnectedTrackLineID)) {
										LastInsertedElement->ID = (*td)->ID + "_a_div_switch";

										// Adjusting the y coordinate of the Arc edges if the Arc is one the diverging switch in the block section
										LastInsertedElement->StartY = Blocks[i].FirstConnectedTrackLineID;
										LastInsertedElement->EndY = abs((Blocks[i].SecondConnectedTrackLineID - Blocks[i].FirstConnectedTrackLineID)) / 2;

									} else if (((*(*LastInsertedElement).TDS_arc).endNode.X == Blocks[i].XEndSwitch) && (it->TracklineID == Blocks[i].SecondConnectedTrackLineID)) {
										LastInsertedElement->ID = (*td)->ID + "_a_conv_switch";

										// Adjusting the y coordinate of the Arc edges if the Arc is one the diverging switch in the block section
										LastInsertedElement->StartY = abs((Blocks[i].SecondConnectedTrackLineID - Blocks[i].FirstConnectedTrackLineID)) / 2;
										LastInsertedElement->EndY = Blocks[i].SecondConnectedTrackLineID;
									}
								}
							}

							AllTopoSequences.push_back(TS);
						}
					}
				}
			}
		}
	}
	// After that all topology sequences have been set up for all the TDS then fill in the list of Unused_TopologyParts
	// for all the TDS containing switches as they have more than 1 Topology Sequence depending on whether the switch is set in the diverging or straight direction

	fillinUnusedTopoPartsOfTopoSequencesForTdsOnSwitches(AllTopoSequences);
}

// Function to print all infrastructure elements for RECIFE conflict detection and resolution algorithm
void printInfrastructureFileForRecife(Section* Blocks, int N_Blocks, list<TopologySequence>& AllTopoSequences, string OutputFolder) {
	ofstream RecifeInfraStream;
	string RECIFEInfraOutputFile;
	RECIFEInfraOutputFile = RECIFEInfraOutputFile + OutputFolder + "/Source_Infrastructure_RECIFE.txt";
	RecifeInfraStream.open((char*)RECIFEInfraOutputFile.c_str(), ios::binary);
	RecifeInfraStream << "Trackline \t BlockID  \t EntrySignal \t ExitSignal \t BlockStartX[Km] \t BlockEndX[Km] \t TopologySequences \t TDS_Id \t topologypartID \t topologyStartY \t topologyEndY \t TopoPartStartX[Km] \tTopoPartEndX[Km] \t length \t gradient \t curve \t speedlimit\n";

	for (int i = 0; i < N_Blocks; i++) {
		string blockinfoline;

		blockinfoline = blockinfoline + tostring(Blocks[i].trackLineId) + " \t " + Blocks[i].ID + "\t sign_" + Blocks[i].start_node.tdsbId + "\t sign_" + Blocks[i].end_node.tdsbId + "\t " + tostring(Blocks[i].start_node.X) + "\t " + tostring(Blocks[i].end_node.X) + "\t ";

		for (auto td = Blocks[i].TDS_in_block.begin(); td != Blocks[i].TDS_in_block.end(); td++) {

			list<string> InfoTDSPartList;		   // This is the list of information regarding each Topology Part in the TDS (including both those used and unused in the block section)
			string TDSTopoSequences;			   // This is ithe string collecting all of the Topology Sequences belonging to the TDS
			int TopoSequenceCounter = 0;		   // This is a counter on the identified Topology Sequence
			bool TopoPartInfoAlreadyTaken = false; // boolean which turn to true if all the arcs (TopologyParts) of the TDS have been already collected in text

			list<TopologySequence>::iterator LastTopoSequenceElement = AllTopoSequences.end(); // This is the last element of the Topology Sequence list
			LastTopoSequenceElement--;														   // this is the last Topology Sequence of the list

			for (auto s = AllTopoSequences.begin(); s != AllTopoSequences.end(); s++) {

				if (s->TDS_ID == (*td)->ID) {
					int TDPartCounter = 0;
					TopoSequenceCounter++; // increase teh counter everytime there is a Topology Sequence found for the TDS
					if (TopoSequenceCounter == 1) {
						TDSTopoSequences = TDSTopoSequences + "[";

					} else { // if there is more than a topology sequence then they need to be split by a comma
						TDSTopoSequences = TDSTopoSequences + ",";
					}

					TDSTopoSequences = TDSTopoSequences + "['" + s->ID + "',[";

					for (auto p = s->TopologyPart_List.begin(); p != s->TopologyPart_List.end(); p++) {
						TDSTopoSequences = TDSTopoSequences + "[" + tostring(TDPartCounter) + ",'" + p->ID + "']";
						TDPartCounter++;
						// if the TopoPart of the TDS is not the last of the TDS
						if (TDPartCounter < (s->TopologyPart_List.size())) {

							TDSTopoSequences = TDSTopoSequences + ",";
							// if the Info of the Topology Parts of the TDS have not yet been taken
							if (TopoPartInfoAlreadyTaken == 0) {
								// then fill in the list InfoTDSPart
								string ArcInfo;
								ArcInfo = p->ID + "\t" + tostring(p->StartY) + "\t" + tostring(p->EndY) + "\t" + tostring(p->StartX) + "\t" + tostring(p->EndX) + "\t" + tostring((*p).TDS_arc->length) + "\t" + tostring((*p).TDS_arc->gradient) + "\t" + tostring((*p).TDS_arc->curvature) + "\t" + tostring((*p).TDS_arc->speedLimit);
								InfoTDSPartList.push_back(ArcInfo);
							}
						} else {
							TDSTopoSequences = TDSTopoSequences + "]]";

							// if the Info of the Topology Parts of the TDS have not yet been taken
							if (TopoPartInfoAlreadyTaken == 0) {
								// then fill in the list InfoTDSPart
								string ArcInfo;
								ArcInfo = p->ID + "\t" + tostring(p->StartY) + "\t" + tostring(p->EndY) + "\t" + tostring(p->StartX) + "\t" + tostring(p->EndX) + "\t" + tostring((*p).TDS_arc->length) + "\t" + tostring((*p).TDS_arc->gradient) + "\t" + tostring((*p).TDS_arc->curvature) + "\t" + tostring((*p).TDS_arc->speedLimit);
								InfoTDSPartList.push_back(ArcInfo);
								// Also add the Arc (i.e. the Topology Parts of the TDS which are not used in the block section) of the TDS
								for (auto u = s->Unused_TopoParts_In_Sequence.begin(); u != s->Unused_TopoParts_In_Sequence.end(); u++) {
									string UnusedArcInfo;
									UnusedArcInfo = u->ID + "\t" + tostring(u->StartY) + "\t" + tostring(u->EndY) + "\t" + tostring(u->StartX) + "\t" + tostring(u->EndX) + "\t" + tostring((*u).TDS_arc->length) + "\t" + tostring((*u).TDS_arc->gradient) + "\t" + tostring((*u).TDS_arc->curvature) + "\t" + tostring((*u).TDS_arc->speedLimit);
									InfoTDSPartList.push_back(UnusedArcInfo);
								}
								TopoPartInfoAlreadyTaken = true; // set that the information of all the arcs (Topology Parts) of the TDS have now been taken in the form of a text string
							}
						}
					}
				}
				if (s == LastTopoSequenceElement) {
					TDSTopoSequences = TDSTopoSequences + "]\t" + (*td)->ID + "\t";
				}
			}
			// Write in the text file the information about all of the arcs ( Topology Parts) for the TDS in the block section
			for (auto v = InfoTDSPartList.begin(); v != InfoTDSPartList.end(); v++) {
				RecifeInfraStream << blockinfoline << TDSTopoSequences << *v << "\n";
			}
		}
	}
	RecifeInfraStream.close();
	// After that the RECIFE infrastructure file has been printed, delete all the elements in the Topology Sequences an in the additional arcs to create TDS to free memory
	AllTopoSequences.clear();
	Additional_Arcs_To_Create_TDS.clear();
}
