// Dispatching measures as the run applies them (simulation/DispatchMeasures.h): which stop a cancellation ends a train at, and
// what the run records.

#include "simulation/DispatchMeasures.h"
#include "simulation/RollingStock.h"
#include "simulation/SimulationRun.h"

#include <QTest>

#include <string>
#include <thread>
#include <vector>

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
	void queuedMeasuresApplyByHandAtTheNextStep();
	void stopsAheadLeaveOutTheStopsBehind();
	void byHandMeasuresBecomeSceneMeasures();
	void appliedMeasureTextNamesTimeSourceAndEffects();
	void stopThatCannotEndTheRunGivesTheNextStop();
	void onlyEffectsThatChangeATrainCount();
	void closedQueueRefusesMeasures();
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

void TestDispatchMeasures::queuedMeasuresApplyByHandAtTheNextStep() {
	SimulationRun run = twoTrains();
	MeasureQueue queue;
	queue.open();
	// The window submits from its own thread.
	std::thread window([&queue] {
		queue.submit(cancel("first", "B", 1, 5.0));
		queue.submit(cancel("second", "", 2, 5.0));
	});
	window.join();
	applyQueuedMeasures(run, queue, 42);
	QCOMPARE(run.appliedMeasures.size(), std::size_t{2});
	QCOMPARE(run.appliedMeasures[0].measure.id, std::string("first"));
	QCOMPARE(run.appliedMeasures[1].measure.id, std::string("second"));
	for (const AppliedMeasure& applied : run.appliedMeasures) {
		QCOMPARE(applied.step, 42);
		QCOMPARE_EQ(applied.measure.timeSeconds, 42.0);
		QVERIFY(applied.measure.source == MeasureSource::ByHand);
	}
	QCOMPARE(run.trains[0].runEndStop, 1);
	QVERIFY(run.trains[1].OutOfSimulation);
	// The queue is empty afterwards, so the next step applies nothing again.
	applyQueuedMeasures(run, queue, 43);
	QCOMPARE(run.appliedMeasures.size(), std::size_t{2});
	QVERIFY(queue.take().empty());
}

void TestDispatchMeasures::stopsAheadLeaveOutTheStopsBehind() {
	SimulationRun run = twoTrains();
	// The run gives the last stop the position of its station when it starts, so the copy made before the run does the same.
	run.trains[0].Stations[2].X = 0.0;
	run.infra.stations.resize(2);
	run.infra.stations[0].stationName = "B";
	run.infra.stations[0].X = 2.0;
	run.infra.stations[1].stationName = "C";
	run.infra.stations[1].X = 3.0;
	const std::vector<RunStop> stops = runStops(run, run.trains[0]);
	QCOMPARE(stops.size(), std::size_t{3});
	QCOMPARE_EQ(stops[2].routeMetres, 3000.0);
	// Before it enters every stop is ahead; the stops lie at 1000, 2000 and 3000 m.
	QCOMPARE(stopsAhead(stops, -9999.0), std::vector<std::string>({"A", "B", "C"}));
	QCOMPARE(stopsAhead(stops, 1500.0), std::vector<std::string>({"B", "C"}));
	// A train that stands at B, a little short of it, can still end there.
	QCOMPARE(stopsAhead(stops, 2000.5), std::vector<std::string>({"B", "C"}));
	QCOMPARE(stopsAhead(stops, 3200.0), std::vector<std::string>());
}

