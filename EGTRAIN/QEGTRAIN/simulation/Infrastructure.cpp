#include "simulation/Infrastructure.h"
#include "simulation/SimulationRun.h"
#include <cstdio>
#include <cfloat>

using namespace std;

InitialParameters initial_variables(0);


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
	latitude = longitude = 0;
	virtualSignal = false;
	stationPlatformId = "None";
	for (std::size_t i = 0; i < RuntimeLimits::kNativeMaxNodeConnections; i++) {
		connectIdBlockSet[i] = 0;
		connectXNode[i] = 0;
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

// This class define all connections amongst TrackLines, therefore represents switches, joints and all the other track connections
Connections::Connections() {
	sceneFirstNodeId.clear();
	sceneSecondNodeId.clear();
	idFirstTrackLine = idSecondTrackLine = 0;
	xFirstNode = xSecondNode = 0;
	speedlimit = 16.667; // by default the speedlimit on a switch is set to 16.667 m /s; i.e. 60 km/h
}

// Function to order pairs of elements
bool orderPassengerListOnPlatform(pair<string, double>& A, pair<string, double>& blockSets) {
	if (A.second < blockSets.second)
		return true;
	else
		return false;
}


void Print_Station_Delay_Stats(const SimulationRun& run, string Name_StationDelay, string kindofdelay) {
	ofstream FileOutput;
	string FileOutName;
	if (kindofdelay == "pos")
		FileOutName = FileOutName + Name_StationDelay + "/Stats_Stations.txt";
	else if (kindofdelay == "pos&neg")
		FileOutName = FileOutName + Name_StationDelay + "/Pos&Neg_Stats_Stations.txt";
	FileOutput.open((char*)FileOutName.c_str());
	FileOutput << "StName" << " " << "Av_Delay" << " " << "Std_Delay" << " " << "Total_Delay" << " " << "Max_Tot_Delay" << " " << "Cum_Cons_Delay" << " " << "Max_Cons_Delay" << " " << "Perc_Delayed" << " " << "Perc_Delayed_3min" << " " << "Perc_Delayed_5min" << " " << "N_StopTrains" << " " << "N_DelTrains" << "\n";
	// Print first the amount of Entrance delay and disturbances to dwell times in input
	FileOutput << "Ent_Del+DwT_Dist" << " " << run.totalInputDelays.Av_Arrival_Delay << " " << run.totalInputDelays.Std_Arrival_Delay << " " << run.totalInputDelays.totalArrivalDelay << " " << run.totalInputDelays.Max_TotalDelay << " " << "N/D" << " " << "N/D" << " " << run.totalInputDelays.Perc_Delayed_T << " " << run.totalInputDelays.Perc_Delayed_T_3min << " " << run.totalInputDelays.Perc_Delayed_T_5min << " " << run.totalInputDelays.N_Stopped_Trains << " " << run.totalInputDelays.N_Delayed_Arr << "\n";
	FileOutput << "Ent_Delays" << " " << run.entranceInputDelays.Av_Arrival_Delay << " " << run.entranceInputDelays.Std_Arrival_Delay << " " << run.entranceInputDelays.totalArrivalDelay << " " << run.entranceInputDelays.Max_TotalDelay << " " << "N/D" << " " << "N/D" << " " << run.entranceInputDelays.Perc_Delayed_T << " " << run.entranceInputDelays.Perc_Delayed_T_3min << " " << run.entranceInputDelays.Perc_Delayed_T_5min << " " << run.entranceInputDelays.N_Stopped_Trains << " " << run.entranceInputDelays.N_Delayed_Arr << "\n";
	FileOutput << "DwT_Dist" << " " << run.disturbanceInput.Av_Arrival_Delay << " " << run.disturbanceInput.Std_Arrival_Delay << " " << run.disturbanceInput.totalArrivalDelay << " " << run.disturbanceInput.Max_TotalDelay << " " << "N/D" << " " << "N/D" << " " << run.disturbanceInput.Perc_Delayed_T << " " << run.disturbanceInput.Perc_Delayed_T_3min << " " << run.disturbanceInput.Perc_Delayed_T_5min << " " << run.disturbanceInput.N_Stopped_Trains << " " << run.disturbanceInput.N_Delayed_Arr << "\n";

	// Print the calculated delays for all the stations
	for (int s = 1; s < run.infra.numStations; s++) { // Print it for every station but the first, if you want it also for the first just start the for loop with s=0
		FileOutput << run.infra.stations[s].stationName << " " << run.infra.stations[s].Av_Arrival_Delay << " " << run.infra.stations[s].Std_Arrival_Delay << " " << run.infra.stations[s].totalArrivalDelay << " " << run.infra.stations[s].Max_TotalDelay << " " << run.infra.stations[s].Tot_Consec_Delay << " " << run.infra.stations[s].Max_Cons_Delay << " " << run.infra.stations[s].Perc_Delayed_T << " " << run.infra.stations[s].Perc_Delayed_T_3min << " " << run.infra.stations[s].Perc_Delayed_T_5min << " " << run.infra.stations[s].N_Stopped_Trains << " " << run.infra.stations[s].N_Delayed_Arr << "\n";
	}

	// Computing Totals over the stations
	double TotalAV = 0, TotalStd = 0, TOTDelay = 0, MAX_TOTDelay = 0, TOTCONSDelay = 0, MAX_CONSDelay = 0, AV_Punct = 0, AV_Punct_3min = 0, AV_Punct_5min = 0;
	int stationsWithSamples = 0;
	for (int s = 1; s < run.infra.numStations; s++) { // Calculate it for every station but the first, if you want it also for the first station just start the loop with s=0
		if (run.infra.stations[s].N_Stopped_Trains <= 0)
			continue;
		++stationsWithSamples;
		TotalAV = TotalAV + run.infra.stations[s].Av_Arrival_Delay;			  // Calculating the Average Delay over the stations
		TotalStd = TotalStd + run.infra.stations[s].Std_Arrival_Delay;		  // Calculating the Average Std over the stations
		TOTDelay = TOTDelay + run.infra.stations[s].totalArrivalDelay;		  // Calculating the Total Delay over all stations
		TOTCONSDelay = TOTCONSDelay + run.infra.stations[s].Tot_Consec_Delay; // Calculating the Total consecutive delay over all stations
		if (stationsWithSamples == 1 || run.infra.stations[s].Max_TotalDelay > MAX_TOTDelay)
			MAX_TOTDelay = run.infra.stations[s].Max_TotalDelay; // Calculating the MAX TOTAL DELAY
		if (stationsWithSamples == 1 || run.infra.stations[s].Max_Cons_Delay > MAX_CONSDelay)
			MAX_CONSDelay = run.infra.stations[s].Max_Cons_Delay;				   // Calculating the MAX CONSECUTIVE DELAY
		AV_Punct = AV_Punct + run.infra.stations[s].Perc_Delayed_T;				   // Calculating the  Avergae Punctuality at stations
		AV_Punct_3min = AV_Punct_3min + run.infra.stations[s].Perc_Delayed_T_3min; // Calculating the  Avergae Punctuality at 3 min at stations
		AV_Punct_5min = AV_Punct_5min + run.infra.stations[s].Perc_Delayed_T_5min; // Calculating the  Avergae Punctuality at 5 min at stations
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
	FileOutput << kFinalStationName << " " << run.finalStation.Av_Arrival_Delay << " " << run.finalStation.Std_Arrival_Delay << " " << run.finalStation.totalArrivalDelay << " " << run.finalStation.Max_TotalDelay << " " << run.finalStation.Tot_Consec_Delay << " " << run.finalStation.Max_Cons_Delay << " " << run.finalStation.Perc_Delayed_T << " " << run.finalStation.Perc_Delayed_T_3min << " " << run.finalStation.Perc_Delayed_T_5min << " " << run.finalStation.N_Stopped_Trains << " " << run.finalStation.N_Delayed_Arr << "\n";

	FileOutput.close();
}
