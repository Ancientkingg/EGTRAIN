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

/// Builds the infrastructure, signalling and routes of a loaded scene into run.infra and the platforms of the run, and empties
/// its final-station statistics. Reads no file. When the diagnostics hold an error, the run keeps the infrastructure, platforms
/// and final-station statistics that it held before.
std::vector<SceneDiagnostic> buildInfrastructureAndSignallingFromScene(SimulationRun& run, const SceneModel& scene);

/// Empties run.infra, the platforms of the run and its final-station statistics.
void resetNativeInfrastructureState(SimulationRun& run);

/// An incident of the selected scenario, as the scene gives it. Times are seconds from the start of the run, compared
/// with the step index.
struct SimulationIncident {
	std::string type;							 ///< "signal_failure" or "train_breakdown".
	std::string target;							 ///< Signal or section ID of a signal failure; service ID of a breakdown.
	double startSeconds = 0.0;					 ///< Start of the incident.
	double endSeconds = 0.0;					 ///< End of the incident; meaningful when hasEndSeconds is true.
	std::vector<std::string> resolvedSectionIDs; ///< Runtime section IDs that a signal failure affects.
	std::string id;								 ///< ID of the incident.
	bool hasOccurrence = false;					 ///< True when the breakdown applies to one occurrence of the service only.
	int occurrence = 1;							 ///< 1-based occurrence of the service when hasOccurrence is true.
	bool hasReducedSpeed = false;				 ///< True when the breakdown caps the speed instead of holding the train.
	double reducedSpeedKmh = 0.0;				 ///< Speed cap of the breakdown in km/h.
	bool terminateAtDestination = false;		 ///< True when the broken-down train is marked as terminated at the end of its route.
	bool hasEndSeconds = false;					 ///< True when the incident has an end; without one a speed cap lasts to the end of the route.
};

bool Incident_Holds_Train(const SimulationRun& run, const std::string& trainDesc, int timestepIndex);
const SimulationIncident* Active_Train_Breakdown(const SimulationRun& run, const std::string& trainDesc, int timestepIndex);
void Apply_Signal_Failures_Mixed_Signalling(SimulationRun& run, int timestepIndex);
void Apply_Single_Track_Authorities_Mixed_Signalling(SimulationRun& run);

/// A record of a train at a place and time. It serves as the passage of a train at an infrastructure element
/// (InfraEvent), as a timetable point of a train (Train::TimetablePoints) and as the train data of a movement authority.
/// Times are seconds from the start of the run, positions metres and speeds m/s. Numbers are -10000 and strings "None"
/// until set, and TrainSpeed is -1.
class TrainEvent {
public:
	std::string trainDescription; ///< trainDescription of the train; for a signal failure authority "signal_failure:" and the incident ID.
	double Time;				  ///< Time of the event: the passage, or the arrival at a timetable point.
	double Time2;				  ///< Departure from a timetable point; unset when the train did not leave it.
	/// Position in metres: the route position of a passage, the scene position of an authority or a timetable point. A
	/// timetable point the train never reached keeps the X of its node in kilometres.
	double Position;
	std::string SuccessorID;	  ///< ID of the next infrastructure element of the route; for a timetable point the name of its station.
	std::string CurrentSectionID; ///< Section of the train when it made a movement authority.
	std::string NextSectionID;	  ///< Next section on the route of the train; "None" at the end of the route.
	double TrainSpeed;			  ///< Train speed at the position given by Position
	double Acceleration;		  ///< Acceleration of the train in m/s2 when it made a movement authority.
	std::string InfraElemStatus;  ///< Position of a switch at the passage: "Straight" or "Diverging"; "None" for other elements.

	TrainEvent();

	/// Copies every member.
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

/// The passages of trains at one infrastructure element, which the blocking times use to find the switch position a
/// train left behind.
class InfraEvent {
public:
	InfraElement ElementInfo;			 ///< The element.
	std::list<TrainEvent> RecordedEvent; ///< One event per train passage, sorted by time after the run.

	InfraEvent();

	/// Copies the element and the events.
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

/// A track detection section: a part of a block section. createTds makes them, and RunInfrastructure::detectionSections
/// holds them.
class TDS {
public:
	std::string blocksection_ID; ///< Not set by createTds; empty.
	std::string ID;				 ///< ID such as `TDS_1@` followed by the block section ID.
	int TracklineID;			 ///< Slot of the track; -1 until set.
	double length;				 ///< Length in kilometres, the difference of the X of the end nodes; -1 until set.
	Node start_node;			 ///< Start node.
	Node end_node;				 ///< End node.
	Node connection_node;		 ///< Switch node of a section with a diverging switch; X is -1 otherwise.
	Node node_on_switch;		 ///< Node halfway along a diverging switch, where its two detection sections meet; X is -1 otherwise.

	bool occupied; ///< Never set to true.

