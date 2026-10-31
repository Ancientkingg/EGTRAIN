// Tests of the position scale of diagrams/PositionScale.h, which maps the route distance of a blocking-time diagram to
// the geo X of the scene, and of the geo X columns of the blocking-time export that it fills.

#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/PositionScale.h"
#include "diagrams/RunCsv.h"

#include <QStringList>
#include <QTest>

#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool near(double actual, double expected) {
	return std::abs(actual - expected) < 1e-9;
}

// One section for each entry: the route distance of the start and the end, then the geo X of both, in km.
std::vector<BlockSectionExtent> sections(const std::vector<std::array<double, 4>>& ends) {
	std::vector<BlockSectionInput> inputs;
	for (std::size_t i = 0; i < ends.size(); ++i)
		inputs.push_back({"@s" + std::to_string(i) + "@", ends[i][0], ends[i][1], ends[i][2], ends[i][3]});
	return buildBlockSectionExtents(inputs);
}

std::vector<double> geoOf(const std::vector<PositionScale::Label>& labels) {
	std::vector<double> values;
	for (const PositionScale::Label& label : labels)
		values.push_back(label.geoKm);
	return values;
}

bool nearAll(const std::vector<double>& actual, const std::vector<double>& expected) {
	if (actual.size() != expected.size())
		return false;
	for (std::size_t i = 0; i < actual.size(); ++i)
		if (!near(actual[i], expected[i]))
			return false;
	return true;
}

// Each label lies further along the route than the one before, by at least 60 px at this zoom.
bool spaced(const std::vector<PositionScale::Label>& labels, double lowKm, double highKm, double widthPx) {
	for (std::size_t i = 1; i < labels.size(); ++i)
		if ((labels[i].routeKm - labels[i - 1].routeKm) * widthPx / (highKm - lowKm) < 60.0 - 1e-6)
			return false;
	return true;
}

std::vector<std::vector<std::string>> csvRows(const std::string& text) {
	std::vector<std::vector<std::string>> rows;
	std::istringstream lines(text);
	std::string line;
	while (std::getline(lines, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		std::vector<std::string> fields;
		std::istringstream cells(line);
		std::string cell;
		while (std::getline(cells, cell, ','))
			fields.push_back(cell);
		if (!line.empty() && line.back() == ',')
			fields.emplace_back();
		rows.push_back(fields);
	}
	return rows;
}

bool nearField(const std::string& field, double expected) {
	return !field.empty() && near(std::stod(field), expected);
}

} // namespace

class TestPositionScale : public QObject {
	Q_OBJECT

private slots:
	void mapsForwardRoute();
	void countsDownAgainstTheCoordinate();
	void splitsRouteAtJump();
	void repeatsGeoXWhereRouteTurnsBack();
	void extendsSectionsOutsideTheScope();
	void spacesLabels();
	void fillsExportColumns();
};

void TestPositionScale::mapsForwardRoute() {
	// In a second display region: the geo X runs 100 km ahead of the route distance.
	const PositionScale scale(sections({{0.0, 2.0, 100.0, 102.0}, {2.0, 4.0, 102.0, 104.0}}));
	// A forward route is one stretch.
	QVERIFY(!scale.empty());
	QVERIFY(scale.jumps().empty());
	QCOMPARE(scale.stretches().size(), std::size_t(1));
	const PositionScale::Stretch& stretch = scale.stretches().front();
	// The stretch spans the route from the first to the last section.
	QCOMPARE_EQ(stretch.startKm, 0.0);
	QCOMPARE_EQ(stretch.endKm, 4.0);
	QCOMPARE_EQ(stretch.geoStartKm, 100.0);
	QCOMPARE_EQ(stretch.geoEndKm, 104.0);
	// A forward route maps the route distance linearly within each section.
	QVERIFY(near(scale.geoAt(0.0), 100.0));
	QVERIFY(near(scale.geoAt(1.25), 101.25));
	QVERIFY(near(scale.geoAt(3.0), 103.0));
	QVERIFY(near(scale.geoAt(4.0), 104.0));
}

