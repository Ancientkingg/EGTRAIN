#ifndef Optimisation_hpp
#define Optimisation_hpp

#include "simulation/RollingStock.h"

// Defining parameters for defining timetabling: Running time recovery and buffer time
extern double recoveryTimePercentage;
extern double bufferTime;

// Function to change the departure time of the trains in order to fit them all in one hour of timetable
void changeTrainDepartureTimesForHourlyTimetabling(Train* Trains, int numTrains);

#endif
