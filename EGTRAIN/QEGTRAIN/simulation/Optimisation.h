#ifndef Optimisation_hpp
#define Optimisation_hpp

#include "simulation/RollingStock.h"

// Function to change the departure time of the trains in order to fit them all in one hour of timetable
void changeTrainDepartureTimesForHourlyTimetabling(SimulationRun& run, Regional* Trains, int numTrains);

#endif
