// Dispatching measures as the run applies them (simulation/DispatchMeasures.h): which stop a cancellation ends a train at, and
// what the run records.

#include "simulation/DispatchMeasures.h"
#include "simulation/RollingStock.h"
#include "simulation/SimulationRun.h"

#include <QTest>

#include <string>

namespace {

// A train of service s that calls at A, B and C, the stops at the ends of arcs of its route, and has not entered the network.
Train train(int occurrence) {
	Train result;
	result.serviceId = "s";
	result.serviceOccurrence = occurrence;
	result.trainDescription = "s-" + std::to_string(occurrence);
	result.numStations = 3;
	result.Stations.resize(3);
	const char* names[] = {"A", "B", "C"};
	for (int stop = 0; stop < 3; ++stop) {
		Node& node = result.Stations[static_cast<std::size_t>(stop)];
		node.stationName = names[stop];
		node.X = 1.0 + stop;
		node.StepStopped = 0;
		result.stationBlockSection[stop] = stop;
		result.stationArc[stop] = 0;
	}
	result.Start_Node_X = 0.0;
	result.CanEnter = false;
	result.OutOfSimulation = false;
	result.StoppedForServiceStop = false;
	return result;
}

// The train has entered and left its stops before `stop`.
void leftStopsBefore(Train& t, int stop) {
	t.CanEnter = true;
	for (int earlier = 0; earlier < stop; ++earlier)
		t.Stations[static_cast<std::size_t>(earlier)].StepStopped = 10;
}

// A measure of service s from the station, or complete with an empty station.
DispatchMeasure cancel(const std::string& id, const std::string& fromStation, int occurrence = 0, double time = 0.0) {
	DispatchMeasure measure;
	measure.id = id;
	measure.serviceId = "s";
	measure.occurrence = occurrence;
	measure.fromStation = fromStation;
	measure.timeSeconds = time;
	return measure;
}

// A run with the trains of two occurrences of service s.
SimulationRun twoTrains() {
	SimulationRun run;
	run.trains = {train(1), train(2)};
	run.numTrains = 2;
	return run;
}

} // namespace

class TestDispatchMeasures : public QObject {
	Q_OBJECT

private slots:
	void completeCancellationKeepsATrainOut();
	void cancellationFromAStationEndsTheRunThere();
	void completeCancellationEndsARunningTrainAtItsNextStop();
	void stationBehindTheTrainEndsItAtItsNextStop();
	void trainThatLeftOrEndsEarlierKeepsItsEnd();
	void occurrenceNamesOneTrain();
	void dueMeasuresApplyOnceInTheirOrder();
	void stopThatCannotEndTheRunGivesTheNextStop();
};

void TestDispatchMeasures::completeCancellationKeepsATrainOut() {
	SimulationRun run = twoTrains();
	DispatchMeasure measure = cancel("m", "", 1);
	measure.source = MeasureSource::ByHand;
	const AppliedMeasure applied = applyMeasure(run, measure, 30);
	QVERIFY(run.trains[0].OutOfSimulation);
	QCOMPARE(run.trains[0].runEndStop, -1);
	QVERIFY(!run.trains[1].OutOfSimulation);
	QCOMPARE(run.appliedMeasures.size(), std::size_t{1});
	QCOMPARE(applied.step, 30);
	QVERIFY(applied.measure.source == MeasureSource::ByHand);
	QCOMPARE(applied.effects.size(), std::size_t{1});
	QCOMPARE(applied.effects[0].trainDescription, std::string("s-1"));
	QVERIFY(applied.effects[0].outcome == MeasureOutcome::NotEntered);
	QCOMPARE(measureEffectText(applied.effects[0]), std::string("does not enter"));
}

void TestDispatchMeasures::cancellationFromAStationEndsTheRunThere() {
	SimulationRun run = twoTrains();
	const AppliedMeasure applied = applyMeasure(run, cancel("m", "B", 1), 0);
	QVERIFY(!run.trains[0].OutOfSimulation);
	QCOMPARE(run.trains[0].runEndStop, 1);
	QVERIFY(applied.effects[0].outcome == MeasureOutcome::EndsAtStop);
	QCOMPARE(measureEffectText(applied.effects[0]), std::string("ends at B"));
	// From its first stop a train that has not entered does not enter.
	QVERIFY(applyMeasure(run, cancel("first", "A", 2), 0).effects[0].outcome == MeasureOutcome::NotEntered);
	QVERIFY(run.trains[1].OutOfSimulation);
}

