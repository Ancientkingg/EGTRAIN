#include "BlockingTimeDiagram.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace {

bool hasValue(const std::string& value) {
	return !value.empty() && value != "None";
}

std::vector<std::string> comparableBlockTokens(const std::string& blockId) {
	std::vector<std::string> tokens;
	std::istringstream line(blockId);
	std::string token;
	int validTokenIndex = 0;
	while (std::getline(line, token, '@')) {
		if (token.empty())
			continue;
		size_t suffix = token.find("-blockSets");
		if (suffix != std::string::npos)
			token = token.substr(0, suffix);
		if (validTokenIndex == 0 || validTokenIndex == 2)
			tokens.push_back(token);
		validTokenIndex++;
	}
	if (tokens.empty() && hasValue(blockId))
		tokens.push_back(blockId);
	return tokens;
}

bool shareBlockId(const BlockingTimeDiagramInput& a, const BlockingTimeDiagramInput& b) {
	std::vector<std::string> aTokens = comparableBlockTokens(a.blockId);
	std::vector<std::string> bTokens = comparableBlockTokens(b.blockId);
	for (const std::string& aToken : aTokens) {
		for (const std::string& bToken : bTokens) {
			if (aToken == bToken)
				return true;
		}
	}
	return false;
}

bool validInterval(const BlockingTimeDiagramInput& block) {
	return block.isComplete &&
		hasValue(block.blockId) &&
		block.startOccTime >= 0.0 &&
		block.endOccTime > block.startOccTime &&
		block.posStart >= 0.0 &&
		block.posEnd >= 0.0;
}

bool overlaps(const BlockingTimeDiagramInput& a, const BlockingTimeDiagramInput& b) {
	return std::max(a.startOccTime, b.startOccTime) < std::min(a.endOccTime, b.endOccTime);
}

BlockingTimeSegmentStyle segmentStyle(const BlockingTimeDiagramInput& block, bool critical) {
	const bool station = hasValue(block.stationName);
	const bool sw = hasValue(block.switchName);
	if (critical)
		return station ? BlockingTimeSegmentStyle::CriticalStation : BlockingTimeSegmentStyle::Critical;
	if (sw)
		return station ? BlockingTimeSegmentStyle::SwitchStation : BlockingTimeSegmentStyle::Switch;
	return station ? BlockingTimeSegmentStyle::Station : BlockingTimeSegmentStyle::Default;
}

} // namespace

std::vector<BlockingTimeDiagramSegment> buildBlockingTimeDiagramSegments(
	const std::vector<std::vector<BlockingTimeDiagramInput>>& trains,
	const std::vector<std::string>& trainNames) {
	std::vector<std::vector<bool>> critical(trains.size());
	for (size_t trainIndex = 0; trainIndex < trains.size(); trainIndex++)
		critical[trainIndex].resize(trains[trainIndex].size(), false);

	for (size_t firstTrain = 0; firstTrain < trains.size(); firstTrain++) {
		for (size_t firstBlock = 0; firstBlock < trains[firstTrain].size(); firstBlock++) {
			if (!validInterval(trains[firstTrain][firstBlock]))
				continue;
			for (size_t secondTrain = firstTrain + 1; secondTrain < trains.size(); secondTrain++) {
				for (size_t secondBlock = 0; secondBlock < trains[secondTrain].size(); secondBlock++) {
					if (!validInterval(trains[secondTrain][secondBlock]))
						continue;
					if (shareBlockId(trains[firstTrain][firstBlock], trains[secondTrain][secondBlock]) &&
						overlaps(trains[firstTrain][firstBlock], trains[secondTrain][secondBlock])) {
						critical[firstTrain][firstBlock] = true;
						critical[secondTrain][secondBlock] = true;
					}
				}
			}
		}
	}

	std::vector<BlockingTimeDiagramSegment> segments;
	for (size_t trainIndex = 0; trainIndex < trains.size(); trainIndex++) {
		const std::string trainName = trainIndex < trainNames.size() ? trainNames[trainIndex] : "";
		for (size_t blockIndex = 0; blockIndex < trains[trainIndex].size(); blockIndex++) {
			const BlockingTimeDiagramInput& block = trains[trainIndex][blockIndex];
			if (!validInterval(block))
				continue;

			BlockingTimeDiagramSegment segment;
			segment.trainName = trainName;
			segment.blockId = block.blockId;
			segment.startTime = block.startOccTime;
			segment.endTime = block.endOccTime;
			segment.midPositionKm = ((block.posStart + block.posEnd) / 2.0) / 1000.0;
			segment.penWidth = std::max(2.0, std::abs(block.posEnd - block.posStart) / 100.0);
			segment.style = segmentStyle(block, critical[trainIndex][blockIndex]);
			segments.push_back(segment);
		}
	}
	return segments;
}
