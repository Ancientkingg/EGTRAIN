#ifndef SIMULATIONRUN_H
#define SIMULATIONRUN_H

#include "simulation/Signalling.h"

#include <list>
#include <string>
#include <vector>

// The state that one simulation run builds up and that the next run must not see.
//
// The process has one run, defaultRun(). It has a fixed address, so reset() clears every member in
// place and the references that the headers declare under the old global names stay valid
// (BlocksOccupied, ETCS_MA and so on, defined in SimulationRun.cpp). Code outside this file keeps
// using those names. A member that is added here must be cleared in reset().
struct SimulationRun {
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

	// Signalling system delay in seconds.
	double signallingDelay = 0;
	// Computing time of the steps, summed over the run.
	double compTimeEgtrain = 0;
	double compTimeRoma = 0;

	// Returns every member to the state of a new run.
	void reset();
};

// The run of the process.
SimulationRun& defaultRun();

#endif // SIMULATIONRUN_H