	/// Sets the numbers and the X of connection_node and node_on_switch to -1.
	TDS();
};

/// Main signal aspect of a block section. The codes that go with each state are in docs/architecture/signalling-levels.md.
enum class SignalState : unsigned char {
	Green,	///< Code 270; the default.
	Yellow, ///< Code 180.
	Red,	///< Code 75.
	RedRed	///< Code 751, level 5 only.
};

/// A block section. RunInfrastructure::sections holds the sections of the scene and the sections derived at switches, and
/// a Route holds copies of them, reversed for a reversed route. X values are kilometres.
class Section {
public:
	std::string ID;							  ///< ID of the section.
	int trackLineId;						  ///< Slot of the track the section lies on; -1 for a section derived at a switch.
	int FirstConnectedTrackLineID;			  ///< First track line connected by diverging switch (when withSwitchDiv=true); -1 otherwise.
	int SecondConnectedTrackLineID;			  ///< Second track line connected by diverging switch (when withSwitchDiv=true); -1 otherwise.
	Arc arcs_in_signalling_block_section[20]; ///< Arcs of the section, total_arcs of them; at most 20 (RuntimeLimits::kNativeMaxSectionArcs).
	/// Start node.
	Node start_node, end_node; ///< End node.
	/// Nodes composing the block section, shared by every copy of the section; empty for a default section and a reversed route section
	std::shared_ptr<const std::vector<Node>> nodelist_of_nodes_in_signalling_section;
	int total_nodes;   ///< Number of nodes in the block section
	int total_arcs;	   ///< Number of arcs in the block section
	double length;	   ///< Length in kilometres, the difference of the X of the end nodes.
	double exit_speed; ///< speedInBraking (m/s) of the last arc, set when an aspect pass marks the section occupied; 0 by default. Only copied and compared.
	/// Aspect code: 270 green, 180 yellow, 75 red, 751 red-red, 0 on an occupied or failed section. 270 by default.
	double code;
	double XStartSwitch; ///< X where the diverging switch starts (nonzero only when withSwitchDiv=true)
	double XEndSwitch;	 ///< X where the diverging switch ends (nonzero only when withSwitchDiv=true)
	double GeoXBegNode;	 ///< Position of the start node in metres in scene coordinates, which a reversed copy keeps.
	double GeoXEndNode;	 ///< Position of the end node in metres in scene coordinates, which a reversed copy keeps.
	/// Signalling level 0 to 5 (enum SignallingLevel in scene/SignallingLevel.h); kSignallingLevelUnset (-99999999)
	/// means no level (default)
	int SignallingLevel;
	double ETCS3BrakingPoints[40];			   ///< Set to -1 by the constructor and never given another value.
	std::string ETCS3BrakingPointsTrainID[40]; ///< Set to "None" by the constructor and never given another value.
	int N_ETCS3BrakingPoints;				   ///< Set to 0 by the constructor and never given another value.
	SignalState state;						   ///< Main signal aspect; Green by default.
	bool Occupied;							   ///< Block section occupied by a train
	bool Occup_By_Train;					   ///< Set to false by the constructor and never given another value.
	bool withSwitchDiv;						   ///< Block section contains a diverging switch
	/// IDs of the sections that depend on this one, which are occupied and released with it: at most 10
	/// (RuntimeLimits::kNativeMaxDependencies).
	std::string IDConnectedBS[10];
	int N_ConnectedBS;			  ///< Number of used entries of IDConnectedBS.
	std::list<TDS*> TDS_in_block; ///< Track detection sections of the section; they point into RunInfrastructure::detectionSections.

	Section();

	/// Copies every member; the node list stays shared.
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

	/// True when the ID, the size, the length, the start node, the shared node list, the arcs, exit_speed, code and state are equal.
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

	/// Cut block section: CutPart="CutEnd" removes from CuttingAbscissa to end;
	/// "CutBegin" removes from start to CuttingAbscissa. CuttingAbscissa is an X in kilometres.
	void CutBlockSection(std::string CutPart, double CuttingAbscissa);

	/// Makes this section the reverse of the section passed as blockSets, with X measured back from RouteLength in kilometres.
	void reverseBlockSection(Section blockSets, double RouteLength);


	/// Names a function that does not exist.
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


/// The route of a train: a sequence of copies of block sections. Positions on a route are X in kilometres along the
/// sections of the route; a train position in metres is X * 1000.
class Route {
public:
	std::string ID;									 ///< ID of the scene route; "None" until built.
	int N_Block_Sections;							 ///< Number of block sections composing the route
	std::vector<Section> sequence_of_block_sections; ///< Sequence of block sections, N_Block_Sections elements once the route is built
	double x_of_start_node;							 ///< X in kilometres of the start node of the route.
	double x_of_end_node;							 ///< X in kilometres of the end node of the route.
	/// True when the train runs against the direction in which the route is defined. The sections are then reversed copies,
	/// and positions are measured from the other end: an output position is OriginalRefReversedRoute minus the route position.
	bool reversed_direction;
	double OriginalRefReversedRoute;				///< For a reversed route, the scene X in metres from which route positions are measured back; -1 otherwise.
	std::list<InfraElement> InfrastructureElements; ///< Infrastructure elements along the route (switches, station stop boards, TDS borders)
	std::string corridor;							///< Corridor of the scene route; "A" when it has none.
	/// Where the route jumps between display regions, which have different kilometre references: the length of the jump in
	/// kilometres, and the X at which it happens. {0, 0} when there is no jump.
	std::pair<double, double> diffRegionsJumpX;
	std::pair<double, double> diffRegionsJumpX2; ///< Set to {0, 0} and never given another value.

