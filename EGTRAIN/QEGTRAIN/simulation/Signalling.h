#ifndef Signalling_hpp
#define Signalling_hpp
#define _CRT_SECURE_NO_WARNINGS

#include <tuple>
#include <map>

#include "simulation/Infrastructure.h"

struct SceneModel;

// --- Incidents: Disruption events ---
struct SimulationIncident {
	std::string type;
	std::string target;
	double startSeconds;
	double endSeconds;
	std::vector<std::string> resolvedSectionIDs; // Resolved section IDs for signal_failure
};

extern std::vector<SimulationIncident> simulationIncidents;
void Load_Incidents(const std::string& MainFolder);
bool Incident_Holds_Train(const std::string& trainDesc, int timestepIndex);

// --- TrainEvent: ordered train events (departures, arrivals, etc.) ---
class TrainEvent {
public:
	string trainDescription;
	double Time;
	double Time2; // Additional time for recording a different event time
	double Position;
	bool StoppedForServiceStop;   // Train is stopped for a service stop
	bool ServiceStopBehindATrain; // Train is stopped behind another train
	string CurrentStoppedStation; // Station the train is currently stopped at
	string SuccessorID;
	string CurrentSectionID;
	string NextSectionID;
	double TrainSpeed;    // Train speed at the position given by Position
	double Acceleration;  // Train acceleration (or equation number used by the train)
	string InfraElemStatus; // Status of a signal or switch at train passage

	TrainEvent();

	TrainEvent& operator=(const TrainEvent& ob2) {
		this->trainDescription = ob2.trainDescription;
		this->Time = ob2.Time;
		this->Time2 = ob2.Time2;
		this->ServiceStopBehindATrain = ob2.ServiceStopBehindATrain;
		this->StoppedForServiceStop = ob2.StoppedForServiceStop;
		this->CurrentStoppedStation = ob2.CurrentStoppedStation;
		Position = ob2.Position;
		SuccessorID = ob2.SuccessorID;
		TrainSpeed = ob2.TrainSpeed;
		Acceleration = ob2.Acceleration;
		CurrentSectionID = ob2.CurrentSectionID;
		NextSectionID = ob2.NextSectionID;
		InfraElemStatus = ob2.InfraElemStatus;
		return *this;
	}
};

// Order TrainEvent list chronologically
bool orderTrainEvents(TrainEvent A, TrainEvent blockSets);

// Order trains in a list when time events are also equal
void orderListOfTrainEvents(list<TrainEvent>& OutputList);

// --- InfraEvent: infrastructure element events (switch position changes, station occupation times, TDS borders) ---
class InfraEvent {
public:
	InfraElement ElementInfo;
	list<TrainEvent> RecordedEvent;

	InfraEvent();

	InfraEvent& operator=(const InfraEvent& ob2) {
		if (this == &ob2)
			return *this;
		this->ElementInfo = ob2.ElementInfo;
		this->RecordedEvent.clear();
		if (ob2.RecordedEvent.size() > 0) {
			for (list<TrainEvent>::const_iterator it = ob2.RecordedEvent.begin(); it != ob2.RecordedEvent.end(); it++) {
				this->RecordedEvent.push_back(*it);
			}
		}
		return *this;
	}
};

extern list<InfraEvent> InfraElementsList;

// Update status of an infrastructure element along a train route given the list of infrastructure elements,
// current time t, train positions at t and t-1, train speed at t-1, and train description
void UpdateInfrastructureElementStatus(const list<InfraElement>& RouteInfrastructureElements, int t, double Xt_1, double Xt, double Vt_1, const string& TrainName);

// Order all recorded infrastructure element events chronologically
void SortRecordedEventsForAllInfrastructureElements(list<InfraEvent>& ListOfAllInfraEvents);

// --- TDS: Track Detection Sections ---
class TDS {
public:
	string blocksection_ID; // Block section this TDS belongs to (fictional in diverging blocks)
	string ID;              // TDS identifier
	int TracklineID;        // Track line identifier
	double length;          // TDS length
	Node start_node;        // Starting node
	Node end_node;          // End node
	Node connection_node;   // Switch node
	Node node_on_switch;    // Third node for switches (replaces third_node)

	bool occupied;

	TDS();
};

