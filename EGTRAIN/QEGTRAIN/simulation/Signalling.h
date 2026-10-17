#ifndef Signalling_hpp
#define Signalling_hpp
#define _CRT_SECURE_NO_WARNINGS

#include <tuple>
#include <map>
#include <memory>
#include <vector>

#include "scene/SceneDiagnostic.h"

#include "simulation/Infrastructure.h"

struct SceneModel;
struct SimulationRun;

// Build the infrastructure/signalling globals from an already loaded scene.
// This path is filesystem-free and is the normal V1 runtime path.
std::vector<SceneDiagnostic> buildInfrastructureAndSignallingFromScene(SimulationRun& run, const SceneModel& scene);

// Release fixed-capacity native infrastructure state between scene runs.
void resetNativeInfrastructureState(SimulationRun& run);

// --- Incidents: Disruption events ---
struct SimulationIncident {
	std::string type;
	std::string target;
	double startSeconds = 0.0;
	double endSeconds = 0.0;
	std::vector<std::string> resolvedSectionIDs; // Resolved section IDs for signal_failure
	std::string id;
	bool hasOccurrence = false;
	int occurrence = 1;
	bool hasReducedSpeed = false;
	double reducedSpeedKmh = 0.0;
	bool terminateAtDestination = false;
	bool hasEndSeconds = false;
};

bool Incident_Holds_Train(const SimulationRun& run, const std::string& trainDesc, int timestepIndex);
const SimulationIncident* Active_Train_Breakdown(const SimulationRun& run, const std::string& trainDesc, int timestepIndex);
void Apply_Signal_Failures_Mixed_Signalling(SimulationRun& run, int timestepIndex);
void Apply_Single_Track_Authorities_Mixed_Signalling(SimulationRun& run);

// --- TrainEvent: ordered train events (departures, arrivals, etc.) ---
class TrainEvent {
public:
	std::string trainDescription;
	double Time;
	double Time2; // Additional time for recording a different event time
	double Position;
	std::string SuccessorID;
	std::string CurrentSectionID;
	std::string NextSectionID;
	double TrainSpeed;			 // Train speed at the position given by Position
	double Acceleration;		 // Train acceleration (or equation number used by the train)
	std::string InfraElemStatus; // Status of a signal or switch at train passage

	TrainEvent();

