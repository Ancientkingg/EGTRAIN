#include "diagrams/RunCsv.h"

#include "diagrams/BlockingTimeScope.h"
#include "diagrams/PositionScale.h"
#include "util/CsvWriter.h"
#include "util/TrajectoryUtil.h"

#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>

namespace {

// A train exports when it is in the visible set. An empty set means the user has
// hidden every train, so nothing is exported.
bool trainInVisibleSet(const QStringList& visibleTrainIds, const std::string& trainId) {
	return visibleTrainIds.contains(QString::fromStdString(trainId));
}

std::string trajectoryDocument(const std::vector<std::vector<std::string>>& rows) {
	if (rows.empty())
		return std::string();
	return csv::makeDocument(
		{"Train", "Operating code", "Service ID", "Occurrence", "Time[s]", "Position[m]", "Speed[m/s]", "Power[kW]", "Tractive effort[kN]", "Energy[kWh]", "Block"}, rows);
}

std::string timetableDocument(const std::vector<TimetableResultRow>& results, const QStringList& visibleTrainIds) {
	std::vector<std::vector<std::string>> rows;
	for (const TimetableResultRow& r : results) {
		if (!trainInVisibleSet(visibleTrainIds, r.trainId))
			continue;
		rows.push_back({r.trainId,
			r.stationId,
			std::to_string(r.journeyIndex),
			r.operatingCode,
			csvValue(r.plannedArrivalSeconds),
			csvValue(r.plannedDepartureSeconds),
			csvValue(r.simulatedArrivalSeconds),
			csvValue(r.simulatedDepartureSeconds),
			csvValue(r.arrivalDelaySeconds),
			csvValue(r.departureDelaySeconds)});
	}
	if (rows.empty())
		return std::string();
	return csv::makeDocument(
		{"Train", "Station", "Journey order", "Operating code", "Planned arrival[s]", "Planned departure[s]",
			"Simulated arrival[s]", "Simulated departure[s]", "Arrival delay[s]", "Departure delay[s]"},
		rows);
}

} // namespace

std::string csvValue(const RunResultValue& value) {
	return value.available ? csv::formatDouble(value.value) : std::string(csv::kMissingValue);
}