extern std::list<TDS> list_of_TDS;

extern int Blocks; // Total number of block sections in the network

// --- Section: Block Section ---
class Section {
public:
	string ID;
	int trackLineId;                               // Track line ID this block belongs to
	int FirstConnectedTrackLineID;                 // First track line connected by diverging switch (when withSwitchDiv=true)
	int SecondConnectedTrackLineID;                // Second track line connected by diverging switch (when withSwitchDiv=true)
	Arc arcs_in_signalling_block_section[20];      // Arcs composing the block section
	Node start_node, end_node;                     // Start and end nodes
	Node* nodelist_of_nodes_in_signalling_section; // Nodes composing the block section
	int total_nodes;                               // Number of nodes in the block section
	int total_arcs;                                // Number of arcs in the block section
	double length;                                 // Block section length
	double exit_speed;                             // Exit speed controlled by signalling system
	double code;
	double XStartSwitch;               // Abscissa where train starts diverging switch (nonzero only when withSwitchDiv=true)
	double XEndSwitch;                 // Final abscissa of diverging switch (nonzero only when withSwitchDiv=true)
	double GeoXBegNode;                // Geographic X coordinate of start node
	double GeoXEndNode;                // Geographic X coordinate of end node
	int SignallingLevel;               // Signalling level: 0=conventional, 1=ETCS L1, 2=ETCS L2, 3=ETCS L3 (default -99999999)
	double ETCS3BrakingPoints[40];     // ETCS L3 braking points (max 2 per train, up to 40 = 20 trains simultaneous)
	string ETCS3BrakingPointsTrainID[40]; // Train IDs for each ETCS3 braking point
	int N_ETCS3BrakingPoints;          // Number of ETCS3 braking points on this section
	char state[20];                    // Main signal aspect
	bool Occupied;                     // Block section occupied by a train
	bool Occup_By_Train;               // Distinguishes train-on-section (true) from switch-occupied (false)
	bool withSwitchDiv;                // Block section contains a diverging switch
	string IDConnectedBS[10];          // IDs of block sections connected by switches
	int N_ConnectedBS;                 // Number of connected block sections
	list<TDS*> TDS_in_block;

	Section();

	Section& operator=(const Section& ob2) {
		if (this == &ob2)
			return *this;
		ID = ob2.ID;
		start_node = ob2.start_node;
		end_node = ob2.end_node;
		total_nodes = ob2.total_nodes;
		total_arcs = ob2.total_arcs;
		GeoXBegNode = ob2.GeoXBegNode;
		GeoXEndNode = ob2.GeoXEndNode;
		length = ob2.length;
		exit_speed = ob2.exit_speed;
		code = ob2.code;
		SignallingLevel = ob2.SignallingLevel;
		nodelist_of_nodes_in_signalling_section = ob2.nodelist_of_nodes_in_signalling_section;
		FirstConnectedTrackLineID = ob2.FirstConnectedTrackLineID;
		SecondConnectedTrackLineID = ob2.SecondConnectedTrackLineID;

		for (int i = 0; i < total_arcs; i++)
			arcs_in_signalling_block_section[i] = ob2.arcs_in_signalling_block_section[i];

		strcpy(state, ob2.state);
		trackLineId = ob2.trackLineId;
		withSwitchDiv = ob2.withSwitchDiv;
		Occupied = ob2.Occupied;
		Occup_By_Train = ob2.Occup_By_Train;
		N_ConnectedBS = ob2.N_ConnectedBS;
		XStartSwitch = ob2.XStartSwitch;
		XEndSwitch = ob2.XEndSwitch;
		for (int i = 0; i < N_ConnectedBS; i++) {
			IDConnectedBS[i] = ob2.IDConnectedBS[i];
		}
		// Copy ETCS3 braking points
		N_ETCS3BrakingPoints = ob2.N_ETCS3BrakingPoints;
		for (int i = 0; i < N_ETCS3BrakingPoints; i++) {
			ETCS3BrakingPoints[i] = ob2.ETCS3BrakingPoints[i];
			ETCS3BrakingPointsTrainID[i] = ob2.ETCS3BrakingPointsTrainID[i];
		}
		TDS_in_block.clear();

		if (ob2.TDS_in_block.size() > 0) {
			for (auto k = ob2.TDS_in_block.begin(); k != ob2.TDS_in_block.end(); k++) {
				this->TDS_in_block.push_back(*k);
			}
		}

		return *this;
	}

