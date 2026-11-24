// Earliest-arrival routing of passengers over the timetable of a run (simulation/PassengerRouting.h).
//
// Stations are numbered 0 to 4 and called A to E in the comments. Times are seconds.

#include "simulation/PassengerRouting.h"

#include <QTest>

#include <cstddef>
#include <limits>
#include <vector>

using passenger_routing::kUnreached;
using passenger_routing::limitedStretchSeconds;
using passenger_routing::Plan;
using passenger_routing::predictTrainState;
using passenger_routing::Router;
using passenger_routing::Search;
using passenger_routing::Stop;
using passenger_routing::TrainProgress;
using passenger_routing::TrainState;
using passenger_routing::TrainTimetable;

namespace {

constexpr std::size_t A = 0, B = 1, C = 2, D = 3, E = 4;
constexpr std::size_t kStations = 5;

// A stop with an arrival and a departure; -1 leaves a time out.
Stop stop(std::size_t station, double arrival, double departure, int platform = 0) {
	return {static_cast<int>(station), platform, arrival, departure};
}

std::vector<TrainState> fresh(std::size_t trains) {
	return std::vector<TrainState>(trains);
}

// The progress of a train that is `delay` seconds late at `now`, with no speed limit.
TrainProgress lateBy(double delay, double now = 0.0) {
	TrainProgress progress;
	progress.delay = delay;
	progress.now = now;
	return progress;
}

// The clock time hh:mm in seconds.
constexpr double daySeconds(int hours, int minutes) {
	return hours * 3600.0 + minutes * 60.0;
}

// A train from A to B to C: it leaves A at 9:42, is at B from 10:00 to 10:01 and reaches C at 10:16. A to B is 30 km, run at an
// average of 100 km/h.
TrainTimetable aToC() {
	return {{stop(A, -1, daySeconds(9, 42)), stop(B, daySeconds(10, 0), daySeconds(10, 1)), stop(C, daySeconds(10, 16), -1)}};
}

// The progress of aToC under a limit of 40 km/h: running 30 km from A to B takes 2,700 s and 12 km from B to C 1,080 s.
TrainProgress limitedAtA(double now) {
	TrainProgress progress;
	progress.now = now;
	progress.standing = true;
	progress.limited = true;
	progress.runs = {30000.0 / (40.0 / 3.6), 12000.0 / (40.0 / 3.6), -1.0};
	return progress;
}

} // namespace

class TestPassengerRouting : public QObject {
	Q_OBJECT

private slots:
	void walkingTimeGrowsWithPlatformDifference();
	void directTrainIsTaken();
	void departedTrainIsNotTaken();
	void earliestArrivalWinsOverEarliestDeparture();
	void walkingTimeSettingChangesTransfers();
	void equalArrivalKeepsFewerTrains();
	void equalArrivalAndTrainsTakesEarlierDeparture();
	void transferLimitIsKept();
	void transferNeedsWalkingTime();
	void trainStateLimitsBoardingAndShiftsTimes();
	void missingTimesBlockBoardingAndAlighting();
	void originAndUnknownStations();
	void statesOfTheWrongSizeReachNothing();
	void delayUpToTheThresholdKeepsPlannedTimes();
	void delayAboveTheThresholdShiftsLaterStops();
	void standingTrainStaysBoardableOnPlannedTimes();
	void limitedStretchRunsAtTheLowerSpeed();
	void slowedTrainTakesTheLongerRunningTime();
	void slowedTrainLosesTimeFromWhereItIs();
	void limitThatEndsLeavesLaterRunsAlone();
	void passengerStaysWithLateTrainThatArrivesFirst();
};

