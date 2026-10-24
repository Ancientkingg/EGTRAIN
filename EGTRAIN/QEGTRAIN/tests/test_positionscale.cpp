// Tests of the position scale of diagrams/PositionScale.h, which maps the route distance of a blocking-time diagram to
// the geo X of the scene, and of the geo X columns of the blocking-time export that it fills.

#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/PositionScale.h"
#include "diagrams/RunExports.h"

#include <QStringList>

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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

bool forwardRoute() {
	bool ok = true;
	// In a second display region: the geo X runs 100 km ahead of the route distance.
	const PositionScale scale(sections({{0.0, 2.0, 100.0, 102.0}, {2.0, 4.0, 102.0, 104.0}}));
	ok &= expect(!scale.empty() && scale.jumps().empty() && scale.stretches().size() == 1, "a forward route is one stretch");
	if (scale.stretches().size() == 1) {
		const PositionScale::Stretch& stretch = scale.stretches().front();
		ok &= expect(stretch.startKm == 0.0 && stretch.endKm == 4.0 && stretch.geoStartKm == 100.0 && stretch.geoEndKm == 104.0,
			"the stretch spans the route from the first to the last section");
	}
	ok &= expect(near(scale.geoAt(0.0), 100.0) && near(scale.geoAt(1.25), 101.25) && near(scale.geoAt(3.0), 103.0)
			&& near(scale.geoAt(4.0), 104.0),
		"a forward route maps the route distance linearly within each section");
	return ok;
}

bool againstTheCoordinate() {
	bool ok = true;
	// Like Paimpol route1, which runs from 36.5 km to 34.5 km in geo X.
	const PositionScale scale(sections({{0.0, 1.2, 36.5, 35.3}, {1.2, 2.0, 35.3, 34.5}}));
	ok &= expect(scale.jumps().empty() && scale.stretches().size() == 1 && scale.stretches().front().geoStartKm == 36.5
			&& scale.stretches().front().geoEndKm == 34.5,
		"a route against the coordinate is one stretch that counts down");
	ok &= expect(near(scale.geoAt(0.5), 36.0) && near(scale.geoAt(1.6), 34.9), "the geo X counts down along the route");
	// 300 px for 2 km: labels 0.4 km apart at least, so every 0.5 km.
	const auto labels = scale.roundLabels(0.0, 2.0, 300.0);
	ok &= expect(nearAll(geoOf(labels), {36.5, 36.0, 35.5, 35.0, 34.5}) && spaced(labels, 0.0, 2.0, 300.0),
		"the labels count down at increasing route distance");
	return ok;
}

bool jump() {
	bool ok = true;
	// Netherlands route0: 18.298 km continues at 30.569 km. The next section starts half a metre on, which is no jump.
	const PositionScale scale(sections({{17.0, 18.298, 17.0, 18.298}, {18.298, 19.0, 30.569, 31.271}, {19.0, 20.0, 31.2715, 32.2715}}));
	ok &= expect(scale.jumps().size() == 1, "one jump");
	if (scale.jumps().size() == 1) {
		const PositionScale::Jump& jump = scale.jumps().front();
		ok &= expect(jump.routeKm == 18.298 && jump.geoBeforeKm == 18.298 && jump.geoAfterKm == 30.569,
			"the jump gives its route distance and the geo X on both sides");
	}
	ok &= expect(scale.stretches().size() == 2 && scale.stretches()[0].endKm == 18.298 && scale.stretches()[1].startKm == 18.298
			&& scale.stretches()[1].endKm == 20.0 && scale.stretches()[1].geoEndKm == 32.2715,
		"the jump splits the route into two stretches");
	ok &= expect(near(scale.geoAt(18.298), 18.298) && near(scale.geoAt(18.5), 30.771), "the geo X on both sides of the jump");
	ok &= expect(near(scale.geoAt(18.298, 17.5), 18.298) && near(scale.geoAt(18.298, 19.0), 30.569)
			&& near(scale.geoAt(18.298 + 1e-9, 17.5), 18.298 + 1e-9),
		"an end of an interval at the jump takes the geo X of its own section");
	// 600 px for 2 km: labels every 0.2 km. 30.6 km lies 0.031 km after the jump, too close to 18.2 km before it.
	const auto labels = scale.roundLabels(17.0, 19.0, 600.0);
	ok &= expect(scale.labelStepKm(17.0, 19.0, 600.0) == 0.2
			&& nearAll(geoOf(labels), {17.0, 17.2, 17.4, 17.6, 17.8, 18.0, 18.2, 30.8, 31.0, 31.2})
			&& spaced(labels, 17.0, 19.0, 600.0),
		"no label on each side of the jump closer than the spacing");
	return ok;
}

bool zigZag() {
	bool ok = true;
	// The route turns back twice, so geo X values repeat.
	const PositionScale scale(sections({{0.0, 1.0, 0.0, 1.0}, {1.0, 2.0, 1.0, 0.0}, {2.0, 3.0, 0.0, 1.0}}));
	ok &= expect(scale.jumps().empty() && scale.stretches().size() == 1, "a route that turns back is one stretch");
	ok &= expect(near(scale.geoAt(0.5), 0.5) && near(scale.geoAt(1.5), 0.5) && near(scale.geoAt(2.5), 0.5),
		"the same geo X lies at several route distances");
	const auto labels = scale.roundLabels(0.0, 3.0, 360.0);
	ok &= expect(nearAll(geoOf(labels), {0.0, 0.5, 1.0, 0.5, 0.0, 0.5, 1.0}) && spaced(labels, 0.0, 3.0, 360.0),
		"labels repeat geo X values at increasing route distance");
	return ok;
}