void TestPositionScale::countsDownAgainstTheCoordinate() {
	// Like Paimpol route1, which runs from 36.5 km to 34.5 km in geo X.
	const PositionScale scale(sections({{0.0, 1.2, 36.5, 35.3}, {1.2, 2.0, 35.3, 34.5}}));
	// A route against the coordinate is one stretch that counts down.
	QVERIFY(scale.jumps().empty());
	QCOMPARE(scale.stretches().size(), std::size_t(1));
	QCOMPARE_EQ(scale.stretches().front().geoStartKm, 36.5);
	QCOMPARE_EQ(scale.stretches().front().geoEndKm, 34.5);
	// The geo X counts down along the route.
	QVERIFY(near(scale.geoAt(0.5), 36.0));
	QVERIFY(near(scale.geoAt(1.6), 34.9));
	// 300 px for 2 km: labels 0.4 km apart at least, so every 0.5 km.
	const auto labels = scale.roundLabels(0.0, 2.0, 300.0);
	// The labels count down at increasing route distance.
	QVERIFY(nearAll(geoOf(labels), {36.5, 36.0, 35.5, 35.0, 34.5}));
	QVERIFY(spaced(labels, 0.0, 2.0, 300.0));
}

void TestPositionScale::splitsRouteAtJump() {
	// Netherlands route0: 18.298 km continues at 30.569 km. The next section starts half a metre on, which is no jump.
	const PositionScale scale(sections({{17.0, 18.298, 17.0, 18.298}, {18.298, 19.0, 30.569, 31.271}, {19.0, 20.0, 31.2715, 32.2715}}));
	// One jump.
	QCOMPARE(scale.jumps().size(), std::size_t(1));
	const PositionScale::Jump& jump = scale.jumps().front();
	// The jump gives its route distance and the geo X on both sides.
	QCOMPARE_EQ(jump.routeKm, 18.298);
	QCOMPARE_EQ(jump.geoBeforeKm, 18.298);
	QCOMPARE_EQ(jump.geoAfterKm, 30.569);
	// The jump splits the route into two stretches.
	QCOMPARE(scale.stretches().size(), std::size_t(2));
	QCOMPARE_EQ(scale.stretches()[0].endKm, 18.298);
	QCOMPARE_EQ(scale.stretches()[1].startKm, 18.298);
	QCOMPARE_EQ(scale.stretches()[1].endKm, 20.0);
	QCOMPARE_EQ(scale.stretches()[1].geoEndKm, 32.2715);
	// The geo X on both sides of the jump.
	QVERIFY(near(scale.geoAt(18.298), 18.298));
	QVERIFY(near(scale.geoAt(18.5), 30.771));
	// An end of an interval at the jump takes the geo X of its own section.
	QVERIFY(near(scale.geoAt(18.298, 17.5), 18.298));
	QVERIFY(near(scale.geoAt(18.298, 19.0), 30.569));
	QVERIFY(near(scale.geoAt(18.298 + 1e-9, 17.5), 18.298 + 1e-9));
	// 600 px for 2 km: labels every 0.2 km. 30.6 km lies 0.031 km after the jump, too close to 18.2 km before it.
	const auto labels = scale.roundLabels(17.0, 19.0, 600.0);
	// No label on each side of the jump closer than the spacing.
	QCOMPARE_EQ(scale.labelStepKm(17.0, 19.0, 600.0), 0.2);
	QVERIFY(nearAll(geoOf(labels), {17.0, 17.2, 17.4, 17.6, 17.8, 18.0, 18.2, 30.8, 31.0, 31.2}));
	QVERIFY(spaced(labels, 17.0, 19.0, 600.0));
}

