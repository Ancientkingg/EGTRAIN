#include "diagrams/BlockingTimeDiagram.h"
#include "diagrams/CapacityAnalysis.h"
#include "scene/SectionLabels.h"
#include "util/CsvWriter.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace {

// Ends of a section and of an envelope that differ by no more are the same position.
constexpr double kBlockSectionToleranceKm = 1e-6;
// A section no longer than this has no extent.
constexpr double kBlockSectionMinimumLengthKm = 1e-9;

// Positions of the columns of the blocking-time export.
constexpr std::size_t kColumnBlock = 1;
constexpr std::size_t kColumnPosition = 4;
constexpr std::size_t kColumnSegmentType = 5;
constexpr std::size_t kColumnStartX = 9;
constexpr std::size_t kColumnEndX = 10;
constexpr std::size_t kColumnLayer = 21;
constexpr std::size_t kColumnReferenceSection = 22;
constexpr std::size_t kColumnGeoPosition = 23;
constexpr std::size_t kColumnStartGeoX = 24;
constexpr std::size_t kColumnEndGeoX = 25;

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

// A section in a note: its label and id, or its id without labels.
std::string sectionName(const std::string& id, const SceneSectionLabels* labels) {
	return labels ? labels->labelWithId(id) : id;
}

// The section of the reference route that shares a block with the envelope of another section.
std::string referenceSectionNote(const BlockSectionExtent& extent, std::size_t count, const SceneSectionLabels* labels) {
	return "Section " + std::to_string(extent.order) + " of " + std::to_string(count) + ": " + sectionName(extent.id, labels);
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
					if (shareBlockingTimeResource(trains[firstTrain][firstBlock], trains[secondTrain][secondBlock])
						&& overlaps(trains[firstTrain][firstBlock], trains[secondTrain][secondBlock])) {
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
		if ((!allowedTrainIds.empty()
				&& std::find(allowedTrainIds.begin(), allowedTrainIds.end(), source.trainName) == allowedTrainIds.end())
			|| (!allowedBlockIds.empty()
				&& std::none_of(allowedBlockIds.begin(), allowedBlockIds.end(),
					[&source](const std::string& allowedBlockId) {
						return shareBlockingTimeResource(source.blockId, allowedBlockId);
					}))
			|| source.endTime <= startTime || source.startTime >= endTime)
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
		point.positionKm += (second.positionKm - first.positionKm) * ((time - first.time) / (second.time - first.time));
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
		return std::isfinite(first) && std::isfinite(second) && std::max(first, second) >= startTime && std::min(first, second) <= endTime;
	};
	for (std::size_t i = 0; i < references.size(); ++i) {
		const BlockingTimePlannedReference& reference = references[i];
		if (!std::isfinite(reference.time))
			continue;
		const bool pointVisible = reference.time >= startTime && reference.time <= endTime;
		const bool previousVisible = i > 0 && references[i - 1].trainName == reference.trainName
			&& segmentIntersects(references[i - 1].time, reference.time);
		const bool nextVisible = i + 1 < references.size() && references[i + 1].trainName == reference.trainName
			&& segmentIntersects(reference.time, references[i + 1].time);
		if (pointVisible || previousVisible || nextVisible)
			filtered.push_back(reference);
	}
	return filtered;
}

std::vector<BlockSectionExtent> buildBlockSectionExtents(const std::vector<BlockSectionInput>& inputs) {
	std::vector<BlockSectionExtent> extents;
	extents.reserve(inputs.size());
	for (std::size_t i = 0; i < inputs.size(); ++i) {
		BlockSectionExtent extent;
		extent.order = static_cast<int>(i) + 1;
		extent.id = inputs[i].id;
		extent.startKm = inputs[i].startKm;
		extent.endKm = inputs[i].endKm;
		const bool geoKnown = std::isfinite(inputs[i].geoStartKm) && std::isfinite(inputs[i].geoEndKm);
		extent.geoStartKm = geoKnown ? inputs[i].geoStartKm : extent.startKm;
		extent.geoEndKm = geoKnown ? inputs[i].geoEndKm : extent.endKm;
		extent.isSwitch = inputs[i].isSwitch;
		if (!std::isfinite(extent.startKm) || !std::isfinite(extent.endKm))
			extent.issue = "no valid position";
		else if (std::abs(extent.endKm - extent.startKm) <= kBlockSectionMinimumLengthKm)
			extent.issue = "zero length";
		extent.geometryValid = extent.issue.empty();
		extents.push_back(std::move(extent));
	}
	return extents;
}

