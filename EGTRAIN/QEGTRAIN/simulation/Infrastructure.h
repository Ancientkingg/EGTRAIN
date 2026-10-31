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
#include "scene/RuntimeLimits.h"
#include "simulation/NumberGenerator.h"
#include <vector>
#include "util/portability.h"
#include "simulation/InitialParameters.h"


struct SimulationRun;

/// The length of a simulation step in seconds. A step index t is the time in seconds from the start of the run.
inline constexpr double timestep = 1;
/// Signalling speed limits in m/s: signalCode1 caps the section with code 75 at levels 0 and 5, signalCode2 the double
/// red section of level 5, signalCode3 is passed to the routines as V_0, which they do not use
inline constexpr double signalCode1 = 11.111, signalCode2 = 0, signalCode3 = 0;

/// A point element on the route of a train: a signal, a switch edge, a station stop or a border of a track detection
/// section. Route::setListInfrastructureElementsForRoute makes one per node that has a tdsbId.
class InfraElement {
public:
	/// ID of the element, the tdsbId of its node; "VirtualTDSB" at the cut end of a section; "None" when not set.
	std::string ID;
	std::string SectionID;		///< ID of the block section the element belongs to.
	std::string stationName;	///< Name of the station at this element; "None" when it is not a station.
	std::string ConnectedPoint; ///< ID of the other edge of the same diverging switch; "None" otherwise.
	/// Name of the switch. For an edge of a diverging switch it comes from the tdsbId: `@0.22-2-B0@/@0.23-2-B1@Point-Start` gives
	/// `@0.22-2-B0@Point`, and the Point-End variant gives `@0.23-2-B1@Point`. Another switch has its ID; "None" when it is no switch.
	std::string SwitchName;
	/// True when the element is a station stop.
	bool isStation, IsSwitch, IsSignal, IsTrackDetSecBorder; ///< True when the element lies at the start or end of its block section.
	/// \var InfraElement::IsSwitch
	/// True when the node of the element has a connection to another track.
	/// \var InfraElement::IsSignal
	/// Never set to true.

	bool withSwitchDiv;						///< True when the element is an edge of a diverging switch.
	bool isEndOfDivSwitchStartOfADivSwitch; ///< True when the end of one diverging switch is the start of another at this element.
	double XCoordinate;						///< Position along the route in metres, measured in the direction of the route.
	double YCoordinate;						///< Y coordinate of the node in metres.
	/// Position in metres in the direction in which the sections are defined, which does not change when a route is reversed.
	double GeoXCoord, GeoYCoord; ///< Y coordinate in metres of the track detection section border.
	double XConnectedPoint;		 ///< Route position in metres of the other edge of the same diverging switch; -1 when there is none.

	/// Sets the strings to "None", the flags to false and the coordinates to -1.
	InfraElement();
};

/// A point of the network. A track is a chain of nodes joined by arcs. A train also keeps copies of its stop nodes
/// (Train::Stations), and the braking routines use nodes as braking targets.
class Node {
public:
	std::string sceneNodeId; ///< Canonical scene node identity, when built from a SceneModel.
	/// Number of the node: track slot times 1,000,000 plus the index along the track plus 1. A node made inside an arc has
	/// the ID of the start node of the arc plus a small fraction.
	double ID;
	/// Position along the track in kilometres. A position in metres is X * 1000. A braking target node with isSignalled set
	/// holds a route position in metres.
	double X, Y; ///< Y coordinate in kilometres.
	/// True on a braking target node that is an end of authority; its X is then a route position in metres. Nodes of the
	/// infrastructure keep false.
	bool isSignalled;
	bool station; ///< True on a node of a station of the scene: a platform node, or a node at the position of a station.
	/// True when a train stopping at the node may leave only when the aspect of its section is not restrictive. Nothing
	/// outside the tests sets it.
	bool respectOrder;
	double dwellTime; ///< Planned dwell at this stop in seconds; set on the stop nodes of a train.
	/// Dwell at this stop in steps, the planned dwell divided by timestep. The passenger dwell model may lengthen it.
	double StopTime;
	int StepStopped;		  ///< Number of steps the train has stood at this stop; 0 before it arrives.
	bool virtualCouplingNode; ///< True on a braking target node where the train can couple virtually to the train ahead.
	int indexOrderList;		  ///< Not used by the simulation; -1 by default.
	/// Track slot of the track at the other end of each connection at this node.
	int connectIdBlockSet[RuntimeLimits::kNativeMaxNodeConnections];
	/// X in kilometres of the node at the other end of each connection.
	double connectXNode[RuntimeLimits::kNativeMaxNodeConnections];
	int numConnections;						  ///< Number of connections at this node, the used entries of the two arrays above.
	std::list<std::string> IDConnectedBlocks; ///< IDs of the block sections that leave this node through a connection.
	/// Speed in m/s to reach at this node when it is a braking target: the speedInBraking of the arc that ends here.
	double arcSpeedLimit;
	/// Name of the station of the node; empty when there is none.
	std::string stationName, stationPlatformId; ///< ID of the platform of the node; "None" by default.
	std::string tdsbId;							///< ID of the track detection section border at this node; empty when there is none.
	double tdsbGeoCoordX;						///< X of the border in metres in the direction in which the sections are defined; -1 when unset.
	double tdsbGeoCoordY;						///< Y of the border in metres; -1 when unset.
	/// Not set for a node; Stations has its own latitude and longitude.
	double latitude, longitude; ///< Not set for a node.
	bool virtualSignal;			///< True for the signal in the middle of a double switch, which setVirtualSignals marks.

