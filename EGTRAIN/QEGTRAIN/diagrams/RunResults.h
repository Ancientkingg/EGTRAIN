#ifndef RUNRESULTS_H
#define RUNRESULTS_H

#include "simulation/RollingStock.h"

#include <string>
#include <vector>

struct RunResultValue {
	bool available = false;
	double value = 0.0;
};

struct TrainRunResult {
	std::string trainId;
	RunResultValue startSeconds;
	RunResultValue endSeconds;
	RunResultValue travelSeconds;
	RunResultValue energyConsumedKWh;
	RunResultValue energyWithRegenKWh;
	RunResultValue substationKWh;
	RunResultValue substationWithRegenKWh;
};

struct TimetableResultRow {
	std::string trainId;
	std::string stationId;
	int journeyIndex = 0;
	int callIndex = 0;
	RunResultValue plannedArrivalSeconds;
	RunResultValue plannedDepartureSeconds;
	RunResultValue simulatedArrivalSeconds;
	RunResultValue simulatedDepartureSeconds;
	RunResultValue arrivalDelaySeconds;
	RunResultValue departureDelaySeconds;
};

struct RunResults {
	std::vector<TrainRunResult> trains;
	RunResultValue networkStartSeconds;
	RunResultValue networkEndSeconds;
	RunResultValue networkTravelSeconds;
	RunResultValue energyConsumedKWh;
	RunResultValue energyWithRegenKWh;
	RunResultValue substationKWh;
	RunResultValue substationWithRegenKWh;
};

// Preserve the legacy MJ-to-kWh conversion used by text outputs.
constexpr double kEnergyMJToKWh = 0.27778;
constexpr double energyMJKWh(double energyMJ) {
	return energyMJ * kEnergyMJToKWh;
}

RunResults buildRunResults(const std::vector<const Train*>& trains, double timestep);
std::vector<TimetableResultRow> buildTimetableResults(const std::vector<const Train*>& trains);

#endif // RUNRESULTS_H
