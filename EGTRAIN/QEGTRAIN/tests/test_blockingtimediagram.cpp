#include "../BlockingTimeDiagram.h"

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
			block("@12-blockSets0@", 10.0, 20.0, 1000.0, 1400.0, "None", "None", true),
			block("@99-blockSets0@", -1.0, 30.0, 1400.0, 1600.0, "None", "None", true),
			block("@13-blockSets0@", 25.0, 40.0, 1600.0, 2200.0, "SW13", "None", true),
		},
		{
			block("@12-blockSets9@", 15.0, 25.0, 1000.0, 1500.0, "None", "StationA", true),
			block("@40-blockSets0@", 50.0, 45.0, 1800.0, 2100.0, "None", "None", true),
			block("@41-blockSets0@", 60.0, 70.0, 2100.0, 2300.0, "None", "None", false),
		},
	};
	std::vector<std::string> names = {"A", "B"};
	std::vector<BlockingTimeDiagramSegment> segments = buildBlockingTimeDiagramSegments(trains, names);

	bool ok = true;
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

	if (!ok)
		return 1;

	std::cout << "all BlockingTimeDiagram tests passed\n";
	return 0;
}
