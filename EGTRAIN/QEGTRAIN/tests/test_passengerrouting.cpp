// Earliest-arrival routing of passengers over the timetable of a run (simulation/PassengerRouting.h).
//
// Stations are numbered 0 to 4 and called A to E in the comments. Times are seconds.

#include "simulation/PassengerRouting.h"

#include <QTest>

#include <cstddef>
#include <vector>

using passenger_routing::kUnreached;
using passenger_routing::Plan;
using passenger_routing::Router;
using passenger_routing::Search;
using passenger_routing::Stop;
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

} // namespace

class TestPassengerRouting : public QObject {
	Q_OBJECT

private slots:
	void walkingTimeGrowsWithPlatformDifference();
	void directTrainIsTaken();
	void departedTrainIsNotTaken();
	void earliestArrivalWinsOverEarliestDeparture();
	void equalArrivalKeepsFewerTrains();
	void transferLimitIsKept();
	void transferNeedsWalkingTime();
	void trainStateLimitsBoardingAndShiftsTimes();
	void missingTimesBlockBoardingAndAlighting();
	void originAndUnknownStations();
};

void TestPassengerRouting::walkingTimeGrowsWithPlatformDifference() {
	using passenger_routing::walkingSeconds;
	// The same or a neighbouring platform needs no walking, an unknown platform neither.
	QCOMPARE(walkingSeconds(2, 2), 0.0);
	QCOMPARE(walkingSeconds(2, 3), 0.0);
	QCOMPARE(walkingSeconds(0, 5), 0.0);
	QCOMPARE(walkingSeconds(5, 0), 0.0);
	// 30 s per platform of difference beyond that.
	QCOMPARE(walkingSeconds(1, 5), 120.0);
	QCOMPARE(walkingSeconds(5, 1), 120.0);
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
	// A delay of 60 s shifts departure and arrival; a passenger who comes after the planned departure still makes it.
	states[0].delay = 60;
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

QTEST_APPLESS_MAIN(TestPassengerRouting)
#include "test_passengerrouting.moc"
