#ifndef Rescheduling_H
#define Rescheduling_H

#include <list>
#include <string>

// EGTRAIN files
#include "simulation/Infrastructure.h"
#include "simulation/Signalling.h"

/*************************************************************************************************************************************************/

// Definition of Objects to convert EGTRAIN infrastructure, timetable and rolling stock to real-time traffic rescheduling algorithms

/*************************************************************************************************************************************************/

// Definition of objects for converting EGTRAIN inputs into real-time traffic rescheduling tool RECIFE
// Definition of infrastructure objects TopologyParts and TopologySequences

class TopologyPart { // Topology Parts are arcs of a Track Detection Section TDS with an associated string ID

public:
	string ID;
	Arc* TDS_arc;
	float StartX, StartY; // These are the spatial coordinate of the Beginning Node of the Arc
	float EndX, EndY;	  // These are the spatial coordinate of the End Node of the Arc
	int trackLineId;

	TopologyPart() {
		ID = "";
		TDS_arc = nullptr;
		StartX = StartY = EndX = EndY = -1;
		trackLineId = -1;
	}
};

// Definition of TopologySequence which identify all the sequences of how arcs in a TDS can be crossed depending on whether switches they might contain are set in a diverging or straight direction
class TopologySequence {
public:
	string ID;										 // ID of the Topology sequence
	string TDS_ID;									 // ID of the TDS the Topology sequence belongs to
	string blocksection_ID;							 // ID of the block section the Topology sequence belongs to
	list<TopologyPart> TopologyPart_List;			 // This is the list of TDS arcs which are used in the Topology Sequence for a given block section
	list<TopologyPart> Unused_TopoParts_In_Sequence; // This is the list of TDS arcs which belong to the TDS but are not used in the Topology Sequence for a given block section

	TopologySequence() {
		ID = TDS_ID = blocksection_ID = "";
	}
};

extern std::list<TopologySequence> All_Topology_Sequences;

extern std::list<Arc> Additional_Arcs_To_Create_TDS;

void fillinUnusedTopoPartsOfTopoSequencesForTdsOnSwitches(list<TopologySequence>& All_TopoSequences);

void initialiseTopologySequencesForRecife(Section* Blocks, int N_Blocks, list<TDS> All_TDS, list<TopologySequence>& AllTopoSequences);

// Function to print all infrastructure elements for RECIFE conflict detection and resolution algorithm
void printInfrastructureFileForRecife(Section* Blocks, int N_Blocks, list<TopologySequence>& AllTopoSequences, string OutputFolder);

#endif // Rescheduling_H
