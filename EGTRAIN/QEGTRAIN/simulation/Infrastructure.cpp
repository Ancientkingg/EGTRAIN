#include "simulation/Infrastructure.h"
#include <cstdio>
#include <cfloat>

extern Logger owl;
double timestep = 1;
// double times = 0;
double signalCode1 = 11.111, signalCode2 = 0, signalCode3 = 0; // Signalling System Speed Codes (for Track Circuit Sisgnalling System: blockSets.A.connections.connections.)
int numTrackLines = 0;				   // This is the total number of TrackLines

list<StationPlatform> AllStationPlatforms; // This is a global list containing all Station platforms existing in the modelled network
int numAllStationPlatforms = 0;			   // This variable provides the total number of station platforms in the modelled network (it is the size of the list AllStationPlatforms)

InitialParameters initial_variables(0);
string InputMainFolder = initial_variables.InputMainFolder; // "Input_EGTRAIN";       //This is the Folder of all the input of EGTRAIN
// string OutputMainFolder = initial_variables.OutputMainFolder;// "Output_EGTRAIN";       //This is the Folder of all the input of EGTRAIN

// extern InitialParameters initial_variables;


// Class constructor function
InfraElement::InfraElement() {
	ID = SectionID = SwitchName = stationName = ConnectedPoint = "None";
	isStation = IsSwitch = IsSignal = IsTrackDetSecBorder = withSwitchDiv = false;
	XCoordinate = YCoordinate = GeoXCoord = GeoYCoord = -1;
	isEndOfDivSwitchStartOfADivSwitch = false;
	XConnectedPoint = -1;
}


// Node Default Values
Node::Node() {
	sceneNodeId.clear();
	X = Y = dwellTime = 0;
	ID = 0;
	isSignalled = virtualCouplingNode = false;
	station = respectOrder = false;
	StopTime = 0;
	StepStopped = 0;
	numConnections = 0;
	indexOrderList = -1;
	tdsbGeoCoordX = tdsbGeoCoordY = -1;
	arcSpeedLimit = 0;
	latitude = longitude = graphX = graphY = 0;
	virtualSignal = false;
	stationPlatformId = "None";
	for (int i = 0; i < 6; i++) {
		connectIdBlockSet[i] = 0;
		connectXNode[i] = 0;
	}
}

void Node::initialiseIdConnectedBlocks(string* IDConnectedBSofBlock, int N_IDConnectedBSofBlock) {
	if (this->numConnections > 0) {
		for (int i = 0; i < this->numConnections; i++) {
			string NameToSearch;
			char blockSetId[100], XNode[100];
			snprintf(blockSetId, sizeof(blockSetId), "%d", this->connectIdBlockSet[i]);
			snprintf(XNode, sizeof(XNode), "%f", this->connectXNode[i]);

			NameToSearch = NameToSearch + "-B" + blockSetId + "@-" + XNode;
			if (N_IDConnectedBSofBlock > 0) {
				for (int h = 0; h < N_IDConnectedBSofBlock; h++) {
					string Prova = IDConnectedBSofBlock[h];
					IDConnectedBSofBlock[h].find(NameToSearch, 0);
					if (IDConnectedBSofBlock[h].find(NameToSearch, 0) != -1) {
						this->IDConnectedBlocks.push_back(IDConnectedBSofBlock[h]);
						break; // break the for loop on the strings of the connected blocks of the block section
					}
				}
			}
		}
	}
}

// Arc Default Values
Arc::Arc() {
	length = curvature = gradient = speedLimit = 0;
	ID = 0;
	signalSpeedLimit = 999;
	speedInBraking = 0;
	fs = 0;
	brakingDistance = 0;
}

void Arc::arcLength() {
	length = sqrt(pow((endNode.X - startNode.X), 2) + pow((endNode.Y - startNode.Y), 2)) * 1000;
}

BlockSet::BlockSet() {
	ID = len = arcs = numNodes = 0;
	sceneTrackId.clear();
	region = 0;
	graphID = -1;
	hasGraphLayout = false;
	firstSwitchX = -1;
	lastSwitchX = DBL_MAX;
}
BlockSet blockSets[268];

// This class define all connections amongst TrackLines, therefore represents switches, joints and all the other track connections
int numConnections = 0;

Connections::Connections() {
	sceneFirstNodeId.clear();
	sceneSecondNodeId.clear();
	idFirstTrackLine = idSecondTrackLine = 0;
	xFirstNode = xSecondNode = 0;
	speedlimit = 16.667; // by default the speedlimit on a switch is set to 16.667 m /s; i.e. 60 km/h
	graphXFirstNode = graphYFirstNode = graphXSecondNode = graphYSecondNode = 0;
}