bool outsideTheScope() {
	bool ok = true;
	// Paimpol: route2's switch section starts at 10.135 km against route0's first section at 10.21 km.
	const PositionScale scale(sections({{10.21, 10.51, 47.79, 47.49}, {10.51, 11.0, 47.49, 47.0}}));
	ok &= expect(near(scale.geoAt(10.135), 47.865) && near(scale.geoAt(11.5), 46.5),
		"a route distance outside the sections extends the first or the last section");
	ok &= expect(near(scale.geoAt(10.135, 10.51), 47.865) && near(scale.geoAt(10.51, 10.135), 47.49),
		"an interval that sticks out maps both ends");
	ok &= expect(PositionScale().empty() && std::isnan(PositionScale().geoAt(1.0)) && PositionScale().roundLabels(0.0, 1.0, 100.0).empty(),
		"an empty scale has no geo X and no labels");
	const double nan = std::numeric_limits<double>::quiet_NaN();
	ok &= expect(PositionScale(buildBlockSectionExtents({{"@z@", 1.0, 1.0}, {"@n@", 1.0, nan}})).empty(),
		"sections without geometry are left out");
	return ok;
}

bool labelSpacing() {
	bool ok = true;
	const PositionScale scale(sections({{0.0, 4.0, 0.0, 4.0}, {4.0, 8.0, 4.0, 8.0}, {8.0, 12.0, 8.0, 12.0}}));
	ok &= expect(scale.labelStepKm(0.0, 12.0, 900.0) == 1.0 && scale.labelStepKm(0.0, 12.0, 300.0) == 5.0
			&& scale.labelStepKm(0.0, 12.0, 1800.0) == 0.5 && scale.labelStepKm(3.0, 3.5, 900.0) == 0.05
			&& scale.labelStepKm(0.0, 12.0, 2.0) == 500.0,
		"the step is 1, 2 or 5 times a power of ten for labels at least 60 px apart");
	const auto wide = scale.roundLabels(0.0, 12.0, 900.0);
	ok &= expect(nearAll(geoOf(wide), {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}) && spaced(wide, 0.0, 12.0, 900.0),
		"a label on every kilometre at 75 px per km");
	ok &= expect(nearAll(geoOf(scale.roundLabels(0.0, 12.0, 300.0)), {0, 5, 10}), "every 5 km at 25 px per km");
	const auto zoomed = scale.roundLabels(3.0, 3.5, 900.0);
	ok &= expect(zoomed.size() == 11 && zoomed[1].geoKm == 3.05 && zoomed[10].geoKm == 3.5 && spaced(zoomed, 3.0, 3.5, 900.0),
		"zoomed in, the labels are the nearest doubles of their round values");
	ok &= expect(scale.roundLabels(0.0, 12.0, 0.0).empty() && scale.roundLabels(5.0, 5.0, 900.0).empty(),
		"no width or no range gives no labels");
	return ok;
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

bool exportColumns() {
	bool ok = true;
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
	ok &= expect(rows.size() == 7 && rows[0].size() == 26 && rows[0][23] == "Geo position[km]" && rows[0][24] == "Start geo X[km]"
			&& rows[0][25] == "End geo X[km]",
		"the export ends with the three geo X columns");
	if (rows.size() != 7)
		return false;
	for (const auto& row : rows)
		ok &= expect(row.size() == 26, "every row is as wide as the header");
	if (!ok)
		return false;
	ok &= expect(nearField(rows[1][23], 17.649) && nearField(rows[1][24], 17.0) && nearField(rows[1][25], 18.298)
			&& nearField(rows[2][23], 30.92) && nearField(rows[2][24], 30.569) && nearField(rows[2][25], 31.271),
		"an envelope gives the geo X of its middle and of its ends, each end in its own section");
	ok &= expect(rows[3][5] == "planned reference" && nearField(rows[3][23], 30.771) && rows[3][24].empty() && rows[3][25].empty(),
		"a planned reference gives the geo X of its position");
	ok &= expect(rows[4][21] == "unmapped blocking envelope" && rows[4][23].empty() && rows[4][24].empty() && rows[4][25].empty(),
		"an envelope that is not on the reference route has no geo X");
	ok &= expect(rows[5][21] == "block section extent" && nearField(rows[5][24], 17.0) && nearField(rows[5][25], 18.298)
			&& nearField(rows[6][24], 30.569) && nearField(rows[6][25], 31.271),
		"the sections give their own geo X");
	return ok;
}

} // namespace

int main() {
	bool ok = true;
	ok &= forwardRoute();
	ok &= againstTheCoordinate();
	ok &= jump();
	ok &= zigZag();
	ok &= outsideTheScope();
	ok &= labelSpacing();
	ok &= exportColumns();
	if (!ok)
		return 1;
	std::cout << "position scale tests passed\n";
	return 0;
}
