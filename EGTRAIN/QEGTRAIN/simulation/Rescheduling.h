#ifndef Rescheduling_H
#define Rescheduling_H

#include <iostream>
#include <map>
#include <tuple>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>

// EGTRAIN files
#include "simulation/RollingStock.h"
#include "simulation/Infrastructure.h"
#include "simulation/Signalling.h"

#include "simulation/DispatchDecision.h"

class Rescheduling {
public:
	// class constructor
	Rescheduling();

	// loads the dictionary
	void loadDict();

	// print dictionary
	void printDict();

	// handle message sent from rescheduling tool
	void handleDispMessage(std::string message);

	// initialize train service path diagram (print header)
	void initalizeTrainServicePathDiagram(std::string FolderName);

	// initialize output file (erase)
	void initalizeOutfileFile(std::string FolderName);

	// read input file from dispatching tool
	void loadDispDecisions(std::string FolderName, int t);

	// create new train object
	void createTrainObject(int trainIdx);

	// print time update (append to file)
	void printTimeUpdateMsg(int t, std::string FolderName);

	// time interval update used to communicate time to disp tool
	int timeUpdateInterval;

	// create joint route from existing routes
	int createNewJointRoute(int shortRouteID, int longRouteID, std::string differentPart);

	// function that implements the dynamic platform allocation
	void checkArrivalPlatform(int trainIdx, int i);

	// change platform at destination
	void changeTrainRoute(int trainIdx, int i);

private:
	// dictionary EGTRAIN <-> rescheduling tool: keys=line,depStation,depPlatform,arrivalPlatform; values=stationArr,route,timetableFile
	std::map<std::tuple<int, std::string, int, int>, std::tuple<std::string, int, std::string>> reschedulingDict;

	// used to define when each train enters the simulation (avoiding two or more entrances at same time and location)
	int entryTime;
};

extern Rescheduling* dispatchingTool;

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