	Route();

	/// Initialize infrastructure element list for route; each element includes its block section ID
	void setListInfrastructureElementsForRoute(Section BS);

	/// Identify ending edges of diverging switches that are also start of another diverging switch
	void identifyEndingEdgeOfDivSwitchesWhichAreStartingOfADivSwitch(std::list<InfraElement>& RouteInfrastructureElements);

	/// Build route from canonical scene block ids. A positive direction keeps their order, a negative one reverses the
	/// route, and 0 reverses it when the first section lies after the last.
	void createRouteFromBlockIds(SimulationRun& run, const std::vector<std::string>& blockIds, int direction = 0);

	/// Moves the sections after a jump between display regions onto the kilometre reference of the sections before it and
	/// records the jump in diffRegionsJumpX.
	void adjustRouteAcrossDiffRegions();
};

/// Replaces run.infra.routes by the routes of the scene, built from their block IDs. routeDirections[i] above 0 builds route i
/// forward, below 0 reversed (Route::reversed_direction), and 0 or a missing entry builds it forward when it has one block or
/// its first block starts at a lower X than its last. A route that the scene marks as reversed also gets reversed_direction.
void setUpRoutesFromScene(SimulationRun& run, const SceneModel& scene, const std::vector<int>& routeDirections = {});


// --- InfraElementsList: set up list of infrastructure element events ---
void setListAllInfrastructureElementsFromRoutes(SimulationRun& run);

/// An end of authority for levels 3 and 4, and for a failed signal at every level. SimulationRun::etcsMa holds the
/// authorities of the current step. Positions are metres.
class MovementAuthority {
public:
	std::string BSID; ///< ID of the block section of the end of authority; "None" by default.
	/// What the authority stands for: "TrainEnd" (an end of a train), "DivergingSwitchOccupied" (a diverging switch a train
	/// is on) or "SignalFailure" (a failed signal or a held single-track section). "None" by default.
	std::string type;
	/// Which end of the train it is: "Front" or "Tale" (the code spells the rear end this way). A signal failure has
	/// "Front". "None" by default.
	std::string typePart;
	bool ReversedDirection; ///< True when the route of the train that made it runs against the direction of the sections.
	/// Position of the end of authority in scene coordinates, which do not depend on the direction of the route; -1 by default.
	double AbsPosEoA;
	/// Position of the end of authority on the route of the train that received it, which the braking routine sets; -1 by default.
	double RelativePosEoA;
	/// Distance of EoA from BSID start node in train's direction of travel.
	/// If reversed: measured from BSID end node. If forward: from BSID start node. -1 by default.
	double EoA_Dist_From_BSID_Beg;
	TrainEvent TrainInfo; ///< Train info for this EoA: position, speed, description

	MovementAuthority();

	/// Copies every member.
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

/// A train that holds or waits for the direction of a single-track limit.
///
/// Pending entries keep a direction reserved through temporary stops. The identity prevents a removed or retargeted
/// train from leaving a phantom reservation. Several same-direction trains may have committed before a leader clears.
struct SingleTrackReservation {
	/// Index of the train in SimulationRun::trains.
	int trainIndex, routeIndex;	  ///< Index of the route of the train.
	std::string trainDescription; ///< trainDescription of the train.
	/// departure_time of the train in seconds.
	double departureTime, origin, destination; ///< X in kilometres of the end of the route.
	/// \var SingleTrackReservation::origin
	/// X in kilometres of the start of the route, Train::Start_Node_X.

	std::pair<double, double> interval; ///< The zone interval [from, to) in metres on the route that the reservation is for.
};

/// Where the zone of one limit lies on one route: the intervals of route position [from, to) in metres, with
/// neighbouring sections merged, and the IDs of its sections in route order. The zone of a limit is every section
/// between its first and last plain section on a route that has both, plus the two protected sections.
struct SingleTrackZone {
	std::vector<std::pair<double, double>> intervals; ///< Intervals [from, to) of route position in metres.
	std::vector<std::string> sectionIDs;			  ///< IDs of the zone sections in route order.
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

/// The entrance of a station area, which is held while a section of the area ahead is occupied. The pointers point into
/// RunInfrastructure::sections and are not owned.
class StationBoundarySection {
public:
	/// Stores the three values.
	StationBoundarySection(Section* entrance, bool direction, Section* exit);

	/// Protect station interlocking area: marks section sectionIndex of route routeIndex occupied when platformBooked is true,
	/// or when it is the entrance, the route has the protected direction and a section ahead up to the exit is occupied.
	/// Otherwise releases it.
	void protectEntrance(SimulationRun& run, int sectionIndex, int routeIndex, bool platformBooked);

	Section* entrance; ///< Entrance section
	bool direction;	   ///< Routes whose reversed_direction equals this value are protected.
	Section* exit;	   ///< Exit section; null at the end of a line, where the area runs to the end of the route.
};

#endif
