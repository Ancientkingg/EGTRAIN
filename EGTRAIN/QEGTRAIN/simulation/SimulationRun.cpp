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
	stationBoundaries.clear();
	routes.clear();
	numRoutes = 0;
}

void SimulationRun::reset() {
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
	totalInputDelays = Stations();
	entranceInputDelays = Stations();
	disturbanceInput = Stations();
	finalStation = Stations();
	std::vector<Regional>().swap(trains);
	numTrains = 0;
	numberGenerator = NumberGenerator(kDefaultRandomSeed);
	signallingDelay = 0;
	compTimeEgtrain = 0;
	compTimeRoma = 0;
}

void SimulationRun::resetInfrastructure() {
	infra.reset();
	resetSingleTrackLocks();
}

namespace {
// It has static storage, so the references below are bound before any code runs.
SimulationRun theRun;
} // namespace

SimulationRun& defaultRun() {
	return theRun;
}

NumberGenerator& runNumberGenerator() {
	return theRun.numberGenerator;
}

void seedRunNumberGenerator(unsigned long inSeed) {
	theRun.numberGenerator = NumberGenerator(inSeed);
}

// The global names of the state that the run holds. They are references to the members of the run.
list<string>& BlocksOccupied = theRun.blocksOccupied;
list<string>& BlocksConnected = theRun.blocksConnected;
list<MovementAuthority>& ETCS_MA = theRun.etcsMa;
std::vector<SimulationIncident>& simulationIncidents = theRun.incidents;
vector<int>& VCmsgTimestep = theRun.vcMsgTimestep;
vector<string>& VCmsgTrain = theRun.vcMsgTrain;
vector<string>& VCmsgText = theRun.vcMsgText;
std::vector<Regional>& regional_train = theRun.trains;
int& numRegions = theRun.numTrains;
list<StationPlatform>& AllStationPlatforms = theRun.platforms;
int& numAllStationPlatforms = theRun.numPlatforms;
list<Passenger>& AllDailyPassengers = theRun.passengers;
int& numAllDailyPassengers = theRun.numPassengers;
PassengerIndex& AllDailyPassengerIndex = theRun.passengerIndex;
PlatformIndex& AllStationPlatformIndex = theRun.platformIndex;
Stations& TotalInputDelays = theRun.totalInputDelays;
Stations& EntranceInputDelays = theRun.entranceInputDelays;
Stations& DisturbanceInput = theRun.disturbanceInput;
Stations& Final_Station = theRun.finalStation;
double& S_delay = theRun.signallingDelay;
double& Comp_Time_EGTRAIN = theRun.compTimeEgtrain;
double& Comp_Time_ROMA = theRun.compTimeRoma;
int& numTrackLines = theRun.infra.numTrackLines;
vector<BlockSet>& blockSets = theRun.infra.blockSets;
int& numConnections = theRun.infra.numConnections;
vector<Connections>& connections = theRun.infra.connections;
int& numStations = theRun.infra.numStations;
vector<Stations>& StationArray = theRun.infra.stations;
int& Blocks = theRun.infra.numBlocks;
vector<Section>& signalling_block_sections = theRun.infra.sections;
list<TDS>& list_of_TDS = theRun.infra.detectionSections;
list<InfraEvent>& InfraElementsList = theRun.infra.infraEvents;
bool& derivedSectionsExceedPlan = theRun.infra.derivedSectionsExceedPlan;
vector<tuple<string, string, string, string>>& singleTrackLimits = theRun.infra.singleTrackLimits;
vector<int>& singleTrackHeld = theRun.infra.singleTrackHeld;
vector<vector<SingleTrackReservation>>& singleTrackReservations = theRun.infra.singleTrackReservations;
vector<StationBoundarySection>& stationBoundarySections = theRun.infra.stationBoundaries;
int& N_Routes = theRun.infra.numRoutes;
vector<Route>& train_route = theRun.infra.routes;
