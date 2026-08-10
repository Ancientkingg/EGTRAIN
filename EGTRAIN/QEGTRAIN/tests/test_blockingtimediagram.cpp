#include "diagrams/BlockingTimeDiagram.h"

#include <iostream>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

static BlockingTimeDiagramInput block(const char* id, double start, double end, double posStart, double posEnd, const char* switchName, const char* stationName, bool complete) {
	BlockingTimeDiagramInput b;
	b.blockId = id;
	b.startOccTime = start;
	b.endOccTime = end;
	b.posStart = posStart;
	b.posEnd = posEnd;
	b.switchName = switchName;
	b.stationName = stationName;
	b.isComplete = complete;
	return b;
}

int main() {
	std::vector<std::vector<BlockingTimeDiagramInput>> trains = {
		{
			block("@12-B0@", 10.0, 20.0, 1000.0, 1400.0, "None", "None", true),
			block("@99-B0@", -1.0, 30.0, 1400.0, 1600.0, "None", "None", true),
			block("@13-B0@", 25.0, 40.0, 1600.0, 2200.0, "SW13", "None", true),
		},
		{
			block("@12-B0@", 15.0, 25.0, 1000.0, 1500.0, "None", "StationA", true),
			block("@40-B0@", 50.0, 45.0, 1800.0, 2100.0, "None", "None", true),
			block("@42-B0@", 45.0, 45.0, 1900.0, 2200.0, "None", "None", true),
			block("@41-B0@", 60.0, 70.0, 2100.0, 2300.0, "None", "None", false),
		},
	};
	std::vector<std::string> names = {"A", "B"};
	std::vector<BlockingTimeDiagramSegment> segments = buildBlockingTimeDiagramSegments(trains, names);

	bool ok = true;
	ok &= expect(!shareBlockingTimeResource("2-B0", "2-B4"),
		"distinct decorated block resources do not match");
	ok &= expect(shareBlockingTimeResource("@2-B0@-1.0/@3-B1@-2.0", "2-B0"),
		"composite block matches its exact component");
	ok &= expect(segments.size() == 3, "invalid and incomplete intervals are filtered");
	if (segments.size() >= 3) {
		ok &= expect(segments[0].trainName == "A", "first segment train name");
		ok &= expect(segments[0].style == BlockingTimeSegmentStyle::Critical, "overlapping matching blocks are critical");
		ok &= expect(segments[0].startTime == 10.0, "first segment start time");
		ok &= expect(segments[0].endTime == 20.0, "first segment end time");
		ok &= expect(segments[0].midPositionKm == 1.2, "first segment midpoint");
		ok &= expect(segments[1].trainName == "A", "switch segment train name");
		ok &= expect(segments[1].style == BlockingTimeSegmentStyle::Switch, "switch segment style");
		ok &= expect(segments[1].penWidth >= 2.0, "minimum pen width");
		ok &= expect(segments[2].trainName == "B", "critical station train name");
		ok &= expect(segments[2].style == BlockingTimeSegmentStyle::CriticalStation, "critical station segment style");
		ok &= expect(segments[2].midPositionKm == 1.25, "critical station midpoint");
	}

	const std::vector<BlockingTimeDiagramSegment> scoped = filterBlockingTimeDiagramSegments(
		segments, {"A"}, {"@12-B0@"}, 12.0, 18.0);
	ok &= expect(scoped.size() == 1, "route block and train scope filters segments");
	if (!scoped.empty()) {
		ok &= expect(scoped[0].startTime == 12.0 && scoped[0].endTime == 18.0,
			"time scope clips copied segment bounds");
		ok &= expect(scoped[0].style == BlockingTimeSegmentStyle::Critical,
			"critical style survives when the conflicting train is hidden");
	}

	const std::vector<BlockingTimePlannedReference> references = {
		{"A", "Origin", "departure", 10.0, 0.0},
		{"A", "Destination", "arrival", 30.0, 2.0},
		{"B", "Other", "departure", 40.0, 0.0}};
	const auto crossing = filterBlockingTimePlannedReferences(references, 15.0, 25.0);
	ok &= expect(crossing.size() == 2 && crossing.front().time == 10.0 && crossing.back().time == 30.0,
		"planned scope retains both source points for a visible line crossing");
	ok &= expect(filterBlockingTimePlannedReferences(references, 31.0, 39.0).empty(),
		"off-window planned points do not create an empty scoped chart or export");
	const auto nearTouch = buildBlockingTimeDiagramSegments({
		{block("T", 0.0, 10.00000005, 0.0, 100.0, "None", "None", true)},
		{block("T", 10.0, 20.0, 0.0, 100.0, "None", "None", true)}}, {"A", "B"});
	ok &= expect(nearTouch.size() == 2
		&& nearTouch[0].style == BlockingTimeSegmentStyle::Default
		&& nearTouch[1].style == BlockingTimeSegmentStyle::Default,
		"sub-tolerance touching is not also styled as an overlap conflict");

	if (!ok)
		return 1;

	std::cout << "all BlockingTimeDiagram tests passed\n";
	return 0;
}
