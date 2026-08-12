#include "diagrams/RunResults.h"

#include "scene/SceneModel.h"
#include "simulation/RollingStock.h"
#include "simulation/Simulation.h"
#include "util/Logger.hpp"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

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

static bool writeBytes(const QString& path, const QByteArray& bytes) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

static bool readBytes(const QString& path, QByteArray& bytes) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return false;
	bytes = file.readAll();
	return file.error() == QFile::NoError;
}

static QJsonObject readJsonObject(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return QJsonDocument::fromJson(file.readAll()).object();
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
		Train train;
		train.End_Time = -1;
		train.setTrainVectorSizesFromInput(5);
		ok &= expect(train.End_Time == 4, "trajectory allocation initializes the active end bound");
	}
	{
		Train train;
		const double zeroSpeedResistancePower = train.total_train_resistances(0.0, 0.0, 0.0) * 0.0;
		ok &= expect(train.curvature_resistances(0.0) == 0.0,
					 "zero curvature has no curvature resistance");
		ok &= expect(std::isfinite(zeroSpeedResistancePower),
					 "zero-speed total-resistance power remains finite");
	}
	{
		const int savedBlocks = Blocks;
		const double savedTimestep = timestep;
		const double savedDelay = S_delay;
		const auto savedRoutes = train_route;
		const auto savedAuthorities = ETCS_MA;

		Section sections[2];
		for (int index = 0; index < 2; ++index) {
			sections[index].ID = index == 0 ? "in-route" : "out-of-route";
			sections[index].SignallingLevel = 3;
			sections[index].start_node.X = 9.636 + index;
			sections[index].end_node.X = 10.636 + index;
			sections[index].start_node.ID = index * 2;
			sections[index].end_node.ID = index * 2 + 1;
			sections[index].GeoXBegNode = sections[index].start_node.X * 1000;
			sections[index].GeoXEndNode = sections[index].end_node.X * 1000;
			sections[index].total_nodes = 2;
			sections[index].total_arcs = 1;
			sections[index].arcs_in_signalling_block_section[0].startNode = sections[index].start_node;
			sections[index].arcs_in_signalling_block_section[0].endNode = sections[index].end_node;
			sections[index].arcs_in_signalling_block_section[0].length = 1000;
		}

		auto route = std::make_unique<Route>();
		route->ID = "moving-block-test";
		route->N_Block_Sections = 1;
		route->sequence_of_block_sections[0] = sections[0];
		route->x_of_start_node = sections[0].start_node.X;
		route->x_of_end_node = sections[0].end_node.X;
		train_route.clear();
		train_route.push_back(*route);
		Blocks = 2;
		timestep = 1;
		S_delay = 0;
		ETCS_MA.clear();

		Train train;
		train.trainDescription = "moving-block-test";
		train.indexOfRoute = 0;
		train.departure_time = 0;
		train.CanEnter = true;
		train.train_length = 1000;
		train.instant_spatial_position = {10600, 10600};
		train.instant_train_speed = {10, 10};
		train.ReportPositionToRBC(1, sections, 1, 50);
		bool outOfRouteAuthority = false;
		for (const auto& authority : ETCS_MA)
			outOfRouteAuthority |= authority.BSID == sections[1].ID;
		ok &= expect(!outOfRouteAuthority,
					 "reporting uses the supplied route section bound");
		bool foundEntranceAuthority = false;
		double entrancePosition = std::numeric_limits<double>::quiet_NaN();
		for (const auto& authority : ETCS_MA) {
			if (authority.BSID == sections[0].ID && authority.type == "TrainEnd" && authority.typePart == "Tale") {
				foundEntranceAuthority = true;
				entrancePosition = authority.AbsPosEoA;
				break;
			}
		}
		ok &= expect(foundEntranceAuthority,
					 "reporting creates a TrainEnd/Tale authority for the first section entrance");
		ok &= expect(foundEntranceAuthority && std::fabs(entrancePosition - sections[0].GeoXBegNode) < 0.001,
					 "first-section entrance authority uses the geographic start coordinate");

		ETCS_MA.clear();
		S_delay = 2;
		train.ReportPositionToRBC(1, sections, 1, 50);
		ok &= expect(ETCS_MA.empty(),
					 "reporting skips delayed samples before the trajectory");

		Blocks = savedBlocks;
		timestep = savedTimestep;
		S_delay = savedDelay;
		train_route = savedRoutes;
		ETCS_MA = savedAuthorities;
	}
	{
		auto train = makeTimetableTrain("timetable", {"Central"});
		train->operatingCode = "R100";
		train->serviceId = "service-1";
		train->serviceOccurrence = 3;
		train->ScheduledArrivals[0] = 100.0;
		train->ScheduledDepartures[0] = 130.0;
		train->TimetablePoints.push_back(makeTimetableEvent("Central", 112.0, 145.0));
		const std::vector<const Train*> trains{train.get()};
		const auto rows = buildTimetableResults(trains);
		ok &= expect(rows.size() == 1, "one timetable station row");
		ok &= expect(rows[0].callIndex == 1 && rows[0].stationId == "Central",
					 "timetable row keeps station occurrence identity");
		ok &= expect(rows[0].operatingCode == "R100" && rows[0].serviceId == "service-1"
					 && rows[0].occurrence == 3,
					 "timetable row carries service provenance");
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

	ok &= expect(Train::clampStationCount(Train::kMaxTimetableStations - 6, "under") ==
					 Train::kMaxTimetableStations - 6,
				 "clampStationCount keeps under-cap counts");
	ok &= expect(Train::clampStationCount(Train::kMaxTimetableStations, "at") ==
					 Train::kMaxTimetableStations,
				 "clampStationCount keeps at-cap counts");
	ok &= expect(Train::clampStationCount(Train::kMaxTimetableStations + 5, "over") ==
					 Train::kMaxTimetableStations,
				 "clampStationCount truncates over-cap counts");

	auto delayed = makeTrain("delayed", 3, 7, 10.0, 20.0, 30.0, 40.0);
	delayed->operatingCode = "1725";
	delayed->serviceId = "service.native";
	delayed->serviceOccurrence = 2;
	delayed->servicePerformancePercent = 75.0;
	delayed->hasConfiguredMaximumSpeed = true;
	delayed->configuredMaximumSpeedKmh = 120.0;
	delayed->compositionMaximumSpeedMs = 40.0;
	delayed->appliedMaximumSpeedMs = 25.0;
	delayed->appliedMaximumSpeedKmh = 90.0;
	const std::vector<const Train*> delayedTrains{delayed.get()};
	const auto delayedResults = buildRunResults(delayedTrains, 0.5);
	ok &= expect(delayedResults.trains.size() == 1, "one train result");
	ok &= expect(delayedResults.trains[0].directIncidentIds.empty()
				 && !delayedResults.trains[0].firstDirectIncidentTime.available
				 && !delayedResults.trains[0].firstDirectIncidentLocation.available,
				 "missing direct incident evidence stays unavailable");
	ok &= expect(delayedResults.trains[0].operatingCode == "1725"
				 && delayedResults.trains[0].serviceId == "service.native"
				 && delayedResults.trains[0].occurrence == 2
				 && closeTo(delayedResults.trains[0].performancePercent, 75.0)
				 && delayedResults.trains[0].hasConfiguredMaximumSpeed
				 && closeTo(delayedResults.trains[0].configuredMaximumSpeedKmh, 120.0)
				 && closeTo(delayedResults.trains[0].compositionMaximumSpeedMs, 40.0)
				 && closeTo(delayedResults.trains[0].appliedMaximumSpeedMs, 25.0)
				 && closeTo(delayedResults.trains[0].appliedMaximumSpeedKmh, 90.0),
				 "run result carries operating, service, performance, and speed provenance");
	ok &= expect(delayedResults.trains[0].startSeconds.available &&
					 closeTo(delayedResults.trains[0].startSeconds.value, 1.5),
					 "delayed active start uses first valid sample");
	ok &= expect(delayedResults.trains[0].endSeconds.available &&
					 closeTo(delayedResults.trains[0].endSeconds.value, 3.5),
					 "end time uses last valid sample");
	ok &= expect(delayedResults.trains[0].travelSeconds.available &&
					 closeTo(delayedResults.trains[0].travelSeconds.value, 2.0),
					 "travel time spans active bounds");
	delayed->directIncidentIds = {"incident.breakdown"};
	delayed->firstDirectIncidentTime = 2.0;
	delayed->firstDirectIncidentLocation = 30.0;
	const auto directResults = buildRunResults(delayedTrains, 0.5);
	ok &= expect(directResults.trains[0].firstDirectIncidentTime.available
				 && closeTo(directResults.trains[0].firstDirectIncidentTime.value, 2.0)
				 && directResults.trains[0].firstDirectIncidentLocation.available
				 && closeTo(directResults.trains[0].firstDirectIncidentLocation.value, 30.0),
				 "recorded direct incident evidence remains available");

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

	{
		DelayRunSnapshot baseline;
		baseline.caseRevision = "revision-7";
		baseline.scenarioId = "baseline";
		baseline.baseTimeSeconds = 8 * 3600;
		baseline.durationSeconds = 3600.0;
		baseline.timestep = 1.0;
		baseline.hasIncidents = false;
		baseline.hasEntranceDelays = false;
		DelayRunSnapshot scenario = baseline;
		scenario.scenarioId = "incident-run";
		scenario.hasIncidents = true;

		const auto addIdentity = [](RunResults& results, const std::string& service, int occurrence,
				const std::string& code, const std::vector<std::string>& incidentIds = {}) {
			TrainRunResult train;
			train.trainId = service + "-" + std::to_string(occurrence);
			train.serviceId = service;
			train.occurrence = occurrence;
			train.operatingCode = code;
			train.directIncidentIds = incidentIds;
			results.trains.push_back(train);
		};
		addIdentity(baseline.run, "service-a", 1, "A1");
		addIdentity(baseline.run, "service-a", 2, "A2");
		addIdentity(baseline.run, "service-a", 3, "A3");
		addIdentity(scenario.run, "service-a", 1, "A1", {"signal-failure-1"});
		addIdentity(scenario.run, "service-a", 2, "A2");
		addIdentity(scenario.run, "service-a", 3, "A3");
		const auto addArrival = [](std::vector<TimetableResultRow>& rows, int occurrence, double arrival) {
			TimetableResultRow row;
			row.serviceId = "service-a";
			row.occurrence = occurrence;
			row.stationId = "destination";
			row.simulatedArrivalSeconds = {true, arrival};
			rows.push_back(row);
		};
		addArrival(baseline.timetable, 1, 100.0);
		addArrival(baseline.timetable, 2, 150.0);
		addArrival(baseline.timetable, 3, 200.0);
		addArrival(scenario.timetable, 1, 112.0);
		addArrival(scenario.timetable, 2, 155.0);
		addArrival(scenario.timetable, 3, 190.0);
		scenario.run.trains[0].firstDirectIncidentTime = {true, 80.0};
		scenario.run.trains[0].firstDirectIncidentLocation = {true, 1234.0};
		scenario.run.trains[0].destinationTerminationRequested = true;
		scenario.run.trains[0].destinationTerminated = true;

		const DelayComparisonResult comparison = compareDelayRuns(baseline, scenario);
		ok &= expect(comparison.valid && comparison.rows.size() == 2
				&& comparison.rows[0].attribution == "primary"
				&& comparison.rows[1].attribution == "secondary"
				&& closeTo(comparison.totalArrivalDelay.value, 17.0),
				"delay comparison keeps primary plus following-secondary rows and exact total");
		ok &= expect(comparison.rows[0].incidentIds == std::vector<std::string>{"signal-failure-1"}
				&& comparison.rows[0].firstDirectTime.available
				&& closeTo(comparison.rows[0].firstDirectTime.value, 80.0)
				&& comparison.rows[0].destinationTerminationRequested
				&& comparison.rows[0].destinationTerminated,
				"signal-failure direct evidence and destination outcome reach comparison rows");

		DelayRunSnapshot mismatched = scenario;
		mismatched.run.trains.pop_back();
		const DelayComparisonResult mismatchResult = compareDelayRuns(baseline, mismatched);
		ok &= expect(!mismatchResult.valid, "delay comparison rejects selected occurrence identity mismatch");
		DelayRunSnapshot noEvidence = scenario;
		for (TrainRunResult& train : noEvidence.run.trains)
			train.directIncidentIds.clear();
		const DelayComparisonResult noEvidenceResult = compareDelayRuns(baseline, noEvidence);
		ok &= expect(!noEvidenceResult.valid, "delay comparison rejects incident runs without direct evidence");
		DelayRunSnapshot incompleteBaseline = baseline;
		DelayRunSnapshot incompleteScenario = scenario;
		TimetableResultRow baselineDestination;
		baselineDestination.serviceId = "service-a";
		baselineDestination.occurrence = 1;
		baselineDestination.stationId = "later-destination";
		baselineDestination.simulatedArrivalSeconds = {true, 220.0};
		incompleteBaseline.timetable.push_back(baselineDestination);
		TimetableResultRow missingScenarioDestination = baselineDestination;
		missingScenarioDestination.simulatedArrivalSeconds = {};
		incompleteScenario.timetable.push_back(missingScenarioDestination);
		const DelayComparisonResult incompleteResult = compareDelayRuns(incompleteBaseline, incompleteScenario);
		ok &= expect(!incompleteResult.valid,
				"delay comparison rejects an unavailable final destination arrival");
	}

	{
		QTemporaryDir temp;
		ok &= expect(temp.isValid(), "temporary directory for provenance tests is available");
		const QString sceneDir = temp.filePath("scene");
		QDir().mkpath(sceneDir);
		const std::vector<QString> sceneFiles = {
			"scene.json", "infrastructure.json", "stations.json", "signalling.json",
			"rolling_stock.json", "services.json", "scenarios.json", "incidents.json", "passengers.json"};
		for (const QString& file : sceneFiles)
			ok &= expect(writeBytes(QDir(sceneDir).filePath(file), ("{" + file + "}").toUtf8()),
				"accepted scene input is written");
		const std::string directoryHash = hashSceneDirectory(sceneDir.toStdString());
		ok &= expect(!directoryHash.empty() && directoryHash == hashSceneDirectory(sceneDir.toStdString()),
				"directory hash is deterministic");
		const SceneInputSnapshot directorySnapshot = readSceneDirectorySnapshot(sceneDir.toStdString());
		ok &= expect(directorySnapshot.reason.empty()
				&& hashSceneInputSnapshot(directorySnapshot.bytes) == directoryHash,
				"directory snapshot hash uses the shared exact-byte framing");
		const QString scenariosPath = QDir(sceneDir).filePath("scenarios.json");
		const QString incidentsPath = QDir(sceneDir).filePath("incidents.json");
		ok &= expect(QFile::remove(scenariosPath) && QFile::remove(incidentsPath)
				&& !hashSceneDirectory(sceneDir.toStdString()).empty(),
				"a compatible scene without scenario files remains reproducible");
		ok &= expect(writeBytes(scenariosPath, "{scenarios.json}")
				&& writeBytes(incidentsPath, "{incidents.json}"),
				"scenario compatibility fixture is restored");
		const QString passengerPath = QDir(sceneDir).filePath("passengers.json");
		ok &= expect(QFile::remove(passengerPath) && QDir().mkpath(passengerPath)
				&& hashSceneDirectory(sceneDir.toStdString()).empty(),
				"an unreadable optional canonical input cannot be ignored");
		ok &= expect(QDir(passengerPath).removeRecursively()
				&& writeBytes(passengerPath, "{passengers.json}"),
				"optional-input failure fixture is restored");
		ok &= expect(writeBytes(QDir(sceneDir).filePath("services.json"), "{services changed}"),
				"directory input can be changed");
		ok &= expect(directoryHash != hashSceneDirectory(sceneDir.toStdString()),
				"directory hash changes when an accepted file changes");
		const RunInputProvenance changedDirectory = captureSavedInput(
			sceneDir.toStdString(), "directory", false, directoryHash);
		ok &= expect(!changedDirectory.reproducible
				&& changedDirectory.sha256 == directoryHash
				&& changedDirectory.reason.find("changed since") != std::string::npos,
				"externally changed directory is tied to the loaded hash and marked non-reproducible");

		const QString bundlePath = temp.filePath("case.egscene");
		ok &= expect(writeBytes(bundlePath, "bundle bytes\n"), "bundle input is written");
		const std::string bundleHash = hashSceneBundle(bundlePath.toStdString());
		ok &= expect(!bundleHash.empty() && bundleHash == hashSceneBundle(bundlePath.toStdString()),
				"bundle hash is deterministic");
		QFile bundleFile(bundlePath);
		ok &= expect(bundleFile.open(QIODevice::ReadOnly)
				&& hashSceneInputSnapshot(bundleFile.readAll().toStdString()) == bundleHash,
				"bundle snapshot hash uses the exact archive bytes");
		ok &= expect(writeBytes(bundlePath, "bundle bytes changed\n"), "bundle input can be changed");
		ok &= expect(bundleHash != hashSceneBundle(bundlePath.toStdString()),
				"bundle hash changes when exact bytes change");
		const RunInputProvenance changedBundle = captureSavedInput(
			bundlePath.toStdString(), "bundle", false, bundleHash);
		ok &= expect(!changedBundle.reproducible
				&& changedBundle.sha256 == bundleHash
				&& changedBundle.reason.find("changed since") != std::string::npos,
				"externally changed bundle is tied to the loaded hash and marked non-reproducible");

		const std::string currentDirectoryHash = hashSceneDirectory(sceneDir.toStdString());
		const RunInputProvenance clean = captureSavedInput(
			sceneDir.toStdString(), "directory", false, currentDirectoryHash);
		ok &= expect(clean.reproducible && clean.status == "reproducible"
				&& clean.path == QFileInfo(sceneDir).absoluteFilePath().toStdString()
				&& !clean.sha256.empty(), "clean saved directory is reproducible with an absolute path");
		const RunInputProvenance unverified = captureSavedInput(
			sceneDir.toStdString(), "directory", false, {});
		ok &= expect(!unverified.reproducible
				&& unverified.reason.find("could not be verified") != std::string::npos,
				"saved input without a load/save hash is not labeled reproducible");
		const RunInputProvenance dirty = captureSavedInput(
			sceneDir.toStdString(), "directory", true, currentDirectoryHash);
		ok &= expect(!dirty.reproducible && dirty.dirty && dirty.status == "non-reproducible"
				&& dirty.reason.find("dirty") != std::string::npos,
				"dirty saved input is marked non-reproducible with a reason");
		const RunInputProvenance unsaved = captureSavedInput({}, "directory", false, {});
		ok &= expect(unsaved.kind == "unsaved" && !unsaved.reproducible
				&& unsaved.reason.find("unsaved") != std::string::npos,
				"unsaved input is marked non-reproducible with a reason");
		const RunInputProvenance unreadable = captureSavedInput(temp.filePath("missing").toStdString(),
			"directory", false, {});
		ok &= expect(!unreadable.reproducible
				&& unreadable.reason.find("missing or unreadable") != std::string::npos,
				"unreadable saved input is marked non-reproducible with a reason");

		RunProvenance provenance;
		provenance.caseName = "Case \"quoted\" \\ path";
		provenance.sceneSchemaVersion = 1;
		provenance.input = clean;
		provenance.appliedScenario = "scenario/\\quoted";
		provenance.baseTimeSeconds = 3600.0;
		provenance.durationSeconds = 7200.0;
		provenance.timestepSeconds = 0.5;
		provenance.bufferSeconds = 7.0;
		provenance.recoveryPercent = 12.5;
		provenance.paxMode = 1;
		provenance.tsmMode = 2;
		provenance.routeChoiceMode = 3;
		provenance.selectedOccurrences = {{"service-A", 2, "A\\\"2"}, {"service-B", 1, "B1"}};
		const QString artifactPath = temp.filePath("result.csv");
		ok &= expect(writeRunArtifactWithProvenance(
				artifactPath.toStdString(), "csv", "header\nrow\n", provenance),
				"normal artifact and provenance sidecar are written together");
		QByteArray artifactBytes;
		ok &= expect(readBytes(artifactPath, artifactBytes) && artifactBytes == "header\nrow\n",
				"paired export preserves exact artifact bytes");
		const QJsonObject sidecar = readJsonObject(artifactPath + ".provenance.json");
		const QJsonObject run = sidecar.value("run").toObject();
		const QJsonObject input = run.value("input").toObject();
		const QJsonArray occurrences = run.value("selected_occurrences").toArray();
		ok &= expect(sidecar.value("schema_version").toInt() == 1
				&& sidecar.value("artifact").toObject().value("kind").toString() == "csv"
				&& sidecar.value("artifact").toObject().value("file_name").toString() == "result.csv",
				"normal sidecar has schema and artifact fields");
		ok &= expect(run.value("case_name").toString() == QString::fromStdString(provenance.caseName)
				&& run.value("applied_scenario").toString() == QString::fromStdString(provenance.appliedScenario)
				&& input.value("path").toString() == QString::fromStdString(clean.path)
				&& input.value("sha256").toString() == QString::fromStdString(clean.sha256),
				"sidecar preserves escaped JSON fields and saved-input fields");
		ok &= expect(occurrences.size() == 2
				&& occurrences.at(0).toObject().value("service_id").toString() == "service-A"
				&& occurrences.at(0).toObject().value("occurrence").toInt() == 2
				&& occurrences.at(0).toObject().value("operating_code").toString() == "A\\\"2",
				"sidecar preserves exact selected occurrence identities");
		const QString failedArtifact = temp.filePath("failed.csv");
		ok &= expect(QDir().mkpath(failedArtifact + ".provenance.json"),
				"sidecar failure path is occupied by a directory");
		ok &= expect(!writeRunArtifactWithProvenance(
				failedArtifact.toStdString(), "csv", "header\n", provenance)
				&& !QFileInfo::exists(failedArtifact),
				"sidecar failure does not publish the artifact");

		RunProvenance scenario = provenance;
		scenario.appliedScenario = "incident";
		const QString delayArtifact = temp.filePath("delay.csv");
		ok &= expect(writeDelayArtifactWithProvenance(
				delayArtifact.toStdString(), "csv", "header\n", provenance, scenario),
				"delay artifact and sidecar are written with two runs");
		const QJsonObject delaySidecar = readJsonObject(delayArtifact + ".provenance.json");
		ok &= expect(delaySidecar.value("baseline_run").toObject().value("applied_scenario").toString() == "scenario/\\quoted"
				&& delaySidecar.value("scenario_run").toObject().value("applied_scenario").toString() == "incident",
				"delay sidecar keeps baseline and scenario provenance distinct");
		const QString failedDelayArtifact = temp.filePath("failed-delay.csv");
		ok &= expect(QDir().mkpath(failedDelayArtifact + ".provenance.json"),
				"delay sidecar failure path is occupied by a directory");
		ok &= expect(!writeDelayArtifactWithProvenance(
				failedDelayArtifact.toStdString(), "csv", "header\n", provenance, scenario)
				&& !QFileInfo::exists(failedDelayArtifact),
				"delay sidecar failure does not publish the artifact");
	}

	if (!ok)
		return 1;
	std::cout << "all RunResults tests passed\n";
	return 0;
}