	/// Sets the numbers to 0, the flags to false, indexOrderList and the border coordinates to -1.
	Node();

	/// True when ID, X, Y, isSignalled, station and dwellTime are equal.
	bool operator==(const Node& ob2) const {
		if ((ID == ob2.ID) && (X == ob2.X) && (Y == ob2.Y) && (isSignalled == ob2.isSignalled) && (station == ob2.station) && (dwellTime == ob2.dwellTime))
			return true;
		else
			return false;
	}
};

/// A directed piece of track between two nodes.
class Arc {
public:
	double ID; ///< Index of the arc along its track.
	/// Node at the start of the arc.
	Node startNode, endNode; ///< Node at the end of the arc.
	/// Length of the arc. Metres for the arcs of a track (arcLength). The arcs of a section hold the difference of the X of
	/// their nodes, in kilometres.
	double length, curvature, gradient, speedLimit; ///< Speed limit of the track in m/s.
	/// \var Arc::curvature
	/// Radius of curvature in metres; 0 means straight.
	/// \var Arc::gradient
	/// Rise per length: 0.01 is a 1 % slope, although the scene field is called gradientPercent.

	/// Position of the end node in metres, X * 1000.
	double fs, brakingDistance, speedInBraking; ///< Speed in m/s to reach at the end of the arc, which the signalling sets.
	/// \var Arc::brakingDistance
	/// Set to 0 by the constructor and never given another value; it is only copied and compared.
	/// \var Arc::speedInBraking
	/// It is the limit of the next arc, the lower limit of the next section, 0 before an occupied section or 0 before a stop.

	double signalSpeedLimit; ///< Speed limit in m/s that the signalling imposes; 999 means none.

	/// Sets the numbers to 0 and signalSpeedLimit to 999.
	Arc();

	/// Sets length in metres from the X and Y of the two nodes.
	void arcLength();

	/// True when every member but the nodes is equal and the nodes have the same IDs.
	bool operator==(const Arc& ob2) const {
		if ((ID == ob2.ID) && (length == ob2.length) && (startNode.ID == ob2.startNode.ID) && (endNode.ID == ob2.endNode.ID) && (speedLimit == ob2.speedLimit) && (gradient == ob2.gradient) && (curvature == ob2.curvature) && (fs == ob2.fs) && (brakingDistance == ob2.brakingDistance) && (speedInBraking == ob2.speedInBraking) && (signalSpeedLimit == ob2.signalSpeedLimit))
			return true;
		else
			return false;
	}
};

/// A track (track line): a chain of nodes joined by arcs.
///
/// The runtime builder sizes A, N and member once per track, so every index below arcs (A, member)
/// or numNodes (N) is valid. GUI items keep Node* and Arc* into these buffers: they are valid only
/// until the next runtime reset or rebuild, which the GUI performs only after tearing the items down.
class BlockSet {
public:
	int ID;					  ///< Slot of the track in RunInfrastructure::blockSets.
	std::string sceneTrackId; ///< Canonical scene track identity, when built from a SceneModel.
	int len;				  ///< Number of arcs, the same as arcs.
	/// Number of arcs of the track.
	int arcs, numNodes;		 ///< Number of nodes of the track.
	std::vector<Arc> A;		 ///< Runtime arcs populated from the scene model
	std::vector<Node> N;	 ///< Runtime nodes populated from the scene model
	std::vector<Arc> member; ///< A second copy of the arcs, which the canvas draws and setVirtualSignals reads.
	int graphID;			 ///< Display level of the track on the canvas from the scene views; -1 when they give none.
	bool hasGraphLayout;	 ///< Whether graphID/region came from authored view data
	int region;				 ///< Display region of the track; 0 by default.
	/// Not set by the builders; -1 by default.
	double firstSwitchX, lastSwitchX; ///< Not set by the builders; DBL_MAX by default.

	BlockSet();
};

/// The number of elements of the arrays of tracks, connections and stations, from RuntimeLimits. The builder rejects a
/// scene that needs more.
inline constexpr int kMaxTrackLines = static_cast<int>(RuntimeLimits::kNativeMaxTracks);
/// See kMaxTrackLines.
inline constexpr int kMaxConnections = static_cast<int>(RuntimeLimits::kNativeMaxConnections);
/// See kMaxTrackLines.
inline constexpr int kMaxStations = static_cast<int>(RuntimeLimits::kNativeMaxStations);

