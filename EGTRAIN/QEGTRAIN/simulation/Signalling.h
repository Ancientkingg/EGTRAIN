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

/// True while a train_breakdown incident without a reduced speed holds the train trainDesc ("<service>-<occurrence>") in step
/// timestepIndex.
bool Incident_Holds_Train(const SimulationRun& run, const std::string& trainDesc, int timestepIndex);
/// The train_breakdown incident of run.incidents that applies to the train trainDesc in step timestepIndex, from its start second up to
/// its end second, or nullptr. A breakdown that stops the train comes first; of the speed caps the lowest reducedSpeedKmh wins.
const SimulationIncident* Active_Train_Breakdown(const SimulationRun& run, const std::string& trainDesc, int timestepIndex);
/// Applies the signal_failure incidents of step timestepIndex: each failed section joins run.blocksOccupied, and every route that
/// contains it gets an end of authority in front of it. In the step after a failure ends, its sections go to run.blocksConnected to
/// be released. See "Common behaviour" in docs/architecture/signalling-levels.md.
void Apply_Signal_Failures_Mixed_Signalling(SimulationRun& run, int timestepIndex);
/// Gives every route against the direction that holds a single-track zone an end of authority in front of each stretch of the zone on
/// that route, at levels 0 to 5. The authorities last for the current step.
void Apply_Single_Track_Authorities_Mixed_Signalling(SimulationRun& run);

/// A record of a train at a place and time. It serves as the passage of a train at an infrastructure element
/// (InfraEvent), as a timetable point of a train (Train::TimetablePoints) and as the train data of a movement authority.
/// Times are seconds from the start of the run, positions metres and speeds m/s. Numbers are -10000 and strings "None"
/// until set, and TrainSpeed is -1.
class TrainEvent {
public:
	/// trainDescription of the train; for a signal failure authority "signal_failure:" and the incident ID (its target
	/// when it has none); for a held single-track section "single_track:" and the limit ID.
	std::string trainDescription;
	double Time;  ///< Time of the event: the passage, or the arrival at a timetable point.
	double Time2; ///< Departure from a timetable point; unset when the train did not leave it.
	/// Position in metres: the route position of a passage, the scene position of an authority or a timetable point. A
	/// timetable point the train never reached keeps the X of its node in kilometres.
	double Position;
	std::string SuccessorID;	  ///< ID of the next infrastructure element of the route; for a timetable point the name of its station.
	std::string CurrentSectionID; ///< Section of the train when it made a movement authority.
	std::string NextSectionID;	  ///< Next section on the route of the train; "None" at the end of the route.
	double TrainSpeed;			  ///< Train speed at the position given by Position
	double Acceleration;		  ///< Acceleration of the train in m/s2 when it made a movement authority.
	std::string InfraElemStatus;  ///< Position of a switch at the passage: "Straight" or "Diverging"; "None" for other elements.

	/// Sets the times, the position and the acceleration to -10000, the speed to -1 and the IDs and the status to "None".
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

/// Sorts the events by Time, earliest first, with non-finite times last. The sort is stable, so events with equal times keep their
/// order.
void orderListOfTrainEvents(std::list<TrainEvent>& OutputList);

/// The passages of trains at one infrastructure element, which the blocking times use to find the switch position a
/// train left behind.
class InfraEvent {
public:
	InfraElement ElementInfo;			 ///< The element.
	std::list<TrainEvent> RecordedEvent; ///< One event per train passage, sorted by time after the run.

	/// Sets the coordinates of the element to -1, its IDs and station name to "None" and its flags to false; no events.
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

/// Records in run.infra.infraEvents a passage of the train TrainName over each element of RouteInfrastructureElements that its head
/// passed in step t, from route position Xt_1 to Xt (m). The event has the time t - 1 (s), the speed Vt_1 (m/s), the next element of
/// the route, and for a switch the position "Straight" or "Diverging".
void UpdateInfrastructureElementStatus(SimulationRun& run, const std::list<InfraElement>& RouteInfrastructureElements, int t, double Xt_1, double Xt, double Vt_1, const std::string& TrainName);

/// Sorts the recorded events of every element with orderListOfTrainEvents.
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