	// Equality operator for block sections
	bool operator==(const Section& ob2) {
		bool AreArcsEqual = true;
		for (int i = 0; i < total_arcs && i < ob2.total_arcs; i++) {
			if (!(arcs_in_signalling_block_section[i] == ob2.arcs_in_signalling_block_section[i])) {
				AreArcsEqual = false;
				break;
			}
		}
		if ((ID == ob2.ID) && (total_nodes == ob2.total_nodes) &&
			(total_arcs == ob2.total_arcs) &&
			(length == ob2.length) &&
			(start_node == ob2.start_node) &&
			(nodelist_of_nodes_in_signalling_section == ob2.nodelist_of_nodes_in_signalling_section) &&
			AreArcsEqual &&
			(exit_speed == ob2.exit_speed) &&
			(code == ob2.code) &&
			(!strcmp(state, ob2.state)))
			return true;
		else
			return false;
	}

	// Cut block section: CutPart="CutEnd" removes from CuttingAbscissa to end;
	// "CutBegin" removes from start to CuttingAbscissa
	void CutBlockSection(string CutPart, double CuttingAbscissa);

	// Reverse block section direction
	void reverseBlockSection(Section blockSets, double RouteLength);

	// Change relative coordinates without reversing
	void changeRelativeCoordinatesOfBlockSectionWithoutReversing(Section blockSets, double RouteLength);

	// Set node coordinates relative to start node
	void setRelativeCoordinatesToStartNode();

	// Reset block coordinate origin (used after setRelativeCoordinatesToStartNode when composing routes)
	void resetBlockCoordinatesToDifferentOrigin(double OriginX);

	friend void defSection(Section* BS, int Blocks, BlockSet blockSets);
	friend void SetBlockSpeed();
};

extern Section signalling_block_sections[6000]; // Global block sections array

// Define block section characteristics
void defSection(Section* BS, int N_Block_Previous, BlockSet blockSets);

// Dynamically change block section configuration during optimization
void SetEquiBlock(double L, Section* BS, int& Blocks, BlockSet blockSets);

// Create equal-length block sections for a single track line
void createEquiBlockSections(double L, Section* BS, int& Blocks, BlockSet blockSets);

// Generate all block sections with equal length
void generateAllBlocksWithEquiLength(double L, Section* BS, int& Blocks);

// Load block section lengths from external file
void loadBlockSections(Section* BS, int& Blocks, char* blockname);

// Create block sections with lengths from text files
void createBlockSectionsFromInputFile(Section* BS, int& Blocks, char* blockname, BlockSet blockSets);

void generateAllBlocksFromInputFile(char* FolderName, Section* BS, int& Blocks);

// Generate block sections connected by switches (old version)
void generateConnectBlockOldVersion(Section BS1, Section BS2, Node N1, Node N2, Section& BS3);

// Generate block sections connected by switches (updated, supports custom switch speed limits)
void generateConnectBlock(Connections* AllConnections, Section BS1, Section BS2, Node N1, Node N2, Section& BS3);

// Create connected block sections at connection nodes
void createBlockConn(Node Nb, Section BS1, int Temp_Blocks, int n_conn);

// Automatically create block sections connected by switches
void setAllConnections();

// Create list of sections whose ID contains "input"
void setList(string input, list<Section>& BSConnected);

// Set dependencies between block sections connected by switches
void setDependenciesBetweenBlocks();

// Check if two block sections are overlapping
bool areBlocksConnected(Section A, Section blockSets);

// Set TDS boundaries and geo coordinates at switches and stations
void setTrackDetectionSectionBoundariesAndGeoCoordAtSwitchesAndStations(Section* BS, int N_BlockSections);

// Create track detection sections
void createTds(Section* Blocks, int N_Blocks, int N_TDS_On_Straight_Blocks, list<TDS>& ALL_TDS);

// Set geo coordinates for all block sections
void setGeoCoordinates(Section* BS, int N_BS);

// Print all block section IDs to file (useful for route creation)
void printAllBlocksId();

// Print all TDS and blocks for RECIFE-MILP input
void printAllBlocksForRecife();