	TrainEvent& operator=(const TrainEvent& ob2) {
		this->trainDescription = ob2.trainDescription;
		this->Time = ob2.Time;
		this->Time2 = ob2.Time2;
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

// Order trains in a list when time events are also equal
void orderListOfTrainEvents(std::list<TrainEvent>& OutputList);

// --- InfraEvent: infrastructure element events (switch position changes, station occupation times, TDS borders) ---
class InfraEvent {
public:
	InfraElement ElementInfo;
	std::list<TrainEvent> RecordedEvent;

	InfraEvent();

	InfraEvent& operator=(const InfraEvent& ob2) {
		if (this == &ob2)
			return *this;
		this->ElementInfo = ob2.ElementInfo;
		this->RecordedEvent.clear();
		if (ob2.RecordedEvent.size() > 0) {
			for (std::list<TrainEvent>::const_iterator it = ob2.RecordedEvent.begin(); it != ob2.RecordedEvent.end(); it++) {
				this->RecordedEvent.push_back(*it);
			}
		}
		return *this;
	}
};

// Update status of an infrastructure element along a train route given the list of infrastructure elements,
// current time t, train positions at t and t-1, train speed at t-1, and train description
void UpdateInfrastructureElementStatus(SimulationRun& run, const std::list<InfraElement>& RouteInfrastructureElements, int t, double Xt_1, double Xt, double Vt_1, const std::string& TrainName);

// Order all recorded infrastructure element events chronologically
void SortRecordedEventsForAllInfrastructureElements(std::list<InfraEvent>& ListOfAllInfraEvents);

// --- TDS: Track Detection Sections ---
class TDS {
public:
	std::string blocksection_ID; // Block section this TDS belongs to (fictional in diverging blocks)
	std::string ID;				 // TDS identifier
	int TracklineID;			 // Track line identifier
	double length;				 // TDS length
	Node start_node;			 // Starting node
	Node end_node;				 // End node
	Node connection_node;		 // Switch node
	Node node_on_switch;		 // Third node for switches (replaces third_node)

	bool occupied;

	TDS();
};

// Main signal aspect of a block section.
enum class SignalState : unsigned char {
	Green,
	Yellow,
	Red,
	RedRed
};

// --- Section: Block Section ---
class Section {
public:
	std::string ID;
	int trackLineId;						  // Track line ID this block belongs to
	int FirstConnectedTrackLineID;			  // First track line connected by diverging switch (when withSwitchDiv=true)
	int SecondConnectedTrackLineID;			  // Second track line connected by diverging switch (when withSwitchDiv=true)
	Arc arcs_in_signalling_block_section[20]; // Arcs composing the block section
	Node start_node, end_node;				  // Start and end nodes
	// Nodes composing the block section, shared by every copy of the section; empty for a default section and a reversed route section
	std::shared_ptr<const std::vector<Node>> nodelist_of_nodes_in_signalling_section;
	int total_nodes;   // Number of nodes in the block section
	int total_arcs;	   // Number of arcs in the block section
	double length;	   // Block section length
	double exit_speed; // Exit speed controlled by signalling system
	double code;
	double XStartSwitch;					   // Abscissa where train starts diverging switch (nonzero only when withSwitchDiv=true)
	double XEndSwitch;						   // Final abscissa of diverging switch (nonzero only when withSwitchDiv=true)
	double GeoXBegNode;						   // Geographic X coordinate of start node
	double GeoXEndNode;						   // Geographic X coordinate of end node
	int SignallingLevel;					   // Signalling level 0 to 5 (enum SignallingLevel in scene/SignallingLevel.h); kSignallingLevelUnset means no level (default)
	double ETCS3BrakingPoints[40];			   // ETCS L3 braking points (max 2 per train, up to 40 = 20 trains simultaneous)
	std::string ETCS3BrakingPointsTrainID[40]; // Train IDs for each ETCS3 braking point
	int N_ETCS3BrakingPoints;				   // Number of ETCS3 braking points on this section
	SignalState state;						   // Main signal aspect
	bool Occupied;							   // Block section occupied by a train
	bool Occup_By_Train;					   // Distinguishes train-on-section (true) from switch-occupied (false)
	bool withSwitchDiv;						   // Block section contains a diverging switch
	std::string IDConnectedBS[10];			   // IDs of block sections connected by switches
	int N_ConnectedBS;						   // Number of connected block sections
	std::list<TDS*> TDS_in_block;

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

		state = ob2.state;
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
		if ((ID == ob2.ID) && (total_nodes == ob2.total_nodes)
			&& (total_arcs == ob2.total_arcs)
			&& (length == ob2.length)
			&& (start_node == ob2.start_node)
			&& (nodelist_of_nodes_in_signalling_section == ob2.nodelist_of_nodes_in_signalling_section)
			&& AreArcsEqual
			&& (exit_speed == ob2.exit_speed)
			&& (code == ob2.code)
			&& (state == ob2.state))
			return true;
		else
			return false;
	}

	// Cut block section: CutPart="CutEnd" removes from CuttingAbscissa to end;
	// "CutBegin" removes from start to CuttingAbscissa
	void CutBlockSection(std::string CutPart, double CuttingAbscissa);

	// Reverse block section direction
	void reverseBlockSection(Section blockSets, double RouteLength);


	friend void SetBlockSpeed();
};

// Generate block sections connected by switches (updated, supports custom switch speed limits)
void generateConnectBlock(SimulationRun& run, Section BS1, Section BS2, Node N1, Node N2, Section& BS3);

// Create connected block sections at connection nodes
void createBlockConn(SimulationRun& run, Node Nb, Section BS1, int Temp_Blocks, int n_conn);

// Automatically create block sections connected by switches
void setAllConnections(SimulationRun& run);

// Create list of sections whose ID contains "input"
void setList(SimulationRun& run, std::string input, std::list<Section>& BSConnected);

// Set dependencies between block sections connected by switches
void setDependenciesBetweenBlocks(SimulationRun& run);

// Check if two block sections are overlapping
bool areBlocksConnected(Section A, Section blockSets);

// Set TDS boundaries and geo coordinates at switches and stations
void setTrackDetectionSectionBoundariesAndGeoCoordAtSwitchesAndStations(Section* BS, int N_BlockSections);

// Create track detection sections
void createTds(Section* Blocks, int N_Blocks, int N_TDS_On_Straight_Blocks, std::list<TDS>& ALL_TDS);

// Set geo coordinates for all block sections
void setGeoCoordinates(Section* BS, int N_BS);


// --- Route: train route as sequence of block sections ---

class Route {
public:
	std::string ID;
	int N_Block_Sections;							 // Number of block sections composing the route
	std::vector<Section> sequence_of_block_sections; // Sequence of block sections, N_Block_Sections elements once the route is built
	double x_of_start_node;							 // Start abscissa (initial node of route)
	double x_of_end_node;							 // End abscissa (final node of route)
	bool reversed_direction;						 // True when train runs opposite to route definition direction;
													 // instant_spatial_position = TotalRouteLength - instant_spatial_position for reversed trains
	double OriginalRefReversedRoute;				 // Original end_node abscissa of last block (non-reversed); used to shift abscissas on reversed routes
	std::list<InfraElement> InfrastructureElements;	 // Infrastructure elements along the route (switches, station stop boards, TDS borders)
	std::string corridor;							 // Corridor identifier (for multi-region, up to 2 corridors)
	std::pair<double, double> diffRegionsJumpX;		 // (jump length, jump lower bound) across regions
	std::pair<double, double> diffRegionsJumpX2;	 // Second connection point across regions
	Route();