void matchBlockingSegmentsToSections(std::vector<BlockingTimeDiagramSegment>& segments,
	const std::vector<BlockSectionExtent>& extents, const std::string& referenceRouteId, const SceneSectionLabels* labels) {
	for (BlockingTimeDiagramSegment& segment : segments) {
		segment.referenceSection = 0;
		segment.referenceNote.clear();
		if (extents.empty() || segment.startPositionKm == segment.endPositionKm) continue;
		const double lower = std::min(segment.startPositionKm, segment.endPositionKm);
		const double upper = std::max(segment.startPositionKm, segment.endPositionKm);
		std::vector<const BlockSectionExtent*> exact, shared;
		for (const BlockSectionExtent& extent : extents) {
			if (!extent.geometryValid) continue;
			if (std::abs(extent.lowerKm() - lower) <= kBlockSectionToleranceKm
				&& std::abs(extent.upperKm() - upper) <= kBlockSectionToleranceKm && extent.id == segment.blockId)
				exact.push_back(&extent);
			if (std::min(upper, extent.upperKm()) - std::max(lower, extent.lowerKm()) > kBlockSectionToleranceKm
				&& shareBlockingTimeResource(extent.id, segment.blockId))
				shared.push_back(&extent);
		}
		const auto& candidates = exact.empty() ? shared : exact;
		if (candidates.size() == 1) {
			segment.referenceSection = candidates.front()->order;
			// The hover names the block, so a block that is the section needs no note.
			if (exact.empty()) segment.referenceNote = referenceSectionNote(*candidates.front(), extents.size(), labels);
		} else if (candidates.size() > 1) {
			segment.referenceNote = "Matches several sections of " + referenceRouteId + ": ";
			// A switch label holds a comma, so labelled sections are listed with semicolons.
			for (const auto* candidate : candidates) {
				if (candidate != candidates.front())
					segment.referenceNote += labels ? "; " : ", ";
				segment.referenceNote += std::to_string(candidate->order) + (labels ? ": " : ":") + sectionName(candidate->id, labels);
			}
		} else {
			segment.referenceNote = "Not on " + referenceRouteId;
		}
	}
}

std::vector<BlockSectionExtent> scopedBlockSectionExtents(const std::vector<BlockSectionExtent>& extents,
	const std::vector<int>& indices) {
	if (indices.empty()) return extents;
	std::vector<BlockSectionExtent> selected;
	for (int index : indices)
		if (index >= 0 && static_cast<std::size_t>(index) < extents.size()) selected.push_back(extents[index]);
	return selected;
}

std::vector<int> blockSectionRangeIndices(int first, int last, int count) {
	std::vector<int> indices;
	const int low = std::min(first, last), high = std::max(first, last);
	for (int index = low; index >= 0 && index <= high && index < count; ++index)
		indices.push_back(index);
	return indices;
}

bool blockingSegmentInSections(const BlockingTimeDiagramSegment& segment, const std::vector<BlockSectionExtent>& sections) {
	const double lower = std::min(segment.startPositionKm, segment.endPositionKm);
	const double upper = std::max(segment.startPositionKm, segment.endPositionKm);
	return std::any_of(sections.begin(), sections.end(), [&](const BlockSectionExtent& section) {
		return section.geometryValid && shareBlockingTimeResource(section.id, segment.blockId)
			&& (lower == upper ? lower >= section.lowerKm() && lower <= section.upperKm()
							   : std::min(section.upperKm(), upper) > std::max(section.lowerKm(), lower));
	});
}

bool positionInBlockSections(double positionKm, const std::vector<BlockSectionExtent>& sections) {
	return std::any_of(sections.begin(), sections.end(), [positionKm](const BlockSectionExtent& section) {
		return section.geometryValid && positionKm >= section.lowerKm() && positionKm <= section.upperKm();
	});
}

std::optional<std::pair<double, double>> blockSectionBounds(const std::vector<BlockSectionExtent>& extents) {
	std::optional<std::pair<double, double>> bounds;
	for (const auto& extent : extents) {
		if (!extent.geometryValid) continue;
		if (!bounds) bounds = {extent.lowerKm(), extent.upperKm()};
		else {
			bounds->first = std::min(bounds->first, extent.lowerKm());
			bounds->second = std::max(bounds->second, extent.upperKm());
		}
	}
	return bounds;
}

