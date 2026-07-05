#ifndef Infrastructure_hpp
#define Infrastructure_hpp

#include <iostream>
#include <fstream>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <new>
#include <list>
#include <string>
#include <ctime>
#include <sstream>
#include <map>
#include <algorithm>
#include "simulation/NumberGenerator.h"
#include <omp.h>
#include <vector>
#include "util/portability.h"
#include "simulation/InitialParameters.h"
#include "util/Logger.hpp"

using namespace std;

extern int t[40000];
extern double timestep;
extern double signalCode1, signalCode2, signalCode3; // Signalling system speed codes (for Track Circuit B.A.C.C.)
extern int numTrackLines;
extern int Signalling_Level; // 0 = ETCS-Level 0 (Track Circuit), 1 = ETCS-Level 1, etc.
extern int Headway;
extern int HeadwayD; // Headway for the Odd Track (Headway is for Even Track). Based on block section length; train starts only when green light is on.

extern string InputMainFolder; // Root input folder for EGTRAIN

// --- InfraElement: point infrastructure element (signal, switch, station stop board, track detection section border) ---

class InfraElement {
public:
	string ID;
	string SectionID;          // Block section this element belongs to
	string stationName;        // Station name (if this is a station stopping board)
	string ConnectedPoint;     // Connected point on a diverging switch
	string SwitchName;         // Switch name on non-diverging block sections. For diverging switch @0.22-2-B0@/@0.23-2-B1@PointStart, the SwitchName is @0.22-2-B0@Point; for the End variant it is @0.23-2-B1@Point.
	bool isStation, IsSwitch, IsSignal, IsTrackDetSecBorder;
	bool withSwitchDiv;        // True if this is a diverging switch
	bool isEndOfDivSwitchStartOfADivSwitch; // True if the end of one diverging switch coincides with the start of another
	double XCoordinate;        // X coordinate along the route (route-relative)
	double YCoordinate;        // Y coordinate along the route (route-relative)
	double GeoXCoord, GeoYCoord; // Geographical coordinates (route-independent)
	double XConnectedPoint;    // Route position of the connected point (for diverging switches)

	InfraElement();

	InfraElement& operator=(const InfraElement& ob2) {
		this->ID = ob2.ID;
		this->SectionID = ob2.SectionID;
		this->IsSignal = ob2.IsSignal;
		this->isStation = ob2.isStation;
		this->IsSwitch = ob2.IsSwitch;
		this->IsTrackDetSecBorder = ob2.IsTrackDetSecBorder;
		this->stationName = ob2.stationName;
		this->SwitchName = ob2.SwitchName;
		this->withSwitchDiv = ob2.withSwitchDiv;
		this->XCoordinate = ob2.XCoordinate;
		this->YCoordinate = ob2.YCoordinate;
		this->GeoXCoord = ob2.GeoXCoord;
		this->GeoYCoord = ob2.GeoYCoord;
		this->ConnectedPoint = ob2.ConnectedPoint;
		this->isEndOfDivSwitchStartOfADivSwitch = ob2.isEndOfDivSwitchStartOfADivSwitch;
		this->XConnectedPoint = ob2.XConnectedPoint;
		return *this;
	}
};

// --- Node: graph vertex representing a point in the network ---

class Node {
public:
	double ID;
	double X, Y;
	bool isSignalled;
	bool station;
	bool respectOrder;
	double dwellTime;
	double StopTime;
	int StepStopped;           // Real stop time (stochastic) of a train at this station node
	bool virtualCouplingNode;  // Whether this node triggers a Virtual Coupling link between consecutive trains
	int indexOrderList;
	int connectIdBlockSet[6];
	double connectXNode[6];
	int numConnections;        // Number of BlockSets connected at this node
	list<string> IDConnectedBlocks; // IDs of block sections connected to this node
	double arcSpeedLimit;      // Speed limit of the arc whose this node is the end node
	string stationName, stationPlatformId;
	string tdsbId;             // Track Detection Section Border ID (empty if not a TDSB)
	double tdsbGeoCoordX;
	double tdsbGeoCoordY;      // Geographical coordinates of the TDSB in meters
	double latitude, longitude;
	double graphX, graphY;     // Graphical coordinates
	bool virtualSignal;        // Identifies mid-signal of double switches

	Node();

