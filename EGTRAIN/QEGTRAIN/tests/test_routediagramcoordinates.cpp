#include "diagrams/CompletedRunData.h"
#include "diagrams/RouteDiagramCoordinates.h"

#include <QTest>

#include <cmath>
#include <iostream>
#include <limits>

static const RouteDiagramPath reference{"main", {{"a", "A", "Alpha", 42}, {"b", "B", "Beta", 47}, {"c", "C", "Gamma", 53}}};
static const RouteDiagramPath reverse{"return", {{"c", "C", "Gamma", 5}, {"b", "B", "Beta", 11}, {"a", "A", "Alpha", 16}}};

class TestRouteDiagramCoordinates : public QObject {
	Q_OBJECT

private slots:
	void projectsReverseRouteOntoReference();
	void projectsReferenceOntoItself();
	void doesNotProjectUnmatchedPaths();
	void labelsColocatedStations();
	void convertsTrajectoryMetresToKilometres();
	void buildsCsvOfSelectedTrains();
	void labelsStationsWithoutName();
	void rangesSectionAxis();
	void placesStopOnReference();
	void cleanup();
	void cleanupTestCase();

private:
	bool m_failed = false;
};

void TestRouteDiagramCoordinates::projectsReverseRouteOntoReference() {
	const auto projection = buildRouteDiagramProjection(reverse, reference);
	const auto middle = projection.map(11);
	QVERIFY(middle);
	QVERIFY(!(std::abs(*middle - 47) > 1e-8));
	QVERIFY(!projection.map(4));
	QVERIFY(!projection.map(17));
}

void TestRouteDiagramCoordinates::projectsReferenceOntoItself() {
	const auto identity = buildRouteDiagramProjection(reference, reference);
	QVERIFY(identity.identity);
	QVERIFY(identity.map(42) == 42);
	const RouteDiagramPath inconsistent{"main", {{"a", "A", "Alpha", 43}, {"b", "B", "Beta", 48}, {"c", "C", "Gamma", 54}}};
	QVERIFY(!buildRouteDiagramProjection(inconsistent, reference).identity);
}

void TestRouteDiagramCoordinates::doesNotProjectUnmatchedPaths() {
	const RouteDiagramPath unrelated{"other", {{"x", "", "Alpha", 1}, {"y", "", "Beta", 2}}};
	QVERIFY(!buildRouteDiagramProjection(unrelated, reference).map(1));
	const RouteDiagramPath repeated{"return", {{"c", "C", "Gamma", 5}, {"b", "B", "Beta", 11}, {"a", "A", "Alpha", 16}, {"b", "B", "Beta", 21}}};
	QVERIFY(!buildRouteDiagramProjection(repeated, reference).map(11));
	const RouteDiagramPath duplicateReference{"main", {{"a", "A", "Alpha", 42}, {"b", "B", "Beta", 47}, {"c", "C", "Gamma", 53}, {"b", "B", "Beta", 58}}};
	QVERIFY(!buildRouteDiagramProjection(reverse, duplicateReference).map(11));
}

void TestRouteDiagramCoordinates::labelsColocatedStations() {
	const RouteDiagramPath colocated{"same", {{"n1", "one", "Central", -2}, {"n2", "two", "Central", -2}, {"n3", "zero", "Zero", 0}}};
	const auto labels = routeDiagramStationLabels(colocated);
	QCOMPARE(labels.size(), std::size_t(2));
	QVERIFY(labels.at(-2).find("one") != std::string::npos);
	QVERIFY(labels.at(-2).find("two") != std::string::npos);
	QVERIFY(labels.at(0).find("zero") != std::string::npos);
}

void TestRouteDiagramCoordinates::convertsTrajectoryMetresToKilometres() {
	QCOMPARE_EQ(routeDiagramTrajectoryKm(42000), 42.0);
	QCOMPARE_EQ(routeDiagramTrajectoryKm(-2000), -2.0);
}