void TestDispatchMeasures::completeCancellationEndsARunningTrainAtItsNextStop() {
	SimulationRun run = twoTrains();
	leftStopsBefore(run.trains[0], 1);
	const AppliedMeasure applied = applyMeasure(run, cancel("m", "", 1), 100);
	QCOMPARE(run.trains[0].runEndStop, 1);
	QVERIFY(applied.effects[0].outcome == MeasureOutcome::EndsAtNextStop);
	QCOMPARE(measureEffectText(applied.effects[0]), std::string("ends at its next stop, B"));
	// A train that stands at a stop ends there.
	run.trains[1].CanEnter = true;
	run.trains[1].Stations[0].StepStopped = 1;
	run.trains[1].StoppedForServiceStop = true;
	applyMeasure(run, cancel("standing", "", 2), 100);
	QCOMPARE(run.trains[1].runEndStop, 0);
}

void TestDispatchMeasures::stationBehindTheTrainEndsItAtItsNextStop() {
	SimulationRun run = twoTrains();
	leftStopsBefore(run.trains[0], 2);
	const AppliedMeasure applied = applyMeasure(run, cancel("m", "B", 1), 400);
	QCOMPARE(run.trains[0].runEndStop, 2);
	QVERIFY(applied.effects[0].outcome == MeasureOutcome::StationBehind);
	QCOMPARE(measureEffectText(applied.effects[0]), std::string("has passed the station and ends at its next stop, C"));
}

void TestDispatchMeasures::trainThatLeftOrEndsEarlierKeepsItsEnd() {
	SimulationRun run = twoTrains();
	run.trains[0].CanEnter = true;
	run.trains[0].OutOfSimulation = true;
	QVERIFY(applyMeasure(run, cancel("left", "B", 1), 900).effects[0].outcome == MeasureOutcome::AlreadyLeft);
	QCOMPARE(run.trains[0].runEndStop, -1);
	applyMeasure(run, cancel("first", "B", 2), 0);
	const AppliedMeasure later = applyMeasure(run, cancel("second", "C", 2), 10);
	QCOMPARE(run.trains[1].runEndStop, 1);
	QVERIFY(later.effects[0].outcome == MeasureOutcome::AlreadyEndsEarlier);
	QCOMPARE(measureEffectText(later.effects[0]), std::string("already ends at B"));
}

void TestDispatchMeasures::occurrenceNamesOneTrain() {
	SimulationRun run = twoTrains();
	const AppliedMeasure every = applyMeasure(run, cancel("every", "C"), 0);
	QCOMPARE(every.effects.size(), std::size_t{2});
	QCOMPARE(run.trains[0].runEndStop, 2);
	QCOMPARE(run.trains[1].runEndStop, 2);
	DispatchMeasure other = cancel("other", "");
	other.serviceId = "t";
	QVERIFY(applyMeasure(run, other, 0).effects.empty());
	QCOMPARE(run.appliedMeasures.size(), std::size_t{2});
}

void TestDispatchMeasures::dueMeasuresApplyOnceInTheirOrder() {
	SimulationRun run = twoTrains();
	run.measures = {cancel("early", "C", 1, 5.0), cancel("late", "B", 2, 20.0)};
	applyDueMeasures(run, 4);
	QVERIFY(run.appliedMeasures.empty());
	applyDueMeasures(run, 5);
	applyDueMeasures(run, 6);
	QCOMPARE(run.appliedMeasures.size(), std::size_t{1});
	QCOMPARE(run.appliedMeasures[0].measure.id, std::string("early"));
	QCOMPARE(run.appliedMeasures[0].step, 5);
	applyDueMeasures(run, 30);
	QCOMPARE(run.appliedMeasures.size(), std::size_t{2});
	QCOMPARE(run.appliedMeasures[1].step, 30);
	QCOMPARE(run.nextMeasure, std::size_t{2});
}

void TestDispatchMeasures::stopThatCannotEndTheRunGivesTheNextStop() {
	SimulationRun run = twoTrains();
	// B is not at the end of an arc of the route, so the train cannot end its run there.
	run.trains[0].stationArc[1] = -1;
	const AppliedMeasure applied = applyMeasure(run, cancel("m", "B", 1), 0);
	QVERIFY(applied.effects[0].outcome == MeasureOutcome::CannotEndThere);
	QCOMPARE(run.trains[0].runEndStop, 2);
	QCOMPARE(measureEffectText(applied.effects[0]), std::string("cannot end its run at the station and ends at its next stop, C"));
}

QTEST_APPLESS_MAIN(TestDispatchMeasures)
#include "test_dispatchmeasures.moc"