	/// An empty section with code 270, state Green and no signalling level (kSignallingLevelUnset). The track line IDs and
	/// the ETCS3 braking points are -1.
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

/// When N1 lies before N2, makes BS3 the section over a switch: BS1 up to node N1, a switch arc from N1 to N2 with the speed limit of
/// the matching connection, and BS2 after N2. Its ID is "<BS1>-<N1>/<BS2>-<N2>" with the node coordinates in km.
void generateConnectBlock(SimulationRun& run, Section BS1, Section BS2, Node N1, Node N2, Section& BS3);

/// Appends to run.infra.sections the section over connection n_conn of node Nb of BS1 (generateConnectBlock), when the connection
/// leads to a higher coordinate and none of the first Temp_Blocks sections has that ID in either direction. When the planned array is
/// full it sets run.infra.derivedSectionsExceedPlan instead.
void createBlockConn(SimulationRun& run, Node Nb, Section BS1, int Temp_Blocks, int n_conn);

/// Makes the section over every connection of every node of the sections in run.infra.sections (createBlockConn).
void setAllConnections(SimulationRun& run);

/// Appends to BSConnected a copy of every section of run.infra.sections whose ID contains input.
void setList(SimulationRun& run, std::string input, std::list<Section>& BSConnected);

/// Gives every section the IDs of the other sections whose ID contains its own (IDConnectedBS, at most 10). This entry point also adds
/// the fixed legacy dependency of section "@5-B6@" and copies the IDs to the switch nodes (Node::IDConnectedBlocks).
void setDependenciesBetweenBlocks(SimulationRun& run);

/// True when the two sections overlap (not connect): their coordinate ranges overlap and their IDs share a block.
bool areBlocksConnected(Section A, Section blockSets);

/// Gives the switch and station nodes of the N_BlockSections sections of BS a track detection border ID (tdsbId) and geographic
/// coordinates in metres (tdsbGeoCoordX and tdsbGeoCoordY, the node X and Y times 1000).
void setTrackDetectionSectionBoundariesAndGeoCoordAtSwitchesAndStations(Section* BS, int N_BlockSections);

/// Splits each of the N_Blocks sections of Blocks into track detection sections and appends them to ALL_TDS, which owns them; the
/// TDS_in_block list of the section points into ALL_TDS. A section without a diverging switch gets N_TDS_On_Straight_Blocks of equal
/// length (at most 20).
void createTds(Section* Blocks, int N_Blocks, int N_TDS_On_Straight_Blocks, std::list<TDS>& ALL_TDS);

/// Sets GeoXBegNode and GeoXEndNode of the N_BS sections of BS to the X of their start and end node in metres.
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

	/// An empty route with ID "None" on corridor "A"; OriginalRefReversedRoute is -1.
	Route();

	/// Appends the infrastructure elements of section BS to InfrastructureElements: its TDS borders, stations and switches, with their
	/// route position in metres and the ID of BS. An element with the ID or position of one already listed is left out; a
	/// diverging switch edge only when its ID, switch name and connected point all match.
	void setListInfrastructureElementsForRoute(Section BS);

	/// Marks with isEndOfDivSwitchStartOfADivSwitch each diverging switch edge of the list that has the position and switch name of
	/// another edge with a different connected point: the end of one diverging switch that is the start of the next.
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


/// Fills run.infra.infraEvents with one entry for each distinct infrastructure element of all routes, without the "VirtualTDSB"
/// borders. A diverging switch edge is listed under its switch name.
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