// Returns true if BS2 start node coincides with BS1 end node
bool orderBlocks(Section BS1, Section BS2);

// Order block sections in reverse direction
bool reverseOrderBlocks(Section BS1, Section BS2);

// --- Route: train route as sequence of block sections ---
extern int N_Routes; // Total number of routes (from input files in Routes folder)

class Route {
public:
	string ID;
	int N_Block_Sections;                  // Number of block sections composing the route
	Section sequence_of_block_sections[600]; // Sequence of block sections
	double x_of_start_node;                // Start abscissa (initial node of route)
	double x_of_end_node;                  // End abscissa (final node of route)
	bool reversed_direction;               // True when train runs opposite to route definition direction;
	                                       // instant_spatial_position = TotalRouteLength - instant_spatial_position for reversed trains
	double OriginalRefReversedRoute;       // Original end_node abscissa of last block (non-reversed); used to shift abscissas on reversed routes
	list<InfraElement> InfrastructureElements; // Infrastructure elements along the route (switches, station stop boards, TDS borders)
	std::string corridor;                  // Corridor identifier (for multi-region, up to 2 corridors)
	std::pair<double, double> diffRegionsJumpX;  // (jump length, jump lower bound) across regions
	std::pair<double, double> diffRegionsJumpX2; // Second connection point across regions
	Route();

	// Initialize infrastructure element list for route; each element includes its block section ID
	void setListInfrastructureElementsForRoute(Section BS);

	// Identify ending edges of diverging switches that are also start of another diverging switch
	void identifyEndingEdgeOfDivSwitchesWhichAreStartingOfADivSwitch(list<InfraElement>& RouteInfrastructureElements);

	// Load route from file and create it
	void createRoute(char* FileName);

	// Build route from canonical scene block ids.
	void createRouteFromBlockIds(const std::vector<std::string>& blockIds);

	// Adjust route across different regions (different km references)
	void adjustRouteAcrossDiffRegions();
};

extern std::vector<Route> train_route;

void writeRouteFilesFromTracks(string FileName, Section* BS, int N_BS, string RouteFolder);

// Write all routes from input folder
void writeAllRoutes(string InputFolderName, string OutputFolderName, Section* BS, int N_BS);

// Join two routes together
void joinRoutes(int IndexRoute1, int IndexRoute2, bool IsReversed, vector<Route>& AllRoutes, int& numAllRoutes);

// Load joined routes to generate
void loadAllJoinedRoutes(string FolderName, vector<Route>& AllRoutes, int& numAllRoutes);

// Set up all routes
void setUpAllRoutes();

// Set up all routes from canonical scene model.
void setUpRoutesFromScene(const SceneModel& scene);

// Print block sections of a route with start/end nodes (for verification)
void printRoutesBlocks(const Route& R, string FolderName, int IndexOfRoute);

void printAllRoutes(string FolderName);

void printRoutesStations(const Route& R);

// Verify route validity
void verifyRouteValidity(const Route& R, int IndexRoute);

// --- InfraElementsList: set up list of infrastructure element events ---
void setListAllInfrastructureElementsFromRoutes(vector<Route>& R, int N_Routes);

// --- MovementAuthority: ETCS Level 3 Movement Authority ---
class MovementAuthority {
public:
	string BSID;                   // Block section ID
	string type;                   // MA type: locked switch, train, or other element
	string typePart;               // MA refers to Front or Tail of train
	bool ReversedDirection;        // Train traverses block sections opposite to specification direction
	double AbsPosEoA;              // Absolute X position of End of Authority
	double RelativePosEoA;         // Relative EoA position on train route
	double EoA_Dist_From_BSID_Beg; // Distance of EoA from BSID start node in train's direction of travel.
	                               // If reversed: measured from BSID end node. If forward: from BSID start node.
	TrainEvent TrainInfo;          // Train info for this EoA: position, speed, description

	MovementAuthority();

	MovementAuthority& operator=(const MovementAuthority& MA2) {
		BSID = MA2.BSID;
		type = MA2.type;
		ReversedDirection = MA2.ReversedDirection;
		AbsPosEoA = MA2.AbsPosEoA;
		EoA_Dist_From_BSID_Beg = MA2.EoA_Dist_From_BSID_Beg;
		RelativePosEoA = MA2.RelativePosEoA;
		typePart = MA2.typePart;
		TrainInfo = MA2.TrainInfo;
		return *this;
	}
};