void TestDispatchMeasures::byHandMeasuresBecomeSceneMeasures() {
	SceneModel scene;
	// Two stations have the label B; the service calls at the second, so a saved measure names that one.
	SceneStation elsewhere;
	elsewhere.id = "st_b_elsewhere";
	elsewhere.name = "B";
	SceneStation station;
	station.id = "st_b";
	station.name = "B";
	scene.stations = {elsewhere, station};
	SceneService service;
	service.id = "s";
	SceneStop stopAtB;
	stopAtB.stationId = "st_b";
	service.stops = {stopAtB};
	scene.services = {service};
	SceneScenario scenario;
	scenario.id = "base";
	SceneMeasure existing;
	existing.id = "by_hand";
	scenario.measures = {existing};
	scene.scenarios = {scenario};

	std::vector<AppliedMeasure> applied(3);
	applied[0].measure = cancel("scenario", "B", 1);
	applied[0].step = 10;
	applied[1].measure = cancel("by_hand_1", "B", 2);
	applied[1].measure.source = MeasureSource::ByHand;
	applied[1].step = 300;
	applied[2].measure = cancel("by_hand_2", "", 0);
	applied[2].measure.source = MeasureSource::ByHand;
	applied[2].step = 420;

	const std::vector<SceneMeasure> measures = byHandSceneMeasures(scene, applied);
	QCOMPARE(measures.size(), std::size_t{2});
	// The ids are new in the scene and differ from each other.
	QCOMPARE(measures[0].id, std::string("by_hand_2"));
	QCOMPARE(measures[1].id, std::string("by_hand_3"));
	QCOMPARE(measures[0].serviceId, std::string("s"));
	QVERIFY(measures[0].hasOccurrence);
	QCOMPARE(measures[0].occurrence, 2);
	QCOMPARE_EQ(measures[0].timeSeconds, 300.0);
	// The run names the station by its name, the scene by its id.
	QCOMPARE(measures[0].fromStationId, std::string("st_b"));
	QVERIFY(!measures[1].hasOccurrence);
	QCOMPARE(measures[1].occurrence, 1);
	QCOMPARE(measures[1].fromStationId, std::string());
	QCOMPARE_EQ(measures[1].timeSeconds, 420.0);
}

void TestDispatchMeasures::appliedMeasureTextNamesTimeSourceAndEffects() {
	SimulationRun run = twoTrains();
	leftStopsBefore(run.trains[1], 1);
	DispatchMeasure measure = cancel("cancel_s", "B");
	measure.source = MeasureSource::ByHand;
	const AppliedMeasure applied = applyMeasure(run, measure, 600);
	QCOMPARE(appliedMeasureText(applied, 8 * 3600), std::string("08:10:00 cancel_s (by hand): s-1 ends at B; s-2 ends at B"));
	AppliedMeasure none;
	none.measure = cancel("gone", "");
	none.measure.serviceId = "x";
	QCOMPARE(appliedMeasureText(none, 0), std::string("00:00:00 gone (scenario): no train of service x in the run"));
}

void TestDispatchMeasures::stopThatCannotEndTheRunGivesTheNextStop() {
	SimulationRun run = twoTrains();
	// B is not at the end of an arc of the route, so the train cannot end its run there.
	run.trains[0].stationArc[1] = -1;
	const AppliedMeasure applied = applyMeasure(run, cancel("m", "B", 1), 0);
	QVERIFY(applied.effects[0].outcome == MeasureOutcome::CannotEndThere);
	QCOMPARE(run.trains[0].runEndStop, 2);
	QCOMPARE(measureEffectText(applied.effects[0]), std::string("cannot end its run at the station and ends at its next stop, C"));
	QVERIFY(measureChangedTrain(applied.effects[0]));
}

void TestDispatchMeasures::onlyEffectsThatChangeATrainCount() {
	MeasureEffect effect;
	for (const MeasureOutcome changed : {MeasureOutcome::NotEntered, MeasureOutcome::EndsAtStop, MeasureOutcome::EndsAtNextStop,
			 MeasureOutcome::StationBehind, MeasureOutcome::CannotEndThere}) {
		effect.outcome = changed;
		QVERIFY(measureChangedTrain(effect));
	}
	for (const MeasureOutcome unchanged : {MeasureOutcome::NoStopLeft, MeasureOutcome::AlreadyLeft, MeasureOutcome::AlreadyEndsEarlier}) {
		effect.outcome = unchanged;
		QVERIFY(!measureChangedTrain(effect));
	}
}

void TestDispatchMeasures::closedQueueRefusesMeasures() {
	MeasureQueue queue;
	// A queue that no run opened takes nothing.
	QVERIFY(!queue.submit(cancel("early", "")));
	queue.open();
	QVERIFY(queue.submit(cancel("first", "")));
	// The last step takes what is queued and closes the queue.
	SimulationRun run = twoTrains();
	applyQueuedMeasures(run, queue, 599, true);
	QCOMPARE(run.appliedMeasures.size(), std::size_t{1});
	QVERIFY(!queue.submit(cancel("late", "")));
	QVERIFY(queue.take().empty());
	// The next run opens it again, empty.
	queue.open();
	QVERIFY(queue.take().empty());
}

QTEST_APPLESS_MAIN(TestDispatchMeasures)
#include "test_dispatchmeasures.moc"
