#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/CapacityAnalysis.h"

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
		if (validTokenIndex == 0 || validTokenIndex == 2)
			tokens.push_back(token);
		validTokenIndex++;
	}
	if (tokens.empty() && hasValue(blockId))
		tokens.push_back(blockId);
	return tokens;
}

bool overlaps(const BlockingTimeDiagramInput& a, const BlockingTimeDiagramInput& b) {
	return std::max(a.startOccTime, b.startOccTime)
		< std::min(a.endOccTime, b.endOccTime) - kBlockingTimeToleranceSeconds;
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

bool shareBlockingTimeResource(const std::string& aBlockId, const std::string& bBlockId) {
	std::vector<std::string> aTokens = comparableBlockTokens(aBlockId);
	std::vector<std::string> bTokens = comparableBlockTokens(bBlockId);
	for (const std::string& aToken : aTokens) {
		for (const std::string& bToken : bTokens) {
			if (aToken == bToken)
				return true;
		}
	}
	return false;
}

bool shareBlockingTimeResource(const BlockingTimeDiagramInput& first,
	const BlockingTimeDiagramInput& second) {
	return shareBlockingTimeResource(first.blockId, second.blockId);
}

bool validBlockingTimeDiagramInput(const BlockingTimeDiagramInput& block) {
	return block.isComplete && hasValue(block.blockId) && std::isfinite(block.startOccTime)
		&& std::isfinite(block.endOccTime) && std::isfinite(block.posStart) && std::isfinite(block.posEnd)
		&& block.startOccTime >= 0.0 && block.endOccTime > block.startOccTime
		&& block.posStart >= 0.0 && block.posEnd >= 0.0;
}

std::vector<BlockingTimeDiagramSegment> buildBlockingTimeDiagramSegments(
	const std::vector<std::vector<BlockingTimeDiagramInput>>& trains,
	const std::vector<std::string>& trainNames) {
	std::vector<std::vector<bool>> critical(trains.size());
	for (size_t trainIndex = 0; trainIndex < trains.size(); trainIndex++)
		critical[trainIndex].resize(trains[trainIndex].size(), false);

	for (size_t firstTrain = 0; firstTrain < trains.size(); firstTrain++) {
		for (size_t firstBlock = 0; firstBlock < trains[firstTrain].size(); firstBlock++) {
			if (!validBlockingTimeDiagramInput(trains[firstTrain][firstBlock]))
				continue;
			for (size_t secondTrain = firstTrain + 1; secondTrain < trains.size(); secondTrain++) {
				for (size_t secondBlock = 0; secondBlock < trains[secondTrain].size(); secondBlock++) {
					if (!validBlockingTimeDiagramInput(trains[secondTrain][secondBlock]))
						continue;
					if (shareBlockingTimeResource(trains[firstTrain][firstBlock], trains[secondTrain][secondBlock]) &&
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
			if (!validBlockingTimeDiagramInput(block) || !std::isfinite(block.endClearTime)
				|| block.endClearTime < 0.0)
				continue;

			BlockingTimeDiagramSegment segment;
			segment.trainName = trainName;
			segment.blockId = block.blockId;
			segment.startTime = block.startOccTime;
			segment.endTime = block.endOccTime;
			segment.startPositionKm = block.posStart / 1000.0;
			segment.endPositionKm = block.posEnd / 1000.0;
			segment.midPositionKm = (segment.startPositionKm + segment.endPositionKm) / 2.0;
			segment.originalStartTime = segment.startTime;
			segment.originalEndTime = segment.endTime;
			segment.startApproachTime = block.startApproachTime;
			segment.startRunTime = block.startRunTime;
			segment.endRunTime = block.endRunTime;
			segment.endClearTime = block.endClearTime;
			segment.setupTime = block.setupTime;
			segment.sightReactionTime = block.sightReactionTime;
			segment.releaseTime = block.releaseTime;
			segment.runTimeMargin = block.runTimeMargin;
			segment.style = segmentStyle(block, critical[trainIndex][blockIndex]);
			segment.capacityCritical = block.capacityCritical;
			segments.push_back(segment);
		}
	}
	return segments;
}

void restoreCompressedOriginalTimes(std::vector<BlockingTimeDiagramSegment>& segments,
	const std::vector<CapacityCompressionRow>& compression) {
	for (auto& segment : segments)
		for (const auto& row : compression)
			if (row.identity == segment.trainName) {
				const double displacement = row.compressedReference - row.originalReference;
				segment.originalStartTime -= displacement;
				segment.originalEndTime -= displacement;
				break;
			}
}

std::vector<BlockingTimeDiagramSegment> filterBlockingTimeDiagramSegments(
	const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::vector<std::string>& allowedTrainIds,
	const std::vector<std::string>& allowedBlockIds,
	double startTime,
	double endTime) {
	std::vector<BlockingTimeDiagramSegment> filtered;
	if (!std::isfinite(startTime) || !std::isfinite(endTime) || endTime < startTime)
		return filtered;

	for (const BlockingTimeDiagramSegment& source : segments) {
		if ((!allowedTrainIds.empty() &&
			 std::find(allowedTrainIds.begin(), allowedTrainIds.end(), source.trainName) == allowedTrainIds.end()) ||
			(!allowedBlockIds.empty() && std::none_of(allowedBlockIds.begin(), allowedBlockIds.end(),
				[&source](const std::string& allowedBlockId) {
					return shareBlockingTimeResource(source.blockId, allowedBlockId);
				})) ||
			source.endTime <= startTime || source.startTime >= endTime)
			continue;

		BlockingTimeDiagramSegment segment = source;
		segment.startTime = std::max(segment.startTime, startTime);
		segment.endTime = std::min(segment.endTime, endTime);
		if (segment.endTime > segment.startTime)
			filtered.push_back(std::move(segment));
	}
	return filtered;
}

std::vector<std::vector<BlockingTimePlannedReference>> clipBlockingTimePlannedReferences(
	const std::vector<BlockingTimePlannedReference>& references, double startTime, double endTime) {
	std::vector<std::vector<BlockingTimePlannedReference>> clipped;
	if (!std::isfinite(startTime) || !std::isfinite(endTime) || endTime < startTime)
		return clipped;
	const auto valid = [](const BlockingTimePlannedReference& point) {
		return std::isfinite(point.time) && std::isfinite(point.positionKm);
	};
	const auto boundary = [](const BlockingTimePlannedReference& first,
		const BlockingTimePlannedReference& second, double time) {
		BlockingTimePlannedReference point = first;
		point.time = time;
		point.positionKm += (second.positionKm - first.positionKm) *
			((time - first.time) / (second.time - first.time));
		point.stationName.clear();
		point.eventType = "clipped planned interpolation";
		return point;
	};
	for (std::size_t i = 0; i < references.size(); ++i) {
		const auto& first = references[i];
		if (!valid(first)) continue;
		bool connected = false;
		if (i + 1 < references.size()) {
			const auto& second = references[i + 1];
			if (first.trainName == second.trainName && valid(second) && first.time != second.time) {
				const double from = std::max(startTime, std::min(first.time, second.time));
				const double to = std::min(endTime, std::max(first.time, second.time));
				if (from <= to) {
					const double begin = first.time < second.time ? from : to;
					const double finish = first.time < second.time ? to : from;
					clipped.push_back({begin == first.time ? first : boundary(first, second, begin),
						finish == second.time ? second : boundary(first, second, finish)});
					connected = true;
				}
			}
		}
		const bool previousConnected = i > 0 && references[i - 1].trainName == first.trainName
			&& valid(references[i - 1]) && references[i - 1].time != first.time
			&& std::min(references[i - 1].time, first.time) <= endTime
			&& std::max(references[i - 1].time, first.time) >= startTime;
		if (!connected && !previousConnected && first.time >= startTime && first.time <= endTime)
			clipped.push_back({first});
	}
	return clipped;
}

std::vector<BlockingTimePlannedReference> filterBlockingTimePlannedReferences(
	const std::vector<BlockingTimePlannedReference>& references,
	double startTime,
	double endTime) {
	std::vector<BlockingTimePlannedReference> filtered;
	if (!std::isfinite(startTime) || !std::isfinite(endTime) || endTime < startTime)
		return filtered;

	const auto segmentIntersects = [startTime, endTime](double first, double second) {
		return std::isfinite(first) && std::isfinite(second) &&
			std::max(first, second) >= startTime && std::min(first, second) <= endTime;
	};
	for (std::size_t i = 0; i < references.size(); ++i) {
		const BlockingTimePlannedReference& reference = references[i];
		if (!std::isfinite(reference.time))
			continue;
		const bool pointVisible = reference.time >= startTime && reference.time <= endTime;
		const bool previousVisible = i > 0 && references[i - 1].trainName == reference.trainName &&
			segmentIntersects(references[i - 1].time, reference.time);
		const bool nextVisible = i + 1 < references.size() && references[i + 1].trainName == reference.trainName &&
			segmentIntersects(reference.time, references[i + 1].time);
		if (pointVisible || previousVisible || nextVisible)
			filtered.push_back(reference);
	}
	return filtered;
}