void TestPassengerRouting::walkingTimeGrowsWithPlatformDifference() {
	using passenger_routing::walkingSeconds;
	// The same platform needs no walking, an unknown platform neither.
	QCOMPARE(walkingSeconds(2, 2), 0.0);
	QCOMPARE(walkingSeconds(0, 5), 0.0);
	QCOMPARE(walkingSeconds(5, 0), 0.0);
	// 30 s per platform number of difference, in both directions.
	QCOMPARE(walkingSeconds(1, 2), 30.0);
	QCOMPARE(walkingSeconds(1, 3), 60.0);
	QCOMPARE(walkingSeconds(5, 1), 120.0);
	// The seconds per platform are a setting.
	QCOMPARE(walkingSeconds(1, 3, 45.0), 90.0);
	QCOMPARE(walkingSeconds(1, 3, 0.0), 0.0);
}

void TestPassengerRouting::walkingTimeSettingChangesTransfers() {
	// T0 arrives at B on platform 1 at 100; T1 leaves B from platform 3 at 150.
	const std::vector<TrainTimetable> trains{{{stop(A, -1, 50, 1), stop(B, 100, -1, 1)}}, {{stop(B, -1, 150, 3), stop(C, 300, -1)}}};
	const auto states = fresh(2);
	// 2 platforms of difference at 30 s is 60 s: ready at 160, too late for T1.
	QCOMPARE(Router(trains, kStations).search(A, 0, 2, states).arrival(C), kUnreached);
	// At 20 s per platform the walk is 40 s and the passenger is in time.
	QCOMPARE(Router(trains, kStations, 20.0).search(A, 0, 2, states).arrival(C), 300.0);
	// At 0 s per platform a platform number makes no difference.
	QCOMPARE(Router(trains, kStations, 0.0).search(A, 0, 2, states).arrival(C), 300.0);
}

void TestPassengerRouting::directTrainIsTaken() {
	// T0: A 100 - B 200/210 - C 300.
	const Router router({{{stop(A, -1, 100), stop(B, 200, 210), stop(C, 300, -1)}}}, kStations);
	const auto states = fresh(1);
	const Search search = router.search(A, 50, 2, states);
	QCOMPARE(search.arrival(B), 200.0);
	QCOMPARE(search.arrival(C), 300.0);
	QCOMPARE(search.arrival(D), kUnreached);
	const Plan plan = search.plan(C);
	QVERIFY(plan.reached());
	QCOMPARE(plan.legs.size(), std::size_t{1});
	QCOMPARE(plan.legs[0].train, std::size_t{0});
	QCOMPARE(plan.legs[0].boardStop, std::size_t{0});
	QCOMPARE(plan.legs[0].alightStop, std::size_t{2});
	QCOMPARE(plan.legs[0].departure, 100.0);
	QCOMPARE(plan.legs[0].arrival, 300.0);
	// A passenger who is ready exactly at the departure still boards.
	QCOMPARE(router.search(A, 100, 2, states).arrival(C), 300.0);
}

void TestPassengerRouting::departedTrainIsNotTaken() {
	const Router router({{{stop(A, -1, 100), stop(C, 300, -1)}}}, kStations);
	const auto states = fresh(1);
	const Search search = router.search(A, 101, 2, states);
	QCOMPARE(search.arrival(C), kUnreached);
	QVERIFY(!search.plan(C).reached());
	QVERIFY(search.plan(C).legs.empty());
}

void TestPassengerRouting::earliestArrivalWinsOverEarliestDeparture() {
	// T0 leaves first but stops everywhere; T1 leaves later and arrives earlier.
	const Router router({{{stop(A, -1, 100), stop(B, 300, 310), stop(C, 600, -1)}}, {{stop(A, -1, 200), stop(C, 400, -1)}}},
		kStations);
	const auto states = fresh(2);
	const Search search = router.search(A, 0, 2, states);
	QCOMPARE(search.arrival(C), 400.0);
	QCOMPARE(search.plan(C).legs[0].train, std::size_t{1});
	// B is only served by T0.
	QCOMPARE(search.arrival(B), 300.0);
}

