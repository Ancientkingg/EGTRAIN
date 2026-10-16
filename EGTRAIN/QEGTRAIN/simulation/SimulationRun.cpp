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

// The global names of the state that the run holds. They are references to the members of the run.
list<string>& BlocksOccupied = theRun.blocksOccupied;
list<string>& BlocksConnected = theRun.blocksConnected;
list<MovementAuthority>& ETCS_MA = theRun.etcsMa;
std::vector<SimulationIncident>& simulationIncidents = theRun.incidents;
vector<int>& VCmsgTimestep = theRun.vcMsgTimestep;
vector<string>& VCmsgTrain = theRun.vcMsgTrain;
vector<string>& VCmsgText = theRun.vcMsgText;
double& S_delay = theRun.signallingDelay;
double& Comp_Time_EGTRAIN = theRun.compTimeEgtrain;
double& Comp_Time_ROMA = theRun.compTimeRoma;