// Blocking-time: keep the occupation-column prefix and append planned
// reference rows so the visible dashed layer is exportable too. Envelopes that
// could not be projected and the sections of the reference route follow as rows
// of their own. Physical sections remain exportable with no visible trains.
std::string buildBlockingTimeCsv(const QStringList& visibleTrainIds,
	const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::vector<BlockingTimePlannedReference>& plannedReferences,
	const std::vector<BlockingTimeTrajectory>& trajectories,
	const std::vector<BlockSectionExtent>& extents,
	const std::vector<BlockingTimeDiagramSegment>& unmapped) {
	const auto optional = [](double value) {
		return std::isfinite(value) && value >= 0.0 ? csv::formatDouble(value) : std::string();
	};
	const PositionScale scale(extents);
	// The coordinates of an unmapped envelope belong to the route of its own train, so they are left out.
	const auto envelopeRow = [&optional, &scale](const BlockingTimeDiagramSegment& s, bool mapped) {
		const auto coordinate = [mapped](double value) { return mapped ? csv::formatDouble(value) : std::string(); };
		std::vector<std::string> row(BlockingTimeCsv::kColumnCount);
		row[BlockingTimeCsv::kTrain] = s.trainName;
		row[BlockingTimeCsv::kBlock] = s.blockId;
		row[BlockingTimeCsv::kOccupationStart] = csv::formatDouble(s.startTime);
		row[BlockingTimeCsv::kOccupationEnd] = csv::formatDouble(s.endTime);
		row[BlockingTimeCsv::kPosition] = coordinate(s.midPositionKm);
		row[BlockingTimeCsv::kSegmentType] = blockingSegmentTypeName(s);
		row[BlockingTimeCsv::kStartX] = coordinate(s.startPositionKm);
		row[BlockingTimeCsv::kEndX] = coordinate(s.endPositionKm);
		row[BlockingTimeCsv::kOriginalStart] = csv::formatDouble(s.originalStartTime);
		row[BlockingTimeCsv::kOriginalEnd] = csv::formatDouble(s.originalEndTime);
		row[BlockingTimeCsv::kOriginalApproach] = optional(s.startApproachTime);
		row[BlockingTimeCsv::kOriginalRunStart] = optional(s.startRunTime);
		row[BlockingTimeCsv::kOriginalRunEnd] = optional(s.endRunTime);
		row[BlockingTimeCsv::kOriginalClearance] = optional(s.endClearTime);
		row[BlockingTimeCsv::kSetup] = optional(s.setupTime);
		row[BlockingTimeCsv::kSightReaction] = optional(s.sightReactionTime);
		row[BlockingTimeCsv::kRelease] = optional(s.releaseTime);
		row[BlockingTimeCsv::kRunMargin] = optional(s.runTimeMargin);
		row[BlockingTimeCsv::kLayer] = mapped ? std::string("calculated blocking envelope") : std::string(kUnmappedEnvelopeLayer);
		row[BlockingTimeCsv::kReferenceSection] = mapped && s.referenceSection > 0 ? std::to_string(s.referenceSection) : std::string();
		row[BlockingTimeCsv::kGeoPosition] = coordinate(scale.geoAt(s.midPositionKm));
		row[BlockingTimeCsv::kStartGeoX] = coordinate(scale.geoAt(s.startPositionKm, s.endPositionKm));
		row[BlockingTimeCsv::kEndGeoX] = coordinate(scale.geoAt(s.endPositionKm, s.startPositionKm));
		return row;
	};
	std::vector<std::vector<std::string>> rows;
	for (const BlockingTimeDiagramSegment& s : segments) {
		if (!trainInVisibleSet(visibleTrainIds, s.trainName))
			continue;
		rows.push_back(envelopeRow(s, true));
	}
	for (const BlockingTimePlannedReference& reference : plannedReferences) {
		if (!trainInVisibleSet(visibleTrainIds, reference.trainName))
			continue;
		std::vector<std::string> row(BlockingTimeCsv::kColumnCount);
		row[BlockingTimeCsv::kTrain] = reference.trainName;
		row[BlockingTimeCsv::kPosition] = csv::formatDouble(reference.positionKm);
		row[BlockingTimeCsv::kSegmentType] = "planned reference";
		row[BlockingTimeCsv::kPlannedReference] = reference.eventType;
		row[BlockingTimeCsv::kStation] = reference.stationName;
		row[BlockingTimeCsv::kPlannedTime] = csv::formatDouble(reference.time);
		row[BlockingTimeCsv::kLayer] = "planned station event";
		row[BlockingTimeCsv::kGeoPosition] = csv::formatDouble(scale.geoAt(reference.positionKm));
		rows.push_back(std::move(row));
	}
	for (const BlockingTimeTrajectory& trajectory : trajectories) {
		if (!trainInVisibleSet(visibleTrainIds, trajectory.trainId))
			continue;
		for (const auto& [positionKm, seconds] : trajectory.points) {
			std::vector<std::string> row(BlockingTimeCsv::kColumnCount);
			row[BlockingTimeCsv::kTrain] = trajectory.trainId;
			row[BlockingTimeCsv::kPosition] = csv::formatDouble(positionKm);
			row[BlockingTimeCsv::kSegmentType] = "recorded sample";
			row[BlockingTimeCsv::kPlannedTime] = csv::formatDouble(seconds);
			row[BlockingTimeCsv::kLayer] = "recorded trajectory";
			row[BlockingTimeCsv::kGeoPosition] = csv::formatDouble(scale.geoAt(positionKm));
			rows.push_back(std::move(row));
		}
	}
	for (const BlockingTimeDiagramSegment& s : unmapped)
		if (trainInVisibleSet(visibleTrainIds, s.trainName)) rows.push_back(envelopeRow(s, false));
	for (auto& row : blockSectionCsvRows(extents))
		rows.push_back(std::move(row));
	for (auto& row : rows)
		row.resize(blockingTimeCsvHeader().size());
	return rows.empty() ? std::string() : csv::makeDocument(blockingTimeCsvHeader(), rows);
}