void TestPassengerRouting::equalArrivalKeepsFewerTrains() {
	// T0 runs A to C directly, arriving at 500. T1 to B and T2 from B to C also arrive at 500.
	const Router router({{{stop(A, -1, 300), stop(C, 500, -1)}}, {{stop(A, -1, 100), stop(B, 200, -1)}},
							{{stop(B, -1, 250), stop(C, 500, -1)}}},
		kStations);
	const auto states = fresh(3);
	const Plan plan = router.search(A, 0, 2, states).plan(C);
	QCOMPARE(plan.arrival, 500.0);
	QCOMPARE(plan.legs.size(), std::size_t{1});
	QCOMPARE(plan.legs[0].train, std::size_t{0});
}

void TestPassengerRouting::equalArrivalAndTrainsTakesEarlierDeparture() {
	// T0 leaves A at 300 and T1 at 100; both reach C at 500 without a change. The first one listed is not taken.
	const Router router({{{stop(A, -1, 300), stop(C, 500, -1)}}, {{stop(A, -1, 100), stop(C, 500, -1)}}}, kStations);
	const auto states = fresh(2);
	const Plan plan = router.search(A, 0, 2, states).plan(C);
	QCOMPARE(plan.arrival, 500.0);
	QCOMPARE(plan.legs.size(), std::size_t{1});
	QCOMPARE(plan.legs[0].train, std::size_t{1});
	// The predicted times count: with T1 late by 400 s it arrives later and T0 wins on arrival.
	std::vector<TrainState> late = fresh(2);
	late[1] = predictTrainState(router.trains()[1], lateBy(400.0));
	QCOMPARE(router.search(A, 0, 2, late).plan(C).legs[0].train, std::size_t{0});
	// Equal departure keeps the first train.
	const Router tie({{{stop(A, -1, 100), stop(C, 500, -1)}}, {{stop(A, -1, 100), stop(C, 500, -1)}}}, kStations);
	QCOMPARE(tie.search(A, 0, 2, states).plan(C).legs[0].train, std::size_t{0});
}

void TestPassengerRouting::transferLimitIsKept() {
	// A chain of three trains: A to B, B to C, C to D.
	const Router router({{{stop(A, -1, 100), stop(B, 200, -1)}}, {{stop(B, -1, 250), stop(C, 350, -1)}},
							{{stop(C, -1, 400), stop(D, 500, -1)}}},
		kStations);
	const auto states = fresh(3);
	QCOMPARE(router.search(A, 0, 0, states).arrival(B), 200.0);
	QCOMPARE(router.search(A, 0, 0, states).arrival(C), kUnreached);
	QCOMPARE(router.search(A, 0, 1, states).arrival(C), 350.0);
	QCOMPARE(router.search(A, 0, 1, states).arrival(D), kUnreached);
	const Plan plan = router.search(A, 0, 2, states).plan(D);
	QCOMPARE(plan.arrival, 500.0);
	QCOMPARE(plan.legs.size(), std::size_t{3});
	QCOMPARE(plan.legs[0].train, std::size_t{0});
	QCOMPARE(plan.legs[1].train, std::size_t{1});
	QCOMPARE(plan.legs[2].train, std::size_t{2});
	QCOMPARE(plan.legs[1].departure, 250.0);
	// A negative limit allows no transfer.
	QCOMPARE(router.search(A, 0, -3, states).arrival(C), kUnreached);
}

void TestPassengerRouting::transferNeedsWalkingTime() {
	// T0 arrives at B on platform 1 at 100. T1 leaves B from platform 5 at 200: the walk of 120 s misses it. T2 leaves
	// from platform 2 at 250.
	const Router router({{{stop(A, -1, 50, 1), stop(B, 100, -1, 1)}}, {{stop(B, -1, 200, 5), stop(C, 300, -1)}},
							{{stop(B, -1, 250, 2), stop(C, 350, -1)}}},
		kStations);
	const auto states = fresh(3);
	const Plan plan = router.search(A, 0, 2, states).plan(C);
	QCOMPARE(plan.arrival, 350.0);
	QCOMPARE(plan.legs[1].train, std::size_t{2});
	// A passenger who starts at B needs no walking and takes T1.
	QCOMPARE(router.search(B, 0, 2, states).arrival(C), 300.0);
	// One who stands on platform 1 at B at 100 walks 120 s to platform 5 as well, and misses T1.
	QCOMPARE(router.search(B, 100, 2, states, 1).arrival(C), 350.0);
}