	Node& operator=(const Node& ob2) {
		ID = ob2.ID;
		X = ob2.X;
		Y = ob2.Y;
		isSignalled = ob2.isSignalled;
		virtualCouplingNode = ob2.virtualCouplingNode;
		station = ob2.station;
		arcSpeedLimit = ob2.arcSpeedLimit;
		indexOrderList = ob2.indexOrderList;
		respectOrder = ob2.respectOrder;
		stationName = ob2.stationName;
		stationPlatformId = ob2.stationPlatformId;
		dwellTime = ob2.dwellTime;
		numConnections = ob2.numConnections;
		tdsbId = ob2.tdsbId;
		tdsbGeoCoordX = ob2.tdsbGeoCoordX;
		tdsbGeoCoordY = ob2.tdsbGeoCoordY;
		StopTime = ob2.StopTime;
		StepStopped = ob2.StepStopped;
		latitude = ob2.latitude;
		longitude = ob2.longitude;
		graphX = ob2.graphX;
		graphY = ob2.graphY;
		for (int i = 0; i < 6; i++) {
			connectIdBlockSet[i] = ob2.connectIdBlockSet[i];
			connectXNode[i] = ob2.connectXNode[i];
		}
		if (this != &ob2) {
			this->IDConnectedBlocks.clear();
			if (ob2.IDConnectedBlocks.size() > 0) {
				for (list<string>::const_iterator it = ob2.IDConnectedBlocks.begin(); it != ob2.IDConnectedBlocks.end(); it++) {
					this->IDConnectedBlocks.push_back(*it);
				}
			}
		}
		return *this;
	}

	bool operator==(const Node& ob2) {
		if ((ID == ob2.ID) && (X == ob2.X) && (Y == ob2.Y) && (isSignalled == ob2.isSignalled) && (station == ob2.station) && (dwellTime == ob2.dwellTime))
			return true;
		else
			return false;
	}

	void initialiseIdConnectedBlocks(string* IDConnectedBSofBlock, int N_IDConnectedBSofBlock);
};

// --- Arc: directed edge between two nodes ---

class Arc {
public:
	double ID;
	Node startNode, endNode;
	double length, curvature, gradient, speedLimit;
	double fs, brakingDistance, speedInBraking;    // finalAbscissa, braking distance, mean speed in braking zone ((A[i-1].speedLimit + A[i].speedLimit) / 2)
	double signalSpeedLimit;                        // Speed limit imposed by signalling (track circuits)

	Arc();

	void arcLength();

	bool operator==(const Arc& ob2) {
		if ((ID == ob2.ID) && (length == ob2.length) && (startNode.ID == ob2.startNode.ID) && (endNode.ID == ob2.endNode.ID) && (speedLimit == ob2.speedLimit) && (gradient = ob2.gradient) && (curvature == ob2.curvature) && (fs == ob2.fs) && (brakingDistance == ob2.brakingDistance) && (speedInBraking == ob2.speedInBraking) && (signalSpeedLimit == ob2.signalSpeedLimit))
			return true;
		else
			return false;
	}
	Arc& operator=(const Arc& ob2) {
		ID = ob2.ID;
		startNode = ob2.startNode;
		endNode = ob2.endNode;
		curvature = ob2.curvature;
		gradient = ob2.gradient;
		speedLimit = ob2.speedLimit;
		length = ob2.length;
		fs = ob2.fs;
		brakingDistance = ob2.brakingDistance;
		speedInBraking = ob2.speedInBraking;
		signalSpeedLimit = ob2.signalSpeedLimit;
		return *this;
	}
};

void loadArc(Arc* A, int& arcs, Node* N, int numNodes, char* name2);

void loadNode(Node* N, int& numNodes, char* name1);

// --- BlockSet: a train path (track line) composed of arcs and nodes ---

#define maxsize 1500

class BlockSet {
public:
	int ID;
	int len;
	int arcs, numNodes;              // Number of arcs and nodes in this track line
	Arc A[1500];                     // All arcs belonging to this BlockSet (loaded from file)
	Node N[1500];                    // All nodes belonging to this BlockSet (loaded from file)
	Arc member[maxsize];             // Member arcs of the track line
	int graphID;                     // Graphical display level
	int region;                      // Geographical region
	double firstSwitchX, lastSwitchX; // X coordinates of first and last switch

	BlockSet();

	int findArc(Arc A);

	bool isMember(Arc A);

	void showSet();

	BlockSet operator+(Arc A) {
		BlockSet newset;
		if (len == maxsize) {
			cout << "There too many Arc in this Trackline";
			return *this;
		}
		newset = *this;
		if (findArc(A) == -1) {
			newset.member[newset.len] = A;
			newset.len++;
		}
		return newset;
	}