std::string blockSectionCapacityScopeIssue(const std::vector<BlockSectionExtent>& extents,
	const std::vector<int>& indices) {
	if (indices.empty()) return {};
	const auto selected = scopedBlockSectionExtents(extents, indices);
	for (const auto& section : selected)
		for (std::size_t index = 0; index < extents.size(); ++index)
			if (std::find(indices.begin(), indices.end(), static_cast<int>(index)) == indices.end()
				&& shareBlockingTimeResource(section.id, extents[index].id))
				return "A selected block is also used outside the chosen blocks, so the passes cannot be told apart. Choose other blocks.";
	for (std::size_t index = 1; index < selected.size(); ++index)
		if (shareBlockingTimeResource(selected.front().id, selected[index].id))
			return "The first block occurs more than once in the selection. Choose another first block.";
	return {};
}

bool blockingTimeEntryAmbiguous(const std::vector<BlockingTimeDiagramInput>& occupations,
	const std::string& entryBlockId) {
	return std::count_if(occupations.begin(), occupations.end(), [&entryBlockId](const auto& occupation) {
		return validBlockingTimeDiagramInput(occupation) && std::isfinite(occupation.startRunTime)
			&& occupation.startRunTime >= 0 && shareBlockingTimeResource(occupation.blockId, entryBlockId);
	}) > 1;
}

const std::vector<std::string>& blockingTimeCsvHeader() {
	static const std::vector<std::string> header = {"Train", "Block", "Occupation start[s]", "Occupation end[s]",
		"Position[km]", "Segment type", "Planned reference", "Station", "Planned time[s]", "Start X[km]", "End X[km]",
		"Original start[s]", "Original end[s]", "Original approach[s]", "Original run start[s]", "Original run end[s]",
		"Original clearance[s]", "Setup[s]", "Sight reaction[s]", "Release[s]", "Run margin[s]", "Layer", "Reference section",
		"Geo position[km]", "Start geo X[km]", "End geo X[km]"};
	return header;
}

std::vector<std::vector<std::string>> blockSectionCsvRows(const std::vector<BlockSectionExtent>& extents) {
	std::vector<std::vector<std::string>> rows;
	rows.reserve(extents.size());
	for (const BlockSectionExtent& extent : extents) {
		std::vector<std::string> row(blockingTimeCsvHeader().size());
		row[kColumnBlock] = extent.id;
		row[kColumnSegmentType] = kBlockSectionSegmentType;
		row[kColumnReferenceSection] = std::to_string(extent.order);
		if (extent.geometryValid) {
			row[kColumnPosition] = csv::formatDouble((extent.startKm + extent.endKm) / 2.0);
			row[kColumnStartX] = csv::formatDouble(extent.startKm);
			row[kColumnEndX] = csv::formatDouble(extent.endKm);
			row[kColumnGeoPosition] = csv::formatDouble((extent.geoStartKm + extent.geoEndKm) / 2.0);
			row[kColumnStartGeoX] = csv::formatDouble(extent.geoStartKm);
			row[kColumnEndGeoX] = csv::formatDouble(extent.geoEndKm);
			row[kColumnLayer] = kBlockSectionLayer;
		} else {
			row[kColumnLayer] = kBlockSectionMissingLayer;
		}
		rows.push_back(std::move(row));
	}
	return rows;
}

std::string blockSectionGeometryNote(const std::vector<BlockSectionExtent>& extents, const SceneSectionLabels* labels) {
	constexpr std::size_t kListedIds = 3;
	std::vector<std::string> ids;
	for (const BlockSectionExtent& extent : extents)
		if (!extent.geometryValid)
			ids.push_back(sectionName(extent.id, labels));
	if (ids.empty())
		return std::string();
	std::string note = "Sections not drawn (no valid length): ";
	// A switch label holds a comma, so labelled sections are listed with semicolons.
	for (std::size_t i = 0; i < ids.size() && i < kListedIds; ++i)
		note += (i > 0 ? (labels ? "; " : ", ") : "") + ids[i];
	if (ids.size() > kListedIds)
		note += " and " + std::to_string(ids.size() - kListedIds) + " more";
	return note;
}

std::string blockingTimeNote(int omitted, bool nothingPlotted, bool unplaced, const std::string& routeId) {
	std::vector<std::string> parts;
	if (omitted == 1)
		parts.push_back("1 block or stop could not be placed on " + routeId + " and is not shown.");
	else if (omitted > 1)
		parts.push_back(std::to_string(omitted) + " blocks or stops could not be placed on " + routeId + " and are not shown.");
	if (nothingPlotted)
		parts.push_back(unplaced ? "No blocking times could be placed on " + routeId + "."
								 : std::string("No complete blocking times in this section and period."));
	std::string note;
	for (const std::string& part : parts) note += (note.empty() ? "" : " ") + part;
	return note;
}