Connections connections[708];

// Function to order pairs of elements
bool orderPassengerListOnPlatform(pair<string, double>& A, pair<string, double>& blockSets) {
	if (A.second < blockSets.second)
		return true;
	else
		return false;
}


Stations::Stations() {
	Av_Arrival_Delay = Std_Arrival_Delay = Tot_Consec_Delay = Perc_Delayed_T_5min = Perc_Delayed_T_3min = Perc_Delayed_T = Max_TotalDelay = Max_Cons_Delay = -1;
	totalArrivalDelay = 0;
	N_Stopped_Trains = N_Delayed_Arr = N_Delayed_Arr_3min = N_Delayed_Arr_5min = -1;
	latitude = longitude = graphX = graphY = 0;
	N_StationPlatforms = 0;
}

Stations StationArray[95]; // Array of all the station in the network
Stations TotalInputDelays; // Element which collects the amount of entrance delays and disturbances set as input
Stations EntranceInputDelays;
Stations DisturbanceInput;
Stations Final_Station; // Fittitious Station to measure train delays at their own Final Station
int numStations = 0;

void Print_Station_Delay_Stats(string Name_StationDelay, string kindofdelay) {
	ofstream FileOutput;
	string FileOutName;
	if (kindofdelay == "pos")
		FileOutName = FileOutName + Name_StationDelay + "/Stats_Stations.txt";
	else if (kindofdelay == "pos&neg")
		FileOutName = FileOutName + Name_StationDelay + "/Pos&Neg_Stats_Stations.txt";
	FileOutput.open((char*)FileOutName.c_str());
	FileOutput << "StName" << " " << "Av_Delay" << " " << "Std_Delay" << " " << "Total_Delay" << " " << "Max_Tot_Delay" << " " << "Cum_Cons_Delay" << " " << "Max_Cons_Delay" << " " << "Perc_Delayed" << " " << "Perc_Delayed_3min" << " " << "Perc_Delayed_5min" << " " << "N_StopTrains" << " " << "N_DelTrains" << "\n";
	// Print first the amount of Entrance delay and disturbances to dwell times in input
	FileOutput << "Ent_Del+DwT_Dist" << " " << TotalInputDelays.Av_Arrival_Delay << " " << TotalInputDelays.Std_Arrival_Delay << " " << TotalInputDelays.totalArrivalDelay << " " << TotalInputDelays.Max_TotalDelay << " " << "N/D" << " " << "N/D" << " " << TotalInputDelays.Perc_Delayed_T << " " << TotalInputDelays.Perc_Delayed_T_3min << " " << TotalInputDelays.Perc_Delayed_T_5min << " " << TotalInputDelays.N_Stopped_Trains << " " << TotalInputDelays.N_Delayed_Arr << "\n";
	FileOutput << "Ent_Delays" << " " << EntranceInputDelays.Av_Arrival_Delay << " " << EntranceInputDelays.Std_Arrival_Delay << " " << EntranceInputDelays.totalArrivalDelay << " " << EntranceInputDelays.Max_TotalDelay << " " << "N/D" << " " << "N/D" << " " << EntranceInputDelays.Perc_Delayed_T << " " << EntranceInputDelays.Perc_Delayed_T_3min << " " << EntranceInputDelays.Perc_Delayed_T_5min << " " << EntranceInputDelays.N_Stopped_Trains << " " << EntranceInputDelays.N_Delayed_Arr << "\n";
	FileOutput << "DwT_Dist" << " " << DisturbanceInput.Av_Arrival_Delay << " " << DisturbanceInput.Std_Arrival_Delay << " " << DisturbanceInput.totalArrivalDelay << " " << DisturbanceInput.Max_TotalDelay << " " << "N/D" << " " << "N/D" << " " << DisturbanceInput.Perc_Delayed_T << " " << DisturbanceInput.Perc_Delayed_T_3min << " " << DisturbanceInput.Perc_Delayed_T_5min << " " << DisturbanceInput.N_Stopped_Trains << " " << DisturbanceInput.N_Delayed_Arr << "\n";

	// Print the calculated delays for all the stations
	for (int s = 1; s < numStations; s++) { // Print it for every station but the first, if you want it also for the first just start the for loop with s=0
		FileOutput << StationArray[s].stationName << " " << StationArray[s].Av_Arrival_Delay << " " << StationArray[s].Std_Arrival_Delay << " " << StationArray[s].totalArrivalDelay << " " << StationArray[s].Max_TotalDelay << " " << StationArray[s].Tot_Consec_Delay << " " << StationArray[s].Max_Cons_Delay << " " << StationArray[s].Perc_Delayed_T << " " << StationArray[s].Perc_Delayed_T_3min << " " << StationArray[s].Perc_Delayed_T_5min << " " << StationArray[s].N_Stopped_Trains << " " << StationArray[s].N_Delayed_Arr << "\n";
	}

	// Computing Totals over the stations
	double TotalAV = 0, TotalStd = 0, TOTDelay = 0, MAX_TOTDelay = 0, TOTCONSDelay = 0, MAX_CONSDelay = 0, AV_Punct = 0, AV_Punct_3min = 0, AV_Punct_5min = 0;
	int stationsWithSamples = 0;
	for (int s = 1; s < numStations; s++) {								// Calculate it for every station but the first, if you want it also for the first station just start the loop with s=0
		if (StationArray[s].N_Stopped_Trains <= 0)
			continue;
		++stationsWithSamples;
		TotalAV = TotalAV + StationArray[s].Av_Arrival_Delay;			// Calculating the Average Delay over the stations
		TotalStd = TotalStd + StationArray[s].Std_Arrival_Delay;		// Calculating the Average Std over the stations
		TOTDelay = TOTDelay + StationArray[s].totalArrivalDelay;		// Calculating the Total Delay over all stations
		TOTCONSDelay = TOTCONSDelay + StationArray[s].Tot_Consec_Delay; // Calculating the Total consecutive delay over all stations
		if (stationsWithSamples == 1 || StationArray[s].Max_TotalDelay > MAX_TOTDelay)
			MAX_TOTDelay = StationArray[s].Max_TotalDelay; // Calculating the MAX TOTAL DELAY
		if (stationsWithSamples == 1 || StationArray[s].Max_Cons_Delay > MAX_CONSDelay)
			MAX_CONSDelay = StationArray[s].Max_Cons_Delay;					 // Calculating the MAX CONSECUTIVE DELAY
		AV_Punct = AV_Punct + StationArray[s].Perc_Delayed_T;				 // Calculating the  Avergae Punctuality at stations
		AV_Punct_3min = AV_Punct_3min + StationArray[s].Perc_Delayed_T_3min; // Calculating the  Avergae Punctuality at 3 min at stations
		AV_Punct_5min = AV_Punct_5min + StationArray[s].Perc_Delayed_T_5min; // Calculating the  Avergae Punctuality at 5 min at stations
	}

	/*//Calculating the averages over all stations
	TotalAV=TotalAV/numStations;
	TotalStd=TotalStd/numStations;
	AV_Punct=AV_Punct/numStations;                        //Calculating the  Average Punctuality at stations
	AV_Punct_3min=AV_Punct_3min/numStations;          //Calculating the  Average Punctuality at 3 min at stations
	AV_Punct_5min=AV_Punct_5min/numStations;          //Calculating the  Average Punctuality at 5 min at stations*/

	if (stationsWithSamples > 0) {
		TotalAV /= stationsWithSamples;
		TotalStd /= stationsWithSamples;
		AV_Punct /= stationsWithSamples;
		AV_Punct_3min /= stationsWithSamples;
		AV_Punct_5min /= stationsWithSamples;
	} else {
		TotalAV = TotalStd = MAX_TOTDelay = MAX_CONSDelay = -1;
		AV_Punct = AV_Punct_3min = AV_Punct_5min = -1;
		TOTCONSDelay = -1;
	}

	FileOutput << "TOTALS" << " " << TotalAV << " " << TotalStd << " " << TOTDelay << " " << MAX_TOTDelay << " " << TOTCONSDelay << " " << MAX_CONSDelay << " " << AV_Punct << " " << AV_Punct_3min << " " << AV_Punct_5min << "\n";
	// Printing the Final_Delay fittitious station
	FileOutput << Final_Station.stationName << " " << Final_Station.Av_Arrival_Delay << " " << Final_Station.Std_Arrival_Delay << " " << Final_Station.totalArrivalDelay << " " << Final_Station.Max_TotalDelay << " " << Final_Station.Tot_Consec_Delay << " " << Final_Station.Max_Cons_Delay << " " << Final_Station.Perc_Delayed_T << " " << Final_Station.Perc_Delayed_T_3min << " " << Final_Station.Perc_Delayed_T_5min << " " << Final_Station.N_Stopped_Trains << " " << Final_Station.N_Delayed_Arr << "\n";

	FileOutput.close();
}