void TestPassengerRouting::trainStateLimitsBoardingAndShiftsTimes() {
	// T0: A 100 - B 200/210 - C 300 - D 400.
	const Router router({{{stop(A, -1, 100), stop(B, 200, 210), stop(C, 300, 310), stop(D, 400, -1)}}}, kStations);
	std::vector<TrainState> states(1);
	// The train has left A and B: it can be boarded at C only.
	states[0].firstStop = 2;
	QCOMPARE(router.search(A, 0, 2, states).arrival(D), kUnreached);
	QCOMPARE(router.search(B, 0, 2, states).arrival(D), kUnreached);
	QCOMPARE(router.search(C, 0, 2, states).arrival(D), 400.0);
	// Predicted times 60 s later move departure and arrival; a passenger who comes after the planned departure still makes it.
	states[0].arrivals = {-1, 260, 360, 460};
	states[0].departures = {160, 270, 370, -1};
	const Plan plan = router.search(C, 350, 2, states).plan(D);
	QCOMPARE(plan.legs[0].departure, 370.0);
	QCOMPARE(plan.arrival, 460.0);
	// A train past its last stop is left out.
	states[0].firstStop = 4;
	QCOMPARE(router.search(C, 0, 2, states).arrival(D), kUnreached);
}

void TestPassengerRouting::missingTimesBlockBoardingAndAlighting() {
	// T0 has no departure at B and no arrival at C.
	const Router router({{{stop(A, -1, 100), stop(B, 200, -1), stop(C, -1, 310), stop(D, 400, -1)}}}, kStations);
	const auto states = fresh(1);
	const Search fromA = router.search(A, 0, 2, states);
	QCOMPARE(fromA.arrival(B), 200.0);
	QCOMPARE(fromA.arrival(C), kUnreached);
	QCOMPARE(fromA.arrival(D), 400.0);
	QCOMPARE(router.search(B, 0, 2, states).arrival(D), kUnreached);
}

void TestPassengerRouting::originAndUnknownStations() {
	// A stop at a station outside the router is ignored.
	const Router router({{{stop(A, -1, 100), stop(7, 150, 160), stop(E, 200, -1)}}}, kStations);
	const auto states = fresh(1);
	const Search search = router.search(A, 30, 2, states);
	QCOMPARE(search.arrival(A), 30.0);
	QVERIFY(search.plan(A).reached());
	QVERIFY(search.plan(A).legs.empty());
	QCOMPARE(search.arrival(E), 200.0);
	QCOMPARE(search.arrival(99), kUnreached);
	// An origin outside the router, or states that do not match the trains, reach nothing.
	QCOMPARE(router.search(99, 0, 2, states).arrival(E), kUnreached);
	QCOMPARE(router.search(A, 0, 2, fresh(2)).arrival(E), kUnreached);
}

void TestPassengerRouting::statesOfTheWrongSizeReachNothing() {
	const Router router({{{stop(A, -1, 100), stop(B, 200, -1)}}}, kStations);
	std::vector<TrainState> states(1);
	states[0].arrivals = {-1, 200, 300};
	states[0].departures = {100, -1, -1};
	QCOMPARE(router.search(A, 0, 2, states).arrival(B), kUnreached);
}

