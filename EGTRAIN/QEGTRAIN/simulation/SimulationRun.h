#ifndef SIMULATIONRUN_H
#define SIMULATIONRUN_H

#include "simulation/InitialParameters.h"
#include "simulation/Infrastructure.h"
#include "simulation/NumberGenerator.h"
#include "simulation/Passengers.h"
#include "simulation/RollingStock.h"
#include "simulation/Signalling.h"

#include <list>
#include <string>
#include <tuple>
#include <vector>

/// The infrastructure and the routes that the scene builder makes: the tracks, connections and stations of the network, its block
/// sections and detection sections, the single-track data and the routes of the trains.
///
/// The arrays of tracks, connections and stations have a fixed number of elements that nothing resizes, so the elements of a built
/// infrastructure and the buffers that they hold stay where they are until the infrastructure is reset or replaced. A build that fails
/// puts the earlier infrastructure back as it was. The canvas keeps Node* and Arc* into the tracks and Connections* into the
/// connections, a section keeps TDS* into the detection sections, and the station boundaries keep Section* into the sections. These
/// pointers are valid until the next reset or build, so the window removes its items first.
struct RunInfrastructure {
	/// The tracks: kMaxTrackLines elements, of which the first numTrackLines are in use.
	std::vector<BlockSet> blockSets = std::vector<BlockSet>(kMaxTrackLines);
	/// The connections between tracks: kMaxConnections elements, of which the first numConnections are in use.
	std::vector<Connections> connections = std::vector<Connections>(kMaxConnections);
	/// The stations: kMaxStations elements, of which the first numStations are in use.
	std::vector<Stations> stations = std::vector<Stations>(kMaxStations);
	int numTrackLines = 0;	///< Number of tracks in use.
	int numConnections = 0; ///< Number of connections in use.
	int numStations = 0;	///< Number of stations in use.

	/// The block sections of the network, sized once per scene by the builder.
	std::vector<Section> sections;
	/// Number of block sections.
	int numBlocks = 0;
	/// The track detection sections. The block sections point into this list.
	std::list<TDS> detectionSections;
	/// The recorded events of the infrastructure elements.
	std::list<InfraEvent> infraEvents;
	/// Set when createBlockConn needs more sections than the builder planned.
	bool derivedSectionsExceedPlan = false;

	/// The single-track limits (first plain block ID, last plain block ID, protected block ID at the first end, protected block ID at
	/// the last end).
	std::vector<std::tuple<std::string, std::string, std::string, std::string>> singleTrackLimits;
	/// The direction in which a train holds each limit. While a train is in the section of limit l, the section is closed to trains
	/// of the opposite direction: singleTrackHeld[l] is +1 when a train on a non-reversed route holds it, -1 for a reversed route,
	/// 0 when it is free. Routes against the holder see it as occupied.
	std::vector<int> singleTrackHeld;
	/// The pending reservations of each limit. Pending entries reserve before their last braking opportunity; forward wins
	/// simultaneous requests on a free zone.
	std::vector<std::vector<SingleTrackReservation>> singleTrackReservations;
	/// The zones of the limits on the routes, derived on first use by singleTrackZone. The index is limit * route count + route.
	std::vector<SingleTrackZone> singleTrackZones;
	/// The numbers of limits and of routes for which singleTrackZones was derived. A change in either derives the zones again.
	std::size_t singleTrackZoneLimits = 0, singleTrackZoneRoutes = 0; ///< See singleTrackZoneLimits.
	/// 1 for a route on which any limit has a zone.
	std::vector<char> singleTrackRouteZone;
	/// True once singleTrackZones has been derived.
	bool singleTrackZonesDerived = false;
	/// The station entrance sections.
	std::vector<StationBoundarySection> stationBoundaries;

	/// The routes of the scene, in the order of the scene.
	std::vector<Route> routes;
	/// Number of routes.
	int numRoutes = 0;

	/// Returns every member to the state of a new object.
	void reset();
};

/// The state that one simulation run builds up and that the next run must not see.
///
/// The routines of the simulation library take the run that they work on as a parameter, and the
/// member functions of a train reach it through Train::run_, which the operations builder sets.
///
/// The process has one run, defaultRun(). It has a fixed address, so reset() clears every member in
/// place. The window reads the run through DispatchController::run(), which gives the same run; a test
/// that calls the builders itself passes it defaultRun() or a run of its own. DispatchController::prepareScene
/// clears it with reset(), builds a run apart from it and puts the finished run in its place with adopt(). A scene
/// with errors leaves it cleared, the infrastructure included. DispatchController::runSimulation then steps the run
/// on the thread of the run and adds the results, which stay until the next scene is prepared. A member that is added
/// here must be cleared in reset(), or in resetInfrastructure() when it belongs to the
/// infrastructure, and a member that points at another member needs a line in adopt().
///
/// Besides the run, the simulation uses this process-wide state: initial_variables (the options, which the operations
/// builder reads and writes and prepareScene copies into settings), the controller simulation, SimulationWorker::active(),
/// and the constants timestep (1 s), signalCode1 to signalCode3 and the array sizes kMaxTrackLines, kMaxConnections and
/// kMaxStations of Infrastructure.h.
struct SimulationRun {
	/// The settings of the run: a copy of initial_variables that DispatchController::prepareScene takes once the scene is built. It holds
	/// the output folder and the buffer time and running time recovery of the run. A change to initial_variables after that does not
	/// reach the run.
	InitialParameters settings{0};