	// Initialize infrastructure element list for route; each element includes its block section ID
	void setListInfrastructureElementsForRoute(Section BS);

	// Identify ending edges of diverging switches that are also start of another diverging switch
	void identifyEndingEdgeOfDivSwitchesWhichAreStartingOfADivSwitch(std::list<InfraElement>& RouteInfrastructureElements);

	// Build route from canonical scene block ids.
	void createRouteFromBlockIds(SimulationRun& run, const std::vector<std::string>& blockIds, int direction = 0);

	// Adjust route across different regions (different km references)
	void adjustRouteAcrossDiffRegions();
};

// Set up all routes from canonical scene model.
void setUpRoutesFromScene(SimulationRun& run, const SceneModel& scene, const std::vector<int>& routeDirections = {});


// --- InfraElementsList: set up list of infrastructure element events ---
void setListAllInfrastructureElementsFromRoutes(SimulationRun& run);

// --- MovementAuthority: end of authority for levels 3 and 4, and for a failed signal at every level ---
class MovementAuthority {
public:
	std::string BSID;			   // Block section ID
	std::string type;			   // MA type: locked switch, train, or other element
	std::string typePart;		   // MA refers to Front or Tail of train
	bool ReversedDirection;		   // Train traverses block sections opposite to specification direction
	double AbsPosEoA;			   // Absolute X position of End of Authority
	double RelativePosEoA;		   // Relative EoA position on train route
	double EoA_Dist_From_BSID_Beg; // Distance of EoA from BSID start node in train's direction of travel.
								   // If reversed: measured from BSID end node. If forward: from BSID start node.
	TrainEvent TrainInfo;		   // Train info for this EoA: position, speed, description

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

// Occupy a block section and all connected blocks
void occupyBlockAndConnected(SimulationRun& run, const Section& BLS, const Section& BLSPrev, double S_i, double S_i_1);

// Occupy a double switch
void occupyDoubleSwitch(SimulationRun& run, const Section& BLS, const Section& BLSPrev);

// Release a double switch
void releaseDoubleSwitch(SimulationRun& run, const Section& BLS, const Section& BLSPrev);

// Unlock double switches (prevents trains getting stuck in the middle)
void unlockDoubleSwitches(SimulationRun& run);

// Activate blocks connected to block sections with diverging switches
void activateBlocksWithSwitchesDiv(SimulationRun& run, const Section& BS, int TrackLineIDPrevBS, double S);

// Activate blocks connected to block sections with diverging switches (fixed block variant)
void activateBlocksWithSwitchesDivFixedBlock(SimulationRun& run, const Section& BS, int TrackLineIDPrevBS, double S);

// Release last block section and connected blocks when train exits simulation
void releaseLastBlockAndConnected(SimulationRun& run, const Section& BLS);

// Generate MAs on connected block sections when train crosses a connection node
void lockSwitchesOnAllConnectedSections(SimulationRun& run, double FrontEndPos, double BackEndPos, double V_i, double Acc_i, const Section& BS, const std::string& CurrentSectionID, const std::string& NextSectionID, const std::string& trainDescription, bool IsRouteReversed, const std::string& IDSectionToExclude);

// Process train position and compute Movement Authorities
void elaborateRbcMas(SimulationRun& run, double S_i, double V_i, double Acc_i, const Section& BLS, const std::string& CurrentSectionID, const std::string& NextSectionID, const std::string& trainDescription, const Route& TrainRoute, bool IsRouteReversed, const std::string& type, const std::string& typePart);

// Elaborate MAs on block sections with diverging switches
void elaborateMaOnBlockSectionsWithSwitchDiv(SimulationRun& run, double S_i, double V_i, double Acc_i, const Section& BS, const std::string& trainDescription, const Route& TrainRoute, const std::string& typePart);

// Lock switches while trains traverse in diverging position
void lockSwitchesWhileTrainTraverses(SimulationRun& run, double FrontEndPos, double BackEndPos, double V_i, double Acc_i, const Section& BLS, const std::string& trainDescription, const Route& TrainRoute, const std::string& typePart);

// --- Mixed Signalling: multi-system signalling areas ---
// Set infrastructure speed limits (from infrastructure, not signalling)
void setInfraSpeedLimits(Section* BS, int Blocks);

// --- ETCS L3 Mixed Signalling (levels 3 and 4) ---
// occupied has one flag per section of the route, nonzero when the section is in BlocksOccupied.
void rbcSendsMasToRouteMixedSignalling(Route& R, const char* occupied);

void manageEtcs3TransitionsToOtherSignalling(Section* BS, int Blocks);

// --- Release Blocks in Mixed Signalling ---
// connected has one flag per section of the route, nonzero when the section is in BlocksConnected.
void releaseBlocksMixedSignalling(Section* BS, int Blocks, const char* connected);

// Bit n of the result is set when a section of the route has signalling level n (0 to 5). A section without a level, or
// with a number that is no level, sets no bit.
unsigned routeLevelMask(const Route& route);

// Activate mixed signalling areas
void activateMixedSignallingSystem(SimulationRun& run);
// The same with other red speeds, the limits that the fixed-block levels write behind an occupied section. The call above passes
// signalCode1 and signalCode2.
void activateMixedSignallingSystem(SimulationRun& run, double redSpeed1, double redSpeed2);

// Release all block sections in mixed signalling areas
void releaseMixedSignallingSystem(SimulationRun& run);

// Release last block section in mixed signalling
void relLastSectionMixedSignalling(SimulationRun& run, std::string blockID);


// Set mid-signals of double switches as virtual signals
void setVirtualSignals(SimulationRun& run);

// Set virtual signals on routes from original block sections (info lost during route creation)
void setRouteVirtualSignals(SimulationRun& run);

// Pending entries keep a direction reserved through temporary stops. The identity prevents a removed or retargeted
// train from leaving a phantom reservation. Several same-direction trains may have committed before a leader clears.
struct SingleTrackReservation {
	int trainIndex, routeIndex;
	std::string trainDescription;
	double departureTime, origin, destination;
	std::pair<double, double> interval;
};

// Where the zone of one limit lies on one route: the intervals of route position [from, to) in metres, with
// neighbouring sections merged, and the IDs of its sections in route order. The zone of a limit is every section
// between its first and last plain section on a route that has both, plus the two protected sections.
struct SingleTrackZone {
	std::vector<std::pair<double, double>> intervals;
	std::vector<std::string> sectionIDs;
};
// Forgets the held directions and the derived zones. Call it whenever the limits or the routes change.
void resetSingleTrackLocks(SimulationRun& run);
// The zone of limit l on route routeIndex. The zones of all limits and routes are derived on first use.
const SingleTrackZone& singleTrackZone(SimulationRun& run, std::size_t l, int routeIndex);
// True when the zone of any limit has a section on the route.
bool singleTrackRouteHasZone(SimulationRun& run, int routeIndex);
// Appends the zone sections that the route has to treat as occupied to BlocksOccupied and returns how many were added.
std::size_t occupySingleTrackForRoute(SimulationRun& run, int routeIndex);
// Updates occupancy and pending entry reservations. Every changed zone is released (RollingStock.cpp).
void updateSingleTrackLocks(SimulationRun& run, int step);

// --- StationBoundarySection: protects entrance of main stations ---
class StationBoundarySection {
public:
	StationBoundarySection(Section* entrance, bool direction, Section* exit);

	// Protect station interlocking area
	void protectEntrance(SimulationRun& run, int sectionIndex, int routeIndex, bool platformBooked);

	Section* entrance; // Entrance section
	bool direction;	   // Direction to observe
	Section* exit;	   // Exit section (unused at end of lines)
};

#endif
