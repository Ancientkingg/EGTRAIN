#include "diagrams/RunResults.h"

#include "scene/SceneModel.h"
#include "simulation/RollingStock.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationRun.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

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
	train->run_ = &defaultRun();
	train->trainDescription = id;
	train->numStations = static_cast<int>(stations.size());
	train->Stations.resize(stations.size());
	for (std::size_t index = 0; index < stations.size(); ++index) {
		train->Stations[index].stationName = stations[index];
		train->StationArrivalNames[index] = stations[index];
	}
	return train;
}

static std::unique_ptr<Regional> makeRegionalTimetableTrain(const std::string& id,
	const std::vector<std::string>& stations) {
	auto train = std::make_unique<Regional>();
	train->run_ = &defaultRun();
	train->trainDescription = id;
	train->numStations = static_cast<int>(stations.size());
	train->Stations.resize(stations.size());
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
	train->run_ = &defaultRun();
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

class TestRunResults : public QObject {
	Q_OBJECT

private slots:
	void allocatesActiveEnd();
	void keepsZeroSpeedResistanceFinite();
	void reportsPositionWithinRoute();
	void buildsTimetableRow();
	void ordersRepeatedCalls();
	void keepsJourneyOrderApartFromCalls();
	void keepsMissingTimesApart();
	void keepsNegativeDelays();
	void classifiesTimetableStatus();
	void classifiesArrivalAndDepartureApart();
	void carriesEntranceDelay();
	void capsTimetableRows();
	void copiesStopLists();
	void clampsStationCount();
	void carriesTrainProvenance();
	void keepsBoundsAcrossGap();
	void missingTrajectoryIsUnavailable();
	void shortPowerSeriesIsUnavailable();
	void shortEnergySeriesIsUnavailable();
	void convertsEnergyToKWh();
	void sumsNetworkEnergy();
	void keepsTerminalPowerOutOfTotals();
	void sanitizeZeroesNonFiniteSample();
	void sanitizeKeepsFiniteSample();
	void sanitizeStaysInSeries();
	void networkEnergyPassSanitizes();
	void collectsRegionalTrains();
	void describesIncidentEvidence();
	void describesOccurrences();
	void recordsRunProvenance();
	void exportsTrajectoryTail();
};

void TestRunResults::allocatesActiveEnd() {
	Train train;
	train.End_Time = -1;
	train.setTrainVectorSizesFromInput(5);
	// trajectory allocation initializes the active end bound
	QCOMPARE(train.End_Time, 4);
}

void TestRunResults::keepsZeroSpeedResistanceFinite() {
	Train train;
	const double zeroSpeedResistancePower = train.total_train_resistances(0.0, 0.0, 0.0) * 0.0;
	// zero curvature has no curvature resistance
	QCOMPARE_EQ(train.curvature_resistances(0.0), 0.0);
	QVERIFY2(std::isfinite(zeroSpeedResistancePower),
		"zero-speed total-resistance power remains finite");
}

void TestRunResults::reportsPositionWithinRoute() {
	const int savedBlocks = defaultRun().infra.numBlocks;
	const double savedDelay = defaultRun().signallingDelay;
	const auto savedRoutes = defaultRun().infra.routes;
	const auto savedAuthorities = defaultRun().etcsMa;
	// A failed check ends the slot, so the default run gets its state back on the way out.
	const auto restore = qScopeGuard([&]() {
		defaultRun().infra.numBlocks = savedBlocks;
		defaultRun().signallingDelay = savedDelay;
		defaultRun().infra.routes = savedRoutes;
		defaultRun().etcsMa = savedAuthorities;
	});

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
	route->sequence_of_block_sections.resize(1);
	route->sequence_of_block_sections[0] = sections[0];
	route->x_of_start_node = sections[0].start_node.X;
	route->x_of_end_node = sections[0].end_node.X;
	defaultRun().infra.routes.clear();
	defaultRun().infra.routes.push_back(*route);
	defaultRun().infra.numBlocks = 2;
	defaultRun().signallingDelay = 0;
	defaultRun().etcsMa.clear();

	Train train;
	train.run_ = &defaultRun();
	train.trainDescription = "moving-block-test";
	train.indexOfRoute = 0;
	train.departure_time = 0;
	train.CanEnter = true;
	train.train_length = 1000;
	train.instant_spatial_position = {10600, 10600};
	train.instant_train_speed = {10, 10};
	train.ReportPositionToRBC(1, sections, 1, 50);
	bool outOfRouteAuthority = false;
	for (const auto& authority : defaultRun().etcsMa)
		outOfRouteAuthority |= authority.BSID == sections[1].ID;
	QVERIFY2(!outOfRouteAuthority,
		"reporting uses the supplied route section bound");
	bool foundEntranceAuthority = false;
	double entrancePosition = std::numeric_limits<double>::quiet_NaN();
	for (const auto& authority : defaultRun().etcsMa) {
		if (authority.BSID == sections[0].ID && authority.type == "TrainEnd" && authority.typePart == "Tale") {
			foundEntranceAuthority = true;
			entrancePosition = authority.AbsPosEoA;
			break;
		}
	}
	QVERIFY2(foundEntranceAuthority,
		"reporting creates a TrainEnd/Tale authority for the first section entrance");
	// first-section entrance authority uses the geographic start coordinate
	QVERIFY(foundEntranceAuthority);
	QVERIFY(std::fabs(entrancePosition - sections[0].GeoXBegNode) < 0.001);

	defaultRun().etcsMa.clear();
	defaultRun().signallingDelay = 2;
	train.ReportPositionToRBC(1, sections, 1, 50);
	QVERIFY2(defaultRun().etcsMa.empty(),
		"reporting skips delayed samples before the trajectory");
}

void TestRunResults::buildsTimetableRow() {
	auto train = makeTimetableTrain("timetable", {"Central"});
	train->operatingCode = "R100";
	train->serviceId = "service-1";
	train->serviceOccurrence = 3;
	train->ScheduledArrivals[0] = 100.0;
	train->ScheduledDepartures[0] = 130.0;
	train->TimetablePoints.push_back(makeTimetableEvent("Central", 112.0, 145.0));
	const std::vector<const Train*> trains{train.get()};
	const auto rows = buildTimetableResults(trains);
	// one timetable station row
	QCOMPARE(rows.size(), std::size_t(1));
	// timetable row keeps station occurrence identity
	QCOMPARE(rows[0].callIndex, 1);
	QCOMPARE(rows[0].stationId, "Central");
	// timetable row carries service provenance
	QCOMPARE(rows[0].operatingCode, "R100");
	QCOMPARE(rows[0].serviceId, "service-1");
	QCOMPARE(rows[0].occurrence, 3);
	// planned arrival is preserved
	QVERIFY(rows[0].plannedArrivalSeconds.available);
	QVERIFY(closeTo(rows[0].plannedArrivalSeconds.value, 100.0));
	// planned departure is preserved
	QVERIFY(rows[0].plannedDepartureSeconds.available);
	QVERIFY(closeTo(rows[0].plannedDepartureSeconds.value, 130.0));
	// simulated arrival uses TrainEvent::Time
	QVERIFY(rows[0].simulatedArrivalSeconds.available);
	QVERIFY(closeTo(rows[0].simulatedArrivalSeconds.value, 112.0));
	// simulated departure uses TrainEvent::Time2
	QVERIFY(rows[0].simulatedDepartureSeconds.available);
	QVERIFY(closeTo(rows[0].simulatedDepartureSeconds.value, 145.0));
	// arrival delay is simulated minus planned
	QVERIFY(rows[0].arrivalDelaySeconds.available);
	QVERIFY(closeTo(rows[0].arrivalDelaySeconds.value, 12.0));
	// departure delay is simulated minus planned
	QVERIFY(rows[0].departureDelaySeconds.available);
	QVERIFY(closeTo(rows[0].departureDelaySeconds.value, 15.0));
}

void TestRunResults::ordersRepeatedCalls() {
	auto train = makeTimetableTrain("repeated", {"Central", "Central"});
	train->ScheduledArrivals[0] = 10.0;
	train->ScheduledArrivals[1] = 20.0;
	train->ScheduledDepartures[0] = 15.0;
	train->ScheduledDepartures[1] = 25.0;
	train->TimetablePoints.push_back(makeTimetableEvent("Central", 11.0, 16.0));
	train->TimetablePoints.push_back(makeTimetableEvent("Central", 22.0, 27.0));
	const std::vector<const Train*> trains{train.get()};
	const auto rows = buildTimetableResults(trains);
	// repeated station calls remain ordered rows
	QCOMPARE(rows.size(), std::size_t(2));
	QCOMPARE(rows[0].callIndex, 1);
	QCOMPARE(rows[1].callIndex, 2);
	// repeated station arrivals match ordered events
	QVERIFY(rows[0].simulatedArrivalSeconds.available);
	QVERIFY(closeTo(rows[0].simulatedArrivalSeconds.value, 11.0));
	QVERIFY(rows[1].simulatedArrivalSeconds.available);
	QVERIFY(closeTo(rows[1].simulatedArrivalSeconds.value, 22.0));
	// repeated station departures match ordered events
	QVERIFY(rows[0].simulatedDepartureSeconds.available);
	QVERIFY(closeTo(rows[0].simulatedDepartureSeconds.value, 16.0));
	QVERIFY(rows[1].simulatedDepartureSeconds.available);
	QVERIFY(closeTo(rows[1].simulatedDepartureSeconds.value, 27.0));
}

void TestRunResults::keepsJourneyOrderApartFromCalls() {
	auto train = makeTimetableTrain("journey-order", {"A", "B", "A"});
	train->TimetablePoints.push_back(makeTimetableEvent("A", 11.0, 16.0));
	train->TimetablePoints.push_back(makeTimetableEvent("B", 22.0, 27.0));
	train->TimetablePoints.push_back(makeTimetableEvent("A", 33.0, 38.0));
	const std::vector<const Train*> trains{train.get()};
	const auto rows = buildTimetableResults(trains);
	// journey order stays distinct from station occurrence
	QCOMPARE(rows.size(), std::size_t(3));
	QCOMPARE(rows[0].journeyIndex, 1);
	QCOMPARE(rows[1].journeyIndex, 2);
	QCOMPARE(rows[2].journeyIndex, 3);
	QCOMPARE(rows[0].callIndex, 1);
	QCOMPARE(rows[1].callIndex, 1);
	QCOMPARE(rows[2].callIndex, 2);
}

void TestRunResults::keepsMissingTimesApart() {
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
	// missing events remain station rows
	QCOMPARE(rows.size(), std::size_t(2));
	// valid planned timestamp zero remains available
	QVERIFY(rows[0].plannedArrivalSeconds.available);
	QVERIFY(closeTo(rows[0].plannedArrivalSeconds.value, 0.0));
	// missing arrival stays independent from departure
	QVERIFY(!rows[0].simulatedArrivalSeconds.available);
	QVERIFY(rows[0].simulatedDepartureSeconds.available);
	QVERIFY(closeTo(rows[0].simulatedDepartureSeconds.value, 5.0));
	QVERIFY(!rows[0].arrivalDelaySeconds.available);
	QVERIFY(!rows[0].departureDelaySeconds.available);
	// missing departure stays independently unavailable
	QVERIFY(!rows[1].plannedArrivalSeconds.available);
	QVERIFY(rows[1].plannedDepartureSeconds.available);
	QVERIFY(rows[1].simulatedArrivalSeconds.available);
	QVERIFY(closeTo(rows[1].simulatedArrivalSeconds.value, 0.0));
	QVERIFY(!rows[1].simulatedDepartureSeconds.available);
	QVERIFY(!rows[1].arrivalDelaySeconds.available);
	QVERIFY(!rows[1].departureDelaySeconds.available);
}

void TestRunResults::keepsNegativeDelays() {
	auto train = makeTimetableTrain("early", {"Central"});
	train->ScheduledArrivals[0] = 100.0;
	train->ScheduledDepartures[0] = 200.0;
	train->TimetablePoints.push_back(makeTimetableEvent("Central", 90.0, 180.0));
	const std::vector<const Train*> trains{train.get()};
	const auto rows = buildTimetableResults(trains);
	// early arrival and departure preserve negative delays
	QCOMPARE(rows.size(), std::size_t(1));
	QVERIFY(rows[0].arrivalDelaySeconds.available);
	QVERIFY(closeTo(rows[0].arrivalDelaySeconds.value, -10.0));
	QVERIFY(rows[0].departureDelaySeconds.available);
	QVERIFY(closeTo(rows[0].departureDelaySeconds.value, -20.0));
}

void TestRunResults::classifiesTimetableStatus() {
	// Plan and simulated time present, plan only, simulated time only, neither; the second call of Central has
	// no timetable point because the train never got there.
	auto train = makeTimetableTrain("status", {"OnPlan", "NoPlanStop", "Central", "Central", "Absent"});
	const double plans[5][2] = {{10.0, 20.0}, {-1.0, -1.0}, {60.0, 70.0}, {80.0, 90.0}, {-1.0, -1.0}};
	for (int index = 0; index < 5; ++index) {
		train->ScheduledArrivals[index] = plans[index][0];
		train->ScheduledDepartures[index] = plans[index][1];
	}
	train->TimetablePoints.push_back(makeTimetableEvent("OnPlan", 11.0, 21.0));
	train->TimetablePoints.push_back(makeTimetableEvent("NoPlanStop", 40.0, 50.0));
	train->TimetablePoints.push_back(makeTimetableEvent("Central", 61.0, 71.0));
	const std::vector<const Train*> trains{train.get()};
	const auto rows = buildTimetableResults(trains);
	// every stop keeps a row whatever its status
	QCOMPARE(rows.size(), std::size_t(5));
	// a planned and simulated time is available
	QCOMPARE(rows[0].arrivalStatus, TimetableValueStatus::Available);
	QCOMPARE(rows[0].departureStatus, TimetableValueStatus::Available);
	// a simulated time without a plan is NoPlan
	QCOMPARE(rows[1].arrivalStatus, TimetableValueStatus::NoPlan);
	QCOMPARE(rows[1].departureStatus, TimetableValueStatus::NoPlan);
	// the second call of a repeated station without a timetable point is NotReached
	QCOMPARE(rows[2].arrivalStatus, TimetableValueStatus::Available);
	QCOMPARE(rows[3].callIndex, 2);
	QCOMPARE(rows[3].arrivalStatus, TimetableValueStatus::NotReached);
	QCOMPARE(rows[3].departureStatus, TimetableValueStatus::NotReached);
	// a stop with neither plan nor simulated time is NoPlanAndNotReached
	QCOMPARE(rows[4].arrivalStatus, TimetableValueStatus::NoPlanAndNotReached);
	QCOMPARE(rows[4].departureStatus, TimetableValueStatus::NoPlanAndNotReached);
	bool statusMatchesDelay = true;
	for (const TimetableResultRow& row : rows) {
		statusMatchesDelay &= (row.arrivalStatus == TimetableValueStatus::Available) == row.arrivalDelaySeconds.available;
		statusMatchesDelay &= (row.departureStatus == TimetableValueStatus::Available) == row.departureDelaySeconds.available;
	}
	QVERIFY2(statusMatchesDelay, "a delay is available exactly when the status is Available");
}

void TestRunResults::classifiesArrivalAndDepartureApart() {
	// Arrival and departure are classified independently.
	auto train = makeTimetableTrain("status-split", {"ArrivalOnly", "DepartureOnly"});
	train->ScheduledArrivals[0] = 5.0;
	train->ScheduledDepartures[0] = -1.0;
	train->ScheduledArrivals[1] = -1.0;
	train->ScheduledDepartures[1] = 30.0;
	train->TimetablePoints.push_back(makeTimetableEvent("ArrivalOnly", -1.0, 9.0));
	train->TimetablePoints.push_back(makeTimetableEvent("DepartureOnly", 25.0, -1.0));
	const std::vector<const Train*> trains{train.get()};
	const auto rows = buildTimetableResults(trains);
	// arrival and departure statuses are independent
	QCOMPARE(rows.size(), std::size_t(2));
	QCOMPARE(rows[0].arrivalStatus, TimetableValueStatus::NotReached);
	QCOMPARE(rows[0].departureStatus, TimetableValueStatus::NoPlan);
	QCOMPARE(rows[1].arrivalStatus, TimetableValueStatus::NoPlan);
	QCOMPARE(rows[1].departureStatus, TimetableValueStatus::NotReached);
}

void TestRunResults::carriesEntranceDelay() {
	// The planned departure at B already contains the 120 s entrance delay; it is passed on unchanged.
	auto train = makeTimetableTrain("entrance", {"A", "B"});
	train->EntranceDelay = 120.0;
	train->entranceDelayStationId = "B";
	train->ScheduledArrivals[0] = -1.0;
	train->ScheduledDepartures[0] = 100.0;
	train->ScheduledArrivals[1] = 400.0;
	train->ScheduledDepartures[1] = 620.0;
	train->TimetablePoints.push_back(makeTimetableEvent("A", 100.0, 100.0));
	train->TimetablePoints.push_back(makeTimetableEvent("B", 450.0, 650.0));
	auto plain = makeTimetableTrain("no-entrance", {"A", "B"});
	const std::vector<const Train*> trains{train.get(), plain.get()};
	const auto results = buildRunResults(trains, 1.0);
	// the run result carries the entrance delay and its station
	QCOMPARE(results.trains.size(), std::size_t(2));
	QVERIFY(closeTo(results.trains[0].entranceDelaySeconds, 120.0));
	QCOMPARE(results.trains[0].entranceDelayStationId, "B");
	// a train without an entrance delay has none in the run result
	QVERIFY(closeTo(results.trains[1].entranceDelaySeconds, 0.0));
	QVERIFY(results.trains[1].entranceDelayStationId.empty());
	const auto rows = buildTimetableResults(trains);
	// the entrance delay station matches a timetable row and the plans stay as set
	QCOMPARE(rows.size(), std::size_t(4));
	QCOMPARE(rows[1].stationId, results.trains[0].entranceDelayStationId);
	QVERIFY(closeTo(rows[1].plannedDepartureSeconds.value, 620.0));
	QVERIFY(closeTo(rows[1].plannedArrivalSeconds.value, 400.0));
	QVERIFY(closeTo(rows[1].departureDelaySeconds.value, 30.0));
	const Train copied(*train);
	// a copy of a train keeps its entrance delay station
	QVERIFY(closeTo(copied.EntranceDelay, 120.0));
	QCOMPARE(copied.entranceDelayStationId, "B");
}

void TestRunResults::capsTimetableRows() {
	std::vector<std::string> stations;
	for (int index = 0; index < Train::kMaxTimetableStations; ++index)
		stations.push_back("S" + std::to_string(index));
	auto train = makeTimetableTrain("overflow", stations);
	train->numStations = Train::kMaxTimetableStations + 5;
	const std::vector<const Train*> trains{train.get()};
	const auto rows = buildTimetableResults(trains);
	// over-cap train yields exactly kMaxTimetableStations rows
	QCOMPARE(static_cast<int>(rows.size()), Train::kMaxTimetableStations);
}

void TestRunResults::copiesStopLists() {
	auto original = makeTimetableTrain("copy-original", {"A", "B"});
	auto copied = std::make_unique<Train>(*original);
	auto assigned = std::make_unique<Train>();
	*assigned = *original;
	// a copy of a train starts with the stops of the original
	QCOMPARE(copied->numStations, 2);
	QCOMPARE(copied->Stations[0].stationName, "A");
	QCOMPARE(copied->Stations[1].stationName, "B");
	QCOMPARE(assigned->numStations, 2);
	QCOMPARE(assigned->Stations[0].stationName, "A");
	QCOMPARE(assigned->Stations[1].stationName, "B");
	copied->Stations[0].stationName = "Changed";
	assigned->Stations[1].stationName = "ChangedToo";
	// a copy of a train keeps its own stop list
	QCOMPARE(original->Stations[0].stationName, "A");
	QCOMPARE(original->Stations[1].stationName, "B");
	QCOMPARE(copied->Stations[0].stationName, "Changed");
	QCOMPARE(copied->Stations[1].stationName, "B");
	QCOMPARE(assigned->Stations[0].stationName, "A");
	QCOMPARE(assigned->Stations[1].stationName, "ChangedToo");
}

void TestRunResults::clampsStationCount() {
	// clampStationCount keeps under-cap counts
	QCOMPARE(Train::clampStationCount(Train::kMaxTimetableStations - 6, "under"), Train::kMaxTimetableStations - 6);
	// clampStationCount keeps at-cap counts
	QCOMPARE(Train::clampStationCount(Train::kMaxTimetableStations, "at"), Train::kMaxTimetableStations);
	// clampStationCount truncates over-cap counts
	QCOMPARE(Train::clampStationCount(Train::kMaxTimetableStations + 5, "over"), Train::kMaxTimetableStations);
}

void TestRunResults::carriesTrainProvenance() {
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
	// one train result
	QCOMPARE(delayedResults.trains.size(), std::size_t(1));
	// missing direct incident evidence stays unavailable
	QVERIFY(delayedResults.trains[0].directIncidentIds.empty());
	QVERIFY(!delayedResults.trains[0].firstDirectIncidentTime.available);
	QVERIFY(!delayedResults.trains[0].firstDirectIncidentLocation.available);
	// run result carries operating, service, performance, and speed provenance
	QCOMPARE(delayedResults.trains[0].operatingCode, "1725");
	QCOMPARE(delayedResults.trains[0].serviceId, "service.native");
	QCOMPARE(delayedResults.trains[0].occurrence, 2);
	QVERIFY(closeTo(delayedResults.trains[0].performancePercent, 75.0));
	QVERIFY(delayedResults.trains[0].hasConfiguredMaximumSpeed);
	QVERIFY(closeTo(delayedResults.trains[0].configuredMaximumSpeedKmh, 120.0));
	QVERIFY(closeTo(delayedResults.trains[0].compositionMaximumSpeedMs, 40.0));
	QVERIFY(closeTo(delayedResults.trains[0].appliedMaximumSpeedMs, 25.0));
	QVERIFY(closeTo(delayedResults.trains[0].appliedMaximumSpeedKmh, 90.0));
	// delayed active start uses first valid sample
	QVERIFY(delayedResults.trains[0].startSeconds.available);
	QVERIFY(closeTo(delayedResults.trains[0].startSeconds.value, 1.5));
	// end time uses last valid sample
	QVERIFY(delayedResults.trains[0].endSeconds.available);
	QVERIFY(closeTo(delayedResults.trains[0].endSeconds.value, 3.5));
	// travel time spans active bounds
	QVERIFY(delayedResults.trains[0].travelSeconds.available);
	QVERIFY(closeTo(delayedResults.trains[0].travelSeconds.value, 2.0));
	delayed->directIncidentIds = {"incident.breakdown"};
	delayed->firstDirectIncidentTime = 2.0;
	delayed->firstDirectIncidentLocation = 30.0;
	const auto directResults = buildRunResults(delayedTrains, 0.5);
	// recorded direct incident evidence remains available
	QVERIFY(directResults.trains[0].firstDirectIncidentTime.available);
	QVERIFY(closeTo(directResults.trains[0].firstDirectIncidentTime.value, 2.0));
	QVERIFY(directResults.trains[0].firstDirectIncidentLocation.available);
	QVERIFY(closeTo(directResults.trains[0].firstDirectIncidentLocation.value, 30.0));
}

void TestRunResults::keepsBoundsAcrossGap() {
	auto gap = makeTrain("gap", 1, 5, 11.0, 21.0, 31.0, 41.0);
	gap->instant_spatial_position[3] = -9999.0;
	const std::vector<const Train*> gapTrains{gap.get()};
	const auto gapResults = buildRunResults(gapTrains, 2.0);
	// internal gap keeps first valid start
	QVERIFY(gapResults.trains[0].startSeconds.available);
	QVERIFY(closeTo(gapResults.trains[0].startSeconds.value, 2.0));
	// internal gap keeps final valid end
	QVERIFY(gapResults.trains[0].endSeconds.available);
	QVERIFY(closeTo(gapResults.trains[0].endSeconds.value, 10.0));
	// internal gap keeps overall travel bounds
	QVERIFY(gapResults.trains[0].travelSeconds.available);
	QVERIFY(closeTo(gapResults.trains[0].travelSeconds.value, 8.0));
}

void TestRunResults::missingTrajectoryIsUnavailable() {
	auto missingTrajectory = makeTrain("missing", 0, 2, 1.0, 2.0, 3.0, 4.0);
	missingTrajectory->earliestActiveTrajectoryIndex = -1;
	const std::vector<const Train*> missingTrains{missingTrajectory.get()};
	const auto missingResults = buildRunResults(missingTrains, 1.0);
	// missing trajectory is unavailable
	QVERIFY(!missingResults.trains[0].startSeconds.available);
	QVERIFY(!missingResults.trains[0].endSeconds.available);
	QVERIFY(!missingResults.trains[0].travelSeconds.available);
	QVERIFY(!missingResults.trains[0].energyConsumedKWh.available);
}

void TestRunResults::shortPowerSeriesIsUnavailable() {
	auto shortPower = makeTrain("short-power", 1, 4, 1.0, 2.0, 3.0, 4.0);
	shortPower->instant_train_power_consumption.resize(2);
	const std::vector<const Train*> shortPowerTrains{shortPower.get()};
	const auto shortPowerResults = buildRunResults(shortPowerTrains, 1.0);
	// short power series is unavailable
	QVERIFY(!shortPowerResults.trains[0].energyConsumedKWh.available);
	QVERIFY(!shortPowerResults.trains[0].energyWithRegenKWh.available);
	QVERIFY(!shortPowerResults.trains[0].substationKWh.available);
	QVERIFY(!shortPowerResults.trains[0].substationWithRegenKWh.available);
}

void TestRunResults::shortEnergySeriesIsUnavailable() {
	auto shortEnergy = makeTrain("short-energy", 1, 4, 1.0, 2.0, 3.0, 4.0);
	shortEnergy->instant_train_energy_consumption.resize(2);
	const std::vector<const Train*> shortEnergyTrains{shortEnergy.get()};
	const auto shortEnergyResults = buildRunResults(shortEnergyTrains, 1.0);
	// short energy series is unavailable
	QVERIFY(!shortEnergyResults.trains[0].energyConsumedKWh.available);
	QVERIFY(!shortEnergyResults.trains[0].energyWithRegenKWh.available);
	QVERIFY(!shortEnergyResults.trains[0].substationKWh.available);
	QVERIFY(!shortEnergyResults.trains[0].substationWithRegenKWh.available);
}

void TestRunResults::convertsEnergyToKWh() {
	auto allFields = makeTrain("fields", 0, 1, 1.0, 2.0, 3.0, 4.0);
	const std::vector<const Train*> allTrains{allFields.get()};
	const auto allResults = buildRunResults(allTrains, 1.0);
	const auto& row = allResults.trains[0];
	// energy consumed converts MJ to kWh
	QVERIFY(row.energyConsumedKWh.available);
	QVERIFY(closeTo(row.energyConsumedKWh.value, 0.27778));
	// regenerative energy converts MJ to kWh
	QVERIFY(row.energyWithRegenKWh.available);
	QVERIFY(closeTo(row.energyWithRegenKWh.value, 0.55556));
	// substation energy converts MJ to kWh
	QVERIFY(row.substationKWh.available);
	QVERIFY(closeTo(row.substationKWh.value, 0.83334));
	// regenerative substation energy converts MJ to kWh
	QVERIFY(row.substationWithRegenKWh.available);
	QVERIFY(closeTo(row.substationWithRegenKWh.value, 1.11112));
}

void TestRunResults::sumsNetworkEnergy() {
	auto allFields = makeTrain("fields", 0, 1, 1.0, 2.0, 3.0, 4.0);
	auto second = makeTrain("second", 0, 1, 5.0, 6.0, 7.0, 8.0);
	auto trains = std::make_unique<Train[]>(2);
	trains[0] = *allFields;
	trains[1] = *second;
	const std::vector<const Train*> totalTrains{&trains[0], &trains[1]};
	const auto totalResults = buildRunResults(totalTrains, 1.0);
	// results table has one row per train plus totals
	QCOMPARE(totalResults.trains.size() + 1, std::size_t(3));
	// network energy total sums available rows
	QVERIFY(totalResults.energyConsumedKWh.available);
	QVERIFY(closeTo(totalResults.energyConsumedKWh.value, 1.66668));
	auto incomplete = std::make_unique<Train>(*second);
	incomplete->instant_train_power_consumption.resize(1);
	trains[0] = *allFields;
	trains[1] = *incomplete;
	const std::vector<const Train*> incompleteTrainPointers{&trains[0], &trains[1]};
	const auto incompleteTotals = buildRunResults(incompleteTrainPointers, 1.0);
	// network totals are unavailable for incomplete rows
	QVERIFY(!incompleteTotals.energyConsumedKWh.available);
	QVERIFY(!incompleteTotals.energyWithRegenKWh.available);
	QVERIFY(!incompleteTotals.substationKWh.available);
	QVERIFY(!incompleteTotals.substationWithRegenKWh.available);
}

void TestRunResults::keepsTerminalPowerOutOfTotals() {
	auto terminalPower = makeTrain("terminal-power", 1, 3, 10.0, 20.0, 30.0, 40.0);
	terminalPower->departure_time = 1;
	terminalPower->instant_train_power_consumption[3] = std::numeric_limits<double>::quiet_NaN();
	terminalPower->TotalEnergyConsumptionWithAndWithoutRegBraking(0.8, 0.7);
	// terminal nonfinite power does not poison regenerative totals
	QVERIFY(std::isfinite(terminalPower->TotalEnergyConsWithRegBrak));
	QVERIFY(std::isfinite(terminalPower->TotalEnergySubstRequestWithRegBrak));
	// energy calculation zeroes the terminal nonfinite sample
	QCOMPARE_EQ(terminalPower->instant_train_power_consumption[3], 0.0);
}

void TestRunResults::sanitizeZeroesNonFiniteSample() {
	auto nanSample = makeTrain("sanitize-nan", 1, 3, 0.0, 0.0, 0.0, 0.0);
	nanSample->instant_train_power_consumption[3] = std::numeric_limits<double>::quiet_NaN();
	nanSample->sanitizeTerminalPowerSample();
	// sanitize zeroes a nonfinite sample at End_Time
	QCOMPARE_EQ(nanSample->instant_train_power_consumption[3], 0.0);
	QVERIFY2(closeTo(nanSample->instant_train_power_consumption[2], 100.0),
		"sanitize leaves samples before End_Time untouched");
}

void TestRunResults::sanitizeKeepsFiniteSample() {
	auto finiteSample = makeTrain("sanitize-finite", 1, 3, 0.0, 0.0, 0.0, 0.0);
	finiteSample->sanitizeTerminalPowerSample();
	QVERIFY2(closeTo(finiteSample->instant_train_power_consumption[3], 100.0),
		"sanitize keeps an in-range finite sample");
}

void TestRunResults::sanitizeStaysInSeries() {
	auto outOfRange = makeTrain("sanitize-out-of-range", 1, 3, 0.0, 0.0, 0.0, 0.0);
	outOfRange->instant_train_power_consumption[3] = std::numeric_limits<double>::quiet_NaN();
	outOfRange->End_Time = 4;
	outOfRange->sanitizeTerminalPowerSample();
	QVERIFY2(std::isnan(outOfRange->instant_train_power_consumption[3]),
		"sanitize does not write past the series end");
	outOfRange->End_Time = -1;
	outOfRange->sanitizeTerminalPowerSample();
	QVERIFY2(std::isnan(outOfRange->instant_train_power_consumption[3]),
		"sanitize does not write for a negative End_Time");
}

void TestRunResults::networkEnergyPassSanitizes() {
	auto networkEnergy = makeTrain("network-energy", 1, 3, 0.0, 0.0, 0.0, 0.0);
	networkEnergy->departure_time = 1;
	networkEnergy->instant_train_power_consumption[3] = std::numeric_limits<double>::quiet_NaN();
	ComputeEnergyConsumptionForAllTrains(networkEnergy.get(), 1);
	// network energy pass sanitizes the terminal sample
	QCOMPARE_EQ(networkEnergy->instant_train_power_consumption[3], 0.0);
	QVERIFY2(closeTo(networkEnergy->TotalEnergyConsumed, 220.0),
		"network energy pass still computes totals");
}

void TestRunResults::collectsRegionalTrains() {
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
	// safe Regional collection keeps both timetable rows
	QCOMPARE(timetableRows.size(), std::size_t(2));
	QCOMPARE(timetableRows[0].trainId, "regional-first");
	QCOMPARE(timetableRows[1].trainId, "regional-second");
	QVERIFY(closeTo(timetableRows[1].simulatedArrivalSeconds.value, 21.0));
	const auto regionalRunResults = buildRunResults(regionalTrains, 1.0);
	// safe Regional collection keeps both run result rows
	QCOMPARE(regionalRunResults.trains.size(), std::size_t(2));
	QCOMPARE(regionalRunResults.trains[0].trainId, "regional-first");
	QCOMPARE(regionalRunResults.trains[1].trainId, "regional-second");
}

void TestRunResults::describesIncidentEvidence() {
	RunResults evidence;
	// the evidence text of a run without trains counts none
	QCOMPARE(directIncidentEvidenceText(evidence), "Incidents acted on 0 of 0 trains");
	TrainRunResult held;
	held.directIncidentIds = {"signal-failure-1", "speed-cap-2"};
	evidence.trains = {held};
	// the evidence text counts a train once however many incidents it has and says train for one
	QCOMPARE(directIncidentEvidenceText(evidence), "Incidents acted on 1 of 1 train");
	evidence.trains = {held, TrainRunResult(), held, TrainRunResult()};
	// the evidence text counts the trains with and without a recorded incident
	QCOMPARE(directIncidentEvidenceText(evidence), "Incidents acted on 2 of 4 trains");
}

void TestRunResults::describesOccurrences() {
	// the occurrences text stays short when every selected occurrence is in the run period
	QCOMPARE(runOccurrencesText(2, 2, 2), "2/2 selected");
	// the occurrences text names the selected occurrences that are in the run period when they differ
	QCOMPARE(runOccurrencesText(196, 196, 185), "196/196 selected, 185 in the run period");
	// the occurrences text names a run period without selected occurrences
	QCOMPARE(runOccurrencesText(3, 5, 0), "3/5 selected, 0 in the run period");
}

void TestRunResults::recordsRunProvenance() {
	QTemporaryDir temp;
	QVERIFY2(temp.isValid(), "temporary directory for provenance tests is available");
	const QString sceneDir = temp.filePath("scene");
	QDir().mkpath(sceneDir);
	const std::vector<QString> sceneFiles = {
		"scene.json", "infrastructure.json", "stations.json", "signalling.json",
		"rolling_stock.json", "services.json", "scenarios.json", "incidents.json", "passengers.json"};
	for (const QString& file : sceneFiles)
		QVERIFY2(writeBytes(QDir(sceneDir).filePath(file), ("{" + file + "}").toUtf8()),
			"accepted scene input is written");
	const std::string directoryHash = hashSceneDirectory(sceneDir.toStdString());
	// directory hash is deterministic
	QVERIFY(!directoryHash.empty());
	QCOMPARE(directoryHash, hashSceneDirectory(sceneDir.toStdString()));
	const SceneInputSnapshot directorySnapshot = readSceneDirectorySnapshot(sceneDir.toStdString());
	// directory snapshot hash uses the shared exact-byte framing
	QVERIFY(directorySnapshot.reason.empty());
	QCOMPARE(hashSceneInputSnapshot(directorySnapshot.bytes), directoryHash);
	const QString scenariosPath = QDir(sceneDir).filePath("scenarios.json");
	const QString incidentsPath = QDir(sceneDir).filePath("incidents.json");
	// a compatible scene without scenario files remains reproducible
	QVERIFY(QFile::remove(scenariosPath));
	QVERIFY(QFile::remove(incidentsPath));
	QVERIFY(!hashSceneDirectory(sceneDir.toStdString()).empty());
	// scenario compatibility fixture is restored
	QVERIFY(writeBytes(scenariosPath, "{scenarios.json}"));
	QVERIFY(writeBytes(incidentsPath, "{incidents.json}"));
	const QString passengerPath = QDir(sceneDir).filePath("passengers.json");
	// an unreadable optional canonical input cannot be ignored
	QVERIFY(QFile::remove(passengerPath));
	QVERIFY(QDir().mkpath(passengerPath));
	QVERIFY(hashSceneDirectory(sceneDir.toStdString()).empty());
	// optional-input failure fixture is restored
	QVERIFY(QDir(passengerPath).removeRecursively());
	QVERIFY(writeBytes(passengerPath, "{passengers.json}"));
	QVERIFY2(writeBytes(QDir(sceneDir).filePath("services.json"), "{services changed}"),
		"directory input can be changed");
	QVERIFY2(directoryHash != hashSceneDirectory(sceneDir.toStdString()),
		"directory hash changes when an accepted file changes");
	const RunInputProvenance changedDirectory = captureSavedInput(
		sceneDir.toStdString(), "directory", false, directoryHash);
	// externally changed directory is tied to the loaded hash and marked non-reproducible
	QVERIFY(!changedDirectory.reproducible);
	QCOMPARE(changedDirectory.sha256, directoryHash);
	QVERIFY(changedDirectory.reason.find("changed since") != std::string::npos);

	const QString bundlePath = temp.filePath("case.egscene");
	QVERIFY2(writeBytes(bundlePath, "bundle bytes\n"), "bundle input is written");
	const std::string bundleHash = hashSceneBundle(bundlePath.toStdString());
	// bundle hash is deterministic
	QVERIFY(!bundleHash.empty());
	QCOMPARE(bundleHash, hashSceneBundle(bundlePath.toStdString()));
	QFile bundleFile(bundlePath);
	// bundle snapshot hash uses the exact archive bytes
	QVERIFY(bundleFile.open(QIODevice::ReadOnly));
	QCOMPARE(hashSceneInputSnapshot(bundleFile.readAll().toStdString()), bundleHash);
	QVERIFY2(writeBytes(bundlePath, "bundle bytes changed\n"), "bundle input can be changed");
	QVERIFY2(bundleHash != hashSceneBundle(bundlePath.toStdString()),
		"bundle hash changes when exact bytes change");
	const RunInputProvenance changedBundle = captureSavedInput(
		bundlePath.toStdString(), "bundle", false, bundleHash);
	// externally changed bundle is tied to the loaded hash and marked non-reproducible
	QVERIFY(!changedBundle.reproducible);
	QCOMPARE(changedBundle.sha256, bundleHash);
	QVERIFY(changedBundle.reason.find("changed since") != std::string::npos);

	const std::string currentDirectoryHash = hashSceneDirectory(sceneDir.toStdString());
	const RunInputProvenance clean = captureSavedInput(
		sceneDir.toStdString(), "directory", false, currentDirectoryHash);
	// clean saved directory is reproducible with an absolute path
	QVERIFY(clean.reproducible);
	QCOMPARE(clean.status, "reproducible");
	QCOMPARE(clean.path, QFileInfo(sceneDir).absoluteFilePath().toStdString());
	QVERIFY(!clean.sha256.empty());
	const RunInputProvenance unverified = captureSavedInput(
		sceneDir.toStdString(), "directory", false, {});
	// saved input without a load/save hash is not labeled reproducible
	QVERIFY(!unverified.reproducible);
	QVERIFY(unverified.reason.find("could not be verified") != std::string::npos);
	const RunInputProvenance dirty = captureSavedInput(
		sceneDir.toStdString(), "directory", true, currentDirectoryHash);
	// dirty saved input is marked non-reproducible with a reason
	QVERIFY(!dirty.reproducible);
	QVERIFY(dirty.dirty);
	QCOMPARE(dirty.status, "non-reproducible");
	QVERIFY(dirty.reason.find("dirty") != std::string::npos);
	const RunInputProvenance unsaved = captureSavedInput({}, "directory", false, {});
	// unsaved input is marked non-reproducible with a reason
	QCOMPARE(unsaved.kind, "unsaved");
	QVERIFY(!unsaved.reproducible);
	QVERIFY(unsaved.reason.find("unsaved") != std::string::npos);
	const RunInputProvenance unreadable = captureSavedInput(temp.filePath("missing").toStdString(),
		"directory", false, {});
	// unreadable saved input is marked non-reproducible with a reason
	QVERIFY(!unreadable.reproducible);
	QVERIFY(unreadable.reason.find("missing or unreadable") != std::string::npos);

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
	provenance.randomSeed = 123456789;
	provenance.selectedOccurrences = {{"service-A", 2, "A\\\"2"}, {"service-B", 1, "B1"}};
	const QString artifactPath = temp.filePath("result.csv");
	QVERIFY2(writeRunArtifactWithProvenance(
				 artifactPath.toStdString(), "csv", "header\nrow\n", provenance),
		"normal artifact and provenance sidecar are written together");
	QByteArray artifactBytes;
	// paired export preserves exact artifact bytes
	QVERIFY(readBytes(artifactPath, artifactBytes));
	QCOMPARE(artifactBytes, QByteArray("header\nrow\n"));
	const QJsonObject sidecar = readJsonObject(artifactPath + ".provenance.json");
	const QJsonObject run = sidecar.value("run").toObject();
	const QJsonObject input = run.value("input").toObject();
	const QJsonArray occurrences = run.value("selected_occurrences").toArray();
	// normal sidecar has schema and artifact fields
	QCOMPARE(sidecar.value("schema_version").toInt(), 1);
	QCOMPARE(sidecar.value("artifact").toObject().value("kind").toString(), "csv");
	QCOMPARE(sidecar.value("artifact").toObject().value("file_name").toString(), "result.csv");
	// sidecar preserves escaped JSON fields and saved-input fields
	QCOMPARE(run.value("case_name").toString(), QString::fromStdString(provenance.caseName));
	QCOMPARE(run.value("applied_scenario").toString(), QString::fromStdString(provenance.appliedScenario));
	QCOMPARE(input.value("path").toString(), QString::fromStdString(clean.path));
	QCOMPARE(input.value("sha256").toString(), QString::fromStdString(clean.sha256));
	// sidecar records the random seed of the run
	QCOMPARE_EQ(run.value("random_seed").toDouble(), 123456789.0);
	// sidecar preserves exact selected occurrence identities
	QCOMPARE(occurrences.size(), qsizetype(2));
	QCOMPARE(occurrences.at(0).toObject().value("service_id").toString(), "service-A");
	QCOMPARE(occurrences.at(0).toObject().value("occurrence").toInt(), 2);
	QCOMPARE(occurrences.at(0).toObject().value("operating_code").toString(), "A\\\"2");
	const QString failedArtifact = temp.filePath("failed.csv");
	QVERIFY2(QDir().mkpath(failedArtifact + ".provenance.json"),
		"sidecar failure path is occupied by a directory");
	// sidecar failure does not publish the artifact
	QVERIFY(!writeRunArtifactWithProvenance(
		failedArtifact.toStdString(), "csv", "header\n", provenance));
	QVERIFY(!QFileInfo::exists(failedArtifact));

	RunProvenance scenario = provenance;
	scenario.appliedScenario = "incident";
	const QString delayArtifact = temp.filePath("delay.csv");
	QVERIFY2(writeDelayArtifactWithProvenance(
				 delayArtifact.toStdString(), "csv", "header\n", provenance, scenario),
		"delay artifact and sidecar are written with two runs");
	const QJsonObject delaySidecar = readJsonObject(delayArtifact + ".provenance.json");
	// delay sidecar keeps baseline and scenario provenance distinct
	QCOMPARE(delaySidecar.value("baseline_run").toObject().value("applied_scenario").toString(), "scenario/\\quoted");
	QCOMPARE(delaySidecar.value("scenario_run").toObject().value("applied_scenario").toString(), "incident");
	const QString failedDelayArtifact = temp.filePath("failed-delay.csv");
	QVERIFY2(QDir().mkpath(failedDelayArtifact + ".provenance.json"),
		"delay sidecar failure path is occupied by a directory");
	// delay sidecar failure does not publish the artifact
	QVERIFY(!writeDelayArtifactWithProvenance(
		failedDelayArtifact.toStdString(), "csv", "header\n", provenance, scenario));
	QVERIFY(!QFileInfo::exists(failedDelayArtifact));
}

void TestRunResults::exportsTrajectoryTail() {
	const auto savedRoutes = defaultRun().infra.routes;
	const std::string savedOutputFolder = defaultRun().settings.OutputMainFolder;
	// A failed check ends the slot, so the default run gets its routes and output folder back on the way out.
	const auto restore = qScopeGuard([&]() {
		defaultRun().infra.routes = savedRoutes;
		defaultRun().settings.OutputMainFolder = savedOutputFolder;
	});
	QTemporaryDir temp;
	// temporary directory for trajectory export is available
	QVERIFY(temp.isValid());
	QVERIFY(QDir().mkpath(temp.filePath("TEMP")));
	defaultRun().settings.OutputMainFolder = temp.path().toStdString();
	defaultRun().infra.routes.clear();
	defaultRun().infra.routes.resize(2);
	defaultRun().infra.routes[1].reversed_direction = true;
	defaultRun().infra.routes[1].OriginalRefReversedRoute = 1000.0;
	const double positions[2][4] = {{100, 110, 130, 160}, {900, 890, 870, 840}};
	const double tails[2][4] = {{16, 26, 46, 76}, {984, 974, 954, 924}};
	for (int direction = 0; direction < 2; ++direction) {
		auto train = std::make_unique<Train>();
		train->run_ = &defaultRun();
		train->trainDescription = direction == 0 ? "tail-forward" : "tail-reversed";
		train->indexOfRoute = direction;
		train->train_length = 84.0;
		train->setTrainVectorSizesFromInput(4);
		train->earliestActiveTrajectoryIndex = 0;
		train->instant_spatial_position = {100, 110, 130, 160};
		train->PrintTrajectory();
		QByteArray bytes;
		const QString path = temp.filePath("TEMP/Traj_Train_" + QString::fromStdString(train->trainDescription) + ".txt");
		QVERIFY2(readBytes(path, bytes), "exported trajectory can be read");
		auto lines = bytes.split('\n');
		if (!lines.isEmpty() && lines.last().isEmpty())
			lines.removeLast();
		// trajectory export has a header and four data rows
		QCOMPARE(lines.size(), qsizetype(5));
		const auto header = lines.first().split('\t');
		// trajectory position and tail columns are identified by the header
		QCOMPARE_GT(header.size(), qsizetype(3));
		QCOMPARE(header[2], QByteArray("Position[m]"));
		QCOMPARE(header[3], QByteArray("Tail_Position[m]"));
		double previousPosition = std::numeric_limits<double>::infinity();
		for (int index = 0; index < 4; ++index) {
			QByteArray line = lines[index + 1];
			if (line.endsWith('\r'))
				line.chop(1);
			const auto cells = line.split('\t');
			// trajectory row keeps all cells including an empty block
			QCOMPARE(cells.size(), qsizetype(8));
			const double position = cells[2].toDouble();
			const double tail = cells[3].toDouble();
			QVERIFY2(closeTo(cells[0].toDouble(), index), "trajectory time follows the sample index");
			QVERIFY2(closeTo(position, positions[direction][index]), direction == 0 ? "forward trajectory writes the front position" : "reversed trajectory writes the reflected front position");
			QVERIFY2(closeTo(tail, tails[direction][index]), direction == 0 ? "forward trajectory writes the rear position" : "reversed trajectory writes the rear position");
			QVERIFY2(closeTo(tail - position, direction == 0 ? -84.0 : 84.0), direction == 0 ? "forward tail is one train length below the front" : "reversed tail is one train length above the front");
			if (direction == 1)
				QVERIFY2(position < previousPosition, "reversed trajectory positions fall");
			previousPosition = position;
		}
	}
}

QTEST_APPLESS_MAIN(TestRunResults)
#include "test_runresults.moc"
