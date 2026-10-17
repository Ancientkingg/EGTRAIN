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

// The infrastructure and the routes that the scene builder makes: the tracks, connections and stations of the network, its block
// sections and detection sections, the single-track data and the routes of the trains.
//
// The arrays of tracks, connections and stations have a fixed number of elements that nothing resizes, so the elements of a built
// infrastructure and the buffers that they hold stay where they are until the infrastructure is reset or replaced. A build that fails
// puts the earlier infrastructure back as it was. The canvas keeps Node* and Arc* into the tracks and Connections* into the
// connections, a section keeps TDS* into the detection sections, and the station boundaries keep Section* into the sections. These
// pointers are valid until the next reset or build, so the window removes its items first.
struct RunInfrastructure {
	// The first numTrackLines, numConnections and numStations elements are in use.
	std::vector<BlockSet> blockSets = std::vector<BlockSet>(kMaxTrackLines);
	std::vector<Connections> connections = std::vector<Connections>(kMaxConnections);
	std::vector<Stations> stations = std::vector<Stations>(kMaxStations);
	int numTrackLines = 0;
	int numConnections = 0;
	int numStations = 0;

	// The block sections of the network, sized once per scene by the builder, and their number.
	std::vector<Section> sections;
	int numBlocks = 0;
	std::list<TDS> detectionSections;
	// The recorded events of the infrastructure elements.
	std::list<InfraEvent> infraEvents;
	// Set when createBlockConn needs more sections than the builder planned.
	bool derivedSectionsExceedPlan = false;

	// The single-track limits (first plain block ID, last plain block ID, protected block ID at the first end, protected block ID at
	// the last end), the direction in which a train holds each of them and the pending reservations.
	std::vector<std::tuple<std::string, std::string, std::string, std::string>> singleTrackLimits;
	std::vector<int> singleTrackHeld;
	std::vector<std::vector<SingleTrackReservation>> singleTrackReservations;
	// The zones of the limits on the routes, derived on first use by singleTrackZone. The index is limit * route count + route.
	std::vector<SingleTrackZone> singleTrackZones;
	std::size_t singleTrackZoneLimits = 0, singleTrackZoneRoutes = 0;
	// 1 for a route on which any limit has a zone.
	std::vector<char> singleTrackRouteZone;
	bool singleTrackZonesDerived = false;
	// The station entrance sections.
	std::vector<StationBoundarySection> stationBoundaries;

	std::vector<Route> routes;
	int numRoutes = 0;

	// Returns every member to the state of a new object.
	void reset();
};

// The state that one simulation run builds up and that the next run must not see.
//
// The routines of the simulation library take the run that they work on as a parameter, and the
// member functions of a train reach it through Train::run_, which the operations builder sets.
//
// The process has one run, defaultRun(). It has a fixed address, so reset() clears every member in
// place and the references that the headers declare under the old global names stay valid
// (BlocksOccupied, ETCS_MA and so on, defined in SimulationRun.cpp). The application and the tests
// use those names, and they all refer to defaultRun(). A member that is added here must be cleared
// in reset(), or in resetInfrastructure() when it belongs to the infrastructure.
struct SimulationRun {
	// The settings of the run: a copy of initial_variables that DispatchController::prepareScene takes once the scene is built. It holds
	// the output folder and the buffer time and running time recovery of the run. A change to initial_variables after that does not
	// reach the run.
	InitialParameters settings{0};

	// Block sections that trains occupy and the sections connected to them. The signalling routines fill
	// the lists during a step and the step clears them.
	std::list<std::string> blocksOccupied;
	std::list<std::string> blocksConnected;
	// Movement authorities of the current step.
	std::list<MovementAuthority> etcsMa;

	// Incidents of the selected scenario.
	std::vector<SimulationIncident> incidents;

	// Virtual coupling notices of the run: timestep, train and text, one entry each.
	std::vector<int> vcMsgTimestep;
	std::vector<std::string> vcMsgTrain;
	std::vector<std::string> vcMsgText;

	// The platforms of the network and the passengers of the run, each with the number of its elements.
	std::list<StationPlatform> platforms;
	int numPlatforms = 0;
	std::list<Passenger> passengers;
	int numPassengers = 0;
	// The indexes of the two lists. An index describes its list only while the list keeps its elements, so reset()
	// clears them with the lists.
	PassengerIndex passengerIndex;
	PlatformIndex platformIndex;

	// Delay statistics: the entrance delays and disturbances that the scenario sets, and the delays at the final stop of
	// every train.
	Stations totalInputDelays;
	Stations entranceInputDelays;
	Stations disturbanceInput;
	Stations finalStation;

	// The trains of the scene and their number. The operations builder creates the vector once per build with
	// exactly the trains of the scene, and it does not grow while the trains are live.
	std::vector<Regional> trains;
	int numTrains = 0;

	// The draws of the passenger sampling. The operations builder seeds it from the settings of the run.
	NumberGenerator numberGenerator{kDefaultRandomSeed};

	// Signalling system delay in seconds.
	double signallingDelay = 0;
	// Computing time of the steps, summed over the run.
	double compTimeEgtrain = 0;
	double compTimeRoma = 0;

	// The infrastructure and the routes. The scene builder replaces them, and the previous ones stay in place until then.
	RunInfrastructure infra;

	// Returns every member except the infrastructure to the state of a new run.
	void reset();
	// Returns the infrastructure to the state of a new run.
	void resetInfrastructure();
};

// The run of the process.
SimulationRun& defaultRun();

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