QStringList trainIdsFrom(const CompletedRunData& data) {
	QStringList ids;
	for (const CompletedTrain& train : data.trains)
		ids.append(QString::fromStdString(train.trainId));
	return ids;
}

std::string trajectoryCsvFrom(const CompletedRunData& data, const QStringList& visibleTrainIds) {
	const double timestepSeconds = data.settings.timestepSeconds;
	const auto cell = [](const std::optional<double>& value) {
		return value ? csv::formatDouble(*value) : std::string(csv::kMissingValue);
	};
	const auto scaledCell = [](const std::optional<double>& value, auto convert) {
		return value ? csv::formatDouble(convert(*value)) : std::string(csv::kMissingValue);
	};
	std::vector<std::vector<std::string>> rows;
	for (const CompletedTrain& train : data.trains) {
		if (!trainInVisibleSet(visibleTrainIds, train.trainId))
			continue;
		const CompletedTrajectory& trajectory = train.trajectory;
		for (const TrajectorySegment& segment : trajectory.segments) {
			for (int i = segment.first; i <= segment.last; ++i) {
				const std::uint32_t block = trajectory.blockAt(i);
				rows.push_back({train.trainId,
					train.operatingCode,
					train.serviceId,
					std::to_string(train.serviceOccurrence),
					csv::formatDouble(i * timestepSeconds),
					cell(trajectory.at(trajectory.position, i)),
					cell(trajectory.at(trajectory.speed, i)),
					scaledCell(trajectory.at(trajectory.power, i), [](double watts) { return watts / 1000.0; }),
					scaledCell(trajectory.at(trajectory.effort, i), [](double newtons) { return newtons / 1000.0; }),
					scaledCell(trajectory.at(trajectory.energy, i), [](double megajoules) { return megajoules * kEnergyMJToKWh; }),
					block == CompletedTrajectory::kNoBlock ? std::string(csv::kMissingValue) : data.blockNames[block]});
			}
		}
	}
	return trajectoryDocument(rows);
}

std::string timetableCsvFrom(const CompletedRunData& data, const QStringList& visibleTrainIds) {
	return timetableDocument(data.timetable, visibleTrainIds);
}

std::function<std::string(const QStringList&)> csvProviderFrom(std::shared_ptr<const CompletedRunData> run,
	CompletedRunCsv csv) {
	return [run = std::move(run), csv](const QStringList& visibleTrainIds) { return csv(*run, visibleTrainIds); };
}

std::string capacityEvidenceText(const std::vector<CapacityHeadwayEvidence>& evidence, const SceneSectionLabels* labels) {
	QStringList values;
	for (const auto& item : evidence)
		values << QString(labels ? "%1 / %2: leader offset %3 s, follower offset %4 s, headway %5 s" : "%1/%2 (leader=%3 s; follower=%4 s; candidate=%5 s)")
					  .arg(QString::fromStdString(labels ? readableSectionLabel(item.leaderBlockId, *labels) : item.leaderBlockId),
						  QString::fromStdString(labels ? readableSectionLabel(item.followerBlockId, *labels) : item.followerBlockId),
						  QString::fromStdString(csv::formatDouble(item.leaderOffset)),
						  QString::fromStdString(csv::formatDouble(item.followerOffset)),
						  QString::fromStdString(csv::formatDouble(item.candidateHeadway)));
	return values.join(labels ? "\n" : "; ").toStdString();
}

std::string capacityEvidenceBlocks(const std::vector<CapacityHeadwayEvidence>& evidence) {
	QStringList values;
	for (const auto& item : evidence)
		values << QString::fromStdString(shortBlockId(item.leaderBlockId) + " / " + shortBlockId(item.followerBlockId));
	return values.join(", ").toStdString();
}

