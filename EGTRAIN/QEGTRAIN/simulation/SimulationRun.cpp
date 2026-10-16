#include "simulation/SimulationRun.h"
#include "simulation/RollingStock.h"
#include "simulation/Simulation.h"

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