// --- Signalling System Functions ---
extern double S_delay; // Signalling system delay in seconds (set to x-1 to get x seconds delay, accounting for timestep)

extern list<string> BlocksOccupied;  // All blocks occupied by trains (directly occupied + connected)
extern list<string> BlocksConnected; // Blocks connected to occupied blocks (released when train leaves)
extern list<MovementAuthority> ETCS_MA; // Movement authorities provided by RBC where ETCS is active

// Occupy a block section and all connected blocks
void occupyBlockAndConnected(const Section& BLS, const Section& BLSPrev, double S_i, double S_i_1);

// Occupy a double switch
void occupyDoubleSwitch(const Section& BLS, const Section& BLSPrev);

// Release a double switch
void releaseDoubleSwitch(const Section& BLS, const Section& BLSPrev);

// Unlock double switches (prevents trains getting stuck in the middle)
void unlockDoubleSwitches();

// Activate blocks connected to block sections with diverging switches
void activateBlocksWithSwitchesDiv(const Section& BS, int TrackLineIDPrevBS, double S);

// Activate blocks connected to block sections with diverging switches (fixed block variant)
void activateBlocksWithSwitchesDivFixedBlock(const Section& BS, int TrackLineIDPrevBS, double S);

// Release last block section and connected blocks when train exits simulation
void releaseLastBlockAndConnected(const Section& BLS);

// Generate MAs on connected block sections when train crosses a connection node
void lockSwitchesOnAllConnectedSections(double FrontEndPos, double BackEndPos, double V_i, double Acc_i, const Section& BS, const string& CurrentSectionID, const string& NextSectionID, const string& trainDescription, bool IsRouteReversed, const string& IDSectionToExclude);

// Process train position and compute Movement Authorities
void elaborateRbcMas(double S_i, double V_i, double Acc_i, const Section& BLS, const string& CurrentSectionID, const string& NextSectionID, const string& trainDescription, const Route& TrainRoute, bool IsRouteReversed, const string& type, const string& typePart);

// Elaborate MAs on block sections with diverging switches
void elaborateMaOnBlockSectionsWithSwitchDiv(double S_i, double V_i, double Acc_i, const Section& BS, const string& trainDescription, const Route& TrainRoute, const string& typePart);

// Lock switches while trains traverse in diverging position
void lockSwitchesWhileTrainTraverses(double FrontEndPos, double BackEndPos, double V_i, double Acc_i, const Section& BLS, const string& trainDescription, const Route& TrainRoute, const string& typePart);

// --- BACC: coded track circuits, cab-signalling (ETCS Level 0, 1 block free behind train) ---
void mTrackCircuitSs1(Section* BS, int Blocks);

// Set permitted speeds at block section nodes (BACC, 1 free block behind)
void setBlockSpeed1(double V_75, double V_751, double V_0, Section* BLS, int Blocks);

// Release block sections when train has passed (for connected blocks and train exit)
void releaseBlocksBacc(Section* BS, int Blocks);

// Release final block section on train exit
void relTrackCircuit1(Section* BS, int Blocks);

// --- ETCS Level 1: Italian SCMT ---
void mEtcsLev1(Section* BS, int Blocks);

// Set permitted speeds at block section nodes (ETCS L1 SCMT)
void setBlockSpeedEtcsLev1(double V_0, Section* BS, int Blocks);

// Release block sections when train has passed
void releaseBlocksEtcsLev1(Section* BS, int Blocks);

// Release final block section on train exit
void relEtcsLev1(Section* BS, int Blocks);

// --- ATB: Dutch National Signalling System (Signalling_Level=2) ---
void mAtb(Section* BS, int Blocks);

// Set permitted speeds at block section nodes (ATB)
void setBlockSpeedAtb(double V_75, double V_0, Section* BS, int Blocks);

// Release block sections when train has passed
void releaseBlocksAtb(Section* BS, int Blocks);

// Release final block section on train exit
void relAtb(Section* BS, int Blocks);

// --- ETCS Level 3: RBC Movement Authorities ---
// Simulate MAs from RBC to train routes
void rbcSendsMasToRoute(Route& R);

