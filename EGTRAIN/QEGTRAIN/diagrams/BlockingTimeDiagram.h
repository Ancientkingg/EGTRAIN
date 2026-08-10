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

struct BlockingTimePlannedReference {
	std::string trainName;
	std::string stationName;
	std::string eventType;
	double time = 0.0;
	double positionKm = 0.0;
};

std::vector<BlockingTimeDiagramSegment> buildBlockingTimeDiagramSegments(
	const std::vector<std::vector<BlockingTimeDiagramInput>>& trains,
	const std::vector<std::string>& trainNames);

// Return copies of already-classified occupation segments in the selected
// train/block/time scope. Empty train or block lists mean no restriction.
std::vector<BlockingTimeDiagramSegment> filterBlockingTimeDiagramSegments(
	const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::vector<std::string>& allowedTrainIds,
	const std::vector<std::string>& allowedBlockIds,
	double startTime,
	double endTime);

// Keep planned source points that fall in the time window or form a visible
// line segment across it. Input points for each train must be contiguous.
std::vector<BlockingTimePlannedReference> filterBlockingTimePlannedReferences(
	const std::vector<BlockingTimePlannedReference>& references,
	double startTime,
	double endTime);

#endif // BLOCKINGTIMEDIAGRAM_H