	/// Sets BSID, type and typePart to "None" and the positions to -1.
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

/// Adds BLS and its connected sections to run.blocksOccupied, and puts BLSPrev and its connected sections in run.blocksConnected
/// (releaseLastBlockAndConnected). S_i and S_i_1 are the tail positions (route, m) of this step and the step before.
void occupyBlockAndConnected(SimulationRun& run, const Section& BLS, const Section& BLSPrev, double S_i, double S_i_1);

/// Occupies the two halves BLS and BLSPrev of a double switch: each half with its connected sections, and the two block sections
/// named in its ID with theirs, go to run.blocksOccupied; the two block sections also go to run.blocksConnected. Does nothing when an
/// ID does not name two sections of run.infra.sections.
void occupyDoubleSwitch(SimulationRun& run, const Section& BLS, const Section& BLSPrev);

/// Releases the two halves BLS and BLSPrev of a double switch: each half and its connected sections go to run.blocksConnected, and
/// for a half with a diverging switch also the two block sections named in its ID with theirs. Does nothing when an ID does not name
/// two sections of run.infra.sections.
void releaseDoubleSwitch(SimulationRun& run, const Section& BLS, const Section& BLSPrev);

/// Frees the double switches that would hold a train in their middle; see "Double switches" under "Common behaviour" in
/// docs/architecture/signalling-levels.md.
void unlockDoubleSwitches(SimulationRun& run);

/// For a train with its tail at route position S (m) on the level 3 or 4 section BS with a diverging switch: puts the block sections
/// named in the ID of BS in run.blocksOccupied: the first while S is before the switch, both while it is on the switch, and the second
/// after it, when both also go to run.blocksConnected. The two swap when TrackLineIDPrevBS is the track line of the second.
void activateBlocksWithSwitchesDiv(SimulationRun& run, const Section& BS, int TrackLineIDPrevBS, double S);

/// The fixed-block form of activateBlocksWithSwitchesDiv: both block sections named in the ID of BS go to run.blocksOccupied and
/// run.blocksConnected. S is not read.
void activateBlocksWithSwitchesDivFixedBlock(SimulationRun& run, const Section& BS, int TrackLineIDPrevBS, double S);

/// Puts BLS, its connected sections and, for a section with a diverging switch, the two block sections named in its ID in
/// run.blocksConnected, so that releaseMixedSignallingSystem clears them.
void releaseLastBlockAndConnected(SimulationRun& run, const Section& BLS);

/// While the train spans a switch node of BS (FrontEndPos at or past it and BackEndPos at or before it, m), adds to run.etcsMa
/// "DivergingSwitchOccupied" ends of authority at the switch on BS and at both edges of every section over that switch, except
/// IDSectionToExclude ("None" excludes nothing). V_i (m/s) and Acc_i (m/s2) are the speed and acceleration of the train.
void lockSwitchesOnAllConnectedSections(SimulationRun& run, double FrontEndPos, double BackEndPos, double V_i, double Acc_i, const Section& BS, const std::string& CurrentSectionID, const std::string& NextSectionID, const std::string& trainDescription, bool IsRouteReversed, const std::string& IDSectionToExclude);

/// Adds to run.etcsMa an end of authority of the given type and typePart for the train at route position S_i (m) on BLS, with its
/// speed V_i (m/s) and acceleration Acc_i (m/s2), unless the train has one on the same section within 1 mm. A position before the
/// route is moved to the start of its first section; on a reversed route AbsPosEoA is in the coordinates of the original direction.
void elaborateRbcMas(SimulationRun& run, double S_i, double V_i, double Acc_i, const Section& BLS, const std::string& CurrentSectionID, const std::string& NextSectionID, const std::string& trainDescription, const Route& TrainRoute, bool IsRouteReversed, const std::string& type, const std::string& typePart);

/// The form of elaborateRbcMas for the section BS with a diverging switch: adds "TrainEnd" ends of authority at S_i (m) on BS and on
/// the block sections named in its ID that S_i covers, the first before the switch, both on it and the second after it.
void elaborateMaOnBlockSectionsWithSwitchDiv(SimulationRun& run, double S_i, double V_i, double Acc_i, const Section& BS, const std::string& trainDescription, const Route& TrainRoute, const std::string& typePart);

/// While the train spans the diverging switch of BLS (FrontEndPos at or past its start and BackEndPos at or before its end, m), adds
/// "DivergingSwitchOccupied" ends of authority at both switch edges, on BLS and on the two block sections named in its ID, and locks
/// the switches that connect to those (lockSwitchesOnAllConnectedSections, 10 m on either side of each edge).
void lockSwitchesWhileTrainTraverses(SimulationRun& run, double FrontEndPos, double BackEndPos, double V_i, double Acc_i, const Section& BLS, const std::string& trainDescription, const Route& TrainRoute, const std::string& typePart);

// --- Mixed Signalling: multi-system signalling areas ---
/// Sets speedInBraking of every arc of the Blocks route sections BS to the speed limit of the next arc (m/s). The last arc of the
/// route takes its own limit.
void setInfraSpeedLimits(Section* BS, int Blocks);

// --- ETCS L3 Mixed Signalling (levels 3 and 4) ---
/// Gives the occupied sections of levels 3 and 4 code 0 and the end speed of their last arc as exit speed, so that the routines of the
/// other levels see them. occupied has one flag per section of the route, nonzero when the section is in BlocksOccupied.
void rbcSendsMasToRouteMixedSignalling(Route& R, const char* occupied);

/// Sets the end speed (speedInBraking of the last arc, m/s) of every level 3 and 4 section of the route: 0 before an occupied section
/// of another level, otherwise the lower of the speed limit and the signal speed limit at the start of the next section.
void manageEtcs3TransitionsToOtherSignalling(Section* BS, int Blocks);

// --- Release Blocks in Mixed Signalling ---
/// Clears each route section flagged in connected (one flag per route section, nonzero when in BlocksConnected) and the sections
/// behind it. See "Codes and what they do to a following train" in docs/architecture/signalling-levels.md.
void releaseBlocksMixedSignalling(Section* BS, int Blocks, const char* connected);

/// Bit n of the result is set when a section of the route has signalling level n (0 to 5). A section without a level, or
/// with a number that is no level, sets no bit.
unsigned routeLevelMask(const Route& route);

/// Writes the codes, states and speed limits of every route for the next step from run.blocksOccupied, with the routine of each level
/// the route has. See "What happens in a step" in docs/architecture/signalling-levels.md.
void activateMixedSignallingSystem(SimulationRun& run);
/// The same with other red speeds (m/s), the limits that the fixed-block levels write behind an occupied section: redSpeed1 on the red
/// section of levels 0 and 5, redSpeed2 on the double red section of level 5. The call above passes signalCode1 and signalCode2.
void activateMixedSignallingSystem(SimulationRun& run, double redSpeed1, double redSpeed2);

/// Clears every route behind the sections in run.blocksConnected (releaseBlocksMixedSignalling), so that the activation can write the
/// aspects again.
void releaseMixedSignallingSystem(SimulationRun& run);

/// Releases section blockID when a train leaves its route there; see "Codes and what they do to a following train" in
/// docs/architecture/signalling-levels.md.
void relLastSectionMixedSignalling(SimulationRun& run, std::string blockID);


/// Marks the middle of each double switch as a virtual signal: in a track line of three arcs that is 30 m long, the section that
/// starts at the start of the track line gets a virtual signal at its end node, any other at its start node, and so do the sections
/// connected to it.
void setVirtualSignals(SimulationRun& run);

/// Copies the virtual signals of run.infra.sections to the section copies of every route, with start and end swapped on a reversed
/// route.
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
/// Forgets the held directions and the derived zones. Call it whenever the limits or the routes change.
void resetSingleTrackLocks(SimulationRun& run);
/// The zone of limit l on route routeIndex, or an empty zone for an index out of range. The zones of all limits and routes are
/// derived on first use.
const SingleTrackZone& singleTrackZone(SimulationRun& run, std::size_t l, int routeIndex);
/// True when the zone of any limit has a section on the route.
bool singleTrackRouteHasZone(SimulationRun& run, int routeIndex);
/// Appends the zone sections that the route has to treat as occupied to BlocksOccupied and returns how many were added.
std::size_t occupySingleTrackForRoute(SimulationRun& run, int routeIndex);
/// Updates occupancy and pending entry reservations from the positions of step `step`. Every changed zone is released (RollingStock.cpp).
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
