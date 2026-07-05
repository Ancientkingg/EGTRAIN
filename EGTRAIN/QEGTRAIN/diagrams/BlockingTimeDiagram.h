#ifndef BLOCKINGTIMEDIAGRAM_H
#define BLOCKINGTIMEDIAGRAM_H

#include <string>
#include <vector>

enum class BlockingTimeSegmentStyle {
	Default,
	Station,
	Switch,
	SwitchStation,
	Critical,
	CriticalStation
};

struct BlockingTimeDiagramInput {
	std::string blockId;
	double startOccTime = -1.0;
	double endOccTime = -1.0;
	double posStart = -1.0;
	double posEnd = -1.0;
	std::string switchName = "None";
	std::string stationName = "None";
	bool isComplete = false;
};

struct BlockingTimeDiagramSegment {
	std::string trainName;
	std::string blockId;
	double startTime = 0.0;
	double endTime = 0.0;
	double midPositionKm = 0.0;
	double penWidth = 2.0;
	BlockingTimeSegmentStyle style = BlockingTimeSegmentStyle::Default;
};

std::vector<BlockingTimeDiagramSegment> buildBlockingTimeDiagramSegments(
	const std::vector<std::vector<BlockingTimeDiagramInput>>& trains,
	const std::vector<std::string>& trainNames);

#endif // BLOCKINGTIMEDIAGRAM_H