// Up to the threshold of 5 minutes the train keeps its planned times; above it the times are predicted. The case can set another
// threshold.
void TestPassengerRouting::delayUpToTheThresholdKeepsPlannedTimes() {
	const TrainTimetable train = aToC();
	const TrainState onPlan = predictTrainState(train, lateBy(300.0));
	QVERIFY(onPlan.arrivals.empty());
	QVERIFY(onPlan.departures.empty());
	QVERIFY(!onPlan.late);
	QVERIFY(!onPlan.slowed);
	QCOMPARE(onPlan.delay, 300.0);
	QCOMPARE(onPlan.arrival(train, 1), daySeconds(10, 0));
	const TrainState late = predictTrainState(train, lateBy(301.0));
	QVERIFY(late.late);
	QCOMPARE(late.arrival(train, 1), daySeconds(10, 0) + 301.0);
	QVERIFY(predictTrainState(train, lateBy(400.0), 600.0).arrivals.empty());
	QVERIFY(!predictTrainState(train, lateBy(1.0), 0.0).arrivals.empty());
}

// A train 10 minutes late at A that was due at B at 10:00 is predicted at B at 10:10, and every later time moves as much. A stop
// without a time keeps none.
void TestPassengerRouting::delayAboveTheThresholdShiftsLaterStops() {
	const TrainTimetable train = aToC();
	const TrainState state = predictTrainState(train, lateBy(600.0));
	QCOMPARE(state.departure(train, 0), daySeconds(9, 52));
	QCOMPARE(state.arrival(train, 1), daySeconds(10, 10));
	QCOMPARE(state.departure(train, 1), daySeconds(10, 11));
	QCOMPARE(state.arrival(train, 2), daySeconds(10, 26));
	QCOMPARE(state.arrival(train, 0), -1.0);
	QCOMPARE(state.departure(train, 2), -1.0);
}

// A train that stands at A 80 s after its planned departure is late by 80 s, within the threshold: it can be boarded at A now, and
// its later stops keep their planned times.
void TestPassengerRouting::standingTrainStaysBoardableOnPlannedTimes() {
	const TrainTimetable train = aToC();
	TrainProgress progress = lateBy(80.0, daySeconds(9, 42) + 80.0);
	progress.standing = true;
	const TrainState state = predictTrainState(train, progress);
	QVERIFY(!state.late);
	QCOMPARE(state.departure(train, 0), daySeconds(9, 42) + 80.0);
	QCOMPARE(state.arrival(train, 1), daySeconds(10, 0));
	QCOMPARE(state.departure(train, 1), daySeconds(10, 1));
	// Before its planned departure it keeps its planned times.
	progress.now = daySeconds(9, 40);
	QVERIFY(predictTrainState(train, progress).departures.empty());
}

// The plain rule: 30 km under a limit of 40 km/h take 30,000 m / 11.11 m/s = 2,700 s. A stretch whose line speed is below the
// limit runs at its line speed, so it loses nothing to the limit.
void TestPassengerRouting::limitedStretchRunsAtTheLowerSpeed() {
	QCOMPARE(limitedStretchSeconds(30000.0, 0.0, 40.0 / 3.6), 2700.0);
	QCOMPARE(limitedStretchSeconds(30000.0, 160.0 / 3.6, 40.0 / 3.6), 2700.0);
	QCOMPARE(limitedStretchSeconds(1000.0, 10.0, 20.0), 100.0);
	QCOMPARE(limitedStretchSeconds(1000.0, std::numeric_limits<double>::infinity(), 20.0), 50.0);
}

