#ifndef BLOCKINGTIMEDIAGRAM_H
#define BLOCKINGTIMEDIAGRAM_H

#include <string>
#include <vector>

constexpr double kBlockingTimeToleranceSeconds = 1e-7;

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
	bool capacityCritical = false;
	// Optional measured/calculated components; negative is the runtime sentinel.
	double startApproachTime = -1.0, startRunTime = -1.0;
	double endRunTime = -1.0, endClearTime = -1.0;
	double setupTime = -1.0, sightReactionTime = -1.0;
	double releaseTime = -1.0, runTimeMargin = -1.0;
};

struct BlockingTimeDiagramSegment {
	std::string trainName;
	std::string blockId;
	double startTime = 0.0;
	double endTime = 0.0;
	double midPositionKm = 0.0;
	double startPositionKm = 0.0;
	double endPositionKm = 0.0;
	double originalStartTime = 0.0;
	double originalEndTime = 0.0;
	double startApproachTime = -1.0, startRunTime = -1.0;
	double endRunTime = -1.0, endClearTime = -1.0;
	double setupTime = -1.0, sightReactionTime = -1.0;
	double releaseTime = -1.0, runTimeMargin = -1.0;
	BlockingTimeSegmentStyle style = BlockingTimeSegmentStyle::Default;
	bool capacityCritical = false;
};

struct CapacityCompressionRow;

// Undo the occupation's profile-based displacement, not the timetable shift.
void restoreCompressedOriginalTimes(std::vector<BlockingTimeDiagramSegment>& segments,
	const std::vector<CapacityCompressionRow>& compression);

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

// Shared resource matcher used by both diagram conflict classification and
// capacity analysis. It matches exact plain and decorated/composite components.
bool shareBlockingTimeResource(const std::string& firstBlockId, const std::string& secondBlockId);
bool shareBlockingTimeResource(const BlockingTimeDiagramInput& first,
	const BlockingTimeDiagramInput& second);
bool validBlockingTimeDiagramInput(const BlockingTimeDiagramInput& input);

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

// Each returned group is a standalone event or one clipped adjacent event pair.
// An empty station name marks an interpolated boundary, never a station event.
std::vector<std::vector<BlockingTimePlannedReference>> clipBlockingTimePlannedReferences(
	const std::vector<BlockingTimePlannedReference>& references, double startTime, double endTime);

#endif // BLOCKINGTIMEDIAGRAM_H