	/// IDs of the block sections that trains occupy. The signalling routines fill the list during a step and the step clears it.
	std::list<std::string> blocksOccupied;
	/// IDs of the block sections connected to the occupied ones, filled and cleared like blocksOccupied.
	std::list<std::string> blocksConnected;
	/// Movement authorities of the current step: those of levels 3 and 4, and the failed signals at every level.
	std::list<MovementAuthority> etcsMa;

	/// Incidents of the selected scenario.
	std::vector<SimulationIncident> incidents;

	/// Step of each virtual coupling notice of the run, in seconds from the start of the run.
	std::vector<int> vcMsgTimestep;
	/// Train of each virtual coupling notice.
	std::vector<std::string> vcMsgTrain;
	/// Text of each virtual coupling notice.
	std::vector<std::string> vcMsgText;

	/// The platforms of the network.
	std::list<StationPlatform> platforms;
	/// Number of platforms.
	int numPlatforms = 0;
	/// The passengers of the run.
	std::list<Passenger> passengers;
	/// Number of passengers.
	int numPassengers = 0;
	/// Looks passengers up by ID. An index describes its list only while the list keeps its elements, so reset()
	/// clears it with the list.
	PassengerIndex passengerIndex;
	/// Looks platforms up by ID. The rule of passengerIndex applies.
	PlatformIndex platformIndex;

	/// Statistics over the trains of the input delay, the entrance delay plus the station disturbances, in seconds.
	Stations totalInputDelays;
	/// Statistics over the trains of the entrance delay that the scenario sets, in seconds.
	Stations entranceInputDelays;
	/// Statistics over the trains of the station disturbances, in seconds. Nothing sets a disturbance, so they are 0.
	Stations disturbanceInput;
	/// Delay statistics at the final stop of every train, under the station name "Final_Station".
	Stations finalStation;

	/// The trains of the scene. The operations builder creates the vector once per build with
	/// exactly the trains of the scene, and it does not grow while the trains are live.
	std::vector<Regional> trains;
	/// Number of trains.
	int numTrains = 0;

	/// The draws of the passenger sampling. The operations builder seeds it from initial_variables.randomSeed.
	NumberGenerator numberGenerator{kDefaultRandomSeed};

	/// Signalling system delay in seconds (set to x-1 to get x seconds delay, accounting for timestep). The signalling reads the
	/// positions of the trains this many whole steps back. Nothing in the program sets it, so a run keeps 0.
	double signallingDelay = 0;
	/// Time of the steps in seconds as clock() measures it, summed over the run.
	double compTimeEgtrain = 0;
	/// Written to the computing times file next to compTimeEgtrain. Nothing in the program sets it, so it stays 0.
	double compTimeRoma = 0;

	/// The infrastructure and the routes. The scene builder replaces them, and the previous ones stay in place until then.
	RunInfrastructure infra;

	/// Returns every member except the infrastructure to the state of a new run.
	void reset();
	/// Returns the infrastructure to the state of a new run.
	void resetInfrastructure();
	/// Replaces every member by that of a run that the builders have filled. The lists and vectors move, so
	/// their elements stay where they are and the pointers between the elements stay valid. The trains are
	/// pointed at this run and the indexes are built again for the lists of this run. The staged run is left
	/// without contents and is not used again.
	void adopt(SimulationRun&& staged);
};

/// The run of the process. Its address never changes.
SimulationRun& defaultRun();

/// The X position in kilometres of a point of the train at step t, as Train::trainXPosition gives it, for a train that is read and
/// not changed. The point lies wagon * train_length / (number_of_wagons + 1) metres behind the head, so wagon 0 is the head and
/// wagon number_of_wagons + 1 the tail. Returns -1 when the point is on no section of the route.
double trainXPositionOf(const Train& train, int t, int wagon = 0);

// These two read the route of the train, so they are defined here and not in RollingStock.h.
inline bool Train::obeysAuthority(const MovementAuthority& authority) const {
	return authority.type != "SignalFailure" || authority.ReversedDirection == run_->infra.routes[indexOfRoute].reversed_direction;
}

inline double Train::authorityRoutePosition(const MovementAuthority& authority, const Section& section) const {
	return run_->infra.routes[indexOfRoute].reversed_direction
		? section.GeoXBegNode - authority.AbsPosEoA + section.start_node.X * 1000
		: authority.AbsPosEoA;
}

#endif // SIMULATIONRUN_H