// The train stands at A on time under a limit of 40 km/h. A to B, run in 18 minutes, takes 2,700 s under the limit, so the train is
// predicted at B at 10:27. The 27 minutes lost carry over to C, where the 12 km take 1,080 s under the limit, more than the 15
// planned minutes.
void TestPassengerRouting::slowedTrainTakesTheLongerRunningTime() {
	const TrainTimetable train = aToC();
	const TrainState state = predictTrainState(train, limitedAtA(daySeconds(9, 42)));
	QVERIFY(state.slowed);
	QVERIFY(!state.late);
	QCOMPARE(state.departure(train, 0), daySeconds(9, 42));
	QCOMPARE(state.arrival(train, 1), daySeconds(10, 27));
	QCOMPARE(state.departure(train, 1), daySeconds(10, 28));
	QCOMPARE(state.arrival(train, 2), daySeconds(10, 46));
	// A run that is faster under the limit than planned keeps its planned running time.
	TrainProgress fast = limitedAtA(daySeconds(9, 42));
	fast.runs = {600.0, 600.0, -1.0};
	const TrainState onPlan = predictTrainState(train, fast);
	QCOMPARE(onPlan.arrival(train, 1), daySeconds(10, 0));
	QCOMPARE(onPlan.arrival(train, 2), daySeconds(10, 16));
	// A train that is also late adds the loss to its delay.
	TrainProgress late = limitedAtA(daySeconds(9, 52));
	late.delay = 600.0;
	QCOMPARE(predictTrainState(train, late).arrival(train, 1), daySeconds(10, 37));
}

// The train runs from A to B: under the limit it reaches B at 10:05 from where it is now. That arrival counts when it is later
// than the planned one plus the delay, and the run to C follows from it.
void TestPassengerRouting::slowedTrainLosesTimeFromWhereItIs() {
	const TrainTimetable train = aToC();
	TrainProgress progress = limitedAtA(daySeconds(9, 55));
	progress.standing = false;
	progress.firstStop = 1;
	progress.nextStop = 1;
	progress.nextArrival = daySeconds(10, 5);
	const TrainState state = predictTrainState(train, progress);
	QCOMPARE(state.arrival(train, 1), daySeconds(10, 5));
	QCOMPARE(state.arrival(train, 2), daySeconds(10, 6) + 1080.0);
	progress.nextArrival = daySeconds(9, 58);
	QCOMPARE(predictTrainState(train, progress).arrival(train, 1), daySeconds(10, 0));
	progress.delay = 240.0;
	QCOMPARE(predictTrainState(train, progress).arrival(train, 1), daySeconds(10, 4));
}

// The limit ends at 10:10, before the train leaves B at 10:28: the run to C keeps its planned running time.
void TestPassengerRouting::limitThatEndsLeavesLaterRunsAlone() {
	const TrainTimetable train = aToC();
	TrainProgress progress = limitedAtA(daySeconds(9, 42));
	progress.limitEnds = daySeconds(10, 10);
	const TrainState state = predictTrainState(train, progress);
	QCOMPARE(state.arrival(train, 1), daySeconds(10, 27));
	QCOMPARE(state.arrival(train, 2), daySeconds(10, 43));
}

// T0 is planned from A at 9:00 to B at 10:00, T1 20 minutes later. A passenger stays with T0 while it is 10 minutes late, as it still
// arrives first, and takes T1 when T0 is 30 minutes late.
void TestPassengerRouting::passengerStaysWithLateTrainThatArrivesFirst() {
	const Router router({{{stop(A, -1, daySeconds(9, 0)), stop(B, daySeconds(10, 0), -1)}}, {{stop(A, -1, daySeconds(9, 20)), stop(B, daySeconds(10, 20), -1)}}},
		kStations);
	std::vector<TrainState> states = fresh(2);
	states[0] = predictTrainState(router.trains()[0], lateBy(600.0));
	Plan plan = router.search(A, daySeconds(8, 50), 2, states).plan(B);
	QCOMPARE(plan.legs[0].train, std::size_t{0});
	QCOMPARE(plan.arrival, daySeconds(10, 10));
	states[0] = predictTrainState(router.trains()[0], lateBy(1800.0));
	plan = router.search(A, daySeconds(8, 50), 2, states).plan(B);
	QCOMPARE(plan.legs[0].train, std::size_t{1});
	QCOMPARE(plan.arrival, daySeconds(10, 20));
}

QTEST_APPLESS_MAIN(TestPassengerRouting)
#include "test_passengerrouting.moc"