	// Compute final abscissa and braking distance for all track arcs
	void setFs();

	// Load nodes and arcs from the given folder
	void defineTrainPath(char* FolderName);
};

extern BlockSet blockSets[268];

void loadTrackLine(char* TrackLineFolder);

// --- Connections: links between track lines (switches, joints, etc.) ---

extern int numConnections;

class Connections {
public:
	string ID;
	int idFirstTrackLine, idSecondTrackLine;
	double xFirstNode, xSecondNode;
	double speedlimit; // Speed limit in m/s (default 16.67 m/s)
	double graphXFirstNode, graphYFirstNode, graphXSecondNode, graphYSecondNode;
	Connections();

	void setConnections(BlockSet* blockSets);
};

extern Connections connections[708];
extern int numConnections;

void loadConnectionsOldVersion(int& numConnections, string ConnectionFilePath);

void loadConnections(int& numConnections, string ConnectionFilePath);

// --- StationPlatform: platform details (length, capacity, waiting passengers) ---

class StationPlatform {
public:
	string ID, StationID, BlockSectionID;
	double X, Y, length, width;    // Platform coordinates (X,Y) and dimensions in meters
	int Max_Passenger_Volume, Current_N_Passengers; // Max capacity and current waiting count

	list<pair<string, double>> Current_List_Pax_On_Platform; // Pairs of (passenger ID, planned departure time) for passengers currently waiting
	list<string> List_Trains_Stopping_At_Platform;            // Train services with a planned stop at this platform

	StationPlatform() {
		ID = StationID = BlockSectionID = "None";
		X = Y = length = width = -1;
		Max_Passenger_Volume = Current_N_Passengers = -1;
	}
};

bool orderPassengerListOnPlatform(pair<string, double>& A, pair<string, double>& blockSets);

extern list<StationPlatform> AllStationPlatforms;
extern int numAllStationPlatforms;

// --- Stations: station node extending Node with delay statistics ---

class Stations : public Node {
public:
	int N_StationPlatforms;
	list<string> StationPlatformIDs;   // IDs of platforms at this station
	double Av_Arrival_Delay;           // Average arrival delay
	double Std_Arrival_Delay;          // Standard deviation of arrival delay
	double totalArrivalDelay;          // Sum of arrival delays across all trains
	double Max_TotalDelay;             // Maximum total delay
	double Tot_Consec_Delay;           // Total consecutive delay (total delay minus entrance delay and previous stochastic delays)
	double Max_Cons_Delay;             // Maximum consecutive delay
	int N_Stopped_Trains;              // Number of trains that stopped at this station
	int N_Delayed_Arr;                 // Number of delayed arrivals
	int N_Delayed_Arr_3min;            // Number of arrivals delayed >3 minutes
	int N_Delayed_Arr_5min;            // Number of arrivals delayed >5 minutes
	double Perc_Delayed_T;             // Percentage of trains delayed
	double Perc_Delayed_T_3min;        // Percentage delayed >3 minutes
	double Perc_Delayed_T_5min;        // Percentage delayed >5 minutes
	double latitude, longitude;
	double graphX, graphY;             // Graphical coordinates
	std::map<int, double> shiftX, shiftY;               // Graphical display levels
	std::map<int, double> signalDeltaX, signalDeltaY;    // Unit vector for trackside signal display
	std::vector<int> regions;                            // Geographical regions
	std::map<int, double> regionX;                       // X positions relative to different regions
	std::vector<std::string> corridors;                  // Corridors

	Stations();
};

extern Stations StationArray[95];   // All stations in the network
extern Stations TotalInputDelays;   // Aggregated entrance delays and disturbances
extern Stations EntranceInputDelays;
extern Stations DisturbanceInput;
extern Stations Final_Station;      // Fictitious station to measure delays at trains' final stations
extern int numStations;

void loadStations(char* FileStations);
void printStations();
void Print_Station_Delay_Stats(string Name_StationDelay, string kindofdelay);

// --- Location: can correspond to a block section or a section ---

class Location {
public:
	string Name;
	double MaxHW;     // Maximum headway at this location
	double MinHW;     // Minimum headway at this location
	double Position;
	string MinimumTrainCouple;
	string CriticalTrainCouple;

	Location();

	bool areLocationsEqual(Location blockSets);
};

extern list<Location> AllLocations; // All possible block sections / sections (e.g. for ETCS level 3)

#endif