void TestPositionScale::repeatsGeoXWhereRouteTurnsBack() {
	// The route turns back twice, so geo X values repeat.
	const PositionScale scale(sections({{0.0, 1.0, 0.0, 1.0}, {1.0, 2.0, 1.0, 0.0}, {2.0, 3.0, 0.0, 1.0}}));
	// A route that turns back is one stretch.
	QVERIFY(scale.jumps().empty());
	QCOMPARE(scale.stretches().size(), std::size_t(1));
	// The same geo X lies at several route distances.
	QVERIFY(near(scale.geoAt(0.5), 0.5));
	QVERIFY(near(scale.geoAt(1.5), 0.5));
	QVERIFY(near(scale.geoAt(2.5), 0.5));
	const auto labels = scale.roundLabels(0.0, 3.0, 360.0);
	// Labels repeat geo X values at increasing route distance.
	QVERIFY(nearAll(geoOf(labels), {0.0, 0.5, 1.0, 0.5, 0.0, 0.5, 1.0}));
	QVERIFY(spaced(labels, 0.0, 3.0, 360.0));
}

void TestPositionScale::extendsSectionsOutsideTheScope() {
	// Paimpol: route2's switch section starts at 10.135 km against route0's first section at 10.21 km.
	const PositionScale scale(sections({{10.21, 10.51, 47.79, 47.49}, {10.51, 11.0, 47.49, 47.0}}));
	// A route distance outside the sections extends the first or the last section.
	QVERIFY(near(scale.geoAt(10.135), 47.865));
	QVERIFY(near(scale.geoAt(11.5), 46.5));
	// An interval that sticks out maps both ends.
	QVERIFY(near(scale.geoAt(10.135, 10.51), 47.865));
	QVERIFY(near(scale.geoAt(10.51, 10.135), 47.49));
	// An empty scale has no geo X and no labels.
	QVERIFY(PositionScale().empty());
	QVERIFY(std::isnan(PositionScale().geoAt(1.0)));
	QVERIFY(PositionScale().roundLabels(0.0, 1.0, 100.0).empty());
	const double nan = std::numeric_limits<double>::quiet_NaN();
	QVERIFY2(PositionScale(buildBlockSectionExtents({{"@z@", 1.0, 1.0}, {"@n@", 1.0, nan}})).empty(),
		"sections without geometry are left out");
}

void TestPositionScale::spacesLabels() {
	const PositionScale scale(sections({{0.0, 4.0, 0.0, 4.0}, {4.0, 8.0, 4.0, 8.0}, {8.0, 12.0, 8.0, 12.0}}));
	// The step is 1, 2 or 5 times a power of ten for labels at least 60 px apart.
	QCOMPARE_EQ(scale.labelStepKm(0.0, 12.0, 900.0), 1.0);
	QCOMPARE_EQ(scale.labelStepKm(0.0, 12.0, 300.0), 5.0);
	QCOMPARE_EQ(scale.labelStepKm(0.0, 12.0, 1800.0), 0.5);
	QCOMPARE_EQ(scale.labelStepKm(3.0, 3.5, 900.0), 0.05);
	QCOMPARE_EQ(scale.labelStepKm(0.0, 12.0, 2.0), 500.0);
	const auto wide = scale.roundLabels(0.0, 12.0, 900.0);
	// A label on every kilometre at 75 px per km.
	QVERIFY(nearAll(geoOf(wide), {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}));
	QVERIFY(spaced(wide, 0.0, 12.0, 900.0));
	QVERIFY2(nearAll(geoOf(scale.roundLabels(0.0, 12.0, 300.0)), {0, 5, 10}), "every 5 km at 25 px per km");
	const auto zoomed = scale.roundLabels(3.0, 3.5, 900.0);
	// Zoomed in, the labels are the nearest doubles of their round values.
	QCOMPARE(zoomed.size(), std::size_t(11));
	QCOMPARE_EQ(zoomed[1].geoKm, 3.05);
	QCOMPARE_EQ(zoomed[10].geoKm, 3.5);
	QVERIFY(spaced(zoomed, 3.0, 3.5, 900.0));
	// No width or no range gives no labels.
	QVERIFY(scale.roundLabels(0.0, 12.0, 0.0).empty());
	QVERIFY(scale.roundLabels(5.0, 5.0, 900.0).empty());
}

