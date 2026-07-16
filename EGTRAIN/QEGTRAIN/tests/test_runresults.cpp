#include "diagrams/RunResults.h"

#include "simulation/RollingStock.h"
#include "simulation/Simulation.h"
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

static std::unique_ptr<Train> makeTimetableTrain(const std::string& id,
																					const std::vector<std::string>& stations) {
	auto train = std::make_unique<Train>();
	train->trainDescription = id;
	train->numStations = static_cast<int>(stations.size());
	train->Stations = new Node[stations.size()];
	for (std::size_t index = 0; index < stations.size(); ++index) {
		train->Stations[index].stationName = stations[index];
		train->StationArrivalNames[index] = stations[index];
	}
	return train;
}

static std::unique_ptr<Regional> makeRegionalTimetableTrain(const std::string& id,
																							const std::vector<std::string>& stations) {
	auto train = std::make_unique<Regional>();
	train->trainDescription = id;
	train->numStations = static_cast<int>(stations.size());
	train->Stations = new Node[stations.size()];
	for (std::size_t index = 0; index < stations.size(); ++index) {
		train->Stations[index].stationName = stations[index];
		train->StationArrivalNames[index] = stations[index];
	}
	return train;
}

static TrainEvent makeTimetableEvent(const std::string& station, double arrival, double departure) {
	TrainEvent event;
	event.SuccessorID = station;
	event.Time = arrival;
	event.Time2 = departure;
	return event;
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
	{
		auto train = makeTimetableTrain("timetable", {"Central"});
		train->ScheduledArrivals[0] = 100.0;
		train->ScheduledDepartures[0] = 130.0;
		train->TimetablePoints.push_back(makeTimetableEvent("Central", 112.0, 145.0));
		const std::vector<const Train*> trains{train.get()};
		const auto rows = buildTimetableResults(trains);
		ok &= expect(rows.size() == 1, "one timetable station row");
		ok &= expect(rows[0].callIndex == 1 && rows[0].stationId == "Central",
					 "timetable row keeps station occurrence identity");
		ok &= expect(rows[0].plannedArrivalSeconds.available &&
					 closeTo(rows[0].plannedArrivalSeconds.value, 100.0),
					 "planned arrival is preserved");
		ok &= expect(rows[0].plannedDepartureSeconds.available &&
					 closeTo(rows[0].plannedDepartureSeconds.value, 130.0),
					 "planned departure is preserved");
		ok &= expect(rows[0].simulatedArrivalSeconds.available &&
					 closeTo(rows[0].simulatedArrivalSeconds.value, 112.0),
					 "simulated arrival uses TrainEvent::Time");
		ok &= expect(rows[0].simulatedDepartureSeconds.available &&
					 closeTo(rows[0].simulatedDepartureSeconds.value, 145.0),
					 "simulated departure uses TrainEvent::Time2");
		ok &= expect(rows[0].arrivalDelaySeconds.available &&
					 closeTo(rows[0].arrivalDelaySeconds.value, 12.0),
					 "arrival delay is simulated minus planned");
		ok &= expect(rows[0].departureDelaySeconds.available &&
					 closeTo(rows[0].departureDelaySeconds.value, 15.0),
					 "departure delay is simulated minus planned");
	}

	{
		auto train = makeTimetableTrain("repeated", {"Central", "Central"});
		train->ScheduledArrivals[0] = 10.0;
		train->ScheduledArrivals[1] = 20.0;
		train->ScheduledDepartures[0] = 15.0;
		train->ScheduledDepartures[1] = 25.0;
		train->TimetablePoints.push_back(makeTimetableEvent("Central", 11.0, 16.0));
		train->TimetablePoints.push_back(makeTimetableEvent("Central", 22.0, 27.0));
		const std::vector<const Train*> trains{train.get()};
		const auto rows = buildTimetableResults(trains);
		ok &= expect(rows.size() == 2 && rows[0].callIndex == 1 && rows[1].callIndex == 2,
					 "repeated station calls remain ordered rows");
		ok &= expect(rows[0].simulatedArrivalSeconds.available &&
					 closeTo(rows[0].simulatedArrivalSeconds.value, 11.0) &&
					 rows[1].simulatedArrivalSeconds.available &&
					 closeTo(rows[1].simulatedArrivalSeconds.value, 22.0),
					 "repeated station arrivals match ordered events");
		ok &= expect(rows[0].simulatedDepartureSeconds.available &&
					 closeTo(rows[0].simulatedDepartureSeconds.value, 16.0) &&
					 rows[1].simulatedDepartureSeconds.available &&
					 closeTo(rows[1].simulatedDepartureSeconds.value, 27.0),
					 "repeated station departures match ordered events");
	}

	{
		auto train = makeTimetableTrain("journey-order", {"A", "B", "A"});
		train->TimetablePoints.push_back(makeTimetableEvent("A", 11.0, 16.0));
		train->TimetablePoints.push_back(makeTimetableEvent("B", 22.0, 27.0));
		train->TimetablePoints.push_back(makeTimetableEvent("A", 33.0, 38.0));
		const std::vector<const Train*> trains{train.get()};
		const auto rows = buildTimetableResults(trains);
		ok &= expect(rows.size() == 3 && rows[0].journeyIndex == 1 && rows[1].journeyIndex == 2 &&
					 rows[2].journeyIndex == 3 && rows[0].callIndex == 1 && rows[1].callIndex == 1 &&
					 rows[2].callIndex == 2,
					"journey order stays distinct from station occurrence");
	}

	{
		auto train = makeTimetableTrain("missing", {"ArrivalOnly", "DepartureOnly"});
		train->ScheduledArrivals[0] = 0.0;
		train->ScheduledDepartures[0] = -1.0;
		train->ScheduledArrivals[1] = -1.0;
		train->ScheduledDepartures[1] = 30.0;
		TrainEvent first = makeTimetableEvent("ArrivalOnly", -1.0, 5.0);
		TrainEvent second = makeTimetableEvent("DepartureOnly", 0.0, -1.0);
		train->TimetablePoints.push_back(first);
		train->TimetablePoints.push_back(second);
		const std::vector<const Train*> trains{train.get()};
		const auto rows = buildTimetableResults(trains);
		ok &= expect(rows.size() == 2, "missing events remain station rows");
		ok &= expect(rows[0].plannedArrivalSeconds.available &&
					 closeTo(rows[0].plannedArrivalSeconds.value, 0.0),
					 "valid planned timestamp zero remains available");
		ok &= expect(!rows[0].simulatedArrivalSeconds.available &&
					 rows[0].simulatedDepartureSeconds.available &&
					 closeTo(rows[0].simulatedDepartureSeconds.value, 5.0) &&
					 !rows[0].arrivalDelaySeconds.available &&
					 !rows[0].departureDelaySeconds.available,
					 "missing arrival stays independent from departure");
		ok &= expect(!rows[1].plannedArrivalSeconds.available &&
					 rows[1].plannedDepartureSeconds.available &&
					 rows[1].simulatedArrivalSeconds.available &&
					 closeTo(rows[1].simulatedArrivalSeconds.value, 0.0) &&
					 !rows[1].simulatedDepartureSeconds.available &&
					 !rows[1].arrivalDelaySeconds.available &&
					 !rows[1].departureDelaySeconds.available,
					 "missing departure stays independently unavailable");
	}

	{
		auto train = makeTimetableTrain("early", {"Central"});
		train->ScheduledArrivals[0] = 100.0;
		train->ScheduledDepartures[0] = 200.0;
		train->TimetablePoints.push_back(makeTimetableEvent("Central", 90.0, 180.0));
		const std::vector<const Train*> trains{train.get()};
		const auto rows = buildTimetableResults(trains);
		ok &= expect(rows.size() == 1 && rows[0].arrivalDelaySeconds.available &&
					 closeTo(rows[0].arrivalDelaySeconds.value, -10.0) &&
					 rows[0].departureDelaySeconds.available &&
					 closeTo(rows[0].departureDelaySeconds.value, -20.0),
					 "early arrival and departure preserve negative delays");
	}

	{
		std::vector<std::string> stations;
		for (int index = 0; index < Train::kMaxTimetableStations; ++index)
			stations.push_back("S" + std::to_string(index));
		auto train = makeTimetableTrain("overflow", stations);
		train->numStations = Train::kMaxTimetableStations + 5;
		const std::vector<const Train*> trains{train.get()};
		const auto rows = buildTimetableResults(trains);
		ok &= expect(static_cast<int>(rows.size()) == Train::kMaxTimetableStations,
					 "over-cap train yields exactly kMaxTimetableStations rows");
	}

	auto delayed = makeTrain("delayed", 3, 7, 10.0, 20.0, 30.0, 40.0);
	const std::vector<const Train*> delayedTrains{delayed.get()};
	const auto delayedResults = buildRunResults(delayedTrains, 0.5);
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
	const std::vector<const Train*> gapTrains{gap.get()};
	const auto gapResults = buildRunResults(gapTrains, 2.0);
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
	const std::vector<const Train*> missingTrains{missingTrajectory.get()};
	const auto missingResults = buildRunResults(missingTrains, 1.0);
	ok &= expect(!missingResults.trains[0].startSeconds.available &&
					 !missingResults.trains[0].endSeconds.available &&
					 !missingResults.trains[0].travelSeconds.available &&
					 !missingResults.trains[0].energyConsumedKWh.available,
					 "missing trajectory is unavailable");

	auto shortPower = makeTrain("short-power", 1, 4, 1.0, 2.0, 3.0, 4.0);
	shortPower->instant_train_power_consumption.resize(2);
	const std::vector<const Train*> shortPowerTrains{shortPower.get()};
	const auto shortPowerResults = buildRunResults(shortPowerTrains, 1.0);
	ok &= expect(!shortPowerResults.trains[0].energyConsumedKWh.available &&
					 !shortPowerResults.trains[0].energyWithRegenKWh.available &&
					 !shortPowerResults.trains[0].substationKWh.available &&
					 !shortPowerResults.trains[0].substationWithRegenKWh.available,
					 "short power series is unavailable");

	auto shortEnergy = makeTrain("short-energy", 1, 4, 1.0, 2.0, 3.0, 4.0);
	shortEnergy->instant_train_energy_consumption.resize(2);
	const std::vector<const Train*> shortEnergyTrains{shortEnergy.get()};
	const auto shortEnergyResults = buildRunResults(shortEnergyTrains, 1.0);
	ok &= expect(!shortEnergyResults.trains[0].energyConsumedKWh.available &&
					 !shortEnergyResults.trains[0].energyWithRegenKWh.available &&
					 !shortEnergyResults.trains[0].substationKWh.available &&
					 !shortEnergyResults.trains[0].substationWithRegenKWh.available,
					 "short energy series is unavailable");

	auto allFields = makeTrain("fields", 0, 1, 1.0, 2.0, 3.0, 4.0);
	const std::vector<const Train*> allTrains{allFields.get()};
	const auto allResults = buildRunResults(allTrains, 1.0);
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
	const std::vector<const Train*> totalTrains{&trains[0], &trains[1]};
	const auto totalResults = buildRunResults(totalTrains, 1.0);
	ok &= expect(totalResults.trains.size() + 1 == 3, "results table has one row per train plus totals");
	ok &= expect(totalResults.energyConsumedKWh.available &&
					 closeTo(totalResults.energyConsumedKWh.value, 1.66668),
					 "network energy total sums available rows");
	auto incomplete = std::make_unique<Train>(*second);
	incomplete->instant_train_power_consumption.resize(1);
	trains[0] = *allFields;
	trains[1] = *incomplete;
	const std::vector<const Train*> incompleteTrainPointers{&trains[0], &trains[1]};
	const auto incompleteTotals = buildRunResults(incompleteTrainPointers, 1.0);
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
	ok &= expect(terminalPower->instant_train_power_consumption[3] == 0.0,
					"energy calculation zeroes the terminal nonfinite sample");

	{
		auto nanSample = makeTrain("sanitize-nan", 1, 3, 0.0, 0.0, 0.0, 0.0);
		nanSample->instant_train_power_consumption[3] = std::numeric_limits<double>::quiet_NaN();
		nanSample->sanitizeTerminalPowerSample();
		ok &= expect(nanSample->instant_train_power_consumption[3] == 0.0,
					 "sanitize zeroes a nonfinite sample at End_Time");
		ok &= expect(closeTo(nanSample->instant_train_power_consumption[2], 100.0),
					 "sanitize leaves samples before End_Time untouched");
	}

	{
		auto finiteSample = makeTrain("sanitize-finite", 1, 3, 0.0, 0.0, 0.0, 0.0);
		finiteSample->sanitizeTerminalPowerSample();
		ok &= expect(closeTo(finiteSample->instant_train_power_consumption[3], 100.0),
					 "sanitize keeps an in-range finite sample");
	}

	{
		auto outOfRange = makeTrain("sanitize-out-of-range", 1, 3, 0.0, 0.0, 0.0, 0.0);
		outOfRange->instant_train_power_consumption[3] = std::numeric_limits<double>::quiet_NaN();
		outOfRange->End_Time = 4;
		outOfRange->sanitizeTerminalPowerSample();
		ok &= expect(std::isnan(outOfRange->instant_train_power_consumption[3]),
					 "sanitize does not write past the series end");
		outOfRange->End_Time = -1;
		outOfRange->sanitizeTerminalPowerSample();
		ok &= expect(std::isnan(outOfRange->instant_train_power_consumption[3]),
					 "sanitize does not write for a negative End_Time");
	}

	{
		auto networkEnergy = makeTrain("network-energy", 1, 3, 0.0, 0.0, 0.0, 0.0);
		networkEnergy->departure_time = 1;
		networkEnergy->instant_train_power_consumption[3] = std::numeric_limits<double>::quiet_NaN();
		ComputeEnergyConsumptionForAllTrains(networkEnergy.get(), 1);
		ok &= expect(networkEnergy->instant_train_power_consumption[3] == 0.0,
					 "network energy pass sanitizes the terminal sample");
		ok &= expect(closeTo(networkEnergy->TotalEnergyConsumed, 220.0),
					 "network energy pass still computes totals");
	}

	{
		auto regionalFirst = makeRegionalTimetableTrain("regional-first", {"A"});
		regionalFirst->ScheduledArrivals[0] = 10.0;
		regionalFirst->TimetablePoints.push_back(makeTimetableEvent("A", 11.0, 12.0));
		regionalFirst->earliestActiveTrajectoryIndex = 0;
		regionalFirst->End_Time = 0;
		regionalFirst->instant_spatial_position = {1.0};
		auto regionalSecond = makeRegionalTimetableTrain("regional-second", {"B"});
		regionalSecond->ScheduledArrivals[0] = 20.0;
		regionalSecond->TimetablePoints.push_back(makeTimetableEvent("B", 21.0, 22.0));
		regionalSecond->earliestActiveTrajectoryIndex = 0;
		regionalSecond->End_Time = 1;
		regionalSecond->instant_spatial_position = {2.0, 3.0};
		const std::vector<const Train*> regionalTrains{regionalFirst.get(), regionalSecond.get()};
		const auto timetableRows = buildTimetableResults(regionalTrains);
		ok &= expect(timetableRows.size() == 2 && timetableRows[0].trainId == "regional-first" &&
					 timetableRows[1].trainId == "regional-second" &&
					 closeTo(timetableRows[1].simulatedArrivalSeconds.value, 21.0),
					"safe Regional collection keeps both timetable rows");
		const auto regionalRunResults = buildRunResults(regionalTrains, 1.0);
		ok &= expect(regionalRunResults.trains.size() == 2 &&
					 regionalRunResults.trains[0].trainId == "regional-first" &&
					 regionalRunResults.trains[1].trainId == "regional-second",
					"safe Regional collection keeps both run result rows");
	}

	if (!ok)
		return 1;
	std::cout << "all RunResults tests passed\n";
	return 0;
}