/// A connection between two tracks, such as a switch.
class Connections {
public:
	std::string ID; ///< ID of the scene connection.
	/// Scene ID of the end node with the lower X.
	std::string sceneFirstNodeId, sceneSecondNodeId; ///< Scene ID of the end node with the higher X.
	/// Slot of the track of the first node.
	int idFirstTrackLine, idSecondTrackLine; ///< Slot of the track of the second node.
	/// X of the first node in kilometres.
	double xFirstNode, xSecondNode; ///< X of the second node in kilometres.
	double speedlimit;				///< Speed limit in m/s; 16.667 (60 km/h) when the scene gives none.

	/// Sets the numbers to 0 and speedlimit to 16.667 m/s.
	Connections();
};

/// A station platform: its size, capacity and waiting passengers.
class StationPlatform {
public:
	/// ID of the platform. "None" until set.
	std::string ID, StationID, BlockSectionID; ///< ID of the block section that holds the platform node.
	/// \var StationPlatform::StationID
	/// Name of the station, or its ID when it has no name.

	/// X of the platform node in kilometres; -1 until set.
	double X, Y, length, width; ///< Width in metres: the scene value, or 2.5.
	/// \var StationPlatform::Y
	/// Y of the platform node in kilometres; -1 until set.
	/// \var StationPlatform::length
	/// Length in metres: the scene value, or 100.

	/// Number of passengers the platform holds, which the operations builder works out from length and width.
	int Max_Passenger_Volume, Current_N_Passengers; ///< Number of passengers waiting now.

	/// Pairs of passenger ID and the time, in seconds since midnight, from which the passenger waits for the train of its
	/// trip. Sorted by that time.
	std::list<std::pair<std::string, double>> Current_List_Pax_On_Platform;
	std::list<std::string> List_Trains_Stopping_At_Platform; ///< trainDescription of each train with a planned stop at this platform.

	/// Sets the IDs to "None" and the numbers to -1.
	StationPlatform() {
		ID = StationID = BlockSectionID = "None";
		X = Y = length = width = -1;
		Max_Passenger_Volume = Current_N_Passengers = -1;
	}
};

bool orderPassengerListOnPlatform(std::pair<std::string, double>& A, std::pair<std::string, double>& blockSets);

/// The name that the station statistics files give to the statistics at the last stop of every train, SimulationRun::finalStation.
inline constexpr char kFinalStationName[] = "Final_Station";

/// The delay statistics of a set of trains. Delays are in seconds and percentages in percent. The statistics stay -1 until they are
/// computed, and totalArrivalDelay stays 0.
struct DelayStats {
	double Av_Arrival_Delay = -1;	 ///< Average arrival delay
	double Std_Arrival_Delay = -1;	 ///< Standard deviation of arrival delay
	double totalArrivalDelay = 0;	 ///< Sum of arrival delays across all trains
	double Max_TotalDelay = -1;		 ///< Maximum total delay
	double Tot_Consec_Delay = -1;	 ///< Total consecutive delay (total delay minus entrance delay and previous stochastic delays)
	double Max_Cons_Delay = -1;		 ///< Maximum consecutive delay
	int N_Stopped_Trains = -1;		 ///< Number of trains that stopped at this station
	int N_Delayed_Arr = -1;			 ///< Number of delayed arrivals
	int N_Delayed_Arr_3min = -1;	 ///< Number of arrivals delayed >3 minutes
	int N_Delayed_Arr_5min = -1;	 ///< Number of arrivals delayed >5 minutes
	double Perc_Delayed_T = -1;		 ///< Percentage of trains delayed
	double Perc_Delayed_T_3min = -1; ///< Percentage delayed >3 minutes
	double Perc_Delayed_T_5min = -1; ///< Percentage delayed >5 minutes
};

/// A station, with the delay statistics of the trains that stop there.
class Stations : public DelayStats {
public:
	std::string stationName;				   ///< Name of the station, or its ID when it has no name.
	double X = 0;							   ///< Position along the track in kilometres.
	double Y = 0;							   ///< Y coordinate of the first node of the first platform in kilometres; 0 for a station with a position.
	int N_StationPlatforms = 0;				   ///< Number of platforms of the station.
	std::list<std::string> StationPlatformIDs; ///< IDs of platforms at this station
	/// Latitude in degrees from the scene views; 0 when there is none.
	double latitude = 0, longitude = 0; ///< Longitude in degrees from the scene views; 0 when there is none.
	std::vector<int> regions;			///< Display regions of the station from the scene views.
	std::map<int, double> regionX;		///< Position in kilometres of the station in each display region.
	std::vector<std::string> corridors; ///< Corridors of the station from the scene views.
};

void Print_Station_Delay_Stats(const SimulationRun& run, std::string Name_StationDelay, std::string kindofdelay);

#endif