std::string capacityAnalysisCsv(const CapacityAnalysisResult& result, const QString& sectionLabel) {
	if (!result.analyzable || result.cycleEndIdentity.empty() || !std::isfinite(result.cycleTime)
		|| result.cycleTime < 0.0 || !std::isfinite(result.cyclePercentage))
		return std::string();
	enum Column : std::size_t {
		kRecordType,
		kLeader,
		kFollower,
		kIdentity,
		kOperatingCode,
		kOriginalReference,
		kScheduledReference,
		kCompressedReference,
		kShift,
		kScheduledHeadway,
		kMinimumHeadway,
		kBuffer,
		kBlock,
		kLeaderOffset,
		kFollowerOffset,
		kCandidateHeadway,
		kGap,
		kCycleTime,
		kPeriod,
		kCyclePercentage,
		kSection,
		kReferenceLabel,
		kReferenceSource,
		kNotes,
		kColumnCount
	};
	const char* const names[] = {
		"Record type", "Leader", "Follower", "Identity", "Operating code", "Original reference[s]",
		"Scheduled reference[s]", "Compressed reference[s]", "Shift[s]", "Scheduled headway[s]",
		"Minimum headway[s]", "Buffer[s]", "Block", "Leader offset[s]", "Follower offset[s]",
		"Candidate headway[s]", "Gap[s]", "Cycle time[s]", "Period[s]", "Cycle / period [%]",
		"Section", "Reference label", "Reference source", "Notes"};
	static_assert(std::size(names) == kColumnCount, "one header name for each column");
	const std::vector<std::string> header(std::begin(names), std::end(names));
	std::vector<std::vector<std::string>> rows;
	const auto referenceSource = [&result](const std::string& identity) {
		for (std::size_t index = 0; index < result.trainIdentities.size(); ++index)
			if (result.trainIdentities[index] == identity && index < result.referenceSources.size())
				return result.referenceSources[index];
		return std::string();
	};
	const auto referenceLabel = [&result](const std::string& identity) {
		for (std::size_t index = 0; index < result.trainIdentities.size(); ++index)
			if (result.trainIdentities[index] == identity && index < result.referenceLabels.size())
				return result.referenceLabels[index];
		return std::string();
	};
	std::vector<std::string> summary(kColumnCount);
	summary[kRecordType] = "summary";
	summary[kLeader] = result.firstIdentity;
	summary[kFollower] = result.cycleEndIdentity;
	summary[kCycleTime] = csv::formatDouble(result.cycleTime);
	summary[kPeriod] = csv::formatDouble(result.periodSeconds);
	summary[kCyclePercentage] = csv::formatDouble(result.cyclePercentage);
	summary[kSection] = sectionLabel.toStdString();
	summary[kReferenceLabel] = referenceLabel(result.firstIdentity) + " -> " + referenceLabel(result.cycleEndIdentity);
	summary[kReferenceSource] = referenceSource(result.firstIdentity) + " -> " + referenceSource(result.cycleEndIdentity);
	summary[kNotes] = "explicit cycle start and next-period closing occurrence";
	rows.push_back(std::move(summary));
	for (const auto& pair : result.allPairs) {
		if (pair.governingEvidence.empty()) {
			std::vector<std::string> record(kColumnCount);
			record[kRecordType] = "pair";
			record[kLeader] = pair.leaderIdentity;
			record[kFollower] = pair.followerIdentity;
			record[kScheduledHeadway] = csv::formatDouble(pair.scheduledHeadway);
			record[kMinimumHeadway] = csv::formatDouble(pair.minimumHeadway);
			record[kBuffer] = csv::formatDouble(pair.buffer);
			record[kSection] = sectionLabel.toStdString();
			record[kReferenceLabel] = referenceLabel(pair.leaderIdentity) + " -> " + referenceLabel(pair.followerIdentity);
			record[kReferenceSource] = referenceSource(pair.leaderIdentity) + " -> " + referenceSource(pair.followerIdentity);
			record[kNotes] = pair.adjacent ? "no shared constraint" : "nonadjacent; no shared constraint";
			rows.push_back(std::move(record));
			continue;
		}
		for (const auto& evidence : pair.governingEvidence) {
			std::vector<std::string> record(kColumnCount);
			record[kRecordType] = "pair";
			record[kLeader] = pair.leaderIdentity;
			record[kFollower] = pair.followerIdentity;
			record[kScheduledHeadway] = csv::formatDouble(pair.scheduledHeadway);
			record[kMinimumHeadway] = csv::formatDouble(pair.minimumHeadway);
			record[kBuffer] = csv::formatDouble(pair.buffer);
			record[kBlock] = evidence.leaderBlockId + " / " + evidence.followerBlockId;
			record[kLeaderOffset] = csv::formatDouble(evidence.leaderOffset);
			record[kFollowerOffset] = csv::formatDouble(evidence.followerOffset);
			record[kCandidateHeadway] = csv::formatDouble(evidence.candidateHeadway);
			record[kSection] = sectionLabel.toStdString();
			record[kReferenceLabel] = referenceLabel(pair.leaderIdentity) + " -> " + referenceLabel(pair.followerIdentity);
			record[kReferenceSource] = referenceSource(pair.leaderIdentity) + " -> " + referenceSource(pair.followerIdentity);
			record[kNotes] = pair.adjacent ? "governing resource" : "nonadjacent ordered constraint; governing resource";
			rows.push_back(std::move(record));
		}
	}
	for (const auto& row : result.compression) {
		const auto appendCompressionRecord = [&](const CapacityCompressionEvidence* governing) {
			const std::string predecessor = governing ? governing->predecessorIdentity : std::string();
			std::vector<std::string> record(kColumnCount);
			record[kRecordType] = "compression";
			record[kLeader] = predecessor;
			record[kIdentity] = row.identity;
			record[kOperatingCode] = row.operatingCode;
			record[kOriginalReference] = csv::formatDouble(row.originalReference);
			record[kScheduledReference] = csv::formatDouble(row.scheduledReference);
			record[kCompressedReference] = csv::formatDouble(row.compressedReference);
			record[kShift] = csv::formatDouble(row.shift);
			record[kBlock] = governing ? capacityEvidenceText(governing->governingEvidence) : std::string();
			record[kSection] = sectionLabel.toStdString();
			record[kReferenceLabel] = referenceLabel(row.identity);
			record[kReferenceSource] = referenceSource(row.identity);
			record[kNotes] = "governing predecessor=" + predecessor;
			rows.push_back(std::move(record));
		};
		if (row.governingPredecessors.empty())
			appendCompressionRecord(nullptr);
		else
			for (const auto& governing : row.governingPredecessors)
				appendCompressionRecord(&governing);
	}
	for (const auto& critical : result.criticalBlocks) {
		std::vector<std::string> record(kColumnCount);
		record[kRecordType] = "critical";
		record[kLeader] = critical.leaderIdentity;
		record[kFollower] = critical.followerIdentity;
		record[kBlock] = critical.leaderBlockId + " / " + critical.followerBlockId;
		record[kGap] = csv::formatDouble(critical.gap);
		record[kSection] = sectionLabel.toStdString();
		record[kReferenceLabel] = referenceLabel(critical.leaderIdentity) + " -> " + referenceLabel(critical.followerIdentity);
		record[kReferenceSource] = referenceSource(critical.leaderIdentity) + " -> " + referenceSource(critical.followerIdentity);
		record[kNotes] = "capacity critical block";
		rows.push_back(std::move(record));
	}
	return csv::makeDocument(header, rows);
}

std::string capacityCsvFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope) {
	if (scope.routeIndex < 0 || !capacityScopeIssueFrom(data, scope).empty())
		return {};
	return capacityAnalysisCsv(capacityAnalysisFrom(data, scope), capacityScopeLabelFrom(data, scope));
}
