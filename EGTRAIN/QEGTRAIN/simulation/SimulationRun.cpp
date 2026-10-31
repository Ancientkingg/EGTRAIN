#include "simulation/SimulationRun.h"
#include "simulation/RollingStock.h"
#include "simulation/Simulation.h"

using namespace std;

void RunInfrastructure::reset() {
	for (BlockSet& track : blockSets)
		track = BlockSet();
	for (Connections& connection : connections)
		connection = Connections();
	for (Stations& station : stations)
		station = Stations();
	numTrackLines = 0;
	numConnections = 0;
	numStations = 0;
	// A node list is freed with the last section that holds it
	std::vector<Section>().swap(sections);
	numBlocks = 0;
	detectionSections.clear();
	infraEvents.clear();
	derivedSectionsExceedPlan = false;
	singleTrackLimits.clear();
	singleTrackHeld.clear();
	singleTrackReservations.clear();
	singleTrackZones.clear();
	singleTrackZoneLimits = 0;
	singleTrackZoneRoutes = 0;
	singleTrackRouteZone.clear();
	singleTrackZonesDerived = false;
	stationBoundaries.clear();
	routes.clear();
	numRoutes = 0;
}

void SimulationRun::reset() {
	settings = InitialParameters(0);
	blocksOccupied.clear();
	blocksConnected.clear();
	etcsMa.clear();
	incidents.clear();
	vcMsgTimestep.clear();
	vcMsgTrain.clear();
	vcMsgText.clear();
	platforms.clear();
	numPlatforms = 0;
	passengers.clear();
	numPassengers = 0;
	passengerIndex.clear();
	platformIndex.clear();
	totalInputDelays = DelayStats();
	entranceInputDelays = DelayStats();
	disturbanceInput = DelayStats();
	finalStation = DelayStats();
	std::vector<Train>().swap(trains);
	numTrains = 0;
	numberGenerator = NumberGenerator(kDefaultRandomSeed);
	signallingDelay = 0;
	compTimeEgtrain = 0;
	compTimeRoma = 0;
}

void SimulationRun::resetInfrastructure() {
	infra.reset();
}

void SimulationRun::adopt(SimulationRun&& staged) {
	*this = std::move(staged);
	for (Train& train : trains)
		train.run_ = this;
	// An index remembers the list it was built for, and the lists are now those of this run.
	platformIndex.build(platforms);
	passengerIndex.build(passengers);
}

namespace {
SimulationRun theRun;
} // namespace

SimulationRun& defaultRun() {
	return theRun;
}