void TestPositionScale::fillsExportColumns() {
	const auto extents = sections({{17.0, 18.298, 17.0, 18.298}, {18.298, 19.0, 30.569, 31.271}});
	const auto envelope = [](const char* train, const char* block, double startKm, double endKm) {
		BlockingTimeDiagramSegment segment;
		segment.trainName = train;
		segment.blockId = block;
		segment.startPositionKm = startKm;
		segment.endPositionKm = endKm;
		segment.midPositionKm = (startKm + endKm) / 2.0;
		segment.endTime = 1.0;
		return segment;
	};
	BlockingTimePlannedReference planned;
	planned.trainName = "T";
	planned.stationName = "Station";
	planned.eventType = "arrival";
	planned.time = 5.0;
	planned.positionKm = 18.5;
	const std::string text = buildBlockingTimeCsv({"T"}, {envelope("T", "@s0@", 17.0, 18.298), envelope("T", "@s1@", 18.298, 19.0)},
		{planned}, {}, extents, {envelope("T", "@other@", 3.0, 4.0)});
	const auto rows = csvRows(text);
	// The export ends with the three geo X columns.
	QCOMPARE(rows.size(), std::size_t(7));
	QCOMPARE(rows[0].size(), std::size_t(26));
	QCOMPARE(rows[0][23], "Geo position[km]");
	QCOMPARE(rows[0][24], "Start geo X[km]");
	QCOMPARE(rows[0][25], "End geo X[km]");
	// Every row is as wide as the header.
	for (const auto& row : rows)
		QCOMPARE(row.size(), std::size_t(26));
	// An envelope gives the geo X of its middle and of its ends, each end in its own section.
	QVERIFY(nearField(rows[1][23], 17.649));
	QVERIFY(nearField(rows[1][24], 17.0));
	QVERIFY(nearField(rows[1][25], 18.298));
	QVERIFY(nearField(rows[2][23], 30.92));
	QVERIFY(nearField(rows[2][24], 30.569));
	QVERIFY(nearField(rows[2][25], 31.271));
	// A planned reference gives the geo X of its position.
	QCOMPARE(rows[3][5], "planned reference");
	QVERIFY(nearField(rows[3][23], 30.771));
	QVERIFY(rows[3][24].empty());
	QVERIFY(rows[3][25].empty());
	// An envelope that is not on the reference route has no geo X.
	QCOMPARE(rows[4][21], "unmapped blocking envelope");
	QVERIFY(rows[4][23].empty());
	QVERIFY(rows[4][24].empty());
	QVERIFY(rows[4][25].empty());
	// The sections give their own geo X.
	QCOMPARE(rows[5][21], "block section extent");
	QVERIFY(nearField(rows[5][24], 17.0));
	QVERIFY(nearField(rows[5][25], 18.298));
	QVERIFY(nearField(rows[6][24], 30.569));
	QVERIFY(nearField(rows[6][25], 31.271));
	// A recorded sample gives a row as wide as the header with its position, its time and the geo X of its position.
	const auto sampled = csvRows(buildBlockingTimeCsv({"T"}, {}, {}, {BlockingTimeTrajectory{"T", {{18.5, 5.0}}}}, extents));
	QCOMPARE(sampled.size(), std::size_t(4));
	QCOMPARE(sampled[1].size(), std::size_t(26));
	QCOMPARE(sampled[1][0], "T");
	QCOMPARE(sampled[1][4], "18.5");
	QCOMPARE(sampled[1][5], "recorded sample");
	QCOMPARE(sampled[1][8], "5");
	QCOMPARE(sampled[1][21], "recorded trajectory");
	QVERIFY(nearField(sampled[1][23], 30.771));
}

QTEST_APPLESS_MAIN(TestPositionScale)
#include "test_positionscale.moc"