void TestRouteDiagramCoordinates::buildsCsvOfSelectedTrains() {
	const std::vector<std::vector<std::string>> rows = {
		{"A", "ref", "forward", "planned arrival", "one", "1", "1", "", "-2"},
		{"B", "ref", "reverse", "trajectory", "", "", "", "0", "0"}};
	const std::string csv = buildRouteDiagramCsv(rows, {"A"});
	QCOMPARE(csv.find("Train,Reference route,Source route,Event,Station,Journey order,Call,Elapsed time[s],Reference route X[km]"),
		std::size_t(0));
	QVERIFY(csv.find("A,ref,forward,planned arrival,one,1,1,,-2") != std::string::npos);
	QCOMPARE(csv.find("B,ref,"), std::string::npos);
	QCOMPARE(buildRouteDiagramCsv(rows, {}).find("A,ref,"), std::string::npos);
}

void TestRouteDiagramCoordinates::labelsStationsWithoutName() {
	const RouteDiagramPath compact{"labels", {{"n", "Paimpol", "Paimpol", 0}, {"m", "Unnamed", "", 1}}};
	const auto compactLabels = routeDiagramStationLabels(compact);
	QCOMPARE(compactLabels.at(0), "Paimpol");
	QCOMPARE(compactLabels.at(1), "Unnamed");
}

// Distance range of a section chart, exactly: usable sections first, else the whole reference path.
void TestRouteDiagramCoordinates::rangesSectionAxis() {
	const auto closeTo = [](const std::pair<double, double>& range, double low, double high) {
		return std::abs(range.first - low) < 1e-12 && std::abs(range.second - high) < 1e-12;
	};
	const double nan = std::numeric_limits<double>::quiet_NaN();
	QVERIFY(closeTo(blockSectionAxisRange(buildBlockSectionExtents({{"@a@", 4.0, 5.0}, {"@b@", 5.0, 6.0}}), reference), 4.0, 6.0));
	QVERIFY(closeTo(blockSectionAxisRange(buildBlockSectionExtents({{"@a@", 6.0, 4.0}}), reference), 4.0, 6.0));
	QVERIFY(closeTo(blockSectionAxisRange(buildBlockSectionExtents({{"@a@", 1.0, 1.001}}), reference), 1.0, 1.02));
	const auto unusable = buildBlockSectionExtents({{"@zero@", 3.0, 3.0}, {"@nan@", 1.0, nan}});
	QVERIFY(closeTo(blockSectionAxisRange(unusable, reference), 42.0, 53.0));
	const RouteDiagramPath falling{"back", {{"c", "C", "Gamma", 53}, {"b", "B", "Beta", 47}, {"a", "A", "Alpha", 42}}};
	QVERIFY(closeTo(blockSectionAxisRange({}, falling), 42.0, 53.0));
	const RouteDiagramPath returning{"loop", {{"a", "A", "Alpha", 2}, {"b", "B", "Beta", 8}, {"c", "C", "Gamma", -2}, {"n", "", "", nan}}};
	QVERIFY(closeTo(blockSectionAxisRange(unusable, returning), -2.0, 8.0));
	QVERIFY(closeTo(blockSectionAxisRange(unusable, RouteDiagramPath{}), 0.0, 1.0));
}

// A stop lies where its node is on the path at the stored abscissa, mapped onto the reference route.
void TestRouteDiagramCoordinates::placesStopOnReference() {
	const auto projection = buildRouteDiagramProjection(reverse, reference);
	CompletedStop stop;
	stop.nodeId = "b";
	stop.routeX = 11;
	const auto stopOnReference = routeDiagramStopPosition(stop, reverse, projection);
	QVERIFY(stopOnReference);
	QVERIFY(!(std::abs(*stopOnReference - 47) > 1e-8));
	stop.routeX = 11.5;
	QVERIFY(!routeDiagramStopPosition(stop, reverse, projection));
	stop.routeX = 11;
	stop.nodeId = "z";
	QVERIFY(!routeDiagramStopPosition(stop, reverse, projection));
	stop.nodeId.clear();
	QVERIFY(!routeDiagramStopPosition(stop, reverse, projection));
}

void TestRouteDiagramCoordinates::cleanup() {
	m_failed = m_failed || QTest::currentTestFailed();
}

// tools/e2e/route_diagram_smoke.py looks for this line.
void TestRouteDiagramCoordinates::cleanupTestCase() {
	if (!m_failed)
		std::cout << "route coordinate projection passed\n";
}

QTEST_APPLESS_MAIN(TestRouteDiagramCoordinates)
#include "test_routediagramcoordinates.moc"
