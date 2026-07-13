#include "diagrams/RunResults.h"

#include "simulation/RollingStock.h"
#include "util/Logger.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

Logger owl;

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static bool closeTo(double actual, double expected) {
	return std::fabs(actual - expected) < 1e-9;
}

static std::unique_ptr<Train> makeTrain(const std::string& id, int first, int last, double energy,
										 double regen, double substation, double substationRegen) {
	auto train = std::make_unique<Train>();
	train->trainDescription = id;
	train->earliestActiveTrajectoryIndex = first;
	train->End_Time = last;
	train->instant_spatial_position.assign(static_cast<std::size_t>(last + 1), -9999.0);
	train->instant_train_power_consumption.assign(static_cast<std::size_t>(last + 1), 100.0);
	train->instant_train_energy_consumption.assign(static_cast<std::size_t>(last + 1), 200.0);
	for (int index = first; index <= last; ++index)
		train->instant_spatial_position[static_cast<std::size_t>(index)] = index * 10.0;
	train->TotalEnergyConsumed = energy;
	train->TotalEnergyConsWithRegBrak = regen;
	train->TotalEnergySubstationRequest = substation;
	train->TotalEnergySubstRequestWithRegBrak = substationRegen;
	return train;
}

int main() {
	bool ok = true;
	auto delayed = makeTrain("delayed", 3, 7, 10.0, 20.0, 30.0, 40.0);
	const auto delayedResults = buildRunResults(delayed.get(), 1, 0.5);
	ok &= expect(delayedResults.trains.size() == 1, "one train result");
	ok &= expect(delayedResults.trains[0].startSeconds.available &&
					 closeTo(delayedResults.trains[0].startSeconds.value, 1.5),
					 "delayed active start uses first valid sample");
	ok &= expect(delayedResults.trains[0].endSeconds.available &&
					 closeTo(delayedResults.trains[0].endSeconds.value, 3.5),
					 "end time uses last valid sample");
	ok &= expect(delayedResults.trains[0].travelSeconds.available &&
					 closeTo(delayedResults.trains[0].travelSeconds.value, 2.0),
					 "travel time spans active bounds");

	auto gap = makeTrain("gap", 1, 5, 11.0, 21.0, 31.0, 41.0);
	gap->instant_spatial_position[3] = -9999.0;
	const auto gapResults = buildRunResults(gap.get(), 1, 2.0);
	ok &= expect(gapResults.trains[0].startSeconds.available &&
					 closeTo(gapResults.trains[0].startSeconds.value, 2.0),
					 "internal gap keeps first valid start");
	ok &= expect(gapResults.trains[0].endSeconds.available &&
					 closeTo(gapResults.trains[0].endSeconds.value, 10.0),
					 "internal gap keeps final valid end");
	ok &= expect(gapResults.trains[0].travelSeconds.available &&
					 closeTo(gapResults.trains[0].travelSeconds.value, 8.0),
					 "internal gap keeps overall travel bounds");

	auto missingTrajectory = makeTrain("missing", 0, 2, 1.0, 2.0, 3.0, 4.0);
	missingTrajectory->earliestActiveTrajectoryIndex = -1;
	const auto missingResults = buildRunResults(missingTrajectory.get(), 1, 1.0);
	ok &= expect(!missingResults.trains[0].startSeconds.available &&
					 !missingResults.trains[0].endSeconds.available &&
					 !missingResults.trains[0].travelSeconds.available &&
					 !missingResults.trains[0].energyConsumedKWh.available,
					 "missing trajectory is unavailable");

	auto shortPower = makeTrain("short-power", 1, 4, 1.0, 2.0, 3.0, 4.0);
	shortPower->instant_train_power_consumption.resize(2);
	const auto shortPowerResults = buildRunResults(shortPower.get(), 1, 1.0);
	ok &= expect(!shortPowerResults.trains[0].energyConsumedKWh.available &&
					 !shortPowerResults.trains[0].energyWithRegenKWh.available &&
					 !shortPowerResults.trains[0].substationKWh.available &&
					 !shortPowerResults.trains[0].substationWithRegenKWh.available,
					 "short power series is unavailable");

	auto shortEnergy = makeTrain("short-energy", 1, 4, 1.0, 2.0, 3.0, 4.0);
	shortEnergy->instant_train_energy_consumption.resize(2);
	const auto shortEnergyResults = buildRunResults(shortEnergy.get(), 1, 1.0);
	ok &= expect(!shortEnergyResults.trains[0].energyConsumedKWh.available &&
					 !shortEnergyResults.trains[0].energyWithRegenKWh.available &&
					 !shortEnergyResults.trains[0].substationKWh.available &&
					 !shortEnergyResults.trains[0].substationWithRegenKWh.available,
					 "short energy series is unavailable");

	auto allFields = makeTrain("fields", 0, 1, 1.0, 2.0, 3.0, 4.0);
	const auto allResults = buildRunResults(allFields.get(), 1, 1.0);
	const auto& row = allResults.trains[0];
	ok &= expect(row.energyConsumedKWh.available && closeTo(row.energyConsumedKWh.value, 0.27778),
					 "energy consumed converts MJ to kWh");
	ok &= expect(row.energyWithRegenKWh.available && closeTo(row.energyWithRegenKWh.value, 0.55556),
					 "regenerative energy converts MJ to kWh");
	ok &= expect(row.substationKWh.available && closeTo(row.substationKWh.value, 0.83334),
					 "substation energy converts MJ to kWh");
	ok &= expect(row.substationWithRegenKWh.available && closeTo(row.substationWithRegenKWh.value, 1.11112),
					 "regenerative substation energy converts MJ to kWh");

	auto second = makeTrain("second", 0, 1, 5.0, 6.0, 7.0, 8.0);
	auto trains = std::make_unique<Train[]>(2);
	trains[0] = *allFields;
	trains[1] = *second;
	const auto totalResults = buildRunResults(trains.get(), 2, 1.0);
	ok &= expect(totalResults.trains.size() + 1 == 3, "results table has one row per train plus totals");
	ok &= expect(totalResults.energyConsumedKWh.available &&
					 closeTo(totalResults.energyConsumedKWh.value, 1.66668),
					 "network energy total sums available rows");
	auto incomplete = std::make_unique<Train>(*second);
	incomplete->instant_train_power_consumption.resize(1);
	trains[0] = *allFields;
	trains[1] = *incomplete;
	const auto incompleteTotals = buildRunResults(trains.get(), 2, 1.0);
	ok &= expect(!incompleteTotals.energyConsumedKWh.available &&
					 !incompleteTotals.energyWithRegenKWh.available &&
					 !incompleteTotals.substationKWh.available &&
					 !incompleteTotals.substationWithRegenKWh.available,
					 "network totals are unavailable for incomplete rows");

	auto terminalPower = makeTrain("terminal-power", 1, 3, 10.0, 20.0, 30.0, 40.0);
	terminalPower->departure_time = 1;
	terminalPower->instant_train_power_consumption[3] = std::numeric_limits<double>::quiet_NaN();
	terminalPower->TotalEnergyConsumptionWithAndWithoutRegBraking(0.8, 0.7);
	ok &= expect(std::isfinite(terminalPower->TotalEnergyConsWithRegBrak) &&
					 std::isfinite(terminalPower->TotalEnergySubstRequestWithRegBrak),
					 "terminal nonfinite power does not poison regenerative totals");

	if (!ok)
		return 1;
	std::cout << "all RunResults tests passed\n";
	return 0;
}