void setInfraSpeedInEtcs3(Section* BS, int Blocks);

// Clear previous RBC MAs before updating
void clearPreviousRbcMasForNewUpdating(Route& R);

// Activate signalling system based on train positions at time t
void activateSignallingSystem();

// Release blocks occupied only by connection (not by actual train presence)
void releaseBlockConnections();

// --- Mixed Signalling: multi-system signalling areas ---
// Set infrastructure speed limits (from infrastructure, not signalling)
void setInfraSpeedLimits(Section* BS, int Blocks);

// --- BACC Mixed Signalling (level 4) ---
void baccMixedSignalling(double V_75, double V_751, double V_0, Section* BS, int Blocks);

void setBlockSpeed1MixedSignalling(Section* BLS, int Blocks);

void releaseBlocksBaccMixedSignalling(Section* BS, int Blocks);

void relTrackCircuit1MixedSignalling(Section* BS, int blockIndex);

// --- ATB Mixed Signalling (level 0) ---
void atbMixedSignalling(double V_75, double V_0, Section* BS, int Blocks);

void setBlockSpeedAtbMixedSignalling(Section* BS, int Blocks);

void releaseBlocksAtbMixedSignalling(Section* BS, int Blocks);

void relAtbMixedSignalling(Section* BS, int blockIndex);

// --- ETCS L1 Mixed Signalling (level 1) ---
void etcsLev1MixedSignalling(double V_0, Section* BS, int Blocks);

void setBlockSpeedEtcsLev1MixedSignalling(Section* BS, int Blocks);

void releaseBlocksEtcsLev1MixedSignalling(Section* BS, int Blocks);

void relEtcsLev1MixedSignalling(Section* BS, int blockIndex);

// --- ETCS L2 Mixed Signalling (level 2) ---
void etcsLev2MixedSignalling(double V_0, Section* BS, int Blocks);

void setBlockSpeedEtcsLev2MixedSignalling(Section* BS, int Blocks);

void releaseBlocksEtcsLev2MixedSignalling(Section* BS, int Blocks);

void relEtcsLev2MixedSignalling(Section* BS, int blockIndex);

// --- ETCS L3 Mixed Signalling (level 3) ---
void rbcSendsMasToRouteMixedSignalling(Route& R);

void manageEtcs3TransitionsToOtherSignalling(Section* BS, int Blocks);

// --- Release Blocks in Mixed Signalling ---
void releaseBlocksMixedSignalling(Section* BS, int Blocks);

// Activate mixed signalling areas
void activateMixedSignallingSystem();

// Release all block sections in mixed signalling areas
void releaseMixedSignallingSystem();

// Release last block section in mixed signalling
void relLastSectionMixedSignalling(string blockID);

// Debug: print block codes at time t
void debugFunctionBlockCodes(int t, string BSID, const Route& R);

void showElement(int t, list<string> blockSets);

void showElementInEtcsMa(int t);

void printBlocksAndConnections();

// Set mid-signals of double switches as virtual signals
void setVirtualSignals();

// Load route corridors from input file
void loadRouteCorridors();

// Set virtual signals on routes from original block sections (info lost during route creation)
void setRouteVirtualSignals();

// Single track limits: (first plain block ID, last plain block ID, occupying train, block IDs to block, _)
extern std::vector<std::tuple<std::string, std::string, std::string, std::string, std::string>> singleTrackLimits;

// Load single track limits
void loadSingleTrackLimits();

// Load station boundary sections
void loadStationBoundarySections();

// --- StationBoundarySection: protects entrance of main stations ---
class StationBoundarySection {
public:
	StationBoundarySection(Section* entrance, bool direction, Section* exit);

	// Protect station interlocking area
	void protectEntrance(int sectionIndex, int routeIndex, bool platformBooked);

	Section* entrance; // Entrance section
	bool direction;    // Direction to observe
	Section* exit;     // Exit section (unused at end of lines)
};

extern std::vector<StationBoundarySection> stationBoundarySections;

// Save signal aspect from previous timestep (prevents signalling function interference)
extern std::map<std::string, int> signalAspects;

// Print signal aspect changes over time for offline visualizer
void saveSignalAspectChanges(int t, std::string FolderName);

#endif
